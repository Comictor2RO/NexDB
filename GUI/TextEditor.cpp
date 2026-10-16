#include "TextEditor.hpp"
#include "Zoom.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

static constexpr float SPACING = 1.0f;
static constexpr size_t MAX_UNDO = 200;

// Same keyword list as the lexer, plus the type aliases the parser accepts
static bool isKeyword(const std::string &upper)
{
    static const char *keywords[] = {
        "SELECT", "INSERT", "DELETE", "FROM", "WHERE", "INTO", "VALUES", "CREATE", "TABLE",
        "INT", "STRING", "DROP", "UPDATE", "SET", "USE", "DATABASE", "FLOAT", "BOOL",
        "NULL", "IS", "NOT", "TEXT", "BOOLEAN"
    };
    for (const char *k : keywords)
        if (upper == k)
            return true;
    return false;
}

static bool isContinuationByte(unsigned char c) { return (c & 0xC0) == 0x80; }
static bool isWordChar(unsigned char c) { return std::isalnum(c) || c == '_' || c >= 0x80; }

static size_t nextChar(const std::string &s, size_t pos)
{
    if (pos >= s.size())
        return s.size();
    pos++;
    while (pos < s.size() && isContinuationByte(s[pos]))
        pos++;
    return pos;
}

static size_t prevChar(const std::string &s, size_t pos)
{
    if (pos == 0)
        return 0;
    pos--;
    while (pos > 0 && isContinuationByte(s[pos]))
        pos--;
    return pos;
}

static size_t wordLeft(const std::string &s, size_t pos)
{
    while (pos > 0 && !isWordChar(s[pos - 1]) && s[pos - 1] != '\n')
        pos--;
    while (pos > 0 && isWordChar(s[pos - 1]))
        pos--;
    return pos;
}

static size_t wordRight(const std::string &s, size_t pos)
{
    while (pos < s.size() && !isWordChar(s[pos]) && s[pos] != '\n')
        pos++;
    while (pos < s.size() && isWordChar(s[pos]))
        pos++;
    return pos;
}

// ---------------------------------------------------------------- geometry

float TextEditor::lineHeight() const { return std::round(fontSize * 1.4f); }

float TextEditor::gutterWidth() const
{
    std::string digits = std::to_string(std::max(99, lineCount()));
    return measure(digits) + 28;
}

Rectangle TextEditor::textArea() const
{
    float gutter = gutterWidth();
    return Rectangle{bounds.x + gutter, bounds.y + 6, std::max(0.0f, bounds.width - gutter - 10),
                     std::max(0.0f, bounds.height - 12)};
}

int TextEditor::lineCount() const
{
    return 1 + (int)std::count(text.begin(), text.end(), '\n');
}

int TextEditor::lineNumber(size_t pos) const
{
    return (int)std::count(text.begin(), text.begin() + (long long)std::min(pos, text.size()), '\n');
}

size_t TextEditor::lineBegin(size_t pos) const
{
    if (pos == 0)
        return 0;
    size_t nl = text.rfind('\n', pos - 1);
    return nl == std::string::npos ? 0 : nl + 1;
}

size_t TextEditor::lineEnd(size_t pos) const
{
    size_t nl = text.find('\n', pos);
    return nl == std::string::npos ? text.size() : nl;
}

size_t TextEditor::lineStartByIndex(int line) const
{
    size_t pos = 0;
    for (int i = 0; i < line; i++)
    {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos)
            return pos;
        pos = nl + 1;
    }
    return pos;
}

float TextEditor::measure(const std::string &s) const
{
    if (s.empty())
        return 0;
    return MeasureTextEx(font, s.c_str(), fontSize, SPACING).x;
}

float TextEditor::xOf(size_t pos) const
{
    size_t begin = lineBegin(pos);
    if (pos == begin)
        return 0;
    // MeasureTextEx leaves out the spacing after the last glyph, DrawTextEx does not
    return measure(text.substr(begin, pos - begin)) + SPACING;
}

