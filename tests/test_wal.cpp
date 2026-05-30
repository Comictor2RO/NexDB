#include <gtest/gtest.h>
#include "../WALManager/WALManager.hpp"
#include "../Engine/Engine.hpp"
#include <fstream>
#include <cstdio>

class WALManagerTest : public ::testing::Test {
protected:
    std::string testWalFile = "test_engine.wal";

    void SetUp() override {
        std::remove(testWalFile.c_str());
    }

    void TearDown() override {
        std::remove(testWalFile.c_str());
    }

    // Helper to read raw file content for verification
    std::vector<std::string> readWalLines() {
        std::vector<std::string> lines;
        std::ifstream file(testWalFile);
        std::string line;
        while (std::getline(file, line)) {
            if (!line.empty()) {
                lines.push_back(line);
            }
        }
        return lines;
    }
};

//Test 1: logInsert writes a correctly formatted uncommitted entry to the WAL file
TEST_F(WALManagerTest, LogInsertCreatesEntry) {
    WALManager wal(testWalFile);
    wal.logInsert("users", "1|Alice|25");

    auto lines = readWalLines();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0], "INSERT|users|1|Alice|25|0");
}

//Test 2: commit appends COMMIT marker after entry
TEST_F(WALManagerTest, CommitUpdatesEntry) {
    WALManager wal(testWalFile);
    wal.logInsert("users", "1|Alice|25");
    wal.commit();

    auto lines = readWalLines();
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], "INSERT|users|1|Alice|25|0");
    EXPECT_EQ(lines[1], "COMMIT");
}

//Test 3: multiple inserts followed by commit appends COMMIT marker
TEST_F(WALManagerTest, MultipleInsertsAndCommit) {
    WALManager wal(testWalFile);
    wal.logInsert("users", "1|Alice|25");
    wal.logInsert("users", "2|Bob|30");
    wal.commit();

    auto lines = readWalLines();
    ASSERT_EQ(lines.size(), 3);
    EXPECT_EQ(lines[0], "INSERT|users|1|Alice|25|0");
    EXPECT_EQ(lines[1], "INSERT|users|2|Bob|30|0");
    EXPECT_EQ(lines[2], "COMMIT");
}

//Test 4: recover replays uncommitted WAL entries and inserts the rows into the table
TEST_F(WALManagerTest, RecoverReplaysUncommitted) {
    const std::string catFile = "test_wal4.cat";
    const std::string dbFile  = "test_wal4.db";
    std::remove(catFile.c_str());
    std::remove(dbFile.c_str());
    std::remove(testWalFile.c_str());

    // Create table schema using a setup engine
    {
        Engine setup(128, dbFile, catFile, testWalFile);
        setup.query("CREATE TABLE users (id INT, name STRING, age INT)");
    }
    // Remove WAL written by setup, then write an uncommitted INSERT
    std::remove(testWalFile.c_str());
    {
        WALManager wal(testWalFile);
        wal.logInsert("users", "1|Alice|25");
    }

    // Engine constructor calls recover() — replays the uncommitted INSERT
    Engine engine(128, dbFile, catFile, testWalFile);

    auto results = engine.query("SELECT * FROM users");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].values[0], "1");
    EXPECT_EQ(results[0].values[1], "Alice");
    EXPECT_EQ(results[0].values[2], "25");

    std::remove(catFile.c_str());
    std::remove(dbFile.c_str());
}

//Test 5: logDelete with condition writes correct format
TEST_F(WALManagerTest, LogDeleteWithConditionCreatesEntry) {
    WALManager wal(testWalFile);
    Condition cond;
    cond.column = "id";
    cond.op    = "=";
    cond.value = "1";
    wal.logDelete("users", &cond);

    auto lines = readWalLines();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0], "DELETE|users|id~=~1|0");
}

//Test 6: logDelete without condition writes empty condition field
TEST_F(WALManagerTest, LogDeleteNoConditionCreatesEntry) {
    WALManager wal(testWalFile);
    wal.logDelete("users", nullptr);

    auto lines = readWalLines();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0], "DELETE|users||0");
}

//Test 7: logUpdate writes condition and assignments in correct format
TEST_F(WALManagerTest, LogUpdateCreatesEntry) {
    WALManager wal(testWalFile);
    Condition cond;
    cond.column = "id";
    cond.op    = "=";
    cond.value = "1";
    std::vector<std::pair<std::string, std::string>> assignments = {{"name", "Bob"}};
    wal.logUpdate("users", &cond, assignments);

    auto lines = readWalLines();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0], "UPDATE|users|id~=~1|name~Bob|0");
}

// --- Recovery tests with full engine setup ---

class WALRecoveryTest : public ::testing::Test {
protected:
    const std::string catFile = "test_wal_recovery.cat";
    const std::string dbFile  = "test_wal_recovery.db";
    const std::string walFile = "test_wal_recovery.wal";

    void SetUp() override  { cleanup(); }
    void TearDown() override { cleanup(); }

    void cleanup() {
        std::remove(catFile.c_str());
        std::remove(dbFile.c_str());
        std::remove(walFile.c_str());
    }
};

//Test 8: crash during DELETE — recovery re-applies DELETE, row is gone
TEST_F(WALRecoveryTest, RecoverReplaysUncommittedDelete) {
    // Setup: create table and insert a row via Engine
    {
        Engine engine(128, dbFile, catFile, walFile);
        engine.query("CREATE TABLE wal_del (id INT, name STRING)");
        engine.query("INSERT INTO wal_del VALUES (1, Alice)");
    }
    std::remove(walFile.c_str());

    // Simulate crash: log DELETE without commit
    {
        WALManager wal(walFile);
        Condition cond;
        cond.column = "id";
        cond.op    = "=";
        cond.value = "1";
        wal.logDelete("wal_del", &cond);
    }

    // Recovery: Engine constructor calls recover()
    Engine engine(128, dbFile, catFile, walFile);
    auto results = engine.query("SELECT * FROM wal_del");
    EXPECT_EQ(results.size(), 0);
}

//Test 9: crash during UPDATE — recovery re-applies UPDATE, value is changed
TEST_F(WALRecoveryTest, RecoverReplaysUncommittedUpdate) {
    // Setup: create table and insert a row via Engine
    {
        Engine engine(128, dbFile, catFile, walFile);
        engine.query("CREATE TABLE wal_upd (id INT, name STRING)");
        engine.query("INSERT INTO wal_upd VALUES (1, Alice)");
    }
    std::remove(walFile.c_str());

    // Simulate crash: log UPDATE without commit
    {
        WALManager wal(walFile);
        Condition cond;
        cond.column = "id";
        cond.op    = "=";
        cond.value = "1";
        std::vector<std::pair<std::string, std::string>> assignments = {{"name", "Bob"}};
        wal.logUpdate("wal_upd", &cond, assignments);
    }

    // Recovery
    Engine engine(128, dbFile, catFile, walFile);
    auto results = engine.query("SELECT * FROM wal_upd");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].values[1], "Bob");
}
