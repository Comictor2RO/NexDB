#include "Platform.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

bool Platform::getCursorScreenPos(int &x, int &y)
{
    POINT p;
    if (!GetCursorPos(&p))
        return false;
    x = p.x;
    y = p.y;
    return true;
}

bool Platform::getWorkArea(void *windowHandle, int &x, int &y, int &width, int &height)
{
    HMONITOR monitor = MonitorFromWindow(static_cast<HWND>(windowHandle), MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{};
    info.cbSize = sizeof(info);
    if (!GetMonitorInfoA(monitor, &info))
        return false;
    x = info.rcWork.left;
    y = info.rcWork.top;
    width = info.rcWork.right - info.rcWork.left;
    height = info.rcWork.bottom - info.rcWork.top;
    return true;
}

#else

// Elsewhere the GUI falls back to raylib (window position + mouse, MaximizeWindow)
bool Platform::getCursorScreenPos(int &, int &) { return false; }
bool Platform::getWorkArea(void *, int &, int &, int &, int &) { return false; }

#endif
