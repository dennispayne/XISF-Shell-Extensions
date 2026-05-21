// RegistryHelpers.h - Thin wrappers for common HKCU registry read/write patterns.
#pragma once

#include <windows.h>
#include <string>

namespace xisf::regutil {

// Returns the REG_SZ value at HKCU\keyPath\valueName, or empty wstring on miss.
// valueName == nullptr reads the default value.
std::wstring ReadHKCUString(const wchar_t* keyPath, const wchar_t* valueName);

// Returns the REG_DWORD value at HKCU\keyPath\valueName, or defaultValue on miss.
DWORD ReadHKCUDword(const wchar_t* keyPath, const wchar_t* valueName, DWORD defaultValue);

// Writes a REG_DWORD value. Returns true on success.
bool WriteHKCUDword(const wchar_t* keyPath, const wchar_t* valueName, DWORD value);

// Writes a REG_SZ value. Returns true on success.
bool WriteHKCUString(const wchar_t* keyPath, const wchar_t* valueName, const wchar_t* data);

} // namespace xisf::regutil
