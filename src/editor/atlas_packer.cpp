#include "atlas_packer.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <string_view>
#include <utility>

#include <zlib.h>

namespace marrow::editor::detail {
namespace {

using marrow::runtime::json::SourceLocation;
using marrow::runtime::json::Value;

constexpr std::array<std::uint8_t, 8> kPngSignature{{
    137, 80, 78, 71, 13, 10, 26, 10,
}};
constexpr SourceLocation kGeneratedLocation{};

struct ImageRgba {
    int width{0};
    int height{0};
    std::vector<std::uint8_t> rgba8;
};

struct Rect {
    int x{0};
    int y{0};
    int width{0};
    int height{0};
};

struct PreparedSprite {
    std::string region_name;
    std::filesystem::path image_path;
    ImageRgba image;
    int trim_left{0};
    int trim_top{0};
    int trim_width{0};
    int trim_height{0};
    int outer_width{0};
    int outer_height{0};
    double origin_x{0.0};
    double origin_y{0.0};
    Rect packed_bounds{};
};

struct PackedLayout {
    bool fits{false};
    int used_width{0};
    int used_height{0};
    std::vector<Rect> placements;
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

std::optional<std::string> read_binary_file(
    const std::filesystem::path& path,
    std::vector<std::uint8_t>* bytes_out) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return "Failed to open image file " + path.string() + ".";
    }

    input.seekg(0, std::ios::end);
    const std::streamoff file_size = input.tellg();
    if (file_size < 0) {
        return "Failed to determine image file size for " + path.string() + ".";
    }

    bytes_out->resize(static_cast<std::size_t>(file_size));
    input.seekg(0, std::ios::beg);
    if (!bytes_out->empty()) {
        input.read(reinterpret_cast<char*>(bytes_out->data()), file_size);
    }
    if (!input) {
        return "Failed to read image file " + path.string() + ".";
    }

