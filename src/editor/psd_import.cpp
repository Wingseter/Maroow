#include "marrow/editor/psd_import.hpp"

#include "atlas_packer.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <string_view>
#include <utility>
#include <vector>

#include "marrow/runtime/json.hpp"
#include "marrow/runtime/skeleton.hpp"

namespace marrow::editor {
namespace {

using marrow::runtime::json::Document;
using marrow::runtime::json::SourceLocation;
using marrow::runtime::json::Value;

constexpr SourceLocation kGeneratedLocation{};
constexpr std::uint16_t kPsdVersion = 1U;
constexpr std::uint16_t kPsdRgbColorMode = 3U;
constexpr std::uint16_t kPsdDepth8 = 8U;
constexpr std::int16_t kPsdChannelRed = 0;
constexpr std::int16_t kPsdChannelGreen = 1;
constexpr std::int16_t kPsdChannelBlue = 2;
constexpr std::int16_t kPsdChannelAlpha = -1;
constexpr std::uint32_t kPsdSectionTypeOpenFolder = 1U;
constexpr std::uint32_t kPsdSectionTypeClosedFolder = 2U;
constexpr std::uint32_t kPsdSectionTypeBounding = 3U;

enum class LayerSectionType {
    Regular,
    GroupStart,
    GroupEnd,
};

struct Cursor {
    const std::vector<std::uint8_t>* bytes{nullptr};
    std::size_t offset{0};
};

struct LayerChannelDescriptor {
    std::int16_t id{0};
    std::uint32_t data_length{0};
};

struct LayerRecord {
    std::string name;
    int top{0};
    int left{0};
    int bottom{0};
    int right{0};
    bool visible{true};
    LayerSectionType section_type{LayerSectionType::Regular};
    std::vector<LayerChannelDescriptor> channels;
    std::vector<std::uint8_t> rgba8;
    std::size_t stack_order{0};
};

struct GroupRecord {
    std::string original_name;
    std::optional<std::size_t> parent_group_index;
    std::vector<std::string> path;
    int left{0};
    int top{0};
    int right{0};
    int bottom{0};
    bool has_bounds{false};
    std::string bone_name;
};

struct ParsedLayer {
    std::string original_name;
    std::optional<std::size_t> parent_group_index;
    std::vector<std::string> group_path;
    int left{0};
    int top{0};
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba8;
    std::size_t stack_order{0};
    std::string slot_name;
    std::string attachment_name;
    std::filesystem::path extracted_image_path;
};

struct ParsedPsdDocument {
    int width{0};
    int height{0};
    std::vector<GroupRecord> groups;
    std::vector<ParsedLayer> layers;
};

Value make_boolean_value(bool value) {
    return Value(value, kGeneratedLocation);
}

Value make_number_value(double value) {
    return Value(value, kGeneratedLocation);
}

Value make_string_value(std::string value) {
    return Value(std::move(value), kGeneratedLocation);
}

Value make_array_value(Value::Array values = {}) {
    return Value(std::move(values), kGeneratedLocation);
}

Value make_object_value(Value::Object values = {}) {
    return Value(std::move(values), kGeneratedLocation);
}

bool checked_add(std::size_t lhs, std::size_t rhs, std::size_t* sum_out) {
    if (rhs > std::numeric_limits<std::size_t>::max() - lhs) {
        return false;
    }
    *sum_out = lhs + rhs;
    return true;
}

bool checked_multiply(std::size_t lhs, std::size_t rhs, std::size_t* product_out) {
    if (lhs != 0U && rhs > std::numeric_limits<std::size_t>::max() / lhs) {
        return false;
    }
    *product_out = lhs * rhs;
    return true;
}

std::optional<std::string> read_bytes(
    Cursor* cursor,
    std::size_t length,
    const std::uint8_t** bytes_out) {
    if (cursor == nullptr || cursor->bytes == nullptr) {
        return "PSD reader cursor was not initialized.";
    }
    if (cursor->offset > cursor->bytes->size() || length > cursor->bytes->size() - cursor->offset) {
        return "PSD file ended unexpectedly.";
    }

    *bytes_out = cursor->bytes->data() + static_cast<std::ptrdiff_t>(cursor->offset);
    cursor->offset += length;
    return std::nullopt;
}

std::optional<std::string> skip_bytes(Cursor* cursor, std::size_t length) {
    const std::uint8_t* ignored = nullptr;
    return read_bytes(cursor, length, &ignored);
}

std::optional<std::string> read_u16(Cursor* cursor, std::uint16_t* value_out) {
    const std::uint8_t* bytes = nullptr;
    if (const auto error = read_bytes(cursor, 2U, &bytes)) {
        return error;
    }
    *value_out = static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[0]) << 8U) |
        static_cast<std::uint16_t>(bytes[1]));
    return std::nullopt;
}

std::optional<std::string> read_i16(Cursor* cursor, std::int16_t* value_out) {
    std::uint16_t bits = 0U;
    if (const auto error = read_u16(cursor, &bits)) {
        return error;
    }
    *value_out = static_cast<std::int16_t>(bits);
    return std::nullopt;
}

