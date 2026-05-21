#include "XISFParser.h"

#include <windows.h>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <cstring>

namespace xisf {

static inline bool IsWhitespace(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r';
}

// XISFRawMetadata helpers

void XISFRawMetadata::buildIndices() {
    fitsIndex_.clear();
    fitsIndex_.reserve(fitsKeywords.size());
    for (size_t i = 0; i < fitsKeywords.size(); ++i) {
        std::string upper = fitsKeywords[i].name;
        std::transform(upper.begin(), upper.end(), upper.begin(),
                       [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
        fitsIndex_.emplace(std::move(upper), i);
    }

    propIndex_.clear();
    propIndex_.reserve(properties.size());
    for (size_t i = 0; i < properties.size(); ++i) {
        propIndex_.emplace(properties[i].id, i);
    }
}

std::string XISFRawMetadata::getFITSValue(const std::string& name) const {
    std::string upper = name;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    auto it = fitsIndex_.find(upper);
    if (it != fitsIndex_.end()) return fitsKeywords[it->second].value;
    return {};
}

std::string XISFRawMetadata::getPropertyValue(const std::string& id) const {
    auto it = propIndex_.find(id);
    if (it != propIndex_.end()) return properties[it->second].value;
    return {};
}

// XISFRawMetadata::GetSearchableTextChunks — produce wide-string text for IFilter

static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return {};
    int needed = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    if (needed <= 0) return {};
    std::wstring result(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), result.data(), needed);
    return result;
}

std::vector<std::wstring> XISFRawMetadata::GetSearchableTextChunks() const {
    std::vector<std::wstring> chunks;

    // FITS keywords: "NAME = VALUE / COMMENT"
    for (const auto& kw : fitsKeywords) {
        std::wstring chunk;
        chunk += Utf8ToWide(kw.name);
        if (!kw.value.empty()) {
            chunk += L" = ";
            chunk += Utf8ToWide(kw.value);
        }
        if (!kw.comment.empty()) {
            chunk += L" / ";
            chunk += Utf8ToWide(kw.comment);
        }
        if (!chunk.empty())
            chunks.push_back(std::move(chunk));
    }

    // XISF Properties: "id = value"
    for (const auto& prop : properties) {
        std::wstring chunk;
        chunk += Utf8ToWide(prop.id);
        if (!prop.value.empty()) {
            chunk += L" = ";
            chunk += Utf8ToWide(prop.value);
        }
        if (!chunk.empty())
            chunks.push_back(std::move(chunk));
    }

    // Image element attributes: "key = value"
    for (const auto& [key, val] : imageAttributes) {
        std::wstring chunk = Utf8ToWide(key) + L" = " + Utf8ToWide(val);
        if (!chunk.empty())
            chunks.push_back(std::move(chunk));
    }

    return chunks;
}

// XISFParser::ParseFile

ParseResult XISFParser::ParseFile(const std::string& filePath) {
    ParseResult result;

    std::ifstream file(filePath, std::ios::binary);
    if (!file.is_open()) {
        result.error        = ParseError::FileNotFound;
        result.errorMessage = "File not found or could not be opened: " + filePath;
        return result;
    }

    // Read the 16-byte binary preamble.
    char preamble[16] = {};
    file.read(preamble, 16);
    if (file.gcount() != 16) {
        result.error        = ParseError::ReadError;
        result.errorMessage = "Failed to read 16-byte preamble from: " + filePath;
        return result;
    }

    // Verify the 8-byte ASCII signature.
    static const char kSignature[8] = {'X','I','S','F','0','1','0','0'};
    if (std::memcmp(preamble, kSignature, 8) != 0) {
        result.error        = ParseError::InvalidSignature;
        result.errorMessage = "Invalid XISF signature in file: " + filePath;
        return result;
    }

    // Decode XML header length (little-endian uint32 at offset 8).
    uint32_t headerLength = 0;
    std::memcpy(&headerLength, preamble + 8, sizeof(uint32_t));

    if (headerLength > kMaxHeaderBytes) {
        result.error        = ParseError::HeaderTooLarge;
        result.errorMessage = "XML header length " + std::to_string(headerLength)
                              + " exceeds the safety limit of "
                              + std::to_string(kMaxHeaderBytes) + " bytes.";
        return result;
    }

    // Read the XML header bytes.
    std::string xml(headerLength, '\0');
    file.read(xml.data(), headerLength);
    if (static_cast<uint32_t>(file.gcount()) != headerLength) {
        result.error        = ParseError::ReadError;
        result.errorMessage = "Failed to read complete XML header ("
                              + std::to_string(headerLength) + " bytes) from: "
                              + filePath;
        return result;
    }

    return ParseXMLString(xml);
}

ParseResult XISFParser::ParseXMLString(const std::string& xmlContent) {
    return ExtractMetadataFromXML(xmlContent);
}

ParseResult XISFParser::ExtractMetadataFromXML(const std::string& xml) {
    ParseResult result;
    result.metadata.xmlHeader = xml;

    // Parse <FITSKeyword> elements.
    auto kwElements = FindElements(xml, "FITSKeyword");
    result.metadata.fitsKeywords.reserve(kwElements.size());
    for (const auto& attrs : kwElements) {
        FITSKeyword kw;
        auto it = attrs.find("name");
        if (it != attrs.end()) kw.name = it->second;

        it = attrs.find("value");
        if (it != attrs.end()) kw.value = it->second;

        it = attrs.find("comment");
        if (it != attrs.end()) kw.comment = it->second;

        result.metadata.fitsKeywords.push_back(std::move(kw));
    }

    // Parse <Property> elements.
    auto propElements = FindElements(xml, "Property");
    result.metadata.properties.reserve(propElements.size());
    for (const auto& attrs : propElements) {
        XISFProperty prop;
        auto it = attrs.find("id");
        if (it != attrs.end()) prop.id = it->second;

        it = attrs.find("type");
        if (it != attrs.end()) prop.type = it->second;

        it = attrs.find("value");
        if (it != attrs.end()) prop.value = it->second;

        result.metadata.properties.push_back(std::move(prop));
    }

    // Parse first <Image> element attributes (sampleFormat, colorSpace, etc.).
    auto imageElements = FindElements(xml, "Image");
    if (!imageElements.empty()) {
        result.metadata.imageAttributes = std::move(imageElements[0]);
    }

    result.metadata.buildIndices();

    return result;
}

std::size_t XISFParser::FindElementEnd(const std::string& xml, std::size_t start, bool& selfClosing) {
    selfClosing = false;
    std::size_t search = start;
    while (search < xml.size()) {
        std::size_t gtPos = xml.find('>', search);
        if (gtPos == std::string::npos) return std::string::npos;

        if (gtPos > 0 && xml[gtPos - 1] == '/') {
            selfClosing = true;
            return gtPos;
        }

        bool inSingleQuote = false;
        bool inDoubleQuote = false;
        for (std::size_t k = search; k < gtPos; ++k) {
            char c = xml[k];
            if (c == '\'' && !inDoubleQuote) inSingleQuote = !inSingleQuote;
            else if (c == '"' && !inSingleQuote) inDoubleQuote = !inDoubleQuote;
        }
        if (!inSingleQuote && !inDoubleQuote) {
            return gtPos;
        }
        search = gtPos + 1;
    }
    return std::string::npos;
}

std::unordered_map<std::string, std::string>
XISFParser::BuildAttributeMap(const std::string& attrText) {
    static const char* kCommonAttrs[] = {
        "name", "value", "comment",   // FITSKeyword
        "id",   "type",               // Property
        "geometry", "sampleFormat", "colorSpace", "location" // Image
    };

    std::unordered_map<std::string, std::string> attrMap;
    for (const char* attr : kCommonAttrs) {
        std::string val = GetAttribute(attrText, attr);
        if (!val.empty() || attrText.find(std::string(attr) + "=") != std::string::npos) {
            attrMap[attr] = val;
        }
    }
    return attrMap;
}

std::vector<std::unordered_map<std::string, std::string>>
XISFParser::FindElements(const std::string& xml, const std::string& tagName) {
    std::vector<std::unordered_map<std::string, std::string>> results;

    const std::string openTag = "<" + tagName;
    std::size_t pos = 0;

    while (pos < xml.size()) {
        std::size_t tagStart = xml.find(openTag, pos);
        if (tagStart == std::string::npos) break;

        // The character immediately after the tag name must be whitespace or
        // '/' or '>' to avoid false-positives (e.g. <FITSKeywordExtra>).
        std::size_t afterTagName = tagStart + openTag.size();
        if (afterTagName < xml.size()) {
            char next = xml[afterTagName];
            if (!IsWhitespace(next) && next != '/' && next != '>') {
                pos = afterTagName;
                continue;
            }
        }

        bool selfClosing = false;
        std::size_t elementEnd = FindElementEnd(xml, afterTagName, selfClosing);
        if (elementEnd == std::string::npos) {
            pos = afterTagName;
            continue;
        }

        std::size_t attrStart = afterTagName;
        std::size_t attrEnd   = selfClosing ? elementEnd - 1 : elementEnd;
        std::string attrText  = xml.substr(attrStart, attrEnd - attrStart);

        results.push_back(BuildAttributeMap(attrText));
        pos = elementEnd + 1;
    }

    return results;
}

static std::string DecodeXMLEntities(const std::string& s) {
    std::string result;
    result.reserve(s.size());
    std::size_t i = 0;
    while (i < s.size()) {
        if (s[i] == '&') {
            if (s.compare(i, 5, "&amp;") == 0)  { result += '&';  i += 5; continue; }
            if (s.compare(i, 4, "&lt;") == 0)   { result += '<';  i += 4; continue; }
            if (s.compare(i, 4, "&gt;") == 0)   { result += '>';  i += 4; continue; }
            if (s.compare(i, 6, "&quot;") == 0) { result += '"';  i += 6; continue; }
            if (s.compare(i, 6, "&apos;") == 0) { result += '\''; i += 6; continue; }
        }
        result += s[i++];
    }
    return result;
}

std::string XISFParser::GetAttribute(const std::string& elementText,
                                     const std::string& attrName) {
    std::size_t pos = 0;
    while (pos < elementText.size()) {
        // Find the attribute name.
        std::size_t namePos = elementText.find(attrName, pos);
        if (namePos == std::string::npos) break;

        // Ensure the character before the name is a word boundary (whitespace
        // or start of string) so we don't match 'myname' when looking for 'name'.
        if (namePos > 0) {
            char before = elementText[namePos - 1];
            if (!IsWhitespace(before)) {
                pos = namePos + attrName.size();
                continue;
            }
        }

        std::size_t afterName = namePos + attrName.size();

        // Skip whitespace after the attribute name.
        while (afterName < elementText.size() && IsWhitespace(elementText[afterName])) {
            ++afterName;
        }

        if (afterName >= elementText.size() || elementText[afterName] != '=') {
            pos = afterName;
            continue;
        }
        ++afterName; // skip '='

        // Skip whitespace after '='.
        while (afterName < elementText.size() && IsWhitespace(elementText[afterName])) {
            ++afterName;
        }

        if (afterName >= elementText.size()) break;

        char quote = elementText[afterName];
        if (quote != '"' && quote != '\'') {
            pos = afterName;
            continue;
        }
        ++afterName; // skip opening quote

        std::size_t valueEnd = elementText.find(quote, afterName);
        if (valueEnd == std::string::npos) break;

        std::string raw = elementText.substr(afterName, valueEnd - afterName);
        return DecodeXMLEntities(raw);
    }
    return {};
}

} // namespace xisf
