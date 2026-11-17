#include <windows.h>
#include <array>
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include "controller.hpp"
#include "updates.hpp"
#define private public
#include "app.hpp"
#undef private
#include <commctrl.h>
#include <cstdio>
#include <new>
#include <stdexcept>
#include <string>

namespace {
HWND foregroundForTest = nullptr;
HWND ForegroundForTest() { return foregroundForTest; }
}

#define GetForegroundWindow ForegroundForTest
#include "../src/main.cpp"
#include "../src/settings.cpp"
#undef GetForegroundWindow

namespace {
using namespace input_overlay;

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

std::string ReadConfiguration(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "Saved configuration must open");
    const DWORD size = GetFileSize(file, nullptr);
    if (size == INVALID_FILE_SIZE || size > 1024 * 1024) { CloseHandle(file); throw std::runtime_error("Saved configuration size must be bounded"); }
    std::string content(size, '\0');
    DWORD read = 0;
    const BOOL result = ReadFile(file, content.data(), size, &read, nullptr);
    CloseHandle(file);
    Check(result && read == size, "Saved configuration must read completely");
    return content;
}

void Pump() {
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void Focus(HWND window) {
    const HWND previous = foregroundForTest;
    foregroundForTest = window;
    ShowWindow(window, SW_RESTORE);
    if (previous && previous != window) SendMessageW(previous, WM_ACTIVATE, WA_INACTIVE, reinterpret_cast<LPARAM>(window));
    SendMessageW(window, WM_ACTIVATE, WA_ACTIVE, reinterpret_cast<LPARAM>(previous));
    Pump();
}

struct Desktop {
    HDESK original = GetThreadDesktop(GetCurrentThreadId());
    HDESK handle = nullptr;
    std::wstring name = L"InputOverlayVisibility-" + std::to_wstring(GetCurrentProcessId());
    Desktop() {
        handle = CreateDesktopW(name.c_str(), nullptr, nullptr, 0, GENERIC_ALL, nullptr);
        Check(handle != nullptr, "Isolated test desktop must be created");
        Check(SetThreadDesktop(handle) != FALSE, "Test thread must use its isolated desktop");
    }
    ~Desktop() {
        SetThreadDesktop(original);
        if (handle) CloseDesktop(handle);
    }
};

struct TemporaryDirectory {
    std::wstring path;
    TemporaryDirectory() {
        wchar_t root[MAX_PATH + 1]{};
        Check(GetTempPathW(MAX_PATH, root) != 0, "Temporary directory must resolve");
        path = std::wstring(root) + L"InputOverlayVisibility-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        Check(CreateDirectoryW(path.c_str(), nullptr) != FALSE, "Temporary directory must be created");
    }
    ~TemporaryDirectory() {
        DeleteFileW((path + L"\\Input Overlay.ini").c_str());
        DeleteFileW((path + L"\\allowed.exe").c_str());
        RemoveDirectoryW(path.c_str());
    }
};

LRESULT CALLBACK FixtureProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(window, message, wParam, lParam);
}

HWND CreateFixture(const wchar_t* title) {
    WNDCLASSW wc{};
    wc.lpfnWndProc = FixtureProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"InputOverlay.VisibilityFixture";
    if (!RegisterClassW(&wc)) Check(GetLastError() == ERROR_CLASS_ALREADY_EXISTS, "Fixture class must register");
    HWND window = CreateWindowW(wc.lpszClassName, title, WS_OVERLAPPEDWINDOW,
        20, 20, 320, 240, nullptr, nullptr, wc.hInstance, nullptr);
    Check(window != nullptr, "Fixture window must be created");
    return window;
}

struct AllowedProcess {
    HANDLE process = nullptr;
    HWND window = nullptr;
    std::wstring path;
    AllowedProcess(const Desktop& desktop, const TemporaryDirectory& directory) {
        path = directory.path + L"\\allowed.exe";
        Check(CopyFileW(ExecutablePath().c_str(), path.c_str(), TRUE) != FALSE, "Allowlisted fixture executable must be copied");
        std::wstring command = L"\"" + path + L"\" --fixture";
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.lpDesktop = const_cast<wchar_t*>(desktop.name.c_str());
        PROCESS_INFORMATION child{};
        Check(CreateProcessW(path.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
            nullptr, directory.path.c_str(), &startup, &child) != FALSE, "Allowlisted fixture process must start");
        process = child.hProcess;
        CloseHandle(child.hThread);
        const auto deadline = GetTickCount64() + 10000;
        while (GetTickCount64() < deadline) {
            struct Search { DWORD pid; HWND window; } search{child.dwProcessId, nullptr};
            EnumWindows([](HWND candidate, LPARAM data) -> BOOL {
                auto& search = *reinterpret_cast<Search*>(data);
                DWORD pid = 0;
                GetWindowThreadProcessId(candidate, &pid);
                if (pid == search.pid && IsWindowVisible(candidate)) { search.window = candidate; return FALSE; }
                return TRUE;
            }, reinterpret_cast<LPARAM>(&search));
            window = search.window;
            if (window) return;
            Check(WaitForSingleObject(process, 20) == WAIT_TIMEOUT, "Allowlisted fixture must remain running");
        }
        throw std::runtime_error("Allowlisted fixture window did not appear");
    }
    ~AllowedProcess() {
        if (window) PostMessageW(window, WM_CLOSE, 0, 0);
        if (process) {
            if (WaitForSingleObject(process, 5000) == WAIT_TIMEOUT) {
                TerminateProcess(process, 1);
                WaitForSingleObject(process, 1000);
            }
            CloseHandle(process);
        }
    }
};

