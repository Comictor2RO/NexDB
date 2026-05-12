#include "Engine/Engine.hpp"
#include "Catalog/Catalog.hpp"
#include "Config/Config.hpp"
#include "GUI/GUI.hpp"

int main()
{
    Config config = Config::load("config.json");
    Catalog catalog;
    Engine engine(catalog, config.cache_capacity);
    GUI gui(catalog, engine, config);
    gui.run();
    return 0;
}
