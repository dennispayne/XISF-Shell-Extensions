#include "XISFParser.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <cstring>

namespace xisf {

// ---------------------------------------------------------------------------
// XISFRawMetadata helpers
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// XISFParser::ParseFile
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// XISFParser::ParseXMLString
// ---------------------------------------------------------------------------

ParseResult XISFParser::ParseXMLString(const std::string& xmlContent) {
    return ExtractMetadataFromXML(xmlContent);
}

// ---------------------------------------------------------------------------
// XISFParser::ExtractMetadataFromXML
// ---------------------------------------------------------------------------

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

    result.metadata.buildIndices();

    return result;
}

// ---------------------------------------------------------------------------
// XISFParser::FindElements
// ---------------------------------------------------------------------------

std::vector<std::unordered_map<std::string, std::string>>
XISFParser::FindElements(const std::string& xml, const std::string& tagName) {
    std::vector<std::unordered_map<std::string, std::string>> results;

    const std::string openTag = "<" + tagName;
    std::size_t pos = 0;

    while (pos < xml.size()) {
        // Locate the next occurrence of <tagName.
        std::size_t tagStart = xml.find(openTag, pos);
        if (tagStart == std::string::npos) break;

        // The character immediately after the tag name must be whitespace or
        // '/' or '>' to avoid false-positives (e.g. <FITSKeywordExtra>).
        std::size_t afterTagName = tagStart + openTag.size();
        if (afterTagName < xml.size()) {
            char next = xml[afterTagName];
            if (next != ' ' && next != '\t' && next != '\n' && next != '\r'
                && next != '/' && next != '>') {
                pos = afterTagName;
                continue;
            }
        }

        // Find the end of this element (self-closing '/>' or '>').
        std::size_t elementEnd = std::string::npos;
        bool selfClosing = false;

        std::size_t search = afterTagName;
        while (search < xml.size()) {
            std::size_t gtPos   = xml.find('>', search);
            if (gtPos == std::string::npos) break;

            if (gtPos > 0 && xml[gtPos - 1] == '/') {
                // Self-closing element.
                elementEnd  = gtPos;
                selfClosing = true;
                break;
            } else {
                // Could be an attribute value containing '>' — only treat as
                // element end if we are not inside a quoted string.
                // Walk from search to gtPos counting quote state.
                bool inSingleQuote = false;
                bool inDoubleQuote = false;
                for (std::size_t k = search; k < gtPos; ++k) {
                    char c = xml[k];
                    if (c == '\'' && !inDoubleQuote) inSingleQuote = !inSingleQuote;
                    else if (c == '"' && !inSingleQuote) inDoubleQuote = !inDoubleQuote;
                }
                if (!inSingleQuote && !inDoubleQuote) {
                    elementEnd  = gtPos;
                    selfClosing = false;
                    break;
                }
                search = gtPos + 1;
            }
        }

        if (elementEnd == std::string::npos) {
            pos = afterTagName;
            continue;
        }

        // Extract the text spanning from after '<tagName' up to (not including)
        // the closing '>' or '/>'.
        std::size_t attrStart = afterTagName;
        std::size_t attrEnd   = selfClosing ? elementEnd - 1 : elementEnd;
        std::string attrText  = xml.substr(attrStart, attrEnd - attrStart);

        // Common attributes for the known element types.
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

        results.push_back(std::move(attrMap));
        pos = elementEnd + 1;
    }

    return results;
}

// ---------------------------------------------------------------------------
// XISFParser::GetAttribute
// ---------------------------------------------------------------------------

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
            if (before != ' ' && before != '\t' && before != '\n' && before != '\r') {
                pos = namePos + attrName.size();
                continue;
            }
        }

        std::size_t afterName = namePos + attrName.size();

        // Skip whitespace after the attribute name.
        while (afterName < elementText.size() &&
               (elementText[afterName] == ' ' || elementText[afterName] == '\t' ||
                elementText[afterName] == '\n' || elementText[afterName] == '\r')) {
            ++afterName;
        }

        if (afterName >= elementText.size() || elementText[afterName] != '=') {
            pos = afterName;
            continue;
        }
        ++afterName; // skip '='

        // Skip whitespace after '='.
        while (afterName < elementText.size() &&
               (elementText[afterName] == ' ' || elementText[afterName] == '\t' ||
                elementText[afterName] == '\n' || elementText[afterName] == '\r')) {
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
