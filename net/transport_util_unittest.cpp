#include "net/transport_util.h"

#include <gmock/gmock.h>

using namespace testing;

namespace net {

class MakePromiseHandlersTest : public Test {
 public:
  MakePromiseHandlersTest();

 protected:
  Transport::Handlers handlers_;
  promise<void> promise_;

  StrictMock<MockFunction<void()>> on_open_;
  StrictMock<MockFunction<void(Error error)>> on_close_;
};

MakePromiseHandlersTest::MakePromiseHandlersTest() {
  auto [promise, handlers] =
      MakePromiseHandlers({.on_open = on_open_.AsStdFunction(),
                           .on_close = on_close_.AsStdFunction()});

  handlers_ = std::move(handlers);
  promise_ = std::move(promise);
}

TEST_F(MakePromiseHandlersTest, Open_TriggersOpenAndResolvesPromise) {
  EXPECT_CALL(on_open_, Call());

  handlers_.on_open();
  promise_.get();
}

TEST_F(MakePromiseHandlersTest, Close_TriggersCloseAndRejectsPromise) {
  Error error = ERR_CACHE_MISS;
  EXPECT_CALL(on_close_, Call(error));

  handlers_.on_close(error);
  EXPECT_THROW(promise_.get(), net::net_exception);
}

TEST_F(MakePromiseHandlersTest, OpenThenClose_TriggersCloseAndIgnoresPromise) {
  InSequence s;
  Error error = ERR_CACHE_MISS;
  EXPECT_CALL(on_open_, Call());
  handlers_.on_open();
  promise_.get();
  EXPECT_CALL(on_close_, Call(error));

  handlers_.on_close(error);
  promise_.get();
}

TEST_F(MakePromiseHandlersTest, ReleaseHandlersFromCallback_NoCrash) {
  EXPECT_CALL(on_open_, Call())
      .WillOnce(Assign(&handlers_, Transport::Handlers{}));

  handlers_.on_open();
  promise_.get();
}

}  // namespace net