    return std::nullopt;
}

std::uint32_t read_big_endian_u32(const std::uint8_t* bytes) {
    return
        (static_cast<std::uint32_t>(bytes[0]) << 24) |
        (static_cast<std::uint32_t>(bytes[1]) << 16) |
        (static_cast<std::uint32_t>(bytes[2]) << 8) |
        static_cast<std::uint32_t>(bytes[3]);
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
    ImageRgba* image_out) {
    if (png_bytes.size() < kPngSignature.size() ||
        !std::equal(kPngSignature.begin(), kPngSignature.end(), png_bytes.begin())) {
        return "Image does not contain a valid PNG signature.";
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
        if ((png_bytes.size() - cursor) < 12U) {
            return "PNG chunk header is truncated.";
        }

        const std::uint32_t chunk_length = read_big_endian_u32(&png_bytes[cursor]);
        cursor += 4U;
        const std::string_view chunk_type(
            reinterpret_cast<const char*>(&png_bytes[cursor]),
            4U);
        cursor += 4U;

        std::size_t total_chunk_bytes = 0;
        if (!checked_add(static_cast<std::size_t>(chunk_length), 4U, &total_chunk_bytes) ||
            cursor + total_chunk_bytes > png_bytes.size()) {
            return "PNG chunk overruns the image file.";
        }

        const std::uint8_t* chunk_data = &png_bytes[cursor];
        cursor += static_cast<std::size_t>(chunk_length);
        cursor += 4U;

        if (chunk_type == "IHDR") {
            if (chunk_length != 13U) {
                return "PNG IHDR chunk had an unexpected size.";
            }

            width = read_big_endian_u32(chunk_data);
            height = read_big_endian_u32(chunk_data + 4U);
            bit_depth = chunk_data[8];
            color_type = chunk_data[9];
            const std::uint8_t compression_method = chunk_data[10];
            const std::uint8_t filter_method = chunk_data[11];
            const std::uint8_t interlace_method = chunk_data[12];
            if (width == 0U || height == 0U) {
                return "PNG image dimensions must be greater than zero.";
            }
            if (bit_depth != 8U) {
                return "PNG images must use 8-bit channels.";
            }
            if (png_channel_count(color_type) == 0) {
                return "PNG images must use RGB or RGBA color data.";
            }
            if (compression_method != 0U || filter_method != 0U || interlace_method != 0U) {
                return "PNG images must use standard non-interlaced encoding.";
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
        return "PNG image is missing the IHDR chunk.";
    }
    if (!saw_end) {
        return "PNG image is missing the IEND chunk.";
    }
    if (compressed_image_data.empty()) {
        return "PNG image did not contain any IDAT payload.";
    }
    if (width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        return "PNG image dimensions exceed supported limits.";
    }

    const std::size_t channels = static_cast<std::size_t>(png_channel_count(color_type));
    std::size_t row_stride = 0;
    if (!checked_multiply(static_cast<std::size_t>(width), channels, &row_stride)) {
        return "PNG image row stride overflowed size_t.";
    }
    std::size_t filtered_row_bytes = 0;
    if (!checked_add(row_stride, 1U, &filtered_row_bytes)) {
        return "PNG image row size overflowed size_t.";
    }
    std::size_t expected_decompressed_size = 0;
    if (!checked_multiply(
            static_cast<std::size_t>(height),
            filtered_row_bytes,
            &expected_decompressed_size)) {
        return "PNG image buffer is too large to decode safely.";
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
        return "PNG DEFLATE stream could not be decompressed.";
    }

    std::size_t unpacked_size = 0;
    if (!checked_multiply(static_cast<std::size_t>(height), row_stride, &unpacked_size)) {
        return "PNG image unpacked buffer overflowed size_t.";
    }
    std::vector<std::uint8_t> unpacked(unpacked_size);
    for (std::size_t row = 0; row < static_cast<std::size_t>(height); ++row) {
        const std::size_t filtered_row_offset = row * filtered_row_bytes;
        const std::uint8_t filter_type = decompressed[filtered_row_offset];
        const std::uint8_t* filtered_row = decompressed.data() + filtered_row_offset + 1U;
        std::uint8_t* output_row = unpacked.data() + (row * row_stride);
        for (std::size_t column = 0; column < row_stride; ++column) {
            const std::uint8_t raw = filtered_row[column];
            const std::uint8_t left = column >= channels ? output_row[column - channels] : 0U;
            const std::uint8_t up =
                row > 0U ? unpacked[((row - 1U) * row_stride) + column] : 0U;
            const std::uint8_t up_left =
                (row > 0U && column >= channels)
                    ? unpacked[((row - 1U) * row_stride) + column - channels]
                    : 0U;

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
                return "PNG image used an unsupported scanline filter.";
            }
        }
    }

    std::size_t rgba_size = 0;
    if (!checked_multiply(
            static_cast<std::size_t>(width),
            static_cast<std::size_t>(height),
            &rgba_size) ||
        !checked_multiply(rgba_size, 4U, &rgba_size)) {
        return "PNG image RGBA output buffer overflowed size_t.";
    }

    ImageRgba image;
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
            image.rgba8[destination_index + 0U] = unpacked[source_index + 0U];
            image.rgba8[destination_index + 1U] = unpacked[source_index + 1U];
            image.rgba8[destination_index + 2U] = unpacked[source_index + 2U];
            image.rgba8[destination_index + 3U] =
                channels == 4U ? unpacked[source_index + 3U] : 255U;
        }
    }

    *image_out = std::move(image);
    return std::nullopt;
}

std::optional<std::string> load_png_rgba8(
    const std::filesystem::path& path,
    ImageRgba* image_out) {
    std::vector<std::uint8_t> png_bytes;
    if (const auto error = read_binary_file(path, &png_bytes)) {
        return error;
    }
    return decode_png_rgba8(png_bytes, image_out);
}

void append_u32_be(std::vector<std::uint8_t>* bytes_out, std::uint32_t value) {
    bytes_out->push_back(static_cast<std::uint8_t>((value >> 24U) & 0xffU));
    bytes_out->push_back(static_cast<std::uint8_t>((value >> 16U) & 0xffU));
    bytes_out->push_back(static_cast<std::uint8_t>((value >> 8U) & 0xffU));
    bytes_out->push_back(static_cast<std::uint8_t>(value & 0xffU));
}

