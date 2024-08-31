#include "transport/tcp_transport.h"

#include "transport/log.h"

#include <boost/asio/connect.hpp>

namespace transport {

// ActiveTcpTransport

ActiveTcpTransport::ActiveTcpTransport(
    const Executor& executor,
    const log_source& log,
    const std::string& host,
    const std::string& service,
    const boost::source_location& source_location)
    : AsioTransport{executor, log},
      host_{host},
      service_{service},
      resolver_{executor},
      type_{Type::ACTIVE},
      source_location_{source_location} {}

ActiveTcpTransport::ActiveTcpTransport(
    Socket socket,
    const log_source& log,
    const boost::source_location& source_location)
    : AsioTransport{socket.get_executor(), log},
      type_{Type::ACCEPTED},
      resolver_{socket.get_executor()},
      source_location_{source_location} {
  io_object_ = std::move(socket);
  connected_ = true;
}

ActiveTcpTransport::~ActiveTcpTransport() {
  // The base class closes the core on destruction.
}

std::string ActiveTcpTransport::name() const {
  switch (type_) {
    case Type::ACTIVE:
      return "TCP Active";
    case Type::ACCEPTED:
      return "TCP Accepted";
    default:
      assert(false);
      return "TCP Unknown";
  }
}

awaitable<expected<any_transport>> ActiveTcpTransport::accept() {
  co_return ERR_ACCESS_DENIED;
}

awaitable<error_code> ActiveTcpTransport::open() {
  if (connected_) {
    co_return OK;
  }

  log_.writef(LogSeverity::Normal, "Open");

  co_return co_await ResolveAndConnect();
}

awaitable<error_code> ActiveTcpTransport::ResolveAndConnect() {
  log_.writef(LogSeverity::Normal, "Start DNS resolution to %s:%s",
              host_.c_str(), service_.c_str());

  auto [error, iterator] = co_await resolver_.async_resolve(
      host_, service_, boost::asio::as_tuple(boost::asio::use_awaitable));

  if (closed_) {
    co_return ERR_ABORTED;
  }

  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      log_.write(LogSeverity::Warning, "DNS resolution error");
      ProcessError(error);
    }
    co_return error;
  }

  log_.write(LogSeverity::Normal, "DNS resolution completed");

  co_return co_await Connect(std::move(iterator));
}

awaitable<error_code> ActiveTcpTransport::Connect(Resolver::iterator iterator) {
  auto [error, connected_iterator] = co_await boost::asio::async_connect(
      io_object_, iterator, boost::asio::as_tuple(boost::asio::use_awaitable));

  if (closed_) {
    co_return ERR_ABORTED;
  }

  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      log_.write(LogSeverity::Warning, "Connect error");
      ProcessError(error);
    }
    co_return error;
  }

  log_.writef(LogSeverity::Normal, "Connected to %s",
              connected_iterator->host_name().c_str());

  connected_ = true;

  co_return OK;
}

void ActiveTcpTransport::Cleanup() {
  assert(closed_);

  log_.write(LogSeverity::Normal, "Cleanup");

  connected_ = false;

  resolver_.cancel();

  boost::system::error_code ec;
  io_object_.cancel(ec);
  io_object_.close(ec);
}

// PassiveTcpTransport

PassiveTcpTransport::PassiveTcpTransport(const Executor& executor,
                                         const log_source& log,
                                         const std::string& host,
                                         const std::string& service)
    : AsioTransport{executor, log},
      host_{host},
      service_{service},
      resolver_{executor},
      acceptor_{executor} {}

PassiveTcpTransport::~PassiveTcpTransport() {
  // The base class closes the core on destruction.
}

void PassiveTcpTransport::Cleanup() {
  assert(closed_);

  log_.write(LogSeverity::Normal, "Cleanup");

  connected_ = false;

  resolver_.cancel();

  boost::system::error_code ec;
  io_object_.cancel(ec);
  io_object_.close(ec);
}

