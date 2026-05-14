// CatalogInstaller.cpp - WinHTTP download + local import (verified/unverified).
#include "CatalogInstaller.h"
#include "Sha256.h"
#include "Paths.h"

#include <windows.h>
#include <winhttp.h>
#include <shlwapi.h>
#include <string>
#include <vector>
#include <array>
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <fstream>
#include <cctype>
#include <cmath>
#include <cstdio>

#pragma comment(lib, "winhttp.lib")

namespace xisf::installer {

namespace {

constexpr DWORD kIoBufSize = 64 * 1024;

// Split https://host/path into host + path. Returns false on malformed URL.
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

    if (uc.nScheme != INTERNET_SCHEME_HTTPS) return false; // HTTPS only
    if (!uc.lpszHostName || uc.dwHostNameLength == 0) return false;

    host.assign(uc.lpszHostName, uc.dwHostNameLength);
    pathAndQuery.assign(uc.lpszUrlPath, uc.dwUrlPathLength);
    if (uc.lpszExtraInfo && uc.dwExtraInfoLength > 0)
        pathAndQuery.append(uc.lpszExtraInfo, uc.dwExtraInfoLength);
    return !host.empty();
}

// RAII for HINTERNET.
struct InetHandle {
    HINTERNET h = nullptr;
    InetHandle() = default;
    explicit InetHandle(HINTERNET x) : h(x) {}
    ~InetHandle() { if (h) WinHttpCloseHandle(h); }
    InetHandle(const InetHandle&) = delete;
    InetHandle& operator=(const InetHandle&) = delete;
    InetHandle(InetHandle&& o) noexcept : h(o.h) { o.h = nullptr; }
    InetHandle& operator=(InetHandle&& o) noexcept { if (this != &o) { if (h) WinHttpCloseHandle(h); h = o.h; o.h = nullptr; } return *this; }
    explicit operator bool() const { return h != nullptr; }
};

std::wstring MakeTempPath(const std::wstring& targetPath)
{
    FILETIME ft{};
    GetSystemTimeAsFileTime(&ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    DWORD pid = GetCurrentProcessId();
    wchar_t buf[64];
    swprintf_s(buf, L".%08lx-%016llx.tmp", pid, u.QuadPart);
    return targetPath + buf;
}

bool IsConstellationsFileName(std::wstring_view fileName)
{
    return _wcsicmp(std::wstring(fileName).c_str(), L"constellations.csv") == 0;
}

bool ContainsUnsafeControlBytes(std::string_view s)
{
    for (unsigned char ch : s) {
        if (ch == '\t' || ch == '\n' || ch == '\r') continue;
        if (ch < 0x20 || ch == 0x7F) return true;
    }
    return false;
}

std::string TrimAscii(std::string s)
{
    auto notSpace = [](unsigned char c) { return !std::isspace(c); };
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), notSpace));
    s.erase(std::find_if(s.rbegin(), s.rend(), notSpace).base(), s.end());
    return s;
}

std::vector<std::string> SplitChar(std::string_view s, char delim)
{
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        size_t pos = s.find(delim, start);
        if (pos == std::string_view::npos) {
            out.emplace_back(s.substr(start));
            break;
        }
        out.emplace_back(s.substr(start, pos - start));
        start = pos + 1;
    }
    return out;
}

bool TryParseDouble(const std::string& s, double& out)
{
    char* end = nullptr;
    out = std::strtod(s.c_str(), &end);
    return end && end != s.c_str();
}

std::string NormalizeLineEndings(std::string_view input)
{
    std::string out;
    out.reserve(input.size() + 8);
    for (size_t i = 0; i < input.size(); ++i) {
        char c = input[i];
        if (c == '\r') {
            if (i + 1 < input.size() && input[i + 1] == '\n') {
                continue;
            }
            out.push_back('\n');
            continue;
        }
        out.push_back(c);
    }
    return out;
}

bool ReadUtf8File(const std::wstring& path, std::string& out, std::wstring& err)
{
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        err = L"Failed to reopen temporary file";
        return false;
    }
    out.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (out.empty()) {
        err = L"Downloaded/loaded content is empty";
        return false;
    }
    if (ContainsUnsafeControlBytes(out)) {
        err = L"Content contains unsafe control bytes";
        return false;
    }
    return true;
}

