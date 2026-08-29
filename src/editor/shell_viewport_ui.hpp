#pragma once

#include <utility>

#include "shell_coalesced_edit.hpp"
#include "shell_state.hpp"
#include "viewport_interaction_controller.hpp"

namespace marrow::editor::shell {

template <typename MutateFn>
bool execute_viewport_setting_edit_action(
    ShellState* state,
    std::string label,
    std::string group,
    bool allow_merge,
    MutateFn mutate) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }
    if (authoring_gesture_active(*state)) {
        state->status_message = "Finish the active edit before changing viewport settings";
        return false;
    }

    const EditorHistorySnapshot before = capture_history_snapshot(*state);
    mutate();
    return record_action_from_snapshots(
        state,
        before,
        EditActionKind::EditProperty,
        std::move(label),
        std::move(group),
        allow_merge,
        marrow::editor::EditImpact::Project);
}

template <typename MutateFn>
bool apply_snap_setting_edit(
    ShellState* state,
    std::string label,
    std::string group,
    MutateFn mutate) {
    return execute_viewport_setting_edit_action(
        state,
        std::move(label),
        std::move(group),
        false,
        [&]() {
            auto settings = state->load_result.project->snap_settings.value_or(
                marrow::editor::ProjectSnapSettings{});
            mutate(&settings);
            state->load_result.project->snap_settings = std::move(settings);
        });
}

template <typename MutateFn>
bool apply_coalesced_snap_setting_edit(
    ShellState* state,
    const CoalescedEditFrame& frame,
    std::string label,
    std::string group,
    MutateFn mutate) {
    if (state == nullptr || !state->load_result ||
        state->load_result.project == nullptr) {
        return false;
    }
    if (frame.activated && authoring_gesture_active(*state)) {
        state->status_message =
            "Finish the active edit before changing viewport settings";
        return false;
    }
    return apply_coalesced_edit_frame(
        state,
        frame,
        CoalescedEditDescriptor{
            EditActionKind::EditProperty,
            std::move(label),
            std::move(group),
            false,
            CoalescedEditPolicy::ProjectMetadataOnly,
            {}},
        [&]() {
            auto settings = state->load_result.project->snap_settings.value_or(
                marrow::editor::ProjectSnapSettings{});
            mutate(&settings);
            state->load_result.project->snap_settings = std::move(settings);
        });
}

template <typename MutateFn>
bool apply_onion_skin_edit(
    ShellState* state,
    std::string label,
    std::string group,
    bool allow_merge,
    MutateFn mutate) {
    return execute_viewport_setting_edit_action(
        state,
        std::move(label),
        std::move(group),
        allow_merge,
        [&]() {
            auto settings = state->viewport.onion_skin;
            mutate(&settings);
            state->viewport.onion_skin = settings;
            state->load_result.project->editor_metadata.viewport.onion_skin = settings;
        });
}

template <typename MutateFn>
bool apply_debug_overlay_edit(
    ShellState* state,
    std::string label,
    std::string group,
    bool allow_merge,
    MutateFn mutate) {
    return execute_viewport_setting_edit_action(
        state,
        std::move(label),
        std::move(group),
        allow_merge,
        [&]() {
            auto settings = state->viewport.debug_overlay;
            mutate(&settings);
            state->viewport.debug_overlay = settings;
            state->load_result.project->editor_metadata.viewport.debug_overlay = settings;
        });
}


void auto_frame_skeleton(ShellState* state, ImVec2 canvas_size);
std::optional<marrow::runtime::AttachmentVertex> bone_local_position_from_world(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t bone_index,
    const ViewportWorldPoint& target);
std::optional<float> viewport_grid_spacing_pixels(
    const ShellState& state,
    const ViewportLayout& layout);
std::optional<ImVec2> ffd_snap_guide_start(
    const ViewportFfdGesture& gesture,
    const ViewportLayout& layout);
const char* onion_skin_mode_name(marrow::editor::OnionSkinMode mode);
void draw_viewport_fallback_scene(
    const ShellState& state,
    const ViewportLayout& layout,
    const std::vector<OnionSkinGhostPose>& ghost_poses,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
    ImDrawList* draw_list);
void draw_viewport_annotations(
    const ShellState& state,
    const ViewportLayout& layout,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
    ImDrawList* draw_list);
void draw_viewport_window(ShellState* state);
void draw_viewport_settings(ShellState* state);
void finalize_orphaned_viewport_transform_gesture(ShellState* state);
void finalize_orphaned_viewport_ffd_gesture(ShellState* state);

} // namespace marrow::editor::shell
