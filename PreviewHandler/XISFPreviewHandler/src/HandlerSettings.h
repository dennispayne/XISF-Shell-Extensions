// HandlerSettings.h - Runtime-toggle helpers for the Preview + Thumbnail Handler.
// Reads HKCU\Software\DennisPayne\XISF Shell Extension\PreviewEnabled (DWORD, default 1).
#pragma once

namespace xisf {

// Returns true if the Preview/Thumbnail Handler class factory should create instances.
// Defaults to true when the registry value is absent or unreadable.
bool IsPreviewHandlerEnabled();

} // namespace xisf
