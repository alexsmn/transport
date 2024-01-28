#pragma once

#include "net/transport.h"

#include <gmock/gmock.h>

namespace net {

class TransportMock : public Transport {
 public:
  TransportMock() {
    using namespace testing;

    ON_CALL(*this, Open(_)).WillByDefault(Return(make_resolved_promise()));

    ON_CALL(*this, Write(_))
        .WillByDefault(Return(make_error_promise<size_t>(net::ERR_ABORTED)));
  }

  ~TransportMock() { Destroy(); }

  MOCK_METHOD(void, Destroy, ());

  MOCK_METHOD(promise<void>, Open, (const Handlers& handlers), (override));
  MOCK_METHOD(void, Close, (), (override));
  MOCK_METHOD(int, Read, (std::span<char> data), (override));
  MOCK_METHOD(promise<size_t>, Write, (std::span<const char> data), (override));
  MOCK_METHOD(std::string, GetName, (), (const override));
  MOCK_METHOD(bool, IsMessageOriented, (), (const override));
  MOCK_METHOD(bool, IsConnected, (), (const override));
  MOCK_METHOD(bool, IsActive, (), (const override));
};

}  // namespace net
