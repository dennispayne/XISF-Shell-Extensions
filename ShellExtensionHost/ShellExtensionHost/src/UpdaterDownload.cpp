// UpdaterDownload.cpp - MSI download, SHA-256 verification, Authenticode check.
#include "UpdaterDownload.h"
#include "UpdaterSpec.h"
#include "Sha256.h"
#include "WinHttpHelpers.h"

#include <windows.h>
#include <winhttp.h>
#include <wintrust.h>
#include <softpub.h>
#include <wincrypt.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <array>
#include <sstream>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")

namespace xisf::updater {

namespace {

constexpr DWORD kIoBuf = 64 * 1024;

using xisf::winhttp::InetHandle;

bool CrackUrlDl(const std::wstring& url, std::wstring& host, std::wstring& path)
{
    return xisf::winhttp::CrackUrl(url, host, path);
}

bool IsAllowedDownloadHost(const std::wstring& host)
{
    auto eq = [&](std::wstring_view s) {
        return _wcsicmp(host.c_str(), std::wstring(s).c_str()) == 0;
    };
    return eq(kGitHubApiHost) || eq(kGitHubDownloadHost) || eq(kGitHubCdnHost);
}

// Opens a WinHTTP session and sends a GET for url, following one redirect.
// Returns the request handle on success; caller must close it.
HINTERNET OpenHttpsGet(const std::wstring& url, InetHandle& hSession,
                       InetHandle& hConn, std::wstring& errorDetail)
{
    std::wstring host, path;
    if (!CrackUrlDl(url, host, path)) {
        errorDetail = L"Invalid or non-HTTPS URL";
        return nullptr;
    }
    if (!IsAllowedDownloadHost(host)) {
        errorDetail = L"Download host not in allow-list: " + host;
        return nullptr;
    }

    if (!hSession.h) {
        hSession.h = WinHttpOpen(L"XISF-Updater/1.0",
                                  WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!hSession) { errorDetail = L"WinHttpOpen failed"; return nullptr; }
        DWORD tlsProtos = xisf::winhttp::kTlsProtocols;
        WinHttpSetOption(hSession.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &tlsProtos, sizeof(tlsProtos));
    }

    hConn.h = WinHttpConnect(hSession.h, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConn) { errorDetail = L"WinHttpConnect failed"; return nullptr; }

    HINTERNET hReq = WinHttpOpenRequest(hConn.h, L"GET", path.c_str(),
                                        nullptr, WINHTTP_NO_REFERER,
                                        WINHTTP_DEFAULT_ACCEPT_TYPES,
                                        WINHTTP_FLAG_SECURE);
    if (!hReq) { errorDetail = L"WinHttpOpenRequest failed"; return nullptr; }

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, nullptr))
    {
        WinHttpCloseHandle(hReq);
        errorDetail = L"WinHttpSendRequest/ReceiveResponse failed";
        return nullptr;
    }

    // Follow one redirect for GitHub asset downloads.
    DWORD statusCode = 0;
    DWORD sz = sizeof(statusCode);
    WinHttpQueryHeaders(hReq, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &sz, nullptr);

    if (statusCode == 301 || statusCode == 302 || statusCode == 307 || statusCode == 308) {
        wchar_t loc[2048] = {};
        DWORD locSz = sizeof(loc);
        if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                                loc, &locSz, nullptr))
        {
            WinHttpCloseHandle(hReq);
            hConn.h = nullptr;
            std::wstring redirectUrl(loc);
            // Re-open on the redirect target (CDN).
            return OpenHttpsGet(redirectUrl, hSession, hConn, errorDetail);
        }
    }

    if (statusCode != 200) {
        WinHttpCloseHandle(hReq);
        errorDetail = L"HTTP " + std::to_wstring(statusCode);
        return nullptr;
    }
    return hReq;
}

