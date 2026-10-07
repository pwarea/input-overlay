#include "app.hpp"
#include "geometry.hpp"
#include <dwmapi.h>
#include <algorithm>
#include <atomic>
#include <climits>
#include <sstream>

namespace input_overlay {
namespace {
constexpr size_t MaxFileBytes = 1024 * 1024;
constexpr size_t MaxApplications = 128;
constexpr wchar_t RunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

int CanonicalInput(int input) {
    switch (input) {
    case VK_LSHIFT: case VK_RSHIFT: return VK_SHIFT;
    case VK_LCONTROL: case VK_RCONTROL: return VK_CONTROL;
    case VK_LMENU: case VK_RMENU: return VK_MENU;
    case VK_LBUTTON: return MouseLeft;
    case VK_RBUTTON: return MouseRight;
    case VK_MBUTTON: return MouseMiddle;
    case VK_XBUTTON1: return MouseSide1;
    case VK_XBUTTON2: return MouseSide2;
    default: return input >= 0 && input < static_cast<int>(InputCount) ? input : InputNone;
    }
}

bool SameText(const std::wstring& a, const std::wstring& b) {
    return a.size() == b.size() && CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
        b.c_str(), static_cast<int>(b.size()), TRUE) == CSTR_EQUAL;
}

bool ValidUnicode(const std::wstring& value) {
    for (size_t i = 0; i < value.size(); ++i) {
        const unsigned c = value[i];
        if (c >= 0xd800 && c <= 0xdbff) {
            if (++i == value.size() || value[i] < 0xdc00 || value[i] > 0xdfff) return false;
        } else if (c >= 0xdc00 && c <= 0xdfff) return false;
    }
    return true;
}

std::wstring CleanPath(std::wstring path) {
    if (path.empty() || path.size() > 32767 || path.find(L'\0') != std::wstring::npos || !ValidUnicode(path)) return {};
    std::replace(path.begin(), path.end(), L'/', L'\\');
    if (path.compare(0, 8, L"\\\\?\\UNC\\") == 0) path = L"\\\\" + path.substr(8);
    else if (path.compare(0, 4, L"\\\\?\\") == 0) path.erase(0, 4);
    const bool drive = path.size() >= 3 && ((path[0] >= L'A' && path[0] <= L'Z') ||
        (path[0] >= L'a' && path[0] <= L'z')) && path[1] == L':' && path[2] == L'\\';
    const bool unc = path.size() > 4 && path[0] == L'\\' && path[1] == L'\\';
    if (!drive && !unc) return {};
    for (wchar_t c : path) if (c < 32 || c == L'"' || c == L'*' || c == L'?') return {};
    return path;
}

std::wstring Trim(const std::wstring& text) {
    const auto first = text.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    return text.substr(first, text.find_last_not_of(L" \t\r\n") - first + 1);
}

bool ParseInt(const std::wstring& value, int& result) {
    const std::wstring text = Trim(value);
    if (text.empty() || text.size() > 11) return false;
    size_t i = 0;
    bool negative = false;
    if (text[0] == L'-' || text[0] == L'+') { negative = text[0] == L'-'; ++i; }
    if (i == text.size()) return false;
    unsigned long long number = 0;
    for (; i < text.size(); ++i) {
        if (text[i] < L'0' || text[i] > L'9') return false;
        number = number * 10 + (text[i] - L'0');
        if (number > static_cast<unsigned long long>(INT_MAX) + (negative ? 1ULL : 0ULL)) return false;
    }
    result = negative ? static_cast<int>(-static_cast<long long>(number)) : static_cast<int>(number);
    return true;
}

std::wstring Escape(const std::wstring& value) {
    std::wstring output;
    for (wchar_t c : value) {
        if (c == L'\\') output += L"\\\\";
        else if (c == L'\r') output += L"\\r";
        else if (c == L'\n') output += L"\\n";
        else if (c == L'\t') output += L"\\t";
        else output += c;
    }
    return output;
}

bool Unescape(const std::wstring& value, std::wstring& output) {
    output.clear();
    for (size_t i = 0; i < value.size(); ++i) {
        wchar_t c = value[i];
        if (c == L'\\') {
            if (++i == value.size()) return false;
            c = value[i];
            if (c == L'n') c = L'\n';
            else if (c == L'r') c = L'\r';
            else if (c == L't') c = L'\t';
            else if (c != L'\\') return false;
        }
        if (c == 0) return false;
        output += c;
    }
    return ValidUnicode(output);
}

bool ReadText(const std::wstring& file, std::wstring& result) {
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size) || size.QuadPart < 0 || size.QuadPart > static_cast<LONGLONG>(MaxFileBytes)) {
        CloseHandle(handle); return false;
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const bool ok = ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
    CloseHandle(handle);
    if (!ok) return false;
    result.clear();
    if (bytes.empty()) return true;
    const auto b = [&](size_t index) { return static_cast<unsigned char>(bytes[index]); };
    if (bytes.size() >= 2 && b(0) == 0xff && b(1) == 0xfe) {
        if (bytes.size() % 2) return false;
        for (size_t i = 2; i < bytes.size(); i += 2) result += static_cast<wchar_t>(b(i) | (b(i + 1) << 8));
        return ValidUnicode(result) && result.find(L'\0') == std::wstring::npos;
    }
    const int offset = bytes.size() >= 3 && b(0) == 0xef && b(1) == 0xbb && b(2) == 0xbf ? 3 : 0;
    if (bytes.size() == static_cast<size_t>(offset)) return true;
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data() + offset,
        static_cast<int>(bytes.size()) - offset, nullptr, 0);
    if (length <= 0) return false;
    result.resize(length);
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data() + offset,
        static_cast<int>(bytes.size()) - offset, result.data(), length)) return false;
    return result.find(L'\0') == std::wstring::npos;
}