std::optional<std::string> read_u32(Cursor* cursor, std::uint32_t* value_out) {
    const std::uint8_t* bytes = nullptr;
    if (const auto error = read_bytes(cursor, 4U, &bytes)) {
        return error;
    }
    *value_out =
        (static_cast<std::uint32_t>(bytes[0]) << 24U) |
        (static_cast<std::uint32_t>(bytes[1]) << 16U) |
        (static_cast<std::uint32_t>(bytes[2]) << 8U) |
        static_cast<std::uint32_t>(bytes[3]);
    return std::nullopt;
}

std::optional<std::string> read_i32(Cursor* cursor, std::int32_t* value_out) {
    std::uint32_t bits = 0U;
    if (const auto error = read_u32(cursor, &bits)) {
        return error;
    }
    *value_out = static_cast<std::int32_t>(bits);
    return std::nullopt;
}

std::optional<std::string> read_signature(Cursor* cursor, std::string* value_out) {
    const std::uint8_t* bytes = nullptr;
    if (const auto error = read_bytes(cursor, 4U, &bytes)) {
        return error;
    }
    value_out->assign(reinterpret_cast<const char*>(bytes), 4U);
    return std::nullopt;
}

std::string trim_trailing_nuls(std::string value) {
    while (!value.empty() && value.back() == '\0') {
        value.pop_back();
    }
    return value;
}

std::optional<std::string> read_pascal_string(Cursor* cursor, std::string* value_out) {
    const std::uint8_t* length_byte = nullptr;
    if (const auto error = read_bytes(cursor, 1U, &length_byte)) {
        return error;
    }

    const std::size_t length = length_byte[0];
    const std::uint8_t* string_bytes = nullptr;
    if (const auto error = read_bytes(cursor, length, &string_bytes)) {
        return error;
    }

    value_out->assign(reinterpret_cast<const char*>(string_bytes), length);
    const std::size_t padded_length = ((1U + length + 3U) / 4U) * 4U;
    const std::size_t padding = padded_length - (1U + length);
    return skip_bytes(cursor, padding);
}

std::optional<std::string> decode_packbits_row(
    const std::uint8_t* encoded,
    std::size_t encoded_length,
    std::size_t expected_length,
    std::vector<std::uint8_t>* row_out) {
    row_out->clear();
    row_out->reserve(expected_length);

    std::size_t offset = 0U;
    while (offset < encoded_length && row_out->size() < expected_length) {
        const std::int8_t header = static_cast<std::int8_t>(encoded[offset++]);
        if (header >= 0) {
            const std::size_t literal_length = static_cast<std::size_t>(header) + 1U;
            if (literal_length > encoded_length - offset) {
                return "PSD RLE literal run exceeded the encoded row.";
            }
            row_out->insert(
                row_out->end(),
                encoded + static_cast<std::ptrdiff_t>(offset),
                encoded + static_cast<std::ptrdiff_t>(offset + literal_length));
            offset += literal_length;
            continue;
        }

        if (header == -128) {
            continue;
        }

        if (offset >= encoded_length) {
            return "PSD RLE repeat run was missing its source byte.";
        }
        const std::size_t repeat_count = static_cast<std::size_t>(1 - static_cast<int>(header));
        row_out->insert(row_out->end(), repeat_count, encoded[offset]);
        ++offset;
    }

    if (row_out->size() != expected_length) {
        return "PSD RLE row did not expand to the expected width.";
    }
    return std::nullopt;
}

std::optional<std::string> decode_channel(
    Cursor* cursor,
    int width,
    int height,
    std::vector<std::uint8_t>* bytes_out) {
    if (width < 0 || height < 0) {
        return "PSD layer dimensions must not be negative.";
    }

    std::uint16_t compression = 0U;
    if (const auto error = read_u16(cursor, &compression)) {
        return error;
    }

    std::size_t pixel_count = 0U;
    if (!checked_multiply(
            static_cast<std::size_t>(width),
            static_cast<std::size_t>(height),
            &pixel_count)) {
        return "PSD layer channel buffer overflowed size_t.";
    }
    bytes_out->assign(pixel_count, 0U);

    if (compression == 0U) {
        if (pixel_count == 0U) {
            return std::nullopt;
        }
        const std::uint8_t* channel_bytes = nullptr;
        if (const auto error = read_bytes(cursor, pixel_count, &channel_bytes)) {
            return error;
        }
        std::copy(
            channel_bytes,
            channel_bytes + static_cast<std::ptrdiff_t>(pixel_count),
            bytes_out->begin());
        return std::nullopt;
    }

    if (compression != 1U) {
        return "PSD channel compression must be raw or PackBits RLE.";
    }

    std::vector<std::uint16_t> row_lengths(static_cast<std::size_t>(height), 0U);
    for (int row = 0; row < height; ++row) {
        if (const auto error = read_u16(cursor, &row_lengths[static_cast<std::size_t>(row)])) {
            return error;
        }
    }

    std::vector<std::uint8_t> row;
    for (int row_index = 0; row_index < height; ++row_index) {
        const std::size_t encoded_length = row_lengths[static_cast<std::size_t>(row_index)];
        const std::uint8_t* encoded = nullptr;
        if (const auto error = read_bytes(cursor, encoded_length, &encoded)) {
            return error;
        }
        if (const auto error = decode_packbits_row(
                encoded,
                encoded_length,
                static_cast<std::size_t>(width),
                &row)) {
            return error;
        }
        std::copy(
            row.begin(),
            row.end(),
            bytes_out->begin() +
                static_cast<std::ptrdiff_t>(
                    static_cast<std::size_t>(row_index) * static_cast<std::size_t>(width)));
    }
    return std::nullopt;
}

