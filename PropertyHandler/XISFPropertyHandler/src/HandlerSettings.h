// HandlerSettings.h - Runtime-toggle helpers for the Property Handler.
// Reads HKCU\Software\DennisPayne\XISF Shell Extension\PropertyEnabled (DWORD, default 1).
#pragma once

namespace xisf {

// Returns true if the Property Handler class factory should create instances.
// Defaults to true when the registry value is absent or unreadable.
bool IsPropertyHandlerEnabled();

} // namespace xisf
