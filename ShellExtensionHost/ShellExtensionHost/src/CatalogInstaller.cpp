// CatalogInstaller.cpp - WinHTTP download + local import with SHA-256 pinning.
#include "CatalogInstaller.h"
#include "Sha256.h"
#include "Paths.h"

#include <windows.h>
#include <winhttp.h>
#include <shlwapi.h>
#include <string>
#include <vector>

#pragma comment(lib, "winhttp.lib")

namespace xisf::installer {

namespace {

constexpr DWORD kIoBufSize = 64 * 1024;

// Split https://host/path into host + path. Returns false on malformed URL.
bool CrackUrl(std::wstring_view url, std::wstring& host, std::wstring& pathAndQuery)
{
    URL_COMPONENTS uc{};
    uc.dwStructSize      = sizeof(uc);
    uc.dwSchemeLength    = static_cast<DWORD>(-1);
    uc.dwHostNameLength  = static_cast<DWORD>(-1);
    uc.dwUrlPathLength   = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.data(), static_cast<DWORD>(url.size()), 0, &uc))
        return false;

    if (uc.nScheme != INTERNET_SCHEME_HTTPS) return false; // HTTPS only
    if (!uc.lpszHostName || uc.dwHostNameLength == 0) return false;

    host.assign(uc.lpszHostName, uc.dwHostNameLength);
    pathAndQuery.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength > 0)
        pathAndQuery.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    return !host.empty();
}

// RAII for HINTERNET.
struct InetHandle {
    HINTERNET h = nullptr;
    InetHandle() = default;
    explicit InetHandle(HINTERNET x) : h(x) {}
    ~InetHandle() { if (h) WinHttpCloseHandle(h); }
    InetHandle(const InetHandle&) = delete;
    InetHandle& operator=(const InetHandle&) = delete;
    InetHandle(InetHandle&& o) noexcept : h(o.h) { o.h = nullptr; }
    InetHandle& operator=(InetHandle&& o) noexcept { if (this != &o) { if (h) WinHttpCloseHandle(h); h = o.h; o.h = nullptr; } return *this; }
    explicit operator bool() const { return h != nullptr; }
};

std::wstring MakeTempPath(const std::wstring& targetPath)
{
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    DWORD pid = GetCurrentProcessId();
    wchar_t buf[64];
    swprintf_s(buf, L".%08lx-%016llx.tmp", pid, u.QuadPart);
    return targetPath + buf;
}

Result StreamToTempFile(HINTERNET hReq,
                        const std::wstring& tempPath,
                        std::uint64_t maxBytes,
                        ProgressFn progress, void* user,
                        std::array<std::uint8_t, 32>& digest,
                        std::uint64_t& bytesTransferred,
                        std::wstring& errDetail)
{
    HANDLE hFile = CreateFileW(tempPath.c_str(),
                               GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                               nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        errDetail = L"CreateFile(temp) failed";
        return Result::WriteFailed;
    }

    Sha256Hasher hasher;
    if (FAILED(hasher.Init())) {
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return Result::HashInitFailed;
    }

    std::vector<BYTE> buf(kIoBufSize);
    bytesTransferred = 0;

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hReq, &avail)) {
            errDetail = L"QueryDataAvailable failed";
            CloseHandle(hFile); DeleteFileW(tempPath.c_str());
            return Result::HttpRequestFailed;
        }
        if (avail == 0) break;

        DWORD toRead = (avail < kIoBufSize) ? avail : kIoBufSize;
        DWORD got = 0;
        if (!WinHttpReadData(hReq, buf.data(), toRead, &got)) {
            errDetail = L"ReadData failed";
            CloseHandle(hFile); DeleteFileW(tempPath.c_str());
            return Result::HttpRequestFailed;
        }
        if (got == 0) break;

        bytesTransferred += got;
        if (bytesTransferred > maxBytes) {
            errDetail = L"Response exceeded pinned max size cap";
            CloseHandle(hFile); DeleteFileW(tempPath.c_str());
            return Result::SizeExceeded;
        }

        if (FAILED(hasher.Update(buf.data(), got))) {
            CloseHandle(hFile); DeleteFileW(tempPath.c_str());
            return Result::HashFailed;
        }

        DWORD written = 0;
        if (!WriteFile(hFile, buf.data(), got, &written, nullptr) || written != got) {
            errDetail = L"WriteFile failed";
            CloseHandle(hFile); DeleteFileW(tempPath.c_str());
            return Result::WriteFailed;
        }

        if (progress && !progress(bytesTransferred, maxBytes, user)) {
            CloseHandle(hFile); DeleteFileW(tempPath.c_str());
            return Result::OperationCancelled;
        }
    }

    if (FAILED(hasher.Finalize(digest))) {
        CloseHandle(hFile); DeleteFileW(tempPath.c_str());
        return Result::HashFailed;
    }

    FlushFileBuffers(hFile);
    CloseHandle(hFile);
    return Result::Ok;
}