size_t TextEditor::posAtX(size_t lineStartPos, float x) const
{
    size_t end = lineEnd(lineStartPos);
    size_t pos = lineStartPos;
    float prevX = 0;
    while (pos < end)
    {
        size_t next = nextChar(text, pos);
        float nextX = measure(text.substr(lineStartPos, next - lineStartPos)) + SPACING;
        if (x < (prevX + nextX) / 2)
            return pos;
        prevX = nextX;
        pos = next;
    }
    return end;
}

size_t TextEditor::posFromPoint(Vector2 point) const
{
    Rectangle area = textArea();
    int line = (int)std::floor((point.y - area.y + scrollY) / lineHeight());
    line = std::clamp(line, 0, lineCount() - 1);
    return posAtX(lineStartByIndex(line), point.x - area.x + scrollX);
}

// ---------------------------------------------------------------- editing

void TextEditor::beginEdit(EditKind kind)
{
    if (kind != lastEdit || kind == EditKind::Other)
    {
        undoStack.push_back({text, cursor, anchor});
        if (undoStack.size() > MAX_UNDO)
            undoStack.erase(undoStack.begin());
    }
    redoStack.clear();
    lastEdit = kind;
}

void TextEditor::afterEdit()
{
    ensureCursorVisible = true;
    preferredX = -1;
    blinkStart = GetTime();
    historyIndex = -1;
}

void TextEditor::replaceSelection(const std::string &s, EditKind kind)
{
    beginEdit(kind);
    size_t from = selectionStart();
    text.replace(from, selectionEnd() - from, s);
    cursor = anchor = from + s.size();
    afterEdit();
}

void TextEditor::eraseRange(size_t from, size_t to, EditKind kind)
{
    if (from >= to)
        return;
    beginEdit(kind);
    text.erase(from, to - from);
    cursor = anchor = from;
    afterEdit();
}

void TextEditor::moveTo(size_t pos, bool extend)
{
    cursor = std::min(pos, text.size());
    if (!extend)
        anchor = cursor;
    preferredX = -1;
    lastEdit = EditKind::None;
    ensureCursorVisible = true;
    blinkStart = GetTime();
}

void TextEditor::moveVertical(int direction, bool extend)
{
    if (preferredX < 0)
        preferredX = xOf(cursor);
    float keepX = preferredX;

    size_t target;
    if (direction < 0)
    {
        size_t begin = lineBegin(cursor);
        target = begin == 0 ? 0 : posAtX(lineBegin(begin - 1), keepX);
    }
    else
    {
        size_t end = lineEnd(cursor);
        target = end == text.size() ? text.size() : posAtX(end + 1, keepX);
    }

    moveTo(target, extend);
    preferredX = keepX;
}

void TextEditor::selectWordAt(size_t pos)
{
    size_t from = pos, to = pos;
    while (from > 0 && isWordChar(text[from - 1]))
        from--;
    while (to < text.size() && isWordChar(text[to]))
        to++;
    anchor = from;
    cursor = to;
    lastEdit = EditKind::None;
}

void TextEditor::setWholeText(const std::string &s)
{
    beginEdit(EditKind::Other);
    text = s;
    cursor = anchor = text.size();
    ensureCursorVisible = true;
    preferredX = -1;
}

void TextEditor::historyPrevious()
{
    if (history.empty())
        return;
    if (historyIndex == -1)
    {
        draft = text;
        historyIndex = (int)history.size() - 1;
    }
    else if (historyIndex > 0)
        historyIndex--;
    else
        return;
    setWholeText(history[historyIndex]);
}

void TextEditor::historyNext()
{
    if (historyIndex == -1)
        return;
    if (historyIndex < (int)history.size() - 1)
    {
        historyIndex++;
        setWholeText(history[historyIndex]);
    }
    else
    {
        historyIndex = -1;
        setWholeText(draft);
    }
}

void TextEditor::undo()
{
    if (undoStack.empty())
        return;
    redoStack.push_back({text, cursor, anchor});
    Snapshot s = undoStack.back();
    undoStack.pop_back();
    text = s.text;
    cursor = s.cursor;
    anchor = s.anchor;
    lastEdit = EditKind::None;
    ensureCursorVisible = true;
}

