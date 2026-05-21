// StringUtil.h — Shared string utilities for the Property Handler.
// Consolidates Trim / ToLower / SplitFields implementations that previously
// lived in DSOCatalog, DSOAliasDB, and ConstellationDB.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace xisf::str {

// Trim leading/trailing ASCII whitespace (" \t\r\n").
std::string Trim(std::string_view s);

// Lowercase a string using the C locale (unsigned char cast to avoid UB
// for non-ASCII bytes, matching the historical behavior of DSOCatalog::ToLower
// and DSOAliasDB::ToLower).
std::string ToLower(std::string_view s);

// Split a single line on the given delimiter into a flat vector of fields.
// Does NOT interpret quotes or escapes — matches the legacy SplitCSV/NextField
// behavior used by OpenNGC (';' delim) and constellations.csv (',' delim).
std::vector<std::string> SplitFields(std::string_view line, char delim);

} // namespace xisf::str
