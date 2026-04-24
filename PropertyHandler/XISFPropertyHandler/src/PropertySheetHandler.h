// PropertySheetHandler.h — IShellPropSheetExt handler for XISF histogram tab
#pragma once
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <string>
#include <thread>
#include <atomic>
#include <cstdint>
#include <cstring>

// ---------------------------------------------------------------------------
// HistogramData — per-channel 8-bit intensity histogram
// (Copied from PreviewHandler; kept separate to avoid cross-DLL dependency)
// ---------------------------------------------------------------------------
struct HistogramData
{
    static constexpr uint32_t kMaxChannels = 3;
    static constexpr uint32_t kBinCount    = 256;

    uint32_t bins[kMaxChannels][kBinCount];
    uint32_t channelCount;  // 1 = grayscale, 3 = RGB
    bool     valid;         // false until successfully populated

    HistogramData() { Reset(); }

    void Reset()
    {
        std::memset(bins, 0, sizeof(bins));
        channelCount = 0;
        valid        = false;
    }

    void Begin(uint32_t channels)
    {
        Reset();
        channelCount = channels;
    }

    void Commit() { valid = true; }
};

// ---------------------------------------------------------------------------
// CXISFPropertySheetHandler
// ---------------------------------------------------------------------------
class CXISFPropertySheetHandler : public IShellExtInit, public IShellPropSheetExt
{
public:
    CXISFPropertySheetHandler();
    ~CXISFPropertySheetHandler();

    // IUnknown
    IFACEMETHODIMP QueryInterface(REFIID riid, void** ppv) override;
    IFACEMETHODIMP_(ULONG) AddRef() override;
    IFACEMETHODIMP_(ULONG) Release() override;

    // IShellExtInit
    IFACEMETHODIMP Initialize(PCIDLIST_ABSOLUTE pidlFolder, IDataObject* pdtobj, HKEY hkeyProgID) override;

    // IShellPropSheetExt
    IFACEMETHODIMP AddPages(LPFNSVADDPROPSHEETPAGE pfnAddPage, LPARAM lParam) override;
    IFACEMETHODIMP ReplacePage(EXPPS uPageID, LPFNSVADDPROPSHEETPAGE pfnReplacePage, LPARAM lParam) override;

    // Accessors for the dialog proc
    bool                  IsComputed() const { return m_computed.load(std::memory_order_acquire); }
    bool                  IsUnavailable() const { return m_unavailable.load(std::memory_order_acquire); }
    const HistogramData&  GetHistogram() const { return m_histogram; }

private:
    void ComputeHistogram();

    static INT_PTR CALLBACK HistogramDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void PaintHistogram(HDC hdc, const RECT& rcArea, const HistogramData& hist);

    long               m_cRef;
    std::wstring       m_filePath;
    HistogramData      m_histogram;
    HWND               m_hwndPage;
    std::thread        m_computeThread;
    std::atomic<bool>  m_computed;
    std::atomic<bool>  m_unavailable;
};
