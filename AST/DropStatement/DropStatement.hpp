#ifndef DROP_STATEMENT_HPP
#define DROP_STATEMENT_HPP

#include "../Statement/Statement.hpp"
#include <string>

class DropStatement : public Statement{
    public:
        explicit DropStatement(std::string table);

        void execute() override;

        std::string getTable() const;

        void setTable(const std::string &table);

        ~DropStatement() override;
    private:
        std::string table;
};


#endif