#include "UseDatabaseStatement.hpp"

UseDatabaseStatement::UseDatabaseStatement(std::string name)
    : name(std::move(name))
{}

void UseDatabaseStatement::execute() {}

const std::string& UseDatabaseStatement::getName() const
{
    return name;
}

UseDatabaseStatement::~UseDatabaseStatement() {}