bool WriteUtf8File(const std::wstring& path, const std::string& text, std::wstring& err)
{
    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        err = L"Failed to open temp file for transformed output";
        return false;
    }
    DWORD written = 0;
    const DWORD total = static_cast<DWORD>(text.size());
    const bool ok = WriteFile(hFile, text.data(), total, &written, nullptr) != 0 && written == total;
    FlushFileBuffers(hFile);
    CloseHandle(hFile);
    if (!ok) {
        err = L"Failed to write transformed output";
        return false;
    }
    return true;
}

bool CanonicalizeOpenNgcCsv(const std::string& content, std::string& normalized, std::wstring& err)
{
    auto text = NormalizeLineEndings(content);
    if (ContainsUnsafeControlBytes(text)) {
        err = L"OpenNGC CSV contains unsafe control bytes";
        return false;
    }

    std::istringstream in(text);
    std::string line;
    std::ostringstream out;
    bool sawHeader = false;
    size_t rowCount = 0;
    constexpr size_t kMaxLineLen = 8192;
    constexpr size_t kMaxRows = 2'000'000;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.size() > kMaxLineLen) {
            err = L"OpenNGC CSV line length exceeds safety cap";
            return false;
        }
        if (!sawHeader) {
            sawHeader = true;
            if (line.find("Name;Type;RA;Dec;Const") != 0) {
                err = L"OpenNGC CSV header is missing or malformed";
                return false;
            }
            out << line << '\n';
            continue;
        }
        const auto fields = SplitChar(line, ';');
        if (fields.size() < 5) {
            err = L"OpenNGC CSV row has too few columns";
            return false;
        }
        const auto primary = TrimAscii(fields[0]);
        const auto ra = TrimAscii(fields[2]);
        const auto dec = TrimAscii(fields[3]);
        if (primary.empty() || ra.find(':') == std::string::npos || dec.find(':') == std::string::npos) {
            err = L"OpenNGC CSV row failed basic harmlessness checks";
            return false;
        }
        out << line << '\n';
        ++rowCount;
        if (rowCount > kMaxRows) {
            err = L"OpenNGC CSV row count exceeds safety cap";
            return false;
        }
    }
    if (!sawHeader || rowCount == 0) {
        err = L"OpenNGC CSV has no data rows";
        return false;
    }
    normalized = out.str();
    return true;
}

bool ParseAsuTsv(const std::string& tsv, std::vector<std::string>& columns, std::vector<std::vector<std::string>>& rows)
{
    auto text = NormalizeLineEndings(tsv);
    std::istringstream in(text);
    std::string line;
    enum class State { Header, Units, Separator, Data };
    State state = State::Header;

    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || (!line.empty() && line.front() == '#')) continue;

        auto fields = SplitChar(line, '\t');
        if (state == State::Header) {
            columns = std::move(fields);
            state = State::Units;
            continue;
        }
        if (state == State::Units) {
            state = State::Separator;
            continue;
        }
        if (state == State::Separator) {
            state = State::Data;
            bool allDashes = true;
            for (const auto& f : fields) {
                auto t = TrimAscii(f);
                if (t.empty() || t.find_first_not_of('-') != std::string::npos) {
                    allDashes = false;
                    break;
                }
            }
            if (allDashes) continue;
        }
        if (state == State::Data) {
            if (fields.size() != columns.size()) continue;
            rows.push_back(std::move(fields));
        }
    }
    return !columns.empty() && !rows.empty();
}