struct Harness {
    Application app;
    HWND outside = nullptr;
    Harness(const TemporaryDirectory& directory, const AllowedProcess& allowed) {
        app.configPath = directory.path + L"\\Input Overlay.ini";
        app.settings.onlySelectedApps = true;
        app.settings.applications = {allowed.path};
        app.settings.autoCheckUpdates = false;
        app.controller = CreateWindowW(L"STATIC", L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
        Check(app.controller != nullptr, "Message-only test controller must be created");
        Check(app.overlay.Create(GetModuleHandleW(nullptr), nullptr), "Test overlay must be created");
        outside = CreateFixture(L"Unrelated application");
        Check(app.preferences.Show(app), "Settings window must open");
        Focus(app.preferences.Handle());
        app.UpdateVisibility();
    }
    ~Harness() {
        app.Shutdown();
        if (outside) DestroyWindow(outside);
    }
    void Expect(bool visible, bool editing, const char* message) {
        Check(app.Visible() == visible && (IsWindowVisible(app.overlay.Handle()) != FALSE) == visible &&
            app.overlay.Editing() == editing, message);
    }
    void BeginPreview() {
        Focus(app.preferences.Handle());
        app.EditPosition(true);
        Expect(true, true, "Position preview must appear while Settings is active");
    }
};

void TestKeyboardForms(Harness& harness) {
    auto& app = harness.app;
    const Settings original = app.settings;
    const HWND window = app.preferences.Handle();
    const auto click = [window](int id) { SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0); };
    const auto chooseLayout = [window](bool iso) {
        Check(SendDlgItemMessageW(window, Layout, CB_SETCURSEL, iso ? 1 : 0, 0) != CB_ERR,
            "ANSI and ISO must remain available beside keyboard form controls");
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(Layout, CBN_SELCHANGE), 0);
    };
    const auto slotFor = [](int input) {
        const auto& definitions = KeyboardSlotDefinitions();
        for (size_t slot = 0; slot < definitions.size(); ++slot) if (definitions[slot].input == input) return slot;
        throw std::runtime_error("Expected physical keyboard input must have a stable slot");
    };
    const auto listSlots = [window]() {
        const HWND list = GetDlgItem(window, SlotList);
        Check(list != nullptr, "Bindings list must exist");
        const LRESULT count = SendMessageW(list, LB_GETCOUNT, 0, 0);
        Check(count > 0 && count <= static_cast<LRESULT>(SlotCount), "Bindings list must have a bounded nonempty set of slots");
        std::vector<size_t> slots;
        for (LRESULT row = 0; row < count; ++row) {
            const LRESULT slot = SendMessageW(list, LB_GETITEMDATA, static_cast<WPARAM>(row), 0);
            Check(slot >= 0 && slot < static_cast<LRESULT>(SlotCount), "Each binding row must carry a valid stable slot identifier");
            Check(std::find(slots.begin(), slots.end(), static_cast<size_t>(slot)) == slots.end(), "Bindings must not list the same physical element twice");
            slots.push_back(static_cast<size_t>(slot));
        }
        return slots;
    };
    const auto selectSlot = [&](size_t slot) {
        const auto slots = listSlots();
        const auto found = std::find(slots.begin(), slots.end(), slot);
        Check(found != slots.end(), "Requested binding must be present in the selected keyboard form");
        const size_t row = static_cast<size_t>(found - slots.begin());
        SendDlgItemMessageW(window, SlotList, LB_SETCURSEL, row, 0);
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(SlotList, LBN_SELCHANGE), 0);
        Check(ui.selectedSlot == static_cast<int>(slot), "Selecting a binding row must resolve its stable slot, not the row index");
        return row;
    };
    Focus(window);
    app.settings.device = OverlayDevice::KeyboardMouse;
    app.settings.showMouse = false;
    app.Changed();
    const size_t ySlot = slotFor('Y'), deleteSlot = slotFor(VK_DELETE), functionSlot = slotFor(VK_F5), numpadSlot = slotFor(VK_NUMPAD1);
    const int keyCounts[] = {27, 61, 87, 104};
    for (const bool hidden : {false, true}) {
        app.settings.enabled = !hidden;
        app.Changed();
        for (int form = 0; form < KeyboardFormCount; ++form) for (const bool iso : {false, true}) {
            click(NavLayout);
            RECT previous{};
            for (int index = 0; index < KeyboardFormCount; ++index) {
                const HWND control = GetDlgItem(window, KeyboardFormFirst + index);
                Check(control != nullptr && IsWindowVisible(control), "All four keyboard form buttons must be visible together");
                RECT bounds{};
                Check(GetWindowRect(control, &bounds) != FALSE && bounds.right > bounds.left && bounds.bottom > bounds.top,
                    "Keyboard form button bounds must be valid");
                if (index) Check(bounds.top == previous.top && bounds.bottom == previous.bottom && bounds.left > previous.right,
                    "Keyboard form buttons must be side by side without overlap");
                previous = bounds;
            }
            chooseLayout(iso);
            click(KeyboardFormFirst + form);
            Check(static_cast<int>(app.settings.keyboardForm) == form && app.settings.isoLayout == iso &&
                app.settings.enabled == !hidden && app.settings.onlySelectedApps && app.settings.applications == original.applications &&
                app.settings.scale == original.scale && app.settings.controllerScale == original.controllerScale &&
                app.settings.controllerLayout == original.controllerLayout && app.settings.style == original.style,
                "Changing keyboard form must immediately preserve layout, visibility, filter, scales and visual style");
            Settings saved;
            Check(LoadSettings(app.configPath, saved) && saved.keyboardForm == app.settings.keyboardForm && saved.isoLayout == iso &&
                saved.enabled == !hidden && saved.onlySelectedApps && saved.applications == original.applications,
                "Keyboard form and ANSI or ISO choice must persist without changing visibility restrictions");
            harness.Expect(false, false, "Changing keyboard form must not reveal a hidden or application-filtered overlay");
            click(NavBindings);
            const auto slots = listSlots();
            Check(slots.size() == static_cast<size_t>(keyCounts[form] + (iso ? 1 : 0)) + MouseSlotCount,
                "Bindings must list only physical keys in the selected form plus all mouse bindings");
            const auto contains = [&slots](size_t slot) { return std::find(slots.begin(), slots.end(), slot) != slots.end(); };
            Check(contains(19) == iso, "The ISO-only key must be excluded from ANSI bindings");
            Check(contains(ySlot) == (form > 0), "Gaming must exclude keys outside the existing left-side layout");
            Check(contains(deleteSlot) == (form >= 2) && contains(functionSlot) == (form >= 2),
                "Navigation and function keys must appear only in TKL and full-size layouts");
            Check(contains(numpadSlot) == (form == 3), "Numpad bindings must appear only in the full-size keyboard");
            for (size_t mouse = 0; mouse < MouseSlotCount; ++mouse)
                Check(slots[slots.size() - MouseSlotCount + mouse] == MouseSlotFirst + mouse,
                    "All seven stable mouse slots must follow keyboard keys even when the mouse is hidden");
        }
    }
    click(OpenKeyboardLayout);
    chooseLayout(false);
    click(KeyboardFormFirst + static_cast<int>(KeyboardForm::FullSize));
    click(NavBindings);
    selectSlot(ySlot);
    SetDlgItemTextW(window, LabelEdit, L"Custom Y");
    click(SaveLabel);
    click(RecordInput);
    app.preferences.CaptureInput(VK_F24);
    Check(!app.preferences.Capturing() && app.settings.slots[ySlot].input == VK_F24,
        "Input recording must target an appended keyboard slot");
    SendDlgItemMessageW(window, SlotVisible, BM_SETCHECK, BST_UNCHECKED, 0);
    click(SlotVisible);
    const size_t mouseSide = MouseSlotFirst + 3;
    Check(selectSlot(mouseSide) != mouseSide, "Full-size mouse binding row must differ from its legacy slot identifier");
    SetDlgItemTextW(window, LabelEdit, L"Side Q");
    click(SaveLabel);
    click(RecordInput);
    app.preferences.CaptureInput('Q');
    Check(app.settings.slots[mouseSide].input == 'Q' && app.settings.slots[7].input == InputNone &&
        app.settings.slots[mouseSide].label == L"Side Q", "Recording Q on a mouse row must affect its stable mouse slot and remove the prior Q assignment");
    selectSlot(ySlot);
    click(RecordInput);
    Check(app.preferences.Capturing(), "Recording must start before changing keyboard layout");
    click(OpenKeyboardLayout);
    Check(!app.preferences.Capturing(), "Opening keyboard layout must cancel an in-progress input recording");
    for (int form = 0; form < KeyboardFormCount; ++form) {
        click(KeyboardFormFirst + form);
        Check(app.settings.slots[ySlot].input == VK_F24 && app.settings.slots[ySlot].label == L"Custom Y" &&
            !app.settings.slots[ySlot].visible && app.settings.slots[mouseSide].input == 'Q' && app.settings.slots[mouseSide].label == L"Side Q",
            "Switching physical keyboard forms must preserve hidden custom bindings and stable mouse assignments");
    }
    click(NavBindings);
    selectSlot(ySlot);
    click(UnbindInput);
    Check(app.settings.slots[ySlot].input == InputNone && app.settings.slots[ySlot].label == L"Custom Y" && !app.settings.slots[ySlot].visible,
        "Unbinding an appended key must preserve its label and visibility");
    click(ResetBinding);
    const Slot defaultY = DefaultSettings().slots[ySlot];
    Check(app.settings.slots[ySlot].input == defaultY.input && app.settings.slots[ySlot].label == defaultY.label &&
        app.settings.slots[ySlot].visible && app.settings.slots[mouseSide].input == 'Q',
        "Reset to default must restore only the selected appended key");
    for (const int physical : {VK_RCONTROL, NumpadEnter, VK_NUMPAD6}) {
        const size_t slot = slotFor(physical);
        selectSlot(slot);
        click(RecordInput);
        app.DeviceInput(INVALID_HANDLE_VALUE, physical, true);
        Check(app.settings.slots[slot].input == physical && !app.preferences.Capturing() &&
            std::none_of(app.pressed.begin(), app.pressed.end(), [](bool down) { return down; }),
            "Recording right Ctrl or a numpad key must retain its physical identity and clear pressed state after saving");
        const WPARAM key = physical == VK_RCONTROL ? VK_CONTROL : physical == NumpadEnter ? VK_RETURN : VK_RIGHT;
        const LPARAM flags = physical == VK_RCONTROL ? 0x011d0000 : physical == NumpadEnter ? 0x011c0000 : 0x004d0000;
        Check(app.captureIgnoreKey_ == static_cast<UINT>(physical) && IsCapturedKeyboardMessage(app.captureIgnoreKey_, key, flags) &&
            IsCapturedKeyboardMessage(app.captureIgnoreKey_, key, flags | static_cast<LPARAM>(0xc0000000u)),
            "After recording, both translated Windows key transitions must be consumed using the captured physical identity");
        if (physical == VK_NUMPAD6) Check(!IsCapturedKeyboardMessage(app.captureIgnoreKey_, VK_RIGHT, 0x014d0000),
            "Consuming captured numpad 6 must not consume the separate extended Right arrow key");
        Settings saved;
        Check(LoadSettings(app.configPath, saved) && saved.slots[slot].input == physical,
            "Physical modifier and numpad bindings must survive reloading saved settings");
    }
    click(OpenKeyboardLayout);
    click(KeyboardFormFirst + static_cast<int>(KeyboardForm::Gaming));
    click(NavBindings);
    Check(ui.selectedSlot == static_cast<int>(listSlots().front()), "Removing the selected key from a layout must select a present key");
    click(OpenKeyboardLayout);
    harness.BeginPreview();
    click(KeyboardFormFirst + static_cast<int>(KeyboardForm::Compact));
    harness.Expect(false, false, "Changing keyboard form must end position mode and restore application filtering");
    harness.BeginPreview();
    chooseLayout(true);
    harness.Expect(false, false, "Changing ANSI or ISO layout must end position mode and restore application filtering");
    app.settings = original;
    app.Changed();
    click(NavOverlay);
}

