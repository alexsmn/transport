#include "net/inprocess_transport.h"

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

  virtual Error Open(const Handlers& handlers) override;

  virtual void Close() override;

  virtual int Read(std::span<char> data) override { return ERR_ACCESS_DENIED; }

  virtual int Write(std::span<const char> data);

  int Receive(std::span<const char> data) {
    // Must be opened.
    assert(accepted_client_);

    if (handlers_.on_message) {
      handlers_.on_message(data);
    }
    return data.size();
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

  virtual Error Open(const Handlers& handlers) override {
    if (opened_) {
      return ERR_ADDRESS_IN_USE;
    }

    if (!host_.listeners_.try_emplace(channel_name_, this).second) {
      return ERR_ADDRESS_IN_USE;
    }

    handlers_ = handlers;
    opened_ = true;
    return OK;
  }

  virtual void Close() override {
    if (opened_) {
      host_.listeners_.erase(channel_name_);
      handlers_ = {};
      opened_ = false;
    }
  }

  virtual int Read(std::span<char> data) override { return ERR_ACCESS_DENIED; }

  virtual int Write(std::span<const char> data) override {
    return ERR_ACCESS_DENIED;
  }

  std::pair<Error, AcceptedClient*> AcceptClient(Client& client);

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

  ~AcceptedClient() {
    auto i = std::ranges::find(server_.accepted_clients_, this);
    server_.accepted_clients_.erase(i);
  }

  virtual bool IsMessageOriented() const override { return true; }

  virtual bool IsConnected() const override { return opened_; }

  virtual bool IsActive() const override { return false; }

  virtual std::string GetName() const override {
    return std::format("server:{}", server_.channel_name_);
  }

  virtual Error Open(const Handlers& handlers) override {
    if (opened_) {
      return ERR_ADDRESS_IN_USE;
    }

    handlers_ = handlers;
    opened_ = true;
    return OK;
  }

  virtual void Close() override {
    if (opened_) {
      client_.OnServerClosed();
      handlers_ = {};
      opened_ = false;
    }
  }

  virtual int Read(std::span<char> data) override { return ERR_ACCESS_DENIED; }

  virtual int Write(std::span<const char> data) override {
    return client_.Receive(data);
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

  int Receive(std::span<const char> data) {
    // Must be opened.
    assert(opened_);

    if (handlers_.on_message) {
      handlers_.on_message(data);
    }
    return data.size();
  }

 private:
  Client& client_;
  Server& server_;

  Handlers handlers_;
  bool opened_ = false;
};

// InprocessTransportHost::Client

Error InprocessTransportHost::Client::Open(const Handlers& handlers) {
  if (accepted_client_) {
    return ERR_ADDRESS_IN_USE;
  }

  auto* server = host_.FindServer(channel_name_);
  if (!server) {
    return ERR_ADDRESS_INVALID;
  }

  handlers_ = handlers;

  auto [error, accepted_client] = server->AcceptClient(*this);
  if (error != OK) {
    handlers_ = {};
    return error;
  }

  accepted_client_ = accepted_client;
  return OK;
}

void InprocessTransportHost::Client::Close() {
  if (accepted_client_) {
    accepted_client_->OnClientClosed();
    accepted_client_ = nullptr;
  }
}

int InprocessTransportHost::Client::Write(std::span<const char> data) {
  return accepted_client_->Receive(data);
}

// InprocessTransportHost::Server

std::pair<Error, InprocessTransportHost::AcceptedClient*>
InprocessTransportHost::Server::AcceptClient(Client& client) {
  assert(opened_);
  auto accepted_client = std::make_unique<AcceptedClient>(client, *this);
  AcceptedClient* accepted_client_ptr = accepted_client.get();
  Error error = handlers_.on_accept(std::move(accepted_client));
  return {error, accepted_client_ptr};
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
