#pragma once

#include "transport/test/coroutine_util.h"
#include "transport/transport.h"

#include <gmock/gmock.h>

namespace transport {

class TransportMock : public Transport {
 public:
  TransportMock() {
    using namespace testing;

    ON_CALL(*this, open()).WillByDefault(CoReturn(ERR_FAILED));
    ON_CALL(*this, close()).WillByDefault(CoReturn(ERR_FAILED));

    ON_CALL(*this, read(/*buffer=*/_))
        .WillByDefault(CoReturn(ErrorOr<size_t>{ERR_ABORTED}));

    ON_CALL(*this, write(/*buffer=*/_))
        .WillByDefault(CoReturn(ErrorOr<size_t>{ERR_ABORTED}));

    ON_CALL(*this, get_executor())
        .WillByDefault(Return(boost::asio::system_executor{}));
  }

  ~TransportMock() { Destroy(); }

  MOCK_METHOD(void, Destroy, ());

  MOCK_METHOD(awaitable<Error>, open, (), (override));
  MOCK_METHOD(awaitable<Error>, close, (), (override));
  MOCK_METHOD(awaitable<ErrorOr<any_transport>>, accept, (), (override));

  MOCK_METHOD(awaitable<ErrorOr<size_t>>,
              read,
              (std::span<char> buffer),
              (override));

  MOCK_METHOD(awaitable<ErrorOr<size_t>>,
              write,
              (std::span<const char> buffer),
              (override));

  MOCK_METHOD(std::string, name, (), (const override));
  MOCK_METHOD(bool, message_oriented, (), (const override));
  MOCK_METHOD(bool, connected, (), (const override));
  MOCK_METHOD(bool, active, (), (const override));
  MOCK_METHOD(Executor, get_executor, (), (const override));
};

}  // namespace transport
