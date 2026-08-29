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
