# Preview Handler — Thumbnail Provider + Preview Pane

**Note:** This file has been migrated to the new documentation structure. Please see [Preview Handler Deep Dive](features/preview-handler-deep-dive.md) for the updated version.

---

## Objective

Add visual representations of XISF images to Windows Explorer: a thumbnail icon visible in icon views, and a full preview in the Preview pane. Both are additional COM interfaces implemented in the same DLL as the property handler.

---

## XISF Pixel Data Layout

Understanding the pixel data layout is essential before attempting to decode it into a displayable bitmap.

### Image Block Location

The XML `<Image>` element's `location` attribute specifies where the pixel data lives:

```xml
<Image geometry="4144:2822:1"
       sampleFormat="UInt16"
       colorSpace="Gray"
       location="attachment:16777216:23414336">
```

`location="attachment:offset:size"` means:
- **offset**: byte offset from the start of the XISF file
- **size**: byte length of the block

### Sample Formats

| `sampleFormat` | Bytes/sample | C++ type | Notes |
|---------------|-------------|----------|-------|
| `UInt8`       | 1 | `uint8_t` | 0–255 |
| `UInt16`      | 2 | `uint16_t` | 0–65535; most common for raw captures |
| `UInt32`      | 4 | `uint32_t` | |
| `Float32`     | 4 | `float` | 0.0–1.0 normalized |
| `Float64`     | 8 | `double` | 0.0–1.0 normalized |

### Channel Storage Orders

| `colorSpace` | Channels | Storage |
|-------------|----------|---------|
| `Gray`      | 1 | Single plane |
| `RGB`       | 3 | Planar by default: all R, then all G, then all B |
| `RGBA`      | 4 | Planar: R plane, G plane, B plane, A plane |

XISF stores multi-channel images in **planar** order unless the `<ColorFilterArray>` or Bayer attribute indicates otherwise. Each plane is `width × height × bytesPerSample` bytes.

### Reading and Normalising a Pixel

For a UInt16 grayscale image:

```cpp
// plane_data: raw bytes of one plane
uint16_t raw;
std::memcpy(&raw, plane_data + y * width + x, sizeof(raw));
double normalized = raw / 65535.0;   // → [0.0, 1.0]
uint8_t display   = static_cast<uint8_t>(normalized * 255.0);
```

For Float32 already normalized to [0,1]:

```cpp
float raw;
std::memcpy(&raw, plane_data + (y * width + x) * 4, sizeof(raw));
float clamped = std::clamp(raw, 0.0f, 1.0f);
uint8_t display = static_cast<uint8_t>(clamped * 255.0f);
```

---

## IThumbnailProvider Interface

`IThumbnailProvider` is declared in `<thumbcache.h>`:

```cpp
struct IThumbnailProvider : IUnknown {
    virtual HRESULT GetThumbnail(UINT cx, HBITMAP* phbmp,
                                 WTS_ALPHATYPE* pdwAlpha) = 0;
};
```

- `cx` — the requested thumbnail size in pixels (e.g. 256).
- `phbmp` — you return a 32-bpp `HBITMAP` (BGRA or BGR).
- `pdwAlpha` — set to `WTSAT_ARGB` if the bitmap has an alpha channel, `WTSAT_RGB` otherwise.

Your class must also implement `IInitializeWithFile` (or `IInitializeWithStream`).

### Typical Implementation Flow

```cpp
HRESULT GetThumbnail(UINT cx, HBITMAP* phbmp, WTS_ALPHATYPE* pdwAlpha) {
    // 1. Read image dimensions from cached metadata
    int srcW = m_width, srcH = m_height;

    // 2. Determine output size (maintain aspect ratio)
    int outW, outH;
    if (srcW >= srcH) { outW = cx; outH = (srcH * cx) / srcW; }
    else              { outH = cx; outW = (srcW * cx) / srcH; }

    // 3. Read and decode a downscaled version of the pixel data
    auto pixels = DecodePixels(m_filePath, outW, outH); // returns 24-bpp BGR

    // 4. Create HBITMAP
    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = outW;
    bmi.bmiHeader.biHeight      = -outH;  // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HDC hdc  = GetDC(nullptr);
    *phbmp   = CreateDIBSection(hdc, &bmi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!*phbmp) return E_OUTOFMEMORY;

    // copy pixels into bits (expand BGR→BGRA as needed)
    // ...

    *pdwAlpha = WTSAT_RGB;
    return S_OK;
}
```

### Using GDI+ for Scaling

GDI+ (`<gdiplus.h>`) provides bicubic/bilinear resampling which is higher quality than nearest-neighbour:

```cpp
#include <gdiplus.h>
#pragma comment(lib, "gdiplus.lib")

// Initialize GDI+ once (e.g., in DLL_PROCESS_ATTACH):
ULONG_PTR gdiplusToken;
Gdiplus::GdiplusStartupInput si;
Gdiplus::GdiplusStartup(&gdiplusToken, &si, nullptr);

// Create source bitmap from raw pixels:
Gdiplus::Bitmap srcBmp(srcW, srcH, stride, PixelFormat24bppRGB, pixelBytes);

// Scale to thumbnail:
Gdiplus::Bitmap dstBmp(outW, outH, PixelFormat32bppRGB);
Gdiplus::Graphics g(&dstBmp);
g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
g.DrawImage(&srcBmp, 0, 0, outW, outH);

// Extract HBITMAP:
dstBmp.GetHBITMAP(Gdiplus::Color(0,0,0), phbmp);
```

