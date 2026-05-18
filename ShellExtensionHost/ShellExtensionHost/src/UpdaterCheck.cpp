// UpdaterCheck.cpp - GitHub releases API check for self-update pipeline.
#include "UpdaterCheck.h"
#include "UpdaterSpec.h"

#ifndef XISF_VERSION_TEXT
#define XISF_VERSION_TEXT 0.1.0.0
#endif
#define _XS2_(x) L ## #x
#define _XS_(x) _XS2_(x)
#define XISF_VERSION_WSTR _XS_(XISF_VERSION_TEXT)

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <cwctype>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")

namespace xisf::updater {

namespace {

// ── WinHTTP helpers ──────────────────────────────────────────────────────────

struct InetHandle {
    HINTERNET h = nullptr;
    explicit InetHandle(HINTERNET x = nullptr) : h(x) {}
    ~InetHandle() { if (h) WinHttpCloseHandle(h); }
    InetHandle(const InetHandle&) = delete;
    InetHandle& operator=(const InetHandle&) = delete;
    InetHandle(InetHandle&& o) noexcept : h(o.h) { o.h = nullptr; }
    explicit operator bool() const { return h != nullptr; }
};

bool CrackUrl(const std::wstring& url, std::wstring& host, std::wstring& path)
{
    URL_COMPONENTS uc{};
    uc.dwStructSize      = sizeof(uc);
    uc.dwSchemeLength    = static_cast<DWORD>(-1);
    uc.dwHostNameLength  = static_cast<DWORD>(-1);
    uc.dwUrlPathLength   = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &uc))
        return false;
    if (uc.nScheme != INTERNET_SCHEME_HTTPS) return false;
    host.assign(uc.lpszHostName, uc.dwHostNameLength);
    path.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength > 0)
        path.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    return !host.empty();
}

bool IsAllowedHost(const std::wstring& host)
{
    auto eq = [&](std::wstring_view s) {
        return _wcsicmp(host.c_str(), std::wstring(s).c_str()) == 0;
    };
    return eq(kGitHubApiHost) || eq(kGitHubDownloadHost) || eq(kGitHubCdnHost);
}

// Fetches an HTTPS URL into body. Returns HTTP status (0 on failure).
// Extra request headers can be provided as one header-per-line (CRLF-terminated).
int FetchHttps(const std::wstring& url,
               const std::wstring& extraHeaders,
               std::uint64_t maxBytes,
               std::string& body,
               std::wstring& etag,
               std::wstring& errorDetail)
{
    std::wstring host, path;
    if (!CrackUrl(url, host, path)) {
        errorDetail = L"Invalid or non-HTTPS URL";
        return 0;
    }
    if (!IsAllowedHost(host)) {
        errorDetail = L"Host not in the update allow-list";
        return 0;
    }

    InetHandle hSession(WinHttpOpen(L"XISF-Updater/1.0",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0));
    if (!hSession) { errorDetail = L"WinHttpOpen failed"; return 0; }

    DWORD tlsProtos = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2 | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
    WinHttpSetOption(hSession.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &tlsProtos, sizeof(tlsProtos));

    InetHandle hConn(WinHttpConnect(hSession.h, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!hConn) { errorDetail = L"WinHttpConnect failed"; return 0; }

    InetHandle hReq(WinHttpOpenRequest(hConn.h, L"GET", path.c_str(),
                                       nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE));
    if (!hReq) { errorDetail = L"WinHttpOpenRequest failed"; return 0; }

    std::wstring headers =
        L"Accept: application/vnd.github+json\r\n"
        L"X-GitHub-Api-Version: 2022-11-28\r\n";
    headers += extraHeaders;

    if (!WinHttpSendRequest(hReq.h, headers.c_str(), static_cast<DWORD>(headers.size()),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
        errorDetail = L"WinHttpSendRequest failed";
        return 0;
    }
    if (!WinHttpReceiveResponse(hReq.h, nullptr)) {
        errorDetail = L"WinHttpReceiveResponse failed";
        return 0;
    }

    DWORD statusCode = 0;
    DWORD statusSize = sizeof(statusCode);
    WinHttpQueryHeaders(hReq.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusSize, nullptr);

    // Extract ETag from response headers.
    wchar_t etagBuf[512] = {};
    DWORD etagSize = sizeof(etagBuf);
    if (WinHttpQueryHeaders(hReq.h, WINHTTP_QUERY_ETAG, WINHTTP_HEADER_NAME_BY_INDEX,
                            etagBuf, &etagSize, nullptr))
        etag = etagBuf;

    if (statusCode == 304) return static_cast<int>(statusCode); // Not Modified

    body.clear();
    constexpr DWORD kBuf = 32 * 1024;
    std::vector<char> buf(kBuf);
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hReq.h, &avail)) break;
        if (avail == 0) break;
        DWORD toRead = avail < kBuf ? avail : kBuf;
        DWORD got = 0;
        if (!WinHttpReadData(hReq.h, buf.data(), toRead, &got) || got == 0) break;
        body.append(buf.data(), got);
        if (static_cast<std::uint64_t>(body.size()) > maxBytes) {
            errorDetail = L"API response exceeded size cap";
            return 0;
        }
    }
    return static_cast<int>(statusCode);
}

