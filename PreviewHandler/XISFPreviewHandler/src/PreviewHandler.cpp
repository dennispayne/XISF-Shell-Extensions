// PreviewHandler.cpp — IPreviewHandler implementation (Preview Handler)
//
// Creates a child window inside the Explorer preview pane and paints
// XISF metadata as formatted text on a dark background.
//
#include "PreviewHandler.h"
#include "PreviewHandlerTraceLogging.h"
#include "ThumbnailProvider.h"

#include <shlwapi.h>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

extern long      g_cDllRef;
extern HINSTANCE g_hInst;

// Window class name
const wchar_t CPreviewHandler::kClassName[] = L"XISFPreviewHandlerWnd";

namespace
{
    // Draw the title bar, divider, and all metadata fields. Advances `y`
    // through each line and leaves the HDC's text color in a caller-defined
    // state (caller is responsible for font lifetime of `regularFont`).
    void PaintMetadataFields(HDC hdc, const RECT& rcClient, int x, int& y,
                             int lineH, int margin,
                             const xisf::XISFRawMetadata& md,
                             const LOGFONTW& lf, COLORREF clrText,
                             HFONT regularFont)
    {
        auto DrawField = [&](const wchar_t* label, const std::string& value)
        {
            if (value.empty()) return;

            ::SetTextColor(hdc, RGB(100, 180, 255));
            int labelLen = static_cast<int>(wcslen(label));
            TextOutW(hdc, x, y, label, labelLen);

            SIZE labelSz{};
            GetTextExtentPoint32W(hdc, label, labelLen, &labelSz);

            ::SetTextColor(hdc, clrText);
            wchar_t wVal[512] = {};
            MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                static_cast<int>(value.size()), wVal, 511);
            TextOutW(hdc, x + labelSz.cx, y, wVal,
                     static_cast<int>(wcslen(wVal)));
            y += lineH;
        };

        // Title
        ::SetTextColor(hdc, RGB(200, 230, 255));
        HFONT hBold = CreateFontW(lf.lfHeight - 2, 0, 0, 0, FW_BOLD,
                                  FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_SWISS, lf.lfFaceName);
        SelectObject(hdc, hBold);
        TextOutW(hdc, x, y, L"XISF File Metadata", 18);
        y += lineH + 8;
        SelectObject(hdc, regularFont); // restore regular font

        // Divider line
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(60, 80, 140));
        HGDIOBJ hOldPen = SelectObject(hdc, hPen);
        MoveToEx(hdc, x, y, nullptr);
        LineTo(hdc, rcClient.right - margin, y);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);
        y += 8;

        DrawField(L"Object:      ", md.getFITSValue("OBJECT"));
        DrawField(L"Exposure:    ", md.getFITSValue("EXPTIME") +
                  (md.getFITSValue("EXPTIME").empty() ? "" : " s"));
        DrawField(L"Filter:      ", md.getFITSValue("FILTER"));
        DrawField(L"Telescope:   ", md.getFITSValue("TELESCOP"));
        DrawField(L"Camera:      ", md.getFITSValue("INSTRUME"));
        {
            std::string temp = md.getFITSValue("CCD-TEMP");
            if (!temp.empty()) temp += " \xC2\xB0""C"; // UTF-8 degree symbol
            DrawField(L"Temperature: ", temp);
        }
        DrawField(L"Observer:    ", md.getFITSValue("OBSERVER"));
        DrawField(L"Gain:        ", md.getFITSValue("GAIN"));

        // XISF Properties (if any FITS keywords are absent)
        DrawField(L"Exp (prop):  ",
                  md.getPropertyValue("Instrument:ExposureTime"));
        DrawField(L"Filter (p):  ",
                  md.getPropertyValue("Instrument:Filter:Name"));

        DeleteObject(hBold);
    }
}

