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
        void createTable(const std::string &name, const std::vector<Columns> &columns);
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
