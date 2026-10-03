#include "updates.hpp"
#include <winhttp.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <limits>
#include <utility>

#ifndef INPUT_OVERLAY_BUILD_COMMIT
#define INPUT_OVERLAY_BUILD_COMMIT ""
#endif

namespace input_overlay {
namespace {
constexpr size_t MaxJson = 2 * 1024 * 1024;
constexpr size_t MaxReferenceJson = 16 * 1024;
constexpr uint64_t MaxExecutable = 32 * 1024 * 1024;
constexpr uint64_t MaxDocument = 2 * 1024 * 1024;
constexpr wchar_t Repository[] = L"pwarea/input-overlay";
constexpr std::array<const wchar_t*, 4> Names = {
    L"Input Overlay.exe", L"LICENSE", L"README.md", L"THIRD_PARTY_NOTICES.txt"
};
struct Json {
    enum class Kind { Null, String, Number, Boolean, Array, Object } kind = Kind::Null;
    std::string text;
    std::vector<Json> values;
    std::vector<std::string> keys;
    const Json* Get(const char* key) const {
        if (kind != Kind::Object) return nullptr;
        for (size_t i = 0; i < keys.size(); ++i) if (keys[i] == key) return &values[i];
        return nullptr;
    }
};
bool Utf8(const std::string& value, std::wstring& result) {
    if (value.empty()) { result.clear(); return true; }
    int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (!length) return false;
    result.resize(length);
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), result.data(), length) == length;
}
class Parser {
public:
    explicit Parser(const std::string& text) : text_(text) {}
    bool Read(Json& result) {
        if (text_.size() > MaxJson || !Value(result, 0)) return false;
        Space();
        return offset_ == text_.size();
    }
private:
    void Space() { while (offset_ < text_.size() && (text_[offset_] == ' ' || text_[offset_] == '\r' || text_[offset_] == '\n' || text_[offset_] == '\t')) ++offset_; }
    bool Take(char c) { Space(); if (offset_ < text_.size() && text_[offset_] == c) { ++offset_; return true; } return false; }
    bool Hex(unsigned& value) {
        value = 0;
        for (int n = 0; n < 4; ++n) {
            if (offset_ == text_.size()) return false;
            char c = text_[offset_++];
            unsigned digit = c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : 16;
            if (digit == 16) return false;
            value = value * 16 + digit;
        }
        return true;
    }
    bool String(std::string& value) {
        if (!Take('"')) return false;
        value.clear();
        while (offset_ < text_.size()) {
            unsigned char c = static_cast<unsigned char>(text_[offset_++]);
            if (c == '"') { std::wstring valid; return Utf8(value, valid); }
            if (c < 32) return false;
            if (c != '\\') { value.push_back(static_cast<char>(c)); continue; }
            if (offset_ == text_.size()) return false;
            c = static_cast<unsigned char>(text_[offset_++]);
            if (c == '"' || c == '\\' || c == '/') value.push_back(static_cast<char>(c));
            else if (c == 'b') value.push_back('\b');
            else if (c == 'f') value.push_back('\f');
            else if (c == 'n') value.push_back('\n');
            else if (c == 'r') value.push_back('\r');
            else if (c == 't') value.push_back('\t');
            else if (c == 'u') {
                unsigned code = 0;
                if (!Hex(code)) return false;
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (offset_ + 2 > text_.size() || text_[offset_] != '\\' || text_[offset_ + 1] != 'u') return false;
                    offset_ += 2;
                    unsigned low = 0;
                    if (!Hex(low) || low < 0xdc00 || low > 0xdfff) return false;
                    code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                } else if (code >= 0xdc00 && code <= 0xdfff) return false;
                if (code < 0x80) value.push_back(static_cast<char>(code));
                else if (code < 0x800) {
                    value.push_back(static_cast<char>(0xc0 | (code >> 6)));
                    value.push_back(static_cast<char>(0x80 | (code & 63)));
                } else if (code < 0x10000) {
                    value.push_back(static_cast<char>(0xe0 | (code >> 12)));
                    value.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
                    value.push_back(static_cast<char>(0x80 | (code & 63)));
                } else {
                    value.push_back(static_cast<char>(0xf0 | (code >> 18)));
                    value.push_back(static_cast<char>(0x80 | ((code >> 12) & 63)));
                    value.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
                    value.push_back(static_cast<char>(0x80 | (code & 63)));
                }
            } else return false;
        }
        return false;
    }
    bool Value(Json& value, unsigned depth) {
        if (depth > 20 || ++nodes_ > 30000) return false;
        Space();
        if (offset_ == text_.size()) return false;
        char c = text_[offset_];
        if (c == '"') { value.kind = Json::Kind::String; return String(value.text); }
        if (c == '{' || c == '[') {
            bool object = c == '{';
            value.kind = object ? Json::Kind::Object : Json::Kind::Array;
            ++offset_;
            if (Take(object ? '}' : ']')) return true;
            do {
                if (object) {
                    std::string key;
                    if (value.keys.size() >= 256 || !String(key) || key.size() > 256 || !Take(':') || std::find(value.keys.begin(), value.keys.end(), key) != value.keys.end()) return false;
                    value.keys.push_back(std::move(key));
                }
                value.values.emplace_back();
                if (!Value(value.values.back(), depth + 1)) return false;
                if (Take(object ? '}' : ']')) return true;
            } while (Take(','));
            return false;
        }
        if (c == 't' || c == 'f' || c == 'n') {
            const char* literal = c == 't' ? "true" : c == 'f' ? "false" : "null";
            size_t length = c == 'f' ? 5 : 4;
            if (text_.compare(offset_, length, literal) != 0) return false;
            offset_ += length;
            value.kind = c == 'n' ? Json::Kind::Null : Json::Kind::Boolean;
            value.text = literal;
            return true;
        }
        size_t start = offset_;
        if (text_[offset_] == '-') ++offset_;
        if (offset_ == text_.size()) return false;
        if (text_[offset_] == '0') ++offset_;
        else {
            if (text_[offset_] < '1' || text_[offset_] > '9') return false;
            while (offset_ < text_.size() && text_[offset_] >= '0' && text_[offset_] <= '9') ++offset_;
        }
        if (offset_ < text_.size() && text_[offset_] == '.') {
            size_t decimal = ++offset_;
            while (offset_ < text_.size() && text_[offset_] >= '0' && text_[offset_] <= '9') ++offset_;
            if (offset_ == decimal) return false;
        }
        if (offset_ < text_.size() && (text_[offset_] == 'e' || text_[offset_] == 'E')) {
            ++offset_;
            if (offset_ < text_.size() && (text_[offset_] == '+' || text_[offset_] == '-')) ++offset_;
            size_t exponent = offset_;
            while (offset_ < text_.size() && text_[offset_] >= '0' && text_[offset_] <= '9') ++offset_;
            if (offset_ == exponent) return false;
        }
        value.kind = Json::Kind::Number;
        value.text = text_.substr(start, offset_ - start);
        return true;
    }
    const std::string& text_;
    size_t offset_ = 0, nodes_ = 0;
};
bool StringValue(const Json* value, std::string& text) {
    if (!value || value->kind != Json::Kind::String) return false;
    text = value->text;
    return true;
}
bool UnsignedValue(const Json* value, uint64_t& result) {
    if (!value || value->kind != Json::Kind::Number || value->text.empty()) return false;
    result = 0;
    for (char c : value->text) {
        if (c < '0' || c > '9' || result > (std::numeric_limits<uint64_t>::max() - (c - '0')) / 10) return false;
        result = result * 10 + c - '0';
    }
    return true;
}
bool HexString(const std::string& value, size_t length) {
    return value.size() == length && std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
bool SafeText(const std::wstring& value) {
    for (wchar_t c : value) if (c < 32 || c == 127 || (c >= 0x202a && c <= 0x202e) || (c >= 0x2066 && c <= 0x2069)) return false;
    return true;
}
bool ValidInfo(const UpdateInfo& info) {
    if (!HexString(info.commit, 40) || info.version.empty() || info.version.size() > 64 || !SafeText(info.version) || info.files.size() != Names.size() || info.notes.size() > 32) return false;
    std::array<bool, 4> found{};
    for (const auto& file : info.files) {
        auto it = std::find_if(Names.begin(), Names.end(), [&](const wchar_t* name) { return file.name == name; });
        if (it == Names.end() || !HexString(file.sha256, 64) || !file.size || file.size > (it == Names.begin() ? MaxExecutable : MaxDocument)) return false;
        size_t index = static_cast<size_t>(it - Names.begin());
        if (found[index]) return false;
        found[index] = true;
    }
    for (const auto& note : info.notes) if (note.empty() || note.size() > 2048 || !SafeText(note)) return false;
    return true;
}
std::wstring WideAscii(const std::string& text) { return std::wstring(text.begin(), text.end()); }
std::wstring AssetUrl(const std::string& commit, const std::wstring& asset) {
    std::wstring remoteName = asset == Names[0] ? L"Input-Overlay.exe" : asset;
    return std::wstring(L"https://github.com/") + Repository + L"/releases/download/build-" + WideAscii(commit) + L"/" + remoteName;
}
struct Internet {
    HINTERNET handle = nullptr;
    explicit Internet(HINTERNET value = nullptr) : handle(value) {}
    ~Internet() { if (handle) WinHttpCloseHandle(handle); }
    Internet(const Internet&) = delete;
    Internet& operator=(const Internet&) = delete;
};
struct FileHandle {
    HANDLE handle = INVALID_HANDLE_VALUE;
    explicit FileHandle(HANDLE value) : handle(value) {}
    ~FileHandle() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};
struct AsyncRequest {
    HINTERNET handle = nullptr;
    HANDLE complete = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE closed = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    DWORD status = 0, error = 0, bytes = 0;
    ~AsyncRequest() {
        if (handle) { WinHttpCloseHandle(handle); WaitForSingleObject(closed, INFINITE); }
        if (complete) CloseHandle(complete);
        if (closed) CloseHandle(closed);
    }
    static void CALLBACK Callback(HINTERNET, DWORD_PTR context, DWORD status, LPVOID data, DWORD length) {
        auto* request = reinterpret_cast<AsyncRequest*>(context);
        if (!request) return;
        if (status == WINHTTP_CALLBACK_STATUS_HANDLE_CLOSING) { SetEvent(request->closed); return; }
        if (status != WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE && status != WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE && status != WINHTTP_CALLBACK_STATUS_READ_COMPLETE && status != WINHTTP_CALLBACK_STATUS_REQUEST_ERROR) return;
        request->status = status;
        request->bytes = status == WINHTTP_CALLBACK_STATUS_READ_COMPLETE ? length : 0;
        request->error = status == WINHTTP_CALLBACK_STATUS_REQUEST_ERROR && data ? static_cast<WINHTTP_ASYNC_RESULT*>(data)->dwError : 0;
        SetEvent(request->complete);
    }
    bool Wait(HANDLE cancel, DWORD wanted, DWORD timeout) {
        HANDLE events[] = { cancel, complete };
        return WaitForMultipleObjects(2, events, FALSE, timeout) == WAIT_OBJECT_0 + 1 && status == wanted && !error;
    }
};
struct Url {
    std::wstring host, path;
};
bool CrackUrl(const std::wstring& url, Url& parsed) {
    if (url.size() > 8192 || url.find(L'\0') != std::wstring::npos || url.find(L'\\') != std::wstring::npos || url.find(L'#') != std::wstring::npos) return false;
    for (wchar_t c : url) if (c <= 32 || c == 127) return false;
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts) || parts.nScheme != INTERNET_SCHEME_HTTPS || parts.nPort != INTERNET_DEFAULT_HTTPS_PORT || parts.dwUserNameLength || parts.dwPasswordLength) return false;
    parsed.host.assign(parts.lpszHostName, parts.dwHostNameLength);
    parsed.path.assign(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (parts.dwExtraInfoLength) parsed.path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    return true;
}
bool Cancelled(HANDLE cancel) { return WaitForSingleObject(cancel, 0) == WAIT_OBJECT_0; }
bool ResponseHeader(HINTERNET request, DWORD header, std::wstring& value) {
    DWORD length = 0;
    if (WinHttpQueryHeaders(request, header, WINHTTP_HEADER_NAME_BY_INDEX, nullptr, &length, WINHTTP_NO_HEADER_INDEX) || GetLastError() != ERROR_INSUFFICIENT_BUFFER || length > 16384) return false;
    std::vector<wchar_t> buffer(length / sizeof(wchar_t) + 1);
    if (!WinHttpQueryHeaders(request, header, WINHTTP_HEADER_NAME_BY_INDEX, buffer.data(), &length, WINHTTP_NO_HEADER_INDEX)) return false;
    value.assign(buffer.data(), length / sizeof(wchar_t));
    while (!value.empty() && value.back() == L'\0') value.pop_back();
    return true;
}
bool HttpGet(const std::wstring& initialUrl, size_t limit, HANDLE cancel, std::string& content, DWORD& status,
    const std::string& commit = {}, const std::wstring& asset = {}) {
    content.clear();
    status = 0;
    Internet session(WinHttpOpen(L"Input Overlay updater", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, WINHTTP_FLAG_ASYNC));
    if (!session.handle) return false;
    WinHttpSetTimeouts(session.handle, 4000, 5000, 5000, 5000);
    std::wstring url = initialUrl;
    for (unsigned redirects = 0; redirects <= 4 && !Cancelled(cancel); ++redirects) {
        Url parsed;
        if (!CrackUrl(url, parsed)) return false;
        if (asset.empty()) { if (url != initialUrl || parsed.host != L"api.github.com") return false; }
        else if (!IsUpdateDownloadUrl(url, commit, asset)) return false;
        Internet connection(WinHttpConnect(session.handle, parsed.host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
        if (!connection.handle) return false;
        AsyncRequest request;
        if (!request.complete || !request.closed) return false;
        request.handle = WinHttpOpenRequest(connection.handle, L"GET", parsed.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
        if (!request.handle) return false;
        DWORD_PTR context = reinterpret_cast<DWORD_PTR>(&request);
        if (!WinHttpSetOption(request.handle, WINHTTP_OPTION_CONTEXT_VALUE, &context, sizeof(context))) { WinHttpCloseHandle(request.handle); request.handle = nullptr; return false; }
        if (WinHttpSetStatusCallback(request.handle, AsyncRequest::Callback, WINHTTP_CALLBACK_FLAG_ALL_COMPLETIONS | WINHTTP_CALLBACK_FLAG_HANDLES, 0) == WINHTTP_INVALID_STATUS_CALLBACK) { WinHttpCloseHandle(request.handle); request.handle = nullptr; return false; }
        DWORD disable = WINHTTP_DISABLE_REDIRECTS | WINHTTP_DISABLE_COOKIES | WINHTTP_DISABLE_AUTHENTICATION;
        DWORD noLogon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
        if (!WinHttpSetOption(request.handle, WINHTTP_OPTION_DISABLE_FEATURE, &disable, sizeof(disable)) || !WinHttpSetOption(request.handle, WINHTTP_OPTION_AUTOLOGON_POLICY, &noLogon, sizeof(noLogon))) return false;
        const wchar_t* headers = asset.empty() ? L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\nCache-Control: no-cache\r\n" : L"Accept: application/octet-stream\r\nCache-Control: no-cache\r\n";
        if (!WinHttpSendRequest(request.handle, headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, context) || !request.Wait(cancel, WINHTTP_CALLBACK_STATUS_SENDREQUEST_COMPLETE, 15000)) return false;
        if (!WinHttpReceiveResponse(request.handle, nullptr) || !request.Wait(cancel, WINHTTP_CALLBACK_STATUS_HEADERS_AVAILABLE, 15000)) return false;
        DWORD size = sizeof(status);
        if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) return false;
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            if (asset.empty() || redirects == 4 || !ResponseHeader(request.handle, WINHTTP_QUERY_LOCATION, url)) return false;
            continue;
        }
        if (status != 200) return true;
        std::wstring declared;
        if (ResponseHeader(request.handle, WINHTTP_QUERY_CONTENT_LENGTH, declared)) {
            uint64_t value = 0;
            if (declared.empty() || declared.size() > 20) return false;
            for (wchar_t c : declared) { if (c < L'0' || c > L'9' || value > (std::numeric_limits<uint64_t>::max() - (c - L'0')) / 10) return false; value = value * 10 + c - L'0'; }
            if (value > limit) return false;
            content.reserve(static_cast<size_t>(value));
        }
        std::array<char, 32768> buffer{};
        ULONGLONG started = GetTickCount64();
        while (!Cancelled(cancel) && GetTickCount64() - started < 120000) {
            if (!WinHttpReadData(request.handle, buffer.data(), static_cast<DWORD>(buffer.size()), nullptr) || !request.Wait(cancel, WINHTTP_CALLBACK_STATUS_READ_COMPLETE, 15000)) return false;
            if (!request.bytes) return true;
            if (request.bytes > buffer.size() || request.bytes > limit - content.size()) return false;
            content.append(buffer.data(), request.bytes);
        }
        return false;
    }
    return false;
}
std::wstring HttpError(DWORD status) {
    if (status == 403 || status == 429) return L"GitHub's request limit was reached. Try again later.";
    if (status == 404) return L"Update information is not available on GitHub yet.";
    return L"Could not connect to GitHub or verify its response. Check your connection and try again.";
}
bool ReleaseAssets(const std::string& content, const std::string& commit, const UpdateInfo* info) {
    Json root;
    std::string tag;
    if (!Parser(content).Read(root) || !StringValue(root.Get("tag_name"), tag) || tag != "build-" + commit) return false;
    const Json* draft = root.Get("draft");
    if (!draft || draft->kind != Json::Kind::Boolean || draft->text != "false") return false;
    const Json* assets = root.Get("assets");
    if (!assets || assets->kind != Json::Kind::Array || assets->values.size() > 32) return false;
    std::array<unsigned, 5> found{};
    for (const auto& value : assets->values) {
        std::string name, url, state;
        uint64_t size = 0;
        if (!StringValue(value.Get("name"), name) || !StringValue(value.Get("browser_download_url"), url) || !StringValue(value.Get("state"), state) || !UnsignedValue(value.Get("size"), size)) return false;
        std::wstring wideName, wideUrl;
        if (!Utf8(name, wideName) || !Utf8(url, wideUrl)) return false;
        size_t index = 5;
        if (wideName == L"update.json") index = 4;
        else if (wideName == L"Input-Overlay.exe") index = 0;
        else for (size_t i = 1; i < Names.size(); ++i) if (wideName == Names[i]) index = i;
        if (index == 5) continue;
        const std::wstring localName = index < 4 ? Names[index] : L"update.json";
        if (++found[index] != 1 || state != "uploaded" || wideUrl != AssetUrl(commit, localName) || !size || (index == 4 && size > MaxJson)) return false;
        if (info && index < 4) {
            auto file = std::find_if(info->files.begin(), info->files.end(), [&](const UpdateFile& entry) { return entry.name == localName; });
            if (file == info->files.end() || file->size != size) return false;
        }
    }
    return std::all_of(found.begin(), found.end(), [](unsigned value) { return value == 1; });
}
std::wstring Join(const std::wstring& directory, const std::wstring& name) { return directory + L"\\" + name; }
void CleanStage(const std::wstring& directory) {
    if (directory.empty()) return;
    for (const wchar_t* name : Names) DeleteFileW(Join(directory, name).c_str());
    RemoveDirectoryW(directory.c_str());
}
bool CreateStage(const std::wstring& applicationDirectory, std::wstring& stage) {
    if (applicationDirectory.empty() || applicationDirectory.size() > 30000) return false;
    DWORD attributes = GetFileAttributesW(applicationDirectory.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY) || attributes & FILE_ATTRIBUTE_REPARSE_POINT) return false;
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        std::array<unsigned char, 16> random{};
        if (BCryptGenRandom(nullptr, random.data(), static_cast<ULONG>(random.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG) < 0) return false;
        constexpr wchar_t digits[] = L"0123456789abcdef";
        std::wstring suffix;
        for (unsigned char byte : random) { suffix.push_back(digits[byte >> 4]); suffix.push_back(digits[byte & 15]); }
        std::wstring candidate = Join(applicationDirectory, L".input-overlay-update-" + suffix);
        if (CreateDirectoryW(candidate.c_str(), nullptr)) { stage = std::move(candidate); return true; }
        if (GetLastError() != ERROR_ALREADY_EXISTS) return false;
    }
    return false;
}
}