void TestPhysicalModifierState(Harness& harness) {
    auto& app = harness.app;
    const HANDLE first = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(101));
    const HANDLE second = reinterpret_cast<HANDLE>(static_cast<UINT_PTR>(202));
    const int modifiers[][3] = {{VK_LSHIFT, VK_RSHIFT, VK_SHIFT}, {VK_LCONTROL, VK_RCONTROL, VK_CONTROL}, {VK_LMENU, VK_RMENU, VK_MENU}};
    for (const auto& keys : modifiers) {
        app.ResetInputs();
        app.DeviceInput(first, keys[0], true);
        app.DeviceInput(first, keys[1], true);
        Check(app.pressed[keys[0]] && app.pressed[keys[1]] && app.pressed[keys[2]],
            "Left and right physical modifiers must both hold their shared legacy alias");
        app.DeviceInput(first, keys[0], false);
        Check(!app.pressed[keys[0]] && app.pressed[keys[1]] && app.pressed[keys[2]],
            "Releasing the left modifier must retain the right modifier and shared alias");
        app.DeviceInput(first, keys[1], false);
        Check(!app.pressed[keys[0]] && !app.pressed[keys[1]] && !app.pressed[keys[2]],
            "Releasing both modifier sides must clear their shared alias");
        app.DeviceInput(first, keys[0], true);
        app.DeviceInput(second, keys[0], true);
        app.DeviceInput(first, keys[0], false);
        Check(app.pressed[keys[0]] && app.pressed[keys[2]],
            "A modifier held on another input device must survive the first device release");
        app.DeviceInput(second, keys[0], false);
        Check(!app.pressed[keys[0]] && !app.pressed[keys[2]],
            "The final device release must clear both physical and generic modifier state");
    }
    app.ResetInputs();
    app.DeviceInput(first, VK_RETURN, true);
    app.DeviceInput(first, NumpadEnter, true);
    app.DeviceInput(first, VK_RETURN, false);
    Check(!app.pressed[VK_RETURN] && app.pressed[NumpadEnter], "Releasing main Enter must not release numpad Enter");
    app.DeviceInput(first, NumpadEnter, false);
    Check(!app.pressed[NumpadEnter], "Releasing numpad Enter must clear its independent state");
    app.ResetInputs();
}

