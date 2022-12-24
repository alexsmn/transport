#include "net/transport_string.h"

#include <charconv>
#include <set>

namespace net {

namespace {

static const char kValueDelimiter = '=';
static const char kParamDelimiter = ';';

static const char* kProtocolNames[] = {
    "TCP",
    "UDP",
    "SERIAL",
    "PIPE",
};

static std::string_view Trim(std::string_view str) {
  auto f = str.find_first_not_of(' ');
  if (f == std::string_view::npos)
    return {};
  auto e = str.find_last_of(' ');
  str = str.substr(0, e);
  return str;
}

}  // namespace

// TransportString::CompareNoCase

bool TransportString::CompareNoCase::operator()(
    const std::string& left,
    const std::string& right) const {
  return _strcmpi(left.c_str(), right.c_str()) < 0;
}

// TransportString

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
    TransportString::kParamActive, TransportString::kParamPassive,
    TransportString::kParamHost,   TransportString::kParamPort,
    TransportString::kParamName,   TransportString::kParamBaudRate,
    TransportString::kParamParity, TransportString::kParamStopBits,
};

const std::string_view TransportString::kFlowControlNone = "No";
const std::string_view TransportString::kFlowControlSoftware = "XON/XOFF";
const std::string_view TransportString::kFlowControlHardware = "Hardware";

TransportString::TransportString(std::string_view str) {
  std::string::size_type s = 0;
  while (s < str.length()) {
    auto e = str.find_first_of(kParamDelimiter, s);
    if (e == std::string::npos)
      e = str.length();

    auto v = str.find_first_of(kValueDelimiter, s);
    std::string_view value;
    if (v != std::string_view::npos && v < e)
      value = str.substr(v + 1, e - v - 1);
    else
      v = e;

    auto param = str.substr(s, v - s);

    param = Trim(param);
    value = Trim(value);
    SetParam(param, value);

    s = e + 1;
  }
}

TransportString::Protocol TransportString::GetProtocol() const {
  static_assert(std::size(kProtocolNames) == PROTOCOL_COUNT,
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

void TransportString::SetParam(std::string_view name, int value) {
  SetParam(name, std::to_string(value));
}

std::string_view TransportString::GetParamStr(std::string_view name) const {
  auto i = param_map_.find(std::string{name});
  if (i != param_map_.end())
    return i->second;

  return {};
}

template <class T>
bool StringToNumber(std::string_view name, T& value) {
  auto [ptr, ec] =
      std::from_chars(name.data(), name.data() + name.size(), value);
  return ec == std::errc() && ptr == name.data() + name.size();
}

int TransportString::GetParamInt(std::string_view name) const {
  auto str = GetParamStr(name);

  int value = 0;
  if (StringToNumber(str, value))
    return value;

  return 0;
}

inline void AppendConnectionString(std::string& str,
                                   std::string_view param,
                                   std::string_view value) {
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

  typedef std::set<std::string, CompareNoCase> ParamSet;
  ParamSet unpassed_params;
  for (const auto& p : param_map_)
    unpassed_params.emplace(p.first);

  Protocol protocol = GetProtocol();
  if (protocol != PROTOCOL_COUNT) {
    AppendConnectionString(str, kProtocolNames[protocol], {});
    unpassed_params.erase(kProtocolNames[protocol]);
  }

  for (int i = 0; i < std::size(kParamOrder); ++i) {
    const auto& param = kParamOrder[i];
    unpassed_params.erase(param);
    if (HasParam(param)) {
      const auto& value = GetParamStr(param);
      AppendConnectionString(str, param, value);
    }
  }

  for (const auto& param : unpassed_params) {
    const auto& value = GetParamStr(param);
    AppendConnectionString(str, param, value);
  }

  return str;
}

int TransportString::ParseSerialPortNumber(std::string_view str) {
  const std::string_view kPrefix = "COM";

  if (!str.starts_with(kPrefix))
    return 0;

  auto number_string = str.substr(kPrefix.size());
  unsigned number = 0;
  if (!StringToNumber(number_string, number))
    return 0;

  return static_cast<int>(number);
}

void TransportString::SetParam(std::string_view name) {
  SetParam(name, std::string_view{});
}

void TransportString::SetParam(std::string_view name, std::string_view value) {
  param_map_[std::string{name}] = std::string{value};
}

void TransportString::RemoveParam(std::string_view name) {
  param_map_.erase(std::string{name});
}

bool TransportString::HasParam(std::string_view name) const {
  return param_map_.find(std::string{name}) != param_map_.end();
}

}  // namespace net
