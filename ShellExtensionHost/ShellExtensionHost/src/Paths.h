// Paths.h - Resolve machine-wide catalog + user settings locations.
#pragma once

#include <string>

namespace xisf::paths {

// %ProgramData%\DennisPayne\XISFShellExtension (created if allowed).
std::wstring AppDataRoot();

// %ProgramData%\DennisPayne\XISFShellExtension\catalogs (created if allowed).
std::wstring CatalogDir();

// Full path for a named catalog file under CatalogDir().
std::wstring CatalogFile(const wchar_t* fileName);

// Metadata sidecar for source link/hash display overrides.
std::wstring CatalogMetadataFile();

// HKCU key name for settings toggles (relative to HKEY_CURRENT_USER).
inline constexpr const wchar_t* kHkcuSettingsKey =
    L"Software\\DennisPayne\\XISF Shell Extension";

} // namespace xisf::paths
