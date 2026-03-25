#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "marrow/runtime/skeleton.hpp"

namespace {

using marrow::runtime::json::Value;

struct InspectOptions {
    std::filesystem::path skeleton_path{"assets/fixtures/player_idle.mskl"};
    std::optional<std::filesystem::path> compare_path;
    std::optional<std::filesystem::path> export_binary_path;
    std::vector<std::string> requested_skins;
};

enum class ParseStatus {
    Ok,
    Help,
    Error,
};

struct ParseResult {
    ParseStatus status{ParseStatus::Error};
    InspectOptions options;
};

void print_usage(std::string_view executable_name) {
    std::cout << "Usage: " << executable_name
              << " [--skin <name> ...] [--export-binary <out.mbin>]"
              << " [--compare <other.mskl|other.mbin>] [skeleton.mskl|skeleton.mbin]\n"
              << "Load a Marrow skeleton, optionally write a compact `.mbin`,"
              << " and compare JSON vs binary assets.\n";
}

void append_unique(std::vector<std::string>& values, std::string value) {
    if (std::find(values.begin(), values.end(), value) == values.end()) {
        values.push_back(std::move(value));
    }
}

std::string join_or_none(const std::vector<std::string>& values) {
    if (values.empty()) {
        return "<none>";
    }

    std::string joined;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            joined += ", ";
        }
        joined += values[index];
    }
    return joined;
}

ParseResult parse_arguments(int argc, char** argv) {
    ParseResult result;
    bool skeleton_path_set = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "-h" || argument == "--help") {
            print_usage(argv[0]);
            result.status = ParseStatus::Help;
            return result;
        }

        if (argument == "--skin") {
            if (index + 1 >= argc) {
                std::cerr << "--skin requires a skin name.\n";
                print_usage(argv[0]);
                return result;
            }

            result.options.requested_skins.emplace_back(argv[++index]);
            continue;
        }

        if (argument == "--compare") {
            if (index + 1 >= argc) {
                std::cerr << "--compare requires a second asset path.\n";
                print_usage(argv[0]);
                return result;
            }

            result.options.compare_path = std::filesystem::path(argv[++index]);
            continue;
        }

        if (argument == "--export-binary") {
            if (index + 1 >= argc) {
                std::cerr << "--export-binary requires an output path.\n";
                print_usage(argv[0]);
                return result;
            }

            result.options.export_binary_path = std::filesystem::path(argv[++index]);
            continue;
        }

        if (!argument.empty() && argument.front() == '-') {
            std::cerr << "Unknown option: " << argument << '\n';
            print_usage(argv[0]);
            return result;
        }

        if (skeleton_path_set) {
            std::cerr << "Only one skeleton path may be provided.\n";
            print_usage(argv[0]);
            return result;
        }

        result.options.skeleton_path = std::filesystem::path(argument);
        skeleton_path_set = true;
    }

    result.status = ParseStatus::Ok;
    return result;
}

std::vector<std::string> collect_names(
    const std::vector<marrow::runtime::AnimationData>& animations) {
    std::vector<std::string> names;
    names.reserve(animations.size());
    for (const auto& animation : animations) {
        names.push_back(animation.name);
    }
    return names;
}

std::vector<std::string> collect_names(const std::vector<marrow::runtime::SkinData>& skins) {
    std::vector<std::string> names;
    names.reserve(skins.size());
    for (const auto& skin : skins) {
        names.push_back(skin.name);
    }
    return names;
}

std::vector<std::string> resolve_active_skin_names(
    const marrow::runtime::SkeletonData& skeleton_data,
    const std::vector<std::string>& requested_skins) {
    std::vector<std::string> active_skin_names;
    if (const auto default_skin_index = skeleton_data.default_skin_index();
        default_skin_index.has_value() && *default_skin_index < skeleton_data.skins().size()) {
        active_skin_names.push_back(skeleton_data.skins()[*default_skin_index].name);
    }

    for (const std::string& requested_skin : requested_skins) {
        append_unique(active_skin_names, requested_skin);
    }
    return active_skin_names;
}

