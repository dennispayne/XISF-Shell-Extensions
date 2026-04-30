#pragma once

#include <string>
#include <filesystem>
#include <system_error>
#include <vector>

namespace xisf::hostpaths {

enum class HandlerType { Property, Preview, Filter };

inline std::wstring ResolveHandlerDllPath(const std::wstring& solutionRoot,
                                          HandlerType handler,
                                          const wchar_t* configuration)
{
    namespace fs = std::filesystem;

    if (solutionRoot.empty() || configuration == nullptr || configuration[0] == 0)
        return L"";

    fs::path rootPath(solutionRoot);
    std::vector<fs::path> candidates;

    switch (handler) {
    case HandlerType::Property:
        candidates.push_back(rootPath / L"x64" / configuration / L"XISFPropertyHandler.dll");
        candidates.push_back(rootPath / L"PropertyHandler" / L"XISFPropertyHandler" / L"x64" / configuration / L"XISFPropertyHandler.dll");
        break;
    case HandlerType::Preview:
        candidates.push_back(rootPath / L"x64" / configuration / L"XISFPreviewHandler.dll");
        candidates.push_back(rootPath / L"PreviewHandler" / L"XISFPreviewHandler" / L"x64" / configuration / L"XISFPreviewHandler.dll");
        break;
    case HandlerType::Filter:
        candidates.push_back(rootPath / L"x64" / configuration / L"XISFFilter.dll");
        candidates.push_back(rootPath / L"Filter" / L"XISFFilter" / L"x64" / configuration / L"XISFFilter.dll");
        break;
    }

    for (const auto& candidate : candidates) {
        std::error_code ec;
        if (fs::exists(candidate, ec)) {
            return candidate.wstring();
        }
    }

    return candidates.empty() ? std::wstring() : candidates.front().wstring();
}

// Legacy overload for backward compatibility
inline std::wstring ResolveHandlerDllPath(const std::wstring& solutionRoot,
                                          bool propertyHandler,
                                          const wchar_t* configuration)
{
    return ResolveHandlerDllPath(solutionRoot,
        propertyHandler ? HandlerType::Property : HandlerType::Preview,
        configuration);
}

} // namespace xisf::hostpaths
