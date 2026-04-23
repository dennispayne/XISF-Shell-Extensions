// HandlerSettings.h - Runtime-toggle helpers for the XISF Search Filter.
// Reads HKCU\Software\DennisPayne\XISF Shell Extension\FilterEnabled (DWORD, default 1).
#pragma once

namespace xisf {

// Returns true if the Filter class factory should create instances.
// Defaults to true when the registry value is absent or unreadable.
bool IsFilterEnabled();

} // namespace xisf
