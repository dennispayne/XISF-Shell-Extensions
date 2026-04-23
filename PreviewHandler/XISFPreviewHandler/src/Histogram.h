// Histogram.h — Per-channel histogram data for XISF preview images
#pragma once
#include <cstdint>
#include <cstring>

/// Holds per-channel 8-bit intensity histograms accumulated during the
/// pixel streaming pass in CThumbnailProvider::CreatePreviewBitmap().
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

    /// Prepare for accumulation with the given number of channels (1 or 3).
    void Begin(uint32_t channels)
    {
        Reset();
        channelCount = channels;
    }

    /// Mark the histogram as successfully completed.
    void Commit() { valid = true; }
};
