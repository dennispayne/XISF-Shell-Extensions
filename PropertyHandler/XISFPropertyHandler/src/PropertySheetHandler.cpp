// PropertySheetHandler.cpp — IShellPropSheetExt implementation for XISF histogram tab
#include "PropertySheetHandler.h"
#include "PropertyHandlerTraceLogging.h"
#include "XISFParser.h"
#include <shlwapi.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comctl32.lib")

extern HINSTANCE g_hInst;
extern long g_cDllRef;

// ---------------------------------------------------------------------------
// In-memory dialog template — an empty rectangle we paint into
// ---------------------------------------------------------------------------
#pragma pack(push, 4)
static struct {
    DLGTEMPLATE tmpl;
    WORD menu;
    WORD cls;
    WORD title;
} s_dlgTemplate = {
    { WS_CHILD | WS_VISIBLE | DS_CONTROL, 0, 0, 0, 0, 300, 200 },
    0, 0, 0
};
#pragma pack(pop)

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

CXISFPropertySheetHandler::CXISFPropertySheetHandler()
    : m_cRef(1), m_hwndPage(nullptr), m_computed(false), m_unavailable(false)
{
    InterlockedIncrement(&g_cDllRef);
}

CXISFPropertySheetHandler::~CXISFPropertySheetHandler()
{
    if (m_computeThread.joinable())
        m_computeThread.join();
    InterlockedDecrement(&g_cDllRef);
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------

IFACEMETHODIMP CXISFPropertySheetHandler::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;

    if (IsEqualIID(riid, IID_IUnknown))
    {
        *ppv = static_cast<IShellExtInit*>(this);
    }
    else if (IsEqualIID(riid, IID_IShellExtInit))
    {
        *ppv = static_cast<IShellExtInit*>(this);
    }
    else if (IsEqualIID(riid, IID_IShellPropSheetExt))
    {
        *ppv = static_cast<IShellPropSheetExt*>(this);
    }
    else
    {
        *ppv = nullptr;
        return E_NOINTERFACE;
    }

    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) CXISFPropertySheetHandler::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

IFACEMETHODIMP_(ULONG) CXISFPropertySheetHandler::Release()
{
    ULONG cRef = InterlockedDecrement(&m_cRef);
    if (cRef == 0)
        delete this;
    return cRef;
}

// ---------------------------------------------------------------------------
// IShellExtInit::Initialize
// ---------------------------------------------------------------------------

