#include "marrow/renderer/module.hpp"

#include "module_internal.hpp"
#include "marrow/runtime/profiler.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

#include <zlib.h>

namespace marrow::renderer {

namespace {

constexpr std::array<std::uint32_t, 6> kQuadIndices{{0, 1, 2, 0, 2, 3}};
constexpr std::array<std::uint8_t, 4> kWhiteTexel{{255, 255, 255, 255}};
constexpr std::size_t kIdentityBoneCount = 1U;
constexpr std::size_t kMaxRendererBoneUniforms = 128U;

struct SceneBounds {
    double min_x{0.0};
    double min_y{0.0};
    double max_x{0.0};
    double max_y{0.0};
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
    std::vector<RenderCommandVertex> vertices;
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

using GlyphRows = std::array<std::uint8_t, 7>;

RenderCommandVertex rigid_stream_vertex(
    const RenderPoint& position,
    const RenderPoint& uv,
    std::size_t bone_index,
    const runtime::SlotColor& light_color,
    const std::optional<runtime::SlotColor>& dark_color);

struct ActiveClipState {
    std::size_t clip_attachment_index{0};
    std::string attachment_name;
    std::optional<std::size_t> end_slot_index;
    std::vector<RenderPoint> polygon;
};

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
    const std::vector<ActiveClipState>& active_clips);
void apply_region_mask_geometry(
    RegionAttachmentDrawCommand* command,
    const std::vector<ActiveClipState>& active_clips);
std::optional<std::string> evaluate_dynamic_mesh_bounds(
    DynamicMeshDrawCommand* attachment,
    const std::vector<runtime::BoneWorldTransform>& bone_palette);

TextureImage fallback_white_texture() {
    TextureImage image;
    image.width = 1;
    image.height = 1;
    image.rgba8.assign(kWhiteTexel.begin(), kWhiteTexel.end());
    return image;
}

const GlyphRows* glyph_rows(char character) {
    static constexpr GlyphRows kGlyph0{{0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}};
    static constexpr GlyphRows kGlyph1{{0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E}};
    static constexpr GlyphRows kGlyph2{{0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}};
    static constexpr GlyphRows kGlyph3{{0x1E, 0x01, 0x01, 0x06, 0x01, 0x01, 0x1E}};
    static constexpr GlyphRows kGlyph4{{0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}};
    static constexpr GlyphRows kGlyph5{{0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}};
    static constexpr GlyphRows kGlyph6{{0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}};
    static constexpr GlyphRows kGlyph7{{0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}};
    static constexpr GlyphRows kGlyph8{{0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}};
    static constexpr GlyphRows kGlyph9{{0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}};
    static constexpr GlyphRows kGlyphA{{0x04, 0x0A, 0x11, 0x11, 0x1F, 0x11, 0x11}};
    static constexpr GlyphRows kGlyphB{{0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}};
    static constexpr GlyphRows kGlyphC{{0x0F, 0x10, 0x10, 0x10, 0x10, 0x10, 0x0F}};
    static constexpr GlyphRows kGlyphD{{0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}};
    static constexpr GlyphRows kGlyphE{{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}};
    static constexpr GlyphRows kGlyphF{{0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}};
    static constexpr GlyphRows kGlyphG{{0x0F, 0x10, 0x10, 0x17, 0x11, 0x11, 0x0F}};
    static constexpr GlyphRows kGlyphI{{0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E}};
    static constexpr GlyphRows kGlyphK{{0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}};
    static constexpr GlyphRows kGlyphL{{0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}};
    static constexpr GlyphRows kGlyphM{{0x11, 0x1B, 0x15, 0x15, 0x11, 0x11, 0x11}};
    static constexpr GlyphRows kGlyphN{{0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}};
    static constexpr GlyphRows kGlyphO{{0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
    static constexpr GlyphRows kGlyphR{{0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}};
    static constexpr GlyphRows kGlyphS{{0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}};
    static constexpr GlyphRows kGlyphT{{0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}};
    static constexpr GlyphRows kGlyphU{{0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}};
    static constexpr GlyphRows kGlyphV{{0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}};
    static constexpr GlyphRows kGlyphW{{0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0A}};
    static constexpr GlyphRows kGlyphX{{0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}};

    switch (character) {
    case '0':
        return &kGlyph0;
    case '1':
        return &kGlyph1;
    case '2':
        return &kGlyph2;
    case '3':
        return &kGlyph3;
    case '4':
        return &kGlyph4;
    case '5':
        return &kGlyph5;
    case '6':
        return &kGlyph6;
    case '7':
        return &kGlyph7;
    case '8':
        return &kGlyph8;
    case '9':
        return &kGlyph9;
    case 'A':
        return &kGlyphA;
    case 'B':
        return &kGlyphB;
    case 'C':
        return &kGlyphC;
    case 'D':
        return &kGlyphD;
    case 'E':
        return &kGlyphE;
    case 'F':
        return &kGlyphF;
    case 'G':
        return &kGlyphG;
    case 'I':
        return &kGlyphI;
    case 'K':
        return &kGlyphK;
    case 'L':
        return &kGlyphL;
    case 'M':
        return &kGlyphM;
    case 'N':
        return &kGlyphN;
    case 'O':
        return &kGlyphO;
    case 'R':
        return &kGlyphR;
    case 'S':
        return &kGlyphS;
    case 'T':
        return &kGlyphT;
    case 'U':
        return &kGlyphU;
    case 'V':
        return &kGlyphV;
    case 'W':
        return &kGlyphW;
    case 'X':
        return &kGlyphX;
    default:
        return nullptr;
    }
}

runtime::ProfilerDrawStats profiler_draw_stats(
    const PreparedSceneBatchSummary& summary) {
    runtime::ProfilerDrawStats draw_stats;
    draw_stats.skeleton_count = summary.skeleton_count;
    draw_stats.draw_calls = summary.draw_call_count;
    draw_stats.vertices = summary.vertex_count;
    draw_stats.batch_merges = summary.merged_draw_calls;
    draw_stats.break_reasons.texture_changes = summary.break_reasons.texture_changes;
    draw_stats.break_reasons.blend_changes = summary.break_reasons.blend_changes;
    draw_stats.break_reasons.clip_changes = summary.break_reasons.clip_changes;
    draw_stats.break_reasons.shader_changes = summary.break_reasons.shader_changes;
    return draw_stats;
}

void append_hud_rect(
    RenderCommand* command,
    double min_x,
    double min_y,
    double max_x,
    double max_y,
    const runtime::SlotColor& color,
    std::size_t identity_bone_index) {
    if (command == nullptr) {
        return;
    }

    const std::uint32_t base_index = static_cast<std::uint32_t>(command->vertices.size());
    command->vertices.push_back(rigid_stream_vertex(
        {min_x, min_y},
        {0.5, 0.5},
        identity_bone_index,
        color,
        std::nullopt));
    command->vertices.push_back(rigid_stream_vertex(
        {max_x, min_y},
        {0.5, 0.5},
        identity_bone_index,
        color,
        std::nullopt));
    command->vertices.push_back(rigid_stream_vertex(
        {max_x, max_y},
        {0.5, 0.5},
        identity_bone_index,
        color,
        std::nullopt));
    command->vertices.push_back(rigid_stream_vertex(
        {min_x, max_y},
        {0.5, 0.5},
        identity_bone_index,
        color,
        std::nullopt));

    for (const std::uint32_t index : kQuadIndices) {
        command->indices.push_back(base_index + index);
    }
}

void append_hud_text(
    RenderCommand* command,
    std::string_view text,
    double origin_x,
    double origin_y,
    double pixel_width,
    double pixel_height,
    const runtime::SlotColor& color,
    std::size_t identity_bone_index) {
    if (command == nullptr) {
        return;
    }

    constexpr double kGlyphColumns = 5.0;
    constexpr double kGlyphAdvance = 6.0;
    double cursor_x = origin_x;
    for (const char character : text) {
        if (character == ' ') {
            cursor_x += pixel_width * kGlyphAdvance;
            continue;
        }

        const GlyphRows* glyph = glyph_rows(character);
        if (glyph == nullptr) {
            cursor_x += pixel_width * kGlyphAdvance;
            continue;
        }

        for (std::size_t row = 0; row < glyph->size(); ++row) {
            for (std::size_t column = 0; column < static_cast<std::size_t>(kGlyphColumns); ++column) {
                const std::uint8_t bit = static_cast<std::uint8_t>(1U << (4U - column));
                if (((*glyph)[row] & bit) == 0U) {
                    continue;
                }

                const double min_x = cursor_x + (static_cast<double>(column) * pixel_width);
                const double max_x = min_x + pixel_width;
                const double max_y = origin_y - (static_cast<double>(row) * pixel_height);
                const double min_y = max_y - pixel_height;
                append_hud_rect(
                    command,
                    min_x,
                    min_y,
                    max_x,
                    max_y,
                    color,
                    identity_bone_index);
            }
        }

        cursor_x += pixel_width * kGlyphAdvance;
    }
}

std::optional<RenderCommand> build_profiler_hud_overlay_command(
    const runtime::ProfilerFrame& frame,
    const SceneBounds& bounds,
    int framebuffer_width,
    int framebuffer_height,
    std::size_t identity_bone_index) {
    const std::vector<std::string> lines = runtime::profiler_hud_lines(frame);
    if (lines.empty()) {
        return std::nullopt;
    }

    const double world_width = std::max(bounds.max_x - bounds.min_x, 1.0);
    const double world_height = std::max(bounds.max_y - bounds.min_y, 1.0);
    const double pixel_width = world_width / std::max(framebuffer_width, 1);
    const double pixel_height = world_height / std::max(framebuffer_height, 1);
    const double scale = framebuffer_width >= 960 ? 2.0 : 1.0;
    const double glyph_pixel_width = pixel_width * scale;
    const double glyph_pixel_height = pixel_height * scale;
    const double char_advance = glyph_pixel_width * 6.0;
    const double line_height = glyph_pixel_height * 10.0;
    const double margin_x = pixel_width * 16.0;
    const double margin_y = pixel_height * 16.0;
    const double padding_x = pixel_width * 8.0;
    const double padding_y = pixel_height * 8.0;

    std::size_t max_columns = 0;
    for (const std::string& line : lines) {
        max_columns = std::max(max_columns, line.size());
    }

    const double panel_width =
        padding_x * 2.0 + (static_cast<double>(max_columns) * char_advance);
    const double panel_height =
        padding_y * 2.0 + (static_cast<double>(lines.size()) * line_height);
    const double panel_min_x = bounds.min_x + margin_x;
    const double panel_max_x = panel_min_x + panel_width;
    const double panel_max_y = bounds.max_y - margin_y;
    const double panel_min_y = panel_max_y - panel_height;

    RenderCommand command;
    command.texture_name = "__hud_white__";
    command.texture_handle = kSolidWhiteTextureHandle;
    command.blend_mode = runtime::BlendMode::Normal;
    command.shader_variant = ColorShaderVariant::SingleColor;
    command.source_draw_command_offset = 0U;
    command.source_draw_command_count = 0U;

    append_hud_rect(
        &command,
        panel_min_x,
        panel_min_y,
        panel_max_x,
        panel_max_y,
        runtime::SlotColor{0.05, 0.07, 0.09, 0.80},
        identity_bone_index);

    const runtime::SlotColor text_color{0.93, 0.95, 0.88, 1.0};
    for (std::size_t line_index = 0; line_index < lines.size(); ++line_index) {
        const double line_x = panel_min_x + padding_x;
        const double line_y =
            panel_max_y - padding_y - (static_cast<double>(line_index) * line_height);
        append_hud_text(
            &command,
            lines[line_index],
            line_x,
            line_y,
            glyph_pixel_width,
            glyph_pixel_height,
            text_color,
            identity_bone_index);
    }

    return command.vertices.empty() ? std::nullopt : std::optional<RenderCommand>(std::move(command));
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
    bounds.min_x = attachment.bounds_min.x;
    bounds.min_y = attachment.bounds_min.y;
    bounds.max_x = attachment.bounds_max.x;
    bounds.max_y = attachment.bounds_max.y;
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
                    if (!attachment.has_bounds) {
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

std::string_view color_shader_variant_name(ColorShaderVariant shader_variant) {
    switch (shader_variant) {
    case ColorShaderVariant::SingleColor:
        return "single_color";
    case ColorShaderVariant::TwoColorTint:
        return "two_color_tint";
    }

    return "unknown";
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

std::uint8_t color_component_byte(double component) {
    const double clamped = std::clamp(component, 0.0, 1.0);
    return static_cast<std::uint8_t>(std::lround(clamped * 255.0));
}

void set_stream_vertex_colors(
    RenderCommandVertex* vertex,
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

ColorShaderVariant command_shader_variant(const PreparedDrawCommand& command) {
    return std::visit(
        [](const auto& attachment) {
            return attachment.dark_color.has_value()
                ? ColorShaderVariant::TwoColorTint
                : ColorShaderVariant::SingleColor;
        },
        command);
}

RenderCommandVertex rigid_stream_vertex(
    const RenderPoint& position,
    const RenderPoint& uv,
    std::size_t bone_index,
    const runtime::SlotColor& color,
    const std::optional<runtime::SlotColor>& dark_color) {
    RenderCommandVertex vertex{};
    vertex.local_positions[0][0] = static_cast<float>(position.x);
    vertex.local_positions[0][1] = static_cast<float>(position.y);
    vertex.uv[0] = static_cast<float>(uv.x);
    vertex.uv[1] = static_cast<float>(uv.y);
    vertex.bone_indices[0] = static_cast<float>(bone_index);
    vertex.bone_weights[0] = 1.0f;
    set_stream_vertex_colors(&vertex, color, dark_color);
    return vertex;
}

RenderCommandVertex weighted_stream_vertex(
    const GpuSkinningVertexPayload& payload,
    const RenderPoint* deform_offset,
    const runtime::SlotColor& color,
    const std::optional<runtime::SlotColor>& dark_color) {
    RenderCommandVertex vertex{};
    vertex.uv[0] = static_cast<float>(payload.uv.x);
    vertex.uv[1] = static_cast<float>(payload.uv.y);
    for (std::size_t influence_index = 0; influence_index < payload.influence_count;
         ++influence_index) {
        const double offset_x = deform_offset != nullptr ? deform_offset->x : 0.0;
        const double offset_y = deform_offset != nullptr ? deform_offset->y : 0.0;
        vertex.local_positions[influence_index][0] =
            static_cast<float>(payload.bone_local_positions[influence_index].x + offset_x);
        vertex.local_positions[influence_index][1] =
            static_cast<float>(payload.bone_local_positions[influence_index].y + offset_y);
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
    std::vector<RenderCommandVertex>* vertices_out,
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
    std::vector<RenderCommandVertex>* vertices_out,
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
    if (!attachment.deform_offsets.empty() &&
        attachment.deform_offsets.size() != attachment.vertex_payloads.size()) {
        return "attachment '" + attachment.attachment_name +
            "' deform offset payload did not match the weighted vertex count";
    }
    for (std::size_t vertex_index = 0; vertex_index < attachment.vertex_payloads.size();
         ++vertex_index) {
        const GpuSkinningVertexPayload& payload = attachment.vertex_payloads[vertex_index];
        for (std::size_t influence_index = 0; influence_index < payload.influence_count;
             ++influence_index) {
            if (payload.bone_indices[influence_index] >= bone_count) {
                return "attachment '" + attachment.attachment_name +
                    "' references invalid weighted bone index " +
                    std::to_string(payload.bone_indices[influence_index]);
            }
        }
        const RenderPoint* deform_offset =
            attachment.deform_offsets.empty() ? nullptr : &attachment.deform_offsets[vertex_index];
        vertices_out->push_back(weighted_stream_vertex(
            payload,
            deform_offset,
            attachment.color,
            attachment.dark_color));
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
    std::vector<RenderCommandVertex>* vertices_out,
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

bool render_batch_matches(const RenderCommand& batch, const PreparedDrawCommand& command) {
    return batch.texture_name == command_texture_name(command) &&
        batch.blend_mode == command_blend_mode(command) &&
        batch.shader_variant == command_shader_variant(command);
}

std::optional<std::string> append_clip_command_geometry(
    const ClipAttachmentDrawCommand& clip_attachment,
    std::size_t identity_bone_index,
    std::size_t bone_count,
    std::size_t source_clip_attachment_index,
    RenderClipCommand* clip_command_out) {
    if (clip_command_out == nullptr) {
        return "Render clip command output must not be null.";
    }
    const bool use_local_polygon = !clip_attachment.local_polygon.empty();
    const std::vector<RenderPoint>& polygon =
        use_local_polygon ? clip_attachment.local_polygon : clip_attachment.polygon;
    const std::size_t polygon_bone_index =
        use_local_polygon ? clip_attachment.bone_index : identity_bone_index;

    if (polygon.size() < 3U) {
        return "Clip attachment '" + clip_attachment.attachment_name +
            "' requires at least 3 points.";
    }
    if (polygon.size() >
        static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
        return "Clip attachment '" + clip_attachment.attachment_name +
            "' exceeded the 32-bit index range.";
    }
    if (polygon_bone_index >= bone_count + kIdentityBoneCount) {
        return "Clip attachment '" + clip_attachment.attachment_name +
            "' references invalid clip bone index " + std::to_string(polygon_bone_index) + ".";
    }

    RenderClipCommand clip_command;
    clip_command.attachment_name = clip_attachment.attachment_name;
    clip_command.source_clip_attachment_index = source_clip_attachment_index;
    clip_command.vertices.reserve(polygon.size());
    for (const RenderPoint& point : polygon) {
        clip_command.vertices.push_back(rigid_stream_vertex(
            point,
            RenderPoint{},
            polygon_bone_index,
            runtime::SlotColor{1.0, 1.0, 1.0, 1.0},
            std::nullopt));
    }
    clip_command.indices.reserve((polygon.size() - 2U) * 3U);
    for (std::size_t index = 1; index + 1 < polygon.size(); ++index) {
        clip_command.indices.push_back(0U);
        clip_command.indices.push_back(static_cast<std::uint32_t>(index));
        clip_command.indices.push_back(static_cast<std::uint32_t>(index + 1U));
    }

    *clip_command_out = std::move(clip_command);
    return std::nullopt;
}

RenderCommandListResult build_render_command_list_impl(
    const PreparedScene& scene,
    const std::array<float, 16>& projection) {
    RenderCommandListResult result;
    const std::size_t bone_count = scene.bone_palette.size();
    if (bone_count + kIdentityBoneCount > kMaxRendererBoneUniforms) {
        result.error_message =
            "Prepared scene requires " + std::to_string(bone_count + kIdentityBoneCount) +
            " bone uniforms, but the batch renderer supports at most " +
            std::to_string(kMaxRendererBoneUniforms) + ".";
        return result;
    }

    RenderCommandList command_list;
    command_list.projection = projection;
    command_list.premultiplied_alpha = scene.premultiplied_alpha;
    command_list.bone_palette = bone_uniform_payload(scene);
    command_list.commands.reserve(scene.draw_commands.size());
    command_list.clip_commands.reserve(scene.clip_attachments.size());
    command_list.ordered_events.reserve(scene.ordered_events.size());

    const std::size_t identity_bone_index = bone_count;
    for (std::size_t clip_index = 0; clip_index < scene.clip_attachments.size(); ++clip_index) {
        RenderClipCommand clip_command;
        if (const std::optional<std::string> error = append_clip_command_geometry(
                scene.clip_attachments[clip_index],
                identity_bone_index,
                bone_count,
                clip_index,
                &clip_command)) {
            result.error_message = *error;
            return result;
        }
        command_list.clip_commands.push_back(std::move(clip_command));
    }

    std::optional<RenderCommand> pending_batch;
    auto flush_pending_batch = [&]() {
        if (!pending_batch.has_value()) {
            return;
        }
        const std::size_t draw_index = command_list.commands.size();
        command_list.commands.push_back(std::move(*pending_batch));
        command_list.ordered_events.push_back({RenderCommandEventKind::Draw, draw_index});
        pending_batch.reset();
    };

    for (const PreparedSceneEventRef& event : scene.ordered_events) {
        switch (event.kind) {
        case PreparedSceneEventKind::ClipStart:
            if (pending_batch.has_value()) {
                command_list.batch_break_reasons.clip_changes += 1U;
            }
            flush_pending_batch();
            if (event.index >= command_list.clip_commands.size()) {
                result.error_message =
                    "Prepared scene clip start event referenced a missing clip attachment.";
                return result;
            }
            command_list.ordered_events.push_back({RenderCommandEventKind::ClipStart, event.index});
            break;
        case PreparedSceneEventKind::Draw: {
            if (event.index >= scene.draw_commands.size()) {
                result.error_message =
                    "Prepared scene draw event referenced a missing draw command.";
                return result;
            }

            const PreparedDrawCommand& source_command = scene.draw_commands[event.index];
            if (!pending_batch.has_value()) {
                flush_pending_batch();
                RenderCommand batch;
                batch.texture_name = command_texture_name(source_command);
                batch.texture_handle = kAtlasTextureHandle;
                batch.blend_mode = command_blend_mode(source_command);
                batch.shader_variant = command_shader_variant(source_command);
                batch.source_draw_command_offset = event.index;
                batch.source_draw_command_count = 0U;
                pending_batch = std::move(batch);
            } else if (!render_batch_matches(*pending_batch, source_command)) {
                command_list.batch_break_reasons.texture_changes +=
                    pending_batch->texture_name != command_texture_name(source_command) ? 1U : 0U;
                command_list.batch_break_reasons.blend_changes +=
                    pending_batch->blend_mode != command_blend_mode(source_command) ? 1U : 0U;
                command_list.batch_break_reasons.shader_changes +=
                    pending_batch->shader_variant != command_shader_variant(source_command) ? 1U : 0U;
                flush_pending_batch();
                RenderCommand batch;
                batch.texture_name = command_texture_name(source_command);
                batch.texture_handle = kAtlasTextureHandle;
                batch.blend_mode = command_blend_mode(source_command);
                batch.shader_variant = command_shader_variant(source_command);
                batch.source_draw_command_offset = event.index;
                batch.source_draw_command_count = 0U;
                pending_batch = std::move(batch);
            }

            const std::size_t previous_vertex_count = pending_batch->vertices.size();
            const std::size_t previous_index_count = pending_batch->indices.size();
            if (const std::optional<std::string> error = append_draw_command_stream_geometry(
                    source_command,
                    identity_bone_index,
                    bone_count,
                    GeometryClipMode::UseOriginalGeometry,
                    &pending_batch->vertices,
                    &pending_batch->indices)) {
                result.error_message = *error;
                return result;
            }

            if (pending_batch->vertices.size() != previous_vertex_count &&
                pending_batch->indices.size() != previous_index_count) {
                pending_batch->source_draw_command_count += 1U;
            }
            break;
        }
        case PreparedSceneEventKind::ClipEnd:
            if (pending_batch.has_value()) {
                command_list.batch_break_reasons.clip_changes += 1U;
            }
            flush_pending_batch();
            if (event.index >= command_list.clip_commands.size()) {
                result.error_message =
                    "Prepared scene clip end event referenced a missing clip attachment.";
                return result;
            }
            command_list.ordered_events.push_back({RenderCommandEventKind::ClipEnd, event.index});
            break;
        }
    }

    flush_pending_batch();
    result.command_list = std::move(command_list);
    return result;
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

bool slot_color_matches(
    const runtime::SlotColor& lhs,
    const runtime::SlotColor& rhs) {
    return lhs.r == rhs.r &&
        lhs.g == rhs.g &&
        lhs.b == rhs.b &&
        lhs.a == rhs.a;
}

bool optional_slot_color_matches(
    const std::optional<runtime::SlotColor>& lhs,
    const std::optional<runtime::SlotColor>& rhs) {
    if (!lhs.has_value() || !rhs.has_value()) {
        return lhs.has_value() == rhs.has_value();
    }

    return slot_color_matches(*lhs, *rhs);
}

bool slot_snapshot_matches(
    const PreparedSceneCache::SlotSnapshot& lhs,
    const PreparedSceneCache::SlotSnapshot& rhs) {
    return lhs.has_attachment == rhs.has_attachment &&
        lhs.attachment_kind == rhs.attachment_kind &&
        lhs.attachment_name == rhs.attachment_name &&
        lhs.attachment_skin_index == rhs.attachment_skin_index &&
        lhs.region_name == rhs.region_name &&
        slot_color_matches(lhs.color, rhs.color) &&
        optional_slot_color_matches(lhs.dark_color, rhs.dark_color) &&
        lhs.mesh_deform_attachment_name == rhs.mesh_deform_attachment_name &&
        lhs.mesh_vertex_offsets == rhs.mesh_vertex_offsets &&
        lhs.clip_end_slot_index == rhs.clip_end_slot_index &&
        lhs.clip_end_slot_name == rhs.clip_end_slot_name;
}

void assign_scene_metadata(
    PreparedScene* scene,
    const runtime::Skeleton& skeleton,
    const runtime::AtlasData& atlas) {
    if (scene == nullptr) {
        return;
    }

    scene->atlas_name = atlas.info().name;
    scene->atlas_image = atlas.info().image;
    scene->atlas_filter_min = atlas.info().filter_min;
    scene->atlas_filter_mag = atlas.info().filter_mag;
    scene->atlas_wrap_x = atlas.info().wrap_x;
    scene->atlas_wrap_y = atlas.info().wrap_y;
    scene->premultiplied_alpha = atlas.info().premultiplied_alpha;
    scene->skeleton_name = skeleton.data()->info().name;
    scene->skeleton_count = 1U;
}

PreparedSceneCache::SlotSnapshot build_slot_snapshot(
    const runtime::Skeleton& skeleton,
    std::size_t slot_index) {
    PreparedSceneCache::SlotSnapshot snapshot;
    if (slot_index >= skeleton.slot_states().size()) {
        return snapshot;
    }

    const runtime::SlotState& slot_state = skeleton.slot_states()[slot_index];
    snapshot.attachment_name = slot_state.attachment_name;
    snapshot.attachment_skin_index = slot_state.attachment_skin_index;
    snapshot.color = slot_state.color;
    snapshot.dark_color = slot_state.dark_color;

    const runtime::AttachmentData* attachment = skeleton.current_attachment(slot_index);
    if (attachment == nullptr) {
        return snapshot;
    }

    snapshot.has_attachment = true;
    snapshot.attachment_kind = attachment->kind;
    snapshot.region_name = std::string(skeleton.current_region_name(slot_index));

    if (attachment->mesh_geometry != nullptr &&
        slot_index < skeleton.mesh_deform_states().size()) {
        const runtime::MeshDeformState& mesh_deform =
            skeleton.mesh_deform_states()[slot_index];
        snapshot.mesh_deform_attachment_name = mesh_deform.attachment_name;
        if (const std::vector<double>* vertex_offsets =
                skeleton.current_mesh_vertex_offsets(slot_index)) {
            snapshot.mesh_vertex_offsets = *vertex_offsets;
        }
    }

    if (attachment->clipping_attachment.has_value()) {
        snapshot.clip_end_slot_index = attachment->clipping_attachment->end_slot_index;
        snapshot.clip_end_slot_name = attachment->clipping_attachment->end_slot_name;
    }

    return snapshot;
}

std::optional<std::string> build_slot_record(
    PreparedSceneCache::SlotRecord* record_out,
    const runtime::Skeleton& skeleton,
    const runtime::AtlasData& atlas,
    std::size_t slot_index) {
    if (record_out == nullptr) {
        return "Prepared scene cache slot-record output must not be null.";
    }

    PreparedSceneCache::SlotRecord previous_record = std::move(*record_out);
    *record_out = {};
    const auto& slots = skeleton.data()->slots();
    const auto& slot_states = skeleton.slot_states();
    const auto& bone_world_transforms = skeleton.bone_world_transforms();
    if (slot_index >= slots.size() || slot_index >= slot_states.size()) {
        return slot_error(slot_index, "is outside the skeleton slot range");
    }

    const runtime::SlotData& slot = slots[slot_index];
    const runtime::SlotState& slot_state = slot_states[slot_index];
    if (slot.bone_index >= bone_world_transforms.size()) {
        return slot_error(slot_index, "references a bone outside the world transform buffer");
    }
    if (slot_state.attachment_name.empty()) {
        return std::nullopt;
    }

    const runtime::AttachmentData* attachment = skeleton.current_attachment(slot_index);
    if (attachment == nullptr) {
        return slot_error(slot_index, "attachment could not be resolved");
    }

    if (attachment->kind == runtime::AttachmentKind::Clipping) {
        const std::optional<runtime::ClippingAttachmentPose> clip_pose =
            skeleton.evaluate_current_clipping_attachment(slot_index);
        if (!clip_pose.has_value()) {
            return slot_error(slot_index, "clipping attachment is missing clipping polygon data");
        }

        ClipAttachmentDrawCommand command;
        command.slot_name = slot.name;
        command.attachment_name = clip_pose->attachment_name;
        command.slot_index = slot_index;
        command.bone_index = slot.bone_index;
        command.end_slot_index = clip_pose->end_slot_index;
        command.end_slot_name = clip_pose->end_slot_name;
        command.polygon.reserve(clip_pose->polygon.size());
        command.local_polygon.reserve(attachment->clipping_attachment->polygon.size());

        for (const runtime::AttachmentVertex& vertex : attachment->clipping_attachment->polygon) {
            command.local_polygon.push_back({vertex.x, vertex.y});
        }
        for (const runtime::AttachmentVertex& vertex : clip_pose->polygon) {
            command.polygon.push_back({vertex.x, vertex.y});
        }

        record_out->clip_attachment = std::move(command);
        return std::nullopt;
    }

    if (attachment->kind == runtime::AttachmentKind::Point ||
        attachment->kind == runtime::AttachmentKind::BoundingBox ||
        attachment->kind == runtime::AttachmentKind::Path) {
        return std::nullopt;
    }

    const std::string_view region_name = skeleton.current_region_name(slot_index);
    const runtime::AtlasRegion* region = atlas.find_region_for_attachment(region_name);
    if (region == nullptr) {
        return slot_error(
            slot_index,
            "attachment '" + std::string(region_name) + "' does not resolve to an atlas region");
    }

    if (attachment->mesh_geometry != nullptr) {
        DynamicMeshDrawCommand* reusable_mesh = nullptr;
        if (previous_record.draw_command.has_value()) {
            reusable_mesh =
                std::get_if<DynamicMeshDrawCommand>(&(*previous_record.draw_command));
        }
        const bool reuse_static_payload =
            reusable_mesh != nullptr &&
            reusable_mesh->mesh_geometry == attachment->mesh_geometry.get() &&
            reusable_mesh->attachment_name == attachment->name &&
            reusable_mesh->atlas_region_name == region->name &&
            reusable_mesh->texture_name == atlas.info().image;

        if (!reuse_static_payload) {
            PreparedScene temporary_scene;
            temporary_scene.atlas_image = atlas.info().image;
            if (const std::optional<std::string> error = append_dynamic_mesh_attachment(
                    &temporary_scene,
                    *attachment,
                    slot,
                    slot_state,
                    0U,
                    slot_index,
                    skeleton.current_mesh_vertex_offsets(slot_index),
                    *region,
                    atlas,
                    bone_world_transforms,
                    {})) {
                return error;
            }
            if (temporary_scene.draw_commands.size() != 1U) {
                return slot_error(slot_index, "failed to build a cached mesh draw command");
            }

            record_out->draw_command = std::move(temporary_scene.draw_commands.front());
        } else {
            reusable_mesh->slot_name = slot.name;
            reusable_mesh->attachment_name = attachment->name;
            reusable_mesh->atlas_region_name = region->name;
            reusable_mesh->texture_name = atlas.info().image;
            reusable_mesh->slot_index = slot_index;
            reusable_mesh->draw_order_index = 0U;
            reusable_mesh->blend_mode = slot.blend_mode;
            reusable_mesh->color = slot_state.color;
            reusable_mesh->dark_color = slot_state.dark_color;
            reusable_mesh->clip_attachment_name.reset();
            reusable_mesh->masked_vertices.clear();
            reusable_mesh->masked_indices.clear();

            const std::vector<double>* vertex_offsets =
                skeleton.current_mesh_vertex_offsets(slot_index);
            reusable_mesh->deform_offsets.clear();
            if (vertex_offsets != nullptr) {
                reusable_mesh->deform_offsets.reserve(reusable_mesh->vertex_payloads.size());
                for (std::size_t vertex_index = 0;
                     vertex_index < reusable_mesh->vertex_payloads.size();
                     ++vertex_index) {
                    reusable_mesh->deform_offsets.push_back({
                        (*vertex_offsets)[vertex_index * 2],
                        (*vertex_offsets)[(vertex_index * 2) + 1]});
                }
                reusable_mesh->deform_buffer_usage = MeshBufferUsage::Dynamic;
            } else {
                reusable_mesh->deform_buffer_usage.reset();
            }
            if (const std::optional<std::string> error =
                    evaluate_dynamic_mesh_bounds(reusable_mesh, bone_world_transforms)) {
                return slot_error(slot_index, *error);
            }

            record_out->draw_command = std::move(previous_record.draw_command);
        }
        if (auto* mesh =
                std::get_if<DynamicMeshDrawCommand>(&(*record_out->draw_command))) {
            mesh->draw_order_index = 0U;
            mesh->clip_attachment_name.reset();
        }
        return std::nullopt;
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
    command.attachment_name = slot_state.attachment_name;
    command.atlas_region_name = region->name;
    command.texture_name = atlas.info().image;
    command.slot_index = slot_index;
    command.bone_index = slot.bone_index;
    command.blend_mode = slot.blend_mode;
    command.color = slot_state.color;
    command.dark_color = slot_state.dark_color;
    for (std::size_t vertex_index = 0; vertex_index < command.vertices.size(); ++vertex_index) {
        command.local_positions[vertex_index] = local_corners[vertex_index];
        command.vertices[vertex_index].position =
            transform_point(bone_world, local_corners[vertex_index]);
        command.vertices[vertex_index].uv = uv_corners[vertex_index];
    }

    record_out->draw_command = std::move(command);
    return std::nullopt;
}

void rebuild_cached_scene(
    PreparedSceneCache* cache,
    const runtime::Skeleton& skeleton,
    const runtime::AtlasData& atlas) {
    if (cache == nullptr) {
        return;
    }

    PreparedScene scene;
    assign_scene_metadata(&scene, skeleton, atlas);
    scene.bone_palette = skeleton.bone_world_transforms();
    scene.clip_attachments.reserve(skeleton.draw_order().size());
    scene.draw_commands.reserve(skeleton.draw_order().size());
    scene.ordered_events.reserve(skeleton.draw_order().size() * 2U);
    std::vector<ActiveClipState> active_clips;

    auto clear_clips_if_needed = [&](std::size_t slot_index) {
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

    for (std::size_t draw_index = 0; draw_index < skeleton.draw_order().size(); ++draw_index) {
        const std::size_t slot_index = skeleton.draw_order()[draw_index];
        if (slot_index >= cache->slot_records_.size()) {
            continue;
        }

        const PreparedSceneCache::SlotRecord& record = cache->slot_records_[slot_index];
        if (record.clip_attachment.has_value()) {
            ClipAttachmentDrawCommand clip = *record.clip_attachment;
            clip.draw_order_index = draw_index;
            const std::size_t clip_attachment_index = scene.clip_attachments.size();
            scene.clip_attachments.push_back(std::move(clip));
            scene.ordered_events.push_back({
                PreparedSceneEventKind::ClipStart,
                clip_attachment_index,
            });

            ActiveClipState clip_state;
            clip_state.clip_attachment_index = clip_attachment_index;
            clip_state.attachment_name = scene.clip_attachments.back().attachment_name;
            clip_state.end_slot_index = scene.clip_attachments.back().end_slot_index;
            clip_state.polygon = scene.clip_attachments.back().polygon;
            active_clips.push_back(std::move(clip_state));
            clear_clips_if_needed(slot_index);
            continue;
        }

        if (!record.draw_command.has_value()) {
            clear_clips_if_needed(slot_index);
            continue;
        }

        PreparedDrawCommand command = *record.draw_command;
        std::visit(
            [&](auto& prepared_attachment) {
                using Attachment = std::decay_t<decltype(prepared_attachment)>;
                prepared_attachment.draw_order_index = draw_index;
                if (!active_clips.empty()) {
                    prepared_attachment.clip_attachment_name = active_clips.back().attachment_name;
                } else {
                    prepared_attachment.clip_attachment_name.reset();
                }
                if constexpr (std::is_same_v<Attachment, RegionAttachmentDrawCommand>) {
                    if (!active_clips.empty()) {
                        apply_region_mask_geometry(&prepared_attachment, active_clips);
                    } else {
                        prepared_attachment.masked_vertices.clear();
                        prepared_attachment.masked_indices.clear();
                    }
                } else {
                    prepared_attachment.masked_vertices.clear();
                    prepared_attachment.masked_indices.clear();
                }
            },
            command);
        scene.draw_commands.push_back(std::move(command));
        scene.ordered_events.push_back({
            PreparedSceneEventKind::Draw,
            scene.draw_commands.size() - 1U,
        });
        clear_clips_if_needed(slot_index);
    }

    while (!active_clips.empty()) {
        scene.ordered_events.push_back({
            PreparedSceneEventKind::ClipEnd,
            active_clips.back().clip_attachment_index,
        });
        active_clips.pop_back();
    }

    cache->scene_ = std::move(scene);
    cache->has_scene_ = true;
    cache->has_batch_summary_ = false;
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

const RenderPoint* dynamic_mesh_deform_offset(
    const DynamicMeshDrawCommand& attachment,
    std::size_t vertex_index) {
    if (attachment.deform_offsets.empty()) {
        return nullptr;
    }
    if (attachment.deform_offsets.size() != attachment.vertex_payloads.size() ||
        vertex_index >= attachment.deform_offsets.size()) {
        return nullptr;
    }
    return &attachment.deform_offsets[vertex_index];
}

RenderPoint influenced_local_position(
    const GpuSkinningVertexPayload& payload,
    const RenderPoint* deform_offset,
    std::size_t influence_index) {
    RenderPoint position = payload.bone_local_positions[influence_index];
    if (deform_offset != nullptr) {
        position.x += deform_offset->x;
        position.y += deform_offset->y;
    }
    return position;
}

std::optional<std::string> evaluate_dynamic_mesh_bounds(
    DynamicMeshDrawCommand* attachment,
    const std::vector<runtime::BoneWorldTransform>& bone_palette) {
    if (attachment == nullptr) {
        return "Dynamic mesh bounds output must not be null.";
    }
    if (!attachment->deform_offsets.empty() &&
        attachment->deform_offsets.size() != attachment->vertex_payloads.size()) {
        return "attachment '" + attachment->attachment_name +
            "' deform offset payload did not match the weighted vertex count";
    }
    if (attachment->vertex_payloads.empty()) {
        attachment->has_bounds = false;
        attachment->bounds_min = {};
        attachment->bounds_max = {};
        return std::nullopt;
    }

    RenderPoint min_point{};
    RenderPoint max_point{};
    bool initialized = false;
    for (std::size_t vertex_index = 0; vertex_index < attachment->vertex_payloads.size();
         ++vertex_index) {
        const GpuSkinningVertexPayload& payload = attachment->vertex_payloads[vertex_index];
        const RenderPoint* deform_offset = dynamic_mesh_deform_offset(*attachment, vertex_index);
        RenderPoint world_position{};

        for (std::size_t influence_index = 0; influence_index < payload.influence_count;
             ++influence_index) {
            const std::size_t bone_index = payload.bone_indices[influence_index];
            if (bone_index >= bone_palette.size()) {
                return "attachment '" + attachment->attachment_name +
                    "' vertex " + std::to_string(vertex_index) +
                    " influence " + std::to_string(influence_index) +
                    " references invalid bone index " + std::to_string(bone_index) +
                    " (bone palette=" + std::to_string(bone_palette.size()) + ")";
            }

            const RenderPoint transformed = transform_point(
                bone_palette[bone_index],
                influenced_local_position(payload, deform_offset, influence_index));
            world_position.x += transformed.x * payload.bone_weights[influence_index];
            world_position.y += transformed.y * payload.bone_weights[influence_index];
        }

        if (!initialized) {
            min_point = max_point = world_position;
            initialized = true;
            continue;
        }

        min_point.x = std::min(min_point.x, world_position.x);
        min_point.y = std::min(min_point.y, world_position.y);
        max_point.x = std::max(max_point.x, world_position.x);
        max_point.y = std::max(max_point.y, world_position.y);
    }

    attachment->bounds_min = min_point;
    attachment->bounds_max = max_point;
    attachment->has_bounds = initialized;
    return std::nullopt;
}

GpuSkinningEvaluationResult evaluate_skinned_vertices(
    const DynamicMeshDrawCommand& attachment,
    const std::vector<runtime::BoneWorldTransform>& bone_palette) {
    GpuSkinningEvaluationResult result;
    if (!attachment.deform_offsets.empty() &&
        attachment.deform_offsets.size() != attachment.vertex_payloads.size()) {
        result.error_message =
            "attachment '" + attachment.attachment_name +
            "' deform offset payload did not match the weighted vertex count";
        return result;
    }
    result.vertices.reserve(attachment.vertex_payloads.size());

    for (std::size_t vertex_index = 0; vertex_index < attachment.vertex_payloads.size();
         ++vertex_index) {
        const GpuSkinningVertexPayload& payload = attachment.vertex_payloads[vertex_index];
        const RenderPoint* deform_offset = dynamic_mesh_deform_offset(attachment, vertex_index);
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
                influenced_local_position(payload, deform_offset, influence_index));
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
    command.mesh_geometry = &geometry;
    command.slot_index = slot_index;
    command.draw_order_index = draw_order_index;
    command.blend_mode = slot.blend_mode;
    command.color = slot_state.color;
    command.dark_color = slot_state.dark_color;
    command.indices = geometry.triangles;
    command.vertex_payloads.reserve(vertex_count);
    if (vertex_offsets != nullptr) {
        command.deform_offsets.reserve(vertex_count);
        command.deform_buffer_usage = MeshBufferUsage::Dynamic;
    } else {
        command.deform_buffer_usage.reset();
    }

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

            payload.bone_local_positions[influence_index] = {influence.x, influence.y};
            payload.bone_indices[influence_index] = influence.bone_index;
            payload.bone_weights[influence_index] = influence.weight;
        }

        if (vertex_offsets != nullptr) {
            command.deform_offsets.push_back({
                (*vertex_offsets)[vertex_index * 2],
                (*vertex_offsets)[(vertex_index * 2) + 1]});
        }
        command.vertex_payloads.push_back(std::move(payload));
    }

    if (const std::optional<std::string> error =
            evaluate_dynamic_mesh_bounds(&command, bone_palette)) {
        return slot_error(slot_index, *error);
    }
    command.masked_vertices.clear();
    command.masked_indices.clear();
    command.clip_attachment_name =
        active_clips.empty()
            ? std::nullopt
            : std::optional<std::string>{active_clips.back().attachment_name};

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

bool PreparedSceneCache::has_scene() const {
    return has_scene_;
}

const PreparedScene& PreparedSceneCache::scene() const {
    return scene_;
}

bool PreparedSceneCache::has_batch_summary() const {
    return has_batch_summary_;
}

const PreparedSceneBatchSummary& PreparedSceneCache::batch_summary() const {
    return batch_summary_;
}

const PreparedSceneCacheUpdateInfo& PreparedSceneCache::last_update() const {
    return last_update_;
}

const std::vector<bool>& PreparedSceneCache::slot_dirty_flags() const {
    return slot_dirty_flags_;
}

bool PreparedSceneCache::draw_order_dirty() const {
    return draw_order_dirty_;
}

bool PreparedSceneCache::skin_swap_dirty() const {
    return skin_swap_dirty_;
}

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

    if (!skeleton.visible()) {
        PreparedScene scene;
        assign_scene_metadata(&scene, skeleton, atlas);
        result.scene = std::move(scene);
        return result;
    }

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
    assign_scene_metadata(&scene, skeleton, atlas);
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
            command.bone_index = slot.bone_index;
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

PreparedSceneCacheResult prepare_setup_pose_scene_cached(
    PreparedSceneCache* cache,
    const runtime::Skeleton& skeleton,
    const runtime::AtlasData& atlas) {
    PreparedSceneCacheResult result;
    if (cache == nullptr) {
        result.error_message = "Prepared scene cache must not be null.";
        return result;
    }

    const std::size_t slot_count = skeleton.data()->slots().size();
    cache->slot_dirty_flags_.assign(slot_count, false);
    cache->draw_order_dirty_ = false;
    cache->skin_swap_dirty_ = false;
    cache->last_update_ = {};

    const bool force_full_rebuild =
        !cache->has_scene_ ||
        cache->skeleton_data_ != skeleton.data().get() ||
        cache->atlas_data_ != &atlas ||
        cache->cached_visible_ != skeleton.visible() ||
        cache->slot_snapshots_.size() != slot_count ||
        cache->slot_records_.size() != slot_count;

    cache->skeleton_data_ = skeleton.data().get();
    cache->atlas_data_ = &atlas;
    cache->cached_visible_ = skeleton.visible();

    if (!skeleton.visible()) {
        if (force_full_rebuild || cache->scene_.skeleton_count != 1U) {
            PreparedScene scene;
            assign_scene_metadata(&scene, skeleton, atlas);
            cache->scene_ = std::move(scene);
            cache->has_scene_ = true;
            cache->has_batch_summary_ = false;
            cache->draw_order_snapshot_.clear();
            cache->slot_snapshots_.assign(slot_count, {});
            cache->slot_records_.assign(slot_count, {});
            cache->last_update_.rebuilt_slot_count = slot_count;
        } else {
            cache->last_update_.cache_hit = true;
            cache->last_update_.bone_palette_only = true;
        }

        result.scene = &cache->scene_;
        result.update_info = &cache->last_update_;
        return result;
    }

    if (force_full_rebuild) {
        cache->draw_order_snapshot_.assign(
            skeleton.draw_order().begin(),
            skeleton.draw_order().end());
    } else if (cache->draw_order_snapshot_ != skeleton.draw_order()) {
        cache->draw_order_dirty_ = true;
    }

    if (cache->slot_snapshots_.size() != slot_count) {
        cache->slot_snapshots_.assign(slot_count, {});
    }
    if (cache->slot_records_.size() != slot_count) {
        cache->slot_records_.assign(slot_count, {});
    }

    for (std::size_t slot_index = 0; slot_index < slot_count; ++slot_index) {
        const PreparedSceneCache::SlotSnapshot snapshot =
            build_slot_snapshot(skeleton, slot_index);
        const bool slot_dirty =
            force_full_rebuild || !slot_snapshot_matches(cache->slot_snapshots_[slot_index], snapshot);
        cache->slot_dirty_flags_[slot_index] = slot_dirty;
        if (!slot_dirty) {
            continue;
        }

        ++cache->last_update_.dirty_slot_count;
        if (cache->slot_snapshots_[slot_index].attachment_skin_index != snapshot.attachment_skin_index) {
            cache->skin_swap_dirty_ = true;
        }

        if (const std::optional<std::string> error = build_slot_record(
                &cache->slot_records_[slot_index],
                skeleton,
                atlas,
                slot_index)) {
            result.error_message = *error;
            return result;
        }
        cache->slot_snapshots_[slot_index] = snapshot;
        ++cache->last_update_.rebuilt_slot_count;
    }

    if (!force_full_rebuild &&
        !cache->draw_order_dirty_ &&
        cache->last_update_.dirty_slot_count == 0U) {
        cache->scene_.bone_palette = skeleton.bone_world_transforms();
        cache->last_update_.cache_hit = true;
        cache->last_update_.bone_palette_only = true;
        result.scene = &cache->scene_;
        result.update_info = &cache->last_update_;
        return result;
    }

    cache->draw_order_snapshot_.assign(
        skeleton.draw_order().begin(),
        skeleton.draw_order().end());
    cache->last_update_.draw_order_dirty = cache->draw_order_dirty_;
    cache->last_update_.skin_swap_dirty = cache->skin_swap_dirty_;
    rebuild_cached_scene(cache, skeleton, atlas);

    result.scene = &cache->scene_;
    result.update_info = &cache->last_update_;
    return result;
}

GpuSkinningEvaluationResult evaluate_gpu_skinned_vertices(
    const DynamicMeshDrawCommand& attachment,
    const std::vector<runtime::BoneWorldTransform>& bone_palette) {
    return evaluate_skinned_vertices(attachment, bone_palette);
}

PreparedSceneBatchSummary summarize_prepared_scene_batches(const PreparedScene& scene) {
    const RenderCommandListResult command_list_result =
        build_render_command_list(scene, orthographic_projection(scene_bounds(scene)));
    if (!command_list_result) {
        PreparedSceneBatchSummary summary;
        summary.error_message = command_list_result.error_message;
        return summary;
    }

    return summarize_render_command_list(scene, *command_list_result.command_list);
}

const PreparedSceneBatchSummary* summarize_prepared_scene_batches_cached(
    PreparedSceneCache* cache) {
    if (cache == nullptr || !cache->has_scene_) {
        return nullptr;
    }

    if (!cache->has_batch_summary_) {
        cache->batch_summary_ = summarize_prepared_scene_batches(cache->scene_);
        cache->has_batch_summary_ = true;
    }

    return &cache->batch_summary_;
}

PreparedSceneBatchSummary summarize_render_command_list(
    const PreparedScene& scene,
    const RenderCommandList& command_list) {
    PreparedSceneBatchSummary summary;
    summary.skeleton_count = scene.skeleton_count;

    summary.draw_command_count = 0;
    for (const RenderCommand& command : command_list.commands) {
        PreparedDrawBatch public_batch;
        public_batch.texture_name = command.texture_name;
        public_batch.blend_mode = command.blend_mode;
        public_batch.shader_variant = command.shader_variant;
        public_batch.draw_command_offset = command.source_draw_command_offset;
        public_batch.draw_command_count = command.source_draw_command_count;
        public_batch.vertex_count = command.vertices.size();
        public_batch.index_count = command.indices.size();
        summary.draw_command_count += command.source_draw_command_count;
        summary.batches.push_back(std::move(public_batch));
    }

    summary.vertex_count = 0;
    summary.index_count = 0;
    for (const RenderCommand& command : command_list.commands) {
        summary.vertex_count += command.vertices.size();
        summary.index_count += command.indices.size();
    }
    summary.draw_call_count = command_list.commands.size();
    summary.merged_draw_calls =
        summary.draw_command_count >= summary.draw_call_count
            ? (summary.draw_command_count - summary.draw_call_count)
            : 0;
    summary.bone_uniform_count = command_list.bone_palette.size() / 6U;
    summary.vertex_buffer_bytes = summary.vertex_count * sizeof(RenderCommandVertex);
    summary.index_buffer_bytes = summary.index_count * sizeof(std::uint32_t);
    summary.break_reasons = command_list.batch_break_reasons;
    return summary;
}

DemoShell::DemoShell(
    SampleAppWindow window,
    PreparedScene scene,
    std::filesystem::path atlas_image_path,
    bool hud_overlay_enabled)
    : window_(std::move(window)),
      scene_(std::move(scene)),
      atlas_image_path_(std::move(atlas_image_path)),
      hud_overlay_enabled_(hud_overlay_enabled) {}

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
           << ", skeletons=" << scene_.skeleton_count
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
           << "draw commands: " << scene_.draw_commands.size() << '\n'
           << "hud overlay: " << (hud_overlay_enabled_ ? "enabled" : "disabled");
    if (batch_summary) {
        stream << '\n'
               << "streaming batches: drawCalls=" << batch_summary.draw_call_count
               << ", mergedCommands=" << batch_summary.merged_draw_calls
               << ", streamedVertices=" << batch_summary.vertex_count
               << ", streamedIndices=" << batch_summary.index_count
               << ", breakTexture=" << batch_summary.break_reasons.texture_changes
               << ", breakBlend=" << batch_summary.break_reasons.blend_changes
               << ", breakClip=" << batch_summary.break_reasons.clip_changes
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

    const TextureImageLoadResult texture_image = load_png_texture_or_white(atlas_image_path_);
    if (!texture_image.loaded_from_file && !texture_image.message.empty()) {
        std::cerr << texture_image.message << '\n';
    }

    BackendCreateInfo create_info;
    create_info.window = window_;
    create_info.atlas_texture = texture_image.image;
    create_info.atlas_filter_min = scene_.atlas_filter_min;
    create_info.atlas_filter_mag = scene_.atlas_filter_mag;
    create_info.atlas_wrap_x = scene_.atlas_wrap_x;
    create_info.atlas_wrap_y = scene_.atlas_wrap_y;
    create_info.hidden_window = auto_close_frames.has_value();

    std::unique_ptr<Backend> backend = internal::make_sokol_backend();

    const SceneBounds base_bounds = scene_bounds(scene_);
    const auto render_frame = [&](const BackendFrameInfo& frame_info) -> std::optional<std::string> {
        const SceneBounds framed_bounds = fit_bounds_to_aspect(
            base_bounds,
            frame_info.framebuffer_width,
            frame_info.framebuffer_height);
        runtime::ProfilerCapture profiler(hud_overlay_enabled_);
        if (hud_overlay_enabled_) {
            profiler.begin_frame();
        }

        const RenderCommandListResult command_list_result =
            runtime::profile_phase(&profiler, runtime::ProfilerPhase::Render, [&]() {
                return build_render_command_list(scene_, orthographic_projection(framed_bounds));
            });
        if (!command_list_result) {
            return command_list_result.error_message;
        }

        RenderCommandList command_list = std::move(*command_list_result.command_list);
        if (hud_overlay_enabled_) {
            const PreparedSceneBatchSummary batch_summary =
                summarize_render_command_list(scene_, command_list);
            profiler.add_draw_stats(profiler_draw_stats(batch_summary));
            profiler.end_frame();

            if (const auto hud_command = build_profiler_hud_overlay_command(
                    runtime::marrow_profiler_frame(profiler),
                    framed_bounds,
                    frame_info.framebuffer_width,
                    frame_info.framebuffer_height,
                    scene_.bone_palette.size())) {
                const std::size_t draw_index = command_list.commands.size();
                command_list.commands.push_back(*hud_command);
                command_list.ordered_events.push_back({RenderCommandEventKind::Draw, draw_index});
            }
        }

        return backend->submit_commands(command_list);
    };

#if defined(__APPLE__)
    if (auto_close_frames.has_value()) {
        if (const std::optional<std::string> error = backend->create(create_info)) {
            backend->destroy();
            return error;
        }

        for (int frame_index = 0; frame_index < *auto_close_frames; ++frame_index) {
            BackendFrameInfo frame_info;
            if (const std::optional<std::string> error = backend->begin_frame(&frame_info)) {
                backend->destroy();
                return error;
            }
            if (const std::optional<std::string> error = render_frame(frame_info)) {
                backend->destroy();
                return error;
            }
            backend->end_frame();
        }

        backend->destroy();
        return std::nullopt;
    }
#endif

    return internal::run_sokol_app(create_info, backend.get(), render_frame, auto_close_frames);
}

RenderCommandListResult build_render_command_list(
    const PreparedScene& scene,
    const std::array<float, 16>& projection) {
    return build_render_command_list_impl(scene, projection);
}

std::string_view component_name() {
    return "marrow-renderer";
}

std::string_view validation_target_name() {
    return "marrow_renderer_sample";
}

} // namespace marrow::renderer
