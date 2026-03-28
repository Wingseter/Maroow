#include "module_internal.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#define SOKOL_NO_ENTRY
#define SOKOL_IMPL
#if defined(__APPLE__)
#import <Metal/Metal.h>
#define SOKOL_METAL
#else
#define SOKOL_GLCORE
#endif

#include "sokol_gfx.h"
#include "sokol_app.h"
#include "sokol_glue.h"
#include "sokol_log.h"

#include "marrow_renderer_shader.h"

namespace marrow::renderer::internal {

namespace {

constexpr std::size_t kStreamingVertexBufferBytes = 2U * 1024U * 1024U;
constexpr std::size_t kStreamingIndexBufferBytes = 512U * 1024U;
constexpr std::size_t kBonePayloadFloatsPerEntry = 6U;
constexpr std::size_t kPackedBoneVec4Count = 192U;
constexpr int kVsUniformSlot = 0;
constexpr int kFsUniformSlot = 1;
constexpr int kAtlasTextureSlot = 0;
constexpr int kAtlasSamplerSlot = 0;
constexpr std::array<std::uint8_t, 4> kSolidWhiteRgba{{255, 255, 255, 255}};

std::optional<std::string> validate_atlas_texture(const TextureImage& texture) {
    if (texture.width <= 0 || texture.height <= 0) {
        return "Atlas texture image data was invalid.";
    }

    const std::size_t expected_rgba_bytes =
        static_cast<std::size_t>(texture.width) *
        static_cast<std::size_t>(texture.height) * 4U;
    if (texture.rgba8.size() != expected_rgba_bytes) {
        return "Atlas texture image data was invalid.";
    }
    return std::nullopt;
}

sg_filter sokol_filter(std::string_view filter_name) {
    return filter_name == "nearest" ? SG_FILTER_NEAREST : SG_FILTER_LINEAR;
}

sg_wrap sokol_wrap(std::string_view wrap_name) {
    if (wrap_name == "repeat") {
        return SG_WRAP_REPEAT;
    }
    if (wrap_name == "mirrored_repeat") {
        return SG_WRAP_MIRRORED_REPEAT;
    }
    return SG_WRAP_CLAMP_TO_EDGE;
}

sg_blend_factor sokol_blend_factor(BlendFactor factor) {
    switch (factor) {
    case BlendFactor::Zero:
        return SG_BLENDFACTOR_ZERO;
    case BlendFactor::One:
        return SG_BLENDFACTOR_ONE;
    case BlendFactor::SrcAlpha:
        return SG_BLENDFACTOR_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha:
        return SG_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstColor:
        return SG_BLENDFACTOR_DST_COLOR;
    case BlendFactor::OneMinusSrcColor:
        return SG_BLENDFACTOR_ONE_MINUS_SRC_COLOR;
    }

    return SG_BLENDFACTOR_ZERO;
}

float pma_uniform_value(bool premultiplied_alpha) {
    return premultiplied_alpha ? 0.0f : 1.0f;
}

std::optional<std::string> resource_state_error(
    std::string_view label,
    sg_resource_state resource_state) {
    if (resource_state == SG_RESOURCESTATE_VALID) {
        return std::nullopt;
    }

    std::string error_message = "Failed to create ";
    error_message += label;
    error_message += " (state=";
    switch (resource_state) {
    case SG_RESOURCESTATE_INVALID:
        error_message += "invalid";
        break;
    case SG_RESOURCESTATE_ALLOC:
        error_message += "alloc";
        break;
    case SG_RESOURCESTATE_FAILED:
        error_message += "failed";
        break;
    default:
        error_message += "unknown";
        break;
    }
    error_message += ").";
    return error_message;
}

marrow_renderer_vs_params_t make_vs_params(const RenderCommandList& command_list) {
    marrow_renderer_vs_params_t params{};
    std::memcpy(
        params.projection,
        command_list.projection.data(),
        sizeof(params.projection));

    const std::size_t bone_count =
        command_list.bone_palette.size() / kBonePayloadFloatsPerEntry;
    for (std::size_t bone_index = 0; bone_index < bone_count; bone_index += 2U) {
        const std::size_t packed_index = (bone_index / 2U) * 3U;
        const std::size_t first_bone_offset = bone_index * kBonePayloadFloatsPerEntry;
        params.bones[packed_index][0] = command_list.bone_palette[first_bone_offset + 0U];
        params.bones[packed_index][1] = command_list.bone_palette[first_bone_offset + 1U];
        params.bones[packed_index][2] = command_list.bone_palette[first_bone_offset + 2U];
        params.bones[packed_index][3] = command_list.bone_palette[first_bone_offset + 3U];
        params.bones[packed_index + 1U][0] = command_list.bone_palette[first_bone_offset + 4U];
        params.bones[packed_index + 1U][1] = command_list.bone_palette[first_bone_offset + 5U];

        if (bone_index + 1U >= bone_count) {
            continue;
        }

        const std::size_t second_bone_offset =
            (bone_index + 1U) * kBonePayloadFloatsPerEntry;
        params.bones[packed_index + 1U][2] =
            command_list.bone_palette[second_bone_offset + 0U];
        params.bones[packed_index + 1U][3] =
            command_list.bone_palette[second_bone_offset + 1U];
        params.bones[packed_index + 2U][0] =
            command_list.bone_palette[second_bone_offset + 2U];
        params.bones[packed_index + 2U][1] =
            command_list.bone_palette[second_bone_offset + 3U];
        params.bones[packed_index + 2U][2] =
            command_list.bone_palette[second_bone_offset + 4U];
        params.bones[packed_index + 2U][3] =
            command_list.bone_palette[second_bone_offset + 5U];
    }

    return params;
}

marrow_renderer_fs_params_t make_fs_params(bool premultiplied_alpha) {
    marrow_renderer_fs_params_t params{};
    params.pma_control[0] = pma_uniform_value(premultiplied_alpha);
    return params;
}

void configure_common_pipeline_state(
    sg_pipeline_desc* pipeline_desc,
    sg_shader shader,
    sg_pixel_format color_format,
    int sample_count,
    BlendState blend_state) {
    pipeline_desc->shader = shader;
    pipeline_desc->layout.buffers[0].stride = static_cast<int>(sizeof(RenderCommandVertex));
    pipeline_desc->color_count = 1;
    pipeline_desc->colors[0].pixel_format = color_format;
    pipeline_desc->colors[0].blend.enabled = true;
    pipeline_desc->colors[0].blend.src_factor_rgb = sokol_blend_factor(blend_state.src_factor);
    pipeline_desc->colors[0].blend.dst_factor_rgb = sokol_blend_factor(blend_state.dst_factor);
    pipeline_desc->colors[0].blend.op_rgb = SG_BLENDOP_ADD;
    pipeline_desc->colors[0].blend.src_factor_alpha = sokol_blend_factor(blend_state.src_factor);
    pipeline_desc->colors[0].blend.dst_factor_alpha = sokol_blend_factor(blend_state.dst_factor);
    pipeline_desc->colors[0].blend.op_alpha = SG_BLENDOP_ADD;
    pipeline_desc->primitive_type = SG_PRIMITIVETYPE_TRIANGLES;
    pipeline_desc->index_type = SG_INDEXTYPE_UINT32;
    pipeline_desc->cull_mode = SG_CULLMODE_NONE;
    pipeline_desc->sample_count = sample_count;
}

void configure_single_color_layout(sg_pipeline_desc* pipeline_desc) {
    pipeline_desc->layout.attrs[0].format = SG_VERTEXFORMAT_FLOAT2;
    pipeline_desc->layout.attrs[0].offset = offsetof(RenderCommandVertex, local_positions);
    pipeline_desc->layout.attrs[1].format = SG_VERTEXFORMAT_FLOAT2;
    pipeline_desc->layout.attrs[1].offset =
        offsetof(RenderCommandVertex, local_positions) + (sizeof(float) * 2U);
    pipeline_desc->layout.attrs[2].format = SG_VERTEXFORMAT_FLOAT2;
    pipeline_desc->layout.attrs[2].offset =
        offsetof(RenderCommandVertex, local_positions) + (sizeof(float) * 4U);
    pipeline_desc->layout.attrs[3].format = SG_VERTEXFORMAT_FLOAT2;
    pipeline_desc->layout.attrs[3].offset =
        offsetof(RenderCommandVertex, local_positions) + (sizeof(float) * 6U);
    pipeline_desc->layout.attrs[4].format = SG_VERTEXFORMAT_FLOAT2;
    pipeline_desc->layout.attrs[4].offset = offsetof(RenderCommandVertex, uv);
    pipeline_desc->layout.attrs[5].format = SG_VERTEXFORMAT_FLOAT4;
    pipeline_desc->layout.attrs[5].offset = offsetof(RenderCommandVertex, bone_indices);
    pipeline_desc->layout.attrs[6].format = SG_VERTEXFORMAT_FLOAT4;
    pipeline_desc->layout.attrs[6].offset = offsetof(RenderCommandVertex, bone_weights);
    pipeline_desc->layout.attrs[7].format = SG_VERTEXFORMAT_FLOAT4;
    pipeline_desc->layout.attrs[7].offset = offsetof(RenderCommandVertex, light_color);
}

void configure_two_color_layout(sg_pipeline_desc* pipeline_desc) {
    configure_single_color_layout(pipeline_desc);
    pipeline_desc->layout.attrs[8].format = SG_VERTEXFORMAT_UBYTE4N;
    pipeline_desc->layout.attrs[8].offset = offsetof(RenderCommandVertex, dark_color);
}

struct DrawPipelineCacheEntry {
    runtime::BlendMode blend_mode{runtime::BlendMode::Normal};
    ColorShaderVariant shader_variant{ColorShaderVariant::SingleColor};
    bool premultiplied_alpha{false};
    bool stencil_enabled{false};
    std::uint8_t stencil_reference{0};
    sg_pipeline pipeline{};
};

struct ClipPipelineCacheEntry {
    std::uint8_t compare_reference{0};
    std::uint8_t write_mask{0};
    sg_pipeline pipeline{};
};

struct ActiveStencilClip {
    std::size_t clip_attachment_index{0};
    std::uint8_t reference_value{0};
    std::uint8_t parent_reference_value{0};
    std::uint8_t invert_mask{0};
};

class SokolBackend final : public Backend {
public:
    ~SokolBackend() override {
        destroy();
    }

