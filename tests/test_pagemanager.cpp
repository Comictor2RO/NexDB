#include <gtest/gtest.h>
#include "../Storage/PageManager/PageManager.hpp"
#include <thread>
#include <vector>
#include <set>
#include <cstdio>

class PageManagerTest : public ::testing::Test {
protected:
    std::string testFile = "pm_test.db";

    void SetUp() override  { std::remove(testFile.c_str()); }
    void TearDown() override { std::remove(testFile.c_str()); }
};

// Test 1: inserare simpla, citire inapoi
TEST_F(PageManagerTest, InsertAndReadBack) {
    PageManager pm(testFile);
    pm.insertRow("hello");
    pm.insertRow("world");

    auto rows = pm.getAllRows();
    ASSERT_EQ(rows.size(), 2);
    EXPECT_EQ(rows[0], "hello");
    EXPECT_EQ(rows[1], "world");
}

// Test 2: insertRowWithLocation returneaza pozitia corecta
TEST_F(PageManagerTest, InsertWithLocationReturnsValidIds) {
    PageManager pm(testFile);
    auto r1 = pm.insertRowWithLocation("row1");
    auto r2 = pm.insertRowWithLocation("row2");

    EXPECT_TRUE(r1.success);
    EXPECT_TRUE(r2.success);
    EXPECT_GE(r1.pageId, 0);
    EXPECT_GE(r1.rowIndex, 0);
    // r2 vine dupa r1 — ori acelasi page cu rowIndex mai mare, ori page nou
    EXPECT_TRUE(r2.pageId > r1.pageId || r2.rowIndex > r1.rowIndex);
}

// Test 3: clearAll sterge toate randurile
TEST_F(PageManagerTest, ClearAllRemovesRows) {
    PageManager pm(testFile);
    pm.insertRow("a");
    pm.insertRow("b");
    pm.clearAll();

    EXPECT_EQ(pm.getAllRows().size(), 0);
    EXPECT_EQ(pm.getNumberOfPages(), 0);
}

// Test 4: inserare concurenta — nu se pierd randuri si nu exista duplicate
TEST_F(PageManagerTest, ConcurrentInsertNoDuplicatesNoLoss) {
    PageManager pm(testFile);
    const int THREADS = 8;
    const int ROWS_PER_THREAD = 50;

    std::vector<std::thread> threads;
    for (int t = 0; t < THREADS; t++) {
        threads.emplace_back([&pm, t]() {
            for (int i = 0; i < ROWS_PER_THREAD; i++) {
                pm.insertRow("t" + std::to_string(t) + "_r" + std::to_string(i));
            }
        });
    }
    for (auto &th : threads) th.join();

    auto rows = pm.getAllRows();
    EXPECT_EQ((int)rows.size(), THREADS * ROWS_PER_THREAD);

    // Fara duplicate
    std::set<std::string> unique(rows.begin(), rows.end());
    EXPECT_EQ(unique.size(), rows.size());
}

// Test 5: insertRowWithLocation concurent — fiecare rand ocupa o pozitie unica
TEST_F(PageManagerTest, ConcurrentInsertUniqueLocations) {
    PageManager pm(testFile);
    const int THREADS = 4;
    const int ROWS_PER_THREAD = 25;

    std::vector<std::pair<int,int>> locations(THREADS * ROWS_PER_THREAD);
    std::vector<std::thread> threads;

    for (int t = 0; t < THREADS; t++) {
        threads.emplace_back([&pm, &locations, t]() {
            for (int i = 0; i < ROWS_PER_THREAD; i++) {
                auto r = pm.insertRowWithLocation("x");
                EXPECT_TRUE(r.success);
                locations[t * ROWS_PER_THREAD + i] = {r.pageId, r.rowIndex};
            }
        });
    }
    for (auto &th : threads) th.join();

    // Fiecare (pageId, rowIndex) trebuie sa fie unic
    std::set<std::pair<int,int>> unique(locations.begin(), locations.end());
    EXPECT_EQ(unique.size(), locations.size());
}
