// FilterTelemetry.h — TraceLogging provider for the XISF Search Filter.
#pragma once

#include <windows.h>
#include <TraceLoggingProvider.h>
#include <evntrace.h>

// {3A4B5C6D-7E8F-9012-AB34-CD56EF789012}  Provider: "XISF-Filter"
TRACELOGGING_DECLARE_PROVIDER(g_hFilterProvider);

static constexpr ULONGLONG XISF_FILTER_KEYWORD_LIFECYCLE = 0x0000000000000001ULL;
static constexpr ULONGLONG XISF_FILTER_KEYWORD_PARSE     = 0x0000000000000002ULL;
static constexpr ULONGLONG XISF_FILTER_KEYWORD_FILTER    = 0x0000000000000004ULL;
