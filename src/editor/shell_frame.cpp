#include "shell_frame.hpp"

#include "imgui.h"
#include "shell_agent_panel.hpp"
#include "shell_asset_watch.hpp"
#include "shell_coalesced_edit.hpp"
#include "shell_constraints.hpp"
#include "shell_file_paths.hpp"
#include "shell_inspector.hpp"
#include "shell_parameters.hpp"
#include "shell_preview.hpp"
#include "shell_problems.hpp"
#include "shell_project_panels.hpp"
#include "shell_selection.hpp"
#include "shell_state.hpp"
#include "shell_theme.hpp"
#include "shell_timeline.hpp"
#include "shell_viewport_ui.hpp"

namespace marrow::editor::shell {

void draw_shell_frame(ShellState& state, double elapsed_seconds) {
    sync_shell_from_editor_session_if_revised(&state);

    // Hot-reload detection needs ~4 Hz, not one stat() sweep per frame.
    // A live gesture defers the poll without losing accumulated time.
    state.runtime_asset_watch_accumulator_seconds += elapsed_seconds;
    if (!authoring_gesture_active(state) &&
        state.runtime_asset_watch_accumulator_seconds >= 0.25) {
        state.runtime_asset_watch_accumulator_seconds = 0.0;
        (void)poll_runtime_asset_changes(&state);
    }
    advance_timeline_playback(&state, ImGui::GetIO().DeltaTime);
    if (current_shell_mode(&state) == ShellMode::Parameter) {
        (void)state.session.advance_parameter_state(ImGui::GetIO().DeltaTime);
        sync_shell_from_editor_session_if_revised(&state);
    }
    handle_project_history_shortcuts(&state);

    draw_menu_bar(&state);
    const ImGuiViewport* main_viewport = ImGui::GetMainViewport();
    const ImGuiID dockspace_id = ImGui::DockSpaceOverViewport(0U, main_viewport);
    ensure_default_dock_layout(&state, dockspace_id, main_viewport);

    // Mode environment wash is part of composition, not the graphics host.
    {
        namespace t = marrow::editor::shell::theme;
        ImVec4 wash = t::kModeSetup;
        switch (current_shell_mode(&state)) {
            case ShellMode::Animation:   wash = t::kModeAnimation; break;
            case ShellMode::WeightPaint: wash = t::kModePaint; break;
            case ShellMode::Parameter:   wash = t::kModeAnimation; break;
            case ShellMode::Setup:       wash = t::kModeSetup; break;
        }
        if (wash.w > 0.0f) {
            ImGui::GetBackgroundDrawList()->AddRectFilled(
                main_viewport->WorkPos,
                ImVec2(main_viewport->WorkPos.x + main_viewport->WorkSize.x,
                       main_viewport->WorkPos.y + main_viewport->WorkSize.y),
                t::u32(wash));
        }
    }
    draw_project_window(&state);
    draw_runtime_window(state);
    draw_constraints_window(&state);
    draw_timeline_window(&state);
    draw_hierarchy_window(&state);
    draw_viewport_window(&state);
    draw_inspector_window(&state);
    draw_problems_window(&state);
    if (current_shell_mode(&state) == ShellMode::Parameter) {
        draw_parameter_windows(&state);
    }
    // Preserve the application ordering: a visibility change invalidates the
    // layout for the next frame, after the current dockspace was submitted.
    if (state.show_agent_panel != state.agent_panel_was_open) {
        state.agent_panel_was_open = state.show_agent_panel;
        state.default_dock_layout_initialized = false;
    }
    if (state.show_agent_panel) {
        draw_agent_window(&state);
    }

    finalize_orphaned_inspector_transform_gesture(&state);
    finalize_orphaned_viewport_transform_gesture(&state);
    finalize_orphaned_viewport_ffd_gesture(&state);
    finalize_orphaned_coalesced_edit(&state);

    // Session replacement must happen after all windows and gesture finalizers,
    // but before the host renders. Both hosts now execute this same boundary.
    (void)apply_pending_file_action(&state);
}

} // namespace marrow::editor::shell