int FindColumn(const std::vector<std::string>& columns, std::initializer_list<const char*> names)
{
    for (const char* rawName : names) {
        std::string name(rawName);
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        for (size_t i = 0; i < columns.size(); ++i) {
            std::string col = columns[i];
            std::transform(col.begin(), col.end(), col.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (col == name) return static_cast<int>(i);
        }
    }
    return -1;
}

std::string DegToHms(double raDeg)
{
    while (raDeg < 0.0) raDeg += 360.0;
    while (raDeg >= 360.0) raDeg -= 360.0;
    double totalSeconds = raDeg / 15.0 * 3600.0;
    int h = static_cast<int>(totalSeconds / 3600.0);
    totalSeconds -= h * 3600.0;
    int m = static_cast<int>(totalSeconds / 60.0);
    double s = totalSeconds - m * 60.0;
    if (s >= 59.995) { s = 0.0; ++m; if (m == 60) { m = 0; h = (h + 1) % 24; } }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%05.2f", h, m, s);
    return buf;
}

std::string DegToDms(double decDeg)
{
    const char sign = decDeg < 0 ? '-' : '+';
    double absDec = std::abs(decDeg);
    double totalSeconds = absDec * 3600.0;
    int d = static_cast<int>(totalSeconds / 3600.0);
    totalSeconds -= d * 3600.0;
    int m = static_cast<int>(totalSeconds / 60.0);
    double s = totalSeconds - m * 60.0;
    if (s >= 59.95) { s = 0.0; ++m; if (m == 60) { m = 0; ++d; } }
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%c%02d:%02d:%04.1f", sign, d, m, s);
    return buf;
}

bool GenerateSharplessFromVizierTsv(const std::string& tsv, std::string& csv, std::wstring& err)
{
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
    if (!ParseAsuTsv(tsv, columns, rows)) {
        err = L"Sharpless source parse failed";
        return false;
    }

    const int idxSh2 = FindColumn(columns, {"Sh2", "sh2"});
    const int idxRa = FindColumn(columns, {"_RAJ2000", "RAJ2000"});
    const int idxDec = FindColumn(columns, {"_DEJ2000", "DEJ2000"});
    const int idxDiam = FindColumn(columns, {"Diam", "diam"});
    if (idxSh2 < 0 || idxRa < 0 || idxDec < 0 || idxDiam < 0) {
        err = L"Sharpless source is missing required columns";
        return false;
    }

    std::ostringstream out;
    out << "Name;Type;RA;Dec;Const;MajAx;MinAx;PosAng;B-Mag;V-Mag;J-Mag;H-Mag;K-Mag;SurfBr;Hubble;Pax;Pm-RA;Pm-Dec;RadVel;Redshift;Cstar-U;Cstar-B;Cstar-V;M;NGC;IC;CstarNames;Identifiers;Common names;NED notes;OpenNGC notes\n";
    size_t produced = 0;
    for (const auto& r : rows) {
        if (idxSh2 >= static_cast<int>(r.size()) || idxRa >= static_cast<int>(r.size()) || idxDec >= static_cast<int>(r.size()) || idxDiam >= static_cast<int>(r.size()))
            continue;
        const auto shRaw = TrimAscii(r[idxSh2]);
        const auto raRaw = TrimAscii(r[idxRa]);
        const auto decRaw = TrimAscii(r[idxDec]);
        const auto diamRaw = TrimAscii(r[idxDiam]);
        if (shRaw.empty() || raRaw.empty() || decRaw.empty()) continue;

        double raDeg = 0.0, decDeg = 0.0;
        if (!TryParseDouble(raRaw, raDeg) || !TryParseDouble(decRaw, decDeg)) continue;

        int shNum = 0;
        try {
            shNum = std::stoi(shRaw);
        } catch (...) {
            continue;
        }
        const std::string name = "Sh2-" + std::to_string(shNum);
        out << name << ";HII;" << DegToHms(raDeg) << ';' << DegToDms(decDeg)
            << ";;" << diamRaw
            << ";;;;;;;;;;;;;;;;;;;;;;;" << name << ";;;Sharpless 1959 (VizieR VII/20)\n";
        ++produced;
    }
    if (produced < 200) {
        err = L"Sharpless generator produced too few rows";
        return false;
    }
    csv = out.str();
    return true;
}

const std::array<std::pair<const char*, const char*>, 88> kConstellationNames = {{
    {"And","Andromeda"},{"Ant","Antlia"},{"Aps","Apus"},{"Aqr","Aquarius"},{"Aql","Aquila"},{"Ara","Ara"},{"Ari","Aries"},{"Aur","Auriga"},
    {"Boo","Bootes"},{"Cae","Caelum"},{"Cam","Camelopardalis"},{"Cnc","Cancer"},{"CVn","Canes Venatici"},{"CMa","Canis Major"},{"CMi","Canis Minor"},{"Cap","Capricornus"},
    {"Car","Carina"},{"Cas","Cassiopeia"},{"Cen","Centaurus"},{"Cep","Cepheus"},{"Cet","Cetus"},{"Cha","Chamaeleon"},{"Cir","Circinus"},{"Col","Columba"},
    {"Com","Coma Berenices"},{"CrA","Corona Australis"},{"CrB","Corona Borealis"},{"Crv","Corvus"},{"Crt","Crater"},{"Cru","Crux"},{"Cyg","Cygnus"},{"Del","Delphinus"},
    {"Dor","Dorado"},{"Dra","Draco"},{"Equ","Equuleus"},{"Eri","Eridanus"},{"For","Fornax"},{"Gem","Gemini"},{"Gru","Grus"},{"Her","Hercules"},
    {"Hor","Horologium"},{"Hya","Hydra"},{"Hyi","Hydrus"},{"Ind","Indus"},{"Lac","Lacerta"},{"Leo","Leo"},{"LMi","Leo Minor"},{"Lep","Lepus"},
    {"Lib","Libra"},{"Lup","Lupus"},{"Lyn","Lynx"},{"Lyr","Lyra"},{"Men","Mensa"},{"Mic","Microscopium"},{"Mon","Monoceros"},{"Mus","Musca"},
    {"Nor","Norma"},{"Oct","Octans"},{"Oph","Ophiuchus"},{"Ori","Orion"},{"Pav","Pavo"},{"Peg","Pegasus"},{"Per","Perseus"},{"Phe","Phoenix"},
    {"Pic","Pictor"},{"Psc","Pisces"},{"PsA","Piscis Austrinus"},{"Pup","Puppis"},{"Pyx","Pyxis"},{"Ret","Reticulum"},{"Sge","Sagitta"},{"Sgr","Sagittarius"},
    {"Sco","Scorpius"},{"Scl","Sculptor"},{"Sct","Scutum"},{"Ser","Serpens"},{"Sex","Sextans"},{"Tau","Taurus"},{"Tel","Telescopium"},{"Tri","Triangulum"},
    {"TrA","Triangulum Australe"},{"Tuc","Tucana"},{"UMa","Ursa Major"},{"UMi","Ursa Minor"},{"Vel","Vela"},{"Vir","Virgo"},{"Vol","Volans"},{"Vul","Vulpecula"}
}};

bool GenerateConstellationsFromRomanTsv(const std::string& tsv, std::string& csv, std::wstring& err)
{
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
    if (!ParseAsuTsv(tsv, columns, rows)) {
        err = L"Constellation source parse failed";
        return false;
    }
    const int idxRaLow = FindColumn(columns, {"RA_low", "ra_low"});
    const int idxRaUp = FindColumn(columns, {"RA_up", "ra_up"});
    const int idxDecLow = FindColumn(columns, {"DE_low", "de_low"});
    const int idxConst = FindColumn(columns, {"const", "cst"});
    if (idxRaLow < 0 || idxRaUp < 0 || idxDecLow < 0 || idxConst < 0) {
        err = L"Constellation source is missing required columns";
        return false;
    }

    std::ostringstream out;
    out << "# Generated from VizieR VI/42 (Roman 1987)\n";
    size_t produced = 0;
    for (const auto& r : rows) {
        if (idxRaLow >= static_cast<int>(r.size()) || idxRaUp >= static_cast<int>(r.size()) || idxDecLow >= static_cast<int>(r.size()) || idxConst >= static_cast<int>(r.size()))
            continue;
        double raLow = 0.0, raUp = 0.0, decLow = 0.0;
        if (!TryParseDouble(TrimAscii(r[idxRaLow]), raLow) ||
            !TryParseDouble(TrimAscii(r[idxRaUp]), raUp) ||
            !TryParseDouble(TrimAscii(r[idxDecLow]), decLow)) {
            continue;
        }
        std::string abbr = TrimAscii(r[idxConst]);
        if (abbr.empty()) continue;
        out << "B," << TrimAscii(r[idxRaLow]) << "," << TrimAscii(r[idxRaUp]) << "," << TrimAscii(r[idxDecLow]) << "," << abbr << "\n";
        ++produced;
    }
    if (produced < 300) {
        err = L"Constellation generator produced too few boundary rows";
        return false;
    }
    for (const auto& [abbr, name] : kConstellationNames) {
        out << "N," << abbr << "," << name << "\n";
    }
    csv = out.str();
    return true;
}

bool ValidateAndNormalizeImportedCsv(std::wstring_view targetFileName, const std::string& raw, std::string& normalized, std::wstring& err)
{
    if (IsConstellationsFileName(targetFileName)) {
        auto text = NormalizeLineEndings(raw);
        if (ContainsUnsafeControlBytes(text)) {
            err = L"Constellations CSV contains unsafe control bytes";
            return false;
        }
        std::istringstream in(text);
        std::string line;
        size_t bRows = 0;
        std::ostringstream out;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            auto t = TrimAscii(line);
            if (t.empty() || t[0] == '#') continue;
            auto fields = SplitChar(t, ',');
            if (fields.empty()) continue;
            if (fields[0] == "B") {
                if (fields.size() < 5) { err = L"Constellations B row malformed"; return false; }
                double v = 0.0;
                if (!TryParseDouble(TrimAscii(fields[1]), v) || !TryParseDouble(TrimAscii(fields[2]), v) || !TryParseDouble(TrimAscii(fields[3]), v)) {
                    err = L"Constellations B row has invalid numeric values";
                    return false;
                }
                ++bRows;
            } else if (fields[0] == "N") {
                if (fields.size() < 3 || TrimAscii(fields[1]).empty() || TrimAscii(fields[2]).empty()) {
                    err = L"Constellations N row malformed";
                    return false;
                }
            } else {
                err = L"Constellations CSV row type must be B or N";
                return false;
            }
            out << t << '\n';
        }
        if (bRows == 0) { err = L"Constellations CSV contains no boundary rows"; return false; }
        normalized = out.str();
        return true;
    }
    return CanonicalizeOpenNgcCsv(raw, normalized, err);
}

