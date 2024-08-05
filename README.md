# Transport
Asynchronous transport library based on coroutines.

The library provides a higher-level abstraction above various [Boost.Asio](https://www.boost.org/doc/libs/1_85_0/doc/html/boost_asio.html) objects, such as:

* TCP and UDP socket
* WebSocket
* Serial port
* Named pipe

It introduces a factory for constructing a transport object from a string description.

Echo server example:

```c++
ErrorOr<awaitable<void>> RunServer(boost::asio::io_context& io_context) {
  TransportFactoryImpl transport_factory{io_context};
  NET_ASSIGN_OR_CO_ERROR(auto server,
    transport_factory.CreateTransport(TransportString{“TCP;Passive;Port=1234”}));
  NET_CO_RETURN_IF_ERROR(co_await server.open());
  for (;;) {
    NET_ASSIGN_OR_CO_RETURN(auto accepted_transport,
                            co_await server.accept());
    boost::asio::co_spawn(io_context.get_executor(),
                          RunEcho(std::move(accepted_transport)),
                          boost::asio::detached);
  }
}

awaitable<Error> RunEcho(any_transport transport) {
  std::vector<char> buffer;

  for (;;) {
    buffer.resize(64);
    NET_ASSIGN_OR_CO_RETURN(auto bytes_read, co_await transport.read(buffer));
    if (bytes_read == 0) {
      // Graceful close.
      co_return OK;
    }
    buffer.resize(*bytes_read);
    NET_ASSIGN_OR_CO_RETURN(auto bytes_written, co_await transport.write(buffer));
    if (bytes_written != bytes_read) {
      co_return ERR_FAILED;
    }
  }
}
```

Client example:

```c++
ErrorOr<awaitable<void>> RunClient(boost::asio::io_context& io_context) {
  TransportFactoryImpl transport_factory{io_context};
  NET_ASSIGN_OR_CO_ERROR(auto client,
    transport_factory.CreateTransport(TransportString{“TCP;Active;Port=1234”}));

  const char message[] = {1, 2, 3};
  NET_ASSIGN_OR_CO_ERROR(auto result, co_await client.write(message));

  std::vector<char> buffer(64);
  NET_ASSIGN_OR_CO_ERROR(auto bytes_read, co_await client.read(buffer));
  if (bytes_read == 0) {
    co_return ERR_CONNECTION_CLOSED;
  }
  buffer.resize(bytes_read);
  if (!std::ranges::equal(buffer, message)) {
    co_return ERR_FAILED;
  }

  co_return OK;
}
```
