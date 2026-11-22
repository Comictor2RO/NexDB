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

        // Validates row without writing anything: column count, types, that the key (if any)
        // fits an int, and that no UNIQUE column repeats a value already in the table
        std::expected<void, TableError> checkRow(const Row &row);
        // validate = false checks only the column count. The clear + reinsert rewrite in
        // deleteRow/updateRow and the WAL replay pass rows that were valid when written, and
        // must keep every one: legacy duplicates, and values a stricter type check now
        // rejects (a FLOAT stored as '.')
        std::expected<void, TableError> insertRow(const Row &row, bool validate = true);
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

        // The row's key as the index sees it, or nullopt if the table has no key or the value
        // is NULL. The key is the first column that is both INT and UNIQUE; it is the only
        // column the B+ tree indexes. Throws if the value does not fit an int.
        std::optional<int> keyOf(const Row &row) const;

        // Position of the key column, or -1 if the table has none
        int keyColumn() const { return keyCol; }

        // Position of the first UNIQUE column in which row repeats a value already stored, or
        // nullopt. NULL never repeats. The key column is looked up in the index, every other
        // UNIQUE column by a scan, comparing like WHERE = does (1.0 = 1, true = 1, text by bytes)
        std::optional<size_t> findDuplicate(const Row &row);

        // The = of WHERE for two non-NULL values of a column type
        static bool valuesEqual(const std::string &type, const std::string &a, const std::string &b);

        // Position of the AUTOINCREMENT column, or -1 if the table has none
        int autoColumn() const { return autoCol; }

        // The value an omitted AUTOINCREMENT would get: one past the highest value the column has
        // ever held, or 1 for a new table. That is the larger of the stored high-water mark and the
        // highest value still in the rows, so a deleted id is never handed out again.
        long long nextAutoValue() const { return maxAuto + 1; }

        // That highest value (the mark or the rows', whichever is larger); 0 if none
        long long autoMax() const { return maxAuto; }

    private:
        std::string name;
        std::vector<Columns> scheme;
        int keyCol = -1;
        int autoCol = -1;
        long long maxAuto = 0;
        PageManager pageManager;
        BPlusTree index;
        int nextKey = 0;

        int getColumnIndex(const std::string &columnName) const;
        bool validateRowAgainstSchema(const Row &row) const;
        std::expected<void, TableError> checkColumnCount(const Row &row) const;
        // Column count, then the type of each value
        std::expected<void, TableError> checkShape(const Row &row) const;

        void rebuildIndex();
        void trackAutoValue(const Row &row);
};

static bool evaluateCondition(const Condition *cond, const Row &row, const std::vector<Columns> &schema);

#endif