Result StreamToTempFile(HINTERNET hReq,
                        const std::wstring& tempPath,
                        std::uint64_t maxBytes,
                        ProgressFn progress, void* user,
                        std::array<std::uint8_t, 32>& digest,
                        std::uint64_t& bytesTransferred,
                        std::wstring& errDetail)
{
    HANDLE hFile = CreateFileW(tempPath.c_str(),
                               GENERIC_WRITE, 0, nullptr,
                               CREATE_ALWAYS,
                               FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                               nullptr);
    if (hFile == INVALID_HANDLE_VALUE) {
        errDetail = L"CreateFile(temp) failed";
        return Result::WriteFailed;
    }

    Sha256Hasher hasher;
    if (FAILED(hasher.Init())) {
        CloseHandle(hFile);
        DeleteFileW(tempPath.c_str());
        return Result::HashInitFailed;
    }

    std::vector<BYTE> buf(kIoBufSize);
    bytesTransferred = 0;

    for (;;) {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hReq, &avail)) {
            errDetail = L"QueryDataAvailable failed";
            CloseHandle(hFile); DeleteFileW(tempPath.c_str());
            return Result::HttpRequestFailed;
        }
        if (avail == 0) break;

        DWORD toRead = (avail < kIoBufSize) ? avail : kIoBufSize;
        DWORD got = 0;
        if (!WinHttpReadData(hReq, buf.data(), toRead, &got)) {
            errDetail = L"ReadData failed";
            CloseHandle(hFile); DeleteFileW(tempPath.c_str());
            return Result::HttpRequestFailed;
        }
        if (got == 0) break;

        bytesTransferred += got;
        if (bytesTransferred > maxBytes) {
            errDetail = L"Response exceeded pinned max size cap";
            CloseHandle(hFile); DeleteFileW(tempPath.c_str());
            return Result::SizeExceeded;
        }

        if (FAILED(hasher.Update(buf.data(), got))) {
            CloseHandle(hFile); DeleteFileW(tempPath.c_str());
            return Result::HashFailed;
        }

        DWORD written = 0;
        if (!WriteFile(hFile, buf.data(), got, &written, nullptr) || written != got) {
            errDetail = L"WriteFile failed";
            CloseHandle(hFile); DeleteFileW(tempPath.c_str());
            return Result::WriteFailed;
        }

        if (progress && !progress(bytesTransferred, maxBytes, user)) {
            CloseHandle(hFile); DeleteFileW(tempPath.c_str());
            return Result::OperationCancelled;
        }
    }

    if (FAILED(hasher.Finalize(digest))) {
        CloseHandle(hFile); DeleteFileW(tempPath.c_str());
        return Result::HashFailed;
    }

    FlushFileBuffers(hFile);
    CloseHandle(hFile);
    return Result::Ok;
}