void TestControllerAppearance(Harness& harness, HWND allowedWindow) {
    auto& app = harness.app;
    const Settings original = app.settings;
    const HWND window = app.preferences.Handle();
    const auto click = [window](int id) { SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0); };
    const auto select = [window](int id, int selection) {
        Check(GetDlgItem(window, id) != nullptr, "Controller appearance selector must exist");
        Check(SendDlgItemMessageW(window, id, CB_SETCURSEL, selection, 0) != CB_ERR,
            "Controller appearance selection must be available");
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, CBN_SELCHANGE), 0);
    };
    Focus(window);
    click(NavOverlay);
    select(AppearanceDevice, 0);
    click(StyleFirst + static_cast<int>(OverlayStyle::Pearl));
    Check(app.settings.device == OverlayDevice::KeyboardMouse, "Controller appearance tests must begin in keyboard and mouse mode");
    for (const bool manuallyHidden : {false, true}) {
        if (manuallyHidden) app.Toggle();
        for (int design = 0; design < ControllerStyleCount; ++design) for (int layout = 0; layout < ControllerLayoutCount; ++layout) {
            click(NavController);
            Check(GetDlgItem(window, OverlayPreview) && GetDlgItem(window, OpenAppearance) && GetDlgItem(window, ControllerConnection) &&
                !GetDlgItem(window, NavController) && !GetDlgItem(window, NavLayout),
                "Controller controls must share the Overlay page without separate sidebar entries");
            Check(SendDlgItemMessageW(window, ControllerLayoutChoice, CB_GETCOUNT, 0, 0) == ControllerLayoutCount,
                "Controller page must expose Xbox, DualSense and DualShock 4 layouts");
            const wchar_t* layoutNames[] = {L"Xbox", L"DualSense", L"DualShock 4"};
            wchar_t layoutLabel[64]{};
            Check(SendDlgItemMessageW(window, ControllerLayoutChoice, CB_GETLBTEXTLEN, layout, 0) ==
                static_cast<LRESULT>(std::wstring(layoutNames[layout]).size()) &&
                SendDlgItemMessageW(window, ControllerLayoutChoice, CB_GETLBTEXT, layout, reinterpret_cast<LPARAM>(layoutLabel)) != CB_ERR &&
                std::wstring(layoutLabel) == layoutNames[layout], "Each controller layout must have its correct label");
            select(ControllerLayoutChoice, layout);
            click(OpenAppearance);
            for (int id = ControllerStyleFirst; id <= ControllerStyleLast; ++id)
                Check(GetDlgItem(window, id) != nullptr, "Controller appearance must expose every design");
            click(ControllerStyleFirst + design);
            Check(static_cast<int>(app.settings.controllerStyle) == design && static_cast<int>(app.settings.controllerLayout) == layout &&
                app.settings.style == OverlayStyle::Pearl && app.settings.device == OverlayDevice::Controller,
                "Each controller design and layout must remain independent of keyboard style");
            Settings saved;
            Check(LoadSettings(app.configPath, saved) && saved.controllerStyle == app.settings.controllerStyle &&
                saved.controllerLayout == app.settings.controllerLayout && saved.style == OverlayStyle::Pearl &&
                saved.device == OverlayDevice::Controller && saved.enabled == !manuallyHidden &&
                saved.onlySelectedApps && saved.applications == original.applications,
                "Controller design and layout changes must persist without enabling a hidden overlay or erasing its filter");
            harness.Expect(false, false, "Controller appearance changes must preserve application filtering and manual hide");
            const std::string beforeColors = ReadConfiguration(app.configPath);
            click(PreviewPressed);
            click(PreviewBackground);
            click(PreviewIdle);
            Check(ReadConfiguration(app.configPath) == beforeColors,
                "Controller page preview controls must not save live settings");
            click(OpenAppearance);
            Check(SendDlgItemMessageW(window, AppearanceDevice, CB_GETCURSEL, 0, 0) == 1 &&
                GetDlgItem(window, ControllerStyleFirst) && !GetDlgItem(window, StyleFirst) &&
                app.settings.device == OverlayDevice::Controller && ReadConfiguration(app.configPath) == beforeColors,
                "The Appearance tab must preserve the selected device and saved preferences");
            Check(SendDlgItemMessageW(window, PreviewLayout, CB_GETCOUNT, 0, 0) == ControllerLayoutCount &&
                SendDlgItemMessageW(window, PreviewLayout, CB_GETCURSEL, 0, 0) == layout,
                "The appearance preview must restore every controller layout including DualShock 4");
            select(PreviewLayout, (layout + 1) % ControllerLayoutCount);
            click(PreviewPressed);
            click(PreviewBackground);
            click(PreviewIdle);
            select(AppearanceDevice, 0);
            Check(GetDlgItem(window, StyleFirst) && !GetDlgItem(window, ControllerStyleFirst) && app.settings.device == OverlayDevice::KeyboardMouse,
                "The unified device selector must activate keyboard mode and show its controls");
            select(PreviewLayout, original.isoLayout ? 0 : 1);
            select(AppearanceDevice, 1);
            Check(app.settings.controllerLayout == saved.controllerLayout && app.settings.isoLayout == original.isoLayout &&
                app.settings.controllerStyle == saved.controllerStyle && app.settings.style == OverlayStyle::Pearl &&
                app.settings.device == OverlayDevice::Controller && ReadConfiguration(app.configPath) == beforeColors,
                "Switching devices back must retain styles while preview layouts and states remain transient");
            Check(std::none_of(app.pressed.begin(), app.pressed.end(), [](bool down) { return down; }),
                "Controller appearance previews must not create live keyboard or mouse input");
            if (app.settings.controllerStyle == ControllerStyle::Prism) {
                Check(GetDlgItem(window, StartHex) && GetDlgItem(window, EndHex) && GetDlgItem(window, GradientFillControl),
                    "Prism must expose gradient colors and fill even when the keyboard style is Pearl");
                click(ThemeFirst + static_cast<int>(ColorTheme::Ocean));
                SetDlgItemTextW(window, StartHex, L"#225588");
                SetDlgItemTextW(window, EndHex, L"#EEAA66");
                const HWND fill = GetDlgItem(window, GradientFillControl);
                SendMessageW(fill, TBM_SETPOS, TRUE, 31);
                SendMessageW(window, WM_HSCROLL, TB_ENDTRACK, reinterpret_cast<LPARAM>(fill));
                Check(app.settings.colorTheme == ColorTheme::Custom && app.settings.backgroundStart == RGB(34, 85, 136) &&
                    app.settings.backgroundEnd == RGB(238, 170, 102) && app.settings.gradientFillOpacity == 31 &&
                    app.settings.style == OverlayStyle::Pearl && app.settings.device == OverlayDevice::Controller,
                    "Prism HEX and fill edits must apply while retaining keyboard style and live device");
            } else if (app.settings.controllerStyle == ControllerStyle::Original) {
                Check(GetDlgItem(window, OriginalOpacityControl) && GetDlgItem(window, OriginalSolid) &&
                    GetDlgItem(window, OriginalTransparent) && GetDlgItem(window, ResetOriginalOpacity) &&
                    !GetDlgItem(window, PressedColor) && !GetDlgItem(window, StartHex) && !GetDlgItem(window, GradientFillControl),
                    "Original must show opacity controls instead of color controls");
            } else {
                Check(GetDlgItem(window, PressedColor) && !GetDlgItem(window, StartHex) && !GetDlgItem(window, GradientFillControl),
                    "Air and Frost must expose their pressed color without Prism-only controls");
            }
            harness.Expect(false, false, "Controller color and preview edits must preserve filtering and manual hide");
        }
        const Settings beforeReset = app.settings;
        click(ResetControllerStyle);
        Check(app.settings.controllerStyle == ControllerStyle::Frost && app.settings.style == OverlayStyle::Pearl &&
            app.settings.device == OverlayDevice::Controller && app.settings.controllerLayout == beforeReset.controllerLayout &&
            app.settings.colorTheme == beforeReset.colorTheme && app.settings.backgroundStart == beforeReset.backgroundStart &&
            app.settings.backgroundEnd == beforeReset.backgroundEnd && app.settings.gradientFillOpacity == beforeReset.gradientFillOpacity &&
            app.settings.opacity == beforeReset.opacity && app.settings.originalOpacity == beforeReset.originalOpacity,
            "Reset style must restore only the controller design while retaining selected layout and custom colors");
        select(AppearanceDevice, 0);
        click(StyleFirst + static_cast<int>(OverlayStyle::Neon));
        Check(app.settings.style == OverlayStyle::Neon && app.settings.controllerStyle == ControllerStyle::Frost,
            "Changing keyboard style must not change the controller design");
        click(StyleFirst + static_cast<int>(OverlayStyle::Pearl));
        Check(app.settings.enabled == !manuallyHidden && app.settings.onlySelectedApps &&
            app.settings.applications == original.applications && app.settings.x == original.x && app.settings.y == original.y &&
            app.settings.scale == original.scale && app.settings.opacity == original.opacity && app.settings.monitor == original.monitor &&
            app.settings.anchorRight == original.anchorRight && app.settings.anchorBottom == original.anchorBottom,
            "Controller appearance controls must preserve visibility, application restrictions and placement");
        Focus(allowedWindow);
        app.UpdateVisibility();
        harness.Expect(!manuallyHidden, false, "An allowlisted window must still respect manual hide after controller appearance edits");
        Focus(window);
        app.UpdateVisibility();
        harness.Expect(false, false, "Returning to Settings must preserve the application filter after controller appearance edits");
    }
    app.settings = original;
    app.Changed();
    click(NavOverlay);
    select(AppearanceDevice, 0);
}

void TestOriginalOpacity(Harness& harness) {
    auto& app = harness.app;
    const Settings original = app.settings;
    const HWND window = app.preferences.Handle();
    const auto click = [window](int id) { SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0); };
    const auto slide = [window](int id, int value, int notification = TB_ENDTRACK) {
        const HWND slider = GetDlgItem(window, id);
        Check(slider != nullptr, "Opacity slider must exist");
        SendMessageW(slider, TBM_SETPOS, TRUE, value);
        SendMessageW(window, WM_HSCROLL, notification, reinterpret_cast<LPARAM>(slider));
    };
    app.settings.device = OverlayDevice::KeyboardMouse;
    app.settings.controllerStyle = ControllerStyle::Frost;
    app.settings.opacity = 73;
    app.settings.originalOpacity = 91;
    app.settings.enabled = false;
    app.Changed();
    Focus(window);
    click(NavController);
    click(OpenAppearance);
    click(ControllerStyleFirst + static_cast<int>(ControllerStyle::Original));
    Check(GetDlgItem(window, OriginalOpacityControl) && app.settings.originalOpacity == 91 && app.settings.opacity == 73 &&
        app.settings.device == OverlayDevice::Controller, "Selecting Original must preserve both opacity preferences and the active device");
    RECT previous{};
    for (int id = ControllerStyleFirst; id <= ControllerStyleLast; ++id) {
        RECT bounds{};
        Check(GetDlgItem(window, id) && GetWindowRect(GetDlgItem(window, id), &bounds), "All four controller style buttons must exist");
        Check(id == ControllerStyleFirst || previous.right < bounds.left, "Controller style buttons must not overlap");
        previous = bounds;
    }
    RECT appearance{};
    GetWindowRect(GetDlgItem(window, ResetControllerStyle), &appearance);
    Check(previous.right < appearance.left, "The fourth controller style must not overlap the reset button");
    Check(GetDlgItem(window, OriginalOpacityControl) && !GetDlgItem(window, PressedColor) &&
        SendDlgItemMessageW(window, OriginalOpacityControl, TBM_GETRANGEMIN, 0, 0) == 15 &&
        SendDlgItemMessageW(window, OriginalOpacityControl, TBM_GETRANGEMAX, 0, 0) == 100 &&
        Text(window, OriginalOpacityValue) == L"91%", "Original opacity editor must restore its own value and expose the full range");
    RECT reset{}, solid{}, transparent{}, slider{}, status{};
    GetWindowRect(GetDlgItem(window, ResetOriginalOpacity), &reset);
    GetWindowRect(GetDlgItem(window, OriginalSolid), &solid);
    GetWindowRect(GetDlgItem(window, OriginalTransparent), &transparent);
    GetWindowRect(GetDlgItem(window, OriginalOpacityControl), &slider);
    GetWindowRect(GetDlgItem(window, Status), &status);
    Check(reset.bottom < solid.top && solid.right < transparent.left && solid.bottom < slider.top && slider.bottom < status.top,
        "Original opacity presets, slider and footer must not overlap");
    const std::string beforeDrag = ReadConfiguration(app.configPath);
    slide(OriginalOpacityControl, 34, TB_THUMBTRACK);
    Check(app.settings.originalOpacity == 91 && app.settings.opacity == 73 && Text(window, OriginalOpacityValue) == L"34%" &&
        ReadConfiguration(app.configPath) == beforeDrag, "Dragging Original opacity must preview without saving partial values");
    slide(OriginalOpacityControl, 34);
    Check(app.settings.originalOpacity == 34 && app.settings.opacity == 73,
        "Releasing Original opacity must update only its independent preference");
    click(OriginalTransparent);
    Check(app.settings.originalOpacity == 55 && Text(window, OriginalOpacityValue) == L"55%" && app.settings.opacity == 73,
        "Transparent preset must set only Original opacity to 55 percent");
    click(OriginalSolid);
    Check(app.settings.originalOpacity == 100 && Text(window, OriginalOpacityValue) == L"100%",
        "Solid preset must set Original opacity to 100 percent");
    for (const int value : {15, 100, 48}) {
        slide(OriginalOpacityControl, value);
        Settings saved;
        Check(LoadSettings(app.configPath, saved) && saved.controllerStyle == ControllerStyle::Original &&
            saved.originalOpacity == value && saved.opacity == 73 && !saved.enabled && saved.onlySelectedApps &&
            saved.applications == original.applications && saved.device == OverlayDevice::Controller,
            "Original opacity must persist without changing global opacity, device, visibility or filters");
        harness.Expect(false, false, "Original opacity changes must preserve filtering and manual hide");
    }
    click(ResetOriginalOpacity);
    Check(app.settings.originalOpacity == 100 && app.settings.opacity == 73 && app.settings.controllerStyle == ControllerStyle::Original,
        "Reset opacity must restore only Original opacity to solid");
    slide(OriginalOpacityControl, 55);
    click(ResetControllerStyle);
    Check(app.settings.controllerStyle == ControllerStyle::Frost && app.settings.originalOpacity == 55 && app.settings.opacity == 73 &&
        GetDlgItem(window, PressedColor) && !GetDlgItem(window, OriginalOpacityControl),
        "Reset style must restore Frost while retaining independent opacity values");
    click(ControllerStyleFirst + static_cast<int>(ControllerStyle::Original));
    Check(Text(window, OriginalOpacityValue) == L"55%", "Returning to Original must restore its previous opacity");
    click(NavLayout);
    slide(Opacity, 62);
    Check(app.settings.opacity == 62 && app.settings.originalOpacity == 55,
        "The Layout opacity slider must retain its existing scope while Original is selected");
    click(NavController);
    click(OpenAppearance);
    click(ControllerStyleFirst + static_cast<int>(ControllerStyle::Prism));
    Check(GetDlgItem(window, StartHex) && app.settings.originalOpacity == 55 && app.settings.opacity == 62,
        "Other controller styles must restore Colors and preserve both opacity values");
    app.settings = original;
    app.Changed();
    click(NavOverlay);
}