bool IsHotkey(UINT vk) {
    return vk > VK_XBUTTON2 && vk <= 0xfe && vk != VK_SHIFT && vk != VK_CONTROL && vk != VK_MENU &&
        vk != VK_LSHIFT && vk != VK_RSHIFT && vk != VK_LCONTROL && vk != VK_RCONTROL &&
        vk != VK_LMENU && vk != VK_RMENU && vk != VK_LWIN && vk != VK_RWIN;
}

std::wstring StartupCommand() {
    const auto path = ExecutablePath();
    return path.empty() ? std::wstring{} : L"\"" + path + L"\" --background";
}
}

Settings DefaultSettings() {
    Settings settings;
    const wchar_t* labels[SlotCount] = {L"ESC", L"1", L"2", L"3", L"4", L"5", L"TAB", L"Q", L"W", L"E", L"R", L"T",
        L"CAPS", L"A", L"S", L"D", L"F", L"G", L"SHIFT", L"\\", L"Z", L"X", L"C", L"V", L"CTRL", L"WIN", L"ALT", L"SPACE",
        L"Left", L"Right", L"Middle", L"Side 1", L"Side 2", L"Wheel up", L"Wheel down"};
    const int inputs[SlotCount] = {VK_ESCAPE, '1', '2', '3', '4', '5', VK_TAB, 'Q', 'W', 'E', 'R', 'T',
        VK_CAPITAL, 'A', 'S', 'D', 'F', 'G', VK_SHIFT, VK_OEM_102, 'Z', 'X', 'C', 'V', VK_CONTROL, VK_LWIN, VK_MENU, VK_SPACE,
        MouseLeft, MouseRight, MouseMiddle, MouseSide1, MouseSide2, WheelUp, WheelDown};
    for (size_t i = 0; i < SlotCount; ++i) settings.slots[i] = {labels[i], inputs[i], true};
    return settings;
}

