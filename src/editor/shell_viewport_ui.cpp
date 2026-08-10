#include "shell_viewport_ui.hpp"

#include "shell_preview.hpp"
#include "shell_project_panels.hpp"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "imgui.h"

#include "shell_selection.hpp"
#include "shell_theme.hpp"
#include "shell_timeline.hpp"
#include "shell_weight_paint.hpp"
#include "shell_widgets.hpp"

namespace marrow::editor::shell {

using marrow::editor::Icon;
using marrow::editor::IconRegistry;

void auto_frame_skeleton(ShellState* state, ImVec2 canvas_size) {
    (void)canvas_size;
    if (state == nullptr || state->viewport_transform_gesture.has_value()) {
        return;
    }
    if (frame_viewport_camera_to_preview_pose(state)) {
        state->status_message = "Framed skeleton to viewport";
    }
}

namespace {

constexpr float kTranslateGizmoLength = 42.0f;
constexpr float kTranslateGizmoHitRadius = 7.0f;
constexpr float kRotationGizmoRadius = 58.0f;
constexpr float kRotationGizmoHitBand = 6.0f;
constexpr float kRotationPivotSuspendRadius = 2.0f;
constexpr float kScaleGizmoRadius = 74.0f;
constexpr float kScaleGizmoHitRadius = 6.0f;
constexpr float kViewportBoxDragThreshold = 4.0f;

ViewportPressTarget viewport_press_target(
    bool active_gesture,
    bool weight_brush,
    bool translate,
    bool rotation,
    bool scale,
    bool entity) {
    if (active_gesture) return ViewportPressTarget::ActiveGesture;
    if (weight_brush) return ViewportPressTarget::WeightBrush;
    if (translate) return ViewportPressTarget::Translate;
    if (rotation) return ViewportPressTarget::Rotation;
    if (scale) return ViewportPressTarget::Scale;
    if (entity) return ViewportPressTarget::Entity;
    return ViewportPressTarget::Box;
}

bool finite_world_point(const ViewportWorldPoint& point) {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool unsupported_rotation_inherit(marrow::runtime::BoneInherit inherit) {
    return inherit == marrow::runtime::BoneInherit::NoRotationOrReflection ||
        inherit == marrow::runtime::BoneInherit::NoScale ||
        inherit == marrow::runtime::BoneInherit::NoScaleOrReflection;
}

const char* unsupported_rotation_inherit_hint(marrow::runtime::BoneInherit inherit) {
    switch (inherit) {
    case marrow::runtime::BoneInherit::NoRotationOrReflection:
        return "Rotation/scale unavailable: inherit noRotationOrReflection";
    case marrow::runtime::BoneInherit::NoScale:
        return "Rotation/scale unavailable: inherit noScale";
    case marrow::runtime::BoneInherit::NoScaleOrReflection:
        return "Rotation/scale unavailable: inherit noScaleOrReflection";
    case marrow::runtime::BoneInherit::Normal:
    case marrow::runtime::BoneInherit::OnlyTranslation:
        return nullptr;
    }
    return nullptr;
}

std::optional<ViewportRotationBasis> rotation_basis_impl(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t bone_index) {
    if (skeleton.data() == nullptr ||
        bone_index >= skeleton.data()->bones().size() ||
        bone_index >= skeleton.bone_poses().size() ||
        bone_index >= skeleton.bone_world_transforms().size()) {
        return std::nullopt;
    }

    const marrow::runtime::BoneInherit inherit =
        skeleton.bone_poses()[bone_index].inherit;
    if (unsupported_rotation_inherit(inherit)) {
        return std::nullopt;
    }

    const auto world = skeleton.bone_world_transforms()[bone_index];
    ViewportRotationBasis basis;
    basis.pivot_world = ViewportWorldPoint{world.world_x, world.world_y};
    basis.inherit = inherit;

    double a = 0.0;
    double b = 0.0;
    double c = 0.0;
    double d = 0.0;
    const auto& bone = skeleton.data()->bones()[bone_index];
    if (!bone.parent_index.has_value() ||
        inherit == marrow::runtime::BoneInherit::OnlyTranslation) {
        a = skeleton.scale_x();
        d = skeleton.scale_y();
    } else {
        const std::size_t parent_index = *bone.parent_index;
        if (parent_index >= skeleton.bone_world_transforms().size()) {
            return std::nullopt;
        }
        const auto parent = skeleton.bone_world_transforms()[parent_index];
        a = parent.a;
        b = parent.b;
        c = parent.c;
        d = parent.d;
    }

    if (!finite_world_point(basis.pivot_world) || !std::isfinite(a) ||
        !std::isfinite(b) || !std::isfinite(c) || !std::isfinite(d)) {
        return std::nullopt;
    }
    const double matrix_scale = std::max(
        {std::abs(a), std::abs(b), std::abs(c), std::abs(d)});
    if (!std::isfinite(matrix_scale) || matrix_scale <= 0.0) {
        return std::nullopt;
    }
    const double normalized_a = a / matrix_scale;
    const double normalized_b = b / matrix_scale;
    const double normalized_c = c / matrix_scale;
    const double normalized_d = d / matrix_scale;
    const double normalized_determinant =
        (normalized_a * normalized_d) - (normalized_b * normalized_c);
    constexpr double kRelativeSingularEpsilon = 1e-8;
    if (!std::isfinite(normalized_determinant) ||
        std::abs(normalized_determinant) <= kRelativeSingularEpsilon) {
        return std::nullopt;
    }
    const double inverse_scale = 1.0 / matrix_scale;
    basis.inverse_a = normalized_d * inverse_scale / normalized_determinant;
    basis.inverse_b = -normalized_b * inverse_scale / normalized_determinant;
    basis.inverse_c = -normalized_c * inverse_scale / normalized_determinant;
    basis.inverse_d = normalized_a * inverse_scale / normalized_determinant;
    if (!std::isfinite(basis.inverse_a) || !std::isfinite(basis.inverse_b) ||
        !std::isfinite(basis.inverse_c) || !std::isfinite(basis.inverse_d)) {
        return std::nullopt;
    }
    return basis;
}

std::optional<double> rotation_angle_impl(
    const ViewportRotationBasis& basis,
    const ViewportWorldPoint& pointer_world) {
    if (!finite_world_point(pointer_world) ||
        !finite_world_point(basis.pivot_world)) {
        return std::nullopt;
    }
    const double dx = pointer_world.x - basis.pivot_world.x;
    const double dy = pointer_world.y - basis.pivot_world.y;
    const double local_x = (basis.inverse_a * dx) + (basis.inverse_b * dy);
    const double local_y = (basis.inverse_c * dx) + (basis.inverse_d * dy);
    if (!std::isfinite(local_x) || !std::isfinite(local_y) ||
        (local_x == 0.0 && local_y == 0.0)) {
        return std::nullopt;
    }
    const double angle = std::atan2(local_y, local_x) * 180.0 /
        static_cast<double>(kPi);
    return std::isfinite(angle) ? std::optional<double>(angle) : std::nullopt;
}

double unwrap_rotation_delta_impl(double delta) {
    if (!std::isfinite(delta)) {
        return delta;
    }
    double wrapped = std::fmod(delta, 360.0);
    if (wrapped <= -180.0) {
        wrapped += 360.0;
    } else if (wrapped > 180.0) {
        wrapped -= 360.0;
    }
    return wrapped;
}

std::optional<ViewportScaleBasis> scale_basis_impl(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t bone_index) {
    if (skeleton.data() == nullptr ||
        bone_index >= skeleton.data()->bones().size() ||
        bone_index >= skeleton.bone_poses().size() ||
        bone_index >= skeleton.bone_world_transforms().size()) {
        return std::nullopt;
    }

    const auto& pose = skeleton.bone_poses()[bone_index];
    if (unsupported_rotation_inherit(pose.inherit)) {
        return std::nullopt;
    }
    const auto world = skeleton.bone_world_transforms()[bone_index];
    ViewportScaleBasis basis;
    basis.pivot_world = ViewportWorldPoint{world.world_x, world.world_y};
    basis.inherit = pose.inherit;

    double parent_a = 0.0;
    double parent_b = 0.0;
    double parent_c = 0.0;
    double parent_d = 0.0;
    const auto& bone = skeleton.data()->bones()[bone_index];
    if (!bone.parent_index.has_value() ||
        pose.inherit == marrow::runtime::BoneInherit::OnlyTranslation) {
        parent_a = skeleton.scale_x();
        parent_d = skeleton.scale_y();
    } else {
        const std::size_t parent_index = *bone.parent_index;
        if (parent_index >= skeleton.bone_world_transforms().size()) {
            return std::nullopt;
        }
        const auto parent = skeleton.bone_world_transforms()[parent_index];
        parent_a = parent.a;
        parent_b = parent.b;
        parent_c = parent.c;
        parent_d = parent.d;
    }

    const double matrix_scale = std::max(
        {std::abs(parent_a), std::abs(parent_b),
         std::abs(parent_c), std::abs(parent_d)});
    if (!finite_world_point(basis.pivot_world) ||
        !std::isfinite(matrix_scale) || matrix_scale <= 0.0 ||
        !std::isfinite(pose.local_pose.rotation) ||
        !std::isfinite(pose.local_pose.shear_x) ||
        !std::isfinite(pose.local_pose.shear_y)) {
        return std::nullopt;
    }
    const double normalized_determinant =
        ((parent_a / matrix_scale) * (parent_d / matrix_scale)) -
        ((parent_b / matrix_scale) * (parent_c / matrix_scale));
    constexpr double kRelativeSingularEpsilon = 1e-8;
    if (!std::isfinite(normalized_determinant) ||
        std::abs(normalized_determinant) <= kRelativeSingularEpsilon) {
        return std::nullopt;
    }

    const double x_angle =
        (static_cast<double>(pose.local_pose.rotation) +
         static_cast<double>(pose.local_pose.shear_x)) *
        static_cast<double>(kPi) / 180.0;
    const double y_angle =
        (static_cast<double>(pose.local_pose.rotation) + 90.0 +
         static_cast<double>(pose.local_pose.shear_y)) *
        static_cast<double>(kPi) / 180.0;
    const double local_x_x = std::cos(x_angle);
    const double local_x_y = std::sin(x_angle);
    const double local_y_x = std::cos(y_angle);
    const double local_y_y = std::sin(y_angle);
    double screen_x_x =
        (parent_a * local_x_x) + (parent_b * local_x_y);
    double screen_x_y =
        -((parent_c * local_x_x) + (parent_d * local_x_y));
    double screen_y_x =
        (parent_a * local_y_x) + (parent_b * local_y_y);
    double screen_y_y =
        -((parent_c * local_y_x) + (parent_d * local_y_y));
    const double x_length = std::hypot(screen_x_x, screen_x_y);
    const double y_length = std::hypot(screen_y_x, screen_y_y);
    if (!std::isfinite(x_length) || !std::isfinite(y_length) ||
        x_length <= 1e-8 || y_length <= 1e-8) {
        return std::nullopt;
    }
    screen_x_x /= x_length;
    screen_x_y /= x_length;
    screen_y_x /= y_length;
    screen_y_y /= y_length;
    const double direction_determinant =
        (screen_x_x * screen_y_y) - (screen_x_y * screen_y_x);
    if (!std::isfinite(direction_determinant) ||
        std::abs(direction_determinant) <= kRelativeSingularEpsilon) {
        return std::nullopt;
    }
    double uniform_x = screen_x_x + screen_y_x;
    double uniform_y = screen_x_y + screen_y_y;
    const double uniform_length = std::hypot(uniform_x, uniform_y);
    if (!std::isfinite(uniform_length) || uniform_length <= 1e-8) {
        return std::nullopt;
    }
    uniform_x /= uniform_length;
    uniform_y /= uniform_length;
    basis.positive_x_screen_direction = ImVec2(
        static_cast<float>(screen_x_x), static_cast<float>(screen_x_y));
    basis.positive_y_screen_direction = ImVec2(
        static_cast<float>(screen_y_x), static_cast<float>(screen_y_y));
    basis.uniform_screen_direction = ImVec2(
        static_cast<float>(uniform_x), static_cast<float>(uniform_y));
    return basis;
}

const ImVec2& scale_handle_direction(
    const ViewportScaleBasis& basis,
    ViewportScaleHandle handle) {
    switch (handle) {
    case ViewportScaleHandle::X:
        return basis.positive_x_screen_direction;
    case ViewportScaleHandle::Y:
        return basis.positive_y_screen_direction;
    case ViewportScaleHandle::Uniform:
        return basis.uniform_screen_direction;
    }
    return basis.positive_x_screen_direction;
}

std::optional<ViewportScaleCandidate> scale_candidate_impl(
    const ViewportScaleGesturePayload& payload,
    const ViewportLayout& layout,
    const ImVec2& pointer) {
    if (!std::isfinite(layout.pixels_per_unit) || layout.pixels_per_unit <= 0.0f ||
        !std::isfinite(pointer.x) || !std::isfinite(pointer.y) ||
        !finite_world_point(payload.basis.pivot_world) ||
        !std::isfinite(payload.start_projection_pixels) ||
        std::abs(payload.start_projection_pixels) <= 1e-8 ||
        !std::isfinite(payload.start_absolute_scale_x) ||
        !std::isfinite(payload.start_absolute_scale_y)) {
        return std::nullopt;
    }
    const ImVec2 pivot = screen_from_world(
        layout, payload.basis.pivot_world.x, payload.basis.pivot_world.y);
    const ImVec2& direction =
        scale_handle_direction(payload.basis, payload.handle);
    if (!std::isfinite(pivot.x) || !std::isfinite(pivot.y) ||
        !std::isfinite(direction.x) || !std::isfinite(direction.y)) {
        return std::nullopt;
    }
    const double projection =
        (static_cast<double>(pointer.x) - static_cast<double>(pivot.x)) *
            static_cast<double>(direction.x) +
        (static_cast<double>(pointer.y) - static_cast<double>(pivot.y)) *
            static_cast<double>(direction.y);
    if (!std::isfinite(projection)) {
        return std::nullopt;
    }

    ViewportScaleCandidate candidate{
        payload.start_absolute_scale_x,
        payload.start_absolute_scale_y};
    if (payload.handle == ViewportScaleHandle::Uniform) {
        if (payload.start_absolute_scale_x == 0.0 &&
            payload.start_absolute_scale_y == 0.0) {
            return std::nullopt;
        }
        const double ratio = projection / payload.start_projection_pixels;
        candidate.scale_x = payload.start_absolute_scale_x * ratio;
        candidate.scale_y = payload.start_absolute_scale_y * ratio;
    } else {
        double* component = payload.handle == ViewportScaleHandle::X
            ? &candidate.scale_x
            : &candidate.scale_y;
        const double start_component = *component;
        if (start_component == 0.0) {
            *component =
                (projection - payload.start_projection_pixels) /
                static_cast<double>(kScaleGizmoRadius);
        } else {
            *component =
                start_component * projection / payload.start_projection_pixels;
        }
    }
    if (!std::isfinite(candidate.scale_x) ||
        !std::isfinite(candidate.scale_y)) {
        return std::nullopt;
    }
    if (candidate.scale_x == 0.0) {
        candidate.scale_x = 0.0;
    }
    if (candidate.scale_y == 0.0) {
        candidate.scale_y = 0.0;
    }
    return candidate;
}

std::optional<ViewportTranslateAxis> hit_test_translate_gizmo(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& position) {
    const ResolvedSelection resolved = resolve_shell_selection(state);
    if (state.shell_mode != ShellMode::Animation ||
        state.selected_animation_name.empty() || state.weight_paint.enabled ||
        !resolved.active_bone_index.has_value() ||
        *resolved.active_bone_index >= layout.bones.size()) {
        return std::nullopt;
    }
    const ImVec2 origin = layout.bones[*resolved.active_bone_index].screen_position;
    if (squared_distance(origin, position) <=
        kTranslateGizmoHitRadius * kTranslateGizmoHitRadius) {
        return ViewportTranslateAxis::Free;
    }
    const ImVec2 x_end(origin.x + kTranslateGizmoLength, origin.y);
    if (point_segment_distance_squared(position, origin, x_end) <=
        kTranslateGizmoHitRadius * kTranslateGizmoHitRadius) {
        return ViewportTranslateAxis::X;
    }
    const ImVec2 y_end(origin.x, origin.y - kTranslateGizmoLength);
    if (point_segment_distance_squared(position, origin, y_end) <=
        kTranslateGizmoHitRadius * kTranslateGizmoHitRadius) {
        return ViewportTranslateAxis::Y;
    }
    return std::nullopt;
}

void draw_translate_gizmo(
    const ShellState& state,
    const ViewportLayout& layout,
    ImDrawList* draw_list) {
    const ResolvedSelection resolved = resolve_shell_selection(state);
    if (draw_list == nullptr || state.shell_mode != ShellMode::Animation ||
        state.selected_animation_name.empty() ||
        state.weight_paint.enabled || !resolved.active_bone_index.has_value() ||
        *resolved.active_bone_index >= layout.bones.size()) {
        return;
    }
    const ImVec2 origin = layout.bones[*resolved.active_bone_index].screen_position;
    const ImVec2 x_end(origin.x + kTranslateGizmoLength, origin.y);
    const ImVec2 y_end(origin.x, origin.y - kTranslateGizmoLength);
    draw_list->AddLine(origin, x_end, IM_COL32(239, 91, 91, 255), 3.0f);
    draw_list->AddTriangleFilled(
        x_end,
        ImVec2(x_end.x - 8.0f, x_end.y - 5.0f),
        ImVec2(x_end.x - 8.0f, x_end.y + 5.0f),
        IM_COL32(239, 91, 91, 255));
    draw_list->AddLine(origin, y_end, IM_COL32(102, 204, 124, 255), 3.0f);
    draw_list->AddTriangleFilled(
        y_end,
        ImVec2(y_end.x - 5.0f, y_end.y + 8.0f),
        ImVec2(y_end.x + 5.0f, y_end.y + 8.0f),
        IM_COL32(102, 204, 124, 255));
    draw_list->AddCircleFilled(origin, 6.0f, IM_COL32(244, 198, 88, 255), 16);
    draw_list->AddCircle(origin, 7.0f, IM_COL32(18, 21, 25, 255), 16, 1.0f);
}

std::optional<std::size_t> rotation_gizmo_bone_index(
    const ShellState& state,
    const ViewportLayout& layout) {
    const ResolvedSelection resolved = resolve_shell_selection(state);
    if (state.shell_mode != ShellMode::Animation ||
        state.selected_animation_name.empty() || state.weight_paint.enabled ||
        state.preview_skeleton == nullptr || state.load_result.skeleton_data == nullptr ||
        state.session.runtime_data() == nullptr ||
        state.session.runtime_data()->find_animation(state.selected_animation_name) == nullptr ||
        !resolved.active_bone_index.has_value()) {
        return std::nullopt;
    }
    const std::size_t bone_index = *resolved.active_bone_index;
    if (bone_index >= layout.bones.size() ||
        bone_index >= state.preview_skeleton->bone_poses().size() ||
        bone_index >= state.preview_skeleton->bone_world_transforms().size() ||
        !state.preview_skeleton->is_bone_active(bone_index) ||
        unsupported_rotation_inherit(
            state.preview_skeleton->bone_poses()[bone_index].inherit)) {
        return std::nullopt;
    }
    return bone_index;
}

const char* active_rotation_inherit_hint(const ShellState& state) {
    const ResolvedSelection resolved = resolve_shell_selection(state);
    if (state.shell_mode != ShellMode::Animation ||
        state.selected_animation_name.empty() || state.weight_paint.enabled ||
        state.preview_skeleton == nullptr || !resolved.active_bone_index.has_value() ||
        *resolved.active_bone_index >= state.preview_skeleton->bone_poses().size() ||
        !state.preview_skeleton->is_bone_active(*resolved.active_bone_index)) {
        return nullptr;
    }
    return unsupported_rotation_inherit_hint(
        state.preview_skeleton->bone_poses()[*resolved.active_bone_index].inherit);
}

ImVec2 rotation_gizmo_center(
    const ShellState& state,
    const ViewportLayout& layout,
    std::size_t bone_index) {
    if (state.viewport_transform_gesture.has_value()) {
        if (const auto* rotate = std::get_if<ViewportRotateGesturePayload>(
                &state.viewport_transform_gesture->payload)) {
            return screen_from_world(
                layout, rotate->basis.pivot_world.x, rotate->basis.pivot_world.y);
        }
    }
    return layout.bones[bone_index].screen_position;
}

bool hit_test_rotation_gizmo(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& position) {
    const auto bone_index = rotation_gizmo_bone_index(state, layout);
    if (!bone_index.has_value() || !std::isfinite(position.x) ||
        !std::isfinite(position.y)) {
        return false;
    }
    const ImVec2 center = rotation_gizmo_center(state, layout, *bone_index);
    const double distance = std::sqrt(static_cast<double>(squared_distance(center, position)));
    return std::isfinite(distance) &&
        std::abs(distance - kRotationGizmoRadius) <= kRotationGizmoHitBand;
}

void draw_rotation_gizmo(
    const ShellState& state,
    const ViewportLayout& layout,
    bool hovered,
    ImDrawList* draw_list) {
    const auto bone_index = rotation_gizmo_bone_index(state, layout);
    if (draw_list == nullptr || !bone_index.has_value()) {
        return;
    }
    const auto* active_rotate = state.viewport_transform_gesture.has_value()
        ? std::get_if<ViewportRotateGesturePayload>(
              &state.viewport_transform_gesture->payload)
        : nullptr;
    const ImVec2 center = rotation_gizmo_center(state, layout, *bone_index);
    const ImU32 ring_color = active_rotate != nullptr
        ? IM_COL32(255, 207, 104, 255)
        : hovered ? IM_COL32(236, 224, 179, 255)
                  : IM_COL32(151, 166, 190, 225);
    draw_list->AddCircle(center, kRotationGizmoRadius, ring_color, 64, 2.25f);

    if (*bone_index < state.preview_skeleton->bone_world_transforms().size()) {
        const auto world =
            state.preview_skeleton->bone_world_transforms()[*bone_index];
        double axis_x = world.a;
        double axis_y = -world.c;
        const double length = std::hypot(axis_x, axis_y);
        if (std::isfinite(length) && length > 1e-8) {
            axis_x /= length;
            axis_y /= length;
            draw_list->AddLine(
                ImVec2(
                    center.x + static_cast<float>(axis_x * (kRotationGizmoRadius - 7.0f)),
                    center.y + static_cast<float>(axis_y * (kRotationGizmoRadius - 7.0f))),
                ImVec2(
                    center.x + static_cast<float>(axis_x * (kRotationGizmoRadius + 7.0f)),
                    center.y + static_cast<float>(axis_y * (kRotationGizmoRadius + 7.0f))),
                IM_COL32(255, 225, 151, 255),
                2.0f);
        }
    }

    if (active_rotate == nullptr) {
        return;
    }
    draw_list->AddLine(
        center,
        active_rotate->pointer_screen,
        IM_COL32(255, 207, 104, 190),
        1.5f);
    std::ostringstream label;
    label << std::fixed << std::setprecision(1)
          << active_rotate->current_absolute_rotation << " deg";
    const std::string text = label.str();
    draw_list->AddText(
        ImVec2(center.x + 10.0f, center.y + kRotationGizmoRadius + 10.0f),
        IM_COL32(255, 232, 174, 255),
        text.c_str());
}

std::optional<std::size_t> scale_gizmo_bone_index(
    const ShellState& state,
    const ViewportLayout& layout) {
    const auto bone_index = rotation_gizmo_bone_index(state, layout);
    if (!bone_index.has_value() ||
        !scale_basis_impl(*state.preview_skeleton, *bone_index).has_value()) {
        return std::nullopt;
    }
    return bone_index;
}

std::optional<ViewportScaleCandidate> effective_scale_at_playhead(
    const ShellState& state,
    std::size_t bone_index) {
    const auto* runtime_data = state.session.runtime_data();
    if (runtime_data == nullptr || bone_index >= runtime_data->bones().size()) {
        return std::nullopt;
    }
    const auto* animation =
        runtime_data->find_animation(state.selected_animation_name);
    if (animation == nullptr || !std::isfinite(state.timeline_time_seconds)) {
        return std::nullopt;
    }
    const auto sampled =
        animation->sample_bone_scale(bone_index, state.timeline_time_seconds);
    const auto& setup = runtime_data->bones()[bone_index].setup_pose;
    const ViewportScaleCandidate scale = sampled.has_value()
        ? ViewportScaleCandidate{sampled->x, sampled->y}
        : ViewportScaleCandidate{
              static_cast<double>(setup.scale_x),
              static_cast<double>(setup.scale_y)};
    if (!std::isfinite(scale.scale_x) || !std::isfinite(scale.scale_y)) {
        return std::nullopt;
    }
    return scale;
}

ViewportScaleBasis visible_scale_basis(
    const ShellState& state,
    std::size_t bone_index) {
    if (state.viewport_transform_gesture.has_value()) {
        if (const auto* scale = std::get_if<ViewportScaleGesturePayload>(
                &state.viewport_transform_gesture->payload)) {
            return scale->basis;
        }
    }
    return *scale_basis_impl(*state.preview_skeleton, bone_index);
}

ImVec2 scale_gizmo_center(
    const ShellState& state,
    const ViewportLayout& layout,
    std::size_t bone_index) {
    const ViewportScaleBasis basis = visible_scale_basis(state, bone_index);
    return screen_from_world(
        layout, basis.pivot_world.x, basis.pivot_world.y);
}

ImVec2 scale_handle_endpoint(
    const ImVec2& center,
    const ViewportScaleBasis& basis,
    ViewportScaleHandle handle) {
    const ImVec2& direction = scale_handle_direction(basis, handle);
    return ImVec2(
        center.x + direction.x * kScaleGizmoRadius,
        center.y + direction.y * kScaleGizmoRadius);
}

bool uniform_scale_handle_enabled(
    const ShellState& state,
    std::size_t bone_index) {
    if (state.viewport_transform_gesture.has_value()) {
        if (const auto* scale = std::get_if<ViewportScaleGesturePayload>(
                &state.viewport_transform_gesture->payload)) {
            return scale->start_absolute_scale_x != 0.0 ||
                scale->start_absolute_scale_y != 0.0;
        }
    }
    const auto values = effective_scale_at_playhead(state, bone_index);
    return values.has_value() &&
        (values->scale_x != 0.0 || values->scale_y != 0.0);
}

const char* active_transform_gizmo_hint(
    const ShellState& state,
    const ViewportLayout* layout) {
    const char* hint = active_rotation_inherit_hint(state);
    if (hint == nullptr && layout != nullptr) {
        const auto scale_bone_index =
            scale_gizmo_bone_index(state, *layout);
        if (scale_bone_index.has_value() &&
            !uniform_scale_handle_enabled(state, *scale_bone_index)) {
            hint = "Uniform scale unavailable: both axes are zero";
        }
    }
    return hint;
}

std::optional<ViewportScaleHandle> hit_test_scale_gizmo(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& position) {
    const auto bone_index = scale_gizmo_bone_index(state, layout);
    if (!bone_index.has_value() || !std::isfinite(position.x) ||
        !std::isfinite(position.y)) {
        return std::nullopt;
    }
    const ViewportScaleBasis basis =
        visible_scale_basis(state, *bone_index);
    const ImVec2 center =
        scale_gizmo_center(state, layout, *bone_index);
    constexpr ViewportScaleHandle kHandles[] = {
        ViewportScaleHandle::X,
        ViewportScaleHandle::Y,
        ViewportScaleHandle::Uniform};
    std::optional<ViewportScaleHandle> closest;
    float closest_distance_squared =
        kScaleGizmoHitRadius * kScaleGizmoHitRadius;
    for (const ViewportScaleHandle handle : kHandles) {
        if (handle == ViewportScaleHandle::Uniform &&
            !uniform_scale_handle_enabled(state, *bone_index)) {
            continue;
        }
        const float distance_squared = squared_distance(
            scale_handle_endpoint(center, basis, handle), position);
        if (distance_squared <=
                kScaleGizmoHitRadius * kScaleGizmoHitRadius &&
            (!closest.has_value() ||
             distance_squared < closest_distance_squared)) {
            closest = handle;
            closest_distance_squared = distance_squared;
        }
    }
    return closest;
}

void draw_scale_gizmo(
    const ShellState& state,
    const ViewportLayout& layout,
    std::optional<ViewportScaleHandle> hovered_handle,
    ImDrawList* draw_list) {
    const auto bone_index = scale_gizmo_bone_index(state, layout);
    if (draw_list == nullptr || !bone_index.has_value()) {
        return;
    }
    const auto* active_scale = state.viewport_transform_gesture.has_value()
        ? std::get_if<ViewportScaleGesturePayload>(
              &state.viewport_transform_gesture->payload)
        : nullptr;
    const ViewportScaleBasis basis =
        visible_scale_basis(state, *bone_index);
    const ImVec2 center =
        scale_gizmo_center(state, layout, *bone_index);

    const auto draw_axis = [&](ViewportScaleHandle handle, ImU32 color) {
        const ImVec2& direction = scale_handle_direction(basis, handle);
        const ImVec2 line_start(
            center.x + direction.x *
                (kRotationGizmoRadius + kRotationGizmoHitBand + 1.0f),
            center.y + direction.y *
                (kRotationGizmoRadius + kRotationGizmoHitBand + 1.0f));
        const ImVec2 endpoint =
            scale_handle_endpoint(center, basis, handle);
        const bool active =
            active_scale != nullptr && active_scale->handle == handle;
        const bool hovered =
            hovered_handle.has_value() && *hovered_handle == handle;
        const ImU32 display_color = active
            ? IM_COL32(255, 232, 174, 255)
            : hovered ? IM_COL32(246, 238, 211, 255) : color;
        draw_list->AddLine(line_start, endpoint, display_color, 2.0f);
        draw_list->AddRectFilled(
            ImVec2(endpoint.x - 4.0f, endpoint.y - 4.0f),
            ImVec2(endpoint.x + 4.0f, endpoint.y + 4.0f),
            display_color,
            1.0f);
        draw_list->AddRect(
            ImVec2(endpoint.x - 5.0f, endpoint.y - 5.0f),
            ImVec2(endpoint.x + 5.0f, endpoint.y + 5.0f),
            IM_COL32(18, 21, 25, 255),
            1.0f);
    };
    draw_axis(ViewportScaleHandle::X, IM_COL32(239, 91, 91, 255));
    draw_axis(ViewportScaleHandle::Y, IM_COL32(102, 204, 124, 255));

    if (uniform_scale_handle_enabled(state, *bone_index)) {
        const ImVec2& direction =
            basis.uniform_screen_direction;
        const ImVec2 endpoint = scale_handle_endpoint(
            center, basis, ViewportScaleHandle::Uniform);
        const bool active = active_scale != nullptr &&
            active_scale->handle == ViewportScaleHandle::Uniform;
        const bool hovered = hovered_handle.has_value() &&
            *hovered_handle == ViewportScaleHandle::Uniform;
        const ImU32 color = active
            ? IM_COL32(255, 232, 174, 255)
            : hovered ? IM_COL32(246, 238, 211, 255)
                      : IM_COL32(244, 198, 88, 255);
        draw_list->AddLine(
            ImVec2(
                center.x + direction.x *
                    (kRotationGizmoRadius + kRotationGizmoHitBand + 1.0f),
                center.y + direction.y *
                    (kRotationGizmoRadius + kRotationGizmoHitBand + 1.0f)),
            endpoint,
            color,
            2.0f);
        draw_list->AddQuadFilled(
            ImVec2(endpoint.x, endpoint.y - 6.0f),
            ImVec2(endpoint.x + 6.0f, endpoint.y),
            ImVec2(endpoint.x, endpoint.y + 6.0f),
            ImVec2(endpoint.x - 6.0f, endpoint.y),
            color);
        draw_list->AddQuad(
            ImVec2(endpoint.x, endpoint.y - 7.0f),
            ImVec2(endpoint.x + 7.0f, endpoint.y),
            ImVec2(endpoint.x, endpoint.y + 7.0f),
            ImVec2(endpoint.x - 7.0f, endpoint.y),
            IM_COL32(18, 21, 25, 255),
            1.0f);
    }

    if (active_scale == nullptr) {
        return;
    }
    std::ostringstream label;
    label << std::fixed << std::setprecision(3)
          << active_scale->current_absolute_scale_x << ", "
          << active_scale->current_absolute_scale_y;
    const std::string text = label.str();
    draw_list->AddText(
        ImVec2(center.x + 10.0f, center.y + kScaleGizmoRadius + 10.0f),
        IM_COL32(255, 232, 174, 255),
        text.c_str());
}

bool begin_viewport_box_selection(
    ShellState* state,
    const ImVec2& pointer,
    bool additive) {
    if (state == nullptr || state->viewport_box_selection.has_value() ||
        authoring_gesture_active(*state)) {
        return false;
    }
    state->viewport_box_selection = ViewportBoxSelectionGesture{
        pointer, pointer, additive, false};
    return true;
}

bool update_viewport_box_selection(
    ShellState* state,
    const ImVec2& pointer) {
    if (state == nullptr || !state->viewport_box_selection.has_value()) {
        return false;
    }
    auto& box = *state->viewport_box_selection;
    box.current = pointer;
    const float dx = pointer.x - box.start.x;
    const float dy = pointer.y - box.start.y;
    box.dragged = box.dragged ||
        (dx * dx) + (dy * dy) >=
            kViewportBoxDragThreshold * kViewportBoxDragThreshold;
    return true;
}

bool finish_viewport_box_selection(
    ShellState* state,
    const ViewportLayout& layout,
    bool commit) {
    if (state == nullptr || !state->viewport_box_selection.has_value()) {
        return false;
    }
    const ViewportBoxSelectionGesture box = *state->viewport_box_selection;
    state->viewport_box_selection.reset();
    if (!commit || !box.dragged) {
        return false;
    }
    return apply_viewport_box_selection_gesture(
        state,
        collect_viewport_box_bones(*state, layout, box.start, box.current),
        box.additive,
        true);
}

std::optional<marrow::runtime::AttachmentVertex> local_position_for_world_target_impl(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t bone_index,
    const ViewportWorldPoint& target) {
    if (bone_index >= skeleton.data()->bones().size() ||
        bone_index >= skeleton.bone_world_transforms().size()) {
        return std::nullopt;
    }
    const auto& bone = skeleton.data()->bones()[bone_index];
    if (!bone.parent_index.has_value()) {
        constexpr double kEpsilon = 1e-8;
        if (std::abs(skeleton.scale_x()) <= kEpsilon ||
            std::abs(skeleton.scale_y()) <= kEpsilon) {
            return std::nullopt;
        }
        return marrow::runtime::AttachmentVertex{
            target.x / skeleton.scale_x(),
            target.y / skeleton.scale_y()};
    }
    const std::size_t parent_index = *bone.parent_index;
    if (parent_index >= skeleton.bone_world_transforms().size()) {
        return std::nullopt;
    }
    const auto parent = skeleton.bone_world_transforms()[parent_index];
    const double a = parent.a;
    const double b = parent.b;
    const double c = parent.c;
    const double d = parent.d;
    const double determinant = (a * d) - (b * c);
    if (std::abs(determinant) <= 1e-8) {
        return std::nullopt;
    }
    const double dx = target.x - parent.world_x;
    const double dy = target.y - parent.world_y;
    return marrow::runtime::AttachmentVertex{
        ((dx * d) - (dy * b)) / determinant,
        ((dy * a) - (dx * c)) / determinant};
}

bool selection_context_matches(
    const marrow::editor::SelectionSet& current,
    const marrow::editor::SelectionSet& captured) {
    if (current.items() != captured.items()) {
        return false;
    }
    const auto* current_active = current.active();
    const auto* captured_active = captured.active();
    return (current_active == nullptr && captured_active == nullptr) ||
        (current_active != nullptr && captured_active != nullptr &&
         *current_active == *captured_active);
}

bool viewport_transform_context_valid(
    const ShellState& state,
    const ViewportTransformGesture& gesture) {
    if (state.shell_mode != ShellMode::Animation || state.weight_paint.enabled ||
        state.preview_skeleton == nullptr || state.load_result.skeleton_data == nullptr ||
        state.session.runtime_data() == nullptr ||
        state.selected_animation_name != gesture.animation_name ||
        !std::isfinite(state.timeline_time_seconds) ||
        std::abs(state.timeline_time_seconds - gesture.time_seconds) > 1e-9 ||
        state.hierarchy_selection_anchor != gesture.hierarchy_anchor_before ||
        state.selected_timeline_track_id != gesture.timeline_focus_before ||
        !selection_context_matches(state.selection, gesture.selection_before)) {
        return false;
    }
    const ResolvedSelection resolved = resolve_shell_selection(state);
    if (!resolved.active_bone_index.has_value() ||
        *resolved.active_bone_index != gesture.bone_index ||
        gesture.bone_index >= state.preview_skeleton->bone_poses().size() ||
        gesture.bone_index >= state.preview_skeleton->bone_world_transforms().size() ||
        !state.preview_skeleton->is_bone_active(gesture.bone_index)) {
        return false;
    }
    const auto runtime_index =
        state.session.runtime_data()->find_bone_index(gesture.bone_name);
    return runtime_index.has_value() && *runtime_index == gesture.bone_index &&
        state.session.runtime_data()->find_animation(gesture.animation_name) != nullptr;
}

void restore_viewport_transform_context(
    ShellState* state,
    const ViewportTransformGesture& gesture) {
    state->selection = gesture.selection_before;
    state->hierarchy_selection_anchor = gesture.hierarchy_anchor_before;
    state->selected_timeline_track_id = gesture.timeline_focus_before;
}

void finish_viewport_transform_gesture(ShellState* state, bool commit) {
    if (state == nullptr || !state->viewport_transform_gesture.has_value()) {
        return;
    }
    if (commit && !viewport_transform_context_valid(
            *state, *state->viewport_transform_gesture)) {
        commit = false;
    }
    ViewportTransformGesture gesture =
        std::move(*state->viewport_transform_gesture);
    state->viewport_transform_gesture.reset();
    const bool rotate =
        std::holds_alternative<ViewportRotateGesturePayload>(gesture.payload);
    const bool scale =
        std::holds_alternative<ViewportScaleGesturePayload>(gesture.payload);
    const char* action_name = rotate ? "rotation" : scale ? "scale" : "move";
    if (!commit || !gesture.changed) {
        gesture.transaction.cancel();
        sync_shell_from_editor_session(state);
        restore_viewport_transform_context(state, gesture);
        if (!commit) {
            state->status_message =
                std::string("Cancelled bone ") + action_name;
        }
        return;
    }
    const marrow::editor::SessionResult result = gesture.transaction.commit();
    sync_shell_from_editor_session(state);
    restore_viewport_transform_context(state, gesture);
    if (!result) {
        state->error_message = result.error->format();
        state->status_message =
            std::string("Bone ") + action_name + " failed";
        return;
    }
    state->error_message.clear();
    state->status_message = "Keyed " + gesture.bone_name +
        (rotate ? " rotation at " : scale ? " scale at " : " translation at ") +
        format_time_seconds(gesture.time_seconds);
}

bool begin_viewport_translate_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    ViewportTranslateAxis axis,
    const ImVec2& pointer) {
    if (state == nullptr) {
        return false;
    }
    const ResolvedSelection resolved = resolve_shell_selection(*state);
    if (state->shell_mode != ShellMode::Animation ||
        state->selected_animation_name.empty() ||
        !resolved.active_bone_index.has_value() || state->preview_skeleton == nullptr ||
        state->load_result.project == nullptr || state->session.runtime_data() == nullptr ||
        state->weight_paint.enabled || authoring_gesture_active(*state)) {
        return false;
    }
    const std::size_t bone_index = *resolved.active_bone_index;
    if (bone_index >= state->preview_skeleton->bone_world_transforms().size() ||
        bone_index >= state->load_result.skeleton_data->bones().size()) {
        return false;
    }
    const ViewportWorldPoint pointer_world = world_from_screen(layout, pointer);
    const auto world = state->preview_skeleton->bone_world_transforms()[bone_index];
    if (!finite_world_point(pointer_world) || !std::isfinite(world.world_x) ||
        !std::isfinite(world.world_y) || !std::isfinite(state->timeline_time_seconds)) {
        return false;
    }
    state->timeline_playing = false;
    state->session.set_playing(false);
    const std::string bone_name = state->load_result.skeleton_data->bones()[bone_index].name;
    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::MoveBone,
        "Move bone " + bone_name,
        "viewport-translate:" + state->selected_animation_name + ":" + bone_name,
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        state->error_message = transaction.error()->format();
        return false;
    }
    ViewportTransformGesture gesture;
    gesture.bone_index = bone_index;
    gesture.bone_name = bone_name;
    gesture.animation_name = state->selected_animation_name;
    gesture.time_seconds = state->timeline_time_seconds;
    gesture.selection_before = state->selection;
    gesture.hierarchy_anchor_before = state->hierarchy_selection_anchor;
    gesture.timeline_focus_before = state->selected_timeline_track_id;
    gesture.transaction = std::move(transaction);
    gesture.payload = ViewportTranslateGesturePayload{
        axis,
        pointer_world,
        ViewportWorldPoint{world.world_x, world.world_y}};
    state->viewport_transform_gesture.emplace(std::move(gesture));
    return true;
}

bool update_viewport_translate_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer) {
    if (state == nullptr || !state->viewport_transform_gesture.has_value() ||
        state->preview_skeleton == nullptr) {
        return false;
    }
    auto& gesture = *state->viewport_transform_gesture;
    auto* translate = std::get_if<ViewportTranslateGesturePayload>(&gesture.payload);
    if (translate == nullptr || !viewport_transform_context_valid(*state, gesture)) {
        finish_viewport_transform_gesture(state, false);
        return false;
    }
    const ViewportWorldPoint pointer_world = world_from_screen(layout, pointer);
    if (!finite_world_point(pointer_world)) {
        finish_viewport_transform_gesture(state, false);
        state->error_message = "Cannot move a bone with a non-finite pointer.";
        return false;
    }
    const double delta_x = pointer_world.x - translate->pointer_start.x;
    const double delta_y = pointer_world.y - translate->pointer_start.y;
    if (!gesture.changed && std::abs(delta_x) <= 1e-6 && std::abs(delta_y) <= 1e-6) {
        return true;
    }
    ViewportWorldPoint target = translate->bone_world_start;
    if (translate->axis != ViewportTranslateAxis::Y) {
        target.x += delta_x;
    }
    if (translate->axis != ViewportTranslateAxis::X) {
        target.y += delta_y;
    }
    const auto local = local_position_for_world_target_impl(
        *state->preview_skeleton, gesture.bone_index, target);
    if (!local.has_value()) {
        const std::string error =
            "Cannot move a bone through a singular parent transform.";
        finish_viewport_transform_gesture(state, false);
        state->error_message = error;
        return false;
    }
    marrow::editor::upsert_transform_keyframe(
        *gesture.transaction.project(),
        *state->session.runtime_data(),
        gesture.animation_name,
        gesture.bone_name,
        marrow::editor::TransformTimelineChannel::Translate,
        gesture.time_seconds,
        marrow::editor::TransformKeyframePatch{
            std::nullopt,
            local->x,
            local->y});
    const marrow::editor::SessionResult refresh = gesture.transaction.refresh_runtime();
    if (!refresh) {
        const std::string error = refresh.error->format();
        finish_viewport_transform_gesture(state, false);
        state->error_message = error;
        return false;
    }
    gesture.changed = true;
    sync_shell_from_editor_session(state);
    return true;
}

