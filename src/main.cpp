#include "app.hpp"
#include "geometry.hpp"
#include "injected_input.hpp"
#include "update_install.hpp"
#include <shellapi.h>
#include <commctrl.h>
#include <wtsapi32.h>
#include <algorithm>

namespace input_overlay {
namespace {
constexpr UINT RenderMessage = WM_APP + 10;
constexpr UINT StartupUpdateMessage = WM_APP + 41;
constexpr UINT WheelTimer = 1;
constexpr UINT ControllerTimer = 2;
constexpr int HotkeyId = 1;
constexpr UINT TraySettings = 100, TrayToggle = 101, TrayPosition = 102, TrayExit = 103, TrayUpdates = 104;
Application* activeApp = nullptr;
bool sessionLocked = false;
InjectedInput injectedInput;

MONITORINFOEXW MonitorFor(const Settings& s) {
    struct Search { const std::wstring* name; HMONITOR result; } search{&s.monitor, nullptr};
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR monitor, HDC, LPRECT, LPARAM data) -> BOOL {
        auto& request = *reinterpret_cast<Search*>(data);
        MONITORINFOEXW info{}; info.cbSize = sizeof(info);
        if (GetMonitorInfoW(monitor, &info) && *request.name == info.szDevice) {
            request.result = monitor; return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&search));
    if (!search.result) search.result = MonitorFromPoint({0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFOEXW info{}; info.cbSize = sizeof(info);
    GetMonitorInfoW(search.result, &info);
    return info;
}

Settings ResolvedSettings(const Settings& settings) {
    auto resolved = settings;
    auto info = MonitorFor(settings);
    const auto position = AnchoredPosition(settings, info.rcMonitor);
    resolved.x = position.x; resolved.y = position.y;
    return resolved;
}

HICON MakeIcon() {
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof(header); header.bV5Width = 32; header.bV5Height = -32;
    header.bV5Planes = 1; header.bV5BitCount = 32; header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00ff0000; header.bV5GreenMask = 0x0000ff00;
    header.bV5BlueMask = 0x000000ff; header.bV5AlphaMask = 0xff000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color) return nullptr;
    auto* pixels = static_cast<DWORD*>(bits);
    for (int y = 0; y < 32; ++y) for (int x = 0; x < 32; ++x) {
        bool inside = x >= 2 && x < 30 && y >= 2 && y < 30;
        bool key = (x >= 7 && x <= 9 && y >= 9 && y <= 22) ||
            (x >= 14 && x <= 24 && y >= 9 && y <= 22 &&
             (x <= 16 || x >= 22 || y <= 11 || y >= 20));
        pixels[y * 32 + x] = inside ? (key ? 0xffa5e2ff : 0xff182532) : 0;
    }
    HBITMAP mask = CreateBitmap(32, 32, 1, 1, nullptr);
    ICONINFO iconInfo{TRUE, 0, 0, mask, color};
    HICON icon = CreateIconIndirect(&iconInfo);
    DeleteObject(mask); DeleteObject(color);
    return icon;
}

bool ValidKeyboardKey(UINT vk) {
    return vk >= VK_BACK && vk <= 0xfe && vk != VK_SHIFT && vk != VK_CONTROL &&
        vk != VK_MENU && vk != VK_LSHIFT && vk != VK_RSHIFT &&
        vk != VK_LCONTROL && vk != VK_RCONTROL && vk != VK_LMENU &&
        vk != VK_RMENU && vk != VK_LWIN && vk != VK_RWIN;
}
}

bool Application::Initialize(HINSTANCE instance, bool quiet) {
    instance_ = instance;
    bool settingsRequired = false;
    auto executable = ExecutablePath();
    configPath = executable.substr(0, executable.find_last_of(L"\\/")) + L"\\Input Overlay.ini";
    const bool existingConfig = GetFileAttributesW(configPath.c_str()) != INVALID_FILE_ATTRIBUTES;
    if (!LoadSettings(configPath, settings) && existingConfig) {
        settings.enabled = false;
        MessageBoxW(nullptr, L"Your settings file could not be read. The overlay is paused and defaults are loaded for this session. Keep a copy of the file before changing settings if you want to recover it.", AppName, MB_ICONWARNING);
        settingsRequired = true;
    }
    NormalizeSettings(settings);
    WNDCLASSEXW wc{}; wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = WndProc; wc.hInstance = instance; wc.lpszClassName = ControllerClass;
    if (!RegisterClassExW(&wc)) return false;
    controller = CreateWindowExW(WS_EX_TOOLWINDOW, ControllerClass, AppName, WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, instance, this);
    if (!controller) return false;
    activeApp = this;
    if (!injectedInput.Start(controller)) PostMessageW(controller, WM_APP_INPUT_ERROR, 0, 0);
    icon_ = MakeIcon();
    taskbarCreated_ = RegisterWindowMessageW(L"TaskbarCreated");
    if (!overlay.Create(instance, controller)) return false;
    foregroundHook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND,
        nullptr, ForegroundEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
    if (!foregroundHook_) {
        MessageBoxW(nullptr, L"Windows could not enable foreground application tracking.", AppName, MB_ICONERROR);
        return false;
    }
    WTSRegisterSessionNotification(controller, NOTIFY_FOR_THIS_SESSION);
    if (!RegisterHotKey(controller, HotkeyId, settings.hotkeyModifiers | MOD_NOREPEAT, settings.hotkeyVk)) {
        MessageBoxW(nullptr, L"The hide/show shortcut is already in use. Choose another shortcut in General settings. You can still hide the overlay from its tray menu.", AppName, MB_ICONWARNING);
        settingsRequired = true;
    }
    UpdateTray();
    if (!trayAdded_) {
        MessageBoxW(nullptr, L"Windows could not create the tray icon. Input Overlay will close so it does not remain running without its controls.", AppName, MB_ICONERROR);
        return false;
    }
    overlay.Render(ResolvedSettings(settings), pressed, controllerState);
    UpdateVisibility();
    if (settingsRequired || (!quiet && !settings.startMinimized)) ShowSettings();
    if (settings.autoCheckUpdates) PostMessageW(controller, StartupUpdateMessage, 0, 0);
    return true;
}

int Application::Run() {
    MSG message{};
    while (true) {
        const BOOL result = GetMessageW(&message, nullptr, 0, 0);
        if (result <= 0) return result < 0 ? 1 : static_cast<int>(message.wParam);
        const HWND window = preferences.Handle();
        if (window && (message.hwnd == window || IsChild(window, message.hwnd)) &&
            message.message >= WM_KEYFIRST && message.message <= WM_KEYLAST) {
            const bool capturedKey = captureIgnoreKey_ && message.wParam == captureIgnoreKey_;
            if (capturedKey && (message.message == WM_KEYUP || message.message == WM_SYSKEYUP)) captureIgnoreKey_ = 0;
            if (preferences.Capturing() || capturedKey) continue;
        }
        if (window && IsWindowVisible(window) && !preferences.Capturing() && IsDialogMessageW(window, &message)) continue;
        TranslateMessage(&message); DispatchMessageW(&message);
    }
}

void Application::Shutdown() {
    if (shuttingDown_) return;
    shuttingDown_ = true;
    updates_.Stop();
    injectedInput.Stop();
    if (foregroundHook_) { UnhookWinEvent(foregroundHook_); foregroundHook_ = nullptr; }
    if (controller) {
        KillTimer(controller, WheelTimer);
        KillTimer(controller, ControllerTimer);
        UnregisterHotKey(controller, HotkeyId);
        WTSUnRegisterSessionNotification(controller);
        if (inputActive_) {
            RAWINPUTDEVICE devices[] = {{1, 2, RIDEV_REMOVE, nullptr}, {1, 6, RIDEV_REMOVE, nullptr}};
            RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE));
        }
        NOTIFYICONDATAW tray{}; tray.cbSize = sizeof(tray); tray.hWnd = controller; tray.uID = 1;
        Shell_NotifyIconW(NIM_DELETE, &tray);
    }
    preferences.Destroy(); overlay.Destroy();
    controllerTimerInterval_ = 0;
    controllerState = {};
    controllerProvider_.Reset();
    if (controller) DestroyWindow(controller);
    controller = nullptr;
    if (icon_) DestroyIcon(icon_);
    icon_ = nullptr; activeApp = nullptr;
}