void NormalizeSettings(Settings& settings) {
    settings.x = std::clamp(settings.x, 0, 32767);
    settings.y = std::clamp(settings.y, 0, 32767);
    settings.scale = std::clamp(settings.scale, 10, 200);
    settings.controllerScale = std::clamp(settings.controllerScale, 10, 200);
    settings.opacity = std::clamp(settings.opacity, 15, 100);
    settings.originalOpacity = std::clamp(settings.originalOpacity, 15, 100);
    settings.gradientFillOpacity = std::clamp(settings.gradientFillOpacity, 4, 45);
    settings.accent &= 0x00ffffff;
    if (settings.controllerAccent != CLR_INVALID && settings.controllerAccent > 0x00ffffff)
        settings.controllerAccent = CLR_INVALID;
    settings.backgroundStart &= 0x00ffffff;
    settings.backgroundEnd &= 0x00ffffff;
    if (static_cast<int>(settings.colorTheme) < 0 || static_cast<int>(settings.colorTheme) >= ColorThemeCount)
        settings.colorTheme = ColorTheme::Original;
    if (static_cast<int>(settings.style) < 0 || static_cast<int>(settings.style) >= OverlayStyleCount)
        settings.style = OverlayStyle::Outline;
    if (settings.device != OverlayDevice::KeyboardMouse && settings.device != OverlayDevice::Controller)
        settings.device = OverlayDevice::KeyboardMouse;
    if (static_cast<int>(settings.controllerLayout) < 0 || static_cast<int>(settings.controllerLayout) >= ControllerLayoutCount)
        settings.controllerLayout = ControllerLayout::Xbox;
    if (static_cast<int>(settings.controllerStyle) < 0 || static_cast<int>(settings.controllerStyle) >= ControllerStyleCount)
        settings.controllerStyle = ControllerStyle::Frost;
    if (settings.controllerIndex < -1 || settings.controllerIndex >= ControllerSlotCount) settings.controllerIndex = -1;
    settings.controllerDeadzone = std::clamp(settings.controllerDeadzone, 0, 40);
    if (settings.monitor.size() >= CCHDEVICENAME || !ValidUnicode(settings.monitor) ||
        std::any_of(settings.monitor.begin(), settings.monitor.end(), [](wchar_t c) { return c < 32; })) settings.monitor.clear();
    settings.hotkeyModifiers &= MOD_ALT | MOD_CONTROL | MOD_SHIFT | MOD_WIN;
    if (!IsHotkey(settings.hotkeyVk)) {
        settings.hotkeyVk = VK_F10;
        settings.hotkeyModifiers = MOD_CONTROL | MOD_ALT;
    }
    std::array<bool, InputCount> used{};
    const auto defaults = DefaultSettings();
    for (size_t i = 0; i < SlotCount; ++i) {
        auto& slot = settings.slots[i];
        slot.input = CanonicalInput(slot.input);
        if (slot.input && used[slot.input]) slot.input = InputNone;
        if (slot.input) used[slot.input] = true;
        if (!ValidUnicode(slot.label)) slot.label = defaults.slots[i].label;
        slot.label.erase(std::remove_if(slot.label.begin(), slot.label.end(), [](wchar_t c) { return c < 32 || c == 127; }), slot.label.end());
        if (slot.label.size() > 24) {
            slot.label.resize(24);
            if (slot.label.back() >= 0xd800 && slot.label.back() <= 0xdbff) slot.label.pop_back();
        }
    }
    std::vector<std::wstring> paths;
    for (const auto& path : settings.applications) {
        auto clean = CleanPath(path);
        if (clean.empty() || std::any_of(paths.begin(), paths.end(), [&](const auto& other) { return SameText(other, clean); })) continue;
        paths.push_back(std::move(clean));
        if (paths.size() == MaxApplications) break;
    }
    settings.applications = std::move(paths);
}

void BindInput(Settings& settings, size_t slot, int input) {
    if (slot >= SlotCount) return;
    input = CanonicalInput(input);
    if (input) for (auto& item : settings.slots) if (CanonicalInput(item.input) == input) item.input = InputNone;
    settings.slots[slot].input = input;
}

