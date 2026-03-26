#include "marrow/renderer/module.hpp"

#include "module_internal.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
#include <OpenGL/gl3.h>
#else
#include <GL/glcorearb.h>
#endif

#include <zlib.h>

namespace marrow::renderer {

namespace {

constexpr std::array<std::uint32_t, 6> kQuadIndices{{0, 1, 2, 0, 2, 3}};
constexpr std::array<std::uint8_t, 4> kWhiteTexel{{255, 255, 255, 255}};
constexpr std::size_t kStreamingVertexBufferBytes = 2U * 1024U * 1024U;
constexpr std::size_t kStreamingIndexBufferBytes = 512U * 1024U;
constexpr std::size_t kClipVertexBufferBytes = 8U * 1024U;
constexpr std::size_t kIdentityBoneCount = 1U;
constexpr std::size_t kMaxRendererBoneUniforms = 128U;

constexpr const char* kSingleColorVertexShaderSource = R"(#version 150
in vec2 a_local_position0;
in vec2 a_local_position1;
in vec2 a_local_position2;
in vec2 a_local_position3;
in vec2 a_uv;
in vec4 a_bone_indices;
in vec4 a_bone_weights;
in vec4 a_light_color;

uniform mat4 u_projection;
uniform mat3x2 u_bones[128];

out vec2 v_uv;
out vec4 v_light_color;

void main() {
    vec2 world_position =
        (u_bones[int(a_bone_indices.x)] * vec3(a_local_position0, 1.0)) * a_bone_weights.x +
        (u_bones[int(a_bone_indices.y)] * vec3(a_local_position1, 1.0)) * a_bone_weights.y +
        (u_bones[int(a_bone_indices.z)] * vec3(a_local_position2, 1.0)) * a_bone_weights.z +
        (u_bones[int(a_bone_indices.w)] * vec3(a_local_position3, 1.0)) * a_bone_weights.w;
    v_uv = a_uv;
    v_light_color = a_light_color;
    gl_Position = u_projection * vec4(world_position, 0.0, 1.0);
}
)";

constexpr const char* kTwoColorTintVertexShaderSource = R"(#version 150
in vec2 a_local_position0;
in vec2 a_local_position1;
in vec2 a_local_position2;
in vec2 a_local_position3;
in vec2 a_uv;
in vec4 a_bone_indices;
in vec4 a_bone_weights;
in vec4 a_light_color;
in vec4 a_dark_color;

uniform mat4 u_projection;
uniform mat3x2 u_bones[128];

out vec2 v_uv;
out vec4 v_light_color;
out vec4 v_dark_color;

void main() {
    vec2 world_position =
        (u_bones[int(a_bone_indices.x)] * vec3(a_local_position0, 1.0)) * a_bone_weights.x +
        (u_bones[int(a_bone_indices.y)] * vec3(a_local_position1, 1.0)) * a_bone_weights.y +
        (u_bones[int(a_bone_indices.z)] * vec3(a_local_position2, 1.0)) * a_bone_weights.z +
        (u_bones[int(a_bone_indices.w)] * vec3(a_local_position3, 1.0)) * a_bone_weights.w;
    v_uv = a_uv;
    v_light_color = a_light_color;
    v_dark_color = a_dark_color;
    gl_Position = u_projection * vec4(world_position, 0.0, 1.0);
}
)";

constexpr GLuint kLocalPosition0AttributeLocation = 0;
constexpr GLuint kLocalPosition1AttributeLocation = 1;
constexpr GLuint kLocalPosition2AttributeLocation = 2;
constexpr GLuint kLocalPosition3AttributeLocation = 3;
constexpr GLuint kUvAttributeLocation = 4;
constexpr GLuint kBoneIndicesAttributeLocation = 5;
constexpr GLuint kBoneWeightsAttributeLocation = 6;
constexpr GLuint kLightColorAttributeLocation = 7;
constexpr GLuint kDarkColorAttributeLocation = 8;

constexpr const char* kSingleColorFragmentShaderSource = R"(#version 150
in vec2 v_uv;
in vec4 v_light_color;

uniform sampler2D u_texture;
uniform float u_pma;

out vec4 frag_color;

void main() {
    vec4 tex_color = texture(u_texture, v_uv);
    vec4 output_color = tex_color * v_light_color;
    if (u_pma < 0.5) {
        output_color.rgb *= v_light_color.a;
    }
    frag_color = output_color;
}
)";

constexpr const char* kTwoColorTintFragmentShaderSource = R"(#version 150
in vec2 v_uv;
in vec4 v_light_color;
in vec4 v_dark_color;

uniform sampler2D u_texture;
uniform float u_pma;

out vec4 frag_color;

void main() {
    vec4 tex_color = texture(u_texture, v_uv);
    vec3 tint_rgb =
        ((u_pma + ((1.0 - u_pma) * tex_color.a)) - tex_color.rgb) * v_dark_color.rgb +
        (tex_color.rgb * v_light_color.rgb);
    frag_color = vec4(tint_rgb, tex_color.a * v_light_color.a);
}
)";

struct SceneBounds {
    double min_x{0.0};
    double min_y{0.0};
    double max_x{0.0};
    double max_y{0.0};
};

struct GlStreamVertex {
    float local_position[4][2];
    float uv[2];
    float bone_indices[4];
    float bone_weights[4];
    float light_color[4];
    std::uint8_t dark_color[4];
};

struct GlShaderProgram {
    GLuint program{0};
    GLuint vertex_shader{0};
    GLuint fragment_shader{0};
    GLint projection_location{-1};
    GLint bone_palette_location{-1};
    GLint texture_location{-1};
    GLint pma_location{-1};
};

struct GlRenderResources {
    GlShaderProgram single_color_program;
    GlShaderProgram two_color_program;
    GLuint vao{0};
    GLuint vbo{0};
    GLuint ebo{0};
    GLuint clip_vao{0};
    GLuint clip_vbo{0};
    GLuint texture{0};
    std::size_t vbo_capacity_bytes{0};
    std::size_t ebo_capacity_bytes{0};
    std::size_t clip_vbo_capacity_bytes{0};
};

struct PreparedStreamBatch {
    std::string texture_name;
    runtime::BlendMode blend_mode{runtime::BlendMode::Normal};
    ColorShaderVariant shader_variant{ColorShaderVariant::SingleColor};
    std::size_t draw_command_offset{0};
    std::size_t draw_command_count{0};
    std::size_t vertex_offset{0};
    std::size_t vertex_count{0};
    std::size_t index_offset{0};
    std::size_t index_count{0};
};

struct StreamBuildResult {
    std::vector<GlStreamVertex> vertices;
    std::vector<std::uint32_t> indices;
    std::vector<PreparedStreamBatch> batches;
    std::optional<std::string> error_message;

    explicit operator bool() const {
        return !error_message.has_value();
    }
};

enum class GeometryClipMode {
    UsePreparedMaskedGeometry,
    UseOriginalGeometry,
};

struct DrawBatchState {
    std::vector<const PreparedDrawCommand*> commands;
    std::string texture_name;
    runtime::BlendMode blend_mode{runtime::BlendMode::Normal};
    ColorShaderVariant shader_variant{ColorShaderVariant::SingleColor};
    std::uint8_t stencil_reference{0};
    bool stencil_enabled{false};
};

struct ActiveStencilClip {
    std::size_t clip_attachment_index{0};
    std::uint8_t reference_value{0};
    std::uint8_t parent_reference_value{0};
    std::uint8_t invert_mask{0};
};

TextureImage fallback_white_texture() {
    TextureImage image;
    image.width = 1;
    image.height = 1;
    image.rgba8.assign(kWhiteTexel.begin(), kWhiteTexel.end());
    return image;
}

TextureImageLoadResult fallback_texture_result(
    const std::filesystem::path& image_path,
    std::string message) {
    TextureImageLoadResult result;
    result.image = fallback_white_texture();
    result.loaded_from_file = false;
    result.message = image_path.empty()
        ? std::move(message)
        : std::move(message) + " Falling back to a 1x1 white texture for " +
            image_path.string() + ".";
    return result;
}

bool checked_multiply(std::size_t lhs, std::size_t rhs, std::size_t* product_out) {
    if (lhs != 0 && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
        return false;
    }

    *product_out = lhs * rhs;
    return true;
}

bool checked_add(std::size_t lhs, std::size_t rhs, std::size_t* sum_out) {
    if (rhs > std::numeric_limits<std::size_t>::max() - lhs) {
        return false;
    }

    *sum_out = lhs + rhs;
    return true;
}

std::uint32_t read_big_endian_u32(const std::uint8_t* bytes) {
    return
        (static_cast<std::uint32_t>(bytes[0]) << 24) |
        (static_cast<std::uint32_t>(bytes[1]) << 16) |
        (static_cast<std::uint32_t>(bytes[2]) << 8) |
        static_cast<std::uint32_t>(bytes[3]);
}

std::optional<std::string> read_binary_file(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>* bytes_out) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return "Failed to open atlas image file.";
    }

    input.seekg(0, std::ios::end);
    const std::streamoff file_size = input.tellg();
    if (file_size < 0) {
        return "Failed to determine atlas image size.";
    }

    bytes_out->resize(static_cast<std::size_t>(file_size));
    input.seekg(0, std::ios::beg);
    if (!bytes_out->empty()) {
        input.read(reinterpret_cast<char*>(bytes_out->data()), file_size);
    }
    if (!input) {
        return "Failed to read atlas image bytes.";
    }

    return std::nullopt;
}

int png_channel_count(std::uint8_t color_type) {
    switch (color_type) {
    case 2:
        return 3;
    case 6:
        return 4;
    default:
        return 0;
    }
}

std::uint8_t paeth_predictor(std::uint8_t left, std::uint8_t up, std::uint8_t up_left) {
    const int base = static_cast<int>(left) + static_cast<int>(up) - static_cast<int>(up_left);
    const int left_distance = std::abs(base - static_cast<int>(left));
    const int up_distance = std::abs(base - static_cast<int>(up));
    const int up_left_distance = std::abs(base - static_cast<int>(up_left));
    if (left_distance <= up_distance && left_distance <= up_left_distance) {
        return left;
    }
    if (up_distance <= up_left_distance) {
        return up;
    }
    return up_left;
}

