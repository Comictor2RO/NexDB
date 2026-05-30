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
    std::vector<std::pair<std::string, std::string>> assignments = {{"name", "Johnny"}};
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
