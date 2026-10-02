#include "marrow/editor/project.hpp"
#include "project_json.hpp"
#include "project_internal.hpp"
#include "mesh_weight_model.hpp"
#include "marrow/editor/authoring.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

namespace marrow::editor {
using namespace project_detail;
using marrow::runtime::json::Document;
using marrow::runtime::json::LoadError;
using marrow::runtime::json::SourceLocation;
using marrow::runtime::json::Value;

namespace project_detail {

std::optional<LoadError> validate_animation_edit_sequence(
    const ProjectData& project,
    const Document& base_skeleton_document) {
    if (project.animation_edits.empty()) {
        return std::nullopt;
    }
    const Value* animations = find_optional_member(base_skeleton_document.root, "animations");
    if (animations == nullptr || !animations->is_object()) {
        return validation_error(
            base_skeleton_document,
            base_skeleton_document.root.location(),
            "$.animation_edits",
            "the base skeleton has no animation catalog");
    }
    std::vector<std::string> names;
    names.reserve(animations->as_object().size() + project.animation_edits.size());
    for (const auto& [name, unused] : animations->as_object()) {
        (void)unused;
        names.push_back(name);
    }
    const auto contains = [&](std::string_view name) {
        return std::find(names.begin(), names.end(), name) != names.end();
    };
    for (std::size_t index = 0; index < project.animation_edits.size(); ++index) {
        const AnimationEdit& edit = project.animation_edits[index];
        const std::string path = "$.animation_edits[" + std::to_string(index) + "]";
        switch (edit.kind) {
        case AnimationEditKind::Create:
            if (contains(edit.name)) {
                return validation_error(
                    base_skeleton_document,
                    base_skeleton_document.root.location(),
                    path,
                    "create destination already exists: " + edit.name);
            }
            names.push_back(edit.name);
            break;
        case AnimationEditKind::Rename: {
            const auto source = std::find(names.begin(), names.end(), edit.name);
            if (source == names.end()) {
                return validation_error(
                    base_skeleton_document,
                    base_skeleton_document.root.location(),
                    path,
                    "rename source does not exist: " + edit.name);
            }
            if (contains(edit.new_name)) {
                return validation_error(
                    base_skeleton_document,
                    base_skeleton_document.root.location(),
                    path,
                    "rename destination already exists: " + edit.new_name);
            }
            *source = edit.new_name;
            break;
        }
        case AnimationEditKind::Delete: {
            const auto target = std::find(names.begin(), names.end(), edit.name);
            if (target == names.end()) {
                return validation_error(
                    base_skeleton_document,
                    base_skeleton_document.root.location(),
                    path,
                    "delete target does not exist: " + edit.name);
            }
            if (names.size() <= 1U) {
                return validation_error(
                    base_skeleton_document,
                    base_skeleton_document.root.location(),
                    path,
                    "the last animation cannot be deleted");
            }
            names.erase(target);
            break;
        }
        case AnimationEditKind::SetDuration:
            if (!contains(edit.name)) {
                return validation_error(
                    base_skeleton_document,
                    base_skeleton_document.root.location(),
                    path,
                    "set_duration target does not exist: " + edit.name);
            }
            if (!std::isfinite(edit.duration) || edit.duration < 0.0 ||
                edit.duration > static_cast<double>(
                    std::numeric_limits<runtime::AnimationScalar>::max())) {
                return validation_error(
                    base_skeleton_document,
                    base_skeleton_document.root.location(),
                    path + ".duration",
                    "duration must be finite, non-negative, and within the runtime float32 range");
            }
            break;
        case AnimationEditKind::Unknown:
            break;
        }
    }
    return std::nullopt;
}

bool validate_project_for_save(const ProjectData& project, ProjectSaveError* error_out) {
    if (project.marrow_version.empty()) {
        error_out->message = "project version must not be empty";
        return false;
    }
    if (project.runtime_assets.skeleton_path.empty()) {
        error_out->message = "runtime skeleton path must not be empty";
        return false;
    }
    if (project.runtime_assets.atlas_paths.empty()) {
        error_out->message = "at least one atlas path is required";
        return false;
    }
    if (project.editor_metadata.name.empty()) {
        error_out->message = "editor metadata name must not be empty";
        return false;
    }
    if (project.editor_metadata.viewport.zoom <= 0.0) {
        error_out->message = "editor viewport zoom must be greater than zero";
        return false;
    }
    if (!std::isfinite(project.editor_metadata.timeline.frames_per_second) ||
        project.editor_metadata.timeline.frames_per_second <= 0.0) {
        error_out->message = "editor timeline fps must be finite and greater than zero";
        return false;
    }
    if (project.editor_metadata.viewport.onion_skin.before_count < 0 ||
        project.editor_metadata.viewport.onion_skin.before_count > 6) {
        error_out->message = "editor onion-skin before count must stay within [0, 6]";
        return false;
    }
    if (project.editor_metadata.viewport.onion_skin.after_count < 0 ||
        project.editor_metadata.viewport.onion_skin.after_count > 6) {
        error_out->message = "editor onion-skin after count must stay within [0, 6]";
        return false;
    }
    if (project.editor_metadata.viewport.onion_skin.step <= 0) {
        error_out->message = "editor onion-skin step must be greater than zero";
        return false;
    }
    if (project.snap_settings.has_value()) {
        const ProjectSnapSettings& settings = *project.snap_settings;
        if (!std::isfinite(settings.world_grid_step) ||
            settings.world_grid_step <= 0.0 ||
            !std::isfinite(settings.local_angle_step_degrees) ||
            settings.local_angle_step_degrees <= 0.0 ||
            !std::isfinite(settings.absolute_scale_step) ||
            settings.absolute_scale_step <= 0.0) {
            error_out->message =
                "project snap steps must be finite and greater than zero";
            return false;
        }
    }

    // MAR-172 re-validates the two structural prerequisites a skeleton-free
    // validator can see, for the same reason the snap block re-validates its
    // steps: the loader's gate protects documents, and this one protects a
    // project mutated in memory.
    {
        const auto validate_lane = [&](const auto& edits) {
            for (const auto& edit : edits) {
                if (!edit.loop_sync) {
                    continue;
                }
                if (edit.keyframes.empty()) {
                    error_out->message =
                        "loop synchronized timelines require at least one keyframe";
                    return false;
                }
                if (std::abs(edit.keyframes.front().time) > 1e-6) {
                    error_out->message =
                        "loop synchronized timelines require a key at time zero";
                    return false;
                }
            }
            return true;
        };
        if (!validate_lane(project.transform_timeline_edits) ||
            !validate_lane(project.slot_color_timeline_edits) ||
            !validate_lane(project.mesh_deform_timeline_edits)) {
            return false;
        }
    }

    // MAR-171 re-validates driver authorability here for the same reason the
    // snap block re-validates its steps: the loader's gate protects documents,
    // and this one protects a project mutated in memory.
    for (const TransformTimelineEdit& edit : project.transform_timeline_edits) {
        for (const TransformKeyframeEdit& keyframe : edit.keyframes) {
            if (keyframe.curve_mode != TimelineCurveMode::Auto) continue;
            if (!curve_driver_is_authorable(
                    TimelineKeyKind::Transform, edit.channel, keyframe.curve_driver)) {
                error_out->message =
                    "automatic curve drivers must name a component the timeline owns";
                return false;
            }
        }
    }
    for (const SlotColorTimelineEdit& edit : project.slot_color_timeline_edits) {
        for (const SlotColorKeyframeEdit& keyframe : edit.keyframes) {
            if (keyframe.curve_mode != TimelineCurveMode::Auto) continue;
            if (!curve_driver_is_authorable(
                    TimelineKeyKind::SlotColor,
                    TransformTimelineChannel::Rotate,
                    keyframe.curve_driver)) {
                error_out->message =
                    "automatic curve drivers must name a component the timeline owns";
                return false;
            }
        }
    }

    for (const auto& atlas_path : project.runtime_assets.atlas_paths) {
        if (atlas_path.empty()) {
            error_out->message = "atlas paths must not be empty";
            return false;
        }
    }
    for (const std::string& preview_skin : project.editor_metadata.preview_skins) {
        if (preview_skin.empty()) {
            error_out->message = "preview skin names must not be empty";
            return false;
        }
    }

    if (project.parameter_model.has_value()) {
        const ParameterModel& model = *project.parameter_model;
        std::vector<std::string> parameter_ids;
        parameter_ids.reserve(model.parameters.size());
        for (const ParameterAuthoringDefinition& parameter : model.parameters) {
            if (parameter.id.empty() || parameter.name.empty()) {
                error_out->message = "parameter definitions require non-empty ids and names";
                return false;
            }
            if (!std::isfinite(parameter.min_value) ||
                !std::isfinite(parameter.max_value) ||
                !std::isfinite(parameter.default_value) ||
                parameter.min_value > parameter.max_value ||
                (parameter.clamp &&
                 (parameter.default_value < parameter.min_value ||
                  parameter.default_value > parameter.max_value))) {
                error_out->message =
                    "parameter ranges and defaults must be finite and ordered";
                return false;
            }
            if (parameter.ui_step.has_value() &&
                (!std::isfinite(*parameter.ui_step) || *parameter.ui_step <= 0.0)) {
                error_out->message = "parameter ui_step must be finite and positive";
                return false;
            }
            if (std::find(parameter_ids.begin(), parameter_ids.end(), parameter.id) !=
                parameter_ids.end()) {
                error_out->message = "parameter ids must be unique";
                return false;
            }
            parameter_ids.push_back(parameter.id);
        }

        std::vector<std::string> group_ids;
        for (const ParameterGroupAuthoringDefinition& group : model.groups) {
            if (group.id.empty() || group.name.empty()) {
                error_out->message = "parameter groups require non-empty ids and names";
                return false;
            }
            if (std::find(group_ids.begin(), group_ids.end(), group.id) != group_ids.end()) {
                error_out->message = "parameter group ids must be unique";
                return false;
            }
            group_ids.push_back(group.id);
            std::vector<std::string> group_parameter_ids;
            for (const std::string& parameter_id : group.parameter_ids) {
                if (std::find(parameter_ids.begin(), parameter_ids.end(), parameter_id) ==
                    parameter_ids.end()) {
                    error_out->message =
                        "parameter groups may only reference existing parameter ids";
                    return false;
                }
                if (std::find(
                        group_parameter_ids.begin(),
                        group_parameter_ids.end(),
                        parameter_id) != group_parameter_ids.end()) {
                    error_out->message = "parameter group references must be unique";
                    return false;
                }
                group_parameter_ids.push_back(parameter_id);
            }
        }

        const auto validate_typed_ids = [&](const auto& values, std::string_view family) {
            std::vector<std::string> ids;
            for (const auto& value : values) {
                if (value.id.empty()) {
                    error_out->message = std::string(family) +
                        " definitions require non-empty string ids";
                    return false;
                }
                if (std::find(ids.begin(), ids.end(), value.id) != ids.end()) {
                    error_out->message = std::string(family) + " ids must be unique";
                    return false;
                }
                ids.push_back(value.id);
            }
            return true;
        };
        if (!validate_typed_ids(model.blend_shapes, "blend shape") ||
            !validate_typed_ids(model.deformers, "deformer") ||
            !validate_typed_ids(model.art_paths, "art path") ||
            !validate_typed_ids(model.expressions, "expression")) {
            return false;
        }
    }

    for (const AnimationEdit& edit : project.animation_edits) {
        if (edit.kind == AnimationEditKind::Unknown) {
            const Value* operation = edit.preserved_source.is_object()
                ? find_optional_member(edit.preserved_source, "op")
                : nullptr;
            if (operation == nullptr || !operation->is_string() ||
                operation->as_string().empty()) {
                error_out->message =
                    "unknown animation edits require a preserved non-empty operation";
                return false;
            }
            continue;
        }
        if (edit.name.empty()) {
            error_out->message = "animation edits require non-empty animation names";
            return false;
        }
        if (edit.kind == AnimationEditKind::Create && !edit.animation.is_object()) {
            error_out->message = "animation create edits require an animation object";
            return false;
        }
        if (edit.kind == AnimationEditKind::Rename &&
            (edit.new_name.empty() || edit.name == edit.new_name)) {
            error_out->message =
                "animation rename edits require distinct non-empty names";
            return false;
        }
        if (edit.kind == AnimationEditKind::SetDuration &&
            (!std::isfinite(edit.duration) || edit.duration < 0.0 ||
             edit.duration > static_cast<double>(
                 std::numeric_limits<runtime::AnimationScalar>::max()))) {
            error_out->message =
                "animation duration edits require finite non-negative float32 values";
            return false;
        }
    }

    std::vector<std::tuple<std::string, std::string, TransformTimelineChannel>> seen_tracks;
    for (const TransformTimelineEdit& edit : project.transform_timeline_edits) {
        if (edit.animation_name.empty() || edit.bone_name.empty()) {
            error_out->message = "timeline edits require animation and bone names";
            return false;
        }
        if (edit.keyframes.empty()) {
            error_out->message = "timeline edits must contain at least one keyframe";
            return false;
        }
        const auto duplicate = std::find(
            seen_tracks.begin(),
            seen_tracks.end(),
            std::make_tuple(edit.animation_name, edit.bone_name, edit.channel));
        if (duplicate != seen_tracks.end()) {
            error_out->message = "duplicate transform timeline edits are not allowed";
            return false;
        }
        seen_tracks.push_back(
            std::make_tuple(edit.animation_name, edit.bone_name, edit.channel));

        double previous_time = 0.0;
        bool has_previous_time = false;
        for (const TransformKeyframeEdit& keyframe : edit.keyframes) {
            if (has_previous_time && keyframe.time <= previous_time) {
                error_out->message = "timeline edit keyframe times must be strictly increasing";
                return false;
            }
            previous_time = keyframe.time;
            has_previous_time = true;
        }
    }

    std::vector<std::tuple<std::string, std::string, std::string>> seen_deform_tracks;
    for (const MeshDeformTimelineEdit& edit : project.mesh_deform_timeline_edits) {
        if (edit.animation_name.empty() || edit.slot_name.empty() || edit.attachment_name.empty()) {
            error_out->message =
                "mesh deform timeline edits require animation, slot, and attachment names";
            return false;
        }
        if (edit.keyframes.empty()) {
            error_out->message = "mesh deform timeline edits must contain at least one keyframe";
            return false;
        }
        const auto duplicate = std::find(
            seen_deform_tracks.begin(),
            seen_deform_tracks.end(),
            std::make_tuple(edit.animation_name, edit.slot_name, edit.attachment_name));
        if (duplicate != seen_deform_tracks.end()) {
            error_out->message = "duplicate mesh deform timeline edits are not allowed";
            return false;
        }
        seen_deform_tracks.push_back(
            std::make_tuple(edit.animation_name, edit.slot_name, edit.attachment_name));

        double previous_time = 0.0;
        bool has_previous_time = false;
        std::size_t expected_vertex_components = 0;
        for (const DeformKeyframeEdit& keyframe : edit.keyframes) {
            if (keyframe.vertex_offsets.empty() ||
                (keyframe.vertex_offsets.size() % 2U) != 0U) {
                error_out->message =
                    "mesh deform timeline edit keyframes must contain x/y vertex offsets";
                return false;
            }
            if (has_previous_time && keyframe.time <= previous_time) {
                error_out->message =
                    "mesh deform timeline edit keyframe times must be strictly increasing";
                return false;
            }
            if (expected_vertex_components == 0U) {
                expected_vertex_components = keyframe.vertex_offsets.size();
            } else if (keyframe.vertex_offsets.size() != expected_vertex_components) {
                error_out->message =
                    "mesh deform timeline edit keyframes must use a consistent vertex count";
                return false;
            }

            previous_time = keyframe.time;
            has_previous_time = true;
        }
    }

    std::vector<std::tuple<std::string, std::string, std::string>> seen_mesh_weight_edits;
    for (const MeshWeightAttachmentEdit& edit : project.mesh_weight_attachment_edits) {
        if (edit.skin_name.empty() || edit.slot_name.empty() || edit.attachment_name.empty()) {
            error_out->message =
                "mesh weight edits require skin, slot, and attachment names";
            return false;
        }
        if (edit.vertices.empty()) {
            error_out->message = "mesh weight edits must contain at least one vertex";
            return false;
        }
        const auto duplicate = std::find(
            seen_mesh_weight_edits.begin(),
            seen_mesh_weight_edits.end(),
            std::make_tuple(edit.skin_name, edit.slot_name, edit.attachment_name));
        if (duplicate != seen_mesh_weight_edits.end()) {
            error_out->message = "duplicate mesh weight edits are not allowed";
            return false;
        }
        seen_mesh_weight_edits.push_back(
            std::make_tuple(edit.skin_name, edit.slot_name, edit.attachment_name));

        for (const MeshWeightVertexEdit& vertex : edit.vertices) {
            if (vertex.influences.empty()) {
                error_out->message =
                    "mesh weight edit vertices must contain at least one influence";
                return false;
            }
            if (vertex.influences.size() >
                mesh_weight_model::kMaxMeshWeightInfluences) {
                error_out->message =
                    "mesh weight edit vertices must not exceed four influences";
                return false;
            }

            std::vector<std::string> seen_bones;
            double total_weight = 0.0;
            for (const MeshWeightInfluenceEdit& influence : vertex.influences) {
                if (influence.bone_name.empty()) {
                    error_out->message =
                        "mesh weight edit influences require non-empty bone names";
                    return false;
                }
                if (influence.weight <= 0.0) {
                    error_out->message =
                        "mesh weight edit influences must preserve positive weights";
                    return false;
                }
                if (std::find(seen_bones.begin(), seen_bones.end(), influence.bone_name) !=
                    seen_bones.end()) {
                    error_out->message =
                        "mesh weight edit vertices must not repeat the same bone";
                    return false;
                }

                seen_bones.push_back(influence.bone_name);
                total_weight += influence.weight;
            }

            if (total_weight <= 0.0) {
                error_out->message =
                    "mesh weight edit vertices must sum to a positive weight";
                return false;
            }
        }
    }

    std::vector<std::string> seen_draw_order_animations;
    for (const DrawOrderTimelineEdit& edit : project.draw_order_timeline_edits) {
        if (edit.animation_name.empty()) {
            error_out->message = "draw order timeline edits require an animation name";
            return false;
        }
        if (edit.keyframes.empty()) {
            error_out->message = "draw order timeline edits must contain at least one keyframe";
            return false;
        }
        if (std::find(
                seen_draw_order_animations.begin(),
                seen_draw_order_animations.end(),
                edit.animation_name) != seen_draw_order_animations.end()) {
            error_out->message = "duplicate draw order timeline edits are not allowed";
            return false;
        }
        seen_draw_order_animations.push_back(edit.animation_name);

        double previous_time = 0.0;
        bool has_previous_time = false;
        for (const DrawOrderKeyframeEdit& keyframe : edit.keyframes) {
            if (keyframe.slot_names.empty()) {
                error_out->message =
                    "draw order timeline edit keyframes must list at least one slot";
                return false;
            }
            for (const std::string& slot_name : keyframe.slot_names) {
                if (slot_name.empty()) {
                    error_out->message = "draw order timeline edit slot names must not be empty";
                    return false;
                }
            }
            std::vector<std::string> sorted_slot_names = keyframe.slot_names;
            std::sort(sorted_slot_names.begin(), sorted_slot_names.end());
            if (std::adjacent_find(sorted_slot_names.begin(), sorted_slot_names.end()) !=
                sorted_slot_names.end()) {
                error_out->message = "draw order timeline edit slot names must be unique";
                return false;
            }
            if (has_previous_time && keyframe.time <= previous_time) {
                error_out->message =
                    "draw order timeline edit keyframe times must be strictly increasing";
                return false;
            }

            previous_time = keyframe.time;
            has_previous_time = true;
        }
    }

    std::vector<std::string> seen_event_animations;
    for (const EventTimelineEdit& edit : project.event_timeline_edits) {
        if (edit.animation_name.empty()) {
            error_out->message = "event timeline edits require an animation name";
            return false;
        }
        if (edit.keyframes.empty()) {
            error_out->message = "event timeline edits must contain at least one keyframe";
            return false;
        }
        if (std::find(
                seen_event_animations.begin(),
                seen_event_animations.end(),
                edit.animation_name) != seen_event_animations.end()) {
            error_out->message = "duplicate event timeline edits are not allowed";
            return false;
        }
        seen_event_animations.push_back(edit.animation_name);

        double previous_time = 0.0;
        bool has_previous_time = false;
        for (const EventKeyframeEdit& keyframe : edit.keyframes) {
            if (keyframe.event_name.empty()) {
                error_out->message = "event timeline edit keyframes require an event name";
                return false;
            }
            if (has_previous_time && keyframe.time < previous_time) {
                error_out->message =
                    "event timeline edit keyframe times must be non-decreasing";
                return false;
            }

            previous_time = keyframe.time;
            has_previous_time = true;
        }
    }

    std::vector<std::string> seen_ik_names;
    for (const IkConstraintEdit& edit : project.ik_constraint_edits) {
        if (edit.name.empty() || edit.target_bone_name.empty()) {
            error_out->message = "ik constraint edits require a name and target bone";
            return false;
        }
        if (edit.bone_names.empty() || edit.bone_names.size() > 2U) {
            error_out->message = "ik constraint edits require one or two bones";
            return false;
        }
        if (edit.mix < 0.0 || edit.mix > 1.0) {
            error_out->message = "ik constraint edit mix must stay within [0, 1]";
            return false;
        }
        if (std::find(seen_ik_names.begin(), seen_ik_names.end(), edit.name) !=
            seen_ik_names.end()) {
            error_out->message = "duplicate ik constraint edit names are not allowed";
            return false;
        }
        seen_ik_names.push_back(edit.name);
        std::vector<std::string> sorted_names = edit.bone_names;
        std::sort(sorted_names.begin(), sorted_names.end());
        if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) != sorted_names.end()) {
            error_out->message = "ik constraint edit bones must be unique";
            return false;
        }
    }