    std::optional<std::string> create(const BackendCreateInfo& create_info) override {
        destroy();

        if (const std::optional<std::string> error =
                validate_atlas_texture(create_info.atlas_texture)) {
            return error;
        }
        if (const std::optional<std::string> error = setup_context(create_info)) {
            destroy();
            return error;
        }

#if defined(__APPLE__)
        if (sg_query_backend() != SG_BACKEND_METAL_MACOS) {
            destroy();
            return "sokol_gfx did not initialize the expected Metal backend on macOS.";
        }
#else
        if (sg_query_backend() != SG_BACKEND_GLCORE) {
            destroy();
            return "sokol_gfx did not initialize the expected OpenGL backend on Linux.";
        }
#endif

        if (const std::optional<std::string> error = create_render_resources(create_info)) {
            destroy();
            return error;
        }

        pass_action_.colors[0].load_action = SG_LOADACTION_CLEAR;
        pass_action_.colors[0].store_action = SG_STOREACTION_STORE;
        pass_action_.colors[0].clear_value = {0.08f, 0.09f, 0.12f, 1.0f};
        pass_action_.depth.load_action = SG_LOADACTION_CLEAR;
        pass_action_.depth.store_action = SG_STOREACTION_DONTCARE;
        pass_action_.depth.clear_value = 1.0f;
        pass_action_.stencil.load_action = SG_LOADACTION_CLEAR;
        pass_action_.stencil.store_action = SG_STOREACTION_DONTCARE;
        pass_action_.stencil.clear_value = 0U;

        created_ = true;
        return std::nullopt;
    }

