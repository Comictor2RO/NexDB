#include "Engine.hpp"

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
}

void Engine::executeCreate(const CreateStatement &stmt)
{
    catalog.createTable(stmt.getTable(), stmt.getColumns());
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
    table.insertRow(row);
    wal.commit();
}

void Engine::executeSelect(const SelectStatement &stmt)
{
    std::vector<Columns> scheme = catalog.getColumns(stmt.getTable());
    Table table(stmt.getTable(), scheme);

    std::vector<Row> rows = table.selectRow(stmt.getCondition());

    for (const auto &row : rows)
    {
        for (const auto &val : row.values)
            std::cout << val << "|";
        std::cout << std::endl;
    }
}

void Engine::executeDelete(const DeleteStatement &stmt)
{
    std::vector<Columns> scheme = catalog.getColumns(stmt.getTable());
    Table table(stmt.getTable(), scheme);
    table.deleteRow(stmt.getCondition());
}