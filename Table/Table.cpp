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

        bool hasDot = false;
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
        }
        return true;
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

    const std::string &left = *rowValue;
    const std::string &right = *cond->value;
    const std::string &type = scheme[colIndex].type;

    // = and != follow the column type, like < and > already did for numbers. For a number
    // column = holds exactly when neither < nor > does, so 9.50 = 9.5 and 007 = 7
    if (type == "INT" || type == "FLOAT")
    {
        try {
            double l = std::stod(left), r = std::stod(right);
            if (cond->op == "=")  return l == r;
            if (cond->op == "!=") return l != r;
            if (cond->op == ">")  return l > r;
            if (cond->op == "<")  return l < r;
            if (cond->op == ">=") return l >= r;
            if (cond->op == "<=") return l <= r;
        } catch (...) { return false; }
        return false;
    }

    // TRUE, true and 1 are the same BOOL value
    if (type == "BOOL" && (cond->op == "=" || cond->op == "!="))
    {
        std::optional<bool> l = boolValue(left), r = boolValue(right);
        if (!l || !r)
            return false;
        return cond->op == "=" ? *l == *r : *l != *r;
    }

    if (cond->op == "=")  return left == right;
    if (cond->op == "!=") return left != right;
    try {
        if (cond->op == ">")  return std::stod(left) > std::stod(right);
        if (cond->op == "<")  return std::stod(left) < std::stod(right);
        if (cond->op == ">=") return std::stod(left) >= std::stod(right);
        if (cond->op == "<=") return std::stod(left) <= std::stod(right);
    } catch (...) { return false; }
    return false;
}

Table::Table(const std::string &name, const std::vector<Columns> &schema,
             StorageFile &storage, int tableId, int cacheCapacity)
    : name(name), scheme(schema), pageManager(storage, tableId, cacheCapacity), index(4), nextKey(0)
{
    rebuildIndex();
}

std::optional<int> Table::keyOf(const Row &row) const
{
    if (scheme.empty() || scheme[0].type != "INT" || row.values.empty() || !row.values[0])
        return std::nullopt;
    return std::stoi(*row.values[0]);
}

std::expected<void, TableError> Table::checkShape(const Row &row) const
{
    // Validate schema: check column count
    if (row.values.size() != scheme.size())
        return std::unexpected(TableError::SCHEME_MISMATCH);

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

    // The key is compared as an int, like the index does, so 007 and 7 collide.
    // NULL keys are not indexed and never collide.
    std::optional<int> key;
    try
    {
        key = keyOf(row);
    }
    catch (...)
    {
        // Passes the digit check but overflows int, e.g. 99999999999
        return std::unexpected(TableError::INDEX_INSERTION_FAILED);
    }
    if (key && index.search(*key).has_value())
        return std::unexpected(TableError::DUPLICATE_KEY);
    return {};
}

std::expected<void, TableError> Table::insertRow(const Row &row, bool enforceUnique)
{
    // Validate before writing: a rejected row used to be stored anyway when the key
    // overflowed int, since the index was only touched after the page write
    auto valid = enforceUnique ? checkRow(row) : checkShape(row);
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
    // Without enforceUnique a legacy key that overflows int is kept unindexed, like
    // rebuildIndex does, so the rewrite does not drop it.
    try
    {
        if (auto key = keyOf(row))
            index.insert(*key, res.pageId, res.rowIndex);
    }
    catch (...) {}

    nextKey++;
    return {};
}

std::vector<Row> Table::selectRow(Condition *cond)
{
    // Cautare rapida cu indexul: doar pentru WHERE col = valoare pe prima coloana INT
    if (cond != nullptr && cond->op == "=" &&
        !scheme.empty() && cond->column == scheme[0].name &&
        scheme[0].type == "INT")
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

void Table::rebuildIndex()
{
    index.clear();
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
            if (!row.values.empty() && row.values[0] && !scheme.empty() && scheme[0].type == "INT")
            {
                try
                {
                    int key = std::stoi(*row.values[0]);
                    index.insert(key, gid, r);
                }
                catch (...) {}
            }
            globalRowCounter++;
        }
    }
    nextKey = globalRowCounter;
}