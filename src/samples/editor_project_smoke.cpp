#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "marrow/editor/project.hpp"
#include "marrow/editor/authoring.hpp"
#include "marrow/editor/session.hpp"
#include "marrow/runtime/animation_compare.hpp"

namespace {

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
    if (!debug_overlay.bones ||
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
    const auto* idle = reloaded.skeleton_data->find_animation("idle");
    const auto* translated =
        spine_index.has_value() && idle != nullptr
        ? idle->find_translate_timeline(*spine_index)
        : nullptr;
    if (translated == nullptr || translated->keyframes.size() != 4U ||
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
    if (!validate_binary_export(json_path, binary_path)) {
        return false;
    }

    std::cout << "Editing P0 auto-key/retime/save/reload/export E2E validated.\n";
    return true;
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
    if (!validate_undo_redo_cycle(result)) {
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
