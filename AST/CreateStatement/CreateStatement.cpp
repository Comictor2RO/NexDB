#include "CreateStatement.hpp"

CreateStatement::CreateStatement(std::string table, std::vector<Columns> columns)
    : table(table), columns(columns)
{}

void CreateStatement::execute()
{}

void CreateStatement::setTable(std::string table)
{
    this->table = table;
}

void CreateStatement::setColumns(std::vector<Columns> columns)
{
    this->columns = columns;
}

std::string CreateStatement::getTable() const
{
    return table;
}

std::vector<Columns> CreateStatement::getColumns() const
{
    return columns;
}

CreateStatement::~CreateStatement()
{}