bool Application::Save() {
    if (SaveSettings(configPath, settings)) return true;
    MessageBoxW(preferences.Handle(),
        L"Settings could not be saved. Move the portable folder to a writable location. Your changes still apply for this session.",
        AppName, MB_ICONWARNING);
    return false;
}

void Application::Changed() {
    NormalizeSettings(settings);
    ResetInputs();
    UpdateVisibility();
    overlay.Render(ResolvedSettings(settings), pressed, controllerState);
    UpdateTray();
    Save();
}

void Application::Toggle() {
    preferences.CancelCapture();
    settings.enabled = !settings.enabled;
    if (overlay.Editing()) overlay.SetEditing(false);
    Changed(); preferences.Refresh();
}

void Application::UpdateVisibility() {
    const HWND foreground = GetForegroundWindow();
    const HWND settingsWindow = preferences.Handle();
    const bool positionPreview = !sessionLocked && settingsWindow && IsWindowVisible(settingsWindow) &&
        !IsIconic(settingsWindow) && (foreground == settingsWindow || foreground == overlay.Handle());
    const bool endPositioning = overlay.Editing() && !positionPreview;
    if (endPositioning) overlay.SetEditing(false);
    const bool allowed = MatchesApplication(settings, ProcessPath(foreground));
    const bool show = !sessionLocked && (overlay.Editing() || (settings.enabled && allowed));
    if (show != visible_) {
        visible_ = show;
        ResetInputs();
        if (show) overlay.Render(ResolvedSettings(settings), pressed, controllerState);
    }
    overlay.SetVisible(show);
    SyncInputRegistration();
    SyncControllerPolling();
    if (endPositioning) preferences.Refresh();
}

