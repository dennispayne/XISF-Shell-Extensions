// DSOAliasDB.h — JSON-backed database of Deep Sky Object aliases (Property Handler)
#pragma once
#include <string>
#include <vector>
#include <unordered_map>

namespace xisf {

/// A single DSO with its canonical name, aliases, type, and constellation.
struct DSOAliasEntry {
    std::string canonicalName;        ///< e.g. "Orion Nebula"
    std::vector<std::string> aliases; ///< e.g. {"M42", "NGC 1976", "LBN 974"}
    std::string type;                 ///< e.g. "Emission Nebula"
    std::string constellation;        ///< e.g. "Orion"
};

/// Loads and queries a JSON file containing an array of DSOAliasEntry records.
class DSOAliasDB {
public:
    DSOAliasDB() = default;

    /// Load the database from a JSON file on disk.
    /// Returns true on success. On failure the database remains empty.
    bool LoadFromFile(const std::string& jsonPath);

    /// Load from an in-memory JSON string (useful for unit tests).
    bool LoadFromString(const std::string& jsonContent);

    /// Return all names (canonical + aliases) for the entry that matches
    /// @p objectName (searched case-insensitively against both canonical
    /// names and aliases). Returns an empty vector if not found.
    std::vector<std::string> GetAllNames(const std::string& objectName) const;

    /// Return the canonical name for any matching alias or canonical name.
    /// Returns an empty string if not found.
    std::string GetCanonicalName(const std::string& name) const;

    /// Number of entries currently loaded.
    size_t Count() const;

private:
    std::vector<DSOAliasEntry> m_entries;
    /// Maps lowercase name / alias → index into m_entries for O(1) lookup.
    std::unordered_map<std::string, size_t> m_index;

    void BuildIndex();
    static std::string ToLower(const std::string& s);

    // -----------------------------------------------------------------------
    // Minimal hand-written JSON parser – avoids any external dependency.
    // -----------------------------------------------------------------------
    bool ParseJSON(const std::string& json);

    static void SkipWhitespace(const std::string& s, size_t& pos);

    /// Read a JSON string value starting at the opening '"'.
    /// Handles \" and \\ escape sequences.
    /// Returns false if the string is malformed or pos is not on '"'.
    static bool ReadString(const std::string& s, size_t& pos,
                           std::string& out);

    /// Advance @p pos past any JSON value (string, number, boolean, null,
    /// or nested array/object) without capturing its contents.
    static bool SkipValue(const std::string& s, size_t& pos);
};

} // namespace xisf
