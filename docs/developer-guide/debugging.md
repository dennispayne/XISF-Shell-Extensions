# Debugging

## Debugging Techniques and Tools

Comprehensive debugging guide for XISF Shell Extensions developers. Covers F5 debugging in Visual Studio, ETW trace logging, performance profiling, and techniques for diagnosing issues in handlers that run inside `explorer.exe`.

## Debugging Environment Setup

### F5 Debugging with Visual Studio

**Goal:** Run explorer.exe under the debugger so breakpoints in handler code trigger when you interact with `.xisf` files.

**Setup (one-time):**

1. **Open project properties:**
   - Right-click **XISFPropertyHandler** project → Properties
   - Configuration Properties → Debugging

2. **Configure debugger to launch Explorer:**
   ```
   Debugger to launch: Windows Explorer (explorer.exe)
   Debugging properties:
   - Command: C:\Windows\explorer.exe
   - Working directory: C:\
   ```

3. **Set build output location:**
   - PropertyHandler debug output: `x64\Debug\`
   - Ensure DLL builds to same folder

4. **Enable debugging symbols:**
   - Debugging → Generate Debug Info: `Yes (/DEBUG)`
   - C/C++ → Debug Information Format: `Program Database (/Zi)`

### F5 Debug Session Workflow

```
F5 (Start Debugging)
  │
  ├─ Builds Debug config
  ├─ Registers handler DLL (if configured)
  ├─ Launches explorer.exe under debugger
  │
  ├─ Navigate to .xisf file in Explorer
  ├─ Open Details pane (View → Details Pane)
  │
  ├─ Breakpoint in PropertyHandler triggers
  ├─ Step through code (F10/F11)
  ├─ Inspect variables (Locals window)
  │
  └─ Stop (Shift+F5) to exit
```

**Common Breakpoint Locations:**
- `PropertyStore::GetCount()` — Called when Details pane opens
- `PropertyStore::GetAt(i)` — Called for each property display
- `XISFParser::ParseFile()` — Entry point for parsing
- `ComputedProperties::PopulateComputedProperties()` — Catalog lookup
- `PixelStatistics::Compute()` — Background histogram thread

### Debugging PreviewHandler

Same setup, but breakpoints trigger when:
1. Preview pane is opened (View → Preview Pane)
2. Thumbnail is shown in File Explorer thumbnail view

**Breakpoints:**
- `PreviewHandler::DoPreview()` — Rendering begins
- `ThumbnailProvider::GetThumbnail()` — Thumbnail generation

### Debugging IFilter (Windows Search)

**Challenge:** IFilter runs in `SearchIndexer.exe` process, not explorer.exe.

**Workaround: Use ETW tracing instead** (see [Using ETW for Diagnosis](#using-etw-for-diagnosis) below)

**Alternative: Direct process attachment**
```powershell
# 1. Trigger a Windows Search index operation
# 2. In Visual Studio: Debug → Attach to Process
# 3. Find SearchIndexer.exe
# 4. Attach debugger
# 5. Breakpoints in IFilter code should trigger
```

## Using ETW for Diagnosis

### What is ETW?

**Event Tracing for Windows (ETW)** — Native Windows tracing infrastructure used by:
- Performance Monitor, Perfview
- Windows Performance Analyzer
- Logman (command-line)
- TraceView

**Advantages:**
- Zero overhead when tracing inactive
- Kernel-level visibility (no performance impact)
- Thread/CPU timing built-in
- Survives process crashes
- Works for services, handlers, background tasks

### XISF Shell Extensions ETW Provider

**Provider GUID:** `XISF-PropertyHandler`, `XISF-PreviewHandler`, `XISF-Filter`

**Key events:**
- `Parser:Start` / `Parser:Stop` — Measure XISF parsing time
- `PropertyStore:GetCount` — Trace property store access
- `Catalog:Lookup` — Track DSO name resolution performance
- `Error:ParseFailed` — Log parse errors with diagnostic data

**Trace logging integration:**
- `PropertyHandlerTraceLogging.h` — Defines ETW provider and events
- `WritePropertyHandlerTelemetry()` — Logging function (thread-safe)

### Collecting ETW Traces

**Using PerfView (recommended):**

1. **Download PerfView:**
   ```powershell
   choco install perfview
   # Or download from https://github.com/microsoft/perfview
   ```

2. **Start tracing:**
   ```powershell
   # Launch PerfView
   perfview

   # In GUI:
   # - Providers text box: "XISF-PropertyHandler XISF-PreviewHandler"
   # - Click "Collect"
   ```

3. **Reproduce issue:**
   - Open `.xisf` file in Explorer
   - Interact with Details pane, Preview pane
   - Toggle handlers on/off

4. **Stop collection:**
   - Click "Stop Collection" in PerfView

5. **View trace:**
   - PerfView auto-loads ETL file
   - Search events or view timeline
   - Export to CSV if needed

**Using Logman (command-line):**
```powershell
# Start trace session
logman start XISFTrace -p "XISF-PropertyHandler" -ets

