#include "Engine/Engine.hpp"
#include "Config/Config.hpp"
#include "GUI/GUI.hpp"
#include <filesystem>
#include <iostream>

int main()
{
    Config config;
    try {
        config = Config::load("config.json");
    } catch (const std::exception &e) {
        std::cerr << "Config error: " << e.what() << std::endl;
        return 1;
    }

    std::filesystem::create_directories("databases");
    std::string dbPath  = "databases/" + config.database + ".db";
    std::string catPath = "databases/" + config.database + ".cat";
    std::string walPath = "databases/" + config.database + ".wal";

    Engine engine(config.cache_capacity, dbPath, catPath, walPath);
    GUI gui(engine, config);
    gui.run();
    return 0;
}
