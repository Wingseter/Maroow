#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#else
#include <GL/glcorearb.h>
#endif

#include "imgui.h"
#include "imgui_internal.h"

#include "shell_types.hpp"
#include "viewport_renderer.hpp"
#include "marrow/allocator.hpp"
#include "marrow/editor/module.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/renderer/module.hpp"
#include "marrow/runtime/animation_state.hpp"
#include "marrow/runtime/profiler.hpp"

namespace marrow::editor::shell {

bool read_text_file(
    const std::filesystem::path& path,
    std::string* text_out,
    std::string* error_out) {
    std::ifstream stream(path);
    if (!stream) {
        if (error_out != nullptr) {
            *error_out = "Failed to open " + path.string();
        }
        return false;
    }

    std::ostringstream buffer;
    buffer << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        if (error_out != nullptr) {
            *error_out = "Failed to read " + path.string();
        }
        return false;
    }

    if (text_out != nullptr) {
        *text_out = buffer.str();
    }
    return true;
}

bool write_text_file(
    const std::filesystem::path& path,
    const std::string& text,
    std::string* error_out) {
    std::ofstream stream(path, std::ios::trunc);
    if (!stream) {
        if (error_out != nullptr) {
            *error_out = "Failed to open " + path.string() + " for writing";
        }
        return false;
    }

    stream << text;
    if (!stream.good()) {
        if (error_out != nullptr) {
            *error_out = "Failed to write " + path.string();
        }
        return false;
    }

    std::error_code error;
    std::filesystem::last_write_time(
        path,
        std::filesystem::file_time_type::clock::now() + std::chrono::seconds(1),
        error);
    return true;
}

bool rewrite_attack_peak_for_hot_reload(
    const std::filesystem::path& skeleton_path,
    std::string* error_out) {
    std::string text;
    if (!read_text_file(skeleton_path, &text, error_out)) {
        return false;
    }

    const std::size_t attack_section = text.find("\"attack\"");
    if (attack_section == std::string::npos) {
        if (error_out != nullptr) {
            *error_out = "Hot-reload smoke could not find the attack animation in " +
                skeleton_path.string();
        }
        return false;
    }

    const std::string before = "\"angle\": 60.0";
    const std::size_t angle_offset = text.find(before, attack_section);
    if (angle_offset == std::string::npos) {
        if (error_out != nullptr) {
            *error_out = "Hot-reload smoke could not find the attack peak key in " +
                skeleton_path.string();
        }
        return false;
    }

    text.replace(angle_offset, before.size(), "\"angle\": 90.0");
    return write_text_file(skeleton_path, text, error_out);
}

bool validate_runtime_asset_hot_reload_smoke(const ShellState& source_state) {
    if (!source_state.load_result || source_state.load_result.project == nullptr) {
        std::cerr << "Hot-reload smoke requires a loaded project.\n";
        return false;
    }

    const std::filesystem::path temp_root =
        std::filesystem::temp_directory_path() / "marrow_editor_hot_reload_smoke";
    std::error_code filesystem_error;
    std::filesystem::remove_all(temp_root, filesystem_error);
    filesystem_error.clear();
    std::filesystem::create_directories(temp_root, filesystem_error);
    if (filesystem_error) {
        std::cerr << "Hot-reload smoke could not create " << temp_root.string() << ".\n";
        return false;
    }

    const std::filesystem::path source_skeleton =
        source_state.load_result.project->resolved_skeleton_path();
    const std::vector<std::filesystem::path> source_atlases =
        source_state.load_result.project->resolved_atlas_paths();
    if (source_atlases.empty()) {
        std::cerr << "Hot-reload smoke requires at least one atlas.\n";
        return false;
    }

    const std::filesystem::path temp_skeleton = temp_root / source_skeleton.filename();
    const std::filesystem::path temp_atlas = temp_root / source_atlases.front().filename();
    std::filesystem::copy_file(
        source_skeleton,
        temp_skeleton,
        std::filesystem::copy_options::overwrite_existing,
        filesystem_error);
    if (filesystem_error) {
        std::cerr << "Hot-reload smoke could not copy " << source_skeleton.string() << ".\n";
        return false;
    }
    filesystem_error.clear();
    std::filesystem::copy_file(
        source_atlases.front(),
        temp_atlas,
        std::filesystem::copy_options::overwrite_existing,
        filesystem_error);
    if (filesystem_error) {
        std::cerr << "Hot-reload smoke could not copy " << source_atlases.front().string() << ".\n";
        return false;
    }

    const std::filesystem::path temp_project = temp_root / "hot_reload_smoke.marrow";
    marrow::editor::MinimalProjectOptions project_options;
    project_options.project_path = temp_project;
    project_options.skeleton_path = temp_skeleton;
    project_options.atlas_paths = {temp_atlas};
    project_options.name = "Hot Reload Smoke";
    project_options.active_animation = "attack";
    project_options.preview_skins = {"default"};
    project_options.notes = "Generated by marrow_editor_shell hot-reload smoke validation.";
    const marrow::editor::ProjectData temp_project_data =
        marrow::editor::create_minimal_project(project_options);
    const auto save_result = marrow::editor::save_project(temp_project_data, temp_project);
    if (!save_result) {
        std::cerr << save_result.error->format() << '\n';
        return false;
    }

    ShellState hot_reload_state;
    hot_reload_state.project_path = temp_project;
    if (!reload_project(&hot_reload_state)) {
        std::cerr << hot_reload_state.error_message << '\n';
        return false;
    }

    const auto arm_index = hot_reload_state.load_result.skeleton_data->find_bone_index("arm_l");
    if (!arm_index.has_value()) {
        std::cerr << "Hot-reload smoke requires the arm_l bone.\n";
        return false;
    }

    hot_reload_state.animation_state->clear_tracks();
    hot_reload_state.animation_state->set_animation(0, "idle", true, 0.0);
    hot_reload_state.animation_state->update(0.5);
    hot_reload_state.selected_animation_name = "idle";
    hot_reload_state.timeline_time_seconds = 0.5;
    if (!apply_current_animation_state_to_preview(&hot_reload_state)) {
        std::cerr << hot_reload_state.error_message << '\n';
        return false;
    }
    hot_reload_state.animation_state->set_animation(0, "attack", false, 0.2);
    hot_reload_state.animation_state->update(0.1);
    hot_reload_state.selected_animation_name = "attack";
    hot_reload_state.timeline_time_seconds = 0.1;
    hot_reload_state.timeline_playing = true;

    if (!apply_current_animation_state_to_preview(&hot_reload_state)) {
        std::cerr << hot_reload_state.error_message << '\n';
        return false;
    }

    std::shared_ptr<marrow::runtime::TrackEntry> current =
        hot_reload_state.animation_state->get_current(0);
    if (current == nullptr || current->mixing_from == nullptr ||
        current->animation_name != "attack" ||
        current->mixing_from->animation_name != "idle") {
        std::cerr << "Hot-reload smoke did not build the expected attack<-idle mix chain.\n";
        return false;
    }

    const double pre_reload_track_time = current->track_time;
    const double pre_reload_mix_time = current->mix_time;
    const double pre_reload_rotation =
        static_cast<double>(
            hot_reload_state.preview_skeleton->bone_poses()[*arm_index].local_pose.rotation);
    if (std::abs(pre_reload_rotation - 15.0) > 1e-3) {
        std::cerr << "Hot-reload smoke expected the pre-reload mixed attack pose at 15 degrees.\n";
        return false;
    }

    std::string rewrite_error;
    if (!rewrite_attack_peak_for_hot_reload(temp_skeleton, &rewrite_error)) {
        std::cerr << rewrite_error << '\n';
        return false;
    }

    const RuntimeAssetPollOutcome poll_outcome =
        poll_runtime_asset_changes(&hot_reload_state);
    if (poll_outcome != RuntimeAssetPollOutcome::Reloaded) {
        std::cerr << "Hot-reload smoke did not detect the modified skeleton file.\n";
        if (!hot_reload_state.error_message.empty()) {
            std::cerr << hot_reload_state.error_message << '\n';
        }
        return false;
    }

    current = hot_reload_state.animation_state->get_current(0);
    if (current == nullptr || current->mixing_from == nullptr ||
        current->animation_name != "attack" ||
        current->mixing_from->animation_name != "idle") {
        std::cerr << "Hot-reload smoke lost the active mix chain after reload.\n";
        return false;
    }
    if (std::abs(current->track_time - pre_reload_track_time) > 1e-6 ||
        std::abs(current->mix_time - pre_reload_mix_time) > 1e-6) {
        std::cerr << "Hot-reload smoke did not preserve track and mix time across reload.\n";
        return false;
    }

    const double post_reload_rotation =
        static_cast<double>(
            hot_reload_state.preview_skeleton->bone_poses()[*arm_index].local_pose.rotation);
    if (std::abs(post_reload_rotation - 22.5) > 1e-3) {
        std::cerr << "Hot-reload smoke did not sample the updated attack pose after reload.\n";
        return false;
    }

    hot_reload_state.animation_state->update(1.0 / 60.0);
    hot_reload_state.timeline_time_seconds =
        hot_reload_state.animation_state->get_current(0)->track_time;
    if (!apply_current_animation_state_to_preview(&hot_reload_state)) {
        std::cerr << hot_reload_state.error_message << '\n';
        return false;
    }
    if (hot_reload_state.animation_state->get_current(0)->track_time <= pre_reload_track_time) {
        std::cerr << "Hot-reload smoke playback did not continue after reload.\n";
        return false;
    }

    return true;
}

const marrow::renderer::RegionAttachmentDrawCommand* find_region_attachment(
    const marrow::renderer::PreparedScene& scene,
    std::string_view slot_name) {
    for (const auto& command : scene.draw_commands) {
        const auto* attachment = marrow::renderer::region_attachment_command(command);
        if (attachment != nullptr && attachment->slot_name == slot_name) {
            return attachment;
        }
    }
    return nullptr;
}

std::optional<std::size_t> find_region_attachment_index(
    const marrow::renderer::PreparedScene& scene,
    std::string_view slot_name) {
    for (std::size_t draw_index = 0; draw_index < scene.draw_commands.size(); ++draw_index) {
        const auto* attachment =
            marrow::renderer::region_attachment_command(scene.draw_commands[draw_index]);
        if (attachment != nullptr && attachment->slot_name == slot_name) {
            return draw_index;
        }
    }
    return std::nullopt;
}

const marrow::renderer::DynamicMeshDrawCommand* find_dynamic_mesh_attachment(
    const marrow::renderer::PreparedScene& scene,
    std::string_view slot_name) {
    for (const auto& command : scene.draw_commands) {
        const auto* attachment = marrow::renderer::dynamic_mesh_attachment_command(command);
        if (attachment != nullptr && attachment->slot_name == slot_name) {
            return attachment;
        }
    }
    return nullptr;
}

const marrow::renderer::ClipAttachmentDrawCommand* find_clip_attachment(
    const marrow::renderer::PreparedScene& scene,
    std::string_view attachment_name) {
    const auto iterator = std::find_if(
        scene.clip_attachments.begin(),
        scene.clip_attachments.end(),
        [&](const marrow::renderer::ClipAttachmentDrawCommand& attachment) {
            return attachment.attachment_name == attachment_name;
        });
    return iterator != scene.clip_attachments.end() ? &(*iterator) : nullptr;
}

std::array<std::uint8_t, 4> read_viewport_pixel(
    const ViewportRenderResources& resources,
    int x,
    int y) {
    std::array<std::uint8_t, 4> pixel{};
    const int safe_x = std::clamp(x, 0, resources.framebuffer_width - 1);
    const int safe_y = std::clamp(y, 0, resources.framebuffer_height - 1);
    glBindFramebuffer(GL_FRAMEBUFFER, resources.framebuffer);
    glReadPixels(safe_x, safe_y, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pixel.data());
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return pixel;
}

bool pixels_match(
    const std::array<std::uint8_t, 4>& left,
    const std::array<std::uint8_t, 4>& right,
    int tolerance = 6) {
    for (std::size_t index = 0; index < left.size(); ++index) {
        if (std::abs(static_cast<int>(left[index]) - static_cast<int>(right[index])) > tolerance) {
            return false;
        }
    }
    return true;
}

bool point_in_polygon(
    const std::vector<marrow::renderer::RenderPoint>& polygon,
    const marrow::renderer::RenderPoint& point) {
    if (polygon.size() < 3U) {
        return false;
    }

    bool inside = false;
    for (std::size_t index = 0, previous = polygon.size() - 1U;
         index < polygon.size();
         previous = index++) {
        const auto& current = polygon[index];
        const auto& prior = polygon[previous];
        const bool intersects =
            ((current.y > point.y) != (prior.y > point.y)) &&
            (point.x <
             ((prior.x - current.x) * (point.y - current.y) / (prior.y - current.y)) +
                 current.x);
        if (intersects) {
            inside = !inside;
        }
    }
    return inside;
}

#if defined(__APPLE__)
struct ScopedViewportSmokeContext {
    CGLPixelFormatObj pixel_format{nullptr};
    CGLContextObj context{nullptr};

    void destroy() {
        if (context != nullptr) {
            if (CGLGetCurrentContext() == context) {
                CGLSetCurrentContext(nullptr);
            }
            CGLReleaseContext(context);
            context = nullptr;
        }
        if (pixel_format != nullptr) {
            CGLReleasePixelFormat(pixel_format);
            pixel_format = nullptr;
        }
    }

    ~ScopedViewportSmokeContext() {
        destroy();
    }
};

std::optional<std::string> initialize_viewport_smoke_context(
    ScopedViewportSmokeContext* smoke_context) {
    const auto make_attributes =
        [](bool accelerated, bool allow_offline) -> std::vector<CGLPixelFormatAttribute> {
        std::vector<CGLPixelFormatAttribute> attributes;
        attributes.reserve(13);
        attributes.push_back(kCGLPFAOpenGLProfile);
        attributes.push_back(static_cast<CGLPixelFormatAttribute>(kCGLOGLPVersion_3_2_Core));
        if (accelerated) {
            attributes.push_back(kCGLPFAAccelerated);
        }
        attributes.push_back(kCGLPFAColorSize);
        attributes.push_back(static_cast<CGLPixelFormatAttribute>(24));
        attributes.push_back(kCGLPFAAlphaSize);
        attributes.push_back(static_cast<CGLPixelFormatAttribute>(8));
        attributes.push_back(kCGLPFADepthSize);
        attributes.push_back(static_cast<CGLPixelFormatAttribute>(24));
        attributes.push_back(kCGLPFAStencilSize);
        attributes.push_back(static_cast<CGLPixelFormatAttribute>(8));
        if (allow_offline) {
            attributes.push_back(kCGLPFAAllowOfflineRenderers);
        }
        attributes.push_back(static_cast<CGLPixelFormatAttribute>(0));
        return attributes;
    };

    const std::array<std::pair<bool, bool>, 4> pixel_format_attempts{{
        {true, true},
        {false, true},
        {true, false},
        {false, false},
    }};

    GLint pixel_format_count = 0;
    CGLError pixel_format_error = kCGLBadAttribute;
    for (const auto [accelerated, allow_offline] : pixel_format_attempts) {
        const std::vector<CGLPixelFormatAttribute> attributes =
            make_attributes(accelerated, allow_offline);
        smoke_context->pixel_format = nullptr;
        pixel_format_count = 0;
        pixel_format_error = CGLChoosePixelFormat(
            attributes.data(),
            &smoke_context->pixel_format,
            &pixel_format_count);
        if (pixel_format_error == kCGLNoError &&
            smoke_context->pixel_format != nullptr &&
            pixel_format_count > 0) {
            break;
        }
    }

    if (pixel_format_error != kCGLNoError ||
        smoke_context->pixel_format == nullptr ||
        pixel_format_count <= 0) {
        return "Viewport renderer smoke failed to create an offscreen macOS GL pixel format.";
    }

    const CGLError context_error =
        CGLCreateContext(smoke_context->pixel_format, nullptr, &smoke_context->context);
    if (context_error != kCGLNoError || smoke_context->context == nullptr) {
        return "Viewport renderer smoke failed to create an offscreen macOS GL context.";
    }

    const CGLError current_error = CGLSetCurrentContext(smoke_context->context);
    if (current_error != kCGLNoError) {
        return "Viewport renderer smoke failed to make the offscreen macOS GL context current.";
    }

    return std::nullopt;
}
#endif

bool validate_viewport_prepared_scene_renderer_smoke(
    const std::filesystem::path& project_path) {
#if defined(__APPLE__)
    ScopedViewportSmokeContext smoke_context;
    if (const auto error = initialize_viewport_smoke_context(&smoke_context)) {
        std::cerr << *error << '\n';
        return false;
    }
#else
    glfwSetErrorCallback(glfw_error_callback);
    glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);
    glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
    if (!glfwInit()) {
        std::cerr << "Viewport renderer smoke failed to initialize GLFW.\n";
        return false;
    }

    configure_glfw_for_editor();
    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    GLFWwindow* window =
        glfwCreateWindow(640, 480, "Marrow Viewport Renderer Smoke", nullptr, nullptr);
    if (window == nullptr) {
        std::cerr << "Viewport renderer smoke failed to create a hidden GL window.\n";
        glfwTerminate();
        return false;
    }
    glfwMakeContextCurrent(window);
    glfwSwapInterval(0);
