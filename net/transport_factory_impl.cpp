#include "net/transport_factory_impl.h"

#include "base/strings/string_util.h"
#include "base/strings/sys_string_conversions.h"
#include "net/logger.h"
#include "net/serial_transport.h"
#include "net/tcp_transport.h"
#include "net/transport_string.h"
#include "net/udp_socket_impl.h"
#include "net/udp_transport.h"

#if defined(OS_WIN)
#include "net/pipe_transport.h"
#endif

#if defined(OS_WIN)
#include <Windows.h>
#endif

namespace net {

namespace {

inline constexpr base::StringPiece AsStringPiece(std::string_view str) {
  return {str.data(), str.size()};
}

boost::asio::serial_port::parity::type ParseParity(std::string_view str) {
  if (base::EqualsCaseInsensitiveASCII(AsStringPiece(str), "No"))
    return boost::asio::serial_port::parity::none;
  else if (base::EqualsCaseInsensitiveASCII(AsStringPiece(str), "Even"))
    return boost::asio::serial_port::parity::even;
  else if (base::EqualsCaseInsensitiveASCII(AsStringPiece(str), "Odd"))
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
    ~Holder() {
      work.reset();
      thread.join();
    }

    boost::asio::io_context io_context;
    TransportFactoryImpl transport_factory{io_context};
    std::thread thread{[this] { io_context.run(); }};
    std::optional<boost::asio::io_context::work> work{io_context};
  };

  auto holder = std::make_shared<Holder>();
  return std::shared_ptr<TransportFactory>(holder, &holder->transport_factory);
}

// TransportFactoryImpl

TransportFactoryImpl::TransportFactoryImpl(boost::asio::io_context& io_context)
    : io_context_{io_context} {
  udp_socket_factory_ =
      [&io_context](UdpSocketContext&& context) -> std::shared_ptr<UdpSocket> {
    return std::make_shared<UdpSocketImpl>(io_context, std::move(context));
  };
}

std::unique_ptr<Transport> TransportFactoryImpl::CreateTransport(
    const TransportString& ts,
    std::shared_ptr<const Logger> logger) {
  logger->WriteF(LogSeverity::Normal, "Create transport: %s",
                 ts.ToString().c_str());

  auto protocol = ts.GetProtocol();
  bool active = ts.IsActive();

  if (protocol == TransportString::PROTOCOL_COUNT)
    protocol = TransportString::TCP;

  if (protocol == TransportString::TCP) {
    // TCP;Active;Host=localhost;Port=3000
    auto host = ts.GetParamStr(TransportString::kParamHost);

    int port = ts.GetParamInt(TransportString::kParamPort);
    if (port <= 0) {
      logger->Write(LogSeverity::Warning, "TCP port is not specified");
      return nullptr;
    }

    auto transport =
        std::make_unique<AsioTcpTransport>(io_context_, std::move(logger));
    transport->host = host;
    transport->service = std::to_string(port);
    transport->active = active;
    return std::move(transport);

  } else if (protocol == TransportString::UDP) {
    // UDP;Passive;Host=0.0.0.0;Port=3000
    auto host = ts.GetParamStr(TransportString::kParamHost);

    int port = ts.GetParamInt(TransportString::kParamPort);
    if (port <= 0) {
      logger->Write(LogSeverity::Warning, "UDP port is not specified");
      return nullptr;
    }

    auto transport = std::make_unique<AsioUdpTransport>(std::move(logger),
                                                        udp_socket_factory_);
    transport->host = host;
    transport->service = std::to_string(port);
    transport->active = active;
    return std::move(transport);

  } else if (protocol == TransportString::SERIAL) {
    // SERIAL;Name=COM2

    const std::string_view device = ts.GetParamStr(TransportString::kParamName);
    if (device.empty()) {
      logger->Write(LogSeverity::Warning, "Serial port name is not specified");
      return nullptr;
    }

    SerialTransport::Options options;

    try {
      if (ts.HasParam(TransportString::kParamBaudRate))
        options.baud_rate.emplace(
            ts.GetParamInt(TransportString::kParamBaudRate));
      if (ts.HasParam(TransportString::kParamByteSize))
        options.character_size.emplace(
            ts.GetParamInt(TransportString::kParamByteSize));
      if (ts.HasParam(TransportString::kParamParity))
        options.parity.emplace(
            ParseParity(ts.GetParamStr(TransportString::kParamParity)));
      if (ts.HasParam(TransportString::kParamStopBits))
        options.stop_bits.emplace(
            ParseStopBits(ts.GetParamStr(TransportString::kParamStopBits)));
      if (ts.HasParam(TransportString::kParamFlowControl))
        options.flow_control.emplace(ParseFlowControl(
            ts.GetParamStr(TransportString::kParamFlowControl)));

    } catch (const std::runtime_error& e) {
      logger->WriteF(LogSeverity::Warning, "Error: %s", e.what());
      return nullptr;
    }

    return std::make_unique<SerialTransport>(io_context_, std::move(logger),
                                             std::string{device}, options);

  } else if (protocol == TransportString::PIPE) {
#ifdef OS_WIN
    // Protocol=PIPE;Mode=Active;Name=mypipe

    const auto& name = base::SysNativeMBToWide(
        AsStringPiece(ts.GetParamStr(TransportString::kParamName)));
    if (name.empty()) {
      logger->Write(LogSeverity::Warning, "Pipe name is not specified");
      return nullptr;
    }

    auto transport = std::make_unique<PipeTransport>(io_context_);
    transport->Init(L"\\\\.\\pipe\\" + name, !active);
    return transport;

#else
    logger->Write(LogSeverity::Warning,
                  "Pipes are supported only under Windows");
    return nullptr;
#endif

  } else {
    return nullptr;
  }
}

}  // namespace net
