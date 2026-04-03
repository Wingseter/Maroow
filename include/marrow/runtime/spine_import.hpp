#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "marrow/runtime/json.hpp"

namespace marrow::runtime {

struct SpineImportResult {
    std::optional<json::Document> document;
    std::optional<json::LoadError> error;
    std::vector<std::string> warnings;

    /// @brief Reports whether Spine JSON import succeeded.
    /// @return `true` when an imported document is present; otherwise `false`.
    explicit operator bool() const {
        return document.has_value();
    }
};

struct SpineAtlasImportResult {
    std::vector<json::Document> documents;
    std::optional<json::LoadError> error;

    /// @brief Reports whether Spine atlas import produced at least one document.
    /// @return `true` when one or more atlas documents were emitted; otherwise `false`.
    explicit operator bool() const {
        return !documents.empty();
    }
};

/**
 * @brief Converts a parsed Spine JSON document into a Marrow runtime document.
 * @param spine_document Parsed Spine JSON document.
 * @return Imported Marrow JSON document or a load error.
 */
SpineImportResult import_spine_json_document(const json::Document& spine_document);
/**
 * @brief Converts a Spine JSON file into a Marrow runtime document.
 * @param spine_json_path Path to the Spine JSON source file.
 * @return Imported Marrow JSON document or a load error.
 */
SpineImportResult import_spine_json_file(const std::filesystem::path& spine_json_path);
/**
 * @brief Converts Spine atlas text into one or more Marrow atlas documents.
 * @param spine_atlas_text Raw `.atlas` text.
 * @param source_path Optional source path used for diagnostics.
 * @return Imported `.matl` documents or a load error.
 */
SpineAtlasImportResult import_spine_atlas_text(
    std::string_view spine_atlas_text,
    std::filesystem::path source_path = {});
/**
 * @brief Converts a Spine atlas file into one or more Marrow atlas documents.
 * @param spine_atlas_path Path to the Spine `.atlas` file.
 * @return Imported `.matl` documents or a load error.
 */
SpineAtlasImportResult import_spine_atlas_file(const std::filesystem::path& spine_atlas_path);

} // namespace marrow::runtime
