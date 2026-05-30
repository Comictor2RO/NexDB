#include <gtest/gtest.h>
#include "../Storage/LRUCache/LRUCache.hpp"
#include <fstream>
#include <filesystem>

class LRUCacheTest : public ::testing::Test {
    protected:
        std::string testFile = "test_cache.db";
        std::fstream file;

        void SetUp() override {
            file.open(testFile, std::ios::in | std::ios::out | std::ios::trunc | std::ios::binary);
            if (!file.is_open()) {
                file.clear();
                file.open(testFile, std::ios::out | std::ios::binary);
                file.close();
                file.open(testFile, std::ios::in | std::ios::out | std::ios::binary);
            }
        }

    void TearDown() override {
            if (file.is_open()) {
                file.close();
            }
        std::filesystem::remove(testFile);
    }

    LRUCache::FlushFn makeFlushFn() {
        return [this](int pageId, const Page& p) {
            file.clear();
            file.seekp(pageId * PAGE_SIZE, std::ios::beg);
            file.write(p.getBuffer(), PAGE_SIZE);
            file.flush();
        };
    }
};

//Test 1: Get from empty cache
TEST_F(LRUCacheTest, GetFromEmptyCache) {
    LRUCache cache(3, makeFlushFn());

    Page *result = cache.get(1);
    EXPECT_EQ(result, nullptr);
}

//Test 2: Put and Get single page
TEST_F(LRUCacheTest, PutGetSinglePage) {
    LRUCache cache(3, makeFlushFn());

    Page page(1);
    cache.put(1, page, false);

    Page *retrieve = cache.get(1);

    ASSERT_NE(retrieve, nullptr);
    EXPECT_EQ(retrieve->getPageId(), 1);
}

//Test 3: Put multiple pages
TEST_F(LRUCacheTest, PutMultiplePages) {
    LRUCache cache(3, makeFlushFn());

    Page page1(1);
    Page page2(2);
    Page page3(3);

    cache.put(1, page1, false);
    cache.put(2, page2, false);
    cache.put(3, page3, false);

    EXPECT_NE(cache.get(1), nullptr);
    EXPECT_NE(cache.get(2), nullptr);
    EXPECT_NE(cache.get(3), nullptr);
}

//Test 4: Eviction policy
TEST_F(LRUCacheTest, EvivtionPolicy) {
    LRUCache cache(3, makeFlushFn());

    Page page1(1);
    Page page2(2);
    Page page3(3);
    Page page4(4);

    cache.put(1, page1, false);
    cache.put(2, page2, false);
    cache.put(3, page3, false);
    cache.put(4, page4, false);

    EXPECT_EQ(cache.get(1), nullptr);
    EXPECT_NE(cache.get(2), nullptr);
    EXPECT_NE(cache.get(3), nullptr);
    EXPECT_NE(cache.get(4), nullptr);
}

// Test 5: LRU ordering - access updates recency
TEST_F(LRUCacheTest, LRUOrderingAfterAccess) {
    LRUCache cache(3, makeFlushFn());

    Page page1(1);
    Page page2(2);
    Page page3(3);
    Page page4(4);

    cache.put(1, page1, false);
    cache.put(2, page2, false);
    cache.put(3, page3, false);

    cache.get(1);

    cache.put(4, page4, false);

    EXPECT_NE(cache.get(1), nullptr);
    EXPECT_EQ(cache.get(2), nullptr);
    EXPECT_NE(cache.get(3), nullptr);
    EXPECT_NE(cache.get(4), nullptr);
}

// Test 6: Update existing page (no eviction)
TEST_F(LRUCacheTest, UpdateExistingPage) {
    LRUCache cache(3, makeFlushFn());

    Page page1(1);
    page1.addRow("row1");
    cache.put(1, page1, false);

    Page page1_updated(1);
    page1_updated.addRow("row2");
    cache.put(1, page1_updated, false);

    Page* retrieved = cache.get(1);
    ASSERT_NE(retrieved, nullptr);
    EXPECT_EQ(retrieved->getRowCount(), 1);
}

