// UpdaterCheck.h - GitHub release check for the self-update pipeline.
#pragma once

#include <string>

namespace xisf::updater {

enum class CheckResult
{
    NoUpdate,        // Installed version is current
    UpdateAvailable, // A newer release exists
    NetworkError,    // WinHTTP call failed
    ParseError,      // Unexpected API response format
    RateLimited,     // Interval since last check has not expired
};

// Information about the latest available release.
struct ReleaseInfo
{
    std::wstring version;       // e.g. "1.1.0"
    std::wstring msiUrl;        // browser_download_url of the MSI asset
    std::wstring checksumUrl;   // browser_download_url of SHA256SUMS.txt
    std::wstring errorDetail;   // Set on error for passing back through PostMessage
};

// Queries the GitHub releases API and compares against the currently-installed
// version (XISF_VERSION_WSTR at compile time). Respects a minimum call interval
// backed by HKCU and uses ETag caching to minimise API quota usage.
//
// On return:
//   UpdateAvailable  -> info is populated with the release data
//   NoUpdate         -> info.version is the latest (same as installed)
//   RateLimited      -> call was skipped; info.version from cache (may be empty)
//   NetworkError / ParseError -> errorDetail contains a human-readable message
CheckResult CheckForUpdate(ReleaseInfo& info, std::wstring& errorDetail, bool ignoreInterval = false);

} // namespace xisf::updater