void append_png_chunk(
    std::vector<std::uint8_t>* bytes_out,
    std::string_view type,
    const std::vector<std::uint8_t>& chunk_data) {
    append_u32_be(bytes_out, static_cast<std::uint32_t>(chunk_data.size()));
    const std::size_t chunk_start = bytes_out->size();
    bytes_out->insert(bytes_out->end(), type.begin(), type.end());
    bytes_out->insert(bytes_out->end(), chunk_data.begin(), chunk_data.end());
    uLong crc = crc32(0L, Z_NULL, 0U);
    crc = crc32(
        crc,
        bytes_out->data() + static_cast<std::ptrdiff_t>(chunk_start),
        static_cast<uInt>(type.size() + chunk_data.size()));
    append_u32_be(bytes_out, static_cast<std::uint32_t>(crc));
}

std::optional<std::string> encode_rgba_png(
    int width,
    int height,
    const std::vector<std::uint8_t>& rgba8,
    std::vector<std::uint8_t>* png_bytes_out) {
    if (width <= 0 || height <= 0) {
        return "PNG output dimensions must be greater than zero.";
    }

    std::size_t expected_bytes = 0;
    if (!checked_multiply(static_cast<std::size_t>(width), static_cast<std::size_t>(height),
                          &expected_bytes) ||
        !checked_multiply(expected_bytes, 4U, &expected_bytes) ||
        rgba8.size() != expected_bytes) {
        return "PNG output RGBA buffer size does not match the image dimensions.";
    }

    std::size_t row_stride = 0;
    if (!checked_multiply(static_cast<std::size_t>(width), 4U, &row_stride)) {
        return "PNG output row stride overflowed size_t.";
    }
    std::size_t filtered_row_bytes = 0;
    if (!checked_add(row_stride, 1U, &filtered_row_bytes)) {
        return "PNG output row size overflowed size_t.";
    }
    std::size_t raw_size = 0;
    if (!checked_multiply(static_cast<std::size_t>(height), filtered_row_bytes, &raw_size)) {
        return "PNG output buffer is too large.";
    }

    std::vector<std::uint8_t> raw_bytes(raw_size, 0U);
    for (int row = 0; row < height; ++row) {
        const std::size_t raw_offset = static_cast<std::size_t>(row) * filtered_row_bytes;
        const std::size_t source_offset = static_cast<std::size_t>(row) * row_stride;
        raw_bytes[raw_offset] = 0U;
        std::copy(
            rgba8.begin() + static_cast<std::ptrdiff_t>(source_offset),
            rgba8.begin() + static_cast<std::ptrdiff_t>(source_offset + row_stride),
            raw_bytes.begin() + static_cast<std::ptrdiff_t>(raw_offset + 1U));
    }

    uLongf compressed_capacity = compressBound(static_cast<uLong>(raw_bytes.size()));
    std::vector<std::uint8_t> compressed_bytes(static_cast<std::size_t>(compressed_capacity));
    const int zlib_status = compress2(
        compressed_bytes.data(),
        &compressed_capacity,
        raw_bytes.data(),
        static_cast<uLong>(raw_bytes.size()),
        Z_BEST_COMPRESSION);
    if (zlib_status != Z_OK) {
        return "Failed to compress PNG image data.";
    }
    compressed_bytes.resize(static_cast<std::size_t>(compressed_capacity));

    std::vector<std::uint8_t> png_bytes;
    png_bytes.insert(png_bytes.end(), kPngSignature.begin(), kPngSignature.end());

    std::vector<std::uint8_t> ihdr_bytes;
    ihdr_bytes.reserve(13U);
    append_u32_be(&ihdr_bytes, static_cast<std::uint32_t>(width));
    append_u32_be(&ihdr_bytes, static_cast<std::uint32_t>(height));
    ihdr_bytes.push_back(8U);
    ihdr_bytes.push_back(6U);
    ihdr_bytes.push_back(0U);
    ihdr_bytes.push_back(0U);
    ihdr_bytes.push_back(0U);
    append_png_chunk(&png_bytes, "IHDR", ihdr_bytes);
    append_png_chunk(&png_bytes, "IDAT", compressed_bytes);
    append_png_chunk(&png_bytes, "IEND", {});

    *png_bytes_out = std::move(png_bytes);
    return std::nullopt;
}