    void destroy() override {
        if (!sg_isvalid()) {
            reset_handles();
            return;
        }

        destroy_cached_pipelines();

        if (two_color_shader_.id != SG_INVALID_ID) {
            sg_destroy_shader(two_color_shader_);
            two_color_shader_ = {};
        }
        if (single_color_shader_.id != SG_INVALID_ID) {
            sg_destroy_shader(single_color_shader_);
            single_color_shader_ = {};
        }
        if (atlas_sampler_.id != SG_INVALID_ID) {
            sg_destroy_sampler(atlas_sampler_);
            atlas_sampler_ = {};
        }
        if (white_view_.id != SG_INVALID_ID) {
            sg_destroy_view(white_view_);
            white_view_ = {};
        }
        if (white_image_.id != SG_INVALID_ID) {
            sg_destroy_image(white_image_);
            white_image_ = {};
        }
        if (atlas_view_.id != SG_INVALID_ID) {
            sg_destroy_view(atlas_view_);
            atlas_view_ = {};
        }
        if (atlas_image_.id != SG_INVALID_ID) {
            sg_destroy_image(atlas_image_);
            atlas_image_ = {};
        }
        if (offscreen_color_view_.id != SG_INVALID_ID) {
            sg_destroy_view(offscreen_color_view_);
            offscreen_color_view_ = {};
        }
        if (offscreen_color_image_.id != SG_INVALID_ID) {
            sg_destroy_image(offscreen_color_image_);
            offscreen_color_image_ = {};
        }
        if (offscreen_depth_stencil_view_.id != SG_INVALID_ID) {
            sg_destroy_view(offscreen_depth_stencil_view_);
            offscreen_depth_stencil_view_ = {};
        }
        if (offscreen_depth_stencil_image_.id != SG_INVALID_ID) {
            sg_destroy_image(offscreen_depth_stencil_image_);
            offscreen_depth_stencil_image_ = {};
        }
        if (stream_index_buffer_.id != SG_INVALID_ID) {
            sg_destroy_buffer(stream_index_buffer_);
            stream_index_buffer_ = {};
        }
        if (stream_vertex_buffer_.id != SG_INVALID_ID) {
            sg_destroy_buffer(stream_vertex_buffer_);
            stream_vertex_buffer_ = {};
        }

        sg_shutdown();
        reset_handles();
    }

    std::optional<std::string> begin_frame(BackendFrameInfo* frame_info_out) override {
        if (frame_info_out == nullptr) {
            return "Backend frame info output was null.";
        }
        if (!created_) {
            return "Sokol backend was not created.";
        }

        if (headless_offscreen_) {
            frame_info_out->framebuffer_width = framebuffer_width_;
            frame_info_out->framebuffer_height = framebuffer_height_;
        } else {
            frame_info_out->framebuffer_width = std::max(sapp_width(), 1);
            frame_info_out->framebuffer_height = std::max(sapp_height(), 1);
        }
        frame_info_out->should_close = false;
        return std::nullopt;
    }

