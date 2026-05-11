#include <gtest/gtest.h>
#include "../WALManager/WALManager.hpp"
#include "../Engine/Engine.hpp"
#include "../Catalog/Catalog.hpp"
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

//Test 2: commit marks the last WAL entry as committed
TEST_F(WALManagerTest, CommitUpdatesEntry) {
    WALManager wal(testWalFile);
    wal.logInsert("users", "1|Alice|25");
    wal.commit();

    auto lines = readWalLines();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0], "INSERT|users|1|Alice|25|1");
}

//Test 3: multiple inserts log correctly and commit only marks the last entry
TEST_F(WALManagerTest, MultipleInsertsAndCommit) {
    WALManager wal(testWalFile);
    wal.logInsert("users", "1|Alice|25");
    wal.logInsert("users", "2|Bob|30");
    wal.commit(); // Commits the last one

    auto lines = readWalLines();
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], "INSERT|users|1|Alice|25|0");
    EXPECT_EQ(lines[1], "INSERT|users|2|Bob|30|1");
}

//Test 4: recover replays uncommitted WAL entries and inserts the rows into the table
TEST_F(WALManagerTest, RecoverReplaysUncommitted) {
    {
        WALManager wal(testWalFile);
        wal.logInsert("users", "1|Alice|25");
    }

    Catalog catalog;
    std::vector<Columns> cols = {{"id", "INT"}, {"name", "TEXT"}, {"age", "INT"}};
    catalog.createTable("users", cols);

    {
        Table table("users", cols);
        table.dropStorage();
    }

    std::remove("engine.wal");
    std::rename(testWalFile.c_str(), "engine.wal");

    Engine engine(catalog);

    auto results = engine.query("SELECT * FROM users");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].values[0], "1");
    EXPECT_EQ(results[0].values[1], "Alice");
    EXPECT_EQ(results[0].values[2], "25");

    std::remove("engine.wal");
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
    void SetUp() override  { cleanup(); }
    void TearDown() override { cleanup(); }

    void cleanup() {
        std::remove("engine.wal");
        std::remove("catalog.dat");
        std::remove("wal_del.db");
        std::remove("wal_upd.db");
    }
};

//Test 8: crash during DELETE — recovery re-applies DELETE, row is gone
TEST_F(WALRecoveryTest, RecoverReplaysUncommittedDelete) {
    std::vector<Columns> cols = {{"id", "INT"}, {"name", "STRING"}};

    // Setup: insert a row via Engine
    {
        Catalog catalog;
        catalog.createTable("wal_del", cols);
        Engine engine(catalog);
        engine.query("INSERT INTO wal_del VALUES (1, Alice)");
    }
    std::remove("engine.wal");

    // Simulate crash: log DELETE without commit
    {
        WALManager wal("engine.wal");
        Condition cond;
        cond.column = "id";
        cond.op    = "=";
        cond.value = "1";
        wal.logDelete("wal_del", &cond);
    }

    // Recovery: Engine constructor calls recover()
    Catalog catalog;
    Engine engine(catalog);
    auto results = engine.query("SELECT * FROM wal_del");
    EXPECT_EQ(results.size(), 0);
}

//Test 9: crash during UPDATE — recovery re-applies UPDATE, value is changed
TEST_F(WALRecoveryTest, RecoverReplaysUncommittedUpdate) {
    std::vector<Columns> cols = {{"id", "INT"}, {"name", "STRING"}};

    // Setup: insert a row via Engine
    {
        Catalog catalog;
        catalog.createTable("wal_upd", cols);
        Engine engine(catalog);
        engine.query("INSERT INTO wal_upd VALUES (1, Alice)");
    }
    std::remove("engine.wal");

    // Simulate crash: log UPDATE without commit
    {
        WALManager wal("engine.wal");
        Condition cond;
        cond.column = "id";
        cond.op    = "=";
        cond.value = "1";
        std::vector<std::pair<std::string, std::string>> assignments = {{"name", "Bob"}};
        wal.logUpdate("wal_upd", &cond, assignments);
    }

    // Recovery
    Catalog catalog;
    Engine engine(catalog);
    auto results = engine.query("SELECT * FROM wal_upd");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].values[1], "Bob");
}