bool begin_viewport_rotate_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer) {
    if (state == nullptr || authoring_gesture_active(*state)) {
        return false;
    }
    const auto bone_index = rotation_gizmo_bone_index(*state, layout);
    if (!bone_index.has_value() || state->session.runtime_data() == nullptr) {
        return false;
    }
    const auto basis = rotation_basis_impl(*state->preview_skeleton, *bone_index);
    const ViewportWorldPoint pointer_world = world_from_screen(layout, pointer);
    const auto wrapped_angle = basis.has_value()
        ? rotation_angle_impl(*basis, pointer_world)
        : std::nullopt;
    const auto* animation = state->session.runtime_data()->find_animation(
        state->selected_animation_name);
    if (!basis.has_value() || !wrapped_angle.has_value() || animation == nullptr ||
        !std::isfinite(state->timeline_time_seconds) ||
        *bone_index >= state->session.runtime_data()->bones().size()) {
        state->error_message =
            "Cannot rotate a bone through an invalid parent-space basis.";
        return false;
    }
    const double start_rotation = animation->sample_bone_rotation(
            *bone_index, state->timeline_time_seconds)
        .value_or(state->session.runtime_data()->bones()[*bone_index].setup_pose.rotation);
    if (!std::isfinite(start_rotation)) {
        state->error_message = "Cannot rotate a bone with a non-finite starting angle.";
        return false;
    }
    const std::string bone_name =
        state->session.runtime_data()->bones()[*bone_index].name;
    state->timeline_playing = false;
    state->session.set_playing(false);
    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::MoveBone,
        "Rotate bone " + bone_name,
        "viewport-rotate:" + state->selected_animation_name + ":" + bone_name,
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        state->error_message = transaction.error()->format();
        return false;
    }

    ViewportTransformGesture gesture;
    gesture.bone_index = *bone_index;
    gesture.bone_name = bone_name;
    gesture.animation_name = state->selected_animation_name;
    gesture.time_seconds = state->timeline_time_seconds;
    gesture.selection_before = state->selection;
    gesture.hierarchy_anchor_before = state->hierarchy_selection_anchor;
    gesture.timeline_focus_before = state->selected_timeline_track_id;
    gesture.transaction = std::move(transaction);
    ViewportRotateGesturePayload rotate;
    rotate.basis = *basis;
    rotate.start_absolute_rotation = start_rotation;
    rotate.previous_wrapped_angle = *wrapped_angle;
    rotate.pointer_screen = pointer;
    rotate.current_absolute_rotation = start_rotation;
    gesture.payload = rotate;
    state->viewport_transform_gesture.emplace(std::move(gesture));
    state->error_message.clear();
    return true;
}

