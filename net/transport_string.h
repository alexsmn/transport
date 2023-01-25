#pragma once

#include "net/base/net_export.h"

#include <map>
#include <string>
#include <string_view>

namespace net {

class Transport;

class NET_EXPORT TransportString {
 public:
  enum Protocol { TCP, UDP, SERIAL, PIPE, WEB_SOCKET, PROTOCOL_COUNT };

  TransportString() : valid_(true) {}
  explicit TransportString(std::string_view str);

  bool is_valid() const { return valid_; }

  bool HasParam(std::string_view name) const;
  std::string_view GetParamStr(std::string_view name) const;
  int GetParamInt(std::string_view name) const;
  bool IsActive() const { return !HasParam(kParamPassive); }
  Protocol GetProtocol() const;

  void SetParam(std::string_view name);
  void SetParam(std::string_view name, std::string_view value);
  void SetParam(std::string_view name, int value);
  void SetActive(bool active);
  void SetProtocol(Protocol protocol);

  void RemoveParam(std::string_view name);

  std::string ToString() const;

  static int ParseSerialPortNumber(std::string_view str);

  static const char* kParamActive;
  static const char* kParamPassive;
  static const char* kParamHost;
  static const char* kParamPort;
  static const char* kParamName;
  static const char* kParamBaudRate;
  static const char* kParamByteSize;
  static const char* kParamParity;
  static const char* kParamStopBits;
  static const char* kParamFlowControl;

  static const char* kParamOrder[];

  static const std::string_view kFlowControlNone;
  static const std::string_view kFlowControlSoftware;
  static const std::string_view kFlowControlHardware;

 private:
  struct CompareNoCase {
    bool operator()(const std::string& left, const std::string& right) const;
  };

  typedef std::map<std::string, std::string, CompareNoCase> ParamMap;

  ParamMap param_map_;

  bool valid_ = true;
};

}  // namespace net
