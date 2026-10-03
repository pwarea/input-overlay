#include "update_install.hpp"
#include <bcrypt.h>
#include <shellapi.h>
#include <array>
#include <cstdio>
#include <cwchar>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace input_overlay;
constexpr std::array<const wchar_t*, 4> Names{
    L"Input Overlay.exe", L"LICENSE", L"README.md", L"THIRD_PARTY_NOTICES.txt"
};
void Check(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
std::wstring Join(const std::wstring& directory, const std::wstring& name) {
    return directory + L"\\" + name;
}
void Write(const std::wstring& path, const std::vector<unsigned char>& bytes) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "Test file must open for writing");
    DWORD written = 0;
    bool valid = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) && written == bytes.size();
    CloseHandle(file);
    Check(valid, "Test file must be written completely");
}
void Write(const std::wstring& path, const std::string& text) {
    Write(path, std::vector<unsigned char>(text.begin(), text.end()));
}
std::vector<unsigned char> Read(const std::wstring& path) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(file != INVALID_HANDLE_VALUE, "Test file must open for reading");
    LARGE_INTEGER size{};
    Check(GetFileSizeEx(file, &size) && size.QuadPart >= 0 && size.QuadPart < 128 * 1024 * 1024, "Test file size must be bounded");
    std::vector<unsigned char> bytes(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    bool valid = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
    CloseHandle(file);
    Check(valid, "Test file must be read completely");
    return bytes;
}
std::string Hash(const std::vector<unsigned char>& bytes) {
    BCRYPT_ALG_HANDLE provider = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    Check(BCryptOpenAlgorithmProvider(&provider, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0, "SHA provider must open");
    Check(BCryptCreateHash(provider, &hash, nullptr, 0, nullptr, 0, 0) >= 0, "SHA hash must create");
    Check(BCryptHashData(hash, const_cast<unsigned char*>(bytes.data()), static_cast<ULONG>(bytes.size()), 0) >= 0, "SHA data must process");
    std::array<unsigned char, 32> digest{};
    Check(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0, "SHA hash must finish");
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(provider, 0);
    std::string result;
    constexpr char hex[] = "0123456789abcdef";
    for (auto byte : digest) { result.push_back(hex[byte >> 4]); result.push_back(hex[byte & 15]); }
    return result;
}
std::vector<unsigned char> Candidate() {
    std::array<wchar_t, 32768> path{};
    Check(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())) != 0, "Fixture executable path must resolve");
    auto bytes = Read(path.data());
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(bytes.data());
    Check(bytes.size() > sizeof(IMAGE_DOS_HEADER) && dos->e_lfanew > 0 &&
        static_cast<size_t>(dos->e_lfanew) + sizeof(IMAGE_NT_HEADERS64) < bytes.size(), "Fixture must be a PE file");
    auto* pe = reinterpret_cast<IMAGE_NT_HEADERS64*>(bytes.data() + dos->e_lfanew);
    pe->OptionalHeader.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_GUI;
    return bytes;
}
struct Fixture {
    std::wstring directory;
    UpdateInstallPlan plan;
    std::array<std::vector<unsigned char>, 4> original;
    std::array<std::vector<unsigned char>, 4> replacement;
    std::vector<unsigned char> ini;
    Fixture() {
        static unsigned serial = 0;
        std::array<wchar_t, 32768> temporary{};
        DWORD length = GetTempPathW(static_cast<DWORD>(temporary.size()), temporary.data());
        Check(length > 0 && length < temporary.size(), "Temporary directory must resolve");
        directory = std::wstring(temporary.data()) + L"InputOverlayInstallTest-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64()) + L"-" + std::to_wstring(++serial);
        Check(CreateDirectoryW(directory.c_str(), nullptr) != FALSE, "Fixture directory must be new");
        plan.stagingDirectory = Join(directory, L".input-overlay-update-12345678");
        plan.targetExecutable = Join(directory, Names[0]);
        Check(CreateDirectoryW(plan.stagingDirectory.c_str(), nullptr) != FALSE, "Staging directory must be new");
        for (size_t index = 0; index < Names.size(); ++index) {
            std::string text = "Original file " + std::to_string(index);
            original[index] = {text.begin(), text.end()};
            text = "Replacement file " + std::to_string(index);
            replacement[index] = index ? std::vector<unsigned char>(text.begin(), text.end()) : Candidate();
            Write(Join(directory, Names[index]), original[index]);
            Write(Join(plan.stagingDirectory, Names[index]), replacement[index]);
            plan.files[index] = {Names[index], Hash(replacement[index]), replacement[index].size()};
        }
        const std::string settings = "[overlay]\nenabled=0\nstartMinimized=1\nautomaticUpdateChecks=0\n";
        ini = {settings.begin(), settings.end()};
        Write(Join(directory, L"Input Overlay.ini"), ini);
    }
    ~Fixture() {
        for (size_t index = 0; index < Names.size(); ++index) {
            DeleteFileW(Join(directory, Names[index]).c_str());
            DeleteFileW(Join(plan.stagingDirectory, Names[index]).c_str());
            DeleteFileW(Join(plan.stagingDirectory, L"backup-" + std::to_wstring(index)).c_str());
        }
        DeleteFileW(Join(plan.stagingDirectory, L"linked-file").c_str());
        DeleteFileW(Join(plan.stagingDirectory, L"InputOverlayUpdater.exe").c_str());
        DeleteFileW(Join(directory, L"Input Overlay.ini").c_str());
        DeleteFileW(Join(directory, L"probe-started").c_str());
        DeleteFileW(Join(directory, L"probe-ready").c_str());
        DeleteFileW(Join(directory, L"probe-error").c_str());
        RemoveDirectoryW(plan.stagingDirectory.c_str());
        RemoveDirectoryW(directory.c_str());
    }
    void CheckOriginal(bool lastMissing = false) {
        for (size_t index = 0; index < Names.size(); ++index) {
            if (lastMissing && index == Names.size() - 1) {
                Check(GetFileAttributesW(Join(directory, Names[index]).c_str()) == INVALID_FILE_ATTRIBUTES,
                    "New documentation must be removed after rollback");
            } else Check(Read(Join(directory, Names[index])) == original[index], "Original files must survive failed updates");
        }
        Check(Read(Join(directory, L"Input Overlay.ini")) == ini, "Personal settings must remain byte-for-byte unchanged");
    }
};
bool RejectRestart(void* context, std::wstring& error) {
    ++*static_cast<int*>(context);
    error = L"Simulated application startup failure.";
    return false;
}
bool AcceptRestart(void* context, std::wstring&) {
    auto& fixture = *static_cast<Fixture*>(context);
    for (size_t index = 0; index < Names.size(); ++index)
        Check(Read(Join(fixture.directory, Names[index])) == fixture.replacement[index], "All files must be installed before relaunch");
    Check(Read(Join(fixture.directory, L"Input Overlay.ini")) == fixture.ini, "Settings must survive installation");
    return true;
}
void TestValidation() {
    Fixture fixture;
    std::wstring error;
    Check(Hash({'a', 'b', 'c'}) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "SHA-256 must match an independent known digest");
    Check(ValidateUpdateInstallPlan(fixture.plan, error), "Complete verified update must be accepted");
    auto bad = fixture.plan;
    bad.files[1].name = L"..\\Input Overlay.ini";
    Check(!ValidateUpdateInstallPlan(bad, error), "Path traversal names must fail");
    bad = fixture.plan; bad.files[1].name = L"Input Overlay.ini";
    Check(!ValidateUpdateInstallPlan(bad, error), "The update must never include settings");
    bad = fixture.plan; bad.files[1].sha256[0] = bad.files[1].sha256[0] == 'a' ? 'b' : 'a';
    Check(!ValidateUpdateInstallPlan(bad, error), "Wrong SHA-256 must fail");
    bad = fixture.plan; ++bad.files[1].size;
    Check(!ValidateUpdateInstallPlan(bad, error), "Wrong file size must fail");
    bad = fixture.plan; bad.stagingDirectory = fixture.directory;
    Check(!ValidateUpdateInstallPlan(bad, error), "The application folder must not be treated as staging");
    bad = fixture.plan; bad.targetExecutable = Join(fixture.directory, L"Other.exe");
    Check(!ValidateUpdateInstallPlan(bad, error), "Unexpected target names must fail");
    bad = fixture.plan; bad.targetExecutable += L":alternate";
    Check(!ValidateUpdateInstallPlan(bad, error), "Alternate data stream targets must fail");
    bad = fixture.plan; bad.targetExecutable = Join(fixture.directory, L"..\\Input Overlay.exe");
    Check(!ValidateUpdateInstallPlan(bad, error), "Dot-segment targets must fail");
    bad = fixture.plan; bad.files[1].sha256 = std::string(64, 'A');
    Check(!ValidateUpdateInstallPlan(bad, error), "Noncanonical hashes must fail");
    Write(Join(fixture.plan.stagingDirectory, L"backup-0"), "Recovery");
    Check(!ValidateUpdateInstallPlan(fixture.plan, error), "Existing recovery files must never be overwritten");
    DeleteFileW(Join(fixture.plan.stagingDirectory, L"backup-0").c_str());
    Check(CreateHardLinkW(Join(fixture.plan.stagingDirectory, L"linked-file").c_str(),
        Join(fixture.plan.stagingDirectory, Names[1]).c_str(), nullptr) != FALSE, "Hard link fixture must create");
    Check(!ValidateUpdateInstallPlan(fixture.plan, error), "Hard-linked update files must fail");
    DeleteFileW(Join(fixture.plan.stagingDirectory, L"linked-file").c_str());
    Write(Join(fixture.plan.stagingDirectory, Names[0]), "Not a PE image");
    const auto text = Read(Join(fixture.plan.stagingDirectory, Names[0]));
    fixture.plan.files[0].sha256 = Hash(text); fixture.plan.files[0].size = text.size();
    Check(!ValidateUpdateInstallPlan(fixture.plan, error), "A valid digest must not make a non-executable payload acceptable");
    fixture.CheckOriginal();
}
void TestSuccessfulTransaction() {
    Fixture fixture;
    std::wstring error;
    Check(ApplyUpdateTransaction(fixture.plan, AcceptRestart, &fixture, error), "Verified update must install and relaunch");
    for (size_t index = 0; index < Names.size(); ++index) {
        Check(GetFileAttributesW(Join(fixture.plan.stagingDirectory, L"backup-" + std::to_wstring(index)).c_str()) == INVALID_FILE_ATTRIBUTES,
            "Successful update must remove recovery copies");
    }
}
void TestStartupRollback() {
    Fixture fixture;
    std::wstring error;
    int restarts = 0;
    DeleteFileW(Join(fixture.directory, Names[3]).c_str());
    Check(!ApplyUpdateTransaction(fixture.plan, RejectRestart, &restarts, error), "Startup failure must fail the transaction");
    Check(restarts == 1, "Updated application must be attempted once");
    Check(error.find(L"restored") != std::wstring::npos, "Rollback must be reported");
    fixture.CheckOriginal(true);
}
void TestPartialReplacementRollback() {
    Fixture fixture;
    std::wstring error;
    int restarts = 0;
    HANDLE locked = CreateFileW(Join(fixture.directory, Names[2]).c_str(), GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    Check(locked != INVALID_HANDLE_VALUE, "Target lock must be obtained");
    const bool installed = ApplyUpdateTransaction(fixture.plan, RejectRestart, &restarts, error);
    CloseHandle(locked);
    Check(!installed && restarts == 0, "Locked target must abort before launch");
    fixture.CheckOriginal();
}
void TestTamperingBeforeCommit() {
    Fixture fixture;
    std::wstring error;
    Check(ValidateUpdateInstallPlan(fixture.plan, error), "Initial plan must validate");
    Write(Join(fixture.plan.stagingDirectory, Names[1]), "Changed after preflight");
    int restarts = 0;
    Check(!ApplyUpdateTransaction(fixture.plan, RejectRestart, &restarts, error) && restarts == 0,
        "Transaction must independently revalidate staged content");
    fixture.CheckOriginal();
}
std::wstring ThisExecutable() {
    std::array<wchar_t, 32768> path{};
    Check(GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size())) != 0, "Process path must resolve");
    return path.data();
}
std::wstring Parent(const std::wstring& path) {
    return path.substr(0, path.find_last_of(L'\\'));
}
int ProbeMode(int count, wchar_t** arguments) {
    if (count == 3 && std::wcscmp(arguments[1], L"--handoff-probe") == 0) {
        UpdateInstallPlan plan;
        plan.targetExecutable = ThisExecutable();
        plan.stagingDirectory = arguments[2];
        for (size_t index = 0; index < Names.size(); ++index) {
            const auto bytes = Read(Join(plan.stagingDirectory, Names[index]));
            plan.files[index] = {Names[index], Hash(bytes), bytes.size()};
        }
        std::wstring error;
        if (!BeginUpdateInstall(plan, error)) {
            Write(Join(Parent(plan.targetExecutable), L"probe-error"), std::string(error.begin(), error.end()));
            return 3;
        }
        Write(Join(Parent(plan.targetExecutable), L"probe-started"), "started");
        return 0;
    }
    if (count == 5 && std::wcscmp(arguments[1], L"--background") == 0 &&
        std::wcscmp(arguments[2], L"--update-ready") == 0) {
        SignalUpdateStartupReady();
        Write(Join(Parent(ThisExecutable()), L"probe-ready"), "ready");
        Sleep(1500);
        return 0;
    }
    return -1;
}
void TestProcessHandoff() {
    Fixture fixture;
    Write(fixture.plan.targetExecutable, fixture.replacement[0]);
    std::wstring command = L"\"" + fixture.plan.targetExecutable + L"\" --handoff-probe \"" + fixture.plan.stagingDirectory + L"\"";
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    Check(CreateProcessW(fixture.plan.targetExecutable.c_str(), command.data(), nullptr, nullptr, FALSE,
        CREATE_NO_WINDOW, nullptr, fixture.directory.c_str(), &startup, &process) != FALSE,
        "Isolated application must launch");
    CloseHandle(process.hThread);
    DWORD wait = WaitForSingleObject(process.hProcess, 15000);
    DWORD result = 1;
    GetExitCodeProcess(process.hProcess, &result);
    CloseHandle(process.hProcess);
    Check(wait == WAIT_OBJECT_0 && result == 0, "Original application must finish a successful helper handoff");
    const auto deadline = GetTickCount64() + 10000;
    while (GetTickCount64() < deadline && (GetFileAttributesW(Join(fixture.directory, L"probe-ready").c_str()) == INVALID_FILE_ATTRIBUTES ||
        GetFileAttributesW(fixture.plan.stagingDirectory.c_str()) != INVALID_FILE_ATTRIBUTES)) Sleep(20);
    Check(GetFileAttributesW(Join(fixture.directory, L"probe-started").c_str()) != INVALID_FILE_ATTRIBUTES,
        "The original process must hand off before it exits");
    Check(GetFileAttributesW(Join(fixture.directory, L"probe-ready").c_str()) != INVALID_FILE_ATTRIBUTES,
        "Updated application must signal successful initialization");
    Check(GetFileAttributesW(fixture.plan.stagingDirectory.c_str()) == INVALID_FILE_ATTRIBUTES,
        "The updated application must clean its helper after helper exit");
    for (size_t index = 0; index < Names.size(); ++index)
        Check(Read(Join(fixture.directory, Names[index])) == fixture.replacement[index], "Handoff must commit every update asset");
    Check(Read(Join(fixture.directory, L"Input Overlay.ini")) == fixture.ini, "Handoff must preserve personal settings");
    Sleep(1600);
}
}
int main() {
    try {
        int helperResult = 0;
        if (input_overlay::RunUpdateInstallerMode(helperResult)) return helperResult;
        int count = 0;
        wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
        Check(arguments != nullptr, "Command line must parse");
        int probe = ProbeMode(count, arguments);
        LocalFree(arguments);
        if (probe >= 0) return probe;
        TestValidation();
        TestSuccessfulTransaction();
        TestStartupRollback();
        TestPartialReplacementRollback();
        TestTamperingBeforeCommit();
        TestProcessHandoff();
        std::puts("Update installer validation, replacement, rollback, settings preservation, and process handoff passed.");
        return 0;
    } catch (const std::exception& failure) {
        std::fprintf(stderr, "%s\n", failure.what());
        return 1;
    }
}
