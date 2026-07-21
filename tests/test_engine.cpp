#include <gtest/gtest.h>
#include "../Engine/Engine.hpp"
#include <cstdio>

class EngineTest : public ::testing::Test {
protected:
    const std::string catFile  = "test_engine.cat";
    const std::string dbFile   = "test_engine.db";
    const std::string walFile  = "test_engine.wal";

    Engine *engine = nullptr;

    void SetUp() override {
        cleanup();
        engine = new Engine(128, dbFile, catFile, walFile);
    }

    void TearDown() override {
        delete engine;
        cleanup();
    }

    void cleanup() {
        std::remove(catFile.c_str());
        std::remove(dbFile.c_str());
        std::remove(walFile.c_str());
    }
};

//Test 1: CREATE TABLE via SQL updates the catalog
TEST_F(EngineTest, CreateTableViaSQL) {
    EXPECT_NO_THROW(engine->query("CREATE TABLE eng_users (id INT, name STRING)"));
    EXPECT_TRUE(engine->getCatalog().tableExists("eng_users"));
}

//Test 2: INSERT followed by SELECT * returns all inserted rows
TEST_F(EngineTest, InsertAndSelectAll) {
    engine->query("CREATE TABLE eng_users (id INT, name STRING)");
    engine->query("INSERT INTO eng_users VALUES (1, Alice)");
    engine->query("INSERT INTO eng_users VALUES (2, Bob)");

    auto results = engine->query("SELECT * FROM eng_users");
    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0].values[0], "1");
    EXPECT_EQ(results[0].values[1], "Alice");
    EXPECT_EQ(results[1].values[0], "2");
    EXPECT_EQ(results[1].values[1], "Bob");
}

//Test 3: SELECT with WHERE filters the correct row
TEST_F(EngineTest, SelectWithCondition) {
    engine->query("CREATE TABLE eng_users (id INT, name STRING)");
    engine->query("INSERT INTO eng_users VALUES (1, Alice)");
    engine->query("INSERT INTO eng_users VALUES (2, Bob)");

    auto results = engine->query("SELECT * FROM eng_users WHERE id = 1");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].values[1], "Alice");
}

//Test 4: SELECT on a specific column returns only that column's values
TEST_F(EngineTest, SelectSpecificColumns) {
    engine->query("CREATE TABLE eng_col (id INT, name STRING)");
    engine->query("INSERT INTO eng_col VALUES (1, Alice)");

    auto results = engine->query("SELECT name FROM eng_col");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].values[0], "Alice");
}

//Test 5: DELETE with WHERE removes only the matching row
TEST_F(EngineTest, DeleteWithCondition) {
    engine->query("CREATE TABLE eng_del (id INT, name STRING)");
    engine->query("INSERT INTO eng_del VALUES (1, Alice)");
    engine->query("INSERT INTO eng_del VALUES (2, Bob)");
    engine->query("INSERT INTO eng_del VALUES (3, Charlie)");

    engine->query("DELETE FROM eng_del WHERE id = 2");

    auto results = engine->query("SELECT * FROM eng_del");
    ASSERT_EQ(results.size(), 2);
    EXPECT_EQ(results[0].values[0], "1");
    EXPECT_EQ(results[1].values[0], "3");
}

//Test 6: UPDATE with WHERE modifies only the matching row
TEST_F(EngineTest, UpdateWithCondition) {
    engine->query("CREATE TABLE eng_upd (id INT, name STRING)");
    engine->query("INSERT INTO eng_upd VALUES (1, Alice)");
    engine->query("INSERT INTO eng_upd VALUES (2, Bob)");

    engine->query("UPDATE eng_upd SET name = Johnny WHERE id = 1");

    auto results = engine->query("SELECT * FROM eng_upd WHERE id = 1");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].values[1], "Johnny");
}

//Test 7: DROP TABLE removes the table from the catalog
TEST_F(EngineTest, DropTable) {
    engine->query("CREATE TABLE eng_drop (id INT, name STRING)");
    EXPECT_TRUE(engine->getCatalog().tableExists("eng_drop"));

    engine->query("DROP TABLE eng_drop");
    EXPECT_FALSE(engine->getCatalog().tableExists("eng_drop"));
}

//Test 8: SELECT on a non-existent table throws a runtime error
TEST_F(EngineTest, SelectFromNonExistentTableThrows) {
    EXPECT_THROW(engine->query("SELECT * FROM no_such_table"), std::runtime_error);
}

//Test 9: INSERT with wrong type for an INT column throws a runtime error
TEST_F(EngineTest, InsertTypeMismatchThrows) {
    engine->query("CREATE TABLE eng_users (id INT, name STRING)");
    EXPECT_THROW(engine->query("INSERT INTO eng_users VALUES (abc, Alice)"), std::runtime_error);
}

//Test 10: Multiple sequential inserts are all retrievable via SELECT
TEST_F(EngineTest, MultipleRowsInsertAndSelect) {
    engine->query("CREATE TABLE eng_multi (id INT, age INT)");
    for (int i = 1; i <= 5; i++)
        engine->query("INSERT INTO eng_multi VALUES (" + std::to_string(i) + ", " + std::to_string(20 + i) + ")");

    auto results = engine->query("SELECT * FROM eng_multi");
    ASSERT_EQ(results.size(), 5);
    EXPECT_EQ(results[4].values[0], "5");
    EXPECT_EQ(results[4].values[1], "25");
}

//Test 11b: A string value with an escaped quote round-trips through INSERT/SELECT
TEST_F(EngineTest, EscapedQuoteRoundTrips) {
    engine->query("CREATE TABLE eng_quote (id INT, note STRING)");
    engine->query("INSERT INTO eng_quote VALUES (1, 'don''t')");

    auto results = engine->query("SELECT * FROM eng_quote");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].values[1], "don't");
}

//Test 11c: A parse error surfaces a readable English message, not a numeric code
TEST_F(EngineTest, ParseErrorMessageIsReadable) {
    try {
        engine->query("INSERT INTO eng_bad VALUES ()");
        FAIL() << "Expected a parse error to be thrown";
    } catch (const std::runtime_error &e) {
        std::string msg = e.what();
        //Must be human-readable, not the old "Parse error: 5"
        EXPECT_NE(msg.find("Parse error"), std::string::npos);
        EXPECT_EQ(msg.find_first_of("0123456789"), std::string::npos);
    }
}

//Test 11: Catalog and data persist correctly across Engine restarts
TEST_F(EngineTest, CatalogPersistsAcrossEngineInstances) {
    engine->query("CREATE TABLE eng_orders (id INT, amount INT)");
    engine->query("INSERT INTO eng_orders VALUES (1, 100)");

    delete engine;
    engine = nullptr;

    std::remove(walFile.c_str());
    engine = new Engine(128, dbFile, catFile, walFile);

    EXPECT_TRUE(engine->getCatalog().tableExists("eng_orders"));
    auto results = engine->query("SELECT * FROM eng_orders");
    ASSERT_EQ(results.size(), 1);
    EXPECT_EQ(results[0].values[1], "100");
}
