#ifndef INSERT_STATEMENT_HPP
#define INSERT_STATEMENT_HPP

#include "../Statement/Statement.hpp"
#include <vector>
#include <string>
#include "../Row/Row.hpp"

class InsertStatement : public Statement{
    public:
        //Constructors
        InsertStatement(std::string table, std::vector<Value> values);
        InsertStatement(std::string table, std::vector<std::string> columns,
             std::vector<Value> values);

        //Execute
        void execute() override;

        //Setters
        void setTable(const std::string &table);
        void setValues(const std::vector<Value> &values);
        void setColumns(const std::vector<std::string> &columns);

        //Getters
        std::string getTable() const;
        std::vector<Value> getValues() const;
        std::vector<std::string> getColumns() const;

        //Destructor
        ~InsertStatement() override;
    private:
        std::string table;
        std::vector<Value> values;
        std::vector<std::string> columns;
};

#endif