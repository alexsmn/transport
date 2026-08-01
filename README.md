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
awaitable<error_code> RunServer(boost::asio::io_context& io_context) {
  TransportFactoryImpl transport_factory{io_context};

  NET_ASSIGN_OR_CO_RETURN(auto server,
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

awaitable<error_code> RunEcho(any_transport transport) {
  std::vector<char> buffer;

  for (;;) {
    NET_CO_RETURN_IF_ERROR(co_await ReadMessage(transport, 64, buffer));
    if (buffer.empty()) {
      co_return OK; // graceful close
    }

    NET_ASSIGN_OR_CO_RETURN(auto bytes_written, co_await transport.write(buffer));
    if (bytes_written != bytes_read) {
      co_return ERR_FAILED;
    }
  }
}
```

Client example:

```c++
awaitable<error_code> RunClient(boost::asio::io_context& io_context) {
  TransportFactoryImpl transport_factory{io_context};
  NET_ASSIGN_OR_CO_RETURN(auto client,
    transport_factory.CreateTransport(TransportString{“TCP;Active;Port=1234”}));

  const char message[] = {1, 2, 3};
  NET_ASSIGN_OR_CO_RETURN(auto result, co_await client.write(message));

  std::vector<char> buffer;
  NET_CO_RETURN_IF_ERROR(co_await ReadMessage(client, 64, buffer));
  if (buffer.empty()) {
    co_return ERR_CONNECTION_CLOSED;
  }

  co_return std::ranges::equal(buffer, message) ? OK : ERR_FAILED;
}
```
