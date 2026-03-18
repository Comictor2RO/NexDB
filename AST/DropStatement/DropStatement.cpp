#include "DropStatement.hpp"

DropStatement::DropStatement(std::string table)
    : table(table)
{}

void DropStatement::execute() {
    //TO DO
}

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
