#pragma once

#include "net/base/net_export.h"
#include "base/strings/string_util.h"

#include <map>
#include <string>

namespace net {

class Transport;

class NET_EXPORT TransportString {
 public:
  enum Protocol { TCP, UDP, SERIAL, PIPE, PROTOCOL_COUNT };
  
  TransportString() : valid_(true) {}
  explicit TransportString(const std::string& str);

  bool is_valid() const { return valid_; }
  
  bool HasParam(const std::string& name) const {
    return param_map_.find(name) != param_map_.end();
  }
  std::string GetParamStr(const std::string& name) const;
  int GetParamInt(const std::string& name) const;
  bool IsActive() const { return !HasParam(kParamPassive); }
  Protocol GetProtocol() const;

  void SetParam(const std::string& name) { param_map_[name]; }
  void SetParam(const std::string& name, const std::string& value) {
    param_map_[name] = value;
  }
  void SetParam(const std::string& name, int value);
  void SetActive(bool active);
  void SetProtocol(Protocol protocol);

  void RemoveParam(const std::string& name) {
    param_map_.erase(name);
  }
  
  std::string ToString() const;
  
  static int ParseSerialPortNumber(const std::string& str);

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
  
 private:
  struct CompareNoCase {
    bool operator()(const std::string& left, const std::string& right) const {
      return base::CompareCaseInsensitiveASCII(left, right) < 0;
    }
  };
 
  typedef std::map<std::string, std::string, CompareNoCase> ParamMap;

  ParamMap param_map_;

  bool valid_;
};

} // namespace net
