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

class Engine {
    public:
        Engine(Catalog &catalog);

        void execute(Statement *statement);
    private:
        Catalog &catalog;
        WALManager wal;

        void executeCreate(const CreateStatement &statement);
        void executeInsert(const InsertStatement &statement);
        void executeDelete(const DeleteStatement &statement);
        void executeSelect(const SelectStatement &statement);
};


#endif