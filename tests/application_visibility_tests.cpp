#include "app.hpp"
#include <commctrl.h>
#include <cstdio>
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

void RunTests() {
    Desktop desktop;
    TemporaryDirectory directory;
    AllowedProcess allowed(desktop, directory);
    Harness harness(directory, allowed);
    auto& app = harness.app;
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
        SendMessageW(app.preferences.Handle(), WM_COMMAND, MAKEWPARAM(220 + style, BN_CLICKED), 0);
        app.settings.scale = 40 + style * 35;
        app.settings.opacity = 35 + style * 10;
        app.Changed();
        harness.Expect(false, false, "Appearance changes must preserve application filtering");
    }
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
        std::puts("Application visibility passed: isolated desktop, simulated foreground events, real HWND visibility, appearance changes, position preview lifecycle, and manual hide.");
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "%s (Win32 error %lu)\n", error.what(), GetLastError());
        return 1;
    }
}