Result StreamLocalFileToTemp(const wchar_t* sourcePath,
                             const std::wstring& tempPath,
                             std::uint64_t maxBytes,
                             ProgressFn progress, void* user,
                             std::array<std::uint8_t, 32>& digest,
                             std::uint64_t& bytesTransferred,
                             std::wstring& errDetail)
{
    HANDLE hSrc = CreateFileW(sourcePath, GENERIC_READ, FILE_SHARE_READ,
                              nullptr, OPEN_EXISTING,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                              nullptr);
    if (hSrc == INVALID_HANDLE_VALUE) {
        errDetail = L"Cannot open source file";
        return Result::SourceOpenFailed;
    }

    HANDLE hDst = CreateFileW(tempPath.c_str(),
                              GENERIC_WRITE, 0, nullptr,
                              CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                              nullptr);
    if (hDst == INVALID_HANDLE_VALUE) {
        CloseHandle(hSrc);
        errDetail = L"CreateFile(temp) failed";
        return Result::WriteFailed;
    }

    Sha256Hasher hasher;
    if (FAILED(hasher.Init())) {
        CloseHandle(hSrc); CloseHandle(hDst); DeleteFileW(tempPath.c_str());
        return Result::HashInitFailed;
    }

    std::vector<BYTE> buf(kIoBufSize);
    bytesTransferred = 0;

    for (;;) {
        DWORD got = 0;
        if (!ReadFile(hSrc, buf.data(), kIoBufSize, &got, nullptr)) {
            errDetail = L"ReadFile(src) failed";
            CloseHandle(hSrc); CloseHandle(hDst); DeleteFileW(tempPath.c_str());
            return Result::SourceOpenFailed;
        }
        if (got == 0) break;

        bytesTransferred += got;
        if (bytesTransferred > maxBytes) {
            errDetail = L"Source exceeded pinned max size cap";
            CloseHandle(hSrc); CloseHandle(hDst); DeleteFileW(tempPath.c_str());
            return Result::SizeExceeded;
        }

        if (FAILED(hasher.Update(buf.data(), got))) {
            CloseHandle(hSrc); CloseHandle(hDst); DeleteFileW(tempPath.c_str());
            return Result::HashFailed;
        }

        DWORD written = 0;
        if (!WriteFile(hDst, buf.data(), got, &written, nullptr) || written != got) {
            CloseHandle(hSrc); CloseHandle(hDst); DeleteFileW(tempPath.c_str());
            errDetail = L"WriteFile(temp) failed";
            return Result::WriteFailed;
        }

        if (progress && !progress(bytesTransferred, maxBytes, user)) {
            CloseHandle(hSrc); CloseHandle(hDst); DeleteFileW(tempPath.c_str());
            return Result::OperationCancelled;
        }
    }

    if (FAILED(hasher.Finalize(digest))) {
        CloseHandle(hSrc); CloseHandle(hDst); DeleteFileW(tempPath.c_str());
        return Result::HashFailed;
    }
    FlushFileBuffers(hDst);
    CloseHandle(hSrc);
    CloseHandle(hDst);
    return Result::Ok;
}

