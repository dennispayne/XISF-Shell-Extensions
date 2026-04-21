# Property Handler — Full Property Mapping + DSO Alias Database

## Objective

Expand the Phase 3 handler to expose the complete set of meaningful XISF/FITS metadata as Windows properties, and add a JSON-backed Deep-Sky Object (DSO) alias database that resolves common object name variants to canonical names.

---

## Installing nlohmann/json via vcpkg

```powershell
cd C:\vcpkg
.\vcpkg install nlohmann-json:x64-windows
```

nlohmann/json is a header-only library, so no `.lib` file is needed. After vcpkg integration, include it as:

```cpp
#include <nlohmann/json.hpp>
using json = nlohmann::json;
```

### Loading the DSO Alias Database

```cpp
std::ifstream f("dso_aliases.json");
json db = json::parse(f);

// Look up a name
std::string canonical = db.value("M42", "M42");  // default to input if not found
```

### Sample dso_aliases.json Structure

```json
{
  "M42":          "Orion Nebula",
  "NGC 1976":     "Orion Nebula",
  "Orion Nebula": "Orion Nebula",
  "M31":          "Andromeda Galaxy",
  "NGC 224":      "Andromeda Galaxy",
  "M45":          "Pleiades",
  "Seven Sisters": "Pleiades",
  "IC 434":       "Horsehead Nebula",
  "Barnard 33":   "Horsehead Nebula"
}
```

---

## Windows Canonical Properties

These properties from `<propkey.h>` map naturally to XISF/FITS metadata:

| PROPERTYKEY Constant | VARTYPE | FITS Keyword / XISF Property | Notes |
|----------------------|---------|------------------------------|-------|
| `PKEY_Title` | `VT_LPWSTR` | `OBJECT` | Primary target name |
| `PKEY_Subject` | `VT_LPWSTR` | `OBJECT` | Duplicate for compatibility |
| `PKEY_Comment` | `VT_LPWSTR` | `COMMENT` | Free-text comment |
| `PKEY_Keywords` | `VT_VECTOR\|VT_LPWSTR` | `FILTER` | Multi-value; add filter name |
| `PKEY_Author` | `VT_VECTOR\|VT_LPWSTR` | `OBSERVER` | Multi-value author list |
| `PKEY_DateCreated` | `VT_FILETIME` | `DATE-OBS` | Observation date/time |
| `PKEY_Media_Duration` | `VT_UI8` | `EXPTIME` | In 100-nanosecond units |
| `PKEY_GPS_Latitude` | `VT_R8` | `SITELAT` | Observer latitude |
| `PKEY_GPS_Longitude` | `VT_R8` | `SITELONG` | Observer longitude |
| `PKEY_GPS_Altitude` | `VT_R8` | `SITEELEV` | Observer elevation (metres) |
| `PKEY_Image_HorizontalSize` | `VT_UI4` | image width | From `geometry` attribute |
| `PKEY_Image_VerticalSize` | `VT_UI4` | image height | From `geometry` attribute |

Link against `propsys.lib` and include `<propkey.h>`.

### Setting Canonical Properties

```cpp
// PKEY_Title (VT_LPWSTR)
PROPVARIANT pv{};
pv.vt = VT_LPWSTR;
pv.pwszVal = /* CoTaskMemAlloc wide string */;
store->SetValue(PKEY_Title, pv);
PropVariantClear(&pv);

// PKEY_DateCreated (VT_FILETIME)
// Convert ISO-8601 string to SYSTEMTIME then to FILETIME:
SYSTEMTIME st{};
// ... parse "2024-03-15T22:30:00" into st ...
FILETIME ft{};
SystemTimeToFileTime(&st, &ft);
PropVariantInit(&pv);
pv.vt       = VT_FILETIME;
pv.filetime = ft;
store->SetValue(PKEY_DateCreated, pv);
PropVariantClear(&pv);
```

---

## Custom Multi-Value Property Patterns

For properties not covered by the canonical set, define custom properties in your `.propdesc` schema and use `VT_VECTOR | VT_LPWSTR`.

### Example: Multi-Value Keywords

```cpp
// Build a CALPWSTR (counted array of wide strings)
std::vector<std::wstring> keywords = {L"Ha", L"narrowband", L"nebula"};

PROPVARIANT pv{};
pv.vt               = VT_VECTOR | VT_LPWSTR;
pv.calpwstr.cElems  = static_cast<ULONG>(keywords.size());
pv.calpwstr.pElems  = static_cast<LPWSTR*>(
    CoTaskMemAlloc(keywords.size() * sizeof(LPWSTR)));

for (size_t i = 0; i < keywords.size(); ++i) {
    size_t len = keywords[i].size() + 1;
    pv.calpwstr.pElems[i] = static_cast<LPWSTR>(
        CoTaskMemAlloc(len * sizeof(wchar_t)));
    wcscpy_s(pv.calpwstr.pElems[i], len, keywords[i].c_str());
}

store->SetValue(PKEY_Keywords, pv);
PropVariantClear(&pv);   // frees all CoTaskMemAlloc'd memory
```

> `PropVariantClear` recursively frees all elements of a vector PROPVARIANT.

---

## Adding Multiple IPropertyStore Values

