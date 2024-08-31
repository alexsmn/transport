#pragma once

#include "transport/asio_transport.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/assert/source_location.hpp>

namespace transport {

class ActiveTcpTransport final
    : public AsioTransport<boost::asio::ip::tcp::socket> {
 public:
  ActiveTcpTransport(
      const executor& executor,
      const log_source& log,
      const std::string& host,
      const std::string& service,
      const boost::source_location& source_location = BOOST_CURRENT_LOCATION);

  // A constructor for a socket accepted by a passive TCP transport.
  // Uses the executor of the socket.
  ActiveTcpTransport(
      boost::asio::ip::tcp::socket socket,
      const log_source& log,
      const boost::source_location& source_location = BOOST_CURRENT_LOCATION);

  ~ActiveTcpTransport();

  [[nodiscard]] virtual std::string name() const override;
  [[nodiscard]] virtual bool active() const override {
    return type_ == Type::ACTIVE;
  }

  [[nodiscard]] virtual awaitable<error_code> open() override;
  [[nodiscard]] virtual awaitable<expected<any_transport>> accept() override;

 protected:
  // AsioTransport
  virtual void Cleanup() override;

 private:
  using Socket = boost::asio::ip::tcp::socket;
  using Resolver = boost::asio::ip::tcp::resolver;

  [[nodiscard]] awaitable<error_code> ResolveAndConnect();
  [[nodiscard]] awaitable<error_code> Connect(Resolver::iterator iterator);

  std::string host_;
  std::string service_;
  boost::source_location source_location_;

  Resolver resolver_;

  enum class Type { ACTIVE, ACCEPTED };
  const Type type_;
};

class PassiveTcpTransport final
    : public AsioTransport<boost::asio::ip::tcp::socket> {
 public:
  PassiveTcpTransport(const executor& executor,
                      const log_source& log,
                      const std::string& host,
                      const std::string& service);

  ~PassiveTcpTransport();

  [[nodiscard]] int GetLocalPort() const;

  [[nodiscard]] virtual std::string name() const override;
  [[nodiscard]] virtual bool active() const override { return false; }
  [[nodiscard]] virtual bool connected() const override { return connected_; }

  [[nodiscard]] virtual executor get_executor() override {
    return acceptor_.get_executor();
  }

  [[nodiscard]] virtual awaitable<error_code> open() override;
  [[nodiscard]] virtual awaitable<error_code> close() override;
  [[nodiscard]] virtual awaitable<expected<any_transport>> accept() override;

  [[nodiscard]] virtual awaitable<expected<size_t>> read(
      std::span<char> data) override;

  [[nodiscard]] virtual awaitable<expected<size_t>> write(
      std::span<const char> data) override;

 protected:
  // AsioTransport
  virtual void Cleanup() override;

 private:
  using Socket = boost::asio::ip::tcp::socket;
  using Resolver = boost::asio::ip::tcp::resolver;

  [[nodiscard]] awaitable<error_code> ResolveAndBind();
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
