#include "dualshock4.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <algorithm>
#include <array>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace input_overlay {
namespace {
constexpr std::size_t MaximumReportBytes = 1024;
constexpr ULONGLONG DiscoveryInterval = 2000;
constexpr DWORD ReadInterval = 16;

bool IsDualShock4(HANDLE handle) {
    HIDD_ATTRIBUTES attributes{};
    attributes.Size = sizeof(attributes);
    return HidD_GetAttributes(handle, &attributes) && attributes.VendorID == 0x054c &&
        (attributes.ProductID == 0x05c4 || attributes.ProductID == 0x09cc);
}

std::uint32_t UpdateCrc(std::uint32_t value, std::uint8_t byte) {
    value ^= byte;
    for (int bit = 0; bit < 8; ++bit) {
        value = (value >> 1) ^ (0xedb88320u & (0u - (value & 1u)));
    }
    return value;
}

std::int16_t ConvertAxis(std::uint8_t value, bool invert) {
    const int centered = static_cast<int>(value) - 128;
    const int negativeRange = invert ? 32767 : 32768;
    const int positiveRange = invert ? 32768 : 32767;
    const int scaled = centered < 0 ? centered * negativeRange / 128 : centered * positiveRange / 127;
    return static_cast<std::int16_t>(invert ? -scaled : scaled);
}

struct HidDevice {
    HANDLE handle = INVALID_HANDLE_VALUE;
    HANDLE event = nullptr;
    OVERLAPPED operation{};
    std::array<std::uint8_t, MaximumReportBytes> bytes{};
    DWORD reportLength = 0;
    bool pending = false;
    bool valid = false;
    ULONGLONG openedAt = 0;
    ControllerRawState state{};

    ~HidDevice() {
        if (handle != INVALID_HANDLE_VALUE) {
            if (pending) {
                CancelIoEx(handle, &operation);
                DWORD count = 0;
                GetOverlappedResult(handle, &operation, &count, TRUE);
            }
            CloseHandle(handle);
        }
        if (event) CloseHandle(event);
    }

    bool Drain() {
        for (int batch = 0; batch < 32; ++batch) {
            DWORD count = 0;
            if (pending) {
                if (!GetOverlappedResult(handle, &operation, &count, FALSE)) {
                    return GetLastError() == ERROR_IO_INCOMPLETE;
                }
                pending = false;
            } else {
                ResetEvent(event);
                operation = {};
                operation.hEvent = event;
                if (!ReadFile(handle, bytes.data(), reportLength, &count, &operation)) {
                    if (GetLastError() != ERROR_IO_PENDING) return false;
                    pending = true;
                    return true;
                }
            }
            if (count == 0 || count > reportLength) return false;
            ControllerRawState parsed;
            if (ParseDualShock4Report(bytes.data(), count, parsed)) {
                state = parsed;
                valid = true;
            }
        }
        return true;
    }
};

std::unique_ptr<HidDevice> OpenDevice(const std::wstring& path) {
    auto device = std::make_unique<HidDevice>();
    device->handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
    if (device->handle == INVALID_HANDLE_VALUE) return {};
    if (!IsDualShock4(device->handle)) return {};
    PHIDP_PREPARSED_DATA data = nullptr;
    if (!HidD_GetPreparsedData(device->handle, &data)) return {};
    HIDP_CAPS caps{};
    const auto status = HidP_GetCaps(data, &caps);
    HidD_FreePreparsedData(data);
    if (status != HIDP_STATUS_SUCCESS || caps.UsagePage != 0x01 || caps.Usage != 0x05 ||
        caps.InputReportByteLength < 10 || caps.InputReportByteLength > MaximumReportBytes) return {};
    device->reportLength = caps.InputReportByteLength;
    device->event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!device->event) return {};
    device->openedAt = GetTickCount64();
    return device;
}

