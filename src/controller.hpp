#pragma once
#include <array>
#include <cstdint>
#include <memory>

namespace input_overlay {
inline constexpr int XInputSlotCount = 4;
inline constexpr int ControllerSlotCount = 8;
class DualShock4Provider;
struct ControllerRawState {
    std::uint16_t buttons = 0;
    std::uint8_t leftTrigger = 0, rightTrigger = 0;
    std::int16_t leftX = 0, leftY = 0, rightX = 0, rightY = 0;
};

struct ControllerState {
    bool connected = false;
    int index = -1;
    std::uint16_t buttons = 0;
    float leftX = 0, leftY = 0, rightX = 0, rightY = 0;
    float leftTrigger = 0, rightTrigger = 0;
    bool operator==(const ControllerState& other) const;
    bool operator!=(const ControllerState& other) const { return !(*this == other); }
};

ControllerState NormalizeController(const ControllerRawState& raw, int index, int deadzone);

class ControllerProvider {
public:
    using Reader = bool (*)(void*, int, ControllerRawState&);
    explicit ControllerProvider(Reader reader = nullptr, void* context = nullptr,
        Reader nativeReader = nullptr, void* nativeContext = nullptr);
    ~ControllerProvider();
    ControllerProvider(const ControllerProvider&) = delete;
    ControllerProvider& operator=(const ControllerProvider&) = delete;
    ControllerState Poll(std::uint64_t now, int index, int deadzone);
    unsigned NextPollDelay(std::uint64_t now) const;
    void Reset();
private:
    bool Read(int index, ControllerRawState& raw);
    std::uint64_t RetryDelay(int index) const;
    Reader reader_ = nullptr;
    void* context_ = nullptr;
    Reader nativeReader_ = nullptr;
    void* nativeContext_ = nullptr;
    std::unique_ptr<DualShock4Provider> native_;
    void* module_ = nullptr;
    void* readState_ = nullptr;
    bool loadAttempted_ = false;
    int selection_ = -2;
    ControllerState state_;
    std::array<std::uint64_t, ControllerSlotCount> retryAfter_{};
    std::uint64_t nextPollAt_ = 0;
};
}