    std::optional<std::string> submit_commands(const RenderCommandList& command_list) override {
        if (!created_) {
            return "Sokol backend was not created.";
        }
        if (command_list.commands.empty() && command_list.ordered_events.empty()) {
            sg_pass pass{};
            pass.action = pass_action_;
            apply_pass_target(&pass);
            pass.label = "marrow-frame";
            sg_begin_pass(&pass);
            sg_end_pass();
            return std::nullopt;
        }
        if (command_list.bone_palette.empty() ||
            (command_list.bone_palette.size() % kBonePayloadFloatsPerEntry) != 0U) {
            return "Render command list did not contain a valid bone palette.";
        }
        if ((command_list.bone_palette.size() / kBonePayloadFloatsPerEntry) > 128U) {
            return "Render command list exceeded the supported bone uniform budget.";
        }

        const marrow_renderer_vs_params_t vs_params = make_vs_params(command_list);
        const marrow_renderer_fs_params_t fs_params =
            make_fs_params(command_list.premultiplied_alpha);

        sg_pass pass{};
        pass.action = pass_action_;
        apply_pass_target(&pass);
        pass.label = "marrow-frame";
        sg_begin_pass(&pass);

        std::vector<ActiveStencilClip> active_clips;
        for (const RenderCommandEventRef& event : command_list.ordered_events) {
            switch (event.kind) {
            case RenderCommandEventKind::ClipStart: {
                if (event.index >= command_list.clip_commands.size()) {
                    sg_end_pass();
                    return "Render command list clip start event referenced a missing clip command.";
                }
                if (active_clips.size() >= 255U) {
                    sg_end_pass();
                    return "Clip nesting exceeded the 8-bit stencil reference range.";
                }

                const std::optional<SoftwareStencilClipState> stencil_state =
                    stencil_clip_state_for_depth(active_clips.size() + 1U);
                if (!stencil_state.has_value()) {
                    sg_end_pass();
                    return "Failed to allocate a valid stencil reference for the clip stack.";
                }

                ActiveStencilClip active_clip;
                active_clip.clip_attachment_index = event.index;
                active_clip.reference_value = stencil_state->reference_value;
                active_clip.parent_reference_value = stencil_state->parent_reference_value;
                active_clip.invert_mask = stencil_state->invert_mask;
                active_clips.push_back(active_clip);

                if (const std::optional<std::string> error = submit_clip_command(
                        command_list.clip_commands[event.index],
                        active_clips.back(),
                        false,
                        vs_params,
                        fs_params)) {
                    sg_end_pass();
                    return error;
                }
                break;
            }
            case RenderCommandEventKind::Draw: {
                if (event.index >= command_list.commands.size()) {
                    sg_end_pass();
                    return "Render command list draw event referenced a missing draw batch.";
                }
                const std::optional<std::uint8_t> stencil_reference =
                    active_clips.empty()
                        ? std::nullopt
                        : std::optional<std::uint8_t>{active_clips.back().reference_value};
                if (const std::optional<std::string> error = submit_draw_command(
                        command_list.commands[event.index],
                        command_list.premultiplied_alpha,
                        stencil_reference,
                        vs_params,
                        fs_params)) {
                    sg_end_pass();
                    return error;
                }
                break;
            }
            case RenderCommandEventKind::ClipEnd: {
                if (event.index >= command_list.clip_commands.size()) {
                    sg_end_pass();
                    return "Render command list clip end event referenced a missing clip command.";
                }
                if (active_clips.empty()) {
                    sg_end_pass();
                    return "Render command list clip end event underflowed the clip stack.";
                }

                const ActiveStencilClip active_clip = active_clips.back();
                active_clips.pop_back();
                if (active_clip.clip_attachment_index != event.index) {
                    sg_end_pass();
                    return "Render command list clip end event did not match the active clip stack.";
                }

                if (const std::optional<std::string> error = submit_clip_command(
                        command_list.clip_commands[event.index],
                        active_clip,
                        true,
                        vs_params,
                        fs_params)) {
                    sg_end_pass();
                    return error;
                }
                break;
            }
            }
        }

        if (!active_clips.empty()) {
            sg_end_pass();
            return "Render command list finished with an unterminated clip stack.";
        }

        sg_end_pass();
        return std::nullopt;
    }

    void end_frame() override {
        if (created_) {
            sg_commit();
        }
    }

private:
    std::optional<std::string> setup_context(const BackendCreateInfo& create_info) {
#if defined(__APPLE__)
        if (create_info.hidden_window) {
            metal_device_ = MTLCreateSystemDefaultDevice();
            if (metal_device_ == nil) {
                return "Failed to create a Metal device for the headless renderer.";
            }

            sg_desc desc{};
            desc.environment.defaults.color_format = SG_PIXELFORMAT_BGRA8;
            desc.environment.defaults.depth_format = SG_PIXELFORMAT_DEPTH_STENCIL;
            desc.environment.defaults.sample_count = 1;
            desc.environment.metal.device = (__bridge const void*)metal_device_;
            desc.uniform_buffer_size = 1024 * 1024;
            desc.logger.func = slog_func;
            sg_setup(&desc);
            if (!sg_isvalid()) {
                return "Failed to initialize headless sokol_gfx.";
            }

            headless_offscreen_ = true;
            framebuffer_width_ = std::max(create_info.window.width, 1);
            framebuffer_height_ = std::max(create_info.window.height, 1);
            swapchain_color_format_ = SG_PIXELFORMAT_BGRA8;
            swapchain_depth_format_ = SG_PIXELFORMAT_DEPTH_STENCIL;
            swapchain_sample_count_ = 1;
            return create_offscreen_target();
        }
#else
        (void)create_info;
#endif

        const sg_environment environment = sglue_environment();

        sg_desc desc{};
        desc.environment = environment;
        desc.uniform_buffer_size = 1024 * 1024;
        desc.logger.func = slog_func;
        sg_setup(&desc);
        if (!sg_isvalid()) {
            return "Failed to initialize sokol_gfx.";
        }

        headless_offscreen_ = false;
        framebuffer_width_ = 0;
        framebuffer_height_ = 0;
        swapchain_color_format_ = environment.defaults.color_format;
        swapchain_depth_format_ = environment.defaults.depth_format;
        swapchain_sample_count_ = environment.defaults.sample_count;
        return std::nullopt;
    }