void TextEditor::redo()
{
    if (redoStack.empty())
        return;
    undoStack.push_back({text, cursor, anchor});
    Snapshot s = redoStack.back();
    redoStack.pop_back();
    text = s.text;
    cursor = s.cursor;
    anchor = s.anchor;
    lastEdit = EditKind::None;
    ensureCursorVisible = true;
}

void TextEditor::clampScroll()
{
    Rectangle area = textArea();
    float contentHeight = lineCount() * lineHeight();
    scrollY = std::clamp(scrollY, 0.0f, std::max(0.0f, contentHeight - area.height));
    scrollX = std::max(0.0f, scrollX);
}

// ---------------------------------------------------------------- public

std::string TextEditor::getRunText() const
{
    if (hasSelection())
        return text.substr(selectionStart(), selectionEnd() - selectionStart());
    return text;
}

void TextEditor::addToHistory(const std::string &entry)
{
    if (entry.empty() || (!history.empty() && history.back() == entry))
        return;
    history.push_back(entry);
    historyIndex = -1;
}

int TextEditor::getLine() const { return lineNumber(cursor) + 1; }

int TextEditor::getColumn() const
{
    // Counted in characters, not bytes, so diacritics count once
    size_t begin = lineBegin(cursor);
    int column = 1;
    for (size_t p = begin; p < cursor; p = nextChar(text, p))
        column++;
    return column;
}

