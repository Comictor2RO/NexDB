#ifndef COLORS_HPP
#define COLORS_HPP

#include "raylib.h"

// Every color the GUI draws with, for one theme. The values reuse the original NexDB
// palette: the dark theme is the old dark one, the light theme keeps the warm beige.
struct Theme {
    Color windowBg;       // behind the panels
    Color titleBar;
    Color statusBar;
    Color statusText;
    Color panel;          // panel body
    Color panelHeader;    // section header strips inside panels
    Color border;
    Color text;
    Color textDim;
    Color accent;
    Color accentText;     // text drawn on top of accent
    Color hover;          // hovered rows and buttons
    Color selection;      // editor selection
    Color lineHighlight;  // editor line with the cursor
    Color editorBg;
    Color rowEven;
    Color rowOdd;
    Color gridLine;
    Color scrollbar;
    Color error;
    Color success;
    Color info;
    Color keyword;        // SQL syntax highlighting
    Color string;
    Color number;
    Color closeHover;     // the window close button
};

inline Theme darkTheme()
{
    Theme t;
    t.windowBg      = Color{13, 15, 18, 255};
    t.titleBar      = Color{18, 20, 24, 255};
    t.statusBar     = Color{24, 58, 96, 255};
    t.statusText    = Color{230, 232, 235, 255};
    t.panel         = Color{22, 24, 28, 255};
    t.panelHeader   = Color{32, 36, 42, 255};
    t.border        = Color{45, 50, 58, 255};
    t.text          = Color{220, 222, 225, 255};
    t.textDim       = Color{130, 136, 145, 255};
    t.accent        = Color{77, 163, 255, 255};
    t.accentText    = Color{13, 15, 18, 255};
    t.hover         = Color{36, 40, 46, 255};
    t.selection     = Color{38, 79, 120, 255};
    t.lineHighlight = Color{31, 34, 40, 255};
    t.editorBg      = Color{26, 29, 34, 255};
    t.rowEven       = Color{28, 30, 35, 255};
    t.rowOdd        = Color{22, 24, 28, 255};
    t.gridLine      = Color{45, 50, 58, 255};
    t.scrollbar     = Color{85, 92, 105, 160};
    t.error         = Color{241, 90, 90, 255};
    t.success       = Color{90, 200, 120, 255};
    t.info          = Color{77, 163, 255, 255};
    t.keyword       = Color{86, 156, 214, 255};
    t.string        = Color{206, 145, 120, 255};
    t.number        = Color{181, 206, 168, 255};
    t.closeHover    = Color{232, 17, 35, 255};
    return t;
}

inline Theme lightTheme()
{
    Theme t;
    t.windowBg      = Color{232, 224, 212, 255};
    t.titleBar      = Color{224, 214, 200, 255};
    t.statusBar     = Color{139, 69, 19, 255};
    t.statusText    = Color{255, 252, 247, 255};
    t.panel         = Color{250, 246, 238, 255};
    t.panelHeader   = Color{218, 205, 188, 255};
    t.border        = Color{198, 185, 170, 255};
    t.text          = Color{40, 38, 35, 255};
    t.textDim       = Color{130, 115, 98, 255};
    t.accent        = Color{139, 69, 19, 255};
    t.accentText    = Color{255, 252, 247, 255};
    t.hover         = Color{236, 227, 214, 255};
    t.selection     = Color{222, 196, 164, 255};
    t.lineHighlight = Color{246, 240, 230, 255};
    t.editorBg      = Color{255, 252, 247, 255};
    t.rowEven       = Color{238, 230, 218, 255};
    t.rowOdd        = Color{250, 246, 238, 255};
    t.gridLine      = Color{218, 205, 188, 255};
    t.scrollbar     = Color{175, 160, 140, 180};
    t.error         = Color{175, 10, 10, 255};
    t.success       = Color{20, 130, 40, 255};
    t.info          = Color{139, 69, 19, 255};
    t.keyword       = Color{0, 90, 180, 255};
    t.string        = Color{163, 21, 21, 255};
    t.number        = Color{9, 134, 88, 255};
    t.closeHover    = Color{232, 17, 35, 255};
    return t;
}

#endif
