#include <gtest/gtest.h>
#include "../Engine/Engine.hpp"
#include "../Networking/NetworkServer.hpp"
#include "../Networking/picosha2.h"
#include <thread>
#include <chrono>
#include <cstdio>
#include <sstream>

// Reads one '\n'-terminated line from the socket, reusing `buf` across calls so
// that bytes from a combined TCP segment (e.g. banner + CHALLENGE arriving
// together) are not lost between reads.
static std::string readLine(tcp::socket &sock, asio::streambuf &buf) {
    asio::read_until(sock, buf, "\n");
    std::istream is(&buf);
    std::string line;
    std::getline(is, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

class NetworkServerTest : public ::testing::Test {
protected:
    Engine *engine = nullptr;
    NetworkServer *server = nullptr;
    std::thread serverThread;

    void SetUp() override {
        cleanup();
        engine = new Engine(128, "test_network.db", "test_network.cat", "test_network.wal");
        engine->query("CREATE TABLE net_users (id INT, name STRING)");
        engine->query("INSERT INTO net_users VALUES (1, Alice)");
        engine->query("INSERT INTO net_users VALUES (2, Bob)");

        server = new NetworkServer(*engine, 0, 3, 30, false);  // no localhost bypass in tests
        server->prepare();
        serverThread = std::thread([this]() { server->run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void TearDown() override {
        server->stop();
        serverThread.join();
        delete server;
        delete engine;
        cleanup();
    }

    void cleanup() {
        std::remove("test_network.cat");
        std::remove("test_network.db");
        std::remove("test_network.wal");
        std::remove("server_auth.conf");
    }

    // Connects, completes challenge-response with correct token, sends SQL, returns response
    std::string sendQuery(const std::string &query) {
        return sendQueryWithToken(server->getAuthToken(), query);
    }

    // Connects, completes challenge-response with given token, sends SQL, returns response
    std::string sendQueryWithToken(const std::string &token, const std::string &query) {
        asio::io_context ctx;
        tcp::socket sock(ctx);
        tcp::resolver resolver(ctx);
        auto endpoints = resolver.resolve("127.0.0.1", std::to_string(server->getPort()));
        asio::connect(sock, endpoints);

        // Read protocol banner (NEXDB/...) then CHALLENGE <nonce> — same streambuf
        asio::streambuf buf;
        readLine(sock, buf);                              // banner
        std::string challengeLine = readLine(sock, buf);  // CHALLENGE <nonce>

        // Parse nonce
        std::string nonce = challengeLine.substr(std::string("CHALLENGE ").size());

        // Send AUTH <SHA256(token + nonce)>
        std::string hash = picosha2::hash256_hex_string(token + nonce);
        std::string authMsg = "AUTH " + hash + "\n";
        asio::write(sock, asio::buffer(authMsg));

        // Read AUTH OK / AUTH FAIL
        asio::streambuf buf2;
        asio::read_until(sock, buf2, "\n");
        std::istream is2(&buf2);
        std::string authResponse;
        std::getline(is2, authResponse);
        if (!authResponse.empty() && authResponse.back() == '\r') authResponse.pop_back();

        if (authResponse != "AUTH OK")
            return authResponse;

        // Send SQL query
        asio::write(sock, asio::buffer(query + "\n"));

        // Read the single-line JSON response (one frame terminated by \n)
        asio::streambuf readBuf;
        asio::read_until(sock, readBuf, "\n");
        std::istream lineStream(&readBuf);
        std::string line;
        std::getline(lineStream, line);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        return line + "\n";
    }

    // Connects and sends wrong auth N times, returns last server response
    std::string sendWrongAuth(int times) {
        std::string lastResponse;
        for (int i = 0; i < times; i++) {
            asio::io_context ctx;
            tcp::socket sock(ctx);
            tcp::resolver resolver(ctx);
            auto endpoints = resolver.resolve("127.0.0.1", std::to_string(server->getPort()));
            asio::connect(sock, endpoints);

            // Banner first, then either CHALLENGE <nonce> or AUTH BANNED
            asio::streambuf buf;
            readLine(sock, buf);                          // banner
            std::string firstLine = readLine(sock, buf);

            if (firstLine == "AUTH BANNED") {
                lastResponse = firstLine;
                continue;
            }

            // Got a challenge — send wrong hash
            asio::write(sock, asio::buffer(std::string("AUTH wronghash\n")));

            asio::streambuf buf2;
            asio::read_until(sock, buf2, "\n");
            std::istream is2(&buf2);
            std::getline(is2, lastResponse);
            if (!lastResponse.empty() && lastResponse.back() == '\r') lastResponse.pop_back();
        }
        return lastResponse;
    }
};

// --- Teste existente ---

// Test 1: SELECT * returneaza toate rows
TEST_F(NetworkServerTest, SelectAllReturnsRows) {
    std::string response = sendQuery("SELECT * FROM net_users");
    EXPECT_NE(response.find("Alice"), std::string::npos);
    EXPECT_NE(response.find("Bob"), std::string::npos);
}

// Test 2: INSERT returneaza OK
TEST_F(NetworkServerTest, InsertReturnsOK) {
    std::string response = sendQuery("INSERT INTO net_users VALUES (3, Charlie)");
    EXPECT_EQ(response, "{\"type\": \"ok\"}\n");
}

// Test 3: SELECT cu WHERE returneaza doar row-ul corect
TEST_F(NetworkServerTest, SelectWithConditionReturnsFilteredRow) {
    std::string response = sendQuery("SELECT * FROM net_users WHERE id = 1");
    EXPECT_NE(response.find("Alice"), std::string::npos);
    EXPECT_EQ(response.find("Bob"), std::string::npos);
}

// Test 4: invalid query returns ERROR
TEST_F(NetworkServerTest, InvalidQueryReturnsError) {
    std::string response = sendQuery("SELECT * FROM nonexistent_table");
    EXPECT_NE(response.find("\"type\": \"error\""), std::string::npos);
}

// Test 5: DELETE returns OK and row disappears
TEST_F(NetworkServerTest, DeleteReturnsOK) {
    std::string response = sendQuery("DELETE FROM net_users WHERE id = 1");
    EXPECT_EQ(response, "{\"type\": \"ok\"}\n");

    std::string selectResponse = sendQuery("SELECT * FROM net_users");
    EXPECT_EQ(selectResponse.find("Alice"), std::string::npos);
    EXPECT_NE(selectResponse.find("Bob"), std::string::npos);
}

// Test 6: Auto port detection - al doilea server alege un port diferit
TEST_F(NetworkServerTest, AutoPortDetectionPicksDifferentPort) {
    size_t firstPort = server->getPort();
    EXPECT_GE(firstPort, 3000u);
    EXPECT_LE(firstPort, 8000u);

    Engine engine2(128, "test_network2.db", "test_network2.cat", "test_network2.wal");
    NetworkServer server2(engine2, 0, 3, 30, false);
    server2.prepare();
    std::thread t([&]() { server2.run(); });
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    size_t secondPort = server2.getPort();
    EXPECT_NE(secondPort, firstPort);
    EXPECT_GE(secondPort, 3000u);
    EXPECT_LE(secondPort, 8000u);

    server2.stop();
    t.join();
    std::remove("test_network2.cat");
    std::remove("test_network2.db");
    std::remove("test_network2.wal");
}

// --- New tests: authentication ---

// Test 7: correct token → AUTH OK, SQL works
TEST_F(NetworkServerTest, CorrectTokenAuthenticates) {
    std::string response = sendQueryWithToken(server->getAuthToken(), "SELECT * FROM net_users");
    EXPECT_NE(response.find("Alice"), std::string::npos);
}

// Test 8: wrong token → AUTH FAIL
TEST_F(NetworkServerTest, WrongTokenReturnsAuthFail) {
    asio::io_context ctx;
    tcp::socket sock(ctx);
    tcp::resolver resolver(ctx);
    asio::connect(sock, resolver.resolve("127.0.0.1", std::to_string(server->getPort())));

    asio::streambuf buf;
    readLine(sock, buf);   // banner
    readLine(sock, buf);   // CHALLENGE <nonce>

    asio::write(sock, asio::buffer(std::string("AUTH totally_wrong_hash\n")));

    asio::streambuf buf2;
    asio::read_until(sock, buf2, "\n");
    std::istream is2(&buf2);
    std::string authResponse;
    std::getline(is2, authResponse);
    if (!authResponse.empty() && authResponse.back() == '\r') authResponse.pop_back();

    EXPECT_EQ(authResponse, "AUTH FAIL");
}

// Test 9: nonce is different per connection (not static/hardcoded)
TEST_F(NetworkServerTest, NonceIsDifferentPerConnection) {
    auto getNonce = [&]() {
        asio::io_context ctx;
        tcp::socket sock(ctx);
        tcp::resolver resolver(ctx);
        asio::connect(sock, resolver.resolve("127.0.0.1", std::to_string(server->getPort())));
        asio::streambuf buf;
        readLine(sock, buf);                    // banner
        std::string line = readLine(sock, buf); // CHALLENGE <nonce>
        // Send a dummy AUTH to unblock the server's read_until before disconnecting
        asio::write(sock, asio::buffer(std::string("AUTH skip\n")));
        asio::streambuf discard;
        asio::read_until(sock, discard, "\n");
        return line; // "CHALLENGE <nonce>"
    };

    std::string c1 = getNonce();
    std::string c2 = getNonce();
    EXPECT_NE(c1, c2);
}

// Test 10: 3 wrong tokens → 4th response is AUTH BANNED
TEST_F(NetworkServerTest, ThreeFailsResultsInBan) {
    sendWrongAuth(3);
    std::string lastResponse = sendWrongAuth(1);
    EXPECT_EQ(lastResponse, "AUTH BANNED");
}

// Test 11: getAuthToken returns a non-empty string after prepare()
TEST_F(NetworkServerTest, AuthTokenIsNonEmpty) {
    EXPECT_FALSE(server->getAuthToken().empty());
}

// --- New tests: JSON wire protocol ---

// Test 12: OK / rows / error responses are valid single-line JSON objects
TEST_F(NetworkServerTest, ResponsesAreJsonObjects) {
    EXPECT_EQ(sendQuery("INSERT INTO net_users VALUES (5, Dave)"), "{\"type\": \"ok\"}\n");

    std::string rows = sendQuery("SELECT * FROM net_users WHERE id = 5");
    EXPECT_NE(rows.find("\"type\": \"rows\""), std::string::npos);
    EXPECT_NE(rows.find("\"Dave\""), std::string::npos);

    std::string err = sendQuery("SELECT * FROM nonexistent_table");
    EXPECT_NE(err.find("\"type\": \"error\""), std::string::npos);
}

// Test 13: values containing '|', newlines, or the literal "END" survive serialization
// (these used to collide with the old '|'-delimited / END-terminated protocol)
TEST_F(NetworkServerTest, SpecialCharactersInValuesAreEscaped) {
    sendQuery("INSERT INTO net_users VALUES (6, 'a|b')");
    sendQuery("INSERT INTO net_users VALUES (7, 'END')");

    // The response must remain a single JSON line and preserve the raw value.
    std::string r1 = sendQuery("SELECT * FROM net_users WHERE id = 6");
    EXPECT_NE(r1.find("\"type\": \"rows\""), std::string::npos);
    EXPECT_NE(r1.find("a|b"), std::string::npos);

    std::string r2 = sendQuery("SELECT * FROM net_users WHERE id = 7");
    EXPECT_NE(r2.find("\"type\": \"rows\""), std::string::npos);
    EXPECT_NE(r2.find("END"), std::string::npos);
}

// --- New test: protocol version banner ---

// Test 14: the very first line on any connection is the protocol banner
TEST_F(NetworkServerTest, ServerSendsProtocolBanner) {
    asio::io_context ctx;
    tcp::socket sock(ctx);
    tcp::resolver resolver(ctx);
    asio::connect(sock, resolver.resolve("127.0.0.1", std::to_string(server->getPort())));

    asio::streambuf buf;
    std::string banner = readLine(sock, buf);
    EXPECT_EQ(banner, NetworkServer::PROTOCOL_VERSION);

    // Unblock the server's read_until before disconnecting
    asio::write(sock, asio::buffer(std::string("AUTH skip\n")));
    asio::streambuf discard;
    asio::error_code ec;
    asio::read_until(sock, discard, "\n", ec);
}