// ── Minimal JSON string extraction ──────────────────────────────────────────

// Extracts the value of the first occurrence of "key":"<value>" in json.
bool ExtractJsonString(const std::string& json, const std::string& key, std::string& value)
{
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    while (pos != std::string::npos) {
        size_t colon = json.find(':', pos + needle.size());
        if (colon == std::string::npos) break;
        size_t q1 = json.find('"', colon + 1);
        if (q1 == std::string::npos) break;
        size_t q2 = q1 + 1;
        while (q2 < json.size()) {
            if (json[q2] == '\\') { q2 += 2; continue; }
            if (json[q2] == '"') break;
            ++q2;
        }
        if (q2 >= json.size()) break;
        value = json.substr(q1 + 1, q2 - q1 - 1);
        return true;
    }
    return false;
}

// Extracts "key": true|false (returns false if key not found).
bool ExtractJsonBool(const std::string& json, const std::string& key, bool& value)
{
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    size_t colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return false;
    size_t v = json.find_first_not_of(" \t\r\n", colon + 1);
    if (v == std::string::npos) return false;
    if (json.compare(v, 4, "true") == 0) { value = true; return true; }
    if (json.compare(v, 5, "false") == 0) { value = false; return true; }
    return false;
}

// Parses tag_name, prerelease, and asset URLs from the GitHub releases/latest
// JSON response. Returns false if required fields are missing.
bool ParseRelease(const std::string& json, ReleaseInfo& info, std::wstring& errorDetail)
{
    std::string tagName;
    if (!ExtractJsonString(json, "tag_name", tagName)) {
        errorDetail = L"GitHub API response missing tag_name";
        return false;
    }

    bool prerelease = false;
    ExtractJsonBool(json, "prerelease", prerelease);
    if (prerelease) {
        errorDetail = L"Latest release is a pre-release; skipping";
        return false;
    }

    // Strip leading 'v' from tag name.
    if (!tagName.empty() && tagName[0] == 'v')
        tagName = tagName.substr(1);
    info.version = std::wstring(tagName.begin(), tagName.end());

    // Find asset URLs. Parse the "assets" array by looking for the MSI name.
    const std::string msiPrefixNarrow(kAssetPrefix.begin(), kAssetPrefix.end());
    const std::string msiSuffixNarrow(kAssetSuffix.begin(), kAssetSuffix.end());
    const std::string cksumName(kChecksumAssetName.begin(), kChecksumAssetName.end());

    // Walk through "name"/"browser_download_url" pairs in the assets array.
    size_t pos = json.find("\"assets\"");
    if (pos == std::string::npos) {
        errorDetail = L"GitHub API response missing assets array";
        return false;
    }

    size_t cursor = pos;
    while (cursor < json.size()) {
        std::string name, dlUrl;
        size_t namePosStart = json.find("\"name\"", cursor);
        if (namePosStart == std::string::npos) break;

        // Make sure this "name" is inside the assets block (rough guard).
        if (namePosStart > pos + 200000) break;

        if (!ExtractJsonString(json.substr(namePosStart), "name", name)) {
            cursor = namePosStart + 6;
            continue;
        }
        size_t urlPos = json.find("\"browser_download_url\"", namePosStart);
        if (urlPos == std::string::npos) break;
        if (!ExtractJsonString(json.substr(urlPos), "browser_download_url", dlUrl)) {
            cursor = urlPos + 22;
            continue;
        }
        cursor = urlPos + 22;

        // Check if this asset is the MSI.
        if (name.find(msiPrefixNarrow) == 0 && name.size() > msiSuffixNarrow.size() &&
            name.compare(name.size() - msiSuffixNarrow.size(), msiSuffixNarrow.size(), msiSuffixNarrow) == 0)
        {
            info.msiUrl = std::wstring(dlUrl.begin(), dlUrl.end());
        }
        // Check if this is SHA256SUMS.txt
        if (name == cksumName) {
            info.checksumUrl = std::wstring(dlUrl.begin(), dlUrl.end());
        }
    }

    if (info.msiUrl.empty()) {
        errorDetail = L"MSI asset not found in release";
        return false;
    }
    if (info.checksumUrl.empty()) {
        errorDetail = L"SHA256SUMS.txt asset not found in release";
        return false;
    }
    return true;
}

