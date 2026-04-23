// PreviewHandler.cpp — IPreviewHandler implementation (Preview Handler)
//
// Creates a child window inside the Explorer preview pane and paints
// XISF metadata as formatted text on a dark background.
//
#include "PreviewHandler.h"
#include "PreviewHandlerTelemetry.h"

#include <shlwapi.h>
#include <cstring>
#include <string>
#include <vector>

extern long      g_cDllRef;
extern HINSTANCE g_hInst;

// ---------------------------------------------------------------------------
// Window class name
// ---------------------------------------------------------------------------

const wchar_t CPreviewHandler::kClassName[] = L"XISFPreviewHandlerWnd";

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

CPreviewHandler::CPreviewHandler()
    : m_cRef(1)
    , m_hwndParent(nullptr)
    , m_hwndPreview(nullptr)
    , m_rcPreview{}
    , m_clrBackground(RGB(18, 18, 32))
    , m_clrText(RGB(220, 220, 220))
    , m_metadata{}
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
    InterlockedDecrement(&g_cDllRef);
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// IInitializeWithStream
// ---------------------------------------------------------------------------

IFACEMETHODIMP CPreviewHandler::Initialize(IStream* pStream, DWORD /*grfMode*/)
{
    if (m_initialized)
    {
        TraceLoggingWrite(g_hPreviewProvider, "PreviewInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_LIFECYCLE),
            TraceLoggingString("AlreadyInitialized", "Stage"));
        if (g_xisfPreviewHandlerTelemetryHook) g_xisfPreviewHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_LIFECYCLE, L"PreviewInitializeFailed Stage=AlreadyInitialized");
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    }
    if (!pStream)
    {
        TraceLoggingWrite(g_hPreviewProvider, "PreviewInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_LIFECYCLE),
            TraceLoggingString("NullStream", "Stage"));
        if (g_xisfPreviewHandlerTelemetryHook) g_xisfPreviewHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_LIFECYCLE, L"PreviewInitializeFailed Stage=NullStream");
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
        if (g_xisfPreviewHandlerTelemetryHook) {
            wchar_t _buf[256]; swprintf_s(_buf, L"PreviewInitializeFailed Stage=ReadPreamble HRESULT=0x%08X CbRead=%u", static_cast<unsigned>(hr), cbRead);
            g_xisfPreviewHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PARSE, _buf);
        }
        return E_FAIL;
    }
    if (memcmp(preamble, "XISF0100", 8) != 0)
    {
        TraceLoggingWrite(g_hPreviewProvider, "PreviewInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_PARSE),
            TraceLoggingString("InvalidSignature", "Stage"));
        if (g_xisfPreviewHandlerTelemetryHook) g_xisfPreviewHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PARSE, L"PreviewInitializeFailed Stage=InvalidSignature");
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
        if (g_xisfPreviewHandlerTelemetryHook) {
            wchar_t _buf[128]; swprintf_s(_buf, L"PreviewInitializeFailed Stage=HeaderLength Length=%u", headerLength);
            g_xisfPreviewHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PARSE, _buf);
        }
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
        if (g_xisfPreviewHandlerTelemetryHook) {
            wchar_t _buf[256]; swprintf_s(_buf, L"PreviewInitializeFailed Stage=ReadHeader HRESULT=0x%08X CbRead=%u Expected=%u", static_cast<unsigned>(hr), cbRead, headerLength);
            g_xisfPreviewHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PARSE, _buf);
        }
        return E_FAIL;
    }

    xisf::ParseResult result = xisf::XISFParser::ParseXMLString(buffer);
    if (!result.ok())
    {
        TraceLoggingWrite(g_hPreviewProvider, "PreviewInitializeFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_PARSE),
            TraceLoggingString("ParseXml", "Stage"));
        if (g_xisfPreviewHandlerTelemetryHook) g_xisfPreviewHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PARSE, L"PreviewInitializeFailed Stage=ParseXml");
        return E_FAIL;
    }
    m_metadata = std::move(result.metadata);

    m_initialized = true;
    TraceLoggingWrite(g_hPreviewProvider, "PreviewInitialized",
        TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
        TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_LIFECYCLE),
        TraceLoggingUInt32(headerLength, "HeaderBytes"));
    if (g_xisfPreviewHandlerTelemetryHook) {
        wchar_t _buf[128]; swprintf_s(_buf, L"PreviewInitialized HeaderBytes=%u", headerLength);
        g_xisfPreviewHandlerTelemetryHook(TRACE_LEVEL_INFORMATION, XISF_PREVIEW_KEYWORD_LIFECYCLE, _buf);
    }
    return S_OK;
}

