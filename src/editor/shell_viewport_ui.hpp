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

// Shell-private interaction boundary shared by the ImGui adapter and the
// UI-free smoke scenarios. These functions own the actual hit-test,
// arbitration, and gesture behavior; callers do not get a parallel test path.
namespace viewport_interaction {

ViewportPressTarget press_target(
    bool active_gesture,
    bool weight_brush,
    bool translate,
    bool rotation,
    bool scale,
    bool entity);
std::optional<ViewportTranslateAxis> hit_test_translate_gizmo(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& position);
std::optional<ViewportRotationBasis> rotation_basis(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t bone_index);
std::optional<double> rotation_angle(
    const ViewportRotationBasis& basis,
    const ViewportWorldPoint& pointer_world);
double unwrap_rotation_delta(double delta);
bool rotation_gizmo_visible(
    const ShellState& state,
    const ViewportLayout& layout);
bool hit_test_rotation_gizmo(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& position);
std::optional<ViewportScaleBasis> scale_basis(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t bone_index);
std::optional<ViewportScaleCandidate> scale_candidate(
    const ViewportScaleGesturePayload& payload,
    const ViewportLayout& layout,
    const ImVec2& pointer);
bool scale_gizmo_visible(
    const ShellState& state,
    const ViewportLayout& layout);
bool uniform_scale_handle_visible(
    const ShellState& state,
    const ViewportLayout& layout);
const char* transform_hint(
    const ShellState& state,
    const ViewportLayout& layout);
std::optional<ViewportScaleHandle> hit_test_scale_gizmo(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& position);
bool begin_translate_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    ViewportTranslateAxis axis,
    const ImVec2& pointer);
bool update_translate_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer);
bool begin_rotate_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer);
bool update_rotate_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer);
bool begin_scale_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    ViewportScaleHandle handle,
    const ImVec2& pointer);
bool update_scale_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer);
void finish_transform_gesture(ShellState* state, bool commit);
bool begin_box_selection(
    ShellState* state,
    const ImVec2& pointer,
    bool additive);
bool update_box_selection(
    ShellState* state,
    const ImVec2& pointer);
bool finish_box_selection(
    ShellState* state,
    const ViewportLayout& layout,
    bool commit);

} // namespace viewport_interaction

} // namespace marrow::editor::shell