bool is_fully_transparent(const std::vector<std::uint8_t>& rgba8) {
    for (std::size_t index = 3U; index < rgba8.size(); index += 4U) {
        if (rgba8[index] != 0U) {
            return false;
        }
    }
    return true;
}

std::optional<std::string> parse_layer_records(
    Cursor* cursor,
    std::uint32_t layer_info_length,
    std::vector<LayerRecord>* layers_out) {
    const std::size_t layer_info_end = cursor->offset + static_cast<std::size_t>(layer_info_length);
    if (layer_info_end > cursor->bytes->size()) {
        return "PSD layer info section overran the file.";
    }

    std::int16_t layer_count = 0;
    if (const auto error = read_i16(cursor, &layer_count)) {
        return error;
    }
    const std::size_t layer_total = static_cast<std::size_t>(std::abs(layer_count));

    std::vector<LayerRecord> layers;
    layers.reserve(layer_total);
    for (std::size_t layer_index = 0; layer_index < layer_total; ++layer_index) {
        LayerRecord layer;
        std::int32_t top = 0;
        std::int32_t left = 0;
        std::int32_t bottom = 0;
        std::int32_t right = 0;
        if (const auto error = read_i32(cursor, &top)) {
            return error;
        }
        if (const auto error = read_i32(cursor, &left)) {
            return error;
        }
        if (const auto error = read_i32(cursor, &bottom)) {
            return error;
        }
        if (const auto error = read_i32(cursor, &right)) {
            return error;
        }
        layer.top = top;
        layer.left = left;
        layer.bottom = bottom;
        layer.right = right;

        std::uint16_t channel_count = 0U;
        if (const auto error = read_u16(cursor, &channel_count)) {
            return error;
        }
        layer.channels.reserve(channel_count);
        for (std::size_t channel_index = 0; channel_index < channel_count; ++channel_index) {
            LayerChannelDescriptor channel;
            if (const auto error = read_i16(cursor, &channel.id)) {
                return error;
            }
            if (const auto error = read_u32(cursor, &channel.data_length)) {
                return error;
            }
            layer.channels.push_back(channel);
        }

        std::string blend_signature;
        std::string blend_mode;
        if (const auto error = read_signature(cursor, &blend_signature)) {
            return error;
        }
        if (const auto error = read_signature(cursor, &blend_mode)) {
            return error;
        }
        if (blend_signature != "8BIM") {
            return "PSD layer record used an unexpected blend signature.";
        }

        const std::uint8_t* misc_bytes = nullptr;
        if (const auto error = read_bytes(cursor, 4U, &misc_bytes)) {
            return error;
        }
        layer.visible = (misc_bytes[2] & 0x02U) == 0U;

        std::uint32_t extra_length = 0U;
        if (const auto error = read_u32(cursor, &extra_length)) {
            return error;
        }
        const std::size_t extra_end = cursor->offset + static_cast<std::size_t>(extra_length);
        if (extra_end > cursor->bytes->size()) {
            return "PSD layer record extra data overran the file.";
        }

        std::uint32_t layer_mask_length = 0U;
        if (const auto error = read_u32(cursor, &layer_mask_length)) {
            return error;
        }
        if (const auto error = skip_bytes(cursor, layer_mask_length)) {
            return error;
        }

        std::uint32_t blending_ranges_length = 0U;
        if (const auto error = read_u32(cursor, &blending_ranges_length)) {
            return error;
        }
        if (const auto error = skip_bytes(cursor, blending_ranges_length)) {
            return error;
        }

        if (const auto error = read_pascal_string(cursor, &layer.name)) {
            return error;
        }
        layer.name = trim_trailing_nuls(std::move(layer.name));

        while (cursor->offset < extra_end) {
            std::string signature;
            std::string key;
            if (const auto error = read_signature(cursor, &signature)) {
                return error;
            }
            if (const auto error = read_signature(cursor, &key)) {
                return error;
            }
            if (signature != "8BIM" && signature != "8B64") {
                return "PSD layer info block used an unexpected signature.";
            }

            std::uint32_t block_length = 0U;
            if (const auto error = read_u32(cursor, &block_length)) {
                return error;
            }
            const std::size_t padded_length =
                static_cast<std::size_t>(block_length) +
                static_cast<std::size_t>(block_length % 2U);
            const std::size_t block_end = cursor->offset + padded_length;
            if (block_end > extra_end) {
                return "PSD additional layer info block overran its layer record.";
            }

            if (key == "lsct" && block_length >= 4U) {
                Cursor block_cursor{cursor->bytes, cursor->offset};
                std::uint32_t section_type = 0U;
                if (const auto error = read_u32(&block_cursor, &section_type)) {
                    return error;
                }
                if (section_type == kPsdSectionTypeOpenFolder ||
                    section_type == kPsdSectionTypeClosedFolder) {
                    layer.section_type = LayerSectionType::GroupStart;
                } else if (section_type == kPsdSectionTypeBounding) {
                    layer.section_type = LayerSectionType::GroupEnd;
                }
            }

            cursor->offset = block_end;
        }

        if (cursor->offset != extra_end) {
            return "PSD layer record extra data did not parse cleanly.";
        }

        layer.stack_order = layer_index;
        layers.push_back(std::move(layer));
    }

    for (LayerRecord& layer : layers) {
        const int width = std::max(layer.right - layer.left, 0);
        const int height = std::max(layer.bottom - layer.top, 0);
        std::size_t pixel_count = 0U;
        if (!checked_multiply(
                static_cast<std::size_t>(width),
                static_cast<std::size_t>(height),
                &pixel_count)) {
            return "PSD layer pixel buffer overflowed size_t.";
        }

        layer.rgba8.assign(pixel_count * 4U, 0U);
        if (pixel_count > 0U) {
            for (std::size_t pixel_index = 0U; pixel_index < pixel_count; ++pixel_index) {
                layer.rgba8[(pixel_index * 4U) + 3U] = 255U;
            }
        }

        for (const LayerChannelDescriptor& channel : layer.channels) {
            const std::size_t channel_start = cursor->offset;
            std::vector<std::uint8_t> channel_bytes;
            if (const auto error = decode_channel(cursor, width, height, &channel_bytes)) {
                return error;
            }

            if (cursor->offset - channel_start != channel.data_length) {
                return "PSD layer channel length did not match its declared size.";
            }

            if (channel.id != kPsdChannelRed &&
                channel.id != kPsdChannelGreen &&
                channel.id != kPsdChannelBlue &&
                channel.id != kPsdChannelAlpha) {
                continue;
            }

            const std::size_t rgba_component =
                channel.id == kPsdChannelRed   ? 0U :
                channel.id == kPsdChannelGreen ? 1U :
                channel.id == kPsdChannelBlue  ? 2U :
                                                 3U;
            for (std::size_t pixel_index = 0U; pixel_index < pixel_count; ++pixel_index) {
                layer.rgba8[(pixel_index * 4U) + rgba_component] = channel_bytes[pixel_index];
            }
        }
    }

    if (cursor->offset > layer_info_end) {
        return "PSD layer image data exceeded the layer info section.";
    }
    cursor->offset = layer_info_end;
    layers_out->swap(layers);
    return std::nullopt;
}

