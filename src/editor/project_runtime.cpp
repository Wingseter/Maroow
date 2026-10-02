#include "marrow/editor/project.hpp"
#include "project_json.hpp"
#include "project_internal.hpp"
#include <algorithm>
#include <utility>

namespace marrow::editor {
using namespace project_detail;
using marrow::runtime::json::Document;
using marrow::runtime::json::LoadError;
using marrow::runtime::json::SourceLocation;
using marrow::runtime::json::Value;

namespace {

/**
 * @brief The runtime `.mskl` shape of one transform timeline.
 *
 * The `.marrow` and `.mskl` transform keyframe shapes were identical until
 * MAR-171, so the export reused the project serializer. They no longer are: the
 * project object may carry `curve_mode` and `curve_driver`, which are authoring
 * intent the runtime has no concept of. This builder emits a fixed member list
 * exactly as `build_runtime_slot_color_keyframes_value()` does, so a future
 * additive `.marrow` keyframe field cannot leak into a runtime file either.
 */
Value build_runtime_transform_keyframes_value(const TransformTimelineEdit& edit) {
    Value::Array keyframes;
    keyframes.reserve(edit.keyframes.size());
    for (const TransformKeyframeEdit& keyframe : edit.keyframes) {
        Value::Object keyframe_object;
        keyframe_object.emplace("time", make_number_value(keyframe.time));
        if (is_vector_channel(edit.channel)) {
            keyframe_object.emplace("x", make_number_value(keyframe.x));
            keyframe_object.emplace("y", make_number_value(keyframe.y));
        } else {
            keyframe_object.emplace("angle", make_number_value(keyframe.angle));
        }
        keyframe_object.emplace("curve", build_interpolation_value(keyframe.interpolation));
        keyframes.push_back(make_object_value(std::move(keyframe_object)));
    }
    return make_array_value(std::move(keyframes));
}

Value build_runtime_slot_color_keyframes_value(const SlotColorTimelineEdit& edit) {
    Value::Array keyframes;
    keyframes.reserve(edit.keyframes.size());
    for (const SlotColorKeyframeEdit& keyframe : edit.keyframes) {
        Value::Object keyframe_object;
        keyframe_object.emplace("time", make_number_value(keyframe.time));
        keyframe_object.emplace("r", make_number_value(keyframe.color.r));
        keyframe_object.emplace("g", make_number_value(keyframe.color.g));
        keyframe_object.emplace("b", make_number_value(keyframe.color.b));
        keyframe_object.emplace("a", make_number_value(keyframe.color.a));
        keyframe_object.emplace("curve", build_interpolation_value(keyframe.interpolation));
        keyframes.push_back(make_object_value(std::move(keyframe_object)));
    }
    return make_array_value(std::move(keyframes));
}

void merge_named_object_array_member(
    Value* root,
    std::string_view key,
    Value edits_value) {
    if (root == nullptr || !edits_value.is_array() || edits_value.as_array().empty()) {
        return;
    }
    if (!root->is_object()) {
        *root = make_object_value();
    }

    Value* existing_member = marrow::runtime::json::find_member(*root, key);
    if (existing_member == nullptr) {
        root->as_object().emplace(std::string(key), std::move(edits_value));
        return;
    }
    if (!existing_member->is_array()) {
        *existing_member = std::move(edits_value);
        return;
    }

    for (Value& edit_value : edits_value.as_array()) {
        if (!edit_value.is_object()) {
            existing_member->as_array().push_back(std::move(edit_value));
            continue;
        }

        const Value* edit_name = find_optional_member(edit_value, "name");
        if (edit_name == nullptr || !edit_name->is_string()) {
            existing_member->as_array().push_back(std::move(edit_value));
            continue;
        }

        const auto existing = std::find_if(
            existing_member->as_array().begin(),
            existing_member->as_array().end(),
            [&](const Value& existing_value) {
                const Value* existing_name = find_optional_member(existing_value, "name");
                return existing_name != nullptr &&
                    existing_name->is_string() &&
                    existing_name->as_string() == edit_name->as_string();
            });
        if (existing != existing_member->as_array().end()) {
            *existing = std::move(edit_value);
        } else {
            existing_member->as_array().push_back(std::move(edit_value));
        }
    }
}

Value* find_skin_attachment_value(
    Value* root,
    std::string_view skin_name,
    std::string_view slot_name,
    std::string_view attachment_name) {
    if (root == nullptr || !root->is_object()) {
        return nullptr;
    }

    Value* skins_value = marrow::runtime::json::find_member(*root, "skins");
    if (skins_value == nullptr || !skins_value->is_object()) {
        return nullptr;
    }

    Value* skin_value = marrow::runtime::json::find_member(*skins_value, skin_name);
    if (skin_value == nullptr || !skin_value->is_object()) {
        return nullptr;
    }

    Value* attachments_root = marrow::runtime::json::find_member(*skin_value, "attachments");
    Value* slot_source =
        attachments_root != nullptr && attachments_root->is_object() ? attachments_root : skin_value;
    if (slot_source == nullptr || !slot_source->is_object()) {
        return nullptr;
    }

    Value* slot_value = marrow::runtime::json::find_member(*slot_source, slot_name);
    if (slot_value == nullptr || !slot_value->is_object()) {
        return nullptr;
    }

    if (attachments_root != nullptr && attachments_root->is_object()) {
        Value* nested_attachment = marrow::runtime::json::find_member(*slot_value, attachment_name);
        if (nested_attachment != nullptr && nested_attachment->is_object()) {
            return nested_attachment;
        }

        for (auto& [candidate_name, candidate_value] : slot_value->as_object()) {
            if (!candidate_value.is_object()) {
                continue;
            }

            const Value* authored_name = find_optional_member(candidate_value, "attachment");
            if (candidate_name == attachment_name ||
                (authored_name != nullptr &&
                 authored_name->is_string() &&
                 authored_name->as_string() == attachment_name)) {
                return &candidate_value;
            }
        }
        return nullptr;
    }

    const Value* authored_name = find_optional_member(*slot_value, "attachment");
    if (authored_name != nullptr &&
        authored_name->is_string() &&
        authored_name->as_string() == attachment_name) {
        return slot_value;
    }

    return attachment_name == slot_name ? slot_value : nullptr;
}

Document build_runtime_document(
    const ProjectData& project,
    const Document& base_skeleton_document) {
    Document document = base_skeleton_document;
    if (!document.root.is_object()) {
        return document;
    }

    if (project.parameter_model.has_value()) {
        const ParameterModel& model = *project.parameter_model;
        const auto replace_array = [&](std::string_view key, const Value::Array& values) {
            document.root.as_object().erase(std::string(key));
            if (!values.empty()) {
                document.root.as_object()[std::string(key)] = make_array_value(values);
            }
        };

        Value::Array parameters;
        parameters.reserve(model.parameters.size());
        for (const ParameterAuthoringDefinition& parameter : model.parameters) {
            parameters.push_back(build_parameter_definition_value(model, parameter));
        }
        replace_array("parameters", parameters);

        Value::Array groups;
        groups.reserve(model.groups.size());
        for (const ParameterGroupAuthoringDefinition& group : model.groups) {
            groups.push_back(build_parameter_group_value(model, group));
        }
        replace_array("parameterGroups", groups);

        const auto build_typed_array = [](const auto& values, const auto& build) {
            Value::Array array;
            array.reserve(values.size());
            for (const auto& value : values) array.push_back(build(value));
            return array;
        };
        replace_array(
            "parameterShapes",
            build_typed_array(model.blend_shapes, build_parameter_shape_authoring_value));
        replace_array(
            "parameterDeformers",
            build_typed_array(model.deformers, build_parameter_deformer_authoring_value));
        replace_array(
            "artPaths",
            build_typed_array(model.art_paths, build_art_path_authoring_value));
        replace_array(
            "expressions",
            build_typed_array(model.expressions, build_expression_authoring_value));

        document.root.as_object().erase("lipSync");
        if (!model.lip_sync.empty()) {
            document.root.as_object()["lipSync"] =
                build_lip_sync_authoring_value(model.lip_sync);
        }
    }

    apply_animation_edits(&document.root, project.animation_edits);

    for (const MeshWeightAttachmentEdit& edit : project.mesh_weight_attachment_edits) {
        Value* attachment_value = find_skin_attachment_value(
            &document.root,
            edit.skin_name,
            edit.slot_name,
            edit.attachment_name);
        if (attachment_value == nullptr || !attachment_value->is_object()) {
            continue;
        }

        attachment_value->as_object()["weights"] = build_mesh_weight_vertices_value(edit);
    }

    Value* animations = marrow::runtime::json::find_member(document.root, "animations");
    if (animations == nullptr) {
        document.root.as_object().emplace("animations", make_object_value());
        animations = marrow::runtime::json::find_member(document.root, "animations");
    }
    if (animations == nullptr) {
        return document;
    }
    if (!animations->is_object()) {
        *animations = make_object_value();
    }

    for (const TransformTimelineEdit& edit : project.transform_timeline_edits) {
        Value* animation_value = ensure_object_member(animations, edit.animation_name);
        Value* bones_value = ensure_object_member(animation_value, "bones");
        Value* bone_value = ensure_object_member(bones_value, edit.bone_name);
        if (bone_value != nullptr) {
            bone_value->as_object()[std::string(transform_channel_json_key(edit.channel))] =
                build_runtime_transform_keyframes_value(edit);
        }
    }

    for (const BoneInheritTimelineEdit& edit : project.bone_inherit_timeline_edits) {
        if (edit.keyframes.empty()) {
            // The runtime refuses an empty inherit array outright, so exporting
            // one would produce an unloadable `.mskl`. An empty edit is the
            // legitimate transient state of a project-only timeline that
            // `ensure` has materialized but no key has landed in yet.
            continue;
        }
        Value* animation_value = ensure_object_member(animations, edit.animation_name);
        Value* bones_value = ensure_object_member(animation_value, "bones");
        Value* bone_value = ensure_object_member(bones_value, edit.bone_name);
        if (bone_value != nullptr) {
            // Assigning INTO the copied base document, rather than replacing
            // the animation's `bones` object, is what keeps every sibling bone
            // and channel the project does not override.
            bone_value->as_object()["inherit"] = build_inherit_keyframes_value(edit);
        }
    }

    for (const MeshDeformTimelineEdit& edit : project.mesh_deform_timeline_edits) {
        Value* animation_value = ensure_object_member(animations, edit.animation_name);
        Value* deform_value = ensure_object_member(animation_value, "deform");
        Value* slot_value = ensure_object_member(deform_value, edit.slot_name);
        if (slot_value != nullptr) {
            slot_value->as_object()[edit.attachment_name] =
                build_deform_keyframes_value(edit);
        }
    }

    for (const DrawOrderTimelineEdit& edit : project.draw_order_timeline_edits) {
        Value* animation_value = ensure_object_member(animations, edit.animation_name);
        if (animation_value != nullptr) {
            animation_value->as_object()["drawOrder"] = build_draw_order_keyframes_value(edit);
        }
    }

    for (const EventTimelineEdit& edit : project.event_timeline_edits) {
        Value* animation_value = ensure_object_member(animations, edit.animation_name);
        if (animation_value != nullptr) {
            animation_value->as_object()["events"] = build_event_keyframes_value(edit);
        }
    }

    for (const SlotColorTimelineEdit& edit : project.slot_color_timeline_edits) {
        Value* animation_value = ensure_object_member(animations, edit.animation_name);
        Value* slots_value = ensure_object_member(animation_value, "slots");
        Value* slot_value = ensure_object_member(slots_value, edit.slot_name);
        if (slot_value != nullptr) {
            slot_value->as_object()["color"] = build_runtime_slot_color_keyframes_value(edit);
        }
    }

    for (const SlotAttachmentTimelineEdit& edit : project.slot_attachment_timeline_edits) {
        Value* animation_value = ensure_object_member(animations, edit.animation_name);
        Value* slots_value = ensure_object_member(animation_value, "slots");
        Value* slot_value = ensure_object_member(slots_value, edit.slot_name);
        if (slot_value != nullptr) {
            slot_value->as_object()["attachment"] =
                build_slot_attachment_keyframes_value(edit);
        }
    }

    // Phase A -- lifecycle, over the root arrays and every skin reference --
    // strictly precedes Phase B, the four upsert merges below. That ordering is
    // what makes the ownership rule coherent: a project-only rename rewrote its
    // upsert entry directly, so it lands after every base rename has run and
    // the two can never race for a name.
    apply_constraint_lifecycle_operations(
        &document.root, project.constraint_lifecycle_operations);

    merge_named_object_array_member(
        &document.root,
        "ik",
        build_ik_constraint_edits_value(project.ik_constraint_edits));
    merge_named_object_array_member(
        &document.root,
        "path",
        build_path_constraint_edits_value(project.path_constraint_edits));
    merge_named_object_array_member(
        &document.root,
        "transform",
        build_transform_constraint_edits_value(project.transform_constraint_edits));
    merge_named_object_array_member(
        &document.root,
        "physics",
        build_physics_constraint_edits_value(project.physics_constraint_edits));

    return document;
}

} // namespace

runtime::json::Document build_project_runtime_document(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document) {
    return build_runtime_document(project, base_skeleton_document);
}

ProjectRuntimeResult build_project_runtime(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document) {
    ProjectRuntimeResult result;
    if (const auto animation_error =
            validate_animation_edit_sequence(project, base_skeleton_document)) {
        result.error = animation_error;
        return result;
    }
    if (const auto lifecycle_error =
            validate_constraint_lifecycle_operations(project, base_skeleton_document)) {
        result.error = lifecycle_error;
        return result;
    }
    const Document runtime_document = build_runtime_document(project, base_skeleton_document);
    const auto skeleton_result = marrow::runtime::load_skeleton_data(runtime_document);
    if (!skeleton_result) {
        result.error = skeleton_result.error;
        return result;
    }

    result.skeleton_data = skeleton_result.skeleton_data;
    return result;
}

} // namespace marrow::editor
