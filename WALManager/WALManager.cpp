#include "WALManager.hpp"
#include "../Engine/Engine.hpp"
#include "../Frontend/Lexer/Lexer.hpp"
#include "../Frontend/Parser/Parser.hpp"
#include "../StringUtils/StringUtils.hpp"

WALManager::WALManager(std::string filename)
    : filename(filename)
{
    file.open(filename, std::ios::in | std::ios::out | std::ios::app);
    if (!file.is_open()) {
        file.open(filename, std::ios::out);
        file.close();
        file.open(filename, std::ios::in | std::ios::out | std::ios::app);
    }
}

void WALManager::logInsert(const std::string &table, const std::string &rowData)
{
    file << "INSERT|" << table << "|" << rowData << "|0\n";
    file.flush();
}

std::vector<WALEntry> WALManager::readLog()
{
    std::vector<WALEntry> entries;
    file.clear();
    file.seekg(0);

    std::string line;
    while (std::getline(file, line))
    {
        if (line.empty())
            continue;

        std::vector<std::string> parts = split(line, '|');

        if (parts.size() < 4)
            continue;

        WALEntry entry;
        entry.type = parts[0];
        entry.table = parts[1];

        for (int i = 2; i < (int)parts.size() - 1; i++)
        {
            if (i > 2) entry.rowData += "|";
            entry.rowData += parts[i];
        }

        entry.committed = (parts.back() == "1");
        entries.push_back(entry);
    }
    return entries;
}

void WALManager::commit()
{
    std::vector<WALEntry> entries = readLog();
    if (entries.empty()) return;

    entries.back().committed = true;

    file.close();
    file.open(filename, std::ios::out | std::ios::trunc);

    for (const auto &e : entries)
        file << e.type << "|" << e.table << "|" << e.rowData << "|"
             << (e.committed ? "1" : "0") << "\n";

    file.flush();
    file.close();
    file.open(filename, std::ios::in | std::ios::out | std::ios::app);
}

void WALManager::recover(Engine &engine)
{
    std::vector<WALEntry> entries = readLog();

    for (const auto &e : entries)
    {
        if (!e.committed && e.type == "INSERT")
        {
            std::string sql = "INSERT INTO " + e.table + " VALUES (" + e.rowData + ")";
            Lexer lexer(sql);
            std::vector<Token> tokens = lexer.tokenize();
            Parser parser(tokens);
            auto result = parser.parse();
            if (!result) {
                std::cerr << "WAL parse error: " << (int)result.error() << std::endl;
                continue;  // Sau return/break
            }
            Statement *stmt = result.value();
            if (stmt)
            {
                engine.execute(stmt);
                delete stmt;
            }
            commit();
        }
    }
}