#endif

    ViewportRenderResources resources;
    auto cleanup = [&]() {
        destroy_viewport_renderer(&resources);
#if defined(__APPLE__)
        smoke_context.destroy();
#else
        glfwDestroyWindow(window);
        glfwTerminate();
#endif
    };

    ShellState shell_state;
    shell_state.project_path = project_path;
    if (!reload_project(&shell_state)) {
        std::cerr << shell_state.error_message << '\n';
        cleanup();
        return false;
    }
    shell_state.viewport.pan_x = 0.0;
    shell_state.viewport.pan_y = 0.0;
    shell_state.viewport.zoom = 1.0;
    shell_state.viewport.debug_overlay = {};

    if (const auto error = initialize_viewport_renderer(&resources)) {
        std::cerr << *error << '\n';
        cleanup();
        return false;
    }
    if (const auto error = ensure_viewport_framebuffer(&resources, 640, 480)) {
        std::cerr << *error << '\n';
        cleanup();
        return false;
    }

    const auto render_scene_to_pixels =
        [&](const ShellState& render_state,
            const ViewportLayout& layout,
            const marrow::renderer::PreparedScene& scene,
            const ViewportGeometryPass& overlay_geometry,
            const ImVec2& sample_point) {
            const ViewportGeometryPass empty_background;
            const std::vector<OnionSkinTexturedGhost> no_ghosts;
            if (const auto error = render_prepared_scene_framebuffer(
                    layout,
                    empty_background,
                    overlay_geometry,
                    no_ghosts,
                    scene,
                    resolve_viewport_atlas_image_path(render_state, scene),
                    &resources)) {
                std::cerr << *error << '\n';
                return std::optional<std::array<std::uint8_t, 4>>{};
            }

            return std::optional<std::array<std::uint8_t, 4>>{read_viewport_pixel(
                resources,
                static_cast<int>(std::lround(sample_point.x)),
                resources.framebuffer_height - 1 -
                    static_cast<int>(std::lround(sample_point.y)))};
        };

    const auto setup_layout = build_viewport_layout(
        shell_state,
        ImVec2(0.0f, 0.0f),
        ImVec2(static_cast<float>(resources.framebuffer_width),
               static_cast<float>(resources.framebuffer_height)));
    if (!setup_layout.has_value()) {
        std::cerr << "Viewport renderer smoke could not build the setup-pose layout.\n";
        cleanup();
        return false;
    }

    const auto setup_scene_result = marrow::renderer::prepare_setup_pose_scene(
        *shell_state.preview_skeleton,
        *shell_state.load_result.atlas_data.front());
    if (!setup_scene_result) {
        std::cerr << setup_scene_result.error_message << '\n';
        cleanup();
        return false;
    }

    const marrow::renderer::PreparedScene& setup_scene = *setup_scene_result.scene;
    const auto body_draw_index = find_region_attachment_index(setup_scene, "body");
    const auto* body_attachment = find_region_attachment(setup_scene, "body");
    const auto* spark_attachment = find_region_attachment(setup_scene, "spark_fx");
    const auto* clip_attachment = find_clip_attachment(setup_scene, "fx_mask");
    if (!body_draw_index.has_value() ||
        body_attachment == nullptr ||
        spark_attachment == nullptr ||
        clip_attachment == nullptr) {
        std::cerr << "Viewport renderer smoke could not resolve the expected setup-pose attachments.\n";
        cleanup();
        return false;
    }

    const ImVec2 body_center = screen_from_world(
        *setup_layout,
        (body_attachment->vertices[0].position.x + body_attachment->vertices[2].position.x) * 0.5,
        (body_attachment->vertices[0].position.y + body_attachment->vertices[2].position.y) * 0.5);
    double body_min_x = body_attachment->vertices[0].position.x;
    double body_max_x = body_attachment->vertices[0].position.x;
    double body_min_y = body_attachment->vertices[0].position.y;
    double body_max_y = body_attachment->vertices[0].position.y;
    for (const auto& vertex : body_attachment->vertices) {
        body_min_x = std::min(body_min_x, vertex.position.x);
        body_max_x = std::max(body_max_x, vertex.position.x);
        body_min_y = std::min(body_min_y, vertex.position.y);
        body_max_y = std::max(body_max_y, vertex.position.y);
    }
    std::vector<ImVec2> body_sample_points;
    body_sample_points.reserve(25);
    for (int row = 1; row <= 5; ++row) {
        const double row_ratio = static_cast<double>(row) / 6.0;
        const double y = body_min_y + ((body_max_y - body_min_y) * row_ratio);
        for (int column = 1; column <= 5; ++column) {
            const double column_ratio = static_cast<double>(column) / 6.0;
            const double x = body_min_x + ((body_max_x - body_min_x) * column_ratio);
            body_sample_points.push_back(screen_from_world(*setup_layout, x, y));
        }
    }
    marrow::renderer::PreparedScene blank_scene = setup_scene;
    blank_scene.clip_attachments.clear();
    blank_scene.draw_commands.clear();
    blank_scene.ordered_events.clear();
    const auto background_pixel =
        render_scene_to_pixels(shell_state, *setup_layout, blank_scene, {}, body_center);
    const auto setup_body_pixel =
        render_scene_to_pixels(shell_state, *setup_layout, setup_scene, {}, body_center);
    if (!background_pixel.has_value() || !setup_body_pixel.has_value()) {
        cleanup();
        return false;
    }
    if (pixels_match(*setup_body_pixel, *background_pixel)) {
        std::cerr << "Viewport renderer smoke did not draw the setup-pose body region.\n";
        cleanup();
        return false;
    }

    marrow::renderer::PreparedScene tinted_scene = setup_scene;
    if (auto* body_command = std::get_if<marrow::renderer::RegionAttachmentDrawCommand>(
            &tinted_scene.draw_commands[*body_draw_index])) {
        body_command->blend_mode = marrow::runtime::BlendMode::Normal;
    } else {
        std::cerr << "Viewport renderer smoke lost the setup-pose body command.\n";
        cleanup();
        return false;
    }
    marrow::renderer::PreparedScene no_dark_scene = tinted_scene;
    if (auto* body_command = std::get_if<marrow::renderer::RegionAttachmentDrawCommand>(
            &no_dark_scene.draw_commands[*body_draw_index])) {
        body_command->dark_color.reset();
    } else {
        std::cerr << "Viewport renderer smoke could not retarget the body two-color test.\n";
        cleanup();
        return false;
    }
    bool two_color_changed = false;
    for (const ImVec2& sample_point : body_sample_points) {
        const auto tinted_pixel =
            render_scene_to_pixels(shell_state, *setup_layout, tinted_scene, {}, sample_point);
        const auto no_dark_pixel =
            render_scene_to_pixels(shell_state, *setup_layout, no_dark_scene, {}, sample_point);
        if (tinted_pixel.has_value() &&
            no_dark_pixel.has_value() &&
            !pixels_match(*tinted_pixel, *no_dark_pixel)) {
            two_color_changed = true;
            break;
        }
    }
    if (!two_color_changed) {
        std::cerr << "Viewport renderer smoke did not apply the two-color tint path.\n";
        cleanup();
        return false;
    }

    auto render_body_blend_mode =
        [&](marrow::runtime::BlendMode blend_mode,
            const ImVec2& sample_point) -> std::optional<std::array<std::uint8_t, 4>> {
            marrow::renderer::PreparedScene blend_scene = setup_scene;
            auto* body_command = std::get_if<marrow::renderer::RegionAttachmentDrawCommand>(
                &blend_scene.draw_commands[*body_draw_index]);
            if (body_command == nullptr) {
                return std::nullopt;
            }
            body_command->dark_color.reset();
            body_command->blend_mode = blend_mode;
            return render_scene_to_pixels(shell_state, *setup_layout, blend_scene, {}, sample_point);
        };
    bool blend_modes_diverged = false;
    for (const ImVec2& sample_point : body_sample_points) {
        const auto normal_pixel =
            render_body_blend_mode(marrow::runtime::BlendMode::Normal, sample_point);
        const auto additive_pixel =
            render_body_blend_mode(marrow::runtime::BlendMode::Additive, sample_point);
        const auto multiply_pixel =
            render_body_blend_mode(marrow::runtime::BlendMode::Multiply, sample_point);
        const auto screen_pixel =
            render_body_blend_mode(marrow::runtime::BlendMode::Screen, sample_point);
        if (normal_pixel.has_value() &&
            additive_pixel.has_value() &&
            multiply_pixel.has_value() &&
            screen_pixel.has_value() &&
            !pixels_match(*normal_pixel, *additive_pixel) &&
            !pixels_match(*normal_pixel, *multiply_pixel) &&
            !pixels_match(*normal_pixel, *screen_pixel)) {
            blend_modes_diverged = true;
            break;
        }
    }
    if (!blend_modes_diverged) {
        std::cerr << "Viewport renderer smoke did not vary the framebuffer result across blend modes.\n";
        cleanup();
        return false;
    }

    double spark_min_x = spark_attachment->vertices[0].position.x;
    double spark_max_x = spark_attachment->vertices[0].position.x;
    double spark_min_y = spark_attachment->vertices[0].position.y;
    double spark_max_y = spark_attachment->vertices[0].position.y;
    for (const auto& vertex : spark_attachment->vertices) {
        spark_min_x = std::min(spark_min_x, vertex.position.x);
        spark_max_x = std::max(spark_max_x, vertex.position.x);
        spark_min_y = std::min(spark_min_y, vertex.position.y);
        spark_max_y = std::max(spark_max_y, vertex.position.y);
    }
    std::vector<ImVec2> clipped_spark_sample_points;
    clipped_spark_sample_points.reserve(16);
    for (int row = 1; row <= 4; ++row) {
        const double row_ratio = static_cast<double>(row) / 5.0;
        const double y = spark_min_y + ((spark_max_y - spark_min_y) * row_ratio);
        for (int column = 1; column <= 4; ++column) {
            const double column_ratio = static_cast<double>(column) / 5.0;
            const double x = spark_min_x + ((spark_max_x - spark_min_x) * column_ratio);
            if (point_in_polygon(clip_attachment->polygon, {x, y})) {
                continue;
            }
            clipped_spark_sample_points.push_back(screen_from_world(*setup_layout, x, y));
        }
    }
    if (clipped_spark_sample_points.empty()) {
        std::cerr << "Viewport renderer smoke could not find a spark sample outside the clip polygon.\n";
        cleanup();
        return false;
    }

    marrow::renderer::PreparedScene unclipped_scene = setup_scene;
    unclipped_scene.clip_attachments.clear();
    unclipped_scene.ordered_events.clear();
    for (std::size_t draw_index = 0; draw_index < unclipped_scene.draw_commands.size(); ++draw_index) {
        std::visit(
            [](auto& attachment) {
                attachment.clip_attachment_name.reset();
                attachment.masked_vertices.clear();
                attachment.masked_indices.clear();
            },
            unclipped_scene.draw_commands[draw_index]);
        unclipped_scene.ordered_events.push_back({
            marrow::renderer::PreparedSceneEventKind::Draw,
            draw_index,
        });
    }
    bool clipping_applied = false;
    for (const ImVec2& sample_point : clipped_spark_sample_points) {
        const auto clipped_spark_pixel =
            render_scene_to_pixels(shell_state, *setup_layout, setup_scene, {}, sample_point);
        const auto unclipped_spark_pixel =
            render_scene_to_pixels(shell_state, *setup_layout, unclipped_scene, {}, sample_point);
        if (clipped_spark_pixel.has_value() &&
            unclipped_spark_pixel.has_value() &&
            !pixels_match(*clipped_spark_pixel, *unclipped_spark_pixel) &&
            !pixels_match(*unclipped_spark_pixel, *background_pixel)) {
            clipping_applied = true;
            break;
        }
    }
    if (!clipping_applied) {
        std::cerr << "Viewport renderer smoke did not apply stencil clipping to the spark attachment.\n";
        cleanup();
        return false;
    }

    const auto spine_index = shell_state.load_result.skeleton_data->find_bone_index("spine");
    if (!spine_index.has_value()) {
        std::cerr << "Viewport renderer smoke requires the spine bone.\n";
        cleanup();
        return false;
    }
    const ImVec2 spine_screen = setup_layout->bones[*spine_index].screen_position;
    const auto setup_spine_pixel =
        render_scene_to_pixels(shell_state, *setup_layout, setup_scene, {}, spine_screen);
    if (!setup_spine_pixel.has_value()) {
        cleanup();
        return false;
    }

    shell_state.selected_bone_index = *spine_index;
    shell_state.viewport.debug_overlay.bones = true;
    ViewportGeometryPass overlay_geometry;
    build_viewport_overlay_geometry(
        shell_state,
        *setup_layout,
        shell_state.selected_bone_index,
        nullptr,
        &overlay_geometry);
    bool overlay_changed = false;
    for (int offset_y = -3; offset_y <= 3 && !overlay_changed; ++offset_y) {
        for (int offset_x = -3; offset_x <= 3; ++offset_x) {
            const ImVec2 overlay_sample_point(
                spine_screen.x + static_cast<float>(offset_x),
                spine_screen.y + static_cast<float>(offset_y));
            const auto base_pixel =
                render_scene_to_pixels(shell_state, *setup_layout, setup_scene, {}, overlay_sample_point);
            const auto overlay_pixel = render_scene_to_pixels(
                shell_state,
                *setup_layout,
                setup_scene,
                overlay_geometry,
                overlay_sample_point);
            if (base_pixel.has_value() &&
                overlay_pixel.has_value() &&
                !pixels_match(*base_pixel, *overlay_pixel)) {
                overlay_changed = true;
                break;
            }
        }
    }
    if (!overlay_changed) {
        std::cerr << "Viewport renderer smoke did not draw the debug bone overlay on top of the character.\n";
        cleanup();
        return false;
    }

    ShellState camera_state = {};
    camera_state.project_path = project_path;
    if (!reload_project(&camera_state)) {
        std::cerr << camera_state.error_message << '\n';
        cleanup();
        return false;
    }
    camera_state.selected_bone_index = *spine_index;
    camera_state.viewport.debug_overlay.bones = true;
    camera_state.viewport.pan_x = 96.0;
    camera_state.viewport.pan_y = -54.0;
    camera_state.viewport.zoom = 1.35;
    const auto panned_layout = build_viewport_layout(
        camera_state,
        ImVec2(0.0f, 0.0f),
        ImVec2(static_cast<float>(resources.framebuffer_width),
               static_cast<float>(resources.framebuffer_height)));
    if (!panned_layout.has_value()) {
        std::cerr << "Viewport renderer smoke could not build the panned layout.\n";
        cleanup();
        return false;
    }
    ViewportGeometryPass panned_overlay_geometry;
    build_viewport_overlay_geometry(
        camera_state,
        *panned_layout,
        camera_state.selected_bone_index,
        nullptr,
        &panned_overlay_geometry);
    const ImVec2 panned_body_center = screen_from_world(
        *panned_layout,
        (body_attachment->vertices[0].position.x + body_attachment->vertices[2].position.x) * 0.5,
        (body_attachment->vertices[0].position.y + body_attachment->vertices[2].position.y) * 0.5);
    const ImVec2 panned_spine_screen = panned_layout->bones[*spine_index].screen_position;
    const auto panned_body_pixel = render_scene_to_pixels(
        camera_state,
        *panned_layout,
        setup_scene,
        panned_overlay_geometry,
        panned_body_center);
    const auto panned_spine_pixel = render_scene_to_pixels(
        camera_state,
        *panned_layout,
        setup_scene,
        panned_overlay_geometry,
        panned_spine_screen);
    if (!panned_body_pixel.has_value() ||
        !panned_spine_pixel.has_value() ||
        pixels_match(*panned_body_pixel, *background_pixel) ||
        pixels_match(*panned_spine_pixel, *background_pixel) ||
        std::abs(panned_body_center.x - body_center.x) < 20.0f ||
        std::abs(panned_spine_screen.x - spine_screen.x) < 20.0f) {
        std::cerr << "Viewport renderer smoke did not apply the shared pan/zoom camera to the character and overlay.\n";
        cleanup();
        return false;
    }

    ShellState mesh_state;
    mesh_state.project_path = project_path;
    if (!reload_project(&mesh_state)) {
        std::cerr << mesh_state.error_message << '\n';
        cleanup();
        return false;
    }
    mesh_state.viewport.pan_x = 0.0;
    mesh_state.viewport.pan_y = 0.0;
    mesh_state.viewport.zoom = 1.0;
    mesh_state.preview_skin_names = {"default"};
    mesh_state.preview_slot_overrides.assign(
        mesh_state.preview_slot_overrides.size(),
        std::nullopt);
    if (!refresh_preview_pose(&mesh_state)) {
        std::cerr << mesh_state.error_message << '\n';
        cleanup();
        return false;
    }
    const auto body_slot_index = mesh_state.load_result.skeleton_data->find_slot_index("body");
    const auto warrior_skin_index = mesh_state.load_result.skeleton_data->find_skin_index("warrior");
    if (!body_slot_index.has_value() ||
        !warrior_skin_index.has_value() ||
        !set_preview_skin_enabled(&mesh_state, *warrior_skin_index, true, false)) {
        std::cerr << "Viewport renderer smoke could not activate the warrior mesh skin.\n";
        cleanup();
        return false;
    }
    if (!set_selected_animation(&mesh_state, "idle", "Smoke", false, true) ||
        !scrub_timeline_time(&mesh_state, 0.5, "Smoke", false)) {
        std::cerr << "Viewport renderer smoke could not scrub the animated warrior preview pose.\n";
        cleanup();
        return false;
    }
    const auto* warrior_attachment =
        mesh_state.preview_skeleton->current_attachment(*body_slot_index);
    if (warrior_attachment == nullptr || warrior_attachment->name != "warrior_body") {
        std::cerr << "Viewport renderer smoke did not apply the warrior body attachment.\n";
        cleanup();
        return false;
    }
    const auto mesh_layout = build_viewport_layout(
        mesh_state,
        ImVec2(0.0f, 0.0f),
        ImVec2(static_cast<float>(resources.framebuffer_width),
               static_cast<float>(resources.framebuffer_height)));
    if (!mesh_layout.has_value()) {
        std::cerr << "Viewport renderer smoke could not build the mesh layout.\n";
        cleanup();
        return false;
    }
    const auto mesh_scene_result = marrow::renderer::prepare_setup_pose_scene(
        *mesh_state.preview_skeleton,
        *mesh_state.load_result.atlas_data.front());
    if (!mesh_scene_result) {
        std::cerr << mesh_scene_result.error_message << '\n';
        cleanup();
        return false;
    }
    const auto* mesh_attachment = find_dynamic_mesh_attachment(*mesh_scene_result.scene, "body");
    if (mesh_attachment == nullptr) {
        std::cerr << "Viewport renderer smoke did not resolve the warrior body mesh.\n";
        cleanup();
        return false;
    }
    const auto mesh_vertices = marrow::renderer::evaluate_gpu_skinned_vertices(
        *mesh_attachment,
        mesh_scene_result.scene->bone_palette);
    if (!mesh_vertices || mesh_vertices.vertices.empty()) {
        std::cerr << "Viewport renderer smoke could not evaluate the warrior body mesh.\n";
        cleanup();
        return false;
    }
    double mesh_center_x = 0.0;
    double mesh_center_y = 0.0;
    for (const auto& vertex : mesh_vertices.vertices) {
        mesh_center_x += vertex.position.x;
        mesh_center_y += vertex.position.y;
    }
    mesh_center_x /= static_cast<double>(mesh_vertices.vertices.size());
    mesh_center_y /= static_cast<double>(mesh_vertices.vertices.size());
    const ImVec2 mesh_center = screen_from_world(*mesh_layout, mesh_center_x, mesh_center_y);
    const auto mesh_pixel = render_scene_to_pixels(
        mesh_state,
        *mesh_layout,
        *mesh_scene_result.scene,
        {},
        mesh_center);
    if (!mesh_pixel.has_value() || pixels_match(*mesh_pixel, *background_pixel)) {
        std::cerr << "Viewport renderer smoke did not draw the warrior GPU-skinned mesh attachment.\n";
        cleanup();
        return false;
    }

    cleanup();
    return true;
}

