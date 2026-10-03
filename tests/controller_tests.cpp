#include "controller.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct FakeControllers {
    std::array<bool, 4> connected{};
    std::array<input_overlay::ControllerRawState, 4> state{};
    std::array<int, 4> calls{};
    static bool Read(void* context, int index, input_overlay::ControllerRawState& result) {
        auto& self = *static_cast<FakeControllers*>(context);
        Check(index >= 0 && index <= 3, "Reader must only receive valid XInput indices");
        ++self.calls[index];
        if (!self.connected[index]) return false;
        result = self.state[index];
        return true;
    }
};

void TestNormalization() {
    input_overlay::ControllerRawState raw;
    auto state = input_overlay::NormalizeController(raw, 0, 15);
    Check(state.connected && state.index == 0 && state.buttons == 0 && state.leftX == 0 &&
        state.rightY == 0 && state.leftTrigger == 0, "A connected resting controller must have a neutral state");
    raw.leftX = 3000; raw.leftY = -3000;
    raw.rightX = -4915; raw.rightY = 0;
    raw.leftTrigger = 30; raw.rightTrigger = 15;
    Check(input_overlay::NormalizeController(raw, 0, 15) == state, "Stick and trigger drift inside their deadzones must not alter the visual state");
    raw.leftX = 20000; raw.leftY = 0;
    auto moved = input_overlay::NormalizeController(raw, 0, 15);
    Check(moved.leftX > 0 && moved.leftX < 1 && moved.leftY == 0,
        "Outside the deadzone, the stick must have proportional travel");
    raw.leftX = 20001;
    Check(input_overlay::NormalizeController(raw, 0, 15) == moved, "Subpixel raw jitter must not trigger a different visual state");
    raw = {};
    raw.leftX = std::numeric_limits<std::int16_t>::min();
    raw.rightX = std::numeric_limits<std::int16_t>::max();
    raw.leftTrigger = 255; raw.rightTrigger = 255; raw.buttons = 0xffff;
    state = input_overlay::NormalizeController(raw, 3, 15);
    Check(state.leftX == -1 && state.rightX == 1 && state.leftTrigger == 1 && state.rightTrigger == 1,
        "Asymmetric signed stick endpoints and full triggers must reach their exact limits");
    Check(state.buttons == 0xf3ff && state.index == 3, "Only documented button bits must be exposed");
    raw.leftY = std::numeric_limits<std::int16_t>::max();
    state = input_overlay::NormalizeController(raw, 3, 15);
    Check(state.leftX < -0.70f && state.leftY > 0.70f &&
        std::hypot(state.leftX, state.leftY) <= 1.01f, "Diagonal travel must retain direction and clamp circular magnitude");
    raw = {};
    raw.leftX = 10000;
    Check(input_overlay::NormalizeController(raw, 0, -100) == input_overlay::NormalizeController(raw, 0, 0),
        "Negative deadzone must safely clamp to zero");
    Check(input_overlay::NormalizeController(raw, 0, 100) == input_overlay::NormalizeController(raw, 0, 40),
        "Oversized deadzone must safely clamp to forty percent");
    for (const int index : {-2, -1, 4, 100}) {
        Check(input_overlay::NormalizeController(raw, index, 15) == input_overlay::ControllerState{},
            "An invalid physical index must normalize to a fully disconnected state");
    }
}

void TestAutomaticSelectionAndRetry() {
    FakeControllers fake;
    input_overlay::ControllerProvider provider(FakeControllers::Read, &fake);
    auto state = provider.Poll(0, -1, 15);
    Check(state == input_overlay::ControllerState{} && fake.calls == std::array<int, 4>{1, 1, 1, 1},
        "Auto selection must check every slot once when all controllers are absent");
    Check(provider.NextPollDelay(0) == 2000, "Absent controllers must schedule a two-second retry");
    fake.connected[2] = true;
    fake.state[2].buttons = 0x1000;
    provider.Poll(16, -1, 15);
    provider.Poll(1999, -1, 15);
    Check(fake.calls == std::array<int, 4>{1, 1, 1, 1}, "Absent slots must not be polled during the retry delay");
    state = provider.Poll(2000, -1, 15);
    Check(state.connected && state.index == 2 && state.buttons == 0x1000 &&
        fake.calls == std::array<int, 4>{2, 2, 2, 1}, "Auto mode must connect at the retry boundary and stop searching after a match");
    Check(provider.NextPollDelay(2000) == 16, "A connected controller must schedule normal input polling");
    Check(provider.Poll(2001, -1, 15) == state && fake.calls[2] == 2,
        "Early ticks must reuse the current state without an extra hardware read");
    Check(provider.Poll(2016, -1, 15) == state && fake.calls == std::array<int, 4>{2, 2, 3, 1},
        "An unchanged active controller must retain identical visual state and leave empty slots alone");
    fake.connected[2] = false;
    fake.connected[3] = true;
    state = provider.Poll(2032, -1, 15);
    Check(state.connected && state.index == 3 && state.buttons == 0,
        "Disconnecting an active controller must clear its held inputs and permit failover");
    Check(fake.calls == std::array<int, 4>{2, 2, 4, 2},
        "Failover must respect prior empty-slot backoff and never read the disconnected slot twice");
    fake.connected[3] = false;
    state = provider.Poll(2048, -1, 15);
    Check(state == input_overlay::ControllerState{}, "Losing the final connected controller must clear buttons, axes and index");
    Check(provider.NextPollDelay(2048) == 1952, "Retry scheduling must use the earliest eligible slot");
}

void TestExplicitSelectionAndReset() {
    FakeControllers fake;
    fake.connected[1] = true;
    fake.state[1].buttons = 0x2000;
    input_overlay::ControllerProvider provider(FakeControllers::Read, &fake);
    Check(provider.Poll(100, 0, 15) == input_overlay::ControllerState{} && fake.calls == std::array<int, 4>{1, 0, 0, 0},
        "An explicitly selected missing controller must never fall back to another player");
    fake.connected[0] = true;
    fake.state[0].leftTrigger = 255;
    Check(provider.Poll(2099, 0, 15) == input_overlay::ControllerState{} && fake.calls[0] == 1,
        "An explicit reconnect must honor the missing-slot retry delay");
    auto state = provider.Poll(2100, 0, 15);
    Check(state.connected && state.index == 0 && state.leftTrigger == 1,
        "An explicitly selected controller must reconnect after the retry delay");
    state = provider.Poll(2101, 1, 15);
    Check(state.connected && state.index == 1 && state.leftTrigger == 0 && state.buttons == 0x2000,
        "Changing selection must immediately replace all previous controller inputs");
    fake.connected[1] = false;
    Check(provider.Poll(2117, 1, 15) == input_overlay::ControllerState{}, "Explicit disconnect must clear all visual state");
    fake.connected[1] = true;
    provider.Reset();
    state = provider.Poll(2118, 1, 15);
    Check(state.connected && state.index == 1, "Reset for a newly visible overlay must allow an immediate fresh connection");
    state = provider.Poll(2119, 99, 15);
    Check(state.connected && state.index == 0, "Invalid configured indices must safely use automatic selection");
}
}

int main() {
    try {
        TestNormalization();
        TestAutomaticSelectionAndRetry();
        TestExplicitSelectionAndReset();
        std::puts("All controller tests passed.");
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
