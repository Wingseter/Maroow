#include "marrow/runtime/skeleton.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace marrow::runtime {
namespace {

constexpr char kBinaryMagic[] = {'M', 'B', 'I', 'N'};
constexpr std::uint64_t kBinaryVersion = 1;
constexpr std::size_t kMaxDecodeDepth = 1024;

enum class NodeTag : std::uint8_t {
    Null = 0,
    Boolean = 1,
    Number = 2,
    String = 3,
    Array = 4,
    Object = 5,
};

json::LoadResult make_failure(const std::filesystem::path& path, std::string message) {
    json::LoadResult result;
    result.error = json::LoadError{
        path,
        {},
        std::move(message),
        {},
        {},
    };
    return result;
}

std::optional<json::LoadError> make_write_failure(
    const std::filesystem::path& path,
    std::string message) {
    return json::LoadError{
        path,
        {},
        std::move(message),
        {},
        {},
    };
}

void append_varint(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    do {
        std::uint8_t byte = static_cast<std::uint8_t>(value & 0x7FU);
        value >>= 7U;
        if (value != 0) {
            byte |= 0x80U;
        }
        bytes.push_back(byte);
    } while (value != 0);
}

void append_float32(std::vector<std::uint8_t>& bytes, double value) {
    const float narrowed = static_cast<float>(value);
    std::uint32_t raw = 0;
    std::memcpy(&raw, &narrowed, sizeof(raw));
    bytes.push_back(static_cast<std::uint8_t>(raw & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((raw >> 8U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((raw >> 16U) & 0xFFU));
    bytes.push_back(static_cast<std::uint8_t>((raw >> 24U) & 0xFFU));
}

struct StringTableBuilder {
    std::vector<std::string> values;
    std::unordered_map<std::string, std::uint64_t> indices;

    std::uint64_t intern(std::string_view value) {
        const auto iterator = indices.find(std::string(value));
        if (iterator != indices.end()) {
            return iterator->second;
        }

        const std::uint64_t index = static_cast<std::uint64_t>(values.size());
        values.emplace_back(value);
        indices.emplace(values.back(), index);
        return index;
    }
};

void collect_strings(const json::Value& value, StringTableBuilder& table) {
    switch (value.type()) {
    case json::Value::Type::String:
        table.intern(value.as_string());
        return;
    case json::Value::Type::Array:
        for (const json::Value& element : value.as_array()) {
            collect_strings(element, table);
        }
        return;
    case json::Value::Type::Object:
        for (const auto& [key, member] : value.as_object()) {
            table.intern(key);
            collect_strings(member, table);
        }
        return;
    case json::Value::Type::Null:
    case json::Value::Type::Boolean:
    case json::Value::Type::Number:
        return;
    }
}

void collect_booleans(const json::Value& value, std::vector<std::uint8_t>& values) {
    switch (value.type()) {
    case json::Value::Type::Boolean:
        values.push_back(value.as_boolean() ? 1U : 0U);
        return;
    case json::Value::Type::Array:
        for (const json::Value& element : value.as_array()) {
            collect_booleans(element, values);
        }
        return;
    case json::Value::Type::Object:
        for (const auto& [key, member] : value.as_object()) {
            static_cast<void>(key);
            collect_booleans(member, values);
        }
        return;
    case json::Value::Type::Null:
    case json::Value::Type::Number:
    case json::Value::Type::String:
        return;
    }
}

std::vector<std::uint8_t> pack_booleans(const std::vector<std::uint8_t>& values) {
    std::vector<std::uint8_t> packed((values.size() + 7U) / 8U, 0U);
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (values[index] == 0U) {
            continue;
        }

        packed[index / 8U] |= static_cast<std::uint8_t>(1U << (index % 8U));
    }
    return packed;
}

void encode_value(
    const json::Value& value,
    const StringTableBuilder& strings,
    std::vector<std::uint8_t>& bytes) {
    switch (value.type()) {
    case json::Value::Type::Null:
        bytes.push_back(static_cast<std::uint8_t>(NodeTag::Null));
        return;
    case json::Value::Type::Boolean:
        bytes.push_back(static_cast<std::uint8_t>(NodeTag::Boolean));
        return;
    case json::Value::Type::Number:
        bytes.push_back(static_cast<std::uint8_t>(NodeTag::Number));
        append_float32(bytes, value.as_number());
        return;
    case json::Value::Type::String:
        bytes.push_back(static_cast<std::uint8_t>(NodeTag::String));
        append_varint(bytes, strings.indices.at(value.as_string()));
        return;
    case json::Value::Type::Array:
        bytes.push_back(static_cast<std::uint8_t>(NodeTag::Array));
        append_varint(bytes, static_cast<std::uint64_t>(value.as_array().size()));
        for (const json::Value& element : value.as_array()) {
            encode_value(element, strings, bytes);
        }
        return;
    case json::Value::Type::Object:
        bytes.push_back(static_cast<std::uint8_t>(NodeTag::Object));
        append_varint(bytes, static_cast<std::uint64_t>(value.as_object().size()));
        for (const auto& [key, member] : value.as_object()) {
            append_varint(bytes, strings.indices.at(key));
            encode_value(member, strings, bytes);
        }
        return;
    }
}

class BinaryDecoder {
public:
    BinaryDecoder(std::string_view bytes, std::filesystem::path source_path)
        : bytes_(bytes),
          source_path_(std::move(source_path)) {}

    json::LoadResult decode() {
        if (bytes_.size() < sizeof(kBinaryMagic) ||
            !std::equal(std::begin(kBinaryMagic), std::end(kBinaryMagic), bytes_.begin())) {
            return make_failure(source_path_, "invalid Marrow binary asset header");
        }
        offset_ = sizeof(kBinaryMagic);

        const std::optional<std::uint64_t> version = read_varint("binary asset version");
        if (!version.has_value()) {
            return finish_failure();
        }
        if (*version != kBinaryVersion) {
            return make_failure(source_path_, "unsupported Marrow binary asset version");
        }

        const std::optional<std::uint64_t> string_count = read_varint("string table count");
        if (!string_count.has_value()) {
            return finish_failure();
        }
        if (*string_count > bytes_.size()) {
            return make_failure(source_path_, "binary asset string table count is implausibly large");
        }
        string_table_.reserve(static_cast<std::size_t>(*string_count));
        for (std::uint64_t index = 0; index < *string_count; ++index) {
            const std::optional<std::uint64_t> string_size =
                read_varint("string table entry length");
            if (!string_size.has_value()) {
                return finish_failure();
            }
            if (!require_bytes(
                    static_cast<std::size_t>(*string_size),
                    "string table entry bytes")) {
                return finish_failure();
            }

            string_table_.emplace_back(
                bytes_.substr(offset_, static_cast<std::size_t>(*string_size)));
            offset_ += static_cast<std::size_t>(*string_size);
        }

        const std::optional<std::uint64_t> boolean_count = read_varint("boolean bitfield count");
        if (!boolean_count.has_value()) {
            return finish_failure();
        }
        if (*boolean_count > static_cast<std::uint64_t>(bytes_.size()) * 8U) {
            return make_failure(source_path_, "binary asset boolean bitfield count is implausibly large");
        }
        const std::size_t packed_boolean_bytes =
            static_cast<std::size_t>((*boolean_count + 7U) / 8U);
        if (!require_bytes(packed_boolean_bytes, "boolean bitfield bytes")) {
            return finish_failure();
        }
        booleans_.reserve(static_cast<std::size_t>(*boolean_count));
        for (std::uint64_t index = 0; index < *boolean_count; ++index) {
            const std::uint8_t packed = static_cast<std::uint8_t>(
                bytes_[offset_ + static_cast<std::size_t>(index / 8U)]);
            booleans_.push_back((packed >> (index % 8U)) & 0x01U);
        }
        offset_ += packed_boolean_bytes;

        json::Value root = decode_value(0);
        if (has_error()) {
            return finish_failure();
        }

        if (boolean_index_ != booleans_.size()) {
            return make_failure(source_path_, "binary asset left unused boolean payload data");
        }
        if (offset_ != bytes_.size()) {
            return make_failure(source_path_, "binary asset has trailing payload bytes");
        }

        json::Document document;
        document.source_path = source_path_;
        document.root = std::move(root);

        json::LoadResult result;
        result.document = std::move(document);
        return result;
    }

private:
    json::Value decode_value(std::size_t depth) {
        if (depth > kMaxDecodeDepth) {
            return fail("binary asset nesting exceeds the supported depth");
        }

        const json::SourceLocation location = current_location();
        const std::optional<std::uint8_t> raw_tag = read_byte("node tag");
        if (!raw_tag.has_value()) {
            return json::Value(location);
        }

        switch (static_cast<NodeTag>(*raw_tag)) {
        case NodeTag::Null:
            return json::Value(nullptr, location);
        case NodeTag::Boolean:
            if (boolean_index_ >= booleans_.size()) {
                return fail("binary asset boolean node exceeded the bitfield payload");
            }
            return json::Value(booleans_[boolean_index_++] != 0U, location);
        case NodeTag::Number: {
            const std::optional<float> number = read_float32("number payload");
            if (!number.has_value()) {
                return json::Value(location);
            }
            return json::Value(static_cast<double>(*number), location);
        }
        case NodeTag::String: {
            const std::optional<std::uint64_t> string_index =
                read_varint("string table index");
            if (!string_index.has_value()) {
                return json::Value(location);
            }
            if (*string_index >= string_table_.size()) {
                return fail("binary asset string table index was out of range");
            }
            return json::Value(
                string_table_[static_cast<std::size_t>(*string_index)],
                location);
        }
        case NodeTag::Array: {
            const std::optional<std::uint64_t> array_size = read_varint("array length");
            if (!array_size.has_value()) {
                return json::Value(location);
            }
            if (*array_size > bytes_.size() - offset_) {
                return fail("binary asset array length is implausibly large");
            }

            json::Value::Array array;
            array.reserve(static_cast<std::size_t>(*array_size));
            for (std::uint64_t index = 0; index < *array_size; ++index) {
                array.push_back(decode_value(depth + 1U));
                if (has_error()) {
                    return json::Value(location);
                }
            }
            return json::Value(std::move(array), location);
        }
        case NodeTag::Object: {
            const std::optional<std::uint64_t> member_count = read_varint("object member count");
            if (!member_count.has_value()) {
                return json::Value(location);
            }
            if (*member_count > bytes_.size() - offset_) {
                return fail("binary asset object member count is implausibly large");
            }

            json::Value::Object object;
            for (std::uint64_t index = 0; index < *member_count; ++index) {
                const std::optional<std::uint64_t> string_index =
                    read_varint("object member key index");
                if (!string_index.has_value()) {
                    return json::Value(location);
                }
                if (*string_index >= string_table_.size()) {
                    return fail("binary asset object key index was out of range");
                }

                const std::string& key =
                    string_table_[static_cast<std::size_t>(*string_index)];
                json::Value value = decode_value(depth + 1U);
                if (has_error()) {
                    return json::Value(location);
                }

                const auto [iterator, inserted] = object.emplace(key, std::move(value));
                if (!inserted) {
                    return fail("binary asset object contained a duplicate key");
                }
            }
            return json::Value(std::move(object), location);
        }
        }

        return fail("binary asset used an unknown node tag");
    }

    json::Value fail(std::string message) {
        if (!error_.has_value()) {
            error_ = json::LoadError{
                source_path_,
                current_location(),
                std::move(message),
                {},
                {},
            };
        }
        return json::Value(current_location());
    }

    bool require_bytes(std::size_t count, std::string_view context) {
        if (count <= bytes_.size() - offset_) {
            return true;
        }

        if (!error_.has_value()) {
            error_ = json::LoadError{
                source_path_,
                current_location(),
                "binary asset ended unexpectedly while reading " + std::string(context),
                {},
                {},
            };
        }
        return false;
    }

    std::optional<std::uint8_t> read_byte(std::string_view context) {
        if (!require_bytes(1, context)) {
            return std::nullopt;
        }

        return static_cast<std::uint8_t>(bytes_[offset_++]);
    }

    std::optional<std::uint64_t> read_varint(std::string_view context) {
        std::uint64_t value = 0;
        int shift = 0;
        for (int byte_index = 0; byte_index < 10; ++byte_index) {
            const std::optional<std::uint8_t> byte = read_byte(context);
            if (!byte.has_value()) {
                return std::nullopt;
            }

            value |= static_cast<std::uint64_t>(*byte & 0x7FU) << shift;
            if ((*byte & 0x80U) == 0U) {
                return value;
            }
            shift += 7;
        }

        if (!error_.has_value()) {
            error_ = json::LoadError{
                source_path_,
                current_location(),
                "binary asset varint exceeded the supported width while reading " +
                    std::string(context),
                {},
                {},
            };
        }
        return std::nullopt;
    }

    std::optional<float> read_float32(std::string_view context) {
        if (!require_bytes(sizeof(std::uint32_t), context)) {
            return std::nullopt;
        }

        std::uint32_t raw = 0;
        raw |= static_cast<std::uint32_t>(
            static_cast<std::uint8_t>(bytes_[offset_ + 0U]));
        raw |= static_cast<std::uint32_t>(
                   static_cast<std::uint8_t>(bytes_[offset_ + 1U]))
            << 8U;
        raw |= static_cast<std::uint32_t>(
                   static_cast<std::uint8_t>(bytes_[offset_ + 2U]))
            << 16U;
        raw |= static_cast<std::uint32_t>(
                   static_cast<std::uint8_t>(bytes_[offset_ + 3U]))
            << 24U;
        offset_ += sizeof(std::uint32_t);

        float value = 0.0F;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }

    json::LoadResult finish_failure() const {
        json::LoadResult result;
        result.error = error_;
        return result;
    }

    bool has_error() const {
        return error_.has_value();
    }

    json::SourceLocation current_location() const {
        return json::SourceLocation{1U, offset_ + 1U, offset_};
    }

    std::string_view bytes_;
    std::filesystem::path source_path_;
    std::size_t offset_{0};
    std::vector<std::string> string_table_;
    std::vector<std::uint8_t> booleans_;
    std::size_t boolean_index_{0};
    std::optional<json::LoadError> error_;
};

} // namespace

std::string_view skeleton_binary_extension() {
    return ".mbin";
}

json::LoadResult load_skeleton_document(const std::filesystem::path& path) {
    if (path.extension() != skeleton_binary_extension()) {
        return json::load_document(path);
    }

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return make_failure(path, "failed to open file");
    }

    std::string bytes(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    if (!input.good() && !input.eof()) {
        return make_failure(path, "failed while reading file");
    }

    BinaryDecoder decoder(bytes, path);
    return decoder.decode();
}

std::optional<json::LoadError> write_skeleton_binary_document(
    const json::Document& document,
    const std::filesystem::path& path) {
    StringTableBuilder strings;
    collect_strings(document.root, strings);

    std::vector<std::uint8_t> booleans;
    collect_booleans(document.root, booleans);

    std::vector<std::uint8_t> bytes;
    bytes.reserve(1024);
    bytes.insert(bytes.end(), std::begin(kBinaryMagic), std::end(kBinaryMagic));
    append_varint(bytes, kBinaryVersion);
    append_varint(bytes, static_cast<std::uint64_t>(strings.values.size()));
    for (const std::string& value : strings.values) {
        append_varint(bytes, static_cast<std::uint64_t>(value.size()));
        bytes.insert(bytes.end(), value.begin(), value.end());
    }

    append_varint(bytes, static_cast<std::uint64_t>(booleans.size()));
    const std::vector<std::uint8_t> packed_booleans = pack_booleans(booleans);
    bytes.insert(bytes.end(), packed_booleans.begin(), packed_booleans.end());
    encode_value(document.root, strings, bytes);

    std::ofstream output(path, std::ios::binary);
    if (!output) {
        return make_write_failure(path, "failed to open file");
    }

    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    if (!output.good()) {
        return make_write_failure(path, "failed while writing file");
    }

    return std::nullopt;
}

} // namespace marrow::runtime
