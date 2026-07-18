#include "agent_dispatch_internal.hpp"

#include "marrow/editor/authoring.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>
#include <vector>

namespace marrow::editor::agent_detail {

namespace {

constexpr double kKeyTimeEpsilon = 1e-6;

json::Value animation_duration_value(
    const runtime::AnimationData& animation,
    double requested_duration,
    bool dry_run) {
    json::Value::Object payload;
    payload.emplace("dry_run", bool_value(dry_run));
    payload.emplace("animation", string_value(animation.name));
    payload.emplace("requested_duration", number_value(requested_duration));
    payload.emplace("duration", number_value(animation.duration()));
    payload.emplace("inferred_duration", number_value(animation.inferred_duration()));
    payload.emplace(
        "has_explicit_duration",
        bool_value(animation.explicit_duration.has_value()));
    if (animation.explicit_duration.has_value()) {
        payload.emplace(
            "explicit_duration",
            number_value(*animation.explicit_duration));
    }
    return object_value(std::move(payload));
}

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

    if (op == "animation.create" || op == "animation.duplicate" ||
        op == "animation.rename" || op == "animation.delete") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error(std::string(op) + " requires an 'args' object.", op, spec);
        }

        const auto destination = string_arg_any(*args, {"name", "to"});
        const auto source = string_arg_any(*args, {"source", "from"});
        if ((op == "animation.create" || op == "animation.delete") &&
            !destination.has_value()) {
            return make_error(std::string(op) + " requires a non-empty name.", op, spec);
        }
        if ((op == "animation.duplicate" || op == "animation.rename") &&
            (!source.has_value() || !destination.has_value())) {
            return make_error(
                std::string(op) + " requires source/from and name/to.", op, spec);
        }

        const auto apply = [&](ProjectData* project) -> AuthoringResult {
            if (op == "animation.create") {
                return create_animation(
                    project, *session.base_skeleton_document(), *destination);
            }
            if (op == "animation.duplicate") {
                return duplicate_animation(
                    project, *session.base_skeleton_document(), *source, *destination);
            }
            if (op == "animation.rename") {
                return rename_animation(
                    project, *session.base_skeleton_document(), *source, *destination);
            }
            return delete_animation(
                project, *session.base_skeleton_document(), *destination);
        };

        if (bool_arg(args, "dry_run")) {
            ProjectData candidate = *session.project();
            const AuthoringResult result = apply(&candidate);
            if (!result) {
                return make_error(result.error, op, spec);
            }
            json::Value::Object preview;
            preview.emplace("dry_run", bool_value(true));
            if (source.has_value()) {
                preview.emplace("source", string_value(std::string(*source)));
            }
            preview.emplace("name", string_value(std::string(*destination)));
            return make_success(
                "Animation catalog edit validated.",
                op,
                spec,
                object_value(std::move(preview)));
        }

        AnimationCatalogEdit edit;
        edit.source_animation = source.has_value() ? std::string(*source) : std::string{};
        edit.destination_animation = destination.has_value()
            ? std::string(*destination)
            : std::string{};
        if (op == "animation.create") {
            edit.kind = AnimationCatalogEditKind::Create;
        } else if (op == "animation.duplicate") {
            edit.kind = AnimationCatalogEditKind::Duplicate;
        } else if (op == "animation.rename") {
            edit.kind = AnimationCatalogEditKind::Rename;
        } else {
            edit.kind = AnimationCatalogEditKind::Delete;
            edit.source_animation = edit.destination_animation;
            edit.destination_animation.clear();
        }

        const SessionResult commit_result = session.edit_animation_catalog(
            std::move(edit),
            {EditKind::EditProperty,
             "Edit animation catalog via Agent",
             "animation-catalog",
             false,
             EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!commit_result) {
            return make_error(
                "Failed to edit animation catalog: " + commit_result.error->format(),
                op,
                spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }

        json::Value::Object preview;
        preview.emplace(
            "selected_animation",
            string_value(session.preview_state().animation_name));
        preview.emplace(
            "queue_enabled",
            bool_value(session.preview_state().queue_enabled));
        preview.emplace(
            "queued_animation",
            string_value(session.preview_state().queued_animation_name));
        return make_success(
            "Edited animation catalog successfully.",
            op,
            spec,
            object_value(std::move(preview)));
    }

    if (op == "animation.set_duration") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error(
                "animation.set_duration requires an 'args' object.", op, spec);
        }
        for (const auto& [name, value] : args->as_object()) {
            (void)value;
            if (name != "animation" && name != "duration" && name != "dry_run") {
                return make_error(
                    "Unexpected animation.set_duration argument: " + name, op, spec);
            }
        }

