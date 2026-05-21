// HostSettings.cpp
#include "HostSettings.h"
#include "Paths.h"
#include "RegistryHelpers.h"

#include <windows.h>

namespace xisf::hostsettings {

using xisf::regutil::ReadHKCUDword;
using xisf::regutil::WriteHKCUDword;

static bool ReadBool(const wchar_t* name, bool defaultValue)
{
    return ReadHKCUDword(paths::kHkcuSettingsKey, name, defaultValue ? 1u : 0u) != 0;
}

static void WriteBool(const wchar_t* name, bool value)
{
    WriteHKCUDword(paths::kHkcuSettingsKey, name, value ? 1u : 0u);
}

bool IsPropertyEnabled()           { return ReadBool(L"PropertyEnabled", true); }
bool IsPreviewEnabled()            { return ReadBool(L"PreviewEnabled",  true); }
bool IsFilterEnabled()             { return ReadBool(L"FilterEnabled",   true); }
void SetPropertyEnabled(bool e)    { WriteBool(L"PropertyEnabled", e); }
void SetPreviewEnabled(bool e)     { WriteBool(L"PreviewEnabled",  e); }
void SetFilterEnabled(bool e)      { WriteBool(L"FilterEnabled",   e); }

FeatureTier GetFeatureTier()
{
    DWORD value = ReadHKCUDword(paths::kHkcuSettingsKey, L"FeatureTier",
                                static_cast<DWORD>(FeatureTier::Full));
    if (value > static_cast<DWORD>(FeatureTier::Full))
        return FeatureTier::Full;
    return static_cast<FeatureTier>(value);
}

void SetFeatureTier(FeatureTier t)
{
    WriteHKCUDword(paths::kHkcuSettingsKey, L"FeatureTier", static_cast<DWORD>(t));
}

static constexpr const wchar_t* kHandlerKey = L"Software\\XISFPropertyHandler";

bool IsProjectionEnabled()
{
    return ReadHKCUDword(kHandlerKey, L"EnableSystemPhotoProjection", 1u) != 0;
}

void SetProjectionEnabled(bool enabled)
{
    WriteHKCUDword(kHandlerKey, L"EnableSystemPhotoProjection", enabled ? 1u : 0u);
}

} // namespace xisf::hostsettings
