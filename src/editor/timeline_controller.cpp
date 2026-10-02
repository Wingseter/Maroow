#include "timeline_controller.hpp"

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

#include "shell_derived_cache.hpp"
#include "shell_preview.hpp"
#include "shell_selection.hpp"
#include "shell_timeline_graph.hpp"
#include "viewport_ffd_controller.hpp"
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
        &state->timeline_editor.selected_keys,
        &state->timeline_editor.active_key,
        tracks);
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

std::optional<marrow::editor::BoneInheritTimelineEdit> make_bone_inherit_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track) {
    if (!state.load_result || !track.bone_index.has_value() ||
        *track.bone_index >= state.load_result.skeleton_data->bones().size()) {
        return std::nullopt;
    }
    const auto* animation =
        state.load_result.skeleton_data->find_animation(track.animation_name);
    const auto* timeline =
        animation != nullptr ? animation->find_inherit_timeline(*track.bone_index) : nullptr;
    if (timeline == nullptr) {
        return std::nullopt;
    }

    marrow::editor::BoneInheritTimelineEdit edit;
    edit.animation_name = track.animation_name;
    edit.bone_name = state.load_result.skeleton_data->bones()[*track.bone_index].name;
    edit.keyframes.reserve(timeline->keyframes.size());
    for (const auto& keyframe : timeline->keyframes) {
        edit.keyframes.push_back(marrow::editor::InheritKeyframeEdit{
            static_cast<double>(keyframe.time), keyframe.inherit});
    }
    return edit;
}

