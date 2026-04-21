// ThumbnailProvider.cpp — IThumbnailProvider implementation (Preview Handler)
//
// For grayscale UInt16 images the real pixel data starts at the byte offset
// specified by  location="attachment:offset:size"  in the XISF <Image> element.
// This implementation:
//   1. Parses the attachment offset/size from the XML.
//   2. Reads raw UInt16 pixels from the stream, auto-stretches them to 8-bit
//      using percentile clipping (1 %–99 %).
//   3. Scales the result to cx×cx and returns an HBITMAP.
//   4. Falls back to a dark-blue placeholder bitmap with descriptive text on
//      any parse or I/O error.
//
#include "ThumbnailProvider.h"
#include "PreviewHandlerTelemetry.h"

#include <shlwapi.h>
#include <evntrace.h>
#include <evntprov.h>
#include <strsafe.h>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <string>
#include <sstream>
#include <vector>
#include <numeric>

#pragma comment(lib, "advapi32.lib")

extern long g_cDllRef;

// Single definition for the Preview Handler ETW provider handle — externed from dllmain.cpp.
extern "C" REGHANDLE g_hPreviewHandlerTelemetryHandle = 0;

// Single definition for the Preview Handler test hook (see PreviewHandlerTelemetry.h).
extern "C" XISFPreviewHandlerTelemetryHook g_xisfPreviewHandlerTelemetryHook = nullptr;

void WritePreviewHandlerTelemetry(UCHAR level, ULONGLONG keyword, PCWSTR format, ...)
{
    const bool etwEnabled = (g_hPreviewHandlerTelemetryHandle != 0 &&
        EventProviderEnabled(g_hPreviewHandlerTelemetryHandle, level, keyword));
    const bool hookEnabled = (g_xisfPreviewHandlerTelemetryHook != nullptr);
    if (!etwEnabled && !hookEnabled)
    {
        return;
    }

    wchar_t buffer[768] = {};
    va_list args;
    va_start(args, format);
    const HRESULT hr = StringCchVPrintfW(buffer, ARRAYSIZE(buffer), format, args);
    va_end(args);
    if (FAILED(hr))
    {
        return;
    }

    if (etwEnabled)
    {
        EventWriteString(g_hPreviewHandlerTelemetryHandle, level, keyword, buffer);
    }
    if (hookEnabled)
    {
        g_xisfPreviewHandlerTelemetryHook(level, keyword, buffer);
    }
}

// ---------------------------------------------------------------------------
// Constructor / Destructor
// ---------------------------------------------------------------------------

CThumbnailProvider::CThumbnailProvider()
    : m_cRef(1), m_pStream(nullptr), m_initialized(false)
{
    InterlockedIncrement(&g_cDllRef);
}

CThumbnailProvider::~CThumbnailProvider()
{
    if (m_pStream) { m_pStream->Release(); m_pStream = nullptr; }
    InterlockedDecrement(&g_cDllRef);
}

// ---------------------------------------------------------------------------
// IUnknown
// ---------------------------------------------------------------------------

IFACEMETHODIMP CThumbnailProvider::QueryInterface(REFIID riid, void** ppv)
{
    if (!ppv) return E_POINTER;

    if (IsEqualIID(riid, IID_IUnknown))
        *ppv = static_cast<IThumbnailProvider*>(this);
    else if (IsEqualIID(riid, IID_IThumbnailProvider))
        *ppv = static_cast<IThumbnailProvider*>(this);
    else if (IsEqualIID(riid, IID_IInitializeWithStream))
        *ppv = static_cast<IInitializeWithStream*>(this);
    else { *ppv = nullptr; return E_NOINTERFACE; }

    AddRef();
    return S_OK;
}

IFACEMETHODIMP_(ULONG) CThumbnailProvider::AddRef()
{
    return InterlockedIncrement(&m_cRef);
}

IFACEMETHODIMP_(ULONG) CThumbnailProvider::Release()
{
    ULONG r = InterlockedDecrement(&m_cRef);
    if (r == 0) delete this;
    return r;
}

// ---------------------------------------------------------------------------
// IInitializeWithStream
// ---------------------------------------------------------------------------

