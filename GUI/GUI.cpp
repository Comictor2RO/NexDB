#include "GUI.hpp"
#include "Platform.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <optional>

// Layout, in pixels
static constexpr float TITLE_H     = 36;
static constexpr float STATUS_H    = 24;
static constexpr float PAD         = 8;     // between the window edge and the panels
static constexpr float GAP         = 8;     // between panels, doubles as the splitter grab area
static constexpr float HEADER_H    = 30;    // section header strips
static constexpr float RADIUS      = 10;    // panel corner radius
static constexpr float MIN_SIDE    = 170;
static constexpr float MIN_CENTER  = 380;
static constexpr float MIN_RESULTS = 110;
static constexpr float MIN_EDITOR  = HEADER_H + 60;
static constexpr float MIN_LOG     = HEADER_H + 50;
static constexpr int   MIN_WINDOW_W = 900;
static constexpr int   MIN_WINDOW_H = 560;
static constexpr float EDGE        = 5;     // window resize grab zone
static constexpr float SPACING     = 1;
static constexpr float EDITOR_FONT = 18;

// Window resize edges
static constexpr int EDGE_LEFT = 1, EDGE_RIGHT = 2, EDGE_TOP = 4, EDGE_BOTTOM = 8;

// ---------------------------------------------------------------- small helpers

static float roundness(Rectangle r, float radius)
{
    float m = std::min(r.width, r.height);
    return m <= 0 ? 0 : std::min(1.0f, 2 * radius / m);
}

static bool equalsIgnoreCase(const std::string &a, const std::string &b)
{
    return a.size() == b.size()
        && std::equal(a.begin(), a.end(), b.begin(), [](unsigned char x, unsigned char y) {
               return std::tolower(x) == std::tolower(y);
           });
}

static std::string trim(const std::string &s)
{
    size_t from = s.find_first_not_of(" \t\r\n");
    if (from == std::string::npos)
        return "";
    size_t to = s.find_last_not_of(" \t\r\n");
    return s.substr(from, to - from + 1);
}

static std::string toUpper(std::string s)
{
    for (char &c : s)
        c = (char)std::toupper((unsigned char)c);
    return s;
}

// The log is one line per entry, so a multi-line query is shown with its whitespace collapsed
static std::string oneLine(const std::string &s)
{
    std::string out;
    bool space = false;
    for (char c : s)
    {
        if (c == '\n' || c == '\r' || c == '\t' || c == ' ')
            space = true;
        else
        {
            if (space && !out.empty())
                out += ' ';
            out += c;
            space = false;
        }
    }
    return out;
}

// Splits the editor text on ';' outside string literals. A doubled '' toggles the quote
// state twice, so escaped quotes need no special case.
static std::vector<std::string> splitStatements(const std::string &sql)
{
    std::vector<std::string> statements;
    std::string current;
    bool inQuote = false;
    for (char c : sql)
    {
        if (c == '\'')
            inQuote = !inQuote;
        if (c == ';' && !inQuote)
        {
            std::string s = trim(current);
            if (!s.empty())
                statements.push_back(s);
            current.clear();
            continue;
        }
        current += c;
    }
    std::string s = trim(current);
    if (!s.empty())
        statements.push_back(s);
    return statements;
}

static std::string formatMs(double ms)
{
    char buf[32];
    std::snprintf(buf, sizeof(buf), ms < 10 ? "%.2f ms" : "%.0f ms", ms);
    return buf;
}

static Font loadFont(const char *path)
{
    // ASCII, Romanian diacritics (comma and cedilla forms) and the few symbols the UI uses
    std::vector<int> codepoints;
    for (int c = 32; c < 127; c++)
        codepoints.push_back(c);
    for (int c : {0x0102, 0x0103, 0x00C2, 0x00E2, 0x00CE, 0x00EE, 0x0218, 0x0219, 0x021A, 0x021B,
                  0x015E, 0x015F, 0x0162, 0x0163, 0x00B7, 0x2014, 0x2026, 0x2022})
        codepoints.push_back(c);
    Font font = LoadFontEx(path, 32, codepoints.data(), (int)codepoints.size());
    SetTextureFilter(font.texture, TEXTURE_FILTER_BILINEAR);
    return font;
}

static Color logColor(const std::string &line, const Theme &theme)
{
    if (line.find("ERROR") != std::string::npos)
        return theme.error;
    if (line.rfind("[OK]", 0) == 0)
        return theme.success;
    if (line.rfind("> ", 0) == 0)
        return theme.text;
    if (line.rfind("[", 0) == 0)
        return theme.info;
    return theme.textDim;
}

static void drawChevron(float x, float cy, bool expanded, Color c)
{
    if (expanded)
    {
        DrawLineEx(Vector2{x + 1, cy - 2}, Vector2{x + 5, cy + 2}, 1.6f, c);
        DrawLineEx(Vector2{x + 5, cy + 2}, Vector2{x + 9, cy - 2}, 1.6f, c);
    }
    else
    {
        DrawLineEx(Vector2{x + 3, cy - 4}, Vector2{x + 7, cy}, 1.6f, c);
        DrawLineEx(Vector2{x + 7, cy}, Vector2{x + 3, cy + 4}, 1.6f, c);
    }
}

static void drawDatabaseIcon(float x, float y, Color c)
{
    DrawRectangleRec(Rectangle{x, y + 3, 12, 8}, c);
    DrawEllipse((int)(x + 6), (int)(y + 11), 6, 2.5f, c);
    DrawEllipse((int)(x + 6), (int)(y + 3), 6, 2.5f, ColorBrightness(c, 0.35f));
}

static void drawTableIcon(float x, float y, Color c)
{
    DrawRectangleLinesEx(Rectangle{x, y, 13, 11}, 1.2f, c);
    DrawRectangleRec(Rectangle{x, y, 13, 3.5f}, c);
    DrawLineEx(Vector2{x + 6.5f, y + 3}, Vector2{x + 6.5f, y + 11}, 1, c);
    DrawLineEx(Vector2{x, y + 7}, Vector2{x + 13, y + 7}, 1, c);
}

// ---------------------------------------------------------------- setup

GUI::GUI(Engine &engine, const Config &config)
    : engine(engine),
      server(engine, config.port, config.aux_max_failures, config.aux_timeout, config.bypass_localhost,
             config.thread_count, config.allow_remote_db_admin, config.handshake_timeout)
{}