std::optional<std::string> decode_png_rgba8(
    const std::vector<std::uint8_t>& png_bytes,
    TextureImage* image_out) {
    constexpr std::array<std::uint8_t, 8> kPngSignature{{
        137, 80, 78, 71, 13, 10, 26, 10,
    }};
    if (png_bytes.size() < kPngSignature.size() ||
        !std::equal(kPngSignature.begin(), kPngSignature.end(), png_bytes.begin())) {
        return "Atlas image does not contain a valid PNG signature.";
    }

    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint8_t bit_depth = 0;
    std::uint8_t color_type = 0;
    std::vector<std::uint8_t> compressed_image_data;
    bool saw_header = false;
    bool saw_end = false;

    std::size_t cursor = kPngSignature.size();
    while (cursor < png_bytes.size()) {
        if ((png_bytes.size() - cursor) < 12) {
            return "PNG chunk header is truncated.";
        }

        const std::uint32_t chunk_length = read_big_endian_u32(&png_bytes[cursor]);
        cursor += 4;
        const std::string_view chunk_type(
            reinterpret_cast<const char*>(&png_bytes[cursor]),
            4);
        cursor += 4;

        std::size_t total_chunk_bytes = 0;
        if (!checked_add(static_cast<std::size_t>(chunk_length), 4U, &total_chunk_bytes) ||
            cursor + total_chunk_bytes > png_bytes.size()) {
            return "PNG chunk overruns the atlas image file.";
        }

        const std::uint8_t* chunk_data = &png_bytes[cursor];
        cursor += static_cast<std::size_t>(chunk_length);
        cursor += 4; // Skip CRC.

        if (chunk_type == "IHDR") {
            if (chunk_length != 13) {
                return "PNG IHDR chunk had an unexpected size.";
            }

            width = read_big_endian_u32(chunk_data);
            height = read_big_endian_u32(chunk_data + 4);
            bit_depth = chunk_data[8];
            color_type = chunk_data[9];
            const std::uint8_t compression_method = chunk_data[10];
            const std::uint8_t filter_method = chunk_data[11];
            const std::uint8_t interlace_method = chunk_data[12];
            if (width == 0 || height == 0) {
                return "PNG atlas image dimensions must be greater than zero.";
            }
            if (bit_depth != 8) {
                return "PNG atlas images must use 8-bit channels.";
            }
            if (png_channel_count(color_type) == 0) {
                return "PNG atlas images must use RGB or RGBA color data.";
            }
            if (compression_method != 0 || filter_method != 0 || interlace_method != 0) {
                return "PNG atlas images must use standard non-interlaced encoding.";
            }
            saw_header = true;
            continue;
        }

        if (chunk_type == "IDAT") {
            if (!saw_header) {
                return "PNG image data appeared before the IHDR chunk.";
            }
            const std::size_t previous_size = compressed_image_data.size();
            std::size_t required_size = 0;
            if (!checked_add(previous_size, static_cast<std::size_t>(chunk_length), &required_size)) {
                return "PNG image data is too large to decode safely.";
            }
            compressed_image_data.resize(required_size);
            std::copy(
                chunk_data,
                chunk_data + chunk_length,
                compressed_image_data.begin() + static_cast<std::ptrdiff_t>(previous_size));
            continue;
        }

        if (chunk_type == "IEND") {
            saw_end = true;
            break;
        }
    }

    if (!saw_header) {
        return "PNG atlas image is missing the IHDR chunk.";
    }
    if (!saw_end) {
        return "PNG atlas image is missing the IEND chunk.";
    }
    if (compressed_image_data.empty()) {
        return "PNG atlas image did not contain any IDAT payload.";
    }
    if (width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return "PNG atlas image dimensions exceed supported renderer limits.";
    }

    const std::size_t channels = static_cast<std::size_t>(png_channel_count(color_type));
    std::size_t row_stride = 0;
    if (!checked_multiply(static_cast<std::size_t>(width), channels, &row_stride)) {
        return "PNG atlas image row stride overflowed size_t.";
    }
    std::size_t filtered_row_bytes = 0;
    if (!checked_add(row_stride, 1U, &filtered_row_bytes)) {
        return "PNG atlas image row size overflowed size_t.";
    }
    std::size_t expected_decompressed_size = 0;
    if (!checked_multiply(
            static_cast<std::size_t>(height),
            filtered_row_bytes,
            &expected_decompressed_size)) {
        return "PNG atlas image buffer is too large to decode safely.";
    }

    std::vector<std::uint8_t> decompressed(expected_decompressed_size);
    uLongf actual_decompressed_size = static_cast<uLongf>(decompressed.size());
    const int zlib_status = uncompress(
        decompressed.data(),
        &actual_decompressed_size,
        compressed_image_data.data(),
        static_cast<uLongf>(compressed_image_data.size()));
    if (zlib_status != Z_OK ||
        actual_decompressed_size != static_cast<uLongf>(expected_decompressed_size)) {
        return "PNG atlas image DEFLATE stream could not be decompressed.";
    }

    std::size_t unpacked_size = 0;
    if (!checked_multiply(static_cast<std::size_t>(height), row_stride, &unpacked_size)) {
        return "PNG atlas image unpacked buffer overflowed size_t.";
    }
    std::vector<std::uint8_t> unpacked(unpacked_size);
    for (std::size_t row = 0; row < static_cast<std::size_t>(height); ++row) {
        const std::size_t filtered_row_offset = row * filtered_row_bytes;
        const std::uint8_t filter_type = decompressed[filtered_row_offset];
        const std::uint8_t* filtered_row = decompressed.data() + filtered_row_offset + 1;
        std::uint8_t* output_row = unpacked.data() + (row * row_stride);
        for (std::size_t column = 0; column < row_stride; ++column) {
            const std::uint8_t raw = filtered_row[column];
            const std::uint8_t left = column >= channels ? output_row[column - channels] : 0;
            const std::uint8_t up =
                row > 0 ? unpacked[((row - 1) * row_stride) + column] : 0;
            const std::uint8_t up_left =
                (row > 0 && column >= channels)
                    ? unpacked[((row - 1) * row_stride) + column - channels]
                    : 0;

            switch (filter_type) {
            case 0:
                output_row[column] = raw;
                break;
            case 1:
                output_row[column] = static_cast<std::uint8_t>(raw + left);
                break;
            case 2:
                output_row[column] = static_cast<std::uint8_t>(raw + up);
                break;
            case 3:
                output_row[column] = static_cast<std::uint8_t>(
                    raw + ((static_cast<int>(left) + static_cast<int>(up)) / 2));
                break;
            case 4:
                output_row[column] =
                    static_cast<std::uint8_t>(raw + paeth_predictor(left, up, up_left));
                break;
            default:
                return "PNG atlas image used an unsupported scanline filter.";
            }
        }
    }

    std::size_t rgba_size = 0;
    if (!checked_multiply(
            static_cast<std::size_t>(width),
            static_cast<std::size_t>(height),
            &rgba_size) ||
        !checked_multiply(rgba_size, 4U, &rgba_size)) {
        return "PNG atlas image RGBA output buffer overflowed size_t.";
    }

    TextureImage image;
    image.width = static_cast<int>(width);
    image.height = static_cast<int>(height);
    image.rgba8.resize(rgba_size);
    for (std::size_t row = 0; row < static_cast<std::size_t>(height); ++row) {
        const std::size_t source_offset = row * row_stride;
        const std::size_t destination_offset =
            row * static_cast<std::size_t>(width) * 4U;
        for (std::size_t column = 0; column < static_cast<std::size_t>(width); ++column) {
            const std::size_t source_index = source_offset + (column * channels);
            const std::size_t destination_index = destination_offset + (column * 4U);
            image.rgba8[destination_index + 0] = unpacked[source_index + 0];
            image.rgba8[destination_index + 1] = unpacked[source_index + 1];
            image.rgba8[destination_index + 2] = unpacked[source_index + 2];
            image.rgba8[destination_index + 3] =
                channels == 4 ? unpacked[source_index + 3] : 255;
        }
    }

    *image_out = std::move(image);
    return std::nullopt;
}

GLint gl_texture_filter(std::string_view filter_name) {
    return filter_name == "nearest" ? GL_NEAREST : GL_LINEAR;
}

GLint gl_texture_wrap(std::string_view wrap_name) {
    if (wrap_name == "repeat") {
        return GL_REPEAT;
    }
    if (wrap_name == "mirrored_repeat") {
        return GL_MIRRORED_REPEAT;
    }
    return GL_CLAMP_TO_EDGE;
}

SceneBounds attachment_bounds(const RegionAttachmentDrawCommand& attachment) {
    const bool use_masked_geometry =
        !attachment.masked_vertices.empty() && !attachment.masked_indices.empty();
    if (use_masked_geometry) {
        SceneBounds bounds;
        bounds.min_x = bounds.max_x = attachment.masked_vertices.front().position.x;
        bounds.min_y = bounds.max_y = attachment.masked_vertices.front().position.y;
        for (const RegionAttachmentVertex& vertex : attachment.masked_vertices) {
            bounds.min_x = std::min(bounds.min_x, vertex.position.x);
            bounds.min_y = std::min(bounds.min_y, vertex.position.y);
            bounds.max_x = std::max(bounds.max_x, vertex.position.x);
            bounds.max_y = std::max(bounds.max_y, vertex.position.y);
        }
        return bounds;
    }

    SceneBounds bounds;
    bounds.min_x = bounds.max_x = attachment.vertices.front().position.x;
    bounds.min_y = bounds.max_y = attachment.vertices.front().position.y;
    for (const RegionAttachmentVertex& vertex : attachment.vertices) {
        bounds.min_x = std::min(bounds.min_x, vertex.position.x);
        bounds.min_y = std::min(bounds.min_y, vertex.position.y);
        bounds.max_x = std::max(bounds.max_x, vertex.position.x);
        bounds.max_y = std::max(bounds.max_y, vertex.position.y);
    }
    return bounds;
}

SceneBounds attachment_bounds(const DynamicMeshDrawCommand& attachment) {
    SceneBounds bounds;
    bounds.min_x = bounds.max_x = attachment.masked_vertices.front().position.x;
    bounds.min_y = bounds.max_y = attachment.masked_vertices.front().position.y;
    for (const SkinnedMeshVertex& vertex : attachment.masked_vertices) {
        bounds.min_x = std::min(bounds.min_x, vertex.position.x);
        bounds.min_y = std::min(bounds.min_y, vertex.position.y);
        bounds.max_x = std::max(bounds.max_x, vertex.position.x);
        bounds.max_y = std::max(bounds.max_y, vertex.position.y);
    }
    return bounds;
}

SceneBounds scene_bounds(const PreparedScene& scene) {
    SceneBounds bounds;
    bool initialized = false;

    for (const PreparedDrawCommand& command : scene.draw_commands) {
        std::visit(
            [&](const auto& attachment) {
                using Attachment = std::decay_t<decltype(attachment)>;
                if constexpr (std::is_same_v<Attachment, DynamicMeshDrawCommand>) {
                    if (attachment.masked_vertices.empty()) {
                        return;
                    }
                }

                const SceneBounds attachment_bounds_value = attachment_bounds(attachment);
                if (!initialized) {
                    bounds = attachment_bounds_value;
                    initialized = true;
                    return;
                }

                bounds.min_x = std::min(bounds.min_x, attachment_bounds_value.min_x);
                bounds.min_y = std::min(bounds.min_y, attachment_bounds_value.min_y);
                bounds.max_x = std::max(bounds.max_x, attachment_bounds_value.max_x);
                bounds.max_y = std::max(bounds.max_y, attachment_bounds_value.max_y);
            },
            command);
    }

    if (!initialized) {
        return {-1.0, -1.0, 1.0, 1.0};
    }

    const double width = std::max(bounds.max_x - bounds.min_x, 1.0);
    const double height = std::max(bounds.max_y - bounds.min_y, 1.0);
    const double padding_x = std::max(width * 0.1, 16.0);
    const double padding_y = std::max(height * 0.1, 16.0);
    bounds.min_x -= padding_x;
    bounds.max_x += padding_x;
    bounds.min_y -= padding_y;
    bounds.max_y += padding_y;
    return bounds;
}

SceneBounds fit_bounds_to_aspect(SceneBounds bounds, int width, int height) {
    const double safe_width = std::max<double>(width, 1.0);
    const double safe_height = std::max<double>(height, 1.0);
    const double target_aspect = safe_width / safe_height;
    const double bounds_width = std::max(bounds.max_x - bounds.min_x, 1.0);
    const double bounds_height = std::max(bounds.max_y - bounds.min_y, 1.0);
    const double bounds_aspect = bounds_width / bounds_height;
    const double center_x = (bounds.min_x + bounds.max_x) * 0.5;
    const double center_y = (bounds.min_y + bounds.max_y) * 0.5;

    if (bounds_aspect < target_aspect) {
        const double expanded_width = bounds_height * target_aspect;
        const double half_width = expanded_width * 0.5;
        bounds.min_x = center_x - half_width;
        bounds.max_x = center_x + half_width;
    } else {
        const double expanded_height = bounds_width / target_aspect;
        const double half_height = expanded_height * 0.5;
        bounds.min_y = center_y - half_height;
        bounds.max_y = center_y + half_height;
    }

    return bounds;
}

std::array<float, 16> orthographic_projection(const SceneBounds& bounds) {
    const double width = std::max(bounds.max_x - bounds.min_x, 1.0);
    const double height = std::max(bounds.max_y - bounds.min_y, 1.0);

    return {{
        static_cast<float>(2.0 / width),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        static_cast<float>(2.0 / height),
        0.0f,
        0.0f,
        0.0f,
        0.0f,
        -1.0f,
        0.0f,
        static_cast<float>(-(bounds.max_x + bounds.min_x) / width),
        static_cast<float>(-(bounds.max_y + bounds.min_y) / height),
        0.0f,
        1.0f,
    }};
}

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
        return "Failed to create OpenGL shader object.";
    }

    glShaderSource(shader, 1, &source, nullptr);
    glCompileShader(shader);

    GLint compile_status = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &compile_status);
    if (compile_status == GL_TRUE) {
        *shader_out = shader;
        return std::nullopt;
    }

    std::string error_message = "OpenGL shader compilation failed.";
    if (const std::optional<std::string> log = shader_log(shader)) {
        error_message += " ";
        error_message += *log;
    }
    glDeleteShader(shader);
    return error_message;
}

std::string_view color_shader_variant_name(ColorShaderVariant shader_variant) {
    switch (shader_variant) {
    case ColorShaderVariant::SingleColor:
        return "single_color";
    case ColorShaderVariant::TwoColorTint:
        return "two_color_tint";
    }

    return "unknown";
}

std::optional<std::string> link_program(
    GlShaderProgram* program_out,
    ColorShaderVariant shader_variant) {
    program_out->program = glCreateProgram();
    if (program_out->program == 0) {
        return "Failed to create OpenGL shader program.";
    }

    glAttachShader(program_out->program, program_out->vertex_shader);
    glAttachShader(program_out->program, program_out->fragment_shader);
    glBindAttribLocation(program_out->program, kLocalPosition0AttributeLocation, "a_local_position0");
    glBindAttribLocation(program_out->program, kLocalPosition1AttributeLocation, "a_local_position1");
    glBindAttribLocation(program_out->program, kLocalPosition2AttributeLocation, "a_local_position2");
    glBindAttribLocation(program_out->program, kLocalPosition3AttributeLocation, "a_local_position3");
    glBindAttribLocation(program_out->program, kUvAttributeLocation, "a_uv");
    glBindAttribLocation(program_out->program, kBoneIndicesAttributeLocation, "a_bone_indices");
    glBindAttribLocation(program_out->program, kBoneWeightsAttributeLocation, "a_bone_weights");
    glBindAttribLocation(program_out->program, kLightColorAttributeLocation, "a_light_color");
    if (shader_variant == ColorShaderVariant::TwoColorTint) {
        glBindAttribLocation(program_out->program, kDarkColorAttributeLocation, "a_dark_color");
    }
    glLinkProgram(program_out->program);

    GLint link_status = GL_FALSE;
    glGetProgramiv(program_out->program, GL_LINK_STATUS, &link_status);
    if (link_status == GL_TRUE) {
        program_out->projection_location =
            glGetUniformLocation(program_out->program, "u_projection");
        program_out->bone_palette_location =
            glGetUniformLocation(program_out->program, "u_bones[0]");
        program_out->texture_location =
            glGetUniformLocation(program_out->program, "u_texture");
        program_out->pma_location =
            glGetUniformLocation(program_out->program, "u_pma");
        if (program_out->projection_location < 0 ||
            program_out->bone_palette_location < 0 ||
            program_out->texture_location < 0 ||
            program_out->pma_location < 0) {
            return "OpenGL shader did not expose the expected uniform locations.";
        }
        return std::nullopt;
    }

    std::string error_message = "OpenGL shader link failed.";
    if (const std::optional<std::string> log = program_log(program_out->program)) {
        error_message += " ";
        error_message += *log;
    }
    return error_message;
}

