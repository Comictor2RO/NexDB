#ifndef GUI_HPP
#define GUI_HPP

#include <vector>
#include <string>
#include "raylib.h"
#include "../AST/Row/Row.hpp"
#include "../Catalog/Catalog.hpp"
#include "../Engine/Engine.hpp"
#include "Colors.hpp"

class GUI {
    public:
        GUI(Catalog &catalog, Engine &engine);
        void run();
    private:
        std::string input;
        int cursorPos;
        int resultsScrollIndex = 0;
        float backspaceRepeatTimer;
        std::vector<Row> results;
        std::vector<std::string> logs;
        bool isDark;
        Catalog &catalog;
        Engine &engine;

        Font regular;
        Font bold;
        Font italic;
        Font semibold;
        void handleInput();
        void executeQuery();
        void drawInputPanel();
        void drawResultsPanel();
        void drawLogPanel();
};

#endif