// Streams hReq body into a file at tempPath, hashing with SHA-256.
// Returns true on success.
bool StreamToFile(HINTERNET hReq,
                  const std::wstring& tempPath,
                  std::uint64_t maxBytes,
                  const UpdateProgressFn& progress,
                  std::array<std::uint8_t, 32>& digest,
                  std::wstring& errorDetail)
{
    HANDLE hFile = CreateFileW(tempPath.c_str(), GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        errorDetail = L"CreateFile for download temp failed";
        return false;
    }

    Sha256Hasher hasher;
    if (FAILED(hasher.Init())) {
        CloseHandle(hFile); DeleteFileW(tempPath.c_str());
        errorDetail = L"SHA-256 init failed";
        return false;
    }

    // Try to read Content-Length for progress reporting.
    wchar_t clBuf[64] = {};
    DWORD clSz = sizeof(clBuf);
    std::uint64_t totalBytes = 0;
    if (WinHttpQueryHeaders(hReq, WINHTTP_QUERY_CONTENT_LENGTH, WINHTTP_HEADER_NAME_BY_INDEX,
                            clBuf, &clSz, nullptr))
        totalBytes = _wcstoui64(clBuf, nullptr, 10);

    std::vector<BYTE> buf(kIoBuf);
    std::uint64_t received = 0;

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hReq, &avail)) break;
        if (avail == 0) break;

        DWORD toRead = avail < kIoBuf ? avail : kIoBuf;
        DWORD got = 0;
        if (!WinHttpReadData(hReq, buf.data(), toRead, &got) || got == 0) break;

        received += got;
        if (received > maxBytes) {
            CloseHandle(hFile); DeleteFileW(tempPath.c_str());
            errorDetail = L"Download exceeded size cap";
            return false;
        }

        if (FAILED(hasher.Update(buf.data(), got))) {
            CloseHandle(hFile); DeleteFileW(tempPath.c_str());
            errorDetail = L"SHA-256 update failed";
            return false;
        }

        DWORD written = 0;
        if (!WriteFile(hFile, buf.data(), got, &written, nullptr) || written != got) {
            CloseHandle(hFile); DeleteFileW(tempPath.c_str());
            errorDetail = L"WriteFile failed";
            return false;
        }

        if (progress && !progress(received, totalBytes)) {
            CloseHandle(hFile); DeleteFileW(tempPath.c_str());
            errorDetail = L"Cancelled";
            return false;
        }
    }

    if (FAILED(hasher.Finalize(digest))) {
        CloseHandle(hFile); DeleteFileW(tempPath.c_str());
        errorDetail = L"SHA-256 finalize failed";
        return false;
    }
    FlushFileBuffers(hFile);
    CloseHandle(hFile);
    return true;
}

// Downloads a small text payload (e.g., SHA256SUMS.txt) into a string.
bool FetchText(const std::wstring& url, std::uint64_t maxBytes,
               std::string& out, std::wstring& errorDetail)
{
    InetHandle hSession, hConn;
    InetHandle hReq(OpenHttpsGet(url, hSession, hConn, errorDetail));
    if (!hReq) return false;

    out.clear();
    std::vector<char> buf(32 * 1024);
    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hReq.h, &avail)) break;
        if (avail == 0) break;
        DWORD toRead = avail < static_cast<DWORD>(buf.size()) ? avail : static_cast<DWORD>(buf.size());
        DWORD got = 0;
        if (!WinHttpReadData(hReq.h, buf.data(), toRead, &got) || got == 0) break;
        out.append(buf.data(), got);
        if (static_cast<std::uint64_t>(out.size()) > maxBytes) {
            errorDetail = L"Checksum file exceeded size cap";
            return false;
        }
    }
    return !out.empty();
}

// Parses SHA256SUMS.txt (lines: "<64-hex>  <filename>") to find the hash for
// the given filename. Returns the 64-char lowercase hex, or empty string.
std::wstring ParseChecksumFile(const std::string& content, const std::wstring& fileName)
{
    std::string narrowName;
    narrowName.reserve(fileName.size());
    for (wchar_t c : fileName) narrowName += static_cast<char>(c);
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 64 + 2) continue;
        // Format: "<64 hex chars>  <filename>" or "<64 hex chars> *<filename>"
        size_t sep = line.find_first_not_of("0123456789abcdefABCDEF");
        if (sep < 64) continue;
        std::string hash = line.substr(0, 64);
        size_t nameStart = line.find_first_not_of(" *", 64);
        if (nameStart == std::string::npos) continue;
        std::string name = line.substr(nameStart);
        // Strip path component if present.
        size_t slash = name.rfind('/');
        if (slash != std::string::npos) name = name.substr(slash + 1);
        slash = name.rfind('\\');
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (_stricmp(name.c_str(), narrowName.c_str()) == 0) {
            // Normalise to lower-case.
            std::transform(hash.begin(), hash.end(), hash.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return std::wstring(hash.begin(), hash.end());
        }
    }
    return {};
}

