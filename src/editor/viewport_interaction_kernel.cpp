#include "viewport_interaction_kernel.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace marrow::editor::viewport_interaction_kernel {
namespace {

constexpr double kPi = 3.14159265358979323846;

bool finite(Point point) {
    return std::isfinite(point.x) && std::isfinite(point.y);
}

bool finite(Matrix2 matrix) {
    return std::isfinite(matrix.a) && std::isfinite(matrix.b) &&
        std::isfinite(matrix.c) && std::isfinite(matrix.d);
}

} // namespace

std::optional<RotationBasis> make_rotation_basis(
    Point pivot,
    Matrix2 parent_space) {
    if (!finite(pivot) || !finite(parent_space)) {
        return std::nullopt;
    }
    const double matrix_scale = std::max(
        {std::abs(parent_space.a), std::abs(parent_space.b),
         std::abs(parent_space.c), std::abs(parent_space.d)});
    if (!std::isfinite(matrix_scale) || matrix_scale <= 0.0) {
        return std::nullopt;
    }
    const Matrix2 normalized{
        parent_space.a / matrix_scale,
        parent_space.b / matrix_scale,
        parent_space.c / matrix_scale,
        parent_space.d / matrix_scale};
    const double determinant =
        (normalized.a * normalized.d) - (normalized.b * normalized.c);
    if (!std::isfinite(determinant) ||
        std::abs(determinant) <= kRelativeSingularEpsilon) {
        return std::nullopt;
    }
    const double inverse_scale = 1.0 / matrix_scale;
    RotationBasis result;
    result.pivot = pivot;
    result.inverse = Matrix2{
        normalized.d * inverse_scale / determinant,
        -normalized.b * inverse_scale / determinant,
        -normalized.c * inverse_scale / determinant,
        normalized.a * inverse_scale / determinant};
    if (!finite(result.inverse)) {
        return std::nullopt;
    }
    return result;
}

std::optional<double> rotation_angle(
    const RotationBasis& basis,
    Point pointer) {
    if (!finite(pointer) || !finite(basis.pivot) || !finite(basis.inverse)) {
        return std::nullopt;
    }
    const double dx = pointer.x - basis.pivot.x;
    const double dy = pointer.y - basis.pivot.y;
    const double local_x = (basis.inverse.a * dx) + (basis.inverse.b * dy);
    const double local_y = (basis.inverse.c * dx) + (basis.inverse.d * dy);
    if (!std::isfinite(local_x) || !std::isfinite(local_y) ||
        (local_x == 0.0 && local_y == 0.0)) {
        return std::nullopt;
    }
    const double angle = std::atan2(local_y, local_x) * 180.0 / kPi;
    return std::isfinite(angle) ? std::optional<double>(angle) : std::nullopt;
}

