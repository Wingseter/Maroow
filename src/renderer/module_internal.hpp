#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "marrow/renderer/module.hpp"

namespace marrow::renderer::internal {

struct SoftwareStencilBuffer {
    int width{0};
    int height{0};
    double origin_x{0.0};
    double origin_y{0.0};
    double pixel_size{1.0};
    std::vector<std::uint8_t> values;
};

struct SoftwareStencilClipState {
    std::uint8_t reference_value{0};
    std::uint8_t parent_reference_value{0};
    std::uint8_t invert_mask{0};
};

std::optional<SoftwareStencilClipState> stencil_clip_state_for_depth(std::size_t nesting_depth);

std::optional<std::string> initialize_software_stencil_buffer(
    int width,
    int height,
    double origin_x,
    double origin_y,
    double pixel_size,
    SoftwareStencilBuffer* buffer_out);

std::optional<std::string> apply_software_stencil_clip_push(
    const std::vector<RenderPoint>& polygon,
    const SoftwareStencilClipState& clip_state,
    SoftwareStencilBuffer* buffer);

std::optional<std::string> apply_software_stencil_clip_pop(
    const std::vector<RenderPoint>& polygon,
    const SoftwareStencilClipState& clip_state,
    SoftwareStencilBuffer* buffer);

std::size_t count_software_stencil_visible_pixels(
    const SoftwareStencilBuffer& buffer,
    const std::vector<RenderPoint>& polygon,
    std::optional<std::uint8_t> required_reference);

} // namespace marrow::renderer::internal