// Returns the filename component of a URL path.
std::wstring FileNameFromUrl(const std::wstring& url)
{
    size_t slash = url.rfind(L'/');
    std::wstring name = (slash != std::wstring::npos) ? url.substr(slash + 1) : url;
    // Strip query string.
    size_t q = name.find(L'?');
    if (q != std::wstring::npos) name = name.substr(0, q);
    return name;
}

// Authenticode verification via WinVerifyTrust + CryptQueryObject.
// If expectedThumbprint is empty, skips signer check (returns true).
// If non-empty, verifies the leaf certificate SHA-1 thumbprint matches.
bool VerifyAuthenticode(const std::wstring& path,
                        std::wstring_view expectedThumbprint,
                        std::wstring& errorDetail)
{
    // Step 1: Check file has a valid Authenticode signature.
    WINTRUST_FILE_INFO fi{};
    fi.cbStruct      = sizeof(fi);
    fi.pcwszFilePath = path.c_str();

    WINTRUST_DATA wd{};
    wd.cbStruct            = sizeof(wd);
    wd.dwUIChoice          = WTD_UI_NONE;
    wd.fdwRevocationChecks = WTD_REVOKE_NONE; // No network revocation check
    wd.dwUnionChoice       = WTD_CHOICE_FILE;
    wd.pFile               = &fi;
    wd.dwStateAction       = WTD_STATEACTION_VERIFY;
    wd.dwProvFlags         = WTD_SAFER_FLAG | WTD_CACHE_ONLY_URL_RETRIEVAL;

    GUID policy = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    LONG trustResult = WinVerifyTrust(nullptr, &policy, &wd);

    // Always close the trust handle.
    wd.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &policy, &wd);

    if (trustResult != ERROR_SUCCESS) {
        if (expectedThumbprint.empty()) {
            // Unsigned-OK mode: no thumbprint pinning, so an unsigned file passes.
            // Still note it in errorDetail for diagnostics.
            errorDetail = L"No Authenticode signature (accepted: unsigned-OK mode)";
            return true;
        }
        errorDetail = L"WinVerifyTrust failed: 0x" + std::to_wstring(static_cast<unsigned long>(trustResult));
        return false;
    }

    if (expectedThumbprint.empty()) return true; // Signed but not pinned — OK

    // Step 2: Extract the leaf certificate thumbprint and compare.
    HCERTSTORE hStore = nullptr;
    HCRYPTMSG hMsg = nullptr;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, path.c_str(),
                          CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
                          CERT_QUERY_FORMAT_FLAG_BINARY,
                          0, nullptr, nullptr, nullptr, &hStore, &hMsg, nullptr))
    {
        errorDetail = L"CryptQueryObject failed";
        return false;
    }

    bool matched = false;
    PCCERT_CONTEXT pCert = nullptr;
    while ((pCert = CertEnumCertificatesInStore(hStore, pCert)) != nullptr) {
        std::array<BYTE, 20> thumb{};
        DWORD thumbSz = static_cast<DWORD>(thumb.size());
        if (CertGetCertificateContextProperty(pCert, CERT_SHA1_HASH_PROP_ID,
                                              thumb.data(), &thumbSz))
        {
            // Convert to lower-case hex.
            std::wstring hex;
            hex.reserve(40);
            for (BYTE b : thumb)
            {
                static const wchar_t kHex[] = L"0123456789abcdef";
                hex += kHex[(b >> 4) & 0xF];
                hex += kHex[b & 0xF];
            }
            if (_wcsicmp(hex.c_str(), std::wstring(expectedThumbprint).c_str()) == 0)
            {
                matched = true;
                CertFreeCertificateContext(pCert);
                break;
            }
        }
    }
    CryptMsgClose(hMsg);
    CertCloseStore(hStore, 0);

    if (!matched)
        errorDetail = L"MSI signer thumbprint does not match pinned value";
    return matched;
}

