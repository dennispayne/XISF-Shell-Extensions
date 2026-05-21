// DSOAliasDB.cpp — Implementation of the Deep Sky Object alias database (Property Handler)
#include "DSOAliasDB.h"
#include "StringUtil.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>

namespace xisf {

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

bool DSOAliasDB::LoadFromFile(const std::string& jsonPath)
{
    std::ifstream file(jsonPath);
    if (!file.is_open())
        return false;

    std::ostringstream oss;
    oss << file.rdbuf();
    return LoadFromString(oss.str());
}

bool DSOAliasDB::LoadFromString(const std::string& jsonContent)
{
    m_entries.clear();
    m_index.clear();
    return ParseJSON(jsonContent);
}

std::vector<std::string> DSOAliasDB::GetAllNames(const std::string& objectName) const
{
    auto it = m_index.find(ToLower(objectName));
    if (it == m_index.end())
        return {};

    const DSOAliasEntry& e = m_entries[it->second];
    std::vector<std::string> result;
    result.reserve(1 + e.aliases.size());
    result.push_back(e.canonicalName);
    for (const auto& alias : e.aliases)
        result.push_back(alias);
    return result;
}

std::string DSOAliasDB::GetCanonicalName(const std::string& name) const
{
    auto it = m_index.find(ToLower(name));
    if (it == m_index.end())
        return {};
    return m_entries[it->second].canonicalName;
}

size_t DSOAliasDB::Count() const
{
    return m_entries.size();
}

// ---------------------------------------------------------------------------
// Index construction
// ---------------------------------------------------------------------------

void DSOAliasDB::BuildIndex()
{
    m_index.clear();
    for (size_t i = 0; i < m_entries.size(); ++i)
    {
        const DSOAliasEntry& e = m_entries[i];
        m_index[ToLower(e.canonicalName)] = i;
        for (const auto& alias : e.aliases)
            m_index[ToLower(alias)] = i;
    }
}

std::string DSOAliasDB::ToLower(const std::string& s)
{
    return xisf::str::ToLower(s);
}

// ---------------------------------------------------------------------------
// JSON parser helpers
// ---------------------------------------------------------------------------

void DSOAliasDB::SkipWhitespace(const std::string& s, size_t& pos)
{
    while (pos < s.size() && (s[pos] == ' ' || s[pos] == '\t' ||
                               s[pos] == '\n' || s[pos] == '\r'))
        ++pos;
}

bool DSOAliasDB::ReadString(const std::string& s, size_t& pos, std::string& out)
{
    if (pos >= s.size() || s[pos] != '"')
        return false;

    ++pos; // consume opening '"'
    out.clear();

    while (pos < s.size())
    {
        char c = s[pos];
        if (c == '"')
        {
            ++pos; // consume closing '"'
            return true;
        }
        if (c == '\\')
        {
            ++pos;
            if (pos >= s.size()) return false;
            char esc = s[pos];
            switch (esc)
            {
            case '"':  out += '"';  break;
            case '\\': out += '\\'; break;
            case '/':  out += '/';  break;
            case 'n':  out += '\n'; break;
            case 'r':  out += '\r'; break;
            case 't':  out += '\t'; break;
            case 'b':  out += '\b'; break;
            case 'f':  out += '\f'; break;
            default:   out += esc;  break;  // ignore unknown escapes
            }
        }
        else
        {
            out += c;
        }
        ++pos;
    }
    return false; // unterminated string
}

bool DSOAliasDB::SkipValue(const std::string& s, size_t& pos)
{
    SkipWhitespace(s, pos);
    if (pos >= s.size()) return false;

    char c = s[pos];

    if (c == '"')
    {
        std::string dummy;
        return ReadString(s, pos, dummy);
    }
    if (c == '{')
    {
        // Skip object
        ++pos;
        int depth = 1;
        while (pos < s.size() && depth > 0)
        {
            char ch = s[pos];
            if (ch == '"') { std::string d; ReadString(s, pos, d); continue; }
            if (ch == '{') ++depth;
            else if (ch == '}') --depth;
            ++pos;
        }
        return depth == 0;
    }
    if (c == '[')
    {
        // Skip array
        ++pos;
        int depth = 1;
        while (pos < s.size() && depth > 0)
        {
            char ch = s[pos];
            if (ch == '"') { std::string d; ReadString(s, pos, d); continue; }
            if (ch == '[') ++depth;
            else if (ch == ']') --depth;
            ++pos;
        }
        return depth == 0;
    }
    // Skip number / boolean / null
    while (pos < s.size())
    {
        char ch = s[pos];
        if (ch == ',' || ch == '}' || ch == ']' || ch == ' ' ||
            ch == '\t' || ch == '\n' || ch == '\r')
            break;
        ++pos;
    }
    return true;
}

// ---------------------------------------------------------------------------
// ParseJSON
//
// Parses a top-level JSON array of objects, each with the shape:
//   {
//     "canonicalName": "...",
//     "aliases": ["...", ...],
//     "type": "...",
//     "constellation": "..."
//   }
// ---------------------------------------------------------------------------

bool DSOAliasDB::ParseJSON(const std::string& json)
{
    size_t pos = 0;

    SkipWhitespace(json, pos);
    if (pos >= json.size() || json[pos] != '[')
        return false;
    ++pos; // consume '['

    while (true)
    {
        SkipWhitespace(json, pos);
        if (pos >= json.size()) return false;

        if (json[pos] == ']')
            break; // end of array

        if (json[pos] == ',') { ++pos; continue; }

        if (json[pos] != '{')
            return false;
        ++pos; // consume '{'

        DSOAliasEntry entry;

        // Parse key-value pairs inside the object
        while (true)
        {
            SkipWhitespace(json, pos);
            if (pos >= json.size()) return false;

            if (json[pos] == '}') { ++pos; break; } // end of object
            if (json[pos] == ',') { ++pos; continue; }

            // Read key
            std::string key;
            if (!ReadString(json, pos, key))
                return false;

            SkipWhitespace(json, pos);
            if (pos >= json.size() || json[pos] != ':') return false;
            ++pos; // consume ':'
            SkipWhitespace(json, pos);

            if (key == "canonicalName")
            {
                if (!ReadString(json, pos, entry.canonicalName))
                    return false;
            }
            else if (key == "type")
            {
                if (!ReadString(json, pos, entry.type))
                    return false;
            }
            else if (key == "constellation")
            {
                if (!ReadString(json, pos, entry.constellation))
                    return false;
            }
            else if (key == "aliases")
            {
                // Parse array of strings
                if (pos >= json.size() || json[pos] != '[') return false;
                ++pos;
                while (true)
                {
                    SkipWhitespace(json, pos);
                    if (pos >= json.size()) return false;
                    if (json[pos] == ']') { ++pos; break; }
                    if (json[pos] == ',') { ++pos; continue; }

                    std::string alias;
                    if (!ReadString(json, pos, alias))
                        return false;
                    entry.aliases.push_back(std::move(alias));
                }
            }
            else
            {
                // Unknown field – skip its value
                if (!SkipValue(json, pos))
                    return false;
            }
        }

        m_entries.push_back(std::move(entry));
    }

    BuildIndex();
    return true;
}

} // namespace xisf
