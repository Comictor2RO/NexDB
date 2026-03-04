#ifndef ROW_PTR_HPP
#define ROW_PTR_HPP

#include <cstdint>

struct RowPtr {
    std::uint32_t pageId;
    std::uint32_t slot;

    RowPtr(uint32_t pageId = 0, uint32_t slot = 0) : pageId(pageId), slot(slot) {}
};

#endif