#include "app.hpp"
#include "updates.hpp"
#include <commctrl.h>
#include <uxtheme.h>
#include <dwmapi.h>
#include <algorithm>
#include <cwchar>

namespace input_overlay {
namespace {
constexpr COLORREF Background = RGB(19, 23, 31), Sidebar = RGB(15, 19, 26);
constexpr COLORREF Surface = RGB(29, 35, 46), Border = RGB(47, 57, 72);
constexpr COLORREF Foreground = RGB(235, 241, 249), Muted = RGB(153, 167, 188), Blue = RGB(125, 211, 252);
constexpr int ClientWidth = 824, ClientHeight = 670;
constexpr DWORD WindowStyle = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
constexpr DWORD WindowExStyle = WS_EX_APPWINDOW | WS_EX_CONTROLPARENT;
enum ControlId {
    NavOverlay = 100, NavBindings, NavApplications, NavGeneral, NavController, NavUpdates,
    Enabled = 200, MouseVisible, Scale, ScaleValue, Opacity, OpacityValue,
    MoveOverlay, Layout, ResetPosition, AccentFirst = 220, AccentLast = 224, StyleFirst = 230, StyleLast = StyleFirst + OverlayStyleCount - 1,
    SlotList = 300, SlotTitle, SlotVisible, LabelEdit, SaveLabel, InputValue, RecordInput, UnbindInput, ResetBinding,
    RestrictApps = 400, WindowList, RefreshWindows, AddWindow, AllowedList, RemoveApp, WindowPath,
    HotkeyValue = 500, ModCtrl, ModAlt, ModShift, ModWin, RecordHotkey, Startup, StartMinimized,
    ControllerMode = 600, ControllerLayoutChoice, ControllerIndex, ControllerDeadzone, ControllerDeadzoneValue,
    AutoCheckUpdates = 700, CheckUpdates, InstallUpdate, UpdateVersion, UpdateMessage, UpdateNotesTitle, UpdateNotes,
    Status = 900, ExitApp = 950
};
const std::array<COLORREF, 5> Accents{{RGB(125, 211, 252), RGB(167, 139, 250), RGB(110, 231, 183), RGB(251, 191, 36), RGB(251, 113, 133)}};
const wchar_t* StyleNames[] = {L"Outline", L"Neon", L"Glass", L"Circuit", L"Pearl"};
const wchar_t* PageTitles[] = {L"Overlay", L"Bindings", L"Applications", L"General", L"Controller", L"Updates"};
const wchar_t* PageSubtitles[] = {
    L"A clear view of every input, exactly where you want it.",
    L"Choose which input lights up each element.",
    L"Show the overlay only when your chosen application is active.",
    L"Your shortcut, startup preferences, and compatibility.",
    L"A controller view with live buttons, triggers, and sticks.",
    L"Keep Input Overlay current and see what changed."
};
struct UiState {
    UINT dpi = 96;
    HFONT normal = nullptr, smallFont = nullptr, heading = nullptr, title = nullptr;
    HBRUSH background = nullptr, surface = nullptr;
    int selectedSlot = 0;
    bool updating = false;
    std::wstring status;
};
UiState ui;
int Px(int value) { return MulDiv(value, static_cast<int>(ui.dpi), 96); }
HFONT MakeFont(int size, int weight) {
    return CreateFontW(-Px(size), 0, 0, 0, weight, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Segoe UI");
}
void FreeFonts() {
    if (ui.normal) DeleteObject(ui.normal);
    if (ui.smallFont) DeleteObject(ui.smallFont);
    if (ui.heading) DeleteObject(ui.heading);
    if (ui.title) DeleteObject(ui.title);
    ui.normal = ui.smallFont = ui.heading = ui.title = nullptr;
}
void CreateFonts() {
    FreeFonts();
    ui.normal = MakeFont(14, FW_NORMAL); ui.smallFont = MakeFont(12, FW_NORMAL);
    ui.heading = MakeFont(16, FW_SEMIBOLD); ui.title = MakeFont(28, FW_SEMIBOLD);
}
UINT WindowDpi(HWND window) {
    const UINT dpi = GetDpiForWindow(window);
    return dpi ? dpi : 96;
}
struct WindowLayout { UINT dpi; RECT bounds; };
WindowLayout FitLayout(HMONITOR monitor, UINT systemDpi, const RECT* position = nullptr) {
    MONITORINFO info{}; info.cbSize = sizeof(info); GetMonitorInfoW(monitor, &info);
    RECT frame{};
    AdjustWindowRectExForDpi(&frame, WindowStyle, FALSE, WindowExStyle, systemDpi);
    const int frameWidth = frame.right - frame.left, frameHeight = frame.bottom - frame.top;
    constexpr int margin = 12;
    const int availableWidth = std::max(1L, info.rcWork.right - info.rcWork.left - margin * 2 - frameWidth);
    const int availableHeight = std::max(1L, info.rcWork.bottom - info.rcWork.top - margin * 2 - frameHeight);
    const UINT dpi = static_cast<UINT>(std::max(1, std::min({static_cast<int>(systemDpi),
        availableWidth * 96 / ClientWidth, availableHeight * 96 / ClientHeight})));
    const int width = MulDiv(ClientWidth, static_cast<int>(dpi), 96) + frameWidth;
    const int height = MulDiv(ClientHeight, static_cast<int>(dpi), 96) + frameHeight;
    const LONG preferredX = position ? position->left : info.rcWork.left + (info.rcWork.right - info.rcWork.left - width) / 2;
    const LONG preferredY = position ? position->top : info.rcWork.top + (info.rcWork.bottom - info.rcWork.top - height) / 2;
    const LONG left = std::clamp(preferredX, info.rcWork.left + margin, std::max(info.rcWork.left + margin, info.rcWork.right - width - margin));
    const LONG top = std::clamp(preferredY, info.rcWork.top + margin, std::max(info.rcWork.top + margin, info.rcWork.bottom - height - margin));
    return {dpi, {left, top, left + width, top + height}};
}
bool FitWindow(HWND window, UINT systemDpi, const RECT* suggested = nullptr) {
    RECT current{}; GetWindowRect(window, &current);
    const RECT& requested = suggested ? *suggested : current;
    const WindowLayout layout = FitLayout(MonitorFromRect(&requested, MONITOR_DEFAULTTONEAREST), systemDpi, &requested);
    const bool scaleChanged = ui.dpi != layout.dpi;
    ui.dpi = layout.dpi;
    if (!EqualRect(&current, &layout.bounds)) SetWindowPos(window, nullptr, layout.bounds.left, layout.bounds.top,
        layout.bounds.right - layout.bounds.left, layout.bounds.bottom - layout.bounds.top, SWP_NOZORDER | SWP_NOACTIVATE);
    return scaleChanged;
}
std::wstring Text(HWND parent, int id) {
    HWND child = GetDlgItem(parent, id);
    const int count = GetWindowTextLengthW(child);
    std::wstring text(static_cast<size_t>(count) + 1, L'\0');
    GetWindowTextW(child, text.data(), count + 1); text.resize(static_cast<size_t>(count));
    return text;
}
void SetText(HWND parent, int id, const std::wstring& text) { SetDlgItemTextW(parent, id, text.c_str()); }
void Check(HWND parent, int id, bool checked) { SendDlgItemMessageW(parent, id, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0); }
bool Checked(HWND parent, int id) { return SendDlgItemMessageW(parent, id, BM_GETCHECK, 0, 0) == BST_CHECKED; }
void Fill(HDC dc, RECT rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color); FillRect(dc, &rect, brush); DeleteObject(brush);
}
void DrawTextAt(HDC dc, const wchar_t* text, RECT rect, HFONT font, COLORREF color, UINT format) {
    const HGDIOBJ old = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT); SetTextColor(dc, color); DrawTextW(dc, text, -1, &rect, format); SelectObject(dc, old);
}
COLORREF Tint(COLORREF base, COLORREF accent, int percent) {
    return RGB((GetRValue(base) * (100 - percent) + GetRValue(accent) * percent) / 100,
        (GetGValue(base) * (100 - percent) + GetGValue(accent) * percent) / 100,
        (GetBValue(base) * (100 - percent) + GetBValue(accent) * percent) / 100);
}
void DrawStylePreview(HDC dc, const RECT& card, OverlayStyle style, COLORREF accent, COLORREF background) {
    const LONG left = card.left + (card.right - card.left - Px(30)) / 2;
    RECT key{left, card.top + Px(8), left + Px(30), card.top + Px(38)};
    auto keycap = [&](RECT bounds, COLORREF edge, COLORREF fill, int thickness) {
        HPEN pen = CreatePen(PS_SOLID, std::max(1, Px(thickness)), edge);
        HBRUSH brush = CreateSolidBrush(fill);
        HGDIOBJ oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, brush);
        RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, Px(6), Px(6));
        SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(pen); DeleteObject(brush);
    };
    COLORREF letter = Foreground;
    if (style == OverlayStyle::Outline) {
        keycap(key, Foreground, background, 1);
    } else if (style == OverlayStyle::Neon) {
        RECT glow = key; InflateRect(&glow, Px(2), Px(2));
        keycap(glow, Tint(background, accent, 26), background, 3);
        keycap(key, accent, Tint(background, accent, 9), 1);
        letter = accent;
    } else if (style == OverlayStyle::Glass) {
        keycap(key, Tint(Foreground, accent, 25), Tint(background, accent, 32), 1);
        RECT shine{key.left + Px(5), key.top + Px(3), key.right - Px(5), key.top + Px(5)};
        Fill(dc, shine, Tint(Foreground, accent, 15));
    } else if (style == OverlayStyle::Circuit) {
        HPEN pen = CreatePen(PS_SOLID, std::max(1, Px(1)), accent);
        HBRUSH brush = CreateSolidBrush(Tint(background, accent, 8));
        HGDIOBJ oldPen = SelectObject(dc, pen), oldBrush = SelectObject(dc, brush);
        const int cut = Px(5);
        POINT outline[] = {{key.left + cut, key.top}, {key.right, key.top}, {key.right, key.bottom - cut},
            {key.right - cut, key.bottom}, {key.left, key.bottom}, {key.left, key.top + cut}};
        Polygon(dc, outline, 6);
        MoveToEx(dc, key.left - Px(4), key.top + Px(9), nullptr); LineTo(dc, key.left, key.top + Px(9));
        MoveToEx(dc, key.right, key.bottom - Px(9), nullptr); LineTo(dc, key.right + Px(4), key.bottom - Px(9));
        SelectObject(dc, oldPen); SelectObject(dc, oldBrush); DeleteObject(pen); DeleteObject(brush);
        letter = accent;
    } else if (style == OverlayStyle::Pearl) {
        keycap(key, Tint(background, Foreground, 70), Tint(background, Foreground, 5), 1);
        RECT glint{key.left + Px(6), key.top + Px(2), key.right - Px(6), key.top + Px(3)};
        Fill(dc, glint, Tint(Foreground, accent, 8));
        RECT reflection{key.left + Px(6), key.bottom - Px(4), key.right - Px(6), key.bottom - Px(3)};
        Fill(dc, reflection, Tint(background, Foreground, 19));
    }
    DrawTextAt(dc, L"W", key, ui.smallFont, letter, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}