        const auto animation_name = string_arg(*args, "animation");
        const auto requested_duration = number_arg(*args, "duration");
        if (!animation_name.has_value() || animation_name->empty() ||
            !requested_duration.has_value() || !std::isfinite(*requested_duration) ||
            *requested_duration < 0.0) {
            return make_error(
                "animation.set_duration requires a non-empty animation and finite "
                "non-negative duration.",
                op,
                spec,
                "validation_failed");
        }
        const json::Value* dry_run_value = json::find_member(*args, "dry_run");
        if (dry_run_value != nullptr && !dry_run_value->is_boolean()) {
            return make_error(
                "animation.set_duration dry_run must be a boolean.",
                op,
                spec,
                "validation_failed");
        }
        const bool dry_run = bool_arg(args, "dry_run");

        const runtime::SkeletonData& effective_skeleton = *session.runtime_data();
        if (effective_skeleton.find_animation(*animation_name) == nullptr) {
            return make_error(
                "Animation not found: " + std::string(*animation_name),
                op,
                spec,
                "not_found");
        }

        if (dry_run) {
            ProjectData candidate = *session.project();
            const AuthoringResult authoring_result = set_animation_duration(
                &candidate,
                effective_skeleton,
                *animation_name,
                *requested_duration);
            if (!authoring_result) {
                return make_error(
                    authoring_result.error, op, spec, "validation_failed");
            }
            if (session.base_skeleton_document() == nullptr) {
                return make_error(
                    "No base skeleton document is loaded.",
                    op,
                    spec,
                    "validation_failed");
            }
            const ProjectRuntimeResult candidate_runtime = build_project_runtime(
                candidate,
                *session.base_skeleton_document());
            if (!candidate_runtime) {
                return make_error(
                    "Candidate runtime validation failed: " +
                        (candidate_runtime.error.has_value()
                             ? candidate_runtime.error->format()
                             : std::string("unknown runtime build failure")),
                    op,
                    spec,
                    "validation_failed");
            }
            const runtime::AnimationData* candidate_animation =
                candidate_runtime.skeleton_data->find_animation(*animation_name);
            if (candidate_animation == nullptr) {
                return make_error(
                    "Candidate runtime lost animation: " +
                        std::string(*animation_name),
                    op,
                    spec,
                    "validation_failed");
            }
            return make_success(
                "Animation duration validated.",
                op,
                spec,
                animation_duration_value(
                    *candidate_animation, *requested_duration, true));
        }

        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            "Set animation duration via Agent",
            "animation-duration:" + std::string(*animation_name),
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(
                transaction.error()->format(), op, spec, "transaction_active");
        }
        const AuthoringResult authoring_result = set_animation_duration(
            transaction.project(),
            effective_skeleton,
            *animation_name,
            *requested_duration);
        if (!authoring_result) {
            transaction.cancel();
            return make_error(
                authoring_result.error, op, spec, "validation_failed");
        }
        if (!authoring_result.changed) {
            transaction.cancel();
            return make_error("No changes made.", op, spec, "no_change");
        }
        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to set animation duration: " +
                    commit_result.error->format(),
                op,
                spec,
                "validation_failed");
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }

        const runtime::AnimationData* updated_animation =
            session.runtime_data()->find_animation(*animation_name);
        if (updated_animation == nullptr) {
            return make_error(
                "Updated runtime lost animation: " +
                    std::string(*animation_name),
                op,
                spec,
                "validation_failed");
        }
        return make_success(
            "Set animation duration successfully.",
            op,
            spec,
            animation_duration_value(
                *updated_animation, *requested_duration, false));
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

        if (skeleton.find_animation(*anim_name) == nullptr) {
            return make_error(
                "Animation not found: " + std::string(*anim_name),
                op,
                spec,
                "not_found");
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

        TransformKeyframePatch patch;
        if (channel == TransformTimelineChannel::Rotate) {
            const auto angle = number_arg(*args, "angle");
            if (!angle.has_value()) {
                return make_error("rotate channel requires 'angle' number.", op, spec);
            }
            patch.angle = *angle;
        } else {
            if (const auto x = number_arg(*args, "x")) {
                patch.x = *x;
            }
            if (const auto y = number_arg(*args, "y")) {
                patch.y = *y;
            }
        }
        upsert_transform_keyframe(
            *transaction.project(),
            skeleton,
            *anim_name,
            *bone_name,
            channel,
            *time,
            patch);

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

    if (op == "timeline.retime_keyframes") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error(
                "timeline.retime_keyframes requires an 'args' object.", op, spec);
        }
        const auto requested_delta = number_arg(*args, "delta");
        const json::Value* keys_value = json::find_member(*args, "keys");
        if (!requested_delta.has_value() || keys_value == nullptr ||
            !keys_value->is_array() || keys_value->as_array().empty()) {
            return make_error(
                "timeline.retime_keyframes requires delta(num) and a non-empty keys(array).",
                op,
                spec);
        }
        if (keys_value->as_array().size() > 4096U) {
            return make_error(
                "timeline.retime_keyframes accepts at most 4096 keys.", op, spec);
        }

        std::vector<TimelineKeySelector> selectors;
        selectors.reserve(keys_value->as_array().size());
        for (std::size_t index = 0U; index < keys_value->as_array().size(); ++index) {
            const json::Value& key_value = keys_value->as_array()[index];
            if (!key_value.is_object()) {
                return make_error(
                    "timeline.retime_keyframes key " + std::to_string(index) +
                        " must be an object.",
                    op,
                    spec);
            }
            const auto kind = string_arg_any(key_value, {"kind", "type"});
            const auto animation = string_arg(key_value, "animation");
            const auto time = number_arg(key_value, "time");
            if (!kind.has_value() || !animation.has_value() || !time.has_value()) {
                return make_error(
                    "timeline.retime_keyframes key " + std::to_string(index) +
                        " requires kind, animation, and time.",
                    op,
                    spec);
            }

            TimelineKeySelector selector;
            selector.animation_name = std::string(*animation);
            selector.time = *time;
            if (*kind == "transform") {
                const auto bone = string_arg(key_value, "bone");
                const auto channel = string_arg(key_value, "channel");
                if (!bone.has_value() || !channel.has_value()) {
                    return make_error(
                        "Transform retime keys require bone and channel.", op, spec);
                }
                selector.kind = TimelineKeyKind::Transform;
                selector.bone_name = std::string(*bone);
                if (*channel == "rotate") {
                    selector.transform_channel = TransformTimelineChannel::Rotate;
                } else if (*channel == "translate") {
                    selector.transform_channel = TransformTimelineChannel::Translate;
                } else if (*channel == "scale") {
                    selector.transform_channel = TransformTimelineChannel::Scale;
                } else if (*channel == "shear") {
                    selector.transform_channel = TransformTimelineChannel::Shear;
                } else {
                    return make_error(
                        "Transform retime channel must be rotate, translate, scale, or shear.",
                        op,
                        spec);
                }
            } else if (*kind == "deform") {
                const auto slot = string_arg(key_value, "slot");
                const auto attachment = string_arg(key_value, "attachment");
                if (!slot.has_value() || !attachment.has_value()) {
                    return make_error(
                        "Deform retime keys require slot and attachment.", op, spec);
                }
                selector.kind = TimelineKeyKind::Deform;
                selector.slot_name = std::string(*slot);
                selector.attachment_name = std::string(*attachment);
            } else if (*kind == "draw_order") {
                selector.kind = TimelineKeyKind::DrawOrder;
            } else if (*kind == "event") {
                selector.kind = TimelineKeyKind::Event;
                if (const auto ordinal = integer_arg(key_value, "ordinal")) {
                    if (*ordinal < 0) {
                        return make_error(
                            "Event retime ordinal must be non-negative.", op, spec);
                    }
                    selector.same_time_ordinal = static_cast<std::size_t>(*ordinal);
                }
            } else if (*kind == "slot_color") {
                const auto slot = string_arg(key_value, "slot");
                if (!slot.has_value()) {
                    return make_error("Slot-color retime keys require slot.", op, spec);
                }
                selector.kind = TimelineKeyKind::SlotColor;
                selector.slot_name = std::string(*slot);
            } else if (*kind == "slot_attachment") {
                const auto slot = string_arg(key_value, "slot");
                if (!slot.has_value()) {
                    return make_error(
                        "Slot-attachment retime keys require slot.", op, spec);
                }
                selector.kind = TimelineKeyKind::SlotAttachment;
                selector.slot_name = std::string(*slot);
            } else {
                return make_error(
                    "Unknown timeline retime key kind: " + std::string(*kind), op, spec);
            }
            selectors.push_back(std::move(selector));
        }

        const bool snap = bool_arg(args, "snap", true);
        const double frames_per_second =
            number_arg(*args, "frames_per_second")
                .value_or(session.project()->editor_metadata.timeline.frames_per_second);
        const auto apply = [&](ProjectData* project) {
            for (const TimelineKeySelector& selector : selectors) {
                switch (selector.kind) {
                case TimelineKeyKind::Transform:
                    (void)ensure_transform_timeline_edit(
                        *project,
                        skeleton,
                        selector.animation_name,
                        selector.bone_name,
                        selector.transform_channel);
                    break;
                case TimelineKeyKind::Deform:
                    (void)ensure_mesh_deform_timeline_edit(
                        *project,
                        skeleton,
                        selector.animation_name,
                        selector.slot_name,
                        selector.attachment_name);
                    break;
                case TimelineKeyKind::DrawOrder:
                    (void)ensure_draw_order_timeline_edit(
                        *project, skeleton, selector.animation_name);
                    break;
                case TimelineKeyKind::Event:
                    (void)ensure_event_timeline_edit(
                        *project, skeleton, selector.animation_name);
                    break;
                case TimelineKeyKind::SlotColor:
                    (void)ensure_slot_color_timeline_edit(
                        *project,
                        skeleton,
                        selector.animation_name,
                        selector.slot_name);
                    break;
                case TimelineKeyKind::SlotAttachment:
                    (void)ensure_slot_attachment_timeline_edit(
                        *project,
                        skeleton,
                        selector.animation_name,
                        selector.slot_name);
                    break;
                }
            }
            return retime_keyframes(
                project, selectors, *requested_delta, snap, frames_per_second);
        };
        const auto response_delta = [&](const TimelineRetimeResult& result, bool dry_run) {
            json::Value::Object response;
            response.emplace("dry_run", bool_value(dry_run));
            response.emplace("requested_delta", number_value(*requested_delta));
            response.emplace("applied_delta", number_value(result.applied_delta));
            response.emplace("key_count", number_value(result.key_count));
            response.emplace("snap", bool_value(snap));
            response.emplace("frames_per_second", number_value(frames_per_second));
            return object_value(std::move(response));
        };

        if (bool_arg(args, "dry_run")) {
            ProjectData candidate = *session.project();
            const TimelineRetimeResult result = apply(&candidate);
            if (!result) {
                return make_error(result.error, op, spec, "not_found");
            }
            return make_success(
                "Timeline retime validated.", op, spec, response_delta(result, true));
        }

        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            selectors.size() == 1U
                ? "Retime timeline key via Agent"
                : "Retime timeline keys via Agent",
            "timeline:retime",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        const TimelineRetimeResult result = apply(transaction.project());
        if (!result) {
            transaction.cancel();
            return make_error(result.error, op, spec, "not_found");
        }
        if (!result.changed) {
            transaction.cancel();
            return make_error("No changes made.", op, spec, "no_change");
        }
        const SessionResult commit_result = transaction.commit();
        if (!commit_result) {
            return make_error(
                "Failed to retime timeline keys: " + commit_result.error->format(),
                op,
                spec);
        }
        if (!commit_result.changed) {
            return make_error("No changes made.", op, spec, "no_change");
        }
        return make_success(
            "Retimed timeline keys successfully.",
            op,
            spec,
            response_delta(result, false));
    }

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
        EventTimelineEdit* edit = ensure_event_timeline_edit(
            project, skeleton, *anim_name);
        if (edit == nullptr) {
            transaction.cancel();
            return make_error("Could not materialize the event timeline.", op, spec);
        }
        auto key_it = std::find_if(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            [&](const EventKeyframeEdit& keyframe) {
                return keyframe.event_name == *event_name &&
                    std::abs(keyframe.time - *time) <= kKeyTimeEpsilon;
            });
        if (key_it == edit->keyframes.end()) {
            const auto insertion = std::upper_bound(
                edit->keyframes.begin(),
                edit->keyframes.end(),
                *time,
                [](double key_time, const EventKeyframeEdit& keyframe) {
                    return key_time < keyframe.time;
                });
            key_it = edit->keyframes.insert(insertion, EventKeyframeEdit{});
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
        std::stable_sort(
            edit->keyframes.begin(),
            edit->keyframes.end(),
            [](const EventKeyframeEdit& left, const EventKeyframeEdit& right) {
                return left.time < right.time;
            });

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
        EventTimelineEdit* materialized = ensure_event_timeline_edit(
            project, skeleton, *anim_name);
        if (materialized == nullptr) {
            transaction.cancel();
            return make_error("Event timeline not found.", op, spec, "not_found");
        }
        auto edit_it = project.event_timeline_edits.begin() +
            (materialized - project.event_timeline_edits.data());
        auto key_it = std::find_if(
            edit_it->keyframes.begin(),
            edit_it->keyframes.end(),
            [&](const EventKeyframeEdit& keyframe) {
                return keyframe.event_name == *event_name &&
                    std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
            });
        if (key_it == edit_it->keyframes.end()) {
            transaction.cancel();
            return make_error("Event keyframe not found.", op, spec, "not_found");
        }
        if (edit_it->keyframes.size() <= 1U) {
            transaction.cancel();
            return make_error(
                "The last event key cannot be removed from an imported timeline.",
                op,
                spec,
                "unsupported");
        }
        edit_it->keyframes.erase(key_it);
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
        MeshDeformTimelineEdit* edit = ensure_mesh_deform_timeline_edit(
            project, skeleton, *anim_name, *slot_name, *attachment_name);
        if (edit == nullptr) {
            transaction.cancel();
            return make_error("Could not materialize the deform timeline.", op, spec);
        }
        auto key_it = find_keyframe_near_time(
            edit->keyframes, *time, kKeyTimeEpsilon);
        if (key_it == edit->keyframes.end()) {
            const auto insertion = std::lower_bound(
                edit->keyframes.begin(),
                edit->keyframes.end(),
                *time,
                [](const DeformKeyframeEdit& keyframe, double key_time) {
                    return keyframe.time < key_time;
                });
            key_it = edit->keyframes.insert(insertion, DeformKeyframeEdit{});
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
        MeshDeformTimelineEdit* materialized = ensure_mesh_deform_timeline_edit(
            project, skeleton, *anim_name, *slot_name, *attachment_name);
        if (materialized == nullptr) {
            transaction.cancel();
            return make_error("Deform timeline edit not found.", op, spec, "not_found");
        }
        auto edit_it = project.mesh_deform_timeline_edits.begin() +
            (materialized - project.mesh_deform_timeline_edits.data());
        auto key_it = std::find_if(
            edit_it->keyframes.begin(),
            edit_it->keyframes.end(),
            [&](const DeformKeyframeEdit& keyframe) {
                return std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
            });
        if (key_it == edit_it->keyframes.end()) {
            transaction.cancel();
            return make_error("Deform keyframe not found.", op, spec, "not_found");
        }
        if (edit_it->keyframes.size() <= 1U) {
            transaction.cancel();
            return make_error(
                "The last deform key cannot be removed from an imported timeline.",
                op,
                spec,
                "unsupported");
        }
        edit_it->keyframes.erase(key_it);
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
        SlotColorTimelineEdit* edit = ensure_slot_color_timeline_edit(
            project, skeleton, *anim_name, *slot_name);
        if (edit == nullptr) {
            transaction.cancel();
            return make_error("Could not materialize the slot-color timeline.", op, spec);
        }
        auto key_it = find_keyframe_near_time(
            edit->keyframes, *time, kKeyTimeEpsilon);
        if (key_it == edit->keyframes.end()) {
            const auto insertion = std::lower_bound(
                edit->keyframes.begin(),
                edit->keyframes.end(),
                *time,
                [](const SlotColorKeyframeEdit& keyframe, double key_time) {
                    return keyframe.time < key_time;
                });
            key_it = edit->keyframes.insert(insertion, SlotColorKeyframeEdit{});
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
            SlotColorTimelineEdit* materialized = ensure_slot_color_timeline_edit(
                project, skeleton, *anim_name, *slot_name);
            if (materialized != nullptr) {
                auto edit_it = project.slot_color_timeline_edits.begin() +
                    (materialized - project.slot_color_timeline_edits.data());
                auto key_it = std::find_if(
                    edit_it->keyframes.begin(),
                    edit_it->keyframes.end(),
                    [&](const SlotColorKeyframeEdit& keyframe) {
                        return std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
                    });
                if (key_it != edit_it->keyframes.end()) {
                    if (edit_it->keyframes.size() > 1U) {
                        edit_it->keyframes.erase(key_it);
                        removed = true;
                    }
                }
            }
        } else {
            SlotAttachmentTimelineEdit* materialized =
                ensure_slot_attachment_timeline_edit(
                    project, skeleton, *anim_name, *slot_name);
            if (materialized != nullptr) {
                auto edit_it = project.slot_attachment_timeline_edits.begin() +
                    (materialized - project.slot_attachment_timeline_edits.data());
                auto key_it = std::find_if(
                    edit_it->keyframes.begin(),
                    edit_it->keyframes.end(),
                    [&](const SlotAttachmentKeyframeEdit& keyframe) {
                        return std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
                    });
                if (key_it != edit_it->keyframes.end()) {
                    if (edit_it->keyframes.size() > 1U) {
                        edit_it->keyframes.erase(key_it);
                        removed = true;
                    }
                }
            }
        }
        if (!removed) {
            transaction.cancel();
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
        SlotAttachmentTimelineEdit* edit = ensure_slot_attachment_timeline_edit(
            project, skeleton, *anim_name, *slot_name);
        if (edit == nullptr) {
            transaction.cancel();
            return make_error("Could not materialize the attachment timeline.", op, spec);
        }
        auto key_it = find_keyframe_near_time(
            edit->keyframes, *time, kKeyTimeEpsilon);
        if (key_it == edit->keyframes.end()) {
            const auto insertion = std::lower_bound(
                edit->keyframes.begin(),
                edit->keyframes.end(),
                *time,
                [](const SlotAttachmentKeyframeEdit& keyframe, double key_time) {
                    return keyframe.time < key_time;
                });
            key_it = edit->keyframes.insert(insertion, SlotAttachmentKeyframeEdit{});
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
        DrawOrderTimelineEdit* edit = ensure_draw_order_timeline_edit(
            project, skeleton, *anim_name);
        if (edit == nullptr) {
            transaction.cancel();
            return make_error("Could not resolve animation draw-order timeline.", op, spec);
        }

        auto key_it = find_keyframe_near_time(
            edit->keyframes, *time, kKeyTimeEpsilon);
        if (key_it != edit->keyframes.end()) {
            key_it->slot_names = std::move(slot_order);
        } else {
            const auto insertion = std::lower_bound(
                edit->keyframes.begin(),
                edit->keyframes.end(),
                *time,
                [](const DrawOrderKeyframeEdit& keyframe, double key_time) {
                    return keyframe.time < key_time;
                });
            DrawOrderKeyframeEdit keyframe;
            keyframe.time = *time;
            keyframe.slot_names = std::move(slot_order);
            edit->keyframes.insert(insertion, std::move(keyframe));
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
        DrawOrderTimelineEdit* materialized = ensure_draw_order_timeline_edit(
            project, skeleton, *anim_name);
        if (materialized == nullptr) {
            transaction.cancel();
            return make_error("Draw-order timeline edit not found.", op, spec, "not_found");
        }
        auto edit_it = project.draw_order_timeline_edits.begin() +
            (materialized - project.draw_order_timeline_edits.data());

        auto key_it = std::find_if(
            edit_it->keyframes.begin(),
            edit_it->keyframes.end(),
            [&](const DrawOrderKeyframeEdit& keyframe) {
                return std::abs(keyframe.time - *time) < kKeyTimeEpsilon;
            });
        if (key_it == edit_it->keyframes.end()) {
            transaction.cancel();
            return make_error("Draw-order keyframe not found at that time.", op, spec, "not_found");
        }

        if (edit_it->keyframes.size() <= 1U) {
            transaction.cancel();
            return make_error(
                "The last draw-order key cannot be removed from an imported timeline.",
                op,
                spec,
                "unsupported");
        }

        edit_it->keyframes.erase(key_it);

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
