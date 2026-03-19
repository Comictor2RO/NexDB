#include "DeleteStatement.hpp"

//Constructors
DeleteStatement::DeleteStatement(std::string table)
    : table(table), condition(nullptr)
{}

DeleteStatement::DeleteStatement(std::string table, Condition *condition)
    :table(table), condition(condition)
{}

//Execute
void DeleteStatement::execute()
{}

//Setters
void DeleteStatement::setTable(const std::string &table)
{
    this->table = table;
}

void DeleteStatement::setCondition(Condition *condition)
{
    this->condition = condition;
}

//Getters
std::string DeleteStatement::getTable() const
{
    return this->table;
}

Condition *DeleteStatement::getCondition() const
{
    return this->condition;
}

//Destructor
DeleteStatement::~DeleteStatement()
{
    delete this->condition;
}