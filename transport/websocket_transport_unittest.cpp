#include "transport/websocket_transport.h"

#include "transport/test/coroutine_util.h"
#include "transport/test/test_log.h"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <gmock/gmock.h>
#include <array>
#include <cctype>
#include <random>
#include <string_view>
#include <thread>
#include <unordered_set>

namespace transport {
namespace {

namespace http = boost::beast::http;
namespace websocket = boost::beast::websocket;

bool HeaderContainsToken(std::string_view header, std::string_view token) {
  while (!header.empty()) {
    const auto comma = header.find(',');
    auto part = comma == std::string_view::npos ? header : header.substr(0, comma);
    while (!part.empty() && std::isspace(static_cast<unsigned char>(part.front())))
      part.remove_prefix(1);
    while (!part.empty() && std::isspace(static_cast<unsigned char>(part.back())))
      part.remove_suffix(1);
    if (part == token)
      return true;
    if (comma == std::string_view::npos)
      break;
    header.remove_prefix(comma + 1);
  }
  return false;
}

constexpr auto kTestCertificatePem = R"(-----BEGIN CERTIFICATE-----
MIIDCTCCAfGgAwIBAgIUQWAR+40WE34MoTuKigrGeiGT5ycwDQYJKoZIhvcNAQEL
BQAwFDESMBAGA1UEAwwJbG9jYWxob3N0MB4XDTI2MDQyMDE4NTczMloXDTM2MDQx
NzE4NTczMlowFDESMBAGA1UEAwwJbG9jYWxob3N0MIIBIjANBgkqhkiG9w0BAQEF
AAOCAQ8AMIIBCgKCAQEAsUhr1SNMP7HMVoUgPK1j2ecIvRSz0PiGagh5kOL6HETy
Eo3MTux9LheWcvhhMvpm3HNOIx6Q8iModo77le+nyYxExrQ4xNFTvwsFhkpso2sQ
C9fWO7uabAFhMSJZZq5vs8y4o3aMQNTeSBWzkoX0ebdzkBn0YbLa6Gvas87oF50g
NWiOKJNNmZL6rC2iSMI+2c/fq7UrPkmDd6VObFXO7ItTFXbqeXPiynuMnLXf5sGU
V3VKP8PyVGR7QJq7LRa30BCsfSTAK6BlVIcPkDMA+1uVM6lkPI103axRF3UhwwU6
B8WOpyafhul18ZiFtSYz78x5K3kYrSvYjOOb1YeHEwIDAQABo1MwUTAdBgNVHQ4E
FgQUaGBbcBpQLHhmkVgc1Eg2OOXTLM0wHwYDVR0jBBgwFoAUaGBbcBpQLHhmkVgc
1Eg2OOXTLM0wDwYDVR0TAQH/BAUwAwEB/zANBgkqhkiG9w0BAQsFAAOCAQEAeEkK
63ra3vPoBp49UWiRgPfroFKbX/AI01/r0WRsSDzwOPoKRmSpuwdcXfT3B3ilzmDX
sfiAJJQtvcgm3V3vNzWmks+4dLS0G+bRwBxih5b16xAWbUcUgty9TX3HOlAzqdOh
BnoQQczubSQv2/0AzfxZ06EGpwrlFrTzXJRpmQRoqo8xGEwgiQ10A3aXYYyxydzj
ChS5hFDZ4P6MA01VI2y5dgIUiA+3uEWOt2SJgexDaFk7ZmpVqzh4KHV0deSyG3Gt
QoPGNJO4DZVyCXKd4iOg40H4JRc2fIlMykOklAD7ukuUpGNeEPKzSQjakxKYKiX8
NGfJbg7n4a46yf+S6A==
-----END CERTIFICATE-----
)";

constexpr auto kTestPrivateKeyPem = R"(-----BEGIN PRIVATE KEY-----
MIIEvwIBADANBgkqhkiG9w0BAQEFAASCBKkwggSlAgEAAoIBAQCxSGvVI0w/scxW
hSA8rWPZ5wi9FLPQ+IZqCHmQ4vocRPISjcxO7H0uF5Zy+GEy+mbcc04jHpDyIyh2
jvuV76fJjETGtDjE0VO/CwWGSmyjaxAL19Y7u5psAWExIllmrm+zzLijdoxA1N5I
FbOShfR5t3OQGfRhstroa9qzzugXnSA1aI4ok02ZkvqsLaJIwj7Zz9+rtSs+SYN3
pU5sVc7si1MVdup5c+LKe4yctd/mwZRXdUo/w/JUZHtAmrstFrfQEKx9JMAroGVU
hw+QMwD7W5UzqWQ8jXTdrFEXdSHDBToHxY6nJp+G6XXxmIW1JjPvzHkreRitK9iM
45vVh4cTAgMBAAECggEAD+7UUimD9s2B8dyxEwL6UGElNekgaA2N9wWf91eO5u+D
WguIaydx8KyKBvcvtScwC2wJf7qFiF2Ei3M6RTVuvPxwSfN0jqvJfQf+jR0vOliq
7oWNaXzo2gAdvg66PjI7M8uYZIiI/mKjP5NDuk1ztWS5bCAJCKbMacsXssVLsqN0
ENH+/vLsawjvD+/UQQGr4FUvw0QCYz1FA5C4XCyHEidMORRSOhh7yWB38lbO0StU
StnWGdeYxNz1+VGJwfckuvvKVl8A6vY8So5LPsHS2aqDXcAwmSkJgd78k/eaW1V7
bQ7kn8NtXECrphIYcHu4/yuvwp+Y9Avc3pCtC4etQQKBgQDEjupDiFsOM3RDNfoj
9fQc/jC9I61TBoi9kWIj0UMR1nCi+SNlxm5uV4gRQcTs4MSxFGqgcdCm8Bb5MNld
dxsMJYOOqDSciMpyEdwCEtk17ctR48+Vsan22xxzqezsI7Skvzu0FzBs7g5cnE79
hxeV3+qgrirnO7xwe6dJLrOCIwKBgQDm5UB+kS9SWc31Dhxc0fuOhDa7PhDQKo4V
ZsCEtc7hpytcjrLSC9Ru5Er1tp3L/oF5jQLMalhdMR1QagG7cJOAdT4K3+tjnags
E4c/Vw5xzRdjuWZEAleZATyRwonvFXSf868Sg8op6xZjMFq4qPb+8mJexzinVOM9
ipsA2CveUQKBgQDCosJXHS8NYOY/p7OK6IJSM2MP58Q58r50+QG1dgJ0J2Rh/VKP
9W5k1UhnzjiyV+BteUoclpeGtzgIida0Nr0RyhP7r5RpbQsK6aRyaTetr0smS+/C
y6sCRvZlkl6JdtHqUXNNYakSNKkEC8QsSRmRz6kGc3EIiJ6Qw+FjFlurAQKBgQDZ
PWch7j3E0IPMBfu/hT2WiGTqZOnywacvEZ8e/ePpQZy1l/k9US4NK7QvXSM4VHvD
Pl4csA31mIlJKIP6tF/DZAv8tVNGRYZ9+d2tRZ5sihdwl3ZVlJKQfa5cQdn/XYN+
HwtgcyjZqbtFlbA1v5usoabWH8D5BxBKzccq0zjrEQKBgQCaSvtY2PveeWHZBOlN
aFTbyLdyBHdhwpYZm3vKQ3MoOtnQGRq1BlQk7ojruxGhKMTnJhNP2cSnDdaUJB/S
urjYLs2lpMW81+/p1qt5QCP+hVU+/Y7rdCXmTQjsy+9rAf3VOupMisj2gmgX8ASm
mCzxKIlbzMnhGhGlzdKwqs5Uhw==
-----END PRIVATE KEY-----
)";

