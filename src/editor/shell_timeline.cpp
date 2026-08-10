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
#include <type_traits>
#include <utility>
#include <vector>

#include "imgui.h"

#include "shell_coalesced_edit.hpp"
#include "shell_derived_cache.hpp"
#include "shell_selection.hpp"
#include "shell_preview.hpp"
#include "shell_state.hpp"
#include "shell_widgets.hpp"
#include "marrow/editor/agent_dispatch.hpp"
#include "marrow/editor/authoring.hpp"
#include "marrow/editor/authoring.hpp"

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
    return apply_coalesced_edit_frame(
        state,
        coalesced_edit_frame_from_last_item(changed),
        CoalescedEditDescriptor{
            kind,
            std::move(label),
            std::move(group),
            allow_merge,
            CoalescedEditPolicy::ProjectRuntime,
            std::move(failure_status)},
        std::move(mutate));
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

namespace {

std::int64_t timeline_time_identity(double time_seconds) {
    return static_cast<std::int64_t>(std::llround(time_seconds * 1'000'000.0));
}

} // namespace

TimelineKeyRef timeline_key_ref(
    const TimelineTrackRow& track,
    std::size_t key_index) {
    TimelineKeyRef result;
    result.track_id = track.id;
    if (key_index >= track.key_times.size()) return result;

    result.time_microseconds = timeline_time_identity(track.key_times[key_index]);
    result.same_time_count = 0U;
    for (std::size_t index = 0U; index < track.key_times.size(); ++index) {
        if (timeline_time_identity(track.key_times[index]) != result.time_microseconds) continue;
        if (index < key_index) ++result.same_time_ordinal;
        ++result.same_time_count;
    }
    return result;
}

std::optional<std::size_t> timeline_key_index(
    const TimelineTrackRow& track,
    const TimelineKeyRef& key) {
    if (key.track_id != track.id || key.same_time_count == 0U) return std::nullopt;

    std::size_t same_time_count = 0U;
    std::optional<std::size_t> resolved;
    for (std::size_t index = 0U; index < track.key_times.size(); ++index) {
        if (timeline_time_identity(track.key_times[index]) != key.time_microseconds) continue;
        if (same_time_count == key.same_time_ordinal) resolved = index;
        ++same_time_count;
    }
    // If an insertion/removal changed a same-time event group, retaining the
    // old ordinal could silently select a different event. Drop it instead.
    if (same_time_count != key.same_time_count) return std::nullopt;
    return resolved;
}

void reconcile_timeline_key_selection(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (state == nullptr) return;
    auto& selection = state->timeline_editor.selected_keys;
    selection.erase(
        std::remove_if(
            selection.begin(),
            selection.end(),
            [&](const TimelineKeyRef& key) {
                const auto track = std::find_if(
                    tracks.begin(), tracks.end(), [&](const TimelineTrackRow& candidate) {
                        return candidate.id == key.track_id;
                    });
                return track == tracks.end() || !timeline_key_index(*track, key).has_value();
            }),
        selection.end());
    std::vector<TimelineKeyRef> unique;
    unique.reserve(selection.size());
    for (const TimelineKeyRef& key : selection) {
        if (std::find(unique.begin(), unique.end(), key) == unique.end()) {
            unique.push_back(key);
        }
    }
    selection = std::move(unique);
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
    const ResolvedSelection resolved = resolve_shell_selection(state);
    if (state.selected_timeline_track_id.has_value() &&
        *state.selected_timeline_track_id == track.id &&
        ((!track.slot_index.has_value() && !track.bone_index.has_value()) ||
         (track.slot_index.has_value() &&
          resolved.active_slot_index == track.slot_index) ||
         (track.bone_index.has_value() &&
          resolved.active_bone_index == track.bone_index))) {
        return true;
    }

    if (track.slot_index.has_value() &&
        resolved.active_slot_index == track.slot_index) {
        return true;
    }

    return track.bone_index.has_value() &&
        resolved.active_bone_index == track.bone_index;
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
    auto* edit = marrow::editor::ensure_transform_timeline_edit(
        *state->load_result.project,
        *state->session.runtime_data(),
        track.animation_name,
        bone_name,
        *track.transform_channel);
    return edit == nullptr
        ? std::nullopt
        : std::optional<std::size_t>(static_cast<std::size_t>(
              edit - state->load_result.project->transform_timeline_edits.data()));
}

std::optional<std::size_t> ensure_mesh_deform_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || !track.slot_index.has_value() ||
        !track.deform_attachment_name.has_value() ||
        *track.slot_index >= state->load_result.skeleton_data->slots().size()) {
        return std::nullopt;
    }

    const std::string slot_name =
        state->load_result.skeleton_data->slots()[*track.slot_index].name;
    auto* edit = marrow::editor::ensure_mesh_deform_timeline_edit(
        *state->load_result.project,
        *state->session.runtime_data(),
        track.animation_name,
        slot_name,
        *track.deform_attachment_name);
    return edit == nullptr
        ? std::nullopt
        : std::optional<std::size_t>(static_cast<std::size_t>(
              edit - state->load_result.project->mesh_deform_timeline_edits.data()));
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

    auto* edit = marrow::editor::ensure_draw_order_timeline_edit(
        *state->load_result.project,
        *state->session.runtime_data(),
        track.animation_name);
    return edit == nullptr
        ? std::nullopt
        : std::optional<std::size_t>(static_cast<std::size_t>(
              edit - state->load_result.project->draw_order_timeline_edits.data()));
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

    auto* edit = marrow::editor::ensure_event_timeline_edit(
        *state->load_result.project,
        *state->session.runtime_data(),
        track.animation_name);
    return edit == nullptr
        ? std::nullopt
        : std::optional<std::size_t>(static_cast<std::size_t>(
              edit - state->load_result.project->event_timeline_edits.data()));
}

std::optional<marrow::editor::SlotColorTimelineEdit> make_slot_color_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track) {
    if (!state.load_result || !track.slot_index.has_value() ||
        *track.slot_index >= state.load_result.skeleton_data->slots().size()) {
        return std::nullopt;
    }
    const auto* animation =
        state.load_result.skeleton_data->find_animation(track.animation_name);
    const auto* timeline =
        animation != nullptr ? animation->find_color_timeline(*track.slot_index) : nullptr;
    if (timeline == nullptr) {
        return std::nullopt;
    }

    marrow::editor::SlotColorTimelineEdit edit;
    edit.animation_name = track.animation_name;
    edit.slot_name = state.load_result.skeleton_data->slots()[*track.slot_index].name;
    edit.keyframes.reserve(timeline->keyframes.size());
    for (const auto& keyframe : timeline->keyframes) {
        marrow::editor::SlotColorKeyframeEdit copied;
        copied.time = static_cast<double>(keyframe.time);
        copied.color = keyframe.color;
        copied.interpolation = keyframe.interpolation;
        edit.keyframes.push_back(std::move(copied));
    }
    return edit;
}

std::optional<marrow::editor::SlotAttachmentTimelineEdit>
make_slot_attachment_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track) {
    if (!state.load_result || !track.slot_index.has_value() ||
        *track.slot_index >= state.load_result.skeleton_data->slots().size()) {
        return std::nullopt;
    }
    const auto* animation =
        state.load_result.skeleton_data->find_animation(track.animation_name);
    const auto* timeline =
        animation != nullptr ? animation->find_attachment_timeline(*track.slot_index) : nullptr;
    if (timeline == nullptr) {
        return std::nullopt;
    }

    marrow::editor::SlotAttachmentTimelineEdit edit;
    edit.animation_name = track.animation_name;
    edit.slot_name = state.load_result.skeleton_data->slots()[*track.slot_index].name;
    edit.keyframes.reserve(timeline->keyframes.size());
    for (const auto& keyframe : timeline->keyframes) {
        edit.keyframes.push_back(marrow::editor::SlotAttachmentKeyframeEdit{
            static_cast<double>(keyframe.time),
            keyframe.attachment_name});
    }
    return edit;
}

std::optional<std::size_t> ensure_slot_color_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (state == nullptr || !state->load_result || !track.slot_index.has_value() ||
        *track.slot_index >= state->load_result.skeleton_data->slots().size()) {
        return std::nullopt;
    }
    const std::string slot_name =
        state->load_result.skeleton_data->slots()[*track.slot_index].name;
    auto* edit = marrow::editor::ensure_slot_color_timeline_edit(
        *state->load_result.project,
        *state->session.runtime_data(),
        track.animation_name,
        slot_name);
    return edit == nullptr
        ? std::nullopt
        : std::optional<std::size_t>(static_cast<std::size_t>(
              edit - state->load_result.project->slot_color_timeline_edits.data()));
}

