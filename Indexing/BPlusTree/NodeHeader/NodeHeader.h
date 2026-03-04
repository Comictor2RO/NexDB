#ifndef NODEHEADER_HPP
#define NODEHEADER_HPP

#include "../../../Storage/PageHeader/PageHeader.hpp"

struct NodeHeader : public PageHeader {
    bool isLeaf;
    int nextPageId;
};

static_assert((sizeof(NodeHeader)) == 20);

#endif
