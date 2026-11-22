#include "Table.hpp"
#include "../Storage/StorageFile/StorageFile.hpp"
#include <algorithm>
#include <climits>
#include <cmath>
#include <optional>

// The logical value of a BOOL literal, or nullopt if it is not one. The single list of
// accepted spellings: validation and comparison both go through here
static std::optional<bool> boolValue(const std::string &value)
{
    if (value == "true" || value == "TRUE" || value == "1")
        return true;
    if (value == "false" || value == "FALSE" || value == "0")
        return false;
    return std::nullopt;
}

// Applies op to two values of the same type; false for an unknown op
template <typename T>
static bool applyOperator(const std::string &op, const T &l, const T &r)
{
    if (op == "=")  return l == r;
    if (op == "!=") return l != r;
    if (op == ">")  return l > r;
    if (op == "<")  return l < r;
    if (op == ">=") return l >= r;
    if (op == "<=") return l <= r;
    return false;
}

int Table::getColumnIndex(const std::string &columnName) const
{
    for (int i = 0; i < (int)scheme.size(); i++)
    {
        if (scheme[i].name == columnName)
            return i;
    }
    return -1;
}
// value is the raw text a user typed; NULL is valid for every type
bool Table::validateValueForType(const Value &maybeValue, const std::string &type) const {
    if (!maybeValue)
        return true;
    const std::string &value = *maybeValue;

    if (type == "INT")
    {
        if (value.empty())
            return false;

        int start = 0;
        if (value[0] == '-' || value[0] == '+')
            start = 1;

        if (start == static_cast<int>(value.size()))
            return false;

        for (int i = start; i < static_cast<int>(value.size()); i++)
        {
            if (!std::isdigit(static_cast<unsigned char>(value[i])))
                return false;
        }
        return true;
    }

    if (type == "FLOAT") {
        if (value.empty())
            return false;

        int start = 0;
        if (value[0] == '-' || value[0] == '+')
            start = 1;

        if (start == static_cast<int>(value.size()))
            return false;

        bool hasDot = false, hasDigit = false;
        for (int i = start; i < static_cast<int>(value.size()); i++)
        {
            if (value[i] == '.')
            {
                if (hasDot)
                        return false;
                hasDot = true;
            }
            else if (!std::isdigit((unsigned char) value[i])) {
                return false;
            }
            else {
                hasDigit = true;
            }
        }
        // .5 and 5. are numbers; '.' and '-.' alone used to pass too
        return hasDigit;
    }

    // Only the four canonical names reach here: the parser maps BOOLEAN / TEXT
    // to BOOL / STRING before the schema is stored.
    if (type == "BOOL")
        return boolValue(value).has_value();

    if (type == "STRING")
        return true;
    return false;
}
bool Table::validateRowAgainstSchema(const Row &row) const
{
    if (row.values.size() != scheme.size())
        return false;

    for (int i = 0; i < static_cast<int>(row.values.size()); i++)
    {
        if (!validateValueForType(row.values[i], scheme[i].type))
            return false;
    }

    return true;
}

// Compares two non-NULL values under a column type. Every operator follows the column type.
// For a number column = holds exactly when neither < nor > does, so 9.50 = 9.5 and 007 = 7.
// STRING is ordered by bytes: case matters ('B' < 'a'), '10' < '9', and UTF-8 text sorts by
// code point. WHERE and UNIQUE both go through here, so they can never disagree.
static bool compareValues(const std::string &op, const std::string &type,
                          const std::string &left, const std::string &right)
{
    if (type == "INT" || type == "FLOAT")
    {
        try { return applyOperator(op, std::stod(left), std::stod(right)); }
        catch (...) { return false; }
    }

    // TRUE, true and 1 are the same BOOL value, and false < true
    if (type == "BOOL")
    {
        std::optional<bool> l = boolValue(left), r = boolValue(right);
        return l && r && applyOperator(op, *l, *r);
    }

    return applyOperator(op, left, right);
}

static bool evaluateCondition(const Condition *cond, const Row &row, const std::vector<Columns> &scheme)
{
    //Finds the column index in scheme
    int colIndex = -1;
    for (int i = 0; i < (int)scheme.size(); i++)
    {
        if (scheme[i].name == cond->column)
        {
            colIndex = i;
            break;
        }
    }
    if (colIndex == -1)
        return false;

    if (row.values.empty() || colIndex >= (int)row.values.size())
        return false;

    const Value &rowValue = row.values[colIndex];

    if (cond->op == "IS NULL")     return !rowValue.has_value();
    if (cond->op == "IS NOT NULL") return rowValue.has_value();

    // Any comparison involving NULL is unknown in SQL, so it never matches,
    // including != (NULL != 5 is not true)
    if (!rowValue || !cond->value)
        return false;

    return compareValues(cond->op, scheme[colIndex].type, *rowValue, *cond->value);
}

bool Table::valuesEqual(const std::string &type, const std::string &a, const std::string &b)
{
    return compareValues("=", type, a, b);
}

