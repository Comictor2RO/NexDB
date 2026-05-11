#include <gtest/gtest.h>
#include "../Catalog/Catalog.hpp"
#include <filesystem>
#include <fstream>
#include <thread>
#include <atomic>

// Test 1: Create new table
TEST(CatalogTest, CreateNewTable) {
    Catalog catalog;
    std::vector<Columns> schema = {
        {"id", "INT"},
        {"name", "STRING"}
    };

    catalog.createTable("test_table", schema);
    EXPECT_TRUE(catalog.tableExists("test_table"));
}

// Test 2: Table does not exist
TEST(CatalogTest, TableDoentExist) {
    Catalog catalog;
    EXPECT_FALSE(catalog.tableExists("non_existent_table"));
}

// Test 3: Get schema for existing table
TEST(CatalogTest, GetSchemaForExistingTable) {
    Catalog catalog;
    std::vector<Columns> schema = {
        {"id", "INT"},
        {"name", "STRING"}
    };

    catalog.createTable("test_schema", schema);
    auto retrieve = catalog.getColumns("test_schema");
    EXPECT_EQ(retrieve.size(), 2);
    EXPECT_EQ(retrieve[0].name, "id");
    EXPECT_EQ(retrieve[1].name, "name");
    EXPECT_EQ(retrieve[0].type, "INT");
    EXPECT_EQ(retrieve[1].type, "STRING");
}

// Test 4: Get schema for non-existent table
TEST(CatalogTest, GetSchemaNonExistent)
{
    Catalog catalog;
    auto retrieve = catalog.getColumns("non_existent_table");

    EXPECT_TRUE(retrieve.empty());
}

// Test 5: Drop existing table
TEST(CatalogTest, DropExistingTable) {
    Catalog catalog;
    std::vector<Columns> schema = {{"id", "INT"}};

    catalog.createTable("test_drop", schema);

    EXPECT_TRUE(catalog.tableExists("test_drop")); //EXISTS ?
    catalog.dropTable("test_drop"); // Drop
    EXPECT_FALSE(catalog.tableExists("test_drop")); //REMOVE ?
}

// Test 6: Drop non-Existing table
TEST(CatalogTest, DropNonExistingTable) {
    Catalog catalog;
    EXPECT_NO_THROW(catalog.dropTable("non_existent_table"));
}

// Test 7: Create duplicate table
TEST(CatalogTest, CreateDuplicateTable) {
    Catalog catalog;
    std::vector<Columns> schema = {{"id", "INT"}};
    std::vector<Columns> schema2 = {{"name", "STRING"}};

    catalog.createTable("test_duplicate", schema);
    catalog.createTable("test_duplicate", schema2);

    auto retrieve = catalog.getColumns("test_duplicate");
    EXPECT_EQ(retrieve.size(), 1);
    EXPECT_EQ(retrieve[0].name, "id");
}

// Test 8: Catalog survives restart
TEST(CatalogTest, SurviveTest) {
    // Create the .db file so Catalog recognizes it at load()
    std::ofstream dummy("test_survive.db");
    dummy.close();

    {
        Catalog catalog;

        std::vector<Columns> schema = {
            {"id", "INT"},
            {"email", "STRING"}
        };

        catalog.createTable("test_survive", schema);
    }

    {
        Catalog catalog2;
        EXPECT_TRUE(catalog2.tableExists("test_survive"));

        auto retrieve = catalog2.getColumns("test_survive");
        EXPECT_EQ(retrieve.size(), 2);
        EXPECT_EQ(retrieve[0].name, "id");
        EXPECT_EQ(retrieve[1].name, "email");
        EXPECT_EQ(retrieve[0].type, "INT");
        EXPECT_EQ(retrieve[1].type, "STRING");
    }

    // Cleanup
    std::filesystem::remove("test_survive.db");
}

// Test 9: Multiple Tables in catalog
TEST(CatalogTest, MultipleTables) {
    Catalog catalog;

    catalog.createTable("users", {{"id", "INT"}});
    catalog.createTable("products", {{"name", "STRING"}});
    catalog.createTable("orders", {{"total", "INT"}});

    EXPECT_TRUE(catalog.tableExists("users"));
    EXPECT_TRUE(catalog.tableExists("products"));
    EXPECT_TRUE(catalog.tableExists("orders"));
}

// Test 10: Empty Schema
TEST(CatalogTest, EmptySchema) {
    Catalog catalog;
    std::vector<Columns> schema = {};
    catalog.createTable("empty_schema", schema);

    EXPECT_FALSE(catalog.tableExists("empty_schema"));
}

// Test 11: createTable concurent — doar un thread reuseste, schema nu e corupta
TEST(CatalogTest, ConcurrentCreateSameTable) {
    Catalog catalog;
    std::vector<Columns> schema = {{"id", "INT"}, {"name", "STRING"}};
    const int THREADS = 10;

    std::vector<std::thread> threads;
    for (int i = 0; i < THREADS; i++)
        threads.emplace_back([&]() { catalog.createTable("conc_table", schema); });
    for (auto &t : threads) t.join();

    EXPECT_TRUE(catalog.tableExists("conc_table"));
    auto cols = catalog.getColumns("conc_table");
    EXPECT_EQ(cols.size(), 2);
}

// Test 12: citiri concurente — tableExists si getColumns nu se blocheaza reciproc
TEST(CatalogTest, ConcurrentReadsDoNotBlock) {
    Catalog catalog;
    catalog.createTable("read_table", {{"id", "INT"}});

    const int THREADS = 20;
    std::atomic<int> successCount{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < THREADS; i++) {
        threads.emplace_back([&]() {
            if (catalog.tableExists("read_table"))
                successCount++;
            auto cols = catalog.getColumns("read_table");
            if (cols.size() == 1)
                successCount++;
        });
    }
    for (auto &t : threads) t.join();

    EXPECT_EQ(successCount.load(), THREADS * 2);
}

// Test 13: createTable si dropTable concurent — nu exista crash sau date corupte
TEST(CatalogTest, ConcurrentCreateAndDrop) {
    Catalog catalog;
    std::vector<Columns> schema = {{"id", "INT"}};

    std::vector<std::thread> threads;
    for (int i = 0; i < 5; i++)
        threads.emplace_back([&]() { catalog.createTable("cd_table", schema); });
    for (int i = 0; i < 5; i++)
        threads.emplace_back([&]() { catalog.dropTable("cd_table"); });
    for (auto &t : threads) t.join();

    // Starea finala trebuie sa fie consistenta (nu crash, nu date corupte)
    bool exists = catalog.tableExists("cd_table");
    auto cols = catalog.getColumns("cd_table");
    if (exists)
        EXPECT_FALSE(cols.empty());
    else
        EXPECT_TRUE(cols.empty());
}

// Test 14: getColumns concurent cu createTable — nu returneaza stare partiala
TEST(CatalogTest, ConcurrentWriteAndRead) {
    Catalog catalog;
    std::vector<Columns> schema = {{"id", "INT"}, {"val", "STRING"}};

    std::atomic<int> corruptReads{0};
    std::vector<std::thread> threads;

    for (int i = 0; i < 8; i++) {
        threads.emplace_back([&]() { catalog.createTable("wr_table", schema); });
        threads.emplace_back([&]() {
            auto cols = catalog.getColumns("wr_table");
            // O singura varianta valida: lista goala (inainte de creare) sau lista completa
            if (!cols.empty() && cols.size() != 2)
                corruptReads++;
        });
    }
    for (auto &t : threads) t.join();

    EXPECT_EQ(corruptReads.load(), 0);
}
