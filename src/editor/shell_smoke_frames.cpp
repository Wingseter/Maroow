#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "shell_frame.hpp"
#include "shell_constraints.hpp"
#include "shell_asset_watch.hpp"
#include "shell_agent_panel.hpp"
#include "shell_coalesced_edit.hpp"
#include "shell_derived_cache.hpp"
#include "shell_inspector.hpp"
#include "shell_project_panels.hpp"
#include "shell_parameters.hpp"
#include "shell_smoke_scenarios.hpp"
#include "shell_preview.hpp"
#include "mesh_weight_model.hpp"
#include "marrow/editor/problems_model.hpp"
#include "shell_problems.hpp"
#include "shell_selection.hpp"
#include "shell_timeline.hpp"
#include "shell_timeline_graph.hpp"
#include "shell_weight_paint.hpp"
#include "shell_viewport_ui.hpp"
#include "shell_file_paths.hpp"
#include "shell_state.hpp"
#include "viewport_renderer.hpp"
#include "marrow/allocator.hpp"
#include "marrow/editor/module.hpp"
#include "marrow/editor/authoring.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/renderer/module.hpp"
#include "marrow/runtime/animation_state.hpp"
#include "marrow/runtime/profiler.hpp"

namespace marrow::editor::shell {

bool render_headless_smoke_frames(
    ShellState& shell_state,
    const Options& options,
    ImGuiIO& io) {
    apply_shell_mode(&shell_state, ShellMode::Parameter);
    // The shared frame must honor the same optional-window condition as the app.
    shell_state.show_agent_panel = true;
    shell_state.agent_panel_was_open = true;
    const int frame_count = options.auto_close_frames.value_or(1);
    bool validated_dock_layout = false;
    for (int frame_index = 0; frame_index < frame_count; ++frame_index) {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        draw_shell_frame(shell_state, io.DeltaTime);

        const ImGuiWindow* agent_window = ImGui::FindWindowByName(kAgentWindowTitle);
        if (agent_window == nullptr || !agent_window->Active) {
            std::cerr << "Shared frame did not draw the enabled Agent panel.\n";
            ImGui::EndFrame();
            return false;
        }
        if (!validated_dock_layout) {
            const ImGuiWindow* viewport_window = ImGui::FindWindowByName(kViewportWindowTitle);
            const ImGuiWindow* timeline_window = ImGui::FindWindowByName(kTimelineWindowTitle);
            const ImGuiWindow* hierarchy_window = ImGui::FindWindowByName(kHierarchyWindowTitle);
            const ImGuiWindow* properties_window = ImGui::FindWindowByName(kPropertiesWindowTitle);
            const ImGuiWindow* parameters_window = ImGui::FindWindowByName(kParametersWindowTitle);
            const ImGuiWindow* deformers_window =
                ImGui::FindWindowByName(kParameterDeformersWindowTitle);
            const ImGuiWindow* expressions_window =
                ImGui::FindWindowByName(kExpressionsWindowTitle);
            const ImGuiWindow* lip_sync_window = ImGui::FindWindowByName(kLipSyncWindowTitle);
            const ImGuiDockNode* viewport_node =
                ImGui::DockBuilderGetNode(shell_state.dock_layout.viewport_node_id);
            const ImGuiDockNode* timeline_node =
                ImGui::DockBuilderGetNode(shell_state.dock_layout.timeline_node_id);
            const ImGuiDockNode* hierarchy_node =
                ImGui::DockBuilderGetNode(shell_state.dock_layout.hierarchy_node_id);
            const ImGuiDockNode* properties_node =
                ImGui::DockBuilderGetNode(shell_state.dock_layout.properties_node_id);
            if (!shell_state.default_dock_layout_initialized ||
                viewport_window == nullptr ||
                timeline_window == nullptr ||
                hierarchy_window == nullptr ||
                properties_window == nullptr ||
                parameters_window == nullptr ||
                deformers_window == nullptr ||
                expressions_window == nullptr ||
                lip_sync_window == nullptr ||
                viewport_node == nullptr ||
                timeline_node == nullptr ||
                hierarchy_node == nullptr ||
                properties_node == nullptr ||
                viewport_window->DockId != shell_state.dock_layout.viewport_node_id ||
                timeline_window->DockId != shell_state.dock_layout.timeline_node_id ||
                hierarchy_window->DockId != shell_state.dock_layout.hierarchy_node_id ||
                properties_window->DockId != shell_state.dock_layout.properties_node_id ||
                !(viewport_node->Pos.x > hierarchy_node->Pos.x) ||
                !(timeline_node->Pos.y > viewport_node->Pos.y) ||
                !(properties_node->Pos.y > hierarchy_node->Pos.y) ||
                std::abs(properties_node->Pos.x - hierarchy_node->Pos.x) > 1e-3f) {
                std::cerr << "DockBuilder did not create the default Viewport/Timeline/Hierarchy/Properties layout.\n";
                return false;
            }
            validated_dock_layout = true;
        }
        ImGui::Render();
    }

    // Restore the default panel layout for the specialized single-panel cases.
    shell_state.show_agent_panel = false;
    shell_state.agent_panel_was_open = false;
    shell_state.default_dock_layout_initialized = false;
    apply_shell_mode(&shell_state, ShellMode::Animation);
    if (!set_selected_animation(&shell_state, "idle", "Graph frame smoke", false, true)) {
        std::cerr << "Actual-frame graph smoke could not select idle.\n";
        return false;
    }
    const auto& graph_tracks = cached_timeline_tracks(&shell_state);
    const TimelineTrackRow* rotate_track =
        find_timeline_track(graph_tracks, "bone:1:Rotate");
    const TimelineTrackRow* linear_rotate_track =
        find_timeline_track(graph_tracks, "bone:2:Rotate");
    const TimelineTrackRow* translate_track =
        find_timeline_track(graph_tracks, "bone:1:Translate");
    const TimelineTrackRow* color_track =
        find_timeline_track(graph_tracks, "slot:0:Color");
    const TimelineTrackRow* attachment_track =
        find_timeline_track(graph_tracks, "slot:0:Attachment");
    if (rotate_track == nullptr || linear_rotate_track == nullptr ||
        translate_track == nullptr || color_track == nullptr ||
        attachment_track == nullptr) {
        std::cerr << "Actual-frame graph smoke requires Rotate, Translate, Color, and Attachment rows.\n";
        return false;
    }

    const std::string graph_project_before =
        marrow::editor::serialize_project(*shell_state.session.project());
    const bool graph_dirty_before = shell_state.session.dirty();
    const bool graph_shell_dirty_before = shell_state.project_dirty;
    const std::size_t graph_undo_before = shell_state.session.undo_count();
    const std::size_t graph_redo_before = shell_state.session.redo_count();
    const std::uint64_t graph_project_revision_before =
        shell_state.session.project_revision();
    const std::uint64_t graph_runtime_revision_before =
        shell_state.session.runtime_revision();
    const std::size_t graph_operation_count_before =
        marrow::editor::agent_operation_descriptor_count();

    shell_state.selected_timeline_track_id = rotate_track->id;
    shell_state.timeline_editor.requested_view_mode = TimelineViewMode::Graph;
    io.DeltaTime = 1.0f / 60.0f;
    ImGui::NewFrame();
    ImGui::SetWindowFocus(kTimelineWindowTitle);
    draw_timeline_window(&shell_state, nullptr);
    ImGui::Render();
    TimelineGraphRenderStats rotate_stats;
    ImGui::NewFrame();
    draw_timeline_window(&shell_state, &rotate_stats);
    ImGui::Render();
    if (rotate_stats.status != timeline_graph_model::ProjectionStatus::Ready ||
        rotate_stats.point_count == 0U || !rotate_stats.playhead_drawn ||
        rotate_stats.stepped_segment_count == 0U ||
        rotate_stats.cubic_segment_count == 0U) {
        std::cerr << "Spine Rotate Graph frame did not submit points, playhead, Stepped, and Cubic geometry: status="
                  << static_cast<int>(rotate_stats.status)
                  << " points=" << rotate_stats.point_count
                  << " linear=" << rotate_stats.linear_segment_count
                  << " stepped=" << rotate_stats.stepped_segment_count
                  << " cubic=" << rotate_stats.cubic_segment_count
                  << " playhead=" << rotate_stats.playhead_drawn
                  << " view=" << static_cast<int>(shell_state.timeline_editor.view_mode)
                  << " requested=" << shell_state.timeline_editor.requested_view_mode.has_value()
                  << ".\n";
        return false;
    }

    shell_state.selected_timeline_track_id = linear_rotate_track->id;
    TimelineGraphRenderStats linear_rotate_stats;
    ImGui::NewFrame();
    draw_timeline_window(&shell_state, &linear_rotate_stats);
    ImGui::Render();
    if (linear_rotate_stats.status !=
            timeline_graph_model::ProjectionStatus::Ready ||
        linear_rotate_stats.point_count == 0U ||
        linear_rotate_stats.linear_segment_count == 0U) {
        std::cerr << "arm_l Rotate Graph frame did not submit points and Linear geometry: status="
                  << static_cast<int>(linear_rotate_stats.status)
                  << " points=" << linear_rotate_stats.point_count
                  << " linear=" << linear_rotate_stats.linear_segment_count
                  << " stepped=" << linear_rotate_stats.stepped_segment_count
                  << " cubic=" << linear_rotate_stats.cubic_segment_count
                  << " playhead=" << linear_rotate_stats.playhead_drawn
                  << " cache=" << shell_state.timeline_editor.graph_cache.track_id
                  << ".\n";
        return false;
    }

    shell_state.selected_timeline_track_id = translate_track->id;
    shell_state.timeline_editor.requested_view_mode = TimelineViewMode::Graph;
    TimelineGraphRenderStats translate_stats;
    ImGui::NewFrame();
    ImGui::SetWindowFocus(kTimelineWindowTitle);
    draw_timeline_window(&shell_state, &translate_stats);
    ImGui::Render();
    if (translate_stats.status != timeline_graph_model::ProjectionStatus::Ready ||
        translate_stats.point_count == 0U || !translate_stats.playhead_drawn ||
        translate_stats.stepped_segment_count == 0U) {
        std::cerr << "Translate Graph frame did not submit points, playhead, and Stepped geometry.\n";
        return false;
    }

    const auto render_graph_frame = [&](TimelineGraphRenderStats* stats) {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        draw_timeline_window(&shell_state, stats);
        ImGui::Render();
    };
    const auto click_graph_item = [&](ImVec2 position, TimelineGraphRenderStats* stats) {
        io.AddMousePosEvent(position.x, position.y);
        render_graph_frame(nullptr);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_graph_frame(nullptr);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_graph_frame(stats);
    };

    shell_state.selected_timeline_track_id = color_track->id;
    shell_state.timeline_editor.requested_view_mode = TimelineViewMode::Graph;
    TimelineGraphRenderStats color_stats;
    render_graph_frame(&color_stats);
    render_graph_frame(&color_stats);
    if (color_stats.status != timeline_graph_model::ProjectionStatus::Ready ||
        color_stats.zero_value_tick_label_count != 1U ||
        color_stats.one_value_tick_label_count != 1U) {
        std::cerr << "Slot Color Graph must submit exactly one value-axis label at zero and one: status="
                  << static_cast<int>(color_stats.status)
                  << " zero=" << color_stats.zero_value_tick_label_count
                  << " one=" << color_stats.one_value_tick_label_count
                  << ".\n";
        return false;
    }

    shell_state.selection.replace(marrow::editor::BoneSelection{"spine"});
    const std::string missing_fallback_focus = "missing-graph-fallback-focus";
    shell_state.selected_timeline_track_id = missing_fallback_focus;
    TimelineGraphRenderStats fallback_stats;
    render_graph_frame(&fallback_stats);
    if (shell_state.selected_timeline_track_id != missing_fallback_focus ||
        shell_state.timeline_editor.graph_cache.track_id != rotate_track->id ||
        fallback_stats.first_component_max_x <= fallback_stats.first_component_min_x ||
        fallback_stats.first_component_max_y <= fallback_stats.first_component_min_y ||
        fallback_stats.fit_max_x <= fallback_stats.fit_min_x ||
        fallback_stats.fit_max_y <= fallback_stats.fit_min_y) {
        std::cerr << "Rendering a fallback Graph row mutated focus or omitted interaction bounds.\n";
        return false;
    }

    shell_state.timeline_editor.graph_view.view.view_start_seconds += 123.0;
    shell_state.timeline_editor.graph_view.needs_fit = false;
    const auto perturbed_fit_view = shell_state.timeline_editor.graph_view.view;
    click_graph_item(
        ImVec2(
            (fallback_stats.fit_min_x + fallback_stats.fit_max_x) * 0.5f,
            (fallback_stats.fit_min_y + fallback_stats.fit_max_y) * 0.5f),
        &fallback_stats);
    if (shell_state.selected_timeline_track_id != missing_fallback_focus ||
        shell_state.timeline_editor.graph_view.view.view_start_seconds ==
            perturbed_fit_view.view_start_seconds) {
        std::cerr << "Fallback Graph Fit did not run without promoting track focus.\n";
        return false;
    }

    ImGuiWindow* fallback_timeline_window =
        ImGui::FindWindowByName(kTimelineWindowTitle);
    if (fallback_timeline_window == nullptr) {
        std::cerr << "Fallback Graph interaction smoke could not resolve Timeline window.\n";
        return false;
    }
    // The Graph plot has always been taller than the Timeline window, and
    // MAR-170's appended preset row pushed its top below the visible clip, so
    // scroll it into view before aiming a real mouse at it. This is the same
    // nudge the MAR-169 handle section below already performs.
    for (int attempt = 0; attempt < 16; ++attempt) {
        fallback_timeline_window = ImGui::FindWindowByName(kTimelineWindowTitle);
        if (fallback_timeline_window == nullptr) break;
        const float visible_top = fallback_timeline_window->InnerClipRect.Min.y + 2.0f;
        const float visible_bottom = fallback_timeline_window->InnerClipRect.Max.y - 2.0f;
        if (fallback_stats.plot_min_y + 2.0f <= visible_bottom &&
            fallback_stats.plot_max_y - 2.0f >= visible_top) {
            break;
        }
        const float next_scroll = std::min(
            fallback_timeline_window->ScrollMax.y,
            fallback_timeline_window->Scroll.y + 48.0f);
        if (next_scroll == fallback_timeline_window->Scroll.y) break;
        ImGui::SetScrollY(fallback_timeline_window, next_scroll);
        render_graph_frame(&fallback_stats);
    }
    fallback_timeline_window = ImGui::FindWindowByName(kTimelineWindowTitle);
    if (fallback_timeline_window == nullptr) {
        std::cerr << "Fallback Graph interaction smoke lost its Timeline window.\n";
        return false;
    }
    const float fallback_hover_min_x = std::max(
        fallback_stats.plot_min_x + 2.0f,
        fallback_timeline_window->InnerClipRect.Min.x + 2.0f);
    const float fallback_hover_max_x = std::min(
        fallback_stats.plot_max_x - 2.0f,
        fallback_timeline_window->InnerClipRect.Max.x - 2.0f);
    const float fallback_hover_min_y = std::max(
        fallback_stats.plot_min_y + 2.0f,
        fallback_timeline_window->InnerClipRect.Min.y + 2.0f);
    const float fallback_hover_max_y = std::min(
        fallback_stats.plot_max_y - 2.0f,
        fallback_timeline_window->InnerClipRect.Max.y - 2.0f);
    if (fallback_hover_min_x > fallback_hover_max_x ||
        fallback_hover_min_y > fallback_hover_max_y) {
        std::cerr << "Fallback Graph interaction smoke could not locate visible plot space: plot=("
                  << fallback_stats.plot_min_y << "-" << fallback_stats.plot_max_y
                  << ") clip=(" << fallback_timeline_window->InnerClipRect.Min.y
                  << "-" << fallback_timeline_window->InnerClipRect.Max.y << ").\n";
        return false;
    }
    const ImVec2 fallback_plot_center{
        (fallback_hover_min_x + fallback_hover_max_x) * 0.5f,
        (fallback_hover_min_y + fallback_hover_max_y) * 0.5f};
    io.AddMousePosEvent(fallback_plot_center.x, fallback_plot_center.y);
    render_graph_frame(nullptr);
    const double fallback_zoom_before =
        shell_state.timeline_editor.graph_view.view.pixels_per_second;
    io.AddMouseWheelEvent(0.0f, -1.0f);
    render_graph_frame(&fallback_stats);
    if (shell_state.selected_timeline_track_id != missing_fallback_focus ||
        shell_state.timeline_editor.graph_view.view.pixels_per_second ==
            fallback_zoom_before) {
        std::cerr << "Fallback Graph wheel zoom did not run without promoting track focus: focus="
                  << shell_state.selected_timeline_track_id.value_or("<none>")
                  << " expected=" << missing_fallback_focus
                  << " hovered=" << fallback_stats.plot_hovered
                  << " scale=" << fallback_zoom_before << "->"
                  << shell_state.timeline_editor.graph_view.view.pixels_per_second
                  << ".\n";
        return false;
    }

    const auto fallback_view_before_pan = shell_state.timeline_editor.graph_view.view;
    io.AddMousePosEvent(fallback_plot_center.x, fallback_plot_center.y);
    render_graph_frame(nullptr);
    io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
    render_graph_frame(nullptr);
    const ImVec2 fallback_pan_target{
        std::min(fallback_hover_max_x, fallback_plot_center.x + 20.0f),
        std::min(fallback_hover_max_y, fallback_plot_center.y + 10.0f)};
    io.AddMousePosEvent(fallback_pan_target.x, fallback_pan_target.y);
    render_graph_frame(nullptr);
    io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
    render_graph_frame(&fallback_stats);
    if (shell_state.selected_timeline_track_id != missing_fallback_focus ||
        (shell_state.timeline_editor.graph_view.view.view_start_seconds ==
             fallback_view_before_pan.view_start_seconds &&
         shell_state.timeline_editor.graph_view.view.value_center ==
             fallback_view_before_pan.value_center)) {
        std::cerr << "Fallback Graph middle pan did not run without promoting track focus.\n";
        return false;
    }

    const bool first_component_before =
        shell_state.timeline_editor.graph_view.component_visible[0U];
    click_graph_item(
        ImVec2(
            (fallback_stats.first_component_min_x +
             fallback_stats.first_component_max_x) * 0.5f,
            (fallback_stats.first_component_min_y +
             fallback_stats.first_component_max_y) * 0.5f),
        &fallback_stats);
    if (shell_state.selected_timeline_track_id != rotate_track->id ||
        shell_state.timeline_editor.graph_view.component_visible[0U] ==
            first_component_before) {
        std::cerr << "Clicking a fallback Graph legend component did not promote its parent focus.\n";
        return false;
    }
    shell_state.timeline_editor.graph_view.component_visible[0U] = true;

    shell_state.selected_timeline_track_id = missing_fallback_focus;
    const TimelineKeyRef fallback_preserved_key =
        timeline_model::key_ref(*rotate_track, 0U);
    shell_state.timeline_editor.selected_keys = {fallback_preserved_key};
    shell_state.timeline_editor.active_key = fallback_preserved_key;
    if (!scrub_timeline_time(
            &shell_state, 0.0, "Fallback empty-plot staging", false)) {
        std::cerr << "Fallback empty-plot smoke could not stage its playhead.\n";
        return false;
    }
    render_graph_frame(&fallback_stats);
    const auto& fallback_projection = shell_state.timeline_editor.graph_cache.projection;
    const timeline_graph_model::PlotRect fallback_plot{
        fallback_stats.plot_min_x,
        fallback_stats.plot_min_y,
        fallback_stats.plot_max_x,
        fallback_stats.plot_max_y};
    const auto fallback_geometry = fallback_projection.track.has_value()
        ? timeline_graph_model::build_geometry(
              *fallback_projection.track,
              shell_state.timeline_editor.graph_view.component_visible,
              shell_state.timeline_editor.graph_view.view,
              fallback_plot,
              shell_state.timeline_time_seconds)
        : std::nullopt;
    std::optional<ImVec2> empty_plot_position;
    if (fallback_geometry.has_value()) {
        for (int x_step = 9; x_step >= 1 && !empty_plot_position.has_value(); --x_step) {
            const float x = fallback_hover_min_x +
                (fallback_hover_max_x - fallback_hover_min_x) *
                    static_cast<float>(x_step) / 10.0f;
            const double candidate_time =
                shell_state.timeline_editor.graph_view.view.view_start_seconds +
                (static_cast<double>(x) - fallback_plot.min_x) /
                    shell_state.timeline_editor.graph_view.view.pixels_per_second;
            if (candidate_time <= 0.05) continue;
            for (int y_step = 1; y_step <= 9; ++y_step) {
                const float y = fallback_hover_min_y +
                    (fallback_hover_max_y - fallback_hover_min_y) *
                        static_cast<float>(y_step) / 10.0f;
                if (!timeline_graph_model::hit_test(
                        *fallback_geometry, x, y, 8.0).has_value()) {
                    empty_plot_position = ImVec2{x, y};
                    break;
                }
            }
        }
    }
    if (!empty_plot_position.has_value()) {
        std::cerr << "Fallback empty-plot smoke could not find visible space outside key hits.\n";
        return false;
    }
    click_graph_item(*empty_plot_position, &fallback_stats);
    if (shell_state.selected_timeline_track_id != rotate_track->id ||
        shell_state.timeline_editor.selected_keys !=
            std::vector<TimelineKeyRef>{fallback_preserved_key} ||
        !(shell_state.timeline_editor.active_key ==
          std::optional<TimelineKeyRef>(fallback_preserved_key)) ||
        shell_state.timeline_time_seconds <= 0.0) {
        std::cerr << "Clicking fallback Graph empty plot did not focus its parent while preserving key state and scrubbing.\n";
        return false;
    }
    shell_state.selected_timeline_track_id = translate_track->id;

    ImGuiWindow* timeline_window = ImGui::FindWindowByName(kTimelineWindowTitle);
    if (timeline_window == nullptr || translate_stats.plot_item_id == 0U ||
        timeline_window->ScrollMax.y <= 0.0f) {
        std::cerr << "Graph wheel routing smoke requires a scrollable Timeline plot item.\n";
        return false;
    }
    ImGui::SetScrollY(timeline_window, timeline_window->ScrollMax.y);
    ImGui::NewFrame();
    TimelineGraphRenderStats visible_plot_stats;
    draw_timeline_window(&shell_state, &visible_plot_stats);
    ImGui::Render();
    timeline_window = ImGui::FindWindowByName(kTimelineWindowTitle);
    const float hover_min_x = std::max(
        visible_plot_stats.plot_min_x + 2.0f,
        timeline_window->InnerClipRect.Min.x + 2.0f);
    const float hover_max_x = std::min(
        visible_plot_stats.plot_max_x - 2.0f,
        timeline_window->InnerClipRect.Max.x - 2.0f);
    const float hover_min_y = std::max(
        visible_plot_stats.plot_min_y + 2.0f,
        timeline_window->InnerClipRect.Min.y + 2.0f);
    const float hover_max_y = std::min(
        visible_plot_stats.plot_max_y - 2.0f,
        timeline_window->InnerClipRect.Max.y - 2.0f);
    if (hover_min_x > hover_max_x || hover_min_y > hover_max_y) {
        std::cerr << "Graph wheel routing smoke could not locate a visible plot point: plot=("
                  << visible_plot_stats.plot_min_x << ","
                  << visible_plot_stats.plot_min_y << ")-("
                  << visible_plot_stats.plot_max_x << ","
                  << visible_plot_stats.plot_max_y << ") clip=("
                  << timeline_window->InnerClipRect.Min.x << ","
                  << timeline_window->InnerClipRect.Min.y << ")-("
                  << timeline_window->InnerClipRect.Max.x << ","
                  << timeline_window->InnerClipRect.Max.y << ") scroll="
                  << timeline_window->Scroll.y << "/" << timeline_window->ScrollMax.y
                  << ".\n";
        return false;
    }
    const ImVec2 graph_hover_position{
        (hover_min_x + hover_max_x) * 0.5f,
        (hover_min_y + hover_max_y) * 0.5f};
    io.AddMousePosEvent(graph_hover_position.x, graph_hover_position.y);
    ImGui::NewFrame();
    TimelineGraphRenderStats hover_stats;
    draw_timeline_window(&shell_state, &hover_stats);
    const ImGuiID wheel_owner_before_input =
        ImGui::GetKeyOwner(ImGuiKey_MouseWheelY);
    ImGui::Render();

    timeline_window = ImGui::FindWindowByName(kTimelineWindowTitle);
    const float timeline_scroll_before_wheel = timeline_window->Scroll.y;
    const float wheel_delta =
        timeline_scroll_before_wheel < timeline_window->ScrollMax.y ? -1.0f : 1.0f;
    const double graph_pixels_before_wheel =
        shell_state.timeline_editor.graph_view.view.pixels_per_second;
    io.AddMouseWheelEvent(0.0f, wheel_delta);
    ImGui::NewFrame();
    TimelineGraphRenderStats wheel_stats;
    draw_timeline_window(&shell_state, &wheel_stats);
    const ImGuiID wheel_owner_after_input =
        ImGui::GetKeyOwner(ImGuiKey_MouseWheelY);
    timeline_window = ImGui::FindWindowByName(kTimelineWindowTitle);
    const float timeline_scroll_after_wheel = timeline_window->Scroll.y;
    const double graph_pixels_after_wheel =
        shell_state.timeline_editor.graph_view.view.pixels_per_second;
    ImGui::Render();
    if (!hover_stats.plot_hovered ||
        hover_stats.plot_item_id != visible_plot_stats.plot_item_id ||
        wheel_stats.plot_item_id != visible_plot_stats.plot_item_id ||
        wheel_owner_before_input != visible_plot_stats.plot_item_id ||
        wheel_owner_after_input != visible_plot_stats.plot_item_id ||
        wheel_stats.status != timeline_graph_model::ProjectionStatus::Ready ||
        graph_pixels_after_wheel == graph_pixels_before_wheel ||
        timeline_scroll_after_wheel != timeline_scroll_before_wheel) {
        std::cerr << "Graph wheel routing did not claim MouseWheelY, zoom Graph, and suppress Timeline scroll: hovered="
                  << hover_stats.plot_hovered
                  << " plot=" << visible_plot_stats.plot_item_id
                  << " hovered_plot=" << hover_stats.plot_item_id
                  << " wheel_plot=" << wheel_stats.plot_item_id
                  << " owner_before=" << wheel_owner_before_input
                  << " owner_after=" << wheel_owner_after_input
                  << " graph_before=" << graph_pixels_before_wheel
                  << " graph_after=" << graph_pixels_after_wheel
                  << " scroll_before=" << timeline_scroll_before_wheel
                  << " scroll_after=" << timeline_scroll_after_wheel
                  << ".\n";
        return false;
    }
    std::cout << "Timeline Graph wheel routing: hovered="
              << hover_stats.plot_hovered
              << " owner=" << wheel_owner_after_input
              << " graph=" << graph_pixels_before_wheel
              << "->" << graph_pixels_after_wheel
              << " scroll=" << timeline_scroll_before_wheel
              << "->" << timeline_scroll_after_wheel
              << ".\n";

    const ImVec2 graph_margin_position{
        visible_plot_stats.plot_min_x - 20.0f,
        graph_hover_position.y};
    io.AddMousePosEvent(graph_margin_position.x, graph_margin_position.y);
    ImGui::NewFrame();
    TimelineGraphRenderStats margin_prime_stats;
    draw_timeline_window(&shell_state, &margin_prime_stats);
    ImGui::Render();
    ImGui::NewFrame();
    TimelineGraphRenderStats margin_hover_stats;
    draw_timeline_window(&shell_state, &margin_hover_stats);
    const ImGuiID margin_owner_before_input =
        ImGui::GetKeyOwner(ImGuiKey_MouseWheelY);
    ImGui::Render();

    timeline_window = ImGui::FindWindowByName(kTimelineWindowTitle);
    const float timeline_scroll_before_margin_wheel = timeline_window->Scroll.y;
    const float margin_wheel_delta =
        timeline_scroll_before_margin_wheel > 0.0f ? 1.0f : -1.0f;
    const double graph_pixels_before_margin_wheel =
        shell_state.timeline_editor.graph_view.view.pixels_per_second;
    io.AddMouseWheelEvent(0.0f, margin_wheel_delta);
    ImGui::NewFrame();
    TimelineGraphRenderStats margin_wheel_stats;
    draw_timeline_window(&shell_state, &margin_wheel_stats);
    const ImGuiID margin_owner_after_input =
        ImGui::GetKeyOwner(ImGuiKey_MouseWheelY);
    timeline_window = ImGui::FindWindowByName(kTimelineWindowTitle);
    const float timeline_scroll_after_margin_wheel = timeline_window->Scroll.y;
    const double graph_pixels_after_margin_wheel =
        shell_state.timeline_editor.graph_view.view.pixels_per_second;
    ImGui::Render();
    if (!margin_hover_stats.plot_item_hovered ||
        margin_hover_stats.plot_hovered ||
        margin_hover_stats.plot_item_id != visible_plot_stats.plot_item_id ||
        margin_owner_before_input != ImGuiKeyOwner_NoOwner ||
        margin_owner_after_input != ImGuiKeyOwner_NoOwner ||
        graph_pixels_after_margin_wheel != graph_pixels_before_margin_wheel ||
        timeline_scroll_after_margin_wheel == timeline_scroll_before_margin_wheel) {
        std::cerr << "Graph axis-margin wheel did not remain unowned, preserve Graph zoom, and allow Timeline scroll: item_hovered="
                  << margin_hover_stats.plot_item_hovered
                  << " plot_hovered=" << margin_hover_stats.plot_hovered
                  << " plot=" << visible_plot_stats.plot_item_id
                  << " margin_plot=" << margin_hover_stats.plot_item_id
                  << " owner_before=" << margin_owner_before_input
                  << " owner_after=" << margin_owner_after_input
                  << " graph_before=" << graph_pixels_before_margin_wheel
                  << " graph_after=" << graph_pixels_after_margin_wheel
                  << " scroll_before=" << timeline_scroll_before_margin_wheel
                  << " scroll_after=" << timeline_scroll_after_margin_wheel
                  << ".\n";
        return false;
    }
    std::cout << "Timeline Graph margin wheel routing: item_hovered="
              << margin_hover_stats.plot_item_hovered
              << " plot_hovered=" << margin_hover_stats.plot_hovered
              << " owner=" << margin_owner_after_input
              << " graph=" << graph_pixels_before_margin_wheel
              << "->" << graph_pixels_after_margin_wheel
              << " scroll=" << timeline_scroll_before_margin_wheel
              << "->" << timeline_scroll_after_margin_wheel
              << ".\n";

    const TimelineKeyRef keyboard_key = timeline_model::key_ref(*translate_track, 0U);
    shell_state.timeline_editor.selected_keys = {keyboard_key};
    shell_state.timeline_editor.active_key = keyboard_key;
    const std::string keyboard_project_before =
        marrow::editor::serialize_project(*shell_state.session.project());
    const std::size_t keyboard_undo_before = shell_state.session.undo_count();
    io.AddKeyEvent(ImGuiKey_Delete, true);
    ImGui::NewFrame();
    ImGui::SetWindowFocus(kTimelineWindowTitle);
    draw_timeline_window(&shell_state, nullptr);
    ImGui::Render();
    io.AddKeyEvent(ImGuiKey_Delete, false);
    io.AddKeyEvent(ImGuiKey_Backspace, true);
    ImGui::NewFrame();
    draw_timeline_window(&shell_state, nullptr);
    ImGui::Render();
    io.AddKeyEvent(ImGuiKey_Backspace, false);
    if (marrow::editor::serialize_project(*shell_state.session.project()) !=
            keyboard_project_before ||
        shell_state.session.undo_count() != keyboard_undo_before ||
        shell_state.timeline_editor.selected_keys !=
            std::vector<TimelineKeyRef>{keyboard_key} ||
        !(shell_state.timeline_editor.active_key ==
          std::optional<TimelineKeyRef>(keyboard_key))) {
        std::cerr << "Graph Delete/Backspace removed an editable key or changed selection.\n";
        return false;
    }

    shell_state.selected_timeline_track_id = attachment_track->id;
    TimelineGraphRenderStats unsupported_stats;
    unsupported_stats.point_count = 99U;
    unsupported_stats.linear_segment_count = 99U;
    unsupported_stats.stepped_segment_count = 99U;
    unsupported_stats.cubic_segment_count = 99U;
    unsupported_stats.playhead_drawn = true;
    ImGui::NewFrame();
    draw_timeline_window(&shell_state, &unsupported_stats);
    ImGui::Render();
    if (unsupported_stats.status !=
            timeline_graph_model::ProjectionStatus::UnsupportedTrack ||
        unsupported_stats.point_count != 0U ||
        unsupported_stats.linear_segment_count != 0U ||
        unsupported_stats.stepped_segment_count != 0U ||
        unsupported_stats.cubic_segment_count != 0U ||
        unsupported_stats.playhead_drawn) {
        std::cerr << "Unsupported Graph focus retained stale submitted geometry stats.\n";
        return false;
    }
    shell_state.selected_timeline_track_id = translate_track->id;

    const auto graph_selection_before_tab = shell_state.timeline_editor.selected_keys;
    const auto graph_active_before_tab = shell_state.timeline_editor.active_key;
    const double graph_playhead_before_tab = shell_state.timeline_time_seconds;
    shell_state.timeline_editor.requested_view_mode = TimelineViewMode::Dopesheet;
    ImGui::NewFrame();
    ImGui::SetWindowFocus(kTimelineWindowTitle);
    draw_timeline_window(&shell_state, nullptr);
    ImGui::Render();
    ImGui::NewFrame();
    draw_timeline_window(&shell_state, nullptr);
    ImGui::Render();
    if (shell_state.timeline_editor.view_mode != TimelineViewMode::Dopesheet ||
        shell_state.timeline_editor.selected_keys != graph_selection_before_tab ||
        !(shell_state.timeline_editor.active_key == graph_active_before_tab) ||
        shell_state.timeline_time_seconds != graph_playhead_before_tab) {
        std::cerr << "Switching from Graph to Dopesheet changed shared timeline state.\n";
        return false;
    }

    if (marrow::editor::serialize_project(*shell_state.session.project()) !=
            graph_project_before ||
        shell_state.session.dirty() != graph_dirty_before ||
        shell_state.project_dirty != graph_shell_dirty_before ||
        shell_state.session.undo_count() != graph_undo_before ||
        shell_state.session.redo_count() != graph_redo_before ||
        shell_state.session.project_revision() != graph_project_revision_before ||
        shell_state.session.runtime_revision() != graph_runtime_revision_before ||
        marrow::editor::agent_operation_descriptor_count() !=
            graph_operation_count_before) {
        std::cerr << "Actual Graph frames mutated project, history, revisions, dirty state, or Agent surface.\n";
        return false;
    }
    std::cout << "Timeline Graph actual-frame stats: spine_rotate points="
              << rotate_stats.point_count
              << " linear=" << rotate_stats.linear_segment_count
              << " stepped=" << rotate_stats.stepped_segment_count
              << " cubic=" << rotate_stats.cubic_segment_count
              << " playhead=" << rotate_stats.playhead_drawn
              << "; arm_l_rotate points=" << linear_rotate_stats.point_count
              << " linear=" << linear_rotate_stats.linear_segment_count
              << " stepped=" << linear_rotate_stats.stepped_segment_count
              << " cubic=" << linear_rotate_stats.cubic_segment_count
              << " playhead=" << linear_rotate_stats.playhead_drawn
              << "; spine_translate points=" << translate_stats.point_count
              << " linear=" << translate_stats.linear_segment_count
              << " stepped=" << translate_stats.stepped_segment_count
              << " cubic=" << translate_stats.cubic_segment_count
              << " playhead=" << translate_stats.playhead_drawn
              << ".\n";

    // --- MAR-168: actual-frame graph point drags through real ImGui mouse
    // events aimed at real submitted point coordinates. ---
    {
        // Editing rebuilds the shared track cache, so this block keeps only
        // value copies of the focused row's identity, never a row pointer.
        const std::string drag_track_id = translate_track->id;
        const std::size_t drag_bone_index = translate_track->bone_index.value_or(0U);
        const TimelineKeyRef drag_key = timeline_model::key_ref(*translate_track, 0U);
        shell_state.selected_timeline_track_id = drag_track_id;
        shell_state.timeline_editor.requested_view_mode = TimelineViewMode::Graph;
        shell_state.timeline_editor.snap_to_frames = true;
        shell_state.timeline_editor.frames_per_second = 60.0;
        const auto reset_drag_selection = [&]() {
            shell_state.timeline_editor.selected_keys = {drag_key};
            shell_state.timeline_editor.active_key = drag_key;
            shell_state.timeline_editor.graph_view.active_component =
                timeline_graph_model::Component::X;
        };
        // One settling frame first: switching the focused row resets the
        // graph's active component and requests a fresh fit.
        render_graph_frame(nullptr);
        reset_drag_selection();
        shell_state.timeline_editor.graph_view.needs_fit = true;
        TimelineGraphRenderStats drag_stats;
        render_graph_frame(&drag_stats);
        // Scroll the plot back into the Timeline window so a real mouse can
        // reach the submitted point.
        if (ImGuiWindow* scroll_window = ImGui::FindWindowByName(kTimelineWindowTitle)) {
            ImGui::SetScrollY(scroll_window, scroll_window->ScrollMax.y);
        }
        reset_drag_selection();
        render_graph_frame(&drag_stats);
        reset_drag_selection();
        render_graph_frame(&drag_stats);
        ImGuiWindow* drag_window = ImGui::FindWindowByName(kTimelineWindowTitle);
        if (drag_window == nullptr || !drag_stats.active_point_valid ||
            !drag_stats.first_point_valid ||
            drag_stats.status != timeline_graph_model::ProjectionStatus::Ready) {
            std::cerr << "Actual-frame graph drag smoke could not submit an active point: valid="
                      << drag_stats.active_point_valid
                      << " points=" << drag_stats.point_count
                      << " component="
                      << (shell_state.timeline_editor.graph_view.active_component.has_value()
                              ? static_cast<int>(
                                    *shell_state.timeline_editor.graph_view.active_component)
                              : -1)
                      << " selected=" << shell_state.timeline_editor.selected_keys.size()
                      << " status=" << static_cast<int>(drag_stats.status) << ".\n";
            return false;
        }
        ImVec2 press_position{drag_stats.active_point_x, drag_stats.active_point_y};
        if (press_position.x < drag_window->InnerClipRect.Min.x + 1.0f ||
            press_position.x > drag_window->InnerClipRect.Max.x - 1.0f ||
            press_position.y < drag_window->InnerClipRect.Min.y + 1.0f ||
            press_position.y > drag_window->InnerClipRect.Max.y - 1.0f) {
            std::cerr << "Actual-frame graph drag smoke could not reach a visible active point: point=("
                      << press_position.x << "," << press_position.y << ") clip=("
                      << drag_window->InnerClipRect.Min.x << ","
                      << drag_window->InnerClipRect.Min.y << ")-("
                      << drag_window->InnerClipRect.Max.x << ","
                      << drag_window->InnerClipRect.Max.y << ").\n";
            return false;
        }

        const std::string drag_project_before =
            marrow::editor::serialize_project(*shell_state.session.project());
        const std::size_t drag_undo_before = shell_state.session.undo_count();

        const auto press_graph = [&](TimelineGraphRenderStats* stats) {
            io.AddMousePosEvent(press_position.x, press_position.y);
            render_graph_frame(nullptr);
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            render_graph_frame(stats);
        };
        const auto move_graph = [&](float dx, float dy, TimelineGraphRenderStats* stats) {
            io.AddMousePosEvent(press_position.x + dx, press_position.y + dy);
            render_graph_frame(stats);
        };
        const auto release_graph = [&](TimelineGraphRenderStats* stats) {
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            render_graph_frame(stats);
        };

        // 1. Dominant vertical motion locks the value axis.
        TimelineGraphRenderStats value_stats;
        press_graph(&value_stats);
        if (!value_stats.drag_active ||
            value_stats.drag_axis != timeline_graph_model::DragAxis::Undecided ||
            value_stats.value_gesture_active) {
            std::cerr << "An actual-frame graph press did not arm a candidate: candidate="
                      << value_stats.drag_active
                      << " axis=" << static_cast<int>(value_stats.drag_axis) << ".\n";
            return false;
        }
        move_graph(0.0f, -40.0f, &value_stats);
        if (value_stats.drag_axis != timeline_graph_model::DragAxis::Value ||
            !value_stats.value_gesture_active || value_stats.graph_owns_retime) {
            std::cerr << "An actual-frame vertical graph drag did not lock the value axis: axis="
                      << static_cast<int>(value_stats.drag_axis)
                      << " value=" << value_stats.value_gesture_active
                      << " retime=" << value_stats.graph_owns_retime << ".\n";
            return false;
        }
        // 2. A frozen view: zoom, pan, and a requested Fit are inert mid-drag.
        const auto view_before_suppression = shell_state.timeline_editor.graph_view.view;
        io.AddMouseWheelEvent(0.0f, -1.0f);
        move_graph(0.0f, -40.0f, &value_stats);
        shell_state.timeline_editor.graph_view.needs_fit = true;
        move_graph(0.0f, -40.0f, &value_stats);
        io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
        move_graph(18.0f, -46.0f, &value_stats);
        io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
        move_graph(0.0f, -40.0f, &value_stats);
        const auto view_after_suppression = shell_state.timeline_editor.graph_view.view;
        if (view_after_suppression.pixels_per_second !=
                view_before_suppression.pixels_per_second ||
            view_after_suppression.pixels_per_value !=
                view_before_suppression.pixels_per_value ||
            view_after_suppression.view_start_seconds !=
                view_before_suppression.view_start_seconds ||
            view_after_suppression.value_center !=
                view_before_suppression.value_center ||
            !shell_state.timeline_editor.graph_view.needs_fit) {
            std::cerr << "A live graph drag did not freeze its view transform.\n";
            return false;
        }
        // 3. Release commits one entry with an unchanged time and sibling.
        const auto* pre_release_animation =
            shell_state.session.runtime_data()->find_animation("idle");
        const auto* pre_release_translate =
            pre_release_animation != nullptr
            ? pre_release_animation->find_translate_timeline(drag_bone_index)
            : nullptr;
        if (pre_release_translate == nullptr || pre_release_translate->keyframes.empty()) {
            std::cerr << "Actual-frame graph drag smoke lost its translate timeline.\n";
            return false;
        }
        const double previewed_x =
            static_cast<double>(pre_release_translate->keyframes.front().x);
        const double previewed_y =
            static_cast<double>(pre_release_translate->keyframes.front().y);
        const double previewed_time =
            static_cast<double>(pre_release_translate->keyframes.front().time);
        release_graph(&value_stats);
        if (value_stats.drag_active || value_stats.value_gesture_active ||
            shell_state.session.undo_count() != drag_undo_before + 1U) {
            std::cerr << "Releasing an actual-frame graph value drag did not commit one entry.\n";
            return false;
        }
        {
            const auto* committed = shell_state.session.runtime_data()->find_animation("idle");
            const auto* committed_translate =
                committed != nullptr
                ? committed->find_translate_timeline(drag_bone_index)
                : nullptr;
            if (committed_translate == nullptr || committed_translate->keyframes.empty() ||
                std::abs(
                    static_cast<double>(committed_translate->keyframes.front().x) -
                    previewed_x) > 1e-5 ||
                std::abs(
                    static_cast<double>(committed_translate->keyframes.front().y) -
                    previewed_y) > 1e-5 ||
                std::abs(
                    static_cast<double>(committed_translate->keyframes.front().time) -
                    previewed_time) > 1e-6) {
                std::cerr << "An actual-frame graph value drag changed a key time or sibling.\n";
                return false;
            }
        }
        if (!shell_state.session.undo()) {
            std::cerr << "Actual-frame graph value drag could not be undone.\n";
            return false;
        }
        sync_shell_from_editor_session(&shell_state);
        shell_state.session.clear_history();
        shell_state.timeline_editor.graph_view.needs_fit = false;

        // 4. Dominant horizontal motion locks the time axis. The rollback
        // baseline is recaptured here: committing and undoing the value drag
        // legitimately advanced the monotonic project revision.
        const std::string time_project_before =
            marrow::editor::serialize_project(*shell_state.session.project());
        const std::size_t time_undo_before = shell_state.session.undo_count();
        const std::uint64_t time_project_revision_before =
            shell_state.session.project_revision();
        // Re-aim at the freshly submitted active point: undo plus the deferred
        // auto-fit that the first drag left pending can move it.
        reset_drag_selection();
        TimelineGraphRenderStats time_stats;
        render_graph_frame(&time_stats);
        reset_drag_selection();
        render_graph_frame(&time_stats);
        drag_window = ImGui::FindWindowByName(kTimelineWindowTitle);
        if (drag_window == nullptr || !time_stats.active_point_valid) {
            std::cerr << "Actual-frame time drag smoke lost its active point.\n";
            return false;
        }
        press_position = ImVec2{time_stats.active_point_x, time_stats.active_point_y};
        if (press_position.x < drag_window->InnerClipRect.Min.x + 1.0f ||
            press_position.x > drag_window->InnerClipRect.Max.x - 41.0f ||
            press_position.y < drag_window->InnerClipRect.Min.y + 1.0f ||
            press_position.y > drag_window->InnerClipRect.Max.y - 1.0f) {
            std::cerr << "Actual-frame time drag smoke could not reach a visible active point: point=("
                      << press_position.x << "," << press_position.y << ").\n";
            return false;
        }
        press_graph(&time_stats);
        move_graph(40.0f, 0.0f, &time_stats);
        if (time_stats.drag_axis != timeline_graph_model::DragAxis::Time ||
            !time_stats.graph_owns_retime || time_stats.value_gesture_active) {
            std::cerr << "An actual-frame horizontal graph drag did not lock the time axis: axis="
                      << static_cast<int>(time_stats.drag_axis)
                      << " retime=" << time_stats.graph_owns_retime
                      << " value=" << time_stats.value_gesture_active << ".\n";
            return false;
        }
        // 5. Switching to the Dopesheet mid-drag cancels with a full rollback.
        shell_state.timeline_editor.requested_view_mode = TimelineViewMode::Dopesheet;
        render_graph_frame(nullptr);
        render_graph_frame(nullptr);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_graph_frame(nullptr);
        if (shell_state.timeline_editor.graph_drag.has_value() ||
            authoring_gesture_active(shell_state) ||
            marrow::editor::serialize_project(*shell_state.session.project()) !=
                time_project_before ||
            shell_state.session.undo_count() != time_undo_before ||
            shell_state.session.project_revision() != time_project_revision_before) {
            std::cerr << "A mid-drag Dopesheet tab switch did not cancel with rollback: drag="
                      << shell_state.timeline_editor.graph_drag.has_value()
                      << " gesture=" << authoring_gesture_active(shell_state)
                      << " undo=" << shell_state.session.undo_count()
                      << " revision=" << shell_state.session.project_revision()
                      << "/" << time_project_revision_before << ".\n";
            return false;
        }
        // 6. The shared dopesheet retime lane still starts and commits.
        reset_drag_selection();
        const auto& dopesheet_tracks = cached_timeline_tracks(&shell_state);
        if (!begin_timeline_retime_gesture(&shell_state, 777U, 0.0f, dopesheet_tracks) ||
            !apply_timeline_retime_delta(
                &shell_state, cached_timeline_tracks(&shell_state), 0.1, true)) {
            std::cerr << "The dopesheet retime lane did not work after a cancelled graph drag.\n";
            return false;
        }
        finish_timeline_retime_gesture(&shell_state, true);
        if (authoring_gesture_active(shell_state) ||
            shell_state.session.undo_count() != time_undo_before + 1U) {
            std::cerr << "The dopesheet retime lane did not commit after a cancelled graph drag.\n";
            return false;
        }
        if (!shell_state.session.undo()) return false;
        sync_shell_from_editor_session(&shell_state);
        shell_state.session.clear_history();
        reconcile_timeline_key_selection(
            &shell_state, cached_timeline_tracks(&shell_state));
        if (marrow::editor::serialize_project(*shell_state.session.project()) !=
            drag_project_before) {
            std::cerr << "Actual-frame graph drag smoke did not restore its project bytes.\n";
            return false;
        }
        std::cout << "Timeline Graph actual-frame drags: value axis="
                  << static_cast<int>(timeline_graph_model::DragAxis::Value)
                  << " time axis=" << static_cast<int>(timeline_graph_model::DragAxis::Time)
                  << " active point=(" << press_position.x << "," << press_position.y
                  << ").\n";
        shell_state.selected_timeline_track_id = drag_track_id;
        shell_state.timeline_editor.requested_view_mode = TimelineViewMode::Graph;
        render_graph_frame(nullptr);
    }

    // --- MAR-169: actual-frame handle drags through real ImGui mouse events
    // aimed at real submitted handle coordinates. ---
    {
        // The shared track cache was rebuilt many times by now, so the row is
        // re-resolved rather than reusing the pointer taken at setup.
        const TimelineTrackRow* handle_row = find_timeline_track(
            cached_timeline_tracks(&shell_state), "bone:1:Translate");
        if (handle_row == nullptr || handle_row->key_times.size() < 2U) {
            std::cerr << "Actual-frame handle smoke lost its Translate row.\n";
            return false;
        }
        const std::string handle_track_id = handle_row->id;
        const TimelineKeyRef handle_key = timeline_model::key_ref(*handle_row, 0U);
        shell_state.selected_timeline_track_id = handle_track_id;
        shell_state.timeline_editor.requested_view_mode = TimelineViewMode::Graph;
        const auto reset_handle_selection = [&]() {
            shell_state.timeline_editor.selected_keys = {handle_key};
            shell_state.timeline_editor.active_key = handle_key;
            // The displayed component is Y; the shared curve it edits is the
            // parent key's, so X re-projects the same easing.
            shell_state.timeline_editor.graph_view.active_component =
                timeline_graph_model::Component::Y;
        };
        render_graph_frame(nullptr);
        reset_handle_selection();
        shell_state.timeline_editor.graph_view.needs_fit = true;
        TimelineGraphRenderStats handle_stats;
        render_graph_frame(&handle_stats);
        if (ImGuiWindow* scroll_window = ImGui::FindWindowByName(kTimelineWindowTitle)) {
            ImGui::SetScrollY(scroll_window, scroll_window->ScrollMax.y);
        }
        reset_handle_selection();
        render_graph_frame(&handle_stats);
        reset_handle_selection();
        render_graph_frame(&handle_stats);
        // The plot is taller than the Timeline window, so scrolling to the
        // bottom can leave the handles above the visible area. Nudge the
        // scroll until the first handle sits inside the clip with room for the
        // drag, instead of assuming a fixed scroll position.
        for (int attempt = 0; attempt < 8; ++attempt) {
            ImGuiWindow* scroll_window = ImGui::FindWindowByName(kTimelineWindowTitle);
            if (scroll_window == nullptr || !handle_stats.handles_drawn) break;
            const float lower = scroll_window->InnerClipRect.Min.y + 48.0f;
            const float upper = scroll_window->InnerClipRect.Max.y - 48.0f;
            if (handle_stats.first_handle_y >= lower &&
                handle_stats.first_handle_y <= upper) {
                break;
            }
            const float target = (lower + upper) * 0.5f;
            const float shift = target - handle_stats.first_handle_y;
            ImGui::SetScrollY(
                scroll_window,
                std::clamp(
                    scroll_window->Scroll.y - shift, 0.0f, scroll_window->ScrollMax.y));
            reset_handle_selection();
            render_graph_frame(&handle_stats);
            reset_handle_selection();
            render_graph_frame(&handle_stats);
        }
        ImGuiWindow* handle_window = ImGui::FindWindowByName(kTimelineWindowTitle);
        if (handle_window == nullptr || !handle_stats.handles_drawn ||
            !std::isfinite(handle_stats.first_handle_x) ||
            !std::isfinite(handle_stats.first_handle_y) ||
            !std::isfinite(handle_stats.second_handle_x) ||
            !std::isfinite(handle_stats.second_handle_y) ||
            handle_stats.handle_flat_value_span ||
            handle_stats.active_segment_kind !=
                timeline_graph_model::SegmentKind::Linear ||
            handle_stats.handle_gesture_active ||
            handle_stats.component_controls_disabled) {
            std::cerr << "Actual-frame graph smoke did not draw the active key handles: drawn="
                      << handle_stats.handles_drawn
                      << " kind=" << static_cast<int>(handle_stats.active_segment_kind)
                      << " first=(" << handle_stats.first_handle_x << ","
                      << handle_stats.first_handle_y << ").\n";
            return false;
        }
        const ImVec2 handle_press{
            handle_stats.first_handle_x, handle_stats.first_handle_y};
        if (handle_press.x < handle_window->InnerClipRect.Min.x + 1.0f ||
            handle_press.x > handle_window->InnerClipRect.Max.x - 41.0f ||
            handle_press.y < handle_window->InnerClipRect.Min.y + 1.0f ||
            handle_press.y > handle_window->InnerClipRect.Max.y - 41.0f) {
            std::cerr << "Actual-frame handle smoke could not reach a visible handle: point=("
                      << handle_press.x << "," << handle_press.y << ") clip=("
                      << handle_window->InnerClipRect.Min.x << ","
                      << handle_window->InnerClipRect.Min.y << ")-("
                      << handle_window->InnerClipRect.Max.x << ","
                      << handle_window->InnerClipRect.Max.y << ") plot=("
                      << handle_stats.plot_min_x << "," << handle_stats.plot_min_y
                      << ")-(" << handle_stats.plot_max_x << ","
                      << handle_stats.plot_max_y << ") second=("
                      << handle_stats.second_handle_x << ","
                      << handle_stats.second_handle_y << ").\n";
            return false;
        }

        const std::string handle_project_before =
            marrow::editor::serialize_project(*shell_state.session.project());
        const std::size_t handle_undo_before = shell_state.session.undo_count();
        const std::uint64_t handle_revision_before =
            shell_state.session.project_revision();
        const auto visible_before =
            shell_state.timeline_editor.graph_view.component_visible;

        io.AddMousePosEvent(handle_press.x, handle_press.y);
        render_graph_frame(nullptr);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_graph_frame(&handle_stats);
        if (!handle_stats.drag_active || handle_stats.handle_gesture_active ||
            shell_state.session.undo_count() != handle_undo_before) {
            std::cerr << "An actual-frame handle press did not arm a candidate without a transaction.\n";
            return false;
        }
        const auto view_before_handle_drag =
            shell_state.timeline_editor.graph_view.view;
        io.AddMousePosEvent(handle_press.x + 26.0f, handle_press.y - 34.0f);
        render_graph_frame(&handle_stats);
        if (!handle_stats.handle_gesture_active ||
            shell_state.timeline_editor.graph_value_gesture.has_value() ||
            shell_state.timeline_editor.retime_gesture.has_value() ||
            !handle_stats.component_controls_disabled ||
            // MAR-170: the preset row and the Default combo are inert while a
            // handle drag owns the session.
            handle_stats.curve_preset_row_enabled) {
            std::cerr << "An actual-frame handle drag did not open the handle gesture: handle="
                      << handle_stats.handle_gesture_active
                      << " value=" << handle_stats.value_gesture_active
                      << " retime=" << handle_stats.graph_owns_retime
                      << " disabled=" << handle_stats.component_controls_disabled << ".\n";
            return false;
        }
        // The view stays frozen and the component visibility cannot change
        // while the handle drag is live.
        io.AddMouseWheelEvent(0.0f, -1.0f);
        io.AddMousePosEvent(handle_press.x + 30.0f, handle_press.y - 38.0f);
        render_graph_frame(&handle_stats);
        shell_state.timeline_editor.graph_view.needs_fit = true;
        io.AddMousePosEvent(handle_press.x + 34.0f, handle_press.y - 42.0f);
        render_graph_frame(&handle_stats);
        io.AddMouseButtonEvent(ImGuiMouseButton_Middle, true);
        io.AddMousePosEvent(handle_press.x + 48.0f, handle_press.y - 50.0f);
        render_graph_frame(&handle_stats);
        io.AddMouseButtonEvent(ImGuiMouseButton_Middle, false);
        render_graph_frame(&handle_stats);
        const auto view_after_handle_drag =
            shell_state.timeline_editor.graph_view.view;
        if (view_after_handle_drag.pixels_per_second !=
                view_before_handle_drag.pixels_per_second ||
            view_after_handle_drag.pixels_per_value !=
                view_before_handle_drag.pixels_per_value ||
            view_after_handle_drag.view_start_seconds !=
                view_before_handle_drag.view_start_seconds ||
            view_after_handle_drag.value_center !=
                view_before_handle_drag.value_center ||
            !shell_state.timeline_editor.graph_view.needs_fit ||
            shell_state.timeline_editor.graph_view.component_visible !=
                visible_before) {
            std::cerr << "A live handle drag did not freeze its view or component visibility.\n";
            return false;
        }

        // Switching to the Dopesheet mid-drag cancels with a full rollback.
        shell_state.timeline_editor.requested_view_mode = TimelineViewMode::Dopesheet;
        render_graph_frame(nullptr);
        render_graph_frame(nullptr);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_graph_frame(nullptr);
        if (shell_state.timeline_editor.graph_drag.has_value() ||
            shell_state.timeline_editor.graph_handle_gesture.has_value() ||
            authoring_gesture_active(shell_state) ||
            marrow::editor::serialize_project(*shell_state.session.project()) !=
                handle_project_before ||
            shell_state.session.undo_count() != handle_undo_before ||
            shell_state.session.project_revision() != handle_revision_before) {
            std::cerr << "A mid-drag Dopesheet tab switch did not cancel the handle gesture with rollback.\n";
            return false;
        }
        // The dopesheet retime lane still works afterwards.
        reset_handle_selection();
        if (!begin_timeline_retime_gesture(
                &shell_state, 778U, 0.0f, cached_timeline_tracks(&shell_state)) ||
            !apply_timeline_retime_delta(
                &shell_state, cached_timeline_tracks(&shell_state), 0.1, true)) {
            std::cerr << "The dopesheet retime lane did not work after a cancelled handle drag.\n";
            return false;
        }
        finish_timeline_retime_gesture(&shell_state, true);
        if (authoring_gesture_active(shell_state) ||
            shell_state.session.undo_count() != handle_undo_before + 1U) {
            std::cerr << "The dopesheet retime lane did not commit after a cancelled handle drag.\n";
            return false;
        }
        if (!shell_state.session.undo()) return false;
        sync_shell_from_editor_session(&shell_state);
        shell_state.session.clear_history();
        reconcile_timeline_key_selection(
            &shell_state, cached_timeline_tracks(&shell_state));
        if (marrow::editor::serialize_project(*shell_state.session.project()) !=
            handle_project_before) {
            std::cerr << "Actual-frame handle smoke did not restore its project bytes.\n";
            return false;
        }

        // A committed handle drag: press the drawn handle, move, release.
        shell_state.selected_timeline_track_id = handle_track_id;
        shell_state.timeline_editor.requested_view_mode = TimelineViewMode::Graph;
        render_graph_frame(nullptr);
        reset_handle_selection();
        render_graph_frame(&handle_stats);
        reset_handle_selection();
        render_graph_frame(&handle_stats);
        if (!handle_stats.handles_drawn) {
            std::cerr << "Actual-frame handle commit smoke lost its handles.\n";
            return false;
        }
        const ImVec2 commit_press{
            handle_stats.first_handle_x, handle_stats.first_handle_y};
        const std::size_t commit_undo_before = shell_state.session.undo_count();
        io.AddMousePosEvent(commit_press.x, commit_press.y);
        render_graph_frame(nullptr);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_graph_frame(&handle_stats);
        io.AddMousePosEvent(commit_press.x + 22.0f, commit_press.y - 28.0f);
        render_graph_frame(&handle_stats);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_graph_frame(&handle_stats);
        if (handle_stats.drag_active || handle_stats.handle_gesture_active ||
            shell_state.session.undo_count() != commit_undo_before + 1U ||
            handle_stats.active_segment_kind !=
                timeline_graph_model::SegmentKind::Cubic) {
            std::cerr << "Releasing an actual-frame handle drag did not commit one cubic entry: undo="
                      << shell_state.session.undo_count() << "/" << commit_undo_before
                      << " kind=" << static_cast<int>(handle_stats.active_segment_kind)
                      << ".\n";
            return false;
        }
        // Pressing a key point where no handle is within its radius still
        // opens MAR-168's point path.
        reset_handle_selection();
        render_graph_frame(&handle_stats);
        if (!handle_stats.active_point_valid) {
            std::cerr << "Actual-frame handle smoke lost its active point.\n";
            return false;
        }
        const ImVec2 point_press{
            handle_stats.active_point_x, handle_stats.active_point_y};
        io.AddMousePosEvent(point_press.x, point_press.y);
        render_graph_frame(nullptr);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_graph_frame(&handle_stats);
        io.AddMousePosEvent(point_press.x, point_press.y - 40.0f);
        render_graph_frame(&handle_stats);
        const bool point_path_won = handle_stats.value_gesture_active &&
            !handle_stats.handle_gesture_active;
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_graph_frame(nullptr);
        if (!point_path_won) {
            std::cerr << "A press on a key point away from every handle did not open "
                         "the MAR-168 point path.\n";
            return false;
        }
        while (shell_state.session.undo_count() > 0U) {
            if (!shell_state.session.undo()) break;
        }
        sync_shell_from_editor_session(&shell_state);
        shell_state.session.clear_history();
        reconcile_timeline_key_selection(
            &shell_state, cached_timeline_tracks(&shell_state));
        std::cout << "Timeline Graph actual-frame handles: first=("
                  << commit_press.x << "," << commit_press.y << ") second=("
                  << handle_stats.second_handle_x << ","
                  << handle_stats.second_handle_y << ").\n";
        shell_state.timeline_editor.requested_view_mode = TimelineViewMode::Graph;
        render_graph_frame(nullptr);
    }

    // --- MAR-170: the appended preset row and Default combo, driven with real
    // ImGui mouse events aimed at real submitted button coordinates. ---
    {
        const TimelineTrackRow* preset_row = find_timeline_track(
            cached_timeline_tracks(&shell_state), "bone:1:Translate");
        if (preset_row == nullptr || preset_row->key_times.size() < 2U) {
            std::cerr << "Actual-frame preset smoke lost its Translate row.\n";
            return false;
        }
        const TimelineKeyRef preset_key = timeline_model::key_ref(*preset_row, 0U);
        shell_state.selected_timeline_track_id = preset_row->id;
        shell_state.timeline_editor.requested_view_mode = TimelineViewMode::Graph;
        const auto select_preset_key = [&]() {
            shell_state.timeline_editor.selected_keys = {preset_key};
            shell_state.timeline_editor.active_key = preset_key;
        };
        select_preset_key();
        shell_state.preferences.default_curve = marrow::editor::CurvePreset::EaseOut;

        TimelineGraphRenderStats preset_stats;
        render_graph_frame(nullptr);
        select_preset_key();
        render_graph_frame(&preset_stats);

        ImGuiWindow* preset_window = ImGui::FindWindowByName(kTimelineWindowTitle);
        if (preset_window == nullptr || !preset_stats.curve_preset_row_drawn ||
            !preset_stats.curve_preset_row_enabled ||
            !std::isfinite(preset_stats.first_preset_min_x) ||
            !std::isfinite(preset_stats.first_preset_min_y) ||
            preset_stats.first_preset_max_x <= preset_stats.first_preset_min_x ||
            preset_stats.first_preset_max_y <= preset_stats.first_preset_min_y) {
            std::cerr << "The Graph preset row was not drawn with a usable rectangle: drawn="
                      << preset_stats.curve_preset_row_drawn
                      << " enabled=" << preset_stats.curve_preset_row_enabled
                      << " rect=(" << preset_stats.first_preset_min_x << ","
                      << preset_stats.first_preset_min_y << ")-("
                      << preset_stats.first_preset_max_x << ","
                      << preset_stats.first_preset_max_y << ").\n";
            return false;
        }
        // The default combo reports the remembered preset, and applying a
        // preset must never change it.
        if (preset_stats.default_preset_index !=
            static_cast<std::size_t>(marrow::editor::CurvePreset::EaseOut)) {
            std::cerr << "The Default combo did not report the remembered curve.\n";
            return false;
        }
        // Appending the row must not have displaced MAR-167/168's widgets or
        // pushed the plot out of the Timeline window.
        if (preset_stats.fit_max_x <= preset_stats.fit_min_x ||
            preset_stats.fit_max_y <= preset_stats.fit_min_y ||
            preset_stats.first_component_max_x <= preset_stats.first_component_min_x ||
            preset_stats.first_component_max_y <= preset_stats.first_component_min_y ||
            preset_stats.plot_min_y >= preset_stats.plot_max_y ||
            preset_stats.plot_max_y > preset_window->InnerClipRect.Max.y +
                preset_window->ScrollMax.y + 1.0f) {
            std::cerr << "Appending the preset row displaced an existing Graph widget.\n";
            return false;
        }
        // Fit and the first component checkbox are still hoverable at their
        // reported rectangles.
        io.AddMousePosEvent(
            (preset_stats.fit_min_x + preset_stats.fit_max_x) * 0.5f,
            (preset_stats.fit_min_y + preset_stats.fit_max_y) * 0.5f);
        render_graph_frame(nullptr);
        if (!ImGui::IsMouseHoveringRect(
                ImVec2(preset_stats.fit_min_x, preset_stats.fit_min_y),
                ImVec2(preset_stats.fit_max_x, preset_stats.fit_max_y),
                false)) {
            std::cerr << "The Fit button was no longer hoverable after the preset row.\n";
            return false;
        }

        // Seed a non-Linear curve through the controller so the first preset
        // button (Linear) is a genuine change when it is clicked.
        select_preset_key();
        const auto seeded = apply_timeline_curve_preset(
            &shell_state,
            cached_timeline_tracks(&shell_state),
            marrow::editor::CurvePreset::EaseInOut);
        if (!seeded.applied || seeded.changed_key_count != 1U) {
            std::cerr << "Actual-frame preset smoke could not seed a non-Linear curve.\n";
            return false;
        }
        select_preset_key();
        render_graph_frame(&preset_stats);
        if (preset_stats.active_preset_index !=
            static_cast<std::size_t>(marrow::editor::CurvePreset::EaseInOut)) {
            std::cerr << "The Outgoing readout did not report the applied preset.\n";
            return false;
        }

        const std::size_t preset_undo_before = shell_state.session.undo_count();
        const ImVec2 preset_click{
            (preset_stats.first_preset_min_x + preset_stats.first_preset_max_x) * 0.5f,
            (preset_stats.first_preset_min_y + preset_stats.first_preset_max_y) * 0.5f};
        io.AddMousePosEvent(preset_click.x, preset_click.y);
        render_graph_frame(nullptr);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_graph_frame(nullptr);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        // A SmallButton fires during the release frame, and the row publishes
        // its readout before the buttons are submitted, so the settled preset
        // is only visible one frame later.
        render_graph_frame(nullptr);
        select_preset_key();
        render_graph_frame(&preset_stats);
        if (shell_state.session.undo_count() != preset_undo_before + 1U ||
            preset_stats.active_preset_index !=
                static_cast<std::size_t>(marrow::editor::CurvePreset::Linear)) {
            std::cerr << "Clicking the first preset button did not commit exactly one "
                         "Linear entry: undo=" << shell_state.session.undo_count() << "/"
                      << preset_undo_before
                      << " preset=" << preset_stats.active_preset_index << ".\n";
            return false;
        }
        // Applying a preset must not touch the remembered default.
        if (shell_state.preferences.default_curve !=
            marrow::editor::CurvePreset::EaseOut) {
            std::cerr << "Applying a preset changed the remembered default curve.\n";
            return false;
        }

        // With nothing selected the row is disabled and a click is inert.
        shell_state.timeline_editor.selected_keys.clear();
        shell_state.timeline_editor.active_key.reset();
        render_graph_frame(&preset_stats);
        const std::size_t empty_undo_before = shell_state.session.undo_count();
        const std::string empty_project_before =
            marrow::editor::serialize_project(*shell_state.session.project());
        if (!preset_stats.curve_preset_row_drawn ||
            preset_stats.curve_preset_row_enabled) {
            std::cerr << "An empty selection must leave the preset row drawn but "
                         "disabled.\n";
            return false;
        }
        io.AddMousePosEvent(
            (preset_stats.first_preset_min_x + preset_stats.first_preset_max_x) * 0.5f,
            (preset_stats.first_preset_min_y + preset_stats.first_preset_max_y) * 0.5f);
        render_graph_frame(nullptr);
        render_graph_frame(nullptr);
        // The disabled row must teach its constraint on EVERY button, not only
        // on the last one: reading the hover state after EndDisabled() tested
        // the final SmallButton and overwrote that button's own tooltip.
        // A tooltip window persists once created, so this asserts it was
        // submitted in the frame just rendered rather than merely existing.
        {
            const ImGuiWindow* disabled_tooltip =
                ImGui::FindWindowByName("##Tooltip_00");
            if (disabled_tooltip == nullptr ||
                disabled_tooltip->LastFrameActive != ImGui::GetFrameCount()) {
                std::cerr << "Hovering the first disabled preset button produced no "
                             "guidance tooltip.\n";
                return false;
            }
        }
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_graph_frame(nullptr);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_graph_frame(&preset_stats);
        if (shell_state.session.undo_count() != empty_undo_before ||
            marrow::editor::serialize_project(*shell_state.session.project()) !=
                empty_project_before) {
            std::cerr << "A click on the disabled preset row changed the project.\n";
            return false;
        }

        while (shell_state.session.undo_count() > 0U) {
            if (!shell_state.session.undo()) break;
        }
        sync_shell_from_editor_session(&shell_state);
        shell_state.session.clear_history();
        reconcile_timeline_key_selection(
            &shell_state, cached_timeline_tracks(&shell_state));
        std::cout << "Timeline Graph actual-frame presets: first button=("
                  << preset_click.x << "," << preset_click.y << ") default="
                  << preset_stats.default_preset_index << ".\n";
    }

    // --- MAR-171: the appended curve-mode row and the auto handle overlay,
    // driven with real ImGui mouse events aimed at real submitted coordinates.
    {
        const TimelineTrackRow* mode_row = find_timeline_track(
            cached_timeline_tracks(&shell_state), "bone:1:Translate");
        if (mode_row == nullptr || mode_row->key_times.size() < 2U) {
            std::cerr << "Actual-frame curve-mode smoke lost its Translate row.\n";
            return false;
        }
        const TimelineKeyRef mode_key = timeline_model::key_ref(*mode_row, 0U);
        shell_state.selected_timeline_track_id = mode_row->id;
        shell_state.timeline_editor.requested_view_mode = TimelineViewMode::Graph;
        const auto select_mode_key = [&]() {
            shell_state.timeline_editor.selected_keys = {mode_key};
            shell_state.timeline_editor.active_key = mode_key;
        };
        select_mode_key();

        TimelineGraphRenderStats mode_stats;
        render_graph_frame(nullptr);
        select_mode_key();
        render_graph_frame(&mode_stats);

        // The appended row displaced nothing: every rectangle MAR-167 through
        // MAR-170 aim at is still finite and still inside the plot's window.
        if (!mode_stats.curve_mode_row_drawn || !mode_stats.curve_mode_row_enabled ||
            !std::isfinite(mode_stats.first_curve_mode_min_x) ||
            !std::isfinite(mode_stats.first_curve_mode_min_y) ||
            mode_stats.first_curve_mode_max_x <= mode_stats.first_curve_mode_min_x ||
            mode_stats.auto_curve_mode_max_x <= mode_stats.auto_curve_mode_min_x ||
            !std::isfinite(mode_stats.fit_min_x) ||
            !std::isfinite(mode_stats.first_component_min_x) ||
            !std::isfinite(mode_stats.first_preset_min_x) ||
            mode_stats.fit_max_x <= mode_stats.fit_min_x ||
            mode_stats.first_component_max_x <= mode_stats.first_component_min_x ||
            mode_stats.first_preset_max_x <= mode_stats.first_preset_min_x ||
            mode_stats.plot_max_x <= mode_stats.plot_min_x) {
            std::cerr << "The curve-mode row was not drawn beside every earlier "
                         "toolbar rectangle.\n";
            return false;
        }

        // Clicking the reported `Auto` button commits exactly one entry and the
        // next frame reports the active key as automatic.
        const std::size_t mode_undo_before = shell_state.session.undo_count();
        const ImVec2 auto_click{
            (mode_stats.auto_curve_mode_min_x + mode_stats.auto_curve_mode_max_x) * 0.5f,
            (mode_stats.auto_curve_mode_min_y + mode_stats.auto_curve_mode_max_y) * 0.5f};
        io.AddMousePosEvent(auto_click.x, auto_click.y);
        render_graph_frame(nullptr);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_graph_frame(nullptr);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        // A SmallButton fires during the release frame and the row publishes its
        // readout before the buttons are submitted, so the settled mode is only
        // visible one frame later.
        render_graph_frame(nullptr);
        select_mode_key();
        render_graph_frame(&mode_stats);
        if (shell_state.session.undo_count() != mode_undo_before + 1U ||
            !mode_stats.active_key_auto) {
            std::cerr << "Clicking Auto did not commit exactly one automatic entry: undo="
                      << shell_state.session.undo_count() << "/" << mode_undo_before
                      << " auto=" << mode_stats.active_key_auto << ".\n";
            return false;
        }
        {
            const auto* edit = shell_state.session.project()
                ->find_transform_timeline_edit(
                    "idle", "spine",
                    marrow::editor::TransformTimelineChannel::Translate);
            if (edit == nullptr || edit->keyframes.empty() ||
                edit->keyframes.front().curve_mode !=
                    marrow::editor::TimelineCurveMode::Auto ||
                edit->keyframes.front().interpolation.kind() !=
                    marrow::runtime::InterpolationKind::CubicBezier ||
                edit->keyframes.front().interpolation.cubic_bezier().cx1 !=
                    static_cast<marrow::runtime::AnimationScalar>(1.0 / 3.0) ||
                edit->keyframes.front().interpolation.cubic_bezier().cx2 !=
                    static_cast<marrow::runtime::AnimationScalar>(2.0 / 3.0)) {
                std::cerr << "The Auto button did not store a resolved automatic curve.\n";
                return false;
            }
        }

        // Pressing the reported auto handle, moving 30 px, and releasing demotes
        // the segment in exactly one entry.
        if (!mode_stats.handles_drawn ||
            !std::isfinite(mode_stats.first_handle_x) ||
            !std::isfinite(mode_stats.first_handle_y)) {
            std::cerr << "An automatic key did not publish its handle coordinates.\n";
            return false;
        }
        const std::size_t drag_undo_before = shell_state.session.undo_count();
        io.AddMousePosEvent(mode_stats.first_handle_x, mode_stats.first_handle_y);
        render_graph_frame(nullptr);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_graph_frame(nullptr);
        io.AddMousePosEvent(
            mode_stats.first_handle_x + 30.0f, mode_stats.first_handle_y - 30.0f);
        render_graph_frame(nullptr);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_graph_frame(nullptr);
        select_mode_key();
        render_graph_frame(&mode_stats);
        if (shell_state.session.undo_count() != drag_undo_before + 1U ||
            mode_stats.active_key_auto) {
            std::cerr << "Dragging an auto handle did not demote it in exactly one "
                         "entry: undo=" << shell_state.session.undo_count() << "/"
                      << drag_undo_before << " auto=" << mode_stats.active_key_auto
                      << ".\n";
            return false;
        }

        // With nothing selected the row is disabled and a click is inert.
        shell_state.timeline_editor.selected_keys.clear();
        shell_state.timeline_editor.active_key.reset();
        render_graph_frame(&mode_stats);
        const std::size_t inert_undo_before = shell_state.session.undo_count();
        const std::string inert_project_before =
            marrow::editor::serialize_project(*shell_state.session.project());
        if (!mode_stats.curve_mode_row_drawn || mode_stats.curve_mode_row_enabled) {
            std::cerr << "An empty selection must leave the curve-mode row drawn but "
                         "disabled.\n";
            return false;
        }
        io.AddMousePosEvent(
            (mode_stats.auto_curve_mode_min_x + mode_stats.auto_curve_mode_max_x) * 0.5f,
            (mode_stats.auto_curve_mode_min_y + mode_stats.auto_curve_mode_max_y) * 0.5f);
        render_graph_frame(nullptr);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_graph_frame(nullptr);
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_graph_frame(&mode_stats);
        if (shell_state.session.undo_count() != inert_undo_before ||
            marrow::editor::serialize_project(*shell_state.session.project()) !=
                inert_project_before) {
            std::cerr << "A click on the disabled curve-mode row changed the project.\n";
            return false;
        }

        while (shell_state.session.undo_count() > 0U) {
            if (!shell_state.session.undo()) break;
        }
        sync_shell_from_editor_session(&shell_state);
        shell_state.session.clear_history();
        reconcile_timeline_key_selection(
            &shell_state, cached_timeline_tracks(&shell_state));
        std::cout << "Timeline Graph actual-frame curve mode: Auto button=("
                  << auto_click.x << "," << auto_click.y << ") driver="
                  << mode_stats.active_driver_index << ".\n";
    }

    // MAR-159: the anchor resets only when filter/tree-collapse removes it
    // from the visible order. A Hierarchy window whose dock tab is hidden
    // renders no rows at all; that degenerate frame must not clear it.
    {
        const auto& smoke_skeleton = *shell_state.load_result.skeleton_data;
        std::optional<std::string> child_bone_name;
        for (const auto& bone : smoke_skeleton.bones()) {
            if (bone.parent_index.has_value()) {
                child_bone_name = bone.name;
                break;
            }
        }
        if (!child_bone_name.has_value()) {
            std::cerr << "Hidden-tab anchor smoke requires a child bone in the fixture.\n";
            return false;
        }
        shell_state.hierarchy_selection_anchor =
            marrow::editor::BoneSelection{*child_bone_name};
        ImGui::DockBuilderDockWindow(
            kPropertiesWindowTitle, shell_state.dock_layout.hierarchy_node_id);
        for (int hidden_frame = 0; hidden_frame < 2; ++hidden_frame) {
            io.DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            ImGui::DockSpaceOverViewport(0U, ImGui::GetMainViewport());
            draw_hierarchy_window(&shell_state);
            draw_inspector_window(&shell_state);
            if (hidden_frame == 0) {
                ImGui::SetWindowFocus(kPropertiesWindowTitle);
            }
            ImGui::Render();
        }
        if (!shell_state.hierarchy_selection_anchor.has_value()) {
            std::cerr << "Hidden hierarchy dock tab cleared the selection anchor.\n";
            return false;
        }
        shell_state.hierarchy_selection_anchor.reset();
    }

    // --- MAR-185 F1: the Mode combo is ON SCREEN, found by a real mouse. ---
    //
    // A UI-free helper cannot observe a deleted widget: calling the function a
    // combo calls asserts the handler, not the combo, and such a case passes
    // unchanged after the widget is removed. AGENTS.md records exactly that
    // failure for MAR-178, whose own scenario printed success while the frame
    // smoke failed by name. S1-S6 are all UI-free; this is the only case that
    // can see the widget.
    {
        // `player_idle` carries no inherit timeline at all, so the lane has to
        // be authored first. Two keys, and NOT a lone {0.0, normal}: every
        // player_idle bone is setup-`Normal` and the runtime prunes an inherit
        // lane of exactly one origin key matching the bone's setup inherit.
        {
            auto transaction = shell_state.session.begin_edit({
                marrow::editor::EditKind::AddKeyframe,
                "Seed an inherit lane for the frame smoke",
                "mar185:frames",
                false,
                marrow::editor::EditImpact::Project |
                    marrow::editor::EditImpact::Runtime |
                    marrow::editor::EditImpact::Preview});
            if (!transaction) {
                std::cerr << "MAR-185 F1: could not open a transaction.\n";
                return false;
            }
            marrow::editor::InheritTimelineMergeRequest request;
            request.animation_name = "idle";
            request.bone_name = "spine";
            request.keys = {{0.25, "noScale"}, {0.6, "normal"}};
            const auto merged = marrow::editor::merge_inherit_timeline(
                transaction.project(), *shell_state.session.runtime_data(), request);
            if (!merged || !merged.changed) {
                transaction.cancel();
                std::cerr << "MAR-185 F1: seeding the inherit lane failed: "
                          << merged.error << '\n';
                return false;
            }
            if (!transaction.commit()) {
                std::cerr << "MAR-185 F1: the seeding transaction did not commit.\n";
                return false;
            }
            sync_shell_from_editor_session(&shell_state);
        }

        const auto spine_index =
            shell_state.load_result.skeleton_data->find_bone_index("spine");
        if (!spine_index.has_value()) {
            std::cerr << "MAR-185 F1: the fixture lost its spine bone.\n";
            return false;
        }
        const std::string inherit_track_id =
            "bone:" + std::to_string(*spine_index) + ":Inherit";
        shell_state.timeline_editor.requested_view_mode = TimelineViewMode::Dopesheet;
        shell_state.selected_timeline_track_id = inherit_track_id;
        shell_state.timeline_editor.selected_keys.clear();
        shell_state.timeline_editor.active_key.reset();

        // `SetWindowFocus` is deliberately confined to the opening frames.
        // Forcing focus onto the Timeline window while a combo popup is open
        // CLOSES the popup, so the item sweep below would find an empty popup
        // and report "the combo never hovered its item" for the wrong reason.
        bool focus_timeline = true;
        const auto render_timeline_frame = [&]() {
            io.DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            if (focus_timeline) ImGui::SetWindowFocus(kTimelineWindowTitle);
            draw_timeline_window(&shell_state, nullptr);
            ImGui::Render();
        };
        render_timeline_frame();
        render_timeline_frame();

        ImGuiWindow* timeline_window = ImGui::FindWindowByName(kTimelineWindowTitle);
        if (timeline_window == nullptr) {
            std::cerr << "MAR-185 F1: no Timeline window.\n";
            return false;
        }
        // The key editor is emitted after the dopesheet table, so it starts
        // below the fold; scroll to the bottom before aiming a mouse at it.
        // `ScrollMax` is written in `Begin()` (`imgui.cpp:8393-8394`) from
        // `window->ContentSize`, which the PREVIOUS frame's `End()` finalised --
        // so the input is one frame behind, and on the first pass after a
        // layout change it still reads 0. A loop that breaks on
        // `ScrollMax == Scroll` gives up before the window has ever reported
        // its real extent, which is how this case first read "widget absent".
        for (int attempt = 0; attempt < 32; ++attempt) {
            timeline_window = ImGui::FindWindowByName(kTimelineWindowTitle);
            if (timeline_window == nullptr) break;
            const float target = timeline_window->ScrollMax.y > 0.0f
                ? timeline_window->ScrollMax.y
                : timeline_window->Scroll.y + 200.0f;
            if (timeline_window->ScrollMax.y > 0.0f &&
                timeline_window->Scroll.y >= timeline_window->ScrollMax.y) {
                break;
            }
            ImGui::SetScrollY(timeline_window, target);
            render_timeline_frame();
        }
        timeline_window = ImGui::FindWindowByName(kTimelineWindowTitle);
        if (timeline_window == nullptr) {
            std::cerr << "MAR-185 F1: lost the Timeline window while scrolling.\n";
            return false;
        }

        // The two seeds. `Time` is swept in the SAME pass and must also be
        // found: without it, a broken `GetID` seed -- a surviving `PushID`, a
        // wrapping `BeginChild`, a `BeginTabItem` -- is indistinguishable from
        // a deleted combo, and the case would report the wrong defect.
        // The seed is NOT `window->GetID(label)`. `draw_timeline_window` wraps
        // the dopesheet in `BeginTabBar("timeline_views")` +
        // `BeginTabItem("Dopesheet")`, and `BeginTabItem` pushes the tab's id,
        // so every widget below it is hashed against that instead of the
        // window root. This is the exact hazard AGENTS.md's Headless Frame
        // Smoke Notes records; the `Time` half of this case is what turned a
        // silent "widget absent" into a diagnosable broken seed. The chain
        // below reproduces `TabBarCalcTabID` for a non-docked tab.
        const ImGuiID tab_bar_id = timeline_window->GetID("timeline_views");
        const ImGuiID dopesheet_tab_id = ImHashStr("Dopesheet", 0, tab_bar_id);
        const ImGuiID mode_id = ImHashStr("Mode##inherit0", 0, dopesheet_tab_id);
        const ImGuiID time_id = ImHashStr("Time##inherit0", 0, dopesheet_tab_id);
        ImVec2 mode_position{0.0f, 0.0f};
        ImVec2 time_position{0.0f, 0.0f};
        bool found_mode = false;
        bool found_time = false;
        const float min_x = timeline_window->InnerClipRect.Min.x + 2.0f;
        const float max_x = timeline_window->InnerClipRect.Max.x - 2.0f;
        const float min_y = timeline_window->InnerClipRect.Min.y + 2.0f;
        const float max_y = timeline_window->InnerClipRect.Max.y - 2.0f;
        for (float y = min_y; y <= max_y && !(found_mode && found_time); y += 4.0f) {
            // Two columns: a left-edge column reaches a full-width DragScalar
            // and a combo, but a widget sharing a `SameLine()` row would be
            // missed by one column alone.
            for (const float x : {min_x + 24.0f, (min_x + max_x) * 0.5f}) {
                io.AddMousePosEvent(x, y);
                render_timeline_frame();
                const ImGuiID hovered = ImGui::GetCurrentContext()->HoveredId;
                if (hovered == mode_id && !found_mode) {
                    found_mode = true;
                    mode_position = ImVec2(x, y);
                }
                if (hovered == time_id && !found_time) {
                    found_time = true;
                    time_position = ImVec2(x, y);
                }
            }
        }
        if (!found_mode) {
            std::cerr << "MAR-185 F1: no widget in the Timeline window matched "
                         "GetID(\"Mode##inherit0\") across the sweep"
                      << (found_time
                              ? " (the control 'Time' matched at x=" +
                                  std::to_string(time_position.x) +
                                  ", so the seed is sound)"
                              : " -- and 'Time' did not match either, so the "
                                "GetID seed is broken rather than the combo "
                                "missing")
                      << ".\n";
            return false;
        }
        if (!found_time) {
            std::cerr << "MAR-185 F1: the Mode combo was found but the Time drag "
                         "of the same key was not, so the sweep is not covering "
                         "the key editor as intended.\n";
            return false;
        }

        // Open the combo and click `noScale` with a real mouse. Two presses at
        // one pixel inside `io.MouseDoubleClickTime` are a DOUBLE click, which
        // ImGui turns into something else entirely, so the clock is advanced
        // past it between gestures.
        const auto stored_mode = [&]() -> std::optional<marrow::runtime::BoneInherit> {
            const auto* edit =
                shell_state.session.project()->find_bone_inherit_timeline_edit(
                    "idle", "spine");
            if (edit == nullptr || edit->keyframes.empty()) return std::nullopt;
            return edit->keyframes.front().inherit;
        };
        if (stored_mode() != marrow::runtime::BoneInherit::NoScale) {
            std::cerr << "MAR-185 F1: the seeded lane does not start at noScale.\n";
            return false;
        }
        const std::size_t undo_before = shell_state.session.undo_count();
        focus_timeline = false;
        io.AddMousePosEvent(mode_position.x, mode_position.y);
        render_timeline_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
        render_timeline_frame();
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        render_timeline_frame();

        // Let the popup settle before reading its rectangle: on the frame it
        // opens, `InnerClipRect` is still the pre-layout value, and a sweep
        // bounded by it walks straight past the item list.
        render_timeline_frame();
        const auto find_combo_popup = [&]() -> ImGuiWindow* {
            for (ImGuiWindow* window : ImGui::GetCurrentContext()->Windows) {
                if (window != nullptr && window->Active &&
                    std::strstr(window->Name, "##Combo_") != nullptr) {
                    return window;
                }
            }
            return nullptr;
        };
        ImGuiWindow* popup = find_combo_popup();
        if (popup == nullptr) {
            std::cerr << "MAR-185 F1: clicking the Mode combo opened no popup.\n";
            return false;
        }
        const float popup_min_y = popup->InnerClipRect.Min.y + 4.0f;
        const float popup_max_y = popup->InnerClipRect.Max.y - 4.0f;
        const float popup_x =
            (popup->InnerClipRect.Min.x + popup->InnerClipRect.Max.x) * 0.5f;
        const ImGuiID item_id = popup->GetID("normal");
        bool clicked_item = false;
        for (float y = popup_min_y; y <= popup_max_y && !clicked_item; y += 3.0f) {
            io.DeltaTime = static_cast<float>(io.MouseDoubleClickTime) * 2.0f;
            io.AddMousePosEvent(popup_x, y);
            render_timeline_frame();
            if (find_combo_popup() == nullptr) break;
            if (ImGui::GetCurrentContext()->HoveredId != item_id) continue;
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, true);
            render_timeline_frame();
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
            render_timeline_frame();
            clicked_item = true;
        }
        if (!clicked_item) {
            std::cerr << "MAR-185 F1: the open combo never hovered its 'normal' "
                         "item across y=" << popup_min_y << ".." << popup_max_y
                      << " at x=" << popup_x << ".\n";
            return false;
        }
        if (stored_mode() != marrow::runtime::BoneInherit::Normal) {
            std::cerr << "MAR-185 F1: clicking 'normal' in the Mode combo did not "
                         "change the stored mode.\n";
            return false;
        }
        if (shell_state.session.undo_count() != undo_before + 1U) {
            std::cerr << "MAR-185 F1: the combo click recorded "
                      << (shell_state.session.undo_count() - undo_before)
                      << " history entries, expected 1.\n";
            return false;
        }
        std::cout << "MAR-185 F1: the Mode combo is on screen at ("
                  << mode_position.x << "," << mode_position.y
                  << ") with the same key's Time drag at (" << time_position.x
                  << "," << time_position.y
                  << "), and clicking it commits one history entry.\n";

        while (shell_state.session.undo_count() > 0U) {
            if (!shell_state.session.undo()) break;
        }
        sync_shell_from_editor_session(&shell_state);
        shell_state.session.clear_history();
    }