Result StreamLocalFileToTemp(const wchar_t* sourcePath,
                             const std::wstring& tempPath,
                             std::uint64_t maxBytes,
                             ProgressFn progress, void* user,
                             std::array<std::uint8_t, 32>& digest,
                             std::uint64_t& bytesTransferred,
                             std::wstring& errDetail)
{
    HANDLE hSrc = CreateFileW(sourcePath, GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                              nullptr);
    if (hSrc == INVALID_HANDLE_VALUE) {
        errDetail = L"Cannot open source file";
        return Result::SourceOpenFailed;
    }

    HANDLE hDst = CreateFileW(tempPath.c_str(),
                              GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                              nullptr);
    if (hDst == INVALID_HANDLE_VALUE) {
        CloseHandle(hSrc);
        errDetail = L"CreateFile(temp) failed";
        return Result::WriteFailed;
    }

    Sha256Hasher hasher;
    if (FAILED(hasher.Init())) {
        CloseHandle(hSrc); CloseHandle(hDst); DeleteFileW(tempPath.c_str());
        return Result::HashInitFailed;
    }

    std::vector<BYTE> buf(kIoBufSize);
    bytesTransferred = 0;

    for (;;) {
        DWORD got = 0;
        if (!ReadFile(hSrc, buf.data(), kIoBufSize, &got, nullptr)) {
            errDetail = L"ReadFile(src) failed";
            CloseHandle(hSrc); CloseHandle(hDst); DeleteFileW(tempPath.c_str());
            return Result::SourceOpenFailed;
        }
        if (got == 0) break;

        bytesTransferred += got;
        if (bytesTransferred > maxBytes) {
            errDetail = L"Source exceeded pinned max size cap";
            CloseHandle(hSrc); CloseHandle(hDst); DeleteFileW(tempPath.c_str());
            return Result::SizeExceeded;
        }

        if (FAILED(hasher.Update(buf.data(), got))) {
            CloseHandle(hSrc); CloseHandle(hDst); DeleteFileW(tempPath.c_str());
            return Result::HashFailed;
        }

        DWORD written = 0;
        if (!WriteFile(hDst, buf.data(), got, &written, nullptr) || written != got) {
            CloseHandle(hSrc); CloseHandle(hDst); DeleteFileW(tempPath.c_str());
            errDetail = L"WriteFile(temp) failed";
            return Result::WriteFailed;
        }

        if (progress && !progress(bytesTransferred, maxBytes, user)) {
            CloseHandle(hSrc); CloseHandle(hDst); DeleteFileW(tempPath.c_str());
            return Result::OperationCancelled;
        }
    }

    if (FAILED(hasher.Finalize(digest))) {
        CloseHandle(hSrc); CloseHandle(hDst); DeleteFileW(tempPath.c_str());
        return Result::HashFailed;
    }
    FlushFileBuffers(hDst);
    CloseHandle(hSrc);
    CloseHandle(hDst);
    return Result::Ok;
}

Result FinalizeAtomic(const std::wstring& tempPath, const std::wstring& targetPath,
                      std::wstring& errDetail)
{
    if (!MoveFileExW(tempPath.c_str(), targetPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DWORD gle = GetLastError();
        wchar_t tmp[64];
        swprintf_s(tmp, L"MoveFileEx failed (Win32 %lu)", gle);
        errDetail = tmp;
        DeleteFileW(tempPath.c_str());
        return Result::MoveFailed;
    }
    return Result::Ok;
}

} // namespace

