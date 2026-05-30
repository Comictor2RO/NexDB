#ifndef STORAGEFILE_HPP
#define STORAGEFILE_HPP

#include "../Page/Page.hpp"
#include <string>
#include <fstream>
#include <vector>
#include <mutex>

class StorageFile {
public:
    StorageFile(const std::string& path);
    ~StorageFile();

    int  allocatePage(int tableId);
    void freePage(int globalPageId);
    void freePagesForTable(int tableId);
    Page readPage(int globalPageId);
    void writePage(int globalPageId, const Page& page);
    std::vector<int> getPagesForTable(int tableId);

private:
    std::string path;
    std::fstream file;
    int totalPages = 0;
    std::vector<int> freeList;
    mutable std::mutex fileMutex;

    void scan();
};

#endif
