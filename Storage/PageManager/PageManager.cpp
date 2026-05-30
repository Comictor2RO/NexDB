#include "PageManager.hpp"

PageManager::PageManager(StorageFile& storage, int tableId, int cacheCapacity)
    : storage(storage), tableId(tableId),
      cache(cacheCapacity, [&storage](int gid, const Page& p) { storage.writePage(gid, p); })
{
    pageIds = storage.getPagesForTable(tableId);
}

PageManager::InsertionResult PageManager::insertRowWithLocation(const std::string &row)
{
    std::unique_lock<std::mutex> insert_lock(insert_mutex);

    if (!pageIds.empty()) {
        int lastGlobalId = pageIds.back();
        Page lastPage = readPage(lastGlobalId);
        if (lastPage.hasSpace((int)row.length() + 1)) {
            int rowIndex = lastPage.getRowCount();
            lastPage.addRow(row);
            writePage(lastGlobalId, lastPage);
            return {true, lastGlobalId, rowIndex};
        }
    }

    int newGlobalId = storage.allocatePage(tableId);
    Page newPage(newGlobalId, tableId);
    if (!newPage.addRow(row)) {
        storage.freePage(newGlobalId);
        return {false, -1, -1};
    }
    {
        std::unique_lock<std::shared_mutex> lock(cache_mutex);
        pageIds.push_back(newGlobalId);
    }
    writePage(newGlobalId, newPage);
    return {true, newGlobalId, 0};
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
    for (int gid : pageIds)
        storage.freePage(gid);
    pageIds.clear();
}

Page PageManager::readPage(int globalPageId)
{
    {
        std::shared_lock<std::shared_mutex> cache_lock(cache_mutex);
        Page* cached = cache.get(globalPageId);
        if (cached != nullptr)
            return *cached;
    }
    Page page = storage.readPage(globalPageId);
    {
        std::unique_lock<std::shared_mutex> cache_lock(cache_mutex);
        cache.put(globalPageId, page, false);
    }
    return page;
}

void PageManager::writePage(int globalPageId, const Page &page)
{
    {
        std::unique_lock<std::shared_mutex> lock(cache_mutex);
        cache.put(globalPageId, page, false);
    }
    storage.writePage(globalPageId, page);
}

std::vector<std::string> PageManager::getAllRows()
{
    std::vector<int> snapshot;
    {
        std::shared_lock<std::shared_mutex> lock(cache_mutex);
        snapshot = pageIds;
    }

    std::vector<std::string> rows;
    for (int gid : snapshot) {
        Page page = readPage(gid);
        auto pageRows = page.getRows();
        rows.insert(rows.end(), pageRows.begin(), pageRows.end());
    }
    return rows;
}

int PageManager::getNumberOfPages()
{
    std::shared_lock<std::shared_mutex> lock(cache_mutex);
    return (int)pageIds.size();
}

std::vector<int> PageManager::getPageIds() const
{
    std::shared_lock<std::shared_mutex> lock(cache_mutex);
    return pageIds;
}

PageManager::~PageManager()
{
    std::unique_lock<std::shared_mutex> lock(cache_mutex);
    cache.flushAll();
}
