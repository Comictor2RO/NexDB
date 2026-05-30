#ifndef PAGE_HPP
#define PAGE_HPP

#include "../PageHeader/PageHeader.hpp"
#include <iostream>
#include <string>
#include <cstring>
#include <vector>

#define PAGE_SIZE 4096

class Page {
    public:
        //Constructors
        Page();
        Page(int pageId);
        Page(int pageId, int tableId);

        //Methods
        bool hasSpace(int rowSize) const;
        bool addRow(const std::string &row);

        //Getters
        std::vector<std::string> getRows() const;
        int getPageId() const;
        int getTableId() const;
        int getFreeSpace() const;
        int getRowCount() const;
        char *getBuffer();
        const char *getBuffer() const;

        ~Page();
    private:
        char data[PAGE_SIZE];
};


#endif