void GUI::run()
{
    // Undecorated: the title bar, its buttons, dragging and edge resizing are drawn and
    // handled here. RESIZABLE keeps raylib updating the framebuffer on SetWindowSize.
    SetConfigFlags(FLAG_WINDOW_UNDECORATED | FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT);
    InitWindow(1280, 760, "NexDB");
    SetWindowMinSize(MIN_WINDOW_W, MIN_WINDOW_H);
    SetExitKey(KEY_NULL);   // Esc must not close the window
    SetTargetFPS(60);
    int monitor = GetCurrentMonitor();
    SetWindowPosition((GetMonitorWidth(monitor) - 1280) / 2, (GetMonitorHeight(monitor) - 760) / 2);

    regular  = loadFont("resources/asap-condensed/asap-condensed/AsapCondensed-Regular.ttf");
    bold     = loadFont("resources/asap-condensed/asap-condensed/AsapCondensed-Bold.ttf");
    italic   = loadFont("resources/asap-condensed/asap-condensed/AsapCondensed-Italic.ttf");
    semibold = loadFont("resources/asap-condensed/asap-condensed/AsapCondensed-SemiBold.ttf");

    refreshExplorer(true);

    while (!WindowShouldClose() && !quit)
    {
        theme = isDark ? darkTheme() : lightTheme();
        mouseConsumed = false;
        wantedCursor = MOUSE_CURSOR_DEFAULT;

        handleShortcuts();
        animatePanels();
        clampSizes();
        Layout layout = computeLayout();
        handleWindowAndSplitters(layout);
        handleWheel(layout);

        bool editorOwnsMouse = drag == Drag::None && !mouseConsumed;
        if (editor.update(layout.editor, regular, EDITOR_FONT, editorOwnsMouse))
            deferred.push_back([this] { runEditor(); });
        if (drag == Drag::None && CheckCollisionPointRec(GetMousePosition(), layout.editor))
            wantedCursor = MOUSE_CURSOR_IBEAM;

        // Network clients can change the schema too, so the explorer polls
        if (GetTime() - lastExplorerRefresh > 2.0)
            refreshExplorer(false);

        BeginDrawing();
        ClearBackground(theme.windowBg);
        if (layout.left.width > 1)
            drawExplorer(layout.left);
        drawCenterPanel(layout);
        if (layout.right.width > 1)
            drawRightPanel(layout.right);
        drawTitleBar(layout);
        drawStatusBar(layout);
        EndDrawing();

        // Clicks only queue their action; running it here means a query never replaces
        // results, logs or the explorer while a draw loop is iterating over them
        std::vector<std::function<void()>> actions;
        actions.swap(deferred);
        for (auto &action : actions)
            action();

        if (wantedCursor != currentCursor)
        {
            SetMouseCursor(wantedCursor);
            currentCursor = wantedCursor;
        }
    }

    if (serverRunning)
    {
        server.stop();
        serverRunning = false;
    }
    if (serverThread.joinable())
        serverThread.join();
    UnloadFont(regular);
    UnloadFont(bold);
    UnloadFont(italic);
    UnloadFont(semibold);
    CloseWindow();
}

// ---------------------------------------------------------------- frame steps

void GUI::handleShortcuts()
{
    bool ctrl = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool alt  = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);

    // Same shortcuts as VS Code: Ctrl+B primary side bar, Ctrl+Alt+B secondary
    if (ctrl && IsKeyPressed(KEY_B))
    {
        if (alt)
            rightOpen = !rightOpen;
        else
            leftOpen = !leftOpen;
    }
    if (IsKeyPressed(KEY_F5))
        deferred.push_back([this] { runEditor(); });
}

void GUI::animatePanels()
{
    float k = std::min(1.0f, GetFrameTime() * 14);
    auto step = [k](float &value, bool open) {
        float target = open ? 1.0f : 0.0f;
        value += (target - value) * k;
        if (std::fabs(target - value) < 0.002f)
            value = target;
    };
    step(leftAnim, leftOpen);
    step(rightAnim, rightOpen);
}

void GUI::clampSizes()
{
    float w = (float)GetScreenWidth();
    float h = (float)GetScreenHeight();

    // Side panels: keep at least MIN_CENTER for the center, shrinking both proportionally
    leftWidth = std::max(MIN_SIDE, leftWidth);
    rightWidth = std::max(MIN_SIDE, rightWidth);
    float maxSides = w - 2 * PAD - 2 * GAP - MIN_CENTER;
    float used = (leftOpen ? leftWidth : 0) + (rightOpen ? rightWidth : 0);
    if (used > maxSides && used > 0)
    {
        float scale = std::max(0.0f, maxSides) / used;
        if (leftOpen)
            leftWidth = std::max(MIN_SIDE, leftWidth * scale);
        if (rightOpen)
            rightWidth = std::max(MIN_SIDE, rightWidth * scale);
    }

    // Center sections: when the window gets shorter, the log gives up space first
    float centerH = h - TITLE_H - STATUS_H - 2 * PAD;
    editorHeight = std::max(MIN_EDITOR, editorHeight);
    logHeight = std::max(MIN_LOG, logHeight);
    float overflow = editorHeight + logHeight + MIN_RESULTS - centerH;
    if (overflow > 0)
    {
        float fromLog = std::min(overflow, logHeight - MIN_LOG);
        logHeight -= fromLog;
        overflow -= fromLog;
        editorHeight = std::max(MIN_EDITOR, editorHeight - overflow);
    }
}

GUI::Layout GUI::computeLayout() const
{
    Layout l{};
    float w = (float)GetScreenWidth();
    float h = (float)GetScreenHeight();

    l.titleBar  = Rectangle{0, 0, w, TITLE_H};
    l.statusBar = Rectangle{0, h - STATUS_H, w, STATUS_H};

    // Window buttons are 46 px wide, like the native Windows ones
    const float bw = 46;
    l.btnClose       = Rectangle{w - bw, 0, bw, TITLE_H};
    l.btnMaximize    = Rectangle{w - 2 * bw, 0, bw, TITLE_H};
    l.btnMinimize    = Rectangle{w - 3 * bw, 0, bw, TITLE_H};
    l.btnToggleRight = Rectangle{w - 3 * bw - 12 - 30, 4, 30, TITLE_H - 8};
    l.btnToggleLeft  = Rectangle{l.btnToggleRight.x - 32, 4, 30, TITLE_H - 8};

    float top = TITLE_H + PAD;
    float height = h - TITLE_H - STATUS_H - 2 * PAD;
    float leftW = leftWidth * leftAnim;
    float rightW = rightWidth * rightAnim;

    l.left = Rectangle{PAD, top, leftW, height};
    float centerX = PAD + leftW + GAP * leftAnim;
    float rightX = w - PAD - rightW;
    l.right = Rectangle{rightX, top, rightW, height};
    float centerRight = rightX - GAP * rightAnim;
    l.center = Rectangle{centerX, top, std::max(0.0f, centerRight - centerX), height};

    // Splitters only exist while the panel is fully open
    if (leftOpen && leftAnim >= 1)
        l.leftSplitter = Rectangle{l.left.x + l.left.width, top, GAP, height};
    if (rightOpen && rightAnim >= 1)
        l.rightSplitter = Rectangle{centerRight, top, GAP, height};

    float cx = l.center.x, cw = l.center.width;
    float logTop = l.center.y + l.center.height - logHeight;
    float editorTop = logTop - editorHeight;
    l.results      = Rectangle{cx, l.center.y, cw, editorTop - l.center.y};
    l.editorHeader = Rectangle{cx, editorTop, cw, HEADER_H};
    l.editor       = Rectangle{cx + 1, editorTop + HEADER_H, cw - 2, editorHeight - HEADER_H};
    l.logs         = Rectangle{cx, logTop, cw, logHeight};
    l.resultsSplitter = Rectangle{cx, editorTop - 3, cw, 6};
    l.logSplitter     = Rectangle{cx, logTop - 3, cw, 6};
    return l;
}

