#pragma once

#include <cstddef>
#include <optional>

#include "shell_state.hpp"
#include "viewport_interaction_kernel.hpp"

namespace marrow::editor::shell {

using ViewportPressTarget =
    marrow::editor::viewport_interaction_kernel::PressTarget;

namespace viewport_interaction {

inline constexpr float kTranslateGizmoLength = 42.0f;
inline constexpr float kTranslateGizmoHitRadius = 7.0f;
inline constexpr float kRotationGizmoRadius = 58.0f;
inline constexpr float kRotationGizmoHitBand = 6.0f;
inline constexpr float kRotationPivotSuspendRadius = 2.0f;
inline constexpr float kScaleGizmoRadius = 74.0f;
inline constexpr float kScaleGizmoHitRadius = 6.0f;
inline constexpr float kViewportBoxDragThreshold = 4.0f;

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
std::optional<std::size_t> rotation_gizmo_bone_index(
    const ShellState& state,
    const ViewportLayout& layout);
const char* active_rotation_inherit_hint(const ShellState& state);
ImVec2 rotation_gizmo_center(
    const ShellState& state,
    const ViewportLayout& layout,
    std::size_t bone_index);
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
const ImVec2& scale_handle_direction(
    const ViewportScaleBasis& basis,
    ViewportScaleHandle handle);
std::optional<ViewportScaleCandidate> scale_candidate(
    const ViewportScaleGesturePayload& payload,
    const ViewportLayout& layout,
    const ImVec2& pointer);
std::optional<std::size_t> scale_gizmo_bone_index(
    const ShellState& state,
    const ViewportLayout& layout);
ViewportScaleBasis visible_scale_basis(
    const ShellState& state,
    std::size_t bone_index);
ImVec2 scale_gizmo_center(
    const ShellState& state,
    const ViewportLayout& layout,
    std::size_t bone_index);
ImVec2 scale_handle_endpoint(
    const ImVec2& center,
    const ViewportScaleBasis& basis,
    ViewportScaleHandle handle);
bool uniform_scale_handle_enabled(
    const ShellState& state,
    std::size_t bone_index);
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
std::optional<marrow::runtime::AttachmentVertex> local_position_for_world_target(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t bone_index,
    const ViewportWorldPoint& target);

} // namespace viewport_interaction
} // namespace marrow::editor::shell
