#ifndef TEXT_EDITOR_HPP
#define TEXT_EDITOR_HPP

#include <string>
#include <vector>
#include "raylib.h"
#include "Colors.hpp"

// Multi-line query editor: cursor, mouse/shift selection, clipboard, undo/redo, query
// history, line numbers and SQL highlighting. Text is UTF-8; cursor and anchor are byte
// offsets that always sit on a character boundary.
//
// It is the only text input in the GUI, so it always owns the keyboard.
class TextEditor {
    public:
        // Processes this frame's keyboard and mouse input. mouseAvailable is false while
        // another widget owns the mouse (a splitter or window drag).
        // Returns true when the user asked to run (Ctrl+Enter).
        bool update(Rectangle bounds, const Font &font, float fontSize, bool mouseAvailable);
        void draw(const Theme &theme) const;

        // What Ctrl+Enter / Run executes: the selection if there is one, otherwise everything
        std::string getRunText() const;
        void addToHistory(const std::string &entry);

        // 1-based, for the status bar
        int getLine() const;
        int getColumn() const;

    private:
        struct Snapshot {
            std::string text;
            size_t cursor;
            size_t anchor;
        };
        // Consecutive edits of the same kind are undone together, like in VS Code
        enum class EditKind { None, Typing, Deleting, Other };

        std::string text;
        size_t cursor = 0;
        size_t anchor = 0;            // the selection is between anchor and cursor
        bool mouseSelecting = false;
        float scrollX = 0;
        float scrollY = 0;
        float preferredX = -1;        // keeps the column while moving up/down
        bool ensureCursorVisible = false;
        double blinkStart = 0;
        double lastClickTime = 0;
        size_t lastClickPos = std::string::npos;

        std::vector<Snapshot> undoStack;
        std::vector<Snapshot> redoStack;
        EditKind lastEdit = EditKind::None;

        std::vector<std::string> history;
        int historyIndex = -1;        // -1 = editing the draft, not browsing history
        std::string draft;

        // Captured by update() and reused by draw() and the helpers below
        Font font{};
        float fontSize = 18;
        Rectangle bounds{};

        bool hasSelection() const { return anchor != cursor; }
        size_t selectionStart() const { return anchor < cursor ? anchor : cursor; }
        size_t selectionEnd() const { return anchor < cursor ? cursor : anchor; }

        float lineHeight() const;
        float gutterWidth() const;
        Rectangle textArea() const;
        int lineCount() const;
        int lineNumber(size_t pos) const;     // 0-based
        size_t lineBegin(size_t pos) const;
        size_t lineEnd(size_t pos) const;
        size_t lineStartByIndex(int line) const;

        float measure(const std::string &s) const;
        float xOf(size_t pos) const;          // x of pos inside its line, unscrolled
        size_t posAtX(size_t lineStartPos, float x) const;
        size_t posFromPoint(Vector2 point) const;

        void beginEdit(EditKind kind);
        void replaceSelection(const std::string &s, EditKind kind);
        void eraseRange(size_t from, size_t to, EditKind kind);
        void afterEdit();
        void moveTo(size_t pos, bool extend);
        void moveVertical(int direction, bool extend);
        void selectWordAt(size_t pos);
        void setWholeText(const std::string &s);
        void historyPrevious();
        void historyNext();
        void undo();
        void redo();
        void clampScroll();
};

#endif
