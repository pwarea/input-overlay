#pragma once
#include <array>
#include <cstdint>

namespace input_overlay {
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
    explicit ControllerProvider(Reader reader = nullptr, void* context = nullptr);
    ~ControllerProvider();
    ControllerProvider(const ControllerProvider&) = delete;
    ControllerProvider& operator=(const ControllerProvider&) = delete;
    ControllerState Poll(std::uint64_t now, int index, int deadzone);
    unsigned NextPollDelay(std::uint64_t now) const;
    void Reset();
private:
    bool Read(int index, ControllerRawState& raw);
    Reader reader_ = nullptr;
    void* context_ = nullptr;
    void* module_ = nullptr;
    void* readState_ = nullptr;
    bool loadAttempted_ = false;
    int selection_ = -2;
    ControllerState state_;
    std::array<std::uint64_t, 4> retryAfter_{};
    std::uint64_t nextPollAt_ = 0;
};
}
