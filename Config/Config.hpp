#ifndef CONFIG_HPP
#define CONFIG_HPP
#include <string>


struct Config {
    int port = 3000;
    int page_size = 4096;
    int cache_capacity = 128;
    int aux_max_failures = 3;
    int aux_timeout = 30;
    int thread_count = 4;
    int handshake_timeout = 5;
    bool bypass_localhost = true;
    bool allow_remote_db_admin = false;
    std::string database = "mydb";

    static Config load(const std::string& path = "config.json");
};


#endif