std::wstring MakeTempFilePath(const std::wstring& dir, const std::wstring& name)
{
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    wchar_t stamp[32];
    swprintf_s(stamp, L".%016llx", u.QuadPart);
    return dir + L"\\" + name + stamp;
}

} // namespace

// ── Public API ────────────────────────────────────────────────────────────────

DownloadReport DownloadUpdate(const std::wstring& msiUrl,
                              const std::wstring& checksumUrl,
                              const std::wstring& tempDir,
                              const UpdateProgressFn& progress)
{
    DownloadReport report;

    // Ensure temp directory exists.
    CreateDirectoryW(tempDir.c_str(), nullptr);

    // 1. Download SHA256SUMS.txt to memory.
    std::string checksumContent;
    std::wstring errDetail;
    if (!FetchText(checksumUrl, kMaxChecksumBytes, checksumContent, errDetail)) {
        report.result      = DownloadResult::NetworkError;
        report.errorDetail = L"Could not download SHA256SUMS.txt: " + errDetail;
        return report;
    }

    // 2. Determine MSI filename and look up its expected hash.
    std::wstring msiFileName = FileNameFromUrl(msiUrl);
    std::wstring expectedHash = ParseChecksumFile(checksumContent, msiFileName);
    if (expectedHash.empty()) {
        report.result      = DownloadResult::ChecksumMissing;
        report.errorDetail = L"SHA256SUMS.txt does not contain an entry for " + msiFileName;
        return report;
    }

    // 3. Download the MSI to a temp file.
    std::wstring tempPath = MakeTempFilePath(tempDir, msiFileName);
    InetHandle hSession, hConn;
    InetHandle hReq(OpenHttpsGet(msiUrl, hSession, hConn, errDetail));
    if (!hReq) {
        report.result      = DownloadResult::NetworkError;
        report.errorDetail = L"Could not connect to download MSI: " + errDetail;
        return report;
    }

    std::array<std::uint8_t, 32> digest{};
    if (!StreamToFile(hReq.h, tempPath, kMaxMsiBytes, progress, digest, errDetail)) {
        if (errDetail == L"Cancelled") {
            report.result      = DownloadResult::NetworkError;
            report.errorDetail = L"Cancelled";
        } else if (errDetail.find(L"size cap") != std::wstring::npos) {
            report.result      = DownloadResult::SizeExceeded;
            report.errorDetail = errDetail;
        } else {
            report.result      = DownloadResult::WriteFailed;
            report.errorDetail = errDetail;
        }
        return report;
    }

    // 4. Verify SHA-256.
    std::wstring actualHash = ToHexLower(digest);
    if (!HexEquals(actualHash, expectedHash)) {
        DeleteFileW(tempPath.c_str());
        report.result      = DownloadResult::HashMismatch;
        report.errorDetail = L"SHA-256 mismatch. Expected: " + expectedHash
                           + L"  Got: " + actualHash;
        return report;
    }

    // 5. Authenticode check (skipped if thumbprint is not configured).
    errDetail.clear();
    if (!VerifyAuthenticode(tempPath, kExpectedSignerThumbprint, errDetail)) {
        DeleteFileW(tempPath.c_str());
        report.result      = DownloadResult::SignerRejected;
        report.errorDetail = errDetail;
        return report;
    }

    report.result         = DownloadResult::Ok;
    report.downloadedPath = tempPath;
    return report;
}

bool LaunchMsiInstaller(const std::wstring& msiPath)
{
    std::wstring params = L"/i \"" + msiPath + L"\"";
    SHELLEXECUTEINFOW sei{};
    sei.cbSize       = sizeof(sei);
    sei.fMask        = SEE_MASK_NOASYNC;
    sei.lpVerb       = L"runas";
    sei.lpFile       = L"msiexec.exe";
    sei.lpParameters = params.c_str();
    sei.nShow        = SW_SHOWNORMAL;
    return ShellExecuteExW(&sei) != FALSE;
}

} // namespace xisf::updater
