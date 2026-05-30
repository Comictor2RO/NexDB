#ifndef PAGEHEADER_HPP
#define PAGEHEADER_HPP

struct PageHeader {
    int pageId;
    int tableId;
    int freeSpace;
    int rowNumber;
    int nextRowOffset;
};

static_assert(sizeof(PageHeader) == 20);

#endif
