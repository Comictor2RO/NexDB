#include "Engine.hpp"

#include <algorithm>
#include <stdexcept>

Engine::Engine(Catalog &catalog, int cacheCapacity) : catalog(catalog), wal("engine.wal"), cacheCapacity(cacheCapacity)
{
    wal.recover(*this);
}

void Engine::execute(Statement *statement)
{
    if (statement == NULL)
        return;
    if (InsertStatement *stmt = dynamic_cast<InsertStatement *>(statement))
        executeInsert(*stmt);
    else if (DeleteStatement *stmt = dynamic_cast<DeleteStatement *>(statement))
        executeDelete(*stmt);
    else if (SelectStatement *stmt = dynamic_cast<SelectStatement *>(statement))
        (void)executeSelect(*stmt);
    else if (CreateStatement *stmt = dynamic_cast<CreateStatement *>(statement))
        executeCreate(*stmt);
    else if (DropStatement *stmt = dynamic_cast<DropStatement *>(statement))
        executeDrop(*stmt);
    else if (UpdateStatement *stmt = dynamic_cast<UpdateStatement *>(statement))
        executeUpdate(*stmt);
}

void Engine::executeCreate(const CreateStatement &stmt)
{
    catalog.createTable(stmt.getTable(), stmt.getColumns());
}

void Engine::executeDrop(const DropStatement &stmt)
{
    if (!catalog.tableExists(stmt.getTable()))
        throw std::runtime_error("Table " + stmt.getTable() + " does not exist.");

    dropTableStorage(stmt.getTable());
    catalog.dropTable(stmt.getTable());
}

void Engine::dropTableStorage(const std::string &tableName)
{
    std::vector<Columns> scheme = catalog.getColumns(tableName);
    Table table(tableName, scheme, cacheCapacity);
    table.dropStorage();
}

void Engine::executeInsert(const InsertStatement &stmt)
{
    std::vector<Columns> scheme = catalog.getColumns(stmt.getTable());
    Table table(stmt.getTable(), scheme, cacheCapacity);

    Row row;
    row.values = stmt.getValues();

    std::string rowData;
    for (int i = 0; i < (int)row.values.size(); i++)
    {
        if (i > 0) rowData += "|";
        rowData += row.values[i];
    }

    wal.logInsert(stmt.getTable(), rowData);

    auto result = table.insertRow(row);
    if (!result)
    {
        std::string errorMsg = "Insert failed for table " + stmt.getTable() + ": ";
        switch (result.error())
        {
            case TableError::SCHEME_MISMATCH:
                errorMsg += "Column count mismatch (expected " + std::to_string(scheme.size()) + " columns)";
                break;
            case TableError::TYPE_VALIDATION_FAILED:
                errorMsg += "Type validation failed (invalid value for column type)";
                break;
            case TableError::PAGE_MANAGER_FULL:
                errorMsg += "Page manager full (disk space or page limit reached)";
                break;
            case TableError::INDEX_INSERTION_FAILED:
                errorMsg += "Index insertion failed (B+Tree error)";
                break;
            default:
                errorMsg += "Unknown error";
                break;
        }
        throw std::runtime_error(errorMsg);
    }

    wal.commit();
}

std::vector<Row> Engine::executeSelect(const SelectStatement &stmt)
{
    std::vector<Columns> scheme = catalog.getColumns(stmt.getTable());
    Table table(stmt.getTable(), scheme, cacheCapacity);

    std::vector<Row> allRows = table.selectRow(stmt.getCondition());
    const std::vector<std::string> &selectedColumns = stmt.getColumns();

    if (selectedColumns.size() == 1 && selectedColumns[0] == "*")
        return allRows;

    for (const auto &selectedColumn : selectedColumns)
    {
        bool found = std::any_of(scheme.begin(), scheme.end(),
                                 [&](const Columns &c) { return c.name == selectedColumn; });
        if (!found)
            throw std::runtime_error("Column " + selectedColumn + " does not exist in table " + stmt.getTable() + ".");
    }

    std::vector<Row> results;
    for (const auto &row : allRows)
    {
        Row projected;
        for (const auto &selectedColumn : selectedColumns)
        {
            auto it = std::find_if(scheme.begin(), scheme.end(),
                                   [&](const Columns &c) { return c.name == selectedColumn; });
            int colIndex = (int)std::distance(scheme.begin(), it);
            if (colIndex >= 0 && colIndex < (int)row.values.size())
                projected.values.push_back(row.values[colIndex]);
        }
        results.push_back(projected);
    }
    return results;
}

void Engine::executeDelete(const DeleteStatement &stmt)
{
    wal.logDelete(stmt.getTable(), stmt.getCondition());
    std::vector<Columns> scheme = catalog.getColumns(stmt.getTable());
    Table table(stmt.getTable(), scheme, cacheCapacity);
    table.deleteRow(stmt.getCondition());
    wal.commit();
}

void Engine::executeUpdate(const UpdateStatement &stmt)
{
    wal.logUpdate(stmt.getTable(), stmt.getCondition(), stmt.getAssignments());
    std::vector<Columns> scheme = catalog.getColumns(stmt.getTable());
    Table table(stmt.getTable(), scheme, cacheCapacity);
    table.updateRow(stmt.getCondition(), stmt.getAssignments());
    wal.commit();
}

std::vector<Row> Engine::query(const std::string &sql)
{
    Lexer lexer(sql);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto result = parser.parse();
    if (!result) {
        throw std::runtime_error("Parse error in query: " + std::to_string((int)result.error()));
    }
    Statement *stmt = result.value();

    std::vector<Row> results;

    if (!stmt)
        throw std::runtime_error("Invalid SQL query.");

    if (SelectStatement *s = dynamic_cast<SelectStatement*>(stmt))
    {
        if (!catalog.tableExists(s->getTable()))
        {
            delete stmt;
            throw std::runtime_error("Table " + s->getTable() + " does not exist.");
        }
        try { results = executeSelect(*s); }
        catch (...) { delete stmt; throw; }
    }
    else if (InsertStatement *ins = dynamic_cast<InsertStatement*>(stmt))
    {
        if (!catalog.tableExists(ins->getTable()))
        {
            delete stmt;
            throw std::runtime_error("Table " + ins->getTable() + " does not exist.");
        }
        execute(stmt);
    }
    else if (DeleteStatement *del = dynamic_cast<DeleteStatement*>(stmt))
    {
        if (!catalog.tableExists(del->getTable()))
        {
            delete stmt;
            throw std::runtime_error("Table " + del->getTable() + " does not exist.");
        }
        execute(stmt);
    }
    else if (DropStatement *drop = dynamic_cast<DropStatement*>(stmt))
    {
        if (!catalog.tableExists(drop->getTable()))
        {
            delete stmt;
            throw std::runtime_error("Table " + drop->getTable() + " does not exist.");
        }
        execute(stmt);
    }
    else
    {
        execute(stmt);
    }

    delete stmt;

    return results;
}