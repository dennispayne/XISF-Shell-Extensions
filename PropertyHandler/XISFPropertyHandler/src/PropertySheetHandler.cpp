// PropertySheetHandler.cpp — IShellPropSheetExt implementation for XISF Astro Details tab
#include "PropertySheetHandler.h"
#include "PropertyHandlerTraceLogging.h"
#include "XISFParser.h"
#include <shlwapi.h>
#include <propsys.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <vector>
#include <objidl.h>
#include <gdiplus.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "gdiplus.lib")
#pragma comment(lib, "propsys.lib")

extern HINSTANCE g_hInst;
extern long g_cDllRef;

#define IDC_ANALYZE 1001

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
    : m_cRef(1), m_hwndPage(nullptr), m_hwndButton(nullptr),
      m_computed(false), m_unavailable(false), m_analyzing(false)
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
    psp.pszTitle   = L"Astro Details";
    psp.pfnDlgProc = AstroDetailsDlgProc;
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
// ReadCachedStats — read stats from the Windows property store (indexer cache)
// ---------------------------------------------------------------------------

bool CXISFPropertySheetHandler::ReadCachedStats()
{
    IPropertyStore* pStore = nullptr;
    HRESULT hr = SHGetPropertyStoreFromParsingName(
        m_filePath.c_str(), nullptr, GPS_FASTPROPERTIESONLY, IID_PPV_ARGS(&pStore));
    if (FAILED(hr) || !pStore) return false;

    // XISF property keys: {7C54FA8B-9D63-4C10-8FBE-1A5A0F9A3B2E}
    PROPERTYKEY keyMedian = {{ 0x7C54FA8B,0x9D63,0x4C10,{0x8F,0xBE,0x1A,0x5A,0x0F,0x9A,0x3B,0x2E} }, 56};
    PROPERTYKEY keyMean   = {{ 0x7C54FA8B,0x9D63,0x4C10,{0x8F,0xBE,0x1A,0x5A,0x0F,0x9A,0x3B,0x2E} }, 57};
    PROPERTYKEY keyClipLo = {{ 0x7C54FA8B,0x9D63,0x4C10,{0x8F,0xBE,0x1A,0x5A,0x0F,0x9A,0x3B,0x2E} }, 58};
    PROPERTYKEY keyClipHi = {{ 0x7C54FA8B,0x9D63,0x4C10,{0x8F,0xBE,0x1A,0x5A,0x0F,0x9A,0x3B,0x2E} }, 59};

    PROPVARIANT pv;
    PropVariantInit(&pv);

    bool gotMedian = false;
    hr = pStore->GetValue(keyMedian, &pv);
    if (SUCCEEDED(hr) && pv.vt == VT_R8) {
        m_cachedStats.median = pv.dblVal;
        gotMedian = true;
    }
    PropVariantClear(&pv);

    if (gotMedian) {
        hr = pStore->GetValue(keyMean, &pv);
        if (SUCCEEDED(hr) && pv.vt == VT_R8)
            m_cachedStats.mean = pv.dblVal;
        PropVariantClear(&pv);

        hr = pStore->GetValue(keyClipLo, &pv);
        if (SUCCEEDED(hr) && pv.vt == VT_R8)
            m_cachedStats.clippingLow = pv.dblVal;
        PropVariantClear(&pv);

        hr = pStore->GetValue(keyClipHi, &pv);
        if (SUCCEEDED(hr) && pv.vt == VT_R8)
            m_cachedStats.clippingHigh = pv.dblVal;
        PropVariantClear(&pv);

        m_cachedStats.valid = true;
    }

    pStore->Release();
    return m_cachedStats.valid;
}

// ---------------------------------------------------------------------------
// StartAnalysis — kick off background computation thread
// ---------------------------------------------------------------------------