// Constructor / Destructor
CPreviewHandler::CPreviewHandler()
    : m_cRef(1)
    , m_hwndParent(nullptr)
    , m_hwndPreview(nullptr)
    , m_rcPreview{}
    , m_clrBackground(RGB(18, 18, 32))
    , m_clrText(RGB(220, 220, 220))
    , m_metadata{}
    , m_pStream(nullptr)
    , m_hbmPreview(nullptr)
    , m_previewWidth(0)
    , m_previewHeight(0)
    , m_initialized(false)
{
    std::memset(&m_lf, 0, sizeof(m_lf));
    m_lf.lfHeight    = -14;
    m_lf.lfWeight    = FW_NORMAL;
    m_lf.lfCharSet   = DEFAULT_CHARSET;
    m_lf.lfQuality   = CLEARTYPE_QUALITY;
    m_lf.lfPitchAndFamily = DEFAULT_PITCH | FF_SWISS;
    wcscpy_s(m_lf.lfFaceName, L"Segoe UI");

    InterlockedIncrement(&g_cDllRef);
}

CPreviewHandler::~CPreviewHandler()
{
    if (m_hwndPreview)
    {
        DestroyWindow(m_hwndPreview);
        m_hwndPreview = nullptr;
    }
    if (m_hbmPreview)
    {
        DeleteObject(m_hbmPreview);
        m_hbmPreview = nullptr;
    }
    if (m_pStream)
    {
        m_pStream->Release();
        m_pStream = nullptr;
    }
    InterlockedDecrement(&g_cDllRef);
}

// IUnknown
IFACEMETHODIMP CPreviewHandler::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;

    if (IsEqualIID(riid, IID_IUnknown))
        *ppv = static_cast<IPreviewHandler*>(this);
    else if (IsEqualIID(riid, IID_IPreviewHandler))
        *ppv = static_cast<IPreviewHandler*>(this);
    else if (IsEqualIID(riid, IID_IInitializeWithStream))
        *ppv = static_cast<IInitializeWithStream*>(this);
    else if (IsEqualIID(riid, IID_IPreviewHandlerVisuals))
        *ppv = static_cast<IPreviewHandlerVisuals*>(this);
    else { *ppv = nullptr; return E_NOINTERFACE; }

    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) CPreviewHandler::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

IFACEMETHODIMP_(ULONG) CPreviewHandler::Release()
{
    ULONG r = InterlockedDecrement(&m_cRef);
    if (r == 0) delete this;
    return r;
}

// IInitializeWithStream
IFACEMETHODIMP CPreviewHandler::Initialize(IStream* pStream, DWORD /*grfMode*/)
{
    if (m_initialized)
    {
        TraceLoggingWrite(g_hPreviewProvider, "PreviewInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_LIFECYCLE),
            TraceLoggingString("AlreadyInitialized", "Stage"));
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_LIFECYCLE,
            L"PreviewInitializeFailed Stage=AlreadyInitialized");
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    }
    if (!pStream)
    {
        TraceLoggingWrite(g_hPreviewProvider, "PreviewInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_LIFECYCLE),
            TraceLoggingString("NullStream", "Stage"));
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_LIFECYCLE,
            L"PreviewInitializeFailed Stage=NullStream");
        return E_INVALIDARG;
    }

    BYTE preamble[16] = {};
    ULONG cbRead = 0;
    HRESULT hr = pStream->Read(preamble, 16, &cbRead);
    if (FAILED(hr) || cbRead < 16)
    {
        TraceLoggingWrite(g_hPreviewProvider, "PreviewInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_PARSE),
            TraceLoggingString("ReadPreamble", "Stage"),
            TraceLoggingHResult(hr, "Hr"),
            TraceLoggingUInt32(cbRead, "CbRead"));
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PARSE,
            L"PreviewInitializeFailed Stage=ReadPreamble HRESULT=0x%08X CbRead=%u",
            static_cast<unsigned>(hr), cbRead);
        return E_FAIL;
    }
    if (memcmp(preamble, "XISF0100", 8) != 0)
    {
        TraceLoggingWrite(g_hPreviewProvider, "PreviewInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_PARSE),
            TraceLoggingString("InvalidSignature", "Stage"));
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PARSE,
            L"PreviewInitializeFailed Stage=InvalidSignature");
        return E_FAIL;
    }

    UINT32 headerLength = 0;
    memcpy(&headerLength, preamble + 8, sizeof(UINT32));
    if (headerLength == 0 || headerLength > xisf::XISFParser::kMaxHeaderBytes)
    {
        TraceLoggingWrite(g_hPreviewProvider, "PreviewInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_PARSE),
            TraceLoggingString("HeaderLength", "Stage"),
            TraceLoggingUInt32(headerLength, "Length"));
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PARSE,
            L"PreviewInitializeFailed Stage=HeaderLength Length=%u", headerLength);
        return E_FAIL;
    }

    std::string buffer(headerLength, '\0');
    hr = pStream->Read(buffer.data(), headerLength, &cbRead);
    if (FAILED(hr) || cbRead < headerLength)
    {
        TraceLoggingWrite(g_hPreviewProvider, "PreviewInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_PARSE),
            TraceLoggingString("ReadHeader", "Stage"),
            TraceLoggingHResult(hr, "Hr"),
            TraceLoggingUInt32(cbRead, "CbRead"),
            TraceLoggingUInt32(headerLength, "Expected"));
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PARSE,
            L"PreviewInitializeFailed Stage=ReadHeader HRESULT=0x%08X CbRead=%u Expected=%u",
            static_cast<unsigned>(hr), cbRead, headerLength);
        return E_FAIL;
    }

    xisf::ParseResult result = xisf::XISFParser::ParseXMLString(buffer);
    if (!result.ok())
    {
        TraceLoggingWrite(g_hPreviewProvider, "PreviewInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_PARSE),
            TraceLoggingString("ParseXml", "Stage"));
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PARSE,
            L"PreviewInitializeFailed Stage=ParseXml");
        return E_FAIL;
    }
    m_metadata = std::move(result.metadata);

    // Keep a reference to the stream for thumbnail/histogram generation
    m_pStream = pStream;
    m_pStream->AddRef();

    m_initialized = true;
    TraceLoggingWrite(g_hPreviewProvider, "PreviewInitialized",
        TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
        TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_LIFECYCLE),
        TraceLoggingUInt32(headerLength, "HeaderBytes"));
    WritePreviewHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_PREVIEW_KEYWORD_LIFECYCLE,
        L"PreviewInitialized HeaderBytes=%u", headerLength);
    return S_OK;
}

