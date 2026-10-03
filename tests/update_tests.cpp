#include "updates.hpp"
#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <vector>

using namespace input_overlay;
namespace {
int failures = 0;
void Check(bool condition, const char* name) {
    if (!condition) { std::printf("FAIL: %s\n", name); ++failures; }
}
const std::string Commit(40, 'a');
const std::string Digest(64, 'b');
std::string Manifest() {
    return "{\"schema\":1,\"commit\":\"" + Commit + "\",\"version\":\"0.1.0\",\"notes\":[\"Fixed controller reconnects.\",\"Improved transparency \\u2728\"],\"files\":["
        "{\"name\":\"Input Overlay.exe\",\"sha256\":\"" + Digest + "\",\"size\":1000},"
        "{\"name\":\"LICENSE\",\"sha256\":\"" + Digest + "\",\"size\":2000},"
        "{\"name\":\"README.md\",\"sha256\":\"" + Digest + "\",\"size\":3000},"
        "{\"name\":\"THIRD_PARTY_NOTICES.txt\",\"sha256\":\"" + Digest + "\",\"size\":4000}]}";
}
std::string Replace(std::string text, const std::string& from, const std::string& to) {
    size_t position = text.find(from);
    if (position != std::string::npos) text.replace(position, from.size(), to);
    return text;
}
bool Accept(const std::string& json) { UpdateInfo info; return ParseUpdateManifest(json, Commit, info); }
struct Temporary {
    std::wstring path;
    Temporary() {
        std::array<wchar_t, MAX_PATH + 1> directory{}, file{};
        if (GetTempPathW(static_cast<DWORD>(directory.size()), directory.data()) && GetTempFileNameW(directory.data(), L"iou", 0, file.data())) path = file.data();
    }
    ~Temporary() { if (!path.empty()) DeleteFileW(path.c_str()); }
    bool Write(const void* data, size_t size) const {
        HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, TRUNCATE_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return false;
        DWORD written = 0;
        bool success = WriteFile(file, data, static_cast<DWORD>(size), &written, nullptr) && written == size;
        CloseHandle(file);
        return success;
    }
};
void JsonTests() {
    UpdateInfo info;
    Check(ParseUpdateManifest(Manifest(), Commit, info), "valid manifest");
    Check(info.commit == Commit && info.version == L"0.1.0" && info.files.size() == 4 && info.notes.size() == 2, "manifest fields");
    Check(info.notes[1] == L"Improved transparency \u2728", "unicode notes");
    Check(!ParseUpdateManifest(Manifest(), std::string(40, 'c'), info), "wrong commit");
    Check(!Accept(Replace(Manifest(), "\"schema\":1", "\"schema\":2")), "unknown schema");
    Check(!Accept(Replace(Manifest(), "\"schema\":1", "\"schema\":1,\"schema\":1")), "duplicate field");
    Check(!Accept(Replace(Manifest(), "\"size\":1000", "\"size\":-1")), "negative size");
    Check(!Accept(Replace(Manifest(), "\"size\":1000", "\"size\":0")), "empty executable");
    Check(!Accept(Replace(Manifest(), "\"size\":1000", "\"size\":1e3")), "exponent size");
    Check(!Accept(Replace(Manifest(), "\"size\":1000", "\"size\":01")), "leading zero size");
    Check(!Accept(Replace(Manifest(), "\"size\":1000", "\"size\":33554433")), "executable size cap");
    Check(!Accept(Replace(Manifest(), "\"size\":2000", "\"size\":2097153")), "document size cap");
    Check(!Accept(Replace(Manifest(), "\"size\":1000", "\"size\":18446744073709551616")), "integer overflow");
    Check(!Accept(Replace(Manifest(), "Input Overlay.exe", "../Input Overlay.exe")), "relative path rejected");
    Check(!Accept(Replace(Manifest(), "Input Overlay.exe", "Input Overlay.exe:payload")), "alternate stream rejected");
    Check(!Accept(Replace(Manifest(), "Input Overlay.exe", "Input-Overlay.exe")), "remote name not local file");
    Check(!Accept(Replace(Manifest(), "LICENSE", "README.md")), "duplicate files");
    Check(!Accept(Replace(Manifest(), "LICENSE", "settings.ini")), "settings file rejected");
    Check(!Accept(Replace(Manifest(), Digest, std::string(64, 'z'))), "invalid digest");
    Check(!Accept(Replace(Manifest(), Digest, std::string(63, 'b'))), "short digest");
    Check(!Accept(Manifest() + "x"), "trailing bytes");
    Check(!Accept(Manifest().substr(0, Manifest().size() - 1)), "truncated manifest");
    Check(!Accept(Replace(Manifest(), "Fixed controller reconnects.", "bad\\u0000text")), "NUL in notes");
    Check(!Accept(Replace(Manifest(), "Fixed controller reconnects.", "bad\\u202etext")), "bidirectional override");
    Check(!Accept(Replace(Manifest(), "Fixed controller reconnects.", "bad\\ud800text")), "unpaired surrogate");
    Check(!Accept(Replace(Manifest(), "Fixed controller reconnects.", std::string("bad\xc0\x80", 5))), "overlong UTF8");
    Check(!Accept(Replace(Manifest(), "Fixed controller reconnects.", std::string(2049, 'x'))), "note size cap");
    Check(Accept(Replace(Manifest(), "Fixed controller reconnects.", "Controller \\ud83c\\udfae")), "surrogate pair");
    Check(!Accept(std::string(2 * 1024 * 1024 + 1, ' ')), "response size cap");
    std::string sha;
    const std::string reference = "{\"ref\":\"refs/heads/main\",\"object\":{\"type\":\"commit\",\"sha\":\"" + Commit + "\"},\"unused\":{\"empty\":[]}}";
    Check(ParseMainCommit(reference, sha) && sha == Commit, "main reference parse");
    Check(!ParseMainCommit(Replace(reference, Commit, std::string(40, 'A')), sha), "noncanonical SHA");
    Check(!ParseMainCommit(Replace(reference, "\"sha\":\"", "\"sha\":\"" + Commit + "\",\"sha\":\""), sha), "duplicate commit field");
    Check(!ParseMainCommit(Replace(reference, "refs/heads/main", "refs/heads/main-extra"), sha), "exact branch required");
    Check(!ParseMainCommit(Replace(reference, "refs/heads/main", "refs/tags/main"), sha), "tag is not main branch");
    Check(!ParseMainCommit(Replace(reference, "\"type\":\"commit\"", "\"type\":\"tag\""), sha), "commit object required");
    Check(!ParseMainCommit(Replace(reference, "\"object\"", "\"unusedObject\""), sha), "reference object required");
    Check(!ParseMainCommit("{\"sha\":\"" + Commit + "\"}", sha), "commit endpoint shape rejected");
    Check(!ParseMainCommit("[" + reference + "]", sha), "matching reference array rejected");
    Check(!ParseMainCommit(Replace(reference, "\"empty\":[]", "\"large\":\"" + std::string(16 * 1024, 'x') + "\""), sha), "reference response size cap");
    std::string deep(30, '['); deep += "0"; deep += std::string(30, ']');
    Check(!ParseMainCommit(Replace(reference, "\"empty\":[]", "\"empty\":" + deep), sha), "nesting cap");
    std::string notes;
    for (int i = 0; i < 32; ++i) { if (i) notes += ','; notes += '"' + std::string(2048, 'x') + '"'; }
    const std::string boundedNotes = Replace(Manifest(), "\"Fixed controller reconnects.\",\"Improved transparency \\u2728\"", notes);
    Check(Accept(boundedNotes), "32 maximum length notes accepted");
    Check(!Accept(Replace(boundedNotes, notes, notes + ",\"extra\"")), "33 notes rejected");
}
void UrlTests() {
    const std::wstring commit(Commit.begin(), Commit.end());
    const std::wstring valid = L"https://github.com/pwarea/input-overlay/releases/download/build-" + commit + L"/Input-Overlay.exe";
    Check(IsUpdateDownloadUrl(valid, Commit, L"Input Overlay.exe"), "canonical executable URL");
    Check(!IsUpdateDownloadUrl(valid + L"?redirect=evil", Commit, L"Input Overlay.exe"), "unexpected primary query");
    Check(!IsUpdateDownloadUrl(valid + L"#file", Commit, L"Input Overlay.exe"), "URL fragment");
    Check(!IsUpdateDownloadUrl(L"http" + valid.substr(5), Commit, L"Input Overlay.exe"), "insecure transport");
    Check(!IsUpdateDownloadUrl(L"https://github.com.evil.example/file", Commit, L"Input Overlay.exe"), "lookalike host");
    Check(!IsUpdateDownloadUrl(L"https://github.com@evil.example/file", Commit, L"Input Overlay.exe"), "userinfo confusion");
    Check(!IsUpdateDownloadUrl(L"https://evil@release-assets.githubusercontent.com/file", Commit, L"Input Overlay.exe"), "userinfo on approved host");
    Check(!IsUpdateDownloadUrl(valid, std::string(40, 'c'), L"Input Overlay.exe"), "URL pinned commit");
    Check(!IsUpdateDownloadUrl(valid, Commit, L"LICENSE"), "URL pinned filename");
    Check(IsUpdateDownloadUrl(L"https://release-assets.githubusercontent.com/github-production-release-asset/123/file?se=2030&sig=value", Commit, L"Input Overlay.exe"), "GitHub signed CDN redirect");
    Check(!IsUpdateDownloadUrl(L"https://release-assets.githubusercontent.com:444/file", Commit, L"Input Overlay.exe"), "unexpected port");
    Check(!IsUpdateDownloadUrl(L"https://release-assets.githubusercontent.com/../file", Commit, L"Input Overlay.exe"), "CDN dot traversal");
    Check(!IsUpdateDownloadUrl(L"https://release-assets.githubusercontent.com/\\evil", Commit, L"Input Overlay.exe"), "backslash URL");
    Check(!IsUpdateDownloadUrl(L"https://release-assets.githubusercontent.com/file", Commit, L"preferences.ini"), "nonallowlisted asset");
}
void ReleaseTests() {
    const std::array<const char*, 5> names = { "Input-Overlay.exe", "LICENSE", "README.md", "THIRD_PARTY_NOTICES.txt", "update.json" };
    std::string release = "{\"tag_name\":\"build-" + Commit + "\",\"draft\":false,\"assets\":[";
    for (size_t i = 0; i < names.size(); ++i) {
        if (i) release += ',';
        release += "{\"name\":\"" + std::string(names[i]) + "\",\"state\":\"uploaded\",\"size\":" + std::to_string((i + 1) * 1000) + ",\"browser_download_url\":\"https://github.com/pwarea/input-overlay/releases/download/build-" + Commit + "/" + names[i] + "\"}";
    }
    release += "]}";
    UpdateInfo info;
    Check(ParseUpdateManifest(Manifest(), Commit, info), "release manifest fixture");
    Check(ValidateUpdateRelease(release, Commit), "complete published release");
    Check(ValidateUpdateRelease(release, Commit, &info), "release sizes agree with manifest");
    Check(!ValidateUpdateRelease(release, std::string(40, 'c')), "wrong release tag");
    Check(!ValidateUpdateRelease(Replace(release, "\"draft\":false", "\"draft\":true"), Commit), "draft release rejected");
    Check(!ValidateUpdateRelease(Replace(release, "\"size\":1000", "\"size\":1001"), Commit, &info), "release size mismatch");
    Check(!ValidateUpdateRelease(Replace(release, "\"name\":\"LICENSE\"", "\"name\":\"README.md\""), Commit), "duplicate release asset");
    Check(!ValidateUpdateRelease(Replace(release, "\"name\":\"LICENSE\"", "\"name\":\"UNKNOWN\""), Commit), "missing release asset");
    Check(!ValidateUpdateRelease(Replace(release, "\"name\":\"Input-Overlay.exe\"", "\"name\":\"Input Overlay.exe\""), Commit), "unmapped release filename");
    Check(!ValidateUpdateRelease(Replace(release, "\"state\":\"uploaded\"", "\"state\":\"starter\""), Commit), "unfinished release asset");
    Check(!ValidateUpdateRelease(Replace(release, "https://github.com/pwarea", "https://github.com/other"), Commit), "foreign repository asset");
    Check(!ValidateUpdateRelease(Replace(release, "\"size\":5000", "\"size\":2097153"), Commit), "manifest asset size limit");
}
void FileTests() {
    Temporary file;
    Check(!file.path.empty() && file.Write("abc", 3), "temporary hash fixture");
    UpdateFile expected{ L"LICENSE", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", 3 };
    Check(VerifyUpdateFile(file.path, expected), "SHA256 exact file");
    expected.size = 4;
    Check(!VerifyUpdateFile(file.path, expected), "file length mismatch");
    expected.size = 3; expected.sha256[0] = '0';
    Check(!VerifyUpdateFile(file.path, expected), "file digest mismatch");
    Check(!IsUpdateExecutable(file.path), "text is not executable");
    std::vector<unsigned char> executable(1024);
    IMAGE_DOS_HEADER dos{};
    dos.e_magic = IMAGE_DOS_SIGNATURE; dos.e_lfanew = 128;
    std::copy(reinterpret_cast<unsigned char*>(&dos), reinterpret_cast<unsigned char*>(&dos) + sizeof(dos), executable.begin());
    IMAGE_NT_HEADERS64 nt{};
    nt.Signature = IMAGE_NT_SIGNATURE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER64);
    nt.FileHeader.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE;
    nt.OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_GUI;
    auto writeNt = [&] { std::copy(reinterpret_cast<unsigned char*>(&nt), reinterpret_cast<unsigned char*>(&nt) + sizeof(nt), executable.begin() + dos.e_lfanew); return file.Write(executable.data(), executable.size()); };
    Check(writeNt() && IsUpdateExecutable(file.path), "x64 GUI header accepted");
    nt.FileHeader.Characteristics |= IMAGE_FILE_DLL;
    Check(writeNt() && !IsUpdateExecutable(file.path), "DLL rejected");
    nt.FileHeader.Characteristics = IMAGE_FILE_EXECUTABLE_IMAGE;
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_I386;
    Check(writeNt() && !IsUpdateExecutable(file.path), "wrong architecture rejected");
    nt.FileHeader.Machine = IMAGE_FILE_MACHINE_AMD64;
    nt.OptionalHeader.Subsystem = IMAGE_SUBSYSTEM_WINDOWS_CUI;
    Check(writeNt() && !IsUpdateExecutable(file.path), "console helper rejected");
}
void WorkerTests() {
    UpdateService service;
    Check(service.Snapshot().status == UpdateStatus::Idle && service.Snapshot().generation == 0, "initial updater state");
    Check(!service.Download({}, L"", nullptr), "invalid download does not start");
    ULONGLONG start = GetTickCount64();
    for (int i = 0; i < 3; ++i) {
        Check(service.Check("", nullptr), "worker starts");
        service.Stop();
    }
    Check(GetTickCount64() - start < 5000, "cancelled workers stop promptly");
    Check(service.Snapshot().generation >= 6, "result generations advance");
}
}
int main() {
    JsonTests(); UrlTests(); ReleaseTests(); FileTests(); WorkerTests();
    if (!failures) std::puts("Update parsing, URL restrictions, integrity and cancellation checks passed.");
    return failures ? 1 : 0;
}
