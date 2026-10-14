#ifndef CONDITION_HPP
#define CONDITION_HPP

#include <string>
#include "../Row/Row.hpp"

// op is one of = != < > <= >= or "IS NULL" / "IS NOT NULL" (value unused for those).
// value is std::nullopt for col = NULL, which by SQL rules matches no row.
struct Condition{
    std::string column;
    Value value;
    std::string op;
};



#endif
