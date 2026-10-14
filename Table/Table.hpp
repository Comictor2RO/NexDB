#ifndef TABLE_HPP
#define TABLE_HPP

#include "../AST/Condition/Condition.hpp"
#include "../Storage/PageManager/PageManager.hpp"
#include "../Storage/StorageFile/StorageFile.hpp"
#include "../AST/Columns/Columns.hpp"
#include "../AST/Row/Row.hpp"
#include "../Indexing/BPlusTree/BPlusTree.hpp"
#include "../StringUtils/StringUtils.hpp"
#include <expected>

enum class TableError {
    SUCCESS,
    SCHEME_MISMATCH,
    TYPE_VALIDATION_FAILED,
    PAGE_MANAGER_FULL,
    INDEX_INSERTION_FAILED,
};

class Table {
    public:
        Table(const std::string &name, const std::vector<Columns> &scheme,
              StorageFile &storage, int tableId, int cacheCapacity = 128);

        std::expected<void, TableError> insertRow(const Row &row);
        std::vector<Row> selectRow(Condition *cond);
        // Both return how many rows were deleted / matched the condition
        size_t deleteRow(Condition *cond);
        void dropStorage();
        size_t updateRow(Condition *cond, const std::vector<std::pair<std::string,Value>> &assignemets);

        // Returns the rows that would remain after the operation, without touching storage.
        // For previewUpdate: returns empty if validation fails or no rows match.
        // matched, if given, receives the number of rows the condition selected.
        std::vector<Row> previewDelete(Condition *cond);
        std::vector<Row> previewUpdate(Condition *cond, const std::vector<std::pair<std::string,Value>> &assignments,
                                       size_t *matched = nullptr);

        // True if value is acceptable for a column of this type (NULL always is)
        bool validateValueForType(const Value &value, const std::string &type) const;

    private:
        std::string name;
        std::vector<Columns> scheme;
        PageManager pageManager;
        BPlusTree index;
        int nextKey = 0;

        int getColumnIndex(const std::string &columnName) const;
        bool validateRowAgainstSchema(const Row &row) const;

        void rebuildIndex();
};

static bool evaluateCondition(const Condition *cond, const Row &row, const std::vector<Columns> &schema);

#endif