// IPreviewHandler
IFACEMETHODIMP CPreviewHandler::SetWindow(HWND hwnd, const RECT* prc)
{
    if (!hwnd || !prc) return E_INVALIDARG;
    m_hwndParent = hwnd;
    m_rcPreview  = *prc;

    if (m_hwndPreview)
    {
        SetParent(m_hwndPreview, m_hwndParent);
        SetWindowPos(m_hwndPreview, nullptr,
                     m_rcPreview.left, m_rcPreview.top,
                     m_rcPreview.right  - m_rcPreview.left,
                     m_rcPreview.bottom - m_rcPreview.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
    return S_OK;
}

IFACEMETHODIMP CPreviewHandler::SetRect(const RECT* prc)
{
    if (!prc) return E_INVALIDARG;
    m_rcPreview = *prc;

    if (m_hwndPreview)
    {
        SetWindowPos(m_hwndPreview, nullptr,
                     m_rcPreview.left, m_rcPreview.top,
                     m_rcPreview.right  - m_rcPreview.left,
                     m_rcPreview.bottom - m_rcPreview.top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        UpdatePreviewWindow();
    }
    return S_OK;
}

IFACEMETHODIMP CPreviewHandler::DoPreview()
{
    if (!m_hwndParent)
    {
        TraceLoggingWrite(g_hPreviewProvider, "DoPreviewFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_PREVIEW),
            TraceLoggingString("MissingParentWindow", "Stage"));
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PREVIEW,
            L"DoPreviewFailed Stage=MissingParentWindow");
        return E_FAIL;
    }

    // Generate a preview bitmap via CThumbnailProvider, which also
    // accumulates the histogram during pixel streaming.
    if (m_pStream && !m_hbmPreview)
    {
        CThumbnailProvider* pThumb = new (std::nothrow) CThumbnailProvider();
        if (pThumb)
        {
            HRESULT hrThumb = pThumb->Initialize(m_pStream, STGM_READ);
            if (SUCCEEDED(hrThumb))
            {
                UINT cx = static_cast<UINT>(m_rcPreview.right - m_rcPreview.left);
                if (cx < 64) cx = 256;
                WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;
                HBITMAP hbm = nullptr;
                hrThumb = pThumb->GetThumbnail(cx, &hbm, &alpha);
                if (SUCCEEDED(hrThumb) && hbm)
                {
                    m_hbmPreview = hbm;
                    BITMAP bm{};
                    GetObject(m_hbmPreview, sizeof(bm), &bm);
                    m_previewWidth  = bm.bmWidth;
                    m_previewHeight = bm.bmHeight;
                }
                m_histogram = pThumb->GetHistogram();
            }
            pThumb->Release();
        }
    }

    CreatePreviewWindow();

    if (!m_hwndPreview)
    {
        TraceLoggingWrite(g_hPreviewProvider, "DoPreviewFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_PREVIEW),
            TraceLoggingString("CreatePreviewWindow", "Stage"));
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PREVIEW,
            L"DoPreviewFailed Stage=CreatePreviewWindow");
        return E_FAIL;
    }

    ShowWindow(m_hwndPreview, SW_SHOW);
    UpdateWindow(m_hwndPreview);
    TraceLoggingWrite(g_hPreviewProvider, "PreviewDisplayed",
        TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
        TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_PREVIEW));
    WritePreviewHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_PREVIEW_KEYWORD_PREVIEW,
        L"PreviewDisplayed");
    return S_OK;
}