// ── Semver comparison ────────────────────────────────────────────────────────

struct SemVer { int major = 0, minor = 0, patch = 0; };

bool ParseSemVer(const std::wstring& s, SemVer& v)
{
    // Accepts "1.0.0", "1.0.0.0", "v1.0.0"
    std::wstring t = s;
    if (!t.empty() && (t[0] == L'v' || t[0] == L'V')) t = t.substr(1);
    // Stop at first '-' (pre-release suffix)
    auto dash = t.find(L'-');
    if (dash != std::wstring::npos) t = t.substr(0, dash);
    int parsed = swscanf_s(t.c_str(), L"%d.%d.%d", &v.major, &v.minor, &v.patch);
    return parsed >= 1;
}

// Returns >0 if a > b, 0 if equal, <0 if a < b.
int CompareSemVer(const SemVer& a, const SemVer& b)
{
    if (a.major != b.major) return a.major - b.major;
    if (a.minor != b.minor) return a.minor - b.minor;
    return a.patch - b.patch;
}

// ── Registry helpers ─────────────────────────────────────────────────────────

const std::wstring kRegKey(kUpdateRegistryKey);

bool RegReadStr(const std::wstring& name, std::wstring& out)
{
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey.c_str(), 0, KEY_READ, &hk) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, sz = 0;
    RegQueryValueExW(hk, name.c_str(), nullptr, &type, nullptr, &sz);
    if (type != REG_SZ || sz < 2) { RegCloseKey(hk); return false; }
    out.resize(sz / sizeof(wchar_t));
    RegQueryValueExW(hk, name.c_str(), nullptr, &type,
                     reinterpret_cast<BYTE*>(&out[0]), &sz);
    while (!out.empty() && out.back() == L'\0') out.pop_back();
    RegCloseKey(hk);
    return true;
}

void RegWriteStr(const std::wstring& name, const std::wstring& value)
{
    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExW(hk, name.c_str(), 0, REG_SZ,
                   reinterpret_cast<const BYTE*>(value.c_str()),
                   static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    RegCloseKey(hk);
}

bool RegReadQword(const std::wstring& name, ULONGLONG& out)
{
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRegKey.c_str(), 0, KEY_READ, &hk) != ERROR_SUCCESS)
        return false;
    DWORD type = 0, sz = sizeof(ULONGLONG);
    BOOL ok = (RegQueryValueExW(hk, name.c_str(), nullptr, &type,
                                 reinterpret_cast<BYTE*>(&out), &sz) == ERROR_SUCCESS)
              && type == REG_QWORD;
    RegCloseKey(hk);
    return ok != FALSE;
}

