#pragma once

#include <string>
#include <filesystem>
#include <system_error>
#include <vector>

namespace xisf::hostpaths {

inline std::wstring ResolveHandlerDllPath(const std::wstring& solutionRoot,
                                          bool propertyHandler,
                                          const wchar_t* configuration)
{
    namespace fs = std::filesystem;

    if (solutionRoot.empty() || configuration == nullptr || configuration[0] == 0)
        return L"";

    fs::path rootPath(solutionRoot);
    std::vector<fs::path> candidates;

    if (propertyHandler) {
        candidates.push_back(rootPath / L"x64" / configuration / L"XISFPropertyHandler.dll");
        candidates.push_back(rootPath / L"PropertyHandler" / L"XISFPropertyHandler" / L"x64" / configuration / L"XISFPropertyHandler.dll");
    }
    else {
        candidates.push_back(rootPath / L"x64" / configuration / L"XISFPreviewHandler.dll");
        candidates.push_back(rootPath / L"PreviewHandler" / L"XISFPreviewHandler" / L"x64" / configuration / L"XISFPreviewHandler.dll");
    }

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::exists(candidate, ec)) {
            return candidate.wstring();
        }
    }

    return candidates.empty() ? std::wstring() : candidates.front().wstring();
}

} // namespace xisf::hostpaths