bool LoadSettings(const std::wstring& file, Settings& settings) {
    settings = DefaultSettings();
    std::wstring text;
    if (!ReadText(file, text)) return false;
    settings.style = OverlayStyle::Outline;
    std::wistringstream stream(text);
    std::wstring line, section;
    bool hasControllerAccent = false;
    bool hasControllerScale = false;
    int positionVersion = 1;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (line.size() > 65536) continue;
        const auto trimmed = Trim(line);
        if (trimmed.empty() || trimmed[0] == L';' || trimmed[0] == L'#') continue;
        if (trimmed.front() == L'[' && trimmed.back() == L']') { section = trimmed.substr(1, trimmed.size() - 2); continue; }
        const auto equal = line.find(L'=');
        if (equal == std::wstring::npos) continue;
        const auto key = Trim(line.substr(0, equal));
        const auto value = line.substr(equal + 1);
        int number = 0;
        const bool numeric = ParseInt(value, number);
        if (section == L"General") {
            if (key == L"originalOpacity") {
                settings.originalOpacity = numeric ? std::clamp(number, 15, 100) : 100;
                continue;
            }
            if (key == L"gradientFillOpacity") {
                settings.gradientFillOpacity = numeric ? std::clamp(number, 4, 45) : 16;
                continue;
            }
            if (key == L"colorTheme") {
                settings.colorTheme = numeric && number >= 0 && number < ColorThemeCount
                    ? static_cast<ColorTheme>(number) : ColorTheme::Original;
                continue;
            }
            if (key == L"style") {
                settings.style = numeric && number >= 0 && number < OverlayStyleCount
                    ? static_cast<OverlayStyle>(number) : OverlayStyle::Outline;
                continue;
            }
            if (key == L"device") {
                settings.device = numeric && number == 1 ? OverlayDevice::Controller : OverlayDevice::KeyboardMouse;
                continue;
            }
            if (key == L"controllerLayout") {
                settings.controllerLayout = numeric && number >= 0 && number < ControllerLayoutCount
                    ? static_cast<ControllerLayout>(number) : ControllerLayout::Xbox;
                continue;
            }
            if (key == L"controllerStyle") {
                settings.controllerStyle = numeric && number >= 0 && number < ControllerStyleCount
                    ? static_cast<ControllerStyle>(number) : ControllerStyle::Frost;
                continue;
            }
            if (key == L"controllerAccent") {
                hasControllerAccent = true;
                settings.controllerAccent = numeric && number >= 0 && number <= 0xffffff
                    ? static_cast<COLORREF>(number) : CLR_INVALID;
                continue;
            }
            if (key == L"controllerIndex") {
                settings.controllerIndex = numeric && number >= -1 && number < ControllerSlotCount ? number : -1;
                continue;
            }
            if (key == L"controllerDeadzone") {
                settings.controllerDeadzone = numeric ? std::clamp(number, 0, 40) : 15;
                continue;
            }
            if (key == L"controllerScale") {
                hasControllerScale = true;
                settings.controllerScale = numeric ? std::clamp(number, 10, 200) : 100;
                continue;
            }
            if (key == L"monitor") {
                std::wstring monitor;
                if (Unescape(value, monitor)) settings.monitor = std::move(monitor);
                continue;
            }
            if (!numeric) continue;
            if (key == L"positionVersion") positionVersion = number;
            else if (key == L"x") settings.x = number;
            else if (key == L"y") settings.y = number;
            else if (key == L"scale") settings.scale = number;
            else if (key == L"opacity") settings.opacity = number;
            else if (key == L"accent" && number >= 0 && number <= 0xffffff) settings.accent = static_cast<COLORREF>(number);
            else if (key == L"backgroundStart" && number >= 0 && number <= 0xffffff) settings.backgroundStart = static_cast<COLORREF>(number);
            else if (key == L"backgroundEnd" && number >= 0 && number <= 0xffffff) settings.backgroundEnd = static_cast<COLORREF>(number);
            else if (key == L"enabled" && (number == 0 || number == 1)) settings.enabled = number != 0;
            else if (key == L"startMinimized" && (number == 0 || number == 1)) settings.startMinimized = number != 0;
            else if (key == L"autoCheckUpdates" && (number == 0 || number == 1)) settings.autoCheckUpdates = number != 0;
            else if (key == L"onlySelectedApps" && (number == 0 || number == 1)) settings.onlySelectedApps = number != 0;
            else if (key == L"showMouse" && (number == 0 || number == 1)) settings.showMouse = number != 0;
            else if (key == L"anchorRight" && (number == 0 || number == 1)) settings.anchorRight = number != 0;
            else if (key == L"anchorBottom" && (number == 0 || number == 1)) settings.anchorBottom = number != 0;
            else if (key == L"isoLayout" && (number == 0 || number == 1)) settings.isoLayout = number != 0;
            else if (key == L"hotkeyModifiers" && number >= 0 && number <= 15) settings.hotkeyModifiers = static_cast<UINT>(number);
            else if (key == L"hotkeyVk" && number >= 0) settings.hotkeyVk = static_cast<UINT>(number);
        } else if (section.compare(0, 4, L"Slot") == 0) {
            int index = -1;
            if (!ParseInt(section.substr(4), index) || index < 0 || index >= static_cast<int>(SlotCount)) continue;
            auto& slot = settings.slots[static_cast<size_t>(index)];
            if (key == L"input" && numeric && number >= 0 && number < static_cast<int>(InputCount)) slot.input = number;
            else if (key == L"visible" && numeric && (number == 0 || number == 1)) slot.visible = number != 0;
            else if (key == L"label") { std::wstring label; if (Unescape(value, label)) slot.label = std::move(label); }
        } else if (section == L"Applications" && key.compare(0, 4, L"path") == 0 && settings.applications.size() < MaxApplications) {
            int index = -1;
            std::wstring path;
            if (ParseInt(key.substr(4), index) && index >= 0 && index < static_cast<int>(MaxApplications) && Unescape(value, path))
                settings.applications.push_back(std::move(path));
        }
    }
    if (!hasControllerAccent && settings.accent != DefaultSettings().accent)
        settings.controllerAccent = settings.accent;
    if (!hasControllerScale) settings.controllerScale = settings.scale;
    NormalizeSettings(settings);
    if (positionVersion < 2 && settings.device == OverlayDevice::Controller) {
        const SIZE legacySize = OverlaySize(settings.controllerScale);
        const RECT viewport = OverlayViewport(settings);
        settings.x = std::min(32767, settings.x + static_cast<int>(settings.anchorRight ? legacySize.cx - viewport.right : viewport.left));
        settings.y = std::min(32767, settings.y + static_cast<int>(settings.anchorBottom ? legacySize.cy - viewport.bottom : viewport.top));
    }
    return true;
}

