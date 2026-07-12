#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

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
    std::optional<std::filesystem::path> report_path;
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
    std::cout << "Usage: " << executable_name
              << " [--report report.json] <spine.json|spine.atlas> [out]\n"
              << "Convert a Spine 4.x JSON export into a Marrow .mskl runtime asset\n"
              << "or a Spine atlas export into one or more Marrow .matl atlas assets.\n"
              << "--report writes a machine-readable compatibility report for Spine JSON imports.\n";
}

bool parse_arguments(int argc, char** argv, Options* options_out) {
    if (argc < 2) {
        print_usage(argv[0]);
        return false;
    }

    Options options;
    std::vector<std::string_view> positional_arguments;
    positional_arguments.reserve(2);
    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "-h" || argument == "--help") {
            print_usage(argv[0]);
            return false;
        }
        if (argument == "--report") {
            if (index + 1 >= argc) {
                std::cerr << "--report requires an output JSON path.\n";
                return false;
            }
            options.report_path = std::filesystem::path(argv[++index]);
            continue;
        }
        positional_arguments.push_back(argument);
    }

    if (positional_arguments.empty() || positional_arguments.size() > 2U) {
        print_usage(argv[0]);
        return false;
    }

    options.input_path = std::filesystem::path(positional_arguments[0]);
    const std::optional<InputKind> input_kind = detect_input_kind(options.input_path);
    if (!input_kind.has_value()) {
        std::cerr << "Unsupported input path '" << options.input_path.string()
                  << "'. Expected a .json or .atlas file.\n";
        return false;
    }
    if (options.report_path.has_value() && *input_kind != InputKind::SpineJson) {
        std::cerr << "--report is only supported for Spine JSON imports.\n";
        return false;
    }

    if (positional_arguments.size() >= 2U) {
        options.output_path = std::filesystem::path(positional_arguments[1]);
    } else {
        options.output_path = options.input_path;
        options.output_path.replace_extension(
            *input_kind == InputKind::SpineJson ? ".mskl" : ".matl");
    }
    *options_out = std::move(options);
    return true;
}

bool write_json_value(
    const std::filesystem::path& output_path,
    const marrow::runtime::json::Value& value) {
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

    output << marrow::runtime::json::serialize_pretty(value);
    if (!output.good()) {
        std::cerr << "Failed while writing '" << output_path.string() << "'.\n";
        return false;
    }

    std::cout << "Wrote " << output_path.string() << '\n';
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

std::optional<std::string> atlas_page_image_name(
    const marrow::runtime::json::Document& document) {
    const marrow::runtime::json::Value* atlas =
        marrow::runtime::json::find_member(document.root, "atlas");
    if (atlas == nullptr || !atlas->is_object()) {
        return std::nullopt;
    }
    const marrow::runtime::json::Value* image =
        marrow::runtime::json::find_member(*atlas, "image");
    if (image == nullptr || !image->is_string() || image->as_string().empty()) {
        return std::nullopt;
    }
    return image->as_string();
}

bool copy_atlas_page_image(
    const std::filesystem::path& source_atlas_path,
    const std::filesystem::path& output_path,
    const marrow::runtime::json::Document& document) {
    const std::optional<std::string> image_name = atlas_page_image_name(document);
    if (!image_name.has_value()) {
        return true;
    }

    const std::filesystem::path declared_image_path(*image_name);
    if (declared_image_path.is_absolute()) {
        return true;
    }

    const std::filesystem::path source_image_path =
        (source_atlas_path.parent_path() / declared_image_path).lexically_normal();
    std::error_code exists_error;
    if (!std::filesystem::exists(source_image_path, exists_error)) {
        return true;
    }

    const std::filesystem::path target_image_path =
        (output_path.parent_path() / declared_image_path).lexically_normal();
    std::error_code equivalent_error;
    if (std::filesystem::equivalent(source_image_path, target_image_path, equivalent_error)) {
        return true;
    }

    const std::filesystem::path target_directory = target_image_path.parent_path();
    if (!target_directory.empty()) {
        std::error_code create_error;
        std::filesystem::create_directories(target_directory, create_error);
        if (create_error) {
            std::cerr << "Failed to create atlas image output directory '"
                      << target_directory.string() << "': " << create_error.message() << '\n';
            return false;
        }
    }

    std::error_code copy_error;
    std::filesystem::copy_file(
        source_image_path,
        target_image_path,
        std::filesystem::copy_options::overwrite_existing,
        copy_error);
    if (copy_error) {
        std::cerr << "Failed to copy atlas image '" << source_image_path.string()
                  << "' to '" << target_image_path.string() << "': "
                  << copy_error.message() << '\n';
        return false;
    }

    std::cout << "Copied " << target_image_path.string() << '\n';
    return true;
}

void print_warnings(const std::vector<std::string>& warnings) {
    for (const std::string& warning : warnings) {
        std::cerr << "Warning: " << warning << '\n';
    }
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
        print_warnings(import_result.warnings);
        if (options.report_path.has_value() &&
            !write_json_value(
                *options.report_path,
                marrow::runtime::spine_import_report_to_json(import_result.report))) {
            return 1;
        }
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
        if (!copy_atlas_page_image(
                options.input_path,
                output_path,
                import_result.documents[page_index])) {
            return 1;
        }
    }
    return 0;
}
