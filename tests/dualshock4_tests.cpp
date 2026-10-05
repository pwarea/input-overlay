#include "dualshock4.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <tlhelp32.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using input_overlay::ControllerRawState;

void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

bool Equal(const ControllerRawState& first, const ControllerRawState& second) {
    return first.buttons == second.buttons && first.leftTrigger == second.leftTrigger &&
        first.rightTrigger == second.rightTrigger && first.leftX == second.leftX &&
        first.leftY == second.leftY && first.rightX == second.rightX && first.rightY == second.rightY;
}

std::vector<std::uint8_t> BasicReport(std::size_t size = 64) {
    std::vector<std::uint8_t> bytes(size);
    bytes[0] = 0x01;
    std::fill(bytes.begin() + 1, bytes.begin() + 5, 128);
    bytes[5] = 8;
    return bytes;
}

std::vector<std::uint8_t> BluetoothReport(std::uint8_t identifier = 0x11, std::size_t size = 78) {
    constexpr std::array<std::uint32_t, 9> checksums{
        0xa09a9936u, 0x523bbc9au, 0xb5745dc1u, 0x6c08f183u, 0x8b4710d8u,
        0x79e63574u, 0x9ea9d42fu, 0x106e6bb1u, 0xf7218aeau};
    std::vector<std::uint8_t> bytes(size);
    bytes[0] = identifier;
    bytes[1] = 0x80;
    std::fill(bytes.begin() + 3, bytes.begin() + 7, 128);
    bytes[7] = 8;
    const auto checksum = checksums.at(identifier - 0x11);
    for (std::size_t index = 0; index < 4; ++index)
        bytes[74 + index] = static_cast<std::uint8_t>(checksum >> (8 * index));
    return bytes;
}

ControllerRawState Parse(const std::vector<std::uint8_t>& bytes) {
    ControllerRawState state{0xf3ff, 255, 255, -32768, 32767, -32768, 32767};
    Check(input_overlay::ParseDualShock4Report(bytes.data(), bytes.size(), state),
        "A valid DualShock 4 report must parse");
    return state;
}

void Reject(const std::uint8_t* bytes, std::size_t size) {
    const ControllerRawState sentinel{0x1234, 51, 93, -129, 411, 17001, -3199};
    auto state = sentinel;
    Check(!input_overlay::ParseDualShock4Report(bytes, size, state),
        "An invalid DualShock 4 report must be rejected");
    Check(Equal(state, sentinel), "Rejected packets must not partially replace the last valid state");
}

void TestBasicReports() {
    for (const std::size_t size : {10u, 64u, 78u, 128u, 1024u}) {
        auto bytes = BasicReport(size);
        Check(Equal(Parse(bytes), {}), "USB, basic Bluetooth and padded reports must share a neutral mapping");
        bytes[5] = 0xf1;
        bytes[6] = 0xf3;
        bytes[7] = 0xff;
        bytes[8] = 87;
        bytes[9] = 212;
        const auto state = Parse(bytes);
        Check(state.buttons == 0xf3f9 && state.leftTrigger == 87 && state.rightTrigger == 212,
            "Padded basic reports must preserve simultaneous buttons and proportional triggers");
    }
    auto bytes = BasicReport(1025);
    Reject(nullptr, 0);
    Reject(nullptr, 64);
    for (std::size_t size = 0; size < 64; ++size) {
        if (size != 10) Reject(bytes.data(), size);
    }
    Reject(bytes.data(), bytes.size());
    for (const std::uint8_t identifier : {0x00, 0x02, 0x10, 0x1a, 0x31, 0xff}) {
        bytes[0] = identifier;
        Reject(bytes.data(), 64);
    }
}

void TestButtonsAndHats() {
    struct ButtonCase { std::size_t byte; std::uint8_t input; std::uint16_t output; };
    constexpr std::array<ButtonCase, 10> buttons{{
        {5, 0x10, 0x4000}, {5, 0x20, 0x1000}, {5, 0x40, 0x2000}, {5, 0x80, 0x8000},
        {6, 0x01, 0x0100}, {6, 0x02, 0x0200}, {6, 0x10, 0x0020}, {6, 0x20, 0x0010},
        {6, 0x40, 0x0040}, {6, 0x80, 0x0080}}};
    for (const auto& button : buttons) {
        auto bytes = BasicReport();
        bytes[button.byte] |= button.input;
        const auto state = Parse(bytes);
        Check(state.buttons == button.output && state.leftTrigger == 0 && state.rightTrigger == 0,
            "Face, shoulder, Share, Options and stick-click inputs must map independently");
    }
    constexpr std::array<std::uint16_t, 16> directions{
        0x1, 0x9, 0x8, 0xa, 0x2, 0x6, 0x4, 0x5, 0, 0, 0, 0, 0, 0, 0, 0};
    for (std::size_t index = 0; index < directions.size(); ++index) {
        auto bytes = BasicReport();
        bytes[5] = static_cast<std::uint8_t>(index);
        Check(Parse(bytes).buttons == directions[index],
            "All hat directions, diagonals and neutral values must decode correctly");
    }
    for (unsigned value = 0; value <= 255; ++value) {
        auto bytes = BasicReport();
        bytes[7] = static_cast<std::uint8_t>(value);
        Check(Equal(Parse(bytes), {}), "Guide, touchpad and packet-counter bits must not become other buttons");
    }
    auto bytes = BasicReport();
    bytes[6] = 0x0c;
    bytes[8] = 64;
    bytes[9] = 129;
    const auto state = Parse(bytes);
    Check(state.buttons == 0 && state.leftTrigger == 64 && state.rightTrigger == 129,
        "Digital trigger flags must not overwrite the analog trigger travel or unrelated buttons");
}

