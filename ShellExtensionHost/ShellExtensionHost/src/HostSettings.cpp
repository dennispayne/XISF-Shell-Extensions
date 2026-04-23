// HostSettings.cpp
#include "HostSettings.h"
#include "Paths.h"

#include <windows.h>

namespace xisf::hostsettings {

static bool ReadDword(const wchar_t* name, bool defaultValue)
{
    DWORD value = 0;
    DWORD cb = sizeof(value);
    LONG st = RegGetValueW(HKEY_CURRENT_USER, paths::kHkcuSettingsKey, name,
                           RRF_RT_REG_DWORD, nullptr, &value, &cb);
    if (st != ERROR_SUCCESS) return defaultValue;
    return value != 0;
}

static void WriteDword(const wchar_t* name, bool value)
{
    HKEY hKey = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, paths::kHkcuSettingsKey, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hKey, nullptr)
        == ERROR_SUCCESS)
    {
        DWORD dw = value ? 1u : 0u;
        RegSetValueExW(hKey, name, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&dw), sizeof(dw));
        RegCloseKey(hKey);
    }
}

bool IsPropertyEnabled()           { return ReadDword(L"PropertyEnabled", true); }
bool IsPreviewEnabled()            { return ReadDword(L"PreviewEnabled",  true); }
bool IsFilterEnabled()             { return ReadDword(L"FilterEnabled",   true); }
void SetPropertyEnabled(bool e)    { WriteDword(L"PropertyEnabled", e); }
void SetPreviewEnabled(bool e)     { WriteDword(L"PreviewEnabled",  e); }
void SetFilterEnabled(bool e)      { WriteDword(L"FilterEnabled",   e); }

} // namespace xisf::hostsettings