std::optional<std::string> compile_program(
    ColorShaderVariant shader_variant,
    const char* vertex_source,
    const char* fragment_source,
    GlShaderProgram* program_out) {
    if (const std::optional<std::string> error =
            compile_shader(GL_VERTEX_SHADER, vertex_source, &program_out->vertex_shader)) {
        return error;
    }
    if (const std::optional<std::string> error =
            compile_shader(GL_FRAGMENT_SHADER, fragment_source, &program_out->fragment_shader)) {
        return error;
    }
    if (const std::optional<std::string> error = link_program(program_out, shader_variant)) {
        return error;
    }
    return std::nullopt;
}

void configure_stream_vertex_array(GLuint vao, GLuint vbo, GLuint ebo) {
    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

    const auto bind_attribute = [](GLuint location, GLint component_count, std::size_t offset) {
        glEnableVertexAttribArray(location);
        glVertexAttribPointer(
            location,
            component_count,
            GL_FLOAT,
            GL_FALSE,
            sizeof(GlStreamVertex),
            reinterpret_cast<const void*>(offset));
    };
    bind_attribute(
        kLocalPosition0AttributeLocation,
        2,
        offsetof(GlStreamVertex, local_position));
    bind_attribute(
        kLocalPosition1AttributeLocation,
        2,
        offsetof(GlStreamVertex, local_position) + (sizeof(float) * 2U));
    bind_attribute(
        kLocalPosition2AttributeLocation,
        2,
        offsetof(GlStreamVertex, local_position) + (sizeof(float) * 4U));
    bind_attribute(
        kLocalPosition3AttributeLocation,
        2,
        offsetof(GlStreamVertex, local_position) + (sizeof(float) * 6U));
    bind_attribute(kUvAttributeLocation, 2, offsetof(GlStreamVertex, uv));
    bind_attribute(kBoneIndicesAttributeLocation, 4, offsetof(GlStreamVertex, bone_indices));
    bind_attribute(kBoneWeightsAttributeLocation, 4, offsetof(GlStreamVertex, bone_weights));
    bind_attribute(kLightColorAttributeLocation, 4, offsetof(GlStreamVertex, light_color));
    glEnableVertexAttribArray(kDarkColorAttributeLocation);
    glVertexAttribPointer(
        kDarkColorAttributeLocation,
        4,
        GL_UNSIGNED_BYTE,
        GL_TRUE,
        sizeof(GlStreamVertex),
        reinterpret_cast<const void*>(offsetof(GlStreamVertex, dark_color)));
}

std::optional<std::string> initialize_resources(
    const PreparedScene& scene,
    const TextureImage& texture_image,
    GlRenderResources* resources) {
    std::size_t expected_rgba_bytes = 0;
    if (texture_image.width <= 0 || texture_image.height <= 0 ||
        !checked_multiply(
            static_cast<std::size_t>(texture_image.width),
            static_cast<std::size_t>(texture_image.height),
            &expected_rgba_bytes) ||
        !checked_multiply(expected_rgba_bytes, 4U, &expected_rgba_bytes) ||
        texture_image.rgba8.size() != expected_rgba_bytes) {
        return "Atlas texture image data was invalid.";
    }

    if (const std::optional<std::string> error = compile_program(
            ColorShaderVariant::SingleColor,
            kSingleColorVertexShaderSource,
            kSingleColorFragmentShaderSource,
            &resources->single_color_program)) {
        return error;
    }
    if (const std::optional<std::string> error = compile_program(
            ColorShaderVariant::TwoColorTint,
            kTwoColorTintVertexShaderSource,
            kTwoColorTintFragmentShaderSource,
            &resources->two_color_program)) {
        return error;
    }

    glGenVertexArrays(1, &resources->vao);
    glGenBuffers(1, &resources->vbo);
    glGenBuffers(1, &resources->ebo);
    glGenVertexArrays(1, &resources->clip_vao);
    glGenBuffers(1, &resources->clip_vbo);
    glGenTextures(1, &resources->texture);
    if (resources->vao == 0 || resources->vbo == 0 || resources->ebo == 0 ||
        resources->clip_vao == 0 || resources->clip_vbo == 0 || resources->texture == 0) {
        return "Failed to allocate OpenGL render resources.";
    }

    configure_stream_vertex_array(resources->vao, resources->vbo, resources->ebo);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(kStreamingVertexBufferBytes),
        nullptr,
        GL_STREAM_DRAW);
    glBufferData(
        GL_ELEMENT_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(kStreamingIndexBufferBytes),
        nullptr,
        GL_STREAM_DRAW);
    resources->vbo_capacity_bytes = kStreamingVertexBufferBytes;
    resources->ebo_capacity_bytes = kStreamingIndexBufferBytes;

    configure_stream_vertex_array(resources->clip_vao, resources->clip_vbo, 0);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(kClipVertexBufferBytes),
        nullptr,
        GL_STREAM_DRAW);
    resources->clip_vbo_capacity_bytes = kClipVertexBufferBytes;

    glBindTexture(GL_TEXTURE_2D, resources->texture);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, gl_texture_filter(scene.atlas_filter_min));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, gl_texture_filter(scene.atlas_filter_mag));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, gl_texture_wrap(scene.atlas_wrap_x));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, gl_texture_wrap(scene.atlas_wrap_y));
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(
        GL_TEXTURE_2D,
        0,
        GL_RGBA,
        texture_image.width,
        texture_image.height,
        0,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        texture_image.rgba8.data());

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    return std::nullopt;
}

void destroy_program(GlShaderProgram* program) {
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
    program->bone_palette_location = -1;
    program->texture_location = -1;
    program->pma_location = -1;
}

void destroy_resources(GlRenderResources* resources) {
    if (resources->texture != 0) {
        glDeleteTextures(1, &resources->texture);
        resources->texture = 0;
    }
    if (resources->clip_vbo != 0) {
        glDeleteBuffers(1, &resources->clip_vbo);
        resources->clip_vbo = 0;
    }
    if (resources->clip_vao != 0) {
        glDeleteVertexArrays(1, &resources->clip_vao);
        resources->clip_vao = 0;
    }
    if (resources->ebo != 0) {
        glDeleteBuffers(1, &resources->ebo);
        resources->ebo = 0;
    }
    if (resources->vbo != 0) {
        glDeleteBuffers(1, &resources->vbo);
        resources->vbo = 0;
    }
    if (resources->vao != 0) {
        glDeleteVertexArrays(1, &resources->vao);
        resources->vao = 0;
    }
    resources->vbo_capacity_bytes = 0;
    resources->ebo_capacity_bytes = 0;
    resources->clip_vbo_capacity_bytes = 0;
    destroy_program(&resources->single_color_program);
    destroy_program(&resources->two_color_program);
}

GLenum gl_blend_factor(BlendFactor factor) {
    switch (factor) {
    case BlendFactor::Zero:
        return GL_ZERO;
    case BlendFactor::One:
        return GL_ONE;
    case BlendFactor::SrcAlpha:
        return GL_SRC_ALPHA;
    case BlendFactor::OneMinusSrcAlpha:
        return GL_ONE_MINUS_SRC_ALPHA;
    case BlendFactor::DstColor:
        return GL_DST_COLOR;
    case BlendFactor::OneMinusSrcColor:
        return GL_ONE_MINUS_SRC_COLOR;
    }

    return GL_ZERO;
}

BlendState compute_blend_state(runtime::BlendMode blend_mode, bool premultiplied_alpha) {
    switch (blend_mode) {
    case runtime::BlendMode::Additive:
        return {
            premultiplied_alpha ? BlendFactor::One : BlendFactor::SrcAlpha,
            BlendFactor::One,
        };
    case runtime::BlendMode::Multiply:
        return {BlendFactor::DstColor, BlendFactor::OneMinusSrcAlpha};
    case runtime::BlendMode::Screen:
        return {BlendFactor::One, BlendFactor::OneMinusSrcColor};
    case runtime::BlendMode::Normal:
    default:
        return {
            premultiplied_alpha ? BlendFactor::One : BlendFactor::SrcAlpha,
            BlendFactor::OneMinusSrcAlpha,
        };
    }
}

void apply_blend_mode(runtime::BlendMode blend_mode, bool premultiplied_alpha) {
    const BlendState blend_state = compute_blend_state(blend_mode, premultiplied_alpha);
    glBlendFunc(
        gl_blend_factor(blend_state.src_factor),
        gl_blend_factor(blend_state.dst_factor));
}

std::uint8_t color_component_byte(double component) {
    const double clamped = std::clamp(component, 0.0, 1.0);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0));
}

void set_stream_vertex_colors(
    GlStreamVertex* vertex,
    const runtime::SlotColor& color,
    const std::optional<runtime::SlotColor>& dark_color) {
    vertex->light_color[0] = static_cast<float>(color.r);
    vertex->light_color[1] = static_cast<float>(color.g);
    vertex->light_color[2] = static_cast<float>(color.b);
    vertex->light_color[3] = static_cast<float>(color.a);

    const runtime::SlotColor dark = dark_color.value_or(runtime::SlotColor{});
    vertex->dark_color[0] = color_component_byte(dark.r);
    vertex->dark_color[1] = color_component_byte(dark.g);
    vertex->dark_color[2] = color_component_byte(dark.b);
    vertex->dark_color[3] = color_component_byte(dark.a);
}

const GlShaderProgram& shader_program_for_variant(
    const GlRenderResources& resources,
    ColorShaderVariant shader_variant) {
    switch (shader_variant) {
    case ColorShaderVariant::TwoColorTint:
        return resources.two_color_program;
    case ColorShaderVariant::SingleColor:
    default:
        return resources.single_color_program;
    }
}

float pma_uniform_value(bool premultiplied_alpha) {
    // Spine's published toggle formula uses 0 for PMA textures and 1 for straight-alpha textures.
    return premultiplied_alpha ? 0.0f : 1.0f;
}

ColorShaderVariant command_shader_variant(const PreparedDrawCommand& command) {
    return std::visit(
        [](const auto& attachment) {
            return attachment.dark_color.has_value()
                ? ColorShaderVariant::TwoColorTint
                : ColorShaderVariant::SingleColor;
        },
        command);
}

GlStreamVertex rigid_stream_vertex(
    const RenderPoint& position,
    const RenderPoint& uv,
    std::size_t bone_index,
    const runtime::SlotColor& color,
    const std::optional<runtime::SlotColor>& dark_color) {
    GlStreamVertex vertex{};
    vertex.local_position[0][0] = static_cast<float>(position.x);
    vertex.local_position[0][1] = static_cast<float>(position.y);
    vertex.uv[0] = static_cast<float>(uv.x);
    vertex.uv[1] = static_cast<float>(uv.y);
    vertex.bone_indices[0] = static_cast<float>(bone_index);
    vertex.bone_weights[0] = 1.0f;
    set_stream_vertex_colors(&vertex, color, dark_color);
    return vertex;
}

GlStreamVertex weighted_stream_vertex(
    const GpuSkinningVertexPayload& payload,
    const runtime::SlotColor& color,
    const std::optional<runtime::SlotColor>& dark_color) {
    GlStreamVertex vertex{};
    vertex.uv[0] = static_cast<float>(payload.uv.x);
    vertex.uv[1] = static_cast<float>(payload.uv.y);
    for (std::size_t influence_index = 0; influence_index < payload.influence_count;
         ++influence_index) {
        vertex.local_position[influence_index][0] =
            static_cast<float>(payload.bone_local_positions[influence_index].x);
        vertex.local_position[influence_index][1] =
            static_cast<float>(payload.bone_local_positions[influence_index].y);
        vertex.bone_indices[influence_index] =
            static_cast<float>(payload.bone_indices[influence_index]);
        vertex.bone_weights[influence_index] =
            static_cast<float>(payload.bone_weights[influence_index]);
    }
    set_stream_vertex_colors(&vertex, color, dark_color);
    return vertex;
}

const std::string& command_texture_name(const PreparedDrawCommand& command) {
    return std::visit(
        [](const auto& attachment) -> const std::string& { return attachment.texture_name; },
        command);
}

runtime::BlendMode command_blend_mode(const PreparedDrawCommand& command) {
    return std::visit(
        [](const auto& attachment) { return attachment.blend_mode; },
        command);
}

std::optional<std::string> append_region_stream_geometry(
    const RegionAttachmentDrawCommand& attachment,
    std::size_t identity_bone_index,
    std::size_t bone_count,
    GeometryClipMode geometry_clip_mode,
    std::vector<GlStreamVertex>* vertices_out,
    std::vector<std::uint32_t>* indices_out) {
    const std::size_t base_vertex = vertices_out->size();
    if (base_vertex > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return "Renderer stream vertex offset exceeded 32-bit index space.";
    }

    const bool use_clipped_geometry =
        geometry_clip_mode == GeometryClipMode::UsePreparedMaskedGeometry &&
        attachment.clip_attachment_name.has_value() &&
        !attachment.masked_vertices.empty() &&
        !attachment.masked_indices.empty();
    if (geometry_clip_mode == GeometryClipMode::UsePreparedMaskedGeometry &&
        attachment.clip_attachment_name.has_value() && !use_clipped_geometry) {
        return std::nullopt;
    }
    if (use_clipped_geometry) {
        vertices_out->reserve(vertices_out->size() + attachment.masked_vertices.size());
        for (const RegionAttachmentVertex& vertex : attachment.masked_vertices) {
            vertices_out->push_back(rigid_stream_vertex(
                vertex.position,
                vertex.uv,
                identity_bone_index,
                attachment.color,
                attachment.dark_color));
        }
        indices_out->reserve(indices_out->size() + attachment.masked_indices.size());
        for (const std::size_t index : attachment.masked_indices) {
            if (index > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                return "Renderer stream region index exceeded 32-bit index space.";
            }
            indices_out->push_back(static_cast<std::uint32_t>(base_vertex + index));
        }
        return std::nullopt;
    }

    if (attachment.bone_index >= bone_count) {
        return "attachment '" + attachment.attachment_name + "' references invalid rigid bone index " +
            std::to_string(attachment.bone_index);
    }

    vertices_out->reserve(vertices_out->size() + attachment.vertices.size());
    for (std::size_t vertex_index = 0; vertex_index < attachment.vertices.size(); ++vertex_index) {
        vertices_out->push_back(rigid_stream_vertex(
            attachment.local_positions[vertex_index],
            attachment.vertices[vertex_index].uv,
            attachment.bone_index,
            attachment.color,
            attachment.dark_color));
    }
    indices_out->reserve(indices_out->size() + kQuadIndices.size());
    for (const std::uint32_t index : kQuadIndices) {
        indices_out->push_back(static_cast<std::uint32_t>(base_vertex + index));
    }
    return std::nullopt;
}

