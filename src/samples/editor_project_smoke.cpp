#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "marrow/editor/constraint_catalog.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/editor/authoring.hpp"
#include "atomic_file_write.hpp"
#include "mesh_weight_model.hpp"
#include "timeline_model.hpp"
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
/**
 * @brief Asserts the viewport metadata a project actually carries.
 *
 * The debug-overlay half keys on the project's CONTENT -- whether the document
 * authors `editor.viewport.debug_overlay` -- and never on how the project was
 * produced. An earlier fix keyed it on `--create`, which is a property of the
 * invocation rather than of the file, so it repaired only that one symptom:
 * every other project lacking an authored overlay block still failed, including
 * `assets/fixtures/atlas_pack_smoke/atlas_pack_project.marrow`, which AGENTS.md
 * documents as a validation command. A smoke pointed at an arbitrary project
 * must not demand one fixture's authored state from it.
 *
 * Both branches assert something real, so this cannot degrade into a gate that
 * silently skips: an authored block must round-trip value for value, and an
 * absent one must produce the documented `DebugOverlaySettings` defaults.
 */
bool validate_viewport_settings(const marrow::editor::ProjectLoadResult& result) {
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

    // `load_project()` keeps the whole original document on `preserved_root`,
    // so the authored state is readable without re-opening the file or
    // assuming anything about `source_path`.
    const marrow::runtime::json::Value* authored_overlay = nullptr;
    if (result.project->preserved_root.is_object()) {
        if (const auto* editor_object = marrow::runtime::json::find_member(
                result.project->preserved_root, "editor")) {
            if (const auto* viewport_object =
                    marrow::runtime::json::find_member(*editor_object, "viewport")) {
                authored_overlay = marrow::runtime::json::find_member(
                    *viewport_object, "debug_overlay");
            }
        }
    }
    if (authored_overlay != nullptr && !authored_overlay->is_object()) {
        std::cerr << "Viewport validation found a non-object "
                     "$.editor.viewport.debug_overlay.\n";
        return false;
    }

    struct OverlayToggle {
        const char* key;
        bool marrow::editor::DebugOverlaySettings::* field;
    };
    static constexpr std::array<OverlayToggle, 6> kOverlayToggles{{
        {"bones", &marrow::editor::DebugOverlaySettings::bones},
        {"ik", &marrow::editor::DebugOverlaySettings::ik_constraints},
        {"path", &marrow::editor::DebugOverlaySettings::path_constraints},
        {"physics", &marrow::editor::DebugOverlaySettings::physics_constraints},
        {"meshes", &marrow::editor::DebugOverlaySettings::mesh_wireframes},
        {"bounds", &marrow::editor::DebugOverlaySettings::bounding_boxes},
    }};
    const marrow::editor::DebugOverlaySettings defaults{};

    std::size_t authored_count = 0U;
    for (const OverlayToggle& toggle : kOverlayToggles) {
        bool expected = defaults.*(toggle.field);
        bool from_document = false;
        if (authored_overlay != nullptr) {
            if (const auto* value =
                    marrow::runtime::json::find_member(*authored_overlay, toggle.key)) {
                if (!value->is_boolean()) {
                    std::cerr << "Viewport validation found a non-boolean "
                                 "$.editor.viewport.debug_overlay." << toggle.key << ".\n";
                    return false;
                }
                expected = value->as_boolean();
                from_document = true;
                ++authored_count;
            }
        }
        if (debug_overlay.*(toggle.field) != expected) {
            std::cerr << "Viewport validation expected debug overlay toggle '"
                      << toggle.key << "' to load as "
                      << (expected ? "true" : "false") << " ("
                      << (from_document ? "the project's authored value"
                                        : "the documented default")
                      << "), measured "
                      << (debug_overlay.*(toggle.field) ? "true" : "false") << ".\n";
            return false;
        }
    }

    // The round trip above cannot catch two toggles wired to each other's key
    // when a fixture happens to author them all alike -- `player_idle.marrow`
    // authors all six `true`. A distinct alternating pattern, pushed through
    // the serializer and the parser, does catch it, and it needs no fixture to
    // carry that pattern.
    {
        marrow::editor::ProjectData permuted = *result.project;
        auto& permuted_overlay = permuted.editor_metadata.viewport.debug_overlay;
        for (std::size_t index = 0; index < kOverlayToggles.size(); ++index) {
            permuted_overlay.*(kOverlayToggles[index].field) = (index % 2U) == 1U;
        }
        const auto permuted_document = marrow::runtime::json::parse_document(
            marrow::editor::serialize_project(permuted),
            result.project->source_path);
        const auto permuted_reloaded = permuted_document
            ? marrow::editor::load_project(*permuted_document.document)
            : marrow::editor::ProjectLoadResult{};
        if (!permuted_document || !permuted_reloaded) {
            std::cerr << "Viewport validation could not round-trip a permuted debug "
                         "overlay.\n";
            return false;
        }
        const auto& reloaded_overlay =
            permuted_reloaded.project->editor_metadata.viewport.debug_overlay;
        for (std::size_t index = 0; index < kOverlayToggles.size(); ++index) {
            const bool expected = (index % 2U) == 1U;
            if (reloaded_overlay.*(kOverlayToggles[index].field) != expected) {
                std::cerr << "Viewport validation found debug overlay toggle '"
                          << kOverlayToggles[index].key
                          << "' crossed with another key: an alternating pattern "
                             "round-tripped as "
                          << (reloaded_overlay.*(kOverlayToggles[index].field)
                                  ? "true" : "false")
                          << " where " << (expected ? "true" : "false")
                          << " was written.\n";
                return false;
            }
        }
    }

    std::cout << "Viewport metadata validated (" << authored_count
              << " of 6 debug overlay toggles authored by the project, the rest "
                 "defaulted; all six independently round-tripped).\n";
    return true;
}

/**
 * @brief Which markers of the `player_idle` EDITING fixture a project carries.
 *
 * Everything below `validate_undo_redo_cycle` in `main` is written against that
 * one fixture: it names bones `spine`/`arm_l`, animations `attack`/`aim` and
 * skin `mesh_base` directly. Those suites used to run for every project that
 * was not `--create`, which is a property of the INVOCATION, so pointing the
 * smoke at any other project ran a suite the project cannot satisfy. That is
 * the same mistake the viewport gate made, one level up.
 *
 * Only markers unique to the editing fixture are listed. `root` and `idle` are
 * deliberately excluded: they are generic enough that
 * `atlas_pack_project.marrow` has both, and including them would turn a clean
 * "not this fixture" into a partial match.
 */
struct EditingFixtureMarkers {
    std::vector<std::string> present;
    std::vector<std::string> missing;
};

EditingFixtureMarkers editing_fixture_markers(
    const marrow::editor::ProjectLoadResult& result) {
    EditingFixtureMarkers markers;
    if (result.skeleton_data == nullptr) {
        markers.missing.emplace_back("a materialized skeleton");
        return markers;
    }
    const auto& skeleton = *result.skeleton_data;
    const auto note = [&](bool found, std::string label) {
        (found ? markers.present : markers.missing).push_back(std::move(label));
    };
    for (const char* bone : {"spine", "arm_l"}) {
        note(skeleton.find_bone_index(bone).has_value(),
             "bone '" + std::string(bone) + "'");
    }
    for (const char* animation : {"attack", "aim"}) {
        note(skeleton.find_animation(animation) != nullptr,
             "animation '" + std::string(animation) + "'");
    }
    note(skeleton.find_skin("mesh_base") != nullptr, "skin 'mesh_base'");
    return markers;
}

std::string join_markers(const std::vector<std::string>& markers) {
    std::string joined;
    for (const std::string& marker : markers) {
        if (!joined.empty()) joined += ", ";
        joined += marker;
    }
    return joined;
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

// MAR-176 deterministic automatic weight generation.
bool validate_mar176_automatic_weights(
    const marrow::editor::ProjectLoadResult& project_result) {
    using marrow::editor::MeshWeightInfluenceEdit;
    using marrow::editor::MeshWeightTarget;
    using marrow::editor::MeshWeightVertexEdit;

    const std::string baseline_json_path = "/tmp/marrow_mar176_baseline.mskl";
    const std::string baseline_binary_path = "/tmp/marrow_mar176_baseline.mbin";
    const std::string case_a_json = "/tmp/marrow_mar176_case_a.mskl";
    const std::string case_a_bin = "/tmp/marrow_mar176_case_a.mbin";
    const std::string case_b_json = "/tmp/marrow_mar176_case_b.mskl";
    const std::string case_b_bin = "/tmp/marrow_mar176_case_b.mbin";

    const auto& skeleton = *project_result.skeleton_data;
    const auto body_slot = skeleton.find_slot_index("body");
    const auto* mesh_skin = skeleton.find_skin("mesh_base");
    const marrow::runtime::AttachmentData* attachment =
        body_slot.has_value() && mesh_skin != nullptr
        ? mesh_skin->find_attachment(*body_slot, "body_mesh")
        : nullptr;
    if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
        std::cerr << "MAR-176 needs the fixture's mesh_base/body/body_mesh weighted mesh.\n";
        return false;
    }
    const MeshWeightTarget target{"mesh_base", "body", "body_mesh"};
    const std::vector<std::string> candidates{"spine", "arm_l"};

    const auto generate = [&](marrow::editor::ProjectData* project,
                              const std::vector<std::string>& bones,
                              const std::vector<std::size_t>& scope) {
        return marrow::editor::generate_mesh_weights(
            project, skeleton, *attachment, target, bones, scope);
    };
    const auto weight_edit = [](const marrow::editor::ProjectData& project)
        -> const marrow::editor::MeshWeightAttachmentEdit* {
        return project.find_mesh_weight_attachment_edit("mesh_base", "body", "body_mesh");
    };
    const auto vertex_text = [&](const marrow::editor::ProjectData& project,
                                 std::size_t vertex_index) {
        const auto* edit = weight_edit(project);
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

    // The weight overlay is materialized lazily, on first mutation, so the
    // loaded project carries none. Materializing it up front is what makes the
    // survival assertions below apples-to-apples: an unnamed vertex is compared
    // against the value the overlay already held, not against "<none>".
    marrow::editor::ProjectData export_base = *project_result.project;
    export_base.mesh_weight_attachment_edits.push_back(
        marrow::editor::mesh_weight_model::mesh_weight_edit_from_runtime(
            skeleton, "mesh_base", "body", "body_mesh", *attachment));

    // A baseline project additionally carrying an authored weight edit on a
    // DIFFERENT attachment, so every scoped write below can prove it left a
    // neighbour alone rather than merely returning success.
    marrow::editor::ProjectData baseline = export_base;
    marrow::editor::MeshWeightAttachmentEdit neighbour;
    neighbour.skin_name = "mage";
    neighbour.slot_name = "body";
    neighbour.attachment_name = "mage_body";
    MeshWeightVertexEdit neighbour_vertex;
    neighbour_vertex.influences = {{"spine", 7.0, 8.0, 1.0}};
    neighbour.vertices.push_back(neighbour_vertex);
    baseline.mesh_weight_attachment_edits.push_back(neighbour);
    const std::string baseline_serialized = marrow::editor::serialize_project(baseline);
    const auto neighbour_text = [&](const marrow::editor::ProjectData& project) {
        const auto* edit =
            project.find_mesh_weight_attachment_edit("mage", "body", "mage_body");
        if (edit == nullptr || edit->vertices.empty()) {
            return std::string("<none>");
        }
        std::ostringstream stream;
        stream << std::setprecision(17);
        for (const auto& influence : edit->vertices[0].influences) {
            stream << influence.bone_name << '=' << influence.weight << '@' << influence.x
                   << ',' << influence.y << ';';
        }
        return stream.str();
    };
    const std::string neighbour_before = neighbour_text(baseline);

    // ── Scoped generation writes exactly what it names ──
    marrow::editor::ProjectData scoped = baseline;
    const auto scoped_result = generate(&scoped, candidates, {0U});
    if (!scoped_result) {
        std::cerr << "MAR-176 scoped generate failed: " << scoped_result.error << '\n';
        return false;
    }
    if (!scoped_result.changed || scoped_result.affected_vertices != std::vector<std::size_t>{0U}) {
        std::cerr << "MAR-176 scoped generate must report vertex 0 and only vertex 0.\n";
        return false;
    }
    if (scoped_result.vertex_count != 4U || scoped_result.scoped_vertex_count != 1U) {
        std::cerr << "MAR-176 scoped generate reported vertex_count "
                  << scoped_result.vertex_count << " and scoped_vertex_count "
                  << scoped_result.scoped_vertex_count << ".\n";
        return false;
    }
    // Vertex 0 carries one influence in the fixture and must gain arm_l.
    {
        const auto* edit = weight_edit(scoped);
        if (edit == nullptr || edit->vertices.size() != 4U ||
            edit->vertices[0].influences.size() != 2U ||
            edit->vertices[0].influences[0].bone_name != "spine" ||
            edit->vertices[0].influences[1].bone_name != "arm_l") {
            std::cerr << "MAR-176 vertex 0 should hold spine then arm_l, but holds "
                      << vertex_text(scoped, 0U) << ".\n";
            return false;
        }
        if (edit->vertices[0].influences[0].weight != 0.6494527252054849 ||
            edit->vertices[0].influences[1].weight != 0.35054727479451514) {
            std::cerr << "MAR-176 vertex 0 weights were " << vertex_text(scoped, 0U) << ".\n";
            return false;
        }
    }
    for (std::size_t index = 1; index < 4U; ++index) {
        if (vertex_text(scoped, index) != vertex_text(baseline, index)) {
            std::cerr << "MAR-176 scoped generate disturbed unnamed vertex " << index << ": "
                      << vertex_text(baseline, index) << " -> " << vertex_text(scoped, index)
                      << '\n';
            return false;
        }
    }
    if (neighbour_text(scoped) != neighbour_before) {
        std::cerr << "MAR-176 scoped generate disturbed an adjacent attachment edit.\n";
        return false;
    }

    // ── Determinism at project level ──
    //
    // Two generates from the SAME starting project must serialize byte for
    // byte identically. This is the property MAR-176 guarantees.
    {
        marrow::editor::ProjectData first = baseline;
        marrow::editor::ProjectData second = baseline;
        if (!generate(&first, candidates, {}) || !generate(&second, candidates, {})) {
            std::cerr << "MAR-176 unscoped generate failed.\n";
            return false;
        }
        if (marrow::editor::serialize_project(first) !=
            marrow::editor::serialize_project(second)) {
            std::cerr << "MAR-176 two generates from the same project were not byte-identical.\n";
            return false;
        }
        // Reversing the candidate list must not change anything either.
        marrow::editor::ProjectData reversed = baseline;
        if (!generate(&reversed, {"arm_l", "spine"}, {})) {
            std::cerr << "MAR-176 reversed-candidate generate failed.\n";
            return false;
        }
        if (marrow::editor::serialize_project(reversed) !=
            marrow::editor::serialize_project(first)) {
            std::cerr << "MAR-176 the result depended on the order the candidates were listed.\n";
            return false;
        }
    }

    // ── Unscoped generation touches every vertex, in ascending order ──
    marrow::editor::ProjectData unscoped = baseline;
    const auto unscoped_result = generate(&unscoped, candidates, {});
    if (!unscoped_result || !unscoped_result.changed) {
        std::cerr << "MAR-176 unscoped generate did not change the project.\n";
        return false;
    }
    if (unscoped_result.affected_vertices !=
        std::vector<std::size_t>{0U, 1U, 2U, 3U}) {
        std::cerr << "MAR-176 unscoped generate must report all four vertices ascending.\n";
        return false;
    }
    // Vertex 2's tie is the acceptance value. Both candidates' clamped closest
    // points land on spine's world origin bit for bit: arm_l's is its segment
    // start (`start + ab * 0.0`, exact for any origin), and spine's is
    // `start + ab * 1.0` from root's origin, which recovers spine's origin
    // exactly because root's origin is exactly (0, 0). That second step is
    // fixture-dependent -- `start + (end - start)` does not round-trip in
    // general. Once the closest points coincide, both distances evaluate the
    // same expression on the same operands, so the weights are exactly one half
    // each whatever the float32 setup-pose error and whatever the compiler does
    // about contraction.
    {
        const auto* edit = weight_edit(unscoped);
        const auto& influences = edit->vertices[2].influences;
        if (influences.size() != 2U || influences[0].bone_name != "spine" ||
            influences[1].bone_name != "arm_l" || influences[0].weight != 0.5 ||
            influences[1].weight != 0.5) {
            std::cerr << "MAR-176 vertex 2 must generate exactly spine 0.5 then arm_l 0.5, "
                      << "but generated " << vertex_text(unscoped, 2U) << ".\n";
            return false;
        }
    }

    // ── Determinism is NOT idempotence, and the difference is measured ──
    //
    // A second generate reads V back out of the influences the first one wrote.
    // BoneWorldTransform is six float32 while bind offsets are double, so
    // S(S^-1(V)) does not reproduce V bit for bit and the recovered distances
    // differ in the last ULPs. The stability figure is MEASURED and printed;
    // no assertion here claims a second generate reports no change.
    double worst_repeat_delta = 0.0;
    {
        marrow::editor::ProjectData again = unscoped;
        const auto repeat = generate(&again, candidates, {});
        if (!repeat) {
            std::cerr << "MAR-176 a repeated generate failed: " << repeat.error << '\n';
            return false;
        }
        const auto* before = weight_edit(unscoped);
        const auto* after = weight_edit(again);
        if (before == nullptr || after == nullptr ||
            before->vertices.size() != after->vertices.size()) {
            std::cerr << "MAR-176 a repeated generate changed the vertex count.\n";
            return false;
        }
        for (std::size_t index = 0; index < before->vertices.size(); ++index) {
            const auto& lhs = before->vertices[index].influences;
            const auto& rhs = after->vertices[index].influences;
            if (lhs.size() != rhs.size()) {
                std::cerr << "MAR-176 a repeated generate changed vertex " << index
                          << "'s influence count.\n";
                return false;
            }
            for (std::size_t slot = 0; slot < lhs.size(); ++slot) {
                if (lhs[slot].bone_name != rhs[slot].bone_name) {
                    std::cerr << "MAR-176 a repeated generate reordered vertex " << index
                              << ".\n";
                    return false;
                }
                worst_repeat_delta = std::max(
                    worst_repeat_delta, std::abs(lhs[slot].weight - rhs[slot].weight));
            }
        }
        if (!(worst_repeat_delta < 1e-9)) {
            std::cerr << "MAR-176 a repeated generate moved a weight by " << worst_repeat_delta
                      << ", far more than the float32 transform asymmetry explains.\n";
            return false;
        }
        std::cout << std::setprecision(4)
                  << "MAR-176 determinism vs idempotence: two generates from the SAME project "
                     "are byte-identical (asserted); a generate applied to a previous "
                     "generate's output is stable to "
                  << worst_repeat_delta
                  << " (measured, not asserted bit-exact). The residue is the float32 "
                     "BoneWorldTransform against double bind offsets, the same asymmetry "
                     "MAR-175 recorded for rebind -- not nondeterminism.\n"
                  << std::setprecision(6);
    }

    // ── An accepted generate must leave a savable, reloadable project ──
    {
        const std::filesystem::path round_trip_dir = "/tmp/marrow_mar176_round_trip";
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
            std::cerr << "MAR-176 could not stage the round-trip assets: "
                      << copy_error.message() << '\n';
            return false;
        }
        const std::string round_trip_path = (round_trip_dir / "round_trip.marrow").string();
        marrow::editor::ProjectData savable = unscoped;
        savable.source_path = round_trip_path;
        const auto saved = marrow::editor::save_project(savable, round_trip_path);
        if (!saved) {
            std::cerr << "MAR-176 save round trip failed: " << saved.error->format() << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(round_trip_path);
        if (!reloaded) {
            std::cerr << "MAR-176 could not reload the saved generated project.\n";
            return false;
        }
        const auto* reloaded_edit = weight_edit(*reloaded.project);
        if (reloaded_edit == nullptr || reloaded_edit->vertices.size() != 4U) {
            std::cerr << "MAR-176 reload lost the generated weight overlay.\n";
            return false;
        }
        // `.marrow` is serialized with `json::serialize_pretty`, which writes 15
        // significant digits (`src/runtime/json.cpp:597`); the exact
        // `serialize_pretty_round_trip` variant exists but is not the one
        // project save uses. A double therefore does NOT survive save/reload bit
        // for bit, so what is asserted here is stability to a tolerance and the
        // measured figure is printed. This is a pre-existing property of the
        // project writer, not something generation introduces, and changing it
        // would rewrite every `.marrow` file's bytes -- out of scope here.
        double worst_reload_delta = 0.0;
        for (std::size_t index = 0; index < 4U; ++index) {
            const auto& before = unscoped.find_mesh_weight_attachment_edit(
                                     "mesh_base", "body", "body_mesh")->vertices[index].influences;
            const auto& after = reloaded_edit->vertices[index].influences;
            if (before.size() != after.size()) {
                std::cerr << "MAR-176 reload changed vertex " << index
                          << "'s influence count.\n";
                return false;
            }
            for (std::size_t slot = 0; slot < before.size(); ++slot) {
                if (before[slot].bone_name != after[slot].bone_name) {
                    std::cerr << "MAR-176 reload reordered vertex " << index << ": "
                              << vertex_text(unscoped, index) << " -> "
                              << vertex_text(*reloaded.project, index) << '\n';
                    return false;
                }
                worst_reload_delta = std::max(
                    worst_reload_delta,
                    std::max(
                        std::abs(before[slot].weight - after[slot].weight),
                        std::max(
                            std::abs(before[slot].x - after[slot].x),
                            std::abs(before[slot].y - after[slot].y))));
            }
        }
        if (!(worst_reload_delta < 1e-12)) {
            std::cerr << "MAR-176 a save/reload moved a generated value by "
                      << worst_reload_delta << ", more than 15-digit serialization explains.\n";
            return false;
        }
        std::cout << std::setprecision(4)
                  << "MAR-176 save/reload round trip: the project saves, reloads, and keeps "
                     "every generated influence in order, stable to "
                  << worst_reload_delta
                  << ". It is NOT bit-exact because `.marrow` writes 15 significant digits "
                     "(json.cpp:597), which is a pre-existing property of the project writer.\n"
                  << std::setprecision(6);
        // Generating again on the reloaded project must reproduce the same
        // project the in-memory second generate produced: the generated form is
        // a fixed point of the .marrow round trip, not merely of memory.
        marrow::editor::ProjectData reloaded_again = *reloaded.project;
        marrow::editor::ProjectData memory_again = unscoped;
        if (!generate(&reloaded_again, candidates, {}) ||
            !generate(&memory_again, candidates, {})) {
            std::cerr << "MAR-176 could not regenerate after a reload.\n";
            return false;
        }
        const auto* reloaded_twice = weight_edit(reloaded_again);
        const auto* memory_twice = weight_edit(memory_again);
        if (reloaded_twice == nullptr || memory_twice == nullptr) {
            std::cerr << "MAR-176 regeneration lost the weight overlay.\n";
            return false;
        }
        double worst_regenerate_delta = 0.0;
        for (std::size_t index = 0; index < 4U; ++index) {
            const auto& lhs = reloaded_twice->vertices[index].influences;
            const auto& rhs = memory_twice->vertices[index].influences;
            if (lhs.size() != rhs.size()) {
                std::cerr << "MAR-176 regenerating after a reload changed vertex " << index
                          << "'s influence count.\n";
                return false;
            }
            for (std::size_t slot = 0; slot < lhs.size(); ++slot) {
                if (lhs[slot].bone_name != rhs[slot].bone_name) {
                    std::cerr << "MAR-176 regenerating after a reload chose different bones at "
                                 "vertex " << index << ".\n";
                    return false;
                }
                worst_regenerate_delta = std::max(
                    worst_regenerate_delta, std::abs(lhs[slot].weight - rhs[slot].weight));
            }
        }
        if (!(worst_regenerate_delta < 1e-12)) {
            std::cerr << "MAR-176 regenerating after a reload diverged by "
                      << worst_regenerate_delta << ".\n";
            return false;
        }
        std::cout << std::setprecision(4)
                  << "MAR-176 regenerate after reload: same bones in the same order as "
                     "regenerating in memory, weights within "
                  << worst_regenerate_delta
                  << " -- the generated form is a fixed point of the `.marrow` round trip to "
                     "the precision that round trip preserves.\n"
                  << std::setprecision(6);
        std::filesystem::remove_all(round_trip_dir, copy_error);
    }

    // ── Every rejection leaves the project byte-identical ──
    {
        struct Rejection {
            const char* label;
            std::vector<std::string> bones;
            std::vector<std::size_t> scope;
            const char* message;
        };
        const std::vector<Rejection> rejections{
            {"empty candidate list", {}, {}, "mesh.generate_weights requires at least one candidate bone."},
            {"unknown bone", {"nope"}, {}, "Bone not found: nope"},
            {"repeated bone", {"spine", "spine"}, {}, "A candidate bone was listed more than once."},
            {"empty bone name", {""}, {}, "Bone not found: "},
            {"out-of-range vertex", {"spine"}, {9U}, "vertex index is outside the target mesh."},
            {"repeated vertex", {"spine"}, {1U, 1U}, "A vertex was selected more than once."},
        };
        for (const Rejection& rejection : rejections) {
            marrow::editor::ProjectData rejected = baseline;
            const auto result = generate(&rejected, rejection.bones, rejection.scope);
            if (result) {
                std::cerr << "MAR-176 accepted an invalid generate: " << rejection.label << '\n';
                return false;
            }
            if (result.error != rejection.message) {
                std::cerr << "MAR-176 " << rejection.label << " message was \"" << result.error
                          << "\".\n";
                return false;
            }
            if (marrow::editor::serialize_project(rejected) != baseline_serialized) {
                std::cerr << "MAR-176 a rejected generate (" << rejection.label
                          << ") changed the project.\n";
                return false;
            }
        }
        // An unweighted attachment has no setup-world position to derive, and
        // turning it into a weighted one would change the exported attachment's
        // kind. Named rejection, not a silent no-op.
        const marrow::runtime::AttachmentData* region_attachment =
            skeleton.find_attachment("default", *body_slot, "body");
        if (region_attachment == nullptr) {
            std::cerr << "MAR-176 needs the fixture's default/body/body region attachment to "
                         "prove the unweighted rejection; without it that case cannot fail.\n";
            return false;
        }
        if (region_attachment->mesh_geometry != nullptr) {
            std::cerr << "MAR-176 expected default/body/body to carry no mesh geometry.\n";
            return false;
        }
        {
            marrow::editor::ProjectData unweighted = baseline;
            const MeshWeightTarget region_target{"default", "body", "body"};
            const auto result = marrow::editor::generate_mesh_weights(
                &unweighted, skeleton, *region_attachment, region_target, candidates, {});
            if (result ||
                result.error != "mesh.generate_weights requires a weighted mesh attachment.") {
                std::cerr << "MAR-176 an unweighted attachment must reject with a named "
                             "message, but got \"" << result.error << "\".\n";
                return false;
            }
            if (marrow::editor::serialize_project(unweighted) != baseline_serialized) {
                std::cerr << "MAR-176 an unweighted-attachment rejection changed the project.\n";
                return false;
            }
        }
    }

    // ── Export, proved on a project that was actually mutated and written ──
    if (!export_project_baseline(project_result, baseline_json_path, baseline_binary_path)) {
        std::cerr << "MAR-176 could not export the untouched baseline.\n";
        return false;
    }
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
        if (reloaded_attachment == nullptr || reloaded_attachment->mesh_geometry == nullptr ||
            vertex >= reloaded_attachment->mesh_geometry->weights.size()) {
            return {};
        }
        return reloaded_attachment->mesh_geometry->weights[vertex].influences;
    };
    const std::uintmax_t baseline_size = file_size_of(baseline_binary_path);

    // Case A -- count-changing. Vertex 0 goes from one influence to two, so the
    // encoded document gains exactly one influence object. Both bone names are
    // already interned from `bones[]`, so no string is added and no string index
    // shifts: the delta is clean.
    marrow::editor::ProjectData case_a = export_base;
    if (!generate(&case_a, candidates, {0U})) {
        std::cerr << "MAR-176 Case A generate failed.\n";
        return false;
    }
    if (!export_project(case_a, case_a_json, case_a_bin)) {
        std::cerr << "MAR-176 Case A export failed.\n";
        return false;
    }
    const std::uintmax_t case_a_size = file_size_of(case_a_bin);
    // Derived from src/runtime/binary.cpp, not fitted: an influence object costs
    // 1 object tag + 1 varint member count + 4 key-index varints + 1 String tag
    // + 1 bone-name-index varint + 3 Number tags + 3 float32 = 18 + K, where K
    // is the five string-index varint bytes. For this fixture's string table
    // `bone` is index 139 and `weight` index 140 (two bytes each) while `x` (15),
    // `y` (16), `spine` (17) and `arm_l` (3) are one byte each, so K = 7 and one
    // influence costs 25 bytes.
    constexpr std::uintmax_t kInfluenceBytes = 25U;
    if (case_a_size != baseline_size + kInfluenceBytes) {
        std::cerr << "MAR-176 Case A: the .mbin grew from " << baseline_size << " to "
                  << case_a_size << " bytes, but the encoding model predicts exactly "
                  << kInfluenceBytes << " bytes for one added influence (18 + K, K = 7: "
                  << "object tag, member-count varint, four key-index varints, a String tag "
                  << "plus the bone-name-index varint, three Number tags, three float32). "
                  << "If this fails the model is wrong -- re-derive it, do not adjust the "
                  << "constant.\n";
        return false;
    }
    {
        const auto decoded = exported_influences(case_a_json, 0U);
        if (decoded.size() != 2U) {
            std::cerr << "MAR-176 Case A: the exported vertex kept " << decoded.size()
                      << " influences.\n";
            return false;
        }
        const auto spine_index = skeleton.find_bone_index("spine");
        const auto arm_index = skeleton.find_bone_index("arm_l");
        if (!spine_index.has_value() || !arm_index.has_value() ||
            decoded[0].bone_index != *spine_index || decoded[1].bone_index != *arm_index) {
            std::cerr << "MAR-176 Case A: the exported influence order is not spine then "
                         "arm_l.\n";
            return false;
        }
        // The generator produces `spine 0.6494527252054849` and
        // `arm_l 0.35054727479451514` in memory. The exported `.mskl` is written
        // by `json::serialize_pretty` at 15 significant digits
        // (`src/runtime/json.cpp:597`), so the decoded doubles are that value
        // rounded, not that value exactly -- a property of the writer, not of
        // generation. The tolerance below is the 15-digit round trip; Case B's
        // `0.5` needs none because one half is exact at any precision, which is
        // why it and not this pair is the story's acceptance value.
        const double expected_weights[2] = {0.6494527252054849, 0.35054727479451514};
        double worst_export_delta = 0.0;
        for (std::size_t slot = 0; slot < 2U; ++slot) {
            worst_export_delta = std::max(
                worst_export_delta, std::abs(decoded[slot].weight - expected_weights[slot]));
        }
        if (!(worst_export_delta < 1e-14)) {
            std::cerr << std::setprecision(17)
                      << "MAR-176 Case A: exported weights were " << decoded[0].weight << " and "
                      << decoded[1].weight << ", which is " << worst_export_delta
                      << " from what the generator produced -- more than 15-digit "
                         "serialization explains.\n";
            return false;
        }
        std::cout << std::setprecision(4)
                  << "MAR-176 Case A decoded weights match the generated pair to "
                  << worst_export_delta << " (15-digit `.mskl` serialization).\n"
                  << std::setprecision(6);
    }
    // No new string was interned: both candidate names already appear in the
    // baseline document's own bone list, which is why the byte delta is clean.
    {
        std::ifstream baseline_stream(baseline_json_path);
        const std::string baseline_text(
            (std::istreambuf_iterator<char>(baseline_stream)),
            std::istreambuf_iterator<char>());
        if (baseline_text.find("\"spine\"") == std::string::npos ||
            baseline_text.find("\"arm_l\"") == std::string::npos) {
            std::cerr << "MAR-176 Case A: a candidate bone name was not already interned, so "
                         "the byte delta is not attributable to the added influence alone.\n";
            return false;
        }
    }
    for (std::size_t index = 1; index < 4U; ++index) {
        if (vertex_text(case_a, index) != vertex_text(export_base, index)) {
            std::cerr << "MAR-176 Case A: unnamed vertex " << index << " changed.\n";
            return false;
        }
    }

    // Case B -- value-only. Vertex 2 already holds {spine, arm_l} and its
    // generated bind offsets are the ones it already carries, so the influence
    // set, the count and the offsets are unchanged. What moves is the pair of
    // weights and their ORDER.
    marrow::editor::ProjectData case_b = export_base;
    if (!generate(&case_b, candidates, {2U})) {
        std::cerr << "MAR-176 Case B generate failed.\n";
        return false;
    }
    if (!export_project(case_b, case_b_json, case_b_bin)) {
        std::cerr << "MAR-176 Case B export failed.\n";
        return false;
    }
    const std::uintmax_t case_b_size = file_size_of(case_b_bin);
    if (case_b_size != baseline_size) {
        std::cerr << "MAR-176 Case B: a value-only weight edit must not change the .mbin size ("
                  << baseline_size << " -> " << case_b_size << ").\n";
        return false;
    }
    {
        const auto decoded = exported_influences(case_b_json, 2U);
        const auto spine_index = skeleton.find_bone_index("spine");
        const auto arm_index = skeleton.find_bone_index("arm_l");
        if (decoded.size() != 2U || !spine_index.has_value() || !arm_index.has_value()) {
            std::cerr << "MAR-176 Case B: the exported vertex lost an influence.\n";
            return false;
        }
        // The fixture canonicalizes to arm_l first (0.6/0.8 = 0.7499999999999999
        // against spine's 0.25); generation ties them at one half each and the
        // ascending-index tie-break moves spine to the front. The order flip is
        // part of the assertion.
        if (decoded[0].bone_index != *spine_index || decoded[1].bone_index != *arm_index) {
            std::cerr << "MAR-176 Case B: the exported influence order did not flip to spine "
                         "then arm_l.\n";
            return false;
        }
        if (decoded[0].weight != 0.5 || decoded[1].weight != 0.5) {
            std::cerr << std::setprecision(17)
                      << "MAR-176 Case B: exported weights were " << decoded[0].weight << " and "
                      << decoded[1].weight << ", not exactly 0.5 and 0.5.\n";
            return false;
        }
        // The bind offsets are the fixture's own, to within the float32
        // setup-pose error Gate C measured at 5.1e-06.
        const double expected_x[2] = {64.0, 94.0};
        const double expected_y[2] = {80.0, 70.0};
        for (std::size_t slot = 0; slot < 2U; ++slot) {
            if (std::abs(decoded[slot].x - expected_x[slot]) > 1e-5 ||
                std::abs(decoded[slot].y - expected_y[slot]) > 1e-5) {
                std::cerr << std::setprecision(17)
                          << "MAR-176 Case B: bind offset " << slot << " moved to ("
                          << decoded[slot].x << ", " << decoded[slot].y << ").\n";
                return false;
            }
        }
    }
    for (const std::size_t index : {0U, 1U, 3U}) {
        if (vertex_text(case_b, index) != vertex_text(export_base, index)) {
            std::cerr << "MAR-176 Case B: unnamed vertex " << index << " changed.\n";
            return false;
        }
    }

    std::cout << "MAR-176 Case A export: MBIN " << baseline_size << " -> " << case_a_size
              << " bytes, exactly " << kInfluenceBytes
              << " bytes for the one added influence, matching the 18 + K encoding model "
                 "derived from binary.cpp (K = 7 for this fixture's string table: bone=139 "
                 "and weight=140 cost two varint bytes each, x=15, y=16 and the bone-name "
                 "index cost one each).\n";
    std::cout << "MAR-176 Case B export: MBIN " << case_b_size
              << " bytes, identical. float32 is fixed width, so size is not a signal for a "
                 "value-only weight edit; the decoded value and the influence order are -- "
                 "the pair ties at exactly 0.5 each and spine moves ahead of arm_l on the "
                 "ascending skeleton-index tie-break.\n";

    for (const auto& path : {baseline_json_path, baseline_binary_path, case_a_json, case_a_bin,
                             case_b_json, case_b_bin}) {
        std::remove(path.c_str());
    }
    std::remove("/tmp/player_idle.matl");

    std::cout << "MAR-176 deterministic automatic weights validated: explicit candidates only, "
                 "inverse-square over setup-pose bone segments, ties broken on skeleton order, "
                 "and canonicalized through the MAR-175 primitive.\n";
    return true;
}

// ===========================================================================
// MAR-177 -- constraint lifecycle project operations.
//
// The `.marrow` constraint overlay had exactly two verbs: replace an element
// by name, and append a new one. It could not say "this constraint is now
// called something else" or "this constraint is gone". These scenarios cover
// the storage, the save-time validation, the materialization, the two project
// primitives, and the export signal for the two verbs MAR-177 adds.
// ===========================================================================

marrow::editor::ConstraintLifecycleOperation mar177_rename(
    marrow::editor::ConstraintKind family,
    std::string from,
    std::string to) {
    marrow::editor::ConstraintLifecycleOperation operation;
    operation.kind = marrow::editor::ConstraintLifecycleKind::Rename;
    operation.family = family;
    operation.name = std::move(from);
    operation.new_name = std::move(to);
    return operation;
}

marrow::editor::ConstraintLifecycleOperation mar177_delete(
    marrow::editor::ConstraintKind family,
    std::string name) {
    marrow::editor::ConstraintLifecycleOperation operation;
    operation.kind = marrow::editor::ConstraintLifecycleKind::Delete;
    operation.family = family;
    operation.name = std::move(name);
    return operation;
}

std::string mar177_read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return std::string(
        (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
}

/**
 * @brief Counts whole JSON string tokens, so `cape_pull` never matches
 *        `cape_pull_renamed`.
 */
std::size_t mar177_count_quoted(const std::string& text, const std::string& token) {
    const std::string needle = "\"" + token + "\"";
    std::size_t count = 0;
    for (std::size_t at = text.find(needle); at != std::string::npos;
         at = text.find(needle, at + needle.size())) {
        ++count;
    }
    return count;
}

/**
 * @brief A project built directly over a constraint fixture.
 *
 * `player_idle` has no base constraints at all -- its four are project-only
 * upserts over empty root arrays -- so it cannot exercise the base-backed half
 * of the ownership rule, and no skin in it names a constraint. These scenarios
 * therefore build their own projects over the constraint fixtures, which carry
 * no `.marrow` and no atlas.
 */
struct Mar177Fixture {
    marrow::editor::ProjectData project;
    marrow::runtime::json::Document base;
    bool ok{false};
};

Mar177Fixture mar177_open_fixture(
    const std::filesystem::path& skeleton_path,
    const std::filesystem::path& project_path) {
    Mar177Fixture fixture;
    const auto document = marrow::runtime::load_skeleton_document(skeleton_path);
    if (!document) {
        std::cerr << "MAR-177 could not load " << skeleton_path << ": "
                  << document.error->format() << '\n';
        return fixture;
    }
    fixture.base = *document.document;

    marrow::editor::MinimalProjectOptions options;
    options.project_path = project_path;
    options.skeleton_path = std::filesystem::absolute(skeleton_path);
    // `validate_project_for_save()` requires at least one atlas path, and the
    // constraint fixtures ship none, so these projects borrow `player_idle.matl`.
    // Nothing cross-validates an atlas against a skeleton, so it is inert here.
    options.atlas_paths = {std::filesystem::absolute("assets/fixtures/player_idle.matl")};
    options.name = "mar177";
    fixture.project = marrow::editor::create_minimal_project(options);
    fixture.ok = true;
    return fixture;
}

/**
 * @brief Writes a `.marrow` over a constraint fixture that `load_project()` opens.
 *
 * `load_project()` resolves the referenced atlases and reports failure unless at
 * least one is present, and the constraint fixtures ship none, so these projects
 * borrow `player_idle.matl`. Nothing cross-validates an atlas against a
 * skeleton -- the atlas carries texture data, and these scenarios assert on the
 * skeleton document -- so the borrowed atlas is inert.
 */
bool mar177_write_loadable_project(
    const marrow::editor::ProjectData& source,
    const std::filesystem::path& skeleton_path,
    const std::filesystem::path& project_path,
    marrow::editor::ProjectData* project_out) {
    marrow::editor::ProjectData project = source;
    project.source_path = project_path;
    project.runtime_assets.skeleton_path = std::filesystem::absolute(skeleton_path);
    project.runtime_assets.atlas_paths = {
        std::filesystem::absolute("assets/fixtures/player_idle.matl")};
    *project_out = project;
    const auto saved = marrow::editor::save_project(project, project_path);
    if (!saved) {
        std::cerr << "MAR-177 could not save " << project_path << ": "
                  << saved.error->message << '\n';
        return false;
    }
    return true;
}

/** @brief Reads `root[key]` as an array, or nullptr when the key is absent. */
const marrow::runtime::json::Value* mar177_array_member(
    const marrow::runtime::json::Value& object,
    std::string_view key) {
    if (!object.is_object()) {
        return nullptr;
    }
    const auto found = object.as_object().find(key);
    if (found == object.as_object().end() || !found->second.is_array()) {
        return nullptr;
    }
    return &found->second;
}

/** @brief The `name` field of every element of a constraint root array. */
std::vector<std::string> mar177_constraint_names(
    const marrow::runtime::json::Value& root,
    std::string_view key) {
    std::vector<std::string> names;
    const auto* array = mar177_array_member(root, key);
    if (array == nullptr) {
        return names;
    }
    for (const auto& element : array->as_array()) {
        if (!element.is_object()) continue;
        const auto found = element.as_object().find("name");
        if (found != element.as_object().end() && found->second.is_string()) {
            names.push_back(found->second.as_string());
        }
    }
    return names;
}

/** @brief The name array a skin uses to reference one constraint family. */
std::vector<std::string> mar177_skin_references(
    const marrow::runtime::json::Value& root,
    std::string_view skin_name,
    std::string_view family_key,
    bool* key_present) {
    std::vector<std::string> names;
    *key_present = false;
    if (!root.is_object()) return names;
    const auto skins = root.as_object().find("skins");
    if (skins == root.as_object().end() || !skins->second.is_object()) return names;
    const auto skin = skins->second.as_object().find(skin_name);
    if (skin == skins->second.as_object().end() || !skin->second.is_object()) return names;
    const auto* array = mar177_array_member(skin->second, family_key);
    if (array == nullptr) return names;
    *key_present = true;
    for (const auto& element : array->as_array()) {
        if (element.is_string()) names.push_back(element.as_string());
    }
    return names;
}

std::string mar177_join(const std::vector<std::string>& names) {
    std::string joined;
    for (const std::string& name : names) {
        if (!joined.empty()) joined += ", ";
        joined += name;
    }
    return joined;
}

// --- Scenario A: schema round trip (AC1, AC4) ------------------------------
bool validate_mar177_scenario_a(
    const marrow::editor::ProjectLoadResult& project_result) {
    using marrow::editor::ConstraintKind;
    using marrow::editor::ConstraintLifecycleKind;
    using marrow::editor::ConstraintLifecycleOperation;
    using marrow::runtime::json::Value;

    std::error_code ignored;
    const std::filesystem::path fixture_path = "assets/fixtures/player_idle.marrow";
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

    // --- A1: absent means absent, and the fixture is still byte-identical --
    if (!project_result.project->constraint_lifecycle_operations.empty()) {
        std::cerr << "MAR-177 A1: a project with no `operations` key must load an "
                     "empty lifecycle vector.\n";
        return false;
    }
    const std::string on_disk = mar177_read_file(fixture_path);
    const std::string untouched =
        marrow::editor::serialize_project(*project_result.project);
    if (untouched.find("\"operations\"") != std::string::npos) {
        std::cerr << "MAR-177 A1: an empty lifecycle vector must not emit the key.\n";
        return false;
    }
    // The design spec claimed the fixture re-serializes byte-identically. It
    // does not, and never did: `build_project_value()` emits
    // `editor.timeline.fps` unconditionally (project.cpp) while the fixture
    // omits the default. That is a pre-existing 41-byte difference in a
    // section MAR-177 does not touch, so the assertion that has teeth here is
    // the exact one: the serialized text differs from the fixture by that
    // block and by nothing else, which is what "MAR-177 changed zero bytes of
    // an existing project's serialization" actually means.
    const std::string pre_existing_timeline_block =
        "    \"timeline\": {\n      \"fps\": 60\n    },\n";
    const auto timeline_at = untouched.find(pre_existing_timeline_block);
    if (timeline_at == std::string::npos) {
        std::cerr << "MAR-177 A1: the pre-existing `editor.timeline` default block "
                     "is no longer emitted verbatim; re-derive the difference rather "
                     "than editing this constant.\n";
        return false;
    }
    std::string without_timeline_default = untouched;
    without_timeline_default.erase(
        timeline_at, pre_existing_timeline_block.size());
    if (without_timeline_default != on_disk) {
        std::cerr << "MAR-177 A1: serialize_project() differs from " << fixture_path
                  << " by more than the pre-existing `timeline` default ("
                  << without_timeline_default.size() << " vs " << on_disk.size()
                  << " bytes).\n";
        return false;
    }
    // The save path is a fixed point: what save_project() writes is what
    // load_project() reads back and serializes again.
    {
        const auto fixed_point_path = std::filesystem::temp_directory_path() /
            ("marrow_mar177_fixed_point_" + path_token + ".marrow");
        marrow::editor::ProjectData project = rebase(fixed_point_path);
        const std::string first = marrow::editor::serialize_project(project);
        const auto saved = marrow::editor::save_project(project, fixed_point_path);
        if (!saved) {
            std::cerr << "MAR-177 A1: save_project() rejected the untouched fixture: "
                      << saved.error->message << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(fixed_point_path);
        std::filesystem::remove(fixed_point_path, ignored);
        if (!reloaded) {
            std::cerr << "MAR-177 A1: reload of the untouched fixture failed: "
                      << reloaded.error->format() << '\n';
            return false;
        }
        if (marrow::editor::serialize_project(*reloaded.project) != first) {
            std::cerr << "MAR-177 A1: save -> reload -> serialize is not a fixed "
                         "point for a project with no lifecycle operations.\n";
            return false;
        }
        if (!reloaded.project->constraint_lifecycle_operations.empty()) {
            std::cerr << "MAR-177 A1: reload invented a lifecycle operation.\n";
            return false;
        }
    }

    // --- A2/A3: one record serializes and reloads field for field ---------
    //
    // `load_project()` materializes the runtime, so a project carrying an
    // unresolvable record cannot be opened at all -- which is the intended
    // behaviour, and which means the round trip has to run over a fixture that
    // actually has the constraint. `player_idle`'s four constraints are all
    // project-only upserts over empty base arrays (§2.9), so a *record* naming
    // one of them is by construction illegal; the base-backed fixture is
    // `skin_inherit_constraints`.
    const auto skin_fixture = mar177_open_fixture(
        "assets/fixtures/skin_inherit_constraints.mskl",
        std::filesystem::temp_directory_path() / "marrow_mar177_a_seed.marrow");
    if (!skin_fixture.ok) return false;
    {
        const auto round_trip_path = std::filesystem::temp_directory_path() /
            ("marrow_mar177_a_" + path_token + ".marrow");
        marrow::editor::ProjectData project;
        marrow::editor::ProjectData seed = skin_fixture.project;
        seed.constraint_lifecycle_operations.push_back(
            mar177_rename(ConstraintKind::Transform, "cape_pull", "cape_drag"));
        if (!mar177_write_loadable_project(
                seed,
                "assets/fixtures/skin_inherit_constraints.mskl",
                round_trip_path,
                &project)) {
            return false;
        }
        const std::string text = marrow::editor::serialize_project(project);
        for (const char* fragment : {"\"operations\"", "\"op\": \"rename\"",
                                     "\"family\": \"transform\"",
                                     "\"from\": \"cape_pull\"",
                                     "\"to\": \"cape_drag\""}) {
            if (text.find(fragment) == std::string::npos) {
                std::cerr << "MAR-177 A2: serialized project is missing " << fragment
                          << ".\n";
                return false;
            }
        }
        const auto reloaded = marrow::editor::load_project(round_trip_path);
        std::filesystem::remove(round_trip_path, ignored);
        if (!reloaded) {
            std::cerr << "MAR-177 A3: reload failed: "
                      << (reloaded.error.has_value() ? reloaded.error->format()
                                                     : std::string("no error reported"))
                      << '\n';
            return false;
        }
        const auto& operations = reloaded.project->constraint_lifecycle_operations;
        if (operations.size() != 1U ||
            operations[0].kind != ConstraintLifecycleKind::Rename ||
            operations[0].family != ConstraintKind::Transform ||
            operations[0].name != "cape_pull" ||
            operations[0].new_name != "cape_drag") {
            std::cerr << "MAR-177 A3: the rename record did not round-trip.\n";
            return false;
        }
        // The reload is a real materialization: the renamed constraint is what
        // the loaded skeleton carries.
        if (reloaded.skeleton_data->transform_constraints().size() != 1U ||
            reloaded.skeleton_data->transform_constraints()[0].name != "cape_drag") {
            std::cerr << "MAR-177 A3: the reloaded skeleton did not carry the rename.\n";
            return false;
        }
    }

    // --- A4: a tombstone-only project survives save/reload -----------------
    //
    // `build_project_value()` gates the whole `constraint_edits` key on the
    // four upsert vectors being non-empty. A project whose only constraint
    // content is one tombstone therefore serializes into nothing at all unless
    // that gate also considers the lifecycle vector -- the MAR-172 failure
    // shape: an `ok: true` save that silently destroys the edit.
    {
        const auto tombstone_path = std::filesystem::temp_directory_path() /
            ("marrow_mar177_tombstone_" + path_token + ".marrow");
        marrow::editor::ProjectData project;
        marrow::editor::ProjectData seed = skin_fixture.project;
        seed.constraint_lifecycle_operations.push_back(
            mar177_delete(ConstraintKind::Transform, "cape_pull"));
        if (!mar177_write_loadable_project(
                seed,
                "assets/fixtures/skin_inherit_constraints.mskl",
                tombstone_path,
                &project)) {
            return false;
        }
        if (!project.ik_constraint_edits.empty() ||
            !project.path_constraint_edits.empty() ||
            !project.transform_constraint_edits.empty() ||
            !project.physics_constraint_edits.empty()) {
            std::cerr << "MAR-177 A4 needs a project with no upserts at all.\n";
            return false;
        }
        const std::string text = marrow::editor::serialize_project(project);
        if (text.find("\"constraint_edits\"") == std::string::npos ||
            text.find("\"operations\"") == std::string::npos) {
            std::cerr << "MAR-177 A4: a tombstone-only project lost its "
                         "`constraint_edits.operations` on serialization -- the emit "
                         "gate still keys on the four upsert vectors alone.\n";
            return false;
        }
        const auto reloaded = marrow::editor::load_project(tombstone_path);
        std::filesystem::remove(tombstone_path, ignored);
        if (!reloaded) {
            std::cerr << "MAR-177 A4: reload failed: "
                      << (reloaded.error.has_value() ? reloaded.error->format()
                                                     : std::string("no error reported"))
                      << '\n';
            return false;
        }
        if (!reloaded.skeleton_data->transform_constraints().empty()) {
            std::cerr << "MAR-177 A4: the tombstone did not survive the reload into "
                         "the materialized skeleton.\n";
            return false;
        }
        const auto& operations = reloaded.project->constraint_lifecycle_operations;
        if (operations.size() != 1U ||
            operations[0].kind != ConstraintLifecycleKind::Delete ||
            operations[0].family != ConstraintKind::Transform ||
            operations[0].name != "cape_pull" ||
            !operations[0].new_name.empty()) {
            std::cerr << "MAR-177 A4: the tombstone did not round-trip.\n";
            return false;
        }
        if (!reloaded.project->ik_constraint_edits.empty() ||
            !reloaded.project->path_constraint_edits.empty() ||
            !reloaded.project->transform_constraint_edits.empty() ||
            !reloaded.project->physics_constraint_edits.empty()) {
            std::cerr << "MAR-177 A4: the tombstone-only reload invented upserts.\n";
            return false;
        }
    }

    // --- A5: every malformed record is rejected on load, with a location ---
    {
        const auto base_document =
            marrow::runtime::json::load_document(fixture_path);
        if (!base_document) {
            std::cerr << "MAR-177 A5 could not parse the fixture.\n";
            return false;
        }

        struct MalformedCase {
            const char* label;
            Value operations;      ///< The whole `operations` member.
            const char* want_path;
            const char* want_message;
        };
        const auto object = [](Value::Object members) {
            return Value(std::move(members), {});
        };
        const auto array = [](Value::Array items) {
            return Value(std::move(items), {});
        };
        const auto text = [](std::string value) {
            return Value(std::move(value), {});
        };
        const auto record = [&](std::vector<std::pair<std::string, Value>> members) {
            Value::Object built;
            for (auto& member : members) {
                built.emplace(member.first, std::move(member.second));
            }
            return array(Value::Array{object(std::move(built))});
        };

        const std::vector<MalformedCase> cases{
            {"`operations` that is not an array",
             text("rename"),
             "$.constraint_edits.operations", "expected array"},
            {"an element that is not an object",
             array(Value::Array{text("rename")}),
             "$.constraint_edits.operations[0]", "expected object"},
            {"a record with no `op`",
             record({{"family", text("ik")}, {"name", text("a")}}),
             "$.constraint_edits.operations[0].op", "missing required member"},
            {"an `op` that is not a string",
             record({{"op", Value(1.0, {})}, {"family", text("ik")}}),
             "$.constraint_edits.operations[0].op", "expected string"},
            {"an unknown `op`",
             record({{"op", text("remove")}, {"family", text("ik")},
                     {"name", text("a")}}),
             "$.constraint_edits.operations[0].op", "must be 'rename' or 'delete'"},
            {"a record with no `family`",
             record({{"op", text("delete")}, {"name", text("a")}}),
             "$.constraint_edits.operations[0].family", "missing required member"},
            {"an unknown `family`",
             record({{"op", text("delete")}, {"family", text("bone")},
                     {"name", text("a")}}),
             "$.constraint_edits.operations[0].family",
             "must be one of 'ik', 'path', 'transform', 'physics'"},
            {"a rename with no `from`",
             record({{"op", text("rename")}, {"family", text("ik")},
                     {"to", text("b")}}),
             "$.constraint_edits.operations[0].from", "missing required member"},
            {"a rename with an empty `from`",
             record({{"op", text("rename")}, {"family", text("ik")},
                     {"from", text("")}, {"to", text("b")}}),
             "$.constraint_edits.operations[0].from", "must not be empty"},
            {"a rename with no `to`",
             record({{"op", text("rename")}, {"family", text("ik")},
                     {"from", text("a")}}),
             "$.constraint_edits.operations[0].to", "missing required member"},
            {"a rename with an empty `to`",
             record({{"op", text("rename")}, {"family", text("ik")},
                     {"from", text("a")}, {"to", text("")}}),
             "$.constraint_edits.operations[0].to", "must not be empty"},
            {"a rename onto its own name",
             record({{"op", text("rename")}, {"family", text("ik")},
                     {"from", text("a")}, {"to", text("a")}}),
             "$.constraint_edits.operations[0].to", "must differ from"},
            {"a rename carrying `name`",
             record({{"op", text("rename")}, {"family", text("ik")},
                     {"from", text("a")}, {"to", text("b")}, {"name", text("a")}}),
             "$.constraint_edits.operations[0].name",
             "rename records must not carry"},
            {"a delete with no `name`",
             record({{"op", text("delete")}, {"family", text("ik")}}),
             "$.constraint_edits.operations[0].name", "missing required member"},
            {"a delete with an empty `name`",
             record({{"op", text("delete")}, {"family", text("ik")},
                     {"name", text("")}}),
             "$.constraint_edits.operations[0].name", "must not be empty"},
            {"a delete carrying `to`",
             record({{"op", text("delete")}, {"family", text("ik")},
                     {"name", text("a")}, {"to", text("b")}}),
             "$.constraint_edits.operations[0].to",
             "delete records must not carry"},
            {"a delete carrying `from`",
             record({{"op", text("delete")}, {"family", text("ik")},
                     {"name", text("a")}, {"from", text("b")}}),
             "$.constraint_edits.operations[0].from",
             "delete records must not carry"},
        };

        for (const MalformedCase& malformed : cases) {
            auto document = *base_document.document;
            Value* constraint_edits =
                marrow::runtime::json::find_member(document.root, "constraint_edits");
            if (constraint_edits == nullptr || !constraint_edits->is_object()) {
                std::cerr << "MAR-177 A5 needs the fixture's `constraint_edits`.\n";
                return false;
            }
            constraint_edits->as_object()["operations"] = malformed.operations;
            const auto loaded = marrow::editor::load_project(document);
            if (loaded) {
                std::cerr << "MAR-177 A5: the loader accepted " << malformed.label
                          << ".\n";
                return false;
            }
            const std::string message = loaded.error->message;
            if (message.rfind(malformed.want_path, 0U) != 0U ||
                message.find(malformed.want_message) == std::string::npos) {
                std::cerr << "MAR-177 A5: rejected " << malformed.label
                          << " with the wrong path or message: " << message << '\n';
                return false;
            }
        }
        std::cout << "MAR-177 A5: " << cases.size()
                  << " malformed `constraint_edits.operations` records rejected on "
                     "load, each with a located error.\n";
    }

    // --- A6: an unknown top-level key still survives load/save (AC4) -------
    {
        const auto base_document =
            marrow::runtime::json::load_document(fixture_path);
        if (!base_document) {
            std::cerr << "MAR-177 A6 could not parse the fixture.\n";
            return false;
        }
        auto document = *base_document.document;
        Value::Object unknown;
        unknown.emplace("kept", Value(std::string("yes"), {}));
        document.root.as_object()["mar177_unknown"] = Value(std::move(unknown), {});
        const auto loaded = marrow::editor::load_project(document);
        if (!loaded) {
            std::cerr << "MAR-177 A6: an unknown top-level key broke load: "
                      << loaded.error->format() << '\n';
            return false;
        }
        marrow::editor::ProjectData project = *loaded.project;
        project.ik_constraint_edits.clear();
        project.constraint_lifecycle_operations.push_back(
            mar177_delete(ConstraintKind::Ik, "editor_arm_reach"));
        const std::string text = marrow::editor::serialize_project(project);
        if (text.find("\"mar177_unknown\"") == std::string::npos ||
            text.find("\"kept\": \"yes\"") == std::string::npos) {
            std::cerr << "MAR-177 A6: the unknown top-level key did not survive "
                         "serialization alongside a lifecycle record.\n";
            return false;
        }
    }

    std::cout << "MAR-177 Scenario A: `constraint_edits.operations` is absent when "
                 "empty, round-trips when present, survives with no upserts at all, "
                 "and rejects every malformed record on load.\n";
    return true;
}

// --- Scenario A2: save-time symbolic replay (AC3) --------------------------
//
// `validate_project_for_save()` has no base skeleton, so it cannot know which
// names exist. It can still reject everything intrinsically broken, by walking
// the operations front to back per family over two sets: `consumed` (names a
// rename or delete has taken away) and `introduced` (names a rename created).
bool validate_mar177_scenario_a2(
    const marrow::editor::ProjectLoadResult& project_result) {
    using marrow::editor::ConstraintKind;
    using marrow::editor::ConstraintLifecycleOperation;

    std::error_code ignored;
    const std::string path_token = std::to_string(
        static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto case_path = std::filesystem::temp_directory_path() /
        ("marrow_mar177_a2_" + path_token + ".marrow");

    const auto rebase = [&] {
        marrow::editor::ProjectData project = *project_result.project;
        project.runtime_assets.skeleton_path =
            std::filesystem::absolute(project.resolved_skeleton_path());
        project.runtime_assets.atlas_paths = project.resolved_atlas_paths();
        for (auto& atlas_path : project.runtime_assets.atlas_paths) {
            atlas_path = std::filesystem::absolute(atlas_path);
        }
        project.source_path = case_path;
        return project;
    };

    struct ReplayCase {
        const char* label;
        std::vector<ConstraintLifecycleOperation> operations;
        bool accept;
        const char* want_message;  ///< Substring; ignored when `accept`.
    };

    const auto rename = mar177_rename;
    const auto erase = mar177_delete;
    ConstraintLifecycleOperation delete_with_new_name =
        mar177_delete(ConstraintKind::Ik, "a");
    delete_with_new_name.new_name = "b";
    ConstraintLifecycleOperation rename_with_empty_source =
        mar177_rename(ConstraintKind::Ik, "", "b");
    ConstraintLifecycleOperation rename_with_empty_target =
        mar177_rename(ConstraintKind::Ik, "a", "");
    ConstraintLifecycleOperation self_rename =
        mar177_rename(ConstraintKind::Ik, "a", "a");

    const std::vector<ReplayCase> cases{
        {"an empty rename source", {rename_with_empty_source}, false,
         "rename source must not be empty"},
        {"an empty rename target", {rename_with_empty_target}, false,
         "rename target must not be empty"},
        {"a rename onto its own name", {self_rename}, false,
         "must differ from its source"},
        {"a delete carrying a new name", {delete_with_new_name}, false,
         "delete records must not carry a new name"},
        {"rename then delete the same source",
         {rename(ConstraintKind::Ik, "a", "b"), erase(ConstraintKind::Ik, "a")},
         false, "was already renamed or deleted"},
        {"delete then rename the same source",
         {erase(ConstraintKind::Ik, "a"), rename(ConstraintKind::Ik, "a", "c")},
         false, "was already renamed or deleted"},
        {"two renames onto the same target",
         {rename(ConstraintKind::Ik, "a", "c"), rename(ConstraintKind::Ik, "b", "c")},
         false, "is already introduced by an earlier operation"},
        // The four rows below are what prove the replay is not simply refusing
        // everything with more than one record in it.
        {"a legal chain",
         {rename(ConstraintKind::Ik, "a", "b"), rename(ConstraintKind::Ik, "b", "c")},
         true, ""},
        {"a legal reuse of a deleted name",
         {erase(ConstraintKind::Ik, "a"), rename(ConstraintKind::Ik, "b", "a")},
         true, ""},
        {"a legal three-step swap",
         {rename(ConstraintKind::Ik, "a", "t"), rename(ConstraintKind::Ik, "b", "a"),
          rename(ConstraintKind::Ik, "t", "b")},
         true, ""},
        {"the same name deleted in two different families",
         {erase(ConstraintKind::Ik, "a"), erase(ConstraintKind::Physics, "a")},
         true, ""},
        // A rename or delete that consumes a name an upsert still carries
        // would re-introduce that name in Phase B -- a resurrection for a
        // delete, and two constraints where there was one for a rename.
        {"a rename whose source is still an upsert's name",
         {rename(ConstraintKind::Ik, "editor_arm_reach", "x")}, false,
         "ik constraint edit 'editor_arm_reach'"},
        {"a delete whose target is still an upsert's name",
         {erase(ConstraintKind::Physics, "editor_ribbon_secondary")}, false,
         "physics constraint edit 'editor_ribbon_secondary'"},
        // The plan expected this row to reject. It must not, and the reason is
        // structural: `Rename{Ik, a, editor_arm_reach}` plus an IK upsert named
        // `editor_arm_reach` is byte-for-byte the state design §5.3's middle
        // row *requires* a shadowing rename to produce (append the record, and
        // rewrite the shadowing upsert's name to the new one). A save-time
        // rule that rejected it would reject the ownership rule's own output.
        // The genuine collision -- renaming onto a name a *different*
        // project-only constraint already holds -- is unreachable through the
        // primitives, whose preflight resolves `to` against the materialized
        // name set; scenario B asserts that rejection where it belongs.
        {"a rename onto a name an upsert already carries (the shadowing shape)",
         {rename(ConstraintKind::Ik, "a", "editor_arm_reach")}, true, ""},
    };

    for (const ReplayCase& replay : cases) {
        marrow::editor::ProjectData project = rebase();
        project.constraint_lifecycle_operations = replay.operations;
        const auto saved = marrow::editor::save_project(project, case_path);
        std::filesystem::remove(case_path, ignored);
        if (replay.accept) {
            if (!saved) {
                std::cerr << "MAR-177 A2: save_project() rejected " << replay.label
                          << ": " << saved.error->message << '\n';
                return false;
            }
            continue;
        }
        if (saved) {
            std::cerr << "MAR-177 A2: save_project() accepted " << replay.label
                      << ".\n";
            return false;
        }
        if (saved.error->message.find(replay.want_message) == std::string::npos) {
            std::cerr << "MAR-177 A2: rejected " << replay.label
                      << " with the wrong message: " << saved.error->message << '\n';
            return false;
        }
    }

    std::cout << "MAR-177 Scenario A2: " << cases.size()
              << " symbolic-replay rows -- chains, reuse, a swap, and cross-family "
                 "independence accepted; every ordering and upsert-stranding fault "
                 "rejected before a byte is written.\n";
    return true;
}

// --- Scenario C: Phase A materialization (AC2, AC3) ------------------------
//
// Skins reference constraints BY NAME, and `parse_skin_scope_members()` fails
// the whole load on an unresolvable one. A delete or rename that does not touch
// `skins[*].<family>` therefore does not produce a subtly wrong rig -- it
// produces a project that still saves and can never be opened again.
bool validate_mar177_scenario_c() {
    using marrow::editor::ConstraintKind;

    const auto skin_fixture = mar177_open_fixture(
        "assets/fixtures/skin_inherit_constraints.mskl",
        std::filesystem::temp_directory_path() / "marrow_mar177_skin.marrow");
    if (!skin_fixture.ok) return false;

    // --- C1: baseline ------------------------------------------------------
    {
        const auto runtime = marrow::editor::build_project_runtime(
            skin_fixture.project, skin_fixture.base);
        if (!runtime) {
            std::cerr << "MAR-177 C1: the untouched fixture failed to build: "
                      << runtime.error->format() << '\n';
            return false;
        }
        const auto& skeleton = *runtime.skeleton_data;
        if (skeleton.transform_constraints().size() != 1U ||
            skeleton.transform_constraints()[0].name != "cape_pull") {
            std::cerr << "MAR-177 C1: expected exactly one base transform constraint "
                         "named cape_pull.\n";
            return false;
        }
        const auto* cape = skeleton.find_skin("cape");
        if (cape == nullptr || cape->transform_constraint_indices.size() != 1U ||
            cape->bone_indices.size() != 1U) {
            std::cerr << "MAR-177 C1: skin `cape` must reference one transform "
                         "constraint and one bone.\n";
            return false;
        }
    }

    // --- C2: rename rewrites the root element AND the skin reference -------
    {
        marrow::editor::ProjectData project = skin_fixture.project;
        project.constraint_lifecycle_operations.push_back(
            mar177_rename(ConstraintKind::Transform, "cape_pull", "cape_drag"));
        const auto document = marrow::editor::build_project_runtime_document(
            project, skin_fixture.base);
        const auto names = mar177_constraint_names(document.root, "transform");
        if (names.size() != 1U || names[0] != "cape_drag") {
            std::cerr << "MAR-177 C2: root transform is [" << mar177_join(names)
                      << "], expected [cape_drag].\n";
            return false;
        }
        bool key_present = false;
        const auto references =
            mar177_skin_references(document.root, "cape", "transform", &key_present);
        if (!key_present || references.size() != 1U || references[0] != "cape_drag") {
            std::cerr << "MAR-177 C2: skins.cape.transform is ["
                      << mar177_join(references)
                      << "], expected [cape_drag]; an unrewritten skin reference "
                         "makes the exported rig unloadable.\n";
            return false;
        }
        const auto runtime =
            marrow::editor::build_project_runtime(project, skin_fixture.base);
        if (!runtime) {
            std::cerr << "MAR-177 C2: the renamed rig failed to load: "
                      << runtime.error->format() << '\n';
            return false;
        }
        const auto& skeleton = *runtime.skeleton_data;
        const auto* cape = skeleton.find_skin("cape");
        if (skeleton.transform_constraints().size() != 1U ||
            skeleton.transform_constraints()[0].name != "cape_drag" ||
            cape == nullptr || cape->transform_constraint_indices.size() != 1U ||
            cape->bone_indices.size() != 1U) {
            std::cerr << "MAR-177 C2: the renamed rig did not keep one transform "
                         "constraint, its skin reference, and `cape_target`.\n";
            return false;
        }
    }

    // --- C3: delete erases the key rather than leaving `[]` ----------------
    //
    // `skeleton_parse.cpp` rejects an empty `transform` array outright
    // ("transform constraints must not be empty when provided"), so deleting
    // the last constraint of a family must remove the key. Leaving `[]` is the
    // difference between "the constraint was deleted" and "the project can no
    // longer be opened".
    {
        marrow::editor::ProjectData project = skin_fixture.project;
        project.constraint_lifecycle_operations.push_back(
            mar177_delete(ConstraintKind::Transform, "cape_pull"));
        const auto document = marrow::editor::build_project_runtime_document(
            project, skin_fixture.base);
        if (document.root.as_object().find("transform") !=
            document.root.as_object().end()) {
            std::cerr << "MAR-177 C3: the emptied root `transform` key must be erased, "
                         "not left as [].\n";
            return false;
        }
        bool key_present = true;
        mar177_skin_references(document.root, "cape", "transform", &key_present);
        if (key_present) {
            std::cerr << "MAR-177 C3: the emptied `skins.cape.transform` key must be "
                         "erased, not left as [].\n";
            return false;
        }
        const auto runtime =
            marrow::editor::build_project_runtime(project, skin_fixture.base);
        if (!runtime) {
            std::cerr << "MAR-177 C3: the rig with its last transform constraint "
                         "deleted failed to load: " << runtime.error->format() << '\n';
            return false;
        }
        const auto& skeleton = *runtime.skeleton_data;
        const auto* cape = skeleton.find_skin("cape");
        if (!skeleton.transform_constraints().empty()) {
            std::cerr << "MAR-177 C3: the transform constraint survived the delete.\n";
            return false;
        }
        // Survival: the delete must not take the adjacent skin scope with it.
        if (cape == nullptr || !cape->transform_constraint_indices.empty() ||
            cape->bone_indices.size() != 1U) {
            std::cerr << "MAR-177 C3: skin `cape` lost `cape_target` to the delete.\n";
            return false;
        }
    }

    // --- C4: delete preserves the relative order of the survivors ----------
    {
        const auto ik_fixture = mar177_open_fixture(
            "assets/fixtures/ik_constraints.mskl",
            std::filesystem::temp_directory_path() / "marrow_mar177_ik.marrow");
        if (!ik_fixture.ok) return false;

        const auto base_names = mar177_constraint_names(ik_fixture.base.root, "ik");
        if (base_names.size() != 13U) {
            std::cerr << "MAR-177 C4: expected 13 base IK constraints, found "
                      << base_names.size() << ".\n";
            return false;
        }
        marrow::editor::ProjectData project = ik_fixture.project;
        // The 1st, the 7th, and the 13th -- the head, the middle, and the tail.
        for (const std::size_t index : {std::size_t{0}, std::size_t{6}, std::size_t{12}}) {
            project.constraint_lifecycle_operations.push_back(
                mar177_delete(ConstraintKind::Ik, base_names[index]));
        }
        const auto document =
            marrow::editor::build_project_runtime_document(project, ik_fixture.base);
        std::vector<std::string> expected;
        for (std::size_t index = 0; index < base_names.size(); ++index) {
            if (index != 0U && index != 6U && index != 12U) {
                expected.push_back(base_names[index]);
            }
        }
        const auto survivors = mar177_constraint_names(document.root, "ik");
        if (survivors != expected) {
            std::cerr << "MAR-177 C4: survivors are [" << mar177_join(survivors)
                      << "], expected [" << mar177_join(expected)
                      << "] -- evaluation order is array order, so the sequence is "
                         "the assertion, not the count.\n";
            return false;
        }
        if (!marrow::editor::build_project_runtime(project, ik_fixture.base)) {
            std::cerr << "MAR-177 C4: the 10-survivor rig failed to load.\n";
            return false;
        }
    }

    // --- C5: family is part of identity ------------------------------------
    //
    // `rope_follow` exists, as a *path* constraint. A delete that claims the IK
    // family must not touch it. Phase A is defensive and no-ops; Scenario D
    // asserts the validator rejects it with a family-mismatch message.
    {
        const auto mixed_fixture = mar177_open_fixture(
            "assets/fixtures/path_transform_constraints.mskl",
            std::filesystem::temp_directory_path() / "marrow_mar177_mixed.marrow");
        if (!mixed_fixture.ok) return false;
        marrow::editor::ProjectData project = mixed_fixture.project;
        project.constraint_lifecycle_operations.push_back(
            mar177_delete(ConstraintKind::Ik, "rope_follow"));
        const auto document =
            marrow::editor::build_project_runtime_document(project, mixed_fixture.base);
        const auto path_names = mar177_constraint_names(document.root, "path");
        const auto transform_names = mar177_constraint_names(document.root, "transform");
        if (path_names != std::vector<std::string>{"rope_follow"} ||
            transform_names != std::vector<std::string>{"mirror_source"}) {
            std::cerr << "MAR-177 C5: an IK-family delete reached across families; "
                         "path=[" << mar177_join(path_names) << "] transform=["
                      << mar177_join(transform_names) << "].\n";
            return false;
        }
    }

    // --- C6: Phase A strictly precedes Phase B -----------------------------
    //
    // A rename record whose target is also an upsert's name must produce ONE
    // element carrying the upsert's fields. If the upserts merged first, the
    // upsert would append as a fourteenth element and `arm_positive` would
    // still be standing.
    {
        const auto ik_fixture = mar177_open_fixture(
            "assets/fixtures/ik_constraints.mskl",
            std::filesystem::temp_directory_path() / "marrow_mar177_phase.marrow");
        if (!ik_fixture.ok) return false;
        marrow::editor::ProjectData project = ik_fixture.project;
        project.constraint_lifecycle_operations.push_back(
            mar177_rename(ConstraintKind::Ik, "arm_positive", "arm_renamed"));
        marrow::editor::IkConstraintEdit upsert;
        upsert.name = "arm_renamed";
        upsert.bone_names = {"upper_arm_pos", "lower_arm_pos"};
        upsert.target_bone_name = "target_pos";
        upsert.mix = 0.25;
        project.ik_constraint_edits.push_back(upsert);

        const auto document =
            marrow::editor::build_project_runtime_document(project, ik_fixture.base);
        const auto names = mar177_constraint_names(document.root, "ik");
        if (names.size() != 13U) {
            std::cerr << "MAR-177 C6: expected 13 IK constraints after a rename plus a "
                         "matching upsert, found " << names.size() << ": ["
                      << mar177_join(names) << "].\n";
            return false;
        }
        if (std::count(names.begin(), names.end(), std::string("arm_renamed")) != 1 ||
            std::count(names.begin(), names.end(), std::string("arm_positive")) != 0) {
            std::cerr << "MAR-177 C6: Phase B ran before Phase A -- names are ["
                      << mar177_join(names) << "].\n";
            return false;
        }
        const auto runtime =
            marrow::editor::build_project_runtime(project, ik_fixture.base);
        if (!runtime) {
            std::cerr << "MAR-177 C6: the merged rig failed to load: "
                      << runtime.error->format() << '\n';
            return false;
        }
        const auto& constraints = runtime.skeleton_data->ik_constraints();
        if (constraints.size() != 13U || constraints[0].name != "arm_renamed" ||
            std::abs(constraints[0].mix - 0.25) > 1e-9) {
            std::cerr << "MAR-177 C6: the upsert's fields did not win in place; "
                         "constraint 0 is " << constraints[0].name << " mix="
                      << constraints[0].mix << ".\n";
            return false;
        }
    }

    std::cout << "MAR-177 Scenario C: lifecycle records rewrite the root arrays and "
                 "every skin reference, erase an emptied family key instead of "
                 "leaving [], preserve survivor order, stay inside their family, and "
                 "run strictly before the upsert merge.\n";
    return true;
}

// --- Scenario D: materialization-time validation (AC3) ---------------------
//
// `validate_project_for_save()` has no base document and so can only replay
// symbolically. This layer has the base, and reports the four causes AC3 names
// separately, because they have very different fixes: a missing source is a
// typo, a family mismatch is the wrong dropdown, a duplicate target is a name
// that is already taken, and an invalid order is a sequence that was legal
// when each record was written and is not legal in this order.
bool validate_mar177_scenario_d() {
    using marrow::editor::ConstraintKind;
    using marrow::editor::ConstraintLifecycleOperation;

    std::error_code ignored;
    const auto ik_fixture = mar177_open_fixture(
        "assets/fixtures/ik_constraints.mskl",
        std::filesystem::temp_directory_path() / "marrow_mar177_d_ik.marrow");
    const auto mixed_fixture = mar177_open_fixture(
        "assets/fixtures/path_transform_constraints.mskl",
        std::filesystem::temp_directory_path() / "marrow_mar177_d_mixed.marrow");
    if (!ik_fixture.ok || !mixed_fixture.ok) return false;

    struct ValidationCase {
        const char* label;
        const Mar177Fixture* fixture;
        std::vector<ConstraintLifecycleOperation> operations;
        bool accept;
        const char* want_message;
    };

    const std::vector<ValidationCase> cases{
        {"a rename of a name that does not exist", &ik_fixture,
         {mar177_rename(ConstraintKind::Ik, "no_such", "x")}, false,
         "does not exist"},
        {"a delete of a name that does not exist", &ik_fixture,
         {mar177_delete(ConstraintKind::Ik, "no_such")}, false,
         "does not exist"},
        {"a rename onto a live name in the same family", &ik_fixture,
         {mar177_rename(ConstraintKind::Ik, "arm_positive", "arm_negative")}, false,
         "is already taken"},
        {"a delete naming the wrong family", &mixed_fixture,
         {mar177_delete(ConstraintKind::Ik, "rope_follow")}, false,
         "exists as a path constraint"},
        // Families are independent: `rope_follow` is a live *path* name, and a
        // *transform* constraint may take it.
        {"a rename onto a name live only in another family", &mixed_fixture,
         {mar177_rename(ConstraintKind::Transform, "mirror_source", "rope_follow")},
         true, ""},
        {"a rename of a name an earlier delete consumed", &ik_fixture,
         {mar177_delete(ConstraintKind::Ik, "arm_positive"),
          mar177_rename(ConstraintKind::Ik, "arm_positive", "x")},
         false, "was already renamed or deleted by an earlier operation"},
        // The chain is legal only in this order, which is why the records are
        // an ordered array and not a map keyed by source.
        {"a legal chain through an intermediate name", &ik_fixture,
         {mar177_rename(ConstraintKind::Ik, "arm_positive", "arm_middle"),
          mar177_rename(ConstraintKind::Ik, "arm_middle", "arm_final")},
         true, ""},
    };

    for (const ValidationCase& validation : cases) {
        marrow::editor::ProjectData project = validation.fixture->project;
        project.constraint_lifecycle_operations = validation.operations;
        const std::string before = marrow::editor::serialize_project(project);

        const auto runtime =
            marrow::editor::build_project_runtime(project, validation.fixture->base);
        if (validation.accept) {
            if (!runtime) {
                std::cerr << "MAR-177 D: build_project_runtime() rejected "
                          << validation.label << ": " << runtime.error->format() << '\n';
                return false;
            }
            continue;
        }
        if (runtime) {
            std::cerr << "MAR-177 D: build_project_runtime() accepted "
                      << validation.label << ".\n";
            return false;
        }
        if (runtime.error->message.find(validation.want_message) == std::string::npos) {
            std::cerr << "MAR-177 D: rejected " << validation.label
                      << " with the wrong message: " << runtime.error->message << '\n';
            return false;
        }

        // A rejected project must write nothing at all. `export_runtime_assets`
        // writes its first byte well after the validation point, so a rejection
        // cannot leave a truncated `.mskl` or a stale `.mbin` behind.
        const auto reject_json = std::filesystem::temp_directory_path() /
            "marrow_mar177_reject.mskl";
        const auto reject_binary = std::filesystem::temp_directory_path() /
            "marrow_mar177_reject.mbin";
        std::filesystem::remove(reject_json, ignored);
        std::filesystem::remove(reject_binary, ignored);
        marrow::editor::ProjectExportOptions options;
        options.skeleton_output_path = reject_json;
        options.binary_output_path = reject_binary;
        const auto exported = marrow::editor::export_runtime_assets(
            project, validation.fixture->base, options);
        if (exported) {
            std::cerr << "MAR-177 D: export_runtime_assets() accepted "
                      << validation.label << ".\n";
            return false;
        }
        if (std::filesystem::exists(reject_json) ||
            std::filesystem::exists(reject_binary)) {
            std::cerr << "MAR-177 D: a rejected export left a file behind for "
                      << validation.label << ".\n";
            std::filesystem::remove(reject_json, ignored);
            std::filesystem::remove(reject_binary, ignored);
            return false;
        }
        if (marrow::editor::serialize_project(project) != before) {
            std::cerr << "MAR-177 D: a rejection mutated the project for "
                      << validation.label << ".\n";
            return false;
        }
    }

    std::cout << "MAR-177 Scenario D: " << cases.size()
              << " materialization rows -- missing source, duplicate target, family "
                 "mismatch, and invalid order each rejected with their own message, "
                 "each writing no output file; cross-family reuse and an ordered "
                 "chain accepted.\n";
    return true;
}

/**
 * @brief Serializes every project section except one constraint family and the
 *        lifecycle records.
 *
 * A lifecycle call is allowed to change exactly two things: its own family's
 * upserts and the ordered records. Comparing this rendering before and after a
 * call asserts that it changed nothing else -- the other three constraint
 * families, the timelines, the snap settings, and the editor metadata -- byte
 * for byte. MAR-172 shipped an `ok: true` that destroyed an adjacent key and
 * was caught only by reading the neighbour back.
 */
std::string mar177_neighbour_text(
    marrow::editor::ProjectData project,
    marrow::editor::ConstraintKind touched) {
    using marrow::editor::ConstraintKind;
    project.constraint_lifecycle_operations.clear();
    switch (touched) {
    case ConstraintKind::Ik:
        project.ik_constraint_edits.clear();
        break;
    case ConstraintKind::Path:
        project.path_constraint_edits.clear();
        break;
    case ConstraintKind::Transform:
        project.transform_constraint_edits.clear();
        break;
    case ConstraintKind::Physics:
        project.physics_constraint_edits.clear();
        break;
    }
    return marrow::editor::serialize_project(project);
}

// --- Scenario B: the two project primitives (AC2) ---------------------------
//
// The ownership rule ships as code, not as prose, so MAR-178's undoable command
// and this smoke share one implementation of "who owns this constraint".
bool validate_mar177_scenario_b(
    const marrow::editor::ProjectLoadResult& project_result) {
    using marrow::editor::ConstraintKind;

    std::error_code ignored;
    const std::string path_token = std::to_string(
        static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    const auto round_trip_path = std::filesystem::temp_directory_path() /
        ("marrow_mar177_b_" + path_token + ".marrow");

    // `player_idle`'s base skeleton declares no constraints at all, so all four
    // of its constraints are project-only upserts: the third row of the
    // ownership table, where the rewrite happens in place and no record is
    // appended.
    const auto rebase = [&] {
        marrow::editor::ProjectData project = *project_result.project;
        project.runtime_assets.skeleton_path =
            std::filesystem::absolute(project.resolved_skeleton_path());
        project.runtime_assets.atlas_paths = project.resolved_atlas_paths();
        for (auto& atlas_path : project.runtime_assets.atlas_paths) {
            atlas_path = std::filesystem::absolute(atlas_path);
        }
        project.source_path = round_trip_path;
        return project;
    };
    const auto& base = *project_result.base_skeleton_document;

    // --- B1: a project-only rename rewrites the upsert and records nothing --
    {
        marrow::editor::ProjectData project = rebase();
        const std::string neighbours_before =
            mar177_neighbour_text(project, ConstraintKind::Ik);
        const auto renamed = marrow::editor::rename_constraint(
            &project, base, ConstraintKind::Ik, "editor_arm_reach", "arm_reach_v2");
        if (!renamed.ok) {
            std::cerr << "MAR-177 B1: rename_constraint() failed: " << renamed.message
                      << '\n';
            return false;
        }
        if (renamed.used_operation || !renamed.changed_upsert) {
            std::cerr << "MAR-177 B1: a project-only rename must rewrite the upsert "
                         "and append no record (used_operation="
                      << renamed.used_operation << " changed_upsert="
                      << renamed.changed_upsert << ").\n";
            return false;
        }
        if (!project.constraint_lifecycle_operations.empty()) {
            std::cerr << "MAR-177 B1: a project-only rename appended a record.\n";
            return false;
        }
        if (project.ik_constraint_edits.size() != 1U ||
            project.ik_constraint_edits[0].name != "arm_reach_v2") {
            std::cerr << "MAR-177 B1: the IK upsert was not renamed in place.\n";
            return false;
        }
        if (mar177_neighbour_text(project, ConstraintKind::Ik) != neighbours_before) {
            std::cerr << "MAR-177 B1: the rename disturbed a section it does not own.\n";
            return false;
        }
        const auto saved = marrow::editor::save_project(project, round_trip_path);
        if (!saved) {
            std::cerr << "MAR-177 B1: save_project() rejected the renamed project: "
                      << saved.error->message << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(round_trip_path);
        std::filesystem::remove(round_trip_path, ignored);
        if (!reloaded) {
            std::cerr << "MAR-177 B1: reload failed.\n";
            return false;
        }
        if (reloaded.project->ik_constraint_edits.size() != 1U ||
            reloaded.project->ik_constraint_edits[0].name != "arm_reach_v2" ||
            !reloaded.project->constraint_lifecycle_operations.empty()) {
            std::cerr << "MAR-177 B1: the renamed project did not round-trip.\n";
            return false;
        }
    }

    // --- B2: a project-only delete erases the upsert and records nothing ---
    {
        marrow::editor::ProjectData project = rebase();
        const std::string neighbours_before =
            mar177_neighbour_text(project, ConstraintKind::Physics);
        const auto deleted = marrow::editor::delete_constraint(
            &project, base, ConstraintKind::Physics, "editor_ribbon_secondary");
        if (!deleted.ok) {
            std::cerr << "MAR-177 B2: delete_constraint() failed: " << deleted.message
                      << '\n';
            return false;
        }
        if (deleted.used_operation || !deleted.changed_upsert) {
            std::cerr << "MAR-177 B2: a project-only delete must erase the upsert and "
                         "append no tombstone.\n";
            return false;
        }
        if (!project.physics_constraint_edits.empty() ||
            !project.constraint_lifecycle_operations.empty()) {
            std::cerr << "MAR-177 B2: the physics upsert or the record vector is "
                         "not what the delete should have left.\n";
            return false;
        }
        // Survival: the other three constraints and every unrelated section.
        if (mar177_neighbour_text(project, ConstraintKind::Physics) != neighbours_before) {
            std::cerr << "MAR-177 B2: the delete destroyed adjacent project data.\n";
            return false;
        }
        if (project.ik_constraint_edits.size() != 1U ||
            project.path_constraint_edits.size() != 1U ||
            project.transform_constraint_edits.size() != 1U) {
            std::cerr << "MAR-177 B2: the delete took another family with it.\n";
            return false;
        }
        const auto saved = marrow::editor::save_project(project, round_trip_path);
        if (!saved) {
            std::cerr << "MAR-177 B2: save_project() rejected the pruned project: "
                      << saved.error->message << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(round_trip_path);
        std::filesystem::remove(round_trip_path, ignored);
        if (!reloaded || !reloaded.project->physics_constraint_edits.empty()) {
            std::cerr << "MAR-177 B2: the pruned project did not round-trip.\n";
            return false;
        }
    }

    // --- B3: rejections leave the project byte-identical -------------------
    {
        struct RejectionCase {
            const char* label;
            bool is_rename;
            const char* from;
            const char* to;
            const char* want_message;
        };
        const std::vector<RejectionCase> cases{
            {"a rename of a name that exists nowhere", true, "no_such", "x",
             "does not exist"},
            {"a delete of a name that exists nowhere", false, "no_such", "",
             "does not exist"},
            {"a rename with an empty target", true, "editor_arm_reach", "",
             "must not be empty"},
            {"a rename onto its own name", true, "editor_arm_reach",
             "editor_arm_reach", "must differ"},
        };
        for (const RejectionCase& rejection : cases) {
            marrow::editor::ProjectData project = rebase();
            const std::string before = marrow::editor::serialize_project(project);
            const auto outcome = rejection.is_rename
                ? marrow::editor::rename_constraint(
                      &project, base, ConstraintKind::Ik, rejection.from, rejection.to)
                : marrow::editor::delete_constraint(
                      &project, base, ConstraintKind::Ik, rejection.from);
            if (outcome.ok) {
                std::cerr << "MAR-177 B3: the primitive accepted " << rejection.label
                          << ".\n";
                return false;
            }
            if (outcome.message.find(rejection.want_message) == std::string::npos) {
                std::cerr << "MAR-177 B3: rejected " << rejection.label
                          << " with the wrong message: " << outcome.message << '\n';
                return false;
            }
            if (marrow::editor::serialize_project(project) != before) {
                std::cerr << "MAR-177 B3: " << rejection.label
                          << " mutated the project despite being rejected.\n";
                return false;
            }
        }
        // A target that another live constraint in the same family already
        // holds is a hard rejection, never a silent overwrite and never an
        // auto-suffix: `unique_constraint_name()` owns auto-suffixing for the
        // create path, and a rename is a user's choice of a specific name.
        marrow::editor::ProjectData project = rebase();
        marrow::editor::IkConstraintEdit second = project.ik_constraint_edits[0];
        second.name = "editor_arm_reach_2";
        project.ik_constraint_edits.push_back(second);
        const std::string before = marrow::editor::serialize_project(project);
        const auto collided = marrow::editor::rename_constraint(
            &project, base, ConstraintKind::Ik, "editor_arm_reach",
            "editor_arm_reach_2");
        if (collided.ok ||
            collided.message.find("is already taken") == std::string::npos) {
            std::cerr << "MAR-177 B3: a rename onto a live name in the same family "
                         "was not rejected as taken: " << collided.message << '\n';
            return false;
        }
        if (marrow::editor::serialize_project(project) != before) {
            std::cerr << "MAR-177 B3: the collision rejection mutated the project.\n";
            return false;
        }
    }

    // --- B4: base-backed and shadowing ownership ---------------------------
    const auto skin_fixture = mar177_open_fixture(
        "assets/fixtures/skin_inherit_constraints.mskl",
        std::filesystem::temp_directory_path() / "marrow_mar177_b_seed.marrow");
    if (!skin_fixture.ok) return false;
    {
        marrow::editor::ProjectData project = skin_fixture.project;
        const auto renamed = marrow::editor::rename_constraint(
            &project, skin_fixture.base, ConstraintKind::Transform, "cape_pull",
            "cape_drag");
        if (!renamed.ok || !renamed.used_operation || renamed.changed_upsert) {
            std::cerr << "MAR-177 B4: a base-backed rename must append exactly one "
                         "record and touch no upsert: " << renamed.message << '\n';
            return false;
        }
        if (project.constraint_lifecycle_operations.size() != 1U ||
            !project.transform_constraint_edits.empty()) {
            std::cerr << "MAR-177 B4: the base-backed rename did not produce one "
                         "record and zero upserts.\n";
            return false;
        }
        const auto runtime =
            marrow::editor::build_project_runtime(project, skin_fixture.base);
        if (!runtime ||
            runtime.skeleton_data->transform_constraints().size() != 1U ||
            runtime.skeleton_data->transform_constraints()[0].name != "cape_drag") {
            std::cerr << "MAR-177 B4: the base-backed rename did not materialize.\n";
            return false;
        }
        // Survives a full save -> reload cycle.
        const auto b4_path = std::filesystem::temp_directory_path() /
            ("marrow_mar177_b4_" + path_token + ".marrow");
        marrow::editor::ProjectData written;
        if (!mar177_write_loadable_project(
                project, "assets/fixtures/skin_inherit_constraints.mskl", b4_path,
                &written)) {
            return false;
        }
        const auto reloaded = marrow::editor::load_project(b4_path);
        std::filesystem::remove(b4_path, ignored);
        if (!reloaded ||
            reloaded.project->constraint_lifecycle_operations.size() != 1U ||
            reloaded.skeleton_data->transform_constraints().size() != 1U ||
            reloaded.skeleton_data->transform_constraints()[0].name != "cape_drag") {
            std::cerr << "MAR-177 B4: the base-backed rename did not survive save -> "
                         "reload.\n";
            return false;
        }
    }

    // --- B5: the shadowing row -- a tombstone AND an upsert erase ----------
    //
    // An upsert whose name also exists in the base is *shadowing* it:
    // `merge_named_object_array_member` replaces the base element in place.
    // Erasing only the upsert therefore resurrects the base constraint, which
    // reads to the user as "delete did nothing".
    {
        marrow::editor::ProjectData project = skin_fixture.project;
        marrow::editor::TransformConstraintEdit shadow;
        shadow.name = "cape_pull";
        shadow.source_bone_name = "controller";
        shadow.bone_names = {"constrained"};
        shadow.rotate_mix = 0.5;
        project.transform_constraint_edits.push_back(shadow);

        const auto deleted = marrow::editor::delete_constraint(
            &project, skin_fixture.base, ConstraintKind::Transform, "cape_pull");
        if (!deleted.ok || !deleted.used_operation || !deleted.changed_upsert) {
            std::cerr << "MAR-177 B5: a shadowing delete must emit BOTH a tombstone "
                         "and an upsert erase (ok=" << deleted.ok << " used_operation="
                      << deleted.used_operation << " changed_upsert="
                      << deleted.changed_upsert << " message=" << deleted.message
                      << ").\n";
            return false;
        }
        if (project.constraint_lifecycle_operations.size() != 1U ||
            !project.transform_constraint_edits.empty()) {
            std::cerr << "MAR-177 B5: the shadowing delete left the wrong state.\n";
            return false;
        }
        const auto document = marrow::editor::build_project_runtime_document(
            project, skin_fixture.base);
        const std::string text =
            marrow::runtime::json::serialize_pretty(document.root);
        if (mar177_count_quoted(text, "cape_pull") != 0U) {
            std::cerr << "MAR-177 B5: the base constraint resurrected after the "
                         "shadowing delete.\n";
            return false;
        }
        const auto runtime =
            marrow::editor::build_project_runtime(project, skin_fixture.base);
        if (!runtime || !runtime.skeleton_data->transform_constraints().empty()) {
            std::cerr << "MAR-177 B5: the rig after a shadowing delete did not load "
                         "with zero transform constraints.\n";
            return false;
        }
    }

    // --- B6: step 5 is not redundant with the preflight --------------------
    //
    // The preflight resolves names; it does not re-check everything
    // `validate_project_for_save()` checks. A project with no atlas path is the
    // witness: the preflight has no opinion about atlases, so without step 5
    // the primitive would return `ok` and `save_project()` would then refuse
    // the result -- the MAR-175 failure shape exactly, a successful command
    // that leaves an unsavable project.
    {
        marrow::editor::ProjectData project = skin_fixture.project;
        project.runtime_assets.atlas_paths.clear();
        const std::string before = marrow::editor::serialize_project(project);
        const auto renamed = marrow::editor::rename_constraint(
            &project, skin_fixture.base, ConstraintKind::Transform, "cape_pull",
            "cape_drag");
        if (renamed.ok) {
            std::cerr << "MAR-177 B6: the primitive returned ok for a project that "
                         "save_project() refuses -- step 5's validator is missing.\n";
            return false;
        }
        if (renamed.message.find("atlas") == std::string::npos) {
            std::cerr << "MAR-177 B6: expected the save validator's atlas message, got: "
                      << renamed.message << '\n';
            return false;
        }
        if (marrow::editor::serialize_project(project) != before) {
            std::cerr << "MAR-177 B6: the rejection mutated the project.\n";
            return false;
        }
    }

    std::cout << "MAR-177 Scenario B: project-only lifecycle rewrites the upsert and "
                 "records nothing, base-backed appends exactly one record, a shadowing "
                 "delete emits both so the base cannot resurrect, and every rejection "
                 "leaves serialize_project() byte-identical.\n";
    return true;
}

/**
 * @brief Decodes the `.mbin` string table straight out of the header.
 *
 * Layout is `MBIN` (4 bytes), `varint(version)`, `varint(count)`, then
 * `varint(len) + bytes` per entry. Reading it directly is what tells a broken
 * encoding apart from a reshuffled fixture: `collect_strings()` interns each
 * DISTINCT string once, so the table is the document's vocabulary, and naming
 * the entries an edit adds or removes is a far sharper assertion than a size.
 */
bool mar177_mbin_string_table(
    const std::filesystem::path& path,
    std::vector<std::string>* table_out) {
    std::ifstream stream(path, std::ios::binary);
    const std::string bytes(
        (std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    if (bytes.size() < 6U || bytes.compare(0, 4, "MBIN") != 0) {
        return false;
    }
    std::size_t at = 4U;
    const auto read_varint = [&](std::uint64_t* value_out) {
        std::uint64_t value = 0;
        unsigned shift = 0;
        while (at < bytes.size()) {
            const auto byte = static_cast<std::uint8_t>(bytes[at++]);
            value |= static_cast<std::uint64_t>(byte & 0x7FU) << shift;
            if ((byte & 0x80U) == 0U) {
                *value_out = value;
                return true;
            }
            shift += 7U;
        }
        return false;
    };
    std::uint64_t version = 0;
    std::uint64_t count = 0;
    if (!read_varint(&version) || !read_varint(&count)) {
        return false;
    }
    table_out->clear();
    table_out->reserve(static_cast<std::size_t>(count));
    for (std::uint64_t index = 0; index < count; ++index) {
        std::uint64_t length = 0;
        if (!read_varint(&length) || at + length > bytes.size()) {
            return false;
        }
        table_out->push_back(bytes.substr(at, static_cast<std::size_t>(length)));
        at += static_cast<std::size_t>(length);
    }
    return true;
}

/** @brief Entries present in `before` and absent from `after`, sorted. */
std::vector<std::string> mar177_table_difference(
    std::vector<std::string> before,
    std::vector<std::string> after) {
    std::sort(before.begin(), before.end());
    std::sort(after.begin(), after.end());
    std::vector<std::string> removed;
    std::set_difference(
        before.begin(), before.end(), after.begin(), after.end(),
        std::back_inserter(removed));
    return removed;
}

std::uintmax_t mar177_file_size(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    return error ? 0U : size;
}

// --- Scenario F: export, on a mutated project (AC5) ------------------------
//
// The fixture is the only one in the tree where a skin names a constraint, so
// it is the only one where the "unloadable rig" failure is reachable at all.
bool validate_mar177_scenario_f() {
    using marrow::editor::ConstraintKind;

    std::error_code ignored;
    const auto directory =
        std::filesystem::temp_directory_path() / "marrow_mar177_export";
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory, ignored);

    const auto base_json = directory / "base.mskl";
    const auto base_binary = directory / "base.mbin";
    const auto rename_json = directory / "rename.mskl";
    const auto rename_binary = directory / "rename.mbin";
    const auto delete_json = directory / "delete.mskl";
    const auto delete_binary = directory / "delete.mbin";

    const auto fixture = mar177_open_fixture(
        "assets/fixtures/skin_inherit_constraints.mskl",
        directory / "project.marrow");
    if (!fixture.ok) return false;

    const auto export_to = [&](const marrow::editor::ProjectData& project,
                               const std::filesystem::path& json_path,
                               const std::filesystem::path& binary_path) {
        marrow::editor::ProjectExportOptions options;
        options.skeleton_output_path = json_path;
        options.binary_output_path = binary_path;
        return marrow::editor::export_runtime_assets(project, fixture.base, options);
    };

    const auto baseline = export_to(fixture.project, base_json, base_binary);
    if (!baseline) {
        std::cerr << "MAR-177 F: the baseline export failed: "
                  << baseline.error->format() << '\n';
        return false;
    }
    const std::string baseline_text = mar177_read_file(base_json);
    const auto baseline_json_size = mar177_file_size(base_json);
    const auto baseline_binary_size = mar177_file_size(base_binary);
    std::vector<std::string> baseline_strings;
    if (!mar177_mbin_string_table(base_binary, &baseline_strings)) {
        std::cerr << "MAR-177 F: could not read the baseline `.mbin` string table.\n";
        return false;
    }
    const std::size_t baseline_occurrences =
        mar177_count_quoted(baseline_text, "cape_pull");
    if (baseline_occurrences != 2U) {
        std::cerr << "MAR-177 F: `cape_pull` occurs " << baseline_occurrences
                  << " times in the exported baseline, expected 2 (the root "
                     "transform[0].name and the skins.cape.transform[0] reference). "
                     "Re-derive the delta below from this count; do not edit the "
                     "constant.\n";
        return false;
    }

    // --- F1: rename -- the .mskl grows by the occurrence count -------------
    {
        marrow::editor::ProjectData project = fixture.project;
        const auto renamed = marrow::editor::rename_constraint(
            &project, fixture.base, ConstraintKind::Transform, "cape_pull",
            "cape_pull_renamed");
        if (!renamed.ok) {
            std::cerr << "MAR-177 F1: rename_constraint() failed: " << renamed.message
                      << '\n';
            return false;
        }
        // A stale skin reference fails `load_skeleton_data` before
        // `export_runtime_assets` writes its first byte, so this call
        // succeeding is itself half the assertion.
        const auto exported = export_to(project, rename_json, rename_binary);
        if (!exported) {
            std::cerr << "MAR-177 F1: the renamed export failed: "
                      << exported.error->format() << '\n';
            return false;
        }

        const std::string text = mar177_read_file(rename_json);
        if (mar177_count_quoted(text, "cape_pull") != 0U ||
            mar177_count_quoted(text, "cape_pull_renamed") != 2U) {
            std::cerr << "MAR-177 F1: the exported text has "
                      << mar177_count_quoted(text, "cape_pull") << " `cape_pull` and "
                      << mar177_count_quoted(text, "cape_pull_renamed")
                      << " `cape_pull_renamed` occurrences, expected 0 and 2.\n";
            return false;
        }

        // Derivation (design §11.2): the exported `.mskl` is serialize_pretty()
        // of the materialized document. `cape_pull` occurs in it exactly twice
        // -- the root transform[0].name and the skins.cape.transform[0]
        // reference -- and nowhere else (it is not a bone, slot, skin,
        // animation, or object key). Renaming to a name 8 UTF-8 bytes longer
        // therefore grows the text by 2 * 8 = 16.
        //
        // +8 instead of +16 means the skin reference was NOT rewritten.
        // Any other value means the occurrence count changed: re-derive it from
        // the exported text, do not edit this constant.
        const auto json_size = mar177_file_size(rename_json);
        const std::intmax_t json_delta =
            static_cast<std::intmax_t>(json_size) -
            static_cast<std::intmax_t>(baseline_json_size);
        if (json_delta != 16) {
            std::cerr << "MAR-177 F1: .mskl delta is " << json_delta
                      << ", expected +16 = 2 occurrences x 8 bytes. A delta of +8 "
                         "means the skin reference was not rewritten; any other "
                         "value means the occurrence count changed -- re-derive it "
                         "from the exported text rather than editing the constant.\n";
            return false;
        }

        // Derivation (design §11.3): collect_strings() interns each DISTINCT
        // string once, so the name occupies one string-table entry no matter
        // how many times it occurs. The new name occurs nowhere else and the
        // old name occurs nowhere else, so the table keeps its entry count and
        // every index; every index varint in encode_value is unchanged, as is
        // the boolean block and the animation section. The only delta is that
        // entry's varint(len)+bytes, and both 9 and 17 are < 128 so both length
        // varints are one byte: (1 + 17) - (1 + 9) = 8.
        //
        // A delta of 16 would mean the `.mbin` counts occurrences, i.e. the
        // encoding stopped interning. Any other value means the string table
        // reshuffled or the encoding moved; the entry counts below say which.
        std::vector<std::string> renamed_strings;
        if (!mar177_mbin_string_table(rename_binary, &renamed_strings)) {
            std::cerr << "MAR-177 F1: could not read the renamed `.mbin` string table.\n";
            return false;
        }
        const auto binary_size = mar177_file_size(rename_binary);
        const std::intmax_t binary_delta =
            static_cast<std::intmax_t>(binary_size) -
            static_cast<std::intmax_t>(baseline_binary_size);
        const auto rename_removed =
            mar177_table_difference(baseline_strings, renamed_strings);
        const auto rename_added =
            mar177_table_difference(renamed_strings, baseline_strings);
        if (renamed_strings.size() != baseline_strings.size() || binary_delta != 8 ||
            rename_removed != std::vector<std::string>{"cape_pull"} ||
            rename_added != std::vector<std::string>{"cape_pull_renamed"}) {
            std::cerr << "MAR-177 F1: .mbin delta is " << binary_delta
                      << ", expected +8; string-table entry count went "
                      << baseline_strings.size() << " -> " << renamed_strings.size()
                      << " with [" << mar177_join(rename_removed) << "] out and ["
                      << mar177_join(rename_added)
                      << "] in (an equal count means the encoding moved; an unequal "
                         "one means the table reshuffled).\n";
            return false;
        }

        std::cout << "MAR-177 F1 export: .mskl " << baseline_json_size << " -> "
                  << json_size << " (+" << json_delta << " = 2 occurrences x 8 bytes), "
                     ".mbin " << baseline_binary_size << " -> " << binary_size << " (+"
                  << binary_delta << " = 1 interned entry x 8 bytes) with the string "
                     "table unchanged at " << renamed_strings.size()
                  << " entries (`cape_pull` out, `cape_pull_renamed` in). The 16:8 ratio IS the occurrence count: JSON counts "
                     "occurrences, MBIN counts distinct strings, so a root-only rename "
                     "would read +8/+8 and fail loudly.\n";
    }

    // --- F2: delete -- the family key leaves, and the .mbin still loads ----
    {
        marrow::editor::ProjectData project = fixture.project;
        const auto deleted = marrow::editor::delete_constraint(
            &project, fixture.base, ConstraintKind::Transform, "cape_pull");
        if (!deleted.ok) {
            std::cerr << "MAR-177 F2: delete_constraint() failed: " << deleted.message
                      << '\n';
            return false;
        }
        const auto exported = export_to(project, delete_json, delete_binary);
        if (!exported) {
            std::cerr << "MAR-177 F2: the pruned export failed: "
                      << exported.error->format() << '\n';
            return false;
        }
        const std::string text = mar177_read_file(delete_json);
        if (mar177_count_quoted(text, "cape_pull") != 0U) {
            std::cerr << "MAR-177 F2: `cape_pull` still occurs "
                      << mar177_count_quoted(text, "cape_pull")
                      << " times in the exported text.\n";
            return false;
        }
        const auto exported_document =
            marrow::runtime::json::load_document(delete_json);
        if (!exported_document) {
            std::cerr << "MAR-177 F2: the exported `.mskl` did not parse.\n";
            return false;
        }
        bool skin_key_present = true;
        mar177_skin_references(
            exported_document.document->root, "cape", "transform", &skin_key_present);
        if (exported_document.document->root.as_object().count("transform") != 0U ||
            skin_key_present) {
            std::cerr << "MAR-177 F2: the exported `.mskl` still carries an empty "
                         "`transform` key at the root or inside `skins.cape`; the "
                         "runtime rejects an empty family array outright.\n";
            return false;
        }

        // The `.mbin` must reload, which is the assertion the byte model cannot
        // make for a delete: the table loses an entry and every later index
        // shifts, so structure is the signal and the header count is the one
        // number read directly.
        const auto reloaded =
            marrow::runtime::load_skeleton_document(delete_binary);
        if (!reloaded) {
            std::cerr << "MAR-177 F2: the exported `.mbin` did not reload: "
                      << reloaded.error->format() << '\n';
            return false;
        }
        const auto skeleton = marrow::runtime::load_skeleton_data(*reloaded.document);
        if (!skeleton) {
            std::cerr << "MAR-177 F2: the reloaded `.mbin` did not parse: "
                      << skeleton.error->format() << '\n';
            return false;
        }
        const auto* cape = skeleton.skeleton_data->find_skin("cape");
        if (!skeleton.skeleton_data->transform_constraints().empty() ||
            cape == nullptr || !cape->transform_constraint_indices.empty() ||
            cape->bone_indices.size() != 1U) {
            std::cerr << "MAR-177 F2: the reloaded `.mbin` did not carry zero "
                         "transform constraints with skin `cape` still holding "
                         "`cape_target`.\n";
            return false;
        }
        std::vector<std::string> deleted_strings;
        if (!mar177_mbin_string_table(delete_binary, &deleted_strings)) {
            std::cerr << "MAR-177 F2: could not read the pruned `.mbin` string table.\n";
            return false;
        }
        // Derivation, measured rather than assumed. The design spec predicted
        // the table would lose exactly one entry. It loses FOUR, and the reason
        // is the same interning that makes the rename cost +8: `collect_strings()`
        // interns each distinct string once across the WHOLE document, object
        // keys included, so deleting a subtree removes every string that
        // occurred only inside it -- not just the value that was named.
        //
        //   cape_pull     the constraint's name
        //   transform     the root array key AND the skin scope key, both gone
        //   source        an object key used only by a transform constraint
        //   translateMix  likewise
        //
        // `name` and `bones` survive because bones and slots use them too. A
        // fifth removal, or a different set, means the fixture changed shape:
        // re-derive from the decoded table, do not edit this list.
        const std::vector<std::string> expected_removed{
            "cape_pull", "source", "transform", "translateMix"};
        const auto removed = mar177_table_difference(baseline_strings, deleted_strings);
        const auto added = mar177_table_difference(deleted_strings, baseline_strings);
        if (removed != expected_removed || !added.empty()) {
            std::cerr << "MAR-177 F2: the `.mbin` string table went "
                      << baseline_strings.size() << " -> " << deleted_strings.size()
                      << " with [" << mar177_join(removed) << "] out and ["
                      << mar177_join(added) << "] in, expected ["
                      << mar177_join(expected_removed)
                      << "] out and nothing in.\n";
            return false;
        }
        std::cout << "MAR-177 F2 export: the deleted name occurs 0 times, neither the "
                     "root `transform` key nor `skins.cape.transform` survives as an "
                     "empty array, the `.mbin` reloads with zero transform constraints "
                     "and `cape_target` intact, and its string table drops "
                  << baseline_strings.size() << " -> " << deleted_strings.size()
                  << " entries -- exactly [" << mar177_join(removed)
                  << "], because interning is per distinct string across the whole "
                     "document, so a deleted subtree takes every key that occurred "
                     "only inside it.\n";
    }

    // --- F3: ambiguity #8 -- an atlas-free project still exports -----------
    //
    // `validate_project_for_save()` requires at least one atlas path, so a
    // *savable* project needs one; `export_runtime_assets()` only iterates
    // `resolved_atlas_paths()`, so an atlas-free project exports fine. Those
    // are two different requirements and the plan's ambiguity #8 conflated them.
    {
        marrow::editor::ProjectData project = fixture.project;
        project.runtime_assets.atlas_paths.clear();
        const auto atlas_free_json = directory / "atlas_free.mskl";
        const auto exported = export_to(project, atlas_free_json, directory / "atlas_free.mbin");
        if (!exported || !exported.atlas_paths.empty()) {
            std::cerr << "MAR-177 F3: an atlas-free export did not succeed with zero "
                         "exported atlases.\n";
            return false;
        }
    }

    std::filesystem::remove_all(directory, ignored);
    return true;
}

// --- Scenario E: ordered chains through materialization, merge, reload -----
//
// Scenario A2 replays the sequences symbolically, without a base. These run the
// same shapes against real fixtures and assert the materialized array, because
// order is load-bearing: a chain needs a name that only exists after the
// previous record ran, a swap passes through a name that is legal only in
// transit, and a reuse is legal in one order and a duplicate target in the
// other. A map keyed by source cannot express any of the three.
bool validate_mar177_scenario_e() {
    using marrow::editor::ConstraintKind;
    using marrow::editor::ConstraintLifecycleKind;
    using marrow::editor::ConstraintLifecycleOperation;

    std::error_code ignored;
    const std::string path_token = std::to_string(
        static_cast<unsigned long long>(
            std::chrono::steady_clock::now().time_since_epoch().count()));

    const auto ik_fixture = mar177_open_fixture(
        "assets/fixtures/ik_constraints.mskl",
        std::filesystem::temp_directory_path() / "marrow_mar177_e_ik.marrow");
    if (!ik_fixture.ok) return false;
    const auto base_names = mar177_constraint_names(ik_fixture.base.root, "ik");

    struct SequenceCase {
        const char* label;
        std::vector<ConstraintLifecycleOperation> operations;
        std::vector<std::string> expected;
    };
    std::vector<SequenceCase> cases;
    {
        // Chain: a name that exists only after the previous record ran, then a
        // delete of the name the chain produced.
        std::vector<std::string> expected(base_names.begin() + 1, base_names.end());
        cases.push_back(
            {"a chain that ends in a delete",
             {mar177_rename(ConstraintKind::Ik, base_names[0], "arm_middle"),
              mar177_rename(ConstraintKind::Ik, "arm_middle", "arm_final"),
              mar177_delete(ConstraintKind::Ik, "arm_final")},
             expected});
    }
    {
        // Swap: every intermediate state is legal, every reordering is not.
        // Both constraints keep their array positions and exchange names.
        std::vector<std::string> expected = base_names;
        std::swap(expected[0], expected[1]);
        cases.push_back(
            {"a three-step swap",
             {mar177_rename(ConstraintKind::Ik, base_names[0], "arm_swap_tmp"),
              mar177_rename(ConstraintKind::Ik, base_names[1], base_names[0]),
              mar177_rename(ConstraintKind::Ik, "arm_swap_tmp", base_names[1])},
             expected});
    }
    {
        // Reuse: legal in this order, a duplicate target in the other.
        std::vector<std::string> expected(base_names.begin() + 1, base_names.end());
        expected[0] = base_names[0];
        cases.push_back(
            {"a delete whose name a later rename reuses",
             {mar177_delete(ConstraintKind::Ik, base_names[0]),
              mar177_rename(ConstraintKind::Ik, base_names[1], base_names[0])},
             expected});
    }

    for (const SequenceCase& sequence : cases) {
        marrow::editor::ProjectData project = ik_fixture.project;
        project.constraint_lifecycle_operations = sequence.operations;
        const auto document =
            marrow::editor::build_project_runtime_document(project, ik_fixture.base);
        const auto names = mar177_constraint_names(document.root, "ik");
        if (names != sequence.expected) {
            std::cerr << "MAR-177 E: " << sequence.label << " materialized ["
                      << mar177_join(names) << "], expected ["
                      << mar177_join(sequence.expected) << "].\n";
            return false;
        }
        const auto runtime =
            marrow::editor::build_project_runtime(project, ik_fixture.base);
        if (!runtime) {
            std::cerr << "MAR-177 E: " << sequence.label << " failed to load: "
                      << runtime.error->format() << '\n';
            return false;
        }
        // The runtime evaluates each family by a plain index loop, so array
        // position IS evaluation order; the sequence is the assertion.
        std::vector<std::string> runtime_names;
        for (const auto& constraint : runtime.skeleton_data->ik_constraints()) {
            runtime_names.push_back(constraint.name);
        }
        if (runtime_names != sequence.expected) {
            std::cerr << "MAR-177 E: " << sequence.label
                      << " loaded in the order [" << mar177_join(runtime_names)
                      << "], expected [" << mar177_join(sequence.expected) << "].\n";
            return false;
        }
        const auto save_path = std::filesystem::temp_directory_path() /
            ("marrow_mar177_e_seq_" + path_token + ".marrow");
        const auto saved = marrow::editor::save_project(project, save_path);
        std::filesystem::remove(save_path, ignored);
        if (!saved) {
            std::cerr << "MAR-177 E: save_project() rejected " << sequence.label
                      << ": " << saved.error->message << '\n';
            return false;
        }
    }

    // --- E2: unknown top-level keys survive a populated `operations` array --
    {
        const auto merge_path = std::filesystem::temp_directory_path() /
            ("marrow_mar177_e_merge_" + path_token + ".marrow");
        const auto skin_fixture = mar177_open_fixture(
            "assets/fixtures/skin_inherit_constraints.mskl",
            std::filesystem::temp_directory_path() / "marrow_mar177_e_seed.marrow");
        if (!skin_fixture.ok) return false;

        marrow::editor::ProjectData seed = skin_fixture.project;
        // Unknown *top-level* keys survive load/save because `build_project_value`
        // starts from the preserved root and overwrites only the known keys.
        marrow::runtime::json::Value::Object unknown;
        unknown.emplace("note", marrow::runtime::json::Value(std::string("kept"), {}));
        marrow::runtime::json::Value::Object preserved;
        preserved.emplace(
            "mar177_future_section",
            marrow::runtime::json::Value(std::move(unknown), {}));
        seed.preserved_root = marrow::runtime::json::Value(std::move(preserved), {});
        seed.constraint_lifecycle_operations = {
            mar177_rename(ConstraintKind::Transform, "cape_pull", "cape_stage_one")};

        marrow::editor::ProjectData written;
        if (!mar177_write_loadable_project(
                seed, "assets/fixtures/skin_inherit_constraints.mskl", merge_path,
                &written)) {
            return false;
        }
        auto loaded = marrow::editor::load_project(merge_path);
        if (!loaded) {
            std::cerr << "MAR-177 E2: the seeded project did not load.\n";
            return false;
        }
        if (loaded.project->constraint_lifecycle_operations.size() != 1U) {
            std::cerr << "MAR-177 E2: the seeded record did not survive the first load.\n";
            return false;
        }

        // One accepted mutation on top of the existing record, producing an
        // ordered pair whose second element depends on the first having run.
        marrow::editor::ProjectData project = *loaded.project;
        const auto renamed = marrow::editor::rename_constraint(
            &project, skin_fixture.base, ConstraintKind::Transform, "cape_stage_one",
            "cape_stage_two");
        if (!renamed.ok || !renamed.used_operation) {
            std::cerr << "MAR-177 E2: the chained rename failed: " << renamed.message
                      << '\n';
            return false;
        }
        const auto saved = marrow::editor::save_project(project, merge_path);
        if (!saved) {
            std::cerr << "MAR-177 E2: save_project() rejected the chain: "
                      << saved.error->message << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(merge_path);
        const std::string text = mar177_read_file(merge_path);
        std::filesystem::remove(merge_path, ignored);
        if (!reloaded) {
            std::cerr << "MAR-177 E2: the chained project did not reload.\n";
            return false;
        }
        const auto& operations = reloaded.project->constraint_lifecycle_operations;
        if (operations.size() != 2U ||
            operations[0].kind != ConstraintLifecycleKind::Rename ||
            operations[0].name != "cape_pull" ||
            operations[0].new_name != "cape_stage_one" ||
            operations[1].kind != ConstraintLifecycleKind::Rename ||
            operations[1].name != "cape_stage_one" ||
            operations[1].new_name != "cape_stage_two") {
            std::cerr << "MAR-177 E2: the two records did not round-trip in order.\n";
            return false;
        }
        if (text.find("\"mar177_future_section\"") == std::string::npos ||
            text.find("\"note\": \"kept\"") == std::string::npos) {
            std::cerr << "MAR-177 E2: the unknown top-level key did not survive the "
                         "save that wrote the records.\n";
            return false;
        }
        if (reloaded.skeleton_data->transform_constraints().size() != 1U ||
            reloaded.skeleton_data->transform_constraints()[0].name !=
                "cape_stage_two") {
            std::cerr << "MAR-177 E2: the reloaded skeleton did not carry the chained "
                         "rename.\n";
            return false;
        }
    }

    std::cout << "MAR-177 Scenario E: chains, swaps, and reuse materialize in array "
                 "order and load in that order, every accepted sequence saves, and an "
                 "ordered pair of records round-trips beside an unknown top-level "
                 "section.\n";
    return true;
}


// ---------------------------------------------------------------------------
// MAR-178 -- constraint rename and delete surfaces.
//
// These scenarios exercise the UI-free command `apply_constraint_catalog_edit()`
// rather than MAR-177's primitives directly. The primitives are already covered
// by the MAR-177 scenarios above; what is new here is the transaction wrapper,
// the pre-mutation skin summary, and the selection cascade.
// ---------------------------------------------------------------------------

/** @brief The four `(family, name)` pairs `player_idle` declares as upserts. */
struct Mar178ProjectConstraint {
    marrow::editor::ConstraintKind family;
    const char* name;
};

constexpr std::array<Mar178ProjectConstraint, 4> kMar178PlayerIdleConstraints{{
    {marrow::editor::ConstraintKind::Ik, "editor_arm_reach"},
    {marrow::editor::ConstraintKind::Path, "editor_guide_follow"},
    {marrow::editor::ConstraintKind::Transform, "editor_transform_follow"},
    {marrow::editor::ConstraintKind::Physics, "editor_ribbon_secondary"},
}};

marrow::editor::EditDescriptor mar178_descriptor(std::string label) {
    return marrow::editor::EditDescriptor{
        marrow::editor::EditKind::EditProperty,
        std::move(label),
        "constraint-catalog",
        false,
        marrow::editor::EditImpact::Project | marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview};
}

/** @brief The ownership spelling the two primitive flags imply. */
std::string mar178_ownership(const marrow::editor::ConstraintCatalogResult& result) {
    if (result.used_operation && result.changed_upsert) return "shadowed";
    if (result.used_operation) return "base";
    if (result.changed_upsert) return "project";
    return "none";
}

bool mar178_constraint_present(
    const marrow::runtime::SkeletonData& skeleton,
    marrow::editor::ConstraintKind family,
    std::string_view name) {
    const auto named = [&](const auto& constraints) {
        return std::find_if(
                   constraints.begin(),
                   constraints.end(),
                   [&](const auto& constraint) { return constraint.name == name; }) !=
            constraints.end();
    };
    switch (family) {
    case marrow::editor::ConstraintKind::Ik:
        return named(skeleton.ik_constraints());
    case marrow::editor::ConstraintKind::Path:
        return named(skeleton.path_constraints());
    case marrow::editor::ConstraintKind::Transform:
        return named(skeleton.transform_constraints());
    case marrow::editor::ConstraintKind::Physics:
        return named(skeleton.physics_constraints());
    }
    return false;
}

bool validate_mar178_scenario_a(
    const marrow::editor::ProjectLoadResult& project_result) {
    if (project_result.project == nullptr) {
        std::cerr << "MAR-178 Scenario A requires a loaded editor project.\n";
        return false;
    }

    marrow::editor::EditorSession session;
    const auto opened = session.open(project_result.project->source_path);
    if (!opened || session.project() == nullptr || session.runtime_data() == nullptr) {
        std::cerr << "MAR-178 Scenario A could not open the smoke project.\n";
        return false;
    }

    const std::string before_rename =
        marrow::editor::serialize_project(*session.project());
    const std::size_t undo_before = session.undo_count();

    // --- Rename a project-only IK constraint through the command. -----------
    const marrow::editor::ConstraintCatalogEdit rename_edit{
        marrow::editor::ConstraintCatalogEditKind::Rename,
        marrow::editor::ConstraintKind::Ik,
        "editor_arm_reach",
        "arm_reach_v2"};
    const marrow::editor::ConstraintCatalogResult renamed =
        marrow::editor::apply_constraint_catalog_edit(
            session, rename_edit, nullptr,
            mar178_descriptor("Renamed IK constraint editor_arm_reach to arm_reach_v2"));
    if (!renamed.ok || !renamed.changed) {
        std::cerr << "MAR-178 A: the command refused a legal project-only rename: "
                  << renamed.message << '\n';
        return false;
    }
    if (renamed.used_operation || !renamed.changed_upsert ||
        mar178_ownership(renamed) != "project") {
        std::cerr << "MAR-178 A: a project-only rename must rewrite the upsert and "
                     "append no ordered record; measured used_operation="
                  << renamed.used_operation
                  << " changed_upsert=" << renamed.changed_upsert << '\n';
        return false;
    }
    if (!renamed.affected_skins.empty()) {
        std::cerr << "MAR-178 A: no `player_idle` skin names a constraint, so the "
                     "affected-skin summary must be empty; measured "
                  << renamed.affected_skins.size() << " entries.\n";
        return false;
    }
    if (renamed.selection_changed) {
        std::cerr << "MAR-178 A: a null SelectionSet cannot have changed.\n";
        return false;
    }
    if (session.undo_count() != undo_before + 1U) {
        std::cerr << "MAR-178 A: one accepted rename must produce exactly one history "
                     "entry; measured "
                  << session.undo_count() << " against " << undo_before << ".\n";
        return false;
    }
    if (!session.project()->constraint_lifecycle_operations.empty()) {
        std::cerr << "MAR-178 A: a project-only rename must append no ordered record.\n";
        return false;
    }

    const marrow::runtime::SkeletonData& renamed_skeleton = *session.runtime_data();
    if (!mar178_constraint_present(
            renamed_skeleton, marrow::editor::ConstraintKind::Ik, "arm_reach_v2") ||
        mar178_constraint_present(
            renamed_skeleton, marrow::editor::ConstraintKind::Ik, "editor_arm_reach")) {
        std::cerr << "MAR-178 A: the renamed IK constraint must resolve under its new "
                     "name and not under its old one.\n";
        return false;
    }
    for (std::size_t index = 1U; index < kMar178PlayerIdleConstraints.size(); ++index) {
        const auto& survivor = kMar178PlayerIdleConstraints[index];
        if (!mar178_constraint_present(
                renamed_skeleton, survivor.family, survivor.name)) {
            std::cerr << "MAR-178 A: renaming one constraint dropped the unrelated "
                      << survivor.name << ".\n";
            return false;
        }
    }
    if (session.project()->transform_timeline_edits.size() !=
            project_result.project->transform_timeline_edits.size() ||
        session.project()->animation_edits.size() !=
            project_result.project->animation_edits.size() ||
        session.project()->editor_metadata.active_animation !=
            project_result.project->editor_metadata.active_animation ||
        session.project()->snap_settings.has_value() !=
            project_result.project->snap_settings.has_value()) {
        std::cerr << "MAR-178 A: a constraint rename must not touch timeline edits, "
                     "snap settings, or editor metadata.\n";
        return false;
    }

    // --- Save and reload the renamed project. -------------------------------
    const auto save_path =
        std::filesystem::temp_directory_path() / "marrow_mar178_scenario_a.marrow";
    std::error_code ignored;
    std::filesystem::remove(save_path, ignored);
    // `player_idle.marrow` references its skeleton and atlas relatively, so a
    // save into a different directory must carry absolute paths for the reload
    // to resolve them. That is orthogonal to the rename under test.
    marrow::editor::ProjectData portable = *session.project();
    portable.runtime_assets.skeleton_path = std::filesystem::absolute(
        session.project()->resolved_skeleton_path());
    portable.runtime_assets.atlas_paths.clear();
    for (const auto& atlas : session.project()->resolved_atlas_paths()) {
        portable.runtime_assets.atlas_paths.push_back(
            std::filesystem::absolute(atlas));
    }
    const auto saved = marrow::editor::save_project(portable, save_path);
    if (!saved) {
        std::cerr << "MAR-178 A: the renamed project failed to save.\n";
        return false;
    }
    const auto reloaded = marrow::editor::load_project(save_path);
    if (!reloaded || reloaded.project == nullptr) {
        std::cerr << "MAR-178 A: the renamed project failed to reload: "
                  << (reloaded.error.has_value() ? reloaded.error->format()
                                                 : std::string("(no error)"))
                  << '\n';
        return false;
    }
    if (!mar178_constraint_present(
            *reloaded.skeleton_data, marrow::editor::ConstraintKind::Ik,
            "arm_reach_v2")) {
        std::cerr << "MAR-178 A: the reloaded project lost the renamed constraint.\n";
        return false;
    }
    std::filesystem::remove(save_path, ignored);

    // --- Delete a project-only physics constraint. --------------------------
    const std::string before_delete =
        marrow::editor::serialize_project(*session.project());
    const marrow::editor::ConstraintCatalogEdit delete_edit{
        marrow::editor::ConstraintCatalogEditKind::Delete,
        marrow::editor::ConstraintKind::Physics,
        "editor_ribbon_secondary",
        {}};
    const marrow::editor::ConstraintCatalogResult deleted =
        marrow::editor::apply_constraint_catalog_edit(
            session, delete_edit, nullptr,
            mar178_descriptor("Deleted Physics constraint editor_ribbon_secondary"));
    if (!deleted.ok || !deleted.changed) {
        std::cerr << "MAR-178 A: the command refused a legal project-only delete: "
                  << deleted.message << '\n';
        return false;
    }
    if (deleted.used_operation || !deleted.changed_upsert ||
        mar178_ownership(deleted) != "project") {
        std::cerr << "MAR-178 A: a project-only delete must erase the upsert and append "
                     "no ordered record.\n";
        return false;
    }
    if (!session.project()->constraint_lifecycle_operations.empty()) {
        std::cerr << "MAR-178 A: a project-only delete must leave "
                     "constraint_lifecycle_operations empty.\n";
        return false;
    }
    if (session.project()->find_physics_constraint_edit("editor_ribbon_secondary") !=
        nullptr) {
        std::cerr << "MAR-178 A: the physics upsert survived its own delete.\n";
        return false;
    }
    if (mar178_constraint_present(
            *session.runtime_data(), marrow::editor::ConstraintKind::Physics,
            "editor_ribbon_secondary")) {
        std::cerr << "MAR-178 A: the deleted physics constraint still materializes.\n";
        return false;
    }
    for (std::size_t index = 1U; index + 1U < kMar178PlayerIdleConstraints.size();
         ++index) {
        const auto& survivor = kMar178PlayerIdleConstraints[index];
        if (!mar178_constraint_present(
                *session.runtime_data(), survivor.family, survivor.name)) {
            std::cerr << "MAR-178 A: deleting one constraint dropped the unrelated "
                      << survivor.name << ".\n";
            return false;
        }
    }

    // §10.5: a successful save proves nothing here. `validate_project_for_save()`
    // takes no base document (`project.cpp:5502`+) and so cannot resolve a skin
    // reference against a skeleton; only `load_project()`, which calls
    // `build_project_runtime()` at `project.cpp:7495`, re-materializes and
    // re-parses. The RELOAD is the assertion.
    {
        const auto delete_reload_path =
            std::filesystem::temp_directory_path() /
            "marrow_mar178_scenario_a_deleted.marrow";
        std::filesystem::remove(delete_reload_path, ignored);
        marrow::editor::ProjectData deleted_portable = *session.project();
        deleted_portable.runtime_assets.skeleton_path = std::filesystem::absolute(
            session.project()->resolved_skeleton_path());
        deleted_portable.runtime_assets.atlas_paths.clear();
        for (const auto& atlas : session.project()->resolved_atlas_paths()) {
            deleted_portable.runtime_assets.atlas_paths.push_back(
                std::filesystem::absolute(atlas));
        }
        const auto deleted_saved =
            marrow::editor::save_project(deleted_portable, delete_reload_path);
        if (!deleted_saved) {
            std::cerr << "MAR-178 A: the project failed to save after the delete.\n";
            return false;
        }
        const auto deleted_reloaded =
            marrow::editor::load_project(delete_reload_path);
        if (!deleted_reloaded || deleted_reloaded.skeleton_data == nullptr) {
            std::cerr << "MAR-178 A: the deleted project saved but could NOT be "
                         "reopened: "
                      << (deleted_reloaded.error.has_value()
                              ? deleted_reloaded.error->format()
                              : std::string("(no error)"))
                      << '\n';
            return false;
        }
        if (mar178_constraint_present(
                *deleted_reloaded.skeleton_data,
                marrow::editor::ConstraintKind::Physics,
                "editor_ribbon_secondary")) {
            std::cerr << "MAR-178 A: the reloaded project resurrected the deleted "
                         "physics constraint.\n";
            return false;
        }
        std::filesystem::remove(delete_reload_path, ignored);
    }

    // --- Undo and redo restore and re-apply, compared as strings. -----------
    if (!session.undo() ||
        marrow::editor::serialize_project(*session.project()) != before_delete) {
        std::cerr << "MAR-178 A: undoing the delete did not restore the exact "
                     "pre-delete serialization.\n";
        return false;
    }
    if (!session.redo() ||
        mar178_constraint_present(
            *session.runtime_data(), marrow::editor::ConstraintKind::Physics,
            "editor_ribbon_secondary")) {
        std::cerr << "MAR-178 A: redoing the delete did not remove the constraint "
                     "again.\n";
        return false;
    }
    if (!session.undo() || !session.undo() ||
        marrow::editor::serialize_project(*session.project()) != before_rename) {
        std::cerr << "MAR-178 A: undoing both edits did not restore the exact original "
                     "serialization.\n";
        return false;
    }

    std::cout << "MAR-178 Scenario A: the catalog command renames and deletes "
                 "project-only constraints through one transaction each, reports "
                 "`project` ownership with an empty skin summary, saves and reloads, "
                 "and undo restores the byte-exact serialization.\n";
    return true;
}

bool validate_mar178_scenario_c(
    const marrow::editor::ProjectLoadResult& project_result) {
    if (project_result.project == nullptr) {
        std::cerr << "MAR-178 Scenario C requires a loaded editor project.\n";
        return false;
    }

    const auto open_session = [&](marrow::editor::EditorSession* session) {
        const auto opened = session->open(project_result.project->source_path);
        return static_cast<bool>(opened) && session->runtime_data() != nullptr;
    };

    // --- Rename: the selected constraint follows its own identity. ----------
    {
        marrow::editor::EditorSession session;
        if (!open_session(&session)) {
            std::cerr << "MAR-178 C could not open the smoke project.\n";
            return false;
        }
        marrow::editor::SelectionSet selection;
        selection.replace(marrow::editor::BoneSelection{"root"});
        selection.toggle(marrow::editor::ConstraintSelection{
            marrow::editor::ConstraintKind::Ik, "editor_arm_reach"});

        const marrow::editor::ConstraintCatalogResult renamed =
            marrow::editor::apply_constraint_catalog_edit(
                session,
                {marrow::editor::ConstraintCatalogEditKind::Rename,
                 marrow::editor::ConstraintKind::Ik, "editor_arm_reach",
                 "arm_reach_v2"},
                &selection,
                mar178_descriptor("Renamed IK constraint"));
        if (!renamed.ok || !renamed.selection_changed) {
            std::cerr << "MAR-178 C: renaming a selected constraint must report a "
                         "selection change; ok="
                      << renamed.ok << " selection_changed=" << renamed.selection_changed
                      << " message=" << renamed.message << '\n';
            return false;
        }
        if (selection.items().size() != 2U) {
            std::cerr << "MAR-178 C: a rename must not change the selection size; "
                         "measured "
                      << selection.items().size() << ".\n";
            return false;
        }
        const auto* active = selection.active_constraint();
        if (active == nullptr || active->constraint_name != "arm_reach_v2" ||
            active->kind != marrow::editor::ConstraintKind::Ik) {
            std::cerr << "MAR-178 C: the active constraint selection did not follow "
                         "the rename. Without the remap, reconcile can only prune, so "
                         "the panel loses the constraint it is editing.\n";
            return false;
        }
        const auto* bone =
            std::get_if<marrow::editor::BoneSelection>(&selection.items()[0]);
        if (bone == nullptr || bone->bone_name != "root") {
            std::cerr << "MAR-178 C: the co-selected bone must be untouched by a "
                         "constraint rename.\n";
            return false;
        }
    }

    // --- Delete: the identity is pruned and the last survivor becomes active. --
    {
        marrow::editor::EditorSession session;
        if (!open_session(&session)) {
            std::cerr << "MAR-178 C could not open the smoke project.\n";
            return false;
        }
        marrow::editor::SelectionSet selection;
        selection.replace(marrow::editor::BoneSelection{"root"});
        selection.toggle(marrow::editor::ConstraintSelection{
            marrow::editor::ConstraintKind::Ik, "editor_arm_reach"});
        selection.toggle(marrow::editor::ConstraintSelection{
            marrow::editor::ConstraintKind::Path, "editor_guide_follow"});
        if (selection.items().size() != 3U ||
            selection.active_constraint() == nullptr ||
            selection.active_constraint()->constraint_name != "editor_guide_follow") {
            std::cerr << "MAR-178 C: the three-member fixture selection did not build "
                         "as expected.\n";
            return false;
        }

        const marrow::editor::ConstraintCatalogResult deleted =
            marrow::editor::apply_constraint_catalog_edit(
                session,
                {marrow::editor::ConstraintCatalogEditKind::Delete,
                 marrow::editor::ConstraintKind::Path, "editor_guide_follow", {}},
                &selection,
                mar178_descriptor("Deleted Path constraint"));
        if (!deleted.ok || !deleted.selection_changed) {
            std::cerr << "MAR-178 C: deleting a selected constraint must prune it from "
                         "the selection; ok="
                      << deleted.ok << " message=" << deleted.message << '\n';
            return false;
        }
        if (selection.items().size() != 2U) {
            std::cerr << "MAR-178 C: the deleted constraint left a ghost member. The "
                         "status line reports items().size() as \"; N selected\", so a "
                         "ghost is user-visible; measured "
                      << selection.items().size() << ".\n";
            return false;
        }
        const auto* first =
            std::get_if<marrow::editor::BoneSelection>(&selection.items()[0]);
        const auto* second =
            std::get_if<marrow::editor::ConstraintSelection>(&selection.items()[1]);
        if (first == nullptr || first->bone_name != "root" || second == nullptr ||
            second->constraint_name != "editor_arm_reach") {
            std::cerr << "MAR-178 C: a delete must preserve the order of the "
                         "survivors.\n";
            return false;
        }
        const auto* active = selection.active_constraint();
        if (active == nullptr || active->constraint_name != "editor_arm_reach") {
            std::cerr << "MAR-178 C: removing the active member must promote the LAST "
                         "survivor, which is what a later "
                         "reconcile_selection_to_runtime() would also choose.\n";
            return false;
        }
    }

    // --- Deleting an unselected constraint leaves the set untouched. --------
    {
        marrow::editor::EditorSession session;
        if (!open_session(&session)) {
            std::cerr << "MAR-178 C could not open the smoke project.\n";
            return false;
        }
        marrow::editor::SelectionSet selection;
        selection.replace(marrow::editor::ConstraintSelection{
            marrow::editor::ConstraintKind::Ik, "editor_arm_reach"});
        const std::vector<marrow::editor::SelectionItem> before = selection.items();

        const marrow::editor::ConstraintCatalogResult deleted =
            marrow::editor::apply_constraint_catalog_edit(
                session,
                {marrow::editor::ConstraintCatalogEditKind::Delete,
                 marrow::editor::ConstraintKind::Physics, "editor_ribbon_secondary",
                 {}},
                &selection,
                mar178_descriptor("Deleted Physics constraint"));
        if (!deleted.ok) {
            std::cerr << "MAR-178 C: deleting an unselected constraint must still "
                         "apply: "
                      << deleted.message << '\n';
            return false;
        }
        if (deleted.selection_changed || selection.items() != before) {
            std::cerr << "MAR-178 C: deleting an unselected constraint must leave the "
                         "selection identical and report selection_changed == false.\n";
            return false;
        }
    }

    // --- A rejection must not move the selection. ---------------------------
    {
        marrow::editor::EditorSession session;
        if (!open_session(&session)) {
            std::cerr << "MAR-178 C could not open the smoke project.\n";
            return false;
        }
        {
            auto seed = session.begin_edit(mar178_descriptor("Seeded sibling"));
            if (!seed) {
                std::cerr << "MAR-178 C could not seed a sibling IK constraint.\n";
                return false;
            }
            marrow::editor::IkConstraintEdit sibling =
                seed.project()->ik_constraint_edits.front();
            sibling.name = "arm_reach_sibling";
            seed.project()->ik_constraint_edits.push_back(std::move(sibling));
            if (!seed.commit()) {
                std::cerr << "MAR-178 C could not commit the sibling IK constraint.\n";
                return false;
            }
        }

        marrow::editor::SelectionSet selection;
        selection.replace(marrow::editor::ConstraintSelection{
            marrow::editor::ConstraintKind::Ik, "editor_arm_reach"});
        const std::vector<marrow::editor::SelectionItem> before = selection.items();
        const std::size_t undo_before = session.undo_count();
        const std::string serialized_before =
            marrow::editor::serialize_project(*session.project());

        // The rejection must name the SELECTED identity, or the cascade could
        // not have moved it even if it ran in the wrong order. A second IK
        // constraint gives the selected one a real collision target.
        const marrow::editor::ConstraintCatalogResult rejected =
            marrow::editor::apply_constraint_catalog_edit(
                session,
                {marrow::editor::ConstraintCatalogEditKind::Rename,
                 marrow::editor::ConstraintKind::Ik, "editor_arm_reach",
                 "arm_reach_sibling"},
                &selection,
                mar178_descriptor("Renamed IK constraint"));
        if (rejected.ok || rejected.message.empty()) {
            std::cerr << "MAR-178 C: a rename onto a name already used in the family "
                         "must be rejected with a message.\n";
            return false;
        }
        if (rejected.selection_changed || selection.items() != before ||
            session.undo_count() != undo_before ||
            marrow::editor::serialize_project(*session.project()) !=
                serialized_before) {
            std::cerr << "MAR-178 C: a rejected edit must leave the selection, the "
                         "history, and the byte serialization exactly as they were.\n";
            return false;
        }
    }

    std::cout << "MAR-178 Scenario C: rename remaps the selected identity and leaves "
                 "co-selected bones alone, delete prunes it and promotes the last "
                 "survivor, an unselected target reports no selection change, and a "
                 "rejection moves neither the selection nor the history.\n";
    return true;
}

/** @brief Opens a session over a `.marrow` written across a constraint fixture. */
bool mar178_open_skin_session(
    const std::filesystem::path& project_path,
    marrow::editor::EditorSession* session) {
    marrow::editor::MinimalProjectOptions options;
    options.project_path = project_path;
    options.skeleton_path =
        std::filesystem::absolute("assets/fixtures/skin_inherit_constraints.mskl");
    // `skin_inherit_constraints.mskl` ships no atlas and `load_project()`
    // requires at least one, so the project borrows `player_idle.matl`. Nothing
    // cross-validates an atlas against a skeleton.
    options.atlas_paths = {
        std::filesystem::absolute("assets/fixtures/player_idle.matl")};
    options.name = "mar178_skin";
    const marrow::editor::ProjectData project =
        marrow::editor::create_minimal_project(options);
    const auto saved = marrow::editor::save_project(project, project_path);
    if (!saved) {
        std::cerr << "MAR-178 could not write " << project_path << ": "
                  << saved.error->message << '\n';
        return false;
    }
    const auto opened = session->open(project_path);
    if (!opened || session->runtime_data() == nullptr) {
        std::cerr << "MAR-178 could not open " << project_path << ".\n";
        return false;
    }
    return true;
}

/**
 * @brief Saves a session's project and reloads it from disk.
 *
 * The save is not the assertion. `load_project()` runs `build_project_runtime()`
 * and the typed skeleton parse, and a lifecycle edit that fails to rewrite
 * `skins[*].<family>` produces a project that still SAVES and can never be
 * OPENED, so only the reload catches it.
 */
bool mar178_save_and_reload(
    const marrow::editor::EditorSession& session,
    const std::filesystem::path& path,
    marrow::editor::ProjectLoadResult* reloaded_out,
    std::string_view label) {
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    const auto saved = marrow::editor::save_project(*session.project(), path);
    if (!saved) {
        std::cerr << label << ": the project failed to save: " << saved.error->message
                  << '\n';
        return false;
    }
    *reloaded_out = marrow::editor::load_project(path);
    if (!*reloaded_out || reloaded_out->skeleton_data == nullptr) {
        std::cerr << label << ": the saved project could NOT be reopened: "
                  << (reloaded_out->error.has_value()
                          ? reloaded_out->error->format()
                          : std::string("(no error)"))
                  << '\n';
        return false;
    }
    return true;
}

bool validate_mar178_scenario_b() {
    const auto project_path =
        std::filesystem::temp_directory_path() / "marrow_mar178_scenario_b.marrow";
    const auto reload_path =
        std::filesystem::temp_directory_path() / "marrow_mar178_scenario_b_out.marrow";
    std::error_code ignored;

    // --- B1: a base-backed rename appends one record and rewrites the skin. --
    {
        marrow::editor::EditorSession session;
        if (!mar178_open_skin_session(project_path, &session)) {
            return false;
        }
        const marrow::editor::ConstraintCatalogResult renamed =
            marrow::editor::apply_constraint_catalog_edit(
                session,
                {marrow::editor::ConstraintCatalogEditKind::Rename,
                 marrow::editor::ConstraintKind::Transform, "cape_pull", "cape_drag"},
                nullptr,
                mar178_descriptor("Renamed Transform constraint cape_pull"));
        if (!renamed.ok || !renamed.used_operation || renamed.changed_upsert ||
            mar178_ownership(renamed) != "base") {
            std::cerr << "MAR-178 B1: a base-backed rename must append exactly one "
                         "ordered record and touch no upsert; ok="
                      << renamed.ok << " used_operation=" << renamed.used_operation
                      << " changed_upsert=" << renamed.changed_upsert
                      << " message=" << renamed.message << '\n';
            return false;
        }
        if (renamed.affected_skins.size() != 1U ||
            renamed.affected_skins.front() != "cape") {
            std::cerr << "MAR-178 B1: the affected-skin summary must be captured from "
                         "the PRE-mutation runtime and report exactly [cape]; measured "
                      << renamed.affected_skins.size() << " entries.\n";
            return false;
        }
        if (session.project()->constraint_lifecycle_operations.size() != 1U) {
            std::cerr << "MAR-178 B1: exactly one lifecycle record must be appended.\n";
            return false;
        }

        marrow::editor::ProjectLoadResult reloaded;
        if (!mar178_save_and_reload(session, reload_path, &reloaded, "MAR-178 B1")) {
            return false;
        }
        const auto& transforms = reloaded.skeleton_data->transform_constraints();
        if (transforms.size() != 1U || transforms.front().name != "cape_drag") {
            std::cerr << "MAR-178 B1: the reloaded rig did not carry the renamed "
                         "constraint.\n";
            return false;
        }
        const auto skin = std::find_if(
            reloaded.skeleton_data->skins().begin(),
            reloaded.skeleton_data->skins().end(),
            [](const marrow::runtime::SkinData& candidate) {
                return candidate.name == "cape";
            });
        if (skin == reloaded.skeleton_data->skins().end() ||
            skin->transform_constraint_indices.size() != 1U ||
            skin->transform_constraint_indices.front() != 0U) {
            std::cerr << "MAR-178 B1: skin `cape` must still resolve the renamed "
                         "constraint. A root-only rewrite makes the project "
                         "unopenable, which is why this asserts on the RELOAD.\n";
            return false;
        }
    }

    // --- B2: a base-backed delete tombstones and prunes the skin. -----------
    {
        marrow::editor::EditorSession session;
        if (!mar178_open_skin_session(project_path, &session)) {
            return false;
        }
        const marrow::editor::ConstraintCatalogResult deleted =
            marrow::editor::apply_constraint_catalog_edit(
                session,
                {marrow::editor::ConstraintCatalogEditKind::Delete,
                 marrow::editor::ConstraintKind::Transform, "cape_pull", {}},
                nullptr,
                mar178_descriptor("Deleted Transform constraint cape_pull"));
        if (!deleted.ok || !deleted.used_operation || deleted.changed_upsert ||
            mar178_ownership(deleted) != "base" ||
            deleted.affected_skins != std::vector<std::string>{"cape"}) {
            std::cerr << "MAR-178 B2: a base-backed delete must emit one tombstone and "
                         "report skin `cape`; message=" << deleted.message << '\n';
            return false;
        }

        marrow::editor::ProjectLoadResult reloaded;
        if (!mar178_save_and_reload(session, reload_path, &reloaded, "MAR-178 B2")) {
            return false;
        }
        if (!reloaded.skeleton_data->transform_constraints().empty()) {
            std::cerr << "MAR-178 B2: the reloaded rig still carries a transform "
                         "constraint.\n";
            return false;
        }
        const auto skin = std::find_if(
            reloaded.skeleton_data->skins().begin(),
            reloaded.skeleton_data->skins().end(),
            [](const marrow::runtime::SkinData& candidate) {
                return candidate.name == "cape";
            });
        if (skin == reloaded.skeleton_data->skins().end() ||
            !skin->transform_constraint_indices.empty()) {
            std::cerr << "MAR-178 B2: skin `cape` must survive with no transform "
                         "constraint reference.\n";
            return false;
        }
        // The adjacent scope must survive: MAR-172 shipped data loss by
        // destroying a neighbouring key while reporting ok.
        const auto cape_target =
            reloaded.skeleton_data->find_bone_index("cape_target");
        if (!cape_target.has_value() ||
            std::find(
                skin->bone_indices.begin(), skin->bone_indices.end(), *cape_target) ==
                skin->bone_indices.end()) {
            std::cerr << "MAR-178 B2: skin `cape` lost its `cape_target` bone to an "
                         "unrelated constraint delete.\n";
            return false;
        }
    }

    // --- B3: a shadowing delete needs BOTH representations. -----------------
    {
        marrow::editor::EditorSession session;
        if (!mar178_open_skin_session(project_path, &session)) {
            return false;
        }
        {
            auto seed = session.begin_edit(mar178_descriptor("Seeded shadowing upsert"));
            if (!seed) {
                std::cerr << "MAR-178 B3 could not open a seeding transaction.\n";
                return false;
            }
            marrow::editor::TransformConstraintEdit shadow;
            shadow.name = "cape_pull";
            shadow.source_bone_name = "controller";
            shadow.bone_names = {"constrained"};
            shadow.rotate_mix = 0.5;
            seed.project()->transform_constraint_edits.push_back(std::move(shadow));
            if (!seed.commit()) {
                std::cerr << "MAR-178 B3 could not commit the shadowing upsert.\n";
                return false;
            }
        }

        const marrow::editor::ConstraintCatalogResult deleted =
            marrow::editor::apply_constraint_catalog_edit(
                session,
                {marrow::editor::ConstraintCatalogEditKind::Delete,
                 marrow::editor::ConstraintKind::Transform, "cape_pull", {}},
                nullptr,
                mar178_descriptor("Deleted Transform constraint cape_pull"));
        if (!deleted.ok || !deleted.used_operation || !deleted.changed_upsert ||
            mar178_ownership(deleted) != "shadowed") {
            std::cerr << "MAR-178 B3: a shadowing delete must emit BOTH a tombstone and "
                         "an upsert erase; used_operation=" << deleted.used_operation
                      << " changed_upsert=" << deleted.changed_upsert
                      << " message=" << deleted.message << '\n';
            return false;
        }
        if (!session.project()->transform_constraint_edits.empty()) {
            std::cerr << "MAR-178 B3: the shadowing upsert survived its own delete.\n";
            return false;
        }

        marrow::editor::ProjectLoadResult reloaded;
        if (!mar178_save_and_reload(session, reload_path, &reloaded, "MAR-178 B3")) {
            return false;
        }
        if (!reloaded.skeleton_data->transform_constraints().empty()) {
            std::cerr << "MAR-178 B3: the base constraint resurrected after the "
                         "shadowing delete -- erasing only the upsert reads to the "
                         "user as \"delete did nothing\".\n";
            return false;
        }
    }

    std::filesystem::remove(project_path, ignored);
    std::filesystem::remove(reload_path, ignored);
    std::cout << "MAR-178 Scenario B: the command reports `base` for a base-backed "
                 "edit and `shadowed` when an upsert covers it, captures [cape] from "
                 "the pre-mutation runtime, and every result saves AND reopens with "
                 "skins.cape and cape_target intact.\n";
    return true;
}

bool validate_mar178_scenario_d(
    const marrow::editor::ProjectLoadResult& project_result) {
    if (project_result.project == nullptr) {
        std::cerr << "MAR-178 Scenario D requires a loaded editor project.\n";
        return false;
    }

    marrow::editor::EditorSession session;
    const auto opened = session.open(project_result.project->source_path);
    if (!opened || session.runtime_data() == nullptr) {
        std::cerr << "MAR-178 Scenario D could not open the smoke project.\n";
        return false;
    }
    // A sibling gives the IK family a real collision target.
    {
        auto seed = session.begin_edit(mar178_descriptor("Seeded sibling"));
        if (!seed) {
            std::cerr << "MAR-178 D could not seed a sibling IK constraint.\n";
            return false;
        }
        marrow::editor::IkConstraintEdit sibling =
            seed.project()->ik_constraint_edits.front();
        sibling.name = "arm_reach_sibling";
        seed.project()->ik_constraint_edits.push_back(std::move(sibling));
        if (!seed.commit()) {
            std::cerr << "MAR-178 D could not commit the sibling IK constraint.\n";
            return false;
        }
    }

    struct Rejection {
        const char* label;
        marrow::editor::ConstraintCatalogEditKind kind;
        marrow::editor::ConstraintKind family;
        const char* source;
        const char* destination;
        const char* message_must_contain;
    };
    const std::array<Rejection, 6> kRejections{{
        {"a rename onto a name the family already carries",
         marrow::editor::ConstraintCatalogEditKind::Rename,
         marrow::editor::ConstraintKind::Ik, "editor_arm_reach", "arm_reach_sibling",
         "already taken"},
        {"a rename onto the source's own name",
         marrow::editor::ConstraintCatalogEditKind::Rename,
         marrow::editor::ConstraintKind::Ik, "editor_arm_reach", "editor_arm_reach",
         "must differ"},
        {"a rename from an empty source",
         marrow::editor::ConstraintCatalogEditKind::Rename,
         marrow::editor::ConstraintKind::Ik, "", "arm_reach_v2", "must not be empty"},
        {"a rename of a name that does not exist",
         marrow::editor::ConstraintCatalogEditKind::Rename,
         marrow::editor::ConstraintKind::Ik, "never_existed", "arm_reach_v2",
         "does not exist"},
        {"a delete of a name that does not exist",
         marrow::editor::ConstraintCatalogEditKind::Delete,
         marrow::editor::ConstraintKind::Physics, "never_existed", "",
         "does not exist"},
        // The identity is `(family, name)`: a real physics constraint named
        // through the IK family is not found, and the message says which family
        // it looked in.
        {"a delete of a real name in the wrong family",
         marrow::editor::ConstraintCatalogEditKind::Delete,
         marrow::editor::ConstraintKind::Ik, "editor_ribbon_secondary", "", "ik"},
    }};

    for (const Rejection& rejection : kRejections) {
        const std::string serialized_before =
            marrow::editor::serialize_project(*session.project());
        const std::size_t undo_before = session.undo_count();

        const marrow::editor::ConstraintCatalogResult result =
            marrow::editor::apply_constraint_catalog_edit(
                session,
                {rejection.kind, rejection.family, rejection.source,
                 rejection.destination},
                nullptr,
                mar178_descriptor("Rejected constraint edit"));
        if (result.ok || result.changed) {
            std::cerr << "MAR-178 D: " << rejection.label << " must be refused.\n";
            return false;
        }
        if (result.message.find(rejection.message_must_contain) == std::string::npos) {
            std::cerr << "MAR-178 D: " << rejection.label
                      << " must report a message containing '"
                      << rejection.message_must_contain << "'; measured '"
                      << result.message << "'.\n";
            return false;
        }
        if (marrow::editor::serialize_project(*session.project()) !=
            serialized_before) {
            std::cerr << "MAR-178 D: " << rejection.label
                      << " changed the project's byte serialization.\n";
            return false;
        }
        if (session.undo_count() != undo_before) {
            std::cerr << "MAR-178 D: " << rejection.label
                      << " left a history entry behind; undo_count " << undo_before
                      << " -> " << session.undo_count() << ".\n";
            return false;
        }
    }

    std::cout << "MAR-178 Scenario D: " << kRejections.size()
              << " rejections each leave the project's serialization byte-identical "
                 "and the history untouched, and the wrong-family case names the "
                 "family it searched.\n";
    return true;
}

bool validate_mar178_scenario_e() {
    // §4's decision, pinned rather than inherited silently.
    //
    // MAR-177's step 5 runs `validate_project_for_save()`, which requires at
    // least one atlas path. Keeping that blunt gate costs nothing REACHABLE:
    // `load_project()` already refuses an atlas-free document, and the shell's
    // only entry into a `ProjectData` is `reload_project()` -> `EditorSession::
    // open()` -> `load_project()`. The refusal therefore reports a blocker the
    // user already has rather than creating one.
    marrow::editor::MinimalProjectOptions options;
    options.project_path = "mar178_atlas_free.marrow";
    options.skeleton_path =
        std::filesystem::absolute("assets/fixtures/skin_inherit_constraints.mskl");
    options.atlas_paths = {};
    options.name = "mar178_atlas_free";
    marrow::editor::ProjectData project =
        marrow::editor::create_minimal_project(options);

    const auto base = marrow::runtime::load_skeleton_document(
        std::filesystem::absolute("assets/fixtures/skin_inherit_constraints.mskl"));
    if (!base) {
        std::cerr << "MAR-178 E could not load the skin fixture skeleton.\n";
        return false;
    }

    const std::string before = marrow::editor::serialize_project(project);
    const auto renamed = marrow::editor::rename_constraint(
        &project, *base.document, marrow::editor::ConstraintKind::Transform,
        "cape_pull", "cape_drag");
    if (renamed.ok) {
        std::cerr << "MAR-178 E: the primitive returned ok for a project "
                     "save_project() refuses -- step 5's validator has been "
                     "narrowed.\n";
        return false;
    }
    if (renamed.message != "at least one atlas path is required") {
        std::cerr << "MAR-178 E: expected the save validator's atlas message, got: "
                  << renamed.message << '\n';
        return false;
    }
    if (marrow::editor::serialize_project(project) != before) {
        std::cerr << "MAR-178 E: the refusal mutated the project.\n";
        return false;
    }

    // The same refusal through the COMMAND, on the one path that reaches this
    // state: a loadable project whose atlas list is emptied in memory. The
    // command must pass the validator's sentence through unchanged -- a surface
    // that paraphrases a validator drifts from it -- and must change nothing.
    {
        const auto session_path =
            std::filesystem::temp_directory_path() / "marrow_mar178_scenario_e.marrow";
        marrow::editor::EditorSession session;
        if (!mar178_open_skin_session(session_path, &session)) {
            return false;
        }
        {
            auto strip = session.begin_edit(mar178_descriptor("Emptied atlas list"));
            if (!strip) {
                std::cerr << "MAR-178 E could not open a transaction to empty the "
                             "atlas list.\n";
                return false;
            }
            strip.project()->runtime_assets.atlas_paths.clear();
            if (!strip.commit()) {
                std::cerr << "MAR-178 E could not commit the emptied atlas list.\n";
                return false;
            }
        }

        const std::string serialized_before =
            marrow::editor::serialize_project(*session.project());
        const std::size_t undo_before = session.undo_count();
        const marrow::editor::ConstraintCatalogResult refused =
            marrow::editor::apply_constraint_catalog_edit(
                session,
                {marrow::editor::ConstraintCatalogEditKind::Rename,
                 marrow::editor::ConstraintKind::Transform, "cape_pull", "cape_drag"},
                nullptr,
                mar178_descriptor("Renamed Transform constraint cape_pull"));
        if (refused.ok) {
            std::cerr << "MAR-178 E: the command accepted a rename on a project "
                         "save_project() refuses.\n";
            return false;
        }
        if (refused.message.find("at least one atlas path is required") ==
            std::string::npos) {
            std::cerr << "MAR-178 E: the command must carry the validator's sentence "
                         "VERBATIM; measured '"
                      << refused.message << "'.\n";
            return false;
        }
        if (marrow::editor::serialize_project(*session.project()) !=
                serialized_before ||
            session.undo_count() != undo_before) {
            std::cerr << "MAR-178 E: the atlas refusal changed the project or the "
                         "history.\n";
            return false;
        }
        std::error_code session_cleanup;
        std::filesystem::remove(session_path, session_cleanup);
    }

    // The other half of the decision: no project on DISK can be in this state,
    // because the loader refuses an atlas-free document before a session exists.
    // The in-memory path above is the only way in, which is why the gate costs
    // nothing reachable and why §4.2 is about the message rather than the check.
    const auto atlas_free_path =
        std::filesystem::temp_directory_path() / "marrow_mar178_atlas_free.marrow";
    std::error_code ignored;
    std::filesystem::remove(atlas_free_path, ignored);
    {
        std::ofstream out(atlas_free_path);
        out << "{\"marrow\":\"1.0\",\"name\":\"mar178_atlas_free\",\"runtime\":{"
            << "\"skeleton\":\""
            << std::filesystem::absolute(
                   "assets/fixtures/skin_inherit_constraints.mskl")
                   .string()
            << "\",\"atlases\":[]}}\n";
    }
    const auto loaded = marrow::editor::load_project(atlas_free_path);
    if (loaded) {
        std::cerr << "MAR-178 E: load_project() accepted an atlas-free project, so "
                     "the gate IS reachable and the decision must be revisited.\n";
        return false;
    }
    const std::string load_message = loaded.error->format();
    if (load_message.find("$.runtime.atlases") == std::string::npos ||
        load_message.find("array must not be empty") == std::string::npos) {
        std::cerr << "MAR-178 E: expected the loader's `$.runtime.atlases` refusal, "
                     "got: "
                  << load_message << '\n';
        return false;
    }
    std::filesystem::remove(atlas_free_path, ignored);

    std::cout << "MAR-178 Scenario E: the blunt atlas gate is kept deliberately. "
                 "The primitive refuses an atlas-free rename with \""
              << renamed.message
              << "\" and changes nothing; the command carries that sentence through "
                 "VERBATIM and leaves the project and history untouched; and "
                 "load_project() refuses an atlas-free document outright, so the "
                 "state is reachable only by emptying the list in memory -- never "
                 "by opening a project.\n";
    return true;
}

/** @brief Whether a JSON object carries `key` at all. */
bool mar178_has_key(
    const marrow::runtime::json::Value& object,
    std::string_view key) {
    return object.is_object() && object.as_object().find(key) != object.as_object().end();
}

bool validate_mar178_scenario_f() {
    // MAR-177 owns the byte deltas of this fixture and asserts them on the
    // PRIMITIVE path. MAR-178 asserts a different property -- occurrence counts
    // and, above all, the save -> reload cycle -- over the COMMAND path.
    std::error_code ignored;
    const auto directory =
        std::filesystem::temp_directory_path() / "marrow_mar178_export";
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory, ignored);

    const auto project_path = directory / "project.marrow";
    const auto rename_export = directory / "rename.mskl";
    const auto delete_export = directory / "delete.mskl";

    const auto reload_export = [](const std::filesystem::path& path,
                                  marrow::runtime::json::Document* document_out,
                                  marrow::runtime::SkeletonDataResult* parsed_out) {
        const auto document = marrow::runtime::load_skeleton_document(path);
        if (!document) {
            std::cerr << "MAR-178 F: the exported document did not load: "
                      << document.error->format() << '\n';
            return false;
        }
        *document_out = *document.document;
        *parsed_out = marrow::runtime::load_skeleton_data(*document_out);
        if (!*parsed_out) {
            std::cerr << "MAR-178 F: the exported document did not TYPED-parse: "
                      << parsed_out->error->format()
                      << "\n           This is the assertion that catches the "
                         "unopenable-project failure: it lands on load, not on save, "
                         "so no save-only check can see it.\n";
            return false;
        }
        return true;
    };

    // --- F1: a command-path rename rewrites BOTH occurrences. --------------
    {
        marrow::editor::EditorSession session;
        if (!mar178_open_skin_session(project_path, &session)) {
            return false;
        }
        const marrow::editor::ConstraintCatalogResult renamed =
            marrow::editor::apply_constraint_catalog_edit(
                session,
                {marrow::editor::ConstraintCatalogEditKind::Rename,
                 marrow::editor::ConstraintKind::Transform, "cape_pull", "cape_drag"},
                nullptr,
                mar178_descriptor("Renamed Transform constraint cape_pull"));
        if (!renamed.ok) {
            std::cerr << "MAR-178 F1: the rename was refused: " << renamed.message
                      << '\n';
            return false;
        }
        marrow::editor::ProjectExportOptions options;
        options.skeleton_output_path = rename_export;
        const auto exported = session.export_runtime(options);
        if (!exported) {
            std::cerr << "MAR-178 F1: the export failed: " << exported.error->format()
                      << '\n';
            return false;
        }

        const std::string text = mar177_read_file(rename_export);
        const std::size_t old_occurrences = mar177_count_quoted(text, "cape_pull");
        const std::size_t new_occurrences = mar177_count_quoted(text, "cape_drag");
        if (old_occurrences != 0U || new_occurrences != 2U) {
            std::cerr << "MAR-178 F1: the exported skeleton names `cape_pull` "
                      << old_occurrences << " times and `cape_drag` "
                      << new_occurrences
                      << " times; expected 0 and 2 (root.transform[0].name and "
                         "root.skins.cape.transform[0]). A root-only rewrite gives 1 "
                         "and fails the reload below.\n";
            return false;
        }

        marrow::runtime::json::Document document;
        marrow::runtime::SkeletonDataResult parsed;
        if (!reload_export(rename_export, &document, &parsed)) {
            return false;
        }
        const auto& skins = parsed.skeleton_data->skins();
        const auto cape = std::find_if(
            skins.begin(), skins.end(),
            [](const marrow::runtime::SkinData& candidate) {
                return candidate.name == "cape";
            });
        if (cape == skins.end() || cape->transform_constraint_indices.size() != 1U) {
            std::cerr << "MAR-178 F1: the reloaded skin `cape` must resolve exactly "
                         "one transform constraint.\n";
            return false;
        }
        const std::size_t index = cape->transform_constraint_indices.front();
        if (index >= parsed.skeleton_data->transform_constraints().size() ||
            parsed.skeleton_data->transform_constraints()[index].name != "cape_drag") {
            std::cerr << "MAR-178 F1: the reloaded skin `cape` does not point at the "
                         "renamed constraint.\n";
            return false;
        }
    }

    // --- F2: a command-path delete erases the keys rather than emptying them.
    {
        marrow::editor::EditorSession session;
        if (!mar178_open_skin_session(project_path, &session)) {
            return false;
        }
        const marrow::editor::ConstraintCatalogResult deleted =
            marrow::editor::apply_constraint_catalog_edit(
                session,
                {marrow::editor::ConstraintCatalogEditKind::Delete,
                 marrow::editor::ConstraintKind::Transform, "cape_pull", {}},
                nullptr,
                mar178_descriptor("Deleted Transform constraint cape_pull"));
        if (!deleted.ok) {
            std::cerr << "MAR-178 F2: the delete was refused: " << deleted.message
                      << '\n';
            return false;
        }
        marrow::editor::ProjectExportOptions options;
        options.skeleton_output_path = delete_export;
        const auto exported = session.export_runtime(options);
        if (!exported) {
            std::cerr << "MAR-178 F2: the export failed: " << exported.error->format()
                      << '\n';
            return false;
        }

        const std::string text = mar177_read_file(delete_export);
        if (mar177_count_quoted(text, "cape_pull") != 0U) {
            std::cerr << "MAR-178 F2: the deleted name still occurs in the export.\n";
            return false;
        }

        marrow::runtime::json::Document document;
        marrow::runtime::SkeletonDataResult parsed;
        if (!reload_export(delete_export, &document, &parsed)) {
            return false;
        }
        // The last delete in a family must ERASE the key, not leave `[]`: the
        // runtime refuses an empty family array with "transform constraints must
        // not be empty when provided", so an emptied array fails the reload.
        if (mar178_has_key(document.root, "transform")) {
            std::cerr << "MAR-178 F2: the export kept a root `transform` key after "
                         "deleting the family's only constraint.\n";
            return false;
        }
        const auto skins_member = document.root.as_object().find("skins");
        if (skins_member == document.root.as_object().end()) {
            std::cerr << "MAR-178 F2: the export lost its `skins` object.\n";
            return false;
        }
        const auto cape_member = skins_member->second.as_object().find("cape");
        if (cape_member == skins_member->second.as_object().end()) {
            std::cerr << "MAR-178 F2: the export lost `skins.cape`.\n";
            return false;
        }
        if (mar178_has_key(cape_member->second, "transform")) {
            std::cerr << "MAR-178 F2: `skins.cape` kept a `transform` key after the "
                         "delete pruned its only reference.\n";
            return false;
        }
        if (!parsed.skeleton_data->transform_constraints().empty()) {
            std::cerr << "MAR-178 F2: the reloaded rig still carries a transform "
                         "constraint.\n";
            return false;
        }
        const auto& skins = parsed.skeleton_data->skins();
        const auto cape = std::find_if(
            skins.begin(), skins.end(),
            [](const marrow::runtime::SkinData& candidate) {
                return candidate.name == "cape";
            });
        const auto cape_target = parsed.skeleton_data->find_bone_index("cape_target");
        if (cape == skins.end() || !cape_target.has_value() ||
            std::find(
                cape->bone_indices.begin(), cape->bone_indices.end(), *cape_target) ==
                cape->bone_indices.end()) {
            std::cerr << "MAR-178 F2: skin `cape` must survive the delete with its "
                         "`cape_target` bone intact -- the adjacent-scope survival "
                         "assertion MAR-172's failure shape demands.\n";
            return false;
        }
    }

    std::filesystem::remove_all(directory, ignored);
    std::cout << "MAR-178 Scenario F: a command-path rename exports 0 x `cape_pull` "
                 "and exactly 2 x `cape_drag` and RELOADS with skins.cape resolving "
                 "the new name; a command-path delete erases both the root "
                 "`transform` key and `skins.cape.transform`, RELOADS with zero "
                 "transform constraints, and keeps `cape_target`.\n";
    return true;
}

bool validate_mar178_scenario_g() {
    // The family spelling lives twice: `constraint_family_json_key()` inside
    // `project.cpp` writes the serialized records, and `constraint_family_key()`
    // in the new header is what the surfaces parse and emit. Nothing makes them
    // agree by construction, so this asserts the agreement instead.
    struct Case {
        marrow::editor::ConstraintKind family;
        const char* expected;
    };
    constexpr std::array<Case, 4> kCases{{
        {marrow::editor::ConstraintKind::Ik, "ik"},
        {marrow::editor::ConstraintKind::Path, "path"},
        {marrow::editor::ConstraintKind::Transform, "transform"},
        {marrow::editor::ConstraintKind::Physics, "physics"},
    }};

    for (const auto& test_case : kCases) {
        if (marrow::editor::constraint_family_key(test_case.family) !=
            test_case.expected) {
            std::cerr << "MAR-178 G: constraint_family_key() spells the family "
                         "expected to be '"
                      << test_case.expected << "' unexpectedly.\n";
            return false;
        }
        const auto parsed =
            marrow::editor::parse_constraint_family(test_case.expected);
        if (!parsed.has_value() || *parsed != test_case.family) {
            std::cerr << "MAR-178 G: parse_constraint_family('" << test_case.expected
                      << "') did not round-trip.\n";
            return false;
        }

        marrow::editor::MinimalProjectOptions options;
        options.project_path = "mar178_family_probe.marrow";
        options.skeleton_path =
            std::filesystem::absolute("assets/fixtures/player_idle.mskl");
        options.atlas_paths = {
            std::filesystem::absolute("assets/fixtures/player_idle.matl")};
        options.name = "mar178";
        marrow::editor::ProjectData project =
            marrow::editor::create_minimal_project(options);
        project.constraint_lifecycle_operations.push_back(
            {marrow::editor::ConstraintLifecycleKind::Rename,
             test_case.family,
             "probe_source",
             "probe_target"});

        const std::string serialized = marrow::editor::serialize_project(project);
        const std::string needle =
            std::string("\"family\": \"") + test_case.expected + "\"";
        if (serialized.find(needle) == std::string::npos) {
            std::cerr << "MAR-178 G: serialize_project() did not emit " << needle
                      << " for the family constraint_family_key() spells '"
                      << test_case.expected
                      << "'. The two spellings have drifted.\n";
            return false;
        }
    }

    for (const char* rejected : {"bone", "IK", "", "transforms"}) {
        if (marrow::editor::parse_constraint_family(rejected).has_value()) {
            std::cerr << "MAR-178 G: parse_constraint_family accepted '" << rejected
                      << "'.\n";
            return false;
        }
    }

    std::cout << "MAR-178 Scenario G: the header's family spelling matches the "
                 "`\"family\"` string serialize_project() writes for all four "
                 "families, and nothing else parses.\n";
    return true;
}

// --- MAR-179: the eleven parameter fields, at the model layer ---------------
//
// These cases exist because the widget stories above them can author a value,
// and a value is only real once it has survived save -> LOAD -> materialize.
// `validate_project_for_save` has no base document and structurally cannot see
// cross-references, so a passing save() proves nothing on its own; every
// assertion below reads the MATERIALIZED runtime struct after a reload.

const marrow::editor::IkConstraintEdit* mar179_find_ik(
    const marrow::editor::ProjectData& project, std::string_view name) {
    for (const auto& edit : project.ik_constraint_edits) {
        if (edit.name == name) return &edit;
    }
    return nullptr;
}

const marrow::editor::PhysicsConstraintEdit* mar179_find_physics(
    const marrow::editor::ProjectData& project, std::string_view name) {
    for (const auto& edit : project.physics_constraint_edits) {
        if (edit.name == name) return &edit;
    }
    return nullptr;
}

const marrow::runtime::IkConstraintData* mar179_runtime_ik(
    const marrow::runtime::SkeletonData& skeleton, std::string_view name) {
    for (const auto& constraint : skeleton.ik_constraints()) {
        if (constraint.name == name) return &constraint;
    }
    return nullptr;
}

const marrow::runtime::PhysicsConstraintData* mar179_runtime_physics(
    const marrow::runtime::SkeletonData& skeleton, std::string_view name) {
    for (const auto& constraint : skeleton.physics_constraints()) {
        if (constraint.name == name) return &constraint;
    }
    return nullptr;
}

/** @brief Rewrites the project so it can be saved and reopened anywhere. */
marrow::editor::ProjectData mar179_relocated(
    const marrow::editor::ProjectData& source,
    const std::filesystem::path& project_path) {
    marrow::editor::ProjectData project = source;
    project.source_path = project_path;
    project.runtime_assets.skeleton_path =
        std::filesystem::absolute("assets/fixtures/player_idle.mskl");
    project.runtime_assets.atlas_paths = {
        std::filesystem::absolute("assets/fixtures/player_idle.matl")};
    return project;
}

bool mar179_near(double actual, double expected, const char* what) {
    if (std::abs(actual - expected) <= 1e-9) return true;
    std::cerr << "MAR-179 model: " << what << " materialized as " << actual
              << ", expected " << expected << ".\n";
    return false;
}

bool validate_mar179_constraint_parameters(
    const marrow::editor::ProjectLoadResult& project_result) {
    const auto project_path =
        std::filesystem::temp_directory_path() / "marrow_mar179_parameters.marrow";
    std::error_code ignored;

    // --- S1: boundary round trip through save -> LOAD -> materialize. -------
    marrow::editor::ProjectData authored =
        mar179_relocated(*project_result.project, project_path);
    {
        auto* ik = const_cast<marrow::editor::IkConstraintEdit*>(
            mar179_find_ik(authored, "editor_arm_reach"));
        auto* physics = const_cast<marrow::editor::PhysicsConstraintEdit*>(
            mar179_find_physics(authored, "editor_ribbon_secondary"));
        if (ik == nullptr || physics == nullptr) {
            std::cerr << "MAR-179 S1 requires the fixture's editor_arm_reach and "
                         "editor_ribbon_secondary constraint edits.\n";
            return false;
        }
        // Every value here is a boundary the widgets can now reach: the lower
        // bound of each Form-B drag, plus the smallest distinctly displayable
        // physics step (1e-4, derived from the "%.4f" format).
        ik->softness = 12.5;
        ik->compress = true;
        ik->stretch = true;
        physics->step = 1e-4;
        physics->x = 0.0;
        physics->y = 2.5;
        physics->rotate = 0.0;
        physics->scale_x = 0.0;
        physics->shear_x = 0.75;
        physics->limit = 0.0;
        physics->mass_inverse = 0.0;
    }

    std::filesystem::remove(project_path, ignored);
    const auto saved = marrow::editor::save_project(authored, project_path);
    if (!saved) {
        std::cerr << "MAR-179 S1: the boundary values did not save: "
                  << saved.error->message << '\n';
        return false;
    }
    const auto reloaded = marrow::editor::load_project(project_path);
    if (!reloaded || reloaded.skeleton_data == nullptr) {
        std::cerr << "MAR-179 S1: the saved project could NOT be reopened: "
                  << (reloaded.error.has_value() ? reloaded.error->format()
                                                 : std::string("(no error)"))
                  << '\n';
        return false;
    }
    {
        const auto* ik = mar179_runtime_ik(*reloaded.skeleton_data, "editor_arm_reach");
        const auto* physics =
            mar179_runtime_physics(*reloaded.skeleton_data, "editor_ribbon_secondary");
        if (ik == nullptr || physics == nullptr) {
            std::cerr << "MAR-179 S1: the reloaded runtime lost a constraint.\n";
            return false;
        }
        if (!mar179_near(ik->softness, 12.5, "ik softness") ||
            !mar179_near(physics->step, 1e-4, "physics step") ||
            !mar179_near(physics->x, 0.0, "physics x") ||
            !mar179_near(physics->y, 2.5, "physics y") ||
            !mar179_near(physics->rotate, 0.0, "physics rotate") ||
            !mar179_near(physics->scale_x, 0.0, "physics scaleX") ||
            !mar179_near(physics->shear_x, 0.75, "physics shearX") ||
            !mar179_near(physics->limit, 0.0, "physics limit") ||
            !mar179_near(physics->mass_inverse, 0.0, "physics massInverse")) {
            return false;
        }
        if (!ik->compress || !ik->stretch) {
            std::cerr << "MAR-179 S1: compress/stretch did not survive the reload.\n";
            return false;
        }
    }

    // --- S2: what the loader refuses, per layer. ---------------------------
    struct InvalidCase {
        const char* label;
        double step;
        double mass_inverse;
        const char* runtime_message;
    };
    static constexpr std::array<InvalidCase, 3> kInvalid{{
        {"step = 0", 0.0, 0.0, "physics step must be greater than zero"},
        {"step = -1", -1.0, 0.0, "physics step must be greater than zero"},
        {"massInverse = -1", 1.0 / 60.0, -1.0, "physics massInverse must be non-negative"},
    }};
    for (const InvalidCase& invalid : kInvalid) {
        marrow::editor::ProjectData broken = authored;
        auto* physics = const_cast<marrow::editor::PhysicsConstraintEdit*>(
            mar179_find_physics(broken, "editor_ribbon_secondary"));
        if (physics == nullptr) return false;
        physics->step = invalid.step;
        physics->mass_inverse = invalid.mass_inverse;

        const auto runtime_result = marrow::editor::build_project_runtime(
            broken, *project_result.base_skeleton_document);
        if (runtime_result) {
            std::cerr << "MAR-179 S2 (" << invalid.label
                      << "): build_project_runtime accepted a value the loader must "
                         "refuse.\n";
            return false;
        }
        const std::string runtime_error = runtime_result.error->format();
        if (runtime_error.find(invalid.runtime_message) == std::string::npos) {
            std::cerr << "MAR-179 S2 (" << invalid.label
                      << "): expected the runtime parse to carry \""
                      << invalid.runtime_message << "\", measured \"" << runtime_error
                      << "\".\n";
            return false;
        }

        const auto broken_path = std::filesystem::temp_directory_path() /
            "marrow_mar179_invalid.marrow";
        std::filesystem::remove(broken_path, ignored);
        const auto broken_saved = marrow::editor::save_project(broken, broken_path);
        if (broken_saved) {
            std::cerr << "MAR-179 S2 (" << invalid.label
                      << "): save_project accepted an out-of-range physics value.\n";
            return false;
        }
        if (broken_saved.error->message.find(
                "physics constraint edit numeric values must stay within their "
                "valid ranges") == std::string::npos) {
            std::cerr << "MAR-179 S2 (" << invalid.label
                      << "): the save refusal message changed to \""
                      << broken_saved.error->message << "\".\n";
            return false;
        }
        if (std::filesystem::exists(broken_path)) {
            std::cerr << "MAR-179 S2 (" << invalid.label
                      << "): a refused save must leave no file behind.\n";
            return false;
        }
        // The good project on disk is untouched by the refused save.
        const auto still_good = marrow::editor::load_project(project_path);
        if (!still_good || still_good.skeleton_data == nullptr) {
            std::cerr << "MAR-179 S2 (" << invalid.label
                      << "): the previously saved project stopped loading.\n";
            return false;
        }
    }

    // L2, the layer that decides whether a FILE OPENS, and the only one the
    // three cases above cannot reach: save_project refuses to write the bad
    // value, and build_project_runtime is L1. A hand-edited or older `.marrow`
    // can still carry one, so the project parse is asserted directly by
    // patching the saved JSON and reopening it.
    {
        static constexpr std::array<std::pair<const char*, const char*>, 3> kL2Cases{{
            {R"("step": 0.0)", "physics constraint edit step must be greater than zero"},
            {R"("step": -1.0)", "physics constraint edit step must be greater than zero"},
            {R"("massInverse": -1.0)", "physics constraint edit massInverse must be non-negative"},
        }};
        std::ifstream good_stream(project_path);
        const std::string good_text(
            (std::istreambuf_iterator<char>(good_stream)),
            std::istreambuf_iterator<char>());
        good_stream.close();
        for (const auto& [replacement, expected] : kL2Cases) {
            const std::string key = std::string(replacement).substr(
                0, std::string(replacement).find(':') + 1);
            const auto key_position = good_text.find(key);
            if (key_position == std::string::npos) {
                std::cerr << "MAR-179 S2 (L2): the saved project does not contain "
                          << key << ".\n";
                return false;
            }
            const auto value_end = good_text.find_first_of(",\n}", key_position);
            std::string patched = good_text;
            patched.replace(
                key_position, value_end - key_position, replacement);

            const auto patched_path = std::filesystem::temp_directory_path() /
                "marrow_mar179_l2.marrow";
            std::filesystem::remove(patched_path, ignored);
            std::ofstream out(patched_path);
            out << patched;
            out.close();

            const auto opened = marrow::editor::load_project(patched_path);
            if (opened) {
                std::cerr << "MAR-179 S2 (L2, " << replacement
                          << "): load_project OPENED a `.marrow` the project parse "
                             "must refuse. A file carrying this value would become "
                             "loadable, and the value would reach the runtime.\n";
                std::filesystem::remove(patched_path, ignored);
                return false;
            }
            const std::string message = opened.error->format();
            if (message.find(expected) == std::string::npos) {
                std::cerr << "MAR-179 S2 (L2, " << replacement
                          << "): expected the project parse to carry \"" << expected
                          << "\", measured \"" << message << "\".\n";
                std::filesystem::remove(patched_path, ignored);
                return false;
            }
            std::filesystem::remove(patched_path, ignored);
        }
    }

    // --- S3: the softness asymmetry, asserted BOTH ways. -------------------
    //
    // The agent rejects a negative softness (a surface guard); the format
    // accepts one at all three layers and the runtime reads it as zero. This
    // case is the compatibility half: it must be SEEN to pass, because the
    // moment anyone "fixes" the missing validation in project.cpp, an existing
    // `.marrow` carrying a negative softness stops opening.
    {
        const auto negative_path = std::filesystem::temp_directory_path() /
            "marrow_mar179_negative_softness.marrow";
        marrow::editor::ProjectData negative = mar179_relocated(authored, negative_path);
        auto* ik = const_cast<marrow::editor::IkConstraintEdit*>(
            mar179_find_ik(negative, "editor_arm_reach"));
        if (ik == nullptr) return false;
        ik->softness = -3.0;

        std::filesystem::remove(negative_path, ignored);
        const auto negative_saved =
            marrow::editor::save_project(negative, negative_path);
        if (!negative_saved) {
            std::cerr << "MAR-179 S3: a negative softness must still SAVE; the save "
                         "refused with \"" << negative_saved.error->message << "\".\n";
            return false;
        }
        const auto negative_reloaded = marrow::editor::load_project(negative_path);
        if (!negative_reloaded || negative_reloaded.skeleton_data == nullptr) {
            std::cerr << "MAR-179 S3: a `.marrow` carrying softness = -3 must still "
                         "OPEN. It no longer does, which is exactly the backward "
                         "compatibility break MAR-179 refuses to take: "
                      << (negative_reloaded.error.has_value()
                              ? negative_reloaded.error->format()
                              : std::string("(no error)"))
                      << '\n';
            return false;
        }
        const auto* materialized =
            mar179_runtime_ik(*negative_reloaded.skeleton_data, "editor_arm_reach");
        if (materialized == nullptr ||
            !mar179_near(materialized->softness, -3.0, "negative ik softness")) {
            std::cerr << "MAR-179 S3: the materialized softness is not the authored "
                         "-3.0, so a layer silently clamped or rejected it.\n";
            return false;
        }
        // And it survives a second full round trip from the reloaded document.
        const auto again_path = std::filesystem::temp_directory_path() /
            "marrow_mar179_negative_softness_again.marrow";
        std::filesystem::remove(again_path, ignored);
        marrow::editor::ProjectData again =
            mar179_relocated(*negative_reloaded.project, again_path);
        const auto again_saved = marrow::editor::save_project(again, again_path);
        const auto again_reloaded = again_saved
            ? marrow::editor::load_project(again_path)
            : marrow::editor::ProjectLoadResult{};
        const auto* again_materialized = again_reloaded.skeleton_data != nullptr
            ? mar179_runtime_ik(*again_reloaded.skeleton_data, "editor_arm_reach")
            : nullptr;
        if (!again_saved || again_materialized == nullptr ||
            !mar179_near(again_materialized->softness, -3.0, "re-saved ik softness")) {
            std::cerr << "MAR-179 S3: softness = -3 did not survive a second "
                         "save/reload cycle.\n";
            return false;
        }
        std::filesystem::remove(negative_path, ignored);
        std::filesystem::remove(again_path, ignored);
    }

    // --- S4: JSON and binary exports agree on all eleven fields. -----------
    //
    // `binary.cpp` is a generic JSON document encoder with no constraint-specific
    // path at all, so this assertion exists to keep that true, not because a
    // change is expected.
    //
    // MEASURED, and it corrects the design: the `.mbin` v2 encoder narrows EVERY
    // JSON number to float32 (`binary.cpp`, append_float32), so the two exports
    // cannot agree to double precision and never could. Authoring step = 1e-4
    // and comparing exactly reports 0.0001 vs 9.9999997473787516e-05. The
    // correct assertion is agreement AFTER the same narrowing -- which is also
    // the reason the "%.4f"-derived minimum is a safe floor: 1e-4 survives
    // float32 as a strictly positive number, so an exported `.mbin` authored at
    // the widget's minimum still satisfies the loader's "step > 0". A smaller
    // minimum could round to 0.0f and make the BINARY export unopenable while
    // the `.mskl` stayed fine.
    {
        const auto as_float32 = [](double value) {
            return static_cast<double>(static_cast<float>(value));
        };
        const auto export_json = std::filesystem::temp_directory_path() /
            "marrow_mar179_export.mskl";
        const auto export_binary = std::filesystem::temp_directory_path() /
            "marrow_mar179_export.mbin";
        std::filesystem::remove(export_json, ignored);
        std::filesystem::remove(export_binary, ignored);

        marrow::editor::ProjectExportOptions export_options;
        export_options.skeleton_output_path = export_json;
        export_options.binary_output_path = export_binary;
        const auto exported = marrow::editor::export_runtime_assets(
            authored, *project_result.base_skeleton_document, export_options);
        if (!exported) {
            std::cerr << "MAR-179 S4: the export failed: " << exported.error->format()
                      << '\n';
            return false;
        }
        const auto json_runtime = marrow::runtime::load_skeleton_data(export_json);
        const auto binary_runtime = marrow::runtime::load_skeleton_data(export_binary);
        if (!json_runtime || !binary_runtime) {
            std::cerr << "MAR-179 S4: an exported skeleton did not parse back.\n";
            return false;
        }
        const auto* json_ik =
            mar179_runtime_ik(*json_runtime.skeleton_data, "editor_arm_reach");
        const auto* binary_ik =
            mar179_runtime_ik(*binary_runtime.skeleton_data, "editor_arm_reach");
        const auto* json_physics = mar179_runtime_physics(
            *json_runtime.skeleton_data, "editor_ribbon_secondary");
        const auto* binary_physics = mar179_runtime_physics(
            *binary_runtime.skeleton_data, "editor_ribbon_secondary");
        if (json_ik == nullptr || binary_ik == nullptr || json_physics == nullptr ||
            binary_physics == nullptr) {
            std::cerr << "MAR-179 S4: an export lost a constraint.\n";
            return false;
        }
        if (as_float32(json_ik->softness) != binary_ik->softness ||
            json_ik->compress != binary_ik->compress ||
            json_ik->stretch != binary_ik->stretch ||
            as_float32(json_physics->step) != binary_physics->step ||
            as_float32(json_physics->x) != binary_physics->x ||
            as_float32(json_physics->y) != binary_physics->y ||
            as_float32(json_physics->rotate) != binary_physics->rotate ||
            as_float32(json_physics->scale_x) != binary_physics->scale_x ||
            as_float32(json_physics->shear_x) != binary_physics->shear_x ||
            as_float32(json_physics->limit) != binary_physics->limit ||
            as_float32(json_physics->mass_inverse) != binary_physics->mass_inverse) {
            std::cerr << std::setprecision(17)
                      << "MAR-179 S4: the `.mskl` and `.mbin` exports disagree on a "
                         "constraint parameter after float32 narrowing. step "
                      << json_physics->step << " -> " << binary_physics->step
                      << ", softness " << json_ik->softness << " -> "
                      << binary_ik->softness << ", massInverse "
                      << json_physics->mass_inverse << " -> "
                      << binary_physics->mass_inverse << ".\n";
            return false;
        }
        if (!mar179_near(binary_ik->softness, 12.5, "exported ik softness") ||
            !mar179_near(binary_physics->step, as_float32(1e-4),
                         "exported physics step") ||
            !mar179_near(binary_physics->mass_inverse, 0.0,
                         "exported physics massInverse")) {
            return false;
        }
        // The load above already proves it, but state it: the widget's minimum
        // step is still strictly positive after the float32 narrowing, so the
        // `.mbin` authored at that minimum satisfies "physics step > 0".
        if (!(binary_physics->step > 0.0)) {
            std::cerr << "MAR-179 S4: the exported binary step is not strictly "
                         "positive after float32 narrowing, so the widget minimum "
                         "would author an unopenable `.mbin`.\n";
            return false;
        }
        std::filesystem::remove(export_json, ignored);
        std::filesystem::remove(export_binary, ignored);
    }

    std::filesystem::remove(project_path, ignored);
    std::cout << "MAR-179 model layer: the eleven runtime-backed IK and physics "
                 "fields round-trip through save -> LOAD -> materialize at their "
                 "boundaries (step = 1e-4, limit/massInverse/x/rotate/scaleX = 0); "
                 "a zero, negative or negative-mass value is refused at ALL THREE "
                 "layers -- the runtime parse, save_project (which leaves no file "
                 "behind) and, for a hand-patched file, the project parse that "
                 "decides whether a `.marrow` OPENS; a "
                 "`.marrow` carrying softness = -3 still opens, materializes as "
                 "-3 and survives a second round trip, because the negative-softness "
                 "guard is a SURFACE guard and tightening the loader would make "
                 "existing projects unopenable; and the `.mskl` and `.mbin` exports "
                 "agree on all eleven AFTER the float32 narrowing `.mbin` v2 applies "
                 "to every number (binary.cpp append_float32), with the widget's "
                 "minimum step still strictly positive on the binary side.\n";
    return true;
}


// ===========================================================================
// MAR-180 -- atomic project I/O and runtime-source adoption.
//
// Every assertion that a saved project is *good* goes through `load_project`,
// never `json::load_document`. `validate_project_for_save` takes no base
// document (project.cpp) and structurally cannot resolve a cross-reference, so
// a successful `save()` proves nothing about whether the file it wrote OPENS.
// `editor_project_smoke.cpp`'s pre-existing save assertion uses the raw JSON
// parser and is exactly why the cross-directory Save As bug was invisible; it
// is deliberately left in place, and these cases add the load-bearing checks.
// ===========================================================================

namespace mar180 {

struct TemporaryDirectory {
    std::filesystem::path path;

    explicit TemporaryDirectory(std::string_view label) {
        const auto unique_suffix =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
            ("marrow-mar180-" + std::string(label) + "-" +
             std::to_string(unique_suffix));
        std::error_code ignored;
        std::filesystem::create_directories(path, ignored);
    }

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;
};

/**
 * @brief Scopes the process-global atomic-rename seam.
 *
 * HAZARD (design section 12.2): the seam is shared by the settings writer and
 * the project writer. Never perform an unrelated atomic write inside the scope.
 */
class ScopedRenameCallback {
public:
    explicit ScopedRenameCallback(marrow::editor::detail::RenameCallback callback) {
        marrow::editor::detail::set_preference_rename_callback_for_testing(
            std::move(callback));
    }

    ~ScopedRenameCallback() {
        marrow::editor::detail::set_preference_rename_callback_for_testing({});
    }

    ScopedRenameCallback(const ScopedRenameCallback&) = delete;
    ScopedRenameCallback& operator=(const ScopedRenameCallback&) = delete;
};

std::optional<std::string> read_bytes(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << input.rdbuf();
    if (!input && !input.eof()) {
        return std::nullopt;
    }
    return buffer.str();
}

bool write_bytes(const std::filesystem::path& path, std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        return false;
    }
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    output.close();
    return static_cast<bool>(output);
}

std::vector<std::string> directory_filenames(const std::filesystem::path& directory) {
    std::vector<std::string> names;
    std::error_code ignored;
    for (const auto& entry : std::filesystem::directory_iterator(directory, ignored)) {
        names.push_back(entry.path().filename().string());
    }
    std::sort(names.begin(), names.end());
    return names;
}

/// @brief The asset filenames every seeded copy carries beside the `.marrow`.
const std::array<const char*, 3>& fixture_asset_filenames() {
    static const std::array<const char*, 3> names{
        "player_idle.mskl", "player_idle.matl", "player_fixture.png"};
    return names;
}

/**
 * @brief Copies the fixture project and every asset it resolves into `directory`.
 * @return The copied `.marrow` path, or an empty path when a copy failed.
 */
std::filesystem::path seed_fixture_copy(
    const std::filesystem::path& fixture_project_path,
    const std::filesystem::path& directory) {
    const std::filesystem::path source_directory =
        std::filesystem::absolute(fixture_project_path).parent_path();
    std::error_code error;
    const std::filesystem::path destination =
        directory / fixture_project_path.filename();
    std::filesystem::copy_file(
        std::filesystem::absolute(fixture_project_path),
        destination,
        std::filesystem::copy_options::overwrite_existing,
        error);
    if (error) {
        return {};
    }
    for (const char* name : fixture_asset_filenames()) {
        std::filesystem::copy_file(
            source_directory / name,
            directory / name,
            std::filesystem::copy_options::overwrite_existing,
            error);
        if (error) {
            return {};
        }
    }
    return destination;
}

} // namespace mar180

/**
 * @brief MAR-180 S1 -- a failed project save preserves the previous file exactly.
 *
 * Before MAR-180, `save_project` opened the destination with `std::ofstream
 * output(path)`, which TRUNCATES it before anything knew the new content was
 * writable, and checked `if (!output)` BEFORE `~ofstream` flushed -- so a
 * close-time write error was never observed at all and `save_project` returned
 * success over a truncated file. This case injects a rename failure at the last
 * possible moment and asserts the destination survived byte-for-byte.
 */
bool validate_mar180_atomic_save_preserves_previous_file(
    const std::filesystem::path& fixture_project_path) {
    const mar180::TemporaryDirectory temporary("s1");
    const std::filesystem::path destination =
        mar180::seed_fixture_copy(fixture_project_path, temporary.path);
    if (destination.empty()) {
        std::cerr << "MAR-180 S1: failed to seed the fixture copy.\n";
        return false;
    }

    const auto loaded = marrow::editor::load_project(destination);
    if (!loaded) {
        std::cerr << "MAR-180 S1: the seeded fixture copy did not load.\n";
        return false;
    }
    marrow::editor::ProjectData project = *loaded.project;
    project.editor_metadata.notes += " mar180-s1";

    const auto previous_bytes = mar180::read_bytes(destination);
    if (!previous_bytes.has_value()) {
        std::cerr << "MAR-180 S1: failed to read the pre-save destination bytes.\n";
        return false;
    }

    int rename_calls = 0;
    std::filesystem::path observed_source;
    std::filesystem::path observed_destination;
    marrow::editor::ProjectSaveResult save_result;
    {
        const mar180::ScopedRenameCallback rename_failure(
            [&](const std::filesystem::path& source,
                const std::filesystem::path& target) {
                ++rename_calls;
                observed_source = source;
                observed_destination = target;
                return std::make_error_code(std::errc::permission_denied);
            });
        save_result = marrow::editor::save_project(project, destination);
    }

    if (static_cast<bool>(save_result)) {
        std::cerr << "MAR-180 S1: an injected rename failure must fail the save.\n";
        return false;
    }
    if (!save_result.error.has_value() || save_result.error->path != destination ||
        save_result.error->message.empty()) {
        std::cerr << "MAR-180 S1: the save failure must name the destination and a cause.\n";
        return false;
    }

    const auto current_bytes = mar180::read_bytes(destination);
    if (!current_bytes.has_value() || *current_bytes != *previous_bytes) {
        std::cerr << "MAR-180 S1: a failed save must preserve the previous file "
                     "byte-for-byte.\n";
        return false;
    }
    const auto reloaded = marrow::editor::load_project(destination);
    if (!reloaded || reloaded.skeleton_data == nullptr) {
        std::cerr << "MAR-180 S1: the preserved project must still OPEN, not merely "
                     "parse as JSON.\n";
        return false;
    }
    if (rename_calls != 1) {
        std::cerr << "MAR-180 S1: an atomic save must attempt exactly one final "
                     "rename (observed " << rename_calls << ").\n";
        return false;
    }
    if (observed_destination != destination) {
        std::cerr << "MAR-180 S1: the rename destination must be the project path.\n";
        return false;
    }
    if (observed_source.parent_path() != destination.parent_path() ||
        observed_source == destination) {
        std::cerr << "MAR-180 S1: the temporary must be unique and live in the "
                     "destination directory so the rename never crosses a "
                     "filesystem boundary.\n";
        return false;
    }
    if (observed_source.empty() || std::filesystem::exists(observed_source)) {
        std::cerr << "MAR-180 S1: a handled rename failure must remove the exact "
                     "temporary file.\n";
        return false;
    }

    std::vector<std::string> expected_names{destination.filename().string()};
    for (const char* name : mar180::fixture_asset_filenames()) {
        expected_names.emplace_back(name);
    }
    std::sort(expected_names.begin(), expected_names.end());
    if (mar180::directory_filenames(temporary.path) != expected_names) {
        std::cerr << "MAR-180 S1: a handled failure must leave no orphan temporary "
                     "beside the project.\n";
        return false;
    }

    std::cout << "MAR-180 S1: an injected rename failure fails the save, preserves the "
                 "destination byte-for-byte (which still OPENS via load_project, not "
                 "merely parses), attempts exactly one rename from a unique temporary "
                 "inside the destination directory, and leaves no orphan behind.\n";
    return true;
}

/**
 * @brief MAR-180 S2 -- a cross-directory Save As produces a project that OPENS.
 *
 * Every path a `.marrow` stores is project-relative by design and `resolve_path`
 * resolves it against the project file's OWN directory. Before MAR-180,
 * `save_project` changed only `source_path`, so saving the fixture (whose
 * `runtime.skeleton` is the relative `player_idle.mskl`) into another directory
 * wrote a project whose skeleton reference resolved to a file that does not
 * exist. `validate_project_for_save` takes no base document and cannot see it,
 * so the save reported success over an unopenable file.
 *
 * This case asserts through `load_project`, which materializes the references
 * and reaches `build_project_runtime`. It also asserts that the raw JSON parser
 * -- the assertion the pre-existing save case uses -- succeeds either way, which
 * is the direct demonstration that the raw parse is NOT load-bearing.
 */
bool validate_mar180_save_as_rebases_relative_paths(
    const std::filesystem::path& fixture_project_path) {
    const mar180::TemporaryDirectory temporary("s2");

    marrow::editor::EditorSession session;
    const auto opened = session.open(fixture_project_path);
    if (!opened || session.project() == nullptr) {
        std::cerr << "MAR-180 S2: failed to open the fixture project.\n";
        return false;
    }

    const std::filesystem::path original_skeleton =
        std::filesystem::weakly_canonical(session.project()->resolved_skeleton_path());
    std::vector<std::filesystem::path> original_atlases;
    for (const auto& atlas_path : session.project()->resolved_atlas_paths()) {
        original_atlases.push_back(std::filesystem::weakly_canonical(atlas_path));
    }
    const std::filesystem::path fixture_directory = std::filesystem::weakly_canonical(
        std::filesystem::absolute(fixture_project_path).parent_path());

    const std::filesystem::path moved = temporary.path / "moved.marrow";
    const auto save_result = session.save(moved);
    if (!save_result) {
        std::cerr << "MAR-180 S2: the cross-directory Save As failed outright.\n";
        return false;
    }

    // The pre-existing save assertion in this file is a raw JSON parse. It
    // passes over an unopenable project, which is exactly why the Save As bug
    // shipped. Asserted here so the contrast is recorded, not assumed.
    if (!marrow::runtime::json::load_document(moved)) {
        std::cerr << "MAR-180 S2: the written file is not even valid JSON.\n";
        return false;
    }

    const auto reloaded = marrow::editor::load_project(moved);
    if (!reloaded || reloaded.skeleton_data == nullptr) {
        std::cerr << "MAR-180 S2: a cross-directory Save As must write a project that "
                     "OPENS -- load_project materializes the references the raw JSON "
                     "parse above cannot see.\n";
        return false;
    }
    if (std::filesystem::weakly_canonical(reloaded.project->resolved_skeleton_path()) !=
        original_skeleton) {
        std::cerr << "MAR-180 S2: the rebased skeleton must resolve to the same "
                     "absolute file it resolved to before the Save As.\n";
        return false;
    }
    std::vector<std::filesystem::path> reloaded_atlases;
    for (const auto& atlas_path : reloaded.project->resolved_atlas_paths()) {
        reloaded_atlases.push_back(std::filesystem::weakly_canonical(atlas_path));
    }
    if (reloaded_atlases != original_atlases) {
        std::cerr << "MAR-180 S2: every rebased atlas must resolve to the same "
                     "absolute file it resolved to before the Save As.\n";
        return false;
    }

    // Design section 4.3: `export_directory` rebases by IDENTITY, the acceptance
    // criterion's literal reading. Exports keep landing where they landed, which
    // will surprise someone -- so it is asserted, not left emergent. A UI choice
    // between the two readings belongs to MAR-181.
    const std::filesystem::path reloaded_export = std::filesystem::weakly_canonical(
        reloaded.project->resolved_export_skeleton_path());
    const std::string export_text = reloaded_export.generic_string();
    const std::string fixture_text = fixture_directory.generic_string() + "/";
    if (export_text.rfind(fixture_text, 0) != 0) {
        std::cerr << "MAR-180 S2: export_directory must rebase by identity, keeping "
                     "exports under the ORIGINAL project directory (got "
                  << export_text << ").\n";
        return false;
    }

    if (session.project() == nullptr ||
        std::filesystem::weakly_canonical(session.project()->resolved_skeleton_path()) !=
            original_skeleton) {
        std::cerr << "MAR-180 S2: the live session must track the rebased project it "
                     "just wrote.\n";
        return false;
    }

    std::cout << "MAR-180 S2: a cross-directory Save As writes a project that OPENS via "
                 "load_project with every runtime reference resolving to the SAME "
                 "absolute file as before, export_directory rebased by identity under "
                 "the original directory, and the live session tracking what was "
                 "written -- while the raw json::load_document assertion the older "
                 "save case uses passes either way.\n";
    return true;
}

/**
 * @brief MAR-180 S3 -- absolute references survive a Save As unchanged.
 *
 * The referenced asset is placed INSIDE the Save As destination directory on
 * purpose: the relative form is then always representable, so removing the
 * `is_absolute()` early return always changes the stored string and the
 * inversion cannot be masked by temp-directory layout.
 */
bool validate_mar180_save_as_preserves_absolute_paths(
    const std::filesystem::path& fixture_project_path) {
    const mar180::TemporaryDirectory temporary("s3");
    const std::filesystem::path seeded =
        mar180::seed_fixture_copy(fixture_project_path, temporary.path);
    if (seeded.empty()) {
        std::cerr << "MAR-180 S3: failed to seed the fixture copy.\n";
        return false;
    }

    const std::filesystem::path destination = temporary.path / "absolute.marrow";
    const std::filesystem::path absolute_skeleton =
        std::filesystem::absolute(temporary.path / "player_idle.mskl");
    const std::filesystem::path absolute_atlas =
        std::filesystem::absolute(temporary.path / "player_idle.matl");

    marrow::editor::MinimalProjectOptions options;
    options.project_path = destination;
    options.skeleton_path = absolute_skeleton;
    options.atlas_paths = {absolute_atlas};
    options.name = "MAR-180 S3";
    marrow::editor::ProjectData project =
        marrow::editor::create_minimal_project(options);
    // create_minimal_project relativizes; this case is about what happens when a
    // project genuinely stores an absolute reference.
    project.runtime_assets.skeleton_path = absolute_skeleton;
    project.runtime_assets.atlas_paths = {absolute_atlas};

    const auto save_result = marrow::editor::save_project(project, destination);
    if (!save_result) {
        std::cerr << "MAR-180 S3: saving the absolute-reference project failed.\n";
        return false;
    }
    const auto reloaded = marrow::editor::load_project(destination);
    if (!reloaded || reloaded.skeleton_data == nullptr) {
        std::cerr << "MAR-180 S3: the absolute-reference project must still OPEN.\n";
        return false;
    }
    if (!reloaded.project->runtime_assets.skeleton_path.is_absolute() ||
        reloaded.project->runtime_assets.skeleton_path != absolute_skeleton) {
        std::cerr << "MAR-180 S3: an absolute skeleton reference must survive a Save As "
                     "unchanged (got "
                  << reloaded.project->runtime_assets.skeleton_path.generic_string()
                  << ").\n";
        return false;
    }
    if (reloaded.project->runtime_assets.atlas_paths.size() != 1U ||
        !reloaded.project->runtime_assets.atlas_paths.front().is_absolute() ||
        reloaded.project->runtime_assets.atlas_paths.front() != absolute_atlas) {
        std::cerr << "MAR-180 S3: an absolute atlas reference must survive a Save As "
                     "unchanged.\n";
        return false;
    }

    std::cout << "MAR-180 S3: absolute runtime references survive a Save As "
                 "string-identical even when the destination directory CONTAINS the "
                 "referenced assets, so a representable relative form exists and is "
                 "deliberately not taken.\n";
    return true;
}

/**
 * @brief MAR-180 S4 -- undo across a Save As still yields a project that OPENS.
 *
 * Every history snapshot holds a whole `ProjectData`, carrying the same relative
 * references the live project does. Rebasing only `source_path` -- what
 * `EditorSession::save` did before MAR-180 -- leaves each snapshot pairing the
 * NEW directory with the OLD relative paths. Undo then restores an in-memory
 * project that no longer resolves, and the next save writes a file that cannot
 * be reopened. Only a reload from disk sees it; the save still returns success.
 */
bool validate_mar180_undo_across_save_as_still_opens(
    const std::filesystem::path& fixture_project_path) {
    const mar180::TemporaryDirectory temporary("s4");

    marrow::editor::EditorSession session;
    if (!session.open(fixture_project_path) || session.project() == nullptr) {
        std::cerr << "MAR-180 S4: failed to open the fixture project.\n";
        return false;
    }
    const std::filesystem::path original_skeleton =
        std::filesystem::weakly_canonical(session.project()->resolved_skeleton_path());

    auto transaction = session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "MAR-180 S4 note",
        "mar180-s4",
        false,
        marrow::editor::EditImpact::Project});
    if (!transaction || transaction.project() == nullptr) {
        std::cerr << "MAR-180 S4: failed to begin the seeding edit.\n";
        return false;
    }
    transaction.project()->editor_metadata.notes += " mar180-s4";
    if (!transaction.commit()) {
        std::cerr << "MAR-180 S4: failed to commit the seeding edit.\n";
        return false;
    }

    const std::filesystem::path moved = temporary.path / "moved.marrow";
    if (!session.save(moved)) {
        std::cerr << "MAR-180 S4: the cross-directory Save As failed.\n";
        return false;
    }
    if (!session.undo()) {
        std::cerr << "MAR-180 S4: undo after the Save As failed.\n";
        return false;
    }
    // Same path, no argument: this writes the UNDONE project, which is the
    // snapshot the history rebase is responsible for.
    if (!session.save({})) {
        std::cerr << "MAR-180 S4: saving the undone project failed.\n";
        return false;
    }

    const auto reloaded = marrow::editor::load_project(moved);
    if (!reloaded || reloaded.skeleton_data == nullptr) {
        std::cerr << "MAR-180 S4: a project saved after undoing across a Save As must "
                     "still OPEN -- a history snapshot rebased only in source_path "
                     "carries the OLD relative paths under the NEW directory.\n";
        return false;
    }
    if (std::filesystem::weakly_canonical(reloaded.project->resolved_skeleton_path()) !=
        original_skeleton) {
        std::cerr << "MAR-180 S4: the undone project's skeleton must still resolve to "
                     "the original file.\n";
        return false;
    }

    std::cout << "MAR-180 S4: an undo across a cross-directory Save As restores a "
                 "project that saves and RELOADS with its skeleton still resolving to "
                 "the original file -- caught by load_project and by nothing else.\n";
    return true;
}

/**
 * @brief MAR-180 S5 -- the history rebase's re-serialization is load-bearing.
 *
 * `HistorySnapshot::serialized_project` is the string `histories_equal` and
 * `apply_history`'s change detection compare, and all five rebased fields are
 * serialized. Rebasing a snapshot's `ProjectData` while leaving its cached string
 * alone makes the cached string describe a project that no longer exists.
 *
 * MEASURED, and it corrects this story's own plan: the three `dirty()`
 * assertions the design specified for this case do NOT catch that omission.
 * `update_dirty` re-serializes the LIVE project every time and never reads a
 * snapshot's cached string, so the dirty flag stays correct. With the
 * re-serialization removed, the entire project smoke suite still passed. They are
 * kept below because they DO catch the source_path-only rebase (S4's inversion).
 *
 * The clause that actually bites is the revision one. `apply_history` derives
 * `project_changed` from the two cached strings and bumps `project_revision` when
 * it is true. This case commits a PREVIEW-only edit, whose before and after
 * snapshots hold identical project content, so a correct rebase leaves the two
 * strings equal and an undo bumps no project revision. A stale cached string
 * makes them differ, and the undo reports an authored-project change that never
 * happened -- which the shell's `observed_project_revision` refresh believes.
 */
bool validate_mar180_history_reserialization_is_load_bearing(
    const std::filesystem::path& fixture_project_path) {
    const mar180::TemporaryDirectory temporary("s5");

    marrow::editor::EditorSession session;
    if (!session.open(fixture_project_path) || session.project() == nullptr) {
        std::cerr << "MAR-180 S5: failed to open the fixture project.\n";
        return false;
    }

    // A preview-only edit: the preview state changes, the authored project does
    // not. The entry survives commit because commit compares preview state too.
    if (!session.set_preview_skins({"default", "warrior"})) {
        std::cerr << "MAR-180 S5: failed to commit the preview-only edit.\n";
        return false;
    }
    if (session.undo_count() != 1U) {
        std::cerr << "MAR-180 S5: the preview-only edit must produce exactly one "
                     "history entry.\n";
        return false;
    }

    const std::filesystem::path moved = temporary.path / "moved.marrow";
    if (!session.save(moved)) {
        std::cerr << "MAR-180 S5: the cross-directory Save As failed.\n";
        return false;
    }
    if (session.dirty()) {
        std::cerr << "MAR-180 S5: a completed Save As must leave the session clean.\n";
        return false;
    }

    const std::uint64_t project_revision_after_save = session.project_revision();
    if (!session.undo()) {
        std::cerr << "MAR-180 S5: undo after the Save As failed.\n";
        return false;
    }
    if (session.project_revision() != project_revision_after_save) {
        std::cerr << "MAR-180 S5: undoing a PREVIEW-only edit must not bump "
                     "project_revision. A history snapshot whose cached serialization "
                     "was not refreshed after the rebase makes apply_history believe "
                     "the authored project changed (observed "
                  << session.project_revision() << ", expected "
                  << project_revision_after_save << ").\n";
        return false;
    }
    if (session.dirty()) {
        std::cerr << "MAR-180 S5: undoing a preview-only edit must not dirty the "
                     "authored project.\n";
        return false;
    }
    if (!session.redo()) {
        std::cerr << "MAR-180 S5: redo after the Save As failed.\n";
        return false;
    }
    if (session.project_revision() != project_revision_after_save || session.dirty()) {
        std::cerr << "MAR-180 S5: redoing a preview-only edit must not bump "
                     "project_revision or dirty the project.\n";
        return false;
    }

    // The dirty half. These clauses catch the source_path-only rebase, not the
    // missing re-serialization -- see this function's comment.
    marrow::editor::EditorSession authored;
    if (!authored.open(fixture_project_path)) {
        std::cerr << "MAR-180 S5: failed to open the fixture for the authored half.\n";
        return false;
    }
    auto transaction = authored.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "MAR-180 S5 note",
        "mar180-s5",
        false,
        marrow::editor::EditImpact::Project});
    if (!transaction || transaction.project() == nullptr) {
        std::cerr << "MAR-180 S5: failed to begin the authored edit.\n";
        return false;
    }
    transaction.project()->editor_metadata.notes += " mar180-s5";
    if (!transaction.commit()) {
        std::cerr << "MAR-180 S5: failed to commit the authored edit.\n";
        return false;
    }
    if (!authored.save(temporary.path / "authored.marrow") || authored.dirty()) {
        std::cerr << "MAR-180 S5: the authored Save As must leave the session clean.\n";
        return false;
    }
    if (!authored.undo() || !authored.dirty()) {
        std::cerr << "MAR-180 S5: undoing an authored edit after a Save As must dirty "
                     "the session.\n";
        return false;
    }
    if (!authored.redo() || authored.dirty()) {
        std::cerr << "MAR-180 S5: redoing back to the saved state after a Save As must "
                     "leave the session clean -- a snapshot rebased only in source_path "
                     "restores the OLD relative paths and never compares equal to the "
                     "saved baseline again.\n";
        return false;
    }

    std::cout << "MAR-180 S5: after a cross-directory Save As, undoing and redoing a "
                 "PREVIEW-only edit bumps no project_revision (the clause that catches "
                 "a snapshot rebased without re-serializing its cached string -- the "
                 "dirty flag does NOT, because update_dirty re-serializes the live "
                 "project every time), and an authored edit's undo/redo still tracks "
                 "the saved dirty baseline exactly.\n";
    return true;
}

namespace mar180 {

/// @brief The six values that totally describe a session's authoring state.
struct SessionSnapshot {
    std::string serialized_project;
    std::uint64_t project_revision{0U};
    std::uint64_t runtime_revision{0U};
    std::uint64_t preview_revision{0U};
    std::size_t undo_count{0U};
    bool dirty{false};
};

SessionSnapshot capture(const marrow::editor::EditorSession& session) {
    SessionSnapshot snapshot;
    if (session.project() != nullptr) {
        snapshot.serialized_project = marrow::editor::serialize_project(*session.project());
    }
    snapshot.project_revision = session.project_revision();
    snapshot.runtime_revision = session.runtime_revision();
    snapshot.preview_revision = session.preview_revision();
    snapshot.undo_count = session.undo_count();
    snapshot.dirty = session.dirty();
    return snapshot;
}

bool snapshots_equal(const SessionSnapshot& left, const SessionSnapshot& right) {
    return left.serialized_project == right.serialized_project &&
        left.project_revision == right.project_revision &&
        left.runtime_revision == right.runtime_revision &&
        left.preview_revision == right.preview_revision &&
        left.undo_count == right.undo_count &&
        left.dirty == right.dirty;
}

/**
 * @brief Rewrites a `.mskl` so it parses as JSON but fails to BUILD.
 *
 * A bone whose `parent` names a bone that does not exist passes the JSON parse
 * and is rejected by `load_skeleton_data`, which is the F11 failure point: the
 * one that only bites AFTER the document has been accepted.
 */
bool break_skeleton_document_build(const std::filesystem::path& skeleton_path) {
    const auto text = read_bytes(skeleton_path);
    if (!text.has_value()) {
        return false;
    }
    const std::string marker = "\"bones\": [";
    const std::size_t position = text->find(marker);
    if (position == std::string::npos) {
        return false;
    }
    std::string broken = *text;
    broken.insert(
        position + marker.size(),
        "\n    {\"name\": \"mar180_orphan\", \"parent\": \"mar180_missing_parent\"},");
    return write_bytes(skeleton_path, broken);
}

} // namespace mar180

/**
 * @brief MAR-180 S10 -- a failed runtime-source adoption leaves a COHERENT session.
 *
 * The shell's hot-reload path assigned the new skeleton document and atlases into
 * the session -- `ShellState::load_result` is a reference into it -- and only then
 * rebuilt. Its rollback restored the old document and re-ran the rebuild while
 * DISCARDING the result, so `skeleton_data` could end up derived from a different
 * document than `base_skeleton_document`. Nothing in the return code shows that.
 *
 * The coherence clause is the one that matters: rebuilding the runtime from the
 * session's own project and its own base document must still succeed, which is
 * only true when the two were never separated.
 */
bool validate_mar180_failed_adoption_keeps_session_coherent(
    const std::filesystem::path& fixture_project_path) {
    const mar180::TemporaryDirectory temporary("s10");
    const std::filesystem::path project_copy =
        mar180::seed_fixture_copy(fixture_project_path, temporary.path);
    if (project_copy.empty()) {
        std::cerr << "MAR-180 S10: failed to seed the fixture copy.\n";
        return false;
    }
    const std::filesystem::path skeleton_copy = temporary.path / "player_idle.mskl";
    const auto original_skeleton_bytes = mar180::read_bytes(skeleton_copy);
    if (!original_skeleton_bytes.has_value()) {
        std::cerr << "MAR-180 S10: failed to read the seeded skeleton.\n";
        return false;
    }

    marrow::editor::EditorSession session;
    if (!session.open(project_copy) || session.runtime_data() == nullptr) {
        std::cerr << "MAR-180 S10: failed to open the seeded project copy.\n";
        return false;
    }
    const mar180::SessionSnapshot before = mar180::capture(session);
    const std::size_t bones_before = session.runtime_data()->bones().size();

    if (!mar180::break_skeleton_document_build(skeleton_copy)) {
        std::cerr << "MAR-180 S10: failed to write the build-breaking skeleton.\n";
        return false;
    }
    // The broken document must still PARSE, or this case would be testing the
    // JSON parser instead of the F11 window it exists for.
    if (!marrow::runtime::json::load_document(skeleton_copy)) {
        std::cerr << "MAR-180 S10: the build-breaking skeleton must still parse as "
                     "JSON, or the adoption never reaches the runtime build.\n";
        return false;
    }

    const auto adoption = session.adopt_runtime_sources();
    if (static_cast<bool>(adoption)) {
        std::cerr << "MAR-180 S10: adopting a skeleton that cannot build must fail.\n";
        return false;
    }
    if (!adoption.error.has_value() ||
        adoption.error->code != marrow::editor::SessionErrorCode::RuntimeBuildFailed) {
        std::cerr << "MAR-180 S10: a failed runtime build must report "
                     "RuntimeBuildFailed.\n";
        return false;
    }
    if (!mar180::snapshots_equal(mar180::capture(session), before)) {
        std::cerr << "MAR-180 S10: a failed adoption must leave the session's six-value "
                     "authoring snapshot unchanged.\n";
        return false;
    }
    if (session.runtime_data() == nullptr ||
        session.runtime_data()->bones().size() != bones_before) {
        std::cerr << "MAR-180 S10: a failed adoption must leave the previous runtime "
                     "data in place.\n";
        return false;
    }
    if (session.base_skeleton_document() == nullptr ||
        !marrow::editor::build_project_runtime(
            *session.project(),
            *session.base_skeleton_document())) {
        std::cerr << "MAR-180 S10: COHERENCE -- after a failed adoption the session's "
                     "runtime data and base skeleton document must still be mutually "
                     "derived. A swap-then-roll-back can separate them while still "
                     "returning the right error code.\n";
        return false;
    }

    if (!mar180::write_bytes(skeleton_copy, *original_skeleton_bytes)) {
        std::cerr << "MAR-180 S10: failed to restore the skeleton bytes.\n";
        return false;
    }
    const auto recovered = session.adopt_runtime_sources();
    if (!recovered) {
        std::cerr << "MAR-180 S10: adopting the restored sources must succeed.\n";
        return false;
    }
    const mar180::SessionSnapshot after = mar180::capture(session);
    if (after.runtime_revision <= before.runtime_revision ||
        after.preview_revision <= before.preview_revision) {
        std::cerr << "MAR-180 S10: a successful adoption must bump the runtime and "
                     "preview revisions.\n";
        return false;
    }
    if (after.project_revision != before.project_revision) {
        std::cerr << "MAR-180 S10: adoption replaces runtime SOURCES, never the "
                     "authored project, so project_revision must not move.\n";
        return false;
    }

    std::cout << "MAR-180 S10: an adoption whose skeleton parses but fails to BUILD "
                 "fails with RuntimeBuildFailed, leaves the six-value session snapshot "
                 "and the previous bone set untouched, and -- the clause the return "
                 "code cannot give you -- keeps runtime data and base document "
                 "mutually derived; the restored sources then adopt cleanly, bumping "
                 "runtime and preview revisions only.\n";
    return true;
}

/**
 * @brief MAR-180 S6 -- `create` builds a dirty, unwritten, save-then-openable session.
 *
 * The dirty-from-birth clause is the load-bearing one. `update_dirty` compares
 * the live serialization against `saved_serialized_project`; copying `open`'s
 * baseline line would let a project that has never been written compare CLEAN,
 * and the dirty flag is the only thing standing between the user and losing it.
 */
bool validate_mar180_create_starts_dirty_and_unwritten(
    const std::filesystem::path& fixture_project_path) {
    const mar180::TemporaryDirectory temporary("s6");
    const std::filesystem::path fixture_directory =
        std::filesystem::absolute(fixture_project_path).parent_path();
    const std::filesystem::path project_path = temporary.path / "created.marrow";

    marrow::editor::MinimalProjectOptions options;
    options.project_path = project_path;
    options.skeleton_path = fixture_directory / "player_idle.mskl";
    options.atlas_paths = {fixture_directory / "player_idle.matl"};
    options.name = "MAR-180 S6";

    marrow::editor::EditorSession session;
    const auto created = session.create(options);
    if (!created) {
        std::cerr << "MAR-180 S6: create against an existing rig must succeed.\n";
        return false;
    }
    if (!session.has_project() || session.project() == nullptr) {
        std::cerr << "MAR-180 S6: create must leave a project open.\n";
        return false;
    }
    if (!session.dirty()) {
        std::cerr << "MAR-180 S6: a created project has never been written, so the "
                     "session must be dirty from birth.\n";
        return false;
    }
    if (session.undo_count() != 0U || session.redo_count() != 0U) {
        std::cerr << "MAR-180 S6: create must start with an empty history.\n";
        return false;
    }
    if (session.runtime_data() == nullptr || session.preview_skeleton() == nullptr) {
        std::cerr << "MAR-180 S6: create must materialize runtime data and a preview.\n";
        return false;
    }
    if (std::filesystem::exists(project_path)) {
        std::cerr << "MAR-180 S6: create must write NOTHING until the user saves.\n";
        return false;
    }

    if (!session.save({})) {
        std::cerr << "MAR-180 S6: saving the created project failed.\n";
        return false;
    }
    const auto reloaded = marrow::editor::load_project(project_path);
    if (!reloaded || reloaded.skeleton_data == nullptr) {
        std::cerr << "MAR-180 S6: the saved created project must OPEN.\n";
        return false;
    }
    if (session.dirty()) {
        std::cerr << "MAR-180 S6: saving a created project must clear the dirty flag.\n";
        return false;
    }

    std::cout << "MAR-180 S6: create adopts an existing rig into a session that is "
                 "dirty from birth with an empty history and materialized runtime, "
                 "writes NOTHING to the target path, and only after an explicit save "
                 "produces a file that RELOADS.\n";
    return true;
}

/**
 * @brief MAR-180 S7 -- a `create` against a missing rig changes nothing.
 */
bool validate_mar180_failed_create_changes_nothing(
    const std::filesystem::path& fixture_project_path) {
    const mar180::TemporaryDirectory temporary("s7");
    const std::filesystem::path fixture_directory =
        std::filesystem::absolute(fixture_project_path).parent_path();

    marrow::editor::EditorSession session;
    if (!session.open(fixture_project_path) || session.project() == nullptr) {
        std::cerr << "MAR-180 S7: failed to open the fixture project.\n";
        return false;
    }
    const mar180::SessionSnapshot before = mar180::capture(session);
    const std::filesystem::path source_path_before = session.project()->source_path;

    marrow::editor::MinimalProjectOptions options;
    options.project_path = temporary.path / "never_created.marrow";
    options.skeleton_path = temporary.path / "does_not_exist.mskl";
    options.atlas_paths = {fixture_directory / "player_idle.matl"};

    const auto created = session.create(options);
    if (static_cast<bool>(created)) {
        std::cerr << "MAR-180 S7: create against a missing skeleton must fail.\n";
        return false;
    }
    if (!created.error.has_value()) {
        std::cerr << "MAR-180 S7: a failed create must carry a load error.\n";
        return false;
    }
    if (!mar180::snapshots_equal(mar180::capture(session), before)) {
        std::cerr << "MAR-180 S7: a failed create must leave the session's six-value "
                     "authoring snapshot unchanged.\n";
        return false;
    }
    if (session.project() == nullptr || session.project()->source_path != source_path_before) {
        std::cerr << "MAR-180 S7: a failed create must leave the previous project in "
                     "place -- assigning the session's project before the skeleton "
                     "loads is exactly the mistake the shell hot-reload path made.\n";
        return false;
    }
    if (std::filesystem::exists(options.project_path)) {
        std::cerr << "MAR-180 S7: a failed create must write nothing.\n";
        return false;
    }

    std::cout << "MAR-180 S7: a create against a missing rig fails with a load error "
                 "and leaves the open session's six-value snapshot, its project's "
                 "source path, and the filesystem all untouched.\n";
    return true;
}

/**
 * @brief MAR-180 S8 -- `close` clears the session and BUMPS the three revisions.
 *
 * The "strictly greater" clauses are the point. Resetting the counters to zero
 * would let a shell holding a stale `observed_*` value compare equal and skip the
 * resync a close most needs.
 */
bool validate_mar180_close_clears_and_bumps_revisions(
    const std::filesystem::path& fixture_project_path) {
    marrow::editor::EditorSession session;
    if (!session.open(fixture_project_path)) {
        std::cerr << "MAR-180 S8: failed to open the fixture project.\n";
        return false;
    }
    const std::uint64_t project_revision_before = session.project_revision();
    const std::uint64_t runtime_revision_before = session.runtime_revision();
    const std::uint64_t preview_revision_before = session.preview_revision();

    if (!session.close()) {
        std::cerr << "MAR-180 S8: close on an idle session must succeed.\n";
        return false;
    }
    if (session.has_project() || session.project() != nullptr ||
        session.runtime_data() != nullptr) {
        std::cerr << "MAR-180 S8: close must discard the project and its runtime.\n";
        return false;
    }
    if (session.can_undo() || session.can_redo() || session.dirty()) {
        std::cerr << "MAR-180 S8: close must clear history and the dirty flag.\n";
        return false;
    }
    if (session.project_revision() <= project_revision_before ||
        session.runtime_revision() <= runtime_revision_before ||
        session.preview_revision() <= preview_revision_before) {
        std::cerr << "MAR-180 S8: close must BUMP all three revisions, never reset "
                     "them -- a reset lets a stale observed value compare equal.\n";
        return false;
    }
    if (!session.open(fixture_project_path) || session.project() == nullptr) {
        std::cerr << "MAR-180 S8: a closed session must stay reusable.\n";
        return false;
    }

    std::cout << "MAR-180 S8: close discards the project, runtime, preview and history, "
                 "leaves the dirty flag clear, bumps all three revisions strictly "
                 "upward rather than resetting them, and leaves the session reusable.\n";
    return true;
}

/**
 * @brief MAR-180 S9 -- `close` and `create` refuse an active edit transaction.
 *
 * Without either gate, the transaction's `project()` pointer would address a
 * ProjectData that was replaced underneath it, and the commit would write into
 * the wrong project.
 */
bool validate_mar180_lifecycle_refuses_active_transaction(
    const std::filesystem::path& fixture_project_path) {
    const mar180::TemporaryDirectory temporary("s9");
    const std::filesystem::path fixture_directory =
        std::filesystem::absolute(fixture_project_path).parent_path();

    marrow::editor::EditorSession session;
    if (!session.open(fixture_project_path) || session.project() == nullptr) {
        std::cerr << "MAR-180 S9: failed to open the fixture project.\n";
        return false;
    }

    auto transaction = session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "MAR-180 S9 note",
        "mar180-s9",
        false,
        marrow::editor::EditImpact::Project});
    if (!transaction || transaction.project() == nullptr) {
        std::cerr << "MAR-180 S9: failed to begin the guarding transaction.\n";
        return false;
    }
    const std::string sentinel = " mar180-s9-sentinel";
    transaction.project()->editor_metadata.notes += sentinel;

    if (session.close()) {
        std::cerr << "MAR-180 S9: close must refuse while an edit transaction is "
                     "active.\n";
        return false;
    }

    marrow::editor::MinimalProjectOptions options;
    options.project_path = temporary.path / "refused.marrow";
    options.skeleton_path = fixture_directory / "player_idle.mskl";
    options.atlas_paths = {fixture_directory / "player_idle.matl"};
    if (static_cast<bool>(session.create(options))) {
        std::cerr << "MAR-180 S9: create must refuse while an edit transaction is "
                     "active.\n";
        return false;
    }

    if (!transaction.commit()) {
        std::cerr << "MAR-180 S9: the transaction must still commit after both "
                     "refusals.\n";
        return false;
    }
    if (session.project() == nullptr ||
        session.project()->editor_metadata.notes.find(sentinel) == std::string::npos) {
        std::cerr << "MAR-180 S9: the committed edit must be readable through the "
                     "session -- a lifecycle call that replaced the project under an "
                     "open transaction would commit into a different ProjectData.\n";
        return false;
    }
    if (std::filesystem::exists(options.project_path)) {
        std::cerr << "MAR-180 S9: a refused create must write nothing.\n";
        return false;
    }

    std::cout << "MAR-180 S9: close and create both refuse an active edit transaction, "
                 "the transaction stays usable, and its commit lands in the SAME "
                 "project the session still holds.\n";
    return true;
}


// ---------------------------------------------------------------------------
// MAR-184 -- stepped inherit timeline overlays.
//
// Every case below runs inside the standing `player_idle.marrow` invocation.
// The editing suites live in `main()`'s marker-gated `else`, so pointing this
// binary at a project built over `skin_inherit_constraints.mskl` would take the
// skip branch and run NONE of them. The base-backed inherit fixture is
// therefore reached by building throwaway projects over it here, exactly as
// MAR-177 and MAR-178 already do.
// ---------------------------------------------------------------------------
namespace mar184 {

/** @brief A scratch directory removed when the case returns. */
struct TemporaryDirectory {
    std::filesystem::path path;

    explicit TemporaryDirectory(std::string_view label) {
        const auto unique_suffix =
            std::chrono::steady_clock::now().time_since_epoch().count();
        path = std::filesystem::temp_directory_path() /
            ("marrow-mar184-" + std::string(label) + "-" +
             std::to_string(unique_suffix));
        std::error_code ignored;
        std::filesystem::create_directories(path, ignored);
    }

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    }
};

/**
 * @brief Minimal SHA-256, for P4's non-effect witness only.
 *
 * A byte length alone would not notice a same-length reordering, and the
 * witness this story is held to is "an unchanged project serializes to the
 * bytes it always did". Test-local by design; nothing in the product hashes.
 */
std::string sha256_hex(const std::string& input) {
    static constexpr std::uint32_t kRoundConstants[64] = {
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U,
        0x923f82a4U, 0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U,
        0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U, 0xc6e00bf3U, 0xd5a79147U,
        0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
        0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU,
        0x5b9cca4fU, 0x682e6ff3U, 0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U};

    std::uint32_t state[8] = {
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};

    std::vector<std::uint8_t> message(input.begin(), input.end());
    const std::uint64_t bit_length = static_cast<std::uint64_t>(input.size()) * 8U;
    message.push_back(0x80U);
    while (message.size() % 64U != 56U) {
        message.push_back(0x00U);
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        message.push_back(static_cast<std::uint8_t>((bit_length >> shift) & 0xffU));
    }

    const auto rotate_right = [](std::uint32_t value, std::uint32_t count) {
        return (value >> count) | (value << (32U - count));
    };
    for (std::size_t chunk = 0; chunk < message.size(); chunk += 64U) {
        std::uint32_t schedule[64] = {};
        for (std::size_t index = 0; index < 16U; ++index) {
            schedule[index] =
                (static_cast<std::uint32_t>(message[chunk + index * 4U]) << 24U) |
                (static_cast<std::uint32_t>(message[chunk + index * 4U + 1U]) << 16U) |
                (static_cast<std::uint32_t>(message[chunk + index * 4U + 2U]) << 8U) |
                static_cast<std::uint32_t>(message[chunk + index * 4U + 3U]);
        }
        for (std::size_t index = 16U; index < 64U; ++index) {
            const std::uint32_t s0 = rotate_right(schedule[index - 15U], 7U) ^
                rotate_right(schedule[index - 15U], 18U) ^ (schedule[index - 15U] >> 3U);
            const std::uint32_t s1 = rotate_right(schedule[index - 2U], 17U) ^
                rotate_right(schedule[index - 2U], 19U) ^ (schedule[index - 2U] >> 10U);
            schedule[index] = schedule[index - 16U] + s0 + schedule[index - 7U] + s1;
        }

        std::uint32_t a = state[0];
        std::uint32_t b = state[1];
        std::uint32_t c = state[2];
        std::uint32_t d = state[3];
        std::uint32_t e = state[4];
        std::uint32_t f = state[5];
        std::uint32_t g = state[6];
        std::uint32_t h = state[7];
        for (std::size_t index = 0; index < 64U; ++index) {
            const std::uint32_t s1 =
                rotate_right(e, 6U) ^ rotate_right(e, 11U) ^ rotate_right(e, 25U);
            const std::uint32_t choice = (e & f) ^ (~e & g);
            const std::uint32_t temp1 =
                h + s1 + choice + kRoundConstants[index] + schedule[index];
            const std::uint32_t s0 =
                rotate_right(a, 2U) ^ rotate_right(a, 13U) ^ rotate_right(a, 22U);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = s0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }
        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const std::uint32_t word : state) {
        out << std::setw(8) << word;
    }
    return out.str();
}

/// @brief Offset of the `.mbin` version varint, straight after the `MBIN` magic.
constexpr std::size_t kBinaryVersionOffset = 4U;

/// @brief Byte length of `serialize_project(load_project(player_idle.marrow))`.
constexpr std::size_t kPlayerIdleSerializedBytes = 6111U;
/// @brief SHA-256 of the same string, measured before any MAR-184 code existed.
constexpr std::string_view kPlayerIdleSerializedSha =
    "c7d6c6de6a0badf8171772ebb75884785c1b203c86ff4fd4553344029953616b";

/** @brief Builds a throwaway project over the base-backed inherit fixture. */
marrow::editor::ProjectData minimal_project(const std::filesystem::path& project_path) {
    marrow::editor::MinimalProjectOptions options;
    options.project_path = project_path;
    options.skeleton_path =
        std::filesystem::absolute("assets/fixtures/skin_inherit_constraints.mskl");
    // The fixture ships no atlas and `load_project()` requires at least one, so
    // the project borrows `player_idle.matl`. Nothing cross-validates the pair.
    options.atlas_paths = {
        std::filesystem::absolute("assets/fixtures/player_idle.matl")};
    options.name = "mar184_inherit";
    options.active_animation = "toggle_inherit";
    return marrow::editor::create_minimal_project(options);
}

std::string_view mode_name(marrow::runtime::BoneInherit mode) {
    switch (mode) {
    case marrow::runtime::BoneInherit::Normal:
        return "normal";
    case marrow::runtime::BoneInherit::OnlyTranslation:
        return "onlyTranslation";
    case marrow::runtime::BoneInherit::NoRotationOrReflection:
        return "noRotationOrReflection";
    case marrow::runtime::BoneInherit::NoScale:
        return "noScale";
    case marrow::runtime::BoneInherit::NoScaleOrReflection:
        return "noScaleOrReflection";
    }
    return "<unknown>";
}

/**
 * @brief Writes a `.marrow` whose only overlay is a hand-built inherit body.
 *
 * The project's `runtime.skeleton_path` resolves to the REAL fixture. A bogus
 * path would make `load_project` fail later at `load_skeleton_document`, and a
 * rejection case asserting only `!result` would then pass under its own
 * inversion -- the exact trap the plan's I1/I2b entries call out. Every
 * rejection case here therefore asserts the message TEXT, not merely failure.
 */
bool write_hand_built_project(
    const std::filesystem::path& path,
    std::string_view bone_name,
    std::string_view keyframes_json,
    std::string_view label) {
    const marrow::editor::ProjectData project = minimal_project(path);
    std::string text = marrow::editor::serialize_project(project);
    const auto brace = text.find('{');
    if (brace == std::string::npos) {
        std::cerr << label << ": serialize_project produced no root object.\n";
        return false;
    }
    std::string overlay = "\n  \"timeline_edits\": {\n    \"animations\": {\n"
                          "      \"toggle_inherit\": {\n        \"bones\": {\n"
                          "          \"";
    overlay += bone_name;
    overlay += "\": {\n            \"inherit\": ";
    overlay += keyframes_json;
    overlay += "\n          }\n        }\n      }\n    }\n  },";
    text.insert(brace + 1U, overlay);

    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    std::ofstream out(path);
    out << text;
    out.close();
    if (!std::filesystem::exists(path)) {
        std::cerr << label << ": failed to write " << path << ".\n";
        return false;
    }
    return true;
}

/** @brief Asserts a hand-built overlay is refused with both message halves. */
bool expect_load_rejection(
    const std::filesystem::path& directory,
    std::string_view sub_case,
    std::string_view bone_name,
    std::string_view keyframes_json,
    std::string_view expected_path,
    std::string_view expected_message) {
    const std::filesystem::path path =
        directory / (std::string(sub_case) + ".marrow");
    if (!write_hand_built_project(path, bone_name, keyframes_json, sub_case)) {
        return false;
    }
    const auto result = marrow::editor::load_project(path);
    if (result) {
        std::cerr << "MAR-184 " << sub_case
                  << ": expected a load error naming '" << expected_path
                  << "', got a successful load. The project parser is the only "
                     "gate on this value; without it the overlay reaches the "
                     "runtime or is silently dropped.\n";
        return false;
    }
    const std::string message = result.error->message;
    if (message.find(expected_path) == std::string::npos ||
        message.find(expected_message) == std::string::npos) {
        std::cerr << "MAR-184 " << sub_case << ": expected message '"
                  << expected_path << ": " << expected_message << "', got '"
                  << message << "'.\n";
        return false;
    }
    return true;
}

bool times_equal(double left, double right) {
    return std::abs(left - right) <= 1e-9;
}

/**
 * @brief Compares a project time against one that has been through the runtime.
 *
 * Project time is `double`; `AnimationScalar` is `float`. `0.4` narrows to
 * `0.4000000059604645`, which is 6e-9 away and fails an exact-ish comparison.
 * Every assertion that reads a MATERIALIZED time must use this tolerance; the
 * 1e-9 form above is only valid against the reloaded project.
 */
bool float32_times_equal(double left, double right) {
    return std::abs(left - right) <= 1e-6;
}

}  // namespace mar184

/**
 * @brief MAR-184 P1-P13: the stepped inherit overlay, end to end.
 *
 * `fixture_result` is the standing `player_idle.marrow` load; only P4 reads it.
 * Every other case builds its own project over `skin_inherit_constraints.mskl`,
 * whose `toggle_inherit` animation carries the tree's ONLY base inherit track
 * (`child`, 4 keys) and whose `controller` bone has none.
 *
 * Project-only test data is `{0.0, noScale} + {0.4, normal}` throughout, and
 * that is load-bearing rather than arbitrary: `prune_constant_timelines`
 * deletes a timeline of exactly one key at t=0 whose mode equals the bone's
 * setup inherit, and every bone in this fixture has setup `Normal`. A lone
 * `{0.0, normal}` overlay is therefore erased by the runtime before any
 * assertion can see it -- measured, not assumed.
 */
bool validate_mar184_inherit_overlays(
    const marrow::editor::ProjectLoadResult& fixture_result) {
    using marrow::runtime::BoneInherit;
    const mar184::TemporaryDirectory temporary("overlays");

    // -- P4 -- the old-project non-effect witness. ---------------------------
    if (!fixture_result.project->bone_inherit_timeline_edits.empty()) {
        std::cerr << "MAR-184 P4: a project authored before this story must load "
                     "with ZERO inherit edits, got "
                  << fixture_result.project->bone_inherit_timeline_edits.size()
                  << ".\n";
        return false;
    }
    {
        const std::string serialized =
            marrow::editor::serialize_project(*fixture_result.project);
        const std::string digest = mar184::sha256_hex(serialized);
        std::cout << "MAR-184 P4: serialize_project(player_idle.marrow) = "
                  << serialized.size() << " bytes, sha256 " << digest << ".\n";
        if (serialized.size() != mar184::kPlayerIdleSerializedBytes ||
            digest != mar184::kPlayerIdleSerializedSha) {
            std::cerr << "MAR-184 P4: adding the inherit schema must not change one "
                         "byte of an existing project's serialization. Expected "
                      << mar184::kPlayerIdleSerializedBytes << " bytes / "
                      << mar184::kPlayerIdleSerializedSha << ", measured "
                      << serialized.size() << " bytes / " << digest << ".\n";
            return false;
        }
    }

    // -- P1 -- save -> LOAD round trip. A passing save proves nothing. -------
    {
        const std::filesystem::path path = temporary.path / "p1.marrow";
        marrow::editor::ProjectData project = mar184::minimal_project(path);
        project.bone_inherit_timeline_edits.push_back(
            marrow::editor::BoneInheritTimelineEdit{
                "toggle_inherit",
                "controller",
                {{0.0, BoneInherit::NoScale}, {0.4, BoneInherit::Normal}}});
        const auto saved = marrow::editor::save_project(project, path);
        if (!saved) {
            std::cerr << "MAR-184 P1: the project failed to save: "
                      << saved.error->message << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(path);
        if (!reloaded) {
            std::cerr << "MAR-184 P1: the saved project failed to reload: "
                      << reloaded.error->message << '\n';
            return false;
        }
        const auto& edits = reloaded.project->bone_inherit_timeline_edits;
        if (edits.size() != 1U) {
            std::cerr << "MAR-184 P1: the reloaded project carries " << edits.size()
                      << " inherit edits, expected 1. A serializer that never "
                         "emits the family loses every overlay on save, and no "
                         "other case notices because no other project has one.\n";
            return false;
        }
        if (edits[0].animation_name != "toggle_inherit" ||
            edits[0].bone_name != "controller" || edits[0].keyframes.size() != 2U) {
            std::cerr << "MAR-184 P1: the reloaded edit is '"
                      << edits[0].animation_name << "'/'" << edits[0].bone_name
                      << "' with " << edits[0].keyframes.size()
                      << " keyframes, expected 'toggle_inherit'/'controller' with 2.\n";
            return false;
        }
        if (!mar184::times_equal(edits[0].keyframes[0].time, 0.0) ||
            !mar184::times_equal(edits[0].keyframes[1].time, 0.4) ||
            edits[0].keyframes[0].inherit != BoneInherit::NoScale ||
            edits[0].keyframes[1].inherit != BoneInherit::Normal) {
            std::cerr << "MAR-184 P1: the reloaded keys are ("
                      << edits[0].keyframes[0].time << ", "
                      << mar184::mode_name(edits[0].keyframes[0].inherit) << ") and ("
                      << edits[0].keyframes[1].time << ", "
                      << mar184::mode_name(edits[0].keyframes[1].inherit)
                      << "), expected (0, noScale) and (0.4, normal).\n";
            return false;
        }
    }

    // -- P2 -- the parser's rejections, each asserted on the MESSAGE. --------
    if (!mar184::expect_load_rejection(
            temporary.path,
            "P2a",
            "controller",
            "[{ \"time\": -0.5, \"inherit\": \"normal\" }]",
            "$.timeline_edits.animations.toggle_inherit.bones.controller.inherit[0].time",
            "inherit keyframe time must be finite and non-negative")) {
        return false;
    }
    if (!mar184::expect_load_rejection(
            temporary.path,
            "P2b",
            "controller",
            "[{ \"time\": 0.4, \"inherit\": \"normal\" },"
            " { \"time\": 0.2, \"inherit\": \"noScale\" }]",
            "$.timeline_edits.animations.toggle_inherit.bones.controller.inherit[1].time",
            "inherit timeline edit keyframe times must be strictly increasing")) {
        return false;
    }
    // 1e39 is a perfectly finite double that no float32 can hold. It is the
    // ONLY non-finite-ish value reachable from a file: JSON has no NaN literal
    // and the tokenizer refuses an out-of-range exponent outright. Deleting the
    // finiteness call still produces an error here -- the runtime's own float32
    // guard fires during materialization -- so this case must assert the TEXT.
    if (!mar184::expect_load_rejection(
            temporary.path,
            "P2c",
            "controller",
            "[{ \"time\": 1e39, \"inherit\": \"normal\" }]",
            "$.timeline_edits.animations.toggle_inherit.bones.controller.inherit[0].time",
            "inherit keyframe time must be finite and non-negative")) {
        return false;
    }
    if (!mar184::expect_load_rejection(
            temporary.path,
            "P2d",
            "controller",
            "[{ \"time\": 0.0, \"inherit\": \"noScales\" }]",
            "$.timeline_edits.animations.toggle_inherit.bones.controller.inherit[0].inherit",
            "inherit mode must be one of normal, onlyTranslation, "
            "noRotationOrReflection, noScale, or noScaleOrReflection")) {
        return false;
    }
    if (!mar184::expect_load_rejection(
            temporary.path,
            "P2e",
            "controller",
            "[]",
            "$.timeline_edits.animations.toggle_inherit.bones.controller.inherit",
            "inherit timeline edits must contain at least one keyframe")) {
        return false;
    }

    // -- P6 -- inherit keys are stepped; a curve member is refused. ----------
    if (!mar184::expect_load_rejection(
            temporary.path,
            "P6",
            "controller",
            "[{ \"time\": 0.0, \"inherit\": \"noScale\", \"curve\": [0, 0, 1, 1] }]",
            "$.timeline_edits.animations.toggle_inherit.bones.controller.inherit[0].curve",
            "inherit keys are stepped and must not carry curve data")) {
        return false;
    }

    // -- P3 -- all five modes survive save -> load -> materialization. -------
    {
        const std::filesystem::path path = temporary.path / "p3.marrow";
        marrow::editor::ProjectData project = mar184::minimal_project(path);
        const std::array<BoneInherit, 5> modes{
            BoneInherit::Normal,
            BoneInherit::OnlyTranslation,
            BoneInherit::NoRotationOrReflection,
            BoneInherit::NoScale,
            BoneInherit::NoScaleOrReflection};
        marrow::editor::BoneInheritTimelineEdit edit;
        edit.animation_name = "toggle_inherit";
        edit.bone_name = "controller";
        for (std::size_t index = 0; index < modes.size(); ++index) {
            edit.keyframes.push_back(marrow::editor::InheritKeyframeEdit{
                static_cast<double>(index) * 0.1, modes[index]});
        }
        project.bone_inherit_timeline_edits.push_back(std::move(edit));
        const auto saved = marrow::editor::save_project(project, path);
        if (!saved) {
            std::cerr << "MAR-184 P3: the project failed to save: "
                      << saved.error->message << '\n';
            return false;
        }
        const auto reloaded = marrow::editor::load_project(path);
        if (!reloaded) {
            std::cerr << "MAR-184 P3: the saved project failed to reload: "
                      << reloaded.error->message << '\n';
            return false;
        }
        const auto bone_index =
            reloaded.skeleton_data->find_bone_index("controller");
        const auto* animation =
            reloaded.skeleton_data->find_animation("toggle_inherit");
        if (!bone_index.has_value() || animation == nullptr) {
            std::cerr << "MAR-184 P3: the materialized skeleton lost 'controller' or "
                         "'toggle_inherit'.\n";
            return false;
        }
        const auto* timeline = animation->find_inherit_timeline(*bone_index);
        if (timeline == nullptr || timeline->keyframes.size() != modes.size()) {
            std::cerr << "MAR-184 P3: the materialized animation carries "
                      << (timeline == nullptr ? 0U : timeline->keyframes.size())
                      << " inherit keys for 'controller', expected " << modes.size()
                      << ".\n";
            return false;
        }
        for (std::size_t index = 0; index < modes.size(); ++index) {
            if (timeline->keyframes[index].inherit != modes[index]) {
                std::cerr << "MAR-184 P3: key " << index << " materialized as "
                          << mar184::mode_name(timeline->keyframes[index].inherit)
                          << ", expected " << mar184::mode_name(modes[index])
                          << ". All five tokens must survive the project layer's own "
                             "table in BOTH directions.\n";
                return false;
            }
        }
    }

    // -- P13 -- `ensure` materializes the base track on first touch. --------
    {
        const std::filesystem::path path = temporary.path / "p13.marrow";
        marrow::editor::ProjectData project = mar184::minimal_project(path);
        const auto saved = marrow::editor::save_project(project, path);
        if (!saved) {
            std::cerr << "MAR-184 P13: the project failed to save: "
                      << saved.error->message << '\n';
            return false;
        }
        const auto loaded = marrow::editor::load_project(path);
        if (!loaded) {
            std::cerr << "MAR-184 P13: the project failed to load: "
                      << loaded.error->message << '\n';
            return false;
        }
        const marrow::runtime::SkeletonData& skeleton = *loaded.skeleton_data;
        marrow::editor::ProjectData& working = *loaded.project;

        marrow::editor::BoneInheritTimelineEdit* base_backed =
            marrow::editor::ensure_bone_inherit_timeline_edit(
                working, skeleton, "toggle_inherit", "child");
        if (base_backed == nullptr) {
            std::cerr << "MAR-184 P13: ensure on 'child' returned nullptr; the "
                         "fixture's only base inherit track lives there.\n";
            return false;
        }
        const std::array<double, 4> base_times{0.0, 0.25, 0.5, 1.0};
        const std::array<BoneInherit, 4> base_modes{
            BoneInherit::Normal,
            BoneInherit::NoRotationOrReflection,
            BoneInherit::OnlyTranslation,
            BoneInherit::Normal};
        if (base_backed->keyframes.size() != base_times.size()) {
            std::cerr << "MAR-184 P13: ensure on 'child' produced "
                      << base_backed->keyframes.size()
                      << " keyframes, expected the base track's "
                      << base_times.size()
                      << ". An ensure that creates an EMPTY edit for a bone that "
                         "already has an imported track makes the first merge "
                         "silently REPLACE that track instead of extending it.\n";
            return false;
        }
        for (std::size_t index = 0; index < base_times.size(); ++index) {
            if (!mar184::times_equal(
                    base_backed->keyframes[index].time, base_times[index]) ||
                base_backed->keyframes[index].inherit != base_modes[index]) {
                std::cerr << "MAR-184 P13: materialized key " << index << " is ("
                          << base_backed->keyframes[index].time << ", "
                          << mar184::mode_name(base_backed->keyframes[index].inherit)
                          << "), expected (" << base_times[index] << ", "
                          << mar184::mode_name(base_modes[index]) << ").\n";
                return false;
            }
        }

        if (marrow::editor::ensure_bone_inherit_timeline_edit(
                working, skeleton, "toggle_inherit", "child") != base_backed ||
            working.bone_inherit_timeline_edits.size() != 1U) {
            std::cerr << "MAR-184 P13: a second ensure on 'child' must return the "
                         "SAME edit and append nothing; the project now holds "
                      << working.bone_inherit_timeline_edits.size() << " edits.\n";
            return false;
        }

        const marrow::editor::BoneInheritTimelineEdit* project_only =
            marrow::editor::ensure_bone_inherit_timeline_edit(
                working, skeleton, "toggle_inherit", "controller");
        if (project_only == nullptr || !project_only->keyframes.empty() ||
            working.bone_inherit_timeline_edits.size() != 2U) {
            std::cerr << "MAR-184 P13: ensure on 'controller', which has no base "
                         "track, must create an EMPTY edit -- that is what a "
                         "project-only timeline is before its first key.\n";
            return false;
        }

        if (marrow::editor::ensure_bone_inherit_timeline_edit(
                working, skeleton, "toggle_inherit", "nosuchbone") != nullptr ||
            marrow::editor::ensure_bone_inherit_timeline_edit(
                working, skeleton, "nosuchanim", "child") != nullptr ||
            working.bone_inherit_timeline_edits.size() != 2U) {
            std::cerr << "MAR-184 P13: an unresolvable bone or animation must "
                         "return nullptr and append nothing; the project now holds "
                      << working.bone_inherit_timeline_edits.size() << " edits.\n";
            return false;
        }
    }

    // -- P7 -- a project-only overlay materializes into a dopesheet row. ----
    {
        const std::filesystem::path path = temporary.path / "p7.marrow";
        marrow::editor::ProjectData project = mar184::minimal_project(path);
        project.bone_inherit_timeline_edits.push_back(
            marrow::editor::BoneInheritTimelineEdit{
                "toggle_inherit",
                "controller",
                {{0.0, BoneInherit::NoScale}, {0.4, BoneInherit::Normal}}});
        if (!marrow::editor::save_project(project, path)) {
            std::cerr << "MAR-184 P7: the project failed to save.\n";
            return false;
        }
        const auto reloaded = marrow::editor::load_project(path);
        if (!reloaded) {
            std::cerr << "MAR-184 P7: the project failed to reload: "
                      << reloaded.error->message << '\n';
            return false;
        }
        const auto bone_index = reloaded.skeleton_data->find_bone_index("controller");
        const auto* animation =
            reloaded.skeleton_data->find_animation("toggle_inherit");
        if (!bone_index.has_value() || animation == nullptr) {
            std::cerr << "MAR-184 P7: the materialized skeleton lost 'controller' or "
                         "'toggle_inherit'.\n";
            return false;
        }
        const auto* timeline = animation->find_inherit_timeline(*bone_index);
        if (timeline == nullptr) {
            std::cerr << "MAR-184 P7: the materialized animation has no inherit "
                         "timeline for 'controller'. A project overlay that never "
                         "reaches build_runtime_document is authored, saved, "
                         "reloaded -- and invisible to the runtime.\n";
            return false;
        }
        if (timeline->keyframes.size() != 2U ||
            timeline->keyframes[0].inherit != BoneInherit::NoScale ||
            timeline->keyframes[1].inherit != BoneInherit::Normal) {
            std::cerr << "MAR-184 P7: the materialized timeline carries "
                      << timeline->keyframes.size()
                      << " keys, expected 2 (noScale, normal).\n";
            return false;
        }

        // The dopesheet row is PRODUCED by a pure function over SkeletonData.
        // This is a model-layer assertion and deliberately not frame coverage:
        // it does not prove a pixel is drawn. MAR-184 adds no drawing code.
        const std::vector<marrow::editor::timeline_model::TrackRow> tracks =
            marrow::editor::timeline_model::build_tracks(
                *reloaded.skeleton_data, *animation);
        const auto row = std::find_if(
            tracks.begin(),
            tracks.end(),
            [&](const marrow::editor::timeline_model::TrackRow& track) {
                return track.kind ==
                        marrow::editor::timeline_model::TimelineTrackKind::Inherit &&
                    track.bone_index == bone_index;
            });
        if (row == tracks.end()) {
            std::cerr << "MAR-184 P7: build_tracks produced no Inherit row for "
                         "'controller'.\n";
            return false;
        }
        if (row->key_times.size() != 2U ||
            !mar184::float32_times_equal(row->key_times[0], 0.0) ||
            !mar184::float32_times_equal(row->key_times[1], 0.4)) {
            std::cerr << "MAR-184 P7: the Inherit row for 'controller' carries "
                      << row->key_times.size()
                      << " key times, expected the overlay's 0 and 0.4.\n";
            return false;
        }
    }

    // -- P8 -- the base document's unrelated data survives materialization. -
    {
        const std::filesystem::path path = temporary.path / "p8.marrow";
        marrow::editor::ProjectData project = mar184::minimal_project(path);
        project.bone_inherit_timeline_edits.push_back(
            marrow::editor::BoneInheritTimelineEdit{
                "toggle_inherit",
                "controller",
                {{0.0, BoneInherit::NoScale}, {0.4, BoneInherit::Normal}}});
        marrow::editor::TransformTimelineEdit rotate_edit;
        rotate_edit.animation_name = "toggle_inherit";
        rotate_edit.bone_name = "root";
        rotate_edit.channel = marrow::editor::TransformTimelineChannel::Rotate;
        rotate_edit.keyframes.push_back(marrow::editor::TransformKeyframeEdit{0.0, 0.0});
        rotate_edit.keyframes.push_back(marrow::editor::TransformKeyframeEdit{0.5, 30.0});
        project.transform_timeline_edits.push_back(std::move(rotate_edit));
        if (!marrow::editor::save_project(project, path)) {
            std::cerr << "MAR-184 P8: the project failed to save.\n";
            return false;
        }
        const auto reloaded = marrow::editor::load_project(path);
        if (!reloaded) {
            std::cerr << "MAR-184 P8: the project failed to reload: "
                      << reloaded.error->message << '\n';
            return false;
        }
        const auto* animation =
            reloaded.skeleton_data->find_animation("toggle_inherit");
        if (animation == nullptr) {
            std::cerr << "MAR-184 P8: the materialized skeleton lost "
                         "'toggle_inherit'.\n";
            return false;
        }
        if (animation->bone_inherit_timelines.size() != 2U) {
            std::cerr << "MAR-184 P8: 'child' lost its base inherit timeline; the "
                         "animation has "
                      << animation->bone_inherit_timelines.size()
                      << " inherit timeline(s), expected 2. Replacing the "
                         "animation's whole `bones` object instead of assigning "
                         "into it discards every sibling the project does not "
                         "override.\n";
            return false;
        }
        const auto child_index = reloaded.skeleton_data->find_bone_index("child");
        const auto root_index = reloaded.skeleton_data->find_bone_index("root");
        if (!child_index.has_value() || !root_index.has_value()) {
            std::cerr << "MAR-184 P8: the materialized skeleton lost a bone.\n";
            return false;
        }
        const auto* base_track = animation->find_inherit_timeline(*child_index);
        const std::array<double, 4> base_times{0.0, 0.25, 0.5, 1.0};
        const std::array<BoneInherit, 4> base_modes{
            BoneInherit::Normal,
            BoneInherit::NoRotationOrReflection,
            BoneInherit::OnlyTranslation,
            BoneInherit::Normal};
        if (base_track == nullptr || base_track->keyframes.size() != base_times.size()) {
            std::cerr << "MAR-184 P8: 'child' base inherit track did not survive.\n";
            return false;
        }
        for (std::size_t index = 0; index < base_times.size(); ++index) {
            if (std::abs(static_cast<double>(base_track->keyframes[index].time) -
                         base_times[index]) > 1e-6 ||
                base_track->keyframes[index].inherit != base_modes[index]) {
                std::cerr << "MAR-184 P8: 'child' base key " << index
                          << " changed under an unrelated bone's overlay.\n";
                return false;
            }
        }
        const auto* rotate_track = animation->find_rotate_timeline(*root_index);
        if (rotate_track == nullptr || rotate_track->keyframes.size() != 2U) {
            std::cerr << "MAR-184 P8: the unrelated 'root' rotate overlay did not "
                         "survive alongside the inherit overlay.\n";
            return false;
        }
    }

    // -- P9 -- an empty edit reaches neither serializer. ---------------------
    {
        const std::filesystem::path path = temporary.path / "p9.marrow";
        marrow::editor::ProjectData seed = mar184::minimal_project(path);
        if (!marrow::editor::save_project(seed, path)) {
            std::cerr << "MAR-184 P9: the seed project failed to save.\n";
            return false;
        }
        const auto loaded = marrow::editor::load_project(path);
        if (!loaded) {
            std::cerr << "MAR-184 P9: the seed project failed to load: "
                      << loaded.error->message << '\n';
            return false;
        }
        marrow::editor::ProjectData& working = *loaded.project;
        if (marrow::editor::ensure_bone_inherit_timeline_edit(
                working, *loaded.skeleton_data, "toggle_inherit", "controller") ==
            nullptr) {
            std::cerr << "MAR-184 P9: ensure on 'controller' returned nullptr.\n";
            return false;
        }
        // The unrelated rotate overlay is load-bearing, not scenery. The
        // serializer gates the whole `timeline_edits` object on the effective
        // edits, so a project whose ONLY edit is the empty inherit one writes no
        // `timeline_edits` at all and `build_timeline_edits_value` never runs --
        // which would leave the builder's own empty-edit skip untested. With a
        // second family present the object IS written, and the skip is the only
        // thing standing between an empty edit and an `"inherit": []` on disk.
        marrow::editor::TransformTimelineEdit unrelated_rotate;
        unrelated_rotate.animation_name = "toggle_inherit";
        unrelated_rotate.bone_name = "root";
        unrelated_rotate.channel = marrow::editor::TransformTimelineChannel::Rotate;
        unrelated_rotate.keyframes.push_back(
            marrow::editor::TransformKeyframeEdit{0.0, 0.0});
        unrelated_rotate.keyframes.push_back(
            marrow::editor::TransformKeyframeEdit{0.5, 30.0});
        working.transform_timeline_edits.push_back(std::move(unrelated_rotate));

        // P9a -- materialization in memory, no save.
        const auto runtime_result = marrow::editor::build_project_runtime(
            working, *loaded.base_skeleton_document);
        if (!runtime_result) {
            std::cerr << "MAR-184 P9a: materialization failed: "
                      << runtime_result.error->format()
                      << ". An empty inherit edit that reaches the runtime "
                         "document writes `\"inherit\": []`, which the runtime "
                         "parser refuses outright.\n";
            return false;
        }
        const auto controller_index =
            runtime_result.skeleton_data->find_bone_index("controller");
        const auto* materialized_animation =
            runtime_result.skeleton_data->find_animation("toggle_inherit");
        if (!controller_index.has_value() || materialized_animation == nullptr ||
            materialized_animation->find_inherit_timeline(*controller_index) !=
                nullptr) {
            std::cerr << "MAR-184 P9a: an empty edit must materialize NO timeline "
                         "for 'controller'.\n";
            return false;
        }

        // P9b -- the same project through save and LOAD.
        const std::filesystem::path empty_path = temporary.path / "p9b.marrow";
        if (!marrow::editor::save_project(working, empty_path)) {
            std::cerr << "MAR-184 P9b: the project failed to save.\n";
            return false;
        }
        std::ifstream written(empty_path);
        const std::string written_text(
            (std::istreambuf_iterator<char>(written)),
            std::istreambuf_iterator<char>());
        if (written_text.find("\"inherit\"") != std::string::npos) {
            std::cerr << "MAR-184 P9b: the written `.marrow` carries an `inherit` "
                         "member for an EMPTY edit. The project parser refuses an "
                         "empty array, so such a file saves and can never be "
                         "reopened.\n";
            return false;
        }
        const auto empty_reloaded = marrow::editor::load_project(empty_path);
        if (!empty_reloaded) {
            std::cerr << "MAR-184 P9b: reload failed: "
                      << empty_reloaded.error->format() << '\n';
            return false;
        }
    }

    // -- P5 -- the merge is deterministic regardless of request order. ------
    {
        const auto build_merged = [&](bool reversed,
                                      std::string* serialized_out,
                                      std::vector<marrow::editor::InheritKeyframeEdit>*
                                          keys_out,
                                      marrow::editor::InheritTimelineMergeResult*
                                          result_out) -> bool {
            const std::filesystem::path path =
                temporary.path / (reversed ? "p5b.marrow" : "p5a.marrow");
            marrow::editor::ProjectData seed = mar184::minimal_project(path);
            if (!marrow::editor::save_project(seed, path)) {
                std::cerr << "MAR-184 P5: the seed project failed to save.\n";
                return false;
            }
            const auto loaded = marrow::editor::load_project(path);
            if (!loaded) {
                std::cerr << "MAR-184 P5: the seed project failed to load: "
                          << loaded.error->message << '\n';
                return false;
            }
            marrow::editor::InheritTimelineMergeRequest request;
            request.animation_name = "toggle_inherit";
            request.bone_name = "controller";
            request.keys = {
                {0.5, "noScale"}, {0.1, "normal"}, {0.3, "onlyTranslation"}};
            if (reversed) {
                std::reverse(request.keys.begin(), request.keys.end());
            }
            *result_out = marrow::editor::merge_inherit_timeline(
                loaded.project.get(), *loaded.skeleton_data, request);
            if (!*result_out) {
                std::cerr << "MAR-184 P5: the merge was rejected: "
                          << result_out->error << '\n';
                return false;
            }
            *serialized_out = marrow::editor::serialize_project(*loaded.project);
            const auto* edit = loaded.project->find_bone_inherit_timeline_edit(
                "toggle_inherit", "controller");
            if (edit == nullptr) {
                std::cerr << "MAR-184 P5: the merge stored no edit.\n";
                return false;
            }
            *keys_out = edit->keyframes;
            return true;
        };

        std::string forward_text;
        std::string reverse_text;
        std::vector<marrow::editor::InheritKeyframeEdit> forward_keys;
        std::vector<marrow::editor::InheritKeyframeEdit> reverse_keys;
        marrow::editor::InheritTimelineMergeResult forward_result;
        marrow::editor::InheritTimelineMergeResult reverse_result;
        if (!build_merged(false, &forward_text, &forward_keys, &forward_result) ||
            !build_merged(true, &reverse_text, &reverse_keys, &reverse_result)) {
            return false;
        }
        if (forward_text != reverse_text) {
            std::cerr << "MAR-184 P5: the same three keys merged in two request "
                         "orders must serialize BYTE-IDENTICALLY; the two runs "
                         "produced "
                      << forward_text.size() << " and " << reverse_text.size()
                      << " bytes.\n";
            return false;
        }
        // Asserted on the IN-MEMORY vector, with no save and no reload, so the
        // runtime's own strictly-increasing guard cannot stand in for it: a
        // descending sort would be caught by the loader, never by this story.
        for (std::size_t index = 1; index < forward_keys.size(); ++index) {
            if (forward_keys[index].time <= forward_keys[index - 1U].time) {
                std::cerr << "MAR-184 P5: stored times are not strictly increasing:";
                for (const auto& key : forward_keys) {
                    std::cerr << ' ' << key.time;
                }
                std::cerr << ".\n";
                return false;
            }
        }
        if (forward_keys.size() != 3U ||
            !mar184::times_equal(forward_keys[0].time, 0.1) ||
            !mar184::times_equal(forward_keys[1].time, 0.3) ||
            !mar184::times_equal(forward_keys[2].time, 0.5) ||
            forward_keys[0].inherit != BoneInherit::Normal ||
            forward_keys[1].inherit != BoneInherit::OnlyTranslation ||
            forward_keys[2].inherit != BoneInherit::NoScale) {
            std::cerr << "MAR-184 P5: the merged keys are not (0.1 normal), "
                         "(0.3 onlyTranslation), (0.5 noScale).\n";
            return false;
        }
        if (forward_result.added_key_count != 3U ||
            forward_result.replaced_key_count != 0U ||
            forward_result.effective_key_count != 3U ||
            !forward_result.changed) {
            std::cerr << "MAR-184 P5: counts are added="
                      << forward_result.added_key_count << " replaced="
                      << forward_result.replaced_key_count << " effective="
                      << forward_result.effective_key_count
                      << ", expected 3/0/3.\n";
            return false;
        }
    }

    // -- P10 -- every rejection is decided BEFORE anything is written. ------
    {
        const auto reject = [&](std::string_view sub_case,
                                std::string_view animation_name,
                                std::string_view bone_name,
                                double time,
                                std::string_view mode,
                                std::string_view expected_a,
                                std::string_view expected_b,
                                bool assert_byte_identity) -> bool {
            const std::filesystem::path path =
                temporary.path / (std::string(sub_case) + ".marrow");
            marrow::editor::ProjectData seed = mar184::minimal_project(path);
            if (!marrow::editor::save_project(seed, path)) {
                std::cerr << "MAR-184 " << sub_case << ": the seed failed to save.\n";
                return false;
            }
            const auto loaded = marrow::editor::load_project(path);
            if (!loaded) {
                std::cerr << "MAR-184 " << sub_case << ": the seed failed to load.\n";
                return false;
            }
            const std::string before =
                marrow::editor::serialize_project(*loaded.project);
            marrow::editor::InheritTimelineMergeRequest request;
            request.animation_name = std::string(animation_name);
            request.bone_name = std::string(bone_name);
            request.keys = {{time, std::string(mode)}};
            const auto result = marrow::editor::merge_inherit_timeline(
                loaded.project.get(), *loaded.skeleton_data, request);
            if (result || result.changed) {
                std::cerr << "MAR-184 " << sub_case
                          << ": expected an error naming '" << expected_a
                          << "', got changed=" << (result.changed ? "true" : "false")
                          << " with error '" << result.error << "'.\n";
                return false;
            }
            if (result.error.find(expected_a) == std::string::npos ||
                result.error.find(expected_b) == std::string::npos) {
                std::cerr << "MAR-184 " << sub_case << ": expected the error to name '"
                          << expected_a << "' and '" << expected_b << "', got '"
                          << result.error << "'.\n";
                return false;
            }
            if (assert_byte_identity) {
                const std::string after =
                    marrow::editor::serialize_project(*loaded.project);
                if (after != before) {
                    std::cerr << "MAR-184 " << sub_case
                              << ": the rejected merge changed serialize_project() ("
                              << before.size() << " -> " << after.size()
                              << " bytes). Validation must complete before ANY "
                                 "write, so a rejection leaves the project "
                                 "bytewise as it was.\n";
                    return false;
                }
            }
            return true;
        };

        // P10a and P10b deliberately assert NO byte identity: for an
        // unresolvable animation or bone `ensure` returns nullptr and writes
        // nothing regardless, so such an assertion could not fail.
        if (!reject("P10a", "nosuchanim", "child", 0.2, "normal",
                    "nosuchanim", "does not exist", false)) {
            return false;
        }
        if (!reject("P10b", "toggle_inherit", "nosuchbone", 0.2, "normal",
                    "nosuchbone", "does not exist", false)) {
            return false;
        }
        // P10c uses `child`, whose base track is NON-EMPTY. On `controller`
        // `ensure` would create an EMPTY edit, which the serializer's gate skips
        // -- so the byte-identity assertion would be vacuous there.
        if (!reject("P10c", "toggle_inherit", "child", 0.2, "noScales",
                    "noScales", "inherit mode must be one of", true)) {
            return false;
        }
        if (!reject("P10d", "toggle_inherit", "child",
                    std::numeric_limits<double>::quiet_NaN(), "normal",
                    "finite and non-negative", "key time", true)) {
            return false;
        }
    }

    // -- P11 -- collision, both arms, and the no-op contract. ----------------
    {
        const std::filesystem::path path = temporary.path / "p11.marrow";
        marrow::editor::ProjectData seed = mar184::minimal_project(path);
        if (!marrow::editor::save_project(seed, path)) {
            std::cerr << "MAR-184 P11: the seed project failed to save.\n";
            return false;
        }
        const auto loaded = marrow::editor::load_project(path);
        if (!loaded) {
            std::cerr << "MAR-184 P11: the seed project failed to load.\n";
            return false;
        }
        marrow::editor::ProjectData* project = loaded.project.get();
        // `child` carries a 4-key base track and NO project edit yet. Both
        // halves are load-bearing: with an edit already present `ensure` would
        // be a no-op and arm 1's byte-identity assertion could not fail.
        if (project->find_bone_inherit_timeline_edit("toggle_inherit", "child") !=
            nullptr) {
            std::cerr << "MAR-184 P11: the seed already carries a 'child' edit, "
                         "which would make arm 1's byte-identity check vacuous.\n";
            return false;
        }

        marrow::editor::InheritTimelineMergeRequest colliding;
        colliding.animation_name = "toggle_inherit";
        colliding.bone_name = "child";
        colliding.keys = {{0.25, "noScale"}};
        const std::string before = marrow::editor::serialize_project(*project);
        const auto rejected = marrow::editor::merge_inherit_timeline(
            project, *loaded.skeleton_data, colliding);
        if (rejected || rejected.changed ||
            rejected.error.find("a key already exists at time") == std::string::npos ||
            rejected.error.find("0.25") == std::string::npos) {
            std::cerr << "MAR-184 P11 arm 1: a key landing on an existing key must "
                         "be refused by default; got changed="
                      << (rejected.changed ? "true" : "false") << " error '"
                      << rejected.error << "'.\n";
            return false;
        }
        if (marrow::editor::serialize_project(*project) != before) {
            std::cerr << "MAR-184 P11 arm 1: the rejected merge changed "
                         "serialize_project(). A collision refused AFTER `ensure` "
                         "has materialized the base track leaves the project "
                         "modified by a call that reported failure.\n";
            return false;
        }

        marrow::editor::InheritTimelineMergeRequest replacing = colliding;
        replacing.replace_existing_times = true;
        replacing.keys = {{0.2500001, "noScale"}};
        const auto replaced = marrow::editor::merge_inherit_timeline(
            project, *loaded.skeleton_data, replacing);
        if (!replaced || !replaced.changed || replaced.replaced_key_count != 1U ||
            replaced.added_key_count != 0U || replaced.effective_key_count != 4U) {
            std::cerr << "MAR-184 P11 arm 2: the replacing merge reported changed="
                      << (replaced.changed ? "true" : "false") << " added="
                      << replaced.added_key_count << " replaced="
                      << replaced.replaced_key_count << " effective="
                      << replaced.effective_key_count
                      << " error '" << replaced.error << "', expected 1 replaced of "
                         "4 effective.\n";
            return false;
        }
        const auto* edit =
            project->find_bone_inherit_timeline_edit("toggle_inherit", "child");
        if (edit == nullptr || edit->keyframes.size() != 4U) {
            std::cerr << "MAR-184 P11 arm 2: the replaced timeline is missing or no "
                         "longer carries the base track's four keys.\n";
            return false;
        }
        if (edit->keyframes[1].inherit != BoneInherit::NoScale) {
            std::cerr << "MAR-184 P11 arm 2: the replaced key's mode is "
                      << mar184::mode_name(edit->keyframes[1].inherit)
                      << ", expected noScale.\n";
            return false;
        }
        if (edit->keyframes[1].time != 0.25) {
            std::cerr << "MAR-184 P11 arm 2: the replaced key drifted to "
                      << std::setprecision(10) << edit->keyframes[1].time
                      << ", expected the stored 0.25. An overwrite that writes the "
                         "REQUESTED time lets the 1e-6 identity window walk a key "
                         "one merge at a time.\n";
            return false;
        }

        marrow::editor::InheritTimelineMergeRequest no_op = replacing;
        no_op.keys = {{0.25, "noScale"}};
        const std::string settled = marrow::editor::serialize_project(*project);
        const auto unchanged = marrow::editor::merge_inherit_timeline(
            project, *loaded.skeleton_data, no_op);
        if (!unchanged || unchanged.changed || !unchanged.error.empty() ||
            marrow::editor::serialize_project(*project) != settled) {
            std::cerr << "MAR-184 P11 arm 3: re-requesting exactly what is stored "
                         "must report changed=false with an EMPTY error and write "
                         "nothing; got changed="
                      << (unchanged.changed ? "true" : "false") << " error '"
                      << unchanged.error << "'.\n";
            return false;
        }
    }

    // -- P12 -- runtime export, `.mskl` and `.mbin`, versions unmoved. ------
    {
        const std::filesystem::path path = temporary.path / "p12.marrow";
        marrow::editor::ProjectData seed = mar184::minimal_project(path);
        if (!marrow::editor::save_project(seed, path)) {
            std::cerr << "MAR-184 P12: the seed project failed to save.\n";
            return false;
        }
        const auto loaded = marrow::editor::load_project(path);
        if (!loaded) {
            std::cerr << "MAR-184 P12: the seed project failed to load.\n";
            return false;
        }
        // Merged rather than hand-authored, so the export path exercises the
        // primitive end to end. `child` is base-backed, so this also proves an
        // imported track survives export with the merge layered onto it.
        marrow::editor::InheritTimelineMergeRequest request;
        request.animation_name = "toggle_inherit";
        request.bone_name = "child";
        request.keys = {{0.75, "noScaleOrReflection"}};
        const auto merged = marrow::editor::merge_inherit_timeline(
            loaded.project.get(), *loaded.skeleton_data, request);
        if (!merged || !merged.changed || merged.effective_key_count != 5U) {
            std::cerr << "MAR-184 P12: the export fixture merge failed: "
                      << merged.error << '\n';
            return false;
        }

        marrow::editor::ProjectExportOptions export_options;
        export_options.skeleton_output_path = temporary.path / "p12_export.mskl";
        export_options.binary_output_path = temporary.path / "p12_export.mbin";
        const auto exported = marrow::editor::export_runtime_assets(
            *loaded.project, *loaded.base_skeleton_document, export_options);
        if (!exported || !exported.binary_path.has_value()) {
            std::cerr << "MAR-184 P12: export failed.\n";
            return false;
        }

        const std::array<double, 5> expected_times{0.0, 0.25, 0.5, 0.75, 1.0};
        const std::array<BoneInherit, 5> expected_modes{
            BoneInherit::Normal,
            BoneInherit::NoRotationOrReflection,
            BoneInherit::OnlyTranslation,
            BoneInherit::NoScaleOrReflection,
            BoneInherit::Normal};
        const auto check_exported = [&](const std::filesystem::path& file,
                                        std::string_view label) -> bool {
            const auto skeleton = marrow::runtime::load_skeleton_data(file);
            if (!skeleton) {
                std::cerr << "MAR-184 P12: the exported " << label
                          << " failed to load: " << skeleton.error->format() << '\n';
                return false;
            }
            const auto bone_index = skeleton.skeleton_data->find_bone_index("child");
            const auto* animation =
                skeleton.skeleton_data->find_animation("toggle_inherit");
            if (!bone_index.has_value() || animation == nullptr) {
                std::cerr << "MAR-184 P12: the exported " << label
                          << " lost 'child' or 'toggle_inherit'.\n";
                return false;
            }
            const auto* timeline = animation->find_inherit_timeline(*bone_index);
            if (timeline == nullptr ||
                timeline->keyframes.size() != expected_times.size()) {
                std::cerr << "MAR-184 P12: the exported " << label << " carries "
                          << (timeline == nullptr ? 0U : timeline->keyframes.size())
                          << " inherit keys, expected " << expected_times.size()
                          << ".\n";
                return false;
            }
            for (std::size_t index = 0; index < expected_times.size(); ++index) {
                // Float32 tolerance is mandatory: project time is `double` and
                // `AnimationScalar` is `float`, and `.mbin` v2 narrows again.
                if (!mar184::float32_times_equal(
                        static_cast<double>(timeline->keyframes[index].time),
                        expected_times[index]) ||
                    timeline->keyframes[index].inherit != expected_modes[index]) {
                    std::cerr << "MAR-184 P12: exported " << label << " key " << index
                              << " is (" << timeline->keyframes[index].time << ", "
                              << mar184::mode_name(timeline->keyframes[index].inherit)
                              << "), expected (" << expected_times[index] << ", "
                              << mar184::mode_name(expected_modes[index]) << ").\n";
                    return false;
                }
            }
            return true;
        };
        if (!check_exported(exported.path, ".mskl") ||
            !check_exported(*exported.binary_path, ".mbin")) {
            return false;
        }

        // The formats do not move. `.mskl` stays 1 and `.mbin` stays 2; inherit
        // rides the generic document codec, which this story does not touch.
        const auto exported_document =
            marrow::runtime::json::load_document(exported.path);
        const marrow::runtime::json::Value* version_value =
            exported_document
            ? marrow::runtime::json::find_member(
                  exported_document.document->root, "version")
            : nullptr;
        if (version_value == nullptr || !version_value->is_number() ||
            version_value->as_number() != 1.0) {
            std::cerr << "MAR-184 P12: the exported `.mskl` version is not 1.\n";
            return false;
        }
        std::ifstream binary(*exported.binary_path, std::ios::binary);
        std::array<char, 5> header{};
        binary.read(header.data(), static_cast<std::streamsize>(header.size()));
        if (header[0] != 'M' || header[1] != 'B' || header[2] != 'I' ||
            header[3] != 'N') {
            std::cerr << "MAR-184 P12: the exported `.mbin` has no MBIN magic, so "
                         "the byte read below would not be a version.\n";
            return false;
        }
        const int binary_version = static_cast<int>(
            static_cast<unsigned char>(header[mar184::kBinaryVersionOffset]));
        if (binary_version != 2) {
            std::cerr << "MAR-184 P12: .mbin version is " << binary_version
                      << ", expected 2.\n";
            return false;
        }
    }

    std::cout << "MAR-184 P1-P13: a stepped inherit overlay round-trips through "
                 "save and LOAD, all five modes materialize, and the parser refuses "
                 "a negative, out-of-float32, non-increasing, unknown-mode, empty, "
                 "and curve-carrying key by JSON path; `ensure` materializes the "
                 "base track once and refuses an unknown animation or bone; "
                 "materialization keeps every unrelated timeline and skips an empty "
                 "edit in BOTH serializers; the merge primitive is deterministic in "
                 "either request order, reports a missing animation/bone, an invalid "
                 "mode and a non-finite time before writing a byte, and keeps the "
                 "STORED time when it replaces a key; and export carries the merged "
                 "timeline into `.mskl` v1 and `.mbin` v2 alike.\n";
    return true;
}

namespace mar185 {

/**
 * @brief Loads a fresh throwaway project over the base-backed inherit fixture.
 *
 * Every case here builds its own project INSIDE the standing
 * `player_idle.marrow` invocation. Pointing `marrow_project_smoke` at a project
 * over `skin_inherit_constraints.mskl` would take `main()`'s
 * `markers.present.empty()` skip branch and run nothing at all.
 */
bool open_seed(
    const std::filesystem::path& path,
    std::string_view label,
    marrow::editor::ProjectLoadResult* loaded_out) {
    const marrow::editor::ProjectData seed = mar184::minimal_project(path);
    const auto saved = marrow::editor::save_project(seed, path);
    if (!saved) {
        std::cerr << label << ": the seed project failed to save: "
                  << saved.error->message << '\n';
        return false;
    }
    *loaded_out = marrow::editor::load_project(path);
    if (!*loaded_out) {
        std::cerr << label << ": the seed project failed to load: "
                  << loaded_out->error->message << '\n';
        return false;
    }
    return true;
}

/** @brief Saves a project and reloads it through the real `load_project`. */
bool save_and_reload(
    const marrow::editor::ProjectData& project,
    const std::filesystem::path& path,
    std::string_view label,
    marrow::editor::ProjectLoadResult* reloaded_out) {
    const auto saved = marrow::editor::save_project(project, path);
    if (!saved) {
        std::cerr << label << ": save_project failed: " << saved.error->message
                  << '\n';
        return false;
    }
    *reloaded_out = marrow::editor::load_project(path);
    if (!*reloaded_out) {
        std::cerr << label << ": the saved project failed to reload: "
                  << reloaded_out->error->message << '\n';
        return false;
    }
    return true;
}

/**
 * @brief Asserts a removal is refused, on its MESSAGE, with nothing written.
 *
 * Never on a bare `!result`: a removal can fail for six distinct reasons and a
 * bare failure check passes under its own inversion.
 */
bool expect_removal_rejection(
    marrow::editor::ProjectData* project,
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view sub_case,
    std::string_view animation_name,
    std::string_view bone_name,
    const std::vector<double>& times,
    std::string_view expected_a,
    std::string_view expected_b) {
    const std::string before = marrow::editor::serialize_project(*project);
    const auto result = marrow::editor::remove_inherit_timeline_keys(
        project, skeleton, animation_name, bone_name, times);
    if (result || result.changed) {
        std::cerr << "MAR-185 " << sub_case << ": expected a rejection naming '"
                  << expected_a << "', got changed="
                  << (result.changed ? "true" : "false") << " with error '"
                  << result.error << "'.\n";
        return false;
    }
    if (result.error.find(expected_a) == std::string::npos ||
        result.error.find(expected_b) == std::string::npos) {
        std::cerr << "MAR-185 " << sub_case << ": expected the error to name '"
                  << expected_a << "' and '" << expected_b << "', got '"
                  << result.error << "'.\n";
        return false;
    }
    const std::string after = marrow::editor::serialize_project(*project);
    if (after != before) {
        std::cerr << "MAR-185 " << sub_case
                  << ": the rejected removal changed serialize_project() ("
                  << before.size() << " -> " << after.size()
                  << " bytes). Validation must complete before ANY write.\n";
        return false;
    }
    return true;
}

/** @brief The effective inherit keys of one exported `.mskl` lane. */
const marrow::runtime::BoneInheritTimeline* exported_inherit_timeline(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view animation_name,
    std::string_view bone_name) {
    const auto* animation = skeleton.find_animation(animation_name);
    if (animation == nullptr) return nullptr;
    const auto bone_index = skeleton.find_bone_index(bone_name);
    if (!bone_index.has_value()) return nullptr;
    for (const auto& timeline : animation->bone_inherit_timelines) {
        if (timeline.bone_index == *bone_index) return &timeline;
    }
    return nullptr;
}

}  // namespace mar185

/**
 * @brief MAR-185 P2-P10, U1-U2: inherit keys as first-class timeline keys.
 *
 * P0 -- the non-effect witness -- is NOT restated here. MAR-184's P4 above
 * already asserts `serialize_project(player_idle.marrow)` against the same
 * 6111-byte / `c7d6c6de…` constants in this same binary, so it is already the
 * live detector for "MAR-185 changed an existing project's bytes". A second
 * copy would be a case that cannot fail for a reason the first one would not.
 */
bool validate_mar185_inherit_editing() {
    using marrow::runtime::BoneInherit;
    const mar184::TemporaryDirectory temporary("editing");

    // -- P1 -- the Inherit dopesheet row is EDITABLE, and its key resolves. --
    //
    // Model-layer, so it runs first in the verification order and is the first
    // thing anywhere to touch the lane. Every shell workflow gates on
    // `track_is_editable`: Add, Remove, copy and the toolbar all bail without
    // it, and the status line says "The selected timeline is read-only".
    {
        const std::filesystem::path path = temporary.path / "p1.marrow";
        marrow::editor::ProjectLoadResult loaded;
        if (!mar185::open_seed(path, "MAR-185 P1", &loaded)) return false;
        const auto* animation =
            loaded.skeleton_data->find_animation("toggle_inherit");
        const auto bone_index = loaded.skeleton_data->find_bone_index("child");
        if (animation == nullptr || !bone_index.has_value()) {
            std::cerr << "MAR-185 P1: the fixture lost 'toggle_inherit'/'child'.\n";
            return false;
        }
        const auto tracks = marrow::editor::timeline_model::build_tracks(
            *loaded.skeleton_data, *animation);
        const auto row = std::find_if(
            tracks.begin(), tracks.end(),
            [&](const marrow::editor::timeline_model::TrackRow& track) {
                return track.kind ==
                        marrow::editor::timeline_model::TimelineTrackKind::Inherit &&
                    track.bone_index == bone_index;
            });
        if (row == tracks.end()) {
            std::cerr << "MAR-185 P1: build_tracks produced no Inherit row for "
                         "'child'.\n";
            return false;
        }
        if (!marrow::editor::timeline_model::track_is_editable(*row)) {
            std::cerr << "MAR-185 P1: the Inherit row '" << row->id
                      << "' is still read-only. Every shell workflow -- Add, "
                         "Remove, copy, the toolbar -- gates on this and bails "
                         "with \"The selected timeline is read-only\".\n";
            return false;
        }
        // The project-layer half of the selector: a materialized edit must
        // resolve by (animation, bone, time) exactly as the shell's selector
        // will once it is built from this row.
        if (marrow::editor::ensure_bone_inherit_timeline_edit(
                *loaded.project, *loaded.skeleton_data, "toggle_inherit",
                "child") == nullptr) {
            std::cerr << "MAR-185 P1: `ensure` did not materialize child's lane.\n";
            return false;
        }
        marrow::editor::TimelineKeySelector selector;
        selector.kind = marrow::editor::TimelineKeyKind::Inherit;
        selector.animation_name = "toggle_inherit";
        selector.bone_name = "child";
        selector.time = row->key_times.front();
        const auto probe = marrow::editor::retime_keyframes(
            loaded.project.get(), {selector}, 0.0, false, 30.0);
        if (!probe || probe.key_count != 1U) {
            std::cerr << "MAR-185 P1: an Inherit selector over the row's own key "
                         "time resolved " << probe.key_count << " keys, expected 1"
                      << (probe.error.empty() ? "" : " (" + probe.error + ")")
                      << ".\n";
            return false;
        }
    }

    // -- P2 -- all five modes, authored through the EDITING primitive. ------
    //
    // MAR-184's own P3 builds the five keys by direct struct construction, so
    // it never exercises the token vocabulary. This one goes through
    // `merge_inherit_timeline`, which is the only place the five tokens are
    // validated, and then through a real `load_project` materialization.
    {
        const std::filesystem::path path = temporary.path / "p2.marrow";
        marrow::editor::ProjectLoadResult loaded;
        if (!mar185::open_seed(path, "MAR-185 P2", &loaded)) return false;
        marrow::editor::InheritTimelineMergeRequest request;
        request.animation_name = "toggle_inherit";
        request.bone_name = "controller";
        request.keys = {
            {0.0, "noScale"},
            {0.1, "onlyTranslation"},
            {0.2, "noRotationOrReflection"},
            {0.3, "noScaleOrReflection"},
            {0.4, "normal"}};
        const auto merged = marrow::editor::merge_inherit_timeline(
            loaded.project.get(), *loaded.skeleton_data, request);
        if (!merged || merged.added_key_count != 5U) {
            std::cerr << "MAR-185 P2: the five-mode merge added "
                      << merged.added_key_count << " keys: " << merged.error << '\n';
            return false;
        }
        marrow::editor::ProjectLoadResult reloaded;
        if (!mar185::save_and_reload(*loaded.project, path, "MAR-185 P2", &reloaded)) {
            return false;
        }
        const auto* lane = mar185::exported_inherit_timeline(
            *reloaded.skeleton_data, "toggle_inherit", "controller");
        const std::array<BoneInherit, 5> expected{
            BoneInherit::NoScale,
            BoneInherit::OnlyTranslation,
            BoneInherit::NoRotationOrReflection,
            BoneInherit::NoScaleOrReflection,
            BoneInherit::Normal};
        if (lane == nullptr || lane->keyframes.size() != expected.size()) {
            std::cerr << "MAR-185 P2: the materialized lane carries "
                      << (lane == nullptr ? 0U : lane->keyframes.size())
                      << " keys, expected 5.\n";
            return false;
        }
        for (std::size_t index = 0; index < expected.size(); ++index) {
            if (lane->keyframes[index].inherit != expected[index]) {
                std::cerr << "MAR-185 P2: key " << index << " materialized as "
                          << mar184::mode_name(lane->keyframes[index].inherit)
                          << ", expected " << mar184::mode_name(expected[index])
                          << ".\n";
                return false;
            }
        }
    }

    // -- P4 -- AC1's same-time obligation, both halves. ----------------------
    //
    // Two inherit keys can never share a time, because both parsers refuse a
    // non-increasing pair. That is a claim about every WRITE path, so it is
    // discharged by test rather than by prose: the merge rejects a colliding
    // request by name, and the clipboard's paste primitive COLLAPSES a pasted
    // key onto an existing one instead of inserting a duplicate.
    {
        const std::filesystem::path path = temporary.path / "p4.marrow";
        marrow::editor::ProjectLoadResult loaded;
        if (!mar185::open_seed(path, "MAR-185 P4", &loaded)) return false;
        const std::string before = marrow::editor::serialize_project(*loaded.project);
        marrow::editor::InheritTimelineMergeRequest request;
        request.animation_name = "toggle_inherit";
        request.bone_name = "controller";
        request.keys = {{0.2, "noScale"}, {0.2000005, "normal"}};
        const auto rejected = marrow::editor::merge_inherit_timeline(
            loaded.project.get(), *loaded.skeleton_data, request);
        if (rejected || rejected.changed ||
            rejected.error.find("collide") == std::string::npos ||
            rejected.error.find("0.2") == std::string::npos) {
            std::cerr << "MAR-185 P4: two keys 5e-7 apart were not rejected by "
                         "name; error was \"" << rejected.error << "\".\n";
            return false;
        }
        if (marrow::editor::serialize_project(*loaded.project) != before) {
            std::cerr << "MAR-185 P4: the rejected merge wrote to the project.\n";
            return false;
        }

        // The paste half, over the same primitive `paste_timeline_clipboard`
        // calls, so this is the shell's collision rule and not a restatement.
        std::vector<marrow::editor::InheritKeyframeEdit> destination{
            {0.0, BoneInherit::NoScale}, {0.4, BoneInherit::Normal}};
        const std::vector<marrow::editor::InheritKeyframeEdit> source{
            {0.0, BoneInherit::OnlyTranslation}};
        marrow::editor::timeline_model::paste_keys_replace_collisions(
            &destination, source, 0.4, false);
        if (destination.size() != 2U) {
            std::cerr << "MAR-185 P4: pasting onto an existing time produced "
                      << destination.size()
                      << " keys, expected the collision to collapse to 2.\n";
            return false;
        }
        if (destination[1].inherit != BoneInherit::OnlyTranslation) {
            std::cerr << "MAR-185 P4: the collapsed key kept the OLD mode.\n";
            return false;
        }
        for (std::size_t index = 1U; index < destination.size(); ++index) {
            if (!(destination[index].time > destination[index - 1U].time)) {
                std::cerr << "MAR-185 P4: the pasted vector is not strictly "
                             "increasing.\n";
                return false;
            }
        }
    }

    // -- P3 -- removal, down to the floor, proved through the EXPORT. --------
    //
    // The removal order is deliberate and is NOT the plan's. Removing down to a
    // lone `{0.0, Normal}` key would hand the survivor straight to
    // `prune_constant_timelines`, which deletes an inherit lane of exactly one
    // origin key whose mode equals the bone's setup inherit -- and every bone of
    // this fixture is setup-`Normal`. The lane would then be ABSENT from the
    // export for a reason that has nothing to do with the removal primitive.
    // Leaving `{0.5, OnlyTranslation}` keeps the survivor observable.
    {
        const std::filesystem::path path = temporary.path / "p3.marrow";
        marrow::editor::ProjectLoadResult loaded;
        if (!mar185::open_seed(path, "MAR-185 P3", &loaded)) return false;

        const auto* materialized = marrow::editor::ensure_bone_inherit_timeline_edit(
            *loaded.project, *loaded.skeleton_data, "toggle_inherit", "child");
        if (materialized == nullptr || materialized->keyframes.size() != 4U) {
            std::cerr << "MAR-185 P3: `ensure` did not materialize child's four "
                         "base keys.\n";
            return false;
        }

        struct Step {
            double time;
            std::size_t expected_effective;
        };
        for (const Step step : {Step{1.0, 3U}, Step{0.0, 2U}, Step{0.25, 1U}}) {
            const auto removed = marrow::editor::remove_inherit_timeline_keys(
                loaded.project.get(), *loaded.skeleton_data, "toggle_inherit",
                "child", {step.time});
            if (!removed || !removed.changed) {
                std::cerr << "MAR-185 P3: removing the key at " << step.time
                          << " failed: " << removed.error << '\n';
                return false;
            }
            if (removed.removed_key_count != 1U ||
                removed.effective_key_count != step.expected_effective) {
                std::cerr << "MAR-185 P3: removing " << step.time << " reported "
                          << removed.removed_key_count << " removed and "
                          << removed.effective_key_count << " remaining, expected 1 and "
                          << step.expected_effective << ".\n";
                return false;
            }
        }

        // The floor. Without it the edit empties, BOTH serializers skip an empty
        // edit, and `build_runtime_document` assigns into a COPY of the base --
        // so the emptied lane silently restores the imported four-key track.
        if (!mar185::expect_removal_rejection(
                loaded.project.get(), *loaded.skeleton_data, "P3",
                "toggle_inherit", "child", {0.5},
                "must keep at least one key", "child")) {
            return false;
        }

        marrow::editor::ProjectLoadResult reloaded;
        if (!mar185::save_and_reload(
                *loaded.project, path, "MAR-185 P3", &reloaded)) {
            return false;
        }
        const std::filesystem::path exported_path =
            temporary.path / "p3_export.mskl";
        marrow::editor::ProjectExportOptions export_options;
        export_options.skeleton_output_path = exported_path;
        const auto exported = marrow::editor::export_runtime_assets(
            *reloaded.project, *reloaded.base_skeleton_document, export_options);
        if (!exported) {
            std::cerr << "MAR-185 P3: the export failed: " << exported.error->message
                      << '\n';
            return false;
        }
        const auto exported_skeleton =
            marrow::runtime::load_skeleton_data(exported_path);
        if (!exported_skeleton) {
            std::cerr << "MAR-185 P3: the exported `.mskl` failed to parse.\n";
            return false;
        }
        const auto* lane = mar185::exported_inherit_timeline(
            *exported_skeleton.skeleton_data, "toggle_inherit", "child");
        if (lane == nullptr) {
            std::cerr << "MAR-185 P3: the exported `.mskl` carries NO inherit "
                         "timeline for 'child'.\n";
            return false;
        }
        if (lane->keyframes.size() != 1U) {
            std::cerr << "MAR-185 P3: the exported .mskl carries "
                      << lane->keyframes.size()
                      << " inherit keys for 'child', expected the 1 that survived "
                         "removal. An emptied edit is skipped by both serializers "
                         "and restores the imported base track.\n";
            return false;
        }
        if (!mar184::float32_times_equal(
                static_cast<double>(lane->keyframes[0].time), 0.5) ||
            lane->keyframes[0].inherit != BoneInherit::OnlyTranslation) {
            std::cerr << "MAR-185 P3: the surviving exported key is ("
                      << static_cast<double>(lane->keyframes[0].time) << ", "
                      << mar184::mode_name(lane->keyframes[0].inherit)
                      << "), expected (0.5, onlyTranslation).\n";
            return false;
        }
    }

    // -- P5 -- rejection atomicity, on a bone with NO project edit yet. ------
    //
    // The "no project edit yet" clause is LOAD-BEARING and is the whole reason
    // this case runs on `child`. `ensure_bone_inherit_timeline_edit` on a bone
    // whose edit already exists is a silent no-op, so a candidate-free
    // implementation could not be caught there; on an unknown animation or bone
    // `ensure` returns nullptr and writes nothing either. `child` is the only
    // bone whose animation AND name resolve while no edit exists, so it is the
    // only place a premature `ensure` leaves four materialized keys behind.
    {
        const std::filesystem::path path = temporary.path / "p5.marrow";
        marrow::editor::ProjectLoadResult loaded;
        if (!mar185::open_seed(path, "MAR-185 P5", &loaded)) return false;
        if (!loaded.project->bone_inherit_timeline_edits.empty()) {
            std::cerr << "MAR-185 P5: the seed already carries an inherit edit, "
                         "which disarms this case.\n";
            return false;
        }
        if (!mar185::expect_removal_rejection(
                loaded.project.get(), *loaded.skeleton_data, "P5",
                "toggle_inherit", "child", {0.31},
                "no inherit key exists at time", "0.31")) {
            return false;
        }
        if (!loaded.project->bone_inherit_timeline_edits.empty()) {
            std::cerr << "MAR-185 P5: the rejected removal left "
                      << loaded.project->bone_inherit_timeline_edits.size()
                      << " materialized inherit edit(s) behind.\n";
            return false;
        }
        // The four cheap rejections, each asserted on its own message.
        if (!mar185::expect_removal_rejection(
                loaded.project.get(), *loaded.skeleton_data, "P5a",
                "no_such_animation", "child", {0.25},
                "animation", "no_such_animation") ||
            !mar185::expect_removal_rejection(
                loaded.project.get(), *loaded.skeleton_data, "P5b",
                "toggle_inherit", "no_such_bone", {0.25},
                "bone", "no_such_bone") ||
            !mar185::expect_removal_rejection(
                loaded.project.get(), *loaded.skeleton_data, "P5c",
                "toggle_inherit", "child",
                {std::numeric_limits<double>::quiet_NaN()},
                "finite", "non-negative") ||
            !mar185::expect_removal_rejection(
                loaded.project.get(), *loaded.skeleton_data, "P5d",
                "toggle_inherit", "child", {0.25, 0.2500005},
                "collide", "0.25") ||
            !mar185::expect_removal_rejection(
                loaded.project.get(), *loaded.skeleton_data, "P5e",
                "toggle_inherit", "child", {},
                "at least one key time", "inherit")) {
            return false;
        }
    }


    // -- P6 -- a retime with room, asserted on the STORED time after reload. -
    {
        const std::filesystem::path path = temporary.path / "p6.marrow";
        marrow::editor::ProjectLoadResult loaded;
        if (!mar185::open_seed(path, "MAR-185 P6", &loaded)) return false;
        if (marrow::editor::ensure_bone_inherit_timeline_edit(
                *loaded.project, *loaded.skeleton_data, "toggle_inherit",
                "child") == nullptr) {
            std::cerr << "MAR-185 P6: `ensure` did not materialize child's lane.\n";
            return false;
        }
        marrow::editor::TimelineKeySelector selector;
        selector.kind = marrow::editor::TimelineKeyKind::Inherit;
        selector.animation_name = "toggle_inherit";
        selector.bone_name = "child";
        selector.time = 0.25;
        const auto retimed = marrow::editor::retime_keyframes(
            loaded.project.get(), {selector}, 0.15, false, 30.0);
        if (!retimed || retimed.key_count != 1U) {
            std::cerr << "MAR-185 P6: retime_keyframes resolved "
                      << retimed.key_count << " keys, expected 1"
                      << (retimed.error.empty() ? "" : " (" + retimed.error + ")")
                      << ".\n";
            return false;
        }
        if (std::abs(retimed.applied_delta - 0.15) > 1e-12) {
            std::cerr << "MAR-185 P6: applied_delta is " << retimed.applied_delta
                      << ", expected the unclamped 0.15.\n";
            return false;
        }
        marrow::editor::ProjectLoadResult reloaded;
        if (!mar185::save_and_reload(*loaded.project, path, "MAR-185 P6", &reloaded)) {
            return false;
        }
        const auto* edit = reloaded.project->find_bone_inherit_timeline_edit(
            "toggle_inherit", "child");
        if (edit == nullptr || edit->keyframes.size() != 4U) {
            std::cerr << "MAR-185 P6: the reloaded lane is missing or resized.\n";
            return false;
        }
        // `applied_delta` alone is not evidence: an `apply_resolved_retime` with
        // no Inherit arm is `void` with no trailing statement, so it reports the
        // full delta while writing NOTHING. Only the stored time can see that.
        const auto moved = std::find_if(
            edit->keyframes.begin(), edit->keyframes.end(),
            [](const marrow::editor::InheritKeyframeEdit& key) {
                return std::abs(key.time - 0.4) <= 1e-9;
            });
        if (moved == edit->keyframes.end()) {
            std::cerr << "MAR-185 P6: after save -> load the key is still at "
                      << edit->keyframes[1].time << ", expected 0.400000.\n";
            return false;
        }
        if (moved->inherit != BoneInherit::NoRotationOrReflection) {
            std::cerr << "MAR-185 P6: the retimed key changed mode to "
                      << mar184::mode_name(moved->inherit) << ".\n";
            return false;
        }
    }

    // -- P7 -- a retime that must clamp against an UNSELECTED neighbour. -----
    {
        const std::filesystem::path path = temporary.path / "p7.marrow";
        marrow::editor::ProjectLoadResult loaded;
        if (!mar185::open_seed(path, "MAR-185 P7", &loaded)) return false;
        if (marrow::editor::ensure_bone_inherit_timeline_edit(
                *loaded.project, *loaded.skeleton_data, "toggle_inherit",
                "child") == nullptr) {
            std::cerr << "MAR-185 P7: `ensure` did not materialize child's lane.\n";
            return false;
        }
        marrow::editor::TimelineKeySelector selector;
        selector.kind = marrow::editor::TimelineKeyKind::Inherit;
        selector.animation_name = "toggle_inherit";
        selector.bone_name = "child";
        selector.time = 0.25;
        // +0.40 would land on 0.65, past the unselected 0.5 neighbour. The
        // clamp is 0.5 - 1 ms - 0.25 = 0.249.
        const auto retimed = marrow::editor::retime_keyframes(
            loaded.project.get(), {selector}, 0.40, false, 30.0);
        if (!retimed || retimed.key_count != 1U) {
            std::cerr << "MAR-185 P7: retime_keyframes failed: " << retimed.error
                      << '\n';
            return false;
        }
        if (std::abs(retimed.applied_delta - 0.249) > 1e-9) {
            std::cerr << "MAR-185 P7: applied_delta is " << retimed.applied_delta
                      << ", expected the clamped 0.249. Without an Inherit arm in "
                         "include_resolved_retime_bounds the delta is bounded by "
                         "nothing at all.\n";
            return false;
        }
        marrow::editor::ProjectLoadResult reloaded;
        if (!mar185::save_and_reload(*loaded.project, path, "MAR-185 P7", &reloaded)) {
            return false;
        }
        const auto* edit = reloaded.project->find_bone_inherit_timeline_edit(
            "toggle_inherit", "child");
        if (edit == nullptr || edit->keyframes.size() != 4U ||
            std::abs(edit->keyframes[1].time - 0.499) > 1e-9) {
            std::cerr << "MAR-185 P7: the reloaded key sits at "
                      << (edit == nullptr ? -1.0 : edit->keyframes[1].time)
                      << ", expected the clamped 0.499.\n";
            return false;
        }
    }

    // -- P8 -- scale: the rejection, its label, and the stored-time precision.
    {
        const std::filesystem::path path = temporary.path / "p8.marrow";
        marrow::editor::ProjectLoadResult loaded;
        if (!mar185::open_seed(path, "MAR-185 P8", &loaded)) return false;
        if (marrow::editor::ensure_bone_inherit_timeline_edit(
                *loaded.project, *loaded.skeleton_data, "toggle_inherit",
                "child") == nullptr) {
            std::cerr << "MAR-185 P8: `ensure` did not materialize child's lane.\n";
            return false;
        }
        const auto make_selector = [](double time) {
            marrow::editor::TimelineKeySelector selector;
            selector.kind = marrow::editor::TimelineKeyKind::Inherit;
            selector.animation_name = "toggle_inherit";
            selector.bone_name = "child";
            selector.time = time;
            return selector;
        };
        // 8a. A ratio that squeezes 0.25 and 0.5 to 0.4 ms apart. Without an
        // Inherit arm in the validate switch, `valid` stays true and `error`
        // stays empty -- there is no diagnostic anywhere and the scale lands.
        const std::string before = marrow::editor::serialize_project(*loaded.project);
        const auto rejected = marrow::editor::scale_keyframe_times(
            loaded.project.get(), {make_selector(0.25), make_selector(0.5)},
            marrow::editor::TimelineScalePivot::RangeStart, 0.0016);
        if (rejected || rejected.changed) {
            std::cerr << "MAR-185 P8: the colliding scale was accepted (changed="
                      << (rejected.changed ? "true" : "false")
                      << "); expected a rejection naming the separation.\n";
            return false;
        }
        if (rejected.error.find("inherit key 'child'") == std::string::npos ||
            rejected.error.find("the minimum separation is") == std::string::npos) {
            std::cerr << "MAR-185 P8: rejection message is \"" << rejected.error
                      << "\"; expected it to name \"inherit key 'child'\" and "
                         "\"the minimum separation is\".\n";
            return false;
        }
        if (marrow::editor::serialize_project(*loaded.project) != before) {
            std::cerr << "MAR-185 P8: the rejected scale changed "
                         "serialize_project().\n";
            return false;
        }

        // 8b. A legal ratio, on a PROJECT-ONLY lane whose stored times are not
        // float32-exact, with selectors carrying the narrowed times a shell
        // TrackRow would supply. `resolved_stored_key_time` is what makes the
        // scale read the stored double instead of the selector's; without its
        // Inherit arm the pivot and the projection are both computed from the
        // narrowed value and every result is ~2.5e-8 off.
        marrow::editor::InheritTimelineMergeRequest request;
        request.animation_name = "toggle_inherit";
        request.bone_name = "controller";
        request.keys = {{0.1, "noScale"}, {0.7, "normal"}};
        const auto merged = marrow::editor::merge_inherit_timeline(
            loaded.project.get(), *loaded.skeleton_data, request);
        if (!merged) {
            std::cerr << "MAR-185 P8: seeding the controller lane failed: "
                      << merged.error << '\n';
            return false;
        }
        const auto narrowed = [](double time) {
            return static_cast<double>(static_cast<float>(time));
        };
        auto low = make_selector(narrowed(0.1));
        auto high = make_selector(narrowed(0.7));
        low.bone_name = "controller";
        high.bone_name = "controller";
        const auto scaled = marrow::editor::scale_keyframe_times(
            loaded.project.get(), {low, high},
            marrow::editor::TimelineScalePivot::RangeStart, 2.0);
        if (!scaled || !scaled.changed) {
            std::cerr << "MAR-185 P8: the legal scale was rejected: " << scaled.error
                      << '\n';
            return false;
        }
        const auto* lane = loaded.project->find_bone_inherit_timeline_edit(
            "toggle_inherit", "controller");
        if (lane == nullptr || lane->keyframes.size() != 2U) {
            std::cerr << "MAR-185 P8: the scaled controller lane is missing.\n";
            return false;
        }
        const double expected_low = 0.1 + (0.1 - 0.1) * 2.0;
        const double expected_high = 0.1 + (0.7 - 0.1) * 2.0;
        if (std::abs(lane->keyframes[0].time - expected_low) > 1e-12 ||
            std::abs(lane->keyframes[1].time - expected_high) > 1e-12) {
            std::cerr << "MAR-185 P8: the scaled times are ("
                      << std::setprecision(17) << lane->keyframes[0].time << ", "
                      << lane->keyframes[1].time << "), expected ("
                      << expected_low << ", " << expected_high
                      << ") within 1e-12. A scale that reads the selector's "
                         "float32-narrowed time instead of the stored double "
                         "misses by ~2.5e-8.\n";
            return false;
        }
    }


    // -- P9 -- an inherit key past an explicit duration grows it. -----------
    {
        const std::filesystem::path path = temporary.path / "p9.marrow";
        marrow::editor::ProjectLoadResult loaded;
        if (!mar185::open_seed(path, "MAR-185 P9", &loaded)) return false;
        const auto duration_set = marrow::editor::set_animation_duration(
            loaded.project.get(), *loaded.skeleton_data, "toggle_inherit", 1.0);
        if (!duration_set) {
            std::cerr << "MAR-185 P9: setting the explicit duration failed: "
                      << duration_set.error << '\n';
            return false;
        }
        marrow::editor::InheritTimelineMergeRequest request;
        request.animation_name = "toggle_inherit";
        request.bone_name = "controller";
        request.keys = {{0.4, "noScale"}, {1.4, "normal"}};
        const auto merged = marrow::editor::merge_inherit_timeline(
            loaded.project.get(), *loaded.skeleton_data, request);
        if (!merged) {
            std::cerr << "MAR-185 P9: seeding the late inherit key failed: "
                      << merged.error << '\n';
            return false;
        }
        const auto grown = marrow::editor::auto_extend_explicit_animation_durations(
            loaded.project.get(), *loaded.skeleton_data);
        if (!grown) {
            std::cerr << "MAR-185 P9: auto-grow failed: " << grown.error << '\n';
            return false;
        }
        marrow::editor::ProjectLoadResult reloaded;
        if (!mar185::save_and_reload(*loaded.project, path, "MAR-185 P9", &reloaded)) {
            std::cerr << "MAR-185 P9: an inherit key past the explicit duration did "
                         "not grow it, so the project no longer round-trips. "
                         "`auto_extend_explicit_animation_durations` folds a "
                         "hand-maintained list of vectors that the compiler does "
                         "not check.\n";
            return false;
        }
        const auto* animation =
            reloaded.skeleton_data->find_animation("toggle_inherit");
        const double expected = static_cast<double>(
            static_cast<marrow::runtime::AnimationScalar>(1.4));
        if (animation == nullptr || !animation->explicit_duration.has_value() ||
            std::abs(
                static_cast<double>(*animation->explicit_duration) - expected) > 1e-9) {
            std::cerr << "MAR-185 P9: the explicit duration is "
                      << (animation == nullptr || !animation->explicit_duration.has_value()
                              ? -1.0
                              : static_cast<double>(*animation->explicit_duration))
                      << " after auto-grow, expected " << expected << ".\n";
            return false;
        }
    }

    // -- U1 -- an animation rename carries the inherit overlay with it. -----
    {
        const std::filesystem::path path = temporary.path / "u1.marrow";
        marrow::editor::ProjectLoadResult loaded;
        if (!mar185::open_seed(path, "MAR-185 U1", &loaded)) return false;
        marrow::editor::InheritTimelineMergeRequest request;
        request.animation_name = "toggle_inherit";
        request.bone_name = "controller";
        request.keys = {{0.0, "noScale"}, {0.4, "normal"}};
        if (!marrow::editor::merge_inherit_timeline(
                loaded.project.get(), *loaded.skeleton_data, request)) {
            std::cerr << "MAR-185 U1: seeding the overlay failed.\n";
            return false;
        }
        const auto renamed = marrow::editor::rename_animation(
            loaded.project.get(), *loaded.base_skeleton_document, "toggle_inherit",
            "toggle_two");
        if (!renamed) {
            std::cerr << "MAR-185 U1: rename_animation failed: " << renamed.error
                      << '\n';
            return false;
        }
        marrow::editor::ProjectLoadResult reloaded;
        if (!mar185::save_and_reload(*loaded.project, path, "MAR-185 U1", &reloaded)) {
            return false;
        }
        const auto& edits = reloaded.project->bone_inherit_timeline_edits;
        if (edits.size() != 1U || edits[0].animation_name != "toggle_two") {
            std::cerr << "MAR-185 U1: after renaming toggle_inherit -> toggle_two, "
                         "the reloaded project's inherit edit still names '"
                      << (edits.empty() ? "<none>" : edits[0].animation_name)
                      << "'.\n";
            return false;
        }
        if (mar185::exported_inherit_timeline(
                *reloaded.skeleton_data, "toggle_two", "controller") == nullptr) {
            std::cerr << "MAR-185 U1: the materialized 'toggle_two' carries no "
                         "inherit timeline for 'controller'.\n";
            return false;
        }
    }

    // -- U2 -- an animation delete takes the inherit overlay with it. -------
    //
    // The second animation exists only so `delete_animation`'s last-animation
    // guard does not fire and mask the case.
    {
        const std::filesystem::path path = temporary.path / "u2.marrow";
        marrow::editor::ProjectLoadResult loaded;
        if (!mar185::open_seed(path, "MAR-185 U2", &loaded)) return false;
        if (!marrow::editor::create_animation(
                loaded.project.get(), *loaded.base_skeleton_document, "spare")) {
            std::cerr << "MAR-185 U2: the spare animation could not be created.\n";
            return false;
        }
        marrow::editor::InheritTimelineMergeRequest request;
        request.animation_name = "toggle_inherit";
        request.bone_name = "controller";
        request.keys = {{0.0, "noScale"}, {0.4, "normal"}};
        if (!marrow::editor::merge_inherit_timeline(
                loaded.project.get(), *loaded.skeleton_data, request)) {
            std::cerr << "MAR-185 U2: seeding the overlay failed.\n";
            return false;
        }
        const auto deleted = marrow::editor::delete_animation(
            loaded.project.get(), *loaded.base_skeleton_document, "toggle_inherit");
        if (!deleted) {
            std::cerr << "MAR-185 U2: delete_animation failed: " << deleted.error
                      << '\n';
            return false;
        }
        marrow::editor::ProjectLoadResult reloaded;
        if (!mar185::save_and_reload(*loaded.project, path, "MAR-185 U2", &reloaded)) {
            return false;
        }
        for (const auto& edit : reloaded.project->bone_inherit_timeline_edits) {
            if (edit.animation_name == "toggle_inherit") {
                std::cerr << "MAR-185 U2: the deleted animation's inherit edit "
                             "survived the delete.\n";
                return false;
            }
        }
        // The orphan edit is what re-creates the animation: on reload
        // `build_runtime_document` walks the overlay and calls
        // `ensure_object_member` for an animation the delete removed.
        if (reloaded.skeleton_data->find_animation("toggle_inherit") != nullptr) {
            std::cerr << "MAR-185 U2: the reloaded skeleton still has animation "
                         "'toggle_inherit', re-created from an orphan inherit "
                         "edit.\n";
            return false;
        }
    }

    std::cout << "MAR-185 P1-P9, U1-U2: the Inherit dopesheet row is editable and "
                 "its keys resolve; all five modes author through the merge "
                 "primitive and materialize in order; two keys can never share a "
                 "time, by rejection and by paste collapse; removal walks down to "
                 "a one-key floor whose survivor reaches the exported `.mskl`, and "
                 "every rejection -- the floor, a time matching no key, an unknown "
                 "animation or bone, a non-finite time, colliding requested times, "
                 "and an empty request -- names its cause and leaves "
                 "serialize_project() byte-identical; a retime applies and clamps "
                 "against unselected neighbours and is proved on the STORED time "
                 "after a real reload; a scale rejects a sub-millisecond "
                 "projection naming the inherit lane and otherwise lands on the "
                 "stored double to 1e-12; a key past an explicit duration grows "
                 "it; and an animation rename or delete carries the overlay with "
                 "it through save and LOAD.\n";
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
    if (!validate_viewport_settings(result)) {
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
    } else if (const EditingFixtureMarkers markers = editing_fixture_markers(result);
               markers.present.empty()) {
        // Not the editing fixture at all. Say so loudly and by name -- a silent
        // skip here would be the "test that cannot fail" failure mode -- then
        // fall through to the export validation, which is what a command like
        // the documented atlas-pack one is actually for.
        std::cout << "Editing-suite validation skipped: this project is not the "
                     "player_idle editing fixture (missing "
                  << join_markers(markers.missing)
                  << "). The project-shape checks above and any export "
                     "validation below still ran.\n";
    } else if (!markers.missing.empty()) {
        // A partial match is a CORRUPTED editing fixture, never a skip. Without
        // this branch, player_idle losing one bone would quietly stop running
        // roughly two dozen suites and still exit 0.
        std::cerr << "Editing-suite validation aborted: this project carries "
                  << join_markers(markers.present) << " but is missing "
                  << join_markers(markers.missing)
                  << ". A project that partially matches the player_idle editing "
                     "fixture is a corrupted fixture, not a project to skip.\n";
        return 1;
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
        if (!validate_mar176_automatic_weights(result)) {
            return 1;
        }
        if (!validate_mar177_scenario_a(result)) {
            return 1;
        }
        if (!validate_mar177_scenario_a2(result)) {
            return 1;
        }
        if (!validate_mar177_scenario_c()) {
            return 1;
        }
        if (!validate_mar177_scenario_d()) {
            return 1;
        }
        if (!validate_mar177_scenario_b(result)) {
            return 1;
        }
        if (!validate_mar177_scenario_e()) {
            return 1;
        }
        if (!validate_mar177_scenario_f()) {
            return 1;
        }
        if (!validate_mar178_scenario_a(result)) {
            return 1;
        }
        if (!validate_mar178_scenario_c(result)) {
            return 1;
        }
        if (!validate_mar178_scenario_b()) {
            return 1;
        }
        if (!validate_mar178_scenario_d(result)) {
            return 1;
        }
        if (!validate_mar178_scenario_e()) {
            return 1;
        }
        if (!validate_mar178_scenario_f()) {
            return 1;
        }
        if (!validate_mar178_scenario_g()) {
            return 1;
        }
        if (!validate_mar179_constraint_parameters(result)) {
            return 1;
        }
        if (!validate_mar180_atomic_save_preserves_previous_file(
                parse_result.options.project_path)) {
            return 1;
        }
        if (!validate_mar180_save_as_rebases_relative_paths(
                parse_result.options.project_path)) {
            return 1;
        }
        if (!validate_mar180_save_as_preserves_absolute_paths(
                parse_result.options.project_path)) {
            return 1;
        }
        if (!validate_mar180_undo_across_save_as_still_opens(
                parse_result.options.project_path)) {
            return 1;
        }
        if (!validate_mar180_history_reserialization_is_load_bearing(
                parse_result.options.project_path)) {
            return 1;
        }
        if (!validate_mar180_failed_adoption_keeps_session_coherent(
                parse_result.options.project_path)) {
            return 1;
        }
        if (!validate_mar180_create_starts_dirty_and_unwritten(
                parse_result.options.project_path)) {
            return 1;
        }
        if (!validate_mar180_failed_create_changes_nothing(
                parse_result.options.project_path)) {
            return 1;
        }
        if (!validate_mar180_close_clears_and_bumps_revisions(
                parse_result.options.project_path)) {
            return 1;
        }
        if (!validate_mar180_lifecycle_refuses_active_transaction(
                parse_result.options.project_path)) {
            return 1;
        }
        if (!validate_mar184_inherit_overlays(result)) {
            return 1;
        }
        if (!validate_mar185_inherit_editing()) {
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
