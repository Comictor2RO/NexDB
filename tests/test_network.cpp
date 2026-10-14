#include <gtest/gtest.h>
#include "../Engine/Engine.hpp"
#include "../Networking/NetworkServer.hpp"
#include "../Networking/picosha2.h"
#include <thread>
#include <chrono>
#include <cstdio>
#include <sstream>
#include <filesystem>

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

// Like readLine, but gives up after `timeout` and returns "<timeout>" instead of
// blocking forever. Used by the pipelining tests: before the per-session buffer fix
// the server dropped the second line, so a plain readLine would hang the suite.
static std::string readLineWithTimeout(asio::io_context &ctx, tcp::socket &sock, asio::streambuf &buf,
                                       std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
    asio::error_code result = asio::error::would_block;
    asio::async_read_until(sock, buf, "\n", [&](asio::error_code ec, std::size_t) { result = ec; });
    ctx.restart();
    ctx.run_for(timeout);

    if (result == asio::error::would_block) {
        // Abort the pending read and let its handler run before `result` goes out of scope
        sock.close();
        ctx.restart();
        ctx.run();
        return "<timeout>";
    }
    if (result)
        return "<error: " + result.message() + ">";

    std::istream is(&buf);
    std::string line;
    std::getline(is, line);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

// A server on its own engine, for tests that need a non-default handshake timeout.
// No localhost bypass, so every connection goes through CHALLENGE / AUTH. Declare it
// in an inner scope and remove its files after the scope ends: the engine must be
// destroyed first, or the destructor recreates them. Files are also removed before the
// engine opens them, so leftovers from a crashed run cannot make CREATE TABLE fail.
struct HandshakeTestServer {
    Engine engine;
    NetworkServer server;
    std::thread thread;

    explicit HandshakeTestServer(int handshakeTimeoutSeconds)
        : engine((removeFiles(), 128), "test_nethandshake.db", "test_nethandshake.cat", "test_nethandshake.wal"),
          server(engine, 0, 3, 30, false, 4, false, handshakeTimeoutSeconds) {
        engine.query("CREATE TABLE hs_users (id INT, name STRING)");
        engine.query("INSERT INTO hs_users VALUES (1, Alice)");
        server.prepare();
        thread = std::thread([this]() { server.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    ~HandshakeTestServer() {
        server.stop();
        thread.join();
    }

    static void removeFiles() {
        std::remove("test_nethandshake.db");
        std::remove("test_nethandshake.cat");
        std::remove("test_nethandshake.wal");
    }
};

// Connects and runs banner + CHALLENGE + AUTH with the correct token, every read bounded
// by `timeout`. Returns the auth result line ("AUTH OK"), or "<timeout>" / "<error...>".
static std::string connectAndAuth(asio::io_context &ctx, tcp::socket &sock, asio::streambuf &buf,
                                  NetworkServer &server,
                                  std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
    tcp::resolver resolver(ctx);
    asio::connect(sock, resolver.resolve("127.0.0.1", std::to_string(server.getPort())));

    std::string banner = readLineWithTimeout(ctx, sock, buf, timeout);
    if (banner != NetworkServer::PROTOCOL_VERSION)
        return banner;
    std::string challengeLine = readLineWithTimeout(ctx, sock, buf, timeout);
    if (challengeLine.rfind("CHALLENGE ", 0) != 0)
        return challengeLine;

    std::string nonce = challengeLine.substr(std::string("CHALLENGE ").size());
    asio::write(sock, asio::buffer("AUTH " + picosha2::hash256_hex_string(server.getAuthToken() + nonce) + "\n"));
    return readLineWithTimeout(ctx, sock, buf, timeout);
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

        // Databases the CREATE/DROP DATABASE tests touch. Removed up front so a
        // leftover file from an earlier run cannot make those tests pass or fail
        // for the wrong reason. Missing files are a no-op.
        for (const char *name : {"net_evil", "net_localadmin", "net_flagallow"})
            for (const char *ext : {".db", ".cat", ".wal"})
                std::remove((std::string("databases/") + name + ext).c_str());
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

// --- New tests: CREATE/DROP DATABASE are refused for remote clients ---

// Test 15: every spelling is refused, and no file is created or deleted.
// The variants matter: the check runs on the parsed AST, not on the query text,
// so case and whitespace cannot get around it.
TEST_F(NetworkServerTest, DatabaseAdminIsRefusedForRemoteClients) {
    const char *queries[] = {
        "CREATE DATABASE net_evil",
        "create database net_evil",
        "   CrEaTe   DaTaBaSe   net_evil",
        "DROP DATABASE test_network",
        "drop   database   test_network"
    };

    for (const char *query : queries) {
        std::string response = sendQuery(query);
        EXPECT_NE(response.find("\"type\": \"error\""), std::string::npos) << query;
        EXPECT_NE(response.find("is not permitted for remote clients"), std::string::npos) << query;
    }

    EXPECT_FALSE(std::filesystem::exists("databases/net_evil.db"));
    EXPECT_TRUE(std::filesystem::exists("test_network.db"));
}

// Test 16: the refusal is an ordinary error frame, not a fatal one — the *same* socket
// keeps working afterwards. Done inline rather than through sendQuery(), which opens a
// fresh connection per call and so could not tell a surviving socket from a new one.
TEST_F(NetworkServerTest, RefusedDatabaseAdminKeepsSameConnectionUsable) {
    asio::io_context ctx;
    tcp::socket sock(ctx);
    tcp::resolver resolver(ctx);
    asio::connect(sock, resolver.resolve("127.0.0.1", std::to_string(server->getPort())));

    asio::streambuf buf;
    readLine(sock, buf);                                 // banner
    std::string challengeLine = readLine(sock, buf);     // CHALLENGE <nonce>
    std::string nonce = challengeLine.substr(std::string("CHALLENGE ").size());

    std::string authMsg = "AUTH " + picosha2::hash256_hex_string(server->getAuthToken() + nonce) + "\n";
    asio::write(sock, asio::buffer(authMsg));
    ASSERT_EQ(readLine(sock, buf), "AUTH OK");

    asio::write(sock, asio::buffer(std::string("CREATE DATABASE net_evil\n")));
    std::string refusal = readLine(sock, buf);
    EXPECT_NE(refusal.find("is not permitted for remote clients"), std::string::npos);

    // No reconnect between the two writes
    asio::write(sock, asio::buffer(std::string("SELECT * FROM net_users\n")));
    std::string rows = readLine(sock, buf);
    EXPECT_NE(rows.find("Alice"), std::string::npos);
}

// Test 17: the block is remote-only — the in-process (GUI) caller still gets both
TEST_F(NetworkServerTest, LocalCallerMayStillAdministerDatabases) {
    EXPECT_NO_THROW(engine->query("CREATE DATABASE net_localadmin"));
    EXPECT_TRUE(std::filesystem::exists("databases/net_localadmin.db"));

    EXPECT_NO_THROW(engine->query("DROP DATABASE net_localadmin"));
    EXPECT_FALSE(std::filesystem::exists("databases/net_localadmin.db"));
}

// Test 18: allow_remote_db_admin = true opens the door again, so the block is genuinely
// driven by the flag and not by something else on the path. Uses its own server, with the
// localhost bypass on to keep the exchange to banner + AUTH OK.
TEST_F(NetworkServerTest, AllowFlagLetsRemoteClientsAdministerDatabases) {
    // Inner scope so the engine is destroyed — and its files closed — before they are
    // removed. Deleting first and letting the destructor run afterwards just recreates
    // them, which is why earlier runs left test_network2.db behind.
    {
        Engine adminEngine(128, "test_netadmin.db", "test_netadmin.cat", "test_netadmin.wal");
        NetworkServer adminServer(adminEngine, 0, 3, 30, true, 4, /*allowRemoteDbAdmin=*/true);
        adminServer.prepare();
        std::thread adminThread([&]() { adminServer.run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));

        asio::io_context ctx;
        tcp::socket sock(ctx);
        tcp::resolver resolver(ctx);
        asio::connect(sock, resolver.resolve("127.0.0.1", std::to_string(adminServer.getPort())));

        asio::streambuf buf;
        readLine(sock, buf);                          // banner
        ASSERT_EQ(readLine(sock, buf), "AUTH OK");    // localhost bypass

        asio::write(sock, asio::buffer(std::string("CREATE DATABASE net_flagallow\n")));
        EXPECT_EQ(readLine(sock, buf), "{\"type\": \"ok\"}");
        EXPECT_TRUE(std::filesystem::exists("databases/net_flagallow.db"));

        asio::write(sock, asio::buffer(std::string("DROP DATABASE net_flagallow\n")));
        EXPECT_EQ(readLine(sock, buf), "{\"type\": \"ok\"}");
        EXPECT_FALSE(std::filesystem::exists("databases/net_flagallow.db"));

        adminServer.stop();
        adminThread.join();
    }

    std::remove("test_netadmin.db");
    std::remove("test_netadmin.cat");
    std::remove("test_netadmin.wal");
}

// --- New tests: pipelined statements ---

// Test 19: two statements in a single write() both get answered, in order.
// The server's read usually pulls both lines at once; the second one used to be
// dropped together with a per-call streambuf, leaving the client waiting forever.
TEST_F(NetworkServerTest, PipelinedQueriesInOneWriteAreAllAnswered) {
    asio::io_context ctx;
    tcp::socket sock(ctx);
    tcp::resolver resolver(ctx);
    asio::connect(sock, resolver.resolve("127.0.0.1", std::to_string(server->getPort())));

    asio::streambuf buf;
    readLine(sock, buf);                                 // banner
    std::string challengeLine = readLine(sock, buf);     // CHALLENGE <nonce>
    std::string nonce = challengeLine.substr(std::string("CHALLENGE ").size());

    std::string authMsg = "AUTH " + picosha2::hash256_hex_string(server->getAuthToken() + nonce) + "\n";
    asio::write(sock, asio::buffer(authMsg));
    ASSERT_EQ(readLine(sock, buf), "AUTH OK");

    asio::write(sock, asio::buffer(std::string(
        "INSERT INTO net_users VALUES (8, Eve)\n"
        "SELECT * FROM net_users WHERE id = 8\n")));

    EXPECT_EQ(readLineWithTimeout(ctx, sock, buf), "{\"type\": \"ok\"}");
    std::string rows = readLineWithTimeout(ctx, sock, buf);
    EXPECT_NE(rows.find("\"type\": \"rows\""), std::string::npos) << rows;
    EXPECT_NE(rows.find("Eve"), std::string::npos) << rows;
}

// Test 20: a query sent in the same write() as the AUTH line is not lost.
// The handshake reads into the session buffer, so whatever follows the AUTH line
// is still there when handleClient starts reading.
TEST_F(NetworkServerTest, QuerySentTogetherWithAuthIsAnswered) {
    asio::io_context ctx;
    tcp::socket sock(ctx);
    tcp::resolver resolver(ctx);
    asio::connect(sock, resolver.resolve("127.0.0.1", std::to_string(server->getPort())));

    asio::streambuf buf;
    readLine(sock, buf);                                 // banner
    std::string challengeLine = readLine(sock, buf);     // CHALLENGE <nonce>
    std::string nonce = challengeLine.substr(std::string("CHALLENGE ").size());

    std::string authMsg = "AUTH " + picosha2::hash256_hex_string(server->getAuthToken() + nonce) + "\n";
    asio::write(sock, asio::buffer(authMsg + "SELECT * FROM net_users WHERE id = 1\n"));

    ASSERT_EQ(readLineWithTimeout(ctx, sock, buf), "AUTH OK");
    std::string rows = readLineWithTimeout(ctx, sock, buf);
    EXPECT_NE(rows.find("Alice"), std::string::npos) << rows;
}

// --- New test: clients that vanish mid-handshake ---

// Test 21: clients that hard-close (RST, via linger 0) at every handshake stage must not
// take the server down. Before the fix, the server's next write on such a socket threw
// out of the accept handler and std::terminate killed the whole test binary.
// The AUTH stage sends the *correct* hash, so the rate limiter never bans 127.0.0.1.
TEST_F(NetworkServerTest, ServerSurvivesClientsThatDisconnectAbruptly) {
    for (int i = 0; i < 30; i++) {
        asio::io_context ctx;
        tcp::socket sock(ctx);
        tcp::resolver resolver(ctx);
        asio::connect(sock, resolver.resolve("127.0.0.1", std::to_string(server->getPort())));
        sock.set_option(asio::socket_base::linger(true, 0));

        asio::streambuf buf;
        switch (i % 3) {
            case 0:                                          // gone before the banner
                break;
            case 1:                                          // gone after CHALLENGE
                readLine(sock, buf);
                readLine(sock, buf);
                break;
            case 2: {                                        // gone right after AUTH
                readLine(sock, buf);
                std::string challengeLine = readLine(sock, buf);
                std::string nonce = challengeLine.substr(std::string("CHALLENGE ").size());
                std::string authMsg = "AUTH " + picosha2::hash256_hex_string(server->getAuthToken() + nonce) + "\n";
                asio::write(sock, asio::buffer(authMsg));
                break;
            }
        }

        asio::error_code ec;
        sock.close(ec);
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Still alive and still accepting
    std::string response = sendQuery("SELECT * FROM net_users");
    EXPECT_NE(response.find("Alice"), std::string::npos) << response;
}

// --- New test: CREATE TABLE on an existing name ---

// Test 26: a client gets an error frame, not {"type": "ok"}, when the name is taken —
// otherwise it cannot tell "created" from "already there with another schema"
TEST_F(NetworkServerTest, CreateExistingTableReturnsError) {
    std::string response = sendQuery("CREATE TABLE net_users (email STRING)");
    EXPECT_NE(response.find("\"type\": \"error\""), std::string::npos) << response;
    EXPECT_NE(response.find("already exists"), std::string::npos) << response;

    // Original schema and data are untouched
    std::string rows = sendQuery("SELECT * FROM net_users");
    EXPECT_NE(rows.find("\"columns\": [\"id\",\"name\"]"), std::string::npos) << rows;
    EXPECT_NE(rows.find("Alice"), std::string::npos) << rows;
}

// --- New test: SQL NULL on the wire ---

// Test 27: SQL NULL is a bare JSON null, while the string 'NULL' stays the JSON string
// "NULL". Before, both came back as "NULL" and a client could not tell them apart
TEST_F(NetworkServerTest, NullIsJsonNullOnTheWire) {
    EXPECT_EQ(sendQuery("INSERT INTO net_users VALUES (8, NULL)"), "{\"type\": \"ok\"}\n");
    EXPECT_EQ(sendQuery("INSERT INTO net_users VALUES (9, 'NULL')"), "{\"type\": \"ok\"}\n");

    std::string r1 = sendQuery("SELECT * FROM net_users WHERE id = 8");
    EXPECT_NE(r1.find("[\"8\",null]"), std::string::npos) << r1;

    std::string r2 = sendQuery("SELECT * FROM net_users WHERE id = 9");
    EXPECT_NE(r2.find("[\"9\",\"NULL\"]"), std::string::npos) << r2;
}

// --- New tests: handshake timeout ---

// Test 22: more silent clients than io threads do not freeze the server. The handshake
// timeout is set far above the read limit, so a pass proves the handshake no longer
// holds a thread while it waits, not that the timer happened to free one in time.
// Before the fix every silent client parked an io thread in a synchronous read_until.
TEST_F(NetworkServerTest, SilentClientsDoNotBlockOtherClients) {
    {
        HandshakeTestServer hs(30);

        asio::io_context silentCtx;
        std::vector<tcp::socket> silent;
        tcp::resolver resolver(silentCtx);
        for (int i = 0; i < 6; i++) {                     // 6 > 4 io threads
            silent.emplace_back(silentCtx);
            asio::connect(silent.back(), resolver.resolve("127.0.0.1", std::to_string(hs.server.getPort())));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        asio::io_context ctx;
        tcp::socket sock(ctx);
        asio::streambuf buf;
        auto limit = std::chrono::milliseconds(1000);
        ASSERT_EQ(connectAndAuth(ctx, sock, buf, hs.server, limit), "AUTH OK");

        asio::write(sock, asio::buffer(std::string("SELECT * FROM hs_users\n")));
        std::string rows = readLineWithTimeout(ctx, sock, buf, limit);
        EXPECT_NE(rows.find("Alice"), std::string::npos) << rows;
    }
    HandshakeTestServer::removeFiles();
}

// Test 23: a client that never sends AUTH is disconnected once the timeout expires.
TEST_F(NetworkServerTest, SilentClientIsDisconnectedAfterHandshakeTimeout) {
    {
        HandshakeTestServer hs(1);

        asio::io_context ctx;
        tcp::socket sock(ctx);
        tcp::resolver resolver(ctx);
        asio::connect(sock, resolver.resolve("127.0.0.1", std::to_string(hs.server.getPort())));

        asio::streambuf buf;
        readLine(sock, buf);                                 // banner
        readLine(sock, buf);                                 // CHALLENGE <nonce>

        auto start = std::chrono::steady_clock::now();
        std::string line = readLineWithTimeout(ctx, sock, buf, std::chrono::milliseconds(3000));
        auto elapsed = std::chrono::steady_clock::now() - start;

        // Closed by the server (EOF / reset), not left hanging until our own limit
        EXPECT_EQ(line.rfind("<error", 0), 0u) << line;
        // ...and not straight away either: the client did get its full second
        EXPECT_GE(elapsed, std::chrono::milliseconds(800));
    }
    HandshakeTestServer::removeFiles();
}

// Test 24: the timer is cancelled once AUTH arrives. A client that authenticates and
// then waits past the timeout must still be served on the same socket.
TEST_F(NetworkServerTest, AuthenticatedClientOutlivesHandshakeTimeout) {
    {
        HandshakeTestServer hs(1);

        asio::io_context ctx;
        tcp::socket sock(ctx);
        asio::streambuf buf;
        ASSERT_EQ(connectAndAuth(ctx, sock, buf, hs.server), "AUTH OK");

        std::this_thread::sleep_for(std::chrono::milliseconds(1500));

        asio::write(sock, asio::buffer(std::string("SELECT * FROM hs_users\n")));
        std::string rows = readLineWithTimeout(ctx, sock, buf);
        EXPECT_NE(rows.find("Alice"), std::string::npos) << rows;
    }
    HandshakeTestServer::removeFiles();
}

// Test 25: timeouts do not count toward the IP ban. As many silent clients as
// maxFailures time out, and the next client with the right token still gets in.
TEST_F(NetworkServerTest, HandshakeTimeoutDoesNotCountTowardBan) {
    {
        HandshakeTestServer hs(1);

        asio::io_context silentCtx;
        std::vector<tcp::socket> silent;
        tcp::resolver resolver(silentCtx);
        for (int i = 0; i < 3; i++) {                     // == maxFailures
            silent.emplace_back(silentCtx);
            asio::connect(silent.back(), resolver.resolve("127.0.0.1", std::to_string(hs.server.getPort())));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1500));   // all three time out

        asio::io_context ctx;
        tcp::socket sock(ctx);
        asio::streambuf buf;
        EXPECT_EQ(connectAndAuth(ctx, sock, buf, hs.server), "AUTH OK");
    }
    HandshakeTestServer::removeFiles();
}
