#include "GUI.hpp"

#include <algorithm>

GUI::GUI(Catalog &catalog, Engine &engine, const Config &config)
    : catalog(catalog), engine(engine), server(engine, config.port, config.aux_max_failures, config.aux_timeout), serverRunning(false), isDark(true), cursorPos(0), backspaceRepeatTimer(0)
{}

void GUI::run()
{
    InitWindow(1280, 720, "NexDB");
    SetTargetFPS(60);
    regular = LoadFont("resources/asap-condensed/asap-condensed/AsapCondensed-Regular.ttf");
    bold = LoadFont("resources/asap-condensed/asap-condensed/AsapCondensed-Bold.ttf");
    italic = LoadFont("resources/asap-condensed/asap-condensed/AsapCondensed-Italic.ttf");
    semibold = LoadFont("resources/asap-condensed/asap-condensed/AsapCondensed-SemiBold.ttf");

    while (!WindowShouldClose())
    {
        handleInput();
        BeginDrawing();
        ClearBackground(isDark ? BLACK : WHITE);
        drawInputPanel();
        drawResultsPanel();
        drawLogPanel();
        EndDrawing();
    }
    if (serverRunning)
    {
        server.stop();
        serverThread.join();
        serverRunning = false;
    }
    UnloadFont(regular);
    UnloadFont(bold);
    UnloadFont(italic);
    CloseWindow();
}

void GUI::toggleServer()
{
    if (!serverRunning)
    {
        try {
            server.prepare();
        }
        catch (const std::exception &e) {
            logs.push_back("[SERVER ERROR] " + std::string(e.what()));
            return;
        }
        server.setLogCallback([this](const std::string& msg) {
            std::lock_guard<std::mutex> lock(logsMutex);
            logs.push_back(msg);
        });
        logs.push_back("[SERVER] Started on port " + std::to_string(server.getPort()) + ".");
        serverRunning = true;
        serverThread = std::thread([this]() {
            try {
                server.run();
            }
            catch (const std::exception &e) {
                serverRunning = false;
                std::lock_guard<std::mutex> lock(logsMutex);
                logs.push_back("[SERVER ERROR] " + std::string(e.what()));
            }
        });
    }
    else
    {
        server.stop();
        serverThread.join();
        serverRunning = false;
        logs.push_back("[SERVER] Stopped.");
    }
}

void GUI::handleInput()
{
    int ch = GetCharPressed();
    while (ch > 0)
    {
        input.insert(cursorPos, 1, (char)ch);
        cursorPos++;
        ch = GetCharPressed();
    }

    if (IsKeyPressed(KEY_LEFT) && cursorPos > 0)
        cursorPos--;
    if (IsKeyPressed(KEY_RIGHT) && cursorPos < (int)input.size())
        cursorPos++;

    if (IsKeyPressed(KEY_BACKSPACE) && cursorPos > 0)
    {
        input.erase(cursorPos - 1, 1);
        cursorPos--;
        backspaceRepeatTimer = 0.5f;
    }
    if (IsKeyDown(KEY_BACKSPACE) && cursorPos > 0)
    {
        backspaceRepeatTimer -= GetFrameTime();
        if (backspaceRepeatTimer <= 0)
        {
            input.erase(cursorPos - 1, 1);
            cursorPos--;
            backspaceRepeatTimer = 0.05f;
        }
    }
    if (!IsKeyDown(KEY_BACKSPACE))
        backspaceRepeatTimer = 0;

    float wheel = GetMouseWheelMove();
    if (wheel != 0)
    {
        if (GetMouseY() > 520)
        {
            // Scroll logs (bottom-anchored: 0 = newest, higher = older)
            logScrollIndex += (int)wheel * 3;
            if (logScrollIndex < 0) logScrollIndex = 0;
            int maxLogScroll = std::max(0, (int)logs.size() - (200 - 30) / 19);
            if (logScrollIndex > maxLogScroll) logScrollIndex = maxLogScroll;
        }
        else
        {
            // Scroll results
            resultsScrollIndex -= (int)wheel * 3;
            if (resultsScrollIndex < 0) resultsScrollIndex = 0;
            int maxScroll = (int)results.size() - 5;
            if (maxScroll < 0) maxScroll = 0;
            if (resultsScrollIndex > maxScroll) resultsScrollIndex = maxScroll;
        }
    }

    if (IsKeyPressed(KEY_ENTER) && !input.empty())
        executeQuery();
}