std::optional<std::size_t> ensure_slot_attachment_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (state == nullptr || !state->load_result || !track.slot_index.has_value() ||
        *track.slot_index >= state->load_result.skeleton_data->slots().size()) {
        return std::nullopt;
    }
    const std::string& slot_name =
        state->load_result.skeleton_data->slots()[*track.slot_index].name;
    auto* edit = marrow::editor::ensure_slot_attachment_timeline_edit(
        *state->load_result.project,
        *state->session.runtime_data(),
        track.animation_name,
        slot_name);
    return edit == nullptr
        ? std::nullopt
        : std::optional<std::size_t>(static_cast<std::size_t>(
              edit - state->load_result.project->slot_attachment_timeline_edits.data()));
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

    if (state->selected_animation_name != animation->name) {
        state->timeline_editor.selected_keys.clear();
        state->timeline_editor.box_selection.reset();
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
    state->hierarchy_selection_anchor.reset();
    if (track.slot_index.has_value()) {
        select_slot(state, *track.slot_index, source, false);
    } else if (track.bone_index.has_value()) {
        select_bone(state, *track.bone_index, source, false);
    }
    state->selected_timeline_track_id = track.id;

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

const TimelineTrackRow* find_timeline_track(
    const std::vector<TimelineTrackRow>& tracks,
    std::string_view track_id) {
    const auto iterator = std::find_if(
        tracks.begin(),
        tracks.end(),
        [&](const TimelineTrackRow& track) { return track.id == track_id; });
    return iterator == tracks.end() ? nullptr : &(*iterator);
}

bool timeline_track_is_editable(const TimelineTrackRow& track) {
    return track.transform_channel.has_value() ||
        track.deform_attachment_name.has_value() ||
        (track.slot_index.has_value() &&
         (track.id.find(":Color") != std::string::npos ||
          track.id.find(":Attachment") != std::string::npos)) ||
        track.id == "global:draw-order" ||
        track.id == "global:events";
}

bool timeline_key_selected(const ShellState& state, const TimelineKeyRef& key) {
    return std::find(
               state.timeline_editor.selected_keys.begin(),
               state.timeline_editor.selected_keys.end(),
               key) != state.timeline_editor.selected_keys.end();
}

std::optional<marrow::editor::TimelineKeySelector> timeline_key_selector(
    const ShellState& state,
    const TimelineTrackRow& track,
    std::size_t key_index) {
    if (!state.load_result || key_index >= track.key_times.size()) {
        return std::nullopt;
    }
    const auto& skeleton = *state.session.runtime_data();
    marrow::editor::TimelineKeySelector selector;
    selector.animation_name = track.animation_name;
    selector.time = track.key_times[key_index];
    if (track.transform_channel.has_value() && track.bone_index.has_value() &&
        *track.bone_index < skeleton.bones().size()) {
        selector.kind = marrow::editor::TimelineKeyKind::Transform;
        selector.bone_name = skeleton.bones()[*track.bone_index].name;
        selector.transform_channel = *track.transform_channel;
        return selector;
    }
    if (track.deform_attachment_name.has_value() && track.slot_index.has_value() &&
        *track.slot_index < skeleton.slots().size()) {
        selector.kind = marrow::editor::TimelineKeyKind::Deform;
        selector.slot_name = skeleton.slots()[*track.slot_index].name;
        selector.attachment_name = *track.deform_attachment_name;
        return selector;
    }
    if (track.id == "global:draw-order") {
        selector.kind = marrow::editor::TimelineKeyKind::DrawOrder;
        return selector;
    }
    if (track.id == "global:events") {
        selector.kind = marrow::editor::TimelineKeyKind::Event;
        for (std::size_t index = 0U; index < key_index; ++index) {
            if (std::abs(track.key_times[index] - selector.time) <= 1e-6) {
                ++selector.same_time_ordinal;
            }
        }
        return selector;
    }
    if (track.slot_index.has_value() && *track.slot_index < skeleton.slots().size()) {
        selector.slot_name = skeleton.slots()[*track.slot_index].name;
        if (track.id.find(":Color") != std::string::npos) {
            selector.kind = marrow::editor::TimelineKeyKind::SlotColor;
            return selector;
        }
        if (track.id.find(":Attachment") != std::string::npos) {
            selector.kind = marrow::editor::TimelineKeyKind::SlotAttachment;
            return selector;
        }
    }
    return std::nullopt;
}

template <typename Fn>
bool visit_editable_timeline_keys(
    ShellState* state,
    const TimelineTrackRow& track,
    Fn&& visitor) {
    if (state == nullptr || !state->load_result ||
        state->load_result.project == nullptr || !timeline_track_is_editable(track)) {
        return false;
    }
    if (track.transform_channel.has_value()) {
        const auto index = ensure_transform_timeline_edit_index(state, track);
        if (!index.has_value()) return false;
        visitor(state->load_result.project->transform_timeline_edits[*index].keyframes);
        return true;
    }
    if (track.deform_attachment_name.has_value()) {
        const auto index = ensure_mesh_deform_timeline_edit_index(state, track);
        if (!index.has_value()) return false;
        visitor(state->load_result.project->mesh_deform_timeline_edits[*index].keyframes);
        return true;
    }
    if (track.id == "global:draw-order") {
        const auto index = ensure_draw_order_timeline_edit_index(state, track);
        if (!index.has_value()) return false;
        visitor(state->load_result.project->draw_order_timeline_edits[*index].keyframes);
        return true;
    }
    if (track.id == "global:events") {
        const auto index = ensure_event_timeline_edit_index(state, track);
        if (!index.has_value()) return false;
        visitor(state->load_result.project->event_timeline_edits[*index].keyframes);
        return true;
    }
    if (track.id.find(":Color") != std::string::npos) {
        const auto index = ensure_slot_color_timeline_edit_index(state, track);
        if (!index.has_value()) return false;
        visitor(state->load_result.project->slot_color_timeline_edits[*index].keyframes);
        return true;
    }
    if (track.id.find(":Attachment") != std::string::npos) {
        const auto index = ensure_slot_attachment_timeline_edit_index(state, track);
        if (!index.has_value()) return false;
        visitor(state->load_result.project->slot_attachment_timeline_edits[*index].keyframes);
        return true;
    }
    return false;
}

template <typename Fn>
bool visit_existing_project_timeline_keys(
    ShellState* state,
    const TimelineTrackRow& track,
    Fn&& visitor) {
    if (state == nullptr || !state->load_result ||
        state->load_result.project == nullptr || !timeline_track_is_editable(track)) {
        return false;
    }
    auto& project = *state->load_result.project;
    if (track.transform_channel.has_value() && track.bone_index.has_value() &&
        *track.bone_index < state->load_result.skeleton_data->bones().size()) {
        const std::string& bone_name =
            state->load_result.skeleton_data->bones()[*track.bone_index].name;
        auto* edit = project.find_transform_timeline_edit(
            track.animation_name, bone_name, *track.transform_channel);
        if (edit == nullptr) return false;
        visitor(edit->keyframes);
        return true;
    }
    if (track.deform_attachment_name.has_value() && track.slot_index.has_value() &&
        *track.slot_index < state->load_result.skeleton_data->slots().size()) {
        const std::string& slot_name =
            state->load_result.skeleton_data->slots()[*track.slot_index].name;
        auto* edit = project.find_mesh_deform_timeline_edit(
            track.animation_name, slot_name, *track.deform_attachment_name);
        if (edit == nullptr) return false;
        visitor(edit->keyframes);
        return true;
    }
    if (track.id == "global:draw-order") {
        auto* edit = project.find_draw_order_timeline_edit(track.animation_name);
        if (edit == nullptr) return false;
        visitor(edit->keyframes);
        return true;
    }
    if (track.id == "global:events") {
        auto* edit = project.find_event_timeline_edit(track.animation_name);
        if (edit == nullptr) return false;
        visitor(edit->keyframes);
        return true;
    }
    if (track.id.find(":Color") != std::string::npos && track.slot_index.has_value() &&
        *track.slot_index < state->load_result.skeleton_data->slots().size()) {
        const std::string& slot_name =
            state->load_result.skeleton_data->slots()[*track.slot_index].name;
        auto* edit = project.find_slot_color_timeline_edit(track.animation_name, slot_name);
        if (edit == nullptr) return false;
        visitor(edit->keyframes);
        return true;
    }
    if (track.id.find(":Attachment") != std::string::npos && track.slot_index.has_value() &&
        *track.slot_index < state->load_result.skeleton_data->slots().size()) {
        const std::string& slot_name =
            state->load_result.skeleton_data->slots()[*track.slot_index].name;
        auto* edit = project.find_slot_attachment_timeline_edit(
            track.animation_name, slot_name);
        if (edit == nullptr) return false;
        visitor(edit->keyframes);
        return true;
    }
    return false;
}

bool finish_timeline_transaction(
    ShellState* state,
    marrow::editor::EditorSession::EditTransaction transaction,
    std::string_view success_status,
    bool changed) {
    if (!changed) {
        transaction.cancel();
        sync_shell_from_editor_session(state);
        return false;
    }
    const marrow::editor::SessionResult result = transaction.commit();
    sync_shell_from_editor_session(state);
    if (!result) {
        state->error_message = result.error->format();
        state->status_message = "Timeline edit failed";
        return false;
    }
    state->status_message = std::string(success_status);
    return result.changed;
}

bool add_timeline_key_at_playhead(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (state == nullptr || !timeline_track_is_editable(track) ||
        authoring_gesture_active(*state)) {
        return false;
    }
    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::AddKeyframe,
        "Add timeline key",
        "timeline:" + track.id,
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        state->error_message = transaction.error()->format();
        return false;
    }

    std::optional<std::size_t> inserted_index;
    const bool resolved = visit_editable_timeline_keys(
        state,
        track,
        [&](auto& keys) {
            using Key = typename std::decay_t<decltype(keys)>::value_type;
            Key new_key{};
            new_key.time = state->timeline_time_seconds;
            if constexpr (std::is_same_v<Key, marrow::editor::TransformKeyframeEdit>) {
                new_key = sample_transform_keyframe(*state, track);
                if (track.transform_channel ==
                        std::optional<marrow::editor::TransformTimelineChannel>(
                            marrow::editor::TransformTimelineChannel::Rotate) &&
                    track.bone_index.has_value() &&
                    *track.bone_index < state->session.runtime_data()->bones().size()) {
                    new_key.angle = marrow::editor::setup_relative_rotation_key(
                        *state->session.runtime_data(),
                        state->session.runtime_data()
                            ->bones()[*track.bone_index]
                            .name,
                        new_key.angle);
                }
            } else if constexpr (std::is_same_v<Key, marrow::editor::DeformKeyframeEdit>) {
                new_key = sample_deform_keyframe(*state, track);
            } else if constexpr (std::is_same_v<Key, marrow::editor::DrawOrderKeyframeEdit>) {
                new_key = sample_draw_order_keyframe(*state);
            } else if constexpr (std::is_same_v<Key, marrow::editor::EventKeyframeEdit>) {
                new_key = sample_event_keyframe(*state);
            } else if constexpr (std::is_same_v<Key, marrow::editor::SlotColorKeyframeEdit>) {
                new_key.interpolation = marrow::runtime::Interpolation::linear();
                if (track.slot_index.has_value() && state->preview_skeleton &&
                    *track.slot_index < state->preview_skeleton->slot_states().size()) {
                    new_key.color =
                        state->preview_skeleton->slot_states()[*track.slot_index].color;
                }
            } else if constexpr (
                std::is_same_v<Key, marrow::editor::SlotAttachmentKeyframeEdit>) {
                if (track.slot_index.has_value() && state->preview_skeleton &&
                    *track.slot_index < state->preview_skeleton->slot_states().size()) {
                    const std::string& attachment =
                        state->preview_skeleton->slot_states()[*track.slot_index].attachment_name;
                    if (!attachment.empty()) new_key.attachment_name = attachment;
                }
            }
            new_key.time = state->timeline_time_seconds;
            auto insertion = std::lower_bound(
                keys.begin(),
                keys.end(),
                new_key.time,
                [](const Key& key, double time) { return key.time < time; });
            auto iterator = insertion;
            if constexpr (std::is_same_v<Key, marrow::editor::EventKeyframeEdit>) {
                iterator = std::upper_bound(
                    keys.begin(),
                    keys.end(),
                    new_key.time,
                    [](double time, const Key& key) { return time < key.time; });
                iterator = keys.insert(iterator, std::move(new_key));
            } else {
                iterator = marrow::editor::find_keyframe_near_time(
                    keys, new_key.time);
                if (iterator != keys.end()) {
                    *iterator = std::move(new_key);
                } else {
                    iterator = keys.insert(insertion, std::move(new_key));
                }
            }
            inserted_index = static_cast<std::size_t>(std::distance(keys.begin(), iterator));
        });
    if (!resolved || !inserted_index.has_value()) {
        transaction.cancel();
        sync_shell_from_editor_session(state);
        state->status_message = "The selected timeline is read-only";
        return false;
    }
    const bool committed = finish_timeline_transaction(
        state, std::move(transaction), "Added key at playhead", true);
    if (committed) {
        state->selected_timeline_track_id = track.id;
        const auto* animation = selected_animation(*state);
        const std::vector<TimelineTrackRow> rebuilt_tracks =
            animation != nullptr
            ? build_timeline_tracks(*state->load_result.skeleton_data, *animation)
            : std::vector<TimelineTrackRow>{};
        const TimelineTrackRow* rebuilt_track = find_timeline_track(rebuilt_tracks, track.id);
        if (rebuilt_track != nullptr && *inserted_index < rebuilt_track->key_times.size()) {
            state->timeline_editor.selected_keys = {
                timeline_key_ref(*rebuilt_track, *inserted_index)};
        } else {
            state->timeline_editor.selected_keys.clear();
        }
    }
    return committed;
}

