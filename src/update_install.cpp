#include "update_install.hpp"
#include <bcrypt.h>
#include <shellapi.h>
#include <algorithm>
#include <cstring>
#include <cwchar>
#include <limits>
#include <thread>
#include <vector>

namespace input_overlay {
namespace {
constexpr wchar_t ExecutableName[] = L"Input Overlay.exe";
constexpr wchar_t HelperName[] = L"InputOverlayUpdater.exe";
constexpr wchar_t StagePrefix[] = L".input-overlay-update-";
constexpr std::array<const wchar_t*, 4> Names = {
    ExecutableName, L"LICENSE", L"README.md", L"THIRD_PARTY_NOTICES.txt"
};
constexpr size_t PathCapacity = 32768;
constexpr uint64_t MaximumFileSize = 128ull * 1024 * 1024;
constexpr DWORD HandoffMagic = 0x49505531;
struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    explicit Handle(HANDLE input) : value(input) {}
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    HANDLE Release() { HANDLE result = value; value = nullptr; return result; }
    explicit operator bool() const { return value && value != INVALID_HANDLE_VALUE; }
};
struct MappedPlan {
    DWORD magic = HandoffMagic;
    DWORD size = sizeof(MappedPlan);
    wchar_t stage[PathCapacity]{};
    wchar_t target[PathCapacity]{};
    char hashes[4][65]{};
    uint64_t sizes[4]{};
};
std::wstring ModulePath() {
    std::wstring path(PathCapacity, L'\0');
    DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) return {};
    path.resize(length);
    return path;
}
std::wstring Directory(const std::wstring& path) {
    auto slash = path.find_last_of(L"\\/");
    return slash == std::wstring::npos ? std::wstring{} : path.substr(0, slash);
}
std::wstring Filename(const std::wstring& path) {
    auto slash = path.find_last_of(L"\\/");
    return path.substr(slash == std::wstring::npos ? 0 : slash + 1);
}
std::wstring Join(const std::wstring& directory, const std::wstring& name) {
    return directory + L"\\" + name;
}
bool EqualPath(const std::wstring& left, const std::wstring& right) {
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}
bool CleanAbsolutePath(const std::wstring& path) {
    if (path.size() < 4 || path.size() >= PathCapacity || path[1] != L':' || path[2] != L'\\' ||
        !((path[0] >= L'A' && path[0] <= L'Z') || (path[0] >= L'a' && path[0] <= L'z')) ||
        path.find_first_of(L"/\"<>|?*", 3) != std::wstring::npos ||
        path.find(L':', 2) != std::wstring::npos || path.find(L'\0') != std::wstring::npos) return false;
    size_t start = 3;
    while (start < path.size()) {
        size_t end = path.find(L'\\', start);
        if (end == std::wstring::npos) end = path.size();
        auto part = path.substr(start, end - start);
        if (part.empty() || part == L"." || part == L".." || part.back() == L'.' || part.back() == L' ') return false;
        for (auto character : part) if (character < 32) return false;
        start = end + 1;
    }
    return path.back() != L'\\';
}
bool PlainDirectory(const std::wstring& path) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
bool PlainFile(const std::wstring& path, bool allowMissing = false) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        DWORD reason = GetLastError();
        return allowMissing && (reason == ERROR_FILE_NOT_FOUND || reason == ERROR_PATH_NOT_FOUND);
    }
    if (attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    Handle file(CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE |
        FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    return file && GetFileInformationByHandle(file.value, &info) && info.nNumberOfLinks == 1 &&
        !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}
bool ValidStage(const std::wstring& stage, const std::wstring& target) {
    if (!CleanAbsolutePath(stage) || !CleanAbsolutePath(target) || Filename(target) != ExecutableName ||
        !EqualPath(Directory(stage), Directory(target)) || !PlainDirectory(stage) ||
        !PlainDirectory(Directory(target))) return false;
    const std::wstring leaf = Filename(stage);
    const size_t prefixLength = std::wcslen(StagePrefix);
    if (leaf.size() < prefixLength + 8 || leaf.size() > prefixLength + 80 ||
        leaf.compare(0, prefixLength, StagePrefix) != 0) return false;
    return std::all_of(leaf.begin() + prefixLength, leaf.end(), [](wchar_t ch) {
        return (ch >= L'0' && ch <= L'9') || (ch >= L'a' && ch <= L'z') ||
            (ch >= L'A' && ch <= L'Z') || ch == L'-';
    });
}
bool ValidHash(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char ch) {
        return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f');
    });
}
bool HashMatches(const std::wstring& path, const UpdateInstallFile& expected) {
    if (!PlainFile(path) || !expected.size || expected.size > MaximumFileSize || !ValidHash(expected.sha256)) return false;
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    LARGE_INTEGER size{};
    if (!file || !GetFileSizeEx(file.value, &size) || static_cast<uint64_t>(size.QuadPart) != expected.size) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    bool valid = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    std::array<unsigned char, 64 * 1024> buffer{};
    uint64_t total = 0;
    while (valid) {
        DWORD read = 0;
        if (!ReadFile(file.value, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) { valid = false; break; }
        if (!read) break;
        total += read;
        if (total > expected.size || BCryptHashData(hash, buffer.data(), read, 0) < 0) valid = false;
    }
    std::array<unsigned char, 32> digest{};
    valid = valid && total == expected.size && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!valid) return false;
    constexpr char hex[] = "0123456789abcdef";
    std::string encoded;
    for (unsigned char byte : digest) { encoded.push_back(hex[byte >> 4]); encoded.push_back(hex[byte & 15]); }
    return encoded == expected.sha256;
}
bool ValidExecutable(const std::wstring& path) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    IMAGE_DOS_HEADER dos{};
    IMAGE_NT_HEADERS64 pe{};
    DWORD read = 0;
    if (!file || !ReadFile(file.value, &dos, sizeof(dos), &read, nullptr) || read != sizeof(dos) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos)) || dos.e_lfanew > 1024 * 1024) return false;
    LARGE_INTEGER position{}; position.QuadPart = dos.e_lfanew;
    if (!SetFilePointerEx(file.value, position, nullptr, FILE_BEGIN) ||
        !ReadFile(file.value, &pe, sizeof(pe), &read, nullptr) || read != sizeof(pe)) return false;
    return pe.Signature == IMAGE_NT_SIGNATURE && pe.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        (pe.FileHeader.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) && !(pe.FileHeader.Characteristics & IMAGE_FILE_DLL) &&
        pe.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC && pe.OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI;
}
std::wstring ProcessPath(HANDLE process) {
    std::wstring path(PathCapacity, L'\0');
    DWORD length = static_cast<DWORD>(path.size());
    if (!QueryFullProcessImageNameW(process, 0, path.data(), &length)) return {};
    path.resize(length);
    return path;
}
std::wstring HandleText(HANDLE value) {
    return std::to_wstring(reinterpret_cast<uintptr_t>(value));
}
bool ParseHandle(const wchar_t* text, HANDLE& value) {
    if (!text || !*text || std::wcslen(text) > 20) return false;
    uintptr_t result = 0;
    for (const wchar_t* position = text; *position; ++position) {
        if (*position < L'0' || *position > L'9') return false;
        const unsigned digit = *position - L'0';
        if (result > (std::numeric_limits<uintptr_t>::max() - digit) / 10) return false;
        result = result * 10 + digit;
    }
    if (!result || result == std::numeric_limits<uintptr_t>::max()) return false;
    value = reinterpret_cast<HANDLE>(result);
    DWORD flags = 0;
    return GetHandleInformation(value, &flags) && (flags & HANDLE_FLAG_INHERIT);
}
bool Launch(const std::wstring& executable, const std::wstring& arguments,
    const std::vector<HANDLE>& handles, PROCESS_INFORMATION& process) {
    std::wstring command = L"\"" + executable + L"\" " + arguments;
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    SIZE_T bytes = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
    std::vector<unsigned char> storage(bytes);
    startup.lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(storage.data());
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &bytes)) return false;
    bool valid = UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
        const_cast<HANDLE*>(handles.data()), handles.size() * sizeof(HANDLE), nullptr, nullptr) != FALSE;
    if (valid) valid = CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
        EXTENDED_STARTUPINFO_PRESENT | CREATE_NO_WINDOW, nullptr, Directory(executable).c_str(),
        &startup.StartupInfo, &process) != FALSE;
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    return valid;
}
void ErrorMessage(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"Input Overlay update", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
}
bool ReadMappedPlan(HANDLE mapping, UpdateInstallPlan& plan) {
    const auto* data = static_cast<const MappedPlan*>(MapViewOfFile(mapping, FILE_MAP_READ, 0, 0, sizeof(MappedPlan)));
    if (!data) return false;
    bool valid = data->magic == HandoffMagic && data->size == sizeof(MappedPlan) &&
        std::wmemchr(data->stage, L'\0', PathCapacity) && std::wmemchr(data->target, L'\0', PathCapacity);
    if (valid) {
        plan.stagingDirectory = data->stage;
        plan.targetExecutable = data->target;
        for (size_t index = 0; index < Names.size(); ++index) {
            if (data->hashes[index][64] || std::memchr(data->hashes[index], '\0', 64)) { valid = false; break; }
            plan.files[index] = {Names[index], std::string(data->hashes[index], 64), data->sizes[index]};
        }
    }
    UnmapViewOfFile(data);
    return valid;
}
bool RestartUpdated(void* context, std::wstring& error) {
    const auto& plan = *static_cast<const UpdateInstallPlan*>(context);
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle ready(CreateEventW(&security, TRUE, FALSE, nullptr));
    Handle helper(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, GetCurrentProcessId()));
    if (!ready || !helper) { error = L"The updated application could not be prepared for launch."; return false; }
    PROCESS_INFORMATION process{};
    if (!Launch(plan.targetExecutable, L"--background --update-ready " + HandleText(ready.value) + L" " + HandleText(helper.value),
        {ready.value, helper.value}, process)) {
        error = L"Windows could not launch the updated application."; return false;
    }
    Handle child(process.hProcess), thread(process.hThread);
    HANDLE waitHandles[]{ready.value, child.value};
    DWORD wait = WaitForMultipleObjects(2, waitHandles, FALSE, 30000);
    if (wait == WAIT_OBJECT_0) return true;
    if (wait != WAIT_OBJECT_0 + 1) {
        TerminateProcess(child.value, ERROR_TIMEOUT);
        WaitForSingleObject(child.value, 5000);
    }
    error = L"The updated application did not finish starting.";
    return false;
}
void RestartPrevious(const std::wstring& executable) {
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    std::wstring command = L"\"" + executable + L"\" --background";
    if (CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, Directory(executable).c_str(), &startup, &process)) {
        CloseHandle(process.hThread); CloseHandle(process.hProcess);
    }
}
}

