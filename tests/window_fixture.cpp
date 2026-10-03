#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <string>

namespace {
unsigned clicks = 0;
bool fullscreen = false;
RECT restored{};
LRESULT CALLBACK WindowProc(HWND hwnd, UINT message, WPARAM key, LPARAM data) {
    switch (message) {
    case WM_LBUTTONDOWN:
        ++clicks;
        SetWindowTextW(hwnd, (L"Input Overlay Test Window - clicks: " + std::to_wstring(clicks)).c_str());
        InvalidateRect(hwnd, nullptr, TRUE); return 0;
    case WM_KEYDOWN:
        if (key == VK_F11) {
            fullscreen = !fullscreen;
            if (fullscreen) {
                GetWindowRect(hwnd, &restored);
                MONITORINFO info{}; info.cbSize = sizeof(info);
                GetMonitorInfoW(MonitorFromWindow(hwnd, MONITOR_DEFAULTTOPRIMARY), &info);
                SetWindowLongPtrW(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
                SetWindowPos(hwnd, nullptr, info.rcMonitor.left, info.rcMonitor.top,
                    info.rcMonitor.right - info.rcMonitor.left, info.rcMonitor.bottom - info.rcMonitor.top, SWP_FRAMECHANGED);
            } else {
                SetWindowLongPtrW(hwnd, GWL_STYLE, WS_OVERLAPPEDWINDOW | WS_VISIBLE);
                SetWindowPos(hwnd, nullptr, restored.left, restored.top, restored.right - restored.left,
                    restored.bottom - restored.top, SWP_FRAMECHANGED);
            }
            return 0;
        }
        break;
    case WM_PAINT: {
        PAINTSTRUCT paint{}; HDC dc = BeginPaint(hwnd, &paint);
        RECT bounds{}; GetClientRect(hwnd, &bounds);
        HBRUSH background = CreateSolidBrush(RGB(21, 28, 38)); FillRect(dc, &bounds, background); DeleteObject(background);
        SetBkMode(dc, TRANSPARENT); SetTextColor(dc, RGB(226, 238, 249));
        bounds.left = 50; bounds.top = 60;
        auto text = L"Input Overlay integration fixture\n\nF11: borderless fullscreen\nClick anywhere: verify clicks pass through the overlay\n\nClicks received: " + std::to_wstring(clicks);
        DrawTextW(dc, text.c_str(), -1, &bounds, DT_LEFT | DT_WORDBREAK);
        EndPaint(hwnd, &paint); return 0;
    }
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd, message, key, data);
}
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int) {
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    WNDCLASSW type{}; type.lpfnWndProc = WindowProc; type.hInstance = instance;
    type.hCursor = LoadCursorW(nullptr, IDC_ARROW); type.lpszClassName = L"InputOverlay.TestFixture";
    RegisterClassW(&type);
    HWND hwnd = CreateWindowW(type.lpszClassName, L"Input Overlay Test Window - clicks: 0", WS_OVERLAPPEDWINDOW,
        20, 20, 1000, 720, nullptr, nullptr, instance, nullptr);
    ShowWindow(hwnd, SW_SHOW);
    MSG message{};
    while (GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
    return 0;
}
