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
        bool insertRow(const std::string &row);
        void clearAll();

        //Getter
        std::vector<std::string> getAllRows();

        //Destructor
        ~PageManager();
        
    private:
        std::string filename;
        std::fstream file;
        int numberOfPages;
        LRUCache cache;

        Page readPage(int pageId);
        void writePage(int pageId, const Page &page);
};


#endif