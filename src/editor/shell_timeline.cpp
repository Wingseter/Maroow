#include "shell_timeline.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "imgui.h"

#include "shell_selection.hpp"
#include "shell_preview.hpp"
#include "shell_state.hpp"
#include "shell_widgets.hpp"
#include "marrow/editor/agent_dispatch.hpp"

namespace marrow::editor::shell {

using marrow::editor::Icon;
using marrow::editor::IconRegistry;

namespace {

template <typename MutateFn>
bool apply_timeline_project_drag(
    ShellState* state,
    bool changed,
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge,
    std::string failure_status,
    MutateFn mutate) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    const ImGuiID item_id = ImGui::GetItemID();
    if (ImGui::IsItemActivated()) {
        state->pending_edit_action = PendingEditAction{
            item_id,
            kind,
            std::move(label),
            std::move(group),
            allow_merge,
            capture_history_snapshot(*state)};
    }

    if (changed) {
        const EditorHistorySnapshot rollback = capture_history_snapshot(*state, false);
        mutate();
        if (!rebuild_project_runtime(state)) {
            const std::string rebuild_error = state->error_message;
            restore_history_snapshot(state, rollback);
            state->pending_edit_action.reset();
            state->error_message = rebuild_error;
            state->status_message = std::move(failure_status);
            return false;
        }
    }

    if (ImGui::IsItemDeactivatedAfterEdit() &&
        state->pending_edit_action.has_value() &&
        state->pending_edit_action->item_id == item_id) {
        PendingEditAction pending = std::move(*state->pending_edit_action);
        state->pending_edit_action.reset();
        return record_action_from_snapshots(
            state,
            pending.before_snapshot,
            pending.kind,
            std::move(pending.label),
            std::move(pending.group),
            pending.allow_merge);
    }

    if (ImGui::IsItemDeactivated() &&
        state->pending_edit_action.has_value() &&
        state->pending_edit_action->item_id == item_id) {
        state->pending_edit_action.reset();
    }

    return true;
}


} // namespace

std::optional<std::size_t> draw_order_position(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t slot_index) {
    const auto& draw_order = skeleton.draw_order();
    const auto it = std::find(draw_order.begin(), draw_order.end(), slot_index);
    if (it == draw_order.end()) {
        return std::nullopt;
    }

    return static_cast<std::size_t>(std::distance(draw_order.begin(), it));
}

// selected_animation moved to shell_core.cpp

// queued_preview_animation moved to shell_core.cpp

// normalize_state_preview_settings and timeline_preview_duration moved to shell_core.cpp

template <typename Timeline>
void append_timeline_key_times(
    const std::vector<Timeline>& timelines,
    std::vector<double>* key_times) {
    if (key_times == nullptr) {
        return;
    }

    for (const Timeline& timeline : timelines) {
        for (const auto& keyframe : timeline.keyframes) {
            key_times->push_back(static_cast<double>(keyframe.time));
        }
    }
}

std::vector<double> collect_animation_key_times(const marrow::runtime::AnimationData& animation) {
    std::vector<double> key_times;
    append_timeline_key_times(animation.bone_rotate_timelines, &key_times);
    append_timeline_key_times(animation.bone_inherit_timelines, &key_times);
    append_timeline_key_times(animation.bone_translate_timelines, &key_times);
    append_timeline_key_times(animation.bone_scale_timelines, &key_times);
    append_timeline_key_times(animation.bone_shear_timelines, &key_times);
    append_timeline_key_times(animation.slot_attachment_timelines, &key_times);
    append_timeline_key_times(animation.slot_color_timelines, &key_times);
    append_timeline_key_times(animation.mesh_deform_timelines, &key_times);
    if (animation.draw_order_timeline_data.has_value()) {
        for (const auto& keyframe : animation.draw_order_timeline_data->keyframes) {
            key_times.push_back(static_cast<double>(keyframe.time));
        }
    }
    if (animation.event_timeline_data.has_value()) {
        for (const auto& keyframe : animation.event_timeline_data->keyframes) {
            key_times.push_back(static_cast<double>(keyframe.time));
        }
    }

    std::sort(key_times.begin(), key_times.end());
    key_times.erase(
        std::unique(
            key_times.begin(),
            key_times.end(),
            [](double lhs, double rhs) { return std::abs(lhs - rhs) <= 1e-6; }),
        key_times.end());
    return key_times;
}

std::string format_time_seconds(double time_seconds) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << time_seconds << "s";
    return stream.str();
}

template <typename Keyframe>
std::vector<double> collect_key_times(const std::vector<Keyframe>& keyframes) {
    std::vector<double> key_times;
    key_times.reserve(keyframes.size());
    for (const Keyframe& keyframe : keyframes) {
        key_times.push_back(static_cast<double>(keyframe.time));
    }
    return key_times;
}

std::vector<TimelineTrackRow> build_timeline_tracks(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::AnimationData& animation) {
    std::vector<TimelineTrackRow> tracks;
    tracks.reserve(
        animation.bone_rotate_timelines.size() +
        animation.bone_inherit_timelines.size() +
        animation.bone_translate_timelines.size() +
        animation.bone_scale_timelines.size() +
        animation.bone_shear_timelines.size() +
        animation.slot_attachment_timelines.size() +
        animation.slot_color_timelines.size() +
        animation.mesh_deform_timelines.size() +
        (animation.draw_order_timeline_data.has_value() ? 1U : 0U) +
        (animation.event_timeline_data.has_value() ? 1U : 0U));

    const auto add_bone_track = [&](std::string_view suffix,
                                    std::size_t bone_index,
                                    const auto& keyframes,
                                    std::optional<marrow::editor::TransformTimelineChannel> transform_channel) {
        if (bone_index >= skeleton.bones().size() || keyframes.empty()) {
            return;
        }

        tracks.push_back(TimelineTrackRow{
            "bone:" + std::to_string(bone_index) + ":" + std::string(suffix),
            "Bone / " + skeleton.bones()[bone_index].name + " / " + std::string(suffix),
            animation.name,
            collect_key_times(keyframes),
            bone_index,
            std::nullopt,
            transform_channel,
            std::nullopt});
    };
    const auto add_slot_track = [&](std::string_view suffix,
                                    std::size_t slot_index,
                                    const auto& keyframes) {
        if (slot_index >= skeleton.slots().size() || keyframes.empty()) {
            return;
        }

        tracks.push_back(TimelineTrackRow{
            "slot:" + std::to_string(slot_index) + ":" + std::string(suffix),
            "Slot / " + skeleton.slots()[slot_index].name + " / " + std::string(suffix),
            animation.name,
            collect_key_times(keyframes),
            std::nullopt,
            slot_index,
            std::nullopt,
            std::nullopt});
    };

    for (const auto& timeline : animation.bone_rotate_timelines) {
        add_bone_track(
            "Rotate",
            timeline.bone_index,
            timeline.keyframes,
            marrow::editor::TransformTimelineChannel::Rotate);
    }
    for (const auto& timeline : animation.bone_translate_timelines) {
        add_bone_track(
            "Translate",
            timeline.bone_index,
            timeline.keyframes,
            marrow::editor::TransformTimelineChannel::Translate);
    }
    for (const auto& timeline : animation.bone_scale_timelines) {
        add_bone_track(
            "Scale",
            timeline.bone_index,
            timeline.keyframes,
            marrow::editor::TransformTimelineChannel::Scale);
    }
    for (const auto& timeline : animation.bone_shear_timelines) {
        add_bone_track(
            "Shear",
            timeline.bone_index,
            timeline.keyframes,
            marrow::editor::TransformTimelineChannel::Shear);
    }
    for (const auto& timeline : animation.bone_inherit_timelines) {
        add_bone_track("Inherit", timeline.bone_index, timeline.keyframes, std::nullopt);
    }
    for (const auto& timeline : animation.slot_attachment_timelines) {
        add_slot_track("Attachment", timeline.slot_index, timeline.keyframes);
    }
    for (const auto& timeline : animation.slot_color_timelines) {
        add_slot_track("Color", timeline.slot_index, timeline.keyframes);
    }
    for (const auto& timeline : animation.mesh_deform_timelines) {
        if (timeline.slot_index >= skeleton.slots().size() || timeline.keyframes.empty()) {
            continue;
        }

        tracks.push_back(TimelineTrackRow{
            "slot:" + std::to_string(timeline.slot_index) + ":deform:" + timeline.attachment_name,
            "Slot / " + skeleton.slots()[timeline.slot_index].name +
                " / Deform / " + timeline.attachment_name,
            animation.name,
            collect_key_times(timeline.keyframes),
            std::nullopt,
            timeline.slot_index,
            std::nullopt,
            timeline.attachment_name});
    }

    if (animation.draw_order_timeline_data.has_value() &&
        !animation.draw_order_timeline_data->keyframes.empty()) {
        tracks.push_back(TimelineTrackRow{
            "global:draw-order",
            "Global / Draw Order",
            animation.name,
            collect_key_times(animation.draw_order_timeline_data->keyframes),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt});
    }

    if (animation.event_timeline_data.has_value() &&
        !animation.event_timeline_data->keyframes.empty()) {
        tracks.push_back(TimelineTrackRow{
            "global:events",
            "Global / Events",
            animation.name,
            collect_key_times(animation.event_timeline_data->keyframes),
            std::nullopt,
            std::nullopt,
            std::nullopt,
            std::nullopt});
    }

    return tracks;
}

const TimelineTrackRow* selected_timeline_track(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (!state.selected_timeline_track_id.has_value()) {
        return nullptr;
    }

    const auto iterator = std::find_if(
        tracks.begin(),
        tracks.end(),
        [&](const TimelineTrackRow& track) {
            return track.id == *state.selected_timeline_track_id;
        });
    return iterator == tracks.end() ? nullptr : &(*iterator);
}

bool timeline_track_matches_selection(
    const ShellState& state,
    const TimelineTrackRow& track) {
    if (state.selected_timeline_track_id.has_value() &&
        *state.selected_timeline_track_id == track.id) {
        return true;
    }

    if (track.slot_index.has_value() &&
        state.selected_slot_index == track.slot_index) {
        return true;
    }

    return track.bone_index.has_value() &&
        state.selected_bone_index == track.bone_index;
}

std::optional<double> adjacent_key_time(
    const std::vector<TimelineTrackRow>& tracks,
    double current_time,
    bool forward) {
    std::optional<double> best_time;

    for (const TimelineTrackRow& track : tracks) {
        for (const double key_time : track.key_times) {
            if (forward) {
                if (key_time <= current_time + 1e-6) {
                    continue;
                }
                if (!best_time.has_value() || key_time < *best_time) {
                    best_time = key_time;
                }
            } else {
                if (key_time >= current_time - 1e-6) {
                    continue;
                }
                if (!best_time.has_value() || key_time > *best_time) {
                    best_time = key_time;
                }
            }
        }
    }

    return best_time;
}

std::optional<double> nearest_key_time(
    const TimelineTrackRow& track,
    double target_time,
    double threshold_time) {
    std::optional<double> best_time;
    double best_distance = threshold_time;

    for (const double key_time : track.key_times) {
        const double distance = std::abs(key_time - target_time);
        if (distance > best_distance) {
            continue;
        }

        best_distance = distance;
        best_time = key_time;
    }

    return best_time;
}


std::string_view transform_channel_label(marrow::editor::TransformTimelineChannel channel) {
    switch (channel) {
    case marrow::editor::TransformTimelineChannel::Rotate:
        return "Rotate";
    case marrow::editor::TransformTimelineChannel::Translate:
        return "Translate";
    case marrow::editor::TransformTimelineChannel::Scale:
        return "Scale";
    case marrow::editor::TransformTimelineChannel::Shear:
        return "Shear";
    }

    return "Rotate";
}

void copy_rotate_timeline_edit(
    const std::vector<marrow::runtime::RotateKeyframe>& source,
    marrow::editor::TransformTimelineEdit* edit) {
    edit->keyframes.clear();
    edit->keyframes.reserve(source.size());
    for (const auto& keyframe : source) {
        marrow::editor::TransformKeyframeEdit copied;
        copied.time = static_cast<double>(keyframe.time);
        copied.angle = static_cast<double>(keyframe.angle);
        copied.interpolation = keyframe.interpolation;
        edit->keyframes.push_back(std::move(copied));
    }
}

