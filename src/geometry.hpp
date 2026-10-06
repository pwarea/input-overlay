#pragma once
#include "app.hpp"
#include <algorithm>
#include <cmath>

namespace input_overlay {
inline SIZE OverlaySize(int scale) {
    scale = std::clamp(scale, 10, 200);
    return {MulDiv(OverlayBaseWidth, scale, 100), MulDiv(OverlayBaseHeight, scale, 100)};
}
inline RECT OverlayViewport(const Settings& s) {
    const int percent = std::clamp(EffectiveOverlayScale(s), 10, 200);
    const SIZE canvas = OverlaySize(percent);
    if (s.device != OverlayDevice::Controller) return {0, 0, canvas.cx, canvas.cy};
    const float scale = static_cast<float>(percent) / 100.0f *
        static_cast<float>(OverlayBaseWidth) / static_cast<float>(OverlayDesignWidth);
    const bool dualShock4 = s.controllerLayout == ControllerLayout::DualShock4;
    const float left = 105.0f + 0.60f * (dualShock4 ? 3.0f : 0.0f);
    const float right = 105.0f + 0.60f * (dualShock4 ? 497.0f : 500.0f);
    const float top = 2.0f + 0.60f * 4.0f;
    const float bottom = 2.0f + 0.60f * (dualShock4 ? 313.0f : 348.0f);
    return {std::max(0L, static_cast<LONG>(std::floor(left * scale)) - 1),
            std::max(0L, static_cast<LONG>(std::floor(top * scale)) - 1),
            std::min(canvas.cx, static_cast<LONG>(std::ceil(right * scale)) + 1),
            std::min(canvas.cy, static_cast<LONG>(std::ceil(bottom * scale)) + 1)};
}
inline SIZE OverlaySize(const Settings& s) {
    const RECT viewport = OverlayViewport(s);
    return {viewport.right - viewport.left, viewport.bottom - viewport.top};
}
inline POINT AnchoredPosition(const Settings& s, const RECT& screen) {
    const SIZE size = OverlaySize(s);
    const LONG x = s.anchorRight ? screen.right - size.cx - s.x : screen.left + s.x;
    const LONG y = s.anchorBottom ? screen.bottom - size.cy - s.y : screen.top + s.y;
    return {std::clamp(x, screen.left, std::max(screen.left, screen.right - size.cx)),
            std::clamp(y, screen.top, std::max(screen.top, screen.bottom - size.cy))};
}
inline void AnchorPosition(Settings& s, const RECT& screen, POINT position) {
    const SIZE size = OverlaySize(s);
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