    std::optional<std::string> create_render_resources(const BackendCreateInfo& create_info) {
        sg_image_desc image_desc{};
        image_desc.width = create_info.atlas_texture.width;
        image_desc.height = create_info.atlas_texture.height;
        image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        image_desc.data.mip_levels[0].ptr = create_info.atlas_texture.rgba8.data();
        image_desc.data.mip_levels[0].size = create_info.atlas_texture.rgba8.size();
        image_desc.label = "marrow-atlas-image";
        atlas_image_ = sg_make_image(&image_desc);
        if (const std::optional<std::string> error =
                resource_state_error("atlas image", sg_query_image_state(atlas_image_))) {
            return error;
        }

        sg_view_desc view_desc{};
        view_desc.texture.image = atlas_image_;
        view_desc.label = "marrow-atlas-view";
        atlas_view_ = sg_make_view(&view_desc);
        if (const std::optional<std::string> error =
                resource_state_error("atlas texture view", sg_query_view_state(atlas_view_))) {
            return error;
        }

        sg_sampler_desc sampler_desc{};
        sampler_desc.min_filter = sokol_filter(create_info.atlas_filter_min);
        sampler_desc.mag_filter = sokol_filter(create_info.atlas_filter_mag);
        sampler_desc.mipmap_filter = sokol_filter(create_info.atlas_filter_min);
        sampler_desc.wrap_u = sokol_wrap(create_info.atlas_wrap_x);
        sampler_desc.wrap_v = sokol_wrap(create_info.atlas_wrap_y);
        sampler_desc.wrap_w = SG_WRAP_CLAMP_TO_EDGE;
        sampler_desc.label = "marrow-atlas-sampler";
        atlas_sampler_ = sg_make_sampler(&sampler_desc);
        if (const std::optional<std::string> error =
                resource_state_error("atlas sampler", sg_query_sampler_state(atlas_sampler_))) {
            return error;
        }

        sg_image_desc white_image_desc{};
        white_image_desc.width = 1;
        white_image_desc.height = 1;
        white_image_desc.pixel_format = SG_PIXELFORMAT_RGBA8;
        white_image_desc.data.mip_levels[0].ptr = kSolidWhiteRgba.data();
        white_image_desc.data.mip_levels[0].size = kSolidWhiteRgba.size();
        white_image_desc.label = "marrow-white-image";
        white_image_ = sg_make_image(&white_image_desc);
        if (const std::optional<std::string> error =
                resource_state_error("white image", sg_query_image_state(white_image_))) {
            return error;
        }

        sg_view_desc white_view_desc{};
        white_view_desc.texture.image = white_image_;
        white_view_desc.label = "marrow-white-view";
        white_view_ = sg_make_view(&white_view_desc);
        if (const std::optional<std::string> error =
                resource_state_error("white texture view", sg_query_view_state(white_view_))) {
            return error;
        }

        sg_buffer_desc vertex_buffer_desc{};
        vertex_buffer_desc.size = kStreamingVertexBufferBytes;
        vertex_buffer_desc.usage.vertex_buffer = true;
        vertex_buffer_desc.usage.immutable = false;
        vertex_buffer_desc.usage.stream_update = true;
        vertex_buffer_desc.label = "marrow-stream-vertices";
        stream_vertex_buffer_ = sg_make_buffer(&vertex_buffer_desc);
        if (const std::optional<std::string> error =
                resource_state_error(
                    "stream vertex buffer",
                    sg_query_buffer_state(stream_vertex_buffer_))) {
            return error;
        }

        sg_buffer_desc index_buffer_desc{};
        index_buffer_desc.size = kStreamingIndexBufferBytes;
        index_buffer_desc.usage.vertex_buffer = false;
        index_buffer_desc.usage.index_buffer = true;
        index_buffer_desc.usage.immutable = false;
        index_buffer_desc.usage.stream_update = true;
        index_buffer_desc.label = "marrow-stream-indices";
        stream_index_buffer_ = sg_make_buffer(&index_buffer_desc);
        if (const std::optional<std::string> error =
                resource_state_error(
                    "stream index buffer",
                    sg_query_buffer_state(stream_index_buffer_))) {
            return error;
        }

        const sg_shader_desc* single_color_desc =
            marrow_renderer_single_color_shader_desc(sg_query_backend());
        if (single_color_desc == nullptr) {
            return "Failed to build the single-color shader description for the active backend.";
        }
        single_color_shader_ = sg_make_shader(single_color_desc);
        if (const std::optional<std::string> error =
                resource_state_error(
                    "single-color shader",
                    sg_query_shader_state(single_color_shader_))) {
            return error;
        }

        const sg_shader_desc* two_color_desc =
            marrow_renderer_two_color_shader_desc(sg_query_backend());
        if (two_color_desc == nullptr) {
            return "Failed to build the two-color shader description for the active backend.";
        }
        two_color_shader_ = sg_make_shader(two_color_desc);
        if (const std::optional<std::string> error =
                resource_state_error(
                    "two-color shader",
                    sg_query_shader_state(two_color_shader_))) {
            return error;
        }

        return std::nullopt;
    }

    std::optional<std::string> create_offscreen_target() {
        sg_image_desc color_image_desc{};
        color_image_desc.usage.color_attachment = true;
        color_image_desc.usage.immutable = false;
        color_image_desc.width = framebuffer_width_;
        color_image_desc.height = framebuffer_height_;
        color_image_desc.pixel_format = swapchain_color_format_;
        color_image_desc.sample_count = swapchain_sample_count_;
        color_image_desc.label = "marrow-offscreen-color";
        offscreen_color_image_ = sg_make_image(&color_image_desc);
        if (const std::optional<std::string> error =
                resource_state_error(
                    "offscreen color attachment",
                    sg_query_image_state(offscreen_color_image_))) {
            return error;
        }

        sg_view_desc color_view_desc{};
        color_view_desc.color_attachment.image = offscreen_color_image_;
        color_view_desc.label = "marrow-offscreen-color-view";
        offscreen_color_view_ = sg_make_view(&color_view_desc);
        if (const std::optional<std::string> error =
                resource_state_error(
                    "offscreen color attachment view",
                    sg_query_view_state(offscreen_color_view_))) {
            return error;
        }

        sg_image_desc depth_stencil_image_desc{};
        depth_stencil_image_desc.usage.depth_stencil_attachment = true;
        depth_stencil_image_desc.usage.immutable = false;
        depth_stencil_image_desc.width = framebuffer_width_;
        depth_stencil_image_desc.height = framebuffer_height_;
        depth_stencil_image_desc.pixel_format = swapchain_depth_format_;
        depth_stencil_image_desc.sample_count = swapchain_sample_count_;
        depth_stencil_image_desc.label = "marrow-offscreen-depth-stencil";
        offscreen_depth_stencil_image_ = sg_make_image(&depth_stencil_image_desc);
        if (const std::optional<std::string> error =
                resource_state_error(
                    "offscreen depth-stencil attachment",
                    sg_query_image_state(offscreen_depth_stencil_image_))) {
            return error;
        }

        sg_view_desc depth_stencil_view_desc{};
        depth_stencil_view_desc.depth_stencil_attachment.image = offscreen_depth_stencil_image_;
        depth_stencil_view_desc.label = "marrow-offscreen-depth-stencil-view";
        offscreen_depth_stencil_view_ = sg_make_view(&depth_stencil_view_desc);
        if (const std::optional<std::string> error =
                resource_state_error(
                    "offscreen depth-stencil attachment view",
                    sg_query_view_state(offscreen_depth_stencil_view_))) {
            return error;
        }

        return std::nullopt;
    }

