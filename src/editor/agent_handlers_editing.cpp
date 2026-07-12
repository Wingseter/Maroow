#include "agent_dispatch_internal.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace marrow::editor::agent_detail {

namespace {

constexpr double kKeyTimeEpsilon = 1e-6;

} // namespace

AgentDispatchResult handle_editing_operation(
    AgentCommandContext& context,
    const json::Value& cmd,
    const OperationSpec& operation) {
    EditorSession& session = context.session;
    const std::string_view op = operation.name;
    const OperationSpec* spec = &operation;

    if (op == "undo") {
        const std::string label(session.undo_label());
        const SessionResult undo_result = session.undo();
        if (undo_result && undo_result.changed) {
            return make_success("Undone: " + label, op, spec);
        }
        return make_error("Nothing to undo.", op, spec, "nothing_to_undo");
    }

    if (op == "redo") {
        const std::string label(session.redo_label());
        const SessionResult redo_result = session.redo();
        if (redo_result && redo_result.changed) {
            return make_success("Redone: " + label, op, spec);
        }
        return make_error("Nothing to redo.", op, spec, "nothing_to_redo");
    }

    const marrow::runtime::SkeletonData& skeleton = *session.runtime_data();

    if (op == "set_transform") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("set_transform requires 'args' object.", op, spec);
        }

        const auto anim_name = string_arg(*args, "animation");
        const auto bone_name = string_arg(*args, "bone");
        const auto channel_str = string_arg(*args, "channel");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !bone_name.has_value() ||
            !channel_str.has_value() || !time.has_value()) {
            return make_error(
                "set_transform requires animation(str), bone(str), channel(str), time(num).",
                op,
                spec);
        }

        TransformTimelineChannel channel;
        if (*channel_str == "rotate") {
            channel = TransformTimelineChannel::Rotate;
        } else if (*channel_str == "translate") {
            channel = TransformTimelineChannel::Translate;
        } else if (*channel_str == "scale") {
            channel = TransformTimelineChannel::Scale;
        } else if (*channel_str == "shear") {
            channel = TransformTimelineChannel::Shear;
        } else {
            return make_error(
                "Invalid channel. Must be rotate, translate, scale, or shear.",
                op,
                spec);
        }

        if (!skeleton.find_bone_index(*bone_name).has_value()) {
            return make_error(
                "Bone not found: " + std::string(*bone_name),
                op,
                spec,
                "not_found");
        }

        if (bool_arg(args, "dry_run")) {
            json::Value::Object preview;
            preview.emplace("dry_run", bool_value(true));
            preview.emplace("animation", string_value(std::string(*anim_name)));
            preview.emplace("bone", string_value(std::string(*bone_name)));
            preview.emplace("channel", string_value(std::string(*channel_str)));
            preview.emplace("time", number_value(*time));
            if (channel == TransformTimelineChannel::Rotate) {
                const auto angle = number_arg(*args, "angle");
                if (!angle.has_value()) {
                    return make_error("rotate channel requires 'angle' number.", op, spec);
                }
                preview.emplace("angle", number_value(*angle));
            } else {
                preview.emplace("x", number_value(number_arg(*args, "x").value_or(0.0)));
                preview.emplace("y", number_value(number_arg(*args, "y").value_or(0.0)));
            }
            return make_success(
                "Transform keyframe validated.",
                op,
                spec,
                object_value(std::move(preview)));
        }

        auto transaction = session.begin_edit({
            EditKind::AddKeyframe,
            "Set transform keyframe via Agent",
            "Agent",
            bool_arg(args, "merge"),
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }

        ProjectData& project = *transaction.project();
        TransformTimelineEdit* edit =
            project.find_transform_timeline_edit(*anim_name, *bone_name, channel);
        if (edit == nullptr) {
            project.transform_timeline_edits.push_back({
                std::string(*anim_name),
                std::string(*bone_name),
                channel,
                {},
            });
            edit = &project.transform_timeline_edits.back();
        }

        auto key_it = std::lower_bound(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            *time,
            [](const TransformKeyframeEdit& keyframe, double key_time) {
                return keyframe.time < key_time;
            });

        TransformKeyframeEdit* key = nullptr;
        if (key_it != edit->keyframes.end() &&
            std::abs(key_it->time - *time) < kKeyTimeEpsilon) {
            key = &(*key_it);
        } else {
            key_it = edit->keyframes.insert(key_it, TransformKeyframeEdit{});
            key = &(*key_it);
            key->time = *time;
            key->interpolation = marrow::runtime::Interpolation::linear();
        }

        if (channel == TransformTimelineChannel::Rotate) {
            const auto angle = number_arg(*args, "angle");
            if (!angle.has_value()) {
                return make_error("rotate channel requires 'angle' number.", op, spec);
            }
            key->angle = *angle;
        } else {
            if (const auto x = number_arg(*args, "x")) {
                key->x = *x;
            }
            if (const auto y = number_arg(*args, "y")) {
                key->y = *y;
            }
        }

        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to apply transform: " + commit_result.error->format(),
                op,
                spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Set transform keyframe successfully.", op, spec);
    }

    if (op == "remove_transform_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("remove_transform_keyframe requires 'args' object.", op, spec);
        }
        const auto anim_name = string_arg(*args, "animation");
        const auto bone_name = string_arg(*args, "bone");
        const auto channel_str = string_arg(*args, "channel");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !bone_name.has_value() ||
            !channel_str.has_value() || !time.has_value()) {
            return make_error(
                "remove_transform_keyframe requires animation, bone, channel, time.",
                op,
                spec);
        }

        TransformTimelineChannel channel;
        if (*channel_str == "rotate") {
            channel = TransformTimelineChannel::Rotate;
        } else if (*channel_str == "translate") {
            channel = TransformTimelineChannel::Translate;
        } else if (*channel_str == "scale") {
            channel = TransformTimelineChannel::Scale;
        } else if (*channel_str == "shear") {
            channel = TransformTimelineChannel::Shear;
        } else {
            return make_error("Invalid channel.", op, spec);
        }

        auto transaction = session.begin_edit({
            EditKind::RemoveKeyframe,
            "Remove transform keyframe via Agent",
            "Agent",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        ProjectData& project = *transaction.project();
        TransformTimelineEdit* edit =
            project.find_transform_timeline_edit(*anim_name, *bone_name, channel);
        if (edit == nullptr) {
            return make_error("Timeline edit not found.", op, spec, "not_found");
        }
        auto key_it = std::find_if(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            [&](const TransformKeyframeEdit& keyframe) {
                return std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
            });
        if (key_it == edit->keyframes.end()) {
            return make_error("Keyframe not found at that time.", op, spec, "not_found");
        }
        edit->keyframes.erase(key_it);

        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to rebuild runtime: " + commit_result.error->format(),
                op,
                spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Removed transform keyframe successfully.", op, spec);
    }

    return handle_timeline_editing_operation(context, cmd, operation);
}

