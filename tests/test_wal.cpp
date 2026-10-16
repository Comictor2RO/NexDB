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
    wal.logInsert("users", {"1", "Alice", "25"});

    auto lines = readWalLines();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0], "INSERT|users|1|Alice|25|0");
}

//Test 2: commit appends COMMIT marker after entry
TEST_F(WALManagerTest, CommitUpdatesEntry) {
    WALManager wal(testWalFile);
    wal.logInsert("users", {"1", "Alice", "25"});
    wal.commit();

    auto lines = readWalLines();
    ASSERT_EQ(lines.size(), 2);
    EXPECT_EQ(lines[0], "INSERT|users|1|Alice|25|0");
    EXPECT_EQ(lines[1], "COMMIT");
}

//Test 3: multiple inserts followed by commit appends COMMIT marker
TEST_F(WALManagerTest, MultipleInsertsAndCommit) {
    WALManager wal(testWalFile);
    wal.logInsert("users", {"1", "Alice", "25"});
    wal.logInsert("users", {"2", "Bob", "30"});
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
        wal.logInsert("users", {"1", "Alice", "25"});
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
    std::vector<std::pair<std::string, Value>> assignments = {{"name", "Bob"}};
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
        std::vector<std::pair<std::string, Value>> assignments = {{"name", "Bob"}};
        wal.logUpdate("wal_upd", &cond, assignments);
    }

    // Recovery
    Engine engine(128, dbFile, catFile, walFile);
    auto results = engine.query("SELECT * FROM wal_upd");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].values[1], "Bob");
}

//Test 10: NULL is written to the WAL as the %N marker, the string "NULL" as plain text
TEST_F(WALManagerTest, LogInsertWritesNullMarker) {
    WALManager wal(testWalFile);
    wal.logInsert("users", {"1", std::nullopt, "NULL"});

    auto lines = readWalLines();
    ASSERT_EQ(lines.size(), 1);
    EXPECT_EQ(lines[0], "INSERT|users|1|%N|NULL|0");
}

//Test 11: recovery of an uncommitted INSERT keeps NULL vs 'NULL' and values that are not
//bare words. The replay used to build VALUES (2,Alice Smith,...) without quotes, so any
//space, comma, quote or empty string made it a parse error and the row was lost
TEST_F(WALRecoveryTest, RecoverInsertPreservesNullsAndQuotedValues) {
    {
        Engine engine(128, dbFile, catFile, walFile);
        engine.query("CREATE TABLE wal_null (id INT, name STRING, score FLOAT)");
    }
    std::remove(walFile.c_str());

    {
        WALManager wal(walFile);
        wal.logInsert("wal_null", {"1", std::nullopt, std::nullopt});
        wal.logInsert("wal_null", {"2", "NULL", "1.5"});
        wal.logInsert("wal_null", {"3", "Alice Smith", std::nullopt});
        wal.logInsert("wal_null", {"4", "a,b", "2"});
        wal.logInsert("wal_null", {"5", "don't", "3"});
        wal.logInsert("wal_null", {"6", "", std::nullopt});
    }

    Engine engine(128, dbFile, catFile, walFile);
    auto rows = engine.query("SELECT * FROM wal_null");
    ASSERT_EQ(rows.size(), 6u);
    EXPECT_FALSE(rows[0].values[1].has_value());
    EXPECT_FALSE(rows[0].values[2].has_value());
    EXPECT_EQ(rows[1].values[1], "NULL");
    EXPECT_EQ(rows[2].values[1], "Alice Smith");
    EXPECT_FALSE(rows[2].values[2].has_value());
    EXPECT_EQ(rows[3].values[1], "a,b");
    EXPECT_EQ(rows[4].values[1], "don't");
    EXPECT_EQ(rows[5].values[1], "");
    EXPECT_FALSE(rows[5].values[2].has_value());
}

//Test 12: recovery of an uncommitted REPLACE (the path DELETE/UPDATE really use) keeps
//NULLs and quoted values the same way
TEST_F(WALRecoveryTest, RecoverReplacePreservesNullsAndQuotedValues) {
    {
        Engine engine(128, dbFile, catFile, walFile);
        engine.query("CREATE TABLE wal_nullr (id INT, name STRING)");
        engine.query("INSERT INTO wal_nullr VALUES (1, 'old')");
    }
    std::remove(walFile.c_str());

    {
        WALManager wal(walFile);
        wal.logReplaceBegin("wal_nullr", nullptr);
        wal.logReplaceRow("wal_nullr", {"1", std::nullopt});
        wal.logReplaceRow("wal_nullr", {"2", "NULL"});
        wal.logReplaceRow("wal_nullr", {"3", "Alice Smith"});
    }

    Engine engine(128, dbFile, catFile, walFile);
    auto rows = engine.query("SELECT * FROM wal_nullr");
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_FALSE(rows[0].values[1].has_value());
    EXPECT_EQ(rows[1].values[1], "NULL");
    EXPECT_EQ(rows[2].values[1], "Alice Smith");
}

