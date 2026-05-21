// RegistryHelpers.cpp - HKCU registry helpers.
#include "RegistryHelpers.h"

#pragma comment(lib, "advapi32.lib")

namespace xisf::regutil {

std::wstring ReadHKCUString(const wchar_t* keyPath, const wchar_t* valueName)
{
    HKEY hk = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, keyPath, 0, KEY_READ, &hk) != ERROR_SUCCESS)
        return {};

    std::wstring out;
    DWORD type = 0, sz = 0;
    RegQueryValueExW(hk, valueName, nullptr, &type, nullptr, &sz);
    if (type == REG_SZ && sz >= 2) {
        out.resize(sz / sizeof(wchar_t));
        if (RegQueryValueExW(hk, valueName, nullptr, &type,
                             reinterpret_cast<BYTE*>(&out[0]), &sz) != ERROR_SUCCESS) {
            out.clear();
        } else {
            while (!out.empty() && out.back() == L'\0') out.pop_back();
        }
    }
    RegCloseKey(hk);
    return out;
}

DWORD ReadHKCUDword(const wchar_t* keyPath, const wchar_t* valueName, DWORD defaultValue)
{
    DWORD value = 0;
    DWORD cb = sizeof(value);
    LONG st = RegGetValueW(HKEY_CURRENT_USER, keyPath, valueName,
                           RRF_RT_REG_DWORD, nullptr, &value, &cb);
    return (st == ERROR_SUCCESS) ? value : defaultValue;
}

bool WriteHKCUDword(const wchar_t* keyPath, const wchar_t* valueName, DWORD value)
{
    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hk, nullptr)
        != ERROR_SUCCESS) {
        return false;
    }
    LONG st = RegSetValueExW(hk, valueName, 0, REG_DWORD,
                             reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(hk);
    return st == ERROR_SUCCESS;
}

bool WriteHKCUString(const wchar_t* keyPath, const wchar_t* valueName, const wchar_t* data)
{
    HKEY hk = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, keyPath, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_SET_VALUE, nullptr, &hk, nullptr)
        != ERROR_SUCCESS) {
        return false;
    }
    DWORD cb = static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t));
    LONG st = RegSetValueExW(hk, valueName, 0, REG_SZ,
                             reinterpret_cast<const BYTE*>(data), cb);
    RegCloseKey(hk);
    return st == ERROR_SUCCESS;
}

} // namespace xisf::regutil