void GUI::executeQuery()
{
    logs.push_back("> " + input);
    try
    {
        results = engine.query(input);

        std::string upper = input;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);
        if (upper.rfind("DROP TABLE", 0) == 0)
            logs.push_back("[OK] Table dropped.");
        else if (!results.empty())
            logs.push_back("[OK] Rows returned: " + std::to_string(results.size()));
        else
            logs.push_back("[OK] Command executed.");
    }
    catch (const std::exception &e)
    {
        results.clear();
        logs.push_back("[ERROR] " + std::string(e.what()));
    }
    catch (...)
    {
        results.clear();
        logs.push_back("[ERROR] Query invalid sau tabela inexistenta.");
    }
    input.clear();
    cursorPos = 0;
    resultsScrollIndex = 0;
}

void GUI::drawInputPanel()
{
    int width = GetRenderWidth();
    DrawRectangle(0, 0, width, 80, isDark ? BG_DARK : BG_LIGHT);

    Rectangle inputBox = {10, 20, 1100, 40};
    DrawRectangleRec(inputBox, isDark ? INPUT_DARK : INPUT_LIGHT);
    DrawRectangleLinesEx(inputBox, 1, isDark ? INPUT_BORDER_DARK : INPUT_BORDER_LIGHT);
    DrawTextEx(semibold, input.c_str(), Vector2 {20, 28}, 23 , 1, isDark ? INPUT_TEXT_DARK : INPUT_TEXT_LIGHT);

    bool showCursor = (int)(GetTime() * 2) % 2 == 0;
    if (showCursor)
    {
        std::string leftPart = input.substr(0, cursorPos);
        int cursorX = 20 + (int)MeasureTextEx(regular, leftPart.c_str(), 23, 1).x;
        DrawTextEx(semibold, "|", Vector2{(float)cursorX, 28}, 23, 2, isDark ? INPUT_TEXT_DARK : INPUT_TEXT_LIGHT);
    }

    Rectangle runBtn = {1120, 20, 140, 40};
    bool hover = CheckCollisionPointRec(GetMousePosition(), runBtn);
    if (isDark)
        DrawRectangleRec(runBtn, hover ? BTN_BG_HOVER_DARK : BTN_BG_DARK);
    else
        DrawRectangleRec(runBtn, hover ? BTN_BG_HOVER_LIGHT : BTN_BG_LIGHT);
    DrawRectangleLinesEx(runBtn, 1, isDark ? (hover ? BTN_TEXT_HOVER_DARK : BTN_BORDER_LIGHT) : (hover ? BTN_TEXT_HOVER_LIGHT : BTN_BORDER_DARK));
    if (isDark)
        DrawTextEx(bold, "RUN", Vector2{1170, 30}, 20, 5, hover ? BTN_TEXT_HOVER_DARK : BTN_TEXT_DARK);
    else
        DrawTextEx(bold, "RUN", Vector2{1170, 30}, 20, 5, hover ? BTN_TEXT_HOVER_LIGHT : BTN_TEXT_LIGHT);

    if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && !input.empty())
        executeQuery();
}

void GUI::drawResultsPanel()
{
    Color bg     = isDark ? BG_DARK  : BG_LIGHT;
    Color header = isDark ? HEADER_BG_DARK  : HEADER_BG_LIGHT;
    Color even   = isDark ? EVEN_DARK  : EVEN_LIGHT;
    Color odd    = isDark ? ODD_DARK  : ODD_LIGHT;
    Color text   = isDark ? ROW_TEXT_DARK : ROW_TEXT_LIGHT;
    Color cell   = isDark ? CELL_DARK : CELL_LIGHT;

    DrawRectangle(0, 80, 1280, 440, bg);
    DrawRectangle(0, 80, 1280, 28, header);
    DrawTextEx(bold, "RESULTS", Vector2{10, 83}, 22, 2, isDark ? HEADER_TEXT_DARK : HEADER_TEXT_LIGHT);
    DrawTextEx(italic, ("Total rows: " + std::to_string(results.size())).c_str(), Vector2{120, 83}, 22, 2, isDark ? HEADER_TEXT_DARK : HEADER_TEXT_LIGHT);

    if (results.empty())
    {
        DrawTextEx(regular, "No results to display.", Vector2{10, 120}, 18, 2, text);
        return;
    }

    int startY    = 115;
    int rowHeight = 26;
    int maxRows   = (440 - 35) / rowHeight;

    // Asigură-te că scroll index-ul este valid
    if (resultsScrollIndex > (int)results.size() - 1) 
        resultsScrollIndex = std::max(0, (int)results.size() - 1);

    for (int i = 0; i < maxRows && (resultsScrollIndex + i) < (int)results.size(); i++)
    {
        int rowIndex = resultsScrollIndex + i;
        int y = startY + i * rowHeight;
        DrawRectangle(0, y, 1280, rowHeight, rowIndex % 2 == 0 ? even : odd);
        DrawRectangleLinesEx(Rectangle{0, (float)y, 1280, (float)rowHeight}, 1, cell);
        std::string rowStr;
        for (int j = 0; j < (int)results[rowIndex].values.size(); j++)
        {
            if (j > 0) rowStr += "   |   ";
            rowStr += results[rowIndex].values[j];
        }
        DrawTextEx(regular, rowStr.c_str(), Vector2{10, (float)y + 4}, 18, 2, text);
    }

    // Scrollbar simplu
    if ((int)results.size() > maxRows)
    {
        float scrollAreaHeight = 440 - 35;
        float barHeight = std::max(20.0f, scrollAreaHeight * ((float)maxRows / results.size()));
        float scrollProgress = (float)resultsScrollIndex / (results.size() - maxRows);
        float barY = startY + scrollProgress * (scrollAreaHeight - barHeight);
        
        DrawRectangle(1270, startY, 10, scrollAreaHeight, ColorAlpha(GRAY, 0.3f));
        DrawRectangle(1270, (int)barY, 10, (int)barHeight, isDark ? LIGHTGRAY : DARKGRAY);
    }
}

