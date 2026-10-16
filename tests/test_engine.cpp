#include <gtest/gtest.h>
#include "../Engine/Engine.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>

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
        for (const char *name : {"eng_dropdb", "eng_dupdb"})
            for (const char *ext : {".db", ".cat", ".wal"})
                std::filesystem::remove("databases/" + std::string(name) + ext);
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

//Test 22: negative numbers keep their sign end to end, through the index and comparisons
TEST_F(EngineTest, NegativeNumbersKeepTheirSign) {
    engine->query("CREATE TABLE eng_neg (id INT, temp FLOAT)");
    engine->query("INSERT INTO eng_neg VALUES (-1, -2.5)");
    engine->query("INSERT INTO eng_neg VALUES (+2, 3.5)");

    auto rows = engine->query("SELECT * FROM eng_neg");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].values[0], "-1");
    EXPECT_EQ(rows[0].values[1], "-2.5");
    EXPECT_EQ(rows[1].values[0], "2");

    // Index path on the first INT column
    auto byId = engine->query("SELECT temp FROM eng_neg WHERE id = -1");
    ASSERT_EQ(byId.size(), 1u);
    EXPECT_EQ(byId[0].values[0], "-2.5");

    // Used to run as temp > 0.5 and return the wrong row
    auto below = engine->query("SELECT id FROM eng_neg WHERE temp < -0.5");
    ASSERT_EQ(below.size(), 1u);
    EXPECT_EQ(below[0].values[0], "-1");
}

//Test 23: the INSERT column list places values by name; columns left out get NULL. It used
//to be ignored, so (name, id) VALUES ('Alice', 1) put 'Alice' into id
TEST_F(EngineTest, InsertColumnListIsAppliedBySchema) {
    engine->query("CREATE TABLE eng_cols (id INT, name STRING, age INT)");
    EXPECT_NO_THROW(engine->query("INSERT INTO eng_cols (name, id) VALUES ('Alice', 1)"));

    auto rows = engine->query("SELECT * FROM eng_cols");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].values[0], "1");
    EXPECT_EQ(rows[0].values[1], "Alice");
    EXPECT_FALSE(rows[0].values[2].has_value());

    // The indexed lookup sees the reordered key
    EXPECT_EQ(engine->query("SELECT * FROM eng_cols WHERE id = 1").size(), 1u);
}

//Test 24: a bad INSERT column list is an error and stores nothing
TEST_F(EngineTest, InsertColumnListErrors) {
    engine->query("CREATE TABLE eng_colerr (id INT, name STRING)");

    try {
        engine->query("INSERT INTO eng_colerr (id, nme) VALUES (1, 'x')");
        FAIL() << "Expected an unknown column to throw";
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string(e.what()).find("Column nme does not exist"), std::string::npos) << e.what();
    }

    try {
        engine->query("INSERT INTO eng_colerr (id, id) VALUES (1, 2)");
        FAIL() << "Expected a repeated column to throw";
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string(e.what()).find("specified more than once"), std::string::npos) << e.what();
    }

    // Types are checked after reordering: 'x' lands in the INT column
    EXPECT_THROW(engine->query("INSERT INTO eng_colerr (name, id) VALUES (1, 'x')"), std::runtime_error);

    EXPECT_EQ(engine->query("SELECT * FROM eng_colerr").size(), 0u);
}

//Test 25: UPDATE with an unknown column, a value of the wrong type, or on a missing table
//is an error; it used to answer ok and change nothing
TEST_F(EngineTest, UpdateErrorsAreReported) {
    engine->query("CREATE TABLE eng_upderr (id INT, age INT)");
    engine->query("INSERT INTO eng_upderr VALUES (1, 30)");

    try {
        engine->query("UPDATE eng_upderr SET agee = 31");
        FAIL() << "Expected an unknown column to throw";
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string(e.what()).find("Column agee does not exist"), std::string::npos) << e.what();
    }

    try {
        engine->query("UPDATE eng_upderr SET age = 'old'");
        FAIL() << "Expected a type mismatch to throw";
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string(e.what()).find("Type validation failed"), std::string::npos) << e.what();
    }

    EXPECT_THROW(engine->query("UPDATE eng_nosuchtable SET age = 1"), std::runtime_error);

    // No matching row is not an error, just zero rows affected
    EXPECT_NO_THROW(engine->query("UPDATE eng_upderr SET age = 5 WHERE id = 999"));
    EXPECT_EQ(engine->getLastAffected(), std::optional<size_t>(0));

    auto rows = engine->query("SELECT age FROM eng_upderr");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].values[0], "30");
}

