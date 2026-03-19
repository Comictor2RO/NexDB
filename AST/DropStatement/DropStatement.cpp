#include "DropStatement.hpp"

DropStatement::DropStatement(std::string table)
    : table(table)
{}

void DropStatement::execute()
{}

void DropStatement::setTable(const std::string &table)
{
    this->table = table;
}

std::string DropStatement::getTable() const
{
    return this->table;
}

DropStatement::~DropStatement()
{}
