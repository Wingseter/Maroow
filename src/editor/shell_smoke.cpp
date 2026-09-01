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
#include "shell_preferences.hpp"
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

int run_headless_smoke(const Options& options) {
    // MAR-170: FIRST statement. Every path below constructs a ShellState and
    // loads user-local editor settings, so the real `editor-settings.json` must
    // be out of reach before anything can read or write it. Destruction on
    // every return path restores the previous value and removes the directory.
    const ScopedPreferenceIsolation preference_isolation("smoke");

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.DisplaySize = ImVec2(1440.0f, 900.0f);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = 1.0f / 60.0f;
    io.IniFilename = nullptr;

    apply_editor_theme();
    unsigned char* font_pixels = nullptr;
    int font_width = 0;
    int font_height = 0;
    io.Fonts->GetTexDataAsRGBA32(&font_pixels, &font_width, &font_height);

    ShellState shell_state;
    // Inside the isolation installed above, so this reads the temporary config
    // home and never the developer's real editor-settings.json.
    load_shell_preferences(&shell_state);
    shell_state.project_path = options.project_path;
    if (!reload_project(&shell_state)) {
        std::cerr << shell_state.error_message;
        ImGui::DestroyContext();
        return 1;
    }

    if (shell_state.session.project() != nullptr &&
        shell_state.session.project()->parameter_model.has_value()) {
        const bool passed =
            validate_parameter_mode_shell_smoke(&shell_state, options, io);
        ImGui::DestroyContext();
        return passed ? 0 : 1;
    }

    if (!validate_viewport_ffd_smoke(options.project_path)) {
        ImGui::DestroyContext();
        return 1;
    }

    if (!validate_timeline_graph_shell_smoke(options.project_path)) {
        ImGui::DestroyContext();
        return 1;
    }

    if (!validate_timeline_graph_edit_shell_smoke(options.project_path)) {
        ImGui::DestroyContext();
        return 1;
    }

    if (!validate_timeline_graph_easing_shell_smoke(options.project_path)) {
        ImGui::DestroyContext();
        return 1;
    }

    if (!validate_timeline_curve_preset_shell_smoke(options.project_path)) {
        ImGui::DestroyContext();
        return 1;
    }

    if (!validate_timeline_curve_mode_shell_smoke(options.project_path)) {
        ImGui::DestroyContext();
        return 1;
    }

    if (!validate_timeline_loop_sync_shell_smoke(options.project_path)) {
        ImGui::DestroyContext();
        return 1;
    }
    if (!validate_timeline_scale_shell_smoke(options.project_path)) {
        ImGui::DestroyContext();
        return 1;
    }
    if (!validate_preview_playback_speed_shell_smoke(options.project_path)) {
        ImGui::DestroyContext();
        return 1;
    }
    if (!validate_inherit_editing_shell_smoke(options.project_path)) {
        ImGui::DestroyContext();
        return 1;
    }
    if (!validate_constraint_lifecycle_shell_smoke(options.project_path)) {
        ImGui::DestroyContext();
        return 1;
    }
    if (!validate_constraint_parameter_shell_smoke(options.project_path)) {
        ImGui::DestroyContext();
        return 1;
    }

    const bool passed =
        validate_shell_foundation_smoke(shell_state, options) &&
        validate_viewport_selection_smoke(shell_state) &&
        validate_timeline_project_smoke(shell_state) &&
        render_headless_smoke_frames(shell_state, options, io) &&
        validate_mar181_frame_body_applied_pending(shell_state);

    ImGui::DestroyContext();
    return passed ? 0 : 1;
}

} // namespace marrow::editor::shell