bool TextEditor::update(Rectangle area, const Font &f, float size, float uiZoom, bool mouseAvailable)
{
    bounds = area;
    font = f;
    fontSize = size;
    zoom = uiZoom;

    bool ctrl  = IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL);
    bool shift = IsKeyDown(KEY_LEFT_SHIFT) || IsKeyDown(KEY_RIGHT_SHIFT);
    bool alt   = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    auto pressed = [](int key) { return IsKeyPressed(key) || IsKeyPressedRepeat(key); };
    bool run = false;

    // Mouse: click places the cursor, drag selects, double click selects a word
    Vector2 mouse = GetMousePosition();
    bool inside = CheckCollisionPointRec(mouse, bounds);
    if (mouseAvailable && inside && IsMouseButtonPressed(MOUSE_BUTTON_LEFT))
    {
        size_t pos = posFromPoint(mouse);
        double now = GetTime();
        if (now - lastClickTime < 0.35 && pos == lastClickPos)
        {
            selectWordAt(pos);
            mouseSelecting = false;
        }
        else
        {
            moveTo(pos, shift);
            mouseSelecting = true;
        }
        lastClickTime = now;
        lastClickPos = pos;
    }
    if (mouseSelecting)
    {
        if (IsMouseButtonDown(MOUSE_BUTTON_LEFT))
        {
            cursor = posFromPoint(mouse);
            ensureCursorVisible = true;
        }
        else
            mouseSelecting = false;
    }
    // Ctrl+wheel is the GUI's zoom, not a scroll
    if (mouseAvailable && inside && !ctrl)
    {
        Vector2 wheel = GetMouseWheelMoveV();
        if (shift)
            scrollX -= wheel.y * 40;
        else
            scrollY -= wheel.y * lineHeight() * 3;
        scrollX -= wheel.x * 40;
    }

    // Typed characters. With Ctrl held they are shortcuts, except AltGr (Ctrl+Alt),
    // which is how many layouts type characters like ș or @
    int ch = GetCharPressed();
    while (ch > 0)
    {
        if (!ctrl || alt)
        {
            int length = 0;
            const char *utf8 = CodepointToUTF8(ch, &length);
            replaceSelection(std::string(utf8, length), EditKind::Typing);
        }
        ch = GetCharPressed();
    }

    if (ctrl && (IsKeyPressed(KEY_ENTER) || IsKeyPressed(KEY_KP_ENTER)))
        run = true;
    else if (pressed(KEY_ENTER) || pressed(KEY_KP_ENTER))
    {
        // Keep the indentation of the current line
        size_t begin = lineBegin(cursor);
        size_t indentEnd = begin;
        while (indentEnd < cursor && text[indentEnd] == ' ')
            indentEnd++;
        replaceSelection("\n" + std::string(indentEnd - begin, ' '), EditKind::Other);
    }

    if (pressed(KEY_TAB))
        replaceSelection("    ", EditKind::Typing);

    if (pressed(KEY_BACKSPACE))
    {
        if (hasSelection())
            replaceSelection("", EditKind::Deleting);
        else
            eraseRange(ctrl ? wordLeft(text, cursor) : prevChar(text, cursor), cursor, EditKind::Deleting);
    }
    if (pressed(KEY_DELETE))
    {
        if (hasSelection())
            replaceSelection("", EditKind::Deleting);
        else
            eraseRange(cursor, ctrl ? wordRight(text, cursor) : nextChar(text, cursor), EditKind::Deleting);
    }

    if (pressed(KEY_LEFT))
    {
        if (hasSelection() && !shift)
            moveTo(selectionStart(), false);
        else
            moveTo(ctrl ? wordLeft(text, cursor) : prevChar(text, cursor), shift);
    }
    if (pressed(KEY_RIGHT))
    {
        if (hasSelection() && !shift)
            moveTo(selectionEnd(), false);
        else
            moveTo(ctrl ? wordRight(text, cursor) : nextChar(text, cursor), shift);
    }
    if (pressed(KEY_HOME))
        moveTo(ctrl ? 0 : lineBegin(cursor), shift);
    if (pressed(KEY_END))
        moveTo(ctrl ? text.size() : lineEnd(cursor), shift);
    if (pressed(KEY_UP))
    {
        if (ctrl)
            historyPrevious();
        else
            moveVertical(-1, shift);
    }
    if (pressed(KEY_DOWN))
    {
        if (ctrl)
            historyNext();
        else
            moveVertical(1, shift);
    }

    if (ctrl && !alt)
    {
        if (IsKeyPressed(KEY_A))
        {
            anchor = 0;
            cursor = text.size();
            lastEdit = EditKind::None;
        }
        if (IsKeyPressed(KEY_C) && hasSelection())
            SetClipboardText(getRunText().c_str());
        if (IsKeyPressed(KEY_X) && hasSelection())
        {
            SetClipboardText(getRunText().c_str());
            replaceSelection("", EditKind::Other);
        }
        if (IsKeyPressed(KEY_V))
        {
            const char *clip = GetClipboardText();
            if (clip != nullptr)
            {
                std::string pasted;
                for (const char *c = clip; *c; c++)
                {
                    if (*c == '\r') continue;
                    if (*c == '\t') pasted += "    ";
                    else pasted += *c;
                }
                replaceSelection(pasted, EditKind::Other);
            }
        }
        if (pressed(KEY_Z))
        {
            if (shift) redo();
            else undo();
        }
        if (pressed(KEY_Y))
            redo();
    }

    if (ensureCursorVisible)
    {
        Rectangle area = textArea();
        float lh = lineHeight();
        float cy = lineNumber(cursor) * lh;
        if (cy < scrollY)
            scrollY = cy;
        if (cy + lh > scrollY + area.height)
            scrollY = cy + lh - area.height;
        float cx = xOf(cursor);
        if (cx < scrollX)
            scrollX = cx;
        if (cx > scrollX + area.width - 12)
            scrollX = cx - area.width + 12;
        ensureCursorVisible = false;
    }
    clampScroll();
    return run;
}

