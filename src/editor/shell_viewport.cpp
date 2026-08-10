#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "imgui.h"
#include "sokol_gfx.h"
#define SOKOL_IMGUI_NO_SOKOL_APP
#include "sokol_imgui.h"

#include "shell_state.hpp"
#include "shell_preview.hpp"
#include "shell_selection.hpp"
#include "shell_timeline.hpp"
#include "shell_weight_paint.hpp"
#include "viewport_renderer.hpp"
#include "marrow/allocator.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/renderer/module.hpp"
#include "marrow/runtime/animation_state.hpp"
#if defined(SOKOL_METAL)
#include "marrow_renderer_shader_metal.h"
#elif defined(SOKOL_GLCORE)
#include "marrow_renderer_shader_gl.h"
#else
#error "Marrow viewport shaders require Metal or GLCORE."
#endif

namespace marrow::editor::shell {

std::optional<std::string> viewport_resource_error(
    std::string_view label,
    sg_resource_state state) {
    if (state == SG_RESOURCESTATE_VALID) {
        return std::nullopt;
    }
    return std::string("Failed to create viewport ") + std::string(label) + ".";
}

void destroy_viewport_framebuffer(ViewportRenderResources* resources) {
    if (resources == nullptr) {
        return;
    }
    if (sg_isvalid()) {
        if (resources->color_attachment_view.id != SG_INVALID_ID) {
            sg_destroy_view(resources->color_attachment_view);
        }
        if (resources->color_texture_view.id != SG_INVALID_ID) {
            sg_destroy_view(resources->color_texture_view);
        }
        if (resources->depth_stencil_view.id != SG_INVALID_ID) {
            sg_destroy_view(resources->depth_stencil_view);
        }
        if (resources->color_image.id != SG_INVALID_ID) {
            sg_destroy_image(resources->color_image);
        }
        if (resources->depth_stencil_image.id != SG_INVALID_ID) {
            sg_destroy_image(resources->depth_stencil_image);
        }
    }
    resources->color_image = {};
    resources->color_attachment_view = {};
    resources->color_texture_view = {};
    resources->depth_stencil_image = {};
    resources->depth_stencil_view = {};
    resources->imgui_texture_id = 0U;
    resources->framebuffer_width = 0;
    resources->framebuffer_height = 0;
}

std::optional<std::string> initialize_viewport_renderer(
    ViewportRenderResources* resources) {
    if (resources == nullptr) {
        return "Viewport renderer state is unavailable.";
    }

    resources->initialization_attempted = true;
    resources->error_message.clear();
    if (resources->available) {
        return std::nullopt;
    }
    if (!sg_isvalid()) {
        resources->error_message = "Viewport renderer requires an initialized Sokol device.";
        return resources->error_message;
    }

    const sg_shader_desc* shader_desc =
        marrow_renderer_viewport_overlay_shader_desc(sg_query_backend());
    resources->overlay_shader = sg_make_shader(shader_desc);
    if (const auto error = viewport_resource_error(
            "overlay shader", sg_query_shader_state(resources->overlay_shader))) {
        resources->error_message = *error;
        destroy_viewport_renderer(resources);
        return error;
    }

    const auto make_pipeline = [&](sg_primitive_type primitive_type, const char* label) {
        sg_pipeline_desc descriptor{};
        descriptor.shader = resources->overlay_shader;
        descriptor.layout.buffers[0].stride = sizeof(ViewportRenderVertex);
        descriptor.layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
        descriptor.layout.attrs[0].offset = offsetof(ViewportRenderVertex, position_x);
        descriptor.layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT4;
        descriptor.layout.attrs[1].offset = offsetof(ViewportRenderVertex, color_r);
        descriptor.primitive_type = primitive_type;
        descriptor.colors[0].pixel_format = SG_PIXELFORMAT_RGBA8;
        descriptor.colors[0].blend.enabled = true;
        descriptor.colors[0].blend.src_factor_rgb = SG_BLENDFACTOR_SRC_ALPHA;
        descriptor.colors[0].blend.dst_factor_rgb = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        descriptor.colors[0].blend.src_factor_alpha = SG_BLENDFACTOR_ONE;
        descriptor.colors[0].blend.dst_factor_alpha = SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        descriptor.depth.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
        descriptor.sample_count = 1;
        descriptor.label = label;
        return sg_make_pipeline(&descriptor);
    };
    resources->overlay_line_pipeline =
        make_pipeline(SG_PRIMITIVETYPE_LINES, "marrow-viewport-overlay-lines");
    resources->overlay_triangle_pipeline =
        make_pipeline(SG_PRIMITIVETYPE_TRIANGLES, "marrow-viewport-overlay-triangles");
    if (const auto error = viewport_resource_error(
            "overlay line pipeline",
            sg_query_pipeline_state(resources->overlay_line_pipeline))) {
        resources->error_message = *error;
        destroy_viewport_renderer(resources);
        return error;
    }
    if (const auto error = viewport_resource_error(
            "overlay triangle pipeline",
            sg_query_pipeline_state(resources->overlay_triangle_pipeline))) {
        resources->error_message = *error;
        destroy_viewport_renderer(resources);
        return error;
    }

    constexpr std::size_t kInitialOverlayCapacity = 256U * 1024U;
    sg_buffer_desc buffer_desc{};
    buffer_desc.size = kInitialOverlayCapacity;
    buffer_desc.usage.vertex_buffer = true;
    buffer_desc.usage.immutable = false;
    buffer_desc.usage.stream_update = true;
    buffer_desc.label = "marrow-viewport-overlay-stream";
    resources->overlay_vertex_buffer = sg_make_buffer(&buffer_desc);
    if (const auto error = viewport_resource_error(
            "overlay stream buffer",
            sg_query_buffer_state(resources->overlay_vertex_buffer))) {
        resources->error_message = *error;
        destroy_viewport_renderer(resources);
        return error;
    }
    resources->overlay_vertex_capacity_bytes = kInitialOverlayCapacity;

    sg_sampler_desc sampler_desc{};
    sampler_desc.min_filter = SG_FILTER_LINEAR;
    sampler_desc.mag_filter = SG_FILTER_LINEAR;
    sampler_desc.wrap_u = SG_WRAP_CLAMP_TO_EDGE;
    sampler_desc.wrap_v = SG_WRAP_CLAMP_TO_EDGE;
    sampler_desc.label = "marrow-viewport-imgui-sampler";
    resources->texture_sampler = sg_make_sampler(&sampler_desc);
    if (const auto error = viewport_resource_error(
            "texture sampler", sg_query_sampler_state(resources->texture_sampler))) {
        resources->error_message = *error;
        destroy_viewport_renderer(resources);
        return error;
    }

    if (const auto error = resources->prepared_scene_renderer.initialize(
            SG_PIXELFORMAT_RGBA8,
            SG_PIXELFORMAT_DEPTH_STENCIL,
            1)) {
        resources->error_message = *error;
        destroy_viewport_renderer(resources);
        return error;
    }

    resources->available = true;
    return std::nullopt;
}

void destroy_viewport_renderer(ViewportRenderResources* resources) {
    if (resources == nullptr) {
        return;
    }

    destroy_viewport_framebuffer(resources);
    resources->prepared_scene_renderer.destroy();
    if (sg_isvalid()) {
        if (resources->texture_sampler.id != SG_INVALID_ID) {
            sg_destroy_sampler(resources->texture_sampler);
        }
        if (resources->overlay_vertex_buffer.id != SG_INVALID_ID) {
            sg_destroy_buffer(resources->overlay_vertex_buffer);
        }
        if (resources->overlay_triangle_pipeline.id != SG_INVALID_ID) {
            sg_destroy_pipeline(resources->overlay_triangle_pipeline);
        }
        if (resources->overlay_line_pipeline.id != SG_INVALID_ID) {
            sg_destroy_pipeline(resources->overlay_line_pipeline);
        }
        if (resources->overlay_shader.id != SG_INVALID_ID) {
            sg_destroy_shader(resources->overlay_shader);
        }
    }
    resources->texture_sampler = {};
    resources->overlay_vertex_buffer = {};
    resources->overlay_vertex_capacity_bytes = 0U;
    resources->overlay_triangle_pipeline = {};
    resources->overlay_line_pipeline = {};
    resources->overlay_shader = {};
    resources->available = false;
}

ViewportFramebufferSize viewport_framebuffer_size(
    const ImVec2& canvas_size,
    const ImVec2& framebuffer_scale) {
    const float scale_x = framebuffer_scale.x > 0.0f ? framebuffer_scale.x : 1.0f;
    const float scale_y = framebuffer_scale.y > 0.0f ? framebuffer_scale.y : 1.0f;

    return ViewportFramebufferSize{
        std::max(1, static_cast<int>(std::lround(std::max(canvas_size.x, 1.0f) * scale_x))),
        std::max(1, static_cast<int>(std::lround(std::max(canvas_size.y, 1.0f) * scale_y)))};
}

std::optional<std::string> ensure_viewport_framebuffer(
    ViewportRenderResources* resources,
    int width,
    int height) {
    if (resources == nullptr) {
        return "Viewport renderer state is unavailable.";
    }
    if (!resources->available) {
        return resources->error_message.empty()
            ? std::optional<std::string>("Viewport renderer is unavailable.")
            : std::optional<std::string>(resources->error_message);
    }
    if (width <= 0 || height <= 0) {
        return "Viewport framebuffer dimensions must be greater than zero.";
    }
    if (resources->color_image.id != SG_INVALID_ID &&
        resources->framebuffer_width == width &&
        resources->framebuffer_height == height) {
        return std::nullopt;
    }

    destroy_viewport_framebuffer(resources);

    sg_image_desc color_desc{};
    color_desc.width = width;
    color_desc.height = height;
    color_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
    color_desc.sample_count = 1;
    color_desc.usage.color_attachment = true;
    color_desc.label = "marrow-editor-viewport-color";
    resources->color_image = sg_make_image(&color_desc);
    if (const auto error = viewport_resource_error(
            "color image", sg_query_image_state(resources->color_image))) {
        resources->error_message = *error;
        destroy_viewport_framebuffer(resources);
        return error;
    }

    sg_view_desc color_attachment_desc{};
    color_attachment_desc.color_attachment.image = resources->color_image;
    color_attachment_desc.label = "marrow-editor-viewport-color-attachment";
    resources->color_attachment_view = sg_make_view(&color_attachment_desc);
    if (const auto error = viewport_resource_error(
            "color attachment view",
            sg_query_view_state(resources->color_attachment_view))) {
        resources->error_message = *error;
        destroy_viewport_framebuffer(resources);
        return error;
    }

    sg_view_desc texture_desc{};
    texture_desc.texture.image = resources->color_image;
    texture_desc.label = "marrow-editor-viewport-texture";
    resources->color_texture_view = sg_make_view(&texture_desc);
    if (const auto error = viewport_resource_error(
            "texture view", sg_query_view_state(resources->color_texture_view))) {
        resources->error_message = *error;
        destroy_viewport_framebuffer(resources);
        return error;
    }

    sg_image_desc depth_desc{};
    depth_desc.width = width;
    depth_desc.height = height;
    depth_desc.pixel_format = SG_PIXELFORMAT_DEPTH_STENCIL;
    depth_desc.sample_count = 1;
    depth_desc.usage.depth_stencil_attachment = true;
    depth_desc.label = "marrow-editor-viewport-depth-stencil";
    resources->depth_stencil_image = sg_make_image(&depth_desc);
    if (const auto error = viewport_resource_error(
            "depth-stencil image",
            sg_query_image_state(resources->depth_stencil_image))) {
        resources->error_message = *error;
        destroy_viewport_framebuffer(resources);
        return error;
    }

    sg_view_desc depth_view_desc{};
    depth_view_desc.depth_stencil_attachment.image = resources->depth_stencil_image;
    depth_view_desc.label = "marrow-editor-viewport-depth-stencil-view";
    resources->depth_stencil_view = sg_make_view(&depth_view_desc);
    if (const auto error = viewport_resource_error(
            "depth-stencil view",
            sg_query_view_state(resources->depth_stencil_view))) {
        resources->error_message = *error;
        destroy_viewport_framebuffer(resources);
        return error;
    }

    resources->imgui_texture_id = simgui_imtextureid_with_sampler(
        resources->color_texture_view,
        resources->texture_sampler);

    resources->framebuffer_width = width;
    resources->framebuffer_height = height;
    resources->error_message.clear();
    return std::nullopt;
}


// --- Viewport geometry, rendering, picking ---

ImVec2 screen_from_world(
    const ViewportLayout& layout,
    double world_x,
    double world_y) {
    const double pixels_per_unit = static_cast<double>(layout.pixels_per_unit);
    return ImVec2(
        layout.screen_center.x +
            static_cast<float>((world_x - layout.world_center_x) * pixels_per_unit),
        layout.screen_center.y -
            static_cast<float>((world_y - layout.world_center_y) * pixels_per_unit));
}

ImVec2 screen_from_world(
    const ViewportLayout& layout,
    float world_x,
    float world_y) {
    return screen_from_world(
        layout,
        static_cast<double>(world_x),
        static_cast<double>(world_y));
}

ViewportWorldPoint world_from_screen(
    const ViewportLayout& layout,
    const ImVec2& screen_position) {
    const double pixels_per_unit = static_cast<double>(layout.pixels_per_unit);
    if (pixels_per_unit <= 0.0) {
        return ViewportWorldPoint{layout.world_center_x, layout.world_center_y};
    }

    return ViewportWorldPoint{
        layout.world_center_x +
            (static_cast<double>(screen_position.x) -
             static_cast<double>(layout.screen_center.x)) /
                pixels_per_unit,
        layout.world_center_y -
            (static_cast<double>(screen_position.y) -
             static_cast<double>(layout.screen_center.y)) /
                pixels_per_unit};
}

std::array<float, 16> viewport_projection_matrix(const ViewportLayout& layout) {
    const double pixels_per_unit =
        std::max(static_cast<double>(layout.pixels_per_unit), 0.0001);
    const double world_width =
        std::max(static_cast<double>(layout.canvas_size.x) / pixels_per_unit, 1.0);
    const double world_height =
        std::max(static_cast<double>(layout.canvas_size.y) / pixels_per_unit, 1.0);
    const double center_x =
        layout.world_center_x -
        (static_cast<double>(layout.screen_center.x - (layout.canvas_origin.x + (layout.canvas_size.x * 0.5f))) /
         pixels_per_unit);
    const double center_y =
        layout.world_center_y +
        (static_cast<double>(layout.screen_center.y - (layout.canvas_origin.y + (layout.canvas_size.y * 0.5f))) /
         pixels_per_unit);
    const double min_x = center_x - (world_width * 0.5);
    const double max_x = center_x + (world_width * 0.5);
    const double min_y = center_y - (world_height * 0.5);
    const double max_y = center_y + (world_height * 0.5);

    return {{
        static_cast<float>(2.0 / world_width),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        static_cast<float>(2.0 / world_height),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        -1.0f,
        0.0f,
        static_cast<float>(-(max_x + min_x) / world_width),
        static_cast<float>(-(max_y + min_y) / world_height),
        0.0f,
        1.0f,
    }};
}

std::filesystem::path resolve_viewport_atlas_image_path(
    const ShellState& state,
    const marrow::renderer::PreparedScene& scene) {
    if (!state.load_result || state.load_result.project == nullptr ||
        state.load_result.project->resolved_atlas_paths().empty()) {
        return {};
    }

    const std::filesystem::path atlas_path =
        state.load_result.project->resolved_atlas_paths().front();
    const std::filesystem::path image_path(scene.atlas_image);
    return image_path.is_absolute()
        ? image_path.lexically_normal()
        : (atlas_path.parent_path() / image_path).lexically_normal();
}

float squared_distance(const ImVec2& a, const ImVec2& b) {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return (dx * dx) + (dy * dy);
}

float point_segment_distance_squared(
    const ImVec2& point,
    const ImVec2& start,
    const ImVec2& end) {
    const float ab_x = end.x - start.x;
    const float ab_y = end.y - start.y;
    const float ab_length_squared = (ab_x * ab_x) + (ab_y * ab_y);
    if (ab_length_squared <= 1e-6f) {
        return squared_distance(point, start);
    }

    const float ap_x = point.x - start.x;
    const float ap_y = point.y - start.y;
    const float projection = std::clamp(
        ((ap_x * ab_x) + (ap_y * ab_y)) / ab_length_squared,
        0.0f,
        1.0f);
    const ImVec2 closest(start.x + (ab_x * projection), start.y + (ab_y * projection));
    return squared_distance(point, closest);
}

float first_grid_line(float anchor, float minimum, float spacing) {
    const float offset = std::fmod(anchor - minimum, spacing);
    return minimum + (offset < 0.0f ? offset + spacing : offset);
}

namespace {

std::optional<ViewportCamera> camera_for_pose(
    const marrow::runtime::Skeleton& skeleton) {
    const auto& world_transforms = skeleton.bone_world_transforms();
    if (world_transforms.empty()) {
        return std::nullopt;
    }

    double min_x = std::numeric_limits<double>::max();
    double min_y = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double max_y = std::numeric_limits<double>::lowest();
    bool has_active_bone = false;

    for (std::size_t bone_index = 0; bone_index < world_transforms.size(); ++bone_index) {
        if (!skeleton.is_bone_active(bone_index)) {
            continue;
        }

        has_active_bone = true;
        min_x = std::min(min_x, static_cast<double>(world_transforms[bone_index].world_x));
        min_y = std::min(min_y, static_cast<double>(world_transforms[bone_index].world_y));
        max_x = std::max(max_x, static_cast<double>(world_transforms[bone_index].world_x));
        max_y = std::max(max_y, static_cast<double>(world_transforms[bone_index].world_y));
    }

    if (!has_active_bone) {
        for (const auto& transform : world_transforms) {
            min_x = std::min(min_x, static_cast<double>(transform.world_x));
            min_y = std::min(min_y, static_cast<double>(transform.world_y));
            max_x = std::max(max_x, static_cast<double>(transform.world_x));
            max_y = std::max(max_y, static_cast<double>(transform.world_y));
        }
    }

    return ViewportCamera{
        true,
        (min_x + max_x) * 0.5,
        (min_y + max_y) * 0.5,
        std::max(max_x - min_x, 80.0),
        std::max(max_y - min_y, 80.0)};
}

} // namespace

bool initialize_viewport_camera_from_preview_pose(ShellState* state) {
    if (state == nullptr || state->preview_skeleton == nullptr) {
        return false;
    }

    const std::optional<ViewportCamera> camera = camera_for_pose(*state->preview_skeleton);
    if (!camera.has_value()) {
        state->viewport_camera = {};
        return false;
    }

    state->viewport_camera = *camera;
    return true;
}

bool frame_viewport_camera_to_preview_pose(ShellState* state) {
    if (state == nullptr || state->preview_skeleton == nullptr) {
        return false;
    }

    const std::optional<ViewportCamera> camera = camera_for_pose(*state->preview_skeleton);
    if (!camera.has_value()) {
        return false;
    }

    state->viewport_camera = *camera;
    state->viewport.pan_x = 0.0;
    state->viewport.pan_y = 0.0;
    state->viewport.zoom = 1.0;
    return true;
}

std::optional<ViewportLayout> build_viewport_layout(
    const ShellState& state,
    const ImVec2& canvas_origin,
    const ImVec2& canvas_size) {
    if (!state.load_result || !state.preview_skeleton ||
        !state.viewport_camera.initialized) {
        return std::nullopt;
    }

    const auto& skeleton = *state.load_result.skeleton_data;
    const auto& world_transforms = state.preview_skeleton->bone_world_transforms();
    if (world_transforms.size() != skeleton.bones().size() || world_transforms.empty()) {
        return std::nullopt;
    }

    const float fit_width = std::max(32.0f, canvas_size.x - 96.0f);
    const float fit_height = std::max(32.0f, canvas_size.y - 96.0f);

    ViewportLayout layout;
    layout.canvas_origin = canvas_origin;
    layout.canvas_size = canvas_size;
    layout.canvas_end = ImVec2(canvas_origin.x + canvas_size.x, canvas_origin.y + canvas_size.y);
    layout.screen_center = ImVec2(
        canvas_origin.x + (canvas_size.x * 0.5f) + static_cast<float>(state.viewport.pan_x),
        canvas_origin.y + (canvas_size.y * 0.5f) + static_cast<float>(state.viewport.pan_y));
    layout.world_center_x = state.viewport_camera.world_center_x;
    layout.world_center_y = state.viewport_camera.world_center_y;
    layout.pixels_per_unit = std::max(
        0.25f,
        std::min(
            fit_width / static_cast<float>(state.viewport_camera.fit_extent_x),
            fit_height / static_cast<float>(state.viewport_camera.fit_extent_y))) *
        static_cast<float>(state.viewport.zoom);
    layout.render_joint_radius =
        std::clamp(4.0f + (layout.pixels_per_unit * 0.05f), 4.0f, 10.0f);
    layout.world_origin_screen = screen_from_world(layout, 0.0, 0.0);
    layout.bones.reserve(skeleton.bones().size());

    for (std::size_t bone_index = 0; bone_index < skeleton.bones().size(); ++bone_index) {
        layout.bones.push_back(BoneCanvasNode{
            bone_index,
            skeleton.bones()[bone_index].parent_index,
            screen_from_world(
                layout,
                world_transforms[bone_index].world_x,
                world_transforms[bone_index].world_y),
            state.preview_skeleton->is_bone_active(bone_index)});
    }

    return layout;
}

bool zoom_viewport_at_screen_position(
    ShellState* state,
    const ImVec2& canvas_origin,
    const ImVec2& canvas_size,
    const ImVec2& screen_position,
    double zoom_factor) {
    if (state == nullptr || !std::isfinite(zoom_factor) || zoom_factor <= 0.0) {
        return false;
    }

    const std::optional<ViewportLayout> before =
        build_viewport_layout(*state, canvas_origin, canvas_size);
    if (!before.has_value()) {
        return false;
    }

    const ViewportWorldPoint anchor_world = world_from_screen(*before, screen_position);
    const double zoom_before = state->viewport.zoom;
    const double zoom_after = std::clamp(zoom_before * zoom_factor, 0.2, 6.0);
    if (zoom_after == zoom_before) {
        return false;
    }

    state->viewport.zoom = zoom_after;
    const std::optional<ViewportLayout> after =
        build_viewport_layout(*state, canvas_origin, canvas_size);
    if (!after.has_value()) {
        state->viewport.zoom = zoom_before;
        return false;
    }

    const ImVec2 remapped_anchor =
        screen_from_world(*after, anchor_world.x, anchor_world.y);
    state->viewport.pan_x +=
        static_cast<double>(screen_position.x - remapped_anchor.x);
    state->viewport.pan_y +=
        static_cast<double>(screen_position.y - remapped_anchor.y);
    return true;
}

double onion_skin_sample_time_for_preview(
    const ShellState& state,
    double time_seconds) {
    const double duration = selected_animation_duration(state);
    if (duration <= 0.0) {
        return 0.0;
    }

    const double clamped_time = std::clamp(time_seconds, 0.0, duration);
    if (state.preview_reverse && !state.preview_queue_enabled) {
        return std::clamp(duration - clamped_time, 0.0, duration);
    }

    return clamped_time;
}

bool sample_preview_pose_at_time(
    const ShellState& state,
    double time_seconds,
    marrow::runtime::Skeleton* skeleton) {
    if (!state.load_result || skeleton == nullptr) {
        return false;
    }

    const auto& skeleton_data = *state.load_result.skeleton_data;
    const std::vector<std::string> normalized_skins =
        normalize_preview_skin_names(skeleton_data, state.preview_skin_names);
    std::vector<std::string_view> skin_names;
    skin_names.reserve(normalized_skins.size());
    for (const std::string& skin_name : normalized_skins) {
        skin_names.push_back(skin_name);
    }

    if (!skeleton->set_skin_composition(skin_names)) {
        return false;
    }

    if (const auto* animation = selected_animation(state)) {
        const double sample_time = onion_skin_sample_time_for_preview(state, time_seconds);
        skeleton->set_attachment_playback_time(sample_time);
        skeleton->apply_animation(*animation, sample_time);
    } else {
        skeleton->set_to_setup_pose();
        skeleton->set_attachment_playback_time(0.0);
    }

    apply_preview_slot_overrides(state, skeleton);
    return true;
}

float onion_skin_alpha(int distance_rank) {
    const int safe_rank = std::max(distance_rank, 1);
    return std::clamp(0.48f / static_cast<float>(safe_rank), 0.08f, 0.48f);
}

std::vector<OnionSkinSampleSpec> build_onion_skin_sample_specs(const ShellState& state) {
    std::vector<OnionSkinSampleSpec> specs;
    if (!state.load_result || !state.viewport.onion_skin.enabled) {
        return specs;
    }

    const auto* animation = selected_animation(state);
    if (animation == nullptr) {
        return specs;
    }

    const auto& settings = state.viewport.onion_skin;
    const double duration = selected_animation_duration(state);
    const double current_time =
        duration > 0.0 ? std::clamp(state.timeline_time_seconds, 0.0, duration) : 0.0;
    const int stride = std::max(settings.step, 1);
    if (settings.before_count <= 0 && settings.after_count <= 0) {
        return specs;
    }

    const auto append_far_to_near = [&](std::vector<OnionSkinSampleSpec>* samples) {
        if (samples == nullptr) {
            return;
        }
        std::reverse(samples->begin(), samples->end());
        specs.insert(specs.end(), samples->begin(), samples->end());
    };

    if (settings.mode == marrow::editor::OnionSkinMode::Keyframe) {
        const std::vector<double> key_times = collect_animation_key_times(*animation);
        std::vector<OnionSkinSampleSpec> before_specs;
        std::vector<OnionSkinSampleSpec> after_specs;

        const auto lower = std::lower_bound(
            key_times.begin(),
            key_times.end(),
            current_time - 1e-6);
        std::ptrdiff_t before_index =
            static_cast<std::ptrdiff_t>(std::distance(key_times.begin(), lower)) - 1;
        for (int rank = 1;
             rank <= settings.before_count && before_index >= 0;
             ++rank, before_index -= stride) {
            before_specs.push_back(OnionSkinSampleSpec{
                key_times[static_cast<std::size_t>(before_index)],
                rank,
                true});
        }

        const auto upper = std::upper_bound(
            key_times.begin(),
            key_times.end(),
            current_time + 1e-6);
        std::ptrdiff_t after_index =
            static_cast<std::ptrdiff_t>(std::distance(key_times.begin(), upper));
        for (int rank = 1;
             rank <= settings.after_count &&
             after_index >= 0 &&
             after_index < static_cast<std::ptrdiff_t>(key_times.size());
             ++rank, after_index += stride) {
            after_specs.push_back(OnionSkinSampleSpec{
                key_times[static_cast<std::size_t>(after_index)],
                rank,
                false});
        }

        append_far_to_near(&before_specs);
        append_far_to_near(&after_specs);
        return specs;
    }

    const double frame_interval = static_cast<double>(stride) * kOnionSkinFrameDuration;
    if (frame_interval <= 0.0) {
        return specs;
    }

    std::vector<OnionSkinSampleSpec> before_specs;
    std::vector<OnionSkinSampleSpec> after_specs;
    if (settings.anchor_to_zero) {
        const double scaled_time = current_time / frame_interval;
        double before_anchor_index = std::floor(scaled_time + 1e-9);
        if (std::abs((before_anchor_index * frame_interval) - current_time) <= 1e-6) {
            before_anchor_index -= 1.0;
        }
        for (int rank = 1; rank <= settings.before_count; ++rank) {
            const double sample_index = before_anchor_index - static_cast<double>(rank - 1);
            const double sample_time = sample_index * frame_interval;
            if (sample_time < -1e-6) {
                break;
            }
            before_specs.push_back(OnionSkinSampleSpec{
                std::max(0.0, sample_time),
                rank,
                true});
        }

        double after_anchor_index = std::ceil(scaled_time - 1e-9);
        if (std::abs((after_anchor_index * frame_interval) - current_time) <= 1e-6) {
            after_anchor_index += 1.0;
        }
        for (int rank = 1; rank <= settings.after_count; ++rank) {
            const double sample_index = after_anchor_index + static_cast<double>(rank - 1);
            const double sample_time = sample_index * frame_interval;
            if (sample_time > duration + 1e-6) {
                break;
            }
            after_specs.push_back(OnionSkinSampleSpec{
                std::min(duration, sample_time),
                rank,
                false});
        }
    } else {
        for (int rank = 1; rank <= settings.before_count; ++rank) {
            const double sample_time = current_time - (static_cast<double>(rank) * frame_interval);
            if (sample_time < -1e-6) {
                break;
            }
            before_specs.push_back(OnionSkinSampleSpec{
                std::max(0.0, sample_time),
                rank,
                true});
        }
        for (int rank = 1; rank <= settings.after_count; ++rank) {
            const double sample_time = current_time + (static_cast<double>(rank) * frame_interval);
            if (sample_time > duration + 1e-6) {
                break;
            }
            after_specs.push_back(OnionSkinSampleSpec{
                std::min(duration, sample_time),
                rank,
                false});
        }
    }

    append_far_to_near(&before_specs);
    append_far_to_near(&after_specs);
    return specs;
}

std::vector<OnionSkinGhostPose> build_onion_skin_ghost_poses(
    const ShellState& state,
    const ViewportLayout& layout) {
    std::vector<OnionSkinGhostPose> ghost_poses;
    if (!state.load_result || !state.viewport.onion_skin.enabled) {
        return ghost_poses;
    }

    const std::vector<OnionSkinSampleSpec> samples = build_onion_skin_sample_specs(state);
    if (samples.empty()) {
        return ghost_poses;
    }

    marrow::runtime::Skeleton sampled_skeleton(state.load_result.skeleton_data);
    ghost_poses.reserve(samples.size());
    for (const OnionSkinSampleSpec& sample : samples) {
        if (!sample_preview_pose_at_time(state, sample.time_seconds, &sampled_skeleton)) {
            continue;
        }

        OnionSkinGhostPose ghost_pose;
        ghost_pose.time_seconds = sample.time_seconds;
        ghost_pose.distance_rank = sample.distance_rank;
        ghost_pose.before_current = sample.before_current;
        ghost_pose.bones.reserve(state.load_result.skeleton_data->bones().size());
        const float alpha = onion_skin_alpha(sample.distance_rank);
        const int alpha_channel = static_cast<int>(std::lround(alpha * 255.0f));
        if (sample.before_current) {
            ghost_pose.line_color = IM_COL32(98, 170, 255, alpha_channel);
            ghost_pose.fill_color = IM_COL32(70, 129, 212, alpha_channel);
        } else {
            ghost_pose.line_color = IM_COL32(255, 140, 102, alpha_channel);
            ghost_pose.fill_color = IM_COL32(214, 102, 74, alpha_channel);
        }
        ghost_pose.outline_color = IM_COL32(18, 21, 25, alpha_channel);

        const auto& skeleton_data = *state.load_result.skeleton_data;
        const auto& world_transforms = sampled_skeleton.bone_world_transforms();
        for (std::size_t bone_index = 0; bone_index < skeleton_data.bones().size(); ++bone_index) {
            ghost_pose.bones.push_back(BoneCanvasNode{
                bone_index,
                skeleton_data.bones()[bone_index].parent_index,
                screen_from_world(
                    layout,
                    world_transforms[bone_index].world_x,
                    world_transforms[bone_index].world_y),
                sampled_skeleton.is_bone_active(bone_index)});
        }

        ghost_poses.push_back(std::move(ghost_pose));
    }

    return ghost_poses;
}

ImVec2 local_viewport_position(const ViewportLayout& layout, const ImVec2& screen_position) {
    return ImVec2(
        screen_position.x - layout.canvas_origin.x,
        screen_position.y - layout.canvas_origin.y);
}

ViewportRenderVertex viewport_vertex(const ImVec2& position, const ImVec4& color) {
    return ViewportRenderVertex{
        position.x,
        position.y,
        color.x,
        color.y,
        color.z,
        color.w};
}

void append_colored_line(
    std::vector<ViewportRenderVertex>* vertices,
    const ImVec2& start,
    const ImVec2& end,
    ImU32 color) {
    const ImVec4 float_color = ImGui::ColorConvertU32ToFloat4(color);
    vertices->push_back(viewport_vertex(start, float_color));
    vertices->push_back(viewport_vertex(end, float_color));
}

void append_polyline_lines(
    std::vector<ViewportRenderVertex>* vertices,
    const std::vector<ImVec2>& points,
    ImU32 color,
    bool closed) {
    if (vertices == nullptr || points.size() < 2U) {
        return;
    }

    for (std::size_t point_index = 1; point_index < points.size(); ++point_index) {
        append_colored_line(vertices, points[point_index - 1U], points[point_index], color);
    }
    if (closed) {
        append_colored_line(vertices, points.back(), points.front(), color);
    }
}

void append_filled_circle(
    std::vector<ViewportRenderVertex>* vertices,
    const ImVec2& center,
    float radius,
    ImU32 color,
    int segment_count = 18) {
    const ImVec4 float_color = ImGui::ColorConvertU32ToFloat4(color);
    for (int segment_index = 0; segment_index < segment_count; ++segment_index) {
        const float angle0 =
            (2.0f * kPi * static_cast<float>(segment_index)) /
            static_cast<float>(segment_count);
        const float angle1 =
            (2.0f * kPi * static_cast<float>(segment_index + 1)) /
            static_cast<float>(segment_count);
        const ImVec2 point0(
            center.x + (std::cos(angle0) * radius),
            center.y + (std::sin(angle0) * radius));
        const ImVec2 point1(
            center.x + (std::cos(angle1) * radius),
            center.y + (std::sin(angle1) * radius));
        vertices->push_back(viewport_vertex(center, float_color));
        vertices->push_back(viewport_vertex(point0, float_color));
        vertices->push_back(viewport_vertex(point1, float_color));
    }
}

void append_colored_triangle(
    std::vector<ViewportRenderVertex>* vertices,
    const ImVec2& position0,
    const ImVec4& color0,
    const ImVec2& position1,
    const ImVec4& color1,
    const ImVec2& position2,
    const ImVec4& color2) {
    vertices->push_back(viewport_vertex(position0, color0));
    vertices->push_back(viewport_vertex(position1, color1));
    vertices->push_back(viewport_vertex(position2, color2));
}

void append_mesh_weight_overlay_geometry(
    const ViewportLayout& layout,
    const MeshWeightOverlay& overlay,
    std::vector<ViewportRenderVertex>* triangle_vertices,
    std::vector<ViewportRenderVertex>* line_vertices) {
    if (triangle_vertices == nullptr || line_vertices == nullptr) {
        return;
    }

    for (std::size_t triangle_index = 0; triangle_index + 2U < overlay.triangles.size();
         triangle_index += 3U) {
        const std::size_t a = overlay.triangles[triangle_index];
        const std::size_t b = overlay.triangles[triangle_index + 1U];
        const std::size_t c = overlay.triangles[triangle_index + 2U];
        if (a >= overlay.vertices.size() ||
            b >= overlay.vertices.size() ||
            c >= overlay.vertices.size()) {
            continue;
        }

        append_colored_triangle(
            triangle_vertices,
            local_viewport_position(layout, overlay.vertices[a].screen_position),
            mesh_weight_heatmap_color(overlay.vertices[a].weight),
            local_viewport_position(layout, overlay.vertices[b].screen_position),
            mesh_weight_heatmap_color(overlay.vertices[b].weight),
            local_viewport_position(layout, overlay.vertices[c].screen_position),
            mesh_weight_heatmap_color(overlay.vertices[c].weight));
    }

    for (const MeshWeightOverlayVertex& vertex : overlay.vertices) {
        append_filled_circle(
            triangle_vertices,
            local_viewport_position(layout, vertex.screen_position),
            std::clamp(layout.render_joint_radius * 0.75f, 4.0f, 8.0f),
            ImGui::ColorConvertFloat4ToU32(mesh_weight_heatmap_color(vertex.weight, 0.82f)),
            14);
    }
}

marrow::runtime::AttachmentVertex transform_attachment_vertex_local(
    const marrow::runtime::BoneWorldTransform& transform,
    double x,
    double y) {
    return marrow::runtime::AttachmentVertex{
        static_cast<double>(transform.world_x) +
            (static_cast<double>(transform.a) * x) +
            (static_cast<double>(transform.b) * y),
        static_cast<double>(transform.world_y) +
            (static_cast<double>(transform.c) * x) +
            (static_cast<double>(transform.d) * y)};
}

marrow::runtime::AttachmentVertex transform_attachment_vertex_local(
    const marrow::runtime::BoneWorldTransform& transform,
    float x,
    float y) {
    return transform_attachment_vertex_local(
        transform,
        static_cast<double>(x),
        static_cast<double>(y));
}

std::optional<marrow::runtime::AttachmentVertex> longest_child_local_offset(
    const marrow::runtime::Skeleton& skeleton,
    const marrow::runtime::SkeletonData& skeleton_data,
    std::size_t bone_index) {
    const auto& poses = skeleton.bone_poses();
    if (bone_index >= poses.size()) {
        return std::nullopt;
    }

    marrow::runtime::AttachmentVertex tip{};
    double best_length_squared = 0.0;
    for (std::size_t child_index = 0; child_index < skeleton_data.bones().size(); ++child_index) {
        if (skeleton_data.bones()[child_index].parent_index != std::optional<std::size_t>{bone_index} ||
            child_index >= poses.size()) {
            continue;
        }

        const auto& child_pose = poses[child_index].local_pose;
        const double length_squared =
            (static_cast<double>(child_pose.x) * static_cast<double>(child_pose.x)) +
            (static_cast<double>(child_pose.y) * static_cast<double>(child_pose.y));
        if (length_squared <= best_length_squared) {
            continue;
        }

        tip = marrow::runtime::AttachmentVertex{child_pose.x, child_pose.y};
        best_length_squared = length_squared;
    }

    if (best_length_squared <= 1e-8) {
        return std::nullopt;
    }
    return tip;
}

std::optional<marrow::runtime::AttachmentVertex> bone_tip_world_position(
    const marrow::runtime::Skeleton& skeleton,
    const marrow::runtime::SkeletonData& skeleton_data,
    std::size_t bone_index) {
    const auto& world_transforms = skeleton.bone_world_transforms();
    if (bone_index >= world_transforms.size()) {
        return std::nullopt;
    }

    const auto local_tip = longest_child_local_offset(skeleton, skeleton_data, bone_index);
    if (!local_tip.has_value()) {
        return std::nullopt;
    }

    return transform_attachment_vertex_local(
        world_transforms[bone_index],
        local_tip->x,
        local_tip->y);
}

void add_debug_line_segment(
    DebugOverlayGeometry* overlay,
    const ImVec2& start,
    const ImVec2& end,
    ImU32 color,
    float thickness = 1.0f) {
    if (overlay == nullptr) {
        return;
    }

    overlay->lines.push_back(DebugOverlayLineSegment{start, end, color, thickness});
}

void add_debug_polyline_segments(
    DebugOverlayGeometry* overlay,
    const std::vector<ImVec2>& points,
    ImU32 color,
    float thickness = 1.0f,
    bool closed = false) {
    if (overlay == nullptr || points.size() < 2U) {
        return;
    }

    for (std::size_t point_index = 1; point_index < points.size(); ++point_index) {
        add_debug_line_segment(
            overlay,
            points[point_index - 1U],
            points[point_index],
            color,
            thickness);
    }
    if (closed) {
        add_debug_line_segment(overlay, points.back(), points.front(), color, thickness);
    }
}

void add_debug_cross_marker(
    DebugOverlayGeometry* overlay,
    const ImVec2& center,
    float radius,
    ImU32 color,
    float thickness = 1.0f) {
    if (overlay == nullptr || radius <= 0.0f) {
        return;
    }

    add_debug_line_segment(
        overlay,
        ImVec2(center.x - radius, center.y),
        ImVec2(center.x + radius, center.y),
        color,
        thickness);
    add_debug_line_segment(
        overlay,
        ImVec2(center.x, center.y - radius),
        ImVec2(center.x, center.y + radius),
        color,
        thickness);
}

void add_debug_arrow(
    DebugOverlayGeometry* overlay,
    const ImVec2& start,
    const ImVec2& end,
    ImU32 color,
    float thickness = 1.0f) {
    if (overlay == nullptr) {
        return;
    }

    add_debug_line_segment(overlay, start, end, color, thickness);
    const ImVec2 direction(end.x - start.x, end.y - start.y);
    const float length = std::sqrt((direction.x * direction.x) + (direction.y * direction.y));
    if (length <= 1e-4f) {
        return;
    }

    const ImVec2 unit(direction.x / length, direction.y / length);
    const ImVec2 perpendicular(-unit.y, unit.x);
    const float head_length = std::min(14.0f, std::max(6.0f, length * 0.28f));
    const ImVec2 head_base(
        end.x - (unit.x * head_length),
        end.y - (unit.y * head_length));
    add_debug_line_segment(
        overlay,
        end,
        ImVec2(
            head_base.x + (perpendicular.x * (head_length * 0.45f)),
            head_base.y + (perpendicular.y * (head_length * 0.45f))),
        color,
        thickness);
    add_debug_line_segment(
        overlay,
        end,
        ImVec2(
            head_base.x - (perpendicular.x * (head_length * 0.45f)),
            head_base.y - (perpendicular.y * (head_length * 0.45f))),
        color,
        thickness);
}

std::vector<marrow::runtime::AttachmentVertex> sample_path_curve_points(
    const std::vector<marrow::runtime::AttachmentVertex>& control_points,
    int samples_per_segment = 16) {
    std::vector<marrow::runtime::AttachmentVertex> sampled_points;
    if (control_points.size() < 4U || samples_per_segment <= 0) {
        return sampled_points;
    }

    for (std::size_t point_index = 0; point_index + 3U < control_points.size(); point_index += 3U) {
        for (int sample_index = 0; sample_index <= samples_per_segment; ++sample_index) {
            if (point_index > 0U && sample_index == 0) {
                continue;
            }

            const double t = static_cast<double>(sample_index) /
                static_cast<double>(samples_per_segment);
            const double inv_t = 1.0 - t;
            const double basis0 = inv_t * inv_t * inv_t;
            const double basis1 = 3.0 * inv_t * inv_t * t;
            const double basis2 = 3.0 * inv_t * t * t;
            const double basis3 = t * t * t;
            const auto& p0 = control_points[point_index];
            const auto& p1 = control_points[point_index + 1U];
            const auto& p2 = control_points[point_index + 2U];
            const auto& p3 = control_points[point_index + 3U];
            sampled_points.push_back(marrow::runtime::AttachmentVertex{
                (static_cast<double>(p0.x) * basis0) +
                    (static_cast<double>(p1.x) * basis1) +
                    (static_cast<double>(p2.x) * basis2) +
                    (static_cast<double>(p3.x) * basis3),
                (static_cast<double>(p0.y) * basis0) +
                    (static_cast<double>(p1.y) * basis1) +
                    (static_cast<double>(p2.y) * basis2) +
                    (static_cast<double>(p3.y) * basis3)});
        }
    }

    return sampled_points;
}

void add_debug_spring_segments(
    DebugOverlayGeometry* overlay,
    const ImVec2& start,
    const ImVec2& end,
    ImU32 color,
    float thickness = 1.0f,
    int coil_count = 6,
    float amplitude = 5.0f) {
    if (overlay == nullptr) {
        return;
    }

    const ImVec2 direction(end.x - start.x, end.y - start.y);
    const float length = std::sqrt((direction.x * direction.x) + (direction.y * direction.y));
    if (length <= 1e-4f) {
        add_debug_line_segment(overlay, start, end, color, thickness);
        return;
    }

    const ImVec2 unit(direction.x / length, direction.y / length);
    const ImVec2 perpendicular(-unit.y, unit.x);
    const int points_per_coil = 2;
    const int interior_point_count = std::max(coil_count * points_per_coil, 2);
    std::vector<ImVec2> points;
    points.reserve(static_cast<std::size_t>(interior_point_count + 2));
    points.push_back(start);
    for (int point_index = 1; point_index <= interior_point_count; ++point_index) {
        const float alpha =
            static_cast<float>(point_index) /
            static_cast<float>(interior_point_count + 1);
        const float lateral =
            (point_index % 2 == 0 ? -1.0f : 1.0f) *
            std::min(amplitude, length * 0.18f);
        points.emplace_back(
            start.x + (direction.x * alpha) + (perpendicular.x * lateral),
            start.y + (direction.y * alpha) + (perpendicular.y * lateral));
    }
    points.push_back(end);
    add_debug_polyline_segments(overlay, points, color, thickness, false);
}

DebugOverlayGeometry build_debug_overlay_geometry(
    const ShellState& state,
    const ViewportLayout& layout) {
    DebugOverlayGeometry overlay;
    overlay.stats.bones_enabled = state.viewport.debug_overlay.bones;
    if (!state.load_result || !state.preview_skeleton) {
        return overlay;
    }

    const auto& skeleton = *state.load_result.skeleton_data;
    const ResolvedSelection resolved = resolve_shell_selection(state);
    const auto& world_transforms = state.preview_skeleton->bone_world_transforms();
    if (world_transforms.size() != skeleton.bones().size()) {
        return overlay;
    }

    const auto slot_selected =
        [&](std::size_t slot_index) {
            return resolved.active_slot_index == slot_index;
        };
    const auto constraint_selected =
        [&](ConstraintKind kind, std::string_view name) {
            return resolved.active_constraint.has_value() &&
                resolved.active_constraint->kind == kind &&
                resolved.active_constraint->constraint_name == name;
        };

    if (state.viewport.debug_overlay.ik_constraints) {
        for (const auto& constraint : skeleton.ik_constraints()) {
            if (constraint.bone_indices.empty() ||
                constraint.bone_indices.front() >= world_transforms.size() ||
                constraint.target_bone_index >= world_transforms.size()) {
                continue;
            }

            const bool selected = constraint_selected(ConstraintKind::Ik, constraint.name);
            const ImU32 primary_color = selected
                ? IM_COL32(178, 255, 186, 245)
                : IM_COL32(106, 224, 134, 210);
            const ImU32 secondary_color = selected
                ? IM_COL32(127, 214, 255, 220)
                : IM_COL32(91, 181, 222, 180);
            const ImVec2 origin = screen_from_world(
                layout,
                world_transforms[constraint.bone_indices.front()].world_x,
                world_transforms[constraint.bone_indices.front()].world_y);
            const ImVec2 target = screen_from_world(
                layout,
                world_transforms[constraint.target_bone_index].world_x,
                world_transforms[constraint.target_bone_index].world_y);

            add_debug_line_segment(&overlay, origin, target, secondary_color, 1.5f);
            overlay.circles.push_back(DebugOverlayCircle{
                target,
                std::clamp(layout.render_joint_radius * 0.9f, 4.0f, 8.0f),
                IM_COL32(66, 154, 87, 92),
                primary_color,
                1.4f});
            add_debug_cross_marker(
                &overlay,
                target,
                std::clamp(layout.render_joint_radius * 0.7f, 4.0f, 7.0f),
                primary_color,
                1.6f);

            double first_length = 0.0;
            double second_length = 0.0;
            if (constraint.bone_indices.size() == 1U) {
                if (const auto tip =
                        bone_tip_world_position(
                            *state.preview_skeleton,
                            skeleton,
                            constraint.bone_indices.front())) {
                    const ImVec2 tip_screen = screen_from_world(layout, tip->x, tip->y);
                    first_length = static_cast<double>(std::sqrt(
                        squared_distance(origin, tip_screen)));
                }
            } else if (constraint.bone_indices.size() >= 2U &&
                       constraint.bone_indices[1U] < world_transforms.size()) {
                const ImVec2 joint = screen_from_world(
                    layout,
                    world_transforms[constraint.bone_indices[1U]].world_x,
                    world_transforms[constraint.bone_indices[1U]].world_y);
                first_length = static_cast<double>(std::sqrt(squared_distance(origin, joint)));
                if (const auto tip =
                        bone_tip_world_position(
                            *state.preview_skeleton,
                            skeleton,
                            constraint.bone_indices[1U])) {
                    const ImVec2 tip_screen = screen_from_world(layout, tip->x, tip->y);
                    second_length = static_cast<double>(std::sqrt(squared_distance(joint, tip_screen)));
                }
            }

            const float outer_radius =
                static_cast<float>(std::max(first_length + second_length, first_length));
            const float inner_radius =
                static_cast<float>(std::abs(first_length - second_length));
            if (outer_radius > 1.0f) {
                const float base_angle = std::atan2(target.y - origin.y, target.x - origin.x);
                const float sweep = constraint.bone_indices.size() >= 2U ? 0.95f : 0.70f;
                std::vector<ImVec2> arc_points;
                constexpr int kArcSegments = 24;
                arc_points.reserve(kArcSegments + 1);
                for (int segment_index = 0; segment_index <= kArcSegments; ++segment_index) {
                    const float alpha =
                        static_cast<float>(segment_index) / static_cast<float>(kArcSegments);
                    const float angle = base_angle - sweep + ((2.0f * sweep) * alpha);
                    arc_points.emplace_back(
                        origin.x + (std::cos(angle) * outer_radius),
                        origin.y + (std::sin(angle) * outer_radius));
                }
                add_debug_polyline_segments(&overlay, arc_points, primary_color, 1.4f, false);

                if (constraint.bone_indices.size() >= 2U && inner_radius > 1.0f) {
                    std::vector<ImVec2> inner_arc_points;
                    inner_arc_points.reserve(kArcSegments + 1);
                    for (int segment_index = 0; segment_index <= kArcSegments; ++segment_index) {
                        const float alpha =
                            static_cast<float>(segment_index) /
                            static_cast<float>(kArcSegments);
                        const float angle = base_angle - sweep + ((2.0f * sweep) * alpha);
                        inner_arc_points.emplace_back(
                            origin.x + (std::cos(angle) * inner_radius),
                            origin.y + (std::sin(angle) * inner_radius));
                    }
                    add_debug_polyline_segments(
                        &overlay,
                        inner_arc_points,
                        IM_COL32(81, 171, 108, 140),
                        1.0f,
                        false);
                }
            }

            ++overlay.stats.ik_constraint_count;
        }
    }

    if (state.viewport.debug_overlay.path_constraints) {
        for (const auto& constraint : skeleton.path_constraints()) {
            if (constraint.slot_index >= skeleton.slots().size() ||
                constraint.slot_index >= state.preview_skeleton->slot_states().size()) {
                continue;
            }

            const auto* attachment = state.preview_skeleton->current_attachment(constraint.slot_index);
            if (attachment == nullptr || !attachment->path_attachment.has_value()) {
                continue;
            }

            const std::size_t path_bone_index = skeleton.slots()[constraint.slot_index].bone_index;
            if (path_bone_index >= world_transforms.size()) {
                continue;
            }

            std::vector<marrow::runtime::AttachmentVertex> world_points;
            world_points.reserve(attachment->path_attachment->control_points.size());
            for (const auto& point : attachment->path_attachment->control_points) {
                world_points.push_back(transform_attachment_vertex_local(
                    world_transforms[path_bone_index],
                    point.x,
                    point.y));
            }
            const std::vector<marrow::runtime::AttachmentVertex> sampled_points =
                sample_path_curve_points(world_points);
            if (sampled_points.size() < 2U) {
                continue;
            }

            std::vector<ImVec2> screen_points;
            screen_points.reserve(sampled_points.size());
            for (const auto& point : sampled_points) {
                screen_points.push_back(screen_from_world(layout, point.x, point.y));
            }

            const bool selected = constraint_selected(ConstraintKind::Path, constraint.name);
            add_debug_polyline_segments(
                &overlay,
                screen_points,
                selected ? IM_COL32(135, 214, 255, 245) : IM_COL32(83, 181, 230, 210),
                selected ? 2.0f : 1.5f,
                false);
            ++overlay.stats.path_constraint_count;
        }
    }

    if (state.viewport.debug_overlay.physics_constraints) {
        for (const auto& constraint : skeleton.physics_constraints()) {
            const bool selected = constraint_selected(ConstraintKind::Physics, constraint.name);
            const ImU32 spring_color = selected
                ? IM_COL32(129, 255, 244, 240)
                : IM_COL32(88, 214, 203, 210);
            const ImU32 force_color = selected
                ? IM_COL32(164, 216, 255, 210)
                : IM_COL32(116, 176, 214, 170);

            bool drew_constraint = false;
            for (const std::size_t bone_index : constraint.bone_indices) {
                if (bone_index >= world_transforms.size()) {
                    continue;
                }
                const auto tip = bone_tip_world_position(*state.preview_skeleton, skeleton, bone_index);
                if (!tip.has_value()) {
                    continue;
                }

                const ImVec2 start = screen_from_world(
                    layout,
                    world_transforms[bone_index].world_x,
                    world_transforms[bone_index].world_y);
                const ImVec2 end = screen_from_world(layout, tip->x, tip->y);
                add_debug_spring_segments(
                    &overlay,
                    start,
                    end,
                    spring_color,
                    selected ? 2.0f : 1.4f,
                    6,
                    std::clamp(layout.render_joint_radius * 0.85f, 4.0f, 7.5f));

                const ImVec2 midpoint((start.x + end.x) * 0.5f, (start.y + end.y) * 0.5f);
                const ImVec2 direction(end.x - start.x, end.y - start.y);
                const float length =
                    std::sqrt((direction.x * direction.x) + (direction.y * direction.y));
                if (length > 1e-4f) {
                    const ImVec2 unit(direction.x / length, direction.y / length);
                    const ImVec2 perpendicular(-unit.y, unit.x);
                    const float damper_half =
                        std::clamp(layout.render_joint_radius * 0.8f, 4.0f, 8.0f);
                    add_debug_line_segment(
                        &overlay,
                        ImVec2(
                            midpoint.x - (perpendicular.x * damper_half),
                            midpoint.y - (perpendicular.y * damper_half)),
                        ImVec2(
                            midpoint.x + (perpendicular.x * damper_half),
                            midpoint.y + (perpendicular.y * damper_half)),
                        spring_color,
                        selected ? 2.0f : 1.4f);
                    add_debug_line_segment(
                        &overlay,
                        ImVec2(
                            midpoint.x - (unit.x * (damper_half * 0.7f)),
                            midpoint.y - (unit.y * (damper_half * 0.7f))),
                        ImVec2(
                            midpoint.x + (unit.x * (damper_half * 0.7f)),
                            midpoint.y + (unit.y * (damper_half * 0.7f))),
                        spring_color,
                        1.2f);
                }

                const ImVec2 force_end = screen_from_world(
                    layout,
                    tip->x + (constraint.wind.x * 0.18f),
                    tip->y + (constraint.gravity.y * 0.18f));
                add_debug_arrow(&overlay, end, force_end, force_color, 1.2f);
                drew_constraint = true;
            }

            if (drew_constraint) {
                ++overlay.stats.physics_constraint_count;
            }
        }
    }

    if (state.viewport.debug_overlay.mesh_wireframes) {
        std::vector<bool> seen_slots(skeleton.slots().size(), false);
        for (const std::size_t slot_index : state.preview_skeleton->draw_order()) {
            if (slot_index >= skeleton.slots().size() || seen_slots[slot_index]) {
                continue;
            }
            seen_slots[slot_index] = true;

            const auto pose = state.preview_skeleton->evaluate_current_mesh_attachment(slot_index);
            if (!pose.has_value()) {
                continue;
            }

            const ImU32 color = slot_selected(slot_index)
                ? IM_COL32(255, 194, 120, 235)
                : IM_COL32(244, 152, 96, 180);
            for (std::size_t triangle_index = 0; triangle_index + 2U < pose->triangles.size();
                 triangle_index += 3U) {
                const std::size_t a = pose->triangles[triangle_index];
                const std::size_t b = pose->triangles[triangle_index + 1U];
                const std::size_t c = pose->triangles[triangle_index + 2U];
                if (a >= pose->vertices.size() ||
                    b >= pose->vertices.size() ||
                    c >= pose->vertices.size()) {
                    continue;
                }

                const ImVec2 p0 =
                    screen_from_world(layout, pose->vertices[a].x, pose->vertices[a].y);
                const ImVec2 p1 =
                    screen_from_world(layout, pose->vertices[b].x, pose->vertices[b].y);
                const ImVec2 p2 =
                    screen_from_world(layout, pose->vertices[c].x, pose->vertices[c].y);
                add_debug_line_segment(&overlay, p0, p1, color, 1.2f);
                add_debug_line_segment(&overlay, p1, p2, color, 1.2f);
                add_debug_line_segment(&overlay, p2, p0, color, 1.2f);
            }

            ++overlay.stats.mesh_attachment_count;
        }
    }

    if (state.viewport.debug_overlay.bounding_boxes) {
        marrow::runtime::SkeletonBounds bounds;
        bounds.update(*state.preview_skeleton, false);
        for (const auto& bounding_box : bounds.bounding_boxes()) {
            if (bounding_box.polygon.size() < 2U) {
                continue;
            }

            std::vector<ImVec2> screen_points;
            screen_points.reserve(bounding_box.polygon.size());
            for (const auto& point : bounding_box.polygon) {
                screen_points.push_back(screen_from_world(layout, point.x, point.y));
            }

            const ImU32 color = slot_selected(bounding_box.slot_index)
                ? IM_COL32(255, 122, 122, 220)
                : IM_COL32(226, 95, 95, 170);
            add_debug_polyline_segments(&overlay, screen_points, color, 1.4f, true);
            ++overlay.stats.bounding_box_count;
        }
    }

    return overlay;
}

void append_debug_overlay_geometry(
    const ViewportLayout& layout,
    const DebugOverlayGeometry& overlay,
    std::vector<ViewportRenderVertex>* line_vertices,
    std::vector<ViewportRenderVertex>* triangle_vertices) {
    if (line_vertices == nullptr || triangle_vertices == nullptr) {
        return;
    }

    for (const auto& line : overlay.lines) {
        append_colored_line(
            line_vertices,
            local_viewport_position(layout, line.start),
            local_viewport_position(layout, line.end),
            line.color);
    }
    for (const auto& circle : overlay.circles) {
        if ((circle.fill_color & IM_COL32_A_MASK) != 0U) {
            append_filled_circle(
                triangle_vertices,
                local_viewport_position(layout, circle.center),
                circle.radius,
                circle.fill_color,
                18);
        }
        if ((circle.outline_color & IM_COL32_A_MASK) != 0U) {
            std::vector<ImVec2> circle_points;
            constexpr int kCircleSegments = 24;
            circle_points.reserve(kCircleSegments);
            for (int segment_index = 0; segment_index < kCircleSegments; ++segment_index) {
                const float angle =
                    (2.0f * kPi * static_cast<float>(segment_index)) /
                    static_cast<float>(kCircleSegments);
                circle_points.emplace_back(
                    local_viewport_position(layout, circle.center).x +
                        (std::cos(angle) * circle.radius),
                    local_viewport_position(layout, circle.center).y +
                        (std::sin(angle) * circle.radius));
            }
            append_polyline_lines(line_vertices, circle_points, circle.outline_color, true);
        }
    }
}

void append_viewport_pose_geometry(
    const ViewportLayout& layout,
    const std::vector<BoneCanvasNode>& bones,
    float joint_radius,
    const std::vector<bool>* selected_bones,
    std::optional<std::size_t> active_bone,
    std::optional<std::size_t> hovered_bone,
    ImU32 active_line_color,
    ImU32 inactive_line_color,
    ImU32 selected_line_color,
    ImU32 active_fill_color,
    ImU32 inactive_fill_color,
    ImU32 hovered_fill_color,
    ImU32 selected_fill_color,
    std::vector<ViewportRenderVertex>* line_vertices,
    std::vector<ViewportRenderVertex>* triangle_vertices) {
    for (const BoneCanvasNode& node : bones) {
        if (!node.parent_index.has_value() || *node.parent_index >= bones.size()) {
            continue;
        }

        const BoneCanvasNode& parent = bones[*node.parent_index];
        const bool selected = selected_bones != nullptr &&
            node.bone_index < selected_bones->size() &&
            (*selected_bones)[node.bone_index];
        const bool active = active_bone == node.bone_index;
        const ImU32 line_color = selected
            ? selected_line_color
            : node.active ? active_line_color : inactive_line_color;
        append_colored_line(
            line_vertices,
            local_viewport_position(layout, parent.screen_position),
            local_viewport_position(layout, node.screen_position),
            line_color);
    }

    for (const BoneCanvasNode& node : bones) {
        const bool selected = selected_bones != nullptr &&
            node.bone_index < selected_bones->size() &&
            (*selected_bones)[node.bone_index];
        const bool active = active_bone == node.bone_index;
        const bool hovered_selection =
            hovered_bone.has_value() && *hovered_bone == node.bone_index;
        const float radius = joint_radius + (active ? 2.5f : selected ? 1.25f : 0.0f);
        const ImU32 fill_color = selected
            ? selected_fill_color
            : hovered_selection ? hovered_fill_color
                                : node.active ? active_fill_color : inactive_fill_color;
        append_filled_circle(
            triangle_vertices,
            local_viewport_position(layout, node.screen_position),
            radius,
            fill_color);
    }
}

void build_viewport_background_geometry(
    const ShellState& state,
    const ViewportLayout& layout,
    const std::vector<OnionSkinGhostPose>& ghost_poses,
    ViewportGeometryPass* geometry) {
    if (geometry == nullptr) {
        return;
    }

    auto& line_vertices = geometry->line_vertices;
    auto& triangle_vertices = geometry->triangle_vertices;
    const float grid_spacing = std::max(18.0f, 40.0f * static_cast<float>(state.viewport.zoom));
    for (float x = first_grid_line(layout.world_origin_screen.x, layout.canvas_origin.x, grid_spacing);
         x < layout.canvas_end.x;
         x += grid_spacing) {
        append_colored_line(
            &line_vertices,
            ImVec2(x - layout.canvas_origin.x, 0.0f),
            ImVec2(x - layout.canvas_origin.x, layout.canvas_size.y),
            IM_COL32(31, 35, 41, 255));
    }
    for (float y = first_grid_line(layout.world_origin_screen.y, layout.canvas_origin.y, grid_spacing);
         y < layout.canvas_end.y;
         y += grid_spacing) {
        append_colored_line(
            &line_vertices,
            ImVec2(0.0f, y - layout.canvas_origin.y),
            ImVec2(layout.canvas_size.x, y - layout.canvas_origin.y),
            IM_COL32(31, 35, 41, 255));
    }

    append_colored_line(
        &line_vertices,
        ImVec2(0.0f, layout.world_origin_screen.y - layout.canvas_origin.y),
        ImVec2(layout.canvas_size.x, layout.world_origin_screen.y - layout.canvas_origin.y),
        IM_COL32(189, 86, 37, 255));
    append_colored_line(
        &line_vertices,
        ImVec2(layout.world_origin_screen.x - layout.canvas_origin.x, 0.0f),
        ImVec2(layout.world_origin_screen.x - layout.canvas_origin.x, layout.canvas_size.y),
        IM_COL32(204, 177, 110, 255));

    for (const OnionSkinGhostPose& ghost_pose : ghost_poses) {
        append_viewport_pose_geometry(
            layout,
            ghost_pose.bones,
            layout.render_joint_radius * 0.9f,
            nullptr,
            std::nullopt,
            std::nullopt,
            ghost_pose.line_color,
            ghost_pose.line_color,
            ghost_pose.line_color,
            ghost_pose.fill_color,
            ghost_pose.fill_color,
            ghost_pose.fill_color,
            ghost_pose.fill_color,
            &line_vertices,
            &triangle_vertices);
    }
}

void build_viewport_overlay_geometry(
    const ShellState& state,
    const ViewportLayout& layout,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
    ViewportGeometryPass* geometry) {
    if (geometry == nullptr) {
        return;
    }

    auto& line_vertices = geometry->line_vertices;
    auto& triangle_vertices = geometry->triangle_vertices;
    if (mesh_weight_overlay != nullptr) {
        append_mesh_weight_overlay_geometry(
            layout,
            *mesh_weight_overlay,
            &triangle_vertices,
            &line_vertices);
    }

    const DebugOverlayGeometry debug_overlay = build_debug_overlay_geometry(state, layout);
    append_debug_overlay_geometry(layout, debug_overlay, &line_vertices, &triangle_vertices);

    if (state.viewport.debug_overlay.bones) {
        const ResolvedSelection resolved = resolve_shell_selection(state);
        std::vector<bool> selected_bones(
            state.load_result.skeleton_data->bones().size(), false);
        for (const marrow::editor::SelectionItem& item : state.selection.items()) {
            const auto* bone = std::get_if<marrow::editor::BoneSelection>(&item);
            if (bone == nullptr) {
                continue;
            }
            if (const auto index =
                    state.load_result.skeleton_data->find_bone_index(bone->bone_name)) {
                selected_bones[*index] = true;
            }
        }
        append_viewport_pose_geometry(
            layout,
            layout.bones,
            layout.render_joint_radius,
            &selected_bones,
            resolved.active_bone_index,
            hovered_bone,
            IM_COL32(214, 163, 76, 220),
            IM_COL32(111, 117, 125, 180),
            IM_COL32(247, 204, 114, 255),
            IM_COL32(208, 134, 57, 230),
            IM_COL32(98, 103, 110, 200),
            IM_COL32(226, 186, 97, 240),
            IM_COL32(247, 204, 114, 255),
            &line_vertices,
            &triangle_vertices);
    }
}

void build_viewport_render_geometry(
    const ShellState& state,
    const ViewportLayout& layout,
    const std::vector<OnionSkinGhostPose>& ghost_poses,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
    std::vector<ViewportRenderVertex>* line_vertices,
    std::vector<ViewportRenderVertex>* triangle_vertices) {
    if (line_vertices == nullptr || triangle_vertices == nullptr) {
        return;
    }

    ViewportGeometryPass background_geometry;
    background_geometry.line_vertices.reserve((layout.bones.size() * 8U) + 1024U);
    background_geometry.triangle_vertices.reserve(layout.bones.size() * 24U);
    build_viewport_background_geometry(state, layout, ghost_poses, &background_geometry);

    ViewportGeometryPass overlay_geometry;
    overlay_geometry.line_vertices.reserve((layout.bones.size() * 24U) + 1024U);
    overlay_geometry.triangle_vertices.reserve(layout.bones.size() * 72U);
    build_viewport_overlay_geometry(
        state,
        layout,
        hovered_bone,
        mesh_weight_overlay,
        &overlay_geometry);

    line_vertices->insert(
        line_vertices->end(),
        background_geometry.line_vertices.begin(),
        background_geometry.line_vertices.end());
    line_vertices->insert(
        line_vertices->end(),
        overlay_geometry.line_vertices.begin(),
        overlay_geometry.line_vertices.end());
    triangle_vertices->insert(
        triangle_vertices->end(),
        background_geometry.triangle_vertices.begin(),
        background_geometry.triangle_vertices.end());
    triangle_vertices->insert(
        triangle_vertices->end(),
        overlay_geometry.triangle_vertices.begin(),
        overlay_geometry.triangle_vertices.end());
}

std::optional<std::string> render_prepared_scene_framebuffer(
    const ViewportLayout& layout,
    const ViewportGeometryPass& background_geometry,
    const ViewportGeometryPass& overlay_geometry,
    const std::vector<OnionSkinTexturedGhost>& textured_ghosts,
    const marrow::renderer::PreparedScene& scene,
    const std::filesystem::path& atlas_image_path,
    ViewportRenderResources* resources) {
    if (resources == nullptr || !resources->available) {
        return "Viewport renderer is unavailable.";
    }
    if (resources->color_attachment_view.id == SG_INVALID_ID ||
        resources->depth_stencil_view.id == SG_INVALID_ID) {
        return "Viewport framebuffer has not been created.";
    }

    const std::array<float, 16> projection = viewport_projection_matrix(layout);
    std::vector<marrow::editor::ViewportRenderer::Submission> submissions;
    submissions.reserve(textured_ghosts.size() + 1U);
    for (const OnionSkinTexturedGhost& ghost : textured_ghosts) {
        submissions.emplace_back();
        if (const auto error = resources->prepared_scene_renderer.prepare(
                ghost.scene,
                atlas_image_path,
                projection,
                ghost.tint_color,
                &submissions.back())) {
            return error;
        }
    }

    submissions.emplace_back();
    if (const auto error = resources->prepared_scene_renderer.prepare(
            scene,
            atlas_image_path,
            projection,
            {{1.0f, 1.0f, 1.0f, 1.0f}},
            &submissions.back())) {
        return error;
    }
    std::vector<const marrow::editor::ViewportRenderer::Submission*> submission_refs;
    submission_refs.reserve(submissions.size());
    for (const auto& submission : submissions) {
        submission_refs.push_back(&submission);
    }
    if (const auto error = resources->prepared_scene_renderer.preflight(submission_refs)) {
        return error;
    }

    const std::size_t overlay_bytes =
        (background_geometry.line_vertices.size() +
         background_geometry.triangle_vertices.size() +
         overlay_geometry.line_vertices.size() +
         overlay_geometry.triangle_vertices.size()) * sizeof(ViewportRenderVertex);
    if (overlay_bytes > resources->overlay_vertex_capacity_bytes) {
        std::size_t new_capacity = std::max<std::size_t>(
            resources->overlay_vertex_capacity_bytes,
            256U * 1024U);
        while (new_capacity < overlay_bytes) {
            if (new_capacity > std::numeric_limits<std::size_t>::max() / 2U) {
                return "Viewport overlay stream capacity overflowed addressable memory.";
            }
            new_capacity *= 2U;
        }
        sg_buffer_desc replacement_desc{};
        replacement_desc.size = new_capacity;
        replacement_desc.usage.vertex_buffer = true;
        replacement_desc.usage.immutable = false;
        replacement_desc.usage.stream_update = true;
        replacement_desc.label = "marrow-viewport-overlay-stream-grown";
        const sg_buffer replacement = sg_make_buffer(&replacement_desc);
        if (const auto error = viewport_resource_error(
                "grown overlay stream buffer", sg_query_buffer_state(replacement))) {
            if (replacement.id != SG_INVALID_ID) {
                sg_destroy_buffer(replacement);
            }
            return error;
        }
        const sg_buffer previous = resources->overlay_vertex_buffer;
        resources->overlay_vertex_buffer = replacement;
        resources->overlay_vertex_capacity_bytes = new_capacity;
        if (previous.id != SG_INVALID_ID) {
            sg_destroy_buffer(previous);
        }
    }

    sg_pass pass{};
    pass.attachments.colors[0] = resources->color_attachment_view;
    pass.attachments.depth_stencil = resources->depth_stencil_view;
    pass.action.colors[0].load_action = SG_LOADACTION_CLEAR;
    pass.action.colors[0].store_action = SG_STOREACTION_STORE;
    pass.action.colors[0].clear_value = {0.07f, 0.08f, 0.10f, 1.0f};
    pass.action.depth.load_action = SG_LOADACTION_CLEAR;
    pass.action.depth.store_action = SG_STOREACTION_DONTCARE;
    pass.action.depth.clear_value = 1.0f;
    pass.action.stencil.load_action = SG_LOADACTION_CLEAR;
    pass.action.stencil.store_action = SG_STOREACTION_DONTCARE;
    pass.action.stencil.clear_value = 0U;
    pass.label = "marrow-editor-viewport-pass";
    sg_begin_pass(&pass);

    marrow_renderer_viewport_overlay_vs_params_t overlay_params{};
    overlay_params.view_size[0] = std::max(layout.canvas_size.x, 1.0f);
    overlay_params.view_size[1] = std::max(layout.canvas_size.y, 1.0f);
    const auto draw_vertices = [&](const std::vector<ViewportRenderVertex>& vertices,
                                   sg_pipeline pipeline) -> std::optional<std::string> {
        if (vertices.empty()) {
            return std::nullopt;
        }
        const sg_range range{vertices.data(), vertices.size() * sizeof(ViewportRenderVertex)};
        const int offset = sg_append_buffer(resources->overlay_vertex_buffer, &range);
        if (sg_query_buffer_overflow(resources->overlay_vertex_buffer)) {
            return "Viewport overlay stream overflowed after successful preflight.";
        }
        sg_bindings bindings{};
        bindings.vertex_buffers[0] = resources->overlay_vertex_buffer;
        bindings.vertex_buffer_offsets[0] = offset;
        sg_apply_pipeline(pipeline);
        sg_apply_bindings(&bindings);
        sg_apply_uniforms(
            UB_marrow_renderer_viewport_overlay_vs_params,
            SG_RANGE(overlay_params));
        sg_draw(0, static_cast<int>(vertices.size()), 1);
        return std::nullopt;
    };

    std::optional<std::string> render_error;
    render_error = draw_vertices(
        background_geometry.line_vertices,
        resources->overlay_line_pipeline);
    if (!render_error) {
        render_error = draw_vertices(
            background_geometry.triangle_vertices,
            resources->overlay_triangle_pipeline);
    }
    for (const auto& submission : submissions) {
        if (render_error) {
            break;
        }
        render_error = resources->prepared_scene_renderer.submit(submission);
    }
    if (!render_error) {
        render_error = draw_vertices(
            overlay_geometry.line_vertices,
            resources->overlay_line_pipeline);
    }
    if (!render_error) {
        render_error = draw_vertices(
            overlay_geometry.triangle_vertices,
            resources->overlay_triangle_pipeline);
    }
    sg_end_pass();
    return render_error;
}

std::optional<std::string> render_viewport_framebuffer(
    const ShellState& state,
    const ViewportLayout& layout,
    const std::vector<OnionSkinGhostPose>& ghost_poses,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
    const marrow::renderer::PreparedScene* prepared_scene,
    ViewportRenderResources* resources) {
    if (!state.load_result || !state.preview_skeleton || state.load_result.atlas_data.empty()) {
        return "Viewport preview scene is unavailable.";
    }

    ViewportGeometryPass background_geometry;
    background_geometry.line_vertices.reserve((layout.bones.size() * 8U) + 1024U);
    background_geometry.triangle_vertices.reserve(layout.bones.size() * 24U);
    build_viewport_background_geometry(state, layout, ghost_poses, &background_geometry);

    ViewportGeometryPass overlay_geometry;
    overlay_geometry.line_vertices.reserve((layout.bones.size() * 24U) + 1024U);
    overlay_geometry.triangle_vertices.reserve(layout.bones.size() * 72U);
    build_viewport_overlay_geometry(
        state,
        layout,
        hovered_bone,
        mesh_weight_overlay,
        &overlay_geometry);

    std::vector<OnionSkinTexturedGhost> textured_ghosts;
    if (state.viewport.onion_skin.enabled && state.load_result.skeleton_data) {
        const std::vector<OnionSkinSampleSpec> ghost_specs =
            build_onion_skin_sample_specs(state);
        if (!ghost_specs.empty()) {
            marrow::runtime::Skeleton sampled_skeleton(state.load_result.skeleton_data);
            const auto& atlas = *state.load_result.atlas_data.front();
            textured_ghosts.reserve(ghost_specs.size());
            for (auto it = ghost_specs.rbegin(); it != ghost_specs.rend(); ++it) {
                if (!sample_preview_pose_at_time(state, it->time_seconds, &sampled_skeleton)) {
                    continue;
                }
                auto ghost_scene_result =
                    marrow::renderer::prepare_setup_pose_scene(sampled_skeleton, atlas);
                if (!ghost_scene_result) {
                    continue;
                }
                const float alpha = onion_skin_alpha(it->distance_rank);
                std::array<float, 4> tint;
                if (it->before_current) {
                    tint = {{0.38f, 0.67f, 1.0f, alpha}};
                } else {
                    tint = {{1.0f, 0.55f, 0.40f, alpha}};
                }
                textured_ghosts.push_back(OnionSkinTexturedGhost{
                    std::move(*ghost_scene_result.scene), tint});
            }
        }
    }

    std::optional<marrow::renderer::PreparedScene> owned_scene;
    if (prepared_scene == nullptr) {
        marrow::renderer::PreparedSceneResult scene_result =
            marrow::renderer::prepare_setup_pose_scene(
                *state.preview_skeleton,
                *state.load_result.atlas_data.front());
        if (!scene_result) {
            return scene_result.error_message;
        }
        owned_scene = std::move(*scene_result.scene);
        prepared_scene = &*owned_scene;
    }

    return render_prepared_scene_framebuffer(
        layout,
        background_geometry,
        overlay_geometry,
        textured_ghosts,
        *prepared_scene,
        resolve_viewport_atlas_image_path(state, *prepared_scene),
        resources);
}

std::optional<std::size_t> pick_bone_at_position(
    const ViewportLayout& layout,
    const ImVec2& position) {
    std::optional<std::size_t> best_bone;
    float best_distance = std::numeric_limits<float>::max();
    const float joint_threshold_squared =
        kBoneJointHitRadiusPixels * kBoneJointHitRadiusPixels;

    for (const BoneCanvasNode& node : layout.bones) {
        const float distance = squared_distance(position, node.screen_position);
        if (distance <= joint_threshold_squared && distance < best_distance) {
            best_distance = distance;
            best_bone = node.bone_index;
        }
    }

    if (best_bone.has_value()) {
        return best_bone;
    }

    const float segment_threshold_squared =
        kBoneBodyHitThresholdPixels * kBoneBodyHitThresholdPixels;
    for (const BoneCanvasNode& node : layout.bones) {
        if (!node.parent_index.has_value() || *node.parent_index >= layout.bones.size()) {
            continue;
        }

        const BoneCanvasNode& parent = layout.bones[*node.parent_index];
        const float distance = point_segment_distance_squared(
            position,
            parent.screen_position,
            node.screen_position);
        if (distance <= segment_threshold_squared && distance < best_distance) {
            best_distance = distance;
            best_bone = node.bone_index;
        }
    }

    return best_bone;
}

namespace {

std::optional<marrow::editor::AttachmentSelection> viewport_attachment_identity(
    const ShellState& state,
    std::size_t slot_index,
    std::string_view attachment_name) {
    if (!state.load_result || slot_index >= state.load_result.skeleton_data->slots().size()) {
        return std::nullopt;
    }
    const auto current = current_attachment_selection(state, slot_index);
    if (!current.has_value() || current->attachment_name != attachment_name) {
        return std::nullopt;
    }
    const auto reference = resolve_attachment_reference(
        *state.load_result.skeleton_data, *current);
    if (!reference.has_value() || !reference->skin_index.has_value() ||
        *reference->skin_index >= state.load_result.skeleton_data->skins().size()) {
        return std::nullopt;
    }
    return marrow::editor::AttachmentSelection{
        state.load_result.skeleton_data->slots()[slot_index].name,
        state.load_result.skeleton_data->skins()[*reference->skin_index].name,
        std::string(attachment_name)};
}

bool point_in_triangle_inclusive(
    const ImVec2& point,
    const std::array<ImVec2, 3>& triangle) {
    const auto cross = [](const ImVec2& a, const ImVec2& b, const ImVec2& c) {
        return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
    };
    constexpr float kEdgeEpsilon = 1e-4f;
    // A degenerate triangle keeps every cross product inside the epsilon
    // band, which would otherwise report a hit for any point.
    if (std::abs(cross(triangle[0], triangle[1], triangle[2])) <= kEdgeEpsilon) {
        return false;
    }
    const float c0 = cross(triangle[0], triangle[1], point);
    const float c1 = cross(triangle[1], triangle[2], point);
    const float c2 = cross(triangle[2], triangle[0], point);
    const bool has_negative = c0 < -kEdgeEpsilon || c1 < -kEdgeEpsilon || c2 < -kEdgeEpsilon;
    const bool has_positive = c0 > kEdgeEpsilon || c1 > kEdgeEpsilon || c2 > kEdgeEpsilon;
    return !(has_negative && has_positive);
}

void append_attachment_triangles(
    ViewportEntityHitGeometry* geometry,
    const ViewportLayout& layout,
    const marrow::editor::AttachmentSelection& selection,
    const std::vector<marrow::renderer::RenderPoint>& vertices,
    const std::vector<std::size_t>& indices,
    std::size_t stable_order,
    ImVec2* centroid_sum,
    std::size_t* centroid_count) {
    if (geometry == nullptr || centroid_sum == nullptr || centroid_count == nullptr) {
        return;
    }
    std::vector<ImVec2> screen_vertices;
    screen_vertices.reserve(vertices.size());
    for (const auto& vertex : vertices) {
        const ImVec2 screen = screen_from_world(layout, vertex.x, vertex.y);
        screen_vertices.push_back(screen);
        centroid_sum->x += screen.x;
        centroid_sum->y += screen.y;
        ++(*centroid_count);
    }
    for (std::size_t index = 0; index + 2U < indices.size(); index += 3U) {
        const std::size_t a = indices[index];
        const std::size_t b = indices[index + 1U];
        const std::size_t c = indices[index + 2U];
        if (a >= screen_vertices.size() || b >= screen_vertices.size() ||
            c >= screen_vertices.size()) {
            continue;
        }
        geometry->triangles.push_back(ViewportHitTriangle{
            ViewportEntityHitCandidate{
                selection,
                ViewportEntityHitPriority::AttachmentSurface,
                0.0f,
                stable_order},
            {screen_vertices[a], screen_vertices[b], screen_vertices[c]}});
    }
}

} // namespace

ViewportEntityHitGeometry build_viewport_entity_hit_geometry(
    const ShellState& state,
    const ViewportLayout& layout,
    const marrow::renderer::PreparedScene* prepared_scene) {
    ViewportEntityHitGeometry geometry;
    if (!state.load_result || !state.preview_skeleton) {
        return geometry;
    }

    const auto& skeleton = *state.load_result.skeleton_data;
    const auto& world_transforms = state.preview_skeleton->bone_world_transforms();
    std::size_t constraint_order = 0U;
    constexpr float kConstraintHitRadius = 8.0f;

    if (state.viewport.debug_overlay.ik_constraints) {
        for (const auto& constraint : skeleton.ik_constraints()) {
            if (constraint.target_bone_index >= world_transforms.size()) {
                ++constraint_order;
                continue;
            }
            geometry.circles.push_back(ViewportHitCircle{
                ViewportEntityHitCandidate{
                    marrow::editor::ConstraintSelection{
                        ConstraintKind::Ik, constraint.name},
                    ViewportEntityHitPriority::ConstraintTarget,
                    0.0f,
                    constraint_order++},
                screen_from_world(
                    layout,
                    world_transforms[constraint.target_bone_index].world_x,
                    world_transforms[constraint.target_bone_index].world_y),
                kConstraintHitRadius,
                ViewportHitMarkerShape::Circle});
        }
    } else {
        constraint_order += skeleton.ik_constraints().size();
    }

    if (state.viewport.debug_overlay.path_constraints) {
        for (const auto& constraint : skeleton.path_constraints()) {
            const std::size_t stable_order = constraint_order++;
            if (constraint.slot_index >= skeleton.slots().size() ||
                constraint.slot_index >= state.preview_skeleton->slot_states().size()) {
                continue;
            }
            const auto* attachment =
                state.preview_skeleton->current_attachment(constraint.slot_index);
            if (attachment == nullptr || !attachment->path_attachment.has_value()) {
                continue;
            }
            const std::size_t bone_index = skeleton.slots()[constraint.slot_index].bone_index;
            if (bone_index >= world_transforms.size()) {
                continue;
            }
            std::vector<marrow::runtime::AttachmentVertex> world_points;
            world_points.reserve(attachment->path_attachment->control_points.size());
            for (const auto& point : attachment->path_attachment->control_points) {
                world_points.push_back(transform_attachment_vertex_local(
                    world_transforms[bone_index], point.x, point.y));
            }
            const auto sampled = sample_path_curve_points(world_points);
            for (std::size_t index = 1U; index < sampled.size(); ++index) {
                geometry.segments.push_back(ViewportHitSegment{
                    ViewportEntityHitCandidate{
                        marrow::editor::ConstraintSelection{
                            ConstraintKind::Path, constraint.name},
                        ViewportEntityHitPriority::ConstraintTarget,
                        0.0f,
                        stable_order},
                    screen_from_world(layout, sampled[index - 1U].x, sampled[index - 1U].y),
                    screen_from_world(layout, sampled[index].x, sampled[index].y),
                    7.0f});
            }
        }
    } else {
        constraint_order += skeleton.path_constraints().size();
    }

    // Transform constraints have no persisted debug-overlay preference. A
    // transient diamond beside their source target keeps them pickable only
    // while the common bone overlay is visible.
    if (state.viewport.debug_overlay.bones) {
        for (const auto& constraint : skeleton.transform_constraints()) {
            const std::size_t stable_order = constraint_order++;
            if (constraint.source_bone_index >= world_transforms.size()) {
                continue;
            }
            ImVec2 center = screen_from_world(
                layout,
                world_transforms[constraint.source_bone_index].world_x,
                world_transforms[constraint.source_bone_index].world_y);
            center.x += 12.0f;
            center.y -= 12.0f;
            geometry.circles.push_back(ViewportHitCircle{
                ViewportEntityHitCandidate{
                    marrow::editor::ConstraintSelection{
                        ConstraintKind::Transform, constraint.name},
                    ViewportEntityHitPriority::ConstraintTarget,
                    0.0f,
                    stable_order},
                center,
                6.0f,
                ViewportHitMarkerShape::Diamond});
        }
    } else {
        constraint_order += skeleton.transform_constraints().size();
    }

    if (state.viewport.debug_overlay.physics_constraints) {
        for (const auto& constraint : skeleton.physics_constraints()) {
            const std::size_t stable_order = constraint_order++;
            for (const std::size_t bone_index : constraint.bone_indices) {
                if (bone_index >= world_transforms.size()) {
                    continue;
                }
                const auto tip = bone_tip_world_position(
                    *state.preview_skeleton, skeleton, bone_index);
                if (!tip.has_value()) {
                    continue;
                }
                geometry.segments.push_back(ViewportHitSegment{
                    ViewportEntityHitCandidate{
                        marrow::editor::ConstraintSelection{
                            ConstraintKind::Physics, constraint.name},
                        ViewportEntityHitPriority::ConstraintTarget,
                        0.0f,
                        stable_order},
                    screen_from_world(
                        layout,
                        world_transforms[bone_index].world_x,
                        world_transforms[bone_index].world_y),
                    screen_from_world(layout, tip->x, tip->y),
                    7.0f});
            }
        }
    }

    if (prepared_scene == nullptr) {
        return geometry;
    }

    struct SlotCentroid {
        marrow::editor::SlotSelection selection;
        ImVec2 sum{};
        std::size_t count{0U};
        std::size_t stable_order{0U};
    };
    std::vector<std::optional<SlotCentroid>> slot_centroids(skeleton.slots().size());
    const std::vector<std::size_t> quad_indices{0U, 1U, 2U, 0U, 2U, 3U};

    for (std::size_t command_index = 0U;
         command_index < prepared_scene->draw_commands.size();
         ++command_index) {
        const std::size_t stable_order =
            prepared_scene->draw_commands.size() - 1U - command_index;
        std::visit(
            [&](const auto& command) {
                using Command = std::decay_t<decltype(command)>;
                if constexpr (std::is_same_v<Command, marrow::renderer::PreparedStrokeCommand>) {
                    return;
                } else {
                    if (command.slot_index >= skeleton.slots().size()) {
                        return;
                    }
                    const auto identity = viewport_attachment_identity(
                        state, command.slot_index, command.attachment_name);
                    if (!identity.has_value()) {
                        return;
                    }

                    ImVec2 centroid_sum{};
                    std::size_t centroid_count = 0U;
                    if constexpr (
                        std::is_same_v<Command, marrow::renderer::RegionAttachmentDrawCommand>) {
                        std::vector<marrow::renderer::RenderPoint> vertices;
                        std::vector<std::size_t> indices;
                        if (!command.masked_vertices.empty() && !command.masked_indices.empty()) {
                            vertices.reserve(command.masked_vertices.size());
                            for (const auto& vertex : command.masked_vertices) {
                                vertices.push_back(vertex.position);
                            }
                            indices = command.masked_indices;
                        } else {
                            vertices.reserve(command.vertices.size());
                            for (const auto& vertex : command.vertices) {
                                vertices.push_back(vertex.position);
                            }
                            indices = quad_indices;
                        }
                        append_attachment_triangles(
                            &geometry,
                            layout,
                            *identity,
                            vertices,
                            indices,
                            stable_order,
                            &centroid_sum,
                            &centroid_count);
                    } else {
                        std::vector<marrow::renderer::RenderPoint> vertices;
                        std::vector<std::size_t> indices;
                        if (!command.masked_vertices.empty() && !command.masked_indices.empty()) {
                            vertices.reserve(command.masked_vertices.size());
                            for (const auto& vertex : command.masked_vertices) {
                                vertices.push_back(vertex.position);
                            }
                            indices = command.masked_indices;
                        } else {
                            const auto evaluated =
                                marrow::renderer::evaluate_gpu_skinned_vertices(
                                    command, prepared_scene->bone_palette);
                            if (evaluated) {
                                vertices.reserve(evaluated.vertices.size());
                                for (const auto& vertex : evaluated.vertices) {
                                    vertices.push_back(vertex.position);
                                }
                                indices = command.indices;
                            }
                        }
                        append_attachment_triangles(
                            &geometry,
                            layout,
                            *identity,
                            vertices,
                            indices,
                            stable_order,
                            &centroid_sum,
                            &centroid_count);
                    }

                    if (centroid_count > 0U) {
                        slot_centroids[command.slot_index] = SlotCentroid{
                            marrow::editor::SlotSelection{
                                skeleton.slots()[command.slot_index].name},
                            centroid_sum,
                            centroid_count,
                            stable_order};
                    }
                }
            },
            prepared_scene->draw_commands[command_index]);
    }

    for (const auto& centroid : slot_centroids) {
        if (!centroid.has_value() || centroid->count == 0U) {
            continue;
        }
        geometry.circles.push_back(ViewportHitCircle{
            ViewportEntityHitCandidate{
                centroid->selection,
                ViewportEntityHitPriority::SlotHandle,
                0.0f,
                centroid->stable_order},
            ImVec2(
                centroid->sum.x / static_cast<float>(centroid->count),
                centroid->sum.y / static_cast<float>(centroid->count)),
            7.0f,
            ViewportHitMarkerShape::Diamond});
    }
    return geometry;
}

std::optional<ViewportEntityHitCandidate> resolve_viewport_entity_hit_candidates(
    const std::vector<ViewportEntityHitCandidate>& candidates) {
    if (candidates.empty()) {
        return std::nullopt;
    }
    const auto best = std::min_element(
        candidates.begin(),
        candidates.end(),
        [](const ViewportEntityHitCandidate& left,
           const ViewportEntityHitCandidate& right) {
            if (left.priority != right.priority) {
                return left.priority < right.priority;
            }
            if (left.distance_squared != right.distance_squared) {
                return left.distance_squared < right.distance_squared;
            }
            return left.stable_order < right.stable_order;
        });
    return *best;
}

std::optional<ViewportEntityHitCandidate> pick_viewport_entity_at_position(
    const ShellState& state,
    const ViewportLayout& layout,
    const ViewportEntityHitGeometry& geometry,
    const ImVec2& position) {
    std::vector<ViewportEntityHitCandidate> candidates;
    if (state.load_result && state.viewport.debug_overlay.bones) {
        const auto& skeleton = *state.load_result.skeleton_data;
        for (const BoneCanvasNode& node : layout.bones) {
            if (!node.active || node.bone_index >= skeleton.bones().size()) {
                continue;
            }
            const float distance = squared_distance(position, node.screen_position);
            if (distance <= kBoneJointHitRadiusPixels * kBoneJointHitRadiusPixels) {
                candidates.push_back(ViewportEntityHitCandidate{
                    marrow::editor::BoneSelection{skeleton.bones()[node.bone_index].name},
                    ViewportEntityHitPriority::BoneJoint,
                    distance,
                    node.bone_index});
            }
            if (!node.parent_index.has_value() || *node.parent_index >= layout.bones.size()) {
                continue;
            }
            const float body_distance = point_segment_distance_squared(
                position,
                layout.bones[*node.parent_index].screen_position,
                node.screen_position);
            if (body_distance <=
                kBoneBodyHitThresholdPixels * kBoneBodyHitThresholdPixels) {
                candidates.push_back(ViewportEntityHitCandidate{
                    marrow::editor::BoneSelection{skeleton.bones()[node.bone_index].name},
                    ViewportEntityHitPriority::BoneBody,
                    body_distance,
                    node.bone_index});
            }
        }
    }

    for (const ViewportHitCircle& circle : geometry.circles) {
        const float dx = position.x - circle.center.x;
        const float dy = position.y - circle.center.y;
        const float distance = squared_distance(position, circle.center);
        const bool hit = circle.marker_shape == ViewportHitMarkerShape::Diamond
            ? std::abs(dx) + std::abs(dy) <= circle.radius
            : distance <= circle.radius * circle.radius;
        if (hit) {
            ViewportEntityHitCandidate candidate = circle.candidate;
            candidate.distance_squared = distance;
            candidates.push_back(std::move(candidate));
        }
    }
    for (const ViewportHitSegment& segment : geometry.segments) {
        const float distance = point_segment_distance_squared(
            position, segment.start, segment.end);
        if (distance <= segment.radius * segment.radius) {
            ViewportEntityHitCandidate candidate = segment.candidate;
            candidate.distance_squared = distance;
            candidates.push_back(std::move(candidate));
        }
    }
    for (const ViewportHitTriangle& triangle : geometry.triangles) {
        if (point_in_triangle_inclusive(position, triangle.points)) {
            candidates.push_back(triangle.candidate);
        }
    }
    return resolve_viewport_entity_hit_candidates(candidates);
}

std::vector<marrow::editor::SelectionItem> collect_viewport_box_bones(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& first_corner,
    const ImVec2& second_corner) {
    std::vector<std::pair<std::size_t, marrow::editor::SelectionItem>> indexed_bones;
    if (!state.load_result || !state.viewport.debug_overlay.bones) {
        return {};
    }
    const float minimum_x = std::min(first_corner.x, second_corner.x);
    const float maximum_x = std::max(first_corner.x, second_corner.x);
    const float minimum_y = std::min(first_corner.y, second_corner.y);
    const float maximum_y = std::max(first_corner.y, second_corner.y);
    const auto& bones = state.load_result.skeleton_data->bones();
    for (const BoneCanvasNode& node : layout.bones) {
        if (!node.active || node.bone_index >= bones.size() ||
            node.screen_position.x < minimum_x || node.screen_position.x > maximum_x ||
            node.screen_position.y < minimum_y || node.screen_position.y > maximum_y) {
            continue;
        }
        indexed_bones.emplace_back(
            node.bone_index,
            marrow::editor::BoneSelection{bones[node.bone_index].name});
    }
    std::sort(
        indexed_bones.begin(),
        indexed_bones.end(),
        [](const auto& left, const auto& right) { return left.first < right.first; });
    std::vector<marrow::editor::SelectionItem> result;
    result.reserve(indexed_bones.size());
    for (auto& [index, item] : indexed_bones) {
        (void)index;
        result.push_back(std::move(item));
    }
    return result;
}

} // namespace marrow::editor::shell