Vector2 GUI::screenCursor() const
{
    int x, y;
    if (Platform::getCursorScreenPos(x, y))
        return Vector2{(float)x, (float)y};
    Vector2 p = GetWindowPosition();
    Vector2 m = GetMousePosition();
    return Vector2{p.x + m.x, p.y + m.y};
}

void GUI::handleWindowAndSplitters(const Layout &l)
{
    Vector2 mouse = GetMousePosition();
    float w = (float)GetScreenWidth();
    float h = (float)GetScreenHeight();

    if (drag == Drag::None)
    {
        // Which window edges the mouse is on (none while maximized, like Windows)
        int edges = 0;
        bool inWindow = mouse.x >= 0 && mouse.y >= 0 && mouse.x < w && mouse.y < h;
        if (!maximized && inWindow)
        {
            if (mouse.x < EDGE)      edges |= EDGE_LEFT;
            if (mouse.x >= w - EDGE) edges |= EDGE_RIGHT;
            if (mouse.y < EDGE)      edges |= EDGE_TOP;
            if (mouse.y >= h - EDGE) edges |= EDGE_BOTTOM;
        }

        bool onButton = CheckCollisionPointRec(mouse, l.btnClose) || CheckCollisionPointRec(mouse, l.btnMaximize)
                     || CheckCollisionPointRec(mouse, l.btnMinimize) || CheckCollisionPointRec(mouse, l.btnToggleLeft)
                     || CheckCollisionPointRec(mouse, l.btnToggleRight);

        if (edges)
        {
            bool nwse = (edges == (EDGE_LEFT | EDGE_TOP)) || (edges == (EDGE_RIGHT | EDGE_BOTTOM));
            bool nesw = (edges == (EDGE_RIGHT | EDGE_TOP)) || (edges == (EDGE_LEFT | EDGE_BOTTOM));
            if (nwse)                                  wantedCursor = MOUSE_CURSOR_RESIZE_NWSE;
            else if (nesw)                             wantedCursor = MOUSE_CURSOR_RESIZE_NESW;
            else if (edges & (EDGE_LEFT | EDGE_RIGHT)) wantedCursor = MOUSE_CURSOR_RESIZE_EW;
            else                                       wantedCursor = MOUSE_CURSOR_RESIZE_NS;
        }
        else if (CheckCollisionPointRec(mouse, l.leftSplitter) || CheckCollisionPointRec(mouse, l.rightSplitter))
            wantedCursor = MOUSE_CURSOR_RESIZE_EW;
        else if (CheckCollisionPointRec(mouse, l.resultsSplitter) || CheckCollisionPointRec(mouse, l.logSplitter))
            wantedCursor = MOUSE_CURSOR_RESIZE_NS;

        if (!IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
            return;

        Vector2 pos = GetWindowPosition();
        if (edges)
        {
            drag = Drag::ResizeWindow;
            resizeEdges = edges;
            dragStartCursor = screenCursor();
            dragStartWindow = Rectangle{pos.x, pos.y, w, h};
            mouseConsumed = true;
        }
        else if (CheckCollisionPointRec(mouse, l.titleBar) && !onButton)
        {
            double now = GetTime();
            if (now - lastTitleClick < 0.35)
            {
                toggleMaximize();
                lastTitleClick = 0;
            }
            else
            {
                lastTitleClick = now;
                drag = Drag::MoveWindow;
                dragStartCursor = screenCursor();
                dragStartWindow = Rectangle{pos.x, pos.y, w, h};
                restoreOnDrag = maximized;
            }
            mouseConsumed = true;
        }
        else if (CheckCollisionPointRec(mouse, l.leftSplitter))    { drag = Drag::LeftSplitter;    mouseConsumed = true; }
        else if (CheckCollisionPointRec(mouse, l.rightSplitter))   { drag = Drag::RightSplitter;   mouseConsumed = true; }
        else if (CheckCollisionPointRec(mouse, l.resultsSplitter)) { drag = Drag::ResultsSplitter; mouseConsumed = true; }
        else if (CheckCollisionPointRec(mouse, l.logSplitter))     { drag = Drag::LogSplitter;     mouseConsumed = true; }
        return;
    }

    if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT))
    {
        drag = Drag::None;
        return;
    }

    float centerH = l.center.height;
    switch (drag)
    {
        case Drag::MoveWindow:
        {
            Vector2 c = screenCursor();
            float dx = c.x - dragStartCursor.x;
            float dy = c.y - dragStartCursor.y;
            if (restoreOnDrag)
            {
                if (std::fabs(dx) + std::fabs(dy) < 6)
                    break;
                // Restore under the cursor, keeping the grab point proportional, like Windows
                float rel = (dragStartCursor.x - dragStartWindow.x) / dragStartWindow.width;
                float grabY = dragStartCursor.y - dragStartWindow.y;
                toggleMaximize();
                float nx = c.x - restoreRect.width * rel;
                float ny = c.y - grabY;
                SetWindowPosition((int)nx, (int)ny);
                dragStartWindow = Rectangle{nx, ny, restoreRect.width, restoreRect.height};
                dragStartCursor = c;
                restoreOnDrag = false;
                break;
            }
            SetWindowPosition((int)(dragStartWindow.x + dx), (int)(dragStartWindow.y + dy));
            break;
        }
        case Drag::ResizeWindow:
        {
            Vector2 c = screenCursor();
            float dx = c.x - dragStartCursor.x;
            float dy = c.y - dragStartCursor.y;
            Rectangle r = dragStartWindow;
            if (resizeEdges & EDGE_LEFT)
            {
                r.width = std::max((float)MIN_WINDOW_W, dragStartWindow.width - dx);
                r.x = dragStartWindow.x + dragStartWindow.width - r.width;
            }
            if (resizeEdges & EDGE_RIGHT)
                r.width = std::max((float)MIN_WINDOW_W, dragStartWindow.width + dx);
            if (resizeEdges & EDGE_TOP)
            {
                r.height = std::max((float)MIN_WINDOW_H, dragStartWindow.height - dy);
                r.y = dragStartWindow.y + dragStartWindow.height - r.height;
            }
            if (resizeEdges & EDGE_BOTTOM)
                r.height = std::max((float)MIN_WINDOW_H, dragStartWindow.height + dy);
            SetWindowPosition((int)r.x, (int)r.y);
            SetWindowSize((int)r.width, (int)r.height);
            break;
        }
        case Drag::LeftSplitter:
        {
            float maxW = w - 2 * PAD - 2 * GAP - MIN_CENTER - (rightOpen ? rightWidth : 0);
            leftWidth = std::clamp(mouse.x - PAD - GAP / 2, MIN_SIDE, std::max(MIN_SIDE, maxW));
            break;
        }
        case Drag::RightSplitter:
        {
            float maxW = w - 2 * PAD - 2 * GAP - MIN_CENTER - (leftOpen ? leftWidth : 0);
            rightWidth = std::clamp(w - PAD - mouse.x - GAP / 2, MIN_SIDE, std::max(MIN_SIDE, maxW));
            break;
        }
        case Drag::ResultsSplitter:
            editorHeight = std::clamp(l.logs.y - mouse.y, MIN_EDITOR,
                                      std::max(MIN_EDITOR, centerH - logHeight - MIN_RESULTS));
            break;
        case Drag::LogSplitter:
            logHeight = std::clamp(l.center.y + l.center.height - mouse.y, MIN_LOG,
                                   std::max(MIN_LOG, centerH - editorHeight - MIN_RESULTS));
            break;
        case Drag::None:
            break;
    }

    switch (drag)
    {
        case Drag::ResizeWindow:
            if (resizeEdges == (EDGE_LEFT | EDGE_TOP) || resizeEdges == (EDGE_RIGHT | EDGE_BOTTOM))
                wantedCursor = MOUSE_CURSOR_RESIZE_NWSE;
            else if (resizeEdges == (EDGE_RIGHT | EDGE_TOP) || resizeEdges == (EDGE_LEFT | EDGE_BOTTOM))
                wantedCursor = MOUSE_CURSOR_RESIZE_NESW;
            else if (resizeEdges & (EDGE_LEFT | EDGE_RIGHT))
                wantedCursor = MOUSE_CURSOR_RESIZE_EW;
            else
                wantedCursor = MOUSE_CURSOR_RESIZE_NS;
            break;
        case Drag::LeftSplitter:
        case Drag::RightSplitter:
            wantedCursor = MOUSE_CURSOR_RESIZE_EW;
            break;
        case Drag::ResultsSplitter:
        case Drag::LogSplitter:
            wantedCursor = MOUSE_CURSOR_RESIZE_NS;
            break;
        default:
            break;
    }
}

