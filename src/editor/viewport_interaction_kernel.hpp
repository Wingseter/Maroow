#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace marrow::editor::viewport_interaction_kernel {

constexpr double kRelativeSingularEpsilon = 1e-8;
constexpr double kRotationPivotSuspendRadius = 2.0;
constexpr double kScaleHandleRadius = 74.0;
constexpr double kFfdVertexHitRadius = 6.0;
constexpr double kFfdDragThreshold = 4.0;

struct Point {
    double x{0.0};
    double y{0.0};
};

struct Matrix2 {
    double a{1.0};
    double b{0.0};
    double c{0.0};
    double d{1.0};
};

struct RotationBasis {
    Point pivot{};
    Matrix2 inverse{};
};

std::optional<RotationBasis> make_rotation_basis(
    Point pivot,
    Matrix2 parent_space);
std::optional<double> rotation_angle(
    const RotationBasis& basis,
    Point pointer);
double unwrap_rotation_delta(double delta);

struct RotationDragState {
    std::optional<double> previous_wrapped_angle;
    double accumulated_rotation{0.0};
    bool angular_reference_suspended{false};
};

enum class RotationUpdateResult : std::uint8_t {
    Suspended,
    Rebased,
    Unchanged,
    Changed,
    Invalid,
};

struct RotationUpdate {
    RotationUpdateResult result{RotationUpdateResult::Invalid};
    double accumulated_rotation{0.0};
};

RotationUpdate update_rotation_drag(
    RotationDragState* state,
    double pivot_distance_squared,
    std::optional<double> wrapped_angle);

struct ScaleBasis {
    Point pivot{};
    Point positive_x_screen_direction{1.0, 0.0};
    Point positive_y_screen_direction{0.0, -1.0};
    Point uniform_screen_direction{0.7071067811865475, -0.7071067811865475};
};

std::optional<ScaleBasis> make_scale_basis(
    Point pivot,
    Matrix2 parent_space,
    double local_rotation_degrees,
    double local_shear_x_degrees,
    double local_shear_y_degrees);

enum class ScaleHandle : std::uint8_t {
    X,
    Y,
    Uniform,
};

struct ScaleMapping {
    Point pivot_screen{};
    Point direction{};
    ScaleHandle handle{ScaleHandle::X};
    double start_scale_x{1.0};
    double start_scale_y{1.0};
    double start_projection_pixels{0.0};
};

struct ScaleCandidate {
    double scale_x{1.0};
    double scale_y{1.0};
};

std::optional<ScaleCandidate> map_scale(
    const ScaleMapping& mapping,
    Point pointer_screen);

struct FfdInfluence {
    Matrix2 bone_world{};
    double weight{0.0};
};

std::optional<std::size_t> nearest_ffd_vertex(
    const std::vector<Point>& vertex_screen_positions,
    Point pointer_screen,
    double hit_radius = kFfdVertexHitRadius);

enum class FfdPointSelectionMode : std::uint8_t {
    Replace,
    Toggle,
};

std::optional<std::vector<std::size_t>> update_ffd_point_selection(
    const std::vector<std::size_t>& current_selection,
    std::size_t vertex_count,
    std::size_t vertex_index,
    FfdPointSelectionMode mode);
std::optional<std::vector<std::size_t>> collect_ffd_vertices_in_box(
    const std::vector<Point>& vertex_screen_positions,
    Point first_corner,
    Point second_corner);
std::optional<std::vector<std::size_t>> update_ffd_box_selection(
    const std::vector<std::size_t>& current_selection,
    const std::vector<Point>& vertex_screen_positions,
    Point first_corner,
    Point second_corner,
    bool additive);

std::optional<Matrix2> make_ffd_inverse(
    const std::vector<FfdInfluence>& influences);
std::optional<Point> map_ffd_delta(
    Matrix2 inverse,
    Point world_delta);

struct FfdVertexDelta {
    std::size_t vertex_index{0U};
    Point local_delta{};
};

std::optional<std::vector<double>> update_ffd_vertex_offsets(
    const std::vector<double>& start_offsets,
    const std::vector<FfdVertexDelta>& vertex_deltas);
std::optional<std::vector<double>> update_ffd_vertex_offsets(
    const std::vector<double>& start_offsets,
    std::size_t vertex_index,
    Point local_delta);

enum class HitPriority : std::uint8_t {
    ConstraintTarget,
    BoneJoint,
    BoneBody,
    SlotHandle,
    AttachmentSurface,
};

struct RankedHit {
    std::size_t source_index{0U};
    HitPriority priority{HitPriority::AttachmentSurface};
    double distance_squared{0.0};
    std::size_t stable_order{0U};
};

std::optional<std::size_t> resolve_hit_index(
    const std::vector<RankedHit>& candidates);

enum class PressTarget : std::uint8_t {
    ActiveGesture,
    WeightBrush,
    Translate,
    Rotation,
    Scale,
    FfdVertex,
    Entity,
    Box,
};

PressTarget resolve_press_target(
    bool active_gesture,
    bool weight_brush,
    bool translate,
    bool rotation,
    bool scale,
    bool ffd_vertex,
    bool entity);

enum class CompletionAction : std::uint8_t {
    Cancel,
    Commit,
};

struct CompletionDecision {
    CompletionAction action{CompletionAction::Cancel};
    bool report_cancelled{false};
    std::size_t history_entries{0U};
};

CompletionDecision completion_decision(
    bool commit_requested,
    bool changed,
    bool context_valid);

} // namespace marrow::editor::viewport_interaction_kernel
