#include "Config.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

Config Config::load(const std::string &path)
{
    Config config;
    std::ifstream file(path);
    if (!file.is_open())
        return config;

    std::string line;
    while (std::getline(file, line))
    {
        auto colon = line.find(':');
        if (colon == std::string::npos)
            continue;

        std::string key = line.substr(0, colon);
        std::string val = line.substr(colon + 1);

        auto trim = [](std::string &s) {
            const std::string chars = " \t\n\r\"";
            s.erase(0, s.find_first_not_of(chars));
            s.erase(s.find_last_not_of(chars + ",") + 1);
        };

        trim(key);
        trim(val);

        if (val.empty())
            continue;

        int n = std::stoi(val);

        if (key == "port")
            config.port = n;
        else if (key == "page_size")
            config.page_size = n;
        else if (key == "cache_capacity")
            config.cache_capacity = n;
        else if (key == "aux_max_failures")
            config.aux_max_failures = n;
        else if (key == "aux_timeout")
            config.aux_timeout = n;
    }

    return config;
}