int run_headless_smoke(const Options& options) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DisplaySize = ImVec2(1440.0f, 900.0f);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;

    apply_editor_theme();
    unsigned char* font_pixels = nullptr;
    int font_width = 0;
    int font_height = 0;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);

    ShellState shell_state;
    shell_state.project_path = options.project_path;
    if (!reload_project(&shell_state)) {
        std::cerr << shell_state.error_message;
        ImGui::DestroyContext();
        return 1;
    }

    if (!validate_viewport_prepared_scene_renderer_smoke(options.project_path)) {
        ImGui::DestroyContext();
        return 1;
    }

    if (!validate_runtime_asset_hot_reload_smoke(shell_state)) {
        ImGui::DestroyContext();
        return 1;
    }

    const auto spine_index = shell_state.load_result.skeleton_data->find_bone_index("spine");
    if (!spine_index.has_value()) {
        std::cerr << "Timeline smoke validation requires the spine bone.\n";
        ImGui::DestroyContext();
        return 1;
    }

    if (!set_selected_animation(&shell_state, "attack", "Smoke", false, true)) {
        std::cerr << "Animation selection smoke validation failed for attack.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (shell_state.selected_animation_name != "attack") {
        std::cerr << "Animation selection did not update the shell timeline state.\n";
        ImGui::DestroyContext();
        return 1;
    }

    if (const auto arm_index = shell_state.load_result.skeleton_data->find_bone_index("arm_l")) {
        if (!scrub_timeline_time(&shell_state, 0.2, "Smoke", false)) {
            std::cerr << "Timeline scrub smoke validation failed for attack at t=0.2.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const double arm_rotation =
            static_cast<double>(
                shell_state.preview_skeleton->bone_poses()[*arm_index].local_pose.rotation);
        if (std::abs(arm_rotation - 60.0) > 1e-3) {
            std::cerr << "Timeline scrub did not update the preview arm rotation at t=0.2.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto smoke_layout = build_viewport_layout(
            shell_state,
            ImVec2(0.0f, 0.0f),
            ImVec2(1280.0f, 720.0f));
        if (!smoke_layout.has_value()) {
            std::cerr << "Failed to build a viewport layout for headless smoke validation.\n";
            ImGui::DestroyContext();
            return 1;
        }

        if (kViewportImageUv0.x != 0.0f || kViewportImageUv0.y != 1.0f ||
            kViewportImageUv1.x != 1.0f || kViewportImageUv1.y != 0.0f) {
            std::cerr << "Viewport image UVs are not flipped for the OpenGL framebuffer texture.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const ViewportFramebufferSize initial_framebuffer_size =
            viewport_framebuffer_size(ImVec2(320.0f, 180.0f), ImVec2(2.0f, 2.0f));
        const ViewportFramebufferSize resized_framebuffer_size =
            viewport_framebuffer_size(ImVec2(640.0f, 360.0f), ImVec2(2.0f, 2.0f));
        if (initial_framebuffer_size.width != 640 ||
            initial_framebuffer_size.height != 360 ||
            resized_framebuffer_size.width != 1280 ||
            resized_framebuffer_size.height != 720) {
            std::cerr << "Viewport framebuffer sizing did not scale with the panel extent.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const ImVec2 spine_position = smoke_layout->bones[*spine_index].screen_position;
        const ImVec2 arm_position = smoke_layout->bones[*arm_index].screen_position;
        const ImVec2 bone_vector(
            arm_position.x - spine_position.x,
            arm_position.y - spine_position.y);
        const float bone_length =
            std::sqrt((bone_vector.x * bone_vector.x) + (bone_vector.y * bone_vector.y));
        if (bone_length <= 1e-6f) {
            std::cerr << "Viewport smoke validation could not build a valid spine->arm segment.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const ImVec2 bone_direction(bone_vector.x / bone_length, bone_vector.y / bone_length);
        const ImVec2 bone_perpendicular(-bone_direction.y, bone_direction.x);

        const ImVec2 joint_priority_probe(
            spine_position.x + (bone_direction.x * (kBoneJointHitRadiusPixels - 1.5f)),
            spine_position.y + (bone_direction.y * (kBoneJointHitRadiusPixels - 1.5f)));
        const auto joint_priority_pick =
            pick_bone_at_position(*smoke_layout, joint_priority_probe);
        if (!joint_priority_pick.has_value() || *joint_priority_pick != *spine_index) {
            std::cerr << "Viewport picking did not prioritize the 6px joint hit zone over the bone body.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const ImVec2 segment_midpoint(
            (spine_position.x + arm_position.x) * 0.5f,
            (spine_position.y + arm_position.y) * 0.5f);
        const ImVec2 segment_body_probe(
            segment_midpoint.x + (bone_perpendicular.x * (kBoneBodyHitThresholdPixels - 1.0f)),
            segment_midpoint.y + (bone_perpendicular.y * (kBoneBodyHitThresholdPixels - 1.0f)));
        const auto segment_body_pick =
            pick_bone_at_position(*smoke_layout, segment_body_probe);
        if (!segment_body_pick.has_value() || *segment_body_pick != *arm_index) {
            std::cerr << "Viewport picking did not select the nearest bone within the 8px body threshold.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const ImVec2 segment_miss_probe(
            segment_midpoint.x + (bone_perpendicular.x * (kBoneBodyHitThresholdPixels + 1.0f)),
            segment_midpoint.y + (bone_perpendicular.y * (kBoneBodyHitThresholdPixels + 1.0f)));
        if (pick_bone_at_position(*smoke_layout, segment_miss_probe).has_value()) {
            std::cerr << "Viewport picking accepted a point outside the 8px bone body threshold.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto picked_bone = pick_bone_at_position(
            *smoke_layout,
            smoke_layout->bones[*arm_index].screen_position);
        if (!picked_bone.has_value() || *picked_bone != *arm_index) {
            std::cerr << "Viewport selection smoke validation failed for bone arm_l.\n";
            ImGui::DestroyContext();
            return 1;
        }

        select_bone(&shell_state, *picked_bone, "Viewport", false);
        if (!shell_state.selected_bone_index.has_value() ||
            *shell_state.selected_bone_index != *arm_index) {
            std::cerr << "Viewport selection did not update the inspector selection state.\n";
            ImGui::DestroyContext();
            return 1;
        }

        if (const auto spine_index = shell_state.load_result.skeleton_data->find_bone_index("spine")) {
            select_bone(&shell_state, *spine_index, "Hierarchy", false);
            if (!shell_state.selected_bone_index.has_value() ||
                *shell_state.selected_bone_index != *spine_index) {
                std::cerr << "Hierarchy selection did not update the inspector selection state.\n";
                ImGui::DestroyContext();
                return 1;
            }
        }
    }

    if (!set_selected_animation(&shell_state, "idle", "Smoke", false, true)) {
        std::cerr << "Animation selection smoke validation failed for idle.\n";
        ImGui::DestroyContext();
        return 1;
    }
    const marrow::runtime::AnimationData* idle_animation = selected_animation(shell_state);
    if (idle_animation == nullptr) {
        std::cerr << "Timeline smoke validation could not resolve the idle animation.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const std::vector<TimelineTrackRow> idle_tracks =
        build_timeline_tracks(*shell_state.load_result.skeleton_data, *idle_animation);
    if (idle_tracks.empty()) {
        std::cerr << "Timeline panel did not expose keyed tracks for the idle animation.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const auto find_onion_ghost =
        [](const std::vector<OnionSkinGhostPose>& ghosts,
           double time_seconds,
           bool before_current) -> const OnionSkinGhostPose* {
            const auto iterator = std::find_if(
                ghosts.begin(),
                ghosts.end(),
                [&](const OnionSkinGhostPose& ghost) {
                    return ghost.before_current == before_current &&
                        std::abs(ghost.time_seconds - time_seconds) <= 1e-6;
                });
            return iterator != ghosts.end() ? &(*iterator) : nullptr;
        };

    shell_state.viewport.onion_skin.enabled = true;
    shell_state.viewport.onion_skin.mode = marrow::editor::OnionSkinMode::Frame;
    shell_state.viewport.onion_skin.anchor_to_zero = false;
    shell_state.viewport.onion_skin.before_count = 2;
    shell_state.viewport.onion_skin.after_count = 2;
    shell_state.viewport.onion_skin.step = 15;
    if (!scrub_timeline_time(&shell_state, 0.5, "Smoke", false)) {
        std::cerr << "Onion-skin smoke could not scrub the idle clip to 0.5s.\n";
        ImGui::DestroyContext();
        return 1;
    }
    const auto onion_layout = build_viewport_layout(
        shell_state,
        ImVec2(0.0f, 0.0f),
        ImVec2(1280.0f, 720.0f));
    if (!onion_layout.has_value()) {
        std::cerr << "Onion-skin smoke could not build a viewport layout.\n";
        ImGui::DestroyContext();
        return 1;
    }
    const std::vector<OnionSkinGhostPose> frame_ghosts =
        build_onion_skin_ghost_poses(shell_state, *onion_layout);
    const OnionSkinGhostPose* frame_before_near = find_onion_ghost(frame_ghosts, 0.25, true);
    const OnionSkinGhostPose* frame_before_far = find_onion_ghost(frame_ghosts, 0.0, true);
    const OnionSkinGhostPose* frame_after_near = find_onion_ghost(frame_ghosts, 0.75, false);
    const OnionSkinGhostPose* frame_after_far = find_onion_ghost(frame_ghosts, 1.0, false);
    if (frame_ghosts.size() != 4U ||
        frame_before_near == nullptr ||
        frame_before_far == nullptr ||
        frame_after_near == nullptr ||
        frame_after_far == nullptr) {
        std::cerr << "Frame onion-skin smoke did not generate the expected 2+2 ghost samples.\n";
        ImGui::DestroyContext();
        return 1;
    }
    const ImVec4 before_near_color = ImGui::ColorConvertU32ToFloat4(frame_before_near->line_color);
    const ImVec4 before_far_color = ImGui::ColorConvertU32ToFloat4(frame_before_far->line_color);
    const ImVec4 after_near_color = ImGui::ColorConvertU32ToFloat4(frame_after_near->line_color);
    const ImVec4 after_far_color = ImGui::ColorConvertU32ToFloat4(frame_after_far->line_color);
    if (!(before_near_color.z > before_near_color.x) ||
        !(after_near_color.x > after_near_color.z) ||
        !(before_near_color.w > before_far_color.w) ||
        !(after_near_color.w > after_far_color.w)) {
        std::cerr << "Frame onion-skin smoke did not apply the expected blue/red tint and alpha falloff.\n";
        ImGui::DestroyContext();
        return 1;
    }

    std::vector<ViewportRenderVertex> baseline_line_vertices;
    std::vector<ViewportRenderVertex> baseline_triangle_vertices;
    build_viewport_render_geometry(
        shell_state,
        *onion_layout,
        {},
        std::nullopt,
        nullptr,
        &baseline_line_vertices,
        &baseline_triangle_vertices);
    std::vector<ViewportRenderVertex> onion_line_vertices;
    std::vector<ViewportRenderVertex> onion_triangle_vertices;
    build_viewport_render_geometry(
        shell_state,
        *onion_layout,
        frame_ghosts,
        std::nullopt,
        nullptr,
        &onion_line_vertices,
        &onion_triangle_vertices);
    if (onion_line_vertices.size() <= baseline_line_vertices.size() ||
        onion_triangle_vertices.size() <= baseline_triangle_vertices.size()) {
        std::cerr << "Viewport onion-skin smoke did not add ghost geometry to the render pass.\n";
        ImGui::DestroyContext();
        return 1;
    }

    shell_state.viewport.onion_skin.anchor_to_zero = true;
    shell_state.viewport.onion_skin.before_count = 1;
    shell_state.viewport.onion_skin.after_count = 1;
    if (!scrub_timeline_time(&shell_state, 0.55, "Smoke", false)) {
        std::cerr << "Onion-skin anchor smoke could not scrub the idle clip to 0.55s.\n";
        ImGui::DestroyContext();
        return 1;
    }
    const std::vector<OnionSkinGhostPose> anchored_ghosts =
        build_onion_skin_ghost_poses(shell_state, *onion_layout);
    if (anchored_ghosts.size() != 2U ||
        find_onion_ghost(anchored_ghosts, 0.5, true) == nullptr ||
        find_onion_ghost(anchored_ghosts, 0.75, false) == nullptr) {
        std::cerr << "Anchor onion-skin smoke did not snap samples to frame-0 intervals.\n";
        ImGui::DestroyContext();
        return 1;
    }

    shell_state.viewport.onion_skin.mode = marrow::editor::OnionSkinMode::Keyframe;
    shell_state.viewport.onion_skin.anchor_to_zero = false;
    shell_state.viewport.onion_skin.before_count = 2;
    shell_state.viewport.onion_skin.after_count = 2;
    shell_state.viewport.onion_skin.step = 1;
    if (!scrub_timeline_time(&shell_state, 0.6, "Smoke", false)) {
        std::cerr << "Onion-skin keyframe smoke could not scrub the idle clip to 0.6s.\n";
        ImGui::DestroyContext();
        return 1;
    }
    const std::vector<OnionSkinGhostPose> keyframe_ghosts =
        build_onion_skin_ghost_poses(shell_state, *onion_layout);
    if (keyframe_ghosts.size() != 4U ||
        find_onion_ghost(keyframe_ghosts, 0.5, true) == nullptr ||
        find_onion_ghost(keyframe_ghosts, 0.25, true) == nullptr ||
        find_onion_ghost(keyframe_ghosts, 0.8, false) == nullptr ||
        find_onion_ghost(keyframe_ghosts, 1.0, false) == nullptr) {
        std::cerr << "Keyframe onion-skin smoke did not sample the expected authored key positions.\n";
        ImGui::DestroyContext();
        return 1;
    }

    shell_state.viewport.onion_skin = {};
    if (!scrub_timeline_time(&shell_state, 0.5, "Smoke", false)) {
        std::cerr << "Onion-skin smoke could not restore the idle playhead.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const auto require_debug_overlay_stats =
        [](const DebugOverlayStats& stats,
           std::size_t ik_constraints,
           std::size_t path_constraints,
           std::size_t physics_constraints,
           std::size_t mesh_attachments,
           std::size_t bounding_boxes,
           std::string_view label) {
            if (stats.ik_constraint_count != ik_constraints ||
                stats.path_constraint_count != path_constraints ||
                stats.physics_constraint_count != physics_constraints ||
                stats.mesh_attachment_count != mesh_attachments ||
                stats.bounding_box_count != bounding_boxes) {
                std::cerr << label
                          << " expected counts IK=" << ik_constraints
                          << ", Path=" << path_constraints
                          << ", Physics=" << physics_constraints
                          << ", Meshes=" << mesh_attachments
                          << ", Bounds=" << bounding_boxes
                          << " but observed IK=" << stats.ik_constraint_count
                          << ", Path=" << stats.path_constraint_count
                          << ", Physics=" << stats.physics_constraint_count
                          << ", Meshes=" << stats.mesh_attachment_count
                          << ", Bounds=" << stats.bounding_box_count << ".\n";
                return false;
            }
            return true;
        };

    if (!shell_state.viewport.debug_overlay.bones ||
        !shell_state.viewport.debug_overlay.ik_constraints ||
        !shell_state.viewport.debug_overlay.path_constraints ||
        !shell_state.viewport.debug_overlay.physics_constraints ||
        !shell_state.viewport.debug_overlay.mesh_wireframes ||
        !shell_state.viewport.debug_overlay.bounding_boxes) {
        std::cerr << "Debug overlay smoke expected the fixture viewport toggles to load as enabled.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const auto debug_warrior_skin_index =
        shell_state.load_result.skeleton_data->find_skin_index("warrior");
    if (!debug_warrior_skin_index.has_value() ||
        !set_preview_skin_enabled(&shell_state, *debug_warrior_skin_index, true, false)) {
        std::cerr << "Debug overlay smoke could not enable the warrior linked-mesh preview.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const marrow::editor::DebugOverlaySettings baseline_debug_overlay =
        shell_state.viewport.debug_overlay;
    const DebugOverlayGeometry baseline_debug_geometry =
        build_debug_overlay_geometry(shell_state, *onion_layout);
    if (!require_debug_overlay_stats(
            baseline_debug_geometry.stats,
            1U,
            1U,
            1U,
            1U,
            1U,
            "Debug overlay smoke")) {
        ImGui::DestroyContext();
        return 1;
    }

    std::vector<ViewportRenderVertex> debug_on_line_vertices;
    std::vector<ViewportRenderVertex> debug_on_triangle_vertices;
    build_viewport_render_geometry(
        shell_state,
        *onion_layout,
        {},
        std::nullopt,
        nullptr,
        &debug_on_line_vertices,
        &debug_on_triangle_vertices);

    shell_state.viewport.debug_overlay.bones = false;
    const DebugOverlayGeometry bones_hidden_geometry =
        build_debug_overlay_geometry(shell_state, *onion_layout);
    if (!require_debug_overlay_stats(
            bones_hidden_geometry.stats,
            1U,
            1U,
            1U,
            1U,
            1U,
            "Debug overlay bones-off smoke")) {
        ImGui::DestroyContext();
        return 1;
    }
    std::vector<ViewportRenderVertex> debug_without_bones_line_vertices;
    std::vector<ViewportRenderVertex> debug_without_bones_triangle_vertices;
    build_viewport_render_geometry(
        shell_state,
        *onion_layout,
        {},
        std::nullopt,
        nullptr,
        &debug_without_bones_line_vertices,
        &debug_without_bones_triangle_vertices);
    if (debug_without_bones_line_vertices.size() >= debug_on_line_vertices.size() ||
        debug_without_bones_triangle_vertices.size() >= debug_on_triangle_vertices.size()) {
        std::cerr << "Debug overlay smoke expected hiding bones to remove viewport geometry.\n";
        ImGui::DestroyContext();
        return 1;
    }
    shell_state.viewport.debug_overlay = baseline_debug_overlay;

    shell_state.viewport.debug_overlay.ik_constraints = false;
    if (!require_debug_overlay_stats(
            build_debug_overlay_geometry(shell_state, *onion_layout).stats,
            0U,
            1U,
            1U,
            1U,
            1U,
            "Debug overlay IK toggle smoke")) {
        ImGui::DestroyContext();
        return 1;
    }
    shell_state.viewport.debug_overlay = baseline_debug_overlay;

    shell_state.viewport.debug_overlay.path_constraints = false;
    if (!require_debug_overlay_stats(
            build_debug_overlay_geometry(shell_state, *onion_layout).stats,
            1U,
            0U,
            1U,
            1U,
            1U,
            "Debug overlay path toggle smoke")) {
        ImGui::DestroyContext();
        return 1;
    }
    shell_state.viewport.debug_overlay = baseline_debug_overlay;

    shell_state.viewport.debug_overlay.physics_constraints = false;
    if (!require_debug_overlay_stats(
            build_debug_overlay_geometry(shell_state, *onion_layout).stats,
            1U,
            1U,
            0U,
            1U,
            1U,
            "Debug overlay physics toggle smoke")) {
        ImGui::DestroyContext();
        return 1;
    }
    shell_state.viewport.debug_overlay = baseline_debug_overlay;

    shell_state.viewport.debug_overlay.mesh_wireframes = false;
    if (!require_debug_overlay_stats(
            build_debug_overlay_geometry(shell_state, *onion_layout).stats,
            1U,
            1U,
            1U,
            0U,
            1U,
            "Debug overlay mesh toggle smoke")) {
        ImGui::DestroyContext();
        return 1;
    }
    shell_state.viewport.debug_overlay = baseline_debug_overlay;

    shell_state.viewport.debug_overlay.bounding_boxes = false;
    if (!require_debug_overlay_stats(
            build_debug_overlay_geometry(shell_state, *onion_layout).stats,
            1U,
            1U,
            1U,
            1U,
            0U,
            "Debug overlay bounds toggle smoke")) {
        ImGui::DestroyContext();
        return 1;
    }
    shell_state.viewport.debug_overlay = baseline_debug_overlay;

    shell_state.hud_overlay_enabled = true;
    shell_state.hud_overlay_frame = build_preview_profiler_frame(shell_state);
    if (!shell_state.hud_overlay_frame.has_value()) {
        std::cerr << "Performance HUD smoke could not build a preview profiler frame.\n";
        ImGui::DestroyContext();
        return 1;
    }
    const marrow::runtime::ProfilerFrame hud_frame = *shell_state.hud_overlay_frame;
    if (hud_frame.skeleton_count != 1U ||
        hud_frame.draw_calls == 0U ||
        hud_frame.vertices == 0U ||
        hud_frame.total_us == 0U ||
        hud_frame.render_us == 0U) {
        std::cerr << "Performance HUD smoke did not report the expected skeleton, draw, vertex, and frame counters.\n";
        ImGui::DestroyContext();
        return 1;
    }
    const std::vector<std::string> hud_lines =
        marrow::runtime::profiler_hud_lines(hud_frame);
    if (hud_lines.size() != 3U ||
        hud_lines[0].find("SKELS 1") == std::string::npos ||
        hud_lines[0].find("DRAWS ") == std::string::npos ||
        hud_lines[1].find("FRAME ") == std::string::npos ||
        hud_lines[2].find("BREAKS T") == std::string::npos) {
        std::cerr << "Performance HUD smoke did not emit the expected overlay text.\n";
        ImGui::DestroyContext();
        return 1;
    }
    shell_state.hud_overlay_enabled = false;
    shell_state.hud_overlay_frame.reset();

    const auto draw_order_track = std::find_if(
        idle_tracks.begin(),
        idle_tracks.end(),
        [&](const TimelineTrackRow& track) { return track.id == "global:draw-order"; });
    if (draw_order_track == idle_tracks.end()) {
        std::cerr << "Timeline smoke validation could not find the global draw-order track.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const auto event_track = std::find_if(
        idle_tracks.begin(),
        idle_tracks.end(),
        [&](const TimelineTrackRow& track) { return track.id == "global:events"; });
    if (event_track == idle_tracks.end()) {
        std::cerr << "Timeline smoke validation could not find the global event track.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const std::string expected_spine_track_id = "bone:" + std::to_string(*spine_index) + ":Rotate";
    const auto spine_track = std::find_if(
        idle_tracks.begin(),
        idle_tracks.end(),
        [&](const TimelineTrackRow& track) { return track.id == expected_spine_track_id; });
    if (spine_track == idle_tracks.end()) {
        std::cerr << "Timeline smoke validation could not find the spine rotate track.\n";
        ImGui::DestroyContext();
        return 1;
    }

    if (!focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
        std::cerr << "Timeline track focus did not scrub the idle clip.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (shell_state.selected_bone_index != spine_index) {
        std::cerr << "Timeline track focus did not synchronize bone selection.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (shell_state.load_result.project->transform_timeline_edits.empty()) {
        std::cerr << "Project fixture did not load any transform timeline edits.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (shell_state.load_result.project->mesh_deform_timeline_edits.empty()) {
        std::cerr << "Project fixture did not load any mesh deform timeline edits.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (shell_state.load_result.project->draw_order_timeline_edits.empty()) {
        std::cerr << "Project fixture did not load any draw-order timeline edits.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (shell_state.load_result.project->event_timeline_edits.empty()) {
        std::cerr << "Project fixture did not load any event timeline edits.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (std::abs(
            shell_state.preview_skeleton->bone_poses()[*spine_index].local_pose.rotation - 8.0f) >
        1e-3f) {
        std::cerr << "Timeline track focus did not apply the project-authored spine rotation.\n";
        ImGui::DestroyContext();
        return 1;
    }

    if (const auto body_slot_index = shell_state.load_result.skeleton_data->find_slot_index("body")) {
        const auto spark_fx_slot_index =
            shell_state.load_result.skeleton_data->find_slot_index("spark_fx");
        if (!spark_fx_slot_index.has_value() ||
            !focus_timeline_track(&shell_state, *draw_order_track, 0.5, "Smoke", false)) {
            std::cerr << "Timeline draw-order focus did not scrub the idle clip.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const auto body_position =
            draw_order_position(*shell_state.preview_skeleton, *body_slot_index);
        const auto spark_fx_position =
            draw_order_position(*shell_state.preview_skeleton, *spark_fx_slot_index);
        if (!body_position.has_value() || !spark_fx_position.has_value() ||
            *body_position != 0U || *spark_fx_position != 2U) {
            std::cerr << "Timeline draw-order focus did not apply the project-authored slot order.\n";
            ImGui::DestroyContext();
            return 1;
        }
    }

    if (!focus_timeline_track(&shell_state, *event_track, 0.25, "Smoke", false) ||
        shell_state.preview_events.size() != 2U ||
        shell_state.preview_events[0].name != "footstep" ||
        shell_state.preview_events[0].int_value != 7 ||
        shell_state.preview_events[0].string_value != "editor_left" ||
        shell_state.preview_events[1].name != "dust_vfx" ||
        std::abs(shell_state.preview_events[1].float_value - 0.9) > 1e-3 ||
        shell_state.preview_events[1].string_value != "editor_dust") {
        std::cerr << "Timeline event focus did not expose the authored preview events.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (!focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
        std::cerr << "Timeline smoke could not restore the spine rotate focus.\n";
        ImGui::DestroyContext();
        return 1;
    }

    {
        marrow::editor::ProjectData previous_project = *shell_state.load_result.project;
        const auto edit_index = ensure_transform_timeline_edit_index(&shell_state, *spine_track);
        if (!edit_index.has_value()) {
            std::cerr << "Timeline editor smoke could not materialize the spine rotate edit.\n";
            ImGui::DestroyContext();
            return 1;
        }

        auto& rotate_edit =
            shell_state.load_result.project->transform_timeline_edits[*edit_index];
        if (rotate_edit.keyframes.size() != 3) {
            std::cerr << "Timeline editor smoke expected the fixture rotate edit to start with 3 keys.\n";
            ImGui::DestroyContext();
            return 1;
        }

        rotate_edit.keyframes[1].angle = 9.0;
        rotate_edit.keyframes[1].interpolation =
            marrow::runtime::Interpolation::linear();
        marrow::editor::TransformKeyframeEdit new_key;
        new_key.time = 0.75;
        new_key.angle = 12.0;
        new_key.interpolation = marrow::runtime::Interpolation::stepped();
        rotate_edit.keyframes.insert(rotate_edit.keyframes.begin() + 2, std::move(new_key));

        if (!rebuild_project_runtime(&shell_state)) {
            std::cerr << shell_state.error_message;
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.project_dirty = true;

        if (!focus_timeline_track(&shell_state, *spine_track, 0.625, "Smoke", false) ||
            std::abs(
                shell_state.preview_skeleton->bone_poses()[*spine_index].local_pose.rotation -
                10.5f) > 1e-3f) {
            std::cerr << "Timeline editor smoke did not apply edited linear interpolation.\n";
            ImGui::DestroyContext();
            return 1;
        }
        if (!scrub_timeline_time(&shell_state, 0.875, "Smoke", false) ||
            std::abs(
                shell_state.preview_skeleton->bone_poses()[*spine_index].local_pose.rotation -
                12.0f) > 1e-3f) {
            std::cerr << "Timeline editor smoke did not apply the inserted stepped key.\n";
            ImGui::DestroyContext();
            return 1;
        }

        marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
        temp_project.source_path = "/tmp/marrow_editor_shell_smoke.marrow";
        materialize_temp_project_runtime_assets(shell_state, &temp_project);

        const auto save_result =
            marrow::editor::save_project(temp_project, temp_project.source_path);
        if (!save_result) {
            std::cerr << save_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }
        const auto export_result = marrow::editor::export_runtime_skeleton(
            *save_result.project,
            *shell_state.load_result.base_skeleton_document,
            "/tmp/marrow_editor_shell_smoke.mskl");
        if (!export_result) {
            std::cerr << export_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }

        const auto exported_skeleton =
            marrow::runtime::load_skeleton_data(export_result.path);
        if (!exported_skeleton) {
            std::cerr << exported_skeleton.error->format();
            ImGui::DestroyContext();
            return 1;
        }
        const auto* exported_idle = exported_skeleton.skeleton_data->find_animation("idle");
        const auto exported_spine_index =
            exported_skeleton.skeleton_data->find_bone_index("spine");
        if (exported_idle == nullptr || !exported_spine_index.has_value()) {
            std::cerr << "Exported shell smoke skeleton lost the idle spine rotate track.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const auto* exported_rotate =
            exported_idle->find_rotate_timeline(*exported_spine_index);
        if (exported_rotate == nullptr || exported_rotate->keyframes.size() != 4 ||
            exported_rotate->keyframes[0].interpolation.kind() !=
                marrow::runtime::InterpolationKind::CubicBezier ||
            exported_rotate->keyframes[1].interpolation.kind() !=
                marrow::runtime::InterpolationKind::Linear ||
            exported_rotate->keyframes[2].interpolation.kind() !=
                marrow::runtime::InterpolationKind::Stepped ||
            std::abs(exported_rotate->keyframes[1].angle - 9.0f) > 1e-3f ||
            std::abs(exported_rotate->keyframes[2].angle - 12.0f) > 1e-3f) {
            std::cerr << "Timeline editor smoke export did not round-trip the edited rotate curve.\n";
            ImGui::DestroyContext();
            return 1;
        }

        *shell_state.load_result.project = std::move(previous_project);
        if (!rebuild_project_runtime(&shell_state)) {
            std::cerr << shell_state.error_message;
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.project_dirty = false;
        if (!focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
            std::cerr << "Timeline smoke failed to restore the original fixture edit.\n";
            ImGui::DestroyContext();
            return 1;
        }
    }

    {
        marrow::editor::ProjectData previous_project = *shell_state.load_result.project;
        const auto edit_index = ensure_draw_order_timeline_edit_index(&shell_state, *draw_order_track);
        if (!edit_index.has_value()) {
            std::cerr << "Timeline editor smoke could not materialize the draw-order edit.\n";
            ImGui::DestroyContext();
            return 1;
        }

        auto& draw_order_edit =
            shell_state.load_result.project->draw_order_timeline_edits[*edit_index];
        if (draw_order_edit.keyframes.size() != 3U) {
            std::cerr << "Timeline editor smoke expected the draw-order edit to start with 3 keys.\n";
            ImGui::DestroyContext();
            return 1;
        }

        draw_order_edit.keyframes[1].slot_names = {
            "body", "spark_fx", "arm_l", "fx_mask", "spawn_anchor", "hurtbox", "guide"};
        marrow::editor::DrawOrderKeyframeEdit new_draw_order_key;
        new_draw_order_key.time = 0.75;
        new_draw_order_key.slot_names = {
            "spark_fx", "body", "arm_l", "fx_mask", "spawn_anchor", "hurtbox", "guide"};
        draw_order_edit.keyframes.insert(
            draw_order_edit.keyframes.begin() + 2,
            std::move(new_draw_order_key));

        if (!rebuild_project_runtime(&shell_state)) {
            std::cerr << shell_state.error_message;
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.project_dirty = true;

        const auto body_slot_index = shell_state.load_result.skeleton_data->find_slot_index("body");
        const auto spark_fx_slot_index =
            shell_state.load_result.skeleton_data->find_slot_index("spark_fx");
        if (!body_slot_index.has_value() || !spark_fx_slot_index.has_value()) {
            std::cerr << "Timeline draw-order smoke could not resolve the edited slot indices.\n";
            ImGui::DestroyContext();
            return 1;
        }
        if (!focus_timeline_track(&shell_state, *draw_order_track, 0.5, "Smoke", false)) {
            std::cerr << "Timeline draw-order smoke could not refocus the edited draw-order track.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const auto edited_body_position =
            draw_order_position(*shell_state.preview_skeleton, *body_slot_index);
        const auto edited_spark_fx_position =
            draw_order_position(*shell_state.preview_skeleton, *spark_fx_slot_index);
        if (!edited_body_position.has_value() || !edited_spark_fx_position.has_value() ||
            *edited_body_position != 0U || *edited_spark_fx_position != 1U) {
            std::cerr << "Timeline editor smoke did not apply the edited draw-order key.\n";
            ImGui::DestroyContext();
            return 1;
        }
        if (!scrub_timeline_time(&shell_state, 0.875, "Smoke", false)) {
            std::cerr << "Timeline draw-order smoke could not scrub to the inserted key.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const auto inserted_body_position =
            draw_order_position(*shell_state.preview_skeleton, *body_slot_index);
        const auto inserted_spark_fx_position =
            draw_order_position(*shell_state.preview_skeleton, *spark_fx_slot_index);
        if (!inserted_body_position.has_value() || !inserted_spark_fx_position.has_value() ||
            *inserted_body_position != 1U || *inserted_spark_fx_position != 0U) {
            std::cerr << "Timeline editor smoke did not apply the inserted draw-order key.\n";
            ImGui::DestroyContext();
            return 1;
        }

        marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
        temp_project.source_path = "/tmp/marrow_editor_shell_draw_order_smoke.marrow";
        materialize_temp_project_runtime_assets(shell_state, &temp_project);

        const auto save_result =
            marrow::editor::save_project(temp_project, temp_project.source_path);
        if (!save_result) {
            std::cerr << save_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }
        const auto export_result = marrow::editor::export_runtime_skeleton(
            *save_result.project,
            *shell_state.load_result.base_skeleton_document,
            "/tmp/marrow_editor_shell_draw_order_smoke.mskl");
        if (!export_result) {
            std::cerr << export_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }

        const auto exported_skeleton =
            marrow::runtime::load_skeleton_data(export_result.path);
        if (!exported_skeleton) {
            std::cerr << exported_skeleton.error->format();
            ImGui::DestroyContext();
            return 1;
        }
        const auto* exported_idle = exported_skeleton.skeleton_data->find_animation("idle");
        const auto* exported_draw_order =
            exported_idle != nullptr ? exported_idle->find_draw_order_timeline() : nullptr;
        if (exported_draw_order == nullptr || exported_draw_order->keyframes.size() != 4U ||
            exported_draw_order->keyframes[1].slot_indices.size() < 3U ||
            exported_skeleton.skeleton_data->slots()[exported_draw_order->keyframes[1].slot_indices[0]].name !=
                "body" ||
            exported_skeleton.skeleton_data->slots()[exported_draw_order->keyframes[1].slot_indices[1]].name !=
                "spark_fx" ||
            exported_skeleton.skeleton_data->slots()[exported_draw_order->keyframes[2].slot_indices[0]].name !=
                "spark_fx") {
            std::cerr << "Timeline editor smoke export did not round-trip the edited draw-order keys.\n";
            ImGui::DestroyContext();
            return 1;
        }

        *shell_state.load_result.project = std::move(previous_project);
        if (!rebuild_project_runtime(&shell_state) ||
            !focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
            std::cerr << "Timeline smoke failed to restore the original draw-order fixture edit.\n";
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.project_dirty = false;
    }

    {
        marrow::editor::ProjectData previous_project = *shell_state.load_result.project;
        const auto edit_index = ensure_event_timeline_edit_index(&shell_state, *event_track);
        if (!edit_index.has_value()) {
            std::cerr << "Timeline editor smoke could not materialize the event edit.\n";
            ImGui::DestroyContext();
            return 1;
        }

        auto& event_edit =
            shell_state.load_result.project->event_timeline_edits[*edit_index];
        if (event_edit.keyframes.size() != 3U) {
            std::cerr << "Timeline editor smoke expected the event edit to start with 3 keys.\n";
            ImGui::DestroyContext();
            return 1;
        }

        event_edit.keyframes[0].int_value = 11;
        marrow::editor::EventKeyframeEdit new_event_key;
        new_event_key.time = 0.9;
        new_event_key.event_name = "dust_vfx";
        new_event_key.float_value = 0.55;
        new_event_key.string_value = "editor_trail";
        event_edit.keyframes.push_back(std::move(new_event_key));

        if (!rebuild_project_runtime(&shell_state)) {
            std::cerr << shell_state.error_message;
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.project_dirty = true;

        if (!focus_timeline_track(&shell_state, *event_track, 0.9, "Smoke", false) ||
            shell_state.preview_events.size() != 4U ||
            shell_state.preview_events.front().int_value != 11 ||
            shell_state.preview_events.back().name != "dust_vfx" ||
            std::abs(shell_state.preview_events.back().float_value - 0.55) > 1e-3 ||
            shell_state.preview_events.back().string_value != "editor_trail") {
            std::cerr << "Timeline editor smoke did not apply the edited event payloads.\n";
            ImGui::DestroyContext();
            return 1;
        }

        marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
        temp_project.source_path = "/tmp/marrow_editor_shell_event_smoke.marrow";
        materialize_temp_project_runtime_assets(shell_state, &temp_project);

        const auto save_result =
            marrow::editor::save_project(temp_project, temp_project.source_path);
        if (!save_result) {
            std::cerr << save_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }
        const auto export_result = marrow::editor::export_runtime_skeleton(
            *save_result.project,
            *shell_state.load_result.base_skeleton_document,
            "/tmp/marrow_editor_shell_event_smoke.mskl");
        if (!export_result) {
            std::cerr << export_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }

        const auto exported_skeleton =
            marrow::runtime::load_skeleton_data(export_result.path);
        if (!exported_skeleton) {
            std::cerr << exported_skeleton.error->format();
            ImGui::DestroyContext();
            return 1;
        }
        const auto* exported_idle = exported_skeleton.skeleton_data->find_animation("idle");
        const auto* exported_events =
            exported_idle != nullptr ? exported_idle->find_event_timeline() : nullptr;
        if (exported_events == nullptr || exported_events->keyframes.size() != 4U ||
            exported_events->keyframes.front().int_value != std::optional<int>(11) ||
            exported_events->keyframes.back().event_index >=
                exported_skeleton.skeleton_data->events().size() ||
            exported_skeleton.skeleton_data
                    ->events()[exported_events->keyframes.back().event_index]
                    .name != "dust_vfx" ||
            exported_events->keyframes.back().string_value !=
                std::optional<std::string>("editor_trail")) {
            std::cerr << "Timeline editor smoke export did not round-trip the edited event keys.\n";
            ImGui::DestroyContext();
            return 1;
        }

        *shell_state.load_result.project = std::move(previous_project);
        if (!rebuild_project_runtime(&shell_state) ||
            !focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
            std::cerr << "Timeline smoke failed to restore the original event fixture edit.\n";
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.project_dirty = false;
    }

    if (const auto body_slot_index = shell_state.load_result.skeleton_data->find_slot_index("body")) {
        const std::string deform_track_id =
            "slot:" + std::to_string(*body_slot_index) + ":deform:body_mesh";
        const auto deform_track = std::find_if(
            idle_tracks.begin(),
            idle_tracks.end(),
            [&](const TimelineTrackRow& track) { return track.id == deform_track_id; });
        if (deform_track == idle_tracks.end()) {
            std::cerr << "Timeline smoke validation could not find the body mesh deform track.\n";
            ImGui::DestroyContext();
            return 1;
        }
        if (!focus_timeline_track(&shell_state, *deform_track, 0.5, "Smoke", false) ||
            shell_state.selected_slot_index != body_slot_index) {
            std::cerr << "Timeline deform track focus did not synchronize slot selection.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const std::vector<double>* fixture_offsets =
            shell_state.preview_skeleton->current_mesh_vertex_offsets(*body_slot_index);
        if (fixture_offsets == nullptr || fixture_offsets->size() != 8U ||
            std::abs((*fixture_offsets)[2] - 12.0) > 1e-3 ||
            std::abs((*fixture_offsets)[3] + 8.0) > 1e-3 ||
            std::abs((*fixture_offsets)[4] - 16.0) > 1e-3 ||
            std::abs((*fixture_offsets)[5] - 20.0) > 1e-3) {
            std::cerr << "Timeline deform track focus did not apply the project-authored FFD key.\n";
            ImGui::DestroyContext();
            return 1;
        }

        {
            marrow::editor::ProjectData previous_project = *shell_state.load_result.project;
            const auto edit_index = ensure_mesh_deform_timeline_edit_index(&shell_state, *deform_track);
            if (!edit_index.has_value()) {
                std::cerr << "Timeline editor smoke could not materialize the deform edit.\n";
                ImGui::DestroyContext();
                return 1;
            }

            auto& deform_edit =
                shell_state.load_result.project->mesh_deform_timeline_edits[*edit_index];
            if (deform_edit.keyframes.size() != 3U) {
                std::cerr << "Timeline editor smoke expected the deform edit to start with 3 keys.\n";
                ImGui::DestroyContext();
                return 1;
            }

            deform_edit.keyframes[1].vertex_offsets = {0.0, 0.0, 14.0, -10.0, 18.0, 24.0, -6.0, 12.0};
            marrow::editor::DeformKeyframeEdit new_deform_key;
            new_deform_key.time = 0.75;
            new_deform_key.vertex_offsets = {0.0, 0.0, 8.0, -6.0, 10.0, 14.0, -3.0, 7.0};
            new_deform_key.interpolation = marrow::runtime::Interpolation::stepped();
            deform_edit.keyframes.insert(
                deform_edit.keyframes.begin() + 2,
                std::move(new_deform_key));

            if (!rebuild_project_runtime(&shell_state)) {
                std::cerr << shell_state.error_message;
                ImGui::DestroyContext();
                return 1;
            }
            shell_state.project_dirty = true;

            if (!focus_timeline_track(&shell_state, *deform_track, 0.5, "Smoke", false)) {
                std::cerr << "Timeline deform smoke could not refocus the edited deform track.\n";
                ImGui::DestroyContext();
                return 1;
            }
            const std::vector<double>* edited_mid_offsets =
                shell_state.preview_skeleton->current_mesh_vertex_offsets(*body_slot_index);
            if (edited_mid_offsets == nullptr || edited_mid_offsets->size() != 8U ||
                std::abs((*edited_mid_offsets)[2] - 14.0) > 1e-3 ||
                std::abs((*edited_mid_offsets)[3] + 10.0) > 1e-3 ||
                std::abs((*edited_mid_offsets)[4] - 18.0) > 1e-3 ||
                std::abs((*edited_mid_offsets)[5] - 24.0) > 1e-3) {
                std::cerr << "Timeline editor smoke did not apply edited deform offsets.\n";
                ImGui::DestroyContext();
                return 1;
            }
            if (!scrub_timeline_time(&shell_state, 0.875, "Smoke", false)) {
                std::cerr << "Timeline deform smoke could not scrub to the inserted key.\n";
                ImGui::DestroyContext();
                return 1;
            }
            const std::vector<double>* inserted_offsets =
                shell_state.preview_skeleton->current_mesh_vertex_offsets(*body_slot_index);
            if (inserted_offsets == nullptr || inserted_offsets->size() != 8U ||
                std::abs((*inserted_offsets)[2] - 8.0) > 1e-3 ||
                std::abs((*inserted_offsets)[3] + 6.0) > 1e-3 ||
                std::abs((*inserted_offsets)[4] - 10.0) > 1e-3 ||
                std::abs((*inserted_offsets)[5] - 14.0) > 1e-3) {
                std::cerr << "Timeline editor smoke did not apply the inserted deform key.\n";
                ImGui::DestroyContext();
                return 1;
            }

            marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
            temp_project.source_path = "/tmp/marrow_editor_shell_deform_smoke.marrow";
            materialize_temp_project_runtime_assets(shell_state, &temp_project);

            const auto save_result =
                marrow::editor::save_project(temp_project, temp_project.source_path);
            if (!save_result) {
                std::cerr << save_result.error->format() << '\n';
                ImGui::DestroyContext();
                return 1;
            }
            const auto export_result = marrow::editor::export_runtime_skeleton(
                *save_result.project,
                *shell_state.load_result.base_skeleton_document,
                "/tmp/marrow_editor_shell_deform_smoke.mskl");
            if (!export_result) {
                std::cerr << export_result.error->format() << '\n';
                ImGui::DestroyContext();
                return 1;
            }

            const auto exported_skeleton =
                marrow::runtime::load_skeleton_data(export_result.path);
            if (!exported_skeleton) {
                std::cerr << exported_skeleton.error->format();
                ImGui::DestroyContext();
                return 1;
            }

            const auto exported_body_slot_index =
                exported_skeleton.skeleton_data->find_slot_index("body");
            const auto* exported_idle =
                exported_skeleton.skeleton_data->find_animation("idle");
            if (!exported_body_slot_index.has_value() || exported_idle == nullptr) {
                std::cerr << "Exported shell smoke skeleton lost the idle deform track.\n";
                ImGui::DestroyContext();
                return 1;
            }
            const auto* exported_deform =
                exported_idle->find_deform_timeline(*exported_body_slot_index, "body_mesh");
            if (exported_deform == nullptr || exported_deform->keyframes.size() != 4U ||
                std::abs(exported_deform->keyframes[1].vertex_offsets[2] - 14.0f) > 1e-3f ||
                std::abs(exported_deform->keyframes[2].vertex_offsets[2] - 8.0f) > 1e-3f ||
                exported_deform->keyframes[2].interpolation.kind() !=
                    marrow::runtime::InterpolationKind::Stepped) {
                std::cerr << "Timeline editor smoke export did not round-trip the edited deform keys.\n";
                ImGui::DestroyContext();
                return 1;
            }

            marrow::runtime::Skeleton exported_preview(exported_skeleton.skeleton_data);
            if (!exported_preview.set_skin("warrior")) {
                std::cerr << "Exported deform smoke could not activate the warrior skin.\n";
                ImGui::DestroyContext();
                return 1;
            }
            exported_preview.apply_animation(*exported_idle, 0.875);
            const std::vector<double>* exported_offsets =
                exported_preview.current_mesh_vertex_offsets(*exported_body_slot_index);
            if (exported_offsets == nullptr || exported_offsets->size() != 8U ||
                std::abs((*exported_offsets)[2] - 8.0) > 1e-3 ||
                std::abs((*exported_offsets)[5] - 14.0) > 1e-3) {
                std::cerr << "Runtime playback did not preserve the exported deform offsets.\n";
                ImGui::DestroyContext();
                return 1;
            }

            *shell_state.load_result.project = std::move(previous_project);
            if (!rebuild_project_runtime(&shell_state) ||
                !focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
                std::cerr << "Timeline smoke failed to restore the original deform fixture edit.\n";
                ImGui::DestroyContext();
                return 1;
            }
            shell_state.project_dirty = false;
        }

        const auto* animated_attachment =
            shell_state.preview_skeleton->current_attachment(*body_slot_index);
        if (animated_attachment == nullptr || animated_attachment->name != "warrior_body") {
            std::cerr << "Timeline scrub did not synchronize the animated body attachment.\n";
            ImGui::DestroyContext();
            return 1;
        }
    }

    if (const auto body_slot_index = shell_state.load_result.skeleton_data->find_slot_index("body")) {
        select_slot(&shell_state, *body_slot_index, "Smoke", false);
        if (!shell_state.selected_slot_index.has_value() ||
            *shell_state.selected_slot_index != *body_slot_index) {
            std::cerr << "Slot selection did not update the inspector slot state.\n";
            ImGui::DestroyContext();
            return 1;
        }
        if (!shell_state.selected_attachment.has_value() ||
            shell_state.selected_attachment->attachment_name != "warrior_body") {
            std::cerr << "Slot selection did not resolve the animated body attachment at t=0.5.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto selected_attachment_reference = resolve_attachment_reference(
            *shell_state.load_result.skeleton_data,
            *shell_state.selected_attachment);
        if (!selected_attachment_reference.has_value()) {
            std::cerr << "Slot selection did not resolve the selected body attachment reference.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const std::vector<MeshWeightVertexRow> weight_rows = build_mesh_weight_rows(
            *shell_state.load_result.skeleton_data,
            *selected_attachment_reference->attachment);
        if (weight_rows.size() != 4U ||
            weight_rows[1].influences.size() != 2U ||
            weight_rows[1].influences[0].bone_name != "spine" ||
            std::abs(weight_rows[1].influences[0].weight - 0.75) > 1e-3 ||
            weight_rows[1].influences[1].bone_name != "arm_l" ||
            std::abs(weight_rows[1].influences[1].weight - 0.25) > 1e-3) {
            std::cerr << "Mesh weight inspector data did not expose the expected weighted influences.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto warrior_skin_index =
            shell_state.load_result.skeleton_data->find_skin_index("warrior");
        if (!warrior_skin_index.has_value() ||
            !set_preview_skin_enabled(&shell_state, *warrior_skin_index, true, false)) {
            std::cerr << "Failed to enable the warrior preview skin in smoke validation.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto* warrior_attachment =
            shell_state.preview_skeleton->current_attachment(*body_slot_index);
        if (warrior_attachment == nullptr || warrior_attachment->name != "warrior_body" ||
            !warrior_attachment->linked_mesh.has_value()) {
            std::cerr << "Skin preview did not activate the warrior linked mesh attachment.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto mage_skin_index =
            shell_state.load_result.skeleton_data->find_skin_index("mage");
        if (!mage_skin_index.has_value()) {
            std::cerr << "Mage skin is missing from the sample project.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const AttachmentSelection mage_attachment_selection{
            *body_slot_index,
            *mage_skin_index,
            "mage_body"};
        const auto mage_attachment_reference = resolve_attachment_reference(
            *shell_state.load_result.skeleton_data,
            mage_attachment_selection);
        if (!mage_attachment_reference.has_value() ||
            !mage_attachment_reference->attachment->linked_mesh.has_value()) {
            std::cerr << "Attachment inspector smoke could not resolve the mage linked mesh.\n";
            ImGui::DestroyContext();
            return 1;
        }

        select_attachment(&shell_state, mage_attachment_selection, "Smoke", false);
        if (!apply_attachment_selection_to_preview_slot(
                &shell_state,
                mage_attachment_selection,
                "Smoke",
                false)) {
            std::cerr << "Attachment preview override failed for mage_body.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto* mage_attachment =
            shell_state.preview_skeleton->current_attachment(*body_slot_index);
        if (mage_attachment == nullptr || mage_attachment->name != "mage_body" ||
            !mage_attachment->linked_mesh.has_value() ||
            mage_attachment->linked_mesh->parent_attachment != "body_mesh") {
            std::cerr << "Attachment override did not expose the linked mesh relationship.\n";
            ImGui::DestroyContext();
            return 1;
        }

        if (!reset_preview_slot_to_skin_selection(&shell_state, *body_slot_index, "Smoke", false)) {
            std::cerr << "Failed to reset the body slot to the active skin preview.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto* restored_attachment =
            shell_state.preview_skeleton->current_attachment(*body_slot_index);
        if (restored_attachment == nullptr || restored_attachment->name != "warrior_body") {
            std::cerr << "Resetting the slot preview did not restore the warrior skin state.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto arm_index = shell_state.load_result.skeleton_data->find_bone_index("arm_l");
        if (!arm_index.has_value()) {
            std::cerr << "Weight paint smoke could not resolve arm_l.\n";
            ImGui::DestroyContext();
            return 1;
        }
        if (!set_selected_animation(&shell_state, "attack", "Smoke", false, true) ||
            !scrub_timeline_time(&shell_state, 0.2, "Smoke", false)) {
            std::cerr << "Weight paint smoke could not scrub the attack preview pose.\n";
            ImGui::DestroyContext();
            return 1;
        }
        select_bone(&shell_state, *arm_index, "Smoke", false);
        shell_state.weight_paint.enabled = true;
        shell_state.weight_paint.show_heatmap = true;
        shell_state.weight_paint.radius_pixels = 20.0f;
        shell_state.weight_paint.strength = 1.0f;
        shell_state.weight_paint.mode = WeightPaintMode::Paint;

        const auto require_weight_near =
            [](auto actual, double expected, double epsilon, std::string_view label) {
                const double actual_value = static_cast<double>(actual);
                if (std::abs(actual_value - expected) <= epsilon) {
                    return true;
                }

                std::cerr << label << " expected " << expected << " but was "
                          << actual_value << '\n';
                return false;
            };
        const auto build_weight_overlay = [&]() -> std::optional<MeshWeightOverlay> {
            const auto layout = build_viewport_layout(
                shell_state,
                ImVec2(0.0f, 0.0f),
                ImVec2(1280.0f, 720.0f));
            if (!layout.has_value()) {
                return std::nullopt;
            }
            return build_mesh_weight_overlay(shell_state, *layout);
        };
        const auto current_weight_target = [&]() -> std::optional<MeshWeightPaintTarget> {
            return current_mesh_weight_paint_target(shell_state);
        };
        const auto current_arm_weight = [&](std::size_t vertex_index) -> std::optional<double> {
            const std::optional<MeshWeightPaintTarget> target = current_weight_target();
            if (!target.has_value() ||
                target->source_attachment == nullptr ||
                target->source_attachment->mesh_geometry == nullptr ||
                vertex_index >= target->source_attachment->mesh_geometry->weights.size()) {
                return std::nullopt;
            }

            return weight_for_bone(
                target->source_attachment->mesh_geometry->weights[vertex_index],
                *arm_index);
        };
        const auto current_vertex_weight_total = [&](std::size_t vertex_index) -> std::optional<double> {
            const std::optional<MeshWeightPaintTarget> target = current_weight_target();
            if (!target.has_value() ||
                target->source_attachment == nullptr ||
                target->source_attachment->mesh_geometry == nullptr ||
                vertex_index >= target->source_attachment->mesh_geometry->weights.size()) {
                return std::nullopt;
            }

            double total = 0.0;
            for (const auto& influence :
                 target->source_attachment->mesh_geometry->weights[vertex_index].influences) {
                total += influence.weight;
            }
            return total;
        };
        const auto current_body_pose = [&]() -> std::optional<marrow::runtime::MeshAttachmentPose> {
            return shell_state.preview_skeleton->evaluate_current_mesh_attachment(*body_slot_index);
        };

        const EditorHistorySnapshot weight_paint_baseline =
            capture_history_snapshot(shell_state);
        shell_state.command_stack.clear();
        shell_state.pending_edit_action.reset();
        update_project_dirty_state(&shell_state);

        const std::optional<MeshWeightOverlay> baseline_overlay = build_weight_overlay();
        const std::optional<marrow::runtime::MeshAttachmentPose> baseline_pose =
            current_body_pose();
        if (!baseline_overlay.has_value() ||
            !baseline_pose.has_value() ||
            baseline_overlay->target.source_skin_name != "mesh_base" ||
            baseline_overlay->target.source_attachment_name != "body_mesh" ||
            baseline_overlay->target.display_attachment_name != "warrior_body" ||
            baseline_overlay->vertices.size() != 4U ||
            !require_weight_near(baseline_overlay->vertices[0].weight, 0.0, 1e-6, "baseline vertex0 arm weight") ||
            !require_weight_near(baseline_overlay->vertices[1].weight, 0.25, 1e-6, "baseline vertex1 arm weight") ||
            !require_weight_near(baseline_overlay->vertices[2].weight, 0.75, 1e-6, "baseline vertex2 arm weight")) {
            std::cerr << "Weight paint smoke did not resolve the expected linked-mesh paint target.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const ImVec4 heat_blue = mesh_weight_heatmap_color(0.0, 1.0f);
        const ImVec4 heat_green = mesh_weight_heatmap_color(1.0 / 3.0, 1.0f);
        const ImVec4 heat_yellow = mesh_weight_heatmap_color(2.0 / 3.0, 1.0f);
        const ImVec4 heat_red = mesh_weight_heatmap_color(1.0, 1.0f);
        if (!(heat_blue.z > heat_blue.y && heat_blue.z > heat_blue.x) ||
            !(heat_green.y > heat_green.x && heat_green.y > heat_green.z) ||
            !(heat_yellow.x > 0.8f && heat_yellow.y > 0.7f && heat_yellow.z < 0.4f) ||
            !(heat_red.x > heat_red.y && heat_red.x > heat_red.z)) {
            std::cerr << "Weight paint smoke did not expose the expected blue/green/yellow/red heat-map ramp.\n";
            ImGui::DestroyContext();
            return 1;
        }

        begin_weight_paint_stroke(&shell_state, baseline_overlay->target);
        if (!apply_weight_paint_sample(
                &shell_state,
                *baseline_overlay,
                baseline_overlay->vertices[1].screen_position) ||
            !finish_weight_paint_stroke(&shell_state)) {
            std::cerr << "Weight paint smoke could not apply a paint stroke.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const std::optional<MeshWeightOverlay> painted_overlay = build_weight_overlay();
        const std::optional<marrow::runtime::MeshAttachmentPose> painted_pose =
            current_body_pose();
        const std::optional<double> painted_arm_weight = current_arm_weight(1U);
        const std::optional<double> painted_total_weight = current_vertex_weight_total(1U);
        if (shell_state.command_stack.undo_count() != 1U ||
            shell_state.command_stack.redo_count() != 0U ||
            shell_state.load_result.project->mesh_weight_attachment_edits.size() != 1U ||
            !painted_overlay.has_value() ||
            !painted_pose.has_value() ||
            !painted_arm_weight.has_value() ||
            !painted_total_weight.has_value() ||
            !require_weight_near(*painted_arm_weight, 0.625, 1e-6, "painted vertex1 arm weight") ||
            !require_weight_near(*painted_total_weight, 1.0, 1e-6, "painted vertex1 total weight") ||
            !require_weight_near(painted_overlay->vertices[1].weight, 0.625, 1e-6, "painted overlay vertex1 arm weight") ||
            !(std::abs(painted_pose->vertices[1].x - baseline_pose->vertices[1].x) > 1e-3 ||
              std::abs(painted_pose->vertices[1].y - baseline_pose->vertices[1].y) > 1e-3)) {
            std::cerr << "Weight paint smoke did not apply the painted weight, normalization, or live preview deformation.\n";
            ImGui::DestroyContext();
            return 1;
        }

        if (!undo_project_change(&shell_state)) {
            std::cerr << "Weight paint smoke could not undo the paint stroke.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const std::optional<double> undone_paint_weight = current_arm_weight(1U);
        const std::optional<marrow::runtime::MeshAttachmentPose> undone_paint_pose =
            current_body_pose();
        if (shell_state.command_stack.undo_count() != 0U ||
            shell_state.command_stack.redo_count() != 1U ||
            !undone_paint_weight.has_value() ||
            !undone_paint_pose.has_value() ||
            !require_weight_near(*undone_paint_weight, 0.25, 1e-6, "undone painted vertex1 arm weight") ||
            !require_weight_near(undone_paint_pose->vertices[1].x, baseline_pose->vertices[1].x, 1e-6, "undone painted vertex1 x") ||
            !require_weight_near(undone_paint_pose->vertices[1].y, baseline_pose->vertices[1].y, 1e-6, "undone painted vertex1 y")) {
            std::cerr << "Weight paint smoke undo did not restore the baseline mesh state.\n";
            ImGui::DestroyContext();
            return 1;
        }

        if (!redo_project_change(&shell_state)) {
            std::cerr << "Weight paint smoke could not redo the paint stroke.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const std::optional<double> redone_paint_weight = current_arm_weight(1U);
        const std::optional<marrow::runtime::MeshAttachmentPose> redone_paint_pose =
            current_body_pose();
        if (shell_state.command_stack.undo_count() != 1U ||
            shell_state.command_stack.redo_count() != 0U ||
            !redone_paint_weight.has_value() ||
            !redone_paint_pose.has_value() ||
            !require_weight_near(*redone_paint_weight, 0.625, 1e-6, "redone painted vertex1 arm weight") ||
            !require_weight_near(redone_paint_pose->vertices[1].x, painted_pose->vertices[1].x, 1e-6, "redone painted vertex1 x") ||
            !require_weight_near(redone_paint_pose->vertices[1].y, painted_pose->vertices[1].y, 1e-6, "redone painted vertex1 y")) {
            std::cerr << "Weight paint smoke redo did not restore the painted mesh state.\n";
            ImGui::DestroyContext();
            return 1;
        }

        shell_state.weight_paint.mode = WeightPaintMode::Erase;
        const std::optional<MeshWeightOverlay> erase_overlay = build_weight_overlay();
        if (!erase_overlay.has_value()) {
            std::cerr << "Weight paint smoke could not build the erase overlay.\n";
            ImGui::DestroyContext();
            return 1;
        }
        begin_weight_paint_stroke(&shell_state, erase_overlay->target);
        if (!apply_weight_paint_sample(
                &shell_state,
                *erase_overlay,
                erase_overlay->vertices[1].screen_position) ||
            !finish_weight_paint_stroke(&shell_state)) {
            std::cerr << "Weight paint smoke could not apply an erase stroke.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const std::optional<double> erased_arm_weight = current_arm_weight(1U);
        const std::optional<double> erased_total_weight = current_vertex_weight_total(1U);
        if (shell_state.command_stack.undo_count() != 2U ||
            shell_state.command_stack.redo_count() != 0U ||
            !erased_arm_weight.has_value() ||
            !erased_total_weight.has_value() ||
            !require_weight_near(*erased_arm_weight, 0.0, 1e-6, "erased vertex1 arm weight") ||
            !require_weight_near(*erased_total_weight, 1.0, 1e-6, "erased vertex1 total weight")) {
            std::cerr << "Weight paint smoke did not erase the active bone influence.\n";
            ImGui::DestroyContext();
            return 1;
        }

        if (!undo_project_change(&shell_state)) {
            std::cerr << "Weight paint smoke could not undo the erase stroke.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const std::optional<double> undone_erase_weight = current_arm_weight(1U);
        if (!undone_erase_weight.has_value() ||
            !require_weight_near(*undone_erase_weight, 0.625, 1e-6, "undone erased vertex1 arm weight")) {
            std::cerr << "Weight paint smoke undo did not restore the painted influence.\n";
            ImGui::DestroyContext();
            return 1;
        }

        if (!redo_project_change(&shell_state)) {
            std::cerr << "Weight paint smoke could not redo the erase stroke.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const std::optional<double> redone_erase_weight = current_arm_weight(1U);
        if (!redone_erase_weight.has_value() ||
            !require_weight_near(*redone_erase_weight, 0.0, 1e-6, "redone erased vertex1 arm weight")) {
            std::cerr << "Weight paint smoke redo did not restore the erased influence.\n";
            ImGui::DestroyContext();
            return 1;
        }

        if (!apply_history_snapshot(&shell_state, weight_paint_baseline)) {
            std::cerr << "Weight paint smoke could not restore the baseline mesh state.\n";
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.command_stack.clear();
        shell_state.pending_edit_action.reset();
        update_project_dirty_state(&shell_state);

        shell_state.weight_paint.mode = WeightPaintMode::Smooth;
        const std::optional<MeshWeightOverlay> smooth_overlay = build_weight_overlay();
        if (!smooth_overlay.has_value()) {
            std::cerr << "Weight paint smoke could not build the smooth overlay.\n";
            ImGui::DestroyContext();
            return 1;
        }
        begin_weight_paint_stroke(&shell_state, smooth_overlay->target);
        if (!apply_weight_paint_sample(
                &shell_state,
                *smooth_overlay,
                smooth_overlay->vertices[0].screen_position) ||
            !finish_weight_paint_stroke(&shell_state)) {
            std::cerr << "Weight paint smoke could not apply a smooth stroke.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const std::optional<double> smoothed_arm_weight = current_arm_weight(0U);
        const std::optional<double> smoothed_total_weight = current_vertex_weight_total(0U);
        if (shell_state.command_stack.undo_count() != 1U ||
            shell_state.command_stack.redo_count() != 0U ||
            !smoothed_arm_weight.has_value() ||
            !smoothed_total_weight.has_value() ||
            !require_weight_near(*smoothed_arm_weight, 0.3125, 1e-6, "smoothed vertex0 arm weight") ||
            !require_weight_near(*smoothed_total_weight, 1.0, 1e-6, "smoothed vertex0 total weight")) {
            std::cerr << "Weight paint smoke did not smooth the neighboring influences.\n";
            ImGui::DestroyContext();
            return 1;
        }

        marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
        temp_project.source_path = "/tmp/marrow_editor_shell_weight_paint_smoke.marrow";
        materialize_temp_project_runtime_assets(shell_state, &temp_project);
        const auto save_result =
            marrow::editor::save_project(temp_project, temp_project.source_path);
        if (!save_result) {
            std::cerr << save_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }
        const auto export_result = marrow::editor::export_runtime_skeleton(
            *save_result.project,
            *shell_state.load_result.base_skeleton_document,
            "/tmp/marrow_editor_shell_weight_paint_smoke.mskl");
        if (!export_result) {
            std::cerr << export_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }

        const auto exported_skeleton =
            marrow::runtime::load_skeleton_data(export_result.path);
        if (!exported_skeleton) {
            std::cerr << exported_skeleton.error->format();
            ImGui::DestroyContext();
            return 1;
        }
        const auto exported_body_slot_index =
            exported_skeleton.skeleton_data->find_slot_index("body");
        const auto exported_arm_index =
            exported_skeleton.skeleton_data->find_bone_index("arm_l");
        const auto* exported_attachment =
            exported_body_slot_index.has_value()
                ? exported_skeleton.skeleton_data->find_attachment(
                      "mesh_base",
                      *exported_body_slot_index,
                      "body_mesh")
                : nullptr;
        if (!exported_body_slot_index.has_value() ||
            !exported_arm_index.has_value() ||
            exported_attachment == nullptr ||
            exported_attachment->mesh_geometry == nullptr ||
            exported_attachment->mesh_geometry->weights.size() <= 0U ||
            !require_weight_near(
                weight_for_bone(
                    exported_attachment->mesh_geometry->weights[0],
                    *exported_arm_index),
                0.3125,
                1e-6,
                "exported smoothed vertex0 arm weight")) {
            std::cerr << "Weight paint smoke export did not round-trip the authored mesh weights.\n";
            ImGui::DestroyContext();
            return 1;
        }

        if (!undo_project_change(&shell_state) ||
            shell_state.command_stack.undo_count() != 0U ||
            shell_state.command_stack.redo_count() != 1U) {
            std::cerr << "Weight paint smoke could not undo the smooth stroke.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const std::optional<double> undone_smooth_weight = current_arm_weight(0U);
        if (!undone_smooth_weight.has_value() ||
            !require_weight_near(*undone_smooth_weight, 0.0, 1e-6, "undone smoothed vertex0 arm weight")) {
            std::cerr << "Weight paint smoke undo did not restore the unsmoothed mesh weights.\n";
            ImGui::DestroyContext();
            return 1;
        }

        if (!redo_project_change(&shell_state) ||
            shell_state.command_stack.undo_count() != 1U ||
            shell_state.command_stack.redo_count() != 0U) {
            std::cerr << "Weight paint smoke could not redo the smooth stroke.\n";
            ImGui::DestroyContext();
            return 1;
        }
        const std::optional<double> redone_smooth_weight = current_arm_weight(0U);
        if (!redone_smooth_weight.has_value() ||
            !require_weight_near(*redone_smooth_weight, 0.3125, 1e-6, "redone smoothed vertex0 arm weight")) {
            std::cerr << "Weight paint smoke redo did not restore the smoothed mesh weights.\n";
            ImGui::DestroyContext();
            return 1;
        }

        if (!apply_history_snapshot(&shell_state, weight_paint_baseline)) {
            std::cerr << "Weight paint smoke could not restore the baseline state after export validation.\n";
            ImGui::DestroyContext();
            return 1;
        }
        reset_weight_paint_stroke(&shell_state);
        shell_state.weight_paint.enabled = false;
        shell_state.command_stack.clear();
        shell_state.pending_edit_action.reset();
        update_project_dirty_state(&shell_state);
    }

    if (!set_selected_animation(&shell_state, "attack", "Smoke", false, true)) {
        std::cerr << "State preview smoke could not select the attack animation.\n";
        ImGui::DestroyContext();
        return 1;
    }
    shell_state.preview_queue_enabled = true;
    shell_state.preview_queued_animation_name = "aim";
    shell_state.preview_queue_delay = 0.0;
    shell_state.preview_use_custom_mix_duration = true;
    shell_state.preview_custom_mix_duration = 0.1;
    shell_state.preview_reverse = false;
    if (!scrub_timeline_time(&shell_state, 0.45, "Smoke", false)) {
        std::cerr << "State preview smoke could not scrub the queued attack->aim transition.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (std::abs(timeline_preview_duration(shell_state) - 0.9) > 1e-6) {
        std::cerr << "State preview smoke did not compute the queued preview duration.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (const auto arm_index = shell_state.load_result.skeleton_data->find_bone_index("arm_l")) {
        const double mixed_rotation =
            static_cast<double>(
                shell_state.preview_skeleton->bone_poses()[*arm_index].local_pose.rotation);
        if (std::abs(mixed_rotation - 45.0) > 1e-3) {
            std::cerr << "State preview smoke did not apply the queued mix pose.\n";
            ImGui::DestroyContext();
            return 1;
        }
    }

    shell_state.preview_queue_enabled = false;
    shell_state.preview_use_custom_mix_duration = false;
    shell_state.preview_reverse = true;
    if (!set_selected_animation(&shell_state, "idle", "Smoke", false, true) ||
        !scrub_timeline_time(&shell_state, 0.35, "Smoke", false)) {
        std::cerr << "State preview smoke could not scrub reverse idle playback.\n";
        ImGui::DestroyContext();
        return 1;
    }
    if (std::abs(shell_state.preview_root_motion_total.x + 14.0) > 1e-3 ||
        std::abs(shell_state.preview_root_motion_total.y - 7.0) > 1e-3) {
        std::cerr << "State preview smoke did not surface reverse root-motion playback.\n";
        ImGui::DestroyContext();
        return 1;
    }
    shell_state.preview_reverse = false;

    const auto require_smoke_near =
        [](auto actual, auto expected, double epsilon, std::string_view label) {
            const double actual_value = static_cast<double>(actual);
            const double expected_value = static_cast<double>(expected);
            if (std::abs(actual_value - expected_value) <= epsilon) {
                return true;
            }

            std::cerr << label << " expected " << expected_value << " but was " << actual_value
                      << ".\n";
            return false;
        };
    constexpr double kConstraintPreviewPositionEpsilon = 5e-2;

    shell_state.preview_skin_names = {"default"};
    shell_state.preview_slot_overrides.assign(
        shell_state.preview_slot_overrides.size(), std::nullopt);
    if (!set_selected_animation(&shell_state, "idle", "Smoke", false, true)) {
        std::cerr << "Constraint smoke could not restore the idle preview state.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const auto ik_tip_index = shell_state.load_result.skeleton_data->find_bone_index("ik_tip");
    const auto ik_target_index = shell_state.load_result.skeleton_data->find_bone_index("ik_target");
    const auto path_a_index = shell_state.load_result.skeleton_data->find_bone_index("path_a");
    const auto path_b_index = shell_state.load_result.skeleton_data->find_bone_index("path_b");
    const auto path_c_index = shell_state.load_result.skeleton_data->find_bone_index("path_c");
    const auto transform_source_index =
        shell_state.load_result.skeleton_data->find_bone_index("transform_source");
    const auto transform_target_index =
        shell_state.load_result.skeleton_data->find_bone_index("transform_target");
    const auto pivot_index = shell_state.load_result.skeleton_data->find_bone_index("pivot");
    const auto ribbon_tip_index =
        shell_state.load_result.skeleton_data->find_bone_index("ribbon_tip");
    if (!ik_tip_index.has_value() || !ik_target_index.has_value() || !path_a_index.has_value() ||
        !path_b_index.has_value() || !path_c_index.has_value() ||
        !transform_source_index.has_value() || !transform_target_index.has_value() ||
        !pivot_index.has_value() || !ribbon_tip_index.has_value()) {
        std::cerr << "Constraint smoke fixture lost the helper bones needed for authoring validation.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const auto* authored_ik = find_named_constraint(
        shell_state.load_result.skeleton_data->ik_constraints(),
        "editor_arm_reach");
    const auto* authored_path = find_named_constraint(
        shell_state.load_result.skeleton_data->path_constraints(),
        "editor_guide_follow");
    const auto* authored_transform = find_named_constraint(
        shell_state.load_result.skeleton_data->transform_constraints(),
        "editor_transform_follow");
    const auto* authored_physics = find_named_constraint(
        shell_state.load_result.skeleton_data->physics_constraints(),
        "editor_ribbon_secondary");
    if (shell_state.load_result.project->ik_constraint_edits.size() != 1U ||
        shell_state.load_result.project->path_constraint_edits.size() != 1U ||
        shell_state.load_result.project->transform_constraint_edits.size() != 1U ||
        shell_state.load_result.project->physics_constraint_edits.size() != 1U ||
        authored_ik == nullptr || authored_path == nullptr ||
        authored_transform == nullptr || authored_physics == nullptr) {
        std::cerr << "Constraint smoke fixture did not expose the expected authored constraint edits.\n";
        ImGui::DestroyContext();
        return 1;
    }

    select_constraint(
        &shell_state,
        ConstraintEditKind::Transform,
        "editor_transform_follow",
        "Smoke",
        false);
    if (!shell_state.selected_constraint.has_value() ||
        shell_state.selected_constraint->kind != ConstraintEditKind::Transform ||
        shell_state.selected_constraint->name != "editor_transform_follow") {
        std::cerr << "Constraint selection smoke did not preserve the chosen transform constraint.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const auto& ik_tip_world =
        shell_state.preview_skeleton->bone_world_transforms()[*ik_tip_index];
    const auto& ik_target_world =
        shell_state.preview_skeleton->bone_world_transforms()[*ik_target_index];
    if (!require_smoke_near(
            ik_tip_world.world_x,
            ik_target_world.world_x,
            kConstraintPreviewPositionEpsilon,
            "constraint preview IK tip x") ||
        !require_smoke_near(
            ik_tip_world.world_y,
            ik_target_world.world_y,
            kConstraintPreviewPositionEpsilon,
            "constraint preview IK tip y")) {
        std::cerr << "Constraint preview did not solve the authored IK reach target.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const auto& path_a_world =
        shell_state.preview_skeleton->bone_world_transforms()[*path_a_index];
    const auto& path_b_world =
        shell_state.preview_skeleton->bone_world_transforms()[*path_b_index];
    const auto& path_c_world =
        shell_state.preview_skeleton->bone_world_transforms()[*path_c_index];
    if (!require_smoke_near(
            path_a_world.world_x,
            20.0,
            kConstraintPreviewPositionEpsilon,
            "constraint preview path_a x") ||
        !require_smoke_near(
            path_a_world.world_y,
            0.0,
            kConstraintPreviewPositionEpsilon,
            "constraint preview path_a y") ||
        !require_smoke_near(
            path_b_world.world_x,
            80.0,
            kConstraintPreviewPositionEpsilon,
            "constraint preview path_b x") ||
        !require_smoke_near(
            path_b_world.world_y,
            0.0,
            kConstraintPreviewPositionEpsilon,
            "constraint preview path_b y") ||
        !require_smoke_near(
            path_c_world.world_x,
            100.0,
            kConstraintPreviewPositionEpsilon,
            "constraint preview path_c x") ||
        !require_smoke_near(
            path_c_world.world_y,
            40.0,
            kConstraintPreviewPositionEpsilon,
            "constraint preview path_c y")) {
        std::cerr << "Constraint preview did not place the path chain on the authored guide.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const double expected_transform_rotation = 12.5;
    const double expected_transform_x = 215.0;
    const double expected_transform_y = 7.5;
    const double expected_transform_scale_x = 1.5;
    const double expected_transform_scale_y = 0.7;
    const double expected_transform_shear_x = 9.75;
    const double expected_transform_shear_y = -4.5;
    const double kPi = 3.14159265358979323846;
    const double transform_x_radians =
        (expected_transform_rotation + expected_transform_shear_x) * kPi / 180.0;
    const double transform_y_radians =
        (expected_transform_rotation + 90.0 + expected_transform_shear_y) * kPi / 180.0;
    const auto& transform_target_world =
        shell_state.preview_skeleton->bone_world_transforms()[*transform_target_index];
    if (!require_smoke_near(
            transform_target_world.world_x,
            expected_transform_x,
            kConstraintPreviewPositionEpsilon,
            "constraint preview transform target x") ||
        !require_smoke_near(
            transform_target_world.world_y,
            expected_transform_y,
            kConstraintPreviewPositionEpsilon,
            "constraint preview transform target y") ||
        !require_smoke_near(
            transform_target_world.a,
            std::cos(transform_x_radians) * expected_transform_scale_x,
            1e-3,
            "constraint preview transform target axis a") ||
        !require_smoke_near(
            transform_target_world.b,
            std::cos(transform_y_radians) * expected_transform_scale_y,
            1e-3,
            "constraint preview transform target axis b") ||
        !require_smoke_near(
            transform_target_world.c,
            std::sin(transform_x_radians) * expected_transform_scale_x,
            1e-3,
            "constraint preview transform target axis c") ||
        !require_smoke_near(
            transform_target_world.d,
            std::sin(transform_y_radians) * expected_transform_scale_y,
            1e-3,
            "constraint preview transform target axis d")) {
        std::cerr << "Constraint preview did not apply the authored transform constraint.\n";
        ImGui::DestroyContext();
        return 1;
    }

    const auto& setup_ribbon_tip =
        shell_state.preview_skeleton->bone_world_transforms()[*ribbon_tip_index];
    if (!require_smoke_near(
            setup_ribbon_tip.world_x,
            230.0,
            5.0,
            "constraint preview setup ribbon tip x") ||
        !require_smoke_near(
            setup_ribbon_tip.world_y,
            -120.0,
            5.0,
            "constraint preview setup ribbon tip y")) {
        std::cerr << "Constraint preview lost the helper ribbon setup pose.\n";
        ImGui::DestroyContext();
        return 1;
    }

    {
        marrow::editor::ProjectData previous_project = *shell_state.load_result.project;
        const auto ik_edit_index =
            ensure_ik_constraint_edit_index(&shell_state, "editor_arm_reach");
        const auto path_edit_index =
            ensure_path_constraint_edit_index(&shell_state, "editor_guide_follow");
        const auto transform_edit_index =
            ensure_transform_constraint_edit_index(&shell_state, "editor_transform_follow");
        const auto physics_edit_index =
            ensure_physics_constraint_edit_index(&shell_state, "editor_ribbon_secondary");
        if (!ik_edit_index.has_value() || !path_edit_index.has_value() ||
            !transform_edit_index.has_value() || !physics_edit_index.has_value()) {
            std::cerr << "Constraint editor smoke could not materialize the authored constraint edits.\n";
            ImGui::DestroyContext();
            return 1;
        }

        shell_state.load_result.project->path_constraint_edits[*path_edit_index].position = 0.0;
        shell_state.load_result.project->transform_constraint_edits[*transform_edit_index]
            .translate_mix = 0.5;
        shell_state.load_result.project->physics_constraint_edits[*physics_edit_index].wind.x =
            18.0;

        marrow::editor::IkConstraintEdit created_ik;
        created_ik.name =
            unique_constraint_name(shell_state, ConstraintEditKind::Ik, "editor_created_ik");
        created_ik.bone_names = {"transform_target"};
        created_ik.target_bone_name = "transform_source";
        created_ik.mix = 0.0;
        created_ik.bend_positive = true;
        shell_state.load_result.project->ik_constraint_edits.push_back(created_ik);
        const std::string created_ik_name = created_ik.name;

        select_constraint(
            &shell_state,
            ConstraintEditKind::Ik,
            created_ik_name,
            "Smoke",
            false);

        if (!rebuild_project_runtime(&shell_state)) {
            std::cerr << shell_state.error_message;
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.project_dirty = true;

        const auto* created_runtime_ik = find_named_constraint(
            shell_state.load_result.skeleton_data->ik_constraints(),
            created_ik_name);
        const auto* edited_runtime_path = find_named_constraint(
            shell_state.load_result.skeleton_data->path_constraints(),
            "editor_guide_follow");
        const auto* edited_runtime_transform = find_named_constraint(
            shell_state.load_result.skeleton_data->transform_constraints(),
            "editor_transform_follow");
        const auto* edited_runtime_physics = find_named_constraint(
            shell_state.load_result.skeleton_data->physics_constraints(),
            "editor_ribbon_secondary");
        if (created_runtime_ik == nullptr || edited_runtime_path == nullptr ||
            edited_runtime_transform == nullptr || edited_runtime_physics == nullptr ||
            !require_smoke_near(
                edited_runtime_path->position,
                0.0,
                1e-6,
                "edited runtime path position") ||
            !require_smoke_near(
                edited_runtime_transform->translate_mix,
                0.5,
                1e-6,
                "edited runtime transform translate mix") ||
            !require_smoke_near(
                edited_runtime_physics->wind.x,
                18.0,
                1e-6,
                "edited runtime physics wind.x") ||
            !require_smoke_near(created_runtime_ik->mix, 0.0, 1e-6, "created runtime IK mix")) {
            std::cerr << "Constraint editor smoke did not rebuild the edited runtime constraint data.\n";
            ImGui::DestroyContext();
            return 1;
        }

        if (!set_selected_animation(&shell_state, "idle", "Smoke", false, true)) {
            std::cerr << "Constraint editor smoke could not refresh the idle preview after edits.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto& edited_path_a_world =
            shell_state.preview_skeleton->bone_world_transforms()[*path_a_index];
        const auto& edited_path_b_world =
            shell_state.preview_skeleton->bone_world_transforms()[*path_b_index];
        const auto& edited_path_c_world =
            shell_state.preview_skeleton->bone_world_transforms()[*path_c_index];
        if (!require_smoke_near(
                edited_path_a_world.world_x,
                0.0,
                kConstraintPreviewPositionEpsilon,
                "edited constraint preview path_a x") ||
            !require_smoke_near(
                edited_path_a_world.world_y,
                0.0,
                kConstraintPreviewPositionEpsilon,
                "edited constraint preview path_a y") ||
            !require_smoke_near(
                edited_path_b_world.world_x,
                60.0,
                kConstraintPreviewPositionEpsilon,
                "edited constraint preview path_b x") ||
            !require_smoke_near(
                edited_path_b_world.world_y,
                0.0,
                kConstraintPreviewPositionEpsilon,
                "edited constraint preview path_b y") ||
            !require_smoke_near(
                edited_path_c_world.world_x,
                100.0,
                kConstraintPreviewPositionEpsilon,
                "edited constraint preview path_c x") ||
            !require_smoke_near(
                edited_path_c_world.world_y,
                20.0,
                kConstraintPreviewPositionEpsilon,
                "edited constraint preview path_c y")) {
            std::cerr << "Constraint editor smoke did not re-preview the edited path constraint.\n";
            ImGui::DestroyContext();
            return 1;
        }

        const auto& edited_transform_target_world =
            shell_state.preview_skeleton->bone_world_transforms()[*transform_target_index];
        if (!require_smoke_near(
                edited_transform_target_world.world_x,
                200.0,
                kConstraintPreviewPositionEpsilon,
                "edited constraint preview transform target x") ||
            !require_smoke_near(
                edited_transform_target_world.world_y,
                25.0,
                kConstraintPreviewPositionEpsilon,
                "edited constraint preview transform target y")) {
            std::cerr << "Constraint editor smoke did not re-preview the edited transform constraint.\n";
            ImGui::DestroyContext();
            return 1;
        }

        shell_state.preview_skeleton->bone_poses()[*pivot_index].local_pose.rotation = 90.0;
        shell_state.preview_skeleton->update_world_transforms();
        const auto lagged_ribbon_tip =
            shell_state.preview_skeleton->bone_world_transforms()[*ribbon_tip_index];
        shell_state.preview_skeleton->update_physics(1.0 / 60.0);
        const auto stepped_ribbon_tip =
            shell_state.preview_skeleton->bone_world_transforms()[*ribbon_tip_index];
        const double preview_physics_motion = std::hypot(
            static_cast<double>(stepped_ribbon_tip.world_x - lagged_ribbon_tip.world_x),
            static_cast<double>(stepped_ribbon_tip.world_y - lagged_ribbon_tip.world_y));
        if (preview_physics_motion <= 0.1) {
            std::cerr << "Constraint editor smoke did not advance the preview physics chain.\n";
            ImGui::DestroyContext();
            return 1;
        }

        marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
        temp_project.source_path = "/tmp/marrow_editor_shell_constraints_smoke.marrow";
        materialize_temp_project_runtime_assets(shell_state, &temp_project);

        const auto save_result =
            marrow::editor::save_project(temp_project, temp_project.source_path);
        if (!save_result) {
            std::cerr << save_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }
        const auto export_result = marrow::editor::export_runtime_skeleton(
            *save_result.project,
            *shell_state.load_result.base_skeleton_document,
            "/tmp/marrow_editor_shell_constraints_smoke.mskl");
        if (!export_result) {
            std::cerr << export_result.error->format() << '\n';
            ImGui::DestroyContext();
            return 1;
        }

        const auto exported_skeleton =
            marrow::runtime::load_skeleton_data(export_result.path);
        if (!exported_skeleton) {
            std::cerr << exported_skeleton.error->format();
            ImGui::DestroyContext();
            return 1;
        }

        const auto* exported_created_ik = find_named_constraint(
            exported_skeleton.skeleton_data->ik_constraints(),
            created_ik_name);
        const auto* exported_path = find_named_constraint(
            exported_skeleton.skeleton_data->path_constraints(),
            "editor_guide_follow");
        const auto* exported_transform = find_named_constraint(
            exported_skeleton.skeleton_data->transform_constraints(),
            "editor_transform_follow");
        const auto* exported_physics = find_named_constraint(
            exported_skeleton.skeleton_data->physics_constraints(),
            "editor_ribbon_secondary");
        if (exported_created_ik == nullptr || exported_path == nullptr ||
            exported_transform == nullptr || exported_physics == nullptr ||
            exported_created_ik->bone_indices !=
                std::vector<std::size_t>{*transform_target_index} ||
            exported_created_ik->target_bone_index != *transform_source_index ||
            !require_smoke_near(
                exported_path->position,
                0.0,
                1e-6,
                "exported path position") ||
            !require_smoke_near(
                exported_transform->translate_mix,
                0.5,
                1e-6,
                "exported transform translate mix") ||
            !require_smoke_near(
                exported_physics->wind.x,
                18.0,
                1e-6,
                "exported physics wind.x")) {
            std::cerr << "Constraint editor smoke export did not round-trip the edited constraints.\n";
            ImGui::DestroyContext();
            return 1;
        }

        *shell_state.load_result.project = std::move(previous_project);
        if (!rebuild_project_runtime(&shell_state) ||
            !set_selected_animation(&shell_state, "idle", "Smoke", false, true)) {
            std::cerr << "Constraint editor smoke failed to restore the original project state.\n";
            ImGui::DestroyContext();
            return 1;
        }
        shell_state.project_dirty = false;
    }

    {
        if (const auto body_slot_index = shell_state.load_result.skeleton_data->find_slot_index("body")) {
            const auto warrior_skin_index =
                shell_state.load_result.skeleton_data->find_skin_index("warrior");
            const auto mage_skin_index =
                shell_state.load_result.skeleton_data->find_skin_index("mage");
            const auto transform_edit_index =
                ensure_transform_timeline_edit_index(&shell_state, *spine_track);
            const auto transform_constraint_edit_index =
                ensure_transform_constraint_edit_index(&shell_state, "editor_transform_follow");
            if (!warrior_skin_index.has_value() || !mage_skin_index.has_value() ||
                !transform_edit_index.has_value() ||
                !transform_constraint_edit_index.has_value()) {
                std::cerr << "Undo smoke could not resolve the baseline editor state.\n";
                ImGui::DestroyContext();
                return 1;
            }

            shell_state.command_stack.clear();
            shell_state.pending_edit_action.reset();
            shell_state.preview_skin_names = {"default"};
            shell_state.preview_slot_overrides.assign(
                shell_state.preview_slot_overrides.size(),
                std::nullopt);
            if (!apply_preview_skin_selection(&shell_state, "Smoke", false)) {
                std::cerr << "Undo smoke could not restore the default preview skin selection.\n";
                ImGui::DestroyContext();
                return 1;
            }
            select_slot(&shell_state, *body_slot_index, "Smoke", false);
            update_project_dirty_state(&shell_state);

            const EditorHistorySnapshot undo_smoke_baseline =
                capture_history_snapshot(shell_state);
            UndoStack merge_stack;
            {
                EditorHistorySnapshot merged_before = undo_smoke_baseline;
                EditorHistorySnapshot merged_mid = undo_smoke_baseline;
                merged_mid.preview_skin_names.push_back("merge-mid");
                EditorHistorySnapshot merged_after = merged_mid;
                merged_after.preview_skin_names.push_back("merge-after");
                merge_stack.push(make_edit_action(
                    EditActionKind::MoveBone,
                    "Merged drag step 1",
                    "smoke:merge",
                    true,
                    merged_before,
                    merged_mid));
                merge_stack.push(make_edit_action(
                    EditActionKind::MoveBone,
                    "Merged drag step 2",
                    "smoke:merge",
                    true,
                    merged_mid,
                    merged_after));
            }
            if (merge_stack.undo_count() != 1U || merge_stack.redo_count() != 0U ||
                merge_stack.peek_undo() == nullptr ||
                merge_stack.peek_undo()->kind() != EditActionKind::MoveBone) {
                std::cerr << "Undo smoke did not merge grouped drag actions into a single history entry.\n";
                ImGui::DestroyContext();
                return 1;
            }

            UndoStack depth_stack;
            for (std::size_t action_index = 0; action_index < 101U; ++action_index) {
                EditorHistorySnapshot before = undo_smoke_baseline;
                EditorHistorySnapshot after = undo_smoke_baseline;
                after.preview_skin_names.push_back(
                    "depth-" + std::to_string(action_index));
                depth_stack.push(make_edit_action(
                    EditActionKind::EditProperty,
                    "Depth smoke " + std::to_string(action_index),
                    "smoke:depth:" + std::to_string(action_index),
                    false,
                    before,
                    after));
            }
            if (depth_stack.undo_count() != 100U || depth_stack.redo_count() != 0U) {
                std::cerr << "Undo smoke did not cap the history depth at 100 actions.\n";
                ImGui::DestroyContext();
                return 1;
            }

            const auto undo_track_group = [&]() {
                return std::string("timeline:") + spine_track->id;
            };
            const auto undo_key_group = [&](std::size_t key_index) {
                return undo_track_group() + ":key:" + std::to_string(key_index);
            };

            auto& rotate_edit =
                shell_state.load_result.project->transform_timeline_edits[*transform_edit_index];
            const std::size_t baseline_key_count = rotate_edit.keyframes.size();
            if (const auto insert_time = insertable_key_time(
                    rotate_edit.keyframes,
                    0.625,
                    selected_animation_duration(shell_state))) {
                marrow::editor::ProjectData previous_project =
                    *shell_state.load_result.project;
                marrow::editor::TransformKeyframeEdit inserted_key =
                    sample_transform_keyframe(shell_state, *spine_track);
                inserted_key.time = *insert_time;
                inserted_key.angle = 11.0;
                inserted_key.interpolation = marrow::runtime::Interpolation::linear();
                const auto insert_iterator = std::upper_bound(
                    rotate_edit.keyframes.begin(),
                    rotate_edit.keyframes.end(),
                    inserted_key.time,
                    [](double time, const marrow::editor::TransformKeyframeEdit& keyframe) {
                        return time < keyframe.time;
                    });
                const std::size_t inserted_index = static_cast<std::size_t>(
                    std::distance(rotate_edit.keyframes.begin(), insert_iterator));
                rotate_edit.keyframes.insert(insert_iterator, inserted_key);
                if (!apply_project_command_change(
                        &shell_state,
                        previous_project,
                        EditActionKind::AddKeyframe,
                        "Added smoke undo key on spine",
                        undo_track_group(),
                        false,
                        "Undo smoke add-key action failed")) {
                    std::cerr << "Undo smoke could not record the add-key action.\n";
                    ImGui::DestroyContext();
                    return 1;
                }

                previous_project = *shell_state.load_result.project;
                shell_state.load_result.project->transform_timeline_edits[*transform_edit_index]
                    .keyframes[inserted_index]
                    .angle = 18.0;
                if (!apply_project_command_change(
                        &shell_state,
                        previous_project,
                        EditActionKind::MoveBone,
                        "Moved smoke undo key on spine",
                        undo_key_group(inserted_index),
                        false,
                        "Undo smoke move action failed")) {
                    std::cerr << "Undo smoke could not record the move-key action.\n";
                    ImGui::DestroyContext();
                    return 1;
                }

                previous_project = *shell_state.load_result.project;
                const double baseline_translate_mix =
                    shell_state.load_result.project
                        ->transform_constraint_edits[*transform_constraint_edit_index]
                        .translate_mix;
                shell_state.load_result.project
                    ->transform_constraint_edits[*transform_constraint_edit_index]
                    .translate_mix = 0.5;
                if (!apply_project_command_change(
                        &shell_state,
                        previous_project,
                        EditActionKind::EditProperty,
                        "Edited smoke constraint translate mix",
                        "constraint:Transform:editor_transform_follow",
                        true,
                        "Undo smoke constraint action failed")) {
                    std::cerr << "Undo smoke could not record the constraint edit action.\n";
                    ImGui::DestroyContext();
                    return 1;
                }

                if (!set_preview_skin_enabled(
                        &shell_state,
                        *warrior_skin_index,
                        true,
                        false,
                        true)) {
                    std::cerr << "Undo smoke could not record the preview skin action.\n";
                    ImGui::DestroyContext();
                    return 1;
                }

                const AttachmentSelection mage_attachment_selection{
                    *body_slot_index,
                    *mage_skin_index,
                    "mage_body"};
                if (!apply_attachment_selection_to_preview_slot(
                        &shell_state,
                        mage_attachment_selection,
                        "Smoke",
                        false,
                        true)) {
                    std::cerr << "Undo smoke could not record the preview attachment swap.\n";
                    ImGui::DestroyContext();
                    return 1;
                }

                previous_project = *shell_state.load_result.project;
                shell_state.load_result.project->transform_timeline_edits[*transform_edit_index]
                    .keyframes.erase(
                        shell_state.load_result.project->transform_timeline_edits[*transform_edit_index]
                            .keyframes.begin() +
                        static_cast<std::ptrdiff_t>(inserted_index));
                if (!apply_project_command_change(
                        &shell_state,
                        previous_project,
                        EditActionKind::RemoveKeyframe,
                        "Removed smoke undo key on spine",
                        undo_track_group(),
                        false,
                        "Undo smoke remove-key action failed")) {
                    std::cerr << "Undo smoke could not record the remove-key action.\n";
                    ImGui::DestroyContext();
                    return 1;
                }

                const EditorHistorySnapshot undo_smoke_final =
                    capture_history_snapshot(shell_state);
                const auto* edited_constraint = find_named_constraint(
                    shell_state.load_result.skeleton_data->transform_constraints(),
                    "editor_transform_follow");
                const auto* overridden_attachment =
                    shell_state.preview_skeleton->current_attachment(*body_slot_index);
                const bool warrior_enabled = std::find(
                    shell_state.preview_skin_names.begin(),
                    shell_state.preview_skin_names.end(),
                    "warrior") != shell_state.preview_skin_names.end();
                if (shell_state.command_stack.undo_count() != 6U ||
                    shell_state.command_stack.redo_count() != 0U ||
                    shell_state.load_result.project->transform_timeline_edits[*transform_edit_index]
                            .keyframes.size() != baseline_key_count ||
                    edited_constraint == nullptr ||
                    !require_smoke_near(
                        edited_constraint->translate_mix,
                        0.5,
                        1e-6,
                        "undo smoke constraint translate mix") ||
                    !warrior_enabled ||
                    !shell_state.preview_slot_overrides[*body_slot_index].has_value() ||
                    shell_state.preview_slot_overrides[*body_slot_index]->attachment_name !=
                        "mage_body" ||
                    overridden_attachment == nullptr ||
                    overridden_attachment->name != "mage_body") {
                    std::cerr << "Undo smoke did not preserve the final edited state before undo.\n";
                    ImGui::DestroyContext();
                    return 1;
                }

                for (int undo_index = 0; undo_index < 6; ++undo_index) {
                    if (!undo_project_change(&shell_state)) {
                        std::cerr << "Undo smoke failed while rewinding the editor history.\n";
                        ImGui::DestroyContext();
                        return 1;
                    }
                }
                if (shell_state.command_stack.undo_count() != 0U ||
                    shell_state.command_stack.redo_count() != 6U ||
                    !history_snapshots_equal(
                        capture_history_snapshot(shell_state),
                        undo_smoke_baseline) ||
                    shell_state.project_dirty) {
                    std::cerr << "Undo smoke did not restore the baseline project and preview state.\n";
                    ImGui::DestroyContext();
                    return 1;
                }

                for (int redo_index = 0; redo_index < 6; ++redo_index) {
                    if (!redo_project_change(&shell_state)) {
                        std::cerr << "Undo smoke failed while replaying the editor history.\n";
                        ImGui::DestroyContext();
                        return 1;
                    }
                }
                if (shell_state.command_stack.undo_count() != 6U ||
                    shell_state.command_stack.redo_count() != 0U ||
                    !history_snapshots_equal(
                        capture_history_snapshot(shell_state),
                        undo_smoke_final)) {
                    std::cerr << "Undo smoke did not reapply the recorded action sequence.\n";
                    ImGui::DestroyContext();
                    return 1;
                }

                if (!undo_project_change(&shell_state) ||
                    shell_state.command_stack.redo_count() != 1U) {
                    std::cerr << "Undo smoke could not prepare the redo-clear validation.\n";
                    ImGui::DestroyContext();
                    return 1;
                }

                previous_project = *shell_state.load_result.project;
                shell_state.load_result.project
                    ->transform_constraint_edits[*transform_constraint_edit_index]
                    .translate_mix = baseline_translate_mix;
                if (!apply_project_command_change(
                        &shell_state,
                        previous_project,
                        EditActionKind::EditProperty,
                        "Branched smoke constraint translate mix",
                        "constraint:Transform:editor_transform_follow",
                        true,
                        "Undo smoke redo-clear action failed")) {
                    std::cerr << "Undo smoke could not validate redo clearing after a new edit.\n";
                    ImGui::DestroyContext();
                    return 1;
                }
                if (shell_state.command_stack.redo_count() != 0U) {
                    std::cerr << "Undo smoke did not clear redo history after a new edit.\n";
                    ImGui::DestroyContext();
                    return 1;
                }
            } else {
                std::cerr << "Undo smoke could not find room for an inserted spine key.\n";
                ImGui::DestroyContext();
                return 1;
            }

            if (!apply_history_snapshot(&shell_state, undo_smoke_baseline)) {
                std::cerr << "Undo smoke failed to restore the baseline shell state.\n";
                ImGui::DestroyContext();
                return 1;
            }
            shell_state.command_stack.clear();
            shell_state.pending_edit_action.reset();
            update_project_dirty_state(&shell_state);
            if (shell_state.project_dirty) {
                std::cerr << "Undo smoke left the project marked dirty after baseline restore.\n";
                ImGui::DestroyContext();
                return 1;
            }
        } else {
            std::cerr << "Undo smoke could not resolve the body slot for preview attachment validation.\n";
            ImGui::DestroyContext();
            return 1;
        }
    }

    const int frame_count = options.auto_close_frames.value_or(1);
    bool validated_dock_layout = false;
    for (int frame_index = 0; frame_index < frame_count; ++frame_index) {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        (void)poll_runtime_asset_changes(&shell_state);
        advance_timeline_playback(&shell_state, io.DeltaTime);
        handle_project_history_shortcuts(&shell_state);

        bool reload_requested = false;
        draw_menu_bar(nullptr, &reload_requested, &shell_state);
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0U, main_viewport);
        ensure_default_dock_layout(&shell_state, dockspace_id, main_viewport);
        draw_project_window(&reload_requested, &shell_state);
        draw_runtime_window(shell_state);
        draw_constraints_window(&shell_state);
        draw_timeline_window(&shell_state);
        draw_hierarchy_window(&shell_state);
        draw_viewport_window(&shell_state);
        draw_inspector_window(&shell_state);

        if (!validated_dock_layout) {
            const ImGuiWindow* viewport_window = ImGui::FindWindowByName(kViewportWindowTitle);
            const ImGuiWindow* timeline_window = ImGui::FindWindowByName(kTimelineWindowTitle);
            const ImGuiWindow* hierarchy_window = ImGui::FindWindowByName(kHierarchyWindowTitle);
            const ImGuiWindow* properties_window = ImGui::FindWindowByName(kPropertiesWindowTitle);
            const ImGuiDockNode* viewport_node =
                ImGui::DockBuilderGetNode(shell_state.dock_layout.viewport_node_id);
            const ImGuiDockNode* timeline_node =
                ImGui::DockBuilderGetNode(shell_state.dock_layout.timeline_node_id);
            const ImGuiDockNode* hierarchy_node =
                ImGui::DockBuilderGetNode(shell_state.dock_layout.hierarchy_node_id);
            const ImGuiDockNode* properties_node =
                ImGui::DockBuilderGetNode(shell_state.dock_layout.properties_node_id);
            if (!shell_state.default_dock_layout_initialized ||
                viewport_window == nullptr ||
                timeline_window == nullptr ||
                hierarchy_window == nullptr ||
                properties_window == nullptr ||
                viewport_node == nullptr ||
                timeline_node == nullptr ||
                hierarchy_node == nullptr ||
                properties_node == nullptr ||
                viewport_window->DockId != shell_state.dock_layout.viewport_node_id ||
                timeline_window->DockId != shell_state.dock_layout.timeline_node_id ||
                hierarchy_window->DockId != shell_state.dock_layout.hierarchy_node_id ||
                properties_window->DockId != shell_state.dock_layout.properties_node_id ||
                !(viewport_node->Pos.x > hierarchy_node->Pos.x) ||
                !(timeline_node->Pos.y > viewport_node->Pos.y) ||
                !(properties_node->Pos.y > hierarchy_node->Pos.y) ||
                std::abs(properties_node->Pos.x - hierarchy_node->Pos.x) > 1e-3f) {
                std::cerr << "DockBuilder did not create the default Viewport/Timeline/Hierarchy/Properties layout.\n";
                ImGui::DestroyContext();
                return 1;
            }
            validated_dock_layout = true;
        }
        ImGui::Render();

        if (reload_requested && !reload_project(&shell_state)) {
            std::cerr << shell_state.error_message;
            ImGui::DestroyContext();
            return 1;
        }
    }

    std::cout << shell_state.status_message << '\n'
              << "Headless editor shell smoke rendered " << frame_count << " frame(s).\n";

    ImGui::DestroyContext();
    return 0;
}

} // namespace marrow::editor::shell
