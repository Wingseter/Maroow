#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "timeline_model.hpp"

namespace marrow::editor::timeline_graph_model {

enum class TrackKind : std::uint8_t {
    Rotate,
    Translate,
    Scale,
    Shear,
    SlotColor,
};

enum class Component : std::uint8_t {
    Angle,
    X,
    Y,
    Red,
    Green,
    Blue,
    Alpha,
};

enum class ProjectionStatus : std::uint8_t {
    Ready,
    UnsupportedTrack,
    MissingSource,
    InvalidData,
};

/** @brief Axis a graph point drag locks onto once it leaves the dead zone. */
enum class DragAxis : std::uint8_t {
    Undecided,
    Time,
    Value,
};

struct ComponentDescriptor {
    Component component{Component::Angle};
    std::string_view label;
};

struct Key {
    timeline_model::KeyRef identity;
    double time_seconds{0.0};
    std::array<double, 4> values{};
    std::size_t value_count{0U};
    marrow::runtime::Interpolation outgoing_easing{};
};

struct Track {
    std::string track_id;
    std::string label;
    TrackKind kind{TrackKind::Rotate};
    std::vector<ComponentDescriptor> components;
    std::vector<Key> keys;
};

struct Projection {
    ProjectionStatus status{ProjectionStatus::UnsupportedTrack};
    std::optional<Track> track;
};

struct PlotRect {
    double min_x{0.0};
    double min_y{0.0};
    double max_x{0.0};
    double max_y{0.0};
};

struct View {
    double view_start_seconds{0.0};
    double pixels_per_second{160.0};
    double value_center{0.0};
    double pixels_per_value{100.0};
};

struct PlotPoint {
    double x{0.0};
    double y{0.0};
};

enum class SegmentKind : std::uint8_t {
    Linear,
    Stepped,
    Cubic,
};

struct Point {
    PlotPoint position;
    timeline_model::KeyRef key;
    Component component{Component::Angle};
    std::size_t component_index{0U};
};

struct Segment {
    SegmentKind kind{SegmentKind::Linear};
    Component component{Component::Angle};
    std::vector<PlotPoint> polyline;
    PlotPoint marker;
};

struct Geometry {
    std::vector<Point> points;
    std::vector<Segment> segments;
    std::optional<double> playhead_x;
};

struct PointHit {
    timeline_model::KeyRef key;
    Component component{Component::Angle};
    std::size_t component_index{0U};
};

/** @brief Which of the two control points of one outgoing segment is meant. */
enum class HandleIndex : std::uint8_t {
    First,
    Second,
};

/**
 * @brief Frozen anchor geometry of one outgoing segment.
 *
 * A handle drag writes only `interpolation`, so the two anchors can never move
 * while it runs. Snapshotting them at the press is what makes the pixel-to-
 * control-point mapping constant for the whole gesture, exactly as the frozen
 * `View` does for MAR-168's point drag.
 */
struct SegmentFrame {
    double start_time_seconds{0.0};
    double end_time_seconds{0.0};
    double start_value{0.0};
    double end_value{0.0};
    double time_span{0.0};   // end_time - start_time, always > kMinimumSegmentSeconds
    double value_span{0.0};  // resolved; never zero, see make_segment_frame
    bool flat_value_span{false};
};

/** @brief Drawable and grabbable handles of one outgoing segment. */
struct HandleGeometry {
    timeline_model::KeyRef key;
    Component component{Component::Angle};
    std::size_t component_index{0U};
    std::size_t key_index{0U};
    SegmentKind kind{SegmentKind::Linear};
    SegmentFrame frame{};
    std::array<double, 4> control_points{};  // seeded for Linear and Stepped
    PlotPoint start_anchor{};
    PlotPoint end_anchor{};
    PlotPoint first_handle{};
    PlotPoint second_handle{};
};

struct HandleHit {
    timeline_model::KeyRef key;
    HandleIndex handle{HandleIndex::First};
};

/**
 * The unique evenly spaced cubic that is exactly identical to Linear: with
 * `cx1 == cy1` and `cx2 == cy2`, `Y(t) == X(t)` and `transform(alpha)` is the
 * identity. The `CubicBezierControlPoints` default `[0, 0, 1, 1]` is also
 * exactly linear but puts both handles on top of the anchors, where they can
 * neither be seen nor grabbed.
 */
inline constexpr std::array<double, 4> kLinearEquivalentControlPoints{
    1.0 / 3.0, 1.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0};
/** Shortest segment whose normalized time axis still has usable extent. */
inline constexpr double kMinimumSegmentSeconds = 1e-6;
/** Below one logical pixel of vertical extent a segment counts as flat. */
inline constexpr double kMinimumSegmentValuePixels = 1.0;
/** Logical pixels that stand in for `cy = 1` on a flat segment. */
inline constexpr double kFlatSegmentHandlePixels = 100.0;

/** @brief Returns the control points a grabbed handle starts from. */
std::array<double, 4> seed_control_points(
    SegmentKind kind,
    const std::array<double, 4>& existing);

/**
 * @brief Freezes the anchors of the segment starting at `key_index`.
 *
 * Returns `std::nullopt` for the last key, an out-of-range component, a
 * non-finite anchor, and a segment shorter than `kMinimumSegmentSeconds`,
 * whose normalized time axis has no extent and therefore no honest fallback.
 * A segment whose two anchors are less than `kMinimumSegmentValuePixels`
 * apart cannot express a handle position on its own value axis, so
 * `kFlatSegmentHandlePixels` of vertical travel is substituted for `cy = 1`.
 * The substituted span is always positive, so "drag up increases cy" holds
 * regardless of the infinitesimal sign of the raw span.
 */
std::optional<SegmentFrame> make_segment_frame(
    const Track& track,
    std::size_t key_index,
    std::size_t component_index,
    const View& view);

/**
 * @brief Builds the drawable and grabbable handles of `active_key`'s segment.
 *
 * Handle points are deliberately not clipped to `rect`; the caller clips
 * drawing and gates hit testing on plot containment.
 */
std::optional<HandleGeometry> build_handle_geometry(
    const Track& track,
    const timeline_model::KeyRef& active_key,
    std::size_t component_index,
    const View& view,
    PlotRect rect);

/** @brief Inclusive square-distance hit test; the nearer handle wins ties. */
std::optional<HandleHit> hit_test_handle(
    const HandleGeometry& geometry,
    double pointer_x,
    double pointer_y,
    double inclusive_radius = 7.0);

/**
 * @brief Maps a pointer to one moved control point, clamping X into [0, 1].
 *
 * `cx` is clamped rather than rejected so a drag past the boundary stops there
 * and the gesture continues; `[0, 1]` is the same invariant the `.marrow` and
 * `.mskl` loaders enforce, and it is what makes the runtime's `X(t) = alpha`
 * inverse well posed. `cy` is never clamped, so finite overshoot survives.
 * Returns `std::nullopt` for any non-finite input or intermediate. The
 * untouched control point is carried through byte-identically.
 */
std::optional<std::array<double, 4>> control_points_from_handle_pointer(
    const SegmentFrame& frame,
    const std::array<double, 4>& current,
    HandleIndex handle,
    const View& view,
    PlotRect rect,
    double pointer_x,
    double pointer_y);

/** @brief Shared pixel/unit mapping used by both rendering and dragging. */
double time_at_x(PlotRect rect, const View& view, double x);
double value_at_y(PlotRect rect, const View& view, double y);
double x_at_time(PlotRect rect, const View& view, double time_seconds);
double y_at_value(PlotRect rect, const View& view, double value);

/** Half-width of the dead-zone box a press must leave before an axis locks. */
constexpr double kDragDeadZonePixels = 4.0;

/**
 * @brief Locks one drag axis by dominant-axis comparison.
 *
 * The dead zone is an axis-aligned square, not a circle: the axis locks once
 * `max(|dx|, |dy|)` reaches `dead_zone_pixels`, so a purely diagonal press
 * travels about 5.7 px of screen distance before locking. Returns `Undecided`
 * for any non-finite input, a non-finite or negative dead zone, and while the
 * pointer stays inside the box. Otherwise `|dx| > |dy|` locks `Time` and every
 * other case locks `Value`, so an exact tie resolves to the value axis.
 */
DragAxis decide_drag_axis(
    double press_x,
    double press_y,
    double pointer_x,
    double pointer_y,
    double dead_zone_pixels = kDragDeadZonePixels);

/** @brief Signed seconds a horizontal drag requests, or nullopt when unusable. */
std::optional<double> drag_time_delta(
    const View& view,
    double press_x,
    double pointer_x);
/** @brief Signed units a vertical drag requests, inverting screen Y. */
std::optional<double> drag_value_delta(
    const View& view,
    double press_y,
    double pointer_y);

bool track_is_supported(const timeline_model::TrackRow& track) noexcept;
Projection project_track(
    const marrow::runtime::AnimationData& animation,
    const timeline_model::TrackRow& track);

std::optional<View> fit_view(
    const Track& track,
    const std::array<bool, 4>& visible,
    PlotRect rect,
    double frames_per_second);
bool zoom_time_at(View* view, PlotRect rect, double cursor_x, double wheel_delta);
bool zoom_value_at(View* view, PlotRect rect, double cursor_y, double wheel_delta);
bool pan_view(View* view, double delta_x, double delta_y);
std::optional<Geometry> build_geometry(
    const Track& track,
    const std::array<bool, 4>& visible,
    const View& view,
    PlotRect rect,
    double playhead_time);
std::optional<double> nice_tick_interval(
    double pixels_per_unit,
    double minimum_logical_spacing);
std::optional<PointHit> hit_test(
    const Geometry& geometry,
    double pointer_x,
    double pointer_y,
    double inclusive_radius = 8.0);

}  // namespace marrow::editor::timeline_graph_model
