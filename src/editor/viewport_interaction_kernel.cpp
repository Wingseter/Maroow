#include "viewport_interaction_kernel.hpp"

#include <algorithm>
#include <cmath>
#include <iterator>
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

std::optional<Matrix2> inverse_matrix(Matrix2 matrix) {
    if (!finite(matrix)) {
        return std::nullopt;
    }
    const double matrix_scale = std::max(
        {std::abs(matrix.a), std::abs(matrix.b),
         std::abs(matrix.c), std::abs(matrix.d)});
    if (!std::isfinite(matrix_scale) || matrix_scale <= 0.0) {
        return std::nullopt;
    }
    const Matrix2 normalized{
        matrix.a / matrix_scale,
        matrix.b / matrix_scale,
        matrix.c / matrix_scale,
        matrix.d / matrix_scale};
    const double determinant =
        (normalized.a * normalized.d) - (normalized.b * normalized.c);
    if (!std::isfinite(determinant) ||
        std::abs(determinant) <= kRelativeSingularEpsilon) {
        return std::nullopt;
    }
    const double inverse_scale = 1.0 / matrix_scale;
    const Matrix2 inverse{
        normalized.d * inverse_scale / determinant,
        -normalized.b * inverse_scale / determinant,
        -normalized.c * inverse_scale / determinant,
        normalized.a * inverse_scale / determinant};
    return finite(inverse) ? std::optional<Matrix2>(inverse) : std::nullopt;
}

} // namespace

