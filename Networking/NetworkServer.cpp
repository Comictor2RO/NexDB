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

static std::string jsonEscape(const std::string &s) {
    std::ostringstream out;
    out << '"';

    for (unsigned char c : s) {
        switch (c) {
            case '"' :
                out << "\\\"";
                break;
            case '\\':
                out << "\\\\";
                break;
            case '\b':
                out << "\\b";
                break;
            case '\f':
                out << "\\f";
                break;
            case '\n':
                out << "\\n";
                break;
            case '\r':
                out << "\\r";
                break;
            case '\t':
                out << "\\t";
                break;
            default:
                if (c <= 0x1F)
                    out << "\\u" << std::hex << std::uppercase << std::setw(4)
                    << std::setfill('0') << static_cast<int>(c) << std::dec << std::nouppercase;
                else
                    out << c;
        }
    }
    out << '"';
    return out.str();
}

NetworkServer::NetworkServer(Engine &engine, int port, int maxFailures, int banSeconds, bool bypassLocalhost, int numThreads, bool allowRemoteDbAdmin, int handshakeTimeoutSeconds)
    : engine(engine), acceptor(io_context), configPort(port), maxFailures(maxFailures), banSeconds(banSeconds), bypassLocalhost(bypassLocalhost), numThreads(numThreads), allowRemoteDbAdmin(allowRemoteDbAdmin), handshakeTimeoutSeconds(handshakeTimeoutSeconds)
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

// Runs on the session's strand. Sends CHALLENGE, then waits for the AUTH line
// asynchronously, so a silent client holds no io thread, and arms a timer that
// closes the socket if the line does not arrive within handshakeTimeoutSeconds.
// On success it hands the session to handleClient; on failure the session is
// simply released and its socket closed when the last handler lets go of it.
void NetworkServer::startHandshake(std::shared_ptr<Session> session, const std::string &clientIp)
{
    tcp::socket &socket = session->socket;
    // All writes below use the error_code overload: a failed write means the client
    // is gone, which is a failed handshake, not an exception. They stay synchronous:
    // a few bytes go straight into the kernel send buffer and do not block.
    asio::error_code ec;

    // Check if IP is currently banned
    {
        std::lock_guard<std::mutex> lock(rateLimitMutex);
        auto it = rateLimitMap.find(clientIp);
        if (it != rateLimitMap.end())
        {
            auto &entry = it->second;
            if (entry.failCount >= maxFailures && std::chrono::steady_clock::now() < entry.bannedUntil)
            {
                asio::write(socket, asio::buffer(std::string("AUTH BANNED\n")), ec);
                if (logCallback) logCallback("[SERVER] Auth failed from: " + clientIp);
                return;
            }
            if (entry.failCount >= maxFailures && std::chrono::steady_clock::now() >= entry.bannedUntil)
                entry.failCount = 0;
        }
    }

    // Send challenge
    std::string nonce = generateNonce();
    asio::write(socket, asio::buffer("CHALLENGE " + nonce + "\n"), ec);
    if (ec) {
        if (logCallback) logCallback("[SERVER] Client disconnected during handshake: " + clientIp);
        return;
    }

    // Timeout: closing the socket aborts the pending read below. A timeout is not a
    // failed auth, so it does not count toward the IP ban.
    session->handshakeTimer.expires_after(std::chrono::seconds(handshakeTimeoutSeconds));
    session->handshakeTimer.async_wait([this, session, clientIp](asio::error_code ec) {
        // Cancelled because the AUTH line arrived, or it arrived just as the timer
        // expired: either way the handshake is no longer ours to close.
        if (ec == asio::error::operation_aborted || session->handshakeDone)
            return;
        if (logCallback) logCallback("[SERVER] Handshake timed out: " + clientIp);
        asio::error_code ignored;
        session->socket.close(ignored);
    });

    // Read into the session buffer: anything the client sent after the AUTH line
    // stays there for handleClient instead of being dropped.
    asio::async_read_until(socket, session->readBuffer, "\n", [this, session, clientIp, nonce](asio::error_code ec, std::size_t) {
        // Stop the timer on every path, success or not. Left running, it would close
        // the socket of a client that has long since authenticated.
        session->handshakeDone = true;
        session->handshakeTimer.cancel();

        if (ec) {
            // operation_aborted means the timer closed the socket and already logged it
            if (ec != asio::error::operation_aborted && logCallback)
                logCallback("[SERVER] Client disconnected during handshake: " + clientIp);
            return;
        }

        std::istream is(&session->readBuffer);
        std::string response;
        std::getline(is, response);
        if (!response.empty() && response.back() == '\r') response.pop_back();

        // Verify: client must send "AUTH <SHA256(secret + nonce)>"
        std::string expectedHash = picosha2::hash256_hex_string(authSecret + nonce);
        std::string expectedResponse = "AUTH " + expectedHash;

        asio::error_code writeEc;
        if (response == expectedResponse)
        {
            {
                std::lock_guard<std::mutex> lock(rateLimitMutex);
                rateLimitMap[clientIp].failCount = 0;
            }
            asio::write(session->socket, asio::buffer(std::string("AUTH OK\n")), writeEc);
            if (writeEc) {
                if (logCallback) logCallback("[SERVER] Client disconnected during handshake: " + clientIp);
                return;
            }
            if (logCallback) logCallback("[SERVER] Client authenticated: " + clientIp);
            handleClient(session);
        }
        else
        {
            {
                std::lock_guard<std::mutex> lock(rateLimitMutex);
                auto &entry = rateLimitMap[clientIp];
                entry.failCount++;
                if (entry.failCount >= maxFailures)
                    entry.bannedUntil = std::chrono::steady_clock::now() + std::chrono::seconds(banSeconds);
            }
            asio::write(session->socket, asio::buffer(std::string("AUTH FAIL\n")), writeEc);
            if (logCallback) logCallback("[SERVER] Auth failed from: " + clientIp);
        }
    });
}