int GenerateTestNetworkPort() {
  static std::mt19937 gen(std::random_device{}());
  static std::uniform_int_distribution distrib{30000, 40000};
  static std::unordered_set<int> seen;
  int port = distrib(gen);
  while (!seen.emplace(port).second) {
    port = distrib(gen);
  }
  return port;
}

log_source MakeTestLog() {
  return log_source{std::make_shared<TestLogSink>()};
}

class BeastHandshakeClient {
 public:
  void Connect(const std::string& host,
               const std::string& port,
               const std::function<void(websocket::request_type&)>& decorate = {}) {
    const auto results = resolver_.resolve(host, port);
    boost::asio::connect(websocket_.next_layer(), results);
    if (decorate) {
      websocket_.set_option(
          websocket::stream_base::decorator(
              [decorate](websocket::request_type& request) { decorate(request); }));
    }
    websocket_.handshake(response_, host + ":" + port, "/");
  }

  void EnablePerMessageDeflate() {
    websocket::permessage_deflate options;
    options.client_enable = true;
    options.server_enable = true;
    websocket_.set_option(options);
  }

  void Write(std::span<const char> data) {
    websocket_.text(true);
    websocket_.write(boost::asio::buffer(data.data(), data.size()));
  }

  std::string Read() {
    boost::beast::flat_buffer buffer;
    websocket_.read(buffer);
    return boost::beast::buffers_to_string(buffer.data());
  }