void TestControllerInputSelection(Harness& harness) {
    auto& app = harness.app;
    const Settings original = app.settings;
    const HWND window = app.preferences.Handle();
    const auto click = [window](int id) { SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0); };
    app.settings.device = OverlayDevice::Controller;
    app.settings.controllerLayout = ControllerLayout::DualShock4;
    app.settings.controllerStyle = ControllerStyle::Prism;
    app.settings.scale = 135;
    app.settings.controllerScale = 75;
    app.settings.enabled = false;
    app.settings.x = 53;
    app.settings.y = 47;
    app.Changed();
    const Settings unchanged = app.settings;
    Focus(window);
    click(NavController);
    Check(SendDlgItemMessageW(window, ControllerIndex, CB_GETCOUNT, 0, 0) == ControllerSlotCount + 1,
        "Controller selection must expose Auto, four XInput slots and four native PlayStation slots");
    for (int index = -1; index < ControllerSlotCount; ++index) {
        const std::wstring expected = index < 0 ? L"Auto" : index < XInputSlotCount ? L"XInput " + std::to_wstring(index + 1) :
            L"PlayStation " + std::to_wstring(index - XInputSlotCount + 1);
        wchar_t label[64]{};
        Check(SendDlgItemMessageW(window, ControllerIndex, CB_GETLBTEXTLEN, index + 1, 0) == static_cast<LRESULT>(expected.size()) &&
            SendDlgItemMessageW(window, ControllerIndex, CB_GETLBTEXT, index + 1, reinterpret_cast<LPARAM>(label)) != CB_ERR &&
            label == expected, "Controller labels must distinguish XInput and native PlayStation slots");
        Check(SendDlgItemMessageW(window, ControllerIndex, CB_SETCURSEL, index + 1, 0) != CB_ERR,
            "Every controller input slot must be selectable");
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(ControllerIndex, CBN_SELCHANGE), 0);
        Settings saved;
        Check(app.settings.controllerIndex == index && LoadSettings(app.configPath, saved) && saved.controllerIndex == index,
            "Controller input selection must persist the original and native slot indices");
        Check(saved.device == unchanged.device && saved.controllerLayout == unchanged.controllerLayout &&
            saved.controllerStyle == unchanged.controllerStyle && saved.controllerScale == 75 && saved.scale == 135 &&
            !saved.enabled && saved.onlySelectedApps == unchanged.onlySelectedApps && saved.applications == unchanged.applications &&
            saved.x == unchanged.x && saved.y == unchanged.y && saved.monitor == unchanged.monitor,
            "Selecting native controller input must preserve the appearance, independent sizes, manual hide, filters and position");
        harness.Expect(false, false, "Controller input selection must not bypass manual hide or application filters");
        click(NavLayout);
        click(NavController);
        Check(SendDlgItemMessageW(window, ControllerIndex, CB_GETCURSEL, 0, 0) == index + 1,
            "Reopening Controller settings must restore the selected input slot");
    }
    SendDlgItemMessageW(window, ControllerIndex, CB_SETCURSEL, ControllerSlotCount + 1, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(ControllerIndex, CBN_SELCHANGE), 0);
    Check(app.settings.controllerIndex == ControllerSlotCount - 1,
        "An unavailable combo selection must not overwrite a valid native controller slot");
    app.settings = original;
    app.Changed();
    click(NavOverlay);
}

