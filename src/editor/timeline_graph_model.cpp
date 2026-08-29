#include "timeline_graph_model.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <tuple>
#include <utility>

#include "marrow/runtime/skeleton.hpp"

namespace marrow::editor::timeline_graph_model {
namespace {

bool interpolation_is_finite(const marrow::runtime::Interpolation& interpolation) {
    if (interpolation.kind() != marrow::runtime::InterpolationKind::CubicBezier) {
        return true;
    }
    const auto& cubic = interpolation.cubic_bezier();
    return std::isfinite(cubic.cx1) && std::isfinite(cubic.cy1) &&
        std::isfinite(cubic.cx2) && std::isfinite(cubic.cy2);
}

constexpr double kCubicDeviationPixels = 0.5;
constexpr int kMaximumCubicSubdivisionDepth = 10;
constexpr double kMinimumTimePixelsPerSecond = 0.01;
constexpr double kMaximumTimePixelsPerSecond = 1600.0;
constexpr double kMinimumValuePixelsPerUnit = 1e-9;
constexpr double kMaximumValuePixelsPerUnit = 1e9;

double safe_midpoint(double left, double right) {
    const double left_magnitude = std::abs(left);
    const double right_magnitude = std::abs(right);
    const double half_maximum = std::numeric_limits<double>::max() * 0.5;
    if (left_magnitude <= half_maximum && right_magnitude <= half_maximum) {
        return (left + right) * 0.5;
    }
    const double twice_minimum_normal = std::numeric_limits<double>::min() * 2.0;
    if (left_magnitude < twice_minimum_normal) return left + right * 0.5;
    if (right_magnitude < twice_minimum_normal) return left * 0.5 + right;
    return left * 0.5 + right * 0.5;
}

double finite_lerp(double start, double end, double alpha) {
    if (start == end) return start;
    const double scale = std::max(std::abs(start), std::abs(end));
    if (scale == 0.0) return 0.0;
    const double normalized_start = start / scale;
    const double normalized_end = end / scale;
    const double normalized_value =
        normalized_start * (1.0 - alpha) + normalized_end * alpha;
    return normalized_value * scale;
}

bool is_finite_rect(PlotRect rect) {
    return std::isfinite(rect.min_x) && std::isfinite(rect.min_y) &&
        std::isfinite(rect.max_x) && std::isfinite(rect.max_y) &&
        rect.min_x <= rect.max_x && rect.min_y <= rect.max_y;
}

bool is_finite_view(const View& view) {
    return std::isfinite(view.view_start_seconds) &&
        std::isfinite(view.pixels_per_second) && view.pixels_per_second > 0.0 &&
        std::isfinite(view.value_center) &&
        std::isfinite(view.pixels_per_value) && view.pixels_per_value > 0.0;
}

bool contains(PlotRect rect, PlotPoint point) {
    return point.x >= rect.min_x && point.x <= rect.max_x &&
        point.y >= rect.min_y && point.y <= rect.max_y;
}

double plot_midpoint_y(PlotRect rect) {
    return safe_midpoint(rect.min_y, rect.max_y);
}

std::optional<PlotPoint> plot_point(
    const View& view,
    PlotRect rect,
    double time_seconds,
    double value) {
    if (!std::isfinite(time_seconds) || !std::isfinite(value)) return std::nullopt;
    const double x = rect.min_x + (time_seconds - view.view_start_seconds) *
        view.pixels_per_second;
    const double y = plot_midpoint_y(rect) -
        (value - view.value_center) * view.pixels_per_value;
    if (!std::isfinite(x) || !std::isfinite(y)) return std::nullopt;
    return PlotPoint{x, y};
}

bool valid_geometry_track(const Track& track, const std::array<bool, 4>& visible) {
    if (track.components.empty() || track.components.size() > visible.size()) return false;
    for (const auto& key : track.keys) {
        if (!std::isfinite(key.time_seconds) || key.value_count != track.components.size() ||
            !interpolation_is_finite(key.outgoing_easing)) {
            return false;
        }
        for (std::size_t component = 0U; component < track.components.size(); ++component) {
            if (visible[component] && !std::isfinite(key.values[component])) return false;
        }
    }
    return true;
}

std::optional<double> segment_value(
    const Key& start,
    const Key& end,
    std::size_t component_index,
    double normalized_time) {
    const double transformed_alpha = start.outgoing_easing.transform(normalized_time);
    if (!std::isfinite(transformed_alpha)) return std::nullopt;
    const double value = finite_lerp(
        start.values[component_index], end.values[component_index], transformed_alpha);
    if (!std::isfinite(value)) return std::nullopt;
    return value;
}

SegmentKind segment_kind(const marrow::runtime::Interpolation& easing) {
    switch (easing.kind()) {
    case marrow::runtime::InterpolationKind::Linear:
        return SegmentKind::Linear;
    case marrow::runtime::InterpolationKind::Stepped:
        return SegmentKind::Stepped;
    case marrow::runtime::InterpolationKind::CubicBezier:
        return SegmentKind::Cubic;
    }
    return SegmentKind::Linear;
}

bool append_cubic_polyline(
    std::vector<PlotPoint>* polyline,
    const Key& start,
    const Key& end,
    std::size_t component_index,
    const View& view,
    PlotRect rect,
    double lower_u,
    PlotPoint lower_point,
    double upper_u,
    PlotPoint upper_point,
    int depth) {
    const double middle_u = safe_midpoint(lower_u, upper_u);
    const auto middle_value = segment_value(start, end, component_index, middle_u);
    if (!middle_value.has_value()) return false;
    const double lower_time = finite_lerp(start.time_seconds, end.time_seconds, lower_u);
    const double upper_time = finite_lerp(start.time_seconds, end.time_seconds, upper_u);
    const double middle_time = safe_midpoint(lower_time, upper_time);
    const auto middle_point = plot_point(view, rect, middle_time, *middle_value);
    if (!middle_point.has_value()) return false;
    const double straight_x = safe_midpoint(lower_point.x, upper_point.x);
    const double straight_y = safe_midpoint(lower_point.y, upper_point.y);
    const double deviation = std::hypot(middle_point->x - straight_x, middle_point->y - straight_y);
    if (!std::isfinite(deviation)) return false;
    // A symmetric cubic can agree with its chord at u=0.5 while bending on
    // both halves, so always seed the first bisection before adaptive tests.
    if ((deviation <= kCubicDeviationPixels && depth > 0) ||
        depth >= kMaximumCubicSubdivisionDepth) {
        polyline->push_back(upper_point);
        return true;
    }
    return append_cubic_polyline(
               polyline,
               start,
               end,
               component_index,
               view,
               rect,
               lower_u,
               lower_point,
               middle_u,
               *middle_point,
               depth + 1) &&
        append_cubic_polyline(
            polyline,
            start,
            end,
            component_index,
            view,
            rect,
            middle_u,
            *middle_point,
            upper_u,
            upper_point,
            depth + 1);
}

struct Bounds {
    double minimum{0.0};
    double maximum{0.0};
};

std::optional<View> make_fit_view(
    Bounds time_bounds,
    Bounds value_bounds,
    PlotRect rect,
    double frames_per_second) {
    const double frame_span = 1.0 / frames_per_second;
    const double raw_time_span = time_bounds.maximum - time_bounds.minimum;
    const double time_span = std::max(raw_time_span, frame_span);
    const double time_center = safe_midpoint(time_bounds.minimum, time_bounds.maximum);
    const double padded_time_span = time_span * 1.1;
    const double time_start = time_center - padded_time_span * 0.5;
    const double time_pixels = rect.max_x - rect.min_x;
    const double pixels_per_second = std::clamp(
        time_pixels / padded_time_span,
        kMinimumTimePixelsPerSecond,
        kMaximumTimePixelsPerSecond);

    const double raw_value_span = value_bounds.maximum - value_bounds.minimum;
    const double value_padding = raw_value_span == 0.0
        ? std::max(std::abs(value_bounds.minimum) * 0.1, 1e-3)
        : raw_value_span * 0.1;
    const double padded_value_span = raw_value_span + value_padding * 2.0;
    const double value_center = safe_midpoint(value_bounds.minimum, value_bounds.maximum);
    const double value_pixels = rect.max_y - rect.min_y;
    const double pixels_per_value = value_pixels / padded_value_span;
    View view{time_start, pixels_per_second, value_center, pixels_per_value};
    if (!is_finite_view(view)) return std::nullopt;
    return view;
}

bool expand_bounds_from_geometry(
    const Geometry& geometry,
    const View& view,
    PlotRect rect,
    Bounds* bounds) {
    bool changed = false;
    for (const Segment& segment : geometry.segments) {
        for (const PlotPoint& point : segment.polyline) {
            const double value = view.value_center +
                (plot_midpoint_y(rect) - point.y) / view.pixels_per_value;
            if (!std::isfinite(value)) continue;
            if (value < bounds->minimum) {
                bounds->minimum = value;
                changed = true;
            }
            if (value > bounds->maximum) {
                bounds->maximum = value;
                changed = true;
            }
        }
    }
    return changed;
}

Projection invalid_projection() {
    return {ProjectionStatus::InvalidData, std::nullopt};
}

template <typename Keyframe, typename PopulateValues>
Projection project_keyframes(
    const timeline_model::TrackRow& row,
    TrackKind kind,
    std::vector<ComponentDescriptor> components,
    const std::vector<Keyframe>& source_keys,
    PopulateValues&& populate_values) {
    if (source_keys.size() != row.key_times.size()) {
        return invalid_projection();
    }

    Track result;
    result.track_id = row.id;
    result.label = row.label;
    result.kind = kind;
    result.components = std::move(components);
    result.keys.reserve(source_keys.size());

    for (std::size_t index = 0U; index < source_keys.size(); ++index) {
        const Keyframe& source = source_keys[index];
        const double source_time = static_cast<double>(source.time);
        if (!std::isfinite(source_time) || !std::isfinite(row.key_times[index]) ||
            timeline_model::time_identity(source_time) !=
                timeline_model::key_ref(row, index).time_microseconds ||
            !interpolation_is_finite(source.interpolation)) {
            return invalid_projection();
        }

        Key key;
        key.identity = timeline_model::key_ref(row, index);
        key.time_seconds = source_time;
        key.value_count = result.components.size();
        key.outgoing_easing = source.interpolation;
        if (!populate_values(source, &key.values)) {
            return invalid_projection();
        }
        result.keys.push_back(std::move(key));
    }
    return {ProjectionStatus::Ready, std::move(result)};
}

bool set_vector_values(
    const marrow::runtime::VectorKeyframe& source,
    std::array<double, 4>* values) {
    const double x = static_cast<double>(source.x);
    const double y = static_cast<double>(source.y);
    if (!std::isfinite(x) || !std::isfinite(y)) return false;
    (*values)[0] = x;
    (*values)[1] = y;
    return true;
}

bool set_color_values(
    const marrow::runtime::ColorKeyframe& source,
    std::array<double, 4>* values) {
    const double red = static_cast<double>(source.color.r);
    const double green = static_cast<double>(source.color.g);
    const double blue = static_cast<double>(source.color.b);
    const double alpha = static_cast<double>(source.color.a);
    if (!std::isfinite(red) || !std::isfinite(green) ||
        !std::isfinite(blue) || !std::isfinite(alpha)) {
        return false;
    }
    *values = {red, green, blue, alpha};
    return true;
}

} // namespace

double time_at_x(PlotRect rect, const View& view, double x) {
    return view.view_start_seconds + (x - rect.min_x) / view.pixels_per_second;
}

double value_at_y(PlotRect rect, const View& view, double y) {
    return view.value_center + (plot_midpoint_y(rect) - y) / view.pixels_per_value;
}

double x_at_time(PlotRect rect, const View& view, double time_seconds) {
    return rect.min_x +
        (time_seconds - view.view_start_seconds) * view.pixels_per_second;
}

double y_at_value(PlotRect rect, const View& view, double value) {
    return plot_midpoint_y(rect) - (value - view.value_center) * view.pixels_per_value;
}

DragAxis decide_drag_axis(
    double press_x,
    double press_y,
    double pointer_x,
    double pointer_y,
    double dead_zone_pixels) {
    if (!std::isfinite(press_x) || !std::isfinite(press_y) ||
        !std::isfinite(pointer_x) || !std::isfinite(pointer_y) ||
        !std::isfinite(dead_zone_pixels) || dead_zone_pixels < 0.0) {
        return DragAxis::Undecided;
    }
    const double dx = pointer_x - press_x;
    const double dy = pointer_y - press_y;
    if (!std::isfinite(dx) || !std::isfinite(dy)) return DragAxis::Undecided;
    if (std::max(std::abs(dx), std::abs(dy)) < dead_zone_pixels) {
        return DragAxis::Undecided;
    }
    // An exact tie resolves to Value: the dopesheet already edits time, so the
    // graph keeps the axis only it can author.
    return std::abs(dx) > std::abs(dy) ? DragAxis::Time : DragAxis::Value;
}

std::optional<double> drag_time_delta(
    const View& view,
    double press_x,
    double pointer_x) {
    if (!std::isfinite(press_x) || !std::isfinite(pointer_x) ||
        !std::isfinite(view.pixels_per_second) || view.pixels_per_second <= 0.0) {
        return std::nullopt;
    }
    const double delta = (pointer_x - press_x) / view.pixels_per_second;
    if (!std::isfinite(delta)) return std::nullopt;
    return delta;
}

std::optional<double> drag_value_delta(
    const View& view,
    double press_y,
    double pointer_y) {
    if (!std::isfinite(press_y) || !std::isfinite(pointer_y) ||
        !std::isfinite(view.pixels_per_value) || view.pixels_per_value <= 0.0) {
        return std::nullopt;
    }
    // ImGui y grows downward while values grow upward.
    const double delta = (press_y - pointer_y) / view.pixels_per_value;
    if (!std::isfinite(delta)) return std::nullopt;
    return delta;
}

bool track_is_supported(const timeline_model::TrackRow& track) noexcept {
    switch (track.kind) {
    case timeline_model::TimelineTrackKind::Rotate:
    case timeline_model::TimelineTrackKind::Translate:
    case timeline_model::TimelineTrackKind::Scale:
    case timeline_model::TimelineTrackKind::Shear:
    case timeline_model::TimelineTrackKind::SlotColor:
        return true;
    case timeline_model::TimelineTrackKind::Unknown:
    case timeline_model::TimelineTrackKind::Inherit:
    case timeline_model::TimelineTrackKind::SlotAttachment:
    case timeline_model::TimelineTrackKind::Deform:
    case timeline_model::TimelineTrackKind::DrawOrder:
    case timeline_model::TimelineTrackKind::Event:
        return false;
    }
    return false;
}

Projection project_track(
    const marrow::runtime::AnimationData& animation,
    const timeline_model::TrackRow& track) {
    if (!track_is_supported(track)) {
        return {ProjectionStatus::UnsupportedTrack, std::nullopt};
    }

    switch (track.kind) {
    case timeline_model::TimelineTrackKind::Rotate: {
        if (!track.bone_index.has_value()) return {ProjectionStatus::MissingSource, std::nullopt};
        const auto* source = animation.find_rotate_timeline(*track.bone_index);
        if (source == nullptr) return {ProjectionStatus::MissingSource, std::nullopt};
        const double setup_rotation = static_cast<double>(source->setup_rotation);
        if (!std::isfinite(setup_rotation)) return invalid_projection();
        return project_keyframes(
            track,
            TrackKind::Rotate,
            {{Component::Angle, "Angle"}},
            source->keyframes,
            [setup_rotation](
                const marrow::runtime::RotateKeyframe& keyframe,
                std::array<double, 4>* values) {
                const double angle = static_cast<double>(keyframe.angle);
                const double absolute_angle = setup_rotation + angle;
                if (!std::isfinite(angle) || !std::isfinite(absolute_angle)) return false;
                (*values)[0] = absolute_angle;
                return true;
            });
    }
    case timeline_model::TimelineTrackKind::Translate: {
        if (!track.bone_index.has_value()) return {ProjectionStatus::MissingSource, std::nullopt};
        const auto* source = animation.find_translate_timeline(*track.bone_index);
        if (source == nullptr) return {ProjectionStatus::MissingSource, std::nullopt};
        return project_keyframes(
            track,
            TrackKind::Translate,
            {{Component::X, "X"}, {Component::Y, "Y"}},
            source->keyframes,
            set_vector_values);
    }
    case timeline_model::TimelineTrackKind::Scale: {
        if (!track.bone_index.has_value()) return {ProjectionStatus::MissingSource, std::nullopt};
        const auto* source = animation.find_scale_timeline(*track.bone_index);
        if (source == nullptr) return {ProjectionStatus::MissingSource, std::nullopt};
        return project_keyframes(
            track,
            TrackKind::Scale,
            {{Component::X, "X"}, {Component::Y, "Y"}},
            source->keyframes,
            set_vector_values);
    }
    case timeline_model::TimelineTrackKind::Shear: {
        if (!track.bone_index.has_value()) return {ProjectionStatus::MissingSource, std::nullopt};
        const auto* source = animation.find_shear_timeline(*track.bone_index);
        if (source == nullptr) return {ProjectionStatus::MissingSource, std::nullopt};
        return project_keyframes(
            track,
            TrackKind::Shear,
            {{Component::X, "X"}, {Component::Y, "Y"}},
            source->keyframes,
            set_vector_values);
    }
    case timeline_model::TimelineTrackKind::SlotColor: {
        if (!track.slot_index.has_value()) return {ProjectionStatus::MissingSource, std::nullopt};
        const auto* source = animation.find_color_timeline(*track.slot_index);
        if (source == nullptr) return {ProjectionStatus::MissingSource, std::nullopt};
        return project_keyframes(
            track,
            TrackKind::SlotColor,
            {{Component::Red, "Red"}, {Component::Green, "Green"},
             {Component::Blue, "Blue"}, {Component::Alpha, "Alpha"}},
            source->keyframes,
            set_color_values);
    }
    case timeline_model::TimelineTrackKind::Unknown:
    case timeline_model::TimelineTrackKind::Inherit:
    case timeline_model::TimelineTrackKind::SlotAttachment:
    case timeline_model::TimelineTrackKind::Deform:
    case timeline_model::TimelineTrackKind::DrawOrder:
    case timeline_model::TimelineTrackKind::Event:
        return {ProjectionStatus::UnsupportedTrack, std::nullopt};
    }
    return {ProjectionStatus::UnsupportedTrack, std::nullopt};
}

std::optional<View> fit_view(
    const Track& track,
    const std::array<bool, 4>& visible,
    PlotRect rect,
    double frames_per_second) {
    if (!is_finite_rect(rect) || rect.min_x == rect.max_x || rect.min_y == rect.max_y ||
        !std::isfinite(frames_per_second) || frames_per_second <= 0.0 ||
        !valid_geometry_track(track, visible) || track.keys.empty()) {
        return std::nullopt;
    }

    bool has_visible_component = false;
    Bounds value_bounds{};
    for (std::size_t component = 0U; component < track.components.size(); ++component) {
        if (!visible[component]) continue;
        for (const Key& key : track.keys) {
            const double value = key.values[component];
            if (!has_visible_component) {
                value_bounds = {value, value};
                has_visible_component = true;
            } else {
                value_bounds.minimum = std::min(value_bounds.minimum, value);
                value_bounds.maximum = std::max(value_bounds.maximum, value);
            }
        }
    }
    if (!has_visible_component) return std::nullopt;
    if (track.kind == TrackKind::SlotColor) {
        value_bounds.minimum = std::min(value_bounds.minimum, 0.0);
        value_bounds.maximum = std::max(value_bounds.maximum, 1.0);
    }

    Bounds time_bounds{track.keys.front().time_seconds, track.keys.front().time_seconds};
    for (const Key& key : track.keys) {
        time_bounds.minimum = std::min(time_bounds.minimum, key.time_seconds);
        time_bounds.maximum = std::max(time_bounds.maximum, key.time_seconds);
    }

    auto provisional = make_fit_view(time_bounds, value_bounds, rect, frames_per_second);
    if (!provisional.has_value()) return std::nullopt;
    const auto provisional_geometry = build_geometry(
        track, visible, *provisional, rect, time_bounds.minimum);
    if (!provisional_geometry.has_value()) return std::nullopt;
    expand_bounds_from_geometry(*provisional_geometry, *provisional, rect, &value_bounds);

    auto final_view = make_fit_view(time_bounds, value_bounds, rect, frames_per_second);
    if (!final_view.has_value()) return std::nullopt;
    const auto final_geometry = build_geometry(track, visible, *final_view, rect, time_bounds.minimum);
    if (!final_geometry.has_value()) return std::nullopt;
    if (expand_bounds_from_geometry(*final_geometry, *final_view, rect, &value_bounds)) {
        final_view = make_fit_view(time_bounds, value_bounds, rect, frames_per_second);
    }
    return final_view;
}

bool zoom_time_at(View* view, PlotRect rect, double cursor_x, double wheel_delta) {
    if (view == nullptr || !is_finite_view(*view) || !is_finite_rect(rect) ||
        !std::isfinite(cursor_x) || !std::isfinite(wheel_delta)) {
        return false;
    }
    const double multiplier = std::pow(1.15, wheel_delta);
    if (!std::isfinite(multiplier)) return false;
    const double candidate_scale = std::clamp(
        view->pixels_per_second * multiplier,
        kMinimumTimePixelsPerSecond,
        kMaximumTimePixelsPerSecond);
    const double anchored_time = view->view_start_seconds +
        (cursor_x - rect.min_x) / view->pixels_per_second;
    const double candidate_start = anchored_time - (cursor_x - rect.min_x) / candidate_scale;
    if (!std::isfinite(candidate_scale) || !std::isfinite(anchored_time) ||
        !std::isfinite(candidate_start)) {
        return false;
    }
    view->pixels_per_second = candidate_scale;
    view->view_start_seconds = candidate_start;
    return true;
}

bool zoom_value_at(View* view, PlotRect rect, double cursor_y, double wheel_delta) {
    if (view == nullptr || !is_finite_view(*view) || !is_finite_rect(rect) ||
        !std::isfinite(cursor_y) || !std::isfinite(wheel_delta)) {
        return false;
    }
    const double log_scale = std::log(view->pixels_per_value);
    const double log_delta = wheel_delta * std::log(1.15);
    if (!std::isfinite(log_scale) || !std::isfinite(log_delta)) return false;
    const double candidate_log_scale = log_scale + log_delta;
    if (!std::isfinite(candidate_log_scale)) return false;

    const double minimum_log_scale = std::log(kMinimumValuePixelsPerUnit);
    const double maximum_log_scale = std::log(kMaximumValuePixelsPerUnit);
    double candidate_scale = 0.0;
    if (candidate_log_scale <= minimum_log_scale) {
        candidate_scale = kMinimumValuePixelsPerUnit;
    } else if (candidate_log_scale >= maximum_log_scale) {
        candidate_scale = kMaximumValuePixelsPerUnit;
    } else {
        candidate_scale = std::clamp(
            std::exp(candidate_log_scale),
            kMinimumValuePixelsPerUnit,
            kMaximumValuePixelsPerUnit);
    }
    if (!std::isfinite(candidate_scale) || candidate_scale <= 0.0) return false;
    if (candidate_scale == view->pixels_per_value) return true;

    const double cursor_offset = plot_midpoint_y(rect) - cursor_y;
    if (!std::isfinite(cursor_offset)) return false;
    const double anchored_value = view->value_center +
        cursor_offset / view->pixels_per_value;
    const double candidate_center = anchored_value -
        cursor_offset / candidate_scale;
    if (!std::isfinite(anchored_value) || !std::isfinite(candidate_center)) {
        return false;
    }
    view->pixels_per_value = candidate_scale;
    view->value_center = candidate_center;
    return true;
}

bool pan_view(View* view, double delta_x, double delta_y) {
    if (view == nullptr || !is_finite_view(*view) || !std::isfinite(delta_x) ||
        !std::isfinite(delta_y)) {
        return false;
    }
    const double candidate_start = view->view_start_seconds - delta_x / view->pixels_per_second;
    const double candidate_center = view->value_center + delta_y / view->pixels_per_value;
    if (!std::isfinite(candidate_start) || !std::isfinite(candidate_center)) return false;
    view->view_start_seconds = candidate_start;
    view->value_center = candidate_center;
    return true;
}

std::optional<double> nice_tick_interval(
    double pixels_per_unit,
    double minimum_logical_spacing) {
    if (!std::isfinite(pixels_per_unit) || pixels_per_unit <= 0.0 ||
        !std::isfinite(minimum_logical_spacing) || minimum_logical_spacing <= 0.0) {
        return std::nullopt;
    }
    const double target = minimum_logical_spacing / pixels_per_unit;
    const double magnitude = std::pow(10.0, std::floor(std::log10(target)));
    if (!std::isfinite(target) || target <= 0.0 || !std::isfinite(magnitude) || magnitude <= 0.0) {
        return std::nullopt;
    }
    for (const double factor : {1.0, 2.0, 5.0, 10.0}) {
        const double interval = factor * magnitude;
        if (std::isfinite(interval) && interval >= target) return interval;
    }
    return std::nullopt;
}

std::optional<PointHit> hit_test(
    const Geometry& geometry,
    double pointer_x,
    double pointer_y,
    double inclusive_radius) {
    if (!std::isfinite(pointer_x) || !std::isfinite(pointer_y) ||
        !std::isfinite(inclusive_radius) || inclusive_radius < 0.0) {
        return std::nullopt;
    }
    const double radius_squared = inclusive_radius * inclusive_radius;
    if (!std::isfinite(radius_squared)) return std::nullopt;

    const Point* winner = nullptr;
    double winner_distance_squared = std::numeric_limits<double>::infinity();
    for (const Point& point : geometry.points) {
        if (!std::isfinite(point.position.x) || !std::isfinite(point.position.y)) continue;
        const double delta_x = point.position.x - pointer_x;
        const double delta_y = point.position.y - pointer_y;
        const double distance_squared = delta_x * delta_x + delta_y * delta_y;
        if (!std::isfinite(distance_squared) || distance_squared > radius_squared) continue;
        if (winner == nullptr || distance_squared < winner_distance_squared) {
            winner = &point;
            winner_distance_squared = distance_squared;
            continue;
        }
        if (distance_squared != winner_distance_squared) continue;
        const auto point_order = std::tie(
            point.key.time_microseconds,
            point.key.same_time_ordinal,
            point.component,
            point.component_index);
        const auto winner_order = std::tie(
            winner->key.time_microseconds,
            winner->key.same_time_ordinal,
            winner->component,
            winner->component_index);
        if (point_order < winner_order) winner = &point;
    }
    if (winner == nullptr) return std::nullopt;
    return PointHit{winner->key, winner->component, winner->component_index};
}

std::optional<Geometry> build_geometry(
    const Track& track,
    const std::array<bool, 4>& visible,
    const View& view,
    PlotRect rect,
    double playhead_time) {
    if (!is_finite_rect(rect) || !is_finite_view(view) || !std::isfinite(playhead_time) ||
        !valid_geometry_track(track, visible)) {
        return std::nullopt;
    }

    Geometry result;
    const double playhead_x = rect.min_x +
        (playhead_time - view.view_start_seconds) * view.pixels_per_second;
    if (!std::isfinite(playhead_x)) return std::nullopt;
    if (playhead_x >= rect.min_x && playhead_x <= rect.max_x) {
        result.playhead_x = playhead_x;
    }

    for (std::size_t key_index = 0U; key_index < track.keys.size(); ++key_index) {
        const Key& key = track.keys[key_index];
        for (std::size_t component_index = 0U;
             component_index < track.components.size();
             ++component_index) {
            if (!visible[component_index]) continue;
            const auto position = plot_point(
                view, rect, key.time_seconds, key.values[component_index]);
            if (!position.has_value()) return std::nullopt;
            if (contains(rect, *position)) {
                result.points.push_back(
                    {*position,
                     key.identity,
                     track.components[component_index].component,
                     component_index});
            }
        }
        if (key_index + 1U >= track.keys.size()) continue;

        const Key& next_key = track.keys[key_index + 1U];
        for (std::size_t component_index = 0U;
             component_index < track.components.size();
             ++component_index) {
            if (!visible[component_index]) continue;
            const auto start = plot_point(
                view, rect, key.time_seconds, key.values[component_index]);
            const auto end = plot_point(
                view, rect, next_key.time_seconds, next_key.values[component_index]);
            if (!start.has_value() || !end.has_value()) return std::nullopt;

            Segment segment;
            segment.kind = segment_kind(key.outgoing_easing);
            segment.component = track.components[component_index].component;
            switch (segment.kind) {
            case SegmentKind::Linear: {
                segment.polyline = {*start, *end};
                const auto marker = plot_point(
                    view,
                    rect,
                    safe_midpoint(key.time_seconds, next_key.time_seconds),
                    safe_midpoint(
                        key.values[component_index], next_key.values[component_index]));
                if (!marker.has_value()) return std::nullopt;
                segment.marker = *marker;
                break;
            }
            case SegmentKind::Stepped: {
                const auto elbow = plot_point(
                    view, rect, next_key.time_seconds, key.values[component_index]);
                if (!elbow.has_value()) return std::nullopt;
                segment.polyline = {*start, *elbow, *end};
                segment.marker = *elbow;
                break;
            }
            case SegmentKind::Cubic: {
                segment.polyline.push_back(*start);
                if (!append_cubic_polyline(
                        &segment.polyline,
                        key,
                        next_key,
                        component_index,
                        view,
                        rect,
                        0.0,
                        *start,
                        1.0,
                        *end,
                        0)) {
                    return std::nullopt;
                }
                const auto marker_value = segment_value(key, next_key, component_index, 0.5);
                if (!marker_value.has_value()) return std::nullopt;
                const auto marker = plot_point(
                    view,
                    rect,
                    safe_midpoint(key.time_seconds, next_key.time_seconds),
                    *marker_value);
                if (!marker.has_value()) return std::nullopt;
                segment.marker = *marker;
                break;
            }
            }
            result.segments.push_back(std::move(segment));
        }
    }
    return result;
}

std::array<double, 4> seed_control_points(
    SegmentKind kind,
    const std::array<double, 4>& existing) {
    return kind == SegmentKind::Cubic ? existing : kLinearEquivalentControlPoints;
}

std::optional<SegmentFrame> make_segment_frame(
    const Track& track,
    std::size_t key_index,
    std::size_t component_index,
    const View& view) {
    if (!is_finite_view(view)) return std::nullopt;
    // The last key owns no outgoing segment, so it anchors no handles.
    if (key_index + 1U >= track.keys.size()) return std::nullopt;
    if (component_index >= track.components.size()) return std::nullopt;
    const Key& start = track.keys[key_index];
    const Key& end = track.keys[key_index + 1U];
    if (component_index >= start.value_count || component_index >= end.value_count) {
        return std::nullopt;
    }

    SegmentFrame frame;
    frame.start_time_seconds = start.time_seconds;
    frame.end_time_seconds = end.time_seconds;
    frame.start_value = start.values[component_index];
    frame.end_value = end.values[component_index];
    if (!std::isfinite(frame.start_time_seconds) ||
        !std::isfinite(frame.end_time_seconds) ||
        !std::isfinite(frame.start_value) || !std::isfinite(frame.end_value)) {
        return std::nullopt;
    }

    frame.time_span = frame.end_time_seconds - frame.start_time_seconds;
    // A zero-duration segment has no fallback that means anything: the runtime
    // never evaluates its curve, and cx would be a ratio over a zero span.
    if (!std::isfinite(frame.time_span) || !(frame.time_span > kMinimumSegmentSeconds)) {
        return std::nullopt;
    }

    const double raw_span = frame.end_value - frame.start_value;
    // View-relative rather than exact equality: the failure mode is numerical
    // conditioning, not equality. Two anchors less than one logical pixel apart
    // cannot express a handle position on this component's own value axis.
    frame.flat_value_span = !std::isfinite(raw_span) ||
        std::abs(raw_span) * view.pixels_per_value < kMinimumSegmentValuePixels;
    frame.value_span = frame.flat_value_span
        ? kFlatSegmentHandlePixels / view.pixels_per_value
        : raw_span;
    if (!std::isfinite(frame.value_span) || frame.value_span == 0.0) {
        return std::nullopt;
    }
    return frame;
}

std::optional<HandleGeometry> build_handle_geometry(
    const Track& track,
    const timeline_model::KeyRef& active_key,
    std::size_t component_index,
    const View& view,
    PlotRect rect) {
    if (!is_finite_rect(rect) || !is_finite_view(view)) return std::nullopt;
    const auto found = std::find_if(
        track.keys.begin(),
        track.keys.end(),
        [&](const Key& candidate) { return candidate.identity == active_key; });
    if (found == track.keys.end()) return std::nullopt;
    const auto key_index =
        static_cast<std::size_t>(std::distance(track.keys.begin(), found));
    const auto frame = make_segment_frame(track, key_index, component_index, view);
    if (!frame.has_value()) return std::nullopt;
    if (!interpolation_is_finite(found->outgoing_easing)) return std::nullopt;

    HandleGeometry geometry;
    geometry.key = active_key;
    geometry.component = track.components[component_index].component;
    geometry.component_index = component_index;
    geometry.key_index = key_index;
    geometry.kind = segment_kind(found->outgoing_easing);
    geometry.frame = *frame;

    std::array<double, 4> stored{0.0, 0.0, 1.0, 1.0};
    if (geometry.kind == SegmentKind::Cubic) {
        const auto& points = found->outgoing_easing.cubic_bezier();
        stored = {
            static_cast<double>(points.cx1),
            static_cast<double>(points.cy1),
            static_cast<double>(points.cx2),
            static_cast<double>(points.cy2)};
    }
    geometry.control_points = seed_control_points(geometry.kind, stored);
    for (const double control_point : geometry.control_points) {
        if (!std::isfinite(control_point)) return std::nullopt;
    }

    const auto start_anchor =
        plot_point(view, rect, frame->start_time_seconds, frame->start_value);
    const auto end_anchor =
        plot_point(view, rect, frame->end_time_seconds, frame->end_value);
    const auto first_handle = plot_point(
        view,
        rect,
        frame->start_time_seconds + geometry.control_points[0] * frame->time_span,
        frame->start_value + geometry.control_points[1] * frame->value_span);
    const auto second_handle = plot_point(
        view,
        rect,
        frame->start_time_seconds + geometry.control_points[2] * frame->time_span,
        frame->start_value + geometry.control_points[3] * frame->value_span);
    if (!start_anchor.has_value() || !end_anchor.has_value() ||
        !first_handle.has_value() || !second_handle.has_value()) {
        return std::nullopt;
    }
    geometry.start_anchor = *start_anchor;
    geometry.end_anchor = *end_anchor;
    geometry.first_handle = *first_handle;
    geometry.second_handle = *second_handle;
    return geometry;
}

std::optional<HandleHit> hit_test_handle(
    const HandleGeometry& geometry,
    double pointer_x,
    double pointer_y,
    double inclusive_radius) {
    if (!std::isfinite(pointer_x) || !std::isfinite(pointer_y) ||
        !std::isfinite(inclusive_radius) || inclusive_radius < 0.0) {
        return std::nullopt;
    }
    const double radius_squared = inclusive_radius * inclusive_radius;
    if (!std::isfinite(radius_squared)) return std::nullopt;

    const std::array<PlotPoint, 2> handles{
        geometry.first_handle, geometry.second_handle};
    std::optional<HandleIndex> winner;
    double winner_distance_squared = std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < handles.size(); ++index) {
        const PlotPoint& handle = handles[index];
        if (!std::isfinite(handle.x) || !std::isfinite(handle.y)) continue;
        const double delta_x = handle.x - pointer_x;
        const double delta_y = handle.y - pointer_y;
        const double distance_squared = delta_x * delta_x + delta_y * delta_y;
        if (!std::isfinite(distance_squared) || distance_squared > radius_squared) {
            continue;
        }
        // Strictly nearer wins, so an exact tie keeps the first handle.
        if (!winner.has_value() || distance_squared < winner_distance_squared) {
            winner = index == 0U ? HandleIndex::First : HandleIndex::Second;
            winner_distance_squared = distance_squared;
        }
    }
    if (!winner.has_value()) return std::nullopt;
    return HandleHit{geometry.key, *winner};
}

