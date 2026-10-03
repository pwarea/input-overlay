#include "injected_input.hpp"
#include <cstdio>
#include <cstdlib>

int main() {
    HWND target = CreateWindowExW(0, L"STATIC", L"Input Overlay worker test", 0,
        0, 0, 0, 0, HWND_MESSAGE, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!target) { std::fputs("FAILED: create message-only test target\n", stderr); return EXIT_FAILURE; }
    DWORD baseline = 0;
    GetProcessHandleCount(GetCurrentProcess(), &baseline);
    bool passed = true;
    for (unsigned cycle = 0; cycle < 24 && passed; ++cycle) {
        input_overlay::InjectedInput input;
        const ULONGLONG started = GetTickCount64();
        if (!input.Start(target)) {
            std::fputs("FAILED: worker start\n", stderr);
            passed = false;
            break;
        }
        input.SetActive(true, cycle * 4 + 1);
        Sleep(10);
        input.SetActive(false, cycle * 4 + 2);
        input.SetActive(true, cycle * 4 + 3);
        input.SetActive(false, cycle * 4 + 4);
        input.Stop();
        input.Stop();
        if (GetTickCount64() - started > 5000) {
            std::fputs("FAILED: worker shutdown exceeded five seconds\n", stderr);
            passed = false;
        }
    }
    DWORD after = 0;
    if (!GetProcessHandleCount(GetCurrentProcess(), &after) || after > baseline + 2) {
        std::fprintf(stderr, "FAILED: worker lifecycle leaked handles (%lu -> %lu)\n", baseline, after);
        passed = false;
    }
    DestroyWindow(target);
    if (passed) std::puts("Injected worker lifecycle tests passed (no key or mouse injection).");
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
