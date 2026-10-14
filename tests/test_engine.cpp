#include <gtest/gtest.h>
#include "../Engine/Engine.hpp"
#include <cstdio>
#include <filesystem>

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

        //CREATE/DROP DATABASE work under databases/, so clean those up too
        for (const char *ext : {".db", ".cat", ".wal"})
            std::filesystem::remove("databases/eng_dropdb" + std::string(ext));
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

//Test 12: DROP DATABASE deletes the files CREATE DATABASE made
TEST_F(EngineTest, DropDatabaseRemovesFiles) {
    engine->query("CREATE DATABASE eng_dropdb");
    ASSERT_TRUE(std::filesystem::exists("databases/eng_dropdb.db"));
    ASSERT_TRUE(std::filesystem::exists("databases/eng_dropdb.cat"));

    EXPECT_NO_THROW(engine->query("DROP DATABASE eng_dropdb"));

    EXPECT_FALSE(std::filesystem::exists("databases/eng_dropdb.db"));
    EXPECT_FALSE(std::filesystem::exists("databases/eng_dropdb.cat"));
}

//Test 13: DROP DATABASE on a database that does not exist errors instead of reporting ok
TEST_F(EngineTest, DropMissingDatabaseThrows) {
    EXPECT_THROW(engine->query("DROP DATABASE eng_no_such_db"), std::runtime_error);
}

//Test 14: DROP DATABASE refuses the active database, whose files are still open
TEST_F(EngineTest, DropActiveDatabaseThrows) {
    try {
        //The fixture opens test_engine.db, so the active database is "test_engine"
        engine->query("DROP DATABASE test_engine");
        FAIL() << "Expected the active-database guard to reject this";
    } catch (const std::runtime_error &e) {
        //Assert on the message: without the guard this would still throw, but
        //from the existence check, which would pass the test for the wrong reason
        EXPECT_NE(std::string(e.what()).find("currently active"), std::string::npos);
    }
}

//Test 15: the active-database guard ignores case, since Windows filenames do too
TEST_F(EngineTest, DropActiveDatabaseGuardIgnoresCase) {
    try {
        engine->query("DROP DATABASE Test_Engine");
        FAIL() << "Expected the active-database guard to reject a differently-cased name";
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string(e.what()).find("currently active"), std::string::npos);
    }
}

//Test 16: CREATE TABLE on a taken name errors, symmetric with DROP TABLE on a missing one,
//and leaves the existing schema untouched. It used to answer ok and silently keep the old one
TEST_F(EngineTest, CreateExistingTableThrowsAndKeepsSchema) {
    engine->query("CREATE TABLE eng_dup (id INT, name STRING)");

    try {
        engine->query("CREATE TABLE eng_dup (email STRING)");
        FAIL() << "Expected CREATE TABLE on an existing name to throw";
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string(e.what()).find("already exists"), std::string::npos) << e.what();
    }

    auto cols = engine->getCatalog().getColumns("eng_dup");
    ASSERT_EQ(cols.size(), 2u);
    EXPECT_EQ(cols[0].name, "id");
    EXPECT_EQ(cols[1].name, "name");
}

//Test 17: TEXT and BOOLEAN work end to end and are stored as their canonical names
TEST_F(EngineTest, TypeAliasesAreStoredAsCanonicalTypes) {
    engine->query("CREATE TABLE eng_alias (id INT, note TEXT, active BOOLEAN)");

    auto cols = engine->getCatalog().getColumns("eng_alias");
    ASSERT_EQ(cols.size(), 3u);
    EXPECT_EQ(cols[1].type, "STRING");
    EXPECT_EQ(cols[2].type, "BOOL");

    engine->query("INSERT INTO eng_alias VALUES (1, 'hello', true)");
    EXPECT_THROW(engine->query("INSERT INTO eng_alias VALUES (2, 'x', maybe)"), std::runtime_error);

    auto rows = engine->query("SELECT * FROM eng_alias");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].values[1], "hello");
    EXPECT_EQ(rows[0].values[2], "true");
}

