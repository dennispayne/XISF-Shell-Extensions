// MemoryLeakTest.cpp — CRT debug heap leak detection for handler lifecycles.
//
// Uses _CrtMemCheckpoint / _CrtMemDifference to verify that creating,
// initializing, enumerating, and releasing handler instances does not leak
// memory.  Active only in Debug builds (_DEBUG); in Release the test is a
// no-op with an informational skip message.

#include "CppUnitTest.h"
#include <windows.h>
#include <initguid.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <psapi.h>
#include "PropertyStore.h"

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Propsys.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")

#ifdef _DEBUG
#include <crtdbg.h>
#endif

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace {
    const std::string kLeakXML = R"(<?xml version="1.0" encoding="UTF-8"?>
<xisf version="1.0" xmlns="http://www.pixinsight.com/xisf">
  <Image geometry="4656:3520:1" sampleFormat="UInt16" colorSpace="Gray" location="attachment:0:0">
    <FITSKeyword name="EXPTIME" value="300.0" comment="Exposure time"/>
    <FITSKeyword name="INSTRUME" value="ASI2600MM Pro" comment="Camera"/>
    <FITSKeyword name="TELESCOP" value="RASA 8" comment="Telescope"/>
    <FITSKeyword name="OBJECT" value="M42" comment="Target"/>
    <FITSKeyword name="FILTER" value="Ha" comment="Filter"/>
    <FITSKeyword name="IMAGETYP" value="Light" comment="Frame type"/>
    <FITSKeyword name="GAIN" value="100" comment="Gain"/>
    <FITSKeyword name="DATE-OBS" value="2024-12-20T03:00:00" comment="Date"/>
    <FITSKeyword name="RA" value="83.82" comment="RA degrees"/>
    <FITSKeyword name="DEC" value="-5.39" comment="Dec degrees"/>
    <Property id="Observation:Object:Name" type="String" value="Orion Nebula"/>
  </Image>
</xisf>
)";

    IStream* CreateLeakStream(const std::string& xml) {
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

    SIZE_T GetWorkingSetBytes() {
        PROCESS_MEMORY_COUNTERS pmc = {};
        pmc.cb = sizeof(pmc);
        GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
        return pmc.WorkingSetSize;
    }
}

namespace PropertyHandler_Performance {

    TEST_CLASS(MemoryLeakDetection)
    {
    public:

        TEST_METHOD(PropertyHandler_100Lifecycles_NoLeak)
        {
#ifndef _DEBUG
            Logger::WriteMessage("SKIPPED — CRT debug heap not available in Release build");
            return;
#else
            const int kIterations = 100;

            // Warm up: one cycle to trigger static initialization (catalog, etc.)
            {
                auto* h = new CXISFPropertyHandler();
                IStream* s = CreateLeakStream(kLeakXML);
                h->Initialize(s, STGM_READ);
                s->Release();
                h->Release();
            }

            SIZE_T wsBefore = GetWorkingSetBytes();

            _CrtMemState before = {}, after = {}, diff = {};
            _CrtMemCheckpoint(&before);

            for (int i = 0; i < kIterations; ++i) {
                auto* h = new CXISFPropertyHandler();
                IStream* s = CreateLeakStream(kLeakXML);
                h->Initialize(s, STGM_READ);

                // Enumerate all properties (exercises PROPVARIANT allocation)
                DWORD count = 0;
                h->GetCount(&count);
                for (DWORD j = 0; j < count; ++j) {
                    PROPERTYKEY pk = {};
                    h->GetAt(j, &pk);
                    PROPVARIANT pv = {};
                    PropVariantInit(&pv);
                    h->GetValue(pk, &pv);
                    PropVariantClear(&pv);
                }

                s->Release();
                h->Release();
            }

            _CrtMemCheckpoint(&after);
            SIZE_T wsAfter = GetWorkingSetBytes();

            BOOL leaked = _CrtMemDifference(&diff, &before, &after);

            wchar_t msg[512];
            swprintf_s(msg,
                L"Iterations: %d, Leaked: %s, New blocks: %lld, New bytes: %lld, "
                L"Working set delta: %+lld KB",
                kIterations,
                leaked ? L"YES" : L"NO",
                static_cast<long long>(diff.lCounts[_NORMAL_BLOCK]),
                static_cast<long long>(diff.lSizes[_NORMAL_BLOCK]),
                static_cast<long long>(wsAfter - wsBefore) / 1024);
            Logger::WriteMessage(msg);

            // Allow a small tolerance for CRT internal bookkeeping, but
            // flag any _NORMAL_BLOCK growth as a real leak.
            Assert::IsTrue(diff.lCounts[_NORMAL_BLOCK] <= 0,
                L"Memory leak detected — new heap blocks after handler lifecycles");
#endif
        }

    };
}