//Test 26: a WHERE on a column that does not exist is an error for SELECT, UPDATE and
//DELETE, instead of quietly matching nothing
TEST_F(EngineTest, UnknownWhereColumnIsAnError) {
    engine->query("CREATE TABLE eng_where (id INT, name STRING)");
    engine->query("INSERT INTO eng_where VALUES (1, 'Alice')");

    for (const char *sql : {"SELECT * FROM eng_where WHERE nme = 'Alice'",
                            "UPDATE eng_where SET name = 'Bob' WHERE nme = 'Alice'",
                            "DELETE FROM eng_where WHERE nme = 'Alice'",
                            "SELECT * FROM eng_where WHERE nme IS NULL"}) {
        try {
            engine->query(sql);
            FAIL() << "Expected an error for: " << sql;
        } catch (const std::runtime_error &e) {
            EXPECT_NE(std::string(e.what()).find("Column nme does not exist"), std::string::npos) << sql;
        }
    }

    auto rows = engine->query("SELECT * FROM eng_where");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].values[1], "Alice");
}

//Test 27: INSERT, UPDATE and DELETE report how many rows they touched; everything else
//reports nothing
TEST_F(EngineTest, AffectedRowCounts) {
    engine->query("CREATE TABLE eng_aff (id INT, grp STRING)");
    EXPECT_FALSE(engine->getLastAffected().has_value());

    engine->query("INSERT INTO eng_aff VALUES (1, 'a')");
    EXPECT_EQ(engine->getLastAffected(), std::optional<size_t>(1));
    engine->query("INSERT INTO eng_aff VALUES (2, 'a')");
    engine->query("INSERT INTO eng_aff VALUES (3, 'b')");

    engine->query("UPDATE eng_aff SET grp = 'c' WHERE grp = 'a'");
    EXPECT_EQ(engine->getLastAffected(), std::optional<size_t>(2));

    engine->query("SELECT * FROM eng_aff");
    EXPECT_FALSE(engine->getLastAffected().has_value());

    engine->query("DELETE FROM eng_aff WHERE id = 999");
    EXPECT_EQ(engine->getLastAffected(), std::optional<size_t>(0));

    engine->query("DELETE FROM eng_aff");
    EXPECT_EQ(engine->getLastAffected(), std::optional<size_t>(3));
}

//Test 28: the first INT column is a unique key. A second INSERT with the same id is
//rejected and the table keeps one row; the key compares as an int, so 007 collides with 7
TEST_F(EngineTest, DuplicateKeyInsertIsRejected) {
    engine->query("CREATE TABLE eng_dup (id INT, name STRING)");
    engine->query("INSERT INTO eng_dup VALUES (1, 'Alice')");
    engine->query("INSERT INTO eng_dup VALUES (7, 'Seven')");

    for (const char *sql : {"INSERT INTO eng_dup VALUES (1, 'Bob')",
                            "INSERT INTO eng_dup VALUES (007, 'Bond')",
                            "INSERT INTO eng_dup (name, id) VALUES ('Carol', 1)"}) {
        try {
            engine->query(sql);
            FAIL() << "Expected a duplicate key error for: " << sql;
        } catch (const std::runtime_error &e) {
            EXPECT_NE(std::string(e.what()).find("Insert failed for table eng_dup: Duplicate key"),
                      std::string::npos) << e.what();
        }
    }

    auto rows = engine->query("SELECT * FROM eng_dup");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].values[1], "Alice");
    EXPECT_EQ(rows[1].values[1], "Seven");
}