std::optional<std::array<double, 4>> control_points_from_handle_pointer(
    const SegmentFrame& frame,
    const std::array<double, 4>& current,
    HandleIndex handle,
    const View& view,
    PlotRect rect,
    double pointer_x,
    double pointer_y) {
    if (!is_finite_rect(rect) || !is_finite_view(view) ||
        !std::isfinite(pointer_x) || !std::isfinite(pointer_y)) {
        return std::nullopt;
    }
    if (!std::isfinite(frame.start_time_seconds) ||
        !std::isfinite(frame.start_value) || !std::isfinite(frame.time_span) ||
        !std::isfinite(frame.value_span) ||
        !(frame.time_span > kMinimumSegmentSeconds) || frame.value_span == 0.0) {
        return std::nullopt;
    }
    for (const double control_point : current) {
        if (!std::isfinite(control_point)) return std::nullopt;
    }

    const double time_seconds = time_at_x(rect, view, pointer_x);
    const double value = value_at_y(rect, view, pointer_y);
    if (!std::isfinite(time_seconds) || !std::isfinite(value)) return std::nullopt;
    const double cx_raw = (time_seconds - frame.start_time_seconds) / frame.time_span;
    const double cy = (value - frame.start_value) / frame.value_span;
    if (!std::isfinite(cx_raw) || !std::isfinite(cy)) return std::nullopt;

    // X is clamped, never rejected: the handle stops at the boundary and the
    // drag continues. Y is never clamped, so finite overshoot survives.
    std::array<double, 4> result = current;
    const std::size_t offset = handle == HandleIndex::First ? 0U : 2U;
    result[offset] = std::clamp(cx_raw, 0.0, 1.0);
    result[offset + 1U] = cy;
    return result;
}

} // namespace marrow::editor::timeline_graph_model
