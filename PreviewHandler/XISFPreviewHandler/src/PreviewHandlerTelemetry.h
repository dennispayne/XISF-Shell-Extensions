// PreviewHandlerTelemetry.h — ETW provider declarations for the XISF Preview/Thumbnail handler.
//
// Mirrors the direct-ETW pattern used by Property Handler (see PropertyStore.cpp/dllmain.cpp).
// Event channels are intentionally coarse: the handler runs inside Explorer and must
// avoid event storms, so per-property chatter is aggregated into summary events.
//
#pragma once

#include <windows.h>
#include <evntprov.h>

// {4fd34fd0-08b3-5d9a-8d77-b9d6705d6b75}
// Provider: "XISF-PreviewHandler"
extern "C" const GUID kPreviewHandlerTelemetryProvider;

// Registered in DLL_PROCESS_ATTACH / unregistered in DLL_PROCESS_DETACH (dllmain.cpp).
// Defined once in ThumbnailProvider.cpp.
extern "C" REGHANDLE g_hPreviewHandlerTelemetryHandle;

// Keywords — separate bits for filter-set granularity
static constexpr ULONGLONG XISF_PREVIEW_KEYWORD_LIFECYCLE = 0x1;
static constexpr ULONGLONG XISF_PREVIEW_KEYWORD_PARSE     = 0x2;
static constexpr ULONGLONG XISF_PREVIEW_KEYWORD_PREVIEW   = 0x4;
static constexpr ULONGLONG XISF_PREVIEW_KEYWORD_THUMBNAIL = 0x8;
static constexpr ULONGLONG XISF_PREVIEW_KEYWORD_FALLBACK  = 0x10;
static constexpr ULONGLONG XISF_PREVIEW_KEYWORD_PERF      = 0x20;

// Default verbosity: informational + warning events; verbose reserved for tracing.
void WritePreviewHandlerTelemetry(UCHAR level, ULONGLONG keyword,
                          _In_z_ _Printf_format_string_ const wchar_t* fmt, ...);

// Test hook: when set, every formatted telemetry payload is also delivered
// here (regardless of ETW provider enablement). Tests install a capturing
// callback to assert on emitted events without needing a live ETW session.
extern "C" {
    using XISFPreviewHandlerTelemetryHook = void (*)(UCHAR level, ULONGLONG keyword, const wchar_t* message);
    extern XISFPreviewHandlerTelemetryHook g_xisfPreviewHandlerTelemetryHook;
}