std::optional<std::size_t> ensure_bone_inherit_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track) {
    if (state == nullptr || !state->load_result || !track.bone_index.has_value() ||
        *track.bone_index >= state->load_result.skeleton_data->bones().size()) {
        return std::nullopt;
    }
    const std::string& bone_name =
        state->load_result.skeleton_data->bones()[*track.bone_index].name;
    auto* edit = marrow::editor::ensure_bone_inherit_timeline_edit(
        *state->load_result.project,
        *state->session.runtime_data(),
        track.animation_name,
        bone_name);
    return edit == nullptr
        ? std::nullopt
        : std::optional<std::size_t>(static_cast<std::size_t>(
              edit - state->load_result.project->bone_inherit_timeline_edits.data()));
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
    // MAR-170: a newly authored continuous segment takes the remembered
    // default. The preference supplies a token, never a number, so the value
    // can only ever be one of six compile-time-verified constants.
    keyframe.interpolation =
        marrow::editor::curve_preset_interpolation(state.preferences.default_curve);

    if (!state.preview_skeleton() || !track.bone_index.has_value() ||
        *track.bone_index >= state.preview_skeleton()->bone_poses().size() ||
        !track.transform_channel.has_value()) {
        return keyframe;
    }

    const auto& pose = state.preview_skeleton()->bone_poses()[*track.bone_index].local_pose;
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
    if (!state.load_result || !state.preview_skeleton()) {
        return keyframe;
    }

    keyframe.slot_names = slot_names_from_indices(
        *state.load_result.skeleton_data,
        state.preview_skeleton()->draw_order());
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

/**
 * @brief Seeds a new inherit key from what the preview currently SHOWS.
 *
 * Sampled, not "setup" and not `Normal`: every sibling sampler reads the
 * current preview -- the transform sampler reads the preview skeleton, the
 * slot-colour path reads `slot_states()[...].color`, the attachment path reads
 * `slot_states()[...].attachment_name`. Seeding from the bone's setup pose
 * instead would make "add a key in the middle of a stepped lane" CHANGE the
 * pose at that instant, which is the one thing an Add must never do.
 */
marrow::editor::InheritKeyframeEdit sample_inherit_keyframe(
    const ShellState& state,
    const TimelineTrackRow& track) {
    marrow::editor::InheritKeyframeEdit keyframe;
    keyframe.time = state.timeline_time_seconds;
    keyframe.inherit = marrow::runtime::BoneInherit::Normal;
    if (!state.load_result || !track.bone_index.has_value() ||
        *track.bone_index >= state.load_result.skeleton_data->bones().size()) {
        return keyframe;
    }
    keyframe.inherit =
        state.load_result.skeleton_data->bones()[*track.bone_index].inherit;
    const auto* animation =
        state.load_result.skeleton_data->find_animation(track.animation_name);
    if (animation == nullptr) {
        return keyframe;
    }
    if (const auto* sampled = animation->sample_bone_inherit(
            *track.bone_index, state.timeline_time_seconds)) {
        keyframe.inherit = sampled->inherit;
    }
    return keyframe;
}

marrow::editor::DeformKeyframeEdit sample_deform_keyframe(
    const ShellState& state,
    const TimelineTrackRow& track) {
    marrow::editor::DeformKeyframeEdit keyframe;
    keyframe.time = state.timeline_time_seconds;
    keyframe.interpolation =
        marrow::editor::curve_preset_interpolation(state.preferences.default_curve);

    if (!state.load_result || !state.preview_skeleton() || !track.slot_index.has_value() ||
        !track.deform_attachment_name.has_value()) {
        return keyframe;
    }

    const auto* attachment = state.load_result.skeleton_data->find_attachment_source(
        *track.slot_index, *track.deform_attachment_name);
    if (attachment == nullptr || attachment->mesh_geometry == nullptr) {
        return keyframe;
    }

    keyframe.vertex_offsets.assign(attachment->mesh_geometry->vertices.size(), 0.0);
    if (*track.slot_index >= state.preview_skeleton()->mesh_deform_states().size()) {
        return keyframe;
    }

    const auto& deform_state =
        state.preview_skeleton()->mesh_deform_states()[*track.slot_index];
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
        state->timeline_editor.active_key.reset();
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
    viewport_ffd::reconcile_selection(state);

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
    viewport_ffd::reconcile_selection(state);

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

    if (!state->animation_state() || !state->preview_skeleton() || !state->load_result) {
        state->timeline_playing = false;
        state->session.set_playing(false);
        return;
    }
    // MAR-174: the single site where preview time advances, so the single site
    // where the transient speed applies. EditorSession::advance() forwards this
    // one delta into the displayed time, the sampled pose, the crossfade, and
    // event dispatch, so they all scale together by construction.
    // preview_playback_speed() is clamped and finite and delta_seconds is
    // already > 0, so the product is > 0 unless an absurd delta overflows it to
    // infinity, which the guard below catches.
    const double scaled_delta = delta_seconds * preview_playback_speed(*state);
    if (!std::isfinite(scaled_delta) || scaled_delta <= 0.0) {
        return;
    }
    state->session.set_playing(true);
    (void)state->session.advance(scaled_delta);
    sync_shell_from_editor_session(state);
}

void advance_timeline_playback(ShellState* state, float delta_seconds) {
    advance_timeline_playback(state, static_cast<double>(delta_seconds));
}

bool set_preview_playback_speed(
    ShellState* state,
    double speed,
    std::string_view source,
    bool update_status_message) {
    // A non-finite request has no sensible clamp target, and substituting one
    // would hide a caller bug, so it is refused with the field bit-unchanged.
    // Finite out-of-range values clamp, which is what the shell already does
    // for its other transient preview scalars.
    if (!std::isfinite(speed)) {
        return false;
    }
    state->preview_speed =
        std::clamp(speed, kPreviewSpeedMinimum, kPreviewSpeedMaximum);

    if (update_status_message) {
        std::ostringstream stream;
        stream << "Preview speed " << std::fixed << std::setprecision(2)
               << state->preview_speed << 'x';
        if (!source.empty()) {
            stream << " via " << source;
        }
        state->status_message = stream.str();
    }
    return true;
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

bool activate_timeline_key(
    ShellState* state,
    const TimelineTrackRow& track,
    std::size_t key_index,
    bool additive,
    std::string_view source,
    bool update_status_message) {
    if (state == nullptr || key_index >= track.key_times.size()) {
        return false;
    }
    state->timeline_playing = false;
    if (!focus_timeline_track(
            state, track, track.key_times[key_index], source, update_status_message)) {
        return false;
    }
    marrow::editor::timeline_model::apply_key_activation(
        &state->timeline_editor.selected_keys,
        &state->timeline_editor.active_key,
        timeline_key_ref(track, key_index),
        additive);
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
    // MAR-185: ahead of the two id-substring slot branches, and keyed on the
    // row's own kind rather than on its id.
    if (track.kind == timeline_model::TimelineTrackKind::Inherit && track.bone_index.has_value() &&
        *track.bone_index < skeleton.bones().size()) {
        selector.kind = marrow::editor::TimelineKeyKind::Inherit;
        selector.bone_name = skeleton.bones()[*track.bone_index].name;
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

bool timeline_key_is_managed_loop_boundary(
    const ShellState& state,
    const TimelineTrackRow& track,
    std::size_t key_index) {
    if (!state.load_result || state.load_result.project == nullptr ||
        key_index >= track.key_times.size()) {
        return false;
    }
    const auto selector = timeline_key_selector(state, track, key_index);
    if (!selector.has_value() || state.session.runtime_data() == nullptr) return false;
    // One derivation, shared with the Agent guard and with the synchronization
    // itself, so no two surfaces can disagree about which key is derived.
    return marrow::editor::timeline_key_is_managed_loop_boundary(
        *state.load_result.project, *state.session.runtime_data(), *selector);
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
    if (track.kind == timeline_model::TimelineTrackKind::Inherit) {
        const auto index = ensure_bone_inherit_timeline_edit_index(state, track);
        if (!index.has_value()) return false;
        visitor(state->load_result.project->bone_inherit_timeline_edits[*index].keyframes);
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
    if (track.kind == timeline_model::TimelineTrackKind::Inherit && track.bone_index.has_value() &&
        *track.bone_index < state->load_result.skeleton_data->bones().size()) {
        const std::string& bone_name =
            state->load_result.skeleton_data->bones()[*track.bone_index].name;
        auto* edit = project.find_bone_inherit_timeline_edit(
            track.animation_name, bone_name);
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
                new_key.interpolation = marrow::editor::curve_preset_interpolation(
                    state->preferences.default_curve);
                if (track.slot_index.has_value() && state->preview_skeleton() &&
                    *track.slot_index < state->preview_skeleton()->slot_states().size()) {
                    new_key.color =
                        state->preview_skeleton()->slot_states()[*track.slot_index].color;
                }
            } else if constexpr (
                std::is_same_v<Key, marrow::editor::SlotAttachmentKeyframeEdit>) {
                if (track.slot_index.has_value() && state->preview_skeleton() &&
                    *track.slot_index < state->preview_skeleton()->slot_states().size()) {
                    const std::string& attachment =
                        state->preview_skeleton()->slot_states()[*track.slot_index].attachment_name;
                    if (!attachment.empty()) new_key.attachment_name = attachment;
                }
            } else if constexpr (
                std::is_same_v<Key, marrow::editor::InheritKeyframeEdit>) {
                // The generic `find_keyframe_near_time` replace path below needs
                // no inherit arm: Add IS Edit for this family too.
                new_key = sample_inherit_keyframe(*state, track);
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
    // MAR-171: a new neighbour invalidates the automatic curves on either side
    // of it, and the recomputation belongs to this same transaction.
    std::string auto_curve_error;
    if (!resolve_timeline_auto_curves(
            transaction.project(), track.animation_name, &auto_curve_error)) {
        transaction.cancel();
        sync_shell_from_editor_session(state);
        state->error_message = auto_curve_error;
        state->status_message = "Failed to update automatic curves: " + auto_curve_error;
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
            const TimelineKeyRef inserted_key =
                timeline_key_ref(*rebuilt_track, *inserted_index);
            state->timeline_editor.selected_keys = {inserted_key};
            state->timeline_editor.active_key = inserted_key;
        } else {
            state->timeline_editor.selected_keys.clear();
            state->timeline_editor.active_key.reset();
        }
    }
    return committed;
}

namespace {

bool same_timeline_key_selector(
    const marrow::editor::TimelineKeySelector& left,
    const marrow::editor::TimelineKeySelector& right) {
    return left.kind == right.kind && left.animation_name == right.animation_name &&
        left.bone_name == right.bone_name &&
        left.transform_channel == right.transform_channel &&
        left.slot_name == right.slot_name &&
        left.attachment_name == right.attachment_name && left.time == right.time &&
        left.same_time_ordinal == right.same_time_ordinal;
}

bool timeline_key_kind_carries_easing(marrow::editor::TimelineKeyKind kind) {
    switch (kind) {
    case marrow::editor::TimelineKeyKind::Transform:
    case marrow::editor::TimelineKeyKind::Deform:
    case marrow::editor::TimelineKeyKind::SlotColor:
        return true;
    case marrow::editor::TimelineKeyKind::DrawOrder:
    case marrow::editor::TimelineKeyKind::Event:
    case marrow::editor::TimelineKeyKind::SlotAttachment:
    // MAR-185: a stepped lane has no outgoing tangent to ease, so the MAR-170
    // curve presets skip it in `collect_curve_preset_selectors`.
    case marrow::editor::TimelineKeyKind::Inherit:
        return false;
    }
    return false;
}

/**
 * @brief The compatible selected keys, in selection order, de-duplicated.
 *
 * One definition shared by the preset row's enabled state and the write, so the
 * buttons can never be enabled for a selection the write would refuse. Order is
 * the selection order, which is stable, and every write is an identical
 * absolute value, which together make the result independent of it.
 */
std::vector<marrow::editor::TimelineKeySelector> collect_curve_preset_selectors(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks,
    std::vector<std::string>* track_ids_out,
    std::size_t* boundary_skip_count_out = nullptr) {
    std::vector<marrow::editor::TimelineKeySelector> selectors;
    selectors.reserve(state.timeline_editor.selected_keys.size());
    for (const TimelineKeyRef& key : state.timeline_editor.selected_keys) {
        const TimelineTrackRow* track = nullptr;
        std::optional<std::size_t> key_index;
        for (const TimelineTrackRow& candidate : tracks) {
            if (!timeline_track_is_editable(candidate)) continue;
            const auto index = timeline_key_index(candidate, key);
            if (!index.has_value()) continue;
            track = &candidate;
            key_index = index;
            break;
        }
        if (track == nullptr || !key_index.has_value()) continue;
        const auto selector = timeline_key_selector(state, *track, *key_index);
        if (!selector.has_value()) continue;
        // The GUI skips easing-free lanes and reports the count; the Agent
        // rejects them. A dopesheet box selection routinely spans an Event lane,
        // so rejecting the whole command would make the feature unusable there.
        if (!timeline_key_kind_carries_easing(selector->kind)) continue;
        // MAR-172: a managed boundary key's easing is derived from key 0 and
        // the sync pass would rewrite it in the same transaction, so the GUI
        // skips and counts it rather than writing something invisible.
        if (timeline_key_is_managed_loop_boundary(state, *track, *key_index)) {
            if (boundary_skip_count_out != nullptr) ++*boundary_skip_count_out;
            continue;
        }
        // Two refs can resolve to the same parent key, and the primitive
        // rejects a duplicate selector atomically, so collapsing here is
        // required rather than defensive.
        bool duplicate = false;
        for (const auto& existing : selectors) {
            if (same_timeline_key_selector(existing, *selector)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        selectors.push_back(*selector);
        if (track_ids_out != nullptr) track_ids_out->push_back(track->id);
    }
    return selectors;
}

} // namespace

std::size_t compatible_curve_preset_key_count(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks) {
    return collect_curve_preset_selectors(state, tracks, nullptr).size();
}

TimelineCurvePresetResult apply_timeline_curve_preset(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    marrow::editor::CurvePreset preset) {
    TimelineCurvePresetResult result;
    if (state == nullptr) return result;
    const marrow::editor::CurvePresetDefinition& definition =
        marrow::editor::curve_preset_definition(preset);
    const std::string display(definition.display_name);

    if (!state->load_result || state->load_result.project == nullptr ||
        selected_animation(*state) == nullptr) {
        result.error = "Applying a curve preset requires an open animation.";
        return result;
    }
    // MAR-170 opens no gesture of its own and never a second transaction, so a
    // live drag simply owns the session until it finishes.
    if (authoring_gesture_active(*state)) {
        result.error = "Finish the active edit before applying a curve preset.";
        return result;
    }

    std::vector<std::string> selector_track_ids;
    std::size_t boundary_skip_count = 0U;
    const std::vector<marrow::editor::TimelineKeySelector> selectors =
        collect_curve_preset_selectors(
            *state, tracks, &selector_track_ids, &boundary_skip_count);
    result.compatible_key_count = selectors.size();
    result.skipped_key_count =
        state->timeline_editor.selected_keys.size() - selectors.size();

    if (selectors.empty()) {
        state->status_message =
            "Select one or more Transform, Deform, or Slot Color keys";
        return result;
    }

    const std::string label = selectors.size() == 1U
        ? "Apply " + display + " curve"
        : "Apply " + display + " curve to " + std::to_string(selectors.size()) + " keys";
    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        label,
        "timeline:curve-preset",
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        result.error = transaction.error()->format();
        state->error_message = result.error;
        return result;
    }

    // Materialize every selected track before the write, so a preset applies to
    // an imported runtime-only track by copying it into the project.
    for (const std::string& track_id : selector_track_ids) {
        const TimelineTrackRow* track = find_timeline_track(tracks, track_id);
        if (track == nullptr || !visit_editable_timeline_keys(state, *track, [](auto&) {})) {
            transaction.cancel();
            sync_shell_from_editor_session(state);
            result.error = "Could not materialize a selected timeline track.";
            state->error_message = result.error;
            state->status_message = "Failed to apply curve preset";
            return result;
        }
    }

    const marrow::editor::TimelineInterpolationResult written =
        marrow::editor::set_keyframe_interpolation(
            transaction.project(), selectors, definition.kind, definition.control_points);
    if (!written) {
        transaction.cancel();
        sync_shell_from_editor_session(state);
        result.error = written.error;
        state->error_message = result.error;
        state->status_message = "Failed to apply curve preset: " + written.error;
        return result;
    }
    if (!written.changed) {
        // A no-change application is not a failure and must not add history.
        transaction.cancel();
        sync_shell_from_editor_session(state);
        state->status_message = "Selected keys already use " + display;
        return result;
    }

    // "Previewed" for a discrete click means the runtime and preview are
    // re-evaluated before the commit, so a failure cancels the whole thing.
    const marrow::editor::SessionResult refresh = transaction.refresh_runtime();
    if (!refresh) {
        result.error = refresh.error->format();
        transaction.cancel();
        sync_shell_from_editor_session(state);
        state->error_message = result.error;
        state->status_message = "Curve preset preview failed";
        return result;
    }

    const marrow::editor::SessionResult committed = transaction.commit();
    sync_shell_from_editor_session(state);
    if (!committed) {
        result.error = committed.error->format();
        state->error_message = result.error;
        state->status_message = "Failed to apply curve preset";
        return result;
    }

    result.applied = true;
    result.changed_key_count = written.changed_key_count;
    // A preset writes only `interpolation`, so no key moves in time and every
    // TimelineKeyRef stays bit-identical; the selection needs no rebuild.
    const std::string key_noun = selectors.size() == 1U ? " key" : " keys";
    if (result.skipped_key_count == 0U) {
        state->status_message = "Applied " + display + " to " +
            std::to_string(selectors.size()) + key_noun;
    } else {
        state->status_message = "Applied " + display + " to " +
            std::to_string(selectors.size()) + " of " +
            std::to_string(state->timeline_editor.selected_keys.size()) +
            " selected keys; " + std::to_string(result.skipped_key_count) +
            " have no easing";
    }
    if (boundary_skip_count != 0U) {
        state->status_message += "; " + std::to_string(boundary_skip_count) +
            (boundary_skip_count == 1U ? " is a managed loop boundary"
                                       : " are managed loop boundaries");
    }
    return result;
}

std::optional<marrow::editor::CurvePreset> active_outgoing_curve_preset(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (!state.timeline_editor.active_key.has_value()) return std::nullopt;
    const marrow::runtime::AnimationData* animation = selected_animation(state);
    const TimelineTrackRow* track = selected_timeline_track(state, tracks);
    if (animation == nullptr || track == nullptr) return std::nullopt;
    const auto projection = timeline_graph_model::project_track(*animation, *track);
    if (projection.status != timeline_graph_model::ProjectionStatus::Ready ||
        !projection.track.has_value()) {
        return std::nullopt;
    }
    const auto& keys = projection.track->keys;
    const auto found = std::find_if(
        keys.begin(), keys.end(), [&](const timeline_graph_model::Key& key) {
            return key.identity == *state.timeline_editor.active_key;
        });
    // A last key has no outgoing segment, so it names no preset at all.
    if (found == keys.end() || std::next(found) == keys.end()) return std::nullopt;
    return marrow::editor::curve_preset_of(found->outgoing_easing);
}

bool resolve_timeline_auto_curves(
    marrow::editor::ProjectData* project,
    std::string_view animation_name,
    std::string* error_out) {
    if (project == nullptr) return true;
    const marrow::editor::TimelineAutoCurveResult result =
        marrow::editor::resolve_automatic_curves(project, animation_name);
    if (!result) {
        if (error_out != nullptr) *error_out = result.error;
        return false;
    }
    return true;
}

namespace {

/**
 * @brief The compatible selected keys for a curve-mode write, de-duplicated.
 *
 * One definition shared by the row's enabled state and the write, exactly as
 * MAR-170's preset selector does. The family set is deliberately narrower than
 * the preset row's: Deform carries an easing but no addressable scalar series,
 * so it has no driver and therefore no automatic mode.
 */
std::vector<marrow::editor::TimelineKeySelector> collect_curve_mode_selectors(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks,
    std::vector<std::string>* track_ids_out,
    std::size_t* boundary_skip_count_out = nullptr) {
    std::vector<marrow::editor::TimelineKeySelector> selectors;
    selectors.reserve(state.timeline_editor.selected_keys.size());
    for (const TimelineKeyRef& key : state.timeline_editor.selected_keys) {
        const TimelineTrackRow* track = nullptr;
        std::optional<std::size_t> key_index;
        for (const TimelineTrackRow& candidate : tracks) {
            if (!timeline_track_is_editable(candidate)) continue;
            const auto index = timeline_key_index(candidate, key);
            if (!index.has_value()) continue;
            track = &candidate;
            key_index = index;
            break;
        }
        if (track == nullptr || !key_index.has_value()) continue;
        const auto selector = timeline_key_selector(state, *track, *key_index);
        if (!selector.has_value()) continue;
        if (selector->kind != marrow::editor::TimelineKeyKind::Transform &&
            selector->kind != marrow::editor::TimelineKeyKind::SlotColor) {
            continue;
        }
        // MAR-172: same reason as the preset row -- a managed boundary key's
        // curve intent is mirrored from key 0 on every transaction.
        if (timeline_key_is_managed_loop_boundary(state, *track, *key_index)) {
            if (boundary_skip_count_out != nullptr) ++*boundary_skip_count_out;
            continue;
        }
        bool duplicate = false;
        for (const auto& existing : selectors) {
            if (same_timeline_key_selector(existing, *selector)) {
                duplicate = true;
                break;
            }
        }
        if (duplicate) continue;
        selectors.push_back(*selector);
        if (track_ids_out != nullptr) track_ids_out->push_back(track->id);
    }
    return selectors;
}

/** @brief The display name of one driver component, for status text. */
std::string_view curve_driver_display_name(
    marrow::editor::TimelineScalarComponent driver) {
    switch (driver) {
    case marrow::editor::TimelineScalarComponent::Angle:
        return "Angle";
    case marrow::editor::TimelineScalarComponent::X:
        return "X";
    case marrow::editor::TimelineScalarComponent::Y:
        return "Y";
    case marrow::editor::TimelineScalarComponent::Red:
        return "Red";
    case marrow::editor::TimelineScalarComponent::Green:
        return "Green";
    case marrow::editor::TimelineScalarComponent::Blue:
        return "Blue";
    case marrow::editor::TimelineScalarComponent::Alpha:
        return "Alpha";
    }
    return "Angle";
}

} // namespace

std::size_t compatible_curve_mode_key_count(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks) {
    return collect_curve_mode_selectors(state, tracks, nullptr).size();
}

TimelineCurveModeApplyResult apply_timeline_curve_mode(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    marrow::editor::TimelineCurveMode mode,
    std::optional<marrow::editor::TimelineScalarComponent> driver) {
    TimelineCurveModeApplyResult result;
    if (state == nullptr) return result;
    const bool automatic = mode == marrow::editor::TimelineCurveMode::Auto;

    if (!state->load_result || state->load_result.project == nullptr ||
        selected_animation(*state) == nullptr) {
        result.error = "Setting a curve mode requires an open animation.";
        return result;
    }
    // MAR-171 opens no gesture of its own and never a second transaction, so a
    // live drag simply owns the session until it finishes.
    if (authoring_gesture_active(*state)) {
        result.error = "Finish the active edit before changing the curve mode.";
        return result;
    }

    std::vector<std::string> selector_track_ids;
    std::size_t boundary_skip_count = 0U;
    const std::vector<marrow::editor::TimelineKeySelector> selectors =
        collect_curve_mode_selectors(
            *state, tracks, &selector_track_ids, &boundary_skip_count);
    result.compatible_key_count = selectors.size();
    result.skipped_key_count =
        state->timeline_editor.selected_keys.size() - selectors.size();

    if (selectors.empty()) {
        state->status_message = "Select one or more Transform or Slot Color keys";
        return result;
    }

    const std::string label = automatic
        ? (selectors.size() == 1U
               ? std::string("Set automatic curve")
               : "Set automatic curves on " + std::to_string(selectors.size()) + " keys")
        : (selectors.size() == 1U
               ? std::string("Set manual curve")
               : "Set manual curves on " + std::to_string(selectors.size()) + " keys");
    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        label,
        "timeline:curve-mode",
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        result.error = transaction.error()->format();
        state->error_message = result.error;
        return result;
    }

    // Materialize every selected track before the write, so a curve mode
    // applies to an imported runtime-only track by copying it into the project.
    for (const std::string& track_id : selector_track_ids) {
        const TimelineTrackRow* track = find_timeline_track(tracks, track_id);
        if (track == nullptr || !visit_editable_timeline_keys(state, *track, [](auto&) {})) {
            transaction.cancel();
            sync_shell_from_editor_session(state);
            result.error = "Could not materialize a selected timeline track.";
            state->error_message = result.error;
            state->status_message = "Failed to set curve mode";
            return result;
        }
    }

    const marrow::editor::TimelineCurveModeResult written =
        marrow::editor::set_keyframe_curve_mode(
            transaction.project(), selectors, mode, driver);
    if (!written) {
        transaction.cancel();
        sync_shell_from_editor_session(state);
        result.error = written.error;
        state->error_message = result.error;
        state->status_message = "Failed to set curve mode: " + written.error;
        return result;
    }
    if (!written.changed) {
        // A no-change application is not a failure and must not add history.
        transaction.cancel();
        sync_shell_from_editor_session(state);
        state->status_message = automatic
            ? "Selected keys already use automatic curves"
            : "Selected keys already use manual curves";
        return result;
    }

    const marrow::editor::SessionResult refresh = transaction.refresh_runtime();
    if (!refresh) {
        result.error = refresh.error->format();
        transaction.cancel();
        sync_shell_from_editor_session(state);
        state->error_message = result.error;
        state->status_message = "Curve mode preview failed";
        return result;
    }

    const marrow::editor::SessionResult committed = transaction.commit();
    sync_shell_from_editor_session(state);
    if (!committed) {
        result.error = committed.error->format();
        state->error_message = result.error;
        state->status_message = "Failed to set curve mode";
        return result;
    }

    result.applied = true;
    result.changed_key_count = written.changed_key_count;
    result.resolved_key_count = written.resolved_key_count;
    // A curve mode writes only intent and easing, so no key moves in time and
    // every TimelineKeyRef stays bit-identical; the selection needs no rebuild.
    const std::string key_noun = selectors.size() == 1U ? " key" : " keys";
    const std::string driven = automatic
        ? " driven by " +
            std::string(curve_driver_display_name(
                driver.value_or(marrow::editor::TimelineScalarComponent::Angle)))
        : std::string();
    const std::string verb = automatic ? " to automatic curves" : " to manual curves";
    if (result.skipped_key_count == 0U) {
        state->status_message = "Set " + std::to_string(selectors.size()) + key_noun +
            verb + (driver.has_value() ? driven : std::string());
    } else {
        state->status_message = "Set " + std::to_string(selectors.size()) + " of " +
            std::to_string(state->timeline_editor.selected_keys.size()) +
            " selected keys" + verb + "; " +
            std::to_string(result.skipped_key_count) + " have no curve mode";
    }
    if (boundary_skip_count != 0U) {
        state->status_message += "; " + std::to_string(boundary_skip_count) +
            (boundary_skip_count == 1U ? " is a managed loop boundary"
                                       : " are managed loop boundaries");
    }
    return result;
}

namespace {

/** @brief The active key's project-domain selector, or nullopt. */
std::optional<marrow::editor::TimelineKeySelector> active_key_selector(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (!state.timeline_editor.active_key.has_value()) return std::nullopt;
    const TimelineTrackRow* track = selected_timeline_track(state, tracks);
    if (track == nullptr) return std::nullopt;
    const auto key_index = timeline_key_index(*track, *state.timeline_editor.active_key);
    if (!key_index.has_value()) return std::nullopt;
    return timeline_key_selector(state, *track, *key_index);
}

} // namespace

std::optional<marrow::editor::TimelineCurveMode> active_outgoing_curve_mode(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (!state.load_result || state.load_result.project == nullptr) return std::nullopt;
    const auto selector = active_key_selector(state, tracks);
    if (!selector.has_value()) return std::nullopt;
    if (selector->kind == marrow::editor::TimelineKeyKind::Transform) {
        const auto* edit = state.load_result.project->find_transform_timeline_edit(
            selector->animation_name, selector->bone_name, selector->transform_channel);
        if (edit == nullptr) return std::nullopt;
        for (const auto& keyframe : edit->keyframes) {
            if (std::abs(keyframe.time - selector->time) <= 1e-6) {
                return keyframe.curve_mode;
            }
        }
        return std::nullopt;
    }
    if (selector->kind == marrow::editor::TimelineKeyKind::SlotColor) {
        const auto* edit = state.load_result.project->find_slot_color_timeline_edit(
            selector->animation_name, selector->slot_name);
        if (edit == nullptr) return std::nullopt;
        for (const auto& keyframe : edit->keyframes) {
            if (std::abs(keyframe.time - selector->time) <= 1e-6) {
                return keyframe.curve_mode;
            }
        }
    }
    return std::nullopt;
}

std::optional<marrow::editor::TimelineScalarComponent> active_outgoing_curve_driver(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks) {
    if (!state.load_result || state.load_result.project == nullptr) return std::nullopt;
    const auto selector = active_key_selector(state, tracks);
    if (!selector.has_value()) return std::nullopt;
    if (selector->kind == marrow::editor::TimelineKeyKind::Transform) {
        const auto* edit = state.load_result.project->find_transform_timeline_edit(
            selector->animation_name, selector->bone_name, selector->transform_channel);
        if (edit == nullptr) return std::nullopt;
        for (const auto& keyframe : edit->keyframes) {
            if (std::abs(keyframe.time - selector->time) <= 1e-6) {
                return keyframe.curve_driver;
            }
        }
        return std::nullopt;
    }
    if (selector->kind == marrow::editor::TimelineKeyKind::SlotColor) {
        const auto* edit = state.load_result.project->find_slot_color_timeline_edit(
            selector->animation_name, selector->slot_name);
        if (edit == nullptr) return std::nullopt;
        for (const auto& keyframe : edit->keyframes) {
            if (std::abs(keyframe.time - selector->time) <= 1e-6) {
                return keyframe.curve_driver;
            }
        }
    }
    return std::nullopt;
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

    // MAR-172: a managed boundary key exists only because its lane's contract
    // requires it, so it is filtered out of the removal rather than deleted out
    // from under that contract. Removing the time-zero key is a different case:
    // the sync pass rejects it and cancels the whole transaction.
    std::size_t boundary_skip_count = 0U;
    removals.erase(
        std::remove_if(
            removals.begin(),
            removals.end(),
            [&](const TimelineKeyRef& removal) {
                for (const TimelineTrackRow& track : tracks) {
                    if (removal.track_id != track.id) continue;
                    const auto index = timeline_key_index(track, removal);
                    if (index.has_value() &&
                        timeline_key_is_managed_loop_boundary(*state, track, *index)) {
                        ++boundary_skip_count;
                        return true;
                    }
                }
                return false;
            }),
        removals.end());
    if (removals.empty()) {
        if (boundary_skip_count != 0U) {
            state->status_message =
                "Nothing was removed: the selection is a managed loop boundary; "
                "disable loop synchronization on that timeline first";
        }
        return false;
    }

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
    // MAR-171: a disappearing neighbour invalidates the automatic curves that
    // read it, inside this same transaction.
    std::string auto_curve_error;
    if (removed_count > 0U &&
        !resolve_timeline_auto_curves(
            transaction.project(), state->selected_animation_name, &auto_curve_error)) {
        transaction.cancel();
        sync_shell_from_editor_session(state);
        state->error_message = auto_curve_error;
        state->status_message = "Failed to update automatic curves: " + auto_curve_error;
        return false;
    }
    const bool committed = finish_timeline_transaction(
        state,
        std::move(transaction),
        removed_count == 1U ? "Removed timeline key" : "Removed timeline keys",
        removed_count > 0U);
    if (committed) {
        state->timeline_editor.selected_keys.clear();
        state->timeline_editor.active_key.reset();
    }
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
        } else if (track.kind == timeline_model::TimelineTrackKind::Inherit &&
                   track.bone_index.has_value()) {
            const std::string& bone_name =
                state->load_result.skeleton_data->bones()[*track.bone_index].name;
            const auto* existing =
                state->load_result.project->find_bone_inherit_timeline_edit(
                    track.animation_name, bone_name);
            const auto runtime = make_bone_inherit_timeline_edit(*state, track);
            if (existing != nullptr) {
                append_selected_timeline_fragment(
                    *existing,
                    indices,
                    &clipboard.project_fragment.bone_inherit_timeline_edits);
            } else if (runtime.has_value()) {
                append_selected_timeline_fragment(
                    *runtime,
                    indices,
                    &clipboard.project_fragment.bone_inherit_timeline_edits);
            }
        }
    }
    // The flag is a property of a lane in a project, never of a clipboard
    // fragment: a pasted lane's own opt-in decides, and a fragment that carried
    // one would assert a contract the destination may not satisfy.
    for (auto& edit : clipboard.project_fragment.transform_timeline_edits) {
        edit.loop_sync = false;
    }
    for (auto& edit : clipboard.project_fragment.slot_color_timeline_edits) {
        edit.loop_sync = false;
    }
    for (auto& edit : clipboard.project_fragment.mesh_deform_timeline_edits) {
        edit.loop_sync = false;
    }
    // MAR-185: `bone_inherit_timeline_edits` gets NO entry here, and its
    // absence is deliberate rather than an oversight -- `BoneInheritTimelineEdit`
    // has no `loop_sync` member at all, so there is nothing to scrub.
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
    const auto find_bone_inherit_track = [&](std::string_view bone_name) {
        const auto bone_index =
            state->load_result.skeleton_data->find_bone_index(bone_name);
        if (!bone_index.has_value()) return static_cast<const TimelineTrackRow*>(nullptr);
        const auto iterator = std::find_if(
            tracks.begin(), tracks.end(), [&](const TimelineTrackRow& track) {
                return track.animation_name == state->selected_animation_name &&
                    track.bone_index == bone_index &&
                    track.kind == timeline_model::TimelineTrackKind::Inherit;
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

    for (const auto& source : clipboard.project_fragment.bone_inherit_timeline_edits) {
        const TimelineTrackRow* track = selected_remap_track != nullptr &&
                selected_remap_track->kind == timeline_model::TimelineTrackKind::Inherit
            ? selected_remap_track
            : find_bone_inherit_track(source.bone_name);
        if (track == nullptr) continue;
        if (const auto index = ensure_bone_inherit_timeline_edit_index(state, *track)) {
            paste_keys_replace_collisions(
                &state->load_result.project->bone_inherit_timeline_edits[*index].keyframes,
                source.keyframes,
                *shift,
                false);
            remember_track(*track, source.keyframes.size());
        }
    }

    // MAR-171: pasted keys keep their copied mode and driver, and the paste
    // gives them new neighbours, so their segments resolve here.
    std::string auto_curve_error;
    if (pasted_count > 0U &&
        !resolve_timeline_auto_curves(
            transaction.project(), state->selected_animation_name, &auto_curve_error)) {
        transaction.cancel();
        sync_shell_from_editor_session(state);
        state->error_message = auto_curve_error;
        state->status_message = "Failed to update automatic curves: " + auto_curve_error;
        return false;
    }
    const bool committed = finish_timeline_transaction(
        state,
        std::move(transaction),
        pasted_count == 1U ? "Pasted timeline key" : "Pasted timeline keys",
        pasted_count > 0U);
    if (committed) {
        state->timeline_editor.selected_keys.clear();
        state->timeline_editor.active_key.reset();
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
    gesture.original_keys = state->timeline_editor.selected_keys;
    gesture.original_active_key = state->timeline_editor.active_key;
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
        // MAR-185, found by the inherit shell scenario's S5 and NOT
        // inherit-specific: `apply_timeline_retime_delta` rewrites
        // `selected_keys` so the selection follows the moving keys, and the
        // cancel below rolls the PROJECT back but left those refs naming times
        // the project no longer has. The next gesture then failed with "The
        // selected timeline keys changed during retime". `gesture.keys` is the
        // pre-gesture selection, already validated to resolve in
        // `begin_timeline_retime_gesture`, and the project is now back in
        // exactly that state -- so restoring it is the coherent end state for
        // every family, not just inherit.
        state->timeline_editor.selected_keys = gesture.original_keys;
        state->timeline_editor.active_key = gesture.original_active_key;
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

    // MAR-171: moving a key time changes every neighbouring segment's spacing.
    std::string auto_curve_error;
    if (!resolve_timeline_auto_curves(
            gesture.transaction.project(),
            state->selected_animation_name,
            &auto_curve_error)) {
        finish_timeline_retime_gesture(state, false);
        state->error_message = auto_curve_error;
        state->status_message = "Failed to update automatic curves: " + auto_curve_error;
        return false;
    }

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
    std::optional<TimelineKeyRef> rebuilt_active;
    if (state->timeline_editor.active_key.has_value()) {
        for (std::size_t selection_index = 0U;
             selection_index < gesture.keys.size();
             ++selection_index) {
            if (gesture.keys[selection_index] == *state->timeline_editor.active_key) {
                rebuilt_active = rebuilt_selection[selection_index];
                break;
            }
        }
    }
    gesture.keys = rebuilt_selection;
    state->timeline_editor.selected_keys = std::move(rebuilt_selection);
    state->timeline_editor.active_key = std::move(rebuilt_active);
    gesture.applied_delta += retime.applied_delta;
    gesture.changed = true;
    return true;
}

namespace {

/** @brief The selectors the current scale selection resolves to, or nothing. */
std::optional<std::vector<marrow::editor::TimelineKeySelector>> scale_selectors(
    const ShellState& state,
    const std::vector<TimelineKeyRef>& keys,
    const std::vector<TimelineTrackRow>& tracks,
    std::vector<std::size_t>* indices_out) {
    std::vector<marrow::editor::TimelineKeySelector> selectors;
    selectors.reserve(keys.size());
    if (indices_out != nullptr) indices_out->clear();
    for (const TimelineKeyRef& key : keys) {
        const TimelineTrackRow* track = find_timeline_track(tracks, key.track_id);
        const auto index =
            track != nullptr ? timeline_key_index(*track, key) : std::nullopt;
        if (track == nullptr || !index.has_value() ||
            !timeline_track_is_editable(*track)) {
            return std::nullopt;
        }
        const auto selector = timeline_key_selector(state, *track, *index);
        if (!selector.has_value()) {
            return std::nullopt;
        }
        if (indices_out != nullptr) indices_out->push_back(*index);
        selectors.push_back(*selector);
    }
    return selectors;
}

} // namespace

bool begin_timeline_scale_gesture(
    ShellState* state,
    std::uint32_t item_id,
    marrow::editor::TimelineScalePivot pivot,
    const std::vector<TimelineTrackRow>& tracks) {
    if (state == nullptr || authoring_gesture_active(*state) ||
        state->timeline_editor.selected_keys.empty()) {
        return false;
    }
    const auto span = marrow::editor::timeline_model::selection_time_span(
        state->timeline_editor.selected_keys, tracks);
    if (!span.valid) {
        return false;
    }
    // Refuse before anything appears to happen, rather than on the first frame,
    // and refuse through the primitive's own predicates so the two surfaces can
    // never disagree about which key is pinned or which tie would be split.
    const auto preflight_selectors = scale_selectors(
        *state, state->timeline_editor.selected_keys, tracks, nullptr);
    if (!preflight_selectors.has_value()) {
        return false;
    }
    if (state->session.project() != nullptr) {
        const std::string refusal = marrow::editor::timeline_scale_selection_refusal(
            *state->session.project(), *preflight_selectors);
        if (!refusal.empty()) {
            state->status_message = refusal;
            return false;
        }
    }

    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        state->timeline_editor.selected_keys.size() == 1U
            ? "Scale timeline key"
            : "Scale timeline keys",
        "timeline:scale",
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        state->error_message = transaction.error()->format();
        return false;
    }
    TimelineScaleGesture gesture;
    gesture.item_id = item_id;
    gesture.pivot = pivot;
    gesture.keys = state->timeline_editor.selected_keys;
    gesture.keys_before = state->timeline_editor.selected_keys;
    gesture.active_key_before = state->timeline_editor.active_key;
    gesture.transaction = std::move(transaction);
    for (const TimelineKeyRef& key : gesture.keys) {
        const TimelineTrackRow* track = find_timeline_track(tracks, key.track_id);
        const auto key_index =
            track != nullptr ? timeline_key_index(*track, key) : std::nullopt;
        if (track == nullptr || !key_index.has_value() ||
            !timeline_track_is_editable(*track)) {
            gesture.transaction.cancel();
            return false;
        }
        gesture.original_times.push_back(track->key_times[*key_index]);
    }
    gesture.pivot_time = pivot == marrow::editor::TimelineScalePivot::RangeStart
        ? span.minimum_time
        : span.maximum_time;
    gesture.edge_original_time =
        pivot == marrow::editor::TimelineScalePivot::RangeStart
        ? span.maximum_time
        : span.minimum_time;
    state->timeline_editor.scale_gesture.emplace(std::move(gesture));
    return true;
}

void finish_timeline_scale_gesture(ShellState* state, bool commit) {
    if (state == nullptr || !state->timeline_editor.scale_gesture.has_value()) return;
    TimelineScaleGesture gesture =
        std::move(*state->timeline_editor.scale_gesture);
    state->timeline_editor.scale_gesture.reset();
    auto completion = marrow::editor::timeline_model::completion_decision(
        commit, gesture.changed);
    // §7.3's error bound is argued in the design and enforced here: an argument
    // in a document does not fail a build. Re-derive every key's expected time
    // from the snapshot and cancel rather than commit on any real drift.
    bool drifted = false;
    if (completion.action ==
        marrow::editor::timeline_model::CompletionAction::Commit) {
        const auto* animation = state->session.runtime_data() != nullptr
            ? state->session.runtime_data()->find_animation(
                  state->selected_animation_name)
            : nullptr;
        const std::vector<TimelineTrackRow> current = animation != nullptr
            ? build_timeline_tracks(*state->session.runtime_data(), *animation)
            : std::vector<TimelineTrackRow>{};
        for (std::size_t index = 0U; index < gesture.keys.size(); ++index) {
            const TimelineTrackRow* track =
                find_timeline_track(current, gesture.keys[index].track_id);
            const auto key_index =
                track != nullptr ? timeline_key_index(*track, gesture.keys[index])
                                 : std::nullopt;
            const double expected = gesture.pivot_time +
                (gesture.original_times[index] - gesture.pivot_time) *
                    gesture.applied_scale;
            if (!key_index.has_value() ||
                std::abs(track->key_times[*key_index] - expected) >
                    marrow::editor::timeline_model::kKeyTimeEpsilon) {
                drifted = true;
                break;
            }
        }
        if (drifted) {
            completion = {
                marrow::editor::timeline_model::CompletionAction::Cancel, false, 0U};
        }
    }
    if (completion.action ==
        marrow::editor::timeline_model::CompletionAction::Cancel) {
        gesture.transaction.cancel();
        // The rollback restored the pre-gesture times, so the pre-gesture refs
        // resolve again; `keys` holds the last accepted frame's refs, which now
        // name times that no longer exist.
        state->timeline_editor.selected_keys = std::move(gesture.keys_before);
        state->timeline_editor.active_key = std::move(gesture.active_key_before);
        sync_shell_from_editor_session(state);
        if (drifted) {
            state->error_message = "Timeline scale drifted; the edit was discarded";
            state->status_message = "Timeline scale failed";
        } else if (completion.report_cancelled) {
            state->status_message = "Cancelled timeline scale";
        }
        return;
    }
    const marrow::editor::SessionResult result = gesture.transaction.commit();
    sync_shell_from_editor_session(state);
    if (!result) {
        state->error_message = result.error->format();
        state->status_message = "Timeline scale failed";
    } else {
        state->status_message = gesture.keys.size() == 1U
            ? "Scaled timeline key"
            : "Scaled timeline keys";
    }
}

std::string_view timeline_scale_rejection(const ShellState& state) {
    return state.timeline_editor.scale_gesture.has_value()
        ? std::string_view(state.timeline_editor.scale_gesture->rejection)
        : std::string_view{};
}

std::string timeline_scale_readout(const ShellState& state) {
    if (!state.timeline_editor.scale_gesture.has_value()) {
        return {};
    }
    const TimelineScaleGesture& gesture = *state.timeline_editor.scale_gesture;
    const double original_span =
        std::abs(gesture.edge_original_time - gesture.pivot_time);
    char line[192]{};
    std::snprintf(
        line,
        sizeof(line),
        "Scale %.3fx   span %.3fs -> %.3fs   (pivot %.3fs)",
        gesture.applied_scale,
        original_span,
        original_span * gesture.applied_scale,
        gesture.pivot_time);
    return std::string(line);
}

bool apply_timeline_scale_ratio(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    double requested_scale) {
    if (state == nullptr || !state->timeline_editor.scale_gesture.has_value()) {
        return false;
    }
    TimelineScaleGesture& gesture = *state->timeline_editor.scale_gesture;
    if (std::abs(requested_scale - gesture.applied_scale) <= 1e-12) return true;
    const auto incremental =
        marrow::editor::timeline_model::incremental_scale_ratio(
            requested_scale, gesture.applied_scale);
    if (!incremental.has_value()) {
        finish_timeline_scale_gesture(state, false);
        state->error_message = "Timeline scale ratio must be finite and positive.";
        state->status_message = "Timeline scale failed";
        return false;
    }

    if (!gesture.materialized) {
        for (const TimelineKeyRef& key : gesture.keys) {
            const TimelineTrackRow* track = find_timeline_track(tracks, key.track_id);
            if (track == nullptr ||
                !visit_editable_timeline_keys(state, *track, [](auto&) {})) {
                finish_timeline_scale_gesture(state, false);
                state->status_message = "Could not materialize the selected timeline keys";
                return false;
            }
        }
        gesture.materialized = true;
    }
    std::vector<std::size_t> resolved_indices;
    const auto selectors =
        scale_selectors(*state, gesture.keys, tracks, &resolved_indices);
    if (!selectors.has_value()) {
        finish_timeline_scale_gesture(state, false);
        state->status_message = "The selected timeline keys changed during scaling";
        return false;
    }

    const marrow::editor::TimelineScaleResult scaled =
        marrow::editor::scale_keyframe_times(
            gesture.transaction.project(),
            *selectors,
            gesture.pivot,
            *incremental);
    if (!scaled) {
        // The one deliberate divergence from the retime gesture: an illegal
        // frame holds the last accepted state instead of ending the drag.
        gesture.rejection = scaled.error;
        return true;
    }
    gesture.rejection.clear();
    if (!scaled.changed) return true;

    // MAR-171: every segment's span changed, so every automatic curve is stale.
    std::string auto_curve_error;
    if (!resolve_timeline_auto_curves(
            gesture.transaction.project(),
            state->selected_animation_name,
            &auto_curve_error)) {
        finish_timeline_scale_gesture(state, false);
        state->error_message = auto_curve_error;
        state->status_message = "Failed to update automatic curves: " + auto_curve_error;
        return false;
    }

    const marrow::editor::SessionResult refresh = gesture.transaction.refresh_runtime();
    if (!refresh) {
        const std::string error = refresh.error->format();
        finish_timeline_scale_gesture(state, false);
        state->error_message = error;
        state->status_message = "Timeline scale preview failed";
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
    // A positive ratio is strictly increasing and the primitive rejects any
    // projected inversion, so every key keeps its index within its timeline.
    for (std::size_t selection_index = 0U;
         selection_index < gesture.keys.size();
         ++selection_index) {
        const TimelineTrackRow* rebuilt_track =
            find_timeline_track(rebuilt_tracks, gesture.keys[selection_index].track_id);
        if (rebuilt_track == nullptr ||
            resolved_indices[selection_index] >= rebuilt_track->key_times.size()) {
            finish_timeline_scale_gesture(state, false);
            state->status_message = "Could not preserve timeline selection after scaling";
            return false;
        }
        rebuilt_selection.push_back(
            timeline_key_ref(*rebuilt_track, resolved_indices[selection_index]));
    }
    std::optional<TimelineKeyRef> rebuilt_active;
    if (state->timeline_editor.active_key.has_value()) {
        for (std::size_t selection_index = 0U;
             selection_index < gesture.keys.size();
             ++selection_index) {
            if (gesture.keys[selection_index] == *state->timeline_editor.active_key) {
                rebuilt_active = rebuilt_selection[selection_index];
                break;
            }
        }
    }
    gesture.keys = rebuilt_selection;
    state->timeline_editor.selected_keys = std::move(rebuilt_selection);
    state->timeline_editor.active_key = std::move(rebuilt_active);
    gesture.applied_scale = requested_scale;
    gesture.changed = true;
    return true;
}


namespace {

std::optional<marrow::editor::TimelineScalarComponent> to_scalar_component(
    timeline_graph_model::Component component) {
    switch (component) {
    case timeline_graph_model::Component::Angle:
        return marrow::editor::TimelineScalarComponent::Angle;
    case timeline_graph_model::Component::X:
        return marrow::editor::TimelineScalarComponent::X;
    case timeline_graph_model::Component::Y:
        return marrow::editor::TimelineScalarComponent::Y;
    case timeline_graph_model::Component::Red:
        return marrow::editor::TimelineScalarComponent::Red;
    case timeline_graph_model::Component::Green:
        return marrow::editor::TimelineScalarComponent::Green;
    case timeline_graph_model::Component::Blue:
        return marrow::editor::TimelineScalarComponent::Blue;
    case timeline_graph_model::Component::Alpha:
        return marrow::editor::TimelineScalarComponent::Alpha;
    }
    return std::nullopt;
}

} // namespace

bool timeline_graph_component_is_editable(
    const TimelineTrackRow& track,
    timeline_graph_model::Component component) {
    using Component = timeline_graph_model::Component;
    switch (track.kind) {
    case timeline_model::TimelineTrackKind::Rotate:
        return component == Component::Angle;
    case timeline_model::TimelineTrackKind::Translate:
    case timeline_model::TimelineTrackKind::Scale:
    case timeline_model::TimelineTrackKind::Shear:
        return component == Component::X || component == Component::Y;
    case timeline_model::TimelineTrackKind::SlotColor:
        return component == Component::Red || component == Component::Green ||
            component == Component::Blue || component == Component::Alpha;
    case timeline_model::TimelineTrackKind::Unknown:
    case timeline_model::TimelineTrackKind::Inherit:
    case timeline_model::TimelineTrackKind::SlotAttachment:
    case timeline_model::TimelineTrackKind::Deform:
    case timeline_model::TimelineTrackKind::DrawOrder:
    case timeline_model::TimelineTrackKind::Event:
        return false;
    }
    return false;
}

bool begin_timeline_graph_value_gesture(
    ShellState* state,
    std::uint32_t item_id,
    const TimelineTrackRow& track,
    timeline_graph_model::Component component,
    const std::vector<TimelineTrackRow>& tracks) {
    if (state == nullptr || authoring_gesture_active(*state) ||
        !timeline_track_is_editable(track) ||
        !timeline_graph_component_is_editable(track, component) ||
        state->timeline_editor.selected_keys.empty() ||
        find_timeline_track(tracks, track.id) == nullptr) {
        return false;
    }
    // The graph shows one track at a time, so only the focused row's keys join
    // the gesture. Selected keys on other rows are ignored and left untouched.
    std::vector<TimelineKeyRef> keys;
    std::size_t boundary_skip_count = 0U;
    for (const TimelineKeyRef& key : state->timeline_editor.selected_keys) {
        if (key.track_id != track.id) continue;
        // MAR-172: a managed boundary key's value is derived from key 0, so a
        // drag on it would be undone by the sync pass in the same transaction.
        const auto key_index = timeline_key_index(track, key);
        if (key_index.has_value() &&
            timeline_key_is_managed_loop_boundary(*state, track, *key_index)) {
            ++boundary_skip_count;
            continue;
        }
        keys.push_back(key);
    }
    if (keys.empty()) {
        if (boundary_skip_count != 0U) {
            state->status_message =
                "Edit the key at time 0 of this timeline instead; the last key is a "
                "managed loop boundary";
        }
        return false;
    }

    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        keys.size() == 1U ? "Edit graph key value" : "Edit graph key values",
        "timeline:graph-value",
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        state->error_message = transaction.error()->format();
        return false;
    }

    TimelineGraphValueGesture gesture;
    gesture.item_id = item_id;
    gesture.track_id = track.id;
    gesture.component = component;
    gesture.keys = std::move(keys);
    gesture.transaction = std::move(transaction);

    const auto& projection = cached_timeline_graph_projection(state, track);
    if (projection.status != timeline_graph_model::ProjectionStatus::Ready ||
        !projection.track.has_value()) {
        gesture.transaction.cancel();
        return false;
    }
    const auto component_slot = std::find_if(
        projection.track->components.begin(),
        projection.track->components.end(),
        [&](const timeline_graph_model::ComponentDescriptor& descriptor) {
            return descriptor.component == component;
        });
    if (component_slot == projection.track->components.end()) {
        gesture.transaction.cancel();
        return false;
    }
    const auto component_index = static_cast<std::size_t>(
        std::distance(projection.track->components.begin(), component_slot));
    for (const TimelineKeyRef& key : gesture.keys) {
        const auto key_index = timeline_key_index(track, key);
        const auto projected = std::find_if(
            projection.track->keys.begin(),
            projection.track->keys.end(),
            [&](const timeline_graph_model::Key& candidate) {
                return candidate.identity == key;
            });
        if (!key_index.has_value() || projected == projection.track->keys.end() ||
            component_index >= projected->value_count) {
            gesture.transaction.cancel();
            return false;
        }
    }
    state->timeline_editor.graph_value_gesture.emplace(std::move(gesture));
    return true;
}

void finish_timeline_graph_value_gesture(ShellState* state, bool commit) {
    if (state == nullptr || !state->timeline_editor.graph_value_gesture.has_value()) {
        return;
    }
    TimelineGraphValueGesture gesture =
        std::move(*state->timeline_editor.graph_value_gesture);
    state->timeline_editor.graph_value_gesture.reset();
    const auto completion =
        marrow::editor::timeline_model::completion_decision(
            commit, gesture.changed);
    if (completion.action ==
        marrow::editor::timeline_model::CompletionAction::Cancel) {
        gesture.transaction.cancel();
        sync_shell_from_editor_session(state);
        if (completion.report_cancelled) {
            state->status_message = "Cancelled graph value edit";
        }
        return;
    }
    const marrow::editor::SessionResult result = gesture.transaction.commit();
    sync_shell_from_editor_session(state);
    if (!result) {
        state->error_message = result.error->format();
        state->status_message = "Graph value edit failed";
    } else {
        state->status_message = gesture.keys.size() == 1U
            ? "Edited graph key value"
            : "Edited graph key values";
    }
}

bool apply_timeline_graph_value_delta(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    double requested_delta) {
    if (state == nullptr || !state->timeline_editor.graph_value_gesture.has_value()) {
        return false;
    }
    TimelineGraphValueGesture& gesture = *state->timeline_editor.graph_value_gesture;
    if (std::abs(requested_delta - gesture.applied_delta) <= 1e-12) return true;
    const auto incremental_delta =
        marrow::editor::timeline_model::incremental_retime_delta(
            requested_delta, gesture.applied_delta);
    if (!incremental_delta.has_value()) {
        finish_timeline_graph_value_gesture(state, false);
        state->error_message = "Graph value delta must be finite.";
        state->status_message = "Graph value edit failed";
        return false;
    }
    const auto scalar_component = to_scalar_component(gesture.component);
    const TimelineTrackRow* track = find_timeline_track(tracks, gesture.track_id);
    if (track == nullptr || !scalar_component.has_value() ||
        !timeline_track_is_editable(*track) ||
        !timeline_graph_component_is_editable(*track, gesture.component)) {
        finish_timeline_graph_value_gesture(state, false);
        state->status_message = "The graph editing context changed during editing";
        return false;
    }

    // The first edit copies every imported runtime key into the project, so a
    // value offset never replaces an unmaterialized track with one key.
    if (!gesture.materialized) {
        if (!visit_editable_timeline_keys(state, *track, [](auto&) {})) {
            finish_timeline_graph_value_gesture(state, false);
            state->status_message = "Could not materialize the selected graph keys";
            return false;
        }
        gesture.materialized = true;
    }

    std::vector<marrow::editor::TimelineKeySelector> selectors;
    selectors.reserve(gesture.keys.size());
    for (const TimelineKeyRef& key : gesture.keys) {
        const TimelineTrackRow* resolved_track =
            find_timeline_track(tracks, gesture.track_id);
        const auto key_index = resolved_track != nullptr
            ? timeline_key_index(*resolved_track, key)
            : std::nullopt;
        if (!key_index.has_value()) {
            finish_timeline_graph_value_gesture(state, false);
            state->status_message = "The selected graph keys changed during editing";
            return false;
        }
        const auto selector =
            timeline_key_selector(*state, *resolved_track, *key_index);
        if (!selector.has_value()) {
            finish_timeline_graph_value_gesture(state, false);
            state->status_message = "Could not resolve the selected graph keys";
            return false;
        }
        selectors.push_back(*selector);
    }

    const marrow::editor::TimelineScalarOffsetResult offset =
        marrow::editor::offset_keyframe_scalars(
            gesture.transaction.project(),
            selectors,
            *scalar_component,
            *incremental_delta);
    if (!offset) {
        const std::string error = offset.error;
        finish_timeline_graph_value_gesture(state, false);
        state->error_message = error;
        state->status_message = "Graph value edit failed";
        return false;
    }
    if (!offset.changed) return true;

    // MAR-171: a driver value moved, so every automatic curve that reads it is
    // recomputed inside this same gesture transaction.
    std::string auto_curve_error;
    if (!resolve_timeline_auto_curves(
            gesture.transaction.project(),
            state->selected_animation_name,
            &auto_curve_error)) {
        finish_timeline_graph_value_gesture(state, false);
        state->error_message = auto_curve_error;
        state->status_message = "Failed to update automatic curves: " + auto_curve_error;
        return false;
    }

    const marrow::editor::SessionResult refresh = gesture.transaction.refresh_runtime();
    if (!refresh) {
        const std::string error = refresh.error->format();
        finish_timeline_graph_value_gesture(state, false);
        state->error_message = error;
        state->status_message = "Graph value edit preview failed";
        return false;
    }
    sync_shell_from_editor_session(state);
    // A value edit never moves a key in time, so every TimelineKeyRef stays
    // bit-identical and selection/active_key need no rebuild.
    gesture.applied_delta += offset.applied_delta;
    // `changed` is the net state, not "some frame mutated something": a drag
    // that travels and comes back must complete as a cancel so it neither
    // claims an edit it did not make nor relies on the session's
    // unchanged-snapshot backstop to suppress a history entry.
    gesture.changed = std::abs(gesture.applied_delta) > 1e-12;
    return true;
}

namespace {

/** @brief Reports whether a frozen segment frame is usable for a drag. */
bool segment_frame_is_finite(const timeline_graph_model::SegmentFrame& frame) {
    return std::isfinite(frame.start_time_seconds) &&
        std::isfinite(frame.end_time_seconds) &&
        std::isfinite(frame.start_value) && std::isfinite(frame.end_value) &&
        std::isfinite(frame.time_span) &&
        frame.time_span > timeline_graph_model::kMinimumSegmentSeconds &&
        std::isfinite(frame.value_span) && frame.value_span != 0.0;
}

bool control_points_are_finite(const std::array<double, 4>& control_points) {
    for (const double control_point : control_points) {
        if (!std::isfinite(control_point)) return false;
    }
    return true;
}

bool same_control_points(
    const std::array<double, 4>& left,
    const std::array<double, 4>& right) {
    for (std::size_t index = 0U; index < left.size(); ++index) {
        if (std::abs(left[index] - right[index]) > 1e-12) return false;
    }
    return true;
}

/**
 * @brief The recorded curve mode of one pressed graph key.
 *
 * A key on an unmaterialized runtime-only track has never been authored, so it
 * is `Manual`, which is exactly what MAR-169's net-state `changed` rule wants
 * for it.
 */
marrow::editor::TimelineCurveMode pressed_key_curve_mode(
    const ShellState& state,
    const TimelineTrackRow& track,
    const TimelineKeyRef& key) {
    if (!state.load_result || state.load_result.project == nullptr) {
        return marrow::editor::TimelineCurveMode::Manual;
    }
    const auto key_index = timeline_key_index(track, key);
    if (!key_index.has_value()) return marrow::editor::TimelineCurveMode::Manual;
    const auto selector = timeline_key_selector(state, track, *key_index);
    if (!selector.has_value()) return marrow::editor::TimelineCurveMode::Manual;
    if (selector->kind == marrow::editor::TimelineKeyKind::Transform) {
        const auto* edit = state.load_result.project->find_transform_timeline_edit(
            selector->animation_name, selector->bone_name, selector->transform_channel);
        if (edit != nullptr) {
            for (const auto& keyframe : edit->keyframes) {
                if (std::abs(keyframe.time - selector->time) <= 1e-6) {
                    return keyframe.curve_mode;
                }
            }
        }
    } else if (selector->kind == marrow::editor::TimelineKeyKind::SlotColor) {
        const auto* edit = state.load_result.project->find_slot_color_timeline_edit(
            selector->animation_name, selector->slot_name);
        if (edit != nullptr) {
            for (const auto& keyframe : edit->keyframes) {
                if (std::abs(keyframe.time - selector->time) <= 1e-6) {
                    return keyframe.curve_mode;
                }
            }
        }
    }
    return marrow::editor::TimelineCurveMode::Manual;
}

} // namespace

bool begin_timeline_graph_handle_gesture(
    ShellState* state,
    std::uint32_t item_id,
    const TimelineTrackRow& track,
    const TimelineKeyRef& key,
    const timeline_graph_model::SegmentFrame& frame,
    const std::array<double, 4>& seed_control_points,
    marrow::runtime::InterpolationKind original_kind,
    const std::vector<TimelineTrackRow>& tracks) {
    if (state == nullptr || authoring_gesture_active(*state) ||
        !timeline_track_is_editable(track) ||
        find_timeline_track(tracks, track.id) == nullptr ||
        !segment_frame_is_finite(frame) ||
        !control_points_are_finite(seed_control_points)) {
        return false;
    }
    const auto key_index = timeline_key_index(track, key);
    // The last key owns no outgoing segment, so it anchors no handles.
    if (!key_index.has_value() || *key_index + 1U >= track.key_times.size()) {
        return false;
    }

    const auto& projection = cached_timeline_graph_projection(state, track);
    if (projection.status != timeline_graph_model::ProjectionStatus::Ready ||
        !projection.track.has_value()) {
        return false;
    }
    const auto projected = std::find_if(
        projection.track->keys.begin(),
        projection.track->keys.end(),
        [&](const timeline_graph_model::Key& candidate) {
            return candidate.identity == key;
        });
    if (projected == projection.track->keys.end()) return false;

    // No component is captured at all: the primitive writes the one shared
    // easing of the parent key and has no component argument to write through.
    if (projection.track->components.empty()) return false;

    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "Edit key easing",
        "timeline:graph-easing",
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        state->error_message = transaction.error()->format();
        return false;
    }

    TimelineGraphHandleGesture gesture;
    gesture.item_id = item_id;
    gesture.track_id = track.id;
    gesture.key = key;
    gesture.frame = frame;
    gesture.original_kind = original_kind;
    gesture.original_control_points = seed_control_points;
    gesture.applied_control_points = seed_control_points;
    gesture.original_mode = pressed_key_curve_mode(*state, track, key);
    gesture.transaction = std::move(transaction);
    state->timeline_editor.graph_handle_gesture.emplace(std::move(gesture));
    return true;
}

void finish_timeline_graph_handle_gesture(ShellState* state, bool commit) {
    if (state == nullptr || !state->timeline_editor.graph_handle_gesture.has_value()) {
        return;
    }
    TimelineGraphHandleGesture gesture =
        std::move(*state->timeline_editor.graph_handle_gesture);
    state->timeline_editor.graph_handle_gesture.reset();
    const auto completion =
        marrow::editor::timeline_model::completion_decision(commit, gesture.changed);
    if (completion.action ==
        marrow::editor::timeline_model::CompletionAction::Cancel) {
        gesture.transaction.cancel();
        sync_shell_from_editor_session(state);
        if (completion.report_cancelled) {
            state->status_message = "Cancelled easing edit";
        }
        return;
    }
    const marrow::editor::SessionResult result = gesture.transaction.commit();
    sync_shell_from_editor_session(state);
    if (!result) {
        state->error_message = result.error->format();
        state->status_message = "Easing edit failed";
    } else {
        state->status_message = "Edited key easing";
    }
}

bool apply_timeline_graph_handle_control_points(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    const std::array<double, 4>& requested_control_points) {
    if (state == nullptr || !state->timeline_editor.graph_handle_gesture.has_value()) {
        return false;
    }
    TimelineGraphHandleGesture& gesture =
        *state->timeline_editor.graph_handle_gesture;
    if (!control_points_are_finite(requested_control_points)) {
        finish_timeline_graph_handle_gesture(state, false);
        state->error_message = "Bezier control points must be finite.";
        state->status_message = "Easing edit failed";
        return false;
    }
    if (gesture.changed &&
        same_control_points(requested_control_points, gesture.applied_control_points)) {
        return true;
    }

    const TimelineTrackRow* track = find_timeline_track(tracks, gesture.track_id);
    if (track == nullptr || !timeline_track_is_editable(*track)) {
        finish_timeline_graph_handle_gesture(state, false);
        state->status_message = "The graph editing context changed during editing";
        return false;
    }

    // The first edit copies every imported runtime key into the project, so an
    // easing edit never replaces an unmaterialized track with one key.
    if (!gesture.materialized) {
        if (!visit_editable_timeline_keys(state, *track, [](auto&) {})) {
            finish_timeline_graph_handle_gesture(state, false);
            state->status_message = "Could not materialize the selected timeline key";
            return false;
        }
        gesture.materialized = true;
    }

    const TimelineTrackRow* resolved_track =
        find_timeline_track(tracks, gesture.track_id);
    const auto key_index = resolved_track != nullptr
        ? timeline_key_index(*resolved_track, gesture.key)
        : std::nullopt;
    if (!key_index.has_value()) {
        finish_timeline_graph_handle_gesture(state, false);
        state->status_message = "The selected graph key changed during editing";
        return false;
    }
    const auto selector =
        timeline_key_selector(*state, *resolved_track, *key_index);
    if (!selector.has_value()) {
        finish_timeline_graph_handle_gesture(state, false);
        state->status_message = "Could not resolve the selected graph key";
        return false;
    }

    const marrow::editor::TimelineInterpolationResult result =
        marrow::editor::set_keyframe_interpolation(
            gesture.transaction.project(),
            {*selector},
            marrow::runtime::InterpolationKind::CubicBezier,
            requested_control_points);
    if (!result) {
        const std::string error = result.error;
        finish_timeline_graph_handle_gesture(state, false);
        state->error_message = error;
        state->status_message = "Easing edit failed";
        return false;
    }
    if (!result.changed) {
        // A no-change frame is not a failure: the authored value is already
        // exactly what was requested.
        gesture.applied_control_points = requested_control_points;
        return true;
    }

    const marrow::editor::SessionResult refresh = gesture.transaction.refresh_runtime();
    if (!refresh) {
        const std::string error = refresh.error->format();
        finish_timeline_graph_handle_gesture(state, false);
        state->error_message = error;
        state->status_message = "Easing edit preview failed";
        return false;
    }
    sync_shell_from_editor_session(state);
    // An easing edit never moves a key in time, so every TimelineKeyRef stays
    // bit-identical and selection/active_key need no rebuild.
    gesture.applied_control_points = requested_control_points;
    // `changed` is the net state against the ORIGINAL authored easing, not
    // "some frame mutated something". A drag that travels and returns to the
    // stored Cubic points completes as a cancel; a drag that returns to the
    // seed of a Linear or Stepped segment still counts as changed, because the
    // authored kind genuinely became Cubic.
    // MAR-171 extends the rule: a drag on an AUTOMATIC key is an authored
    // change even when the points land back on their starting values, because
    // the key stops tracking its neighbours. See the design §9.4.
    gesture.changed =
        gesture.original_kind != marrow::runtime::InterpolationKind::CubicBezier ||
        gesture.original_mode == marrow::editor::TimelineCurveMode::Auto ||
        !same_control_points(
            requested_control_points, gesture.original_control_points);
    return true;
}

} // namespace marrow::editor::shell