bool ValidateUpdateInstallPlan(const UpdateInstallPlan& plan, std::wstring& error) {
    error.clear();
    if (!ValidStage(plan.stagingDirectory, plan.targetExecutable) || !PlainFile(plan.targetExecutable)) {
        error = L"The update folder or application location is invalid."; return false;
    }
    for (size_t index = 0; index < Names.size(); ++index) {
        const auto& file = plan.files[index];
        if (file.name != Names[index] || !HashMatches(Join(plan.stagingDirectory, file.name), file)) {
            error = L"An update file is missing or failed its integrity check."; return false;
        }
        if (!PlainFile(Join(Directory(plan.targetExecutable), file.name), true)) {
            error = L"An application file is linked or is not a regular file."; return false;
        }
        if (GetFileAttributesW(Join(plan.stagingDirectory, L"backup-" + std::to_wstring(index)).c_str()) != INVALID_FILE_ATTRIBUTES) {
            error = L"This update folder already contains recovery files. Download the update again."; return false;
        }
    }
    if (!ValidExecutable(Join(plan.stagingDirectory, ExecutableName))) {
        error = L"The update is not a compatible Windows application."; return false;
    }
    return true;
}

bool ApplyUpdateTransaction(const UpdateInstallPlan& plan, UpdateRestartFunction restart,
    void* context, std::wstring& error) {
    if (!ValidateUpdateInstallPlan(plan, error) || !restart) return false;
    std::vector<size_t> changed;
    std::array<bool, 4> hadOriginal{};
    bool success = true;
    constexpr std::array<size_t, 4> order{1, 2, 3, 0};
    for (size_t index : order) {
        const std::wstring target = Join(Directory(plan.targetExecutable), Names[index]);
        const std::wstring source = Join(plan.stagingDirectory, Names[index]);
        const std::wstring backup = Join(plan.stagingDirectory, L"backup-" + std::to_wstring(index));
        hadOriginal[index] = GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES;
        if (hadOriginal[index]) success = ReplaceFileW(target.c_str(), source.c_str(), backup.c_str(),
            REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr) != FALSE;
        else success = MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
        if (!success) {
            if (GetFileAttributesW(backup.c_str()) != INVALID_FILE_ATTRIBUTES) changed.push_back(index);
            error = L"An application file could not be replaced. Close any program using this folder and try again.";
            break;
        }
        changed.push_back(index);
    }
    if (success) success = restart(context, error);
    if (!success) {
        bool recovered = true;
        for (auto position = changed.rbegin(); position != changed.rend(); ++position) {
            const size_t index = *position;
            const std::wstring target = Join(Directory(plan.targetExecutable), Names[index]);
            const std::wstring backup = Join(plan.stagingDirectory, L"backup-" + std::to_wstring(index));
            if (hadOriginal[index]) {
                if (GetFileAttributesW(target.c_str()) != INVALID_FILE_ATTRIBUTES) {
                    if (!ReplaceFileW(target.c_str(), backup.c_str(), nullptr, REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) recovered = false;
                } else if (!MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) recovered = false;
            } else if (!DeleteFileW(target.c_str()) && GetLastError() != ERROR_FILE_NOT_FOUND) recovered = false;
        }
        error += recovered ? L"\n\nThe previous application files have been restored." :
            L"\n\nAutomatic recovery could not finish. Recovery files remain in:\n" + plan.stagingDirectory;
        return false;
    }
    for (size_t index = 0; index < Names.size(); ++index)
        DeleteFileW(Join(plan.stagingDirectory, L"backup-" + std::to_wstring(index)).c_str());
    return true;
}

bool BeginUpdateInstall(const UpdateInstallPlan& plan, std::wstring& error) {
    if (!EqualPath(ModulePath(), plan.targetExecutable)) {
        error = L"The update does not target this running application."; return false;
    }
    if (!ValidateUpdateInstallPlan(plan, error)) return false;
    const std::wstring helperPath = Join(plan.stagingDirectory, HelperName);
    if (!CopyFileW(plan.targetExecutable.c_str(), helperPath.c_str(), TRUE)) {
        error = L"The update helper could not be prepared. Download the update again."; return false;
    }
    SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
    Handle parent(OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, TRUE, GetCurrentProcessId()));
    Handle ready(CreateEventW(&security, TRUE, FALSE, nullptr));
    Handle cancel(CreateEventW(&security, TRUE, FALSE, nullptr));
    Handle mapping(CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0, sizeof(MappedPlan), nullptr));
    if (!parent || !ready || !cancel || !mapping) {
        DeleteFileW(helperPath.c_str()); error = L"Windows could not prepare the update handoff."; return false;
    }
    auto* data = static_cast<MappedPlan*>(MapViewOfFile(mapping.value, FILE_MAP_WRITE, 0, 0, sizeof(MappedPlan)));
    if (!data) { DeleteFileW(helperPath.c_str()); error = L"The update handoff could not be created."; return false; }
    *data = MappedPlan{};
    std::copy(plan.stagingDirectory.begin(), plan.stagingDirectory.end(), data->stage);
    std::copy(plan.targetExecutable.begin(), plan.targetExecutable.end(), data->target);
    for (size_t index = 0; index < Names.size(); ++index) {
        std::copy(plan.files[index].sha256.begin(), plan.files[index].sha256.end(), data->hashes[index]);
        data->sizes[index] = plan.files[index].size;
    }
    UnmapViewOfFile(data);
    PROCESS_INFORMATION process{};
    const std::wstring arguments = L"--apply-update " + HandleText(parent.value) + L" " + HandleText(ready.value) +
        L" " + HandleText(cancel.value) + L" " + HandleText(mapping.value);
    if (!Launch(helperPath, arguments, {parent.value, ready.value, cancel.value, mapping.value}, process)) {
        DeleteFileW(helperPath.c_str()); error = L"Windows could not start the update helper."; return false;
    }
    Handle helper(process.hProcess), thread(process.hThread);
    HANDLE waitHandles[]{ready.value, helper.value};
    DWORD wait = WaitForMultipleObjects(2, waitHandles, FALSE, 5000);
    if (wait == WAIT_OBJECT_0) return true;
    SetEvent(cancel.value);
    WaitForSingleObject(helper.value, 1000);
    DeleteFileW(helperPath.c_str());
    error = L"The update helper could not safely start. The running application has not been changed.";
    return false;
}

