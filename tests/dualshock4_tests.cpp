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

std::vector<std::uint8_t> DualSenseReport(bool bluetooth, bool active = false) {
    std::vector<std::uint8_t> bytes(bluetooth ? 78 : 64);
    bytes[0] = bluetooth ? 0x31 : 0x01;
    const std::size_t offset = bluetooth ? 2 : 1;
    if (bluetooth) bytes[1] = 0x10;
    std::fill(bytes.begin() + offset, bytes.begin() + offset + 4, 128);
    bytes[offset + 7] = 8;
    if (active) {
        constexpr std::array<std::uint8_t, 10> controls{0, 255, 64, 192, 87, 212, 0x5a, 0xf5, 0xf3, 0xff};
        std::copy(controls.begin(), controls.end(), bytes.begin() + offset);
    }
    if (bluetooth) {
        const std::uint32_t checksum = active ? 0xb420ca63u : 0x48df25ccu;
        for (std::size_t index = 0; index < 4; ++index)
            bytes[74 + index] = static_cast<std::uint8_t>(checksum >> (8 * index));
    }
    return bytes;
}

ControllerRawState ParseDualSense(const std::vector<std::uint8_t>& bytes) {
    ControllerRawState state{0xf3ff, 255, 255, -32768, 32767, -32768, 32767};
    Check(input_overlay::ParsePlayStationReport(input_overlay::PlayStationProtocol::DualSense,
        bytes.data(), bytes.size(), state), "Valid DualSense input must reach the native provider parser");
    return state;
}

void RejectDualSense(const std::uint8_t* bytes, std::size_t size) {
    const ControllerRawState sentinel{0x1234, 51, 93, -129, 411, 17001, -3199};
    auto state = sentinel;
    Check(!input_overlay::ParseDualSenseReport(bytes, size, state), "Malformed DualSense reports must be rejected");
    Check(Equal(state, sentinel), "A malformed DualSense report must retain the last valid state");
}

void TestPlayStationIdentity() {
    using input_overlay::IdentifyPlayStationController;
    using input_overlay::PlayStationProtocol;
    for (const std::uint16_t product : {0x05c4, 0x09cc})
        Check(IdentifyPlayStationController(0x054c, product) == PlayStationProtocol::DualShock4,
            "Both native DualShock 4 revisions must keep their existing protocol");
    for (const std::uint16_t product : {0x0ce6, 0x0df2})
        Check(IdentifyPlayStationController(0x054c, product) == PlayStationProtocol::DualSense,
            "DualSense and DualSense Edge must be eligible for native input discovery");
    for (unsigned vendor = 0; vendor <= 0xffff; ++vendor) {
        if (vendor == 0x054c) continue;
        Check(IdentifyPlayStationController(static_cast<std::uint16_t>(vendor), 0x0ce6) == PlayStationProtocol::Unsupported,
            "A matching product ID from another vendor must not be decoded as a Sony controller");
    }
    for (const std::uint16_t product : {0, 0x0268, 0x0ba0, 0xffff})
        Check(IdentifyPlayStationController(0x054c, product) == PlayStationProtocol::Unsupported,
            "Unknown native protocols must not be guessed from the Sony vendor ID");
    auto bytes = BasicReport();
    ControllerRawState state{0x1234, 51, 93, -129, 411, 17001, -3199};
    const auto sentinel = state;
    Check(!input_overlay::ParsePlayStationReport(PlayStationProtocol::Unsupported, bytes.data(), bytes.size(), state) &&
        Equal(state, sentinel), "Unsupported devices must not publish a connected input state");
    Check(input_overlay::ParsePlayStationReport(PlayStationProtocol::DualShock4, bytes.data(), bytes.size(), state) &&
        Equal(state, {}), "The expanded native reader must preserve the original DualShock 4 route");
}