void NetworkServer::run()
{
    std::vector<std::thread> pool;
    for (int i = 1; i < numThreads; i++)
        pool.emplace_back([this]() {
            runWorker();
        });

    runWorker();
    for (auto &t : pool)
        t.join();
}

// Safety net: an exception escaping a handler leaves io_context.run(), and on a pool
// thread that means std::terminate. asio allows run() to be called again afterwards,
// so log it and keep serving. Returns only when run() ends normally (stop()).
void NetworkServer::runWorker()
{
    for (;;) {
        try {
            io_context.run();
            return;
        }
        catch (const std::exception &e) {
            if (logCallback) logCallback("[SERVER ERROR] Handler threw: " + std::string(e.what()));
        }
        catch (...) {
            if (logCallback) logCallback("[SERVER ERROR] Handler threw an unknown exception");
        }
    }
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

// Opens the server. If configPort == 0, auto-scans 3000-8000 for a free port.
void NetworkServer::openServer()
{
    if (configPort != 0) {
        try {
            acceptor.open(tcp::v4());
            acceptor.bind(tcp::endpoint(tcp::v4(), configPort));
            acceptor.listen();
            port = configPort;
            return;
        }
        catch (const asio::system_error &e) {
            if (acceptor.is_open())
                acceptor.close();
            throw std::runtime_error("Could not bind to port " + std::to_string(configPort) + ": " + e.what());
        }
    }

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
    // Each accepted socket gets its own strand: handlers of one connection (read,
    // write, handshake timer) are serialized, different connections stay parallel.
    acceptor.async_accept(asio::make_strand(io_context), [this](asio::error_code ec, tcp::socket socket) {
        // Acceptor was closed by stop(): do not re-arm
        if (ec == asio::error::operation_aborted)
            return;

        // Re-arm first, so no early return or exception below can stop the server
        // from accepting. Another pool thread can take the next client meanwhile.
        acceptConnections();

        if (ec) {
            if (logCallback) logCallback("[SERVER ERROR] " + ec.message());
            return;
        }

        // From here on, every socket call uses the error_code overload: a client that
        // disconnects mid-handshake must only cost its own connection.
        auto session = std::make_shared<Session>(std::move(socket));
        asio::error_code ioEc;

        tcp::endpoint remote = session->socket.remote_endpoint(ioEc);
        if (ioEc) {
            if (logCallback) logCallback("[SERVER] Client disconnected before handshake: " + ioEc.message());
            return;
        }
        std::string clientIp = remote.address().to_string();

        asio::write(session->socket, asio::buffer(std::string(PROTOCOL_VERSION) + '\n'), ioEc);
        if (ioEc) {
            if (logCallback) logCallback("[SERVER] Client disconnected during handshake: " + clientIp);
            return;
        }

        if (bypassLocalhost && (clientIp == "127.0.0.1" || clientIp == "::1")) {
            asio::write(session->socket, asio::buffer(std::string("AUTH OK\n")), ioEc);
            if (ioEc) {
                if (logCallback) logCallback("[SERVER] Client disconnected during handshake: " + clientIp);
                return;
            }
            if (logCallback) logCallback("[SERVER] Client authenticated: " + clientIp);
            handleClient(session);
        }
        else {
            // The handshake starts two async operations (read + timer) on the session;
            // starting them from inside its strand keeps even their setup serialized.
            asio::dispatch(session->socket.get_executor(), [this, session, clientIp]() {
                startHandshake(session, clientIp);
            });
        }
    });
}

// Handles communication with a connected client
// The session buffer is reused across calls: if a previous read pulled in more
// than one line, async_read_until completes from the buffered data first.
void NetworkServer::handleClient(std::shared_ptr<Session> sharedSession)
{
    asio::async_read_until(sharedSession->socket, sharedSession->readBuffer, "\n", [this, sharedSession](asio::error_code ec, std::size_t bytes_transferred) {
        if (!ec && bytes_transferred > 0) {
            std::istream is(&sharedSession->readBuffer);
            std::string message;
            std::getline(is, message);
            if (!message.empty() && message.back() == '\r')
                message.pop_back();

            auto response = std::make_shared<std::string>(executeQuery(message));
            asio::async_write(sharedSession->socket, asio::buffer(*response), [this, sharedSession, response](asio::error_code ec, std::size_t) {
                if (ec && logCallback)
                    logCallback("[SERVER ERROR] Send failed: " + ec.message());
                else
                    handleClient(sharedSession);
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
        std::string upper = query.substr(0, 6);
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        if (logCallback) logCallback("[QUERY] " + upper + " — " + query);

        std::vector<Row> rows = engine.query(query, allowRemoteDbAdmin);

        std::string pending = engine.consumePendingSwitch();
        if (!pending.empty())
            return "{\"type\": \"switch\", \"db\": " + jsonEscape(pending) + "}\n";

        const std::vector<std::string> &columns = engine.getLastColumns();

        if (rows.empty() && columns.empty())
            return "{\"type\": \"ok\"}\n";

        std::string result = "{\"type\": \"rows\", \"columns\": [";
        for (size_t i = 0; i < columns.size(); i++) {
            if (i > 0) result += ",";
            result += jsonEscape(columns[i]);
        }
        result += "], \"rows\": [";

        bool first = true;
        for (const Row &row : rows) {
            if (!first) result += ",";
            first = false;
            result += "[";
            for (size_t i = 0; i < row.values.size(); i++) {
                if (i > 0) result += ",";
                result += jsonEscape(row.values[i]);
            }
            result += "]";
        }
        result += "]}\n";
        return result;
    }
    catch (const std::exception &e) {
        return std::string("{\"type\": \"error\", \"message\": ") + jsonEscape(e.what()) + "}\n";
    }
}
