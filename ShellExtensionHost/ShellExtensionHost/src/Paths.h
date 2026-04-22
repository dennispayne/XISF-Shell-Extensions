// Paths.h - Resolve user-scoped catalog + settings locations.
#pragma once

#include <string>

namespace xisf::paths {

// %LOCALAPPDATA%\XISFShellExtension  (created if missing).
std::wstring AppDataRoot();

// %LOCALAPPDATA%\XISFShellExtension\catalogs  (created if missing).
std::wstring CatalogDir();

// Full path for a named catalog file under CatalogDir().
std::wstring CatalogFile(const wchar_t* fileName);

// HKCU key name for settings toggles (relative to HKEY_CURRENT_USER).
inline constexpr const wchar_t* kHkcuSettingsKey =
    L"Software\\DennisPayne\\XISF Shell Extension";

} // namespace xisf::paths
