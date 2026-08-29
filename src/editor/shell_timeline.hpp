#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "imgui.h"

#include "timeline_controller.hpp"
#include "timeline_model.hpp"
#include "shell_timeline_graph.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/runtime/animation.hpp"

namespace marrow::runtime {
class Skeleton;
}

namespace marrow::editor::shell {

struct ShellState;
using marrow::editor::timeline_model::insertable_key_time;
void draw_draw_order_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track);
void draw_event_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track);
void draw_mesh_deform_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track);
void draw_slot_color_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track);
void draw_slot_attachment_timeline_editor(
    ShellState* state,
    const TimelineTrackRow& track);
void draw_transform_timeline_editor(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);
/**
 * @brief Arms one dopesheet scale drag on a selection-range grip.
 *
 * Holds no transaction: the gesture opens only when the pointer leaves the
 * shared 4.0 px dead zone, so a click on a grip can never block an unrelated
 * authoring path. Every view scalar is frozen here and never re-read.
 * Refuses on a partial event tie or a loop-pinned key, reporting through
 * `state->status_message`.
 */
bool begin_timeline_scale_drag(
    ShellState* state,
    std::uint32_t item_id,
    marrow::editor::TimelineScalePivot pivot,
    double pointer_x,
    double lane_min_x,
    double pixels_per_second,
    double view_start_seconds,
    const std::vector<TimelineTrackRow>& tracks);

/**
 * @brief Advances or terminates the live scale drag from sampled input.
 * @return true while a candidate or gesture remains live.
 */
bool update_timeline_scale_drag(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    double pointer_x,
    bool pointer_down,
    bool cancel_requested,
    bool bypass_frame_snap);

void cancel_timeline_scale_drag(ShellState* state);

void draw_timeline_window(
    ShellState* state,
    TimelineGraphRenderStats* graph_stats_out = nullptr);

} // namespace marrow::editor::shell
