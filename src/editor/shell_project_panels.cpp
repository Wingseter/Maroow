#include "shell_project_panels.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
#include "marrow/editor/authoring.hpp"

namespace marrow::editor::shell {

using marrow::editor::Icon;

namespace {

constexpr char kAnimationNamePopup[] = "Animation Name##animation_catalog";
constexpr char kAnimationDeletePopup[] = "Delete Animation##animation_catalog";

struct AnimationCatalogPopupState {
    AnimationCatalogAction action{AnimationCatalogAction::Create};
    std::string source_animation;
    std::string delete_animation;
    std::array<char, 128> name{};
};

AnimationCatalogPopupState g_animation_catalog_popup;

const char* animation_catalog_verb(AnimationCatalogAction action) {
    switch (action) {
    case AnimationCatalogAction::Create:
        return "Create";
    case AnimationCatalogAction::Duplicate:
        return "Duplicate";
    case AnimationCatalogAction::Rename:
        return "Rename";
    case AnimationCatalogAction::Delete:
        return "Delete";
    }
    return "Edit";
}

std::string unique_animation_name(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view stem) {
    std::string candidate(stem);
    if (skeleton.find_animation(candidate) == nullptr) {
        return candidate;
    }
    for (std::size_t suffix = 2U;; ++suffix) {
        candidate = std::string(stem) + "_" + std::to_string(suffix);
        if (skeleton.find_animation(candidate) == nullptr) {
            return candidate;
        }
    }
}

void open_animation_name_popup(
    AnimationCatalogAction action,
    std::string source_animation,
    std::string initial_name) {
    g_animation_catalog_popup.action = action;
    g_animation_catalog_popup.source_animation = std::move(source_animation);
    std::snprintf(
        g_animation_catalog_popup.name.data(),
        g_animation_catalog_popup.name.size(),
        "%s",
        initial_name.c_str());
    ImGui::OpenPopup(kAnimationNamePopup);
}

void draw_animation_catalog_popups(ShellState* state) {
    if (ImGui::BeginPopupModal(
            kAnimationNamePopup,
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        const char* verb = animation_catalog_verb(g_animation_catalog_popup.action);
        if (g_animation_catalog_popup.action == AnimationCatalogAction::Create) {
            ImGui::TextUnformatted("Create a new empty animation clip.");
        } else {
            ImGui::Text(
                "%s '%s'.",
                verb,
                g_animation_catalog_popup.source_animation.c_str());
        }
        if (ImGui::IsWindowAppearing()) {
            ImGui::SetKeyboardFocusHere();
        }
        const bool enter_pressed = ImGui::InputText(
            "Name",
            g_animation_catalog_popup.name.data(),
            g_animation_catalog_popup.name.size(),
            ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
        const std::string destination(g_animation_catalog_popup.name.data());
        const bool unchanged_rename =
            g_animation_catalog_popup.action == AnimationCatalogAction::Rename &&
            destination == g_animation_catalog_popup.source_animation;
        const bool can_apply =
            !destination.empty() && !unchanged_rename &&
            !authoring_gesture_active(*state) && !state->session.transaction_active();

        if (!state->error_message.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kStateErr);
            ImGui::TextWrapped("%s", state->error_message.c_str());
            ImGui::PopStyleColor();
        }

        ImGui::BeginDisabled(!can_apply);
        const bool apply_pressed = ImGui::Button(verb) || enter_pressed;
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        } else if (apply_pressed && can_apply &&
                   apply_animation_catalog_action(
                       state,
                       g_animation_catalog_popup.action,
                       g_animation_catalog_popup.source_animation,
                       destination)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    if (ImGui::BeginPopupModal(
            kAnimationDeletePopup,
            nullptr,
            ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text(
            "Delete '%s' and all of its authored timeline edits?",
            g_animation_catalog_popup.delete_animation.c_str());
        ImGui::TextDisabled("This action can be undone.");
        if (!state->error_message.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text, theme::kStateErr);
            ImGui::TextWrapped("%s", state->error_message.c_str());
            ImGui::PopStyleColor();
        }
        ImGui::Spacing();
        const bool delete_blocked =
            authoring_gesture_active(*state) || state->session.transaction_active();
        ImGui::BeginDisabled(delete_blocked);
        const bool delete_pressed = ImGui::Button("Delete");
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
        } else if (delete_pressed && !delete_blocked &&
                   apply_animation_catalog_action(
                       state,
                       AnimationCatalogAction::Delete,
                       g_animation_catalog_popup.delete_animation)) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void draw_animation_catalog(ShellState* state) {
    const auto& skeleton = *state->load_result.skeleton_data;
    if (!ImGui::CollapsingHeader(
            "Animation Management",
            ImGuiTreeNodeFlags_DefaultOpen)) {
        draw_animation_catalog_popups(state);
        return;
    }

    const float list_height =
        std::min(5.0f, static_cast<float>(std::max<std::size_t>(1U, skeleton.animations().size()))) *
            ImGui::GetTextLineHeightWithSpacing() +
        (ImGui::GetStyle().FramePadding.y * 2.0f);
    if (ImGui::BeginChild(
            "##animation_catalog_list",
            ImVec2(0.0f, list_height),
            ImGuiChildFlags_Borders)) {
        for (const auto& animation : skeleton.animations()) {
            const bool selected = state->selected_animation_name == animation.name;
            ImGui::PushID(animation.name.c_str());
            char label[256];
            std::snprintf(
                label,
                sizeof(label),
                "%s  (%.2fs)",
                animation.name.c_str(),
                animation.duration());
            if (icon_selectable(state->icons, Icon::NodeAnim, label, selected)) {
                state->timeline_playing = false;
                (void)set_selected_animation(
                    state,
                    animation.name,
                    "Project",
                    true,
                    true);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
            ImGui::PopID();
        }
    }
    ImGui::EndChild();

    const bool edit_blocked =
        authoring_gesture_active(*state) || state->session.transaction_active();
    const marrow::runtime::AnimationData* selected_clip = selected_animation(*state);
    const bool has_selection = selected_clip != nullptr;
    ImGui::BeginDisabled(edit_blocked);
    if (ImGui::Button("Create...")) {
        state->error_message.clear();
        open_animation_name_popup(
            AnimationCatalogAction::Create,
            {},
            unique_animation_name(skeleton, "animation"));
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(!has_selection);
    if (ImGui::Button("Duplicate...")) {
        state->error_message.clear();
        const std::string stem = state->selected_animation_name + "_copy";
        open_animation_name_popup(
            AnimationCatalogAction::Duplicate,
            state->selected_animation_name,
            unique_animation_name(skeleton, stem));
    }
    ImGui::SameLine();
    if (ImGui::Button("Rename...")) {
        state->error_message.clear();
        open_animation_name_popup(
            AnimationCatalogAction::Rename,
            state->selected_animation_name,
            state->selected_animation_name);
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(skeleton.animations().size() <= 1U);
    if (ImGui::Button("Delete...")) {
        state->error_message.clear();
        g_animation_catalog_popup.delete_animation = state->selected_animation_name;
        ImGui::OpenPopup(kAnimationDeletePopup);
    }
    ImGui::EndDisabled();
    ImGui::EndDisabled();
    ImGui::EndDisabled();

    if (skeleton.animations().size() <= 1U && has_selection) {
        ImGui::TextDisabled("The last animation cannot be deleted.");
    }

    if (selected_clip != nullptr) {
        const std::string animation_name = selected_clip->name;
        double edited_duration = selected_clip->duration();
        const double inferred_duration = selected_clip->inferred_duration();
        const bool has_explicit_duration = selected_clip->explicit_duration.has_value();

        ImGui::Spacing();
        ImGui::SeparatorText("Clip Timing");
        ImGui::TextDisabled(
            "Inferred %.3fs  |  %s",
            inferred_duration,
            has_explicit_duration ? "explicit boundary" : "inferred fallback");

        const bool owns_duration_gesture =
            state->animation_duration_gesture.has_value() &&
            state->animation_duration_gesture->animation_name == animation_name;
        const bool duration_blocked =
            (authoring_gesture_active(*state) && !owns_duration_gesture) ||
            (state->session.transaction_active() && !owns_duration_gesture);
        ImGui::BeginDisabled(duration_blocked);
        ImGui::SetNextItemWidth(180.0f);
        const bool duration_changed = ImGui::DragScalar(
            "Clip Duration",
            ImGuiDataType_Double,
            &edited_duration,
            0.01f,
            nullptr,
            nullptr,
            "%.3f s");
        const bool duration_activated = ImGui::IsItemActivated();
        const bool duration_deactivated = ImGui::IsItemDeactivated();
        const bool duration_deactivated_after_edit = ImGui::IsItemDeactivatedAfterEdit();
        const bool duration_escape =
            ImGui::IsItemActive() && ImGui::IsKeyPressed(ImGuiKey_Escape, false);
        ImGui::EndDisabled();

        if (duration_activated) {
            (void)begin_animation_duration_gesture(state, animation_name);
        }
        if (duration_changed &&
            state->animation_duration_gesture.has_value() &&
            state->animation_duration_gesture->animation_name == animation_name) {
            (void)apply_animation_duration_gesture(state, edited_duration);
        }
        if (state->animation_duration_gesture.has_value() &&
            state->animation_duration_gesture->animation_name == animation_name) {
            if (duration_escape) {
                (void)finish_animation_duration_gesture(state, false);
            } else if (duration_deactivated_after_edit || duration_deactivated) {
                (void)finish_animation_duration_gesture(state, true);
            }
        }
    }
    draw_animation_catalog_popups(state);
}

} // namespace

bool begin_animation_duration_gesture(
    ShellState* state,
    std::string_view animation_name) {
    if (state == nullptr || !state->load_result ||
        state->load_result.project == nullptr || animation_name.empty()) {
        return false;
    }
    if (authoring_gesture_active(*state) || state->session.transaction_active()) {
        state->status_message = "Finish the active edit before changing clip duration";
        return false;
    }
    if (state->session.runtime_data() == nullptr ||
        state->session.runtime_data()->find_animation(animation_name) == nullptr) {
        state->error_message =
            "Animation not found: " + std::string(animation_name);
        state->status_message = "Clip duration edit failed";
        return false;
    }

    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::EditProperty,
        "Set animation clip duration",
        "animation-duration:" + std::string(animation_name),
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        state->error_message = transaction.error().has_value()
            ? transaction.error()->format()
            : "Could not start the clip duration edit.";
        state->status_message = "Clip duration edit failed";
        return false;
    }

    AnimationDurationGesture gesture;
    gesture.animation_name = std::string(animation_name);
    gesture.transaction = std::move(transaction);
    state->animation_duration_gesture.emplace(std::move(gesture));
    state->error_message.clear();
    return true;
}

bool apply_animation_duration_gesture(ShellState* state, double duration) {
    if (state == nullptr || !state->animation_duration_gesture.has_value() ||
        state->session.runtime_data() == nullptr) {
        return false;
    }

    AnimationDurationGesture& gesture = *state->animation_duration_gesture;
    const marrow::editor::AuthoringResult mutation =
        marrow::editor::set_animation_duration(
            gesture.transaction.project(),
            *state->session.runtime_data(),
            gesture.animation_name,
            duration);
    if (!mutation) {
        const std::string error = mutation.error.empty()
            ? "The requested clip duration is invalid."
            : mutation.error;
        AnimationDurationGesture cancelled = std::move(gesture);
        state->animation_duration_gesture.reset();
        cancelled.transaction.cancel();
        sync_shell_from_editor_session(state);
        state->error_message = error;
        state->status_message = "Clip duration edit rejected";
        return false;
    }
    if (!mutation.changed) {
        return true;
    }

    const marrow::editor::SessionResult refreshed = gesture.transaction.refresh_runtime();
    if (!refreshed) {
        const std::string error = refreshed.error.has_value()
            ? refreshed.error->format()
            : "The clip duration preview could not be refreshed.";
        AnimationDurationGesture cancelled = std::move(gesture);
        state->animation_duration_gesture.reset();
        cancelled.transaction.cancel();
        sync_shell_from_editor_session(state);
        state->error_message = error;
        state->status_message = "Clip duration preview failed";
        return false;
    }

    gesture.changed = true;
    sync_shell_from_editor_session(state);
    state->error_message.clear();
    state->status_message =
        "Previewing clip duration for " + gesture.animation_name;
    return true;
}

bool finish_animation_duration_gesture(ShellState* state, bool commit) {
    if (state == nullptr || !state->animation_duration_gesture.has_value()) {
        return false;
    }

    AnimationDurationGesture gesture =
        std::move(*state->animation_duration_gesture);
    state->animation_duration_gesture.reset();
    if (!commit || !gesture.changed) {
        gesture.transaction.cancel();
        sync_shell_from_editor_session(state);
        if (!commit) {
            state->status_message = "Cancelled clip duration edit";
        }
        return false;
    }

    const marrow::editor::SessionResult result = gesture.transaction.commit();
    sync_shell_from_editor_session(state);
    if (!result) {
        state->error_message = result.error.has_value()
            ? result.error->format()
            : "The clip duration edit could not be committed.";
        state->status_message = "Clip duration edit failed";
        return false;
    }
    state->error_message.clear();
    state->status_message = "Updated clip duration for " + gesture.animation_name;
    return result.changed;
}

ShellMode current_shell_mode(const ShellState* state) {
    return state == nullptr ? ShellMode::Setup : state->shell_mode;
}

void apply_shell_mode(ShellState* state, ShellMode mode) {
    if (state == nullptr || authoring_gesture_active(*state)) {
        return;
    }
    const bool mode_changed = state->shell_mode != mode;
    const auto clear_ffd_context = [&]() {
        if (mode_changed) {
            state->viewport_ffd_selection.reset();
            state->viewport_ffd_box_selection.reset();
        }
    };
    switch (mode) {
        case ShellMode::Setup:
            state->weight_paint.enabled = false;
            state->timeline_playing = false;
            state->selected_animation_name.clear();
            state->selected_timeline_track_id.reset();
            state->preview_queue_enabled = false;
            state->session.set_playing(false);
            if (!state->session.select_setup_pose()) {
                state->error_message = "Failed to select the setup-pose preview.";
            } else {
                sync_shell_from_editor_session(state);
                state->error_message.clear();
                state->status_message = "Setup Pose is read-only";
                state->shell_mode = ShellMode::Setup;
                clear_ffd_context();
            }
            break;
        case ShellMode::Animation:
            state->weight_paint.enabled = false;
            if (state->selected_animation_name.empty() &&
                state->load_result.skeleton_data != nullptr &&
                !state->load_result.skeleton_data->animations().empty()) {
                const std::string animation_name =
                    state->load_result.skeleton_data->animations().front().name;
                if (!set_selected_animation(
                        state,
                        animation_name,
                        "Mode",
                        false,
                        false)) {
                    state->error_message = "Failed to enter Animation mode.";
                    return;
                }
            }
            state->shell_mode = ShellMode::Animation;
            clear_ffd_context();
            break;
        case ShellMode::WeightPaint:
            state->weight_paint.enabled = true;
            state->shell_mode = ShellMode::WeightPaint;
            clear_ffd_context();
            break;
        case ShellMode::Parameter:
            state->weight_paint.enabled = false;
            state->timeline_playing = false;
            state->session.set_playing(false);
            state->shell_mode = ShellMode::Parameter;
            clear_ffd_context();
            state->error_message.clear();
            state->status_message = "Parameter Modeling preview";
            break;
    }
}

bool apply_animation_catalog_action(
    ShellState* state,
    AnimationCatalogAction action,
    std::string_view source_animation,
    std::string_view destination_animation) {
    if (state == nullptr || !state->session.has_project() ||
        state->session.base_skeleton_document() == nullptr) {
        return false;
    }
    if (authoring_gesture_active(*state) || state->session.transaction_active()) {
        state->status_message = "Finish the active edit before editing animations";
        return false;
    }

    const std::string source(source_animation);
    const std::string destination(destination_animation);
    std::string label;
    switch (action) {
    case AnimationCatalogAction::Create:
        label = "Created animation " + destination;
        break;
    case AnimationCatalogAction::Duplicate:
        label = "Duplicated " + source + " as " + destination;
        break;
    case AnimationCatalogAction::Rename:
        label = "Renamed animation " + source + " to " + destination;
        break;
    case AnimationCatalogAction::Delete:
        label = "Deleted animation " + source;
        break;
    }

    marrow::editor::AnimationCatalogEdit edit;
    edit.source_animation = source;
    edit.destination_animation = destination;
    switch (action) {
    case AnimationCatalogAction::Create:
        edit.kind = marrow::editor::AnimationCatalogEditKind::Create;
        break;
    case AnimationCatalogAction::Duplicate:
        edit.kind = marrow::editor::AnimationCatalogEditKind::Duplicate;
        break;
    case AnimationCatalogAction::Rename:
        edit.kind = marrow::editor::AnimationCatalogEditKind::Rename;
        break;
    case AnimationCatalogAction::Delete:
        edit.kind = marrow::editor::AnimationCatalogEditKind::Delete;
        break;
    }

    const std::string previous_selection = state->selected_animation_name;
    const marrow::editor::SessionResult commit_result =
        state->session.edit_animation_catalog(
            std::move(edit),
            {marrow::editor::EditKind::EditProperty,
             label,
             "animation-catalog",
             false,
             marrow::editor::EditImpact::Project |
                 marrow::editor::EditImpact::Runtime |
                 marrow::editor::EditImpact::Preview});
    if (!commit_result || !commit_result.changed) {
        state->error_message = commit_result.error.has_value()
            ? commit_result.error->format()
            : "The animation catalog edit did not change the project.";
        state->status_message = "Animation edit failed";
        sync_shell_from_editor_session(state);
        return false;
    }

    sync_shell_from_editor_session(state);
    if (state->selected_animation_name != previous_selection) {
        state->timeline_editor.selected_keys.clear();
        state->timeline_editor.box_selection.reset();
    }
    state->selected_timeline_track_id.reset();
    state->error_message.clear();
    state->status_message = std::move(label);
    return true;
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

            if (icon_button(state->icons, Icon::Save, "Save project", false,
                            !project_loaded || gesture_active)) {
                save_project_file(state, true);
            }
            if (icon_button(state->icons, Icon::Export,
                            "Export runtime assets", false,
                            !project_loaded || gesture_active)) {
                export_runtime_assets_file(state, true);
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
                                    "WEIGHT PAINT", "PARAMETERS"};
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
            if (widgets::seg_toggle("##modestrip", kModes, 4, &mode_idx)) {
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

ProjectMenuAction draw_menu_bar(bool* reload_requested, ShellState* state) {
    if (!ImGui::BeginMainMenuBar()) {
        return ProjectMenuAction::None;
    }

    ProjectMenuAction action = ProjectMenuAction::None;

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
        if (ImGui::MenuItem("Quit")) {
            action = ProjectMenuAction::QuitRequested;
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
    return action;
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

    draw_animation_catalog(state);
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
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