  const websocket::response_type& response() const { return response_; }

  void Close() {
    if (websocket_.is_open())
      websocket_.close(websocket::close_code::normal);
  }

 private:
  boost::asio::io_context io_context_;
  boost::asio::ip::tcp::resolver resolver_{io_context_};
  websocket::stream<boost::asio::ip::tcp::socket> websocket_{io_context_};
  websocket::response_type response_;
};

class TlsBeastHandshakeClient {
 public:
  TlsBeastHandshakeClient() : websocket_{io_context_, ssl_context_} {
    ssl_context_.set_verify_mode(boost::asio::ssl::verify_none);
  }

  void Connect(const std::string& host, const std::string& port) {
    const auto results = resolver_.resolve(host, port);
    boost::asio::connect(websocket_.next_layer().next_layer(), results);
    websocket_.next_layer().handshake(boost::asio::ssl::stream_base::client);
    websocket_.handshake(host + ":" + port, "/");
  }

  void Write(std::span<const char> data) {
    websocket_.text(true);
    websocket_.write(boost::asio::buffer(data.data(), data.size()));
  }

  std::string Read() {
    boost::beast::flat_buffer buffer;
    websocket_.read(buffer);
    return boost::beast::buffers_to_string(buffer.data());
  }

  void Close() {
    if (websocket_.is_open())
      websocket_.close(websocket::close_code::normal);
  }

 private:
  boost::asio::io_context io_context_;
  boost::asio::ssl::context ssl_context_{boost::asio::ssl::context::tls_client};
  boost::asio::ip::tcp::resolver resolver_{io_context_};
  websocket::stream<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>
      websocket_;
};

TEST(WebSocketTransportTest, ActiveAndPassiveExchangeMessages) {
  boost::asio::io_context io_context;
  const auto port = std::to_string(GenerateTestNetworkPort());
  auto log = MakeTestLog();

  auto future = boost::asio::co_spawn(io_context, [&]() -> awaitable<void> {
    WebSocketTransport server{
        io_context.get_executor(), log.with_channel("Server"), "127.0.0.1",
        port, /*active=*/false};
    WebSocketTransport client{
        io_context.get_executor(), log.with_channel("Client"), "127.0.0.1",
        port, /*active=*/true};

    NET_EXPECT_OK(co_await server.open());
    NET_EXPECT_OK(co_await client.open());

    auto accepted_result = co_await server.accept();
    EXPECT_TRUE(accepted_result.ok());
    if (!accepted_result.ok())
      co_return;
    auto accepted = std::move(*accepted_result);
    EXPECT_TRUE(accepted.connected());
    EXPECT_EQ(accepted.name(), "WebSocket Connection");

    constexpr std::array<char, 4> kRequest = {3, 1, 2, 3};
    auto written_result = co_await client.write(kRequest);
    EXPECT_TRUE(written_result.ok());
    if (!written_result.ok())
      co_return;
    auto written = *written_result;
    EXPECT_EQ(written, kRequest.size());

    std::array<char, 16> read_buffer{};
    auto accepted_read_result = co_await accepted.read(read_buffer);
    EXPECT_TRUE(accepted_read_result.ok());
    if (!accepted_read_result.ok())
      co_return;
    auto accepted_read = *accepted_read_result;
    EXPECT_EQ(accepted_read, kRequest.size());
    EXPECT_TRUE(std::equal(
        kRequest.begin(), kRequest.end(), read_buffer.begin(),
        read_buffer.begin() + static_cast<std::ptrdiff_t>(accepted_read)));

    auto echoed_result = co_await accepted.write(
        std::span<const char>{read_buffer.data(), accepted_read});
    EXPECT_TRUE(echoed_result.ok());
    if (!echoed_result.ok())
      co_return;
    auto echoed = *echoed_result;
    EXPECT_EQ(echoed, accepted_read);

    std::array<char, 16> client_read_buffer{};
    auto client_read_result = co_await client.read(client_read_buffer);
    EXPECT_TRUE(client_read_result.ok());
    if (!client_read_result.ok())
      co_return;
    auto client_read = *client_read_result;
    EXPECT_EQ(client_read, kRequest.size());
    EXPECT_TRUE(std::equal(
        kRequest.begin(), kRequest.end(), client_read_buffer.begin(),
        client_read_buffer.begin() + static_cast<std::ptrdiff_t>(client_read)));

    NET_EXPECT_OK(co_await server.close());
  }, boost::asio::use_future);

  io_context.run();
  future.get();
}

