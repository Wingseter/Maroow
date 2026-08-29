#pragma once

#include <array>
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
bool activate_timeline_key(
    ShellState* state,
    const TimelineTrackRow& track,
    std::size_t key_index,
    bool additive,
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

/**
 * @brief Opens one live transaction that offsets a single graph component.
 *
 * The gesture edits `component` on every selected key that belongs to `track`,
 * leaving key times, every other component, and the shared outgoing easing
 * untouched. Fails closed on a non-editable row, an unsupported (row,
 * component) pairing, an empty selection, or another live authoring gesture.
 */
bool begin_timeline_graph_value_gesture(
    ShellState* state,
    std::uint32_t item_id,
    const TimelineTrackRow& track,
    timeline_graph_model::Component component,
    const std::vector<TimelineTrackRow>& tracks);
/**
 * @brief Applies one absolute value delta, cancelling atomically on failure.
 * @return false when the gesture ended; the gesture is already gone.
 */
bool apply_timeline_graph_value_delta(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    double requested_delta);
void finish_timeline_graph_value_gesture(ShellState* state, bool commit);

/**
 * @brief Opens one live transaction that authors the active key's easing.
 *
 * The easing belongs to the whole parent key and is shared by every component
 * of that key, so the gesture carries no component at all: there is no argument
 * through which a component could influence which bytes are written. The
 * displayed component cannot change under a live drag because the visibility
 * controls are disabled for as long as one is live. Fails closed on a null
 * state, another live authoring gesture, a non-editable or unprojectable row,
 * a key that is not on `track`, the track's last key, and a non-finite frame.
 */
bool begin_timeline_graph_handle_gesture(
    ShellState* state,
    std::uint32_t item_id,
    const TimelineTrackRow& track,
    const TimelineKeyRef& key,
    const timeline_graph_model::SegmentFrame& frame,
    const std::array<double, 4>& seed_control_points,
    marrow::runtime::InterpolationKind original_kind,
    const std::vector<TimelineTrackRow>& tracks);
/**
 * @brief Applies one absolute control-point set, cancelling atomically on
 *        failure.
 * @return false when the gesture ended; the gesture is already gone.
 */
bool apply_timeline_graph_handle_control_points(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    const std::array<double, 4>& requested_control_points);
void finish_timeline_graph_handle_gesture(ShellState* state, bool commit);

/** @brief Reports whether `component` is authorable on `track`. */
bool timeline_graph_component_is_editable(
    const TimelineTrackRow& track,
    timeline_graph_model::Component component);

} // namespace marrow::editor::shell