std::optional<std::string> append_dynamic_mesh_stream_geometry(
    const DynamicMeshDrawCommand& attachment,
    std::size_t identity_bone_index,
    std::size_t bone_count,
    GeometryClipMode geometry_clip_mode,
    std::vector<GlStreamVertex>* vertices_out,
    std::vector<std::uint32_t>* indices_out) {
    const std::size_t base_vertex = vertices_out->size();
    if (base_vertex > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return "Renderer stream vertex offset exceeded 32-bit index space.";
    }

    const bool use_clipped_geometry =
        geometry_clip_mode == GeometryClipMode::UsePreparedMaskedGeometry &&
        attachment.clip_attachment_name.has_value() &&
        !attachment.masked_vertices.empty() &&
        !attachment.masked_indices.empty();
    if (geometry_clip_mode == GeometryClipMode::UsePreparedMaskedGeometry &&
        attachment.clip_attachment_name.has_value() && !use_clipped_geometry) {
        return std::nullopt;
    }
    if (use_clipped_geometry) {
        vertices_out->reserve(vertices_out->size() + attachment.masked_vertices.size());
        for (const SkinnedMeshVertex& vertex : attachment.masked_vertices) {
            vertices_out->push_back(rigid_stream_vertex(
                vertex.position,
                vertex.uv,
                identity_bone_index,
                attachment.color,
                attachment.dark_color));
        }
        indices_out->reserve(indices_out->size() + attachment.masked_indices.size());
        for (const std::size_t index : attachment.masked_indices) {
            if (index > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
                return "Renderer stream mesh index exceeded 32-bit index space.";
            }
            indices_out->push_back(static_cast<std::uint32_t>(base_vertex + index));
        }
        return std::nullopt;
    }

    vertices_out->reserve(vertices_out->size() + attachment.vertex_payloads.size());
    for (const GpuSkinningVertexPayload& payload : attachment.vertex_payloads) {
        for (std::size_t influence_index = 0; influence_index < payload.influence_count;
             ++influence_index) {
            if (payload.bone_indices[influence_index] >= bone_count) {
                return "attachment '" + attachment.attachment_name +
                    "' references invalid weighted bone index " +
                    std::to_string(payload.bone_indices[influence_index]);
            }
        }
        vertices_out->push_back(weighted_stream_vertex(payload, attachment.color, attachment.dark_color));
    }
    indices_out->reserve(indices_out->size() + attachment.indices.size());
    for (const std::size_t index : attachment.indices) {
        if (index > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            return "Renderer stream mesh index exceeded 32-bit index space.";
        }
        indices_out->push_back(static_cast<std::uint32_t>(base_vertex + index));
    }
    return std::nullopt;
}

StreamBuildResult build_stream_batches(
    const PreparedScene& scene,
    GeometryClipMode geometry_clip_mode) {
    StreamBuildResult result;
    if (scene.draw_commands.empty()) {
        return result;
    }

    const std::size_t bone_count = scene.bone_palette.size();
    if (bone_count + kIdentityBoneCount > kMaxRendererBoneUniforms) {
        result.error_message =
            "Prepared scene requires " + std::to_string(bone_count + kIdentityBoneCount) +
            " bone uniforms, but the batch renderer supports at most " +
            std::to_string(kMaxRendererBoneUniforms) + ".";
        return result;
    }

    const std::size_t identity_bone_index = bone_count;
    for (std::size_t command_index = 0; command_index < scene.draw_commands.size(); ++command_index) {
        const PreparedDrawCommand& command = scene.draw_commands[command_index];
        const std::size_t vertex_offset = result.vertices.size();
        const std::size_t index_offset = result.indices.size();

        std::optional<std::string> error = std::visit(
            [&](const auto& attachment) {
                using Attachment = std::decay_t<decltype(attachment)>;
                if constexpr (std::is_same_v<Attachment, RegionAttachmentDrawCommand>) {
                    return append_region_stream_geometry(
                        attachment,
                        identity_bone_index,
                        bone_count,
                        geometry_clip_mode,
                        &result.vertices,
                        &result.indices);
                } else {
                    return append_dynamic_mesh_stream_geometry(
                        attachment,
                        identity_bone_index,
                        bone_count,
                        geometry_clip_mode,
                        &result.vertices,
                        &result.indices);
                }
            },
            command);
        if (error.has_value()) {
            result.error_message = *error;
            return result;
        }

        const std::size_t added_vertices = result.vertices.size() - vertex_offset;
        const std::size_t added_indices = result.indices.size() - index_offset;
        if (added_vertices == 0 || added_indices == 0) {
            continue;
        }

        if (!result.batches.empty() &&
            result.batches.back().texture_name == command_texture_name(command) &&
            result.batches.back().blend_mode == command_blend_mode(command) &&
            result.batches.back().shader_variant == command_shader_variant(command)) {
            PreparedStreamBatch& batch = result.batches.back();
            batch.draw_command_count += 1;
            batch.vertex_count += added_vertices;
            batch.index_count += added_indices;
            continue;
        }

        PreparedStreamBatch batch;
        batch.texture_name = command_texture_name(command);
        batch.blend_mode = command_blend_mode(command);
        batch.shader_variant = command_shader_variant(command);
        batch.draw_command_offset = command_index;
        batch.draw_command_count = 1;
        batch.vertex_offset = vertex_offset;
        batch.vertex_count = added_vertices;
        batch.index_offset = index_offset;
        batch.index_count = added_indices;
        result.batches.push_back(std::move(batch));
    }

    return result;
}

void ensure_stream_buffer_capacity(
    GLenum target,
    GLuint buffer,
    std::size_t required_bytes,
    std::size_t initial_capacity,
    std::size_t* capacity_bytes) {
    std::size_t capacity = *capacity_bytes;
    if (capacity == 0) {
        capacity = initial_capacity;
    }
    while (capacity < required_bytes) {
        capacity *= 2U;
    }
    if (capacity == *capacity_bytes) {
        return;
    }

    glBindBuffer(target, buffer);
    glBufferData(target, static_cast<GLsizeiptr>(capacity), nullptr, GL_STREAM_DRAW);
    *capacity_bytes = capacity;
}

std::vector<float> bone_uniform_payload(const PreparedScene& scene) {
    std::vector<float> payload;
    if (scene.draw_commands.empty()) {
        return payload;
    }

    payload.reserve((scene.bone_palette.size() + kIdentityBoneCount) * 6U);
    for (const runtime::BoneWorldTransform& transform : scene.bone_palette) {
        payload.push_back(static_cast<float>(transform.a));
        payload.push_back(static_cast<float>(transform.c));
        payload.push_back(static_cast<float>(transform.b));
        payload.push_back(static_cast<float>(transform.d));
        payload.push_back(static_cast<float>(transform.world_x));
        payload.push_back(static_cast<float>(transform.world_y));
    }

    payload.push_back(1.0f);
    payload.push_back(0.0f);
    payload.push_back(0.0f);
    payload.push_back(1.0f);
    payload.push_back(0.0f);
    payload.push_back(0.0f);
    return payload;
}

std::optional<std::string> append_draw_command_stream_geometry(
    const PreparedDrawCommand& command,
    std::size_t identity_bone_index,
    std::size_t bone_count,
    GeometryClipMode geometry_clip_mode,
    std::vector<GlStreamVertex>* vertices_out,
    std::vector<std::uint32_t>* indices_out) {
    return std::visit(
        [&](const auto& attachment) {
            using Attachment = std::decay_t<decltype(attachment)>;
            if constexpr (std::is_same_v<Attachment, RegionAttachmentDrawCommand>) {
                return append_region_stream_geometry(
                    attachment,
                    identity_bone_index,
                    bone_count,
                    geometry_clip_mode,
                    vertices_out,
                    indices_out);
            } else {
                return append_dynamic_mesh_stream_geometry(
                    attachment,
                    identity_bone_index,
                    bone_count,
                    geometry_clip_mode,
                    vertices_out,
                    indices_out);
            }
        },
        command);
}

bool draw_batch_matches(
    const DrawBatchState& batch,
    const PreparedDrawCommand& command,
    std::uint8_t stencil_reference,
    bool stencil_enabled) {
    if (batch.commands.empty()) {
        return false;
    }

    return
        batch.texture_name == command_texture_name(command) &&
        batch.blend_mode == command_blend_mode(command) &&
        batch.shader_variant == command_shader_variant(command) &&
        batch.stencil_reference == stencil_reference &&
        batch.stencil_enabled == stencil_enabled;
}

void reset_draw_batch(DrawBatchState* batch) {
    batch->commands.clear();
    batch->texture_name.clear();
    batch->blend_mode = runtime::BlendMode::Normal;
    batch->shader_variant = ColorShaderVariant::SingleColor;
    batch->stencil_reference = 0;
    batch->stencil_enabled = false;
}

void set_stencil_test_for_reference(std::optional<std::uint8_t> stencil_reference) {
    if (stencil_reference.has_value()) {
        glEnable(GL_STENCIL_TEST);
        glStencilMask(0x00);
        glStencilFunc(GL_EQUAL, static_cast<GLint>(*stencil_reference), 0xFF);
        glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        return;
    }

    glDisable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glStencilFunc(GL_ALWAYS, 0, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}

std::vector<GlStreamVertex> clip_polygon_vertices(
    const ClipAttachmentDrawCommand& clip,
    std::size_t identity_bone_index) {
    std::vector<GlStreamVertex> vertices;
    vertices.reserve(clip.polygon.size());
    for (const RenderPoint& point : clip.polygon) {
        vertices.push_back(rigid_stream_vertex(
            point,
            RenderPoint{},
            identity_bone_index,
            runtime::SlotColor{1.0, 1.0, 1.0, 1.0},
            std::nullopt));
    }
    return vertices;
}

std::optional<std::string> render_clip_polygon_to_stencil(
    const ClipAttachmentDrawCommand& clip,
    const ActiveStencilClip& stencil_clip,
    bool restoring_parent_reference,
    bool premultiplied_alpha,
    const std::array<float, 16>& projection,
    const std::vector<float>& bone_payload,
    GlRenderResources* resources) {
    if (clip.polygon.size() < 3) {
        return "Clip attachment '" + clip.attachment_name + "' requires at least 3 points.";
    }

    const std::vector<GlStreamVertex> vertices =
        clip_polygon_vertices(clip, bone_payload.size() / 6U - 1U);
    const std::size_t vertex_bytes = vertices.size() * sizeof(GlStreamVertex);
    ensure_stream_buffer_capacity(
        GL_ARRAY_BUFFER,
        resources->clip_vbo,
        vertex_bytes,
        kClipVertexBufferBytes,
        &resources->clip_vbo_capacity_bytes);

    glBindVertexArray(resources->clip_vao);
    glBindBuffer(GL_ARRAY_BUFFER, resources->clip_vbo);
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        static_cast<GLsizeiptr>(vertex_bytes),
        vertices.data());

    const GlShaderProgram& program = resources->single_color_program;
    glUseProgram(program.program);
    glUniformMatrix4fv(program.projection_location, 1, GL_FALSE, projection.data());
    glUniform1i(program.texture_location, 0);
    glUniform1f(program.pma_location, pma_uniform_value(premultiplied_alpha));
    glUniformMatrix3x2fv(
        program.bone_palette_location,
        static_cast<GLsizei>(bone_payload.size() / 6U),
        GL_FALSE,
        bone_payload.data());

    glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
    glDisable(GL_BLEND);
    glEnable(GL_STENCIL_TEST);
    glStencilMask(stencil_clip.invert_mask);
    glStencilFunc(
        GL_EQUAL,
        static_cast<GLint>(
            restoring_parent_reference
                ? stencil_clip.reference_value
                : stencil_clip.parent_reference_value),
        0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_INVERT);
    glDrawArrays(GL_TRIANGLE_FAN, 0, static_cast<GLsizei>(vertices.size()));
    glStencilMask(0x00);
    glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glEnable(GL_BLEND);
    return std::nullopt;
}