std::wstring SlotName(size_t slot) {
    if (slot < KeyboardCount) return DefaultSettings().slots[slot].label + L" key";
    static const wchar_t* names[] = {L"Mouse left", L"Mouse right", L"Mouse middle", L"Mouse side 1", L"Mouse side 2", L"Wheel up", L"Wheel down"};
    return names[slot - KeyboardCount];
}
std::wstring FileName(const std::wstring& path) {
    const size_t slash = path.find_last_of(L"\\/"); return slash == std::wstring::npos ? path : path.substr(slash + 1);
}
UINT SelectedModifiers(HWND window) {
    UINT result = 0;
    if (Checked(window, ModCtrl)) result |= MOD_CONTROL;
    if (Checked(window, ModAlt)) result |= MOD_ALT;
    if (Checked(window, ModShift)) result |= MOD_SHIFT;
    if (Checked(window, ModWin)) result |= MOD_WIN;
    return result;
}

bool UpdateBusy(UpdateStatus status) {
    return status == UpdateStatus::Checking || status == UpdateStatus::Downloading || status == UpdateStatus::Ready;
}

std::wstring UpdateMessageText(const UpdateResult& result) {
    if (!result.message.empty()) return result.message;
    switch (result.status) {
    case UpdateStatus::Idle: return L"Check for the latest build of Input Overlay.";
    case UpdateStatus::Checking: return L"Checking for updates...";
    case UpdateStatus::UpToDate: return L"You are using the latest build.";
    case UpdateStatus::Available: return result.info.version.empty() ? L"A new build is available." : L"Version " + result.info.version + L" is available.";
    case UpdateStatus::BuildPending: return L"A newer commit is available. Its Windows build is not ready yet.";
    case UpdateStatus::Downloading: return L"Downloading and verifying the update...";
    case UpdateStatus::Ready: return L"The update is ready. Preparing installation...";
    case UpdateStatus::Error: return L"The update could not be completed. Try again.";
    }
    return L"";
}

std::wstring UpdateNotesText(const UpdateResult& result) {
    std::wstring notes;
    for (const auto& note : result.info.notes) {
        if (note.empty()) continue;
        if (!notes.empty()) notes += L"\r\n\r\n";
        notes += L"\u2022 " + note;
    }
    if (!notes.empty()) return notes;
    if (result.status == UpdateStatus::UpToDate) return L"No update is available.";
    if (result.status == UpdateStatus::BuildPending) return L"Release notes will appear when the new build is ready.";
    if (result.status == UpdateStatus::Available || result.status == UpdateStatus::Downloading || result.status == UpdateStatus::Ready)
        return L"No release notes were provided.";
    return L"Release notes will appear here when an update is available.";
}
}

