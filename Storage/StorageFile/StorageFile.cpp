#include "StorageFile.hpp"
#include <filesystem>
#include <stdexcept>
#include <cstring>

StorageFile::StorageFile(const std::string& path) : path(path)
{
    auto parent = std::filesystem::path(path).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);

    file.open(path, std::ios::in | std::ios::out | std::ios::binary);
    if (!file.is_open()) {
        file.open(path, std::ios::out | std::ios::binary);
        file.close();
        file.open(path, std::ios::in | std::ios::out | std::ios::binary);
    }
    if (!file.is_open())
        throw std::runtime_error("Failed to open or create database file: " + path);

    file.seekg(0, std::ios::end);
    totalPages = (int)(file.tellg() / PAGE_SIZE);

    scan();
}

StorageFile::~StorageFile()
{
    if (file.is_open()) file.close();
}

void StorageFile::scan()
{
    freeList.clear();
    for (int i = 0; i < totalPages; i++) {
        file.clear();
        file.seekg(i * PAGE_SIZE);
        char header[sizeof(PageHeader)];
        file.read(header, sizeof(PageHeader));
        if (file.gcount() == (std::streamsize)sizeof(PageHeader)) {
            const PageHeader* h = (const PageHeader*)header;
            if (h->tableId == 0)
                freeList.push_back(i);
        }
    }
}

int StorageFile::allocatePage(int tableId)
{
    std::lock_guard<std::mutex> lock(fileMutex);

    int globalPageId;
    if (!freeList.empty()) {
        globalPageId = freeList.back();
        freeList.pop_back();
    } else {
        globalPageId = totalPages++;
    }

    Page p(globalPageId, tableId);
    file.clear();
    file.seekp(globalPageId * PAGE_SIZE, std::ios::beg);
    file.write(p.getBuffer(), PAGE_SIZE);
    file.flush();

    return globalPageId;
}

void StorageFile::freePage(int globalPageId)
{
    std::lock_guard<std::mutex> lock(fileMutex);

    Page blank;
    file.clear();
    file.seekp(globalPageId * PAGE_SIZE, std::ios::beg);
    file.write(blank.getBuffer(), PAGE_SIZE);
    file.flush();

    freeList.push_back(globalPageId);
}

void StorageFile::freePagesForTable(int tableId)
{
    std::vector<int> toFree;
    {
        std::lock_guard<std::mutex> lock(fileMutex);
        for (int i = 0; i < totalPages; i++) {
            file.clear();
            file.seekg(i * PAGE_SIZE);
            char header[sizeof(PageHeader)];
            file.read(header, sizeof(PageHeader));
            if (file.gcount() == (std::streamsize)sizeof(PageHeader)) {
                const PageHeader* h = (const PageHeader*)header;
                if (h->tableId == tableId)
                    toFree.push_back(i);
            }
        }
    }
    for (int id : toFree)
        freePage(id);
}

Page StorageFile::readPage(int globalPageId)
{
    std::lock_guard<std::mutex> lock(fileMutex);
    file.clear();
    file.seekg(globalPageId * PAGE_SIZE);
    Page page;
    file.read(page.getBuffer(), PAGE_SIZE);
    return page;
}

void StorageFile::writePage(int globalPageId, const Page& page)
{
    std::lock_guard<std::mutex> lock(fileMutex);
    file.clear();
    file.seekp(globalPageId * PAGE_SIZE, std::ios::beg);
    file.write(page.getBuffer(), PAGE_SIZE);
    if (file.fail()) file.clear();
    file.flush();
}

std::vector<int> StorageFile::getPagesForTable(int tableId)
{
    std::lock_guard<std::mutex> lock(fileMutex);
    std::vector<int> result;
    for (int i = 0; i < totalPages; i++) {
        file.clear();
        file.seekg(i * PAGE_SIZE);
        char header[sizeof(PageHeader)];
        file.read(header, sizeof(PageHeader));
        if (file.gcount() == (std::streamsize)sizeof(PageHeader)) {
            const PageHeader* h = (const PageHeader*)header;
            if (h->tableId == tableId)
                result.push_back(i);
        }
    }
    return result;
}
