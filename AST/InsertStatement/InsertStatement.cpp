#include "InsertStatement.hpp"

//Constructors
InsertStatement::InsertStatement(std::string table, std::vector<std::string> values)
    : table(table), columns({}),values(values)
{}
InsertStatement::InsertStatement(std::string table, std::vector<std::string> columns, std::vector<std::string> values)
    :table(table), columns(columns), values(values)
{}

//Execute
void InsertStatement::execute()
{}

//Setters
void InsertStatement::setTable(const std::string &table)
{
    this->table = table;
}

void InsertStatement::setValues(const std::vector<std::string> &values)
{
    this->values = values;
}

void InsertStatement::setColumns(const std::vector<std::string> &columns)
{
    this->columns = columns;
}

//Getters
std::string InsertStatement::getTable() const
{
    return this->table;
}

std::vector<std::string> InsertStatement::getValues() const
{
    return this->values;
}

std::vector<std::string> InsertStatement::getColumns() const
{
    return this->columns;
}

//Destructor
InsertStatement::~InsertStatement()
{}