void TextEditor::draw(const Theme &theme) const
{
    DrawRectangleRec(bounds, theme.editorBg);
    if (bounds.width <= 0 || bounds.height <= 0)
        return;

    Rectangle area = textArea();
    float lh = lineHeight();
    float gutter = gutterWidth();
    int total = lineCount();
    int first = std::max(0, (int)(scrollY / lh));
    int last = std::min(total - 1, (int)((scrollY + area.height) / lh) + 1);
    int cursorLine = lineNumber(cursor);
    float textOffset = (lh - fontSize) / 2;

    // Pass 1: current-line highlight and line numbers (clipped to the whole editor)
    beginScissor(bounds, zoom);
    for (int i = first; i <= last; i++)
    {
        float y = area.y + i * lh - scrollY;
        if (i == cursorLine && !hasSelection())
            DrawRectangleRec(Rectangle{bounds.x, y, bounds.width, lh}, theme.lineHighlight);
        std::string number = std::to_string(i + 1);
        float w = measure(number);
        DrawTextEx(font, number.c_str(), Vector2{snapToPixel(bounds.x + gutter - 14 - w, zoom), snapToPixel(y + textOffset, zoom)},
                   fontSize, SPACING, i == cursorLine ? theme.text : theme.textDim);
    }
    EndScissorMode();

    // Pass 2: selection, highlighted text and cursor (clipped to the text area)
    beginScissor(Rectangle{area.x, bounds.y, bounds.x + bounds.width - area.x, bounds.height}, zoom);

    // Find the string state at the first visible line, since a string can span lines
    size_t begin = lineStartByIndex(first);
    bool inString = false;
    for (size_t p = 0; p < begin; p++)
        if (text[p] == '\'')
            inString = !inString;

    size_t selFrom = selectionStart(), selTo = selectionEnd();
    for (int i = first; i <= last; i++)
    {
        size_t end = lineEnd(begin);
        float y = area.y + i * lh - scrollY;
        float baseX = area.x - scrollX;

        if (hasSelection() && selFrom <= end && selTo >= begin)
        {
            float x0 = selFrom > begin ? xOf(selFrom) : 0;
            float x1 = selTo < end ? xOf(selTo) : xOf(end) + (selTo > end ? fontSize * 0.4f : 0);
            DrawRectangleRec(Rectangle{baseX + x0, y, x1 - x0, lh}, theme.selection);
        }

        // Draw the line token by token so keywords, strings and numbers get their own color
        float x = baseX;
        size_t pos = begin;
        while (pos < end)
        {
            size_t tokenEnd = pos;
            Color color = theme.text;
            unsigned char c = text[pos];
            if (inString || c == '\'')
            {
                tokenEnd = inString ? pos : pos + 1;
                while (tokenEnd < end && text[tokenEnd] != '\'')
                    tokenEnd++;
                if (tokenEnd < end)
                {
                    tokenEnd++;          // include the closing quote
                    inString = false;
                }
                else
                    inString = true;     // the string continues on the next line
                color = theme.string;
            }
            else if (std::isdigit(c) || ((c == '-' || c == '+') && pos + 1 < end && std::isdigit((unsigned char)text[pos + 1])
                                          && (pos == begin || !isWordChar(text[pos - 1]))))
            {
                tokenEnd = pos + 1;
                while (tokenEnd < end && (std::isdigit((unsigned char)text[tokenEnd]) || text[tokenEnd] == '.'))
                    tokenEnd++;
                color = theme.number;
            }
            else if (isWordChar(c))
            {
                while (tokenEnd < end && isWordChar(text[tokenEnd]))
                    tokenEnd = nextChar(text, tokenEnd);
                std::string upper = text.substr(pos, tokenEnd - pos);
                for (char &u : upper)
                    u = (char)std::toupper((unsigned char)u);
                if (isKeyword(upper))
                    color = theme.keyword;
            }
            else
                tokenEnd = nextChar(text, pos);

            std::string token = text.substr(pos, tokenEnd - pos);
            DrawTextEx(font, token.c_str(), Vector2{snapToPixel(x, zoom), snapToPixel(y + textOffset, zoom)}, fontSize, SPACING, color);
            x += measure(token) + SPACING;
            pos = tokenEnd;
        }
        // A quote toggles string state even when the line is not drawn past it
        begin = end + 1;
    }

    if (text.empty())
        DrawTextEx(font, "Write a query. Ctrl+Enter runs it (or the selection), Ctrl+Up/Down browses history.",
                   Vector2{snapToPixel(area.x, zoom), snapToPixel(area.y + textOffset, zoom)}, fontSize, SPACING, theme.textDim);

    bool blinkOn = std::fmod(GetTime() - blinkStart, 1.0) < 0.55;
    if (blinkOn)
    {
        float cx = area.x - scrollX + xOf(cursor);
        float cy = area.y + cursorLine * lh - scrollY;
        DrawRectangleRec(Rectangle{cx, cy + 3, 2, lh - 6}, theme.accent);
    }
    EndScissorMode();
}
