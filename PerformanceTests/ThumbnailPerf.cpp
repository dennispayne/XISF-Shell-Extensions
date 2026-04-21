// ThumbnailPerf.cpp — Performance test for CThumbnailProvider using real XISF files.
//
// Place one or more .xisf files in C:\TestData\XISF (or set env var
// XISF_PERF_DATA_DIR to override).  The test iterates all .xisf files,
// calls GetThumbnail(256, ...) on each, and reports timing.
// If no files are found the test passes with an informational message.

#include "CppUnitTest.h"
#include <windows.h>
#include <initguid.h>
#include <shobjidl.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <filesystem>
#include "ThumbnailProvider.h"

#pragma comment(lib, "Ole32.lib")
#pragma comment(lib, "Shlwapi.lib")
#pragma comment(lib, "Gdi32.lib")

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace {

    std::wstring GetDataDir()
    {
        wchar_t buf[MAX_PATH]{};
        if (GetEnvironmentVariableW(L"XISF_PERF_DATA_DIR", buf, MAX_PATH) > 0)
            return buf;
        return L"C:\\TestData\\XISF";
    }

    std::vector<std::wstring> FindXISFFiles(const std::wstring& dir)
    {
        std::vector<std::wstring> files;
        if (!std::filesystem::is_directory(dir))
            return files;
        for (auto& entry : std::filesystem::directory_iterator(dir))
        {
            if (!entry.is_regular_file()) continue;
            auto ext = entry.path().extension().wstring();
            if (_wcsicmp(ext.c_str(), L".xisf") == 0)
                files.push_back(entry.path().wstring());
        }
        return files;
    }

    IStream* OpenFileStream(const std::wstring& path)
    {
        IStream* pStream = nullptr;
        HRESULT hr = SHCreateStreamOnFileEx(path.c_str(), STGM_READ | STGM_SHARE_DENY_WRITE,
                                            0, FALSE, nullptr, &pStream);
        return SUCCEEDED(hr) ? pStream : nullptr;
    }
}

namespace PreviewHandler_Performance {

    TEST_CLASS(ThumbnailPerformance)
    {
    public:

        TEST_METHOD(GetThumbnail_RealFiles_256px)
        {
            const UINT cx = 256;
            auto dir = GetDataDir();
            auto files = FindXISFFiles(dir);

            if (files.empty())
            {
                std::wstring msg = L"SKIPPED — no .xisf files in " + dir +
                    L".  Set XISF_PERF_DATA_DIR or place files in C:\\TestData\\XISF";
                Logger::WriteMessage(msg.c_str());
                return;
            }

            wchar_t hdr[512];
            swprintf_s(hdr, L"Found %zu .xisf files in %s", files.size(), dir.c_str());
            Logger::WriteMessage(hdr);

            LARGE_INTEGER freq;
            QueryPerformanceFrequency(&freq);

            double totalMs = 0.0;
            int succeeded = 0;

            for (const auto& filePath : files)
            {
                IStream* pStream = OpenFileStream(filePath);
                if (!pStream)
                {
                    Logger::WriteMessage((L"  SKIP (cannot open): " + filePath).c_str());
                    continue;
                }

                CThumbnailProvider* provider = new CThumbnailProvider();
                HRESULT hr = provider->Initialize(pStream, STGM_READ);
                if (FAILED(hr))
                {
                    pStream->Release();
                    provider->Release();
                    Logger::WriteMessage((L"  SKIP (init failed): " + filePath).c_str());
                    continue;
                }

                HBITMAP hbmp = nullptr;
                WTS_ALPHATYPE alpha = WTSAT_UNKNOWN;

                LARGE_INTEGER t0, t1;
                QueryPerformanceCounter(&t0);
                hr = provider->GetThumbnail(cx, &hbmp, &alpha);
                QueryPerformanceCounter(&t1);

                double ms = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1000.0 / freq.QuadPart;
                totalMs += ms;

                auto fileName = std::filesystem::path(filePath).filename().wstring();
                wchar_t line[512];
                swprintf_s(line, L"  %s  %.1f ms  %s",
                           SUCCEEDED(hr) && hbmp ? L"OK" : L"PLACEHOLDER",
                           ms, fileName.c_str());
                Logger::WriteMessage(line);

                if (SUCCEEDED(hr) && hbmp)
                {
                    ++succeeded;
                    DeleteObject(hbmp);
                }

                provider->Release();
                pStream->Release();
            }

            wchar_t summary[512];
            swprintf_s(summary,
                       L"\nSUMMARY: %d/%zu thumbnails in %.1f ms total (%.1f ms avg)",
                       succeeded, files.size(), totalMs,
                       files.size() > 0 ? totalMs / files.size() : 0.0);
            Logger::WriteMessage(summary);

            if (!files.empty())
                Assert::IsTrue(succeeded > 0, L"No real thumbnails produced from test files");
        }
    };
}
