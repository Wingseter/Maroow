#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "sokol_gfx.h"

namespace marrow::tests {

struct GpuReadbackResult {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> top_left_rgba8;
    std::string error;

    explicit operator bool() const noexcept { return error.empty(); }
};

GpuReadbackResult read_sokol_rgba8_image(
    sg_image image,
    int width,
    int height);

} // namespace marrow::tests