void CXISFPropertySheetHandler::StartAnalysis()
{
    m_analyzing.store(true, std::memory_order_release);
    if (m_hwndButton) ShowWindow(m_hwndButton, SW_HIDE);
    InvalidateRect(m_hwndPage, nullptr, TRUE);
    m_computeThread = std::thread([this]() {
        ComputeAnalysis();
        if (IsWindow(m_hwndPage))
            InvalidateRect(m_hwndPage, nullptr, TRUE);
    });
}

// ---------------------------------------------------------------------------
// Dialog proc
// ---------------------------------------------------------------------------

INT_PTR CALLBACK CXISFPropertySheetHandler::AstroDetailsDlgProc(
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

            // Create the Analyze button (centered)
            RECT rc;
            GetClientRect(hwnd, &rc);
            int btnW = 120, btnH = 30;
            int btnX = (rc.right - btnW) / 2;
            int btnY = (rc.bottom - btnH) / 2;
            pThis->m_hwndButton = CreateWindowW(L"BUTTON", L"Analyze",
                WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                btnX, btnY, btnW, btnH, hwnd, (HMENU)IDC_ANALYZE, g_hInst, nullptr);
            HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            SendMessage(pThis->m_hwndButton, WM_SETFONT, (WPARAM)hFont, TRUE);

            // Check indexer cache for pre-computed stats
            if (pThis->ReadCachedStats())
            {
                // Cached stats available — hide button and auto-start for histogram
                ShowWindow(pThis->m_hwndButton, SW_HIDE);
                pThis->StartAnalysis();
            }
        }
        return TRUE;
    }

    case WM_COMMAND:
    {
        if (LOWORD(wParam) == IDC_ANALYZE && pThis && !pThis->IsAnalyzing())
        {
            pThis->StartAnalysis();
        }
        return TRUE;
    }

    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT rc;
        GetClientRect(hwnd, &rc);

        const int statsAreaHeight = 55;

        if (pThis && pThis->IsComputed())
        {
            // Full results: paint stats on top, histogram below
            RECT rcStats = rc;
            rcStats.bottom = rcStats.top + statsAreaHeight;
            PaintStats(hdc, rcStats, pThis->GetStats());

            RECT rcHist = rc;
            rcHist.top += statsAreaHeight;
            PaintHistogram(hdc, rcHist, pThis->GetHistogram());
        }
        else if (pThis && pThis->HasCachedStats() && pThis->IsAnalyzing())
        {
            // Cached stats + histogram computing
            RECT rcStats = rc;
            rcStats.bottom = rcStats.top + statsAreaHeight;
            PaintStats(hdc, rcStats, pThis->GetCachedStats());

            RECT rcHist = rc;
            rcHist.top += statsAreaHeight;

            HBRUSH hBg = CreateSolidBrush(RGB(20, 22, 35));
            FillRect(hdc, &rcHist, hBg);
            DeleteObject(hBg);

            HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            HGDIOBJ hOldFont = SelectObject(hdc, hFont);
            SetTextColor(hdc, RGB(150, 155, 175));
            SetBkMode(hdc, TRANSPARENT);
            DrawTextW(hdc, L"Computing histogram\u2026", -1, &rcHist,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, hOldFont);
            DeleteObject(hFont);
        }
        else if (pThis && pThis->IsAnalyzing())
        {
            // Analyzing without cached stats
            HBRUSH hBg = CreateSolidBrush(RGB(20, 22, 35));
            FillRect(hdc, &rc, hBg);
            DeleteObject(hBg);

            HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            HGDIOBJ hOldFont = SelectObject(hdc, hFont);
            SetTextColor(hdc, RGB(150, 155, 175));
            SetBkMode(hdc, TRANSPARENT);
            DrawTextW(hdc, L"Analyzing\u2026", -1, &rc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, hOldFont);
            DeleteObject(hFont);
        }
        else if (pThis && pThis->IsUnavailable())
        {
            HBRUSH hBg = CreateSolidBrush(RGB(20, 22, 35));
            FillRect(hdc, &rc, hBg);
            DeleteObject(hBg);

            HFONT hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                DEFAULT_CHARSET, 0, 0, CLEARTYPE_QUALITY, 0, L"Segoe UI");
            HGDIOBJ hOldFont = SelectObject(hdc, hFont);
            SetTextColor(hdc, RGB(150, 155, 175));
            SetBkMode(hdc, TRANSPARENT);
            DrawTextW(hdc, L"Analysis unavailable", -1, &rc,
                DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            SelectObject(hdc, hOldFont);
            DeleteObject(hFont);
        }
        else
        {
            // Nothing yet — dark background (button is visible)
            HBRUSH hBg = CreateSolidBrush(RGB(20, 22, 35));
            FillRect(hdc, &rc, hBg);
            DeleteObject(hBg);
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
// PaintStats — render pixel statistics in a horizontal row using GDI+
// ---------------------------------------------------------------------------

void CXISFPropertySheetHandler::PaintStats(
    HDC hdc, const RECT& rcArea, const PixelStats& stats)
{
    Gdiplus::GdiplusStartupInput gdipInput;
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, nullptr);

    {
        Gdiplus::Graphics gfx(hdc);
        gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        int areaW = rcArea.right - rcArea.left;
        int areaH = rcArea.bottom - rcArea.top;

        // Background
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 20, 22, 35));
        gfx.FillRectangle(&bgBrush, rcArea.left, rcArea.top, areaW, areaH);

        Gdiplus::Font font(L"Segoe UI", 9.0f);
        Gdiplus::SolidBrush dimBrush(Gdiplus::Color(255, 130, 135, 155));
        Gdiplus::SolidBrush brightBrush(Gdiplus::Color(255, 240, 242, 255));

        // Format stat values
        wchar_t medianStr[32], meanStr[32], clipLoStr[32], clipHiStr[32];
        swprintf_s(medianStr, L"%.4f", stats.median);
        swprintf_s(meanStr,   L"%.4f", stats.mean);
        swprintf_s(clipLoStr, L"%.1f%%", stats.clippingLow);
        swprintf_s(clipHiStr, L"%.1f%%", stats.clippingHigh);

        struct StatItem { const wchar_t* label; const wchar_t* value; };
        StatItem items[] = {
            { L"Median: ",        medianStr },
            { L"Mean: ",          meanStr },
            { L"Clipping Low: ",  clipLoStr },
            { L"Clipping High: ", clipHiStr },
        };

        // Measure total width to center the row
        Gdiplus::StringFormat sfNear;
        sfNear.SetAlignment(Gdiplus::StringAlignmentNear);
        sfNear.SetFormatFlags(Gdiplus::StringFormatFlagsNoWrap);

        float totalW = 0;
        const float spacing = 24.0f;
        struct MeasuredItem { float labelW; float valueW; };
        MeasuredItem measured[4] = {};

        for (int i = 0; i < 4; ++i) {
            Gdiplus::RectF bounds;
            gfx.MeasureString(items[i].label, -1, &font, Gdiplus::PointF(0, 0), &sfNear, &bounds);
            measured[i].labelW = bounds.Width;
            gfx.MeasureString(items[i].value, -1, &font, Gdiplus::PointF(0, 0), &sfNear, &bounds);
            measured[i].valueW = bounds.Width;
            totalW += measured[i].labelW + measured[i].valueW;
        }
        totalW += spacing * 3; // spacing between items

        float x = rcArea.left + (areaW - totalW) / 2.0f;
        float y = rcArea.top + (areaH - 16.0f) / 2.0f;

        for (int i = 0; i < 4; ++i) {
            Gdiplus::PointF ptLabel(x, y);
            gfx.DrawString(items[i].label, -1, &font, ptLabel, &sfNear, &dimBrush);
            x += measured[i].labelW;

            Gdiplus::PointF ptValue(x, y);
            gfx.DrawString(items[i].value, -1, &font, ptValue, &sfNear, &brightBrush);
            x += measured[i].valueW + spacing;
        }
    }

    Gdiplus::GdiplusShutdown(gdipToken);
}