### Using WIC for Scaling

Windows Imaging Component (WIC) is the recommended modern approach:

```cpp
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")

IWICImagingFactory* pFactory = nullptr;
CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                 IID_IWICImagingFactory, (void**)&pFactory);

// Wrap your decoded pixels in a WIC bitmap:
IWICBitmap* pBitmap = nullptr;
pFactory->CreateBitmapFromMemory(srcW, srcH, GUID_WICPixelFormat24bppBGR,
    stride, dataSize, pixelBytes, &pBitmap);

// Scale with IWICBitmapScaler:
IWICBitmapScaler* pScaler = nullptr;
pFactory->CreateBitmapScaler(&pScaler);
pScaler->Initialize(pBitmap, outW, outH,
    WICBitmapInterpolationModeHighQualityCubic);

// Render to HBITMAP via IWICBitmapSource + DIB section...
```

---

## IPreviewHandler Interface

`IPreviewHandler` is declared in `<shobjidl_core.h>`:

```cpp
struct IPreviewHandler : IUnknown {
    virtual HRESULT SetWindow(HWND hwnd, const RECT* prc) = 0;
    virtual HRESULT SetRect(const RECT* prc)              = 0;
    virtual HRESULT DoPreview()                           = 0;
    virtual HRESULT Unload()                              = 0;
    virtual HRESULT SetFocus()                            = 0;
    virtual HRESULT QueryFocus(HWND* phwnd)               = 0;
    virtual HRESULT TranslateAccelerator(MSG* pmsg)       = 0;
};
```

Your handler also implements `IPreviewHandlerVisuals` (optional, for background colour) and `IInitializeWithFile`.

### Typical DoPreview Flow

```cpp
HRESULT DoPreview() {
    // 1. Create a child window filling m_previewRect inside m_hwndParent
    m_hwnd = CreateWindowEx(0, L"STATIC", nullptr,
        WS_CHILD | WS_VISIBLE | SS_OWNERDRAW,
        m_previewRect.left, m_previewRect.top,
        m_previewRect.right  - m_previewRect.left,
        m_previewRect.bottom - m_previewRect.top,
        m_hwndParent, nullptr, g_hInstance, nullptr);

    // 2. Decode a preview-resolution image (e.g. 1024 × 1024 max)
    m_previewBitmap = DecodePreview(m_filePath, 1024);

    // 3. Register a WM_PAINT handler that BitBlts m_previewBitmap
    // ...
    return S_OK;
}
```

---

## Registration Requirements

Both providers need their CLSIDs written to the registry alongside the property handler.

### Thumbnail Provider

```
HKEY_CLASSES_ROOT\CLSID\{THUMBNAIL-CLSID}
  (Default) = "XISF Thumbnail Provider"
  InProcServer32
    (Default) = "C:\path\to\XISFPropertyHandler.dll"
    ThreadingModel = "Apartment"

HKEY_LOCAL_MACHINE\SOFTWARE\Classes\.xisf\shellex
  {E357FCCD-A995-4576-B01F-234630154E96}    ← IThumbnailProvider shell ext GUID
    (Default) = "{THUMBNAIL-CLSID}"
```

### Preview Handler

```
HKEY_CLASSES_ROOT\CLSID\{PREVIEW-CLSID}
  (Default) = "XISF Preview Handler"
  InProcServer32
    (Default) = "C:\path\to\XISFPropertyHandler.dll"
    ThreadingModel = "Apartment"
  AppID = "{6d2b5079-2f0b-48dd-ab7f-97cec514d30b}"  ← host surrogate

HKEY_LOCAL_MACHINE\SOFTWARE\Classes\.xisf\shellex
  {8895B1C6-B41F-4C1C-A562-0D564250836F}    ← IPreviewHandler shell ext GUID
    (Default) = "{PREVIEW-CLSID}"
```

The `AppID` pointing to `{6d2b5079-…}` causes the preview handler to run in a surrogate host process (`prevhost.exe`) for Explorer stability — this is **required** since Windows Vista.

---

## Project Layout

```
PreviewHandler/
├── XISFPreviewHandler/
│   ├── XISFPreviewHandler.vcxproj
│   └── src/
│       ├── dllmain.cpp
│       ├── PreviewHandler.h / .cpp
│       ├── ThumbnailProvider.h / .cpp
│       ├── XISFParser.h / .cpp
│       └── PreviewHandlerTelemetry.h
└── XISFPreviewHandlerTests/
```

---

## Build Notes

- **GDI+**: `#include <gdiplus.h>`, link `gdiplus.lib`. Initialise in `DLL_PROCESS_ATTACH`.
- **WIC**: `#include <wincodec.h>`, link `windowscodecs.lib`. Use `CoCreateInstance(CLSID_WICImagingFactory, …)`.
- **Additional linker inputs for preview/thumbnail support**: `gdiplus.lib` or `windowscodecs.lib`, `user32.lib`, `gdi32.lib`
- Preview handler hosting: add `AppID` registry value under your preview handler CLSID pointing to `{6d2b5079-2f0b-48dd-ab7f-97cec514d30b}` (the system-provided preview host).
- Test the thumbnail by pressing **F5** to refresh Explorer's icon cache, or run `ie4uinit.exe -show` to clear the thumbnail cache.
