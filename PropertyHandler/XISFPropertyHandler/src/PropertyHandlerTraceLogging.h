// PropertyHandlerTraceLogging.h — TraceLogging provider for the XISF Property Handler.
//
// Uses the Windows TraceLogging API for structured ETW events with typed fields.
// The provider GUID matches the legacy EventWriteString provider so existing
// logman/WPR collection workflows continue working unchanged.
//
#pragma once

#include <windows.h>
#include <TraceLoggingProvider.h>
#include <evntrace.h>

// {6F6B0C9D-6B76-5A24-BC3D-708314E96F2B}  Provider: "XISF-PropertyHandler"
TRACELOGGING_DECLARE_PROVIDER(g_hPropertyProvider);

// Keywords — same bit values as the legacy EventWriteString implementation
static constexpr ULONGLONG XISF_ETW_KEYWORD_LIFECYCLE  = 0x0000000000000001ULL;
static constexpr ULONGLONG XISF_ETW_KEYWORD_PARSE      = 0x0000000000000002ULL;
static constexpr ULONGLONG XISF_ETW_KEYWORD_CATALOG    = 0x0000000000000004ULL;
static constexpr ULONGLONG XISF_ETW_KEYWORD_PROJECTION = 0x0000000000000008ULL;
static constexpr ULONGLONG XISF_ETW_KEYWORD_PERF       = 0x0000000000000010ULL;

// Test hook: when set, every formatted telemetry payload is also delivered
// here (regardless of ETW provider enablement). Tests install a capturing
// callback to assert on emitted events without needing a live ETW session.
extern "C" {
    using XISFPropertyHandlerTelemetryHook = void (*)(UCHAR level, ULONGLONG keyword, const wchar_t* message);
    extern XISFPropertyHandlerTelemetryHook g_xisfPropertyHandlerTelemetryHook;
}

// Legacy-compatible wrapper: formats a message string for the test hook,
// then emits via TraceLogging.  Call sites that only need the test hook
// (no structured fields) can use this directly.
void WritePropertyHandlerTelemetry(UCHAR level, ULONGLONG keyword,
    _In_z_ _Printf_format_string_ const wchar_t* fmt, ...);
