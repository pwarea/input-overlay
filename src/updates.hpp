#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace input_overlay {
constexpr UINT WM_APP_UPDATE = WM_APP + 40;
struct UpdateFile {
    std::wstring name;
    std::string sha256;
    uint64_t size = 0;
};
struct UpdateInfo {
    std::string commit;
    std::wstring version;
    std::vector<std::wstring> notes;
    std::vector<UpdateFile> files;
};
enum class UpdateStatus { Idle, Checking, UpToDate, Available, BuildPending, Downloading, Ready, Error };
struct UpdateResult {
    uint64_t generation = 0;
    UpdateStatus status = UpdateStatus::Idle;
    UpdateInfo info;
    std::wstring message;
    std::wstring stageDirectory;
};
bool ParseUpdateManifest(const std::string& json, const std::string& expectedCommit, UpdateInfo& info);
bool ParseMainCommit(const std::string& json, std::string& commit);
bool ValidateUpdateRelease(const std::string& json, const std::string& commit, const UpdateInfo* info = nullptr);
bool IsUpdateDownloadUrl(const std::wstring& url, const std::string& commit, const std::wstring& asset);
bool VerifyUpdateFile(const std::wstring& path, const UpdateFile& expected);
bool IsUpdateExecutable(const std::wstring& path);
const char* CurrentBuildCommit();
class UpdateService {
public:
    UpdateService();
    ~UpdateService();
    UpdateService(const UpdateService&) = delete;
    UpdateService& operator=(const UpdateService&) = delete;
    bool Check(const std::string& currentCommit, HWND notifyWindow);
    bool Download(const UpdateInfo& info, const std::wstring& applicationDirectory, HWND notifyWindow);
    UpdateResult Snapshot() const;
    void Stop();
private:
    bool Begin(UpdateStatus status, HWND notifyWindow);
    void Publish(UpdateResult result, HWND notifyWindow);
    void CheckWorker(std::string currentCommit, HWND notifyWindow);
    void DownloadWorker(UpdateInfo info, std::wstring applicationDirectory, HWND notifyWindow);
    mutable std::mutex mutex_;
    std::thread worker_;
    UpdateResult result_;
    HANDLE cancel_ = nullptr;
    std::atomic<bool> running_{ false };
};
}
