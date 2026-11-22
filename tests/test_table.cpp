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
// rewrite path (validate = false) keeps duplicates so clear + reinsert loses no row
TEST_F(TableTest, DuplicateKeyIsRejectedUnlessUniquenessIsOff) {
    std::vector<Columns> schema = {{"id", "INT", true}, {"name", "STRING"}};
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
    std::vector<Columns> schema = {{"id", "INT", true}, {"name", "STRING"}};
    Table table = makeTable("test_overflow", schema);

    auto res = table.insertRow({{"99999999999", "Big"}});
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), TableError::INDEX_INSERTION_FAILED);
    EXPECT_EQ(table.selectRow(nullptr).size(), 0u);
}

// Test 17: a value the type check now rejects survives the rewrite. FLOAT used to accept '.',
// so a stored '.' would fail the stricter check and vanish on the first DELETE or UPDATE
TEST_F(TableTest, LegacyValueSurvivesRewrite) {
    std::vector<Columns> schema = {{"id", "INT"}, {"price", "FLOAT"}};
    Table table = makeTable("test_legacy_value", schema);

    auto res = table.insertRow({{"1", "."}});
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), TableError::TYPE_VALIDATION_FAILED);

    // A row stored before the check got stricter
    ASSERT_TRUE(table.insertRow({{"1", "."}}, false).has_value());
    ASSERT_TRUE(table.insertRow({{"2", "1.5"}}).has_value());

    Condition byId = {"id", "2", "="};
    EXPECT_EQ(table.deleteRow(&byId), 1u);

    auto rows = table.selectRow(nullptr);
    ASSERT_EQ(rows.size(), 1u);
    EXPECT_EQ(rows[0].values[0], "1");
    EXPECT_EQ(rows[0].values[1], ".");
}

// Test 18: UNIQUE on a STRING column. The key is not involved: the second column is checked by a
// scan, and findDuplicate says which column repeated. Text compares by bytes, so case matters
TEST_F(TableTest, UniqueStringColumnRejectsDuplicate) {
    std::vector<Columns> schema = {{"id", "INT"}, {"email", "STRING", true}};
    Table table = makeTable("test_uq_str", schema);

    ASSERT_TRUE(table.insertRow({{"1", "a@x"}}).has_value());

    Row dup = {{"2", "a@x"}};
    auto res = table.insertRow(dup);
    ASSERT_FALSE(res.has_value());
    EXPECT_EQ(res.error(), TableError::DUPLICATE_KEY);
    EXPECT_EQ(table.findDuplicate(dup), std::optional<size_t>(1));

    EXPECT_TRUE(table.insertRow({{"3", "A@x"}}).has_value());
    EXPECT_EQ(table.selectRow(nullptr).size(), 2u);
}

// Test 19: NULL is never a duplicate, in a scanned column or in the key
TEST_F(TableTest, NullIsNotADuplicateInUniqueColumns) {
    std::vector<Columns> schema = {{"id", "INT", true}, {"email", "STRING", true}};
    Table table = makeTable("test_uq_null", schema);

    EXPECT_TRUE(table.insertRow({{std::nullopt, std::nullopt}}).has_value());
    EXPECT_TRUE(table.insertRow({{std::nullopt, std::nullopt}}).has_value());
    EXPECT_TRUE(table.insertRow({{"1", std::nullopt}}).has_value());
    EXPECT_TRUE(table.insertRow({{"2", std::nullopt}}).has_value());
    // A NULL next to stored values: it matches none of them, and none of them blocks it
    EXPECT_TRUE(table.insertRow({{"3", "a"}}).has_value());
    EXPECT_TRUE(table.insertRow({{std::nullopt, "b"}}).has_value());
    EXPECT_TRUE(table.insertRow({{"4", std::nullopt}}).has_value());
    EXPECT_FALSE(table.findDuplicate({{std::nullopt, std::nullopt}}).has_value());
    EXPECT_EQ(table.selectRow(nullptr).size(), 7u);
}

// Test 20: two UNIQUE columns are both enforced, and the first conflicting one is reported
TEST_F(TableTest, TwoUniqueColumnsAreBothEnforced) {
    std::vector<Columns> schema = {{"id", "INT", true}, {"email", "STRING", true}};
    Table table = makeTable("test_uq_two", schema);
    ASSERT_TRUE(table.insertRow({{"1", "a"}}).has_value());

    EXPECT_EQ(table.findDuplicate({{"1", "b"}}), std::optional<size_t>(0));
    EXPECT_EQ(table.findDuplicate({{"2", "a"}}), std::optional<size_t>(1));
    EXPECT_EQ(table.findDuplicate({{"1", "a"}}), std::optional<size_t>(0));
    EXPECT_EQ(table.findDuplicate({{"2", "b"}}), std::nullopt);
    EXPECT_EQ(table.checkRow({{"2", "a"}}).error(), TableError::DUPLICATE_KEY);
}

// Test 21: a table with no UNIQUE column has no key: duplicates are accepted
TEST_F(TableTest, NoUniqueColumnMeansNoKey) {
    std::vector<Columns> schema = {{"id", "INT"}, {"name", "STRING"}};
    Table table = makeTable("test_no_key", schema);

    EXPECT_EQ(table.keyColumn(), -1);
    EXPECT_TRUE(table.insertRow({{"1", "a"}}).has_value());
    EXPECT_TRUE(table.insertRow({{"1", "b"}}).has_value());
    EXPECT_EQ(table.selectRow(nullptr).size(), 2u);
    Condition byId = {"id", "1", "="};
    EXPECT_EQ(table.selectRow(&byId).size(), 2u);
}