void GUI::handleWheel(const Layout &l)
{
    Vector2 wheel = GetMouseWheelMoveV();
    if ((wheel.x == 0 && wheel.y == 0) || drag != Drag::None)
        return;

    Vector2 mouse = GetMousePosition();
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    if (CheckCollisionPointRec(mouse, l.left))
        explorerScroll -= wheel.y * 72;
    else if (CheckCollisionPointRec(mouse, l.results))
    {
        if (shift)
            resultsScrollX -= wheel.y * 60;
        else
            resultsScrollY -= wheel.y * 78;
        resultsScrollX -= wheel.x * 60;
    }
    else if (CheckCollisionPointRec(mouse, l.logs))
        logScroll += (int)(wheel.y * 3);
    // the editor scrolls itself in TextEditor::update
}

void GUI::toggleMaximize()
{
    if (!maximized)
    {
        Vector2 p = GetWindowPosition();
        restoreRect = Rectangle{p.x, p.y, (float)GetScreenWidth(), (float)GetScreenHeight()};
        int x, y, w, h;
        // Fill the work area instead of the whole monitor, so the taskbar stays visible
        if (Platform::getWorkArea(GetWindowHandle(), x, y, w, h))
        {
            SetWindowPosition(x, y);
            SetWindowSize(w, h);
            nativeMaximize = false;
        }
        else
        {
            MaximizeWindow();
            nativeMaximize = true;
        }
        maximized = true;
    }
    else
    {
        if (nativeMaximize)
            RestoreWindow();
        else
        {
            SetWindowSize((int)restoreRect.width, (int)restoreRect.height);
            SetWindowPosition((int)restoreRect.x, (int)restoreRect.y);
        }
        maximized = false;
    }
}

// ---------------------------------------------------------------- widget helpers

bool GUI::hovered(Rectangle r) const
{
    return drag == Drag::None && CheckCollisionPointRec(GetMousePosition(), r);
}

bool GUI::clicked(Rectangle r)
{
    if (!mouseConsumed && hovered(r) && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        mouseConsumed = true;
        return true;
    }
    return false;
}

void GUI::drawPanel(Rectangle r) const
{
    DrawRectangleRounded(r, roundness(r, RADIUS), 16, theme.panel);
}

void GUI::drawPanelBorder(Rectangle r) const
{
    DrawRectangleRoundedLinesEx(r, roundness(r, RADIUS), 16, 1, theme.border);
}

void GUI::drawSectionHeader(Rectangle r, const char *title, bool roundedTop) const
{
    if (roundedTop)
    {
        // Rounded at the top only: a rounded rectangle with its lower half squared off
        DrawRectangleRounded(r, roundness(r, RADIUS), 16, theme.panelHeader);
        DrawRectangleRec(Rectangle{r.x, r.y + RADIUS, r.width, r.height - RADIUS}, theme.panelHeader);
    }
    else
    {
        DrawRectangleRec(r, theme.panelHeader);
        DrawLineEx(Vector2{r.x, r.y + 0.5f}, Vector2{r.x + r.width, r.y + 0.5f}, 1, theme.border);
    }
    DrawLineEx(Vector2{r.x, r.y + r.height - 0.5f}, Vector2{r.x + r.width, r.y + r.height - 0.5f}, 1, theme.border);
    DrawTextEx(semibold, title, Vector2{std::round(r.x + 12), std::round(r.y + (r.height - 15) / 2)}, 15, 1.5f, theme.textDim);
}

void GUI::drawText(const Font &font, const std::string &s, float x, float y, float size, Color color) const
{
    DrawTextEx(font, s.c_str(), Vector2{std::round(x), std::round(y)}, size, SPACING, color);
}

float GUI::measure(const Font &font, const std::string &s, float size) const
{
    if (s.empty())
        return 0;
    return MeasureTextEx(font, s.c_str(), size, SPACING).x;
}

std::string GUI::fit(const Font &font, const std::string &s, float size, float maxWidth) const
{
    if (measure(font, s, size) <= maxWidth)
        return s;
    std::string cut = s;
    while (!cut.empty())
    {
        // drop one UTF-8 character
        do { cut.pop_back(); } while (!cut.empty() && ((unsigned char)cut.back() & 0xC0) == 0x80);
        if (measure(font, cut + "…", size) <= maxWidth)
            return cut + "…";
    }
    return "";
}

// ---------------------------------------------------------------- title bar and status bar

