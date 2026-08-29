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
void draw_timeline_window(
    ShellState* state,
    TimelineGraphRenderStats* graph_stats_out = nullptr);

} // namespace marrow::editor::shell
