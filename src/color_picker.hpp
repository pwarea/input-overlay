#pragma once
#include <windows.h>

namespace input_overlay {
bool ChooseOverlayColor(HWND owner, COLORREF& color, const wchar_t* title);
}
