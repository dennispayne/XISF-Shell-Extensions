// WinHttpHelpers.h - Shared RAII wrapper + URL cracker + TLS policy for WinHTTP.
#pragma once

#include <windows.h>
#include <winhttp.h>
#include <string>
#include <string_view>

namespace xisf::winhttp {

// Modern TLS only. Enforced via WinHttpSetOption(WINHTTP_OPTION_SECURE_PROTOCOLS).
static constexpr DWORD kTlsProtocols =
    WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    | WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
#endif
    ;

// RAII wrapper around HINTERNET.
struct InetHandle {
    HINTERNET h = nullptr;
    InetHandle() = default;
    explicit InetHandle(HINTERNET x) : h(x) {}
    ~InetHandle() { if (h) WinHttpCloseHandle(h); }
    InetHandle(const InetHandle&) = delete;
    InetHandle& operator=(const InetHandle&) = delete;
    InetHandle(InetHandle&& o) noexcept : h(o.h) { o.h = nullptr; }
    InetHandle& operator=(InetHandle&& o) noexcept {
        if (this != &o) {
            if (h) WinHttpCloseHandle(h);
            h = o.h;
            o.h = nullptr;
        }
        return *this;
    }
    explicit operator bool() const { return h != nullptr; }
};

// Cracks an HTTPS URL into host + path-and-query. HTTPS-only; returns false otherwise.
bool CrackUrl(std::wstring_view url, std::wstring& host, std::wstring& pathAndQuery);

} // namespace xisf::winhttp
