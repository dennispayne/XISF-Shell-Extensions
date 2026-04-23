// ConcurrencyStress.cpp — Thread-safety stress test for CXISFPropertyHandler.
//
// Validates the m_propertyLock mutex under real contention: N threads
// concurrently call GetCount, GetAt, and GetValue in a tight loop for
// one second, then asserts no crashes, consistent counts, and no HRESULT
// failures.  Reports total operations/second to Logger::WriteMessage.

#include "CppUnitTest.h"
#include <windows.h>
#include <initguid.h>
#include <shobjidl.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <algorithm>
#include "PropertyStore.h"

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Propsys.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Shell32.lib")

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace {
    const std::string kStressXML = R"(<?xml version="1.0" encoding="UTF-8"?>
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

    IStream* CreateStressStream(const std::string& xml) {
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

    TEST_CLASS(ConcurrencyStress)
    {
    public:

        TEST_METHOD(ConcurrentReads_NoRaces_NoCrashes)
        {
            // Initialize handler once
            CXISFPropertyHandler* handler = new CXISFPropertyHandler();
            IStream* pStream = CreateStressStream(kStressXML);
            Assert::IsNotNull(pStream, L"Failed to create stress stream");
            HRESULT hr = handler->Initialize(pStream, STGM_READ);
            Assert::AreEqual(S_OK, hr, L"Initialize failed");
            pStream->Release();

            DWORD expectedCount = 0;
            handler->GetCount(&expectedCount);
            Assert::IsTrue(expectedCount > 0, L"No properties after init");

            const unsigned threadCount = (std::min)(
                static_cast<unsigned>(std::thread::hardware_concurrency()), 8u);
            const DWORD durationMs = 1000;

            std::atomic<bool> running{ true };
            std::atomic<LONG> totalOps{ 0 };
            std::atomic<LONG> errorCount{ 0 };

            auto worker = [&](unsigned /*id*/) {
                LONG ops = 0;
                while (running.load(std::memory_order_relaxed)) {
                    DWORD count = 0;
                    HRESULT hrc = handler->GetCount(&count);
                    if (FAILED(hrc) || count != expectedCount) {
                        errorCount.fetch_add(1, std::memory_order_relaxed);
                    }

                    // Read all property keys
                    for (DWORD i = 0; i < count; ++i) {
                        PROPERTYKEY pk = {};
                        hrc = handler->GetAt(i, &pk);
                        if (FAILED(hrc)) {
                            errorCount.fetch_add(1, std::memory_order_relaxed);
                            continue;
                        }
                        PROPVARIANT pv = {};
                        PropVariantInit(&pv);
                        hrc = handler->GetValue(pk, &pv);
                        if (FAILED(hrc)) {
                            errorCount.fetch_add(1, std::memory_order_relaxed);
                        }
                        PropVariantClear(&pv);
                    }
                    ++ops;
                }
                totalOps.fetch_add(ops, std::memory_order_relaxed);
            };

            std::vector<std::thread> threads;
            threads.reserve(threadCount);
            for (unsigned i = 0; i < threadCount; ++i)
                threads.emplace_back(worker, i);

            Sleep(durationMs);
            running.store(false, std::memory_order_relaxed);

            for (auto& t : threads) t.join();

            handler->Release();

            wchar_t msg[256];
            swprintf_s(msg, L"Threads: %u, Total iterations: %ld, Errors: %ld, Ops/sec: %ld",
                threadCount, totalOps.load(), errorCount.load(),
                totalOps.load() * 1000 / durationMs);
            Logger::WriteMessage(msg);

            Assert::AreEqual(0L, errorCount.load(),
                L"Concurrent reads produced errors — possible data race");
        }
    };
}