Table::Table(const std::string &name, const std::vector<Columns> &schema,
             StorageFile &storage, int tableId, int cacheCapacity)
    : name(name), scheme(schema), pageManager(storage, tableId, cacheCapacity), index(4), nextKey(0)
{
    // The key is the first UNIQUE INT column; autoCol is the AUTOINCREMENT one (INT, at most one)
    for (size_t i = 0; i < scheme.size(); i++) {
        if (keyCol < 0 && scheme[i].type == "INT" && scheme[i].unique)
            keyCol = static_cast<int>(i);
        if (autoCol < 0 && scheme[i].auto_increment)
            autoCol = static_cast<int>(i);
    }
    rebuildIndex();
}

std::optional<int> Table::keyOf(const Row &row) const
{
    if (keyCol < 0 || keyCol >= row.values.size() || !row.values[keyCol])
        return std::nullopt;
    return std::stoi(*row.values[keyCol]);
}

std::expected<void, TableError> Table::checkColumnCount(const Row &row) const
{
    if (row.values.size() != scheme.size())
        return std::unexpected(TableError::SCHEME_MISMATCH);
    return {};
}

std::expected<void, TableError> Table::checkShape(const Row &row) const
{
    if (auto count = checkColumnCount(row); !count)
        return count;

    // Validate schema: check types for each column
    for (int i = 0; i < static_cast<int>(row.values.size()); i++)
    {
        if (!validateValueForType(row.values[i], scheme[i].type))
            return std::unexpected(TableError::TYPE_VALIDATION_FAILED);
    }
    return {};
}

std::expected<void, TableError> Table::checkRow(const Row &row)
{
    if (auto shape = checkShape(row); !shape)
        return shape;

    try
    {
        keyOf(row);
    }
    catch (...)
    {
        // Passes the digit check but overflows int, e.g. 99999999999
        return std::unexpected(TableError::INDEX_INSERTION_FAILED);
    }
    if (findDuplicate(row))
        return std::unexpected(TableError::DUPLICATE_KEY);
    return {};
}

std::optional<size_t> Table::findDuplicate(const Row &row)
{
    // Rows are read once, and only if a UNIQUE column other than the key needs a scan
    std::optional<std::vector<Row>> stored;

    for (size_t i = 0; i < scheme.size() && i < row.values.size(); i++)
    {
        // NULL is not part of uniqueness: any number of rows may hold it
        if (!scheme[i].unique || !row.values[i])
            continue;

        if (static_cast<int>(i) == keyCol)
        {
            // The key is compared as an int, like the index does, so 007 and 7 collide
            try
            {
                if (auto key = keyOf(row); key && index.search(*key).has_value())
                    return i;
            }
            catch (...) {} // overflows int: not indexed, checkRow reports it
            continue;
        }

        if (!stored)
            stored = selectRow(nullptr);
        for (const Row &other : *stored)
        {
            if (i < other.values.size() && other.values[i]
                && valuesEqual(scheme[i].type, *other.values[i], *row.values[i]))
                return i;
        }
    }
    return std::nullopt;
}

std::expected<void, TableError> Table::insertRow(const Row &row, bool validate)
{
    // Validate before writing: a rejected row used to be stored anyway when the key
    // overflowed int, since the index was only touched after the page write
    auto valid = validate ? checkRow(row) : checkColumnCount(row);
    if (!valid)
        return valid;

    // Serialize row data
    std::string serialized;
    for (int i = 0; i < (int)row.values.size(); i++)
    {
        serialized += encodeValue(row.values[i]);
        if (i + 1 < (int)row.values.size())
            serialized += "|";
    }

    // Insert into page manager
    PageManager::InsertionResult res = pageManager.insertRowWithLocation(serialized);
    if (!res.success)
        return std::unexpected(TableError::PAGE_MANAGER_FULL);

    // Index column 0, if it is INT, as primary key. A NULL key gets no index entry:
    // WHERE id = x never matches it anyway, and IS NULL always does a full scan.
    // Without validate a legacy key that overflows int is kept unindexed, like
    // rebuildIndex does, so the rewrite does not drop it.
    try
    {
        if (auto key = keyOf(row))
            index.insert(*key, res.pageId, res.rowIndex);
    }
    catch (...) {}

    trackAutoValue(row);
    nextKey++;
    return {};
}

