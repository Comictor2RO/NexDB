#ifndef CREATE_STATEMENT_HPP
#define CREATE_STATEMENT_HPP

#include "../Columns/Columns.hpp"
#include <string>
#include <vector>

#include "../Statement/Statement.hpp"

class CreateStatement : public Statement {
    public:
        //Constructor
        CreateStatement(std::string table, std::vector<Columns> columns);

        //Method
        void execute() override;

        //Setters
        void setTable(std::string table);
        void setColumns(std::vector<Columns> columns);

        //Getters
        std::string getTable() const;
        std::vector<Columns> getColumns() const;

        ~CreateStatement() override;
    private:
        std::string table;
        std::vector<Columns> columns;
};


#endif