// Test 7: Dirty page handling - eviction writes to disk
TEST_F(LRUCacheTest, DirtyPageWrittenOnEviction) {
    LRUCache cache(2, makeFlushFn()); // Capacity 2

    Page page1(1);
    page1.addRow("test_data");

    cache.put(1, page1, true);

    Page page2(2);
    Page page3(3);
    cache.put(2, page2, false);

    cache.put(3, page3, false);

    file.clear();
    file.seekg(1 * PAGE_SIZE);
    char buffer[PAGE_SIZE];
    file.read(buffer, PAGE_SIZE);

    bool hasData = false;
    for (int i = 0; i < PAGE_SIZE; ++i) {
        if (buffer[i] != 0) {
            hasData = true;
            break;
        }
    }
    EXPECT_TRUE(hasData);
}

// Test 8: Clean page eviction - no disk write
TEST_F(LRUCacheTest, CleanPageNoWrite) {
    char zeros[PAGE_SIZE] = {0};
    file.seekp(1 * PAGE_SIZE);
    file.write(zeros, PAGE_SIZE);
    file.flush();

    LRUCache cache(2, makeFlushFn());

    Page page1(1);
    Page page2(2);
    Page page3(3);

    cache.put(1, page1, false);
    cache.put(2, page2, false);

    cache.put(3, page3, false);

    file.clear();
    file.seekg(1 * PAGE_SIZE);
    char buffer[PAGE_SIZE];
    file.read(buffer, PAGE_SIZE);

    bool allZeros = true;
    for (int i = 0; i < PAGE_SIZE; ++i) {
        if (buffer[i] != 0) {
            allZeros = false;
            break;
        }
    }
    EXPECT_TRUE(allZeros);
}

// Test 9: Clear cache
TEST_F(LRUCacheTest, ClearCache) {
    LRUCache cache(3, makeFlushFn());

    Page page1(1);
    Page page2(2);
    cache.put(1, page1, false);
    cache.put(2, page2, false);

    cache.clear();

    EXPECT_EQ(cache.get(1), nullptr);
    EXPECT_EQ(cache.get(2), nullptr);
}

// Test 10: FlushAll writes all dirty pages
TEST_F(LRUCacheTest, FlushAllDirtyPages) {
    LRUCache cache(3, makeFlushFn());

    Page page1(1);
    page1.addRow("data1");
    Page page2(2);
    page2.addRow("data2");

    cache.put(1, page1, true);
    cache.put(2, page2, true);

    cache.flushAll();

    file.clear();
    file.seekg(1 * PAGE_SIZE);
    char buffer1[PAGE_SIZE];
    file.read(buffer1, PAGE_SIZE);

    file.seekg(2 * PAGE_SIZE);
    char buffer2[PAGE_SIZE];
    file.read(buffer2, PAGE_SIZE);

    bool page1Written = false, page2Written = false;
    for (int i = 0; i < PAGE_SIZE; ++i) {
        if (buffer1[i] != 0) page1Written = true;
        if (buffer2[i] != 0) page2Written = true;
    }

    EXPECT_TRUE(page1Written);
    EXPECT_TRUE(page2Written);
}

// Test 11: Capacity limit enforcement
TEST_F(LRUCacheTest, CapacityLimitEnforced) {
    LRUCache cache(1, makeFlushFn());

    Page page1(1);
    Page page2(2);

    cache.put(1, page1, false);
    cache.put(2, page2, false);

    EXPECT_EQ(cache.get(1), nullptr);
    EXPECT_NE(cache.get(2), nullptr);
}

// Test 12: Get updates LRU position
TEST_F(LRUCacheTest, GetUpdatesLRUPosition) {
    LRUCache cache(3, makeFlushFn());

    Page page1(1);
    Page page2(2);
    Page page3(3);

    cache.put(1, page1, false);
    cache.put(2, page2, false);
    cache.put(3, page3, false);

    cache.get(1);
    cache.get(1);

    Page page4(4);
    cache.put(4, page4, false);

    EXPECT_NE(cache.get(1), nullptr);
    EXPECT_EQ(cache.get(2), nullptr);
}

// Test 13: Mixed dirty and clean pages
TEST_F(LRUCacheTest, MixedDirtyCleanPages) {
    LRUCache cache(3, makeFlushFn());

    Page page1(1);
    Page page2(2);
    Page page3(3);

    cache.put(1, page1, true);
    cache.put(2, page2, false);
    cache.put(3, page3, true);

    EXPECT_NE(cache.get(1), nullptr);
    EXPECT_NE(cache.get(2), nullptr);
    EXPECT_NE(cache.get(3), nullptr);
}
