#include <gtest/gtest.h>
#include "../Table/Table.hpp"
#include "../Storage/StorageFile/StorageFile.hpp"
#include <cstdio>

class TableTest : public ::testing::Test {
protected:
    std::string dbFile = "test_table.db";
    StorageFile *storage = nullptr;
    int nextTableId = 1;

    void SetUp() override {
        std::remove(dbFile.c_str());
        storage = new StorageFile(dbFile);
    }

    void TearDown() override {
        delete storage;
        storage = nullptr;
        std::remove(dbFile.c_str());
    }

    Table makeTable(const std::string &name, const std::vector<Columns> &schema) {
        return Table(name, schema, *storage, nextTableId++);
    }
};

//Test 1: Create Table with Schema
TEST_F(TableTest, CreateTableWithValidSchema) {
    std::vector<Columns> schema = {
        {"id","INT"},
        {"name", "STRING"}
    };

    Table table = makeTable("test_users", schema);
    SUCCEED();
}

//Test 2: Insert Valid Row
TEST_F(TableTest, InsertValidRow) {
    std::vector<Columns> schema = {
        {"id", "INT"},
        {"name", "STRING"}
    };

    Table table = makeTable("test_insert", schema);

    Row row;
    row.values = {"1", "John"};
    auto result = table.insertRow(row);

    EXPECT_TRUE(result.has_value());
}

//Test 3: Insert Row with Schema Mismatch
TEST_F(TableTest, InsertSchemaMismatch) {
    std::vector<Columns> schema = {
        {"id", "INT"},
        {"name", "STRING"}
    };
    Table table = makeTable("test_mismatch", schema);

    Row row;
    row.values = {"1"};
    auto result = table.insertRow(row);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), TableError::SCHEME_MISMATCH);
}

//Test 4: Select All Rows
TEST_F(TableTest, SelectAllRows) {
    std::vector<Columns> schema = {
        {"id", "INT"},
        {"name", "STRING"}
    };
    Table table = makeTable("test_select", schema);

    table.insertRow({{"1", "John"}});
    table.insertRow({{"2", "Jane"}});

    auto result = table.selectRow(nullptr);
    EXPECT_EQ(result.size(), 2);
}

//Test 5: Select with CONDITION
TEST_F(TableTest, SelectWithCondition) {
    std::vector<Columns> schema = {
        {"id", "INT"},
        {"name", "STRING"}
    };
    Table table = makeTable("test_condition", schema);

    table.insertRow({{"1", "John"}});
    table.insertRow({{"2", "Jane"}});

    Condition cond = {"id", "1", "="};

    auto result = table.selectRow(&cond);
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].values[1], "John");
}

// Test 6: Insert with invalid type (non-INT for INT column)
TEST_F(TableTest, InsertInvalidType) {
    std::vector<Columns> schema = {
        {"id", "INT"},
        {"name", "STRING"}
    };
    Table table = makeTable("test_invalid_type", schema);

    Row row;
    row.values = {"abc", "John"}; // Invalid INT
    auto result = table.insertRow(row);

    EXPECT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), TableError::TYPE_VALIDATION_FAILED);
}

// Test 7: Delete with condition
TEST_F(TableTest, DeleteWithCondition) {
    std::vector<Columns> schema = {
        {"id", "INT"},
        {"name", "STRING"}
    };
    Table table = makeTable("test_delete", schema);

    table.insertRow({{"1", "John"}});
    table.insertRow({{"2", "Jane"}});
    table.insertRow({{"3", "Bob"}});

    Condition cond = {"id", "2", "="};
    table.deleteRow(&cond);

    auto result = table.selectRow(nullptr);
    EXPECT_EQ(result.size(), 2);
    EXPECT_EQ(result[0].values[0], "1");
    EXPECT_EQ(result[1].values[0], "3");
}

// Test 8: Delete all rows (no condition)
TEST_F(TableTest, DeleteAllRows) {
    std::vector<Columns> schema = {
        {"id", "INT"},
        {"name", "STRING"}
    };
    Table table = makeTable("test_delete_all", schema);

    table.insertRow({{"1", "John"}});
    table.insertRow({{"2", "Jane"}});

    table.deleteRow(nullptr);

    auto result = table.selectRow(nullptr);
    EXPECT_EQ(result.size(), 0);
}