AgentDispatchResult handle_timeline_editing_operation(
    AgentCommandContext& context,
    const json::Value& cmd,
    const OperationSpec& operation) {
    EditorSession& session = context.session;
    const std::string_view op = operation.name;
    const OperationSpec* spec = &operation;

    const auto& skeleton = *session.runtime_data();

    if (op == "set_event_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("set_event_keyframe requires 'args' object.", op, spec);
        }
        const auto anim_name = string_arg(*args, "animation");
        const auto event_name = string_arg(*args, "event");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !event_name.has_value() || !time.has_value()) {
            return make_error("set_event_keyframe requires animation, event, and time.", op, spec);
        }
        if (skeleton.find_animation(*anim_name) == nullptr) {
            return make_error("Animation not found: " + std::string(*anim_name), op, spec, "not_found");
        }
        json::Value::Object preview;
        preview.emplace("dry_run", bool_value(bool_arg(args, "dry_run")));
        preview.emplace("animation", string_value(std::string(*anim_name)));
        preview.emplace("event", string_value(std::string(*event_name)));
        preview.emplace("time", number_value(*time));
        if (bool_arg(args, "dry_run")) {
            return make_success("Event keyframe validated.", op, spec, object_value(std::move(preview)));
        }

        auto transaction = session.begin_edit({
            EditKind::AddKeyframe,
            "Set event keyframe via Agent",
            "Agent",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        ProjectData& project = *transaction.project();
        EventTimelineEdit* edit = project.find_event_timeline_edit(*anim_name);
        if (edit == nullptr) {
            project.event_timeline_edits.push_back({std::string(*anim_name), {}});
            edit = &project.event_timeline_edits.back();
        }
        auto key_it = std::lower_bound(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            *time,
            [](const EventKeyframeEdit& keyframe, double key_time) {
                return keyframe.time < key_time;
            });
        if (key_it == edit->keyframes.end() || std::abs(key_it->time - *time) >= kKeyTimeEpsilon ||
            key_it->event_name != *event_name) {
            key_it = edit->keyframes.insert(key_it, EventKeyframeEdit{});
        }
        key_it->time = *time;
        key_it->event_name = std::string(*event_name);
        if (const auto value = integer_arg(*args, "int")) {
            key_it->int_value = *value;
        }
        if (const auto value = number_arg(*args, "float")) {
            key_it->float_value = *value;
        }
        if (const auto value = string_arg(*args, "string")) {
            key_it->string_value = std::string(*value);
        }
        if (const auto value = string_arg(*args, "audio_path")) {
            key_it->audio_path = std::string(*value);
        }
        if (const auto value = number_arg(*args, "volume")) {
            key_it->volume = *value;
        }
        if (const auto value = number_arg(*args, "balance")) {
            key_it->balance = *value;
        }

        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to apply event keyframe: " + commit_result.error->format(), op, spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Set event keyframe successfully.", op, spec, object_value(std::move(preview)));
    }

    if (op == "remove_event_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("remove_event_keyframe requires 'args' object.", op, spec);
        }
        const auto anim_name = string_arg(*args, "animation");
        const auto event_name = string_arg(*args, "event");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !event_name.has_value() || !time.has_value()) {
            return make_error("remove_event_keyframe requires animation, event, and time.", op, spec);
        }
        auto transaction = session.begin_edit({
            EditKind::RemoveKeyframe,
            "Remove event keyframe via Agent",
            "Agent",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        ProjectData& project = *transaction.project();
        auto edit_it = std::find_if(
            project.event_timeline_edits.begin(),
            project.event_timeline_edits.end(),
            [&](const EventTimelineEdit& edit) { return edit.animation_name == *anim_name; });
        if (edit_it == project.event_timeline_edits.end()) {
            return make_error("Event timeline edit not found.", op, spec, "not_found");
        }
        auto key_it = std::find_if(
            edit_it->keyframes.begin(),
            edit_it->keyframes.end(),
            [&](const EventKeyframeEdit& keyframe) {
                return keyframe.event_name == *event_name &&
                    std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
            });
        if (key_it == edit_it->keyframes.end()) {
            return make_error("Event keyframe not found.", op, spec, "not_found");
        }
        edit_it->keyframes.erase(key_it);
        if (edit_it->keyframes.empty()) {
            project.event_timeline_edits.erase(edit_it);
        }
        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to remove event keyframe: " + commit_result.error->format(), op, spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Removed event keyframe successfully.", op, spec);
    }

    if (op == "set_deform_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("set_deform_keyframe requires 'args' object.", op, spec);
        }
        const auto anim_name = string_arg(*args, "animation");
        const auto slot_name = string_arg(*args, "slot");
        const auto attachment_name = string_arg(*args, "attachment");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !slot_name.has_value() ||
            !attachment_name.has_value() || !time.has_value()) {
            return make_error("set_deform_keyframe requires animation, slot, attachment, and time.", op, spec);
        }
        if (skeleton.find_animation(*anim_name) == nullptr) {
            return make_error("Animation not found: " + std::string(*anim_name), op, spec, "not_found");
        }
        const auto slot_index = skeleton.find_slot_index(*slot_name);
        const auto* attachment = slot_index.has_value()
            ? skeleton.find_attachment_source(*slot_index, *attachment_name)
            : nullptr;
        if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
            return make_error("Mesh attachment not found.", op, spec, "not_found");
        }
        std::vector<double> offsets;
        std::string parse_error;
        if (!parse_number_array(*args, "offsets", 65536U, &offsets, &parse_error)) {
            return make_error(std::move(parse_error), op, spec);
        }
        if (offsets.size() != attachment->mesh_geometry->vertices.size()) {
            return make_error("offsets must match the target mesh vertex offset count.", op, spec);
        }
        std::string interpolation_error;
        const auto interpolation = interpolation_arg(*args, "interpolation", &interpolation_error);
        if (!interpolation.has_value()) {
            return make_error(std::move(interpolation_error), op, spec);
        }
        json::Value::Object preview;
        preview.emplace("dry_run", bool_value(bool_arg(args, "dry_run")));
        preview.emplace("offset_count", number_value(offsets.size()));
        if (bool_arg(args, "dry_run")) {
            return make_success("Deform keyframe validated.", op, spec, object_value(std::move(preview)));
        }

        auto transaction = session.begin_edit({
            EditKind::AddKeyframe,
            "Set deform keyframe via Agent",
            "Agent",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        ProjectData& project = *transaction.project();
        MeshDeformTimelineEdit* edit =
            project.find_mesh_deform_timeline_edit(*anim_name, *slot_name, *attachment_name);
        if (edit == nullptr) {
            project.mesh_deform_timeline_edits.push_back(
                {std::string(*anim_name), std::string(*slot_name), std::string(*attachment_name), {}});
            edit = &project.mesh_deform_timeline_edits.back();
        }
        auto key_it = std::lower_bound(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            *time,
            [](const DeformKeyframeEdit& keyframe, double key_time) { return keyframe.time < key_time; });
        if (key_it == edit->keyframes.end() || std::abs(key_it->time - *time) >= kKeyTimeEpsilon) {
            key_it = edit->keyframes.insert(key_it, DeformKeyframeEdit{});
        }
        key_it->time = *time;
        key_it->vertex_offsets = std::move(offsets);
        key_it->interpolation = *interpolation;
        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to apply deform keyframe: " + commit_result.error->format(), op, spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Set deform keyframe successfully.", op, spec, object_value(std::move(preview)));
    }

    if (op == "remove_deform_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("remove_deform_keyframe requires 'args' object.", op, spec);
        }
        const auto anim_name = string_arg(*args, "animation");
        const auto slot_name = string_arg(*args, "slot");
        const auto attachment_name = string_arg(*args, "attachment");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !slot_name.has_value() ||
            !attachment_name.has_value() || !time.has_value()) {
            return make_error("remove_deform_keyframe requires animation, slot, attachment, and time.", op, spec);
        }
        auto transaction = session.begin_edit({
            EditKind::RemoveKeyframe,
            "Remove deform keyframe via Agent",
            "Agent",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        ProjectData& project = *transaction.project();
        auto edit_it = std::find_if(
            project.mesh_deform_timeline_edits.begin(),
            project.mesh_deform_timeline_edits.end(),
            [&](const MeshDeformTimelineEdit& edit) {
                return edit.animation_name == *anim_name &&
                    edit.slot_name == *slot_name &&
                    edit.attachment_name == *attachment_name;
            });
        if (edit_it == project.mesh_deform_timeline_edits.end()) {
            return make_error("Deform timeline edit not found.", op, spec, "not_found");
        }
        auto key_it = std::find_if(
            edit_it->keyframes.begin(),
            edit_it->keyframes.end(),
            [&](const DeformKeyframeEdit& keyframe) {
                return std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
            });
        if (key_it == edit_it->keyframes.end()) {
            return make_error("Deform keyframe not found.", op, spec, "not_found");
        }
        edit_it->keyframes.erase(key_it);
        if (edit_it->keyframes.empty()) {
            project.mesh_deform_timeline_edits.erase(edit_it);
        }
        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to remove deform keyframe: " + commit_result.error->format(), op, spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Removed deform keyframe successfully.", op, spec);
    }

    if (op == "set_vertex_weights" || op == "normalize_weights") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error(std::string(op) + " requires 'args' object.", op, spec);
        }
        const auto skin_name = string_arg(*args, "skin");
        const auto slot_name = string_arg(*args, "slot");
        const auto attachment_name = string_arg(*args, "attachment");
        if (!skin_name.has_value() || !slot_name.has_value() || !attachment_name.has_value()) {
            return make_error(std::string(op) + " requires skin, slot, and attachment.", op, spec);
        }
        const auto* attachment =
            find_mesh_attachment(skeleton, *skin_name, *slot_name, *attachment_name);
        if (attachment == nullptr) {
            return make_error("Mesh attachment not found.", op, spec, "not_found");
        }
        if (bool_arg(args, "dry_run")) {
            json::Value::Object preview;
            preview.emplace("dry_run", bool_value(true));
            preview.emplace("vertex_count", number_value(attachment->mesh_geometry->weights.size()));
            return make_success("Mesh weight edit validated.", op, spec, object_value(std::move(preview)));
        }
        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            "Edit mesh weights via Agent",
            "Agent",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        ProjectData& project = *transaction.project();
        MeshWeightAttachmentEdit* edit = ensure_mesh_weight_edit(
            project, skeleton, *skin_name, *slot_name, *attachment_name, *attachment);

        if (op == "set_vertex_weights") {
            const json::Value* vertices = json::find_member(*args, "vertices");
            if (vertices == nullptr || !vertices->is_array()) {
                return make_error("set_vertex_weights requires vertices array.", op, spec);
            }
            for (const json::Value& vertex_value : vertices->as_array()) {
                if (!vertex_value.is_object()) {
                    return make_error("vertices entries must be objects.", op, spec);
                }
                const auto index_number = number_arg(vertex_value, "index");
                if (!index_number.has_value() || *index_number < 0.0 ||
                    std::abs(*index_number - std::round(*index_number)) > 1e-6) {
                    return make_error("vertex index must be a non-negative integer.", op, spec);
                }
                const std::size_t vertex_index = static_cast<std::size_t>(std::round(*index_number));
                if (vertex_index >= edit->vertices.size()) {
                    return make_error("vertex index is outside the target mesh.", op, spec);
                }
                const json::Value* influences = json::find_member(vertex_value, "influences");
                if (influences == nullptr || !influences->is_array() ||
                    influences->as_array().empty() || influences->as_array().size() > 4U) {
                    return make_error("vertex influences must contain 1 to 4 entries.", op, spec);
                }
                MeshWeightVertexEdit next_vertex;
                for (const json::Value& influence_value : influences->as_array()) {
                    if (!influence_value.is_object()) {
                        return make_error("influences entries must be objects.", op, spec);
                    }
                    const auto bone_name = string_arg(influence_value, "bone");
                    const auto x = number_arg(influence_value, "x");
                    const auto y = number_arg(influence_value, "y");
                    const auto weight = number_arg(influence_value, "weight");
                    if (!bone_name.has_value() || !x.has_value() || !y.has_value() ||
                        !weight.has_value()) {
                        return make_error("influences require bone, x, y, and weight.", op, spec);
                    }
                    if (!skeleton.find_bone_index(*bone_name).has_value()) {
                        return make_error("Bone not found: " + std::string(*bone_name), op, spec, "not_found");
                    }
                    next_vertex.influences.push_back(
                        MeshWeightInfluenceEdit{std::string(*bone_name), *x, *y, *weight});
                }
                if (bool_arg(args, "normalize", true)) {
                    normalize_weight_vertex(&next_vertex);
                }
                edit->vertices[vertex_index] = std::move(next_vertex);
            }
        } else {
            for (auto& vertex : edit->vertices) {
                normalize_weight_vertex(&vertex);
            }
        }

        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to apply mesh weights: " + commit_result.error->format(), op, spec);
        }
        if (!commit_result.changed) {
            if (op == "normalize_weights") {
                return make_success("Mesh weights already normalized.", op, spec);
            }
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Edited mesh weights successfully.", op, spec);
    }

    if (op == "set_slot_color_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("set_slot_color_keyframe requires 'args' object.", op, spec);
        }
        const auto anim_name = string_arg(*args, "animation");
        const auto slot_name = string_arg(*args, "slot");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !slot_name.has_value() || !time.has_value()) {
            return make_error("set_slot_color_keyframe requires animation, slot, and time.", op, spec);
        }
        if (skeleton.find_animation(*anim_name) == nullptr ||
            !skeleton.find_slot_index(*slot_name).has_value()) {
            return make_error("Animation or slot not found.", op, spec, "not_found");
        }
        std::string color_error;
        const auto color = color_arg(*args, "color", &color_error);
        if (!color.has_value()) {
            return make_error(std::move(color_error), op, spec);
        }
        std::string interpolation_error;
        const auto interpolation = interpolation_arg(*args, "interpolation", &interpolation_error);
        if (!interpolation.has_value()) {
            return make_error(std::move(interpolation_error), op, spec);
        }
        json::Value::Object preview;
        preview.emplace("dry_run", bool_value(bool_arg(args, "dry_run")));
        preview.emplace("slot", string_value(std::string(*slot_name)));
        if (bool_arg(args, "dry_run")) {
            return make_success("Slot color keyframe validated.", op, spec, object_value(std::move(preview)));
        }
        auto transaction = session.begin_edit({
            EditKind::AddKeyframe,
            "Set slot color keyframe via Agent",
            "Agent",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        ProjectData& project = *transaction.project();
        SlotColorTimelineEdit* edit = project.find_slot_color_timeline_edit(*anim_name, *slot_name);
        if (edit == nullptr) {
            project.slot_color_timeline_edits.push_back({std::string(*anim_name), std::string(*slot_name), {}});
            edit = &project.slot_color_timeline_edits.back();
        }
        auto key_it = std::lower_bound(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            *time,
            [](const SlotColorKeyframeEdit& keyframe, double key_time) { return keyframe.time < key_time; });
        if (key_it == edit->keyframes.end() || std::abs(key_it->time - *time) >= kKeyTimeEpsilon) {
            key_it = edit->keyframes.insert(key_it, SlotColorKeyframeEdit{});
        }
        key_it->time = *time;
        key_it->color = *color;
        key_it->interpolation = *interpolation;
        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to apply slot color: " + commit_result.error->format(), op, spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Set slot color keyframe successfully.", op, spec, object_value(std::move(preview)));
    }

    if (op == "remove_slot_color_keyframe" || op == "remove_attachment_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error(std::string(op) + " requires 'args' object.", op, spec);
        }
        const auto anim_name = string_arg(*args, "animation");
        const auto slot_name = string_arg(*args, "slot");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !slot_name.has_value() || !time.has_value()) {
            return make_error(std::string(op) + " requires animation, slot, and time.", op, spec);
        }
        auto transaction = session.begin_edit({
            EditKind::RemoveKeyframe,
            "Remove slot keyframe via Agent",
            "Agent",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        ProjectData& project = *transaction.project();
        bool removed = false;
        if (op == "remove_slot_color_keyframe") {
            auto edit_it = std::find_if(
                project.slot_color_timeline_edits.begin(),
                project.slot_color_timeline_edits.end(),
                [&](const SlotColorTimelineEdit& edit) {
                    return edit.animation_name == *anim_name && edit.slot_name == *slot_name;
                });
            if (edit_it != project.slot_color_timeline_edits.end()) {
                auto key_it = std::find_if(
                    edit_it->keyframes.begin(),
                    edit_it->keyframes.end(),
                    [&](const SlotColorKeyframeEdit& keyframe) {
                        return std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
                    });
                if (key_it != edit_it->keyframes.end()) {
                    edit_it->keyframes.erase(key_it);
                    removed = true;
                    if (edit_it->keyframes.empty()) {
                        project.slot_color_timeline_edits.erase(edit_it);
                    }
                }
            }
        } else {
            auto edit_it = std::find_if(
                project.slot_attachment_timeline_edits.begin(),
                project.slot_attachment_timeline_edits.end(),
                [&](const SlotAttachmentTimelineEdit& edit) {
                    return edit.animation_name == *anim_name && edit.slot_name == *slot_name;
                });
            if (edit_it != project.slot_attachment_timeline_edits.end()) {
                auto key_it = std::find_if(
                    edit_it->keyframes.begin(),
                    edit_it->keyframes.end(),
                    [&](const SlotAttachmentKeyframeEdit& keyframe) {
                        return std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
                    });
                if (key_it != edit_it->keyframes.end()) {
                    edit_it->keyframes.erase(key_it);
                    removed = true;
                    if (edit_it->keyframes.empty()) {
                        project.slot_attachment_timeline_edits.erase(edit_it);
                    }
                }
            }
        }
        if (!removed) {
            return make_error("Slot keyframe not found.", op, spec, "not_found");
        }
        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to remove slot keyframe: " + commit_result.error->format(), op, spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Removed slot keyframe successfully.", op, spec);
    }

    if (op == "set_attachment_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("set_attachment_keyframe requires 'args' object.", op, spec);
        }
        const auto anim_name = string_arg(*args, "animation");
        const auto slot_name = string_arg(*args, "slot");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !slot_name.has_value() || !time.has_value()) {
            return make_error("set_attachment_keyframe requires animation, slot, and time.", op, spec);
        }
        const auto slot_index = skeleton.find_slot_index(*slot_name);
        if (skeleton.find_animation(*anim_name) == nullptr || !slot_index.has_value()) {
            return make_error("Animation or slot not found.", op, spec, "not_found");
        }
        std::optional<std::string> attachment_name;
        const json::Value* attachment_value = json::find_member(*args, "attachment");
        if (attachment_value == nullptr) {
            return make_error("set_attachment_keyframe requires attachment string or null.", op, spec);
        }
        if (attachment_value->is_string()) {
            attachment_name = attachment_value->as_string();
            if (skeleton.find_attachment_source(*slot_index, *attachment_name) == nullptr) {
                return make_error("Attachment not found for slot.", op, spec, "not_found");
            }
        } else if (!attachment_value->is_null()) {
            return make_error("attachment must be string or null.", op, spec);
        }
        json::Value::Object preview;
        preview.emplace("dry_run", bool_value(bool_arg(args, "dry_run")));
        if (attachment_name.has_value()) {
            preview.emplace("attachment", string_value(*attachment_name));
        }
        if (bool_arg(args, "dry_run")) {
            return make_success("Attachment keyframe validated.", op, spec, object_value(std::move(preview)));
        }
        auto transaction = session.begin_edit({
            EditKind::AddKeyframe,
            "Set attachment keyframe via Agent",
            "Agent",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        ProjectData& project = *transaction.project();
        SlotAttachmentTimelineEdit* edit =
            project.find_slot_attachment_timeline_edit(*anim_name, *slot_name);
        if (edit == nullptr) {
            project.slot_attachment_timeline_edits.push_back({std::string(*anim_name), std::string(*slot_name), {}});
            edit = &project.slot_attachment_timeline_edits.back();
        }
        auto key_it = std::lower_bound(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            *time,
            [](const SlotAttachmentKeyframeEdit& keyframe, double key_time) { return keyframe.time < key_time; });
        if (key_it == edit->keyframes.end() || std::abs(key_it->time - *time) >= kKeyTimeEpsilon) {
            key_it = edit->keyframes.insert(key_it, SlotAttachmentKeyframeEdit{});
        }
        key_it->time = *time;
        key_it->attachment_name = std::move(attachment_name);
        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to apply attachment keyframe: " + commit_result.error->format(), op, spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success("Set attachment keyframe successfully.", op, spec, object_value(std::move(preview)));
    }

    if (op == "set_draw_order_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("set_draw_order_keyframe requires 'args' object.", op, spec);
        }

        const auto anim_name = string_arg(*args, "animation");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !time.has_value()) {
            return make_error(
                "set_draw_order_keyframe requires animation string and time number.",
                op,
                spec);
        }
        if (skeleton.find_animation(*anim_name) == nullptr) {
            return make_error("Animation not found: " + std::string(*anim_name), op, spec, "not_found");
        }

        std::vector<std::string> slot_order;
        std::string parse_error;
        if (!parse_complete_slot_order(skeleton, *args, &slot_order, &parse_error)) {
            return make_error(std::move(parse_error), op, spec);
        }

        json::Value::Object preview;
        preview.emplace("animation", string_value(std::string(*anim_name)));
        preview.emplace("time", number_value(*time));
        preview.emplace("slots", string_array_value(slot_order));
        if (bool_arg(args, "dry_run")) {
            return make_success(
                "Draw-order keyframe validated.",
                op,
                spec,
                object_value(std::move(preview)));
        }

        auto transaction = session.begin_edit({
            EditKind::AddKeyframe,
            "Set draw order keyframe via Agent",
            "Agent",
            bool_arg(args, "merge"),
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        ProjectData& project = *transaction.project();
        DrawOrderTimelineEdit* edit = project.find_draw_order_timeline_edit(*anim_name);
        if (edit == nullptr) {
            std::optional<DrawOrderTimelineEdit> copied =
                draw_order_edit_from_runtime(skeleton, *anim_name);
            if (!copied.has_value()) {
                transaction.cancel();
                return make_error("Could not resolve animation draw-order timeline.", op, spec);
            }
            project.draw_order_timeline_edits.push_back(std::move(*copied));
            edit = &project.draw_order_timeline_edits.back();
        }

        auto key_it = std::lower_bound(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            *time,
            [](const DrawOrderKeyframeEdit& keyframe, double key_time) {
                return keyframe.time < key_time;
            });
        if (key_it != edit->keyframes.end() && std::abs(key_it->time - *time) < kKeyTimeEpsilon) {
            key_it->slot_names = std::move(slot_order);
        } else {
            DrawOrderKeyframeEdit keyframe;
            keyframe.time = *time;
            keyframe.slot_names = std::move(slot_order);
            edit->keyframes.insert(key_it, std::move(keyframe));
        }

        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to apply draw-order keyframe: " + commit_result.error->format(), op, spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }

        return make_success(
            "Set draw-order keyframe successfully.",
            op,
            spec,
            object_value(std::move(preview)));
    }

    if (op == "remove_draw_order_keyframe") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error("remove_draw_order_keyframe requires 'args' object.", op, spec);
        }

        const auto anim_name = string_arg(*args, "animation");
        const auto time = number_arg(*args, "time");
        if (!anim_name.has_value() || !time.has_value()) {
            return make_error(
                "remove_draw_order_keyframe requires animation string and time number.",
                op,
                spec);
        }

        auto transaction = session.begin_edit({
            EditKind::RemoveKeyframe,
            "Remove draw order keyframe via Agent",
            "Agent",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        ProjectData& project = *transaction.project();
        auto edit_it = std::find_if(
            project.draw_order_timeline_edits.begin(),
            project.draw_order_timeline_edits.end(),
            [&](const DrawOrderTimelineEdit& edit) {
                return edit.animation_name == *anim_name;
            });
        if (edit_it == project.draw_order_timeline_edits.end()) {
            return make_error("Draw-order timeline edit not found.", op, spec, "not_found");
        }

        auto key_it = std::find_if(
            edit_it->keyframes.begin(),
            edit_it->keyframes.end(),
            [&](const DrawOrderKeyframeEdit& keyframe) {
                return std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
            });
        if (key_it == edit_it->keyframes.end()) {
            return make_error("Draw-order keyframe not found at that time.", op, spec, "not_found");
        }

        edit_it->keyframes.erase(key_it);
        if (edit_it->keyframes.empty()) {
            project.draw_order_timeline_edits.erase(edit_it);
        }

        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to remove draw-order keyframe: " + commit_result.error->format(), op, spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }

        return make_success("Removed draw-order keyframe successfully.", op, spec);
    }

    return make_error("Unknown operation: " + std::string(op), op, spec, "unknown_operation");
}

} // namespace marrow::editor::agent_detail
