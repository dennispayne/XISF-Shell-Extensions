# Testing

## Testing Strategy and Procedures

Comprehensive testing is critical for shell handlers that run in-process in `explorer.exe`. This guide covers unit tests, integration tests, performance benchmarks, and how to run the full test suite locally and in CI.

## Testing Strategy

### Test Organization

```
PropertyHandler/
├── XISFPropertyHandlerTests/
│   ├── XISFPropertyHandlerTests.cpp   (65+ property tests)
│   ├── Test fixtures for XISF files
│   └── Unit tests compiled into .dll
├── XISFPropertyHandler/
│   └── src/
│       ├── XISFParser.cpp             (parsing logic)
│       ├── PropertyStore.cpp          (COM handler)
│       └── ComputedProperties.cpp     (derived properties)

PreviewHandler/
├── XISFPreviewHandlerTests/
│   └── XISFPreviewHandlerTests.cpp    (rendering, thumbnails)

Filter/
├── XISFFilterTests/
│   └── XISFFilterTests.cpp            (Windows Search indexing)

PerformanceTests/
├── PropertyStorePerf.cpp              (property extraction speed)
├── ThumbnailPerf.cpp                 (render time benchmarks)
├── ParserPerf.cpp                    (XML parsing speed)
├── ConcurrencyStress.cpp             (thread safety)
└── MemoryLeakTest.cpp                (leak detection)
```

### Test Perspectives

Tests are written from three viewpoints:

1. **Principal Developer** — API contracts, error handling, code correctness
   - Does `XISFParser::ParseFile()` return correct metadata?
   - Do COM reference counts balance?
   - Are thread-safety invariants maintained?

2. **Astrophotographer** — Feature correctness and usability
   - Does Object Name resolution work for NGC catalog?
   - Are pixel statistics accurate?
   - Do thumbnails render at correct scale?

3. **Windows Sysadmin** — Robustness and safety
   - Does corrupt XISF file crash explorer.exe?
   - Are permissions enforced (read-only)?
   - Does handler timeout gracefully?

## Unit Testing

### Test Framework

All C++ tests use **Microsoft C++ Unit Test Framework** (built into Visual Studio):

```cpp
#include "CppUnitTest.h"
using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(XISFParserTests) {
    TEST_METHOD(ParseValidXISFFile) {
        auto result = XISFParser::ParseFile("sample.xisf");
        Assert::IsTrue(result.ok());
        Assert::AreEqual(size_t(2), result.metadata.imageCount);
    }
    
    TEST_METHOD(ParseErrorHandling) {
        auto result = XISFParser::ParseFile("nonexistent.xisf");
        Assert::IsFalse(result.ok());
        Assert::AreEqual(ParseError::FileNotFound, result.error);
    }
};
```

### Property Handler Tests

**File:** `PropertyHandler\XISFPropertyHandlerTests\XISFPropertyHandlerTests.cpp`

**Coverage:**

| Category | Tests | Purpose |
|----------|-------|---------|
| **Parser** | 12 | Validate XISF binary parsing, XML extraction, error handling |
| **Properties** | 65+ | Each property extracted correctly (dimensions, date, camera, etc.) |
| **Catalog Lookups** | 15 | Object name resolution, cone search, constellation mapping |
| **Computed Properties** | 20 | Derived metadata (RA hour bands, Dec bands, filter types) |
| **COM Contracts** | 8 | IPropertyStore ref counting, QueryInterface, initialization |
| **Edge Cases** | 10 | Corrupt files, missing properties, malformed XML |

**Key Test Fixtures:**
- `sample_basic.xisf` — Minimal valid XISF with basic metadata
- `sample_astro.xisf` — Full astrophotography file with RA/Dec, catalog data
- `sample_corrupt.xisf` — Intentionally malformed for error handling tests

### Preview Handler Tests

**File:** `PreviewHandler\XISFPreviewHandlerTests\XISFPreviewHandlerTests.cpp`

**Coverage:**
- Image decoding (RGB, grayscale, mono16)
- Thumbnail rendering at various sizes
- Histogram generation accuracy
- Color space conversions
- Error handling (corrupt image data)

### IFilter Tests

**File:** `Filter\XISFFilterTests\XISFFilterTests.cpp`

