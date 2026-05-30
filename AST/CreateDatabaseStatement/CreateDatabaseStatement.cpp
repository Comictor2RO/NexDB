#include "CreateDatabaseStatement.hpp"

CreateDatabaseStatement::CreateDatabaseStatement(std::string name)
    : name(std::move(name))
{}

void CreateDatabaseStatement::execute() {}

const std::string& CreateDatabaseStatement::getName() const
{
    return name;
}

CreateDatabaseStatement::~CreateDatabaseStatement() {}
