#include "Engine.hpp"

#include <algorithm>
#include <cctype>
#include <climits>
#include <stdexcept>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <unordered_set>

// Filenames are case-insensitive on Windows, so database names must be compared that way
static bool equalsIgnoreCase(const std::string &a, const std::string &b)
{
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
               return std::tolower(x) == std::tolower(y);
           });
}

// Index of a column in the schema, or -1
static int findColumn(const std::vector<Columns> &scheme, const std::string &name)
{
    for (int i = 0; i < (int)scheme.size(); i++)
        if (scheme[i].name == name)
            return i;
    return -1;
}

// Throws the same error the SELECT list uses when a column is not in the table
static void requireColumn(const std::vector<Columns> &scheme, const std::string &column, const std::string &table)
{
    if (findColumn(scheme, column) == -1)
        throw std::runtime_error("Column " + column + " does not exist in table " + table);
}

// A WHERE on a misspelled column used to match nothing and succeed, which looks exactly
// like "no matching rows". The same goes for a literal the column can never hold
// (age = 'abc'), so that is an error too. INT and FLOAT accept any number, which keeps
// age > 2.5 and id = 5.0 working; IS [NOT] NULL and = NULL carry no value to check.
static void requireValidCondition(const Condition *cond, const std::vector<Columns> &scheme,
                                  const Table &table, const std::string &tableName)
{
    if (cond == nullptr)
        return;
    requireColumn(scheme, cond->column, tableName);
    if (!cond->value)
        return;

    const Columns &column = scheme[findColumn(scheme, cond->column)];
    const std::string checkAs = column.type == "INT" ? "FLOAT" : column.type;
    if (!table.validateValueForType(cond->value, checkAs))
        throw std::runtime_error("Type validation failed (invalid value for column " + column.name
            + " of type " + column.type + ")");
}

// The same name twice: in CREATE TABLE only the first copy would ever be reachable, in
// UPDATE SET the last one would silently win
static void requireUniqueNames(const std::vector<std::string> &names)
{
    std::unordered_set<std::string> seen;
    for (const auto &name : names)
        if (!seen.insert(name).second)
            throw std::runtime_error("Column " + name + " specified more than once");
}

Engine::Engine(int cacheCapacity,
               const std::string &storagePath,
               const std::string &catalogPath,
               const std::string &walPath)
    : catalog(std::make_unique<Catalog>(catalogPath)),
      storage(std::make_unique<StorageFile>(storagePath)),
      wal(std::make_unique<WALManager>(walPath)),
      cacheCapacity(cacheCapacity)
{
    currentDatabase = std::filesystem::path(storagePath).stem().string();
    wal->recover(*this);
}

Catalog& Engine::getCatalog()
{
    return *catalog;
}

std::string Engine::getCurrentDatabase() const
{
    return currentDatabase;
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
    currentDatabase = std::filesystem::path(storagePath).stem().string();
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
    else if (DropDatabaseStatement *stmt = dynamic_cast<DropDatabaseStatement *>(statement))
        executeDropDatabase(*stmt);
    else if (UseDatabaseStatement *stmt = dynamic_cast<UseDatabaseStatement *>(statement))
        executeUseDatabase(*stmt);
}

void Engine::executeCreate(const CreateStatement &stmt)
{
    // Checked here and not in the parser, since a ParseError cannot carry the column name.
    // Names are case-sensitive, like everywhere else, so (a INT, A STRING) is allowed.
    std::vector<std::string> names;
    for (const auto &c : stmt.getColumns())
        names.push_back(c.name);
    requireUniqueNames(names);

    // Checked here and not in the parser for the same reason: the message names the column.
    // One counter per table keeps "the generated value" unambiguous.
    int autoColumns = 0;
    for (const auto &c : stmt.getColumns())
    {
        if (!c.auto_increment)
            continue;
        if (c.type != "INT")
            throw std::runtime_error("Column " + c.name + " cannot be AUTOINCREMENT: only INT columns can");
        if (++autoColumns > 1)
            throw std::runtime_error("Table " + stmt.getTable() + " has more than one AUTOINCREMENT column");
    }

    // createTable also refuses an empty column list, but the parser already rejects
    // that, so a false result here always means the name is taken.
    if (!catalog->createTable(stmt.getTable(), stmt.getColumns()))
        throw std::runtime_error("Table " + stmt.getTable() + " already exists");
}

void Engine::executeDrop(const DropStatement &stmt)
{
    if (!catalog->tableExists(stmt.getTable()))
        throw std::runtime_error("Table " + stmt.getTable() + " does not exist");

    dropTableStorage(stmt.getTable());
    catalog->dropTable(stmt.getTable());
}

