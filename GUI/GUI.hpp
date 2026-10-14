#ifndef GUI_HPP
#define GUI_HPP

#include <atomic>
#include <functional>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include "raylib.h"
#include "../AST/Row/Row.hpp"
#include "../Catalog/Catalog.hpp"
#include "../Engine/Engine.hpp"
#include "Colors.hpp"
#include "TextEditor.hpp"
#include "../Networking/NetworkServer.hpp"
#include "../Config/Config.hpp"

// VS Code-like window: a custom (undecorated) title bar, a database explorer on the left,
// a center panel split into results / query editor / log, a reserved panel on the right
// and a status bar. Side panels can be toggled and every split can be dragged.
class GUI {
    public:
        GUI(Engine &engine, const Config &config);
        void run();

    private:
        // Where everything is this frame, recomputed from the window size and panel state
        struct Layout {
            Rectangle titleBar, statusBar;
            Rectangle left, center, right;
            Rectangle leftSplitter, rightSplitter;
            Rectangle results, editorHeader, editor, logs;
            Rectangle resultsSplitter, logSplitter;
            Rectangle btnToggleLeft, btnToggleRight, btnMinimize, btnMaximize, btnClose;
        };

        enum class Drag {
            None, MoveWindow, ResizeWindow,
            LeftSplitter, RightSplitter, ResultsSplitter, LogSplitter
        };

        struct ExplorerTable {
            std::string name;
            std::vector<Columns> columns;
            bool expanded = false;
        };
        struct ExplorerDatabase {
            std::string name;
            std::vector<ExplorerTable> tables;
            bool expanded = false;
        };

        // Engine and server
        Engine &engine;
        NetworkServer server;
        std::thread serverThread;
        std::atomic<bool> serverRunning{false};

        // Look
        bool isDark = true;
        Theme theme;
        Font regular{};
        Font bold{};
        Font italic{};
        Font semibold{};

        // Log, written by the GUI and by the server's io threads
        std::vector<std::string> logs;
        std::mutex logsMutex;
        int logScroll = 0;            // lines from the bottom: 0 = newest

        // Last query result
        std::vector<Row> results;
        std::vector<std::string> resultsColumns;
        std::vector<float> columnWidths;
        std::string resultMessage;
        bool resultIsError = false;
        double lastQueryMs = 0;
        float resultsScrollX = 0;
        float resultsScrollY = 0;

        TextEditor editor;

        // Explorer: every database in databases/, its tables and their columns
        std::vector<ExplorerDatabase> explorer;
        std::string currentDatabase;
        float explorerScroll = 0;
        double lastExplorerRefresh = -100;

        // Panels and splits
        bool leftOpen = true;
        bool rightOpen = true;
        float leftAnim = 1;           // 0 = closed, 1 = open; animated towards the target
        float rightAnim = 1;
        float leftWidth = 250;
        float rightWidth = 260;
        float editorHeight = 170;     // editor section, header included
        float logHeight = 180;        // log section, header included

        // Window (custom title bar)
        Drag drag = Drag::None;
        int resizeEdges = 0;
        Vector2 dragStartCursor{};
        Rectangle dragStartWindow{};
        bool restoreOnDrag = false;   // dragging a maximized window restores it first
        bool maximized = false;
        bool nativeMaximize = false;  // fell back to raylib's MaximizeWindow
        Rectangle restoreRect{};
        double lastTitleClick = 0;
        bool quit = false;

        // Per frame
        bool mouseConsumed = false;
        int wantedCursor = MOUSE_CURSOR_DEFAULT;
        int currentCursor = MOUSE_CURSOR_DEFAULT;
        std::vector<std::function<void()>> deferred;   // run after drawing, see run()

        // Frame steps
        void handleShortcuts();
        void animatePanels();
        void clampSizes();
        Layout computeLayout() const;
        void handleWindowAndSplitters(const Layout &layout);
        void handleWheel(const Layout &layout);

        // Drawing
        void drawTitleBar(const Layout &layout);
        void drawStatusBar(const Layout &layout);
        void drawExplorer(Rectangle panel);
        void drawRightPanel(Rectangle panel);
        void drawCenterPanel(const Layout &layout);
        void drawResults(Rectangle section);
        void drawEditorHeader(Rectangle header);
        void drawLogs(Rectangle section);

        // Actions
        bool executeQuery(const std::string &sql);
        void runEditor();
        void toggleServer();
        void toggleMaximize();
        void refreshExplorer(bool wait);
        void computeColumnWidths();
        void addLog(const std::string &message);

        // Widget helpers
        Vector2 screenCursor() const;
        bool hovered(Rectangle r) const;
        bool clicked(Rectangle r);
        void drawPanel(Rectangle r) const;
        void drawPanelBorder(Rectangle r) const;   // drawn last, over the section headers
        void drawSectionHeader(Rectangle r, const char *title, bool roundedTop) const;
        void drawText(const Font &font, const std::string &s, float x, float y, float size, Color color) const;
        float measure(const Font &font, const std::string &s, float size) const;
        std::string fit(const Font &font, const std::string &s, float size, float maxWidth) const;
};

#endif
