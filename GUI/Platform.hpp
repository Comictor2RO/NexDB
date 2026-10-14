#ifndef PLATFORM_HPP
#define PLATFORM_HPP

// OS helpers the custom (undecorated) title bar needs and raylib does not expose.
// They live in their own translation unit because windows.h and raylib.h cannot be
// included together (Rectangle, CloseWindow, DrawText... clash).
namespace Platform {
    // Cursor position in screen coordinates. Window dragging needs this: the window-relative
    // mouse position goes stale the moment the window itself moves.
    bool getCursorScreenPos(int &x, int &y);

    // Work area (monitor minus taskbar) of the monitor the window is on, used to maximize
    // without covering the taskbar. windowHandle is raylib's GetWindowHandle().
    bool getWorkArea(void *windowHandle, int &x, int &y, int &width, int &height);
}

#endif