bool SaveSettings(const std::wstring& file, const Settings& settings) {
    Settings clean = settings;
    NormalizeSettings(clean);
    std::wostringstream output;
    output << L"[General]\r\nversion=1\r\npositionVersion=2\r\nx=" << clean.x << L"\r\ny=" << clean.y << L"\r\nscale=" << clean.scale
        << L"\r\nopacity=" << clean.opacity << L"\r\naccent=" << clean.accent << L"\r\nenabled=" << clean.enabled
        << L"\r\nstyle=" << static_cast<int>(clean.style)
        << L"\r\ncolorTheme=" << static_cast<int>(clean.colorTheme)
        << L"\r\ngradientFillOpacity=" << clean.gradientFillOpacity
        << L"\r\nbackgroundStart=" << clean.backgroundStart << L"\r\nbackgroundEnd=" << clean.backgroundEnd
        << L"\r\ndevice=" << static_cast<int>(clean.device)
        << L"\r\ncontrollerLayout=" << static_cast<int>(clean.controllerLayout)
        << L"\r\ncontrollerStyle=" << static_cast<int>(clean.controllerStyle)
        << L"\r\noriginalOpacity=" << clean.originalOpacity
        << L"\r\ncontrollerScale=" << clean.controllerScale
        << L"\r\ncontrollerAccent=" << (clean.controllerAccent == CLR_INVALID ? -1 : static_cast<int>(clean.controllerAccent))
        << L"\r\ncontrollerIndex=" << clean.controllerIndex << L"\r\ncontrollerDeadzone=" << clean.controllerDeadzone
        << L"\r\nstartMinimized=" << clean.startMinimized
        << L"\r\nautoCheckUpdates=" << clean.autoCheckUpdates
        << L"\r\nonlySelectedApps=" << clean.onlySelectedApps << L"\r\nshowMouse=" << clean.showMouse
        << L"\r\nanchorRight=" << clean.anchorRight << L"\r\nanchorBottom=" << clean.anchorBottom << L"\r\nisoLayout=" << clean.isoLayout
        << L"\r\nmonitor=" << Escape(clean.monitor)
        << L"\r\nhotkeyModifiers=" << clean.hotkeyModifiers << L"\r\nhotkeyVk=" << clean.hotkeyVk << L"\r\n";
    for (size_t i = 0; i < SlotCount; ++i) output << L"\r\n[Slot" << i << L"]\r\nlabel=" << Escape(clean.slots[i].label)
        << L"\r\ninput=" << clean.slots[i].input << L"\r\nvisible=" << clean.slots[i].visible << L"\r\n";
    output << L"\r\n[Applications]\r\n";
    for (size_t i = 0; i < clean.applications.size(); ++i) output << L"path" << i << L"=" << Escape(clean.applications[i]) << L"\r\n";
    const auto wide = output.str();
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0 || length + 3 > static_cast<int>(MaxFileBytes)) return false;
    std::string bytes = "\xef\xbb\xbf";
    bytes.resize(static_cast<size_t>(length) + 3);
    if (!WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), static_cast<int>(wide.size()), bytes.data() + 3, length, nullptr, nullptr)) return false;
    static std::atomic<unsigned> sequence{0};
    std::wstring temporary;
    HANDLE handle = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 16; ++attempt) {
        temporary = file + L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(++sequence);
        handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (handle != INVALID_HANDLE_VALUE) break;
        if (GetLastError() != ERROR_FILE_EXISTS && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    }
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
        written == bytes.size() && FlushFileBuffers(handle);
    const bool closed = CloseHandle(handle) != FALSE;
    if (!ok || !closed || !MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        DeleteFileW(temporary.c_str()); return false;
    }
    return true;
}

