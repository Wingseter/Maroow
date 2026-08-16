#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
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

using BackendFrameCallback = std::function<std::optional<std::string>(const BackendFrameInfo&)>;

struct SokolSceneTargetInfo {
    std::uint32_t color_format{0};
    std::uint32_t depth_format{0};
    int sample_count{1};
};

class SokolSceneRenderer {
public:
    virtual ~SokolSceneRenderer() = default;
    virtual std::optional<std::string> create_scene_resources(
        const BackendCreateInfo& create_info,
        const SokolSceneTargetInfo& target_info) = 0;
    virtual void destroy_scene_resources() = 0;
    virtual std::optional<std::string> prepare_command_lists(
        const std::vector<const RenderCommandList*>& command_lists) = 0;
    virtual std::optional<std::string> submit_commands_to_active_pass(
        const RenderCommandList& command_list) = 0;
};

std::unique_ptr<Backend> make_sokol_backend();
std::unique_ptr<SokolSceneRenderer> make_sokol_scene_renderer();

std::optional<std::string> render_demo_frame(
    const PreparedScene& scene,
    bool hud_overlay_enabled,
    Backend* backend,
    const BackendFrameInfo& frame_info);

std::optional<std::string> run_sokol_app(
    const BackendCreateInfo& create_info,
    Backend* backend,
    const BackendFrameCallback& render_callback,
    std::optional<int> auto_close_frames);

} // namespace marrow::renderer::internal
