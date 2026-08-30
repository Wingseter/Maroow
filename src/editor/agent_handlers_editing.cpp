#include "agent_dispatch_internal.hpp"
#include "mesh_weight_model.hpp"

#include "timeline_model.hpp"
#include "marrow/editor/authoring.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
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

std::string transform_channel_name(TransformTimelineChannel channel) {
    switch (channel) {
    case TransformTimelineChannel::Rotate: return "rotate";
    case TransformTimelineChannel::Translate: return "translate";
    case TransformTimelineChannel::Scale: return "scale";
    case TransformTimelineChannel::Shear: return "shear";
    }
    return "rotate";
}

std::string timeline_key_kind_name(TimelineKeyKind kind) {
    switch (kind) {
    case TimelineKeyKind::Transform: return "transform";
    case TimelineKeyKind::Deform: return "deform";
    case TimelineKeyKind::DrawOrder: return "draw_order";
    case TimelineKeyKind::Event: return "event";
    case TimelineKeyKind::SlotColor: return "slot_color";
    case TimelineKeyKind::SlotAttachment: return "slot_attachment";
    }
    return "transform";
}

/** @brief Encodes an easing exactly as the `.marrow`/`.mskl` `curve` field. */
json::Value interpolation_curve_value(const runtime::Interpolation& interpolation) {
    switch (interpolation.kind()) {
    case runtime::InterpolationKind::Linear:
        return string_value("linear");
    case runtime::InterpolationKind::Stepped:
        return string_value("stepped");
    case runtime::InterpolationKind::CubicBezier: {
        const auto& points = interpolation.cubic_bezier();
        json::Value::Array control_points;
        control_points.reserve(4U);
        control_points.push_back(number_value(static_cast<double>(points.cx1)));
        control_points.push_back(number_value(static_cast<double>(points.cy1)));
        control_points.push_back(number_value(static_cast<double>(points.cx2)));
        control_points.push_back(number_value(static_cast<double>(points.cy2)));
        return array_value(std::move(control_points));
    }
    }
    return string_value("linear");
}

json::Value interpolation_request_value(
    runtime::InterpolationKind kind,
    const std::array<double, 4>& control_points) {
    switch (kind) {
    case runtime::InterpolationKind::Linear:
        return string_value("linear");
    case runtime::InterpolationKind::Stepped:
        return string_value("stepped");
    case runtime::InterpolationKind::CubicBezier: {
        json::Value::Array points;
        points.reserve(4U);
        for (const double value : control_points) {
            points.push_back(number_value(value));
        }
        return array_value(std::move(points));
    }
    }
    return string_value("linear");
}

/**
 * @brief Reads the stored easing of one selected key, or null when the key
 *        does not resolve in `project`.
 */
json::Value timeline_key_curve_value(
    const ProjectData& project,
    const TimelineKeySelector& selector) {
    const auto matching = [&](const auto& keyframes) -> const auto* {
        for (const auto& keyframe : keyframes) {
            if (std::abs(keyframe.time - selector.time) <= kKeyTimeEpsilon) {
                return &keyframe;
            }
        }
        return decltype(&keyframes.front()){nullptr};
    };
    switch (selector.kind) {
    case TimelineKeyKind::Transform: {
        const auto* edit = project.find_transform_timeline_edit(
            selector.animation_name, selector.bone_name, selector.transform_channel);
        if (edit == nullptr || edit->keyframes.empty()) break;
        if (const auto* keyframe = matching(edit->keyframes)) {
            return interpolation_curve_value(keyframe->interpolation);
        }
        break;
    }
    case TimelineKeyKind::Deform: {
        const auto* edit = project.find_mesh_deform_timeline_edit(
            selector.animation_name, selector.slot_name, selector.attachment_name);
        if (edit == nullptr || edit->keyframes.empty()) break;
        if (const auto* keyframe = matching(edit->keyframes)) {
            return interpolation_curve_value(keyframe->interpolation);
        }
        break;
    }
    case TimelineKeyKind::SlotColor: {
        const auto* edit = project.find_slot_color_timeline_edit(
            selector.animation_name, selector.slot_name);
        if (edit == nullptr || edit->keyframes.empty()) break;
        if (const auto* keyframe = matching(edit->keyframes)) {
            return interpolation_curve_value(keyframe->interpolation);
        }
        break;
    }
    case TimelineKeyKind::DrawOrder:
    case TimelineKeyKind::Event:
    case TimelineKeyKind::SlotAttachment:
        break;
    }
    return json::Value{};
}

/**
 * @brief The recorded curve mode and driver of one key, for the Agent echo.
 *
 * Only Transform and Slot Color keys carry curve intent; every other family
 * reports nothing at all, which is what makes `previous_mode` absent rather
 * than a guessed "manual" for a key that could never have one.
 */
std::optional<std::pair<
    marrow::editor::TimelineCurveMode,
    marrow::editor::TimelineScalarComponent>>
timeline_key_curve_intent(
    const ProjectData& project,
    const TimelineKeySelector& selector) {
    const auto matching = [&](const auto& keyframes) -> const auto* {
        for (const auto& keyframe : keyframes) {
            if (std::abs(keyframe.time - selector.time) <= kKeyTimeEpsilon) {
                return &keyframe;
            }
        }
        return decltype(&keyframes.front()){nullptr};
    };
    if (selector.kind == TimelineKeyKind::Transform) {
        const auto* edit = project.find_transform_timeline_edit(
            selector.animation_name, selector.bone_name, selector.transform_channel);
        if (edit == nullptr || edit->keyframes.empty()) return std::nullopt;
        if (const auto* keyframe = matching(edit->keyframes)) {
            return std::make_pair(keyframe->curve_mode, keyframe->curve_driver);
        }
        return std::nullopt;
    }
    if (selector.kind == TimelineKeyKind::SlotColor) {
        const auto* edit = project.find_slot_color_timeline_edit(
            selector.animation_name, selector.slot_name);
        if (edit == nullptr || edit->keyframes.empty()) return std::nullopt;
        if (const auto* keyframe = matching(edit->keyframes)) {
            return std::make_pair(keyframe->curve_mode, keyframe->curve_driver);
        }
    }
    return std::nullopt;
}

/**
 * @brief Recomputes every automatic curve of the project inside `transaction`.
 *
 * MAR-171: a scripted call is not in a per-frame loop, so the Agent resolves
 * the whole project rather than one animation. A failure cancels the caller's
 * transaction, rolling back its own mutation too, because a partially resolved
 * project is exactly the staleness automatic curves exist to remove.
 * @return an error string when the resolve failed; empty on success.
 */
std::string resolve_agent_auto_curves(ProjectData* project) {
    const marrow::editor::TimelineAutoCurveResult result =
        marrow::editor::resolve_automatic_curves(project, {});
    return result ? std::string() : result.error;
}

bool same_curve_value(const json::Value& left, const json::Value& right) {
    if (left.is_string() && right.is_string()) {
        return left.as_string() == right.as_string();
    }
    if (left.is_array() && right.is_array()) {
        if (left.as_array().size() != right.as_array().size()) return false;
        for (std::size_t index = 0U; index < left.as_array().size(); ++index) {
            const json::Value& first = left.as_array()[index];
            const json::Value& second = right.as_array()[index];
            if (!first.is_number() || !second.is_number() ||
                first.as_number() != second.as_number()) {
                return false;
            }
        }
        return true;
    }
    return left.is_null() && right.is_null();
}

/**
 * @brief Splits primitive rejections into "the key is not there" and
 *        "the request was malformed", matching timeline.retime_keyframes.
 */
/**
 * @brief The Agent's rejection for a write or removal on a derived key.
 *
 * A managed boundary key's value and easing are copies of the key at time zero,
 * so writing one would be reverted by the synchronization in the same
 * transaction -- the "the command appears to do nothing and nothing explains
 * why" outcome -- and removing one would leave the lane without the key its
 * opt-in asserts. The GUI skips such a key and reports the skip because a
 * dopesheet box selection routinely spans it; a scripted selector list does not,
 * so the Agent rejects atomically and names the remedy.
 */
