#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "shell_state.hpp"
#include "timeline_model.hpp"
#include "marrow/editor/authoring.hpp"

namespace marrow::editor::shell {

std::optional<std::size_t> draw_order_position(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t slot_index);
std::string format_time_seconds(double time_seconds);
std::vector<double> collect_animation_key_times(
    const marrow::runtime::AnimationData& animation);
std::vector<TimelineTrackRow> build_timeline_tracks(
    const marrow::runtime::SkeletonData& skeleton,
    const marrow::runtime::AnimationData& animation);
TimelineKeyRef timeline_key_ref(
    const TimelineTrackRow& track,
    std::size_t key_index);
std::optional<std::size_t> timeline_key_index(
    const TimelineTrackRow& track,
    const TimelineKeyRef& key);
void reconcile_timeline_key_selection(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);
const TimelineTrackRow* selected_timeline_track(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks);
bool timeline_track_matches_selection(
    const ShellState& state,
    const TimelineTrackRow& track);
const TimelineTrackRow* find_timeline_track(
    const std::vector<TimelineTrackRow>& tracks,
    std::string_view track_id);
bool timeline_track_is_editable(const TimelineTrackRow& track);
bool timeline_key_selected(
    const ShellState& state,
    const TimelineKeyRef& key);
std::optional<marrow::editor::TimelineKeySelector> timeline_key_selector(
    const ShellState& state,
    const TimelineTrackRow& track,
    std::size_t key_index);
std::string_view transform_channel_label(
    marrow::editor::TransformTimelineChannel channel);
std::optional<marrow::editor::TransformTimelineEdit> make_transform_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track);
std::optional<marrow::editor::MeshDeformTimelineEdit> make_mesh_deform_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track);
std::optional<marrow::editor::DrawOrderTimelineEdit> make_draw_order_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track);
std::optional<marrow::editor::EventTimelineEdit> make_event_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track);
std::optional<marrow::editor::SlotColorTimelineEdit> make_slot_color_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track);
std::optional<marrow::editor::SlotAttachmentTimelineEdit>
make_slot_attachment_timeline_edit(
    const ShellState& state,
    const TimelineTrackRow& track);
marrow::editor::DrawOrderKeyframeEdit sample_draw_order_keyframe(
    const ShellState& state);
marrow::editor::EventKeyframeEdit sample_event_keyframe(
    const ShellState& state);
marrow::editor::DeformKeyframeEdit sample_deform_keyframe(
    const ShellState& state,
    const TimelineTrackRow& track);

bool set_selected_animation(
    ShellState* state,
    std::string_view animation_name,
    std::string_view source,
    bool update_status_message,
    bool reset_time);
bool scrub_timeline_time(
    ShellState* state,
    double time_seconds,
    std::string_view source,
    bool update_status_message);
void advance_timeline_playback(ShellState* state, double delta_seconds);
void advance_timeline_playback(ShellState* state, float delta_seconds);
bool focus_timeline_track(
    ShellState* state,
    const TimelineTrackRow& track,
    double time_seconds,
    std::string_view source,
    bool update_status_message);

std::optional<std::size_t> ensure_transform_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track);
std::optional<std::size_t> ensure_mesh_deform_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track);
std::optional<std::size_t> ensure_draw_order_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track);
std::optional<std::size_t> ensure_event_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track);
std::optional<std::size_t> ensure_slot_color_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track);
std::optional<std::size_t> ensure_slot_attachment_timeline_edit_index(
    ShellState* state,
    const TimelineTrackRow& track);
marrow::editor::TransformKeyframeEdit sample_transform_keyframe(
    const ShellState& state,
    const TimelineTrackRow& track);

bool add_timeline_key_at_playhead(
    ShellState* state,
    const TimelineTrackRow& track);
bool remove_selected_timeline_keys(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);
bool copy_selected_timeline_keys(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);
bool cut_selected_timeline_keys(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);
bool paste_timeline_clipboard(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);
bool begin_timeline_retime_gesture(
    ShellState* state,
    std::uint32_t item_id,
    float start_mouse_x,
    const std::vector<TimelineTrackRow>& tracks);
bool apply_timeline_retime_delta(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    double requested_delta,
    bool snap_to_frames);
void finish_timeline_retime_gesture(ShellState* state, bool commit);

} // namespace marrow::editor::shell