std::wstring InputName(int input) {
    input = CanonicalInput(input);
    if (input >= 'A' && input <= 'Z') return std::wstring(1, static_cast<wchar_t>(input));
    if (input >= '0' && input <= '9') return std::wstring(1, static_cast<wchar_t>(input));
    if (input >= VK_F1 && input <= VK_F24) return L"F" + std::to_wstring(input - VK_F1 + 1);
    if (input >= VK_NUMPAD0 && input <= VK_NUMPAD9) return L"Numpad " + std::to_wstring(input - VK_NUMPAD0);
    switch (input) {
    case InputNone: return L"Unbound";
    case MouseLeft: return L"Mouse left";
    case MouseRight: return L"Mouse right";
    case MouseMiddle: return L"Mouse middle";
    case MouseSide1: return L"Mouse side 1";
    case MouseSide2: return L"Mouse side 2";
    case WheelUp: return L"Wheel up";
    case WheelDown: return L"Wheel down";
    case VK_BACK: return L"Backspace";
    case VK_TAB: return L"Tab";
    case VK_CLEAR: return L"Clear";
    case VK_RETURN: return L"Enter";
    case VK_SHIFT: return L"Shift";
    case VK_CONTROL: return L"Ctrl";
    case VK_MENU: return L"Alt";
    case VK_PAUSE: return L"Pause";
    case VK_CAPITAL: return L"Caps Lock";
    case VK_ESCAPE: return L"Escape";
    case VK_SPACE: return L"Space";
    case VK_PRIOR: return L"Page Up";
    case VK_NEXT: return L"Page Down";
    case VK_END: return L"End";
    case VK_HOME: return L"Home";
    case VK_LEFT: return L"Left arrow";
    case VK_UP: return L"Up arrow";
    case VK_RIGHT: return L"Right arrow";
    case VK_DOWN: return L"Down arrow";
    case VK_SNAPSHOT: return L"Print Screen";
    case VK_INSERT: return L"Insert";
    case VK_DELETE: return L"Delete";
    case VK_HELP: return L"Help";
    case VK_LWIN: return L"Left Windows";
    case VK_RWIN: return L"Right Windows";
    case VK_APPS: return L"Menu";
    case VK_SLEEP: return L"Sleep";
    case VK_MULTIPLY: return L"Numpad *";
    case VK_ADD: return L"Numpad +";
    case VK_SEPARATOR: return L"Numpad separator";
    case VK_SUBTRACT: return L"Numpad -";
    case VK_DECIMAL: return L"Numpad decimal";
    case VK_DIVIDE: return L"Numpad /";
    case VK_NUMLOCK: return L"Num Lock";
    case VK_SCROLL: return L"Scroll Lock";
    case VK_BROWSER_BACK: return L"Browser back";
    case VK_BROWSER_FORWARD: return L"Browser forward";
    case VK_BROWSER_REFRESH: return L"Browser refresh";
    case VK_BROWSER_STOP: return L"Browser stop";
    case VK_BROWSER_SEARCH: return L"Browser search";
    case VK_BROWSER_FAVORITES: return L"Browser favorites";
    case VK_BROWSER_HOME: return L"Browser home";
    case VK_VOLUME_MUTE: return L"Mute";
    case VK_VOLUME_DOWN: return L"Volume down";
    case VK_VOLUME_UP: return L"Volume up";
    case VK_MEDIA_NEXT_TRACK: return L"Next track";
    case VK_MEDIA_PREV_TRACK: return L"Previous track";
    case VK_MEDIA_STOP: return L"Media stop";
    case VK_MEDIA_PLAY_PAUSE: return L"Play / pause";
    case VK_LAUNCH_MAIL: return L"Mail";
    case VK_LAUNCH_MEDIA_SELECT: return L"Media player";
    case VK_LAUNCH_APP1: return L"Launch app 1";
    case VK_LAUNCH_APP2: return L"Launch app 2";
    case VK_OEM_1: return L"Semicolon";
    case VK_OEM_PLUS: return L"Equals / plus";
    case VK_OEM_COMMA: return L"Comma";
    case VK_OEM_MINUS: return L"Minus";
    case VK_OEM_PERIOD: return L"Period";
    case VK_OEM_2: return L"Slash";
    case VK_OEM_3: return L"Grave accent";
    case VK_OEM_4: return L"Left bracket";
    case VK_OEM_5: return L"Backslash";
    case VK_OEM_6: return L"Right bracket";
    case VK_OEM_7: return L"Quote";
    case VK_OEM_8: return L"OEM 8";
    case VK_OEM_102: return L"Extra backslash";
    default: return L"Key " + std::to_wstring(input);
    }
}

