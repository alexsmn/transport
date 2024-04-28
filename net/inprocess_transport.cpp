#include "net/inprocess_transport.h"

#include "net/net_exception.h"

#include <format>

namespace net {

class InprocessTransportHost::Client : public Transport {
 public:
  Client(InprocessTransportHost& host, std::string channel_name)
      : host_{host}, channel_name_{std::move(channel_name)} {}

  ~Client() { Close(); }

  virtual bool IsMessageOriented() const override { return true; }

  virtual bool IsConnected() const override {
    return accepted_client_ != nullptr;
  }

  virtual bool IsActive() const override { return true; }

  virtual std::string GetName() const override {
    return std::format("client:{}", channel_name_);
  }

  [[nodiscard]] virtual awaitable<void> Open(
      Handlers handlers) override;

  virtual void Close() override;

  virtual int Read(std::span<char> data) override { return ERR_ACCESS_DENIED; }

  [[nodiscard]] virtual awaitable<size_t> Write(
      std::vector<char> data) override;

  void Receive(std::span<const char> data) {
    // Must be opened.
    assert(accepted_client_);

    if (handlers_.on_message) {
      handlers_.on_message(data);
    }
  }

  void OnServerClosed() {
    // Must be opened.
    assert(accepted_client_);

    if (handlers_.on_close) {
      handlers_.on_close(OK);
    }
  }

 private:
  InprocessTransportHost& host_;
  const std::string channel_name_;

  Handlers handlers_;
  AcceptedClient* accepted_client_ = nullptr;
};

class InprocessTransportHost::Server : public Transport {
 public:
  Server(InprocessTransportHost& host, std::string channel_name)
      : host_{host}, channel_name_{std::move(channel_name)} {}

  ~Server() { Close(); }

  virtual bool IsMessageOriented() const override { return true; }

  virtual bool IsConnected() const override { return opened_; }

  virtual bool IsActive() const override { return false; }

  virtual std::string GetName() const override {
    return std::format("server:{}", channel_name_);
  }

  [[nodiscard]] virtual awaitable<void> Open(
      Handlers handlers) override {
    if (opened_) {
      handlers.on_close(ERR_ADDRESS_IN_USE);
      co_return;
    }

    if (!host_.listeners_.try_emplace(channel_name_, this).second) {
      handlers.on_close(ERR_ADDRESS_IN_USE);
      throw net_exception{ERR_ADDRESS_IN_USE};
    }

    handlers_ = handlers;
    opened_ = true;

    if (auto on_open = std::move(handlers_.on_open)) {
      on_open();
    }
  }

  virtual void Close() override {
    if (opened_) {
      host_.listeners_.erase(channel_name_);
      handlers_ = {};
      opened_ = false;
    }
  }

  virtual int Read(std::span<char> data) override { return ERR_ACCESS_DENIED; }

  virtual awaitable<size_t> Write(
      std::vector<char> data) override {
    throw net_exception{ERR_ACCESS_DENIED};
  }

  AcceptedClient* AcceptClient(Client& client);

 private:
  InprocessTransportHost& host_;
  const std::string channel_name_;

  Handlers handlers_;
  bool opened_ = false;
  std::vector<AcceptedClient*> accepted_clients_;

  friend class AcceptedClient;
};

class InprocessTransportHost::AcceptedClient : public Transport {
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

  [[nodiscard]] virtual awaitable<void> Open(
      Handlers handlers) override {
    if (opened_) {
      handlers.on_close(ERR_ADDRESS_IN_USE);
      throw net_exception{ERR_ADDRESS_IN_USE};
    }

    handlers_ = std::move(handlers);
    opened_ = true;

    // Accepted transport doesn't trigger `on_open` by design.
    assert(!handlers_.on_open);
    handlers_.on_open = nullptr;

    co_return;
  }

  virtual void Close() override {
    if (opened_) {
      opened_ = false;
      handlers_ = {};
      client_.OnServerClosed();
    }
  }

  virtual int Read(std::span<char> data) override { return ERR_ACCESS_DENIED; }

  virtual awaitable<size_t> Write(
      std::vector<char> data) override {
    client_.Receive(data);
    co_return data.size();
  }

  void OnClientClosed() {
    // Client might have not called `Open` yet.
    if (opened_) {
      opened_ = false;
      if (handlers_.on_close) {
        handlers_.on_close(OK);
      }
    }
  }

  void Receive(std::span<const char> data) {
    // Must be opened.
    assert(opened_);

    if (handlers_.on_message) {
      handlers_.on_message(data);
    }
  }

 private:
  Client& client_;
  Server& server_;

  Handlers handlers_;
  bool opened_ = false;
};

// InprocessTransportHost::Client

awaitable<void> InprocessTransportHost::Client::Open(
    Handlers handlers) {
  if (accepted_client_) {
    handlers.on_close(ERR_ADDRESS_IN_USE);
    throw net_exception{ERR_ADDRESS_IN_USE};
  }

  auto* server = host_.FindServer(channel_name_);
  if (!server) {
    handlers.on_close(ERR_ADDRESS_INVALID);
    throw net_exception{ERR_ADDRESS_IN_USE};
  }

  handlers_ = std::move(handlers);

  accepted_client_ = server->AcceptClient(*this);

  if (auto on_open = std::move(handlers_.on_open)) {
    on_open();
  }

  co_return;
}

void InprocessTransportHost::Client::Close() {
  if (accepted_client_) {
    auto* accepted_client = accepted_client_;
    accepted_client_ = nullptr;
    handlers_ = {};
    accepted_client->OnClientClosed();
  }
}

awaitable<size_t> InprocessTransportHost::Client::Write(
    std::vector<char> data) {
  if (!accepted_client_) {
    throw net_exception{ERR_CONNECTION_CLOSED};
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
  handlers_.on_accept(std::move(accepted_client));
  return accepted_client_ptr;
}

// InprocessTransportHost

std::unique_ptr<Transport> InprocessTransportHost::CreateServer(
    std::string_view channel_name) {
  return std::make_unique<Server>(*this, std::string{channel_name});
}

std::unique_ptr<Transport> InprocessTransportHost::CreateClient(
    std::string_view channel_name) {
  return std::make_unique<Client>(*this, std::string{channel_name});
}

InprocessTransportHost::Server* InprocessTransportHost::FindServer(
    const std::string& channel_name) {
  auto i = listeners_.find(channel_name);
  return i == listeners_.end() ? nullptr : i->second;
}

}  // namespace net
