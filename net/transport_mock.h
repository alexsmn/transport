#pragma once

#include "net/test/coroutine_util.h"
#include "net/transport.h"

#include <gmock/gmock.h>

namespace net {

class TransportMock : public Transport {
 public:
  TransportMock() {
    using namespace testing;

    ON_CALL(*this, Open()).WillByDefault(CoReturn(ERR_FAILED));

    ON_CALL(*this, Read(/*buffer=*/_))
        .WillByDefault(CoReturn(ErrorOr<size_t>{ERR_ABORTED}));

    ON_CALL(*this, Write(/*buffer=*/_))
        .WillByDefault(CoReturn(ErrorOr<size_t>{ERR_ABORTED}));
  }

  ~TransportMock() { Destroy(); }

  MOCK_METHOD(void, Destroy, ());

  MOCK_METHOD(awaitable<Error>, Open, (), (override));

  MOCK_METHOD(void, Close, (), (override));

  MOCK_METHOD(awaitable<ErrorOr<std::unique_ptr<Transport>>>,
              Accept,
              (),
              (override));

  MOCK_METHOD(awaitable<ErrorOr<size_t>>,
              Read,
              (std::span<char> buffer),
              (override));

  MOCK_METHOD(awaitable<ErrorOr<size_t>>,
              Write,
              (std::span<const char> buffer),
              (override));

  MOCK_METHOD(std::string, GetName, (), (const override));
  MOCK_METHOD(bool, IsMessageOriented, (), (const override));
  MOCK_METHOD(bool, IsConnected, (), (const override));
  MOCK_METHOD(bool, IsActive, (), (const override));
  MOCK_METHOD(Executor, GetExecutor, (), (const override));
};

}  // namespace net
