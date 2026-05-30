#ifndef LRUCACHE_HPP
#define LRUCACHE_HPP

#include "../Page/Page.hpp"
#include <list>
#include <unordered_map>
#include <functional>

struct CacheEntry {
    Page page;
    bool isDirty;
};

class LRUCache {
    public:
        using FlushFn = std::function<void(int, const Page&)>;
        LRUCache(int capacity, FlushFn flushFn);

        Page* get(int pageId);
        void put(int pageId, const Page &page, bool isDirty);
        void clear();
        void flushAll();

    private:
        int capacity;
        FlushFn flushFn;
        std::list<std::pair<int, CacheEntry>> lruList;
        std::unordered_map<int, std::list<std::pair<int, CacheEntry>>::iterator> cacheMap;

        void evict();
};

#endif