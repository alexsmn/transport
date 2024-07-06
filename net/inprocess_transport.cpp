#include "net/inprocess_transport.h"

#include <format>
#include <queue>

namespace net {

class InprocessTransportHost::Client final : public Transport {
 public:
  Client(InprocessTransportHost& host,
         const Executor& executor,
         std::string channel_name)
      : host_{host},
        executor_{executor},
        channel_name_{std::move(channel_name)} {}

  ~Client();

  virtual bool IsMessageOriented() const override { return true; }

  virtual bool IsConnected() const override {
    return accepted_client_ != nullptr;
  }

  virtual bool IsActive() const override { return true; }

  virtual std::string GetName() const override {
    return std::format("client:{}", channel_name_);
  }

  [[nodiscard]] virtual awaitable<Error> Open() override;
  [[nodiscard]] virtual awaitable<Error> Close() override;

  [[nodiscard]] virtual awaitable<ErrorOr<std::unique_ptr<Transport>>>
  Accept() {
    co_return ERR_ACCESS_DENIED;
  }

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Read(
      std::span<char> data) override {
    co_return ERR_ACCESS_DENIED;
  }

  [[nodiscard]] virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) override;

  virtual Executor GetExecutor() const override { return executor_; }

  void Receive(std::span<const char> data) {
    // Must be opened.
    assert(accepted_client_);

    received_messages_.emplace(data.begin(), data.end());
  }

  void OnServerClosed() {
    // Must be opened.
    assert(accepted_client_);
  }

 private:
  InprocessTransportHost& host_;
  const Executor executor_;
  const std::string channel_name_;

  AcceptedClient* accepted_client_ = nullptr;

  std::queue<std::vector<char>> received_messages_;
};

class InprocessTransportHost::Server final : public Transport {
 public:
  Server(InprocessTransportHost& host,
         const Executor& executor,
         std::string channel_name)
      : host_{host},
        executor_{executor},
        channel_name_{std::move(channel_name)} {}

  ~Server() {
    if (opened_) {
      host_.listeners_.erase(channel_name_);
    }
  }

  virtual bool IsMessageOriented() const override { return true; }

  virtual bool IsConnected() const override { return opened_; }

  virtual bool IsActive() const override { return false; }

  virtual std::string GetName() const override {
    return std::format("server:{}", channel_name_);
  }

  [[nodiscard]] virtual awaitable<Error> Open() override {
    if (opened_) {
      co_return ERR_ADDRESS_IN_USE;
    }

    if (!host_.listeners_.try_emplace(channel_name_, this).second) {
      co_return ERR_ADDRESS_IN_USE;
    }

    opened_ = true;

    co_return OK;
  }

  [[nodiscard]] virtual awaitable<Error> Close() override {
    if (!opened_) {
      co_return ERR_CONNECTION_CLOSED;
    }
    host_.listeners_.erase(channel_name_);
    opened_ = false;
    co_return OK;
  }

  [[nodiscard]] virtual awaitable<ErrorOr<std::unique_ptr<Transport>>> Accept()
      override {
    // TODO: Implement.
    co_return ERR_ACCESS_DENIED;
  }

  virtual awaitable<ErrorOr<size_t>> Read(std::span<char> data) override {
    co_return ERR_ACCESS_DENIED;
  }

  virtual Executor GetExecutor() const override { return executor_; }

  virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) override {
    co_return ERR_ACCESS_DENIED;
  }

  AcceptedClient* AcceptClient(Client& client);

 private:
  InprocessTransportHost& host_;
  const Executor executor_;
  const std::string channel_name_;

  bool opened_ = false;
  std::vector<AcceptedClient*> accepted_clients_;

  friend class AcceptedClient;
};

class InprocessTransportHost::AcceptedClient final : public Transport {
 public:
  AcceptedClient(Client& client, Server& server)
      : client_{client}, server_{server} {
    server_.accepted_clients_.emplace_back(this);
  }