void TestIndependentSizes(Harness& harness, HWND allowedWindow) {
    auto& app = harness.app;
    const Settings original = app.settings;
    const HWND window = app.preferences.Handle();
    const auto click = [window](int id) { SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0); };
    const auto size = [&]() {
        RECT rect{};
        Check(GetWindowRect(app.overlay.Handle(), &rect) != FALSE, "Overlay bounds must be readable");
        return SIZE{rect.right - rect.left, rect.bottom - rect.top};
    };
    const auto expectSize = [&](int scale) {
        Check(EffectiveOverlayScale(app.settings) == scale, "Active device must retain its selected scale");
        const SIZE actual = size(), expected = OverlaySize(app.settings);
        Check(actual.cx == expected.cx && actual.cy == expected.cy, "Live overlay dimensions must follow the active device size");
    };
    const auto slide = [window](int id, int value, int notification = TB_ENDTRACK) {
        HWND control = GetDlgItem(window, id);
        Check(control != nullptr, "Size slider must exist on its device page");
        SendMessageW(control, TBM_SETPOS, TRUE, value);
        SendMessageW(window, WM_HSCROLL, notification, reinterpret_cast<LPARAM>(control));
    };
    const auto controllerMode = [&](bool enabled) {
        SendDlgItemMessageW(window, AppearanceDevice, CB_SETCURSEL, enabled ? 1 : 0, 0);
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(AppearanceDevice, CBN_SELCHANGE), 0);
    };
    app.settings.scale = 135;
    app.settings.controllerScale = 75;
    app.settings.enabled = false;
    app.settings.x = 57; app.settings.y = 41;
    app.settings.anchorRight = true; app.settings.anchorBottom = true;
    app.settings.slots[0].label = L"Custom";
    app.settings.slots[0].input = VK_F8;
    app.Changed();
    const Settings positioned = app.settings;
    Focus(window);
    click(NavController);
    Check(GetDlgItem(window, ControllerScale) && GetDlgItem(window, ControllerScaleValue) &&
        GetDlgItem(window, ResetControllerSize) && GetDlgItem(window, MoveOverlay),
        "Controller page must expose its own size, percentage, reset, and positioning controls");
    Check(SendDlgItemMessageW(window, ControllerScale, TBM_GETRANGEMIN, 0, 0) == 10 &&
        SendDlgItemMessageW(window, ControllerScale, TBM_GETRANGEMAX, 0, 0) == 200 &&
        Text(window, ControllerScaleValue) == L"75%", "Controller size range and initial percentage must match its saved setting");
    RECT reset{}, move{}, slider{}, device{}, deadzone{}, status{};
    GetWindowRect(GetDlgItem(window, ResetControllerSize), &reset);
    GetWindowRect(GetDlgItem(window, MoveOverlay), &move);
    GetWindowRect(GetDlgItem(window, ControllerScale), &slider);
    GetWindowRect(GetDlgItem(window, ControllerIndex), &device);
    GetWindowRect(GetDlgItem(window, ControllerDeadzone), &deadzone);
    GetWindowRect(GetDlgItem(window, Status), &status);
    Check(move.right < reset.left && slider.bottom < move.top && device.bottom < slider.top &&
        slider.right <= deadzone.left && deadzone.bottom < status.top,
        "Controller sizing, device, deadzone, and footer controls must not overlap");
    Check(IsWindowEnabled(GetDlgItem(window, MoveOverlay)), "The selected controller device must expose positioning");
    click(MoveOverlay);
    harness.Expect(true, true, "Controller positioning must preview the selected controller overlay");
    click(MoveOverlay);
    const std::string beforeDrag = ReadConfiguration(app.configPath);
    slide(ControllerScale, 145, TB_THUMBTRACK);
    Check(app.settings.controllerScale == 75 && Text(window, ControllerScaleValue) == L"145%" &&
        ReadConfiguration(app.configPath) == beforeDrag, "Dragging controller size must update its label without saving partial values");
    for (const int value : {10, 200, 145}) {
        slide(ControllerScale, value);
        Check(app.settings.controllerScale == value && app.settings.scale == 135 && app.settings.device == OverlayDevice::Controller,
            "Controller size must change independently while retaining the selected device");
        expectSize(value);
        harness.Expect(false, false, "Controller size edits must preserve a filtered manually hidden overlay");
    }
    controllerMode(true);
    Check(IsWindowEnabled(GetDlgItem(window, MoveOverlay)), "Controller positioning must be available when controller view is active");
    expectSize(145);
    click(NavLayout);
    slide(Scale, 85);
    Check(app.settings.scale == 85 && app.settings.controllerScale == 145 && Text(window, ScaleValue) == L"85%",
        "Keyboard size must retain its own value after selecting keyboard mode");
    expectSize(85);
    click(NavController);
    click(ResetControllerSize);
    Check(app.settings.controllerScale == 100 && app.settings.scale == 85 && Text(window, ControllerScaleValue) == L"100%" &&
        app.settings.x == positioned.x && app.settings.y == positioned.y && app.settings.monitor == positioned.monitor &&
        app.settings.anchorRight == positioned.anchorRight && app.settings.anchorBottom == positioned.anchorBottom,
        "Reset controller size must restore only controller scale while preserving keyboard size and placement");
    expectSize(100);
    slide(ControllerScale, 120);
    controllerMode(false);
    expectSize(85);
    controllerMode(true);
    expectSize(120);
    Check(!app.settings.enabled && app.settings.onlySelectedApps && app.settings.applications == original.applications &&
        app.settings.slots[0].input == VK_F8 && app.settings.slots[0].label == L"Custom",
        "Device switching and sizing must preserve manual hide, application filters, and bindings");
    click(MoveOverlay);
    harness.Expect(true, true, "Controller page must open a position preview directly");
    expectSize(120);
    controllerMode(false);
    harness.Expect(false, false, "Switching device must end controller positioning and preserve manual hide");
    expectSize(85);
    controllerMode(true);
    click(MoveOverlay);
    harness.Expect(true, true, "Controller positioning must work again after switching back");
    Focus(harness.outside);
    app.UpdateVisibility();
    harness.Expect(false, false, "Alt-Tab must end controller positioning and restore manual hide");
    Focus(window);
    click(NavLayout);
    click(ResetPosition);
    Check(app.settings.scale == 100 && app.settings.controllerScale == 120 && app.settings.x == 32 && app.settings.y == 32 &&
        !app.settings.anchorRight && app.settings.anchorBottom,
        "Layout reset must restore keyboard size and shared position without changing controller size");
    expectSize(100);
    Settings saved;
    Check(LoadSettings(app.configPath, saved) && saved.scale == 100 && saved.controllerScale == 120 &&
        saved.device == OverlayDevice::KeyboardMouse && !saved.enabled && saved.onlySelectedApps &&
        saved.applications == original.applications && saved.slots[0].input == VK_F8 && saved.slots[0].label == L"Custom",
        "Independent sizes, active device, bindings, and visibility preferences must survive reload together");
    click(NavOverlay);
    SendDlgItemMessageW(window, AppearanceDevice, CB_SETCURSEL, 1, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(AppearanceDevice, CBN_SELCHANGE), 0);
    click(OpenLayout);
    Check(GetDlgItem(window, ControllerScale) && !GetDlgItem(window, Scale),
        "Size and position from controller appearance must open the Controller page");
    Focus(allowedWindow);
    app.UpdateVisibility();
    harness.Expect(false, false, "An allowlisted window must still respect manual hide after resizing");
    app.Toggle();
    harness.Expect(true, false, "Showing controller view in an allowlisted window must use its stored size");
    expectSize(120);
    Focus(window);
    app.UpdateVisibility();
    harness.Expect(false, false, "Switching back to Settings must still apply the application filter");
    app.settings = original;
    app.Changed();
    click(NavOverlay);
    SendDlgItemMessageW(window, AppearanceDevice, CB_SETCURSEL, 0, 0);
    SendMessageW(window, WM_COMMAND, MAKEWPARAM(AppearanceDevice, CBN_SELCHANGE), 0);
    click(OpenLayout);
    Check(GetDlgItem(window, Scale) && !GetDlgItem(window, ControllerScale),
        "Size and position from keyboard appearance must open the Layout page");
}

