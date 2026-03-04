#ifndef LRUCACHE_HPP
#define LRUCACHE_HPP

#include "../Page/Page.hpp"
#include <list>
#include <unordered_map>
#include <fstream>

struct CacheEntry {
    Page page;
    bool isDirty;
};

class LRUCache {
    public:
        LRUCache(int capacity, std::fstream &file);

        Page* get(int pageId);
        void put(int pageId, const Page &page, bool isDirty);
        void clear();
        void flushAll();

    private:
        int capacity;
        std::fstream &file;
        std::list<std::pair<int, CacheEntry>> lruList;
        std::unordered_map<int, std::list<std::pair<int, CacheEntry>>::iterator> cacheMap;

        void evict();
};

#endif