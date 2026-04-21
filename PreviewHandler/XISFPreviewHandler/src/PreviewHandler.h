// PreviewHandler.h — IPreviewHandler + IInitializeWithStream (Preview Handler)
#pragma once
#include <windows.h>
#include <shobjidl.h>
#include <shlobj.h>
#include <string>
#include "XISFParser.h"

class CPreviewHandler :
    public IPreviewHandler,
    public IInitializeWithStream,
    public IPreviewHandlerVisuals
{
public:
    CPreviewHandler();
    ~CPreviewHandler();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* pStream, DWORD grfMode) override;

    // IPreviewHandler
    IFACEMETHODIMP SetWindow(HWND hwnd, const RECT* prc) override;
    IFACEMETHODIMP SetRect(const RECT* prc) override;
    IFACEMETHODIMP DoPreview() override;
    IFACEMETHODIMP Unload() override;
    IFACEMETHODIMP SetFocus() override;
    IFACEMETHODIMP QueryFocus(HWND* phwnd) override;
    IFACEMETHODIMP TranslateAccelerator(MSG* pmsg) override;

    // IPreviewHandlerVisuals
    IFACEMETHODIMP SetBackgroundColor(COLORREF color) override;
    IFACEMETHODIMP SetFont(const LOGFONTW* plf) override;
    IFACEMETHODIMP SetTextColor(COLORREF color) override;

private:
    long                  m_cRef;
    HWND                  m_hwndParent;
    HWND                  m_hwndPreview;
    RECT                  m_rcPreview;
    COLORREF              m_clrBackground;
    COLORREF              m_clrText;
    LOGFONTW              m_lf;
    xisf::XISFRawMetadata m_metadata;
    bool                  m_initialized;

    void CreatePreviewWindow();
    void UpdatePreviewWindow();

    static LRESULT CALLBACK PreviewWndProc(HWND hwnd, UINT msg,
                                            WPARAM wp, LPARAM lp);

    static const wchar_t kClassName[];
};
