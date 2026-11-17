#pragma once
#include "app.hpp"

namespace input_overlay {
inline int ModifierFamily(int input) {
    switch (input) {
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return VK_SHIFT;
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
    case VK_MENU: case VK_LMENU: case VK_RMENU: return VK_MENU;
    default: return InputNone;
    }
}
inline int ModifierLeft(int family) {
    return family == VK_SHIFT ? VK_LSHIFT : family == VK_CONTROL ? VK_LCONTROL : family == VK_MENU ? VK_LMENU : InputNone;
}
inline int ModifierRight(int family) {
    return family == VK_SHIFT ? VK_RSHIFT : family == VK_CONTROL ? VK_RCONTROL : family == VK_MENU ? VK_RMENU : InputNone;
}
inline bool InputsOverlap(int first, int second) {
    if (!first || !second) return false;
    if (first == second) return true;
    const int family = ModifierFamily(first);
    return family && family == ModifierFamily(second) && (first == family || second == family);
}
inline int NormalizeKeyboardInput(UINT vk, UINT scan, bool extended, bool e1 = false) {
    if (!vk || vk >= 255 || scan == 0xff) return InputNone;
    if (vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT) {
        if (extended || e1) return InputNone;
        if (vk != VK_SHIFT) return static_cast<int>(vk);
        return scan == 0x36 ? VK_RSHIFT : VK_LSHIFT;
    }
    if (vk == VK_CONTROL) return e1 ? InputNone : extended ? VK_RCONTROL : VK_LCONTROL;
    if (vk == VK_MENU) return e1 ? InputNone : extended ? VK_RMENU : VK_LMENU;
    if (vk == VK_RETURN) return extended ? NumpadEnter : VK_RETURN;
    if (vk == VK_CANCEL && extended && scan == 0x46) return VK_PAUSE;
    if (!extended && !e1) {
        struct Key { UINT scan, navigation, number; };
        constexpr Key keys[] = {{0x47, VK_HOME, VK_NUMPAD7}, {0x48, VK_UP, VK_NUMPAD8},
            {0x49, VK_PRIOR, VK_NUMPAD9}, {0x4b, VK_LEFT, VK_NUMPAD4}, {0x4c, VK_CLEAR, VK_NUMPAD5},
            {0x4d, VK_RIGHT, VK_NUMPAD6}, {0x4f, VK_END, VK_NUMPAD1}, {0x50, VK_DOWN, VK_NUMPAD2},
            {0x51, VK_NEXT, VK_NUMPAD3}, {0x52, VK_INSERT, VK_NUMPAD0}, {0x53, VK_DELETE, VK_DECIMAL}};
        for (const auto& key : keys)
            if (scan == key.scan && (vk == key.navigation || vk == key.number)) return static_cast<int>(key.number);
    }
    return static_cast<int>(vk);
}
inline bool IsCapturedKeyboardMessage(UINT input, WPARAM key, LPARAM flags) {
    if (!input) return false;
    const UINT scan = (static_cast<ULONG_PTR>(flags) >> 16) & 0xff;
    const UINT vk = static_cast<UINT>(key);
    const bool extended = (static_cast<ULONG_PTR>(flags) & 0x01000000) != 0;
    const int family = ModifierFamily(static_cast<int>(input));
    if (!scan && family && family == static_cast<int>(vk)) return true;
    return static_cast<int>(input) == NormalizeKeyboardInput(vk, scan, extended);
}
}