bool SettingsWindow::Show(Application& app) {
    app_ = &app;
    if (!hwnd_) {
        WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
        wc.lpfnWndProc = WndProc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = LoadIconW(wc.hInstance, MAKEINTRESOURCEW(1));
        if (!wc.hIcon) wc.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        wc.hIconSm = wc.hIcon; wc.lpszClassName = L"InputOverlay.Settings";
        if (!RegisterClassExW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        INITCOMMONCONTROLSEX common{sizeof(common), ICC_BAR_CLASSES}; InitCommonControlsEx(&common);
        const HWND foreground = GetForegroundWindow();
        const WindowLayout layout = FitLayout(MonitorFromWindow(foreground, MONITOR_DEFAULTTOPRIMARY), WindowDpi(foreground));
        ui.dpi = layout.dpi;
        hwnd_ = CreateWindowExW(WindowExStyle, wc.lpszClassName,
            L"Input Overlay — Settings", WindowStyle, layout.bounds.left, layout.bounds.top,
            layout.bounds.right - layout.bounds.left, layout.bounds.bottom - layout.bounds.top, nullptr, nullptr, wc.hInstance, this);
        if (!hwnd_) return false;
    }
    ShowWindow(hwnd_, SW_RESTORE);
    if (FitWindow(hwnd_, WindowDpi(hwnd_))) { CreateFonts(); Build(); }
    Refresh(); SetForegroundWindow(hwnd_); return true;
}
bool SettingsWindow::ShowUpdates(Application& app) {
    if (!Show(app)) return false;
    SelectPage(5);
    UpdateControls();
    return true;
}
void SettingsWindow::Destroy() { if (hwnd_) DestroyWindow(hwnd_); hwnd_ = nullptr; }

void SettingsWindow::Build() {
    ui.updating = true;
    for (HWND control : controls_) DestroyWindow(control);
    controls_.clear();
    auto add = [&](const wchar_t* className, const wchar_t* label, DWORD style,
                   int x, int y, int width, int height, int id, HFONT font = nullptr, DWORD ex = 0) {
        HWND control = CreateWindowExW(ex, className, label, WS_CHILD | WS_VISIBLE | style,
            Px(x), Px(y), Px(width), Px(height), hwnd_, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font ? font : ui.normal), TRUE);
        controls_.push_back(control); return control;
    };
    auto text = [&](const wchar_t* label, int x, int y, int width, int height,
                    int id = 0, bool muted = false, HFONT font = nullptr) {
        HWND control = add(L"STATIC", label, SS_LEFT, x, y, width, height, id, font);
        if (muted) SetPropW(control, L"InputOverlay.Muted", reinterpret_cast<HANDLE>(1));
        return control;
    };
    auto button = [&](const wchar_t* label, int x, int y, int width, int height, int id, bool primary = false) {
        HWND control = add(L"BUTTON", label, WS_TABSTOP | BS_OWNERDRAW, x, y, width, height, id);
        if (primary) SetPropW(control, L"InputOverlay.Primary", reinterpret_cast<HANDLE>(1));
        return control;
    };
    auto checkbox = [&](const wchar_t* label, int x, int y, int width, int id) {
        HWND control = add(L"BUTTON", label, WS_TABSTOP | BS_AUTOCHECKBOX, x, y, width, 26, id);
        SetWindowTheme(control, L"", L""); return control;
    };
    auto list = [&](int x, int y, int width, int height, int id) {
        HWND control = add(L"LISTBOX", L"", WS_TABSTOP | WS_VSCROLL | WS_HSCROLL | LBS_NOTIFY | LBS_NOINTEGRALHEIGHT | WS_BORDER,
            x, y, width, height, id, nullptr, WS_EX_CLIENTEDGE);
        SetWindowTheme(control, L"", L""); return control;
    };
    for (int i = 0; i < 6; ++i) button(PageTitles[i], 16, 126 + i * 49, 152, 40, NavOverlay + i);
    button(L"Exit Input Overlay", 16, 570, 152, 36, ExitApp);
    text(L"Changes save automatically.", 216, 638, 400, 20, 0, true, ui.smallFont);
    text(ui.status.c_str(), 216, 593, 578, 38, Status, true, ui.smallFont);
    if (page_ == 0) {
        checkbox(L"Show overlay", 216, 113, 230, Enabled);
        checkbox(L"Show mouse", 498, 113, 230, MouseVisible);
        text(L"Keyboard layout", 216, 160, 276, 25, 0, false, ui.heading);
        HWND layout = add(L"COMBOBOX", L"Keyboard layout", WS_TABSTOP | CBS_DROPDOWNLIST, 498, 157, 296, 110, Layout);
        SendMessageW(layout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"ANSI"));
        SendMessageW(layout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"ISO"));
        text(L"Style", 216, 195, 578, 24, 0, false, ui.heading);
        for (int i = 0; i < OverlayStyleCount; ++i) button(StyleNames[i], 216 + i * 118, 225, 106, 68, StyleFirst + i);
        text(L"Size", 216, 315, 188, 24, 0, false, ui.heading);
        text(L"", 424, 315, 80, 24, ScaleValue, true);
        HWND scale = add(TRACKBAR_CLASSW, L"Overlay size", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, 208, 347, 280, 34, Scale);
        SendMessageW(scale, TBM_SETRANGE, TRUE, MAKELPARAM(10, 200)); SendMessageW(scale, TBM_SETPAGESIZE, 0, 10);
        text(L"Opacity", 516, 315, 188, 24, 0, false, ui.heading);
        text(L"", 724, 315, 70, 24, OpacityValue, true);
        HWND opacity = add(TRACKBAR_CLASSW, L"Overlay opacity", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS, 508, 347, 290, 34, Opacity);
        SendMessageW(opacity, TBM_SETRANGE, TRUE, MAKELPARAM(15, 100)); SendMessageW(opacity, TBM_SETPAGESIZE, 0, 5);
        text(L"Accent", 216, 389, 500, 26, 0, false, ui.heading);
        const wchar_t* accentNames[] = {L"Sky", L"Violet", L"Mint", L"Amber", L"Rose"};
        for (int i = 0; i < 5; ++i) button(accentNames[i], 216 + i * 114, 427, 104, 38, AccentFirst + i);
        button(L"Move overlay", 216, 496, 174, 38, MoveOverlay);
        button(L"Reset position and size", 406, 496, 212, 38, ResetPosition);
        text(L"Drag the overlay, then select Done moving.", 216, 548, 578, 26, 0, true);
    } else if (page_ == 1) {
        text(L"Display position", 216, 113, 205, 24, 0, true); list(216, 145, 211, 418, SlotList);
        text(L"", 452, 113, 342, 28, SlotTitle, false, ui.heading);
        checkbox(L"Show this element", 452, 154, 334, SlotVisible);
        text(L"Display label", 452, 208, 330, 24, 0, true);
        HWND edit = add(L"EDIT", L"", WS_TABSTOP | ES_AUTOHSCROLL | WS_BORDER, 452, 239, 229, 31, LabelEdit, nullptr, WS_EX_CLIENTEDGE);
        SendMessageW(edit, EM_SETLIMITTEXT, 24, 0); button(L"Save", 693, 238, 101, 33, SaveLabel);
        text(L"Assigned input", 452, 308, 330, 24, 0, true);
        text(L"", 452, 337, 342, 28, InputValue, false, ui.heading);
        button(L"Record input", 452, 380, 166, 38, RecordInput, true); button(L"Unbind", 630, 380, 164, 38, UnbindInput);
        button(L"Reset to default", 452, 430, 166, 38, ResetBinding);
        text(L"Any key can light up any element, including a mouse side button. Assigning an input moves it from its previous element. The display layout stays in place.",
            452, 484, 342, 92, 0, true);
    } else if (page_ == 2) {
        checkbox(L"Show only in selected applications", 216, 114, 578, RestrictApps);
        text(L"The overlay follows the foreground application and hides immediately when you switch away.", 216, 152, 578, 45, 0, true);
        text(L"Open windows", 216, 211, 350, 27, 0, false, ui.heading); button(L"Refresh", 692, 204, 102, 33, RefreshWindows);
        add(L"COMBOBOX", L"Open application windows", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, 216, 250, 405, 230, WindowList);
        button(L"Add selected", 633, 248, 161, 34, AddWindow, true);
        text(L"", 216, 291, 578, 36, WindowPath, true, ui.smallFont);
        text(L"Selected applications", 216, 336, 578, 25, 0, false, ui.heading); list(216, 373, 578, 162, AllowedList);
        button(L"Remove selected", 630, 546, 164, 33, RemoveApp); windows_ = EnumerateApplications();
    } else if (page_ == 3) {
        text(L"Instant visibility shortcut", 216, 114, 570, 28, 0, false, ui.heading);
        text(L"", 216, 153, 578, 29, HotkeyValue, false, ui.heading);
        checkbox(L"Ctrl", 216, 197, 94, ModCtrl); checkbox(L"Alt", 320, 197, 94, ModAlt);
        checkbox(L"Shift", 424, 197, 94, ModShift); checkbox(L"Win", 528, 197, 94, ModWin);
        button(L"Record shortcut", 216, 244, 187, 38, RecordHotkey, true);
        text(L"Choose the modifiers above, then record a keyboard key. The shortcut shows or hides the overlay instantly.", 216, 299, 578, 49, 0, true);
        text(L"Startup", 216, 367, 578, 26, 0, false, ui.heading);
        checkbox(L"Run at startup", 216, 407, 276, Startup);
        checkbox(L"Start minimized", 516, 407, 278, StartMinimized);
        text(L"Turn off before moving or deleting the portable folder.", 216, 440, 276, 43, 0, true, ui.smallFont);
        text(L"Open in the system tray. Keep the last overlay state.", 516, 440, 278, 43, 0, true, ui.smallFont);
        text(L"Full-screen compatibility", 216, 495, 578, 26, 0, false, ui.heading);
        text(L"Works above normal and borderless full-screen windows. Exclusive full-screen games may cover desktop overlays; choose borderless mode in the game.",
            216, 531, 578, 49, 0, true);
    } else if (page_ == 4) {
        checkbox(L"Show controller instead of keyboard and mouse", 216, 113, 578, ControllerMode);
        text(L"Controller layout", 216, 163, 276, 25, 0, false, ui.heading);
        HWND layout = add(L"COMBOBOX", L"Controller layout", WS_TABSTOP | CBS_DROPDOWNLIST,
            498, 160, 296, 120, ControllerLayoutChoice);
        SendMessageW(layout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Xbox"));
        SendMessageW(layout, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"PlayStation"));
        text(L"Changes the button symbols shown on the overlay.", 216, 204, 578, 22, 0, true, ui.smallFont);
        text(L"Controller", 216, 249, 276, 25, 0, false, ui.heading);
        HWND controller = add(L"COMBOBOX", L"Controller", WS_TABSTOP | CBS_DROPDOWNLIST,
            498, 246, 296, 190, ControllerIndex);
        SendMessageW(controller, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Auto"));
        for (int i = 1; i <= 4; ++i) {
            const std::wstring label = L"Controller " + std::to_wstring(i);
            SendMessageW(controller, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        text(L"Auto uses the first connected XInput controller. Choose a number to use a specific controller.",
            216, 291, 578, 42, 0, true, ui.smallFont);
        text(L"Stick deadzone", 216, 350, 460, 25, 0, false, ui.heading);
        text(L"", 724, 350, 70, 24, ControllerDeadzoneValue, true);
        HWND deadzone = add(TRACKBAR_CLASSW, L"Stick deadzone", WS_TABSTOP | TBS_HORZ | TBS_NOTICKS,
            208, 385, 590, 34, ControllerDeadzone);
        SendMessageW(deadzone, TBM_SETRANGE, TRUE, MAKELPARAM(0, 40));
        SendMessageW(deadzone, TBM_SETPAGESIZE, 0, 5);
        text(L"Ignore small stick movements around the center. Lower values show finer movements.",
            216, 430, 578, 22, 0, true, ui.smallFont);
        text(L"Bindings customizes keyboard and mouse inputs only.", 216, 461, 578, 21, 0, true, ui.smallFont);
        text(L"Device compatibility", 216, 492, 578, 26, 0, false, ui.heading);
        text(L"Requires an XInput-compatible controller. PlayStation layout changes the appearance; it does not add native PlayStation device support.",
            216, 532, 578, 48, 0, true);
    } else {
        text(L"Installed version", 216, 115, 578, 25, 0, false, ui.heading);
        text(L"", 216, 151, 578, 29, UpdateVersion, true);
        checkbox(L"Check for updates at startup", 216, 198, 578, AutoCheckUpdates);
        text(L"Checks the latest main build once per launch. Downloads start only when you choose to install.",
            216, 237, 578, 43, 0, true, ui.smallFont);
        button(L"Check for updates", 216, 296, 194, 38, CheckUpdates);
        button(L"Download and install", 426, 296, 226, 38, InstallUpdate, true);
        text(L"", 216, 352, 578, 48, UpdateMessage);
        text(L"Release notes", 216, 412, 578, 25, UpdateNotesTitle, false, ui.heading);
        HWND notes = add(L"EDIT", L"", WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | WS_BORDER,
            216, 448, 578, 132, UpdateNotes, nullptr, WS_EX_CLIENTEDGE);
        SendMessageW(notes, EM_SETLIMITTEXT, 262144, 0);
        SetWindowTheme(notes, L"", L"");
    }
    ui.updating = false; UpdateControls(); InvalidateRect(hwnd_, nullptr, TRUE);
}

void SettingsWindow::SelectPage(int page) {
    if (page < 0 || page > 5 || page == page_) return;
    CancelCapture(); page_ = page; ui.status.clear(); Build(); SetFocus(GetDlgItem(hwnd_, NavOverlay + page_));
}
void SettingsWindow::UpdateControls() {
    if (!hwnd_ || !app_ || ui.updating) return;
    ui.updating = true;
    Settings& settings = app_->settings;
    if (page_ == 0) {
        Check(hwnd_, Enabled, settings.enabled); Check(hwnd_, MouseVisible, settings.showMouse);
        SendDlgItemMessageW(hwnd_, Layout, CB_SETCURSEL, settings.isoLayout ? 1 : 0, 0);
        SendDlgItemMessageW(hwnd_, Scale, TBM_SETPOS, TRUE, settings.scale);
        SendDlgItemMessageW(hwnd_, Opacity, TBM_SETPOS, TRUE, settings.opacity);
        SetText(hwnd_, ScaleValue, std::to_wstring(settings.scale) + L"%"); SetText(hwnd_, OpacityValue, std::to_wstring(settings.opacity) + L"%");
        SetText(hwnd_, MoveOverlay, app_->overlay.Editing() ? L"Done moving" : L"Move overlay");
        for (int i = AccentFirst; i <= AccentLast; ++i) InvalidateRect(GetDlgItem(hwnd_, i), nullptr, FALSE);
        for (int i = StyleFirst; i <= StyleLast; ++i) InvalidateRect(GetDlgItem(hwnd_, i), nullptr, FALSE);
    } else if (page_ == 1) {
        const auto& slot = settings.slots[static_cast<size_t>(ui.selectedSlot)];
        HWND list = GetDlgItem(hwnd_, SlotList); SendMessageW(list, WM_SETREDRAW, FALSE, 0);
        const LRESULT top = SendMessageW(list, LB_GETTOPINDEX, 0, 0); SendMessageW(list, LB_RESETCONTENT, 0, 0);
        for (size_t i = 0; i < SlotCount; ++i) {
            std::wstring name = SlotName(i); if (!settings.slots[i].visible) name += L"  (hidden)";
            SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(name.c_str()));
        }
        SendMessageW(list, LB_SETCURSEL, ui.selectedSlot, 0); if (top != LB_ERR) SendMessageW(list, LB_SETTOPINDEX, top, 0);
        SendMessageW(list, WM_SETREDRAW, TRUE, 0); InvalidateRect(list, nullptr, TRUE);
        SetText(hwnd_, SlotTitle, SlotName(static_cast<size_t>(ui.selectedSlot))); Check(hwnd_, SlotVisible, slot.visible);
        if (GetFocus() != GetDlgItem(hwnd_, LabelEdit)) SetText(hwnd_, LabelEdit, slot.label);
        SetText(hwnd_, InputValue, slot.input == InputNone ? L"Not assigned" : InputName(slot.input));
        SetText(hwnd_, RecordInput, captureSlot_ >= 0 ? L"Cancel recording" : L"Record input");
    } else if (page_ == 2) {
        Check(hwnd_, RestrictApps, settings.onlySelectedApps);
        HWND combo = GetDlgItem(hwnd_, WindowList); LRESULT selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        for (const auto& window : windows_) {
            std::wstring entry = window.title + L"  —  " + FileName(window.path);
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(entry.c_str()));
        }
        if (selected < 0 || static_cast<size_t>(selected) >= windows_.size()) selected = 0;
        SendMessageW(combo, CB_SETCURSEL, selected, 0);
        SetText(hwnd_, WindowPath, windows_.empty() ? L"No eligible windows found. Open an application, then refresh." : windows_[static_cast<size_t>(selected)].path);
        EnableWindow(GetDlgItem(hwnd_, AddWindow), !windows_.empty());
        HWND allowed = GetDlgItem(hwnd_, AllowedList); LRESULT oldSelection = SendMessageW(allowed, LB_GETCURSEL, 0, 0);
        SendMessageW(allowed, LB_RESETCONTENT, 0, 0);
        HDC dc = GetDC(allowed); HGDIOBJ font = SelectObject(dc, ui.normal); int widest = 0;
        for (const auto& path : settings.applications) {
            SendMessageW(allowed, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(path.c_str()));
            SIZE extent{}; GetTextExtentPoint32W(dc, path.c_str(), static_cast<int>(path.size()), &extent);
            widest = std::max(widest, static_cast<int>(extent.cx) + Px(14));
        }
        SelectObject(dc, font); ReleaseDC(allowed, dc); SendMessageW(allowed, LB_SETHORIZONTALEXTENT, widest, 0);
        if (oldSelection < 0 || static_cast<size_t>(oldSelection) >= settings.applications.size()) oldSelection = 0;
        SendMessageW(allowed, LB_SETCURSEL, oldSelection, 0); EnableWindow(GetDlgItem(hwnd_, RemoveApp), !settings.applications.empty());
    } else if (page_ == 3) {
        SetText(hwnd_, HotkeyValue, HotkeyName(settings.hotkeyModifiers, settings.hotkeyVk));
        if (captureSlot_ != -2) {
            Check(hwnd_, ModCtrl, (settings.hotkeyModifiers & MOD_CONTROL) != 0); Check(hwnd_, ModAlt, (settings.hotkeyModifiers & MOD_ALT) != 0);
            Check(hwnd_, ModShift, (settings.hotkeyModifiers & MOD_SHIFT) != 0); Check(hwnd_, ModWin, (settings.hotkeyModifiers & MOD_WIN) != 0);
        }
        SetText(hwnd_, RecordHotkey, captureSlot_ == -2 ? L"Cancel recording" : L"Record shortcut"); Check(hwnd_, Startup, StartupEnabled());
        Check(hwnd_, StartMinimized, settings.startMinimized);
    } else if (page_ == 4) {
        Check(hwnd_, ControllerMode, settings.device == OverlayDevice::Controller);
        SendDlgItemMessageW(hwnd_, ControllerLayoutChoice, CB_SETCURSEL, static_cast<int>(settings.controllerLayout), 0);
        SendDlgItemMessageW(hwnd_, ControllerIndex, CB_SETCURSEL, settings.controllerIndex + 1, 0);
        SendDlgItemMessageW(hwnd_, ControllerDeadzone, TBM_SETPOS, TRUE, settings.controllerDeadzone);
        SetText(hwnd_, ControllerDeadzoneValue, std::to_wstring(settings.controllerDeadzone) + L"%");
    } else {
        const auto& result = app_->updateResult;
        const char* build = CurrentBuildCommit();
        const std::string commit = build ? build : "";
        const std::wstring buildLabel = commit.size() == 40
            ? L"Build " + std::wstring(commit.begin(), commit.begin() + 12) : L"Local build";
        SetText(hwnd_, UpdateVersion, std::wstring(AppVersion) + L"  \u2022  " + buildLabel);
        Check(hwnd_, AutoCheckUpdates, settings.autoCheckUpdates);
        EnableWindow(GetDlgItem(hwnd_, CheckUpdates), !UpdateBusy(result.status));
        EnableWindow(GetDlgItem(hwnd_, InstallUpdate), result.status == UpdateStatus::Available);
        SetText(hwnd_, CheckUpdates, result.status == UpdateStatus::Checking ? L"Checking..." : L"Check for updates");
        SetText(hwnd_, InstallUpdate, result.status == UpdateStatus::Downloading ? L"Downloading..." : L"Download and install");
        SetText(hwnd_, UpdateMessage, UpdateMessageText(result));
        SetText(hwnd_, UpdateNotesTitle, result.info.version.empty() ? L"Release notes" : L"Release notes  \u2022  " + result.info.version);
        const auto notes = UpdateNotesText(result);
        if (Text(hwnd_, UpdateNotes) != notes) SetText(hwnd_, UpdateNotes, notes);
    }
    SetText(hwnd_, Status, ui.status); ui.updating = false;
}
void SettingsWindow::Refresh() { UpdateControls(); }
bool SettingsWindow::Capturing() const { return hwnd_ && IsWindowVisible(hwnd_) && captureSlot_ != -1; }
void SettingsWindow::CancelCapture() {
    if (captureSlot_ == -1) return;
    captureSlot_ = -1; ui.status.clear(); if (app_) app_->Changed(); UpdateControls();
}
void SettingsWindow::CaptureInput(int input) {
    if (!Capturing() || input <= InputNone || input >= static_cast<int>(InputCount)) return;
    if (input == MouseLeft) {
        POINT cursor{}; GetCursorPos(&cursor); HWND target = WindowFromPoint(cursor);
        if (target != hwnd_ && IsChild(hwnd_, target)) return;
    }
    if (captureSlot_ == -2) {
        if (input >= 256 || input == VK_SHIFT || input == VK_CONTROL || input == VK_MENU ||
            input == VK_LSHIFT || input == VK_RSHIFT || input == VK_LCONTROL || input == VK_RCONTROL ||
            input == VK_LMENU || input == VK_RMENU || input == VK_LWIN || input == VK_RWIN) return;
        if (!app_->SetHotkey(SelectedModifiers(hwnd_), static_cast<UINT>(input))) {
            ui.status = L"That shortcut is unavailable. Choose another key or modifier combination.";
            SetText(hwnd_, Status, ui.status); return;
        }
        captureSlot_ = -1; ui.status = L"Shortcut saved.";
    } else {
        BindInput(app_->settings, static_cast<size_t>(captureSlot_), input);
        captureSlot_ = -1; ui.status = L"Input assigned. This input now lights up only the selected element.";
    }
    app_->ResetInputs(); app_->Changed(); UpdateControls();
}