std::wstring HotkeyName(UINT modifiers, UINT vk) {
    std::wstring name;
    if (modifiers & MOD_CONTROL) name += L"Ctrl + ";
    if (modifiers & MOD_ALT) name += L"Alt + ";
    if (modifiers & MOD_SHIFT) name += L"Shift + ";
    if (modifiers & MOD_WIN) name += L"Win + ";
    return name + InputName(static_cast<int>(vk));
}

bool MatchesApplication(const Settings& settings, const std::wstring& path) {
    if (!settings.onlySelectedApps) return true;
    const auto clean = CleanPath(path);
    if (clean.empty()) return false;
    for (const auto& allowed : settings.applications) if (SameText(CleanPath(allowed), clean)) return true;
    return false;
}

std::wstring ExecutablePath() {
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {};
    path.resize(length);
    return path;
}

std::wstring ProcessPath(HWND window) {
    if (!window) return {};
    DWORD pid = 0;
    if (!GetWindowThreadProcessId(window, &pid) || !pid) return {};
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) return {};
    std::wstring path(32768, L'\0');
    DWORD size = static_cast<DWORD>(path.size());
    const bool ok = QueryFullProcessImageNameW(process, 0, path.data(), &size) != FALSE;
    CloseHandle(process);
    if (!ok) return {};
    path.resize(size);
    return path;
}

