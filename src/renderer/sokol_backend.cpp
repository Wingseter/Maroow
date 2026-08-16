#include "module_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

#if defined(__APPLE__)
#import <Metal/Metal.h>
#define SOKOL_METAL
#else
#define SOKOL_GLCORE
#endif

#include "sokol_gfx.h"
#define SOKOL_NO_ENTRY
#include "sokol_app.h"
#include "sokol_glue.h"
#include "sokol_log.h"

namespace marrow::renderer::internal {
namespace {

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

class SokolBackend final : public Backend {
public:
    ~SokolBackend() override { destroy(); }

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
            return "sokol_gfx did not initialize the expected GLCORE backend on this platform.";
        }
#endif

        scene_renderer_ = make_sokol_scene_renderer();
        const SokolSceneTargetInfo target_info{
            static_cast<std::uint32_t>(color_format_),
            static_cast<std::uint32_t>(depth_format_),
            sample_count_};
        if (const std::optional<std::string> error =
                scene_renderer_->create_scene_resources(create_info, target_info)) {
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
        if (scene_renderer_ != nullptr) {
            scene_renderer_->destroy_scene_resources();
            scene_renderer_.reset();
        }
        destroy_offscreen_target();
        if (owns_graphics_device_ && sg_isvalid()) {
            sg_shutdown();
        }
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

    std::optional<std::string> submit_commands(
        const RenderCommandList& command_list) override {
        if (!created_ || scene_renderer_ == nullptr) {
            return "Sokol backend was not created.";
        }
        if (const std::optional<std::string> error =
                scene_renderer_->prepare_command_lists({&command_list})) {
            return error;
        }

        sg_pass pass{};
        pass.action = pass_action_;
        apply_pass_target(&pass);
        pass.label = "marrow-frame";
        sg_begin_pass(&pass);
        const std::optional<std::string> result =
            scene_renderer_->submit_commands_to_active_pass(command_list);
        sg_end_pass();
        return result;
    }

    void end_frame() override {
        if (created_) {
            sg_commit();
        }
    }

private:
    std::optional<std::string> setup_context(const BackendCreateInfo& create_info) {
        owns_graphics_device_ = true;
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
            color_format_ = SG_PIXELFORMAT_BGRA8;
            depth_format_ = SG_PIXELFORMAT_DEPTH_STENCIL;
            sample_count_ = 1;
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
        color_format_ = environment.defaults.color_format;
        depth_format_ = environment.defaults.depth_format;
        sample_count_ = environment.defaults.sample_count;
        return std::nullopt;
    }

    std::optional<std::string> create_offscreen_target() {
        sg_image_desc color_image_desc{};
        color_image_desc.usage.color_attachment = true;
        color_image_desc.usage.immutable = false;
        color_image_desc.width = framebuffer_width_;
        color_image_desc.height = framebuffer_height_;
        color_image_desc.pixel_format = color_format_;
        color_image_desc.sample_count = sample_count_;
        color_image_desc.label = "marrow-offscreen-color";
        offscreen_color_image_ = sg_make_image(&color_image_desc);
        if (const std::optional<std::string> error = resource_state_error(
                "offscreen color attachment",
                sg_query_image_state(offscreen_color_image_))) {
            return error;
        }

        sg_view_desc color_view_desc{};
        color_view_desc.color_attachment.image = offscreen_color_image_;
        color_view_desc.label = "marrow-offscreen-color-view";
        offscreen_color_view_ = sg_make_view(&color_view_desc);
        if (const std::optional<std::string> error = resource_state_error(
                "offscreen color attachment view",
                sg_query_view_state(offscreen_color_view_))) {
            return error;
        }

        sg_image_desc depth_stencil_image_desc{};
        depth_stencil_image_desc.usage.depth_stencil_attachment = true;
        depth_stencil_image_desc.usage.immutable = false;
        depth_stencil_image_desc.width = framebuffer_width_;
        depth_stencil_image_desc.height = framebuffer_height_;
        depth_stencil_image_desc.pixel_format = depth_format_;
        depth_stencil_image_desc.sample_count = sample_count_;
        depth_stencil_image_desc.label = "marrow-offscreen-depth-stencil";
        offscreen_depth_stencil_image_ = sg_make_image(&depth_stencil_image_desc);
        if (const std::optional<std::string> error = resource_state_error(
                "offscreen depth-stencil attachment",
                sg_query_image_state(offscreen_depth_stencil_image_))) {
            return error;
        }

        sg_view_desc depth_stencil_view_desc{};
        depth_stencil_view_desc.depth_stencil_attachment.image =
            offscreen_depth_stencil_image_;
        depth_stencil_view_desc.label = "marrow-offscreen-depth-stencil-view";
        offscreen_depth_stencil_view_ = sg_make_view(&depth_stencil_view_desc);
        if (const std::optional<std::string> error = resource_state_error(
                "offscreen depth-stencil attachment view",
                sg_query_view_state(offscreen_depth_stencil_view_))) {
            return error;
        }
        return std::nullopt;
    }

    void destroy_offscreen_target() {
        if (!sg_isvalid()) {
            return;
        }
        if (offscreen_depth_stencil_view_.id != SG_INVALID_ID) {
            sg_destroy_view(offscreen_depth_stencil_view_);
        }
        if (offscreen_depth_stencil_image_.id != SG_INVALID_ID) {
            sg_destroy_image(offscreen_depth_stencil_image_);
        }
        if (offscreen_color_view_.id != SG_INVALID_ID) {
            sg_destroy_view(offscreen_color_view_);
        }
        if (offscreen_color_image_.id != SG_INVALID_ID) {
            sg_destroy_image(offscreen_color_image_);
        }
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
        owns_graphics_device_ = false;
        headless_offscreen_ = false;
        color_format_ = SG_PIXELFORMAT_NONE;
        depth_format_ = SG_PIXELFORMAT_NONE;
        sample_count_ = 1;
        framebuffer_width_ = 0;
        framebuffer_height_ = 0;
        pass_action_ = {};
        offscreen_color_image_ = {};
        offscreen_color_view_ = {};
        offscreen_depth_stencil_image_ = {};
        offscreen_depth_stencil_view_ = {};
#if defined(__APPLE__)
        metal_device_ = nil;
#endif
    }

    bool created_{false};
    bool owns_graphics_device_{false};
    bool headless_offscreen_{false};
    sg_pixel_format color_format_{SG_PIXELFORMAT_NONE};
    sg_pixel_format depth_format_{SG_PIXELFORMAT_NONE};
    int sample_count_{1};
    int framebuffer_width_{0};
    int framebuffer_height_{0};
    sg_pass_action pass_action_{};
    sg_image offscreen_color_image_{};
    sg_view offscreen_color_view_{};
    sg_image offscreen_depth_stencil_image_{};
    sg_view offscreen_depth_stencil_view_{};
#if defined(__APPLE__)
    id<MTLDevice> metal_device_{nil};
#endif
    std::unique_ptr<SokolSceneRenderer> scene_renderer_;
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
