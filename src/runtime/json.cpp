#include "marrow/runtime/json.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace marrow::runtime::json {

namespace {

struct LineContext {
    std::string line_excerpt;
    std::string caret_excerpt;
};

LineContext build_line_context(std::string_view text, const SourceLocation& location) {
    if (text.empty()) {
        return {};
    }

    const std::size_t safe_offset = std::min(location.offset, text.size());
    std::size_t line_start = safe_offset;
    while (line_start > 0 && text[line_start - 1] != '\n') {
        --line_start;
    }

    std::size_t line_end = safe_offset;
    while (line_end < text.size() && text[line_end] != '\n') {
        ++line_end;
    }

    std::string line{text.substr(line_start, line_end - line_start)};
    std::size_t caret_column = location.column > 0 ? location.column - 1 : 0;
    if (caret_column > line.size()) {
        caret_column = line.size();
    }

    return {
        std::move(line),
        std::string(caret_column, ' ') + '^',
    };
}

class Parser {
public:
    Parser(std::string_view text, std::filesystem::path source_path)
        : text_(text),
          source_path_(std::move(source_path)) {}

    LoadResult parse() {
        skip_whitespace();
        if (eof()) {
            return failure("expected a JSON value");
        }

        Value root = parse_value();
        if (has_error()) {
            return finish_failure();
        }

        skip_whitespace();
        if (!eof()) {
            return failure("unexpected trailing characters");
        }

        Document document;
        document.source_path = source_path_;
        document.source_text.assign(text_.begin(), text_.end());
        document.root = std::move(root);

        LoadResult result;
        result.document = std::move(document);
        return result;
    }

private:
    Value parse_value() {
        skip_whitespace();
        const SourceLocation start = current_location();
        if (eof()) {
            return fail("unexpected end of input");
        }

        switch (peek()) {
        case '{':
            return parse_object();
        case '[':
            return parse_array();
        case '"':
            return parse_string();
        case 't':
            return parse_true();
        case 'f':
            return parse_false();
        case 'n':
            return parse_null();
        default:
            if (peek() == '-' || std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                return parse_number();
            }
            return fail("unexpected token while parsing value", start);
        }
    }

    Value parse_object() {
        const SourceLocation start = current_location();
        advance();

        Value::Object object;
        skip_whitespace();
        if (consume_if('}')) {
            return Value(std::move(object), start);
        }

        while (!eof()) {
            skip_whitespace();
            if (peek() != '"') {
                return fail("expected an object key string");
            }

            Value key_value = parse_string();
            if (has_error()) {
                return Value(start);
            }

            const std::string key = key_value.as_string();
            skip_whitespace();
            if (!consume_if(':')) {
                return fail("expected ':' after object key");
            }

            skip_whitespace();
            Value value = parse_value();
            if (has_error()) {
                return Value(start);
            }

            const auto [iterator, inserted] = object.emplace(key, std::move(value));
            if (!inserted) {
                return fail("duplicate object key '" + key + "'", key_value.location());
            }

            skip_whitespace();
            if (consume_if('}')) {
                return Value(std::move(object), start);
            }

            if (!consume_if(',')) {
                return fail("expected ',' or '}' after object member");
            }
        }

        return fail("unterminated object", start);
    }

    Value parse_array() {
        const SourceLocation start = current_location();
        advance();

        Value::Array array;
        skip_whitespace();
        if (consume_if(']')) {
            return Value(std::move(array), start);
        }

        while (!eof()) {
            skip_whitespace();
            array.push_back(parse_value());
            if (has_error()) {
                return Value(start);
            }

            skip_whitespace();
            if (consume_if(']')) {
                return Value(std::move(array), start);
            }

            if (!consume_if(',')) {
                return fail("expected ',' or ']' after array element");
            }
        }

        return fail("unterminated array", start);
    }

