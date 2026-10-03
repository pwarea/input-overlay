#pragma once
#include "app.hpp"
#include <algorithm>
#include <cwchar>

namespace input_overlay {
struct ThemeColors { COLORREF start, middle, end; };
inline COLORREF MixColor(COLORREF a, COLORREF b, float amount) {
    const float t = std::clamp(amount, 0.0f, 1.0f);
    const auto mix = [t](BYTE first, BYTE second) {
        return static_cast<BYTE>(first + (second - first) * t + 0.5f);
    };
    return RGB(mix(GetRValue(a), GetRValue(b)), mix(GetGValue(a), GetGValue(b)), mix(GetBValue(a), GetBValue(b)));
}
inline ThemeColors ThemePalette(const Settings& settings) {
    COLORREF start = RGB(255, 178, 91), end = RGB(192, 121, 242);
    switch (settings.colorTheme) {
    case ColorTheme::Aurora: start = RGB(97, 230, 190); end = RGB(154, 140, 250); break;
    case ColorTheme::Ocean: start = RGB(88, 223, 237); end = RGB(98, 137, 242); break;
    case ColorTheme::Rose: start = RGB(255, 196, 160); end = RGB(234, 129, 184); break;
    case ColorTheme::Custom: start = settings.backgroundStart; end = settings.backgroundEnd; break;
    default: break;
    }
    return {start, MixColor(start, end, 0.5f), end};
}
inline COLORREF ThemeColorAt(const Settings& settings, float position) {
    const auto palette = ThemePalette(settings);
    return MixColor(palette.start, palette.end, position);
}
inline std::wstring ColorHex(COLORREF color) {
    wchar_t text[8]{};
    swprintf_s(text, L"#%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
    return text;
}
inline bool ParseColorHex(const std::wstring& text, COLORREF& color) {
    const size_t offset = !text.empty() && text[0] == L'#' ? 1 : 0;
    if (text.size() - offset != 6) return false;
    unsigned value = 0;
    for (size_t i = offset; i < text.size(); ++i) {
        const wchar_t c = text[i];
        const int digit = c >= L'0' && c <= L'9' ? c - L'0' :
            c >= L'a' && c <= L'f' ? c - L'a' + 10 : c >= L'A' && c <= L'F' ? c - L'A' + 10 : -1;
        if (digit < 0) return false;
        value = value * 16 + static_cast<unsigned>(digit);
    }
    color = RGB((value >> 16) & 255, (value >> 8) & 255, value & 255);
    return true;
}
}
