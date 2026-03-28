#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "marrow/runtime/json.hpp"
#include "marrow/runtime/spine_import.hpp"

namespace {

enum class InputKind {
    SpineJson,
    SpineAtlas,
};

struct Options {
    std::filesystem::path input_path;
    std::filesystem::path output_path;
};

std::string lowercase_copy(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const char character : value) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lowered;
}

std::optional<InputKind> detect_input_kind(const std::filesystem::path& input_path) {
    const std::string extension = lowercase_copy(input_path.extension().string());
    if (extension == ".json") {
        return InputKind::SpineJson;
    }
    if (extension == ".atlas") {
        return InputKind::SpineAtlas;
    }
    return std::nullopt;
}

void print_usage(std::string_view executable_name) {
    std::cout << "Usage: " << executable_name << " <spine.json|spine.atlas> [out]\n"
              << "Convert a Spine 4.x JSON export into a Marrow .mskl runtime asset\n"
              << "or a Spine atlas export into one or more Marrow .matl atlas assets.\n";
}

bool parse_arguments(int argc, char** argv, Options* options_out) {
    if (argc < 2 || argc > 3) {
        print_usage(argv[0]);
        return false;
    }

    const std::string_view first_argument = argv[1];
    if (first_argument == "-h" || first_argument == "--help") {
        print_usage(argv[0]);
        return false;
    }

    Options options;
    options.input_path = std::filesystem::path(argv[1]);
    const std::optional<InputKind> input_kind = detect_input_kind(options.input_path);
    if (!input_kind.has_value()) {
        std::cerr << "Unsupported input path '" << options.input_path.string()
                  << "'. Expected a .json or .atlas file.\n";
        return false;
    }

    if (argc >= 3) {
        options.output_path = std::filesystem::path(argv[2]);
    } else {
        options.output_path = options.input_path;
        options.output_path.replace_extension(
            *input_kind == InputKind::SpineJson ? ".mskl" : ".matl");
    }
    *options_out = std::move(options);
    return true;
}

bool write_document(
    const std::filesystem::path& output_path,
    marrow::runtime::json::Document document) {
    document.source_path = output_path;

    const std::filesystem::path output_directory = output_path.parent_path();
    if (!output_directory.empty()) {
        std::error_code create_error;
        std::filesystem::create_directories(output_directory, create_error);
        if (create_error) {
            std::cerr << "Failed to create output directory '" << output_directory.string()
                      << "': " << create_error.message() << '\n';
            return false;
        }
    }

    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) {
        std::cerr << "Failed to open '" << output_path.string() << "' for writing.\n";
        return false;
    }

    output << marrow::runtime::json::serialize_pretty(document.root);
    if (!output.good()) {
        std::cerr << "Failed while writing '" << output_path.string() << "'.\n";
        return false;
    }

    std::cout << "Wrote " << output_path.string() << '\n';
    return true;
}

std::string sanitize_output_component(std::string_view value) {
    std::string sanitized;
    sanitized.reserve(value.size());
    for (const char character : value) {
        if (std::isalnum(static_cast<unsigned char>(character)) != 0 || character == '_' ||
            character == '-') {
            sanitized.push_back(character);
        } else {
            sanitized.push_back('_');
        }
    }
    return sanitized.empty() ? std::string("page") : sanitized;
}

std::string atlas_page_suffix(
    const marrow::runtime::json::Document& document,
    std::size_t page_index) {
    const marrow::runtime::json::Value* atlas =
        marrow::runtime::json::find_member(document.root, "atlas");
    if (atlas == nullptr || !atlas->is_object()) {
        return "page_" + std::to_string(page_index);
    }
    const marrow::runtime::json::Value* image =
        marrow::runtime::json::find_member(*atlas, "image");
    if (image == nullptr || !image->is_string() || image->as_string().empty()) {
        return "page_" + std::to_string(page_index);
    }

    const std::filesystem::path image_path(image->as_string());
    const std::string stem = image_path.stem().string();
    return sanitize_output_component(stem.empty() ? image->as_string() : stem);
}

std::filesystem::path atlas_output_path_for_page(
    std::filesystem::path base_output_path,
    const marrow::runtime::json::Document& document,
    std::size_t page_count,
    std::size_t page_index) {
    if (base_output_path.extension().empty()) {
        base_output_path.replace_extension(".matl");
    }
    if (page_count <= 1) {
        return base_output_path;
    }

    const std::string suffix = atlas_page_suffix(document, page_index);
    return base_output_path.parent_path() /
        (base_output_path.stem().string() + "_" + suffix + base_output_path.extension().string());
}

} // namespace

int main(int argc, char** argv) {
    Options options;
    if (!parse_arguments(argc, argv, &options)) {
        return argc >= 2 &&
                (std::string_view(argv[1]) == "-h" || std::string_view(argv[1]) == "--help")
            ? 0
            : 1;
    }

    const std::optional<InputKind> input_kind = detect_input_kind(options.input_path);
    if (!input_kind.has_value()) {
        std::cerr << "Unsupported input path '" << options.input_path.string()
                  << "'. Expected a .json or .atlas file.\n";
        return 1;
    }

    if (*input_kind == InputKind::SpineJson) {
        const auto import_result = marrow::runtime::import_spine_json_file(options.input_path);
        if (!import_result) {
            std::cerr << import_result.error->format() << '\n';
            return 1;
        }

        return write_document(options.output_path, *import_result.document) ? 0 : 1;
    }

    const auto import_result = marrow::runtime::import_spine_atlas_file(options.input_path);
    if (!import_result) {
        std::cerr << import_result.error->format() << '\n';
        return 1;
    }

    for (std::size_t page_index = 0; page_index < import_result.documents.size(); ++page_index) {
        const std::filesystem::path output_path = atlas_output_path_for_page(
            options.output_path,
            import_result.documents[page_index],
            import_result.documents.size(),
            page_index);
        if (!write_document(output_path, import_result.documents[page_index])) {
            return 1;
        }
    }
    return 0;
}