void copy_vector_timeline_edit(
    const std::vector<marrow::runtime::VectorKeyframe>& source,
    marrow::editor::TransformTimelineEdit* edit) {
    edit->keyframes.clear();
    edit->keyframes.reserve(source.size());
    for (const auto& keyframe : source) {
        marrow::editor::TransformKeyframeEdit copied;
        copied.time = static_cast<double>(keyframe.time);
        copied.x = static_cast<double>(keyframe.x);
        copied.y = static_cast<double>(keyframe.y);
        copied.interpolation = keyframe.interpolation;
        edit->keyframes.push_back(std::move(copied));
    }
}

void copy_deform_timeline_edit(
    const std::vector<marrow::runtime::DeformKeyframe>& source,
    marrow::editor::MeshDeformTimelineEdit* edit) {
    edit->keyframes.clear();
    edit->keyframes.reserve(source.size());
    for (const auto& keyframe : source) {
        marrow::editor::DeformKeyframeEdit copied;
        copied.time = static_cast<double>(keyframe.time);
        copied.vertex_offsets.reserve(keyframe.vertex_offsets.size());
        for (const marrow::runtime::AnimationScalar offset : keyframe.vertex_offsets) {
            copied.vertex_offsets.push_back(static_cast<double>(offset));
        }
        copied.interpolation = keyframe.interpolation;
        edit->keyframes.push_back(std::move(copied));
    }
}

std::optional<double> widen_animation_optional(
    const std::optional<marrow::runtime::AnimationScalar>& value) {
    if (!value.has_value()) {
        return std::nullopt;
    }
    return static_cast<double>(*value);
}

std::optional<marrow::editor::TransformTimelineEdit> make_transform_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track) {
    if (!state.load_result || !track.transform_channel.has_value() ||
        !track.bone_index.has_value() ||
        *track.bone_index >= state.load_result.skeleton_data->bones().size()) {
        return std::nullopt;
    }

    const auto* animation =
        state.load_result.skeleton_data->find_animation(track.animation_name);
    if (animation == nullptr) {
        return std::nullopt;
    }

    marrow::editor::TransformTimelineEdit edit;
    edit.animation_name = track.animation_name;
    edit.bone_name = state.load_result.skeleton_data->bones()[*track.bone_index].name;
    edit.channel = *track.transform_channel;

    switch (*track.transform_channel) {
    case marrow::editor::TransformTimelineChannel::Rotate: {
        const auto* timeline = animation->find_rotate_timeline(*track.bone_index);
        if (timeline == nullptr) {
            return std::nullopt;
        }
        copy_rotate_timeline_edit(timeline->keyframes, &edit);
        break;
    }
    case marrow::editor::TransformTimelineChannel::Translate: {
        const auto* timeline = animation->find_translate_timeline(*track.bone_index);
        if (timeline == nullptr) {
            return std::nullopt;
        }
        copy_vector_timeline_edit(timeline->keyframes, &edit);
        break;
    }
    case marrow::editor::TransformTimelineChannel::Scale: {
        const auto* timeline = animation->find_scale_timeline(*track.bone_index);
        if (timeline == nullptr) {
            return std::nullopt;
        }
        copy_vector_timeline_edit(timeline->keyframes, &edit);
        break;
    }
    case marrow::editor::TransformTimelineChannel::Shear: {
        const auto* timeline = animation->find_shear_timeline(*track.bone_index);
        if (timeline == nullptr) {
            return std::nullopt;
        }
        copy_vector_timeline_edit(timeline->keyframes, &edit);
        break;
    }
    }

    return edit;
}

std::optional<marrow::editor::MeshDeformTimelineEdit> make_mesh_deform_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track) {
    if (!state.load_result || !track.slot_index.has_value() ||
        !track.deform_attachment_name.has_value() ||
        *track.slot_index >= state.load_result.skeleton_data->slots().size()) {
        return std::nullopt;
    }

    const auto* animation =
        state.load_result.skeleton_data->find_animation(track.animation_name);
    if (animation == nullptr) {
        return std::nullopt;
    }

    const auto* timeline = animation->find_deform_timeline(
        *track.slot_index, *track.deform_attachment_name);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    marrow::editor::MeshDeformTimelineEdit edit;
    edit.animation_name = track.animation_name;
    edit.slot_name = state.load_result.skeleton_data->slots()[*track.slot_index].name;
    edit.attachment_name = *track.deform_attachment_name;
    copy_deform_timeline_edit(timeline->keyframes, &edit);
    return edit;
}

std::optional<std::size_t> ensure_transform_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || !track.transform_channel.has_value() ||
        !track.bone_index.has_value() ||
        *track.bone_index >= state->load_result.skeleton_data->bones().size()) {
        return std::nullopt;
    }

    const std::string& bone_name =
        state->load_result.skeleton_data->bones()[*track.bone_index].name;
    const auto existing = std::find_if(
        state->load_result.project->transform_timeline_edits.begin(),
        state->load_result.project->transform_timeline_edits.end(),
        [&](const marrow::editor::TransformTimelineEdit& edit) {
            return edit.animation_name == track.animation_name &&
                edit.bone_name == bone_name &&
                edit.channel == *track.transform_channel;
        });
    if (existing != state->load_result.project->transform_timeline_edits.end()) {
        return static_cast<std::size_t>(
            std::distance(
                state->load_result.project->transform_timeline_edits.begin(),
                existing));
    }

    const auto edit = make_transform_timeline_edit(*state, track);
    if (!edit.has_value()) {
        return std::nullopt;
    }

    state->load_result.project->transform_timeline_edits.push_back(*edit);
    return state->load_result.project->transform_timeline_edits.size() - 1U;
}

std::optional<std::size_t> ensure_mesh_deform_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || !track.slot_index.has_value() ||
        !track.deform_attachment_name.has_value() ||
        *track.slot_index >= state->load_result.skeleton_data->slots().size()) {
        return std::nullopt;
    }

    const std::string& slot_name =
        state->load_result.skeleton_data->slots()[*track.slot_index].name;
    const auto existing = std::find_if(
        state->load_result.project->mesh_deform_timeline_edits.begin(),
        state->load_result.project->mesh_deform_timeline_edits.end(),
        [&](const marrow::editor::MeshDeformTimelineEdit& edit) {
            return edit.animation_name == track.animation_name &&
                edit.slot_name == slot_name &&
                edit.attachment_name == *track.deform_attachment_name;
        });
    if (existing != state->load_result.project->mesh_deform_timeline_edits.end()) {
        return static_cast<std::size_t>(
            std::distance(
                state->load_result.project->mesh_deform_timeline_edits.begin(),
                existing));
    }

    const auto edit = make_mesh_deform_timeline_edit(*state, track);
    if (!edit.has_value()) {
        return std::nullopt;
    }

    state->load_result.project->mesh_deform_timeline_edits.push_back(*edit);
    return state->load_result.project->mesh_deform_timeline_edits.size() - 1U;
}

std::vector<std::string> slot_names_from_indices(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::size_t>& slot_indices) {
    std::vector<std::string> slot_names;
    slot_names.reserve(slot_indices.size());
    for (const std::size_t slot_index : slot_indices) {
        if (slot_index >= skeleton.slots().size()) {
            return {};
        }
        slot_names.push_back(skeleton.slots()[slot_index].name);
    }
    return slot_names;
}

std::optional<marrow::editor::DrawOrderTimelineEdit> make_draw_order_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track) {
    if (!state.load_result || track.id != "global:draw-order") {
        return std::nullopt;
    }

    const auto* animation =
        state.load_result.skeleton_data->find_animation(track.animation_name);
    const auto* timeline =
        animation != nullptr ? animation->find_draw_order_timeline() : nullptr;
    if (timeline == nullptr) {
        return std::nullopt;
    }

    marrow::editor::DrawOrderTimelineEdit edit;
    edit.animation_name = track.animation_name;
    edit.keyframes.reserve(timeline->keyframes.size());
    for (const auto& keyframe : timeline->keyframes) {
        const std::vector<std::string> slot_names =
            slot_names_from_indices(*state.load_result.skeleton_data, keyframe.slot_indices);
        if (slot_names.size() != keyframe.slot_indices.size()) {
            return std::nullopt;
        }

        marrow::editor::DrawOrderKeyframeEdit copied;
        copied.time = static_cast<double>(keyframe.time);
        copied.slot_names = slot_names;
        edit.keyframes.push_back(std::move(copied));
    }

    return edit;
}

std::optional<std::size_t> ensure_draw_order_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || track.id != "global:draw-order") {
        return std::nullopt;
    }

    const auto existing = std::find_if(
        state->load_result.project->draw_order_timeline_edits.begin(),
        state->load_result.project->draw_order_timeline_edits.end(),
        [&](const marrow::editor::DrawOrderTimelineEdit& edit) {
            return edit.animation_name == track.animation_name;
        });
    if (existing != state->load_result.project->draw_order_timeline_edits.end()) {
        return static_cast<std::size_t>(
            std::distance(
                state->load_result.project->draw_order_timeline_edits.begin(),
                existing));
    }

    const auto edit = make_draw_order_timeline_edit(*state, track);
    if (!edit.has_value()) {
        return std::nullopt;
    }

    state->load_result.project->draw_order_timeline_edits.push_back(*edit);
    return state->load_result.project->draw_order_timeline_edits.size() - 1U;
}

std::optional<marrow::editor::EventTimelineEdit> make_event_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track) {
    if (!state.load_result || track.id != "global:events") {
        return std::nullopt;
    }

    const auto* animation =
        state.load_result.skeleton_data->find_animation(track.animation_name);
    const auto* timeline =
        animation != nullptr ? animation->find_event_timeline() : nullptr;
    if (timeline == nullptr) {
        return std::nullopt;
    }

    marrow::editor::EventTimelineEdit edit;
    edit.animation_name = track.animation_name;
    edit.keyframes.reserve(timeline->keyframes.size());
    for (const auto& keyframe : timeline->keyframes) {
        if (keyframe.event_index >= state.load_result.skeleton_data->events().size()) {
            return std::nullopt;
        }

        marrow::editor::EventKeyframeEdit copied;
        copied.time = static_cast<double>(keyframe.time);
        copied.event_name =
            state.load_result.skeleton_data->events()[keyframe.event_index].name;
        copied.int_value = keyframe.int_value;
        copied.float_value = widen_animation_optional(keyframe.float_value);
        copied.string_value = keyframe.string_value;
        copied.audio_path = keyframe.audio_path;
        copied.volume = widen_animation_optional(keyframe.volume);
        copied.balance = widen_animation_optional(keyframe.balance);
        edit.keyframes.push_back(std::move(copied));
    }

    return edit;
}

std::optional<std::size_t> ensure_event_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || track.id != "global:events") {
        return std::nullopt;
    }

    const auto existing = std::find_if(
        state->load_result.project->event_timeline_edits.begin(),
        state->load_result.project->event_timeline_edits.end(),
        [&](const marrow::editor::EventTimelineEdit& edit) {
            return edit.animation_name == track.animation_name;
        });
    if (existing != state->load_result.project->event_timeline_edits.end()) {
        return static_cast<std::size_t>(
            std::distance(
                state->load_result.project->event_timeline_edits.begin(),
                existing));
    }

    const auto edit = make_event_timeline_edit(*state, track);
    if (!edit.has_value()) {
        return std::nullopt;
    }

    state->load_result.project->event_timeline_edits.push_back(*edit);
    return state->load_result.project->event_timeline_edits.size() - 1U;
}

