#include "shell_smoke_scenarios.hpp"

#include <cmath>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>

#include "imgui.h"
#include "imgui_internal.h"
#include "marrow/editor/project.hpp"
#include "shell_agent_panel.hpp"
#include "shell_coalesced_edit.hpp"
#include "shell_file_paths.hpp"
#include "shell_frame.hpp"
#include "shell_parameters.hpp"
#include "shell_preferences.hpp"
#include "shell_project_panels.hpp"

namespace marrow::editor::shell {

// Runs inside run_headless_smoke's real ImGui context and preference isolation.
// This test owns its session; it never mutates the fixture files or the host's
// state. Frame composition is exclusively the production coordinator.
bool validate_shared_shell_frame_contract(ImGuiIO& io) {
    try {
        const auto require = [](bool condition, const char* message) {
            if (!condition) {
                throw std::runtime_error(message);
            }
        };
        const auto active = [](const char* title) {
            const ImGuiWindow* window = ImGui::FindWindowByName(title);
            return window != nullptr && window->Active;
        };
        ShellState state;
        load_shell_preferences(&state);
        state.project_path = "assets/fixtures/player_idle.marrow";
        require(reload_project(&state), "could not open the frame contract fixture");
        apply_shell_mode(&state, ShellMode::Setup);
        state.session.set_playing(false);
        sync_shell_from_editor_session(&state);

        const auto frame = [&](double elapsed, float playback_delta = 1.0f / 60.0f,
                               const std::function<void()>& before_render = {}) {
            io.DeltaTime = playback_delta;
            ImGui::NewFrame();
            try {
                draw_shell_frame(state, elapsed);
                if (before_render) {
                    before_render();
                }
                ImGui::Render();
            } catch (...) {
                if (ImGui::GetCurrentContext()->WithinFrameScope) {
                    ImGui::EndFrame();
                }
                throw;
            }
        };

        // 1. Optional Agent composition and next-frame dock invalidation.
        frame(0.0);
        require(!active(kAgentWindowTitle), "closed Agent panel was submitted");
        state.show_agent_panel = true;
        frame(0.0);
        require(active(kAgentWindowTitle), "enabled Agent panel was omitted");
        require(state.agent_panel_was_open && !state.default_dock_layout_initialized,
                "Agent toggle did not invalidate the following frame's layout");
        frame(0.0);
        require(state.default_dock_layout_initialized,
                "Agent layout was not rebuilt on the following frame");
        state.show_agent_panel = false;
        frame(0.0);
        require(!active(kAgentWindowTitle) && !state.agent_panel_was_open &&
                    !state.default_dock_layout_initialized,
                "closing Agent failed to remove its window and invalidate layout");

        // 2. Parameter-only windows must not leak into Setup composition.
        apply_shell_mode(&state, ShellMode::Parameter);
        frame(0.0);
        for (const char* title : {kParametersWindowTitle, kParameterDeformersWindowTitle,
                                  kExpressionsWindowTitle, kLipSyncWindowTitle}) {
            require(active(title), "Parameter mode omitted an authoring window");
        }
        apply_shell_mode(&state, ShellMode::Setup);
        frame(0.0);
        for (const char* title : {kParametersWindowTitle, kParameterDeformersWindowTitle,
                                  kExpressionsWindowTitle, kLipSyncWindowTitle}) {
            require(!active(title), "Parameter window leaked into Setup mode");
        }

        // 3. A session-side revision is visible to shell composition without a
        // manual sync in the host (the deliberate stale mirror is not a mutation).
        apply_shell_mode(&state, ShellMode::Animation);
        require(state.session.select_animation("idle"), "idle animation missing");
        state.session.set_playing(false);
        require(state.session.seek(0.35), "session seek failed");
        state.timeline_time_seconds = -123.0;
        frame(0.0);
        require(std::abs(state.timeline_time_seconds - 0.35) < 1e-6,
                "frame did not synchronize a session-side preview revision");

        // 4. Playback uses ImGui DeltaTime, not the host's watch elapsed time,
        // and advances once rather than once in each host plus the coordinator.
        require(state.session.seek(0.2), "playback setup seek failed");
        state.session.set_playing(true);
        sync_shell_from_editor_session(&state);
        frame(0.08, 0.02f);
        require(std::abs(state.timeline_time_seconds - 0.22) < 1e-6,
                "playback did not advance exactly once using ImGui DeltaTime");
        state.session.set_playing(false);
        sync_shell_from_editor_session(&state);

        // 5. Clearing only the watch snapshot makes the real poll observable:
        // it repopulates the paths, without touching the fixture's bytes/mtime.
        state.runtime_asset_watch_entries.clear();
        state.runtime_asset_watch_accumulator_seconds = 0.0;
        frame(0.10);
        frame(0.14);
        require(state.runtime_asset_watch_entries.empty() &&
                    std::abs(state.runtime_asset_watch_accumulator_seconds - 0.24) < 1e-9,
                "asset watch polled before its real-time threshold");
        frame(0.02);
        require(!state.runtime_asset_watch_entries.empty() &&
                    state.runtime_asset_watch_accumulator_seconds == 0.0,
                "asset watch did not poll at its real-time threshold");

        // 6. A real coalesced-edit activation defers watch I/O. An orphan is
        // finalized at frame end, so accumulated time is consumed next frame.
        state.runtime_asset_watch_entries.clear();
        state.runtime_asset_watch_accumulator_seconds = 0.0;
        CoalescedEditDescriptor descriptor;
        descriptor.label = "Frame contract orphan";
        descriptor.policy = CoalescedEditPolicy::ProjectMetadataOnly;
        require(apply_coalesced_edit_frame(
                    &state, CoalescedEditFrame{0x745123U, true, false, false, false},
                    descriptor, [] {}),
                "could not activate the coalesced-edit contract fixture");
        require(authoring_gesture_active(state), "activation did not establish a gesture");
        frame(0.30);
        require(state.runtime_asset_watch_entries.empty() &&
                    std::abs(state.runtime_asset_watch_accumulator_seconds - 0.30) < 1e-9,
                "active authoring gesture did not defer watch polling");
        require(!state.pending_edit_action.has_value() && !authoring_gesture_active(state),
                "coordinator did not finalize an orphaned coalesced edit");
        frame(0.01);
        require(!state.runtime_asset_watch_entries.empty() &&
                    state.runtime_asset_watch_accumulator_seconds == 0.0,
                "deferred asset polling lost accumulated time");

        // 7. Session replacement completes before host render, after the
        // coordinator's composition/finalization, and refreshes preview aliases.
        state.pending_file_application = PendingFileApplication{
            FileAction::Open, "assets/fixtures/parameter_face_basic.marrow", {}, {}};
        frame(0.0, 1.0f / 60.0f, [&] {
            require(!state.pending_file_application.has_value(),
                    "deferred file action was left for the host to apply");
            require(state.session.project() != nullptr &&
                        state.session.project()->parameter_model.has_value(),
                    "deferred Open did not replace the session before rendering");
            require(state.preview_skeleton() == state.session.preview_skeleton() &&
                        state.animation_state() == state.session.preview_animation_state(),
                    "deferred Open left stale runtime aliases");
        });

        // 8. An input-free, stopped frame does not create persistent edits.
        state.session.set_playing(false);
        sync_shell_from_editor_session(&state);
        state.session.clear_history();
        const std::string project_before = serialize_project(*state.session.project());
        const auto revision_before = state.session.project_revision();
        const bool dirty_before = state.session.dirty();
        frame(0.0);
        require(serialize_project(*state.session.project()) == project_before &&
                    state.session.project_revision() == revision_before &&
                    state.session.dirty() == dirty_before &&
                    !state.session.can_undo() && !state.session.can_redo(),
                "input-free frame changed persistent project state or history");
        std::cout << "Shared shell frame contract: 8 groups passed.\n";
        return true;
    } catch (const std::exception& error) {
        std::cerr << "Shared shell frame contract FAILED: " << error.what() << '\n';
        return false;
    }
}

} // namespace marrow::editor::shell