// "<op> failed for table T: ..." for a TableError; row supplies the key for DUPLICATE_KEY
// dupCol is the UNIQUE column that repeats a value; only read for DUPLICATE_KEY
static std::string tableErrorMessage(const std::string &op, const std::string &table, TableError error,
                                     const std::vector<Columns> &scheme, const Row &row,
                                     size_t dupCol = 0)
{
    std::string errorMsg = op + " failed for table " + table + ": ";
    switch (error)
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
        case TableError::DUPLICATE_KEY:
        {
            std::string shown = dupCol < row.values.size() ? row.values[dupCol].value_or("") : "";
            // INT compares as a number, so show it as one: 007 collides with 7 and is reported as 7
            if (dupCol < scheme.size() && scheme[dupCol].type == "INT")
            {
                try { shown = std::to_string(std::stoll(shown)); }
                catch (...) {}
            }
            errorMsg += "Duplicate key " + shown + " for column "
                + (dupCol < scheme.size() ? scheme[dupCol].name : "?");
            break;
        }
        default:
            errorMsg += "Unknown error";
            break;
    }
    return errorMsg;
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

    const int autoCol = table.autoColumn();

    // With a column list, place each value at its schema position; columns left out get
    // NULL. The parser already checked that the list and the values have the same length.
    // Only the reordered row reaches the WAL, so recovery can replay it positionally.
    const std::vector<std::string> columns = stmt.getColumns();
    if (!columns.empty())
    {
        std::vector<Value> ordered(scheme.size(), std::nullopt);
        std::vector<bool> seen(scheme.size(), false);
        for (size_t i = 0; i < columns.size(); i++)
        {
            int colIndex = findColumn(scheme, columns[i]);
            if (colIndex == -1)
                throw std::runtime_error("Column " + columns[i] + " does not exist in table " + stmt.getTable());
            if (seen[colIndex])
                throw std::runtime_error("Column " + columns[i] + " specified more than once");
            seen[colIndex] = true;
            ordered[colIndex] = row.values[i];
        }
        row.values = std::move(ordered);
    }
    // No column list, but exactly one value short and the table has an AUTOINCREMENT column:
    // the values are for the other columns, in order, and the counter column is left out.
    // So INSERT INTO products VALUES ('milk') works for (id INT AUTOINCREMENT, name STRING).
    else if (autoCol >= 0 && row.values.size() + 1 == scheme.size())
    {
        row.values.insert(row.values.begin() + autoCol, std::nullopt);
    }

    // An AUTOINCREMENT column left out of the INSERT, or given NULL, gets the next value.
    // Done before checkRow and before the WAL: the log then holds the concrete value, so
    // replaying it assigns exactly the same id.
    if (autoCol >= 0 && autoCol < static_cast<int>(row.values.size()) && !row.values[autoCol])
    {
        const long long next = table.nextAutoValue();
        if (next > INT_MAX)
            throw std::runtime_error("Insert failed for table " + stmt.getTable() + ": AUTOINCREMENT column "
                + scheme[autoCol].name + " reached its maximum value");
        row.values[autoCol] = std::to_string(next);
    }

    // Reject before logging, so a failed INSERT leaves no uncommitted line in the WAL.
    // Recovery skips the duplicate and type checks: everything in the WAL was valid when
    // written, and replaying a REPLACE block row by row must not drop legacy duplicates or
    // values a stricter check now rejects.
    if (!inRecovery)
    {
        if (auto ok = table.checkRow(row); !ok)
        {
            const size_t dupCol = ok.error() == TableError::DUPLICATE_KEY
                ? table.findDuplicate(row).value_or(0) : 0;
            throw std::runtime_error(tableErrorMessage("Insert", stmt.getTable(), ok.error(), scheme, row, dupCol));
        }
        wal->logInsert(stmt.getTable(), row.values);
    }

    auto result = table.insertRow(row, !inRecovery);
    if (!result)
        throw std::runtime_error(tableErrorMessage("Insert", stmt.getTable(), result.error(), scheme, row));

    lastAffected = 1;
    if (autoCol >= 0 && autoCol < static_cast<int>(row.values.size()) && row.values[autoCol])
    {
        try { lastInsertId = std::stoll(*row.values[autoCol]); }
        catch (...) {}
    }

    if (!inRecovery)
        wal->commit();
}