void TestAxesAndTriggers() {
    for (std::size_t axis = 1; axis <= 4; ++axis) {
        const bool vertical = axis == 2 || axis == 4;
        int previous = vertical ? 32768 : -32769;
        for (unsigned value = 0; value <= 255; ++value) {
            auto bytes = BasicReport(10);
            bytes[axis] = static_cast<std::uint8_t>(value);
            const auto state = Parse(bytes);
            const std::array<int, 4> actual{state.leftX, state.leftY, state.rightX, state.rightY};
            for (std::size_t other = 0; other < actual.size(); ++other)
                if (other != axis - 1) Check(actual[other] == 0, "Moving one axis must not move another");
            const int current = actual[axis - 1];
            Check(vertical ? current < previous : current > previous,
                "All stick values must map monotonically, with screen Y inverted");
            if (value == 0) Check(current == (vertical ? 32767 : -32768), "Low stick endpoint must reach full travel");
            if (value == 128) Check(current == 0, "Raw center 128 must be exactly neutral");
            if (value == 255) Check(current == (vertical ? -32768 : 32767), "High stick endpoint must reach full travel");
            previous = current;
        }
    }
    for (unsigned value = 0; value <= 255; ++value) {
        auto bytes = BasicReport(10);
        bytes[8] = static_cast<std::uint8_t>(value);
        auto state = Parse(bytes);
        Check(state.leftTrigger == value && state.rightTrigger == 0 && state.buttons == 0,
            "Every left-trigger pressure must be preserved independently");
        bytes[8] = 0;
        bytes[9] = static_cast<std::uint8_t>(value);
        state = Parse(bytes);
        Check(state.rightTrigger == value && state.leftTrigger == 0 && state.buttons == 0,
            "Every right-trigger pressure must be preserved independently");
    }
}

void TestBluetoothReports() {
    for (std::uint8_t identifier = 0x11; identifier <= 0x19; ++identifier) {
        for (const std::size_t size : {78u, 128u, 1024u}) {
            auto bytes = BluetoothReport(identifier, size);
            std::fill(bytes.begin() + 78, bytes.end(), 0xcc);
            Check(Equal(Parse(bytes), {}), "Enhanced Bluetooth variants must use the first 78 bytes and native gameplay offset");
        }
    }
    {
        auto full = BluetoothReport();
        auto basic = BasicReport();
        constexpr std::array<std::uint8_t, 9> controls{0, 255, 64, 192, 0xf5, 0xf3, 0xff, 87, 212};
        std::copy(controls.begin(), controls.end(), full.begin() + 3);
        std::copy(controls.begin(), controls.end(), basic.begin() + 1);
        full[74] = 0x9d;
        full[75] = 0x89;
        full[76] = 0x70;
        full[77] = 0x48;
        Check(Equal(Parse(full), Parse(basic)),
            "Enhanced Bluetooth and USB must produce identical gameplay state from matching controls");
    }
    auto bytes = BluetoothReport();
    for (std::size_t size = 0; size < 78; ++size) Reject(bytes.data(), size);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        bytes[index] ^= 0x01;
        Reject(bytes.data(), bytes.size());
        bytes[index] ^= 0x01;
    }
    bytes[1] = 0;
    bytes[74] = 0xfc;
    bytes[75] = 0x30;
    bytes[76] = 0x9f;
    bytes[77] = 0xf7;
    Reject(bytes.data(), bytes.size());
}

DWORD HandleCount() {
    DWORD count = 0;
    Check(GetProcessHandleCount(GetCurrentProcess(), &count) != FALSE, "Process handle count must be readable");
    return count;
}

unsigned ThreadCount() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    Check(snapshot != INVALID_HANDLE_VALUE, "Thread snapshot must be available");
    THREADENTRY32 entry{};
    entry.dwSize = sizeof(entry);
    unsigned count = 0;
    if (Thread32First(snapshot, &entry)) {
        do {
            if (entry.th32OwnerProcessID == GetCurrentProcessId()) ++count;
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return count;
}

void TestWorkerLifecycle() {
    const DWORD handlesBefore = HandleCount();
    const unsigned threadsBefore = ThreadCount();
    const auto began = std::chrono::steady_clock::now();
    {
        input_overlay::DualShock4Provider provider;
        ControllerRawState raw;
        Check(!provider.Discovering(), "An unused native provider must stay suspended");
        for (const int invalid : {-1, 4, 100}) {
            Check(!provider.Read(invalid, raw), "Native reader must reject invalid slots");
            Check(!provider.Discovering(), "Invalid native slots must not trigger discovery");
        }
        for (int iteration = 0; iteration < 256; ++iteration) {
            for (int index = 0; index < 4; ++index) provider.Read(index, raw);
            provider.Reset();
            Check(!provider.Discovering(), "Reset must immediately suspend discovery and clear published state");
        }
        Check(ThreadCount() <= threadsBefore + 1, "Repeated visibility changes must reuse one native worker");
        Check(std::chrono::steady_clock::now() - began < std::chrono::seconds(2),
            "Read and Reset must not synchronously wait for device discovery or pending HID reads");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (ThreadCount() <= threadsBefore && HandleCount() <= handlesBefore + 2) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    Check(ThreadCount() <= threadsBefore, "Destroying the native provider must stop its background worker");
    Check(HandleCount() <= handlesBefore + 2, "Native discovery and reset must release handles after cancellation completes");
}
}

int main() {
    try {
        TestBasicReports();
        TestButtonsAndHats();
        TestAxesAndTriggers();
        TestBluetoothReports();
        TestWorkerLifecycle();
        std::puts("All DualShock 4 report and worker lifecycle tests passed.");
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