void TestDualSenseReports() {
    auto basic = BasicReport(10);
    const std::array<std::uint8_t, 9> controls{0, 255, 64, 192, 0xf5, 0xf3, 0xff, 87, 212};
    std::copy(controls.begin(), controls.end(), basic.begin() + 1);
    const auto expected = Parse(basic);
    for (const bool bluetooth : {false, true}) {
        Check(Equal(ParseDualSense(DualSenseReport(bluetooth)), {}),
            "USB and enhanced Bluetooth DualSense input must both report neutral controls correctly");
        Check(Equal(ParseDualSense(DualSenseReport(bluetooth, true)), expected),
            "DualSense USB and enhanced Bluetooth must map buttons, triggers and axes to the same gamepad state");
    }
    for (const std::size_t size : {10u, 78u}) {
        auto padded = BasicReport(size);
        Check(Equal(ParseDualSense(padded), {}), "DualSense basic Bluetooth reports must support Windows padding");
        std::copy(controls.begin(), controls.end(), padded.begin() + 1);
        Check(Equal(ParseDualSense(padded), expected),
            "Basic Bluetooth controls must not be confused with the different USB field positions");
    }
    for (const std::size_t size : {78u, 128u, 1024u}) {
        auto padded = DualSenseReport(true, true);
        padded.resize(size, 0xcc);
        Check(Equal(ParseDualSense(padded), expected),
            "Enhanced Bluetooth padding must not shift gameplay fields or the authenticated report");
    }
    for (unsigned value = 0; value <= 255; ++value) {
        for (std::size_t field = 0; field < 6; ++field) {
            auto usb = DualSenseReport(false);
            auto equivalent = BasicReport(10);
            usb[field + 1] = static_cast<std::uint8_t>(value);
            equivalent[field < 4 ? field + 1 : field + 4] = static_cast<std::uint8_t>(value);
            Check(Equal(ParseDualSense(usb), Parse(equivalent)),
                "Every DualSense stick coordinate and analog trigger pressure must retain its full independent range");
        }
        for (const std::size_t field : {7u, 8u}) {
            auto usb = DualSenseReport(false);
            auto equivalent = BasicReport(10);
            usb[field + 1] = static_cast<std::uint8_t>(value);
            equivalent[field - 2] = static_cast<std::uint8_t>(value);
            Check(Equal(ParseDualSense(usb), Parse(equivalent)),
                "Every combination of DualSense face, shoulder, menu, stick-click and D-pad buttons must decode independently");
        }
        auto usb = DualSenseReport(false);
        usb[7] = usb[10] = usb[11] = static_cast<std::uint8_t>(value);
        Check(Equal(ParseDualSense(usb), {}),
            "Packet counters, PS, touchpad, microphone and Edge-only controls must not activate unrelated gameplay inputs");
    }
    auto enhanced = DualSenseReport(true);
    RejectDualSense(nullptr, 0);
    RejectDualSense(nullptr, 64);
    for (std::size_t size = 0; size < 78; ++size) RejectDualSense(enhanced.data(), size);
    for (std::size_t index = 0; index < enhanced.size(); ++index) {
        enhanced[index] ^= 1;
        RejectDualSense(enhanced.data(), enhanced.size());
        enhanced[index] ^= 1;
    }
    auto usb = DualSenseReport(false);
    for (std::size_t size = 0; size < 64; ++size)
        if (size != 10) RejectDualSense(usb.data(), size);
    for (const std::uint8_t identifier : {0x00, 0x02, 0x11, 0x30, 0xff}) {
        usb[0] = identifier;
        RejectDualSense(usb.data(), usb.size());
    }
    usb[0] = 1;
    for (const std::size_t size : {65u, 77u, 79u, 128u, 1025u}) {
        usb.resize(size);
        RejectDualSense(usb.data(), usb.size());
    }
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

void PrepareNativeDiscovery() {
    const unsigned threadsBefore = ThreadCount();
    {
        input_overlay::DualShock4Provider provider;
        ControllerRawState raw;
        provider.Read(0, raw);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (provider.Discovering() && std::chrono::steady_clock::now() < deadline)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        Check(!provider.Discovering(), "Initial Windows device discovery must finish before measuring worker resources");
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (ThreadCount() > threadsBefore && std::chrono::steady_clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    Check(ThreadCount() <= threadsBefore, "The initial discovery worker must stop before taking the resource baseline");
}

void TestWorkerLifecycle() {
    PrepareNativeDiscovery();
    const DWORD handlesBefore = HandleCount();
    const unsigned threadsBefore = ThreadCount();
    for (int lifetime = 0; lifetime < 4; ++lifetime) {
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
}

int main() {
    try {
        TestBasicReports();
        TestButtonsAndHats();
        TestAxesAndTriggers();
        TestBluetoothReports();
        TestPlayStationIdentity();
        TestDualSenseReports();
        TestWorkerLifecycle();
        std::puts("All DualShock 4, DualSense, native discovery and worker lifecycle tests passed.");
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        return EXIT_FAILURE;
    }
}
