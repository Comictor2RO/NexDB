#ifndef COLUMNS_HPP
#define COLUMNS_HPP

#include <string>

struct Columns {
    std::string name;
    std::string type;
    bool unique = false;
    bool auto_increment = false;
    // Only meaningful on the AUTOINCREMENT column: the highest value the table ever held, saved in
    // the catalog before a DELETE or UPDATE can remove it, so ids are never handed out twice
    long long autoHighWater = 0;

    std::string getName() const {
        return name;
    }
};


#endif
