#ifndef BPLUSNODE_HPP
#define BPLUSNODE_HPP
#include <memory>
#include <vector>

#include "../IndexRecord/IndexRecord.hpp"

struct BPlusNode {
    BPlusNode() : parent(nullptr), isLeaf(false) {}
    std::vector<int> keys; //Keys inside the node
    std::vector<std::unique_ptr<BPlusNode>> children; //Pointers towards the children(if is internal node)
    BPlusNode *parent;
    std::vector<IndexRecord> records; //Data
    bool isLeaf = false;
};

#endif
