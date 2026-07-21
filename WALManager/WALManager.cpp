#include "WALManager.hpp"
#include "../Engine/Engine.hpp"
#include "../Frontend/Lexer/Lexer.hpp"
#include "../Frontend/Parser/Parser.hpp"
#include "../StringUtils/StringUtils.hpp"
#include <memory>
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

void WALManager::logInsert(const std::string &table, const std::vector<std::string> &values)
{
    std::string rowData;
    for (int i = 0; i < (int)values.size(); i++) {
        if (i > 0) rowData += "|";
        rowData += walEncode(values[i]);
    }
    file << "INSERT|" << table << "|" << rowData << "|0\n";
    file.flush();
}

void WALManager::logDelete(const std::string &table, const Condition *condition)
{
    std::string condData;
    if (condition)
        condData = walEncode(condition->column) + "~" + walEncode(condition->op) + "~" + walEncode(condition->value);
    file << "DELETE|" << table << "|" << condData << "|0\n";
    file.flush();
}

void WALManager::logUpdate(const std::string &table, const Condition *condition,
                           const std::vector<std::pair<std::string, std::string>> &assignments)
{
    std::string condData;
    if (condition)
        condData = walEncode(condition->column) + "~" + walEncode(condition->op) + "~" + walEncode(condition->value);

    std::string assignData;
    for (const auto &a : assignments)
    {
        if (!assignData.empty()) assignData += "~";
        assignData += walEncode(a.first) + "~" + walEncode(a.second);
    }

    file << "UPDATE|" << table << "|" << condData << "|" << assignData << "|0\n";
    file.flush();
}

void WALManager::logReplaceBegin(const std::string &table, const Condition *condition)
{
    std::string condData;
    if (condition)
        condData = walEncode(condition->column) + "~" + walEncode(condition->op) + "~" + walEncode(condition->value);
    file << "REPLACE_BEGIN|" << table << "|" << condData << "|0\n";
    file.flush();
}

void WALManager::logReplaceRow(const std::string &table, const std::vector<std::string> &values)
{
    std::string rowData;
    for (int i = 0; i < (int)values.size(); i++) {
        if (i > 0) rowData += "|";
        rowData += walEncode(values[i]);
    }
    file << "REPLACE_ROW|" << table << "|" << rowData << "|0\n";
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

        if (line == "COMMIT")
        {
            for (auto &e : entries)
                e.committed = true;
            continue;
        }

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
    file << "COMMIT\n";
    file.flush();
    file.close();
    syncFile(filename);
    file.open(filename, std::ios::in | std::ios::out | std::ios::app);
}

static void replaySQL(const std::string &sql, Engine &engine)
{
    Lexer lexer(sql);
    std::vector<Token> tokens = lexer.tokenize();
    Parser parser(tokens);
    auto result = parser.parse();
    if (!result) {
        std::cerr << "WAL parse error: " << parseErrorMessage(result.error()) << std::endl;
        return;
    }
    std::unique_ptr<Statement> stmt = std::move(result.value());
    if (stmt)
        engine.execute(stmt.get());
}

void WALManager::recover(Engine &engine)
{
    std::vector<WALEntry> entries = readLog();

    for (int i = 0; i < (int)entries.size(); i++)
    {
        const auto &e = entries[i];
        if (e.committed) continue;

        if (e.type == "REPLACE_BEGIN")
        {
            // Collect the rows to restore from following REPLACE_ROW entries
            std::vector<std::vector<std::string>> rows;
            int j = i + 1;
            while (j < (int)entries.size() && entries[j].type == "REPLACE_ROW")
            {
                std::vector<std::string> vals = split(entries[j].rowData, '|');
                for (auto &v : vals) v = walDecode(v);
                rows.push_back(std::move(vals));
                j++;
            }
            i = j - 1;

            // Enter recovery mode so storage ops don't re-log to WAL
            engine.setRecoveryMode(true);

            // Clear the table
            replaySQL("DELETE FROM " + e.table, engine);

            // Re-insert the rows that should remain
            for (const auto &vals : rows)
            {
                std::string csv;
                for (int k = 0; k < (int)vals.size(); k++) {
                    if (k > 0) csv += ",";
                    csv += vals[k];
                }
                replaySQL("INSERT INTO " + e.table + " VALUES (" + csv + ")", engine);
            }

            engine.setRecoveryMode(false);
            commit();
            continue;
        }

        std::string sql;

        if (e.type == "INSERT")
        {
            std::vector<std::string> vals = split(e.rowData, '|');
            std::string csv;
            for (int k = 0; k < (int)vals.size(); k++) {
                if (k > 0) csv += ",";
                csv += walDecode(vals[k]);
            }
            sql = "INSERT INTO " + e.table + " VALUES (" + csv + ")";
        }
        else if (e.type == "DELETE")
        {
            sql = "DELETE FROM " + e.table;
            if (!e.rowData.empty())
            {
                std::vector<std::string> parts = split(e.rowData, '~');
                if (parts.size() == 3)
                    sql += " WHERE " + walDecode(parts[0]) + " " + walDecode(parts[1]) + " " + walDecode(parts[2]);
            }
        }
        else if (e.type == "UPDATE")
        {
            std::vector<std::string> fields = split(e.rowData, '|');
            std::string condPart  = fields.size() > 0 ? fields[0] : "";
            std::string assignPart = fields.size() > 1 ? fields[1] : "";

            std::vector<std::string> assigns = split(assignPart, '~');
            if (assigns.size() < 2)
                continue;

            sql = "UPDATE " + e.table + " SET ";
            for (int k = 0; k + 1 < (int)assigns.size(); k += 2)
            {
                if (k > 0) sql += ", ";
                sql += walDecode(assigns[k]) + " = " + walDecode(assigns[k + 1]);
            }

            if (!condPart.empty())
            {
                std::vector<std::string> cparts = split(condPart, '~');
                if (cparts.size() == 3)
                    sql += " WHERE " + walDecode(cparts[0]) + " " + walDecode(cparts[1]) + " " + walDecode(cparts[2]);
            }
        }
        else
            continue;

        replaySQL(sql, engine);
        commit();
    }
}