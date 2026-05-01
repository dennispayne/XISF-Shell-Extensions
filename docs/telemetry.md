# XISF Handler Telemetry

**Note:** This file has been migrated to the new documentation structure. Please see [Telemetry & ETW](features/telemetry-etw.md) for the updated version.

---

The Property and Preview shell handlers emit Event Tracing for Windows (ETW) events
using the [TraceLogging](https://learn.microsoft.com/en-us/windows/win32/tracelogging/trace-logging-portal)
API. Each event carries typed, structured fields (strings, integers, HRESULTs,
durations) that trace viewers (WPA, PerfView) can filter, sort, and aggregate
natively — no format-string parsing required. Traces are **local-only** — no data
leaves the machine, and nothing is aggregated or uploaded. Full file paths may
appear in trace strings; path privacy is not a concern for this repo.
