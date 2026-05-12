#ifndef GUI_HPP
#define GUI_HPP

#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include "raylib.h"
#include "../AST/Row/Row.hpp"
#include "../Catalog/Catalog.hpp"
#include "../Engine/Engine.hpp"
#include "Colors.hpp"
#include "../Networking/NetworkServer.hpp"
#include "../Config/Config.hpp"

class GUI {
    public:
        GUI(Catalog &catalog, Engine &engine, const Config &config);
        void run();
    private:
        std::string input;
        int cursorPos;
        int resultsScrollIndex = 0;
        float backspaceRepeatTimer;
        std::vector<Row> results;
        std::vector<std::string> logs;
        std::mutex logsMutex;
        bool isDark;
        Catalog &catalog;
        Engine &engine;
        NetworkServer server;
        std::thread serverThread;
        bool serverRunning;

        Font regular;
        Font bold;
        Font italic;
        Font semibold;
        void handleInput();
        void executeQuery();
        void toggleServer();
        void drawInputPanel();
        void drawResultsPanel();
        void drawLogPanel();
};

#endif
