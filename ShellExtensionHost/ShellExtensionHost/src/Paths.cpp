// Paths.cpp
#include "Paths.h"

#include <windows.h>
#include <shlobj.h>
#include <shlwapi.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ole32.lib")

namespace xisf::paths {

static std::wstring KnownFolder(REFKNOWNFOLDERID id)
{
    PWSTR pszPath = nullptr;
    HRESULT hr = SHGetKnownFolderPath(id, KF_FLAG_CREATE, nullptr, &pszPath);
    if (FAILED(hr) || !pszPath) return L"";
    std::wstring s(pszPath);
    CoTaskMemFree(pszPath);
    return s;
}

static void EnsureDir(const std::wstring& p)
{
    DWORD attrs = GetFileAttributesW(p.c_str());
    if (attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY))
        return;
    CreateDirectoryW(p.c_str(), nullptr);
}

std::wstring AppDataRoot()
{
    // Machine-wide catalog storage. MSI should provision this tree with ACLs that
    // allow standard users to update catalog files; we still attempt creation so
    // elevated/dev scenarios work without installer pre-provisioning.
    std::wstring base = KnownFolder(FOLDERID_ProgramData);
    if (base.empty()) return L"";
    std::wstring vendor = base + L"\\DennisPayne";
    EnsureDir(vendor);
    std::wstring p = vendor + L"\\XISFShellExtension";
    EnsureDir(p);
    return p;
}

std::wstring CatalogDir()
{
    std::wstring root = AppDataRoot();
    if (root.empty()) return L"";
    std::wstring p = root + L"\\catalogs";
    EnsureDir(p);
    return p;
}

std::wstring CatalogFile(const wchar_t* fileName)
{
    std::wstring dir = CatalogDir();
    if (dir.empty() || !fileName) return L"";
    return dir + L"\\" + fileName;
}

std::wstring CatalogMetadataFile()
{
    std::wstring dir = CatalogDir();
    if (dir.empty()) return L"";
    return dir + L"\\catalog-metadata.tsv";
}

} // namespace xisf::paths
