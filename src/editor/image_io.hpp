#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace marrow::editor::detail {

std::optional<std::string> load_png_rgba8_raw(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>* rgba8_out,
    int* width_out,
    int* height_out);

} // namespace marrow::editor::detail
