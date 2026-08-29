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
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

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
#include "shell_selection.hpp"
#include "shell_timeline.hpp"
#include "shell_timeline_graph.hpp"
#include "shell_weight_paint.hpp"
#include "shell_viewport_ui.hpp"
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
    const int frame_count = options.auto_close_frames.value_or(1);
    bool validated_dock_layout = false;
    for (int frame_index = 0; frame_index < frame_count; ++frame_index) {
        io.DeltaTime = 1.0f / 60.0f;
        ImGui::NewFrame();
        (void)poll_runtime_asset_changes(&shell_state);
        advance_timeline_playback(&shell_state, io.DeltaTime);
        (void)shell_state.session.advance_parameter_state(io.DeltaTime);
        sync_shell_from_editor_session_if_revised(&shell_state);
        handle_project_history_shortcuts(&shell_state);

        bool reload_requested = false;
        (void)draw_menu_bar(&reload_requested, &shell_state);
        const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
        const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0U, main_viewport);
        ensure_default_dock_layout(&shell_state, dockspace_id, main_viewport);
        draw_project_window(&reload_requested, &shell_state);
        draw_runtime_window(shell_state);
        draw_constraints_window(&shell_state);
        draw_timeline_window(&shell_state);
        draw_hierarchy_window(&shell_state);
        draw_viewport_window(&shell_state);
        draw_inspector_window(&shell_state);
        draw_parameter_windows(&shell_state);

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

        if (reload_requested && !reload_project(&shell_state)) {
            std::cerr << shell_state.error_message;
            return false;
        }
    }

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
        std::cerr << "Fallback Graph interaction smoke could not locate visible plot space.\n";
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

    std::cout << shell_state.status_message << '\n'
              << "Headless editor shell smoke rendered " << frame_count << " frame(s).\n";
    return true;
}

} // namespace marrow::editor::shell