std::optional<std::string> flush_draw_batch(
    DrawBatchState* batch,
    const PreparedScene& scene,
    const std::array<float, 16>& projection,
    const std::vector<float>& bone_payload,
    GlRenderResources* resources) {
    if (batch->commands.empty()) {
        return std::nullopt;
    }

    std::vector<GlStreamVertex> vertices;
    std::vector<std::uint32_t> indices;
    const std::size_t bone_count = scene.bone_palette.size();
    const std::size_t identity_bone_index = bone_count;
    for (const PreparedDrawCommand* command : batch->commands) {
        if (const std::optional<std::string> error = append_draw_command_stream_geometry(
                *command,
                identity_bone_index,
                bone_count,
                GeometryClipMode::UseOriginalGeometry,
                &vertices,
                &indices)) {
            reset_draw_batch(batch);
            return error;
        }
    }

    const runtime::BlendMode blend_mode = batch->blend_mode;
    const ColorShaderVariant shader_variant = batch->shader_variant;
    const std::uint8_t stencil_reference = batch->stencil_reference;
    const bool stencil_enabled = batch->stencil_enabled;
    reset_draw_batch(batch);
    if (vertices.empty() || indices.empty()) {
        return std::nullopt;
    }

    const std::size_t vertex_bytes = vertices.size() * sizeof(GlStreamVertex);
    const std::size_t index_bytes = indices.size() * sizeof(std::uint32_t);
    ensure_stream_buffer_capacity(
        GL_ARRAY_BUFFER,
        resources->vbo,
        vertex_bytes,
        kStreamingVertexBufferBytes,
        &resources->vbo_capacity_bytes);
    ensure_stream_buffer_capacity(
        GL_ELEMENT_ARRAY_BUFFER,
        resources->ebo,
        index_bytes,
        kStreamingIndexBufferBytes,
        &resources->ebo_capacity_bytes);

    glBindVertexArray(resources->vao);
    glBindBuffer(GL_ARRAY_BUFFER, resources->vbo);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, resources->ebo);
    glBufferSubData(
        GL_ARRAY_BUFFER,
        0,
        static_cast<GLsizeiptr>(vertex_bytes),
        vertices.data());
    glBufferSubData(
        GL_ELEMENT_ARRAY_BUFFER,
        0,
        static_cast<GLsizeiptr>(index_bytes),
        indices.data());

    const GlShaderProgram& program = shader_program_for_variant(*resources, shader_variant);
    glUseProgram(program.program);
    glUniformMatrix4fv(program.projection_location, 1, GL_FALSE, projection.data());
    glUniform1i(program.texture_location, 0);
    glUniform1f(program.pma_location, pma_uniform_value(scene.premultiplied_alpha));
    glUniformMatrix3x2fv(
        program.bone_palette_location,
        static_cast<GLsizei>(bone_payload.size() / 6U),
        GL_FALSE,
        bone_payload.data());
    glEnable(GL_BLEND);
    apply_blend_mode(blend_mode, scene.premultiplied_alpha);
    set_stencil_test_for_reference(
        stencil_enabled ? std::optional<std::uint8_t>{stencil_reference} : std::nullopt);
    glDrawElements(
        GL_TRIANGLES,
        static_cast<GLsizei>(indices.size()),
        GL_UNSIGNED_INT,
        nullptr);
    return std::nullopt;
}

std::optional<std::string> render_scene_frame(
    const PreparedScene& scene,
    GlRenderResources* resources,
    const SceneBounds& base_bounds,
    int framebuffer_width,
    int framebuffer_height) {
    glViewport(0, 0, framebuffer_width, framebuffer_height);
    glClearColor(0.08f, 0.09f, 0.12f, 1.0f);
    glClearStencil(0);
    glClear(GL_COLOR_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

    const SceneBounds framed_bounds =
        fit_bounds_to_aspect(base_bounds, framebuffer_width, framebuffer_height);
    const std::array<float, 16> projection = orthographic_projection(framed_bounds);
    const std::vector<float> bone_payload = bone_uniform_payload(scene);
    if (bone_payload.empty()) {
        return std::nullopt;
    }

    DrawBatchState batch;
    std::vector<ActiveStencilClip> active_clips;

    for (const PreparedSceneEventRef& event : scene.ordered_events) {
        switch (event.kind) {
        case PreparedSceneEventKind::ClipStart: {
            if (event.index >= scene.clip_attachments.size()) {
                return "Prepared scene clip start event referenced a missing clip attachment.";
            }
            if (const std::optional<std::string> error =
                    flush_draw_batch(&batch, scene, projection, bone_payload, resources)) {
                return error;
            }
            if (active_clips.size() >= 255U) {
                return "Clip nesting exceeded the 8-bit stencil reference range.";
            }

            const std::optional<internal::SoftwareStencilClipState> stencil_state =
                internal::stencil_clip_state_for_depth(active_clips.size() + 1U);
            if (!stencil_state.has_value()) {
                return "Failed to allocate a valid stencil reference for the clip stack.";
            }

            ActiveStencilClip active_clip;
            active_clip.clip_attachment_index = event.index;
            active_clip.reference_value = stencil_state->reference_value;
            active_clip.parent_reference_value = stencil_state->parent_reference_value;
            active_clip.invert_mask = stencil_state->invert_mask;
            active_clips.push_back(active_clip);

            if (const std::optional<std::string> error = render_clip_polygon_to_stencil(
                    scene.clip_attachments[event.index],
                    active_clips.back(),
                    false,
                    scene.premultiplied_alpha,
                    projection,
                    bone_payload,
                    resources)) {
                return error;
            }
            set_stencil_test_for_reference(
                std::optional<std::uint8_t>{active_clips.back().reference_value});
            break;
        }
        case PreparedSceneEventKind::Draw: {
            if (event.index >= scene.draw_commands.size()) {
                return "Prepared scene draw event referenced a missing draw command.";
            }
            const PreparedDrawCommand& command = scene.draw_commands[event.index];
            const bool stencil_enabled = !active_clips.empty();
            const std::uint8_t stencil_reference =
                stencil_enabled ? active_clips.back().reference_value : 0;
            if (!draw_batch_matches(batch, command, stencil_reference, stencil_enabled)) {
                if (const std::optional<std::string> error =
                        flush_draw_batch(&batch, scene, projection, bone_payload, resources)) {
                    return error;
                }
                batch.texture_name = command_texture_name(command);
                batch.blend_mode = command_blend_mode(command);
                batch.shader_variant = command_shader_variant(command);
                batch.stencil_reference = stencil_reference;
                batch.stencil_enabled = stencil_enabled;
            }
            batch.commands.push_back(&command);
            break;
        }
        case PreparedSceneEventKind::ClipEnd: {
            if (event.index >= scene.clip_attachments.size()) {
                return "Prepared scene clip end event referenced a missing clip attachment.";
            }
            if (const std::optional<std::string> error =
                    flush_draw_batch(&batch, scene, projection, bone_payload, resources)) {
                return error;
            }
            if (active_clips.empty()) {
                return "Prepared scene clip end event underflowed the stencil stack.";
            }

            const ActiveStencilClip active_clip = active_clips.back();
            active_clips.pop_back();
            if (active_clip.clip_attachment_index != event.index) {
                return "Prepared scene clip end event did not match the active clip stack.";
            }
            if (const std::optional<std::string> error = render_clip_polygon_to_stencil(
                    scene.clip_attachments[event.index],
                    active_clip,
                    true,
                    scene.premultiplied_alpha,
                    projection,
                    bone_payload,
                    resources)) {
                return error;
            }
            if (active_clips.empty()) {
                set_stencil_test_for_reference(std::nullopt);
            } else {
                set_stencil_test_for_reference(
                    std::optional<std::uint8_t>{active_clips.back().reference_value});
            }
            break;
        }
        }
    }

    if (const std::optional<std::string> error =
            flush_draw_batch(&batch, scene, projection, bone_payload, resources)) {
        return error;
    }
    if (!active_clips.empty()) {
        return "Prepared scene finished rendering with an unterminated clip stack.";
    }

    set_stencil_test_for_reference(std::nullopt);
    return std::nullopt;
}

RenderPoint transform_point(
    const runtime::BoneWorldTransform& transform,
    const RenderPoint& local_point) {
    return {
        transform.a * local_point.x + transform.b * local_point.y + transform.world_x,
        transform.c * local_point.x + transform.d * local_point.y + transform.world_y};
}

RenderPoint normalized_uv(double x, double y, const runtime::AtlasData& atlas) {
    return {x / atlas.info().width, y / atlas.info().height};
}

RenderPoint mesh_uv(
    double u,
    double v,
    const runtime::AtlasRegion& region,
    const runtime::AtlasData& atlas) {
    return normalized_uv(
        region.x + (u * region.width),
        region.y + (v * region.height),
        atlas);
}

std::string slot_error(std::size_t slot_index, std::string_view message) {
    std::ostringstream stream;
    stream << "slot[" << slot_index << "] " << message;
    return stream.str();
}

double mesh_weight_sum(const runtime::MeshGeometry::VertexWeights& vertex_weights) {
    double total_weight = 0.0;
    for (const runtime::MeshGeometry::VertexWeight& influence : vertex_weights.influences) {
        total_weight += influence.weight;
    }

    return total_weight;
}

std::string_view mesh_buffer_usage_name(MeshBufferUsage usage) {
    switch (usage) {
    case MeshBufferUsage::Static:
        return "static";
    case MeshBufferUsage::Dynamic:
        return "dynamic";
    }

    return "unknown";
}

std::string_view mesh_shader_path_name(MeshShaderPath shader_path) {
    switch (shader_path) {
    case MeshShaderPath::GpuSkinning:
        return "gpu_skinning";
    }

    return "unknown";
}

std::string_view blend_mode_name(runtime::BlendMode blend_mode) {
    switch (blend_mode) {
    case runtime::BlendMode::Normal:
        return "normal";
    case runtime::BlendMode::Additive:
        return "additive";
    case runtime::BlendMode::Multiply:
        return "multiply";
    case runtime::BlendMode::Screen:
        return "screen";
    }

    return "unknown";
}

struct ClipVertex {
    RenderPoint position;
    RenderPoint uv;
};

struct ActiveClipState {
    std::size_t clip_attachment_index{0};
    std::string attachment_name;
    std::optional<std::size_t> end_slot_index;
    std::vector<RenderPoint> polygon;
};

double polygon_signed_area(const std::vector<RenderPoint>& polygon) {
    if (polygon.size() < 3) {
        return 0.0;
    }

    double area = 0.0;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const RenderPoint& current = polygon[index];
        const RenderPoint& next = polygon[(index + 1) % polygon.size()];
        area += (current.x * next.y) - (next.x * current.y);
    }

    return area * 0.5;
}

bool polygon_is_convex(const std::vector<RenderPoint>& polygon) {
    if (polygon.size() < 4) {
        return polygon.size() >= 3;
    }

    constexpr double kTolerance = 1e-9;
    double expected_sign = 0.0;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const RenderPoint& a = polygon[index];
        const RenderPoint& b = polygon[(index + 1) % polygon.size()];
        const RenderPoint& c = polygon[(index + 2) % polygon.size()];
        const double cross =
            ((b.x - a.x) * (c.y - b.y)) - ((b.y - a.y) * (c.x - b.x));
        if (std::abs(cross) <= kTolerance) {
            continue;
        }
        if (expected_sign == 0.0) {
            expected_sign = cross;
            continue;
        }
        if ((cross > 0.0) != (expected_sign > 0.0)) {
            return false;
        }
    }

    return expected_sign != 0.0;
}

RenderPoint software_stencil_sample_point(
    const internal::SoftwareStencilBuffer& buffer,
    int x,
    int y) {
    return {
        buffer.origin_x + (static_cast<double>(x) + 0.5) * buffer.pixel_size,
        buffer.origin_y + (static_cast<double>(y) + 0.5) * buffer.pixel_size,
    };
}

double triangle_signed_area(
    const RenderPoint& a,
    const RenderPoint& b,
    const RenderPoint& c) {
    return ((b.x - a.x) * (c.y - a.y)) - ((b.y - a.y) * (c.x - a.x));
}

bool point_inside_triangle(
    const RenderPoint& point,
    const RenderPoint& a,
    const RenderPoint& b,
    const RenderPoint& c) {
    constexpr double kTolerance = 1e-9;
    const double area = triangle_signed_area(a, b, c);
    if (std::abs(area) <= kTolerance) {
        return false;
    }

    const double ab = triangle_signed_area(a, b, point);
    const double bc = triangle_signed_area(b, c, point);
    const double ca = triangle_signed_area(c, a, point);
    if (area > 0.0) {
        return ab >= -kTolerance && bc >= -kTolerance && ca >= -kTolerance;
    }
    return ab <= kTolerance && bc <= kTolerance && ca <= kTolerance;
}

bool point_on_segment(
    const RenderPoint& point,
    const RenderPoint& start,
    const RenderPoint& end) {
    constexpr double kTolerance = 1e-9;
    const double cross =
        ((end.x - start.x) * (point.y - start.y)) -
        ((end.y - start.y) * (point.x - start.x));
    if (std::abs(cross) > kTolerance) {
        return false;
    }

    const double min_x = std::min(start.x, end.x) - kTolerance;
    const double max_x = std::max(start.x, end.x) + kTolerance;
    const double min_y = std::min(start.y, end.y) - kTolerance;
    const double max_y = std::max(start.y, end.y) + kTolerance;
    return point.x >= min_x && point.x <= max_x && point.y >= min_y && point.y <= max_y;
}

bool point_inside_triangle_fan_even_odd(
    const std::vector<RenderPoint>& polygon,
    const RenderPoint& point) {
    if (polygon.size() < 3) {
        return false;
    }

    bool inside = false;
    for (std::size_t index = 0; index < polygon.size(); ++index) {
        const RenderPoint& start = polygon[index];
        const RenderPoint& end = polygon[(index + 1) % polygon.size()];
        if (point_on_segment(point, start, end)) {
            return true;
        }

        const bool intersects_scanline =
            ((start.y > point.y) != (end.y > point.y)) &&
            (point.x < ((end.x - start.x) * (point.y - start.y) / (end.y - start.y)) + start.x);
        if (intersects_scanline) {
            inside = !inside;
        }
    }

    return inside;
}

