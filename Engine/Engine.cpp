#include "Engine.hpp"

#include <algorithm>
#include <stdexcept>

Engine::Engine(Catalog &catalog) : catalog(catalog), wal("engine.wal")
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
        executeSelect(*stmt);
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

    {
        std::vector<Columns> scheme = catalog.getColumns(stmt.getTable());
        Table table(stmt.getTable(), scheme);
        table.dropStorage();
    }

    catalog.dropTable(stmt.getTable());
}

void Engine::executeInsert(const InsertStatement &stmt)
{
    std::vector<Columns> scheme = catalog.getColumns(stmt.getTable());
    Table table(stmt.getTable(), scheme);

    Row row;
    row.values = stmt.getValues();

    std::string rowData;
    for (int i = 0; i < (int)row.values.size(); i++)
    {
        if (i > 0) rowData += "|";
        rowData += row.values[i];
    }

    wal.logInsert(stmt.getTable(), rowData);

    if (!table.insertRow(row))
        throw std::runtime_error("Insert failed for table " + stmt.getTable() + ".");

    wal.commit();
}

void Engine::executeSelect(const SelectStatement &stmt)
{
    std::vector<Columns> scheme = catalog.getColumns(stmt.getTable());
    Table table(stmt.getTable(), scheme);

    std::vector<Row> rows = table.selectRow(stmt.getCondition());
    const std::vector<std::string> &selectedColumns = stmt.getColumns();

    for (const auto &row : rows)
    {
        if (selectedColumns.size() == 1 && selectedColumns[0] == "*")
        {
            for (const auto &val : row.values)
                std::cout << val << "|";
            std::cout << std::endl;
            continue;
        }

        for (int i = 0; i < (int)selectedColumns.size(); i++)
        {
            auto it = std::find_if(scheme.begin(), scheme.end(),
                                   [&](const Columns &c)
                                   {
                                       return c.name == selectedColumns[i];
                                   });

            if (it == scheme.end())
                throw std::runtime_error("Column " + selectedColumns[i] + " does not exist in table " + stmt.getTable() + ".");

            int colIndex = (int)std::distance(scheme.begin(), it);
            if (colIndex >= 0 && colIndex < (int)row.values.size())
            {
                if (i > 0)
                    std::cout << "|";
                std::cout << row.values[colIndex];
            }
        }
        std::cout << std::endl;
    }
}

void Engine::executeDelete(const DeleteStatement &stmt)
{
    std::vector<Columns> scheme = catalog.getColumns(stmt.getTable());
    Table table(stmt.getTable(), scheme);
    table.deleteRow(stmt.getCondition());
}

void Engine::executeUpdate(const UpdateStatement &stmt)
{
    std::vector<Columns> scheme = catalog.getColumns(stmt.getTable());
    Table table(stmt.getTable(), scheme);
    table.updateRow(stmt.getCondition(), stmt.getAssignments());
}

std::vector<Row> Engine::query(const std::string &sql)
{
    Lexer lexer(sql);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);
    Statement *stmt = parser.parse();

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

        std::vector<Columns> scheme = catalog.getColumns(s->getTable());
        Table table(s->getTable(), scheme);
        std::vector<Row> allRows = table.selectRow(s->getCondition());
        const std::vector<std::string> &selectedColumns = s->getColumns();

        if (selectedColumns.size() == 1 && selectedColumns[0] == "*")
        {
            results = allRows;
        }
        else
        {
            for (const auto &selectedColumn : selectedColumns)
            {
                bool found = false;
                for (const auto &col : scheme)
                {
                    if (col.name == selectedColumn)
                    {
                        found = true;
                        break;
                    }
                }

                if (!found)
                {
                    delete stmt;
                    throw std::runtime_error("Column " + selectedColumn + " does not exist in table " + s->getTable() + ".");
                }
            }

            for (const auto &row : allRows)
            {
                Row projectedRow;

                for (const auto &selectedColumn : selectedColumns)
                {
                    auto it = std::find_if(scheme.begin(), scheme.end(),
                                           [&](const Columns &c)
                                           {
                                               return c.name == selectedColumn;
                                           });

                    int colIndex = (int)std::distance(scheme.begin(), it);
                    if (colIndex >= 0 && colIndex < (int)row.values.size())
                        projectedRow.values.push_back(row.values[colIndex]);
                }

                results.push_back(projectedRow);
            }
        }
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