// ThumbnailProvider.h — IThumbnailProvider + IInitializeWithStream (Preview Handler)
#pragma once
#include <windows.h>
#include <thumbcache.h>
#include <wincodec.h>
#include <string>
#include "XISFParser.h"

class CThumbnailProvider :
    public IThumbnailProvider,
    public IInitializeWithStream
{
public:
    CThumbnailProvider();
    ~CThumbnailProvider();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IInitializeWithStream
    IFACEMETHODIMP Initialize(IStream* pStream, DWORD grfMode) override;

    // IThumbnailProvider
    IFACEMETHODIMP GetThumbnail(UINT cx, HBITMAP* phbmp,
                                WTS_ALPHATYPE* pdwAlpha) override;

private:
    long                  m_cRef;
    IStream*              m_pStream;
    xisf::XISFRawMetadata m_metadata;
    bool                  m_initialized;

    /// Generate a preview bitmap from attached pixel data (grayscale UInt16
    /// auto-stretch to 8-bit). Falls back to CreatePlaceholderBitmap on error.
    HBITMAP CreatePreviewBitmap(UINT cx);

    /// Create a dark-blue cx×cx bitmap showing basic file info as text.
    HBITMAP CreatePlaceholderBitmap(UINT cx);
};