std::vector<Row> Table::selectRow(Condition *cond)
{
    // Cautare rapida cu indexul: doar pentru WHERE col = valoare pe prima coloana INT
    if (cond != nullptr && cond->op == "=" &&
        keyCol >= 0 && cond->column == scheme[keyCol].name)
    {
        // id = NULL matches nothing
        if (!cond->value)
            return {};

        // The literal is read as a number, like the full scan reads it, so 5.0 finds key 5.
        // stoi used to turn 5.5 into 5 and to leak its own error for 'abc' or 99999999999.
        // Anything that is not an exact int falls through to the scan, which answers it
        // numerically: nothing for 5.5, a legacy key that overflows int (and is not indexed)
        std::optional<int> key;
        try
        {
            double number = std::stod(*cond->value);
            if (number == std::floor(number) && number >= INT_MIN && number <= INT_MAX)
                key = static_cast<int>(number);
        }
        catch (...) {}

        if (key)
        {
            auto record = index.search(*key);
            if (record.has_value())
            {
                Page page = pageManager.readPage(record.value().pageId);
                std::vector<std::string> pageRows = page.getRows();
                int localIndex = record.value().offset;
                if (localIndex >= 0 && localIndex < (int)pageRows.size())
                {
                    Row row;
                    for (const auto &field : splitFields(pageRows[localIndex], '|'))
                        row.values.push_back(decodeValue(field));
                    return { row };
                }
                else
                {
                    throw std::runtime_error("Index record corrupted: row " + std::to_string(localIndex) + " not found in page " + std::to_string(record.value().pageId));
                }
            }
            return {};
        }
    }

    // Full scan pentru orice alta conditie
    std::vector<std::string> rawRows = pageManager.getAllRows();
    std::vector<Row> result;

    for (const auto &raw : rawRows)
    {
        Row row;
        for (const auto &field : splitFields(raw, '|'))
            row.values.push_back(decodeValue(field));
        if (cond == nullptr || evaluateCondition(cond, row, scheme))
            result.push_back(row);
    }

    return result;
}

std::vector<Row> Table::previewDelete(Condition *cond)
{
    if (cond == nullptr)
        return {}; // delete all → keep none

    std::vector<Row> allRows = selectRow(nullptr);
    std::vector<Row> toKeep;
    for (const auto &row : allRows)
        if (!evaluateCondition(cond, row, scheme))
            toKeep.push_back(row);
    return toKeep;
}

std::vector<Row> Table::previewUpdate(Condition *cond,
    const std::vector<std::pair<std::string, Value>> &assignments, size_t *matched)
{
    if (matched)
        *matched = 0;

    for (const auto &a : assignments)
    {
        int colIndex = getColumnIndex(a.first);
        if (colIndex == -1) return {};
        if (!validateValueForType(a.second, scheme[colIndex].type)) return {};
    }

    std::vector<Row> allRows = selectRow(nullptr);
    size_t updatedCount = 0;

    for (auto &row : allRows)
    {
        if (cond != nullptr && !evaluateCondition(cond, row, scheme))
            continue;

        for (const auto &a : assignments)
        {
            int colIndex = getColumnIndex(a.first);
            if (colIndex < 0 || colIndex >= static_cast<int>(row.values.size()))
                return {};
            row.values[colIndex] = a.second;
        }
        updatedCount++;
    }

    if (matched)
        *matched = updatedCount;
    if (updatedCount == 0) return {};
    return allRows;
}

size_t Table::deleteRow(Condition *cond)
{
    size_t before = pageManager.getAllRows().size();
    std::vector<Row> toKeep = previewDelete(cond);

    pageManager.clearAll();
    index.clear();
    nextKey = 0;

    for (const auto &row : toKeep)
        insertRow(row, false);
    return before - toKeep.size();
}

size_t Table::updateRow(Condition *cond, const std::vector<std::pair<std::string, Value>> &assignmets)
{
    size_t matched = 0;
    std::vector<Row> newState = previewUpdate(cond, assignmets, &matched);
    if (newState.empty())
        return 0;

    pageManager.clearAll();
    index.clear();
    nextKey = 0;

    for (const auto &row : newState)
        insertRow(row, false);
    return matched;
}

void Table::dropStorage()
{
    pageManager.clearAll();
    index.clear();
    nextKey = 0;
}

// Keeps maxAuto at the highest AUTOINCREMENT value seen. A value that is not a number that
// fits a long long (a legacy row) is ignored.
void Table::trackAutoValue(const Row &row)
{
    if (autoCol < 0 || autoCol >= static_cast<int>(row.values.size()) || !row.values[autoCol])
        return;
    try { maxAuto = std::max(maxAuto, std::stoll(*row.values[autoCol])); }
    catch (...) {}
}

void Table::rebuildIndex()
{
    index.clear();
    maxAuto = 0;
    std::vector<int> pageIds = pageManager.getPageIds();
    int globalRowCounter = 0;

    for (int gid : pageIds)
    {
        Page page = pageManager.readPage(gid);
        std::vector<std::string> rows = page.getRows();
        for (int r = 0; r < (int)rows.size(); r++)
        {
            Row row;
            for (const auto &field : splitFields(rows[r], '|'))
                row.values.push_back(decodeValue(field));
            try {
                if (auto key = keyOf(row)) {
                    index.insert(*key, gid, r);
                }
            }
            catch (...) {}
            trackAutoValue(row);
            globalRowCounter++;
        }
    }
    nextKey = globalRowCounter;
}