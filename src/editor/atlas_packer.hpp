#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "marrow/editor/project.hpp"

namespace marrow::editor::detail {

struct PackedAtlasArtifact {
    std::filesystem::path atlas_path;
    std::filesystem::path image_path;
    std::string atlas_text;
    std::vector<std::uint8_t> image_bytes;
};

struct PackedAtlasArtifactResult {
    std::optional<PackedAtlasArtifact> artifact;
    std::string error_message;

    explicit operator bool() const {
        return artifact.has_value();
    }
};

PackedAtlasArtifactResult build_packed_atlas_artifact(
    const ProjectData& project,
    const AtlasPackDefinition& definition,
    const std::filesystem::path& exported_atlas_path);

std::optional<std::string> write_rgba_png(
    const std::filesystem::path& path,
    int width,
    int height,
    const std::vector<std::uint8_t>& rgba8);

} // namespace marrow::editor::detail