std::size_t pixel_index(const ImageRgba& image, int x, int y) {
    return (static_cast<std::size_t>(y) * static_cast<std::size_t>(image.width) +
            static_cast<std::size_t>(x)) * 4U;
}

const std::uint8_t* pixel_ptr(const ImageRgba& image, int x, int y) {
    return image.rgba8.data() + static_cast<std::ptrdiff_t>(pixel_index(image, x, y));
}

std::uint8_t* pixel_ptr(ImageRgba* image, int x, int y) {
    return image->rgba8.data() + static_cast<std::ptrdiff_t>(pixel_index(*image, x, y));
}

void copy_pixel(
    const ImageRgba& source,
    int source_x,
    int source_y,
    ImageRgba* destination,
    int destination_x,
    int destination_y) {
    const std::uint8_t* source_pixel = pixel_ptr(source, source_x, source_y);
    std::uint8_t* destination_pixel = pixel_ptr(destination, destination_x, destination_y);
    std::copy(source_pixel, source_pixel + 4, destination_pixel);
}

bool rect_contains(const Rect& outer, const Rect& inner) {
    return inner.x >= outer.x &&
        inner.y >= outer.y &&
        inner.x + inner.width <= outer.x + outer.width &&
        inner.y + inner.height <= outer.y + outer.height;
}

bool rects_intersect(const Rect& left, const Rect& right) {
    return left.x < right.x + right.width &&
        left.x + left.width > right.x &&
        left.y < right.y + right.height &&
        left.y + left.height > right.y;
}

void add_rect_if_valid(std::vector<Rect>* rects_out, Rect rect) {
    if (rect.width <= 0 || rect.height <= 0) {
        return;
    }
    rects_out->push_back(rect);
}

void prune_free_rectangles(std::vector<Rect>* free_rectangles) {
    for (std::size_t index = 0; index < free_rectangles->size(); ++index) {
        bool erased = false;
        for (std::size_t other_index = 0; other_index < free_rectangles->size(); ++other_index) {
            if (index == other_index) {
                continue;
            }
            if (rect_contains((*free_rectangles)[other_index], (*free_rectangles)[index])) {
                free_rectangles->erase(
                    free_rectangles->begin() + static_cast<std::ptrdiff_t>(index));
                --index;
                erased = true;
                break;
            }
        }
        if (erased) {
            continue;
        }
    }
}

void split_free_rectangles(const Rect& used, std::vector<Rect>* free_rectangles) {
    for (std::size_t index = 0; index < free_rectangles->size();) {
        const Rect free_rect = (*free_rectangles)[index];
        if (!rects_intersect(free_rect, used)) {
            ++index;
            continue;
        }

        free_rectangles->erase(free_rectangles->begin() + static_cast<std::ptrdiff_t>(index));

        add_rect_if_valid(
            free_rectangles,
            Rect{
                free_rect.x,
                free_rect.y,
                used.x - free_rect.x,
                free_rect.height,
            });
        add_rect_if_valid(
            free_rectangles,
            Rect{
                used.x + used.width,
                free_rect.y,
                (free_rect.x + free_rect.width) - (used.x + used.width),
                free_rect.height,
            });
        add_rect_if_valid(
            free_rectangles,
            Rect{
                free_rect.x,
                free_rect.y,
                free_rect.width,
                used.y - free_rect.y,
            });
        add_rect_if_valid(
            free_rectangles,
            Rect{
                free_rect.x,
                used.y + used.height,
                free_rect.width,
                (free_rect.y + free_rect.height) - (used.y + used.height),
            });
    }

    prune_free_rectangles(free_rectangles);
}

