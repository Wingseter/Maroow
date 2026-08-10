#pragma once

#include <utility>

#include "shell_state.hpp"

namespace marrow::editor::shell {

enum class ViewportPressTarget {
    ActiveGesture,
    WeightBrush,
    Translate,
    Rotation,
    Scale,
    Entity,
    Box,
};

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
        allow_merge);
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

// UI-free smoke hooks for exercising the same gesture transaction used by the
// ImGui viewport without synthesizing platform mouse events.
bool begin_viewport_translate_gesture_for_smoke(
    ShellState* state,
    const ViewportLayout& layout,
    ViewportTranslateAxis axis,
    const ImVec2& pointer);
bool update_viewport_translate_gesture_for_smoke(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer);
void finish_viewport_translate_gesture_for_smoke(ShellState* state, bool commit);
std::optional<ViewportTranslateAxis> hit_test_translate_gizmo_for_smoke(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& position);
std::optional<ViewportRotationBasis> viewport_rotation_basis_for_smoke(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t bone_index);
std::optional<double> viewport_rotation_angle_for_smoke(
    const ViewportRotationBasis& basis,
    const ViewportWorldPoint& pointer_world);
double unwrap_viewport_rotation_delta_for_smoke(double delta);
bool viewport_rotation_gizmo_visible_for_smoke(
    const ShellState& state,
    const ViewportLayout& layout);
bool hit_test_rotation_gizmo_for_smoke(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& position);
bool begin_viewport_rotate_gesture_for_smoke(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer);
bool update_viewport_rotate_gesture_for_smoke(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer);
std::optional<ViewportScaleBasis> viewport_scale_basis_for_smoke(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t bone_index);
std::optional<ViewportScaleCandidate> viewport_scale_candidate_for_smoke(
    const ViewportScaleGesturePayload& payload,
    const ViewportLayout& layout,
    const ImVec2& pointer);
bool viewport_scale_gizmo_visible_for_smoke(
    const ShellState& state,
    const ViewportLayout& layout);
bool viewport_uniform_scale_handle_visible_for_smoke(
    const ShellState& state,
    const ViewportLayout& layout);
const char* viewport_transform_hint_for_smoke(
    const ShellState& state,
    const ViewportLayout& layout);
std::optional<ViewportScaleHandle> hit_test_scale_gizmo_for_smoke(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& position);
bool begin_viewport_scale_gesture_for_smoke(
    ShellState* state,
    const ViewportLayout& layout,
    ViewportScaleHandle handle,
    const ImVec2& pointer);
bool update_viewport_scale_gesture_for_smoke(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer);
ViewportPressTarget viewport_press_target_for_smoke(
    bool active_gesture,
    bool weight_brush,
    bool translate,
    bool rotation,
    bool scale,
    bool entity);
void finish_viewport_transform_gesture_for_smoke(
    ShellState* state,
    bool commit);
bool apply_viewport_point_selection_for_smoke(
    ShellState* state,
    const marrow::editor::SelectionItem& item,
    bool command_modifier);
bool begin_viewport_box_selection_for_smoke(
    ShellState* state,
    const ImVec2& pointer,
    bool additive);
bool update_viewport_box_selection_for_smoke(
    ShellState* state,
    const ImVec2& pointer);
bool finish_viewport_box_selection_for_smoke(
    ShellState* state,
    const ViewportLayout& layout,
    bool commit);

} // namespace marrow::editor::shell
