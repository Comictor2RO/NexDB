#include "PageManager.hpp"

PageManager::PageManager(std::string filename) : cache(16, file) {
    this->filename = filename;
    file.open(filename, std::ios::in | std::ios::out | std::ios::binary);

    if (!file.is_open()) {
        file.open(filename, std::ios::out | std::ios::binary);
        file.close();
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    }

    if (!file.is_open()) {
        throw std::runtime_error("Nu s-a putut deschide sau crea fisierul bazei de date: " + filename);
    }

    file.seekg(0, std::ios::end);
    numberOfPages = (int)(file.tellg() / PAGE_SIZE);
}

PageManager::InsertionResult PageManager::insertRowWithLocation(const std::string &row)
{
    if (numberOfPages > 0)
    {
        Page lastPage = readPage(numberOfPages - 1);
        if (lastPage.hasSpace(row.length() + 1))
        {
            int rowIndex = lastPage.getRowCount();
            lastPage.addRow(row);
            writePage(numberOfPages - 1, lastPage);
            return {true, numberOfPages - 1, rowIndex};
        }
    }
    Page newPage(numberOfPages);
    if (!newPage.addRow(row))
        return {false, -1, -1};
    writePage(numberOfPages, newPage);
    numberOfPages++;
    return {true, numberOfPages - 1, 0};
}

bool PageManager::insertRow(const std::string &row)
{
    return insertRowWithLocation(row).success;
}

void PageManager::clearAll()
{
    cache.clear();
    if (file.is_open())
        file.close();

    // Trunchiere fisier
    file.open(filename, std::ios::out | std::ios::trunc | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Nu s-a putut trunchia fisierul: " + filename);
    }
    file.close();

    // Redeschidere pentru in/out
    file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("Nu s-a putut redeschide fisierul dupa trunchiere: " + filename);
    }

    file.clear(); // Reset error flags (EOF etc.)
    numberOfPages = 0;
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

int PageManager::getNumberOfPages()
{
    return numberOfPages;
}

PageManager::~PageManager()
{
    cache.flushAll();
    if (file.is_open())
        file.close();
}
