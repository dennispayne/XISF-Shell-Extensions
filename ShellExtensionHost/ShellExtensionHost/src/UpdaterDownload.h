// UpdaterDownload.h - MSI download and verification for the self-update pipeline.
#pragma once

#include <string>
#include <functional>
#include <cstdint>

namespace xisf::updater {

enum class DownloadResult
{
    Ok,
    InvalidUrl,
    NetworkError,
    SizeExceeded,
    HashMismatch,
    SignerRejected,
    AntiRollbackBlocked,
    WriteFailed,
    ChecksumMissing,
};

struct DownloadReport
{
    DownloadResult result = DownloadResult::NetworkError;
    std::wstring   downloadedPath; // Set on Ok; the staged .msi temp file
    std::wstring   errorDetail;
};

// Called during download: bytesReceived, totalBytes (0 if unknown).
// Return false to cancel.
using UpdateProgressFn = std::function<bool(std::uint64_t bytesReceived,
                                            std::uint64_t totalBytes)>;

// Downloads the MSI at msiUrl, verifies its SHA-256 against the companion
// SHA256SUMS.txt at checksumUrl, and optionally verifies the Authenticode
// signer against kExpectedSignerThumbprint.
//
// The MSI is staged to tempDir (created if necessary). On success, the caller
// is responsible for launching the installer and deleting the file.
DownloadReport DownloadUpdate(const std::wstring& msiUrl,
                              const std::wstring& checksumUrl,
                              const std::wstring& tempDir,
                              const UpdateProgressFn& progress = {});

// Launches the staged MSI (ShellExecute msiexec /i).
// Returns true if the launch succeeded (does not wait for install to complete).
bool LaunchMsiInstaller(const std::wstring& msiPath);

} // namespace xisf::updater