    void apply_pass_target(sg_pass* pass) const {
        if (headless_offscreen_) {
            pass->attachments.colors[0] = offscreen_color_view_;
            pass->attachments.depth_stencil = offscreen_depth_stencil_view_;
        } else {
            pass->swapchain = sglue_swapchain();
        }
    }

    void reset_handles() {
        created_ = false;
        headless_offscreen_ = false;
        swapchain_color_format_ = SG_PIXELFORMAT_NONE;
        swapchain_depth_format_ = SG_PIXELFORMAT_NONE;
        swapchain_sample_count_ = 1;
        framebuffer_width_ = 0;
        framebuffer_height_ = 0;
        pass_action_ = {};
        stream_vertex_buffer_ = {};
        stream_index_buffer_ = {};
        offscreen_color_image_ = {};
        offscreen_color_view_ = {};
        offscreen_depth_stencil_image_ = {};
        offscreen_depth_stencil_view_ = {};
        atlas_image_ = {};
        atlas_view_ = {};
        atlas_sampler_ = {};
        white_image_ = {};
        white_view_ = {};
#if defined(__APPLE__)
        metal_device_ = nil;
#endif
        single_color_shader_ = {};
        two_color_shader_ = {};
        draw_pipelines_.clear();
        clip_pipelines_.clear();
    }

    void destroy_cached_pipelines() {
        if (!sg_isvalid()) {
            draw_pipelines_.clear();
            clip_pipelines_.clear();
            return;
        }

        for (DrawPipelineCacheEntry& entry : draw_pipelines_) {
            if (entry.pipeline.id != SG_INVALID_ID) {
                sg_destroy_pipeline(entry.pipeline);
            }
        }
        for (ClipPipelineCacheEntry& entry : clip_pipelines_) {
            if (entry.pipeline.id != SG_INVALID_ID) {
                sg_destroy_pipeline(entry.pipeline);
            }
        }
        draw_pipelines_.clear();
        clip_pipelines_.clear();
    }

    void configure_depth_stencil_format(sg_pipeline_desc* pipeline_desc) const {
        if (swapchain_depth_format_ != SG_PIXELFORMAT_NONE) {
            pipeline_desc->depth.pixel_format = swapchain_depth_format_;
        }
    }

    std::optional<std::string> append_stream_data(
        const std::vector<RenderCommandVertex>& vertices,
        const std::vector<std::uint32_t>& indices,
        int* vertex_offset_out,
        int* index_offset_out) {
        const sg_range vertex_range{
            vertices.data(),
            vertices.size() * sizeof(RenderCommandVertex),
        };
        const int vertex_offset = sg_append_buffer(stream_vertex_buffer_, &vertex_range);
        if (sg_query_buffer_overflow(stream_vertex_buffer_)) {
            return "Sokol vertex stream buffer overflowed while appending a render command.";
        }

        const sg_range index_range{
            indices.data(),
            indices.size() * sizeof(std::uint32_t),
        };
        const int index_offset = sg_append_buffer(stream_index_buffer_, &index_range);
        if (sg_query_buffer_overflow(stream_index_buffer_)) {
            return "Sokol index stream buffer overflowed while appending a render command.";
        }

        *vertex_offset_out = vertex_offset;
        *index_offset_out = index_offset;
        return std::nullopt;
    }

    sg_bindings make_stream_bindings(
        int vertex_offset,
        int index_offset,
        TextureHandle texture_handle) const {
        sg_bindings bindings{};
        bindings.vertex_buffers[0] = stream_vertex_buffer_;
        bindings.vertex_buffer_offsets[0] = vertex_offset;
        bindings.index_buffer = stream_index_buffer_;
        bindings.index_buffer_offset = index_offset;
        bindings.views[kAtlasTextureSlot] =
            texture_handle == kSolidWhiteTextureHandle ? white_view_ : atlas_view_;
        bindings.samplers[kAtlasSamplerSlot] = atlas_sampler_;
        return bindings;
    }

