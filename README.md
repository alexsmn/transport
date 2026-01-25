# Transport

Asynchronous transport library based on Boost.Asio with a promise-based API.

The library provides a unified abstraction over various [Boost.Asio](https://www.boost.org/doc/libs/1_85_0/doc/html/boost_asio.html) I/O objects:

* TCP socket
* UDP socket
* WebSocket
* Serial port
* Named pipe (Windows)
* In-process transport (for testing)

Transports are created from string descriptions via a factory.

## Transport String Format

Semicolon-delimited parameters: `Protocol;Direction;Param=Value;...`

Examples:
- `TCP;Active;Host=localhost;Port=1234` - TCP client
- `TCP;Passive;Port=1234` - TCP server
- `UDP;Active;Host=localhost;Port=5000` - UDP client
- `SERIAL;Name=COM1;BaudRate=9600` - Serial port
- `WS;Active;Host=localhost;Port=8080` - WebSocket client

## Example: Echo Server

```c++
#include "net/transport_factory_impl.h"
#include "net/transport_string.h"

void RunServer(boost::asio::io_context& io_context) {
  net::TransportFactoryImpl factory{io_context};
  auto executor = io_context.get_executor();

  auto server = factory.CreateTransport(
      net::TransportString{"TCP;Passive;Port=1234"}, executor);

  server->Open({
      .on_accept = [&](std::unique_ptr<net::Transport> client) {
        auto tr = std::shared_ptr<net::Transport>{client.release()};
        tr->Open({
            .on_close = [tr](net::Error) mutable { tr.reset(); },
            .on_data = [tr]() {
              std::array<char, 1024> buffer;
              int bytes_read = tr->Read(buffer);
              if (bytes_read > 0) {
                tr->Write(std::span{buffer}.subspan(0, bytes_read));
              }
            }
        });
      }
  });

  io_context.run();
}
```

## Example: Client

```c++
void RunClient(boost::asio::io_context& io_context) {
  net::TransportFactoryImpl factory{io_context};
  auto executor = io_context.get_executor();

  auto client = factory.CreateTransport(
      net::TransportString{"TCP;Active;Host=localhost;Port=1234"}, executor);

  client->Open({
      .on_open = [&]() {
        const char message[] = "Hello";
        client->Write(message);
      },
      .on_close = [](net::Error error) {
        // Handle disconnection
      },
      .on_data = [&]() {
        std::array<char, 1024> buffer;
        int bytes_read = client->Read(buffer);
        // Process received data
      }
  });

  io_context.run();
}
```

## API Overview

### Transport Interface

- `promise<void> Open(Handlers)` - Open with event callbacks
- `void Close()` - Close the transport
- `int Read(std::span<char>)` - Read available data (returns bytes read or error)
- `promise<size_t> Write(std::span<const char>)` - Write data asynchronously

### Event Handlers

```c++
struct Handlers {
  std::function<void()> on_open;
  std::function<void(Error)> on_close;
  std::function<void()> on_data;                          // For streaming transports
  std::function<void(std::span<const char>)> on_message;  // For message-oriented transports
  std::function<void(std::unique_ptr<Transport>)> on_accept;  // For passive transports
};
```

## Dependencies

- Boost (Asio, Beast for WebSocket)
- C++20
