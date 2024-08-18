#pragma once

#include "transport/asio_transport.h"

#include <boost/asio/ip/tcp.hpp>
#include <source_location>

namespace transport {

class ActiveTcpTransport final
    : public AsioTransport<boost::asio::ip::tcp::socket> {
 public:
  ActiveTcpTransport(const Executor& executor,
                     const log_source& log,
                     const std::string& host,
                     const std::string& service,
                     const std::source_location& source_location =
                         std::source_location::current());

  // A constructor for a socket accepted by a passive TCP transport.
  // Uses the executor of the socket.
  ActiveTcpTransport(boost::asio::ip::tcp::socket socket,
                     const log_source& log,
                     const std::source_location& source_location =
                         std::source_location::current());

  ~ActiveTcpTransport();

  [[nodiscard]] virtual std::string name() const override;
  [[nodiscard]] virtual bool active() const override {
    return type_ == Type::ACTIVE;
  }

  [[nodiscard]] virtual awaitable<Error> open() override;
  [[nodiscard]] virtual awaitable<ErrorOr<any_transport>> accept() override;

 protected:
  // AsioTransport
  virtual void Cleanup() override;

 private:
  using Socket = boost::asio::ip::tcp::socket;
  using Resolver = boost::asio::ip::tcp::resolver;

  [[nodiscard]] awaitable<Error> ResolveAndConnect();
  [[nodiscard]] awaitable<Error> Connect(Resolver::iterator iterator);

  std::string host_;
  std::string service_;
  std::source_location source_location_;

  Resolver resolver_;

  enum class Type { ACTIVE, ACCEPTED };
  const Type type_;
};

class PassiveTcpTransport final
    : public AsioTransport<boost::asio::ip::tcp::socket> {
 public:
  PassiveTcpTransport(const Executor& executor,
                      const log_source& log,
                      const std::string& host,
                      const std::string& service);

  ~PassiveTcpTransport();

  [[nodiscard]] int GetLocalPort() const;

  [[nodiscard]] virtual std::string name() const override;
  [[nodiscard]] virtual bool active() const override { return false; }
  [[nodiscard]] virtual bool connected() const override { return connected_; }

  [[nodiscard]] virtual Executor get_executor() override {
    return acceptor_.get_executor();
  }

  [[nodiscard]] virtual awaitable<Error> open() override;
  [[nodiscard]] virtual awaitable<Error> close() override;
  [[nodiscard]] virtual awaitable<ErrorOr<any_transport>> accept() override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> write(
      std::span<const char> data) override;

 protected:
  // AsioTransport
  virtual void Cleanup() override;

 private:
  using Socket = boost::asio::ip::tcp::socket;
  using Resolver = boost::asio::ip::tcp::resolver;

  [[nodiscard]] awaitable<Error> ResolveAndBind();
  [[nodiscard]] boost::system::error_code Bind(Resolver::iterator iterator);

  void ProcessError(const boost::system::error_code& ec);

  std::string host_;
  std::string service_;

  Resolver resolver_;
  boost::asio::ip::tcp::acceptor acceptor_;

  bool connected_ = false;
  bool closed_ = false;
};

}  // namespace transport
