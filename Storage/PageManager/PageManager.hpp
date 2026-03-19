#ifndef PAGEMANAGER_HPP
#define PAGEMANAGER_HPP

#include "../LRUCache/LRUCache.hpp"
#include "../Page/Page.hpp"
#include <string>
#include <iostream>
#include <fstream>
#include <vector>

class PageManager {
    public:

        //Constructor
        PageManager(std::string filename);

        //Method
    struct InsertionResult {
        bool success;
        int pageId;
        int rowIndex;
    };

    InsertionResult insertRowWithLocation(const std::string &row);
    bool insertRow(const std::string &row);
        void clearAll();
        Page readPage(int pageId);
        void writePage(int pageId, const Page &page);

        //Getter
        std::vector<std::string> getAllRows();
        int getNumberOfPages();

        //Destructor
        ~PageManager();
        
    private:
        std::string filename;
        std::fstream file;
        int numberOfPages;
        LRUCache cache;
};


#endif