std::vector<WindowInfo> EnumerateApplications() {
    std::vector<WindowInfo> windows;
    EnumWindows([](HWND hwnd, LPARAM argument) -> BOOL {
        if (!IsWindowVisible(hwnd) || GetWindow(hwnd, GW_OWNER)) return TRUE;
        DWORD pid = 0;
        GetWindowThreadProcessId(hwnd, &pid);
        if (!pid || pid == GetCurrentProcessId()) return TRUE;
        DWORD cloaked = 0;
        if (SUCCEEDED(DwmGetWindowAttribute(hwnd, DWMWA_CLOAKED, &cloaked, sizeof(cloaked))) && cloaked) return TRUE;
        const int length = GetWindowTextLengthW(hwnd);
        if (length <= 0 || length > 32767) return TRUE;
        std::wstring title(static_cast<size_t>(length) + 1, L'\0');
        const int copied = GetWindowTextW(hwnd, title.data(), static_cast<int>(title.size()));
        if (copied <= 0) return TRUE;
        title.resize(copied);
        auto path = ProcessPath(hwnd);
        if (path.empty()) return TRUE;
        auto& entries = *reinterpret_cast<std::vector<WindowInfo>*>(argument);
        if (std::any_of(entries.begin(), entries.end(), [&](const auto& entry) { return SameText(entry.path, path); })) return TRUE;
        entries.push_back({hwnd, std::move(title), std::move(path)});
        return TRUE;
    }, reinterpret_cast<LPARAM>(&windows));
    std::sort(windows.begin(), windows.end(), [](const auto& a, const auto& b) {
        const int order = CompareStringOrdinal(a.title.c_str(), static_cast<int>(a.title.size()), b.title.c_str(), static_cast<int>(b.title.size()), TRUE);
        return order == CSTR_EQUAL ? a.path < b.path : order == CSTR_LESS_THAN;
    });
    return windows;
}

bool StartupEnabled() {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, RunKey, 0, KEY_QUERY_VALUE, &key) != ERROR_SUCCESS) return false;
    DWORD type = 0, bytes = 0;
    LSTATUS status = RegQueryValueExW(key, AppName, nullptr, &type, nullptr, &bytes);
    if (status != ERROR_SUCCESS || type != REG_SZ || bytes < sizeof(wchar_t) || bytes > 131072 || bytes % sizeof(wchar_t)) {
        RegCloseKey(key); return false;
    }
    std::wstring value(bytes / sizeof(wchar_t), L'\0');
    status = RegQueryValueExW(key, AppName, nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes);
    RegCloseKey(key);
    if (status != ERROR_SUCCESS || type != REG_SZ) return false;
    value.resize(bytes / sizeof(wchar_t));
    if (!value.empty() && value.back() == L'\0') value.pop_back();
    const auto expected = StartupCommand();
    return !expected.empty() && SameText(value, expected);
}

bool SetStartup(bool enabled) {
    HKEY key = nullptr;
    if (enabled) {
        const auto command = StartupCommand();
        if (command.empty()) return false;
        if (RegCreateKeyExW(HKEY_CURRENT_USER, RunKey, 0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr) != ERROR_SUCCESS) return false;
        const auto status = RegSetValueExW(key, AppName, 0, REG_SZ, reinterpret_cast<const BYTE*>(command.c_str()),
            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
        return status == ERROR_SUCCESS;
    }
    const auto opened = RegOpenKeyExW(HKEY_CURRENT_USER, RunKey, 0, KEY_SET_VALUE, &key);
    if (opened == ERROR_FILE_NOT_FOUND) return true;
    if (opened != ERROR_SUCCESS) return false;
    const auto status = RegDeleteValueW(key, AppName);
    RegCloseKey(key);
    return status == ERROR_SUCCESS || status == ERROR_FILE_NOT_FOUND;
}
}
