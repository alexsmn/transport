#include "transport/transport_factory_impl.h"

#include "transport/inprocess_transport.h"
#include "transport/log.h"
#include "transport/serial_transport.h"
#include "transport/tcp_transport.h"
#include "transport/transport_string.h"
#include "transport/udp_socket_impl.h"
#include "transport/udp_transport.h"
#include "transport/websocket_transport.h"

#if defined(OS_WIN)
#include "transport/pipe_transport.h"
#endif

#include <boost/algorithm/string/predicate.hpp>
#include <boost/locale/encoding_utf.hpp>

#if defined(OS_WIN)
#include <Windows.h>
#endif

namespace transport {

namespace {

boost::asio::serial_port::parity::type ParseParity(std::string_view str) {
  if (boost::iequals(str, "No"))
    return boost::asio::serial_port::parity::none;
  else if (boost::iequals(str, "Even"))
    return boost::asio::serial_port::parity::even;
  else if (boost::iequals(str, "Odd"))
    return boost::asio::serial_port::parity::odd;
  else
    throw std::invalid_argument{"Wrong parity string"};
}

boost::asio::serial_port::stop_bits::type ParseStopBits(std::string_view str) {
  if (str == "1")
    return boost::asio::serial_port::stop_bits::one;
  else if (str == "1.5")
    return boost::asio::serial_port::stop_bits::onepointfive;
  else if (str == "2")
    return boost::asio::serial_port::stop_bits::two;
  else
    throw std::invalid_argument{"Wrong stop bits string"};
}

boost::asio::serial_port::flow_control::type ParseFlowControl(
    std::string_view str) {
  if (str == TransportString::kFlowControlNone)
    return boost::asio::serial_port::flow_control::none;
  else if (str == TransportString::kFlowControlSoftware)
    return boost::asio::serial_port::flow_control::software;
  else if (str == TransportString::kFlowControlHardware)
    return boost::asio::serial_port::flow_control::hardware;
  else
    throw std::invalid_argument{"Wrong flow control string"};
}

}  // namespace

std::shared_ptr<TransportFactory> CreateTransportFactory() {
  struct Holder {
    boost::asio::io_context io_context;
    TransportFactoryImpl transport_factory{io_context};
    std::jthread thread{[this] { io_context.run(); }};
    std::optional<boost::asio::io_context::work> work{io_context};
  };

  auto holder = std::make_shared<Holder>();
  return std::shared_ptr<TransportFactory>(holder, &holder->transport_factory);
}

// TransportFactoryImpl

TransportFactoryImpl::TransportFactoryImpl(boost::asio::io_context& io_context)
    : io_context_{io_context} {
  udp_socket_factory_ =
      [](UdpSocketContext&& context) -> std::shared_ptr<UdpSocket> {
    return std::make_shared<UdpSocketImpl>(std::move(context));
  };
}

TransportFactoryImpl::~TransportFactoryImpl() = default;

ErrorOr<any_transport> TransportFactoryImpl::CreateTransport(
    const TransportString& transport_string,
    const Executor& executor,
    const log_source& log) {
  log.writef(LogSeverity::Normal, "Create transport: %s",
             transport_string.ToString().c_str());

  auto protocol = transport_string.GetProtocol();
  bool active = transport_string.active();

  if (protocol == TransportString::PROTOCOL_COUNT)
    protocol = TransportString::TCP;

  if (protocol == TransportString::TCP) {
    // TCP;Active;Host=localhost;Port=3000
    auto host = transport_string.GetParamStr(TransportString::kParamHost);

    int port = transport_string.GetParamInt(TransportString::kParamPort);
    if (port <= 0) {
      log.write(LogSeverity::Warning, "TCP port is not specified");
      return ERR_INVALID_ARGUMENT;
    }

    return active
               ? any_transport{std::make_unique<ActiveTcpTransport>(
                     executor, log, std::string{host}, std::to_string(port))}
               : any_transport{std::make_unique<PassiveTcpTransport>(
                     executor, log, std::string{host}, std::to_string(port))};

  } else if (protocol == TransportString::UDP) {
    // UDP;Passive;Host=0.0.0.0;Port=3000
    auto host = transport_string.GetParamStr(TransportString::kParamHost);

    int port = transport_string.GetParamInt(TransportString::kParamPort);
    if (port <= 0) {
      log.write(LogSeverity::Warning, "UDP port is not specified");
      return ERR_INVALID_ARGUMENT;
    }

    return active ? any_transport{std::make_unique<ActiveUdpTransport>(
                        executor, log, udp_socket_factory_, std::string{host},
                        std::to_string(port))}
                  : any_transport{std::make_unique<PassiveUdpTransport>(
                        executor, log, udp_socket_factory_, std::string{host},
                        std::to_string(port))};

  } else if (protocol == TransportString::SERIAL) {
    // SERIAL;Name=COM2

    const std::string_view device =
        transport_string.GetParamStr(TransportString::kParamName);
    if (device.empty()) {
      log.write(LogSeverity::Warning, "Serial port name is not specified");
      return ERR_INVALID_ARGUMENT;
    }

    SerialTransport::Options options;

    try {
      if (transport_string.HasParam(TransportString::kParamBaudRate))
        options.baud_rate.emplace(
            transport_string.GetParamInt(TransportString::kParamBaudRate));
      if (transport_string.HasParam(TransportString::kParamByteSize))
        options.character_size.emplace(
            transport_string.GetParamInt(TransportString::kParamByteSize));
      if (transport_string.HasParam(TransportString::kParamParity))
        options.parity.emplace(ParseParity(
            transport_string.GetParamStr(TransportString::kParamParity)));
      if (transport_string.HasParam(TransportString::kParamStopBits))
        options.stop_bits.emplace(ParseStopBits(
            transport_string.GetParamStr(TransportString::kParamStopBits)));
      if (transport_string.HasParam(TransportString::kParamFlowControl))
        options.flow_control.emplace(ParseFlowControl(
            transport_string.GetParamStr(TransportString::kParamFlowControl)));

    } catch (const std::runtime_error& e) {
      log.writef(LogSeverity::Warning, "Error: %s", e.what());
      return ERR_INVALID_ARGUMENT;
    }

    return any_transport{std::make_unique<SerialTransport>(
        executor, std::move(log), std::string{device}, options)};

  } else if (protocol == TransportString::PIPE) {
#ifdef OS_WIN
    // Protocol=PIPE;Mode=Active;Name=mypipe

    const auto& name = boost::locale::conv::utf_to_utf<wchar_t>(
        std::string{transport_string.GetParamStr(TransportString::kParamName)});
    if (name.empty()) {
      log.write(LogSeverity::Warning, "Pipe name is not specified");
      return nullptr;
    }

    auto transport = std::make_unique<PipeTransport>(executor);
    transport->Init(LR"(\\.\pipe\)" + name, !active);
    return transport;

#else
    log.write(LogSeverity::Warning, "Pipes are supported only under Windows");
    return ERR_NOT_IMPLEMENTED;
#endif

  } else if (protocol == TransportString::WEB_SOCKET) {
    // WS;Passive;Host=0.0.0.0;Port=3000
    auto host = transport_string.GetParamStr(TransportString::kParamHost);

    int port = transport_string.GetParamInt(TransportString::kParamPort);
    if (port <= 0) {
      log.write(LogSeverity::Warning, "UDP port is not specified");
      return ERR_INVALID_ARGUMENT;
    }

    return any_transport{std::make_unique<WebSocketTransport>(
        executor, std::string{host}, port)};

  } else if (protocol == TransportString::INPROCESS) {
    if (!inprocess_transport_host_) {
      inprocess_transport_host_ = std::make_unique<InprocessTransportHost>();
    }

    // INPROCESS;Passive;Name=Abc
    auto name = transport_string.GetParamStr(TransportString::kParamName);

    return active ? inprocess_transport_host_->CreateClient(executor, name)
                  : inprocess_transport_host_->CreateServer(executor, name);

  } else {
    log.write(LogSeverity::Warning,
              "Cannot create transport with unknown protocol");
    return ERR_INVALID_ARGUMENT;
  }
}

}  // namespace transport
