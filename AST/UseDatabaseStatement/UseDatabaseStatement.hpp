#ifndef USE_DATABASE_STATEMENT_HPP
#define USE_DATABASE_STATEMENT_HPP

#include "../Statement/Statement.hpp"
#include <string>

class UseDatabaseStatement : public Statement {
public:
    explicit UseDatabaseStatement(std::string name);

    void execute() override;

    const std::string& getName() const;

    ~UseDatabaseStatement() override;
private:
    std::string name;
};

#endif