PackedLayout pack_rectangles_best_short_side_fit(
    const std::vector<Rect>& rectangles,
    int bin_width,
    int bin_height) {
    PackedLayout layout;
    if (bin_width <= 0 || bin_height <= 0) {
        return layout;
    }

    std::vector<Rect> free_rectangles;
    free_rectangles.push_back(Rect{0, 0, bin_width, bin_height});
    layout.placements.resize(rectangles.size());

    for (std::size_t rect_index = 0; rect_index < rectangles.size(); ++rect_index) {
        const Rect& rect = rectangles[rect_index];
        int best_short_side_fit = std::numeric_limits<int>::max();
        int best_long_side_fit = std::numeric_limits<int>::max();
        std::optional<Rect> chosen_rect;

        for (const Rect& free_rect : free_rectangles) {
            if (rect.width > free_rect.width || rect.height > free_rect.height) {
                continue;
            }

            const int leftover_width = free_rect.width - rect.width;
            const int leftover_height = free_rect.height - rect.height;
            const int short_side_fit = std::min(leftover_width, leftover_height);
            const int long_side_fit = std::max(leftover_width, leftover_height);
            if (short_side_fit > best_short_side_fit ||
                (short_side_fit == best_short_side_fit &&
                 long_side_fit >= best_long_side_fit)) {
                continue;
            }

            chosen_rect = Rect{
                free_rect.x,
                free_rect.y,
                rect.width,
                rect.height,
            };
            best_short_side_fit = short_side_fit;
            best_long_side_fit = long_side_fit;
        }

        if (!chosen_rect.has_value()) {
            layout.placements.clear();
            return layout;
        }

        layout.placements[rect_index] = *chosen_rect;
        split_free_rectangles(*chosen_rect, &free_rectangles);
        layout.used_width =
            std::max(layout.used_width, chosen_rect->x + chosen_rect->width);
        layout.used_height =
            std::max(layout.used_height, chosen_rect->y + chosen_rect->height);
    }

    layout.fits = true;
    return layout;
}

std::optional<PreparedSprite> prepare_sprite(
    const ProjectData& project,
    const AtlasPackDefinition& definition,
    const AtlasPackSprite& sprite,
    std::string* error_out) {
    PreparedSprite prepared;
    prepared.region_name = sprite.region_name;
    prepared.image_path = project.resolve_path(sprite.image_path);

    if (const auto error = load_png_rgba8(prepared.image_path, &prepared.image)) {
        *error_out = *error;
        return std::nullopt;
    }

    prepared.outer_width = prepared.image.width;
    prepared.outer_height = prepared.image.height;
    const double default_origin_x = static_cast<double>(prepared.outer_width) * 0.5;
    const double default_origin_y = static_cast<double>(prepared.outer_height) * 0.5;

    int min_x = prepared.image.width;
    int min_y = prepared.image.height;
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < prepared.image.height; ++y) {
        for (int x = 0; x < prepared.image.width; ++x) {
            if (pixel_ptr(prepared.image, x, y)[3] == 0U) {
                continue;
            }
            min_x = std::min(min_x, x);
            min_y = std::min(min_y, y);
            max_x = std::max(max_x, x);
            max_y = std::max(max_y, y);
        }
    }

    if (max_x < min_x || max_y < min_y) {
        *error_out = "Atlas pack sprite '" + sprite.region_name +
            "' is fully transparent and cannot be trimmed.";
        return std::nullopt;
    }

    if (!definition.trim) {
        min_x = 0;
        min_y = 0;
        max_x = prepared.image.width - 1;
        max_y = prepared.image.height - 1;
    }

    prepared.trim_left = min_x;
    prepared.trim_top = min_y;
    prepared.trim_width = max_x - min_x + 1;
    prepared.trim_height = max_y - min_y + 1;
    prepared.origin_x = sprite.origin_x.value_or(default_origin_x) - static_cast<double>(min_x);
    prepared.origin_y = sprite.origin_y.value_or(default_origin_y) - static_cast<double>(min_y);
    prepared.packed_bounds.width = prepared.trim_width + (definition.padding * 2);
    prepared.packed_bounds.height = prepared.trim_height + (definition.padding * 2);
    return prepared;
}

