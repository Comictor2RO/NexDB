#include "Catalog.hpp"
#include "../StringUtils/StringUtils.hpp"
#include <fstream>
#include <sstream>
#include <filesystem>

const char* Catalog::CATALOG_FILE = "catalog.dat";

Catalog::Catalog()
{
    load();
}

bool Catalog::tableExists(const std::string &name) const
{
    return columns.find(name) != columns.end();
}

void Catalog::createTable(const std::string &name, const std::vector<Columns> &columns)
{
    if (tableExists(name))
        return;
    this->columns[name] = columns;
    save();
}

std::vector<Columns> Catalog::getColumns(const std::string &name) const
{
    if (!tableExists(name))
        return {};
    return columns.at(name);
}

void Catalog::load()
{
    std::ifstream f(CATALOG_FILE);
    if (!f.is_open())
        return;
    std::string line;
    while (std::getline(f, line))
    {
        if (line.empty())
            continue;
        std::vector<std::string> parts = split(line, '|');
        if (parts.size() < 2)
            continue;
        std::string tableName = parts[0];
        std::vector<Columns> cols;
        for (size_t i = 1; i < parts.size(); ++i)
        {
            std::vector<std::string> pair = split(parts[i], ' ');
            if (pair.size() >= 2)
            {
                Columns c;
                c.name = pair[0];
                c.type = pair[1];
                cols.push_back(c);
            }
        }
        // We are keeping in .dat only the files .db which exists
        if (!cols.empty() && std::filesystem::exists(tableName + ".db"))
            columns[tableName] = cols;
    }
    save(); // update .dat
}

void Catalog::save()
{
    std::ofstream f(CATALOG_FILE);
    if (!f.is_open())
        return;
    for (const auto& p : columns)
    {
        f << p.first << "|";
        for (size_t i = 0; i < p.second.size(); ++i)
        {
            if (i > 0)
                f << "|";
            f << p.second[i].name << " " << p.second[i].type;
        }
        f << "\n";
    }
}
