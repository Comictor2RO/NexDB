#ifndef PAGEHEADER_HPP
#define PAGEHEADER_HPP

struct PageHeader {
    int pageId;
    int freeSpace;
    int rowNumber;
    int nextRowOffset;
};

static_assert(sizeof(PageHeader) == 16);

#endif