void Application::SyncInputRegistration() {
    const bool wanted = !sessionLocked && ((visible_ && settings.device == OverlayDevice::KeyboardMouse) || preferences.Capturing());
    if (wanted == inputActive_) return;
    const DWORD flags = wanted ? RIDEV_INPUTSINK | RIDEV_DEVNOTIFY : RIDEV_REMOVE;
    RAWINPUTDEVICE devices[] = {{1, 2, flags, wanted ? controller : nullptr},
                                {1, 6, flags, wanted ? controller : nullptr}};
    if (RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE))) {
        inputActive_ = wanted;
        injectedInput.SetActive(wanted, ++inputGeneration_);
    }
    else if (wanted) MessageBoxW(preferences.Handle(), L"Windows could not enable keyboard and mouse input. Restart Input Overlay to try again.", AppName, MB_ICONERROR);
}

void Application::SyncControllerPolling() {
    if (shuttingDown_ || !visible_ || settings.device != OverlayDevice::Controller) {
        if (controller) KillTimer(controller, ControllerTimer);
        controllerTimerInterval_ = 0;
        controllerProvider_.Reset();
        if (controllerState != ControllerState{}) {
            controllerState = {};
            RequestRender();
        }
        return;
    }
    if (!controllerTimerInterval_) PollController();
}

void Application::PollController() {
    if (shuttingDown_ || !visible_ || settings.device != OverlayDevice::Controller) {
        SyncControllerPolling();
        return;
    }
    const auto now = GetTickCount64();
    const auto state = controllerProvider_.Poll(now, settings.controllerIndex, settings.controllerDeadzone);
    if (state != controllerState) {
        controllerState = state;
        RequestRender();
    }
    const auto interval = controllerProvider_.NextPollDelay(now);
    if (interval != controllerTimerInterval_) {
        controllerTimerInterval_ = SetTimer(controller, ControllerTimer, interval, nullptr) ? interval : 0;
    }
}

void Application::SetPosition(int x, int y) {
    const SIZE size = OverlaySize(settings);
    RECT rect{x, y, x + size.cx, y + size.cy};
    HMONITOR monitor = MonitorFromRect(&rect, MONITOR_DEFAULTTONEAREST);
    MONITORINFOEXW info{}; info.cbSize = sizeof(info); GetMonitorInfoW(monitor, &info);
    settings.monitor = info.szDevice;
    AnchorPosition(settings, info.rcMonitor, {x, y});
    Save();
    RequestRender();
}

void Application::EditPosition(bool edit) {
    if (edit) ShowSettings();
    overlay.SetEditing(edit);
    UpdateVisibility();
    overlay.Render(ResolvedSettings(settings), pressed, controllerState);
    UpdateTray();
    preferences.Refresh();
}