TEST(WebSocketTransportTest, AcceptedTransportIsMessageOrientedAndPassive) {
  boost::asio::io_context io_context;
  const auto port = std::to_string(GenerateTestNetworkPort());
  auto log = MakeTestLog();

  auto future = boost::asio::co_spawn(io_context, [&]() -> awaitable<void> {
    WebSocketTransport server{
        io_context.get_executor(), log.with_channel("Server"), "127.0.0.1",
        port, /*active=*/false};
    WebSocketTransport client{
        io_context.get_executor(), log.with_channel("Client"), "127.0.0.1",
        port, /*active=*/true};

    EXPECT_FALSE(server.active());
    EXPECT_TRUE(client.active());
    EXPECT_TRUE(server.message_oriented());
    EXPECT_TRUE(client.message_oriented());

    NET_EXPECT_OK(co_await server.open());
    NET_EXPECT_OK(co_await client.open());

    auto accepted_result = co_await server.accept();
    EXPECT_TRUE(accepted_result.ok());
    if (!accepted_result.ok())
      co_return;
    auto accepted = std::move(*accepted_result);
    EXPECT_FALSE(accepted.active());
    EXPECT_TRUE(accepted.message_oriented());
    EXPECT_TRUE(accepted.connected());

    NET_EXPECT_OK(co_await server.close());
  }, boost::asio::use_future);

  io_context.run();
  future.get();
}

TEST(WebSocketTransportTest, PassiveServerHandshakeCallbackCanRejectRequest) {
  boost::asio::io_context io_context;
  auto work = boost::asio::make_work_guard(io_context);
  std::thread thread([&] { io_context.run(); });

  const auto port = std::to_string(GenerateTestNetworkPort());
  auto log = MakeTestLog();
  WebSocketTransport server{
      io_context.get_executor(),
      log.with_channel("Server"),
      "127.0.0.1",
      port,
      /*active=*/false,
      {.handshake_callback =
           [](const WebSocketServerRequest& request)
               -> std::optional<WebSocketServerReject> {
             const auto origin = request.find(http::field::origin);
             if (origin != request.end() &&
                 origin->value() == "https://allowed.local") {
               return std::nullopt;
             }
             return WebSocketServerReject{
                 .status = http::status::forbidden,
                 .body = "Origin denied",
             };
           }}};

  EXPECT_EQ(
      boost::asio::co_spawn(io_context, server.open(), boost::asio::use_future)
          .get(),
      OK);

  BeastHandshakeClient client;
  EXPECT_THROW(
      client.Connect(
          "127.0.0.1",
          port,
          [](websocket::request_type& request) {
            request.set(http::field::origin, "https://evil.local");
          }),
      boost::system::system_error);

  EXPECT_EQ(
      boost::asio::co_spawn(io_context, server.close(), boost::asio::use_future)
          .get(),
      OK);
  work.reset();
  io_context.stop();
  thread.join();
}

TEST(WebSocketTransportTest, PassiveServerAddsHeadersAndCompressionOptions) {
  boost::asio::io_context io_context;
  auto work = boost::asio::make_work_guard(io_context);
  std::thread thread([&] { io_context.run(); });

  const auto port = std::to_string(GenerateTestNetworkPort());
  auto log = MakeTestLog();
  WebSocketTransport server{
      io_context.get_executor(),
      log.with_channel("Server"),
      "127.0.0.1",
      port,
      /*active=*/false,
      {.enable_permessage_deflate = true,
       .response_headers = {{"X-Test-Header", "enabled"}}}};

  EXPECT_EQ(
      boost::asio::co_spawn(io_context, server.open(), boost::asio::use_future)
          .get(),
      OK);

  BeastHandshakeClient client;
  client.EnablePerMessageDeflate();
  client.Connect("127.0.0.1", port);

  EXPECT_EQ(client.response()["X-Test-Header"], "enabled");
  EXPECT_FALSE(
      client.response()[http::field::sec_websocket_extensions].empty());

  any_transport accepted;
  boost::asio::co_spawn(
      io_context,
      [&]() -> awaitable<void> {
        auto accepted_result = co_await server.accept();
        EXPECT_TRUE(accepted_result.ok());
        if (!accepted_result.ok())
          co_return;
        accepted = std::move(*accepted_result);
      },
      boost::asio::use_future)
      .get();

  EXPECT_EQ(
      boost::asio::co_spawn(io_context, server.close(), boost::asio::use_future)
          .get(),
      OK);
  work.reset();
  io_context.stop();
  thread.join();
}

