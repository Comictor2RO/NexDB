#include "LRUCache.hpp"

LRUCache::LRUCache(int capacity, FlushFn flushFn)
    : capacity(capacity), flushFn(std::move(flushFn))
{}

Page *LRUCache::get(int pageId)
{
    auto it = cacheMap.find(pageId);
    if (it == cacheMap.end())
        return nullptr;
    lruList.splice(lruList.begin(), lruList, it->second);
    it->second = lruList.begin();

    return &it->second->second.page;
}

void LRUCache::put(int pageId,const Page &page, bool isDirty)
{
    auto it = cacheMap.find(pageId);
    if (it == cacheMap.end())
    {
        if ((int)lruList.size() >= capacity)
            evict();
        lruList.push_front({pageId, CacheEntry{page, isDirty}});
        cacheMap[pageId] = lruList.begin();
    }
    else
    {
        it->second->second.page = page;
        it->second->second.isDirty = isDirty;
        lruList.splice(lruList.begin(), lruList, it->second);
        it->second = lruList.begin();
    }
}

void LRUCache::clear()
{
    lruList.clear();
    cacheMap.clear();
}

void LRUCache::flushAll()
{
    while (!lruList.empty())
        evict();
}

void LRUCache::evict()
{
    auto &last = lruList.back();
    int pageId = last.first;
    if (last.second.isDirty)
        flushFn(pageId, last.second.page);
    cacheMap.erase(pageId);
    lruList.pop_back();
}