std::string summarize_runtime(
    const marrow::runtime::SkeletonData& skeleton_data,
    const std::vector<std::string>& active_skin_names) {
    const auto animation_names = collect_names(skeleton_data.animations());
    const auto skin_names = collect_names(skeleton_data.skins());
    const auto& info = skeleton_data.info();

    std::ostringstream stream;
    stream << "Skeleton: " << info.name << " (" << info.width << " x " << info.height << ")\n"
           << "Counts: bones=" << skeleton_data.bones().size()
           << " slots=" << skeleton_data.slots().size()
           << " skins=" << skeleton_data.skins().size()
           << " animations=" << skeleton_data.animations().size()
           << " events=" << skeleton_data.events().size()
           << " ik=" << skeleton_data.ik_constraints().size()
           << " path=" << skeleton_data.path_constraints().size()
           << " transform=" << skeleton_data.transform_constraints().size()
           << " physics=" << skeleton_data.physics_constraints().size() << '\n'
           << "Animations: " << join_or_none(animation_names) << '\n'
           << "Skins: " << join_or_none(skin_names) << '\n'
           << "Active skins: " << join_or_none(active_skin_names);
    return stream.str();
}

bool apply_requested_skins(
    marrow::runtime::Skeleton& skeleton,
    const std::vector<std::string>& requested_skins) {
    if (requested_skins.empty()) {
        return true;
    }

    std::vector<std::string_view> skin_views;
    skin_views.reserve(requested_skins.size());
    for (const std::string& requested_skin : requested_skins) {
        if (!skeleton.data()->find_skin_index(requested_skin).has_value()) {
            std::cerr << "Unknown skin: " << requested_skin << "\nAvailable skins: "
                      << join_or_none(collect_names(skeleton.data()->skins())) << '\n';
            return false;
        }
        skin_views.push_back(requested_skin);
    }

    if (!skeleton.set_skin_composition(skin_views)) {
        std::cerr << "Failed to apply the requested skin composition.\n";
        return false;
    }

    return true;
}

std::string member_path(std::string_view base, std::string_view key) {
    return std::string(base) + "." + std::string(key);
}

std::string array_path(std::string_view base, std::size_t index) {
    return std::string(base) + "[" + std::to_string(index) + "]";
}

std::optional<std::string> compare_values(
    const Value& left,
    const Value& right,
    std::string_view path) {
    if (left.type() != right.type()) {
        return std::string(path) + ": type mismatch";
    }

    switch (left.type()) {
    case Value::Type::Null:
        return std::nullopt;
    case Value::Type::Boolean:
        if (left.as_boolean() != right.as_boolean()) {
            return std::string(path) + ": boolean mismatch";
        }
        return std::nullopt;
    case Value::Type::Number:
        if (std::abs(left.as_number() - right.as_number()) >
            (1e-6 * std::max({1.0, std::abs(left.as_number()), std::abs(right.as_number())}))) {
            std::ostringstream stream;
            stream << path << ": number mismatch (" << left.as_number() << " vs "
                   << right.as_number() << ")";
            return stream.str();
        }
        return std::nullopt;
    case Value::Type::String:
        if (left.as_string() != right.as_string()) {
            return std::string(path) + ": string mismatch";
        }
        return std::nullopt;
    case Value::Type::Array:
        if (left.as_array().size() != right.as_array().size()) {
            return std::string(path) + ": array length mismatch";
        }
        for (std::size_t index = 0; index < left.as_array().size(); ++index) {
            if (const auto mismatch = compare_values(
                    left.as_array()[index],
                    right.as_array()[index],
                    array_path(path, index))) {
                return mismatch;
            }
        }
        return std::nullopt;
    case Value::Type::Object:
        if (left.as_object().size() != right.as_object().size()) {
            return std::string(path) + ": object member count mismatch";
        }
        for (const auto& [key, left_member] : left.as_object()) {
            const auto iterator = right.as_object().find(key);
            if (iterator == right.as_object().end()) {
                return member_path(path, key) + ": missing from comparison asset";
            }
            if (const auto mismatch =
                    compare_values(left_member, iterator->second, member_path(path, key))) {
                return mismatch;
            }
        }
        return std::nullopt;
    }

    return std::string(path) + ": unsupported value type";
}

} // namespace

