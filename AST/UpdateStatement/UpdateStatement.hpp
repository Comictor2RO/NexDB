#ifndef UPDATE_STATEMENT_HPP
#define UPDATE_STATEMENT_HPP

#include <string>
#include <vector>
#include "Condition.hpp"
#include "Statement.hpp"


class UpdateStatement : public Statement{
    public:
        UpdateStatement(std::string &table);

        //Methods
        void execute() override;
        void addAssignements(const std::string &column, const std::string &value);

        //Getters
        std::string getTable() const;
        const std::vector<std::pair<std::string, std::string>> &getAssignments() const;
        Condition *getCondition() const;

        //Setters
        void setTable(const std::string &table);
        void setAssignments(const std::vector<std::pair<std::string, std::string>> &assignments);
        void setCondition(Condition *condition);

        ~UpdateStatement() override;
private:
        std::string table;
        std::vector<std::pair<std::string, std::string>> assignments;
        Condition *condition;
};


#endif