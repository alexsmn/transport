#include "net/transport_factory.h"

#include "base/strings/sys_string_conversions.h"
#include "net/asio_tcp_transport.h"
#include "net/transport_string.h"

#ifdef OS_WIN
#include "net/pipe_transport.h"
#include "net/serial_transport.h"
#include "net/socket_transport.h"
#endif

#ifdef OS_WIN
#include <Windows.h>
#endif

namespace net {

namespace {

#ifdef OS_WIN

static BYTE ParseParity(const std::string& str) {
  if (_stricmp(str.c_str(), "No") == 0)
    return NOPARITY;
  else if (_stricmp(str.c_str(), "Even") == 0)
    return EVENPARITY;
  else if (_stricmp(str.c_str(), "Odd") == 0)
    return ODDPARITY;
  else if (_stricmp(str.c_str(), "Mark") == 0)
    return MARKPARITY;
  else if (_stricmp(str.c_str(), "Space") == 0)
    return SPACEPARITY;
  else
    return NOPARITY;
}

static BYTE ParseStopBits(const std::string& str) {
  if (str == "1")
    return ONESTOPBIT;
  else if (str == "1.5")
    return ONE5STOPBITS;
  else if (str == "2")
    return TWOSTOPBITS;
  else
    return ONESTOPBIT;
}

#endif // OS_WIN

} // namespace

TransportFactoryImpl::TransportFactoryImpl(boost::asio::io_service& io_service)
    : io_service_{io_service} {
}

std::unique_ptr<Transport> TransportFactoryImpl::CreateTransport(const TransportString& ts) {
  auto protocol = ts.GetProtocol();
  bool active = ts.IsActive();
  
  if (protocol == TransportString::PROTOCOL_COUNT)
    protocol = TransportString::TCP;
    
  if (protocol == TransportString::TCP) {
    // TCP;Active;Host=localhost;Port=3000
    auto host = ts.GetParamStr(TransportString::kParamHost);
    if (host.empty())
      host = "localhost";

    int port = ts.GetParamInt(TransportString::kParamPort);
    if (port <= 0) {
      LOG(WARNING) << "TCP port is not specified";
      return NULL;
    }
  
#ifdef OS_WIN
    {
      auto transport = std::make_unique<SocketTransport>();
      transport->set_active(active);
      transport->host_ = host;
      transport->port_ = static_cast<unsigned short>(port);
      return std::move(transport);
    }
#endif

    if (active) {
      auto transport = std::make_unique<AsioTcpTransport>(io_service_);
      transport->host = host;
      transport->service = std::to_string(port);
      return std::move(transport);
    } else {
      LOG(WARNING) << "Passive TCP transport is not supported";
      return nullptr;
    }

  } else if (protocol == TransportString::SERIAL) {
#ifdef OS_WIN
    // SERIAL;Name=COM2
  
    auto name = ts.GetParamStr(TransportString::kParamName);
    if (name.empty()) {
      LOG(WARNING) << "Serial port name is not specified";
      return NULL;
    }
    
    auto transport = std::make_unique<SerialTransport>(io_service_);
    transport->m_file_name = "\\\\.\\" + name;
    
    COMMCONFIG config = { sizeof(config) };
    DWORD config_size = sizeof(config);
    GetDefaultCommConfig(base::SysNativeMBToWide(name).c_str(), &config, &config_size);
    
    DCB& dcb = config.dcb;
    if (ts.HasParam(TransportString::kParamBaudRate))
      dcb.BaudRate = ts.GetParamInt(TransportString::kParamBaudRate);
    if (ts.HasParam(TransportString::kParamByteSize))
      dcb.ByteSize = static_cast<BYTE>(ts.GetParamInt(TransportString::kParamByteSize));
    if (ts.HasParam(TransportString::kParamParity))
      dcb.Parity = ParseParity(ts.GetParamStr(TransportString::kParamParity));
    if (ts.HasParam(TransportString::kParamParity))
      dcb.StopBits = ParseStopBits(ts.GetParamStr(TransportString::kParamStopBits));
                         
    transport->m_dcb = dcb;
    return std::move(transport);

#else
    LOG(WARNING) << "Serial ports are not supported";
    return nullptr;
#endif

  } else if (protocol == TransportString::PIPE) {
#ifdef OS_WIN
    // Protocol=PIPE;Mode=Active;Name=mypipe
  
    base::string16 name = base::SysNativeMBToWide(ts.GetParamStr(TransportString::kParamName));
    if (name.empty()) {
      LOG(WARNING) << "Pipe name is not specified";
      return NULL;
    }
    
    auto transport = std::make_unique<PipeTransport>(io_service_);
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

} // namespace net