  ~AcceptedClient() { std::erase(server_.accepted_clients_, this); }

  virtual bool IsMessageOriented() const override { return true; }

  virtual bool IsConnected() const override { return opened_; }

  virtual bool IsActive() const override { return false; }

  virtual std::string GetName() const override {
    return std::format("server:{}", server_.channel_name_);
  }

  [[nodiscard]] virtual awaitable<Error> Open() override {
    if (opened_) {
      co_return ERR_ADDRESS_IN_USE;
    }

    opened_ = true;
    co_return OK;
  }

  [[nodiscard]] virtual awaitable<Error> Close() override {
    if (!opened_) {
      co_return ERR_CONNECTION_CLOSED;
    }
    opened_ = false;
    client_.OnServerClosed();
    co_return OK;
  }

  [[nodiscard]] virtual awaitable<ErrorOr<std::unique_ptr<Transport>>> Accept()
      override {
    co_return ERR_ACCESS_DENIED;
  }

  virtual awaitable<ErrorOr<size_t>> Read(std::span<char> data) override {
    co_return ERR_ACCESS_DENIED;
  }

  virtual awaitable<ErrorOr<size_t>> Write(
      std::span<const char> data) override {
    client_.Receive(data);
    co_return data.size();
  }

  virtual Executor GetExecutor() const override { return server_.executor_; }

  void OnClientClosed() {
    // WARNING: Client might have not called `Open` yet.
    opened_ = false;
  }

  void Receive(std::span<const char> data) {
    // Must be opened.
    assert(opened_);

    received_messages_.emplace(data.begin(), data.end());
  }

 private:
  Client& client_;
  Server& server_;

  bool opened_ = false;

  std::queue<std::vector<char>> received_messages_;
};

// InprocessTransportHost::Client

InprocessTransportHost::Client::~Client() {
  if (accepted_client_) {
    accepted_client_->OnClientClosed();
  }
}

awaitable<Error> InprocessTransportHost::Client::Open() {
  if (accepted_client_) {
    co_return ERR_ADDRESS_IN_USE;
  }

  auto* server = host_.FindServer(channel_name_);
  if (!server) {
    co_return ERR_ADDRESS_IN_USE;
  }

  accepted_client_ = server->AcceptClient(*this);

  co_return OK;
}

awaitable<Error> InprocessTransportHost::Client::Close() {
  if (accepted_client_) {
    auto* accepted_client = accepted_client_;
    accepted_client_ = nullptr;
    accepted_client->OnClientClosed();
  }

  co_return OK;
}

awaitable<ErrorOr<size_t>> InprocessTransportHost::Client::Write(
    std::span<const char> data) {
  if (!accepted_client_) {
    co_return ERR_CONNECTION_CLOSED;
  }

  accepted_client_->Receive(data);
  co_return data.size();
}

// InprocessTransportHost::Server

InprocessTransportHost::AcceptedClient*
InprocessTransportHost::Server::AcceptClient(Client& client) {
  assert(opened_);
  auto accepted_client = std::make_unique<AcceptedClient>(client, *this);
  AcceptedClient* accepted_client_ptr = accepted_client.get();
  // handlers_.on_accept(std::move(accepted_client));
  return accepted_client_ptr;
}

// InprocessTransportHost

any_transport InprocessTransportHost::CreateServer(
    const Executor& executor,
    std::string_view channel_name) {
  return any_transport{
      std::make_unique<Server>(*this, executor, std::string{channel_name})};
}

any_transport InprocessTransportHost::CreateClient(
    const Executor& executor,
    std::string_view channel_name) {
  return any_transport{
      std::make_unique<Client>(*this, executor, std::string{channel_name})};
}

InprocessTransportHost::Server* InprocessTransportHost::FindServer(
    const std::string& channel_name) {
  auto i = listeners_.find(channel_name);
  return i == listeners_.end() ? nullptr : i->second;
}

}  // namespace net
