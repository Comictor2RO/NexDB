#include "Table.hpp"

#include <algorithm>
#include <bits/locale_facets_nonio.h>

int Table::getColumnIndex(const std::string &columnName) const
{
    for (int i = 0; i < (int)scheme.size(); i++)
    {
        if (scheme[i].name == columnName)
            return i;
    }
    return -1;
}
bool Table::validateValueForType(const std::string &value, const std::string &type) const {
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

    if (type == "STRING" || type == "TEXT")
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

    const std::string &rowValue = row.values[colIndex];

    if (cond->op == "=")  return rowValue == cond->value;
    if (cond->op == "!=") return rowValue != cond->value;
    if (cond->op == ">")  return std::stoi(rowValue) > std::stoi(cond->value);
    if (cond->op == "<")  return std::stoi(rowValue) < std::stoi(cond->value);
    if (cond->op == ">=") return std::stoi(rowValue) >= std::stoi(cond->value);
    if (cond->op == "<=") return std::stoi(rowValue) <= std::stoi(cond->value);
    return false;
}

Table::Table(const std::string &name, const std::vector<Columns> &schema)
    : name(name), scheme(schema), pageManager(name + ".db"), index(4), nextKey(0)
{
    rebuildIndex();
}

bool Table::insertRow(const Row &row)
{
    if (!validateRowAgainstSchema(row))
        return false;

    std::string serialized;
    for (int i = 0; i < (int)row.values.size(); i++)
    {
        serialized += row.values[i];
        if (i + 1 < (int)row.values.size())
            serialized += "|";
    }

    PageManager::InsertionResult res = pageManager.insertRowWithLocation(serialized);
    if (!res.success)
        return false;

    // Indexam primul camp de tip INT ca si cheie
    if (!row.values.empty() && !scheme.empty() && scheme[0].type == "INT")
    {
        try
        {
            int key = std::stoi(row.values[0]);
            index.insert(key, res.pageId, res.rowIndex);
        }
        catch (...) {}
    }

    nextKey++;
    return true;
}

std::vector<Row> Table::selectRow(Condition *cond)
{
    // Cautare rapida cu indexul: doar pentru WHERE col = valoare pe prima coloana INT
    if (cond != nullptr && cond->op == "=" &&
        !scheme.empty() && cond->column == scheme[0].name &&
        scheme[0].type == "INT")
    {
        try
        {
            int key = std::stoi(cond->value);
            IndexRecord *record = index.search(key);
            if (record != nullptr)
            {
                Page page = pageManager.readPage(record->pageId);
                std::vector<std::string> pageRows = page.getRows();
                int localIndex = record->offset;
                if (localIndex >= 0 && localIndex < (int)pageRows.size())
                {
                    Row row;
                    row.values = split(pageRows[localIndex], '|');
                    return { row };
                }
                else
                {
                    throw std::runtime_error("Index record corrupted: row " + std::to_string(localIndex) + " not found in page " + std::to_string(record->pageId));
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
        row.values = split(raw, '|');
        if (cond == nullptr || evaluateCondition(cond, row, scheme))
            result.push_back(row);
    }

    return result;
}

void Table::deleteRow(Condition *cond)
{
    std::vector<Row> allRows = selectRow(nullptr);
    std::vector<Row> toKeep;

    for (const auto &row : allRows)
    {
        if (cond == nullptr)
        {
            // delete all rows -> keep none
            continue;
        }

        if (!evaluateCondition(cond, row, scheme))
            toKeep.push_back(row);
    }

    pageManager.clearAll();

    index.clear();
    nextKey = 0;

    for (const auto &row : toKeep)
        insertRow(row);
}

void Table::updateRow(Condition *cond, const std::vector<std::pair<std::string, std::string>> &assignmets)
{
    auto validateAssignments = [this](const std::vector<std::pair<std::string, std::string>> &assignments) -> bool
    {
        for (const auto &assignment : assignments)
        {
            int colIndex = getColumnIndex(assignment.first);
            if (colIndex == -1)
                return false;

            if (!validateValueForType(assignment.second, scheme[colIndex].type))
                return false;
        }
        return true;
    };

    if (!validateAssignments(assignmets))
        return;

    std::vector<Row> allRows = selectRow(nullptr);
    bool updatedAny = false;

    for (auto &row : allRows)
    {
        if (cond != nullptr && !evaluateCondition(cond, row, scheme))
            continue;

        for (const auto &assignment : assignmets)
        {
            int colIndex = getColumnIndex(assignment.first);
            if (colIndex == -1)
                return;

            if (colIndex >= static_cast<int>(row.values.size()))
                return;

            row.values[colIndex] = assignment.second;
        }

        updatedAny = true;
    }

    if (!updatedAny)
        return;

    pageManager.clearAll();
    index.clear();
    nextKey = 0;

    for (const auto &row : allRows)
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
    int totalPages = pageManager.getNumberOfPages();
    int globalRowCounter = 0;

    for (int p = 0; p < totalPages; p++)
    {
        Page page = pageManager.readPage(p);
        std::vector<std::string> rows = page.getRows();
        for (int r = 0; r < (int)rows.size(); r++)
        {
            Row row;
            row.values = split(rows[r], '|');
            if (!row.values.empty() && !scheme.empty() && scheme[0].type == "INT")
            {
                try
                {
                    int key = std::stoi(row.values[0]);
                    index.insert(key, p, r);
                }
                catch (...) {}
            }
            globalRowCounter++;
        }
    }
    nextKey = globalRowCounter;
}