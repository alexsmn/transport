# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an asynchronous transport library built on Boost.Asio, providing a unified abstraction over TCP, UDP, WebSocket, serial port, named pipe, and in-process transports, plus shared Beast-based websocket connection adapters used by higher-level modules. The library uses C++20 coroutines throughout: every asynchronous operation returns a `boost::asio::awaitable`, and errors are returned as values rather than thrown.

## Build Commands

This library builds standalone — it consumes no other product. Set `VCPKG_ROOT`
in the environment; anything else machine-specific goes in `.scada-local.cmake`
beside `build-support/`. See `build-support/README.md`.

```shell
cmake --preset ninja
cmake --build --preset release        # or: debug, relwithdebinfo
ctest --preset test-release           # or: test-debug
```

Output lands in `build/ninja/bin/<config>/`.

It is also spliced into any product that consumes it — `find_package(Transport)`
adds this directory as a subdirectory. Built that way it keeps its own C++
standard and its own cppcheck suppressions rather than inheriting the
consumer's.

## Architecture

### Core Abstractions

- **Transport** (`transport.h`) - Base interface combining `Connector`, `Reader`, `Sender`, and `TransportMetadata`. All transport types inherit from this.

- **TransportFactory** (`transport_factory.h`) - Creates transports from string descriptions. Use `TransportFactoryImpl` for the concrete implementation.

- **TransportString** (`transport_string.h`) - Parses and constructs transport configuration strings like `"TCP;Active;Host=localhost;Port=1234"` or `"SERIAL;Name=COM1;BaudRate=9600"`.

### Transport String Format

Semicolon-delimited parameters with optional `=value`:
- **Protocols**: `TCP`, `UDP`, `SERIAL`, `PIPE`, `WS`, `INPROCESS`
- `WS` transport strings are implemented via `WebSocketTransport`, which uses Beast websocket sessions directly for both client/server transport strings and accepted websocket connections handed in by higher-level modules.
- **Direction**: `Active` (client) or `Passive` (server)
- **Common params**: `Host`, `Port`, `Name`
- **Serial params**: `BaudRate`, `ByteSize`, `Parity`, `StopBits`, `FlowControl`

### Async Model

Every asynchronous operation is a C++20 coroutine returning
`awaitable<T>` (`awaitable.h`, an alias for `boost::asio::awaitable`). There are
no completion handlers and no promise type — a caller `co_await`s the operation
and receives the result as a value. The five operations on `Transport`
(`transport.h`) are:

- `awaitable<error_code> open()` - connects, or begins listening
- `awaitable<expected<size_t>> read(std::span<char>)` - streaming transports;
  resolves to 0 when the transport is closed
- `awaitable<expected<size_t>> write(std::span<const char>)` - resolves to the
  number of bytes written
- `awaitable<expected<any_transport>> accept()` - passive transports; yields the
  accepted peer
- `awaitable<error_code> close()`

The caller must keep the buffer alive until a `read`/`write` completes. All five
are `[[nodiscard]]`, so a forgotten `co_await` is a compile-time diagnostic
rather than a silently dropped operation.

**`any_transport`** (`any_transport.h`) is the move-only, type-erased owner of a
transport, and is what `accept()` yields. It forwards the same five operations
plus the `TransportMetadata` accessors, so most code holds an `any_transport`
rather than a `Transport*`.

`BindCancelation` (`awaitable.h`) wraps an awaitable so that it resolves to
`ERR_ABORTED` if a `std::weak_ptr` guard expires before or during the operation.

### Transport Implementations

Active (client) and passive (server) roles are separate classes, not a flag:

- **AsioTransport** (`asio_transport.h`) - Base for Asio-based transports with shared read/write buffer management
- **ActiveTcpTransport** / **PassiveTcpTransport** (`tcp_transport.h`) - TCP client and server
- **ActiveUdpTransport** / **PassiveUdpTransport** (`udp_transport.h`) - UDP client and server; the passive side yields an `AcceptedUdpTransport` per peer. Both sit on the **UdpSocket** seam (`udp_socket.h`, `udp_socket_factory.h`) that lets tests substitute a socket
- **WebSocketTransport** (`websocket_transport.h`) - Beast-based websocket transport used both for generic `WS;...` transport strings and for already-accepted websocket sessions from higher-level servers
- **SerialTransport** (`serial_transport.h`) - Serial ports
- **PipeTransport** (`pipe_transport.h`) - Named pipes (Windows only)
- **InprocessTransportHost** (`inprocess_transport.h`) - In-memory transport for testing; hands out named client/server pairs

Wrappers that add behaviour to another transport rather than talking to a device:

- **MessageReaderTransport** (`message_reader_transport.h`) - Frames a streaming transport into messages using a `MessageReader`
- **DeferredTransport** (`deferred_transport.h`) - Defers an underlying transport
- **DelegatingTransport** (`delegating_transport.h`), **InterceptingTransport** (`intercepting_transport.h`) - Forwarding and interception seams
- **QueueTransport** (`queue_transport.h`), **DummyTransport**, **StubTransport**, **TransportMock** - Test doubles

### Session Layer

**Session** (`session/session.h`) wraps a transport to provide:
- Automatic reconnection with configurable period
- Message sequencing and acknowledgment
- Send queue management with priorities
- Statistics tracking (bytes/messages sent/received)

### Error Handling

**Errors are returned as values; nothing in the library throws to report one.**

- `error_code` (`error.h`) is an alias for `boost::system::error_code`. The
  named constants — `OK`, `ERR_FAILED`, `ERR_ABORTED`, `ERR_TIMED_OUT`,
  `ERR_CONNECTION_CLOSED`, `ERR_ADDRESS_IN_USE` and the rest — are `constexpr`
  `error_code`s built from `boost::system::errc`, not enumerators of a library
  error enum. So they compare and print as `boost::system` codes, and a
  `boost::system` code from Asio or Beast can be returned unchanged.
- `expected<T>` (`expected.h`) carries either a `T` or an `error_code`, and is
  the return type of every operation that yields a value. It is `[[nodiscard]]`.
  Query it with `ok()`, then `value()`/`operator*`; `error()` returns `OK` when
  the value is present. `value()` asserts on the error case rather than
  throwing.
- Operations that yield no value return a bare `error_code` (`open`, `close`),
  which is `OK` on success.
- Propagate with the macros in `expected.h` rather than by hand:
  `NET_ASSIGN_OR_CO_RETURN(lhs, expr)` and `NET_CO_RETURN_IF_ERROR(expr)` inside
  coroutines, `NET_ASSIGN_OR_RETURN` / `NET_RETURN_IF_ERROR` outside them. Each
  attaches a `boost::source_location` to the propagated code, so an error
  carries the line it came from.

Historical note: this section described `net_exception` and
`make_error_promise<T>` against a Chromium-style `net/base/net_errors.h` until
2026-08-26. None of those existed — the library had already moved to coroutines
returning `expected<T>`, and there is no `base/` directory here.

## Dependencies

- **Boost** - Asio (including its C++20 coroutine support), Beast (WebSocket/HTTP), System, plus Algorithm, Assert, Locale, Random and UUID. `vcpkg.json` is the authority; note there is no `boost-coroutine` — the library uses `boost::asio::awaitable`, not Boost.Coroutine.
- **OpenSSL** - TLS transports
- **GTest** - Unit testing

## Platform-Specific Code

Files are filtered by suffix:
- `*_win*`, `winsock*`, `win/*` - Windows only
- `*_posix*`, `*_linux*` - Linux only
- `pipe_transport.*` - Windows only
