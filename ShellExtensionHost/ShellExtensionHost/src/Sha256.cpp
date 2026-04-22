// Sha256.cpp - CNG-backed SHA-256 implementation.
#include "Sha256.h"

#include <windows.h>
#include <bcrypt.h>
#include <cctype>

#pragma comment(lib, "bcrypt.lib")

namespace xisf {

Sha256Hasher::Sha256Hasher() = default;

Sha256Hasher::~Sha256Hasher()
{
    if (m_hash) {
        BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(m_hash));
        m_hash = nullptr;
    }
    if (m_alg) {
        BCryptCloseAlgorithmProvider(static_cast<BCRYPT_ALG_HANDLE>(m_alg), 0);
        m_alg = nullptr;
    }
}

HRESULT Sha256Hasher::Init()
{
    if (m_hash || m_alg) return E_UNEXPECTED;

    BCRYPT_ALG_HANDLE alg = nullptr;
    NTSTATUS st = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (st != 0) return HRESULT_FROM_NT(st);

    BCRYPT_HASH_HANDLE hash = nullptr;
    st = BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0);
    if (st != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        return HRESULT_FROM_NT(st);
    }

    m_alg  = alg;
    m_hash = hash;
    return S_OK;
}

HRESULT Sha256Hasher::Update(const void* data, std::size_t len)
{
    if (!m_hash) return E_UNEXPECTED;
    if (len == 0) return S_OK;
    if (len > 0x7fffffffu) return E_INVALIDARG;
    NTSTATUS st = BCryptHashData(static_cast<BCRYPT_HASH_HANDLE>(m_hash),
                                 const_cast<PUCHAR>(static_cast<const UCHAR*>(data)),
                                 static_cast<ULONG>(len), 0);
    return (st == 0) ? S_OK : HRESULT_FROM_NT(st);
}

HRESULT Sha256Hasher::Finalize(std::array<std::uint8_t, 32>& digest)
{
    if (!m_hash) return E_UNEXPECTED;
    NTSTATUS st = BCryptFinishHash(static_cast<BCRYPT_HASH_HANDLE>(m_hash),
                                   digest.data(),
                                   static_cast<ULONG>(digest.size()), 0);
    BCryptDestroyHash(static_cast<BCRYPT_HASH_HANDLE>(m_hash));
    m_hash = nullptr;
    return (st == 0) ? S_OK : HRESULT_FROM_NT(st);
}

std::wstring ToHexLower(const std::array<std::uint8_t, 32>& digest)
{
    static const wchar_t kHex[] = L"0123456789abcdef";
    std::wstring out;
    out.resize(64);
    for (std::size_t i = 0; i < digest.size(); ++i) {
        out[i * 2 + 0] = kHex[(digest[i] >> 4) & 0xF];
        out[i * 2 + 1] = kHex[(digest[i] >> 0) & 0xF];
    }
    return out;
}

bool HexEquals(std::wstring_view a, std::wstring_view b)
{
    if (a.size() != b.size()) return false;
    // Constant-time comparison to avoid leaking via timing. Overkill for a
    // local hash compare but costs nothing.
    unsigned diff = 0;
    for (std::size_t i = 0; i < a.size(); ++i) {
        wchar_t ca = a[i]; if (ca >= L'A' && ca <= L'Z') ca = wchar_t(ca + 32);
        wchar_t cb = b[i]; if (cb >= L'A' && cb <= L'Z') cb = wchar_t(cb + 32);
        diff |= static_cast<unsigned>(ca ^ cb);
    }
    return diff == 0;
}

HRESULT HashFile(const wchar_t* path,
                 std::array<std::uint8_t, 32>& digest,
                 std::uint64_t& fileSize)
{
    fileSize = 0;
    digest.fill(0);

    HANDLE h = CreateFileW(path, GENERIC_READ,
                           FILE_SHARE_READ,
                           nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) return HRESULT_FROM_WIN32(GetLastError());

    Sha256Hasher hasher;
    HRESULT hr = hasher.Init();
    if (FAILED(hr)) { CloseHandle(h); return hr; }

    constexpr DWORD kChunk = 64 * 1024;
    BYTE buf[kChunk];
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(h, buf, kChunk, &read, nullptr)) {
            hr = HRESULT_FROM_WIN32(GetLastError());
            CloseHandle(h);
            return hr;
        }
        if (read == 0) break;
        hr = hasher.Update(buf, read);
        if (FAILED(hr)) { CloseHandle(h); return hr; }
        fileSize += read;
    }
    CloseHandle(h);
    return hasher.Finalize(digest);
}

} // namespace xisf
