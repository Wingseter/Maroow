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