std::optional<internal::SoftwareStencilClipState> stencil_clip_state_for_depth_impl(
    std::size_t nesting_depth) {
    if (nesting_depth == 0 || nesting_depth > 255U) {
        return std::nullopt;
    }

    const std::uint8_t reference_value = static_cast<std::uint8_t>(nesting_depth);
    const std::uint8_t parent_reference_value = static_cast<std::uint8_t>(nesting_depth - 1U);
    return internal::SoftwareStencilClipState{
        reference_value,
        parent_reference_value,
        static_cast<std::uint8_t>(reference_value ^ parent_reference_value),
    };
}

std::optional<std::string> initialize_software_stencil_buffer_impl(
    int width,
    int height,
    double origin_x,
    double origin_y,
    double pixel_size,
    internal::SoftwareStencilBuffer* buffer_out) {
    if (buffer_out == nullptr) {
        return "Software stencil buffer output must not be null.";
    }
    if (width <= 0 || height <= 0) {
        return "Software stencil buffer dimensions must be positive.";
    }
    if (pixel_size <= 0.0) {
        return "Software stencil buffer pixel size must be positive.";
    }

    std::size_t sample_count = 0;
    if (!checked_multiply(static_cast<std::size_t>(width), static_cast<std::size_t>(height), &sample_count)) {
        return "Software stencil buffer dimensions overflowed size_t.";
    }

    internal::SoftwareStencilBuffer buffer;
    buffer.width = width;
    buffer.height = height;
    buffer.origin_x = origin_x;
    buffer.origin_y = origin_y;
    buffer.pixel_size = pixel_size;
    buffer.values.assign(sample_count, 0);
    *buffer_out = std::move(buffer);
    return std::nullopt;
}

std::optional<std::string> apply_software_stencil_clip_impl(
    const std::vector<RenderPoint>& polygon,
    const internal::SoftwareStencilClipState& clip_state,
    bool restoring_parent_reference,
    internal::SoftwareStencilBuffer* buffer) {
    if (buffer == nullptr) {
        return "Software stencil buffer must not be null.";
    }
    if (polygon.size() < 3) {
        return "Stencil clip polygons require at least 3 points.";
    }

    const std::uint8_t required_reference =
        restoring_parent_reference ? clip_state.reference_value : clip_state.parent_reference_value;
    for (int y = 0; y < buffer->height; ++y) {
        for (int x = 0; x < buffer->width; ++x) {
            const std::size_t sample_index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer->width) +
                static_cast<std::size_t>(x);
            if (buffer->values[sample_index] != required_reference) {
                continue;
            }

            if (!point_inside_triangle_fan_even_odd(
                    polygon,
                    software_stencil_sample_point(*buffer, x, y))) {
                continue;
            }

            buffer->values[sample_index] ^= clip_state.invert_mask;
        }
    }

    return std::nullopt;
}

std::size_t count_software_stencil_visible_pixels_impl(
    const internal::SoftwareStencilBuffer& buffer,
    const std::vector<RenderPoint>& polygon,
    std::optional<std::uint8_t> required_reference) {
    if (polygon.size() < 3) {
        return 0;
    }

    std::size_t visible_pixels = 0;
    for (int y = 0; y < buffer.height; ++y) {
        for (int x = 0; x < buffer.width; ++x) {
            const std::size_t sample_index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(buffer.width) +
                static_cast<std::size_t>(x);
            if (required_reference.has_value() && buffer.values[sample_index] != *required_reference) {
                continue;
            }
            if (point_inside_triangle_fan_even_odd(
                    polygon,
                    software_stencil_sample_point(buffer, x, y))) {
                ++visible_pixels;
            }
        }
    }

    return visible_pixels;
}

double edge_cross(
    const RenderPoint& edge_start,
    const RenderPoint& edge_end,
    const RenderPoint& point) {
    return ((edge_end.x - edge_start.x) * (point.y - edge_start.y)) -
        ((edge_end.y - edge_start.y) * (point.x - edge_start.x));
}

bool inside_clip_edge(
    const RenderPoint& edge_start,
    const RenderPoint& edge_end,
    const RenderPoint& point,
    double orientation_sign) {
    constexpr double kTolerance = 1e-9;
    const double cross = edge_cross(edge_start, edge_end, point);
    return orientation_sign >= 0.0 ? cross >= -kTolerance : cross <= kTolerance;
}

ClipVertex intersect_clip_edge(
    const ClipVertex& start,
    const ClipVertex& end,
    const RenderPoint& edge_start,
    const RenderPoint& edge_end) {
    const double dx = end.position.x - start.position.x;
    const double dy = end.position.y - start.position.y;
    const double edge_dx = edge_end.x - edge_start.x;
    const double edge_dy = edge_end.y - edge_start.y;
    const double denominator = (dx * edge_dy) - (dy * edge_dx);

    double t = 0.0;
    if (std::abs(denominator) > 1e-9) {
        const double start_to_edge_x = edge_start.x - start.position.x;
        const double start_to_edge_y = edge_start.y - start.position.y;
        t = ((start_to_edge_x * edge_dy) - (start_to_edge_y * edge_dx)) / denominator;
        t = std::clamp(t, 0.0, 1.0);
    }

    return {
        {
            start.position.x + (dx * t),
            start.position.y + (dy * t),
        },
        {
            start.uv.x + ((end.uv.x - start.uv.x) * t),
            start.uv.y + ((end.uv.y - start.uv.y) * t),
        }};
}

std::vector<ClipVertex> clip_polygon_against_convex_polygon(
    const std::vector<ClipVertex>& subject_polygon,
    const std::vector<RenderPoint>& clip_polygon) {
    if (subject_polygon.empty() || clip_polygon.size() < 3) {
        return {};
    }

    std::vector<ClipVertex> output = subject_polygon;
    const double orientation_sign = polygon_signed_area(clip_polygon);
    for (std::size_t edge_index = 0; edge_index < clip_polygon.size() && !output.empty();
         ++edge_index) {
        const RenderPoint& edge_start = clip_polygon[edge_index];
        const RenderPoint& edge_end = clip_polygon[(edge_index + 1) % clip_polygon.size()];
        std::vector<ClipVertex> input = std::move(output);
        output.clear();

        ClipVertex previous = input.back();
        bool previous_inside =
            inside_clip_edge(edge_start, edge_end, previous.position, orientation_sign);
        for (const ClipVertex& current : input) {
            const bool current_inside =
                inside_clip_edge(edge_start, edge_end, current.position, orientation_sign);
            if (current_inside != previous_inside) {
                output.push_back(intersect_clip_edge(previous, current, edge_start, edge_end));
            }
            if (current_inside) {
                output.push_back(current);
            }

            previous = current;
            previous_inside = current_inside;
        }
    }

    return output;
}

template <typename Vertex>
std::vector<std::size_t> triangle_fan_indices(const std::vector<Vertex>& vertices) {
    std::vector<std::size_t> indices;
    if (vertices.size() < 3) {
        return indices;
    }

    indices.reserve((vertices.size() - 2) * 3);
    for (std::size_t index = 1; index + 1 < vertices.size(); ++index) {
        indices.push_back(0);
        indices.push_back(index);
        indices.push_back(index + 1);
    }

    return indices;
}

GpuSkinningEvaluationResult evaluate_skinned_vertices(
    const DynamicMeshDrawCommand& attachment,
    const std::vector<runtime::BoneWorldTransform>& bone_palette) {
    GpuSkinningEvaluationResult result;
    result.vertices.reserve(attachment.vertex_payloads.size());

    for (std::size_t vertex_index = 0; vertex_index < attachment.vertex_payloads.size();
         ++vertex_index) {
        const GpuSkinningVertexPayload& payload = attachment.vertex_payloads[vertex_index];
        SkinnedMeshVertex vertex;
        vertex.uv = payload.uv;

        for (std::size_t influence_index = 0; influence_index < payload.influence_count;
             ++influence_index) {
            const std::size_t bone_index = payload.bone_indices[influence_index];
            if (bone_index >= bone_palette.size()) {
                result.vertices.clear();
                result.error_message =
                    "attachment '" + attachment.attachment_name +
                    "' vertex " + std::to_string(vertex_index) +
                    " influence " + std::to_string(influence_index) +
                    " references invalid bone index " + std::to_string(bone_index) +
                    " (bone palette=" + std::to_string(bone_palette.size()) + ")";
                return result;
            }

            const RenderPoint transformed = transform_point(
                bone_palette[bone_index],
                payload.bone_local_positions[influence_index]);
            vertex.position.x += transformed.x * payload.bone_weights[influence_index];
            vertex.position.y += transformed.y * payload.bone_weights[influence_index];
        }

        result.vertices.push_back(std::move(vertex));
    }

    return result;
}

void apply_region_mask_geometry(
    RegionAttachmentDrawCommand* command,
    const std::vector<ActiveClipState>& active_clips) {
    command->masked_vertices.assign(command->vertices.begin(), command->vertices.end());
    command->masked_indices = {0, 1, 2, 0, 2, 3};

    if (active_clips.empty()) {
        return;
    }

    command->clip_attachment_name = active_clips.back().attachment_name;
    std::vector<ClipVertex> clipped{
        ClipVertex{command->vertices[0].position, command->vertices[0].uv},
        ClipVertex{command->vertices[1].position, command->vertices[1].uv},
        ClipVertex{command->vertices[2].position, command->vertices[2].uv},
        ClipVertex{command->vertices[3].position, command->vertices[3].uv},
    };
    for (const ActiveClipState& active_clip : active_clips) {
        if (!polygon_is_convex(active_clip.polygon)) {
            return;
        }
        clipped = clip_polygon_against_convex_polygon(clipped, active_clip.polygon);
    }

    command->masked_vertices.clear();
    command->masked_vertices.reserve(clipped.size());
    for (const ClipVertex& vertex : clipped) {
        command->masked_vertices.push_back({vertex.position, vertex.uv});
    }
    command->masked_indices = triangle_fan_indices(command->masked_vertices);
}

std::optional<std::string> apply_dynamic_mesh_mask_geometry(
    DynamicMeshDrawCommand* command,
    const std::vector<runtime::BoneWorldTransform>& bone_palette,
    const std::vector<ActiveClipState>& active_clips) {
    GpuSkinningEvaluationResult skinned = evaluate_skinned_vertices(*command, bone_palette);
    if (skinned.error_message.has_value()) {
        return skinned.error_message;
    }

    command->masked_vertices = std::move(skinned.vertices);
    command->masked_indices = command->indices;

    if (active_clips.empty()) {
        return std::nullopt;
    }

    command->clip_attachment_name = active_clips.back().attachment_name;
    for (const ActiveClipState& active_clip : active_clips) {
        if (!polygon_is_convex(active_clip.polygon)) {
            return std::nullopt;
        }
    }
    std::vector<SkinnedMeshVertex> clipped_vertices;
    std::vector<std::size_t> clipped_indices;
    for (std::size_t index = 0; index + 2 < command->indices.size(); index += 3) {
        if (command->indices[index] >= command->masked_vertices.size() ||
            command->indices[index + 1] >= command->masked_vertices.size() ||
            command->indices[index + 2] >= command->masked_vertices.size()) {
            continue;
        }

        const std::vector<ClipVertex> subject_triangle{
            ClipVertex{
                command->masked_vertices[command->indices[index]].position,
                command->masked_vertices[command->indices[index]].uv},
            ClipVertex{
                command->masked_vertices[command->indices[index + 1]].position,
                command->masked_vertices[command->indices[index + 1]].uv},
            ClipVertex{
                command->masked_vertices[command->indices[index + 2]].position,
                command->masked_vertices[command->indices[index + 2]].uv},
        };
        std::vector<ClipVertex> clipped_triangle = subject_triangle;
        for (const ActiveClipState& active_clip : active_clips) {
            clipped_triangle = clip_polygon_against_convex_polygon(
                clipped_triangle,
                active_clip.polygon);
        }
        if (clipped_triangle.size() < 3) {
            continue;
        }

        const std::size_t base_index = clipped_vertices.size();
        for (const ClipVertex& vertex : clipped_triangle) {
            clipped_vertices.push_back({vertex.position, vertex.uv});
        }
        const std::vector<std::size_t> fan = triangle_fan_indices(clipped_triangle);
        for (const std::size_t clipped_index : fan) {
            clipped_indices.push_back(base_index + clipped_index);
        }
    }

    command->masked_vertices = std::move(clipped_vertices);
    command->masked_indices = std::move(clipped_indices);
    return std::nullopt;
}