**Coverage:**
- Metadata extraction for indexing
- Windows Search VARIANT formatting
- Catalog-enriched text generation
- Multi-language support

## Integration Testing

### Installer Tests

**File:** `Installer\XISFInstallerTests\XISFInstallerTests.csproj` (C# MSTest)

**Coverage:**
- MSI installation succeeds
- Registry entries created correctly
- DLL files copied to Program Files
- Uninstall removes all artifacts
- Handler toggling via Settings app

**Run integration tests:**
```powershell
# Build MSI first
msbuild Installer\XISFInstaller\XISFInstaller.wixproj /restore /p:Configuration=Release /m /v:minimal /nologo

# Run installer tests
dotnet test Installer\XISFInstallerTests\XISFInstallerTests.csproj
```

### End-to-End Testing (Manual)

1. **Build solution** in Release mode
2. **Launch Settings app:** `x64\Release\XISFShellExtensionHost.exe`
3. **Toggle each handler on/off**, verify:
   - Registry entries appear/disappear
   - Explorer Details pane shows/hides metadata
   - Thumbnails appear/disappear
4. **Download catalogs** via Settings app, verify:
   - Object names appear in Details pane
   - Search finds files by NGC number
5. **Restart Explorer,** verify handlers still work

## Performance Testing

### Performance Test Suite

**File:** `PerformanceTests\PerformanceTests.vcxproj`

Tests measure:

| Benchmark | Target | Importance |
|-----------|--------|-----------|
| **Parser** | <50 ms | Runs on UI thread; must be fast |
| **Catalog Lookup** | <10 ms | Property retrieval must feel instant |
| **Histogram Rendering** | <500 ms | Background thread; user can wait |
| **Thumbnail Generation** | <200 ms | Called frequently; cancellable |
| **Memory Usage** | <1 MB per instance | Explorer runs hundreds of handlers |
| **Startup Latency** | <100 ms | DLL load + first property lookup |

### Individual Performance Tests

**Parser Performance:**
```cpp
// PerformanceTests/ParserPerf.cpp
TEST_METHOD(XISFParserSpeed) {
    auto start = std::chrono::high_resolution_clock::now();
    auto result = XISFParser::ParseFile("large_sample.xisf");
    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
    Assert::IsTrue(ms < 50, L"Parser took >50ms");
}
```

**Run performance tests:**
```powershell
# Build
msbuild PerformanceTests\PerformanceTests.vcxproj /p:Configuration=Release /p:Platform=x64

# Run with timing
vstest.console.exe x64\Release\PerformanceTests.dll /Logger:console
```

### Memory Leak Detection

**Asan (AddressSanitizer) for MSVC:**
```powershell
# Build with sanitizers
msbuild Win11-XISF-Shell-Extensions.sln `
  /p:Configuration=Release `
  /p:Platform=x64 `
  /p:EnableASan=true

# Run tests; ASAN reports any leaks to stderr
vstest.console.exe x64\Release\XISFPropertyHandlerTests.dll
```

## Running the Test Suite

### From Visual Studio (Recommended)

1. **Open Test Explorer:** View → Test Explorer (or Ctrl+E, T)
2. **Run all tests:** Click "Run All" icon
3. **Filter tests:** Search box filters by test name
4. **Debug individual test:** Right-click → Debug

**Test Explorer UI shows:**
- Test status (passed ✓, failed ✗)
- Execution time per test
- Output and assertion details

### From Command Line

**Run all tests:**
```powershell
vstest.console.exe `
  x64\Release\XISFPropertyHandlerTests.dll `
  x64\Release\XISFPreviewHandlerTests.dll `
  x64\Release\XISFFilterTests.dll
```

**Run specific test:**
```powershell
vstest.console.exe x64\Release\XISFPropertyHandlerTests.dll `
  /Tests:XISFParserTests::ParseValidXISFFile
```

**Run with code coverage:**
```powershell
vstest.console.exe x64\Release\XISFPropertyHandlerTests.dll `
  /Settings:CodeCoverage.runsettings `
  /ResultsDirectory:TestResults

# View coverage report
start TestResults\...coverage
```

**Run with logging:**
```powershell
vstest.console.exe x64\Release\XISFPropertyHandlerTests.dll `
  /Logger:trx `
  /ResultsDirectory:TestResults
```

### In CI (GitHub Actions)

**Pipeline:** `.github\workflows\ci.yml`

**Steps:**
1. Setup MSBuild + .NET SDK
2. Download test catalogs (NGC.csv, addendum.csv with SHA-256 verification)
3. Build solution
4. Run tests via VSTest
5. Publish test results
6. Generate code coverage

**View CI status:** [Actions tab](https://github.com/dennispayne/XISF-Shell-Extensions/actions)

## Code Coverage

### Coverage Goals

- **Target:** >80% coverage for core parsing and property extraction
- **Exceptions:** UI rendering code (harder to test), error paths (edge cases)
- **Not tested:** Legacy code path not yet refactored

### Generate Coverage Report

```powershell
# Build tests
msbuild PropertyHandler\XISFPropertyHandlerTests\XISFPropertyHandlerTests.vcxproj `
  /p:Configuration=Release /p:Platform=x64

# Run with coverage
vstest.console.exe x64\Release\XISFPropertyHandlerTests.dll `
  /Settings:CodeCoverage.runsettings `
  /ResultsDirectory:TestResults

# Analyze results (Visual Studio)
start TestResults\*\In\...coverage
```

**Coverage.runsettings** filters:
- Includes: `XISFPropertyHandler\src\*.cpp`
- Excludes: Test code, dllmain.cpp (entry point only)

## Writing New Tests

### Test Template

```cpp
// PropertyHandler/XISFPropertyHandlerTests/NewTests.cpp
#include "CppUnitTest.h"
#include "../XISFPropertyHandler/src/SomeComponent.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

TEST_CLASS(MyNewTests) {
    TEST_METHOD(TestSomething) {
        // Arrange
        MyComponent component;
        int expected = 42;
        
        // Act
        int result = component.DoSomething();
        
        // Assert
        Assert::AreEqual(expected, result);
    }
    
    TEST_METHOD(TestErrorCondition) {
        MyComponent component;
        
        // Should not throw, should return error code
        HRESULT hr = component.FailGracefully();
        Assert::IsTrue(FAILED(hr));
    }
};
```

### Test Best Practices

1. **One assertion per test** (when possible) — Easier to debug failures
2. **Arrange-Act-Assert** — Clear test structure
3. **Use descriptive names** — `TestInvalidXISFFileReturnsFileNotFound` > `TestError`
4. **Test edge cases** — Empty input, max sizes, invalid data
5. **Use fixtures for setup** — Avoid test interdependencies
6. **Isolate from file system** — Mock IStream for handlers; use temp directory for integration tests

### Mocking COM Objects

Example: Mock IStream for handler testing

```cpp
class MockStream : public IStream {
    STDMETHOD(Read)(void *pv, ULONG cb, ULONG *pcbRead) override {
        // Simulate reading from buffer
        memcpy(pv, m_buffer.data() + m_pos, cb);
        m_pos += cb;
        if (pcbRead) *pcbRead = cb;
        return S_OK;
    }
    // ... implement other IStream methods
private:
    std::vector<uint8_t> m_buffer;
    size_t m_pos = 0;
};
```

## Debugging Failed Tests

### Common Failures

| Error | Cause | Fix |
|-------|-------|-----|
| `Assert::AreEqual failed` | Value mismatch | Print actual vs. expected in test output |
| `HRESULT: 0x80070002` | FILE_NOT_FOUND | Check test fixture file path |
| `Memory leak: N bytes` | Unreleased COM objects | Ensure all `AddRef()` calls have matching `Release()` |
| `Test hangs/times out` | Infinite loop or deadlock | Check thread synchronization; use timeout in VSTest |
| `Catalog not found` | LOCALAPPDATA missing | Create `%LOCALAPPDATA%\XISFShellExtension\catalogs\` or mock |

### Test Debugging Tips

1. **Add diagnostics:** `Assert::AreEqual(expected, actual, L"Custom message")`
2. **Set breakpoints in test:** F5 to debug individual test
3. **View test output:** Test Explorer → Output pane shows detailed logs
4. **Use ETW tracing:** PropertyHandler tests include telemetry hooks (see [Debugging](debugging.md))
5. **Reduce test scope:** Isolate one component at a time

---

Related: [Building](building.md), [Debugging](debugging.md), [Contributing](contributing.md)