bool update_viewport_rotate_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer) {
    if (state == nullptr || !state->viewport_transform_gesture.has_value()) {
        return false;
    }
    auto& gesture = *state->viewport_transform_gesture;
    auto* rotate = std::get_if<ViewportRotateGesturePayload>(&gesture.payload);
    if (rotate == nullptr || !viewport_transform_context_valid(*state, gesture) ||
        !std::isfinite(layout.pixels_per_unit) || layout.pixels_per_unit <= 0.0f ||
        !std::isfinite(pointer.x) || !std::isfinite(pointer.y)) {
        finish_viewport_transform_gesture(state, false);
        state->error_message = "Bone rotation was cancelled after viewport context loss.";
        return false;
    }

    rotate->pointer_screen = pointer;
    const ImVec2 pivot_screen = screen_from_world(
        layout, rotate->basis.pivot_world.x, rotate->basis.pivot_world.y);
    if (!std::isfinite(pivot_screen.x) || !std::isfinite(pivot_screen.y)) {
        finish_viewport_transform_gesture(state, false);
        state->error_message = "Bone rotation was cancelled after invalid viewport math.";
        return false;
    }
    if (squared_distance(pivot_screen, pointer) <=
        kRotationPivotSuspendRadius * kRotationPivotSuspendRadius) {
        rotate->previous_wrapped_angle.reset();
        rotate->angular_reference_suspended = true;
        return true;
    }

    const ViewportWorldPoint pointer_world = world_from_screen(layout, pointer);
    const auto wrapped_angle = rotation_angle_impl(rotate->basis, pointer_world);
    if (!wrapped_angle.has_value()) {
        finish_viewport_transform_gesture(state, false);
        state->error_message = "Bone rotation was cancelled after invalid parent-space math.";
        return false;
    }
    if (rotate->angular_reference_suspended ||
        !rotate->previous_wrapped_angle.has_value()) {
        rotate->previous_wrapped_angle = *wrapped_angle;
        rotate->angular_reference_suspended = false;
        return true;
    }

    const double step = unwrap_rotation_delta_impl(
        *wrapped_angle - *rotate->previous_wrapped_angle);
    rotate->previous_wrapped_angle = *wrapped_angle;
    if (!std::isfinite(step)) {
        finish_viewport_transform_gesture(state, false);
        state->error_message = "Bone rotation was cancelled after a non-finite angle.";
        return false;
    }
    if (std::abs(step) <= 1e-9) {
        return true;
    }
    const double accumulated = rotate->accumulated_rotation + step;
    const double candidate = rotate->start_absolute_rotation + accumulated;
    if (!std::isfinite(accumulated) || !std::isfinite(candidate)) {
        finish_viewport_transform_gesture(state, false);
        state->error_message = "Bone rotation was cancelled after a non-finite result.";
        return false;
    }

    marrow::editor::upsert_transform_keyframe(
        *gesture.transaction.project(),
        *state->session.runtime_data(),
        gesture.animation_name,
        gesture.bone_name,
        marrow::editor::TransformTimelineChannel::Rotate,
        gesture.time_seconds,
        marrow::editor::TransformKeyframePatch{candidate, std::nullopt, std::nullopt});
    const marrow::editor::SessionResult refresh = gesture.transaction.refresh_runtime();
    if (!refresh) {
        const std::string error = refresh.error->format();
        finish_viewport_transform_gesture(state, false);
        state->error_message = error;
        return false;
    }
    rotate->accumulated_rotation = accumulated;
    rotate->current_absolute_rotation = candidate;
    gesture.changed = true;
    sync_shell_from_editor_session(state);
    return true;
}