std::vector<std::size_t> selected_indices_for_track(
    const ShellState& state,
    const TimelineTrackRow& track) {
    std::vector<std::size_t> indices;
    for (const TimelineKeyRef& key : state.timeline_editor.selected_keys) {
        if (key.track_id == track.id) {
            if (const auto index = timeline_key_index(track, key)) indices.push_back(*index);
        }
    }
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    return indices;
}

bool remove_selected_timeline_keys(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (state == nullptr || authoring_gesture_active(*state)) return false;
    std::vector<TimelineKeyRef> removals = state->timeline_editor.selected_keys;
    if (removals.empty()) {
        const TimelineTrackRow* track = selected_timeline_track(*state, tracks);
        if (track == nullptr) {
            state->status_message = "Select an editable timeline track or key";
            return false;
        }
        if (!timeline_track_is_editable(*track)) {
            state->status_message = "The selected timeline is read-only";
            return false;
        }
        std::vector<std::size_t> exact_project_indices;
        const bool has_authored_timeline = visit_existing_project_timeline_keys(
            state, *track, [&](auto& keys) {
                for (std::size_t index = 0U; index < keys.size(); ++index) {
                    if (std::abs(keys[index].time - state->timeline_time_seconds) <= 1e-6) {
                        exact_project_indices.push_back(index);
                    }
                }
            });
        if (!has_authored_timeline || exact_project_indices.empty()) {
            const bool imported_key_at_playhead = std::any_of(
                track->key_times.begin(), track->key_times.end(), [&](double time) {
                    return std::abs(time - state->timeline_time_seconds) <= 1e-6;
                });
            state->status_message = imported_key_at_playhead
                ? "Select the imported key before removing it"
                : "No authored key exists at the playhead";
            return false;
        }
        if (exact_project_indices.size() != 1U) {
            state->status_message =
                "Multiple authored keys are at the playhead; select the key to remove";
            return false;
        }
        const std::size_t index = exact_project_indices.front();
        if (index >= track->key_times.size() ||
            std::abs(track->key_times[index] - state->timeline_time_seconds) > 1e-6) {
            state->status_message = "Could not resolve the authored key at the playhead";
            return false;
        }
        removals.push_back(timeline_key_ref(*track, index));
    }
    if (removals.empty()) return false;

    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::RemoveKeyframe,
        removals.size() == 1U ? "Remove timeline key" : "Remove timeline keys",
        "timeline:remove-keys",
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        state->error_message = transaction.error()->format();
        return false;
    }

    std::size_t removed_count = 0U;
    for (const TimelineTrackRow& track : tracks) {
        std::vector<std::size_t> indices;
        for (const TimelineKeyRef& removal : removals) {
            if (removal.track_id == track.id) {
                if (const auto index = timeline_key_index(track, removal)) {
                    indices.push_back(*index);
                }
            }
        }
        std::sort(indices.begin(), indices.end(), std::greater<std::size_t>());
        indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
        if (indices.empty()) continue;
        visit_editable_timeline_keys(state, track, [&](auto& keys) {
            // A timeline must retain one key so the runtime override remains valid.
            for (const std::size_t index : indices) {
                if (keys.size() <= 1U) break;
                if (index < keys.size()) {
                    keys.erase(keys.begin() + static_cast<std::ptrdiff_t>(index));
                    ++removed_count;
                }
            }
        });
    }
    const bool committed = finish_timeline_transaction(
        state,
        std::move(transaction),
        removed_count == 1U ? "Removed timeline key" : "Removed timeline keys",
        removed_count > 0U);
    if (committed) state->timeline_editor.selected_keys.clear();
    return committed;
}

template <typename Timeline>
void append_selected_timeline_fragment(
    const Timeline& source,
    const std::vector<std::size_t>& indices,
    std::vector<Timeline>* destination) {
    Timeline copied = source;
    copied.keyframes.clear();
    for (const std::size_t index : indices) {
        if (index < source.keyframes.size()) copied.keyframes.push_back(source.keyframes[index]);
    }
    if (!copied.keyframes.empty()) destination->push_back(std::move(copied));
}

