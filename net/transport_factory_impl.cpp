#include "net/transport_factory_impl.h"

#include "net/inprocess_transport.h"
#include "net/logger.h"
#include "net/serial_transport.h"
#include "net/tcp_transport.h"
#include "net/transport_string.h"
#include "net/udp_socket_impl.h"
#include "net/udp_transport.h"
#include "net/websocket_transport.h"

#if defined(OS_WIN)
#include "net/pipe_transport.h"
#endif

#include <boost/algorithm/string/predicate.hpp>
#include <boost/locale/encoding_utf.hpp>

#if defined(OS_WIN)
#include <Windows.h>
#endif

namespace net {

namespace {

boost::asio::serial_port::parity::type ParseParity(std::string_view str) {
  if (boost::iequals(str, "No")) {
    return boost::asio::serial_port::parity::none;
  }
  if (boost::iequals(str, "Even")) {
    return boost::asio::serial_port::parity::even;
  }
  if (boost::iequals(str, "Odd")) {
    return boost::asio::serial_port::parity::odd;
  }
  throw std::invalid_argument{"Wrong parity string"};
}

boost::asio::serial_port::stop_bits::type ParseStopBits(std::string_view str) {
  if (str == "1") {
    return boost::asio::serial_port::stop_bits::one;
  }
  if (str == "1.5") {
    return boost::asio::serial_port::stop_bits::onepointfive;
  }
  if (str == "2") {
    return boost::asio::serial_port::stop_bits::two;
  }
  throw std::invalid_argument{"Wrong stop bits string"};
}

boost::asio::serial_port::flow_control::type ParseFlowControl(
    std::string_view str) {
  if (str == TransportString::kFlowControlNone) {
    return boost::asio::serial_port::flow_control::none;
  }
  if (str == TransportString::kFlowControlSoftware) {
    return boost::asio::serial_port::flow_control::software;
  }
  if (str == TransportString::kFlowControlHardware) {
    return boost::asio::serial_port::flow_control::hardware;
  }
  throw std::invalid_argument{"Wrong flow control string"};
}

}  // namespace

std::shared_ptr<TransportFactory> CreateTransportFactory() {
  struct Holder {
    boost::asio::io_context io_context;
    TransportFactoryImpl transport_factory{io_context};
    std::jthread thread{[this] { io_context.run(); }};
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type>
        work{io_context.get_executor()};
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

std::unique_ptr<Transport> TransportFactoryImpl::CreateTransport(
    const TransportString& transport_string,
    const net::Executor& executor,
    std::shared_ptr<const Logger> logger) {
  if (!logger) {
    logger = NullLogger::GetInstance();
  }

  logger->WriteF(LogSeverity::Normal, "Create transport: %s",
                 transport_string.ToString().c_str());

  auto protocol = transport_string.GetProtocol();
  bool active = transport_string.IsActive();

  if (protocol == TransportString::PROTOCOL_COUNT) {
    protocol = TransportString::TCP;
  }

  if (protocol == TransportString::TCP) {
    // TCP;Active;Host=localhost;Port=3000
    auto host = transport_string.GetParamStr(TransportString::kParamHost);

    int port = transport_string.GetParamInt(TransportString::kParamPort);
    if (port <= 0) {
      logger->Write(LogSeverity::Warning, "TCP port is not specified");
      return nullptr;
    }

    return std::make_unique<AsioTcpTransport>(executor, std::move(logger),
                                              std::string{host},
                                              std::to_string(port), active);
  }

  if (protocol == TransportString::UDP) {
    // UDP;Passive;Host=0.0.0.0;Port=3000
    auto host = transport_string.GetParamStr(TransportString::kParamHost);

    int port = transport_string.GetParamInt(TransportString::kParamPort);
    if (port <= 0) {
      logger->Write(LogSeverity::Warning, "UDP port is not specified");
      return nullptr;
    }

    return std::make_unique<AsioUdpTransport>(
        executor, std::move(logger), udp_socket_factory_, std::string{host},
        std::to_string(port), active);
  }

  if (protocol == TransportString::SERIAL) {
    // SERIAL;Name=COM2

    const std::string_view device =
        transport_string.GetParamStr(TransportString::kParamName);
    if (device.empty()) {
      logger->Write(LogSeverity::Warning, "Serial port name is not specified");
      return nullptr;
    }

    SerialTransport::Options options;

    try {
      if (transport_string.HasParam(TransportString::kParamBaudRate)) {
        options.baud_rate.emplace(
            transport_string.GetParamInt(TransportString::kParamBaudRate));
      }
      if (transport_string.HasParam(TransportString::kParamByteSize)) {
        options.character_size.emplace(
            transport_string.GetParamInt(TransportString::kParamByteSize));
      }
      if (transport_string.HasParam(TransportString::kParamParity)) {
        options.parity.emplace(ParseParity(
            transport_string.GetParamStr(TransportString::kParamParity)));
      }
      if (transport_string.HasParam(TransportString::kParamStopBits)) {
        options.stop_bits.emplace(ParseStopBits(
            transport_string.GetParamStr(TransportString::kParamStopBits)));
      }
      if (transport_string.HasParam(TransportString::kParamFlowControl)) {
        options.flow_control.emplace(ParseFlowControl(
            transport_string.GetParamStr(TransportString::kParamFlowControl)));
      }
    } catch (const std::runtime_error& e) {
      logger->WriteF(LogSeverity::Warning, "Error: %s", e.what());
      return nullptr;
    }

    return std::make_unique<SerialTransport>(executor, std::move(logger),
                                             std::string{device}, options);
  }

  if (protocol == TransportString::PIPE) {
#ifdef OS_WIN
    // Protocol=PIPE;Mode=Active;Name=mypipe

    const auto& name = boost::locale::conv::utf_to_utf<wchar_t>(
        std::string{transport_string.GetParamStr(TransportString::kParamName)});
    if (name.empty()) {
      logger->Write(LogSeverity::Warning, "Pipe name is not specified");
      return nullptr;
    }

    auto transport = std::make_unique<PipeTransport>(executor);
    transport->Init(LR"(\\.\pipe\)" + name, !active);
    return transport;

#else
    logger->Write(LogSeverity::Warning,
                  "Pipes are supported only under Windows");
    return nullptr;
#endif
  }

  if (protocol == TransportString::WEB_SOCKET) {
    // WS;Passive;Host=0.0.0.0;Port=3000
    auto host = transport_string.GetParamStr(TransportString::kParamHost);

    int port = transport_string.GetParamInt(TransportString::kParamPort);
    if (port <= 0) {
      logger->Write(LogSeverity::Warning, "UDP port is not specified");
      return nullptr;
    }

    return std::make_unique<WebSocketTransport>(io_context_, std::string{host},
                                                port);
  }

  if (protocol == TransportString::INPROCESS) {
    if (!inprocess_transport_host_) {
      inprocess_transport_host_ = std::make_unique<InprocessTransportHost>();
    }

    // INPROCESS;Passive;Name=Abc
    auto name = transport_string.GetParamStr(TransportString::kParamName);

    return active ? inprocess_transport_host_->CreateClient(name)
                  : inprocess_transport_host_->CreateServer(name);
  }

  return nullptr;
}

}  // namespace net
