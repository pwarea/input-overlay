#include "injected_input.hpp"

namespace input_overlay {
namespace { thread_local InjectedInput* current = nullptr; }
bool InjectedInput::Start(HWND target) {
    target_ = target;
    stop_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    changed_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    if (!stop_ || !changed_) { Stop(); return false; }
    thread_ = CreateThread(nullptr, 0, Worker, this, 0, nullptr);
    if (!thread_) { Stop(); return false; }
    return true;
}
void InjectedInput::SetActive(bool active, unsigned generation) {
    state_.store((generation << 1) | (active ? 1u : 0u));
    if (changed_) SetEvent(changed_);
}
void InjectedInput::Stop() {
    if (stop_) SetEvent(stop_);
    if (thread_) { WaitForSingleObject(thread_, INFINITE); CloseHandle(thread_); }
    if (stop_) CloseHandle(stop_);
    if (changed_) CloseHandle(changed_);
    thread_ = stop_ = changed_ = nullptr;
}
void InjectedInput::Post(int input, bool down) {
    if (!(workerState_ & 1)) return;
    PostMessageW(target_, WM_APP_INJECTED, input,
        static_cast<LPARAM>((workerState_ & ~1u) | (down ? 1u : 0u)));
}
LRESULT CALLBACK InjectedInput::Keyboard(int code, WPARAM message, LPARAM data) {
    if (code == HC_ACTION && current) {
        const auto& key = *reinterpret_cast<KBDLLHOOKSTRUCT*>(data);
        if (key.flags & LLKHF_INJECTED) {
            UINT vk = key.vkCode;
            if (vk == VK_SHIFT) vk = MapVirtualKeyW(key.scanCode, MAPVK_VSC_TO_VK_EX);
            if (vk == VK_CONTROL) vk = key.flags & LLKHF_EXTENDED ? VK_RCONTROL : VK_LCONTROL;
            if (vk == VK_MENU) vk = key.flags & LLKHF_EXTENDED ? VK_RMENU : VK_LMENU;
            current->Post(static_cast<int>(vk), !(key.flags & LLKHF_UP));
        }
    }
    return CallNextHookEx(nullptr, code, message, data);
}
DWORD WINAPI InjectedInput::Worker(void* context) {
    auto& self = *static_cast<InjectedInput*>(context);
    current = &self;
    HANDLE events[] = {self.stop_, self.changed_};
    HHOOK keyboard = nullptr;
    bool reportedError = false;
    bool running = true;
    while (running) {
        const DWORD wake = MsgWaitForMultipleObjects(2, events, FALSE, INFINITE, QS_ALLINPUT);
        if (wake == WAIT_OBJECT_0 || wake == WAIT_FAILED) break;
        if (wake == WAIT_OBJECT_0 + 1) {
            self.workerState_ = self.state_.load();
            if (self.workerState_ & 1) {
                if (!keyboard) keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, Keyboard, GetModuleHandleW(nullptr), 0);
                if (!keyboard && !reportedError) {
                    PostMessageW(self.target_, WM_APP_INPUT_ERROR, 0, 0);
                    reportedError = true;
                }
            } else {
                if (keyboard) UnhookWindowsHookEx(keyboard);
                keyboard = nullptr;
            }
        }
        MSG message{};
        while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
            if (message.message == WM_QUIT) { running = false; break; }
            TranslateMessage(&message); DispatchMessageW(&message);
        }
    }
    if (keyboard) UnhookWindowsHookEx(keyboard);
    current = nullptr;
    return 0;
}
}
