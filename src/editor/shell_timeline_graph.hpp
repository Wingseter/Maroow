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

TimelineGraphRenderStats draw_timeline_graph_body(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);

} // namespace marrow::editor::shell