    std::vector<std::string> seen_path_names;
    for (const PathConstraintEdit& edit : project.path_constraint_edits) {
        if (edit.name.empty() || edit.slot_name.empty() || edit.bone_names.empty()) {
            error_out->message = "path constraint edits require a name, slot, and bone chain";
            return false;
        }
        if (edit.position < 0.0 || edit.position > 1.0 ||
            edit.rotate_mix < 0.0 || edit.rotate_mix > 1.0 ||
            edit.translate_mix < 0.0 || edit.translate_mix > 1.0 ||
            edit.spacing < 0.0) {
            error_out->message =
                "path constraint edit numeric values must stay within their valid ranges";
            return false;
        }
        if (std::find(seen_path_names.begin(), seen_path_names.end(), edit.name) !=
            seen_path_names.end()) {
            error_out->message = "duplicate path constraint edit names are not allowed";
            return false;
        }
        seen_path_names.push_back(edit.name);
        std::vector<std::string> sorted_names = edit.bone_names;
        std::sort(sorted_names.begin(), sorted_names.end());
        if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) != sorted_names.end()) {
            error_out->message = "path constraint edit bones must be unique";
            return false;
        }
    }

    std::vector<std::string> seen_transform_names;
    for (const TransformConstraintEdit& edit : project.transform_constraint_edits) {
        if (edit.name.empty() || edit.source_bone_name.empty() || edit.bone_names.empty()) {
            error_out->message =
                "transform constraint edits require a name, source bone, and targets";
            return false;
        }
        if (edit.rotate_mix < 0.0 || edit.rotate_mix > 1.0 ||
            edit.translate_mix < 0.0 || edit.translate_mix > 1.0 ||
            edit.scale_mix < 0.0 || edit.scale_mix > 1.0 ||
            edit.shear_mix < 0.0 || edit.shear_mix > 1.0) {
            error_out->message =
                "transform constraint edit mix values must stay within [0, 1]";
            return false;
        }
        if (std::find(seen_transform_names.begin(), seen_transform_names.end(), edit.name) !=
            seen_transform_names.end()) {
            error_out->message = "duplicate transform constraint edit names are not allowed";
            return false;
        }
        seen_transform_names.push_back(edit.name);
        std::vector<std::string> sorted_names = edit.bone_names;
        std::sort(sorted_names.begin(), sorted_names.end());
        if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) != sorted_names.end()) {
            error_out->message = "transform constraint edit target bones must be unique";
            return false;
        }
        if (std::find(edit.bone_names.begin(), edit.bone_names.end(), edit.source_bone_name) !=
            edit.bone_names.end()) {
            error_out->message = "transform constraint source bone must not also be a target";
            return false;
        }
    }

    std::vector<std::string> seen_physics_names;
    for (const PhysicsConstraintEdit& edit : project.physics_constraint_edits) {
        if (edit.name.empty() || edit.bone_names.empty()) {
            error_out->message = "physics constraint edits require a name and bone chain";
            return false;
        }
        if (edit.step <= 0.0 ||
            edit.x < 0.0 || edit.y < 0.0 ||
            edit.rotate < 0.0 || edit.scale_x < 0.0 ||
            edit.shear_x < 0.0 || edit.limit < 0.0 ||
            edit.inertia < 0.0 || edit.inertia > 1.0 ||
            edit.damping < 0.0 || edit.strength < 0.0 ||
            edit.mass_inverse < 0.0 ||
            edit.mix < 0.0 || edit.mix > 1.0) {
            error_out->message =
                "physics constraint edit numeric values must stay within their valid ranges";
            return false;
        }
        if (std::find(seen_physics_names.begin(), seen_physics_names.end(), edit.name) !=
            seen_physics_names.end()) {
            error_out->message = "duplicate physics constraint edit names are not allowed";
            return false;
        }
        seen_physics_names.push_back(edit.name);
        std::vector<std::string> sorted_names = edit.bone_names;
        std::sort(sorted_names.begin(), sorted_names.end());
        if (std::adjacent_find(sorted_names.begin(), sorted_names.end()) != sorted_names.end()) {
            error_out->message = "physics constraint edit bones must be unique";
            return false;
        }
    }

    // MAR-177 symbolic replay. This function has no base skeleton document, so
    // it cannot know which constraint names exist; `validate_constraint_lifecycle_operations`
    // does that at materialization time. What is knowable here is everything
    // intrinsic to the sequence: walk it front to back, per family, over
    // `consumed` (names a rename or delete has taken away) and `introduced`
    // (names a rename has created). Sorted vectors, not hash sets, matching the
    // `seen_*_names` idiom above -- nothing on this path may depend on hash order.
    if (!project.constraint_lifecycle_operations.empty()) {
        struct FamilyReplayState {
            std::vector<std::string> consumed;
            std::vector<std::string> introduced;
        };
        std::array<FamilyReplayState, kConstraintFamilies.size()> replay{};
        const auto family_slot = [](ConstraintKind family) {
            return static_cast<std::size_t>(family);
        };
        const auto contains = [](const std::vector<std::string>& names,
                                 const std::string& name) {
            return std::find(names.begin(), names.end(), name) != names.end();
        };
        const auto insert_name = [&](std::vector<std::string>& names,
                                     const std::string& name) {
            if (!contains(names, name)) {
                names.push_back(name);
                std::sort(names.begin(), names.end());
            }
        };
        const auto erase_name = [](std::vector<std::string>& names,
                                   const std::string& name) {
            names.erase(std::remove(names.begin(), names.end(), name), names.end());
        };

        for (const ConstraintLifecycleOperation& operation :
             project.constraint_lifecycle_operations) {
            const std::string family(constraint_family_json_key(operation.family));
            FamilyReplayState& state = replay[family_slot(operation.family)];

            if (operation.name.empty()) {
                error_out->message = operation.kind == ConstraintLifecycleKind::Rename
                    ? family + " constraint lifecycle rename source must not be empty"
                    : family + " constraint lifecycle delete target must not be empty";
                return false;
            }
            if (operation.kind == ConstraintLifecycleKind::Rename) {
                if (operation.new_name.empty()) {
                    error_out->message =
                        family + " constraint lifecycle rename target must not be empty";
                    return false;
                }
                if (operation.new_name == operation.name) {
                    error_out->message = family +
                        " constraint lifecycle rename target must differ from its source";
                    return false;
                }
            } else if (!operation.new_name.empty()) {
                error_out->message =
                    family + " constraint lifecycle delete records must not carry a new name";
                return false;
            }

            // Invalid order, source side: the name is gone by the time this
            // operation runs.
            if (contains(state.consumed, operation.name)) {
                error_out->message = family + " constraint lifecycle operation source '" +
                    operation.name + "' was already renamed or deleted by an earlier operation";
                return false;
            }
            // Invalid order, target side.
            if (operation.kind == ConstraintLifecycleKind::Rename &&
                contains(state.introduced, operation.new_name)) {
                error_out->message = family + " constraint lifecycle rename target '" +
                    operation.new_name + "' is already introduced by an earlier operation";
                return false;
            }

            erase_name(state.introduced, operation.name);
            insert_name(state.consumed, operation.name);
            if (operation.kind == ConstraintLifecycleKind::Rename) {
                erase_name(state.consumed, operation.new_name);
                insert_name(state.introduced, operation.new_name);
            }
        }

        // Cross-check against the upserts. A consumed name that an upsert still
        // carries is re-introduced by Phase B after Phase A took it away: a
        // resurrection for a delete, and two constraints where there was one
        // for a rename.
        //
        // The mirror check -- an upsert named by an *introduced* name -- is
        // deliberately absent. That state is byte-for-byte what a shadowing
        // rename is required to produce (append the record, rewrite the
        // shadowing upsert's name), so rejecting it would reject the ownership
        // rule's own output. The genuine collision is caught by the primitives'
        // preflight, which resolves the target against the materialized name set.
        const auto reject_consumed_upsert = [&](ConstraintKind family,
                                                const std::string& name) {
            const std::vector<std::string>& consumed = replay[family_slot(family)].consumed;
            if (!contains(consumed, name)) {
                return false;
            }
            error_out->message = std::string(constraint_family_json_key(family)) +
                " constraint edit '" + name +
                "' is named by a constraint lifecycle operation that already renamed "
                "or deleted it";
            return true;
        };
        for (const IkConstraintEdit& edit : project.ik_constraint_edits) {
            if (reject_consumed_upsert(ConstraintKind::Ik, edit.name)) return false;
        }
        for (const PathConstraintEdit& edit : project.path_constraint_edits) {
            if (reject_consumed_upsert(ConstraintKind::Path, edit.name)) return false;
        }
        for (const TransformConstraintEdit& edit : project.transform_constraint_edits) {
            if (reject_consumed_upsert(ConstraintKind::Transform, edit.name)) return false;
        }
        for (const PhysicsConstraintEdit& edit : project.physics_constraint_edits) {
            if (reject_consumed_upsert(ConstraintKind::Physics, edit.name)) return false;
        }
    }

    std::vector<std::filesystem::path> seen_packed_atlas_paths;
    for (const AtlasPackDefinition& definition : project.atlas_pack_definitions) {
        if (definition.atlas_path.empty()) {
            error_out->message = "atlas pack output paths must not be empty";
            return false;
        }
        const std::filesystem::path resolved_definition_path =
            project.resolve_path(definition.atlas_path);
        const auto referenced_atlas = std::find_if(
            project.runtime_assets.atlas_paths.begin(),
            project.runtime_assets.atlas_paths.end(),
            [&](const std::filesystem::path& atlas_path) {
                return project.resolve_path(atlas_path) == resolved_definition_path;
            });
        if (referenced_atlas == project.runtime_assets.atlas_paths.end()) {
            error_out->message =
                "atlas pack outputs must also appear in runtime atlas paths";
            return false;
        }
        const auto duplicate_atlas = std::find(
            seen_packed_atlas_paths.begin(),
            seen_packed_atlas_paths.end(),
            resolved_definition_path);
        if (duplicate_atlas != seen_packed_atlas_paths.end()) {
            error_out->message = "atlas pack output paths must be unique";
            return false;
        }
        seen_packed_atlas_paths.push_back(resolved_definition_path);

        if (definition.filter_min.empty() ||
            definition.filter_mag.empty() ||
            definition.wrap_x.empty() ||
            definition.wrap_y.empty()) {
            error_out->message = "atlas pack filter and wrap settings must not be empty";
            return false;
        }
        if (definition.padding < 0) {
            error_out->message = "atlas pack padding must be zero or greater";
            return false;
        }
        if (definition.bleed < 0) {
            error_out->message = "atlas pack bleed must be zero or greater";
            return false;
        }
        if (definition.bleed > definition.padding) {
            error_out->message = "atlas pack bleed must not exceed padding";
            return false;
        }
        if (definition.sprites.empty()) {
            error_out->message = "atlas pack definitions require at least one sprite";
            return false;
        }

        std::vector<std::string> seen_region_names;
        for (const AtlasPackSprite& sprite : definition.sprites) {
            if (sprite.region_name.empty() || sprite.image_path.empty()) {
                error_out->message =
                    "atlas pack sprites require a region name and image path";
                return false;
            }
            if (std::find(
                    seen_region_names.begin(),
                    seen_region_names.end(),
                    sprite.region_name) != seen_region_names.end()) {
                error_out->message = "atlas pack sprite region names must be unique";
                return false;
            }
            seen_region_names.push_back(sprite.region_name);
        }
    }

    return true;
}

} // namespace project_detail

