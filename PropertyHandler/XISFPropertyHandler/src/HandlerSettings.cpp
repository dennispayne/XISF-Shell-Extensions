// HandlerSettings.cpp - see header.
#include "HandlerSettings.h"
#include <windows.h>

namespace xisf {

bool IsPropertyHandlerEnabled()
{
    DWORD value = 1;
    DWORD cb = sizeof(value);
    LONG st = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\DennisPayne\\XISF Shell Extension",
        L"PropertyEnabled",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &cb);
    if (st != ERROR_SUCCESS) {
        // Default: enabled.
        return true;
    }
    return value != 0;
}

FeatureTier GetFeatureTier()
{
    DWORD value = static_cast<DWORD>(FeatureTier::Full);
    DWORD cb = sizeof(value);
    LONG st = RegGetValueW(
        HKEY_CURRENT_USER,
        L"Software\\DennisPayne\\XISF Shell Extension",
        L"FeatureTier",
        RRF_RT_REG_DWORD,
        nullptr,
        &value,
        &cb);
    if (st != ERROR_SUCCESS || value > static_cast<DWORD>(FeatureTier::Full))
        return FeatureTier::Full;
    return static_cast<FeatureTier>(value);
}

bool IsProjectionEnabled()
{
    DWORD val = 1;
    DWORD cb = sizeof(val);
    LONG st = RegGetValueW(HKEY_CURRENT_USER, L"Software\\XISFPropertyHandler",
        L"EnableSystemPhotoProjection", RRF_RT_REG_DWORD, nullptr, &val, &cb);
    if (st != ERROR_SUCCESS) return true;
    return val != 0;
}

} // namespace xisf