// Test 22: the key is the first UNIQUE INT column wherever it sits. WHERE = takes the index
// path, any other operator still scans (and never sees a null condition)
TEST_F(TableTest, KeyColumnNeedNotBeFirst) {
    std::vector<Columns> schema = {{"name", "STRING"}, {"id", "INT", true}};
    Table table = makeTable("test_key_pos", schema);
    EXPECT_EQ(table.keyColumn(), 1);

    ASSERT_TRUE(table.insertRow({{"a", "5"}}).has_value());
    ASSERT_TRUE(table.insertRow({{"c", "9"}}).has_value());
    EXPECT_EQ(table.insertRow({{"b", "5"}}).error(), TableError::DUPLICATE_KEY);
    EXPECT_EQ(table.findDuplicate({{"b", "5"}}), std::optional<size_t>(1));

    Condition eq = {"id", "5", "="};
    auto found = table.selectRow(&eq);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].values[0], "a");

    Condition gt = {"id", "5", ">"};
    auto greater = table.selectRow(&gt);
    ASSERT_EQ(greater.size(), 1u);
    EXPECT_EQ(greater[0].values[0], "c");
    EXPECT_EQ(table.selectRow(nullptr).size(), 2u);
}

// Test 23: UNIQUE compares like WHERE = does: 1.0 is 1, true is 1, a second UNIQUE INT is
// compared as a number (010 is 10)
TEST_F(TableTest, UniqueComparesByColumnType) {
    std::vector<Columns> schema = {{"id", "INT", true}, {"code", "INT", true},
                                   {"x", "FLOAT", true}, {"b", "BOOL", true}};
    Table table = makeTable("test_uq_types", schema);
    ASSERT_TRUE(table.insertRow({{"1", "10", "1", "true"}}).has_value());

    EXPECT_EQ(table.findDuplicate({{"2", "010", "2", "false"}}), std::optional<size_t>(1));
    EXPECT_EQ(table.findDuplicate({{"2", "11", "1.0", "false"}}), std::optional<size_t>(2));
    EXPECT_EQ(table.findDuplicate({{"2", "11", "2", "1"}}), std::optional<size_t>(3));
    EXPECT_EQ(table.findDuplicate({{"2", "11", "2", "false"}}), std::nullopt);
}

// Test 24: the rewrite (validate = false) keeps rows that repeat a UNIQUE value, like it
// keeps legacy duplicate keys
TEST_F(TableTest, RewriteKeepsRowsThatRepeatAUniqueValue) {
    std::vector<Columns> schema = {{"id", "INT", true}, {"email", "STRING", true}};
    Table table = makeTable("test_uq_rewrite", schema);
    ASSERT_TRUE(table.insertRow({{"1", "a"}}).has_value());
    ASSERT_TRUE(table.insertRow({{"2", "a"}}, false).has_value());
    ASSERT_TRUE(table.insertRow({{"3", "z"}}).has_value());

    Condition byId = {"id", "3", "="};
    EXPECT_EQ(table.deleteRow(&byId), 1u);
    EXPECT_EQ(table.selectRow(nullptr).size(), 2u);
}

// Test 25: the AUTOINCREMENT counter is the highest value + 1, also after a reopen, and a
// value given explicitly moves it
TEST_F(TableTest, NextAutoValueIsHighestPlusOne) {
    std::vector<Columns> schema = {{"id", "INT", true, true}, {"name", "STRING"}};
    const int tableId = nextTableId;
    Table table = makeTable("test_auto", schema);

    EXPECT_EQ(table.autoColumn(), 0);
    EXPECT_EQ(table.keyColumn(), 0);
    EXPECT_EQ(table.nextAutoValue(), 1);

    ASSERT_TRUE(table.insertRow({{"5", "a"}}).has_value());
    EXPECT_EQ(table.nextAutoValue(), 6);
    ASSERT_TRUE(table.insertRow({{"2", "b"}}).has_value());
    EXPECT_EQ(table.nextAutoValue(), 6);

    Table reopened("test_auto", schema, *storage, tableId);
    EXPECT_EQ(reopened.nextAutoValue(), 6);
}

// Test 26: the next AUTOINCREMENT value starts from the stored high-water mark, even on an empty
// table, and the rows can only raise it
TEST_F(TableTest, NextAutoValueStartsFromTheHighWaterMark) {
    Columns id;   id.name = "id";   id.type = "INT"; id.unique = true; id.auto_increment = true;
    id.autoHighWater = 7;
    Columns name; name.name = "name"; name.type = "STRING";
    std::vector<Columns> schema = {id, name};
    const int tableId = nextTableId;
    Table table = makeTable("test_auto_hwm", schema);

    EXPECT_EQ(table.autoMax(), 7);
    EXPECT_EQ(table.nextAutoValue(), 8);

    ASSERT_TRUE(table.insertRow({{"3", "low"}}).has_value());     // below the mark: no change
    EXPECT_EQ(table.nextAutoValue(), 8);
    ASSERT_TRUE(table.insertRow({{"20", "high"}}).has_value());   // above it: the rows win
    EXPECT_EQ(table.nextAutoValue(), 21);

    Table reopened("test_auto_hwm", schema, *storage, tableId);
    EXPECT_EQ(reopened.nextAutoValue(), 21);
}