bool IsSafeCatalogFileName(const wchar_t* fileName)
{
    if (!fileName || !*fileName) return false;
    const wchar_t* base = PathFindFileNameW(fileName);
    if (!base || !*base) return false;
    if (wcscmp(base, fileName) != 0) return false;
    if (wcschr(base, L'\\') || wcschr(base, L'/')) return false;
    if (wcschr(base, L':')) return false;
    return true;
}

bool IsReparsePointPath(const wchar_t* path)
{
    DWORD attrs = GetFileAttributesW(path);
    if (attrs == INVALID_FILE_ATTRIBUTES) return false;
    return (attrs & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}

Result FinalizeAtomic(const std::wstring& tempPath, const std::wstring& targetPath,
                      std::wstring& errDetail)
{
    if (!MoveFileExW(tempPath.c_str(), targetPath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
    {
        DWORD gle = GetLastError();
        wchar_t tmp[64];
        swprintf_s(tmp, L"MoveFileEx failed (Win32 %lu)", gle);
        errDetail = tmp;
        DeleteFileW(tempPath.c_str());
        return Result::MoveFailed;
    }
    return Result::Ok;
}

} // namespace

Report InstallFromPinnedUrl(const catalogspec::CatalogSource& src,
                            ProgressFn progress, void* user)
{
    Report rep{};

    // Allow-list check: URL must begin with one of the compiled-in prefixes.
    {
        bool allowed = false;
        for (const auto& prefix : catalogspec::kAllowedUrlPrefixes) {
            if (src.url.size() >= prefix.size() &&
                std::wstring_view(src.url.data(), prefix.size()) == prefix) {
                allowed = true;
                break;
            }
        }
        if (!allowed) {
            rep.result = Result::UrlNotAllowed;
            rep.errorDetail = L"URL not in compiled allow-list";
            return rep;
        }
    }

    std::wstring targetDir = paths::CatalogDir();
    if (targetDir.empty()) {
        rep.result = Result::CatalogDirUnavailable;
        return rep;
    }
    std::wstring targetPath = targetDir + L"\\" + std::wstring(src.fileName);
    std::wstring tempPath   = MakeTempPath(targetPath);

    std::wstring host, objectPath;
    std::wstring urlStr(src.url);
    if (!CrackUrl(urlStr, host, objectPath)) {
        rep.result = Result::HttpOpenFailed;
        rep.errorDetail = L"Malformed URL";
        return rep;
    }

    InetHandle hSession(WinHttpOpen(L"XISFShellExtensionHost/1.0",
                                    WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                    WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0));
    if (!hSession) { rep.result = Result::HttpOpenFailed; return rep; }

    // Enforce modern TLS only (12 and 13 where available). No fallback to old protocols.
    DWORD secureProtocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
#ifdef WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3
    secureProtocols |= WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_3;
#endif
    WinHttpSetOption(hSession.h, WINHTTP_OPTION_SECURE_PROTOCOLS,
                     &secureProtocols, sizeof(secureProtocols));

    // Reasonable timeouts: resolve 10s, connect 15s, send 30s, receive 60s.
    WinHttpSetTimeouts(hSession.h, 10'000, 15'000, 30'000, 60'000);

    InetHandle hConnect(WinHttpConnect(hSession.h, host.c_str(),
                                       INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!hConnect) { rep.result = Result::HttpConnectFailed; return rep; }

    InetHandle hReq(WinHttpOpenRequest(hConnect.h, L"GET", objectPath.c_str(), nullptr,
                                       WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES,
                                       WINHTTP_FLAG_SECURE));
    if (!hReq) { rep.result = Result::HttpOpenFailed; return rep; }

    // Defense-in-depth: reject any proxy-controlled cert override.
    DWORD secFlags = 0;
    DWORD sfSize = sizeof(secFlags);
    WinHttpQueryOption(hReq.h, WINHTTP_OPTION_SECURITY_FLAGS, &secFlags, &sfSize);
    // Do NOT set SECURITY_FLAG_IGNORE_* -- we want full cert validation.

    if (!WinHttpSendRequest(hReq.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0))
    {
        rep.result = Result::HttpRequestFailed;
        rep.errorDetail = L"SendRequest failed";
        return rep;
    }
    if (!WinHttpReceiveResponse(hReq.h, nullptr)) {
        rep.result = Result::HttpRequestFailed;
        rep.errorDetail = L"ReceiveResponse failed";
        return rep;
    }

    DWORD status = 0, statusSize = sizeof(status);
    WinHttpQueryHeaders(hReq.h,
                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                        WINHTTP_NO_HEADER_INDEX);
    if (status != 200) {
        rep.result = Result::HttpBadStatus;
        rep.httpStatus = status;
        return rep;
    }

    std::array<std::uint8_t, 32> digest{};
    std::uint64_t written = 0;
    std::wstring err;
    Result r = StreamToTempFile(hReq.h, tempPath, src.maxBytes,
                                progress, user, digest, written, err);
    rep.bytesTransferred = written;
    rep.errorDetail = err;
    if (r != Result::Ok) { rep.result = r; return rep; }

    std::wstring gotHex = ToHexLower(digest);
    rep.computedHash = gotHex;
    const bool pinExpected = _wcsicmp(src.sourceHashDisplay.data(), catalogspec::kSourceHashNA.data()) != 0;
    if (pinExpected && !HexEquals(gotHex, src.expectedSha256)) {
        DeleteFileW(tempPath.c_str());
        rep.result = Result::HashMismatch;
        return rep;
    }

    std::string downloaded;
    if (!ReadUtf8File(tempPath, downloaded, rep.errorDetail)) {
        DeleteFileW(tempPath.c_str());
        rep.result = Result::InvalidContent;
        return rep;
    }

    std::string transformed;
    bool transformOk = false;
    if (_wcsicmp(src.fileName.data(), L"sharpless.csv") == 0) {
        transformOk = GenerateSharplessFromVizierTsv(downloaded, transformed, rep.errorDetail);
    } else if (IsConstellationsFileName(src.fileName)) {
        transformOk = GenerateConstellationsFromRomanTsv(downloaded, transformed, rep.errorDetail);
    } else {
        transformOk = CanonicalizeOpenNgcCsv(downloaded, transformed, rep.errorDetail);
    }
    if (!transformOk) {
        DeleteFileW(tempPath.c_str());
        rep.result = Result::InvalidContent;
        return rep;
    }
    if (!WriteUtf8File(tempPath, transformed, rep.errorDetail)) {
        DeleteFileW(tempPath.c_str());
        rep.result = Result::WriteFailed;
        return rep;
    }

    r = FinalizeAtomic(tempPath, targetPath, rep.errorDetail);
    rep.result = r;
    return rep;
}

Report InstallFromLocalFileVerified(const catalogspec::CatalogSource& src,
                                    const wchar_t* sourcePath,
                                    ProgressFn progress, void* user)
{
    Report rep{};
    std::wstring targetDir = paths::CatalogDir();
    if (targetDir.empty()) { rep.result = Result::CatalogDirUnavailable; return rep; }
    std::wstring targetPath = targetDir + L"\\" + std::wstring(src.fileName);
    std::wstring tempPath   = MakeTempPath(targetPath);

    std::array<std::uint8_t, 32> digest{};
    std::uint64_t written = 0;
    Result r = StreamLocalFileToTemp(sourcePath, tempPath, src.maxBytes,
                                     progress, user, digest, written,
                                     rep.errorDetail);
    rep.bytesTransferred = written;
    if (r != Result::Ok) { rep.result = r; return rep; }

    std::wstring gotHex = ToHexLower(digest);
    rep.computedHash = gotHex;
    if (!HexEquals(gotHex, src.expectedSha256)) {
        DeleteFileW(tempPath.c_str());
        rep.result = Result::HashMismatch;
        return rep;
    }

    rep.result = FinalizeAtomic(tempPath, targetPath, rep.errorDetail);
    return rep;
}

Report InstallFromLocalFileUnverified(const wchar_t* targetFileName,
                                      const wchar_t* sourcePath,
                                      std::uint64_t maxBytes,
                                      ProgressFn progress, void* user)
{
    Report rep{};
    if (!sourcePath || !*sourcePath || !IsSafeCatalogFileName(targetFileName)) {
        rep.result = Result::SourceOpenFailed;
        rep.errorDetail = L"Invalid source or target filename";
        return rep;
    }

    if (IsReparsePointPath(sourcePath)) {
        rep.result = Result::SourceOpenFailed;
        rep.errorDetail = L"Source file cannot be a reparse point";
        return rep;
    }

    std::wstring targetDir = paths::CatalogDir();
    if (targetDir.empty()) { rep.result = Result::CatalogDirUnavailable; return rep; }
    if (IsReparsePointPath(targetDir.c_str())) {
        rep.result = Result::CatalogDirUnavailable;
        rep.errorDetail = L"Catalog directory cannot be a reparse point";
        return rep;
    }

    std::wstring targetPath = targetDir + L"\\" + std::wstring(targetFileName);
    std::wstring tempPath   = MakeTempPath(targetPath);

    std::array<std::uint8_t, 32> digest{};
    std::uint64_t written = 0;
    Result r = StreamLocalFileToTemp(sourcePath, tempPath, maxBytes,
                                     progress, user, digest, written,
                                     rep.errorDetail);
    rep.bytesTransferred = written;
    if (r != Result::Ok) { rep.result = r; return rep; }

    rep.computedHash = ToHexLower(digest);

    std::string imported;
    if (!ReadUtf8File(tempPath, imported, rep.errorDetail)) {
        DeleteFileW(tempPath.c_str());
        rep.result = Result::InvalidContent;
        return rep;
    }
    std::string normalized;
    if (!ValidateAndNormalizeImportedCsv(targetFileName, imported, normalized, rep.errorDetail)) {
        DeleteFileW(tempPath.c_str());
        rep.result = Result::InvalidContent;
        return rep;
    }
    if (!WriteUtf8File(tempPath, normalized, rep.errorDetail)) {
        DeleteFileW(tempPath.c_str());
        rep.result = Result::WriteFailed;
        return rep;
    }

    rep.result = FinalizeAtomic(tempPath, targetPath, rep.errorDetail);
    return rep;
}

Presence Probe(const catalogspec::CatalogSource& src)
{
    Presence p{};
    std::wstring path = paths::CatalogFile(std::wstring(src.fileName).c_str());
    if (path.empty()) return p;

    WIN32_FILE_ATTRIBUTE_DATA fad{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) {
        p.state = PresenceState::Missing;
        return p;
    }
    ULARGE_INTEGER sz;
    sz.LowPart  = fad.nFileSizeLow;
    sz.HighPart = fad.nFileSizeHigh;
    p.sizeBytes = sz.QuadPart;

    std::array<std::uint8_t, 32> digest{};
    std::uint64_t n = 0;
    if (FAILED(HashFile(path.c_str(), digest, n))) {
        p.state = PresenceState::PresentUnknown;
        return p;
    }
    p.computedHash = ToHexLower(digest);
    if (_wcsicmp(src.sourceHashDisplay.data(), catalogspec::kSourceHashNA.data()) == 0) {
        p.state = PresenceState::PresentVerified;
    } else {
        p.state = HexEquals(p.computedHash, src.expectedSha256)
                    ? PresenceState::PresentVerified
                    : PresenceState::PresentMismatch;
    }
    return p;
}

} // namespace xisf::installer
