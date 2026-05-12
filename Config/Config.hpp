#ifndef CONFIG_HPP
#define CONFIG_HPP
#include <string>


struct Config {
    int port = 3000;
    int page_size = 4096;
    int cache_capacity = 128;
    int aux_max_failures = 3;
    int aux_timeout = 30;

    static Config load(const std::string& path = "config.json");
};


#endif