    // --- MAR-187 F1: the Problems window is ON SCREEN, driven by a real mouse.
    //
    // The ONLY case in this story that can observe a widget. S1-S6 call
    // `activate_problem_row` and `apply_safe_fix` directly, so they assert the
    // ROUTER; every one of them passes unchanged if `draw_problems_window`'s
    // body is deleted. AGENTS.md records the MAR-178 worked example where
    // exactly that left a story's own scenario printing its success line.
    //
    // It is also the only detector for one real defect: wiring the Fix button to
    // `normalize_weights_command` instead of `apply_safe_fix`. That command's
    // scope is EMPTY -- meaning every vertex -- unless an FFD selection narrows
    // it, so it would repair a second vertex the user never saw a row for. No
    // UI-free case reaches the button, so no UI-free case can see it.
    {
        // Two non-canonical vertices. Repairing the FIRST must leave the SECOND
        // reported -- that difference is the whole detector.
        {
            const auto body_slot =
                shell_state.load_result.skeleton_data->find_slot_index("body");
            const auto* attachment = body_slot.has_value()
                ? shell_state.load_result.skeleton_data->find_attachment(
                      "mesh_base", *body_slot, "body_mesh")
                : nullptr;
            if (attachment == nullptr) {
                std::cerr << "MAR-187 F1: the fixture lost mesh_base/body/body_mesh.\n";
                return false;
            }
            marrow::editor::MeshWeightAttachmentEdit edit =
                marrow::editor::mesh_weight_model::mesh_weight_edit_from_runtime(
                    *shell_state.load_result.skeleton_data, "mesh_base", "body",
                    "body_mesh", *attachment);
            for (auto& vertex : edit.vertices) {
                (void)marrow::editor::mesh_weight_model::canonicalize_mesh_weight_vertex(
                    *shell_state.load_result.skeleton_data, &vertex);
            }
            if (edit.vertices.size() < 3U) {
                std::cerr << "MAR-187 F1: body_mesh has too few vertices.\n";
                return false;
            }
            for (auto& influence : edit.vertices[0].influences) {
                influence.weight *= 0.7;
            }
            for (auto& influence : edit.vertices[2].influences) {
                influence.weight *= 0.7;
            }
            auto transaction = shell_state.session.begin_edit({
                marrow::editor::EditKind::EditProperty,
                "MAR-187 F1 seeds two non-canonical vertices",
                "mar187:f1",
                false,
                marrow::editor::EditImpact::Project | marrow::editor::EditImpact::Runtime});
            if (!transaction) {
                std::cerr << "MAR-187 F1: could not open a transaction.\n";
                return false;
            }
            transaction.project()->mesh_weight_attachment_edits = {edit};
            const auto committed = transaction.commit();
            if (!committed) {
                std::cerr << "MAR-187 F1: seeding failed: " << committed.error->message
                          << '\n';
                return false;
            }
            sync_shell_from_editor_session(&shell_state);
            shell_state.session.clear_history();
        }

        static constexpr char kFirstIdentity[] =
            "weights.non_canonical|mesh_base|body|body_mesh|0";
        static constexpr char kSecondIdentity[] =
            "weights.non_canonical|mesh_base|body|body_mesh|2";

        // `SetWindowFocus` is confined to the OPENING frames. The window is a
        // dock tab beside the Timeline in the real layout, so it renders no rows
        // until focused -- and forcing focus later would close any popup the
        // severity combo had opened, making an item sweep report the widget
        // absent for the wrong reason.
        bool focus_problems = true;
        const auto render_problems_frame = [&]() {
            io.DeltaTime = 1.0f / 60.0f;
            ImGui::NewFrame();
            if (focus_problems) {
                ImGui::SetWindowFocus(kProblemsWindowTitle);
            }
            draw_problems_window(&shell_state);
            ImGui::Render();
        };
        render_problems_frame();
        render_problems_frame();
        focus_problems = false;

        ImGuiWindow* problems_window = ImGui::FindWindowByName(kProblemsWindowTitle);
        if (problems_window == nullptr) {
            std::cerr << "MAR-187 F1: no Problems window exists after two frames. "
                         "`draw_problems_window` is not reached from this frame "
                         "body.\n";
            return false;
        }

        // Every widget is emitted at plain window scope with an explicit
        // `##<identity>` suffix -- no `PushID`, no `BeginTabItem`, no
        // `BeginChild` -- so `window->GetID(label)` IS the seed. If a future
        // revision wraps the rows, this seed silently stops matching and the
        // sweep reports "absent"; the control sweep below is what makes that
        // distinguishable from a genuinely missing widget.
        const ImGuiID severity_id = problems_window->GetID("Severity");
        const std::string fix_label = std::string("Fix##fix_") + kFirstIdentity;
        const ImGuiID fix_id = problems_window->GetID(fix_label.c_str());
        const std::string row_label_prefix = "##row_";

        const auto sweep_for = [&](ImGuiID target, float column_inset,
                                   ImVec2* found_out) {
            const ImVec2 origin = problems_window->Pos;
            const ImVec2 size = problems_window->Size;
            for (float y = origin.y + 4.0f; y < origin.y + size.y - 2.0f; y += 2.0f) {
                const float x = origin.x + column_inset;
                io.AddMousePosEvent(x, y);
                render_problems_frame();
                if (ImGui::GetCurrentContext()->HoveredId == target) {
                    *found_out = ImVec2(x, y);
                    return true;
                }
            }
            return false;
        };

        // CONTROL SWEEP FIRST. Sweeping a control that already exists is what
        // keeps a broken id seed from being misread as a missing widget.
        ImVec2 severity_at{};
        if (!sweep_for(severity_id, 60.0f, &severity_at)) {
            std::cerr << "MAR-187 F1: no widget in the \"Problems\" window matched the "
                         "id of the severity filter. A real mouse swept every "
                         "position and HoveredId never equalled it, so the widget is "
                         "absent or unreachable -- or the id seed is wrong, which is "
                         "why this control is swept before anything else.\n";
            return false;
        }

        // The Fix button sits after a `SameLine()`, so it needs its OWN sweep
        // column: a column at the row's left edge reaches the Selectable and
        // misses the button entirely.
        ImVec2 fix_at{};
        bool found_fix = false;
        for (float inset = 40.0f; inset < problems_window->Size.x - 8.0f && !found_fix;
             inset += 20.0f) {
            found_fix = sweep_for(fix_id, inset, &fix_at);
        }
        if (!found_fix) {
            std::cerr << "MAR-187 F1: the Fix button for '" << kFirstIdentity
                      << "' was never hovered by any sweep column. The severity "
                         "filter WAS found, so the window draws and the id seed is "
                         "right; the button itself is missing or unreachable.\n";
            return false;
        }

        const std::size_t undo_before = shell_state.session.undo_count();
        io.AddMousePosEvent(fix_at.x, fix_at.y);
        render_problems_frame();
        io.AddMouseButtonEvent(0, true);
        render_problems_frame();
        io.AddMouseButtonEvent(0, false);
        render_problems_frame();
        // Two presses at the same pixel are a double click, and ImGui turns one
        // into a text input on some widgets. Advance past the threshold before
        // any further gesture.
        io.DeltaTime = io.MouseDoubleClickTime + 0.1f;
        render_problems_frame();
        io.DeltaTime = 1.0f / 60.0f;

        if (shell_state.session.undo_count() != undo_before + 1U) {
            std::cerr << "MAR-187 F1: clicking Fix produced "
                      << (shell_state.session.undo_count() - undo_before)
                      << " history entries, expected exactly one.\n";
            return false;
        }
        // A safe fix commits through a session transaction, and a Runtime-impact
        // commit replaces the PreviewController -- destroying the Skeleton and
        // AnimationState the shell's raw aliases point at. Asserted on POINTER
        // IDENTITY, not by dereferencing: reading through a dangling pointer is UB
        // that usually happens to pass. The click above is real, so this fails if
        // the Fix handler stops calling `sync_shell_from_editor_session`.
        if (shell_state.preview_skeleton !=
                marrow::editor::EditorSessionShellBinding::preview_skeleton(
                    shell_state.session) ||
            shell_state.animation_state !=
                marrow::editor::EditorSessionShellBinding::preview_animation_state(
                    shell_state.session)) {
            std::cerr << "MAR-187 F1: after clicking Fix the shell's preview aliases "
                         "still point at the pre-commit PreviewController, which the "
                         "commit destroyed. The handler must call "
                         "sync_shell_from_editor_session() before returning.\n";
            return false;
        }
        refresh_problems_if_revised(&shell_state);
        if (!shell_state.problems.report.has_value()) {
            std::cerr << "MAR-187 F1: no report after the fix.\n";
            return false;
        }
        if (marrow::editor::find_issue_by_identity(
                *shell_state.problems.report, kFirstIdentity).has_value()) {
            std::cerr << "MAR-187 F1: the clicked row's problem is still reported.\n";
            return false;
        }
        // THE I25 DETECTOR. `normalize_weights_command`'s scope is empty --
        // every vertex -- so wiring the button to it would silently repair this
        // one too, and no UI-free case would notice.
        if (!marrow::editor::find_issue_by_identity(
                *shell_state.problems.report, kSecondIdentity).has_value()) {
            std::cerr << "MAR-187 F1: clicking Fix on vertex 0 also repaired vertex 2. "
                         "The button must call apply_safe_fix, whose scope is the ONE "
                         "vertex the row names -- not normalize_weights_command, "
                         "whose scope is empty and therefore means EVERY vertex.\n";
            return false;
        }
        std::cout << "MAR-187 F1: the Problems window renders; the severity filter was "
                     "located at (" << severity_at.x << "," << severity_at.y
                  << ") and the Fix button at (" << fix_at.x << "," << fix_at.y
                  << ") by a real mouse; clicking it recorded one history entry, "
                     "cleared that row, and left the second non-canonical vertex "
                     "reported.\n";

        sync_shell_from_editor_session(&shell_state);
        shell_state.session.clear_history();
    }

    std::cout << shell_state.status_message << '\n'
              << "Headless editor shell smoke rendered " << frame_count << " frame(s).\n";
    return true;
}

} // namespace marrow::editor::shell
