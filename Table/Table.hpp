#ifndef TABLE_HPP
#define TABLE_HPP

#include "Condition.hpp"
#include "../Storage/PageManager/PageManager.hpp"
#include "../AST/Columns/Columns.hpp"
#include "../AST/Row/Row.hpp"
#include "../Indexing/BPlusTree/BPlusTree.hpp"
#include "../StringUtils/StringUtils.hpp"

class Table {
    public:
        Table(const std::string &name, const std::vector<Columns> &scheme);

        bool insertRow(const Row &row);
        std::vector<Row> selectRow(Condition *cond);
        void deleteRow(Condition *cond);
        void dropStorage();
        void updateRow(Condition *cond, const std::vector<std::pair<std::string,std::string>> &assignemets);

    private:
        std::string name;
        std::vector<Columns> scheme;
        PageManager pageManager;
        BPlusTree index;
        int nextKey = 0;

        int getColumnIndex(const std::string &columnName) const;
        bool validateValueForType(const std::string &value, const std::string &type) const;
        bool validateRowAgainstSchema(const Row &row) const;

        void rebuildIndex();
};

static bool evaluateCondition(const Condition *cond, const Row &row, const std::vector<Columns> &schema);

#endif