IFACEMETHODIMP CThumbnailProvider::Initialize(IStream* pStream, DWORD /*grfMode*/)
{
    if (m_initialized)
    {
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_LIFECYCLE,
            L"ThumbnailInitializeFailed Stage=AlreadyInitialized");
        return HRESULT_FROM_WIN32(ERROR_ALREADY_INITIALIZED);
    }
    if (!pStream)
    {
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_LIFECYCLE,
            L"ThumbnailInitializeFailed Stage=NullStream");
        return E_INVALIDARG;
    }

    m_pStream = pStream;
    m_pStream->AddRef();

    LARGE_INTEGER liZero = {};
    pStream->Seek(liZero, STREAM_SEEK_SET, nullptr);
    BYTE preamble[16] = {};
    ULONG cbRead = 0;
    HRESULT hr = pStream->Read(preamble, 16, &cbRead);
    if (FAILED(hr) || cbRead < 16)
    {
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PARSE,
            L"ThumbnailInitializeFailed Stage=ReadPreamble HRESULT=0x%08X CbRead=%u",
            static_cast<unsigned>(hr), cbRead);
        return E_FAIL;
    }
    if (memcmp(preamble, "XISF0100", 8) != 0)
    {
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PARSE,
            L"ThumbnailInitializeFailed Stage=InvalidSignature");
        return E_FAIL;
    }

    UINT32 headerLength = 0;
    memcpy(&headerLength, preamble + 8, sizeof(UINT32));
    if (headerLength == 0 || headerLength > xisf::XISFParser::kMaxHeaderBytes)
    {
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PARSE,
            L"ThumbnailInitializeFailed Stage=HeaderLength Length=%u",
            headerLength);
        return E_FAIL;
    }

    std::string buffer(headerLength, '\0');
    hr = pStream->Read(buffer.data(), headerLength, &cbRead);
    if (FAILED(hr) || cbRead < headerLength)
    {
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PARSE,
            L"ThumbnailInitializeFailed Stage=ReadHeader HRESULT=0x%08X CbRead=%u Expected=%u",
            static_cast<unsigned>(hr), cbRead, headerLength);
        return E_FAIL;
    }

    xisf::ParseResult result = xisf::XISFParser::ParseXMLString(buffer);
    if (!result.ok())
    {
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_PARSE,
            L"ThumbnailInitializeFailed Stage=ParseXml");
        return E_FAIL;
    }
    m_metadata = std::move(result.metadata);
    m_initialized = true;
    WritePreviewHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_PREVIEW_KEYWORD_LIFECYCLE,
        L"ThumbnailInitialized HeaderBytes=%u", headerLength);
    return S_OK;
}

// ---------------------------------------------------------------------------
// IThumbnailProvider
// ---------------------------------------------------------------------------

IFACEMETHODIMP CThumbnailProvider::GetThumbnail(UINT cx, HBITMAP* phbmp,
                                                   WTS_ALPHATYPE* pdwAlpha)
{
    if (!phbmp || !pdwAlpha) return E_POINTER;
    *phbmp    = nullptr;
    *pdwAlpha = WTSAT_UNKNOWN;

    const auto tStart = std::chrono::steady_clock::now();

    HBITMAP hbmp = CreatePreviewBitmap(cx);
    const bool usedPlaceholder = (hbmp == nullptr);
    if (!hbmp)
        hbmp = CreatePlaceholderBitmap(cx);

    const auto tEnd = std::chrono::steady_clock::now();
    const LONGLONG durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        tEnd - tStart).count();

    if (!hbmp)
    {
        WritePreviewHandlerTelemetry(TRACE_LEVEL_ERROR, XISF_PREVIEW_KEYWORD_THUMBNAIL,
            L"ThumbnailFailed RequestedSize=%u DurationMs=%lld",
            cx, durationMs);
        return E_FAIL;
    }

    WritePreviewHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_PREVIEW_KEYWORD_THUMBNAIL,
        L"ThumbnailCompleted RequestedSize=%u UsedPlaceholder=%u DurationMs=%lld",
        cx, usedPlaceholder ? 1u : 0u, durationMs);

    *phbmp    = hbmp;
    *pdwAlpha = WTSAT_UNKNOWN;
    return S_OK;
}

// ---------------------------------------------------------------------------
// CreatePreviewBitmap
//
// Attempts to read raw UInt16 pixel data from the XISF attachment, auto-stretch,
// scale to cx×cx, and return a 24-bit DIB.
// ---------------------------------------------------------------------------

