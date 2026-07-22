#include "Config.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {

// Parse a strictly-integer config value: the whole string must be a valid int,
// so "abc" and "123abc" are both rejected instead of silently truncated.
int parseInt(const std::string &key, const std::string &val)
{
    try {
        size_t pos = 0;
        int n = std::stoi(val, &pos);
        if (pos != val.size())
            throw std::runtime_error(
                "Invalid config value for '" + key + "': '" + val +
                "' is not a valid integer");
        return n;
    } catch (const std::invalid_argument &) {
        throw std::runtime_error(
            "Invalid config value for '" + key + "': '" + val +
            "' is not a valid integer");
    } catch (const std::out_of_range &) {
        throw std::runtime_error(
            "Invalid config value for '" + key + "': '" + val +
            "' is out of range");
    }
}

int parsePositive(const std::string &key, const std::string &val)
{
    int n = parseInt(key, val);
    if (n <= 0)
        throw std::runtime_error(
            "Invalid config value for '" + key + "': " + std::to_string(n) +
            " must be greater than 0");
    return n;
}

} // namespace

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

        if (key == "database") {
            config.database = val;
            continue;
        }

        if (key == "bypass_localhost") {
            if (val != "true" && val != "false" && val != "1" && val != "0")
                throw std::runtime_error(
                    "Invalid config value for 'bypass_localhost': '" + val +
                    "' must be true or false");
            config.bypass_localhost = (val == "true" || val == "1");
            continue;
        }

        if (key == "port") {
            int n = parseInt(key, val);
            if (n < 1 || n > 65535)
                throw std::runtime_error(
                    "Invalid config value for 'port': " + std::to_string(n) +
                    " must be between 1 and 65535");
            config.port = n;
        }
        else if (key == "page_size")
            config.page_size = parsePositive(key, val);
        else if (key == "cache_capacity")
            config.cache_capacity = parsePositive(key, val);
        else if (key == "aux_max_failures")
            config.aux_max_failures = parsePositive(key, val);
        else if (key == "aux_timeout")
            config.aux_timeout = parsePositive(key, val);
        else if (key == "thread_count")
            config.thread_count = parsePositive(key, val);
    }

    return config;
}