void RegWriteQword(const std::wstring& name, ULONGLONG value)
{
    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, kRegKey.c_str(), 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr, &hk, nullptr) != ERROR_SUCCESS)
        return;
    RegSetValueExW(hk, name.c_str(), 0, REG_QWORD,
                   reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(hk);
}

ULONGLONG NowEpoch()
{
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    return u.QuadPart / 10000000ULL - 11644473600ULL;
}

} // namespace

// ── Public API ───────────────────────────────────────────────────────────────

CheckResult CheckForUpdate(ReleaseInfo& info, std::wstring& errorDetail, bool ignoreInterval)
{
    // Respect the minimum polling interval.
    if (!ignoreInterval) {
        ULONGLONG lastChecked = 0;
        if (RegReadQword(std::wstring(kRegLastChecked), lastChecked)) {
            ULONGLONG nowSec = NowEpoch();
            if (nowSec < lastChecked + static_cast<ULONGLONG>(kCheckIntervalMinutes) * 60) {
                // Serve from cache.
                std::wstring cachedVer, cachedUrl;
                RegReadStr(std::wstring(kRegAvailableVer), cachedVer);
                RegReadStr(std::wstring(kRegAssetUrl), cachedUrl);
                info.version  = cachedVer;
                info.msiUrl   = cachedUrl;
                return CheckResult::RateLimited;
            }
        }
    }

    // Build the releases/latest URL.
    const std::wstring apiUrl = L"https://" + std::wstring(kGitHubApiHost)
        + L"/repos/" + std::wstring(kOwner) + L"/" + std::wstring(kRepo)
        + L"/releases/latest";

    // Include ETag for conditional GET.
    std::wstring storedEtag;
    std::wstring etagHeader;
    if (RegReadStr(std::wstring(kRegETag), storedEtag) && !storedEtag.empty())
        etagHeader = L"If-None-Match: " + storedEtag + L"\r\n";

    std::string body;
    std::wstring responseEtag;
    int status = FetchHttps(apiUrl, etagHeader, kMaxApiBytes, body, responseEtag, errorDetail);

    if (status == 0) return CheckResult::NetworkError;

    // Update last-checked timestamp regardless of outcome.
    RegWriteQword(std::wstring(kRegLastChecked), NowEpoch());

    if (status == 304) {
        // Not Modified — serve from cache.
        std::wstring cachedVer, cachedUrl;
        RegReadStr(std::wstring(kRegAvailableVer), cachedVer);
        RegReadStr(std::wstring(kRegAssetUrl), cachedUrl);
        info.version = cachedVer;
        info.msiUrl  = cachedUrl;
        // Still need to decide NoUpdate vs UpdateAvailable from the cache.
        SemVer installed, available;
        ParseSemVer(XISF_VERSION_WSTR, installed);
        ParseSemVer(cachedVer, available);
        return (CompareSemVer(available, installed) > 0)
               ? CheckResult::UpdateAvailable : CheckResult::NoUpdate;
    }

    if (status != 200) {
        errorDetail = L"GitHub API returned HTTP " + std::to_wstring(status);
        return CheckResult::NetworkError;
    }

    if (!responseEtag.empty())
        RegWriteStr(std::wstring(kRegETag), responseEtag);

    if (!ParseRelease(body, info, errorDetail))
        return CheckResult::ParseError;

    // Compare versions.
    SemVer installed, available;
    if (!ParseSemVer(XISF_VERSION_WSTR, installed)) {
        errorDetail = L"Could not parse installed version: " XISF_VERSION_WSTR;
        return CheckResult::ParseError;
    }
    if (!ParseSemVer(info.version, available)) {
        errorDetail = L"Could not parse available version: " + info.version;
        return CheckResult::ParseError;
    }

    // Cache result.
    RegWriteStr(std::wstring(kRegAvailableVer), info.version);
    RegWriteStr(std::wstring(kRegAssetUrl), info.msiUrl);

    return (CompareSemVer(available, installed) > 0)
           ? CheckResult::UpdateAvailable : CheckResult::NoUpdate;
}

} // namespace xisf::updater
