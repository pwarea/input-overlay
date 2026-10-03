#include "color_picker.hpp"
#include "colors.hpp"
#include <algorithm>
#include <cmath>
#include <cwchar>
#include <dwmapi.h>
#include <windowsx.h>

namespace input_overlay {
namespace {
constexpr wchar_t PickerClass[] = L"InputOverlay.ColorPicker";
constexpr wchar_t PaletteClass[] = L"InputOverlay.ColorPalette";
constexpr COLORREF Background = RGB(19, 23, 31), Surface = RGB(29, 35, 46);
constexpr COLORREF Foreground = RGB(235, 241, 249), Muted = RGB(153, 167, 188);
constexpr COLORREF Border = RGB(47, 57, 72), Accent = RGB(125, 211, 252);
constexpr int ClientWidth = 360, ClientHeight = 508;
constexpr int PaletteId = 100, HueId = 101, HexId = 102;
constexpr DWORD PickerStyle = WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
constexpr DWORD PickerExStyle = WS_EX_DLGMODALFRAME | WS_EX_CONTROLPARENT;

struct Hsv { double hue = 0, saturation = 0, value = 0; };
Hsv ToHsv(COLORREF color, double previousHue = 0) {
    const double red = GetRValue(color) / 255.0, green = GetGValue(color) / 255.0, blue = GetBValue(color) / 255.0;
    const double high = std::max({red, green, blue}), low = std::min({red, green, blue});
    const double range = high - low;
    Hsv hsv{previousHue, high > 0 ? range / high : 0, high};
    if (range > 0) {
        if (high == red) hsv.hue = 60 * std::fmod((green - blue) / range, 6.0);
        else if (high == green) hsv.hue = 60 * ((blue - red) / range + 2);
        else hsv.hue = 60 * ((red - green) / range + 4);
        if (hsv.hue < 0) hsv.hue += 360;
    }
    return hsv;
}
COLORREF FromHsv(const Hsv& hsv) {
    const double chroma = hsv.value * hsv.saturation;
    const double hue = hsv.hue / 60;
    const double cross = chroma * (1 - std::abs(std::fmod(hue, 2.0) - 1));
    const double minimum = hsv.value - chroma;
    double red = 0, green = 0, blue = 0;
    if (hue < 1) { red = chroma; green = cross; }
    else if (hue < 2) { red = cross; green = chroma; }
    else if (hue < 3) { green = chroma; blue = cross; }
    else if (hue < 4) { green = cross; blue = chroma; }
    else if (hue < 5) { red = cross; blue = chroma; }
    else { red = chroma; blue = cross; }
    return RGB(static_cast<BYTE>((red + minimum) * 255 + 0.5),
        static_cast<BYTE>((green + minimum) * 255 + 0.5), static_cast<BYTE>((blue + minimum) * 255 + 0.5));
}
struct Picker {
    HWND window = nullptr, palette = nullptr, hueBar = nullptr, hexLabel = nullptr, edit = nullptr, apply = nullptr, cancel = nullptr;
    UINT dpi = 96;
    COLORREF original = 0, color = 0;
    Hsv hsv;
    bool updating = false, valid = true, accepted = false, done = false;
    HBRUSH background = CreateSolidBrush(Background), surface = CreateSolidBrush(Surface);
    HFONT normal = nullptr, smallFont = nullptr, heading = nullptr;
    HBITMAP saturationBitmap = nullptr, hueBitmap = nullptr;
    DWORD* saturationPixels = nullptr;
    ~Picker() {
        if (normal) DeleteObject(normal);
        if (smallFont) DeleteObject(smallFont);
        if (heading) DeleteObject(heading);
        if (saturationBitmap) DeleteObject(saturationBitmap);
        if (hueBitmap) DeleteObject(hueBitmap);
        if (background) DeleteObject(background);
        if (surface) DeleteObject(surface);
    }
    int Px(int value) const { return MulDiv(value, static_cast<int>(dpi), 96); }
};
DWORD Pixel(COLORREF color) {
    return (static_cast<DWORD>(GetRValue(color)) << 16) | (static_cast<DWORD>(GetGValue(color)) << 8) | GetBValue(color);
}
HBITMAP MakeBitmap(int width, int height, DWORD*& pixels) {
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    return CreateDIBSection(nullptr, &info, DIB_RGB_COLORS, reinterpret_cast<void**>(&pixels), nullptr, 0);
}
void BuildSaturationBitmap(Picker& picker) {
    if (!picker.saturationBitmap) picker.saturationBitmap = MakeBitmap(256, 256, picker.saturationPixels);
    if (!picker.saturationPixels) return;
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x)
            picker.saturationPixels[y * 256 + x] = Pixel(FromHsv({picker.hsv.hue, x / 255.0, 1 - y / 255.0}));
}
void BuildHueBitmap(Picker& picker) {
    DWORD* pixels = nullptr;
    picker.hueBitmap = MakeBitmap(1, 360, pixels);
    if (!pixels) return;
    for (int y = 0; y < 360; ++y) pixels[y] = Pixel(FromHsv({y * 359.999 / 359, 1, 1}));
}
void Fill(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}
void Text(HDC dc, const wchar_t* text, RECT rect, HFONT font, COLORREF color, UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE) {
    HGDIOBJ previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text, -1, &rect, format);
    SelectObject(dc, previous);
}
void Frame(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FrameRect(dc, &rect, brush);
    DeleteObject(brush);
}
void RefreshColor(Picker& picker, bool hueChanged, bool updateText) {
    if (hueChanged) BuildSaturationBitmap(picker);
    if (updateText) {
        picker.updating = true;
        SetWindowTextW(picker.edit, ColorHex(picker.color).c_str());
        picker.updating = false;
        picker.valid = true;
    }
    EnableWindow(picker.apply, picker.valid);
    InvalidateRect(picker.palette, nullptr, FALSE);
    InvalidateRect(picker.hueBar, nullptr, FALSE);
    RECT lower{0, picker.Px(330), picker.Px(ClientWidth), picker.Px(ClientHeight)};
    InvalidateRect(picker.window, &lower, FALSE);
}
void ColorFromPosition(Picker& picker, HWND control, int x, int y) {
    RECT rect{};
    GetClientRect(control, &rect);
    const int inset = std::max(1, picker.Px(2));
    const double horizontal = std::clamp((x - inset) / static_cast<double>(std::max(1L, rect.right - inset * 2 - 1)), 0.0, 1.0);
    const double vertical = std::clamp((y - inset) / static_cast<double>(std::max(1L, rect.bottom - inset * 2 - 1)), 0.0, 1.0);
    const bool isHue = GetDlgCtrlID(control) == HueId;
    if (isHue) picker.hsv.hue = vertical * 359.999;
    else { picker.hsv.saturation = horizontal; picker.hsv.value = 1 - vertical; }
    picker.color = FromHsv(picker.hsv);
    RefreshColor(picker, isHue, true);
}
void PaintPalette(Picker& picker, HWND control) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(control, &paint);
    RECT bounds{};
    GetClientRect(control, &bounds);
    FillRect(dc, &bounds, picker.background);
    const int inset = std::max(1, picker.Px(2));
    RECT content = bounds;
    InflateRect(&content, -inset, -inset);
    const int width = std::max(1L, content.right - content.left), height = std::max(1L, content.bottom - content.top);
    const bool isHue = GetDlgCtrlID(control) == HueId;
    HBITMAP bitmap = isHue ? picker.hueBitmap : picker.saturationBitmap;
    if (bitmap) {
        HDC memory = CreateCompatibleDC(dc);
        HGDIOBJ previous = SelectObject(memory, bitmap);
        SetStretchBltMode(dc, COLORONCOLOR);
        StretchBlt(dc, content.left, content.top, width, height, memory, 0, 0, isHue ? 1 : 256, isHue ? 360 : 256, SRCCOPY);
        SelectObject(memory, previous);
        DeleteDC(memory);
    }
    Frame(dc, bounds, GetFocus() == control ? Accent : Border);
    HPEN dark = CreatePen(PS_SOLID, std::max(2, picker.Px(3)), RGB(10, 14, 20));
    HPEN light = CreatePen(PS_SOLID, std::max(1, picker.Px(1)), Foreground);
    HGDIOBJ oldPen = SelectObject(dc, dark), oldBrush = SelectObject(dc, GetStockObject(NULL_BRUSH));
    if (isHue) {
        const int center = content.top + static_cast<int>(picker.hsv.hue / 359.999 * (height - 1) + 0.5);
        const RECT marker{content.left, center - picker.Px(3), content.right, center + picker.Px(3) + 1};
        Rectangle(dc, marker.left, marker.top, marker.right, marker.bottom);
        SelectObject(dc, light);
        Rectangle(dc, marker.left, marker.top, marker.right, marker.bottom);
    } else {
        const int x = content.left + static_cast<int>(picker.hsv.saturation * (width - 1) + 0.5);
        const int y = content.top + static_cast<int>((1 - picker.hsv.value) * (height - 1) + 0.5);
        const int radius = std::max(3, picker.Px(6));
        Ellipse(dc, x - radius, y - radius, x + radius + 1, y + radius + 1);
        SelectObject(dc, light);
        Ellipse(dc, x - radius, y - radius, x + radius + 1, y + radius + 1);
    }
    SelectObject(dc, oldPen);
    SelectObject(dc, oldBrush);
    DeleteObject(dark);
    DeleteObject(light);
    EndPaint(control, &paint);
}
LRESULT CALLBACK PaletteProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* picker = reinterpret_cast<Picker*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        picker = static_cast<Picker*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(picker));
    }
    if (!picker) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_PAINT: PaintPalette(*picker, window); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_GETDLGCODE: return DLGC_WANTARROWS;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: InvalidateRect(window, nullptr, FALSE); return 0;
    case WM_LBUTTONDOWN:
        SetFocus(window);
        SetCapture(window);
        ColorFromPosition(*picker, window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_MOUSEMOVE:
        if (GetCapture() == window && (wParam & MK_LBUTTON)) ColorFromPosition(*picker, window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
        return 0;
    case WM_LBUTTONUP:
        if (GetCapture() == window) {
            ColorFromPosition(*picker, window, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
            ReleaseCapture();
        }
        return 0;
    case WM_CANCELMODE:
        if (GetCapture() == window) ReleaseCapture();
        return 0;
    case WM_KEYDOWN: {
        const bool isHue = GetDlgCtrlID(window) == HueId;
        const double step = GetKeyState(VK_SHIFT) & 0x8000 ? 10 : 1;
        if (wParam != VK_LEFT && wParam != VK_RIGHT && wParam != VK_UP && wParam != VK_DOWN && wParam != VK_HOME && wParam != VK_END) break;
        if (isHue) {
            if (wParam == VK_HOME) picker->hsv.hue = 0;
            else if (wParam == VK_END) picker->hsv.hue = 359.999;
            else picker->hsv.hue = std::clamp(picker->hsv.hue + ((wParam == VK_DOWN || wParam == VK_RIGHT) ? step : -step), 0.0, 359.999);
        } else if (wParam == VK_LEFT || wParam == VK_RIGHT) {
            picker->hsv.saturation = std::clamp(picker->hsv.saturation + (wParam == VK_RIGHT ? step : -step) / 100, 0.0, 1.0);
        } else if (wParam == VK_UP || wParam == VK_DOWN) {
            picker->hsv.value = std::clamp(picker->hsv.value + (wParam == VK_UP ? step : -step) / 100, 0.0, 1.0);
        } else if (wParam == VK_HOME) { picker->hsv.saturation = 0; picker->hsv.value = 1; }
        else { picker->hsv.saturation = 1; picker->hsv.value = 0; }
        picker->color = FromHsv(picker->hsv);
        RefreshColor(*picker, isHue, true);
        return 0;
    }
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
void Layout(Picker& picker) {
    const auto font = [&](int size, int weight) {
        return CreateFontW(-picker.Px(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
    };
    HFONT previousNormal = picker.normal, previousSmall = picker.smallFont, previousHeading = picker.heading;
    picker.normal = font(14, FW_NORMAL);
    picker.smallFont = font(12, FW_NORMAL);
    picker.heading = font(18, FW_SEMIBOLD);
    for (HWND control : {picker.edit, picker.apply, picker.cancel}) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(picker.normal), FALSE);
    SendMessageW(picker.hexLabel, WM_SETFONT, reinterpret_cast<WPARAM>(picker.smallFont), FALSE);
    if (previousNormal) DeleteObject(previousNormal);
    if (previousSmall) DeleteObject(previousSmall);
    if (previousHeading) DeleteObject(previousHeading);
    const auto place = [&](HWND control, int x, int y, int width, int height) {
        SetWindowPos(control, nullptr, picker.Px(x), picker.Px(y), picker.Px(width), picker.Px(height), SWP_NOZORDER | SWP_NOACTIVATE);
    };
    place(picker.palette, 24, 54, 268, 268);
    place(picker.hueBar, 308, 54, 28, 268);
    place(picker.hexLabel, 112, 338, 224, 22);
    place(picker.edit, 112, 362, 224, 34);
    place(picker.cancel, 24, 450, 148, 36);
    place(picker.apply, 188, 450, 148, 36);
    SendMessageW(picker.edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(picker.Px(10), picker.Px(8)));
    InvalidateRect(picker.window, nullptr, TRUE);
}
void FitWindow(Picker& picker, UINT systemDpi, const RECT* suggested = nullptr) {
    RECT reference{};
    if (suggested) reference = *suggested;
    else GetWindowRect(GetWindow(picker.window, GW_OWNER), &reference);
    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    if (!GetMonitorInfoW(MonitorFromRect(&reference, MONITOR_DEFAULTTONEAREST), &monitor)) SystemParametersInfoW(SPI_GETWORKAREA, 0, &monitor.rcWork, 0);
    RECT frame{};
    AdjustWindowRectExForDpi(&frame, PickerStyle, FALSE, PickerExStyle, systemDpi);
    const int frameWidth = frame.right - frame.left, frameHeight = frame.bottom - frame.top;
    const int availableWidth = std::max(1L, monitor.rcWork.right - monitor.rcWork.left - 24 - frameWidth);
    const int availableHeight = std::max(1L, monitor.rcWork.bottom - monitor.rcWork.top - 24 - frameHeight);
    picker.dpi = static_cast<UINT>(std::max(1, std::min({static_cast<int>(systemDpi), availableWidth * 96 / ClientWidth, availableHeight * 96 / ClientHeight})));
    const int width = picker.Px(ClientWidth) + frameWidth, height = picker.Px(ClientHeight) + frameHeight;
    const LONG desiredX = suggested ? reference.left : reference.left + (reference.right - reference.left - width) / 2;
    const LONG desiredY = suggested ? reference.top : reference.top + (reference.bottom - reference.top - height) / 2;
    const LONG left = std::clamp(desiredX, monitor.rcWork.left + 12, std::max(monitor.rcWork.left + 12, monitor.rcWork.right - width - 12));
    const LONG top = std::clamp(desiredY, monitor.rcWork.top + 12, std::max(monitor.rcWork.top + 12, monitor.rcWork.bottom - height - 12));
    SetWindowPos(picker.window, nullptr, left, top, width, height, SWP_NOZORDER | SWP_NOACTIVATE);
    Layout(picker);
}
void PaintDialog(Picker& picker) {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(picker.window, &paint);
    RECT bounds{};
    GetClientRect(picker.window, &bounds);
    FillRect(dc, &bounds, picker.background);
    const auto rect = [&](int x, int y, int width, int height) { return RECT{picker.Px(x), picker.Px(y), picker.Px(x + width), picker.Px(y + height)}; };
    Text(dc, L"Pick any color", rect(24, 15, 312, 28), picker.heading, Foreground);
    const RECT swatch = rect(24, 352, 72, 44);
    RECT before = swatch, after = swatch;
    before.right = after.left = (swatch.left + swatch.right) / 2;
    Fill(dc, before, picker.original);
    Fill(dc, after, picker.color);
    Frame(dc, swatch, Border);
    Text(dc, L"Old / New", rect(24, 397, 76, 19), picker.smallFont, Muted, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (!picker.valid) Text(dc, L"Use six hex digits, e.g. #7DD3FC.", rect(112, 399, 224, 20), picker.smallFont, RGB(251, 143, 153));
    wchar_t values[64]{};
    swprintf_s(values, L"R  %u     G  %u     B  %u", GetRValue(picker.color), GetGValue(picker.color), GetBValue(picker.color));
    Text(dc, values, rect(112, 419, 224, 20), picker.smallFont, Muted);
    EndPaint(picker.window, &paint);
}
void PaintButton(Picker& picker, const DRAWITEMSTRUCT& item) {
    const bool primary = item.CtlID == IDOK, disabled = item.itemState & ODS_DISABLED, pressed = item.itemState & ODS_SELECTED;
    const COLORREF fill = disabled ? Surface : primary ? (pressed ? RGB(86, 167, 207) : Accent) : (pressed ? RGB(43, 52, 67) : Surface);
    HPEN pen = CreatePen(PS_SOLID, 1, primary && !disabled ? fill : Border);
    HBRUSH brush = CreateSolidBrush(fill);
    HGDIOBJ oldPen = SelectObject(item.hDC, pen), oldBrush = SelectObject(item.hDC, brush);
    FillRect(item.hDC, &item.rcItem, picker.background);
    RoundRect(item.hDC, item.rcItem.left, item.rcItem.top, item.rcItem.right, item.rcItem.bottom, picker.Px(8), picker.Px(8));
    SelectObject(item.hDC, oldPen);
    SelectObject(item.hDC, oldBrush);
    DeleteObject(pen);
    DeleteObject(brush);
    Text(item.hDC, primary ? L"Apply color" : L"Cancel", item.rcItem, picker.normal,
        disabled ? Muted : primary ? Background : Foreground, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    if (item.itemState & ODS_FOCUS) {
        RECT focus = item.rcItem;
        InflateRect(&focus, -picker.Px(4), -picker.Px(4));
        DrawFocusRect(item.hDC, &focus);
    }
}
LRESULT CALLBACK PickerProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* picker = reinterpret_cast<Picker*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        picker = static_cast<Picker*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        picker->window = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(picker));
    }
    if (!picker) return DefWindowProcW(window, message, wParam, lParam);
    switch (message) {
    case WM_CREATE: {
        const HINSTANCE instance = reinterpret_cast<CREATESTRUCTW*>(lParam)->hInstance;
        const auto child = [&](const wchar_t* className, const wchar_t* label, DWORD style, int id, void* parameter = nullptr) {
            return CreateWindowExW(0, className, label, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0, 1, 1, window,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, parameter);
        };
        picker->palette = child(PaletteClass, L"Saturation and brightness. Use arrow keys to adjust.", 0, PaletteId, picker);
        picker->hueBar = child(PaletteClass, L"Hue. Use arrow keys to adjust.", 0, HueId, picker);
        picker->hexLabel = CreateWindowExW(0, L"STATIC", L"HEX", WS_CHILD | WS_VISIBLE | SS_LEFT | SS_CENTERIMAGE | SS_NOPREFIX,
            0, 0, 1, 1, window, nullptr, instance, nullptr);
        picker->edit = child(L"EDIT", L"", ES_AUTOHSCROLL | WS_BORDER, HexId);
        picker->cancel = child(L"BUTTON", L"Cancel", BS_OWNERDRAW, IDCANCEL);
        picker->apply = child(L"BUTTON", L"Apply color", BS_OWNERDRAW, IDOK);
        if (!picker->palette || !picker->hueBar || !picker->hexLabel || !picker->edit || !picker->cancel || !picker->apply) return -1;
        SendMessageW(picker->edit, EM_SETLIMITTEXT, 7, 0);
        BuildSaturationBitmap(*picker);
        BuildHueBitmap(*picker);
        if (!picker->saturationBitmap || !picker->hueBitmap) return -1;
        picker->updating = true;
        SetWindowTextW(picker->edit, ColorHex(picker->color).c_str());
        picker->updating = false;
        return 0;
    }
    case WM_PAINT: PaintDialog(*picker); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_DRAWITEM: PaintButton(*picker, *reinterpret_cast<DRAWITEMSTRUCT*>(lParam)); return TRUE;
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, Foreground);
        SetBkColor(dc, Surface);
        return reinterpret_cast<LRESULT>(picker->surface);
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wParam);
        SetTextColor(dc, Muted);
        SetBkColor(dc, Background);
        return reinterpret_cast<LRESULT>(picker->background);
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == HexId && HIWORD(wParam) == EN_CHANGE && !picker->updating) {
            wchar_t text[16]{};
            GetWindowTextW(picker->edit, text, static_cast<int>(std::size(text)));
            COLORREF color = picker->color;
            picker->valid = ParseColorHex(text, color);
            if (picker->valid) {
                const double previousHue = picker->hsv.hue;
                picker->color = color;
                picker->hsv = ToHsv(color, previousHue);
                RefreshColor(*picker, picker->hsv.hue != previousHue, false);
            } else RefreshColor(*picker, false, false);
            return 0;
        }
        if (LOWORD(wParam) == IDOK) {
            if (!picker->valid) { SetFocus(picker->edit); return 0; }
            picker->accepted = true;
            picker->done = true;
            DestroyWindow(window);
            return 0;
        }
        if (LOWORD(wParam) == IDCANCEL) { SendMessageW(window, WM_CLOSE, 0, 0); return 0; }
        break;
    case DM_GETDEFID: return MAKELRESULT(IDOK, DC_HASDEFID);
    case WM_DPICHANGED: FitWindow(*picker, HIWORD(wParam), reinterpret_cast<RECT*>(lParam)); return 0;
    case WM_DISPLAYCHANGE: {
        RECT current{};
        GetWindowRect(window, &current);
        const UINT dpi = GetDpiForWindow(window);
        FitWindow(*picker, dpi ? dpi : 96, &current);
        return 0;
    }
    case WM_CLOSE:
        picker->done = true;
        DestroyWindow(window);
        return 0;
    case WM_NCDESTROY:
        picker->done = true;
        picker->window = nullptr;
        SetWindowLongPtrW(window, GWLP_USERDATA, 0);
        break;
    }
    return DefWindowProcW(window, message, wParam, lParam);
}
bool RegisterPickerClasses(HINSTANCE instance) {
    WNDCLASSEXW palette{};
    palette.cbSize = sizeof(palette);
    palette.hInstance = instance;
    palette.lpfnWndProc = PaletteProc;
    palette.lpszClassName = PaletteClass;
    palette.hCursor = LoadCursorW(nullptr, IDC_CROSS);
    if (!RegisterClassExW(&palette) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
    WNDCLASSEXW picker{};
    picker.cbSize = sizeof(picker);
    picker.hInstance = instance;
    picker.lpfnWndProc = PickerProc;
    picker.lpszClassName = PickerClass;
    picker.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    return RegisterClassExW(&picker) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
}
}

