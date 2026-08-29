#include "viewport_interaction_controller.hpp"

#include "shell_preview.hpp"
#include "shell_project_panels.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "shell_selection.hpp"
#include "shell_timeline.hpp"
#include "shell_weight_paint.hpp"
#include "viewport_interaction_kernel.hpp"

namespace marrow::editor::shell::viewport_interaction {

ViewportPressTarget press_target(
    bool active_gesture,
    bool weight_brush,
    bool translate,
    bool rotation,
    bool scale,
    bool ffd_vertex,
    bool entity) {
    return static_cast<ViewportPressTarget>(
        marrow::editor::viewport_interaction_kernel::resolve_press_target(
            active_gesture,
            weight_brush,
            translate,
            rotation,
            scale,
            ffd_vertex,
            entity));
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

std::optional<ViewportRotationBasis> rotation_basis(
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

    const auto kernel_basis =
        marrow::editor::viewport_interaction_kernel::make_rotation_basis(
            {basis.pivot_world.x, basis.pivot_world.y}, {a, b, c, d});
    if (!kernel_basis.has_value()) {
        return std::nullopt;
    }
    basis.inverse_a = kernel_basis->inverse.a;
    basis.inverse_b = kernel_basis->inverse.b;
    basis.inverse_c = kernel_basis->inverse.c;
    basis.inverse_d = kernel_basis->inverse.d;
    return basis;
}

std::optional<double> rotation_angle(
    const ViewportRotationBasis& basis,
    const ViewportWorldPoint& pointer_world) {
    return marrow::editor::viewport_interaction_kernel::rotation_angle(
        {{basis.pivot_world.x, basis.pivot_world.y},
         {basis.inverse_a, basis.inverse_b, basis.inverse_c, basis.inverse_d}},
        {pointer_world.x, pointer_world.y});
}

double unwrap_rotation_delta(double delta) {
    return marrow::editor::viewport_interaction_kernel::unwrap_rotation_delta(delta);
}

std::optional<ViewportScaleBasis> scale_basis(
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

    const auto kernel_basis =
        marrow::editor::viewport_interaction_kernel::make_scale_basis(
            {basis.pivot_world.x, basis.pivot_world.y},
            {parent_a, parent_b, parent_c, parent_d},
            static_cast<double>(pose.local_pose.rotation),
            static_cast<double>(pose.local_pose.shear_x),
            static_cast<double>(pose.local_pose.shear_y));
    if (!kernel_basis.has_value()) {
        return std::nullopt;
    }
    basis.positive_x_screen_direction = ImVec2(
        static_cast<float>(kernel_basis->positive_x_screen_direction.x),
        static_cast<float>(kernel_basis->positive_x_screen_direction.y));
    basis.positive_y_screen_direction = ImVec2(
        static_cast<float>(kernel_basis->positive_y_screen_direction.x),
        static_cast<float>(kernel_basis->positive_y_screen_direction.y));
    basis.uniform_screen_direction = ImVec2(
        static_cast<float>(kernel_basis->uniform_screen_direction.x),
        static_cast<float>(kernel_basis->uniform_screen_direction.y));
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

std::optional<ViewportScaleCandidate> scale_candidate(
    const ViewportScaleGesturePayload& payload,
    const ViewportLayout& layout,
    const ImVec2& pointer) {
    if (!std::isfinite(layout.pixels_per_unit) || layout.pixels_per_unit <= 0.0f) {
        return std::nullopt;
    }
    const ImVec2 pivot = screen_from_world(
        layout, payload.basis.pivot_world.x, payload.basis.pivot_world.y);
    const ImVec2& direction =
        scale_handle_direction(payload.basis, payload.handle);
    namespace kernel = marrow::editor::viewport_interaction_kernel;
    const auto candidate = kernel::map_scale(
        {{static_cast<double>(pivot.x), static_cast<double>(pivot.y)},
         {static_cast<double>(direction.x), static_cast<double>(direction.y)},
         static_cast<kernel::ScaleHandle>(payload.handle),
         payload.start_absolute_scale_x,
         payload.start_absolute_scale_y,
         payload.start_projection_pixels},
        {static_cast<double>(pointer.x), static_cast<double>(pointer.y)});
    if (!candidate.has_value()) {
        return std::nullopt;
    }
    return ViewportScaleCandidate{candidate->scale_x, candidate->scale_y};
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


std::optional<std::size_t> scale_gizmo_bone_index(
    const ShellState& state,
    const ViewportLayout& layout) {
    const auto bone_index = rotation_gizmo_bone_index(state, layout);
    if (!bone_index.has_value() ||
        !scale_basis(*state.preview_skeleton, *bone_index).has_value()) {
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
    return *scale_basis(*state.preview_skeleton, bone_index);
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

const char* transform_hint(
    const ShellState& state,
    const ViewportLayout& layout) {
    const char* hint = active_rotation_inherit_hint(state);
    if (hint == nullptr) {
        const auto scale_bone_index =
            scale_gizmo_bone_index(state, layout);
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


bool begin_box_selection(
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

bool update_box_selection(
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

bool finish_box_selection(
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

std::optional<marrow::runtime::AttachmentVertex> local_position_for_world_target(
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

void finish_transform_gesture(ShellState* state, bool commit) {
    if (state == nullptr || !state->viewport_transform_gesture.has_value()) {
        return;
    }
    const bool context_valid = !commit || viewport_transform_context_valid(
        *state, *state->viewport_transform_gesture);
    ViewportTransformGesture gesture =
        std::move(*state->viewport_transform_gesture);
    state->viewport_transform_gesture.reset();
    const bool rotate =
        std::holds_alternative<ViewportRotateGesturePayload>(gesture.payload);
    const bool scale =
        std::holds_alternative<ViewportScaleGesturePayload>(gesture.payload);
    const char* action_name = rotate ? "rotation" : scale ? "scale" : "move";
    const auto completion =
        marrow::editor::viewport_interaction_kernel::completion_decision(
            commit, gesture.changed, context_valid);
    if (completion.action ==
        marrow::editor::viewport_interaction_kernel::CompletionAction::Cancel) {
        gesture.transaction.cancel();
        sync_shell_from_editor_session(state);
        restore_viewport_transform_context(state, gesture);
        if (completion.report_cancelled) {
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

bool begin_viewport_transform_gesture(
    ShellState* state,
    std::size_t bone_index,
    const char* action,
    const char* group_prefix,
    ViewportTransformGesturePayload payload) {
    if (state == nullptr || !state->load_result ||
        state->load_result.project == nullptr ||
        state->session.runtime_data() == nullptr ||
        state->selected_animation_name.empty() ||
        !std::isfinite(state->timeline_time_seconds) ||
        bone_index >= state->session.runtime_data()->bones().size() ||
        authoring_gesture_active(*state)) {
        return false;
    }

    const std::string bone_name =
        state->session.runtime_data()->bones()[bone_index].name;
    state->timeline_playing = false;
    state->session.set_playing(false);
    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::MoveBone,
        std::string(action) + " bone " + bone_name,
        std::string(group_prefix) + ":" + state->selected_animation_name + ":" + bone_name,
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
    gesture.payload = std::move(payload);
    state->viewport_transform_gesture.emplace(std::move(gesture));
    return true;
}

bool begin_translate_gesture(
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
    return begin_viewport_transform_gesture(
        state,
        bone_index,
        "Move",
        "viewport-translate",
        ViewportTranslateGesturePayload{
            axis,
            pointer_world,
            ViewportWorldPoint{world.world_x, world.world_y},
            ViewportWorldPoint{world.world_x, world.world_y}});
}

bool update_translate_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer,
    ViewportSnapModifiers modifiers) {
    if (state == nullptr || !state->viewport_transform_gesture.has_value() ||
        state->preview_skeleton == nullptr) {
        return false;
    }
    auto& gesture = *state->viewport_transform_gesture;
    auto* translate = std::get_if<ViewportTranslateGesturePayload>(&gesture.payload);
    if (translate == nullptr || !viewport_transform_context_valid(*state, gesture)) {
        finish_transform_gesture(state, false);
        return false;
    }
    const ViewportWorldPoint pointer_world = world_from_screen(layout, pointer);
    if (!finite_world_point(pointer_world)) {
        finish_transform_gesture(state, false);
        state->error_message = "Cannot move a bone with a non-finite pointer.";
        return false;
    }
    const double delta_x = pointer_world.x - translate->pointer_start.x;
    const double delta_y = pointer_world.y - translate->pointer_start.y;
    const bool applicable_x_moved =
        translate->axis != ViewportTranslateAxis::Y &&
        std::abs(delta_x) > 1e-6;
    const bool applicable_y_moved =
        translate->axis != ViewportTranslateAxis::X &&
        std::abs(delta_y) > 1e-6;
    if (!gesture.changed && !applicable_x_moved && !applicable_y_moved) {
        return true;
    }
    ViewportWorldPoint target = translate->bone_world_start;
    if (translate->axis != ViewportTranslateAxis::Y) {
        target.x += delta_x;
    }
    if (translate->axis != ViewportTranslateAxis::X) {
        target.y += delta_y;
    }
    namespace kernel = marrow::editor::viewport_interaction_kernel;
    const marrow::editor::ProjectSnapSettings default_settings;
    const auto& snap_settings = gesture.transaction.project()->snap_settings.has_value()
        ? *gesture.transaction.project()->snap_settings
        : default_settings;
    const kernel::SnapActivation activation{
        snap_settings.world_grid_enabled,
        modifiers.temporarily_enable,
        modifiers.bypass};
    if (translate->axis != ViewportTranslateAxis::Y) {
        const auto snapped = kernel::snap_scalar({
            target.x, snap_settings.world_grid_step, activation});
        if (!snapped.has_value()) {
            finish_transform_gesture(state, false);
            state->error_message = "Bone move was cancelled after invalid snap math.";
            return false;
        }
        target.x = *snapped;
    }
    if (translate->axis != ViewportTranslateAxis::X) {
        const auto snapped = kernel::snap_scalar({
            target.y, snap_settings.world_grid_step, activation});
        if (!snapped.has_value()) {
            finish_transform_gesture(state, false);
            state->error_message = "Bone move was cancelled after invalid snap math.";
            return false;
        }
        target.y = *snapped;
    }
    if (std::abs(target.x - translate->current_world_target.x) <= 1e-12 &&
        std::abs(target.y - translate->current_world_target.y) <= 1e-12) {
        return true;
    }
    const auto local = local_position_for_world_target(
        *state->preview_skeleton, gesture.bone_index, target);
    if (!local.has_value()) {
        const std::string error =
            "Cannot move a bone through a singular parent transform.";
        finish_transform_gesture(state, false);
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
        finish_transform_gesture(state, false);
        state->error_message = error;
        return false;
    }
    translate->current_world_target = target;
    gesture.changed = true;
    sync_shell_from_editor_session(state);
    return true;
}

bool begin_rotate_gesture(
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
    const auto basis = rotation_basis(*state->preview_skeleton, *bone_index);
    const ViewportWorldPoint pointer_world = world_from_screen(layout, pointer);
    const auto wrapped_angle = basis.has_value()
        ? rotation_angle(*basis, pointer_world)
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
    ViewportRotateGesturePayload rotate;
    rotate.basis = *basis;
    rotate.start_absolute_rotation = start_rotation;
    rotate.previous_wrapped_angle = *wrapped_angle;
    rotate.pointer_screen = pointer;
    rotate.current_absolute_rotation = start_rotation;
    const bool started = begin_viewport_transform_gesture(
        state,
        *bone_index,
        "Rotate",
        "viewport-rotate",
        std::move(rotate));
    if (started) {
        state->error_message.clear();
    }
    return started;
}

bool update_rotate_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer,
    ViewportSnapModifiers modifiers) {
    if (state == nullptr || !state->viewport_transform_gesture.has_value()) {
        return false;
    }
    auto& gesture = *state->viewport_transform_gesture;
    auto* rotate = std::get_if<ViewportRotateGesturePayload>(&gesture.payload);
    if (rotate == nullptr || !viewport_transform_context_valid(*state, gesture) ||
        !std::isfinite(layout.pixels_per_unit) || layout.pixels_per_unit <= 0.0f ||
        !std::isfinite(pointer.x) || !std::isfinite(pointer.y)) {
        finish_transform_gesture(state, false);
        state->error_message = "Bone rotation was cancelled after viewport context loss.";
        return false;
    }

    rotate->pointer_screen = pointer;
    const ImVec2 pivot_screen = screen_from_world(
        layout, rotate->basis.pivot_world.x, rotate->basis.pivot_world.y);
    if (!std::isfinite(pivot_screen.x) || !std::isfinite(pivot_screen.y)) {
        finish_transform_gesture(state, false);
        state->error_message = "Bone rotation was cancelled after invalid viewport math.";
        return false;
    }
    const double pivot_distance_squared = squared_distance(pivot_screen, pointer);
    const bool at_pivot = pivot_distance_squared <=
        kRotationPivotSuspendRadius * kRotationPivotSuspendRadius;
    std::optional<double> wrapped_angle;
    if (!at_pivot) {
        const ViewportWorldPoint pointer_world = world_from_screen(layout, pointer);
        wrapped_angle = rotation_angle(rotate->basis, pointer_world);
        if (!wrapped_angle.has_value()) {
            finish_transform_gesture(state, false);
            state->error_message =
                "Bone rotation was cancelled after invalid parent-space math.";
            return false;
        }
    }
    namespace kernel = marrow::editor::viewport_interaction_kernel;
    kernel::RotationDragState drag_state{
        rotate->previous_wrapped_angle,
        rotate->accumulated_rotation,
        rotate->angular_reference_suspended};
    const kernel::RotationUpdate update = kernel::update_rotation_drag(
        &drag_state, pivot_distance_squared, wrapped_angle);
    rotate->previous_wrapped_angle = drag_state.previous_wrapped_angle;
    rotate->accumulated_rotation = drag_state.accumulated_rotation;
    rotate->angular_reference_suspended = drag_state.angular_reference_suspended;
    if (update.result == kernel::RotationUpdateResult::Invalid) {
        finish_transform_gesture(state, false);
        state->error_message =
            "Bone rotation was cancelled after a non-finite angle.";
        return false;
    }
    if (update.result == kernel::RotationUpdateResult::Changed) {
        rotate->has_pointer_movement = true;
    }
    if (!rotate->has_pointer_movement) {
        return true;
    }
    const double accumulated = update.accumulated_rotation;
    const double raw_candidate = rotate->start_absolute_rotation + accumulated;
    if (!std::isfinite(accumulated) || !std::isfinite(raw_candidate)) {
        finish_transform_gesture(state, false);
        state->error_message = "Bone rotation was cancelled after a non-finite result.";
        return false;
    }
    const marrow::editor::ProjectSnapSettings default_settings;
    const auto& snap_settings = gesture.transaction.project()->snap_settings.has_value()
        ? *gesture.transaction.project()->snap_settings
        : default_settings;
    const auto snapped = kernel::snap_scalar({
        raw_candidate,
        snap_settings.local_angle_step_degrees,
        {snap_settings.local_angle_enabled,
         modifiers.temporarily_enable,
         modifiers.bypass}});
    if (!snapped.has_value()) {
        finish_transform_gesture(state, false);
        state->error_message = "Bone rotation was cancelled after invalid snap math.";
        return false;
    }
    const double candidate = *snapped;
    if (std::abs(candidate - rotate->current_absolute_rotation) <= 1e-12) {
        return true;
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
        finish_transform_gesture(state, false);
        state->error_message = error;
        return false;
    }
    rotate->current_absolute_rotation = candidate;
    gesture.changed = true;
    sync_shell_from_editor_session(state);
    return true;
}

bool begin_scale_gesture(
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
        scale_basis(*state->preview_skeleton, *bone_index);
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

    ViewportScaleGesturePayload scale;
    scale.basis = *basis;
    scale.handle = handle;
    scale.start_absolute_scale_x = start_scale->scale_x;
    scale.start_absolute_scale_y = start_scale->scale_y;
    scale.start_projection_pixels = start_projection;
    scale.pointer_screen = pointer;
    scale.current_absolute_scale_x = start_scale->scale_x;
    scale.current_absolute_scale_y = start_scale->scale_y;
    const bool started = begin_viewport_transform_gesture(
        state,
        *bone_index,
        "Scale",
        "viewport-scale",
        std::move(scale));
    if (started) {
        state->error_message.clear();
    }
    return started;
}

bool update_scale_gesture(
    ShellState* state,
    const ViewportLayout& layout,
    const ImVec2& pointer,
    ViewportSnapModifiers modifiers) {
    if (state == nullptr || !state->viewport_transform_gesture.has_value()) {
        return false;
    }
    auto& gesture = *state->viewport_transform_gesture;
    auto* scale =
        std::get_if<ViewportScaleGesturePayload>(&gesture.payload);
    if (scale == nullptr ||
        !viewport_transform_context_valid(*state, gesture)) {
        finish_transform_gesture(state, false);
        state->error_message =
            "Bone scale was cancelled after viewport context loss.";
        return false;
    }
    scale->pointer_screen = pointer;
    const auto raw_candidate =
        scale_candidate(*scale, layout, pointer);
    if (!raw_candidate.has_value()) {
        finish_transform_gesture(state, false);
        state->error_message =
            "Bone scale was cancelled after invalid local-axis math.";
        return false;
    }
    constexpr double kScaleChangeEpsilon = 1e-12;
    if (std::abs(
            raw_candidate->scale_x - scale->start_absolute_scale_x) >
            kScaleChangeEpsilon ||
        std::abs(
            raw_candidate->scale_y - scale->start_absolute_scale_y) >
            kScaleChangeEpsilon) {
        scale->has_pointer_movement = true;
    }
    if (!scale->has_pointer_movement) {
        return true;
    }
    namespace kernel = marrow::editor::viewport_interaction_kernel;
    const marrow::editor::ProjectSnapSettings default_settings;
    const auto& snap_settings = gesture.transaction.project()->snap_settings.has_value()
        ? *gesture.transaction.project()->snap_settings
        : default_settings;
    const kernel::ScaleHandle kernel_handle =
        scale->handle == ViewportScaleHandle::X
        ? kernel::ScaleHandle::X
        : scale->handle == ViewportScaleHandle::Y
        ? kernel::ScaleHandle::Y
        : kernel::ScaleHandle::Uniform;
    const auto candidate = kernel::snap_scale_candidate(
        {raw_candidate->scale_x, raw_candidate->scale_y},
        kernel_handle,
        snap_settings.absolute_scale_step,
        {snap_settings.absolute_scale_enabled,
         modifiers.temporarily_enable,
         modifiers.bypass});
    if (!candidate.has_value()) {
        finish_transform_gesture(state, false);
        state->error_message =
            "Bone scale was cancelled after invalid snap math.";
        return false;
    }
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
        finish_transform_gesture(state, false);
        state->error_message = error;
        return false;
    }
    scale->current_absolute_scale_x = candidate->scale_x;
    scale->current_absolute_scale_y = candidate->scale_y;
    gesture.changed = true;
    sync_shell_from_editor_session(state);
    return true;
}

bool rotation_gizmo_visible(
    const ShellState& state,
    const ViewportLayout& layout) {
    return rotation_gizmo_bone_index(state, layout).has_value();
}

bool scale_gizmo_visible(
    const ShellState& state,
    const ViewportLayout& layout) {
    return scale_gizmo_bone_index(state, layout).has_value();
}

bool uniform_scale_handle_visible(
    const ShellState& state,
    const ViewportLayout& layout) {
    const auto bone_index = scale_gizmo_bone_index(state, layout);
    return bone_index.has_value() &&
        uniform_scale_handle_enabled(state, *bone_index);
}


} // namespace marrow::editor::shell::viewport_interaction