int main(int argc, char** argv) {
    const ParseResult parse_result = parse_arguments(argc, argv);
    if (parse_result.status == ParseStatus::Help) {
        return 0;
    }
    if (parse_result.status != ParseStatus::Ok) {
        return 1;
    }

    const auto document_result =
        marrow::runtime::load_skeleton_document(parse_result.options.skeleton_path);
    if (!document_result) {
        std::cerr << document_result.error->format();
        return 1;
    }

    if (parse_result.options.export_binary_path.has_value()) {
        if (parse_result.options.export_binary_path->extension() !=
            marrow::runtime::skeleton_binary_extension()) {
            std::cerr << "Binary export paths must use "
                      << marrow::runtime::skeleton_binary_extension() << ".\n";
            return 1;
        }
        if (const auto error = marrow::runtime::write_skeleton_binary_document(
                *document_result.document,
                *parse_result.options.export_binary_path)) {
            std::cerr << error->format();
            return 1;
        }
    }

    const auto result = marrow::runtime::load_skeleton_data(*document_result.document);
    if (!result) {
        std::cerr << result.error->format();
        return 1;
    }

    marrow::runtime::Skeleton skeleton(result.skeleton_data);
    if (!apply_requested_skins(skeleton, parse_result.options.requested_skins)) {
        return 1;
    }

    const auto active_skin_names =
        resolve_active_skin_names(*result.skeleton_data, parse_result.options.requested_skins);
    const std::string runtime_summary =
        summarize_runtime(*result.skeleton_data, active_skin_names);

    std::cout << "File: " << parse_result.options.skeleton_path.string() << '\n'
              << runtime_summary << '\n';

    if (parse_result.options.export_binary_path.has_value()) {
        std::cout << "Wrote binary asset: "
                  << parse_result.options.export_binary_path->string() << '\n';
    }

    if (parse_result.options.compare_path.has_value()) {
        const auto compare_document_result =
            marrow::runtime::load_skeleton_document(*parse_result.options.compare_path);
        if (!compare_document_result) {
            std::cerr << compare_document_result.error->format();
            return 1;
        }

        if (const auto mismatch = compare_values(
                document_result.document->root,
                compare_document_result.document->root,
                "$")) {
            std::cerr << "Asset comparison failed: " << *mismatch << '\n';
            return 1;
        }

        const auto compare_skeleton_result =
            marrow::runtime::load_skeleton_data(*compare_document_result.document);
        if (!compare_skeleton_result) {
            std::cerr << compare_skeleton_result.error->format();
            return 1;
        }

        marrow::runtime::Skeleton compare_skeleton(compare_skeleton_result.skeleton_data);
        if (!apply_requested_skins(compare_skeleton, parse_result.options.requested_skins)) {
            return 1;
        }

        const auto compare_active_skin_names = resolve_active_skin_names(
            *compare_skeleton_result.skeleton_data,
            parse_result.options.requested_skins);
        const std::string compare_summary = summarize_runtime(
            *compare_skeleton_result.skeleton_data,
            compare_active_skin_names);
        if (runtime_summary != compare_summary) {
            std::cerr << "Runtime summary mismatch between "
                      << parse_result.options.skeleton_path.string() << " and "
                      << parse_result.options.compare_path->string() << ".\n";
            return 1;
        }

        std::cout << "Comparison: " << parse_result.options.skeleton_path.string() << " matches "
                  << parse_result.options.compare_path->string() << '\n';
    }
    return 0;
}