//Test 29: NULL keys are not part of uniqueness, like SQL UNIQUE
TEST_F(EngineTest, ManyNullKeysAreAllowed) {
    engine->query("CREATE TABLE eng_dupnull (id INT, name STRING)");
    EXPECT_NO_THROW(engine->query("INSERT INTO eng_dupnull VALUES (NULL, 'a')"));
    EXPECT_NO_THROW(engine->query("INSERT INTO eng_dupnull VALUES (NULL, 'b')"));
    EXPECT_NO_THROW(engine->query("INSERT INTO eng_dupnull (name) VALUES ('c')"));
    EXPECT_EQ(engine->query("SELECT * FROM eng_dupnull").size(), 3u);
}

//Test 30: an UPDATE that would give two rows the same key is rejected before anything
//is written, whether the key already exists or the SET hits several rows at once
TEST_F(EngineTest, UpdateToExistingKeyIsRejected) {
    engine->query("CREATE TABLE eng_dupupd (id INT, name STRING)");
    engine->query("INSERT INTO eng_dupupd VALUES (1, 'Alice')");
    engine->query("INSERT INTO eng_dupupd VALUES (2, 'Bob')");

    for (const char *sql : {"UPDATE eng_dupupd SET id = 2 WHERE id = 1",
                            "UPDATE eng_dupupd SET id = 5",
                            "UPDATE eng_dupupd SET name = 'x', id = 02 WHERE name = 'Alice'"}) {
        try {
            engine->query(sql);
            FAIL() << "Expected a duplicate key error for: " << sql;
        } catch (const std::runtime_error &e) {
            EXPECT_NE(std::string(e.what()).find("Update failed for table eng_dupupd: Duplicate key"),
                      std::string::npos) << e.what();
        }
    }

    auto rows = engine->query("SELECT * FROM eng_dupupd");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_EQ(rows[0].values[0], "1");
    EXPECT_EQ(rows[0].values[1], "Alice");
    EXPECT_EQ(rows[1].values[0], "2");
    EXPECT_EQ(rows[1].values[1], "Bob");
}

//Test 31: UPDATEs that cannot create a duplicate still go through: setting a row's key
//to its own value, moving it to a free key, and not touching the key at all
TEST_F(EngineTest, UpdateWithoutKeyConflictSucceeds) {
    engine->query("CREATE TABLE eng_dupok (id INT, name STRING)");
    engine->query("INSERT INTO eng_dupok VALUES (1, 'Alice')");
    engine->query("INSERT INTO eng_dupok VALUES (2, 'Bob')");

    EXPECT_NO_THROW(engine->query("UPDATE eng_dupok SET id = 1 WHERE id = 1"));
    EXPECT_EQ(engine->getLastAffected(), std::optional<size_t>(1));
    EXPECT_NO_THROW(engine->query("UPDATE eng_dupok SET id = 3 WHERE id = 2"));
    EXPECT_NO_THROW(engine->query("UPDATE eng_dupok SET name = 'x'"));
    EXPECT_EQ(engine->getLastAffected(), std::optional<size_t>(2));

    auto moved = engine->query("SELECT * FROM eng_dupok WHERE id = 3");
    ASSERT_EQ(moved.size(), 1u);
    EXPECT_EQ(moved[0].values[1], "x");
    EXPECT_EQ(engine->query("SELECT * FROM eng_dupok WHERE id = 2").size(), 0u);
}

//Test 32: CREATE TABLE with the same column twice is an error and creates nothing; only
//the first copy used to be reachable. Names are case-sensitive, so a and A may coexist
TEST_F(EngineTest, CreateTableWithDuplicateColumnsIsRejected) {
    try {
        engine->query("CREATE TABLE eng_dupcol (a INT, b STRING, a FLOAT)");
        FAIL() << "Expected a repeated column to throw";
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string(e.what()).find("Column a specified more than once"), std::string::npos) << e.what();
    }
    EXPECT_FALSE(engine->getCatalog().tableExists("eng_dupcol"));

    EXPECT_NO_THROW(engine->query("CREATE TABLE eng_dupcase (a INT, A STRING)"));
}