std::string managed_loop_boundary_rejection(
    const ProjectData& project,
    const runtime::SkeletonData& skeleton,
    const TimelineKeySelector& selector) {
    if (!marrow::editor::timeline_key_is_managed_loop_boundary(
            project, skeleton, selector)) {
        return {};
    }
    return "That key is a managed loop boundary; edit the key at time 0 of the "
           "same timeline instead, or disable loop synchronization on that "
           "timeline.";
}

/** @brief The same rejection for a whole selector list, first hit wins. */
std::string managed_loop_boundary_rejection(
    const ProjectData& project,
    const runtime::SkeletonData& skeleton,
    const std::vector<TimelineKeySelector>& selectors) {
    for (const TimelineKeySelector& selector : selectors) {
        std::string rejection =
            managed_loop_boundary_rejection(project, skeleton, selector);
        if (!rejection.empty()) {
            return rejection;
        }
    }
    return {};
}

std::string_view classify_timeline_key_error(std::string_view error) {
    return error.find("not found") != std::string_view::npos ? "not_found"
                                                             : "invalid_request";
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
        // MAR-171: a duration change moves no key today, so this resolves
        // nothing (§18.3 asserts it). The seam is wired because criterion 3
        // names duration and because MAR-172's managed boundary key will live
        // at `duration`.
        if (const std::string auto_curve_error =
                resolve_agent_auto_curves(transaction.project());
            !auto_curve_error.empty()) {
            transaction.cancel();
            return make_error(auto_curve_error, op, spec, "invalid_request");
        }
        if (auto result = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{
                    "Failed to set animation duration: ",
                    "validation_failed"})) {
            return std::move(*result);
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

        {
            TimelineKeySelector selector;
            selector.kind = TimelineKeyKind::Transform;
            selector.animation_name = std::string(*anim_name);
            selector.bone_name = std::string(*bone_name);
            selector.transform_channel = channel;
            selector.time = *time;
            if (const std::string rejection = managed_loop_boundary_rejection(
                    *session.project(), skeleton, selector);
                !rejection.empty()) {
                return make_error(rejection, op, spec, "invalid_request");
            }
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

        if (const std::string auto_curve_error =
                resolve_agent_auto_curves(transaction.project());
            !auto_curve_error.empty()) {
            transaction.cancel();
            return make_error(auto_curve_error, op, spec, "invalid_request");
        }
        if (auto result = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to apply transform: "})) {
            return std::move(*result);
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
        {
            TimelineKeySelector selector;
            selector.kind = TimelineKeyKind::Transform;
            selector.animation_name = std::string(*anim_name);
            selector.bone_name = std::string(*bone_name);
            selector.transform_channel = channel;
            selector.time = key_it->time;
            if (const std::string rejection =
                    managed_loop_boundary_rejection(project, skeleton, selector);
                !rejection.empty()) {
                transaction.cancel();
                return make_error(rejection, op, spec, "invalid_request");
            }
        }
        edit->keyframes.erase(key_it);

        if (const std::string auto_curve_error =
                resolve_agent_auto_curves(transaction.project());
            !auto_curve_error.empty()) {
            transaction.cancel();
            return make_error(auto_curve_error, op, spec, "invalid_request");
        }
        if (auto result = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to rebuild runtime: "})) {
            return std::move(*result);
        }
        return make_success("Removed transform keyframe successfully.", op, spec);
    }

    return handle_timeline_editing_operation(context, cmd, operation);
}

bool timeline_key_selectors_arg(
    const json::Value& keys_value,
    std::string_view operation_label,
    std::string_view family_noun,
    std::vector<TimelineKeySelector>* selectors_out,
    std::string* error_out) {
    selectors_out->clear();
    selectors_out->reserve(keys_value.as_array().size());
    for (std::size_t index = 0U; index < keys_value.as_array().size(); ++index) {
        const json::Value& key_value = keys_value.as_array()[index];
        if (!key_value.is_object()) {
            *error_out = std::string(operation_label) + " key " + std::to_string(index) +
                    " must be an object.";
            return false;
        }
        const auto kind = string_arg_any(key_value, {"kind", "type"});
        const auto animation = string_arg(key_value, "animation");
        const auto time = number_arg(key_value, "time");
        if (!kind.has_value() || !animation.has_value() || !time.has_value()) {
            *error_out = std::string(operation_label) + " key " + std::to_string(index) +
                    " requires kind, animation, and time.";
            return false;
        }

        TimelineKeySelector selector;
        selector.animation_name = std::string(*animation);
        selector.time = *time;
        if (*kind == "transform") {
            const auto bone = string_arg(key_value, "bone");
            const auto channel = string_arg(key_value, "channel");
            if (!bone.has_value() || !channel.has_value()) {
                *error_out = "Transform " + std::string(family_noun) + " keys require bone and channel.";
                return false;
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
                *error_out = "Transform " + std::string(family_noun) +
                    " channel must be rotate, translate, scale, or shear.";
                return false;
            }
        } else if (*kind == "deform") {
            const auto slot = string_arg(key_value, "slot");
            const auto attachment = string_arg(key_value, "attachment");
            if (!slot.has_value() || !attachment.has_value()) {
                *error_out = "Deform " + std::string(family_noun) + " keys require slot and attachment.";
                return false;
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
                    *error_out = "Event " + std::string(family_noun) + " ordinal must be non-negative.";
                    return false;
                }
                selector.same_time_ordinal = static_cast<std::size_t>(*ordinal);
            }
        } else if (*kind == "slot_color") {
            const auto slot = string_arg(key_value, "slot");
            if (!slot.has_value()) {
                *error_out = "Slot-color " + std::string(family_noun) + " keys require slot.";
                return false;
            }
            selector.kind = TimelineKeyKind::SlotColor;
            selector.slot_name = std::string(*slot);
        } else if (*kind == "slot_attachment") {
            const auto slot = string_arg(key_value, "slot");
            if (!slot.has_value()) {
                *error_out = "Slot-attachment " + std::string(family_noun) + " keys require slot.";
                return false;
            }
            selector.kind = TimelineKeyKind::SlotAttachment;
            selector.slot_name = std::string(*slot);
        } else {
            *error_out = "Unknown timeline " + std::string(family_noun) + " key kind: " + std::string(*kind);
            return false;
        }
        selectors_out->push_back(std::move(selector));
    }
    return true;
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
        std::string selector_error;
        if (!timeline_key_selectors_arg(
                *keys_value,
                "timeline.retime_keyframes",
                "retime",
                &selectors,
                &selector_error)) {
            return make_error(std::move(selector_error), op, spec);
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
        if (const std::string auto_curve_error =
                resolve_agent_auto_curves(transaction.project());
            !auto_curve_error.empty()) {
            transaction.cancel();
            return make_error(auto_curve_error, op, spec, "invalid_request");
        }
        if (auto commit = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to retime timeline keys: "})) {
            return std::move(*commit);
        }
        return make_success(
            "Retimed timeline keys successfully.",
            op,
            spec,
            response_delta(result, false));
    }

    if (op == "timeline.set_interpolation") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error(
                "timeline.set_interpolation requires an 'args' object.", op, spec);
        }
        const json::Value* keys_value = json::find_member(*args, "keys");
        if (keys_value == nullptr || !keys_value->is_array() ||
            keys_value->as_array().empty()) {
            return make_error(
                "timeline.set_interpolation requires a non-empty keys(array).",
                op,
                spec);
        }
        if (keys_value->as_array().size() > 4096U) {
            return make_error(
                "timeline.set_interpolation accepts at most 4096 keys.", op, spec);
        }

        std::vector<TimelineKeySelector> selectors;
        selectors.reserve(keys_value->as_array().size());
        for (std::size_t index = 0U; index < keys_value->as_array().size(); ++index) {
            const json::Value& key_value = keys_value->as_array()[index];
            if (!key_value.is_object()) {
                return make_error(
                    "timeline.set_interpolation key " + std::to_string(index) +
                        " must be an object.",
                    op,
                    spec);
            }
            const auto kind = string_arg_any(key_value, {"kind", "type"});
            const auto animation = string_arg(key_value, "animation");
            const auto time = number_arg(key_value, "time");
            if (!kind.has_value() || !animation.has_value() || !time.has_value()) {
                return make_error(
                    "timeline.set_interpolation key " + std::to_string(index) +
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
                        "Transform easing keys require bone and channel.", op, spec);
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
                        "Transform easing channel must be rotate, translate, scale, or shear.",
                        op,
                        spec);
                }
            } else if (*kind == "deform") {
                const auto slot = string_arg(key_value, "slot");
                const auto attachment = string_arg(key_value, "attachment");
                if (!slot.has_value() || !attachment.has_value()) {
                    return make_error(
                        "Deform easing keys require slot and attachment.", op, spec);
                }
                selector.kind = TimelineKeyKind::Deform;
                selector.slot_name = std::string(*slot);
                selector.attachment_name = std::string(*attachment);
            } else if (*kind == "slot_color") {
                const auto slot = string_arg(key_value, "slot");
                if (!slot.has_value()) {
                    return make_error("Slot-colour easing keys require slot.", op, spec);
                }
                selector.kind = TimelineKeyKind::SlotColor;
                selector.slot_name = std::string(*slot);
            } else if (*kind == "draw_order" || *kind == "event" ||
                       *kind == "slot_attachment") {
                // These families carry no `interpolation` field at all.
                return make_error(
                    "timeline.set_interpolation does not support " +
                        std::string(*kind) + " keys.",
                    op,
                    spec);
            } else {
                return make_error(
                    "Unknown timeline easing key kind: " + std::string(*kind), op, spec);
            }
            selectors.push_back(std::move(selector));
        }

        marrow::runtime::InterpolationKind requested_kind =
            marrow::runtime::InterpolationKind::Linear;
        std::array<double, 4> requested_points{0.0, 0.0, 1.0, 1.0};
        std::string interpolation_error;
        if (!interpolation_request_arg(
                *args,
                "interpolation",
                &requested_kind,
                &requested_points,
                &interpolation_error)) {
            return make_error(interpolation_error, op, spec);
        }

        // MAR-172: a managed boundary key's easing is a copy of key 0's, so
        // writing it here would be reverted by the synchronization in the same
        // transaction. Reject before anything is materialized.
        if (const std::string rejection = managed_loop_boundary_rejection(
                *session.project(), skeleton, selectors);
            !rejection.empty()) {
            return make_error(rejection, op, spec, "invalid_request");
        }
        const auto materialize = [&](ProjectData* project) {
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
                case TimelineKeyKind::SlotColor:
                    (void)ensure_slot_color_timeline_edit(
                        *project,
                        skeleton,
                        selector.animation_name,
                        selector.slot_name);
                    break;
                case TimelineKeyKind::DrawOrder:
                case TimelineKeyKind::Event:
                case TimelineKeyKind::SlotAttachment:
                    break;
                }
            }
        };
        // Previous curves are read from the materialized candidate, so a
        // runtime-only track reports its runtime curve instead of "not found".
        const auto collect_previous = [&](const ProjectData& project) {
            std::vector<json::Value> curves;
            curves.reserve(selectors.size());
            for (const TimelineKeySelector& selector : selectors) {
                curves.push_back(timeline_key_curve_value(project, selector));
            }
            return curves;
        };
        const auto apply = [&](ProjectData* project) {
            materialize(project);
            return set_keyframe_interpolation(
                project, selectors, requested_kind, requested_points);
        };
        const auto response_delta =
            [&](const TimelineInterpolationResult& result,
                bool dry_run,
                const std::vector<json::Value>& previous,
                const std::vector<json::Value>& current) {
                json::Value::Object response;
                response.emplace("dry_run", bool_value(dry_run));
                response.emplace(
                    "interpolation",
                    interpolation_request_value(requested_kind, requested_points));
                response.emplace("key_count", number_value(result.key_count));
                response.emplace(
                    "changed_key_count", number_value(result.changed_key_count));
                constexpr std::size_t kMaxReportedKeys = 256U;
                const std::size_t reported =
                    std::min(selectors.size(), kMaxReportedKeys);
                response.emplace(
                    "keys_truncated", bool_value(selectors.size() > reported));
                json::Value::Array keys;
                keys.reserve(reported);
                for (std::size_t index = 0U; index < reported; ++index) {
                    json::Value::Object entry;
                    const TimelineKeySelector& selector = selectors[index];
                    entry.emplace(
                        "kind", string_value(timeline_key_kind_name(selector.kind)));
                    entry.emplace("animation", string_value(selector.animation_name));
                    if (selector.kind == TimelineKeyKind::Transform) {
                        entry.emplace("bone", string_value(selector.bone_name));
                        entry.emplace(
                            "channel",
                            string_value(std::string(
                                transform_channel_name(selector.transform_channel))));
                    } else {
                        entry.emplace("slot", string_value(selector.slot_name));
                        if (selector.kind == TimelineKeyKind::Deform) {
                            entry.emplace(
                                "attachment", string_value(selector.attachment_name));
                        }
                    }
                    entry.emplace("time", number_value(selector.time));
                    entry.emplace(
                        "previous_interpolation",
                        index < previous.size() ? previous[index] : json::Value{});
                    const bool changed = index < previous.size() &&
                        index < current.size() &&
                        !same_curve_value(previous[index], current[index]);
                    entry.emplace("changed", bool_value(changed));
                    keys.push_back(object_value(std::move(entry)));
                }
                response.emplace("keys", array_value(std::move(keys)));
                return object_value(std::move(response));
            };

        if (bool_arg(args, "dry_run")) {
            ProjectData candidate = *session.project();
            materialize(&candidate);
            const std::vector<json::Value> previous = collect_previous(candidate);
            const TimelineInterpolationResult result = set_keyframe_interpolation(
                &candidate, selectors, requested_kind, requested_points);
            if (!result && !result.error.empty()) {
                return make_error(
                    result.error,
                    op,
                    spec,
                    std::string(classify_timeline_key_error(result.error)));
            }
            const std::vector<json::Value> current = collect_previous(candidate);
            return make_success(
                "Timeline key easing validated.",
                op,
                spec,
                response_delta(result, true, previous, current));
        }

        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            selectors.size() == 1U
                ? "Set timeline key easing via Agent"
                : "Set timeline key easings via Agent",
            "timeline:interpolation",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        materialize(transaction.project());
        const std::vector<json::Value> previous = collect_previous(*transaction.project());
        const TimelineInterpolationResult result = set_keyframe_interpolation(
            transaction.project(), selectors, requested_kind, requested_points);
        if (!result && !result.error.empty()) {
            const std::string error = result.error;
            transaction.cancel();
            return make_error(
                error, op, spec, std::string(classify_timeline_key_error(error)));
        }
        if (!result.changed) {
            transaction.cancel();
            return make_error("No changes made.", op, spec, "no_change");
        }
        const std::vector<json::Value> current = collect_previous(*transaction.project());
        if (const std::string auto_curve_error =
                resolve_agent_auto_curves(transaction.project());
            !auto_curve_error.empty()) {
            transaction.cancel();
            return make_error(auto_curve_error, op, spec, "invalid_request");
        }
        if (auto commit = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to set timeline key easing: "})) {
            return std::move(*commit);
        }
        return make_success(
            "Set timeline key easing successfully.",
            op,
            spec,
            response_delta(result, false, previous, current));
    }

    if (op == "timeline.set_curve_mode") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error(
                "timeline.set_curve_mode requires an 'args' object.", op, spec);
        }
        const json::Value* keys_value = json::find_member(*args, "keys");
        if (keys_value == nullptr || !keys_value->is_array() ||
            keys_value->as_array().empty()) {
            return make_error(
                "timeline.set_curve_mode requires a non-empty keys(array).", op, spec);
        }
        if (keys_value->as_array().size() > 4096U) {
            return make_error(
                "timeline.set_curve_mode accepts at most 4096 keys.", op, spec);
        }

        std::vector<TimelineKeySelector> selectors;
        selectors.reserve(keys_value->as_array().size());
        for (std::size_t index = 0U; index < keys_value->as_array().size(); ++index) {
            const json::Value& key_value = keys_value->as_array()[index];
            if (!key_value.is_object()) {
                return make_error(
                    "timeline.set_curve_mode key " + std::to_string(index) +
                        " must be an object.",
                    op,
                    spec);
            }
            const auto kind = string_arg_any(key_value, {"kind", "type"});
            const auto animation = string_arg(key_value, "animation");
            const auto time = number_arg(key_value, "time");
            if (!kind.has_value() || !animation.has_value() || !time.has_value()) {
                return make_error(
                    "timeline.set_curve_mode key " + std::to_string(index) +
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
                        "Transform curve-mode keys require bone and channel.", op, spec);
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
                        "Transform curve-mode channel must be rotate, translate, scale, "
                        "or shear.",
                        op,
                        spec);
                }
            } else if (*kind == "slot_color") {
                const auto slot = string_arg(key_value, "slot");
                if (!slot.has_value()) {
                    return make_error(
                        "Slot-colour curve-mode keys require slot.", op, spec);
                }
                selector.kind = TimelineKeyKind::SlotColor;
                selector.slot_name = std::string(*slot);
            } else if (*kind == "deform") {
                // Deliberately narrower than timeline.set_interpolation: a
                // deform key's value is a vertex-offset vector with no
                // canonical scalar to drive a tangent.
                return make_error(
                    "timeline.set_curve_mode does not support deform keys: a deform "
                    "key's value has no canonical scalar to drive a tangent.",
                    op,
                    spec);
            } else if (*kind == "draw_order" || *kind == "event" ||
                       *kind == "slot_attachment") {
                return make_error(
                    "timeline.set_curve_mode does not support " + std::string(*kind) +
                        " keys: they carry no easing at all.",
                    op,
                    spec);
            } else {
                return make_error(
                    "Unknown timeline curve-mode key kind: " + std::string(*kind),
                    op,
                    spec);
            }
            selectors.push_back(std::move(selector));
        }

        marrow::editor::TimelineCurveMode requested_mode =
            marrow::editor::TimelineCurveMode::Manual;
        std::optional<marrow::editor::TimelineScalarComponent> requested_driver;
        std::string mode_error;
        if (!curve_mode_request_arg(
                *args, &requested_mode, &requested_driver, &mode_error)) {
            return make_error(mode_error, op, spec);
        }

        // MAR-172: a managed boundary key's easing is a copy of key 0's, so
        // writing it here would be reverted by the synchronization in the same
        // transaction. Reject before anything is materialized.
        if (const std::string rejection = managed_loop_boundary_rejection(
                *session.project(), skeleton, selectors);
            !rejection.empty()) {
            return make_error(rejection, op, spec, "invalid_request");
        }
        const auto materialize = [&](ProjectData* project) {
            for (const TimelineKeySelector& selector : selectors) {
                if (selector.kind == TimelineKeyKind::Transform) {
                    (void)ensure_transform_timeline_edit(
                        *project,
                        skeleton,
                        selector.animation_name,
                        selector.bone_name,
                        selector.transform_channel);
                } else if (selector.kind == TimelineKeyKind::SlotColor) {
                    (void)ensure_slot_color_timeline_edit(
                        *project, skeleton, selector.animation_name, selector.slot_name);
                }
            }
        };
        // Previous intent and curves are read from the materialized candidate,
        // so a runtime-only track reports its runtime state rather than
        // "not found".
        struct PreviousIntent {
            json::Value mode;
            json::Value driver;
            json::Value curve;
        };
        const auto collect_previous = [&](const ProjectData& project) {
            std::vector<PreviousIntent> previous;
            previous.reserve(selectors.size());
            for (const TimelineKeySelector& selector : selectors) {
                PreviousIntent entry;
                entry.curve = timeline_key_curve_value(project, selector);
                const auto intent = timeline_key_curve_intent(project, selector);
                if (intent.has_value()) {
                    entry.mode = string_value(std::string(
                        marrow::editor::curve_mode_token(intent->first)));
                    entry.driver =
                        intent->first == marrow::editor::TimelineCurveMode::Auto
                        ? string_value(std::string(
                              marrow::editor::curve_driver_token(intent->second)))
                        : json::Value{};
                }
                previous.push_back(std::move(entry));
            }
            return previous;
        };
        const auto response_delta =
            [&](const marrow::editor::TimelineCurveModeResult& result,
                bool dry_run,
                const std::vector<PreviousIntent>& previous,
                const std::vector<json::Value>& current) {
                json::Value::Object response;
                response.emplace("dry_run", bool_value(dry_run));
                response.emplace(
                    "mode",
                    string_value(std::string(
                        marrow::editor::curve_mode_token(requested_mode))));
                response.emplace(
                    "driver",
                    requested_driver.has_value()
                        ? string_value(std::string(marrow::editor::curve_driver_token(
                              *requested_driver)))
                        : json::Value{});
                response.emplace("key_count", number_value(result.key_count));
                response.emplace(
                    "changed_key_count", number_value(result.changed_key_count));
                response.emplace(
                    "resolved_key_count", number_value(result.resolved_key_count));
                constexpr std::size_t kMaxReportedKeys = 256U;
                const std::size_t reported =
                    std::min(selectors.size(), kMaxReportedKeys);
                response.emplace(
                    "keys_truncated", bool_value(selectors.size() > reported));
                json::Value::Array keys;
                keys.reserve(reported);
                for (std::size_t index = 0U; index < reported; ++index) {
                    json::Value::Object entry;
                    const TimelineKeySelector& selector = selectors[index];
                    entry.emplace(
                        "kind", string_value(timeline_key_kind_name(selector.kind)));
                    entry.emplace("animation", string_value(selector.animation_name));
                    if (selector.kind == TimelineKeyKind::Transform) {
                        entry.emplace("bone", string_value(selector.bone_name));
                        entry.emplace(
                            "channel",
                            string_value(std::string(
                                transform_channel_name(selector.transform_channel))));
                    } else {
                        entry.emplace("slot", string_value(selector.slot_name));
                    }
                    entry.emplace("time", number_value(selector.time));
                    entry.emplace(
                        "previous_mode",
                        index < previous.size() ? previous[index].mode : json::Value{});
                    entry.emplace(
                        "previous_driver",
                        index < previous.size() ? previous[index].driver : json::Value{});
                    entry.emplace(
                        "previous_interpolation",
                        index < previous.size() ? previous[index].curve : json::Value{});
                    entry.emplace(
                        "interpolation",
                        index < current.size() ? current[index] : json::Value{});
                    const bool changed = index < previous.size() &&
                        index < current.size() &&
                        !same_curve_value(previous[index].curve, current[index]);
                    entry.emplace("changed", bool_value(changed));
                    keys.push_back(object_value(std::move(entry)));
                }
                response.emplace("keys", array_value(std::move(keys)));
                return object_value(std::move(response));
            };
        const auto collect_curves = [&](const ProjectData& project) {
            std::vector<json::Value> curves;
            curves.reserve(selectors.size());
            for (const TimelineKeySelector& selector : selectors) {
                curves.push_back(timeline_key_curve_value(project, selector));
            }
            return curves;
        };

        if (bool_arg(args, "dry_run")) {
            ProjectData candidate = *session.project();
            materialize(&candidate);
            const std::vector<PreviousIntent> previous = collect_previous(candidate);
            const marrow::editor::TimelineCurveModeResult result =
                marrow::editor::set_keyframe_curve_mode(
                    &candidate, selectors, requested_mode, requested_driver);
            if (!result && !result.error.empty()) {
                return make_error(
                    result.error,
                    op,
                    spec,
                    std::string(classify_timeline_key_error(result.error)));
            }
            const std::vector<json::Value> current = collect_curves(candidate);
            return make_success(
                "Timeline curve mode validated.",
                op,
                spec,
                response_delta(result, true, previous, current));
        }

        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            selectors.size() == 1U
                ? "Set timeline curve mode via Agent"
                : "Set timeline curve modes via Agent",
            "timeline:curve-mode",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        materialize(transaction.project());
        const std::vector<PreviousIntent> previous =
            collect_previous(*transaction.project());
        const marrow::editor::TimelineCurveModeResult result =
            marrow::editor::set_keyframe_curve_mode(
                transaction.project(), selectors, requested_mode, requested_driver);
        if (!result && !result.error.empty()) {
            const std::string error = result.error;
            transaction.cancel();
            return make_error(
                error, op, spec, std::string(classify_timeline_key_error(error)));
        }
        if (!result.changed) {
            transaction.cancel();
            return make_error("No changes made.", op, spec, "no_change");
        }
        const std::vector<json::Value> current = collect_curves(*transaction.project());
        if (auto commit = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to set timeline curve mode: "})) {
            return std::move(*commit);
        }
        return make_success(
            "Set timeline curve mode successfully.",
            op,
            spec,
            response_delta(result, false, previous, current));
    }

    if (op == "timeline.set_loop_sync") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error(
                "timeline.set_loop_sync requires an 'args' object.", op, spec);
        }
        std::vector<marrow::editor::TimelineLaneSelector> lanes;
        std::string lane_error;
        if (!timeline_lane_selectors_arg(*args, &lanes, &lane_error)) {
            return make_error(lane_error, op, spec);
        }
        const json::Value* enabled_value = json::find_member(*args, "enabled");
        if (enabled_value == nullptr || !enabled_value->is_boolean()) {
            // Missing is an error rather than a default: guessing for a whole
            // selection is destructive in exactly one of the two directions.
            return make_error(
                "timeline.set_loop_sync requires a boolean 'enabled'.", op, spec);
        }
        const bool requested_enabled = enabled_value->as_boolean();

        const auto materialize = [&](ProjectData* project) {
            for (const marrow::editor::TimelineLaneSelector& lane : lanes) {
                switch (lane.kind) {
                case marrow::editor::TimelineLaneKind::Transform:
                    (void)ensure_transform_timeline_edit(
                        *project,
                        skeleton,
                        lane.animation_name,
                        lane.bone_name,
                        lane.transform_channel);
                    break;
                case marrow::editor::TimelineLaneKind::SlotColor:
                    (void)ensure_slot_color_timeline_edit(
                        *project, skeleton, lane.animation_name, lane.slot_name);
                    break;
                case marrow::editor::TimelineLaneKind::Deform:
                    (void)ensure_mesh_deform_timeline_edit(
                        *project,
                        skeleton,
                        lane.animation_name,
                        lane.slot_name,
                        lane.attachment_name);
                    break;
                }
            }
        };

        struct LaneReport {
            bool enabled{false};
            json::Value duration;
            json::Value boundary_time;
            json::Value boundary;
        };
        // The boundary key is reported using the `.marrow` keyframe encoding so
        // a caller can compare it against the stored bytes. A deform boundary
        // reports its vertex count instead of hundreds of offsets: the response
        // is a report, not a copy.
        const auto collect = [&](const ProjectData& project) {
            std::vector<LaneReport> reports;
            reports.reserve(lanes.size());
            for (const marrow::editor::TimelineLaneSelector& lane : lanes) {
                LaneReport entry;
                // The managed boundary is derived: it is the key at
                // `float32(duration)`, so a lane whose last key sits elsewhere
                // reports no boundary at all rather than its last authored key.
                const auto* animation = skeleton.find_animation(lane.animation_name);
                std::optional<double> boundary_time;
                if (animation != nullptr && animation->explicit_duration.has_value()) {
                    entry.duration = number_value(*animation->explicit_duration);
                    boundary_time = static_cast<double>(
                        static_cast<marrow::runtime::AnimationScalar>(
                            *animation->explicit_duration));
                }
                const auto boundary_of = [&](const auto* edit) {
                    if (edit == nullptr) return;
                    entry.enabled = edit->loop_sync;
                    if (edit->keyframes.size() < 2U || !boundary_time.has_value()) return;
                    const auto& key = edit->keyframes.back();
                    if (std::abs(key.time - *boundary_time) > 1e-6) return;
                    entry.boundary_time = number_value(key.time);
                    json::Value::Object encoded;
                    encoded.emplace("time", number_value(key.time));
                    encoded.emplace(
                        "curve", interpolation_curve_value(key.interpolation));
                    entry.boundary = object_value(std::move(encoded));
                };
                switch (lane.kind) {
                case marrow::editor::TimelineLaneKind::Transform: {
                    const auto* edit = project.find_transform_timeline_edit(
                        lane.animation_name, lane.bone_name, lane.transform_channel);
                    boundary_of(edit);
                    if (edit != nullptr && entry.boundary.is_object()) {
                        const auto& key = edit->keyframes.back();
                        if (lane.transform_channel == TransformTimelineChannel::Rotate) {
                            entry.boundary.as_object()["angle"] = number_value(key.angle);
                        } else {
                            entry.boundary.as_object()["x"] = number_value(key.x);
                            entry.boundary.as_object()["y"] = number_value(key.y);
                        }
                    }
                    break;
                }
                case marrow::editor::TimelineLaneKind::SlotColor: {
                    const auto* edit = project.find_slot_color_timeline_edit(
                        lane.animation_name, lane.slot_name);
                    boundary_of(edit);
                    if (edit != nullptr && entry.boundary.is_object()) {
                        const auto& color = edit->keyframes.back().color;
                        entry.boundary.as_object()["r"] = number_value(color.r);
                        entry.boundary.as_object()["g"] = number_value(color.g);
                        entry.boundary.as_object()["b"] = number_value(color.b);
                        entry.boundary.as_object()["a"] = number_value(color.a);
                    }
                    break;
                }
                case marrow::editor::TimelineLaneKind::Deform: {
                    const auto* edit = project.find_mesh_deform_timeline_edit(
                        lane.animation_name, lane.slot_name, lane.attachment_name);
                    boundary_of(edit);
                    if (edit != nullptr && entry.boundary.is_object()) {
                        entry.boundary.as_object()["vertex_count"] =
                            number_value(edit->keyframes.back().vertex_offsets.size());
                    }
                    break;
                }
                }
                reports.push_back(std::move(entry));
            }
            return reports;
        };

        const auto boundary_action_name =
            [](marrow::editor::TimelineLoopBoundaryAction action) -> std::string {
            switch (action) {
            case marrow::editor::TimelineLoopBoundaryAction::Created:
                return "created";
            case marrow::editor::TimelineLoopBoundaryAction::Adopted:
                return "adopted";
            case marrow::editor::TimelineLoopBoundaryAction::Moved:
                return "moved";
            case marrow::editor::TimelineLoopBoundaryAction::Rewritten:
                return "rewritten";
            case marrow::editor::TimelineLoopBoundaryAction::Released:
                return "released";
            case marrow::editor::TimelineLoopBoundaryAction::Unchanged:
                break;
            }
            return "unchanged";
        };

        const auto response_delta =
            [&](const marrow::editor::TimelineLoopSyncResult& result,
                bool dry_run,
                const std::vector<LaneReport>& previous,
                const std::vector<LaneReport>& current) {
                json::Value::Object response;
                response.emplace("dry_run", bool_value(dry_run));
                response.emplace("enabled", bool_value(requested_enabled));
                response.emplace("lane_count", number_value(result.lane_count));
                response.emplace(
                    "changed_lane_count", number_value(result.changed_lane_count));
                response.emplace(
                    "synchronized_lane_count",
                    number_value(result.synchronized_lane_count));
                response.emplace(
                    "created_key_count", number_value(result.created_key_count));
                response.emplace("moved_key_count", number_value(result.moved_key_count));
                response.emplace(
                    "rewritten_key_count", number_value(result.rewritten_key_count));
                response.emplace(
                    "resolved_key_count", number_value(result.resolved_key_count));
                constexpr std::size_t kMaxReportedLanes = 256U;
                const std::size_t reported = std::min(lanes.size(), kMaxReportedLanes);
                response.emplace(
                    "lanes_truncated", bool_value(lanes.size() > reported));
                json::Value::Array reported_lanes;
                reported_lanes.reserve(reported);
                for (std::size_t index = 0U; index < reported; ++index) {
                    const marrow::editor::TimelineLaneSelector& lane = lanes[index];
                    json::Value::Object entry;
                    entry.emplace(
                        "kind",
                        string_value(std::string(
                            marrow::editor::timeline_lane_kind_token(lane.kind))));
                    entry.emplace("animation", string_value(lane.animation_name));
                    if (lane.kind == marrow::editor::TimelineLaneKind::Transform) {
                        entry.emplace("bone", string_value(lane.bone_name));
                        entry.emplace(
                            "channel",
                            string_value(std::string(
                                transform_channel_name(lane.transform_channel))));
                    } else {
                        entry.emplace("slot", string_value(lane.slot_name));
                        if (lane.kind == marrow::editor::TimelineLaneKind::Deform) {
                            entry.emplace(
                                "attachment", string_value(lane.attachment_name));
                        }
                    }
                    entry.emplace(
                        "previous_enabled",
                        bool_value(index < previous.size() && previous[index].enabled));
                    entry.emplace(
                        "enabled",
                        bool_value(index < current.size() && current[index].enabled));
                    entry.emplace(
                        "duration",
                        index < current.size() ? current[index].duration : json::Value{});
                    entry.emplace(
                        "boundary_time",
                        index < current.size() ? current[index].boundary_time
                                               : json::Value{});
                    entry.emplace(
                        "boundary_action",
                        string_value(
                            index < result.lane_actions.size()
                                ? boundary_action_name(result.lane_actions[index])
                                : std::string("unchanged")));
                    entry.emplace(
                        "previous_boundary",
                        index < previous.size() ? previous[index].boundary
                                                : json::Value{});
                    entry.emplace(
                        "boundary",
                        index < current.size() ? current[index].boundary : json::Value{});
                    const bool changed = index < previous.size() &&
                        index < current.size() &&
                        (previous[index].enabled != current[index].enabled ||
                         !same_curve_value(
                             previous[index].boundary, current[index].boundary));
                    entry.emplace("changed", bool_value(changed));
                    reported_lanes.push_back(object_value(std::move(entry)));
                }
                response.emplace("lanes", array_value(std::move(reported_lanes)));
                return object_value(std::move(response));
            };

        if (bool_arg(args, "dry_run")) {
            ProjectData candidate = *session.project();
            materialize(&candidate);
            const std::vector<LaneReport> previous = collect(candidate);
            const marrow::editor::TimelineLoopSyncResult result =
                marrow::editor::set_timeline_loop_sync(
                    &candidate, skeleton, lanes, requested_enabled);
            if (!result && !result.error.empty()) {
                return make_error(
                    result.error,
                    op,
                    spec,
                    std::string(classify_timeline_key_error(result.error)));
            }
            const std::vector<LaneReport> current = collect(candidate);
            return make_success(
                "Timeline loop synchronization validated.",
                op,
                spec,
                response_delta(result, true, previous, current));
        }

        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            requested_enabled
                ? (lanes.size() == 1U ? "Enable loop synchronization via Agent"
                                      : "Enable loop synchronization on timelines via Agent")
                : (lanes.size() == 1U ? "Disable loop synchronization via Agent"
                                      : "Disable loop synchronization on timelines via Agent"),
            "timeline:loop-sync",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        materialize(transaction.project());
        const std::vector<LaneReport> previous = collect(*transaction.project());
        const marrow::editor::TimelineLoopSyncResult result =
            marrow::editor::set_timeline_loop_sync(
                transaction.project(), skeleton, lanes, requested_enabled);
        if (!result && !result.error.empty()) {
            const std::string error = result.error;
            transaction.cancel();
            return make_error(
                error, op, spec, std::string(classify_timeline_key_error(error)));
        }
        if (!result.changed) {
            transaction.cancel();
            return make_error("No changes made.", op, spec, "no_change");
        }
        const std::vector<LaneReport> current = collect(*transaction.project());
        if (auto commit = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to set loop synchronization: "})) {
            return std::move(*commit);
        }
        return make_success(
            "Set timeline loop synchronization successfully.",
            op,
            spec,
            response_delta(result, false, previous, current));
    }

    if (op == "timeline.scale_key_times") {
        const json::Value* args = command_args(cmd);
        if (args == nullptr) {
            return make_error(
                "timeline.scale_key_times requires an 'args' object.", op, spec);
        }
        const json::Value* keys_value = json::find_member(*args, "keys");
        if (keys_value == nullptr || !keys_value->is_array() ||
            keys_value->as_array().empty()) {
            return make_error(
                "timeline.scale_key_times requires a non-empty keys(array).", op, spec);
        }
        if (keys_value->as_array().size() > 4096U) {
            return make_error(
                "timeline.scale_key_times accepts at most 4096 keys.", op, spec);
        }
        std::vector<TimelineKeySelector> selectors;
        std::string selector_error;
        if (!timeline_key_selectors_arg(
                *keys_value,
                "timeline.scale_key_times",
                "scale",
                &selectors,
                &selector_error)) {
            return make_error(std::move(selector_error), op, spec);
        }
        // Missing is an error rather than a default: guessing a ratio or an
        // anchor for the caller's whole selection is destructive.
        const auto requested_scale = number_arg(*args, "scale");
        if (!requested_scale.has_value()) {
            return make_error(
                "timeline.scale_key_times requires a numeric 'scale'.", op, spec);
        }
        const auto pivot_token = string_arg(*args, "pivot");
        std::optional<marrow::editor::TimelineScalePivot> pivot;
        if (pivot_token.has_value()) {
            if (*pivot_token == "start") {
                pivot = marrow::editor::TimelineScalePivot::RangeStart;
            } else if (*pivot_token == "end") {
                pivot = marrow::editor::TimelineScalePivot::RangeEnd;
            }
        }
        if (!pivot.has_value()) {
            return make_error(
                "timeline.scale_key_times requires a 'pivot' of \"start\" or \"end\".",
                op,
                spec);
        }
        // A scripted ratio is exact, so snapping is opt-in here while
        // timeline.retime_keyframes defaults its pointer-shaped delta to on.
        const bool snap = bool_arg(args, "snap", false);
        const double frames_per_second =
            number_arg(*args, "frames_per_second")
                .value_or(session.project()->editor_metadata.timeline.frames_per_second);
        if (snap && (!std::isfinite(frames_per_second) || frames_per_second <= 0.0)) {
            return make_error(
                "timeline.scale_key_times frames per second must be positive.", op, spec);
        }

        const auto materialize = [&](ProjectData* project) {
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
        };
        // Snapping reshapes the ratio so the MOVED edge lands on a frame
        // boundary; interior keys keep the ratio's exact placement, because
        // quantizing them would stop the result from being a scale at all.
        double applied_request = *requested_scale;
        if (snap) {
            double minimum_time = std::numeric_limits<double>::infinity();
            double maximum_time = -std::numeric_limits<double>::infinity();
            for (const TimelineKeySelector& selector : selectors) {
                minimum_time = std::min(minimum_time, selector.time);
                maximum_time = std::max(maximum_time, selector.time);
            }
            const bool start_pivot =
                *pivot == marrow::editor::TimelineScalePivot::RangeStart;
            const auto snapped = marrow::editor::timeline_model::snap_scale_to_frames(
                start_pivot ? minimum_time : maximum_time,
                start_pivot ? maximum_time : minimum_time,
                *requested_scale,
                frames_per_second);
            if (!snapped.has_value()) {
                return make_error(
                    "No frame boundary produces a positive scale ratio.", op, spec);
            }
            applied_request = *snapped;
        }

        const auto apply = [&](ProjectData* project) {
            materialize(project);
            return marrow::editor::scale_keyframe_times(
                project, selectors, *pivot, applied_request);
        };
        // `previous_time` comes from the primitive's resolved snapshot, not
        // from the request: a selector's `time` only has to identify a key
        // within the resolver's one-microsecond window, so echoing it would
        // report what the caller asked for rather than what the project holds.
        const auto response_delta =
            [&](const marrow::editor::TimelineScaleResult& result, bool dry_run) {
                const std::vector<double>& previous_times = result.previous_times;
                json::Value::Object response;
                response.emplace("dry_run", bool_value(dry_run));
                response.emplace("requested_scale", number_value(*requested_scale));
                response.emplace("applied_scale", number_value(applied_request));
                response.emplace(
                    "pivot",
                    string_value(std::string(
                        *pivot == marrow::editor::TimelineScalePivot::RangeStart
                            ? "start"
                            : "end")));
                response.emplace("pivot_time", number_value(result.pivot_time));
                response.emplace("original_span", number_value(result.original_span));
                response.emplace("scaled_span", number_value(result.scaled_span));
                response.emplace("snap", bool_value(snap));
                response.emplace("frames_per_second", number_value(frames_per_second));
                response.emplace("key_count", number_value(result.key_count));
                response.emplace(
                    "moved_key_count", number_value(result.moved_key_count));
                constexpr std::size_t kMaxReportedKeys = 256U;
                const std::size_t reported =
                    std::min(selectors.size(), kMaxReportedKeys);
                response.emplace(
                    "keys_truncated", bool_value(selectors.size() > reported));
                json::Value::Array reported_keys;
                reported_keys.reserve(reported);
                for (std::size_t index = 0U; index < reported; ++index) {
                    const TimelineKeySelector& selector = selectors[index];
                    const double previous = index < previous_times.size()
                        ? previous_times[index]
                        : selector.time;
                    const double scaled = result.pivot_time +
                        (previous - result.pivot_time) * applied_request;
                    json::Value::Object entry;
                    entry.emplace("kind", string_value(timeline_key_kind_name(selector.kind)));
                    entry.emplace("animation", string_value(selector.animation_name));
                    switch (selector.kind) {
                    case TimelineKeyKind::Transform:
                        entry.emplace("bone", string_value(selector.bone_name));
                        entry.emplace(
                            "channel",
                            string_value(std::string(
                                transform_channel_name(selector.transform_channel))));
                        break;
                    case TimelineKeyKind::Deform:
                        entry.emplace("slot", string_value(selector.slot_name));
                        entry.emplace(
                            "attachment", string_value(selector.attachment_name));
                        break;
                    case TimelineKeyKind::SlotColor:
                    case TimelineKeyKind::SlotAttachment:
                        entry.emplace("slot", string_value(selector.slot_name));
                        break;
                    case TimelineKeyKind::Event:
                        entry.emplace(
                            "ordinal", number_value(selector.same_time_ordinal));
                        break;
                    case TimelineKeyKind::DrawOrder:
                        break;
                    }
                    entry.emplace("previous_time", number_value(previous));
                    entry.emplace("time", number_value(scaled));
                    entry.emplace(
                        "moved", bool_value(std::abs(scaled - previous) > 1e-12));
                    reported_keys.push_back(object_value(std::move(entry)));
                }
                response.emplace("keys", array_value(std::move(reported_keys)));
                return object_value(std::move(response));
            };

        if (bool_arg(args, "dry_run")) {
            ProjectData candidate = *session.project();
            const marrow::editor::TimelineScaleResult result = apply(&candidate);
            if (!result) {
                return make_error(
                    result.error,
                    op,
                    spec,
                    std::string(classify_timeline_key_error(result.error)));
            }
            return make_success(
                "Timeline key scaling validated.",
                op,
                spec,
                response_delta(result, true));
        }

        auto transaction = session.begin_edit({
            EditKind::EditProperty,
            selectors.size() == 1U
                ? "Scale timeline key via Agent"
                : "Scale timeline keys via Agent",
            "timeline:scale",
            false,
            EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
        if (!transaction) {
            return make_error(transaction.error()->format(), op, spec, "transaction_active");
        }
        const marrow::editor::TimelineScaleResult result = apply(transaction.project());
        if (!result) {
            const std::string error = result.error;
            transaction.cancel();
            return make_error(
                error, op, spec, std::string(classify_timeline_key_error(error)));
        }
        if (!result.changed) {
            transaction.cancel();
            return make_error("No changes made.", op, spec, "no_change");
        }
        if (const std::string auto_curve_error =
                resolve_agent_auto_curves(transaction.project());
            !auto_curve_error.empty()) {
            transaction.cancel();
            return make_error(auto_curve_error, op, spec, "invalid_request");
        }
        if (auto commit = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to scale timeline keys: "})) {
            return std::move(*commit);
        }
        return make_success(
            "Scaled timeline keys successfully.",
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

        if (auto result = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to apply event keyframe: "})) {
            return std::move(*result);
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
        if (auto result = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to remove event keyframe: "})) {
            return std::move(*result);
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
        {
            TimelineKeySelector selector;
            selector.kind = TimelineKeyKind::Deform;
            selector.animation_name = std::string(*anim_name);
            selector.slot_name = std::string(*slot_name);
            selector.attachment_name = std::string(*attachment_name);
            selector.time = *time;
            if (const std::string rejection = managed_loop_boundary_rejection(
                    *session.project(), skeleton, selector);
                !rejection.empty()) {
                return make_error(rejection, op, spec, "invalid_request");
            }
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
        if (auto result = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to apply deform keyframe: "})) {
            return std::move(*result);
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
        {
            TimelineKeySelector selector;
            selector.kind = TimelineKeyKind::Deform;
            selector.animation_name = std::string(*anim_name);
            selector.slot_name = std::string(*slot_name);
            selector.attachment_name = std::string(*attachment_name);
            selector.time = *time;
            if (const std::string rejection = managed_loop_boundary_rejection(
                    project, skeleton, selector);
                !rejection.empty()) {
                transaction.cancel();
                return make_error(rejection, op, spec, "invalid_request");
            }
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
        if (auto result = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to remove deform keyframe: "})) {
            return std::move(*result);
        }
        return make_success("Removed deform keyframe successfully.", op, spec);
    }

    if (op == "set_vertex_weights" || op == "normalize_weights" ||
        op == "mesh.rebind_weights" || op == "mesh.generate_weights") {
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
        const MeshWeightTarget target{
            std::string(*skin_name), std::string(*slot_name), std::string(*attachment_name)};

        // `bones` is REQUIRED for mesh.generate_weights and is never expanded.
        // A default of "every bone" is exactly the silent expansion the story
        // forbids, and a default of "the bones already influencing the vertex"
        // would make the operation a no-op for its main use. Parsed here, before
        // any transaction, so an unresolvable name is reported as `not_found`
        // rather than surfacing from the primitive as `invalid_request`.
        std::vector<std::string> requested_bones;
        if (op == "mesh.generate_weights") {
            const json::Value* bones = json::find_member(*args, "bones");
            if (bones == nullptr || !bones->is_array()) {
                return make_error(
                    "mesh.generate_weights requires a 'bones' array of candidate bone names.",
                    op,
                    spec);
            }
            if (bones->as_array().empty()) {
                return make_error(
                    "mesh.generate_weights requires at least one candidate bone.", op, spec);
            }
            for (const json::Value& bone_value : bones->as_array()) {
                if (!bone_value.is_string() || bone_value.as_string().empty()) {
                    return make_error(
                        "candidate bone names must be non-empty strings.", op, spec);
                }
                const std::string bone_name = bone_value.as_string();
                if (!skeleton.find_bone_index(bone_name).has_value()) {
                    return make_error("Bone not found: " + bone_name, op, spec, "not_found");
                }
                if (std::find(requested_bones.begin(), requested_bones.end(), bone_name) !=
                    requested_bones.end()) {
                    return make_error(
                        "A candidate bone was listed more than once.", op, spec);
                }
                requested_bones.push_back(bone_name);
            }
        }

        // Preflight everything into locals BEFORE opening a transaction. The
        // shipped handler wrote `edit->vertices[i]` for earlier entries and
        // could then reject on a later one, relying on the transaction
        // destructor to unwind; parsing first makes the atomicity local and
        // visible instead.
        std::vector<std::pair<std::size_t, MeshWeightVertexEdit>> requested_vertices;
        std::vector<std::size_t> requested_scope;
        if (op == "set_vertex_weights") {
            // MAR-175 C1: canonicalization is unconditional, so "normalize":
            // false has no implementable meaning. Honouring it re-opens the
            // defects where a committed write could not be saved; ignoring it
            // would report success for a request that was not carried out.
            if (const json::Value* normalize_flag = json::find_member(*args, "normalize");
                normalize_flag != nullptr && normalize_flag->is_boolean() &&
                !normalize_flag->as_boolean()) {
                return make_error(
                    "normalize:false is no longer supported; weight writes are always "
                    "canonicalized.",
                    op,
                    spec,
                    "invalid_request");
            }
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
                const json::Value* influences = json::find_member(vertex_value, "influences");
                if (influences == nullptr || !influences->is_array() ||
                    influences->as_array().empty() ||
                    influences->as_array().size() >
                        mesh_weight_model::kMaxMeshWeightInfluences) {
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
                requested_vertices.emplace_back(vertex_index, std::move(next_vertex));
            }
        } else if (const json::Value* scope = json::find_member(*args, "vertices");
                   scope != nullptr) {
            if (!scope->is_array()) {
                return make_error("vertices must be an array of vertex indices.", op, spec);
            }
            if (scope->as_array().empty()) {
                return make_error(
                    std::string(op) + " requires at least one vertex when 'vertices' is given.",
                    op,
                    spec);
            }
            for (const json::Value& index_value : scope->as_array()) {
                if (!index_value.is_number() || index_value.as_number() < 0.0 ||
                    std::abs(index_value.as_number() - std::round(index_value.as_number())) > 1e-6) {
                    return make_error("vertex index must be a non-negative integer.", op, spec);
                }
                requested_scope.push_back(
                    static_cast<std::size_t>(std::round(index_value.as_number())));
            }
        }

        // Run the identical preflight a live call runs, against a copy. A dry
        // run therefore validates exactly what a live call validates and can
        // report the same affected-vertex payload, without touching
        // project_revision(), undo_count(), or dirty().
        const auto apply_weight_edit = [&](ProjectData* into) -> MeshWeightResult {
            if (op == "set_vertex_weights") {
                return set_mesh_vertex_weights(
                    into, skeleton, *attachment, target, requested_vertices);
            }
            if (op == "normalize_weights") {
                return normalize_mesh_weights(
                    into, skeleton, *attachment, target, requested_scope);
            }
            if (op == "mesh.generate_weights") {
                return generate_mesh_weights(
                    into, skeleton, *attachment, target, requested_bones, requested_scope);
            }
            return rebind_mesh_weights(
                into, skeleton, *attachment, target, requested_scope);
        };

        ProjectData preview_project = *session.project();
        const MeshWeightResult preview_result = apply_weight_edit(&preview_project);
        if (!preview_result) {
            return make_error(preview_result.error, op, spec, "invalid_request");
        }

        const auto build_payload = [&](bool dry_run) {
            json::Value::Object payload;
            payload.emplace("dry_run", bool_value(dry_run));
            payload.emplace("vertex_count", number_value(preview_result.vertex_count));
            payload.emplace(
                "scoped_vertex_count", number_value(preview_result.scoped_vertex_count));
            json::Value::Array affected;
            affected.reserve(preview_result.affected_vertices.size());
            for (const std::size_t index : preview_result.affected_vertices) {
                affected.push_back(number_value(index));
            }
            payload.emplace("affected_vertices", array_value(std::move(affected)));
            payload.emplace("changed", bool_value(preview_result.changed));
            // Emitted by mesh.generate_weights alone. Adding it unconditionally
            // would change the shipped payload of three operations whose exact
            // shape agent_dispatch_smoke asserts.
            if (op == "mesh.generate_weights") {
                payload.emplace(
                    "candidate_bone_count", number_value(requested_bones.size()));
            }
            return object_value(std::move(payload));
        };

        if (bool_arg(args, "dry_run")) {
            return make_success(
                op == "mesh.rebind_weights"
                    ? "Mesh weight rebind validated."
                    : (op == "mesh.generate_weights" ? "Mesh weight generation validated."
                                                     : "Mesh weight edit validated."),
                op,
                spec,
                build_payload(true));
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
        const MeshWeightResult live_result = apply_weight_edit(transaction.project());
        if (!live_result) {
            transaction.cancel();
            return make_error(live_result.error, op, spec, "invalid_request");
        }

        const CommitPolicy commit_policy = op == "normalize_weights"
            ? CommitPolicy{
                  "Failed to apply mesh weights: ",
                  "invalid_request",
                  NoChangeResult::Success,
                  "Mesh weights already normalized.",
                  {}}
            : (op == "mesh.rebind_weights"
                   ? CommitPolicy{
                         "Failed to apply mesh weights: ",
                         "invalid_request",
                         NoChangeResult::Success,
                         "Mesh weights already bound to the setup pose.",
                         {}}
                   : (op == "mesh.generate_weights"
                          ? CommitPolicy{
                                "Failed to apply mesh weights: ",
                                "invalid_request",
                                NoChangeResult::Success,
                                "Mesh weights already match the generated candidates.",
                                {}}
                          : CommitPolicy{"Failed to apply mesh weights: "}));
        if (auto result = commit_or_error(
                transaction, op, spec, commit_policy)) {
            return std::move(*result);
        }
        return make_success(
            op == "mesh.rebind_weights"
                ? "Rebound mesh weights successfully."
                : (op == "mesh.generate_weights" ? "Generated mesh weights successfully."
                                                 : "Edited mesh weights successfully."),
            op,
            spec,
            build_payload(false));
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
        {
            TimelineKeySelector selector;
            selector.kind = TimelineKeyKind::SlotColor;
            selector.animation_name = std::string(*anim_name);
            selector.slot_name = std::string(*slot_name);
            selector.time = *time;
            if (const std::string rejection = managed_loop_boundary_rejection(
                    *session.project(), skeleton, selector);
                !rejection.empty()) {
                return make_error(rejection, op, spec, "invalid_request");
            }
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
        // MAR-171: `interpolation_arg()` returns Linear for an ABSENT member,
        // so "the caller asked for this curve" and "the caller said nothing"
        // are only distinguishable here. An explicitly supplied easing is an
        // absolute authored curve and must demote the key, exactly as
        // `timeline.set_interpolation` does; without that the resolver below
        // would silently overwrite it and the call would report `no_change`.
        // An absent member leaves an automatic key automatic, so a colour-only
        // write still re-resolves against the value it just changed.
        const json::Value* supplied_interpolation =
            json::find_member(*args, "interpolation");
        const bool interpolation_was_supplied =
            supplied_interpolation != nullptr && !supplied_interpolation->is_null();
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
        if (interpolation_was_supplied) {
            key_it->curve_mode = marrow::editor::TimelineCurveMode::Manual;
        }
        if (const std::string auto_curve_error =
                resolve_agent_auto_curves(transaction.project());
            !auto_curve_error.empty()) {
            transaction.cancel();
            return make_error(auto_curve_error, op, spec, "invalid_request");
        }
        if (auto result = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to apply slot color: "})) {
            return std::move(*result);
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
        if (op == "remove_slot_color_keyframe") {
            TimelineKeySelector selector;
            selector.kind = TimelineKeyKind::SlotColor;
            selector.animation_name = std::string(*anim_name);
            selector.slot_name = std::string(*slot_name);
            selector.time = *time;
            if (const std::string rejection = managed_loop_boundary_rejection(
                    project, skeleton, selector);
                !rejection.empty()) {
                transaction.cancel();
                return make_error(rejection, op, spec, "invalid_request");
            }
        }
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
        if (auto result = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to remove slot keyframe: "})) {
            return std::move(*result);
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
        if (auto result = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to apply attachment keyframe: "})) {
            return std::move(*result);
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

        if (auto result = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to apply draw-order keyframe: "})) {
            return std::move(*result);
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

        if (auto result = commit_or_error(
                transaction,
                op,
                spec,
                CommitPolicy{"Failed to remove draw-order keyframe: "})) {
            return std::move(*result);
        }

        return make_success("Removed draw-order keyframe successfully.", op, spec);
    }

    return make_error("Unknown operation: " + std::string(op), op, spec, "unknown_operation");
}

} // namespace marrow::editor::agent_detail
