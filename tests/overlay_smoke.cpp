#include "../src/app.hpp"
#include "../src/geometry.hpp"
#include <cstdio>

namespace {
int renders = 0;

bool Check(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "%s (Win32 error %lu)\n", message, GetLastError());
    return condition;
}

bool Exercise(input_overlay::OverlayStyle style, int device, bool allSizes) {
    input_overlay::Overlay overlay;
    if (!Check(overlay.Create(GetModuleHandleW(nullptr), nullptr), "Overlay creation failed")) return false;
    input_overlay::Settings settings;
    settings.style = style;
    settings.device = device == 0 ? input_overlay::OverlayDevice::KeyboardMouse : input_overlay::OverlayDevice::Controller;
    settings.controllerLayout = device == 2 ? input_overlay::ControllerLayout::PlayStation : input_overlay::ControllerLayout::Xbox;
    settings.x = -75;
    settings.y = 40;
    for (size_t i = 0; i < settings.slots.size(); ++i) {
        settings.slots[i].label = i < input_overlay::KeyboardCount ? L"W" : L"Mouse";
        settings.slots[i].input = static_cast<int>(i + 1);
    }
    std::array<bool, input_overlay::InputCount> pressed{};
    input_overlay::ControllerState controller;
    const auto render = [&] {
        overlay.Render(settings, pressed, controller);
        ++renders;
    };
    std::vector<int> sizes;
    if (allSizes) for (int percent = 10; percent <= 200; ++percent) sizes.push_back(percent);
    else sizes = {10, 50, 100, 200};
    for (const int percent : sizes) {
        settings.scale = percent;
        settings.isoLayout = (percent % 2 == 0);
        settings.showMouse = (percent % 3 != 0);
        pressed[static_cast<size_t>(percent) % input_overlay::SlotCount + 1] = true;
        controller.connected = percent % 3 != 0;
        controller.index = controller.connected ? percent % 4 : -1;
        controller.buttons = static_cast<std::uint16_t>(percent % 2 ? 0xf3ff : 0);
        controller.leftX = static_cast<float>(percent % 21 - 10) / 10.0f;
        controller.leftY = -controller.leftX;
        controller.rightX = -controller.leftX;
        controller.rightY = controller.leftX;
        controller.leftTrigger = static_cast<float>(percent % 101) / 100.0f;
        controller.rightTrigger = 1.0f - controller.leftTrigger;
        render();
        RECT rect{};
        GetWindowRect(overlay.Handle(), &rect);
        const SIZE expected = input_overlay::OverlaySize(percent);
        if (!Check(rect.right - rect.left == expected.cx && rect.bottom - rect.top == expected.cy,
                   "Rendered size differs from physical pixel geometry")) return false;
        if (!Check(rect.left == settings.x && rect.top == settings.y,
                   "Renderer moved the requested screen position")) return false;
        if (!Check(!IsWindowVisible(overlay.Handle()), "Hidden renderer unexpectedly became visible")) return false;
    }
    if (device != 0) {
        settings.scale = 100;
        controller = {};
        render();
        controller.connected = true;
        controller.index = 0;
        render();
        controller.buttons = 0xf3ff;
        controller.leftTrigger = controller.rightTrigger = 1.0f;
        controller.leftX = controller.leftY = 1.0f;
        controller.rightX = controller.rightY = -1.0f;
        render();
        controller.connected = false;
        render();
    }
    const LONG_PTR initialStyle = GetWindowLongPtrW(overlay.Handle(), GWL_EXSTYLE);
    const LONG_PTR required = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW | WS_EX_TOPMOST;
    if (!Check((initialStyle & required) == required, "Overlay window styles are incomplete")) return false;
    if (!Check(SendMessageW(overlay.Handle(), WM_NCHITTEST, 0, 0) == HTTRANSPARENT,
               "Normal mode does not pass hit tests through")) return false;
    overlay.SetEditing(true);
    render();
    if (!Check(!(GetWindowLongPtrW(overlay.Handle(), GWL_EXSTYLE) & WS_EX_TRANSPARENT),
               "Editing did not enable mouse interaction")) return false;
    if (!Check(SendMessageW(overlay.Handle(), WM_NCHITTEST, 0, 0) == HTCLIENT,
               "Editing does not accept drag hit tests")) return false;
    if (!Check(SendMessageW(overlay.Handle(), WM_MOUSEACTIVATE, 0, 0) == MA_NOACTIVATE,
               "Overlay can steal activation")) return false;
    overlay.SetEditing(false);
    if (!Check(GetWindowLongPtrW(overlay.Handle(), GWL_EXSTYLE) & WS_EX_TRANSPARENT,
               "Leaving edit mode did not restore clickthrough")) return false;
    overlay.Destroy();
    overlay.Destroy();
    return Check(!overlay.Handle(), "Overlay handle survived destruction");
}
}

int main() {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    for (int device = 0; device < 3; ++device)
        for (int style = 0; style < input_overlay::OverlayStyleCount; ++style)
            if (!Exercise(static_cast<input_overlay::OverlayStyle>(style), device, true)) return 1;
    const DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD windowsBefore = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int cycle = 0; cycle < 5; ++cycle)
        for (int device = 0; device < 3; ++device)
            for (int style = 0; style < input_overlay::OverlayStyleCount; ++style)
                if (!Exercise(static_cast<input_overlay::OverlayStyle>(style), device, false)) return 1;
    const DWORD after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD windowsAfter = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    if (!Check(after <= before, "GDI objects leaked across renderer lifecycles")) return 1;
    if (!Check(windowsAfter <= windowsBefore, "USER objects leaked across renderer lifecycles")) return 1;
    std::printf("Overlay smoke passed: %d hidden renders, all styles and 10-200%% sizes, keyboard/mouse and both controller layouts, connected/disconnected/pressed states, input/edit styles, GDI %lu -> %lu, USER %lu -> %lu.\n",
                renders, before, after, windowsBefore, windowsAfter);
    return 0;
}
