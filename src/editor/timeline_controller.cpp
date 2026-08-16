#include "timeline_controller.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

#include "shell_derived_cache.hpp"
#include "shell_preview.hpp"
#include "shell_selection.hpp"
#include "marrow/editor/authoring.hpp"

namespace marrow::editor::shell {

using marrow::editor::timeline_model::append_selected_timeline_fragment;
using marrow::editor::timeline_model::clamp_existing_key_time;
using marrow::editor::timeline_model::clamp_existing_non_decreasing_key_time;
using marrow::editor::timeline_model::insertable_key_time;
using marrow::editor::timeline_model::paste_keys_replace_collisions;

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

std::vector<double> collect_animation_key_times(
    const marrow::runtime::AnimationData& animation) {
    return marrow::editor::timeline_model::collect_animation_key_times(animation);
}

std::string format_time_seconds(double time_seconds) {
    std::ostringstream stream;
    stream << std::fixed << std::setprecision(3) << time_seconds << "s";
    return stream.str();
}

std::vector<TimelineTrackRow> build_timeline_tracks(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::AnimationData& animation) {
    return marrow::editor::timeline_model::build_tracks(skeleton, animation);
}

TimelineKeyRef timeline_key_ref(
    const TimelineTrackRow& track,
    std::size_t key_index) {
    return marrow::editor::timeline_model::key_ref(track, key_index);
}

std::optional<std::size_t> timeline_key_index(
    const TimelineTrackRow& track,
    const TimelineKeyRef& key) {
    return marrow::editor::timeline_model::key_index(track, key);
}

void reconcile_timeline_key_selection(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (state == nullptr) {
        return;
    }
    marrow::editor::timeline_model::reconcile_selection(
        &state->timeline_editor.selected_keys, tracks);
}

const TimelineTrackRow* selected_timeline_track(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (!state.selected_timeline_track_id.has_value()) {
        return nullptr;
    }
    return marrow::editor::timeline_model::find_track(
        tracks, *state.selected_timeline_track_id);
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
    return marrow::editor::timeline_model::find_track(tracks, track_id);
}

bool timeline_track_is_editable(const TimelineTrackRow& track) {
    return marrow::editor::timeline_model::track_is_editable(track);
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
    const auto completion =
        marrow::editor::timeline_model::completion_decision(true, changed);
    if (completion.action ==
        marrow::editor::timeline_model::CompletionAction::Cancel) {
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
    return marrow::editor::timeline_model::selected_indices(
        state.timeline_editor.selected_keys, track);
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

bool paste_timeline_clipboard(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (state == nullptr || !state->load_result || authoring_gesture_active(*state)) {
        return false;
    }
    const TimelineClipboard& clipboard = state->timeline_editor.clipboard;
    const auto shift = marrow::editor::timeline_model::clipboard_time_shift(
        clipboard, state->selected_animation_name, state->timeline_time_seconds);
    if (!shift.has_value()) {
        return false;
    }
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
        marrow::editor::timeline_model::clipboard_track_count(clipboard);
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
                *shift,
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
                *shift,
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
                *shift,
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
                *shift,
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
                *shift,
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
                *shift,
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




bool begin_timeline_retime_gesture(
    ShellState* state,
    std::uint32_t item_id,
    float start_mouse_x,
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
    gesture.start_mouse_x = start_mouse_x;
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
    const auto completion =
        marrow::editor::timeline_model::completion_decision(
            commit, gesture.changed);
    if (completion.action ==
        marrow::editor::timeline_model::CompletionAction::Cancel) {
        gesture.transaction.cancel();
        sync_shell_from_editor_session(state);
        if (completion.report_cancelled) {
            state->status_message = "Cancelled timeline retime";
        }
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
    const auto incremental_delta =
        marrow::editor::timeline_model::incremental_retime_delta(
            requested_delta, gesture.applied_delta);
    if (!incremental_delta.has_value()) {
        finish_timeline_retime_gesture(state, false);
        state->error_message = "Timeline retime delta must be finite.";
        state->status_message = "Timeline retime failed";
        return false;
    }

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
            *incremental_delta,
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


} // namespace marrow::editor::shell
