#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <cstdint>
#include <string>

namespace input_overlay {
struct UpdateInstallFile {
    std::wstring name;
    std::string sha256;
    uint64_t size = 0;
};
struct UpdateInstallPlan {
    std::wstring stagingDirectory;
    std::wstring targetExecutable;
    std::array<UpdateInstallFile, 4> files;
};
bool ValidateUpdateInstallPlan(const UpdateInstallPlan& plan, std::wstring& error);
bool BeginUpdateInstall(const UpdateInstallPlan& plan, std::wstring& error);
bool RunUpdateInstallerMode(int& exitCode);
void SignalUpdateStartupReady();
using UpdateRestartFunction = bool (*)(void* context, std::wstring& error);
bool ApplyUpdateTransaction(const UpdateInstallPlan& plan, UpdateRestartFunction restart,
    void* context, std::wstring& error);
}
