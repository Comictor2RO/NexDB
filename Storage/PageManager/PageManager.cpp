#include "PageManager.hpp"

PageManager::PageManager(std::string filename, int cacheCapacity) : cache(cacheCapacity, file) {
    std::unique_lock<std::mutex> lock(file_mutex);
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
    std::unique_lock<std::mutex> insert_lock(insert_mutex);

    int currentPages = numberOfPages;

    if (currentPages > 0)
    {
        Page lastPage = readPage(currentPages - 1);
        if (lastPage.hasSpace(row.length() + 1))
        {
            int rowIndex = lastPage.getRowCount();
            lastPage.addRow(row);
            writePage(currentPages - 1, lastPage);
            return {true, currentPages - 1, rowIndex};
        }
    }

    Page newPage(numberOfPages);
    if (!newPage.addRow(row))
        return {false, -1, -1};
    int newPageId = numberOfPages;
    numberOfPages++;
    writePage(newPageId, newPage);
    return {true, newPageId, 0};
}

bool PageManager::insertRow(const std::string &row)
{
    return insertRowWithLocation(row).success;
}

void PageManager::clearAll()
{
    {
        std::unique_lock<std::shared_mutex> lock(cache_mutex);
        cache.clear();
    }

    {
        std::unique_lock<std::mutex> lock(file_mutex);

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
}

Page PageManager::readPage(int pageNumber)
{
    {
        std::shared_lock<std::shared_mutex> cache_lock(cache_mutex);
        Page *cached = cache.get(pageNumber);
        if (cached != nullptr)
            return *cached;
    }
    {
        std::unique_lock<std::mutex> file_lock(file_mutex);
        file.clear();
        file.seekg(pageNumber * PAGE_SIZE);
        Page page;
        file.read(page.getBuffer(), PAGE_SIZE);

        {
            std::unique_lock<std::shared_mutex> cache_lock(cache_mutex);
            cache.put(pageNumber, page, false);
        }
        return page;
    }
}

void PageManager::writePage(int pageId, const Page &page)
{
    {
        std::unique_lock<std::shared_mutex> cache_lock(cache_mutex);
        cache.put(pageId, page, true);
    }

    {
        std::unique_lock<std::mutex> file_lock(file_mutex);
        file.seekp(pageId * PAGE_SIZE, std::ios::beg);
        file.write(page.getBuffer(), PAGE_SIZE);
        if (pageId >= numberOfPages) {
            std::unique_lock<std::shared_mutex> num_lock(cache_mutex);
            numberOfPages = pageId + 1;
        }
    }
}

std::vector<std::string> PageManager::getAllRows()
{
    // Snapshot numberOfPages to avoid race conditions
    int totalPages;
    {
        std::shared_lock lock(cache_mutex);
        totalPages = numberOfPages;
    }

    std::vector<std::string> rows;
    for (int i = 0; i < totalPages; i++) {
        Page page = readPage(i);
        std::vector<std::string> pageRows = page.getRows();
        rows.insert(rows.end(), pageRows.begin(), pageRows.end());
    }

    return rows;
}

int PageManager::getNumberOfPages()
{
    std::shared_lock<std::shared_mutex> num_lock(cache_mutex);
    return numberOfPages;
}

PageManager::~PageManager()
{
    {
        std::unique_lock<std::shared_mutex> lock(cache_mutex);
        cache.flushAll();
    }
    {
        std::unique_lock<std::mutex> lock(file_mutex);
        if (file.is_open()) file.close();
    }
}
