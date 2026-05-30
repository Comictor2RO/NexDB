#ifndef CREATE_DATABASE_STATEMENT_HPP
#define CREATE_DATABASE_STATEMENT_HPP

#include "../Statement/Statement.hpp"
#include <string>

class CreateDatabaseStatement : public Statement {
public:
    explicit CreateDatabaseStatement(std::string name);

    void execute() override;

    const std::string& getName() const;

    ~CreateDatabaseStatement() override;
private:
    std::string name;
};

#endif
