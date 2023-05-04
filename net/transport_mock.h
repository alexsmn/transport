#pragma once

#include "net/transport.h"

#include <gmock/gmock.h>

namespace net {

class TransportMock : public Transport {
 public:
  MOCK_METHOD1(Open, Error(Delegate& delegate));
  MOCK_METHOD0(Close, void());
  MOCK_METHOD1(Read, int(std::span<char> data));
  MOCK_METHOD1(Write, int(std::span<const char> data));
  MOCK_CONST_METHOD0(GetName, std::string());
  MOCK_CONST_METHOD0(IsMessageOriented, bool());
  MOCK_CONST_METHOD0(IsConnected, bool());
  MOCK_CONST_METHOD0(IsActive, bool());
};

}  // namespace net
