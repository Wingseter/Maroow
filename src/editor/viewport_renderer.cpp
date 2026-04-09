#include "viewport_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>
#include <vector>

namespace marrow::editor {

namespace {

constexpr std::size_t kBonePayloadFloatsPerEntry = 6U;
constexpr std::size_t kPackedBoneVec4Count = 192U;
constexpr std::array<std::uint8_t, 4> kWhiteTexel{{255, 255, 255, 255}};

constexpr const char* kSceneVertexShaderSource = R"(#version 150
uniform mat4 u_projection;
uniform vec4 u_bones[192];

in vec2 a_local_position0;
in vec2 a_local_position1;
in vec2 a_local_position2;
in vec2 a_local_position3;
in vec2 a_uv;
in vec4 a_bone_indices;
in vec4 a_bone_weights;
in vec4 a_light_color;
in vec4 a_dark_color;

out vec2 v_uv;
out vec4 v_light_color;
out vec4 v_dark_color;

vec2 marrow_transform_point(vec2 local_position, int bone_index) {
    int pair_index = bone_index / 2;
    int base_index = pair_index * 3;
    vec4 packed0 = u_bones[base_index + 0];
    vec4 packed1 = u_bones[base_index + 1];
    vec4 packed2 = u_bones[base_index + 2];

    vec4 matrix_values;
    vec2 translation;
    if ((bone_index & 1) == 0) {
        matrix_values = packed0;
        translation = packed1.xy;
    } else {
        matrix_values = vec4(packed1.zw, packed2.xy);
        translation = packed2.zw;
    }

    return vec2(
        (matrix_values.x * local_position.x) + (matrix_values.z * local_position.y) + translation.x,
        (matrix_values.y * local_position.x) + (matrix_values.w * local_position.y) + translation.y);
}

void main() {
    vec2 world_position =
        marrow_transform_point(a_local_position0, int(a_bone_indices.x)) * a_bone_weights.x +
        marrow_transform_point(a_local_position1, int(a_bone_indices.y)) * a_bone_weights.y +
        marrow_transform_point(a_local_position2, int(a_bone_indices.z)) * a_bone_weights.z +
        marrow_transform_point(a_local_position3, int(a_bone_indices.w)) * a_bone_weights.w;
    v_uv = a_uv;
    v_light_color = a_light_color;
    v_dark_color = a_dark_color;
    gl_Position = u_projection * vec4(world_position, 0.0, 1.0);
}
)";

constexpr const char* kSingleColorFragmentShaderSource = R"(#version 150
uniform sampler2D u_atlas_texture;
uniform float u_pma_control;

in vec2 v_uv;
in vec4 v_light_color;

out vec4 frag_color;

void main() {
    vec4 tex_color = texture(u_atlas_texture, v_uv);
    vec4 output_color = tex_color * v_light_color;
    if (u_pma_control < 0.5) {
        output_color.rgb *= v_light_color.a;
    }
    frag_color = output_color;
}
)";

constexpr const char* kTwoColorFragmentShaderSource = R"(#version 150
uniform sampler2D u_atlas_texture;
uniform float u_pma_control;

in vec2 v_uv;
in vec4 v_light_color;
in vec4 v_dark_color;

out vec4 frag_color;

void main() {
    vec4 tex_color = texture(u_atlas_texture, v_uv);
    vec3 tint_rgb =
        ((u_pma_control + ((1.0 - u_pma_control) * tex_color.a)) - tex_color.rgb) *
            v_dark_color.rgb +
        (tex_color.rgb * v_light_color.rgb);
    frag_color = vec4(tint_rgb, tex_color.a * v_light_color.a);
}
)";

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

std::optional<std::string> compile_shader(
    GLenum shader_type,
    const char* source,
    GLuint* shader_out) {
    GLuint shader = glCreateShader(shader_type);
    if (shader == 0) {
        return "Failed to allocate a viewport prepared-scene shader.";
    }

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compile_status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
    if (compile_status == GL_TRUE) {
        *shader_out = shader;
        return std::nullopt;
    }

    std::string error = "Viewport prepared-scene shader compilation failed.";
    if (const auto log = shader_log(shader)) {
        error += " ";
        error += *log;
    }
    glDeleteShader(shader);
    return error;
}

