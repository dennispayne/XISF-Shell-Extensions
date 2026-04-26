// PixelStatistics.cpp — Compute pixel-level statistics from XISF image data.
// Extracted from PropertyStore.cpp to isolate expensive stream-reading operations.
#include "PixelStatistics.h"
#include "PropertyHandlerTraceLogging.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <strsafe.h>

extern "C" XISFPropertyHandlerTelemetryHook g_xisfPropertyHandlerTelemetryHook;
void WritePropertyHandlerTelemetry(UCHAR level, ULONGLONG keyword, PCWSTR format, ...);

namespace xisf {

bool IsPixelStatKey(REFPROPERTYKEY key)
{
    return IsEqualPropertyKey(key, PKEY_XISF_Median) ||
           IsEqualPropertyKey(key, PKEY_XISF_Mean) ||
           IsEqualPropertyKey(key, PKEY_XISF_ClippingLow) ||
           IsEqualPropertyKey(key, PKEY_XISF_ClippingHigh);
}

PixelStatsResult ComputePixelStats(IStream* pStream, const std::string& xmlHeader)
{
    const ULONGLONG statsStart = GetTickCount64();
    PixelStatsResult result;

    if (!pStream || xmlHeader.empty()) {
        return result;
    }

    const std::string& xml = xmlHeader;

    // Local attribute parser
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
        WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PERF,
            L"PixelStatsUnavailable Reason=NoAttachmentImage");
        return result;
    }

    // Pick largest non-thumbnail image
    size_t bestIdx = 0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].isThumbnail) continue;
        if (candidates[i].attachSize > candidates[bestIdx].attachSize || candidates[bestIdx].isThumbnail)
            bestIdx = i;
    }
    const std::string& imgElem = candidates[bestIdx].elemText;

    std::string geometry = findAttr(imgElem, "geometry");
    std::string location = findAttr(imgElem, "location");
    std::string sampleFmt = findAttr(imgElem, "sampleFormat");

    if (geometry.empty() || location.empty()) {
        WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PERF,
            L"PixelStatsUnavailable Reason=MissingGeometryOrLocation");
        return result;
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
        return result;
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
        return result;
    }

    // Sample format
    bool isUInt16 = (sampleFmt == "UInt16" || sampleFmt.empty());
    bool isUInt8 = (sampleFmt == "UInt8");
    bool isFloat32 = (sampleFmt == "Float32");
    bool isFloat64 = (sampleFmt == "Float64");
    size_t bps = isUInt8 ? 1 : isFloat32 ? 4 : isFloat64 ? 8 : 2;

    UINT readChannels = (imgC >= 3) ? 3 : 1;
    size_t channelPixels = static_cast<size_t>(imgW) * static_cast<size_t>(imgH);

    // Subsample strategy: target ~1024 rows, cap total samples at ~1M for fast compute
    constexpr size_t kMaxTotalSamples = 1024 * 1024;

    UINT sampleRows = (std::min)(imgH, 1024u);
    UINT rowStride = (std::max)(1u, imgH / sampleRows);

    // Collect normalized pixel samples (pooled across all channels)
    // Cap columns so total samples (cols × rows × channels) stays under budget
    UINT sampleCols = imgW;
    UINT colStride = 1;
    size_t maxSamples = static_cast<size_t>(sampleCols) * sampleRows * readChannels;
    if (maxSamples > kMaxTotalSamples) {
        UINT targetCols = static_cast<UINT>(kMaxTotalSamples / (static_cast<size_t>(sampleRows) * readChannels));
        sampleCols = (std::max)(1u, (std::min)(imgW, targetCols));
        colStride = (std::max)(1u, imgW / sampleCols);
        maxSamples = static_cast<size_t>(sampleCols) * sampleRows * readChannels;
    }

    std::vector<float> samples;
    samples.reserve(maxSamples);

    size_t rowBytes = static_cast<size_t>(imgW) * bps;
    std::vector<uint8_t> rowBuf(rowBytes);

    size_t clipLow = 0, clipHigh = 0;
    double runningSum = 0.0;

    constexpr float kClipLowThresh = 0.001f;
    constexpr float kClipHighThresh = 0.999f;

    for (UINT ch = 0; ch < readChannels; ++ch) {
        ULONGLONG chBase = offset + static_cast<ULONGLONG>(ch) * channelPixels * bps;

        for (UINT row = 0; row < imgH; row += rowStride) {
            LARGE_INTEGER seekPos;
            seekPos.QuadPart = static_cast<LONGLONG>(chBase + static_cast<ULONGLONG>(row) * rowBytes);
            if (FAILED(pStream->Seek(seekPos, STREAM_SEEK_SET, nullptr))) {
                WritePropertyHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_ETW_KEYWORD_PERF,
                    L"PixelStatsUnavailable Reason=SeekFailed Channel=%u Row=%u", ch, row);
                return result;
            }

            ULONG cbRead = 0;
            if (FAILED(pStream->Read(rowBuf.data(), static_cast<ULONG>(rowBytes), &cbRead)) || cbRead < rowBytes) {
                return result;
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

                samples.push_back(val);
                runningSum += val;
                if (val <= kClipLowThresh) ++clipLow;
                if (val >= kClipHighThresh) ++clipHigh;
            }
        }
    }

    if (samples.empty()) {
        return result;
    }

    // Compute stats — round to display precision
    double meanVal = runningSum / samples.size();

    // Median via nth_element (O(n))
    size_t midIdx = samples.size() / 2;
    std::nth_element(samples.begin(), samples.begin() + midIdx, samples.end());
    double medianVal = samples[midIdx];

    double clipLowPct = 100.0 * clipLow / samples.size();
    double clipHighPct = 100.0 * clipHigh / samples.size();

    // Round: Median/Mean to 4 decimal places, Clipping to 1
    medianVal = std::round(medianVal * 10000.0) / 10000.0;
    meanVal = std::round(meanVal * 10000.0) / 10000.0;
    clipLowPct = std::round(clipLowPct * 10.0) / 10.0;
    clipHighPct = std::round(clipHighPct * 10.0) / 10.0;

    result.available = true;
    result.median = medianVal;
    result.mean = meanVal;
    result.clippingLowPct = clipLowPct;
    result.clippingHighPct = clipHighPct;

    ULONGLONG statsDur = GetTickCount64() - statsStart;
    WritePropertyHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_ETW_KEYWORD_PERF,
        L"PixelStatsComputed Median=%.4f Mean=%.4f ClipLow=%.2f%% ClipHigh=%.2f%% Samples=%zu DurationMs=%llu",
        medianVal, meanVal, clipLowPct, clipHighPct, samples.size(), statsDur);

    return result;
}

} // namespace xisf