bool copy_selected_timeline_keys(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (state == nullptr || !state->load_result ||
        state->timeline_editor.selected_keys.empty()) {
        return false;
    }
    TimelineClipboard clipboard;
    clipboard.animation_name = state->selected_animation_name;
    clipboard.earliest_time = std::numeric_limits<double>::infinity();
    for (const TimelineTrackRow& track : tracks) {
        const std::vector<std::size_t> indices = selected_indices_for_track(*state, track);
        if (indices.empty() || !timeline_track_is_editable(track)) continue;
        for (const std::size_t index : indices) {
            clipboard.earliest_time = std::min(clipboard.earliest_time, track.key_times[index]);
        }
        if (track.transform_channel.has_value() && track.bone_index.has_value()) {
            const std::string& bone_name =
                state->load_result.skeleton_data->bones()[*track.bone_index].name;
            const auto* existing = state->load_result.project->find_transform_timeline_edit(
                track.animation_name, bone_name, *track.transform_channel);
            const auto runtime = make_transform_timeline_edit(*state, track);
            if (existing != nullptr) {
                append_selected_timeline_fragment(
                    *existing, indices, &clipboard.project_fragment.transform_timeline_edits);
            } else if (runtime.has_value()) {
                append_selected_timeline_fragment(
                    *runtime, indices, &clipboard.project_fragment.transform_timeline_edits);
            }
        } else if (track.deform_attachment_name.has_value()) {
            const std::string& slot_name =
                state->load_result.skeleton_data->slots()[*track.slot_index].name;
            const auto* existing = state->load_result.project->find_mesh_deform_timeline_edit(
                track.animation_name, slot_name, *track.deform_attachment_name);
            const auto runtime = make_mesh_deform_timeline_edit(*state, track);
            if (existing != nullptr) {
                append_selected_timeline_fragment(
                    *existing, indices, &clipboard.project_fragment.mesh_deform_timeline_edits);
            } else if (runtime.has_value()) {
                append_selected_timeline_fragment(
                    *runtime, indices, &clipboard.project_fragment.mesh_deform_timeline_edits);
            }
        } else if (track.id == "global:draw-order") {
            const auto* existing =
                state->load_result.project->find_draw_order_timeline_edit(track.animation_name);
            const auto runtime = make_draw_order_timeline_edit(*state, track);
            if (existing != nullptr) {
                append_selected_timeline_fragment(
                    *existing, indices, &clipboard.project_fragment.draw_order_timeline_edits);
            } else if (runtime.has_value()) {
                append_selected_timeline_fragment(
                    *runtime, indices, &clipboard.project_fragment.draw_order_timeline_edits);
            }
        } else if (track.id == "global:events") {
            const auto* existing =
                state->load_result.project->find_event_timeline_edit(track.animation_name);
            const auto runtime = make_event_timeline_edit(*state, track);
            if (existing != nullptr) {
                append_selected_timeline_fragment(
                    *existing, indices, &clipboard.project_fragment.event_timeline_edits);
            } else if (runtime.has_value()) {
                append_selected_timeline_fragment(
                    *runtime, indices, &clipboard.project_fragment.event_timeline_edits);
            }
        } else if (track.id.find(":Color") != std::string::npos) {
            const std::string& slot_name =
                state->load_result.skeleton_data->slots()[*track.slot_index].name;
            const auto* existing = state->load_result.project->find_slot_color_timeline_edit(
                track.animation_name, slot_name);
            const auto runtime = make_slot_color_timeline_edit(*state, track);
            if (existing != nullptr) {
                append_selected_timeline_fragment(
                    *existing, indices, &clipboard.project_fragment.slot_color_timeline_edits);
            } else if (runtime.has_value()) {
                append_selected_timeline_fragment(
                    *runtime, indices, &clipboard.project_fragment.slot_color_timeline_edits);
            }
        } else if (track.id.find(":Attachment") != std::string::npos) {
            const std::string& slot_name =
                state->load_result.skeleton_data->slots()[*track.slot_index].name;
            const auto* existing =
                state->load_result.project->find_slot_attachment_timeline_edit(
                    track.animation_name, slot_name);
            const auto runtime = make_slot_attachment_timeline_edit(*state, track);
            if (existing != nullptr) {
                append_selected_timeline_fragment(
                    *existing,
                    indices,
                    &clipboard.project_fragment.slot_attachment_timeline_edits);
            } else if (runtime.has_value()) {
                append_selected_timeline_fragment(
                    *runtime,
                    indices,
                    &clipboard.project_fragment.slot_attachment_timeline_edits);
            }
        }
    }
    clipboard.has_data = std::isfinite(clipboard.earliest_time);
    if (!clipboard.has_data) return false;
    state->timeline_editor.clipboard = std::move(clipboard);
    state->status_message = state->timeline_editor.selected_keys.size() == 1U
        ? "Copied timeline key"
        : "Copied timeline keys";
    return true;
}

template <typename Key>
void paste_keys_replace_collisions(
    std::vector<Key>* destination,
    const std::vector<Key>& source,
    double time_shift,
    bool retain_same_time_source_order) {
    if (destination == nullptr || source.empty()) return;
    std::vector<double> target_times;
    for (const Key& key : source) {
        const double target_time = std::max(0.0, key.time + time_shift);
        if (std::find_if(
                target_times.begin(),
                target_times.end(),
                [&](double time) { return std::abs(time - target_time) <= 1e-6; }) ==
            target_times.end()) {
            target_times.push_back(target_time);
        }
    }
    destination->erase(
        std::remove_if(
            destination->begin(),
            destination->end(),
            [&](const Key& key) {
                return std::any_of(
                    target_times.begin(),
                    target_times.end(),
                    [&](double time) { return std::abs(key.time - time) <= 1e-6; });
            }),
        destination->end());
    for (Key key : source) {
        key.time = std::max(0.0, key.time + time_shift);
        destination->push_back(std::move(key));
    }
    if (retain_same_time_source_order) {
        std::stable_sort(
            destination->begin(), destination->end(), [](const Key& left, const Key& right) {
                return left.time < right.time;
            });
    } else {
        std::sort(destination->begin(), destination->end(), [](const Key& left, const Key& right) {
            return left.time < right.time;
        });
    }
}