std::vector<Row> Engine::executeSelect(const SelectStatement &stmt)
{
    std::vector<Columns> scheme = catalog->getColumns(stmt.getTable());
    Table table(stmt.getTable(), scheme, *storage, catalog->getTableId(stmt.getTable()), cacheCapacity);

    requireValidCondition(stmt.getCondition(), scheme, table, stmt.getTable());
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
            throw std::runtime_error("Column " + selectedColumn + " does not exist in table " + stmt.getTable());
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

    requireValidCondition(stmt.getCondition(), scheme, table, stmt.getTable());

    // The row holding the highest AUTOINCREMENT value may be about to go: record that value in the
    // catalog first, so the id is never handed out again. Saved before the WAL is written, so a
    // crash in between leaves at worst a gap, never a reused id. Recovery skips it: the mark was
    // already saved when the statement first ran.
    if (!inRecovery && table.autoColumn() >= 0)
        catalog->raiseAutoHighWater(stmt.getTable(), table.autoMax());

    if (!inRecovery)
    {
        // Log the full new state before touching storage — guarantees recovery
        std::vector<Row> toKeep = table.previewDelete(stmt.getCondition());
        wal->logReplaceBegin(stmt.getTable(), stmt.getCondition());
        for (const auto &row : toKeep)
            wal->logReplaceRow(stmt.getTable(), row.values);
    }

    lastAffected = table.deleteRow(stmt.getCondition());

    if (!inRecovery)
        wal->commit();
}

