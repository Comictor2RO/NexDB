#include <iostream>
#include <cstdio>
#include "Frontend/Lexer/Lexer.hpp"
#include "Frontend/Parser/Parser.hpp"
#include "Engine/Engine.hpp"
#include "Catalog/Catalog.hpp"
#include "raylib.h"

void runQuery(const std::string &sql, Engine &engine)
{
    std::cout << ">> " << sql << "\n";
    Lexer lexer(sql);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);
    Statement *stmt = parser.parse();
    if (stmt)
    {
        engine.execute(stmt);
        delete stmt;
    }
}

int main()
{
    // -------------------------------------------------------
    // TEST WAL: Simulare crash - recover la repornire
    // -------------------------------------------------------
    std::cout << "\n=== TEST WAL: Recover dupa crash ===\n\n";
    std::remove("wal_test.db");
    std::remove("engine.wal");

    // PASUL 1: Simulam un crash - scriem in WAL dar nu facem commit
    {
        std::cout << "-- Pas 1: Scriem direct in WAL fara commit (crash simulat) --\n";
        WALManager wal("engine.wal");
        wal.logInsert("wal_test", "1|Vlad|25");
        wal.logInsert("wal_test", "2|Ana|30");
        // program "crapa" aici - fara commit, fara insert real in .db
    }

    // PASUL 2: La repornire, Engine apeleaza recover() in constructor
    {
        std::cout << "\n-- Pas 2: Engine porneste, recover() ruleaza automat --\n";
        Catalog catalog;
        catalog.createTable("wal_test", {{"id","INT"}, {"name","VARCHAR"}, {"age","INT"}});
        Engine engine(catalog); // <- recover() e apelat aici

        std::cout << "\n-- SELECT dupa recover - trebuie sa apara randurile --\n";
        runQuery("SELECT * FROM wal_test", engine);
        std::cout << "-- Daca apare 1|Vlad si 2|Ana, WAL a functionat --\n";
    }
}
