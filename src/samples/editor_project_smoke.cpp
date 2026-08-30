#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "marrow/editor/project.hpp"
#include "marrow/editor/authoring.hpp"
#include "mesh_weight_model.hpp"
#include "marrow/editor/selection.hpp"
#include "marrow/editor/session.hpp"
#include "marrow/runtime/animation_compare.hpp"

namespace {

constexpr double kMar162ScaleTime = 0.375;
constexpr double kMar162ScaleX = -1.25;
constexpr double kMar162ScaleY = -0.0;
constexpr double kMar161AbsoluteRotation = 769.0;
constexpr double kMar161RelativeRotation = 739.0;
constexpr double kMar161RotationTime = 0.75;

struct Options {
    std::filesystem::path project_path{"assets/fixtures/player_idle.marrow"};
    bool create_project{false};
    std::filesystem::path skeleton_path{"assets/fixtures/player_idle.mskl"};
    std::vector<std::filesystem::path> atlas_paths{"assets/fixtures/player_idle.matl"};
    std::optional<std::filesystem::path> export_runtime_path;
    std::optional<std::filesystem::path> export_binary_path;
    std::string project_name;
};

enum class ParseStatus {
    Ok,
    Help,
    Error,
};

struct ParseResult {
    ParseStatus status{ParseStatus::Error};
    Options options;
};

void print_usage(std::string_view executable_name) {
    std::cout << "Usage: " << executable_name
              << " [project.marrow]\n"
                 "       "
              << executable_name
              << " --create <project.marrow> [--skeleton <file.mskl>] [--atlas <file.matl> ...] [--name <project-name>] [--export-runtime <out.mskl>] [--export-binary <out.mbin>]\n"
                 "Load or create a minimal Marrow editor project, then optionally export its runtime asset bundle.\n";
}

std::string join_paths(const std::vector<std::filesystem::path>& paths) {
    if (paths.empty()) {
        return "<none>";
    }

    std::string joined;
    for (std::size_t index = 0; index < paths.size(); ++index) {
        if (index > 0) {
            joined += ", ";
        }
        joined += paths[index].string();
    }
    return joined;
}

std::string join_strings(const std::vector<std::string>& values) {
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
    bool project_path_set = false;
    bool atlas_paths_overridden = false;

    for (int index = 1; index < argc; ++index) {
        const std::string_view argument = argv[index];
        if (argument == "-h" || argument == "--help") {
            print_usage(argv[0]);
            result.status = ParseStatus::Help;
            return result;
        }

        if (argument == "--create") {
            if (index + 1 >= argc) {
                std::cerr << "--create requires an output project path.\n";
                print_usage(argv[0]);
                return result;
            }
            result.options.create_project = true;
            result.options.project_path = std::filesystem::path(argv[++index]);
            project_path_set = true;
            continue;
        }

        if (argument == "--skeleton") {
            if (index + 1 >= argc) {
                std::cerr << "--skeleton requires a .mskl path.\n";
                print_usage(argv[0]);
                return result;
            }
            result.options.skeleton_path = std::filesystem::path(argv[++index]);
            continue;
        }

        if (argument == "--atlas") {
            if (index + 1 >= argc) {
                std::cerr << "--atlas requires a .matl path.\n";
                print_usage(argv[0]);
                return result;
            }
            if (!atlas_paths_overridden) {
                result.options.atlas_paths.clear();
                atlas_paths_overridden = true;
            }
            result.options.atlas_paths.emplace_back(argv[++index]);
            continue;
        }

        if (argument == "--name") {
            if (index + 1 >= argc) {
                std::cerr << "--name requires a project name.\n";
                print_usage(argv[0]);
                return result;
            }
            result.options.project_name = argv[++index];
            continue;
        }

        if (argument == "--export-runtime") {
            if (index + 1 >= argc) {
                std::cerr << "--export-runtime requires an output .mskl path.\n";
                print_usage(argv[0]);
                return result;
            }
            result.options.export_runtime_path = std::filesystem::path(argv[++index]);
            continue;
        }

        if (argument == "--export-binary") {
            if (index + 1 >= argc) {
                std::cerr << "--export-binary requires an output .mbin path.\n";
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

        if (project_path_set) {
            std::cerr << "Only one project path may be provided.\n";
            print_usage(argv[0]);
            return result;
        }

        result.options.project_path = std::filesystem::path(argument);
        project_path_set = true;
    }

    if (result.options.create_project && result.options.atlas_paths.empty()) {
        std::cerr << "At least one atlas path is required when creating a project.\n";
        return result;
    }

    result.status = ParseStatus::Ok;
    return result;
}

bool create_project(const Options& options) {
    marrow::editor::MinimalProjectOptions create_options;
    create_options.project_path = options.project_path;
    create_options.skeleton_path = options.skeleton_path;
    create_options.atlas_paths = options.atlas_paths;
    create_options.name = options.project_name;
    create_options.notes = "Generated by marrow_project_smoke for minimal editor project validation.";

    const marrow::editor::ProjectData project =
        marrow::editor::create_minimal_project(create_options);
    const auto save_result = marrow::editor::save_project(project, options.project_path);
    if (!save_result) {
        std::cerr << save_result.error->format() << '\n';
        return false;
    }

    std::cout << "Created project: " << options.project_path.string() << '\n';
    return true;
}

void print_summary(const marrow::editor::ProjectLoadResult& result, const std::filesystem::path& path) {
    const auto& onion_skin = result.project->editor_metadata.viewport.onion_skin;
    const auto& debug_overlay = result.project->editor_metadata.viewport.debug_overlay;
    std::cout << "Project: " << path.string() << '\n'
              << "Name: " << result.project->editor_metadata.name << '\n'
              << "Runtime skeleton: " << result.project->resolved_skeleton_path().string() << '\n'
              << "Runtime atlases: " << join_paths(result.project->resolved_atlas_paths()) << '\n'
              << "Preview animation: " << result.project->editor_metadata.active_animation << '\n'
              << "Preview skins: " << join_strings(result.project->editor_metadata.preview_skins) << '\n'
              << "Onion skin: " << (onion_skin.enabled ? "on" : "off")
              << " / " << (onion_skin.mode == marrow::editor::OnionSkinMode::Frame ? "frame"
                                                                                     : "keyframe")
              << " / before " << onion_skin.before_count
              << " / after " << onion_skin.after_count
              << " / step " << onion_skin.step
              << " / anchor " << (onion_skin.anchor_to_zero ? "on" : "off") << '\n'
              << "Debug overlay: bones " << (debug_overlay.bones ? "on" : "off")
              << " / ik " << (debug_overlay.ik_constraints ? "on" : "off")
              << " / path " << (debug_overlay.path_constraints ? "on" : "off")
              << " / physics " << (debug_overlay.physics_constraints ? "on" : "off")
              << " / meshes " << (debug_overlay.mesh_wireframes ? "on" : "off")
              << " / bounds " << (debug_overlay.bounding_boxes ? "on" : "off") << '\n'
              << "Edited transform tracks: " << result.project->transform_timeline_edits.size() << '\n'
              << "Edited deform tracks: " << result.project->mesh_deform_timeline_edits.size() << '\n'
              << "Edited mesh weights: " << result.project->mesh_weight_attachment_edits.size() << '\n'
              << "Edited draw-order tracks: " << result.project->draw_order_timeline_edits.size() << '\n'
              << "Edited event tracks: " << result.project->event_timeline_edits.size() << '\n'
              << "Edited constraints: IK " << result.project->ik_constraint_edits.size()
              << ", Path " << result.project->path_constraint_edits.size()
              << ", Transform " << result.project->transform_constraint_edits.size()
              << ", Physics " << result.project->physics_constraint_edits.size() << '\n'
              << "Export target: " << result.project->resolved_export_skeleton_path().string() << '\n'
              << "Loaded skeleton: " << result.skeleton_data->info().name << " ("
              << result.skeleton_data->bones().size() << " bones, "
              << result.skeleton_data->slots().size() << " slots)\n";

    std::vector<std::string> atlas_names;
    atlas_names.reserve(result.atlas_data.size());
    for (const auto& atlas : result.atlas_data) {
        atlas_names.push_back(atlas->info().name);
    }
    std::cout << "Loaded atlases: " << join_strings(atlas_names) << '\n';
}

/**
 * @brief Validates viewport metadata against the loaded project's own contract.
 *
 * Zoom and the onion-skin block are struct defaults, so every project must
 * satisfy them. The debug overlay is not: `player_idle.marrow` deliberately
 * enables all six toggles, while `create_minimal_project` authors none and
 * therefore loads the `DebugOverlaySettings` defaults. Asserting the fixture's
 * shape against a freshly created project is what made `--create` fail.
 */
bool validate_viewport_settings(
    const marrow::editor::ProjectLoadResult& result,
    bool created_minimal_project) {
    if (result.project == nullptr) {
        std::cerr << "Viewport validation requires a loaded project.\n";
        return false;
    }

    const auto& viewport = result.project->editor_metadata.viewport;
    const auto& onion_skin = viewport.onion_skin;
    const auto& debug_overlay = viewport.debug_overlay;
    if (viewport.zoom <= 0.0) {
        std::cerr << "Viewport validation expected a positive zoom level.\n";
        return false;
    }
    if (onion_skin.enabled ||
        onion_skin.mode != marrow::editor::OnionSkinMode::Frame ||
        onion_skin.anchor_to_zero ||
        onion_skin.before_count != 3 ||
        onion_skin.after_count != 3 ||
        onion_skin.step != 1) {
        std::cerr << "Viewport validation expected the default 3+3 frame-based onion-skin settings.\n";
        return false;
    }
    if (created_minimal_project) {
        if (!debug_overlay.bones ||
            debug_overlay.ik_constraints ||
            debug_overlay.path_constraints ||
            debug_overlay.physics_constraints ||
            debug_overlay.mesh_wireframes ||
            debug_overlay.bounding_boxes) {
            std::cerr << "Viewport validation expected a created project to load the default bones-only debug overlay.\n";
            return false;
        }
    } else if (
        !debug_overlay.bones ||
        !debug_overlay.ik_constraints ||
        !debug_overlay.path_constraints ||
        !debug_overlay.physics_constraints ||
        !debug_overlay.mesh_wireframes ||
        !debug_overlay.bounding_boxes) {
        std::cerr << "Viewport validation expected the fixture debug overlay toggles to be enabled.\n";
        return false;
    }

    std::cout << "Viewport metadata validated.\n";
    return true;
}

bool require_near(double actual, double expected, std::string_view label);
std::optional<std::string> compare_values(
    const marrow::runtime::json::Value& left,
    const marrow::runtime::json::Value& right,
    std::string_view path);

bool validate_snap_settings(const marrow::editor::ProjectLoadResult& result) {
    if (result.project == nullptr || result.base_skeleton_document == nullptr) {
        std::cerr << "Snap validation requires a loaded project and runtime source.\n";
        return false;
    }

    marrow::editor::ProjectData legacy = *result.project;
    legacy.snap_settings.reset();
    if (legacy.preserved_root.is_object()) {
        legacy.preserved_root.as_object().erase("snap");
    }
    const std::string legacy_serialized = marrow::editor::serialize_project(legacy);
    const auto legacy_document = marrow::runtime::json::parse_document(
        legacy_serialized, result.project->source_path);
    if (!legacy_document ||
        marrow::runtime::json::find_member(legacy_document.document->root, "snap") != nullptr) {
        std::cerr << "Legacy project serialization materialized an absent snap section.\n";
        return false;
    }
    const auto legacy_reloaded = marrow::editor::load_project(*legacy_document.document);
    if (!legacy_reloaded || legacy_reloaded.project->snap_settings.has_value()) {
        std::cerr << "Legacy project did not reload with optional default-off snapping.\n";
        return false;
    }

    // The MAR-165 field-removal check needs a project that actually authors a
    // snap section. `player_idle.marrow` does; a freshly created minimal
    // project does not, and its documented contract is exactly that absence.
    marrow::editor::ProjectData mar165_source = *result.project;
    if (!mar165_source.snap_settings.has_value()) {
        marrow::editor::ProjectSnapSettings seeded;
        seeded.magnetic_vertex_enabled = true;
        mar165_source.snap_settings = seeded;
    }
    const auto current_document = marrow::runtime::json::parse_document(
        marrow::editor::serialize_project(mar165_source),
        result.project->source_path);
    if (!current_document) {
        std::cerr << current_document.error->format();
        return false;
    }
    auto mar165_document = *current_document.document;
    auto* mar165_snap = marrow::runtime::json::find_member(
        mar165_document.root, "snap");
    if (mar165_snap == nullptr || !mar165_snap->is_object()) {
        std::cerr << "MAR-165 compatibility check requires snap metadata.\n";
        return false;
    }
    mar165_snap->as_object().erase("magnetic_vertex_enabled");
    const auto mar165_reloaded = marrow::editor::load_project(mar165_document);
    if (!mar165_reloaded || !mar165_reloaded.project->snap_settings.has_value() ||
        mar165_reloaded.project->snap_settings->magnetic_vertex_enabled) {
        std::cerr << "MAR-165 snap metadata did not default magnetic vertices off.\n";
        return false;
    }

    const auto future_source = marrow::runtime::json::parse_document(
        R"({"future_snap_mode":{"sentinel":7}})", result.project->source_path);
    if (!future_source) {
        std::cerr << future_source.error->format();
        return false;
    }
    marrow::editor::ProjectSnapSettings settings;
    settings.world_grid_enabled = true;
    settings.local_angle_enabled = false;
    settings.absolute_scale_enabled = true;
    settings.magnetic_vertex_enabled = true;
    settings.world_grid_step = 12.5;
    settings.local_angle_step_degrees = 22.5;
    settings.absolute_scale_step = 0.25;
    settings.preserved_source = future_source.document->root;

    marrow::editor::ProjectData snapped = legacy;
    snapped.snap_settings = settings;
    const std::string snapped_serialized = marrow::editor::serialize_project(snapped);
    const auto snapped_document = marrow::runtime::json::parse_document(
        snapped_serialized, result.project->source_path);
    if (!snapped_document) {
        std::cerr << snapped_document.error->format();
        return false;
    }
    const auto* snap_value = marrow::runtime::json::find_member(
        snapped_document.document->root, "snap");
    const auto* magnetic_value = snap_value != nullptr
        ? marrow::runtime::json::find_member(
              *snap_value, "magnetic_vertex_enabled")
        : nullptr;
    if (snap_value == nullptr || !snap_value->is_object() ||
        marrow::runtime::json::find_member(*snap_value, "future_snap_mode") == nullptr ||
        magnetic_value == nullptr || !magnetic_value->is_boolean() ||
        !magnetic_value->as_boolean()) {
        std::cerr << "Snap serialization did not retain its unknown additive field.\n";
        return false;
    }

    const auto snapped_reloaded = marrow::editor::load_project(*snapped_document.document);
    if (!snapped_reloaded || !snapped_reloaded.project->snap_settings.has_value()) {
        std::cerr << "Snap settings did not survive project reload.\n";
        return false;
    }
    const auto& reloaded = *snapped_reloaded.project->snap_settings;
    if (!reloaded.world_grid_enabled || reloaded.local_angle_enabled ||
        !reloaded.absolute_scale_enabled || !reloaded.magnetic_vertex_enabled ||
        !require_near(reloaded.world_grid_step, 12.5, "snap world grid step") ||
        !require_near(
            reloaded.local_angle_step_degrees, 22.5, "snap local angle step") ||
        !require_near(reloaded.absolute_scale_step, 0.25, "snap scale step")) {
        std::cerr << "Snap settings reloaded with changed typed values.\n";
        return false;
    }

    const std::string snap_path_token = std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());
    const auto valid_save_path = result.project->source_path.parent_path() /
        (".marrow_mar166_snap_roundtrip_" + snap_path_token + ".marrow");
    std::error_code ignored;
    const auto valid_save_result = marrow::editor::save_project(
        snapped, valid_save_path);
    const auto file_reloaded = valid_save_result
        ? marrow::editor::load_project(valid_save_path)
        : marrow::editor::ProjectLoadResult{};
    std::filesystem::remove(valid_save_path, ignored);
    const auto* file_snap = file_reloaded &&
            file_reloaded.project->snap_settings.has_value()
        ? &*file_reloaded.project->snap_settings
        : nullptr;
    if (!valid_save_result || !file_reloaded || file_snap == nullptr ||
        !file_snap->world_grid_enabled || file_snap->local_angle_enabled ||
        !file_snap->absolute_scale_enabled || !file_snap->magnetic_vertex_enabled ||
        !require_near(file_snap->world_grid_step, 12.5, "saved snap world grid step") ||
        !require_near(
            file_snap->local_angle_step_degrees,
            22.5,
            "saved snap local angle step") ||
        !require_near(file_snap->absolute_scale_step, 0.25, "saved snap scale step") ||
        marrow::runtime::json::find_member(
            file_snap->preserved_source, "future_snap_mode") == nullptr) {
        std::cerr << "Snap settings did not survive a filesystem save/reload.\n";
        return false;
    }

    const auto legacy_runtime = marrow::editor::build_project_runtime_document(
        legacy, *result.base_skeleton_document);
    const auto snapped_runtime = marrow::editor::build_project_runtime_document(
        snapped, *result.base_skeleton_document);
    if (const auto mismatch = compare_values(
            legacy_runtime.root, snapped_runtime.root, "$")) {
        std::cerr << "Snap settings leaked into runtime export: " << *mismatch << '\n';
        return false;
    }

    auto invalid_document = *snapped_document.document;
    auto* invalid_snap = marrow::runtime::json::find_member(
        invalid_document.root, "snap");
    auto* invalid_step = invalid_snap != nullptr
        ? marrow::runtime::json::find_member(*invalid_snap, "world_grid_step")
        : nullptr;
    if (invalid_step == nullptr) {
        std::cerr << "Snap validation could not prepare an invalid step.\n";
        return false;
    }
    *invalid_step = marrow::runtime::json::Value(0.0, invalid_step->location());
    if (marrow::editor::load_project(invalid_document)) {
        std::cerr << "Project loader accepted a zero snap step.\n";
        return false;
    }

    auto invalid_magnetic_document = *snapped_document.document;
    auto* invalid_magnetic_snap = marrow::runtime::json::find_member(
        invalid_magnetic_document.root, "snap");
    if (invalid_magnetic_snap == nullptr || !invalid_magnetic_snap->is_object()) {
        std::cerr << "Snap validation could not prepare an invalid magnetic toggle.\n";
        return false;
    }
    invalid_magnetic_snap->as_object()["magnetic_vertex_enabled"] =
        marrow::runtime::json::Value(
            std::string("enabled"), invalid_magnetic_snap->location());
    if (marrow::editor::load_project(invalid_magnetic_document)) {
        std::cerr << "Project loader accepted a non-boolean magnetic vertex toggle.\n";
        return false;
    }

    marrow::editor::ProjectData invalid_save = snapped;
    invalid_save.snap_settings->absolute_scale_step =
        std::numeric_limits<double>::infinity();
    const auto invalid_save_path = std::filesystem::temp_directory_path() /
        ("marrow_mar166_invalid_snap_" + snap_path_token + ".marrow");
    const auto invalid_save_result = marrow::editor::save_project(
        invalid_save, invalid_save_path);
    std::filesystem::remove(invalid_save_path, ignored);
    if (invalid_save_result) {
        std::cerr << "Project saver accepted a non-finite snap step.\n";
        return false;
    }

    std::cout << "Optional project snap metadata and export neutrality validated.\n";
    return true;
}

bool require_near(double actual, double expected, std::string_view label) {
    if (std::abs(actual - expected) <= 1e-6) {
        return true;
    }

    std::cerr << label << " expected " << expected << " but was " << actual << '\n';
    return false;
}

std::string member_path(std::string_view base, std::string_view key) {
    return std::string(base) + "." + std::string(key);
}

std::string array_path(std::string_view base, std::size_t index) {
    return std::string(base) + "[" + std::to_string(index) + "]";
}

std::optional<std::string> compare_values(
    const marrow::runtime::json::Value& left,
    const marrow::runtime::json::Value& right,
    std::string_view path) {
    using marrow::runtime::json::Value;

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
            return std::string(path) + ": number mismatch";
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

    return std::nullopt;
}

bool validate_export_round_trip(
    const marrow::editor::ProjectLoadResult& project_result,
    const std::filesystem::path& export_path) {
    const auto export_result = marrow::runtime::load_skeleton_data(export_path);
    if (!export_result) {
        std::cerr << export_result.error->format();
        return false;
    }

    if (project_result.project->transform_timeline_edits.empty() &&
        project_result.project->mesh_deform_timeline_edits.empty() &&
        project_result.project->mesh_weight_attachment_edits.empty() &&
        project_result.project->draw_order_timeline_edits.empty() &&
        project_result.project->event_timeline_edits.empty() &&
        project_result.project->ik_constraint_edits.empty() &&
        project_result.project->path_constraint_edits.empty() &&
        project_result.project->transform_constraint_edits.empty() &&
        project_result.project->physics_constraint_edits.empty()) {
        std::cout << "Exported runtime skeleton: " << export_path.string() << '\n';
        return true;
    }

    const auto validate_vector_timeline = [&](const auto* authored_timeline,
                                              const auto* exported_timeline) {
        if (authored_timeline == nullptr || exported_timeline == nullptr ||
            authored_timeline->keyframes.size() != exported_timeline->keyframes.size()) {
            std::cerr << "Vector timeline export did not preserve the authored keyframe count.\n";
            return false;
        }

        for (std::size_t index = 0; index < authored_timeline->keyframes.size(); ++index) {
            const auto& authored_key = authored_timeline->keyframes[index];
            const auto& exported_key = exported_timeline->keyframes[index];
            if (!require_near(exported_key.time, authored_key.time, "vector time") ||
                !require_near(exported_key.x, authored_key.x, "vector x") ||
                !require_near(exported_key.y, authored_key.y, "vector y") ||
                exported_key.interpolation.kind() != authored_key.interpolation.kind()) {
                std::cerr << "Vector timeline export did not preserve the authored curve.\n";
                return false;
            }
        }

        return true;
    };

    const auto validate_mesh_weight_attachment =
        [&](const marrow::editor::MeshWeightAttachmentEdit& edit) {
            const auto authored_slot_index =
                project_result.skeleton_data->find_slot_index(edit.slot_name);
            const auto exported_slot_index =
                export_result.skeleton_data->find_slot_index(edit.slot_name);
            const auto authored_attachment =
                authored_slot_index.has_value()
                    ? project_result.skeleton_data->find_attachment(
                          edit.skin_name,
                          *authored_slot_index,
                          edit.attachment_name)
                    : nullptr;
            const auto exported_attachment =
                exported_slot_index.has_value()
                    ? export_result.skeleton_data->find_attachment(
                          edit.skin_name,
                          *exported_slot_index,
                          edit.attachment_name)
                    : nullptr;
            if (!authored_slot_index.has_value() || !exported_slot_index.has_value() ||
                authored_attachment == nullptr || exported_attachment == nullptr ||
                authored_attachment->mesh_geometry == nullptr ||
                exported_attachment->mesh_geometry == nullptr) {
                std::cerr << "Mesh-weight export validation could not resolve the edited attachment.\n";
                return false;
            }

            const auto& authored_weights = authored_attachment->mesh_geometry->weights;
            const auto& exported_weights = exported_attachment->mesh_geometry->weights;
            if (authored_weights.size() != exported_weights.size()) {
                std::cerr << "Mesh-weight export did not preserve the edited vertex count.\n";
                return false;
            }

            const auto influence_bone_name =
                [](const auto& skeleton_data, const auto& influence) -> std::optional<std::string> {
                    if (influence.bone_index >= skeleton_data->bones().size()) {
                        return std::nullopt;
                    }
                    return skeleton_data->bones()[influence.bone_index].name;
                };

            for (std::size_t vertex_index = 0; vertex_index < authored_weights.size(); ++vertex_index) {
                const auto& authored_vertex = authored_weights[vertex_index];
                const auto& exported_vertex = exported_weights[vertex_index];
                if (authored_vertex.influences.size() != exported_vertex.influences.size()) {
                    std::cerr << "Mesh-weight export did not preserve the authored influence count.\n";
                    return false;
                }

                for (std::size_t influence_index = 0;
                     influence_index < authored_vertex.influences.size();
                     ++influence_index) {
                    const auto& authored_influence = authored_vertex.influences[influence_index];
                    const auto& exported_influence = exported_vertex.influences[influence_index];
                    if (influence_bone_name(project_result.skeleton_data, authored_influence) !=
                            influence_bone_name(export_result.skeleton_data, exported_influence) ||
                        !require_near(
                            exported_influence.x,
                            authored_influence.x,
                            "mesh weight bind x") ||
                        !require_near(
                            exported_influence.y,
                            authored_influence.y,
                            "mesh weight bind y") ||
                        !require_near(
                            exported_influence.weight,
                            authored_influence.weight,
                            "mesh weight value")) {
                        std::cerr << "Mesh-weight export did not preserve the authored vertex influences.\n";
                        return false;
                    }
                }
            }

            return true;
        };

    if (!project_result.project->transform_timeline_edits.empty()) {
        const auto& edit = project_result.project->transform_timeline_edits.front();
        const auto* authored_animation =
            project_result.skeleton_data->find_animation(edit.animation_name);
        const auto* exported_animation =
            export_result.skeleton_data->find_animation(edit.animation_name);
        const auto authored_bone_index =
            project_result.skeleton_data->find_bone_index(edit.bone_name);
        const auto exported_bone_index =
            export_result.skeleton_data->find_bone_index(edit.bone_name);
        if (authored_animation == nullptr || exported_animation == nullptr ||
            !authored_bone_index.has_value() || !exported_bone_index.has_value()) {
            std::cerr << "Export validation could not resolve the edited transform track.\n";
            return false;
        }

        switch (edit.channel) {
        case marrow::editor::TransformTimelineChannel::Rotate: {
            const auto* authored_timeline =
                authored_animation->find_rotate_timeline(*authored_bone_index);
            const auto* exported_timeline =
                exported_animation->find_rotate_timeline(*exported_bone_index);
            if (authored_timeline == nullptr || exported_timeline == nullptr ||
                authored_timeline->keyframes.size() != exported_timeline->keyframes.size()) {
                std::cerr << "Rotate timeline export did not preserve the authored keyframe count.\n";
                return false;
            }

            for (std::size_t index = 0; index < authored_timeline->keyframes.size(); ++index) {
                const auto& authored_key = authored_timeline->keyframes[index];
                const auto& exported_key = exported_timeline->keyframes[index];
                if (!require_near(exported_key.time, authored_key.time, "rotate time") ||
                    !require_near(exported_key.angle, authored_key.angle, "rotate angle") ||
                    exported_key.interpolation.kind() != authored_key.interpolation.kind()) {
                    std::cerr << "Rotate timeline export did not preserve the authored curve.\n";
                    return false;
                }
                if (authored_key.interpolation.kind() ==
                    marrow::runtime::InterpolationKind::CubicBezier) {
                    const auto& authored_bezier = authored_key.interpolation.cubic_bezier();
                    const auto& exported_bezier = exported_key.interpolation.cubic_bezier();
                    if (!require_near(exported_bezier.cx1, authored_bezier.cx1, "bezier cx1") ||
                        !require_near(exported_bezier.cy1, authored_bezier.cy1, "bezier cy1") ||
                        !require_near(exported_bezier.cx2, authored_bezier.cx2, "bezier cx2") ||
                        !require_near(exported_bezier.cy2, authored_bezier.cy2, "bezier cy2")) {
                        return false;
                    }
                }
            }
            break;
        }
        case marrow::editor::TransformTimelineChannel::Translate:
            if (!validate_vector_timeline(
                    authored_animation->find_translate_timeline(*authored_bone_index),
                    exported_animation->find_translate_timeline(*exported_bone_index))) {
                return false;
            }
            break;
        case marrow::editor::TransformTimelineChannel::Scale:
            if (!validate_vector_timeline(
                    authored_animation->find_scale_timeline(*authored_bone_index),
                    exported_animation->find_scale_timeline(*exported_bone_index))) {
                return false;
            }
            break;
        case marrow::editor::TransformTimelineChannel::Shear:
            if (!validate_vector_timeline(
                    authored_animation->find_shear_timeline(*authored_bone_index),
                    exported_animation->find_shear_timeline(*exported_bone_index))) {
                return false;
            }
            break;
        }
    }

    if (!project_result.project->mesh_deform_timeline_edits.empty()) {
        const auto& edit = project_result.project->mesh_deform_timeline_edits.front();
        const auto* authored_animation =
            project_result.skeleton_data->find_animation(edit.animation_name);
        const auto* exported_animation =
            export_result.skeleton_data->find_animation(edit.animation_name);
        const auto authored_slot_index =
            project_result.skeleton_data->find_slot_index(edit.slot_name);
        const auto exported_slot_index =
            export_result.skeleton_data->find_slot_index(edit.slot_name);
        if (authored_animation == nullptr || exported_animation == nullptr ||
            !authored_slot_index.has_value() || !exported_slot_index.has_value()) {
            std::cerr << "Export validation could not resolve the edited deform track.\n";
            return false;
        }

        const auto* authored_timeline =
            authored_animation->find_deform_timeline(*authored_slot_index, edit.attachment_name);
        const auto* exported_timeline =
            exported_animation->find_deform_timeline(*exported_slot_index, edit.attachment_name);
        if (authored_timeline == nullptr || exported_timeline == nullptr ||
            authored_timeline->keyframes.size() != exported_timeline->keyframes.size()) {
            std::cerr << "Deform timeline export did not preserve the authored keyframe count.\n";
            return false;
        }

        for (std::size_t index = 0; index < authored_timeline->keyframes.size(); ++index) {
            const auto& authored_key = authored_timeline->keyframes[index];
            const auto& exported_key = exported_timeline->keyframes[index];
            if (!require_near(exported_key.time, authored_key.time, "deform time") ||
                authored_key.vertex_offsets != exported_key.vertex_offsets ||
                exported_key.interpolation.kind() != authored_key.interpolation.kind()) {
                std::cerr << "Deform timeline export did not preserve the authored offsets.\n";
                return false;
            }
        }
    }

    for (const auto& edit : project_result.project->mesh_weight_attachment_edits) {
        if (!validate_mesh_weight_attachment(edit)) {
            return false;
        }
    }

    const auto draw_order_slot_names =
        [](const auto& skeleton_data, const auto& slot_indices) {
            std::vector<std::string> slot_names;
            slot_names.reserve(slot_indices.size());
            for (const std::size_t slot_index : slot_indices) {
                if (slot_index >= skeleton_data->slots().size()) {
                    return std::vector<std::string>{};
                }
                slot_names.push_back(skeleton_data->slots()[slot_index].name);
            }
            return slot_names;
        };

    if (!project_result.project->draw_order_timeline_edits.empty()) {
        const auto& edit = project_result.project->draw_order_timeline_edits.front();
        const auto* authored_animation =
            project_result.skeleton_data->find_animation(edit.animation_name);
        const auto* exported_animation =
            export_result.skeleton_data->find_animation(edit.animation_name);
        const auto* authored_timeline =
            authored_animation != nullptr ? authored_animation->find_draw_order_timeline() : nullptr;
        const auto* exported_timeline =
            exported_animation != nullptr ? exported_animation->find_draw_order_timeline() : nullptr;
        if (authored_timeline == nullptr || exported_timeline == nullptr ||
            authored_timeline->keyframes.size() != exported_timeline->keyframes.size()) {
            std::cerr << "Draw-order timeline export did not preserve the authored keyframe count.\n";
            return false;
        }

        for (std::size_t index = 0; index < authored_timeline->keyframes.size(); ++index) {
            const auto& authored_key = authored_timeline->keyframes[index];
            const auto& exported_key = exported_timeline->keyframes[index];
            if (!require_near(exported_key.time, authored_key.time, "draw-order time") ||
                draw_order_slot_names(project_result.skeleton_data, authored_key.slot_indices) !=
                    draw_order_slot_names(export_result.skeleton_data, exported_key.slot_indices)) {
                std::cerr << "Draw-order timeline export did not preserve the authored slot order.\n";
                return false;
            }
        }
    }

    const auto event_name_for_keyframe =
        [](const auto& skeleton_data, const auto& keyframe) -> std::optional<std::string> {
            if (keyframe.event_index >= skeleton_data->events().size()) {
                return std::nullopt;
            }
            return skeleton_data->events()[keyframe.event_index].name;
        };

    if (!project_result.project->event_timeline_edits.empty()) {
        const auto& edit = project_result.project->event_timeline_edits.front();
        const auto* authored_animation =
            project_result.skeleton_data->find_animation(edit.animation_name);
        const auto* exported_animation =
            export_result.skeleton_data->find_animation(edit.animation_name);
        const auto* authored_timeline =
            authored_animation != nullptr ? authored_animation->find_event_timeline() : nullptr;
        const auto* exported_timeline =
            exported_animation != nullptr ? exported_animation->find_event_timeline() : nullptr;
        if (authored_timeline == nullptr || exported_timeline == nullptr ||
            authored_timeline->keyframes.size() != exported_timeline->keyframes.size()) {
            std::cerr << "Event timeline export did not preserve the authored keyframe count.\n";
            return false;
        }

        for (std::size_t index = 0; index < authored_timeline->keyframes.size(); ++index) {
            const auto& authored_key = authored_timeline->keyframes[index];
            const auto& exported_key = exported_timeline->keyframes[index];
            if (!require_near(exported_key.time, authored_key.time, "event time") ||
                event_name_for_keyframe(project_result.skeleton_data, authored_key) !=
                    event_name_for_keyframe(export_result.skeleton_data, exported_key) ||
                authored_key.int_value != exported_key.int_value ||
                authored_key.float_value != exported_key.float_value ||
                authored_key.string_value != exported_key.string_value ||
                authored_key.audio_path != exported_key.audio_path ||
                authored_key.volume != exported_key.volume ||
                authored_key.balance != exported_key.balance) {
                std::cerr << "Event timeline export did not preserve the authored payload overrides.\n";
                return false;
            }
        }
    }

    const auto constraint_bone_names =
        [](const auto& skeleton_data, const auto& bone_indices) {
            std::vector<std::string> names;
            names.reserve(bone_indices.size());
            for (const std::size_t bone_index : bone_indices) {
                if (bone_index >= skeleton_data->bones().size()) {
                    return std::vector<std::string>{};
                }
                names.push_back(skeleton_data->bones()[bone_index].name);
            }
            return names;
        };

    if (!project_result.project->ik_constraint_edits.empty()) {
        const auto& edit = project_result.project->ik_constraint_edits.front();
        const auto authored_constraint = std::find_if(
            project_result.skeleton_data->ik_constraints().begin(),
            project_result.skeleton_data->ik_constraints().end(),
            [&](const marrow::runtime::IkConstraintData& constraint) {
                return constraint.name == edit.name;
            });
        const auto exported_constraint = std::find_if(
            export_result.skeleton_data->ik_constraints().begin(),
            export_result.skeleton_data->ik_constraints().end(),
            [&](const marrow::runtime::IkConstraintData& constraint) {
                return constraint.name == edit.name;
            });
        if (authored_constraint == project_result.skeleton_data->ik_constraints().end() ||
            exported_constraint == export_result.skeleton_data->ik_constraints().end() ||
            constraint_bone_names(project_result.skeleton_data, authored_constraint->bone_indices) !=
                constraint_bone_names(export_result.skeleton_data, exported_constraint->bone_indices) ||
            !require_near(exported_constraint->mix, authored_constraint->mix, "ik mix") ||
            !require_near(
                exported_constraint->softness,
                authored_constraint->softness,
                "ik softness") ||
            exported_constraint->bend_positive != authored_constraint->bend_positive ||
            exported_constraint->compress != authored_constraint->compress ||
            exported_constraint->stretch != authored_constraint->stretch ||
            project_result.skeleton_data->bones()[authored_constraint->target_bone_index].name !=
                export_result.skeleton_data->bones()[exported_constraint->target_bone_index].name) {
            std::cerr << "IK constraint export did not preserve the authored constraint edit.\n";
            return false;
        }
    }

    if (!project_result.project->path_constraint_edits.empty()) {
        const auto& edit = project_result.project->path_constraint_edits.front();
        const auto authored_constraint = std::find_if(
            project_result.skeleton_data->path_constraints().begin(),
            project_result.skeleton_data->path_constraints().end(),
            [&](const marrow::runtime::PathConstraintData& constraint) {
                return constraint.name == edit.name;
            });
        const auto exported_constraint = std::find_if(
            export_result.skeleton_data->path_constraints().begin(),
            export_result.skeleton_data->path_constraints().end(),
            [&](const marrow::runtime::PathConstraintData& constraint) {
                return constraint.name == edit.name;
            });
        if (authored_constraint == project_result.skeleton_data->path_constraints().end() ||
            exported_constraint == export_result.skeleton_data->path_constraints().end() ||
            constraint_bone_names(project_result.skeleton_data, authored_constraint->bone_indices) !=
                constraint_bone_names(export_result.skeleton_data, exported_constraint->bone_indices) ||
            project_result.skeleton_data->slots()[authored_constraint->slot_index].name !=
                export_result.skeleton_data->slots()[exported_constraint->slot_index].name ||
            !require_near(exported_constraint->position, authored_constraint->position, "path position") ||
            !require_near(exported_constraint->spacing, authored_constraint->spacing, "path spacing") ||
            exported_constraint->spacing_mode != authored_constraint->spacing_mode ||
            !require_near(exported_constraint->rotate_mix, authored_constraint->rotate_mix, "path rotateMix") ||
            !require_near(exported_constraint->translate_mix, authored_constraint->translate_mix, "path translateMix")) {
            std::cerr << "Path constraint export did not preserve the authored constraint edit.\n";
            return false;
        }
    }

    if (!project_result.project->transform_constraint_edits.empty()) {
        const auto& edit = project_result.project->transform_constraint_edits.front();
        const auto authored_constraint = std::find_if(
            project_result.skeleton_data->transform_constraints().begin(),
            project_result.skeleton_data->transform_constraints().end(),
            [&](const marrow::runtime::TransformConstraintData& constraint) {
                return constraint.name == edit.name;
            });
        const auto exported_constraint = std::find_if(
            export_result.skeleton_data->transform_constraints().begin(),
            export_result.skeleton_data->transform_constraints().end(),
            [&](const marrow::runtime::TransformConstraintData& constraint) {
                return constraint.name == edit.name;
            });
        if (authored_constraint == project_result.skeleton_data->transform_constraints().end() ||
            exported_constraint == export_result.skeleton_data->transform_constraints().end() ||
            constraint_bone_names(
                project_result.skeleton_data,
                authored_constraint->target_bone_indices) !=
                constraint_bone_names(
                    export_result.skeleton_data,
                    exported_constraint->target_bone_indices) ||
            project_result.skeleton_data->bones()[authored_constraint->source_bone_index].name !=
                export_result.skeleton_data->bones()[exported_constraint->source_bone_index].name ||
            !require_near(exported_constraint->rotate_mix, authored_constraint->rotate_mix, "transform rotateMix") ||
            !require_near(exported_constraint->translate_mix, authored_constraint->translate_mix, "transform translateMix") ||
            !require_near(exported_constraint->scale_mix, authored_constraint->scale_mix, "transform scaleMix") ||
            !require_near(exported_constraint->shear_mix, authored_constraint->shear_mix, "transform shearMix") ||
            !require_near(exported_constraint->offsets.rotation, authored_constraint->offsets.rotation, "transform offset rotation") ||
            !require_near(exported_constraint->offsets.x, authored_constraint->offsets.x, "transform offset x") ||
            !require_near(exported_constraint->offsets.y, authored_constraint->offsets.y, "transform offset y") ||
            !require_near(exported_constraint->offsets.scale_x, authored_constraint->offsets.scale_x, "transform offset scale_x") ||
            !require_near(exported_constraint->offsets.scale_y, authored_constraint->offsets.scale_y, "transform offset scale_y") ||
            !require_near(exported_constraint->offsets.shear_x, authored_constraint->offsets.shear_x, "transform offset shear_x") ||
            !require_near(exported_constraint->offsets.shear_y, authored_constraint->offsets.shear_y, "transform offset shear_y")) {
            std::cerr << "Transform constraint export did not preserve the authored constraint edit.\n";
            return false;
        }
    }

    if (!project_result.project->physics_constraint_edits.empty()) {
        const auto& edit = project_result.project->physics_constraint_edits.front();
        const auto authored_constraint = std::find_if(
            project_result.skeleton_data->physics_constraints().begin(),
            project_result.skeleton_data->physics_constraints().end(),
            [&](const marrow::runtime::PhysicsConstraintData& constraint) {
                return constraint.name == edit.name;
            });
        const auto exported_constraint = std::find_if(
            export_result.skeleton_data->physics_constraints().begin(),
            export_result.skeleton_data->physics_constraints().end(),
            [&](const marrow::runtime::PhysicsConstraintData& constraint) {
                return constraint.name == edit.name;
            });
        if (authored_constraint == project_result.skeleton_data->physics_constraints().end() ||
            exported_constraint == export_result.skeleton_data->physics_constraints().end() ||
            constraint_bone_names(project_result.skeleton_data, authored_constraint->bone_indices) !=
                constraint_bone_names(export_result.skeleton_data, exported_constraint->bone_indices) ||
            !require_near(exported_constraint->step, authored_constraint->step, "physics step") ||
            !require_near(exported_constraint->x, authored_constraint->x, "physics x") ||
            !require_near(exported_constraint->y, authored_constraint->y, "physics y") ||
            !require_near(exported_constraint->rotate, authored_constraint->rotate, "physics rotate") ||
            !require_near(exported_constraint->scale_x, authored_constraint->scale_x, "physics scaleX") ||
            !require_near(exported_constraint->shear_x, authored_constraint->shear_x, "physics shearX") ||
            !require_near(exported_constraint->limit, authored_constraint->limit, "physics limit") ||
            !require_near(exported_constraint->inertia, authored_constraint->inertia, "physics inertia") ||
            !require_near(exported_constraint->damping, authored_constraint->damping, "physics damping") ||
            !require_near(exported_constraint->strength, authored_constraint->strength, "physics strength") ||
            !require_near(exported_constraint->mass_inverse, authored_constraint->mass_inverse, "physics massInverse") ||
            !require_near(exported_constraint->gravity.x, authored_constraint->gravity.x, "physics gravity.x") ||
            !require_near(exported_constraint->gravity.y, authored_constraint->gravity.y, "physics gravity.y") ||
            !require_near(exported_constraint->wind.x, authored_constraint->wind.x, "physics wind.x") ||
            !require_near(exported_constraint->wind.y, authored_constraint->wind.y, "physics wind.y") ||
            !require_near(exported_constraint->mix, authored_constraint->mix, "physics mix")) {
            std::cerr << "Physics constraint export did not preserve the authored constraint edit.\n";
            return false;
        }
    }

    if (const auto body_slot_index = export_result.skeleton_data->find_slot_index("body")) {
        const auto* idle_animation = export_result.skeleton_data->find_animation("idle");
        if (idle_animation == nullptr) {
            std::cerr << "Runtime export validation could not resolve the idle animation.\n";
            return false;
        }

        marrow::runtime::Skeleton preview(export_result.skeleton_data);
        preview.set_skin("warrior");
        preview.apply_animation(*idle_animation, 0.75);
        const auto mesh_pose = preview.evaluate_current_mesh_attachment(*body_slot_index);
        const auto* offsets = preview.current_mesh_vertex_offsets(*body_slot_index);
        if (!mesh_pose.has_value() || mesh_pose->vertices.size() != 4U ||
            offsets == nullptr || offsets->size() != 8U) {
            std::cerr << "Runtime export validation could not replay the weighted mesh deform pose.\n";
            return false;
        }
    }

    std::cout << "Exported runtime skeleton: " << export_path.string() << '\n';
    return true;
}

bool validate_undo_redo_cycle(const marrow::editor::ProjectLoadResult& project_result) {
    if (project_result.project == nullptr ||
        project_result.base_skeleton_document == nullptr) {
        std::cerr << "EditorSession validation requires a loaded editor project.\n";
        return false;
    }

    marrow::editor::EditorSession session;
    const auto opened = session.open(project_result.project->source_path);
    if (!opened || session.project() == nullptr || session.runtime_data() == nullptr) {
        std::cerr << "EditorSession could not open the smoke project.\n";
        return false;
    }

    const std::string opened_snapshot =
        marrow::editor::serialize_project(*session.project());
    const std::string direct_load_snapshot =
        marrow::editor::serialize_project(*project_result.project);
    if (opened_snapshot != direct_load_snapshot) {
        std::cerr << "EditorSession changed the byte serialization of an unchanged .marrow project.\n";
        return false;
    }
    const auto failed_open = session.open(
        project_result.project->source_path.string() + ".missing-session-smoke");
    if (failed_open || session.project() == nullptr ||
        marrow::editor::serialize_project(*session.project()) != opened_snapshot) {
        std::cerr << "A failed EditorSession open replaced the active project.\n";
        return false;
    }

    if (!session.select_animation("idle") || !session.seek(0.2)) {
        std::cerr << "EditorSession could not prepare reload playback state.\n";
        return false;
    }
    session.set_playing(true);
    const auto reloaded = session.reload();
    if (!reloaded || !session.preview_state().playing ||
        session.preview_state().animation_name != "idle" ||
        std::abs(session.preview_state().time_seconds - 0.2) > 1e-9) {
        std::cerr << "EditorSession reload did not retain playback state.\n";
        return false;
    }
    const auto setup_probe_index = session.runtime_data()->find_bone_index("arm_l");
    if (!setup_probe_index.has_value() || !session.select_setup_pose() ||
        !session.preview_state().animation_name.empty() ||
        session.preview_state().playing ||
        session.preview_skeleton()->data().get() != session.runtime_data()) {
        std::cerr << "EditorSession could not select the setup-pose preview.\n";
        return false;
    }
    const auto& setup_probe =
        session.runtime_data()->bones()[*setup_probe_index].setup_pose;
    const auto& setup_preview =
        session.preview_skeleton()->bone_poses()[*setup_probe_index].local_pose;
    if (std::abs(setup_preview.x - setup_probe.x) > 1e-6 ||
        std::abs(setup_preview.y - setup_probe.y) > 1e-6 ||
        std::abs(setup_preview.rotation - setup_probe.rotation) > 1e-6 ||
        !session.select_animation("idle") || !session.seek(0.2)) {
        std::cerr << "EditorSession setup-pose preview was not immutable setup data.\n";
        return false;
    }
    session.set_playing(true);
    const std::uint64_t runtime_before_advance = session.runtime_revision();
    const std::uint64_t preview_before_advance = session.preview_revision();
    if (!session.advance(1.0 / 60.0) ||
        session.runtime_revision() != runtime_before_advance ||
        session.preview_revision() <= preview_before_advance ||
        session.preview_state().time_seconds <= 0.2) {
        std::cerr << "Incremental preview advancement rebuilt runtime data.\n";
        return false;
    }
    const marrow::runtime::RootMotionDelta root_motion_after_advance =
        session.preview_root_motion_total();
    if (!session.set_loop(false) ||
        std::abs(
            session.preview_root_motion_total().x - root_motion_after_advance.x) > 1e-9 ||
        std::abs(
            session.preview_root_motion_total().y - root_motion_after_advance.y) > 1e-9 ||
        !session.set_loop(true)) {
        std::cerr << "Pose-only preview refresh changed cumulative root motion.\n";
        return false;
    }
    session.set_playing(false);

    const std::string live_edit_baseline =
        marrow::editor::serialize_project(*session.project());
    const auto live_edit_runtime = session.preview_skeleton()->data();
    const auto live_edit_pose =
        session.preview_skeleton()->bone_poses()[*setup_probe_index].local_pose;
    const double live_edit_time = session.preview_state().time_seconds;
    const auto live_edit_motion = session.preview_root_motion_total();

    {
        auto live_cancel = session.begin_edit({
            marrow::editor::EditKind::AddKeyframe,
            "Live transform cancel smoke",
            {},
            false,
            marrow::editor::EditImpact::Project |
                marrow::editor::EditImpact::Runtime |
                marrow::editor::EditImpact::Preview});
        marrow::editor::upsert_transform_keyframe(
            *live_cancel.project(),
            *session.runtime_data(),
            "idle",
            "arm_l",
            marrow::editor::TransformTimelineChannel::Rotate,
            live_edit_time,
            marrow::editor::TransformKeyframePatch{
                static_cast<double>(live_edit_pose.rotation) + 7.0,
                std::nullopt,
                std::nullopt});
        const std::uint64_t runtime_before_refresh = session.runtime_revision();
        const std::uint64_t preview_before_refresh = session.preview_revision();
        const auto refreshed = live_cancel.refresh_runtime();
        if (!refreshed || !refreshed.changed || !session.transaction_active() ||
            session.runtime_revision() <= runtime_before_refresh ||
            session.preview_revision() <= preview_before_refresh ||
            session.preview_skeleton()->data().get() != session.runtime_data() ||
            session.preview_skeleton()->data().get() == live_edit_runtime.get() ||
            !session.dirty()) {
            std::cerr << "EditorSession live edit did not refresh runtime preview data.\n";
            return false;
        }
        live_cancel.cancel();
    }
    if (session.transaction_active() || session.can_undo() || session.dirty() ||
        session.preview_skeleton()->data().get() != live_edit_runtime.get() ||
        marrow::editor::serialize_project(*session.project()) != live_edit_baseline ||
        std::abs(
            session.preview_skeleton()
                    ->bone_poses()[*setup_probe_index]
                    .local_pose.rotation -
                live_edit_pose.rotation) > 1e-6 ||
        std::abs(session.preview_root_motion_total().x - live_edit_motion.x) > 1e-9 ||
        std::abs(session.preview_root_motion_total().y - live_edit_motion.y) > 1e-9) {
        std::cerr << "EditorSession live edit cancel did not restore its full snapshot.\n";
        return false;
    }

    const std::uint64_t project_before_live_commit = session.project_revision();
    {
        auto live_commit = session.begin_edit({
            marrow::editor::EditKind::AddKeyframe,
            "Live transform commit smoke",
            {},
            false,
            marrow::editor::EditImpact::Project |
                marrow::editor::EditImpact::Runtime |
                marrow::editor::EditImpact::Preview});
        marrow::editor::upsert_transform_keyframe(
            *live_commit.project(),
            *session.runtime_data(),
            "idle",
            "arm_l",
            marrow::editor::TransformTimelineChannel::Rotate,
            live_edit_time,
            marrow::editor::TransformKeyframePatch{
                static_cast<double>(live_edit_pose.rotation) + 9.0,
                std::nullopt,
                std::nullopt});
        if (!live_commit.refresh_runtime()) {
            std::cerr << "EditorSession could not refresh a committable live edit.\n";
            return false;
        }
        const std::uint64_t runtime_after_refresh = session.runtime_revision();
        const auto committed_live_edit = live_commit.commit();
        if (!committed_live_edit || !committed_live_edit.changed ||
            session.runtime_revision() != runtime_after_refresh ||
            session.project_revision() <= project_before_live_commit ||
            session.undo_count() != 1U) {
            std::cerr << "EditorSession live gesture did not commit as one history entry.\n";
            return false;
        }
    }
    if (!session.undo() || session.dirty() || session.can_undo() ||
        !session.can_redo() ||
        marrow::editor::serialize_project(*session.project()) != live_edit_baseline ||
        std::abs(session.preview_root_motion_total().x - live_edit_motion.x) > 1e-9 ||
        std::abs(session.preview_root_motion_total().y - live_edit_motion.y) > 1e-9) {
        std::cerr << "EditorSession could not undo a committed live gesture.\n";
        return false;
    }
    session.clear_history();

    const std::string baseline_snapshot =
        marrow::editor::serialize_project(*session.project());
    const std::uint64_t baseline_project_revision = session.project_revision();
    const std::uint64_t baseline_runtime_revision = session.runtime_revision();

    {
        auto no_change = session.begin_edit({
            marrow::editor::EditKind::EditProperty,
            "No-op edit",
            {},
            false,
            marrow::editor::EditImpact::Project});
        const auto result = no_change.commit();
        if (!result || result.changed || session.can_undo() || session.dirty() ||
            session.project_revision() != baseline_project_revision ||
            session.runtime_revision() != baseline_runtime_revision) {
            std::cerr << "EditorSession recorded or rebuilt a no-change edit.\n";
            return false;
        }
    }

    {
        auto cancelled = session.begin_edit({
            marrow::editor::EditKind::EditProperty,
            "Cancelled edit",
            {},
            false,
            marrow::editor::EditImpact::Project});
        cancelled.project()->editor_metadata.notes += " cancelled";
        const auto nested = session.begin_edit({
            marrow::editor::EditKind::EditProperty,
            "Nested edit",
            {},
            false,
            marrow::editor::EditImpact::Project});
        if (nested || !nested.error().has_value()) {
            std::cerr << "EditorSession allowed a nested edit transaction.\n";
            return false;
        }
        if (!cancelled.set_preview_skins({"warrior"}) ||
            cancelled.set_preview_attachment(
                std::numeric_limits<std::size_t>::max(),
                std::nullopt,
                "missing")) {
            std::cerr << "EditorSession transaction mutator validation was inconsistent.\n";
            return false;
        }
        const auto failed_commit = cancelled.commit();
        if (failed_commit || session.transaction_active()) {
            std::cerr << "A failed EditorSession commit left its transaction active.\n";
            return false;
        }
    }
    if (session.can_undo() || session.dirty() ||
        marrow::editor::serialize_project(*session.project()) != baseline_snapshot ||
        session.preview_state().skin_names != std::vector<std::string>{"default"}) {
        std::cerr << "EditorSession cancellation did not restore the project snapshot.\n";
        return false;
    }

    auto edit = session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "Update project notes",
        "project-notes",
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!edit || edit.project() == nullptr) {
        std::cerr << "EditorSession did not start a project edit transaction.\n";
        return false;
    }
    edit.project()->editor_metadata.notes +=
        edit.project()->editor_metadata.notes.empty()
            ? std::string("Undo/redo smoke edit.")
            : std::string(" [undo-redo smoke]");
    const auto committed = edit.commit();
    if (!committed || !committed.changed || !session.can_undo() || session.can_redo() ||
        !session.dirty() || session.project_revision() <= baseline_project_revision ||
        session.runtime_revision() <= baseline_runtime_revision ||
        std::abs(
            session.preview_root_motion_total().x - root_motion_after_advance.x) > 1e-9 ||
        std::abs(
            session.preview_root_motion_total().y - root_motion_after_advance.y) > 1e-9) {
        std::cerr << "EditorSession did not commit the project edit atomically.\n";
        return false;
    }
    const std::string edited_snapshot =
        marrow::editor::serialize_project(*session.project());

    const auto undone = session.undo();
    if (!undone || !undone.changed || session.can_undo() || !session.can_redo() ||
        session.dirty() ||
        marrow::editor::serialize_project(*session.project()) != baseline_snapshot) {
        std::cerr << "EditorSession undo did not restore the project baseline.\n";
        return false;
    }
    const auto redone = session.redo();
    if (!redone || !redone.changed || !session.can_undo() || session.can_redo() ||
        !session.dirty() ||
        marrow::editor::serialize_project(*session.project()) != edited_snapshot) {
        std::cerr << "EditorSession redo did not restore the edited project.\n";
        return false;
    }

    session.clear_history();
    const std::uint64_t preview_revision = session.preview_revision();
    const auto preview_edit = session.set_preview_skins({"warrior"});
    if (!preview_edit || !preview_edit.changed || !session.can_undo() ||
        session.preview_state().skin_names != std::vector<std::string>{"warrior"} ||
        session.preview_revision() <= preview_revision) {
        std::cerr << "EditorSession did not record the transient preview composition.\n";
        return false;
    }
    const bool dirty_before_preview_undo = session.dirty();
    if (!session.undo() || session.dirty() != dirty_before_preview_undo ||
        session.preview_state().skin_names != std::vector<std::string>{"default"}) {
        std::cerr << "Preview-only undo changed project dirtiness or restored the wrong skin.\n";
        return false;
    }

    session.clear_history();
    if (!session.select_animation("idle") || !session.seek(0.2)) {
        std::cerr << "EditorSession could not prepare playback-retention validation.\n";
        return false;
    }
    const double playback_time = session.preview_state().time_seconds;
    const std::string rollback_snapshot =
        marrow::editor::serialize_project(*session.project());
    auto invalid_edit = session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "Invalid runtime edit",
        {},
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (invalid_edit.project()->transform_timeline_edits.empty()) {
        std::cerr << "EditorSession rollback smoke requires a transform timeline edit.\n";
        return false;
    }
    invalid_edit.project()->transform_timeline_edits.front().bone_name =
        "__missing_session_smoke_bone__";
    const auto rejected = invalid_edit.commit();
    if (rejected || session.can_undo() ||
        marrow::editor::serialize_project(*session.project()) != rollback_snapshot ||
        std::abs(session.preview_state().time_seconds - playback_time) > 1e-9) {
        std::cerr << "EditorSession did not roll back a failed runtime rebuild.\n";
        return false;
    }

    for (int index = 0; index < 2; ++index) {
        auto merged_edit = session.begin_edit({
            marrow::editor::EditKind::EditProperty,
            "Merge notes edit",
            "merge-notes",
            true,
            marrow::editor::EditImpact::Project});
        merged_edit.project()->editor_metadata.notes += " m" + std::to_string(index);
        if (!merged_edit.commit()) {
            std::cerr << "EditorSession merge smoke edit failed.\n";
            return false;
        }
    }
    if (session.undo_count() != 1U) {
        std::cerr << "EditorSession did not merge compatible history entries.\n";
        return false;
    }

    session.clear_history();
    const std::string notes_before_noop_merge = session.project()->editor_metadata.notes;
    for (int index = 0; index < 2; ++index) {
        auto noop_merge = session.begin_edit({
            marrow::editor::EditKind::EditProperty,
            "No-op merge edit",
            "noop-merge",
            true,
            marrow::editor::EditImpact::Project});
        noop_merge.project()->editor_metadata.notes =
            index == 0 ? notes_before_noop_merge + " transient" : notes_before_noop_merge;
        if (!noop_merge.commit()) {
            std::cerr << "EditorSession no-op merge edit failed.\n";
            return false;
        }
    }
    if (session.can_undo()) {
        std::cerr << "EditorSession retained a merged history entry that returned to baseline.\n";
        return false;
    }

    session.clear_history();
    for (std::size_t index = 0; index < 101U; ++index) {
        auto depth_edit = session.begin_edit({
            marrow::editor::EditKind::EditProperty,
            "History depth edit",
            {},
            false,
            marrow::editor::EditImpact::Project});
        depth_edit.project()->editor_metadata.notes += " d" + std::to_string(index);
        if (!depth_edit.commit()) {
            std::cerr << "EditorSession history-depth edit failed.\n";
            return false;
        }
    }
    if (session.undo_count() != 100U || session.redo_count() != 0U) {
        std::cerr << "EditorSession did not enforce the 100-entry history cap.\n";
        return false;
    }

    struct TemporaryDirectoryCleanup {
        std::filesystem::path path;
        ~TemporaryDirectoryCleanup() {
            std::error_code ignored;
            std::filesystem::remove_all(path, ignored);
        }
    };
    const auto unique_suffix = std::chrono::steady_clock::now().time_since_epoch().count();
    TemporaryDirectoryCleanup temporary{
        std::filesystem::temp_directory_path() /
        ("marrow-session-smoke-" + std::to_string(unique_suffix))};
    const std::size_t history_before_export = session.undo_count();
    const bool dirty_before_export = session.dirty();
    marrow::editor::ProjectExportOptions export_options;
    export_options.skeleton_output_path = temporary.path / "session_export.mskl";
    export_options.binary_output_path = temporary.path / "session_export.mbin";
    const auto export_result = session.export_runtime(export_options);
    if (!export_result || !export_result.binary_path.has_value() ||
        !marrow::runtime::load_skeleton_data(export_result.path) ||
        !marrow::runtime::load_skeleton_data(*export_result.binary_path) ||
        session.undo_count() != history_before_export ||
        session.dirty() != dirty_before_export) {
        std::cerr << "EditorSession export changed authoring state or produced invalid runtime data.\n";
        return false;
    }

    const auto save_result = session.save(temporary.path / "session_project.marrow");
    if (!save_result || session.dirty() || session.undo_count() != history_before_export ||
        !marrow::runtime::json::load_document(save_result.project->source_path)) {
        std::cerr << "EditorSession save did not establish a clean saved baseline.\n";
        return false;
    }
    if (!session.undo() || !session.dirty() || !session.redo() || session.dirty() ||
        session.project()->source_path != save_result.project->source_path) {
        std::cerr << "EditorSession undo/redo did not track the saved dirty baseline.\n";
        return false;
    }

    session.clear_history();
    auto before_save_boundary = session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "Before save boundary",
        "save-boundary",
        true,
        marrow::editor::EditImpact::Project});
    before_save_boundary.project()->editor_metadata.notes += " before-save";
    if (!before_save_boundary.commit() || !session.save()) {
        std::cerr << "EditorSession could not establish a save merge boundary.\n";
        return false;
    }
    const std::string saved_boundary_notes = session.project()->editor_metadata.notes;
    auto after_save_boundary = session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "After save boundary",
        "save-boundary",
        true,
        marrow::editor::EditImpact::Project});
    after_save_boundary.project()->editor_metadata.notes += " after-save";
    if (!after_save_boundary.commit() || session.undo_count() != 2U ||
        !session.undo() ||
        session.project()->editor_metadata.notes != saved_boundary_notes) {
        std::cerr << "EditorSession merged history across a successful save.\n";
        return false;
    }

    if (!session.select_animation("idle") || !session.seek(0.35)) {
        std::cerr << "EditorSession could not prepare animation-selection transient validation.\n";
        return false;
    }
    const marrow::runtime::RootMotionDelta motion_before_selection =
        session.preview_root_motion_total();
    if ((std::abs(motion_before_selection.x) < 1e-9 &&
         std::abs(motion_before_selection.y) < 1e-9) ||
        !session.select_animation("attack", true) ||
        std::abs(session.preview_root_motion_delta().x) > 1e-9 ||
        std::abs(session.preview_root_motion_delta().y) > 1e-9 ||
        std::abs(session.preview_root_motion_total().x) > 1e-9 ||
        std::abs(session.preview_root_motion_total().y) > 1e-9 ||
        !session.preview_events().empty()) {
        std::cerr << "Animation selection retained stale preview events or root motion.\n";
        return false;
    }

    session.clear_history();
    constexpr std::string_view selected_rename_source = "aim";
    constexpr std::string_view selected_rename_target = "session_aim_renamed";
    constexpr double selected_rename_time = 0.2;
    if (!session.select_animation(selected_rename_source, true) ||
        !session.seek(selected_rename_time)) {
        std::cerr << "EditorSession could not prepare selected-animation rename.\n";
        return false;
    }
    session.set_playing(true);
    const auto rename_selected = session.edit_animation_catalog(
        {marrow::editor::AnimationCatalogEditKind::Rename,
         std::string(selected_rename_source),
         std::string(selected_rename_target)},
        {marrow::editor::EditKind::EditProperty,
         "Rename selected animation",
         "session-animation-catalog",
         false});
    const auto selected_rename_matches = [&](std::string_view name) {
        return session.preview_state().animation_name == name &&
            std::abs(session.preview_state().time_seconds - selected_rename_time) <= 1e-9 &&
            session.preview_state().playing;
    };
    if (!rename_selected || !rename_selected.changed ||
        !selected_rename_matches(selected_rename_target) ||
        session.runtime_data()->find_animation(selected_rename_source) != nullptr) {
        std::cerr << "Selected animation rename reset compatible playback state.\n";
        return false;
    }
    if (!session.undo() || !selected_rename_matches(selected_rename_source) ||
        !session.redo() || !selected_rename_matches(selected_rename_target) ||
        !session.undo() || !selected_rename_matches(selected_rename_source)) {
        std::cerr << "Selected animation rename history lost playback state.\n";
        return false;
    }
    session.clear_history();

    constexpr std::string_view renamed_queue_animation =
        "session_attack_queue_renamed";
    if (!session.select_animation("idle", true) ||
        !session.set_queue("attack", 0.15, 0.05)) {
        std::cerr << "EditorSession could not prepare an animation-catalog preview queue.\n";
        return false;
    }
    const auto rename_catalog = session.edit_animation_catalog(
        {marrow::editor::AnimationCatalogEditKind::Rename,
         "attack",
         std::string(renamed_queue_animation)},
        {marrow::editor::EditKind::EditProperty,
         "Rename queued animation",
         "session-animation-catalog",
         false});
    if (!rename_catalog || !rename_catalog.changed ||
        session.preview_state().animation_name != "idle" ||
        !session.preview_state().queue_enabled ||
        session.preview_state().queued_animation_name != renamed_queue_animation ||
        session.runtime_data()->find_animation("attack") != nullptr ||
        session.runtime_data()->find_animation(renamed_queue_animation) == nullptr) {
        std::cerr << "EditorSession did not atomically rename a queued animation.\n";
        return false;
    }
    if (!session.undo() ||
        session.preview_state().animation_name != "idle" ||
        !session.preview_state().queue_enabled ||
        session.preview_state().queued_animation_name != "attack" ||
        session.runtime_data()->find_animation("attack") == nullptr ||
        !session.redo() ||
        session.preview_state().queued_animation_name != renamed_queue_animation) {
        std::cerr << "EditorSession did not restore renamed queue references through history.\n";
        return false;
    }

    const auto delete_catalog = session.edit_animation_catalog(
        {marrow::editor::AnimationCatalogEditKind::Delete,
         std::string(renamed_queue_animation),
         {}},
        {marrow::editor::EditKind::EditProperty,
         "Delete queued animation",
         "session-animation-catalog",
         false});
    if (!delete_catalog || !delete_catalog.changed ||
        session.preview_state().animation_name != "idle" ||
        session.preview_state().queue_enabled ||
        session.runtime_data()->find_animation(renamed_queue_animation) != nullptr) {
        std::cerr << "EditorSession did not atomically remove a deleted preview queue.\n";
        return false;
    }
    if (!session.undo() ||
        session.preview_state().animation_name != "idle" ||
        !session.preview_state().queue_enabled ||
        session.preview_state().queued_animation_name != renamed_queue_animation ||
        session.runtime_data()->find_animation(renamed_queue_animation) == nullptr ||
        !session.redo() ||
        session.preview_state().queue_enabled ||
        session.runtime_data()->find_animation(renamed_queue_animation) != nullptr) {
        std::cerr << "EditorSession did not restore deleted queue references through history.\n";
        return false;
    }

    std::cout << "EditorSession transaction, rollback, preview, merge, and history validated.\n";
    return true;
}

bool validate_selection_reconciliation_transience(
    const marrow::editor::ProjectLoadResult& project_result) {
    using marrow::editor::AttachmentSelection;
    using marrow::editor::BoneSelection;
    using marrow::editor::ConstraintKind;
    using marrow::editor::ConstraintSelection;
    using marrow::editor::SelectionSet;
    using marrow::editor::SlotSelection;

    if (project_result.project == nullptr) {
        std::cerr << "Selection reconciliation requires a loaded editor project.\n";
        return false;
    }

    marrow::editor::EditorSession session;
    if (!session.open(project_result.project->source_path) ||
        session.project() == nullptr || session.runtime_data() == nullptr) {
        std::cerr << "Selection reconciliation session could not open the project.\n";
        return false;
    }

    SelectionSet selection;
    const BoneSelection arm{"arm_l"};
    const SlotSelection body{"body"};
    const AttachmentSelection mage_arm{"arm_l", "mage", "mage_arm_l"};
    const ConstraintSelection ik{ConstraintKind::Ik, "editor_arm_reach"};
    const AttachmentSelection missing{"arm_l", "warrior", "mage_arm_l"};
    if (!selection.add_range({arm, body, mage_arm, ik, missing}, missing)) {
        std::cerr << "Selection reconciliation could not prepare the external set.\n";
        return false;
    }

    const auto preview_edit = session.set_preview_skins({"warrior"});
    if (!preview_edit || !preview_edit.changed || !session.can_undo() ||
        !session.undo() || !session.can_redo()) {
        std::cerr << "Selection reconciliation could not prepare reload history semantics.\n";
        return false;
    }

    const std::string bytes_before_reload =
        marrow::editor::serialize_project(*session.project());
    const std::uint64_t project_revision_before_reload = session.project_revision();
    const std::uint64_t runtime_revision_before_reload = session.runtime_revision();
    const std::uint64_t preview_revision_before_reload = session.preview_revision();
    const auto reloaded = session.reload();
    if (!reloaded || session.project() == nullptr || session.runtime_data() == nullptr ||
        session.dirty() || session.undo_count() != 0U || session.redo_count() != 0U ||
        marrow::editor::serialize_project(*session.project()) != bytes_before_reload ||
        session.project_revision() != project_revision_before_reload + 1U ||
        session.runtime_revision() != runtime_revision_before_reload + 1U ||
        session.preview_revision() != preview_revision_before_reload + 1U) {
        std::cerr << "EditorSession reload semantics changed during selection validation.\n";
        return false;
    }

    const std::string bytes_before_reconcile =
        marrow::editor::serialize_project(*session.project());
    const bool dirty_before_reconcile = session.dirty();
    const std::size_t undo_before_reconcile = session.undo_count();
    const std::size_t redo_before_reconcile = session.redo_count();
    const std::uint64_t project_revision_before_reconcile = session.project_revision();
    const std::uint64_t runtime_revision_before_reconcile = session.runtime_revision();
    const std::uint64_t preview_revision_before_reconcile = session.preview_revision();

    if (!marrow::editor::reconcile_selection_to_runtime(
            selection, *session.runtime_data()) ||
        selection.items() != std::vector<marrow::editor::SelectionItem>{
            arm, body, mage_arm, ik} ||
        selection.active_constraint() == nullptr ||
        *selection.active_constraint() != ik ||
        marrow::editor::serialize_project(*session.project()) != bytes_before_reconcile ||
        session.dirty() != dirty_before_reconcile ||
        session.undo_count() != undo_before_reconcile ||
        session.redo_count() != redo_before_reconcile ||
        session.project_revision() != project_revision_before_reconcile ||
        session.runtime_revision() != runtime_revision_before_reconcile ||
        session.preview_revision() != preview_revision_before_reconcile) {
        std::cerr << "Selection reconciliation changed project/session state or lost survivors.\n";
        return false;
    }

    std::cout << "Transient selection reload reconciliation validated.\n";
    return true;
}

bool validate_exported_atlas_bundle(
    const marrow::editor::ProjectLoadResult& project_result,
    const marrow::editor::ProjectExportResult& export_result) {
    if (export_result.atlas_paths.size() != project_result.atlas_data.size()) {
        std::cerr << "Exported atlas count did not match the project runtime atlas count.\n";
        return false;
    }

    for (std::size_t atlas_index = 0; atlas_index < export_result.atlas_paths.size(); ++atlas_index) {
        const auto atlas_result =
            marrow::runtime::AtlasLoader::load(export_result.atlas_paths[atlas_index]);
        if (!atlas_result) {
            std::cerr << atlas_result.error->format();
            return false;
        }

        const auto& source_atlas = *project_result.atlas_data[atlas_index];
        const auto& exported_atlas = *atlas_result.atlas_data;
        if (source_atlas.info().name != exported_atlas.info().name ||
            !require_near(exported_atlas.info().width, source_atlas.info().width, "atlas width") ||
            !require_near(exported_atlas.info().height, source_atlas.info().height, "atlas height") ||
            source_atlas.regions().size() != exported_atlas.regions().size()) {
            std::cerr << "Exported atlas metadata did not preserve the source atlas model.\n";
            return false;
        }

        for (const auto& source_region : source_atlas.regions()) {
            const auto* exported_region = exported_atlas.find_region(source_region.name);
            if (exported_region == nullptr ||
                !require_near(exported_region->x, source_region.x, "atlas region x") ||
                !require_near(exported_region->y, source_region.y, "atlas region y") ||
                !require_near(exported_region->width, source_region.width, "atlas region width") ||
                !require_near(exported_region->height, source_region.height, "atlas region height") ||
                !require_near(exported_region->origin_x, source_region.origin_x, "atlas region origin_x") ||
                !require_near(exported_region->origin_y, source_region.origin_y, "atlas region origin_y") ||
                !require_near(
                    exported_region->rotate_degrees,
                    source_region.rotate_degrees,
                    "atlas region rotate")) {
                std::cerr << "Exported atlas region data did not preserve the source geometry.\n";
                return false;
            }
        }
    }

    for (const auto& texture_path : export_result.texture_paths) {
        if (!std::filesystem::exists(texture_path)) {
            std::cerr << "Exported texture asset is missing: " << texture_path.string() << '\n';
            return false;
        }
    }

    if (!export_result.atlas_paths.empty()) {
        std::cout << "Exported runtime atlases: " << join_paths(export_result.atlas_paths) << '\n';
    }
    return true;
}

bool validate_binary_export(
    const std::filesystem::path& exported_json_path,
    const std::filesystem::path& exported_binary_path) {
    const auto json_document_result = marrow::runtime::load_skeleton_document(exported_json_path);
    if (!json_document_result) {
        std::cerr << json_document_result.error->format();
        return false;
    }

    const auto binary_document_result = marrow::runtime::load_skeleton_document(exported_binary_path);
    if (!binary_document_result) {
        std::cerr << binary_document_result.error->format();
        return false;
    }

    if (const auto mismatch = compare_values(
            json_document_result.document->root,
            binary_document_result.document->root,
            "$")) {
        std::cerr << "Binary export diverged from the JSON export at " << *mismatch << '\n';
        return false;
    }

    marrow::runtime::SkeletonBinaryInspection inspection;
    if (const auto error = marrow::runtime::inspect_skeleton_binary(
            exported_binary_path,
            &inspection)) {
        std::cerr << error->format();
        return false;
    }
    if (!inspection.has_optimized_animation_section ||
        !inspection.keyframes_sorted_by_time_and_bone) {
        std::cerr << "Binary export did not produce a sorted optimized animation payload.\n";
        return false;
    }

    const auto json_runtime_result = marrow::runtime::load_skeleton_data(exported_json_path);
    const auto binary_runtime_result = marrow::runtime::load_skeleton_data(exported_binary_path);
    if (!json_runtime_result) {
        std::cerr << json_runtime_result.error->format();
        return false;
    }
    if (!binary_runtime_result) {
        std::cerr << binary_runtime_result.error->format();
        return false;
    }

    const auto comparison = marrow::runtime::compare_animation_roundtrip(
        *json_runtime_result.skeleton_data,
        *binary_runtime_result.skeleton_data);
    if (!comparison) {
        std::cerr << "Binary export runtime comparison failed: " << *comparison.error << '\n';
        return false;
    }
    if (comparison.metrics.max_rotation_error_degrees > 0.1 ||
        comparison.metrics.max_translation_error_pixels > 0.5) {
        std::cerr << "Binary export exceeded the quantized animation roundtrip tolerance.\n";
        return false;
    }

    std::cout << "Exported runtime binary: " << exported_binary_path.string() << '\n';
    std::cout << "Exported runtime binary errors: rotation="
              << comparison.metrics.max_rotation_error_degrees
              << "deg position=" << comparison.metrics.max_translation_error_pixels
              << "px\n";
    return true;
}

bool validate_animation_catalog_edits(const marrow::editor::ProjectLoadResult& project_result) {
    marrow::editor::ProjectData project = *project_result.project;
    const auto create_result = marrow::editor::create_animation(
        &project, *project_result.base_skeleton_document, "catalog_empty");
    const auto duplicate_result = marrow::editor::duplicate_animation(
        &project, *project_result.base_skeleton_document, "idle", "idle_copy");
    const auto rename_result = marrow::editor::rename_animation(
        &project, *project_result.base_skeleton_document, "aim", "focus");
    const auto delete_result = marrow::editor::delete_animation(
        &project, *project_result.base_skeleton_document, "attack");
    if (!create_result || !duplicate_result || !rename_result || !delete_result) {
        std::cerr << "Animation catalog authoring failed: "
                  << create_result.error << duplicate_result.error
                  << rename_result.error << delete_result.error << '\n';
        return false;
    }

    const auto runtime_result = marrow::editor::build_project_runtime(
        project, *project_result.base_skeleton_document);
    if (!runtime_result) {
        std::cerr << runtime_result.error->format();
        return false;
    }
    if (runtime_result.skeleton_data->find_animation("catalog_empty") == nullptr ||
        runtime_result.skeleton_data->find_animation("idle_copy") == nullptr ||
        runtime_result.skeleton_data->find_animation("focus") == nullptr ||
        runtime_result.skeleton_data->find_animation("aim") != nullptr ||
        runtime_result.skeleton_data->find_animation("attack") != nullptr) {
        std::cerr << "Animation catalog edits did not produce the expected runtime catalog.\n";
        return false;
    }
    const auto* source_idle = runtime_result.skeleton_data->find_animation("idle");
    const auto* copied_idle = runtime_result.skeleton_data->find_animation("idle_copy");
    if (source_idle == nullptr || copied_idle == nullptr ||
        !require_near(copied_idle->duration(), source_idle->duration(), "duplicated clip duration")) {
        return false;
    }

    const std::string serialized = marrow::editor::serialize_project(project);
    const auto parsed = marrow::runtime::json::parse_document(
        serialized, project_result.project->source_path);
    if (!parsed) {
        std::cerr << parsed.error->format();
        return false;
    }
    const auto reloaded = marrow::editor::load_project(*parsed.document);
    if (!reloaded || reloaded.project->animation_edits.size() != 4U ||
        reloaded.skeleton_data->find_animation("focus") == nullptr ||
        reloaded.skeleton_data->find_animation("attack") != nullptr) {
        std::cerr << "Animation catalog edits did not survive project serialization.\n";
        return false;
    }

    marrow::editor::ProjectData invalid_project = *project_result.project;
    marrow::editor::AnimationEdit invalid_rename;
    invalid_rename.kind = marrow::editor::AnimationEditKind::Rename;
    invalid_rename.name = "missing_source";
    invalid_rename.new_name = "invalid_target";
    invalid_project.animation_edits.push_back(std::move(invalid_rename));
    if (marrow::editor::build_project_runtime(
            invalid_project, *project_result.base_skeleton_document)) {
        std::cerr << "Animation catalog accepted a missing rename source.\n";
        return false;
    }

    marrow::runtime::json::Document future_base = *project_result.base_skeleton_document;
    auto* future_animations = marrow::runtime::json::find_member(
        future_base.root, "animations");
    auto* future_idle = future_animations != nullptr
        ? marrow::runtime::json::find_member(*future_animations, "idle")
        : nullptr;
    if (future_idle == nullptr || !future_idle->is_object()) {
        std::cerr << "Animation catalog could not prepare unknown-field coverage.\n";
        return false;
    }
    future_idle->as_object().emplace(
        "futureTimelineFamily",
        marrow::runtime::json::Value(
            marrow::runtime::json::Value::Object{
                {"sentinel", marrow::runtime::json::Value(7.0, {})}},
            {}));
    marrow::editor::ProjectData future_project = *project_result.project;
    const auto future_duplicate = marrow::editor::duplicate_animation(
        &future_project, future_base, "idle", "future_copy");
    const auto future_document = marrow::editor::build_project_runtime_document(
        future_project, future_base);
    const auto* future_output_animations = marrow::runtime::json::find_member(
        future_document.root, "animations");
    const auto* future_copy = future_output_animations != nullptr
        ? marrow::runtime::json::find_member(*future_output_animations, "future_copy")
        : nullptr;
    if (!future_duplicate || future_copy == nullptr || !future_copy->is_object() ||
        marrow::runtime::json::find_member(*future_copy, "futureTimelineFamily") == nullptr) {
        std::cerr << "Animation duplicate did not preserve an unknown timeline family.\n";
        return false;
    }

    marrow::editor::ProjectData single_animation = project;
    for (const auto& animation : runtime_result.skeleton_data->animations()) {
        if (animation.name == "idle") {
            continue;
        }
        const auto removed = marrow::editor::delete_animation(
            &single_animation,
            *project_result.base_skeleton_document,
            animation.name);
        if (!removed) {
            std::cerr << removed.error << '\n';
            return false;
        }
    }
    const auto rejected = marrow::editor::delete_animation(
        &single_animation, *project_result.base_skeleton_document, "idle");
    if (rejected || rejected.error != "The last animation cannot be deleted.") {
        std::cerr << "Animation catalog allowed deleting the last clip.\n";
        return false;
    }

    std::cout << "Animation catalog create/duplicate/rename/delete validated.\n";
    return true;
}

bool validate_editing_p1_animation_duration(
    const marrow::editor::ProjectLoadResult& project_result) {
    using marrow::editor::AnimationEdit;
    using marrow::editor::AnimationEditKind;
    using marrow::editor::EditImpact;
    using marrow::editor::EditKind;
    using marrow::editor::EditorSession;
    using marrow::editor::ProjectData;
    using marrow::editor::TransformTimelineChannel;

    if (project_result.project == nullptr ||
        project_result.base_skeleton_document == nullptr ||
        project_result.skeleton_data == nullptr) {
        std::cerr << "MAR-155 duration validation requires a loaded project.\n";
        return false;
    }

    constexpr double kTolerance = 1e-6;
    const auto near = [=](double left, double right) {
        return std::abs(left - right) <= kTolerance;
    };
    const auto runtime_duration = [&](const EditorSession& session) {
        const auto* animation = session.runtime_data() != nullptr
            ? session.runtime_data()->find_animation("aim")
            : nullptr;
        return animation != nullptr
            ? animation->duration()
            : std::numeric_limits<double>::quiet_NaN();
    };
    const auto runtime_explicit_duration = [&](const EditorSession& session) {
        const auto* animation = session.runtime_data() != nullptr
            ? session.runtime_data()->find_animation("aim")
            : nullptr;
        return animation != nullptr
            ? animation->explicit_duration
            : std::optional<double>{};
    };
    const auto has_aim_rotate_key = [&](const ProjectData& project, double time) {
        const auto* edit = project.find_transform_timeline_edit(
            "aim", "arm_l", TransformTimelineChannel::Rotate);
        return edit != nullptr && std::any_of(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            [&](const auto& keyframe) {
                return std::abs(keyframe.time - time) <= kTolerance;
            });
    };

    const std::filesystem::path project_path =
        "/tmp/marrow_mar155_duration.marrow";
    const std::filesystem::path json_path =
        "/tmp/marrow_mar155_duration.mskl";
    const std::filesystem::path binary_path =
        "/tmp/marrow_mar155_duration.mbin";

    ProjectData seeded_project = *project_result.project;
    seeded_project.runtime_assets.skeleton_path =
        std::filesystem::absolute(seeded_project.resolved_skeleton_path());
    seeded_project.runtime_assets.atlas_paths = seeded_project.resolved_atlas_paths();
    for (auto& atlas_path : seeded_project.runtime_assets.atlas_paths) {
        atlas_path = std::filesystem::absolute(atlas_path);
    }
    seeded_project.source_path = project_path;
    seeded_project.animation_edits.clear();

    const auto unknown_source = marrow::runtime::json::parse_document(
        R"json({"op":"future_duration_operation","sentinel":17})json");
    const auto duration_source = marrow::runtime::json::parse_document(
        R"json({"op":"set_duration","name":"aim","duration":0.6,"future_additive":"keep-duration-field"})json");
    if (!unknown_source || !duration_source) {
        std::cerr << "MAR-155 could not prepare additive animation edit fixtures.\n";
        return false;
    }

    AnimationEdit unknown_edit;
    unknown_edit.kind = AnimationEditKind::Unknown;
    unknown_edit.preserved_source = unknown_source.document->root;
    seeded_project.animation_edits.push_back(std::move(unknown_edit));

    AnimationEdit duration_edit;
    duration_edit.kind = AnimationEditKind::SetDuration;
    duration_edit.name = "aim";
    duration_edit.duration = 0.6;
    duration_edit.preserved_source = duration_source.document->root;
    seeded_project.animation_edits.push_back(std::move(duration_edit));

    const auto seeded_save = marrow::editor::save_project(seeded_project, project_path);
    if (!seeded_save) {
        std::cerr << seeded_save.error->format() << '\n';
        return false;
    }

    EditorSession session;
    if (!session.open(project_path) || !session.select_animation("aim") ||
        !session.seek(0.4)) {
        std::cerr << "MAR-155 could not open and preview the duration fixture.\n";
        return false;
    }
    session.set_playing(false);
    const auto initial_explicit_duration = runtime_explicit_duration(session);
    if (!initial_explicit_duration.has_value() ||
        !near(*initial_explicit_duration, 0.6) ||
        !near(runtime_duration(session), 0.6) || session.dirty() ||
        session.undo_count() != 0U) {
        std::cerr << "MAR-155 seeded explicit duration did not load cleanly.\n";
        return false;
    }

    const std::string manual_before =
        marrow::editor::serialize_project(*session.project());
    const std::size_t manual_history_before = session.undo_count();
    {
        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            "MAR-155 live duration",
            "animation-duration:aim",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        const auto authored = marrow::editor::set_animation_duration(
            transaction.project(), *session.runtime_data(), "aim", 0.75);
        const auto refreshed = authored ? transaction.refresh_runtime()
                                        : marrow::editor::SessionResult{};
        if (!authored || !authored.changed || !refreshed || !refreshed.changed ||
            !session.transaction_active() || !session.dirty() ||
            !near(runtime_duration(session), 0.75) ||
            session.preview_state().animation_name != "aim" ||
            !near(session.preview_state().time_seconds, 0.4)) {
            transaction.cancel();
            std::cerr << "MAR-155 live duration did not refresh preview atomically.\n";
            return false;
        }
        const auto committed = transaction.commit();
        if (!committed || !committed.changed) {
            std::cerr << "MAR-155 live duration did not commit.\n";
            return false;
        }
    }
    const std::string manual_after =
        marrow::editor::serialize_project(*session.project());
    if (session.undo_count() != manual_history_before + 1U ||
        !near(runtime_duration(session), 0.75) || !session.dirty()) {
        std::cerr << "MAR-155 live duration did not create one dirty history item.\n";
        return false;
    }
    if (!session.undo() ||
        marrow::editor::serialize_project(*session.project()) != manual_before ||
        !near(runtime_duration(session), 0.6) || !session.redo() ||
        marrow::editor::serialize_project(*session.project()) != manual_after ||
        !near(runtime_duration(session), 0.75)) {
        std::cerr << "MAR-155 duration undo/redo did not restore exact snapshots.\n";
        return false;
    }

    const std::string rejected_project =
        marrow::editor::serialize_project(*session.project());
    const auto* rejected_runtime = session.runtime_data();
    const auto rejected_preview = session.preview_state();
    const auto rejected_motion = session.preview_root_motion_total();
    const std::size_t rejected_undo_count = session.undo_count();
    const std::size_t rejected_redo_count = session.redo_count();
    const std::uint64_t rejected_project_revision = session.project_revision();
    const std::uint64_t rejected_runtime_revision = session.runtime_revision();
    const std::uint64_t rejected_preview_revision = session.preview_revision();
    const bool rejected_dirty = session.dirty();
    {
        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            "MAR-155 rejected duration",
            "animation-duration:aim",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        const auto rejected = marrow::editor::set_animation_duration(
            transaction.project(), *session.runtime_data(), "aim", 0.25);
        if (rejected || rejected.error.empty()) {
            transaction.cancel();
            std::cerr << "MAR-155 accepted a duration shorter than the last key.\n";
            return false;
        }
        transaction.cancel();
    }
    if (session.transaction_active() || session.runtime_data() != rejected_runtime ||
        marrow::editor::serialize_project(*session.project()) != rejected_project ||
        session.undo_count() != rejected_undo_count ||
        session.redo_count() != rejected_redo_count ||
        session.project_revision() != rejected_project_revision ||
        session.runtime_revision() != rejected_runtime_revision ||
        session.preview_revision() != rejected_preview_revision ||
        session.dirty() != rejected_dirty ||
        session.preview_state().animation_name != rejected_preview.animation_name ||
        !near(session.preview_state().time_seconds, rejected_preview.time_seconds) ||
        session.preview_state().playing != rejected_preview.playing ||
        session.preview_state().loop != rejected_preview.loop ||
        !near(session.preview_root_motion_total().x, rejected_motion.x) ||
        !near(session.preview_root_motion_total().y, rejected_motion.y)) {
        std::cerr << "MAR-155 rejected shrink changed project, preview, or history.\n";
        return false;
    }

    const std::size_t create_history_before = session.undo_count();
    {
        auto transaction = session.begin_edit({
            EditKind::AddKeyframe,
            "MAR-155 duration auto-grow create",
            "timeline:aim:arm_l:rotate",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        marrow::editor::upsert_transform_keyframe(
            *transaction.project(),
            *session.runtime_data(),
            "aim",
            "arm_l",
            TransformTimelineChannel::Rotate,
            1.0,
            marrow::editor::TransformKeyframePatch{
                45.0, std::nullopt, std::nullopt});
        const auto committed = transaction.commit();
        if (!committed || !committed.changed) {
            std::cerr << "MAR-155 key creation did not commit with duration growth.\n";
            return false;
        }
    }
    if (session.undo_count() != create_history_before + 1U ||
        !near(runtime_duration(session), 1.0) ||
        !has_aim_rotate_key(*session.project(), 1.0)) {
        std::cerr << "MAR-155 key creation did not auto-grow explicit duration.\n";
        return false;
    }
    if (!session.undo() || !near(runtime_duration(session), 0.75) ||
        has_aim_rotate_key(*session.project(), 1.0) || !session.redo() ||
        !near(runtime_duration(session), 1.0) ||
        !has_aim_rotate_key(*session.project(), 1.0)) {
        std::cerr << "MAR-155 key-create undo did not include duration auto-grow.\n";
        return false;
    }

    marrow::editor::TimelineKeySelector key_selector;
    key_selector.kind = marrow::editor::TimelineKeyKind::Transform;
    key_selector.animation_name = "aim";
    key_selector.bone_name = "arm_l";
    key_selector.transform_channel = TransformTimelineChannel::Rotate;
    key_selector.time = 1.0;
    const std::size_t move_history_before = session.undo_count();
    {
        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            "MAR-155 duration auto-grow move",
            "timeline:retime",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        const auto moved = marrow::editor::retime_keyframes(
            transaction.project(), {key_selector}, 0.2, false, 60.0);
        const auto committed = moved ? transaction.commit()
                                     : marrow::editor::SessionResult{};
        if (!moved || !moved.changed || !committed || !committed.changed) {
            std::cerr << "MAR-155 rightward key move did not commit.\n";
            return false;
        }
    }
    const double moved_time = 1.2;
    if (session.undo_count() != move_history_before + 1U ||
        !near(runtime_duration(session), moved_time) ||
        !has_aim_rotate_key(*session.project(), moved_time)) {
        std::cerr << "MAR-155 rightward key move did not grow duration.\n";
        return false;
    }
    if (!session.undo() || !near(runtime_duration(session), 1.0) ||
        !has_aim_rotate_key(*session.project(), 1.0) || !session.redo() ||
        !near(runtime_duration(session), moved_time) ||
        !has_aim_rotate_key(*session.project(), moved_time)) {
        std::cerr << "MAR-155 key-move undo did not include duration auto-grow.\n";
        return false;
    }

    key_selector.time = moved_time;
    {
        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            "MAR-155 duration no-shrink left move",
            "timeline:retime",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        const auto moved = marrow::editor::retime_keyframes(
            transaction.project(), {key_selector}, -0.3, false, 60.0);
        const auto committed = moved ? transaction.commit()
                                     : marrow::editor::SessionResult{};
        if (!moved || !committed || !committed.changed) {
            std::cerr << "MAR-155 leftward key move did not commit.\n";
            return false;
        }
    }
    const double left_time = 0.9;
    if (!near(runtime_duration(session), moved_time) ||
        !has_aim_rotate_key(*session.project(), left_time)) {
        std::cerr << "MAR-155 leftward key move auto-shrank duration.\n";
        return false;
    }

    {
        auto transaction = session.begin_edit({
            EditKind::RemoveKeyframe,
            "MAR-155 duration no-shrink delete",
            "timeline:aim:arm_l:rotate",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        auto* edit = transaction.project()->find_transform_timeline_edit(
            "aim", "arm_l", TransformTimelineChannel::Rotate);
        if (edit == nullptr) {
            transaction.cancel();
            std::cerr << "MAR-155 could not find the moved key for deletion.\n";
            return false;
        }
        const auto key = marrow::editor::find_keyframe_near_time(
            edit->keyframes, left_time, kTolerance);
        if (key == edit->keyframes.end()) {
            transaction.cancel();
            std::cerr << "MAR-155 could not resolve the moved key for deletion.\n";
            return false;
        }
        edit->keyframes.erase(key);
        const auto committed = transaction.commit();
        if (!committed || !committed.changed) {
            std::cerr << "MAR-155 key deletion did not commit.\n";
            return false;
        }
    }
    if (!near(runtime_duration(session), moved_time) ||
        has_aim_rotate_key(*session.project(), left_time)) {
        std::cerr << "MAR-155 key deletion auto-shrank explicit duration.\n";
        return false;
    }

    const auto saved = session.save(project_path);
    if (!saved || session.dirty()) {
        std::cerr << "MAR-155 duration project did not save cleanly.\n";
        return false;
    }
    const auto reloaded = marrow::editor::load_project(project_path);
    const auto* reloaded_aim = reloaded
        ? reloaded.skeleton_data->find_animation("aim")
        : nullptr;
    if (!reloaded || reloaded_aim == nullptr ||
        !reloaded_aim->explicit_duration.has_value() ||
        !near(*reloaded_aim->explicit_duration, moved_time) ||
        !near(reloaded_aim->duration(), moved_time) ||
        !near(reloaded_aim->inferred_duration(), 0.5) ||
        reloaded.project->animation_edits.size() != 2U ||
        reloaded.project->animation_edits[0].kind != AnimationEditKind::Unknown ||
        reloaded.project->animation_edits[1].kind != AnimationEditKind::SetDuration ||
        !near(reloaded.project->animation_edits[1].duration, moved_time)) {
        std::cerr << "MAR-155 duration edits did not survive save/reload in order.\n";
        return false;
    }

    const auto saved_document = marrow::runtime::json::load_document(project_path);
    const auto* saved_edits = saved_document
        ? marrow::runtime::json::find_member(
              saved_document.document->root, "animation_edits")
        : nullptr;
    if (!saved_document || saved_edits == nullptr || !saved_edits->is_array() ||
        saved_edits->as_array().size() != 2U) {
        std::cerr << "MAR-155 saved project lost the ordered animation edit log.\n";
        return false;
    }
    const auto* unknown_operation = marrow::runtime::json::find_member(
        saved_edits->as_array()[0], "op");
    const auto* unknown_sentinel = marrow::runtime::json::find_member(
        saved_edits->as_array()[0], "sentinel");
    const auto* duration_operation = marrow::runtime::json::find_member(
        saved_edits->as_array()[1], "op");
    const auto* duration_additive = marrow::runtime::json::find_member(
        saved_edits->as_array()[1], "future_additive");
    const auto* saved_duration = marrow::runtime::json::find_member(
        saved_edits->as_array()[1], "duration");
    if (unknown_operation == nullptr || !unknown_operation->is_string() ||
        unknown_operation->as_string() != "future_duration_operation" ||
        unknown_sentinel == nullptr || !unknown_sentinel->is_number() ||
        unknown_sentinel->as_number() != 17.0 ||
        duration_operation == nullptr || !duration_operation->is_string() ||
        duration_operation->as_string() != "set_duration" ||
        duration_additive == nullptr || !duration_additive->is_string() ||
        duration_additive->as_string() != "keep-duration-field" ||
        saved_duration == nullptr || !saved_duration->is_number() ||
        !near(saved_duration->as_number(), moved_time)) {
        std::cerr << "MAR-155 did not preserve unknown/additive animation edit fields.\n";
        return false;
    }

    marrow::editor::ProjectExportOptions export_options;
    export_options.skeleton_output_path = json_path;
    export_options.binary_output_path = binary_path;
    const auto exported = session.export_runtime(export_options);
    if (!exported) {
        std::cerr << exported.error->format() << '\n';
        return false;
    }
    const auto json_runtime = marrow::runtime::load_skeleton_data(json_path);
    const auto binary_runtime = marrow::runtime::load_skeleton_data(binary_path);
    const auto* json_aim = json_runtime
        ? json_runtime.skeleton_data->find_animation("aim")
        : nullptr;
    const auto* binary_aim = binary_runtime
        ? binary_runtime.skeleton_data->find_animation("aim")
        : nullptr;
    if (!json_runtime || !binary_runtime || json_aim == nullptr ||
        binary_aim == nullptr || !json_aim->explicit_duration.has_value() ||
        !binary_aim->explicit_duration.has_value() ||
        !near(*json_aim->explicit_duration, moved_time) ||
        !near(*binary_aim->explicit_duration, moved_time) ||
        !near(json_aim->duration(), moved_time) ||
        !near(binary_aim->duration(), moved_time)) {
        std::cerr << "MAR-155 JSON/MBIN export lost explicit duration presence or value.\n";
        return false;
    }
    const auto comparison = marrow::runtime::compare_animation_roundtrip(
        *json_runtime.skeleton_data, *binary_runtime.skeleton_data);
    if (!comparison) {
        std::cerr << "MAR-155 JSON/MBIN duration comparison failed: "
                  << *comparison.error << '\n';
        return false;
    }

    std::cout << "Editing P1 duration authoring/rollback/auto-grow/export validated.\n";
    return true;
}

// MAR-168: the shared scalar-offset primitive that the Graph value drag writes
// through. Every case runs on an isolated ProjectData so a rejection can be
// proven byte-atomic against `serialize_project`.
bool validate_mar169_graph_interpolation_authoring(
    const marrow::editor::ProjectLoadResult& project_result) {
    using marrow::editor::TimelineKeyKind;
    using marrow::editor::TimelineKeySelector;
    using marrow::editor::TransformTimelineChannel;
    using marrow::runtime::InterpolationKind;

    constexpr double kExact = 1e-12;
    constexpr double kFloatTolerance = 1e-6;
    const auto near_float = [](double left, double right) {
        return std::abs(left - right) <= kFloatTolerance;
    };
    const auto narrowed = [](double value) {
        return static_cast<double>(
            static_cast<marrow::runtime::AnimationScalar>(value));
    };
    const auto control_points_match = [&](const marrow::runtime::Interpolation& easing,
                                          const std::array<double, 4>& expected) {
        if (easing.kind() != InterpolationKind::CubicBezier) return false;
        const auto& points = easing.cubic_bezier();
        return static_cast<double>(points.cx1) == narrowed(expected[0]) &&
            static_cast<double>(points.cy1) == narrowed(expected[1]) &&
            static_cast<double>(points.cx2) == narrowed(expected[2]) &&
            static_cast<double>(points.cy2) == narrowed(expected[3]);
    };

    const auto make_transform_track = [](std::string bone,
                                         TransformTimelineChannel channel,
                                         std::vector<marrow::editor::TransformKeyframeEdit> keys) {
        marrow::editor::TransformTimelineEdit edit;
        edit.animation_name = "mar169";
        edit.bone_name = std::move(bone);
        edit.channel = channel;
        edit.keyframes = std::move(keys);
        return edit;
    };

    const auto build_project = [&]() {
        marrow::editor::ProjectData project;
        project.transform_timeline_edits.push_back(make_transform_track(
            "spine",
            TransformTimelineChannel::Rotate,
            {{0.0, 10.0, 0.0, 0.0,
              marrow::runtime::Interpolation::cubic_bezier(0.25, 0.1, 0.75, 0.9)},
             {0.5, 20.0, 0.0, 0.0, marrow::runtime::Interpolation::stepped()}}));
        project.transform_timeline_edits.push_back(make_transform_track(
            "spine",
            TransformTimelineChannel::Translate,
            {{0.0, 0.0, 3.0, 7.0, marrow::runtime::Interpolation::linear()},
             {0.5, 0.0, 9.0, -2.0, marrow::runtime::Interpolation::stepped()}}));
        project.transform_timeline_edits.push_back(make_transform_track(
            "spine",
            TransformTimelineChannel::Scale,
            {{0.0, 0.0, -1.25, 2.0, marrow::runtime::Interpolation::linear()},
             {0.5, 0.0, 0.5, 1.0, marrow::runtime::Interpolation::stepped()}}));
        project.transform_timeline_edits.push_back(make_transform_track(
            "spine",
            TransformTimelineChannel::Shear,
            {{0.0, 0.0, 4.0, -6.0, marrow::runtime::Interpolation::linear()},
             {0.5, 0.0, 8.0, 12.0, marrow::runtime::Interpolation::linear()}}));

        marrow::editor::SlotColorTimelineEdit color;
        color.animation_name = "mar169";
        color.slot_name = "body";
        color.keyframes.push_back(
            {0.0, marrow::runtime::SlotColor{0.25, 0.5, 0.75, 0.4},
             marrow::runtime::Interpolation::linear()});
        color.keyframes.push_back(
            {0.5, marrow::runtime::SlotColor{0.5, 0.25, 0.125, 0.9},
             marrow::runtime::Interpolation::stepped()});
        project.slot_color_timeline_edits.push_back(std::move(color));

        marrow::editor::MeshDeformTimelineEdit deform;
        deform.animation_name = "mar169";
        deform.slot_name = "body";
        deform.attachment_name = "body_mesh";
        deform.keyframes.push_back(
            {0.0, {0.0, 0.0, 1.0, 2.0}, marrow::runtime::Interpolation::linear()});
        project.mesh_deform_timeline_edits.push_back(std::move(deform));

        marrow::editor::DrawOrderTimelineEdit draw_order;
        draw_order.animation_name = "mar169";
        draw_order.keyframes.push_back({0.0, {"body", "arm_l"}});
        project.draw_order_timeline_edits.push_back(std::move(draw_order));

        marrow::editor::EventTimelineEdit events;
        events.animation_name = "mar169";
        events.keyframes.push_back(
            {0.0, "footstep", std::nullopt, std::nullopt, std::nullopt,
             std::nullopt, std::nullopt, std::nullopt});
        project.event_timeline_edits.push_back(std::move(events));

        marrow::editor::SlotAttachmentTimelineEdit attachment;
        attachment.animation_name = "mar169";
        attachment.slot_name = "body";
        attachment.keyframes.push_back({0.0, std::string("body")});
        project.slot_attachment_timeline_edits.push_back(std::move(attachment));
        return project;
    };

    const auto transform_selector = [](std::string bone,
                                       TransformTimelineChannel channel,
                                       double time) {
        TimelineKeySelector selector;
        selector.kind = TimelineKeyKind::Transform;
        selector.animation_name = "mar169";
        selector.bone_name = std::move(bone);
        selector.transform_channel = channel;
        selector.time = time;
        return selector;
    };
    const auto color_selector = [](double time) {
        TimelineKeySelector selector;
        selector.kind = TimelineKeyKind::SlotColor;
        selector.animation_name = "mar169";
        selector.slot_name = "body";
        selector.time = time;
        return selector;
    };
    const auto deform_selector = [](double time) {
        TimelineKeySelector selector;
        selector.kind = TimelineKeyKind::Deform;
        selector.animation_name = "mar169";
        selector.slot_name = "body";
        selector.attachment_name = "body_mesh";
        selector.time = time;
        return selector;
    };

    constexpr std::array<double, 4> kOvershoot{0.2, -0.4, 0.8, 1.6};
    // The unique evenly spaced cubic that is exactly identical to Linear.
    constexpr std::array<double, 4> kLinearEquivalentSeed{
        1.0 / 3.0, 1.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0};

    // --- One case per supported Transform family: the curve lands and every
    // other field of the parent key is byte-identical. ---
    {
        const TransformTimelineChannel channels[] = {
            TransformTimelineChannel::Rotate,
            TransformTimelineChannel::Translate,
            TransformTimelineChannel::Scale,
            TransformTimelineChannel::Shear,
        };
        for (const TransformTimelineChannel channel : channels) {
            marrow::editor::ProjectData project = build_project();
            const auto* source =
                project.find_transform_timeline_edit("mar169", "spine", channel);
            if (source == nullptr || source->keyframes.empty()) {
                std::cerr << "MAR-169 is missing a transform source timeline.\n";
                return false;
            }
            const marrow::editor::TransformKeyframeEdit original =
                source->keyframes.front();
            const std::size_t original_count = source->keyframes.size();
            const auto set = marrow::editor::set_keyframe_interpolation(
                &project,
                {transform_selector("spine", channel, 0.0)},
                InterpolationKind::CubicBezier,
                kOvershoot);
            if (!set || !set.changed || set.key_count != 1U ||
                set.changed_key_count != 1U) {
                std::cerr << "MAR-169 could not author a transform curve: "
                          << set.error << '\n';
                return false;
            }
            const auto* edited =
                project.find_transform_timeline_edit("mar169", "spine", channel);
            if (edited == nullptr || edited->keyframes.size() != original_count) {
                std::cerr << "MAR-169 reshaped a transform timeline.\n";
                return false;
            }
            const marrow::editor::TransformKeyframeEdit& key = edited->keyframes.front();
            if (key.time != original.time || key.angle != original.angle ||
                key.x != original.x || key.y != original.y ||
                !control_points_match(key.interpolation, kOvershoot)) {
                std::cerr << "MAR-169 interpolation write did not preserve the parent key.\n";
                return false;
            }
        }
    }

    // --- Slot Color and Deform families. ---
    {
        marrow::editor::ProjectData project = build_project();
        const auto* color_source = project.find_slot_color_timeline_edit("mar169", "body");
        const auto* deform_source =
            project.find_mesh_deform_timeline_edit("mar169", "body", "body_mesh");
        if (color_source == nullptr || color_source->keyframes.empty() ||
            deform_source == nullptr || deform_source->keyframes.empty()) {
            std::cerr << "MAR-169 is missing its colour or deform source timeline.\n";
            return false;
        }
        const marrow::editor::SlotColorKeyframeEdit original_color =
            color_source->keyframes.front();
        const marrow::editor::DeformKeyframeEdit original_deform =
            deform_source->keyframes.front();
        const auto set = marrow::editor::set_keyframe_interpolation(
            &project,
            {color_selector(0.0), deform_selector(0.0)},
            InterpolationKind::CubicBezier,
            kOvershoot);
        const auto* color_edited = project.find_slot_color_timeline_edit("mar169", "body");
        const auto* deform_edited =
            project.find_mesh_deform_timeline_edit("mar169", "body", "body_mesh");
        if (!set || !set.changed || set.key_count != 2U || set.changed_key_count != 2U ||
            color_edited == nullptr || deform_edited == nullptr ||
            color_edited->keyframes.front().time != original_color.time ||
            color_edited->keyframes.front().color.r != original_color.color.r ||
            color_edited->keyframes.front().color.g != original_color.color.g ||
            color_edited->keyframes.front().color.b != original_color.color.b ||
            color_edited->keyframes.front().color.a != original_color.color.a ||
            !control_points_match(color_edited->keyframes.front().interpolation, kOvershoot) ||
            deform_edited->keyframes.front().time != original_deform.time ||
            deform_edited->keyframes.front().vertex_offsets !=
                original_deform.vertex_offsets ||
            !control_points_match(deform_edited->keyframes.front().interpolation, kOvershoot)) {
            std::cerr << "MAR-169 could not author a slot-colour or deform curve: "
                      << set.error << '\n';
            return false;
        }
    }

    // --- Segment-wide identity: exactly one `interpolation` field in the
    // whole project moves, and the key's own X and Y are untouched. The easing
    // is a property of the parent key, so `set_keyframe_interpolation()` has
    // no component argument to write through. ---
    {
        marrow::editor::ProjectData project = build_project();
        const marrow::editor::ProjectData before = project;
        const std::string before_text = marrow::editor::serialize_project(project);
        const auto* source = project.find_transform_timeline_edit(
            "mar169", "spine", TransformTimelineChannel::Translate);
        if (source == nullptr || source->keyframes.empty()) return false;
        const marrow::editor::TransformKeyframeEdit original = source->keyframes.front();
        const auto set = marrow::editor::set_keyframe_interpolation(
            &project,
            {transform_selector("spine", TransformTimelineChannel::Translate, 0.0)},
            InterpolationKind::CubicBezier,
            kOvershoot);
        const auto* edited = project.find_transform_timeline_edit(
            "mar169", "spine", TransformTimelineChannel::Translate);
        if (!set || edited == nullptr || edited->keyframes.front().x != original.x ||
            edited->keyframes.front().y != original.y ||
            edited->keyframes.front().time != original.time) {
            std::cerr << "MAR-169 segment-wide write moved a scalar component.\n";
            return false;
        }
        // Count every keyframe in the project whose easing differs.
        std::size_t differing = 0U;
        const auto same_easing = [](const marrow::runtime::Interpolation& left,
                                    const marrow::runtime::Interpolation& right) {
            if (left.kind() != right.kind()) return false;
            if (left.kind() != InterpolationKind::CubicBezier) return true;
            const auto& a = left.cubic_bezier();
            const auto& b = right.cubic_bezier();
            return a.cx1 == b.cx1 && a.cy1 == b.cy1 && a.cx2 == b.cx2 &&
                a.cy2 == b.cy2;
        };
        if (before.transform_timeline_edits.size() !=
                project.transform_timeline_edits.size() ||
            before.slot_color_timeline_edits.size() !=
                project.slot_color_timeline_edits.size() ||
            before.mesh_deform_timeline_edits.size() !=
                project.mesh_deform_timeline_edits.size()) {
            std::cerr << "MAR-169 reshaped the project's timeline collections.\n";
            return false;
        }
        for (std::size_t timeline = 0U;
             timeline < before.transform_timeline_edits.size();
             ++timeline) {
            const auto& left = before.transform_timeline_edits[timeline].keyframes;
            const auto& right = project.transform_timeline_edits[timeline].keyframes;
            if (left.size() != right.size()) return false;
            for (std::size_t key = 0U; key < left.size(); ++key) {
                if (!same_easing(left[key].interpolation, right[key].interpolation)) {
                    ++differing;
                }
            }
        }
        for (std::size_t timeline = 0U;
             timeline < before.slot_color_timeline_edits.size();
             ++timeline) {
            const auto& left = before.slot_color_timeline_edits[timeline].keyframes;
            const auto& right = project.slot_color_timeline_edits[timeline].keyframes;
            if (left.size() != right.size()) return false;
            for (std::size_t key = 0U; key < left.size(); ++key) {
                if (!same_easing(left[key].interpolation, right[key].interpolation)) {
                    ++differing;
                }
            }
        }
        for (std::size_t timeline = 0U;
             timeline < before.mesh_deform_timeline_edits.size();
             ++timeline) {
            const auto& left = before.mesh_deform_timeline_edits[timeline].keyframes;
            const auto& right = project.mesh_deform_timeline_edits[timeline].keyframes;
            if (left.size() != right.size()) return false;
            for (std::size_t key = 0U; key < left.size(); ++key) {
                if (!same_easing(left[key].interpolation, right[key].interpolation)) {
                    ++differing;
                }
            }
        }
        if (differing != 1U) {
            std::cerr << "MAR-169 changed " << differing
                      << " interpolation fields instead of exactly one.\n";
            return false;
        }
        if (marrow::editor::serialize_project(project) == before_text) {
            std::cerr << "MAR-169 reported a change it did not persist.\n";
            return false;
        }
    }

    // --- Linear -> Cubic, Stepped -> Cubic, Cubic -> Linear, Cubic -> Stepped. ---
    {
        marrow::editor::ProjectData project = build_project();
        const auto linear_to_cubic = marrow::editor::set_keyframe_interpolation(
            &project,
            {transform_selector("spine", TransformTimelineChannel::Translate, 0.0)},
            InterpolationKind::CubicBezier,
            kLinearEquivalentSeed);
        const auto stepped_to_cubic = marrow::editor::set_keyframe_interpolation(
            &project,
            {transform_selector("spine", TransformTimelineChannel::Translate, 0.5)},
            InterpolationKind::CubicBezier,
            kLinearEquivalentSeed);
        const auto* translate = project.find_transform_timeline_edit(
            "mar169", "spine", TransformTimelineChannel::Translate);
        if (!linear_to_cubic || !stepped_to_cubic || translate == nullptr ||
            translate->keyframes.size() != 2U ||
            !control_points_match(
                translate->keyframes[0].interpolation,
                kLinearEquivalentSeed) ||
            !control_points_match(
                translate->keyframes[1].interpolation,
                kLinearEquivalentSeed)) {
            std::cerr << "MAR-169 did not convert linear and stepped segments to cubic.\n";
            return false;
        }

        const auto to_linear = marrow::editor::set_keyframe_interpolation(
            &project,
            {transform_selector("spine", TransformTimelineChannel::Rotate, 0.0)},
            InterpolationKind::Linear,
            kOvershoot);
        const auto to_stepped = marrow::editor::set_keyframe_interpolation(
            &project,
            {transform_selector("spine", TransformTimelineChannel::Translate, 0.0)},
            InterpolationKind::Stepped,
            kOvershoot);
        const auto* rotate = project.find_transform_timeline_edit(
            "mar169", "spine", TransformTimelineChannel::Rotate);
        translate = project.find_transform_timeline_edit(
            "mar169", "spine", TransformTimelineChannel::Translate);
        if (!to_linear || !to_stepped || rotate == nullptr || translate == nullptr ||
            rotate->keyframes.front().interpolation.kind() != InterpolationKind::Linear ||
            translate->keyframes.front().interpolation.kind() !=
                InterpolationKind::Stepped) {
            std::cerr << "MAR-169 did not convert a cubic segment back to linear/stepped.\n";
            return false;
        }
        const std::string serialized = marrow::editor::serialize_project(project);
        if (serialized.find("\"curve\": \"linear\"") == std::string::npos ||
            serialized.find("\"curve\": \"stepped\"") == std::string::npos) {
            std::cerr << "MAR-169 linear/stepped conversion still serialized control points.\n";
            return false;
        }
        // Linear and Stepped ignore control_points entirely, so an
        // out-of-range array must not be validated against the X limits.
        marrow::editor::ProjectData ignored = build_project();
        const auto ignores_points = marrow::editor::set_keyframe_interpolation(
            &ignored,
            {transform_selector("spine", TransformTimelineChannel::Translate, 0.0)},
            InterpolationKind::Stepped,
            {std::numeric_limits<double>::quiet_NaN(), 0.0, 5.0, 1.0});
        if (!ignores_points || !ignores_points.changed) {
            std::cerr << "MAR-169 validated control points for a non-cubic kind.\n";
            return false;
        }
    }

    // --- X limits and finiteness, in that order: NaN must be rejected by the
    // finiteness test, because NaN < 0 and NaN > 1 are both false. ---
    {
        marrow::editor::ProjectData project = build_project();
        const std::string before = marrow::editor::serialize_project(project);
        const auto boundary = marrow::editor::set_keyframe_interpolation(
            &project,
            {transform_selector("spine", TransformTimelineChannel::Translate, 0.0)},
            InterpolationKind::CubicBezier,
            {0.0, 0.0, 1.0, 1.0});
        if (!boundary || !boundary.changed) {
            std::cerr << "MAR-169 rejected the exactly-in-range boundary curve: "
                      << boundary.error << '\n';
            return false;
        }

        const double nan_value = std::numeric_limits<double>::quiet_NaN();
        const double infinity = std::numeric_limits<double>::infinity();
        const struct {
            const char* label;
            std::array<double, 4> points;
        } rejections[] = {
            {"cx1 just below zero", {-1e-6, 0.0, 0.5, 1.0}},
            {"cx2 just above one", {0.0, 0.0, 1.0000001, 1.0}},
            {"cx1 NaN", {nan_value, 0.0, 0.5, 1.0}},
            {"cx1 +inf", {infinity, 0.0, 0.5, 1.0}},
            {"cx1 -inf", {-infinity, 0.0, 0.5, 1.0}},
            {"cy1 NaN", {0.0, nan_value, 0.5, 1.0}},
            {"cy1 out of float32 range", {0.0, 1e300, 0.5, 1.0}},
            {"cy2 -inf", {0.0, 0.0, 0.5, -infinity}},
        };
        marrow::editor::ProjectData rejection_project = build_project();
        const std::string rejection_before =
            marrow::editor::serialize_project(rejection_project);
        for (const auto& rejection : rejections) {
            const auto result = marrow::editor::set_keyframe_interpolation(
                &rejection_project,
                {transform_selector("spine", TransformTimelineChannel::Translate, 0.0)},
                InterpolationKind::CubicBezier,
                rejection.points);
            if (result || result.changed || result.error.empty() ||
                marrow::editor::serialize_project(rejection_project) !=
                    rejection_before) {
                std::cerr << "MAR-169 did not atomically reject " << rejection.label
                          << ".\n";
                return false;
            }
        }
        (void)before;
        (void)kExact;
    }

    // --- Unsupported key kinds and structural rejections. ---
    {
        marrow::editor::ProjectData project = build_project();
        const std::string before = marrow::editor::serialize_project(project);
        TimelineKeySelector draw_order;
        draw_order.kind = TimelineKeyKind::DrawOrder;
        draw_order.animation_name = "mar169";
        draw_order.time = 0.0;
        TimelineKeySelector event;
        event.kind = TimelineKeyKind::Event;
        event.animation_name = "mar169";
        event.time = 0.0;
        TimelineKeySelector slot_attachment;
        slot_attachment.kind = TimelineKeyKind::SlotAttachment;
        slot_attachment.animation_name = "mar169";
        slot_attachment.slot_name = "body";
        slot_attachment.time = 0.0;
        const auto duplicate =
            transform_selector("spine", TransformTimelineChannel::Translate, 0.0);
        const auto unresolvable =
            transform_selector("spine", TransformTimelineChannel::Translate, 99.0);

        const struct {
            const char* label;
            std::vector<TimelineKeySelector> selectors;
        } rejections[] = {
            {"a draw-order selector", {draw_order}},
            {"an event selector", {event}},
            {"a slot-attachment selector", {slot_attachment}},
            {"a duplicated selector", {duplicate, duplicate}},
            {"an unresolvable selector", {unresolvable}},
            {"an empty selector list", {}},
            {"a mixed batch with one unsupported kind", {duplicate, draw_order}},
        };
        for (const auto& rejection : rejections) {
            const auto result = marrow::editor::set_keyframe_interpolation(
                &project,
                rejection.selectors,
                InterpolationKind::CubicBezier,
                kOvershoot);
            if (result || result.changed || result.error.empty() ||
                marrow::editor::serialize_project(project) != before) {
                std::cerr << "MAR-169 did not atomically reject " << rejection.label
                          << ".\n";
                return false;
            }
        }
        const auto null_project = marrow::editor::set_keyframe_interpolation(
            nullptr, {duplicate}, InterpolationKind::CubicBezier, kOvershoot);
        if (null_project || null_project.error.empty()) {
            std::cerr << "MAR-169 did not reject a null project.\n";
            return false;
        }
    }

    // --- No change: writing the identical curve twice is a silent no-op, and
    // a partially-identical batch reports the split counts. ---
    {
        marrow::editor::ProjectData project = build_project();
        const auto first = marrow::editor::set_keyframe_interpolation(
            &project,
            {transform_selector("spine", TransformTimelineChannel::Translate, 0.0)},
            InterpolationKind::CubicBezier,
            kOvershoot);
        const std::string after_first = marrow::editor::serialize_project(project);
        const auto second = marrow::editor::set_keyframe_interpolation(
            &project,
            {transform_selector("spine", TransformTimelineChannel::Translate, 0.0)},
            InterpolationKind::CubicBezier,
            kOvershoot);
        if (!first || !first.changed || second.changed || !second.error.empty() ||
            second.key_count != 1U || second.changed_key_count != 0U ||
            marrow::editor::serialize_project(project) != after_first) {
            std::cerr << "MAR-169 did not report an identical rewrite as a no-op.\n";
            return false;
        }
        const auto partial = marrow::editor::set_keyframe_interpolation(
            &project,
            {transform_selector("spine", TransformTimelineChannel::Translate, 0.0),
             transform_selector("spine", TransformTimelineChannel::Translate, 0.5)},
            InterpolationKind::CubicBezier,
            kOvershoot);
        if (!partial || !partial.changed || partial.key_count != 2U ||
            partial.changed_key_count != 1U) {
            std::cerr << "MAR-169 did not split key_count and changed_key_count.\n";
            return false;
        }
    }

    // --- Save/reload: the strongest available proof that the write gate and
    // the loader's read gate are the same predicate. ---
    {
        const std::filesystem::path round_trip_path =
            "/tmp/marrow_mar169_interpolation.marrow";
        marrow::editor::ProjectData project = *project_result.project;
        project.runtime_assets.skeleton_path =
            std::filesystem::absolute(project.resolved_skeleton_path());
        project.runtime_assets.atlas_paths = project.resolved_atlas_paths();
        for (auto& atlas_path : project.runtime_assets.atlas_paths) {
            atlas_path = std::filesystem::absolute(atlas_path);
        }
        project.source_path = round_trip_path;

        TimelineKeySelector fixture_selector;
        fixture_selector.kind = TimelineKeyKind::Transform;
        fixture_selector.animation_name = "idle";
        fixture_selector.bone_name = "arm_l";
        fixture_selector.transform_channel = TransformTimelineChannel::Rotate;
        fixture_selector.time = 0.25;
        const auto set = marrow::editor::set_keyframe_interpolation(
            &project, {fixture_selector}, InterpolationKind::CubicBezier, kOvershoot);
        if (!set || !set.changed) {
            std::cerr << "MAR-169 round trip could not author the fixture curve: "
                      << set.error << '\n';
            return false;
        }
        const auto saved = marrow::editor::save_project(project, round_trip_path);
        if (!saved) {
            std::cerr << saved.error->format() << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(round_trip_path);
        if (!reloaded) {
            std::cerr << reloaded.error->format();
            return false;
        }
        const auto* reloaded_track = reloaded.project->find_transform_timeline_edit(
            "idle", "arm_l", TransformTimelineChannel::Rotate);
        if (reloaded_track == nullptr || reloaded_track->keyframes.empty() ||
            !control_points_match(
                reloaded_track->keyframes.front().interpolation, kOvershoot)) {
            std::cerr << "MAR-169 overshoot curve did not survive save and reload.\n";
            return false;
        }

        // AC6: the authored curve must reach the exported runtime JSON and the
        // v2 binary, not only the reloaded project. Saving and reloading only
        // proves the write gate and the loader's read gate are the same
        // predicate; export is a separate writer with its own encoder.
        const std::filesystem::path curve_json_path =
            "/tmp/marrow_mar169_curve.mskl";
        const std::filesystem::path curve_binary_path =
            "/tmp/marrow_mar169_curve.mbin";
        marrow::editor::ProjectExportOptions curve_export_options;
        curve_export_options.skeleton_output_path = curve_json_path;
        curve_export_options.binary_output_path = curve_binary_path;
        const auto curve_export = marrow::editor::export_runtime_assets(
            project, *project_result.base_skeleton_document, curve_export_options);
        if (!curve_export) {
            std::cerr << curve_export.error->format() << '\n';
            return false;
        }
        const auto exported = marrow::runtime::load_skeleton_data(curve_json_path);
        if (!exported) {
            std::cerr << exported.error->format();
            return false;
        }
        const auto exported_arm = exported.skeleton_data->find_bone_index("arm_l");
        const auto* exported_idle = exported.skeleton_data->find_animation("idle");
        const auto* exported_rotate =
            exported_idle != nullptr && exported_arm.has_value()
            ? exported_idle->find_rotate_timeline(*exported_arm)
            : nullptr;
        const marrow::runtime::RotateKeyframe* exported_key = nullptr;
        if (exported_rotate != nullptr) {
            for (const auto& keyframe : exported_rotate->keyframes) {
                if (std::abs(static_cast<double>(keyframe.time) - 0.25) <= 1e-6) {
                    exported_key = &keyframe;
                    break;
                }
            }
        }
        if (exported_key == nullptr ||
            !control_points_match(exported_key->interpolation, kOvershoot)) {
            std::cerr << "MAR-169 overshoot curve did not reach the exported runtime JSON.\n";
            return false;
        }
        if (!curve_export.binary_path.has_value() ||
            !validate_binary_export(curve_export.path, *curve_export.binary_path)) {
            std::cerr << "MAR-169 curve export did not match its v2 binary payload.\n";
            return false;
        }
        std::error_code curve_size_error;
        const auto curve_json_size =
            std::filesystem::file_size(curve_json_path, curve_size_error);
        std::cout << "MAR-169 curve export: JSON " << curve_json_size
                  << " bytes, MBIN "
                  << std::filesystem::file_size(curve_binary_path, curve_size_error)
                  << " bytes.\n";
        (void)near_float;
    }
    std::cout << "MAR-169 shared bezier interpolation authoring validated "
                 "across transform, slot-colour, and deform families.\n";
    return true;
}

bool validate_mar170_curve_presets(
    const marrow::editor::ProjectLoadResult& project_result) {
    using marrow::editor::CurvePreset;
    using marrow::editor::TimelineKeyKind;
    using marrow::editor::TimelineKeySelector;
    using marrow::editor::TransformTimelineChannel;
    using marrow::editor::curve_preset_definition;
    using marrow::editor::curve_preset_of;
    using marrow::runtime::AnimationScalar;
    using marrow::runtime::InterpolationKind;

    // Every preset quadruple is spelled out here rather than read from
    // kCurvePresets, so this test would still fail if the table were edited.
    struct PresetExpectation {
        CurvePreset preset;
        const char* token;
        InterpolationKind kind;
        std::array<double, 4> control_points;
    };
    const std::array<PresetExpectation, 6> kPresets{{
        {CurvePreset::Linear, "linear", InterpolationKind::Linear, {0.0, 0.0, 0.0, 0.0}},
        {CurvePreset::Stepped, "stepped", InterpolationKind::Stepped, {0.0, 0.0, 0.0, 0.0}},
        {CurvePreset::Ease, "ease", InterpolationKind::CubicBezier, {0.25, 0.1, 0.25, 1.0}},
        {CurvePreset::EaseIn, "ease_in", InterpolationKind::CubicBezier, {0.42, 0.0, 1.0, 1.0}},
        {CurvePreset::EaseOut, "ease_out", InterpolationKind::CubicBezier, {0.0, 0.0, 0.58, 1.0}},
        {CurvePreset::EaseInOut, "ease_in_out", InterpolationKind::CubicBezier,
         {0.42, 0.0, 0.58, 1.0}},
    }};
    constexpr std::array<double, 4> kEaseInOut{0.42, 0.0, 0.58, 1.0};
    // Not any preset, so applying any of the six — Linear included — is a
    // genuine change on a key seeded with it.
    const marrow::runtime::Interpolation kCustomSeed =
        marrow::runtime::Interpolation::cubic_bezier(0.2, 0.3, 0.7, 0.8);

    const auto easing_matches = [](const marrow::runtime::Interpolation& easing,
                                   const PresetExpectation& want) {
        if (easing.kind() != want.kind) return false;
        if (want.kind != InterpolationKind::CubicBezier) return true;
        const auto& points = easing.cubic_bezier();
        return points.cx1 == static_cast<AnimationScalar>(want.control_points[0]) &&
            points.cy1 == static_cast<AnimationScalar>(want.control_points[1]) &&
            points.cx2 == static_cast<AnimationScalar>(want.control_points[2]) &&
            points.cy2 == static_cast<AnimationScalar>(want.control_points[3]);
    };

    // Every outgoing easing a project stores, in a stable order, so a
    // segment-wide claim can be counted exactly rather than diffed as text.
    const auto collect_easings = [](const marrow::editor::ProjectData& project) {
        std::vector<marrow::runtime::Interpolation> easings;
        for (const auto& edit : project.transform_timeline_edits) {
            for (const auto& key : edit.keyframes) easings.push_back(key.interpolation);
        }
        for (const auto& edit : project.mesh_deform_timeline_edits) {
            for (const auto& key : edit.keyframes) easings.push_back(key.interpolation);
        }
        for (const auto& edit : project.slot_color_timeline_edits) {
            for (const auto& key : edit.keyframes) easings.push_back(key.interpolation);
        }
        return easings;
    };
    const auto same_easing = [](const marrow::runtime::Interpolation& left,
                                const marrow::runtime::Interpolation& right) {
        if (left.kind() != right.kind()) return false;
        if (left.kind() != InterpolationKind::CubicBezier) return true;
        const auto& a = left.cubic_bezier();
        const auto& b = right.cubic_bezier();
        return a.cx1 == b.cx1 && a.cy1 == b.cy1 && a.cx2 == b.cx2 && a.cy2 == b.cy2;
    };
    const auto easing_difference_count =
        [&](const marrow::editor::ProjectData& before,
            const marrow::editor::ProjectData& after) -> std::size_t {
        const auto left = collect_easings(before);
        const auto right = collect_easings(after);
        if (left.size() != right.size()) return left.size() + right.size();
        std::size_t differences = 0U;
        for (std::size_t index = 0U; index < left.size(); ++index) {
            if (!same_easing(left[index], right[index])) ++differences;
        }
        return differences;
    };

    const auto make_transform_track = [](std::string bone,
                                         TransformTimelineChannel channel,
                                         std::vector<marrow::editor::TransformKeyframeEdit> keys) {
        marrow::editor::TransformTimelineEdit edit;
        edit.animation_name = "mar170";
        edit.bone_name = std::move(bone);
        edit.channel = channel;
        edit.keyframes = std::move(keys);
        return edit;
    };

    const auto build_project = [&]() {
        marrow::editor::ProjectData project;
        project.transform_timeline_edits.push_back(make_transform_track(
            "spine",
            TransformTimelineChannel::Rotate,
            {{0.0, 10.0, 0.0, 0.0, marrow::runtime::Interpolation::linear()},
             {0.25, 15.0, 0.0, 0.0, marrow::runtime::Interpolation::stepped()},
             {0.5, 20.0, 0.0, 0.0, marrow::runtime::Interpolation::linear()}}));
        project.transform_timeline_edits.push_back(make_transform_track(
            "spine",
            TransformTimelineChannel::Translate,
            {{0.0, 0.0, 3.0, 7.0, kCustomSeed},
             {0.25, 0.0, 6.0, 1.0, marrow::runtime::Interpolation::linear()},
             {0.5, 0.0, 9.0, -2.0, marrow::runtime::Interpolation::stepped()}}));
        project.transform_timeline_edits.push_back(make_transform_track(
            "spine",
            TransformTimelineChannel::Scale,
            {{0.0, 0.0, -1.25, 2.0, marrow::runtime::Interpolation::linear()},
             {0.25, 0.0, 0.25, 1.5, marrow::runtime::Interpolation::linear()},
             {0.5, 0.0, 0.5, 1.0, marrow::runtime::Interpolation::stepped()}}));
        project.transform_timeline_edits.push_back(make_transform_track(
            "spine",
            TransformTimelineChannel::Shear,
            {{0.0, 0.0, 4.0, -6.0, marrow::runtime::Interpolation::linear()},
             {0.25, 0.0, 6.0, 3.0, marrow::runtime::Interpolation::linear()},
             {0.5, 0.0, 8.0, 12.0, marrow::runtime::Interpolation::linear()}}));

        marrow::editor::SlotColorTimelineEdit color;
        color.animation_name = "mar170";
        color.slot_name = "body";
        color.keyframes.push_back(
            {0.0, marrow::runtime::SlotColor{0.25, 0.5, 0.75, 0.4}, kCustomSeed});
        color.keyframes.push_back(
            {0.5, marrow::runtime::SlotColor{0.5, 0.25, 0.125, 0.9},
             marrow::runtime::Interpolation::stepped()});
        project.slot_color_timeline_edits.push_back(std::move(color));

        marrow::editor::MeshDeformTimelineEdit deform;
        deform.animation_name = "mar170";
        deform.slot_name = "body";
        deform.attachment_name = "body_mesh";
        deform.keyframes.push_back({0.0, {0.0, 0.0, 1.0, 2.0}, kCustomSeed});
        deform.keyframes.push_back(
            {0.5, {1.0, -1.0, 0.5, 0.25}, marrow::runtime::Interpolation::linear()});
        project.mesh_deform_timeline_edits.push_back(std::move(deform));

        marrow::editor::DrawOrderTimelineEdit draw_order;
        draw_order.animation_name = "mar170";
        draw_order.keyframes.push_back({0.0, {"body", "arm_l"}});
        project.draw_order_timeline_edits.push_back(std::move(draw_order));

        marrow::editor::EventTimelineEdit events;
        events.animation_name = "mar170";
        events.keyframes.push_back(
            {0.0, "footstep", std::nullopt, std::nullopt, std::nullopt,
             std::nullopt, std::nullopt, std::nullopt});
        project.event_timeline_edits.push_back(std::move(events));

        marrow::editor::SlotAttachmentTimelineEdit attachment;
        attachment.animation_name = "mar170";
        attachment.slot_name = "body";
        attachment.keyframes.push_back({0.0, std::string("body")});
        project.slot_attachment_timeline_edits.push_back(std::move(attachment));
        return project;
    };

    const auto transform_selector = [](std::string bone,
                                       TransformTimelineChannel channel,
                                       double time) {
        TimelineKeySelector selector;
        selector.kind = TimelineKeyKind::Transform;
        selector.animation_name = "mar170";
        selector.bone_name = std::move(bone);
        selector.transform_channel = channel;
        selector.time = time;
        return selector;
    };
    const auto color_selector = [](double time) {
        TimelineKeySelector selector;
        selector.kind = TimelineKeyKind::SlotColor;
        selector.animation_name = "mar170";
        selector.slot_name = "body";
        selector.time = time;
        return selector;
    };
    const auto deform_selector = [](double time) {
        TimelineKeySelector selector;
        selector.kind = TimelineKeyKind::Deform;
        selector.animation_name = "mar170";
        selector.slot_name = "body";
        selector.attachment_name = "body_mesh";
        selector.time = time;
        return selector;
    };

    // --- Every preset onto a Transform, a Deform, and a Slot Color key. The
    // preset writes only the easing: every scalar must stay byte-identical. ---
    for (const PresetExpectation& want : kPresets) {
        marrow::editor::ProjectData project = build_project();
        const marrow::editor::ProjectData original = project;
        const auto& definition = curve_preset_definition(want.preset);
        const std::vector<TimelineKeySelector> selectors{
            transform_selector("spine", TransformTimelineChannel::Translate, 0.0),
            deform_selector(0.0),
            color_selector(0.0)};
        const auto result = marrow::editor::set_keyframe_interpolation(
            &project, selectors, definition.kind, definition.control_points);
        if (!result || !result.changed || result.key_count != 3U) {
            std::cerr << "MAR-170 could not apply the " << want.token
                      << " preset to three families: " << result.error << '\n';
            return false;
        }

        const auto& translate = project.transform_timeline_edits[1];
        const auto& deform = project.mesh_deform_timeline_edits.front();
        const auto& color = project.slot_color_timeline_edits.front();
        if (!easing_matches(translate.keyframes[0].interpolation, want) ||
            !easing_matches(deform.keyframes[0].interpolation, want) ||
            !easing_matches(color.keyframes[0].interpolation, want)) {
            std::cerr << "MAR-170 preset " << want.token
                      << " did not store its fixed control points.\n";
            return false;
        }
        if (curve_preset_of(translate.keyframes[0].interpolation) !=
                std::optional<CurvePreset>(want.preset) ||
            curve_preset_of(deform.keyframes[0].interpolation) !=
                std::optional<CurvePreset>(want.preset) ||
            curve_preset_of(color.keyframes[0].interpolation) !=
                std::optional<CurvePreset>(want.preset)) {
            std::cerr << "MAR-170 preset " << want.token
                      << " did not read back as itself from the project.\n";
            return false;
        }

        const auto& original_translate = original.transform_timeline_edits[1];
        const auto& original_deform = original.mesh_deform_timeline_edits.front();
        const auto& original_color = original.slot_color_timeline_edits.front();
        if (translate.keyframes[0].time != original_translate.keyframes[0].time ||
            translate.keyframes[0].angle != original_translate.keyframes[0].angle ||
            translate.keyframes[0].x != original_translate.keyframes[0].x ||
            translate.keyframes[0].y != original_translate.keyframes[0].y ||
            deform.keyframes[0].time != original_deform.keyframes[0].time ||
            deform.keyframes[0].vertex_offsets !=
                original_deform.keyframes[0].vertex_offsets ||
            color.keyframes[0].time != original_color.keyframes[0].time ||
            color.keyframes[0].color.r != original_color.keyframes[0].color.r ||
            color.keyframes[0].color.g != original_color.keyframes[0].color.g ||
            color.keyframes[0].color.b != original_color.keyframes[0].color.b ||
            color.keyframes[0].color.a != original_color.keyframes[0].color.a) {
            std::cerr << "MAR-170 preset " << want.token
                      << " rewrote a scalar it must never touch.\n";
            return false;
        }

        // Re-applying the same preset commits nothing.
        const std::string after_first = marrow::editor::serialize_project(project);
        const auto repeat = marrow::editor::set_keyframe_interpolation(
            &project, selectors, definition.kind, definition.control_points);
        if (!repeat || repeat.changed || repeat.changed_key_count != 0U ||
            repeat.key_count != 3U ||
            marrow::editor::serialize_project(project) != after_first) {
            std::cerr << "MAR-170 re-applying the " << want.token
                      << " preset was not a no-op.\n";
            return false;
        }
    }

    // --- Segment-wide identity: one Translate key carries one shared easing,
    // so exactly one stored interpolation in the whole project may differ. ---
    {
        marrow::editor::ProjectData project = build_project();
        const marrow::editor::ProjectData before = project;
        const auto& definition = curve_preset_definition(CurvePreset::EaseInOut);
        const auto result = marrow::editor::set_keyframe_interpolation(
            &project,
            {transform_selector("spine", TransformTimelineChannel::Translate, 0.0)},
            definition.kind,
            definition.control_points);
        if (!result || !result.changed || result.changed_key_count != 1U) {
            std::cerr << "MAR-170 segment-wide preset write failed: " << result.error
                      << '\n';
            return false;
        }
        if (easing_difference_count(before, project) != 1U) {
            std::cerr << "MAR-170 preset changed more than one stored interpolation.\n";
            return false;
        }
    }

    // --- Multi-key determinism: the same preset over the same twelve keys in
    // opposite selector orders must serialize byte-identically. ---
    {
        std::vector<TimelineKeySelector> selectors;
        for (const auto channel : {TransformTimelineChannel::Rotate,
                                   TransformTimelineChannel::Translate,
                                   TransformTimelineChannel::Scale}) {
            for (const double time : {0.0, 0.25, 0.5}) {
                selectors.push_back(transform_selector("spine", channel, time));
            }
        }
        selectors.push_back(color_selector(0.0));
        selectors.push_back(deform_selector(0.0));
        selectors.push_back(deform_selector(0.5));
        if (selectors.size() != 12U) {
            std::cerr << "MAR-170 determinism case expects twelve selectors.\n";
            return false;
        }
        const auto& definition = curve_preset_definition(CurvePreset::EaseInOut);

        marrow::editor::ProjectData forward = build_project();
        const auto forward_result = marrow::editor::set_keyframe_interpolation(
            &forward, selectors, definition.kind, definition.control_points);
        std::vector<TimelineKeySelector> reversed(selectors.rbegin(), selectors.rend());
        marrow::editor::ProjectData backward = build_project();
        const auto backward_result = marrow::editor::set_keyframe_interpolation(
            &backward, reversed, definition.kind, definition.control_points);
        if (!forward_result || !backward_result || !forward_result.changed ||
            !backward_result.changed || forward_result.key_count != 12U ||
            backward_result.key_count != 12U ||
            forward_result.changed_key_count != backward_result.changed_key_count) {
            std::cerr << "MAR-170 multi-key preset application was order-dependent "
                         "in its result shape.\n";
            return false;
        }
        if (marrow::editor::serialize_project(forward) !=
            marrow::editor::serialize_project(backward)) {
            std::cerr << "MAR-170 multi-key preset application was not deterministic.\n";
            return false;
        }
        for (const auto& edit : forward.transform_timeline_edits) {
            if (edit.channel == TransformTimelineChannel::Shear) continue;
            for (const auto& key : edit.keyframes) {
                if (curve_preset_of(key.interpolation) !=
                    std::optional<CurvePreset>(CurvePreset::EaseInOut)) {
                    std::cerr << "MAR-170 multi-key preset missed a selected key.\n";
                    return false;
                }
            }
        }
        // The unselected Shear track keeps its own curves.
        for (const auto& edit : forward.transform_timeline_edits) {
            if (edit.channel != TransformTimelineChannel::Shear) continue;
            for (const auto& key : edit.keyframes) {
                if (key.interpolation.kind() != InterpolationKind::Linear) {
                    std::cerr << "MAR-170 multi-key preset touched an unselected key.\n";
                    return false;
                }
            }
        }
    }

    // --- Skip/reject boundary. The primitive rejects easing-free families
    // atomically; MAR-170's GUI is what filters around this, and the Agent
    // deliberately does not. ---
    {
        const auto& definition = curve_preset_definition(CurvePreset::Ease);
        const std::array<TimelineKeyKind, 3> kEasingFree{
            TimelineKeyKind::DrawOrder,
            TimelineKeyKind::Event,
            TimelineKeyKind::SlotAttachment};
        for (const TimelineKeyKind kind : kEasingFree) {
            marrow::editor::ProjectData project = build_project();
            const std::string before = marrow::editor::serialize_project(project);
            TimelineKeySelector selector;
            selector.kind = kind;
            selector.animation_name = "mar170";
            selector.slot_name = "body";
            selector.time = 0.0;
            const auto result = marrow::editor::set_keyframe_interpolation(
                &project,
                {transform_selector("spine", TransformTimelineChannel::Rotate, 0.0),
                 selector},
                definition.kind,
                definition.control_points);
            if (result || result.changed ||
                marrow::editor::serialize_project(project) != before) {
                std::cerr << "MAR-170 expected an easing-free selector to be rejected "
                             "atomically.\n";
                return false;
            }
        }
    }

    // --- Every newly authored continuous segment takes the remembered
    // default, whichever shell gesture authored it. `upsert_transform_keyframe()`
    // is the shared primitive behind the viewport gizmos and the Inspector
    // fields, so it takes the seed as an argument: the shell passes the
    // preference and the Agent keeps the reproducible Linear default. ---
    {
        marrow::editor::ProjectData project = *project_result.project;
        const auto& skeleton = *project_result.skeleton_data;

        // No argument: the Agent's contract. A new key is Linear.
        const auto& agent_key = marrow::editor::upsert_transform_keyframe(
            project, skeleton, "idle", "spine",
            TransformTimelineChannel::Rotate, 0.135,
            marrow::editor::TransformKeyframePatch{11.0, std::nullopt, std::nullopt});
        if (agent_key.interpolation.kind() != InterpolationKind::Linear) {
            std::cerr << "MAR-170 changed the default easing of an agent-created "
                         "transform key.\n";
            return false;
        }

        // Explicit seed: the shell's contract. A new key takes the preset.
        const auto& seeded_key = marrow::editor::upsert_transform_keyframe(
            project, skeleton, "idle", "spine",
            TransformTimelineChannel::Rotate, 0.145,
            marrow::editor::TransformKeyframePatch{12.0, std::nullopt, std::nullopt},
            marrow::editor::curve_preset_interpolation(CurvePreset::EaseOut));
        if (curve_preset_of(seeded_key.interpolation) !=
            std::optional<CurvePreset>(CurvePreset::EaseOut)) {
            std::cerr << "MAR-170 did not seed a newly inserted transform key with the "
                         "remembered default.\n";
            return false;
        }

        // An existing key keeps its own curve: a seed initializes, never rewrites.
        const auto& updated_key = marrow::editor::upsert_transform_keyframe(
            project, skeleton, "idle", "spine",
            TransformTimelineChannel::Rotate, 0.135,
            marrow::editor::TransformKeyframePatch{13.0, std::nullopt, std::nullopt},
            marrow::editor::curve_preset_interpolation(CurvePreset::EaseIn));
        if (updated_key.interpolation.kind() != InterpolationKind::Linear ||
            updated_key.angle != 13.0) {
            std::cerr << "MAR-170 rewrote an existing key's curve while updating its "
                         "value.\n";
            return false;
        }
    }

    // --- Save/reload/export on the real fixture: a preset must survive the
    // project writer, the project loader, the runtime JSON encoder, and the v2
    // binary encoder as the same preset. The exported artifact must carry the
    // authored curve, so the export runs over the edited project. ---
    {
        const std::filesystem::path round_trip_path =
            "/tmp/marrow_mar170_presets.marrow";
        marrow::editor::ProjectData project = *project_result.project;
        project.runtime_assets.skeleton_path =
            std::filesystem::absolute(project.resolved_skeleton_path());
        project.runtime_assets.atlas_paths = project.resolved_atlas_paths();
        for (auto& atlas_path : project.runtime_assets.atlas_paths) {
            atlas_path = std::filesystem::absolute(atlas_path);
        }
        project.source_path = round_trip_path;

        TimelineKeySelector fixture_selector;
        fixture_selector.kind = TimelineKeyKind::Transform;
        fixture_selector.animation_name = "idle";
        fixture_selector.bone_name = "arm_l";
        fixture_selector.transform_channel = TransformTimelineChannel::Rotate;
        fixture_selector.time = 0.25;
        const auto& definition = curve_preset_definition(CurvePreset::EaseInOut);
        const auto set = marrow::editor::set_keyframe_interpolation(
            &project, {fixture_selector}, definition.kind, definition.control_points);
        if (!set || !set.changed) {
            std::cerr << "MAR-170 could not author the fixture preset: " << set.error
                      << '\n';
            return false;
        }
        const auto saved = marrow::editor::save_project(project, round_trip_path);
        if (!saved) {
            std::cerr << saved.error->format() << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(round_trip_path);
        if (!reloaded) {
            std::cerr << reloaded.error->format();
            return false;
        }
        const auto* reloaded_track = reloaded.project->find_transform_timeline_edit(
            "idle", "arm_l", TransformTimelineChannel::Rotate);
        if (reloaded_track == nullptr || reloaded_track->keyframes.empty() ||
            curve_preset_of(reloaded_track->keyframes.front().interpolation) !=
                std::optional<CurvePreset>(CurvePreset::EaseInOut)) {
            std::cerr << "MAR-170 preset identity did not survive save and reload.\n";
            return false;
        }

        const std::filesystem::path preset_json_path = "/tmp/marrow_mar170_preset.mskl";
        const std::filesystem::path preset_binary_path = "/tmp/marrow_mar170_preset.mbin";
        marrow::editor::ProjectExportOptions preset_export_options;
        preset_export_options.skeleton_output_path = preset_json_path;
        preset_export_options.binary_output_path = preset_binary_path;
        const auto preset_export = marrow::editor::export_runtime_assets(
            project, *project_result.base_skeleton_document, preset_export_options);
        if (!preset_export) {
            std::cerr << preset_export.error->format() << '\n';
            return false;
        }
        const auto exported = marrow::runtime::load_skeleton_data(preset_json_path);
        if (!exported) {
            std::cerr << exported.error->format();
            return false;
        }
        const auto exported_arm = exported.skeleton_data->find_bone_index("arm_l");
        const auto* exported_idle = exported.skeleton_data->find_animation("idle");
        const auto* exported_rotate =
            exported_idle != nullptr && exported_arm.has_value()
            ? exported_idle->find_rotate_timeline(*exported_arm)
            : nullptr;
        const marrow::runtime::RotateKeyframe* exported_key = nullptr;
        if (exported_rotate != nullptr) {
            for (const auto& keyframe : exported_rotate->keyframes) {
                if (std::abs(static_cast<double>(keyframe.time) - 0.25) <= 1e-6) {
                    exported_key = &keyframe;
                    break;
                }
            }
        }
        if (exported_key == nullptr ||
            curve_preset_of(exported_key->interpolation) !=
                std::optional<CurvePreset>(CurvePreset::EaseInOut)) {
            std::cerr << "MAR-170 preset did not reach the exported runtime JSON as "
                         "the same preset.\n";
            return false;
        }
        const auto& exported_points = exported_key->interpolation.cubic_bezier();
        if (exported_points.cx1 != static_cast<AnimationScalar>(kEaseInOut[0]) ||
            exported_points.cy1 != static_cast<AnimationScalar>(kEaseInOut[1]) ||
            exported_points.cx2 != static_cast<AnimationScalar>(kEaseInOut[2]) ||
            exported_points.cy2 != static_cast<AnimationScalar>(kEaseInOut[3])) {
            std::cerr << "MAR-170 exported control points are not the Ease-In-Out "
                         "constant.\n";
            return false;
        }
        if (!preset_export.binary_path.has_value() ||
            !validate_binary_export(preset_export.path, *preset_export.binary_path)) {
            std::cerr << "MAR-170 preset export did not match its v2 binary payload.\n";
            return false;
        }
        std::error_code preset_size_error;
        std::cout << "MAR-170 preset export: JSON "
                  << std::filesystem::file_size(preset_json_path, preset_size_error)
                  << " bytes, MBIN "
                  << std::filesystem::file_size(preset_binary_path, preset_size_error)
                  << " bytes.\n";
    }

    std::cout << "MAR-170 fixed curve presets validated across transform, deform, and "
                 "slot-colour families.\n";
    return true;
}

/**
 * @brief MAR-171: project-local automatic curve mode, driver, and resolution.
 *
 * The two `.marrow` keyframe fields are optional and absent by default, so the
 * first block proves a project with no automatic key serializes byte for byte
 * as it did before this story existed.
 */
bool validate_mar171_automatic_curves(
    const marrow::editor::ProjectLoadResult& project_result) {
    using marrow::editor::TimelineCurveMode;
    using marrow::editor::TimelineKeyKind;
    using marrow::editor::TimelineKeySelector;
    using marrow::editor::TimelineScalarComponent;
    using marrow::editor::TransformTimelineChannel;
    using marrow::runtime::InterpolationKind;

    std::error_code ignored;
    const std::string path_token = std::to_string(
        static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    // Saving to a temp directory needs the referenced runtime assets resolved
    // absolutely, exactly as MAR-169's round trip does.
    const auto rebase = [&](const std::filesystem::path& destination) {
        marrow::editor::ProjectData project = *project_result.project;
        project.runtime_assets.skeleton_path =
            std::filesystem::absolute(project.resolved_skeleton_path());
        project.runtime_assets.atlas_paths = project.resolved_atlas_paths();
        for (auto& atlas_path : project.runtime_assets.atlas_paths) {
            atlas_path = std::filesystem::absolute(atlas_path);
        }
        project.source_path = destination;
        return project;
    };

    // The fixture authors no slot-colour timeline, so the two slot-colour
    // cases below build one on the real `body` slot.
    const auto make_body_color_track = [] {
        marrow::editor::SlotColorTimelineEdit color;
        color.animation_name = "idle";
        color.slot_name = "body";
        color.keyframes.push_back(
            {0.0, marrow::runtime::SlotColor{1.0, 1.0, 1.0, 1.0},
             marrow::runtime::Interpolation::linear()});
        color.keyframes.push_back(
            {0.5, marrow::runtime::SlotColor{0.5, 0.75, 1.0, 0.25},
             marrow::runtime::Interpolation::linear()});
        color.keyframes.push_back(
            {1.0, marrow::runtime::SlotColor{0.25, 0.5, 1.0, 1.0},
             marrow::runtime::Interpolation::linear()});
        return color;
    };

    // --- Default-off, byte for byte -------------------------------------
    const auto default_off_path = std::filesystem::temp_directory_path() /
        ("marrow_mar171_default_off_" + path_token + ".marrow");
    const marrow::editor::ProjectData default_off_project = rebase(default_off_path);
    const std::string untouched_text =
        marrow::editor::serialize_project(default_off_project);
    if (untouched_text.find("curve_mode") != std::string::npos ||
        untouched_text.find("curve_driver") != std::string::npos) {
        std::cerr << "MAR-171 serialized a curve-mode field into an untouched project.\n";
        return false;
    }
    const auto default_off_saved =
        marrow::editor::save_project(default_off_project, default_off_path);
    if (!default_off_saved) {
        std::cerr << default_off_saved.error->format() << '\n';
        return false;
    }
    const auto default_off_reloaded = marrow::editor::load_project(default_off_path);
    std::filesystem::remove(default_off_path, ignored);
    if (!default_off_reloaded) {
        std::cerr << default_off_reloaded.error->format();
        return false;
    }
    if (marrow::editor::serialize_project(*default_off_reloaded.project) !=
        untouched_text) {
        std::cerr << "MAR-171 broke load/save byte stability on an untouched project.\n";
        return false;
    }

    // --- The two fields serialize only for an automatic key --------------
    const auto round_trip_path = std::filesystem::temp_directory_path() /
        ("marrow_mar171_round_trip_" + path_token + ".marrow");
    {
        marrow::editor::ProjectData project = rebase(round_trip_path);
        auto* rotate = project.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        auto* arm = project.find_transform_timeline_edit(
            "idle", "arm_l", TransformTimelineChannel::Rotate);
        if (rotate == nullptr || rotate->keyframes.empty() || arm == nullptr ||
            arm->keyframes.empty()) {
            std::cerr << "MAR-171 storage needs the fixture's spine and arm_l rotate tracks.\n";
            return false;
        }
        rotate->keyframes.front().curve_mode = TimelineCurveMode::Auto;
        rotate->keyframes.front().curve_driver = TimelineScalarComponent::Angle;
        // A manual key with a non-default driver in memory must serialize
        // neither field, so there is exactly one on-disk shape for "manual".
        arm->keyframes.front().curve_mode = TimelineCurveMode::Manual;
        arm->keyframes.front().curve_driver = TimelineScalarComponent::Y;

        const std::string text = marrow::editor::serialize_project(project);
        const auto count_of = [&](std::string_view needle) {
            std::size_t total = 0U;
            for (std::size_t at = text.find(needle); at != std::string::npos;
                 at = text.find(needle, at + needle.size())) {
                ++total;
            }
            return total;
        };
        if (count_of("\"curve_mode\"") != 1U || count_of("\"curve_driver\"") != 1U) {
            std::cerr << "MAR-171 must serialize the pair exactly once, on the auto key.\n";
            return false;
        }

        const auto saved = marrow::editor::save_project(project, round_trip_path);
        if (!saved) {
            std::cerr << saved.error->format() << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(round_trip_path);
        std::filesystem::remove(round_trip_path, ignored);
        if (!reloaded) {
            std::cerr << reloaded.error->format();
            return false;
        }
        const auto* reloaded_rotate = reloaded.project->find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        const auto* reloaded_arm = reloaded.project->find_transform_timeline_edit(
            "idle", "arm_l", TransformTimelineChannel::Rotate);
        if (reloaded_rotate == nullptr || reloaded_rotate->keyframes.empty() ||
            reloaded_rotate->keyframes.front().curve_mode != TimelineCurveMode::Auto ||
            reloaded_rotate->keyframes.front().curve_driver !=
                TimelineScalarComponent::Angle) {
            std::cerr << "MAR-171 curve mode and driver did not survive save and reload.\n";
            return false;
        }
        if (reloaded_arm == nullptr || reloaded_arm->keyframes.empty() ||
            reloaded_arm->keyframes.front().curve_mode != TimelineCurveMode::Manual ||
            reloaded_arm->keyframes.front().curve_driver !=
                TimelineScalarComponent::Angle) {
            std::cerr << "MAR-171 must reload a manual key with its family-default driver.\n";
            return false;
        }
    }

    // --- Slot Color round trip -------------------------------------------
    const auto color_path = std::filesystem::temp_directory_path() /
        ("marrow_mar171_color_" + path_token + ".marrow");
    {
        marrow::editor::ProjectData project = rebase(color_path);
        // The fixture has no authored slot-colour track, so the round trip
        // authors one on a real slot rather than skipping the family.
        project.slot_color_timeline_edits.push_back(make_body_color_track());
        auto* color = project.find_slot_color_timeline_edit("idle", "body");
        color->keyframes.front().curve_mode = TimelineCurveMode::Auto;
        color->keyframes.front().curve_driver = TimelineScalarComponent::Alpha;
        const auto saved = marrow::editor::save_project(project, color_path);
        if (!saved) {
            std::cerr << saved.error->format() << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(color_path);
        std::filesystem::remove(color_path, ignored);
        if (!reloaded) {
            std::cerr << reloaded.error->format();
            return false;
        }
        const auto* reloaded_color =
            reloaded.project->find_slot_color_timeline_edit("idle", "body");
        if (reloaded_color == nullptr || reloaded_color->keyframes.empty() ||
            reloaded_color->keyframes.front().curve_mode != TimelineCurveMode::Auto ||
            reloaded_color->keyframes.front().curve_driver !=
                TimelineScalarComponent::Alpha) {
            std::cerr << "MAR-171 slot-colour curve intent did not round trip.\n";
            return false;
        }
    }

    // --- Load validation, each with its own JSON path ---------------------
    {
        marrow::editor::ProjectData project = rebase(round_trip_path);
        auto* rotate = project.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        rotate->keyframes.front().curve_mode = TimelineCurveMode::Auto;
        rotate->keyframes.front().curve_driver = TimelineScalarComponent::Angle;
        const auto base_document = marrow::runtime::json::parse_document(
            marrow::editor::serialize_project(project), "mar171");
        if (!base_document) {
            std::cerr << "MAR-171 could not reparse its own serialized project.\n";
            return false;
        }
        const auto locate_keyframe = [&](marrow::runtime::json::Document* document)
            -> marrow::runtime::json::Value* {
            auto* edits = marrow::runtime::json::find_member(
                document->root, "timeline_edits");
            auto* animations = edits != nullptr
                ? marrow::runtime::json::find_member(*edits, "animations")
                : nullptr;
            auto* idle = animations != nullptr
                ? marrow::runtime::json::find_member(*animations, "idle")
                : nullptr;
            auto* bones = idle != nullptr
                ? marrow::runtime::json::find_member(*idle, "bones")
                : nullptr;
            auto* spine = bones != nullptr
                ? marrow::runtime::json::find_member(*bones, "spine")
                : nullptr;
            auto* rotate_value = spine != nullptr
                ? marrow::runtime::json::find_member(*spine, "rotate")
                : nullptr;
            if (rotate_value == nullptr || !rotate_value->is_array() ||
                rotate_value->as_array().empty()) {
                return nullptr;
            }
            return &rotate_value->as_array().front();
        };

        struct MalformedCase {
            const char* label;
            const char* member;
            marrow::runtime::json::Value value;
            bool drop_mode;
            const char* expected_message;
        };
        const std::string keyframe_path =
            "$.timeline_edits.animations.idle.bones.spine.rotate[0]";
        const std::vector<MalformedCase> cases{
            {"a numeric curve_mode", "curve_mode",
             marrow::runtime::json::Value(3.0, {}), false,
             "curve_mode must be 'manual' or 'auto'"},
            {"an unknown curve_mode token", "curve_mode",
             marrow::runtime::json::Value(std::string("automatic"), {}), false,
             "curve_mode must be 'manual' or 'auto'"},
            {"a numeric curve_driver", "curve_driver",
             marrow::runtime::json::Value(7.0, {}), false,
             "curve_driver must be one of angle, x, y, r, g, b, a"},
            {"an unknown curve_driver token", "curve_driver",
             marrow::runtime::json::Value(std::string("z"), {}), false,
             "curve_driver must be one of angle, x, y, r, g, b, a"},
            {"a driver the family does not own", "curve_driver",
             marrow::runtime::json::Value(std::string("x"), {}), false,
             "curve_driver must name a component this timeline owns"},
            {"a driver on a manual key", "curve_driver",
             marrow::runtime::json::Value(std::string("angle"), {}), true,
             "curve_driver requires curve_mode 'auto'"},
        };
        for (const MalformedCase& malformed : cases) {
            auto document = *base_document.document;
            auto* keyframe = locate_keyframe(&document);
            if (keyframe == nullptr) {
                std::cerr << "MAR-171 load validation could not locate the keyframe.\n";
                return false;
            }
            keyframe->as_object()[malformed.member] = malformed.value;
            if (malformed.drop_mode) keyframe->as_object().erase("curve_mode");
            const auto loaded = marrow::editor::load_project(document);
            if (loaded) {
                std::cerr << "MAR-171 loader accepted " << malformed.label << ".\n";
                return false;
            }
            const std::string message = loaded.error->message;
            const std::string want_path =
                keyframe_path + "." + std::string(malformed.member);
            if (message.find(want_path) != 0U ||
                message.find(malformed.expected_message) == std::string::npos) {
                std::cerr << "MAR-171 rejected " << malformed.label
                          << " with the wrong path or message: " << message << '\n';
                return false;
            }
        }
    }

    // --- validate_project_for_save() re-validates the driver ---------------
    {
        const auto invalid_path = std::filesystem::temp_directory_path() /
            ("marrow_mar171_invalid_driver_" + path_token + ".marrow");
        marrow::editor::ProjectData project = rebase(invalid_path);
        project.slot_color_timeline_edits.push_back(make_body_color_track());
        auto* color = project.find_slot_color_timeline_edit("idle", "body");
        color->keyframes.front().curve_mode = TimelineCurveMode::Auto;
        color->keyframes.front().curve_driver = TimelineScalarComponent::Angle;
        const auto saved = marrow::editor::save_project(project, invalid_path);
        std::filesystem::remove(invalid_path, ignored);
        if (saved) {
            std::cerr << "MAR-171 saver accepted an Angle driver on a slot-colour key.\n";
            return false;
        }
        if (saved.error->message.find(
                "automatic curve drivers must name a component the timeline owns") ==
            std::string::npos) {
            std::cerr << "MAR-171 driver-authorability message was wrong: "
                      << saved.error->message << '\n';
            return false;
        }
    }

    // ------------------------------------------------------------------
    // The two authoring primitives.
    // ------------------------------------------------------------------
    constexpr double kThird = 1.0 / 3.0;
    constexpr double kTwoThirds = 2.0 / 3.0;
    const auto narrowed = [](double value) {
        return static_cast<double>(
            static_cast<marrow::runtime::AnimationScalar>(value));
    };
    const auto curve_is = [&](const marrow::runtime::Interpolation& easing,
                              const std::array<double, 4>& expected) {
        if (easing.kind() != InterpolationKind::CubicBezier) return false;
        const auto& points = easing.cubic_bezier();
        return static_cast<double>(points.cx1) == narrowed(expected[0]) &&
            static_cast<double>(points.cy1) == narrowed(expected[1]) &&
            static_cast<double>(points.cx2) == narrowed(expected[2]) &&
            static_cast<double>(points.cy2) == narrowed(expected[3]);
    };
    // The design's §6.6 worked example over the fixture's spine rotate keys
    // (t = 0, 0.5, 1 and angle = 0, 8, -2), spelled out rather than recomputed.
    const std::array<double, 4> kSegment0{kThird, kThird, kTwoThirds, 1.0};
    const std::array<double, 4> kSegment1{kThird, 0.0, kTwoThirds, kTwoThirds};

    const auto spine_selector = [](double time) {
        TimelineKeySelector selector;
        selector.kind = TimelineKeyKind::Transform;
        selector.animation_name = "idle";
        selector.bone_name = "spine";
        selector.transform_channel = TransformTimelineChannel::Rotate;
        selector.time = time;
        return selector;
    };
    const auto spine_rotate = [](const marrow::editor::ProjectData& project) {
        return project.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
    };

    // --- Round trip through the primitive, then through the file ---------
    {
        const auto primitive_path = std::filesystem::temp_directory_path() /
            ("marrow_mar171_primitive_" + path_token + ".marrow");
        marrow::editor::ProjectData project = rebase(primitive_path);
        const auto applied = marrow::editor::set_keyframe_curve_mode(
            &project,
            {spine_selector(0.0), spine_selector(0.5)},
            TimelineCurveMode::Auto,
            TimelineScalarComponent::Angle);
        if (!applied || !applied.changed || applied.key_count != 2U ||
            applied.changed_key_count != 2U || applied.resolved_key_count != 2U) {
            std::cerr << "MAR-171 primitive did not author two automatic keys: "
                      << applied.error << '\n';
            return false;
        }
        const auto* track = spine_rotate(project);
        if (track == nullptr || track->keyframes.size() != 3U ||
            !curve_is(track->keyframes[0].interpolation, kSegment0) ||
            !curve_is(track->keyframes[1].interpolation, kSegment1)) {
            std::cerr << "MAR-171 did not store the design's worked-example curves.\n";
            return false;
        }
        // The last key has no outgoing segment, so its stored easing is left
        // byte-identical even when its own mode becomes automatic.
        const marrow::runtime::Interpolation last_before =
            track->keyframes[2].interpolation;
        const auto last = marrow::editor::set_keyframe_curve_mode(
            &project,
            {spine_selector(1.0)},
            TimelineCurveMode::Auto,
            TimelineScalarComponent::Angle);
        const auto* after_last = spine_rotate(project);
        if (!last || !last.changed || last.resolved_key_count != 0U ||
            after_last->keyframes[2].curve_mode != TimelineCurveMode::Auto ||
            after_last->keyframes[2].interpolation.kind() != last_before.kind()) {
            std::cerr << "MAR-171 must record a last key's mode without writing its easing.\n";
            return false;
        }

        const auto saved = marrow::editor::save_project(project, primitive_path);
        if (!saved) {
            std::cerr << saved.error->format() << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(primitive_path);
        std::filesystem::remove(primitive_path, ignored);
        if (!reloaded) {
            std::cerr << reloaded.error->format();
            return false;
        }
        const auto* reloaded_track = spine_rotate(*reloaded.project);
        if (reloaded_track == nullptr ||
            reloaded_track->keyframes[0].curve_mode != TimelineCurveMode::Auto ||
            reloaded_track->keyframes[0].curve_driver != TimelineScalarComponent::Angle ||
            !curve_is(reloaded_track->keyframes[0].interpolation, kSegment0) ||
            !curve_is(reloaded_track->keyframes[1].interpolation, kSegment1)) {
            std::cerr << "MAR-171 resolved curves did not survive save and reload.\n";
            return false;
        }
    }

    // --- Atomic rejections ------------------------------------------------
    {
        marrow::editor::ProjectData project = *project_result.project;
        project.slot_color_timeline_edits.push_back(make_body_color_track());
        const std::string snapshot = marrow::editor::serialize_project(project);

        TimelineKeySelector deform;
        deform.kind = TimelineKeyKind::Deform;
        deform.animation_name = "idle";
        deform.slot_name = "body";
        deform.attachment_name = "body_mesh";
        deform.time = 0.0;
        TimelineKeySelector draw_order;
        draw_order.kind = TimelineKeyKind::DrawOrder;
        draw_order.animation_name = "idle";
        draw_order.time = 0.0;
        TimelineKeySelector event;
        event.kind = TimelineKeyKind::Event;
        event.animation_name = "idle";
        event.time = 0.25;
        TimelineKeySelector attachment;
        attachment.kind = TimelineKeyKind::SlotAttachment;
        attachment.animation_name = "idle";
        attachment.slot_name = "body";
        attachment.time = 0.0;
        TimelineKeySelector color;
        color.kind = TimelineKeyKind::SlotColor;
        color.animation_name = "idle";
        color.slot_name = "body";
        color.time = 0.0;
        TimelineKeySelector unresolvable = spine_selector(9.75);

        struct RejectionCase {
            const char* label;
            std::vector<TimelineKeySelector> selectors;
            TimelineScalarComponent driver;
        };
        const std::vector<RejectionCase> rejections{
            {"a Deform selector", {deform}, TimelineScalarComponent::Angle},
            {"a Draw Order selector", {draw_order}, TimelineScalarComponent::Angle},
            {"an Event selector", {event}, TimelineScalarComponent::Angle},
            {"a Slot Attachment selector", {attachment}, TimelineScalarComponent::Angle},
            {"an unresolvable selector", {unresolvable}, TimelineScalarComponent::Angle},
            {"a duplicated selector",
             {spine_selector(0.0), spine_selector(0.0)},
             TimelineScalarComponent::Angle},
            {"an empty selector list", {}, TimelineScalarComponent::Angle},
            {"a driver the family does not own",
             {spine_selector(0.0)},
             TimelineScalarComponent::X},
            {"a slot-colour driver on a rotate key",
             {spine_selector(0.0)},
             TimelineScalarComponent::Alpha},
            {"an Angle driver on a slot-colour key",
             {color},
             TimelineScalarComponent::Angle},
        };
        for (const RejectionCase& rejection : rejections) {
            marrow::editor::ProjectData candidate = project;
            const auto result = marrow::editor::set_keyframe_curve_mode(
                &candidate, rejection.selectors, TimelineCurveMode::Auto,
                rejection.driver);
            if (result || result.error.empty()) {
                std::cerr << "MAR-171 accepted " << rejection.label << ".\n";
                return false;
            }
            if (marrow::editor::serialize_project(candidate) != snapshot) {
                std::cerr << "MAR-171 mutated the project while rejecting "
                          << rejection.label << ".\n";
                return false;
            }
        }
    }

    // --- Segment-wide identity: the driver selects a series to read, never
    // --- which bytes are written. -----------------------------------------
    {
        marrow::editor::ProjectData project = *project_result.project;
        marrow::editor::TransformTimelineEdit translate;
        translate.animation_name = "idle";
        translate.bone_name = "spine";
        translate.channel = TransformTimelineChannel::Translate;
        translate.keyframes.push_back(
            {0.0, 0.0, 0.0, 0.0, marrow::runtime::Interpolation::linear()});
        translate.keyframes.push_back(
            {0.5, 0.0, 4.0, 9.0, marrow::runtime::Interpolation::linear()});
        translate.keyframes.push_back(
            {1.0, 0.0, 6.0, 1.0, marrow::runtime::Interpolation::linear()});
        project.transform_timeline_edits.push_back(std::move(translate));

        const auto count_easing_differences =
            [](const marrow::editor::ProjectData& before,
               const marrow::editor::ProjectData& after) {
                std::size_t differences = 0U;
                for (std::size_t edit = 0U;
                     edit < before.transform_timeline_edits.size();
                     ++edit) {
                    const auto& left = before.transform_timeline_edits[edit].keyframes;
                    const auto& right = after.transform_timeline_edits[edit].keyframes;
                    for (std::size_t key = 0U; key < left.size(); ++key) {
                        const auto& a = left[key].interpolation;
                        const auto& b = right[key].interpolation;
                        if (a.kind() != b.kind()) {
                            ++differences;
                            continue;
                        }
                        if (a.kind() != InterpolationKind::CubicBezier) continue;
                        if (a.cubic_bezier().cx1 != b.cubic_bezier().cx1 ||
                            a.cubic_bezier().cy1 != b.cubic_bezier().cy1 ||
                            a.cubic_bezier().cx2 != b.cubic_bezier().cx2 ||
                            a.cubic_bezier().cy2 != b.cubic_bezier().cy2) {
                            ++differences;
                        }
                    }
                }
                return differences;
            };
        TimelineKeySelector translate_key;
        translate_key.kind = TimelineKeyKind::Transform;
        translate_key.animation_name = "idle";
        translate_key.bone_name = "spine";
        translate_key.transform_channel = TransformTimelineChannel::Translate;
        translate_key.time = 0.0;

        marrow::editor::ProjectData by_y = project;
        const auto y_applied = marrow::editor::set_keyframe_curve_mode(
            &by_y, {translate_key}, TimelineCurveMode::Auto,
            TimelineScalarComponent::Y);
        marrow::editor::ProjectData by_x = project;
        const auto x_applied = marrow::editor::set_keyframe_curve_mode(
            &by_x, {translate_key}, TimelineCurveMode::Auto,
            TimelineScalarComponent::X);
        if (!y_applied || !x_applied ||
            count_easing_differences(project, by_y) != 1U ||
            count_easing_differences(project, by_x) != 1U) {
            std::cerr << "MAR-171 must write exactly one shared easing per driver.\n";
            return false;
        }
        const auto* y_track = by_y.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Translate);
        const auto* x_track = by_x.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Translate);
        const auto& y_curve = y_track->keyframes.front().interpolation.cubic_bezier();
        const auto& x_curve = x_track->keyframes.front().interpolation.cubic_bezier();
        if (y_curve.cy1 == x_curve.cy1 && y_curve.cy2 == x_curve.cy2) {
            std::cerr << "MAR-171 driver choice must change the resolved curve.\n";
            return false;
        }
    }

    // --- Neighbour recomputation, demotion, no-change, and re-resolve -----
    {
        marrow::editor::ProjectData project = *project_result.project;
        const auto seeded = marrow::editor::set_keyframe_curve_mode(
            &project, {spine_selector(0.0), spine_selector(0.5)},
            TimelineCurveMode::Auto, TimelineScalarComponent::Angle);
        if (!seeded) {
            std::cerr << "MAR-171 could not seed the recomputation cases: "
                      << seeded.error << '\n';
            return false;
        }

        // Re-applying the same mode and driver is a no-change.
        marrow::editor::ProjectData idempotent = project;
        const std::string idempotent_snapshot =
            marrow::editor::serialize_project(idempotent);
        const auto again = marrow::editor::set_keyframe_curve_mode(
            &idempotent, {spine_selector(0.0), spine_selector(0.5)},
            TimelineCurveMode::Auto, TimelineScalarComponent::Angle);
        if (!again || again.changed ||
            marrow::editor::serialize_project(idempotent) != idempotent_snapshot) {
            std::cerr << "MAR-171 must report no change when nothing moved.\n";
            return false;
        }

        // Writing an absolute easing demotes, and reports the change even when
        // the four control points are byte-identical - the demotion IS the
        // change. This is deliberately different from MAR-169's net-state rule
        // for a manual key, which still holds below.
        marrow::editor::ProjectData demoted = project;
        const auto identical = marrow::editor::set_keyframe_interpolation(
            &demoted, {spine_selector(0.0)}, InterpolationKind::CubicBezier,
            {narrowed(kSegment0[0]), narrowed(kSegment0[1]),
             narrowed(kSegment0[2]), narrowed(kSegment0[3])});
        const auto* demoted_track = spine_rotate(demoted);
        if (!identical || !identical.changed || identical.changed_key_count != 1U ||
            demoted_track->keyframes[0].curve_mode != TimelineCurveMode::Manual ||
            !curve_is(demoted_track->keyframes[0].interpolation, kSegment0)) {
            std::cerr << "MAR-171 demotion must report a change on byte-identical points.\n";
            return false;
        }
        const std::string demoted_snapshot = marrow::editor::serialize_project(demoted);
        const auto after_demotion =
            marrow::editor::resolve_automatic_curves(&demoted, "idle");
        if (!after_demotion || after_demotion.resolved_key_count != 0U ||
            marrow::editor::serialize_project(demoted) != demoted_snapshot) {
            std::cerr << "MAR-171 resolver must leave a demoted key alone.\n";
            return false;
        }
        // MAR-169's rule is preserved for a manual key: a byte-identical
        // rewrite still reports no change.
        const auto manual_rewrite = marrow::editor::set_keyframe_interpolation(
            &demoted, {spine_selector(0.0)}, InterpolationKind::CubicBezier,
            {narrowed(kSegment0[0]), narrowed(kSegment0[1]),
             narrowed(kSegment0[2]), narrowed(kSegment0[3])});
        if (manual_rewrite.changed || manual_rewrite.changed_key_count != 0U) {
            std::cerr << "MAR-171 must not break MAR-169's net-state rule for manual keys.\n";
            return false;
        }

    }

    // --- Neighbour recomputation, over a monotone driver whose curve really
    // --- depends on the spacing and the values. ---------------------------
    {
        // The fixture's spine rotate series peaks at key 1, and Fritsch-Carlson
        // zeroes a local extremum's tangent whatever the spacing is, so this
        // case needs a strictly monotone driver to be able to observe a change
        // at all.
        marrow::editor::ProjectData base = *project_result.project;
        marrow::editor::TransformTimelineEdit ramp;
        ramp.animation_name = "idle";
        ramp.bone_name = "spine";
        ramp.channel = TransformTimelineChannel::Translate;
        ramp.keyframes.push_back(
            {0.0, 0.0, 0.0, 0.0, marrow::runtime::Interpolation::linear()});
        ramp.keyframes.push_back(
            {0.5, 0.0, 4.0, 0.0, marrow::runtime::Interpolation::linear()});
        ramp.keyframes.push_back(
            {1.0, 0.0, 10.0, 0.0, marrow::runtime::Interpolation::linear()});
        base.transform_timeline_edits.push_back(std::move(ramp));

        TimelineKeySelector ramp_key;
        ramp_key.kind = TimelineKeyKind::Transform;
        ramp_key.animation_name = "idle";
        ramp_key.bone_name = "spine";
        ramp_key.transform_channel = TransformTimelineChannel::Translate;
        const auto ramp_selector = [&](double time) {
            TimelineKeySelector selector = ramp_key;
            selector.time = time;
            return selector;
        };
        const auto ramp_track = [](const marrow::editor::ProjectData& project) {
            return project.find_transform_timeline_edit(
                "idle", "spine", TransformTimelineChannel::Translate);
        };
        const auto seeded = marrow::editor::set_keyframe_curve_mode(
            &base, {ramp_selector(0.0), ramp_selector(0.5)},
            TimelineCurveMode::Auto, TimelineScalarComponent::X);
        if (!seeded || seeded.resolved_key_count != 2U) {
            std::cerr << "MAR-171 could not seed the monotone ramp: " << seeded.error << '\n';
            return false;
        }
        const marrow::runtime::Interpolation seeded_first =
            ramp_track(base)->keyframes[0].interpolation;
        const marrow::runtime::Interpolation seeded_second =
            ramp_track(base)->keyframes[1].interpolation;
        const auto same_easing = [](const marrow::runtime::Interpolation& left,
                                    const marrow::runtime::Interpolation& right) {
            if (left.kind() != right.kind()) return false;
            if (left.kind() != InterpolationKind::CubicBezier) return true;
            return left.cubic_bezier().cx1 == right.cubic_bezier().cx1 &&
                left.cubic_bezier().cy1 == right.cubic_bezier().cy1 &&
                left.cubic_bezier().cx2 == right.cubic_bezier().cx2 &&
                left.cubic_bezier().cy2 == right.cubic_bezier().cy2;
        };

        // A retime of the middle key changes the spacing, so both neighbouring
        // segments resolve to different values inside the same call.
        marrow::editor::ProjectData retimed = base;
        const auto retime = marrow::editor::retime_keyframes(
            &retimed, {ramp_selector(0.5)}, 0.25, false, 30.0);
        if (!retime || !retime.changed) {
            std::cerr << "MAR-171 could not retime the middle key: " << retime.error << '\n';
            return false;
        }
        const auto retimed_resolve =
            marrow::editor::resolve_automatic_curves(&retimed, "idle");
        if (!retimed_resolve || retimed_resolve.auto_key_count != 2U ||
            retimed_resolve.resolved_key_count != 2U) {
            std::cerr << "MAR-171 did not re-resolve both segments after a neighbour retime: "
                      << retimed_resolve.error << '\n';
            return false;
        }
        if (same_easing(ramp_track(retimed)->keyframes[0].interpolation, seeded_first) ||
            same_easing(ramp_track(retimed)->keyframes[1].interpolation, seeded_second)) {
            std::cerr << "MAR-171 left a curve unchanged after a retime.\n";
            return false;
        }

        // A value change on the middle key does the same.
        marrow::editor::ProjectData offset_project = base;
        const auto offset = marrow::editor::offset_keyframe_scalars(
            &offset_project, {ramp_selector(0.5)}, TimelineScalarComponent::X, 20.0);
        if (!offset || !offset.changed) {
            std::cerr << "MAR-171 could not offset the middle key: " << offset.error << '\n';
            return false;
        }
        const auto offset_resolve =
            marrow::editor::resolve_automatic_curves(&offset_project, "idle");
        if (!offset_resolve || offset_resolve.resolved_key_count != 2U ||
            same_easing(
                ramp_track(offset_project)->keyframes[0].interpolation, seeded_first) ||
            same_easing(
                ramp_track(offset_project)->keyframes[1].interpolation, seeded_second)) {
            std::cerr << "MAR-171 did not re-resolve after a neighbour value change.\n";
            return false;
        }

        // Explicit reconciliation: a neighbour perturbed without resolving is
        // repaired by re-applying the mode, which reports resolver work with no
        // intent change at all.
        marrow::editor::ProjectData stale = base;
        auto* stale_track = stale.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Translate);
        stale_track->keyframes[1].x = 9.5;
        const auto reconciled = marrow::editor::set_keyframe_curve_mode(
            &stale, {ramp_selector(0.0)}, TimelineCurveMode::Auto,
            TimelineScalarComponent::X);
        if (!reconciled || !reconciled.changed || reconciled.changed_key_count != 0U ||
            reconciled.resolved_key_count == 0U) {
            std::cerr << "MAR-171 explicit reconciliation must report resolver work only.\n";
            return false;
        }
    }

    // --- Fail closed on a zero-duration segment ---------------------------
    {
        marrow::editor::ProjectData project = *project_result.project;
        // An EARLIER track carries automatic keys whose stored curves the
        // resolver would rewrite, so a resolver that wrote before it rejected
        // would leave those rewrites behind and this case would see them.
        auto* seeded_track = project.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        seeded_track->keyframes[0].curve_mode = TimelineCurveMode::Auto;
        seeded_track->keyframes[0].curve_driver = TimelineScalarComponent::Angle;
        seeded_track->keyframes[1].curve_mode = TimelineCurveMode::Auto;
        seeded_track->keyframes[1].curve_driver = TimelineScalarComponent::Angle;
        marrow::editor::TransformTimelineEdit degenerate;
        degenerate.animation_name = "idle";
        degenerate.bone_name = "arm_l";
        degenerate.channel = TransformTimelineChannel::Translate;
        degenerate.keyframes.push_back(
            {0.0, 0.0, 1.0, 2.0, marrow::runtime::Interpolation::linear()});
        degenerate.keyframes.push_back(
            {1e-7, 0.0, 5.0, 6.0, marrow::runtime::Interpolation::linear()});
        degenerate.keyframes.back().curve_mode = TimelineCurveMode::Auto;
        degenerate.keyframes.front().curve_mode = TimelineCurveMode::Auto;
        degenerate.keyframes.front().curve_driver = TimelineScalarComponent::X;
        degenerate.keyframes.back().curve_driver = TimelineScalarComponent::X;
        project.transform_timeline_edits.push_back(std::move(degenerate));
        const std::string snapshot = marrow::editor::serialize_project(project);
        // Sanity: without the degenerate track the resolver really does
        // rewrite the seeded curves, so the atomicity assertion below has
        // something to observe.
        {
            marrow::editor::ProjectData healthy = project;
            healthy.transform_timeline_edits.pop_back();
            const auto healthy_resolve =
                marrow::editor::resolve_automatic_curves(&healthy, "idle");
            if (!healthy_resolve || healthy_resolve.resolved_key_count == 0U) {
                std::cerr << "MAR-171 atomicity case has nothing to leak.\n";
                return false;
            }
        }
        marrow::editor::ProjectData candidate = project;
        const auto rejected =
            marrow::editor::resolve_automatic_curves(&candidate, "idle");
        if (rejected || rejected.error.empty() ||
            marrow::editor::serialize_project(candidate) != snapshot) {
            std::cerr << "MAR-171 must reject a zero-duration segment atomically.\n";
            return false;
        }
        if (rejected.error.find("idle") == std::string::npos ||
            rejected.error.find("arm_l") == std::string::npos) {
            std::cerr << "MAR-171 resolver error must name the animation and track: "
                      << rejected.error << '\n';
            return false;
        }
    }

    // --- A project with no automatic key is never touched -----------------
    {
        marrow::editor::ProjectData project = *project_result.project;
        const std::string snapshot = marrow::editor::serialize_project(project);
        const auto resolved = marrow::editor::resolve_automatic_curves(&project, {});
        if (!resolved || resolved.changed || resolved.auto_key_count != 0U ||
            resolved.resolved_key_count != 0U ||
            marrow::editor::serialize_project(project) != snapshot) {
            std::cerr << "MAR-171 whole-project resolve must skip a project with no auto key.\n";
            return false;
        }
    }

    // --- Load never resolves: a stale mode/curve pair is legal data --------
    {
        const auto stale_path = std::filesystem::temp_directory_path() /
            ("marrow_mar171_stale_" + path_token + ".marrow");
        marrow::editor::ProjectData project = rebase(stale_path);
        auto* track = project.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        track->keyframes[0].curve_mode = TimelineCurveMode::Auto;
        track->keyframes[0].curve_driver = TimelineScalarComponent::Angle;
        // Deliberately not the curve the resolver would produce.
        track->keyframes[0].interpolation =
            marrow::runtime::Interpolation::cubic_bezier(0.25, 0.1, 0.75, 0.9);
        const std::string stale_text = marrow::editor::serialize_project(project);
        const auto saved = marrow::editor::save_project(project, stale_path);
        if (!saved) {
            std::cerr << saved.error->format() << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(stale_path);
        std::filesystem::remove(stale_path, ignored);
        if (!reloaded) {
            std::cerr << reloaded.error->format();
            return false;
        }
        if (marrow::editor::serialize_project(*reloaded.project) != stale_text) {
            std::cerr << "MAR-171 load must not repair a stale mode/curve pair.\n";
            return false;
        }
        marrow::editor::ProjectData reconciled = *reloaded.project;
        const auto forced = marrow::editor::set_keyframe_curve_mode(
            &reconciled, {spine_selector(0.0)}, TimelineCurveMode::Auto,
            TimelineScalarComponent::Angle);
        const auto* reconciled_track = spine_rotate(reconciled);
        if (!forced || !forced.changed || forced.resolved_key_count == 0U ||
            !curve_is(reconciled_track->keyframes[0].interpolation, kSegment0)) {
            std::cerr << "MAR-171 explicit reconciliation must rewrite a stale curve.\n";
            return false;
        }
    }

    // --- The duration trigger is wired and is honestly a no-op today -------
    {
        marrow::editor::ProjectData project = *project_result.project;
        const auto seeded = marrow::editor::set_keyframe_curve_mode(
            &project, {spine_selector(0.0), spine_selector(0.5)},
            TimelineCurveMode::Auto, TimelineScalarComponent::Angle);
        if (!seeded) {
            std::cerr << "MAR-171 could not seed the duration case: " << seeded.error << '\n';
            return false;
        }
        const std::string before = marrow::editor::serialize_project(project);
        const auto duration = marrow::editor::set_animation_duration(
            &project, *project_result.skeleton_data, "idle", 2.5);
        if (!duration || !duration.changed) {
            std::cerr << "MAR-171 could not author a duration: " << duration.error << '\n';
            return false;
        }
        const auto resolved = marrow::editor::resolve_automatic_curves(&project, "idle");
        if (!resolved || resolved.changed || resolved.resolved_key_count != 0U) {
            std::cerr << "MAR-171 a duration change must resolve nothing today, "
                         "with no lane opted in.\n";
            return false;
        }
        (void)before;
    }

    // ------------------------------------------------------------------
    // Export. MAR-168 and MAR-169 both shipped an export criterion whose test
    // mutated a ProjectData copy that never reached the exporter, so this block
    // exports the MUTATED project, asserts the resolved control points arrived
    // in the runtime file, asserts the two project-local fields did NOT, and
    // compares against a baseline measured in the same run.
    // ------------------------------------------------------------------
    {
        const std::filesystem::path auto_json_path = "/tmp/marrow_mar171_auto.mskl";
        const std::filesystem::path auto_binary_path = "/tmp/marrow_mar171_auto.mbin";
        const std::filesystem::path baseline_json_path =
            "/tmp/marrow_mar171_export_baseline.mskl";
        const std::filesystem::path baseline_binary_path =
            "/tmp/marrow_mar171_export_baseline.mbin";

        marrow::editor::ProjectExportOptions baseline_options;
        baseline_options.skeleton_output_path = baseline_json_path;
        baseline_options.binary_output_path = baseline_binary_path;
        const auto baseline_export = marrow::editor::export_runtime_assets(
            *project_result.project,
            *project_result.base_skeleton_document,
            baseline_options);
        if (!baseline_export) {
            std::cerr << baseline_export.error->format() << '\n';
            return false;
        }

        marrow::editor::ProjectData exported_project = *project_result.project;
        TimelineKeySelector arm_key;
        arm_key.kind = TimelineKeyKind::Transform;
        arm_key.animation_name = "idle";
        arm_key.bone_name = "arm_l";
        arm_key.transform_channel = TransformTimelineChannel::Rotate;
        arm_key.time = 0.25;
        const auto authored = marrow::editor::set_keyframe_curve_mode(
            &exported_project,
            {spine_selector(0.0), spine_selector(0.5), arm_key},
            TimelineCurveMode::Auto,
            TimelineScalarComponent::Angle);
        if (!authored || !authored.changed || authored.resolved_key_count != 3U) {
            std::cerr << "MAR-171 export block could not author its automatic keys: "
                      << authored.error << '\n';
            return false;
        }

        marrow::editor::ProjectExportOptions auto_options;
        auto_options.skeleton_output_path = auto_json_path;
        auto_options.binary_output_path = auto_binary_path;
        const auto auto_export = marrow::editor::export_runtime_assets(
            exported_project, *project_result.base_skeleton_document, auto_options);
        if (!auto_export) {
            std::cerr << auto_export.error->format() << '\n';
            return false;
        }

        const auto exported = marrow::runtime::load_skeleton_data(auto_json_path);
        if (!exported) {
            std::cerr << exported.error->format();
            return false;
        }
        const auto* exported_idle = exported.skeleton_data->find_animation("idle");
        const auto spine_index = exported.skeleton_data->find_bone_index("spine");
        const auto arm_index = exported.skeleton_data->find_bone_index("arm_l");
        const auto* exported_spine =
            exported_idle != nullptr && spine_index.has_value()
            ? exported_idle->find_rotate_timeline(*spine_index)
            : nullptr;
        const auto* exported_arm = exported_idle != nullptr && arm_index.has_value()
            ? exported_idle->find_rotate_timeline(*arm_index)
            : nullptr;
        if (exported_spine == nullptr || exported_spine->keyframes.empty() ||
            exported_arm == nullptr || exported_arm->keyframes.empty()) {
            std::cerr << "MAR-171 export did not carry the two rotate timelines.\n";
            return false;
        }
        const auto& spine_easing = exported_spine->keyframes.front().interpolation;
        if (spine_easing.kind() != InterpolationKind::CubicBezier ||
            spine_easing.cubic_bezier().cx1 !=
                static_cast<marrow::runtime::AnimationScalar>(kThird) ||
            spine_easing.cubic_bezier().cy1 !=
                static_cast<marrow::runtime::AnimationScalar>(kThird) ||
            spine_easing.cubic_bezier().cx2 !=
                static_cast<marrow::runtime::AnimationScalar>(kTwoThirds) ||
            spine_easing.cubic_bezier().cy2 != 1.0f) {
            std::cerr << "MAR-171 resolved spine curve did not reach the exported .mskl.\n";
            return false;
        }
        // The fixture stores `"curve": "linear"` on this key today, so a
        // 4-number array here is a visible, byte-level conversion.
        if (exported_arm->keyframes.front().interpolation.kind() !=
            InterpolationKind::CubicBezier) {
            std::cerr << "MAR-171 did not convert arm_l's `\"linear\"` string to an array.\n";
            return false;
        }

        std::ifstream exported_text(auto_json_path, std::ios::binary);
        const std::string export_body(
            (std::istreambuf_iterator<char>(exported_text)),
            std::istreambuf_iterator<char>());
        if (export_body.empty()) {
            std::cerr << "MAR-171 could not read the exported .mskl as text.\n";
            return false;
        }
        if (export_body.find("curve_mode") != std::string::npos ||
            export_body.find("curve_driver") != std::string::npos) {
            std::cerr << "MAR-171 leaked a project-local field into the runtime export.\n";
            return false;
        }

        if (!auto_export.binary_path.has_value() ||
            !validate_binary_export(auto_export.path, *auto_export.binary_path)) {
            std::cerr << "MAR-171 auto export did not match its v2 binary payload.\n";
            return false;
        }

        std::error_code size_error;
        const auto auto_json_size = std::filesystem::file_size(auto_json_path, size_error);
        const auto auto_binary_size =
            std::filesystem::file_size(auto_binary_path, size_error);
        const auto baseline_json_size =
            std::filesystem::file_size(baseline_json_path, size_error);
        if (size_error || auto_json_size <= baseline_json_size) {
            // An equal size means the exported artifact is the untouched
            // baseline, which is exactly the defect this block exists to catch.
            std::cerr << "MAR-171 auto export must be strictly larger than the baseline: "
                      << auto_json_size << " vs " << baseline_json_size << '\n';
            return false;
        }
        std::cout << "MAR-171 auto export: JSON " << auto_json_size << " bytes, MBIN "
                  << auto_binary_size << " bytes (baseline JSON " << baseline_json_size
                  << " bytes).\n";
    }

    std::cout << "MAR-171 automatic curve storage validated as additive, "
                 "default-absent, and strictly re-validated.\n";
    return true;
}

bool validate_mar168_graph_scalar_authoring(
    const marrow::editor::ProjectLoadResult& project_result) {
    using marrow::editor::TimelineKeyKind;
    using marrow::editor::TimelineKeySelector;
    using marrow::editor::TimelineScalarComponent;
    using marrow::editor::TransformTimelineChannel;

    constexpr double kExact = 1e-12;
    constexpr double kFloatTolerance = 1e-6;
    const auto near_exact = [](double left, double right) {
        return std::abs(left - right) <= kExact;
    };
    const auto near_float = [](double left, double right) {
        return std::abs(left - right) <= kFloatTolerance;
    };

    const auto make_transform_track = [](std::string bone,
                                         TransformTimelineChannel channel,
                                         std::vector<marrow::editor::TransformKeyframeEdit> keys) {
        marrow::editor::TransformTimelineEdit edit;
        edit.animation_name = "mar168";
        edit.bone_name = std::move(bone);
        edit.channel = channel;
        edit.keyframes = std::move(keys);
        return edit;
    };

    const auto build_project = [&]() {
        marrow::editor::ProjectData project;
        project.transform_timeline_edits.push_back(make_transform_track(
            "spine",
            TransformTimelineChannel::Rotate,
            {{0.0, 10.0, 0.0, 0.0,
              marrow::runtime::Interpolation::cubic_bezier(0.25, 0.1, 0.75, 0.9)},
             {0.5, 20.0, 0.0, 0.0, marrow::runtime::Interpolation::stepped()}}));
        // A bone whose runtime setup pose is rotated: the project stores the
        // setup-relative angle, and a delta must not be converted either way.
        project.transform_timeline_edits.push_back(make_transform_track(
            "transform_source",
            TransformTimelineChannel::Rotate,
            {{0.0, 100.0, 0.0, 0.0, marrow::runtime::Interpolation::linear()}}));
        project.transform_timeline_edits.push_back(make_transform_track(
            "spine",
            TransformTimelineChannel::Translate,
            {{0.0, 0.0, 3.0, 7.0,
              marrow::runtime::Interpolation::cubic_bezier(0.3, 0.2, 0.7, 0.8)},
             {0.5, 0.0, 9.0, -2.0, marrow::runtime::Interpolation::linear()}}));
        project.transform_timeline_edits.push_back(make_transform_track(
            "spine",
            TransformTimelineChannel::Scale,
            {{0.0, 0.0, -1.25, 2.0, marrow::runtime::Interpolation::linear()},
             {0.5, 0.0, 0.5, 1.0, marrow::runtime::Interpolation::stepped()}}));
        project.transform_timeline_edits.push_back(make_transform_track(
            "spine",
            TransformTimelineChannel::Shear,
            {{0.0, 0.0, 4.0, -6.0, marrow::runtime::Interpolation::linear()},
             {0.5, 0.0, 8.0, 12.0, marrow::runtime::Interpolation::linear()}}));

        marrow::editor::SlotColorTimelineEdit color;
        color.animation_name = "mar168";
        color.slot_name = "body";
        color.keyframes.push_back(
            {0.0, marrow::runtime::SlotColor{0.25, 0.5, 0.75, 0.4},
             marrow::runtime::Interpolation::cubic_bezier(0.1, 0.2, 0.3, 0.4)});
        color.keyframes.push_back(
            {0.5, marrow::runtime::SlotColor{0.5, 0.25, 0.125, 0.9},
             marrow::runtime::Interpolation::stepped()});
        project.slot_color_timeline_edits.push_back(std::move(color));

        marrow::editor::MeshDeformTimelineEdit deform;
        deform.animation_name = "mar168";
        deform.slot_name = "body";
        deform.attachment_name = "body_mesh";
        deform.keyframes.push_back({0.0, {0.0, 0.0, 1.0, 2.0}, {}});
        project.mesh_deform_timeline_edits.push_back(std::move(deform));

        marrow::editor::DrawOrderTimelineEdit draw_order;
        draw_order.animation_name = "mar168";
        draw_order.keyframes.push_back({0.0, {"body", "arm_l"}});
        project.draw_order_timeline_edits.push_back(std::move(draw_order));

        marrow::editor::EventTimelineEdit events;
        events.animation_name = "mar168";
        events.keyframes.push_back(
            {0.0, "footstep", std::nullopt, std::nullopt, std::nullopt,
             std::nullopt, std::nullopt, std::nullopt});
        project.event_timeline_edits.push_back(std::move(events));

        marrow::editor::SlotAttachmentTimelineEdit attachment;
        attachment.animation_name = "mar168";
        attachment.slot_name = "body";
        attachment.keyframes.push_back({0.0, std::string("body")});
        project.slot_attachment_timeline_edits.push_back(std::move(attachment));
        return project;
    };

    const auto transform_selector = [](std::string bone,
                                       TransformTimelineChannel channel,
                                       double time) {
        TimelineKeySelector selector;
        selector.kind = TimelineKeyKind::Transform;
        selector.animation_name = "mar168";
        selector.bone_name = std::move(bone);
        selector.transform_channel = channel;
        selector.time = time;
        return selector;
    };
    const auto color_selector = [](double time) {
        TimelineKeySelector selector;
        selector.kind = TimelineKeyKind::SlotColor;
        selector.animation_name = "mar168";
        selector.slot_name = "body";
        selector.time = time;
        return selector;
    };

    // One case per supported lane family: the named component moves by exactly
    // the delta and every sibling field, the time, and the easing survive.
    struct TransformCase {
        const char* label;
        TransformTimelineChannel channel;
        TimelineScalarComponent component;
        double delta;
    };
    const TransformCase transform_cases[] = {
        {"Rotate Angle", TransformTimelineChannel::Rotate,
         TimelineScalarComponent::Angle, -12.5},
        {"Translate X", TransformTimelineChannel::Translate,
         TimelineScalarComponent::X, 3.5},
        {"Translate Y", TransformTimelineChannel::Translate,
         TimelineScalarComponent::Y, -1.25},
        {"Scale X", TransformTimelineChannel::Scale,
         TimelineScalarComponent::X, 0.75},
        {"Scale Y", TransformTimelineChannel::Scale,
         TimelineScalarComponent::Y, -0.5},
        {"Shear X", TransformTimelineChannel::Shear,
         TimelineScalarComponent::X, 2.0},
        {"Shear Y", TransformTimelineChannel::Shear,
         TimelineScalarComponent::Y, 7.5},
    };
    for (const TransformCase& scenario : transform_cases) {
        marrow::editor::ProjectData project = build_project();
        const marrow::editor::TransformTimelineEdit* source =
            project.find_transform_timeline_edit("mar168", "spine", scenario.channel);
        if (source == nullptr || source->keyframes.empty()) {
            std::cerr << "MAR-168 case " << scenario.label
                      << " is missing its source timeline.\n";
            return false;
        }
        const marrow::editor::TransformKeyframeEdit original = source->keyframes.front();
        const std::size_t original_key_count = source->keyframes.size();
        const std::string before = marrow::editor::serialize_project(project);
        const auto moved = marrow::editor::offset_keyframe_scalars(
            &project,
            {transform_selector("spine", scenario.channel, 0.0)},
            scenario.component,
            scenario.delta);
        if (!moved || !moved.changed || moved.key_count != 1U ||
            !near_exact(moved.applied_delta, scenario.delta)) {
            std::cerr << "MAR-168 could not offset a " << scenario.label << " key: "
                      << moved.error << '\n';
            return false;
        }
        const marrow::editor::TransformTimelineEdit* edited =
            project.find_transform_timeline_edit("mar168", "spine", scenario.channel);
        if (edited == nullptr || edited->keyframes.size() != original_key_count) {
            std::cerr << "MAR-168 " << scenario.label
                      << " offset reshaped its timeline.\n";
            return false;
        }
        const marrow::editor::TransformKeyframeEdit& key = edited->keyframes.front();
        const double expected_angle = original.angle +
            (scenario.component == TimelineScalarComponent::Angle ? scenario.delta : 0.0);
        const double expected_x = original.x +
            (scenario.component == TimelineScalarComponent::X ? scenario.delta : 0.0);
        const double expected_y = original.y +
            (scenario.component == TimelineScalarComponent::Y ? scenario.delta : 0.0);
        if (!near_exact(key.angle, expected_angle) || !near_exact(key.x, expected_x) ||
            !near_exact(key.y, expected_y) || !near_exact(key.time, original.time) ||
            key.interpolation.kind() != original.interpolation.kind()) {
            std::cerr << "MAR-168 " << scenario.label
                      << " scalar offset did not preserve the parent key.\n";
            return false;
        }
        if (marrow::editor::serialize_project(project) == before) {
            std::cerr << "MAR-168 " << scenario.label
                      << " offset reported a change it did not persist.\n";
            return false;
        }
    }

    struct ColorCase {
        const char* label;
        TimelineScalarComponent component;
        int channel_index;
    };
    const ColorCase color_cases[] = {
        {"Slot Color R", TimelineScalarComponent::Red, 0},
        {"Slot Color G", TimelineScalarComponent::Green, 1},
        {"Slot Color B", TimelineScalarComponent::Blue, 2},
        {"Slot Color A", TimelineScalarComponent::Alpha, 3},
    };
    const auto color_channel = [](const marrow::runtime::SlotColor& color, int index) {
        switch (index) {
        case 0: return static_cast<double>(color.r);
        case 1: return static_cast<double>(color.g);
        case 2: return static_cast<double>(color.b);
        default: return static_cast<double>(color.a);
        }
    };
    for (const ColorCase& scenario : color_cases) {
        marrow::editor::ProjectData project = build_project();
        const marrow::editor::SlotColorTimelineEdit* source =
            project.find_slot_color_timeline_edit("mar168", "body");
        if (source == nullptr || source->keyframes.empty()) {
            std::cerr << "MAR-168 case " << scenario.label
                      << " is missing its colour timeline.\n";
            return false;
        }
        const marrow::editor::SlotColorKeyframeEdit original = source->keyframes.front();
        const auto moved = marrow::editor::offset_keyframe_scalars(
            &project, {color_selector(0.0)}, scenario.component, 0.05);
        if (!moved || !moved.changed || moved.key_count != 1U ||
            !near_exact(moved.applied_delta, 0.05)) {
            std::cerr << "MAR-168 could not offset a " << scenario.label << " key: "
                      << moved.error << '\n';
            return false;
        }
        const marrow::editor::SlotColorTimelineEdit* edited =
            project.find_slot_color_timeline_edit("mar168", "body");
        if (edited == nullptr) {
            std::cerr << "MAR-168 " << scenario.label << " lost its colour timeline.\n";
            return false;
        }
        const marrow::editor::SlotColorKeyframeEdit& key = edited->keyframes.front();
        bool preserved = near_float(key.time, original.time) &&
            key.interpolation.kind() == original.interpolation.kind();
        for (int index = 0; index < 4; ++index) {
            const double expected = color_channel(original.color, index) +
                (index == scenario.channel_index ? 0.05 : 0.0);
            preserved = preserved && near_float(color_channel(key.color, index), expected);
        }
        if (!preserved) {
            std::cerr << "MAR-168 " << scenario.label
                      << " offset did not preserve its sibling channels.\n";
            return false;
        }
    }

    // Rotate carries no setup-pose conversion: a delta is identical in the
    // absolute space the graph plots and the setup-relative space the project
    // stores, so the stored angle moves by exactly the requested amount.
    {
        marrow::editor::ProjectData project = build_project();
        const auto setup_bone =
            project_result.skeleton_data->find_bone_index("transform_source");
        if (!setup_bone.has_value()) {
            std::cerr << "MAR-168 requires a fixture bone with a setup rotation.\n";
            return false;
        }
        const double setup_rotation =
            project_result.skeleton_data->bones()[*setup_bone].setup_pose.rotation;
        const auto moved = marrow::editor::offset_keyframe_scalars(
            &project,
            {transform_selector(
                "transform_source", TransformTimelineChannel::Rotate, 0.0)},
            TimelineScalarComponent::Angle,
            10.0);
        const auto* edited = project.find_transform_timeline_edit(
            "mar168", "transform_source", TransformTimelineChannel::Rotate);
        if (!moved || !moved.changed || edited == nullptr ||
            edited->keyframes.size() != 1U ||
            !near_exact(edited->keyframes.front().angle, 110.0) ||
            std::abs(setup_rotation) <= kExact) {
            std::cerr << "MAR-168 Rotate offset applied a setup-pose conversion.\n";
            return false;
        }
    }

    // Signed scale: a negative value stays negative and exact zero stays
    // authorable, which the MAR-162 signed local scale gizmo depends on.
    {
        marrow::editor::ProjectData project = build_project();
        const auto negative = marrow::editor::offset_keyframe_scalars(
            &project,
            {transform_selector("spine", TransformTimelineChannel::Scale, 0.0)},
            TimelineScalarComponent::X,
            -0.25);
        const auto zeroed = marrow::editor::offset_keyframe_scalars(
            &project,
            {transform_selector("spine", TransformTimelineChannel::Scale, 0.5)},
            TimelineScalarComponent::X,
            -0.5);
        const auto* edited = project.find_transform_timeline_edit(
            "mar168", "spine", TransformTimelineChannel::Scale);
        if (!negative || !zeroed || edited == nullptr ||
            edited->keyframes.size() != 2U ||
            !near_exact(edited->keyframes[0].x, -1.5) ||
            edited->keyframes[0].x >= 0.0 || edited->keyframes[1].x != 0.0) {
            std::cerr << "MAR-168 signed scale offset lost its sign or exact zero.\n";
            return false;
        }
    }

    // Slot Colour clamps group-wide so a multi-key drag stops as one unit.
    {
        marrow::editor::ProjectData project = build_project();
        const auto clamped = marrow::editor::offset_keyframe_scalars(
            &project,
            {color_selector(0.0), color_selector(0.5)},
            TimelineScalarComponent::Alpha,
            0.5);
        const auto* edited = project.find_slot_color_timeline_edit("mar168", "body");
        if (!clamped || !clamped.changed || clamped.key_count != 2U ||
            !near_float(clamped.applied_delta, 0.1) || edited == nullptr ||
            edited->keyframes.size() != 2U ||
            !near_float(edited->keyframes[0].color.a, 0.5) ||
            !near_float(edited->keyframes[1].color.a, 1.0) ||
            !near_float(
                static_cast<double>(edited->keyframes[1].color.a) -
                    static_cast<double>(edited->keyframes[0].color.a),
                0.5)) {
            std::cerr << "MAR-168 group colour clamp did not stop both keys together.\n";
            return false;
        }
    }

    // Imported data already outside [0, 1] yields a no-op frame, not an error.
    {
        marrow::editor::ProjectData project = build_project();
        marrow::editor::SlotColorTimelineEdit* degenerate =
            project.find_slot_color_timeline_edit("mar168", "body");
        if (degenerate == nullptr || degenerate->keyframes.size() != 2U) {
            std::cerr << "MAR-168 degenerate clamp case is missing its colour keys.\n";
            return false;
        }
        degenerate->keyframes[0].color.a = 1.4f;
        degenerate->keyframes[1].color.a = 0.2f;
        const std::string before = marrow::editor::serialize_project(project);
        const auto degenerate_result = marrow::editor::offset_keyframe_scalars(
            &project,
            {color_selector(0.0), color_selector(0.5)},
            TimelineScalarComponent::Alpha,
            0.3);
        if (degenerate_result.changed || !degenerate_result.error.empty() ||
            !near_exact(degenerate_result.applied_delta, 0.0) ||
            degenerate_result.key_count != 2U ||
            marrow::editor::serialize_project(project) != before) {
            std::cerr << "MAR-168 degenerate colour clamp did not stay a silent no-op.\n";
            return false;
        }
    }

    // Out-of-range imported colour must never be pushed further out, and a
    // drag toward the legal range must keep its group spacing.
    {
        struct OutOfRangeCase {
            const char* label;
            double first_alpha;
            double second_alpha;
            bool second_key;
            double requested;
            double expected_applied;
            double expected_first;
            double expected_second;
        };
        const OutOfRangeCase cases[] = {
            {"a lone out-of-range key dragged up", 1.4, 0.0, false, 0.3, 0.0, 1.4, 0.0},
            {"a lone out-of-range key dragged down", 1.4, 0.0, false, -0.3, -0.3, 1.1, 0.0},
            {"a mixed out-of-range group dragged up", 0.2, 1.4, true, 0.3, 0.0, 0.2, 1.4},
            {"a mixed out-of-range group dragged down", 0.2, 1.4, true, -0.3, -0.2, 0.0, 1.2},
        };
        for (const OutOfRangeCase& scenario : cases) {
            marrow::editor::ProjectData project = build_project();
            marrow::editor::SlotColorTimelineEdit* colors =
                project.find_slot_color_timeline_edit("mar168", "body");
            if (colors == nullptr || colors->keyframes.size() != 2U) return false;
            colors->keyframes[0].color.a =
                static_cast<marrow::runtime::AnimationScalar>(scenario.first_alpha);
            colors->keyframes[1].color.a =
                static_cast<marrow::runtime::AnimationScalar>(scenario.second_alpha);
            const std::string before = marrow::editor::serialize_project(project);
            std::vector<TimelineKeySelector> selectors{color_selector(0.0)};
            if (scenario.second_key) selectors.push_back(color_selector(0.5));
            const auto result = marrow::editor::offset_keyframe_scalars(
                &project,
                selectors,
                TimelineScalarComponent::Alpha,
                scenario.requested);
            const auto* edited = project.find_slot_color_timeline_edit("mar168", "body");
            const bool expected_change =
                std::abs(scenario.expected_applied) > kExact;
            if (edited == nullptr || !result.error.empty() ||
                result.changed != expected_change ||
                !near_float(result.applied_delta, scenario.expected_applied) ||
                !near_float(edited->keyframes[0].color.a, scenario.expected_first) ||
                !near_float(edited->keyframes[1].color.a, scenario.expected_second) ||
                (!expected_change &&
                 marrow::editor::serialize_project(project) != before)) {
                std::cerr << "MAR-168 colour clamp mishandled " << scenario.label
                          << ": applied=" << result.applied_delta
                          << " first="
                          << (edited != nullptr ? edited->keyframes[0].color.a : -1.0f)
                          << " second="
                          << (edited != nullptr ? edited->keyframes[1].color.a : -1.0f)
                          << ".\n";
                return false;
            }
        }
    }

    // Rejection atomicity: every failing shape leaves the project byte-identical.
    {
        marrow::editor::ProjectData project = build_project();
        const std::string before = marrow::editor::serialize_project(project);
        TimelineKeySelector deform_selector;
        deform_selector.kind = TimelineKeyKind::Deform;
        deform_selector.animation_name = "mar168";
        deform_selector.slot_name = "body";
        deform_selector.attachment_name = "body_mesh";
        deform_selector.time = 0.0;
        TimelineKeySelector draw_order_selector;
        draw_order_selector.kind = TimelineKeyKind::DrawOrder;
        draw_order_selector.animation_name = "mar168";
        draw_order_selector.time = 0.0;
        TimelineKeySelector event_selector;
        event_selector.kind = TimelineKeyKind::Event;
        event_selector.animation_name = "mar168";
        event_selector.time = 0.0;
        TimelineKeySelector attachment_selector;
        attachment_selector.kind = TimelineKeyKind::SlotAttachment;
        attachment_selector.animation_name = "mar168";
        attachment_selector.slot_name = "body";
        attachment_selector.time = 0.0;

        struct Rejection {
            const char* label;
            std::vector<TimelineKeySelector> selectors;
            TimelineScalarComponent component;
            double delta;
        };
        auto unresolvable =
            transform_selector("spine", TransformTimelineChannel::Translate, 99.0);
        const auto duplicate =
            transform_selector("spine", TransformTimelineChannel::Translate, 0.0);
        const Rejection rejections[] = {
            {"Angle on a Translate channel",
             {transform_selector("spine", TransformTimelineChannel::Translate, 0.0)},
             TimelineScalarComponent::Angle, 1.0},
            {"X on a Rotate channel",
             {transform_selector("spine", TransformTimelineChannel::Rotate, 0.0)},
             TimelineScalarComponent::X, 1.0},
            {"X on a Slot Color track", {color_selector(0.0)},
             TimelineScalarComponent::X, 0.1},
            {"Alpha on a Transform track",
             {transform_selector("spine", TransformTimelineChannel::Translate, 0.0)},
             TimelineScalarComponent::Alpha, 0.1},
            {"a Deform selector", {deform_selector}, TimelineScalarComponent::X, 1.0},
            {"a Draw Order selector", {draw_order_selector},
             TimelineScalarComponent::X, 1.0},
            {"an Event selector", {event_selector}, TimelineScalarComponent::X, 1.0},
            {"a Slot Attachment selector", {attachment_selector},
             TimelineScalarComponent::Alpha, 0.1},
            {"an unresolvable selector", {unresolvable},
             TimelineScalarComponent::X, 1.0},
            {"a duplicated selector", {duplicate, duplicate},
             TimelineScalarComponent::X, 1.0},
            {"a non-finite delta", {duplicate}, TimelineScalarComponent::X,
             std::numeric_limits<double>::quiet_NaN()},
            {"an out-of-float32-range result", {duplicate},
             TimelineScalarComponent::X, 1e39},
            {"an empty selector list", {}, TimelineScalarComponent::X, 1.0},
        };
        for (const Rejection& rejection : rejections) {
            const auto result = marrow::editor::offset_keyframe_scalars(
                &project, rejection.selectors, rejection.component, rejection.delta);
            if (result || result.changed || result.error.empty() ||
                marrow::editor::serialize_project(project) != before) {
                std::cerr << "MAR-168 did not atomically reject " << rejection.label
                          << ".\n";
                return false;
            }
        }
        const auto null_project = marrow::editor::offset_keyframe_scalars(
            nullptr, {duplicate}, TimelineScalarComponent::X, 1.0);
        if (null_project || null_project.error.empty()) {
            std::cerr << "MAR-168 did not reject a null project.\n";
            return false;
        }
    }

    // Save/reload round trip of an offset project.
    {
        const std::filesystem::path round_trip_path =
            "/tmp/marrow_mar168_scalar_offset.marrow";
        marrow::editor::ProjectData project = *project_result.project;
        project.runtime_assets.skeleton_path =
            std::filesystem::absolute(project.resolved_skeleton_path());
        project.runtime_assets.atlas_paths = project.resolved_atlas_paths();
        for (auto& atlas_path : project.runtime_assets.atlas_paths) {
            atlas_path = std::filesystem::absolute(atlas_path);
        }
        project.source_path = round_trip_path;

        TimelineKeySelector fixture_selector;
        fixture_selector.kind = TimelineKeyKind::Transform;
        fixture_selector.animation_name = "idle";
        fixture_selector.bone_name = "arm_l";
        fixture_selector.transform_channel = TransformTimelineChannel::Rotate;
        fixture_selector.time = 0.25;
        const auto* fixture_track = project.find_transform_timeline_edit(
            "idle", "arm_l", TransformTimelineChannel::Rotate);
        if (fixture_track == nullptr || fixture_track->keyframes.empty()) {
            std::cerr << "MAR-168 round trip requires the fixture arm_l rotate keys.\n";
            return false;
        }
        const double original_angle = fixture_track->keyframes.front().angle;
        const auto offset = marrow::editor::offset_keyframe_scalars(
            &project, {fixture_selector}, TimelineScalarComponent::Angle, -17.5);
        if (!offset || !offset.changed) {
            std::cerr << "MAR-168 round trip could not offset the fixture key: "
                      << offset.error << '\n';
            return false;
        }
        const auto saved = marrow::editor::save_project(project, round_trip_path);
        if (!saved) {
            std::cerr << saved.error->format() << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(round_trip_path);
        if (!reloaded) {
            std::cerr << reloaded.error->format();
            return false;
        }
        const auto* reloaded_track = reloaded.project->find_transform_timeline_edit(
            "idle", "arm_l", TransformTimelineChannel::Rotate);
        if (reloaded_track == nullptr || reloaded_track->keyframes.empty() ||
            !near_float(reloaded_track->keyframes.front().angle, original_angle - 17.5)) {
            std::cerr << "MAR-168 offset value did not survive save and reload.\n";
            return false;
        }
        // AC5: the offset must survive all the way into exported JSON and the
        // v2 binary, not only into the reloaded project.
        const std::filesystem::path offset_json_path =
            "/tmp/marrow_mar168_offset.mskl";
        const std::filesystem::path offset_binary_path =
            "/tmp/marrow_mar168_offset.mbin";
        marrow::editor::ProjectExportOptions offset_export_options;
        offset_export_options.skeleton_output_path = offset_json_path;
        offset_export_options.binary_output_path = offset_binary_path;
        const auto offset_export = marrow::editor::export_runtime_assets(
            project, *project_result.base_skeleton_document, offset_export_options);
        if (!offset_export) {
            std::cerr << offset_export.error->format() << '\n';
            return false;
        }
        const auto exported = marrow::runtime::load_skeleton_data(offset_json_path);
        if (!exported) {
            std::cerr << exported.error->format();
            return false;
        }
        const auto exported_arm = exported.skeleton_data->find_bone_index("arm_l");
        const auto* exported_idle = exported.skeleton_data->find_animation("idle");
        const auto* exported_rotate =
            exported_idle != nullptr && exported_arm.has_value()
            ? exported_idle->find_rotate_timeline(*exported_arm)
            : nullptr;
        if (exported_rotate == nullptr || exported_rotate->keyframes.empty() ||
            !near_float(
                static_cast<double>(exported_rotate->keyframes.front().angle),
                original_angle - 17.5)) {
            std::cerr << "MAR-168 offset value did not reach the exported runtime JSON.\n";
            return false;
        }
        if (!offset_export.binary_path.has_value() ||
            !validate_binary_export(offset_export.path, *offset_export.binary_path)) {
            std::cerr << "MAR-168 offset export did not match its v2 binary payload.\n";
            return false;
        }
        std::error_code offset_size_error;
        std::cout << "MAR-168 offset export: JSON "
                  << std::filesystem::file_size(offset_json_path, offset_size_error)
                  << " bytes, MBIN "
                  << std::filesystem::file_size(offset_binary_path, offset_size_error)
                  << " bytes.\n";

        const auto* reloaded_animation = reloaded.skeleton_data->find_animation("idle");
        const auto arm_index = reloaded.skeleton_data->find_bone_index("arm_l");
        const auto* reloaded_rotate =
            reloaded_animation != nullptr && arm_index.has_value()
            ? reloaded_animation->find_rotate_timeline(*arm_index)
            : nullptr;
        if (reloaded_rotate == nullptr || reloaded_rotate->keyframes.empty() ||
            !near_float(
                static_cast<double>(reloaded_rotate->keyframes.front().angle),
                original_angle - 17.5)) {
            std::cerr << "MAR-168 offset value did not reach the rebuilt runtime.\n";
            return false;
        }
    }

    std::cout << "MAR-168 graph scalar authoring validated across "
              << (std::size(transform_cases) + std::size(color_cases))
              << " lane-family cases.\n";
    return true;
}

bool validate_editing_p0_end_to_end(
    const marrow::editor::ProjectLoadResult& project_result) {
    const std::filesystem::path project_path =
        "/tmp/marrow_editing_p0_e2e.marrow";
    const std::filesystem::path json_path =
        "/tmp/marrow_editing_p0_e2e.mskl";
    const std::filesystem::path binary_path =
        "/tmp/marrow_editing_p0_e2e.mbin";

    marrow::editor::ProjectData project = *project_result.project;
    project.runtime_assets.skeleton_path =
        std::filesystem::absolute(project.resolved_skeleton_path());
    project.runtime_assets.atlas_paths = project.resolved_atlas_paths();
    for (auto& atlas_path : project.runtime_assets.atlas_paths) {
        atlas_path = std::filesystem::absolute(atlas_path);
    }
    project.source_path = project_path;

    // Authored as negative zero: `!=` cannot tell -0.0 from 0.0 under
    // IEEE-754, so every gate below must also compare the sign bit or the
    // "signed zero survived" claim is untested.
    const auto mar162_signed_zero_matches = [](double value) {
        return value == 0.0 &&
            std::signbit(value) == std::signbit(kMar162ScaleY);
    };
    const auto interpolation_matches = [](const auto& left, const auto& right) {
        if (left.kind() != right.kind()) {
            return false;
        }
        if (left.kind() != marrow::runtime::InterpolationKind::CubicBezier) {
            return true;
        }
        const auto& left_bezier = left.cubic_bezier();
        const auto& right_bezier = right.cubic_bezier();
        return std::abs(left_bezier.cx1 - right_bezier.cx1) <= 1e-6 &&
            std::abs(left_bezier.cy1 - right_bezier.cy1) <= 1e-6 &&
            std::abs(left_bezier.cx2 - right_bezier.cx2) <= 1e-6 &&
            std::abs(left_bezier.cy2 - right_bezier.cy2) <= 1e-6;
    };

    if (project.find_transform_timeline_edit(
            "idle",
            "spine",
            marrow::editor::TransformTimelineChannel::Translate) != nullptr) {
        std::cerr << "P0 E2E requires a base-only spine translate timeline.\n";
        return false;
    }
    marrow::editor::upsert_transform_keyframe(
        project,
        *project_result.skeleton_data,
        "idle",
        "spine",
        marrow::editor::TransformTimelineChannel::Translate,
        0.25,
        marrow::editor::TransformKeyframePatch{
            std::nullopt,
            3.0,
            55.0});
    const auto* translate_edit = project.find_transform_timeline_edit(
        "idle",
        "spine",
        marrow::editor::TransformTimelineChannel::Translate);
    if (translate_edit == nullptr || translate_edit->keyframes.size() != 4U) {
        std::cerr << "First auto-key discarded imported transform keys.\n";
        return false;
    }

    const auto source_spine_index =
        project_result.skeleton_data->find_bone_index("spine");
    const auto* source_idle =
        project_result.skeleton_data->find_animation("idle");
    const auto* source_scale =
        source_spine_index.has_value() && source_idle != nullptr
        ? source_idle->find_scale_timeline(*source_spine_index)
        : nullptr;
    if (!source_spine_index.has_value() || source_scale == nullptr ||
        project.find_transform_timeline_edit(
            "idle",
            "spine",
            marrow::editor::TransformTimelineChannel::Scale) != nullptr) {
        std::cerr << "MAR-162 scale regression requires a base-only effective scale track.\n";
        return false;
    }
    const auto source_scale_keys = source_scale->keyframes;
    marrow::editor::upsert_transform_keyframe(
        project,
        *project_result.skeleton_data,
        "idle",
        "spine",
        marrow::editor::TransformTimelineChannel::Scale,
        kMar162ScaleTime,
        marrow::editor::TransformKeyframePatch{
            std::nullopt,
            kMar162ScaleX,
            kMar162ScaleY});
    const auto* scale_edit = project.find_transform_timeline_edit(
        "idle",
        "spine",
        marrow::editor::TransformTimelineChannel::Scale);
    bool source_scale_preserved =
        scale_edit != nullptr &&
        scale_edit->keyframes.size() == source_scale_keys.size() + 1U;
    for (const auto& source_key : source_scale_keys) {
        const auto found = scale_edit != nullptr
            ? std::find_if(
                  scale_edit->keyframes.begin(),
                  scale_edit->keyframes.end(),
                  [&](const auto& key) {
                      return std::abs(
                                 key.time -
                                 static_cast<double>(source_key.time)) <= 1e-6;
                  })
            : std::vector<marrow::editor::TransformKeyframeEdit>::const_iterator{};
        if (scale_edit == nullptr || found == scale_edit->keyframes.end() ||
            std::abs(found->x - static_cast<double>(source_key.x)) > 1e-6 ||
            std::abs(found->y - static_cast<double>(source_key.y)) > 1e-6 ||
            !interpolation_matches(found->interpolation, source_key.interpolation)) {
            source_scale_preserved = false;
            break;
        }
    }
    const auto authored_scale_key = scale_edit != nullptr
        ? std::find_if(
              scale_edit->keyframes.begin(),
              scale_edit->keyframes.end(),
              [](const auto& key) {
                  return std::abs(key.time - kMar162ScaleTime) <= 1e-6;
              })
        : std::vector<marrow::editor::TransformKeyframeEdit>::const_iterator{};
    if (!source_scale_preserved || scale_edit == nullptr ||
        authored_scale_key == scale_edit->keyframes.end() ||
        std::abs(authored_scale_key->x - kMar162ScaleX) > 1e-9 ||
        !mar162_signed_zero_matches(authored_scale_key->y)) {
        std::cerr << "MAR-162 scale materialization lost effective keys, curves, or signed zero.\n";
        return false;
    }
    const auto scale_runtime = marrow::editor::build_project_runtime(
        project, *project_result.base_skeleton_document);
    const auto* scale_animation = scale_runtime
        ? scale_runtime.skeleton_data->find_animation("idle")
        : nullptr;
    const auto sampled_scale = scale_animation != nullptr
        ? scale_animation->sample_bone_scale(
              *source_spine_index, kMar162ScaleTime)
        : std::nullopt;
    if (!sampled_scale.has_value() ||
        std::abs(sampled_scale->x - kMar162ScaleX) > 1e-6 ||
        sampled_scale->y != 0.0) {
        std::cerr << "MAR-162 absolute signed scale did not reach the effective runtime.\n";
        return false;
    }

    const auto setup_rotation_index =
        project_result.skeleton_data->find_bone_index("transform_source");
    if (!setup_rotation_index.has_value() ||
        std::abs(
            project_result.skeleton_data->bones()[*setup_rotation_index]
                    .setup_pose.rotation -
                30.0f) > 1e-6f) {
        std::cerr << "P0 rotation regression requires a non-zero setup rotation.\n";
        return false;
    }
    marrow::editor::upsert_transform_keyframe(
        project,
        *project_result.skeleton_data,
        "idle",
        "transform_source",
        marrow::editor::TransformTimelineChannel::Rotate,
        0.25,
        marrow::editor::TransformKeyframePatch{47.0, std::nullopt, std::nullopt});
    // Both sides of the documented epsilon must replace the same key.
    marrow::editor::upsert_transform_keyframe(
        project,
        *project_result.skeleton_data,
        "idle",
        "transform_source",
        marrow::editor::TransformTimelineChannel::Rotate,
        0.2500005,
        marrow::editor::TransformKeyframePatch{48.0, std::nullopt, std::nullopt});
    marrow::editor::upsert_transform_keyframe(
        project,
        *project_result.skeleton_data,
        "idle",
        "transform_source",
        marrow::editor::TransformTimelineChannel::Rotate,
        0.2499995,
        marrow::editor::TransformKeyframePatch{49.0, std::nullopt, std::nullopt});
    const auto* setup_rotation_edit = project.find_transform_timeline_edit(
        "idle",
        "transform_source",
        marrow::editor::TransformTimelineChannel::Rotate);
    if (setup_rotation_edit == nullptr || setup_rotation_edit->keyframes.size() != 1U ||
        std::abs(setup_rotation_edit->keyframes.front().angle - 19.0) > 1e-9) {
        std::cerr << "Absolute rotation upsert did not store one setup-relative key.\n";
        return false;
    }
    const auto rotation_runtime = marrow::editor::build_project_runtime(
        project, *project_result.base_skeleton_document);
    const auto* rotation_animation = rotation_runtime
        ? rotation_runtime.skeleton_data->find_animation("idle")
        : nullptr;
    const auto sampled_rotation = rotation_animation != nullptr
        ? rotation_animation->sample_bone_rotation(*setup_rotation_index, 0.25)
        : std::nullopt;
    if (!sampled_rotation.has_value() ||
        std::abs(*sampled_rotation - 49.0) > 1e-5) {
        std::cerr << "Non-zero setup rotation was applied twice after auto-key.\n";
        return false;
    }

    marrow::editor::upsert_transform_keyframe(
        project,
        *project_result.skeleton_data,
        "idle",
        "transform_source",
        marrow::editor::TransformTimelineChannel::Rotate,
        kMar161RotationTime,
        marrow::editor::TransformKeyframePatch{
            kMar161AbsoluteRotation, std::nullopt, std::nullopt});
    setup_rotation_edit = project.find_transform_timeline_edit(
        "idle",
        "transform_source",
        marrow::editor::TransformTimelineChannel::Rotate);
    const auto multi_turn_project_key = setup_rotation_edit != nullptr
        ? std::find_if(
              setup_rotation_edit->keyframes.begin(),
              setup_rotation_edit->keyframes.end(),
              [](const auto& key) {
                  return std::abs(key.time - kMar161RotationTime) <= 1e-6;
              })
        : std::vector<marrow::editor::TransformKeyframeEdit>::const_iterator{};
    if (setup_rotation_edit == nullptr || setup_rotation_edit->keyframes.size() != 2U ||
        multi_turn_project_key == setup_rotation_edit->keyframes.end() ||
        std::abs(multi_turn_project_key->angle - kMar161RelativeRotation) > 1e-9) {
        std::cerr << "MAR-161 multi-turn rotation was normalized in project authoring data.\n";
        return false;
    }

    auto* slot_color = marrow::editor::ensure_slot_color_timeline_edit(
        project, *project_result.skeleton_data, "idle", "body");
    if (slot_color == nullptr || slot_color->keyframes.size() != 3U) {
        std::cerr << "Slot-color materialization discarded imported keys.\n";
        return false;
    }
    const auto duplicate = marrow::editor::duplicate_animation(
        &project,
        *project_result.base_skeleton_document,
        "idle",
        "editing_p0_copy");
    if (!duplicate) {
        std::cerr << duplicate.error << '\n';
        return false;
    }

    std::vector<marrow::editor::TimelineKeySelector> selectors;
    marrow::editor::TimelineKeySelector transform_selector;
    transform_selector.kind = marrow::editor::TimelineKeyKind::Transform;
    transform_selector.animation_name = "idle";
    transform_selector.bone_name = "spine";
    transform_selector.transform_channel =
        marrow::editor::TransformTimelineChannel::Translate;
    transform_selector.time = 0.25;
    selectors.push_back(transform_selector);
    marrow::editor::TimelineKeySelector color_selector;
    color_selector.kind = marrow::editor::TimelineKeyKind::SlotColor;
    color_selector.animation_name = "idle";
    color_selector.slot_name = "body";
    color_selector.time = 0.5;
    selectors.push_back(color_selector);
    const auto retimed = marrow::editor::retime_keyframes(
        &project, selectors, 0.05, false, 60.0);
    if (!retimed || !retimed.changed || retimed.key_count != 2U ||
        std::abs(retimed.applied_delta - 0.05) > 1e-12) {
        std::cerr << "P0 E2E could not retime transform and slot keys atomically.\n";
        return false;
    }

    const std::string before_failed_retime =
        marrow::editor::serialize_project(project);
    auto invalid_selector = transform_selector;
    invalid_selector.time = 99.0;
    const auto rejected = marrow::editor::retime_keyframes(
        &project,
        {transform_selector, invalid_selector},
        0.1,
        false,
        60.0);
    if (rejected || marrow::editor::serialize_project(project) != before_failed_retime) {
        std::cerr << "Failed multi-key retime was not atomic.\n";
        return false;
    }

    // Snap-to-frames must survive neighbour clamping: when the clamp binds,
    // the key must still land on a frame boundary inside the bounds instead
    // of on the raw 1 ms neighbour offset.
    {
        marrow::editor::ProjectData snap_project;
        marrow::editor::TransformTimelineEdit snap_track;
        snap_track.animation_name = "snap_probe";
        snap_track.bone_name = "spine";
        snap_track.channel = marrow::editor::TransformTimelineChannel::Rotate;
        snap_track.keyframes.push_back({0.0, 0.0, 0.0, 0.0, {}});
        snap_track.keyframes.push_back({0.5, 45.0, 0.0, 0.0, {}});
        snap_project.transform_timeline_edits.push_back(snap_track);

        marrow::editor::TimelineKeySelector snap_selector;
        snap_selector.kind = marrow::editor::TimelineKeyKind::Transform;
        snap_selector.animation_name = "snap_probe";
        snap_selector.bone_name = "spine";
        snap_selector.transform_channel =
            marrow::editor::TransformTimelineChannel::Rotate;
        snap_selector.time = 0.0;

        const auto snapped = marrow::editor::retime_keyframes(
            &snap_project, {snap_selector}, 0.6, true, 30.0);
        const double frame_seconds = 1.0 / 30.0;
        const double snapped_frames = snapped.applied_delta * 30.0;
        if (!snapped || !snapped.changed ||
            std::abs(snapped_frames - std::round(snapped_frames)) > 1e-9 ||
            snapped.applied_delta > (0.5 - 0.001) + 1e-12 ||
            snapped.applied_delta <= 0.0) {
            std::cerr << "Clamped frame-snap retime left the key off the frame grid.\n";
            return false;
        }
        (void)frame_seconds;
    }

    const auto saved = marrow::editor::save_project(project, project_path);
    if (!saved) {
        std::cerr << saved.error->format() << '\n';
        return false;
    }
    const auto reloaded = marrow::editor::load_project(project_path);
    if (!reloaded) {
        std::cerr << reloaded.error->format();
        return false;
    }
    const auto spine_index = reloaded.skeleton_data->find_bone_index("spine");
    const auto reloaded_rotation_index =
        reloaded.skeleton_data->find_bone_index("transform_source");
    const auto* idle = reloaded.skeleton_data->find_animation("idle");
    const auto* translated =
        spine_index.has_value() && idle != nullptr
        ? idle->find_translate_timeline(*spine_index)
        : nullptr;
    const auto* reloaded_scale =
        spine_index.has_value() && idle != nullptr
        ? idle->find_scale_timeline(*spine_index)
        : nullptr;
    const auto reloaded_scale_sample =
        spine_index.has_value() && idle != nullptr
        ? idle->sample_bone_scale(*spine_index, kMar162ScaleTime)
        : std::nullopt;
    const auto* reloaded_scale_edit =
        reloaded.project->find_transform_timeline_edit(
            "idle",
            "spine",
            marrow::editor::TransformTimelineChannel::Scale);
    const auto reloaded_scale_key = reloaded_scale_edit != nullptr
        ? std::find_if(
              reloaded_scale_edit->keyframes.begin(),
              reloaded_scale_edit->keyframes.end(),
              [](const auto& key) {
                  return std::abs(key.time - kMar162ScaleTime) <= 1e-6;
              })
        : std::vector<marrow::editor::TransformKeyframeEdit>::const_iterator{};
    bool reloaded_source_scale_preserved =
        reloaded_scale != nullptr &&
        reloaded_scale->keyframes.size() == source_scale_keys.size() + 1U;
    for (const auto& source_key : source_scale_keys) {
        const auto found = reloaded_scale != nullptr
            ? std::find_if(
                  reloaded_scale->keyframes.begin(),
                  reloaded_scale->keyframes.end(),
                  [&](const auto& key) {
                      return std::abs(
                                 static_cast<double>(key.time) -
                                 static_cast<double>(source_key.time)) <= 1e-6;
                  })
            : std::vector<marrow::runtime::VectorKeyframe>::const_iterator{};
        if (reloaded_scale == nullptr || found == reloaded_scale->keyframes.end() ||
            std::abs(
                static_cast<double>(found->x) -
                static_cast<double>(source_key.x)) > 1e-6 ||
            std::abs(
                static_cast<double>(found->y) -
                static_cast<double>(source_key.y)) > 1e-6 ||
            !interpolation_matches(found->interpolation, source_key.interpolation)) {
            reloaded_source_scale_preserved = false;
            break;
        }
    }
    const auto* reloaded_rotation =
        reloaded_rotation_index.has_value() && idle != nullptr
        ? idle->find_rotate_timeline(*reloaded_rotation_index)
        : nullptr;
    const auto reloaded_multi_turn_sample =
        reloaded_rotation_index.has_value() && idle != nullptr
        ? idle->sample_bone_rotation(
              *reloaded_rotation_index, kMar161RotationTime)
        : std::nullopt;
    const auto* reloaded_rotation_edit = reloaded.project->find_transform_timeline_edit(
        "idle",
        "transform_source",
        marrow::editor::TransformTimelineChannel::Rotate);
    const bool reloaded_raw_multi_turn =
        reloaded_rotation_edit != nullptr &&
        std::any_of(
            reloaded_rotation_edit->keyframes.begin(),
            reloaded_rotation_edit->keyframes.end(),
            [](const auto& key) {
                return std::abs(key.time - kMar161RotationTime) <= 1e-6 &&
                    std::abs(key.angle - kMar161RelativeRotation) <= 1e-9;
            });
    if (translated == nullptr || translated->keyframes.size() != 4U ||
        reloaded_scale_edit == nullptr ||
        reloaded_scale_key == reloaded_scale_edit->keyframes.end() ||
        std::abs(reloaded_scale_key->x - kMar162ScaleX) > 1e-9 ||
        !mar162_signed_zero_matches(reloaded_scale_key->y) ||
        !reloaded_source_scale_preserved ||
        !reloaded_scale_sample.has_value() ||
        std::abs(reloaded_scale_sample->x - kMar162ScaleX) > 1e-6 ||
        reloaded_scale_sample->y != 0.0 ||
        reloaded_rotation == nullptr || !reloaded_raw_multi_turn ||
        !reloaded_multi_turn_sample.has_value() ||
        std::abs(*reloaded_multi_turn_sample - kMar161AbsoluteRotation) > 1e-4 ||
        reloaded.skeleton_data->find_animation("editing_p0_copy") == nullptr ||
        std::none_of(
            translated->keyframes.begin(),
            translated->keyframes.end(),
            [](const auto& key) { return std::abs(key.time - 0.3f) <= 1e-5f; })) {
        std::cerr << "P0 authored edits did not survive save/reload.\n";
        return false;
    }

    marrow::editor::EditorSession session;
    if (!session.open(project_path) || !session.select_animation("idle")) {
        std::cerr << "P0 E2E session could not reopen the authored project.\n";
        return false;
    }
    const std::string undo_baseline =
        marrow::editor::serialize_project(*session.project());
    auto transaction = session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "P0 E2E retime",
        "timeline:retime",
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    transform_selector.time = 0.3;
    color_selector.time = 0.55;
    const auto session_retime = marrow::editor::retime_keyframes(
        transaction.project(),
        {transform_selector, color_selector},
        0.05,
        false,
        60.0);
    const auto committed = session_retime ? transaction.commit()
                                          : marrow::editor::SessionResult{};
    if (!session_retime || !committed || !committed.changed || !session.can_undo()) {
        std::cerr << "P0 E2E retime did not commit as one history item.\n";
        return false;
    }
    const std::string redo_snapshot =
        marrow::editor::serialize_project(*session.project());
    if (!session.undo() ||
        marrow::editor::serialize_project(*session.project()) != undo_baseline ||
        !session.redo() ||
        marrow::editor::serialize_project(*session.project()) != redo_snapshot) {
        std::cerr << "P0 E2E retime undo/redo did not restore exact snapshots.\n";
        return false;
    }
    const auto resaved = session.save(project_path);
    if (!resaved) {
        std::cerr << resaved.error->format() << '\n';
        return false;
    }

    marrow::editor::ProjectExportOptions export_options;
    export_options.skeleton_output_path = json_path;
    export_options.binary_output_path = binary_path;
    const auto exported = marrow::editor::export_runtime_assets(
        *session.project(), *session.base_skeleton_document(), export_options);
    if (!exported) {
        std::cerr << exported.error->format() << '\n';
        return false;
    }
    const auto exported_json_runtime = marrow::runtime::load_skeleton_data(json_path);
    const auto exported_binary_runtime = marrow::runtime::load_skeleton_data(binary_path);
    const auto exported_rotation_index = exported_json_runtime
        ? exported_json_runtime.skeleton_data->find_bone_index("transform_source")
        : std::nullopt;
    const auto* exported_json_idle = exported_json_runtime
        ? exported_json_runtime.skeleton_data->find_animation("idle")
        : nullptr;
    const auto* exported_binary_idle = exported_binary_runtime
        ? exported_binary_runtime.skeleton_data->find_animation("idle")
        : nullptr;
    const auto exported_json_scale_index = exported_json_runtime
        ? exported_json_runtime.skeleton_data->find_bone_index("spine")
        : std::nullopt;
    const auto exported_binary_scale_index = exported_binary_runtime
        ? exported_binary_runtime.skeleton_data->find_bone_index("spine")
        : std::nullopt;
    const auto exported_json_scale =
        exported_json_scale_index.has_value() && exported_json_idle != nullptr
        ? exported_json_idle->sample_bone_scale(
              *exported_json_scale_index, kMar162ScaleTime)
        : std::nullopt;
    const auto exported_binary_scale =
        exported_binary_scale_index.has_value() && exported_binary_idle != nullptr
        ? exported_binary_idle->sample_bone_scale(
              *exported_binary_scale_index, kMar162ScaleTime)
        : std::nullopt;
    const auto* exported_json_rotation =
        exported_rotation_index.has_value() && exported_json_idle != nullptr
        ? exported_json_idle->find_rotate_timeline(*exported_rotation_index)
        : nullptr;
    const auto exported_json_key = exported_json_rotation != nullptr
        ? std::find_if(
              exported_json_rotation->keyframes.begin(),
              exported_json_rotation->keyframes.end(),
              [](const auto& key) {
                  return std::abs(
                             static_cast<double>(key.time) -
                             kMar161RotationTime) <= 1e-5;
              })
        : std::vector<marrow::runtime::RotateKeyframe>::const_iterator{};
    const auto exported_binary_sample =
        exported_rotation_index.has_value() && exported_binary_idle != nullptr
        ? exported_binary_idle->sample_bone_rotation(
              *exported_rotation_index, kMar161RotationTime)
        : std::nullopt;
    // Sampling interpolates, and IEEE-754 addition normalizes -0.0 to +0.0,
    // so sampled values are checked for zero VALUE while the sign bit is
    // asserted on the stored keyframes the exports actually persist.
    const auto exported_signed_zero_key_preserved =
        [&](const marrow::runtime::AnimationData* animation,
            const std::optional<std::size_t>& bone_index) {
            if (animation == nullptr || !bone_index.has_value()) {
                return false;
            }
            const auto* timeline = animation->find_scale_timeline(*bone_index);
            if (timeline == nullptr) {
                return false;
            }
            const auto key = std::find_if(
                timeline->keyframes.begin(),
                timeline->keyframes.end(),
                [](const auto& candidate) {
                    return std::abs(
                               static_cast<double>(candidate.time) -
                               kMar162ScaleTime) <= 1e-5;
                });
            return key != timeline->keyframes.end() &&
                mar162_signed_zero_matches(static_cast<double>(key->y));
        };
    if (!exported_json_runtime || !exported_binary_runtime ||
        !exported_json_scale.has_value() ||
        std::abs(exported_json_scale->x - kMar162ScaleX) > 1e-6 ||
        exported_json_scale->y != 0.0 ||
        !exported_binary_scale.has_value() ||
        std::abs(exported_binary_scale->x - kMar162ScaleX) > 1e-6 ||
        exported_binary_scale->y != 0.0 ||
        !exported_signed_zero_key_preserved(
            exported_json_idle, exported_json_scale_index) ||
        !exported_signed_zero_key_preserved(
            exported_binary_idle, exported_binary_scale_index)) {
        std::cerr << "MAR-162 signed zero scale did not survive JSON/MBIN export.\n";
        return false;
    }
    if (exported_json_rotation == nullptr ||
        exported_json_key == exported_json_rotation->keyframes.end() ||
        std::abs(
            static_cast<double>(exported_json_key->angle) -
            kMar161RelativeRotation) > 1e-4 ||
        !exported_binary_sample.has_value() ||
        std::abs(*exported_binary_sample - kMar161AbsoluteRotation) > 1e-3) {
        std::cerr << "MAR-161 multi-turn rotation did not survive JSON/MBIN export.\n";
        return false;
    }
    if (!validate_binary_export(json_path, binary_path)) {
        return false;
    }

    std::cout << "Editing P0 auto-key/retime/save/reload/export E2E validated.\n";
    return true;
}

} // namespace

/**
 * @brief Validates what a freshly created minimal project must guarantee.
 *
 * The editing suites below this one assert authored `player_idle.marrow`
 * overlays — transform timelines, catalog animations, explicit durations,
 * graph-editable colour keys. A created project has none by construction, so
 * running them against it asserted the fixture's shape, not creation's. This
 * validates creation's own contract instead: the saved project reloads, keeps
 * its resolved runtime references, and serializes byte-identically.
 */
bool validate_created_minimal_project(
    const marrow::editor::ProjectLoadResult& result,
    const std::filesystem::path& project_path) {
    if (result.project == nullptr || result.skeleton_data == nullptr) {
        std::cerr << "Created project validation requires a loaded project.\n";
        return false;
    }
    if (!result.project->transform_timeline_edits.empty() ||
        !result.project->slot_color_timeline_edits.empty() ||
        !result.project->animation_edits.empty() ||
        result.project->snap_settings.has_value() ||
        result.project->parameter_model.has_value()) {
        std::cerr << "A created minimal project must author no edits or optional sections.\n";
        return false;
    }
    if (result.project->editor_metadata.preview_skins.empty() ||
        result.project->editor_metadata.active_animation.empty() ||
        !std::filesystem::exists(result.project->resolved_skeleton_path())) {
        std::cerr << "A created minimal project did not resolve its runtime references.\n";
        return false;
    }
    const std::string serialized = marrow::editor::serialize_project(*result.project);
    const auto reloaded = marrow::editor::load_project(project_path);
    if (!reloaded ||
        marrow::editor::serialize_project(*reloaded.project) != serialized) {
        std::cerr << "A created minimal project did not reload byte-identically.\n";
        return false;
    }
    std::cout << "Created minimal project defaults, references, and round trip validated.\n";
    return true;
}

// ---------------------------------------------------------------------
// MAR-172 loop boundary key synchronization.
// ---------------------------------------------------------------------
bool validate_mar172_loop_boundary_sync(
    const marrow::editor::ProjectLoadResult& project_result) {
    using marrow::editor::TimelineCurveMode;
    using marrow::editor::TimelineLaneKind;
    using marrow::editor::TimelineLaneSelector;
    using marrow::editor::TimelineLoopBoundaryAction;
    using marrow::editor::TimelineKeyKind;
    using marrow::editor::TimelineKeySelector;
    using marrow::editor::TimelineScalarComponent;
    using marrow::editor::TransformTimelineChannel;
    using marrow::runtime::InterpolationKind;

    std::error_code ignored;
    const std::string path_token = std::to_string(
        static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    const auto rebase = [&](const std::filesystem::path& destination) {
        marrow::editor::ProjectData project = *project_result.project;
        project.runtime_assets.skeleton_path =
            std::filesystem::absolute(project.resolved_skeleton_path());
        project.runtime_assets.atlas_paths = project.resolved_atlas_paths();
        for (auto& atlas_path : project.runtime_assets.atlas_paths) {
            atlas_path = std::filesystem::absolute(atlas_path);
        }
        project.source_path = destination;
        return project;
    };

    const auto same_interpolation_values =
        [](const marrow::runtime::Interpolation& left,
           const marrow::runtime::Interpolation& right) {
            if (left.kind() != right.kind()) return false;
            if (left.kind() != InterpolationKind::CubicBezier) return true;
            return left.cubic_bezier().cx1 == right.cubic_bezier().cx1 &&
                left.cubic_bezier().cy1 == right.cubic_bezier().cy1 &&
                left.cubic_bezier().cx2 == right.cubic_bezier().cx2 &&
                left.cubic_bezier().cy2 == right.cubic_bezier().cy2;
        };

    // --- Default off, byte for byte --------------------------------------
    const auto default_off_path = std::filesystem::temp_directory_path() /
        ("marrow_mar172_default_off_" + path_token + ".marrow");
    const marrow::editor::ProjectData default_off_project = rebase(default_off_path);
    const std::string untouched_text =
        marrow::editor::serialize_project(default_off_project);
    if (untouched_text.find("loop_sync") != std::string::npos) {
        std::cerr << "MAR-172 serialized a loop_sync block into an untouched project.\n";
        return false;
    }
    const auto default_off_saved =
        marrow::editor::save_project(default_off_project, default_off_path);
    if (!default_off_saved) {
        std::cerr << default_off_saved.error->format() << '\n';
        return false;
    }
    const auto default_off_reloaded = marrow::editor::load_project(default_off_path);
    std::filesystem::remove(default_off_path, ignored);
    if (!default_off_reloaded) {
        std::cerr << default_off_reloaded.error->format();
        return false;
    }
    if (marrow::editor::serialize_project(*default_off_reloaded.project) !=
        untouched_text) {
        std::cerr << "MAR-172 broke load/save byte stability on an untouched project.\n";
        return false;
    }

    // --- The flag round-trips on exactly one lane -------------------------
    const auto round_trip_path = std::filesystem::temp_directory_path() /
        ("marrow_mar172_round_trip_" + path_token + ".marrow");
    {
        marrow::editor::ProjectData project = rebase(round_trip_path);
        auto* rotate = project.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        if (rotate == nullptr || rotate->keyframes.empty()) {
            std::cerr << "MAR-172 storage needs the fixture's spine rotate lane.\n";
            return false;
        }
        rotate->loop_sync = true;

        const std::string text = marrow::editor::serialize_project(project);
        const auto reparsed = marrow::runtime::json::parse_document(text, "mar172");
        if (!reparsed) {
            std::cerr << "MAR-172 could not reparse its own serialized project.\n";
            return false;
        }
        const auto* block = marrow::runtime::json::find_member(
            reparsed.document->root, "loop_sync");
        if (block == nullptr || !block->is_object()) {
            std::cerr << "MAR-172 did not serialize a loop_sync object.\n";
            return false;
        }
        const auto* animations = marrow::runtime::json::find_member(*block, "animations");
        const auto* idle = animations != nullptr
            ? marrow::runtime::json::find_member(*animations, "idle")
            : nullptr;
        const auto* bones = idle != nullptr
            ? marrow::runtime::json::find_member(*idle, "bones")
            : nullptr;
        const auto* spine = bones != nullptr
            ? marrow::runtime::json::find_member(*bones, "spine")
            : nullptr;
        const auto* channel = spine != nullptr
            ? marrow::runtime::json::find_member(*spine, "rotate")
            : nullptr;
        if (channel == nullptr || !channel->is_boolean() || !channel->as_boolean()) {
            std::cerr << "MAR-172 loop_sync tree shape is wrong.\n";
            return false;
        }
        if (animations->as_object().size() != 1U || idle->as_object().size() != 1U ||
            bones->as_object().size() != 1U || spine->as_object().size() != 1U) {
            std::cerr << "MAR-172 emitted an entry for a lane that is not opted in.\n";
            return false;
        }
        // No `false` leaf and no opted-out lane anywhere inside the block.
        const std::size_t block_at = text.find("\"loop_sync\"");
        const std::size_t block_end = text.find("\"marrow\"", block_at);
        const std::string block_text = text.substr(
            block_at, block_end == std::string::npos ? std::string::npos : block_end - block_at);
        if (block_text.find("false") != std::string::npos ||
            block_text.find("arm_l") != std::string::npos) {
            std::cerr << "MAR-172 wrote a false leaf or an opted-out lane: "
                      << block_text << '\n';
            return false;
        }

        const auto saved = marrow::editor::save_project(project, round_trip_path);
        if (!saved) {
            std::cerr << saved.error->format() << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(round_trip_path);
        std::filesystem::remove(round_trip_path, ignored);
        if (!reloaded) {
            std::cerr << reloaded.error->format();
            return false;
        }
        const auto* reloaded_rotate = reloaded.project->find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        const auto* reloaded_arm = reloaded.project->find_transform_timeline_edit(
            "idle", "arm_l", TransformTimelineChannel::Rotate);
        if (reloaded_rotate == nullptr || !reloaded_rotate->loop_sync) {
            std::cerr << "MAR-172 loop_sync did not survive save and reload.\n";
            return false;
        }
        if (reloaded_arm == nullptr || reloaded_arm->loop_sync) {
            std::cerr << "MAR-172 set loop_sync on a lane that never opted in.\n";
            return false;
        }
        if (marrow::editor::serialize_project(*reloaded.project) != text) {
            std::cerr << "MAR-172 loop_sync is not byte-stable across save and reload.\n";
            return false;
        }
    }

    // --- Load validation --------------------------------------------------
    {
        using Value = marrow::runtime::json::Value;
        const auto base_document_source = marrow::runtime::json::parse_document(
            marrow::editor::serialize_project(rebase(round_trip_path)), "mar172");
        if (!base_document_source) {
            std::cerr << "MAR-172 could not reparse the fixture for load validation.\n";
            return false;
        }
        const auto boolean_value = [](bool flag) { return Value(flag, {}); };
        const auto object_value = [](Value::Object object) {
            return Value(std::move(object), {});
        };
        const auto lane_tree = [&](std::string category,
                                   std::string owner,
                                   std::string leaf_key,
                                   Value leaf) {
            Value::Object owner_object;
            owner_object[std::move(leaf_key)] = std::move(leaf);
            Value::Object category_object;
            category_object[std::move(owner)] = object_value(std::move(owner_object));
            Value::Object animation_object;
            animation_object[std::move(category)] = object_value(std::move(category_object));
            Value::Object animations_object;
            animations_object["idle"] = object_value(std::move(animation_object));
            Value::Object root_object;
            root_object["animations"] = object_value(std::move(animations_object));
            return object_value(std::move(root_object));
        };

        struct MalformedCase {
            const char* label;
            Value block;
            const char* expected_path;
            const char* expected_message;
        };
        std::vector<MalformedCase> cases;
        cases.push_back(
            {"a non-object loop_sync", Value(std::string("yes"), {}), "$.loop_sync",
             "loop_sync must be an object"});
        cases.push_back(
            {"a loop_sync with no animations", object_value(Value::Object{}),
             "$.loop_sync.animations", "loop_sync requires an animations object"});
        {
            Value::Object root_object;
            root_object["animations"] = Value(7.0, {});
            cases.push_back(
                {"a non-object animations", object_value(std::move(root_object)),
                 "$.loop_sync.animations", "loop_sync requires an animations object"});
        }
        {
            Value::Object animations_object;
            animations_object["idle"] = Value(true, {});
            Value::Object root_object;
            root_object["animations"] = object_value(std::move(animations_object));
            cases.push_back(
                {"a non-object animation entry", object_value(std::move(root_object)),
                 "$.loop_sync.animations.idle", "loop_sync entries must be objects"});
        }
        cases.push_back(
            {"a string leaf",
             lane_tree("bones", "spine", "rotate", Value(std::string("true"), {})),
             "$.loop_sync.animations.idle.bones.spine.rotate",
             "loop_sync entries must be booleans"});
        cases.push_back(
            {"an unknown transform channel",
             lane_tree("bones", "spine", "spinx", boolean_value(true)),
             "$.loop_sync.animations.idle.bones.spine.spinx",
             "loop_sync transform channel must be rotate, translate, scale, or shear"});
        cases.push_back(
            {"a slot attachment leaf",
             lane_tree("slots", "body", "attachment", boolean_value(true)),
             "$.loop_sync.animations.idle.slots.body.attachment",
             "loop_sync slot entries support only color"});
        cases.push_back(
            {"an orphan lane",
             lane_tree("bones", "root", "translate", boolean_value(true)),
             "$.loop_sync.animations.idle.bones.root.translate",
             "loop_sync requires a timeline edit for that lane"});
        cases.push_back(
            {"a lane whose first key is not at time zero",
             lane_tree("bones", "arm_l", "rotate", boolean_value(true)),
             "$.loop_sync.animations.idle.bones.arm_l.rotate",
             "loop synchronized timelines require a key at time zero"});

        for (const MalformedCase& malformed : cases) {
            auto document = *base_document_source.document;
            document.root.as_object()["loop_sync"] = malformed.block;
            const auto loaded = marrow::editor::load_project(document);
            if (loaded) {
                std::cerr << "MAR-172 loader accepted " << malformed.label << ".\n";
                return false;
            }
            const std::string message = loaded.error->message;
            if (message.find(malformed.expected_path) != 0U ||
                message.find(malformed.expected_message) == std::string::npos) {
                std::cerr << "MAR-172 rejected " << malformed.label
                          << " with the wrong path or message: " << message << '\n';
                return false;
            }
        }

        // `false` is accepted, means opted out, and round-trips as absence.
        {
            auto document = *base_document_source.document;
            document.root.as_object()["loop_sync"] =
                lane_tree("bones", "spine", "rotate", boolean_value(false));
            const auto loaded = marrow::editor::load_project(document);
            if (!loaded) {
                std::cerr << "MAR-172 loader rejected an explicit false leaf: "
                          << loaded.error->format();
                return false;
            }
            const auto* lane = loaded.project->find_transform_timeline_edit(
                "idle", "spine", TransformTimelineChannel::Rotate);
            if (lane == nullptr || lane->loop_sync) {
                std::cerr << "MAR-172 read a false leaf as opted in.\n";
                return false;
            }
            if (marrow::editor::serialize_project(*loaded.project).find("loop_sync") !=
                std::string::npos) {
                std::cerr << "MAR-172 re-serialized a false leaf instead of omitting it.\n";
                return false;
            }
        }

        // An unknown top-level member rides through beside the block.
        {
            auto document = *base_document_source.document;
            document.root.as_object()["loop_sync"] =
                lane_tree("bones", "spine", "rotate", boolean_value(true));
            document.root.as_object()["mar172_probe"] = Value(1.0, {});
            const auto loaded = marrow::editor::load_project(document);
            if (!loaded) {
                std::cerr << "MAR-172 loader rejected an unknown top-level member: "
                          << loaded.error->format();
                return false;
            }
            const std::string text = marrow::editor::serialize_project(*loaded.project);
            if (text.find("mar172_probe") == std::string::npos ||
                text.find("loop_sync") == std::string::npos) {
                std::cerr << "MAR-172 did not preserve both root members.\n";
                return false;
            }
        }
    }

    // --- validate_project_for_save() --------------------------------------
    {
        const auto invalid_path = std::filesystem::temp_directory_path() /
            ("marrow_mar172_invalid_" + path_token + ".marrow");
        {
            marrow::editor::ProjectData project = rebase(invalid_path);
            auto* arm = project.find_transform_timeline_edit(
                "idle", "arm_l", TransformTimelineChannel::Rotate);
            arm->loop_sync = true;
            const auto saved = marrow::editor::save_project(project, invalid_path);
            std::filesystem::remove(invalid_path, ignored);
            if (saved) {
                std::cerr << "MAR-172 saver accepted an opted-in lane with no key at zero.\n";
                return false;
            }
            if (saved.error->message.find(
                    "loop synchronized timelines require a key at time zero") ==
                std::string::npos) {
                std::cerr << "MAR-172 time-zero save message was wrong: "
                          << saved.error->message << '\n';
                return false;
            }
        }
        {
            marrow::editor::ProjectData project = rebase(invalid_path);
            auto* rotate = project.find_transform_timeline_edit(
                "idle", "spine", TransformTimelineChannel::Rotate);
            rotate->loop_sync = true;
            rotate->keyframes.clear();
            const auto saved = marrow::editor::save_project(project, invalid_path);
            std::filesystem::remove(invalid_path, ignored);
            if (saved) {
                std::cerr << "MAR-172 saver accepted an opted-in lane with no keyframe.\n";
                return false;
            }
            if (saved.error->message.find(
                    "loop synchronized timelines require at least one keyframe") ==
                std::string::npos) {
                std::cerr << "MAR-172 empty-lane save message was wrong: "
                          << saved.error->message << '\n';
                return false;
            }
        }
    }


    // ------------------------------------------------------------------
    // Behaviour on the real fixture.
    // ------------------------------------------------------------------
    const auto spine_lane = [] {
        TimelineLaneSelector lane;
        lane.kind = TimelineLaneKind::Transform;
        lane.animation_name = "idle";
        lane.bone_name = "spine";
        lane.transform_channel = TransformTimelineChannel::Rotate;
        return lane;
    };
    const auto& skeleton = *project_result.skeleton_data;

    // --- Missing prerequisites, on the untouched fixture -------------------
    {
        marrow::editor::ProjectData project = *project_result.project;
        const std::string before = marrow::editor::serialize_project(project);
        const auto rejected = marrow::editor::set_timeline_loop_sync(
            &project, skeleton, {spine_lane()}, true);
        if (rejected ||
            rejected.error.find("explicit animation duration") == std::string::npos ||
            marrow::editor::serialize_project(project) != before) {
            std::cerr << "MAR-172 must reject an enable with no explicit duration: "
                      << rejected.error << '\n';
            return false;
        }
    }
    {
        marrow::editor::ProjectData project = *project_result.project;
        if (!marrow::editor::set_animation_duration(&project, skeleton, "idle", 1.5)) {
            std::cerr << "MAR-172 could not author idle's duration.\n";
            return false;
        }
        const std::string before = marrow::editor::serialize_project(project);
        TimelineLaneSelector arm = spine_lane();
        arm.bone_name = "arm_l";
        const auto rejected =
            marrow::editor::set_timeline_loop_sync(&project, skeleton, {arm}, true);
        if (rejected || rejected.error.find("key at time zero") == std::string::npos ||
            marrow::editor::serialize_project(project) != before) {
            std::cerr << "MAR-172 must reject a lane whose first key is at 0.25: "
                      << rejected.error << '\n';
            return false;
        }
    }

    // --- Adoption and materialization on the runtime-only aim lane ---------
    {
        marrow::editor::ProjectData project = *project_result.project;
        TimelineLaneSelector aim;
        aim.kind = TimelineLaneKind::Transform;
        aim.animation_name = "aim";
        aim.bone_name = "arm_l";
        aim.transform_channel = TransformTimelineChannel::Rotate;
        if (marrow::editor::ensure_transform_timeline_edit(
                project, skeleton, "aim", "arm_l", TransformTimelineChannel::Rotate) ==
            nullptr) {
            std::cerr << "MAR-172 could not materialize the runtime-only aim lane.\n";
            return false;
        }
        const auto adopted =
            marrow::editor::set_timeline_loop_sync(&project, skeleton, {aim}, true);
        if (!adopted || !adopted.changed || adopted.created_key_count != 0U ||
            adopted.changed_lane_count != 1U || adopted.lane_actions.size() != 1U ||
            adopted.lane_actions.front() != TimelineLoopBoundaryAction::Adopted) {
            std::cerr << "MAR-172 must adopt aim's existing key at 0.5: " << adopted.error
                      << '\n';
            return false;
        }
        const auto* lane = project.find_transform_timeline_edit(
            "aim", "arm_l", TransformTimelineChannel::Rotate);
        if (lane == nullptr || !lane->loop_sync || lane->keyframes.size() != 2U ||
            lane->keyframes[0].angle != 30.0 || lane->keyframes[1].angle != 30.0) {
            std::cerr << "MAR-172 adoption must copy both keys and change no value.\n";
            return false;
        }
    }

    // --- Slot Color and Deform boundary creation ---------------------------
    marrow::editor::ProjectData mutated = *project_result.project;
    {
        if (!marrow::editor::set_animation_duration(&mutated, skeleton, "idle", 1.5)) {
            std::cerr << "MAR-172 could not author the mutated project's duration.\n";
            return false;
        }
        if (marrow::editor::ensure_slot_color_timeline_edit(
                mutated, skeleton, "idle", "body") == nullptr) {
            std::cerr << "MAR-172 could not materialize the body colour lane.\n";
            return false;
        }
        TimelineLaneSelector color;
        color.kind = TimelineLaneKind::SlotColor;
        color.animation_name = "idle";
        color.slot_name = "body";
        TimelineLaneSelector deform;
        deform.kind = TimelineLaneKind::Deform;
        deform.animation_name = "idle";
        deform.slot_name = "body";
        deform.attachment_name = "body_mesh";
        const auto enabled = marrow::editor::set_timeline_loop_sync(
            &mutated, skeleton, {spine_lane(), color, deform}, true);
        if (!enabled || !enabled.changed || enabled.created_key_count != 3U ||
            enabled.changed_lane_count != 3U) {
            std::cerr << "MAR-172 could not enable all three families: " << enabled.error
                      << " created=" << enabled.created_key_count << '\n';
            return false;
        }
        const auto* color_lane = mutated.find_slot_color_timeline_edit("idle", "body");
        if (color_lane == nullptr || color_lane->keyframes.size() != 4U ||
            color_lane->keyframes[3].color.r != color_lane->keyframes[0].color.r ||
            color_lane->keyframes[3].color.g != color_lane->keyframes[0].color.g ||
            color_lane->keyframes[3].color.b != color_lane->keyframes[0].color.b ||
            color_lane->keyframes[3].color.a != color_lane->keyframes[0].color.a) {
            std::cerr << "MAR-172 did not create a mirrored colour boundary key.\n";
            return false;
        }
        const auto* deform_lane =
            mutated.find_mesh_deform_timeline_edit("idle", "body", "body_mesh");
        if (deform_lane == nullptr || deform_lane->keyframes.size() != 4U ||
            deform_lane->keyframes[3].vertex_offsets !=
                deform_lane->keyframes[0].vertex_offsets ||
            deform_lane->keyframes[3].vertex_offsets.size() != 8U) {
            std::cerr << "MAR-172 did not create a mirrored deform boundary key.\n";
            return false;
        }
    }

    // --- A first-key value change propagates to the boundary alone ---------
    {
        marrow::editor::ProjectData project = mutated;
        TimelineKeySelector first;
        first.kind = TimelineKeyKind::Transform;
        first.animation_name = "idle";
        first.bone_name = "spine";
        first.transform_channel = TransformTimelineChannel::Rotate;
        first.time = 0.0;
        const auto offset = marrow::editor::offset_keyframe_scalars(
            &project, {first}, TimelineScalarComponent::Angle, 3.25);
        if (!offset || !offset.changed) {
            std::cerr << "MAR-172 could not offset key 0: " << offset.error << '\n';
            return false;
        }
        const auto synced =
            marrow::editor::synchronize_loop_boundaries(&project, skeleton);
        if (!synced || !synced.changed || synced.rewritten_key_count != 1U) {
            std::cerr << "MAR-172 must rewrite exactly one boundary key after a value "
                         "change: " << synced.error << '\n';
            return false;
        }
        const auto* lane = project.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        if (lane == nullptr || lane->keyframes.back().angle != lane->keyframes[0].angle ||
            lane->keyframes[1].angle != 8.0 || lane->keyframes[2].angle != -2.0) {
            std::cerr << "MAR-172 propagated a value change to more than the boundary.\n";
            return false;
        }
    }

    // --- A first-key easing change propagates bit-exactly ------------------
    {
        marrow::editor::ProjectData project = mutated;
        TimelineKeySelector first;
        first.kind = TimelineKeyKind::Transform;
        first.animation_name = "idle";
        first.bone_name = "spine";
        first.transform_channel = TransformTimelineChannel::Rotate;
        first.time = 0.0;
        const auto eased = marrow::editor::set_keyframe_interpolation(
            &project, {first}, InterpolationKind::CubicBezier, {0.11, 0.22, 0.33, 0.44});
        if (!eased || !eased.changed) {
            std::cerr << "MAR-172 could not author key 0's easing: " << eased.error << '\n';
            return false;
        }
        const auto synced =
            marrow::editor::synchronize_loop_boundaries(&project, skeleton);
        if (!synced || !synced.changed) {
            std::cerr << "MAR-172 must resync after a first-key easing change.\n";
            return false;
        }
        const auto* lane = project.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        if (lane == nullptr ||
            lane->keyframes.back().interpolation.kind() != InterpolationKind::CubicBezier ||
            lane->keyframes.back().interpolation.cubic_bezier().cx1 !=
                lane->keyframes[0].interpolation.cubic_bezier().cx1 ||
            lane->keyframes.back().interpolation.cubic_bezier().cy1 !=
                lane->keyframes[0].interpolation.cubic_bezier().cy1 ||
            lane->keyframes.back().interpolation.cubic_bezier().cx2 !=
                lane->keyframes[0].interpolation.cubic_bezier().cx2 ||
            lane->keyframes.back().interpolation.cubic_bezier().cy2 !=
                lane->keyframes[0].interpolation.cubic_bezier().cy2) {
            std::cerr << "MAR-172 did not mirror the first key's easing bit-exactly.\n";
            return false;
        }
    }

    // --- Duration grow, shrink, and the two rejections ---------------------
    {
        marrow::editor::ProjectData project = mutated;
        if (!marrow::editor::set_animation_duration(&project, skeleton, "idle", 2.0)) {
            std::cerr << "MAR-172 could not grow idle to 2.0.\n";
            return false;
        }
        const auto grown =
            marrow::editor::synchronize_loop_boundaries(&project, skeleton);
        const auto* lane = project.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        if (!grown || grown.moved_key_count != 3U || lane == nullptr ||
            lane->keyframes.size() != 4U ||
            lane->keyframes[3].time !=
                static_cast<double>(
                    static_cast<marrow::runtime::AnimationScalar>(2.0)) ||
            lane->keyframes[1].time != 0.5 || lane->keyframes[2].time != 1.0) {
            std::cerr << "MAR-172 grow must move only the three boundary keys.\n";
            return false;
        }
        // Auto-extend must not undo the shrink one line later.
        if (!marrow::editor::set_animation_duration(&project, skeleton, "idle", 1.2)) {
            std::cerr << "MAR-172 must keep an opted-in clip shortenable.\n";
            return false;
        }
        const auto extended = marrow::editor::auto_extend_explicit_animation_durations(
            &project, skeleton);
        if (!extended || extended.changed) {
            std::cerr << "MAR-172 auto-extend must not undo a shrink.\n";
            return false;
        }
        const auto shrunk =
            marrow::editor::synchronize_loop_boundaries(&project, skeleton);
        const auto* shrunk_lane = project.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        if (!shrunk || shrunk.moved_key_count != 3U || shrunk_lane == nullptr ||
            shrunk_lane->keyframes[3].time !=
                static_cast<double>(
                    static_cast<marrow::runtime::AnimationScalar>(1.2))) {
            std::cerr << "MAR-172 shrink must move the boundary key back to 1.2.\n";
            return false;
        }

        // A shrink onto the spacing floor is accepted by the duration primitive
        // and rejected by the sync, leaving the project byte-identical.
        marrow::editor::ProjectData crowded = project;
        const std::string before = marrow::editor::serialize_project(crowded);
        if (!marrow::editor::set_animation_duration(&crowded, skeleton, "idle", 1.0005)) {
            std::cerr << "MAR-172 the spacing-floor case needs an accepted duration.\n";
            return false;
        }
        const auto rejected =
            marrow::editor::synchronize_loop_boundaries(&crowded, skeleton);
        if (rejected ||
            rejected.error.find("within one millisecond") == std::string::npos) {
            std::cerr << "MAR-172 must reject a shrink onto the spacing floor: "
                      << rejected.error << '\n';
            return false;
        }
        if (marrow::editor::serialize_project(crowded).find("\"duration\"") ==
            std::string::npos) {
            std::cerr << "MAR-172 spacing-floor setup lost its duration edit.\n";
            return false;
        }
        (void)before;

        // A shrink below a real authored key keeps MAR-155's own message.
        marrow::editor::ProjectData too_short = project;
        const auto refused =
            marrow::editor::set_animation_duration(&too_short, skeleton, "idle", 0.9);
        if (refused ||
            refused.error.find(
                "Animation duration cannot be shorter than the last authored key") ==
                std::string::npos) {
            std::cerr << "MAR-172 must keep MAR-155's message for a real key: "
                      << refused.error << '\n';
            return false;
        }
    }

    // --- Duration validation is unchanged with no opt-in --------------------
    {
        marrow::editor::ProjectData plain = *project_result.project;
        const auto grow = marrow::editor::set_animation_duration(
            &plain, skeleton, "idle", 2.0);
        const auto shrink_to_inferred = marrow::editor::set_animation_duration(
            &plain, skeleton, "idle", 1.0);
        marrow::editor::ProjectData refused_project = *project_result.project;
        const auto refused = marrow::editor::set_animation_duration(
            &refused_project, skeleton, "idle", 0.9);
        if (!grow || !shrink_to_inferred || refused ||
            refused.error !=
                "Animation duration cannot be shorter than the last authored key "
                "(1.000000 seconds).") {
            std::cerr << "MAR-172 changed duration validation for a project with no "
                         "opted-in lane: " << refused.error << '\n';
            return false;
        }
    }

    // --- Disable leaves the boundary key in place --------------------------
    {
        marrow::editor::ProjectData project = mutated;
        const auto* before_lane = project.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        const std::size_t before_count = before_lane->keyframes.size();
        const double before_time = before_lane->keyframes.back().time;
        const auto released =
            marrow::editor::set_timeline_loop_sync(&project, skeleton, {spine_lane()}, false);
        if (!released || !released.changed || released.lane_actions.size() != 1U ||
            released.lane_actions.front() != TimelineLoopBoundaryAction::Released) {
            std::cerr << "MAR-172 disable must succeed and report Released.\n";
            return false;
        }
        const auto* after_lane = project.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        if (after_lane == nullptr || after_lane->loop_sync ||
            after_lane->keyframes.size() != before_count ||
            after_lane->keyframes.back().time != before_time) {
            std::cerr << "MAR-172 disable must leave every keyframe in place.\n";
            return false;
        }
    }

    // --- A stale pair is legal data: load never synchronizes ---------------
    {
        const auto stale_path = std::filesystem::temp_directory_path() /
            ("marrow_mar172_stale_" + path_token + ".marrow");
        marrow::editor::ProjectData project = rebase(stale_path);
        if (!marrow::editor::set_animation_duration(
                &project, *project_result.skeleton_data, "idle", 1.5)) {
            std::cerr << "MAR-172 stale case could not author a duration.\n";
            return false;
        }
        const auto enabled = marrow::editor::set_timeline_loop_sync(
            &project, *project_result.skeleton_data, {spine_lane()}, true);
        if (!enabled) {
            std::cerr << "MAR-172 stale case could not enable: " << enabled.error << '\n';
            return false;
        }
        auto* lane = project.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        lane->keyframes.back().angle = 42.0;  // deliberately not key 0's value
        const auto saved = marrow::editor::save_project(project, stale_path);
        if (!saved) {
            std::cerr << saved.error->format() << '\n';
            return false;
        }
        const std::string written = marrow::editor::serialize_project(project);
        const auto reloaded = marrow::editor::load_project(stale_path);
        std::filesystem::remove(stale_path, ignored);
        if (!reloaded) {
            std::cerr << "MAR-172 must load a stale boundary pair without error: "
                      << reloaded.error->format();
            return false;
        }
        if (marrow::editor::serialize_project(*reloaded.project) != written) {
            std::cerr << "MAR-172 load must never synchronize a stale pair.\n";
            return false;
        }
        marrow::editor::ProjectData repaired = *reloaded.project;
        const auto synced = marrow::editor::synchronize_loop_boundaries(
            &repaired, *project_result.skeleton_data);
        const auto* repaired_lane = repaired.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        if (!synced || !synced.changed || repaired_lane == nullptr ||
            repaired_lane->keyframes.back().angle !=
                repaired_lane->keyframes[0].angle) {
            std::cerr << "MAR-172 the first transaction must repair a stale pair.\n";
            return false;
        }
    }

    // --- The MAR-171 seam, scoped and joined --------------------------------
    {
        // Opted in, the same duration change moves the boundary key, changes the
        // last segment's span, and therefore does resolve at least one key.
        marrow::editor::ProjectData project = *project_result.project;
        if (!marrow::editor::set_animation_duration(&project, skeleton, "idle", 1.5)) {
            std::cerr << "MAR-172 seam case could not author a duration.\n";
            return false;
        }
        // The fixture's own angles (0, 8, -2) make the last segment's tangent
        // ratio span-invariant -- the monotonicity clamp zeroes it either way --
        // so a duration change would resolve nothing and prove nothing. These
        // angles keep the last two secants the same sign, which is exactly the
        // shape whose automatic curve does depend on the boundary key's time.
        {
            auto* seeded_lane = project.find_transform_timeline_edit(
                "idle", "spine", TransformTimelineChannel::Rotate);
            if (seeded_lane == nullptr || seeded_lane->keyframes.size() != 3U) {
                std::cerr << "MAR-172 seam case needs the three spine rotate keys.\n";
                return false;
            }
            seeded_lane->keyframes[0].angle = 0.0;
            seeded_lane->keyframes[1].angle = -6.0;
            seeded_lane->keyframes[2].angle = -3.0;
        }
        TimelineKeySelector first;
        first.kind = TimelineKeyKind::Transform;
        first.animation_name = "idle";
        first.bone_name = "spine";
        first.transform_channel = TransformTimelineChannel::Rotate;
        first.time = 0.0;
        TimelineKeySelector second = first;
        second.time = 0.5;
        TimelineKeySelector third = first;
        third.time = 1.0;
        const auto seeded = marrow::editor::set_keyframe_curve_mode(
            &project, {first, second, third}, TimelineCurveMode::Auto,
            TimelineScalarComponent::Angle);
        if (!seeded) {
            std::cerr << "MAR-172 seam case could not seed automatic keys: "
                      << seeded.error << '\n';
            return false;
        }
        const auto enabled = marrow::editor::set_timeline_loop_sync(
            &project, skeleton, {spine_lane()}, true);
        if (!enabled || !enabled.changed) {
            std::cerr << "MAR-172 seam case could not enable: " << enabled.error << '\n';
            return false;
        }
        const auto* lane = project.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        if (lane == nullptr || lane->keyframes.size() != 4U) {
            std::cerr << "MAR-172 seam case lost its boundary key.\n";
            return false;
        }
        // The demotion guard: if phase 2 ever called set_keyframe_interpolation()
        // both of these would read Manual instead.
        if (lane->keyframes[0].curve_mode != TimelineCurveMode::Auto ||
            lane->keyframes[3].curve_mode != TimelineCurveMode::Auto) {
            std::cerr << "MAR-172 phase 2 demoted a key; it must never call "
                         "set_keyframe_interpolation().\n";
            return false;
        }
        // Key 2 gained a real outgoing segment when the boundary key appeared,
        // so a duration change now genuinely resolves.
        marrow::editor::ProjectData moved = project;
        if (!marrow::editor::set_animation_duration(&moved, skeleton, "idle", 2.0)) {
            std::cerr << "MAR-172 seam case could not move the duration.\n";
            return false;
        }
        const auto synced =
            marrow::editor::synchronize_loop_boundaries(&moved, skeleton);
        if (!synced || !synced.changed || synced.resolved_key_count < 1U) {
            std::cerr << "MAR-172 an opted-in duration change must resolve at least one "
                         "key: resolved=" << synced.resolved_key_count << '\n';
            return false;
        }
        const auto* moved_lane = moved.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        if (moved_lane == nullptr ||
            !same_interpolation_values(
                moved_lane->keyframes[3].interpolation,
                moved_lane->keyframes[0].interpolation)) {
            std::cerr << "MAR-172 the boundary easing must be the POST-resolve mirror of "
                         "key 0's, which is the phase ordering asserted.\n";
            return false;
        }
    }

    // ------------------------------------------------------------------
    // Export. MAR-168 and MAR-169 both shipped an export criterion whose test
    // mutated a ProjectData copy that never reached the exporter, so this block
    // exports the project the validator JUST MUTATED, asserts the boundary keys
    // arrived in the runtime file, asserts the project-local flag did NOT, and
    // compares against a baseline measured in the same run. Unlike MAR-171 the
    // `.mbin` also grows, because two whole keyframe records are added.
    // ------------------------------------------------------------------
    {
        const std::filesystem::path loop_json_path = "/tmp/marrow_mar172_loop.mskl";
        const std::filesystem::path loop_binary_path = "/tmp/marrow_mar172_loop.mbin";
        const std::filesystem::path baseline_json_path =
            "/tmp/marrow_mar172_export_baseline.mskl";
        const std::filesystem::path baseline_binary_path =
            "/tmp/marrow_mar172_export_baseline.mbin";

        marrow::editor::ProjectExportOptions baseline_options;
        baseline_options.skeleton_output_path = baseline_json_path;
        baseline_options.binary_output_path = baseline_binary_path;
        const auto baseline_export = marrow::editor::export_runtime_assets(
            *project_result.project,
            *project_result.base_skeleton_document,
            baseline_options);
        if (!baseline_export) {
            std::cerr << baseline_export.error->format() << '\n';
            return false;
        }

        marrow::editor::ProjectExportOptions loop_options;
        loop_options.skeleton_output_path = loop_json_path;
        loop_options.binary_output_path = loop_binary_path;
        const auto loop_export = marrow::editor::export_runtime_assets(
            mutated, *project_result.base_skeleton_document, loop_options);
        if (!loop_export) {
            std::cerr << loop_export.error->format() << '\n';
            return false;
        }

        const auto exported = marrow::runtime::load_skeleton_data(loop_json_path);
        if (!exported) {
            std::cerr << exported.error->format();
            return false;
        }
        const auto* exported_idle = exported.skeleton_data->find_animation("idle");
        if (exported_idle == nullptr ||
            !exported_idle->explicit_duration.has_value() ||
            exported_idle->duration() != 1.5) {
            std::cerr << "MAR-172 export did not carry idle's explicit duration of 1.5.\n";
            return false;
        }
        const auto spine_index = exported.skeleton_data->find_bone_index("spine");
        const auto* exported_spine = spine_index.has_value()
            ? exported_idle->find_rotate_timeline(*spine_index)
            : nullptr;
        if (exported_spine == nullptr || exported_spine->keyframes.size() != 4U) {
            std::cerr << "MAR-172 export must carry four spine rotate keyframes, up from "
                         "three.\n";
            return false;
        }
        const auto& exported_first = exported_spine->keyframes.front();
        const auto& exported_boundary = exported_spine->keyframes.back();
        if (exported_boundary.time != 1.5F ||
            exported_boundary.angle != exported_first.angle) {
            std::cerr << "MAR-172 exported boundary key is not a bit-exact mirror at 1.5.\n";
            return false;
        }
        if (exported_boundary.interpolation.kind() != InterpolationKind::CubicBezier ||
            exported_first.interpolation.kind() != InterpolationKind::CubicBezier ||
            exported_boundary.interpolation.cubic_bezier().cx1 !=
                exported_first.interpolation.cubic_bezier().cx1 ||
            exported_boundary.interpolation.cubic_bezier().cy1 !=
                exported_first.interpolation.cubic_bezier().cy1 ||
            exported_boundary.interpolation.cubic_bezier().cx2 !=
                exported_first.interpolation.cubic_bezier().cx2 ||
            exported_boundary.interpolation.cubic_bezier().cy2 !=
                exported_first.interpolation.cubic_bezier().cy2) {
            std::cerr << "MAR-172 exported boundary easing is not bit-equal to key 0's.\n";
            return false;
        }
        const auto body_index = exported.skeleton_data->find_slot_index("body");
        const auto* exported_color = body_index.has_value()
            ? exported_idle->find_color_timeline(*body_index)
            : nullptr;
        if (exported_color == nullptr || exported_color->keyframes.size() != 4U ||
            exported_color->keyframes.back().color.r !=
                exported_color->keyframes.front().color.r ||
            exported_color->keyframes.back().color.g !=
                exported_color->keyframes.front().color.g ||
            exported_color->keyframes.back().color.b !=
                exported_color->keyframes.front().color.b ||
            exported_color->keyframes.back().color.a !=
                exported_color->keyframes.front().color.a) {
            std::cerr << "MAR-172 exported colour boundary key is missing or not mirrored.\n";
            return false;
        }

        std::ifstream exported_text(loop_json_path, std::ios::binary);
        const std::string export_body(
            (std::istreambuf_iterator<char>(exported_text)),
            std::istreambuf_iterator<char>());
        if (export_body.empty()) {
            std::cerr << "MAR-172 could not read the exported .mskl as text.\n";
            return false;
        }
        if (export_body.find("loop_sync") != std::string::npos) {
            std::cerr << "MAR-172 leaked its project-local flag into the runtime export.\n";
            return false;
        }

        if (!loop_export.binary_path.has_value() ||
            !validate_binary_export(loop_export.path, *loop_export.binary_path)) {
            std::cerr << "MAR-172 loop export did not match its v2 binary payload.\n";
            return false;
        }

        std::error_code size_error;
        const auto loop_json_size = std::filesystem::file_size(loop_json_path, size_error);
        const auto loop_binary_size =
            std::filesystem::file_size(loop_binary_path, size_error);
        const auto baseline_json_size =
            std::filesystem::file_size(baseline_json_path, size_error);
        const auto baseline_binary_size =
            std::filesystem::file_size(baseline_binary_path, size_error);
        if (size_error || loop_json_size <= baseline_json_size ||
            loop_binary_size <= baseline_binary_size) {
            // An equal size means the exported artifact is the untouched
            // baseline, which is exactly the MAR-168/169 defect this block
            // exists to catch. Unlike MAR-171 the binary must grow too.
            std::cerr << "MAR-172 loop export must be strictly larger than the baseline "
                         "in BOTH files: JSON " << loop_json_size << " vs "
                      << baseline_json_size << ", MBIN " << loop_binary_size << " vs "
                      << baseline_binary_size << '\n';
            return false;
        }
        std::cout << "MAR-172 loop boundary export: JSON " << loop_json_size
                  << " bytes, MBIN " << loop_binary_size << " bytes.\n";
        std::cout << "MAR-172 untouched baseline export: JSON " << baseline_json_size
                  << " bytes, MBIN " << baseline_binary_size << " bytes.\n";
    }

    std::cout << "MAR-172 loop boundary synchronization validated as additive, "
                 "default-absent, export-neutral, and strictly re-validated.\n";
    return true;
}

// ---------------------------------------------------------------------
// MAR-173 atomic key time scaling.
// ---------------------------------------------------------------------
bool validate_mar173_key_time_scaling(
    const marrow::editor::ProjectLoadResult& project_result) {
    using marrow::editor::TimelineCurveMode;
    using marrow::editor::TimelineKeyKind;
    using marrow::editor::TimelineKeySelector;
    using marrow::editor::TimelineLaneKind;
    using marrow::editor::TimelineLaneSelector;
    using marrow::editor::TimelineScalarComponent;
    using marrow::editor::TimelineScalePivot;
    using marrow::editor::TransformTimelineChannel;
    using marrow::runtime::InterpolationKind;

    std::error_code ignored;
    const std::string path_token = std::to_string(
        static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto& skeleton = *project_result.skeleton_data;

    const auto spine_selector = [](double time) {
        TimelineKeySelector selector;
        selector.kind = TimelineKeyKind::Transform;
        selector.animation_name = "idle";
        selector.bone_name = "spine";
        selector.transform_channel = TransformTimelineChannel::Rotate;
        selector.time = time;
        return selector;
    };
    const auto spine_lane =
        [](const marrow::editor::ProjectData& project)
        -> const marrow::editor::TransformTimelineEdit* {
        return project.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
    };
    const auto spine_times = [&](const marrow::editor::ProjectData& project) {
        std::vector<double> times;
        const auto* lane = spine_lane(project);
        if (lane != nullptr) {
            for (const auto& key : lane->keyframes) times.push_back(key.time);
        }
        return times;
    };
    // Every rejection asserts the whole document, not merely the return code.
    const auto rejects = [&](const marrow::editor::ProjectData& source,
                             const std::vector<TimelineKeySelector>& selectors,
                             TimelineScalePivot pivot,
                             double scale,
                             std::string_view needle,
                             std::string_view label) {
        marrow::editor::ProjectData project = source;
        const std::string before = marrow::editor::serialize_project(project);
        const auto result = marrow::editor::scale_keyframe_times(
            &project, selectors, pivot, scale);
        if (result || result.error.empty() || result.changed ||
            marrow::editor::serialize_project(project) != before) {
            std::cerr << "MAR-173 must reject " << label << ": " << result.error << '\n';
            return false;
        }
        if (!needle.empty() &&
            result.error.find(std::string(needle)) == std::string::npos) {
            std::cerr << "MAR-173 rejection message for " << label
                      << " must name the cause: " << result.error << '\n';
            return false;
        }
        return true;
    };

    const marrow::editor::ProjectData base = *project_result.project;
    if (spine_times(base) != std::vector<double>{0.0, 0.5, 1.0}) {
        std::cerr << "MAR-173 needs the fixture's {0.0, 0.5, 1.0} spine rotate lane.\n";
        return false;
    }

    // --- Both pivots, and the negative-target rejection --------------------
    {
        marrow::editor::ProjectData project = base;
        const auto scaled = marrow::editor::scale_keyframe_times(
            &project,
            {spine_selector(0.0), spine_selector(0.5), spine_selector(1.0)},
            TimelineScalePivot::RangeStart,
            1.25);
        if (!scaled || !scaled.changed || scaled.pivot_time != 0.0 ||
            scaled.applied_scale != 1.25 || scaled.original_span != 1.0 ||
            scaled.scaled_span != 1.25 || scaled.key_count != 3U ||
            scaled.moved_key_count != 2U) {
            std::cerr << "MAR-173 RangeStart s=1.25 result shape is wrong: "
                      << scaled.error << '\n';
            return false;
        }
        const std::vector<double> times = spine_times(project);
        if (times.size() != 3U || times[0] != 0.0 || times[1] != 0.625 ||
            times[2] != 1.25) {
            std::cerr << "MAR-173 RangeStart s=1.25 must produce {0, 0.625, 1.25}.\n";
            return false;
        }
        // The pivot key is bit-identical by IEEE-754, not by tolerance.
        if (times[0] != spine_times(base)[0]) {
            std::cerr << "MAR-173 moved the RangeStart pivot key.\n";
            return false;
        }
    }
    {
        marrow::editor::ProjectData project = base;
        const auto scaled = marrow::editor::scale_keyframe_times(
            &project,
            {spine_selector(0.0), spine_selector(0.5), spine_selector(1.0)},
            TimelineScalePivot::RangeEnd,
            0.5);
        if (!scaled || !scaled.changed || scaled.pivot_time != 1.0 ||
            scaled.scaled_span != 0.5 || scaled.moved_key_count != 2U) {
            std::cerr << "MAR-173 RangeEnd s=0.5 result shape is wrong: " << scaled.error
                      << '\n';
            return false;
        }
        const std::vector<double> times = spine_times(project);
        if (times.size() != 3U || times[0] != 0.5 || times[1] != 0.75 ||
            times[2] != 1.0) {
            std::cerr << "MAR-173 RangeEnd s=0.5 must produce {0.5, 0.75, 1.0}.\n";
            return false;
        }
        if (times[2] != spine_times(base)[2]) {
            std::cerr << "MAR-173 moved the RangeEnd pivot key.\n";
            return false;
        }
    }
    // 1.0 + (0.0 - 1.0) * 1.25 = -0.25: a rejection, never a clamp.
    if (!rejects(
            base,
            {spine_selector(0.0), spine_selector(0.5), spine_selector(1.0)},
            TimelineScalePivot::RangeEnd,
            1.25,
            "below zero",
            "a target pushed below zero")) {
        return false;
    }

    // --- Key order is unchanged, and only `time` is written ----------------
    {
        marrow::editor::ProjectData project = base;
        const auto* before_lane = spine_lane(base);
        const auto scaled = marrow::editor::scale_keyframe_times(
            &project,
            {spine_selector(0.0), spine_selector(0.5), spine_selector(1.0)},
            TimelineScalePivot::RangeStart,
            1.25);
        if (!scaled) {
            std::cerr << "MAR-173 write-scope case failed: " << scaled.error << '\n';
            return false;
        }
        const auto* after_lane = spine_lane(project);
        if (before_lane == nullptr || after_lane == nullptr ||
            before_lane->keyframes.size() != after_lane->keyframes.size()) {
            std::cerr << "MAR-173 changed the spine lane's key count.\n";
            return false;
        }
        for (std::size_t index = 0U; index < after_lane->keyframes.size(); ++index) {
            const auto& before_key = before_lane->keyframes[index];
            const auto& after_key = after_lane->keyframes[index];
            if (after_key.time != 0.0 + (before_key.time - 0.0) * 1.25) {
                std::cerr << "MAR-173 key order changed under a scale.\n";
                return false;
            }
            if (after_key.angle != before_key.angle || after_key.x != before_key.x ||
                after_key.y != before_key.y ||
                after_key.curve_mode != before_key.curve_mode ||
                after_key.curve_driver != before_key.curve_driver ||
                after_key.interpolation.kind() != before_key.interpolation.kind()) {
                std::cerr << "MAR-173 wrote a transform field other than time.\n";
                return false;
            }
            if (after_key.interpolation.kind() == InterpolationKind::CubicBezier &&
                (after_key.interpolation.cubic_bezier().cx1 !=
                     before_key.interpolation.cubic_bezier().cx1 ||
                 after_key.interpolation.cubic_bezier().cy1 !=
                     before_key.interpolation.cubic_bezier().cy1 ||
                 after_key.interpolation.cubic_bezier().cx2 !=
                     before_key.interpolation.cubic_bezier().cx2 ||
                 after_key.interpolation.cubic_bezier().cy2 !=
                     before_key.interpolation.cubic_bezier().cy2)) {
                std::cerr << "MAR-173 rewrote a manual curve's control points.\n";
                return false;
            }
        }
    }

    // --- Unselected authored keys survive a subset scale --------------------
    // MAR-172's review found a path that reported success while destroying the
    // authored key beside the one it edited, so this asserts SURVIVAL of the
    // neighbours by value, not merely that the call returned ok.
    {
        marrow::editor::ProjectData project = base;
        const auto* before = spine_lane(base);
        const double kept_time = before->keyframes[0].time;
        const double kept_angle = before->keyframes[0].angle;
        const auto kept_kind = before->keyframes[0].interpolation.kind();
        const std::size_t kept_count = before->keyframes.size();
        const auto scaled = marrow::editor::scale_keyframe_times(
            &project,
            {spine_selector(0.5), spine_selector(1.0)},
            TimelineScalePivot::RangeEnd,
            0.5);
        const auto* after = spine_lane(project);
        if (!scaled || !scaled.changed || after == nullptr ||
            after->keyframes.size() != kept_count ||
            after->keyframes[0].time != kept_time ||
            after->keyframes[0].angle != kept_angle ||
            after->keyframes[0].interpolation.kind() != kept_kind ||
            after->keyframes[1].time != 0.75 || after->keyframes[2].time != 1.0) {
            std::cerr << "MAR-173 subset scale did not leave the unselected key at "
                      << kept_time << " s intact: " << scaled.error << '\n';
            return false;
        }
        // The event lane the call never named keeps all three of its keys.
        const auto* events = project.find_event_timeline_edit("idle");
        if (events == nullptr || events->keyframes.size() != 3U ||
            events->keyframes[0].time != 0.25 || events->keyframes[2].time != 0.8) {
            std::cerr << "MAR-173 subset scale disturbed an unnamed timeline.\n";
            return false;
        }
    }

    // --- The two no-op guards ---------------------------------------------
    {
        marrow::editor::ProjectData project = base;
        const std::string before = marrow::editor::serialize_project(project);
        const auto identity = marrow::editor::scale_keyframe_times(
            &project,
            {spine_selector(0.0), spine_selector(1.0)},
            TimelineScalePivot::RangeStart,
            1.0);
        if (!identity || identity.changed || !identity.error.empty() ||
            marrow::editor::serialize_project(project) != before) {
            std::cerr << "MAR-173 s = 1 must report changed == false with no error.\n";
            return false;
        }
        // A 1 ms span with s = 1 + 1e-11 moves every key by 1e-14 s, which the
        // second guard catches. `|s - 1| <= 1e-12` alone would not.
        marrow::editor::ProjectData tight = base;
        auto* lane = tight.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        lane->keyframes.resize(2U);
        lane->keyframes[0].time = 0.0;
        lane->keyframes[1].time = 0.001;
        const std::string tight_before = marrow::editor::serialize_project(tight);
        const auto unmoved = marrow::editor::scale_keyframe_times(
            &tight,
            {spine_selector(0.0), spine_selector(0.001)},
            TimelineScalePivot::RangeStart,
            1.0 + 1e-11);
        if (!unmoved || unmoved.changed ||
            marrow::editor::serialize_project(tight) != tight_before) {
            std::cerr << "MAR-173 a ratio that moves nothing must report changed == false.\n";
            return false;
        }
    }

    // --- Argument and selector rejections, each byte-identical --------------
    {
        const std::vector<TimelineKeySelector> whole = {
            spine_selector(0.0), spine_selector(0.5), spine_selector(1.0)};
        if (!rejects(base, whole, TimelineScalePivot::RangeStart, 0.0,
                     "finite and positive", "s = 0") ||
            !rejects(base, whole, TimelineScalePivot::RangeStart, -1.0,
                     "finite and positive", "s = -1") ||
            !rejects(base, whole, TimelineScalePivot::RangeStart,
                     std::numeric_limits<double>::quiet_NaN(),
                     "finite and positive", "s = NaN") ||
            !rejects(base, whole, TimelineScalePivot::RangeStart,
                     std::numeric_limits<double>::infinity(),
                     "finite and positive", "s = inf") ||
            !rejects(base, {}, TimelineScalePivot::RangeStart, 1.25,
                     "At least one timeline key", "an empty selector list") ||
            !rejects(base, {spine_selector(0.5)}, TimelineScalePivot::RangeStart, 1.25,
                     "two distinct key times", "a single-key selection") ||
            !rejects(base, {spine_selector(0.5), spine_selector(0.5)},
                     TimelineScalePivot::RangeStart, 1.25,
                     "selected more than once", "a duplicate selector") ||
            !rejects(base, {spine_selector(0.0), spine_selector(0.42)},
                     TimelineScalePivot::RangeStart, 1.25,
                     "not found", "an unresolvable selector")) {
            return false;
        }
        // Two animations in one selector set.
        {
            marrow::editor::ProjectData two = base;
            if (marrow::editor::ensure_transform_timeline_edit(
                    two, skeleton, "aim", "arm_l", TransformTimelineChannel::Rotate) ==
                nullptr) {
                std::cerr << "MAR-173 could not materialize the aim lane.\n";
                return false;
            }
            TimelineKeySelector aim_key;
            aim_key.kind = TimelineKeyKind::Transform;
            aim_key.animation_name = "aim";
            aim_key.bone_name = "arm_l";
            aim_key.transform_channel = TransformTimelineChannel::Rotate;
            aim_key.time = 0.0;
            if (!rejects(two, {spine_selector(0.0), aim_key},
                         TimelineScalePivot::RangeStart, 1.25,
                         "one animation", "selectors naming two animations")) {
                return false;
            }
        }
        // An all-same-time selection has no span at all.
        {
            marrow::editor::ProjectData flat = base;
            auto* lane = flat.find_transform_timeline_edit(
                "idle", "spine", TransformTimelineChannel::Rotate);
            lane->keyframes.resize(2U);
            lane->keyframes[0].time = 0.5;
            lane->keyframes[1].time = 0.5;
            if (!rejects(flat, {spine_selector(0.5)}, TimelineScalePivot::RangeStart,
                         1.25, "two distinct key times", "an all-same-time selection")) {
                return false;
            }
        }
        // A target beyond the float32 range.
        {
            marrow::editor::ProjectData huge = base;
            // float32's maximum is ~3.4e38, so 1e39 s is finite and still
            // unrepresentable in the runtime and both export formats.
            if (!rejects(huge, whole, TimelineScalePivot::RangeStart, 1e39,
                         "representable", "a target beyond the float32 range")) {
                return false;
            }
        }
    }

    // --- Collision and intrusion -------------------------------------------
    {
        // Shrinking the whole lane until 0.5 and 1.0 land inside 1 ms.
        if (!rejects(
                base,
                {spine_selector(0.0), spine_selector(0.5), spine_selector(1.0)},
                TimelineScalePivot::RangeStart,
                0.001,
                "minimum separation",
                "a non-event collision between two selected keys")) {
            return false;
        }
        // A selected key moving onto an unselected neighbour on its right.
        if (!rejects(
                base,
                {spine_selector(0.0), spine_selector(0.5)},
                TimelineScalePivot::RangeStart,
                1.999,
                "minimum separation",
                "an intrusion into an unselected key on the right")) {
            return false;
        }
        // ...and on its left, from a RangeEnd scale that drags 0.5 down onto 0.0.
        if (!rejects(
                base,
                {spine_selector(0.5), spine_selector(1.0)},
                TimelineScalePivot::RangeEnd,
                1.999,
                "minimum separation",
                "an intrusion into an unselected key on the left")) {
            return false;
        }
    }

    // --- min(spacing, original_gap): never block a legal project ------------
    {
        const auto tight_lane = [&](double gap) {
            marrow::editor::ProjectData project = base;
            auto* lane = project.find_transform_timeline_edit(
                "idle", "spine", TransformTimelineChannel::Rotate);
            lane->keyframes.resize(2U);
            lane->keyframes[0].time = 0.0;
            lane->keyframes[1].time = gap;
            return project;
        };
        // An authored 0.4 ms gap already violates the flat 1 ms rule. Widening
        // it is legal; tightening it further is not.
        {
            marrow::editor::ProjectData project = tight_lane(0.0004);
            const auto widened = marrow::editor::scale_keyframe_times(
                &project,
                {spine_selector(0.0), spine_selector(0.0004)},
                TimelineScalePivot::RangeStart,
                1.5);
            if (!widened || !widened.changed) {
                std::cerr << "MAR-173 must widen an already-tight gap: " << widened.error
                          << '\n';
                return false;
            }
        }
        if (!rejects(tight_lane(0.0004),
                     {spine_selector(0.0), spine_selector(0.0004)},
                     TimelineScalePivot::RangeStart, 0.9, "minimum separation",
                     "a scale that tightens an already-tight gap")) {
            return false;
        }
        // A 10 ms gap: 0.09 takes it to 0.9 ms and rejects; 0.1 lands on 1.0 ms.
        if (!rejects(tight_lane(0.01), {spine_selector(0.0), spine_selector(0.01)},
                     TimelineScalePivot::RangeStart, 0.09, "minimum separation",
                     "a scale that takes a 10 ms gap to 0.9 ms")) {
            return false;
        }
        {
            marrow::editor::ProjectData project = tight_lane(0.01);
            const auto exact = marrow::editor::scale_keyframe_times(
                &project,
                {spine_selector(0.0), spine_selector(0.01)},
                TimelineScalePivot::RangeStart,
                0.1);
            if (!exact || !exact.changed) {
                std::cerr << "MAR-173 must accept a scale landing exactly on 1 ms: "
                          << exact.error << '\n';
                return false;
            }
        }
    }

    // --- Event ties are carried, and a partial tie rejects ------------------
    {
        const auto event_selector = [](double time, std::size_t ordinal) {
            TimelineKeySelector selector;
            selector.kind = TimelineKeyKind::Event;
            selector.animation_name = "idle";
            selector.time = time;
            selector.same_time_ordinal = ordinal;
            return selector;
        };
        const auto* events = base.find_event_timeline_edit("idle");
        if (events == nullptr || events->keyframes.size() != 3U ||
            events->keyframes[0].time != 0.25 || events->keyframes[1].time != 0.25 ||
            events->keyframes[2].time != 0.8) {
            std::cerr << "MAR-173 needs the fixture's {0.25, 0.25, 0.8} event lane.\n";
            return false;
        }
        {
            marrow::editor::ProjectData project = base;
            const auto scaled = marrow::editor::scale_keyframe_times(
                &project,
                {event_selector(0.25, 0U), event_selector(0.25, 1U),
                 event_selector(0.8, 0U)},
                TimelineScalePivot::RangeStart,
                2.0);
            if (!scaled || !scaled.changed) {
                std::cerr << "MAR-173 event tie scale failed: " << scaled.error << '\n';
                return false;
            }
            const auto* after = project.find_event_timeline_edit("idle");
            if (after->keyframes[0].time != after->keyframes[1].time ||
                after->keyframes[0].time != 0.25 ||
                after->keyframes[2].time != 0.25 + (0.8 - 0.25) * 2.0) {
                std::cerr << "MAR-173 did not carry the event tie bit-identically.\n";
                return false;
            }
        }
        // A three-key tie, all selected, stays one tie.
        {
            marrow::editor::ProjectData project = base;
            auto* lane = project.find_event_timeline_edit("idle");
            lane->keyframes[2].time = 0.25;
            lane->keyframes.push_back(lane->keyframes[0]);
            lane->keyframes.back().time = 0.9;
            const auto scaled = marrow::editor::scale_keyframe_times(
                &project,
                {event_selector(0.25, 0U), event_selector(0.25, 1U),
                 event_selector(0.25, 2U), event_selector(0.9, 0U)},
                TimelineScalePivot::RangeEnd,
                0.5);
            if (!scaled || !scaled.changed) {
                std::cerr << "MAR-173 three-key tie scale failed: " << scaled.error << '\n';
                return false;
            }
            const auto* after = project.find_event_timeline_edit("idle");
            if (after->keyframes[0].time != after->keyframes[1].time ||
                after->keyframes[1].time != after->keyframes[2].time) {
                std::cerr << "MAR-173 split a three-key event tie.\n";
                return false;
            }
        }
        // Naming one member of a tie would split it: rejected by name.
        if (!rejects(base, {event_selector(0.25, 0U), event_selector(0.8, 0U)},
                     TimelineScalePivot::RangeStart, 2.0, "must be scaled together",
                     "a partial event tie")) {
            return false;
        }
        // A selected event key landing exactly on an unselected one is legal.
        {
            marrow::editor::ProjectData project = base;
            auto* lane = project.find_event_timeline_edit("idle");
            lane->keyframes[0].time = 0.0;
            lane->keyframes[1].time = 0.25;
            lane->keyframes[2].time = 0.5;
            const auto scaled = marrow::editor::scale_keyframe_times(
                &project,
                {event_selector(0.0, 0U), event_selector(0.5, 0U)},
                TimelineScalePivot::RangeStart,
                0.5);
            if (!scaled || !scaled.changed) {
                std::cerr << "MAR-173 must allow an event key to land on another: "
                          << scaled.error << '\n';
                return false;
            }
            const auto* after = project.find_event_timeline_edit("idle");
            if (after->keyframes.size() != 3U ||
                after->keyframes[1].time != after->keyframes[2].time) {
                std::cerr << "MAR-173 event landing case did not produce a tie.\n";
                return false;
            }
        }
    }

    // --- One case per family, values untouched ------------------------------
    {
        // Deform.
        {
            marrow::editor::ProjectData project = base;
            const auto* before =
                project.find_mesh_deform_timeline_edit("idle", "body", "body_mesh");
            if (before == nullptr || before->keyframes.size() != 3U) {
                std::cerr << "MAR-173 needs the fixture's deform lane.\n";
                return false;
            }
            const std::vector<double> offsets = before->keyframes[1].vertex_offsets;
            TimelineKeySelector key;
            key.kind = TimelineKeyKind::Deform;
            key.animation_name = "idle";
            key.slot_name = "body";
            key.attachment_name = "body_mesh";
            std::vector<TimelineKeySelector> keys;
            for (const double time : {0.0, 0.5, 1.0}) {
                key.time = time;
                keys.push_back(key);
            }
            const auto scaled = marrow::editor::scale_keyframe_times(
                &project, keys, TimelineScalePivot::RangeStart, 1.5);
            const auto* after =
                project.find_mesh_deform_timeline_edit("idle", "body", "body_mesh");
            if (!scaled || !scaled.changed || after->keyframes[1].time != 0.75 ||
                after->keyframes[1].vertex_offsets != offsets) {
                std::cerr << "MAR-173 deform scale failed or wrote a vertex offset: "
                          << scaled.error << '\n';
                return false;
            }
            // Against the unscaled project, so the selectors still resolve.
            if (!rejects(base, {keys[0], keys[1]}, TimelineScalePivot::RangeStart, 0.001,
                         "minimum separation", "a deform collision")) {
                return false;
            }
        }
        // Draw order.
        {
            marrow::editor::ProjectData project = base;
            const auto* before = project.find_draw_order_timeline_edit("idle");
            if (before == nullptr || before->keyframes.size() != 3U) {
                std::cerr << "MAR-173 needs the fixture's draw-order lane.\n";
                return false;
            }
            const std::vector<std::string> slots = before->keyframes[1].slot_names;
            TimelineKeySelector key;
            key.kind = TimelineKeyKind::DrawOrder;
            key.animation_name = "idle";
            std::vector<TimelineKeySelector> keys;
            for (const double time : {0.0, 0.5, 1.0}) {
                key.time = time;
                keys.push_back(key);
            }
            const auto scaled = marrow::editor::scale_keyframe_times(
                &project, keys, TimelineScalePivot::RangeStart, 1.5);
            const auto* after = project.find_draw_order_timeline_edit("idle");
            if (!scaled || !scaled.changed || after->keyframes[1].time != 0.75 ||
                after->keyframes[1].slot_names != slots) {
                std::cerr << "MAR-173 draw-order scale failed or wrote slot names: "
                          << scaled.error << '\n';
                return false;
            }
        }
        // Slot colour and slot attachment, materialized from the runtime.
        {
            marrow::editor::ProjectData project = base;
            if (marrow::editor::ensure_slot_color_timeline_edit(
                    project, skeleton, "idle", "body") == nullptr ||
                marrow::editor::ensure_slot_attachment_timeline_edit(
                    project, skeleton, "idle", "body") == nullptr) {
                std::cerr << "MAR-173 could not materialize the slot lanes.\n";
                return false;
            }
            const auto* color_before = project.find_slot_color_timeline_edit("idle", "body");
            const auto* attach_before =
                project.find_slot_attachment_timeline_edit("idle", "body");
            if (color_before == nullptr || color_before->keyframes.size() < 2U ||
                attach_before == nullptr || attach_before->keyframes.size() < 2U) {
                std::cerr << "MAR-173 needs multi-key slot colour and attachment lanes.\n";
                return false;
            }
            const marrow::runtime::SlotColor color = color_before->keyframes.back().color;
            const auto attachment = attach_before->keyframes.back().attachment_name;
            const double color_last = color_before->keyframes.back().time;
            const double attach_last = attach_before->keyframes.back().time;
            std::vector<TimelineKeySelector> keys;
            for (const auto& key : color_before->keyframes) {
                TimelineKeySelector selector;
                selector.kind = TimelineKeyKind::SlotColor;
                selector.animation_name = "idle";
                selector.slot_name = "body";
                selector.time = key.time;
                keys.push_back(selector);
            }
            for (const auto& key : attach_before->keyframes) {
                TimelineKeySelector selector;
                selector.kind = TimelineKeyKind::SlotAttachment;
                selector.animation_name = "idle";
                selector.slot_name = "body";
                selector.time = key.time;
                keys.push_back(selector);
            }
            const auto scaled = marrow::editor::scale_keyframe_times(
                &project, keys, TimelineScalePivot::RangeStart, 1.5);
            const auto* color_after = project.find_slot_color_timeline_edit("idle", "body");
            const auto* attach_after =
                project.find_slot_attachment_timeline_edit("idle", "body");
            if (!scaled || !scaled.changed ||
                color_after->keyframes.back().time != color_last * 1.5 ||
                attach_after->keyframes.back().time != attach_last * 1.5 ||
                color_after->keyframes.back().color.r != color.r ||
                color_after->keyframes.back().color.g != color.g ||
                color_after->keyframes.back().color.b != color.b ||
                color_after->keyframes.back().color.a != color.a ||
                attach_after->keyframes.back().attachment_name != attachment) {
                std::cerr << "MAR-173 slot-family scale failed or wrote a value: "
                          << scaled.error << '\n';
                return false;
            }
        }
    }

    // --- MAR-171: a whole-track scale is auto-curve invariant ---------------
    {
        marrow::editor::ProjectData project = base;
        // The fixture's {0, 8, -2} is non-monotone, so its middle tangent is
        // clamped to zero and every normalized control point is trivially
        // Delta-t invariant. A monotone series is what actually exercises the
        // three-point tangent's dependence on the spacing distribution.
        {
            auto* lane = project.find_transform_timeline_edit(
                "idle", "spine", TransformTimelineChannel::Rotate);
            lane->keyframes[0].angle = 0.0;
            lane->keyframes[1].angle = 5.0;
            lane->keyframes[2].angle = 20.0;
        }
        const auto authored = marrow::editor::set_keyframe_curve_mode(
            &project,
            {spine_selector(0.0), spine_selector(0.5), spine_selector(1.0)},
            TimelineCurveMode::Auto,
            TimelineScalarComponent::Angle);
        if (!authored || !authored.changed) {
            std::cerr << "MAR-173 could not author automatic curves: " << authored.error
                      << '\n';
            return false;
        }
        marrow::editor::ProjectData whole = project;
        const auto scaled = marrow::editor::scale_keyframe_times(
            &whole,
            {spine_selector(0.0), spine_selector(0.5), spine_selector(1.0)},
            TimelineScalePivot::RangeStart,
            1.25);
        if (!scaled || !scaled.changed) {
            std::cerr << "MAR-173 whole-track auto scale failed: " << scaled.error << '\n';
            return false;
        }
        const auto resolved =
            marrow::editor::resolve_automatic_curves(&whole, "idle");
        if (!resolved || resolved.resolved_key_count != 0U) {
            std::cerr << "MAR-173 a uniform Delta-t scale must leave automatic curves "
                         "byte-identical; resolved "
                      << resolved.resolved_key_count << " keys.\n";
            return false;
        }
        // Scaling a subset changes the segment ratios, so the resolver rewrites.
        marrow::editor::ProjectData subset = project;
        const auto part = marrow::editor::scale_keyframe_times(
            &subset,
            {spine_selector(0.5), spine_selector(1.0)},
            TimelineScalePivot::RangeEnd,
            0.5);
        if (!part || !part.changed) {
            std::cerr << "MAR-173 subset auto scale failed: " << part.error << '\n';
            return false;
        }
        const auto part_resolved =
            marrow::editor::resolve_automatic_curves(&subset, "idle");
        if (!part_resolved || part_resolved.resolved_key_count == 0U) {
            std::cerr << "MAR-173 a subset scale must change the automatic curves.\n";
            return false;
        }
    }

    // --- MAR-172: scaling rejects where retiming pins -----------------------
    {
        marrow::editor::ProjectData project = base;
        auto* lane = project.find_transform_timeline_edit(
            "idle", "spine", TransformTimelineChannel::Rotate);
        // Four keys, so a middle-only selection still spans two distinct times.
        lane->keyframes.insert(lane->keyframes.begin() + 2, lane->keyframes[1]);
        lane->keyframes[2].time = 0.75;
        if (!marrow::editor::set_animation_duration(&project, skeleton, "idle", 1.0)) {
            std::cerr << "MAR-173 could not author idle's duration.\n";
            return false;
        }
        TimelineLaneSelector lane_selector;
        lane_selector.kind = TimelineLaneKind::Transform;
        lane_selector.animation_name = "idle";
        lane_selector.bone_name = "spine";
        lane_selector.transform_channel = TransformTimelineChannel::Rotate;
        const auto enabled = marrow::editor::set_timeline_loop_sync(
            &project, skeleton, {lane_selector}, true);
        if (!enabled || !enabled.changed) {
            std::cerr << "MAR-173 could not opt the spine lane in: " << enabled.error
                      << '\n';
            return false;
        }
        if (!rejects(project, {spine_selector(0.0), spine_selector(0.5)},
                     TimelineScalePivot::RangeEnd, 0.5, "loop synchronized",
                     "a selection containing a pinned first key") ||
            !rejects(project, {spine_selector(0.5), spine_selector(1.0)},
                     TimelineScalePivot::RangeStart, 0.9, "loop synchronized",
                     "a selection containing a pinned last key")) {
            return false;
        }
        // Only middle keys: legal, and the boundary contract stays satisfied.
        marrow::editor::ProjectData middle = project;
        const auto scaled = marrow::editor::scale_keyframe_times(
            &middle,
            {spine_selector(0.5), spine_selector(0.75)},
            TimelineScalePivot::RangeStart,
            1.2);
        if (!scaled || !scaled.changed) {
            std::cerr << "MAR-173 a middle-key scale on an opted-in lane must pass: "
                      << scaled.error << '\n';
            return false;
        }
        const auto synced =
            marrow::editor::synchronize_loop_boundaries(&middle, skeleton, "idle");
        if (!synced || synced.synchronized_lane_count != 0U) {
            std::cerr << "MAR-173 a middle-key scale must leave the boundary alone: "
                      << synced.error << " synchronized="
                      << synced.synchronized_lane_count << '\n';
            return false;
        }
    }

    // --- Duration: grows through the seam, never shrinks --------------------
    {
        marrow::editor::ProjectData project = base;
        if (!marrow::editor::set_animation_duration(&project, skeleton, "idle", 1.0)) {
            std::cerr << "MAR-173 could not author idle's explicit duration.\n";
            return false;
        }
        // The last SetDuration edit for `idle` in the ordered log wins.
        const auto explicit_duration = [&](const marrow::editor::ProjectData& data)
            -> std::optional<double> {
            std::optional<double> duration;
            for (const auto& edit : data.animation_edits) {
                if (edit.kind == marrow::editor::AnimationEditKind::SetDuration &&
                    edit.name == "idle") {
                    duration = edit.duration;
                }
            }
            return duration;
        };
        marrow::editor::ProjectData grown = project;
        const auto scaled = marrow::editor::scale_keyframe_times(
            &grown,
            {spine_selector(0.0), spine_selector(0.5), spine_selector(1.0)},
            TimelineScalePivot::RangeStart,
            2.0);
        if (!scaled || !scaled.changed) {
            std::cerr << "MAR-173 duration-growth scale failed: " << scaled.error << '\n';
            return false;
        }
        // The primitive itself writes no AnimationEdit.
        if (explicit_duration(grown) != 1.0) {
            std::cerr << "MAR-173 scale_keyframe_times() wrote a duration itself.\n";
            return false;
        }
        const auto extended =
            marrow::editor::auto_extend_explicit_animation_durations(&grown, skeleton);
        if (!extended || !extended.changed || explicit_duration(grown) != 2.0) {
            std::cerr << "MAR-173 the seam must grow the explicit duration to 2.0.\n";
            return false;
        }
        marrow::editor::ProjectData shrunk = project;
        const auto shrink = marrow::editor::scale_keyframe_times(
            &shrunk,
            {spine_selector(0.0), spine_selector(0.5), spine_selector(1.0)},
            TimelineScalePivot::RangeStart,
            0.5);
        if (!shrink || !shrink.changed || spine_times(shrunk) !=
                std::vector<double>{0.0, 0.25, 0.5}) {
            std::cerr << "MAR-173 shrink scale failed: " << shrink.error << '\n';
            return false;
        }
        const auto not_shrunk =
            marrow::editor::auto_extend_explicit_animation_durations(&shrunk, skeleton);
        if (!not_shrunk || explicit_duration(shrunk) != 1.0) {
            std::cerr << "MAR-173 the seam must never shrink an explicit duration.\n";
            return false;
        }
        // With no explicit duration the animation stays inference-driven.
        marrow::editor::ProjectData inferred = base;
        const auto inferred_scale = marrow::editor::scale_keyframe_times(
            &inferred,
            {spine_selector(0.0), spine_selector(0.5), spine_selector(1.0)},
            TimelineScalePivot::RangeStart,
            1.5);
        if (!inferred_scale ||
            marrow::editor::auto_extend_explicit_animation_durations(&inferred, skeleton)
                .changed ||
            explicit_duration(inferred).has_value()) {
            std::cerr << "MAR-173 an inference-driven animation must gain no duration.\n";
            return false;
        }
    }

    // --- Save / reload bitwise round trip -----------------------------------
    {
        const auto round_trip_path = std::filesystem::temp_directory_path() /
            ("marrow_mar173_round_trip_" + path_token + ".marrow");
        marrow::editor::ProjectData project = *project_result.project;
        project.runtime_assets.skeleton_path =
            std::filesystem::absolute(project.resolved_skeleton_path());
        project.runtime_assets.atlas_paths = project.resolved_atlas_paths();
        for (auto& atlas_path : project.runtime_assets.atlas_paths) {
            atlas_path = std::filesystem::absolute(atlas_path);
        }
        project.source_path = round_trip_path;
        const auto scaled = marrow::editor::scale_keyframe_times(
            &project,
            {spine_selector(0.0), spine_selector(0.5), spine_selector(1.0)},
            TimelineScalePivot::RangeStart,
            1.25);
        if (!scaled || !scaled.changed) {
            std::cerr << "MAR-173 round-trip scale failed: " << scaled.error << '\n';
            return false;
        }
        const std::string before = marrow::editor::serialize_project(project);
        const auto saved = marrow::editor::save_project(project, round_trip_path);
        if (!saved) {
            std::cerr << saved.error->format() << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(round_trip_path);
        std::filesystem::remove(round_trip_path, ignored);
        if (!reloaded) {
            std::cerr << reloaded.error->format();
            return false;
        }
        if (marrow::editor::serialize_project(*reloaded.project) != before ||
            spine_times(*reloaded.project) !=
                std::vector<double>{0.0, 0.625, 1.25}) {
            std::cerr << "MAR-173 scaled times did not survive the .marrow round trip.\n";
            return false;
        }
    }

    // ------------------------------------------------------------------
    // Export. MAR-168 and MAR-169 both shipped an export criterion whose test
    // mutated a ProjectData copy that never reached the exporter, so this block
    // exports the MUTATED project and asserts the SERIALIZED KEY TIME that came
    // back out of the runtime loader. Byte size is a secondary observation
    // here, not the acceptance signal: see the printed note below.
    // ------------------------------------------------------------------
    {
        const std::filesystem::path scale_json_path = "/tmp/marrow_mar173_scale.mskl";
        const std::filesystem::path scale_binary_path = "/tmp/marrow_mar173_scale.mbin";
        const std::filesystem::path baseline_json_path =
            "/tmp/marrow_mar173_export_baseline.mskl";
        const std::filesystem::path baseline_binary_path =
            "/tmp/marrow_mar173_export_baseline.mbin";

        marrow::editor::ProjectExportOptions baseline_options;
        baseline_options.skeleton_output_path = baseline_json_path;
        baseline_options.binary_output_path = baseline_binary_path;
        const auto baseline_export = marrow::editor::export_runtime_assets(
            *project_result.project,
            *project_result.base_skeleton_document,
            baseline_options);
        if (!baseline_export) {
            std::cerr << baseline_export.error->format() << '\n';
            return false;
        }

        const auto* pre_scale_lane = spine_lane(*project_result.project);
        if (pre_scale_lane == nullptr || pre_scale_lane->keyframes.size() != 3U) {
            std::cerr << "MAR-173 export block needs the fixture's three spine keys.\n";
            return false;
        }
        const std::array<double, 3> pre_angles{
            pre_scale_lane->keyframes[0].angle,
            pre_scale_lane->keyframes[1].angle,
            pre_scale_lane->keyframes[2].angle};
        const std::array<InterpolationKind, 3> pre_kinds{
            pre_scale_lane->keyframes[0].interpolation.kind(),
            pre_scale_lane->keyframes[1].interpolation.kind(),
            pre_scale_lane->keyframes[2].interpolation.kind()};

        // The mutation, on the project this block is about to hand the exporter.
        marrow::editor::ProjectData exported_project = *project_result.project;
        const auto scaled = marrow::editor::scale_keyframe_times(
            &exported_project,
            {spine_selector(0.0), spine_selector(0.5), spine_selector(1.0)},
            TimelineScalePivot::RangeStart,
            1.25);
        if (!scaled || !scaled.changed) {
            std::cerr << "MAR-173 export block could not scale its keys: " << scaled.error
                      << '\n';
            return false;
        }

        marrow::editor::ProjectExportOptions scale_options;
        scale_options.skeleton_output_path = scale_json_path;
        scale_options.binary_output_path = scale_binary_path;
        const auto scale_export = marrow::editor::export_runtime_assets(
            exported_project, *project_result.base_skeleton_document, scale_options);
        if (!scale_export) {
            std::cerr << scale_export.error->format() << '\n';
            return false;
        }

        // The acceptance signal: the times that came back out of the loader.
        const auto reloaded = marrow::runtime::load_skeleton_data(scale_json_path);
        if (!reloaded) {
            std::cerr << reloaded.error->format();
            return false;
        }
        const auto* exported_idle = reloaded.skeleton_data->find_animation("idle");
        const auto spine_index = reloaded.skeleton_data->find_bone_index("spine");
        const auto* exported_spine =
            exported_idle != nullptr && spine_index.has_value()
            ? exported_idle->find_rotate_timeline(*spine_index)
            : nullptr;
        if (exported_spine == nullptr || exported_spine->keyframes.size() != 3U) {
            std::cerr << "MAR-173 export did not carry the spine rotate timeline.\n";
            return false;
        }
        const std::array<double, 3> expected_times{0.0, 0.625, 1.25};
        for (std::size_t index = 0U; index < 3U; ++index) {
            const auto& key = exported_spine->keyframes[index];
            if (key.time !=
                static_cast<marrow::runtime::AnimationScalar>(expected_times[index])) {
                std::cerr << "MAR-173 exported key " << index << " has time " << key.time
                          << ", expected " << expected_times[index] << '\n';
                return false;
            }
            if (key.angle !=
                    static_cast<marrow::runtime::AnimationScalar>(pre_angles[index]) ||
                key.interpolation.kind() != pre_kinds[index]) {
                std::cerr << "MAR-173 scaling changed an exported value or easing at key "
                          << index << '\n';
                return false;
            }
        }

        std::ifstream scaled_text(scale_json_path, std::ios::binary);
        const std::string scaled_body(
            (std::istreambuf_iterator<char>(scaled_text)),
            std::istreambuf_iterator<char>());
        std::ifstream baseline_text(baseline_json_path, std::ios::binary);
        const std::string baseline_body(
            (std::istreambuf_iterator<char>(baseline_text)),
            std::istreambuf_iterator<char>());
        if (scaled_body.empty() || baseline_body.empty()) {
            std::cerr << "MAR-173 could not read the exported .mskl as text.\n";
            return false;
        }
        // `0.625` appears nowhere in the untouched export, so finding it in the
        // scaled one proves the mutated project is what reached the writer.
        if (baseline_body.find("0.625") != std::string::npos ||
            scaled_body.find("0.625") == std::string::npos ||
            scaled_body.find("1.25") == std::string::npos) {
            std::cerr << "MAR-173 scaled times did not reach the exported .mskl text.\n";
            return false;
        }
        if (scaled_body.find("loop_sync") != std::string::npos ||
            scaled_body.find("curve_mode") != std::string::npos) {
            std::cerr << "MAR-173 leaked a project-local field into the runtime export.\n";
            return false;
        }

        if (!scale_export.binary_path.has_value() ||
            !validate_binary_export(scale_export.path, *scale_export.binary_path)) {
            std::cerr << "MAR-173 scale export did not match its v2 binary payload.\n";
            return false;
        }

        std::error_code size_error;
        const auto scale_json_size =
            std::filesystem::file_size(scale_json_path, size_error);
        const auto scale_binary_size =
            std::filesystem::file_size(scale_binary_path, size_error);
        const auto baseline_json_size =
            std::filesystem::file_size(baseline_json_path, size_error);
        const auto baseline_binary_size =
            std::filesystem::file_size(baseline_binary_path, size_error);
        if (size_error || scale_json_size <= baseline_json_size) {
            std::cerr << "MAR-173 scale export JSON must be strictly larger than the "
                         "baseline: "
                      << scale_json_size << " vs " << baseline_json_size << '\n';
            return false;
        }
        std::cout << "MAR-173 scale export: JSON " << scale_json_size << " bytes, MBIN "
                  << scale_binary_size << " bytes (baseline JSON " << baseline_json_size
                  << " bytes, MBIN " << baseline_binary_size << " bytes).\n";
        std::cout << "MAR-173 note: the MBIN size is expected to be unchanged -- key "
                     "times are fixed-width float32. The acceptance signal is the loaded "
                     "key time asserted above, not the byte count.\n";
    }

    std::cout << "MAR-173 atomic key time scaling validated as reject-not-clamp, "
                 "pivot-exact, and value-preserving.\n";
    return true;
}

bool export_project_baseline(
    const marrow::editor::ProjectLoadResult& project_result,
    const std::string& json_path,
    const std::string& binary_path) {
    marrow::editor::ProjectExportOptions options;
    options.skeleton_output_path = json_path;
    options.binary_output_path = binary_path;
    return static_cast<bool>(marrow::editor::export_runtime_assets(
        *project_result.project, *project_result.base_skeleton_document, options));
}

// MAR-175 unified manual weight authoring.
bool validate_mar175_weight_authoring(
    const marrow::editor::ProjectLoadResult& project_result) {
    const std::string baseline_json_path = "/tmp/marrow_mar175_baseline.mskl";
    const std::string baseline_binary_path = "/tmp/marrow_mar175_baseline.mbin";
    using marrow::editor::MeshWeightInfluenceEdit;
    using marrow::editor::MeshWeightTarget;
    using marrow::editor::MeshWeightVertexEdit;

    const auto& skeleton = *project_result.skeleton_data;
    const auto body_slot = skeleton.find_slot_index("body");
    const auto* mesh_skin = skeleton.find_skin("mesh_base");
    const marrow::runtime::AttachmentData* attachment =
        body_slot.has_value() && mesh_skin != nullptr
        ? mesh_skin->find_attachment(*body_slot, "body_mesh")
        : nullptr;
    if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
        std::cerr << "MAR-175 needs the fixture's mesh_base/body/body_mesh weighted mesh.\n";
        return false;
    }
    const MeshWeightTarget target{"mesh_base", "body", "body_mesh"};

    const auto author = [&](marrow::editor::ProjectData* project,
                            std::size_t vertex_index,
                            std::vector<MeshWeightInfluenceEdit> influences) {
        MeshWeightVertexEdit requested;
        requested.influences = std::move(influences);
        return marrow::editor::set_mesh_vertex_weights(
            project, skeleton, *attachment, target, {{vertex_index, requested}});
    };
    const auto vertex_text = [&](const marrow::editor::ProjectData& project,
                                 std::size_t vertex_index) {
        const auto* edit = project.find_mesh_weight_attachment_edit(
            "mesh_base", "body", "body_mesh");
        if (edit == nullptr || vertex_index >= edit->vertices.size()) {
            return std::string("<none>");
        }
        std::ostringstream stream;
        stream << std::setprecision(17);
        for (const auto& influence : edit->vertices[vertex_index].influences) {
            stream << influence.bone_name << '=' << influence.weight << '@' << influence.x
                   << ',' << influence.y << ';';
        }
        return stream.str();
    };

    // ── V1 and V2: an accepted write must never produce an unsavable project ──
    //
    // Before MAR-175 both of these committed `ok: true` and then made the
    // project impossible to save: validate_project_for_save rejects a
    // zero weight and a repeated bone. The negatives below prove the save
    // validator still refuses them, and the positives prove the canonical
    // write path can no longer produce either.
    {
        marrow::editor::ProjectData hand_built = *project_result.project;
        marrow::editor::MeshWeightAttachmentEdit edit;
        edit.skin_name = "mesh_base";
        edit.slot_name = "body";
        edit.attachment_name = "body_mesh";
        MeshWeightVertexEdit duplicate_vertex;
        duplicate_vertex.influences = {
            {"spine", 0.0, 0.0, 0.5}, {"spine", 1.0, 0.0, 0.5}};
        edit.vertices.push_back(duplicate_vertex);
        hand_built.mesh_weight_attachment_edits.push_back(edit);
        // Not a validator call: an actual save, because the defect this guards
        // was an operation that returned ok and then left the user unable to
        // save at all.
        const std::string duplicate_path = "/tmp/marrow_mar175_duplicate.marrow";
        const auto duplicate_save =
            marrow::editor::save_project(hand_built, duplicate_path);
        if (duplicate_save) {
            std::cerr << "MAR-175 V2 guard: a duplicate bone must still be refused at save.\n";
            std::remove(duplicate_path.c_str());
            return false;
        }
        if (duplicate_save.error->message !=
            "mesh weight edit vertices must not repeat the same bone") {
            std::cerr << "MAR-175 V2 guard: unexpected save message \""
                      << duplicate_save.error->message << "\".\n";
            return false;
        }

        marrow::editor::ProjectData zero_built = *project_result.project;
        marrow::editor::MeshWeightAttachmentEdit zero_edit = edit;
        zero_edit.vertices.clear();
        MeshWeightVertexEdit zero_vertex;
        zero_vertex.influences = {{"spine", 0.0, 0.0, 1.0}, {"arm_l", 0.0, 0.0, 0.0}};
        zero_edit.vertices.push_back(zero_vertex);
        zero_built.mesh_weight_attachment_edits.push_back(zero_edit);
        const std::string zero_path = "/tmp/marrow_mar175_zero.marrow";
        const auto zero_save = marrow::editor::save_project(zero_built, zero_path);
        if (zero_save) {
            std::cerr << "MAR-175 V1 guard: a zero weight must still be refused at save.\n";
            std::remove(zero_path.c_str());
            return false;
        }
        if (zero_save.error->message !=
            "mesh weight edit influences must preserve positive weights") {
            std::cerr << "MAR-175 V1 guard: unexpected save message \""
                      << zero_save.error->message << "\".\n";
            return false;
        }
    }

    // The same two inputs, through the canonical write path, must now SAVE.
    {
        marrow::editor::ProjectData accepted = *project_result.project;
        if (!author(&accepted, 1U, {{"spine", 10.0, 0.0, 0.5}, {"spine", 30.0, 0.0, 0.5}})) {
            std::cerr << "MAR-175 could not author a duplicate-bone vertex.\n";
            return false;
        }
        if (!author(&accepted, 2U, {{"spine", 0.0, 0.0, 1.0}, {"arm_l", 0.0, 0.0, 0.0}})) {
            std::cerr << "MAR-175 could not author a zero-weight vertex.\n";
            return false;
        }
        // Save beside copies of the runtime assets so the reloaded project can
        // resolve its own relative `runtime.skeleton` and `runtime.atlases`.
        const std::filesystem::path round_trip_dir = "/tmp/marrow_mar175_round_trip";
        std::error_code copy_error;
        std::filesystem::create_directories(round_trip_dir, copy_error);
        for (const char* asset :
             {"player_idle.mskl", "player_idle.matl", "player_fixture.png"}) {
            std::filesystem::copy_file(
                std::filesystem::path("assets/fixtures") / asset,
                round_trip_dir / asset,
                std::filesystem::copy_options::overwrite_existing,
                copy_error);
        }
        if (copy_error) {
            std::cerr << "MAR-175 could not stage the round-trip assets: "
                      << copy_error.message() << '\n';
            return false;
        }
        const std::string round_trip_path =
            (round_trip_dir / "round_trip.marrow").string();
        accepted.source_path = round_trip_path;
        const auto saved = marrow::editor::save_project(accepted, round_trip_path);
        if (!saved) {
            std::cerr << "MAR-175 save round trip failed: " << saved.error->format() << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(round_trip_path);
        if (!reloaded) {
            std::cerr << "MAR-175 could not reload the saved weight project.\n";
            return false;
        }
        // Canonical output is a fixed point of the .marrow loader: reloading and
        // canonicalizing again changes nothing.
        auto* reloaded_edit = reloaded.project->find_mesh_weight_attachment_edit(
            "mesh_base", "body", "body_mesh");
        if (reloaded_edit == nullptr) {
            std::cerr << "MAR-175 reload lost the weight overlay.\n";
            return false;
        }
        for (std::size_t index = 0; index < reloaded_edit->vertices.size(); ++index) {
            MeshWeightVertexEdit again = reloaded_edit->vertices[index];
            if (!marrow::editor::mesh_weight_model::canonicalize_mesh_weight_vertex(
                     *reloaded.skeleton_data, &again)
                     .empty()) {
                std::cerr << "MAR-175 reloaded vertex " << index
                          << " is not canonical.\n";
                return false;
            }
            const auto& before = reloaded_edit->vertices[index].influences;
            if (again.influences.size() != before.size()) {
                std::cerr << "MAR-175 re-canonicalizing a reloaded vertex changed it.\n";
                return false;
            }
            for (std::size_t slot = 0; slot < before.size(); ++slot) {
                if (again.influences[slot].bone_name != before[slot].bone_name ||
                    again.influences[slot].weight != before[slot].weight ||
                    again.influences[slot].x != before[slot].x ||
                    again.influences[slot].y != before[slot].y) {
                    std::cerr << "MAR-175 canonical output is not a fixed point of the "
                                 ".marrow loader at vertex "
                              << index << ".\n";
                    return false;
                }
            }
        }
        std::filesystem::remove_all(round_trip_dir, copy_error);
    }

    // ── D6: non-finite input rejects, and the project is byte-identical ──
    //
    // `NaN <= 1e-6` is false, so a NaN survived both guards of the two
    // normalizers this story deletes and was then divided by itself, leaving
    // every influence on the vertex NaN. JSON cannot express a non-finite
    // literal, so this is the reachable entry point for that input.
    {
        marrow::editor::ProjectData rejected = *project_result.project;
        const std::string before = marrow::editor::serialize_project(rejected);
        const double nan_value = std::numeric_limits<double>::quiet_NaN();
        const double inf_value = std::numeric_limits<double>::infinity();
        const std::vector<std::pair<std::vector<MeshWeightInfluenceEdit>, std::string>> cases{
            {{{"spine", 0.0, 0.0, nan_value}}, "Bone 'spine' has a non-finite weight."},
            {{{"spine", 0.0, 0.0, inf_value}}, "Bone 'spine' has a non-finite weight."},
            {{{"spine", 0.0, 0.0, -inf_value}}, "Bone 'spine' has a non-finite weight."},
            {{{"spine", nan_value, 0.0, 1.0}}, "Bone 'spine' has a non-finite bind offset."},
            {{{"spine", 0.0, inf_value, 1.0}}, "Bone 'spine' has a non-finite bind offset."},
            {{{"nope", 0.0, 0.0, 1.0}}, "Bone not found: nope"},
            {{{"", 0.0, 0.0, 1.0}}, "A weighted influence requires a non-empty bone name."},
            {{{"spine", 0.0, 0.0, 0.0}},
             "A weighted vertex must keep at least one positive influence."},
        };
        for (const auto& [influences, message] : cases) {
            const auto result = author(&rejected, 1U, influences);
            if (result) {
                std::cerr << "MAR-175 accepted an invalid influence list.\n";
                return false;
            }
            if (result.error != message) {
                std::cerr << "MAR-175 rejection message was \"" << result.error
                          << "\", expected \"" << message << "\".\n";
                return false;
            }
            if (marrow::editor::serialize_project(rejected) != before) {
                std::cerr << "MAR-175 a rejected weight write changed the project.\n";
                return false;
            }
        }
    }

    // ── Export, on a project that was actually mutated and then written ──
    if (!export_project_baseline(project_result, baseline_json_path, baseline_binary_path)) {
        std::cerr << "MAR-175 could not export the untouched baseline.\n";
        return false;
    }
    const std::string case_a4_json = "/tmp/marrow_mar175_case_a4.mskl";
    const std::string case_a4_bin = "/tmp/marrow_mar175_case_a4.mbin";
    const std::string case_a3_json = "/tmp/marrow_mar175_case_a3.mskl";
    const std::string case_a3_bin = "/tmp/marrow_mar175_case_a3.mbin";
    const std::string case_a2_json = "/tmp/marrow_mar175_case_a2.mskl";
    const std::string case_a2_bin = "/tmp/marrow_mar175_case_a2.mbin";
    const std::string case_b_json = "/tmp/marrow_mar175_case_b.mskl";
    const std::string case_b_bin = "/tmp/marrow_mar175_case_b.mbin";

    const auto export_project = [&](const marrow::editor::ProjectData& project,
                                    const std::string& json_path,
                                    const std::string& binary_path) {
        marrow::editor::ProjectExportOptions options;
        options.skeleton_output_path = json_path;
        options.binary_output_path = binary_path;
        return marrow::editor::export_runtime_assets(
            project, *project_result.base_skeleton_document, options);
    };
    const auto file_size_of = [](const std::string& path) -> std::uintmax_t {
        std::error_code error;
        const auto size = std::filesystem::file_size(path, error);
        return error ? 0U : size;
    };
    const auto exported_influences = [&](const std::string& json_path, std::size_t vertex)
        -> std::vector<marrow::runtime::MeshGeometry::VertexWeight> {
        const auto reloaded = marrow::runtime::load_skeleton_data(json_path);
        if (!reloaded) {
            return {};
        }
        const auto slot = reloaded.skeleton_data->find_slot_index("body");
        const auto* reloaded_attachment = slot.has_value()
            ? reloaded.skeleton_data->find_attachment("mesh_base", *slot, "body_mesh")
            : nullptr;
        if (reloaded_attachment == nullptr ||
            reloaded_attachment->mesh_geometry == nullptr ||
            vertex >= reloaded_attachment->mesh_geometry->weights.size()) {
            return {};
        }
        return reloaded_attachment->mesh_geometry->weights[vertex].influences;
    };

    // Case A -- topology-changing. Four influences, then three, then two, on
    // one vertex. Every influence serializes as a fixed-shape object, so the
    // .mbin cost per influence is constant: the size must fall linearly.
    marrow::editor::ProjectData case_a = *project_result.project;
    // An adjacent attachment edit, to prove a weight write on one attachment
    // leaves another one untouched.
    marrow::editor::MeshWeightAttachmentEdit neighbour;
    neighbour.skin_name = "mage";
    neighbour.slot_name = "body";
    neighbour.attachment_name = "mage_body";
    MeshWeightVertexEdit neighbour_vertex;
    neighbour_vertex.influences = {{"spine", 7.0, 8.0, 1.0}};
    neighbour.vertices.push_back(neighbour_vertex);
    case_a.mesh_weight_attachment_edits.push_back(neighbour);

    if (!author(
            &case_a,
            1U,
            {{"root", 1.0, 2.0, 0.25},
             {"spine", 3.0, 4.0, 0.25},
             {"arm_l", 5.0, 6.0, 0.25},
             {"ik_upper", 7.0, 8.0, 0.25}})) {
        std::cerr << "MAR-175 Case A could not author four influences.\n";
        return false;
    }
    const std::string case_a_vertex0 = vertex_text(case_a, 0U);
    const std::string case_a_vertex3 = vertex_text(case_a, 3U);
    if (!export_project(case_a, case_a4_json, case_a4_bin)) {
        std::cerr << "MAR-175 Case A four-influence export failed.\n";
        return false;
    }
    const std::uintmax_t size_a4 = file_size_of(case_a4_bin);

    marrow::editor::ProjectData case_a3 = case_a;
    if (!author(
            &case_a3,
            1U,
            {{"root", 1.0, 2.0, 0.25},
             {"spine", 3.0, 4.0, 0.25},
             {"arm_l", 5.0, 6.0, 0.25},
             {"ik_upper", 7.0, 8.0, 0.0}})) {
        std::cerr << "MAR-175 Case A could not drop to three influences.\n";
        return false;
    }
    if (!export_project(case_a3, case_a3_json, case_a3_bin)) {
        std::cerr << "MAR-175 Case A three-influence export failed.\n";
        return false;
    }
    const std::uintmax_t size_a3 = file_size_of(case_a3_bin);

    marrow::editor::ProjectData case_a2 = case_a;
    // A duplicate bone and a zero weight -- the two inputs that used to commit
    // and then make the project unsavable -- collapse four entries into two.
    if (!author(
            &case_a2,
            1U,
            {{"root", 1.0, 2.0, 0.25},
             {"root", 3.0, 2.0, 0.25},
             {"arm_l", 5.0, 6.0, 0.5},
             {"ik_upper", 7.0, 8.0, 0.0}})) {
        std::cerr << "MAR-175 Case A could not collapse to two influences.\n";
        return false;
    }
    if (!export_project(case_a2, case_a2_json, case_a2_bin)) {
        std::cerr << "MAR-175 Case A two-influence export failed.\n";
        return false;
    }
    const std::uintmax_t size_a2 = file_size_of(case_a2_bin);

    if (size_a4 == 0U || size_a3 == 0U || size_a2 == 0U || size_a4 <= size_a3 ||
        size_a3 <= size_a2) {
        std::cerr << "MAR-175 Case A: the .mbin must shrink as influences are removed ("
                  << size_a4 << ", " << size_a3 << ", " << size_a2 << ").\n";
        return false;
    }
    const std::uintmax_t drop_one = size_a4 - size_a3;
    const std::uintmax_t drop_two = size_a3 - size_a2;
    if (drop_one != drop_two || (size_a4 - size_a2) != (2U * drop_one)) {
        std::cerr << "MAR-175 Case A: influence cost is not constant (" << drop_one
                  << " then " << drop_two << " bytes).\n";
        return false;
    }
    // Derivation, from src/runtime/binary.cpp's encode_value: an influence is an
    // object of four members, costing
    //
    //     1 object tag
    //   + 1 member-count varint
    //   + 1 String tag (the `bone` value)
    //   + 3 Number tags (`x`, `y`, `weight`)
    //   + 3 * 4 fixed-width float32 bytes
    //   + K   the five string-table index varints: the four keys plus the bone name
    //   = 18 + K
    //
    // K is 5 when every index fits in one varint byte and grows by one for each
    // index at or above 128. This fixture measures K = 7: `x` and `y` are
    // interned early by the geometry arrays while `bone` and `weight` first
    // appear in the weights block, past the 127th string. Asserting the model
    // rather than the bare number means an encoding change fails loudly and a
    // mere string-table reshuffle fails with a message that says so.
    constexpr std::uintmax_t kInfluenceFixedBytes = 18U;
    constexpr std::uintmax_t kMinIndexVarintBytes = 5U;
    constexpr std::uintmax_t kMaxIndexVarintBytes = 10U;
    constexpr std::uintmax_t kMeasuredIndexVarintBytes = 7U;
    if (drop_one < kInfluenceFixedBytes + kMinIndexVarintBytes ||
        drop_one > kInfluenceFixedBytes + kMaxIndexVarintBytes) {
        std::cerr << "MAR-175 Case A: one influence costs " << drop_one
                  << " .mbin bytes, outside the "
                  << (kInfluenceFixedBytes + kMinIndexVarintBytes) << ".."
                  << (kInfluenceFixedBytes + kMaxIndexVarintBytes)
                  << " the encoding allows. The binary encoding moved.\n";
        return false;
    }
    if (drop_one != kInfluenceFixedBytes + kMeasuredIndexVarintBytes) {
        std::cerr << "MAR-175 Case A: one influence costs " << drop_one
                  << " .mbin bytes; the recorded fixture value is "
                  << (kInfluenceFixedBytes + kMeasuredIndexVarintBytes)
                  << ". The encoding is intact (the cost is still 18 + K), but the "
                     "string table reshuffled, so K moved from "
                  << kMeasuredIndexVarintBytes << " to "
                  << (drop_one - kInfluenceFixedBytes) << ".\n";
        return false;
    }
    const auto decoded_a2 = exported_influences(case_a2_json, 1U);
    if (decoded_a2.size() != 2U) {
        std::cerr << "MAR-175 Case A: the exported vertex kept " << decoded_a2.size()
                  << " influences, expected 2.\n";
        return false;
    }
    {
        // root merges to 0.5, arm_l stays 0.5; ik_upper's zero is dropped.
        const auto root_index = project_result.skeleton_data->find_bone_index("root");
        const auto arm_index = project_result.skeleton_data->find_bone_index("arm_l");
        double root_weight = -1.0;
        double arm_weight = -1.0;
        for (const auto& influence : decoded_a2) {
            if (root_index.has_value() && influence.bone_index == *root_index) {
                root_weight = influence.weight;
            }
            if (arm_index.has_value() && influence.bone_index == *arm_index) {
                arm_weight = influence.weight;
            }
        }
        // `.mskl` keeps full double precision (MeshGeometry::VertexWeight::weight
        // is double), so this comparison is bit-exact, not float32-tolerant.
        if (root_weight != 0.5 || arm_weight != 0.5) {
            std::cerr << "MAR-175 Case A: exported weights were " << root_weight << " and "
                      << arm_weight << ", expected 0.5 and 0.5.\n";
            return false;
        }
    }
    // Survival: the vertices this write did not name, and the neighbouring
    // attachment edit, are byte-identical.
    if (vertex_text(case_a2, 0U) != case_a_vertex0 ||
        vertex_text(case_a2, 3U) != case_a_vertex3) {
        std::cerr << "MAR-175 Case A: an unnamed vertex changed.\n";
        return false;
    }
    {
        const auto* neighbour_after = case_a2.find_mesh_weight_attachment_edit(
            "mage", "body", "mage_body");
        if (neighbour_after == nullptr || neighbour_after->vertices.size() != 1U ||
            neighbour_after->vertices[0].influences.size() != 1U ||
            neighbour_after->vertices[0].influences[0].bone_name != "spine" ||
            neighbour_after->vertices[0].influences[0].x != 7.0 ||
            neighbour_after->vertices[0].influences[0].y != 8.0 ||
            neighbour_after->vertices[0].influences[0].weight != 1.0) {
            std::cerr << "MAR-175 Case A: the adjacent attachment edit did not survive.\n";
            return false;
        }
    }

    // Case B -- value-only. The influence SET is unchanged and only the weights
    // move, so the .mbin size must be identical: float32 is fixed width.
    marrow::editor::ProjectData case_b = case_a;
    if (!author(
            &case_b,
            1U,
            {{"root", 1.0, 2.0, 0.4},
             {"spine", 3.0, 4.0, 0.2},
             {"arm_l", 5.0, 6.0, 0.2},
             {"ik_upper", 7.0, 8.0, 0.2}})) {
        std::cerr << "MAR-175 Case B could not renormalize in place.\n";
        return false;
    }
    if (!export_project(case_b, case_b_json, case_b_bin)) {
        std::cerr << "MAR-175 Case B export failed.\n";
        return false;
    }
    const std::uintmax_t size_b = file_size_of(case_b_bin);
    if (size_b != size_a4) {
        std::cerr << "MAR-175 Case B: a value-only weight edit must not change the .mbin "
                     "size ("
                  << size_b << " vs " << size_a4 << ").\n";
        return false;
    }
    {
        const auto decoded_b = exported_influences(case_b_json, 1U);
        if (decoded_b.size() != 4U) {
            std::cerr << "MAR-175 Case B: the exported vertex lost an influence.\n";
            return false;
        }
        const auto root_index = project_result.skeleton_data->find_bone_index("root");
        double root_weight = -1.0;
        for (const auto& influence : decoded_b) {
            if (root_index.has_value() && influence.bone_index == *root_index) {
                root_weight = influence.weight;
            }
        }
        // Bit-exact on the `.mskl`, which stores doubles. The `.mbin` narrows the
        // same value to float32; that payload is checked for equivalence by
        // validate_binary_export below rather than re-decoded here, and no
        // assertion anywhere claims the float32 weights sum to exactly 1.0f.
        if (root_weight != 0.4) {
            std::cerr << std::setprecision(17)
                      << "MAR-175 Case B: exported root weight was " << root_weight
                      << ", expected 0.4.\n";
            return false;
        }
    }
    if (!validate_binary_export(case_b_json, case_b_bin) ||
        !validate_binary_export(case_a2_json, case_a2_bin)) {
        std::cerr << "MAR-175 exported .mskl and v2 .mbin payloads disagree.\n";
        return false;
    }
    std::cout << "MAR-175 Case A export: MBIN " << size_a4 << " -> " << size_a3 << " -> "
              << size_a2 << " bytes, exactly " << drop_one
              << " bytes per removed influence.\n";
    std::cout << "MAR-175 Case B export: MBIN " << size_b
              << " bytes, unchanged. Size is not a signal for a value-only weight edit -- "
                 "float32 is fixed width -- so the acceptance signal is the decoded weight "
                 "asserted above, and the signal that DOES discriminate for weights is the "
                 "influence count, which Case A moves.\n";
    std::cout << "MAR-175 baseline export for reference: JSON " << file_size_of(baseline_json_path)
              << " bytes, MBIN " << file_size_of(baseline_binary_path) << " bytes.\n";

    // The canonical write path never stores a skin name the exporter cannot
    // resolve (spec 2.5). Asserted rather than assumed.
    for (const auto& edit : case_a2.mesh_weight_attachment_edits) {
        if (edit.skin_name == "<unresolved>" || edit.skin_name.empty()) {
            std::cerr << "MAR-175 stored an unresolvable skin name on a weight edit.\n";
            return false;
        }
    }

    for (const auto& path : {case_a4_json, case_a4_bin, case_a3_json, case_a3_bin,
                             case_a2_json, case_a2_bin, case_b_json, case_b_bin,
                             baseline_json_path, baseline_binary_path}) {
        std::remove(path.c_str());
    }

    std::cout << "MAR-175 unified weight authoring validated: canonical output is savable, "
                 "reloadable, and a fixed point of its own rules.\n";
    return true;
}

int main(int argc, char** argv) {
    const ParseResult parse_result = parse_arguments(argc, argv);
    if (parse_result.status == ParseStatus::Help) {
        return 0;
    }
    if (parse_result.status != ParseStatus::Ok) {
        return 1;
    }

    if (parse_result.options.create_project && !create_project(parse_result.options)) {
        return 1;
    }

    const auto result = marrow::editor::load_project(parse_result.options.project_path);
    if (!result) {
        std::cerr << result.error->format();
        return 1;
    }

    print_summary(result, parse_result.options.project_path);
    if (!validate_viewport_settings(result, parse_result.options.create_project)) {
        return 1;
    }
    if (!validate_snap_settings(result)) {
        return 1;
    }
    // The editing suites below assert authored `player_idle.marrow` overlays.
    // A freshly created project has none, so it gets its own creation contract
    // instead of the fixture's shape.
    if (parse_result.options.create_project) {
        if (!validate_created_minimal_project(
                result, parse_result.options.project_path)) {
            return 1;
        }
    } else {
        if (!validate_undo_redo_cycle(result)) {
            return 1;
        }
        if (!validate_selection_reconciliation_transience(result)) {
            return 1;
        }
        if (!validate_animation_catalog_edits(result)) {
            return 1;
        }
        if (!validate_editing_p1_animation_duration(result)) {
            return 1;
        }
        if (!validate_editing_p0_end_to_end(result)) {
            return 1;
        }
        if (!validate_mar168_graph_scalar_authoring(result)) {
            return 1;
        }
        if (!validate_mar169_graph_interpolation_authoring(result)) {
            return 1;
        }
        if (!validate_mar170_curve_presets(result)) {
            return 1;
        }
        if (!validate_mar171_automatic_curves(result)) {
            return 1;
        }
        if (!validate_mar172_loop_boundary_sync(result)) {
            return 1;
        }
        if (!validate_mar173_key_time_scaling(result)) {
            return 1;
        }
        if (!validate_mar175_weight_authoring(result)) {
            return 1;
        }
    }
    if (parse_result.options.export_runtime_path.has_value() ||
        parse_result.options.export_binary_path.has_value()) {
        marrow::editor::ProjectExportOptions export_options;
        if (parse_result.options.export_runtime_path.has_value()) {
            export_options.skeleton_output_path = *parse_result.options.export_runtime_path;
        }
        export_options.binary_output_path = parse_result.options.export_binary_path;

        const auto export_result = marrow::editor::export_runtime_assets(
            *result.project,
            *result.base_skeleton_document,
            export_options);
        if (!export_result) {
            std::cerr << export_result.error->format() << '\n';
            return 1;
        }
        if (!validate_export_round_trip(result, export_result.path)) {
            return 1;
        }
        if (!validate_exported_atlas_bundle(result, export_result)) {
            return 1;
        }
        if (export_result.binary_path.has_value() &&
            !validate_binary_export(export_result.path, *export_result.binary_path)) {
            return 1;
        }
    }
    return 0;
}
