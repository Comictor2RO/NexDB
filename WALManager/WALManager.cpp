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

// Rebuilds the WHERE clause of a logged condition ("col~op~value") as SQL.
// IS NULL / IS NOT NULL carry no value, everything else gets a quoted literal.
static std::string conditionSQL(const std::string &condData)
{
    std::vector<std::string> parts = splitFields(condData, '~');
    if (parts.size() != 3)
        return "";
    std::string op = walDecode(parts[1]);
    std::string sql = " WHERE " + walDecode(parts[0]) + " " + op;
    if (op.rfind("IS", 0) != 0)
        sql += " " + sqlLiteral(decodeValue(parts[2]));
    return sql;
}

// Joins logged row fields into a VALUES list: NULL stays NULL, every other value is
// quoted so spaces, commas, quotes and empty strings survive the replay.
static std::string valuesSQL(const std::string &rowData)
{
    std::string csv;
    std::vector<std::string> fields = splitFields(rowData, '|');
    for (int k = 0; k < (int)fields.size(); k++) {
        if (k > 0) csv += ",";
        csv += sqlLiteral(decodeValue(fields[k]));
    }
    return csv;
}

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

void WALManager::logInsert(const std::string &table, const std::vector<Value> &values)
{
    std::string rowData;
    for (int i = 0; i < (int)values.size(); i++) {
        if (i > 0) rowData += "|";
        rowData += encodeValue(values[i]);
    }
    file << "INSERT|" << table << "|" << rowData << "|0\n";
    file.flush();
}

void WALManager::logDelete(const std::string &table, const Condition *condition)
{
    std::string condData;
    if (condition)
        condData = walEncode(condition->column) + "~" + walEncode(condition->op) + "~" + encodeValue(condition->value);
    file << "DELETE|" << table << "|" << condData << "|0\n";
    file.flush();
}

void WALManager::logUpdate(const std::string &table, const Condition *condition,
                           const std::vector<std::pair<std::string, Value>> &assignments)
{
    std::string condData;
    if (condition)
        condData = walEncode(condition->column) + "~" + walEncode(condition->op) + "~" + encodeValue(condition->value);

    std::string assignData;
    for (const auto &a : assignments)
    {
        if (!assignData.empty()) assignData += "~";
        assignData += walEncode(a.first) + "~" + encodeValue(a.second);
    }

    file << "UPDATE|" << table << "|" << condData << "|" << assignData << "|0\n";
    file.flush();
}

void WALManager::logReplaceBegin(const std::string &table, const Condition *condition)
{
    std::string condData;
    if (condition)
        condData = walEncode(condition->column) + "~" + walEncode(condition->op) + "~" + encodeValue(condition->value);
    file << "REPLACE_BEGIN|" << table << "|" << condData << "|0\n";
    file.flush();
}

void WALManager::logReplaceRow(const std::string &table, const std::vector<Value> &values)
{
    std::string rowData;
    for (int i = 0; i < (int)values.size(); i++) {
        if (i > 0) rowData += "|";
        rowData += encodeValue(values[i]);
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
    // Reading to EOF leaves eofbit/failbit set, which would make every later write fail
    // silently: the first statement after startup used to be missing from the WAL
    file.clear();
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
    // A statement that fails on replay (e.g. a WHERE on a column that no longer exists) is
    // logged and skipped, like a parse error, instead of aborting the Engine constructor
    try {
        if (stmt)
            engine.execute(stmt.get());
    } catch (const std::exception &e) {
        std::cerr << "WAL replay error: " << e.what() << std::endl;
    }
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
            std::vector<std::string> rows;
            int j = i + 1;
            while (j < (int)entries.size() && entries[j].type == "REPLACE_ROW")
            {
                rows.push_back(valuesSQL(entries[j].rowData));
                j++;
            }
            i = j - 1;

            // Enter recovery mode so storage ops don't re-log to WAL
            engine.setRecoveryMode(true);

            // Clear the table
            replaySQL("DELETE FROM " + e.table, engine);

            // Re-insert the rows that should remain
            for (const auto &csv : rows)
                replaySQL("INSERT INTO " + e.table + " VALUES (" + csv + ")", engine);

            engine.setRecoveryMode(false);
            commit();
            continue;
        }

        std::string sql;

        if (e.type == "INSERT")
        {
            sql = "INSERT INTO " + e.table + " VALUES (" + valuesSQL(e.rowData) + ")";
        }
        else if (e.type == "DELETE")
        {
            sql = "DELETE FROM " + e.table;
            if (!e.rowData.empty())
                sql += conditionSQL(e.rowData);
        }
        else if (e.type == "UPDATE")
        {
            std::vector<std::string> fields = split(e.rowData, '|');
            std::string condPart  = fields.size() > 0 ? fields[0] : "";
            std::string assignPart = fields.size() > 1 ? fields[1] : "";

            std::vector<std::string> assigns = splitFields(assignPart, '~');
            if (assigns.size() < 2)
                continue;

            sql = "UPDATE " + e.table + " SET ";
            for (int k = 0; k + 1 < (int)assigns.size(); k += 2)
            {
                if (k > 0) sql += ", ";
                sql += walDecode(assigns[k]) + " = " + sqlLiteral(decodeValue(assigns[k + 1]));
            }

            if (!condPart.empty())
                sql += conditionSQL(condPart);
        }
        else
            continue;

        replaySQL(sql, engine);
        commit();
    }
}