GLenum gl_blend_factor(renderer::BlendFactor factor) {
    switch (factor) {
    case renderer::BlendFactor::Zero:
        return GL_ZERO;
    case renderer::BlendFactor::One:
        return GL_ONE;
    case renderer::BlendFactor::SrcAlpha:
        return GL_SRC_ALPHA;
    case renderer::BlendFactor::OneMinusSrcAlpha:
        return GL_ONE_MINUS_SRC_ALPHA;
    case renderer::BlendFactor::DstColor:
        return GL_DST_COLOR;
    case renderer::BlendFactor::OneMinusSrcColor:
        return GL_ONE_MINUS_SRC_COLOR;
    }

    return GL_ZERO;
}

GLint gl_filter(std::string_view filter_name) {
    return filter_name == "nearest" ? GL_NEAREST : GL_LINEAR;
}

GLint gl_wrap(std::string_view wrap_name) {
    if (wrap_name == "repeat") {
        return GL_REPEAT;
    }
    if (wrap_name == "mirrored_repeat") {
        return GL_MIRRORED_REPEAT;
    }
    return GL_CLAMP_TO_EDGE;
}

float pma_uniform_value(bool premultiplied_alpha) {
    return premultiplied_alpha ? 0.0f : 1.0f;
}

std::array<float, kPackedBoneVec4Count * 4U> pack_bone_uniforms(
    const renderer::RenderCommandList& command_list) {
    std::array<float, kPackedBoneVec4Count * 4U> packed{};
    const std::size_t bone_count =
        command_list.bone_palette.size() / kBonePayloadFloatsPerEntry;
    for (std::size_t bone_index = 0; bone_index < bone_count; bone_index += 2U) {
        const std::size_t packed_index = (bone_index / 2U) * 3U;
        const std::size_t first_bone_offset = bone_index * kBonePayloadFloatsPerEntry;
        packed[(packed_index * 4U) + 0U] = command_list.bone_palette[first_bone_offset + 0U];
        packed[(packed_index * 4U) + 1U] = command_list.bone_palette[first_bone_offset + 1U];
        packed[(packed_index * 4U) + 2U] = command_list.bone_palette[first_bone_offset + 2U];
        packed[(packed_index * 4U) + 3U] = command_list.bone_palette[first_bone_offset + 3U];
        packed[((packed_index + 1U) * 4U) + 0U] =
            command_list.bone_palette[first_bone_offset + 4U];
        packed[((packed_index + 1U) * 4U) + 1U] =
            command_list.bone_palette[first_bone_offset + 5U];

        if (bone_index + 1U >= bone_count) {
            continue;
        }

        const std::size_t second_bone_offset =
            (bone_index + 1U) * kBonePayloadFloatsPerEntry;
        packed[((packed_index + 1U) * 4U) + 2U] =
            command_list.bone_palette[second_bone_offset + 0U];
        packed[((packed_index + 1U) * 4U) + 3U] =
            command_list.bone_palette[second_bone_offset + 1U];
        packed[((packed_index + 2U) * 4U) + 0U] =
            command_list.bone_palette[second_bone_offset + 2U];
        packed[((packed_index + 2U) * 4U) + 1U] =
            command_list.bone_palette[second_bone_offset + 3U];
        packed[((packed_index + 2U) * 4U) + 2U] =
            command_list.bone_palette[second_bone_offset + 4U];
        packed[((packed_index + 2U) * 4U) + 3U] =
            command_list.bone_palette[second_bone_offset + 5U];
    }
    return packed;
}