bool Application::SetHotkey(UINT modifiers, UINT vk) {
    modifiers &= MOD_CONTROL | MOD_ALT | MOD_SHIFT | MOD_WIN;
    if (!ValidKeyboardKey(vk)) return false;
    if (modifiers == settings.hotkeyModifiers && vk == settings.hotkeyVk) {
        UnregisterHotKey(controller, HotkeyId);
        return RegisterHotKey(controller, HotkeyId, modifiers | MOD_NOREPEAT, vk) != FALSE;
    }
    constexpr int CandidateHotkey = 2;
    if (!RegisterHotKey(controller, CandidateHotkey, modifiers | MOD_NOREPEAT, vk)) return false;
    UnregisterHotKey(controller, HotkeyId);
    UnregisterHotKey(controller, CandidateHotkey);
    if (!RegisterHotKey(controller, HotkeyId, modifiers | MOD_NOREPEAT, vk)) {
        RegisterHotKey(controller, HotkeyId, settings.hotkeyModifiers | MOD_NOREPEAT, settings.hotkeyVk);
        return false;
    }
    settings.hotkeyModifiers = modifiers; settings.hotkeyVk = vk;
    Changed();
    return true;
}

void Application::ResetInputs() {
    pressed.fill(false); deviceInputs_.clear(); wheelUntil_.fill(0);
    controllerState = {};
    controllerProvider_.Reset();
    controllerTimerInterval_ = 0;
    if (controller) {
        KillTimer(controller, WheelTimer);
        KillTimer(controller, ControllerTimer);
    }
    RequestRender();
}

void Application::RequestRender() {
    if (!dirty_ && controller && !shuttingDown_) { dirty_ = true; PostMessageW(controller, RenderMessage, 0, 0); }
}

void Application::Input(int code, bool down) {
    if (code <= 0 || code >= static_cast<int>(InputCount)) return;
    if (pressed[code] == down) return;
    pressed[code] = down;
    if (down && preferences.Capturing()) {
        if (code < 256) captureIgnoreKey_ = static_cast<UINT>(code);
        preferences.CaptureInput(code);
        return;
    }
    if (visible_ && settings.device == OverlayDevice::KeyboardMouse) {
        for (const auto& slot : settings.slots) if (slot.visible && slot.input == code) { RequestRender(); break; }
    }
}

void Application::DeviceInput(HANDLE deviceHandle, int code, bool down) {
        if (code <= 0 || code >= static_cast<int>(InputCount)) return;
        deviceInputs_[deviceHandle][code] = down;
        int output = code;
        if (code == VK_LSHIFT || code == VK_RSHIFT) output = VK_SHIFT;
        if (code == VK_LCONTROL || code == VK_RCONTROL) output = VK_CONTROL;
        if (code == VK_LMENU || code == VK_RMENU) output = VK_MENU;
        bool held = false;
        for (const auto& device : deviceInputs_) {
            if (output == VK_SHIFT) held |= device.second[VK_LSHIFT] || device.second[VK_RSHIFT];
            else if (output == VK_CONTROL) held |= device.second[VK_LCONTROL] || device.second[VK_RCONTROL];
            else if (output == VK_MENU) held |= device.second[VK_LMENU] || device.second[VK_RMENU];
            else held |= device.second[output];
        }
        Input(output, held);
}

