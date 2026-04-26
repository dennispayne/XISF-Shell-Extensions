// HostSettings.h - Read/write HKCU handler-enable flags.
#pragma once

#include <cstdint>

namespace xisf::hostsettings {

bool IsPropertyEnabled();  // default true
bool IsPreviewEnabled();   // default true
bool IsFilterEnabled();    // default true

void SetPropertyEnabled(bool enabled);
void SetPreviewEnabled(bool enabled);
void SetFilterEnabled(bool enabled);

// Feature tier (matches xisf::FeatureTier in the handler DLL)
enum class FeatureTier : uint32_t {
    Basic    = 0,
    Standard = 1,
    Full     = 2
};

FeatureTier GetFeatureTier();       // default Full
void SetFeatureTier(FeatureTier t);

// System.Photo projection toggle
bool IsProjectionEnabled();         // default true
void SetProjectionEnabled(bool enabled);

} // namespace xisf::hostsettings