bool ChooseOverlayColor(HWND owner, COLORREF& color, const wchar_t* title) {
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!RegisterPickerClasses(instance)) return false;
    Picker picker;
    picker.original = picker.color = color;
    picker.hsv = ToHsv(color);
    const bool restoreOwner = owner && IsWindow(owner) && IsWindowEnabled(owner);
    const HWND previousFocus = GetFocus();
    HWND window = CreateWindowExW(PickerExStyle, PickerClass, title && *title ? title : L"Choose color", PickerStyle,
        CW_USEDEFAULT, CW_USEDEFAULT, 1, 1, owner, nullptr, instance, &picker);
    if (!window) return false;
    const BOOL dark = TRUE;
    if (FAILED(DwmSetWindowAttribute(window, 20, &dark, sizeof(dark)))) DwmSetWindowAttribute(window, 19, &dark, sizeof(dark));
    const UINT dpi = GetDpiForWindow(owner ? owner : window);
    FitWindow(picker, dpi ? dpi : 96);
    if (restoreOwner) EnableWindow(owner, FALSE);
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    SetFocus(picker.edit);
    SendMessageW(picker.edit, EM_SETSEL, 0, -1);
    MSG message{};
    int quitCode = 0;
    bool quit = false;
    while (!picker.done) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) {
            quit = result == 0;
            quitCode = static_cast<int>(message.wParam);
            break;
        }
        if (!IsDialogMessageW(window, &message)) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
    }
    if (IsWindow(window)) DestroyWindow(window);
    if (restoreOwner && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetForegroundWindow(owner);
        if (IsWindow(previousFocus)) SetFocus(previousFocus);
    }
    if (quit) PostQuitMessage(quitCode);
    if (picker.accepted) color = picker.color;
    return picker.accepted;
}
}
