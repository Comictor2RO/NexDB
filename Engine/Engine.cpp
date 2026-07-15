#include "Engine.hpp"

#include <algorithm>
#include <stdexcept>
#include <filesystem>
#include <fstream>

Engine::Engine(int cacheCapacity,
               const std::string &storagePath,
               const std::string &catalogPath,
               const std::string &walPath)
    : catalog(std::make_unique<Catalog>(catalogPath)),
      storage(std::make_unique<StorageFile>(storagePath)),
      wal(std::make_unique<WALManager>(walPath)),
      cacheCapacity(cacheCapacity)
{
    wal->recover(*this);
}

Catalog& Engine::getCatalog()
{
    return *catalog;
}

void Engine::switchDatabase(const std::string &storagePath,
                            const std::string &catalogPath,
                            const std::string &walPath)
{
    wal.reset();
    storage.reset();
    catalog.reset();

    catalog = std::make_unique<Catalog>(catalogPath);
    storage = std::make_unique<StorageFile>(storagePath);
    wal     = std::make_unique<WALManager>(walPath);
    wal->recover(*this);
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
    else if (CreateDatabaseStatement *stmt = dynamic_cast<CreateDatabaseStatement *>(statement))
        executeCreateDatabase(*stmt);
    else if (UseDatabaseStatement *stmt = dynamic_cast<UseDatabaseStatement *>(statement))
        executeUseDatabase(*stmt);
}

void Engine::executeCreate(const CreateStatement &stmt)
{
    catalog->createTable(stmt.getTable(), stmt.getColumns());
}

void Engine::executeDrop(const DropStatement &stmt)
{
    if (!catalog->tableExists(stmt.getTable()))
        throw std::runtime_error("Table " + stmt.getTable() + " does not exist.");

    dropTableStorage(stmt.getTable());
    catalog->dropTable(stmt.getTable());
}

void Engine::dropTableStorage(const std::string &tableName)
{
    storage->freePagesForTable(catalog->getTableId(tableName));
}

void Engine::executeInsert(const InsertStatement &stmt)
{
    std::vector<Columns> scheme = catalog->getColumns(stmt.getTable());
    Table table(stmt.getTable(), scheme, *storage, catalog->getTableId(stmt.getTable()), cacheCapacity);

    Row row;
    row.values = stmt.getValues();

    if (!inRecovery)
        wal->logInsert(stmt.getTable(), row.values);

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

    if (!inRecovery)
        wal->commit();
}

std::vector<Row> Engine::executeSelect(const SelectStatement &stmt)
{
    std::vector<Columns> scheme = catalog->getColumns(stmt.getTable());
    Table table(stmt.getTable(), scheme, *storage, catalog->getTableId(stmt.getTable()), cacheCapacity);

    std::vector<Row> allRows = table.selectRow(stmt.getCondition());
    const std::vector<std::string> &selectedColumns = stmt.getColumns();

    if (selectedColumns.size() == 1 && selectedColumns[0] == "*") {
        for (const auto& c : scheme)
            lastColumns.push_back(c.name);
        return allRows;
    }

    for (const auto &selectedColumn : selectedColumns)
    {
        bool found = std::any_of(scheme.begin(), scheme.end(),
                                 [&](const Columns &c) { return c.name == selectedColumn; });
        if (!found)
            throw std::runtime_error("Column " + selectedColumn + " does not exist in table " + stmt.getTable() + ".");
    }

    lastColumns = selectedColumns;

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
    std::vector<Columns> scheme = catalog->getColumns(stmt.getTable());
    Table table(stmt.getTable(), scheme, *storage, catalog->getTableId(stmt.getTable()), cacheCapacity);

    if (!inRecovery)
    {
        // Log the full new state before touching storage — guarantees recovery
        std::vector<Row> toKeep = table.previewDelete(stmt.getCondition());
        wal->logReplaceBegin(stmt.getTable(), stmt.getCondition());
        for (const auto &row : toKeep)
            wal->logReplaceRow(stmt.getTable(), row.values);
    }

    table.deleteRow(stmt.getCondition());

    if (!inRecovery)
        wal->commit();
}

void Engine::executeUpdate(const UpdateStatement &stmt)
{
    std::vector<Columns> scheme = catalog->getColumns(stmt.getTable());
    Table table(stmt.getTable(), scheme, *storage, catalog->getTableId(stmt.getTable()), cacheCapacity);

    if (!inRecovery)
    {
        std::vector<Row> newState = table.previewUpdate(stmt.getCondition(), stmt.getAssignments());
        if (!newState.empty())
        {
            wal->logReplaceBegin(stmt.getTable(), stmt.getCondition());
            for (const auto &row : newState)
                wal->logReplaceRow(stmt.getTable(), row.values);
        }
    }

    table.updateRow(stmt.getCondition(), stmt.getAssignments());

    if (!inRecovery)
        wal->commit();
}

std::string Engine::consumePendingSwitch()
{
    std::string s = pendingSwitch;
    pendingSwitch.clear();
    return s;
}

const std::vector<std::string> &Engine::getLastColumns() const {
    return lastColumns;
}

void Engine::executeCreateDatabase(const CreateDatabaseStatement &stmt)
{
    std::filesystem::create_directories("databases");
    std::string base = "databases/" + stmt.getName();
    if (!std::filesystem::exists(base + ".db"))
        std::ofstream(base + ".db").close();
    if (!std::filesystem::exists(base + ".cat"))
        std::ofstream(base + ".cat").close();
}

void Engine::executeUseDatabase(const UseDatabaseStatement &stmt)
{
    std::string base = "databases/" + stmt.getName();
    if (!std::filesystem::exists(base + ".db"))
        throw std::runtime_error("Database '" + stmt.getName() + "' does not exist. Use CREATE DATABASE first.");

    switchDatabase(base + ".db", base + ".cat", base + ".wal");
    pendingSwitch = stmt.getName();
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
    std::unique_ptr<Statement> stmt = std::move(result.value());

    std::vector<Row> results;
    lastColumns.clear();

    if (!stmt)
        throw std::runtime_error("Invalid SQL query.");

    if (SelectStatement *s = dynamic_cast<SelectStatement*>(stmt.get()))
    {
        if (!catalog->tableExists(s->getTable()))
            throw std::runtime_error("Table " + s->getTable() + " does not exist.");
        results = executeSelect(*s);
    }
    else if (InsertStatement *ins = dynamic_cast<InsertStatement*>(stmt.get()))
    {
        if (!catalog->tableExists(ins->getTable()))
            throw std::runtime_error("Table " + ins->getTable() + " does not exist.");
        execute(stmt.get());
    }
    else if (DeleteStatement *del = dynamic_cast<DeleteStatement*>(stmt.get()))
    {
        if (!catalog->tableExists(del->getTable()))
            throw std::runtime_error("Table " + del->getTable() + " does not exist.");
        execute(stmt.get());
    }
    else if (DropStatement *drop = dynamic_cast<DropStatement*>(stmt.get()))
    {
        if (!catalog->tableExists(drop->getTable()))
            throw std::runtime_error("Table " + drop->getTable() + " does not exist.");
        execute(stmt.get());
    }
    else
    {
        execute(stmt.get());
    }

    return results;
}
