#include "Table.hpp"
#include "../Storage/StorageFile/StorageFile.hpp"
#include <algorithm>

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
    if (type == "BOOL") {
        return value == "true" || value == "false"
                || value == "1" || value == "0"
                || value == "TRUE" || value == "FALSE";
    }

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

std::expected<void, TableError> Table::insertRow(const Row &row)
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

    // Index first INT column as primary key. A NULL key gets no index entry:
    // WHERE id = x never matches it anyway, and IS NULL always does a full scan.
    if (!row.values.empty() && row.values[0] && !scheme.empty() && scheme[0].type == "INT")
    {
        try
        {
            int key = std::stoi(*row.values[0]);
            index.insert(key, res.pageId, res.rowIndex);
        }
        catch (...)
        {
            return std::unexpected(TableError::INDEX_INSERTION_FAILED);
        }
    }

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
        try
        {
            int key = std::stoi(*cond->value);
            auto record = index.search(key);
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
        catch (const std::exception &e)
        {
            throw; // Repropaga erorile de index sau corupere
        }
        catch (...) {}
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
    const std::vector<std::pair<std::string, Value>> &assignments)
{
    for (const auto &a : assignments)
    {
        int colIndex = getColumnIndex(a.first);
        if (colIndex == -1) return {};
        if (!validateValueForType(a.second, scheme[colIndex].type)) return {};
    }

    std::vector<Row> allRows = selectRow(nullptr);
    bool updatedAny = false;

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
        updatedAny = true;
    }

    if (!updatedAny) return {};
    return allRows;
}

void Table::deleteRow(Condition *cond)
{
    std::vector<Row> toKeep = previewDelete(cond);

    pageManager.clearAll();
    index.clear();
    nextKey = 0;

    for (const auto &row : toKeep)
        insertRow(row);
}

void Table::updateRow(Condition *cond, const std::vector<std::pair<std::string, Value>> &assignmets)
{
    std::vector<Row> newState = previewUpdate(cond, assignmets);
    if (newState.empty())
        return;

    pageManager.clearAll();
    index.clear();
    nextKey = 0;

    for (const auto &row : newState)
        insertRow(row);
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