TEST(WebSocketTransportTest, PassiveServerSupportsTlsConnections) {
  boost::asio::io_context io_context;
  auto work = boost::asio::make_work_guard(io_context);
  std::thread thread([&] { io_context.run(); });

  const auto port = std::to_string(GenerateTestNetworkPort());
  auto log = MakeTestLog();
  WebSocketTransport server{
      io_context.get_executor(),
      log.with_channel("Server"),
      "127.0.0.1",
      port,
      /*active=*/false,
      {.tls =
           WebSocketServerTlsConfig{
               .certificate_chain_pem = kTestCertificatePem,
               .private_key_pem = kTestPrivateKeyPem,
           }}};

  EXPECT_EQ(
      boost::asio::co_spawn(io_context, server.open(), boost::asio::use_future)
          .get(),
      OK);

  TlsBeastHandshakeClient client;
  client.Connect("127.0.0.1", port);

  any_transport accepted;
  boost::asio::co_spawn(
      io_context,
      [&]() -> awaitable<void> {
        auto accepted_result = co_await server.accept();
        EXPECT_TRUE(accepted_result.ok());
        if (!accepted_result.ok())
          co_return;
        accepted = std::move(*accepted_result);
      },
      boost::asio::use_future)
      .get();

  constexpr std::string_view kRequest = "ping";
  client.Write(std::span<const char>{kRequest.data(), kRequest.size()});

  std::array<char, 16> read_buffer{};
  expected<size_t> accepted_read_result{ERR_ABORTED};
  boost::asio::co_spawn(
      io_context,
      [&]() -> awaitable<void> {
        accepted_read_result = co_await accepted.read(read_buffer);
      },
      boost::asio::use_future)
      .get();
  ASSERT_TRUE(accepted_read_result.ok());
  ASSERT_EQ(*accepted_read_result, kRequest.size());
  EXPECT_TRUE(std::equal(
      kRequest.begin(), kRequest.end(), read_buffer.begin(),
      read_buffer.begin() + static_cast<std::ptrdiff_t>(*accepted_read_result)));

  expected<size_t> echoed_result{ERR_ABORTED};
  boost::asio::co_spawn(
      io_context,
      [&]() -> awaitable<void> {
        echoed_result = co_await accepted.write(
            std::span<const char>{read_buffer.data(), *accepted_read_result});
      },
      boost::asio::use_future)
      .get();
  ASSERT_TRUE(echoed_result.ok());
  EXPECT_EQ(*echoed_result, kRequest.size());

  EXPECT_EQ(client.Read(), "ping");
  EXPECT_EQ(
      boost::asio::co_spawn(io_context, server.close(), boost::asio::use_future)
          .get(),
      OK);
  work.reset();
  io_context.stop();
  thread.join();
}

TEST(WebSocketTransportTest, ActiveClientSupportsTlsConnections) {
  boost::asio::io_context io_context;
  const auto port = std::to_string(GenerateTestNetworkPort());
  auto log = MakeTestLog();

  auto future = boost::asio::co_spawn(io_context, [&]() -> awaitable<void> {
    WebSocketTransport server{
        io_context.get_executor(),
        log.with_channel("Server"),
        "127.0.0.1",
        port,
        /*active=*/false,
        {.tls =
             WebSocketServerTlsConfig{
                 .certificate_chain_pem = kTestCertificatePem,
                 .private_key_pem = kTestPrivateKeyPem,
             }}};
    WebSocketTransport client{
        io_context.get_executor(),
        log.with_channel("Client"),
        "127.0.0.1",
        port,
        /*active=*/true,
        {},
        {.tls = WebSocketClientTlsConfig{.verify_peer = false}}};

    NET_EXPECT_OK(co_await server.open());
    NET_EXPECT_OK(co_await client.open());

    auto accepted_result = co_await server.accept();
    EXPECT_TRUE(accepted_result.ok());
    if (!accepted_result.ok())
      co_return;
    auto accepted = std::move(*accepted_result);

    constexpr std::array<char, 5> kRequest = {'h', 'e', 'l', 'l', 'o'};
    auto written_result = co_await client.write(kRequest);
    EXPECT_TRUE(written_result.ok());
    if (!written_result.ok())
      co_return;

    std::array<char, 16> read_buffer{};
    auto accepted_read_result = co_await accepted.read(read_buffer);
    EXPECT_TRUE(accepted_read_result.ok());
    if (!accepted_read_result.ok())
      co_return;
    EXPECT_EQ(*accepted_read_result, kRequest.size());

    NET_EXPECT_OK(co_await server.close());
  }, boost::asio::use_future);

  io_context.run();
  future.get();
}

