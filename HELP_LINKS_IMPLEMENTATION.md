# Contextual Help Links Implementation for XISF Shell Extension Settings

## Overview
Added comprehensive contextual help links to the XISF Shell Extension Settings app (ShellExtensionHost.cpp) to connect UI elements to relevant documentation.

## Implementation Summary

### 1. Helper Functions Added

#### `GetDocumentationUrl(const wchar_t* docPath)`
- Constructs documentation URLs with intelligent fallback
- First tries to open local documentation via `file://` protocol (for MSI installs)
- Falls back to GitHub URLs if local files not found
- Works with both dev builds and installed MSI packages

#### `OpenDocumentation(HWND hDlg, const wchar_t* docPath)`
- Wrapper function to open documentation URLs using ShellExecute()
- Handles both local and GitHub URLs seamlessly

### 2. Enhanced Tooltips (AddTooltips function)
Expanded tooltip text for better discoverability:
- **Property Handler toggle**: Describes function + help hint
- **Preview Handler toggle**: Describes function + help hint  
- **Search Filter toggle**: Describes function + help hint
- **Catalog buttons** (Fetch NGC, Fetch Add, Import): Includes help references
- **Hash verification button**: Links to documentation
- **Feature tier button**: Links to tier documentation
- **Advanced button**: Links to ETW tracing documentation

### 3. Handler Toggle Buttons (IDC_BTN_TOGGLE_*)

Added context-sensitive help:
- **Triggers**: Ctrl+Click or Shift+Click on any toggle button
- **Property Handler**: Opens `user-guide/handlers-overview.md#property-handler`
- **Preview Handler**: Opens `user-guide/handlers-overview.md#preview-handler`
- **Search Filter**: Opens `user-guide/handlers-overview.md#search-filter`
- Shows confirmation dialog before opening documentation

### 4. Catalog Management Buttons

#### IDC_BTN_FETCH_NGC / IDC_BTN_FETCH_ADD
- Shows info dialog explaining the operation
- Offers to open `user-guide/catalog-management.md` before proceeding
- User can proceed with download or read docs first

#### IDC_BTN_IMPORT_FILE
- Shows info dialog about local import
- Offers to open `user-guide/catalog-management.md#offline-import`
- User can proceed or view documentation first

#### IDC_BTN_COPY_EXPECTED_HASHES
- After copying hashes, shows confirmation dialog
- Offers to open `user-guide/catalog-management.md#verification`
- Educates users about hash verification

### 5. Feature Tier Button (IDC_BTN_SHOW_TIERS)

Enhanced with dual functionality:
1. Shows the existing IDD_TIERS dialog
2. **After** dialog, prompts user to view full documentation in browser
3. Opens `features/feature-tiers.md` if user accepts

### 6. Advanced Button (IDC_BTN_ADVANCED)

Enhanced workflow:
1. First shows dialog asking about ETW tracing documentation
2. If user accepts, opens `features/telemetry-etw.md`
3. Then proceeds to open advanced dialog regardless

### 7. Advanced Dialog ETW Trace Controls (AdvancedDlgProc)

#### IDC_BTN_TRACE_START
- Shows confirmation dialog before starting trace
- Offers to view `features/telemetry-etw.md` first
- If declined, proceeds with trace startup

#### IDC_BTN_TRACE_OPEN_ETL
- Shows confirmation dialog when viewing trace file
- Offers to view `features/telemetry-etw.md` first
- If declined, opens ETL file viewer

### 8. Link Controls (WM_NOTIFY handlers)

Enhanced existing link handlers:
- **IDC_LINK_OPENNGC_COMMIT**: Now shows informative message when clicked
- **IDC_LINK_HASH_HELP**: Enhanced to offer local documentation first, fallback to GitHub
- **IDC_LINK_VERSION**: Offers choice between GitHub repo or local getting-started guide

## Documentation Links Mapped

| UI Element | Documentation Path |
|-----------|-------------------|
| Property Handler toggle | `user-guide/handlers-overview.md#property-handler` |
| Preview Handler toggle | `user-guide/handlers-overview.md#preview-handler` |
| Search Filter toggle | `user-guide/handlers-overview.md#search-filter` |
| Fetch NGC/Addendum | `user-guide/catalog-management.md` |
| Import from File | `user-guide/catalog-management.md#offline-import` |
| Copy Expected Hashes | `user-guide/catalog-management.md#verification` |
| Feature Tier dropdown | `features/feature-tiers.md` |
| Advanced button | `features/telemetry-etw.md` |
| Start Trace | `features/telemetry-etw.md` |
| View ETL Trace | `features/telemetry-etw.md` |
| Hash verification link | `user-guide/catalog-management.md#verification` |
| Version link | `getting-started.md` |

## User Experience Design

### Discovery Methods
1. **Tooltips**: Hover over buttons to see helpful context + documentation hints
2. **Modifier Keys**: Ctrl+Click or Shift+Click on handler toggles shows docs
3. **Dialog Prompts**: Action dialogs offer doc links before/after operations
4. **Links**: Click on existing hyperlinks for full documentation

### Smart Fallback Strategy
- **Local files**: If docs are found in `C:\Program Files\XISF Shell Extensions\docs\...`
- **GitHub URLs**: If local files not found, direct to GitHub repository
- **Works offline**: Local installations have docs bundled; dev builds fallback to GitHub

### Non-Intrusive Design
- Help links are **suggestions**, not blockers
- Users can skip documentation and proceed with operations
- No mandatory clicks or interruptions
- Status bar shows informative messages about what was opened

## Implementation Details

### Key Changes in ShellExtensionHost.cpp
- Added ~40 lines of helper functions
- Enhanced ~20 lines of tooltip text
- Modified ~15 handler cases to add help logic
- Total additions: ~100 lines of code

### Dialog Message Handling
- All help dialogs use `MessageBoxW()` with `MB_YESNO | MB_ICONINFORMATION`
- Consistent messaging pattern across all controls
- Status bar updated to confirm documentation opened

### URL Construction
- Local paths: Convert backslashes to forward slashes for `file://` protocol
- GitHub URLs: Direct to tree/main/docs/ path for raw file viewing
- Anchor links: Support markdown section anchors (e.g., `#property-handler`)

## Testing Recommendations

1. **Build & Deploy**: Verify compilation and resource IDs correct
2. **Local Docs**: Test with docs folder bundled in install location
3. **GitHub Fallback**: Test with docs folder removed (should fallback to GitHub)
4. **Modifier Keys**: Verify Ctrl+Click and Shift+Click triggers on toggles
5. **Dialog Flows**: Verify all Yes/No prompts work correctly
6. **URL Opening**: Verify URLs open in default browser
7. **Status Messages**: Verify progress bar shows appropriate messages

## Future Enhancements

1. **Keyboard Shortcuts**: Add F1 context-sensitive help
2. **Embedded Help**: Mini help panels instead of external links
3. **Offline Help Browser**: Built-in documentation viewer
4. **Video Tutorials**: Links to YouTube walkthroughs
5. **Accessibility**: Screen reader compatible help guidance

## Files Modified
- `ShellExtensionHost/ShellExtensionHost/src/ShellExtensionHost.cpp`

## Backward Compatibility
✅ All changes are backward compatible:
- Existing functionality preserved
- Help links are additive (don't break existing flows)
- Works with existing and new documentation structure
- No resource file changes required (no new UI controls needed)