HBITMAP CThumbnailProvider::CreatePreviewBitmap(UINT cx)
{
    if (!m_pStream || !m_initialized)
    {
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_THUMBNAIL,
            L"ThumbnailDecodeFailed Stage=NotInitialized");
        return nullptr;
    }

    const std::string& xml = m_metadata.xmlHeader;

    // Find the <Image …> element and extract geometry and location attributes.
    // We reuse the same minimal attribute scanner from XISFParser.
    auto findAttr = [&](const std::string& elemText,
                        const std::string& attr) -> std::string
    {
        size_t p = 0;
        while (p < elemText.size())
        {
            size_t ap = elemText.find(attr, p);
            if (ap == std::string::npos) break;
            if (ap > 0)
            {
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

    // Locate the best <Image> element for thumbnailing.
    // Strategy: prefer id="thumbnail", then largest attachment image.
    struct ImageCandidate {
        std::string elemText;
        ULONGLONG   attachSize;
        bool        isThumbnail;
    };
    std::vector<ImageCandidate> candidates;

    size_t searchPos = 0;
    while (true)
    {
        size_t imgStart = xml.find("<Image", searchPos);
        if (imgStart == std::string::npos) break;
        size_t imgEnd = xml.find('>', imgStart);
        if (imgEnd == std::string::npos) break;
        searchPos = imgEnd + 1;

        std::string elem = xml.substr(imgStart + 6, imgEnd - imgStart - 6);
        std::string loc  = findAttr(elem, "location");
        if (loc.compare(0, 11, "attachment:") != 0) continue; // skip non-attachment

        std::string id   = findAttr(elem, "id");
        bool isThumb = (id == "thumbnail" || id == "Thumbnail");

        // Parse attachment size from "attachment:offset:size"
        ULONGLONG aSize = 0;
        {
            const char* p = loc.c_str() + 11;
            char* end = nullptr;
            std::strtoull(p, &end, 10); // skip offset
            if (end && *end == ':')
                aSize = std::strtoull(end + 1, nullptr, 10);
        }
        if (aSize == 0) continue;

        candidates.push_back({std::move(elem), aSize, isThumb});
    }

    if (candidates.empty()) {
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_THUMBNAIL,
            L"ThumbnailDecodeFailed Stage=NoAttachmentImage");
        return nullptr;
    }

    // Pick best: embedded thumbnail first, then largest image
    size_t bestIdx = 0;
    for (size_t i = 0; i < candidates.size(); ++i) {
        if (candidates[i].isThumbnail) { bestIdx = i; break; }
        if (candidates[i].attachSize > candidates[bestIdx].attachSize)
            bestIdx = i;
    }
    const std::string& imgElem = candidates[bestIdx].elemText;

    std::string geometry  = findAttr(imgElem, "geometry");
    std::string location  = findAttr(imgElem, "location");
    std::string sampleFmt = findAttr(imgElem, "sampleFormat");
    std::string colorSpace = findAttr(imgElem, "colorSpace");

    if (geometry.empty() || location.empty()) {
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_THUMBNAIL,
            L"ThumbnailDecodeFailed Stage=MissingGeometryOrLocation");
        return nullptr;
    }

    // Parse geometry: "width:height:channels"
    UINT imgW = 0, imgH = 0, imgC = 1;
    {
        char* end = nullptr;
        imgW = static_cast<UINT>(std::strtoul(geometry.c_str(), &end, 10));
        if (end && *end == ':')
        {
            imgH = static_cast<UINT>(std::strtoul(end + 1, &end, 10));
            if (end && *end == ':')
                imgC = static_cast<UINT>(std::strtoul(end + 1, nullptr, 10));
        }
    }
    if (imgW == 0 || imgH == 0) {
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_THUMBNAIL,
            L"ThumbnailDecodeFailed Stage=InvalidGeometry Width=%u Height=%u Channels=%u",
            imgW, imgH, imgC);
        return nullptr;
    }

    // Parse location: "attachment:offset:size"
    ULONGLONG offset = 0, size = 0;
    if (location.compare(0, 11, "attachment:") == 0)
    {
        const char* p = location.c_str() + 11;
        char* end = nullptr;
        offset = std::strtoull(p, &end, 10);
        if (end && *end == ':')
            size = std::strtoull(end + 1, nullptr, 10);
    }
    if (size == 0) {
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_THUMBNAIL,
            L"ThumbnailDecodeFailed Stage=ZeroAttachmentSize");
        return nullptr;
    }

    // Determine bytes per sample
    bool isUInt16  = (sampleFmt == "UInt16" || sampleFmt.empty());
    bool isUInt8   = (sampleFmt == "UInt8");
    bool isFloat32 = (sampleFmt == "Float32");
    bool isFloat64 = (sampleFmt == "Float64");
    size_t bps     = isUInt8 ? 1 : isFloat32 ? 4 : isFloat64 ? 8 : 2;

    ULONGLONG pixelCount = static_cast<ULONGLONG>(imgW) * imgH * imgC;
    ULONGLONG expected   = pixelCount * bps;
    if (size < expected) {
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_THUMBNAIL,
            L"ThumbnailDecodeFailed Stage=AttachmentTooSmall Expected=%llu Actual=%llu",
            expected, size);
        return nullptr;
    }

    UINT readChannels = (imgC >= 3) ? 3 : 1;
    size_t channelPixels = static_cast<size_t>(imgW) * imgH;
    size_t readBytes = channelPixels * readChannels * bps;
    if (readBytes > 768ULL * 1024 * 1024)
    {
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_THUMBNAIL,
            L"ThumbnailDecodeFailed Stage=ExceedsMemoryGuard ReadBytes=%llu",
            static_cast<unsigned long long>(readBytes));
        return nullptr;
    }

    // Compute aspect-ratio-preserving thumbnail dimensions.
    // Return a non-square bitmap — Explorer handles centering natively.
    UINT thumbW, thumbH;
    if (imgW >= imgH) {
        thumbW = cx;
        thumbH = static_cast<UINT>(static_cast<float>(imgH) / imgW * cx + 0.5f);
        if (thumbH < 1) thumbH = 1;
    } else {
        thumbH = cx;
        thumbW = static_cast<UINT>(static_cast<float>(imgW) / imgH * cx + 0.5f);
        if (thumbW < 1) thumbW = 1;
    }

    // Strided-I/O downsample
    // Instead of loading the entire image into RAM, we stream rows through a
    // small buffer.  Every source row that maps to a thumbnail row is read and
    // averaged, producing artifact-free area-average downsampling.  Batched
    // consecutive-row reads keep I/O overhead low.
    size_t thumbPixels = static_cast<size_t>(thumbW) * thumbH;
    std::vector<std::vector<float>> thumbCh(readChannels, std::vector<float>(thumbPixels, 0.0f));

    // Pre-compute thumbnail column mapping (shared across all rows).
    struct Span { UINT s0, s1; };
    std::vector<Span> colMap(thumbW);
    for (UINT dx = 0; dx < thumbW; ++dx)
    {
        UINT sx0 = static_cast<UINT>(static_cast<float>(dx)     / thumbW * imgW);
        UINT sx1 = static_cast<UINT>(static_cast<float>(dx + 1) / thumbW * imgW);
        if (sx1 <= sx0) sx1 = sx0 + 1;
        if (sx1 > imgW) sx1 = imgW;
        colMap[dx] = {sx0, sx1};
    }

    // Build a flat sorted list of {sourceRow, thumbRow} for all sampled rows
    // across all thumbnail rows, then make one sequential pass per channel.
    struct RowJob { UINT srcRow; UINT thumbRow; UINT sampledCount; };
    std::vector<RowJob> jobs;
    jobs.reserve(static_cast<size_t>(imgH));  // one job per source row touched

    std::vector<UINT> rowSampleCounts(thumbH);
    for (UINT dy = 0; dy < thumbH; ++dy)
    {
        UINT sy0 = static_cast<UINT>(static_cast<float>(dy)     / thumbH * imgH);
        UINT sy1 = static_cast<UINT>(static_cast<float>(dy + 1) / thumbH * imgH);
        if (sy1 <= sy0) sy1 = sy0 + 1;
        if (sy1 > imgH) sy1 = imgH;

        UINT blockH = sy1 - sy0;
        rowSampleCounts[dy] = blockH;

        for (UINT y = sy0; y < sy1; ++y)
            jobs.push_back({y, dy, blockH});
    }

    // Sort by source row for sequential I/O (stable to preserve thumb-row order)
    std::stable_sort(jobs.begin(), jobs.end(),
        [](const RowJob& a, const RowJob& b) { return a.srcRow < b.srcRow; });

    // Identify contiguous runs of consecutive source rows.  Each run is read
    // with a single Seek + Read (one I/O call instead of N), dramatically
    // reducing the shcore.dll IStream overhead that dominated the profile.
    struct RowRun { UINT startRow; UINT rowCount; size_t jobStart; size_t jobCount; };
    std::vector<RowRun> runs;
    {
        size_t i = 0;
        while (i < jobs.size())
        {
            UINT runStart = jobs[i].srcRow;
            size_t jobStart = i;
            UINT prevRow = runStart;
            ++i;
            while (i < jobs.size() && jobs[i].srcRow <= prevRow + 1)
            {
                prevRow = jobs[i].srcRow;
                ++i;
            }
            UINT runRows = prevRow - runStart + 1;
            runs.push_back({runStart, runRows, jobStart, i - jobStart});
        }
    }

    // Row buffer: sized to hold the largest contiguous run of rows.
    // Typical: ~4 consecutive rows × 6224 × 2 = ~48 KB (vs 684 individual seeks).
    UINT maxRunRows = 1;
    for (const auto& run : runs)
        maxRunRows = (std::max)(maxRunRows, run.rowCount);

    size_t rowBytes = static_cast<size_t>(imgW) * bps;
    std::vector<uint8_t> rowBuf(static_cast<size_t>(maxRunRows) * rowBytes);

    for (UINT ch = 0; ch < readChannels; ++ch)
    {
        ULONGLONG chBase = offset + static_cast<ULONGLONG>(ch) * channelPixels * bps;
        float* thumbOut = thumbCh[ch].data();

        for (const auto& run : runs)
        {
            // Single seek + read for the entire contiguous run
            LARGE_INTEGER seekPos;
            seekPos.QuadPart = static_cast<LONGLONG>(chBase + static_cast<ULONGLONG>(run.startRow) * rowBytes);
            if (FAILED(m_pStream->Seek(seekPos, STREAM_SEEK_SET, nullptr))) {
                WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_THUMBNAIL,
                    L"ThumbnailDecodeFailed Stage=SeekFailed Channel=%u", ch);
                return nullptr;
            }

            ULONG runReadBytes = static_cast<ULONG>(run.rowCount * rowBytes);
            ULONG cbRead = 0;
            if (FAILED(m_pStream->Read(rowBuf.data(), runReadBytes, &cbRead))) {
                WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_THUMBNAIL,
                    L"ThumbnailDecodeFailed Stage=ReadFailed Channel=%u Bytes=%lu",
                    ch, runReadBytes);
                return nullptr;
            }
            if (cbRead < runReadBytes) {
                WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_THUMBNAIL,
                    L"ThumbnailDecodeFailed Stage=ShortRead Channel=%u Expected=%lu Actual=%lu",
                    ch, runReadBytes, cbRead);
                return nullptr;
            }

            // Process each job in this run — the row data is already in rowBuf
            for (size_t ji = run.jobStart; ji < run.jobStart + run.jobCount; ++ji)
            {
                const auto& job = jobs[ji];
                UINT rowInBuf = job.srcRow - run.startRow;
                const uint8_t* rowData = rowBuf.data() + static_cast<size_t>(rowInBuf) * rowBytes;

                size_t thumbIdx = static_cast<size_t>(job.thumbRow) * thumbW;
                float invSamples = 1.0f / job.sampledCount;

                if (isUInt16)
                {
                    const uint16_t* row = reinterpret_cast<const uint16_t*>(rowData);
                    constexpr float scale = 1.0f / 65535.0f;
                    for (UINT dx = 0; dx < thumbW; ++dx)
                    {
                        const auto [sx0, sx1] = colMap[dx];
                        float sum = 0.0f;
                        for (UINT x = sx0; x < sx1; ++x)
                            sum += row[x];
                        thumbOut[thumbIdx + dx] += sum * scale * invSamples / (sx1 - sx0);
                    }
                }
                else if (isUInt8)
                {
                    constexpr float scale = 1.0f / 255.0f;
                    for (UINT dx = 0; dx < thumbW; ++dx)
                    {
                        const auto [sx0, sx1] = colMap[dx];
                        float sum = 0.0f;
                        for (UINT x = sx0; x < sx1; ++x)
                            sum += rowData[x];
                        thumbOut[thumbIdx + dx] += sum * scale * invSamples / (sx1 - sx0);
                    }
                }
                else if (isFloat32)
                {
                    const float* row = reinterpret_cast<const float*>(rowData);
                    for (UINT dx = 0; dx < thumbW; ++dx)
                    {
                        const auto [sx0, sx1] = colMap[dx];
                        float sum = 0.0f;
                        for (UINT x = sx0; x < sx1; ++x)
                            sum += row[x];
                        thumbOut[thumbIdx + dx] += sum * invSamples / (sx1 - sx0);
                    }
                }
                else // Float64
                {
                    const double* row = reinterpret_cast<const double*>(rowData);
                    for (UINT dx = 0; dx < thumbW; ++dx)
                    {
                        const auto [sx0, sx1] = colMap[dx];
                        float sum = 0.0f;
                        for (UINT x = sx0; x < sx1; ++x)
                            sum += static_cast<float>(row[x]);
                        thumbOut[thumbIdx + dx] += sum * invSamples / (sx1 - sx0);
                    }
                }
            }
        }
    }

    // Per-channel percentile auto-stretch (1%–99%).
    // For mono, one stretch. For RGB, independent per-channel stretch preserves
    // color balance from any palette (Hubble SHO, HOO, LRGB, natural color, etc.)
    struct StretchParams { float lo, hi; };
    std::vector<StretchParams> stretch(readChannels);
    for (UINT ch = 0; ch < readChannels; ++ch)
    {
        std::vector<float> sorted(thumbCh[ch]);
        size_t loIdx = thumbPixels / 100;
        size_t hiIdx = thumbPixels - thumbPixels / 100 - 1;
        if (hiIdx <= loIdx) hiIdx = loIdx + 1;
        std::nth_element(sorted.begin(), sorted.begin() + loIdx, sorted.end());
        stretch[ch].lo = sorted[loIdx];
        std::nth_element(sorted.begin() + loIdx + 1,
                         sorted.begin() + hiIdx, sorted.end());
        stretch[ch].hi = sorted[hiIdx];
        if (stretch[ch].hi <= stretch[ch].lo)
            stretch[ch].hi = stretch[ch].lo + 1e-6f;
        }

    // Determine if data is linear and needs gamma correction for display.
    bool isLinear = (isFloat32 || isFloat64);
    if (colorSpace == "Gray" || colorSpace == "RGB") isLinear = true;
    if (colorSpace == "GraySRGB" || colorSpace == "RGBSRGB") isLinear = false;
    if (isUInt8) isLinear = false;
    constexpr float kInvGamma = 1.0f / 2.2f;

    // Build a 24-bit BGR pixel buffer (bottom-up for SetDIBits)
    UINT rowStride = ((thumbW * 3 + 3) & ~3u); // DWORD-aligned
    std::vector<uint8_t> bits(static_cast<size_t>(rowStride) * thumbH, 0);

    bool isColor = (readChannels >= 3);

    for (UINT dy = 0; dy < thumbH; ++dy)
    {
        UINT bmpY = (thumbH - 1) - dy; // bottom-up
        for (UINT dx = 0; dx < thumbW; ++dx)
        {
            size_t ti = dy * thumbW + dx;
            uint8_t* p = bits.data() + bmpY * rowStride + dx * 3;

            if (isColor)
            {
                // XISF planar: ch0=R, ch1=G, ch2=B → BGR output
                for (int c = 0; c < 3; ++c)
                {
                    float v = thumbCh[c][ti];
                    float n = (std::max)(0.0f, (std::min)(1.0f,
                              (v - stretch[c].lo) / (stretch[c].hi - stretch[c].lo)));
                    if (isLinear) n = powf(n, kInvGamma);
                    p[2 - c] = static_cast<uint8_t>(n * 255.0f + 0.5f); // R→[2], G→[1], B→[0]
                }
            }
            else
            {
                float v = thumbCh[0][ti];
                float n = (std::max)(0.0f, (std::min)(1.0f,
                          (v - stretch[0].lo) / (stretch[0].hi - stretch[0].lo)));
                if (isLinear) n = powf(n, kInvGamma);
                uint8_t byte = static_cast<uint8_t>(n * 255.0f + 0.5f);
                p[0] = p[1] = p[2] = byte; // BGR = grey
            }
        }
    }

    // Create a device-compatible bitmap (DDB) at actual aspect ratio
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmp = CreateCompatibleBitmap(hdcScreen, thumbW, thumbH);
    if (!hbmp) {
        WritePreviewHandlerTelemetry(TRACE_LEVEL_WARNING, XISF_PREVIEW_KEYWORD_THUMBNAIL,
            L"ThumbnailDecodeFailed Stage=CreateCompatibleBitmap Width=%u Height=%u",
            thumbW, thumbH);
        DeleteDC(hdcMem);
        ReleaseDC(nullptr, hdcScreen);
        return nullptr;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = static_cast<LONG>(thumbW);
    bmi.bmiHeader.biHeight      = static_cast<LONG>(thumbH); // bottom-up
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 24;
    bmi.bmiHeader.biCompression = BI_RGB;

    SetDIBits(hdcMem, hbmp, 0, thumbH, bits.data(), &bmi, DIB_RGB_COLORS);
    DeleteDC(hdcMem);
    ReleaseDC(nullptr, hdcScreen);

    WritePreviewHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_PREVIEW_KEYWORD_THUMBNAIL,
        L"ThumbnailDecoded ThumbW=%u ThumbH=%u ImgW=%u ImgH=%u Channels=%u SampleFormat=%hs",
        thumbW, thumbH, imgW, imgH, imgC,
        sampleFmt.empty() ? "UInt16" : sampleFmt.c_str());

    return hbmp;
}

