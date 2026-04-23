# Debugging XISF Shell Extensions

This guide covers debugging all components of the XISF Shell Extensions solution.

## EXE Debugging (ShellExtensionHost)

The settings utility EXE works with standard F5 debugging out of the box:

1. Set **ShellExtensionHost** as the startup project (right-click → Set as Startup Project)
2. Press **F5** — Visual Studio launches the EXE directly
3. Breakpoints, Watch, Locals, Call Stack all work normally

## DLL Debugging (Handler DLLs in Explorer)

The handler DLLs (PropertyHandler, PreviewHandler) run **inside explorer.exe** as in-process COM servers. The solution includes `build/debug.props` which configures F5 to launch Explorer under the debugger.

### Setup

1. Set the handler DLL project as the startup project (e.g., **XISFPropertyHandler**)
2. Build the solution
3. **Kill Explorer** — the debugger needs to launch a fresh Explorer instance:
   ```
   Stop-Process -Name explorer -Force
   ```
4. Press **F5** — Visual Studio launches `explorer.exe` under the debugger
5. In Explorer, navigate to a folder with `.xisf` files
6. The handler DLL loads when Explorer needs properties/thumbnails — breakpoints will hit

### Important: The Build Dance

Explorer caches handler DLLs in-process. After rebuilding a handler DLL, you must:

1. **Unregister** the handler (run ShellExtensionHost → Advanced → Unregister, or `regsvr32 /u`)
2. **Restart Explorer** (`Stop-Process -Name explorer -Force`)
3. **Re-register** the handler (run ShellExtensionHost → Advanced → Register, or `regsvr32`)

Without this sequence, Explorer continues using the old cached DLL.

### Alternative: Attach to Process

For ad-hoc debugging without restarting Explorer:

1. Build the DLL and do the build dance (above)
2. Open **Debug → Attach to Process** (Ctrl+Alt+P)
3. Select **explorer.exe** from the process list
4. Set breakpoints and trigger the handler (e.g., select an .xisf file in Explorer)

> **Note:** Attach-to-process skips the DLL load sequence, so `DllMain` breakpoints won't hit unless you attach before the DLL is loaded.

## Test Debugging

All test projects use Microsoft's CppUnitTestFramework and are debuggable through Test Explorer:

1. Open **Test → Test Explorer** (Ctrl+E, T)
2. Build the solution to discover tests
3. Right-click a test → **Debug** to hit breakpoints in test code and production code

The test DLLs compile production source files directly (no binary dependencies), so stepping into handler code from tests works seamlessly.

## ETW Tracing Alongside the Debugger

You can collect ETW traces while debugging to get both breakpoint control and telemetry:

1. Start an ETW trace session (see `docs/telemetry.md`)
2. Launch the debugger as described above
3. Traces are written to the `.etl` file while you step through code

This is useful for understanding the full event sequence around a specific breakpoint.

## Code Coverage

The solution includes `CodeCoverage.runsettings` for measuring test coverage:

1. Open **Test → Configure Run Settings → Select Solution Wide runsettings File**
2. Choose `CodeCoverage.runsettings`
3. Run tests with coverage: **Test → Analyze Code Coverage → All Tests**

## Tips

- **PDB files** are generated for all 7 projects in both Debug and Release configurations and are colocated with binaries in `x64/{Debug,Release}/`
- **Symbol loading**: VS automatically finds PDBs since they're next to the binaries. If symbols don't load, check **Debug → Windows → Modules** to verify the PDB path
- **Output window**: Watch the Debug output window for DLL load/unload messages when Explorer loads the handler
- Set **Debug → Options → "Enable Just My Code"** to skip framework internals when stepping
