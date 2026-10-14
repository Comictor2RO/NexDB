#ifndef WALMANAGER_HPP
#define WALMANAGER_HPP

#include <string>
#include <fstream>
#include <vector>
#include "../AST/Condition/Condition.hpp"
#include "../AST/Row/Row.hpp"

struct WALEntry {
    std::string type;
    std::string table;
    std::string rowData;
    bool committed;
};

class Engine;

class WALManager {
public:
    WALManager(std::string filename);

    void logInsert(const std::string &table, const std::vector<Value> &values);
    void logDelete(const std::string &table, const Condition *condition);
    void logUpdate(const std::string &table, const Condition *condition,
                   const std::vector<std::pair<std::string, Value>> &assignments);
    void logReplaceBegin(const std::string &table, const Condition *condition);
    void logReplaceRow(const std::string &table, const std::vector<Value> &values);
    void commit();
    void recover(Engine &engine);

private:
    std::string filename;
    std::fstream file;

    std::vector<WALEntry> readLog();
};

#endif