// Test 9: Update with condition
TEST_F(TableTest, UpdateWithCondition) {
    std::vector<Columns> schema = {
        {"id", "INT"},
        {"name", "STRING"}
    };
    Table table = makeTable("test_update", schema);

    table.insertRow({{"1", "John"}});
    table.insertRow({{"2", "Jane"}});

    Condition cond = {"id", "1", "="};
    std::vector<std::pair<std::string, Value>> assignments = {{"name", "Johnny"}};
    table.updateRow(&cond, assignments);

    auto result = table.selectRow(&cond);
    EXPECT_EQ(result.size(), 1);
    EXPECT_EQ(result[0].values[1], "Johnny");
}

// Test 10: Select with different operators (>, <, >=, <=, !=)
TEST_F(TableTest, SelectWithDifferentOperators) {
    std::vector<Columns> schema = {
        {"id", "INT"},
        {"age", "INT"}
    };
    Table table = makeTable("test_operators", schema);

    table.insertRow({{"1", "18"}});
    table.insertRow({{"2", "25"}});
    table.insertRow({{"3", "30"}});

    // Test >
    Condition cond1 = {"age", "20", ">"};
    auto result1 = table.selectRow(&cond1);
    EXPECT_EQ(result1.size(), 2);

    // Test <
    Condition cond2 = {"age", "26", "<"};
    auto result2 = table.selectRow(&cond2);
    EXPECT_EQ(result2.size(), 2);

    // Test !=
    Condition cond3 = {"age", "25", "!="};
    auto result3 = table.selectRow(&cond3);
    EXPECT_EQ(result3.size(), 2);
}

// Test 11: NULL passes type validation for every column type and round-trips through
// storage as NULL, distinct from the string "NULL"
TEST_F(TableTest, NullIsValidForEveryTypeAndRoundTrips) {
    std::vector<Columns> schema = {
        {"id", "INT"}, {"name", "STRING"}, {"price", "FLOAT"}, {"active", "BOOL"}
    };
    Table table = makeTable("test_null_types", schema);

    ASSERT_TRUE(table.insertRow({{"1", std::nullopt, std::nullopt, std::nullopt}}).has_value());
    ASSERT_TRUE(table.insertRow({{"2", "NULL", "1.5", "true"}}).has_value());
    // The text NULL is still not a valid INT
    EXPECT_FALSE(table.insertRow({{"NULL", "x", "1", "true"}}).has_value());

    auto rows = table.selectRow(nullptr);
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_FALSE(rows[0].values[1].has_value());
    EXPECT_FALSE(rows[0].values[2].has_value());
    EXPECT_FALSE(rows[0].values[3].has_value());
    ASSERT_TRUE(rows[1].values[1].has_value());
    EXPECT_EQ(*rows[1].values[1], "NULL");
}

// Test 12: IS NULL / IS NOT NULL select by nullness; =, != and < never match a NULL
TEST_F(TableTest, NullComparisonsFollowSqlRules) {
    std::vector<Columns> schema = {{"id", "INT"}, {"age", "INT"}};
    Table table = makeTable("test_null_where", schema);

    table.insertRow({{"1", "18"}});
    table.insertRow({{"2", std::nullopt}});
    table.insertRow({{"3", "30"}});

    Condition isNull = {"age", std::nullopt, "IS NULL"};
    auto r1 = table.selectRow(&isNull);
    ASSERT_EQ(r1.size(), 1u);
    EXPECT_EQ(r1[0].values[0], "2");

    Condition isNotNull = {"age", std::nullopt, "IS NOT NULL"};
    EXPECT_EQ(table.selectRow(&isNotNull).size(), 2u);

    // NULL != 18 is not true, so only row 3 matches
    Condition notEqual = {"age", "18", "!="};
    auto r3 = table.selectRow(&notEqual);
    ASSERT_EQ(r3.size(), 1u);
    EXPECT_EQ(r3[0].values[0], "3");

    Condition less = {"age", "100", "<"};
    EXPECT_EQ(table.selectRow(&less).size(), 2u);

    Condition equalsNull = {"age", std::nullopt, "="};
    EXPECT_EQ(table.selectRow(&equalsNull).size(), 0u);
}