IFACEMETHODIMP CPreviewHandler::Unload()
{
    if (m_hwndPreview)
    {
        DestroyWindow(m_hwndPreview);
        m_hwndPreview = nullptr;
    }
    if (m_hbmPreview)
    {
        DeleteObject(m_hbmPreview);
        m_hbmPreview = nullptr;
    }
    if (m_pStream)
    {
        m_pStream->Release();
        m_pStream = nullptr;
    }
    m_histogram.Reset();
    TraceLoggingWrite(g_hPreviewProvider, "PreviewUnloaded",
        TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
        TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_LIFECYCLE));
    WritePreviewHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_PREVIEW_KEYWORD_LIFECYCLE,
        L"PreviewUnloaded");
    return S_OK;
}

IFACEMETHODIMP CPreviewHandler::SetFocus()
{
    if (!m_hwndPreview) return E_FAIL;
    ::SetFocus(m_hwndPreview);
    return S_OK;
}

IFACEMETHODIMP CPreviewHandler::QueryFocus(HWND* phwnd)
{
    if (!phwnd) return E_POINTER;
    *phwnd = ::GetFocus();
    return S_OK;
}

IFACEMETHODIMP CPreviewHandler::TranslateAccelerator(MSG* pmsg)
{
    if (!pmsg) return E_POINTER;
    return S_FALSE;
}

// IPreviewHandlerVisuals
IFACEMETHODIMP CPreviewHandler::SetBackgroundColor(COLORREF color)
{
    m_clrBackground = color;
    UpdatePreviewWindow();
    return S_OK;
}

IFACEMETHODIMP CPreviewHandler::SetFont(const LOGFONTW* plf)
{
    if (!plf) return E_INVALIDARG;
    m_lf = *plf;
    UpdatePreviewWindow();
    return S_OK;
}

IFACEMETHODIMP CPreviewHandler::SetTextColor(COLORREF color)
{
    m_clrText = color;
    UpdatePreviewWindow();
    return S_OK;
}

// Window management
void CPreviewHandler::CreatePreviewWindow()
{
    if (m_hwndPreview) return;

    // Register the window class once per process
    WNDCLASSEXW wcx{};
    wcx.cbSize        = sizeof(wcx);
    wcx.style         = CS_HREDRAW | CS_VREDRAW;
    wcx.lpfnWndProc   = PreviewWndProc;
    wcx.hInstance     = g_hInst;
    wcx.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wcx.lpszClassName = kClassName;
    RegisterClassExW(&wcx); // ignore failure — class may already exist

    m_hwndPreview = CreateWindowExW(
        0, kClassName, L"",
        WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN,
        m_rcPreview.left, m_rcPreview.top,
        m_rcPreview.right  - m_rcPreview.left,
        m_rcPreview.bottom - m_rcPreview.top,
        m_hwndParent, nullptr, g_hInst, this);
}

void CPreviewHandler::UpdatePreviewWindow()
{
    if (m_hwndPreview)
        InvalidateRect(m_hwndPreview, nullptr, TRUE);
}