    Value parse_string() {
        const SourceLocation start = current_location();
        if (!consume_if('"')) {
            return fail("expected string");
        }

        std::string result;
        while (!eof()) {
            const char current = advance();
            if (current == '"') {
                return Value(std::move(result), start);
            }

            if (current == '\\') {
                if (eof()) {
                    return fail("unterminated escape sequence", start);
                }

                const char escape = advance();
                switch (escape) {
                case '"':
                case '\\':
                case '/':
                    result.push_back(escape);
                    break;
                case 'b':
                    result.push_back('\b');
                    break;
                case 'f':
                    result.push_back('\f');
                    break;
                case 'n':
                    result.push_back('\n');
                    break;
                case 'r':
                    result.push_back('\r');
                    break;
                case 't':
                    result.push_back('\t');
                    break;
                case 'u': {
                    const auto code_point = parse_unicode_escape();
                    if (!code_point.has_value()) {
                        return Value(start);
                    }
                    append_utf8(*code_point, result);
                    break;
                }
                default:
                    return fail("unsupported escape sequence");
                }
                continue;
            }

            if (static_cast<unsigned char>(current) < 0x20U) {
                return fail("control characters must be escaped inside strings");
            }

            result.push_back(current);
        }

        return fail("unterminated string", start);
    }

    Value parse_number() {
        const SourceLocation start = current_location();
        const std::size_t begin = index_;

        if (consume_if('-') && eof()) {
            return fail("expected digits after minus sign", start);
        }

        if (consume_if('0')) {
            if (!eof() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                return fail("leading zeroes are not allowed in JSON numbers");
            }
        } else {
            if (eof() || std::isdigit(static_cast<unsigned char>(peek())) == 0) {
                return fail("expected digits in number", start);
            }
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                advance();
            }
        }

        if (consume_if('.')) {
            if (eof() || std::isdigit(static_cast<unsigned char>(peek())) == 0) {
                return fail("expected digits after decimal point", start);
            }
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                advance();
            }
        }

        if (!eof() && (peek() == 'e' || peek() == 'E')) {
            advance();
            if (!eof() && (peek() == '+' || peek() == '-')) {
                advance();
            }
            if (eof() || std::isdigit(static_cast<unsigned char>(peek())) == 0) {
                return fail("expected digits in exponent", start);
            }
            while (!eof() && std::isdigit(static_cast<unsigned char>(peek())) != 0) {
                advance();
            }
        }

