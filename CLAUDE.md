# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is an asynchronous transport library built on Boost.Asio, providing a unified abstraction over TCP, UDP, WebSocket, serial port, and named pipe transports. The library uses C++20 features and promise-based asynchronous operations.

## Build Commands

This library is built as part of the parent SCADA project. From the parent directory:

```batch
# Windows
generate.bat          # Configure CMake
build.bat             # Build
```

```shell
# Linux
./generate.sh
./build.sh
```

### Running Tests

```batch
# Run all net library tests
ctest --build-config RelWithDebInfo --tests-regex net_unittests
```

## Architecture

### Core Abstractions

- **Transport** (`transport.h`) - Base interface combining `Connector`, `Reader`, `Sender`, and `TransportMetadata`. All transport types inherit from this.

- **TransportFactory** (`transport_factory.h`) - Creates transports from string descriptions. Use `TransportFactoryImpl` for the concrete implementation.

- **TransportString** (`transport_string.h`) - Parses and constructs transport configuration strings like `"TCP;Active;Host=localhost;Port=1234"` or `"SERIAL;Name=COM1;BaudRate=9600"`.

### Transport String Format

Semicolon-delimited parameters with optional `=value`:
- **Protocols**: `TCP`, `UDP`, `SERIAL`, `PIPE`, `WS` (WebSocket), `INPROCESS`
- **Direction**: `Active` (client) or `Passive` (server)
- **Common params**: `Host`, `Port`, `Name`
- **Serial params**: `BaudRate`, `ByteSize`, `Parity`, `StopBits`, `FlowControl`

### Async Model

The library uses `promise_hpp` for async operations:
- `promise<void> Open(handlers)` - Opens transport with event handlers
- `promise<size_t> Write(data)` - Writes data, resolves with bytes written
- Handler callbacks: `on_open`, `on_close`, `on_data` (streaming), `on_message` (message-oriented), `on_accept`

### Transport Implementations

- **AsioTransport** (`asio_transport.h`) - Template base for Asio-based transports with shared read/write buffer management
- **AsioTcpTransport** - TCP client/server
- **UdpTransport** - UDP sockets
- **WebSocketTransport** - WebSocket protocol
- **SerialTransport** - Serial ports
- **PipeTransport** - Named pipes (Windows only)
- **InprocessTransport** - In-memory transport for testing

### Session Layer

**Session** (`session.h`) wraps a transport to provide:
- Automatic reconnection with configurable period
- Message sequencing and acknowledgment
- Send queue management with priorities
- Statistics tracking (bytes/messages sent/received)

### Error Handling

Uses Chromium-style error codes (`net/base/net_errors.h`). Key patterns:
- `Error` enum with negative values for errors, `OK = 0`
- `net_exception` wraps errors for promise rejection
- `make_error_promise<T>(error)` creates rejected promises

## Dependencies

- **Boost** - Asio, Coroutine, Beast (for WebSocket)
- **ChromiumBase** - Threading utilities, compiler macros
- **promise.hpp** - Promise-based async
- **GTest** - Unit testing

## Platform-Specific Code

Files are filtered by suffix:
- `*_win*`, `winsock*`, `win/*` - Windows only
- `*_posix*`, `*_linux*` - Linux only
- `pipe_transport.*` - Windows only

## Workflow

- Do not commit after each change. Wait for explicit user request to commit.
- Do not commit directly to the current branch. Create a separate feature branch for changes and submit a PR.
- Consult README.md when making changes. Keep README.md consistent with code changes (dependencies, API, examples).