std::optional<std::string> build_best_layout(
    std::vector<PreparedSprite>* sprites_in_out,
    int* atlas_width_out,
    int* atlas_height_out) {
    if (sprites_in_out->empty()) {
        return "Atlas pack definitions require at least one sprite image.";
    }

    int max_rect_width = 0;
    int sum_rect_width = 0;
    int sum_rect_height = 0;
    for (const PreparedSprite& sprite : *sprites_in_out) {
        max_rect_width = std::max(max_rect_width, sprite.packed_bounds.width);
        sum_rect_width += sprite.packed_bounds.width;
        sum_rect_height += sprite.packed_bounds.height;
    }

    std::vector<std::size_t> order(sprites_in_out->size());
    for (std::size_t index = 0; index < order.size(); ++index) {
        order[index] = index;
    }
    std::sort(
        order.begin(),
        order.end(),
        [&](std::size_t left, std::size_t right) {
            const PreparedSprite& lhs = (*sprites_in_out)[left];
            const PreparedSprite& rhs = (*sprites_in_out)[right];
            const int lhs_longest = std::max(lhs.packed_bounds.width, lhs.packed_bounds.height);
            const int rhs_longest = std::max(rhs.packed_bounds.width, rhs.packed_bounds.height);
            if (lhs_longest != rhs_longest) {
                return lhs_longest > rhs_longest;
            }
            const int lhs_area = lhs.packed_bounds.width * lhs.packed_bounds.height;
            const int rhs_area = rhs.packed_bounds.width * rhs.packed_bounds.height;
            if (lhs_area != rhs_area) {
                return lhs_area > rhs_area;
            }
            return lhs.region_name < rhs.region_name;
        });

    bool found_layout = false;
    std::size_t best_area = std::numeric_limits<std::size_t>::max();
    int best_max_side = std::numeric_limits<int>::max();
    int best_width = 0;
    int best_height = 0;
    std::vector<Rect> best_placements;

    for (int candidate_width = max_rect_width; candidate_width <= sum_rect_width; ++candidate_width) {
        std::vector<Rect> rectangles;
        rectangles.reserve(order.size());
        for (const std::size_t sprite_index : order) {
            rectangles.push_back((*sprites_in_out)[sprite_index].packed_bounds);
        }

        PackedLayout candidate_layout = pack_rectangles_best_short_side_fit(
            rectangles,
            candidate_width,
            sum_rect_height);
        if (!candidate_layout.fits) {
            continue;
        }

        const std::size_t used_area =
            static_cast<std::size_t>(candidate_layout.used_width) *
            static_cast<std::size_t>(candidate_layout.used_height);
        const int used_max_side =
            std::max(candidate_layout.used_width, candidate_layout.used_height);
        if (found_layout &&
            (used_area > best_area ||
             (used_area == best_area && used_max_side > best_max_side) ||
             (used_area == best_area && used_max_side == best_max_side &&
              candidate_layout.used_width >= best_width))) {
            continue;
        }

        found_layout = true;
        best_area = used_area;
        best_max_side = used_max_side;
        best_width = candidate_layout.used_width;
        best_height = candidate_layout.used_height;
        best_placements = std::move(candidate_layout.placements);
    }

    if (!found_layout) {
        return "MaxRects packing could not fit the requested sprite set.";
    }

    for (std::size_t order_index = 0; order_index < order.size(); ++order_index) {
        (*sprites_in_out)[order[order_index]].packed_bounds = best_placements[order_index];
    }
    *atlas_width_out = best_width;
    *atlas_height_out = best_height;
    return std::nullopt;
}

