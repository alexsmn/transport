#include "net/transport_factory_impl.h"

#include "base/strings/sys_string_conversions.h"
#include "net/asio_tcp_transport.h"
#include "net/transport_string.h"

#if defined(OS_WIN)
#include "net/pipe_transport.h"
#include "net/serial_transport.h"
#endif

#if defined(OS_WIN)
#include <Windows.h>
#endif

namespace net {

namespace {

static boost::asio::serial_port::parity::type ParseParity(
    const std::string& str) {
  if (_stricmp(str.c_str(), "No") == 0)
    return boost::asio::serial_port::parity::none;
  else if (_stricmp(str.c_str(), "Even") == 0)
    return boost::asio::serial_port::parity::even;
  else if (_stricmp(str.c_str(), "Odd") == 0)
    return boost::asio::serial_port::parity::odd;
  else
    return boost::asio::serial_port::parity::none;
}

static boost::asio::serial_port::stop_bits::type ParseStopBits(
    const std::string& str) {
  if (str == "1")
    return boost::asio::serial_port::stop_bits::one;
  else if (str == "1.5")
    return boost::asio::serial_port::stop_bits::onepointfive;
  else if (str == "2")
    return boost::asio::serial_port::stop_bits::two;
  else
    return boost::asio::serial_port::stop_bits::one;
}

}  // namespace

TransportFactoryImpl::TransportFactoryImpl(boost::asio::io_context& io_context)
    : io_context_{io_context} {}

std::unique_ptr<Transport> TransportFactoryImpl::CreateTransport(
    const TransportString& ts,
    Logger* logger) {
  auto protocol = ts.GetProtocol();
  bool active = ts.IsActive();

  if (protocol == TransportString::PROTOCOL_COUNT)
    protocol = TransportString::TCP;

  if (protocol == TransportString::TCP) {
    // TCP;Active;Host=localhost;Port=3000
    auto host = ts.GetParamStr(TransportString::kParamHost);

    int port = ts.GetParamInt(TransportString::kParamPort);
    if (port <= 0) {
      LOG(WARNING) << "TCP port is not specified";
      return NULL;
    }

    auto transport = std::make_unique<AsioTcpTransport>(io_context_);
    transport->host = host;
    transport->service = std::to_string(port);
    transport->active = active;
    return std::move(transport);

  } else if (protocol == TransportString::SERIAL) {
    // SERIAL;Name=COM2

    auto device = ts.GetParamStr(TransportString::kParamName);
    if (device.empty()) {
      LOG(WARNING) << "Serial port name is not specified";
      return NULL;
    }

    SerialTransport::Options options;
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

    return std::make_unique<SerialTransport>(io_context_, device, options);

  } else if (protocol == TransportString::PIPE) {
#ifdef OS_WIN
    // Protocol=PIPE;Mode=Active;Name=mypipe

    base::string16 name =
        base::SysNativeMBToWide(ts.GetParamStr(TransportString::kParamName));
    if (name.empty()) {
      LOG(WARNING) << "Pipe name is not specified";
      return NULL;
    }

    auto transport = std::make_unique<PipeTransport>(io_context_);
    transport->Init(L"\\\\.\\pipe\\" + name, !active);
    return transport;

#else
    LOG(WARNING) << "Pipes are supported only under Windows";
    return nullptr;
#endif

  } else {
    return NULL;
  }
}

}  // namespace net
