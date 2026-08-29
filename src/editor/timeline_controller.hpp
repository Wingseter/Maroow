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

/**
 * @brief Reports whether one key of one lane is a managed loop boundary.
 *
 * True only when the lane is loop synchronized and `key_index` names its last
 * key, which is exactly the contract's definition of the managed boundary. Used
 * by every selection collector so a derived key is never offered for direct
 * value or easing authoring, and by the removal filter so it cannot be deleted
 * out from under its own contract.
 */
bool timeline_key_is_managed_loop_boundary(
    const ShellState& state,
    const TimelineTrackRow& track,
    std::size_t key_index);
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

/** @brief Result of one preset application, for status text and tests. */
struct TimelineCurvePresetResult {
    bool applied{false};
    std::size_t changed_key_count{0U};
    std::size_t compatible_key_count{0U};
    std::size_t skipped_key_count{0U};  // selected keys with no easing field
    std::string error;
};

/**
 * @brief How many selected keys the preset row would actually write.
 *
 * Shares one definition of "compatible selected key" with
 * `apply_timeline_curve_preset()`, so the row can never be enabled for a
 * selection the write would refuse. Easing-free lanes and duplicate refs are
 * excluded exactly as they are on the write path.
 */
std::size_t compatible_curve_preset_key_count(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks);

/**
 * @brief Applies one fixed preset to every compatible selected key.
 *
 * Draw-order, event, and slot-attachment selections are skipped rather than
 * rejected, duplicates are collapsed, runtime-only tracks are materialized, and
 * the whole write is one transaction with live preview and one history entry
 * whatever the key count. A selection with no compatible key, or a selection
 * already carrying the preset, leaves the project and the history untouched.
 * Applying a preset never changes the remembered default curve.
 */
TimelineCurvePresetResult apply_timeline_curve_preset(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    marrow::editor::CurvePreset preset);

/**
 * @brief The preset the active key's outgoing easing is, or nullopt for custom.
 *
 * Recomputed from the stored curve on every call, so it needs no invalidation
 * and reports `Custom` the moment a handle drag moves away from a preset.
 */
std::optional<marrow::editor::CurvePreset> active_outgoing_curve_preset(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks);

/** @brief Result of one curve-mode application, for status text and tests. */
struct TimelineCurveModeApplyResult {
    bool applied{false};
    std::size_t changed_key_count{0U};
    std::size_t resolved_key_count{0U};
    std::size_t compatible_key_count{0U};
    std::size_t skipped_key_count{0U};  // selected keys with no curve mode
    std::string error;
};

/**
 * @brief How many selected keys the curve-mode row would actually write.
 *
 * Shares one definition of "compatible selected key" with
 * `apply_timeline_curve_mode()`, so the row can never be enabled for a
 * selection the write would refuse. Deform is compatible with MAR-170's preset
 * row but NOT with this one: a deform key's value is a vertex-offset vector
 * with no canonical scalar to drive a tangent.
 */
std::size_t compatible_curve_mode_key_count(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks);

/**
 * @brief Applies one curve mode to every compatible selected key.
 *
 * Deform, draw-order, event, and slot-attachment selections are skipped rather
 * than rejected, duplicates are collapsed, runtime-only tracks are materialized,
 * and the whole write plus its automatic resolution is one transaction with
 * live preview and one history entry.
 */
TimelineCurveModeApplyResult apply_timeline_curve_mode(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    marrow::editor::TimelineCurveMode mode,
    std::optional<marrow::editor::TimelineScalarComponent> driver);

/**
 * @brief Resolves the animation's automatic curves inside the caller's open
 *        transaction.
 *
 * Every timeline transaction that can change a Transform or Slot Color key's
 * time, value, or existence calls this after its own mutation and before
 * `refresh_runtime()`, so one edit stays one history entry.
 * @return false with `*error_out` set when the resolve failed; the caller
 *         cancels.
 */
bool resolve_timeline_auto_curves(
    marrow::editor::ProjectData* project,
    std::string_view animation_name,
    std::string* error_out);

/** @brief The active key's recorded curve mode and effective driver. */
std::optional<marrow::editor::TimelineCurveMode> active_outgoing_curve_mode(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks);
std::optional<marrow::editor::TimelineScalarComponent> active_outgoing_curve_driver(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks);

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
 * @brief Opens one live transaction that scales the current key selection.
 *
 * The pivot names which edge of the selection's own time range stays fixed;
 * the primitive recomputes it from the resolved times every frame, so the
 * pivot key is bit-identical throughout. Fails closed on a null state, another
 * live authoring gesture, an empty or single-time selection, a non-editable or
 * unresolvable track, a partial event tie, and a loop-synchronization pinned
 * key. The last two are reported through `state->status_message`.
 */
bool begin_timeline_scale_gesture(
    ShellState* state,
    std::uint32_t item_id,
    marrow::editor::TimelineScalePivot pivot,
    const std::vector<TimelineTrackRow>& tracks);

/**
 * @brief Applies one absolute ratio, holding the last accepted state on a
 *        rejection and cancelling atomically on a structural failure.
 *
 * A `scale_keyframe_times()` rejection deliberately keeps the gesture alive:
 * dragging inward past a collision and back out again is ordinary, and killing
 * the drag there would lose the edit. Every other failure — materialization, a
 * lost key identity, the automatic-curve resolve, the runtime refresh — cancels
 * the whole transaction.
 *
 * @return false when the gesture ended; the gesture is already gone.
 */
bool apply_timeline_scale_ratio(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    double requested_scale);

void finish_timeline_scale_gesture(ShellState* state, bool commit);

/** @brief The last rejected frame's reason, or empty while the scale is legal. */
std::string_view timeline_scale_rejection(const ShellState& state);

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
