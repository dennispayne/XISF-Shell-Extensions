// CatalogSpec.h - Pinned, integrity-verified catalog sources.
//
// SECURITY MODEL
// --------------
// We download catalog files ONLY from fixed URLs pinned to an immutable git
// commit SHA at raw.githubusercontent.com. The SHA-256 of the bytes at that
// commit is compiled in below and verified after every download (and every
// offline file import that uses the verified path). Any mismatch is fatal:
// the candidate file is deleted and the install aborts.
//
// To rotate to a newer OpenNGC snapshot:
//   1. Pick a commit SHA on https://github.com/mattiaverga/OpenNGC
//   2. Download each file at that commit and recompute SHA-256
//   3. Update kOpenNGCCommit and the kExpectedSha256 entries below
//   4. Bump CHANGELOG.md and version.json
//
// Sharpless/constellations are generated at runtime from public VizieR sources.
// Their source hash display is "N/A" because the upstream response is dynamic.
//
// DO NOT add URLs pointing to mutable refs (branches, tags). A pinned commit
// SHA is cryptographically stable; master / main is not.
#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <string_view>

namespace xisf::catalogspec {

// OpenNGC commit pinned for this release. See CHANGELOG for rotation history.
inline constexpr std::wstring_view kOpenNGCCommit =
    L"36cb178a0f69dba8bfc03a99c10512831edf1c6b";
inline constexpr std::wstring_view kOpenNGCCommitDate = L"2026-04-16";

struct CatalogSource {
    // Display name shown in the settings UI.
    std::wstring_view displayName;
    // Destination file name under %ProgramData%\DennisPayne\XISFShellExtension\catalogs\.
    std::wstring_view fileName;
    // Fully-qualified HTTPS URL used for online install/update attempts.
    std::wstring_view url;
    // Human-facing upstream source URL shown in the Settings app.
    std::wstring_view sourceUrl;
    // Expected SHA-256 of the file contents, lower-case hex, 64 chars.
    std::wstring_view expectedSha256;
    // Hash shown in the Source column. Use "N/A" for dynamic/webapp sources.
    std::wstring_view sourceHashDisplay;
    // Hard ceiling on download size (bytes). Reject anything larger before hashing.
    std::uint64_t maxBytes;
};

inline constexpr std::wstring_view kSourceHashNA = L"N/A";

// NOTE: sizes below are generous upper bounds, not exact. Exact verification
// happens via SHA-256. The cap exists only to limit DoS from a compromised CDN.
inline constexpr CatalogSource kNGC {
    L"OpenNGC NGC.csv",
    L"NGC.csv",
    L"https://raw.githubusercontent.com/mattiaverga/OpenNGC/"
    L"36cb178a0f69dba8bfc03a99c10512831edf1c6b/database_files/NGC.csv",
    L"https://github.com/mattiaverga/OpenNGC/blob/"
    L"36cb178a0f69dba8bfc03a99c10512831edf1c6b/database_files/NGC.csv",
    L"840fe0c9ee1332e551b2e722a0e92726cd7b157914a3d2177602832aadd3aa9e",
    L"840fe0c9ee1332e551b2e722a0e92726cd7b157914a3d2177602832aadd3aa9e",
    8ull * 1024ull * 1024ull
};

inline constexpr CatalogSource kAddendum {
    L"OpenNGC addendum.csv",
    L"addendum.csv",
    L"https://raw.githubusercontent.com/mattiaverga/OpenNGC/"
    L"36cb178a0f69dba8bfc03a99c10512831edf1c6b/database_files/addendum.csv",
    L"https://github.com/mattiaverga/OpenNGC/blob/"
    L"36cb178a0f69dba8bfc03a99c10512831edf1c6b/database_files/addendum.csv",
    L"1d8f0914e643ada325a5a94d88d8fefad6a4937a2f77cc34f21483af22b11983",
    L"1d8f0914e643ada325a5a94d88d8fefad6a4937a2f77cc34f21483af22b11983",
    1ull * 1024ull * 1024ull
};

// Sharpless HII-region catalog source. Runtime generator converts this
// machine-readable response into OpenNGC-style semicolon CSV.
inline constexpr CatalogSource kSharpless {
    L"Sharpless sharpless.csv",
    L"sharpless.csv",
    L"https://vizier.cds.unistra.fr/viz-bin/asu-tsv"
    L"?-source=VII/20/catalog"
    L"&-out.max=unlimited"
    L"&-out.add=_RAJ2000,_DEJ2000"
    L"&-out=Sh2,Diam,Form,Struct,Bright",
    L"https://cdsarc.cds.unistra.fr/viz-bin/cat/VII/20",
    L"3452cd838e2c9252a0b99ceb2c9c222ad4cbf38f3770cebd251e85dac725c081",
    kSourceHashNA,
    4ull * 1024ull * 1024ull
};

// IAU constellation boundaries source (Roman 1987).
// Runtime generator converts this machine-readable response into the
// ConstellationDB B/N comma format.
inline constexpr CatalogSource kConstellations {
    L"IAU Constellations constellations.csv",
    L"constellations.csv",
    L"https://vizier.cds.unistra.fr/viz-bin/asu-tsv"
    L"?-source=VI/42/data"
    L"&-out.max=unlimited",
    L"https://cdsarc.cds.unistra.fr/viz-bin/cat/VI/42",
    L"9e742f498fc6f355df37ff941c3d3adfcb3d759b05d7e0760a7b85822b5c074b",
    kSourceHashNA,
    1ull * 1024ull * 1024ull
};

inline constexpr std::array<const CatalogSource*, 4> kAllCatalogs = {
    &kNGC, &kAddendum, &kSharpless, &kConstellations
};

// Host allow-list (full URL prefixes). Any URL not starting with one of these
// entries is rejected by InstallFromPinnedUrl. Extend only for new trusted hosts.
inline constexpr std::array<std::wstring_view, 3> kAllowedUrlPrefixes = {{
    L"https://raw.githubusercontent.com/mattiaverga/OpenNGC/",
    L"https://vizier.cds.unistra.fr/viz-bin/asu-tsv?",
    L"https://vizier.cds.unistra.fr/viz-bin/asu-tsv",
}};

} // namespace xisf::catalogspec