void Engine::executeUpdate(const UpdateStatement &stmt)
{
    std::vector<Columns> scheme = catalog->getColumns(stmt.getTable());
    Table table(stmt.getTable(), scheme, *storage, catalog->getTableId(stmt.getTable()), cacheCapacity);

    // Validate up front: previewUpdate silently returns nothing for a bad column or value,
    // which used to reach the client as a plain ok
    std::vector<std::string> assigned;
    for (const auto &a : stmt.getAssignments())
    {
        requireColumn(scheme, a.first, stmt.getTable());
        const Columns &column = scheme[findColumn(scheme, a.first)];
        if (!table.validateValueForType(a.second, column.type))
            throw std::runtime_error("Update failed for table " + stmt.getTable()
                + ": Type validation failed (invalid value for column " + column.name
                + " of type " + column.type + ")");
        assigned.push_back(a.first);
    }
    requireUniqueNames(assigned);
    requireValidCondition(stmt.getCondition(), scheme, table, stmt.getTable());

    // Same reason as in DELETE: assigning the AUTOINCREMENT column can overwrite its highest value
    if (!inRecovery && table.autoColumn() >= 0
        && std::find(assigned.begin(), assigned.end(), scheme[table.autoColumn()].name) != assigned.end())
        catalog->raiseAutoHighWater(stmt.getTable(), table.autoMax());

    if (!inRecovery)
    {
        std::vector<Row> newState = table.previewUpdate(stmt.getCondition(), stmt.getAssignments());

        // Only an assignment to a UNIQUE column can create a duplicate. SET values are
        // literals, so every matched row gets the same value: a conflict means two rows end
        // up with it. Checked before the WAL and before updateRow's clear + reinsert.
        for (const auto &a : stmt.getAssignments())
        {
            const int col = findColumn(scheme, a.first); // validated above
            const Columns &column = scheme[col];
            if (!column.unique)
                continue;

            if (!a.second)
            {
                // NULL is never a duplicate, but an AUTOINCREMENT column must always hold a value
                if (column.auto_increment)
                    throw std::runtime_error("Update failed for table " + stmt.getTable() + ": Column "
                        + column.name + " is AUTOINCREMENT and cannot be NULL");
                continue;
            }

            Row probe;
            probe.values.assign(scheme.size(), std::nullopt);
            probe.values[col] = a.second;
            if (col == table.keyColumn())
            {
                // Passes the digit check but overflows int: it could not be indexed
                try { table.keyOf(probe); }
                catch (...)
                {
                    throw std::runtime_error(tableErrorMessage("Update", stmt.getTable(),
                        TableError::INDEX_INSERTION_FAILED, scheme, probe));
                }
            }

            size_t holders = 0;
            for (const auto &row : newState)
                if (row.values[col] && Table::valuesEqual(column.type, *row.values[col], *a.second))
                    holders++;
            if (holders > 1)
                throw std::runtime_error(tableErrorMessage("Update", stmt.getTable(),
                    TableError::DUPLICATE_KEY, scheme, probe, static_cast<size_t>(col)));
        }

        if (!newState.empty())
        {
            wal->logReplaceBegin(stmt.getTable(), stmt.getCondition());
            for (const auto &row : newState)
                wal->logReplaceRow(stmt.getTable(), row.values);
        }
    }

    lastAffected = table.updateRow(stmt.getCondition(), stmt.getAssignments());

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

std::optional<size_t> Engine::getLastAffected() const {
    return lastAffected;
}

std::optional<long long> Engine::getLastInsertId() const {
    return lastInsertId;
}

void Engine::executeCreateDatabase(const CreateDatabaseStatement &stmt)
{
    std::filesystem::create_directories("databases");
    std::string base = "databases/" + stmt.getName();

    // Existence is judged by .db, like USE and DROP. It used to answer ok and leave the
    // old files alone, which looks exactly like a fresh, empty database being created
    if (std::filesystem::exists(base + ".db"))
        throw std::runtime_error("Database '" + stmt.getName() + "' already exists");

    // DROP removes .cat before .db, so no orphaned .cat can be left behind to truncate here
    std::ofstream(base + ".db").close();
    std::ofstream(base + ".cat").close();
}

void Engine::executeDropDatabase(const DropDatabaseStatement &stmt)
{
    const std::string &name = stmt.getName();

    if (equalsIgnoreCase(name, currentDatabase))
        throw std::runtime_error("Cannot drop the currently active database '" + name + "' (USE another database first)");

    std::string base = "databases/" + name;
    if (!std::filesystem::exists(base + ".db"))
        throw std::runtime_error("Database '" + name + "' does not exist");

    // .db goes last so a partial failure leaves the database still droppable.
    // .wal is absent until something has been written, which remove() treats as a no-op.
    for (const char *ext : {".wal", ".cat", ".db"})
    {
        std::error_code ec;
        std::filesystem::remove(base + ext, ec);
        if (ec)
            throw std::runtime_error("Failed to delete database '" + name + "': " + ext + " is in use");
    }
}

void Engine::executeUseDatabase(const UseDatabaseStatement &stmt)
{
    std::string base = "databases/" + stmt.getName();
    if (!std::filesystem::exists(base + ".db"))
        throw std::runtime_error("Database '" + stmt.getName() + "' does not exist (use CREATE DATABASE first)");

    switchDatabase(base + ".db", base + ".cat", base + ".wal");
    pendingSwitch = stmt.getName();
}

std::vector<Row> Engine::query(const std::string &sql, bool allowDatabaseAdmin)
{
    Lexer lexer(sql);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);

    auto result = parser.parse();
    if (!result) {
        throw std::runtime_error("Parse error: " + parseErrorMessage(result.error()));
    }
    std::unique_ptr<Statement> stmt = std::move(result.value());

    std::vector<Row> results;
    lastColumns.clear();
    lastAffected.reset();
    lastInsertId.reset();

    if (!stmt)
        throw std::runtime_error("Invalid SQL query");

    if (!allowDatabaseAdmin && (dynamic_cast<CreateDatabaseStatement*>(stmt.get()) || dynamic_cast<DropDatabaseStatement*>(stmt.get())))
        throw std::runtime_error("CREATE DATABASE/DROP DATABASE is not permitted for remote clients");

    if (SelectStatement *s = dynamic_cast<SelectStatement*>(stmt.get()))
    {
        if (!catalog->tableExists(s->getTable()))
            throw std::runtime_error("Table " + s->getTable() + " does not exist");
        results = executeSelect(*s);
    }
    else if (InsertStatement *ins = dynamic_cast<InsertStatement*>(stmt.get()))
    {
        if (!catalog->tableExists(ins->getTable()))
            throw std::runtime_error("Table " + ins->getTable() + " does not exist");
        execute(stmt.get());
    }
    else if (DeleteStatement *del = dynamic_cast<DeleteStatement*>(stmt.get()))
    {
        if (!catalog->tableExists(del->getTable()))
            throw std::runtime_error("Table " + del->getTable() + " does not exist");
        execute(stmt.get());
    }
    else if (UpdateStatement *upd = dynamic_cast<UpdateStatement*>(stmt.get()))
    {
        if (!catalog->tableExists(upd->getTable()))
            throw std::runtime_error("Table " + upd->getTable() + " does not exist");
        execute(stmt.get());
    }
    else if (DropStatement *drop = dynamic_cast<DropStatement*>(stmt.get()))
    {
        if (!catalog->tableExists(drop->getTable()))
            throw std::runtime_error("Table " + drop->getTable() + " does not exist");
        execute(stmt.get());
    }
    else
    {
        execute(stmt.get());
    }

    return results;
}
