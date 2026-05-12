#include "WALManager.hpp"
#include "../Engine/Engine.hpp"
#include "../Frontend/Lexer/Lexer.hpp"
#include "../Frontend/Parser/Parser.hpp"
#include "../StringUtils/StringUtils.hpp"
#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

static void syncFile(const std::string &filename)
{
#ifdef _WIN32
    HANDLE h = CreateFileA(filename.c_str(), GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(h);
        CloseHandle(h);
    }
#else
    int fd = open(filename.c_str(), O_WRONLY);
    if (fd >= 0) {
        fsync(fd);
        close(fd);
    }
#endif
}

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

void WALManager::logDelete(const std::string &table, const Condition *condition)
{
    std::string condData;
    if (condition)
        condData = condition->column + "~" + condition->op + "~" + condition->value;
    file << "DELETE|" << table << "|" << condData << "|0\n";
    file.flush();
}

void WALManager::logUpdate(const std::string &table, const Condition *condition,
                           const std::vector<std::pair<std::string, std::string>> &assignments)
{
    std::string condData;
    if (condition)
        condData = condition->column + "~" + condition->op + "~" + condition->value;

    std::string assignData;
    for (const auto &a : assignments)
    {
        if (!assignData.empty()) assignData += "~";
        assignData += a.first + "~" + a.second;
    }

    file << "UPDATE|" << table << "|" << condData << "|" << assignData << "|0\n";
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
    syncFile(filename);
    file.open(filename, std::ios::in | std::ios::out | std::ios::app);
}

void WALManager::recover(Engine &engine)
{
    std::vector<WALEntry> entries = readLog();

    for (const auto &e : entries)
    {
        if (e.committed) continue;

        std::string sql;

        if (e.type == "INSERT")
        {
            std::string csv = e.rowData;
            for (char &c : csv) if (c == '|') c = ',';
            sql = "INSERT INTO " + e.table + " VALUES (" + csv + ")";
        }
        else if (e.type == "DELETE")
        {
            sql = "DELETE FROM " + e.table;
            if (!e.rowData.empty())
            {
                std::vector<std::string> parts = split(e.rowData, '~');
                if (parts.size() == 3)
                    sql += " WHERE " + parts[0] + " " + parts[1] + " " + parts[2];
            }
        }
        else if (e.type == "UPDATE")
        {
            // rowData format: condcol~condop~condval|a1col~a1val~a2col~a2val
            std::vector<std::string> fields = split(e.rowData, '|');
            std::string condPart = fields.size() > 0 ? fields[0] : "";
            std::string assignPart = fields.size() > 1 ? fields[1] : "";

            std::vector<std::string> assigns = split(assignPart, '~');
            if (assigns.size() < 2) continue;

            sql = "UPDATE " + e.table + " SET ";
            for (int i = 0; i + 1 < (int)assigns.size(); i += 2)
            {
                if (i > 0) sql += ", ";
                sql += assigns[i] + " = " + assigns[i + 1];
            }

            if (!condPart.empty())
            {
                std::vector<std::string> cparts = split(condPart, '~');
                if (cparts.size() == 3)
                    sql += " WHERE " + cparts[0] + " " + cparts[1] + " " + cparts[2];
            }
        }
        else continue;

        Lexer lexer(sql);
        std::vector<Token> tokens = lexer.tokenize();
        Parser parser(tokens);
        auto result = parser.parse();
        if (!result) {
            std::cerr << "WAL parse error: " << (int)result.error() << std::endl;
            continue;
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