#pragma once
#include "app.hpp"
#include <atomic>

namespace input_overlay {
constexpr UINT WM_APP_INJECTED = WM_APP + 11;
constexpr UINT WM_APP_INPUT_ERROR = WM_APP + 12;
class InjectedInput {
public:
    bool Start(HWND target);
    void SetActive(bool active, unsigned generation);
    void Stop();
private:
    static DWORD WINAPI Worker(void* context);
    static LRESULT CALLBACK Keyboard(int code, WPARAM message, LPARAM data);
    void Post(int input, bool down);
    HWND target_ = nullptr;
    HANDLE thread_ = nullptr, stop_ = nullptr, changed_ = nullptr;
    std::atomic<unsigned> state_{0};
    unsigned workerState_ = 0;
};
}
