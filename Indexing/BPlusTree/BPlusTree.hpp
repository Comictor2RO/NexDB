#ifndef BPLUSTREE_HPP
#define BPLUSTREE_HPP

#include "../BPlusNode/BPlusNode.hpp"
#include "../IndexRecord/IndexRecord.hpp"

class BPlusTree
{
    public:
        BPlusTree(int order);

        void insert(int key, int pageId, int offset);
        IndexRecord *search(int key);
        void clear();

    private:
        int order;
        std::unique_ptr<BPlusNode> root;

        BPlusNode* findLeaf(int key);
        void deleteNode(BPlusNode* node);
        void splitLeaf(BPlusNode* leaf, BPlusNode* parent, int index);
        void splitInternal(BPlusNode* node, BPlusNode* parent, int index);
};


#endif