    std::optional<std::string> ensure_draw_pipeline(
        runtime::BlendMode blend_mode,
        ColorShaderVariant shader_variant,
        bool premultiplied_alpha,
        std::optional<std::uint8_t> stencil_reference,
        sg_pipeline* pipeline_out) {
        for (const DrawPipelineCacheEntry& entry : draw_pipelines_) {
            if (entry.blend_mode == blend_mode &&
                entry.shader_variant == shader_variant &&
                entry.premultiplied_alpha == premultiplied_alpha &&
                entry.stencil_enabled == stencil_reference.has_value() &&
                entry.stencil_reference == stencil_reference.value_or(0U)) {
                *pipeline_out = entry.pipeline;
                return std::nullopt;
            }
        }

        const BlendState blend_state = blend_state_for(blend_mode, premultiplied_alpha);
        sg_pipeline_desc pipeline_desc{};
        configure_common_pipeline_state(
            &pipeline_desc,
            shader_variant == ColorShaderVariant::SingleColor ? single_color_shader_ : two_color_shader_,
            swapchain_color_format_,
            swapchain_sample_count_,
            blend_state);
        configure_depth_stencil_format(&pipeline_desc);
        if (shader_variant == ColorShaderVariant::SingleColor) {
            configure_single_color_layout(&pipeline_desc);
            pipeline_desc.label = "marrow-single-color-pipeline";
        } else {
            configure_two_color_layout(&pipeline_desc);
            pipeline_desc.label = "marrow-two-color-pipeline";
        }
        if (stencil_reference.has_value()) {
            pipeline_desc.stencil.enabled = true;
            pipeline_desc.stencil.front.compare = SG_COMPAREFUNC_EQUAL;
            pipeline_desc.stencil.front.fail_op = SG_STENCILOP_KEEP;
            pipeline_desc.stencil.front.depth_fail_op = SG_STENCILOP_KEEP;
            pipeline_desc.stencil.front.pass_op = SG_STENCILOP_KEEP;
            pipeline_desc.stencil.back = pipeline_desc.stencil.front;
            pipeline_desc.stencil.read_mask = 0xFFU;
            pipeline_desc.stencil.write_mask = 0x00U;
            pipeline_desc.stencil.ref = *stencil_reference;
        }

        sg_pipeline pipeline = sg_make_pipeline(&pipeline_desc);
        if (const std::optional<std::string> error =
                resource_state_error("draw pipeline", sg_query_pipeline_state(pipeline))) {
            if (pipeline.id != SG_INVALID_ID) {
                sg_destroy_pipeline(pipeline);
            }
            return error;
        }

        draw_pipelines_.push_back({
            blend_mode,
            shader_variant,
            premultiplied_alpha,
            stencil_reference.has_value(),
            stencil_reference.value_or(0U),
            pipeline,
        });
        *pipeline_out = pipeline;
        return std::nullopt;
    }

    std::optional<std::string> ensure_clip_pipeline(
        std::uint8_t compare_reference,
        std::uint8_t write_mask,
        sg_pipeline* pipeline_out) {
        for (const ClipPipelineCacheEntry& entry : clip_pipelines_) {
            if (entry.compare_reference == compare_reference &&
                entry.write_mask == write_mask) {
                *pipeline_out = entry.pipeline;
                return std::nullopt;
            }
        }

        sg_pipeline_desc pipeline_desc{};
        configure_common_pipeline_state(
            &pipeline_desc,
            single_color_shader_,
            swapchain_color_format_,
            swapchain_sample_count_,
            BlendState{BlendFactor::One, BlendFactor::Zero});
        configure_depth_stencil_format(&pipeline_desc);
        configure_single_color_layout(&pipeline_desc);
        pipeline_desc.colors[0].write_mask = static_cast<sg_color_mask>(0);
        pipeline_desc.colors[0].blend.enabled = false;
        pipeline_desc.stencil.enabled = true;
        pipeline_desc.stencil.front.compare = SG_COMPAREFUNC_EQUAL;
        pipeline_desc.stencil.front.fail_op = SG_STENCILOP_KEEP;
        pipeline_desc.stencil.front.depth_fail_op = SG_STENCILOP_KEEP;
        pipeline_desc.stencil.front.pass_op = SG_STENCILOP_INVERT;
        pipeline_desc.stencil.back = pipeline_desc.stencil.front;
        pipeline_desc.stencil.read_mask = 0xFFU;
        pipeline_desc.stencil.write_mask = write_mask;
        pipeline_desc.stencil.ref = compare_reference;
        pipeline_desc.label = "marrow-clip-pipeline";

        sg_pipeline pipeline = sg_make_pipeline(&pipeline_desc);
        if (const std::optional<std::string> error =
                resource_state_error("clip pipeline", sg_query_pipeline_state(pipeline))) {
            if (pipeline.id != SG_INVALID_ID) {
                sg_destroy_pipeline(pipeline);
            }
            return error;
        }

        clip_pipelines_.push_back({compare_reference, write_mask, pipeline});
        *pipeline_out = pipeline;
        return std::nullopt;
    }

    std::optional<std::string> submit_draw_command(
        const RenderCommand& command,
        bool premultiplied_alpha,
        std::optional<std::uint8_t> stencil_reference,
        const marrow_renderer_vs_params_t& vs_params,
        const marrow_renderer_fs_params_t& fs_params) {
        if (command.texture_handle != kAtlasTextureHandle &&
            command.texture_handle != kSolidWhiteTextureHandle) {
            return "Render command referenced an unknown texture handle.";
        }
        if (command.vertices.empty() || command.indices.empty()) {
            return std::nullopt;
        }

        int vertex_offset = 0;
        int index_offset = 0;
        if (const std::optional<std::string> error = append_stream_data(
                command.vertices,
                command.indices,
                &vertex_offset,
                &index_offset)) {
            return error;
        }

        sg_pipeline pipeline{};
        if (const std::optional<std::string> error = ensure_draw_pipeline(
                command.blend_mode,
                command.shader_variant,
                premultiplied_alpha,
                stencil_reference,
                &pipeline)) {
            return error;
        }

        const sg_bindings bindings =
            make_stream_bindings(vertex_offset, index_offset, command.texture_handle);
        sg_apply_pipeline(pipeline);
        sg_apply_bindings(&bindings);
        sg_apply_uniforms(kVsUniformSlot, SG_RANGE(vs_params));
        sg_apply_uniforms(kFsUniformSlot, SG_RANGE(fs_params));
        sg_draw(0, static_cast<int>(command.indices.size()), 1);
        return std::nullopt;
    }