// ---------------------------------------------------------------------------
// CreatePlaceholderBitmap
// ---------------------------------------------------------------------------
//
// Draws a dark-blue thumbnail with XISF metadata text.
// ---------------------------------------------------------------------------

HBITMAP CThumbnailProvider::CreatePlaceholderBitmap(UINT cx)
{
    WritePreviewHandlerTelemetry(TRACE_LEVEL_INFORMATION, XISF_PREVIEW_KEYWORD_FALLBACK,
        L"ThumbnailPlaceholderUsed RequestedSize=%u", cx);
    HDC hdcScreen = GetDC(nullptr);
    HDC hdcMem    = CreateCompatibleDC(hdcScreen);
    HBITMAP hbmp  = CreateCompatibleBitmap(hdcScreen, cx, cx);
    ReleaseDC(nullptr, hdcScreen);

    if (!hbmp) { DeleteDC(hdcMem); return nullptr; }

    HGDIOBJ hOld = SelectObject(hdcMem, hbmp);

    // Background: dark navy blue
    RECT rc = { 0, 0, static_cast<LONG>(cx), static_cast<LONG>(cx) };
    HBRUSH hBgBrush = CreateSolidBrush(RGB(10, 20, 60));
    FillRect(hdcMem, &rc, hBgBrush);
    DeleteObject(hBgBrush);

    // Choose a font that scales with thumbnail size
    int fontH = static_cast<int>(cx / 12);
    if (fontH < 8) fontH = 8;

    HFONT hFont = CreateFontW(-fontH, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                              CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                              DEFAULT_PITCH | FF_SWISS, L"Segoe UI");
    HGDIOBJ hOldFont = SelectObject(hdcMem, hFont);

    SetBkMode(hdcMem, TRANSPARENT);
    SetTextColor(hdcMem, RGB(180, 220, 255)); // light blue text

    // Header "XISF"
    int y = static_cast<int>(cx * 0.05);
    int x = static_cast<int>(cx * 0.05);
    int lineH = fontH + 2;

    TextOutW(hdcMem, x, y, L"XISF Image", 10);
    y += lineH * 2;

    SetTextColor(hdcMem, RGB(220, 220, 220)); // white-ish body text

    auto DrawLine = [&](const wchar_t* label, const std::string& val)
    {
        if (val.empty()) return;
        // Convert narrow to wide
        wchar_t wBuf[256] = {};
        MultiByteToWideChar(CP_UTF8, 0, val.c_str(),
                            static_cast<int>(val.size()), wBuf, 255);
        wchar_t line[512];
        swprintf_s(line, L"%s%s", label, wBuf);
        TextOutW(hdcMem, x, y, line, static_cast<int>(wcslen(line)));
        y += lineH;
    };

    DrawLine(L"Object:  ", m_metadata.getFITSValue("OBJECT"));
    DrawLine(L"Filter:  ", m_metadata.getFITSValue("FILTER"));
    DrawLine(L"Exp:     ", m_metadata.getFITSValue("EXPTIME"));
    DrawLine(L"Scope:   ", m_metadata.getFITSValue("TELESCOP"));
    DrawLine(L"Camera:  ", m_metadata.getFITSValue("INSTRUME"));
    DrawLine(L"Temp:    ", m_metadata.getFITSValue("CCD-TEMP"));

    SelectObject(hdcMem, hOldFont);
    DeleteObject(hFont);
    SelectObject(hdcMem, hOld);
    DeleteDC(hdcMem);

    return hbmp;
}