bool begin_viewport_scale_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    ViewportScaleHandle handle,
    const ImVec2& pointer) {
    if (state == nullptr || authoring_gesture_active(*state)) {
        return false;
    }
    const auto bone_index = scale_gizmo_bone_index(*state, layout);
    if (!bone_index.has_value() || state->session.runtime_data() == nullptr ||
        state->load_result.project == nullptr ||
        !std::isfinite(pointer.x) || !std::isfinite(pointer.y)) {
        return false;
    }
    const auto basis =
        scale_basis_impl(*state->preview_skeleton, *bone_index);
    const auto start_scale =
        effective_scale_at_playhead(*state, *bone_index);
    if (!basis.has_value() || !start_scale.has_value() ||
        (handle == ViewportScaleHandle::Uniform &&
         start_scale->scale_x == 0.0 && start_scale->scale_y == 0.0)) {
        state->error_message =
            "Cannot scale a bone through an invalid local-axis basis.";
        return false;
    }
    const ImVec2 pivot = screen_from_world(
        layout, basis->pivot_world.x, basis->pivot_world.y);
    const ImVec2& direction = scale_handle_direction(*basis, handle);
    const double start_projection =
        (static_cast<double>(pointer.x) - static_cast<double>(pivot.x)) *
            static_cast<double>(direction.x) +
        (static_cast<double>(pointer.y) - static_cast<double>(pivot.y)) *
            static_cast<double>(direction.y);
    if (!std::isfinite(pivot.x) || !std::isfinite(pivot.y) ||
        !std::isfinite(start_projection) ||
        std::abs(start_projection) <= 1e-8 ||
        !std::isfinite(state->timeline_time_seconds)) {
        state->error_message =
            "Cannot scale a bone through an unsolvable pointer mapping.";
        return false;
    }

    const std::string bone_name =
        state->session.runtime_data()->bones()[*bone_index].name;
    state->timeline_playing = false;
    state->session.set_playing(false);
    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::MoveBone,
        "Scale bone " + bone_name,
        "viewport-scale:" + state->selected_animation_name + ":" + bone_name,
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        state->error_message = transaction.error()->format();
        return false;
    }

    ViewportTransformGesture gesture;
    gesture.bone_index = *bone_index;
    gesture.bone_name = bone_name;
    gesture.animation_name = state->selected_animation_name;
    gesture.time_seconds = state->timeline_time_seconds;
    gesture.selection_before = state->selection;
    gesture.hierarchy_anchor_before = state->hierarchy_selection_anchor;
    gesture.timeline_focus_before = state->selected_timeline_track_id;
    gesture.transaction = std::move(transaction);
    ViewportScaleGesturePayload scale;
    scale.basis = *basis;
    scale.handle = handle;
    scale.start_absolute_scale_x = start_scale->scale_x;
    scale.start_absolute_scale_y = start_scale->scale_y;
    scale.start_projection_pixels = start_projection;
    scale.pointer_screen = pointer;
    scale.current_absolute_scale_x = start_scale->scale_x;
    scale.current_absolute_scale_y = start_scale->scale_y;
    gesture.payload = scale;
    state->viewport_transform_gesture.emplace(std::move(gesture));
    state->error_message.clear();
    return true;
}

bool update_viewport_scale_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer) {
    if (state == nullptr || !state->viewport_transform_gesture.has_value()) {
        return false;
    }
    auto& gesture = *state->viewport_transform_gesture;
    auto* scale =
        std::get_if<ViewportScaleGesturePayload>(&gesture.payload);
    if (scale == nullptr ||
        !viewport_transform_context_valid(*state, gesture)) {
        finish_viewport_transform_gesture(state, false);
        state->error_message =
            "Bone scale was cancelled after viewport context loss.";
        return false;
    }
    scale->pointer_screen = pointer;
    const auto candidate =
        scale_candidate_impl(*scale, layout, pointer);
    if (!candidate.has_value()) {
        finish_viewport_transform_gesture(state, false);
        state->error_message =
            "Bone scale was cancelled after invalid local-axis math.";
        return false;
    }
    constexpr double kScaleChangeEpsilon = 1e-12;
    if (std::abs(
            candidate->scale_x - scale->current_absolute_scale_x) <=
            kScaleChangeEpsilon &&
        std::abs(
            candidate->scale_y - scale->current_absolute_scale_y) <=
            kScaleChangeEpsilon) {
        return true;
    }

    marrow::editor::upsert_transform_keyframe(
        *gesture.transaction.project(),
        *state->session.runtime_data(),
        gesture.animation_name,
        gesture.bone_name,
        marrow::editor::TransformTimelineChannel::Scale,
        gesture.time_seconds,
        marrow::editor::TransformKeyframePatch{
            std::nullopt,
            candidate->scale_x,
            candidate->scale_y});
    const marrow::editor::SessionResult refresh =
        gesture.transaction.refresh_runtime();
    if (!refresh) {
        const std::string error = refresh.error->format();
        finish_viewport_transform_gesture(state, false);
        state->error_message = error;
        return false;
    }
    scale->current_absolute_scale_x = candidate->scale_x;
    scale->current_absolute_scale_y = candidate->scale_y;
    gesture.changed = true;
    sync_shell_from_editor_session(state);
    return true;
}

} // namespace

bool begin_viewport_translate_gesture_for_smoke(
    ShellState* state,
    const ViewportLayout& layout,
    ViewportTranslateAxis axis,
    const ImVec2& pointer) {
    return begin_viewport_translate_gesture(state, layout, axis, pointer);
}

bool update_viewport_translate_gesture_for_smoke(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer) {
    return update_viewport_translate_gesture(state, layout, pointer);
}

void finish_viewport_translate_gesture_for_smoke(ShellState* state, bool commit) {
    finish_viewport_transform_gesture(state, commit);
}

std::optional<ViewportTranslateAxis> hit_test_translate_gizmo_for_smoke(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& position) {
    return hit_test_translate_gizmo(state, layout, position);
}

std::optional<ViewportRotationBasis> viewport_rotation_basis_for_smoke(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t bone_index) {
    return rotation_basis_impl(skeleton, bone_index);
}

std::optional<double> viewport_rotation_angle_for_smoke(
    const ViewportRotationBasis& basis,
    const ViewportWorldPoint& pointer_world) {
    return rotation_angle_impl(basis, pointer_world);
}

double unwrap_viewport_rotation_delta_for_smoke(double delta) {
    return unwrap_rotation_delta_impl(delta);
}