marrow::editor::TransformKeyframeEdit sample_transform_keyframe(
    const ShellState& state,
    const TimelineTrackRow& track) {
    marrow::editor::TransformKeyframeEdit keyframe;
    keyframe.time = state.timeline_time_seconds;
    keyframe.interpolation = marrow::runtime::Interpolation::linear();

    if (!state.preview_skeleton || !track.bone_index.has_value() ||
        *track.bone_index >= state.preview_skeleton->bone_poses().size() ||
        !track.transform_channel.has_value()) {
        return keyframe;
    }

    const auto& pose = state.preview_skeleton->bone_poses()[*track.bone_index].local_pose;
    switch (*track.transform_channel) {
    case marrow::editor::TransformTimelineChannel::Rotate:
        keyframe.angle = static_cast<double>(pose.rotation);
        break;
    case marrow::editor::TransformTimelineChannel::Translate:
        keyframe.x = static_cast<double>(pose.x);
        keyframe.y = static_cast<double>(pose.y);
        break;
    case marrow::editor::TransformTimelineChannel::Scale:
        keyframe.x = static_cast<double>(pose.scale_x);
        keyframe.y = static_cast<double>(pose.scale_y);
        break;
    case marrow::editor::TransformTimelineChannel::Shear:
        keyframe.x = static_cast<double>(pose.shear_x);
        keyframe.y = static_cast<double>(pose.shear_y);
        break;
    }

    return keyframe;
}

marrow::editor::DrawOrderKeyframeEdit sample_draw_order_keyframe(const ShellState& state) {
    marrow::editor::DrawOrderKeyframeEdit keyframe;
    keyframe.time = state.timeline_time_seconds;
    if (!state.load_result || !state.preview_skeleton) {
        return keyframe;
    }

    keyframe.slot_names = slot_names_from_indices(
        *state.load_result.skeleton_data,
        state.preview_skeleton->draw_order());
    return keyframe;
}

marrow::editor::EventKeyframeEdit sample_event_keyframe(const ShellState& state) {
    marrow::editor::EventKeyframeEdit keyframe;
    keyframe.time = state.timeline_time_seconds;
    if (!state.load_result || state.load_result.skeleton_data->events().empty()) {
        return keyframe;
    }

    keyframe.event_name = state.load_result.skeleton_data->events().front().name;
    return keyframe;
}

marrow::editor::DeformKeyframeEdit sample_deform_keyframe(
    const ShellState& state,
    const TimelineTrackRow& track) {
    marrow::editor::DeformKeyframeEdit keyframe;
    keyframe.time = state.timeline_time_seconds;
    keyframe.interpolation = marrow::runtime::Interpolation::linear();

    if (!state.load_result || !state.preview_skeleton || !track.slot_index.has_value() ||
        !track.deform_attachment_name.has_value()) {
        return keyframe;
    }

    const auto* attachment = state.load_result.skeleton_data->find_attachment_source(
        *track.slot_index, *track.deform_attachment_name);
    if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
        return keyframe;
    }

    keyframe.vertex_offsets.assign(attachment->mesh_geometry->vertices.size(), 0.0);
    if (*track.slot_index >= state.preview_skeleton->mesh_deform_states().size()) {
        return keyframe;
    }

    const auto& deform_state =
        state.preview_skeleton->mesh_deform_states()[*track.slot_index];
    if (deform_state.attachment_name == *track.deform_attachment_name &&
        deform_state.vertex_offsets.size() == keyframe.vertex_offsets.size()) {
        keyframe.vertex_offsets = deform_state.vertex_offsets;
    }

    return keyframe;
}


template <typename Keyframe>
double clamp_existing_key_time(
    const std::vector<Keyframe>& keyframes,
    std::size_t key_index,
    double desired_time,
    double duration) {
    constexpr double kKeySpacing = 0.001;
    const double minimum_time =
        key_index == 0 ? 0.0 : keyframes[key_index - 1].time + kKeySpacing;
    if (key_index + 1 >= keyframes.size()) {
        return std::max(desired_time, minimum_time);
    }

    const double maximum_time = keyframes[key_index + 1].time - kKeySpacing;
    if (maximum_time < minimum_time) {
        return minimum_time;
    }

    return std::clamp(desired_time, minimum_time, maximum_time);
}

template <typename Keyframe>
double clamp_existing_non_decreasing_key_time(
    const std::vector<Keyframe>& keyframes,
    std::size_t key_index,
    double desired_time) {
    const double minimum_time =
        key_index == 0 ? 0.0 : keyframes[key_index - 1].time;
    if (key_index + 1 >= keyframes.size()) {
        return std::max(desired_time, minimum_time);
    }

    return std::clamp(desired_time, minimum_time, keyframes[key_index + 1].time);
}


bool set_selected_animation(
    ShellState* state,
    std::string_view animation_name,
    std::string_view source,
    bool update_status_message,
    bool reset_time) {
    if (!state->load_result) {
        return false;
    }

    const marrow::runtime::AnimationData* animation =
        state->load_result.skeleton_data->find_animation(animation_name);
    if (animation == nullptr) {
        return false;
    }

    state->selected_animation_name = animation->name;
    state->selected_timeline_track_id.reset();
    normalize_state_preview_settings(state);
    if (reset_time) {
        state->timeline_time_seconds = 0.0;
    } else {
        state->timeline_time_seconds = std::clamp(
            state->timeline_time_seconds,
            0.0,
            timeline_preview_duration(*state));
    }

    if (!state->session.select_animation(animation->name, reset_time) ||
        !state->session.set_loop(state->timeline_loop) ||
        !state->session.set_reverse(state->preview_reverse)) {
        sync_shell_from_editor_session(state);
        return false;
    }
    if (state->preview_queue_enabled) {
        const std::optional<double> mix_duration = state->preview_use_custom_mix_duration
            ? std::optional<double>(state->preview_custom_mix_duration)
            : std::nullopt;
        if (!state->session.set_queue(
                state->preview_queued_animation_name,
                state->preview_queue_delay,
                mix_duration)) {
            sync_shell_from_editor_session(state);
            return false;
        }
    } else if (!state->session.clear_queue()) {
        sync_shell_from_editor_session(state);
        return false;
    }
    state->session.set_playing(state->timeline_playing);
    if (!state->session.seek(state->timeline_time_seconds)) {
        sync_shell_from_editor_session(state);
        return false;
    }
    sync_shell_from_editor_session(state);

    if (update_status_message) {
        std::ostringstream stream;
        stream << "Selected animation " << animation->name;
        if (!source.empty()) {
            stream << " via " << source;
        }
        state->status_message = stream.str();
    }

    return true;
}

bool scrub_timeline_time(
    ShellState* state,
    double time_seconds,
    std::string_view source,
    bool update_status_message) {
    const double duration = timeline_preview_duration(*state);
    state->timeline_time_seconds =
        duration > 0.0 ? std::clamp(time_seconds, 0.0, duration) : 0.0;
    if (!state->session.set_loop(state->timeline_loop) ||
        !state->session.set_reverse(state->preview_reverse)) {
        return false;
    }
    if (state->preview_queue_enabled) {
        const std::optional<double> mix_duration = state->preview_use_custom_mix_duration
            ? std::optional<double>(state->preview_custom_mix_duration)
            : std::nullopt;
        if (!state->session.set_queue(
                state->preview_queued_animation_name,
                state->preview_queue_delay,
                mix_duration)) {
            return false;
        }
    } else if (!state->session.clear_queue()) {
        return false;
    }
    state->session.set_playing(state->timeline_playing);
    if (!state->session.seek(state->timeline_time_seconds)) {
        return false;
    }
    sync_shell_from_editor_session(state);

    if (update_status_message) {
        std::ostringstream stream;
        stream << "Scrubbed " << format_time_seconds(state->timeline_time_seconds);
        if (!state->selected_animation_name.empty()) {
            stream << " on " << state->selected_animation_name;
        }
        if (!source.empty()) {
            stream << " via " << source;
        }
        state->status_message = stream.str();
    }

    return true;
}

void advance_timeline_playback(ShellState* state, double delta_seconds) {
    if (!state->timeline_playing || delta_seconds <= 0.0) {
        return;
    }

    if (!state->animation_state || !state->preview_skeleton || !state->load_result) {
        state->timeline_playing = false;
        state->session.set_playing(false);
        return;
    }
    state->session.set_playing(true);
    (void)state->session.advance(delta_seconds);
    sync_shell_from_editor_session(state);
}

void advance_timeline_playback(ShellState* state, float delta_seconds) {
    advance_timeline_playback(state, static_cast<double>(delta_seconds));
}

bool focus_timeline_track(
    ShellState* state,
    const TimelineTrackRow& track,
    double time_seconds,
    std::string_view source,
    bool update_status_message) {
    state->selected_timeline_track_id = track.id;
    if (track.slot_index.has_value()) {
        select_slot(state, *track.slot_index, source, false);
    } else if (track.bone_index.has_value()) {
        select_bone(state, *track.bone_index, source, false);
    }

    if (!scrub_timeline_time(state, time_seconds, source, false)) {
        return false;
    }

    if (update_status_message) {
        std::ostringstream stream;
        stream << "Focused " << track.label
               << " at " << format_time_seconds(state->timeline_time_seconds);
        if (!source.empty()) {
            stream << " via " << source;
        }
        state->status_message = stream.str();
    }

    return true;
}



marrow::editor::Icon track_property_icon(const std::string& track_id) {
    if (track_id.find(":Rotate") != std::string::npos)      return Icon::PropRotate;
    if (track_id.find(":Translate") != std::string::npos)   return Icon::PropTranslate;
    if (track_id.find(":Scale") != std::string::npos)       return Icon::PropScale;
    if (track_id.find(":Shear") != std::string::npos)       return Icon::PropShear;
    if (track_id.find(":Color") != std::string::npos)       return Icon::PropColor;
    if (track_id.find(":Attachment") != std::string::npos)  return Icon::AttRegion;
    if (track_id.find(":deform:") != std::string::npos)     return Icon::AttMesh;
    if (track_id.find("draw_order") != std::string::npos)   return Icon::PropOrder;
    if (track_id.find("event") != std::string::npos)        return Icon::PropEvent;
    return Icon::NodeBone;
}

// A muted left-edge accent grouping tracks by property family. Stays within
// the restrained v2 palette: transform family → primary-dim, colour → the
// tertiary accent, structural (attachment/order/event) → secondary. Used as a
// 2px rail so unselected lanes still read as grouped without new hues.
ImU32 track_group_accent(const std::string& track_id) {
    if (track_id.find(":Rotate") != std::string::npos ||
        track_id.find(":Translate") != std::string::npos ||
        track_id.find(":Scale") != std::string::npos ||
        track_id.find(":Shear") != std::string::npos) {
        return IM_COL32(0x52, 0x69, 0xa8, 0x80);  // primary-dim @ 50%
    }
    if (track_id.find(":Color") != std::string::npos) {
        return IM_COL32(0xa8, 0x46, 0x43, 0x80);  // tertiary-dim @ 50%
    }
    return IM_COL32(0xbe, 0xc7, 0xdc, 0x4D);      // secondary @ 30%
}

