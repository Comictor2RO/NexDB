#include "UpdateStatement.hpp"

UpdateStatement::UpdateStatement(std::string &table)
    : table(table), assignments()
{}

//Methods
void UpdateStatement::execute()
{}

void UpdateStatement::addAssignements(const std::string &column, const std::string &value)
{
    assignments.emplace_back(column, value);
}

//Getters
std::string UpdateStatement::getTable() const
{
    return table;
}

const std::vector<std::pair<std::string, std::string>> &UpdateStatement::getAssignments() const
{
    return assignments;
}
Condition *UpdateStatement::getCondition() const
{
    return condition;
}


//Setters
void UpdateStatement::setTable(const std::string &table)
{
    this->table = table;
}

void UpdateStatement::setAssignments(const std::vector<std::pair<std::string, std::string>> &assignments)
{
    this->assignments = assignments;
}

void UpdateStatement::setCondition(Condition *condition)
{
    this->condition = condition;
}

UpdateStatement::~UpdateStatement()
{}