// Test 13: a NULL in the indexed first INT column gets no index entry, so it neither
// breaks the insert nor shows up for an indexed lookup
TEST_F(TableTest, NullInIndexedColumnIsSkippedByIndex) {
    std::vector<Columns> schema = {{"id", "INT"}, {"name", "STRING"}};
    Table table = makeTable("test_null_index", schema);

    ASSERT_TRUE(table.insertRow({{std::nullopt, "ghost"}}).has_value());
    ASSERT_TRUE(table.insertRow({{"1", "Alice"}}).has_value());

    Condition byId = {"id", "1", "="};
    auto r1 = table.selectRow(&byId);
    ASSERT_EQ(r1.size(), 1u);
    EXPECT_EQ(r1[0].values[1], "Alice");

    Condition idEqualsNull = {"id", std::nullopt, "="};
    EXPECT_EQ(table.selectRow(&idEqualsNull).size(), 0u);

    Condition idIsNull = {"id", std::nullopt, "IS NULL"};
    auto r3 = table.selectRow(&idIsNull);
    ASSERT_EQ(r3.size(), 1u);
    EXPECT_EQ(r3[0].values[1], "ghost");
}

// Test 14: an empty string in the last column survives storage. Rows are '|'-joined,
// and the old split dropped a trailing empty field, so the row came back one column short
TEST_F(TableTest, EmptyStringInLastColumnRoundTrips) {
    std::vector<Columns> schema = {{"id", "INT"}, {"note", "STRING"}};
    Table table = makeTable("test_empty_last", schema);

    ASSERT_TRUE(table.insertRow({{"1", ""}}).has_value());

    auto rows = table.selectRow(nullptr);
    ASSERT_EQ(rows.size(), 1u);
    ASSERT_EQ(rows[0].values.size(), 2u);
    EXPECT_EQ(rows[0].values[1], "");
}

// Test 15: insertRow rejects a key that is already indexed and writes nothing, while the
// rewrite path (enforceUnique = false) keeps duplicates so clear + reinsert loses no row
TEST_F(TableTest, DuplicateKeyIsRejectedUnlessUniquenessIsOff) {
    std::vector<Columns> schema = {{"id", "INT"}, {"name", "STRING"}};
    Table table = makeTable("test_dup", schema);

    ASSERT_TRUE(table.insertRow({{"1", "Alice"}}).has_value());

    auto dup = table.insertRow({{"1", "Bob"}});
    ASSERT_FALSE(dup.has_value());
    EXPECT_EQ(dup.error(), TableError::DUPLICATE_KEY);
    EXPECT_EQ(table.checkRow({{"01", "Bob"}}).error(), TableError::DUPLICATE_KEY);
    EXPECT_TRUE(table.checkRow({{"2", "Bob"}}).has_value());
    EXPECT_EQ(table.selectRow(nullptr).size(), 1u);

    EXPECT_TRUE(table.insertRow({{"1", "Bob"}}, false).has_value());
    EXPECT_EQ(table.selectRow(nullptr).size(), 2u);

    // deleteRow rewrites the rows it keeps; both legacy duplicates must survive it
    ASSERT_TRUE(table.insertRow({{"2", "Carol"}}).has_value());
    Condition byId = {"id", "2", "="};
    EXPECT_EQ(table.deleteRow(&byId), 1u);
    EXPECT_EQ(table.selectRow(nullptr).size(), 2u);
}

// Test 16: a key that passes the digit check but overflows int is rejected before the
// page write. It used to be stored anyway, while the caller got INDEX_INSERTION_FAILED
TEST_F(TableTest, OverflowingKeyIsNotStored) {
    std::vector<Columns> schema = {{"id", "INT"}, {"name", "STRING"}};
    Table table = makeTable("test_overflow", schema);

    auto res = table.insertRow({{"99999999999", "Big"}});
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), TableError::INDEX_INSERTION_FAILED);
    EXPECT_EQ(table.selectRow(nullptr).size(), 0u);
}