// Window procedure
LRESULT CALLBACK CPreviewHandler::PreviewWndProc(HWND hwnd, UINT msg,
                                                   WPARAM wp, LPARAM lp)
{
    CPreviewHandler* pThis = nullptr;

    if (msg == WM_CREATE)
    {
        CREATESTRUCTW* pcs = reinterpret_cast<CREATESTRUCTW*>(lp);
        pThis = static_cast<CPreviewHandler*>(pcs->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(pThis));
    }
    else
    {
        pThis = reinterpret_cast<CPreviewHandler*>(
            GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }

    if (msg == WM_PAINT && pThis)
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);

        RECT rcClient{};
        GetClientRect(hwnd, &rcClient);

        // --- Background ---
        HBRUSH hBgBrush = CreateSolidBrush(pThis->m_clrBackground);
        FillRect(hdc, &rcClient, hBgBrush);
        DeleteObject(hBgBrush);

        // --- Font ---
        HFONT hFont = CreateFontIndirectW(&pThis->m_lf);
        HGDIOBJ hOldFont = SelectObject(hdc, hFont);

        SetBkMode(hdc, TRANSPARENT);

        int margin = 16;
        int x      = rcClient.left + margin;
        int y      = rcClient.top  + margin;
        int lineH  = abs(pThis->m_lf.lfHeight) + 4;
        int clientW = rcClient.right - rcClient.left;

        // ----- Section 1: Image preview -----
        if (pThis->m_hbmPreview && pThis->m_previewWidth > 0 &&
            pThis->m_previewHeight > 0)
        {
            int availW = clientW - 2 * margin;
            // Scale to fit width, capping height at 60% of client area
            int maxH  = (rcClient.bottom - rcClient.top) * 60 / 100;
            int drawW = availW;
            int drawH = MulDiv(pThis->m_previewHeight, drawW,
                               pThis->m_previewWidth);
            if (drawH > maxH)
            {
                drawH = maxH;
                drawW = MulDiv(pThis->m_previewWidth, drawH,
                               pThis->m_previewHeight);
            }

            HDC hdcMem = CreateCompatibleDC(hdc);
            HGDIOBJ hOldBmp = SelectObject(hdcMem, pThis->m_hbmPreview);
            SetStretchBltMode(hdc, HALFTONE);
            SetBrushOrgEx(hdc, 0, 0, nullptr);
            int imgX = x + (availW - drawW) / 2;
            StretchBlt(hdc, imgX, y, drawW, drawH,
                       hdcMem, 0, 0,
                       pThis->m_previewWidth, pThis->m_previewHeight,
                       SRCCOPY);
            SelectObject(hdcMem, hOldBmp);
            DeleteDC(hdcMem);

            y += drawH + margin;
        }

        // ----- Section 2: Histogram -----
        if (pThis->m_histogram.valid)
        {
            int histH = 130;
            RECT rcHist = { x, y,
                            rcClient.right - margin, y + histH };
            PaintHistogram(hdc, rcHist, pThis->m_histogram);
            y += histH + margin;
        }

        // ----- Section 3: Metadata text -----
        PaintMetadataFields(hdc, rcClient, x, y, lineH, margin,
                            pThis->m_metadata, pThis->m_lf, pThis->m_clrText,
                            hFont);

        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);

        EndPaint(hwnd, &ps);
        return 0;
    }

    if (msg == WM_ERASEBKGND)
        return 1; // handled in WM_PAINT

    return DefWindowProcW(hwnd, msg, wp, lp);
}

// Histogram rendering (pure GDI)
void CPreviewHandler::PaintHistogram(HDC hdc, const RECT& rcArea,
                                     const HistogramData& hist)
{
    // Background fill — slightly lighter than the main background
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
    // Bottom axis
    MoveToEx(hdc, plotLeft, plotBottom, nullptr);
    LineTo(hdc, plotRight, plotBottom);
    // Left axis
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
    if (logMax <= 0.0) return; // nothing to draw

    // Channel colours — draw back-to-front for RGB overlap
    // For RGB: Blue behind, Green middle, Red front
    // For grayscale: single light gray pass
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

        for (int i = 0; i < HistogramData::kBinCount; ++i)
        {
            double v = std::log(1.0 + hist.bins[ch][i]);
            int barH = static_cast<int>(v / logMax * (plotH - 1));
            if (barH < 1) continue;

            // Map bin index to x pixel range
            int x0 = drawLeft + MulDiv(i,     drawW, HistogramData::kBinCount);
            int x1 = drawLeft + MulDiv(i + 1, drawW, HistogramData::kBinCount);

            // Draw vertical lines filling the bar
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
