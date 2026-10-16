#include <gtest//gtest.h>
#include "../Indexing/BPlusTree/BPlusTree.hpp"

//Test 1: Insert single key
TEST(BPlusTreeTest, InsertSingleKey) {
    BPlusTree tree(3);

    tree.insert(10, 1, 0);
    auto result = tree.search(10);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->key, 10);
    EXPECT_EQ(result->pageId, 1);
    EXPECT_EQ(result->offset, 0);
}

//Test 2: Search for non-existing key
TEST(BPlusTreeTest, SearchNonExistingKey) {
    BPlusTree tree(3);

    tree.insert(10, 1, 0);
    tree.insert(20, 2, 0);
    auto result = tree.search(15);
    EXPECT_FALSE(result.has_value());
}

//Test 3: Insert multiple keys
TEST(BPlusTreeTest, InsertMultipleKeys) {
    BPlusTree tree(3);

    tree.insert(10, 1, 0);
    tree.insert(20, 2, 0);
    tree.insert(30, 3, 0);
    tree.insert(40, 4, 0);

    EXPECT_TRUE(tree.search(10).has_value());
    EXPECT_TRUE(tree.search(20).has_value());
    EXPECT_TRUE(tree.search(30).has_value());
    EXPECT_TRUE(tree.search(40).has_value());
}

//Test 4: Insert keys in reverse order
TEST(BPlusTreeTest, InsertKeysReverseOrder) {
    BPlusTree tree(3);

    tree.insert(40, 4, 0);
    tree.insert(30, 3, 0);
    tree.insert(20, 2, 0);
    tree.insert(10, 1, 0);

    EXPECT_TRUE(tree.search(10).has_value());
    EXPECT_TRUE(tree.search(20).has_value());
    EXPECT_TRUE(tree.search(30).has_value());
    EXPECT_TRUE(tree.search(40).has_value());
}

//Test 5: Insert keys in random order
TEST(BPlusTreeTest, InsertKeysRandomOrder) {
    BPlusTree tree(3);

    tree.insert(30, 3, 0);
    tree.insert(10, 1, 0);
    tree.insert(40, 4, 0);
    tree.insert(20, 2, 0);

    EXPECT_TRUE(tree.search(10).has_value());
    EXPECT_TRUE(tree.search(20).has_value());
    EXPECT_TRUE(tree.search(30).has_value());
    EXPECT_TRUE(tree.search(40).has_value());
}

//Test 6: Trigger leaf split
TEST(BPlusTreeTest, TriggerLeafSplit) {
    BPlusTree tree(3);

    tree.insert(10, 1, 0);
    tree.insert(20, 2, 0);
    tree.insert(30, 3, 0);

    EXPECT_TRUE(tree.search(10).has_value());
    EXPECT_TRUE(tree.search(20).has_value());
    EXPECT_TRUE(tree.search(30).has_value());
}

//Test 7: Trigger internal node split
TEST(BPlusTreeTest, TriggerInternalNodeSplit) {
    BPlusTree tree(3);

    for (int i = 1; i <= 10; ++i) {
        tree.insert(i * 10, i, 0);
    }

    for (int i = 1; i <= 10; ++i) {
        auto result = tree.search(i * 10);
        EXPECT_TRUE(result.has_value());
        EXPECT_EQ(result->pageId, i);
    }
}

//Test 8: Insert duplicates key
TEST(BPlusTreeTest, InsertDuplicatesKey) {
    BPlusTree tree(3);

    tree.insert(10, 1, 0);
    tree.insert(10, 2, 100);

    auto result = tree.search(10);

    //both entries stay in the tree; search returns the one it reaches first in the leaf.
    //Table never inserts a duplicate key: the first INT column is unique (Table::checkRow)
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(result->pageId, 2);
    EXPECT_EQ(result->offset, 100);
}

//Test 9: Search on empty tree
TEST(BPlusTreeTest, SearchEmptyTree) {
    BPlusTree tree(3);

    auto result = tree.search(15);
    EXPECT_FALSE(result.has_value());
}

//Test 10: Clear tree and verify empty
TEST(BPlusTreeTest, ClearTree) {
    BPlusTree tree(3);

    tree.insert(10, 1, 0);
    tree.insert(20, 2, 0);
    tree.insert(30, 3, 0);

    tree.clear();

    EXPECT_FALSE(tree.search(10).has_value());
    EXPECT_FALSE(tree.search(20).has_value());
    EXPECT_FALSE(tree.search(30).has_value());
}

//Test 11: Stress test
TEST(BPlusTreeTest, StressTest) {
    BPlusTree tree(4);

    for (int i = 1; i <= 10000; ++i) {
        tree.insert(i, i, i * 10);
    }

    for (int i = 1; i <= 10000; ++i) {
        auto result = tree.search(i);
        EXPECT_TRUE(result.has_value());
        EXPECT_EQ(result->pageId, i);
        EXPECT_EQ(result->offset, i * 10);
    }
    EXPECT_FALSE(tree.search(10001).has_value());
}

//Test 12: Different order size
TEST(BPlusTreeTest, DiffOrderSize) {
    BPlusTree tree(3);
    tree.insert(10, 1, 0);
    EXPECT_TRUE(tree.search(10).has_value());

    BPlusTree tree2(4);
    tree2.insert(10, 1, 0);
    EXPECT_TRUE(tree2.search(10).has_value());

    BPlusTree tree3(5);
    tree3.insert(10, 1, 0);
    EXPECT_TRUE(tree3.search(10).has_value());
}

//Test 13: Insert with different offsets
TEST(BPlusTreeTest, InsertDiffOffsets) {
    BPlusTree tree(3);

    tree.insert(10, 5, 111);
    tree.insert(20, 7, 256);

    auto result1 = tree.search(10);
    EXPECT_TRUE(result1.has_value());
    EXPECT_EQ(result1->pageId, 5);
    EXPECT_EQ(result1->offset, 111);

    auto result2 = tree.search(20);
    EXPECT_TRUE(result2.has_value());
    EXPECT_EQ(result2->pageId, 7);
    EXPECT_EQ(result2->offset, 256);
}