// ---------------------------------------------------------------------------
// PaintHistogram — GDI+ log-scale histogram rendering
// (Ported from PreviewHandler)
// ---------------------------------------------------------------------------

void CXISFPropertySheetHandler::PaintHistogram(
    HDC hdc, const RECT& rcArea, const HistogramData& hist)
{
    // Initialize GDI+
    Gdiplus::GdiplusStartupInput gdipInput;
    ULONG_PTR gdipToken = 0;
    Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, nullptr);

    {
        Gdiplus::Graphics gfx(hdc);
        gfx.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        gfx.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        int areaW = rcArea.right - rcArea.left;
        int areaH = rcArea.bottom - rcArea.top;

        // Background
        Gdiplus::SolidBrush bgBrush(Gdiplus::Color(255, 20, 22, 35));
        gfx.FillRectangle(&bgBrush, rcArea.left, rcArea.top, areaW, areaH);

        // Plot area with margins for labels
        const int marginLeft = 10, marginRight = 14;
        const int marginTop = 28, marginBottom = 24;
        int plotLeft   = rcArea.left + marginLeft;
        int plotTop    = rcArea.top  + marginTop;
        int plotRight  = rcArea.right - marginRight;
        int plotBottom = rcArea.bottom - marginBottom;
        int plotW = plotRight - plotLeft;
        int plotH = plotBottom - plotTop;
        if (plotW < 32 || plotH < 32) { Gdiplus::GdiplusShutdown(gdipToken); return; }

        // Title
        Gdiplus::Font titleFont(L"Segoe UI", 10.0f, Gdiplus::FontStyleBold);
        Gdiplus::SolidBrush textBrush(Gdiplus::Color(255, 200, 205, 220));
        Gdiplus::StringFormat sfCenter;
        sfCenter.SetAlignment(Gdiplus::StringAlignmentCenter);
        Gdiplus::RectF titleRect((float)rcArea.left, (float)rcArea.top + 4.0f,
                                  (float)areaW, (float)marginTop - 4.0f);
        const wchar_t* title = (hist.channelCount == 1) ? L"Luminance Histogram" : L"RGB Histogram";
        gfx.DrawString(title, -1, &titleFont, titleRect, &sfCenter, &textBrush);

        // Plot background (slightly lighter)
        Gdiplus::SolidBrush plotBg(Gdiplus::Color(255, 28, 30, 48));
        gfx.FillRectangle(&plotBg, plotLeft, plotTop, plotW, plotH);

        // Subtle grid lines
        Gdiplus::Pen gridPen(Gdiplus::Color(40, 100, 110, 140), 1.0f);
        for (int g = 1; g <= 3; ++g) {
            int gy = plotBottom - (plotH * g / 4);
            gfx.DrawLine(&gridPen, plotLeft, gy, plotRight, gy);
        }
        for (int g = 1; g <= 3; ++g) {
            int gx = plotLeft + (plotW * g / 4);
            gfx.DrawLine(&gridPen, gx, plotTop, gx, plotBottom);
        }

        // Compute log-scale max across all channels
        double logMax = 0.0;
        for (uint32_t ch = 0; ch < hist.channelCount; ++ch)
            for (uint32_t b = 0; b < HistogramData::kBinCount; ++b) {
                double v = std::log(1.0 + hist.bins[ch][b]);
                if (v > logMax) logMax = v;
            }

        if (logMax > 0.0) {
            // Channel styles
            struct ChStyle { uint32_t idx; Gdiplus::Color lineColor; Gdiplus::Color fillColor; };
            ChStyle styles[3]{};
            int nStyles = 0;

            if (hist.channelCount == 1) {
                styles[0] = { 0, Gdiplus::Color(255, 180, 190, 220),
                                 Gdiplus::Color(60, 140, 160, 220) };
                nStyles = 1;
            } else {
                // Draw back-to-front: Blue, Green, Red
                styles[0] = { 2, Gdiplus::Color(220, 50, 80, 230),
                                 Gdiplus::Color(50, 40, 70, 220) };
                styles[1] = { 1, Gdiplus::Color(220, 30, 200, 80),
                                 Gdiplus::Color(50, 30, 190, 70) };
                styles[2] = { 0, Gdiplus::Color(220, 230, 50, 50),
                                 Gdiplus::Color(50, 220, 40, 40) };
                nStyles = 3;
            }

            for (int s = 0; s < nStyles; ++s) {
                uint32_t ch = styles[s].idx;

                // Build polyline points (256 bins + 2 closing points for filled area)
                const int nBins = static_cast<int>(HistogramData::kBinCount);
                std::vector<Gdiplus::PointF> pts(nBins + 2);

                for (int i = 0; i < nBins; ++i) {
                    double v = std::log(1.0 + hist.bins[ch][i]);
                    float h = static_cast<float>(v / logMax * (plotH - 1));
                    float x = static_cast<float>(plotLeft) + (static_cast<float>(i) + 0.5f)
                              / nBins * plotW;
                    float y = static_cast<float>(plotBottom) - h;
                    pts[i] = Gdiplus::PointF(x, y);
                }
                // Close the polygon along the bottom
                pts[nBins]     = Gdiplus::PointF(static_cast<float>(plotRight),
                                                  static_cast<float>(plotBottom));
                pts[nBins + 1] = Gdiplus::PointF(static_cast<float>(plotLeft),
                                                  static_cast<float>(plotBottom));

                // Filled area (semi-transparent)
                Gdiplus::SolidBrush fillBrush(styles[s].fillColor);
                gfx.FillPolygon(&fillBrush, pts.data(), nBins + 2);

                // Smooth curve on top (anti-aliased)
                Gdiplus::Pen linePen(styles[s].lineColor, 1.5f);
                gfx.DrawCurve(&linePen, pts.data(), nBins, 0.3f);
            }
        }

        // Plot border
        Gdiplus::Pen borderPen(Gdiplus::Color(180, 70, 75, 100), 1.0f);
        gfx.DrawRectangle(&borderPen, plotLeft, plotTop, plotW, plotH);

        // Axis labels
        Gdiplus::Font labelFont(L"Segoe UI", 7.5f);
        Gdiplus::SolidBrush labelBrush(Gdiplus::Color(180, 150, 155, 175));
        Gdiplus::StringFormat sfLeft;
        sfLeft.SetAlignment(Gdiplus::StringAlignmentNear);
        Gdiplus::StringFormat sfRight;
        sfRight.SetAlignment(Gdiplus::StringAlignmentFar);

        float labelY = static_cast<float>(plotBottom + 4);
        Gdiplus::RectF r0((float)plotLeft, labelY, 40.0f, 16.0f);
        gfx.DrawString(L"0", -1, &labelFont, r0, &sfLeft, &labelBrush);

        Gdiplus::RectF r1((float)(plotRight - 40), labelY, 40.0f, 16.0f);
        gfx.DrawString(L"1", -1, &labelFont, r1, &sfRight, &labelBrush);

        // Center label
        Gdiplus::RectF rc5((float)plotLeft, labelY, (float)plotW, 16.0f);
        gfx.DrawString(L"Pixel Value", -1, &labelFont, rc5, &sfCenter, &labelBrush);
    }

    Gdiplus::GdiplusShutdown(gdipToken);
}

