#pragma once

#include "net/transport.h"

#include <gmock/gmock.h>

namespace net {

class TransportMock : public Transport {
 public:
  MOCK_METHOD(void, Open, (const Handlers& handlers), (override));
  MOCK_METHOD(void, Close, (), (override));
  MOCK_METHOD(int, Read, (std::span<char> data), (override));
  MOCK_METHOD(int, Write, (std::span<const char> data), (override));
  MOCK_METHOD(std::string, GetName, (), (const override));
  MOCK_METHOD(bool, IsMessageOriented, (), (const override));
  MOCK_METHOD(bool, IsConnected, (), (const override));
  MOCK_METHOD(bool, IsActive, (), (const override));
};

}  // namespace net