std::string fallback_name(std::string_view prefix, std::size_t index) {
    return std::string(prefix) + "_" + std::to_string(index);
}

std::string join_path(
    const std::vector<std::string>& segments,
    std::string_view leaf = {}) {
    std::string joined;
    for (std::size_t index = 0; index < segments.size(); ++index) {
        if (!joined.empty()) {
            joined += '/';
        }
        joined += segments[index];
    }
    if (!leaf.empty()) {
        if (!joined.empty()) {
            joined += '/';
        }
        joined += leaf;
    }
    return joined;
}

std::string make_unique_name(
    std::string base_name,
    std::set<std::string>* used_names) {
    if (base_name.empty()) {
        base_name = "item";
    }
    if (used_names->insert(base_name).second) {
        return base_name;
    }

    for (std::size_t suffix = 2U;; ++suffix) {
        const std::string candidate = base_name + "_" + std::to_string(suffix);
        if (used_names->insert(candidate).second) {
            return candidate;
        }
    }
}

std::string sanitize_file_component(std::string_view value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (const char character : value) {
        const unsigned char byte = static_cast<unsigned char>(character);
        if (std::isalnum(byte) != 0U || character == '_' || character == '-') {
            sanitized.push_back(static_cast<char>(byte));
        } else {
            sanitized.push_back('_');
        }
    }
    return sanitized.empty() ? std::string("layer") : sanitized;
}

