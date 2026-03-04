#include <iostream>
#include <cstdio>
#include "Frontend/Lexer/Lexer.hpp"
#include "Frontend/Parser/Parser.hpp"
#include "Engine/Engine.hpp"
#include "Catalog/Catalog.hpp"

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
    // TEST 1: Operatii de baza + index B+ Tree
    // -------------------------------------------------------
    std::cout << "=== TEST 1: Operatii de baza ===\n\n";
    std::remove("users.db");
    {
        Catalog catalog;
        Engine engine(catalog);

        runQuery("CREATE TABLE users (id INT, name VARCHAR, age INT)", engine);
        runQuery("INSERT INTO users VALUES (1, Ion, 25)", engine);
        runQuery("INSERT INTO users VALUES (2, Maria, 30)", engine);
        runQuery("INSERT INTO users VALUES (3, Andrei, 22)", engine);
        runQuery("INSERT INTO users VALUES (4, Elena, 28)", engine);

        std::cout << "\n-- SELECT * (full scan) --\n";
        runQuery("SELECT * FROM users", engine);

        std::cout << "\n-- SELECT cu index WHERE id = 2 --\n";
        runQuery("SELECT * FROM users WHERE id = 2", engine);

        std::cout << "\n-- DELETE WHERE id = 1 --\n";
        runQuery("DELETE FROM users WHERE id = 1", engine);

        std::cout << "\n-- SELECT * dupa delete --\n";
        runQuery("SELECT * FROM users", engine);
    }
    // La iesirea din bloc, destructorul PageManager apeleaza
    // cache.flushAll() -> paginile dirty sunt scrise pe disc

    // -------------------------------------------------------
    // TEST 2: Cache hit - citiri repetate din acelasi Table
    // -------------------------------------------------------
    std::cout << "\n=== TEST 2: Cache hit - citiri repetate ===\n\n";
    std::remove("cache_test.db");
    {
        Catalog catalog;
        Engine engine(catalog);

        runQuery("CREATE TABLE cache_test (id INT, val VARCHAR)", engine);
        runQuery("INSERT INTO cache_test VALUES (1, alpha)", engine);
        runQuery("INSERT INTO cache_test VALUES (2, beta)", engine);
        runQuery("INSERT INTO cache_test VALUES (3, gamma)", engine);

        std::cout << "\n-- Prima citire (cache miss -> citire de pe disc) --\n";
        runQuery("SELECT * FROM cache_test", engine);

        std::cout << "\n-- A doua citire (cache hit -> citire din RAM) --\n";
        runQuery("SELECT * FROM cache_test", engine);

        std::cout << "\n-- A treia citire (cache hit) --\n";
        runQuery("SELECT * FROM cache_test", engine);

        std::cout << "\n-- Rezultatele trebuie sa fie identice de 3 ori --\n";
    }

    // -------------------------------------------------------
    // TEST 3: Flush la destructor - date persista pe disc
    // -------------------------------------------------------
    std::cout << "\n=== TEST 3: Persistenta dupa destructor ===\n\n";
    std::remove("persist_test.db");
    {
        Catalog catalog;
        Engine engine(catalog);
        runQuery("CREATE TABLE persist_test (id INT, name VARCHAR)", engine);
        runQuery("INSERT INTO persist_test VALUES (1, Vlad)", engine);
        runQuery("INSERT INTO persist_test VALUES (2, Ana)", engine);
        std::cout << "\n-- Inserare facuta, destructorul va flush cache pe disc --\n";
    }
    // Destructorul a fost apelat, datele sunt pe disc

    {
        Catalog catalog;
        Engine engine(catalog);
        std::cout << "\n-- Citire dupa restart (date din fisier, nu din cache) --\n";
        runQuery("SELECT * FROM persist_test", engine);
        std::cout << "-- Daca apare 1|Vlad si 2|Ana, flush-ul a functionat --\n";
    }

    return 0;
}