void SettingsWindow::Command(int id, int code) {
    if (ui.updating || !app_) return;
    if (id >= NavOverlay && id <= NavUpdates) { SelectPage(id - NavOverlay); return; }
    Settings& settings = app_->settings;
    if (id >= StyleFirst && id <= StyleLast) {
        settings.style = static_cast<OverlayStyle>(id - StyleFirst);
        app_->Changed(); UpdateControls(); return;
    }
    if (id >= AccentFirst && id <= AccentLast) {
        settings.accent = Accents[static_cast<size_t>(id - AccentFirst)]; app_->Changed(); UpdateControls(); return;
    }
    switch (id) {
    case ExitApp: app_->Quit(); return;
    case Enabled: settings.enabled = Checked(hwnd_, Enabled); app_->Changed(); break;
    case MouseVisible: settings.showMouse = Checked(hwnd_, MouseVisible); app_->Changed(); break;
    case Layout:
        if (code != CBN_SELCHANGE) return;
        settings.isoLayout = SendDlgItemMessageW(hwnd_, Layout, CB_GETCURSEL, 0, 0) == 1; app_->Changed();
        break;
    case MoveOverlay:
        app_->EditPosition(!app_->overlay.Editing());
        ui.status = app_->overlay.Editing() ? L"Position mode is active. Drag the overlay to move it." : L"Overlay position saved.";
        UpdateControls(); break;
    case ResetPosition:
        settings.x = settings.y = 32; settings.anchorRight = false; settings.anchorBottom = true;
        settings.monitor.clear(); settings.scale = 100; ui.status = L"Position and size reset to the bottom-left corner of your primary display.";
        app_->Changed(); break;
    case SlotList:
        if (code != LBN_SELCHANGE) return;
        {
            const LRESULT selected = SendDlgItemMessageW(hwnd_, SlotList, LB_GETCURSEL, 0, 0);
            CancelCapture();
            if (selected >= 0 && selected < static_cast<LRESULT>(SlotCount)) ui.selectedSlot = static_cast<int>(selected);
            ui.status.clear(); UpdateControls();
        }
        break;
    case SlotVisible: settings.slots[static_cast<size_t>(ui.selectedSlot)].visible = Checked(hwnd_, SlotVisible); app_->Changed(); break;
    case SaveLabel:
        settings.slots[static_cast<size_t>(ui.selectedSlot)].label = Text(hwnd_, LabelEdit);
        ui.status = L"Display label saved. The assigned input is unchanged."; app_->Changed(); break;
    case RecordInput:
        if (captureSlot_ != -1) CancelCapture();
        else { captureSlot_ = ui.selectedSlot; ui.status = L"Press a key or mouse button. For a mouse click, click outside this settings window."; app_->Changed(); }
        UpdateControls(); break;
    case UnbindInput:
        CancelCapture(); BindInput(settings, static_cast<size_t>(ui.selectedSlot), InputNone);
        ui.status = L"Input removed. This element keeps its position and label."; app_->Changed(); break;
    case ResetBinding: {
        CancelCapture();
        const size_t selected = static_cast<size_t>(ui.selectedSlot);
        const Slot defaults = DefaultSettings().slots[selected];
        BindInput(settings, selected, defaults.input);
        settings.slots[selected].label = defaults.label;
        settings.slots[selected].visible = defaults.visible;
        ui.status = L"Selected element restored to its default input, label, and visibility.";
        app_->Changed(); break;
    }
    case RestrictApps:
        settings.onlySelectedApps = Checked(hwnd_, RestrictApps);
        ui.status = settings.onlySelectedApps && settings.applications.empty() ? L"Add an application below. The overlay stays hidden until a selected application is active." : L"";
        app_->Changed(); break;
    case RefreshWindows: windows_ = EnumerateApplications(); UpdateControls(); break;
    case WindowList:
        if (code == CBN_SELCHANGE) {
            const LRESULT selected = SendDlgItemMessageW(hwnd_, WindowList, CB_GETCURSEL, 0, 0);
            if (selected >= 0 && static_cast<size_t>(selected) < windows_.size()) SetText(hwnd_, WindowPath, windows_[static_cast<size_t>(selected)].path);
        }
        return;
    case AddWindow: {
        const LRESULT selected = SendDlgItemMessageW(hwnd_, WindowList, CB_GETCURSEL, 0, 0);
        if (selected < 0 || static_cast<size_t>(selected) >= windows_.size()) break;
        const auto& path = windows_[static_cast<size_t>(selected)].path;
        const bool duplicate = std::any_of(settings.applications.begin(), settings.applications.end(), [&](const std::wstring& item) { return _wcsicmp(item.c_str(), path.c_str()) == 0; });
        if (!duplicate) {
            settings.applications.push_back(path); ui.status = L"Application added. Any window from this executable can activate the overlay."; app_->Changed();
        } else { ui.status = L"This application is already in the list."; UpdateControls(); }
        break;
    }
    case RemoveApp: {
        const LRESULT selected = SendDlgItemMessageW(hwnd_, AllowedList, LB_GETCURSEL, 0, 0);
        if (selected >= 0 && static_cast<size_t>(selected) < settings.applications.size()) {
            settings.applications.erase(settings.applications.begin() + selected); ui.status = L"Application removed."; app_->Changed();
        }
        break;
    }
    case ModCtrl: case ModAlt: case ModShift: case ModWin:
        if (captureSlot_ != -2) {
            if (!app_->SetHotkey(SelectedModifiers(hwnd_), settings.hotkeyVk)) ui.status = L"That shortcut is unavailable. The previous shortcut is still active.";
            else ui.status = L"Shortcut saved.";
            app_->Changed(); UpdateControls();
        }
        break;
    case RecordHotkey:
        if (captureSlot_ != -1) CancelCapture();
        else { captureSlot_ = -2; ui.status = L"Press a keyboard key. The selected modifier checkboxes are used for the shortcut."; app_->Changed(); }
        UpdateControls(); break;
    case Startup:
        if (!SetStartup(Checked(hwnd_, Startup))) {
            ui.status = L"Windows could not update the startup setting. Your previous setting is unchanged.";
            MessageBoxW(hwnd_, L"Could not update the Windows startup setting. Check access to your user startup settings and try again.", AppName, MB_OK | MB_ICONERROR);
        } else ui.status = Checked(hwnd_, Startup) ? L"Startup enabled. Keep the executable in its current location." : L"Startup disabled.";
        app_->Changed(); UpdateControls(); break;
    case StartMinimized:
        if (code != BN_CLICKED) return;
        settings.startMinimized = Checked(hwnd_, StartMinimized);
        ui.status = settings.startMinimized ? L"Next launch will open in the system tray." : L"Next launch will open Settings.";
        app_->Changed(); break;
    case ControllerMode:
        if (code != BN_CLICKED) return;
        settings.device = Checked(hwnd_, ControllerMode) ? OverlayDevice::Controller : OverlayDevice::KeyboardMouse;
        ui.status = settings.device == OverlayDevice::Controller ? L"Controller view enabled." : L"Keyboard and mouse view enabled.";
        app_->Changed(); break;
    case ControllerLayoutChoice: {
        if (code != CBN_SELCHANGE) return;
        const LRESULT selected = SendDlgItemMessageW(hwnd_, ControllerLayoutChoice, CB_GETCURSEL, 0, 0);
        if (selected < 0 || selected > 1) return;
        settings.controllerLayout = static_cast<ControllerLayout>(selected);
        app_->Changed(); break;
    }
    case ControllerIndex: {
        if (code != CBN_SELCHANGE) return;
        const LRESULT selected = SendDlgItemMessageW(hwnd_, ControllerIndex, CB_GETCURSEL, 0, 0);
        if (selected < 0 || selected > 4) return;
        settings.controllerIndex = static_cast<int>(selected) - 1;
        app_->Changed(); break;
    }
    case AutoCheckUpdates:
        if (code != BN_CLICKED) return;
        settings.autoCheckUpdates = Checked(hwnd_, AutoCheckUpdates);
        ui.status = settings.autoCheckUpdates ? L"Startup update checks enabled." : L"Startup update checks disabled. You can still check manually.";
        app_->Save(); break;
    case CheckUpdates:
        if (code != BN_CLICKED || UpdateBusy(app_->updateResult.status)) return;
        ui.status.clear();
        app_->CheckForUpdates(true); break;
    case InstallUpdate:
        if (code != BN_CLICKED || app_->updateResult.status != UpdateStatus::Available) return;
        ui.status.clear();
        app_->InstallUpdate(); break;
    default: return;
    }
    UpdateControls();
}

