// XISFParser.h — Phase 1: XISF binary header parser and XML metadata extractor
// Reads the XISF binary preamble, extracts the XML header block, and
// parses FITS keywords and XISF Properties from it without any external
// XML library dependency (educational single-file approach).
#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <cstdint>

namespace xisf {

// ---------------------------------------------------------------------------
// Data structures
// ---------------------------------------------------------------------------

/// Raw FITS keyword extracted from a <FITSKeyword> element.
struct FITSKeyword {
    std::string name;
    std::string value;
    std::string comment;
};

/// Raw XISF property extracted from a <Property> element.
struct XISFProperty {
    std::string id;      ///< e.g. "Instrument:ExposureTime"
    std::string type;    ///< e.g. "Float64", "String", "Int32"
    std::string value;
};

/// All metadata extracted from an XISF file's XML header.
struct XISFRawMetadata {
    std::string xmlHeader;                        ///< Full XML text
    std::vector<FITSKeyword>  fitsKeywords;       ///< FITS keywords
    std::vector<XISFProperty> properties;         ///< XISF typed properties
    std::unordered_map<std::string, std::string> imageAttributes; ///< Image element attrs (sampleFormat, colorSpace, etc.)

    /// Build O(1) lookup indices. Called automatically after parsing.
    void buildIndices();

    /// Lookup a FITS keyword value by name (case-insensitive). Returns empty
    /// string if not found.
    std::string getFITSValue(const std::string& name) const;

    /// Lookup an XISF property value by id. Returns empty string if not found.
    std::string getPropertyValue(const std::string& id) const;

private:
    std::unordered_map<std::string, size_t> fitsIndex_;  ///< uppercase name → fitsKeywords index
    std::unordered_map<std::string, size_t> propIndex_;  ///< id → properties index
};

// ---------------------------------------------------------------------------
// Error handling
// ---------------------------------------------------------------------------

enum class ParseError {
    None,
    FileNotFound,
    InvalidSignature,   ///< First 8 bytes are not "XISF0100"
    HeaderTooLarge,     ///< XML header size exceeds safety limit (64 MB)
    ReadError,          ///< I/O error reading file
    XMLParseError       ///< Malformed XML or missing required elements
};

struct ParseResult {
    XISFRawMetadata metadata;
    ParseError      error   = ParseError::None;
    std::string     errorMessage;

    bool ok() const { return error == ParseError::None; }
};

// ---------------------------------------------------------------------------
// Parser
// ---------------------------------------------------------------------------

class XISFParser {
public:
    /// Maximum XML header size accepted (64 MB safety guard).
    static constexpr uint32_t kMaxHeaderBytes = 64u * 1024u * 1024u;

    /// Parse an XISF file on disk.
    static ParseResult ParseFile(const std::string& filePath);

    /// Parse from an already-loaded XML string (useful for unit tests).
    static ParseResult ParseXMLString(const std::string& xmlContent);

private:
    static ParseResult ExtractMetadataFromXML(const std::string& xml);

    /// Extract all occurrences of a named XML element's attributes.
    /// Returns a vector of attribute maps, one map per element found.
    static std::vector<std::unordered_map<std::string, std::string>>
        FindElements(const std::string& xml, const std::string& tagName);

    /// Extract the value of a named attribute from a single element text
    /// (the raw text between < and >).
    static std::string GetAttribute(const std::string& elementText,
                                    const std::string& attrName);
};

} // namespace xisf
