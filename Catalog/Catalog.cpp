#include "Catalog.hpp"
#include "../StringUtils/StringUtils.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <mutex>

Catalog::Catalog(const std::string& catalogPath)
    : catalogFile(catalogPath)
{
    load();
}

bool Catalog::tableExists(const std::string &name) const
{
    std::shared_lock lock(mutex);
    return columns.find(name) != columns.end();
}

bool Catalog::createTable(const std::string &name, const std::vector<Columns> &columns)
{
    std::unique_lock lock(mutex);
    if (this->columns.count(name) || columns.empty())
        return false;
    this->columns[name] = columns;
    tableIds[name] = nextTableId++;
    save();
    return true;
}

void Catalog::raiseAutoHighWater(const std::string &table, long long value)
{
    std::unique_lock lock(mutex);
    auto it = columns.find(table);
    if (it == columns.end())
        return;
    for (Columns &c : it->second)
    {
        if (!c.auto_increment)
            continue;
        if (value > c.autoHighWater)
        {
            c.autoHighWater = value;
            save();
        }
        return;
    }
}

void Catalog::dropTable(const std::string &name)
{
    std::unique_lock lock(mutex);
    if (!this->columns.count(name))
        return;
    this->columns.erase(name);
    tableIds.erase(name);
    save();
}

std::vector<std::string> Catalog::getTableNames() const
{
    std::shared_lock lock(mutex);
    std::vector<std::string> names;
    for (const auto &p : columns)
        names.push_back(p.first);
    return names;
}

std::vector<Columns> Catalog::getColumns(const std::string &name) const
{
    std::shared_lock lock(mutex);
    auto it = columns.find(name);
    if (it == columns.end())
        return {};
    return it->second;
}

int Catalog::getTableId(const std::string &name) const
{
    std::shared_lock lock(mutex);
    auto it = tableIds.find(name);
    if (it == tableIds.end())
        return -1;
    return it->second;
}

void Catalog::load()
{
    std::ifstream f(catalogFile);
    if (!f.is_open())
        return;
    std::string line;
    int maxId = 0;
    while (std::getline(f, line))
    {
        if (line.empty())
            continue;
        std::vector<std::string> parts = split(line, '|');
        // New format: tableName|tableId|col1 TYPE1|col2 TYPE2|...
        if (parts.size() < 3)
            continue;
        std::string tableName = parts[0];
        int tid = std::stoi(parts[1]);
        std::vector<Columns> cols;
        for (size_t i = 2; i < parts.size(); ++i)
        {
            std::vector<std::string> pair = split(parts[i], ' ');
            if (pair.size() >= 2)
            {
                Columns c;
                c.name = pair[0];
                c.type = pair[1];
                for (size_t i = 2; i < pair.size(); i++) {
                    if (pair[i] == "unique")
                        c.unique = true;
                    else if (pair[i] == "auto_increment") {
                        c.unique = true;
                        c.auto_increment = true;
                    }
                    else if (pair[i].rfind("hwm=", 0) == 0) {
                        try { c.autoHighWater = std::stoll(pair[i].substr(4)); }
                        catch (...) {}
                    }
                }
                cols.push_back(c);
            }
        }
        if (!cols.empty())
        {
            columns[tableName] = cols;
            tableIds[tableName] = tid;
            if (tid > maxId)
                maxId = tid;
        }
    }
    nextTableId = maxId + 1;
}

void Catalog::save()
{
    auto parent = std::filesystem::path(catalogFile).parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);

    std::ofstream f(catalogFile);
    if (!f.is_open())
        return;
    for (const auto& p : columns)
    {
        auto idIt = tableIds.find(p.first);
        int tid = (idIt != tableIds.end()) ? idIt->second : 0;
        f << p.first << "|" << tid;
        for (size_t i = 0; i < p.second.size(); ++i)
        {
            const Columns &c = p.second[i];
            f << "|" << c.name << " " << c.type;
            // auto_increment implies unique, so one flag word is enough
            if (c.auto_increment)
            {
                f << " auto_increment";
                if (c.autoHighWater > 0)
                    f << " hwm=" << c.autoHighWater;
            }
            else if (c.unique)
                f << " unique";
        }
        f << "\n";
    }
}
