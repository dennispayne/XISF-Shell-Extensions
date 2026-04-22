// HostSettings.h - Read/write HKCU handler-enable flags.
#pragma once

namespace xisf::hostsettings {

bool IsPropertyEnabled();  // default true
bool IsPreviewEnabled();   // default true

void SetPropertyEnabled(bool enabled);
void SetPreviewEnabled(bool enabled);

} // namespace xisf::hostsettings
