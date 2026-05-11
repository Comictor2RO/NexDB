#include "../Engine/Engine.hpp"
#include "NetworkServer.hpp"
#include "picosha2.h"
#include <iostream>
#include <fstream>
#include <random>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <iomanip>

NetworkServer::NetworkServer(Engine &engine)
    : engine(engine), acceptor(io_context)
{};

void NetworkServer::prepare()
{
    io_context.restart();
    loadOrCreateSecret();
    openServer();
    acceptConnections();
}

void NetworkServer::loadOrCreateSecret()
{
    // 1. Environment variable (highest priority)
    const char *envToken = std::getenv("VDT_AUTH_TOKEN");
    if (envToken && std::strlen(envToken) > 0)
    {
        authSecret = envToken;
        if (logCallback) logCallback("[SERVER] Auth token loaded from VDT_AUTH_TOKEN.");
        return;
    }

    // 2. Config file
    const std::string configFile = "server_auth.conf";
    std::ifstream f(configFile);
    if (f.is_open())
    {
        std::getline(f, authSecret);
        if (!authSecret.empty())
        {
            if (logCallback) logCallback("[SERVER] Auth token loaded from server_auth.conf.");
            return;
        }
    }

    // 3. Generate a random token, save it
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<uint64_t> dist;
    std::ostringstream raw;
    for (int i = 0; i < 4; i++) raw << dist(gen);
    authSecret = picosha2::hash256_hex_string(raw.str()).substr(0, 32);

    std::ofstream out(configFile);
    out << authSecret;
    if (logCallback) logCallback("[SERVER] Generated auth token: " + authSecret + " (saved to server_auth.conf)");
}

std::string NetworkServer::generateNonce()
{
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<uint32_t> dist(0, 255);
    std::ostringstream oss;
    for (int i = 0; i < 16; i++)
        oss << std::hex << std::setw(2) << std::setfill('0') << dist(gen);
    return oss.str();
}

bool NetworkServer::handleHandshake(tcp::socket &socket, const std::string &clientIp)
{
    // Check if IP is currently banned
    {
        std::lock_guard<std::mutex> lock(rateLimitMutex);
        auto it = rateLimitMap.find(clientIp);
        if (it != rateLimitMap.end())
        {
            auto &entry = it->second;
            if (entry.failCount >= 3 && std::chrono::steady_clock::now() < entry.bannedUntil)
            {
                asio::write(socket, asio::buffer(std::string("AUTH BANNED\n")));
                return false;
            }
            if (entry.failCount >= 3 && std::chrono::steady_clock::now() >= entry.bannedUntil)
                entry.failCount = 0;
        }
    }

    // Send challenge
    std::string nonce = generateNonce();
    asio::write(socket, asio::buffer("CHALLENGE " + nonce + "\n"));

    // Read client response (synchronous, with 5s timeout via deadline)
    asio::streambuf buf;
    asio::error_code ec;
    asio::read_until(socket, buf, "\n", ec);
    if (ec) return false;

    std::istream is(&buf);
    std::string response;
    std::getline(is, response);
    if (!response.empty() && response.back() == '\r') response.pop_back();

    // Verify: client must send "AUTH <SHA256(secret + nonce)>"
    std::string expectedHash = picosha2::hash256_hex_string(authSecret + nonce);
    std::string expectedResponse = "AUTH " + expectedHash;

    if (response == expectedResponse)
    {
        std::lock_guard<std::mutex> lock(rateLimitMutex);
        rateLimitMap[clientIp].failCount = 0;
        asio::write(socket, asio::buffer(std::string("AUTH OK\n")));
        return true;
    }
    else
    {
        std::lock_guard<std::mutex> lock(rateLimitMutex);
        auto &entry = rateLimitMap[clientIp];
        entry.failCount++;
        if (entry.failCount >= 3)
            entry.bannedUntil = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        asio::write(socket, asio::buffer(std::string("AUTH FAIL\n")));
        return false;
    }
}

void NetworkServer::run()
{
    io_context.run();
}

size_t NetworkServer::getPort() const
{
    return port;
}

std::string NetworkServer::getAuthToken() const
{
    return authSecret;
}

void NetworkServer::setLogCallback(std::function<void(const std::string&)> callback)
{
    logCallback = callback;
}

void NetworkServer::stop()
{
    acceptor.close();
    io_context.stop();
    std::cout << "Server stopped." << std::endl;
}

// Opens the server, auto-detecting a free port in range [3000, 8000]
void NetworkServer::openServer()
{
    for (size_t p = 3000; p <= 8000; p++) {
        try {
            acceptor.open(tcp::v4());
            acceptor.bind(tcp::endpoint(tcp::v4(), p));
            acceptor.listen();
            port = p;
            return;
        }
        catch (const asio::system_error &) {
            if (acceptor.is_open())
                acceptor.close();
        }
    }
    throw std::runtime_error("No free port found in range 3000-8000");
}

// Accepts incoming connections and handles them async
void NetworkServer::acceptConnections()
{
    acceptor.async_accept(io_context, [this](asio::error_code ec, tcp::socket socket) {
        if (!ec) {
            std::string clientIp = socket.remote_endpoint().address().to_string();
            if (handleHandshake(socket, clientIp))
            {
                if (logCallback) logCallback("[SERVER] Client authenticated: " + clientIp);
                handleClient(std::move(socket));
            }
            else
            {
                if (logCallback) logCallback("[SERVER] Auth failed from: " + clientIp);
            }
        }
        else if (ec == asio::error::operation_aborted) {
            return;
        }
        else {
            if (logCallback) logCallback("[SERVER ERROR] " + ec.message());
        }

        acceptConnections();
    });
}

// Handles communication with a connected client
void NetworkServer::handleClient(tcp::socket socket)
{
    auto sharedSocket = std::make_shared<tcp::socket>(std::move(socket));
    auto buffer = std::make_shared<asio::streambuf>();

    asio::async_read_until(*sharedSocket, *buffer, "\n", [this, sharedSocket, buffer](asio::error_code ec, std::size_t bytes_transferred) {
        if (!ec && bytes_transferred > 0) {
            std::istream is(buffer.get());
            std::string message;
            std::getline(is, message);
            if (!message.empty() && message.back() == '\r')
                message.pop_back();

            auto response = std::make_shared<std::string>(executeQuery(message));
            asio::async_write(*sharedSocket, asio::buffer(*response), [this, sharedSocket, response](asio::error_code ec, std::size_t) {
                if (ec && logCallback) logCallback("[SERVER ERROR] Send failed: " + ec.message());
            });
        }
        else {
            if (logCallback) logCallback("[SERVER ERROR] Read failed: " + ec.message());
        }
    });
}

std::string NetworkServer::executeQuery(std::string &query)
{
    std::lock_guard<std::mutex> lock(engineMutex);
    try {
        std::vector<Row> rows = engine.query(query);

        if (rows.empty())
            return "OK\n";

        std::string result;
        for (const Row &row : rows) {
            for (size_t i = 0; i < row.values.size(); i++) {
                if (i > 0) result += "|";
                result += row.values[i];
            }
            result += "\n";
        }
        return result;
    }
    catch (const std::exception &e) {
        return std::string("ERROR: ") + e.what() + "\n";
    }
}