IFACEMETHODIMP CXISFPropertySheetHandler::Initialize(
    PCIDLIST_ABSOLUTE /*pidlFolder*/, IDataObject* pdtobj, HKEY /*hkeyProgID*/)
{
    if (!pdtobj) return E_INVALIDARG;

    FORMATETC fe = { CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
    STGMEDIUM stm = {};
    HRESULT hr = pdtobj->GetData(&fe, &stm);
    if (FAILED(hr)) return hr;

    HDROP hDrop = static_cast<HDROP>(GlobalLock(stm.hGlobal));
    if (!hDrop)
    {
        ReleaseStgMedium(&stm);
        return E_FAIL;
    }

    UINT fileCount = DragQueryFileW(hDrop, 0xFFFFFFFF, nullptr, 0);
    if (fileCount == 0)
    {
        GlobalUnlock(stm.hGlobal);
        ReleaseStgMedium(&stm);
        return E_FAIL;
    }

    wchar_t szPath[MAX_PATH] = {};
    if (DragQueryFileW(hDrop, 0, szPath, ARRAYSIZE(szPath)) == 0)
    {
        GlobalUnlock(stm.hGlobal);
        ReleaseStgMedium(&stm);
        return E_FAIL;
    }

    GlobalUnlock(stm.hGlobal);
    ReleaseStgMedium(&stm);

    // Only handle .xisf files
    const wchar_t* ext = PathFindExtensionW(szPath);
    if (!ext || _wcsicmp(ext, L".xisf") != 0)
        return E_FAIL;

    m_filePath = szPath;
    return S_OK;
}

// ---------------------------------------------------------------------------
// IShellPropSheetExt
// ---------------------------------------------------------------------------

IFACEMETHODIMP CXISFPropertySheetHandler::AddPages(
    LPFNSVADDPROPSHEETPAGE pfnAddPage, LPARAM lParam)
{
    PROPSHEETPAGEW psp = {};
    psp.dwSize    = sizeof(psp);
    psp.dwFlags   = PSP_USETITLE | PSP_DLGINDIRECT;
    psp.hInstance  = g_hInst;
    psp.pResource  = &s_dlgTemplate.tmpl;
    psp.pszTitle   = L"Histogram";
    psp.pfnDlgProc = HistogramDlgProc;
    psp.lParam     = reinterpret_cast<LPARAM>(this);

    HPROPSHEETPAGE hPage = CreatePropertySheetPageW(&psp);
    if (hPage)
    {
        if (!pfnAddPage(hPage, lParam))
        {
            DestroyPropertySheetPage(hPage);
        }
        else
        {
            AddRef(); // prevent premature Release while page is alive
        }
    }
    return S_OK;
}

IFACEMETHODIMP CXISFPropertySheetHandler::ReplacePage(
    EXPPS /*uPageID*/, LPFNSVADDPROPSHEETPAGE /*pfnReplacePage*/, LPARAM /*lParam*/)
{
    return E_NOTIMPL;
}

// ---------------------------------------------------------------------------
// Dialog proc
// ---------------------------------------------------------------------------

INT_PTR CALLBACK CXISFPropertySheetHandler::HistogramDlgProc(
    HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    CXISFPropertySheetHandler* pThis = reinterpret_cast<CXISFPropertySheetHandler*>(
        GetWindowLongPtr(hwnd, DWLP_USER));

    switch (msg)
    {
    case WM_INITDIALOG:
    {
        auto* ppsp = reinterpret_cast<PROPSHEETPAGEW*>(lParam);
        pThis = reinterpret_cast<CXISFPropertySheetHandler*>(ppsp->lParam);
        SetWindowLongPtr(hwnd, DWLP_USER, reinterpret_cast<LONG_PTR>(pThis));

        if (pThis)
        {
            pThis->m_hwndPage = hwnd;
            // Start background computation
            pThis->m_computeThread = std::thread([pThis]() {
                pThis->ComputeHistogram();
                if (IsWindow(pThis->m_hwndPage))
                    InvalidateRect(pThis->m_hwndPage, nullptr, TRUE);
            });
        }
        return TRUE;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        if (pThis && pThis->IsComputed())
        {
            PaintHistogram(hdc, rc, pThis->GetHistogram());
        }
        else
        {
            HBRUSH hBg = CreateSolidBrush(RGB(25, 25, 40));
            FillRect(hdc, &rc, hBg);
            DeleteObject(hBg);

            SetTextColor(hdc, RGB(200, 200, 220));
            SetBkMode(hdc, TRANSPARENT);

            const wchar_t* text = (pThis && pThis->IsUnavailable())
                ? L"Histogram unavailable"
                : L"Computing histogram...";
            DrawTextW(hdc, text, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }

        EndPaint(hwnd, &ps);
        return TRUE;
    }

    case WM_DESTROY:
        if (pThis)
        {
            if (pThis->m_computeThread.joinable())
                pThis->m_computeThread.join();
            pThis->Release(); // match AddRef in AddPages
        }
        return TRUE;
    }

    return FALSE;
}

// ---------------------------------------------------------------------------
// PaintHistogram — GDI log-scale histogram rendering
// (Ported from PreviewHandler)
// ---------------------------------------------------------------------------

void CXISFPropertySheetHandler::PaintHistogram(
    HDC hdc, const RECT& rcArea, const HistogramData& hist)
{
    // Background fill
    HBRUSH hBgBrush = CreateSolidBrush(RGB(25, 25, 40));
    FillRect(hdc, &rcArea, hBgBrush);
    DeleteObject(hBgBrush);

    // Subtle border
    HPEN hBorderPen = CreatePen(PS_SOLID, 1, RGB(60, 60, 90));
    HGDIOBJ hOldPen = SelectObject(hdc, hBorderPen);
    HGDIOBJ hOldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rcArea.left, rcArea.top, rcArea.right, rcArea.bottom);
    SelectObject(hdc, hOldBrush);
    SelectObject(hdc, hOldPen);
    DeleteObject(hBorderPen);

    // Inner plot area with padding
    const int pad = 6;
    int plotLeft   = rcArea.left   + pad;
    int plotTop    = rcArea.top    + pad;
    int plotRight  = rcArea.right  - pad;
    int plotBottom = rcArea.bottom - pad;
    int plotW = plotRight - plotLeft;
    int plotH = plotBottom - plotTop;
    if (plotW < 16 || plotH < 16) return;

    // Axes frame in light gray
    HPEN hAxisPen = CreatePen(PS_SOLID, 1, RGB(100, 100, 120));
    hOldPen = SelectObject(hdc, hAxisPen);
    MoveToEx(hdc, plotLeft, plotBottom, nullptr);
    LineTo(hdc, plotRight, plotBottom);
    MoveToEx(hdc, plotLeft, plotTop, nullptr);
    LineTo(hdc, plotLeft, plotBottom);
    SelectObject(hdc, hOldPen);
    DeleteObject(hAxisPen);

    // Compute log-scale max across all channels
    double logMax = 0.0;
    for (uint32_t ch = 0; ch < hist.channelCount; ++ch)
    {
        for (uint32_t b = 0; b < HistogramData::kBinCount; ++b)
        {
            double v = std::log(1.0 + hist.bins[ch][b]);
            if (v > logMax) logMax = v;
        }
    }
    if (logMax <= 0.0) return;

    // Channel colours — draw back-to-front for RGB overlap
    struct ChannelStyle { uint32_t idx; COLORREF color; };
    ChannelStyle styles[3]{};
    int nStyles = 0;

    if (hist.channelCount == 1)
    {
        styles[0] = { 0, RGB(200, 200, 220) };
        nStyles = 1;
    }
    else
    {
        styles[0] = { 2, RGB(60, 80, 220) };   // Blue behind
        styles[1] = { 1, RGB(40, 200, 80) };   // Green middle
        styles[2] = { 0, RGB(220, 50, 50) };   // Red front
        nStyles = 3;
    }

    // Draw each channel as filled vertical bars
    int drawLeft = plotLeft + 1;
    int drawW    = plotRight - drawLeft;

    for (int s = 0; s < nStyles; ++s)
    {
        uint32_t ch = styles[s].idx;
        HPEN hChPen = CreatePen(PS_SOLID, 1, styles[s].color);
        hOldPen = SelectObject(hdc, hChPen);

        for (int i = 0; i < static_cast<int>(HistogramData::kBinCount); ++i)
        {
            double v = std::log(1.0 + hist.bins[ch][i]);
            int barH = static_cast<int>(v / logMax * (plotH - 1));
            if (barH < 1) continue;

            int x0 = drawLeft + MulDiv(i,     drawW, HistogramData::kBinCount);
            int x1 = drawLeft + MulDiv(i + 1, drawW, HistogramData::kBinCount);

            for (int px = x0; px < x1; ++px)
            {
                MoveToEx(hdc, px, plotBottom - 1, nullptr);
                LineTo(hdc, px, plotBottom - 1 - barH);
            }
        }

        SelectObject(hdc, hOldPen);
        DeleteObject(hChPen);
    }
}

// ---------------------------------------------------------------------------
// ComputeHistogram — background thread: parse XISF and bin pixel samples
// ---------------------------------------------------------------------------

void CXISFPropertySheetHandler::ComputeHistogram()
{
    // Open file via SHCreateStreamOnFileEx
    IStream* pStream = nullptr;
    HRESULT hr = SHCreateStreamOnFileEx(
        m_filePath.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
        FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &pStream);
    if (FAILED(hr) || !pStream)
    {
        m_unavailable.store(true, std::memory_order_release);
        return;
    }

    // Parse XISF XML header
    // Read the 16-byte XISF preamble to get XML header offset and size
    LARGE_INTEGER seekZero = {};
    pStream->Seek(seekZero, STREAM_SEEK_SET, nullptr);

    uint8_t preamble[16] = {};
    ULONG cbRead = 0;
    hr = pStream->Read(preamble, 16, &cbRead);
    if (FAILED(hr) || cbRead < 16)
    {
        pStream->Release();
        m_unavailable.store(true, std::memory_order_release);
        return;
    }

    // Validate XISF signature: "XISF0100"
    if (std::memcmp(preamble, "XISF0100", 8) != 0)
    {
        pStream->Release();
        m_unavailable.store(true, std::memory_order_release);
        return;
    }

    // Bytes 8-11: XML header length (little-endian uint32)
    uint32_t xmlLen = *reinterpret_cast<const uint32_t*>(preamble + 8);
    if (xmlLen == 0 || xmlLen > xisf::XISFParser::kMaxHeaderBytes)
    {
        pStream->Release();
        m_unavailable.store(true, std::memory_order_release);
        return;
    }

    // Read XML header
    std::string xml(xmlLen, '\0');
    hr = pStream->Read(xml.data(), xmlLen, &cbRead);
    if (FAILED(hr) || cbRead < xmlLen)
    {
        pStream->Release();
        m_unavailable.store(true, std::memory_order_release);
        return;
    }

    // Local attribute parser (same pattern as PropertyStore.cpp)
    auto findAttr = [](const std::string& elemText, const std::string& attr) -> std::string {
        size_t p = 0;
        while (p < elemText.size()) {
            size_t ap = elemText.find(attr, p);
            if (ap == std::string::npos) break;
            if (ap > 0) {
                char prev = elemText[ap - 1];
                if (prev != ' ' && prev != '\t' && prev != '\n' && prev != '\r')
                { p = ap + attr.size(); continue; }
            }
            size_t eq = ap + attr.size();
            while (eq < elemText.size() && (elemText[eq]==' '||elemText[eq]=='\t')) ++eq;
            if (eq >= elemText.size() || elemText[eq] != '=') { p = eq; continue; }
            ++eq;
            while (eq < elemText.size() && (elemText[eq]==' '||elemText[eq]=='\t')) ++eq;
            if (eq >= elemText.size()) break;
            char q = elemText[eq]; if (q!='"' && q!='\'') { p=eq; continue; }
            ++eq;
            size_t end = elemText.find(q, eq);
            if (end == std::string::npos) break;
            return elemText.substr(eq, end - eq);
        }
        return {};
    };

    // Scan for Image elements with attachments
    struct ImageCandidate {
        std::string elemText;
        ULONGLONG attachSize;
        bool isThumbnail;
    };
    std::vector<ImageCandidate> candidates;

    size_t searchPos = 0;
    while (true) {
        size_t imgStart = xml.find("<Image", searchPos);
        if (imgStart == std::string::npos) break;
        size_t imgEnd = xml.find('>', imgStart);
        if (imgEnd == std::string::npos) break;
        searchPos = imgEnd + 1;

        std::string elem = xml.substr(imgStart + 6, imgEnd - imgStart - 6);
        std::string loc = findAttr(elem, "location");
        if (loc.compare(0, 11, "attachment:") != 0) continue;

        std::string id = findAttr(elem, "id");
        bool isThumb = (id == "thumbnail" || id == "Thumbnail");

        ULONGLONG aSize = 0;
        {
            const char* lp = loc.c_str() + 11;
            char* ep = nullptr;
            std::strtoull(lp, &ep, 10);
            if (ep && *ep == ':')
                aSize = std::strtoull(ep + 1, nullptr, 10);
        }
        if (aSize == 0) continue;
        candidates.push_back({std::move(elem), aSize, isThumb});
    }

    if (candidates.empty()) {
        pStream->Release();
        m_unavailable.store(true, std::memory_order_release);
        return;
    }

    // Pick largest non-thumbnail image
    size_t bestIdx = 0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].isThumbnail) continue;
        if (candidates[i].attachSize > candidates[bestIdx].attachSize || candidates[bestIdx].isThumbnail)
            bestIdx = i;
    }
    const std::string& imgElem = candidates[bestIdx].elemText;

    std::string geometry  = findAttr(imgElem, "geometry");
    std::string location  = findAttr(imgElem, "location");
    std::string sampleFmt = findAttr(imgElem, "sampleFormat");

    if (geometry.empty() || location.empty()) {
        pStream->Release();
        m_unavailable.store(true, std::memory_order_release);
        return;
    }

    // Parse geometry
    UINT imgW = 0, imgH = 0, imgC = 1;
    {
        char* ep = nullptr;
        imgW = static_cast<UINT>(std::strtoul(geometry.c_str(), &ep, 10));
        if (ep && *ep == ':') {
            imgH = static_cast<UINT>(std::strtoul(ep + 1, &ep, 10));
            if (ep && *ep == ':')
                imgC = static_cast<UINT>(std::strtoul(ep + 1, nullptr, 10));
        }
    }
    if (imgW == 0 || imgH == 0) {
        pStream->Release();
        m_unavailable.store(true, std::memory_order_release);
        return;
    }

    // Parse location offset/size
    ULONGLONG offset = 0, attachSize = 0;
    if (location.compare(0, 11, "attachment:") == 0) {
        const char* lp = location.c_str() + 11;
        char* ep = nullptr;
        offset = std::strtoull(lp, &ep, 10);
        if (ep && *ep == ':')
            attachSize = std::strtoull(ep + 1, nullptr, 10);
    }
    if (attachSize == 0) {
        pStream->Release();
        m_unavailable.store(true, std::memory_order_release);
        return;
    }

    // Sample format
    bool isUInt16  = (sampleFmt == "UInt16" || sampleFmt.empty());
    bool isUInt8   = (sampleFmt == "UInt8");
    bool isFloat32 = (sampleFmt == "Float32");
    bool isFloat64 = (sampleFmt == "Float64");
    size_t bps = isUInt8 ? 1 : isFloat32 ? 4 : isFloat64 ? 8 : 2;

    UINT readChannels = (imgC >= 3) ? 3 : 1;
    size_t channelPixels = static_cast<size_t>(imgW) * imgH;

    // Subsample strategy: target ~1024 rows, cap total samples at ~1M
    constexpr size_t kMaxTotalSamples = 1024 * 1024;

    UINT sampleRows = (std::min)(imgH, 1024u);
    UINT rowStride  = (std::max)(1u, imgH / sampleRows);

    UINT sampleCols = imgW;
    UINT colStride  = 1;
    size_t maxSamples = static_cast<size_t>(sampleCols) * sampleRows * readChannels;
    if (maxSamples > kMaxTotalSamples) {
        UINT targetCols = static_cast<UINT>(kMaxTotalSamples / (static_cast<size_t>(sampleRows) * readChannels));
        sampleCols = (std::max)(1u, (std::min)(imgW, targetCols));
        colStride  = (std::max)(1u, imgW / sampleCols);
    }

    size_t rowBytes = static_cast<size_t>(imgW) * bps;
    std::vector<uint8_t> rowBuf(rowBytes);

    m_histogram.Begin(readChannels);

    for (UINT ch = 0; ch < readChannels; ++ch) {
        ULONGLONG chBase = offset + static_cast<ULONGLONG>(ch) * channelPixels * bps;

        for (UINT row = 0; row < imgH; row += rowStride) {
            LARGE_INTEGER seekPos;
            seekPos.QuadPart = static_cast<LONGLONG>(chBase + static_cast<ULONGLONG>(row) * rowBytes);
            if (FAILED(pStream->Seek(seekPos, STREAM_SEEK_SET, nullptr))) {
                pStream->Release();
                m_unavailable.store(true, std::memory_order_release);
                return;
            }

            ULONG cbRowRead = 0;
            if (FAILED(pStream->Read(rowBuf.data(), static_cast<ULONG>(rowBytes), &cbRowRead))
                || cbRowRead < rowBytes) {
                pStream->Release();
                m_unavailable.store(true, std::memory_order_release);
                return;
            }

            for (UINT col = 0; col < imgW; col += colStride) {
                float val = 0.0f;
                if (isUInt16) {
                    uint16_t raw = *reinterpret_cast<const uint16_t*>(rowBuf.data() + col * 2);
                    val = static_cast<float>(raw) / 65535.0f;
                } else if (isUInt8) {
                    val = static_cast<float>(rowBuf[col]) / 255.0f;
                } else if (isFloat32) {
                    val = *reinterpret_cast<const float*>(rowBuf.data() + col * 4);
                    val = (std::max)(0.0f, (std::min)(1.0f, val));
                } else if (isFloat64) {
                    double d = *reinterpret_cast<const double*>(rowBuf.data() + col * 8);
                    val = static_cast<float>((std::max)(0.0, (std::min)(1.0, d)));
                }

                uint8_t binIdx = static_cast<uint8_t>((std::min)(255.0f, val * 256.0f));
                m_histogram.bins[ch][binIdx]++;
            }
        }
    }

    m_histogram.Commit();
    m_computed.store(true, std::memory_order_release);

    pStream->Release();
}