bool paste_timeline_clipboard(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (state == nullptr || !state->load_result || authoring_gesture_active(*state) ||
        !state->timeline_editor.clipboard.has_data ||
        state->timeline_editor.clipboard.animation_name != state->selected_animation_name) {
        return false;
    }
    const TimelineClipboard& clipboard = state->timeline_editor.clipboard;
    const double shift = state->timeline_time_seconds - clipboard.earliest_time;
    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::AddKeyframe,
        "Paste timeline keys",
        "timeline:paste",
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        state->error_message = transaction.error()->format();
        return false;
    }

    std::size_t pasted_count = 0U;
    std::optional<std::string> first_track_id;
    const std::size_t clipboard_track_count =
        clipboard.project_fragment.transform_timeline_edits.size() +
        clipboard.project_fragment.mesh_deform_timeline_edits.size() +
        clipboard.project_fragment.draw_order_timeline_edits.size() +
        clipboard.project_fragment.event_timeline_edits.size() +
        clipboard.project_fragment.slot_color_timeline_edits.size() +
        clipboard.project_fragment.slot_attachment_timeline_edits.size();
    const TimelineTrackRow* selected_remap_track = clipboard_track_count == 1U
        ? selected_timeline_track(*state, tracks)
        : nullptr;
    if (selected_remap_track != nullptr &&
        !timeline_track_is_editable(*selected_remap_track)) {
        selected_remap_track = nullptr;
    }
    const auto find_transform_track = [&](const marrow::editor::TransformTimelineEdit& edit) {
        const auto bone_index = state->load_result.skeleton_data->find_bone_index(edit.bone_name);
        if (!bone_index.has_value()) return static_cast<const TimelineTrackRow*>(nullptr);
        const auto iterator = std::find_if(
            tracks.begin(), tracks.end(), [&](const TimelineTrackRow& track) {
                return track.animation_name == state->selected_animation_name &&
                    track.bone_index == bone_index && track.transform_channel == edit.channel;
            });
        return iterator == tracks.end() ? nullptr : &(*iterator);
    };
    const auto find_slot_track = [&](std::string_view slot_name, std::string_view suffix) {
        const auto slot_index = state->load_result.skeleton_data->find_slot_index(slot_name);
        if (!slot_index.has_value()) return static_cast<const TimelineTrackRow*>(nullptr);
        const auto iterator = std::find_if(
            tracks.begin(), tracks.end(), [&](const TimelineTrackRow& track) {
                return track.animation_name == state->selected_animation_name &&
                    track.slot_index == slot_index &&
                    track.id.find(suffix) != std::string::npos;
            });
        return iterator == tracks.end() ? nullptr : &(*iterator);
    };
    const auto remember_track = [&](const TimelineTrackRow& track, std::size_t count) {
        if (!first_track_id.has_value()) first_track_id = track.id;
        pasted_count += count;
    };

    for (const auto& source : clipboard.project_fragment.transform_timeline_edits) {
        const bool remap_is_compatible = selected_remap_track != nullptr &&
            selected_remap_track->bone_index.has_value() &&
            selected_remap_track->transform_channel ==
                std::optional<marrow::editor::TransformTimelineChannel>(source.channel);
        const TimelineTrackRow* track = remap_is_compatible
            ? selected_remap_track
            : find_transform_track(source);
        if (track == nullptr) continue;
        if (const auto index = ensure_transform_timeline_edit_index(state, *track)) {
            paste_keys_replace_collisions(
                &state->load_result.project->transform_timeline_edits[*index].keyframes,
                source.keyframes,
                shift,
                false);
            remember_track(*track, source.keyframes.size());
        }
    }
    for (const auto& source : clipboard.project_fragment.mesh_deform_timeline_edits) {
        const auto slot_index = state->load_result.skeleton_data->find_slot_index(source.slot_name);
        const auto source_iterator = std::find_if(
            tracks.begin(), tracks.end(), [&](const TimelineTrackRow& track) {
                return track.slot_index == slot_index &&
                    track.deform_attachment_name ==
                        std::optional<std::string>(source.attachment_name);
            });
        const TimelineTrackRow* track = selected_remap_track != nullptr &&
                selected_remap_track->deform_attachment_name.has_value()
            ? selected_remap_track
            : (source_iterator != tracks.end() ? &(*source_iterator) : nullptr);
        if (track == nullptr) continue;
        if (const auto index = ensure_mesh_deform_timeline_edit_index(state, *track)) {
            paste_keys_replace_collisions(
                &state->load_result.project->mesh_deform_timeline_edits[*index].keyframes,
                source.keyframes,
                shift,
                false);
            remember_track(*track, source.keyframes.size());
        }
    }
    for (const auto& source : clipboard.project_fragment.draw_order_timeline_edits) {
        const TimelineTrackRow* track = selected_remap_track != nullptr &&
                selected_remap_track->id == "global:draw-order"
            ? selected_remap_track
            : find_timeline_track(tracks, "global:draw-order");
        if (track == nullptr) continue;
        if (const auto index = ensure_draw_order_timeline_edit_index(state, *track)) {
            paste_keys_replace_collisions(
                &state->load_result.project->draw_order_timeline_edits[*index].keyframes,
                source.keyframes,
                shift,
                false);
            remember_track(*track, source.keyframes.size());
        }
    }
    for (const auto& source : clipboard.project_fragment.event_timeline_edits) {
        const TimelineTrackRow* track = selected_remap_track != nullptr &&
                selected_remap_track->id == "global:events"
            ? selected_remap_track
            : find_timeline_track(tracks, "global:events");
        if (track == nullptr) continue;
        if (const auto index = ensure_event_timeline_edit_index(state, *track)) {
            paste_keys_replace_collisions(
                &state->load_result.project->event_timeline_edits[*index].keyframes,
                source.keyframes,
                shift,
                true);
            remember_track(*track, source.keyframes.size());
        }
    }
    for (const auto& source : clipboard.project_fragment.slot_color_timeline_edits) {
        const TimelineTrackRow* track = selected_remap_track != nullptr &&
                selected_remap_track->slot_index.has_value() &&
                selected_remap_track->id.find(":Color") != std::string::npos
            ? selected_remap_track
            : find_slot_track(source.slot_name, ":Color");
        if (track == nullptr) continue;
        if (const auto index = ensure_slot_color_timeline_edit_index(state, *track)) {
            paste_keys_replace_collisions(
                &state->load_result.project->slot_color_timeline_edits[*index].keyframes,
                source.keyframes,
                shift,
                false);
            remember_track(*track, source.keyframes.size());
        }
    }
    for (const auto& source : clipboard.project_fragment.slot_attachment_timeline_edits) {
        const TimelineTrackRow* track = selected_remap_track != nullptr &&
                selected_remap_track->slot_index.has_value() &&
                selected_remap_track->id.find(":Attachment") != std::string::npos
            ? selected_remap_track
            : find_slot_track(source.slot_name, ":Attachment");
        if (track == nullptr) continue;
        if (const auto index = ensure_slot_attachment_timeline_edit_index(state, *track)) {
            paste_keys_replace_collisions(
                &state->load_result.project->slot_attachment_timeline_edits[*index].keyframes,
                source.keyframes,
                shift,
                false);
            remember_track(*track, source.keyframes.size());
        }
    }

    const bool committed = finish_timeline_transaction(
        state,
        std::move(transaction),
        pasted_count == 1U ? "Pasted timeline key" : "Pasted timeline keys",
        pasted_count > 0U);
    if (committed) {
        state->timeline_editor.selected_keys.clear();
        if (first_track_id.has_value()) state->selected_timeline_track_id = *first_track_id;
    }
    return committed;
}

bool cut_selected_timeline_keys(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (!copy_selected_timeline_keys(state, tracks)) return false;
    const bool removed = remove_selected_timeline_keys(state, tracks);
    if (removed) state->status_message = "Cut timeline keys";
    return removed;
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

double timeline_major_tick_interval(double pixels_per_second) {
    constexpr std::array<double, 12> kIntervals{
        1.0 / 60.0, 1.0 / 30.0, 0.05, 0.1, 0.25, 0.5,
        1.0, 2.0, 5.0, 10.0, 30.0, 60.0};
    for (const double interval : kIntervals) {
        if (interval * pixels_per_second >= 72.0) return interval;
    }
    return kIntervals.back();
}

double timeline_time_from_x(
    const ShellState& state,
    float screen_x,
    float lane_min_x) {
    return state.timeline_editor.view_start_seconds +
        static_cast<double>(screen_x - lane_min_x) /
            state.timeline_editor.pixels_per_second;
}

float timeline_x_from_time(
    const ShellState& state,
    double time_seconds,
    float lane_min_x) {
    return lane_min_x + static_cast<float>(
        (time_seconds - state.timeline_editor.view_start_seconds) *
        state.timeline_editor.pixels_per_second);
}

bool begin_timeline_retime_gesture(
    ShellState* state,
    ImGuiID item_id,
    const std::vector<TimelineTrackRow>& tracks) {
    if (state == nullptr || authoring_gesture_active(*state) ||
        state->timeline_editor.selected_keys.empty()) {
        return false;
    }
    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        state->timeline_editor.selected_keys.size() == 1U
            ? "Retime timeline key"
            : "Retime timeline keys",
        "timeline:retime",
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        state->error_message = transaction.error()->format();
        return false;
    }
    TimelineRetimeGesture gesture;
    gesture.item_id = item_id;
    gesture.start_mouse_x = ImGui::GetIO().MousePos.x;
    gesture.keys = state->timeline_editor.selected_keys;
    gesture.transaction = std::move(transaction);
    for (const TimelineKeyRef& key : gesture.keys) {
        const TimelineTrackRow* track = find_timeline_track(tracks, key.track_id);
        const auto key_index =
            track != nullptr ? timeline_key_index(*track, key) : std::nullopt;
        if (track == nullptr || !key_index.has_value() || !timeline_track_is_editable(*track)) {
            gesture.transaction.cancel();
            return false;
        }
        gesture.original_times.push_back(track->key_times[*key_index]);
    }
    state->timeline_editor.retime_gesture.emplace(std::move(gesture));
    return true;
}

void finish_timeline_retime_gesture(ShellState* state, bool commit) {
    if (state == nullptr || !state->timeline_editor.retime_gesture.has_value()) return;
    TimelineRetimeGesture gesture =
        std::move(*state->timeline_editor.retime_gesture);
    state->timeline_editor.retime_gesture.reset();
    if (!commit || !gesture.changed) {
        gesture.transaction.cancel();
        sync_shell_from_editor_session(state);
        if (!commit) state->status_message = "Cancelled timeline retime";
        return;
    }
    const marrow::editor::SessionResult result = gesture.transaction.commit();
    sync_shell_from_editor_session(state);
    if (!result) {
        state->error_message = result.error->format();
        state->status_message = "Timeline retime failed";
    } else {
        state->status_message = gesture.keys.size() == 1U
            ? "Retimed timeline key"
            : "Retimed timeline keys";
    }
}

