#pragma once
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <string>
#include <vector>
#include <cstdint>
#include <unordered_map>
#include "controller.hpp"
#include "updates.hpp"

#ifndef INPUT_OVERLAY_VERSION
#define INPUT_OVERLAY_VERSION "0.4.2"
#endif
#define INPUT_OVERLAY_WIDEN_IMPL(value) L##value
#define INPUT_OVERLAY_WIDEN(value) INPUT_OVERLAY_WIDEN_IMPL(value)

namespace input_overlay {
constexpr wchar_t AppName[] = L"Input Overlay";
constexpr wchar_t AppVersion[] = INPUT_OVERLAY_WIDEN(INPUT_OVERLAY_VERSION);
#undef INPUT_OVERLAY_WIDEN
#undef INPUT_OVERLAY_WIDEN_IMPL
constexpr wchar_t ControllerClass[] = L"InputOverlay.Controller";
constexpr UINT WM_APP_REFRESH = WM_APP + 1;
constexpr UINT WM_APP_FOREGROUND = WM_APP + 2;
constexpr UINT WM_APP_TRAY = WM_APP + 3;
constexpr UINT WM_APP_SHOW_SETTINGS = WM_APP + 4;
constexpr int InputNone = 0;
constexpr int MouseLeft = 256, MouseRight = 257, MouseMiddle = 258;
constexpr int MouseSide1 = 259, MouseSide2 = 260, WheelUp = 261, WheelDown = 262;
constexpr size_t InputCount = 263;
constexpr size_t KeyboardCount = 28, SlotCount = 35;
constexpr int OverlayDesignWidth = 510, OverlayDesignHeight = 220;
constexpr int OverlayBaseWidth = 357, OverlayBaseHeight = 154;
enum class OverlayStyle { Outline = 0, Neon = 1, Glass = 2, Circuit = 3, Pearl = 4, Gradient = 5 };
constexpr int OverlayStyleCount = 6;
enum class ColorTheme { Original = 0, Sunset = 1, Aurora = 2, Ocean = 3, Rose = 4, Custom = 5 };
constexpr int ColorThemeCount = 6;
enum class OverlayDevice { KeyboardMouse = 0, Controller = 1 };
enum class ControllerLayout { Xbox = 0, PlayStation = 1, DualShock4 = 2 };
constexpr int ControllerLayoutCount = 3;
enum class ControllerStyle { Air = 0, Frost = 1, Prism = 2, Original = 3 };
constexpr int ControllerStyleCount = 4;

struct Slot {
    std::wstring label;
    int input = 0;
    bool visible = true;
};
struct Settings {
    std::array<Slot, SlotCount> slots;
    int x = 32, y = 32, scale = 100, opacity = 92;
    COLORREF accent = RGB(125, 211, 252);
    ColorTheme colorTheme = ColorTheme::Original;
    COLORREF backgroundStart = RGB(255, 178, 91), backgroundEnd = RGB(192, 121, 242);
    int gradientFillOpacity = 16;
    OverlayStyle style = OverlayStyle::Pearl;
    OverlayDevice device = OverlayDevice::KeyboardMouse;
    ControllerLayout controllerLayout = ControllerLayout::Xbox;
    ControllerStyle controllerStyle = ControllerStyle::Frost;
    int originalOpacity = 100;
    COLORREF controllerAccent = CLR_INVALID;
    int controllerIndex = -1, controllerDeadzone = 15, controllerScale = 100;
    bool enabled = true, onlySelectedApps = false, showMouse = true;
    bool startMinimized = false;
    bool autoCheckUpdates = true;
    bool anchorRight = false, anchorBottom = true, isoLayout = false;
    std::wstring monitor;
    UINT hotkeyModifiers = MOD_CONTROL | MOD_ALT, hotkeyVk = VK_F10;
    std::vector<std::wstring> applications;
};
inline int EffectiveOverlayScale(const Settings& settings) {
    return settings.device == OverlayDevice::Controller ? settings.controllerScale : settings.scale;
}
inline int EffectiveOverlayOpacity(const Settings& settings) {
    return settings.device == OverlayDevice::Controller && settings.controllerStyle == ControllerStyle::Original
        ? settings.originalOpacity : settings.opacity;
}
struct WindowInfo { HWND hwnd = nullptr; std::wstring title, path; };

Settings DefaultSettings();
void DrawOverlayPreview(HDC dc, const RECT& bounds, const Settings& settings, bool pressed, bool lightBackground);
void NormalizeSettings(Settings& settings);
bool LoadSettings(const std::wstring& file, Settings& settings);
bool SaveSettings(const std::wstring& file, const Settings& settings);
void BindInput(Settings& settings, size_t slot, int input);
std::wstring InputName(int input);
std::wstring HotkeyName(UINT modifiers, UINT vk);
bool MatchesApplication(const Settings& settings, const std::wstring& path);
std::wstring ExecutablePath();
std::vector<WindowInfo> EnumerateApplications();
std::wstring ProcessPath(HWND window);
bool StartupEnabled();
bool SetStartup(bool enabled);

class Overlay {
public:
    bool Create(HINSTANCE instance, HWND owner);
    void Destroy();
    void Render(const Settings& settings, const std::array<bool, InputCount>& pressed,
        const ControllerState& controller = {});
    void SetVisible(bool visible);
    void SetEditing(bool editing);
    HWND Handle() const { return hwnd_; }
    bool Editing() const { return editing_; }
private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    HWND hwnd_ = nullptr, owner_ = nullptr;
    bool editing_ = false;
    POINT drag_{};
    ULONG_PTR gdiplusToken_ = 0;
    HDC memoryDC_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ oldBitmap_ = nullptr;
    void* pixels_ = nullptr;
    int width_ = 0, height_ = 0;
    bool visible_ = false, dragging_ = false;
};

class Application;
class SettingsWindow {
public:
    bool Show(Application& app);
    bool ShowUpdates(Application& app);
    void Destroy();
    void Refresh();
    void CaptureInput(int input);
    void CancelCapture();
    bool Capturing() const;
    HWND Handle() const { return hwnd_; }
private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    void Build();
    void SelectPage(int page);
    void UpdateControls();
    void Command(int id, int code);
    HWND hwnd_ = nullptr;
    Application* app_ = nullptr;
    int page_ = 0, captureSlot_ = -1;
    std::vector<HWND> controls_;
    std::vector<WindowInfo> windows_;
};

class Application {
public:
    Settings settings = DefaultSettings();
    Overlay overlay;
    SettingsWindow preferences;
    std::array<bool, InputCount> pressed{};
    ControllerState controllerState;
    HWND controller = nullptr;
    std::wstring configPath;
    UpdateResult updateResult;
    bool Initialize(HINSTANCE instance, bool quiet);
    int Run();
    void Shutdown();
    bool Save();
    void Changed();
    void Toggle();
    void UpdateVisibility();
    void SetPosition(int x, int y);
    void EditPosition(bool edit);
    bool SetHotkey(UINT modifiers, UINT vk);
    void ResetInputs();
    void ShowSettings();
    void CheckForUpdates(bool manual);
    void InstallUpdate();
    void Quit();
    bool Visible() const { return visible_; }
private:
    static LRESULT CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);
    static void CALLBACK ForegroundEvent(HWINEVENTHOOK, DWORD, HWND, LONG, LONG, DWORD, DWORD);
    void RawInput(HRAWINPUT handle);
    void Input(int code, bool down);
    void DeviceInput(HANDLE device, int code, bool down);
    void UpdateTray();
    void TrayMenu();
    void SyncInputRegistration();
    void SyncControllerPolling();
    void PollController();
    void RequestRender();
    void UpdateCompleted();
    void NotifyUpdate();
    HINSTANCE instance_ = nullptr;
    HWINEVENTHOOK foregroundHook_ = nullptr;
    UINT taskbarCreated_ = 0;
    bool visible_ = false, trayAdded_ = false, dirty_ = false;
    bool inputActive_ = false, shuttingDown_ = false;
    unsigned inputGeneration_ = 0;
    UINT captureIgnoreKey_ = 0;
    HICON icon_ = nullptr;
    std::unordered_map<HANDLE, std::array<bool, InputCount>> deviceInputs_;
    std::array<ULONGLONG, 2> wheelUntil_{};
    ControllerProvider controllerProvider_;
    unsigned controllerTimerInterval_ = 0;
    UpdateService updates_;
    bool manualUpdateCheck_ = false;
    bool updateInstallRequested_ = false;
    uint64_t lastUpdateGeneration_ = 0;
};
}
