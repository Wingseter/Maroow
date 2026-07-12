#include "shell_project_panels.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#include "imgui.h"
#include "imgui_internal.h"

#include "agent_socket.hpp"
#include "shell_asset_watch.hpp"
#include "shell_preview.hpp"
#include "shell_selection.hpp"
#include "shell_theme.hpp"
#include "shell_timeline.hpp"
#include "shell_viewport_ui.hpp"
#include "shell_weight_paint.hpp"
#include "shell_widgets.hpp"

namespace marrow::editor::shell {

using marrow::editor::Icon;

ShellMode current_shell_mode(const ShellState* state) {
    if (state->weight_paint.enabled) return ShellMode::WeightPaint;
    if (!state->selected_animation_name.empty()) return ShellMode::Animation;
    return ShellMode::Setup;
}

void apply_shell_mode(ShellState* state, ShellMode mode) {
    if (state == nullptr || authoring_gesture_active(*state)) {
        return;
    }
    switch (mode) {
        case ShellMode::Setup:
            state->weight_paint.enabled = false;
            state->selected_animation_name.clear();
            break;
        case ShellMode::Animation:
            state->weight_paint.enabled = false;
            if (state->selected_animation_name.empty() &&
                state->load_result.skeleton_data != nullptr &&
                !state->load_result.skeleton_data->animations().empty()) {
                state->selected_animation_name =
                    state->load_result.skeleton_data->animations()
                        .front()
                        .name;
            }
            break;
        case ShellMode::WeightPaint:
            state->weight_paint.enabled = true;
            break;
    }
}

// Secondary toolbar tier (below the menu bar): global actions on the left,
// the ModeStrip centered, drawn as a viewport side bar so the dockspace
// shrinks to fit beneath it.
void draw_shell_toolbar(bool* reload_requested, ShellState* state) {
    namespace t = marrow::editor::shell::theme;
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float h = ImGui::GetFrameHeight() + 8.0f;
    ImGui::PushStyleColor(ImGuiCol_MenuBarBg, t::kSurfaceDefault);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoScrollbar |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_MenuBar;
    if (ImGui::BeginViewportSideBar("##ShellToolbar", vp, ImGuiDir_Up, h,
                                    flags)) {
        if (ImGui::BeginMenuBar()) {
            const bool project_loaded =
                state->load_result.project != nullptr;
            const bool gesture_active = authoring_gesture_active(*state);
            auto dispatch_op = [&](const char* op) {
                namespace json = marrow::runtime::json;
                json::Value::Object cmd_obj;
                cmd_obj.emplace("op", json::Value(std::string(op), {}));
                dispatch_agent_command(
                    state, json::Value(std::move(cmd_obj), {}));
            };

            if (icon_button(state->icons, Icon::Save, "Save project", false,
                            !project_loaded || gesture_active)) {
                dispatch_op("save");
            }
            if (icon_button(state->icons, Icon::Export,
                            "Export runtime assets", false,
                            !project_loaded || gesture_active)) {
                dispatch_op("export_runtime");
            }
            if (icon_button(state->icons, Icon::Reload, "Reload project",
                            false, !project_loaded || gesture_active)) {
                *reload_requested = true;
            }
            ImGui::TextDisabled("|");
            if (icon_button(state->icons, Icon::Undo, "Undo (Ctrl+Z)", false,
                            gesture_active || !state->session.can_undo())) {
                undo_project_change(state);
            }
            if (icon_button(state->icons, Icon::Redo, "Redo (Ctrl+Shift+Z)",
                            false, gesture_active || !state->session.can_redo())) {
                redo_project_change(state);
            }
            if (state->project_dirty) {
                ImGui::SameLine(0.0f, 12.0f);
                widgets::chip("UNSAVED", widgets::ChipTone::Warn, true);
            }

            // ModeStrip — centered segmented control.
            const char* kModes[] = {"SETUP POSE", "ANIMATION",
                                    "WEIGHT PAINT"};
            int mode_idx = static_cast<int>(current_shell_mode(state));
            float strip_w = 16.0f;
            for (const char* m : kModes) {
                strip_w += ImGui::CalcTextSize(m).x + 24.0f;
            }
            const float center_x =
                (ImGui::GetWindowWidth() - strip_w) * 0.5f;
            if (center_x > ImGui::GetCursorPosX()) {
                ImGui::SameLine(center_x);
            } else {
                ImGui::SameLine(0.0f, 24.0f);
            }
            ImGui::BeginDisabled(gesture_active);
            if (widgets::seg_toggle("##modestrip", kModes, 3, &mode_idx)) {
                apply_shell_mode(state, static_cast<ShellMode>(mode_idx));
            }
            ImGui::EndDisabled();

            // Context actions (right): Agent panel toggle.
            const float btn_x = ImGui::GetWindowWidth() - 44.0f;
            if (btn_x > ImGui::GetCursorPosX()) {
                ImGui::SameLine(btn_x);
            } else {
                ImGui::SameLine(0.0f, 16.0f);
            }
            if (icon_button(state->icons, Icon::Eye,
                            "Toggle Agent panel (Ctrl+L)",
                            state->show_agent_panel)) {
                state->show_agent_panel = !state->show_agent_panel;
            }

            ImGui::EndMenuBar();
        }
    }
    ImGui::End();
    ImGui::PopStyleColor();
}

void draw_menu_bar(GLFWwindow* window, bool* reload_requested, ShellState* state) {
    if (!ImGui::BeginMainMenuBar()) {
        return;
    }

    if (g_font_semibold) ImGui::PushFont(g_font_semibold);
    ImGui::TextColored(marrow::editor::shell::theme::kPrimary, "marrow");
    if (g_font_semibold) ImGui::PopFont();
    ImGui::TextDisabled("·");

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem(
                "Reload Project",
                nullptr,
                false,
                !authoring_gesture_active(*state))) {
            *reload_requested = true;
        }
        if (ImGui::MenuItem("Quit", nullptr, false, window != nullptr)) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem(
                "Undo",
                "Ctrl+Z",
                false,
                !authoring_gesture_active(*state) && state->session.can_undo())) {
            undo_project_change(state);
        }
        if (ImGui::MenuItem(
                "Redo",
                "Ctrl+Shift+Z / Ctrl+Y",
                false,
                !authoring_gesture_active(*state) && state->session.can_redo())) {
            redo_project_change(state);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        const bool viewport_settings_available =
            state->load_result && state->load_result.project != nullptr;
        const bool onion_skin_enabled = state->viewport.onion_skin.enabled;
        if (ImGui::MenuItem(
                "Onion Skinning",
                nullptr,
                onion_skin_enabled,
                viewport_settings_available)) {
            apply_onion_skin_edit(
                state,
                std::string(onion_skin_enabled ? "Disabled" : "Enabled") + " onion skinning",
                "viewport:onion-skin:enabled",
                false,
                [&](marrow::editor::OnionSkinSettings* settings) {
                    settings->enabled = !onion_skin_enabled;
                });
        }
        if (ImGui::MenuItem(
                "Performance HUD",
                nullptr,
                state->hud_overlay_enabled,
                viewport_settings_available)) {
            state->hud_overlay_enabled = !state->hud_overlay_enabled;
            if (!state->hud_overlay_enabled) {
                state->hud_overlay_frame.reset();
            }
        }
        ImGui::Separator();
        const auto toggle_debug_overlay_item =
            [&](const char* label,
                bool enabled,
                std::string_view group,
                auto mutate) {
                if (ImGui::MenuItem(label, nullptr, enabled, viewport_settings_available)) {
                    apply_debug_overlay_edit(
                        state,
                        std::string(enabled ? "Disabled " : "Enabled ") + label,
                        std::string(group),
                        false,
                        mutate);
                }
            };
        toggle_debug_overlay_item(
            "Bone Hierarchy",
            state->viewport.debug_overlay.bones,
            "viewport:debug-overlay:bones",
            [](marrow::editor::DebugOverlaySettings* settings) {
                settings->bones = !settings->bones;
            });
        toggle_debug_overlay_item(
            "IK Constraints",
            state->viewport.debug_overlay.ik_constraints,
            "viewport:debug-overlay:ik",
            [](marrow::editor::DebugOverlaySettings* settings) {
                settings->ik_constraints = !settings->ik_constraints;
            });
        toggle_debug_overlay_item(
            "Path Constraints",
            state->viewport.debug_overlay.path_constraints,
            "viewport:debug-overlay:path",
            [](marrow::editor::DebugOverlaySettings* settings) {
                settings->path_constraints = !settings->path_constraints;
            });
        toggle_debug_overlay_item(
            "Physics Constraints",
            state->viewport.debug_overlay.physics_constraints,
            "viewport:debug-overlay:physics",
            [](marrow::editor::DebugOverlaySettings* settings) {
                settings->physics_constraints = !settings->physics_constraints;
            });
        toggle_debug_overlay_item(
            "Mesh Wireframes",
            state->viewport.debug_overlay.mesh_wireframes,
            "viewport:debug-overlay:meshes",
            [](marrow::editor::DebugOverlaySettings* settings) {
                settings->mesh_wireframes = !settings->mesh_wireframes;
            });
        toggle_debug_overlay_item(
            "Bounding Boxes",
            state->viewport.debug_overlay.bounding_boxes,
            "viewport:debug-overlay:bounds",
            [](marrow::editor::DebugOverlaySettings* settings) {
                settings->bounding_boxes = !settings->bounding_boxes;
            });
        ImGui::Separator();
        if (ImGui::MenuItem("Zoom to Fit", "F", false, viewport_settings_available)) {
            const ImVec2 avail = ImGui::GetContentRegionAvail();
            auto_frame_skeleton(state, avail);
        }
        if (ImGui::MenuItem("Reset Viewport", nullptr, false, viewport_settings_available)) {
            state->viewport.zoom = 1.0;
            state->viewport.pan_x = 0.0;
            state->viewport.pan_y = 0.0;
            state->status_message = "Reset viewport to 1:1";
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Window")) {
        if (ImGui::MenuItem("Reset Layout")) {
            state->default_dock_layout_initialized = false;
            state->status_message = "Reset dock layout";
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        if (ImGui::MenuItem("Keyboard Shortcuts")) {
            state->status_message =
                "Shortcuts: Ctrl+Z Undo | Ctrl+Shift+Z Redo | Space Play/Pause | "
                "Home Reset | F Frame | RMB Pan | Wheel Zoom";
        }
        ImGui::Separator();
        ImGui::TextDisabled("Maroow Editor v1.0");
        ImGui::TextDisabled("Spine 4.2 compatible");
        ImGui::EndMenu();
    }

    // Right cluster: status message + agent presence chip. Global actions
    // moved to the secondary toolbar tier (draw_shell_toolbar).
    namespace th = marrow::editor::shell::theme;
    const bool agent_running =
        state->agent_server != nullptr && state->agent_server->is_running();
    char presence[80];
    if (agent_running) {
        std::snprintf(presence, sizeof(presence), "AGENT · LISTENING :%d",
                      state->agent_listen_port.value_or(kDefaultAgentPort));
    } else {
        std::snprintf(presence, sizeof(presence), "AGENT · OFF");
    }
    const float chip_w =
        ImGui::CalcTextSize(presence).x + 16.0f + 12.0f;  // pad + dot
    const float status_w =
        state->status_message.empty()
            ? 0.0f
            : ImGui::CalcTextSize(state->status_message.c_str()).x + 16.0f;
    const float right_block = chip_w + status_w;
    const float target_x = ImGui::GetWindowWidth() - right_block - 12.0f;
    if (target_x > ImGui::GetCursorPosX()) {
        ImGui::SameLine(target_x);
    } else {
        ImGui::SameLine(0.0f, 16.0f);
    }
    if (!state->status_message.empty()) {
        ImGui::TextColored(th::kFaint, "%s", state->status_message.c_str());
        ImGui::SameLine(0.0f, 16.0f);
    }
    widgets::chip(presence,
                  agent_running ? widgets::ChipTone::Prim
                                : widgets::ChipTone::Neutral,
                  true);

    ImGui::EndMainMenuBar();

    draw_shell_toolbar(reload_requested, state);
}

void draw_project_window(bool* reload_requested, ShellState* state) {
    ImGui::Begin(kProjectWindowTitle);
    widgets::panel_head(state->icons, Icon::NodeAnim, "Project",
                        state->project_dirty ? "UNSAVED" : nullptr);

    const bool gesture_active = authoring_gesture_active(*state);
    if (icon_button(
            state->icons,
            Icon::Reload,
            "Reload project",
            false,
            gesture_active)) {
        *reload_requested = true;
    }
    ImGui::SameLine();
    if (icon_button(state->icons, Icon::Save, "Save project", false, gesture_active)) {
        save_project_file(state, true);
    }
    ImGui::SameLine();
    if (icon_button(
            state->icons,
            Icon::Export,
            "Export runtime assets",
            false,
            gesture_active)) {
        export_runtime_assets_file(state, true);
    }

    ImGui::SameLine();
    ImGui::Checkbox("Export .mbin", &state->export_binary_output);

    // Project identity card (surface-card tonal lift, no border).
    {
        namespace t = marrow::editor::shell::theme;
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, t::kSurfaceCard);
        ImGui::BeginChild("proj_card", ImVec2(0.0f, 0.0f),
                          ImGuiChildFlags_AutoResizeY);
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        if (g_font_small) ImGui::PushFont(g_font_small);
        ImGui::TextColored(t::kFaint, "PROJECT");
        if (g_font_small) ImGui::PopFont();
        const std::string stem = state->project_path.filename().string();
        ImGui::TextColored(t::kOnSurface, "%s",
                           stem.empty() ? "(unsaved)" : stem.c_str());
        ImGui::TextColored(t::kFaint, "%s",
                           state->project_path.string().c_str());
        ImGui::Dummy(ImVec2(0.0f, 4.0f));
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::Dummy(ImVec2(0.0f, 6.0f));
    }

    if (!state->load_result) {
        ImGui::TextUnformatted("Project data is unavailable.");
        if (!state->error_message.empty()) {
            ImGui::Spacing();
            ImGui::TextWrapped("%s", state->error_message.c_str());
        }
        ImGui::End();
        return;
    }

    const auto& project = *state->load_result.project;
    const auto& skeleton = *state->load_result.skeleton_data;
    const std::string active_animation_label =
        state->selected_animation_name.empty() ? std::string("<setup pose>")
                                               : state->selected_animation_name;
    ImGui::Text("Name: %s", project.editor_metadata.name.c_str());
    ImGui::Text("Animation: %s", active_animation_label.c_str());
    ImGui::Text(
        "Playhead: %s / %s (%s)",
        format_time_seconds(state->timeline_time_seconds).c_str(),
        format_time_seconds(timeline_preview_duration(*state)).c_str(),
        state->timeline_playing ? "playing" : "paused");
    ImGui::Text(
        "Preview mode: %s%s",
        state->preview_queue_enabled ? "queued transition" : "single clip",
        state->preview_reverse ? " / reverse" : "");
    ImGui::Text(
        "Authored default animation: %s",
        project.editor_metadata.active_animation.c_str());
    ImGui::Text(
        "Preview skins: %s",
        preview_skin_summary(skeleton, state->preview_skin_names).c_str());
    ImGui::Text(
        "Edited transform tracks: %zu  Deform: %zu  Draw order: %zu  Events: %zu (%s)",
        project.transform_timeline_edits.size(),
        project.mesh_deform_timeline_edits.size(),
        project.draw_order_timeline_edits.size(),
        project.event_timeline_edits.size(),
        state->project_dirty ? "unsaved changes" : "saved");
    ImGui::Text(
        "Preview root motion: recent(%.2f, %.2f) total(%.2f, %.2f)",
        state->preview_root_motion_delta.x,
        state->preview_root_motion_delta.y,
        state->preview_root_motion_total.x,
        state->preview_root_motion_total.y);
    ImGui::Text(
        "Edited constraints: IK %zu, Path %zu, Transform %zu, Physics %zu (%s)",
        project.ik_constraint_edits.size(),
        project.path_constraint_edits.size(),
        project.transform_constraint_edits.size(),
        project.physics_constraint_edits.size(),
        state->project_dirty ? "unsaved changes" : "saved");
    ImGui::Text(
        "History: undo %zu  redo %zu",
        state->session.undo_count(),
        state->session.redo_count());
    ImGui::Text("Runtime skeleton: %s", project.resolved_skeleton_path().string().c_str());
    ImGui::Text("Runtime atlases: %s", join_paths(project.resolved_atlas_paths()).c_str());
    ImGui::Text(
        "Export bundle: %s",
        project.resolved_export_skeleton_path().string().c_str());
    if (state->export_binary_output) {
        ImGui::Text(
            "Binary target: %s",
            project.resolved_export_binary_path().string().c_str());
    }
    ImGui::Spacing();
    ImGui::TextWrapped("%s", project.editor_metadata.notes.c_str());
    if (!state->error_message.empty()) {
        ImGui::Spacing();
        ImGui::TextWrapped("%s", state->error_message.c_str());
    }

    ImGui::End();
}

