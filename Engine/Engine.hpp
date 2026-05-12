#ifndef ENGINE_HPP
#define ENGINE_HPP

#include "../Catalog/Catalog.hpp"
#include "../Table/Table.hpp"
#include "../AST/Statement/Statement.hpp"
#include "../AST/CreateStatement/CreateStatement.hpp"
#include "../AST/InsertStatement/InsertStatement.hpp"
#include "../AST/DeleteStatement/DeleteStatement.hpp"
#include "../AST/SelectStatement/SelectStatement.hpp"
#include "../WALManager/WALManager.hpp"
#include "../Frontend/Lexer/Lexer.hpp"
#include "../Frontend/Parser/Parser.hpp"

class Engine {
    public:
        Engine(Catalog &catalog, int cacheCapacity = 128);

        void execute(Statement *statement);
        std::vector<Row> query(const std::string &sql);
    private:
        Catalog &catalog;
        WALManager wal;
        int cacheCapacity;

        void executeCreate(const CreateStatement &statement);
        void executeInsert(const InsertStatement &statement);
        void executeDelete(const DeleteStatement &statement);
        std::vector<Row> executeSelect(const SelectStatement &statement);
        void executeDrop(const DropStatement &statement);
        void executeUpdate(const UpdateStatement &statement);
        void dropTableStorage(const std::string &tableName);
};


#endif