void draw_timeline_lane(
    ShellState* state,
    const TimelineTrackRow& track,
    double duration_seconds) {
    ImGui::PushID(track.id.c_str());

    const bool selected = timeline_track_matches_selection(*state, track);
    const float lane_height = 24.0f;
    const float lane_width = std::max(96.0f, ImGui::GetContentRegionAvail().x);
    ImGui::InvisibleButton("timeline_lane", ImVec2(lane_width, lane_height));

    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    const ImVec2 rect_min = ImGui::GetItemRectMin();
    const ImVec2 rect_max = ImGui::GetItemRectMax();
    const float rect_width = std::max(1.0f, rect_max.x - rect_min.x);

    // Charcoal Studio lane: transparent when unselected (table zebra shows
    // through), subtle primary tint when selected. No opaque border — ghost
    // 20% outline_variant instead.
    if (selected) {
        draw_list->AddRectFilled(
            rect_min,
            rect_max,
            IM_COL32(0x60, 0x8b, 0xff, 0x1F),  // primary-container @ ~12%
            2.0f);
        draw_list->AddLine(
            ImVec2(rect_min.x, rect_min.y),
            ImVec2(rect_min.x, rect_max.y),
            IM_COL32(0x60, 0x8b, 0xff, 0xFF),  // primary-container solid
            2.0f);
    } else {
        // Property-group accent rail (muted; selection overrides it above).
        draw_list->AddLine(
            ImVec2(rect_min.x, rect_min.y),
            ImVec2(rect_min.x, rect_max.y),
            track_group_accent(track.id),
            2.0f);
    }

    // Subtle quarter-ticks (outline_variant @ 30%)
    for (int tick_index = 1; tick_index < 4; ++tick_index) {
        const float normalized = static_cast<float>(tick_index) / 4.0f;
        const float tick_x = rect_min.x + (rect_width * normalized);
        draw_list->AddLine(
            ImVec2(tick_x, rect_min.y + 2.0f),
            ImVec2(tick_x, rect_max.y - 2.0f),
            IM_COL32(0x43, 0x46, 0x54, 0x4D));  // outline_variant @ 30%
    }

    // Horizontal interpolation hint line connecting keyframes
    if (track.key_times.size() >= 2) {
        const float first_normalized = duration_seconds > 0.0
            ? static_cast<float>(std::clamp(track.key_times.front() / duration_seconds, 0.0, 1.0))
            : 0.0f;
        const float last_normalized = duration_seconds > 0.0
            ? static_cast<float>(std::clamp(track.key_times.back() / duration_seconds, 0.0, 1.0))
            : 0.0f;
        const float line_y = (rect_min.y + rect_max.y) * 0.5f;
        draw_list->AddLine(
            ImVec2(rect_min.x + rect_width * first_normalized, line_y),
            ImVec2(rect_min.x + rect_width * last_normalized, line_y),
            IM_COL32(0xb3, 0xc5, 0xff, 0x26));  // primary @ ~15%
    }

    // Playhead: tertiary-container red (#ff5450)
    if (duration_seconds > 0.0) {
        const float playhead_x = rect_min.x +
            static_cast<float>(state->timeline_time_seconds / duration_seconds) * rect_width;
        draw_list->AddLine(
            ImVec2(playhead_x, rect_min.y),
            ImVec2(playhead_x, rect_max.y),
            IM_COL32(0xff, 0x54, 0x50, 0xFF),
            1.0f);
    }

    // Keyframe diamonds: primary normally, tertiary-container at playhead,
    // secondary on unselected rows for hierarchy.
    const float marker_half = 4.0f;
    for (const double key_time : track.key_times) {
        const float normalized = duration_seconds > 0.0
            ? static_cast<float>(std::clamp(key_time / duration_seconds, 0.0, 1.0))
            : 0.0f;
        const float marker_x = rect_min.x + (rect_width * normalized);
        const bool near_playhead =
            std::abs(key_time - state->timeline_time_seconds) <= 1e-6;
        ImU32 fill_color;
        if (near_playhead) {
            fill_color = IM_COL32(0xff, 0x54, 0x50, 0xFF);   // tertiary-container
        } else if (selected) {
            fill_color = IM_COL32(0xb3, 0xc5, 0xff, 0xFF);   // primary
        } else {
            fill_color = IM_COL32(0xbe, 0xc7, 0xdc, 0xD0);   // secondary @ 82%
        }
        const ImU32 outline_color = IM_COL32(0x10, 0x13, 0x19, 0xFF);  // surface
        const float cx = marker_x;
        const float cy = (rect_min.y + rect_max.y) * 0.5f;
        draw_list->AddQuadFilled(
            ImVec2(cx,                cy - marker_half),
            ImVec2(cx + marker_half,  cy),
            ImVec2(cx,                cy + marker_half),
            ImVec2(cx - marker_half,  cy),
            fill_color);
        draw_list->AddQuad(
            ImVec2(cx,                cy - marker_half),
            ImVec2(cx + marker_half,  cy),
            ImVec2(cx,                cy + marker_half),
            ImVec2(cx - marker_half,  cy),
            outline_color,
            1.0f);
    }

    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const float local_x = std::clamp(ImGui::GetIO().MousePos.x - rect_min.x, 0.0f, rect_width);
        const double clicked_time = duration_seconds > 0.0
            ? (static_cast<double>(local_x) / static_cast<double>(rect_width)) * duration_seconds
            : 0.0;
        const double threshold_time =
            duration_seconds > 0.0
                ? (12.0 / static_cast<double>(rect_width)) * duration_seconds
                : 0.0;
        const std::optional<double> snapped_time =
            nearest_key_time(track, clicked_time, threshold_time);
        state->timeline_playing = false;
        focus_timeline_track(
            state,
            track,
            snapped_time.value_or(clicked_time),
            "Timeline",
            true);
    }

    if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "%s\nKeys: %zu",
            track.label.c_str(),
            track.key_times.size());
    }

    ImGui::PopID();
}

void copy_string_to_input_buffer(
    std::string_view source,
    std::array<char, 256>* buffer) {
    if (buffer == nullptr) {
        return;
    }

    buffer->fill('\0');
    const std::size_t copy_size = std::min(source.size(), buffer->size() - 1U);
    std::snprintf(buffer->data(), buffer->size(), "%.*s", static_cast<int>(copy_size), source.data());
}