std::optional<std::string> append_dynamic_mesh_attachment(
    PreparedScene* scene,
    const runtime::AttachmentData& attachment,
    const runtime::SlotData& slot,
    const runtime::SlotState& slot_state,
    std::size_t draw_order_index,
    std::size_t slot_index,
    const std::vector<double>* vertex_offsets,
    const runtime::AtlasRegion& region,
    const runtime::AtlasData& atlas,
    const std::vector<runtime::BoneWorldTransform>& bone_palette,
    const std::vector<ActiveClipState>& active_clips) {
    if (attachment.mesh_geometry == nullptr) {
        return slot_error(slot_index, "mesh attachment is missing geometry");
    }

    const runtime::MeshGeometry& geometry = *attachment.mesh_geometry;
    if (geometry.vertices.size() % 2 != 0) {
        return slot_error(slot_index, "mesh vertices must contain x/y pairs");
    }

    const std::size_t vertex_count = geometry.vertices.size() / 2;
    if (vertex_count == 0) {
        return slot_error(slot_index, "mesh attachments require at least one vertex");
    }
    if (geometry.uvs.size() != geometry.vertices.size()) {
        return slot_error(slot_index, "mesh uv coordinates must match the vertex count");
    }
    if (geometry.weights.size() != vertex_count) {
        return slot_error(slot_index, "mesh weights must match the vertex count");
    }
    if (vertex_offsets != nullptr && vertex_offsets->size() != geometry.vertices.size()) {
        return slot_error(slot_index, "mesh deform offsets must align with the vertex count");
    }

    DynamicMeshDrawCommand command;
    command.slot_name = slot.name;
    command.attachment_name = attachment.name;
    command.atlas_region_name = region.name;
    command.texture_name = scene->atlas_image;
    command.slot_index = slot_index;
    command.draw_order_index = draw_order_index;
    command.blend_mode = slot.blend_mode;
    command.color = slot_state.color;
    command.dark_color = slot_state.dark_color;
    command.indices = geometry.triangles;
    command.vertex_payloads.reserve(vertex_count);

    for (const std::size_t triangle_index : command.indices) {
        if (triangle_index >= vertex_count) {
            return slot_error(slot_index, "mesh triangle indices exceed the uploaded vertex count");
        }
    }

    for (std::size_t vertex_index = 0; vertex_index < vertex_count; ++vertex_index) {
        const runtime::MeshGeometry::VertexWeights& vertex_weights = geometry.weights[vertex_index];
        if (vertex_weights.influences.empty()) {
            return slot_error(slot_index, "mesh vertices must preserve at least one bone influence");
        }
        if (vertex_weights.influences.size() > kMaxGpuSkinningInfluences) {
            return slot_error(slot_index, "mesh vertices exceed the 4-bone GPU skinning limit");
        }
        const double total_weight = mesh_weight_sum(vertex_weights);
        constexpr double kWeightNormalizationTolerance = 1e-6;
        assert(
            std::abs(total_weight - 1.0) <= kWeightNormalizationTolerance &&
            "mesh weights must be normalized by the runtime loader before rendering");
        if (std::abs(total_weight - 1.0) > kWeightNormalizationTolerance) {
            return slot_error(
                slot_index,
                "mesh weights must be normalized by the runtime loader before rendering");
        }

        GpuSkinningVertexPayload payload;
        payload.influence_count = vertex_weights.influences.size();
        payload.uv = mesh_uv(
            geometry.uvs[(vertex_index * 2) + 0],
            geometry.uvs[(vertex_index * 2) + 1],
            region,
            atlas);

        for (std::size_t influence_index = 0; influence_index < vertex_weights.influences.size();
             ++influence_index) {
            const runtime::MeshGeometry::VertexWeight& influence =
                vertex_weights.influences[influence_index];
            if (influence.bone_index >= bone_palette.size()) {
                return slot_error(slot_index, "mesh influence references a missing bone palette entry");
            }

            const double offset_x = vertex_offsets != nullptr ? (*vertex_offsets)[vertex_index * 2] : 0.0;
            const double offset_y =
                vertex_offsets != nullptr ? (*vertex_offsets)[(vertex_index * 2) + 1] : 0.0;
            payload.bone_local_positions[influence_index] = {
                influence.x + offset_x,
                influence.y + offset_y};
            payload.bone_indices[influence_index] = influence.bone_index;
            payload.bone_weights[influence_index] = influence.weight;
        }

        command.vertex_payloads.push_back(std::move(payload));
    }

    if (const std::optional<std::string> error =
            apply_dynamic_mesh_mask_geometry(&command, bone_palette, active_clips)) {
        return slot_error(slot_index, *error);
    }

    scene->draw_commands.push_back(std::move(command));
    return std::nullopt;
}

} // namespace

namespace internal {

std::optional<SoftwareStencilClipState> stencil_clip_state_for_depth(std::size_t nesting_depth) {
    return stencil_clip_state_for_depth_impl(nesting_depth);
}

std::optional<std::string> initialize_software_stencil_buffer(
    int width,
    int height,
    double origin_x,
    double origin_y,
    double pixel_size,
    SoftwareStencilBuffer* buffer_out) {
    return initialize_software_stencil_buffer_impl(
        width,
        height,
        origin_x,
        origin_y,
        pixel_size,
        buffer_out);
}

std::optional<std::string> apply_software_stencil_clip_push(
    const std::vector<RenderPoint>& polygon,
    const SoftwareStencilClipState& clip_state,
    SoftwareStencilBuffer* buffer) {
    return apply_software_stencil_clip_impl(polygon, clip_state, false, buffer);
}

std::optional<std::string> apply_software_stencil_clip_pop(
    const std::vector<RenderPoint>& polygon,
    const SoftwareStencilClipState& clip_state,
    SoftwareStencilBuffer* buffer) {
    return apply_software_stencil_clip_impl(polygon, clip_state, true, buffer);
}

std::size_t count_software_stencil_visible_pixels(
    const SoftwareStencilBuffer& buffer,
    const std::vector<RenderPoint>& polygon,
    std::optional<std::uint8_t> required_reference) {
    return count_software_stencil_visible_pixels_impl(buffer, polygon, required_reference);
}

} // namespace internal

BlendState blend_state_for(runtime::BlendMode blend_mode, bool premultiplied_alpha) {
    return compute_blend_state(blend_mode, premultiplied_alpha);
}

const RegionAttachmentDrawCommand* region_attachment_command(const PreparedDrawCommand& command) {
    return std::get_if<RegionAttachmentDrawCommand>(&command);
}

const DynamicMeshDrawCommand* dynamic_mesh_attachment_command(const PreparedDrawCommand& command) {
    return std::get_if<DynamicMeshDrawCommand>(&command);
}

TextureImageLoadResult load_png_texture_or_white(const std::filesystem::path& image_path) {
    if (image_path.empty()) {
        return fallback_texture_result(
            image_path,
            "Atlas image path was empty.");
    }

    std::vector<std::uint8_t> png_bytes;
    if (const std::optional<std::string> error = read_binary_file(image_path, &png_bytes)) {
        return fallback_texture_result(image_path, *error);
    }

    TextureImage image;
    if (const std::optional<std::string> error = decode_png_rgba8(png_bytes, &image)) {
        return fallback_texture_result(image_path, *error);
    }

    TextureImageLoadResult result;
    result.image = std::move(image);
    result.loaded_from_file = true;
    result.message =
        "Loaded atlas texture from " + image_path.string() + " (" +
        std::to_string(result.image.width) + "x" +
        std::to_string(result.image.height) + ").";
    return result;
}

PreparedSceneResult prepare_setup_pose_scene(
    const runtime::Skeleton& skeleton,
    const runtime::AtlasData& atlas) {
    PreparedSceneResult result;

    const auto& data = skeleton.data();
    const auto& slots = data->slots();
    const auto& slot_states = skeleton.slot_states();
    const auto& draw_order = skeleton.draw_order();
    const auto& bone_world_transforms = skeleton.bone_world_transforms();

    if (slot_states.size() != slots.size()) {
        result.error_message = "slot state count does not match SkeletonData slots";
        return result;
    }
    if (draw_order.size() != slots.size()) {
        result.error_message = "draw order count does not match SkeletonData slots";
        return result;
    }

    PreparedScene scene;
    scene.atlas_name = atlas.info().name;
    scene.atlas_image = atlas.info().image;
    scene.atlas_filter_min = atlas.info().filter_min;
    scene.atlas_filter_mag = atlas.info().filter_mag;
    scene.atlas_wrap_x = atlas.info().wrap_x;
    scene.atlas_wrap_y = atlas.info().wrap_y;
    scene.premultiplied_alpha = atlas.info().premultiplied_alpha;
    scene.skeleton_name = data->info().name;
    scene.bone_palette = bone_world_transforms;
    scene.clip_attachments.reserve(draw_order.size());
    scene.draw_commands.reserve(draw_order.size());
    scene.ordered_events.reserve(draw_order.size() * 2U);
    std::vector<ActiveClipState> active_clips;

    for (std::size_t draw_index = 0; draw_index < draw_order.size(); ++draw_index) {
        const std::size_t slot_index = draw_order[draw_index];
        if (slot_index >= slots.size()) {
            result.error_message = slot_error(slot_index, "is outside the skeleton slot range");
            return result;
        }

        const runtime::SlotData& slot = slots[slot_index];
        if (slot.bone_index >= bone_world_transforms.size()) {
            result.error_message =
                slot_error(slot_index, "references a bone outside the world transform buffer");
            return result;
        }

        const std::string& attachment_name = slot_states[slot_index].attachment_name;
        const auto clear_clips_if_needed = [&]() {
            while (!active_clips.empty() &&
                   active_clips.back().end_slot_index.has_value() &&
                   *active_clips.back().end_slot_index == slot_index) {
                scene.ordered_events.push_back({
                    PreparedSceneEventKind::ClipEnd,
                    active_clips.back().clip_attachment_index,
                });
                active_clips.pop_back();
            }
        };

        if (attachment_name.empty()) {
            clear_clips_if_needed();
            continue;
        }

        const runtime::AttachmentData* attachment = skeleton.current_attachment(slot_index);
        if (attachment != nullptr && attachment->kind == runtime::AttachmentKind::Clipping) {
            const std::optional<runtime::ClippingAttachmentPose> clip_pose =
                skeleton.evaluate_current_clipping_attachment(slot_index);
            if (!clip_pose.has_value()) {
                result.error_message =
                    slot_error(slot_index, "clipping attachment is missing clipping polygon data");
                return result;
            }

            ClipAttachmentDrawCommand command;
            command.slot_name = slot.name;
            command.attachment_name = clip_pose->attachment_name;
            command.slot_index = slot_index;
            command.draw_order_index = draw_index;
            command.end_slot_index = clip_pose->end_slot_index;
            command.end_slot_name = clip_pose->end_slot_name;
            command.polygon.reserve(clip_pose->polygon.size());

            ActiveClipState clip_state;
            clip_state.clip_attachment_index = scene.clip_attachments.size();
            clip_state.attachment_name = clip_pose->attachment_name;
            clip_state.end_slot_index = clip_pose->end_slot_index;
            clip_state.polygon.reserve(clip_pose->polygon.size());

            for (const runtime::AttachmentVertex& vertex : clip_pose->polygon) {
                const RenderPoint point{vertex.x, vertex.y};
                command.polygon.push_back(point);
                clip_state.polygon.push_back(point);
            }

            scene.clip_attachments.push_back(std::move(command));
            scene.ordered_events.push_back({
                PreparedSceneEventKind::ClipStart,
                clip_state.clip_attachment_index,
            });
            active_clips.push_back(std::move(clip_state));
            clear_clips_if_needed();
            continue;
        }

        if (attachment != nullptr &&
            (attachment->kind == runtime::AttachmentKind::Point ||
             attachment->kind == runtime::AttachmentKind::BoundingBox ||
             attachment->kind == runtime::AttachmentKind::Path)) {
            clear_clips_if_needed();
            continue;
        }

        const std::string_view region_name = skeleton.current_region_name(slot_index);
        const runtime::AtlasRegion* region = atlas.find_region_for_attachment(region_name);
        if (region == nullptr) {
            result.error_message = slot_error(
                slot_index,
                "attachment '" + std::string(region_name) + "' does not resolve to an atlas region");
            return result;
        }

        if (attachment != nullptr && attachment->mesh_geometry != nullptr) {
            const std::vector<double>* vertex_offsets =
                skeleton.current_mesh_vertex_offsets(slot_index);
            if (const std::optional<std::string> error = append_dynamic_mesh_attachment(
                    &scene,
                    *attachment,
                    slot,
                    slot_states[slot_index],
                    draw_index,
                    slot_index,
                    vertex_offsets,
                    *region,
                    atlas,
                    scene.bone_palette,
                    active_clips)) {
                result.error_message = *error;
                return result;
            }
            scene.ordered_events.push_back({
                PreparedSceneEventKind::Draw,
                scene.draw_commands.size() - 1U,
            });
            clear_clips_if_needed();
            continue;
        }

        const runtime::BoneWorldTransform& bone_world = bone_world_transforms[slot.bone_index];
        const std::array<RenderPoint, 4> local_corners{{
            {-region->origin_x, -region->origin_y},
            {region->width - region->origin_x, -region->origin_y},
            {region->width - region->origin_x, region->height - region->origin_y},
            {-region->origin_x, region->height - region->origin_y},
        }};
        const std::array<RenderPoint, 4> uv_corners{{
            normalized_uv(region->x, region->y, atlas),
            normalized_uv(region->x + region->width, region->y, atlas),
            normalized_uv(region->x + region->width, region->y + region->height, atlas),
            normalized_uv(region->x, region->y + region->height, atlas),
        }};

        RegionAttachmentDrawCommand command;
        command.slot_name = slot.name;
        command.attachment_name = attachment_name;
        command.atlas_region_name = region->name;
        command.texture_name = scene.atlas_image;
        command.slot_index = slot_index;
        command.draw_order_index = draw_index;
        command.bone_index = slot.bone_index;
        command.blend_mode = slot.blend_mode;
        command.color = slot_states[slot_index].color;
        command.dark_color = slot_states[slot_index].dark_color;

        for (std::size_t vertex_index = 0; vertex_index < command.vertices.size(); ++vertex_index) {
            command.local_positions[vertex_index] = local_corners[vertex_index];
            command.vertices[vertex_index].position =
                transform_point(bone_world, local_corners[vertex_index]);
            command.vertices[vertex_index].uv = uv_corners[vertex_index];
        }

        apply_region_mask_geometry(&command, active_clips);
        scene.draw_commands.push_back(std::move(command));
        scene.ordered_events.push_back({
            PreparedSceneEventKind::Draw,
            scene.draw_commands.size() - 1U,
        });
        clear_clips_if_needed();
    }

    while (!active_clips.empty()) {
        scene.ordered_events.push_back({
            PreparedSceneEventKind::ClipEnd,
            active_clips.back().clip_attachment_index,
        });
        active_clips.pop_back();
    }

    result.scene = std::move(scene);
    return result;
}