//Test 18: unquoted NULL goes into any column type, 'NULL' stays text; the README example
//with NULL in a FLOAT column used to fail with a type error
TEST_F(EngineTest, NullInsertsIntoEveryTypeAndDiffersFromQuotedNull) {
    engine->query("CREATE TABLE eng_null (id INT, name STRING, price FLOAT, active BOOL)");
    EXPECT_NO_THROW(engine->query("INSERT INTO eng_null VALUES (1, NULL, NULL, NULL)"));
    EXPECT_NO_THROW(engine->query("INSERT INTO eng_null VALUES (2, 'NULL', 1.5, true)"));
    EXPECT_NO_THROW(engine->query("INSERT INTO eng_null VALUES (3, 'Out of stock', NULL, false)"));

    auto rows = engine->query("SELECT * FROM eng_null");
    ASSERT_EQ(rows.size(), 3u);
    EXPECT_FALSE(rows[0].values[1].has_value());
    EXPECT_FALSE(rows[0].values[2].has_value());
    EXPECT_FALSE(rows[0].values[3].has_value());
    ASSERT_TRUE(rows[1].values[1].has_value());
    EXPECT_EQ(*rows[1].values[1], "NULL");
    EXPECT_FALSE(rows[2].values[2].has_value());
}

//Test 19: IS NULL finds the real NULLs only, = NULL finds nothing, = 'NULL' finds the text
TEST_F(EngineTest, WhereIsNullVersusEqualsNull) {
    engine->query("CREATE TABLE eng_nullw (id INT, name STRING)");
    engine->query("INSERT INTO eng_nullw VALUES (1, NULL)");
    engine->query("INSERT INTO eng_nullw VALUES (2, 'NULL')");
    engine->query("INSERT INTO eng_nullw VALUES (3, 'Bob')");

    auto isNull = engine->query("SELECT id FROM eng_nullw WHERE name IS NULL");
    ASSERT_EQ(isNull.size(), 1u);
    EXPECT_EQ(isNull[0].values[0], "1");

    EXPECT_EQ(engine->query("SELECT * FROM eng_nullw WHERE name IS NOT NULL").size(), 2u);
    EXPECT_EQ(engine->query("SELECT * FROM eng_nullw WHERE name = NULL").size(), 0u);

    auto text = engine->query("SELECT id FROM eng_nullw WHERE name = 'NULL'");
    ASSERT_EQ(text.size(), 1u);
    EXPECT_EQ(text[0].values[0], "2");

    // NULL != 'Bob' is not true, so row 1 is not included
    EXPECT_EQ(engine->query("SELECT * FROM eng_nullw WHERE name != 'Bob'").size(), 1u);
}

//Test 20: UPDATE ... SET col = NULL stores a real NULL, and DELETE ... WHERE IS NULL
//removes exactly those rows
TEST_F(EngineTest, UpdateToNullAndDeleteWhereIsNull) {
    engine->query("CREATE TABLE eng_nullu (id INT, score INT)");
    engine->query("INSERT INTO eng_nullu VALUES (1, 10)");
    engine->query("INSERT INTO eng_nullu VALUES (2, 20)");

    engine->query("UPDATE eng_nullu SET score = NULL WHERE id = 1");
    auto rows = engine->query("SELECT * FROM eng_nullu WHERE id = 1");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_FALSE(rows[0].values[1].has_value());

    engine->query("DELETE FROM eng_nullu WHERE score IS NULL");
    auto left = engine->query("SELECT * FROM eng_nullu");
    ASSERT_EQ(left.size(), 1u);
    EXPECT_EQ(left[0].values[0], "2");
}

//Test 21: NULL in the indexed first INT column; indexed lookups still work for the rest
TEST_F(EngineTest, NullInFirstIntColumnIsAllowed) {
    engine->query("CREATE TABLE eng_nullpk (id INT, name STRING)");
    EXPECT_NO_THROW(engine->query("INSERT INTO eng_nullpk VALUES (NULL, 'ghost')"));
    engine->query("INSERT INTO eng_nullpk VALUES (7, 'Alice')");

    auto byId = engine->query("SELECT name FROM eng_nullpk WHERE id = 7");
    ASSERT_EQ(byId.size(), 1u);
    EXPECT_EQ(byId[0].values[0], "Alice");

    EXPECT_EQ(engine->query("SELECT * FROM eng_nullpk WHERE id = NULL").size(), 0u);
    auto ghost = engine->query("SELECT name FROM eng_nullpk WHERE id IS NULL");
    ASSERT_EQ(ghost.size(), 1u);
    EXPECT_EQ(ghost[0].values[0], "ghost");
}
