// StringUtil.cpp — Shared string utility implementations.
#include "StringUtil.h"

#include <algorithm>
#include <cctype>

namespace xisf::str {

std::string Trim(std::string_view s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string_view::npos) return {};
    size_t b = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(a, b - a + 1));
}

std::string ToLower(std::string_view s) {
    std::string r(s);
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

std::vector<std::string> SplitFields(std::string_view line, char delim) {
    std::vector<std::string> fields;
    std::string field;
    for (char c : line) {
        if (c == delim) { fields.push_back(std::move(field)); field.clear(); }
        else field += c;
    }
    fields.push_back(std::move(field));
    return fields;
}

} // namespace xisf::str