int PassiveTcpTransport::GetLocalPort() const {
  return acceptor_.local_endpoint().port();
}

awaitable<error_code> PassiveTcpTransport::open() {
  log_.writef(LogSeverity::Normal, "Open");

  return ResolveAndBind();
}

awaitable<error_code> PassiveTcpTransport::ResolveAndBind() {
  log_.writef(LogSeverity::Normal, "Start DNS resolution to %s:%s",
              host_.c_str(), service_.c_str());

  auto [error, iterator] = co_await resolver_.async_resolve(
      /*query=*/{host_, service_},
      boost::asio::as_tuple(boost::asio::use_awaitable));

  if (closed_) {
    co_return ERR_ABORTED;
  }

  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      log_.write(LogSeverity::Warning, "DNS resolution error");
      ProcessError(error);
    }
    co_return error;
  }

  log_.write(LogSeverity::Normal, "DNS resolution completed");

  if (auto ec = Bind(std::move(iterator)); ec) {
    log_.write(LogSeverity::Warning, "Bind error");
    ProcessError(ec);
    co_return error;
  }

  log_.write(LogSeverity::Normal, "Bind completed");

  connected_ = true;

  co_return OK;
}

boost::system::error_code PassiveTcpTransport::Bind(
    Resolver::iterator iterator) {
  log_.write(LogSeverity::Normal, "Bind");

  boost::system::error_code ec = boost::asio::error::fault;

  for (Resolver::iterator end; iterator != end; ++iterator) {
    acceptor_.open(iterator->endpoint().protocol(), ec);
    if (ec)
      continue;

    acceptor_.set_option(Socket::reuse_address{true}, ec);
    // TODO: Log endpoint.
    acceptor_.bind(iterator->endpoint(), ec);

    if (!ec)
      acceptor_.listen(Socket::max_listen_connections, ec);

    if (!ec)
      break;

    acceptor_.close();
  }

  return ec;
}

awaitable<error_code> PassiveTcpTransport::close() {
  co_await boost::asio::dispatch(acceptor_.get_executor(),
                                 boost::asio::use_awaitable);

  if (closed_) {
    co_return ERR_CONNECTION_CLOSED;
  }

  log_.write(LogSeverity::Normal, "Close");

  closed_ = true;
  connected_ = false;
  acceptor_.close();

  co_return OK;
}

awaitable<expected<any_transport>> PassiveTcpTransport::accept() {
  // TODO: Use different executor.
  auto [error, peer] = co_await acceptor_.async_accept(
      boost::asio::as_tuple(boost::asio::use_awaitable));

  if (closed_) {
    co_return ERR_ABORTED;
  }

  // TODO: Log connection information.
  log_.write(LogSeverity::Normal, "Accept incoming connection");

  if (error) {
    if (error != boost::asio::error::operation_aborted) {
      log_.write(LogSeverity::Warning, "Accept connection error");
      ProcessError(error);
    }
    co_return error;
  }

  log_.write(LogSeverity::Normal, "Connection accepted");

  co_return any_transport{std::make_unique<ActiveTcpTransport>(
      std::move(peer), log_, BOOST_CURRENT_LOCATION)};
}

awaitable<expected<size_t>> PassiveTcpTransport::read(std::span<char> data) {
  co_return ERR_ACCESS_DENIED;
}

awaitable<expected<size_t>> PassiveTcpTransport::write(
    std::span<const char> data) {
  co_return ERR_ACCESS_DENIED;
}

void PassiveTcpTransport::ProcessError(const boost::system::error_code& ec) {
  if (closed_) {
    return;
  }

  if (ec != OK) {
    log_.writef(LogSeverity::Warning, "error_code: %s",
                ErrorToShortString(ec).c_str());
  } else {
    log_.writef(LogSeverity::Normal, "Graceful close");
  }

  connected_ = false;
  closed_ = true;
}

std::string PassiveTcpTransport::name() const {
  return "TCP Passive";
}

}  // namespace transport
