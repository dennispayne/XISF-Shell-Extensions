#include "CppUnitTest.h"
#include <windows.h>
#include <initguid.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include "PropertyStore.h"
#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Propsys.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

// Stubs for DLL globals referenced by PropertyStore.cpp
long g_cDllRef = 0;
HINSTANCE g_hInst = nullptr;

// TraceLogging provider stubs — test module needs its own definitions since
// dllmain.cpp is not compiled into the test DLL.
#include "PropertyHandlerTraceLogging.h"
#include "PreviewHandlerTraceLogging.h"
TRACELOGGING_DEFINE_PROVIDER(g_hPropertyProvider, "XISF-PropertyHandler",
    (0x6f6b0c9d, 0x6b76, 0x5a24, 0xbc, 0x3d, 0x70, 0x83, 0x14, 0xe9, 0x6f, 0x2b));
TRACELOGGING_DEFINE_PROVIDER(g_hPreviewProvider, "XISF-PreviewHandler",
    (0x4fd34fd0, 0x08b3, 0x5d9a, 0x8d, 0x77, 0xb9, 0xd6, 0x70, 0x5d, 0x6b, 0x75));

namespace {
    const std::string kPerfXML = R"(<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">
  <Image geometry="4656:3520:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:0:0">
    <FITSKeyword name="EXPTIME" value="300.0" comment="Exposure time"/>
    <FITSKeyword name="INSTRUME" value="ASI2600MM Pro" comment="Camera"/>
    <FITSKeyword name="TELESCOP" value="RASA 8" comment="Telescope"/>
    <FITSKeyword name="FOCALLEN" value="400.0" comment="Focal length mm"/>
    <FITSKeyword name="FOCRATIO" value="2.0" comment="F-ratio"/>
    <FITSKeyword name="OBJECT" value="M42" comment="Target"/>
    <FITSKeyword name="FILTER" value="Ha" comment="Filter"/>
    <FITSKeyword name="IMAGETYP" value="Light" comment="Frame type"/>
    <FITSKeyword name="GAIN" value="100" comment="Gain"/>
    <FITSKeyword name="OFFSET" value="50" comment="Offset"/>
    <FITSKeyword name="CCD-TEMP" value="-10.0" comment="Sensor temp"/>
    <FITSKeyword name="XBINNING" value="2" comment="Binning X"/>
    <FITSKeyword name="YBINNING" value="2" comment="Binning Y"/>
    <FITSKeyword name="DATE-OBS" value="2024-12-20T03:00:00" comment="Date"/>
    <FITSKeyword name="SWCREATE" value="N.I.N.A." comment="Software"/>
    <FITSKeyword name="RA" value="83.82" comment="RA degrees"/>
    <FITSKeyword name="DEC" value="-5.39" comment="Dec degrees"/>
    <FITSKeyword name="SET-TEMP" value="-15.0" comment="Set temp"/>
    <FITSKeyword name="XPIXSZ" value="3.76" comment="Pixel size um"/>
    <FITSKeyword name="READOUTM" value="High Gain" comment="Readout mode"/>
    <FITSKeyword name="BAYERPAT" value="RGGB" comment="Bayer"/>
    <FITSKeyword name="SITELAT" value="33.45" comment="Latitude"/>
    <FITSKeyword name="SITELONG" value="-112.07" comment="Longitude"/>
    <FITSKeyword name="SITEELEV" value="340.0" comment="Elevation m"/>
    <FITSKeyword name="CENTALT" value="62.5" comment="Altitude"/>
    <FITSKeyword name="CENTAZ" value="180.3" comment="Azimuth"/>
    <FITSKeyword name="AIRMASS" value="1.23" comment="Airmass"/>
    <FITSKeyword name="PIERSIDE" value="West" comment="Pier side"/>
    <FITSKeyword name="OBJCTRA" value="05h 35m 17.3s" comment="Object RA"/>
    <FITSKeyword name="OBJCTDEC" value="-05d 23m 28s" comment="Object Dec"/>
    <FITSKeyword name="ROTATANG" value="12.5" comment="Rotation"/>
    <FITSKeyword name="FOCNAME" value="ZWO EAF" comment="Focuser"/>
    <FITSKeyword name="FOCPOS" value="12500" comment="Focuser pos"/>
    <FITSKeyword name="FOCTEMP" value="8.3" comment="Focuser temp"/>
    <FITSKeyword name="ROTNAME" value="Pegasus Falcon" comment="Rotator"/>
    <FITSKeyword name="ROTATOR" value="92.1" comment="Rotator angle"/>
    <FITSKeyword name="FWHEEL" value="ZWO 8pos" comment="Filter wheel"/>
    <FITSKeyword name="DEWPOINT" value="5.2" comment="Dew point"/>
    <FITSKeyword name="HUMIDITY" value="62.0" comment="Humidity"/>
    <FITSKeyword name="AMBTEMP" value="12.8" comment="Ambient temp"/>
    <FITSKeyword name="DATE-LOC" value="2024-12-19T20:00:00" comment="Local date"/>
    <Property id="Instrument:ExposureTime" type="Float64" value="300.0"/>
    <Property id="Observation:Object:Name" type="String" value="Orion Nebula"/>
    <Property id="Instrument:Filter" type="String" value="Ha"/>
  </Image>
</xisf>
)";

    IStream* CreateXISFStream(const std::string& xml) {
        std::vector<BYTE> buf;
        const char sig[] = "XISF0100";
        buf.insert(buf.end(), sig, sig + 8);
        uint32_t xmlLen = static_cast<uint32_t>(xml.size());
        auto* p = reinterpret_cast<const BYTE*>(&xmlLen);
        buf.insert(buf.end(), p, p + 4);
        uint32_t reserved = 0;
        p = reinterpret_cast<const BYTE*>(&reserved);
        buf.insert(buf.end(), p, p + 4);
        buf.insert(buf.end(), xml.begin(), xml.end());
        return SHCreateMemStream(buf.data(), static_cast<UINT>(buf.size()));
    }
}

namespace PropertyHandler_Performance {

    TEST_CLASS(InitializePerformance)
    {
    public:
        TEST_METHOD(Initialize_100Files_MeasuresThroughput)
        {
            const int kIterations = 100;
            LARGE_INTEGER freq, start, end;
            QueryPerformanceFrequency(&freq);
            QueryPerformanceCounter(&start);

            for (int i = 0; i < kIterations; ++i) {
                CXISFPropertyHandler* handler = new CXISFPropertyHandler();
                IStream* pStream = CreateXISFStream(kPerfXML);
                Assert::IsNotNull(pStream);
                HRESULT hr = handler->Initialize(pStream, STGM_READ);
                Assert::AreEqual(S_OK, hr);
                DWORD count = 0;
                handler->GetCount(&count);
                Assert::IsTrue(count > 30);
                pStream->Release();
                handler->Release();
            }

            QueryPerformanceCounter(&end);
            double elapsedMs = static_cast<double>(end.QuadPart - start.QuadPart) * 1000.0 / freq.QuadPart;
            double perFileMs = elapsedMs / kIterations;

            wchar_t msg[256];
            swprintf_s(msg, L"Total: %.1f ms for %d files (%.2f ms/file)", elapsedMs, kIterations, perFileMs);
            Logger::WriteMessage(msg);

            // Baseline expectation: < 5ms per file in test harness
            Assert::IsTrue(perFileMs < 50.0, L"Initialize too slow (>50ms/file)");
        }
    };
}
