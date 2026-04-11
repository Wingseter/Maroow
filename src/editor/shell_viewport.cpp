#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glcorearb.h>
#endif

#include "imgui.h"

#include "shell_types.hpp"
#include "viewport_renderer.hpp"
#include "marrow/allocator.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/renderer/module.hpp"
#include "marrow/runtime/animation_state.hpp"

namespace marrow::editor::shell {

std::optional<std::string> shader_log(GLuint shader) {
    GLint log_length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    if (log_length <= 1) {
        return std::nullopt;
    }

    std::string log(static_cast<std::size_t>(log_length), '\0');
    glGetShaderInfoLog(shader, log_length, nullptr, log.data());
    if (!log.empty() && log.back() == '\0') {
        log.pop_back();
    }
    return log;
}

std::optional<std::string> program_log(GLuint program) {
    GLint log_length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    if (log_length <= 1) {
        return std::nullopt;
    }

    std::string log(static_cast<std::size_t>(log_length), '\0');
    glGetProgramInfoLog(program, log_length, nullptr, log.data());
    if (!log.empty() && log.back() == '\0') {
        log.pop_back();
    }
    return log;
}

void destroy_viewport_framebuffer(ViewportRenderResources* resources) {
    if (resources->depth_stencil_renderbuffer != 0) {
        glDeleteRenderbuffers(1, &resources->depth_stencil_renderbuffer);
        resources->depth_stencil_renderbuffer = 0;
    }
    if (resources->color_texture != 0) {
        glDeleteTextures(1, &resources->color_texture);
        resources->color_texture = 0;
    }
    if (resources->framebuffer != 0) {
        glDeleteFramebuffers(1, &resources->framebuffer);
        resources->framebuffer = 0;
    }
    resources->framebuffer_width = 0;
    resources->framebuffer_height = 0;
}

std::optional<std::string> compile_viewport_shader(
    GLenum shader_type,
    const char* source,
    GLuint* shader_out) {
    GLuint shader = glCreateShader(shader_type);
    if (shader == 0) {
        return "Failed to allocate a viewport shader object.";
    }

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compile_status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
    if (compile_status == GL_TRUE) {
        *shader_out = shader;
        return std::nullopt;
    }

    std::string error = "Viewport shader compilation failed.";
    if (const auto log = shader_log(shader)) {
        error += " ";
        error += *log;
    }
    glDeleteShader(shader);
    return error;
}

std::optional<std::string> link_viewport_program(ViewportRenderResources* resources) {
    resources->program = glCreateProgram();
    if (resources->program == 0) {
        return "Failed to allocate the viewport shader program.";
    }

    glAttachShader(resources->program, resources->vertex_shader);
    glAttachShader(resources->program, resources->fragment_shader);
    glBindAttribLocation(resources->program, 0, "a_position");
    glBindAttribLocation(resources->program, 1, "a_color");
    glBindFragDataLocation(resources->program, 0, "frag_color");
    glLinkProgram(resources->program);

    GLint link_status = GL_FALSE;
    glGetProgramiv(resources->program, GL_LINK_STATUS, &link_status);
    if (link_status != GL_TRUE) {
        std::string error = "Viewport shader program link failed.";
        if (const auto log = program_log(resources->program)) {
            error += " ";
            error += *log;
        }
        return error;
    }

    resources->view_size_location =
        glGetUniformLocation(resources->program, "u_view_size");
    if (resources->view_size_location < 0) {
        return "Viewport shader program did not expose u_view_size.";
    }

    return std::nullopt;
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

    if (const auto error = compile_viewport_shader(
            GL_VERTEX_SHADER,
            kViewportVertexShaderSource,
            &resources->vertex_shader)) {
        resources->error_message = *error;
        destroy_viewport_renderer(resources);
        return error;
    }
    if (const auto error = compile_viewport_shader(
            GL_FRAGMENT_SHADER,
            kViewportFragmentShaderSource,
            &resources->fragment_shader)) {
        resources->error_message = *error;
        destroy_viewport_renderer(resources);
        return error;
    }
    if (const auto error = link_viewport_program(resources)) {
        resources->error_message = *error;
        destroy_viewport_renderer(resources);
        return error;
    }

    glGenVertexArrays(1, &resources->vao);
    glGenBuffers(1, &resources->vbo);
    if (resources->vao == 0 || resources->vbo == 0) {
        resources->error_message = "Failed to allocate viewport mesh buffers.";
        destroy_viewport_renderer(resources);
        return resources->error_message;
    }

    glBindVertexArray(resources->vao);
    glBindBuffer(GL_ARRAY_BUFFER, resources->vbo);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(ViewportRenderVertex),
        reinterpret_cast<const void*>(offsetof(ViewportRenderVertex, position_x)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(ViewportRenderVertex),
        reinterpret_cast<const void*>(offsetof(ViewportRenderVertex, color_r)));
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    if (const auto error = resources->prepared_scene_renderer.initialize()) {
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
    if (resources->vbo != 0) {
        glDeleteBuffers(1, &resources->vbo);
        resources->vbo = 0;
    }
    if (resources->vao != 0) {
        glDeleteVertexArrays(1, &resources->vao);
        resources->vao = 0;
    }
    if (resources->program != 0) {
        glDeleteProgram(resources->program);
        resources->program = 0;
    }
    if (resources->vertex_shader != 0) {
        glDeleteShader(resources->vertex_shader);
        resources->vertex_shader = 0;
    }
    if (resources->fragment_shader != 0) {
        glDeleteShader(resources->fragment_shader);
        resources->fragment_shader = 0;
    }
    resources->view_size_location = -1;
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
    if (resources->framebuffer != 0 &&
        resources->framebuffer_width == width &&
        resources->framebuffer_height == height) {
        return std::nullopt;
    }

    destroy_viewport_framebuffer(resources);

    glGenFramebuffers(1, &resources->framebuffer);
    glGenTextures(1, &resources->color_texture);
    if (resources->framebuffer == 0 || resources->color_texture == 0) {
        resources->error_message = "Failed to allocate viewport framebuffer resources.";
        destroy_viewport_framebuffer(resources);
        return resources->error_message;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, resources->framebuffer);
    glBindTexture(GL_TEXTURE_2D, resources->color_texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        width,
        height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        nullptr);
    glFramebufferTexture2D(
        GL_FRAMEBUFFER,
        GL_COLOR_ATTACHMENT0,
        GL_TEXTURE_2D,
        resources->color_texture,
        0);
    glGenRenderbuffers(1, &resources->depth_stencil_renderbuffer);
    if (resources->depth_stencil_renderbuffer == 0) {
        resources->error_message = "Failed to allocate viewport depth-stencil storage.";
        glBindTexture(GL_TEXTURE_2D, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        destroy_viewport_framebuffer(resources);
        return resources->error_message;
    }
    glBindRenderbuffer(GL_RENDERBUFFER, resources->depth_stencil_renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
    glFramebufferRenderbuffer(
        GL_FRAMEBUFFER,
        GL_DEPTH_STENCIL_ATTACHMENT,
        GL_RENDERBUFFER,
        resources->depth_stencil_renderbuffer);

    const GLenum framebuffer_status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (framebuffer_status != GL_FRAMEBUFFER_COMPLETE) {
        resources->error_message =
            "Viewport framebuffer is incomplete (status " +
            std::to_string(static_cast<unsigned int>(framebuffer_status)) + ").";
        destroy_viewport_framebuffer(resources);
        return resources->error_message;
    }

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

std::optional<ViewportLayout> build_viewport_layout(
    const ShellState& state,
    const ImVec2& canvas_origin,
    const ImVec2& canvas_size) {
    if (!state.load_result || !state.preview_skeleton) {
        return std::nullopt;
    }

    const auto& skeleton = *state.load_result.skeleton_data;
    const auto& world_transforms = state.preview_skeleton->bone_world_transforms();
    if (world_transforms.size() != skeleton.bones().size() || world_transforms.empty()) {
        return std::nullopt;
    }

    double min_x = std::numeric_limits<double>::max();
    double min_y = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    double max_y = std::numeric_limits<double>::lowest();
    bool has_active_bone = false;

    for (std::size_t bone_index = 0; bone_index < world_transforms.size(); ++bone_index) {
        if (!state.preview_skeleton->is_bone_active(bone_index)) {
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

    const double extent_x = std::max(max_x - min_x, 80.0);
    const double extent_y = std::max(max_y - min_y, 80.0);
    const float fit_width = std::max(32.0f, canvas_size.x - 96.0f);
    const float fit_height = std::max(32.0f, canvas_size.y - 96.0f);

    ViewportLayout layout;
    layout.canvas_origin = canvas_origin;
    layout.canvas_size = canvas_size;
    layout.canvas_end = ImVec2(canvas_origin.x + canvas_size.x, canvas_origin.y + canvas_size.y);
    layout.screen_center = ImVec2(
        canvas_origin.x + (canvas_size.x * 0.5f) + static_cast<float>(state.viewport.pan_x),
        canvas_origin.y + (canvas_size.y * 0.5f) + static_cast<float>(state.viewport.pan_y));
    layout.world_center_x = (min_x + max_x) * 0.5;
    layout.world_center_y = (min_y + max_y) * 0.5;
    layout.pixels_per_unit = std::max(
        0.25f,
        std::min(
            fit_width / static_cast<float>(extent_x),
            fit_height / static_cast<float>(extent_y))) *
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
    const auto& world_transforms = state.preview_skeleton->bone_world_transforms();
    if (world_transforms.size() != skeleton.bones().size()) {
        return overlay;
    }

    const auto slot_selected =
        [&](std::size_t slot_index) {
            return state.selected_slot_index.has_value() &&
                *state.selected_slot_index == slot_index;
        };
    const auto constraint_selected =
        [&](ConstraintEditKind kind, std::string_view name) {
            return state.selected_constraint.has_value() &&
                state.selected_constraint->kind == kind &&
                state.selected_constraint->name == name;
        };

    if (state.viewport.debug_overlay.ik_constraints) {
        for (const auto& constraint : skeleton.ik_constraints()) {
            if (constraint.bone_indices.empty() ||
                constraint.bone_indices.front() >= world_transforms.size() ||
                constraint.target_bone_index >= world_transforms.size()) {
                continue;
            }

            const bool selected = constraint_selected(ConstraintEditKind::Ik, constraint.name);
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

            const bool selected = constraint_selected(ConstraintEditKind::Path, constraint.name);
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
            const bool selected = constraint_selected(ConstraintEditKind::Physics, constraint.name);
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
    std::optional<std::size_t> selected_bone,
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
        const bool selected =
            selected_bone.has_value() && *selected_bone == node.bone_index;
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
        const bool selected =
            selected_bone.has_value() && *selected_bone == node.bone_index;
        const bool hovered_selection =
            hovered_bone.has_value() && *hovered_bone == node.bone_index;
        const float radius = joint_radius + (selected ? 2.0f : 0.0f);
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
        append_viewport_pose_geometry(
            layout,
            layout.bones,
            layout.render_joint_radius,
            state.selected_bone_index,
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
    if (resources->framebuffer == 0 || resources->color_texture == 0) {
        return "Viewport framebuffer has not been created.";
    }

    glBindFramebuffer(GL_FRAMEBUFFER, resources->framebuffer);
    glViewport(0, 0, resources->framebuffer_width, resources->framebuffer_height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    glUseProgram(resources->program);
    glUniform2f(
        resources->view_size_location,
        std::max(layout.canvas_size.x, 1.0f),
        std::max(layout.canvas_size.y, 1.0f));
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glBindVertexArray(resources->vao);
    glBindBuffer(GL_ARRAY_BUFFER, resources->vbo);

    const auto draw_vertices = [&](const std::vector<ViewportRenderVertex>& vertices, GLenum mode) {
        if (vertices.empty()) {
            return;
        }

        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(vertices.size() * sizeof(ViewportRenderVertex)),
            vertices.data(),
            GL_STREAM_DRAW);
        glDrawArrays(mode, 0, static_cast<GLsizei>(vertices.size()));
    };
    draw_vertices(background_geometry.line_vertices, GL_LINES);
    draw_vertices(background_geometry.triangle_vertices, GL_TRIANGLES);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);

    const std::array<float, 16> projection = viewport_projection_matrix(layout);
    for (const OnionSkinTexturedGhost& ghost : textured_ghosts) {
        if (const auto error = resources->prepared_scene_renderer.render_tinted(
                ghost.scene,
                atlas_image_path,
                projection,
                ghost.tint_color)) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return error;
        }
    }

    if (const auto error = resources->prepared_scene_renderer.render(
            scene,
            atlas_image_path,
            projection)) {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return error;
    }

    glUseProgram(resources->program);
    glUniform2f(
        resources->view_size_location,
        std::max(layout.canvas_size.x, 1.0f),
        std::max(layout.canvas_size.y, 1.0f));
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDisable(GL_STENCIL_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glBindVertexArray(resources->vao);
    glBindBuffer(GL_ARRAY_BUFFER, resources->vbo);
    draw_vertices(overlay_geometry.line_vertices, GL_LINES);
    draw_vertices(overlay_geometry.triangle_vertices, GL_TRIANGLES);

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return std::nullopt;
}

std::optional<std::string> render_viewport_framebuffer(
    const ShellState& state,
    const ViewportLayout& layout,
    const std::vector<OnionSkinGhostPose>& ghost_poses,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
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

    const marrow::renderer::PreparedSceneResult scene_result =
        marrow::renderer::prepare_setup_pose_scene(
            *state.preview_skeleton,
            *state.load_result.atlas_data.front());
    if (!scene_result) {
        return scene_result.error_message;
    }

    return render_prepared_scene_framebuffer(
        layout,
        background_geometry,
        overlay_geometry,
        textured_ghosts,
        *scene_result.scene,
        resolve_viewport_atlas_image_path(state, *scene_result.scene),
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

} // namespace marrow::editor::shell