        std::string number_text{text_.substr(begin, index_ - begin)};
        char* end = nullptr;
        errno = 0;
        const double value = std::strtod(number_text.c_str(), &end);
        if (errno == ERANGE || end == number_text.c_str() || *end != '\0') {
            return fail("invalid numeric value", start);
        }
        return Value(value, start);
    }

    Value parse_true() {
        const SourceLocation start = current_location();
        if (!consume_literal("true")) {
            return fail("expected 'true'", start);
        }
        return Value(true, start);
    }

    Value parse_false() {
        const SourceLocation start = current_location();
        if (!consume_literal("false")) {
            return fail("expected 'false'", start);
        }
        return Value(false, start);
    }

    Value parse_null() {
        const SourceLocation start = current_location();
        if (!consume_literal("null")) {
            return fail("expected 'null'", start);
        }
        return Value(nullptr, start);
    }

    std::optional<unsigned int> parse_unicode_escape() {
        unsigned int code_unit = 0;
        for (int i = 0; i < 4; ++i) {
            if (eof()) {
                fail("incomplete unicode escape");
                return std::nullopt;
            }

            const char digit = advance();
            code_unit <<= 4U;
            if (digit >= '0' && digit <= '9') {
                code_unit |= static_cast<unsigned int>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
                code_unit |= static_cast<unsigned int>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
                code_unit |= static_cast<unsigned int>(digit - 'A' + 10);
            } else {
                fail("invalid unicode escape sequence");
                return std::nullopt;
            }
        }

        if (code_unit < 0xD800U || code_unit > 0xDFFFU) {
            return code_unit;
        }

        if (code_unit > 0xDBFFU) {
            fail("unexpected low surrogate without preceding high surrogate");
            return std::nullopt;
        }

        if (!consume_if('\\') || !consume_if('u')) {
            fail("expected low surrogate pair after high surrogate");
            return std::nullopt;
        }

        unsigned int low_surrogate = 0;
        for (int i = 0; i < 4; ++i) {
            if (eof()) {
                fail("incomplete low surrogate unicode escape");
                return std::nullopt;
            }

            const char digit = advance();
            low_surrogate <<= 4U;
            if (digit >= '0' && digit <= '9') {
                low_surrogate |= static_cast<unsigned int>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
                low_surrogate |= static_cast<unsigned int>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
                low_surrogate |= static_cast<unsigned int>(digit - 'A' + 10);
            } else {
                fail("invalid unicode escape sequence");
                return std::nullopt;
            }
        }

        if (low_surrogate < 0xDC00U || low_surrogate > 0xDFFFU) {
            fail("invalid low surrogate in unicode escape");
            return std::nullopt;
        }

        return 0x10000U + ((code_unit - 0xD800U) << 10U) + (low_surrogate - 0xDC00U);
    }

    void append_utf8(unsigned int code_point, std::string& out) {
        if (code_point <= 0x7FU) {
            out.push_back(static_cast<char>(code_point));
            return;
        }
        if (code_point <= 0x7FFU) {
            out.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
            out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
            return;
        }
        if (code_point <= 0xFFFFU) {
            out.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
            out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
            out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
            return;
        }

        out.push_back(static_cast<char>(0xF0U | (code_point >> 18U)));
        out.push_back(static_cast<char>(0x80U | ((code_point >> 12U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
        out.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
    }

    LoadResult failure(std::string message) {
        fail(std::move(message));
        return finish_failure();
    }

    Value fail(std::string message, SourceLocation location) {
        if (!error_.has_value()) {
            auto context = build_line_context(text_, location);
            error_ = LoadError{
                source_path_,
                location,
                std::move(message),
                std::move(context.line_excerpt),
                std::move(context.caret_excerpt),
            };
        }
        return Value(location);
    }

    Value fail(std::string message) {
        return fail(std::move(message), current_location());
    }

    LoadResult finish_failure() {
        LoadResult result;
        result.error = error_;
        return result;
    }

    bool has_error() const {
        return error_.has_value();
    }

    void skip_whitespace() {
        while (!eof()) {
            const char current = peek();
            if (current != ' ' && current != '\n' && current != '\r' && current != '\t') {
                return;
            }
            advance();
        }
    }

    bool consume_if(char expected) {
        if (!eof() && peek() == expected) {
            advance();
            return true;
        }
        return false;
    }

    bool consume_literal(std::string_view literal) {
        for (const char expected : literal) {
            if (eof() || peek() != expected) {
                return false;
            }
            advance();
        }
        return true;
    }

    char peek() const {
        return text_[index_];
    }

    char advance() {
        const char current = text_[index_];
        ++index_;

        if (current == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }

        return current;
    }

    bool eof() const {
        return index_ >= text_.size();
    }

    SourceLocation current_location() const {
        return SourceLocation{line_, column_, index_};
    }

    std::string_view text_;
    std::filesystem::path source_path_;
    std::size_t index_{0};
    std::size_t line_{1};
    std::size_t column_{1};
    std::optional<LoadError> error_;
};

std::string source_label(const std::filesystem::path& path) {
    return path.empty() ? "<memory>" : path.string();
}

std::string escape_json_string(std::string_view text) {
    std::ostringstream escaped;
    for (const unsigned char character : text) {
        switch (character) {
        case '\"':
            escaped << "\\\"";
            break;
        case '\\':
            escaped << "\\\\";
            break;
        case '\b':
            escaped << "\\b";
            break;
        case '\f':
            escaped << "\\f";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            if (character < 0x20U) {
                escaped << "\\u00";
                constexpr char kHexDigits[] = "0123456789abcdef";
                escaped << kHexDigits[(character >> 4U) & 0x0FU];
                escaped << kHexDigits[character & 0x0FU];
            } else {
                escaped << static_cast<char>(character);
            }
            break;
        }
    }

    return escaped.str();
}

void serialize_value(
    const Value& value,
    int indent_size,
    int indent_level,
    std::ostringstream* output) {
    switch (value.type()) {
    case Value::Type::Null:
        *output << "null";
        return;
    case Value::Type::Boolean:
        *output << (value.as_boolean() ? "true" : "false");
        return;
    case Value::Type::Number: {
        std::ostringstream number_stream;
        number_stream << std::setprecision(15) << value.as_number();
        *output << number_stream.str();
        return;
    }
    case Value::Type::String:
        *output << '\"' << escape_json_string(value.as_string()) << '\"';
        return;
    case Value::Type::Array: {
        const Value::Array& array = value.as_array();
        if (array.empty()) {
            *output << "[]";
            return;
        }

        *output << "[\n";
        for (std::size_t index = 0; index < array.size(); ++index) {
            *output << std::string((indent_level + 1) * indent_size, ' ');
            serialize_value(array[index], indent_size, indent_level + 1, output);
            if (index + 1 < array.size()) {
                *output << ',';
            }
            *output << '\n';
        }
        *output << std::string(indent_level * indent_size, ' ') << ']';
        return;
    }
    case Value::Type::Object: {
        const Value::Object& object = value.as_object();
        if (object.empty()) {
            *output << "{}";
            return;
        }

        *output << "{\n";
        std::size_t index = 0;
        for (const auto& [key, member_value] : object) {
            *output << std::string((indent_level + 1) * indent_size, ' ')
                    << '\"' << escape_json_string(key) << "\": ";
            serialize_value(member_value, indent_size, indent_level + 1, output);
            if (index + 1 < object.size()) {
                *output << ',';
            }
            *output << '\n';
            ++index;
        }
        *output << std::string(indent_level * indent_size, ' ') << '}';
        return;
    }
    }
}

} // namespace

Value::Value()
    : Value(nullptr, SourceLocation{}) {}

Value::Value(SourceLocation location)
    : Value(nullptr, location) {}

Value::Value(std::nullptr_t, SourceLocation location)
    : location_(location),
      storage_(nullptr) {}

Value::Value(bool boolean_value, SourceLocation location)
    : location_(location),
      storage_(boolean_value) {}

Value::Value(double number_value, SourceLocation location)
    : location_(location),
      storage_(number_value) {}

Value::Value(std::string string_value, SourceLocation location)
    : location_(location),
      storage_(std::move(string_value)) {}

Value::Value(Array array_value, SourceLocation location)
    : location_(location),
      storage_(std::move(array_value)) {}

Value::Value(Object object_value, SourceLocation location)
    : location_(location),
      storage_(std::move(object_value)) {}

Value::Type Value::type() const {
    if (std::holds_alternative<std::nullptr_t>(storage_)) {
        return Type::Null;
    }
    if (std::holds_alternative<bool>(storage_)) {
        return Type::Boolean;
    }
    if (std::holds_alternative<double>(storage_)) {
        return Type::Number;
    }
    if (std::holds_alternative<std::string>(storage_)) {
        return Type::String;
    }
    if (std::holds_alternative<Array>(storage_)) {
        return Type::Array;
    }
    return Type::Object;
}

const SourceLocation& Value::location() const {
    return location_;
}

bool Value::is_null() const {
    return std::holds_alternative<std::nullptr_t>(storage_);
}

bool Value::is_boolean() const {
    return std::holds_alternative<bool>(storage_);
}

bool Value::is_number() const {
    return std::holds_alternative<double>(storage_);
}

bool Value::is_string() const {
    return std::holds_alternative<std::string>(storage_);
}

bool Value::is_array() const {
    return std::holds_alternative<Array>(storage_);
}

bool Value::is_object() const {
    return std::holds_alternative<Object>(storage_);
}

bool Value::as_boolean() const {
    return std::get<bool>(storage_);
}

double Value::as_number() const {
    return std::get<double>(storage_);
}

const std::string& Value::as_string() const {
    return std::get<std::string>(storage_);
}

std::string& Value::as_string() {
    return std::get<std::string>(storage_);
}

const Value::Array& Value::as_array() const {
    return std::get<Array>(storage_);
}

Value::Array& Value::as_array() {
    return std::get<Array>(storage_);
}

const Value::Object& Value::as_object() const {
    return std::get<Object>(storage_);
}

Value::Object& Value::as_object() {
    return std::get<Object>(storage_);
}

std::string LoadError::format() const {
    std::ostringstream stream;
    stream << source_label(source_path);

    if (location.line > 0) {
        stream << ':' << location.line << ':' << location.column;
    }

    stream << ": " << message;

    if (!line_excerpt.empty()) {
        stream << '\n' << line_excerpt;
        if (!caret_excerpt.empty()) {
            stream << '\n' << caret_excerpt;
        }
    }

    return stream.str();
}

LoadResult parse_document(std::string_view text, std::filesystem::path source_path) {
    Parser parser(text, std::move(source_path));
    return parser.parse();
}

LoadResult load_document(const std::filesystem::path& path) {
    std::ifstream input(path);
    if (!input) {
        LoadResult result;
        result.error = LoadError{
            path,
            {},
            "failed to open file",
            {},
            {},
        };
        return result;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input.good() && !input.eof()) {
        LoadResult result;
        result.error = LoadError{
            path,
            {},
            "failed while reading file",
            {},
            {},
        };
        return result;
    }

    return parse_document(buffer.str(), path);
}

std::string_view type_name(Value::Type type) {
    switch (type) {
    case Value::Type::Null:
        return "null";
    case Value::Type::Boolean:
        return "boolean";
    case Value::Type::Number:
        return "number";
    case Value::Type::String:
        return "string";
    case Value::Type::Array:
        return "array";
    case Value::Type::Object:
        return "object";
    }
    return "unknown";
}

const Value* find_member(const Value& object, std::string_view key) {
    if (!object.is_object()) {
        return nullptr;
    }

    const auto iterator = object.as_object().find(key);
    if (iterator == object.as_object().end()) {
        return nullptr;
    }

    return &iterator->second;
}

Value* find_member(Value& object, std::string_view key) {
    if (!object.is_object()) {
        return nullptr;
    }

    auto iterator = object.as_object().find(key);
    if (iterator == object.as_object().end()) {
        return nullptr;
    }

    return &iterator->second;
}

std::string serialize_pretty(const Value& value, int indent_size) {
    std::ostringstream output;
    serialize_value(value, std::max(indent_size, 0), 0, &output);
    output << '\n';
    return output.str();
}

LoadError make_validation_error(
    const Document& document,
    const SourceLocation& location,
    std::string json_path,
    std::string message) {
    auto context = build_line_context(document.source_text, location);
    LoadError error;
    error.source_path = document.source_path;
    error.location = location;
    error.message = std::move(json_path) + ": " + std::move(message);
    error.line_excerpt = std::move(context.line_excerpt);
    error.caret_excerpt = std::move(context.caret_excerpt);
    return error;
}

std::optional<LoadError> require_type(
    const Document& document,
    const Value& value,
    Value::Type expected,
    std::string_view json_path) {
    if (value.type() == expected) {
        return std::nullopt;
    }

    return make_validation_error(
        document,
        value.location(),
        std::string(json_path),
        "expected " + std::string(type_name(expected)) + " but found " +
            std::string(type_name(value.type())));
}

std::optional<LoadError> require_member(
    const Document& document,
    const Value& object,
    std::string_view key,
    Value::Type expected,
    std::string_view json_path,
    const Value** member_out) {
    if (const auto error = require_type(document, object, Value::Type::Object, json_path)) {
        return error;
    }

    const auto iterator = object.as_object().find(key);
    const std::string member_path = std::string(json_path) + "." + std::string(key);
    if (iterator == object.as_object().end()) {
        return make_validation_error(
            document,
            object.location(),
            member_path,
            "missing required member");
    }

    if (const auto error = require_type(document, iterator->second, expected, member_path)) {
        return error;
    }

    if (member_out != nullptr) {
        *member_out = &iterator->second;
    }

    return std::nullopt;
}

} // namespace marrow::runtime::json