std::optional<std::string> parse_psd_document(
    const std::vector<std::uint8_t>& bytes,
    ParsedPsdDocument* document_out) {
    Cursor cursor{&bytes, 0U};

    std::string signature;
    if (const auto error = read_signature(&cursor, &signature)) {
        return error;
    }
    if (signature != "8BPS") {
        return "PSD files must begin with the 8BPS signature.";
    }

    std::uint16_t version = 0U;
    if (const auto error = read_u16(&cursor, &version)) {
        return error;
    }
    if (version != kPsdVersion) {
        return "Only PSD version 1 files are supported.";
    }
    if (const auto error = skip_bytes(&cursor, 6U)) {
        return error;
    }

    std::uint16_t channel_count = 0U;
    std::uint32_t height = 0U;
    std::uint32_t width = 0U;
    std::uint16_t depth = 0U;
    std::uint16_t color_mode = 0U;
    if (const auto error = read_u16(&cursor, &channel_count)) {
        return error;
    }
    if (const auto error = read_u32(&cursor, &height)) {
        return error;
    }
    if (const auto error = read_u32(&cursor, &width)) {
        return error;
    }
    if (const auto error = read_u16(&cursor, &depth)) {
        return error;
    }
    if (const auto error = read_u16(&cursor, &color_mode)) {
        return error;
    }

    if (width == 0U || height == 0U) {
        return "PSD canvas dimensions must be greater than zero.";
    }
    if (depth != kPsdDepth8) {
        return "Only 8-bit PSD files are supported.";
    }
    if (color_mode != kPsdRgbColorMode) {
        return "Only RGB PSD files are supported.";
    }
    if (channel_count < 3U || channel_count > 4U) {
        return "PSD files must provide RGB or RGBA image channels.";
    }

    std::uint32_t color_mode_length = 0U;
    if (const auto error = read_u32(&cursor, &color_mode_length)) {
        return error;
    }
    if (const auto error = skip_bytes(&cursor, color_mode_length)) {
        return error;
    }

    std::uint32_t image_resources_length = 0U;
    if (const auto error = read_u32(&cursor, &image_resources_length)) {
        return error;
    }
    if (const auto error = skip_bytes(&cursor, image_resources_length)) {
        return error;
    }

    std::uint32_t layer_and_mask_length = 0U;
    if (const auto error = read_u32(&cursor, &layer_and_mask_length)) {
        return error;
    }
    const std::size_t layer_and_mask_end =
        cursor.offset + static_cast<std::size_t>(layer_and_mask_length);
    if (layer_and_mask_end > bytes.size()) {
        return "PSD layer and mask section overran the file.";
    }

    std::uint32_t layer_info_length = 0U;
    if (const auto error = read_u32(&cursor, &layer_info_length)) {
        return error;
    }

    std::vector<LayerRecord> records;
    if (const auto error = parse_layer_records(&cursor, layer_info_length, &records)) {
        return error;
    }

    if (cursor.offset < layer_and_mask_end) {
        if (const auto error = skip_bytes(&cursor, layer_and_mask_end - cursor.offset)) {
            return error;
        }
    }

    ParsedPsdDocument parsed;
    parsed.width = static_cast<int>(width);
    parsed.height = static_cast<int>(height);

    std::vector<std::size_t> active_groups;
    for (std::size_t record_index = 0; record_index < records.size(); ++record_index) {
        const LayerRecord& record = records[record_index];
        if (record.section_type == LayerSectionType::GroupEnd) {
            if (active_groups.empty()) {
                return "PSD folder end marker appeared without an open folder.";
            }
            active_groups.pop_back();
            continue;
        }

        if (record.section_type == LayerSectionType::GroupStart) {
            GroupRecord group;
            group.original_name = record.name.empty() ? fallback_name("group", record_index) : record.name;
            group.parent_group_index =
                active_groups.empty() ? std::nullopt
                                      : std::optional<std::size_t>(active_groups.back());
            if (group.parent_group_index.has_value()) {
                group.path = parsed.groups[*group.parent_group_index].path;
            }
            group.path.push_back(group.original_name);
            parsed.groups.push_back(std::move(group));
            active_groups.push_back(parsed.groups.size() - 1U);
            continue;
        }

        const int layer_width = std::max(record.right - record.left, 0);
        const int layer_height = std::max(record.bottom - record.top, 0);
        if (layer_width == 0 || layer_height == 0 || record.rgba8.empty() || !record.visible ||
            is_fully_transparent(record.rgba8)) {
            continue;
        }

        ParsedLayer layer;
        layer.original_name = record.name.empty() ? fallback_name("layer", record_index) : record.name;
        layer.parent_group_index =
            active_groups.empty() ? std::nullopt
                                  : std::optional<std::size_t>(active_groups.back());
        if (layer.parent_group_index.has_value()) {
            layer.group_path = parsed.groups[*layer.parent_group_index].path;
        }
        layer.left = record.left;
        layer.top = record.top;
        layer.width = layer_width;
        layer.height = layer_height;
        layer.rgba8 = record.rgba8;
        layer.stack_order = record.stack_order;
        parsed.layers.push_back(std::move(layer));

        for (const std::size_t group_index : active_groups) {
            GroupRecord& group = parsed.groups[group_index];
            if (!group.has_bounds) {
                group.left = record.left;
                group.top = record.top;
                group.right = record.right;
                group.bottom = record.bottom;
                group.has_bounds = true;
                continue;
            }
            group.left = std::min(group.left, record.left);
            group.top = std::min(group.top, record.top);
            group.right = std::max(group.right, record.right);
            group.bottom = std::max(group.bottom, record.bottom);
        }
    }

    if (!active_groups.empty()) {
        return "PSD folder markers were unbalanced.";
    }
    if (parsed.layers.empty()) {
        return "PSD import requires at least one visible pixel layer.";
    }

    std::set<std::string> used_bone_names;
    used_bone_names.insert("root");
    for (GroupRecord& group : parsed.groups) {
        group.bone_name = make_unique_name(join_path(group.path), &used_bone_names);
        if (!group.has_bounds) {
            group.left = 0;
            group.top = 0;
            group.right = 0;
            group.bottom = 0;
        }
    }

    std::map<std::string, std::size_t, std::less<>> duplicate_layer_names;
    for (const ParsedLayer& layer : parsed.layers) {
        ++duplicate_layer_names[layer.original_name];
    }

    std::set<std::string> used_slot_names;
    for (ParsedLayer& layer : parsed.layers) {
        std::string base_name = duplicate_layer_names[layer.original_name] > 1U
            ? join_path(layer.group_path, layer.original_name)
            : layer.original_name;
        layer.slot_name = make_unique_name(base_name, &used_slot_names);
        layer.attachment_name = layer.slot_name;
    }

    document_out->width = parsed.width;
    document_out->height = parsed.height;
    document_out->groups = std::move(parsed.groups);
    document_out->layers = std::move(parsed.layers);
    return std::nullopt;
}