GpuSkinningEvaluationResult evaluate_gpu_skinned_vertices(
    const DynamicMeshDrawCommand& attachment,
    const std::vector<runtime::BoneWorldTransform>& bone_palette) {
    return evaluate_skinned_vertices(attachment, bone_palette);
}

PreparedSceneBatchSummary summarize_prepared_scene_batches(const PreparedScene& scene) {
    PreparedSceneBatchSummary summary;
    const StreamBuildResult stream =
        build_stream_batches(scene, GeometryClipMode::UsePreparedMaskedGeometry);
    if (!stream) {
        summary.error_message = stream.error_message;
        return summary;
    }

    summary.draw_command_count = 0;
    for (const PreparedStreamBatch& batch : stream.batches) {
        PreparedDrawBatch public_batch;
        public_batch.texture_name = batch.texture_name;
        public_batch.blend_mode = batch.blend_mode;
        public_batch.shader_variant = batch.shader_variant;
        public_batch.draw_command_offset = batch.draw_command_offset;
        public_batch.draw_command_count = batch.draw_command_count;
        public_batch.vertex_count = batch.vertex_count;
        public_batch.index_count = batch.index_count;
        summary.draw_command_count += batch.draw_command_count;
        summary.batches.push_back(std::move(public_batch));
    }

    summary.vertex_count = stream.vertices.size();
    summary.index_count = stream.indices.size();
    summary.draw_call_count = stream.batches.size();
    summary.merged_draw_calls =
        summary.draw_command_count >= summary.draw_call_count
            ? (summary.draw_command_count - summary.draw_call_count)
            : 0;
    summary.bone_uniform_count = scene.draw_commands.empty()
        ? 0
        : scene.bone_palette.size() + kIdentityBoneCount;
    summary.vertex_buffer_bytes = summary.vertex_count * sizeof(GlStreamVertex);
    summary.index_buffer_bytes = summary.index_count * sizeof(std::uint32_t);
    return summary;
}

DemoShell::DemoShell(
    SampleAppWindow window,
    PreparedScene scene,
    std::filesystem::path atlas_image_path)
    : window_(std::move(window)),
      scene_(std::move(scene)),
      atlas_image_path_(std::move(atlas_image_path)) {}

std::string DemoShell::launch_report() const {
    std::size_t region_attachment_count = 0;
    std::size_t dynamic_mesh_attachment_count = 0;
    for (const PreparedDrawCommand& command : scene_.draw_commands) {
        if (region_attachment_command(command) != nullptr) {
            ++region_attachment_count;
            continue;
        }
        if (dynamic_mesh_attachment_command(command) != nullptr) {
            ++dynamic_mesh_attachment_count;
        }
    }

    const PreparedSceneBatchSummary batch_summary = summarize_prepared_scene_batches(scene_);
    std::ostringstream stream;
    stream << validation_target_name() << " launching " << component_name() << '\n'
           << "window: " << window_.title << " (" << window_.width << "x"
           << window_.height << ")\n"
           << "prepared scene: skeleton=" << scene_.skeleton_name
           << ", atlas=" << scene_.atlas_name
           << ", image=" << scene_.atlas_image
           << ", filter=(" << scene_.atlas_filter_min << ", " << scene_.atlas_filter_mag << ")"
           << ", wrap=(" << scene_.atlas_wrap_x << ", " << scene_.atlas_wrap_y << ")"
           << ", premultipliedAlpha=" << (scene_.premultiplied_alpha ? "true" : "false");
    if (!atlas_image_path_.empty()) {
        stream << '\n' << "atlas texture path: " << atlas_image_path_.string();
    }
    stream << '\n'
           << "bone palette entries: " << scene_.bone_palette.size() << '\n'
           << "clip attachments: " << scene_.clip_attachments.size() << '\n'
           << "region attachments: " << region_attachment_count << '\n'
           << "dynamic mesh attachments: " << dynamic_mesh_attachment_count << '\n'
           << "draw commands: " << scene_.draw_commands.size();
    if (batch_summary) {
        stream << '\n'
               << "streaming batches: drawCalls=" << batch_summary.draw_call_count
               << ", mergedCommands=" << batch_summary.merged_draw_calls
               << ", streamedVertices=" << batch_summary.vertex_count
               << ", streamedIndices=" << batch_summary.index_count
               << ", vertexBytes=" << batch_summary.vertex_buffer_bytes
               << ", indexBytes=" << batch_summary.index_buffer_bytes
               << ", mat3x2Bones=" << batch_summary.bone_uniform_count;
        for (std::size_t batch_index = 0; batch_index < batch_summary.batches.size(); ++batch_index) {
            const PreparedDrawBatch& batch = batch_summary.batches[batch_index];
            stream << '\n'
                   << "batch[" << batch_index << "] texture=" << batch.texture_name
                   << ", blend=" << blend_mode_name(batch.blend_mode)
                   << ", shader=" << color_shader_variant_name(batch.shader_variant)
                   << ", commands=" << batch.draw_command_count;
        }
    } else if (batch_summary.error_message.has_value()) {
        stream << '\n' << "streaming batches: error=" << *batch_summary.error_message;
    }

    for (const ClipAttachmentDrawCommand& attachment : scene_.clip_attachments) {
        stream << '\n'
               << "clip[" << attachment.slot_index << "] " << attachment.slot_name
               << " -> attachment=" << attachment.attachment_name
               << ", end="
               << (attachment.end_slot_name.empty() ? std::string("<none>") : attachment.end_slot_name)
               << ", vertices=" << attachment.polygon.size();
    }

    for (const PreparedDrawCommand& command : scene_.draw_commands) {
        if (const RegionAttachmentDrawCommand* attachment = region_attachment_command(command)) {
            const RegionAttachmentVertex& min_corner = attachment->vertices[0];
            const RegionAttachmentVertex& max_corner = attachment->vertices[2];
            stream << '\n'
                   << "slot[" << attachment->slot_index << "] " << attachment->slot_name
                   << " -> attachment=" << attachment->attachment_name
                   << ", region=" << attachment->atlas_region_name
                   << ", bone=" << attachment->bone_index
                   << ", blend=" << blend_mode_name(attachment->blend_mode)
                   << ", rgba=(" << attachment->color.r << ", " << attachment->color.g << ", "
                   << attachment->color.b << ", " << attachment->color.a << ")";
            if (attachment->dark_color.has_value()) {
                stream << ", darkRgba=(" << attachment->dark_color->r << ", "
                       << attachment->dark_color->g << ", " << attachment->dark_color->b << ", "
                       << attachment->dark_color->a << ")";
            } else {
                stream << ", darkRgba=(disabled)";
            }
            stream
                   << ", quadMin=(" << min_corner.position.x << ", " << min_corner.position.y << ")"
                   << ", quadMax=(" << max_corner.position.x << ", " << max_corner.position.y << ")"
                   << ", maskedVertices=" << attachment->masked_vertices.size()
                   << ", maskedTriangles=" << (attachment->masked_indices.size() / 3);
            if (attachment->clip_attachment_name.has_value()) {
                stream << ", clip=" << *attachment->clip_attachment_name;
            }
            continue;
        }

        const DynamicMeshDrawCommand& attachment =
            *dynamic_mesh_attachment_command(command);
        const GpuSkinningEvaluationResult skinned_result =
            evaluate_gpu_skinned_vertices(attachment, scene_.bone_palette);
        const std::vector<SkinnedMeshVertex>& skinned_vertices = skinned_result.vertices;

        double min_x = 0.0;
        double min_y = 0.0;
        double max_x = 0.0;
        double max_y = 0.0;
        if (!skinned_vertices.empty()) {
            min_x = max_x = skinned_vertices.front().position.x;
            min_y = max_y = skinned_vertices.front().position.y;
            for (const SkinnedMeshVertex& vertex : skinned_vertices) {
                min_x = std::min(min_x, vertex.position.x);
                min_y = std::min(min_y, vertex.position.y);
                max_x = std::max(max_x, vertex.position.x);
                max_y = std::max(max_y, vertex.position.y);
            }
        }

        stream << '\n'
               << "slot[" << attachment.slot_index << "] " << attachment.slot_name
               << " -> attachment=" << attachment.attachment_name
               << ", region=" << attachment.atlas_region_name
               << ", buffer=" << mesh_buffer_usage_name(attachment.vertex_buffer_usage)
               << ", shader=" << mesh_shader_path_name(attachment.shader_path)
               << ", blend=" << blend_mode_name(attachment.blend_mode)
               << ", rgba=(" << attachment.color.r << ", " << attachment.color.g << ", "
               << attachment.color.b << ", " << attachment.color.a << ")";
        if (attachment.dark_color.has_value()) {
            stream << ", darkRgba=(" << attachment.dark_color->r << ", "
                   << attachment.dark_color->g << ", " << attachment.dark_color->b << ", "
                   << attachment.dark_color->a << ")";
        } else {
            stream << ", darkRgba=(disabled)";
        }
        stream
               << ", vertices=" << attachment.vertex_payloads.size()
               << ", triangles=" << (attachment.indices.size() / 3)
               << ", maskedVertices=" << attachment.masked_vertices.size()
               << ", maskedTriangles=" << (attachment.masked_indices.size() / 3)
               << ", boundsMin=(" << min_x << ", " << min_y << ")"
               << ", boundsMax=(" << max_x << ", " << max_y << ")";
        if (skinned_result.error_message.has_value()) {
            stream << ", error=" << *skinned_result.error_message;
        }
        if (attachment.clip_attachment_name.has_value()) {
            stream << ", clip=" << *attachment.clip_attachment_name;
        }
    }

    return stream.str();
}

std::optional<std::string> DemoShell::run(std::optional<int> auto_close_frames) const {
    if (scene_.draw_commands.empty()) {
        return "Prepared scene does not contain any attachments to render.";
    }

    glfwSetErrorCallback([](int error_code, const char* description) {
        std::cerr << "GLFW error " << error_code << ": " << description << '\n';
    });
    glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, GLFW_FALSE);
    glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
    if (!glfwInit()) {
        return "Failed to initialize GLFW.";
    }

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GL_TRUE);
#endif
    glfwWindowHint(GLFW_STENCIL_BITS, 8);
    glfwWindowHint(GLFW_VISIBLE, auto_close_frames.has_value() ? GLFW_FALSE : GLFW_TRUE);

    GLFWwindow* window = glfwCreateWindow(
        window_.width,
        window_.height,
        window_.title.c_str(),
        nullptr,
        nullptr);
    if (window == nullptr) {
        glfwTerminate();
        return "Failed to create GLFW window.";
    }

    glfwMakeContextCurrent(window);
    glfwSwapInterval(auto_close_frames.has_value() ? 0 : 1);

    const int stencil_bits = glfwGetWindowAttrib(window, GLFW_STENCIL_BITS);
    if (stencil_bits < 8) {
        glfwDestroyWindow(window);
        glfwTerminate();
        return "OpenGL context did not expose the required 8-bit stencil buffer.";
    }

    const TextureImageLoadResult texture_image = load_png_texture_or_white(atlas_image_path_);
    if (!texture_image.loaded_from_file && !texture_image.message.empty()) {
        std::cerr << texture_image.message << '\n';
    }

    GlRenderResources resources;
    if (const std::optional<std::string> error =
            initialize_resources(scene_, texture_image.image, &resources)) {
        destroy_resources(&resources);
        glfwDestroyWindow(window);
        glfwTerminate();
        return error;
    }

    glBindVertexArray(resources.vao);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, resources.texture);
    glEnable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    const SceneBounds base_bounds = scene_bounds(scene_);
    if (auto_close_frames.has_value()) {
        for (int frame_index = 0; frame_index < *auto_close_frames; ++frame_index) {
            int framebuffer_width = 0;
            int framebuffer_height = 0;
            glfwPollEvents();
            glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
            if (const std::optional<std::string> error = render_scene_frame(
                    scene_,
                    &resources,
                    base_bounds,
                    framebuffer_width,
                    framebuffer_height)) {
                destroy_resources(&resources);
                glfwDestroyWindow(window);
                glfwTerminate();
                return error;
            }
            glfwSwapBuffers(window);
        }
    } else {
        while (!glfwWindowShouldClose(window)) {
            int framebuffer_width = 0;
            int framebuffer_height = 0;
            glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);
            glfwPollEvents();
            if (const std::optional<std::string> error = render_scene_frame(
                    scene_,
                    &resources,
                    base_bounds,
                    framebuffer_width,
                    framebuffer_height)) {
                destroy_resources(&resources);
                glfwDestroyWindow(window);
                glfwTerminate();
                return error;
            }
            glfwSwapBuffers(window);
        }
    }

    glBindTexture(GL_TEXTURE_2D, 0);
    glBindVertexArray(0);
    glUseProgram(0);
    destroy_resources(&resources);
    glfwDestroyWindow(window);
    glfwTerminate();
    return std::nullopt;
}

std::string_view component_name() {
    return "marrow-renderer";
}

std::string_view validation_target_name() {
    return "marrow_renderer_sample";
}

} // namespace marrow::renderer
