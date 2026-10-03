#include "../src/color_picker.cpp"
#include <cstdio>

namespace {
bool Check(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "%s (Win32 error %lu)\n", message, GetLastError());
    return condition;
}
}

int main() {
    using namespace input_overlay;
    for (int red = 0; red <= 255; red += 17)
        for (int green = 0; green <= 255; green += 17)
            for (int blue = 0; blue <= 255; blue += 17) {
                const COLORREF color = RGB(red, green, blue);
                if (!Check(FromHsv(ToHsv(color)) == color, "HEX colors must survive HSV conversion")) return 1;
            }
    const HINSTANCE instance = GetModuleHandleW(nullptr);
    if (!Check(RegisterPickerClasses(instance), "Picker classes must register")) return 1;
    DWORD baselineGdi = 0, baselineUser = 0;
    for (int iteration = 0; iteration < 30; ++iteration) {
        {
            Picker picker;
            picker.original = picker.color = RGB(125, 211, 252);
            picker.hsv = ToHsv(picker.color);
            HWND window = CreateWindowExW(PickerExStyle, PickerClass, L"Hidden picker test", PickerStyle,
                0, 0, 400, 600, nullptr, nullptr, instance, &picker);
            if (!Check(window != nullptr, "Hidden picker must open")) return 1;
            picker.dpi = iteration % 2 ? 144 : 96;
            Layout(picker);
            SetWindowTextW(picker.edit, L"ff8000");
            if (!Check(picker.valid && picker.color == RGB(255, 128, 0) && IsWindowEnabled(picker.apply),
                "Six HEX digits without a prefix must be accepted")) return 1;
            SetWindowTextW(picker.edit, L"#gg1234");
            SendMessageW(window, WM_COMMAND, IDOK, 0);
            if (!Check(!picker.valid && !IsWindowEnabled(picker.apply) && !picker.accepted && !picker.done &&
                picker.color == RGB(255, 128, 0), "Invalid HEX must not replace or commit the last valid color")) return 1;
            SendMessageW(picker.hueBar, WM_KEYDOWN, VK_HOME, 0);
            if (!Check(picker.valid && IsWindowEnabled(picker.apply), "Hue adjustment must recover from invalid HEX")) return 1;
            SetWindowTextW(picker.edit, L"#000000");
            SendMessageW(picker.hueBar, WM_KEYDOWN, VK_DOWN, 0);
            if (!Check(picker.valid && picker.color == RGB(0, 0, 0), "Changing hue must preserve zero brightness")) return 1;
            SetWindowTextW(picker.edit, L"#aBcDeF");
            if (!Check(picker.valid && picker.color == RGB(171, 205, 239), "Mixed-case HEX must be accepted")) return 1;
            const bool accept = iteration % 2 != 0;
            SendMessageW(window, WM_COMMAND, accept ? IDOK : IDCANCEL, 0);
            if (!Check(picker.accepted == accept && picker.done && !picker.window,
                "Only Apply may accept a color; Apply and Cancel must close the picker")) return 1;
        }
        if (iteration == 9) {
            baselineGdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
            baselineUser = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
        }
    }
    const DWORD afterGdi = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
    const DWORD afterUser = GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS);
    if (!Check(afterGdi <= baselineGdi && afterUser <= baselineUser, "Picker lifecycles must not leak GDI or USER objects")) return 1;
    std::printf("Color picker passed: 4096 HSV round trips, HEX validation, accept/cancel, DPI, GDI %lu -> %lu, USER %lu -> %lu.\n",
        baselineGdi, afterGdi, baselineUser, afterUser);
    return 0;
}