std::vector<std::wstring> DevicePaths(HANDLE control) {
    std::vector<std::wstring> paths;
    GUID guid{};
    HidD_GetHidGuid(&guid);
    HDEVINFO info = SetupDiGetClassDevsW(&guid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (info == INVALID_HANDLE_VALUE) return paths;
    try {
        for (DWORD index = 0; index < 1024 && WaitForSingleObject(control, 0) != WAIT_OBJECT_0; ++index) {
            SP_DEVICE_INTERFACE_DATA entry{};
            entry.cbSize = sizeof(entry);
            if (!SetupDiEnumDeviceInterfaces(info, nullptr, &guid, index, &entry)) break;
            DWORD length = 0;
            SetupDiGetDeviceInterfaceDetailW(info, &entry, nullptr, 0, &length, nullptr);
            if (length < sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W) || length > 65536) continue;
            std::vector<std::uint8_t> buffer(length);
            auto* detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(buffer.data());
            detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
            if (!SetupDiGetDeviceInterfaceDetailW(info, &entry, detail, length, nullptr, nullptr)) continue;
            std::wstring path(detail->DevicePath);
            std::transform(path.begin(), path.end(), path.begin(), [](wchar_t character) {
                return character >= L'A' && character <= L'Z' ? static_cast<wchar_t>(character + L'a' - L'A') : character;
            });
            const auto metadata = CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE,
                nullptr, OPEN_EXISTING, 0, nullptr);
            if (metadata == INVALID_HANDLE_VALUE) continue;
            const bool supported = IsDualShock4(metadata);
            CloseHandle(metadata);
            if (!supported) continue;
            paths.push_back(std::move(path));
        }
    } catch (...) {
        SetupDiDestroyDeviceInfoList(info);
        throw;
    }
    SetupDiDestroyDeviceInfoList(info);
    std::sort(paths.begin(), paths.end());
    paths.erase(std::unique(paths.begin(), paths.end()), paths.end());
    return paths;
}
}

bool ParseDualShock4Report(const std::uint8_t* bytes, std::size_t size, ControllerRawState& result) {
    if (!bytes || size < 10 || size > MaximumReportBytes) return false;
    std::size_t offset = 1;
    if (bytes[0] == 0x01) {
        if (size != 10 && size < 64) return false;
    } else if (bytes[0] >= 0x11 && bytes[0] <= 0x19) {
        if (size < 78 || !(bytes[1] & 0x80)) return false;
        auto crc = UpdateCrc(0xffffffffu, 0xa1);
        for (std::size_t index = 0; index < 74; ++index) crc = UpdateCrc(crc, bytes[index]);
        const auto received = static_cast<std::uint32_t>(bytes[74]) |
            (static_cast<std::uint32_t>(bytes[75]) << 8) |
            (static_cast<std::uint32_t>(bytes[76]) << 16) |
            (static_cast<std::uint32_t>(bytes[77]) << 24);
        if (~crc != received) return false;
        offset = 3;
    } else {
        return false;
    }
    const auto* data = bytes + offset;
    ControllerRawState parsed;
    parsed.leftX = ConvertAxis(data[0], false);
    parsed.leftY = ConvertAxis(data[1], true);
    parsed.rightX = ConvertAxis(data[2], false);
    parsed.rightY = ConvertAxis(data[3], true);
    constexpr std::array<std::uint16_t, 9> hats{0x0001, 0x0009, 0x0008, 0x000a, 0x0002, 0x0006, 0x0004, 0x0005, 0};
    const auto hat = static_cast<std::size_t>(data[4] & 0x0f);
    if (hat < hats.size()) parsed.buttons = hats[hat];
    if (data[4] & 0x10) parsed.buttons |= 0x4000;
    if (data[4] & 0x20) parsed.buttons |= 0x1000;
    if (data[4] & 0x40) parsed.buttons |= 0x2000;
    if (data[4] & 0x80) parsed.buttons |= 0x8000;
    if (data[5] & 0x01) parsed.buttons |= 0x0100;
    if (data[5] & 0x02) parsed.buttons |= 0x0200;
    if (data[5] & 0x10) parsed.buttons |= 0x0020;
    if (data[5] & 0x20) parsed.buttons |= 0x0010;
    if (data[5] & 0x40) parsed.buttons |= 0x0040;
    if (data[5] & 0x80) parsed.buttons |= 0x0080;
    parsed.leftTrigger = data[7];
    parsed.rightTrigger = data[8];
    result = parsed;
    return true;
}

struct DualShock4Provider::Shared {
    struct Slot {
        bool connected = false;
        ControllerRawState state{};
    };
    std::mutex mutex;
    HANDLE control = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    bool enabled = false;
    bool shutdown = false;
    bool failed = false;
    bool discovering = false;
    std::uint64_t generation = 0;
    std::array<Slot, ControllerSlotCount - XInputSlotCount> slots{};
    ~Shared() { if (control) CloseHandle(control); }
};

DualShock4Provider::DualShock4Provider() {
    try {
        shared_ = std::make_shared<Shared>();
        if (!shared_->control) {
            shared_->failed = true;
            return;
        }
        std::thread([shared = shared_] { Run(shared); }).detach();
    } catch (...) {
        if (shared_) shared_->failed = true;
    }
}