void blit_trimmed_sprite(
    const PreparedSprite& sprite,
    const AtlasPackDefinition& definition,
    ImageRgba* atlas_image) {
    const int destination_x = sprite.packed_bounds.x + definition.padding;
    const int destination_y = sprite.packed_bounds.y + definition.padding;
    for (int y = 0; y < sprite.trim_height; ++y) {
        for (int x = 0; x < sprite.trim_width; ++x) {
            copy_pixel(
                sprite.image,
                sprite.trim_left + x,
                sprite.trim_top + y,
                atlas_image,
                destination_x + x,
                destination_y + y);
        }
    }

    if (definition.bleed <= 0) {
        return;
    }

    const int left = destination_x;
    const int top = destination_y;
    const int right = destination_x + sprite.trim_width - 1;
    const int bottom = destination_y + sprite.trim_height - 1;
    for (int offset = 1; offset <= definition.bleed; ++offset) {
        for (int x = 0; x < sprite.trim_width; ++x) {
            copy_pixel(*atlas_image, left + x, top, atlas_image, left + x, top - offset);
            copy_pixel(*atlas_image, left + x, bottom, atlas_image, left + x, bottom + offset);
        }
        for (int y = 0; y < sprite.trim_height; ++y) {
            copy_pixel(*atlas_image, left, top + y, atlas_image, left - offset, top + y);
            copy_pixel(*atlas_image, right, top + y, atlas_image, right + offset, top + y);
        }
        for (int corner_y = 1; corner_y <= definition.bleed; ++corner_y) {
            copy_pixel(*atlas_image, left, top, atlas_image, left - offset, top - corner_y);
            copy_pixel(*atlas_image, right, top, atlas_image, right + offset, top - corner_y);
            copy_pixel(*atlas_image, left, bottom, atlas_image, left - offset, bottom + corner_y);
            copy_pixel(
                *atlas_image,
                right,
                bottom,
                atlas_image,
                right + offset,
                bottom + corner_y);
        }
    }
}

std::string default_atlas_name(
    const AtlasPackDefinition& definition,
    const std::filesystem::path& exported_atlas_path) {
    if (!definition.atlas_name.empty()) {
        return definition.atlas_name;
    }
    if (!exported_atlas_path.stem().empty()) {
        return exported_atlas_path.stem().string();
    }
    if (!definition.atlas_path.stem().empty()) {
        return definition.atlas_path.stem().string();
    }
    return "packed_atlas";
}

std::string build_atlas_document_text(
    const AtlasPackDefinition& definition,
    const std::filesystem::path& image_path,
    int atlas_width,
    int atlas_height,
    const std::vector<PreparedSprite>& sprites) {
    Value::Object root;
    root.emplace("marrow", make_string_value("1.0"));
    root.emplace("version", make_number_value(1.0));

    Value::Object atlas_object;
    atlas_object.emplace("name", make_string_value(default_atlas_name(definition, definition.atlas_path)));
    atlas_object.emplace("image", make_string_value(image_path.filename().generic_string()));
    atlas_object.emplace("width", make_number_value(static_cast<double>(atlas_width)));
    atlas_object.emplace("height", make_number_value(static_cast<double>(atlas_height)));
    atlas_object.emplace("filter_min", make_string_value(definition.filter_min));
    atlas_object.emplace("filter_mag", make_string_value(definition.filter_mag));
    atlas_object.emplace("wrap_x", make_string_value(definition.wrap_x));
    atlas_object.emplace("wrap_y", make_string_value(definition.wrap_y));
    atlas_object.emplace(
        "premultiplied_alpha",
        make_boolean_value(definition.premultiplied_alpha));
    root.emplace("atlas", make_object_value(std::move(atlas_object)));

    Value::Array regions;
    regions.reserve(sprites.size());
    for (const PreparedSprite& sprite : sprites) {
        Value::Object region_object;
        region_object.emplace("name", make_string_value(sprite.region_name));
        region_object.emplace(
            "x",
            make_number_value(
                static_cast<double>(sprite.packed_bounds.x + definition.padding)));
        region_object.emplace(
            "y",
            make_number_value(
                static_cast<double>(sprite.packed_bounds.y + definition.padding)));
        region_object.emplace("width", make_number_value(static_cast<double>(sprite.trim_width)));
        region_object.emplace("height", make_number_value(static_cast<double>(sprite.trim_height)));
        region_object.emplace("origin_x", make_number_value(sprite.origin_x));
        region_object.emplace("origin_y", make_number_value(sprite.origin_y));
        regions.push_back(make_object_value(std::move(region_object)));
    }
    root.emplace("regions", make_array_value(std::move(regions)));

    return marrow::runtime::json::serialize_pretty(make_object_value(std::move(root)));
}

} // namespace