bool viewport_rotation_gizmo_visible_for_smoke(
    const ShellState& state,
    const ViewportLayout& layout) {
    return rotation_gizmo_bone_index(state, layout).has_value();
}

bool hit_test_rotation_gizmo_for_smoke(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& position) {
    return hit_test_rotation_gizmo(state, layout, position);
}

bool begin_viewport_rotate_gesture_for_smoke(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer) {
    return begin_viewport_rotate_gesture(state, layout, pointer);
}

bool update_viewport_rotate_gesture_for_smoke(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer) {
    return update_viewport_rotate_gesture(state, layout, pointer);
}

std::optional<ViewportScaleBasis> viewport_scale_basis_for_smoke(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t bone_index) {
    return scale_basis_impl(skeleton, bone_index);
}

std::optional<ViewportScaleCandidate> viewport_scale_candidate_for_smoke(
    const ViewportScaleGesturePayload& payload,
    const ViewportLayout& layout,
    const ImVec2& pointer) {
    return scale_candidate_impl(payload, layout, pointer);
}

bool viewport_scale_gizmo_visible_for_smoke(
    const ShellState& state,
    const ViewportLayout& layout) {
    return scale_gizmo_bone_index(state, layout).has_value();
}

bool viewport_uniform_scale_handle_visible_for_smoke(
    const ShellState& state,
    const ViewportLayout& layout) {
    const auto bone_index = scale_gizmo_bone_index(state, layout);
    return bone_index.has_value() &&
        uniform_scale_handle_enabled(state, *bone_index);
}

const char* viewport_transform_hint_for_smoke(
    const ShellState& state,
    const ViewportLayout& layout) {
    return active_transform_gizmo_hint(state, &layout);
}

std::optional<ViewportScaleHandle> hit_test_scale_gizmo_for_smoke(
    const ShellState& state,
    const ViewportLayout& layout,
    const ImVec2& position) {
    return hit_test_scale_gizmo(state, layout, position);
}

bool begin_viewport_scale_gesture_for_smoke(
    ShellState* state,
    const ViewportLayout& layout,
    ViewportScaleHandle handle,
    const ImVec2& pointer) {
    return begin_viewport_scale_gesture(state, layout, handle, pointer);
}

bool update_viewport_scale_gesture_for_smoke(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer) {
    return update_viewport_scale_gesture(state, layout, pointer);
}

ViewportPressTarget viewport_press_target_for_smoke(
    bool active_gesture,
    bool weight_brush,
    bool translate,
    bool rotation,
    bool scale,
    bool entity) {
    return viewport_press_target(
        active_gesture,
        weight_brush,
        translate,
        rotation,
        scale,
        entity);
}

void finish_viewport_transform_gesture_for_smoke(
    ShellState* state,
    bool commit) {
    finish_viewport_transform_gesture(state, commit);
}

bool apply_viewport_point_selection_for_smoke(
    ShellState* state,
    const marrow::editor::SelectionItem& item,
    bool command_modifier) {
    return apply_viewport_point_selection_gesture(
        state, item, command_modifier, false);
}

bool begin_viewport_box_selection_for_smoke(
    ShellState* state,
    const ImVec2& pointer,
    bool additive) {
    return begin_viewport_box_selection(state, pointer, additive);
}

bool update_viewport_box_selection_for_smoke(
    ShellState* state,
    const ImVec2& pointer) {
    return update_viewport_box_selection(state, pointer);
}

bool finish_viewport_box_selection_for_smoke(
    ShellState* state,
    const ViewportLayout& layout,
    bool commit) {
    return finish_viewport_box_selection(state, layout, commit);
}

std::optional<marrow::runtime::AttachmentVertex> bone_local_position_from_world(
    const marrow::runtime::Skeleton& skeleton,
    std::size_t bone_index,
    const ViewportWorldPoint& target) {
    return local_position_for_world_target_impl(skeleton, bone_index, target);
}


template <typename MutateFn>
bool apply_coalesced_viewport_drag(
    ShellState* state,
    bool changed,
    std::string label,
    std::string group,
    bool allow_merge,
    MutateFn mutate) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    const ImGuiID item_id = ImGui::GetItemID();
    if (ImGui::IsItemActivated()) {
        state->pending_edit_action = PendingEditAction{
            item_id,
            EditActionKind::EditProperty,
            std::move(label),
            std::move(group),
            allow_merge,
            capture_history_snapshot(*state)};
    }

    if (changed) {
        mutate();
        update_project_dirty_state(state);
    }

    if (ImGui::IsItemDeactivatedAfterEdit() &&
        state->pending_edit_action.has_value() &&
        state->pending_edit_action->item_id == item_id) {
        PendingEditAction pending = std::move(*state->pending_edit_action);
        state->pending_edit_action.reset();
        return record_action_from_snapshots(
            state,
            pending.before_snapshot,
            pending.kind,
            std::move(pending.label),
            std::move(pending.group),
            pending.allow_merge);
    }

    if (ImGui::IsItemDeactivated() &&
        state->pending_edit_action.has_value() &&
        state->pending_edit_action->item_id == item_id) {
        state->pending_edit_action.reset();
    }

    return true;
}