std::optional<runtime::json::LoadError> validate_constraint_lifecycle_operations(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document) {
    if (project.constraint_lifecycle_operations.empty()) {
        return std::nullopt;
    }

    struct FamilyState {
        std::vector<std::string> live;      ///< Names present when the next record runs.
        std::vector<std::string> consumed;  ///< Names an earlier record took away.
    };
    std::array<FamilyState, kConstraintFamilies.size()> families;
    for (std::size_t slot = 0; slot < kConstraintFamilies.size(); ++slot) {
        const std::string key(constraint_family_json_key(kConstraintFamilies[slot]));
        const Value* array = find_optional_member(base_skeleton_document.root, key);
        if (array == nullptr || !array->is_array()) {
            continue;
        }
        for (const Value& element : array->as_array()) {
            const Value* name = find_optional_member(element, "name");
            if (name != nullptr && name->is_string()) {
                families[slot].live.push_back(name->as_string());
            }
        }
    }

    const auto contains = [](const std::vector<std::string>& names,
                             const std::string& name) {
        return std::find(names.begin(), names.end(), name) != names.end();
    };
    const auto reject = [&](const std::string& path, std::string message) {
        return validation_error(
            base_skeleton_document,
            base_skeleton_document.root.location(),
            path,
            std::move(message));
    };

    for (std::size_t index = 0; index < project.constraint_lifecycle_operations.size();
         ++index) {
        const ConstraintLifecycleOperation& operation =
            project.constraint_lifecycle_operations[index];
        const std::string path =
            "$.constraint_edits.operations[" + std::to_string(index) + "]";
        const std::string family(constraint_family_json_key(operation.family));
        FamilyState& state = families[static_cast<std::size_t>(operation.family)];

        const auto source = std::find(state.live.begin(), state.live.end(), operation.name);
        if (source == state.live.end()) {
            if (contains(state.consumed, operation.name)) {
                return reject(
                    path,
                    family + " constraint '" + operation.name +
                        "' was already renamed or deleted by an earlier operation");
            }
            // A name that exists, but in a different family, is a distinct
            // fault from a name that does not exist: the fix is the family, not
            // the spelling. Identity is `(family, name)`, and the runtime
            // enforces uniqueness only within a family, so the two can coexist.
            for (const ConstraintKind other : kConstraintFamilies) {
                if (other == operation.family) {
                    continue;
                }
                if (contains(families[static_cast<std::size_t>(other)].live,
                             operation.name)) {
                    return reject(
                        path,
                        family + " constraint '" + operation.name + "' exists as a " +
                            std::string(constraint_family_json_key(other)) +
                            " constraint, not a " + family + " constraint");
                }
            }
            return reject(
                path,
                family + " constraint '" + operation.name +
                    "' does not exist in the base skeleton");
        }

        if (operation.kind == ConstraintLifecycleKind::Rename) {
            if (contains(state.live, operation.new_name)) {
                return reject(
                    path,
                    family + " constraint lifecycle rename target '" +
                        operation.new_name + "' is already taken by a live " + family +
                        " constraint");
            }
            *source = operation.new_name;
        } else {
            state.live.erase(source);
        }
        state.consumed.push_back(operation.name);
    }

    return std::nullopt;
}

} // namespace marrow::editor
