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

    /// @brief Constructs a null JSON value with a default source location.
    Value();
    /**
     * @brief Constructs a null JSON value at a specific source location.
     * @param location Source location associated with the value.
     */
    explicit Value(SourceLocation location);
    /**
     * @brief Constructs a null JSON value at a specific source location.
     * @param null_value Null sentinel used to select the null constructor.
     * @param location Source location associated with the value.
     */
    Value(std::nullptr_t null_value, SourceLocation location);
    /**
     * @brief Constructs a boolean JSON value.
     * @param boolean_value Boolean payload to store.
     * @param location Source location associated with the value.
     */
    Value(bool boolean_value, SourceLocation location);
    /**
     * @brief Constructs a numeric JSON value.
     * @param number_value Numeric payload to store.
     * @param location Source location associated with the value.
     */
    Value(double number_value, SourceLocation location);
    /**
     * @brief Constructs a string JSON value.
     * @param string_value String payload to store.
     * @param location Source location associated with the value.
     */
    Value(std::string string_value, SourceLocation location);
    /**
     * @brief Constructs an array JSON value.
     * @param array_value Array payload to store.
     * @param location Source location associated with the value.
     */
    Value(Array array_value, SourceLocation location);
    /**
     * @brief Constructs an object JSON value.
     * @param object_value Object payload to store.
     * @param location Source location associated with the value.
     */
    Value(Object object_value, SourceLocation location);

    /// @brief Returns the stored JSON type.
    /// @return The active JSON type tag.
    Type type() const;
    /// @brief Returns the parsed source location of the value.
    /// @return Source line, column, and offset data.
    const SourceLocation& location() const;

    /// @brief Reports whether the value stores JSON null.
    /// @return `true` when the active type is null; otherwise `false`.
    bool is_null() const;
    /// @brief Reports whether the value stores a JSON boolean.
    /// @return `true` when the active type is boolean; otherwise `false`.
    bool is_boolean() const;
    /// @brief Reports whether the value stores a JSON number.
    /// @return `true` when the active type is number; otherwise `false`.
    bool is_number() const;
    /// @brief Reports whether the value stores a JSON string.
    /// @return `true` when the active type is string; otherwise `false`.
    bool is_string() const;
    /// @brief Reports whether the value stores a JSON array.
    /// @return `true` when the active type is array; otherwise `false`.
    bool is_array() const;
    /// @brief Reports whether the value stores a JSON object.
    /// @return `true` when the active type is object; otherwise `false`.
    bool is_object() const;

    /// @brief Returns the stored boolean payload.
    /// @return The active boolean value.
    bool as_boolean() const;
    /// @brief Returns the stored numeric payload.
    /// @return The active numeric value.
    double as_number() const;
    /// @brief Returns the stored string payload.
    /// @return The active string value.
    const std::string& as_string() const;
    /// @brief Returns the stored string payload for mutation.
    /// @return A mutable reference to the active string value.
    std::string& as_string();
    /// @brief Returns the stored array payload.
    /// @return The active array value.
    const Array& as_array() const;
    /// @brief Returns the stored array payload for mutation.
    /// @return A mutable reference to the active array value.
    Array& as_array();
    /// @brief Returns the stored object payload.
    /// @return The active object value.
    const Object& as_object() const;
    /// @brief Returns the stored object payload for mutation.
    /// @return A mutable reference to the active object value.
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

    /// @brief Formats the load error as a human-readable diagnostic.
    /// @return A formatted error string with location excerpts when available.
    std::string format() const;
};

struct LoadResult {
    std::optional<Document> document;
    std::optional<LoadError> error;

    /// @brief Reports whether loading produced a document.
    /// @return `true` when a parsed document is present; otherwise `false`.
    explicit operator bool() const {
        return document.has_value();
    }
};

/**
 * @brief Parses a JSON document from text.
 * @param text JSON source text to parse.
 * @param source_path Optional source path used in diagnostics.
 * @return Parsed document or a load error.
 */
LoadResult parse_document(std::string_view text, std::filesystem::path source_path = {});
/**
 * @brief Loads and parses a JSON document from disk.
 * @param path Path to the JSON file.
 * @return Parsed document or a load error.
 */
LoadResult load_document(const std::filesystem::path& path);

/**
 * @brief Returns a human-readable name for a JSON value type.
 * @param type Type tag to describe.
 * @return The display name for the type.
 */
std::string_view type_name(Value::Type type);
/**
 * @brief Finds a member in an object without throwing.
 * @param object JSON object to search.
 * @param key Member name to look up.
 * @return Pointer to the member value, or `nullptr` when absent.
 */
const Value* find_member(const Value& object, std::string_view key);
/**
 * @brief Finds a mutable member in an object without throwing.
 * @param object JSON object to search.
 * @param key Member name to look up.
 * @return Pointer to the member value, or `nullptr` when absent.
 */
Value* find_member(Value& object, std::string_view key);
/**
 * @brief Serializes a JSON value with indentation.
 * @param value JSON value to serialize.
 * @param indent_size Number of spaces to use per indentation level.
 * @return Pretty-printed JSON text.
 */
std::string serialize_pretty(const Value& value, int indent_size = 2);

/**
 * @brief Serializes a JSON value compactly onto a single line.
 * @param value JSON value to serialize.
 * @return Compact JSON text.
 */
std::string serialize_compact(const Value& value);

/**
 * @brief Builds a validation error tied to a JSON path and source location.
 * @param document Source document used for excerpts and file paths.
 * @param location Location where validation failed.
 * @param json_path Logical JSON path of the failing value.
 * @param message Human-readable validation message.
 * @return A populated load error.
 */
LoadError make_validation_error(
    const Document& document,
    const SourceLocation& location,
    std::string json_path,
    std::string message);

/**
 * @brief Validates that a JSON value matches an expected type.
 * @param document Source document used for diagnostics.
 * @param value Value to validate.
 * @param expected Required JSON type.
 * @param json_path Logical JSON path of the value.
 * @return A validation error when the type is wrong, otherwise `std::nullopt`.
 */
std::optional<LoadError> require_type(
    const Document& document,
    const Value& value,
    Value::Type expected,
    std::string_view json_path);

/**
 * @brief Validates and retrieves a named member from an object.
 * @param document Source document used for diagnostics.
 * @param object Object that must contain the member.
 * @param key Required member name.
 * @param expected Required JSON type for the member.
 * @param json_path Logical JSON path of the object.
 * @param member_out Optional output pointer receiving the located member.
 * @return A validation error when the member is missing or typed incorrectly.
 */
std::optional<LoadError> require_member(
    const Document& document,
    const Value& object,
    std::string_view key,
    Value::Type expected,
    std::string_view json_path,
    const Value** member_out = nullptr);

} // namespace marrow::runtime::json
