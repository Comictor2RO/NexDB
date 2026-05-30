#ifndef PAGEMANAGER_HPP
#define PAGEMANAGER_HPP

#include "../LRUCache/LRUCache.hpp"
#include "../Page/Page.hpp"
#include "../StorageFile/StorageFile.hpp"
#include <vector>
#include <mutex>
#include <shared_mutex>

class PageManager {
    public:
        PageManager(StorageFile& storage, int tableId, int cacheCapacity = 128);

        struct InsertionResult {
            bool success;
            int pageId;
            int rowIndex;
        };

        InsertionResult insertRowWithLocation(const std::string &row);
        bool insertRow(const std::string &row);
        void clearAll();
        Page readPage(int globalPageId);
        void writePage(int globalPageId, const Page &page);

        std::vector<std::string> getAllRows();
        int getNumberOfPages();
        std::vector<int> getPageIds() const;

        ~PageManager();

    private:
        mutable std::mutex insert_mutex;
        mutable std::shared_mutex cache_mutex;
        StorageFile& storage;
        int tableId;
        std::vector<int> pageIds;
        LRUCache cache;
};

#endif