# Reproduce issue here

# Stop and save trace
logman stop XISFTrace -ets
# Output: XISFTrace.etl

# View with TraceView
traceview XISFTrace.etl
```

### Interpreting ETW Output

**Example trace output:**
```
PropertyHandler Parser:Start  (FileSize=2048000 bytes)
  → Parser:End  (DurationMs=42, ImageCount=1, Status=Success)
  → PropertyStore:GetCount (Count=65)
  → PropertyStore:GetAt[0] (PropertyName="System.Image.Dimensions")
  → PropertyStore:GetAt[1] (PropertyName="XISF.Object")
  → Catalog:Lookup (ObjectName="NGC1234", MatchCount=1)
```

**Metrics to track:**
- **Parser:End DurationMs** — If >50ms, parser is too slow
- **Catalog:Lookup MatchCount** — If 0, catalog may not be loaded
- **Error:ParseFailed Status** — Diagnostic code for parse failures

## ETW Tracing in Code

### Adding Instrumentation

Example: Adding ETW event to your function

```cpp
#include "PropertyHandlerTraceLogging.h"

HRESULT MyComponent::DoSomething() {
    WritePropertyHandlerTelemetry(
        TRACE_LEVEL_INFORMATION,
        XISF_ETW_KEYWORD_PERF,
        L"MyComponent:Start Value=%d",
        myValue
    );
    
    auto result = /* ... do work ... */;
    
    if (FAILED(result)) {
        WritePropertyHandlerTelemetry(
            TRACE_LEVEL_ERROR,
            XISF_ETW_KEYWORD_ERROR,
            L"MyComponent:Failed HRESULT=0x%08X",
            result
        );
    }
    
    return result;
}
```

### ETW Keywords (Filtering)

| Keyword | Purpose | Verbosity |
|---------|---------|-----------|
| `XISF_ETW_KEYWORD_PERF` | Performance measurements | Info |
| `XISF_ETW_KEYWORD_ERROR` | Error events | Error/Warning |
| `XISF_ETW_KEYWORD_CATALOG` | Catalog operations | Info |
| `XISF_ETW_KEYWORD_PARSER` | Parser tracing | Verbose |

**Filter by keyword in PerfView:**
- Providers: `XISF-PropertyHandler:0x1` (only errors)
- Providers: `XISF-PropertyHandler:0x3` (errors + perf)

## Debugging Test Code

### Running Single Test with Debugger

1. **Open Test Explorer:** Ctrl+E, T
2. **Right-click test → Debug Selected Tests**
   - Launches test under debugger
   - Breakpoints in test code trigger

### Debugging Handler in Unit Test

Tests compile handler source directly; breakpoints work normally.

```cpp
// XISFPropertyHandlerTests.cpp
TEST_METHOD(TestPropertyExtraction) {
    // Arrange
    MockStream stream("sample.xisf");
    CXISFPropertyHandler handler;
    handler.Initialize(nullptr, &stream, nullptr);  // ← Breakpoint here
    
    // Act
    UINT count;
    handler.GetCount(&count);  // ← Or here
    
    // Assert
    Assert::AreEqual(65U, count);
}
```

Run test → Breakpoints trigger normally.

## Common Debugging Issues

### "No Symbols Loaded"

**Problem:** Debugger can't find `.pdb` files

**Solution:**
```
Debug → Windows → Modules
  Right-click DLL → Load Symbols
  Browse to x64\Debug\XISFPropertyHandler.pdb
```

Or set symbol path:
```
Tools → Options → Debugging → Symbols
  Add: x64\Debug
