#ifndef CATALOG_HPP
#define CATALOG_HPP

#include <map>
#include <string>
#include <vector>
#include <shared_mutex>
#include "../AST/Columns/Columns.hpp"

class Catalog {
    public:
        explicit Catalog(const std::string& catalogPath = "catalog.dat");

        //Methods
        // Returns false, and changes nothing, if the name is taken or columns is empty.
        // Check and insert happen under one lock, so callers can rely on the result.
        bool createTable(const std::string &name, const std::vector<Columns> &columns);
        bool tableExists(const std::string &name) const;
        void dropTable(const std::string &name);

        //Getter
        std::vector<Columns> getColumns(const std::string &name) const;
        int getTableId(const std::string &name) const;

    private:
        mutable std::shared_mutex mutex;
        std::map<std::string, std::vector<Columns>> columns;
        std::map<std::string, int> tableIds;
        int nextTableId = 1;
        std::string catalogFile;

        void load();
        void save();
};


#endif
