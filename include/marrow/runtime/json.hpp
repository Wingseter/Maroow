#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace marrow::runtime::json {

struct SourceLocation {
    std::size_t line{1};
    std::size_t column{1};
    std::size_t offset{0};
};

class Value {
public:
    using Array = std::vector<Value>;
    using Object = std::map<std::string, Value, std::less<>>;

    enum class Type {
        Null,
        Boolean,
        Number,
        String,
        Array,
        Object,
    };

    Value();
    explicit Value(SourceLocation location);
    Value(std::nullptr_t, SourceLocation location);
    Value(bool boolean_value, SourceLocation location);
    Value(double number_value, SourceLocation location);
    Value(std::string string_value, SourceLocation location);
    Value(Array array_value, SourceLocation location);
    Value(Object object_value, SourceLocation location);

    Type type() const;
    const SourceLocation& location() const;

    bool is_null() const;
    bool is_boolean() const;
    bool is_number() const;
    bool is_string() const;
    bool is_array() const;
    bool is_object() const;

    bool as_boolean() const;
    double as_number() const;
    const std::string& as_string() const;
    std::string& as_string();
    const Array& as_array() const;
    Array& as_array();
    const Object& as_object() const;
    Object& as_object();

private:
    SourceLocation location_{};
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> storage_;
};

struct Document {
    std::filesystem::path source_path;
    std::string source_text;
    Value root;
};

struct LoadError {
    std::filesystem::path source_path;
    SourceLocation location{};
    std::string message;
    std::string line_excerpt;
    std::string caret_excerpt;

    std::string format() const;
};

struct LoadResult {
    std::optional<Document> document;
    std::optional<LoadError> error;

    explicit operator bool() const {
        return document.has_value();
    }
};

LoadResult parse_document(std::string_view text, std::filesystem::path source_path = {});
LoadResult load_document(const std::filesystem::path& path);

std::string_view type_name(Value::Type type);
const Value* find_member(const Value& object, std::string_view key);
Value* find_member(Value& object, std::string_view key);
std::string serialize_pretty(const Value& value, int indent_size = 2);

LoadError make_validation_error(
    const Document& document,
    const SourceLocation& location,
    std::string json_path,
    std::string message);

std::optional<LoadError> require_type(
    const Document& document,
    const Value& value,
    Value::Type expected,
    std::string_view json_path);

std::optional<LoadError> require_member(
    const Document& document,
    const Value& object,
    std::string_view key,
    Value::Type expected,
    std::string_view json_path,
    const Value** member_out = nullptr);

} // namespace marrow::runtime::json