bool RunUpdateInstallerMode(int& exitCode) {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return false;
    const bool requested = count >= 2 && std::wcscmp(arguments[1], L"--apply-update") == 0;
    if (!requested) { LocalFree(arguments); return false; }
    HANDLE parentValue = nullptr, readyValue = nullptr, cancelValue = nullptr, mappingValue = nullptr;
    bool valid = count == 6 && ParseHandle(arguments[2], parentValue) && ParseHandle(arguments[3], readyValue) &&
        ParseHandle(arguments[4], cancelValue) && ParseHandle(arguments[5], mappingValue);
    LocalFree(arguments);
    exitCode = 1;
    if (!valid) { ErrorMessage(L"This update helper can only be started by Input Overlay."); return true; }
    Handle parent(parentValue), ready(readyValue), cancel(cancelValue), mapping(mappingValue);
    UpdateInstallPlan plan;
    std::wstring error;
    const std::wstring ownPath = ModulePath();
    if (!ReadMappedPlan(mapping.value, plan) || GetProcessId(parent.value) == 0 ||
        GetProcessId(parent.value) == GetCurrentProcessId() || !EqualPath(ProcessPath(parent.value), plan.targetExecutable) ||
        !EqualPath(ownPath, Join(plan.stagingDirectory, HelperName)) || !ValidateUpdateInstallPlan(plan, error)) {
        return true;
    }
    if (!SetEvent(ready.value)) return true;
    HANDLE waitHandles[]{cancel.value, parent.value};
    if (WaitForMultipleObjects(2, waitHandles, FALSE, 30000) != WAIT_OBJECT_0 + 1) return true;
    if (!ApplyUpdateTransaction(plan, RestartUpdated, &plan, error)) {
        ErrorMessage(error);
        RestartPrevious(plan.targetExecutable);
        return true;
    }
    exitCode = 0;
    return true;
}

void SignalUpdateStartupReady() {
    int count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
    if (!arguments) return;
    HANDLE readyValue = nullptr, helperValue = nullptr;
    const bool requested = count == 5 && std::wcscmp(arguments[1], L"--background") == 0 &&
        std::wcscmp(arguments[2], L"--update-ready") == 0 && ParseHandle(arguments[3], readyValue) &&
        ParseHandle(arguments[4], helperValue);
    LocalFree(arguments);
    if (!requested) return;
    Handle ready(readyValue), helper(helperValue);
    const std::wstring helperPath = ProcessPath(helper.value);
    const std::wstring target = ModulePath();
    if (GetProcessId(helper.value) == 0 || Filename(helperPath) != HelperName ||
        !ValidStage(Directory(helperPath), target) || !SetEvent(ready.value)) return;
    HANDLE process = helper.Release();
    try {
        std::thread([process, helperPath] {
            Handle pending(process);
            if (WaitForSingleObject(process, 60000) == WAIT_OBJECT_0) {
                DeleteFileW(helperPath.c_str());
                RemoveDirectoryW(Directory(helperPath).c_str());
            }
        }).detach();
    } catch (...) { CloseHandle(process); }
}
}
