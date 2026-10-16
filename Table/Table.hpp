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
    DUPLICATE_KEY,
};

class Table {
    public:
        Table(const std::string &name, const std::vector<Columns> &scheme,
              StorageFile &storage, int tableId, int cacheCapacity = 128);

        // Validates row without writing anything: column count, types, and that the key
        // (column 0, if it is INT) parses and is not already in the index
        std::expected<void, TableError> checkRow(const Row &row);
        // enforceUnique = false skips the duplicate check: the clear + reinsert rewrite in
        // deleteRow/updateRow must keep every row it had, including legacy duplicates
        std::expected<void, TableError> insertRow(const Row &row, bool enforceUnique = true);
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

        // The row's key as the index sees it, or nullopt if the table has no key (column 0
        // is not INT; an INT further right never is a key) or the value is NULL. Throws if the value does not fit an int.
        std::optional<int> keyOf(const Row &row) const;

    private:
        std::string name;
        std::vector<Columns> scheme;
        PageManager pageManager;
        BPlusTree index;
        int nextKey = 0;

        int getColumnIndex(const std::string &columnName) const;
        bool validateRowAgainstSchema(const Row &row) const;
        std::expected<void, TableError> checkShape(const Row &row) const;

        void rebuildIndex();
};

static bool evaluateCondition(const Condition *cond, const Row &row, const std::vector<Columns> &schema);

#endif