LRESULT CALLBACK SettingsWindow::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam); self = static_cast<SettingsWindow*>(create->lpCreateParams);
        self->hwnd_ = hwnd; SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
    switch (message) {
    case WM_CREATE: {
        const BOOL dark = TRUE;
        if (FAILED(DwmSetWindowAttribute(hwnd, 20, &dark, sizeof(dark)))) DwmSetWindowAttribute(hwnd, 19, &dark, sizeof(dark));
        FitWindow(hwnd, WindowDpi(hwnd)); CreateFonts(); ui.background = CreateSolidBrush(Background); ui.surface = CreateSolidBrush(Surface); self->Build(); return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wParam) == IDCANCEL) {
            self->CancelCapture(); self->app_->EditPosition(false); ShowWindow(hwnd, SW_HIDE);
        } else self->Command(LOWORD(wParam), HIWORD(wParam));
        return 0;
    case WM_CLOSE:
        self->CancelCapture(); self->app_->EditPosition(false); ShowWindow(hwnd, SW_HIDE); return 0;
    case WM_DESTROY:
        self->captureSlot_ = -1; self->controls_.clear(); FreeFonts();
        if (ui.background) DeleteObject(ui.background); if (ui.surface) DeleteObject(ui.surface);
        ui.background = ui.surface = nullptr; return 0;
    case WM_NCDESTROY:
        self->hwnd_ = nullptr; SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0); return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_DPICHANGED: {
        if (FitWindow(hwnd, HIWORD(wParam), reinterpret_cast<const RECT*>(lParam))) { CreateFonts(); self->Build(); }
        return 0;
    }
    case WM_EXITSIZEMOVE: case WM_DISPLAYCHANGE:
        if (FitWindow(hwnd, WindowDpi(hwnd))) { CreateFonts(); self->Build(); }
        return 0;
    case WM_SETTINGCHANGE:
        if (wParam == SPI_SETWORKAREA && FitWindow(hwnd, WindowDpi(hwnd))) { CreateFonts(); self->Build(); }
        break;
    case WM_HSCROLL: {
        const int id = GetDlgCtrlID(reinterpret_cast<HWND>(lParam));
        if (id == Scale || id == Opacity || id == ControllerDeadzone) {
            const int value = static_cast<int>(SendMessageW(reinterpret_cast<HWND>(lParam), TBM_GETPOS, 0, 0));
            const int valueId = id == Scale ? ScaleValue : (id == Opacity ? OpacityValue : ControllerDeadzoneValue);
            SetText(hwnd, valueId, std::to_wstring(value) + L"%");
            if (LOWORD(wParam) != TB_THUMBTRACK) {
                int& setting = id == Scale ? self->app_->settings.scale :
                    (id == Opacity ? self->app_->settings.opacity : self->app_->settings.controllerDeadzone);
                if (setting != value) { setting = value; self->app_->Changed(); }
                self->UpdateControls();
            }
        }
        return 0;
    }
    case WM_NOTIFY: {
        const auto* custom = reinterpret_cast<NMCUSTOMDRAW*>(lParam);
        if (custom->hdr.code == NM_CUSTOMDRAW && (custom->hdr.idFrom == Scale || custom->hdr.idFrom == Opacity || custom->hdr.idFrom == ControllerDeadzone)) {
            if (custom->dwDrawStage == CDDS_PREPAINT) return CDRF_NOTIFYITEMDRAW;
            if (custom->dwDrawStage == CDDS_ITEMPREPAINT) {
                if (custom->dwItemSpec == TBCD_CHANNEL) { Fill(custom->hdc, custom->rc, Border); return CDRF_SKIPDEFAULT; }
                if (custom->dwItemSpec == TBCD_THUMB) {
                    RECT thumb = custom->rc; HBRUSH brush = CreateSolidBrush(Blue);
                    HGDIOBJ oldBrush = SelectObject(custom->hdc, brush); HGDIOBJ oldPen = SelectObject(custom->hdc, GetStockObject(NULL_PEN));
                    RoundRect(custom->hdc, thumb.left, thumb.top, thumb.right, thumb.bottom, Px(8), Px(8));
                    SelectObject(custom->hdc, oldPen); SelectObject(custom->hdc, oldBrush); DeleteObject(brush); return CDRF_SKIPDEFAULT;
                }
            }
        }
        break;
    }
    case WM_CTLCOLORSTATIC: case WM_CTLCOLORBTN: {
        HDC dc = reinterpret_cast<HDC>(wParam); HWND control = reinterpret_cast<HWND>(lParam);
        if (GetDlgCtrlID(control) == UpdateNotes) {
            SetTextColor(dc, Foreground); SetBkColor(dc, Surface); return reinterpret_cast<LRESULT>(ui.surface);
        }
        SetTextColor(dc, GetPropW(control, L"InputOverlay.Muted") ? Muted : Foreground); SetBkColor(dc, Background); SetBkMode(dc, TRANSPARENT);
        return reinterpret_cast<LRESULT>(ui.background);
    }
    case WM_CTLCOLOREDIT: case WM_CTLCOLORLISTBOX: {
        HDC dc = reinterpret_cast<HDC>(wParam); SetTextColor(dc, Foreground); SetBkColor(dc, Surface); return reinterpret_cast<LRESULT>(ui.surface);
    }
    case WM_ERASEBKGND: return 1;
    case WM_DRAWITEM: {
        auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lParam); if (item->CtlType != ODT_BUTTON) break;
        const bool nav = item->CtlID >= NavOverlay && item->CtlID <= NavUpdates;
        const bool styleCard = item->CtlID >= StyleFirst && item->CtlID <= StyleLast;
        const bool selected = (nav && static_cast<int>(item->CtlID - NavOverlay) == self->page_) ||
            (styleCard && static_cast<int>(item->CtlID - StyleFirst) == static_cast<int>(self->app_->settings.style));
        const COLORREF accent = self->app_->settings.accent;
        const bool primary = GetPropW(item->hwndItem, L"InputOverlay.Primary") != nullptr;
        const bool swatch = item->CtlID >= AccentFirst && item->CtlID <= AccentLast;
        COLORREF fill = selected ? RGB(30, 49, 64) : (nav ? Sidebar : Surface);
        COLORREF textColor = selected || primary ? Blue : Foreground;
        if (styleCard && selected) { fill = Tint(Background, accent, 12); textColor = accent; }
        if (item->itemState & ODS_SELECTED) fill = RGB(48, 65, 83);
        if (item->itemState & ODS_DISABLED) textColor = Muted;
        Fill(item->hDC, item->rcItem, nav || item->CtlID == ExitApp ? Sidebar : Background);
        const COLORREF edge = selected ? (styleCard ? accent : RGB(50, 93, 118)) : (nav ? Sidebar : Border);
        HBRUSH brush = CreateSolidBrush(fill); HPEN pen = CreatePen(PS_SOLID, 1, edge);
        HGDIOBJ oldBrush = SelectObject(item->hDC, brush); HGDIOBJ oldPen = SelectObject(item->hDC, pen);
        RoundRect(item->hDC, item->rcItem.left, item->rcItem.top, item->rcItem.right, item->rcItem.bottom, Px(10), Px(10));
        SelectObject(item->hDC, oldBrush); SelectObject(item->hDC, oldPen); DeleteObject(brush); DeleteObject(pen);
        RECT labelRect = item->rcItem; UINT align = DT_CENTER;
        if (nav) { labelRect.left += Px(15); align = DT_LEFT; }
        if (styleCard) {
            DrawStylePreview(item->hDC, item->rcItem, static_cast<OverlayStyle>(item->CtlID - StyleFirst), accent, fill);
            labelRect.left += Px(6); labelRect.right -= Px(6); labelRect.top += Px(42); labelRect.bottom -= Px(5);
        }
        if (swatch) {
            const COLORREF accent = Accents[item->CtlID - AccentFirst];
            RECT circle{item->rcItem.left + Px(12), item->rcItem.top + Px(12), item->rcItem.left + Px(25), item->rcItem.top + Px(25)};
            HBRUSH color = CreateSolidBrush(accent); oldBrush = SelectObject(item->hDC, color); oldPen = SelectObject(item->hDC, GetStockObject(NULL_PEN));
            Ellipse(item->hDC, circle.left, circle.top, circle.right, circle.bottom);
            SelectObject(item->hDC, oldBrush); SelectObject(item->hDC, oldPen); DeleteObject(color); labelRect.left += Px(24);
            if (self->app_->settings.accent == accent) {
                RECT underline{item->rcItem.left + Px(13), item->rcItem.bottom - Px(5), item->rcItem.right - Px(13), item->rcItem.bottom - Px(3)}; Fill(item->hDC, underline, accent);
            }
        }
        wchar_t label[128]{}; GetWindowTextW(item->hwndItem, label, 128);
        DrawTextAt(item->hDC, label, labelRect, ui.normal, textColor, align | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if ((item->itemState & ODS_FOCUS) && !(item->itemState & ODS_NOFOCUSRECT)) {
            RECT focus = item->rcItem; InflateRect(&focus, -Px(4), -Px(4)); DrawFocusRect(item->hDC, &focus);
        }
        return TRUE;
    }
    case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd, &paint); RECT client{}; GetClientRect(hwnd, &client); Fill(dc, client, Background);
        RECT sidebar{0, 0, Px(184), client.bottom}; Fill(dc, sidebar, Sidebar);
        RECT divider{Px(183), 0, Px(184), client.bottom}; Fill(dc, divider, Border);
        RECT logo{Px(22), Px(28), Px(41), Px(47)}; Fill(dc, logo, Blue);
        RECT cutout{Px(27), Px(33), Px(36), Px(42)}; Fill(dc, cutout, Sidebar);
        RECT name{Px(51), Px(23), Px(177), Px(69)}; DrawTextAt(dc, L"Input\nOverlay", name, ui.heading, Foreground, DT_LEFT);
        RECT section{Px(30), Px(95), Px(166), Px(116)}; DrawTextAt(dc, L"PREFERENCES", section, ui.smallFont, Muted, DT_LEFT | DT_SINGLELINE);
        RECT foot{Px(25), Px(630), Px(169), Px(654)}; DrawTextAt(dc, L"Portable  /  Windows", foot, ui.smallFont, Muted, DT_LEFT | DT_SINGLELINE);
        RECT title{Px(216), Px(25), Px(794), Px(66)}; DrawTextAt(dc, PageTitles[self->page_], title, ui.title, Foreground, DT_LEFT | DT_SINGLELINE);
        RECT subtitle{Px(216), Px(72), Px(797), Px(105)}; DrawTextAt(dc, PageSubtitles[self->page_], subtitle, ui.normal, Muted, DT_LEFT | DT_WORDBREAK);
        EndPaint(hwnd, &paint); return 0;
    }
    default: break;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
}