double unwrap_rotation_delta(double delta) {
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

RotationUpdate update_rotation_drag(
    RotationDragState* state,
    double pivot_distance_squared,
    std::optional<double> wrapped_angle) {
    if (state == nullptr || !std::isfinite(pivot_distance_squared) ||
        pivot_distance_squared < 0.0 ||
        !std::isfinite(state->accumulated_rotation)) {
        return {RotationUpdateResult::Invalid, 0.0};
    }
    if (pivot_distance_squared <=
        kRotationPivotSuspendRadius * kRotationPivotSuspendRadius) {
        state->previous_wrapped_angle.reset();
        state->angular_reference_suspended = true;
        return {RotationUpdateResult::Suspended, state->accumulated_rotation};
    }
    if (!wrapped_angle.has_value() || !std::isfinite(*wrapped_angle)) {
        return {RotationUpdateResult::Invalid, state->accumulated_rotation};
    }
    if (state->angular_reference_suspended ||
        !state->previous_wrapped_angle.has_value()) {
        state->previous_wrapped_angle = *wrapped_angle;
        state->angular_reference_suspended = false;
        return {RotationUpdateResult::Rebased, state->accumulated_rotation};
    }

    const double step = unwrap_rotation_delta(
        *wrapped_angle - *state->previous_wrapped_angle);
    state->previous_wrapped_angle = *wrapped_angle;
    if (!std::isfinite(step)) {
        return {RotationUpdateResult::Invalid, state->accumulated_rotation};
    }
    if (std::abs(step) <= 1e-9) {
        return {RotationUpdateResult::Unchanged, state->accumulated_rotation};
    }
    const double accumulated = state->accumulated_rotation + step;
    if (!std::isfinite(accumulated)) {
        return {RotationUpdateResult::Invalid, state->accumulated_rotation};
    }
    state->accumulated_rotation = accumulated;
    return {RotationUpdateResult::Changed, accumulated};
}

std::optional<ScaleBasis> make_scale_basis(
    Point pivot,
    Matrix2 parent_space,
    double local_rotation_degrees,
    double local_shear_x_degrees,
    double local_shear_y_degrees) {
    if (!finite(pivot) || !finite(parent_space) ||
        !std::isfinite(local_rotation_degrees) ||
        !std::isfinite(local_shear_x_degrees) ||
        !std::isfinite(local_shear_y_degrees)) {
        return std::nullopt;
    }
    const double matrix_scale = std::max(
        {std::abs(parent_space.a), std::abs(parent_space.b),
         std::abs(parent_space.c), std::abs(parent_space.d)});
    if (!std::isfinite(matrix_scale) || matrix_scale <= 0.0) {
        return std::nullopt;
    }
    const double determinant =
        ((parent_space.a / matrix_scale) * (parent_space.d / matrix_scale)) -
        ((parent_space.b / matrix_scale) * (parent_space.c / matrix_scale));
    if (!std::isfinite(determinant) ||
        std::abs(determinant) <= kRelativeSingularEpsilon) {
        return std::nullopt;
    }

    const double x_angle =
        (local_rotation_degrees + local_shear_x_degrees) * kPi / 180.0;
    const double y_angle =
        (local_rotation_degrees + 90.0 + local_shear_y_degrees) * kPi / 180.0;
    const Point local_x{std::cos(x_angle), std::sin(x_angle)};
    const Point local_y{std::cos(y_angle), std::sin(y_angle)};
    Point screen_x{
        (parent_space.a * local_x.x) + (parent_space.b * local_x.y),
        -((parent_space.c * local_x.x) + (parent_space.d * local_x.y))};
    Point screen_y{
        (parent_space.a * local_y.x) + (parent_space.b * local_y.y),
        -((parent_space.c * local_y.x) + (parent_space.d * local_y.y))};
    const double x_length = std::hypot(screen_x.x, screen_x.y);
    const double y_length = std::hypot(screen_y.x, screen_y.y);
    if (!std::isfinite(x_length) || !std::isfinite(y_length) ||
        x_length <= kRelativeSingularEpsilon ||
        y_length <= kRelativeSingularEpsilon) {
        return std::nullopt;
    }
    screen_x.x /= x_length;
    screen_x.y /= x_length;
    screen_y.x /= y_length;
    screen_y.y /= y_length;
    const double direction_determinant =
        (screen_x.x * screen_y.y) - (screen_x.y * screen_y.x);
    if (!std::isfinite(direction_determinant) ||
        std::abs(direction_determinant) <= kRelativeSingularEpsilon) {
        return std::nullopt;
    }
    Point uniform{screen_x.x + screen_y.x, screen_x.y + screen_y.y};
    const double uniform_length = std::hypot(uniform.x, uniform.y);
    if (!std::isfinite(uniform_length) ||
        uniform_length <= kRelativeSingularEpsilon) {
        return std::nullopt;
    }
    uniform.x /= uniform_length;
    uniform.y /= uniform_length;
    return ScaleBasis{pivot, screen_x, screen_y, uniform};
}

std::optional<ScaleCandidate> map_scale(
    const ScaleMapping& mapping,
    Point pointer_screen) {
    if (!finite(mapping.pivot_screen) || !finite(mapping.direction) ||
        !finite(pointer_screen) ||
        !std::isfinite(mapping.start_projection_pixels) ||
        std::abs(mapping.start_projection_pixels) <= kRelativeSingularEpsilon ||
        !std::isfinite(mapping.start_scale_x) ||
        !std::isfinite(mapping.start_scale_y)) {
        return std::nullopt;
    }
    const double projection =
        (pointer_screen.x - mapping.pivot_screen.x) * mapping.direction.x +
        (pointer_screen.y - mapping.pivot_screen.y) * mapping.direction.y;
    if (!std::isfinite(projection)) {
        return std::nullopt;
    }

    ScaleCandidate candidate{mapping.start_scale_x, mapping.start_scale_y};
    if (mapping.handle == ScaleHandle::Uniform) {
        if (mapping.start_scale_x == 0.0 && mapping.start_scale_y == 0.0) {
            return std::nullopt;
        }
        const double ratio = projection / mapping.start_projection_pixels;
        candidate.scale_x = mapping.start_scale_x * ratio;
        candidate.scale_y = mapping.start_scale_y * ratio;
    } else {
        double* component = mapping.handle == ScaleHandle::X
            ? &candidate.scale_x
            : &candidate.scale_y;
        const double start_component = *component;
        if (start_component == 0.0) {
            *component = (projection - mapping.start_projection_pixels) /
                kScaleHandleRadius;
        } else {
            *component = start_component * projection /
                mapping.start_projection_pixels;
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

std::optional<std::size_t> resolve_hit_index(
    const std::vector<RankedHit>& candidates) {
    if (candidates.empty()) {
        return std::nullopt;
    }
    const auto best = std::min_element(
        candidates.begin(),
        candidates.end(),
        [](const RankedHit& left, const RankedHit& right) {
            if (left.priority != right.priority) {
                return left.priority < right.priority;
            }
            if (left.distance_squared != right.distance_squared) {
                return left.distance_squared < right.distance_squared;
            }
            return left.stable_order < right.stable_order;
        });
    return best->source_index;
}

PressTarget resolve_press_target(
    bool active_gesture,
    bool weight_brush,
    bool translate,
    bool rotation,
    bool scale,
    bool entity) {
    if (active_gesture) return PressTarget::ActiveGesture;
    if (weight_brush) return PressTarget::WeightBrush;
    if (translate) return PressTarget::Translate;
    if (rotation) return PressTarget::Rotation;
    if (scale) return PressTarget::Scale;
    if (entity) return PressTarget::Entity;
    return PressTarget::Box;
}

CompletionDecision completion_decision(
    bool commit_requested,
    bool changed,
    bool context_valid) {
    const bool effective_commit = commit_requested && context_valid;
    if (effective_commit && changed) {
        return {CompletionAction::Commit, false, 1U};
    }
    return {CompletionAction::Cancel, !effective_commit, 0U};
}

} // namespace marrow::editor::viewport_interaction_kernel