```

### "Module Not Loaded"

**Problem:** Handler DLL not registered; Explorer doesn't load it

**Solution:**
1. Launch Settings app: `x64\Release\XISFShellExtensionHost.exe`
2. Toggle handler **on** (should see success message)
3. Restart Explorer: `taskkill /IM explorer.exe` then start Explorer again
4. Retry F5 debugging

### "Breakpoint Will Not Be Hit"

**Problem:** Line of code is optimized away or unreachable

**Solutions:**
- Build in **Debug** configuration (not Release)
- Check Optimization settings: C/C++ → Optimization → `/Od` (Disabled)
- Ensure module was just built (timestamps are recent)

### Handler Code Hangs Debugger

**Problem:** Handler is deadlocked or waiting indefinitely

**Solution:**
- Break in debugger: Ctrl+Alt+Break
- View Call Stack window to see where code is stuck
- Check for lock contention, event waits, file I/O blocking

**Prevention:**
- Never use `WaitForSingleObject()` with `INFINITE` in handler code
- Keep handler work off UI thread (use background threads)
- Timeouts for all blocking operations

## Performance Profiling

### Measuring Parser Performance

```cpp
auto start = std::chrono::high_resolution_clock::now();
auto result = XISFParser::ParseFile("large.xisf");
auto elapsed = std::chrono::high_resolution_clock::now() - start;
auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
OutputDebugStringW(std::format(L"Parse took {} ms\n", ms).c_str());
```

### Profiling Handler Performance (PerfView)

1. **Collect CPU samples:**
   ```powershell
   perfview CollectMultiple explorer.exe
   ```
   (while interacting with .xisf file)

2. **View call stack:**
   - Open `.etl` in PerfView
   - Filter to `explorer.exe`
   - Group by function
   - Largest boxes = hottest code

3. **Optimize targets:**
   - XISFParser if >50ms per file
   - DSOCatalog::LookupNearest if >10ms
   - Histogram computation if blocking UI

## Memory Leak Detection

### Detecting Leaks in Debug Build

**Visual Studio Debug Heap:**
```cpp
#define _CRTDBG_MAP_ALLOC
#include <crtdbg.h>

int main() {
    _CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
    
    // Run your code
    
    // At exit, any leaks are dumped to Debug output
}
```

**Output format:**
```
Detected memory leaks!
Dumping objects ->
{123} normal block at 0x00A23F80, 256 bytes long.
 Data: < ...  ...  ...  ...>CD CD CD CD CD CD CD CD CD CD CD CD CD CD CD CD
Object dump complete.
```

### Using ASAN (AddressSanitizer)

```powershell
# Build with ASAN
msbuild PropertyHandler\XISFPropertyHandler\XISFPropertyHandler.vcxproj `
  /p:Configuration=Release `
  /p:Platform=x64 `
  /p:EnableASan=true

# Run tests or handler
# ASAN will report:
# - Use-after-free
# - Buffer overflow
# - Memory leaks
```

**Output to stderr** when issues detected.

### Profiling Memory (PerfView)

```powershell
perfview /GCOnly collect explorer.exe
# Reproduces issue here
perfview /GCOnly stop

# View:
# - GC allocations by type
# - Large object heap
# - Pinned memory
```

## Debugging Install/Uninstall

### MSI Installation Issues

**Enable verbose MSI logging:**
```powershell
# Install with verbose logging
msiexec /i XISF.ShellExtensions_1.0.0_x64.msi /l*v install.log

# View log
cat install.log | Select-String -Pattern "Error|Warning|Return"
```

**Common issues:**
- File in use (reboot after uninstall)
- Registry permissions (run as admin)
- Wrong Platform (x86 vs x64 mismatch)

### Registry Debugging

**View handler registration:**
```powershell
# Check if handlers registered
Get-Item -Path "HKCU:\Software\Classes\.xisf" -ErrorAction SilentlyContinue

# Check handler toggles
Get-ItemProperty -Path "HKCU:\Software\DennisPayne\XISF Shell Extension"

# Manually disable handler (for testing)
Set-ItemProperty -Path "HKCU:\Software\DennisPayne\XISF Shell Extension" `
  -Name PropertyEnabled -Value 0
```

## Debugging Common Failures

| Symptom | Probable Cause | Debug Steps |
|---------|----------------|-------------|
| Details pane blank | Handler not registered | Check registry; re-run Settings app |
| Parser timeout | Large XISF file; slow disk | Use PerfView to profile parser |
| "Access Denied" | File permissions | Check NTFS ACLs; run as admin |
| Crash (0xC0000374) | Heap corruption | Enable ASAN; use heap debugger |
| Catalog lookup returns 0 | Catalog file not found | Check `%LOCALAPPDATA%\XISFShellExtension\catalogs\` |
| Thumbnail not showing | Preview Handler not registered | Re-enable in Settings app; restart Explorer |

## Documentation

- **Logging:** [Telemetry & ETW](../features/telemetry-etw.md)
- **Testing:** [Testing Guide](testing.md)
- **Architecture:** [Architecture](architecture.md)
- **Windows Shell:** [Microsoft Shell Extension Reference](https://docs.microsoft.com/en-us/windows/win32/shell/handlers)

---

Related: [Testing](testing.md), [Architecture](architecture.md), [Building](building.md)