void TestControllerEdgePosition(Harness& harness) {
    auto& app = harness.app;
    const Settings original = app.settings;
    app.settings.device = OverlayDevice::Controller;
    app.settings.enabled = false;
    const RECT screen = MonitorFor(app.settings).rcMonitor;
    for (const auto style : {ControllerStyle::Frost, ControllerStyle::Original}) {
        app.settings.controllerStyle = style;
        for (int layout = 0; layout < ControllerLayoutCount; ++layout) {
            app.settings.controllerLayout = static_cast<ControllerLayout>(layout);
            for (const int scale : {10, 100, 157, 200}) {
                app.settings.controllerScale = scale;
                harness.BeginPreview();
                const SIZE size = OverlaySize(app.settings);
                Check(size.cx < OverlaySize(scale).cx, "Controller drag area must exclude the keyboard canvas padding");
                app.SetPosition(screen.left, screen.bottom - size.cy);
                app.overlay.Render(ResolvedSettings(app.settings), app.pressed, app.controllerState);
                RECT placed{};
                Check(GetWindowRect(app.overlay.Handle(), &placed) != FALSE &&
                    placed.left == screen.left && placed.bottom == screen.bottom &&
                    placed.right - placed.left == size.cx && placed.bottom - placed.top == size.cy,
                    "Every controller layout must remain at the bottom-left screen edge after a move");
                Settings saved;
                Check(LoadSettings(app.configPath, saved) && saved.x == 0 && saved.y == 0 &&
                    !saved.anchorRight && saved.anchorBottom && !saved.enabled && saved.onlySelectedApps &&
                    saved.applications == original.applications,
                    "Moving a controller to the screen edge must persist zero margins and preserve visibility preferences");
                const POINT restored = AnchoredPosition(saved, screen);
                Check(restored.x == placed.left && restored.y == placed.top,
                    "Reloading controller position must not restore the old transparent padding");
                Focus(harness.outside);
                app.UpdateVisibility();
                harness.Expect(false, false, "Leaving Settings after a controller move must end editing and restore manual hide");
                Check((GetWindowLongPtrW(app.overlay.Handle(), GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0,
                    "Finishing controller positioning must restore click-through");
                app.overlay.Render(ResolvedSettings(app.settings), app.pressed, app.controllerState);
                RECT afterEditing{};
                GetWindowRect(app.overlay.Handle(), &afterEditing);
                Check(EqualRect(&placed, &afterEditing) != FALSE,
                    "Leaving controller move mode must not change the cropped window position or size");
            }
        }
    }
    app.settings = original;
    app.Changed();
    Focus(app.preferences.Handle());
    app.UpdateVisibility();
}

void TestVisibilityShortcut(Harness& harness) {
    auto& app = harness.app;
    const HWND window = app.preferences.Handle();
    const auto click = [window](int id) {
        Check(GetDlgItem(window, id) != nullptr, "Shortcut control must be visible");
        SendMessageW(window, WM_COMMAND, MAKEWPARAM(id, BN_CLICKED), 0);
    };
    const auto hotkey = [&](int id, UINT modifiers, UINT vk) {
        Application::WndProc(app.controller, WM_HOTKEY, static_cast<WPARAM>(id), MAKELPARAM(modifiers, vk));
    };
    SetWindowLongPtrW(app.controller, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&app));
    Check(app.SetHotkey(0, VK_F8), "F8 must register on the isolated desktop");
    click(OpenShortcut);
    Check(Text(window, HotkeyValue) == L"F8", "Overlay shortcut button must open the current shortcut");
    const int previousId = app.hotkeyId_;
    click(RecordHotkey);
    Check(app.preferences.Capturing(), "Recording must enable shortcut capture");
    SendDlgItemMessageW(window, ModShift, BM_SETCHECK, BST_CHECKED, 0);
    app.Input(VK_F23, true);
    Check(!app.preferences.Capturing() && app.settings.hotkeyVk == VK_F23 && app.settings.hotkeyModifiers == MOD_SHIFT &&
        Text(window, HotkeyValue) == L"Shift + F23", "Recording must replace the shortcut and update Settings");
    Settings saved;
    Check(LoadSettings(app.configPath, saved) && saved.hotkeyVk == VK_F23 && saved.hotkeyModifiers == MOD_SHIFT,
        "Recorded shortcut must survive restart");
    const int currentId = app.hotkeyId_;
    constexpr int conflictId = 99;
    Check(RegisterHotKey(app.controller, conflictId, MOD_SHIFT | MOD_ALT, VK_F22), "Test must reserve a competing shortcut");
    const bool acceptedConflict = app.SetHotkey(MOD_SHIFT | MOD_ALT, VK_F22);
    UnregisterHotKey(app.controller, conflictId);
    Check(!acceptedConflict && app.hotkeyId_ == currentId && app.settings.hotkeyVk == VK_F23,
        "An unavailable shortcut must leave the previous registration active");

    app.captureIgnoreKey_ = 0;
    hotkey(previousId, 0, VK_F8);
    hotkey(currentId, 0, VK_F23);
    Check(app.settings.enabled, "Stale or mismatched hotkey messages must not change visibility");
    hotkey(currentId, MOD_SHIFT, VK_F23);
    Check(!app.settings.enabled, "The new shortcut must hide the overlay");
    hotkey(currentId, MOD_SHIFT, VK_F23);
    Check(app.settings.enabled, "The new shortcut must show the overlay again");
    click(ResetHotkey);
    Check(app.settings.hotkeyVk == VK_F8 && app.settings.hotkeyModifiers == 0 && Text(window, HotkeyValue) == L"F8",
        "Reset must restore F8 without modifiers");
    click(NavOverlay);
    SetWindowLongPtrW(app.controller, GWLP_USERDATA, 0);
}

void TestControllerSettingsPolling(Harness& harness) {
    auto& app = harness.app;
    const auto original = app.settings;
    const HWND window = app.preferences.Handle();
    bool connected = true;
    const auto read = [](void* context, int index, ControllerRawState& raw) {
        if (index != 0 || !*static_cast<const bool*>(context)) return false;
        raw.buttons = 0x1000;
        raw.leftTrigger = 255;
        return true;
    };
    app.controllerProvider_.~ControllerProvider();
    new (&app.controllerProvider_) ControllerProvider(read, &connected);
    app.settings.enabled = false;
    app.settings.device = OverlayDevice::Controller;
    app.settings.controllerIndex = 0;
    app.preferences.SelectPage(4);
    Focus(window);
    app.Changed();
    Check(app.preferences.WantsControllerInput() && app.controllerState.connected && app.controllerState.buttons == 0x1000 &&
        app.controllerState.leftTrigger == 1.f && Text(window, ControllerConnection) == L"Connected",
        "Controller settings must show live input while the overlay is filtered and manually hidden");
    harness.Expect(false, false, "Testing a controller in Settings must preserve manual hide and application filtering");

    Focus(harness.outside);
    app.UpdateVisibility();
    Check(!app.preferences.WantsControllerInput() && !app.controllerTimerInterval_ && !app.controllerState.connected,
        "Leaving controller Settings must stop polling when the overlay is hidden");
    Focus(window);
    app.UpdateVisibility();
    Check(app.controllerState.connected && Text(window, ControllerConnection) == L"Connected",
        "Returning to controller Settings must resume input polling");
    connected = false;
    app.Changed();
    Check(!app.controllerState.connected && Text(window, ControllerConnection) == L"Waiting for controller",
        "The input status must reflect a disconnected source");
    app.preferences.SelectPage(3);
    Check(!app.controllerTimerInterval_ && !app.preferences.WantsControllerInput(),
        "Leaving the Overlay page must stop hidden controller polling");
    app.controllerProvider_.~ControllerProvider();
    new (&app.controllerProvider_) ControllerProvider();
    app.settings = original;
    app.preferences.SelectPage(0);
    app.Changed();
}

void RunTests() {
    Desktop desktop;
    TemporaryDirectory directory;
    AllowedProcess allowed(desktop, directory);
    Harness harness(directory, allowed);
    auto& app = harness.app;
    TestVisibilityShortcut(harness);
    TestControllerSettingsPolling(harness);
    harness.Expect(false, false, "An application filter must hide the overlay over Settings");
    harness.BeginPreview();
    SendMessageW(app.preferences.Handle(), WM_COMMAND, MAKEWPARAM(234, BN_CLICKED), 0);
    harness.Expect(true, true, "Appearance edits may retain the preview while Settings is active");
    Focus(harness.outside);
    app.UpdateVisibility();
    harness.Expect(false, false, "Switching away from Settings must end position mode and hide a restricted overlay");
    harness.BeginPreview();
    foregroundForTest = harness.outside;
    app.UpdateVisibility();
    harness.Expect(false, false, "Foreground tracking must independently end position preview outside Settings");
    Focus(app.preferences.Handle());
    for (int style = 0; style < OverlayStyleCount; ++style) {
        SendMessageW(app.preferences.Handle(), WM_COMMAND, MAKEWPARAM(230 + style, BN_CLICKED), 0);
        SendMessageW(app.preferences.Handle(), WM_COMMAND, MAKEWPARAM(220 + style % 5, BN_CLICKED), 0);
        app.settings.scale = 40 + style * 25;
        app.settings.opacity = 35 + style * 10;
        app.Changed();
        harness.Expect(false, false, "Appearance changes must preserve application filtering");
    }
    const Settings beforeGradient = app.settings;
    const HWND settingsWindow = app.preferences.Handle();
    Check(GetDlgItem(settingsWindow, StartHex) && GetDlgItem(settingsWindow, GradientFillControl),
        "Gradient style must expose endpoint controls and fill opacity on the Overlay page");
    SetDlgItemTextW(settingsWindow, StartHex, L"#123456");
    SetDlgItemTextW(settingsWindow, EndHex, L"abcdef");
    Check(app.settings.colorTheme == ColorTheme::Custom && app.settings.backgroundStart == RGB(18, 52, 86) &&
        app.settings.backgroundEnd == RGB(171, 205, 239), "Valid endpoint HEX edits must create a custom gradient");
    const COLORREF customStart = app.settings.backgroundStart, customEnd = app.settings.backgroundEnd;
    for (int theme = 1; theme < ColorThemeCount; ++theme) {
        SendMessageW(app.preferences.Handle(), WM_COMMAND, MAKEWPARAM(800 + theme, BN_CLICKED), 0);
        Check(static_cast<int>(app.settings.colorTheme) == theme, "Theme cards must select their matching theme");
        Check(app.settings.backgroundStart == customStart && app.settings.backgroundEnd == customEnd,
            "Browsing presets must preserve the saved custom gradient endpoints");
        Check(app.settings.accent == beforeGradient.accent, "Gradient presets must not overwrite the pressed color of other styles");
        harness.Expect(false, false, "Theme changes must preserve application filtering");
    }
    Check(ThemePalette(app.settings).start == customStart && ThemePalette(app.settings).end == customEnd,
        "Returning to Custom must restore the exact previously edited endpoints");
    const std::string beforeInvalid = ReadConfiguration(app.configPath);
    SetDlgItemTextW(settingsWindow, StartHex, L"#GGGGGG");
    Check(app.settings.backgroundStart == customStart && ReadConfiguration(app.configPath) == beforeInvalid,
        "Invalid endpoint HEX input must not alter colors or the saved configuration");
    SendMessageW(settingsWindow, WM_COMMAND, MAKEWPARAM(StartHex, EN_KILLFOCUS), 0);
    const std::string beforePreview = ReadConfiguration(app.configPath);
    SendMessageW(settingsWindow, WM_COMMAND, MAKEWPARAM(PreviewPressed, BN_CLICKED), 0);
    SendMessageW(settingsWindow, WM_COMMAND, MAKEWPARAM(PreviewBackground, BN_CLICKED), 0);
    SendDlgItemMessageW(settingsWindow, PreviewLayout, CB_SETCURSEL, beforeGradient.isoLayout ? 0 : 1, 0);
    SendMessageW(settingsWindow, WM_COMMAND, MAKEWPARAM(PreviewLayout, CBN_SELCHANGE), 0);
    SendMessageW(settingsWindow, WM_COMMAND, MAKEWPARAM(PreviewIdle, BN_CLICKED), 0);
    Check(app.settings.isoLayout == beforeGradient.isoLayout && ReadConfiguration(app.configPath) == beforePreview,
        "Preview pressed state, background, and keyboard layout must not change or save live overlay preferences");
    Check(std::none_of(app.pressed.begin(), app.pressed.end(), [](bool down) { return down; }),
        "Pressed preview must not inject input into the actual overlay");
    harness.Expect(false, false, "Preview controls must not show a filtered overlay");
    SendMessageW(settingsWindow, WM_COMMAND, MAKEWPARAM(SwapGradient, BN_CLICKED), 0);
    Check(app.settings.backgroundStart == customEnd && app.settings.backgroundEnd == customStart && app.settings.colorTheme == ColorTheme::Custom,
        "Swap must reverse only the two gradient endpoints");
    HWND fill = GetDlgItem(settingsWindow, GradientFillControl);
    SendMessageW(fill, TBM_SETPOS, TRUE, 45);
    SendMessageW(settingsWindow, WM_HSCROLL, TB_THUMBTRACK, reinterpret_cast<LPARAM>(fill));
    Check(app.settings.gradientFillOpacity == beforeGradient.gradientFillOpacity,
        "Dragging fill opacity must preview without saving partial values");
    SendMessageW(settingsWindow, WM_HSCROLL, TB_ENDTRACK, reinterpret_cast<LPARAM>(fill));
    Check(app.settings.gradientFillOpacity == 45, "Releasing fill opacity must save the selected amount");
    SendMessageW(settingsWindow, WM_COMMAND, MAKEWPARAM(ResetGradient, BN_CLICKED), 0);
    Check(app.settings.gradientFillOpacity == 16 && app.settings.colorTheme == ColorTheme::Sunset,
        "Reset Gradient must restore the Sunset palette and default transparent fill");
    Check(app.settings.onlySelectedApps == beforeGradient.onlySelectedApps && app.settings.applications == beforeGradient.applications &&
        app.settings.enabled == beforeGradient.enabled && app.settings.x == beforeGradient.x && app.settings.y == beforeGradient.y &&
        app.settings.anchorRight == beforeGradient.anchorRight && app.settings.anchorBottom == beforeGradient.anchorBottom &&
        app.settings.monitor == beforeGradient.monitor && app.settings.scale == beforeGradient.scale && app.settings.opacity == beforeGradient.opacity &&
        app.settings.accent == beforeGradient.accent && app.settings.isoLayout == beforeGradient.isoLayout,
        "Gradient editing and reset must preserve application restrictions, visibility, layout, position, size, overall opacity, and pressed accent");
    TestKeyboardForms(harness);
    TestPhysicalModifierState(harness);
    TestControllerAppearance(harness, allowed.window);
    TestOriginalOpacity(harness);
    TestControllerInputSelection(harness);
    TestIndependentSizes(harness, allowed.window);
    TestControllerEdgePosition(harness);
    SendMessageW(settingsWindow, WM_COMMAND, MAKEWPARAM(NavLayout, BN_CLICKED), 0);
    Check(GetDlgItem(settingsWindow, Scale) && GetDlgItem(settingsWindow, Opacity) && GetDlgItem(settingsWindow, MoveOverlay) &&
        GetDlgItem(settingsWindow, ResetPosition) && GetDlgItem(settingsWindow, Enabled) && GetDlgItem(settingsWindow, Layout),
        "The Layout page must retain shared positioning, visibility, sizing, and keyboard layout controls");
    SendMessageW(app.preferences.Handle(), WM_COMMAND, MAKEWPARAM(100, BN_CLICKED), 0);
    Settings saved;
    Check(LoadSettings(app.configPath, saved) && saved.onlySelectedApps && saved.applications == app.settings.applications,
        "Appearance changes must persist the application restriction");
    Focus(allowed.window);
    app.UpdateVisibility();
    harness.Expect(true, false, "An allowlisted application must show the overlay");
    Focus(harness.outside);
    app.UpdateVisibility();
    harness.Expect(false, false, "An unrelated application must hide the overlay after normal use");
    app.overlay.Render(app.settings, app.pressed, app.controllerState);
    harness.Expect(false, false, "Rendering must not reopen a previously visible hidden overlay");
    Focus(allowed.window);
    app.UpdateVisibility();
    harness.Expect(true, false, "Returning to an allowlisted application must show the overlay again");
    app.Toggle();
    harness.Expect(false, false, "Manual hide must hide the overlay in an allowlisted application");
    SendMessageW(settingsWindow, WM_COMMAND, MAKEWPARAM(ThemeFirst + static_cast<int>(ColorTheme::Aurora), BN_CLICKED), 0);
    SendMessageW(settingsWindow, WM_COMMAND, MAKEWPARAM(ResetGradient, BN_CLICKED), 0);
    harness.Expect(false, false, "Editing and resetting Gradient must preserve a manually hidden overlay");
    Check(LoadSettings(app.configPath, saved) && !saved.enabled && saved.onlySelectedApps,
        "Gradient edits must persist manual hide and application filtering together");
    Focus(harness.outside);
    app.UpdateVisibility();
    Focus(allowed.window);
    app.UpdateVisibility();
    harness.Expect(false, false, "Focus changes must preserve manual hide");
    app.Toggle();
    harness.BeginPreview();
    SendMessageW(app.preferences.Handle(), WM_COMMAND, MAKEWPARAM(102, BN_CLICKED), 0);
    harness.Expect(false, false, "Leaving the Overlay settings page must end position preview");
    SendMessageW(app.preferences.Handle(), WM_COMMAND, MAKEWPARAM(100, BN_CLICKED), 0);
    harness.BeginPreview();
    ShowWindow(app.preferences.Handle(), SW_MINIMIZE);
    Pump();
    app.UpdateVisibility();
    harness.Expect(false, false, "Minimizing Settings must end position preview");
    harness.BeginPreview();
    SendMessageW(app.preferences.Handle(), WM_CLOSE, 0, 0);
    app.UpdateVisibility();
    harness.Expect(false, false, "Closing Settings must end position preview");
}
}

int main(int argc, char** argv) {
    try {
        SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
        if (argc == 2 && std::string(argv[1]) == "--fixture") {
            HWND window = CreateFixture(L"Allowlisted application");
            ShowWindow(window, SW_SHOW);
            MSG message{};
            while (GetMessageW(&message, nullptr, 0, 0) > 0) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
            return 0;
        }
        RunTests();
        std::puts("Application visibility passed: isolated desktop, simulated foreground events, real HWND visibility, appearance changes, native controller selection, independent device sizes, position preview lifecycle, and manual hide.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s (Win32 error %lu)\n", error.what(), GetLastError());
        return 1;
    }
}
