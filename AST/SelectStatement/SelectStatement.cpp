#include "SelectStatement.hpp"

//Constructor
SelectStatement::SelectStatement(std::vector<std::string> columns, std::string table)
    : columns(columns), table(table), condition(nullptr)
{}

SelectStatement::SelectStatement(std::vector<std::string> columns, std::string table, Condition *condition)
    : columns(columns), table(table), condition(condition)
{}

//Execute
void SelectStatement::execute()
{}

//Setters
void SelectStatement::setTable(const std::string &table)
{
    this->table = table;
}

void SelectStatement::setColumns(const std::vector<std::string> &columns)
{
    this->columns = columns;
}

void SelectStatement::setCondition(Condition *condition)
{
    this->condition = condition;
}

//Getters
std::string SelectStatement::getTable() const
{
    return this->table;
}

std::vector<std::string> SelectStatement::getColumns() const
{
    return this->columns;
}

Condition *SelectStatement::getCondition() const
{
    return this->condition;
}

//Destructor
SelectStatement::~SelectStatement()
{
    delete this->condition;
}