std::optional<ViewportRenderer::ActiveStencilClip> stencil_clip_for_depth(std::size_t depth) {
    if (depth == 0U || depth > 255U) {
        return std::nullopt;
    }

    const std::uint8_t reference_value = static_cast<std::uint8_t>(depth);
    const std::uint8_t parent_reference_value = static_cast<std::uint8_t>(depth - 1U);
    return ViewportRenderer::ActiveStencilClip{
        0U,
        reference_value,
        parent_reference_value,
        static_cast<std::uint8_t>(reference_value ^ parent_reference_value)};
}

} // namespace

std::optional<std::string> ViewportRenderer::initialize() {
    destroy();

    if (const auto error = initialize_programs()) {
        error_message_ = *error;
        destroy();
        return error;
    }

    glGenVertexArrays(1, &scene_vao_);
    glGenBuffers(1, &scene_vbo_);
    glGenBuffers(1, &scene_ebo_);
    if (scene_vao_ == 0 || scene_vbo_ == 0 || scene_ebo_ == 0) {
        error_message_ = "Failed to allocate viewport prepared-scene buffers.";
        destroy();
        return error_message_;
    }

    glBindVertexArray(scene_vao_);
    glBindBuffer(GL_ARRAY_BUFFER, scene_vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, scene_ebo_);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(renderer::RenderCommandVertex),
        reinterpret_cast<const void*>(offsetof(renderer::RenderCommandVertex, local_positions)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(renderer::RenderCommandVertex),
        reinterpret_cast<const void*>(
            offsetof(renderer::RenderCommandVertex, local_positions) + (sizeof(float) * 2U)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(
        2,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(renderer::RenderCommandVertex),
        reinterpret_cast<const void*>(
            offsetof(renderer::RenderCommandVertex, local_positions) + (sizeof(float) * 4U)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(
        3,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(renderer::RenderCommandVertex),
        reinterpret_cast<const void*>(
            offsetof(renderer::RenderCommandVertex, local_positions) + (sizeof(float) * 6U)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(
        4,
        2,
        GL_FLOAT,
        GL_FALSE,
        sizeof(renderer::RenderCommandVertex),
        reinterpret_cast<const void*>(offsetof(renderer::RenderCommandVertex, uv)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(
        5,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(renderer::RenderCommandVertex),
        reinterpret_cast<const void*>(offsetof(renderer::RenderCommandVertex, bone_indices)));
    glEnableVertexAttribArray(6);
    glVertexAttribPointer(
        6,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(renderer::RenderCommandVertex),
        reinterpret_cast<const void*>(offsetof(renderer::RenderCommandVertex, bone_weights)));
    glEnableVertexAttribArray(7);
    glVertexAttribPointer(
        7,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(renderer::RenderCommandVertex),
        reinterpret_cast<const void*>(offsetof(renderer::RenderCommandVertex, light_color)));
    glEnableVertexAttribArray(8);
    glVertexAttribPointer(
        8,
        4,
        GL_UNSIGNED_BYTE,
        GL_TRUE,
        sizeof(renderer::RenderCommandVertex),
        reinterpret_cast<const void*>(offsetof(renderer::RenderCommandVertex, dark_color)));

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    if (const auto error = ensure_textures()) {
        error_message_ = *error;
        destroy();
        return error;
    }

    available_ = true;
    error_message_.clear();
    return std::nullopt;
}

void ViewportRenderer::destroy() {
    destroy_textures();
    if (scene_ebo_ != 0) {
        glDeleteBuffers(1, &scene_ebo_);
        scene_ebo_ = 0;
    }
    if (scene_vbo_ != 0) {
        glDeleteBuffers(1, &scene_vbo_);
        scene_vbo_ = 0;
    }
    if (scene_vao_ != 0) {
        glDeleteVertexArrays(1, &scene_vao_);
        scene_vao_ = 0;
    }
    destroy_program(&single_color_program_);
    destroy_program(&two_color_program_);
    available_ = false;
}

std::optional<std::string> ViewportRenderer::render(
    const renderer::PreparedScene& scene,
    const std::filesystem::path& atlas_image_path,
    const std::array<float, 16>& projection) {
    if (!available_) {
        return error_message_.empty()
            ? std::optional<std::string>("Viewport prepared-scene renderer is unavailable.")
            : std::optional<std::string>(error_message_);
    }

    const renderer::RenderCommandListResult command_list_result =
        renderer::build_render_command_list(scene, projection);
    if (!command_list_result) {
        error_message_ = command_list_result.error_message;
        return command_list_result.error_message;
    }

    if (const auto error = ensure_atlas_texture(scene, atlas_image_path)) {
        error_message_ = *error;
        return error;
    }

    if (const auto error = submit_command_list(*command_list_result.command_list)) {
        error_message_ = *error;
        return error;
    }

    error_message_.clear();
    return std::nullopt;
}

bool ViewportRenderer::available() const {
    return available_;
}

const std::string& ViewportRenderer::error_message() const {
    return error_message_;
}

std::optional<std::string> ViewportRenderer::initialize_programs() {
    if (const auto error = create_program(
            kSceneVertexShaderSource,
            kSingleColorFragmentShaderSource,
            &single_color_program_)) {
        return error;
    }
    if (const auto error = create_program(
            kSceneVertexShaderSource,
            kTwoColorFragmentShaderSource,
            &two_color_program_)) {
        return error;
    }
    return std::nullopt;
}

void ViewportRenderer::destroy_program(ProgramHandles* program) {
    if (program == nullptr) {
        return;
    }
    if (program->program != 0) {
        glDeleteProgram(program->program);
        program->program = 0;
    }
    if (program->vertex_shader != 0) {
        glDeleteShader(program->vertex_shader);
        program->vertex_shader = 0;
    }
    if (program->fragment_shader != 0) {
        glDeleteShader(program->fragment_shader);
        program->fragment_shader = 0;
    }
    program->projection_location = -1;
    program->bones_location = -1;
    program->atlas_texture_location = -1;
    program->pma_control_location = -1;
}

std::optional<std::string> ViewportRenderer::create_program(
    const char* vertex_source,
    const char* fragment_source,
    ProgramHandles* program_out) {
    if (program_out == nullptr) {
        return "Viewport prepared-scene program output must not be null.";
    }

    if (const auto error =
            compile_shader(GL_VERTEX_SHADER, vertex_source, &program_out->vertex_shader)) {
        return error;
    }
    if (const auto error =
            compile_shader(GL_FRAGMENT_SHADER, fragment_source, &program_out->fragment_shader)) {
        return error;
    }

    program_out->program = glCreateProgram();
    if (program_out->program == 0) {
        return "Failed to allocate a viewport prepared-scene program.";
    }

    glAttachShader(program_out->program, program_out->vertex_shader);
    glAttachShader(program_out->program, program_out->fragment_shader);
    glBindAttribLocation(program_out->program, 0, "a_local_position0");
    glBindAttribLocation(program_out->program, 1, "a_local_position1");
    glBindAttribLocation(program_out->program, 2, "a_local_position2");
    glBindAttribLocation(program_out->program, 3, "a_local_position3");
    glBindAttribLocation(program_out->program, 4, "a_uv");
    glBindAttribLocation(program_out->program, 5, "a_bone_indices");
    glBindAttribLocation(program_out->program, 6, "a_bone_weights");
    glBindAttribLocation(program_out->program, 7, "a_light_color");
    glBindAttribLocation(program_out->program, 8, "a_dark_color");
    glBindFragDataLocation(program_out->program, 0, "frag_color");
    glLinkProgram(program_out->program);

    GLint link_status = GL_FALSE;
    glGetProgramiv(program_out->program, GL_LINK_STATUS, &link_status);
    if (link_status != GL_TRUE) {
        std::string error = "Viewport prepared-scene program link failed.";
        if (const auto log = program_log(program_out->program)) {
            error += " ";
            error += *log;
        }
        return error;
    }

    program_out->projection_location =
        glGetUniformLocation(program_out->program, "u_projection");
    program_out->bones_location =
        glGetUniformLocation(program_out->program, "u_bones");
    program_out->atlas_texture_location =
        glGetUniformLocation(program_out->program, "u_atlas_texture");
    program_out->pma_control_location =
        glGetUniformLocation(program_out->program, "u_pma_control");
    if (program_out->projection_location < 0 ||
        program_out->bones_location < 0 ||
        program_out->atlas_texture_location < 0 ||
        program_out->pma_control_location < 0) {
        return "Viewport prepared-scene shader uniforms were not available.";
    }

    return std::nullopt;
}

std::optional<std::string> ViewportRenderer::ensure_textures() {
    if (white_texture_ != 0) {
        return std::nullopt;
    }

    glGenTextures(1, &white_texture_);
    if (white_texture_ == 0) {
        return "Failed to allocate the viewport prepared-scene white texture.";
    }

    glBindTexture(GL_TEXTURE_2D, white_texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        1,
        1,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        kWhiteTexel.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    glGenTextures(1, &atlas_texture_);
    if (atlas_texture_ == 0) {
        return "Failed to allocate the viewport prepared-scene atlas texture.";
    }

    return std::nullopt;
}

void ViewportRenderer::destroy_textures() {
    if (atlas_texture_ != 0) {
        glDeleteTextures(1, &atlas_texture_);
        atlas_texture_ = 0;
    }
    if (white_texture_ != 0) {
        glDeleteTextures(1, &white_texture_);
        white_texture_ = 0;
    }
    atlas_texture_path_.clear();
    atlas_texture_write_time_.reset();
    atlas_filter_min_.clear();
    atlas_filter_mag_.clear();
    atlas_wrap_x_.clear();
    atlas_wrap_y_.clear();
    atlas_premultiplied_alpha_ = false;
}

std::optional<std::string> ViewportRenderer::ensure_atlas_texture(
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

    if (atlas_texture_ != 0 &&
        atlas_texture_path_ == atlas_image_path &&
        atlas_texture_write_time_ == write_time &&
        atlas_filter_min_ == scene.atlas_filter_min &&
        atlas_filter_mag_ == scene.atlas_filter_mag &&
        atlas_wrap_x_ == scene.atlas_wrap_x &&
        atlas_wrap_y_ == scene.atlas_wrap_y &&
        atlas_premultiplied_alpha_ == scene.premultiplied_alpha) {
        return std::nullopt;
    }

    atlas_texture_write_time_ = write_time;
    return upload_atlas_texture(scene, atlas_image_path);
}

std::optional<std::string> ViewportRenderer::upload_atlas_texture(
    const renderer::PreparedScene& scene,
    const std::filesystem::path& atlas_image_path) {
    const renderer::TextureImageLoadResult texture_result =
        renderer::load_png_texture_or_white(atlas_image_path);
    const renderer::TextureImage& image = texture_result.image;
    if (image.width <= 0 || image.height <= 0 || image.rgba8.empty()) {
        return "Viewport prepared-scene atlas image data was invalid.";
    }

    glBindTexture(GL_TEXTURE_2D, atlas_texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_filter(scene.atlas_filter_min));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_filter(scene.atlas_filter_mag));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_wrap(scene.atlas_wrap_x));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_wrap(scene.atlas_wrap_y));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA8,
        image.width,
        image.height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        image.rgba8.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    atlas_texture_path_ = atlas_image_path;
    atlas_filter_min_ = scene.atlas_filter_min;
    atlas_filter_mag_ = scene.atlas_filter_mag;
    atlas_wrap_x_ = scene.atlas_wrap_x;
    atlas_wrap_y_ = scene.atlas_wrap_y;
    atlas_premultiplied_alpha_ = scene.premultiplied_alpha;
    if (!texture_result.loaded_from_file && !texture_result.message.empty()) {
        error_message_ = texture_result.message;
    }
    return std::nullopt;
}

std::optional<std::string> ViewportRenderer::submit_command_list(
    const renderer::RenderCommandList& command_list) {
    if ((command_list.bone_palette.size() % kBonePayloadFloatsPerEntry) != 0U) {
        return "Viewport render command list contained an invalid bone palette.";
    }

    glBindVertexArray(scene_vao_);
    glActiveTexture(GL_TEXTURE0);
    const std::array<float, kPackedBoneVec4Count * 4U> packed_bones =
        pack_bone_uniforms(command_list);

    std::vector<ActiveStencilClip> active_clips;
    for (const renderer::RenderCommandEventRef& event : command_list.ordered_events) {
        switch (event.kind) {
        case renderer::RenderCommandEventKind::ClipStart: {
            if (event.index >= command_list.clip_commands.size()) {
                glBindVertexArray(0);
                return "Viewport clip start event referenced a missing clip command.";
            }

            const auto clip_state = stencil_clip_for_depth(active_clips.size() + 1U);
            if (!clip_state.has_value()) {
                glBindVertexArray(0);
                return "Viewport clip nesting exceeded the stencil reference range.";
            }

            ActiveStencilClip active_clip = *clip_state;
            active_clip.clip_attachment_index = event.index;
            active_clips.push_back(active_clip);
            if (const auto error = submit_clip_command(
                    command_list.clip_commands[event.index],
                    command_list,
                    active_clips.back(),
                    false,
                    packed_bones)) {
                glBindVertexArray(0);
                return error;
            }
            break;
        }
        case renderer::RenderCommandEventKind::Draw: {
            if (event.index >= command_list.commands.size()) {
                glBindVertexArray(0);
                return "Viewport draw event referenced a missing draw command.";
            }

            const std::optional<std::uint8_t> stencil_reference =
                active_clips.empty()
                    ? std::nullopt
                    : std::optional<std::uint8_t>{active_clips.back().reference_value};
            if (const auto error = submit_draw_command(
                    command_list.commands[event.index],
                    command_list,
                    stencil_reference,
                    packed_bones)) {
                glBindVertexArray(0);
                return error;
            }
            break;
        }
        case renderer::RenderCommandEventKind::ClipEnd: {
            if (event.index >= command_list.clip_commands.size()) {
                glBindVertexArray(0);
                return "Viewport clip end event referenced a missing clip command.";
            }
            if (active_clips.empty()) {
                glBindVertexArray(0);
                return "Viewport clip end event underflowed the active clip stack.";
            }

            const ActiveStencilClip active_clip = active_clips.back();
            active_clips.pop_back();
            if (active_clip.clip_attachment_index != event.index) {
                glBindVertexArray(0);
                return "Viewport clip end event did not match the active clip stack.";
            }
            if (const auto error = submit_clip_command(
                    command_list.clip_commands[event.index],
                    command_list,
                    active_clip,
                    true,
                    packed_bones)) {
                glBindVertexArray(0);
                return error;
            }
            break;
        }
        }
    }

    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
    if (!active_clips.empty()) {
        return "Viewport render finished with an unterminated clip stack.";
    }
    return std::nullopt;
}

std::optional<std::string> ViewportRenderer::submit_draw_command(
    const renderer::RenderCommand& command,
    const renderer::RenderCommandList& command_list,
    std::optional<std::uint8_t> stencil_reference,
    const std::array<float, kPackedBoneVec4Count * 4U>& packed_bones) {
    if (command.vertices.empty() || command.indices.empty()) {
        return std::nullopt;
    }
    if (command.texture_handle != renderer::kAtlasTextureHandle &&
        command.texture_handle != renderer::kSolidWhiteTextureHandle) {
        return "Viewport draw command referenced an unknown texture handle.";
    }

    if (const auto error = upload_geometry(command.vertices, command.indices)) {
        return error;
    }

    const renderer::BlendState blend_state =
        renderer::blend_state_for(command.blend_mode, command_list.premultiplied_alpha);
    configure_draw_state(
        command.shader_variant,
        blend_state,
        command_list.premultiplied_alpha,
        stencil_reference,
        command_list,
        packed_bones);
    glBindTexture(
        GL_TEXTURE_2D,
        command.texture_handle == renderer::kSolidWhiteTextureHandle
            ? white_texture_
            : atlas_texture_);
    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(command.indices.size()),
        GL_UNSIGNED_INT,
        nullptr);
    return std::nullopt;
}

std::optional<std::string> ViewportRenderer::submit_clip_command(
    const renderer::RenderClipCommand& clip_command,
    const renderer::RenderCommandList& command_list,
    const ActiveStencilClip& clip,
    bool restoring_parent_reference,
    const std::array<float, kPackedBoneVec4Count * 4U>& packed_bones) {
    if (clip_command.vertices.empty() || clip_command.indices.empty()) {
        return std::nullopt;
    }

    if (const auto error = upload_geometry(clip_command.vertices, clip_command.indices)) {
        return error;
    }

    configure_clip_state(clip, restoring_parent_reference, command_list, packed_bones);
    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(clip_command.indices.size()),
        GL_UNSIGNED_INT,
        nullptr);
    return std::nullopt;
}

std::optional<std::string> ViewportRenderer::upload_geometry(
    const std::vector<renderer::RenderCommandVertex>& vertices,
    const std::vector<std::uint32_t>& indices) {
    glBindBuffer(GL_ARRAY_BUFFER, scene_vbo_);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, scene_ebo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(vertices.size() * sizeof(renderer::RenderCommandVertex)),
        vertices.data(),
        GL_STREAM_DRAW);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(indices.size() * sizeof(std::uint32_t)),
        indices.data(),
        GL_STREAM_DRAW);
    return std::nullopt;
}

void ViewportRenderer::configure_draw_state(
    renderer::ColorShaderVariant shader_variant,
    renderer::BlendState blend_state,
    bool premultiplied_alpha,
    std::optional<std::uint8_t> stencil_reference,
    const renderer::RenderCommandList& command_list,
    const std::array<float, kPackedBoneVec4Count * 4U>& packed_bones) {
    const ProgramHandles& program =
        shader_variant == renderer::ColorShaderVariant::SingleColor
            ? single_color_program_
            : two_color_program_;

    glUseProgram(program.program);
    glUniformMatrix4fv(
        program.projection_location,
        1,
        GL_FALSE,
        command_list.projection.data());
    glUniform4fv(
        program.bones_location,
        static_cast<GLsizei>(kPackedBoneVec4Count),
        packed_bones.data());
    glUniform1i(program.atlas_texture_location, 0);
    glUniform1f(program.pma_control_location, pma_uniform_value(premultiplied_alpha));

    glEnable(GL_BLEND);
    glBlendFunc(
        gl_blend_factor(blend_state.src_factor),
        gl_blend_factor(blend_state.dst_factor));
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    if (stencil_reference.has_value()) {
        glEnable(GL_STENCIL_TEST);
        glStencilMask(0x00U);
        glStencilFunc(GL_EQUAL, *stencil_reference, 0xFFU);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    } else {
        glDisable(GL_STENCIL_TEST);
    }
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
}

void ViewportRenderer::configure_clip_state(
    const ActiveStencilClip& clip,
    bool restoring_parent_reference,
    const renderer::RenderCommandList& command_list,
    const std::array<float, kPackedBoneVec4Count * 4U>& packed_bones) {
    glUseProgram(single_color_program_.program);
    glUniformMatrix4fv(
        single_color_program_.projection_location,
        1,
        GL_FALSE,
        command_list.projection.data());
    glUniform4fv(
        single_color_program_.bones_location,
        static_cast<GLsizei>(kPackedBoneVec4Count),
        packed_bones.data());
    glUniform1i(single_color_program_.atlas_texture_location, 0);
    glUniform1f(single_color_program_.pma_control_location, 1.0f);

    glBindTexture(GL_TEXTURE_2D, white_texture_);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_STENCIL_TEST);
    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glStencilMask(clip.invert_mask);
    glStencilFunc(
        GL_EQUAL,
        restoring_parent_reference ? clip.reference_value : clip.parent_reference_value,
        0xFFU);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
}

} // namespace marrow::editor
