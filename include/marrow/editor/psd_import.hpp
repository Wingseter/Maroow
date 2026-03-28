#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace marrow::editor {

struct PsdImportedLayer {
    std::string name;
    std::vector<std::string> group_path;
    std::string slot_name;
    std::string attachment_name;
    std::string bone_name;
    std::filesystem::path extracted_image_path;
    int left{0};
    int top{0};
    int width{0};
    int height{0};
};

struct PsdBoneHint {
    std::string name;
    std::optional<std::string> parent_name;
    double x{0.0};
    double y{0.0};
};

struct PsdImportOptions {
    std::filesystem::path psd_path;
    std::filesystem::path skeleton_output_path;
    std::filesystem::path atlas_output_path;
    std::optional<std::filesystem::path> existing_skeleton_path;
    std::filesystem::path extracted_layers_directory;
    std::string atlas_name;
};

struct PsdImportError {
    std::filesystem::path path;
    std::string message;

    /// @brief Formats the import error as a human-readable message.
    /// @return A formatted error string containing the file path and failure text.
    std::string format() const;
};

struct PsdImportResult {
    std::filesystem::path skeleton_path;
    std::filesystem::path atlas_path;
    std::filesystem::path texture_path;
    std::filesystem::path extracted_layers_directory;
    std::vector<PsdImportedLayer> layers;
    std::vector<PsdBoneHint> bones;
    std::optional<PsdImportError> error;

    /// @brief Reports whether the import completed without an error.
    /// @return `true` when no error payload is present; otherwise `false`.
    explicit operator bool() const {
        return !error.has_value();
    }
};

/**
 * @brief Imports a PSD document into a runtime skeleton bundle plus atlas metadata.
 * @param options Import paths and output settings for the conversion.
 * @return The imported runtime bundle or an error describing why import failed.
 */
PsdImportResult import_psd_to_runtime_bundle(const PsdImportOptions& options);

} // namespace marrow::editor
