#include "app.hpp"
#include "colors.hpp"
#include "geometry.hpp"
#include <algorithm>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>

namespace {
constexpr size_t QSlot = 7, ShiftSlot = 18, ControlSlot = 24, AltSlot = 26, SideSlot = input_overlay::KeyboardCount + 3;
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct TemporaryDirectory {
    std::wstring path;
    TemporaryDirectory() {
        wchar_t root[MAX_PATH + 1]{};
        Check(GetTempPathW(MAX_PATH, root) != 0, "GetTempPath failed");
        for (int attempt = 0; attempt < 100; ++attempt) {
            path = std::wstring(root) + L"InputOverlay-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
                std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(attempt);
            if (CreateDirectoryW(path.c_str(), nullptr)) return;
            Check(GetLastError() == ERROR_ALREADY_EXISTS, "Create test directory failed");
        }
        throw std::runtime_error("Unable to allocate test directory");
    }
    ~TemporaryDirectory() {
        WIN32_FIND_DATAW entry{};
        HANDLE search = FindFirstFileW((path + L"\\*").c_str(), &entry);
        if (search != INVALID_HANDLE_VALUE) {
            do {
                if (!(entry.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) DeleteFileW((path + L"\\" + entry.cFileName).c_str());
            } while (FindNextFileW(search, &entry));
            FindClose(search);
        }
        RemoveDirectoryW(path.c_str());
    }
    std::wstring File(const wchar_t* name) const { return path + L"\\" + name; }
};

void WriteBytes(const std::wstring& file, const std::string& text) {
    HANDLE handle = CreateFileW(file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(handle != INVALID_HANDLE_VALUE, "Create fixture failed");
    DWORD written = 0;
    const bool ok = WriteFile(handle, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) && written == text.size();
    CloseHandle(handle);
    Check(ok, "Write fixture failed");
}

void TestRemapping() {
    auto settings = input_overlay::DefaultSettings();
    const auto original = settings;
    input_overlay::BindInput(settings, SideSlot, 'Q');
    Check(settings.slots[SideSlot].input == 'Q', "Q must trigger mouse side 1");
    Check(settings.slots[QSlot].input == input_overlay::InputNone, "Q visual must no longer react to Q");
    for (size_t i = 0; i < input_overlay::SlotCount; ++i) {
        Check(settings.slots[i].label == original.slots[i].label, "Mapping must preserve every label");
        Check(settings.slots[i].visible == original.slots[i].visible, "Mapping must preserve visibility");
    }
    input_overlay::BindInput(settings, SideSlot, input_overlay::InputNone);
    Check(settings.slots[SideSlot].input == input_overlay::InputNone, "Unbound must disable input");
    input_overlay::BindInput(settings, 0, VK_RSHIFT);
    Check(settings.slots[0].input == VK_SHIFT && settings.slots[ShiftSlot].input == 0, "Right Shift must share generic Shift binding");
    input_overlay::BindInput(settings, 2, VK_LCONTROL);
    Check(settings.slots[2].input == VK_CONTROL && settings.slots[ControlSlot].input == 0, "Control aliases must be unique");
    input_overlay::BindInput(settings, 3, VK_RMENU);
    Check(settings.slots[3].input == VK_MENU && settings.slots[AltSlot].input == 0, "Alt aliases must be unique");
    input_overlay::BindInput(settings, 4, VK_XBUTTON1);
    Check(settings.slots[4].input == input_overlay::MouseSide1, "Mouse VK alias must use mouse input domain");
    input_overlay::BindInput(settings, input_overlay::SlotCount, 'Z');
    input_overlay::BindInput(settings, 5, INT_MAX);
    Check(settings.slots[5].input == 0, "Out of range input must not index input state");
}

void TestNormalization() {
    auto settings = input_overlay::DefaultSettings();
    settings.x = INT_MIN; settings.y = INT_MAX; settings.scale = -1; settings.controllerScale = INT_MAX; settings.opacity = 1000;
    settings.hotkeyVk = VK_RSHIFT; settings.hotkeyModifiers = 0xffffffff;
    settings.slots[1].input = VK_LSHIFT;
    settings.slots[2].input = VK_RSHIFT;
    settings.slots[3].input = -8;
    settings.slots[4].label = L"Some\r\nlabel\t";
    settings.slots[5].label = std::wstring(100, L'W');
    settings.applications = {L"C:\\Games\\game.exe", L"c:/games/GAME.exe", L"game.exe", L"D:\\Other\\app.exe"};
    input_overlay::NormalizeSettings(settings);
    Check(settings.x == 0 && settings.y == 32767, "Anchor margins must be nonnegative and bounded");
    Check(settings.scale == 10 && settings.controllerScale == 200 && settings.opacity == 100, "Visual bounds must clamp independently");
    Check(settings.hotkeyVk == VK_F10 && settings.hotkeyModifiers == (MOD_CONTROL | MOD_ALT), "Invalid shortcut needs safe default");
    Check(settings.slots[1].input == VK_SHIFT && settings.slots[2].input == 0 && settings.slots[ShiftSlot].input == 0, "Duplicate aliases must normalize once");
    Check(settings.slots[3].input == 0, "Invalid input must become unbound");
    Check(settings.slots[4].label == L"Somelabel" && settings.slots[5].label.size() == 24, "Labels need bounded control-free text");
    Check(settings.applications.size() == 2, "Only unique absolute application paths are allowed");
    Check(input_overlay::InputName(VK_RSHIFT) == L"Shift", "Modifier name must be English and canonical");
    Check(input_overlay::HotkeyName(MOD_CONTROL | MOD_ALT, VK_F10) == L"Ctrl + Alt + F10", "Shortcut label must be English");
}

void TestApplicationMatching() {
    auto settings = input_overlay::DefaultSettings();
    Check(input_overlay::MatchesApplication(settings, L""), "Unrestricted mode must allow any foreground app");
    settings.onlySelectedApps = true;
    Check(!input_overlay::MatchesApplication(settings, L"C:\\Games\\game.exe"), "Restricted empty list must hide overlay");
    settings.applications = {L"C:\\Games\\game.exe"};
    Check(input_overlay::MatchesApplication(settings, L"c:\\GAMES\\GAME.EXE"), "Full paths must compare case insensitively");
    Check(input_overlay::MatchesApplication(settings, L"C:/Games/game.exe"), "Full path separators should normalize");
    Check(input_overlay::MatchesApplication(settings, L"\\\\?\\C:\\Games\\game.exe"), "Extended drive path should match");
    Check(!input_overlay::MatchesApplication(settings, L"C:\\Games\\game.exe.attacker.exe"), "Application allowlist must not use prefix matching");
    Check(!input_overlay::MatchesApplication(settings, L"D:\\Games\\game.exe"), "Same executable name at different path must not match");
    Check(!input_overlay::MatchesApplication(settings, L"game.exe"), "Relative names must not match");
    Check(!input_overlay::MatchesApplication(settings, L""), "Unknown foreground process must not match restrictions");
}

void TestGeometry() {
    auto settings = input_overlay::DefaultSettings();
    const RECT fullHD{0, 0, 1920, 1080}, hd{0, 0, 1280, 720};
    auto fullPosition = input_overlay::AnchoredPosition(settings, fullHD);
    auto hdPosition = input_overlay::AnchoredPosition(settings, hd);
    auto size = input_overlay::OverlaySize(settings.scale);
    Check(fullPosition.x == 32 && fullPosition.y == 894, "Default overlay must anchor 32px from left and bottom");
    Check(hdPosition.x == 32 && hdPosition.y == 534, "Resolution change must preserve physical bottom margin");
    Check(fullHD.bottom - fullPosition.y - size.cy == hd.bottom - hdPosition.y - size.cy, "1080p to 720p must preserve bottom distance");
    settings.anchorRight = true;
    fullPosition = input_overlay::AnchoredPosition(settings, fullHD);
    hdPosition = input_overlay::AnchoredPosition(settings, hd);
    Check(fullPosition.x == 1531 && hdPosition.x == 891, "Right anchored margin must survive resolution change");
    const RECT leftMonitor{-1920, -200, 0, 880};
    auto negativePosition = input_overlay::AnchoredPosition(settings, leftMonitor);
    Check(negativePosition.x == -389 && negativePosition.y == 694, "Negative monitor origin must preserve edge anchors");
    input_overlay::AnchorPosition(settings, leftMonitor, {-1910, 706});
    Check(!settings.anchorRight && settings.anchorBottom && settings.x == 10 && settings.y == 20, "Dragging near lower left must choose left/bottom margins");
    auto roundTrip = input_overlay::AnchoredPosition(settings, leftMonitor);
    Check(roundTrip.x == -1910 && roundTrip.y == 706, "Negative display position must round trip through anchors");
    input_overlay::AnchorPosition(settings, leftMonitor, {-367, -188});
    Check(settings.anchorRight && !settings.anchorBottom && settings.x == 10 && settings.y == 12, "Dragging near upper right must choose right/top margins");
    roundTrip = input_overlay::AnchoredPosition(settings, leftMonitor);
    Check(roundTrip.x == -367 && roundTrip.y == -188, "Right/top position must round trip through anchors");
    settings.x = settings.y = 32767;
    settings.anchorRight = settings.anchorBottom = false;
    auto clamped = input_overlay::AnchoredPosition(settings, fullHD);
    Check(clamped.x == 1563 && clamped.y == 926, "Large left/top margins must clamp overlay onto monitor");
    settings.anchorRight = settings.anchorBottom = true;
    clamped = input_overlay::AnchoredPosition(settings, fullHD);
    Check(clamped.x == 0 && clamped.y == 0, "Large right/bottom margins must not put overlay offscreen");
    settings.scale = 200;
    const RECT tiny{10, 20, 310, 170};
    clamped = input_overlay::AnchoredPosition(settings, tiny);
    Check(clamped.x == 10 && clamped.y == 20, "Oversized overlay must clamp to display origin without invalid clamp bounds");
    const auto minimum = input_overlay::OverlaySize(10), maximum = input_overlay::OverlaySize(200);
    Check(minimum.cx == 36 && minimum.cy == 15 && maximum.cx == 714 && maximum.cy == 308, "Scale endpoints must use fixed physical pixels");
    Check(input_overlay::OverlaySize(-1).cx == minimum.cx && input_overlay::OverlaySize(INT_MAX).cy == maximum.cy, "Geometry must independently clamp scale");
    const auto previousDpi = SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_UNAWARE);
    const auto unawareSize = input_overlay::OverlaySize(100);
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    const auto awareSize = input_overlay::OverlaySize(100);
    if (previousDpi) SetThreadDpiAwarenessContext(previousDpi);
    Check(unawareSize.cx == 357 && unawareSize.cy == 154 && awareSize.cx == unawareSize.cx && awareSize.cy == unawareSize.cy,
        "Overlay size must not depend on thread DPI awareness");
}

void TestControllerScale(const TemporaryDirectory& temp) {
    const auto file = temp.File(L"controller-scale.ini");
    auto settings = input_overlay::DefaultSettings();
    Check(settings.scale == 100 && settings.controllerScale == 100, "Both devices must default to 100 percent");
    settings.scale = 75;
    settings.controllerScale = 145;
    settings.anchorRight = settings.anchorBottom = true;
    const RECT display{-1920, -200, 0, 880}, resized{-1280, 0, 0, 720};
    for (const auto device : {input_overlay::OverlayDevice::KeyboardMouse, input_overlay::OverlayDevice::Controller}) {
        settings.device = device;
        const int expected = device == input_overlay::OverlayDevice::Controller ? 145 : 75;
        Check(input_overlay::EffectiveOverlayScale(settings) == expected, "Effective scale must use the active device preference");
        const auto size = input_overlay::OverlaySize(expected);
        for (const RECT screen : {display, resized}) {
            const auto position = input_overlay::AnchoredPosition(settings, screen);
            Check(screen.right - position.x - size.cx == settings.x && screen.bottom - position.y - size.cy == settings.y,
                "Resolution changes must preserve active device size and edge margins");
            auto dragged = settings;
            input_overlay::AnchorPosition(dragged, screen, position);
            Check(dragged.anchorRight && dragged.anchorBottom && dragged.x == settings.x && dragged.y == settings.y,
                "Dragging must calculate anchors from the active device dimensions");
        }
        Check(input_overlay::SaveSettings(file, settings), "Independent device sizes must save");
        input_overlay::Settings loaded;
        Check(input_overlay::LoadSettings(file, loaded) && loaded.scale == 75 && loaded.controllerScale == 145 &&
            loaded.device == device && input_overlay::EffectiveOverlayScale(loaded) == expected,
            "Both device sizes must persist independently of the selected device");
    }
    for (const int legacy : {-1, 10, 75, 100, 200, INT_MAX}) {
        WriteBytes(file, "[General]\nscale=" + std::to_string(legacy) + "\nenabled=0\nonlySelectedApps=1\n");
        Check(input_overlay::LoadSettings(file, settings) && settings.scale == std::clamp(legacy, 10, 200) &&
            settings.controllerScale == settings.scale && !settings.enabled && settings.onlySelectedApps,
            "Legacy shared size must migrate without changing visibility or application filters");
    }
    for (const bool reversed : {false, true}) {
        const std::string values = reversed ? "controllerScale=100\nscale=75\n" : "scale=75\ncontrollerScale=100\n";
        WriteBytes(file, "[General]\n" + values);
        Check(input_overlay::LoadSettings(file, settings) && settings.scale == 75 && settings.controllerScale == 100,
            "Explicit default controller size must suppress migration regardless of entry order");
    }
    for (const int value : {INT_MIN, 10, 123, 200, INT_MAX}) {
        WriteBytes(file, "[General]\nscale=75\ncontrollerScale=" + std::to_string(value) + "\n");
        Check(input_overlay::LoadSettings(file, settings) && settings.scale == 75 && settings.controllerScale == std::clamp(value, 10, 200),
            "Controller size must clamp independently when loaded");
        settings.controllerScale = value;
        Check(input_overlay::SaveSettings(file, settings) && input_overlay::LoadSettings(file, settings) &&
            settings.scale == 75 && settings.controllerScale == std::clamp(value, 10, 200),
            "Controller size must clamp independently when saved");
    }
    for (const char* invalid : {"2147483648", "invalid", ""}) {
        WriteBytes(file, std::string("[General]\nscale=75\ncontrollerScale=145\ncontrollerScale=") + invalid + "\n");
        Check(input_overlay::LoadSettings(file, settings) && settings.scale == 75 && settings.controllerScale == 100,
            "Malformed explicit controller size must recover to its own default");
    }
    Check(!input_overlay::LoadSettings(temp.File(L"new-device-sizes.ini"), settings) && settings.scale == 100 && settings.controllerScale == 100,
        "Missing settings must reset both sizes to their defaults");
}

void TestPersistence(const TemporaryDirectory& temp) {
    const auto file = temp.File(L"settings.ini");
    auto settings = input_overlay::DefaultSettings();
    settings.slots[0].label = L"\u00c7\u0130\u015e \u9375";
    settings.slots[1].label = L"[Q] = \\";
    settings.slots[3].visible = false;
    settings.x = 2500; settings.scale = 175; settings.opacity = 47;
    settings.enabled = false; settings.onlySelectedApps = true; settings.showMouse = false;
    settings.style = input_overlay::OverlayStyle::Glass;
    settings.anchorRight = true; settings.anchorBottom = false; settings.isoLayout = true;
    settings.monitor = L"\\\\.\\DISPLAY2";
    settings.hotkeyVk = VK_F12; settings.hotkeyModifiers = MOD_SHIFT;
    settings.applications = {L"C:\\Fixture\\\u00c7\u0130\u015e\\\u30b2\u30fc\u30e0.exe", L"D:\\Space Dir\\Game.exe"};
    input_overlay::BindInput(settings, SideSlot, 'Q');
    Check(input_overlay::SaveSettings(file, settings), "Save config failed");
    input_overlay::Settings loaded;
    Check(input_overlay::LoadSettings(file, loaded), "Load config failed");
    for (size_t i = 0; i < input_overlay::SlotCount; ++i) {
        Check(loaded.slots[i].label == settings.slots[i].label, "Unicode labels must round trip");
        Check(loaded.slots[i].input == settings.slots[i].input, "Bindings must round trip");
        Check(loaded.slots[i].visible == settings.slots[i].visible, "Visibility must round trip");
    }
    Check(loaded.x == 2500 && loaded.scale == 175 && loaded.opacity == 47, "Visual settings must round trip");
    Check(!loaded.enabled && loaded.onlySelectedApps && !loaded.showMouse, "Boolean settings must round trip");
    Check(loaded.style == input_overlay::OverlayStyle::Glass, "Selected overlay style must round trip");
    Check(loaded.anchorRight && !loaded.anchorBottom && loaded.isoLayout && loaded.monitor == settings.monitor, "Anchor, keyboard layout and display must round trip");
    Check(loaded.hotkeyVk == VK_F12 && loaded.hotkeyModifiers == MOD_SHIFT, "Hotkey must round trip");
    Check(loaded.applications == settings.applications, "Unicode paths must round trip");
    loaded.opacity = 65;
    Check(input_overlay::SaveSettings(file, loaded) && input_overlay::LoadSettings(file, loaded) && loaded.opacity == 65, "Atomic replacement must replace existing settings");
    HANDLE locked = CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "Lock fixture failed");
    loaded.opacity = 25;
    const bool blocked = input_overlay::SaveSettings(file, loaded);
    CloseHandle(locked);
    Check(!blocked, "Failed atomic replacement must be reported");
    Check(input_overlay::LoadSettings(file, loaded) && loaded.opacity == 65, "Failed save must preserve previous file");
    WIN32_FIND_DATAW entry{};
    HANDLE search = FindFirstFileW((file + L".tmp.*").c_str(), &entry);
    if (search != INVALID_HANDLE_VALUE) FindClose(search);
    Check(search == INVALID_HANDLE_VALUE, "Save must not leave temporary files");
    Check(!input_overlay::SaveSettings(temp.File(L"missing\\settings.ini"), settings), "Unwritable destination must report failure");
}

void TestStyles(const TemporaryDirectory& temp) {
    const auto file = temp.File(L"styles.ini");
    const input_overlay::OverlayStyle styles[] = {input_overlay::OverlayStyle::Outline, input_overlay::OverlayStyle::Neon,
        input_overlay::OverlayStyle::Glass, input_overlay::OverlayStyle::Circuit, input_overlay::OverlayStyle::Pearl,
        input_overlay::OverlayStyle::Gradient};
    Check(input_overlay::DefaultSettings().style == input_overlay::OverlayStyle::Pearl, "New settings must use Pearl style");
    input_overlay::Settings newInstall;
    Check(!input_overlay::LoadSettings(temp.File(L"new-install.ini"), newInstall) && newInstall.style == input_overlay::OverlayStyle::Pearl,
        "A new installation without a settings file must retain the Pearl default");
    for (const auto style : styles) {
        auto settings = input_overlay::DefaultSettings();
        settings.style = style;
        settings.scale = 137;
        Check(input_overlay::SaveSettings(file, settings), "Saving each supported style must succeed");
        input_overlay::Settings loaded;
        Check(input_overlay::LoadSettings(file, loaded) && loaded.style == style && loaded.scale == 137,
            "Every supported style must round trip without changing other settings");
    }
    for (const int invalid : {-1, input_overlay::OverlayStyleCount, INT_MIN, INT_MAX}) {
        auto settings = input_overlay::DefaultSettings();
        settings.style = static_cast<input_overlay::OverlayStyle>(invalid);
        input_overlay::NormalizeSettings(settings);
        Check(settings.style == input_overlay::OverlayStyle::Outline, "Invalid in-memory style must normalize to Outline");
        settings.style = static_cast<input_overlay::OverlayStyle>(invalid);
        Check(input_overlay::SaveSettings(file, settings), "Saving an invalid style must recover safely");
        Check(input_overlay::LoadSettings(file, settings) && settings.style == input_overlay::OverlayStyle::Outline,
            "Save must persist the safe fallback for invalid styles");
    }
    for (const char* invalid : {"-1", "6", "2147483648", "invalid", ""}) {
        WriteBytes(file, std::string("[General]\nversion=1\nstyle=2\nstyle=") + invalid + "\nopacity=73\n");
        input_overlay::Settings loaded;
        Check(input_overlay::LoadSettings(file, loaded) && loaded.style == input_overlay::OverlayStyle::Outline && loaded.opacity == 73,
            "Invalid INI styles must fall back to Outline while preserving other entries");
    }
    WriteBytes(file, "[General]\nversion=1\nscale=75\n[Slot7]\nlabel=Legacy Q\n");
    auto legacy = input_overlay::DefaultSettings();
    legacy.style = input_overlay::OverlayStyle::Neon;
    Check(input_overlay::LoadSettings(file, legacy) && legacy.style == input_overlay::OverlayStyle::Outline &&
        legacy.scale == 75 && legacy.slots[QSlot].label == L"Legacy Q",
        "Version 1 settings without a style must keep existing values and default to Outline");
    for (int oldId = 0; oldId < 5; ++oldId) {
        WriteBytes(file, "[General]\nversion=1\nstyle=" + std::to_string(oldId) + "\n");
        Check(input_overlay::LoadSettings(file, legacy) && legacy.style == styles[oldId],
            "Saved pre-Gradient style IDs must retain their original appearance");
    }
}

void TestColorSettings(const TemporaryDirectory& temp) {
    const auto file = temp.File(L"colors.ini");
    const auto defaults = input_overlay::DefaultSettings();
    Check(defaults.colorTheme == input_overlay::ColorTheme::Original,
        "New settings must preserve the original color appearance");
    Check(defaults.gradientFillOpacity == 16 && defaults.backgroundStart == RGB(255, 178, 91) &&
        defaults.backgroundEnd == RGB(192, 121, 242), "Gradient defaults must match the approved Sunset colors and fill");
    auto saved = defaults;
    saved.enabled = false;
    saved.onlySelectedApps = true;
    saved.applications = {temp.File(L"Absent Game\\game.exe")};
    saved.style = input_overlay::OverlayStyle::Gradient;
    saved.hotkeyVk = VK_F8;
    saved.hotkeyModifiers = MOD_SHIFT;
    saved.accent = RGB(19, 72, 211);
    saved.backgroundStart = RGB(0, 121, 255);
    saved.backgroundEnd = RGB(241, 38, 0);
    input_overlay::BindInput(saved, SideSlot, 'Q');
    for (int theme = 0; theme < input_overlay::ColorThemeCount; ++theme) for (const int fill : {4, 16, 45}) {
        saved.colorTheme = static_cast<input_overlay::ColorTheme>(theme);
        saved.gradientFillOpacity = fill;
        Check(input_overlay::SaveSettings(file, saved), "Every color theme must save");
        input_overlay::Settings restored;
        Check(input_overlay::LoadSettings(file, restored) && restored.colorTheme == saved.colorTheme &&
            restored.backgroundStart == saved.backgroundStart && restored.backgroundEnd == saved.backgroundEnd &&
            restored.accent == saved.accent && restored.gradientFillOpacity == fill,
            "Theme, fill opacity and custom color channels must round trip exactly");
        Check(!restored.enabled && restored.onlySelectedApps && restored.applications == saved.applications &&
            restored.style == saved.style && restored.hotkeyVk == VK_F8 && restored.hotkeyModifiers == MOD_SHIFT &&
            restored.slots[SideSlot].input == 'Q' && restored.slots[QSlot].input == 0,
            "Color preferences must preserve restrictions, disabled state, style, hotkey and input bindings");
    }
    for (const int invalid : {-1, input_overlay::ColorThemeCount, INT_MIN, INT_MAX}) {
        auto malformed = saved;
        malformed.colorTheme = static_cast<input_overlay::ColorTheme>(invalid);
        malformed.backgroundStart = 0xff123456;
        malformed.backgroundEnd = 0x809abcde;
        input_overlay::NormalizeSettings(malformed);
        Check(malformed.colorTheme == input_overlay::ColorTheme::Original &&
            malformed.backgroundStart == 0x123456 && malformed.backgroundEnd == 0x9abcde,
            "Normalization must recover unknown themes and keep only RGB color bits");
        malformed.colorTheme = static_cast<input_overlay::ColorTheme>(invalid);
        malformed.backgroundStart |= 0x80000000;
        Check(input_overlay::SaveSettings(file, malformed) && input_overlay::LoadSettings(file, malformed) &&
            malformed.colorTheme == input_overlay::ColorTheme::Original && malformed.backgroundStart == 0x123456,
            "Saving malformed in-memory colors must persist safe normalized values");
    }
    for (const char* invalid : {"-1", "6", "2147483648", "invalid", ""}) {
        WriteBytes(file, std::string("[General]\ncolorTheme=5\ncolorTheme=") + invalid + "\nopacity=73\n");
        input_overlay::Settings loaded;
        Check(input_overlay::LoadSettings(file, loaded) && loaded.colorTheme == input_overlay::ColorTheme::Original &&
            loaded.opacity == 73, "Malformed stored themes must safely restore Original without losing other settings");
    }
    for (const char* invalid : {"-1", "16777216", "2147483648", "#ABCDEF", "0x123456", "invalid", ""}) {
        const std::string entries = std::string("\nbackgroundStart=") + invalid + "\nbackgroundEnd=" + invalid + "\n";
        WriteBytes(file, "[General]\ncolorTheme=5" + entries);
        input_overlay::Settings loaded;
        Check(input_overlay::LoadSettings(file, loaded) && loaded.colorTheme == input_overlay::ColorTheme::Custom &&
            loaded.backgroundStart == defaults.backgroundStart && loaded.backgroundEnd == defaults.backgroundEnd,
            "Malformed stored colors must retain their defaults and selected theme");
        WriteBytes(file, "[General]\nbackgroundStart=0\nbackgroundEnd=16777215" + entries);
        Check(input_overlay::LoadSettings(file, loaded) && loaded.backgroundStart == RGB(0, 0, 0) &&
            loaded.backgroundEnd == RGB(255, 255, 255),
            "Invalid duplicate colors must not replace valid black and white endpoints");
    }
    WriteBytes(file, "[General]\nversion=1\nstyle=4\naccent=13781005\nenabled=0\nonlySelectedApps=1\nscale=75\n");
    saved.colorTheme = input_overlay::ColorTheme::Aurora;
    Check(input_overlay::LoadSettings(file, saved) && saved.colorTheme == input_overlay::ColorTheme::Original &&
        saved.backgroundStart == defaults.backgroundStart && saved.backgroundEnd == defaults.backgroundEnd &&
        saved.style == input_overlay::OverlayStyle::Pearl && saved.accent == 13781005 && !saved.enabled &&
        saved.onlySelectedApps && saved.scale == 75 && saved.gradientFillOpacity == 16,
        "Legacy settings must preserve their appearance and application restrictions");
    for (const int invalid : {INT_MIN, -1, 0, 3, 46, INT_MAX}) {
        saved.gradientFillOpacity = invalid;
        input_overlay::NormalizeSettings(saved);
        const int expected = invalid < 4 ? 4 : 45;
        Check(saved.gradientFillOpacity == expected, "Gradient fill normalization must clamp to 4 through 45 percent");
        WriteBytes(file, "[General]\nstyle=5\nonlySelectedApps=1\ngradientFillOpacity=" + std::to_string(invalid) + "\n");
        Check(input_overlay::LoadSettings(file, saved) && saved.gradientFillOpacity == expected &&
            saved.style == input_overlay::OverlayStyle::Gradient && saved.onlySelectedApps,
            "Stored gradient fill must clamp safely without altering the mode or application restriction");
    }
    for (const char* invalid : {"2147483648", "-2147483649", "16.5", "16%", "invalid", ""}) {
        WriteBytes(file, std::string("[General]\nstyle=5\nenabled=0\ngradientFillOpacity=32\ngradientFillOpacity=") + invalid + "\n");
        Check(input_overlay::LoadSettings(file, saved) && saved.gradientFillOpacity == 16 &&
            saved.style == input_overlay::OverlayStyle::Gradient && !saved.enabled,
            "Malformed gradient fill must restore the default without changing the saved overlay state");
    }
}

void TestGradientPalettes() {
    struct Preset { input_overlay::ColorTheme theme; COLORREF start, end; };
    const Preset presets[] = {
        {input_overlay::ColorTheme::Original, RGB(255, 178, 91), RGB(192, 121, 242)},
        {input_overlay::ColorTheme::Sunset, RGB(255, 178, 91), RGB(192, 121, 242)},
        {input_overlay::ColorTheme::Aurora, RGB(97, 230, 190), RGB(154, 140, 250)},
        {input_overlay::ColorTheme::Ocean, RGB(88, 223, 237), RGB(98, 137, 242)},
        {input_overlay::ColorTheme::Rose, RGB(255, 196, 160), RGB(234, 129, 184)},
        {input_overlay::ColorTheme::Custom, RGB(11, 73, 141), RGB(231, 129, 5)}
    };
    auto settings = input_overlay::DefaultSettings();
    settings.style = input_overlay::OverlayStyle::Gradient;
    settings.backgroundStart = RGB(11, 73, 141);
    settings.backgroundEnd = RGB(231, 129, 5);
    for (const auto& preset : presets) {
        settings.colorTheme = preset.theme;
        const auto palette = input_overlay::ThemePalette(settings);
        Check(palette.start == preset.start && palette.end == preset.end &&
            palette.middle == input_overlay::MixColor(preset.start, preset.end, 0.5f),
            "Each gradient preset must use the approved endpoints and their linear midpoint");
        for (const float point : {-1.0f, 0.0f, 0.25f, 0.5f, 0.75f, 1.0f, 2.0f}) {
            Check(input_overlay::ThemeColorAt(settings, point) == input_overlay::MixColor(preset.start, preset.end, point),
                "Gradient samples must interpolate directly between two endpoints and clamp outside their range");
        }
        Check(settings.backgroundStart == RGB(11, 73, 141) && settings.backgroundEnd == RGB(231, 129, 5),
            "Resolving a preset must leave custom color choices intact");
    }
}

void TestColorHex() {
    struct ValidColor { const wchar_t* text; COLORREF value; const wchar_t* formatted; };
    const ValidColor valid[] = {
        {L"#000000", RGB(0, 0, 0), L"#000000"}, {L"FFFFFF", RGB(255, 255, 255), L"#FFFFFF"},
        {L"#ff0012", RGB(255, 0, 18), L"#FF0012"}, {L"aB09cD", RGB(171, 9, 205), L"#AB09CD"}
    };
    for (const auto& entry : valid) {
        COLORREF parsed = RGB(1, 2, 3);
        Check(input_overlay::ParseColorHex(entry.text, parsed) && parsed == entry.value,
            "HEX input must accept optional hash and mixed case without reversing RGB channels");
        Check(input_overlay::ColorHex(parsed) == entry.formatted,
            "Formatted HEX must use six uppercase RGB digits with a leading hash");
        COLORREF reparsed = 0;
        Check(input_overlay::ParseColorHex(input_overlay::ColorHex(parsed), reparsed) && reparsed == parsed,
            "Formatted HEX must parse back to the exact selected color");
    }
    for (const wchar_t* invalid : {L"", L"#", L"#ABC", L"ABCDE", L"ABCDEFG", L"#12345678", L"0x112233",
        L"#12GG34", L"# 12345", L" 123456", L"123456 ", L"12345\n", L"\uff1112345"}) {
        COLORREF parsed = RGB(11, 22, 33);
        Check(!input_overlay::ParseColorHex(invalid, parsed) && parsed == RGB(11, 22, 33),
            "Malformed HEX must be rejected without overwriting the current color");
    }
    COLORREF parsed = RGB(11, 22, 33);
    Check(!input_overlay::ParseColorHex(std::wstring(L"12\0ABC", 6), parsed) && parsed == RGB(11, 22, 33),
        "Embedded nulls must not be accepted as HEX color input");
    Check(input_overlay::ColorHex(0xaa123456) == L"#563412",
        "HEX formatting must ignore COLORREF reserved bits and use RGB channel order");
}

void TestStartupState(const TemporaryDirectory& temp) {
    const auto file = temp.File(L"startup-state.ini");
    const auto absentGame = temp.File(L"Rocket League\\Binaries\\Win64\\RocketLeague.exe");
    Check(GetFileAttributesW(absentGame.c_str()) == INVALID_FILE_ATTRIBUTES,
        "The saved game fixture must not exist or be running");
    auto saved = input_overlay::DefaultSettings();
    Check(!saved.startMinimized, "New settings must open the settings window by default");
    saved.enabled = false;
    saved.onlySelectedApps = true;
    saved.startMinimized = true;
    saved.applications = {absentGame, L"D:\\Games\\Another Game\\game.exe"};
    saved.style = input_overlay::OverlayStyle::Circuit;
    input_overlay::BindInput(saved, SideSlot, 'Q');
    Check(input_overlay::SaveSettings(file, saved), "Saving disabled and restricted startup state must succeed");
    input_overlay::Settings restored;
    Check(input_overlay::LoadSettings(file, restored), "Fresh settings load must succeed");
    Check(!restored.enabled && restored.onlySelectedApps && restored.startMinimized,
        "Starting minimized must preserve the saved disabled and restricted state");
    Check(restored.applications == saved.applications && input_overlay::MatchesApplication(restored, absentGame),
        "Saved full application paths must persist even when their executables are absent");
    Check(!input_overlay::MatchesApplication(restored, L"D:\\Other\\RocketLeague.exe"),
        "Restored restrictions must not allow an executable at a different path");
    Check(restored.style == saved.style && restored.slots[SideSlot].input == 'Q' && restored.slots[QSlot].input == 0,
        "Startup preferences must preserve style and remapped input settings");
    restored.startMinimized = false;
    Check(input_overlay::SaveSettings(file, restored), "Turning off minimized startup must save");
    input_overlay::Settings reopened;
    Check(input_overlay::LoadSettings(file, reopened) && !reopened.startMinimized && !reopened.enabled &&
        reopened.onlySelectedApps && reopened.applications == saved.applications,
        "Changing minimized startup must not re-enable the overlay or erase saved applications");
    for (const char* invalid : {"-1", "2", "true", "2147483648", ""}) {
        WriteBytes(file, std::string("[General]\nversion=1\nenabled=0\nonlySelectedApps=1\nstartMinimized=") + invalid + "\n");
        reopened.startMinimized = true;
        Check(input_overlay::LoadSettings(file, reopened) && !reopened.startMinimized && !reopened.enabled && reopened.onlySelectedApps,
            "Invalid minimized-startup booleans must use the default without changing other flags");
    }
    WriteBytes(file, "[General]\nversion=1\nenabled=0\nonlySelectedApps=1\nscale=75\n");
    reopened.startMinimized = true;
    Check(input_overlay::LoadSettings(file, reopened) && !reopened.startMinimized && !reopened.enabled &&
        reopened.onlySelectedApps && reopened.scale == 75,
        "Legacy files without minimized startup must retain their disabled and restricted settings");
}

void TestControllerSettings(const TemporaryDirectory& temp) {
    const auto file = temp.File(L"controller.ini");
    auto settings = input_overlay::DefaultSettings();
    Check(settings.device == input_overlay::OverlayDevice::KeyboardMouse && settings.controllerLayout == input_overlay::ControllerLayout::Xbox &&
        settings.controllerIndex == -1 && settings.controllerDeadzone == 15,
        "Controller support must preserve the keyboard and mouse defaults");
    settings.device = input_overlay::OverlayDevice::Controller;
    settings.controllerLayout = input_overlay::ControllerLayout::PlayStation;
    settings.enabled = false;
    settings.onlySelectedApps = true;
    settings.scale = 125;
    settings.controllerScale = 80;
    settings.applications = {temp.File(L"Absent Game\\game.exe")};
    input_overlay::BindInput(settings, SideSlot, 'Q');
    for (int index = -1; index < input_overlay::ControllerSlotCount; ++index) for (const int deadzone : {0, 15, 40}) {
        settings.controllerIndex = index;
        settings.controllerDeadzone = deadzone;
        Check(input_overlay::SaveSettings(file, settings), "Controller settings must save");
        input_overlay::Settings restored;
        Check(input_overlay::LoadSettings(file, restored) && restored.device == input_overlay::OverlayDevice::Controller &&
            restored.controllerLayout == input_overlay::ControllerLayout::PlayStation && restored.controllerIndex == index &&
            restored.controllerDeadzone == deadzone, "Controller layout, player and deadzone must round trip");
        Check(!restored.enabled && restored.onlySelectedApps && restored.applications == settings.applications &&
            restored.slots[SideSlot].input == 'Q' && restored.slots[QSlot].input == 0 &&
            restored.scale == 125 && restored.controllerScale == 80,
            "Controller preferences must not erase restrictions, disabled state, keyboard remaps or independent sizes");
    }
    for (const int invalid : {-100, input_overlay::ControllerSlotCount, INT_MAX}) {
        settings.device = static_cast<input_overlay::OverlayDevice>(invalid);
        settings.controllerLayout = static_cast<input_overlay::ControllerLayout>(invalid);
        settings.controllerIndex = invalid;
        settings.controllerDeadzone = invalid;
        input_overlay::NormalizeSettings(settings);
        Check(settings.device == input_overlay::OverlayDevice::KeyboardMouse && settings.controllerLayout == input_overlay::ControllerLayout::Xbox &&
            settings.controllerIndex == -1 && settings.controllerDeadzone >= 0 && settings.controllerDeadzone <= 40,
            "Invalid in-memory controller settings must normalize safely");
    }
    for (const char* invalid : {"-100", "8", "2147483648", "invalid", ""}) {
        WriteBytes(file, std::string("[General]\nversion=1\nenabled=0\ndevice=1\ndevice=") + invalid +
            "\ncontrollerLayout=1\ncontrollerLayout=" + invalid + "\ncontrollerIndex=2\ncontrollerIndex=" + invalid + "\n");
        Check(input_overlay::LoadSettings(file, settings) && !settings.enabled && settings.device == input_overlay::OverlayDevice::KeyboardMouse &&
            settings.controllerLayout == input_overlay::ControllerLayout::Xbox && settings.controllerIndex == -1,
            "Malformed or unsupported controller IDs must use safe defaults without affecting enabled state");
    }
    for (const char* invalid : {"2147483648", "invalid", ""}) {
        WriteBytes(file, std::string("[General]\ncontrollerDeadzone=25\ncontrollerDeadzone=") + invalid + "\n");
        Check(input_overlay::LoadSettings(file, settings) && settings.controllerDeadzone == 15,
            "Malformed controller deadzone must recover to its default");
    }
    WriteBytes(file, "[General]\ncontrollerDeadzone=-1\n");
    Check(input_overlay::LoadSettings(file, settings) && settings.controllerDeadzone == 0, "Stored deadzone must clamp at zero");
    WriteBytes(file, "[General]\ncontrollerDeadzone=999\n");
    Check(input_overlay::LoadSettings(file, settings) && settings.controllerDeadzone == 40, "Stored deadzone must clamp at forty");
    WriteBytes(file, "[General]\nversion=1\nenabled=0\nscale=75\nonlySelectedApps=1\n");
    Check(input_overlay::LoadSettings(file, settings) && settings.device == input_overlay::OverlayDevice::KeyboardMouse &&
        settings.controllerLayout == input_overlay::ControllerLayout::Xbox && settings.controllerIndex == -1 && settings.controllerDeadzone == 15 &&
        !settings.enabled && settings.onlySelectedApps && settings.scale == 75,
        "Legacy files must remain in keyboard and mouse mode and retain their existing restrictions");
}

void TestControllerStyles(const TemporaryDirectory& temp) {
    const auto file = temp.File(L"controller-styles.ini");
    auto settings = input_overlay::DefaultSettings();
    Check(settings.controllerStyle == input_overlay::ControllerStyle::Frost,
        "New settings must use the Frost controller design");
    settings.controllerStyle = input_overlay::ControllerStyle::Prism;
    Check(!input_overlay::LoadSettings(temp.File(L"new-controller-style.ini"), settings) &&
        settings.controllerStyle == input_overlay::ControllerStyle::Frost,
        "A missing settings file must restore the default controller design");
    settings.device = input_overlay::OverlayDevice::Controller;
    settings.controllerIndex = 2;
    settings.controllerDeadzone = 23;
    settings.enabled = false;
    settings.onlySelectedApps = true;
    settings.startMinimized = true;
    settings.autoCheckUpdates = false;
    settings.applications = {temp.File(L"Absent Game\\game.exe")};
    settings.x = 83; settings.y = 57; settings.scale = 123; settings.opacity = 64;
    settings.anchorRight = true; settings.anchorBottom = false;
    settings.monitor = L"\\\\.\\DISPLAY2";
    settings.isoLayout = true; settings.showMouse = false;
    settings.colorTheme = input_overlay::ColorTheme::Custom;
    settings.backgroundStart = RGB(25, 81, 147); settings.backgroundEnd = RGB(230, 156, 7);
    settings.gradientFillOpacity = 24; settings.accent = RGB(63, 224, 190);
    settings.hotkeyVk = VK_F8; settings.hotkeyModifiers = MOD_SHIFT;
    input_overlay::BindInput(settings, SideSlot, 'Q');
    for (int style = 0; style < input_overlay::ControllerStyleCount; ++style)
        for (int keyboardStyle = 0; keyboardStyle < input_overlay::OverlayStyleCount; ++keyboardStyle)
            for (const auto layout : {input_overlay::ControllerLayout::Xbox, input_overlay::ControllerLayout::PlayStation}) {
                settings.controllerStyle = static_cast<input_overlay::ControllerStyle>(style);
                settings.style = static_cast<input_overlay::OverlayStyle>(keyboardStyle);
                settings.controllerLayout = layout;
                Check(input_overlay::SaveSettings(file, settings), "Each controller design and layout must save");
                input_overlay::Settings restored;
                Check(input_overlay::LoadSettings(file, restored) && restored.controllerStyle == settings.controllerStyle &&
                    restored.controllerLayout == layout && restored.style == settings.style &&
                    restored.device == settings.device && restored.controllerIndex == 2 && restored.controllerDeadzone == 23,
                    "Controller designs must round trip independently of keyboard styles and controller layout");
                Check(!restored.enabled && restored.onlySelectedApps && restored.startMinimized && !restored.autoCheckUpdates &&
                    restored.applications == settings.applications && restored.hotkeyVk == VK_F8 && restored.hotkeyModifiers == MOD_SHIFT &&
                    restored.slots[SideSlot].input == 'Q' && restored.slots[QSlot].input == 0,
                    "Changing controller design must preserve visibility restrictions, startup preferences and input bindings");
                Check(restored.x == 83 && restored.y == 57 && restored.scale == 123 && restored.opacity == 64 &&
                    restored.anchorRight && !restored.anchorBottom && restored.monitor == settings.monitor &&
                    restored.isoLayout && !restored.showMouse && restored.colorTheme == settings.colorTheme &&
                    restored.backgroundStart == settings.backgroundStart && restored.backgroundEnd == settings.backgroundEnd &&
                    restored.gradientFillOpacity == 24 && restored.accent == settings.accent,
                    "Changing controller design must preserve placement, scale, keyboard layout and color preferences");
            }
    for (const int invalid : {INT_MIN, -1, input_overlay::ControllerStyleCount, INT_MAX}) {
        settings.controllerStyle = static_cast<input_overlay::ControllerStyle>(invalid);
        input_overlay::NormalizeSettings(settings);
        Check(settings.controllerStyle == input_overlay::ControllerStyle::Frost &&
            settings.style == input_overlay::OverlayStyle::Gradient && !settings.enabled && settings.onlySelectedApps,
            "Invalid controller designs must normalize to Frost without changing the keyboard style or visibility");
        settings.controllerStyle = static_cast<input_overlay::ControllerStyle>(invalid);
        Check(input_overlay::SaveSettings(file, settings) && static_cast<int>(settings.controllerStyle) == invalid,
            "Saving an invalid controller design must normalize a copy of the settings");
        input_overlay::Settings restored;
        Check(input_overlay::LoadSettings(file, restored) && restored.controllerStyle == input_overlay::ControllerStyle::Frost &&
            restored.style == input_overlay::OverlayStyle::Gradient && restored.applications == settings.applications,
            "Saved invalid controller designs must recover to Frost and retain application restrictions");
    }
    for (const char* invalid : {"-2147483648", "-1", "3", "2147483647", "2147483648", "-2147483649", "1.5", "invalid", ""}) {
        WriteBytes(file, std::string("[General]\nstyle=5\ndevice=1\ncontrollerLayout=1\nenabled=0\nonlySelectedApps=1\n") +
            "controllerStyle=2\ncontrollerStyle=" + invalid + "\nscale=75\n");
        Check(input_overlay::LoadSettings(file, settings) && settings.controllerStyle == input_overlay::ControllerStyle::Frost &&
            settings.style == input_overlay::OverlayStyle::Gradient && settings.device == input_overlay::OverlayDevice::Controller &&
            settings.controllerLayout == input_overlay::ControllerLayout::PlayStation &&
            !settings.enabled && settings.onlySelectedApps && settings.scale == 75,
            "Malformed stored controller designs must restore Frost without changing other saved preferences");
    }
    for (const int device : {0, 1}) {
        WriteBytes(file, "[General]\nversion=1\nstyle=4\ncontrollerLayout=1\ncontrollerIndex=3\ncontrollerDeadzone=27\n"
            "enabled=0\nonlySelectedApps=1\nstartMinimized=1\nscale=75\ndevice=" + std::to_string(device) + "\n");
        settings.controllerStyle = input_overlay::ControllerStyle::Prism;
        Check(input_overlay::LoadSettings(file, settings) && settings.controllerStyle == input_overlay::ControllerStyle::Frost &&
            static_cast<int>(settings.device) == device && settings.controllerLayout == input_overlay::ControllerLayout::PlayStation &&
            settings.controllerIndex == 3 && settings.controllerDeadzone == 27 && settings.style == input_overlay::OverlayStyle::Pearl &&
            !settings.enabled && settings.onlySelectedApps && settings.startMinimized && settings.scale == 75,
            "Existing settings without a controller design must gain Frost while preserving their saved mode and preferences");
    }
}

void TestControllerAccent(const TemporaryDirectory& temp) {
    const auto file = temp.File(L"controller-accent.ini");
    const COLORREF sky = RGB(125, 211, 252);
    auto settings = input_overlay::DefaultSettings();
    Check(settings.controllerAccent == CLR_INVALID && settings.accent == sky,
        "New controller accents must use their automatic style color independently of the keyboard accent");
    settings.accent = RGB(71, 122, 193);
    settings.style = input_overlay::OverlayStyle::Pearl;
    settings.device = input_overlay::OverlayDevice::Controller;
    settings.enabled = false;
    settings.onlySelectedApps = true;
    settings.startMinimized = true;
    settings.applications = {temp.File(L"Absent Game\\game.exe")};
    settings.colorTheme = input_overlay::ColorTheme::Custom;
    settings.backgroundStart = RGB(11, 64, 127);
    settings.backgroundEnd = RGB(238, 142, 37);
    settings.gradientFillOpacity = 21;
    for (int style = 0; style < input_overlay::ControllerStyleCount; ++style) {
        settings.controllerStyle = static_cast<input_overlay::ControllerStyle>(style);
        for (const COLORREF accent : {COLORREF{CLR_INVALID}, sky, RGB(0, 0, 0), RGB(255, 255, 255),
            RGB(162, 246, 218), RGB(102, 209, 255), RGB(219, 63, 129)}) {
            settings.controllerAccent = accent;
            Check(input_overlay::SaveSettings(file, settings), "Automatic and explicit controller accents must save");
            input_overlay::Settings restored;
            Check(input_overlay::LoadSettings(file, restored) && restored.controllerAccent == accent &&
                restored.accent == settings.accent && restored.controllerStyle == settings.controllerStyle &&
                restored.style == settings.style && restored.device == settings.device,
                "Controller accents must round trip exactly without changing either device's selected style or keyboard accent");
            const COLORREF expectedColor = accent != CLR_INVALID ? accent :
                restored.controllerStyle == input_overlay::ControllerStyle::Air ? RGB(162, 246, 218) : RGB(102, 209, 255);
            Check(input_overlay::ControllerAccentColor(restored) == expectedColor,
                "Automatic controller colors must match each design while explicit colors, including sky, remain literal");
            Check(!restored.enabled && restored.onlySelectedApps && restored.startMinimized &&
                restored.applications == settings.applications && restored.colorTheme == settings.colorTheme &&
                restored.backgroundStart == settings.backgroundStart && restored.backgroundEnd == settings.backgroundEnd &&
                restored.gradientFillOpacity == settings.gradientFillOpacity,
                "Controller accents must preserve overlay visibility, application restrictions and custom gradient colors");
        }
    }
    for (const COLORREF invalid : {COLORREF{0x01000000}, COLORREF{0x80000000}, COLORREF{0xfffffffe}}) {
        settings.controllerAccent = invalid;
        input_overlay::NormalizeSettings(settings);
        Check(settings.controllerAccent == CLR_INVALID && settings.accent == RGB(71, 122, 193),
            "Reserved controller color bits must recover to automatic style color without altering keyboard accent");
        settings.controllerAccent = invalid;
        Check(input_overlay::SaveSettings(file, settings) && settings.controllerAccent == invalid,
            "Saving invalid controller accents must normalize a copy");
        input_overlay::Settings restored;
        Check(input_overlay::LoadSettings(file, restored) && restored.controllerAccent == CLR_INVALID &&
            restored.accent == settings.accent,
            "Saved invalid controller accents must remain automatic instead of migrating the keyboard accent");
    }
    for (const char* invalid : {"-2", "-2147483648", "16777216", "2147483647", "2147483648", "4294967295", "invalid", "1.5", ""}) {
        WriteBytes(file, std::string("[General]\naccent=197121\ncontrollerAccent=255\ncontrollerAccent=") + invalid +
            "\ncontrollerStyle=2\ncolorTheme=3\nbackgroundStart=123456\nbackgroundEnd=654321\nenabled=0\nonlySelectedApps=1\n");
        Check(input_overlay::LoadSettings(file, settings) && settings.controllerAccent == CLR_INVALID &&
            settings.accent == RGB(1, 2, 3) && settings.controllerStyle == input_overlay::ControllerStyle::Prism &&
            settings.colorTheme == input_overlay::ColorTheme::Ocean && settings.backgroundStart == 123456 &&
            settings.backgroundEnd == 654321 && !settings.enabled && settings.onlySelectedApps,
            "Malformed controller accents must suppress migration, recover automatic color and preserve unrelated preferences");
    }
    for (const COLORREF legacyAccent : {sky, RGB(0, 0, 0), RGB(255, 255, 255), RGB(219, 63, 129)}) {
        WriteBytes(file, "[General]\ncontrollerStyle=0\naccent=" + std::to_string(legacyAccent) +
            "\nstyle=4\nenabled=0\nonlySelectedApps=1\n");
        const COLORREF migrated = legacyAccent == sky ? CLR_INVALID : legacyAccent;
        Check(input_overlay::LoadSettings(file, settings) && settings.controllerAccent == migrated && settings.accent == legacyAccent &&
            settings.controllerStyle == input_overlay::ControllerStyle::Air && settings.style == input_overlay::OverlayStyle::Pearl &&
            !settings.enabled && settings.onlySelectedApps,
            "Legacy settings must migrate custom accents while allowing the old default sky to use the new design's default");
        Check(input_overlay::SaveSettings(file, settings) && input_overlay::LoadSettings(file, settings) &&
            settings.controllerAccent == migrated, "Migrated controller colors must remain stable after saving and reopening");
    }
    for (const bool keyFirst : {false, true}) {
        const std::string explicitSky = "controllerAccent=" + std::to_string(sky) + "\n";
        const std::string keyboardAccent = "accent=197121\n";
        WriteBytes(file, "[General]\n" + (keyFirst ? explicitSky + keyboardAccent : keyboardAccent + explicitSky));
        Check(input_overlay::LoadSettings(file, settings) && settings.controllerAccent == sky && settings.accent == RGB(1, 2, 3),
            "An explicitly selected sky controller color must remain literal regardless of stored key order");
    }
    WriteBytes(file, "[General]\ncontrollerAccent=-1\naccent=197121\n");
    Check(input_overlay::LoadSettings(file, settings) && settings.controllerAccent == CLR_INVALID && settings.accent == RGB(1, 2, 3),
        "The explicit automatic sentinel must suppress legacy custom-accent migration");
    WriteBytes(file, "[General]\nenabled=0\nonlySelectedApps=1\n");
    Check(input_overlay::LoadSettings(file, settings) && settings.controllerAccent == CLR_INVALID && settings.accent == sky &&
        !settings.enabled && settings.onlySelectedApps,
        "Legacy settings without either accent must retain automatic controller color and existing visibility preferences");
    settings.controllerAccent = RGB(219, 63, 129);
    Check(!input_overlay::LoadSettings(temp.File(L"missing-controller-accent.ini"), settings) && settings.controllerAccent == CLR_INVALID,
        "A new installation must use automatic controller colors");
}

void TestUpdatePreferences(const TemporaryDirectory& temp) {
    const auto file = temp.File(L"updates.ini");
    auto settings = input_overlay::DefaultSettings();
    Check(settings.autoCheckUpdates, "New installations must check for updates at startup by default");
    settings.enabled = false;
    settings.onlySelectedApps = true;
    settings.startMinimized = true;
    settings.device = input_overlay::OverlayDevice::Controller;
    settings.applications = {temp.File(L"Absent Game\\game.exe")};
    input_overlay::BindInput(settings, SideSlot, 'Q');
    for (const bool enabled : {false, true}) {
        settings.autoCheckUpdates = enabled;
        Check(input_overlay::SaveSettings(file, settings), "Update check preference must save");
        input_overlay::Settings restored;
        restored.autoCheckUpdates = !enabled;
        Check(input_overlay::LoadSettings(file, restored) && restored.autoCheckUpdates == enabled,
            "Both automatic update check preferences must round trip through a fresh load");
        Check(!restored.enabled && restored.onlySelectedApps && restored.startMinimized &&
            restored.device == input_overlay::OverlayDevice::Controller && restored.applications == settings.applications &&
            restored.slots[SideSlot].input == 'Q' && restored.slots[QSlot].input == 0,
            "Changing update preferences must preserve overlay state, applications, device and input bindings");
    }
    for (const char* invalid : {"-1", "2", "true", "2147483648", ""}) {
        WriteBytes(file, std::string("[General]\nversion=1\nenabled=0\nonlySelectedApps=1\nautoCheckUpdates=") + invalid + "\n");
        settings.autoCheckUpdates = false;
        Check(input_overlay::LoadSettings(file, settings) && settings.autoCheckUpdates &&
            !settings.enabled && settings.onlySelectedApps,
            "Invalid automatic update booleans must preserve the default and other saved preferences");
    }
    WriteBytes(file, "[General]\nversion=1\nenabled=0\nstartMinimized=1\nscale=75\n");
    settings.autoCheckUpdates = false;
    Check(input_overlay::LoadSettings(file, settings) && settings.autoCheckUpdates &&
        !settings.enabled && settings.startMinimized && settings.scale == 75,
        "Legacy files must enable startup update checks without changing previous overlay preferences");
    settings.autoCheckUpdates = false;
    Check(!input_overlay::LoadSettings(temp.File(L"new-update-install.ini"), settings) && settings.autoCheckUpdates,
        "A new installation without a settings file must retain the default update preference");
}

void TestMalformed(const TemporaryDirectory& temp) {
    const auto file = temp.File(L"malformed.ini");
    WriteBytes(file,
        "[General]\nscale=999999\nopacity=-10\nx=2147483648\ny=-2147483648\nenabled=not-a-bool\n"
        "onlySelectedApps=1\nhotkeyVk=160\nhotkeyModifiers=-1\nunknown=arbitrary\n"
        "[Slot7]\ninput=81\nvisible=2\nlabel=bad\\q\n"
        "[Slot8]\ninput=81\n[Slot9]\ninput=2147483647\n[Slot34]\ninput=0\n"
        "[Slot9999999999999]\ninput=42\n[Applications]\npath0=relative.exe\n");
    input_overlay::Settings settings;
    Check(input_overlay::LoadSettings(file, settings), "Malformed entries should not reject readable config");
    Check(settings.scale == 200 && settings.opacity == 15, "Numeric bounds must normalize");
    Check(settings.x == 32 && settings.y == 0, "Overflow must be ignored and negative margins clamped");
    Check(settings.enabled && settings.slots[QSlot].visible, "Malformed booleans must preserve defaults");
    Check(settings.slots[QSlot].label == L"Q", "Malformed escaped label must preserve default");
    Check(settings.slots[QSlot].input == 'Q' && settings.slots[8].input == 0, "Duplicate stored bindings must be removed");
    Check(settings.slots[9].input == 'E' && settings.slots[34].input == 0, "Invalid stored input should preserve default, zero should unbind");
    Check(settings.hotkeyVk == VK_F10 && settings.hotkeyModifiers == (MOD_CONTROL | MOD_ALT), "Invalid hotkey must recover");
    Check(settings.onlySelectedApps && settings.applications.empty(), "Malformed app list must keep restrictive mode");
    Check(!input_overlay::MatchesApplication(settings, L"C:\\game.exe"), "Corrupt allowlist must not permit all apps");
    WriteBytes(file, std::string("\xff\xff", 2));
    Check(!input_overlay::LoadSettings(file, settings) && settings.slots[QSlot].input == 'Q', "Invalid Unicode must fail safely with defaults");
    const std::wstring utf16 = L"[General]\nscale=125\n[Slot0]\nlabel=\u9375\n";
    std::string bytes("\xff\xfe", 2);
    for (wchar_t c : utf16) { bytes += static_cast<char>(c & 255); bytes += static_cast<char>((c >> 8) & 255); }
    WriteBytes(file, bytes);
    Check(input_overlay::LoadSettings(file, settings) && settings.scale == 125 && settings.slots[0].label == L"\u9375", "UTF-16 LE fixture must load");
    Check(!input_overlay::LoadSettings(temp.File(L"does-not-exist.ini"), settings) && settings.scale == 100, "Missing config must leave complete defaults");
}
}

int main() {
    try {
        TemporaryDirectory temp;
        TestRemapping();
        TestNormalization();
        TestApplicationMatching();
        TestGeometry();
        TestControllerScale(temp);
        TestPersistence(temp);
        TestStyles(temp);
        TestColorSettings(temp);
        TestGradientPalettes();
        TestColorHex();
        TestStartupState(temp);
        TestControllerSettings(temp);
        TestControllerStyles(temp);
        TestControllerAccent(temp);
        TestUpdatePreferences(temp);
        TestMalformed(temp);
        Check(!input_overlay::ExecutablePath().empty(), "Executable path must resolve");
        Check(input_overlay::ProcessPath(nullptr).empty(), "Null foreground window must resolve safely");
        std::puts("All core tests passed.");
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
