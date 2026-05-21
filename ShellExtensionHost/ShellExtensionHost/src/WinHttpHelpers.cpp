// WinHttpHelpers.cpp - Shared WinHTTP utilities.
#include "WinHttpHelpers.h"

#pragma comment(lib, "winhttp.lib")

namespace xisf::winhttp {

bool CrackUrl(std::wstring_view url, std::wstring& host, std::wstring& pathAndQuery)
{
    URL_COMPONENTS uc{};
    uc.dwStructSize      = sizeof(uc);
    uc.dwSchemeLength    = static_cast<DWORD>(-1);
    uc.dwHostNameLength  = static_cast<DWORD>(-1);
    uc.dwUrlPathLength   = static_cast<DWORD>(-1);
    uc.dwExtraInfoLength = static_cast<DWORD>(-1);

    if (!WinHttpCrackUrl(url.data(), static_cast<DWORD>(url.size()), 0, &uc))
        return false;

    if (uc.nScheme != INTERNET_SCHEME_HTTPS) return false;
    if (!uc.lpszHostName || uc.dwHostNameLength == 0) return false;

    host.assign(uc.lpszHostName, uc.dwHostNameLength);
    pathAndQuery.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength > 0)
        pathAndQuery.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    return !host.empty();
}

} // namespace xisf::winhttp