//Test 33: UPDATE ... SET a = 1, a = 2 is an error and changes nothing; the last
//assignment used to win silently
TEST_F(EngineTest, UpdateWithRepeatedColumnIsRejected) {
    engine->query("CREATE TABLE eng_updrep (id INT, age INT)");
    engine->query("INSERT INTO eng_updrep VALUES (1, 30)");

    try {
        engine->query("UPDATE eng_updrep SET age = 1, age = 2");
        FAIL() << "Expected a repeated column to throw";
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string(e.what()).find("Column age specified more than once"), std::string::npos) << e.what();
    }

    auto rows = engine->query("SELECT age FROM eng_updrep");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].values[0], "30");
}

//Test 34: = and != compare by column type. They used to compare the text, so 9.50 did not
//equal a stored 9.5, TRUE did not equal true, and 007 did not equal 7 outside the index
TEST_F(EngineTest, EqualityComparesByColumnType) {
    engine->query("CREATE TABLE eng_eqtype (id INT, score FLOAT, active BOOL, qty INT, name STRING)");
    engine->query("INSERT INTO eng_eqtype VALUES (1, 9.5, true, 7, 'a')");
    engine->query("INSERT INTO eng_eqtype VALUES (2, 3.25, FALSE, 8, 'b')");

    auto idsWhere = [&](const std::string &where) {
        std::vector<std::string> ids;
        for (const auto &row : engine->query("SELECT id FROM eng_eqtype WHERE " + where))
            ids.push_back(*row.values[0]);
        return ids;
    };
    const std::vector<std::string> first = {"1"}, second = {"2"};

    EXPECT_EQ(idsWhere("score = 9.50"), first);
    EXPECT_EQ(idsWhere("score != 9.50"), second);

    for (const char *where : {"active = TRUE", "active = true", "active = 1", "active = 'TRUE'"})
        EXPECT_EQ(idsWhere(where), first) << where;
    EXPECT_EQ(idsWhere("active != TRUE"), second);
    EXPECT_EQ(idsWhere("active = 0"), second);

    EXPECT_EQ(idsWhere("qty = 007"), first);
    EXPECT_EQ(idsWhere("qty != 07"), second);

    // Strings still compare as text
    EXPECT_EQ(idsWhere("name = 'a'"), first);

    // UPDATE and DELETE match through previewUpdate / previewDelete, not the index
    engine->query("UPDATE eng_eqtype SET name = 'z' WHERE score = 9.50");
    EXPECT_EQ(engine->getLastAffected(), std::optional<size_t>(1));
    EXPECT_EQ(idsWhere("name = 'z'"), first);

    // The key column too: SELECT used to find 02 through the index while UPDATE missed it
    engine->query("UPDATE eng_eqtype SET name = 'y' WHERE id = 02");
    EXPECT_EQ(engine->getLastAffected(), std::optional<size_t>(1));
    EXPECT_EQ(idsWhere("name = 'y'"), second);

    engine->query("DELETE FROM eng_eqtype WHERE score = 9.50");
    EXPECT_EQ(engine->getLastAffected(), std::optional<size_t>(1));
    auto left = engine->query("SELECT id FROM eng_eqtype");
    ASSERT_EQ(left.size(), 1u);
    EXPECT_EQ(left[0].values[0], "2");
}