void GUI::drawTitleBar(const Layout &l)
{
    float w = l.titleBar.width;
    DrawRectangleRec(l.titleBar, theme.titleBar);
    DrawLineEx(Vector2{0, TITLE_H - 0.5f}, Vector2{w, TITLE_H - 0.5f}, 1, theme.border);

    // Logo and name
    Rectangle logo = {12, 9, 18, 18};
    DrawRectangleRounded(logo, 0.35f, 8, theme.accent);
    float nw = measure(bold, "N", 15);
    drawText(bold, "N", logo.x + (logo.width - nw) / 2, logo.y + 1, 15, theme.accentText);
    drawText(semibold, "NexDB", 38, 9, 17, theme.text);

    // Centered title, like the file / folder title in VS Code
    std::string title = currentDatabase.empty() ? "NexDB" : currentDatabase + " — NexDB";
    float tw = measure(regular, title, 15);
    drawText(regular, title, (w - tw) / 2, 10, 15, theme.textDim);

    // Panel toggles
    auto toggleIcon = [&](Rectangle b, bool leftSide, bool open) {
        if (hovered(b))
            DrawRectangleRounded(b, 0.3f, 6, theme.hover);
        Rectangle box = {std::round(b.x + (b.width - 16) / 2), std::round(b.y + (b.height - 12) / 2), 16, 12};
        Color c = open ? theme.text : theme.textDim;
        DrawRectangleLinesEx(box, 1.2f, c);
        Rectangle strip = leftSide ? Rectangle{box.x, box.y, 5, box.height}
                                   : Rectangle{box.x + box.width - 5, box.y, 5, box.height};
        if (open)
            DrawRectangleRec(strip, c);
        else
            DrawRectangleLinesEx(strip, 1, c);
    };
    toggleIcon(l.btnToggleLeft, true, leftOpen);
    toggleIcon(l.btnToggleRight, false, rightOpen);
    if (clicked(l.btnToggleLeft))
        leftOpen = !leftOpen;
    if (clicked(l.btnToggleRight))
        rightOpen = !rightOpen;

    // Minimize, maximize / restore, close
    Color icon = theme.text;
    if (hovered(l.btnMinimize))
        DrawRectangleRec(l.btnMinimize, theme.hover);
    {
        float cx = l.btnMinimize.x + l.btnMinimize.width / 2, cy = TITLE_H / 2;
        DrawLineEx(Vector2{cx - 5, cy}, Vector2{cx + 5, cy}, 1, icon);
    }
    if (hovered(l.btnMaximize))
        DrawRectangleRec(l.btnMaximize, theme.hover);
    {
        float cx = l.btnMaximize.x + l.btnMaximize.width / 2, cy = TITLE_H / 2;
        if (maximized)
        {
            // restore: two overlapping squares
            DrawRectangleLinesEx(Rectangle{cx - 3, cy - 5, 8, 8}, 1, icon);
            DrawRectangleRec(Rectangle{cx - 5, cy - 3, 8, 8}, hovered(l.btnMaximize) ? theme.hover : theme.titleBar);
            DrawRectangleLinesEx(Rectangle{cx - 5, cy - 3, 8, 8}, 1, icon);
        }
        else
            DrawRectangleLinesEx(Rectangle{cx - 5, cy - 5, 10, 10}, 1, icon);
    }
    bool closeHover = hovered(l.btnClose);
    if (closeHover)
        DrawRectangleRec(l.btnClose, theme.closeHover);
    {
        float cx = l.btnClose.x + l.btnClose.width / 2, cy = TITLE_H / 2;
        Color c = closeHover ? WHITE : icon;
        DrawLineEx(Vector2{cx - 5, cy - 5}, Vector2{cx + 5, cy + 5}, 1.2f, c);
        DrawLineEx(Vector2{cx + 5, cy - 5}, Vector2{cx - 5, cy + 5}, 1.2f, c);
    }

    if (clicked(l.btnMinimize))
        deferred.push_back([] { MinimizeWindow(); });
    if (clicked(l.btnMaximize))
        deferred.push_back([this] { toggleMaximize(); });
    if (clicked(l.btnClose))
        quit = true;
}

void GUI::drawStatusBar(const Layout &l)
{
    Rectangle bar = l.statusBar;
    DrawRectangleRec(bar, theme.statusBar);
    const float size = 14;
    float textY = bar.y + (bar.height - size) / 2 - 1;

    // An item is a clickable label with a hover background
    auto item = [&](float x, const std::string &label, float extraLeft, bool clickable) {
        Rectangle r = {x, bar.y, measure(regular, label, size) + 16 + extraLeft, bar.height};
        if (clickable && hovered(r))
        {
            DrawRectangleRec(r, Fade(WHITE, 0.14f));
            wantedCursor = MOUSE_CURSOR_POINTING_HAND;
        }
        drawText(regular, label, x + 8 + extraLeft, textY, size, theme.statusText);
        return r;
    };

    // Left: server state (click to start/stop) and the active database
    bool running = serverRunning;
    std::string serverLabel = running ? "Server: port " + std::to_string(server.getPort()) : "Server stopped";
    Rectangle serverItem = item(4, serverLabel, 12, true);
    DrawCircle((int)(serverItem.x + 12), (int)(bar.y + bar.height / 2), 4,
               running ? Color{90, 210, 120, 255} : Fade(theme.statusText, 0.45f));
    if (clicked(serverItem))
        deferred.push_back([this] { toggleServer(); });

    Rectangle dbItem = item(serverItem.x + serverItem.width + 6, "DB: " + currentDatabase, 0, false);
    (void)dbItem;

    // Right: theme toggle and the editor cursor position
    std::string themeLabel = isDark ? "Theme: Dark" : "Theme: Light";
    float themeW = measure(regular, themeLabel, size) + 16;
    Rectangle themeItem = item(bar.width - 4 - themeW, themeLabel, 0, true);
    if (clicked(themeItem))
        isDark = !isDark;

    std::string position = "Ln " + std::to_string(editor.getLine()) + ", Col " + std::to_string(editor.getColumn());
    float posW = measure(regular, position, size) + 16;
    item(themeItem.x - 6 - posW, position, 0, false);
}

// ---------------------------------------------------------------- side panels