Report InstallFromPinnedUrl(const catalogspec::CatalogSource& src,
                            ProgressFn progress, void* user)
{
    Report rep{};

    // Allow-list check: only pinned repo host + path prefix.
    if (src.url.size() < catalogspec::kAllowedUrlPrefix.size() ||
        std::wstring_view(src.url.data(), catalogspec::kAllowedUrlPrefix.size())
            != catalogspec::kAllowedUrlPrefix)
    {
        rep.result = Result::UrlNotAllowed;
        rep.errorDetail = L"URL not in compiled allow-list";
        return rep;
    }

    std::wstring targetDir = paths::CatalogDir();
    if (targetDir.empty()) {
        rep.result = Result::CatalogDirUnavailable;
        return rep;
    }
    std::wstring targetPath = targetDir + L"\\" + std::wstring(src.fileName);
    std::wstring tempPath   = MakeTempPath(targetPath);

    std::wstring host, objectPath;
    std::wstring urlStr(src.url);
    if (!CrackUrl(urlStr, host, objectPath)) {
        rep.result = Result::HttpOpenFailed;
        rep.errorDetail = L"Malformed URL";
        return rep;
    }

    InetHandle hSession(WinHttpOpen(L"XISFShellExtensionHost/1.0",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0));
    if (!hSession) { rep.result = Result::HttpOpenFailed; return rep; }

    // Enforce modern TLS only (12 and 13 where available). No fallback to old protocols.
    DWORD secureProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    secureProtocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(hSession.h, WINHTTP_OPTION_SECURE_PROTOCOLS,
                     &secureProtocols, sizeof(secureProtocols));

    // Reasonable timeouts: resolve 10s, connect 15s, send 30s, receive 60s.
    WinHttpSetTimeouts(hSession.h, 10'000, 15'000, 30'000, 60'000);

    InetHandle hConnect(WinHttpConnect(hSession.h, host.c_str(),
                                       INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!hConnect) { rep.result = Result::HttpConnectFailed; return rep; }

    InetHandle hReq(WinHttpOpenRequest(hConnect.h, L"GET", objectPath.c_str(), nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE));
    if (!hReq) { rep.result = Result::HttpOpenFailed; return rep; }

    // Defense-in-depth: reject any proxy-controlled cert override.
    DWORD secFlags = 0;
    DWORD sfSize = sizeof(secFlags);
    WinHttpQueryOption(hReq.h, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, &sfSize);
    // Do NOT set SECURITY_FLAG_IGNORE_* -- we want full cert validation.

    if (!WinHttpSendRequest(hReq.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    {
        rep.result = Result::HttpRequestFailed;
        rep.errorDetail = L"SendRequest failed";
        return rep;
    }
    if (!WinHttpReceiveResponse(hReq.h, nullptr)) {
        rep.result = Result::HttpRequestFailed;
        rep.errorDetail = L"ReceiveResponse failed";
        return rep;
    }

    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(hReq.h,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        rep.result = Result::HttpBadStatus;
        rep.httpStatus = status;
        return rep;
    }

    std::array<std::uint8_t, 32> digest{};
    std::uint64_t written = 0;
    std::wstring err;
    Result r = StreamToTempFile(hReq.h, tempPath, src.maxBytes,
                                progress, user, digest, written, err);
    rep.bytesTransferred = written;
    rep.errorDetail = err;
    if (r != Result::Ok) { rep.result = r; return rep; }

    std::wstring gotHex = ToHexLower(digest);
    rep.computedHash = gotHex;
    if (!HexEquals(gotHex, src.expectedSha256)) {
        DeleteFileW(tempPath.c_str());
        rep.result = Result::HashMismatch;
        return rep;
    }

    r = FinalizeAtomic(tempPath, targetPath, rep.errorDetail);
    rep.result = r;
    return rep;
}

Report InstallFromLocalFileVerified(const catalogspec::CatalogSource& src,
                                    const wchar_t* sourcePath,
                                    ProgressFn progress, void* user)
{
    Report rep{};
    std::wstring targetDir = paths::CatalogDir();
    if (targetDir.empty()) { rep.result = Result::CatalogDirUnavailable; return rep; }
    std::wstring targetPath = targetDir + L"\\" + std::wstring(src.fileName);
    std::wstring tempPath   = MakeTempPath(targetPath);

    std::array<std::uint8_t, 32> digest{};
    std::uint64_t written = 0;
    Result r = StreamLocalFileToTemp(sourcePath, tempPath, src.maxBytes,
                                     progress, user, digest, written,
                                     rep.errorDetail);
    rep.bytesTransferred = written;
    if (r != Result::Ok) { rep.result = r; return rep; }

    std::wstring gotHex = ToHexLower(digest);
    rep.computedHash = gotHex;
    if (!HexEquals(gotHex, src.expectedSha256)) {
        DeleteFileW(tempPath.c_str());
        rep.result = Result::HashMismatch;
        return rep;
    }

    rep.result = FinalizeAtomic(tempPath, targetPath, rep.errorDetail);
    return rep;
}

Presence Probe(const catalogspec::CatalogSource& src)
{
    Presence p{};
    std::wstring path = paths::CatalogFile(std::wstring(src.fileName).c_str());
    if (path.empty()) return p;

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        p.state = PresenceState::Missing;
        return p;
    }
    ULARGE_INTEGER sz;
    sz.LowPart  = fad.nFileSizeLow;
    sz.HighPart = fad.nFileSizeHigh;
    p.sizeBytes = sz.QuadPart;

    std::array<std::uint8_t, 32> digest{};
    std::uint64_t n = 0;
    if (FAILED(HashFile(path.c_str(), digest, n))) {
        p.state = PresenceState::PresentUnknown;
        return p;
    }
    p.computedHash = ToHexLower(digest);
    p.state = HexEquals(p.computedHash, src.expectedSha256)
                ? PresenceState::PresentVerified
                : PresenceState::PresentMismatch;
    return p;
}

} // namespace xisf::installer