//Test 35: WHERE on the key reads the literal as a number. stoi used to turn 5.5 into 5 and
//return that row, and to reach the client as "stoi" for a value outside int
TEST_F(EngineTest, KeyLookupUsesNumericValue) {
    engine->query("CREATE TABLE eng_keynum (id INT, name STRING)");
    engine->query("INSERT INTO eng_keynum VALUES (5, 'five')");
    engine->query("INSERT INTO eng_keynum VALUES (6, 'six')");

    EXPECT_EQ(engine->query("SELECT * FROM eng_keynum WHERE id = 5.5").size(), 0u);

    auto exact = engine->query("SELECT name FROM eng_keynum WHERE id = 5.0");
    ASSERT_EQ(exact.size(), 1u);
    EXPECT_EQ(exact[0].values[0], "five");

    std::vector<Row> huge;
    EXPECT_NO_THROW(huge = engine->query("SELECT * FROM eng_keynum WHERE id = 99999999999"));
    EXPECT_EQ(huge.size(), 0u);

    engine->query("DELETE FROM eng_keynum WHERE id = 5.5");
    EXPECT_EQ(engine->getLastAffected(), std::optional<size_t>(0));
    EXPECT_EQ(engine->query("SELECT * FROM eng_keynum").size(), 2u);
}

//Test 36: a WHERE literal the column can never hold is an error for SELECT, UPDATE and
//DELETE, instead of quietly matching nothing. Any number is fine for INT and FLOAT
TEST_F(EngineTest, WrongTypeLiteralInWhereIsAnError) {
    engine->query("CREATE TABLE eng_wtype (id INT, age INT, active BOOL, name STRING)");
    engine->query("INSERT INTO eng_wtype VALUES (1, 30, true, 'abc')");

    for (const char *sql : {"SELECT * FROM eng_wtype WHERE age = 'abc'",
                            "SELECT * FROM eng_wtype WHERE age > 'abc'",
                            "SELECT * FROM eng_wtype WHERE active = 'da'",
                            "SELECT * FROM eng_wtype WHERE id = 'abc'",
                            "UPDATE eng_wtype SET name = 'x' WHERE age = 'abc'",
                            "UPDATE eng_wtype SET name = 'x' WHERE active = 'da'",
                            "DELETE FROM eng_wtype WHERE age > 'abc'",
                            "DELETE FROM eng_wtype WHERE active = 'da'"}) {
        try {
            engine->query(sql);
            FAIL() << "Expected a type error for: " << sql;
        } catch (const std::runtime_error &e) {
            EXPECT_NE(std::string(e.what()).find("Type validation failed (invalid value for column "),
                      std::string::npos) << sql << ": " << e.what();
        }
    }

    try {
        engine->query("SELECT * FROM eng_wtype WHERE age = 'abc'");
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string(e.what()).find("column age of type INT"), std::string::npos) << e.what();
    }

    EXPECT_EQ(engine->query("SELECT * FROM eng_wtype WHERE name = 'abc'").size(), 1u);
    EXPECT_EQ(engine->query("SELECT * FROM eng_wtype WHERE age > 2.5").size(), 1u);
    EXPECT_EQ(engine->query("SELECT * FROM eng_wtype WHERE id = '1'").size(), 1u);
    EXPECT_EQ(engine->query("SELECT * FROM eng_wtype WHERE age = NULL").size(), 0u);
    EXPECT_EQ(engine->query("SELECT * FROM eng_wtype WHERE active IS NOT NULL").size(), 1u);

    auto rows = engine->query("SELECT name FROM eng_wtype");
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].values[0], "abc");
}

//Test 37: CREATE DATABASE on a name that exists is an error and leaves its files alone;
//it used to answer ok, which looks like a fresh database was made
TEST_F(EngineTest, CreateExistingDatabaseThrows) {
    engine->query("CREATE DATABASE eng_dupdb");
    std::ofstream("databases/eng_dupdb.cat") << "kept";

    try {
        engine->query("CREATE DATABASE eng_dupdb");
        FAIL() << "Expected CREATE DATABASE on an existing name to throw";
    } catch (const std::runtime_error &e) {
        EXPECT_NE(std::string(e.what()).find("Database 'eng_dupdb' already exists"), std::string::npos) << e.what();
    }
    EXPECT_GT(std::filesystem::file_size("databases/eng_dupdb.cat"), 0u);

    EXPECT_NO_THROW(engine->query("DROP DATABASE eng_dupdb"));
    EXPECT_FALSE(std::filesystem::exists("databases/eng_dupdb.db"));
}
