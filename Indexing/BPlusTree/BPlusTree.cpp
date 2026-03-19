#include "BPlusTree.hpp"

BPlusTree::BPlusTree(int order)
    : order(order), root(nullptr)
{}

void BPlusTree::clear()
{
    root.reset();
}

void BPlusTree::splitLeaf(BPlusNode *leaf, BPlusNode *parent, int index)
{
    int middle = (order + 1) / 2;
    auto right = std::make_unique<BPlusNode>();
    right->isLeaf = true;
    right->parent = parent;

    // Move data to right
    right->keys.assign(leaf->keys.begin() + middle, leaf->keys.end());
    right->records.assign(leaf->records.begin() + middle, leaf->records.end());

    leaf->keys.erase(leaf->keys.begin() + middle, leaf->keys.end());
    leaf->records.erase(leaf->records.begin() + middle, leaf->records.end());

    int promotedKey = right->keys[0];

    if (parent == nullptr) {
        // Root split: move current root into new root
        auto newRoot = std::make_unique<BPlusNode>();
        newRoot->isLeaf = false;
        newRoot->keys.push_back(promotedKey);

        BPlusNode* oldRootPtr = root.get();
        newRoot->children.push_back(std::move(root));
        newRoot->children.push_back(std::move(right));

        // Update parents
        if (oldRootPtr) oldRootPtr->parent = newRoot.get();
        if (newRoot->children.size() > 1) newRoot->children[1]->parent = newRoot.get();

        root = std::move(newRoot);
    } else {
        // Insert into existing parent
        right->parent = parent;

        parent->keys.insert(parent->keys.begin() + index, promotedKey);
        parent->children.insert(parent->children.begin() + index + 1, std::move(right));

        // Check if parent needs split
        if ((int)parent->keys.size() > order) {
            BPlusNode* grandparent = parent->parent;
            int parentIndex = 0;
            if (grandparent) {
                for (int i = 0; i < (int)grandparent->children.size(); i++) {
                    if (grandparent->children[i].get() == parent) {
                        parentIndex = i;
                        break;
                    }
                }
            }
            splitInternal(parent, grandparent, parentIndex);
        }
    }
}

void BPlusTree::splitInternal(BPlusNode* node, BPlusNode* parent, int index)
{
    int middle = order / 2;
    int promotedKey = node->keys[middle];

    auto right = std::make_unique<BPlusNode>();
    right->isLeaf = false;

    // Copy keys to right (after promoted)
    right->keys.assign(node->keys.begin() + middle + 1, node->keys.end());

    // Move children efficiently
    right->children.assign(std::make_move_iterator(node->children.begin() + middle + 1),
                           std::make_move_iterator(node->children.end()));

    // Update parent pointers for right children
    for (auto& child : right->children)
        child->parent = right.get();

    // Truncate left node
    node->keys.erase(node->keys.begin() + middle, node->keys.end());
    node->children.erase(node->children.begin() + middle + 1, node->children.end());

    if (parent == nullptr) {
        // Root split
        auto newRoot = std::make_unique<BPlusNode>();
        newRoot->isLeaf = false;
        newRoot->keys.push_back(promotedKey);

        BPlusNode* oldRootPtr = root.get();
        newRoot->children.push_back(std::move(root));
        newRoot->children.push_back(std::move(right));

        // Update parents
        if (oldRootPtr) oldRootPtr->parent = newRoot.get();
        if (newRoot->children.size() > 1) newRoot->children[1]->parent = newRoot.get();

        root = std::move(newRoot);
    } else {
        // Insert into parent
        right->parent = parent;

        parent->keys.insert(parent->keys.begin() + index, promotedKey);
        parent->children.insert(parent->children.begin() + index + 1, std::move(right));

        if ((int)parent->keys.size() > order) {
            BPlusNode* grandparent = parent->parent;
            int parentIndex = 0;
            if (grandparent) {
                for (int i = 0; i < (int)grandparent->children.size(); i++) {
                    if (grandparent->children[i].get() == parent) {
                        parentIndex = i;
                        break;
                    }
                }
            }
            splitInternal(parent, grandparent, parentIndex);
        }
    }
}

BPlusNode* BPlusTree::findLeaf(int key)
{
    if (root == nullptr)
        return nullptr;

    BPlusNode* current = root.get();
    while (!current->isLeaf)
    {
        int i = 0;
        while (i < (int)current->keys.size() && key >= current->keys[i])
            i++;
        current = current->children[i].get();
    }
    return current;
}

IndexRecord *BPlusTree::search(int key)
{
    BPlusNode* leaf = findLeaf(key);
    if (leaf == nullptr)
        return nullptr;

    for (int i = 0; i < (int)leaf->records.size(); i++)
    {
        if (leaf->records[i].key == key)
            return &leaf->records[i];
    }
    return nullptr;
}

void BPlusTree::insert(int key, int pageId, int offset)
{
    if (root == nullptr)
    {
        root = std::make_unique<BPlusNode>();
        root->isLeaf = true;
    }
    BPlusNode* leaf = findLeaf(key);

    int i = 0;
    while (i < (int)leaf->keys.size() && key > leaf->keys[i])
        i++;

    leaf->keys.insert(leaf->keys.begin() + i, key);
    leaf->records.insert(leaf->records.begin() + i, {key, pageId, offset});

    if ((int)leaf->keys.size() > order) {
        int leafIndex = 0;
        if (leaf->parent) {
            for (int j = 0; j < (int)leaf->parent->children.size(); j++) {
                if (leaf->parent->children[j].get() == leaf) {
                    leafIndex = j;
                    break;
                }
            }
        }
        splitLeaf(leaf, leaf->parent, leafIndex);
    }
}