// ---------------------------------------------------------------------------
// ComputeAnalysis — background thread: parse XISF, bin pixels, compute stats
// ---------------------------------------------------------------------------

void CXISFPropertySheetHandler::ComputeAnalysis()
{
    // Open file via SHCreateStreamOnFileEx
    IStream* pStream = nullptr;
    HRESULT hr = SHCreateStreamOnFileEx(
        m_filePath.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
        FILE_ATTRIBUTE_NORMAL, FALSE, nullptr, &pStream);
    if (FAILED(hr) || !pStream)
    {
        m_unavailable.store(true, std::memory_order_release);
        m_analyzing.store(false, std::memory_order_release);
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
        m_analyzing.store(false, std::memory_order_release);
        return;
    }

    // Validate XISF signature: "XISF0100"
    if (std::memcmp(preamble, "XISF0100", 8) != 0)
    {
        pStream->Release();
        m_unavailable.store(true, std::memory_order_release);
        m_analyzing.store(false, std::memory_order_release);
        return;
    }

    // Bytes 8-11: XML header length (little-endian uint32)
    uint32_t xmlLen = *reinterpret_cast<const uint32_t*>(preamble + 8);
    if (xmlLen == 0 || xmlLen > xisf::XISFParser::kMaxHeaderBytes)
    {
        pStream->Release();
        m_unavailable.store(true, std::memory_order_release);
        m_analyzing.store(false, std::memory_order_release);
        return;
    }

    // Read XML header
    std::string xml(xmlLen, '\0');
    hr = pStream->Read(xml.data(), xmlLen, &cbRead);
    if (FAILED(hr) || cbRead < xmlLen)
    {
        pStream->Release();
        m_unavailable.store(true, std::memory_order_release);
        m_analyzing.store(false, std::memory_order_release);
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
        m_analyzing.store(false, std::memory_order_release);
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
        m_analyzing.store(false, std::memory_order_release);
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
        m_analyzing.store(false, std::memory_order_release);
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
        m_analyzing.store(false, std::memory_order_release);
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

    // Reserve space for sample collection (stats computation)
    std::vector<float> allSamples;
    allSamples.reserve(kMaxTotalSamples);

    m_histogram.Begin(readChannels);

    for (UINT ch = 0; ch < readChannels; ++ch) {
        ULONGLONG chBase = offset + static_cast<ULONGLONG>(ch) * channelPixels * bps;

        for (UINT row = 0; row < imgH; row += rowStride) {
            LARGE_INTEGER seekPos;
            seekPos.QuadPart = static_cast<LONGLONG>(chBase + static_cast<ULONGLONG>(row) * rowBytes);
            if (FAILED(pStream->Seek(seekPos, STREAM_SEEK_SET, nullptr))) {
                pStream->Release();
                m_unavailable.store(true, std::memory_order_release);
                m_analyzing.store(false, std::memory_order_release);
                return;
            }

            ULONG cbRowRead = 0;
            if (FAILED(pStream->Read(rowBuf.data(), static_cast<ULONG>(rowBytes), &cbRowRead))
                || cbRowRead < rowBytes) {
                pStream->Release();
                m_unavailable.store(true, std::memory_order_release);
                m_analyzing.store(false, std::memory_order_release);
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
                allSamples.push_back(val);
            }
        }
    }

    m_histogram.Commit();

    // Compute stats from collected samples
    if (!allSamples.empty()) {
        // Mean
        double sum = 0;
        for (float v : allSamples) sum += v;
        m_stats.mean = sum / allSamples.size();

        // Median (nth_element is O(n))
        size_t mid = allSamples.size() / 2;
        std::nth_element(allSamples.begin(), allSamples.begin() + mid, allSamples.end());
        m_stats.median = allSamples[mid];

        // Clipping
        size_t clipLo = 0, clipHi = 0;
        for (float v : allSamples) {
            if (v <= 0.0f) clipLo++;
            if (v >= 1.0f) clipHi++;
        }
        m_stats.clippingLow = 100.0 * clipLo / allSamples.size();
        m_stats.clippingHigh = 100.0 * clipHi / allSamples.size();
        m_stats.valid = true;
    }

    m_computed.store(true, std::memory_order_release);
    m_analyzing.store(false, std::memory_order_release);

    pStream->Release();
}