void GUI::drawExplorer(Rectangle panel)
{
    drawPanel(panel);
    BeginScissorMode((int)panel.x, (int)panel.y, (int)std::ceil(panel.width), (int)panel.height);

    Rectangle header = {panel.x, panel.y, panel.width, HEADER_H};
    drawSectionHeader(header, "EXPLORER", true);

    // Refresh button
    Rectangle refresh = {header.x + header.width - 32, header.y + 4, 26, header.height - 8};
    if (hovered(refresh))
    {
        DrawRectangleRounded(refresh, 0.3f, 6, theme.hover);
        wantedCursor = MOUSE_CURSOR_POINTING_HAND;
    }
    Vector2 rc = {refresh.x + refresh.width / 2, refresh.y + refresh.height / 2};
    DrawRing(rc, 4.2f, 5.8f, 60, 360, 20, theme.textDim);
    DrawTriangle(Vector2{rc.x + 1, rc.y - 8}, Vector2{rc.x + 1, rc.y - 2}, Vector2{rc.x + 5, rc.y - 5}, theme.textDim);
    if (clicked(refresh))
        deferred.push_back([this] { refreshExplorer(true); });
    EndScissorMode();

    Rectangle body = {panel.x + 4, panel.y + HEADER_H + 4, std::max(0.0f, panel.width - 8), panel.height - HEADER_H - 10};
    BeginScissorMode((int)body.x, (int)body.y, (int)std::ceil(body.width), (int)body.height);

    const float rowH = 24;
    Vector2 mouse = GetMousePosition();
    bool mouseInBody = CheckCollisionPointRec(mouse, body);
    float y = body.y - explorerScroll;
    auto visible = [&](float rowY) { return rowY + rowH >= body.y && rowY <= body.y + body.height; };

    if (explorer.empty())
        drawText(italic, "No databases in databases/", body.x + 8, body.y + 6, 16, theme.textDim);

    for (ExplorerDatabase &db : explorer)
    {
        bool active = equalsIgnoreCase(db.name, currentDatabase);
        Rectangle row = {body.x, y, body.width, rowH};
        float x = body.x + 6;
        if (visible(y))
        {
            bool h = mouseInBody && hovered(row);
            if (h)
            {
                DrawRectangleRounded(row, 0.3f, 6, theme.hover);
                wantedCursor = MOUSE_CURSOR_POINTING_HAND;
            }
            drawChevron(x, y + rowH / 2, db.expanded, theme.textDim);
            drawDatabaseIcon(x + 16, y + 5, active ? theme.accent : theme.textDim);
            float nameX = x + 36;
            std::string tag = active ? "active" : "";
            float tagW = active ? measure(regular, tag, 13) + 10 : 0;
            drawText(active ? semibold : regular, fit(active ? semibold : regular, db.name, 17, body.x + body.width - nameX - tagW - 6),
                     nameX, y + 3, 17, active ? theme.accent : theme.text);
            if (active)
                drawText(regular, tag, body.x + body.width - tagW, y + 5, 13, theme.textDim);

            if (mouseInBody && clicked(row))
            {
                // The chevron, or the active database, only expands; another database is opened with USE
                if (mouse.x < x + 14 || active)
                    db.expanded = !db.expanded;
                else
                {
                    std::string name = db.name;
                    deferred.push_back([this, name] {
                        if (executeQuery("USE " + name))
                            for (auto &e : explorer)
                                if (e.name == name)
                                    e.expanded = true;
                    });
                }
            }
        }
        y += rowH;
        if (!db.expanded)
            continue;

        if (db.tables.empty())
        {
            if (visible(y))
                drawText(italic, "no tables", x + 36, y + 4, 15, theme.textDim);
            y += rowH;
        }

        for (ExplorerTable &table : db.tables)
        {
            Rectangle trow = {body.x, y, body.width, rowH};
            float tx = x + 16;
            if (visible(y))
            {
                if (mouseInBody && hovered(trow))
                {
                    DrawRectangleRounded(trow, 0.3f, 6, theme.hover);
                    wantedCursor = MOUSE_CURSOR_POINTING_HAND;
                }
                drawChevron(tx, y + rowH / 2, table.expanded, theme.textDim);
                drawTableIcon(tx + 16, y + 6, theme.info);
                drawText(regular, fit(regular, table.name, 17, body.x + body.width - tx - 40), tx + 36, y + 3, 17, theme.text);

                if (mouseInBody && clicked(trow))
                {
                    // Chevron expands the columns; the name runs SELECT *, switching database first if needed
                    if (mouse.x < tx + 14)
                        table.expanded = !table.expanded;
                    else
                    {
                        std::string dbName = db.name, tableName = table.name;
                        deferred.push_back([this, active, dbName, tableName] {
                            if (active || executeQuery("USE " + dbName))
                                executeQuery("SELECT * FROM " + tableName);
                        });
                    }
                }
            }
            y += rowH;
            if (!table.expanded)
                continue;

            for (const Columns &column : table.columns)
            {
                if (visible(y))
                {
                    float cx = tx + 22;
                    DrawCircle((int)(cx + 4), (int)(y + rowH / 2), 2.5f, theme.textDim);
                    float typeW = measure(regular, column.type, 14);
                    drawText(regular, fit(regular, column.name, 16, body.x + body.width - cx - typeW - 28), cx + 14, y + 4, 16, theme.text);
                    drawText(regular, column.type, body.x + body.width - typeW - 8, y + 5, 14, theme.textDim);
                }
                y += rowH;
            }
        }
    }
    EndScissorMode();

    // Clamp the scroll now that the content height is known, and show a scrollbar
    float contentH = y + explorerScroll - body.y;
    float maxScroll = std::max(0.0f, contentH - body.height);
    explorerScroll = std::clamp(explorerScroll, 0.0f, maxScroll);
    if (maxScroll > 0)
    {
        float barH = std::max(24.0f, body.height * body.height / contentH);
        float barY = body.y + (body.height - barH) * (explorerScroll / maxScroll);
        DrawRectangleRounded(Rectangle{panel.x + panel.width - 7, barY, 4, barH}, 1, 4, theme.scrollbar);
    }

    drawPanelBorder(panel);
}

void GUI::drawRightPanel(Rectangle panel)
{
    drawPanel(panel);
    BeginScissorMode((int)panel.x, (int)panel.y, (int)std::ceil(panel.width), (int)panel.height);
    drawSectionHeader(Rectangle{panel.x, panel.y, panel.width, HEADER_H}, "PANEL", true);

    const char *line1 = "Reserved for future features";
    const char *line2 = "Ctrl+Alt+B to hide";
    float w1 = measure(italic, line1, 16), w2 = measure(regular, line2, 14);
    float cy = panel.y + panel.height / 2;
    drawText(italic, line1, panel.x + (panel.width - w1) / 2, cy - 14, 16, theme.textDim);
    drawText(regular, line2, panel.x + (panel.width - w2) / 2, cy + 8, 14, theme.textDim);
    EndScissorMode();
    drawPanelBorder(panel);
}

// ---------------------------------------------------------------- center panel

void GUI::drawCenterPanel(const Layout &l)
{
    drawPanel(l.center);
    drawResults(l.results);
    drawEditorHeader(l.editorHeader);
    editor.draw(theme);
    drawLogs(l.logs);

    // Splitter highlight while hovered or dragged, like the VS Code sash
    Vector2 mouse = GetMousePosition();
    auto sashActive = [&](Rectangle r, Drag d) {
        return drag == d || (drag == Drag::None && !mouseConsumed && CheckCollisionPointRec(mouse, r));
    };
    if (sashActive(l.resultsSplitter, Drag::ResultsSplitter))
        DrawRectangleRec(Rectangle{l.center.x + 1, l.editorHeader.y - 1, l.center.width - 2, 2}, theme.accent);
    if (sashActive(l.logSplitter, Drag::LogSplitter))
        DrawRectangleRec(Rectangle{l.center.x + 1, l.logs.y - 1, l.center.width - 2, 2}, theme.accent);
    if (sashActive(l.leftSplitter, Drag::LeftSplitter))
        DrawRectangleRec(Rectangle{l.leftSplitter.x + GAP / 2 - 1, l.leftSplitter.y + RADIUS, 2, l.leftSplitter.height - 2 * RADIUS}, theme.accent);
    if (sashActive(l.rightSplitter, Drag::RightSplitter))
        DrawRectangleRec(Rectangle{l.rightSplitter.x + GAP / 2 - 1, l.rightSplitter.y + RADIUS, 2, l.rightSplitter.height - 2 * RADIUS}, theme.accent);

    drawPanelBorder(l.center);
}

void GUI::computeColumnWidths()
{
    // Measured once per result, from the header and up to 500 rows, then clamped
    columnWidths.assign(resultsColumns.size(), 0);
    size_t sample = std::min(results.size(), (size_t)500);
    for (size_t c = 0; c < resultsColumns.size(); c++)
    {
        float w = measure(semibold, resultsColumns[c], 16) + 24;
        for (size_t r = 0; r < sample; r++)
        {
            if (c >= results[r].values.size())
                continue;
            const Value &v = results[r].values[c];
            w = std::max(w, measure(v ? regular : italic, v ? *v : "NULL", 16) + 24);
        }
        columnWidths[c] = std::clamp(w, 70.0f, 360.0f);
    }
}

