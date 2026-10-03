#pragma once
#include "app.hpp"
#include <algorithm>

namespace input_overlay {
inline SIZE OverlaySize(int scale) {
    scale = std::clamp(scale, 10, 200);
    return {MulDiv(OverlayBaseWidth, scale, 100), MulDiv(OverlayBaseHeight, scale, 100)};
}
inline POINT AnchoredPosition(const Settings& s, const RECT& screen) {
    const SIZE size = OverlaySize(s.scale);
    const LONG x = s.anchorRight ? screen.right - size.cx - s.x : screen.left + s.x;
    const LONG y = s.anchorBottom ? screen.bottom - size.cy - s.y : screen.top + s.y;
    return {std::clamp(x, screen.left, std::max(screen.left, screen.right - size.cx)),
            std::clamp(y, screen.top, std::max(screen.top, screen.bottom - size.cy))};
}
inline void AnchorPosition(Settings& s, const RECT& screen, POINT position) {
    const SIZE size = OverlaySize(s.scale);
    const int left = std::max(0L, position.x - screen.left);
    const int right = std::max(0L, screen.right - position.x - size.cx);
    const int top = std::max(0L, position.y - screen.top);
    const int bottom = std::max(0L, screen.bottom - position.y - size.cy);
    s.anchorRight = right < left;
    s.anchorBottom = bottom <= top;
    s.x = s.anchorRight ? right : left;
    s.y = s.anchorBottom ? bottom : top;
}
}
