#include "../src/app.hpp"
#include "../src/geometry.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace {
int renders = 0;

bool Check(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "%s (Win32 error %lu)\n", message, GetLastError());
    return condition;
}

bool PreviewChecks() {
    input_overlay::Overlay runtime;
    if (!Check(runtime.Create(GetModuleHandleW(nullptr), nullptr), "Preview graphics startup failed")) return false;
    constexpr int width = 612, height = 264;
    HDC dc = CreateCompatibleDC(nullptr);
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* data = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &data, nullptr, 0);
    if (!dc || !bitmap) {
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        runtime.Destroy();
        return Check(false, "Preview test surface allocation failed");
    }
    HGDIOBJ previous = SelectObject(dc, bitmap);
    const RECT bounds{0, 0, width, height};
    auto* pixels = static_cast<std::uint32_t*>(data);
    input_overlay::Settings settings;
    settings.opacity = 100;
    settings.showMouse = false;
    for (auto& slot : settings.slots) slot.visible = false;
    settings.slots[8].visible = true;
    settings.slots[8].input = 'W';
    const auto draw = [&](bool pressed = false, bool light = false) {
        input_overlay::DrawOverlayPreview(dc, bounds, settings, pressed, light);
        GdiFlush();
        return std::vector<std::uint32_t>(pixels, pixels + width * height);
    };
    bool ok = true;
    for (int device = 0; device < 3; ++device) {
        settings.device = device == 0 ? input_overlay::OverlayDevice::KeyboardMouse : input_overlay::OverlayDevice::Controller;
        settings.controllerLayout = device == 2 ? input_overlay::ControllerLayout::PlayStation : input_overlay::ControllerLayout::Xbox;
        for (int style = 0; style < static_cast<int>(input_overlay::OverlayStyle::Gradient); ++style) {
            settings.style = static_cast<input_overlay::OverlayStyle>(style);
            for (const bool pressed : {false, true}) {
                settings.colorTheme = input_overlay::ColorTheme::Original;
                settings.gradientFillOpacity = 4;
                const auto original = draw(pressed);
                settings.colorTheme = input_overlay::ColorTheme::Custom;
                settings.backgroundStart = RGB(255, 0, 0);
                settings.backgroundEnd = RGB(0, 255, 0);
                settings.gradientFillOpacity = 45;
                ok = Check(original == draw(pressed), "Gradient settings changed an existing style") && ok;
            }
        }
    }
    settings.device = input_overlay::OverlayDevice::KeyboardMouse;
    settings.style = input_overlay::OverlayStyle::Gradient;
    settings.colorTheme = input_overlay::ColorTheme::Sunset;
    settings.slots[8].visible = false;
    const auto scene = draw();
    settings.slots[8].visible = true;
    settings.gradientFillOpacity = 4;
    const auto faint = draw();
    settings.gradientFillOpacity = 16;
    const auto normal = draw();
    settings.gradientFillOpacity = 45;
    const auto strong = draw();
    const auto pressed = draw(true);
    const float scale = std::min((width - 24.0f) / input_overlay::OverlayDesignWidth,
        (height - 24.0f) / input_overlay::OverlayDesignHeight);
    const auto index = [scale](float x, float y) {
        const int left = static_cast<int>((width - std::ceil(input_overlay::OverlayDesignWidth * scale)) * 0.5f + x * scale);
        const int top = static_cast<int>((height - std::ceil(input_overlay::OverlayDesignHeight * scale)) * 0.5f + y * scale);
        return static_cast<size_t>(top * width + left);
    };
    const size_t interior = index(158.0f, 58.0f), gap = index(144.0f, 60.0f);
    const auto red = [interior](const std::vector<std::uint32_t>& snapshot) { return (snapshot[interior] >> 16) & 255; };
    ok = Check(red(scene) < red(faint) && red(faint) < red(normal) && red(normal) < red(strong) && red(strong) < red(pressed),
        "Gradient fill opacity or pressed response is not visible") && ok;
    ok = Check(scene[gap] == normal[gap] && scene[0] == normal[0], "Gradient filled a transparent gap") && ok;
    settings.gradientFillOpacity = 16;
    settings.accent = RGB(0, 255, 0);
    ok = Check(normal == draw(), "Pressed accent changed the Gradient palette") && ok;
    ok = Check(normal == draw(), "Gradient preview changed without an input or setting change") && ok;
    ok = Check(normal != draw(false, true), "Light preview background has no effect") && ok;
    settings.opacity = 15;
    ok = Check(red(draw()) < red(normal), "Preview ignores overall overlay opacity") && ok;
    const DWORD gdiBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD userBefore = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int i = 0; i < 60; ++i) {
        settings.gradientFillOpacity = 4 + i % 42;
        input_overlay::DrawOverlayPreview(dc, bounds, settings, i % 2 != 0, i % 3 == 0);
    }
    ok = Check(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) <= gdiBefore,
        "Preview repaints leaked GDI resources") && ok;
    ok = Check(GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS) <= userBefore,
        "Preview created or leaked windows") && ok;
    SelectObject(dc, previous);
    DeleteObject(bitmap);
    DeleteDC(dc);
    runtime.Destroy();
    return ok;
}

bool Exercise(input_overlay::OverlayStyle style, int device, bool allSizes,
              input_overlay::ColorTheme colorTheme = input_overlay::ColorTheme::Original) {
    input_overlay::Overlay overlay;
    if (!Check(overlay.Create(GetModuleHandleW(nullptr), nullptr), "Overlay creation failed")) return false;
    input_overlay::Settings settings;
    settings.style = style;
    settings.colorTheme = colorTheme;
    settings.backgroundStart = RGB(0, 0, 0);
    settings.backgroundEnd = RGB(255, 255, 255);
    settings.gradientFillOpacity = allSizes ? 16 : 45;
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
    if (!PreviewChecks()) return 1;
    for (int device = 0; device < 3; ++device)
        for (int style = 0; style < input_overlay::OverlayStyleCount; ++style)
            if (!Exercise(static_cast<input_overlay::OverlayStyle>(style), device, true)) return 1;
    const DWORD before = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD windowsBefore = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    for (int cycle = 0; cycle < 5; ++cycle)
        for (int device = 0; device < 3; ++device)
            for (int style = 0; style < input_overlay::OverlayStyleCount; ++style)
                if (!Exercise(static_cast<input_overlay::OverlayStyle>(style), device, false,
                              static_cast<input_overlay::ColorTheme>(cycle + 1))) return 1;
    const DWORD after = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD windowsAfter = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    if (!Check(after <= before, "GDI objects leaked across renderer lifecycles")) return 1;
    if (!Check(windowsAfter <= windowsBefore, "USER objects leaked across renderer lifecycles")) return 1;
    std::printf("Overlay smoke passed: %d hidden renders, all styles and color themes, 10-200%% sizes, keyboard/mouse and both controller layouts, connected/disconnected/pressed states, input/edit styles, GDI %lu -> %lu, USER %lu -> %lu.\n",
                renders, before, after, windowsBefore, windowsAfter);
    return 0;
}
