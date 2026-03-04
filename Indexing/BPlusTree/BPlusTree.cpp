#include "BPlusTree.hpp"

BPlusTree::BPlusTree(int order)
    : order(order), root(nullptr)
{}

void BPlusTree::deleteNode(BPlusNode* node)
{
    if (!node->isLeaf)
    {
        for (BPlusNode* child : node->children)
            deleteNode(child);
    }
    delete node;
}

void BPlusTree::clear()
{
    if (root != nullptr)
    {
        deleteNode(root);
        root = nullptr;
    }
}

void BPlusTree::splitLeaf(BPlusNode *leaf, BPlusNode *parent, int index)
{
    int middle = (order + 1) / 2;
    BPlusNode* right = new BPlusNode();
    right->isLeaf = true;
    right->next = nullptr;

    //Puts from the middle until the end in the new node
    right->keys = std::vector<int>(leaf->keys.begin() + middle, leaf->keys.end());
    right->records = std::vector<IndexRecord>(leaf->records.begin() + middle, leaf->records.end());

    leaf->keys.erase(leaf->keys.begin() + middle, leaf->keys.end());
    leaf->records.erase(leaf->records.begin() + middle, leaf->records.end());

    right->next = leaf->next;
    leaf->next = right;

    int promovateKey = right->keys[0];

    if (parent == nullptr)
    {
        BPlusNode* newLeaf = new BPlusNode();
        newLeaf->isLeaf = false;
        newLeaf->keys.push_back(promovateKey);
        newLeaf->children.push_back(leaf);
        newLeaf->children.push_back(right);
        leaf->parent = newLeaf;
        right->parent = newLeaf;
        root = newLeaf;
    }
    else
    {
        int parentIdx = 0;
        while (parentIdx < (int)parent->keys.size() && promovateKey > parent->keys[parentIdx])
            parentIdx++;

        parent->keys.insert(parent->keys.begin() + parentIdx, promovateKey);
        parent->children.insert(parent->children.begin() + parentIdx + 1, right);
        right->parent = parent;

        if ((int)parent->keys.size() > order)
            splitInternal(parent, parent->parent, parentIdx);
    }
}

void BPlusTree::splitInternal(BPlusNode* node, BPlusNode* parent, int index)
{
    int middle = order / 2;
    int promovateKey = node->keys[middle];

    BPlusNode* right = new BPlusNode();
    right->isLeaf = false;
    right->parent = parent;

    right->keys = std::vector<int>(node->keys.begin() + middle + 1, node->keys.end());

    right->children = std::vector<BPlusNode*>(node->children.begin() + middle + 1, node->children.end());

    for (BPlusNode* child : right->children)
        child->parent = right;

    node->keys.erase(node->keys.begin() + middle, node->keys.end());
    node->children.erase(node->children.begin() + middle + 1, node->children.end());

    if (parent == nullptr)
    {
        BPlusNode* newRoot = new BPlusNode();
        newRoot->isLeaf = false;
        newRoot->keys.push_back(promovateKey);
        newRoot->children.push_back(node);
        newRoot->children.push_back(right);
        node->parent  = newRoot;
        right->parent = newRoot;
        root = newRoot;
    }
    else
    {
        int parentIdx = 0;
        while (parentIdx < (int)parent->keys.size() && promovateKey > parent->keys[parentIdx])
            parentIdx++;

        right->parent = parent;
        parent->keys.insert(parent->keys.begin() + parentIdx, promovateKey);
        parent->children.insert(parent->children.begin() + parentIdx + 1, right);

        if ((int)parent->keys.size() > order)
            splitInternal(parent, parent->parent, parentIdx);
    }
}

BPlusNode* BPlusTree::findLeaf(int key)
{
    if (root == nullptr)
        return nullptr;

    BPlusNode* current = root;
    while (!current->isLeaf)
    {
        int i = 0;
        while (i < current->keys.size() && key >= current->keys[i])
        {
            i++;
        }
        current = current->children[i];
    }

    return current;
}

IndexRecord *BPlusTree::search(int key)
{
    BPlusNode* leaf = findLeaf(key);

    if (leaf == nullptr)
        return nullptr;

    for (int i = 0; i < leaf->records.size(); i++)
    {
        if (leaf->records[i].key == key)
        {
            return &leaf->records[i];
        }
    }
    return nullptr;
}

void BPlusTree::insert(int key, int pageId, int offset)
{
    if (root == nullptr)
    {
        root = new BPlusNode();
        root->isLeaf = true;
        root->next = nullptr;
    }
    BPlusNode* leaf = findLeaf(key);

    int i;
    for (i = 0; i < leaf->keys.size() && key > leaf->keys[i]; i++)
    {}

    leaf->keys.insert(leaf->keys.begin() + i, key);
    leaf->records.insert(leaf->records.begin() + i, {key, pageId, offset});

    if ((int)leaf->keys.size() > order)
        splitLeaf(leaf, leaf->parent, i);
}