#pragma once

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
    bool drag_candidate_active{false};
    bool value_gesture_active{false};
    bool retime_gesture_active{false};
    timeline_graph_model::DragAxis drag_axis{
        timeline_graph_model::DragAxis::Undecided};
};

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

TimelineGraphRenderStats draw_timeline_graph_body(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);

} // namespace marrow::editor::shell