void Application::RawInput(HRAWINPUT handle) {
    if (!inputActive_) return;
    RAWINPUT raw{};
    UINT bytes = sizeof(raw);
    if (GetRawInputData(handle, RID_INPUT, &raw, &bytes, sizeof(RAWINPUTHEADER)) == UINT(-1)) return;
    if (raw.header.dwType == RIM_TYPEMOUSE && !raw.data.mouse.usButtonFlags) return;
    if (raw.header.dwType != RIM_TYPEKEYBOARD && raw.header.dwType != RIM_TYPEMOUSE) return;
    auto event = [&](int code, bool down) {
        DeviceInput(raw.header.hDevice, code, down);
    };
    if (raw.header.dwType == RIM_TYPEKEYBOARD) {
        const auto& key = raw.data.keyboard;
        UINT vk = key.VKey;
        if (vk == 255 || vk == 0) return;
        if (vk == VK_SHIFT) vk = MapVirtualKeyW(key.MakeCode, MAPVK_VSC_TO_VK_EX);
        if (vk == VK_CONTROL) vk = key.Flags & RI_KEY_E0 ? VK_RCONTROL : VK_LCONTROL;
        if (vk == VK_MENU) vk = key.Flags & RI_KEY_E0 ? VK_RMENU : VK_LMENU;
        event(vk, !(key.Flags & RI_KEY_BREAK));
    } else {
        const USHORT flags = raw.data.mouse.usButtonFlags;
        constexpr USHORT downs[] = {RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_DOWN,
            RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_5_DOWN};
        constexpr USHORT ups[] = {RI_MOUSE_LEFT_BUTTON_UP, RI_MOUSE_RIGHT_BUTTON_UP,
            RI_MOUSE_MIDDLE_BUTTON_UP, RI_MOUSE_BUTTON_4_UP, RI_MOUSE_BUTTON_5_UP};
        for (int i = 0; i < 5; ++i) {
            if (flags & downs[i]) event(MouseLeft + i, true);
            if (flags & ups[i]) event(MouseLeft + i, false);
        }
        if (flags & RI_MOUSE_WHEEL) {
            const int direction = static_cast<SHORT>(raw.data.mouse.usButtonData) > 0 ? 0 : 1;
            wheelUntil_[direction] = GetTickCount64() + 90;
            Input(WheelUp + direction, true);
            SetTimer(controller, WheelTimer, 30, nullptr);
        }
    }
}

void Application::ShowSettings() {
    preferences.Show(*this);
    if (icon_ && preferences.Handle()) {
        SendMessageW(preferences.Handle(), WM_SETICON, ICON_BIG, reinterpret_cast<LPARAM>(icon_));
        SendMessageW(preferences.Handle(), WM_SETICON, ICON_SMALL, reinterpret_cast<LPARAM>(icon_));
    }
}

void Application::CheckForUpdates(bool manual) {
    if (shuttingDown_ || updateInstallRequested_) return;
    if (manual) preferences.ShowUpdates(*this);
    if (!updates_.Check(CurrentBuildCommit(), controller)) return;
    manualUpdateCheck_ = manual;
    updateResult = updates_.Snapshot();
    preferences.Refresh();
}

void Application::InstallUpdate() {
    if (shuttingDown_ || updateInstallRequested_ || updateResult.status != UpdateStatus::Available) return;
    const auto executable = ExecutablePath();
    const auto directory = executable.substr(0, executable.find_last_of(L"\\/"));
    if (!updates_.Download(updateResult.info, directory, controller)) return;
    updateInstallRequested_ = true;
    updateResult = updates_.Snapshot();
    preferences.Refresh();
}

void Application::NotifyUpdate() {
    if (!controller || !trayAdded_) return;
    NOTIFYICONDATAW notification{};
    notification.cbSize = sizeof(notification);
    notification.hWnd = controller;
    notification.uID = 1;
    notification.uFlags = NIF_INFO;
    notification.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
    wcsncpy_s(notification.szInfoTitle, L"Input Overlay update available", _TRUNCATE);
    wcsncpy_s(notification.szInfo, L"A new update is ready. Click to read what changed and choose whether to install it.", _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &notification);
}

void Application::UpdateCompleted() {
    if (shuttingDown_) return;
    auto completed = updates_.Snapshot();
    if (completed.generation <= lastUpdateGeneration_) return;
    lastUpdateGeneration_ = completed.generation;
    updateResult = std::move(completed);
    preferences.Refresh();
    if (updateResult.status == UpdateStatus::Available) {
        if (manualUpdateCheck_ || (preferences.Handle() && IsWindowVisible(preferences.Handle())))
            preferences.ShowUpdates(*this);
        else NotifyUpdate();
        UpdateTray();
    }
    if (updateResult.status == UpdateStatus::Error) updateInstallRequested_ = false;
    if (updateResult.status != UpdateStatus::Ready || !updateInstallRequested_) return;
    updateInstallRequested_ = false;
    UpdateInstallPlan plan;
    plan.stagingDirectory = updateResult.stageDirectory;
    plan.targetExecutable = ExecutablePath();
    const wchar_t* names[] = {L"Input Overlay.exe", L"LICENSE", L"README.md", L"THIRD_PARTY_NOTICES.txt"};
    for (size_t index = 0; index < plan.files.size(); ++index) {
        const auto file = std::find_if(updateResult.info.files.begin(), updateResult.info.files.end(),
            [&](const auto& candidate) { return candidate.name == names[index]; });
        if (file != updateResult.info.files.end()) plan.files[index] = {file->name, file->sha256, file->size};
    }
    std::wstring error;
    if (BeginUpdateInstall(plan, error)) {
        Quit();
    } else {
        updateResult.status = UpdateStatus::Error;
        updateResult.message = error.empty() ? L"The update could not be started. Your current installation is unchanged." : error;
        preferences.ShowUpdates(*this);
    }
}

