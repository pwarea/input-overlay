#pragma once
#include "controller.hpp"
#include <cstddef>
#include <memory>

namespace input_overlay {
bool ParseDualShock4Report(const std::uint8_t* bytes, std::size_t size, ControllerRawState& result);

class DualShock4Provider {
public:
    DualShock4Provider();
    ~DualShock4Provider();
    DualShock4Provider(const DualShock4Provider&) = delete;
    DualShock4Provider& operator=(const DualShock4Provider&) = delete;
    bool Read(int index, ControllerRawState& result);
    bool Discovering() const;
    void Reset();
private:
    struct Shared;
    std::shared_ptr<Shared> shared_;
    static void Run(const std::shared_ptr<Shared>& shared);
};
}
