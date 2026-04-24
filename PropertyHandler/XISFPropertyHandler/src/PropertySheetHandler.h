// PropertySheetHandler.h — IShellPropSheetExt handler for XISF Astro Details tab
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
// ---------------------------------------------------------------------------
struct HistogramData
{
    static constexpr uint32_t kMaxChannels = 3;
    static constexpr uint32_t kBinCount    = 256;

    uint32_t bins[kMaxChannels][kBinCount];
    uint32_t channelCount;
    bool     valid;

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
// PixelStats — computed statistics from pixel data
// ---------------------------------------------------------------------------
struct PixelStats
{
    double median;
    double mean;
    double clippingLow;
    double clippingHigh;
    bool   valid;

    PixelStats() : median(0), mean(0), clippingLow(0), clippingHigh(0), valid(false) {}
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
    bool                  IsAnalyzing() const { return m_analyzing.load(std::memory_order_acquire); }
    const HistogramData&  GetHistogram() const { return m_histogram; }
    const PixelStats&     GetStats() const { return m_stats; }
    bool                  HasCachedStats() const { return m_cachedStats.valid; }
    const PixelStats&     GetCachedStats() const { return m_cachedStats; }

private:
    void ComputeAnalysis();
    bool ReadCachedStats();
    bool ReadStatsFromStore(IPropertyStore* pStore);
    void StartAnalysis();

    static INT_PTR CALLBACK AstroDetailsDlgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static void PaintHistogram(HDC hdc, const RECT& rcArea, const HistogramData& hist);
    static void PaintStats(HDC hdc, const RECT& rcArea, const PixelStats& stats);

    long               m_cRef;
    std::wstring       m_filePath;
    HistogramData      m_histogram;
    PixelStats         m_stats;
    PixelStats         m_cachedStats;
    HWND               m_hwndPage;
    HWND               m_hwndButton;
    std::thread        m_computeThread;
    std::atomic<bool>  m_computed;
    std::atomic<bool>  m_unavailable;
    std::atomic<bool>  m_analyzing;
};
