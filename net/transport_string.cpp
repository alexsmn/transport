#include "net/transport_string.h"

#include "base/logging.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/sys_string_conversions.h"

#include <set>

namespace net {

const char* TransportString::kParamActive = "Active";
const char* TransportString::kParamPassive = "Passive";
const char* TransportString::kParamHost = "Host";
const char* TransportString::kParamPort = "Port";
const char* TransportString::kParamName = "Name";
const char* TransportString::kParamBaudRate = "BaudRate";
const char* TransportString::kParamByteSize = "ByteSize";
const char* TransportString::kParamParity = "Parity";
const char* TransportString::kParamStopBits = "StopBits";
const char* TransportString::kParamFlowControl = "FlowControl";

const char* TransportString::kParamOrder[] = {
    TransportString::kParamActive,
    TransportString::kParamPassive,
    TransportString::kParamHost,
    TransportString::kParamPort,
    TransportString::kParamName,
    TransportString::kParamBaudRate,
    TransportString::kParamParity,
    TransportString::kParamStopBits,
};

static const char kValueDelimiter = '=';
static const char kParamDelimiter = ';';

static const char* kProtocolNames[] = { "TCP",
                                        "UDP",
                                        "SERIAL",
                                        "PIPE", };

static void Trim(std::string& str) {
  while (!str.empty() && *str.begin() == ' ')
    str.erase(str.begin());
  while (!str.empty() && *--str.end() == ' ')
    str.erase(--str.end());
}

TransportString::TransportString(const std::string& str)
    : valid_(true) {
  std::string::size_type s = 0;
  while (s < str.length()) {
    std::string::size_type e = str.find_first_of(kParamDelimiter, s);
    if (e == std::string::npos)
      e = str.length();

    std::string::size_type v = str.find_first_of(kValueDelimiter, s);
    std::string value;
    if (v != std::string::npos && v < e)
      value = str.substr(v + 1, e - v - 1);
    else
      v = e;

    std::string param = str.substr(s, v - s);
    
    Trim(param);
    Trim(value);
    param_map_[param] = value;
    
    s = e + 1;
  }
}

TransportString::Protocol TransportString::GetProtocol() const {
  static_assert(arraysize(kProtocolNames) == PROTOCOL_COUNT,
                "NotEnoughProtocolNames");
  for (int i = 0; i < PROTOCOL_COUNT; ++i) {
    if (HasParam(kProtocolNames[i]))
      return static_cast<Protocol>(i);
  }
  return PROTOCOL_COUNT;
}

void TransportString::SetActive(bool active) {
  RemoveParam(kParamActive);
  RemoveParam(kParamPassive);
  SetParam(active ? kParamActive : kParamPassive);
}

void TransportString::SetProtocol(Protocol protocol) {
  for (int i = 0; i < PROTOCOL_COUNT; ++i)
    RemoveParam(kProtocolNames[i]);
  SetParam(kProtocolNames[protocol]);
}

void TransportString::SetParam(const std::string& name, int value) {
  SetParam(name, base::IntToString(value));
}

std::string TransportString::GetParamStr(const std::string& name) const {
  ParamMap::const_iterator i = param_map_.find(name);
  if (i != param_map_.end())
    return i->second;

  return std::string();
}

int TransportString::GetParamInt(const std::string& name) const {
  std::string str = GetParamStr(name);

  int value;
  if (base::StringToInt(str, &value))
    return value;

  return 0;
}

inline void AppendConnectionString(std::string& str, const std::string& param,
                                   const std::string& value) {
  if (!str.empty())
    str += ';';
    
  str += param;
  
  if (!value.empty()) {
    str += '=';
    str += value;
  }
}

std::string TransportString::ToString() const {
  // Fill parameters in predefined order.
  std::string str;  
  
  Protocol protocol = GetProtocol();
  if (protocol == PROTOCOL_COUNT)
    return str;

  typedef std::set<std::string, CompareNoCase> ParamSet;
  ParamSet unpassed_params;
  for (ParamMap::const_iterator i = param_map_.begin();
       i != param_map_.end(); ++i)
    unpassed_params.insert(i->first);
  
  AppendConnectionString(str, kProtocolNames[protocol], std::string());
  unpassed_params.erase(kProtocolNames[protocol]);

  for (int i = 0; i < arraysize(kParamOrder); ++i) {
    const std::string param = kParamOrder[i];
    unpassed_params.erase(param);
    if (HasParam(param)) {
      const std::string& value = GetParamStr(param);
      AppendConnectionString(str, param, value);
    }
  }
  
  for (ParamSet::iterator i = unpassed_params.begin(); i != unpassed_params.end(); ++i) {
    const std::string& param = *i;
    const std::string& value = GetParamStr(param);
    AppendConnectionString(str, param, value);
  }
  
  return str;
}

int TransportString::ParseSerialPortNumber(const std::string& str) {
  int value;
  if (sscanf(str.c_str(), "COM%u", &value) != 1)
    value = 0;
  return value;
}

} // namespace net
