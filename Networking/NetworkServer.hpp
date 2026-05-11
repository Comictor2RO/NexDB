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

class Engine;

using asio::ip::tcp;

struct RateLimitEntry {
    int failCount = 0;
    std::chrono::steady_clock::time_point bannedUntil{};
};

class NetworkServer {
    public:
        NetworkServer(Engine &engine);

        void prepare();
        void run();
        void stop();
        size_t getPort() const;
        std::string getAuthToken() const;
        void setLogCallback(std::function<void(const std::string&)> callback);

    private:
        size_t port = 0;
        asio::io_context io_context;
        tcp::acceptor acceptor;
        Engine &engine;
        std::mutex engineMutex;
        std::function<void(const std::string&)> logCallback;

        std::string authSecret;
        std::map<std::string, RateLimitEntry> rateLimitMap;
        std::mutex rateLimitMutex;

        void openServer();
        void acceptConnections();
        void handleClient(tcp::socket socket);
        std::string executeQuery(std::string &query);

        void loadOrCreateSecret();
        std::string generateNonce();
        bool handleHandshake(tcp::socket &socket, const std::string &clientIp);
};

#endif
