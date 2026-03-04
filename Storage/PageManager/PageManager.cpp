#include "PageManager.hpp"

PageManager::PageManager(std::string filename) : cache(16, file) {
    this->filename = filename;
    file.open(filename, std::ios::in | std::ios::out | std::ios::binary);

    if (!file.is_open()) {
        file.open(filename, std::ios::out | std::ios::binary);
        file.close();
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
        numberOfPages = 0;
    } else {
        file.seekg(0, std::ios::end);
        numberOfPages = file.tellg() / PAGE_SIZE;
    }
}

bool PageManager::insertRow(const std::string &row)
{
    if (numberOfPages > 0) //If there are pages
    {
        Page lastPage = readPage(numberOfPages - 1);
        if (lastPage.hasSpace(row.length() + 1)) //Checks if the last page has space
        {
            lastPage.addRow(row); //Adds the row
            writePage(numberOfPages - 1, lastPage); //Saves it
            return true;
        }
    }
    Page newPage(numberOfPages);
    if (!newPage.addRow(row))
        return false;
    writePage(numberOfPages, newPage);
    numberOfPages++;
    return true;
}

void PageManager::clearAll()
{
    file.close();
    file.open(filename, std::ios::out | std::ios::trunc | std::ios::binary);
    file.close();
    file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    numberOfPages = 0;
    cache.clear();
}

Page PageManager::readPage(int pageNumber)
{
    Page page;
    Page *cached = cache.get(pageNumber);
    if (cached != nullptr)
        return *cached;
    file.clear();
    file.seekg(pageNumber * PAGE_SIZE); //Changes the current position where to read
    file.read(page.getBuffer(), PAGE_SIZE);

    cache.put(pageNumber, page, false);
    return page;
}

void PageManager::writePage(int pageId, const Page &page)
{
    cache.put(pageId, page, true);
}

std::vector<std::string> PageManager::getAllRows()
{
    std::vector<std::string> rows;

    for (int i = 0; i < numberOfPages; i++) {
        Page page = readPage(i);
        std::vector<std::string> pageRows = page.getRows();
        rows.insert(rows.end(), pageRows.begin(), pageRows.end());
    }

    return rows;
}

PageManager::~PageManager()
{
    cache.flushAll();
    if (file.is_open())
        file.close();
}