void GUI::drawResults(Rectangle s)
{
    Rectangle header = {s.x, s.y, s.width, HEADER_H};
    drawSectionHeader(header, "RESULTS", true);

    if (!resultsColumns.empty())
    {
        std::string info = std::to_string(results.size()) + (results.size() == 1 ? " row" : " rows")
                         + "  ·  " + formatMs(lastQueryMs);
        float iw = measure(regular, info, 14);
        drawText(regular, info, header.x + header.width - iw - 14, header.y + 7, 14, theme.textDim);
    }

    Rectangle body = {s.x + 1, s.y + HEADER_H, std::max(0.0f, s.width - 2), std::max(0.0f, s.height - HEADER_H)};
    if (resultsColumns.empty())
    {
        bool none = resultMessage.empty();
        std::string msg = none ? "Run a query to see its results here." : resultMessage;
        Color c = none ? theme.textDim : (resultIsError ? theme.error : theme.success);
        drawText(none ? italic : regular, fit(regular, msg, 17, body.width - 28), body.x + 14, body.y + 14, 17, c);
        return;
    }

    const float rowH = 26, headH = 28, indexW = 52;
    float totalW = indexW;
    for (float w : columnWidths)
        totalW += w;
    float viewH = body.height - headH;
    float contentH = results.size() * rowH;
    float maxY = std::max(0.0f, contentH - viewH);
    float maxX = std::max(0.0f, totalW - body.width);
    resultsScrollY = std::clamp(resultsScrollY, 0.0f, maxY);
    resultsScrollX = std::clamp(resultsScrollX, 0.0f, maxX);

    Vector2 mouse = GetMousePosition();
    bool mouseInBody = CheckCollisionPointRec(mouse, body);
    BeginScissorMode((int)body.x, (int)body.y, (int)body.width, (int)body.height);

    // Rows: only the visible ones are drawn
    int first = (int)(resultsScrollY / rowH);
    int count = (int)(viewH / rowH) + 2;
    for (int i = first; i < (int)results.size() && i < first + count; i++)
    {
        float y = body.y + headH + i * rowH - resultsScrollY;
        Rectangle row = {body.x, y, body.width, rowH};
        Color bg = i % 2 == 0 ? theme.rowEven : theme.rowOdd;
        if (mouseInBody && hovered(row) && mouse.y >= body.y + headH)
            bg = theme.hover;
        DrawRectangleRec(row, bg);

        float x = body.x - resultsScrollX;
        std::string index = std::to_string(i + 1);
        drawText(regular, index, x + indexW - 12 - measure(regular, index, 14), y + 6, 14, theme.textDim);
        x += indexW;
        for (size_t c = 0; c < columnWidths.size(); c++)
        {
            float w = columnWidths[c];
            if (x + w >= body.x && x <= body.x + body.width && c < results[i].values.size())
            {
                // SQL NULL is italic and dim, so it never looks like the string 'NULL'
                const Value &v = results[i].values[c];
                if (v)
                    drawText(regular, fit(regular, *v, 16, w - 16), x + 8, y + 4, 16, theme.text);
                else
                    drawText(italic, "NULL", x + 8, y + 4, 16, theme.textDim);
            }
            DrawLineEx(Vector2{x + w - 0.5f, y}, Vector2{x + w - 0.5f, y + rowH}, 1, theme.gridLine);
            x += w;
        }
    }

    // Sticky header row
    DrawRectangleRec(Rectangle{body.x, body.y, body.width, headH}, theme.panel);
    float x = body.x - resultsScrollX;
    drawText(semibold, "#", x + indexW - 12 - measure(semibold, "#", 15), body.y + 6, 15, theme.textDim);
    x += indexW;
    for (size_t c = 0; c < columnWidths.size(); c++)
    {
        float w = columnWidths[c];
        drawText(semibold, fit(semibold, resultsColumns[c], 16, w - 16), x + 8, body.y + 5, 16, theme.text);
        DrawLineEx(Vector2{x + w - 0.5f, body.y}, Vector2{x + w - 0.5f, body.y + headH}, 1, theme.gridLine);
        x += w;
    }
    DrawLineEx(Vector2{body.x, body.y + headH - 0.5f}, Vector2{body.x + body.width, body.y + headH - 0.5f}, 1, theme.border);
    EndScissorMode();

    // Scrollbars
    if (maxY > 0)
    {
        float barH = std::max(24.0f, viewH * viewH / contentH);
        float barY = body.y + headH + (viewH - barH) * (resultsScrollY / maxY);
        DrawRectangleRounded(Rectangle{body.x + body.width - 7, barY, 5, barH}, 1, 4, theme.scrollbar);
    }
    if (maxX > 0)
    {
        float barW = std::max(24.0f, body.width * body.width / totalW);
        float barX = body.x + (body.width - barW) * (resultsScrollX / maxX);
        DrawRectangleRounded(Rectangle{barX, body.y + body.height - 7, barW, 5}, 1, 4, theme.scrollbar);
    }
}

void GUI::drawEditorHeader(Rectangle header)
{
    drawSectionHeader(header, "QUERY", false);

    Rectangle runBtn = {header.x + header.width - 84, header.y + 4, 72, header.height - 8};
    bool h = hovered(runBtn);
    if (h)
        wantedCursor = MOUSE_CURSOR_POINTING_HAND;
    DrawRectangleRounded(runBtn, 0.35f, 8, h ? ColorBrightness(theme.accent, 0.15f) : theme.accent);
    // Play triangle; raylib wants the vertices counter-clockwise on screen
    float tx = runBtn.x + 14, ty = runBtn.y + runBtn.height / 2;
    DrawTriangle(Vector2{tx, ty - 5}, Vector2{tx, ty + 5}, Vector2{tx + 8, ty}, theme.accentText);
    drawText(bold, "Run", tx + 16, runBtn.y + (runBtn.height - 16) / 2 - 1, 16, theme.accentText);
    if (clicked(runBtn))
        deferred.push_back([this] { runEditor(); });

    std::string hint = "Ctrl+Enter";
    float hw = measure(regular, hint, 14);
    drawText(regular, hint, runBtn.x - hw - 12, header.y + 7, 14, theme.textDim);
}

void GUI::drawLogs(Rectangle s)
{
    Rectangle header = {s.x, s.y, s.width, HEADER_H};
    drawSectionHeader(header, "LOG", false);

    Rectangle clearBtn = {header.x + header.width - 64, header.y + 4, 52, header.height - 8};
    if (hovered(clearBtn))
    {
        DrawRectangleRounded(clearBtn, 0.3f, 6, theme.hover);
        wantedCursor = MOUSE_CURSOR_POINTING_HAND;
    }
    float cw = measure(regular, "Clear", 14);
    drawText(regular, "Clear", clearBtn.x + (clearBtn.width - cw) / 2, clearBtn.y + 3, 14, theme.textDim);
    if (clicked(clearBtn))
        deferred.push_back([this] {
            std::lock_guard<std::mutex> lock(logsMutex);
            logs.clear();
            logScroll = 0;
        });

    Rectangle body = {s.x + 1, s.y + HEADER_H + 4, std::max(0.0f, s.width - 2), std::max(0.0f, s.height - HEADER_H - 8)};
    const float lineH = 20;
    int maxLines = std::max(1, (int)(body.height / lineH));

    std::lock_guard<std::mutex> lock(logsMutex);
    int total = (int)logs.size();
    int maxScroll = std::max(0, total - maxLines);
    logScroll = std::clamp(logScroll, 0, maxScroll);
    int startIdx = std::max(0, total - maxLines - logScroll);

    BeginScissorMode((int)body.x, (int)body.y, (int)body.width, (int)body.height);
    for (int i = startIdx; i < total && i < startIdx + maxLines; i++)
    {
        float y = body.y + (i - startIdx) * lineH;
        drawText(regular, logs[i], body.x + 12, y, 16, logColor(logs[i], theme));
    }
    EndScissorMode();

    if (maxScroll > 0)
    {
        float viewH = body.height;
        float barH = std::max(20.0f, viewH * maxLines / (float)total);
        // Bottom-anchored: logScroll 0 means the bar sits at the bottom
        float barY = body.y + (viewH - barH) * (1.0f - (float)logScroll / maxScroll);
        DrawRectangleRounded(Rectangle{body.x + body.width - 7, barY, 5, barH}, 1, 4, theme.scrollbar);
    }
}

