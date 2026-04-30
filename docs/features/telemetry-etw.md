# XISF Handler Telemetry & Event Tracing for Windows

The Property and Preview shell handlers emit Event Tracing for Windows (ETW) events using the [TraceLogging](https://learn.microsoft.com/en-us/windows/win32/tracelogging/trace-logging-portal) API. Each event carries typed, structured fields (strings, integers, HRESULTs, durations) that trace viewers (WPA, PerfView) can filter, sort, and aggregate natively — no format-string parsing required. 

**Important**: Traces are **local-only** — no data leaves the machine, and nothing is aggregated or uploaded. Full file paths may appear in trace strings; path privacy is not a concern for this repo.

## Providers

| Handler | Provider name        | Provider GUID                              | Host binary                  |
|---------|----------------------|--------------------------------------------|------------------------------|
| Property | XISF-PropertyHandler | `{6F6B0C9D-6B76-5A24-BC3D-708314E96F2B}`    | `XISFPropertyHandler.dll`    |
| Preview  | XISF-PreviewHandler  | `{4FD34FD0-08B3-5D9A-8D77-B9D6705D6B75}`    | `XISFPreviewHandler.dll`     |

Each provider is registered via `TraceLoggingRegister` in `DllMain(DLL_PROCESS_ATTACH)` and unregistered via `TraceLoggingUnregister` in `DLL_PROCESS_DETACH`. No external registration (e.g. `wevtutil`) is required.

## Levels and keywords

Default verbosity is **info + warning**. Verbose is reserved for explicit tracing sessions (not emitted at runtime unless the provider is configured with `TRACE_LEVEL_VERBOSE`).

| Level   | Meaning                                                 |
|---------|---------------------------------------------------------|
| ERROR   | A caller-visible failure (e.g. `GetThumbnail` returns `E_FAIL`). |
| WARNING | A non-fatal failure stage or fallback path.             |
| INFO    | Lifecycle and aggregated completion events.             |
| VERBOSE | (reserved) Detailed per-record traces.                  |

### Property handler keywords

| Bit | Keyword       | Purpose                                                  |
|-----|---------------|----------------------------------------------------------|
| 0x1 | `LIFECYCLE`   | DLL attach/detach, `Initialize` success & failure stages |
| 0x2 | `PARSE`       | XISF preamble/header/XML parse stages                    |
| 0x4 | `CATALOG`     | DSO catalog configuration and lookups                    |
| 0x8 | `PROJECTION`  | Coordinate geometry and projection evaluation            |
| 0x10| `PERF`        | Timing / aggregated summary events                       |

### Preview handler keywords

| Bit | Keyword      | Purpose                                              |
|-----|--------------|------------------------------------------------------|
| 0x1 | `LIFECYCLE`  | DLL attach/detach, `Initialize` / `Unload`           |
| 0x2 | `PARSE`      | XISF preamble/header/XML parse stages                |
| 0x4 | `PREVIEW`    | `DoPreview` window creation / display                |
| 0x8 | `THUMBNAIL`  | Thumbnail decode / completion events                 |
| 0x10| `FALLBACK`   | Placeholder bitmap used                              |
| 0x20| `PERF`       | Timing / aggregated summaries                        |

## Local collection

Events are emitted as TraceLogging structured payloads. Any ETW consumer that understands TraceLogging providers will decode the typed fields automatically. Legacy string-mode consumers still work — a `LegacyTrace` event with a single `Message` string field is emitted alongside each structured event via the `WritePropertyHandlerTelemetry` / `WritePreviewHandlerTelemetry` wrappers.

### Option 1 – `logman` (no extra files)

`logman` only accepts a single `-p` per invocation, so add providers one at a time with `update trace`:

```powershell
logman create trace XISFTrace -o C:\Temp\xisf.etl -ets
logman update trace XISFTrace -p "{6F6B0C9D-6B76-5A24-BC3D-708314E96F2B}" 0xFFFF 0x04 -ets
logman update trace XISFTrace -p "{4FD34FD0-08B3-5D9A-8D77-B9D6705D6B75}" 0xFFFF 0x04 -ets

# Reproduce the scenario (right-click an .xisf file, or view Details pane) …

logman stop XISFTrace -ets
```

Level `0x04` = informational (includes warning and error). Use `0x05` for verbose when investigating deep issues. Keyword mask `0xFFFF` captures every keyword defined above.

If you prefer a single `start` command, stage the providers in a text file and pass it via `-pf`:

```powershell
@"
"{6F6B0C9D-6B76-5A24-BC3D-708314E96F2B}" 0xFFFF 0x04
"{4FD34FD0-08B3-5D9A-8D77-B9D6705D6B75}" 0xFFFF 0x04
"@ | Set-Content xisf-providers.txt -Encoding ASCII

logman start XISFTrace -pf xisf-providers.txt -o C:\Temp\xisf.etl -ets
# … reproduce …
logman stop XISFTrace -ets
```

### Option 2 – `wpr` (Windows Performance Recorder)

`wpr` needs a recording profile file. Save the following as `XISFTrace.wprp` anywhere convenient (it is *not* committed to the repo):

```xml
<?xml version="1.0" encoding="utf-8"?>
<WindowsPerformanceRecorder Version="1.0">
  <Profiles>
    <EventCollector Id="EC_XISF" Name="XISF Collector">
      <BufferSize Value="1024" />
      <Buffers Value="64" />
    </EventCollector>
    <EventProvider Id="EP_Property" Name="6F6B0C9D-6B76-5A24-BC3D-708314E96F2B" Level="4" />
    <EventProvider Id="EP_Preview" Name="4FD34FD0-08B3-5D9A-8D77-B9D6705D6B75" Level="4" />
    <Profile Id="XISFTrace.Verbose.File" Name="XISFTrace" Description="XISF handler trace"
             LoggingMode="File" DetailLevel="Verbose">
      <Collectors>
        <EventCollectorId Value="EC_XISF">
          <EventProviders>
            <EventProviderId Value="EP_Property" />
            <EventProviderId Value="EP_Preview" />
          </EventProviders>
        </EventCollectorId>
      </Collectors>
    </Profile>
  </Profiles>
</WindowsPerformanceRecorder>
```

Then:

```powershell
wpr -start .\XISFTrace.wprp
# … reproduce …
wpr -stop C:\Temp\xisf.etl
```

### Viewing the trace

- **Windows Performance Analyzer (WPA)** – open the `.etl`, add the *Generic Events* table, filter by provider GUID.
- **`tracerpt`** – `tracerpt xisf.etl -o xisf.csv -of CSV` for a flat CSV.
- **PerfView** – open the `.etl`, switch to *Events*, filter by provider.

## Event naming convention

Events use short descriptive names as the TraceLogging event name (e.g. `PropertyStoreInitializeFailed`, `ThumbnailCompleted`), with typed fields for each parameter:

```
Event: PropertyStoreInitializeFailed
  Stage       = "InvalidSignature"  (string)
  Hr          = 0x8007000D          (HRESULT)
  Mode        = 0                   (uint32)
  BytesRead   = 16                  (uint32)
  DurationMs  = 3                   (uint64)

Event: ThumbnailCompleted
  RequestedSize   = 256   (uint32)
  UsedPlaceholder = 0     (bool)
  DurationMs      = 47    (int64)

Event: PropertyPopulation
  FitsKeywordCount = 23   (uint32)
  PropertyCount    = 42   (uint32)
  MatchedObjectCount = 3  (uint32)
  DurationMs       = 12   (uint64)
```

Prefer one aggregated event per operation over per-property granularity to avoid event storms in the Explorer host process.

## Test hooks

Both handlers expose a function-pointer hook for unit tests to capture telemetry without needing a live ETW session:

- `g_xisfPropertyHandlerTelemetryHook` — set in PropertyHandler tests
- `g_xisfPreviewHandlerTelemetryHook` — set in PreviewHandler tests

When set, the hook receives `(level, keyword, formatted_message)` for every event, enabling assertions on emitted telemetry in CI.

---

Related: [Debugging Guide](../developer-guide/debugging.md), [Architecture](../developer-guide/architecture.md)
