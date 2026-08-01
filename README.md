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

## Lifetime rules

These are contracts callers must uphold, not implementation details. Each was
learned from a production use-after-free.

### An `any_transport` must outlive every operation started on it

`any_transport::read` and `write` are **member coroutines**, and they are lazy:
the body — including the `if (!transport_)` guard — runs on first resume, not
at the call site. So the guard is not a lifetime check. A *moved-from*
`any_transport` is safe (`transport_` is null, the call returns
`ERR_INVALID_HANDLE`), but a *destroyed* one is not: the guard reads freed
memory, sees a non-null garbage pointer, passes, and the very next virtual
dispatch faults.

Symptom to recognise: a SIGSEGV inside `any_transport::write`'s coroutine
`.actor`, faulting on an indirect call through a pointer whose low bytes are
zeroed. Observed in production 2026-08-01.

Keep the transport alive for the whole operation. Where that is impossible,
give the coroutine a `weak_ptr` liveness token owned by whoever owns the
transport, and re-check it after **every** suspension before touching anything
that belongs to the owner.

### `WriteQueue` does not own its transport

`WriteQueue` is constructed from an `any_transport&` and stores a raw pointer.
It may be copied, and a copy shares the same state — so a copy handed to a
detached coroutine can easily outlive the transport it points at.

Its `cancelation` token keys on the **queue**, not on the transport: it expires
when the originating `WriteQueue` is destroyed, which is what makes a write
from an outlived copy abort with `ERR_ABORTED` instead of dereferencing freed
memory. That is load-bearing, and it is re-checked after the queue-ordering
suspension because the transport may die while a write waits its turn. Do not
weaken either check.

### A passive bind takes the first endpoint that works

`PassiveTcpTransport::Bind` walks the resolver results and binds the first
endpoint it can, then stops — it does **not** bind every resolved address. A
name that resolves to more than one family therefore serves exactly one of
them: `localhost` is both `::1` and `127.0.0.1`, and glibc's RFC 6724 ordering
puts `::1` first, so `TCP;Passive;Host=localhost` listens on IPv6 loopback
only. A peer dialling the literal `127.0.0.1` gets connection-refused against a
listener that looks healthy from this side.

**Prefer an address literal for any passive bind** (`127.0.0.1`, `0.0.0.0`):
it resolves to exactly one endpoint, so the bind cannot depend on resolver
ordering. The bound endpoint is logged, and a name that resolved to several
endpoints logs a warning naming the ones dropped.

### The bail-out macros are load-bearing for lifetime

`NET_CO_RETURN_IF_ERROR` / `NET_ASSIGN_OR_CO_RETURN` return **without touching
any member**. Expanding one by hand — even just to add a log line on an error
path — silently opts out of that guarantee, and for an accept/read loop the
teardown that fails the pending operation is often the very thing that
destroyed `this`. If an error path must touch a member, re-check a captured
cancelation token first.
