// PreviewHandlerTraceLogging.h — TraceLogging provider for the XISF Preview/Thumbnail Handler.
//
// Uses the Windows TraceLogging API for structured ETW events with typed fields.
// The provider GUID matches the legacy EventWriteString provider so existing
// logman/WPR collection workflows continue working unchanged.
//
#pragma once

#include <windows.h>
#include <TraceLoggingProvider.h>
#include <evntrace.h>

// {4FD34FD0-08B3-5D9A-8D77-B9D6705D6B75}  Provider: "XISF-PreviewHandler"
TRACELOGGING_DECLARE_PROVIDER(g_hPreviewProvider);

// Keywords — same bit values as the legacy EventWriteString implementation
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
