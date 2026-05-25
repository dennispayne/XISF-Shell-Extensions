// UpdaterInternals.h - Pure-logic helpers from the updater that can be
// unit-tested without network access. These are included directly in the
// test project and in the .cpp files that use them.
//
// This header is intentionally only included from UpdaterCheck.cpp,
// UpdaterDownload.cpp, and the test project.
#pragma once

#include "UpdaterSpec.h"
#include <string>
#include <string_view>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace xisf::updater::internals {

// ── Semver ───────────────────────────────────────────────────────────────────

struct SemVer { int major = 0, minor = 0, patch = 0; };

// Parses "1.2.3", "v1.2.3", "1.2.3.0" (4th component ignored).
// Returns false if at least one component could not be parsed.
inline bool ParseSemVer(const std::wstring& s, SemVer& v)
{
    std::wstring t = s;
    if (!t.empty() && (t[0] == L'v' || t[0] == L'V')) t = t.substr(1);
    auto dash = t.find(L'-');
    if (dash != std::wstring::npos) t = t.substr(0, dash);
    int parsed = swscanf_s(t.c_str(), L"%d.%d.%d", &v.major, &v.minor, &v.patch);
    return parsed >= 1;
}

// Returns >0 if a > b, 0 if equal, <0 if a < b.
inline int CompareSemVer(const SemVer& a, const SemVer& b)
{
    if (a.major != b.major) return a.major - b.major;
    if (a.minor != b.minor) return a.minor - b.minor;
    return a.patch - b.patch;
}

// ── Minimal JSON extraction ──────────────────────────────────────────────────

// Extracts the value of "key":"<value>" (first occurrence, no nested object
// awareness). Returns false if not found.
inline bool ExtractJsonString(const std::string& json, const std::string& key,
                               std::string& value)
{
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    while (pos != std::string::npos) {
        size_t colon = json.find(':', pos + needle.size());
        if (colon == std::string::npos) break;
        size_t q1 = json.find('"', colon + 1);
        if (q1 == std::string::npos) break;
        size_t q2 = q1 + 1;
        while (q2 < json.size()) {
            if (json[q2] == '\\') { q2 += 2; continue; }
            if (json[q2] == '"') break;
            ++q2;
        }
        if (q2 >= json.size()) break;
        value = json.substr(q1 + 1, q2 - q1 - 1);
        return true;
    }
    return false;
}

// Extracts "key": true|false.
inline bool ExtractJsonBool(const std::string& json, const std::string& key,
                             bool& value)
{
    std::string needle = "\"" + key + "\"";
    size_t pos = json.find(needle);
    if (pos == std::string::npos) return false;
    size_t colon = json.find(':', pos + needle.size());
    if (colon == std::string::npos) return false;
    size_t v = json.find_first_not_of(" \t\r\n", colon + 1);
    if (v == std::string::npos) return false;
    if (json.compare(v, 4, "true") == 0) { value = true; return true; }
    if (json.compare(v, 5, "false") == 0) { value = false; return true; }
    return false;
}

// ── Asset filename matching ──────────────────────────────────────────────────

// Returns true if name matches kAssetPrefix + <anything> + kAssetSuffix.
inline bool IsExpectedMsiName(const std::string& name)
{
    const std::string_view prefix = kAssetPrefixNarrow;
    const std::string_view suffix = kAssetSuffixNarrow;
    if (name.size() <= prefix.size() + suffix.size()) return false;
    return name.compare(0, prefix.size(), prefix) == 0 &&
           name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// ── SHA256SUMS.txt parsing ───────────────────────────────────────────────────

// Parses a SHA256SUMS.txt file and returns the 64-char lowercase hex for the
// given filename. Returns empty string if not found.
inline std::wstring ParseChecksumFile(const std::string& content,
                                       const std::wstring& fileName)
{
    // MSI filenames are always ASCII; cast each wchar_t explicitly.
    std::string narrowName;
    narrowName.reserve(fileName.size());
    for (wchar_t c : fileName) narrowName += static_cast<char>(c);
    std::istringstream ss(content);
    std::string line;
    while (std::getline(ss, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.size() < 64 + 2) continue;
        std::string hash = line.substr(0, 64);
        // Verify the hash is all hex.
        bool allHex = true;
        for (char c : hash)
            if (!std::isxdigit(static_cast<unsigned char>(c))) { allHex = false; break; }
        if (!allHex) continue;
        size_t nameStart = line.find_first_not_of(" *", 64);
        if (nameStart == std::string::npos) continue;
        std::string name = line.substr(nameStart);
        size_t slash = name.rfind('/');
        if (slash != std::string::npos) name = name.substr(slash + 1);
        slash = name.rfind('\\');
        if (slash != std::string::npos) name = name.substr(slash + 1);
        if (_stricmp(name.c_str(), narrowName.c_str()) == 0) {
            std::transform(hash.begin(), hash.end(), hash.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return std::wstring(hash.begin(), hash.end());
        }
    }
    return {};
}

// ── Host allow-list ──────────────────────────────────────────────────────────

inline bool IsAllowedUpdateHost(const std::wstring& host)
{
    auto eq = [&](std::wstring_view s) {
        return _wcsicmp(host.c_str(), std::wstring(s).c_str()) == 0;
    };
    return eq(kGitHubApiHost) || eq(kGitHubDownloadHost) || eq(kGitHubCdnHost);
}

} // namespace xisf::updater::internals