    std::optional<std::string> submit_clip_command(
        const RenderClipCommand& clip_command,
        const ActiveStencilClip& stencil_clip,
        bool restoring_parent_reference,
        const marrow_renderer_vs_params_t& vs_params,
        const marrow_renderer_fs_params_t& fs_params) {
        if (clip_command.vertices.empty() || clip_command.indices.empty()) {
            return std::nullopt;
        }

        int vertex_offset = 0;
        int index_offset = 0;
        if (const std::optional<std::string> error = append_stream_data(
                clip_command.vertices,
                clip_command.indices,
                &vertex_offset,
                &index_offset)) {
            return error;
        }

        sg_pipeline pipeline{};
        if (const std::optional<std::string> error = ensure_clip_pipeline(
                restoring_parent_reference
                    ? stencil_clip.reference_value
                    : stencil_clip.parent_reference_value,
                stencil_clip.invert_mask,
                &pipeline)) {
            return error;
        }

        const sg_bindings bindings =
            make_stream_bindings(vertex_offset, index_offset, kSolidWhiteTextureHandle);
        sg_apply_pipeline(pipeline);
        sg_apply_bindings(&bindings);
        sg_apply_uniforms(kVsUniformSlot, SG_RANGE(vs_params));
        sg_apply_uniforms(kFsUniformSlot, SG_RANGE(fs_params));
        sg_draw(0, static_cast<int>(clip_command.indices.size()), 1);
        return std::nullopt;
    }

    bool created_{false};
    bool headless_offscreen_{false};
    sg_pixel_format swapchain_color_format_{SG_PIXELFORMAT_NONE};
    sg_pixel_format swapchain_depth_format_{SG_PIXELFORMAT_NONE};
    int swapchain_sample_count_{1};
    int framebuffer_width_{0};
    int framebuffer_height_{0};
    sg_pass_action pass_action_{};
    sg_buffer stream_vertex_buffer_{};
    sg_buffer stream_index_buffer_{};
    sg_image offscreen_color_image_{};
    sg_view offscreen_color_view_{};
    sg_image offscreen_depth_stencil_image_{};
    sg_view offscreen_depth_stencil_view_{};
    sg_image atlas_image_{};
    sg_view atlas_view_{};
    sg_sampler atlas_sampler_{};
    sg_image white_image_{};
    sg_view white_view_{};
#if defined(__APPLE__)
    id<MTLDevice> metal_device_{nil};
#endif
    sg_shader single_color_shader_{};
    sg_shader two_color_shader_{};
    std::vector<DrawPipelineCacheEntry> draw_pipelines_{};
    std::vector<ClipPipelineCacheEntry> clip_pipelines_{};
};

struct SokolAppLoopState {
    const BackendCreateInfo* create_info{nullptr};
    Backend* backend{nullptr};
    const BackendFrameCallback* render_callback{nullptr};
    std::optional<int> auto_close_frames;
    std::optional<std::string> error_message;
    bool backend_created{false};
    int rendered_frames{0};
};

void sokol_app_init(void* user_data) {
    auto* state = static_cast<SokolAppLoopState*>(user_data);
    if (state == nullptr || state->backend == nullptr || state->create_info == nullptr) {
        return;
    }

    state->error_message = state->backend->create(*state->create_info);
    if (state->error_message.has_value()) {
        sapp_quit();
        return;
    }
    state->backend_created = true;
}

void sokol_app_frame(void* user_data) {
    auto* state = static_cast<SokolAppLoopState*>(user_data);
    if (state == nullptr || !state->backend_created || state->backend == nullptr ||
        state->render_callback == nullptr) {
        return;
    }

    BackendFrameInfo frame_info;
    state->error_message = state->backend->begin_frame(&frame_info);
    if (state->error_message.has_value()) {
        sapp_quit();
        return;
    }
    if (frame_info.should_close) {
        sapp_quit();
        return;
    }

    state->error_message = (*state->render_callback)(frame_info);
    if (state->error_message.has_value()) {
        sapp_quit();
        return;
    }

    state->backend->end_frame();
    state->rendered_frames += 1;
    if (state->auto_close_frames.has_value() &&
        state->rendered_frames >= *state->auto_close_frames) {
        sapp_quit();
    }
}

void sokol_app_cleanup(void* user_data) {
    auto* state = static_cast<SokolAppLoopState*>(user_data);
    if (state == nullptr || state->backend == nullptr) {
        return;
    }

    state->backend->destroy();
    state->backend_created = false;
}

} // namespace

std::unique_ptr<Backend> make_sokol_backend() {
    return std::make_unique<SokolBackend>();
}

std::optional<std::string> run_sokol_app(
    const BackendCreateInfo& create_info,
    Backend* backend,
    const BackendFrameCallback& render_callback,
    std::optional<int> auto_close_frames) {
    if (backend == nullptr) {
        return "Renderer backend was null.";
    }

    SokolAppLoopState state;
    state.create_info = &create_info;
    state.backend = backend;
    state.render_callback = &render_callback;
    state.auto_close_frames = auto_close_frames;

    sapp_desc app_desc{};
    app_desc.user_data = &state;
    app_desc.init_userdata_cb = sokol_app_init;
    app_desc.frame_userdata_cb = sokol_app_frame;
    app_desc.cleanup_userdata_cb = sokol_app_cleanup;
    app_desc.width = std::max(create_info.window.width, 1);
    app_desc.height = std::max(create_info.window.height, 1);
    app_desc.sample_count = 1;
    app_desc.swap_interval = create_info.hidden_window ? 0 : 1;
    app_desc.high_dpi = true;
    app_desc.window_title = create_info.window.title.c_str();
    app_desc.logger.func = slog_func;
#if !defined(__APPLE__)
    app_desc.gl.major_version = 4;
    app_desc.gl.minor_version = 1;
#endif

    sapp_run(&app_desc);
    return state.error_message;
}

} // namespace marrow::renderer::internal
