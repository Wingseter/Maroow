#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "shell_state.hpp"

namespace marrow::editor::shell {

struct TimelineGraphRenderStats {
    timeline_graph_model::ProjectionStatus status{
        timeline_graph_model::ProjectionStatus::UnsupportedTrack};
    std::size_t point_count{0U};
    std::size_t linear_segment_count{0U};
    std::size_t stepped_segment_count{0U};
    std::size_t cubic_segment_count{0U};
    std::size_t zero_value_tick_label_count{0U};
    std::size_t one_value_tick_label_count{0U};
    bool playhead_drawn{false};
    float first_component_min_x{0.0f};
    float first_component_min_y{0.0f};
    float first_component_max_x{0.0f};
    float first_component_max_y{0.0f};
    float fit_min_x{0.0f};
    float fit_min_y{0.0f};
    float fit_max_x{0.0f};
    float fit_max_y{0.0f};
    std::uint32_t plot_item_id{0U};
    float plot_min_x{0.0f};
    float plot_min_y{0.0f};
    float plot_max_x{0.0f};
    float plot_max_y{0.0f};
    bool plot_item_hovered{false};
    bool plot_hovered{false};
    // Submitted-point coordinates so an actual-frame smoke can aim a real
    // mouse at a real point. Populated only from geometry that reached the
    // draw list.
    float active_point_x{0.0f};
    float active_point_y{0.0f};
    bool active_point_valid{false};
    float first_point_x{0.0f};
    float first_point_y{0.0f};
    bool first_point_valid{false};
    // True from the press until release, across both the undecided phase and
    // the locked-axis phase.
    bool drag_active{false};
    bool value_gesture_active{false};
    // A retime is live AND owned by this graph drag, not by the dopesheet.
    bool graph_owns_retime{false};
    timeline_graph_model::DragAxis drag_axis{
        timeline_graph_model::DragAxis::Undecided};
    // MAR-169 handle overlay. Coordinates are the submitted handle centres, so
    // an actual-frame smoke can aim a real mouse at a real handle.
    bool handles_drawn{false};
    float first_handle_x{0.0f};
    float first_handle_y{0.0f};
    float second_handle_x{0.0f};
    float second_handle_y{0.0f};
    bool handle_flat_value_span{false};
    bool handle_gesture_active{false};
    // True while any graph candidate or gesture is live, which is exactly when
    // the component checkboxes and Fit are wrapped in BeginDisabled.
    bool component_controls_disabled{false};
    timeline_graph_model::SegmentKind active_segment_kind{
        timeline_graph_model::SegmentKind::Linear};
    // MAR-170 preset row. Appended, never reordered, so MAR-167/168/169's
    // positional expectations are unaffected. The first button's rectangle lets
    // an actual-frame smoke aim a real mouse at a real button.
    bool curve_preset_row_drawn{false};
    bool curve_preset_row_enabled{false};
    float first_preset_min_x{0.0f};
    float first_preset_min_y{0.0f};
    float first_preset_max_x{0.0f};
    float first_preset_max_y{0.0f};
    // A custom curve, or an active key with no outgoing segment, is reported as
    // kCurvePresets.size().
    std::size_t active_preset_index{0U};
    std::size_t default_preset_index{0U};
};

/**
 * @brief Draws the six fixed preset buttons and the `Default:` combo.
 *
 * Shared by the Graph and Dopesheet toolbars: both read the same
 * `TimelineEditorState::selected_keys`, so there is exactly one selection
 * semantics and one place the row's behaviour lives. The row performs no
 * arithmetic and no mutation of its own — a button click calls
 * `apply_timeline_curve_preset()` and a combo change calls
 * `set_shell_default_curve()`. `stats` may be null.
 */
void draw_timeline_curve_preset_row(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    TimelineGraphRenderStats* stats);

const TimelineTrackRow* resolve_timeline_graph_track(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks);

const timeline_graph_model::Projection& cached_timeline_graph_projection(
    ShellState* state,
    const TimelineTrackRow& track);

bool activate_timeline_graph_point(
    ShellState* state,
    const TimelineTrackRow& track,
    const timeline_graph_model::PointHit& point,
    bool additive,
    std::string_view source);

/**
 * @brief Arms a graph point drag candidate on a left press.
 *
 * A candidate owns no transaction, so `authoring_gesture_active` stays false
 * until the pointer leaves the dead zone and one axis is locked. Fails closed
 * on a non-editable or unprojectable row, a key outside the current selection,
 * another live authoring gesture, and non-finite pointer or view input.
 */
bool begin_timeline_graph_point_drag(
    ShellState* state,
    const TimelineTrackRow& track,
    const timeline_graph_model::PointHit& point,
    std::uint32_t item_id,
    timeline_graph_model::PlotRect plot,
    const timeline_graph_model::View& view,
    double pointer_x,
    double pointer_y);

/**
 * @brief Arms a graph handle drag candidate on a left press.
 *
 * Shares MAR-168's candidate slot and dead zone, so a press owns no
 * transaction until the pointer leaves the zone. Fails closed on a
 * non-editable or unprojectable row, a geometry whose key is no longer the
 * active key, another live authoring gesture, and non-finite pointer, view,
 * plot, or frame input. A handle press deliberately does not activate the
 * point under it: scrubbing the playhead or changing the selection mid-grab
 * would move the very anchors the frozen frame depends on.
 */
bool begin_timeline_graph_handle_drag(
    ShellState* state,
    const TimelineTrackRow& track,
    const timeline_graph_model::HandleGeometry& handles,
    timeline_graph_model::HandleIndex handle,
    std::uint32_t item_id,
    timeline_graph_model::PlotRect plot,
    const timeline_graph_model::View& view,
    double pointer_x,
    double pointer_y);

/**
 * @brief Advances or terminates the live graph drag from sampled input.
 * @return true while a candidate or gesture remains live.
 */
bool update_timeline_graph_point_drag(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    double pointer_x,
    double pointer_y,
    bool pointer_down,
    bool cancel_requested,
    bool bypass_frame_snap);

void cancel_timeline_graph_point_drag(ShellState* state);

/**
 * @brief Samples ImGui input once per frame and advances any live graph drag.
 *
 * This runs from `draw_timeline_window` above every Graph-body early return,
 * so a release, Escape, or invalidated context can always end the gesture and
 * release its transaction.
 */
void poll_timeline_graph_point_drag(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);

TimelineGraphRenderStats draw_timeline_graph_body(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);

} // namespace marrow::editor::shell
