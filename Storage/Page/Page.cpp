#include "Page.hpp"

Page::Page()
{
    memset(data, 0, PAGE_SIZE);
}

Page::Page(int pageId)
{
    memset(data, 0, PAGE_SIZE);
    PageHeader *header = (PageHeader *)data;
    header->pageId = pageId;
    header->freeSpace = PAGE_SIZE - sizeof(PageHeader);
    header->rowNumber = 0;
    header->nextRowOffset = sizeof(PageHeader);
}

std::vector<std::string> Page::getRows() const
{
    const PageHeader *header = (const PageHeader *)data;
    std::vector<std::string> rows;
    int currentOffset = sizeof(PageHeader);

    for (int i = 0; i < header->rowNumber; i++)
    {
        if (currentOffset >= PAGE_SIZE) break;
        
        std::string row;
        while (currentOffset < PAGE_SIZE && data[currentOffset] != '\n')
        {
            row += data[currentOffset];
            currentOffset++;
        }
        
        if (currentOffset < PAGE_SIZE && data[currentOffset] == '\n')
        {
            rows.push_back(row);
            currentOffset++;
        }
    }

    return rows;
}

int Page::getPageId() const
{
    PageHeader *header = (PageHeader *)data;
    return header->pageId;
}

int Page::getFreeSpace() const
{
    PageHeader *header = (PageHeader *)data;
    return header->freeSpace;
}

int Page::getRowCount() const
{
    const PageHeader *header = (const PageHeader *)data;
    return header->rowNumber;
}

char *Page::getBuffer()
{
    return data;
}

const char *Page::getBuffer() const
{
    return data;
}

bool Page::hasSpace(int rowSize) const
{
    PageHeader *header = (PageHeader *)data;
    return header->freeSpace >= rowSize;
}

bool Page::addRow(const std::string &row)
{
    PageHeader *header = (PageHeader *)data;

    if (!hasSpace(row.length() + 1)) //If there is no space returns false
        return false;

    int offset = header->nextRowOffset;

    memcpy(data + offset, row.c_str(), row.length()); //Saves the row
    data[offset + row.length()] = '\n'; //Adds the newline at the end

    header->nextRowOffset += row.length() + 1;
    header->freeSpace -= row.length() + 1; //Updates the free space available
    header->rowNumber++; //Updates the number of rows

    return true;
}

Page::~Page()
{}