const char* CurrentBuildCommit() { return INPUT_OVERLAY_BUILD_COMMIT; }
bool ValidateUpdateRelease(const std::string& json, const std::string& commit, const UpdateInfo* info) {
    return HexString(commit, 40) && (!info || (ValidInfo(*info) && info->commit == commit)) && ReleaseAssets(json, commit, info);
}
bool ParseMainCommit(const std::string& json, std::string& commit) {
    Json root;
    std::string reference, type, candidate;
    if (json.size() > MaxReferenceJson || !Parser(json).Read(root) || !StringValue(root.Get("ref"), reference) || reference != "refs/heads/main") return false;
    const Json* object = root.Get("object");
    if (!object || !StringValue(object->Get("type"), type) || type != "commit" || !StringValue(object->Get("sha"), candidate) || !HexString(candidate, 40)) return false;
    commit = std::move(candidate);
    return true;
}
bool ParseUpdateManifest(const std::string& json, const std::string& expectedCommit, UpdateInfo& info) {
    Json root;
    UpdateInfo parsed;
    uint64_t schema = 0;
    std::string version;
    if (!Parser(json).Read(root) || !UnsignedValue(root.Get("schema"), schema) || schema != 1 || !StringValue(root.Get("commit"), parsed.commit) || parsed.commit != expectedCommit || !StringValue(root.Get("version"), version) || !Utf8(version, parsed.version)) return false;
    const Json* notes = root.Get("notes");
    const Json* files = root.Get("files");
    if (!notes || notes->kind != Json::Kind::Array || !files || files->kind != Json::Kind::Array || notes->values.size() > 32 || files->values.size() != 4) return false;
    for (const auto& value : notes->values) {
        std::string text;
        std::wstring wide;
        if (!StringValue(&value, text) || !Utf8(text, wide)) return false;
        parsed.notes.push_back(std::move(wide));
    }
    for (const auto& value : files->values) {
        UpdateFile file;
        std::string name;
        if (!StringValue(value.Get("name"), name) || !Utf8(name, file.name) || !StringValue(value.Get("sha256"), file.sha256) || !UnsignedValue(value.Get("size"), file.size)) return false;
        parsed.files.push_back(std::move(file));
    }
    if (!ValidInfo(parsed)) return false;
    info = std::move(parsed);
    return true;
}
bool IsUpdateDownloadUrl(const std::wstring& url, const std::string& commit, const std::wstring& asset) {
    if (!HexString(commit, 40) || (asset != L"update.json" && std::find_if(Names.begin(), Names.end(), [&](const wchar_t* name) { return asset == name; }) == Names.end())) return false;
    Url parsed;
    if (!CrackUrl(url, parsed)) return false;
    if (parsed.host == L"github.com") return url == AssetUrl(commit, asset);
    return parsed.host == L"release-assets.githubusercontent.com" && parsed.path.size() > 1 && parsed.path.front() == L'/' && parsed.path.find(L"..") == std::wstring::npos;
}
bool VerifyUpdateFile(const std::wstring& path, const UpdateFile& expected) {
    if (!HexString(expected.sha256, 64) || !expected.size || expected.size > MaxExecutable) return false;
    FileHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
    if (file.handle == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION information{};
    if (!GetFileInformationByHandle(file.handle, &information) || information.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    uint64_t length = (static_cast<uint64_t>(information.nFileSizeHigh) << 32) | information.nFileSizeLow;
    if (length != expected.size) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return false;
    bool valid = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    std::array<unsigned char, 65536> buffer{};
    uint64_t total = 0;
    while (valid) {
        DWORD count = 0;
        if (!ReadFile(file.handle, buffer.data(), static_cast<DWORD>(buffer.size()), &count, nullptr)) { valid = false; break; }
        if (!count) break;
        total += count;
        if (total > expected.size || BCryptHashData(hash, buffer.data(), count, 0) < 0) { valid = false; break; }
    }
    std::array<unsigned char, 32> digest{};
    if (valid) valid = total == expected.size && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    constexpr char digits[] = "0123456789abcdef";
    std::string actual;
    for (unsigned char byte : digest) { actual.push_back(digits[byte >> 4]); actual.push_back(digits[byte & 15]); }
    return valid && actual == expected.sha256;
}
bool IsUpdateExecutable(const std::wstring& path) {
    FileHandle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (file.handle == INVALID_HANDLE_VALUE) return false;
    IMAGE_DOS_HEADER dos{};
    DWORD count = 0;
    if (!ReadFile(file.handle, &dos, sizeof(dos), &count, nullptr) || count != sizeof(dos) || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < static_cast<LONG>(sizeof(dos)) || dos.e_lfanew > 1024 * 1024) return false;
    LARGE_INTEGER offset{};
    offset.QuadPart = dos.e_lfanew;
    IMAGE_NT_HEADERS64 nt{};
    if (!SetFilePointerEx(file.handle, offset, nullptr, FILE_BEGIN) || !ReadFile(file.handle, &nt, sizeof(nt), &count, nullptr) || count != sizeof(nt)) return false;
    return nt.Signature == IMAGE_NT_SIGNATURE && nt.FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 && nt.FileHeader.SizeOfOptionalHeader == sizeof(IMAGE_OPTIONAL_HEADER64) && (nt.FileHeader.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) && !(nt.FileHeader.Characteristics & IMAGE_FILE_DLL) && nt.OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC && nt.OptionalHeader.Subsystem == IMAGE_SUBSYSTEM_WINDOWS_GUI;
}
UpdateService::UpdateService() : cancel_(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
UpdateService::~UpdateService() { Stop(); if (cancel_) CloseHandle(cancel_); }
UpdateResult UpdateService::Snapshot() const { std::lock_guard<std::mutex> lock(mutex_); return result_; }
void UpdateService::Publish(UpdateResult result, HWND notifyWindow) {
    { std::lock_guard<std::mutex> lock(mutex_); result.generation = result_.generation + 1; result_ = std::move(result); }
    if (!Cancelled(cancel_) && notifyWindow) PostMessageW(notifyWindow, WM_APP_UPDATE, 0, 0);
}
bool UpdateService::Begin(UpdateStatus status, HWND notifyWindow) {
    if (!cancel_ || running_.exchange(true)) return false;
    if (worker_.joinable()) worker_.join();
    ResetEvent(cancel_);
    UpdateResult result;
    if (status == UpdateStatus::Downloading) result.info = Snapshot().info;
    result.status = status;
    Publish(std::move(result), notifyWindow);
    return true;
}
bool UpdateService::Check(const std::string& currentCommit, HWND notifyWindow) {
    if (!Begin(UpdateStatus::Checking, notifyWindow)) return false;
    try { worker_ = std::thread(&UpdateService::CheckWorker, this, currentCommit, notifyWindow); }
    catch (...) { running_ = false; UpdateResult result; result.status = UpdateStatus::Error; result.message = L"Could not start the update check."; Publish(std::move(result), notifyWindow); return false; }
    return true;
}
bool UpdateService::Download(const UpdateInfo& info, const std::wstring& applicationDirectory, HWND notifyWindow) {
    if (!ValidInfo(info) || !Begin(UpdateStatus::Downloading, notifyWindow)) return false;
    try { worker_ = std::thread(&UpdateService::DownloadWorker, this, info, applicationDirectory, notifyWindow); }
    catch (...) { running_ = false; UpdateResult result; result.status = UpdateStatus::Error; result.message = L"Could not start the update download."; Publish(std::move(result), notifyWindow); return false; }
    return true;
}
void UpdateService::Stop() {
    if (cancel_) SetEvent(cancel_);
    if (worker_.joinable()) worker_.join();
    running_ = false;
}
void UpdateService::CheckWorker(std::string currentCommit, HWND notifyWindow) {
    UpdateResult result;
    result.status = UpdateStatus::Error;
    DWORD status = 0;
    try {
        std::string mainJson;
        if (!HttpGet(std::wstring(L"https://api.github.com/repos/") + Repository + L"/git/ref/heads/main", MaxReferenceJson, cancel_, mainJson, status) || status != 200 || !ParseMainCommit(mainJson, result.info.commit)) result.message = HttpError(status);
        else if (result.info.commit == currentCommit) { result.status = UpdateStatus::UpToDate; result.message = L"You have the latest build from main."; }
        else {
            std::string release;
            if (!HttpGet(std::wstring(L"https://api.github.com/repos/") + Repository + L"/releases/tags/build-" + WideAscii(result.info.commit), MaxJson, cancel_, release, status)) result.message = HttpError(status);
            else if (status == 404) { result.status = UpdateStatus::BuildPending; result.message = L"A newer commit is available. Its downloadable build is not ready yet. Try again later."; }
            else if (status != 200) result.message = HttpError(status);
            else if (!ValidateUpdateRelease(release, result.info.commit)) result.message = L"The update release is incomplete or could not be verified.";
            else {
                std::string manifest;
                UpdateInfo info;
                if (!HttpGet(AssetUrl(result.info.commit, L"update.json"), MaxJson, cancel_, manifest, status, result.info.commit, L"update.json") || status != 200) result.message = HttpError(status);
                else if (!ParseUpdateManifest(manifest, result.info.commit, info) || !ValidateUpdateRelease(release, result.info.commit, &info)) result.message = L"The update manifest did not pass verification.";
                else { result.info = std::move(info); result.status = UpdateStatus::Available; result.message = L"A new update is available."; }
            }
        }
    } catch (...) { result.message = L"The update check could not be completed."; }
    running_ = false;
    Publish(std::move(result), notifyWindow);
}
void UpdateService::DownloadWorker(UpdateInfo info, std::wstring applicationDirectory, HWND notifyWindow) {
    UpdateResult result;
    result.status = UpdateStatus::Error;
    result.info = info;
    std::wstring stage;
    try {
        if (!CreateStage(applicationDirectory, stage)) result.message = L"The application folder is not writable. Move Input Overlay to a folder you own and try again.";
        else {
            bool valid = true;
            for (const auto& file : info.files) {
                std::string content;
                DWORD status = 0;
                if (Cancelled(cancel_) || !HttpGet(AssetUrl(info.commit, file.name), static_cast<size_t>(file.size), cancel_, content, status, info.commit, file.name) || status != 200 || content.size() != file.size) { result.message = L"The update download was interrupted or incomplete. Try again."; valid = false; break; }
                std::wstring path = Join(stage, file.name);
                {
                    FileHandle output(CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
                    DWORD written = 0;
                    if (output.handle == INVALID_HANDLE_VALUE || !WriteFile(output.handle, content.data(), static_cast<DWORD>(content.size()), &written, nullptr) || written != content.size() || !FlushFileBuffers(output.handle)) { result.message = L"The update could not be saved. Check free disk space and folder permissions."; valid = false; }
                }
                if (!valid) break;
                if (!VerifyUpdateFile(path, file) || (file.name == Names[0] && !IsUpdateExecutable(path))) { result.message = L"The downloaded update did not pass integrity verification."; valid = false; break; }
            }
            if (valid && !Cancelled(cancel_)) { result.status = UpdateStatus::Ready; result.message = L"The update is ready to install."; result.stageDirectory = stage; }
        }
    } catch (...) { result.message = L"The update download could not be completed."; }
    if (result.status != UpdateStatus::Ready) CleanStage(stage);
    running_ = false;
    Publish(std::move(result), notifyWindow);
}
}
