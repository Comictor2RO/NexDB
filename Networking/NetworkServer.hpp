#ifndef NETWORK_SERVER_HPP
#define NETWORK_SERVER_HPP

#include <iostream>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef NOGDI
#define NOGDI
#endif
#ifndef NOUSER
#define NOUSER
#endif

#include <asio.hpp>
#include <asio/ts/buffer.hpp>
#include <asio/ts/internet.hpp>
#include <string>
#include <mutex>
#include <map>
#include <chrono>
#include <functional>
#include <memory>

class Engine;

using asio::ip::tcp;

struct RateLimitEntry {
    int failCount = 0;
    std::chrono::steady_clock::time_point bannedUntil{};
};

// One per connection. readBuffer must outlive a single read: async_read_until
// may pull several pipelined lines from the socket at once.
// The socket is accepted on its own strand, and the timer is built on the socket's
// executor, so the read handler and the timeout handler never run concurrently.
struct Session {
    explicit Session(tcp::socket s)
        : socket(std::move(s)), handshakeTimer(socket.get_executor()) {}

    tcp::socket socket;
    asio::streambuf readBuffer;
    asio::steady_timer handshakeTimer;
    // Set once the AUTH line (or a read error) has arrived. A timer that already
    // expired cannot be cancelled anymore, so its handler checks this instead.
    bool handshakeDone = false;
};

class NetworkServer {
    public:
        NetworkServer(Engine &engine, int port = 0, int maxFailures = 3, int banSeconds = 30, bool bypassLocalhost = true, int numThreads = 4, bool allowRemoteDbAdmin = false, int handshakeTimeoutSeconds = 5);

        static constexpr const char* PROTOCOL_VERSION = "NEXDB/1.0.0";

        void prepare();
        void run();
        void stop();
        size_t getPort() const;
        std::string getAuthToken() const;
        void setLogCallback(std::function<void(const std::string&)> callback);

    private:
        int configPort;
        int maxFailures;
        int banSeconds;
        bool bypassLocalhost;
        bool allowRemoteDbAdmin;
        int handshakeTimeoutSeconds;
        size_t port = 0;
        asio::io_context io_context;
        tcp::acceptor acceptor;
        Engine &engine;
        std::mutex engineMutex;
        std::function<void(const std::string&)> logCallback;
        int numThreads;

        std::string authSecret;
        std::map<std::string, RateLimitEntry> rateLimitMap;
        std::mutex rateLimitMutex;

        void openServer();
        void runWorker();
        void acceptConnections();
        void handleClient(std::shared_ptr<Session> sharedSession);
        std::string executeQuery(std::string &query);

        void loadOrCreateSecret();
        std::string generateNonce();
        void startHandshake(std::shared_ptr<Session> session, const std::string &clientIp);
};

#endif