bool apply_timeline_retime_delta(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    double requested_delta,
    bool snap_to_frames) {
    if (state == nullptr || !state->timeline_editor.retime_gesture.has_value()) {
        return false;
    }
    TimelineRetimeGesture& gesture = *state->timeline_editor.retime_gesture;
    if (std::abs(requested_delta - gesture.applied_delta) <= 1e-9) return true;

    if (!gesture.materialized) {
        for (const TimelineKeyRef& key : gesture.keys) {
            const TimelineTrackRow* track = find_timeline_track(tracks, key.track_id);
            if (track == nullptr ||
                !visit_editable_timeline_keys(state, *track, [](auto&) {})) {
                finish_timeline_retime_gesture(state, false);
                state->status_message = "Could not materialize the selected timeline keys";
                return false;
            }
        }
        gesture.materialized = true;
    }
    std::vector<std::size_t> resolved_indices;
    resolved_indices.reserve(gesture.keys.size());
    for (const TimelineKeyRef& key : gesture.keys) {
        const TimelineTrackRow* track = find_timeline_track(tracks, key.track_id);
        const auto index = track != nullptr ? timeline_key_index(*track, key) : std::nullopt;
        if (!index.has_value()) {
            finish_timeline_retime_gesture(state, false);
            state->status_message = "The selected timeline keys changed during retime";
            return false;
        }
        resolved_indices.push_back(*index);
    }
    std::vector<marrow::editor::TimelineKeySelector> selectors;
    selectors.reserve(gesture.keys.size());
    for (std::size_t selection_index = 0U;
         selection_index < gesture.keys.size();
         ++selection_index) {
        const TimelineTrackRow* track =
            find_timeline_track(tracks, gesture.keys[selection_index].track_id);
        const auto selector = track != nullptr
            ? timeline_key_selector(*state, *track, resolved_indices[selection_index])
            : std::nullopt;
        if (!selector.has_value()) {
            finish_timeline_retime_gesture(state, false);
            state->status_message = "Could not resolve the selected timeline keys";
            return false;
        }
        selectors.push_back(*selector);
    }
    const marrow::editor::TimelineRetimeResult retime =
        marrow::editor::retime_keyframes(
            gesture.transaction.project(),
            selectors,
            requested_delta - gesture.applied_delta,
            snap_to_frames,
            state->timeline_editor.frames_per_second);
    if (!retime) {
        const std::string error = retime.error;
        finish_timeline_retime_gesture(state, false);
        state->error_message = error;
        state->status_message = "Timeline retime failed";
        return false;
    }
    if (!retime.changed) return true;

    const marrow::editor::SessionResult refresh = gesture.transaction.refresh_runtime();
    if (!refresh) {
        const std::string error = refresh.error->format();
        finish_timeline_retime_gesture(state, false);
        state->error_message = error;
        state->status_message = "Timeline retime preview failed";
        return false;
    }
    sync_shell_from_editor_session(state);
    const auto* rebuilt_animation =
        state->session.runtime_data()->find_animation(state->selected_animation_name);
    const std::vector<TimelineTrackRow> rebuilt_tracks =
        rebuilt_animation != nullptr
        ? build_timeline_tracks(*state->session.runtime_data(), *rebuilt_animation)
        : std::vector<TimelineTrackRow>{};
    std::vector<TimelineKeyRef> rebuilt_selection;
    rebuilt_selection.reserve(gesture.keys.size());
    for (std::size_t selection_index = 0U;
         selection_index < gesture.keys.size();
         ++selection_index) {
        const TimelineTrackRow* rebuilt_track =
            find_timeline_track(rebuilt_tracks, gesture.keys[selection_index].track_id);
        if (rebuilt_track == nullptr ||
            resolved_indices[selection_index] >= rebuilt_track->key_times.size()) {
            finish_timeline_retime_gesture(state, false);
            state->status_message = "Could not preserve timeline selection after retime";
            return false;
        }
        rebuilt_selection.push_back(
            timeline_key_ref(*rebuilt_track, resolved_indices[selection_index]));
    }
    gesture.keys = rebuilt_selection;
    state->timeline_editor.selected_keys = std::move(rebuilt_selection);
    gesture.applied_delta += retime.applied_delta;
    gesture.changed = true;
    return true;
}

void update_timeline_retime_gesture(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (state == nullptr || !state->timeline_editor.retime_gesture.has_value()) return;
    if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        finish_timeline_retime_gesture(state, false);
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        finish_timeline_retime_gesture(state, true);
        return;
    }
    TimelineRetimeGesture& gesture = *state->timeline_editor.retime_gesture;
    if (!ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) return;
    const double requested_delta =
        static_cast<double>(ImGui::GetIO().MousePos.x - gesture.start_mouse_x) /
        state->timeline_editor.pixels_per_second;
    const bool snap_to_frames =
        state->timeline_editor.snap_to_frames && !ImGui::GetIO().KeyAlt;
    (void)apply_timeline_retime_delta(
        state, tracks, requested_delta, snap_to_frames);
}

void draw_timeline_ruler(ShellState* state, double duration_seconds) {
    const float width = std::max(96.0f, ImGui::GetContentRegionAvail().x);
    constexpr float kHeight = 28.0f;
    ImGui::InvisibleButton(
        "timeline_ruler",
        ImVec2(width, kHeight),
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
    const ImVec2 rect_min = ImGui::GetItemRectMin();
    const ImVec2 rect_max = ImGui::GetItemRectMax();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();
    draw_list->AddRectFilled(rect_min, rect_max, IM_COL32(0x17, 0x1a, 0x21, 0xff));

    if (ImGui::IsItemHovered() && std::abs(ImGui::GetIO().MouseWheel) > 1e-6f) {
        const double cursor_time = timeline_time_from_x(
            *state, ImGui::GetIO().MousePos.x, rect_min.x);
        const double zoom_factor = std::pow(1.15, ImGui::GetIO().MouseWheel);
        state->timeline_editor.pixels_per_second = std::clamp(
            state->timeline_editor.pixels_per_second * zoom_factor, 24.0, 1600.0);
        state->timeline_editor.view_start_seconds = std::max(
            0.0,
            cursor_time -
                static_cast<double>(ImGui::GetIO().MousePos.x - rect_min.x) /
                    state->timeline_editor.pixels_per_second);
    }
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Middle)) {
        state->timeline_editor.view_start_seconds = std::max(
            0.0,
            state->timeline_editor.view_start_seconds -
                static_cast<double>(ImGui::GetIO().MouseDelta.x) /
                    state->timeline_editor.pixels_per_second);
    }
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        state->timeline_playing = false;
        scrub_timeline_time(
            state,
            timeline_time_from_x(*state, ImGui::GetIO().MousePos.x, rect_min.x),
            "Timeline ruler",
            true);
    }

    const double interval = timeline_major_tick_interval(
        state->timeline_editor.pixels_per_second);
    const double visible_end = state->timeline_editor.view_start_seconds +
        static_cast<double>(rect_max.x - rect_min.x) /
            state->timeline_editor.pixels_per_second;
    double tick_time = std::floor(state->timeline_editor.view_start_seconds / interval) * interval;
    for (; tick_time <= visible_end + interval; tick_time += interval) {
        if (tick_time < 0.0) continue;
        const float x = timeline_x_from_time(*state, tick_time, rect_min.x);
        draw_list->AddLine(
            ImVec2(x, rect_min.y + 10.0f),
            ImVec2(x, rect_max.y),
            IM_COL32(0x76, 0x79, 0x86, 0xb0));
        char label[48]{};
        std::snprintf(
            label,
            sizeof(label),
            "%.2fs  f%d",
            tick_time,
            static_cast<int>(std::llround(tick_time * state->timeline_editor.frames_per_second)));
        draw_list->AddText(ImVec2(x + 3.0f, rect_min.y + 1.0f), IM_COL32_WHITE, label);
    }
    const float playhead_x =
        timeline_x_from_time(*state, state->timeline_time_seconds, rect_min.x);
    if (playhead_x >= rect_min.x && playhead_x <= rect_max.x) {
        draw_list->AddLine(
            ImVec2(playhead_x, rect_min.y),
            ImVec2(playhead_x, rect_max.y),
            IM_COL32(0xff, 0x54, 0x50, 0xff),
            2.0f);
    }
    ImGui::SetItemTooltip(
        "%.0f FPS | mouse wheel zoom | middle-drag pan | span %.3fs",
        state->timeline_editor.frames_per_second,
        duration_seconds);
}