TEST(WebSocketTransportTest,
     ActiveClientCustomizesHandshakeAndValidatesResponse) {
  boost::asio::io_context io_context;
  const auto port = std::to_string(GenerateTestNetworkPort());
  auto log = MakeTestLog();
  bool response_validated = false;

  auto future = boost::asio::co_spawn(io_context, [&]() -> awaitable<void> {
    WebSocketTransport server{
        io_context.get_executor(),
        log.with_channel("Server"),
        "127.0.0.1",
        port,
        /*active=*/false,
        {.handshake_callback =
             [](const WebSocketServerRequest& request)
                 -> std::optional<WebSocketServerReject> {
               const auto client_header = request.find("X-Client-Header");
               if (request.target() != "/custom" ||
                   client_header == request.end() ||
                   client_header->value() != "enabled") {
                 return WebSocketServerReject{
                     .status = http::status::bad_request,
                     .body = "Invalid handshake",
                 };
               }
               return std::nullopt;
             },
         .response_headers = {{"X-Server-Header", "accepted"}}}};
    WebSocketTransport client{
        io_context.get_executor(),
        log.with_channel("Client"),
        "127.0.0.1",
        port,
        /*active=*/true,
        {},
        {.path = "/custom",
         .request_callback =
             [](WebSocketClientRequest& request) {
               request.set("X-Client-Header", "enabled");
             },
         .response_validator =
             [&](const WebSocketClientResponse& response)
                 -> std::optional<error_code> {
               response_validated = true;
               const auto server_header = response.find("X-Server-Header");
               if (server_header == response.end() ||
                   server_header->value() != "accepted") {
                 return ERR_ACCESS_DENIED;
               }
               return std::nullopt;
             }}};

    NET_EXPECT_OK(co_await server.open());
    NET_EXPECT_OK(co_await client.open());
    EXPECT_TRUE(response_validated);

    auto accepted_result = co_await server.accept();
    EXPECT_TRUE(accepted_result.ok());
    if (!accepted_result.ok())
      co_return;
    auto accepted = std::move(*accepted_result);

    constexpr std::array<char, 2> kRequest = {'o', 'k'};
    auto written_result = co_await client.write(kRequest);
    EXPECT_TRUE(written_result.ok());
    if (!written_result.ok())
      co_return;

    std::array<char, 16> read_buffer{};
    auto accepted_read_result = co_await accepted.read(read_buffer);
    EXPECT_TRUE(accepted_read_result.ok());
    if (!accepted_read_result.ok())
      co_return;
    EXPECT_EQ(*accepted_read_result, kRequest.size());

    NET_EXPECT_OK(co_await server.close());
  }, boost::asio::use_future);

  io_context.run();
  future.get();
}

TEST(WebSocketTransportTest, ActiveClientCanRejectHandshakeResponse) {
  boost::asio::io_context io_context;
  const auto port = std::to_string(GenerateTestNetworkPort());
  auto log = MakeTestLog();

  auto future = boost::asio::co_spawn(io_context, [&]() -> awaitable<void> {
    WebSocketTransport server{
        io_context.get_executor(),
        log.with_channel("Server"),
        "127.0.0.1",
        port,
        /*active=*/false};
    WebSocketTransport client{
        io_context.get_executor(),
        log.with_channel("Client"),
        "127.0.0.1",
        port,
        /*active=*/true,
        {},
        {.response_validator =
             [](const WebSocketClientResponse&)
                 -> std::optional<error_code> { return ERR_ACCESS_DENIED; }}};

    NET_EXPECT_OK(co_await server.open());
    EXPECT_EQ(co_await client.open(), ERR_ACCESS_DENIED);
    NET_EXPECT_OK(co_await server.close());
  }, boost::asio::use_future);

  io_context.run();
  future.get();
}

}  // namespace
}  // namespace transport
