// Sha256.h - Thin wrapper over Windows CNG SHA-256.
#pragma once

#include <windows.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace xisf {

// Streaming SHA-256 hasher using BCryptHashData (Windows built-in).
// Not thread-safe. Returns HRESULT for all failure modes.
class Sha256Hasher {
public:
    Sha256Hasher();
    ~Sha256Hasher();
    Sha256Hasher(const Sha256Hasher&) = delete;
    Sha256Hasher& operator=(const Sha256Hasher&) = delete;

    // Must be called before Update/Finalize. Safe to call once per object.
    HRESULT Init();

    HRESULT Update(const void* data, std::size_t len);

    // Writes 32 bytes to digest[] and invalidates the hasher.
    HRESULT Finalize(std::array<std::uint8_t, 32>& digest);

private:
    void* m_alg   = nullptr; // BCRYPT_ALG_HANDLE
    void* m_hash  = nullptr; // BCRYPT_HASH_HANDLE
};

// Returns 64-char lowercase hex representation.
std::wstring ToHexLower(const std::array<std::uint8_t, 32>& digest);

// Compares hex strings case-insensitively. Returns false for length mismatch.
bool HexEquals(std::wstring_view a, std::wstring_view b);

// Convenience: hash an entire file. Returns S_OK and fills digest/fileSize on
// success. File is opened shared-read so it coexists with a MoveFile from
// another handle.
HRESULT HashFile(const wchar_t* path,
                 std::array<std::uint8_t, 32>& digest,
                 std::uint64_t& fileSize);

} // namespace xisf
