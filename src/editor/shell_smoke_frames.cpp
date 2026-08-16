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