template <typename MutateFn>
bool apply_coalesced_onion_skin_drag(
    ShellState* state,
    bool changed,
    std::string label,
    std::string group,
    bool allow_merge,
    MutateFn mutate) {
    return apply_coalesced_viewport_drag(
        state,
        changed,
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

const char* onion_skin_mode_name(marrow::editor::OnionSkinMode mode) {
    switch (mode) {
    case marrow::editor::OnionSkinMode::Frame:
        return "Frame";
    case marrow::editor::OnionSkinMode::Keyframe:
        return "Keyframe";
    }

    return "Frame";
}


void draw_viewport_pose_fallback(
    const std::vector<BoneCanvasNode>& bones,
    float joint_radius,
    const std::vector<bool>* selected_bones,
    std::optional<std::size_t> active_bone,
    std::optional<std::size_t> hovered_bone,
    ImU32 active_line_color,
    ImU32 inactive_line_color,
    ImU32 selected_line_color,
    ImU32 active_fill_color,
    ImU32 inactive_fill_color,
    ImU32 hovered_fill_color,
    ImU32 selected_fill_color,
    ImU32 active_outline_color,
    ImU32 inactive_outline_color,
    ImDrawList* draw_list) {
    if (draw_list == nullptr) {
        return;
    }

    for (const BoneCanvasNode& node : bones) {
        if (!node.parent_index.has_value() || *node.parent_index >= bones.size()) {
            continue;
        }

        const BoneCanvasNode& parent = bones[*node.parent_index];
        const bool selected = selected_bones != nullptr &&
            node.bone_index < selected_bones->size() &&
            (*selected_bones)[node.bone_index];
        const ImU32 line_color = selected
            ? selected_line_color
            : node.active ? active_line_color : inactive_line_color;
        draw_list->AddLine(
            parent.screen_position,
            node.screen_position,
            line_color,
            selected ? 3.0f : 2.0f);
    }

    for (const BoneCanvasNode& node : bones) {
        const bool selected = selected_bones != nullptr &&
            node.bone_index < selected_bones->size() &&
            (*selected_bones)[node.bone_index];
        const bool active = active_bone == node.bone_index;
        const bool hovered_selection =
            hovered_bone.has_value() && *hovered_bone == node.bone_index;
        const float radius = joint_radius + (active ? 2.5f : selected ? 1.25f : 0.0f);
        const ImU32 fill_color = selected
            ? selected_fill_color
            : hovered_selection ? hovered_fill_color
                                : node.active ? active_fill_color : inactive_fill_color;
        const ImU32 outline_color =
            node.active ? active_outline_color : inactive_outline_color;
        draw_list->AddCircleFilled(node.screen_position, radius, fill_color, 18);
        draw_list->AddCircle(node.screen_position, radius, outline_color, 18, 1.5f);
    }
}

ImU32 average_overlay_triangle_color(
    const MeshWeightOverlayVertex& a,
    const MeshWeightOverlayVertex& b,
    const MeshWeightOverlayVertex& c) {
    const ImVec4 color_a = mesh_weight_heatmap_color(a.weight);
    const ImVec4 color_b = mesh_weight_heatmap_color(b.weight);
    const ImVec4 color_c = mesh_weight_heatmap_color(c.weight);
    const ImVec4 average(
        (color_a.x + color_b.x + color_c.x) / 3.0f,
        (color_a.y + color_b.y + color_c.y) / 3.0f,
        (color_a.z + color_b.z + color_c.z) / 3.0f,
        (color_a.w + color_b.w + color_c.w) / 3.0f);
    return ImGui::ColorConvertFloat4ToU32(average);
}

void draw_mesh_weight_overlay_fallback(
    const ViewportLayout& layout,
    const MeshWeightOverlay& overlay,
    ImDrawList* draw_list) {
    if (draw_list == nullptr) {
        return;
    }

    for (std::size_t triangle_index = 0; triangle_index + 2U < overlay.triangles.size();
         triangle_index += 3U) {
        const std::size_t a = overlay.triangles[triangle_index];
        const std::size_t b = overlay.triangles[triangle_index + 1U];
        const std::size_t c = overlay.triangles[triangle_index + 2U];
        if (a >= overlay.vertices.size() ||
            b >= overlay.vertices.size() ||
            c >= overlay.vertices.size()) {
            continue;
        }

        draw_list->AddTriangleFilled(
            overlay.vertices[a].screen_position,
            overlay.vertices[b].screen_position,
            overlay.vertices[c].screen_position,
            average_overlay_triangle_color(
                overlay.vertices[a],
                overlay.vertices[b],
                overlay.vertices[c]));
    }

    for (const MeshWeightOverlayVertex& vertex : overlay.vertices) {
        const ImU32 fill_color =
            ImGui::ColorConvertFloat4ToU32(mesh_weight_heatmap_color(vertex.weight, 0.84f));
        draw_list->AddCircleFilled(
            vertex.screen_position,
            std::clamp(layout.render_joint_radius * 0.75f, 4.0f, 8.0f),
            fill_color,
            14);
        draw_list->AddCircle(
            vertex.screen_position,
            std::clamp(layout.render_joint_radius * 0.75f, 4.0f, 8.0f),
            IM_COL32(18, 21, 25, 180),
            14,
            1.0f);
    }
}

void draw_debug_overlay_fallback(
    const DebugOverlayGeometry& overlay,
    ImDrawList* draw_list) {
    if (draw_list == nullptr) {
        return;
    }

    for (const auto& line : overlay.lines) {
        draw_list->AddLine(line.start, line.end, line.color, line.thickness);
    }
    for (const auto& circle : overlay.circles) {
        if ((circle.fill_color & IM_COL32_A_MASK) != 0U) {
            draw_list->AddCircleFilled(circle.center, circle.radius, circle.fill_color, 18);
        }
        if ((circle.outline_color & IM_COL32_A_MASK) != 0U) {
            draw_list->AddCircle(
                circle.center,
                circle.radius,
                circle.outline_color,
                24,
                circle.outline_thickness);
        }
    }
}

void draw_viewport_fallback_scene(
    const ShellState& state,
    const ViewportLayout& layout,
    const std::vector<OnionSkinGhostPose>& ghost_poses,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
    ImDrawList* draw_list) {
    draw_list->AddRectFilled(
        layout.canvas_origin,
        layout.canvas_end,
        IM_COL32(18, 21, 25, 255),
        6.0f);
    draw_list->AddRect(
        layout.canvas_origin,
        layout.canvas_end,
        IM_COL32(56, 61, 69, 255),
        6.0f);

    const float grid_spacing = std::max(18.0f, 40.0f * static_cast<float>(state.viewport.zoom));
    for (float x = first_grid_line(layout.world_origin_screen.x, layout.canvas_origin.x, grid_spacing);
         x < layout.canvas_end.x;
         x += grid_spacing) {
        draw_list->AddLine(
            ImVec2(x, layout.canvas_origin.y),
            ImVec2(x, layout.canvas_end.y),
            IM_COL32(31, 35, 41, 255));
    }
    for (float y = first_grid_line(layout.world_origin_screen.y, layout.canvas_origin.y, grid_spacing);
         y < layout.canvas_end.y;
         y += grid_spacing) {
        draw_list->AddLine(
            ImVec2(layout.canvas_origin.x, y),
            ImVec2(layout.canvas_end.x, y),
            IM_COL32(31, 35, 41, 255));
    }

    draw_list->AddLine(
        ImVec2(layout.canvas_origin.x, layout.world_origin_screen.y),
        ImVec2(layout.canvas_end.x, layout.world_origin_screen.y),
        IM_COL32(189, 86, 37, 255),
        1.5f);
    draw_list->AddLine(
        ImVec2(layout.world_origin_screen.x, layout.canvas_origin.y),
        ImVec2(layout.world_origin_screen.x, layout.canvas_end.y),
        IM_COL32(204, 177, 110, 255),
        1.5f);

    for (const OnionSkinGhostPose& ghost_pose : ghost_poses) {
        draw_viewport_pose_fallback(
            ghost_pose.bones,
            layout.render_joint_radius * 0.9f,
            nullptr,
            std::nullopt,
            std::nullopt,
            ghost_pose.line_color,
            ghost_pose.line_color,
            ghost_pose.line_color,
            ghost_pose.fill_color,
            ghost_pose.fill_color,
            ghost_pose.fill_color,
            ghost_pose.fill_color,
            ghost_pose.outline_color,
            ghost_pose.outline_color,
            draw_list);
    }

    if (mesh_weight_overlay != nullptr) {
        draw_mesh_weight_overlay_fallback(layout, *mesh_weight_overlay, draw_list);
    }

    const DebugOverlayGeometry debug_overlay = build_debug_overlay_geometry(state, layout);
    draw_debug_overlay_fallback(debug_overlay, draw_list);

    if (state.viewport.debug_overlay.bones) {
        const ResolvedSelection resolved = resolve_shell_selection(state);
        std::vector<bool> selected_bones(
            state.load_result.skeleton_data->bones().size(), false);
        for (const marrow::editor::SelectionItem& item : state.selection.items()) {
            const auto* bone = std::get_if<marrow::editor::BoneSelection>(&item);
            if (bone == nullptr) {
                continue;
            }
            if (const auto index =
                    state.load_result.skeleton_data->find_bone_index(bone->bone_name)) {
                selected_bones[*index] = true;
            }
        }
        draw_viewport_pose_fallback(
            layout.bones,
            layout.render_joint_radius,
            &selected_bones,
            resolved.active_bone_index,
            hovered_bone,
            IM_COL32(214, 163, 76, 220),
            IM_COL32(111, 117, 125, 180),
            IM_COL32(247, 204, 114, 255),
            IM_COL32(208, 134, 57, 230),
            IM_COL32(98, 103, 110, 200),
            IM_COL32(226, 186, 97, 240),
            IM_COL32(247, 204, 114, 255),
            IM_COL32(33, 37, 41, 255),
            IM_COL32(48, 50, 54, 255),
            draw_list);
    }
}

void draw_viewport_annotations(
    const ShellState& state,
    const ViewportLayout& layout,
    std::optional<std::size_t> hovered_bone,
    const MeshWeightOverlay* mesh_weight_overlay,
    ImDrawList* draw_list) {
    if (!state.load_result) {
        return;
    }

    const auto& bones = state.load_result.skeleton_data->bones();
    const ResolvedSelection resolved = resolve_shell_selection(state);
    if (state.viewport.debug_overlay.bones) {
        for (const BoneCanvasNode& node : layout.bones) {
            const bool selected = resolved.active_bone_index == node.bone_index;
            const bool hovered_selection =
                hovered_bone.has_value() && *hovered_bone == node.bone_index;
            const float radius = layout.render_joint_radius + (selected ? 2.0f : 0.0f);
            if (selected || hovered_selection || layout.bones.size() <= 12U) {
                draw_list->AddText(
                    ImVec2(node.screen_position.x + radius + 6.0f, node.screen_position.y - 6.0f),
                    selected ? IM_COL32(247, 232, 191, 255) : IM_COL32(225, 212, 180, 220),
                    bones[node.bone_index].name.c_str());
            }
        }
    }

    // ── Glass selection chip (top-left) ──
    if (resolved.active_bone_index.has_value() &&
        *resolved.active_bone_index < bones.size()) {
        std::ostringstream selection_stream;
        selection_stream << bones[*resolved.active_bone_index].name
                         << "  ·  pan(" << static_cast<int>(state.viewport.pan_x) << ", "
                         << static_cast<int>(state.viewport.pan_y) << ")"
                         << "  ·  zoom " << state.viewport.zoom;
        const std::string chip_text = selection_stream.str();
        const ImVec2 text_size = ImGui::CalcTextSize(chip_text.c_str());
        const float pad_x = 10.0f;
        const float pad_y = 6.0f;
        const ImVec2 chip_min(
            layout.canvas_origin.x + 10.0f,
            layout.canvas_origin.y + 10.0f);
        const ImVec2 chip_max(
            chip_min.x + text_size.x + pad_x * 2.0f,
            chip_min.y + text_size.y + pad_y * 2.0f);
        // Glass background: surface-highest @ 85% opacity
        draw_list->AddRectFilled(
            chip_min,
            chip_max,
            IM_COL32(0x32, 0x35, 0x3b, 0xD9),
            2.0f);
        // Ghost border: outline-variant @ 20% opacity
        draw_list->AddRect(
            chip_min,
            chip_max,
            IM_COL32(0x43, 0x46, 0x54, 0x33),
            2.0f,
            0,
            1.0f);
        draw_list->AddText(
            ImVec2(chip_min.x + pad_x, chip_min.y + pad_y),
            IM_COL32(0xe1, 0xe2, 0xea, 0xFF),
            chip_text.c_str());
    }

    const DebugOverlayGeometry debug_overlay = build_debug_overlay_geometry(state, layout);
    std::vector<const char*> enabled_layers;
    enabled_layers.reserve(6U);
    if (state.viewport.debug_overlay.bones) {
        enabled_layers.push_back("bones");
    }
    if (debug_overlay.stats.ik_constraint_count > 0U) {
        enabled_layers.push_back("ik");
    }
    if (debug_overlay.stats.path_constraint_count > 0U) {
        enabled_layers.push_back("path");
    }
    if (debug_overlay.stats.physics_constraint_count > 0U) {
        enabled_layers.push_back("physics");
    }
    if (debug_overlay.stats.mesh_attachment_count > 0U) {
        enabled_layers.push_back("meshes");
    }
    if (debug_overlay.stats.bounding_box_count > 0U) {
        enabled_layers.push_back("bounds");
    }
    if (!enabled_layers.empty()) {
        std::ostringstream debug_stream;
        debug_stream << "Debug overlay:";
        for (const char* layer : enabled_layers) {
            debug_stream << ' ' << layer;
        }
        draw_list->AddText(
            ImVec2(layout.canvas_origin.x + 14.0f, layout.canvas_origin.y + 30.0f),
            IM_COL32(210, 221, 232, 220),
            debug_stream.str().c_str());
    }

    // ── Glass perf HUD (top-right) ──
    if (state.hud_overlay_enabled && state.hud_overlay_frame.has_value()) {
        const std::vector<std::string> hud_lines =
            marrow::runtime::profiler_hud_lines(*state.hud_overlay_frame);
        const float line_height = ImGui::GetTextLineHeight();
        const float line_spacing = line_height + 2.0f;
        const float pad_x = 10.0f;
        const float pad_y = 6.0f;
        const char* title = "PERF HUD";
        const ImVec2 title_size = ImGui::CalcTextSize(title);
        float max_line_width = title_size.x;
        for (const std::string& line : hud_lines) {
            const ImVec2 s = ImGui::CalcTextSize(line.c_str());
            max_line_width = std::max(max_line_width, s.x);
        }
        const float title_block = title_size.y + 4.0f + 1.0f;  // title + gap + divider
        const float body_block = static_cast<float>(hud_lines.size()) * line_spacing;
        const float chip_w = max_line_width + pad_x * 2.0f;
        const float chip_h = title_block + 4.0f + body_block + pad_y * 2.0f;
        const ImVec2 chip_max(layout.canvas_end.x - 10.0f, layout.canvas_origin.y + 10.0f + chip_h);
        const ImVec2 chip_min(chip_max.x - chip_w, layout.canvas_origin.y + 10.0f);
        draw_list->AddRectFilled(
            chip_min,
            chip_max,
            IM_COL32(0x32, 0x35, 0x3b, 0xD9),
            2.0f);
        draw_list->AddRect(
            chip_min,
            chip_max,
            IM_COL32(0x43, 0x46, 0x54, 0x33),
            2.0f,
            0,
            1.0f);
        // Title
        draw_list->AddText(
            ImVec2(chip_min.x + pad_x, chip_min.y + pad_y),
            IM_COL32(0x8d, 0x90, 0xa0, 0xFF),
            title);
        // Divider line
        const float divider_y = chip_min.y + pad_y + title_size.y + 3.0f;
        draw_list->AddLine(
            ImVec2(chip_min.x + pad_x, divider_y),
            ImVec2(chip_max.x - pad_x, divider_y),
            IM_COL32(0x43, 0x46, 0x54, 0x66),
            1.0f);
        // Body lines
        float line_y = divider_y + 4.0f;
        for (const std::string& line : hud_lines) {
            draw_list->AddText(
                ImVec2(chip_min.x + pad_x, line_y),
                IM_COL32(0xc3, 0xc6, 0xd6, 0xFF),
                line.c_str());
            line_y += line_spacing;
        }
    }

    // ── Glass onion skin legend (bottom-left) ──
    if (state.viewport.onion_skin.enabled) {
        const char* legend_text = "Onion skin  ·  prev  ·  next";
        const ImVec2 text_size = ImGui::CalcTextSize(legend_text);
        const float pad_x = 10.0f;
        const float pad_y = 6.0f;
        const ImVec2 chip_min(
            layout.canvas_origin.x + 10.0f,
            layout.canvas_end.y - 10.0f - (text_size.y + pad_y * 2.0f));
        const ImVec2 chip_max(
            chip_min.x + text_size.x + pad_x * 2.0f,
            layout.canvas_end.y - 10.0f);
        draw_list->AddRectFilled(
            chip_min,
            chip_max,
            IM_COL32(0x32, 0x35, 0x3b, 0xD9),
            2.0f);
        draw_list->AddRect(
            chip_min,
            chip_max,
            IM_COL32(0x43, 0x46, 0x54, 0x33),
            2.0f,
            0,
            1.0f);
        draw_list->AddText(
            ImVec2(chip_min.x + pad_x, chip_min.y + pad_y),
            IM_COL32(0xc3, 0xc6, 0xd6, 0xFF),
            legend_text);
        // Colored dots for prev (primary) and next (tertiary)
        const float dot_y = chip_min.y + pad_y + text_size.y * 0.5f;
        const ImVec2 prev_size = ImGui::CalcTextSize("prev  ·  next");
        const float prev_dot_x = chip_max.x - pad_x - prev_size.x - 8.0f;
        draw_list->AddCircleFilled(
            ImVec2(prev_dot_x, dot_y),
            3.0f,
            IM_COL32(0xb3, 0xc5, 0xff, 0x80));  // primary @ 50%
        const ImVec2 next_size = ImGui::CalcTextSize("next");
        const float next_dot_x = chip_max.x - pad_x - next_size.x - 8.0f;
        draw_list->AddCircleFilled(
            ImVec2(next_dot_x, dot_y),
            3.0f,
            IM_COL32(0xff, 0xb3, 0xad, 0x80));  // tertiary @ 50%
    }

    if (mesh_weight_overlay != nullptr) {
        std::ostringstream overlay_stream;
        overlay_stream << "Weight heatmap: "
                       << mesh_weight_overlay->target.display_attachment_name
                       << " -> " << mesh_weight_overlay->target.source_skin_name
                       << "/" << mesh_weight_overlay->target.source_attachment_name
                       << "  " << weight_paint_mode_name(state.weight_paint.mode)
                       << "  radius " << static_cast<int>(state.weight_paint.radius_pixels)
                       << "  strength " << std::fixed << std::setprecision(2)
                       << state.weight_paint.strength;
        draw_list->AddText(
            ImVec2(layout.canvas_origin.x + 14.0f, layout.canvas_end.y - 48.0f),
            IM_COL32(233, 223, 199, 230),
            overlay_stream.str().c_str());
    }
}

namespace {

void draw_viewport_selection_feedback(
    const ShellState& state,
    const ViewportLayout& layout,
    const ViewportEntityHitGeometry& geometry,
    const std::optional<ViewportEntityHitCandidate>& hovered_entity,
    ImDrawList* draw_list) {
    if (draw_list == nullptr) {
        return;
    }
    const marrow::editor::SelectionItem* active = state.selection.active();
    const auto is_hovered = [&](const marrow::editor::SelectionItem& item) {
        return hovered_entity.has_value() && hovered_entity->item == item;
    };
    const auto outline_color = [&](const marrow::editor::SelectionItem& item) {
        if (active != nullptr && *active == item) {
            return IM_COL32(247, 204, 114, 255);
        }
        if (state.selection.contains(item)) {
            return IM_COL32(129, 174, 221, 235);
        }
        return is_hovered(item)
            ? IM_COL32(236, 229, 209, 230)
            : IM_COL32(105, 112, 123, 190);
    };

    for (const ViewportHitTriangle& triangle : geometry.triangles) {
        const bool selected = state.selection.contains(triangle.candidate.item);
        const bool hovered = is_hovered(triangle.candidate.item);
        if (!selected && !hovered) {
            continue;
        }
        draw_list->AddTriangle(
            triangle.points[0],
            triangle.points[1],
            triangle.points[2],
            outline_color(triangle.candidate.item),
            active != nullptr && *active == triangle.candidate.item ? 2.5f : 1.5f);
    }

    for (const ViewportHitCircle& circle : geometry.circles) {
        const bool marker = circle.marker_shape == ViewportHitMarkerShape::Diamond;
        const bool selected = state.selection.contains(circle.candidate.item);
        const bool hovered = is_hovered(circle.candidate.item);
        if (!marker && !selected && !hovered) {
            continue;
        }
        const ImU32 color = outline_color(circle.candidate.item);
        if (circle.marker_shape == ViewportHitMarkerShape::Diamond) {
            const ImVec2 top(circle.center.x, circle.center.y - circle.radius);
            const ImVec2 right(circle.center.x + circle.radius, circle.center.y);
            const ImVec2 bottom(circle.center.x, circle.center.y + circle.radius);
            const ImVec2 left(circle.center.x - circle.radius, circle.center.y);
            if (selected || hovered) {
                draw_list->AddQuadFilled(
                    top, right, bottom, left, IM_COL32(37, 43, 51, 220));
            }
            draw_list->AddQuad(
                top,
                right,
                bottom,
                left,
                color,
                active != nullptr && *active == circle.candidate.item ? 2.5f : 1.5f);
        } else if (selected || hovered) {
            draw_list->AddCircle(
                circle.center,
                circle.radius + 2.0f,
                color,
                20,
                active != nullptr && *active == circle.candidate.item ? 2.5f : 1.5f);
        }
    }

    const ResolvedSelection resolved = resolve_shell_selection(state);
    if (resolved.active_bone_index.has_value() &&
        *resolved.active_bone_index < layout.bones.size()) {
        draw_list->AddCircle(
            layout.bones[*resolved.active_bone_index].screen_position,
            layout.render_joint_radius + 4.0f,
            IM_COL32(247, 204, 114, 255),
            24,
            2.0f);
    }

    if (!state.viewport_box_selection.has_value() ||
        !state.viewport_box_selection->dragged) {
        return;
    }
    const auto& box = *state.viewport_box_selection;
    const ImVec2 minimum(
        std::min(box.start.x, box.current.x),
        std::min(box.start.y, box.current.y));
    const ImVec2 maximum(
        std::max(box.start.x, box.current.x),
        std::max(box.start.y, box.current.y));
    draw_list->AddRectFilled(minimum, maximum, IM_COL32(88, 145, 210, 32));
    draw_list->AddRect(minimum, maximum, IM_COL32(129, 174, 221, 235), 0.0f, 0, 1.5f);
    const auto candidates = collect_viewport_box_bones(state, layout, box.start, box.current);
    for (const auto& item : candidates) {
        const auto* bone = std::get_if<marrow::editor::BoneSelection>(&item);
        if (bone == nullptr || !state.load_result) {
            continue;
        }
        const auto index = state.load_result.skeleton_data->find_bone_index(bone->bone_name);
        if (!index.has_value() || *index >= layout.bones.size()) {
            continue;
        }
        draw_list->AddCircle(
            layout.bones[*index].screen_position,
            layout.render_joint_radius + 3.0f,
            IM_COL32(129, 174, 221, 245),
            20,
            2.0f);
    }
}

} // namespace


void draw_viewport_window(ShellState* state) {
    if (!ImGui::Begin(kViewportWindowTitle)) {
        finish_viewport_transform_gesture(state, false);
        state->viewport_box_selection.reset();
        ImGui::End();
        return;
    }

    // No scene → editorial empty state, no chrome.
    if (!state->load_result) {
        finish_viewport_transform_gesture(state, false);
        state->viewport_box_selection.reset();
        widgets::empty_hero(
            "VIEWPORT",
            "Nothing staged",
            "Open a .marrow project to preview the rig. The viewport "
            "renders the runtime skeleton, onion skins and weight heatmaps.");
        ImGui::End();
        return;
    }
    const ResolvedSelection resolved = resolve_shell_selection(*state);
    const WeightPaintSelectionContext weight_selection =
        resolve_weight_paint_selection_context(*state);

    const std::optional<MeshWeightPaintTarget> paint_target =
        state->load_result ? current_mesh_weight_paint_target(*state) : std::nullopt;
    const bool weight_mode_ready =
        state->weight_paint.mode == WeightPaintMode::Smooth ||
        weight_selection.influence_bone_index.has_value();
    const bool weight_tool_ready =
        state->weight_paint.enabled &&
        paint_target.has_value() &&
        weight_mode_ready;
    const std::string preview_label =
        state->selected_animation_name.empty() ? std::string("Setup pose preview")
                                               : "Animation preview / " + state->selected_animation_name;

    // ── Viewport Toolbar ──
    if (state->load_result && state->preview_skeleton) {
        const ImVec2 pre_toolbar_avail = ImGui::GetContentRegionAvail();
        if (!state->viewport_transform_gesture.has_value() &&
            icon_button(state->icons, Icon::ZoomFit, "Zoom to fit")) {
            auto_frame_skeleton(state, pre_toolbar_avail);
        }
        ImGui::SameLine();
        if (!state->viewport_transform_gesture.has_value() &&
            icon_button(state->icons, Icon::ZoomOne, "Zoom 1:1")) {
            state->viewport.zoom = 1.0;
            state->viewport.pan_x = 0.0;
            state->viewport.pan_y = 0.0;
            state->status_message = "Reset viewport to 1:1";
        }
        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        {
            const bool bones_on = state->viewport.debug_overlay.bones;
            if (icon_button(state->icons, Icon::BoneToggle, "Toggle bone overlay", bones_on)) {
                apply_debug_overlay_edit(
                    state,
                    std::string(bones_on ? "Disabled" : "Enabled") + " bones",
                    "viewport:debug-overlay:bones",
                    false,
                    [bones_on](marrow::editor::DebugOverlaySettings* s) {
                        s->bones = !bones_on;
                    });
            }
        }
        ImGui::SameLine();
        {
            const bool onion_on = state->viewport.onion_skin.enabled;
            if (icon_button(state->icons, Icon::OnionSkin, "Toggle onion skin", onion_on)) {
                apply_onion_skin_edit(
                    state,
                    std::string(onion_on ? "Disabled" : "Enabled") + " onion skinning",
                    "viewport:onion-skin:enabled",
                    false,
                    [onion_on](marrow::editor::OnionSkinSettings* s) {
                        s->enabled = !onion_on;
                    });
            }
        }
        ImGui::SameLine();
        {
            const bool hud_on = state->hud_overlay_enabled;
            if (icon_button(state->icons, Icon::PerfHud, "Toggle performance HUD", hud_on)) {
                state->hud_overlay_enabled = !hud_on;
                if (hud_on) {
                    state->hud_overlay_frame.reset();
                }
            }
        }
    }

    const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
    if (canvas_size.x < 32.0f || canvas_size.y < 32.0f) {
        finish_viewport_transform_gesture(state, false);
        state->viewport_box_selection.reset();
        ImGui::End();
        return;
    }

    const ImVec2 canvas_origin = ImGui::GetCursorScreenPos();
    const ViewportFramebufferSize framebuffer_size = viewport_framebuffer_size(
        canvas_size,
        ImGui::GetIO().DisplayFramebufferScale);
    bool use_framebuffer = state->viewport_renderer.available;
    if (use_framebuffer) {
        if (const auto error = ensure_viewport_framebuffer(
                &state->viewport_renderer,
                framebuffer_size.width,
                framebuffer_size.height)) {
            state->viewport_renderer.error_message = *error;
            use_framebuffer = false;
        }
    }

    if (use_framebuffer) {
        const ViewportTextureUv uv =
            viewport_texture_uv(sg_query_features().origin_top_left);
        ImGui::Image(
            ImTextureRef(static_cast<ImTextureID>(
                state->viewport_renderer.imgui_texture_id)),
            canvas_size,
            ImVec2(uv.u0, uv.v0),
            ImVec2(uv.u1, uv.v1));
        ImGui::SetCursorScreenPos(canvas_origin);
    }
    ImGui::InvisibleButton(
        "viewport_canvas",
        canvas_size,
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    const bool hovered = ImGui::IsItemHovered();
    ImDrawList* draw_list = ImGui::GetWindowDrawList();

    if (!state->viewport_transform_gesture.has_value() &&
        !state->viewport_box_selection.has_value() && hovered &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f)) {
        const ImVec2 mouse_delta = ImGui::GetIO().MouseDelta;
        state->viewport.pan_x += static_cast<double>(mouse_delta.x);
        state->viewport.pan_y += static_cast<double>(mouse_delta.y);
    }

    if (!state->viewport_transform_gesture.has_value() &&
        !state->viewport_box_selection.has_value() && hovered &&
        std::abs(ImGui::GetIO().MouseWheel) > 0.0f) {
        const float zoom_factor = ImGui::GetIO().MouseWheel > 0.0f ? 1.1f : 0.9f;
        (void)zoom_viewport_at_screen_position(
            state,
            canvas_origin,
            canvas_size,
            ImGui::GetIO().MousePos,
            static_cast<double>(zoom_factor));
    }

    const auto layout = build_viewport_layout(*state, canvas_origin, canvas_size);
    if (!layout.has_value()) {
        finish_viewport_transform_gesture(state, false);
        state->viewport_box_selection.reset();
    }
    std::optional<marrow::renderer::PreparedScene> frame_scene;
    if (use_framebuffer && layout.has_value() && state->preview_skeleton != nullptr &&
        !state->load_result.atlas_data.empty()) {
        auto scene_result = marrow::renderer::prepare_setup_pose_scene(
            *state->preview_skeleton, *state->load_result.atlas_data.front());
        if (scene_result) {
            frame_scene = std::move(*scene_result.scene);
        } else {
            state->viewport_renderer.error_message = scene_result.error_message;
            use_framebuffer = false;
        }
    }
    const ViewportEntityHitGeometry entity_hit_geometry = layout.has_value()
        ? build_viewport_entity_hit_geometry(
              *state,
              *layout,
              use_framebuffer && frame_scene.has_value() ? &*frame_scene : nullptr)
        : ViewportEntityHitGeometry{};
    state->hud_overlay_frame =
        state->hud_overlay_enabled ? build_preview_profiler_frame(*state) : std::nullopt;
    std::optional<MeshWeightOverlay> mesh_weight_overlay =
        layout.has_value() && (state->weight_paint.enabled || state->weight_paint.show_heatmap)
            ? build_mesh_weight_overlay(*state, *layout)
            : std::nullopt;
    const bool brush_enabled =
        state->weight_paint.enabled &&
        mesh_weight_overlay.has_value() &&
        weight_mode_ready;
    const std::optional<ViewportEntityHitCandidate> hovered_entity =
        hovered && layout.has_value()
        ? pick_viewport_entity_at_position(
              *state, *layout, entity_hit_geometry, ImGui::GetIO().MousePos)
        : std::nullopt;
    std::optional<std::size_t> hovered_bone;
    if (hovered_entity.has_value()) {
        if (const auto* bone =
                std::get_if<marrow::editor::BoneSelection>(&hovered_entity->item)) {
            hovered_bone = state->load_result.skeleton_data->find_bone_index(
                bone->bone_name);
        }
    }
    const std::optional<ViewportTranslateAxis> hovered_translate_axis =
        hovered && layout.has_value()
            ? hit_test_translate_gizmo(*state, *layout, ImGui::GetIO().MousePos)
            : std::nullopt;
    const bool hovered_rotation_ring = hovered && layout.has_value() &&
        hit_test_rotation_gizmo(*state, *layout, ImGui::GetIO().MousePos);
    const std::optional<ViewportScaleHandle> hovered_scale_handle =
        hovered && layout.has_value()
            ? hit_test_scale_gizmo(*state, *layout, ImGui::GetIO().MousePos)
            : std::nullopt;
    const std::vector<OnionSkinGhostPose> ghost_poses =
        layout.has_value() ? build_onion_skin_ghost_poses(*state, *layout)
                           : std::vector<OnionSkinGhostPose>{};
    if (state->weight_paint_stroke.active && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        finish_weight_paint_stroke(state);
    }
    if (state->viewport_box_selection.has_value() &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        // The release happened while the viewport was not submitting frames
        // (collapsed/hidden tab). Cancel instead of committing stale corners.
        state->viewport_box_selection.reset();
    }
    if (state->viewport_box_selection.has_value() &&
        ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        state->viewport_box_selection.reset();
    }
    if (hovered && layout.has_value() &&
        ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
        const ImGuiIO& io = ImGui::GetIO();
        const bool command_modifier = hierarchy_command_modifier(
            io.ConfigMacOSXBehaviors, io.KeyCtrl, io.KeySuper);
        // Deterministic press arbitration: an active edit/brush consumes the
        // press, then translate Free/X/Y, rotation, scale, entity picking,
        // and only a confirmed empty-space press may seed a box gesture.
        switch (viewport_press_target(
            authoring_gesture_active(*state),
            brush_enabled,
            hovered_translate_axis.has_value(),
            hovered_rotation_ring,
            hovered_scale_handle.has_value(),
            hovered_entity.has_value())) {
        case ViewportPressTarget::ActiveGesture:
            // The owning gesture handles its own update below or in its panel.
            break;
        case ViewportPressTarget::WeightBrush:
            begin_weight_paint_stroke(state, mesh_weight_overlay->target);
            break;
        case ViewportPressTarget::Translate:
            (void)begin_viewport_translate_gesture(
                state, *layout, *hovered_translate_axis, io.MousePos);
            break;
        case ViewportPressTarget::Rotation:
            (void)begin_viewport_rotate_gesture(state, *layout, io.MousePos);
            break;
        case ViewportPressTarget::Scale:
            (void)begin_viewport_scale_gesture(
                state, *layout, *hovered_scale_handle, io.MousePos);
            break;
        case ViewportPressTarget::Entity:
            (void)apply_viewport_point_selection_gesture(
                state, hovered_entity->item, command_modifier, true);
            break;
        case ViewportPressTarget::Box:
            (void)begin_viewport_box_selection(
                state, io.MousePos, command_modifier);
            break;
        }
    }
    if (state->viewport_transform_gesture.has_value() && layout.has_value() &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
        if (std::holds_alternative<ViewportRotateGesturePayload>(
                state->viewport_transform_gesture->payload)) {
            (void)update_viewport_rotate_gesture(
                state, *layout, ImGui::GetIO().MousePos);
        } else if (std::holds_alternative<ViewportScaleGesturePayload>(
                       state->viewport_transform_gesture->payload)) {
            (void)update_viewport_scale_gesture(
                state, *layout, ImGui::GetIO().MousePos);
        } else {
            (void)update_viewport_translate_gesture(
                state, *layout, ImGui::GetIO().MousePos);
        }
    }
    if (brush_enabled &&
        hovered &&
        mesh_weight_overlay.has_value() &&
        state->weight_paint_stroke.active &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const float sample_spacing = std::max(state->weight_paint.radius_pixels * 0.18f, 6.0f);
        if (!state->weight_paint_stroke.has_last_sample ||
            std::sqrt(
                squared_distance(
                    ImGui::GetIO().MousePos,
                    state->weight_paint_stroke.last_sample_position)) >= sample_spacing) {
            if (apply_weight_paint_sample(
                    state,
                    *mesh_weight_overlay,
                    WeightPaintSample{
                        ImGui::GetIO().MousePos,
                        state->pointer_mediator.state().stroke_pressure()}) &&
                layout.has_value()) {
                state->weight_paint_stroke.last_sample_position = ImGui::GetIO().MousePos;
                state->weight_paint_stroke.has_last_sample = true;
                mesh_weight_overlay = build_mesh_weight_overlay(*state, *layout);
            }
        }
    }
    if (state->viewport_box_selection.has_value() && layout.has_value() &&
        ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        (void)update_viewport_box_selection(state, ImGui::GetIO().MousePos);
    }
    if (state->viewport_box_selection.has_value() && layout.has_value() &&
        ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
        (void)finish_viewport_box_selection(state, *layout, true);
    }

    const ImVec2 canvas_end(
        canvas_origin.x + canvas_size.x,
        canvas_origin.y + canvas_size.y);
    const MeshWeightOverlay* rendered_overlay =
        state->weight_paint.show_heatmap && mesh_weight_overlay.has_value()
            ? &(*mesh_weight_overlay)
            : nullptr;
    if (use_framebuffer && layout.has_value()) {
        if (const auto error = render_viewport_framebuffer(
                *state,
                *layout,
                ghost_poses,
                hovered_bone,
                rendered_overlay,
                frame_scene.has_value() ? &*frame_scene : nullptr,
                &state->viewport_renderer)) {
            state->viewport_renderer.error_message = *error;
            use_framebuffer = false;
        } else {
            state->viewport_renderer.error_message.clear();
        }
    }

    if (use_framebuffer) {
        draw_list->AddRect(canvas_origin, canvas_end, IM_COL32(56, 61, 69, 255), 6.0f);
    } else {
        if (layout.has_value()) {
            draw_viewport_fallback_scene(
                *state,
                *layout,
                ghost_poses,
                hovered_bone,
                rendered_overlay,
                draw_list);
        } else {
            draw_list->AddRectFilled(
                canvas_origin,
                canvas_end,
                IM_COL32(18, 21, 25, 255),
                6.0f);
            draw_list->AddRect(
                canvas_origin,
                canvas_end,
                IM_COL32(56, 61, 69, 255),
                6.0f);
        }
    }

    if (layout.has_value()) {
        draw_viewport_annotations(*state, *layout, hovered_bone, rendered_overlay, draw_list);
        draw_viewport_selection_feedback(
            *state, *layout, entity_hit_geometry, hovered_entity, draw_list);
        draw_translate_gizmo(*state, *layout, draw_list);
        draw_rotation_gizmo(
            *state, *layout, hovered_rotation_ring, draw_list);
        draw_scale_gizmo(
            *state, *layout, hovered_scale_handle, draw_list);
        if (brush_enabled && hovered) {
            draw_list->AddCircle(
                ImGui::GetIO().MousePos,
                state->weight_paint.radius_pixels,
                IM_COL32(248, 236, 211, 210),
                48,
                1.5f);
        }
    } else {
        draw_list->AddText(
            ImVec2(canvas_origin.x + 16.0f, canvas_origin.y + 16.0f),
            IM_COL32(240, 232, 213, 255),
            "Project load failed. Reload a valid .marrow file.");
    }

    // ── Floating translucent header (top-left, overlaid on the canvas) ──
    {
        const bool animation_move_ready =
            !weight_tool_ready && !state->selected_animation_name.empty() &&
            resolved.active_bone_index.has_value();
        const char* transform_hint = active_transform_gizmo_hint(
            *state, layout.has_value() ? &*layout : nullptr);
        std::string hint =
            std::string(weight_tool_ready
                            ? "LMB brush weights"
                            : animation_move_ready
                                ? "LMB move / rotate / scale gizmo / select"
                                : "LMB select") +
            "   ·   RMB pan   ·   Wheel zoom";
        if (transform_hint != nullptr) {
            hint += "   ·   ";
            hint += transform_hint;
        }
        if (!state->selected_animation_name.empty()) {
            hint += "   ·   " +
                    format_time_seconds(state->timeline_time_seconds) + " / " +
                    format_time_seconds(timeline_preview_duration(*state));
        }
        const ImVec2 title_size = ImGui::CalcTextSize(preview_label.c_str());
        const ImVec2 hint_size = ImGui::CalcTextSize(hint.c_str());
        const float pad_x = 12.0f;
        const float pad_y = 8.0f;
        const float gap_y = 4.0f;
        const float body_w = std::max(title_size.x, hint_size.x);
        const ImVec2 head_min(canvas_origin.x + 10.0f, canvas_origin.y + 10.0f);
        const ImVec2 head_max(
            head_min.x + body_w + pad_x * 2.0f,
            head_min.y + title_size.y + hint_size.y + gap_y + pad_y * 2.0f);
        draw_list->AddRectFilled(
            head_min, head_max, IM_COL32(0x32, 0x35, 0x3b, 0xD9), 4.0f);
        draw_list->AddRect(
            head_min, head_max, IM_COL32(0x43, 0x46, 0x54, 0x33), 4.0f, 0, 1.0f);
        draw_list->AddText(
            ImVec2(head_min.x + pad_x, head_min.y + pad_y),
            IM_COL32(0xe1, 0xe2, 0xea, 0xFF),
            preview_label.c_str());
        draw_list->AddText(
            ImVec2(head_min.x + pad_x,
                   head_min.y + pad_y + title_size.y + gap_y),
            IM_COL32(0x9a, 0x9e, 0xab, 0xFF),
            hint.c_str());
    }

    if (hovered_bone.has_value() && state->load_result &&
        !hovered_translate_axis.has_value() && !hovered_rotation_ring &&
        !hovered_scale_handle.has_value()) {
        const auto& bones = state->load_result.skeleton_data->bones();
        ImGui::SetTooltip("%s", bones[*hovered_bone].name.c_str());
    }

    if (!state->viewport_renderer.error_message.empty()) {
        draw_list->AddText(
            ImVec2(canvas_origin.x + 14.0f, canvas_end.y - 28.0f),
            IM_COL32(228, 143, 104, 255),
            state->viewport_renderer.error_message.c_str());
    }

    ImGui::End();
}

void finalize_orphaned_viewport_transform_gesture(ShellState* state) {
    if (state == nullptr || !state->viewport_transform_gesture.has_value()) {
        return;
    }
    const bool cancel = ImGui::GetCurrentContext() == nullptr ||
        ImGui::IsKeyPressed(ImGuiKey_Escape, false);
    if (cancel) {
        finish_viewport_transform_gesture(state, false);
        return;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        finish_viewport_transform_gesture(state, true);
    }
}

void draw_viewport_settings(ShellState* state) {
    if (!state->load_result || !state->load_result.project) {
        return;
    }

    const std::optional<MeshWeightPaintTarget> paint_target =
        state->load_result ? current_mesh_weight_paint_target(*state) : std::nullopt;
    const WeightPaintSelectionContext weight_selection =
        resolve_weight_paint_selection_context(*state);

    if (ImGui::CollapsingHeader("Onion Skin##settings")) {
        const auto& onion_skin = state->viewport.onion_skin;
        bool onion_enabled = onion_skin.enabled;
        if (ImGui::Checkbox("Enabled##onion_skin", &onion_enabled)) {
            apply_onion_skin_edit(
                state,
                std::string(onion_enabled ? "Enabled" : "Disabled") + " onion skinning",
                "viewport:onion-skin:enabled",
                false,
                [&](marrow::editor::OnionSkinSettings* settings) {
                    settings->enabled = onion_enabled;
                });
        }

        int mode_index = onion_skin.mode == marrow::editor::OnionSkinMode::Frame ? 0 : 1;
        constexpr const char* kOnionSkinModes[] = {"Frame", "Keyframe"};
        if (ImGui::Combo(
                "Mode##onion_skin",
                &mode_index,
                kOnionSkinModes,
                IM_ARRAYSIZE(kOnionSkinModes))) {
            apply_onion_skin_edit(
                state,
                std::string("Set onion skin mode to ") + kOnionSkinModes[mode_index],
                "viewport:onion-skin:mode",
                false,
                [&](marrow::editor::OnionSkinSettings* settings) {
                    settings->mode = mode_index == 0 ? marrow::editor::OnionSkinMode::Frame
                                                     : marrow::editor::OnionSkinMode::Keyframe;
                });
        }

        bool anchor_to_zero = onion_skin.anchor_to_zero;
        if (mode_index != 0) {
            ImGui::BeginDisabled();
        }
        if (ImGui::Checkbox("Anchor To Frame 0##onion_skin", &anchor_to_zero)) {
            apply_onion_skin_edit(
                state,
                std::string(anchor_to_zero ? "Enabled" : "Disabled") + " onion-skin anchoring",
                "viewport:onion-skin:anchor",
                false,
                [&](marrow::editor::OnionSkinSettings* settings) {
                    settings->anchor_to_zero = anchor_to_zero;
                });
        }
        if (mode_index != 0) {
            ImGui::EndDisabled();
        }

        int before_count = onion_skin.before_count;
        const bool before_changed =
            ImGui::SliderInt("Before Ghosts##onion_skin", &before_count, 0, 6);
        apply_coalesced_onion_skin_drag(
            state,
            before_changed,
            "Updated onion-skin before ghost count",
            "viewport:onion-skin:before",
            false,
            [&](marrow::editor::OnionSkinSettings* settings) {
                settings->before_count = before_count;
            });

        int after_count = onion_skin.after_count;
        const bool after_changed =
            ImGui::SliderInt("After Ghosts##onion_skin", &after_count, 0, 6);
        apply_coalesced_onion_skin_drag(
            state,
            after_changed,
            "Updated onion-skin after ghost count",
            "viewport:onion-skin:after",
            false,
            [&](marrow::editor::OnionSkinSettings* settings) {
                settings->after_count = after_count;
            });

        int step = onion_skin.step;
        const char* step_label = mode_index == 0 ? "Frame Step##onion_skin"
                                                 : "Keyframe Stride##onion_skin";
        const bool step_changed = ImGui::SliderInt(step_label, &step, 1, 12);
        apply_coalesced_onion_skin_drag(
            state,
            step_changed,
            "Updated onion-skin sampling step",
            "viewport:onion-skin:step",
            false,
            [&](marrow::editor::OnionSkinSettings* settings) {
                settings->step = step;
            });

        ImGui::TextDisabled(
            "Before ghosts render cool blue, after ghosts render warm red.");
    }

    if (ImGui::CollapsingHeader("Debug Overlay##settings")) {
        const auto& debug_overlay = state->viewport.debug_overlay;
        const auto draw_toggle = [&](const char* label,
                                     const char* status_label,
                                     bool value,
                                     std::string_view group,
                                     auto mutate) {
            bool edited = value;
            if (ImGui::Checkbox(label, &edited)) {
                apply_debug_overlay_edit(
                    state,
                    std::string(edited ? "Enabled " : "Disabled ") + status_label,
                    std::string(group),
                    false,
                    [&](marrow::editor::DebugOverlaySettings* settings) {
                        mutate(settings, edited);
                    });
            }
        };

        draw_toggle(
            "Bones##debug_overlay", "bones",
            debug_overlay.bones, "viewport:debug-overlay:bones",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->bones = v; });
        draw_toggle(
            "IK Constraints##debug_overlay", "IK constraints",
            debug_overlay.ik_constraints, "viewport:debug-overlay:ik",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->ik_constraints = v; });
        draw_toggle(
            "Path Constraints##debug_overlay", "path constraints",
            debug_overlay.path_constraints, "viewport:debug-overlay:path",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->path_constraints = v; });
        draw_toggle(
            "Physics Constraints##debug_overlay", "physics constraints",
            debug_overlay.physics_constraints, "viewport:debug-overlay:physics",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->physics_constraints = v; });
        draw_toggle(
            "Mesh Wireframes##debug_overlay", "mesh wireframes",
            debug_overlay.mesh_wireframes, "viewport:debug-overlay:meshes",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->mesh_wireframes = v; });
        draw_toggle(
            "Bounding Boxes##debug_overlay", "bounding boxes",
            debug_overlay.bounding_boxes, "viewport:debug-overlay:bounds",
            [](marrow::editor::DebugOverlaySettings* s, bool v) { s->bounding_boxes = v; });
    }

    if (ImGui::CollapsingHeader("Performance HUD##settings")) {
        bool hud_enabled = state->hud_overlay_enabled;
        if (ImGui::Checkbox("Enabled##performance_hud", &hud_enabled)) {
            state->hud_overlay_enabled = hud_enabled;
            if (!hud_enabled) {
                state->hud_overlay_frame.reset();
            }
        }
        ImGui::TextDisabled("Profiles animation, transforms, skinning, and render prep.");
    }

    if (ImGui::CollapsingHeader("Weight Paint##settings")) {
        bool tool_enabled = state->weight_paint.enabled;
        if (ImGui::Checkbox("Enable Tool##weight_paint", &tool_enabled)) {
            if (!tool_enabled) {
                finish_weight_paint_stroke(state);
            }
            apply_shell_mode(
                state,
                tool_enabled ? ShellMode::WeightPaint : ShellMode::Animation);
        }

        // 3-icon toggle group (Paint / Erase / Smooth)
        const bool mode_paint = state->weight_paint.mode == WeightPaintMode::Paint;
        const bool mode_erase = state->weight_paint.mode == WeightPaintMode::Erase;
        const bool mode_smooth = state->weight_paint.mode == WeightPaintMode::Smooth;
        ImGui::TextDisabled("Mode");
        ImGui::SameLine(0.0f, 10.0f);
        if (icon_button(state->icons, Icon::WeightBrush, "Paint weights", mode_paint)) {
            state->weight_paint.mode = WeightPaintMode::Paint;
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (icon_button(state->icons, Icon::WeightErase, "Erase weights", mode_erase)) {
            state->weight_paint.mode = WeightPaintMode::Erase;
        }
        ImGui::SameLine(0.0f, 4.0f);
        if (icon_button(state->icons, Icon::WeightSmooth, "Smooth weights", mode_smooth)) {
            state->weight_paint.mode = WeightPaintMode::Smooth;
        }

        ImGui::SliderFloat(
            "Radius##weight_paint", &state->weight_paint.radius_pixels,
            8.0f, 160.0f, "%.0f px");
        ImGui::SliderFloat(
            "Strength##weight_paint", &state->weight_paint.strength,
            0.05f, 1.0f, "%.2f");
        ImGui::Checkbox("Show Heat Map##weight_paint", &state->weight_paint.show_heatmap);

        if (paint_target.has_value()) {
            ImGui::Text("Preview mesh: %s", paint_target->display_attachment_name.c_str());
            ImGui::Text("Editing source: %s / %s",
                paint_target->source_skin_name.c_str(),
                paint_target->source_attachment_name.c_str());
        } else {
            ImGui::TextDisabled("Select a mesh slot to paint weights.");
        }

        if (weight_selection.influence_bone_index.has_value() &&
            *weight_selection.influence_bone_index <
                state->load_result.skeleton_data->bones().size()) {
            ImGui::Text(
                "Influence bone: %s",
                state->load_result.skeleton_data
                    ->bones()[*weight_selection.influence_bone_index]
                    .name.c_str());
        } else {
            ImGui::TextDisabled(
                state->weight_paint.mode == WeightPaintMode::Smooth
                    ? "Smooth does not require an influence bone."
                    : "Select a bone, or a single slot/attachment, for Paint/Erase.");
        }
    }
}


} // namespace marrow::editor::shell