std::optional<std::string> write_rgba_png(
    const std::filesystem::path& path,
    int width,
    int height,
    const std::vector<std::uint8_t>& rgba8) {
    std::vector<std::uint8_t> png_bytes;
    if (const auto error = encode_rgba_png(width, height, rgba8, &png_bytes)) {
        return error;
    }

    const std::filesystem::path parent_path = path.parent_path();
    if (!parent_path.empty()) {
        std::error_code directory_error;
        std::filesystem::create_directories(parent_path, directory_error);
        if (directory_error) {
            return "Failed to create " + parent_path.string() + ": " +
                directory_error.message();
        }
    }

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return "Failed to open " + path.string() + " for writing.";
    }

    output.write(
        reinterpret_cast<const char*>(png_bytes.data()),
        static_cast<std::streamsize>(png_bytes.size()));
    if (!output) {
        return "Failed to write PNG output " + path.string() + ".";
    }

    return std::nullopt;
}

PackedAtlasArtifactResult build_packed_atlas_artifact(
    const ProjectData& project,
    const AtlasPackDefinition& definition,
    const std::filesystem::path& exported_atlas_path) {
    PackedAtlasArtifactResult result;
    if (definition.padding < 0) {
        result.error_message = "Atlas pack padding must be zero or greater.";
        return result;
    }
    if (definition.bleed < 0) {
        result.error_message = "Atlas pack bleed must be zero or greater.";
        return result;
    }
    if (definition.bleed > definition.padding) {
        result.error_message =
            "Atlas pack bleed must not exceed the configured padding.";
        return result;
    }
    if (definition.sprites.empty()) {
        result.error_message = "Atlas pack definitions require at least one sprite image.";
        return result;
    }

    std::vector<PreparedSprite> sprites;
    sprites.reserve(definition.sprites.size());
    for (const AtlasPackSprite& sprite : definition.sprites) {
        std::string error_message;
        std::optional<PreparedSprite> prepared =
            prepare_sprite(project, definition, sprite, &error_message);
        if (!prepared.has_value()) {
            result.error_message = std::move(error_message);
            return result;
        }
        sprites.push_back(std::move(*prepared));
    }

    int atlas_width = 0;
    int atlas_height = 0;
    if (const auto error = build_best_layout(&sprites, &atlas_width, &atlas_height)) {
        result.error_message = *error;
        return result;
    }

    std::size_t atlas_pixel_count = 0;
    if (!checked_multiply(static_cast<std::size_t>(atlas_width), static_cast<std::size_t>(atlas_height),
                          &atlas_pixel_count) ||
        !checked_multiply(atlas_pixel_count, 4U, &atlas_pixel_count)) {
        result.error_message = "Packed atlas image size overflowed size_t.";
        return result;
    }

    ImageRgba atlas_image;
    atlas_image.width = atlas_width;
    atlas_image.height = atlas_height;
    atlas_image.rgba8.assign(atlas_pixel_count, 0U);
    for (const PreparedSprite& sprite : sprites) {
        blit_trimmed_sprite(sprite, definition, &atlas_image);
    }

    std::vector<std::uint8_t> png_bytes;
    if (const auto error = encode_rgba_png(
            atlas_width,
            atlas_height,
            atlas_image.rgba8,
            &png_bytes)) {
        result.error_message = *error;
        return result;
    }

    PackedAtlasArtifact artifact;
    artifact.atlas_path = exported_atlas_path.lexically_normal();
    artifact.image_path = exported_atlas_path;
    artifact.image_path.replace_extension(".png");
    artifact.atlas_text =
        build_atlas_document_text(definition, artifact.image_path, atlas_width, atlas_height, sprites);
    artifact.image_bytes = std::move(png_bytes);
    result.artifact = std::move(artifact);
    return result;
}

} // namespace marrow::editor::detail
