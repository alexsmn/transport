#pragma once

#include <map>
#include <string>
#include <string_view>

namespace transport {

class TransportString {
 public:
  enum Protocol {
    TCP,
    UDP,
    SERIAL,
    PIPE,
    WEB_SOCKET,
    INPROCESS,
    PROTOCOL_COUNT
  };

  TransportString() = default;
  explicit TransportString(std::string_view str);

  bool is_valid() const { return valid_; }

  bool HasParam(std::string_view name) const;
  std::string_view GetParamStr(std::string_view name) const;
  int GetParamInt(std::string_view name) const;
  bool active() const { return !HasParam(kParamPassive); }
  Protocol GetProtocol() const;

  TransportString& SetParam(std::string_view name);
  TransportString& SetParam(std::string_view name, std::string_view value);
  TransportString& SetParam(std::string_view name, int value);
  TransportString& SetActive(bool active);
  TransportString& SetProtocol(Protocol protocol);

  TransportString& RemoveParam(std::string_view name);

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

  using ParamMap = std::map<std::string, std::string, CompareNoCase>;

  ParamMap param_map_;

  bool valid_ = true;
};

}  // namespace transport