void draw_runtime_window(const ShellState& state) {
    ImGui::Begin(kRuntimeAssetsWindowTitle);
    widgets::panel_head(state.icons, Icon::NodeSkin, "Runtime assets",
                        "READ ONLY");

    if (!state.load_result) {
        ImGui::TextUnformatted("Load a valid project to inspect runtime assets.");
        ImGui::End();
        return;
    }

    const marrow::runtime::SkeletonData& skeleton = *state.load_result.skeleton_data;
    const auto& info = skeleton.info();
    ImGui::Text("Skeleton: %s", info.name.c_str());
    ImGui::Text("Bounds: %.0f x %.0f", info.width, info.height);
    ImGui::Text("Bones: %zu", skeleton.bones().size());
    ImGui::Text("Slots: %zu", skeleton.slots().size());
    ImGui::Text("Skins: %zu", skeleton.skins().size());
    ImGui::Text("Animations: %zu", skeleton.animations().size());
    ImGui::Text(
        "Constraints: IK %zu, Path %zu, Transform %zu, Physics %zu",
        skeleton.ik_constraints().size(),
        skeleton.path_constraints().size(),
        skeleton.transform_constraints().size(),
        skeleton.physics_constraints().size());

    if (ImGui::CollapsingHeader("Animation Clips", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& animation : skeleton.animations()) {
            char buf[256];
            std::snprintf(buf, sizeof(buf), "%s  (%.2fs)", animation.name.c_str(), animation.duration());
            icon_label(state.icons, Icon::NodeAnim, buf, 0.85f);
        }
    }

    if (ImGui::CollapsingHeader("Skins", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& skin : skeleton.skins()) {
            icon_label(state.icons, Icon::NodeSkin, skin.name.c_str(), 0.85f);
        }
    }

    if (ImGui::CollapsingHeader("Constraints", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& constraint : skeleton.ik_constraints()) {
            icon_label(state.icons, Icon::ConstraintIk, constraint.name.c_str(), 0.85f);
        }
        for (const auto& constraint : skeleton.path_constraints()) {
            icon_label(state.icons, Icon::ConstraintPath, constraint.name.c_str(), 0.85f);
        }
        for (const auto& constraint : skeleton.transform_constraints()) {
            icon_label(state.icons, Icon::ConstraintXform, constraint.name.c_str(), 0.85f);
        }
        for (const auto& constraint : skeleton.physics_constraints()) {
            icon_label(state.icons, Icon::ConstraintPhysics, constraint.name.c_str(), 0.85f);
        }
    }

    if (ImGui::CollapsingHeader("Atlases", ImGuiTreeNodeFlags_DefaultOpen)) {
        for (const auto& atlas : state.load_result.atlas_data) {
            ImGui::BulletText(
                "%s (%zu regions, %.0f x %.0f)",
                atlas->info().name.c_str(),
                atlas->regions().size(),
                atlas->info().width,
                atlas->info().height);
        }
    }

    ImGui::End();
}

// Maps each bone_index to slot_indices whose bone_index matches.
// Maps a track id to the property icon shown next to it in the timeline label
// column. Detection mirrors the string prefix logic in draw_transform_timeline.

} // namespace marrow::editor::shell