void draw_draw_order_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || state->load_result.project == nullptr) {
        ImGui::TextUnformatted("Load a valid project to edit draw order keys.");
        return;
    }

    ImGui::TextUnformatted("Draw Order Key Editor");
    const auto runtime_edit = make_draw_order_timeline_edit(*state, track);
    if (!runtime_edit.has_value()) {
        ImGui::TextUnformatted("The selected draw-order track could not be resolved.");
        return;
    }

    const marrow::editor::DrawOrderTimelineEdit* existing_edit =
        state->load_result.project->find_draw_order_timeline_edit(track.animation_name);
    const marrow::editor::DrawOrderTimelineEdit display_edit =
        existing_edit != nullptr ? *existing_edit : *runtime_edit;
    const double duration_seconds = selected_animation_duration(*state);

    ImGui::Text("%s / Global / Draw Order", track.animation_name.c_str());
    ImGui::Text(
        "Source: %s",
        existing_edit != nullptr ? "project draw-order edit" : "runtime track");
    ImGui::TextUnformatted(
        "Each key lists the full slot stack. Move entries up or down to preview reordered presentation.");

    const auto track_group = [&]() {
        return std::string("timeline:") + track.id;
    };
    const auto key_group = [&](std::size_t key_index) {
        return track_group() + ":key:" + std::to_string(key_index);
    };
    const auto commit_project_change = [&](const marrow::editor::ProjectData& previous_project,
                                           std::string status_message,
                                           EditActionKind kind = EditActionKind::EditProperty,
                                           std::string group = {},
                                           bool allow_merge = true) {
        if (group.empty()) {
            group = track_group();
        }
        if (!apply_project_command_change(
                state,
                previous_project,
                kind,
                std::move(status_message),
                std::move(group),
                allow_merge,
                "Draw-order edit failed")) {
            return false;
        }

        state->selected_timeline_track_id = track.id;
        return true;
    };

    if (ImGui::Button("Add Key At Playhead##draw_order")) {
        const marrow::editor::ProjectData previous_project = *state->load_result.project;
        const auto edit_index = ensure_draw_order_timeline_edit_index(state, track);
        if (edit_index.has_value()) {
            auto& editable_track =
                state->load_result.project->draw_order_timeline_edits[*edit_index];
            if (const auto insert_time = insertable_key_time(
                    editable_track.keyframes,
                    state->timeline_time_seconds,
                    duration_seconds)) {
                marrow::editor::DrawOrderKeyframeEdit new_key =
                    sample_draw_order_keyframe(*state);
                new_key.time = *insert_time;
                const auto iterator = std::upper_bound(
                    editable_track.keyframes.begin(),
                    editable_track.keyframes.end(),
                    new_key.time,
                    [](double time, const marrow::editor::DrawOrderKeyframeEdit& keyframe) {
                        return time < keyframe.time;
                    });
                editable_track.keyframes.insert(iterator, std::move(new_key));
                commit_project_change(
                    previous_project,
                    "Added a draw-order key on " + track.animation_name,
                    EditActionKind::AddKeyframe,
                    track_group(),
                    false);
            } else {
                *state->load_result.project = previous_project;
                state->status_message =
                    "Could not place a new draw-order key between existing keyframes";
            }
        }
    }

    ImGui::BeginChild("draw_order_key_editor", ImVec2(0.0f, 280.0f), true);
    for (std::size_t key_index = 0; key_index < display_edit.keyframes.size(); ++key_index) {
        const auto& display_key = display_edit.keyframes[key_index];
        ImGui::PushID(static_cast<int>(key_index));
        const std::string header = "Key " + std::to_string(key_index + 1U) +
            " @ " + format_time_seconds(display_key.time);
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (display_edit.keyframes.size() > 1U &&
                ImGui::Button("Remove Key##draw_order")) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                if (const auto edit_index = ensure_draw_order_timeline_edit_index(state, track)) {
                    auto& editable_track =
                        state->load_result.project->draw_order_timeline_edits[*edit_index];
                    editable_track.keyframes.erase(
                        editable_track.keyframes.begin() + static_cast<std::ptrdiff_t>(key_index));
                    commit_project_change(
                        previous_project,
                        "Removed a draw-order key on " + track.animation_name,
                        EditActionKind::RemoveKeyframe,
                        track_group(),
                        false);
                }
            }

            double edited_time = display_key.time;
            const bool time_changed = ImGui::DragScalar(
                "Time",
                ImGuiDataType_Double,
                &edited_time,
                0.01f,
                nullptr,
                nullptr,
                "%.3f s");
            apply_timeline_project_drag(
                state,
                time_changed,
                EditActionKind::EditProperty,
                "Updated draw-order key timing on " + track.animation_name,
                key_group(key_index),
                false,
                "Draw-order edit failed",
                [&]() {
                    if (const auto edit_index =
                            ensure_draw_order_timeline_edit_index(state, track)) {
                        auto& editable_track =
                            state->load_result.project->draw_order_timeline_edits[*edit_index];
                        editable_track.keyframes[key_index].time = clamp_existing_key_time(
                            editable_track.keyframes,
                            key_index,
                            edited_time,
                            duration_seconds);
                    }
                });

            if (ImGui::Button("Use Current Preview Order")) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                if (const auto edit_index = ensure_draw_order_timeline_edit_index(state, track)) {
                    state->load_result.project->draw_order_timeline_edits[*edit_index]
                        .keyframes[key_index]
                        .slot_names = sample_draw_order_keyframe(*state).slot_names;
                    commit_project_change(
                        previous_project,
                        "Copied the preview slot order into " + track.animation_name);
                }
            }

            ImGui::Separator();
            for (std::size_t slot_order_index = 0;
                 slot_order_index < display_key.slot_names.size();
                 ++slot_order_index) {
                ImGui::PushID(static_cast<int>(slot_order_index));
                ImGui::Text(
                    "%zu. %s",
                    slot_order_index + 1U,
                    display_key.slot_names[slot_order_index].c_str());
                if (slot_order_index > 0U) {
                    ImGui::SameLine();
                    if (ImGui::Button("Up")) {
                        const marrow::editor::ProjectData previous_project =
                            *state->load_result.project;
                        if (const auto edit_index =
                                ensure_draw_order_timeline_edit_index(state, track)) {
                            auto& slot_names =
                                state->load_result.project->draw_order_timeline_edits[*edit_index]
                                    .keyframes[key_index]
                                    .slot_names;
                            std::swap(slot_names[slot_order_index], slot_names[slot_order_index - 1U]);
                            commit_project_change(
                                previous_project,
                                "Moved " + slot_names[slot_order_index - 1U] +
                                    " earlier in the draw order");
                        }
                    }
                }
                if (slot_order_index + 1U < display_key.slot_names.size()) {
                    ImGui::SameLine();
                    if (ImGui::Button("Down")) {
                        const marrow::editor::ProjectData previous_project =
                            *state->load_result.project;
                        if (const auto edit_index =
                                ensure_draw_order_timeline_edit_index(state, track)) {
                            auto& slot_names =
                                state->load_result.project->draw_order_timeline_edits[*edit_index]
                                    .keyframes[key_index]
                                    .slot_names;
                            std::swap(slot_names[slot_order_index], slot_names[slot_order_index + 1U]);
                            commit_project_change(
                                previous_project,
                                "Moved " + slot_names[slot_order_index + 1U] +
                                    " later in the draw order");
                        }
                    }
                }
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void draw_event_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || state->load_result.project == nullptr) {
        ImGui::TextUnformatted("Load a valid project to edit event keys.");
        return;
    }
    if (state->load_result.skeleton_data->events().empty()) {
        ImGui::TextUnformatted("The loaded skeleton does not define any event names.");
        return;
    }

    ImGui::TextUnformatted("Event Key Editor");
    const auto runtime_edit = make_event_timeline_edit(*state, track);
    if (!runtime_edit.has_value()) {
        ImGui::TextUnformatted("The selected event track could not be resolved.");
        return;
    }

    const marrow::editor::EventTimelineEdit* existing_edit =
        state->load_result.project->find_event_timeline_edit(track.animation_name);
    const marrow::editor::EventTimelineEdit display_edit =
        existing_edit != nullptr ? *existing_edit : *runtime_edit;

    ImGui::Text("%s / Global / Events", track.animation_name.c_str());
    ImGui::Text(
        "Source: %s",
        existing_edit != nullptr ? "project event edit" : "runtime track");
    ImGui::TextUnformatted(
        "Preview playback emits the resolved event payloads up to the current playhead.");

    if (state->preview_events.empty()) {
        ImGui::TextDisabled("Triggered at preview time: none");
    } else {
        ImGui::TextUnformatted("Triggered at preview time:");
        for (const auto& event : state->preview_events) {
            ImGui::BulletText(
                "%s @ %.3fs  int=%d  float=%.2f  string=%s",
                event.name.c_str(),
                event.time,
                event.int_value,
                event.float_value,
                event.string_value.c_str());
        }
    }

    const auto event_track_group = [&]() {
        return std::string("timeline:") + track.id;
    };
    const auto event_key_group = [&](std::size_t key_index) {
        return event_track_group() + ":key:" + std::to_string(key_index);
    };
    const auto commit_project_change = [&](const marrow::editor::ProjectData& previous_project,
                                           std::string status_message,
                                           EditActionKind kind = EditActionKind::EditProperty,
                                           std::string group = {},
                                           bool allow_merge = true) {
        if (group.empty()) {
            group = event_track_group();
        }
        if (!apply_project_command_change(
                state,
                previous_project,
                kind,
                std::move(status_message),
                std::move(group),
                allow_merge,
                "Event edit failed")) {
            return false;
        }

        state->selected_timeline_track_id = track.id;
        return true;
    };

    if (ImGui::Button("Add Key At Playhead##events")) {
        const marrow::editor::ProjectData previous_project = *state->load_result.project;
        if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
            auto& editable_track =
                state->load_result.project->event_timeline_edits[*edit_index];
            marrow::editor::EventKeyframeEdit new_key = sample_event_keyframe(*state);
            const auto iterator = std::upper_bound(
                editable_track.keyframes.begin(),
                editable_track.keyframes.end(),
                new_key.time,
                [](double time, const marrow::editor::EventKeyframeEdit& keyframe) {
                    return time < keyframe.time;
                });
            editable_track.keyframes.insert(iterator, std::move(new_key));
            commit_project_change(
                previous_project,
                "Added an event key on " + track.animation_name,
                EditActionKind::AddKeyframe,
                event_track_group(),
                false);
        }
    }

    ImGui::BeginChild("event_key_editor", ImVec2(0.0f, 340.0f), true);
    for (std::size_t key_index = 0; key_index < display_edit.keyframes.size(); ++key_index) {
        const auto& display_key = display_edit.keyframes[key_index];
        ImGui::PushID(static_cast<int>(key_index));
        const std::string header = "Key " + std::to_string(key_index + 1U) +
            " @ " + format_time_seconds(display_key.time) + " / " + display_key.event_name;
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (display_edit.keyframes.size() > 1U &&
                ImGui::Button("Remove Key##events")) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                    auto& editable_track =
                        state->load_result.project->event_timeline_edits[*edit_index];
                    editable_track.keyframes.erase(
                        editable_track.keyframes.begin() + static_cast<std::ptrdiff_t>(key_index));
                    commit_project_change(
                        previous_project,
                        "Removed an event key on " + track.animation_name,
                        EditActionKind::RemoveKeyframe,
                        event_track_group(),
                        false);
                }
            }

            double edited_time = display_key.time;
            const bool time_changed = ImGui::DragScalar(
                "Time",
                ImGuiDataType_Double,
                &edited_time,
                0.01f,
                nullptr,
                nullptr,
                "%.3f s");
            apply_timeline_project_drag(
                state,
                time_changed,
                EditActionKind::EditProperty,
                "Updated event key timing on " + track.animation_name,
                event_key_group(key_index),
                false,
                "Event edit failed",
                [&]() {
                    if (const auto edit_index =
                            ensure_event_timeline_edit_index(state, track)) {
                        auto& editable_track =
                            state->load_result.project->event_timeline_edits[*edit_index];
                        editable_track.keyframes[key_index].time =
                            clamp_existing_non_decreasing_key_time(
                                editable_track.keyframes,
                                key_index,
                                edited_time);
                    }
                });

            if (ImGui::BeginCombo("Event", display_key.event_name.c_str())) {
                for (const auto& definition : state->load_result.skeleton_data->events()) {
                    const bool selected = display_key.event_name == definition.name;
                    if (ImGui::Selectable(definition.name.c_str(), selected)) {
                        const marrow::editor::ProjectData previous_project =
                            *state->load_result.project;
                        if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                            state->load_result.project->event_timeline_edits[*edit_index]
                                .keyframes[key_index]
                                .event_name = definition.name;
                            commit_project_change(
                                previous_project,
                                "Updated event key name on " + track.animation_name);
                        }
                    }
                    if (selected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            const auto event_definition = std::find_if(
                state->load_result.skeleton_data->events().begin(),
                state->load_result.skeleton_data->events().end(),
                [&](const marrow::runtime::EventDefinition& definition) {
                    return definition.name == display_key.event_name;
                });
            if (event_definition != state->load_result.skeleton_data->events().end()) {
                ImGui::TextDisabled(
                    "Defaults: int=%d float=%.2f string=%s",
                    event_definition->int_value,
                    event_definition->float_value,
                    event_definition->string_value.c_str());
            }

            auto update_optional_number = [&](const char* toggle_label,
                                              const char* value_label,
                                              std::optional<double> value,
                                              double default_value,
                                              auto setter,
                                              std::string toggle_status,
                                              std::string value_status) {
                bool enabled = value.has_value();
                if (ImGui::Checkbox(toggle_label, &enabled)) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                        setter(
                            &state->load_result.project->event_timeline_edits[*edit_index]
                                 .keyframes[key_index],
                            enabled ? std::optional<double>(default_value) : std::nullopt);
                        commit_project_change(
                            previous_project,
                            std::move(toggle_status),
                            EditActionKind::EditProperty,
                            event_key_group(key_index),
                            true);
                    }
                }
                if (!enabled) {
                    return;
                }

                double edited_value = value.value_or(default_value);
                const bool value_changed = ImGui::DragScalar(
                    value_label,
                    ImGuiDataType_Double,
                    &edited_value,
                    0.05f,
                    nullptr,
                    nullptr,
                    "%.3f");
                apply_timeline_project_drag(
                    state,
                    value_changed,
                    EditActionKind::EditProperty,
                    std::move(value_status),
                    event_key_group(key_index),
                    false,
                    "Event edit failed",
                    [&]() {
                        if (const auto edit_index =
                                ensure_event_timeline_edit_index(state, track)) {
                            setter(
                                &state->load_result.project->event_timeline_edits[*edit_index]
                                     .keyframes[key_index],
                                std::optional<double>(edited_value));
                        }
                    });
            };

            bool override_int = display_key.int_value.has_value();
            if (ImGui::Checkbox("Override Int", &override_int)) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                    auto& key =
                        state->load_result.project->event_timeline_edits[*edit_index]
                            .keyframes[key_index];
                    key.int_value = override_int
                        ? std::optional<int>(
                              event_definition != state->load_result.skeleton_data->events().end()
                                  ? event_definition->int_value
                                  : 0)
                        : std::nullopt;
                    commit_project_change(previous_project, "Updated event int override");
                }
            }
            if (override_int) {
                int edited_value = display_key.int_value.value_or(
                    event_definition != state->load_result.skeleton_data->events().end()
                        ? event_definition->int_value
                        : 0);
                if (ImGui::InputInt("Int", &edited_value)) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                        state->load_result.project->event_timeline_edits[*edit_index]
                            .keyframes[key_index]
                            .int_value = edited_value;
                        commit_project_change(previous_project, "Updated event int value");
                    }
                }
            }

            update_optional_number(
                "Override Float",
                "Float",
                display_key.float_value,
                event_definition != state->load_result.skeleton_data->events().end()
                    ? event_definition->float_value
                    : 0.0,
                [](marrow::editor::EventKeyframeEdit* key, std::optional<double> value) {
                    key->float_value = value;
                },
                "Updated event float override",
                "Updated event float value");
            update_optional_number(
                "Override Volume",
                "Volume",
                display_key.volume,
                event_definition != state->load_result.skeleton_data->events().end()
                    ? event_definition->volume
                    : 1.0,
                [](marrow::editor::EventKeyframeEdit* key, std::optional<double> value) {
                    key->volume = value;
                },
                "Updated event volume override",
                "Updated event volume value");
            update_optional_number(
                "Override Balance",
                "Balance",
                display_key.balance,
                event_definition != state->load_result.skeleton_data->events().end()
                    ? event_definition->balance
                    : 0.0,
                [](marrow::editor::EventKeyframeEdit* key, std::optional<double> value) {
                    key->balance = value;
                },
                "Updated event balance override",
                "Updated event balance value");

            const auto update_optional_text = [&](const char* toggle_label,
                                                  const char* value_label,
                                                  const std::optional<std::string>& value,
                                                  std::string_view default_value,
                                                  auto setter,
                                                  std::string status) {
                bool enabled = value.has_value();
                if (ImGui::Checkbox(toggle_label, &enabled)) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                        setter(
                            &state->load_result.project->event_timeline_edits[*edit_index]
                                 .keyframes[key_index],
                            enabled ? std::optional<std::string>(default_value) : std::nullopt);
                        commit_project_change(previous_project, status);
                    }
                }
                if (!enabled) {
                    return;
                }

                std::array<char, 256> buffer{};
                copy_string_to_input_buffer(value.value_or(std::string(default_value)), &buffer);
                if (ImGui::InputText(value_label, buffer.data(), buffer.size())) {
                    const marrow::editor::ProjectData previous_project = *state->load_result.project;
                    if (const auto edit_index = ensure_event_timeline_edit_index(state, track)) {
                        setter(
                            &state->load_result.project->event_timeline_edits[*edit_index]
                                 .keyframes[key_index],
                            std::optional<std::string>(buffer.data()));
                        commit_project_change(previous_project, status);
                    }
                }
            };

            update_optional_text(
                "Override String",
                "String",
                display_key.string_value,
                event_definition != state->load_result.skeleton_data->events().end()
                    ? std::string_view(event_definition->string_value)
                    : std::string_view{},
                [](marrow::editor::EventKeyframeEdit* key,
                   std::optional<std::string> value) {
                    key->string_value = std::move(value);
                },
                "Updated event string override");
            update_optional_text(
                "Override Audio",
                "Audio",
                display_key.audio_path,
                event_definition != state->load_result.skeleton_data->events().end() &&
                        event_definition->audio_path.has_value()
                    ? std::string_view(*event_definition->audio_path)
                    : std::string_view{},
                [](marrow::editor::EventKeyframeEdit* key,
                   std::optional<std::string> value) {
                    key->audio_path = std::move(value);
                },
                "Updated event audio override");
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void draw_transform_timeline_editor(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks) {
    ImGui::Separator();

    if (!state->load_result || state->load_result.project == nullptr) {
        ImGui::TextUnformatted("Load a valid project to edit transform keys.");
        return;
    }

    const TimelineTrackRow* track = selected_timeline_track(*state, tracks);
    if (track == nullptr) {
        ImGui::TextUnformatted("Select a transform track row to author keyed motion.");
        return;
    }
    if (track->id == "global:draw-order") {
        draw_draw_order_timeline_editor(state, *track);
        return;
    }
    if (track->id == "global:events") {
        draw_event_timeline_editor(state, *track);
        return;
    }
    if (track->deform_attachment_name.has_value()) {
        draw_mesh_deform_timeline_editor(state, *track);
        return;
    }

    ImGui::TextUnformatted("Transform Key Editor");
    if (!track->transform_channel.has_value() || !track->bone_index.has_value() ||
        *track->bone_index >= state->load_result.skeleton_data->bones().size()) {
        ImGui::TextUnformatted(
            "The selected timeline row is read-only. Keyframe editing is available for rotate, translate, scale, and shear tracks.");
        return;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    const std::string& bone_name = skeleton.bones()[*track->bone_index].name;
    const auto runtime_edit = make_transform_timeline_edit(*state, *track);
    if (!runtime_edit.has_value()) {
        ImGui::TextUnformatted("The selected transform track could not be resolved.");
        return;
    }

    const marrow::editor::TransformTimelineEdit* existing_edit =
        state->load_result.project->find_transform_timeline_edit(
            track->animation_name,
            bone_name,
            *track->transform_channel);
    const marrow::editor::TransformTimelineEdit display_edit =
        existing_edit != nullptr ? *existing_edit : *runtime_edit;
    const double duration_seconds = selected_animation_duration(*state);

    ImGui::Text(
        "%s / %s / %s",
        track->animation_name.c_str(),
        bone_name.c_str(),
        std::string(transform_channel_label(*track->transform_channel)).c_str());
    ImGui::Text(
        "Source: %s",
        existing_edit != nullptr ? "project timeline edit" : "runtime track");
    ImGui::TextUnformatted(
        "Edits are stored in the .marrow project file and exported back into a .mskl skeleton.");

    const auto track_group = [&]() {
        return std::string("timeline:") + track->id;
    };
    const auto key_group = [&](std::size_t key_index) {
        return track_group() + ":key:" + std::to_string(key_index);
    };
    const auto commit_project_change = [&](const marrow::editor::ProjectData& previous_project,
                                           EditActionKind kind,
                                           std::string group,
                                           bool allow_merge,
                                           std::string status_message) {
        if (!apply_project_command_change(
                state,
                previous_project,
                kind,
                std::move(status_message),
                std::move(group),
                allow_merge,
                "Timeline edit failed")) {
            return false;
        }

        state->selected_timeline_track_id = track->id;
        return true;
    };

    if (ImGui::Button("Add Key At Playhead")) {
        const auto sampled = sample_transform_keyframe(*state, *track);
        namespace json = marrow::runtime::json;
        json::Value::Object cmd_obj;
        cmd_obj.emplace("op", json::Value("set_transform", {}));
        json::Value::Object args_obj;
        args_obj.emplace("animation", json::Value(track->animation_name, {}));
        args_obj.emplace("bone", json::Value(bone_name, {}));
        
        const char* channel_name = "rotate";
        switch (*track->transform_channel) {
            case marrow::editor::TransformTimelineChannel::Rotate: channel_name = "rotate"; break;
            case marrow::editor::TransformTimelineChannel::Translate: channel_name = "translate"; break;
            case marrow::editor::TransformTimelineChannel::Scale: channel_name = "scale"; break;
            case marrow::editor::TransformTimelineChannel::Shear: channel_name = "shear"; break;
        }
        args_obj.emplace("channel", json::Value(channel_name, {}));
        args_obj.emplace("time", json::Value(state->timeline_time_seconds, {}));
        if (*track->transform_channel == marrow::editor::TransformTimelineChannel::Rotate) {
            args_obj.emplace("angle", json::Value(sampled.angle, {}));
        } else {
            args_obj.emplace("x", json::Value(sampled.x, {}));
            args_obj.emplace("y", json::Value(sampled.y, {}));
        }
        cmd_obj.emplace("args", json::Value(std::move(args_obj), {}));
        dispatch_agent_command(state, json::Value(std::move(cmd_obj), {}));
    }

    ImGui::BeginChild("transform_key_editor", ImVec2(0.0f, 250.0f), true);
    for (std::size_t key_index = 0; key_index < display_edit.keyframes.size(); ++key_index) {
        const auto& display_key = display_edit.keyframes[key_index];
        ImGui::PushID(static_cast<int>(key_index));
        const std::string header = "Key " + std::to_string(key_index + 1U) +
            " @ " + format_time_seconds(display_key.time);
        if (ImGui::CollapsingHeader(
                header.c_str(),
                ImGuiTreeNodeFlags_DefaultOpen)) {
            if (display_edit.keyframes.size() > 1U &&
                ImGui::Button("Remove Key##transform")) {
                namespace json = marrow::runtime::json;
                json::Value::Object cmd_obj;
                cmd_obj.emplace("op", json::Value("remove_transform_keyframe", {}));
                json::Value::Object args_obj;
                args_obj.emplace("animation", json::Value(track->animation_name, {}));
                args_obj.emplace("bone", json::Value(bone_name, {}));
                
                const char* channel_name = "rotate";
                switch (*track->transform_channel) {
                    case marrow::editor::TransformTimelineChannel::Rotate: channel_name = "rotate"; break;
                    case marrow::editor::TransformTimelineChannel::Translate: channel_name = "translate"; break;
                    case marrow::editor::TransformTimelineChannel::Scale: channel_name = "scale"; break;
                    case marrow::editor::TransformTimelineChannel::Shear: channel_name = "shear"; break;
                }
                args_obj.emplace("channel", json::Value(channel_name, {}));
                args_obj.emplace("time", json::Value(display_key.time, {}));
                cmd_obj.emplace("args", json::Value(std::move(args_obj), {}));
                dispatch_agent_command(state, json::Value(std::move(cmd_obj), {}));
            }

            double edited_time = display_key.time;
            const bool time_changed = ImGui::DragScalar(
                "Time",
                ImGuiDataType_Double,
                &edited_time,
                0.01f,
                nullptr,
                nullptr,
                "%.3f s");
            apply_timeline_project_drag(
                state,
                time_changed,
                EditActionKind::EditProperty,
                "Updated key timing on " + bone_name + " " +
                    std::string(transform_channel_label(*track->transform_channel)),
                key_group(key_index),
                false,
                "Timeline edit failed",
                [&]() {
                    const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                    if (edit_index.has_value()) {
                        auto& editable_track =
                            state->load_result.project->transform_timeline_edits[*edit_index];
                        editable_track.keyframes[key_index].time = clamp_existing_key_time(
                            editable_track.keyframes,
                            key_index,
                            edited_time,
                            duration_seconds);
                    }
                });

            if (*track->transform_channel == marrow::editor::TransformTimelineChannel::Rotate) {
                double edited_angle = display_key.angle;
                const bool angle_changed = ImGui::DragScalar(
                    "Angle",
                    ImGuiDataType_Double,
                    &edited_angle,
                    0.1f,
                    nullptr,
                    nullptr,
                    "%.3f deg");
                apply_timeline_project_drag(
                    state,
                    angle_changed,
                    EditActionKind::MoveBone,
                    "Updated key angle on " + bone_name,
                    key_group(key_index),
                    false,
                    "Timeline edit failed",
                    [&]() {
                        const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                        if (edit_index.has_value()) {
                            state->load_result.project->transform_timeline_edits[*edit_index]
                                .keyframes[key_index]
                                .angle = edited_angle;
                        }
                    });
            } else {
                double edited_x = display_key.x;
                const bool x_changed = ImGui::DragScalar(
                    "X",
                    ImGuiDataType_Double,
                    &edited_x,
                    0.1f,
                    nullptr,
                    nullptr,
                    "%.3f");
                apply_timeline_project_drag(
                    state,
                    x_changed,
                    EditActionKind::MoveBone,
                    "Updated key X on " + bone_name,
                    key_group(key_index),
                    false,
                    "Timeline edit failed",
                    [&]() {
                        const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                        if (edit_index.has_value()) {
                            state->load_result.project->transform_timeline_edits[*edit_index]
                                .keyframes[key_index]
                                .x = edited_x;
                        }
                    });

                double edited_y = display_key.y;
                const bool y_changed = ImGui::DragScalar(
                    "Y",
                    ImGuiDataType_Double,
                    &edited_y,
                    0.1f,
                    nullptr,
                    nullptr,
                    "%.3f");
                apply_timeline_project_drag(
                    state,
                    y_changed,
                    EditActionKind::MoveBone,
                    "Updated key Y on " + bone_name,
                    key_group(key_index),
                    false,
                    "Timeline edit failed",
                    [&]() {
                        const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                        if (edit_index.has_value()) {
                            state->load_result.project->transform_timeline_edits[*edit_index]
                                .keyframes[key_index]
                                .y = edited_y;
                        }
                    });
            }

            int interpolation_kind = 0;
            switch (display_key.interpolation.kind()) {
            case marrow::runtime::InterpolationKind::Linear:
                interpolation_kind = 0;
                break;
            case marrow::runtime::InterpolationKind::Stepped:
                interpolation_kind = 1;
                break;
            case marrow::runtime::InterpolationKind::CubicBezier:
                interpolation_kind = 2;
                break;
            }
            constexpr const char* kInterpolationLabels[] = {
                "Linear",
                "Stepped",
                "Bezier",
            };
            if (ImGui::Combo(
                    "Interpolation",
                    &interpolation_kind,
                    kInterpolationLabels,
                    IM_ARRAYSIZE(kInterpolationLabels))) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                const auto edit_index = ensure_transform_timeline_edit_index(state, *track);
                if (edit_index.has_value()) {
                    auto& editable_key =
                        state->load_result.project->transform_timeline_edits[*edit_index]
                            .keyframes[key_index];
                    switch (interpolation_kind) {
                    case 0:
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::linear();
                        break;
                    case 1:
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::stepped();
                        break;
                    case 2: {
                        marrow::runtime::CubicBezierControlPoints bezier{
                            0.25,
                            0.1,
                            0.75,
                            0.9,
                        };
                        if (display_key.interpolation.kind() ==
                            marrow::runtime::InterpolationKind::CubicBezier) {
                            bezier = display_key.interpolation.cubic_bezier();
                        }
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::cubic_bezier(
                                static_cast<double>(bezier.cx1),
                                static_cast<double>(bezier.cy1),
                                static_cast<double>(bezier.cx2),
                                static_cast<double>(bezier.cy2));
                        break;
                    }
                    }
                    commit_project_change(
                        previous_project,
                        EditActionKind::EditProperty,
                        key_group(key_index),
                        true,
                        "Updated interpolation on " + bone_name + " " +
                            std::string(transform_channel_label(*track->transform_channel)));
                }
            }

            if (display_key.interpolation.kind() ==
                marrow::runtime::InterpolationKind::CubicBezier) {
                marrow::runtime::CubicBezierControlPoints bezier =
                    display_key.interpolation.cubic_bezier();

                const auto update_bezier = [&](const char* label,
                                               marrow::runtime::AnimationScalar* component,
                                               bool clamp_x,
                                               std::string status) {
                    const bool changed = ImGui::DragScalar(
                        label,
                        ImGuiDataType_Float,
                        component,
                        0.01f,
                        nullptr,
                        nullptr,
                        "%.3f");
                    if (clamp_x) {
                        *component = std::clamp(*component, 0.0f, 1.0f);
                    }
                    apply_timeline_project_drag(
                        state,
                        changed,
                        EditActionKind::EditProperty,
                        std::move(status),
                        key_group(key_index),
                        false,
                        "Timeline edit failed",
                        [&]() {
                            const auto edit_index =
                                ensure_transform_timeline_edit_index(state, *track);
                            if (edit_index.has_value()) {
                                auto& editable_key =
                                    state->load_result.project
                                        ->transform_timeline_edits[*edit_index]
                                        .keyframes[key_index];
                                editable_key.interpolation =
                                    marrow::runtime::Interpolation::cubic_bezier(
                                        static_cast<double>(bezier.cx1),
                                        static_cast<double>(bezier.cy1),
                                        static_cast<double>(bezier.cx2),
                                        static_cast<double>(bezier.cy2));
                            }
                        });
                };
                update_bezier("Bezier X1", &bezier.cx1, true, "Updated bezier control point X1");
                update_bezier("Bezier Y1", &bezier.cy1, false, "Updated bezier control point Y1");
                update_bezier("Bezier X2", &bezier.cx2, true, "Updated bezier control point X2");
                update_bezier("Bezier Y2", &bezier.cy2, false, "Updated bezier control point Y2");
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void draw_mesh_deform_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || state->load_result.project == nullptr ||
        !track.slot_index.has_value() || !track.deform_attachment_name.has_value() ||
        *track.slot_index >= state->load_result.skeleton_data->slots().size()) {
        ImGui::TextUnformatted("The selected deform track could not be resolved.");
        return;
    }

    ImGui::TextUnformatted("Mesh Deform Key Editor");

    const auto& skeleton = *state->load_result.skeleton_data;
    const std::string& slot_name = skeleton.slots()[*track.slot_index].name;
    const auto runtime_edit = make_mesh_deform_timeline_edit(*state, track);
    if (!runtime_edit.has_value()) {
        ImGui::TextUnformatted("The selected deform track could not be resolved.");
        return;
    }

    const auto* attachment = skeleton.find_attachment_source(
        *track.slot_index, *track.deform_attachment_name);
    if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
        ImGui::TextUnformatted("The selected deform track no longer resolves to a mesh attachment.");
        return;
    }

    const marrow::editor::MeshDeformTimelineEdit* existing_edit =
        state->load_result.project->find_mesh_deform_timeline_edit(
            track.animation_name,
            slot_name,
            *track.deform_attachment_name);
    const marrow::editor::MeshDeformTimelineEdit display_edit =
        existing_edit != nullptr ? *existing_edit : *runtime_edit;
    const double duration_seconds = selected_animation_duration(*state);
    const std::size_t vertex_count = attachment->mesh_geometry->vertices.size() / 2U;

    ImGui::Text(
        "%s / %s / %s",
        track.animation_name.c_str(),
        slot_name.c_str(),
        track.deform_attachment_name->c_str());
    ImGui::Text(
        "Source: %s",
        existing_edit != nullptr ? "project deform edit" : "runtime deform track");
    ImGui::Text(
        "Vertices: %zu  Components per key: %zu",
        vertex_count,
        attachment->mesh_geometry->vertices.size());
    ImGui::TextUnformatted(
        "Offsets are authored per vertex in local mesh space and exported back into the .mskl deform timeline.");

    const auto deform_track_group = [&]() {
        return std::string("timeline:") + track.id;
    };
    const auto deform_key_group = [&](std::size_t key_index) {
        return deform_track_group() + ":key:" + std::to_string(key_index);
    };
    const auto commit_project_change = [&](const marrow::editor::ProjectData& previous_project,
                                           std::string status_message,
                                           EditActionKind kind = EditActionKind::EditProperty,
                                           std::string group = {},
                                           bool allow_merge = true) {
        if (group.empty()) {
            group = deform_track_group();
        }
        if (!apply_project_command_change(
                state,
                previous_project,
                kind,
                std::move(status_message),
                std::move(group),
                allow_merge,
                "Mesh deform edit failed")) {
            return false;
        }

        state->selected_timeline_track_id = track.id;
        return true;
    };

    if (ImGui::Button("Add Key At Playhead")) {
        const marrow::editor::ProjectData previous_project = *state->load_result.project;
        const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
        if (edit_index.has_value()) {
            auto& editable_track =
                state->load_result.project->mesh_deform_timeline_edits[*edit_index];
            if (const auto insert_time = insertable_key_time(
                    editable_track.keyframes,
                    state->timeline_time_seconds,
                    duration_seconds)) {
                marrow::editor::DeformKeyframeEdit new_key =
                    sample_deform_keyframe(*state, track);
                new_key.time = *insert_time;
                const auto iterator = std::upper_bound(
                    editable_track.keyframes.begin(),
                    editable_track.keyframes.end(),
                    new_key.time,
                    [](double time, const marrow::editor::DeformKeyframeEdit& keyframe) {
                        return time < keyframe.time;
                    });
                editable_track.keyframes.insert(iterator, std::move(new_key));
                commit_project_change(
                    previous_project,
                    "Added a mesh deform key on " + slot_name + " / " +
                        *track.deform_attachment_name,
                    EditActionKind::AddKeyframe,
                    deform_track_group(),
                    false);
            } else {
                *state->load_result.project = previous_project;
                state->status_message = "Could not place a new deform key between existing keyframes";
            }
        }
    }

    ImGui::BeginChild("mesh_deform_key_editor", ImVec2(0.0f, 300.0f), true);
    for (std::size_t key_index = 0; key_index < display_edit.keyframes.size(); ++key_index) {
        const auto& display_key = display_edit.keyframes[key_index];
        ImGui::PushID(static_cast<int>(key_index));
        const std::string header = "Key " + std::to_string(key_index + 1U) +
            " @ " + format_time_seconds(display_key.time);
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            if (display_edit.keyframes.size() > 1U &&
                ImGui::Button("Remove Key##deform")) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                if (const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track)) {
                    auto& editable_track =
                        state->load_result.project->mesh_deform_timeline_edits[*edit_index];
                    editable_track.keyframes.erase(
                        editable_track.keyframes.begin() + static_cast<std::ptrdiff_t>(key_index));
                    commit_project_change(
                        previous_project,
                        "Removed a mesh deform key on " + slot_name + " / " +
                            *track.deform_attachment_name,
                        EditActionKind::RemoveKeyframe,
                        deform_track_group(),
                        false);
                }
            }

            double edited_time = display_key.time;
            const bool time_changed = ImGui::DragScalar(
                "Time",
                ImGuiDataType_Double,
                &edited_time,
                0.01f,
                nullptr,
                nullptr,
                "%.3f s");
            apply_timeline_project_drag(
                state,
                time_changed,
                EditActionKind::EditProperty,
                "Updated deform key timing on " + slot_name,
                deform_key_group(key_index),
                false,
                "Mesh deform edit failed",
                [&]() {
                    const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
                    if (edit_index.has_value()) {
                        auto& editable_track =
                            state->load_result.project->mesh_deform_timeline_edits[*edit_index];
                        editable_track.keyframes[key_index].time = clamp_existing_key_time(
                            editable_track.keyframes,
                            key_index,
                            edited_time,
                            duration_seconds);
                    }
                });

            int interpolation_kind = 0;
            switch (display_key.interpolation.kind()) {
            case marrow::runtime::InterpolationKind::Linear:
                interpolation_kind = 0;
                break;
            case marrow::runtime::InterpolationKind::Stepped:
                interpolation_kind = 1;
                break;
            case marrow::runtime::InterpolationKind::CubicBezier:
                interpolation_kind = 2;
                break;
            }
            constexpr const char* kInterpolationLabels[] = {
                "Linear",
                "Stepped",
                "Bezier",
            };
            if (ImGui::Combo(
                    "Interpolation",
                    &interpolation_kind,
                    kInterpolationLabels,
                    IM_ARRAYSIZE(kInterpolationLabels))) {
                const marrow::editor::ProjectData previous_project = *state->load_result.project;
                const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
                if (edit_index.has_value()) {
                    auto& editable_key =
                        state->load_result.project->mesh_deform_timeline_edits[*edit_index]
                            .keyframes[key_index];
                    switch (interpolation_kind) {
                    case 0:
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::linear();
                        break;
                    case 1:
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::stepped();
                        break;
                    case 2: {
                        marrow::runtime::CubicBezierControlPoints bezier{
                            0.25,
                            0.1,
                            0.75,
                            0.9,
                        };
                        if (display_key.interpolation.kind() ==
                            marrow::runtime::InterpolationKind::CubicBezier) {
                            bezier = display_key.interpolation.cubic_bezier();
                        }
                        editable_key.interpolation =
                            marrow::runtime::Interpolation::cubic_bezier(
                                static_cast<double>(bezier.cx1),
                                static_cast<double>(bezier.cy1),
                                static_cast<double>(bezier.cx2),
                                static_cast<double>(bezier.cy2));
                        break;
                    }
                    }
                    commit_project_change(
                        previous_project,
                        "Updated deform interpolation on " + slot_name);
                }
            }

            if (display_key.interpolation.kind() ==
                marrow::runtime::InterpolationKind::CubicBezier) {
                marrow::runtime::CubicBezierControlPoints bezier =
                    display_key.interpolation.cubic_bezier();

                const auto update_bezier = [&](const char* label,
                                               marrow::runtime::AnimationScalar* component,
                                               bool clamp_x,
                                               std::string status) {
                    const bool changed = ImGui::DragScalar(
                        label,
                        ImGuiDataType_Float,
                        component,
                        0.01f,
                        nullptr,
                        nullptr,
                        "%.3f");
                    if (clamp_x) {
                        *component = std::clamp(*component, 0.0f, 1.0f);
                    }
                    apply_timeline_project_drag(
                        state,
                        changed,
                        EditActionKind::EditProperty,
                        std::move(status),
                        deform_key_group(key_index),
                        false,
                        "Mesh deform edit failed",
                        [&]() {
                            const auto edit_index =
                                ensure_mesh_deform_timeline_edit_index(state, track);
                            if (edit_index.has_value()) {
                                auto& editable_key =
                                    state->load_result.project
                                        ->mesh_deform_timeline_edits[*edit_index]
                                        .keyframes[key_index];
                                editable_key.interpolation =
                                    marrow::runtime::Interpolation::cubic_bezier(
                                        static_cast<double>(bezier.cx1),
                                        static_cast<double>(bezier.cy1),
                                        static_cast<double>(bezier.cx2),
                                        static_cast<double>(bezier.cy2));
                            }
                        });
                };
                update_bezier("Bezier X1", &bezier.cx1, true, "Updated deform bezier control point X1");
                update_bezier("Bezier Y1", &bezier.cy1, false, "Updated deform bezier control point Y1");
                update_bezier("Bezier X2", &bezier.cx2, true, "Updated deform bezier control point X2");
                update_bezier("Bezier Y2", &bezier.cy2, false, "Updated deform bezier control point Y2");
            }

            ImGui::Separator();
            ImGui::TextUnformatted("Vertex Offsets");
            for (std::size_t vertex_index = 0; vertex_index < display_key.vertex_offsets.size() / 2U;
                 ++vertex_index) {
                ImGui::PushID(static_cast<int>(vertex_index));
                const std::size_t x_index = vertex_index * 2U;
                const std::size_t y_index = x_index + 1U;

                double edited_x = display_key.vertex_offsets[x_index];
                const bool x_changed = ImGui::DragScalar(
                    "X",
                    ImGuiDataType_Double,
                    &edited_x,
                    0.25f,
                    nullptr,
                    nullptr,
                    "%.3f");
                apply_timeline_project_drag(
                    state,
                    x_changed,
                    EditActionKind::EditProperty,
                    "Updated deform vertex X on " + slot_name + " / " +
                        *track.deform_attachment_name,
                    deform_key_group(key_index) + ":vertex:" + std::to_string(vertex_index),
                    false,
                    "Mesh deform edit failed",
                    [&]() {
                        const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
                        if (edit_index.has_value()) {
                            state->load_result.project->mesh_deform_timeline_edits[*edit_index]
                                .keyframes[key_index]
                                .vertex_offsets[x_index] = edited_x;
                        }
                    });

                ImGui::SameLine();
                double edited_y = display_key.vertex_offsets[y_index];
                const bool y_changed = ImGui::DragScalar(
                    "Y",
                    ImGuiDataType_Double,
                    &edited_y,
                    0.25f,
                    nullptr,
                    nullptr,
                    "%.3f");
                apply_timeline_project_drag(
                    state,
                    y_changed,
                    EditActionKind::EditProperty,
                    "Updated deform vertex Y on " + slot_name + " / " +
                        *track.deform_attachment_name,
                    deform_key_group(key_index) + ":vertex:" + std::to_string(vertex_index),
                    false,
                    "Mesh deform edit failed",
                    [&]() {
                        const auto edit_index = ensure_mesh_deform_timeline_edit_index(state, track);
                        if (edit_index.has_value()) {
                            state->load_result.project->mesh_deform_timeline_edits[*edit_index]
                                .keyframes[key_index]
                                .vertex_offsets[y_index] = edited_y;
                        }
                    });

                ImGui::SameLine();
                ImGui::TextDisabled(
                    "V%zu  base(%.1f, %.1f)",
                    vertex_index,
                    attachment->mesh_geometry->vertices[x_index],
                    attachment->mesh_geometry->vertices[y_index]);
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void draw_timeline_window(ShellState* state) {
    ImGui::Begin(kTimelineWindowTitle);
    widgets::panel_head(state->icons, Icon::NodeAnim, "Timeline");

    if (!state->load_result || !state->preview_skeleton) {
        ImGui::TextUnformatted("Load a valid project to scrub and inspect keyed animation tracks.");
        ImGui::End();
        return;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    if (skeleton.animations().empty()) {
        ImGui::TextUnformatted("The loaded skeleton does not define any animations.");
        ImGui::End();
        return;
    }

    const std::string combo_label =
        state->selected_animation_name.empty() ? skeleton.animations().front().name
                                               : state->selected_animation_name;
    if (ImGui::BeginCombo("Animation", combo_label.c_str())) {
        for (const auto& animation : skeleton.animations()) {
            const bool selected = state->selected_animation_name == animation.name;
            ImGui::PushID(animation.name.c_str());
            if (icon_selectable(state->icons, Icon::NodeAnim, animation.name.c_str(), selected)) {
                state->timeline_playing = false;
                set_selected_animation(state, animation.name, "Timeline", true, true);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
        ImGui::EndCombo();
    }

    normalize_state_preview_settings(state);
    const auto apply_state_preview_change = [&](std::string status_message) {
        state->timeline_playing = false;
        refresh_preview_pose(state);
        state->status_message = std::move(status_message);
    };

    // Preview options collapse by default — scrubbing the clip is the
    // primary action; queueing/mix overrides are advanced.
    const bool preview_opts_open =
        widgets::section_header("Preview options", nullptr, false);
    if (preview_opts_open) {
    bool queue_enabled = state->preview_queue_enabled;
    if (ImGui::Checkbox("Queue Next Clip", &queue_enabled)) {
        state->preview_queue_enabled = queue_enabled;
        normalize_state_preview_settings(state);
        apply_state_preview_change(
            std::string(queue_enabled ? "Enabled" : "Disabled") + " queued state preview");
    }
    ImGui::SameLine();
    bool reverse_enabled = state->preview_reverse;
    if (ImGui::Checkbox("Reverse", &reverse_enabled)) {
        state->preview_reverse = reverse_enabled;
        apply_state_preview_change(
            std::string(reverse_enabled ? "Enabled" : "Disabled") + " reverse preview");
    }

    if (state->preview_queue_enabled) {
        const char* queued_label =
            state->preview_queued_animation_name.empty()
                ? "<select animation>"
                : state->preview_queued_animation_name.c_str();
        if (ImGui::BeginCombo("Queued Animation", queued_label)) {
            for (const auto& preview_animation : skeleton.animations()) {
                if (preview_animation.name == state->selected_animation_name) {
                    continue;
                }
                const bool selected =
                    state->preview_queued_animation_name == preview_animation.name;
                ImGui::PushID(preview_animation.name.c_str());
                if (icon_selectable(
                        state->icons,
                        Icon::NodeAnim,
                        preview_animation.name.c_str(),
                        selected)) {
                    state->preview_queued_animation_name = preview_animation.name;
                    apply_state_preview_change(
                        "Queued " + preview_animation.name + " after " +
                        state->selected_animation_name);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
                ImGui::PopID();
            }
            ImGui::EndCombo();
        }

        double queue_delay = state->preview_queue_delay;
        if (ImGui::DragScalar(
                "Queue Delay",
                ImGuiDataType_Double,
                &queue_delay,
                0.01f,
                nullptr,
                nullptr,
                "%.3f s")) {
            state->preview_queue_delay = std::max(0.0, queue_delay);
            apply_state_preview_change("Updated queued preview delay");
        }

        bool custom_mix = state->preview_use_custom_mix_duration;
        if (ImGui::Checkbox("Override Mix Duration", &custom_mix)) {
            state->preview_use_custom_mix_duration = custom_mix;
            apply_state_preview_change(
                std::string(custom_mix ? "Enabled" : "Disabled") + " custom mix preview");
        }
        if (state->preview_use_custom_mix_duration) {
            double mix_duration = state->preview_custom_mix_duration;
            if (ImGui::DragScalar(
                    "Mix Duration",
                    ImGuiDataType_Double,
                    &mix_duration,
                    0.01f,
                    nullptr,
                    nullptr,
                    "%.3f s")) {
                state->preview_custom_mix_duration = std::max(0.0, mix_duration);
                apply_state_preview_change("Updated queued preview mix duration");
            }
        }
    }
    }  // preview_opts_open

    const marrow::runtime::AnimationData* animation = selected_animation(*state);
    const double duration_seconds = timeline_preview_duration(*state);
    const std::vector<TimelineTrackRow> tracks =
        animation != nullptr ? build_timeline_tracks(skeleton, *animation)
                             : std::vector<TimelineTrackRow>{};

    if (icon_button(
            state->icons,
            Icon::Rewind,
            "Reset to start",
            false,
            animation == nullptr)) {
        state->timeline_playing = false;
        scrub_timeline_time(state, 0.0, "Timeline", true);
    }
    ImGui::SameLine();
    if (icon_button(
            state->icons,
            Icon::PrevKey,
            "Previous keyframe",
            false,
            animation == nullptr)) {
        if (const auto previous_key = adjacent_key_time(
                tracks,
                state->timeline_time_seconds,
                false)) {
            state->timeline_playing = false;
            scrub_timeline_time(state, *previous_key, "Timeline", true);
        }
    }
    ImGui::SameLine();
    const Icon play_icon = state->timeline_playing ? Icon::Pause : Icon::Play;
    if (icon_button(
            state->icons,
            play_icon,
            state->timeline_playing ? "Pause" : "Play",
            state->timeline_playing,
            animation == nullptr)) {
        if (animation != nullptr) {
            const bool was_playing = state->timeline_playing;
            state->timeline_playing = !state->timeline_playing;
            if (state->timeline_playing && !was_playing) {
                refresh_preview_pose(state);
                state->animation_state->set_listener(
                    [state](marrow::runtime::AnimationState&,
                            marrow::runtime::AnimationStateEventType type,
                            const std::shared_ptr<marrow::runtime::TrackEntry>&,
                            const marrow::runtime::AnimationEvent* event) {
                        if (type == marrow::runtime::AnimationStateEventType::Event &&
                            event != nullptr) {
                            state->preview_events.push_back(*event);
                        }
                    });
            } else if (!state->timeline_playing && was_playing) {
                if (state->animation_state) {
                    state->animation_state->set_listener({});
                }
            }
            state->status_message =
                std::string(state->timeline_playing ? "Playing " : "Paused ") +
                state->selected_animation_name;
        }
    }
    ImGui::SameLine();
    if (icon_button(
            state->icons,
            Icon::NextKey,
            "Next keyframe",
            false,
            animation == nullptr)) {
        if (const auto next_key = adjacent_key_time(
                tracks,
                state->timeline_time_seconds,
                true)) {
            state->timeline_playing = false;
            scrub_timeline_time(state, *next_key, "Timeline", true);
        }
    }
    ImGui::SameLine();
    if (icon_button(
            state->icons,
            Icon::Loop,
            "Toggle looping",
            state->timeline_loop)) {
        state->timeline_loop = !state->timeline_loop;
        refresh_preview_pose(state);
        state->status_message =
            std::string(state->timeline_loop ? "Enabled" : "Disabled") + " timeline looping";
    }

    // Separator between playback and keyframe clusters
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();

    // Add / Remove keyframe — navigation helpers that jump to the per-track
    // keyframe editor below. Disabled when no track is selected. Actual
    // add/remove lives in the track-specific editor since each track type
    // (transform / deform / draw-order / event) has its own keyframe model.
    const bool has_selected_track = state->selected_timeline_track_id.has_value();
    if (icon_button(
            state->icons,
            Icon::AddKey,
            has_selected_track
                ? "Scroll to track editor to add keyframe at playhead"
                : "Select a track first to add keyframes",
            false,
            !has_selected_track)) {
        state->status_message = "Use the track editor below to add a keyframe at the playhead";
    }
    ImGui::SameLine();
    if (icon_button(
            state->icons,
            Icon::RemoveKey,
            has_selected_track
                ? "Scroll to track editor to remove keyframe at playhead"
                : "Select a track first to remove keyframes",
            false,
            !has_selected_track)) {
        state->status_message = "Use the track editor below to remove the keyframe nearest the playhead";
    }

    double slider_time = state->timeline_time_seconds;
    const double minimum_time = 0.0;
    const double maximum_time = duration_seconds > 0.0 ? duration_seconds : 1.0;
    if (duration_seconds <= 0.0) {
        ImGui::BeginDisabled();
    }
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderScalar(
            "Time",
            ImGuiDataType_Double,
            &slider_time,
            &minimum_time,
            &maximum_time,
            "%.3fs")) {
        state->timeline_playing = false;
        scrub_timeline_time(state, slider_time, "Timeline", true);
    }
    if (duration_seconds <= 0.0) {
        ImGui::EndDisabled();
    }

    ImGui::Text(
        "Preview span: %s   Keyed tracks: %zu   Root motion total: (%.2f, %.2f)",
        format_time_seconds(duration_seconds).c_str(),
        tracks.size(),
        state->preview_root_motion_total.x,
        state->preview_root_motion_total.y);

    if (tracks.empty()) {
        ImGui::TextUnformatted("The selected animation does not contain keyed tracks.");
        ImGui::End();
        return;
    }

    const float table_height = std::clamp(
        96.0f + static_cast<float>(tracks.size()) * 28.0f,
        180.0f,
        420.0f);
    if (ImGui::BeginTable(
            "timeline_tracks",
            2,
            ImGuiTableFlags_BordersInnerV |
                ImGuiTableFlags_RowBg |
                ImGuiTableFlags_Resizable |
                ImGuiTableFlags_ScrollY |
                ImGuiTableFlags_SizingStretchProp,
            ImVec2(0.0f, table_height))) {
        ImGui::TableSetupColumn("Track", ImGuiTableColumnFlags_WidthFixed, 260.0f);
        ImGui::TableSetupColumn("Keys", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();

        for (const TimelineTrackRow& track : tracks) {
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            const bool selected = timeline_track_matches_selection(*state, track);
            const Icon track_icon = track_property_icon(track.id);
            const std::string label =
                track.label + "  (" + std::to_string(track.key_times.size()) + ")";
            ImGui::PushID(track.id.c_str());
            if (icon_selectable(state->icons, track_icon, label.c_str(), selected)) {
                state->timeline_playing = false;
                focus_timeline_track(
                    state,
                    track,
                    state->timeline_time_seconds,
                    "Timeline",
                    true);
            }
            ImGui::PopID();

            ImGui::TableSetColumnIndex(1);
            draw_timeline_lane(state, track, duration_seconds);
        }

        ImGui::EndTable();
    }

    draw_transform_timeline_editor(state, tracks);

    ImGui::End();
}


} // namespace marrow::editor::shell
