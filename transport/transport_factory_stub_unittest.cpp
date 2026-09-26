#include "transport/transport_factory_stub.h"

#include "transport/dummy_transport.h"

#include <boost/asio/system_executor.hpp>
#include <gmock/gmock.h>

#include <memory>

namespace transport {
namespace {

// The stub was written against a TransportFactory that returned
// std::unique_ptr<Transport> and took a Logger, and stopped compiling when the
// interface moved to any_transport; nothing included it, so nothing noticed.
// Building it here is the check that it keeps matching the interface.
TEST(StubTransportFactory, HandsOutARegisteredTransportOnce) {
  const TransportString transport_string{"TCP;Active;Host=localhost;Port=1"};
  const std::string name = transport_string.ToString();

  StubTransportFactory factory;
  factory.AddTransport(name, any_transport{std::make_unique<DummyTransport>()});

  const executor executor = boost::asio::system_executor{};
  auto transport = factory.CreateTransport(transport_string, executor);
  ASSERT_TRUE(transport.ok());
  EXPECT_EQ(transport->name(), "DummyTransport");

  EXPECT_EQ(factory.CreateTransport(transport_string, executor).error(),
            ERR_INVALID_ARGUMENT);
}

TEST(StubTransportFactory, AnUnknownNameIsAnInvalidArgument) {
  StubTransportFactory factory;
  EXPECT_EQ(factory
                .CreateTransport(TransportString{"TCP;Active;Host=h;Port=1"},
                                 boost::asio::system_executor{})
                .error(),
            ERR_INVALID_ARGUMENT);
}

}  // namespace
}  // namespace transport
