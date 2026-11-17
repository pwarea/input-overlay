#pragma once
#include "app.hpp"
#include <algorithm>

namespace input_overlay {
namespace keyboard {
enum KeySlot : size_t {
    Escape = 0,
    One = 1,
    Two = 2,
    Three = 3,
    Four = 4,
    Five = 5,
    Tab = 6,
    Q = 7,
    W = 8,
    E = 9,
    R = 10,
    T = 11,
    Caps = 12,
    A = 13,
    S = 14,
    D = 15,
    F = 16,
    G = 17,
    LeftShift = 18,
    ISOBackslash = 19,
    Z = 20,
    X = 21,
    C = 22,
    V = 23,
    LeftCtrl = 24,
    LeftWin = 25,
    LeftAlt = 26,
    Space = 27,
    LeftMouse = 28,
    RightMouse = 29,
    MiddleMouse = 30,
    Side1 = 31,
    Side2 = 32,
    ScrollUp = 33,
    ScrollDown = 34,
    Six = 35,
    Seven = 36,
    Eight = 37,
    Nine = 38,
    Zero = 39,
    Grave = 40,
    Minus = 41,
    Equals = 42,
    Backspace = 43,
    Y = 44,
    U = 45,
    I = 46,
    O = 47,
    P = 48,
    LeftBracket = 49,
    RightBracket = 50,
    Backslash = 51,
    H = 52,
    J = 53,
    K = 54,
    L = 55,
    Semicolon = 56,
    Quote = 57,
    Enter = 58,
    B = 59,
    N = 60,
    M = 61,
    Comma = 62,
    Period = 63,
    Slash = 64,
    RightShift = 65,
    RightAlt = 66,
    RightWin = 67,
    Menu = 68,
    RightCtrl = 69,
    F1 = 70,
    F2 = 71,
    F3 = 72,
    F4 = 73,
    F5 = 74,
    F6 = 75,
    F7 = 76,
    F8 = 77,
    F9 = 78,
    F10 = 79,
    F11 = 80,
    F12 = 81,
    PrintScreen = 82,
    ScrollLock = 83,
    Pause = 84,
    Insert = 85,
    Home = 86,
    PageUp = 87,
    Delete = 88,
    End = 89,
    PageDown = 90,
    ArrowUp = 91,
    ArrowLeft = 92,
    ArrowDown = 93,
    ArrowRight = 94,
    NumLock = 95,
    NumDivide = 96,
    NumMultiply = 97,
    NumSubtract = 98,
    NumAdd = 99,
    NumEnter = 100,
    NumDecimal = 101,
    Num0 = 102,
    Num1 = 103,
    Num2 = 104,
    Num3 = 105,
    Num4 = 106,
    Num5 = 107,
    Num6 = 108,
    Num7 = 109,
    Num8 = 110,
    Num9 = 111
};
}
struct KeyboardSlotDefinition {
    const wchar_t* label;
    int input;
    const wchar_t* name;
};
inline const std::array<KeyboardSlotDefinition, SlotCount>& KeyboardSlotDefinitions() {
    static const std::array<KeyboardSlotDefinition, SlotCount> definitions{{
        {L"ESC", VK_ESCAPE, L"Escape"},
        {L"1", '1', L"1"},
        {L"2", '2', L"2"},
        {L"3", '3', L"3"},
        {L"4", '4', L"4"},
        {L"5", '5', L"5"},
        {L"TAB", VK_TAB, L"Tab"},
        {L"Q", 'Q', L"Q"},
        {L"W", 'W', L"W"},
        {L"E", 'E', L"E"},
        {L"R", 'R', L"R"},
        {L"T", 'T', L"T"},
        {L"CAPS", VK_CAPITAL, L"Caps Lock"},
        {L"A", 'A', L"A"},
        {L"S", 'S', L"S"},
        {L"D", 'D', L"D"},
        {L"F", 'F', L"F"},
        {L"G", 'G', L"G"},
        {L"SHIFT", VK_LSHIFT, L"Left Shift"},
        {L"\\", VK_OEM_102, L"ISO backslash"},
        {L"Z", 'Z', L"Z"},
        {L"X", 'X', L"X"},
        {L"C", 'C', L"C"},
        {L"V", 'V', L"V"},
        {L"CTRL", VK_LCONTROL, L"Left Ctrl"},
        {L"WIN", VK_LWIN, L"Left Windows"},
        {L"ALT", VK_LMENU, L"Left Alt"},
        {L"SPACE", VK_SPACE, L"Space"},
        {L"Left", MouseLeft, L"Mouse left"},
        {L"Right", MouseRight, L"Mouse right"},
        {L"Middle", MouseMiddle, L"Mouse middle"},
        {L"Side 1", MouseSide1, L"Mouse side 1"},
        {L"Side 2", MouseSide2, L"Mouse side 2"},
        {L"Wheel up", WheelUp, L"Wheel up"},
        {L"Wheel down", WheelDown, L"Wheel down"},
        {L"6", '6', L"6"},
        {L"7", '7', L"7"},
        {L"8", '8', L"8"},
        {L"9", '9', L"9"},
        {L"0", '0', L"0"},
        {L"`", VK_OEM_3, L"Grave"},
        {L"-", VK_OEM_MINUS, L"Minus"},
        {L"=", VK_OEM_PLUS, L"Equals"},
        {L"BACK", VK_BACK, L"Backspace"},
        {L"Y", 'Y', L"Y"},
        {L"U", 'U', L"U"},
        {L"I", 'I', L"I"},
        {L"O", 'O', L"O"},
        {L"P", 'P', L"P"},
        {L"[", VK_OEM_4, L"Left bracket"},
        {L"]", VK_OEM_6, L"Right bracket"},
        {L"\\", VK_OEM_5, L"Backslash"},
        {L"H", 'H', L"H"},
        {L"J", 'J', L"J"},
        {L"K", 'K', L"K"},
        {L"L", 'L', L"L"},
        {L";", VK_OEM_1, L"Semicolon"},
        {L"'", VK_OEM_7, L"Quote"},
        {L"ENTER", VK_RETURN, L"Enter"},
        {L"B", 'B', L"B"},
        {L"N", 'N', L"N"},
        {L"M", 'M', L"M"},
        {L",", VK_OEM_COMMA, L"Comma"},
        {L".", VK_OEM_PERIOD, L"Period"},
        {L"/", VK_OEM_2, L"Slash"},
        {L"SHIFT", VK_RSHIFT, L"Right Shift"},
        {L"ALT", VK_RMENU, L"Right Alt"},
        {L"WIN", VK_RWIN, L"Right Windows"},
        {L"MENU", VK_APPS, L"Menu"},
        {L"CTRL", VK_RCONTROL, L"Right Ctrl"},
        {L"F1", VK_F1, L"F1"},
        {L"F2", VK_F2, L"F2"},
        {L"F3", VK_F3, L"F3"},
        {L"F4", VK_F4, L"F4"},
        {L"F5", VK_F5, L"F5"},
        {L"F6", VK_F6, L"F6"},
        {L"F7", VK_F7, L"F7"},
        {L"F8", VK_F8, L"F8"},
        {L"F9", VK_F9, L"F9"},
        {L"F10", VK_F10, L"F10"},
        {L"F11", VK_F11, L"F11"},
        {L"F12", VK_F12, L"F12"},
        {L"PRT", VK_SNAPSHOT, L"Print Screen"},
        {L"SCR", VK_SCROLL, L"Scroll Lock"},
        {L"PAUSE", VK_PAUSE, L"Pause"},
        {L"INS", VK_INSERT, L"Insert"},
        {L"HOME", VK_HOME, L"Home"},
        {L"PGUP", VK_PRIOR, L"Page Up"},
        {L"DEL", VK_DELETE, L"Delete"},
        {L"END", VK_END, L"End"},
        {L"PGDN", VK_NEXT, L"Page Down"},
        {L"\u2191", VK_UP, L"Up arrow"},
        {L"\u2190", VK_LEFT, L"Left arrow"},
        {L"\u2193", VK_DOWN, L"Down arrow"},
        {L"\u2192", VK_RIGHT, L"Right arrow"},
        {L"NUM", VK_NUMLOCK, L"Num Lock"},
        {L"/", VK_DIVIDE, L"Numpad divide"},
        {L"*", VK_MULTIPLY, L"Numpad multiply"},
        {L"-", VK_SUBTRACT, L"Numpad subtract"},
        {L"+", VK_ADD, L"Numpad add"},
        {L"ENTER", NumpadEnter, L"Numpad Enter"},
        {L".", VK_DECIMAL, L"Numpad decimal"},
        {L"0", VK_NUMPAD0, L"Numpad 0"},
        {L"1", VK_NUMPAD1, L"Numpad 1"},
        {L"2", VK_NUMPAD2, L"Numpad 2"},
        {L"3", VK_NUMPAD3, L"Numpad 3"},
        {L"4", VK_NUMPAD4, L"Numpad 4"},
        {L"5", VK_NUMPAD5, L"Numpad 5"},
        {L"6", VK_NUMPAD6, L"Numpad 6"},
        {L"7", VK_NUMPAD7, L"Numpad 7"},
        {L"8", VK_NUMPAD8, L"Numpad 8"},
        {L"9", VK_NUMPAD9, L"Numpad 9"}
    }};
    return definitions;
}
struct KeyboardKey {
    size_t slot;
    float x, y, w, h;
    bool modifier = false;
    bool isoEnter = false;
};

inline std::vector<KeyboardKey> BuildKeyboardKeys(KeyboardForm form, bool iso) {
    using namespace keyboard;
    std::vector<KeyboardKey> keys;
    keys.reserve(105);
    const auto add = [&](size_t slot, float x, float y, float w, float h = 34.f, bool modifier = false, bool isoEnter = false) {
        keys.push_back({slot, x, y, w, h, modifier, isoEnter});
    };
    if (form == KeyboardForm::Gaming) {
        for (size_t row = 0; row < 3; ++row) {
            const float y = 8.f + static_cast<float>(row) * 41.f;
            const float firstWidth = 55.f + static_cast<float>(row) * 10.f;
            const float width = 60.f - static_cast<float>(row) * 2.f;
            add(row * 6, 12.f, y, firstWidth, 34.f, true);
            for (size_t col = 1; col < 6; ++col)
                add(row * 6 + col, 18.f + firstWidth + static_cast<float>(col - 1) * (width + 6.f), y, width);
        }
        add(LeftShift, 12, 131, iso ? 58.f : 104.f, 34, true);
        if (iso) add(ISOBackslash, 76, 131, 40);
        for (size_t col = 0; col < 4; ++col) add(Z + col, 122.f + static_cast<float>(col) * 70.f, 131, 64);
        add(LeftCtrl, 12, 172, 60, 34, true);
        add(LeftWin, 78, 172, 48, 34, true);
        add(LeftAlt, 132, 172, 52, 34, true);
        add(Space, 190, 172, 207, 34, true);
        return keys;
    }
    const bool extended = form == KeyboardForm::Tkl || form == KeyboardForm::FullSize;
    const float top = extended ? 64.f : 8.f;
    const auto unit = [&](size_t slot, float x, float row, float span = 1.f, float tall = 1.f, bool modifier = false) {
        add(slot, 12.f + x * 40.f, top + row * 40.f, span * 40.f - 6.f, tall * 40.f - 6.f, modifier);
    };
    if (extended) {
        add(Escape, 12, 8, 34, 34, true);
        for (size_t i = 0; i < 12; ++i) {
            const float x = 12.f + 80.f + static_cast<float>(i) * 40.f + static_cast<float>(i / 4) * 20.f;
            add(F1 + i, x, 8, 34, 34, true);
        }
        add(PrintScreen, 628, 8, 34, 34, true);
        add(ScrollLock, 668, 8, 34, 34, true);
        add(Pause, 708, 8, 34, 34, true);
    }
    const size_t numberRow[] = {extended ? Grave : Escape, One, Two, Three, Four, Five, Six, Seven, Eight, Nine, Zero, Minus, Equals};
    for (size_t i = 0; i < 13; ++i) unit(numberRow[i], static_cast<float>(i), 0, 1, 1, i == 0 && !extended);
    unit(Backspace, 13, 0, 2, 1, true);
    unit(Tab, 0, 1, 1.5f, 1, true);
    const size_t upper[] = {Q, W, E, R, T, Y, U, I, O, P, LeftBracket, RightBracket};
    for (size_t i = 0; i < 12; ++i) unit(upper[i], 1.5f + static_cast<float>(i), 1);
    if (!iso) unit(Backslash, 13.5f, 1, 1.5f);
    unit(Caps, 0, 2, 1.75f, 1, true);
    const size_t home[] = {A, S, D, F, G, H, J, K, L, Semicolon, Quote};
    for (size_t i = 0; i < 11; ++i) unit(home[i], 1.75f + static_cast<float>(i), 2);
    if (iso) {
        unit(Backslash, 12.75f, 2);
        add(Enter, 552, top + 40, 54, 74, true, true);
    } else unit(Enter, 12.75f, 2, 2.25f, 1, true);
    unit(LeftShift, 0, 3, iso ? 1.25f : 2.25f, 1, true);
    if (iso) unit(ISOBackslash, 1.25f, 3);
    const size_t lower[] = {Z, X, C, V, B, N, M, Comma, Period, Slash};
    for (size_t i = 0; i < 10; ++i) unit(lower[i], 2.25f + static_cast<float>(i), 3);
    unit(RightShift, 12.25f, 3, 2.75f, 1, true);
    unit(LeftCtrl, 0, 4, 1.25f, 1, true);
    unit(LeftWin, 1.25f, 4, 1.25f, 1, true);
    unit(LeftAlt, 2.5f, 4, 1.25f, 1, true);
    unit(Space, 3.75f, 4, 6.25f, 1, true);
    unit(RightAlt, 10, 4, 1.25f, 1, true);
    unit(RightWin, 11.25f, 4, 1.25f, 1, true);
    unit(Menu, 12.5f, 4, 1.25f, 1, true);
    unit(RightCtrl, 13.75f, 4, 1.25f, 1, true);
    if (extended) {
        const size_t navigation[] = {Insert, Home, PageUp, Delete, End, PageDown};
        for (size_t i = 0; i < 6; ++i)
            add(navigation[i], 628.f + static_cast<float>(i % 3) * 40.f, top + static_cast<float>(i / 3) * 40.f, 34, 34, true);
        add(ArrowUp, 668, top + 120, 34, 34, true);
        add(ArrowLeft, 628, top + 160, 34, 34, true);
        add(ArrowDown, 668, top + 160, 34, 34, true);
        add(ArrowRight, 708, top + 160, 34, 34, true);
    }
    if (form == KeyboardForm::FullSize) {
        const size_t padTop[] = {NumLock, NumDivide, NumMultiply, NumSubtract};
        for (size_t i = 0; i < 4; ++i) add(padTop[i], 764.f + static_cast<float>(i) * 40.f, top, 34, 34, i == 0);
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 3; ++col)
                add(Num0 + static_cast<size_t>(7 - row * 3 + col), 764.f + col * 40.f, top + (row + 1) * 40.f, 34);
        add(NumAdd, 884, top + 40, 34, 74);
        add(NumEnter, 884, top + 120, 34, 74, true);
        add(Num0, 764, top + 160, 74);
        add(NumDecimal, 844, top + 160, 34);
    }
    return keys;
}
inline const std::vector<KeyboardKey>& KeyboardKeys(const Settings& settings) {
    static const std::array<std::vector<KeyboardKey>, KeyboardFormCount * 2> layouts = [] {
        std::array<std::vector<KeyboardKey>, KeyboardFormCount * 2> result;
        for (int form = 0; form < KeyboardFormCount; ++form)
            for (int iso = 0; iso < 2; ++iso)
                result[form * 2 + iso] = BuildKeyboardKeys(static_cast<KeyboardForm>(form), iso != 0);
        return result;
    }();
    const int form = std::clamp(static_cast<int>(settings.keyboardForm), 0, KeyboardFormCount - 1);
    return layouts[form * 2 + (settings.isoLayout ? 1 : 0)];
}
inline std::vector<size_t> KeyboardSlots(const Settings& settings) {
    std::vector<size_t> slots;
    slots.reserve(105);
    for (const auto& key : KeyboardKeys(settings)) slots.push_back(key.slot);
    return slots;
}
inline bool IsKeyboardSlotPresent(const Settings& settings, size_t slot) {
    const auto& keys = KeyboardKeys(settings);
    return std::any_of(keys.begin(), keys.end(), [slot](const KeyboardKey& key) { return key.slot == slot; });
}
inline SIZE KeyboardDesignSize(const Settings& settings) {
    if (settings.keyboardForm == KeyboardForm::Gaming) return {OverlayDesignWidth, OverlayDesignHeight};
    const LONG right = settings.keyboardForm == KeyboardForm::Compact ? 606 :
        settings.keyboardForm == KeyboardForm::Tkl ? 742 : 918;
    return {right + (settings.showMouse ? 113 : 12), settings.keyboardForm == KeyboardForm::Compact ? 220L : 272L};
}
inline POINT KeyboardMouseOffset(const Settings& settings) {
    if (settings.keyboardForm == KeyboardForm::Gaming) return {0, 0};
    const SIZE size = KeyboardDesignSize(settings);
    const LONG right = settings.keyboardForm == KeyboardForm::Compact ? 606 :
        settings.keyboardForm == KeyboardForm::Tkl ? 742 : 918;
    return {right - 397, (size.cy - OverlayDesignHeight) / 2};
}
}
