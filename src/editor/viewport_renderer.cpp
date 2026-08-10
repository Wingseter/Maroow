#include "viewport_renderer.hpp"

#include <algorithm>
#include <utility>

#include "../renderer/module_internal.hpp"

namespace marrow::editor {

ViewportRenderer::ViewportRenderer() = default;

ViewportRenderer::~ViewportRenderer() {
    destroy();
}

std::optional<std::string> ViewportRenderer::initialize(
    std::uint32_t color_format,
    std::uint32_t depth_format,
    int sample_count) {
    destroy();
    color_format_ = color_format;
    depth_format_ = depth_format;
    sample_count_ = std::max(sample_count, 1);
    scene_renderer_ = renderer::internal::make_sokol_scene_renderer();
    if (!scene_renderer_) {
        error_message_ = "Failed to allocate the Sokol viewport scene renderer.";
        return error_message_;
    }
    available_ = true;
    error_message_.clear();
    return std::nullopt;
}

void ViewportRenderer::destroy() {
    if (scene_renderer_) {
        scene_renderer_->destroy_scene_resources();
        scene_renderer_.reset();
    }
    atlas_texture_path_.clear();
    atlas_texture_write_time_.reset();
    atlas_filter_min_.clear();
    atlas_filter_mag_.clear();
    atlas_wrap_x_.clear();
    atlas_wrap_y_.clear();
    atlas_premultiplied_alpha_ = false;
    color_format_ = 0U;
    depth_format_ = 0U;
    sample_count_ = 1;
    available_ = false;
}

std::optional<std::string> ViewportRenderer::prepare(
    const renderer::PreparedScene& scene,
    const std::filesystem::path& atlas_image_path,
    const std::array<float, 16>& projection,
    const std::array<float, 4>& tint_color,
    Submission* submission_out) {
    if (!available_ || !scene_renderer_ || submission_out == nullptr) {
        return error_message_.empty()
            ? std::optional<std::string>("Viewport Sokol scene renderer is unavailable.")
            : std::optional<std::string>(error_message_);
    }
    if (const auto error = ensure_atlas_resources(scene, atlas_image_path)) {
        error_message_ = *error;
        return error;
    }

    renderer::RenderCommandListResult command_list_result =
        renderer::build_render_command_list(scene, projection);
    if (!command_list_result) {
        error_message_ = command_list_result.error_message;
        return command_list_result.error_message;
    }
    submission_out->command_list = std::move(*command_list_result.command_list);
    for (renderer::RenderCommand& command : submission_out->command_list.commands) {
        for (renderer::RenderCommandVertex& vertex : command.vertices) {
            vertex.light_color[0] *= tint_color[0];
            vertex.light_color[1] *= tint_color[1];
            vertex.light_color[2] *= tint_color[2];
            vertex.light_color[3] *= tint_color[3];
        }
    }
    error_message_.clear();
    return std::nullopt;
}

std::optional<std::string> ViewportRenderer::preflight(
    const std::vector<const Submission*>& submissions) {
    if (!available_ || !scene_renderer_) {
        return "Viewport Sokol scene renderer is unavailable.";
    }
    std::vector<const renderer::RenderCommandList*> command_lists;
    command_lists.reserve(submissions.size());
    for (const Submission* submission : submissions) {
        if (submission == nullptr) {
            return "Viewport scene preflight received a null submission.";
        }
        command_lists.push_back(&submission->command_list);
    }
    return scene_renderer_->prepare_command_lists(command_lists);
}

std::optional<std::string> ViewportRenderer::submit(const Submission& submission) {
    if (!available_ || !scene_renderer_) {
        return "Viewport Sokol scene renderer is unavailable.";
    }
    return scene_renderer_->submit_commands_to_active_pass(submission.command_list);
}

bool ViewportRenderer::available() const {
    return available_;
}

const std::string& ViewportRenderer::error_message() const {
    return error_message_;
}

std::optional<std::string> ViewportRenderer::ensure_atlas_resources(
    const renderer::PreparedScene& scene,
    const std::filesystem::path& atlas_image_path) {
    std::error_code error;
    std::optional<std::filesystem::file_time_type> write_time;
    if (!atlas_image_path.empty() && std::filesystem::exists(atlas_image_path, error) && !error) {
        write_time = std::filesystem::last_write_time(atlas_image_path, error);
        if (error) {
            write_time.reset();
        }
    }

    if (atlas_texture_path_ == atlas_image_path &&
        atlas_texture_write_time_ == write_time &&
        atlas_filter_min_ == scene.atlas_filter_min &&
        atlas_filter_mag_ == scene.atlas_filter_mag &&
        atlas_wrap_x_ == scene.atlas_wrap_x &&
        atlas_wrap_y_ == scene.atlas_wrap_y &&
        atlas_premultiplied_alpha_ == scene.premultiplied_alpha) {
        return std::nullopt;
    }

    const renderer::TextureImageLoadResult texture_result =
        renderer::load_png_texture_or_white(atlas_image_path);
    if (texture_result.image.width <= 0 || texture_result.image.height <= 0 ||
        texture_result.image.rgba8.empty()) {
        return "Viewport Sokol atlas image data was invalid.";
    }

    renderer::BackendCreateInfo create_info;
    create_info.atlas_texture = texture_result.image;
    create_info.atlas_filter_min = scene.atlas_filter_min;
    create_info.atlas_filter_mag = scene.atlas_filter_mag;
    create_info.atlas_wrap_x = scene.atlas_wrap_x;
    create_info.atlas_wrap_y = scene.atlas_wrap_y;

    renderer::internal::SokolSceneTargetInfo target_info;
    target_info.color_format = color_format_;
    target_info.depth_format = depth_format_;
    target_info.sample_count = sample_count_;
    if (const auto create_error =
            scene_renderer_->create_scene_resources(create_info, target_info)) {
        return create_error;
    }

    atlas_texture_path_ = atlas_image_path;
    atlas_texture_write_time_ = write_time;
    atlas_filter_min_ = scene.atlas_filter_min;
    atlas_filter_mag_ = scene.atlas_filter_mag;
    atlas_wrap_x_ = scene.atlas_wrap_x;
    atlas_wrap_y_ = scene.atlas_wrap_y;
    atlas_premultiplied_alpha_ = scene.premultiplied_alpha;
    return std::nullopt;
}

} // namespace marrow::editor