void Application::Quit() { PostQuitMessage(0); }

void Application::UpdateTray() {
    if (!controller) return;
    NOTIFYICONDATAW tray{}; tray.cbSize = sizeof(tray);
    tray.hWnd = controller; tray.uID = 1;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = WM_APP_TRAY;
    tray.hIcon = icon_ ? icon_ : LoadIconW(nullptr, IDI_APPLICATION);
    std::wstring tip = L"Input Overlay - " + std::wstring(settings.enabled ? L"Enabled" : L"Hidden") +
        L" (" + HotkeyName(settings.hotkeyModifiers, settings.hotkeyVk) + L")";
    if (updateResult.status == UpdateStatus::Available) tip += L" - Update available";
    wcsncpy_s(tray.szTip, tip.c_str(), _TRUNCATE);
    if (Shell_NotifyIconW(trayAdded_ ? NIM_MODIFY : NIM_ADD, &tray)) trayAdded_ = true;
}

void Application::TrayMenu() {
    const bool finishPositioning = overlay.Editing();
    HMENU menu = CreatePopupMenu();
    AppendMenuW(menu, MF_STRING, TraySettings, L"Settings");
    AppendMenuW(menu, MF_STRING, TrayUpdates, L"Check for updates");
    AppendMenuW(menu, MF_STRING, TrayToggle, settings.enabled ? L"Hide overlay" : L"Show overlay");
    AppendMenuW(menu, MF_STRING, TrayPosition, finishPositioning ? L"Finish positioning" : L"Move overlay");
    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, TrayExit, L"Exit Input Overlay");
    SetMenuDefaultItem(menu, TraySettings, FALSE);
    POINT position{}; GetCursorPos(&position);
    SetForegroundWindow(controller);
    const UINT choice = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
        position.x, position.y, 0, controller, nullptr);
    PostMessageW(controller, WM_NULL, 0, 0); DestroyMenu(menu);
    if (choice == TraySettings) ShowSettings();
    if (choice == TrayUpdates) CheckForUpdates(true);
    if (choice == TrayToggle) Toggle();
    if (choice == TrayPosition) EditPosition(!finishPositioning);
    if (choice == TrayExit) Quit();
}

void CALLBACK Application::ForegroundEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD) {
    if (activeApp && activeApp->controller) PostMessageW(activeApp->controller, WM_APP_FOREGROUND, 0, 0);
}