void GUI::drawLogPanel()
{
    Color bg     = isDark ? BG_DARK : BG_LIGHT;
    Color header = isDark ? HEADER_BG_DARK : HEADER_BG_LIGHT;

    DrawRectangle(0, 520, 1280, 200, bg);
    DrawRectangle(0, 520, 1280, 30, header);
    DrawTextEx(bold, "LOG", Vector2{10, 524}, 22, 2, isDark ? HEADER_TEXT_DARK : HEADER_TEXT_LIGHT);

    // Buton START/STOP server
    Rectangle serverBtn = {1070, 524, 110, 23};
    bool serverHover = CheckCollisionPointRec(GetMousePosition(), serverBtn);
    Color serverBtnColor = serverRunning ? RED : GREEN;
    DrawRectangleRec(serverBtn, ColorAlpha(serverBtnColor, serverHover ? 0.8f : 0.5f));
    DrawTextEx(bold, serverRunning ? "STOP SERVER" : "START SERVER", Vector2{1075, 527}, 16, 1, WHITE);
    if (serverHover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        toggleServer();

    // Buton toggle dark/light mode
    Rectangle toggleBtn = {1190, 524, 80, 23};
    bool hover = CheckCollisionPointRec(GetMousePosition(), toggleBtn);
    if (isDark)
        DrawRectangleRec(toggleBtn, hover ? MODE_BTN_HOVER_DARK : MODE_BTN_DARK);
    else
        DrawRectangleRec(toggleBtn, hover ? MODE_BTN_HOVER_LIGHT : MODE_BTN_LIGHT);
    DrawTextEx(bold, isDark ? "LIGHT" : "DARK", Vector2{1200, 527}, 18, 2, isDark ? MODE_BTN_TEXT_DARK : MODE_BTN_TEXT_LIGHT);
    if (hover && IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        isDark = !isDark;

    int lineHeight = 19;
    int maxLines   = (200 - 30) / lineHeight;

    std::lock_guard<std::mutex> lock(logsMutex);

    int maxScroll = std::max(0, (int)logs.size() - maxLines);
    if (logScrollIndex > maxScroll) logScrollIndex = maxScroll;
    int startIdx = maxScroll - logScrollIndex;

    for (int i = startIdx; i < startIdx + maxLines && i < (int)logs.size(); i++)
    {
        int y = 550 + (i - startIdx) * lineHeight;
        bool isError = logs[i].find("ERROR") != std::string::npos;
        Color c = isError ? (isDark ? ERROR_DARK : ERROR_LIGHT) : (isDark ? SUCCESS_DARK : SUCCESS_LIGHT);
        DrawTextEx(bold, logs[i].c_str(), Vector2{10, (float)y}, 20, 2, c);
    }

    if ((int)logs.size() > maxLines)
    {
        float scrollAreaHeight = 200 - 30;
        float barHeight = std::max(20.0f, scrollAreaHeight * ((float)maxLines / logs.size()));
        float scrollProgress = (float)logScrollIndex / maxScroll;
        float barY = 550 + (1.0f - scrollProgress) * (scrollAreaHeight - barHeight);
        DrawRectangle(1270, 550, 10, (int)scrollAreaHeight, ColorAlpha(GRAY, 0.3f));
        DrawRectangle(1270, (int)barY, 10, (int)barHeight, isDark ? LIGHTGRAY : DARKGRAY);
    }
}