//Test 13: recovery of an uncommitted DELETE ... WHERE name IS NULL rebuilds the
//condition without a value, and removes only the NULL rows
TEST_F(WALRecoveryTest, RecoverDeleteWhereIsNull) {
    {
        Engine engine(128, dbFile, catFile, walFile);
        engine.query("CREATE TABLE wal_nulld (id INT, name STRING)");
        engine.query("INSERT INTO wal_nulld VALUES (1, NULL)");
        engine.query("INSERT INTO wal_nulld VALUES (2, 'NULL')");
    }
    std::remove(walFile.c_str());

    {
        WALManager wal(walFile);
        Condition cond;
        cond.column = "name";
        cond.op     = "IS NULL";
        wal.logDelete("wal_nulld", &cond);
    }

    Engine engine(128, dbFile, catFile, walFile);
    auto rows = engine.query("SELECT * FROM wal_nulld");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].values[0], "2");
}

//Test 14: an INSERT with a column list is logged already in schema order, so the
//positional replay puts every value back in the right column
TEST_F(WALRecoveryTest, InsertWithColumnListIsLoggedInSchemaOrder) {
    {
        Engine engine(128, dbFile, catFile, walFile);
        engine.query("CREATE TABLE wal_cols (id INT, name STRING, age INT)");
        engine.query("INSERT INTO wal_cols (name, id) VALUES ('Alice', 1)");
    }

    std::ifstream file(walFile);
    std::string line, content;
    bool found = false;
    while (std::getline(file, line))
    {
        content += line + "\n";
        if (line == "INSERT|wal_cols|1|Alice|%N|0")
            found = true;
    }
    EXPECT_TRUE(found) << "reordered INSERT not found in the WAL:\n" << content;
}

//Test 15: a logged statement that fails on replay is skipped with a message instead of
//making the Engine constructor throw
TEST_F(WALRecoveryTest, FailingReplayDoesNotAbortRecovery) {
    {
        Engine engine(128, dbFile, catFile, walFile);
        engine.query("CREATE TABLE wal_bad (id INT, name STRING)");
        engine.query("INSERT INTO wal_bad VALUES (1, 'Alice')");
    }
    std::remove(walFile.c_str());

    {
        WALManager wal(walFile);
        Condition cond;
        cond.column = "nosuch";
        cond.op     = "=";
        cond.value  = "1";
        wal.logDelete("wal_bad", &cond);
    }

    std::unique_ptr<Engine> engine;
    ASSERT_NO_THROW(engine = std::make_unique<Engine>(128, dbFile, catFile, walFile));
    EXPECT_EQ(engine->query("SELECT * FROM wal_bad").size(), 1u);
}

//Test 16: the very first statement after the Engine starts reaches the WAL. readLog used
//to leave the stream in a failed state after reading to EOF, so that write was dropped
TEST_F(WALRecoveryTest, FirstStatementAfterStartupIsLogged) {
    {
        Engine setup(128, dbFile, catFile, walFile);
        setup.query("CREATE TABLE wal_first (id INT)");
    }
    {
        Engine engine(128, dbFile, catFile, walFile);
        engine.query("INSERT INTO wal_first VALUES (1)");
    }

    std::ifstream file(walFile);
    std::string line;
    bool found = false;
    while (std::getline(file, line))
        if (line == "INSERT|wal_first|1|0")
            found = true;
    EXPECT_TRUE(found);
}

//Test 17: a duplicate-key INSERT is rejected before it is logged. Logging first left an
//uncommitted line that recovery replayed, bringing the rejected row back after a restart
TEST_F(WALRecoveryTest, RejectedInsertIsNotLogged) {
    {
        Engine engine(128, dbFile, catFile, walFile);
        engine.query("CREATE TABLE wal_dup (id INT, name STRING)");
        engine.query("INSERT INTO wal_dup VALUES (1, 'Alice')");
        EXPECT_THROW(engine.query("INSERT INTO wal_dup VALUES (1, 'Bob')"), std::runtime_error);
    }

    {
        std::ifstream file(walFile);
        std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        EXPECT_EQ(content.find("Bob"), std::string::npos) << content;
    }

    testing::internal::CaptureStderr();
    Engine engine(128, dbFile, catFile, walFile);
    std::string err = testing::internal::GetCapturedStderr();
    EXPECT_EQ(err.find("WAL replay error"), std::string::npos) << err;

    auto rows = engine.query("SELECT * FROM wal_dup");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].values[1], "Alice");
}
