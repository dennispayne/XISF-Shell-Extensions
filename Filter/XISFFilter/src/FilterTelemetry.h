// FilterTelemetry.h — TraceLogging provider for the XISF Search Filter.
//
// Uses the Windows TraceLogging API for structured ETW events with typed fields.
// The provider GUID is unique to the filter DLL.
//
#pragma once

#include <windows.h>
#include <TraceLoggingProvider.h>
#include <evntrace.h>

// {3A4B5C6D-7E8F-9012-AB34-CD56EF789012}  Provider: "XISF-Filter"
TRACELOGGING_DECLARE_PROVIDER(g_hFilterProvider);

static constexpr ULONGLONG XISF_FILTER_KEYWORD_LIFECYCLE = 0x0000000000000001ULL;
static constexpr ULONGLONG XISF_FILTER_KEYWORD_PARSE     = 0x0000000000000002ULL;
static constexpr ULONGLONG XISF_FILTER_KEYWORD_FILTER    = 0x0000000000000004ULL;

// Default verbosity: informational + warning events; verbose reserved for tracing.
void WriteFilterTelemetry(UCHAR level, ULONGLONG keyword,
                          _In_z_ _Printf_format_string_ const wchar_t* fmt, ...);

// Test hook: when set, every formatted telemetry payload is also delivered
// here (regardless of ETW provider enablement). Tests install a capturing
// callback to assert on emitted events without needing a live ETW session.
extern "C" {
    using XISFFilterTelemetryHook = void (*)(UCHAR level, ULONGLONG keyword, const wchar_t* message);
    extern XISFFilterTelemetryHook g_xisfFilterTelemetryHook;
}
