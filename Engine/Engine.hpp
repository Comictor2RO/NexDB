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
#include "../Storage/StorageFile/StorageFile.hpp"
#include "../AST/CreateDatabaseStatement/CreateDatabaseStatement.hpp"
#include "../AST/UseDatabaseStatement/UseDatabaseStatement.hpp"
#include <memory>

class Engine {
    public:
        Engine(int cacheCapacity = 128,
               const std::string &storagePath = "databases/mydb.db",
               const std::string &catalogPath = "databases/mydb.cat",
               const std::string &walPath     = "databases/mydb.wal");

        void execute(Statement *statement);
        std::vector<Row> query(const std::string &sql);

        Catalog& getCatalog();

        // Returns pending database switch name (empty if none), then clears it
        std::string consumePendingSwitch();

        // Suppress WAL logging during crash recovery replays
        void setRecoveryMode(bool mode) { inRecovery = mode; }

    private:
        bool inRecovery = false;
        std::unique_ptr<Catalog>     catalog;
        std::unique_ptr<StorageFile> storage;
        std::unique_ptr<WALManager>  wal;
        int cacheCapacity;
        std::string pendingSwitch;

        void executeCreate(const CreateStatement &statement);
        void executeInsert(const InsertStatement &statement);
        void executeDelete(const DeleteStatement &statement);
        std::vector<Row> executeSelect(const SelectStatement &statement);
        void executeDrop(const DropStatement &statement);
        void executeUpdate(const UpdateStatement &statement);
        void executeCreateDatabase(const CreateDatabaseStatement &statement);
        void executeUseDatabase(const UseDatabaseStatement &statement);
        void dropTableStorage(const std::string &tableName);

        void switchDatabase(const std::string &storagePath,
                            const std::string &catalogPath,
                            const std::string &walPath);
};


#endif