DualShock4Provider::~DualShock4Provider() {
    if (!shared_) return;
    std::lock_guard<std::mutex> lock(shared_->mutex);
    shared_->shutdown = true;
    shared_->enabled = false;
    shared_->slots = {};
    if (shared_->control) SetEvent(shared_->control);
}

bool DualShock4Provider::Read(int index, ControllerRawState& result) {
    if (!shared_ || index < 0 || index >= ControllerSlotCount - XInputSlotCount) return false;
    std::lock_guard<std::mutex> lock(shared_->mutex);
    if (shared_->failed || shared_->shutdown) return false;
    if (!shared_->enabled) {
        shared_->enabled = true;
        shared_->discovering = true;
        ++shared_->generation;
        SetEvent(shared_->control);
    }
    if (!shared_->slots[index].connected) return false;
    result = shared_->slots[index].state;
    return true;
}

bool DualShock4Provider::Discovering() const {
    if (!shared_) return false;
    std::lock_guard<std::mutex> lock(shared_->mutex);
    return shared_->enabled && !shared_->failed && shared_->discovering;
}

void DualShock4Provider::Reset() {
    if (!shared_) return;
    std::lock_guard<std::mutex> lock(shared_->mutex);
    if (!shared_->enabled) return;
    shared_->enabled = false;
    shared_->discovering = false;
    shared_->slots = {};
    ++shared_->generation;
    SetEvent(shared_->control);
}

void DualShock4Provider::Run(const std::shared_ptr<Shared>& shared) {
    try {
        std::array<std::unique_ptr<HidDevice>, ControllerSlotCount - XInputSlotCount> devices;
        std::array<std::wstring, ControllerSlotCount - XInputSlotCount> paths;
        std::uint64_t generation = 0;
        ULONGLONG nextDiscovery = 0;
        while (true) {
            bool enabled = false;
            bool changed = false;
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                if (shared->shutdown) break;
                ResetEvent(shared->control);
                enabled = shared->enabled;
                changed = generation != shared->generation;
                generation = shared->generation;
            }
            if (changed) {
                for (auto& device : devices) device.reset();
                nextDiscovery = 0;
            }
            if (!enabled) {
                WaitForSingleObject(shared->control, INFINITE);
                continue;
            }
            auto now = GetTickCount64();
            if (now >= nextDiscovery) {
                const auto found = DevicePaths(shared->control);
                for (std::size_t index = 0; index < paths.size(); ++index) {
                    if (devices[index] && !std::binary_search(found.begin(), found.end(), paths[index])) devices[index].reset();
                }
                for (const auto& path : found) {
                    if (WaitForSingleObject(shared->control, 0) == WAIT_OBJECT_0) break;
                    auto known = std::find(paths.begin(), paths.end(), path);
                    if (known != paths.end() && devices[static_cast<std::size_t>(known - paths.begin())]) continue;
                    std::size_t slot = known == paths.end() ? paths.size() : static_cast<std::size_t>(known - paths.begin());
                    if (slot == paths.size()) {
                        for (std::size_t index = 0; index < paths.size(); ++index) {
                            if (!devices[index] && !std::binary_search(found.begin(), found.end(), paths[index])) {
                                slot = index;
                                break;
                            }
                        }
                    }
                    if (slot == paths.size()) continue;
                    auto device = OpenDevice(path);
                    if (!device) continue;
                    paths[slot] = path;
                    devices[slot] = std::move(device);
                }
                now = GetTickCount64();
                nextDiscovery = now + DiscoveryInterval;
            }
            std::array<Shared::Slot, ControllerSlotCount - XInputSlotCount> states{};
            bool anyOpen = false;
            bool discovering = false;
            for (std::size_t index = 0; index < devices.size(); ++index) {
                auto& device = devices[index];
                if (!device) continue;
                if (!device->Drain()) {
                    device.reset();
                    continue;
                }
                anyOpen = true;
                if (!device->valid && now - device->openedAt < 250) discovering = true;
                states[index].connected = device->valid;
                states[index].state = device->state;
            }
            {
                std::lock_guard<std::mutex> lock(shared->mutex);
                if (shared->shutdown) break;
                if (shared->generation != generation) continue;
                shared->slots = states;
                shared->discovering = discovering;
            }
            now = GetTickCount64();
            const auto delay = anyOpen ? ReadInterval : static_cast<DWORD>(nextDiscovery > now ? nextDiscovery - now : 1);
            WaitForSingleObject(shared->control, delay);
        }
    } catch (...) {
        std::lock_guard<std::mutex> lock(shared->mutex);
        shared->failed = true;
        shared->discovering = false;
        shared->slots = {};
    }
}
}