LRESULT CALLBACK Application::WndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* self = reinterpret_cast<Application*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<Application*>(reinterpret_cast<CREATESTRUCTW*>(lParam)->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(hwnd, message, wParam, lParam);
    if (self->taskbarCreated_ && message == self->taskbarCreated_) {
        self->trayAdded_ = false; self->UpdateTray(); return 0;
    }
    switch (message) {
    case StartupUpdateMessage:
        if (self->settings.autoCheckUpdates) self->CheckForUpdates(false);
        return 0;
    case WM_APP_UPDATE: self->UpdateCompleted(); return 0;
    case WM_INPUT:
        self->RawInput(reinterpret_cast<HRAWINPUT>(lParam));
        return DefWindowProcW(hwnd, message, wParam, lParam);
    case WM_INPUT_DEVICE_CHANGE:
        self->ResetInputs(); self->SyncControllerPolling(); return 0;
    case WM_APP_INPUT_ERROR:
        MessageBoxW(self->preferences.Handle(), L"Software-remapped keyboard input could not be enabled. Physical keyboard and mouse input still work. Restart Input Overlay to try again.", AppName, MB_ICONWARNING);
        return 0;
    case WM_APP_INJECTED:
        if (self->inputActive_ && (static_cast<unsigned>(lParam) >> 1) == self->inputGeneration_) {
            const int input = static_cast<int>(wParam);
            if (input == WheelUp || input == WheelDown) {
                self->wheelUntil_[input - WheelUp] = GetTickCount64() + 90;
                self->Input(input, true);
                SetTimer(hwnd, WheelTimer, 30, nullptr);
            } else self->DeviceInput(INVALID_HANDLE_VALUE, input, (lParam & 1) != 0);
        }
        return 0;
    case WM_HOTKEY: if (!self->preferences.Capturing()) self->Toggle(); return 0;
    case WM_APP_FOREGROUND:
        self->UpdateVisibility(); return 0;
    case WM_APP_SHOW_SETTINGS: self->ShowSettings(); return 0;
    case WM_APP_REFRESH: self->SetPosition(static_cast<int>(wParam), static_cast<int>(lParam)); return 0;
    case RenderMessage:
        self->dirty_ = false;
        if (self->visible_) self->overlay.Render(ResolvedSettings(self->settings), self->pressed, self->controllerState);
        return 0;
    case WM_APP_TRAY:
        if (lParam == NIN_BALLOONUSERCLICK) self->preferences.ShowUpdates(*self);
        else if (lParam == WM_LBUTTONDBLCLK || lParam == NIN_KEYSELECT) self->ShowSettings();
        else if (lParam == WM_RBUTTONUP || lParam == WM_CONTEXTMENU) self->TrayMenu();
        return 0;
    case WM_TIMER:
        if (wParam == ControllerTimer) self->PollController();
        else if (wParam == WheelTimer) {
            bool pending = false;
            const auto now = GetTickCount64();
            for (int i = 0; i < 2; ++i) {
                if (self->wheelUntil_[i] && now >= self->wheelUntil_[i]) {
                    self->wheelUntil_[i] = 0; self->Input(WheelUp + i, false);
                }
                pending |= self->wheelUntil_[i] != 0;
            }
            if (!pending) KillTimer(hwnd, WheelTimer);
        }
        return 0;
    case WM_DISPLAYCHANGE:
    case WM_SETTINGCHANGE:
        self->overlay.Render(ResolvedSettings(self->settings), self->pressed, self->controllerState);
        self->UpdateVisibility(); return 0;
    case WM_WTSSESSION_CHANGE:
        if (wParam == WTS_SESSION_LOCK) sessionLocked = true;
        if (wParam == WTS_SESSION_UNLOCK) sessionLocked = false;
        self->ResetInputs(); self->UpdateVisibility(); return 0;
    case WM_POWERBROADCAST:
        self->ResetInputs(); self->UpdateVisibility(); return TRUE;
    case WM_QUERYENDSESSION: return TRUE;
    case WM_ENDSESSION: if (wParam) self->Quit(); return 0;
    case WM_CLOSE: self->Quit(); return 0;
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR commandLine, int) {
    int installerExit = 0;
    if (input_overlay::RunUpdateInstallerMode(installerExit)) return installerExit;
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"Local\\InputOverlay.Singleton");
    if (!mutex) return 1;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        HWND existing = FindWindowW(input_overlay::ControllerClass, input_overlay::AppName);
        if (existing && !wcsstr(commandLine, L"--background")) {
            DWORD pid = 0; GetWindowThreadProcessId(existing, &pid); AllowSetForegroundWindow(pid);
            PostMessageW(existing, input_overlay::WM_APP_SHOW_SETTINGS, 0, 0);
        }
        CloseHandle(mutex); return 0;
    }
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_STANDARD_CLASSES | ICC_BAR_CLASSES | ICC_LISTVIEW_CLASSES};
    InitCommonControlsEx(&controls);
    input_overlay::Application application;
    int result = 1;
    if (application.Initialize(instance, wcsstr(commandLine, L"--background") != nullptr)) {
        input_overlay::SignalUpdateStartupReady();
        result = application.Run();
    }
    application.Shutdown();
    CloseHandle(mutex);
    return result;
}