void draw_timeline_lane(
    ShellState* state,
    const TimelineTrackRow& track,
    const std::vector<TimelineTrackRow>& tracks) {
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

    const double major_interval = timeline_major_tick_interval(
        state->timeline_editor.pixels_per_second);
    const double visible_end = state->timeline_editor.view_start_seconds +
        static_cast<double>(rect_width) / state->timeline_editor.pixels_per_second;
    double tick_time =
        std::floor(state->timeline_editor.view_start_seconds / major_interval) * major_interval;
    for (; tick_time <= visible_end + major_interval; tick_time += major_interval) {
        const float tick_x = timeline_x_from_time(*state, tick_time, rect_min.x);
        draw_list->AddLine(
            ImVec2(tick_x, rect_min.y + 2.0f),
            ImVec2(tick_x, rect_max.y - 2.0f),
            IM_COL32(0x43, 0x46, 0x54, 0x4D));  // outline_variant @ 30%
    }

    // Horizontal interpolation hint line connecting keyframes
    if (track.key_times.size() >= 2) {
        const float line_y = (rect_min.y + rect_max.y) * 0.5f;
        draw_list->AddLine(
            ImVec2(timeline_x_from_time(*state, track.key_times.front(), rect_min.x), line_y),
            ImVec2(timeline_x_from_time(*state, track.key_times.back(), rect_min.x), line_y),
            IM_COL32(0xb3, 0xc5, 0xff, 0x26));  // primary @ ~15%
    }

    // Playhead: tertiary-container red (#ff5450)
    {
        const float playhead_x =
            timeline_x_from_time(*state, state->timeline_time_seconds, rect_min.x);
        draw_list->AddLine(
            ImVec2(playhead_x, rect_min.y),
            ImVec2(playhead_x, rect_max.y),
            IM_COL32(0xff, 0x54, 0x50, 0xFF),
            1.0f);
    }

    // Keyframe diamonds: primary normally, tertiary-container at playhead,
    // secondary on unselected rows for hierarchy.
    const float marker_half = 4.0f;
    for (std::size_t key_index = 0; key_index < track.key_times.size(); ++key_index) {
        const double key_time = track.key_times[key_index];
        const float marker_x = timeline_x_from_time(*state, key_time, rect_min.x);
        if (marker_x < rect_min.x - marker_half || marker_x > rect_max.x + marker_half) continue;
        const bool near_playhead =
            std::abs(key_time - state->timeline_time_seconds) <= 1e-6;
        const bool key_selected = timeline_key_selected(
            *state, timeline_key_ref(track, key_index));
        ImU32 fill_color;
        if (key_selected) {
            fill_color = IM_COL32(0xff, 0xc1, 0x5c, 0xff);
        } else if (near_playhead) {
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

    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
        !state->timeline_editor.retime_gesture.has_value()) {
        const float local_x = std::clamp(ImGui::GetIO().MousePos.x - rect_min.x, 0.0f, rect_width);
        const double clicked_time = timeline_time_from_x(
            *state, rect_min.x + local_x, rect_min.x);
        const double threshold_time = 9.0 / state->timeline_editor.pixels_per_second;
        std::optional<std::size_t> hit_key_index;
        double hit_distance = threshold_time;
        for (std::size_t index = 0; index < track.key_times.size(); ++index) {
            const double distance = std::abs(track.key_times[index] - clicked_time);
            if (distance <= hit_distance) {
                hit_distance = distance;
                hit_key_index = index;
            }
        }
        state->timeline_playing = false;
        focus_timeline_track(
            state,
            track,
            hit_key_index.has_value() ? track.key_times[*hit_key_index] : clicked_time,
            "Timeline",
            true);
        const bool additive = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        if (hit_key_index.has_value()) {
            const TimelineKeyRef hit = timeline_key_ref(track, *hit_key_index);
            auto& selection = state->timeline_editor.selected_keys;
            const auto existing = std::find(selection.begin(), selection.end(), hit);
            if (additive) {
                if (existing == selection.end()) selection.push_back(hit);
                else selection.erase(existing);
            } else if (existing == selection.end()) {
                selection = {hit};
            }
            if (timeline_key_selected(*state, hit) && timeline_track_is_editable(track)) {
                begin_timeline_retime_gesture(state, ImGui::GetItemID(), tracks);
            }
        } else {
            if (!additive) state->timeline_editor.selected_keys.clear();
            state->timeline_editor.box_selection = TimelineBoxSelection{
                track.id, clicked_time, clicked_time, additive};
        }
    }

    if (state->timeline_editor.box_selection.has_value() &&
        state->timeline_editor.box_selection->track_id == track.id) {
        auto& box = *state->timeline_editor.box_selection;
        box.current_time = timeline_time_from_x(
            *state,
            std::clamp(ImGui::GetIO().MousePos.x, rect_min.x, rect_max.x),
            rect_min.x);
        const float box_start = timeline_x_from_time(*state, box.start_time, rect_min.x);
        const float box_end = timeline_x_from_time(*state, box.current_time, rect_min.x);
        draw_list->AddRectFilled(
            ImVec2(std::min(box_start, box_end), rect_min.y),
            ImVec2(std::max(box_start, box_end), rect_max.y),
            IM_COL32(0x60, 0x8b, 0xff, 0x30));
        draw_list->AddRect(
            ImVec2(std::min(box_start, box_end), rect_min.y),
            ImVec2(std::max(box_start, box_end), rect_max.y),
            IM_COL32(0xb3, 0xc5, 0xff, 0xc0));
        if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            const double minimum_time = std::min(box.start_time, box.current_time);
            const double maximum_time = std::max(box.start_time, box.current_time);
            auto& selection = state->timeline_editor.selected_keys;
            if (!box.additive) selection.clear();
            for (std::size_t index = 0; index < track.key_times.size(); ++index) {
                if (track.key_times[index] < minimum_time - 1e-6 ||
                    track.key_times[index] > maximum_time + 1e-6) continue;
                const TimelineKeyRef key = timeline_key_ref(track, index);
                if (!timeline_key_selected(*state, key)) selection.push_back(key);
            }
            state->timeline_editor.box_selection.reset();
        }
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

void draw_slot_color_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || !track.slot_index.has_value() ||
        *track.slot_index >= state->load_result.skeleton_data->slots().size()) {
        ImGui::TextUnformatted("The selected slot-color track could not be resolved.");
        return;
    }
    const std::string& slot_name =
        state->load_result.skeleton_data->slots()[*track.slot_index].name;
    const auto runtime_edit = make_slot_color_timeline_edit(*state, track);
    if (!runtime_edit.has_value()) {
        ImGui::TextUnformatted("The selected slot-color track could not be resolved.");
        return;
    }
    const auto* existing = state->load_result.project->find_slot_color_timeline_edit(
        track.animation_name, slot_name);
    const marrow::editor::SlotColorTimelineEdit display_edit =
        existing != nullptr ? *existing : *runtime_edit;
    const double duration_seconds = selected_animation_duration(*state);

    ImGui::TextUnformatted("Slot Light RGBA Key Editor");
    ImGui::Text("%s / %s / Light RGBA", track.animation_name.c_str(), slot_name.c_str());
    ImGui::TextDisabled("Dark tint remains read-only. Light color and alpha export to .mskl.");
    ImGui::BeginChild("slot_color_key_editor", ImVec2(0.0f, 270.0f), true);
    for (std::size_t key_index = 0; key_index < display_edit.keyframes.size(); ++key_index) {
        const auto display_key = display_edit.keyframes[key_index];
        ImGui::PushID(static_cast<int>(key_index));
        const std::string header = "Key " + std::to_string(key_index + 1U) +
            " @ " + format_time_seconds(display_key.time);
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            double edited_time = display_key.time;
            const bool time_changed = ImGui::DragScalar(
                "Time", ImGuiDataType_Double, &edited_time, 0.01f, nullptr, nullptr, "%.3f s");
            apply_timeline_project_drag(
                state,
                time_changed,
                EditActionKind::EditProperty,
                "Updated slot-color key timing on " + slot_name,
                "timeline:" + track.id + ":key:" + std::to_string(key_index),
                false,
                "Slot-color edit failed",
                [&]() {
                    if (const auto edit_index = ensure_slot_color_timeline_edit_index(state, track)) {
                        auto& keys = state->load_result.project
                            ->slot_color_timeline_edits[*edit_index].keyframes;
                        keys[key_index].time = clamp_existing_key_time(
                            keys, key_index, edited_time, duration_seconds);
                    }
                });

            float rgba[4] = {
                display_key.color.r,
                display_key.color.g,
                display_key.color.b,
                display_key.color.a};
            const bool color_changed = ImGui::ColorEdit4(
                "Light RGBA", rgba, ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_Float);
            apply_timeline_project_drag(
                state,
                color_changed,
                EditActionKind::EditProperty,
                "Updated slot light color on " + slot_name,
                "timeline:" + track.id + ":key:" + std::to_string(key_index),
                false,
                "Slot-color edit failed",
                [&]() {
                    if (const auto edit_index = ensure_slot_color_timeline_edit_index(state, track)) {
                        auto& key = state->load_result.project
                            ->slot_color_timeline_edits[*edit_index].keyframes[key_index];
                        key.color = marrow::runtime::SlotColor{
                            std::clamp(rgba[0], 0.0f, 1.0f),
                            std::clamp(rgba[1], 0.0f, 1.0f),
                            std::clamp(rgba[2], 0.0f, 1.0f),
                            std::clamp(rgba[3], 0.0f, 1.0f)};
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
            constexpr const char* kInterpolationLabels[] = {"Linear", "Stepped", "Bezier"};
            if (ImGui::Combo(
                    "Interpolation",
                    &interpolation_kind,
                    kInterpolationLabels,
                    IM_ARRAYSIZE(kInterpolationLabels))) {
                const marrow::editor::ProjectData previous = *state->load_result.project;
                if (const auto edit_index = ensure_slot_color_timeline_edit_index(state, track)) {
                    auto& interpolation = state->load_result.project
                        ->slot_color_timeline_edits[*edit_index].keyframes[key_index].interpolation;
                    interpolation = interpolation_kind == 0
                        ? marrow::runtime::Interpolation::linear()
                        : interpolation_kind == 1
                            ? marrow::runtime::Interpolation::stepped()
                            : marrow::runtime::Interpolation::cubic_bezier(0.25, 0.1, 0.75, 0.9);
                    apply_project_command_change(
                        state,
                        previous,
                        EditActionKind::EditProperty,
                        "Updated slot-color interpolation on " + slot_name,
                        "timeline:" + track.id + ":key:" + std::to_string(key_index),
                        false,
                        "Slot-color edit failed");
                }
            }
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

void draw_slot_attachment_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (!state->load_result || !track.slot_index.has_value() ||
        *track.slot_index >= state->load_result.skeleton_data->slots().size()) {
        ImGui::TextUnformatted("The selected attachment track could not be resolved.");
        return;
    }
    const auto& skeleton = *state->load_result.skeleton_data;
    const std::string slot_name = skeleton.slots()[*track.slot_index].name;
    const auto runtime_edit = make_slot_attachment_timeline_edit(*state, track);
    if (!runtime_edit.has_value()) {
        ImGui::TextUnformatted("The selected attachment track could not be resolved.");
        return;
    }
    const auto* existing = state->load_result.project->find_slot_attachment_timeline_edit(
        track.animation_name, slot_name);
    const marrow::editor::SlotAttachmentTimelineEdit display_edit =
        existing != nullptr ? *existing : *runtime_edit;
    const double duration_seconds = selected_animation_duration(*state);
    const std::vector<std::string>& attachment_names =
        cached_timeline_attachment_names(state, *track.slot_index);

    ImGui::TextUnformatted("Slot Attachment Key Editor");
    ImGui::Text("%s / %s / Attachment", track.animation_name.c_str(), slot_name.c_str());
    ImGui::TextDisabled("Attachment keys are stepped; <none> hides the slot.");
    ImGui::BeginChild("slot_attachment_key_editor", ImVec2(0.0f, 250.0f), true);
    for (std::size_t key_index = 0; key_index < display_edit.keyframes.size(); ++key_index) {
        const auto display_key = display_edit.keyframes[key_index];
        ImGui::PushID(static_cast<int>(key_index));
        const std::string header = "Key " + std::to_string(key_index + 1U) +
            " @ " + format_time_seconds(display_key.time);
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) {
            double edited_time = display_key.time;
            const bool time_changed = ImGui::DragScalar(
                "Time", ImGuiDataType_Double, &edited_time, 0.01f, nullptr, nullptr, "%.3f s");
            apply_timeline_project_drag(
                state,
                time_changed,
                EditActionKind::EditProperty,
                "Updated attachment key timing on " + slot_name,
                "timeline:" + track.id + ":key:" + std::to_string(key_index),
                false,
                "Attachment edit failed",
                [&]() {
                    if (const auto edit_index =
                            ensure_slot_attachment_timeline_edit_index(state, track)) {
                        auto& keys = state->load_result.project
                            ->slot_attachment_timeline_edits[*edit_index].keyframes;
                        keys[key_index].time = clamp_existing_key_time(
                            keys, key_index, edited_time, duration_seconds);
                    }
                });

            const char* selected_name = display_key.attachment_name.has_value()
                ? display_key.attachment_name->c_str()
                : "<none>";
            if (ImGui::BeginCombo("Attachment", selected_name)) {
                const auto choose_attachment = [&](std::optional<std::string> name) {
                    const marrow::editor::ProjectData previous = *state->load_result.project;
                    if (const auto edit_index =
                            ensure_slot_attachment_timeline_edit_index(state, track)) {
                        state->load_result.project
                            ->slot_attachment_timeline_edits[*edit_index]
                            .keyframes[key_index]
                            .attachment_name = std::move(name);
                        apply_project_command_change(
                            state,
                            previous,
                            EditActionKind::EditProperty,
                            "Updated attachment key on " + slot_name,
                            "timeline:" + track.id + ":key:" + std::to_string(key_index),
                            false,
                            "Attachment edit failed");
                    }
                };
                if (ImGui::Selectable("<none>", !display_key.attachment_name.has_value())) {
                    choose_attachment(std::nullopt);
                }
                for (const std::string& attachment_name : attachment_names) {
                    const bool selected =
                        display_key.attachment_name == std::optional<std::string>(attachment_name);
                    if (ImGui::Selectable(attachment_name.c_str(), selected)) {
                        choose_attachment(attachment_name);
                    }
                }
                ImGui::EndCombo();
            }
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
    if (track->slot_index.has_value() && track->id.find(":Color") != std::string::npos) {
        draw_slot_color_timeline_editor(state, *track);
        return;
    }
    if (track->slot_index.has_value() && track->id.find(":Attachment") != std::string::npos) {
        draw_slot_attachment_timeline_editor(state, *track);
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
                const double setup_rotation = static_cast<double>(
                    skeleton.bones()[*track->bone_index].setup_pose.rotation);
                double edited_angle = display_key.angle + setup_rotation;
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
                                .angle = marrow::editor::setup_relative_rotation_key(
                                    *state->session.runtime_data(),
                                    bone_name,
                                    edited_angle);
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
    const std::vector<TimelineTrackRow>& tracks = cached_timeline_tracks(state);
    reconcile_timeline_key_selection(state, tracks);

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
            animation == nullptr || state->shell_mode == ShellMode::Parameter)) {
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

    const TimelineTrackRow* toolbar_track = selected_timeline_track(*state, tracks);
    const bool has_editable_track =
        toolbar_track != nullptr && timeline_track_is_editable(*toolbar_track);
    if (icon_button(
            state->icons,
            Icon::AddKey,
            has_editable_track
                ? "Add or replace a keyframe at the playhead"
                : "Select an editable track (Inherit remains read-only)",
            false,
            !has_editable_track)) {
        add_timeline_key_at_playhead(state, *toolbar_track);
    }
    ImGui::SameLine();
    const bool has_selected_editable_key = std::any_of(
        state->timeline_editor.selected_keys.begin(),
        state->timeline_editor.selected_keys.end(),
        [&](const TimelineKeyRef& key) {
            const TimelineTrackRow* track = find_timeline_track(tracks, key.track_id);
            return track != nullptr && timeline_track_is_editable(*track);
        });
    const bool has_remove_target = has_editable_track || has_selected_editable_key;
    if (icon_button(
            state->icons,
            Icon::RemoveKey,
            has_remove_target
                ? "Remove selected keys, or an authored key exactly at the playhead"
                : "Select an editable track (Inherit remains read-only)",
            false,
            !has_remove_target)) {
        remove_selected_timeline_keys(state, tracks);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    const bool has_key_selection = has_selected_editable_key;
    if (!has_key_selection) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Copy")) copy_selected_timeline_keys(state, tracks);
    ImGui::SameLine();
    if (ImGui::SmallButton("Cut")) cut_selected_timeline_keys(state, tracks);
    if (!has_key_selection) ImGui::EndDisabled();
    ImGui::SameLine();
    const bool paste_enabled = state->timeline_editor.clipboard.has_data &&
        state->timeline_editor.clipboard.animation_name == state->selected_animation_name;
    if (!paste_enabled) ImGui::BeginDisabled();
    if (ImGui::SmallButton("Paste")) paste_timeline_clipboard(state, tracks);
    if (!paste_enabled) ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Snap to Frames", &state->timeline_editor.snap_to_frames);

    if (ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
        !ImGui::GetIO().WantTextInput &&
        !state->timeline_editor.retime_gesture.has_value()) {
        const bool command = ImGui::GetIO().KeyCtrl || ImGui::GetIO().KeySuper;
        if (command && ImGui::IsKeyPressed(ImGuiKey_C, false)) {
            copy_selected_timeline_keys(state, tracks);
        } else if (command && ImGui::IsKeyPressed(ImGuiKey_X, false)) {
            cut_selected_timeline_keys(state, tracks);
        } else if (command && ImGui::IsKeyPressed(ImGuiKey_V, false)) {
            paste_timeline_clipboard(state, tracks);
        } else if (ImGui::IsKeyPressed(ImGuiKey_Delete, false) ||
                   ImGui::IsKeyPressed(ImGuiKey_Backspace, false)) {
            remove_selected_timeline_keys(state, tracks);
        }
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

    draw_timeline_ruler(state, duration_seconds);

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
            draw_timeline_lane(state, track, tracks);
        }

        ImGui::EndTable();
    }

    update_timeline_retime_gesture(state, tracks);

    draw_transform_timeline_editor(state, tracks);

    ImGui::End();
}


} // namespace marrow::editor::shell