### Recommended Pattern: Populate Once on Initialize

Populate a `std::map<PROPERTYKEY, PROPVARIANT>` when `IInitializeWithFile::Initialize` is called, then answer `GetCount`, `GetAt`, and `GetValue` from that map:

```cpp
HRESULT XISFPropertyHandler::Initialize(LPCWSTR pszFilePath, DWORD grfMode) {
    // 1. Parse the XISF file
    auto result = XISFParser::ParseFile(WideToUtf8(pszFilePath));
    if (!result.ok()) return E_FAIL;

    // 2. Build normalised metadata
    auto meta = XISFMetadataReader::Read(result.metadata);

    // 3. Map to Windows properties
    m_properties = PropertyMapper::Map(meta, m_dsoAliasDb);

    // 4. Build ordered key list for GetAt
    for (auto& [key, _] : m_properties)
        m_keys.push_back(key);

    return S_OK;
}

HRESULT XISFPropertyHandler::GetCount(DWORD* cProps) {
    *cProps = static_cast<DWORD>(m_keys.size());
    return S_OK;
}

HRESULT XISFPropertyHandler::GetAt(DWORD iProp, PROPERTYKEY* pkey) {
    if (iProp >= m_keys.size()) return E_INVALIDARG;
    *pkey = m_keys[iProp];
    return S_OK;
}

HRESULT XISFPropertyHandler::GetValue(REFPROPERTYKEY key, PROPVARIANT* pv) {
    auto it = m_properties.find(key);
    if (it == m_properties.end()) {
        PropVariantInit(pv);
        return S_OK;  // not found → VT_EMPTY is correct behaviour
    }
    return PropVariantCopy(pv, &it->second);
}
```

### PROPERTYKEY Comparison for std::map

`PROPERTYKEY` contains a `GUID` and a `DWORD`. Provide a comparator:

```cpp
struct PropertyKeyLess {
    bool operator()(const PROPERTYKEY& a, const PROPERTYKEY& b) const noexcept {
        int cmp = std::memcmp(&a.fmtid, &b.fmtid, sizeof(GUID));
        if (cmp != 0) return cmp < 0;
        return a.pid < b.pid;
    }
};

using PropertyMap = std::map<PROPERTYKEY, PROPVARIANT, PropertyKeyLess>;
```

---

## Full FITS→Windows Property Mapping Reference

| FITS Keyword | XISF Property id | Windows Property | VARTYPE |
|-------------|-----------------|-----------------|---------|
| `OBJECT` | `Observation:Object:Name` | `PKEY_Title` | `VT_LPWSTR` |
| `OBSERVER` | `Observer:Name` | `PKEY_Author` | `VT_VECTOR\|VT_LPWSTR` |
| `DATE-OBS` | `Observation:Time:Start` | `PKEY_DateCreated` | `VT_FILETIME` |
| `EXPTIME` | `Instrument:ExposureTime` | `PKEY_Media_Duration` | `VT_UI8` |
| `FILTER` | `Instrument:Filter` | `PKEY_Keywords` | `VT_VECTOR\|VT_LPWSTR` |
| `TELESCOP` | `Instrument:Telescope:Name` | custom | `VT_LPWSTR` |
| `INSTRUME` | `Instrument:Camera:Name` | custom | `VT_LPWSTR` |
| `FOCALLEN` | `Instrument:Telescope:FocalLength` | custom | `VT_R8` |
| `SITELAT` | `Observation:Location:Latitude` | `PKEY_GPS_Latitude` | `VT_R8` |
| `SITELONG` | `Observation:Location:Longitude` | `PKEY_GPS_Longitude` | `VT_R8` |
| `SITEELEV` | `Observation:Location:Elevation` | `PKEY_GPS_Altitude` | `VT_R8` |
| `RA` | `Observation:Object:RA` | custom | `VT_R8` |
| `DEC` | `Observation:Object:Dec` | custom | `VT_R8` |
| `COMMENT` | — | `PKEY_Comment` | `VT_LPWSTR` |

Prefer XISF `<Property>` values over FITS keywords when both are present, as XISF properties are typed and more reliable.

---

## Project Layout

```
PropertyHandler/
├── XISFPropertyHandler/
│   ├── XISFPropertyHandler.vcxproj
│   ├── dso_aliases.json
│   ├── XISFProperties.propdesc
│   └── src/
│       ├── dllmain.cpp
│       ├── ClassFactory.h / .cpp
│       ├── XISFPropertyHandler.h / .cpp
│       ├── PropertyMapper.h / .cpp      ← expanded full mapping
│       ├── DSOAliasDatabase.h / .cpp    ← JSON alias lookup
│       └── RegistryHelper.h / .cpp
└── XISFPropertyHandlerTests/
```

---

## Build Notes

- **Additional vcpkg package**: `nlohmann-json:x64-windows`
- **Additional linker inputs**: `propsys.lib`, `shlwapi.lib`, `ole32.lib`, `oleaut32.lib`
- The `dso_aliases.json` file must be deployed alongside the DLL (or embedded as a resource with `RC_DATA`).
- For embedding as a resource, add to your `.rc` file:  
  `IDR_DSO_ALIASES RCDATA "dso_aliases.json"`