std::optional<std::string> read_binary_file(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>* bytes_out) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return "failed to open the input file";
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size < 0) {
        return "failed to determine the input file size";
    }
    bytes_out->resize(static_cast<std::size_t>(size));
    input.seekg(0, std::ios::beg);
    if (!bytes_out->empty()) {
        input.read(reinterpret_cast<char*>(bytes_out->data()), size);
    }
    if (!input) {
        return "failed to read the input file";
    }
    return std::nullopt;
}

std::optional<std::string> write_text_file(
    const std::filesystem::path& path,
    std::string_view text) {
    const std::filesystem::path parent_path = path.parent_path();
    if (!parent_path.empty()) {
        std::error_code directory_error;
        std::filesystem::create_directories(parent_path, directory_error);
        if (directory_error) {
            return directory_error.message();
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return "failed to open the output file";
    }
    output << text;
    if (!output.good()) {
        return "failed to write the output file";
    }
    return std::nullopt;
}

std::optional<std::string> write_binary_file(
    const std::filesystem::path& path,
    const std::vector<std::uint8_t>& bytes) {
    const std::filesystem::path parent_path = path.parent_path();
    if (!parent_path.empty()) {
        std::error_code directory_error;
        std::filesystem::create_directories(parent_path, directory_error);
        if (directory_error) {
            return directory_error.message();
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return "failed to open the output file";
    }
    if (!bytes.empty()) {
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
    }
    if (!output.good()) {
        return "failed to write the output file";
    }
    return std::nullopt;
}

std::string extracted_layers_directory_name(
    const std::filesystem::path& atlas_output_path) {
    const std::string stem = atlas_output_path.stem().string();
    return (stem.empty() ? std::string("psd_layers") : stem) + "_layers";
}

std::filesystem::path default_extracted_layers_directory(
    const PsdImportOptions& options) {
    const std::filesystem::path parent =
        options.atlas_output_path.has_parent_path()
            ? options.atlas_output_path.parent_path()
            : std::filesystem::path(".");
    return (parent / extracted_layers_directory_name(options.atlas_output_path)).lexically_normal();
}

std::filesystem::path effective_existing_skeleton_path(const PsdImportOptions& options) {
    if (options.existing_skeleton_path.has_value()) {
        return options.existing_skeleton_path->lexically_normal();
    }
    return options.skeleton_output_path.lexically_normal();
}

std::string existing_string_member(
    const Value& object,
    std::string_view key,
    std::string fallback) {
    const Value* value = marrow::runtime::json::find_member(object, key);
    if (value == nullptr || !value->is_string() || value->as_string().empty()) {
        return fallback;
    }
    return value->as_string();
}

Value::Object* ensure_root_object(Document* document) {
    if (!document->root.is_object()) {
        document->root = make_object_value();
    }
    return &document->root.as_object();
}

Value build_bones_value(const ParsedPsdDocument& parsed) {
    Value::Array bones;
    bones.reserve(parsed.groups.size() + 1U);

    Value::Object root_bone;
    root_bone.emplace("name", make_string_value("root"));
    bones.push_back(make_object_value(std::move(root_bone)));

    for (const GroupRecord& group : parsed.groups) {
        Value::Object bone;
        bone.emplace("name", make_string_value(group.bone_name));
        bone.emplace(
            "parent",
            make_string_value(
                group.parent_group_index.has_value()
                    ? parsed.groups[*group.parent_group_index].bone_name
                    : std::string("root")));
        const int parent_left = group.parent_group_index.has_value()
            ? parsed.groups[*group.parent_group_index].left
            : 0;
        const int parent_top = group.parent_group_index.has_value()
            ? parsed.groups[*group.parent_group_index].top
            : 0;
        bone.emplace("x", make_number_value(static_cast<double>(group.left - parent_left)));
        bone.emplace("y", make_number_value(static_cast<double>(group.top - parent_top)));
        bones.push_back(make_object_value(std::move(bone)));
    }

    return make_array_value(std::move(bones));
}

Value build_slots_value(const ParsedPsdDocument& parsed) {
    std::vector<const ParsedLayer*> ordered_layers;
    ordered_layers.reserve(parsed.layers.size());
    for (const ParsedLayer& layer : parsed.layers) {
        ordered_layers.push_back(&layer);
    }
    std::sort(
        ordered_layers.begin(),
        ordered_layers.end(),
        [](const ParsedLayer* lhs, const ParsedLayer* rhs) {
            return lhs->stack_order > rhs->stack_order;
        });

    Value::Array slots;
    slots.reserve(ordered_layers.size());
    for (const ParsedLayer* layer : ordered_layers) {
        Value::Object slot;
        slot.emplace("name", make_string_value(layer->slot_name));
        slot.emplace(
            "bone",
            make_string_value(
                layer->parent_group_index.has_value()
                    ? parsed.groups[*layer->parent_group_index].bone_name
                    : std::string("root")));
        slot.emplace("attachment", make_string_value(layer->attachment_name));
        slots.push_back(make_object_value(std::move(slot)));
    }
    return make_array_value(std::move(slots));
}

Document build_skeleton_document(
    const ParsedPsdDocument& parsed,
    const PsdImportOptions& options,
    const Document* existing_document) {
    Document document;
    if (existing_document != nullptr) {
        document = *existing_document;
    }

    Value::Object* root = ensure_root_object(&document);
    (*root)["marrow"] = make_string_value("1.0");
    (*root)["version"] = make_number_value(1.0);

    Value skeleton_value = make_object_value();
    if (const Value* existing_skeleton = marrow::runtime::json::find_member(document.root, "skeleton");
        existing_skeleton != nullptr && existing_skeleton->is_object()) {
        skeleton_value = *existing_skeleton;
    }
    if (!skeleton_value.is_object()) {
        skeleton_value = make_object_value();
    }
    skeleton_value.as_object()["name"] = make_string_value(
        existing_string_member(
            skeleton_value,
            "name",
            options.psd_path.stem().empty() ? std::string("psd_import") : options.psd_path.stem().string()));
    skeleton_value.as_object()["width"] = make_number_value(static_cast<double>(parsed.width));
    skeleton_value.as_object()["height"] = make_number_value(static_cast<double>(parsed.height));
    (*root)["skeleton"] = std::move(skeleton_value);

    (*root)["bones"] = build_bones_value(parsed);
    (*root)["slots"] = build_slots_value(parsed);
    root->erase("skins");

    Value* animations = marrow::runtime::json::find_member(document.root, "animations");
    if (animations == nullptr || !animations->is_object() || animations->as_object().empty()) {
        Value::Object default_animations;
        default_animations.emplace("idle", make_object_value());
        (*root)["animations"] = make_object_value(std::move(default_animations));
    }

    document.source_path = options.skeleton_output_path;
    return document;
}

bool is_safe_generated_directory(const std::filesystem::path& path) {
    if (path.empty()) {
        return false;
    }

    const std::filesystem::path normalized = path.lexically_normal();
    if (normalized.empty() || normalized == normalized.root_path()) {
        return false;
    }

    const std::string filename = normalized.filename().string();
    return !filename.empty() && filename != "." && filename != "..";
}

std::optional<std::string> write_imported_layers(
    ParsedPsdDocument* parsed,
    const std::filesystem::path& extracted_layers_directory) {
    if (!is_safe_generated_directory(extracted_layers_directory)) {
        return "refusing to clear an unsafe extracted layer output directory";
    }

    std::error_code remove_error;
    std::filesystem::remove_all(extracted_layers_directory, remove_error);
    if (remove_error) {
        return remove_error.message();
    }

    std::set<std::string> used_file_stems;
    for (ParsedLayer& layer : parsed->layers) {
        const std::string file_stem =
            make_unique_name(sanitize_file_component(layer.slot_name), &used_file_stems);
        layer.extracted_image_path =
            (extracted_layers_directory / (file_stem + ".png")).lexically_normal();
        if (const auto error = marrow::editor::detail::write_rgba_png(
                layer.extracted_image_path,
                layer.width,
                layer.height,
                layer.rgba8)) {
            return *error;
        }
    }

    return std::nullopt;
}

detail::PackedAtlasArtifactResult build_imported_atlas(
    const ParsedPsdDocument& parsed,
    const PsdImportOptions& options) {
    ProjectData project;
    project.source_path = options.atlas_output_path;

    AtlasPackDefinition definition;
    definition.atlas_path = options.atlas_output_path;
    definition.atlas_name =
        options.atlas_name.empty() ? options.atlas_output_path.stem().string() : options.atlas_name;
    definition.trim = false;
    definition.padding = 2;
    definition.bleed = 1;

    for (const ParsedLayer& layer : parsed.layers) {
        const int parent_left = layer.parent_group_index.has_value()
            ? parsed.groups[*layer.parent_group_index].left
            : 0;
        const int parent_top = layer.parent_group_index.has_value()
            ? parsed.groups[*layer.parent_group_index].top
            : 0;

        AtlasPackSprite sprite;
        sprite.region_name = layer.attachment_name;
        sprite.image_path = layer.extracted_image_path;
        sprite.origin_x = -static_cast<double>(layer.left - parent_left);
        sprite.origin_y = -static_cast<double>(layer.top - parent_top);
        definition.sprites.push_back(std::move(sprite));
    }

    return detail::build_packed_atlas_artifact(
        project,
        definition,
        options.atlas_output_path.lexically_normal());
}

void populate_result_metadata(
    const ParsedPsdDocument& parsed,
    const std::filesystem::path& extracted_layers_directory,
    PsdImportResult* result_out) {
    result_out->extracted_layers_directory = extracted_layers_directory;
    result_out->layers.clear();
    result_out->layers.reserve(parsed.layers.size());
    for (const ParsedLayer& layer : parsed.layers) {
        PsdImportedLayer imported_layer;
        imported_layer.name = layer.original_name;
        imported_layer.group_path = layer.group_path;
        imported_layer.slot_name = layer.slot_name;
        imported_layer.attachment_name = layer.attachment_name;
        imported_layer.bone_name =
            layer.parent_group_index.has_value()
                ? parsed.groups[*layer.parent_group_index].bone_name
                : std::string("root");
        imported_layer.extracted_image_path = layer.extracted_image_path;
        imported_layer.left = layer.left;
        imported_layer.top = layer.top;
        imported_layer.width = layer.width;
        imported_layer.height = layer.height;
        result_out->layers.push_back(std::move(imported_layer));
    }

    result_out->bones.clear();
    result_out->bones.push_back(PsdBoneHint{"root", std::nullopt, 0.0, 0.0});
    for (const GroupRecord& group : parsed.groups) {
        const int parent_left = group.parent_group_index.has_value()
            ? parsed.groups[*group.parent_group_index].left
            : 0;
        const int parent_top = group.parent_group_index.has_value()
            ? parsed.groups[*group.parent_group_index].top
            : 0;
        result_out->bones.push_back(PsdBoneHint{
            group.bone_name,
            group.parent_group_index.has_value()
                ? std::optional<std::string>(parsed.groups[*group.parent_group_index].bone_name)
                : std::optional<std::string>(std::string("root")),
            static_cast<double>(group.left - parent_left),
            static_cast<double>(group.top - parent_top),
        });
    }
}

PsdImportResult make_error_result(
    const std::filesystem::path& path,
    std::string message) {
    PsdImportResult result;
    result.error = PsdImportError{path, std::move(message)};
    return result;
}

} // namespace

std::string PsdImportError::format() const {
    if (path.empty()) {
        return "PSD import failed: " + message;
    }
    return "PSD import failed for '" + path.string() + "': " + message;
}

PsdImportResult import_psd_to_runtime_bundle(const PsdImportOptions& options) {
    if (options.psd_path.empty()) {
        return make_error_result(options.psd_path, "input PSD path must not be empty");
    }
    if (options.skeleton_output_path.empty()) {
        return make_error_result(options.skeleton_output_path, "output skeleton path must not be empty");
    }
    if (options.atlas_output_path.empty()) {
        return make_error_result(options.atlas_output_path, "output atlas path must not be empty");
    }

    std::vector<std::uint8_t> bytes;
    if (const auto error = read_binary_file(options.psd_path, &bytes)) {
        return make_error_result(options.psd_path, *error);
    }

    ParsedPsdDocument parsed;
    if (const auto error = parse_psd_document(bytes, &parsed)) {
        return make_error_result(options.psd_path, *error);
    }

    const std::filesystem::path extracted_layers_directory =
        options.extracted_layers_directory.empty()
            ? default_extracted_layers_directory(options)
            : options.extracted_layers_directory.lexically_normal();
    if (extracted_layers_directory.empty()) {
        return make_error_result(
            options.atlas_output_path,
            "extracted layer output directory must not be empty");
    }

    if (const auto error = write_imported_layers(&parsed, extracted_layers_directory)) {
        return make_error_result(extracted_layers_directory, *error);
    }

    std::optional<Document> existing_document;
    const std::filesystem::path existing_skeleton_path = effective_existing_skeleton_path(options);
    if (!existing_skeleton_path.empty() && std::filesystem::exists(existing_skeleton_path)) {
        const auto load_result = marrow::runtime::load_skeleton_document(existing_skeleton_path);
        if (!load_result) {
            return make_error_result(existing_skeleton_path, load_result.error->format());
        }
        existing_document = std::move(*load_result.document);
    }

    const auto atlas_result = build_imported_atlas(parsed, options);
    if (!atlas_result) {
        return make_error_result(options.atlas_output_path, atlas_result.error_message);
    }
    if (const auto error = write_binary_file(
            atlas_result.artifact->image_path,
            atlas_result.artifact->image_bytes)) {
        return make_error_result(atlas_result.artifact->image_path, *error);
    }
    if (const auto error = write_text_file(
            atlas_result.artifact->atlas_path,
            atlas_result.artifact->atlas_text)) {
        return make_error_result(atlas_result.artifact->atlas_path, *error);
    }

    const Document skeleton_document =
        build_skeleton_document(parsed, options, existing_document ? &*existing_document : nullptr);
    if (const auto error = write_text_file(
            options.skeleton_output_path,
            marrow::runtime::json::serialize_pretty(skeleton_document.root))) {
        return make_error_result(options.skeleton_output_path, *error);
    }

    PsdImportResult result;
    result.skeleton_path = options.skeleton_output_path.lexically_normal();
    result.atlas_path = atlas_result.artifact->atlas_path;
    result.texture_path = atlas_result.artifact->image_path;
    populate_result_metadata(parsed, extracted_layers_directory, &result);
    return result;
}

} // namespace marrow::editor
