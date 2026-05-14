// CatalogInstaller.h - Download / import / verify catalog files.
//
// Verified installation paths flow through Install*() functions which:
//   1. Stream bytes into a temp file under CatalogDir() with a unique suffix.
//   2. Hash-while-streaming with BCrypt SHA-256.
//   3. Enforce the per-source maxBytes cap during streaming.
//   4. Compare final hash to the compiled-in expected value.
//   5. Atomically replace the target file via MoveFileExW.
//
// A hash mismatch deletes the temp file and returns an error for pinned
// sources. File-import can optionally skip pin checks via the unverified API.
#pragma once

#include "CatalogSpec.h"
#include <string>

namespace xisf::installer {

enum class Result {
    Ok,
    AllocFailed,
    CatalogDirUnavailable,
    HttpOpenFailed,
    HttpConnectFailed,
    HttpRequestFailed,
    HttpBadStatus,
    UrlNotAllowed,
    SizeExceeded,
    WriteFailed,
    HashInitFailed,
    HashFailed,
    HashMismatch,
    InvalidContent,
    MoveFailed,
    SourceOpenFailed,
    OperationCancelled,
};

struct Report {
    Result      result = Result::Ok;
    unsigned    httpStatus = 0;       // filled when result == HttpBadStatus
    std::uint64_t bytesTransferred = 0;
    std::wstring computedHash;        // lowercase hex when hashing succeeded
    std::wstring errorDetail;         // human-readable extra info
};

// Progress callback. Return false to cancel (streaming aborts, temp file deleted).
// bytesSoFar may exceed the cap briefly by one buffer chunk; enforcement is
// done after each chunk.
using ProgressFn = bool(*)(std::uint64_t bytesSoFar, std::uint64_t maxBytes, void* user);

// Download pinned URL and install with strict hash verification.
Report InstallFromPinnedUrl(const catalogspec::CatalogSource& src,
                            ProgressFn progress, void* user);

// Copy a local file into the catalog directory with strict hash verification
// against the compiled-in pin. Used for air-gapped installs of the exact
// pinned version.
Report InstallFromLocalFileVerified(const catalogspec::CatalogSource& src,
                                    const wchar_t* sourcePath,
                                    ProgressFn progress, void* user);

// Copy a local file into the catalog directory without pin checks. The file is
// streamed to a temp file and atomically replaced. Returns computed SHA-256.
Report InstallFromLocalFileUnverified(const wchar_t* targetFileName,
                                      const wchar_t* sourcePath,
                                      std::uint64_t maxBytes,
                                      ProgressFn progress, void* user);

// Local summary helpers for the UI.
enum class PresenceState {
    Missing,          // file not found
    PresentUnknown,   // file exists, not yet hashed
    PresentVerified,  // file exists and matches expected pinned hash
    PresentMismatch,  // file exists but hash differs from pinned (user override or stale)
};

struct Presence {
    PresenceState  state = PresenceState::Missing;
    std::uint64_t  sizeBytes = 0;
    std::wstring   computedHash;
};

// Hashes the on-disk file (if any) and compares against the pin.
Presence Probe(const catalogspec::CatalogSource& src);

} // namespace xisf::installer