// ---------------------------------------------------------------------------
// IPreviewHandler
// ---------------------------------------------------------------------------

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
        if (g_xisfPreviewHandlerTelemetryHook) g_xisfPreviewHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PREVIEW, L"DoPreviewFailed Stage=MissingParentWindow");
        return E_FAIL;
    }

    CreatePreviewWindow();

    if (!m_hwndPreview)
    {
        TraceLoggingWrite(g_hPreviewProvider, "DoPreviewFailed",
            TraceLoggingLevel(TRACE_LEVEL_WARNING),
            TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_PREVIEW),
            TraceLoggingString("CreatePreviewWindow", "Stage"));
        if (g_xisfPreviewHandlerTelemetryHook) g_xisfPreviewHandlerTelemetryHook(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PREVIEW, L"DoPreviewFailed Stage=CreatePreviewWindow");
        return E_FAIL;
    }

    ShowWindow(m_hwndPreview, SW_SHOW);
    UpdateWindow(m_hwndPreview);
    TraceLoggingWrite(g_hPreviewProvider, "PreviewDisplayed",
        TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
        TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_PREVIEW));
    if (g_xisfPreviewHandlerTelemetryHook) g_xisfPreviewHandlerTelemetryHook(TRACE_LEVEL_INFORMATION, XISF_PREVIEW_KEYWORD_PREVIEW, L"PreviewDisplayed");
    return S_OK;
}

IFACEMETHODIMP CPreviewHandler::Unload()
{
    if (m_hwndPreview)
    {
        DestroyWindow(m_hwndPreview);
        m_hwndPreview = nullptr;
    }
    TraceLoggingWrite(g_hPreviewProvider, "PreviewUnloaded",
        TraceLoggingLevel(TRACE_LEVEL_INFORMATION),
        TraceLoggingKeyword(XISF_PREVIEW_KEYWORD_LIFECYCLE));
    if (g_xisfPreviewHandlerTelemetryHook) g_xisfPreviewHandlerTelemetryHook(TRACE_LEVEL_INFORMATION, XISF_PREVIEW_KEYWORD_LIFECYCLE, L"PreviewUnloaded");
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

// ---------------------------------------------------------------------------
// IPreviewHandlerVisuals
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Window management
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Window procedure
// ---------------------------------------------------------------------------

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

        // Helper: draw a label (accent colour) + value (body colour) on one line
        auto DrawField = [&](const wchar_t* label, const std::string& value)
        {
            if (value.empty()) return;

            // Label in accent colour
            ::SetTextColor(hdc, RGB(100, 180, 255));
            int labelLen = static_cast<int>(wcslen(label));
            TextOutW(hdc, x, y, label, labelLen);

            // Measure label width to place value after it
            SIZE labelSz{};
            GetTextExtentPoint32W(hdc, label, labelLen, &labelSz);

            // Value in body colour
            ::SetTextColor(hdc, pThis->m_clrText);
            wchar_t wVal[512] = {};
            MultiByteToWideChar(CP_UTF8, 0, value.c_str(),
                                static_cast<int>(value.size()), wVal, 511);
            TextOutW(hdc, x + labelSz.cx, y, wVal,
                     static_cast<int>(wcslen(wVal)));
            y += lineH;
        };

        // Title
        ::SetTextColor(hdc, RGB(200, 230, 255));
        HFONT hBold = CreateFontW(pThis->m_lf.lfHeight - 2, 0, 0, 0, FW_BOLD,
                                  FALSE, FALSE, FALSE,
                                  DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                                  CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                                  DEFAULT_PITCH | FF_SWISS,
                                  pThis->m_lf.lfFaceName);
        SelectObject(hdc, hBold);
        TextOutW(hdc, x, y, L"XISF File Metadata", 18);
        y += lineH + 8;
        SelectObject(hdc, hFont); // restore regular font

        // Divider line
        HPEN hPen = CreatePen(PS_SOLID, 1, RGB(60, 80, 140));
        HGDIOBJ hOldPen = SelectObject(hdc, hPen);
        MoveToEx(hdc, x, y, nullptr);
        LineTo(hdc, rcClient.right - margin, y);
        SelectObject(hdc, hOldPen);
        DeleteObject(hPen);
        y += 8;

        // Metadata fields
        const auto& md = pThis->m_metadata;

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

        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
        DeleteObject(hBold);

        EndPaint(hwnd, &ps);
        return 0;
    }

    if (msg == WM_ERASEBKGND)
        return 1; // handled in WM_PAINT

    return DefWindowProcW(hwnd, msg, wp, lp);
}
