#include "controller.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <xinput.h>
#include <algorithm>
#include <cmath>

namespace input_overlay {
namespace {
constexpr std::uint64_t ActiveInterval = 16, RetryInterval = 2000;

float Quantize(float value) {
    return std::round(value * 100.0f) / 100.0f;
}

void NormalizeStick(std::int16_t rawX, std::int16_t rawY, int deadzone, float& x, float& y) {
    const float horizontal = rawX / (rawX < 0 ? 32768.0f : 32767.0f);
    const float vertical = rawY / (rawY < 0 ? 32768.0f : 32767.0f);
    const float magnitude = std::sqrt(horizontal * horizontal + vertical * vertical);
    const float threshold = std::clamp(deadzone, 0, 40) / 100.0f;
    if (magnitude <= threshold) { x = y = 0; return; }
    const float distance = (std::min(magnitude, 1.0f) - threshold) / (1.0f - threshold);
    x = Quantize(horizontal / magnitude * distance);
    y = Quantize(vertical / magnitude * distance);
}

float NormalizeTrigger(std::uint8_t value) {
    if (value <= XINPUT_GAMEPAD_TRIGGER_THRESHOLD) return 0;
    return Quantize((value - XINPUT_GAMEPAD_TRIGGER_THRESHOLD) /
        static_cast<float>(255 - XINPUT_GAMEPAD_TRIGGER_THRESHOLD));
}
}

bool ControllerState::operator==(const ControllerState& other) const {
    return connected == other.connected && index == other.index && buttons == other.buttons &&
        leftX == other.leftX && leftY == other.leftY && rightX == other.rightX && rightY == other.rightY &&
        leftTrigger == other.leftTrigger && rightTrigger == other.rightTrigger;
}

ControllerState NormalizeController(const ControllerRawState& raw, int index, int deadzone) {
    ControllerState result;
    if (index < 0 || index > 3) return result;
    result.connected = true;
    result.index = index;
    result.buttons = raw.buttons & 0xf3ff;
    NormalizeStick(raw.leftX, raw.leftY, deadzone, result.leftX, result.leftY);
    NormalizeStick(raw.rightX, raw.rightY, deadzone, result.rightX, result.rightY);
    result.leftTrigger = NormalizeTrigger(raw.leftTrigger);
    result.rightTrigger = NormalizeTrigger(raw.rightTrigger);
    return result;
}

ControllerProvider::ControllerProvider(Reader reader, void* context) : reader_(reader), context_(context) {}

ControllerProvider::~ControllerProvider() {
    if (module_) FreeLibrary(static_cast<HMODULE>(module_));
}

bool ControllerProvider::Read(int index, ControllerRawState& raw) {
    if (reader_) return reader_(context_, index, raw);
    if (!loadAttempted_) {
        loadAttempted_ = true;
        for (const auto name : {L"xinput1_4.dll", L"xinput9_1_0.dll"}) {
            auto module = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!module) continue;
            auto function = GetProcAddress(module, "XInputGetState");
            if (function) {
                module_ = module;
                readState_ = reinterpret_cast<void*>(function);
                break;
            }
            FreeLibrary(module);
        }
    }
    if (!readState_) return false;
    using GetState = DWORD (WINAPI*)(DWORD, XINPUT_STATE*);
    XINPUT_STATE state{};
    if (reinterpret_cast<GetState>(readState_)(static_cast<DWORD>(index), &state) != ERROR_SUCCESS) return false;
    const auto& gamepad = state.Gamepad;
    raw = {gamepad.wButtons, gamepad.bLeftTrigger, gamepad.bRightTrigger,
        gamepad.sThumbLX, gamepad.sThumbLY, gamepad.sThumbRX, gamepad.sThumbRY};
    return true;
}

ControllerState ControllerProvider::Poll(std::uint64_t now, int index, int deadzone) {
    if (index < -1 || index > 3) index = -1;
    if (selection_ != index) {
        Reset();
        selection_ = index;
    }
    if (now < nextPollAt_) return state_;
    ControllerRawState raw;
    if (state_.connected) {
        const int active = state_.index;
        if (Read(active, raw)) {
            state_ = NormalizeController(raw, active, deadzone);
            nextPollAt_ = now + ActiveInterval;
            return state_;
        }
        retryAfter_[active] = now + RetryInterval;
        state_ = {};
    }
    const int first = index < 0 ? 0 : index, last = index < 0 ? 3 : index;
    for (int candidate = first; candidate <= last; ++candidate) {
        if (now < retryAfter_[candidate]) continue;
        if (Read(candidate, raw)) {
            state_ = NormalizeController(raw, candidate, deadzone);
            nextPollAt_ = now + ActiveInterval;
            return state_;
        }
        retryAfter_[candidate] = now + RetryInterval;
    }
    nextPollAt_ = *std::min_element(retryAfter_.begin() + first, retryAfter_.begin() + last + 1);
    return state_;
}

unsigned ControllerProvider::NextPollDelay(std::uint64_t now) const {
    if (nextPollAt_ <= now) return static_cast<unsigned>(ActiveInterval);
    return static_cast<unsigned>(std::clamp(nextPollAt_ - now, ActiveInterval, RetryInterval));
}

void ControllerProvider::Reset() {
    state_ = {};
    selection_ = -2;
    retryAfter_.fill(0);
    nextPollAt_ = 0;
}
}