// ---------------------------------------------------------------- actions

void GUI::addLog(const std::string &message)
{
    std::lock_guard<std::mutex> lock(logsMutex);
    logs.push_back(message);
}

bool GUI::executeQuery(const std::string &sql)
{
    addLog("> " + oneLine(sql));
    auto start = std::chrono::steady_clock::now();
    try
    {
        std::vector<Row> rows;
        std::vector<std::string> columns;
        std::optional<size_t> affected;
        std::string switchedTo;
        {
            // The same lock the network clients take, so a GUI query never runs at the
            // same time as a remote one
            std::lock_guard<std::mutex> lock(server.getEngineMutex());
            rows = engine.query(sql);
            columns = engine.getLastColumns();
            affected = engine.getLastAffected();
            // A USE from the GUI must not leave a pending switch behind, or the next
            // network client would get the "switch" frame as the answer to its own query
            switchedTo = engine.consumePendingSwitch();
        }
        lastQueryMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

        results = std::move(rows);
        resultsColumns = std::move(columns);
        resultsScrollX = resultsScrollY = 0;
        resultIsError = false;
        computeColumnWidths();

        std::string upper = toUpper(trim(sql));
        if (!switchedTo.empty())
            resultMessage = "Switched to database " + switchedTo;
        else if (!resultsColumns.empty())
            resultMessage = "Rows returned: " + std::to_string(results.size());
        else if (affected)
            resultMessage = "Rows affected: " + std::to_string(*affected);
        else if (upper.rfind("CREATE TABLE", 0) == 0)
            resultMessage = "Table created";
        else if (upper.rfind("DROP TABLE", 0) == 0)
            resultMessage = "Table dropped";
        else if (upper.rfind("CREATE DATABASE", 0) == 0)
            resultMessage = "Database created";
        else if (upper.rfind("DROP DATABASE", 0) == 0)
            resultMessage = "Database dropped";
        else
            resultMessage = "Command executed";
        addLog("[OK] " + resultMessage + " (" + formatMs(lastQueryMs) + ")");
        refreshExplorer(true);
        return true;
    }
    catch (const std::exception &e)
    {
        results.clear();
        resultsColumns.clear();
        columnWidths.clear();
        resultMessage = e.what();
        resultIsError = true;
        addLog("[ERROR] " + std::string(e.what()));
    }
    catch (...)
    {
        results.clear();
        resultsColumns.clear();
        columnWidths.clear();
        resultMessage = "Invalid query or table does not exist";
        resultIsError = true;
        addLog("[ERROR] " + resultMessage);
    }
    return false;
}

void GUI::runEditor()
{
    // ';' separates statements; they run in order and the script stops at the first error.
    // The last one's result stays on screen
    std::string text = editor.getRunText();
    std::vector<std::string> statements = splitStatements(text);
    if (statements.empty())
        return;
    editor.addToHistory(trim(text));
    for (const std::string &statement : statements)
        if (!executeQuery(statement))
            break;
}

void GUI::toggleServer()
{
    if (!serverRunning)
    {
        // A server thread that died on an exception is still joinable
        if (serverThread.joinable())
            serverThread.join();

        // Set before prepare(): prepare() logs the generated auth token, which used to
        // go into an empty callback and never reach the log
        server.setLogCallback([this](const std::string &msg) { addLog(msg); });
        try {
            server.prepare();
        }
        catch (const std::exception &e) {
            addLog("[SERVER ERROR] " + std::string(e.what()));
            return;
        }
        addLog("[SERVER] Started on port " + std::to_string(server.getPort()) + ".");
        serverRunning = true;
        serverThread = std::thread([this]() {
            try {
                server.run();
            }
            catch (const std::exception &e) {
                serverRunning = false;
                addLog("[SERVER ERROR] " + std::string(e.what()));
            }
        });
    }
    else
    {
        server.stop();
        serverThread.join();
        serverRunning = false;
        addLog("[SERVER] Stopped.");
    }
}

void GUI::refreshExplorer(bool wait)
{
    // Takes the engine lock: a network USE swaps the engine's catalog. The periodic refresh
    // only tries, so a long remote query never freezes a frame
    std::unique_lock<std::mutex> lock(server.getEngineMutex(), std::defer_lock);
    if (wait)
        lock.lock();
    else if (!lock.try_lock())
        return;
    lastExplorerRefresh = GetTime();

    namespace fs = std::filesystem;
    currentDatabase = engine.getCurrentDatabase();
    std::vector<ExplorerDatabase> fresh;
    std::error_code ec;
    for (auto it = fs::directory_iterator("databases", ec); !ec && it != fs::directory_iterator(); it.increment(ec))
    {
        const fs::path &path = it->path();
        if (path.extension() != ".db")
            continue;

        ExplorerDatabase db;
        db.name = path.stem().string();
        auto addTables = [&db](const Catalog &catalog) {
            for (const std::string &name : catalog.getTableNames())
                db.tables.push_back(ExplorerTable{name, catalog.getColumns(name)});
        };
        if (equalsIgnoreCase(db.name, currentDatabase))
            addTables(engine.getCatalog());   // the live catalog
        else
            addTables(Catalog((path.parent_path() / (db.name + ".cat")).string()));   // read-only load
        fresh.push_back(std::move(db));
    }
    std::sort(fresh.begin(), fresh.end(), [](const ExplorerDatabase &a, const ExplorerDatabase &b) {
        return toUpper(a.name) < toUpper(b.name);
    });

    // Keep what the user expanded; a database seen for the first time opens if it is active
    for (ExplorerDatabase &db : fresh)
    {
        auto old = std::find_if(explorer.begin(), explorer.end(), [&](const ExplorerDatabase &e) { return e.name == db.name; });
        if (old == explorer.end())
        {
            db.expanded = equalsIgnoreCase(db.name, currentDatabase);
            continue;
        }
        db.expanded = old->expanded;
        for (ExplorerTable &table : db.tables)
            for (const ExplorerTable &oldTable : old->tables)
                if (oldTable.name == table.name)
                    table.expanded = oldTable.expanded;
    }
    explorer = std::move(fresh);
}