std::optional<RotationBasis> make_rotation_basis(
    Point pivot,
    Matrix2 parent_space) {
    if (!finite(pivot) || !finite(parent_space)) {
        return std::nullopt;
    }
    const auto inverse = inverse_matrix(parent_space);
    if (!inverse.has_value()) {
        return std::nullopt;
    }
    RotationBasis result;
    result.pivot = pivot;
    result.inverse = *inverse;
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

std::optional<std::size_t> nearest_ffd_vertex(
    const std::vector<Point>& vertex_screen_positions,
    Point pointer_screen,
    double hit_radius) {
    if (!finite(pointer_screen) || !std::isfinite(hit_radius) || hit_radius < 0.0) {
        return std::nullopt;
    }
    const double radius_squared = hit_radius * hit_radius;
    std::optional<std::size_t> best_index;
    double best_distance_squared = radius_squared;
    for (std::size_t index = 0U; index < vertex_screen_positions.size(); ++index) {
        const Point position = vertex_screen_positions[index];
        if (!finite(position)) {
            continue;
        }
        const double dx = pointer_screen.x - position.x;
        const double dy = pointer_screen.y - position.y;
        const double distance_squared = (dx * dx) + (dy * dy);
        if (!std::isfinite(distance_squared) || distance_squared > radius_squared) {
            continue;
        }
        if (!best_index.has_value() || distance_squared < best_distance_squared ||
            (distance_squared == best_distance_squared && index < *best_index)) {
            best_index = index;
            best_distance_squared = distance_squared;
        }
    }
    return best_index;
}

namespace {

bool valid_ffd_selection(
    const std::vector<std::size_t>& selection,
    std::size_t vertex_count) {
    std::optional<std::size_t> previous;
    for (const std::size_t vertex_index : selection) {
        if (vertex_index >= vertex_count ||
            (previous.has_value() && vertex_index <= *previous)) {
            return false;
        }
        previous = vertex_index;
    }
    return true;
}

} // namespace

std::optional<std::vector<std::size_t>> update_ffd_point_selection(
    const std::vector<std::size_t>& current_selection,
    std::size_t vertex_count,
    std::size_t vertex_index,
    FfdPointSelectionMode mode) {
    if (vertex_count == 0U || vertex_index >= vertex_count ||
        !valid_ffd_selection(current_selection, vertex_count)) {
        return std::nullopt;
    }
    if (mode == FfdPointSelectionMode::Replace) {
        return std::vector<std::size_t>{vertex_index};
    }
    std::vector<std::size_t> candidate = current_selection;
    const auto position = std::lower_bound(
        candidate.begin(), candidate.end(), vertex_index);
    if (position != candidate.end() && *position == vertex_index) {
        candidate.erase(position);
    } else {
        candidate.insert(position, vertex_index);
    }
    return candidate;
}

std::optional<std::vector<std::size_t>> collect_ffd_vertices_in_box(
    const std::vector<Point>& vertex_screen_positions,
    Point first_corner,
    Point second_corner) {
    if (!finite(first_corner) || !finite(second_corner)) {
        return std::nullopt;
    }
    const double minimum_x = std::min(first_corner.x, second_corner.x);
    const double maximum_x = std::max(first_corner.x, second_corner.x);
    const double minimum_y = std::min(first_corner.y, second_corner.y);
    const double maximum_y = std::max(first_corner.y, second_corner.y);
    std::vector<std::size_t> candidate;
    candidate.reserve(vertex_screen_positions.size());
    for (std::size_t index = 0U; index < vertex_screen_positions.size(); ++index) {
        const Point position = vertex_screen_positions[index];
        if (!finite(position)) {
            return std::nullopt;
        }
        if (position.x >= minimum_x && position.x <= maximum_x &&
            position.y >= minimum_y && position.y <= maximum_y) {
            candidate.push_back(index);
        }
    }
    return candidate;
}

std::optional<std::vector<std::size_t>> update_ffd_box_selection(
    const std::vector<std::size_t>& current_selection,
    const std::vector<Point>& vertex_screen_positions,
    Point first_corner,
    Point second_corner,
    bool additive) {
    if (!valid_ffd_selection(
            current_selection, vertex_screen_positions.size())) {
        return std::nullopt;
    }
    const auto collected = collect_ffd_vertices_in_box(
        vertex_screen_positions, first_corner, second_corner);
    if (!collected.has_value()) {
        return std::nullopt;
    }
    if (!additive) {
        return collected;
    }
    std::vector<std::size_t> candidate;
    candidate.reserve(current_selection.size() + collected->size());
    std::set_union(
        current_selection.begin(),
        current_selection.end(),
        collected->begin(),
        collected->end(),
        std::back_inserter(candidate));
    return candidate;
}

std::optional<Matrix2> make_ffd_inverse(
    const std::vector<FfdInfluence>& influences) {
    if (influences.empty()) {
        return std::nullopt;
    }
    Matrix2 weighted{0.0, 0.0, 0.0, 0.0};
    double total_weight = 0.0;
    for (const FfdInfluence& influence : influences) {
        if (!finite(influence.bone_world) || !std::isfinite(influence.weight) ||
            influence.weight <= 0.0) {
            return std::nullopt;
        }
        weighted.a += influence.bone_world.a * influence.weight;
        weighted.b += influence.bone_world.b * influence.weight;
        weighted.c += influence.bone_world.c * influence.weight;
        weighted.d += influence.bone_world.d * influence.weight;
        total_weight += influence.weight;
    }
    if (!finite(weighted) || !std::isfinite(total_weight) ||
        std::abs(total_weight - 1.0) > 1e-6) {
        return std::nullopt;
    }
    return inverse_matrix(weighted);
}

std::optional<Point> map_ffd_delta(
    Matrix2 inverse,
    Point world_delta) {
    if (!finite(inverse) || !finite(world_delta)) {
        return std::nullopt;
    }
    const Point local{
        (inverse.a * world_delta.x) + (inverse.b * world_delta.y),
        (inverse.c * world_delta.x) + (inverse.d * world_delta.y)};
    return finite(local) ? std::optional<Point>(local) : std::nullopt;
}

std::optional<std::vector<double>> update_ffd_vertex_offsets(
    const std::vector<double>& start_offsets,
    const std::vector<FfdVertexDelta>& vertex_deltas) {
    if (start_offsets.empty() || (start_offsets.size() % 2U) != 0U ||
        vertex_deltas.empty() ||
        !std::all_of(start_offsets.begin(), start_offsets.end(), [](double value) {
            return std::isfinite(value);
        })) {
        return std::nullopt;
    }
    const std::size_t vertex_count = start_offsets.size() / 2U;
    std::vector<bool> visited(vertex_count, false);
    std::vector<double> candidate = start_offsets;
    for (const FfdVertexDelta& update : vertex_deltas) {
        if (update.vertex_index >= vertex_count ||
            visited[update.vertex_index] || !finite(update.local_delta)) {
            return std::nullopt;
        }
        visited[update.vertex_index] = true;
        const std::size_t component = update.vertex_index * 2U;
        candidate[component] += update.local_delta.x;
        candidate[component + 1U] += update.local_delta.y;
        if (!std::isfinite(candidate[component]) ||
            !std::isfinite(candidate[component + 1U])) {
            return std::nullopt;
        }
    }
    return candidate;
}

std::optional<std::vector<double>> update_ffd_vertex_offsets(
    const std::vector<double>& start_offsets,
    std::size_t vertex_index,
    Point local_delta) {
    return update_ffd_vertex_offsets(
        start_offsets,
        std::vector<FfdVertexDelta>{{vertex_index, local_delta}});
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
    bool ffd_vertex,
    bool entity) {
    if (active_gesture) return PressTarget::ActiveGesture;
    if (weight_brush) return PressTarget::WeightBrush;
    if (translate) return PressTarget::Translate;
    if (rotation) return PressTarget::Rotation;
    if (scale) return PressTarget::Scale;
    if (ffd_vertex) return PressTarget::FfdVertex;
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
