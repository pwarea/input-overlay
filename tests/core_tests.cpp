#include "app.hpp"
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
    settings.x = INT_MIN; settings.y = INT_MAX; settings.scale = -1; settings.opacity = 1000;
    settings.hotkeyVk = VK_RSHIFT; settings.hotkeyModifiers = 0xffffffff;
    settings.slots[1].input = VK_LSHIFT;
    settings.slots[2].input = VK_RSHIFT;
    settings.slots[3].input = -8;
    settings.slots[4].label = L"Some\r\nlabel\t";
    settings.slots[5].label = std::wstring(100, L'W');
    settings.applications = {L"C:\\Games\\game.exe", L"c:/games/GAME.exe", L"game.exe", L"D:\\Other\\app.exe"};
    input_overlay::NormalizeSettings(settings);
    Check(settings.x == 0 && settings.y == 32767, "Anchor margins must be nonnegative and bounded");
    Check(settings.scale == 10 && settings.opacity == 100, "Visual bounds must clamp");
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
        input_overlay::OverlayStyle::Glass, input_overlay::OverlayStyle::Circuit, input_overlay::OverlayStyle::Pearl};
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
    for (const char* invalid : {"-1", "5", "2147483648", "invalid", ""}) {
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
    for (int oldId = 0; oldId < 4; ++oldId) {
        WriteBytes(file, "[General]\nversion=1\nstyle=" + std::to_string(oldId) + "\n");
        Check(input_overlay::LoadSettings(file, legacy) && legacy.style == styles[oldId],
            "Saved pre-Pearl style IDs must retain their original appearance");
    }
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
    settings.applications = {temp.File(L"Absent Game\\game.exe")};
    input_overlay::BindInput(settings, SideSlot, 'Q');
    for (const int index : {-1, 0, 1, 2, 3}) for (const int deadzone : {0, 15, 40}) {
        settings.controllerIndex = index;
        settings.controllerDeadzone = deadzone;
        Check(input_overlay::SaveSettings(file, settings), "Controller settings must save");
        input_overlay::Settings restored;
        Check(input_overlay::LoadSettings(file, restored) && restored.device == input_overlay::OverlayDevice::Controller &&
            restored.controllerLayout == input_overlay::ControllerLayout::PlayStation && restored.controllerIndex == index &&
            restored.controllerDeadzone == deadzone, "Controller layout, player and deadzone must round trip");
        Check(!restored.enabled && restored.onlySelectedApps && restored.applications == settings.applications &&
            restored.slots[SideSlot].input == 'Q' && restored.slots[QSlot].input == 0,
            "Controller preferences must not erase restrictions, disabled state or keyboard remaps");
    }
    for (const int invalid : {-100, 4, INT_MAX}) {
        settings.device = static_cast<input_overlay::OverlayDevice>(invalid);
        settings.controllerLayout = static_cast<input_overlay::ControllerLayout>(invalid);
        settings.controllerIndex = invalid;
        settings.controllerDeadzone = invalid;
        input_overlay::NormalizeSettings(settings);
        Check(settings.device == input_overlay::OverlayDevice::KeyboardMouse && settings.controllerLayout == input_overlay::ControllerLayout::Xbox &&
            settings.controllerIndex == -1 && settings.controllerDeadzone >= 0 && settings.controllerDeadzone <= 40,
            "Invalid in-memory controller settings must normalize safely");
    }
    for (const char* invalid : {"-100", "4", "2147483648", "invalid", ""}) {
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
        TestPersistence(temp);
        TestStyles(temp);
        TestStartupState(temp);
        TestControllerSettings(temp);
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
