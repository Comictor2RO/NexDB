#include "Table.hpp"

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

void Table::insertRow(const Row &row)
{
    std::string serialized;
    for (int i = 0; i < (int)row.values.size(); i++)
    {
        serialized += row.values[i];
        if (i + 1 < (int)row.values.size())
            serialized += "|";
    }
    pageManager.insertRow(serialized);

    // Indexam primul camp de tip INT ca si cheie
    if (!row.values.empty() && !scheme.empty() && scheme[0].type == "INT")
    {
        try
        {
            int key = std::stoi(row.values[0]);
            index.insert(key, 0, nextKey);
        }
        catch (...) {}
    }
    nextKey++;
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
                std::vector<std::string> allRows = pageManager.getAllRows();
                int globalIndex = record->offset;
                if (globalIndex >= 0 && globalIndex < (int)allRows.size())
                {
                    Row row;
                    row.values = split(allRows[globalIndex], '|');
                    return { row };
                }
            }
            return {};
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
            ;
        if (cond != nullptr && evaluateCondition(cond, row, scheme))
            toKeep.push_back(row);
    }

    pageManager.clearAll();

    // Resetam indexul si contorul inainte de rescriere
    index.clear();
    nextKey = 0;

    for (const auto &row : toKeep)
        insertRow(row);
}

void Table::rebuildIndex()
{
    std::vector<std::string> rawRows = pageManager.getAllRows();
    nextKey = (int)rawRows.size();

    if (scheme.empty() || scheme[0].type != "INT")
        return;

    for (int i = 0; i < (int)rawRows.size(); i++)
    {
        Row row;
        row.values = split(rawRows[i], '|');
        if (!row.values.empty())
        {
            try
            {
                int key = std::stoi(row.values[0]);
                index.insert(key, 0, i);
            }
            catch (...) {}
        }
    }
}