#ifndef ZOOM_HPP
#define ZOOM_HPP

#include <algorithm>
#include <cmath>
#include "raylib.h"

// The UI is laid out in logical pixels and drawn through a Camera2D zoom (GUI::run).
// These helpers cover what the camera does not transform.

// Scissor rects skip the camera, so they are scaled here. Each edge is rounded on its own,
// so the clip covers the same screen pixels that filling r would
inline void beginScissor(Rectangle r, float zoom)
{
    int x0 = (int)std::round(r.x * zoom), y0 = (int)std::round(r.y * zoom);
    int x1 = (int)std::round((r.x + r.width) * zoom), y1 = (int)std::round((r.y + r.height) * zoom);
    BeginScissorMode(x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0));
}

// A logical coordinate that lands on a whole screen pixel, so text stays sharp at 110%, 125%...
inline float snapToPixel(float v, float zoom)
{
    return std::round(v * zoom) / zoom;
}

#endif
