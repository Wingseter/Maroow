#include "timeline_graph_model.hpp"
#include "timeline_model.hpp"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>
#include <utility>

#include "marrow/editor/project.hpp"
#include "marrow/runtime/skeleton.hpp"

namespace model = marrow::editor::timeline_model;
namespace graph = marrow::editor::timeline_graph_model;

namespace {

class TestSuite {
public:
    template <typename Function>
    void run(std::string name, Function&& function) {
        current_case_ = std::move(name);
        const int failures_before = failures_;
        std::forward<Function>(function)();
        if (failures_ == failures_before) {
            std::cout << "PASS: " << current_case_ << '\n';
        } else {
            std::cout << "FAIL: " << current_case_ << '\n';
        }
        ++case_count_;
    }

    void expect(bool condition, std::string_view message) {
        if (!condition) {
            ++failures_;
            std::cerr << current_case_ << ": " << message << '\n';
        }
    }

    int finish() const {
        if (failures_ == 0) {
            std::cout << "Timeline graph model: " << case_count_ << " cases passed\n";
            return 0;
        }
        std::cerr << "Timeline graph model: " << failures_ << " failure(s) across "
                  << case_count_ << " cases\n";
        return 1;
    }

private:
    std::string current_case_;
    int failures_{0};
    int case_count_{0};
};

model::TrackRow translate_row(std::vector<double> key_times) {
    return {
        "bone:0:Translate",
        "Bone / synthetic / Translate",
        "synthetic",
        std::move(key_times),
        0U,
        std::nullopt,
        marrow::editor::TransformTimelineChannel::Translate,
        std::nullopt,
        model::TimelineTrackKind::Translate};
}

bool near(double left, double right, double tolerance = 1e-6) {
    return std::abs(left - right) <= tolerance;
}

bool near_scaled(double left, double right, double tolerance = 1e-5) {
    return std::abs(left - right) <=
        tolerance * std::max({1.0, std::abs(left), std::abs(right)});
}

graph::Track make_scalar_track(
    marrow::runtime::Interpolation easing,
    double first_value,
    double second_value) {
    graph::Track track;
    track.track_id = "bone:0:Rotate";
    track.label = "Bone / synthetic / Rotate";
    track.kind = graph::TrackKind::Rotate;
    track.components = {{graph::Component::Angle, "Angle"}};
    track.keys = {
        {{track.track_id, 0, 0U, 1U}, 0.0, {first_value, 0.0, 0.0, 0.0}, 1U,
         std::move(easing)},
        {{track.track_id, 1'000'000, 0U, 1U}, 1.0, {second_value, 0.0, 0.0, 0.0},
         1U, marrow::runtime::Interpolation::linear()},
    };
    return track;
}

void test_segment_geometry(TestSuite& suite) {
    const graph::PlotRect rect{0.0, 0.0, 200.0, 200.0};
    const graph::View view{0.0, 100.0, 5.0, 10.0};
    const auto linear = graph::build_geometry(
        make_scalar_track(marrow::runtime::Interpolation::linear(), 0.0, 10.0),
        {true, false, false, false}, view, rect, 0.5);
    suite.expect(
        linear && linear->segments.size() == 1U &&
            linear->segments[0].kind == graph::SegmentKind::Linear &&
            linear->segments[0].polyline.size() == 2U,
        "linear easing must produce one straight two-point segment");

    const auto stepped = graph::build_geometry(
        make_scalar_track(marrow::runtime::Interpolation::stepped(), 0.0, 10.0),
        {true, false, false, false}, view, rect, 0.5);
    suite.expect(
        stepped && stepped->segments.size() == 1U &&
            stepped->segments[0].kind == graph::SegmentKind::Stepped &&
            stepped->segments[0].polyline.size() == 3U &&
            near(stepped->segments[0].polyline[1].y, stepped->segments[0].polyline[0].y),
        "stepped easing must hold then jump at the next key");

    const auto cubic = graph::build_geometry(
        make_scalar_track(
            marrow::runtime::Interpolation::cubic_bezier(0.25, 0.1, 0.75, 0.9),
            0.0,
            10.0),
        {true, false, false, false}, view, rect, 0.5);
    suite.expect(
        cubic && cubic->segments.size() == 1U &&
            cubic->segments[0].kind == graph::SegmentKind::Cubic &&
            cubic->segments[0].polyline.size() > 2U,
        "cubic easing must generate adaptive intermediate points");
    suite.expect(
        cubic && cubic->segments.size() == 1U,
        "the final key must not create an outgoing segment");
}

void test_geometry_rejects_invalid_and_omits_off_canvas_centers(TestSuite& suite) {
    const graph::PlotRect rect{0.0, 0.0, 100.0, 100.0};
    const graph::View view{0.0, 100.0, 0.0, 10.0};
    const auto invalid = graph::build_geometry(
        make_scalar_track(
            marrow::runtime::Interpolation::cubic_bezier(
                0.25,
                std::numeric_limits<double>::infinity(),
                0.75,
                0.9),
            0.0,
            10.0),
        {true, false, false, false}, view, rect, 0.5);
    suite.expect(!invalid.has_value(), "a non-finite easing sample must reject complete geometry");

    const auto off_canvas = graph::build_geometry(
        make_scalar_track(marrow::runtime::Interpolation::linear(), -10.0, 10.0),
        {true, false, false, false}, view, rect, 2.0);
    suite.expect(
        off_canvas && off_canvas->points.empty() && !off_canvas->playhead_x.has_value(),
        "off-canvas point and playhead centers must not enter hittable geometry");
}

void test_fit_view_and_view_operations(TestSuite& suite) {
    const graph::PlotRect rect{0.0, 0.0, 220.0, 240.0};
    auto padded_time_track = make_scalar_track(
        marrow::runtime::Interpolation::linear(), 10.0, 30.0);
    padded_time_track.keys[0].time_seconds = 1.0;
    padded_time_track.keys[1].time_seconds = 3.0;
    const auto fit = graph::fit_view(
        padded_time_track,
        {true, false, false, false}, rect, 60.0);
    suite.expect(
        fit && near(fit->view_start_seconds, 0.9) && near(fit->pixels_per_second, 100.0) &&
            near(fit->value_center, 20.0) && near(fit->pixels_per_value, 10.0),
        "fit must apply exact five-percent time and ten-percent value padding");

    auto flat = make_scalar_track(marrow::runtime::Interpolation::linear(), 20.0, 20.0);
    const auto flat_fit = graph::fit_view(flat, {true, false, false, false}, rect, 60.0);
    suite.expect(
        flat_fit && near(flat_fit->pixels_per_value, 60.0),
        "flat values must use max(abs(value) * 0.1, 1e-3) padding");

    const graph::PlotRect narrow_rect{0.0, 0.0, 10.0, 240.0};
    auto one_key = make_scalar_track(marrow::runtime::Interpolation::linear(), 2.0, 2.0);
    one_key.keys.resize(1U);
    one_key.keys[0].time_seconds = 2.0;
    const auto one_frame_fit = graph::fit_view(one_key, {true, false, false, false}, narrow_rect, 20.0);
    suite.expect(
        one_frame_fit && near(one_frame_fit->view_start_seconds, 1.9725) &&
            near(one_frame_fit->pixels_per_second, 10.0 / 0.055),
        "fit must retain a one-frame time span before applying padding");

    auto color = make_scalar_track(marrow::runtime::Interpolation::linear(), 0.4, 0.6);
    color.kind = graph::TrackKind::SlotColor;
    color.components = {{graph::Component::Red, "Red"}, {graph::Component::Green, "Green"},
                        {graph::Component::Blue, "Blue"}, {graph::Component::Alpha, "Alpha"}};
    color.keys[0].values = {0.4, 0.4, 0.4, 0.4};
    color.keys[1].values = {0.6, 0.6, 0.6, 0.6};
    color.keys[0].value_count = 4U;
    color.keys[1].value_count = 4U;
    const auto color_fit = graph::fit_view(color, {true, false, false, false}, rect, 60.0);
    suite.expect(
        color_fit && near(color_fit->value_center, 0.5) && near(color_fit->pixels_per_value, 200.0),
        "Slot Color fit must include the complete zero-to-one range");

    const auto overshoot = make_scalar_track(
        marrow::runtime::Interpolation::cubic_bezier(0.2, 2.0, 0.8, 2.0), 0.0, 1.0);
    const auto overshoot_fit = graph::fit_view(overshoot, {true, false, false, false}, rect, 60.0);
    suite.expect(
        overshoot_fit &&
            overshoot_fit->value_center + 120.0 / overshoot_fit->pixels_per_value > 1.5,
        "fit must include sampled cubic overshoot before padding");

    graph::View view{1.0, 100.0, 10.0, 20.0};
    const double anchored_time = view.view_start_seconds + (60.0 - rect.min_x) / view.pixels_per_second;
    const double anchored_value = view.value_center +
        ((rect.min_y + rect.max_y) * 0.5 - 80.0) / view.pixels_per_value;
    suite.expect(graph::zoom_time_at(&view, rect, 60.0, 1.0), "finite time zoom must succeed");
    suite.expect(graph::zoom_value_at(&view, rect, 80.0, -1.0), "finite value zoom must succeed");
    suite.expect(
        near(view.view_start_seconds + 60.0 / view.pixels_per_second, anchored_time) &&
            near(view.value_center + ((rect.min_y + rect.max_y) * 0.5 - 80.0) /
                                      view.pixels_per_value,
                 anchored_value),
        "zoom must preserve the time and value under the cursor");
    suite.expect(graph::pan_view(&view, 23.0, 11.0), "finite pan must succeed");
    suite.expect(
        near(view.view_start_seconds + 23.0 / view.pixels_per_second,
             anchored_time - 60.0 / view.pixels_per_second) &&
            near(view.value_center - 11.0 / view.pixels_per_value,
                 anchored_value - ((rect.min_y + rect.max_y) * 0.5 - 80.0) /
                                      view.pixels_per_value),
        "right/down pan must apply the documented signed deltas");

    graph::View time_clamp{0.0, 100.0, 0.0, 1.0};
    suite.expect(graph::zoom_time_at(&time_clamp, rect, 0.0, -100.0) &&
                     near(time_clamp.pixels_per_second, 0.01),
                 "time zoom must clamp to the lower 0.01 pixels-per-second bound");
    suite.expect(graph::zoom_time_at(&time_clamp, rect, 0.0, 100.0) &&
                     near(time_clamp.pixels_per_second, 1600.0),
                 "time zoom must clamp to the upper 1600 pixels-per-second bound");
    const graph::View unchanged = view;
    suite.expect(
        !graph::zoom_value_at(&view, rect, 80.0, std::numeric_limits<double>::infinity()) &&
            near(view.value_center, unchanged.value_center) &&
            near(view.pixels_per_value, unchanged.pixels_per_value),
        "non-finite zoom input must leave the view unchanged");

    suite.expect(
        graph::nice_tick_interval(10.0, 72.0).has_value() &&
            near(*graph::nice_tick_interval(10.0, 72.0), 10.0) &&
            graph::nice_tick_interval(100.0, 48.0).has_value() &&
            near(*graph::nice_tick_interval(100.0, 48.0), 0.5) &&
            !graph::nice_tick_interval(0.0, 48.0).has_value() &&
            !graph::nice_tick_interval(100.0, std::numeric_limits<double>::infinity()).has_value(),
        "ticks must select 1/2/5 intervals meeting spacing and reject invalid input");
    suite.expect(
        flat.keys[0].time_seconds == 0.0 && flat.keys[1].time_seconds == 1.0,
        "view operations must preserve authored key timing");
}

void test_value_zoom_clamps_and_preserves_cursor_anchor(TestSuite& suite) {
    const graph::PlotRect rect{0.0, 0.0, 200.0, 200.0};
    const auto value_at_cursor = [&](const graph::View& view, double cursor_y) {
        return view.value_center +
            (((rect.min_y + rect.max_y) * 0.5) - cursor_y) /
                view.pixels_per_value;
    };

    constexpr double lower_cursor_y = 99.999999;
    graph::View lower{0.0, 100.0, 4.0, 1.0};
    const double lower_anchor = value_at_cursor(lower, lower_cursor_y);
    suite.expect(
        graph::zoom_value_at(&lower, rect, lower_cursor_y, -1000.0) &&
            lower.pixels_per_value == 1e-9 &&
            near_scaled(value_at_cursor(lower, lower_cursor_y), lower_anchor),
        "value zoom must clamp to 1e-9 pixels per native unit using the final cursor anchor");

    constexpr double upper_cursor_y = 75.0;
    graph::View upper{0.0, 100.0, 4.0, 1.0};
    const double upper_anchor = value_at_cursor(upper, upper_cursor_y);
    suite.expect(
        graph::zoom_value_at(&upper, rect, upper_cursor_y, 1000.0) &&
            upper.pixels_per_value == 1e9 &&
            near_scaled(value_at_cursor(upper, upper_cursor_y), upper_anchor),
        "value zoom must clamp to 1e9 pixels per native unit using the final cursor anchor");

    graph::View repeated{0.0, 100.0, 4.0, 1.0};
    const double repeated_anchor = value_at_cursor(repeated, 80.0);
    bool repeated_zoom_succeeded = true;
    for (int step = 0; step < 400; ++step) {
        repeated_zoom_succeeded = repeated_zoom_succeeded &&
            graph::zoom_value_at(&repeated, rect, 80.0, 1.0);
    }
    suite.expect(
        repeated_zoom_succeeded && repeated.pixels_per_value == 1e9 &&
            near_scaled(value_at_cursor(repeated, 80.0), repeated_anchor),
        "repeated upward wheel input must saturate at the upper value-zoom bound without anchor drift");
    for (int step = 0; step < 800; ++step) {
        repeated_zoom_succeeded = repeated_zoom_succeeded &&
            graph::zoom_value_at(&repeated, rect, 80.0, -1.0);
    }
    suite.expect(
        repeated_zoom_succeeded && repeated.pixels_per_value == 1e-9 &&
            near_scaled(value_at_cursor(repeated, 80.0), repeated_anchor),
        "repeated downward wheel input must saturate at the lower value-zoom bound without anchor drift");

    graph::View overflow_safe{0.0, 100.0, 4.0, 1.0};
    const double overflow_anchor = value_at_cursor(overflow_safe, 75.0);
    suite.expect(
        graph::zoom_value_at(
            &overflow_safe,
            rect,
            75.0,
            std::numeric_limits<double>::max()) &&
            overflow_safe.pixels_per_value == 1e9 &&
            near_scaled(value_at_cursor(overflow_safe, 75.0), overflow_anchor),
        "finite extreme wheel input must saturate without overflowing scale computation");

    const graph::View unchanged = overflow_safe;
    suite.expect(
        !graph::zoom_value_at(
            &overflow_safe,
            rect,
            75.0,
            std::numeric_limits<double>::infinity()) &&
            overflow_safe.view_start_seconds == unchanged.view_start_seconds &&
            overflow_safe.pixels_per_second == unchanged.pixels_per_second &&
            overflow_safe.value_center == unchanged.value_center &&
            overflow_safe.pixels_per_value == unchanged.pixels_per_value,
        "non-finite value-zoom input must reject transactionally without changing any view field");
}

graph::Point graph_point(
    double x,
    double y,
    std::int64_t time_microseconds,
    std::size_t same_time_ordinal,
    graph::Component component,
    std::size_t component_index) {
    return {{x, y},
            {"bone:0:Rotate", time_microseconds, same_time_ordinal, 2U},
            component,
            component_index};
}

void test_inclusive_hit_test_and_stable_ties(TestSuite& suite) {
    graph::Geometry radius_geometry;
    radius_geometry.points.push_back(graph_point(0.0, 0.0, 0, 0U, graph::Component::Angle, 0U));
    const auto edge_hit = graph::hit_test(radius_geometry, 8.0, 0.0);
    suite.expect(
        edge_hit.has_value() && edge_hit->component == graph::Component::Angle,
        "a point exactly eight logical pixels away must hit inclusively");
    suite.expect(
        !graph::hit_test(radius_geometry, 8.001, 0.0).has_value(),
        "a point farther than eight logical pixels must miss");

    graph::Geometry nearest_geometry;
    nearest_geometry.points = {
        graph_point(0.0, 0.0, 10, 0U, graph::Component::Angle, 0U),
        graph_point(3.0, 0.0, 20, 0U, graph::Component::X, 1U),
    };
    const auto nearest_hit = graph::hit_test(nearest_geometry, 2.5, 0.0);
    suite.expect(
        nearest_hit.has_value() && nearest_hit->key.time_microseconds == 20,
        "minimum Euclidean distance must win before stable tie ordering");

    graph::Geometry time_tie_geometry;
    time_tie_geometry.points = {
        graph_point(0.0, 0.0, 20, 0U, graph::Component::Angle, 0U),
        graph_point(0.0, 0.0, 10, 1U, graph::Component::Alpha, 3U),
        graph_point(0.0, 0.0, 10, 0U, graph::Component::Alpha, 3U),
        graph_point(0.0, 0.0, 10, 0U, graph::Component::Angle, 0U),
    };
    const auto tie_hit = graph::hit_test(time_tie_geometry, 0.0, 0.0);
    suite.expect(
        tie_hit.has_value() && tie_hit->key.time_microseconds == 10 &&
            tie_hit->key.same_time_ordinal == 0U &&
            tie_hit->component == graph::Component::Angle,
        "exact ties must order by time, same-time ordinal, then graph component order");
}

void test_near_limit_midpoints_remain_finite(TestSuite& suite) {
    const graph::PlotRect rect{0.0, 0.0, 200.0, 200.0};
    const auto flat_fit = graph::fit_view(
        make_scalar_track(marrow::runtime::Interpolation::linear(), 1e308, 1e308),
        {true, false, false, false},
        rect,
        60.0);
    suite.expect(
        flat_fit && std::isfinite(flat_fit->value_center) &&
            std::isfinite(flat_fit->pixels_per_value),
        "finite near-limit flat values must produce a finite fit");

    const graph::View value_view{0.0, 100.0, 1e308, 1.0};
    const auto linear = graph::build_geometry(
        make_scalar_track(marrow::runtime::Interpolation::linear(), 1e308, 1e308),
        {true, false, false, false},
        value_view, rect, 0.5);
    suite.expect(
        linear && std::isfinite(linear->segments[0].marker.x) &&
            std::isfinite(linear->segments[0].marker.y),
        "finite near-limit linear endpoints must retain a finite marker");

    auto cubic_track = make_scalar_track(
        marrow::runtime::Interpolation::cubic_bezier(0.25, 0.1, 0.75, 0.9), 0.0, 10.0);
    cubic_track.keys[0].time_seconds = 1e308;
    cubic_track.keys[1].time_seconds = std::nextafter(1e308, std::numeric_limits<double>::infinity());
    const graph::View time_view{1e308, 1e-292, 5.0, 10.0};
    const auto cubic = graph::build_geometry(
        cubic_track, {true, false, false, false}, time_view, rect, 1e308);
    suite.expect(
        cubic && std::isfinite(cubic->segments[0].marker.x) &&
            std::isfinite(cubic->segments[0].marker.y),
        "finite near-limit cubic endpoint times must retain a finite marker");
}

void test_geometry_markers_sampling_and_off_canvas_segments(TestSuite& suite) {
    const graph::PlotRect rect{0.0, 0.0, 200.0, 200.0};
    const graph::View view{0.0, 100.0, 5.0, 10.0};
    const auto linear = graph::build_geometry(
        make_scalar_track(marrow::runtime::Interpolation::linear(), 0.0, 10.0),
        {true, false, false, false}, view, rect, 0.5);
    const auto stepped = graph::build_geometry(
        make_scalar_track(marrow::runtime::Interpolation::stepped(), 0.0, 10.0),
        {true, false, false, false}, view, rect, 0.5);
    const auto easing = marrow::runtime::Interpolation::cubic_bezier(0.25, 0.0, 0.75, 0.0);
    const auto cubic = graph::build_geometry(
        make_scalar_track(easing, 0.0, 10.0),
        {true, false, false, false}, view, rect, 0.5);
    suite.expect(
        linear && near(linear->segments[0].marker.x, 50.0) &&
            near(linear->segments[0].marker.y, 100.0) &&
            stepped && near(stepped->segments[0].marker.x, 100.0) &&
            near(stepped->segments[0].marker.y, 150.0) &&
            cubic && near(cubic->segments[0].marker.x, 50.0) &&
            near(cubic->segments[0].marker.y, 150.0 - 100.0 * easing.transform(0.5)),
        "Linear, Stepped, and Cubic markers must use midpoint, elbow, and runtime u=0.5 semantics");

    bool cubic_deviation_is_bounded = cubic.has_value() &&
        cubic->segments[0].polyline.size() <= 1025U;
    if (cubic_deviation_is_bounded) {
        const auto& polyline = cubic->segments[0].polyline;
        for (std::size_t index = 0U; index + 1U < polyline.size(); ++index) {
            const double midpoint_time = (polyline[index].x + polyline[index + 1U].x) / 200.0;
            const double expected_y = 150.0 - 100.0 * easing.transform(midpoint_time);
            const double chord_x = (polyline[index].x + polyline[index + 1U].x) * 0.5;
            const double chord_y = (polyline[index].y + polyline[index + 1U].y) * 0.5;
            if (std::hypot(100.0 * midpoint_time - chord_x, expected_y - chord_y) > 0.500001) {
                cubic_deviation_is_bounded = false;
                break;
            }
        }
    }
    suite.expect(
        cubic_deviation_is_bounded,
        "cubic subdivision must keep each plot-space midpoint deviation within 0.5 pixels and depth ten");

    const auto off_canvas = graph::build_geometry(
        make_scalar_track(marrow::runtime::Interpolation::linear(), -20.0, 20.0),
        {true, false, false, false}, view, rect, 2.1);
    suite.expect(
        off_canvas && off_canvas->segments.size() == 1U &&
            off_canvas->segments[0].polyline.size() == 2U && off_canvas->points.empty() &&
            !off_canvas->playhead_x.has_value(),
        "a plot-crossing segment with off-canvas key centers must remain drawable while those centers stay unhittable");
}

void test_fit_contract_edges(TestSuite& suite) {
    const graph::PlotRect rect{0.0, 0.0, 220.0, 240.0};
    graph::Track two_components = make_scalar_track(
        marrow::runtime::Interpolation::linear(), 0.0, 1.0);
    two_components.kind = graph::TrackKind::Translate;
    two_components.components = {{graph::Component::X, "X"}, {graph::Component::Y, "Y"}};
    two_components.keys[0].values = {0.0, -1e6, 0.0, 0.0};
    two_components.keys[1].values = {1.0, 1e6, 0.0, 0.0};
    two_components.keys[0].value_count = 2U;
    two_components.keys[1].value_count = 2U;
    const auto visible_x_fit = graph::fit_view(two_components, {true, false, false, false}, rect, 60.0);
    suite.expect(
        visible_x_fit && near(visible_x_fit->value_center, 0.5) &&
            near(visible_x_fit->pixels_per_value, 200.0),
        "fit must consider visible components only");

    graph::Track empty_track = two_components;
    empty_track.keys.clear();
    graph::Track nonfinite_track = two_components;
    nonfinite_track.keys[0].values[0] = std::numeric_limits<double>::quiet_NaN();
    suite.expect(
        !graph::fit_view(empty_track, {true, false, false, false}, rect, 60.0).has_value() &&
            !graph::fit_view(nonfinite_track, {true, false, false, false}, rect, 60.0).has_value(),
        "empty and visible non-finite tracks must reject fitting");

    const auto zero_flat_fit = graph::fit_view(
        make_scalar_track(marrow::runtime::Interpolation::linear(), 0.0, 0.0),
        {true, false, false, false}, rect, 60.0);
    suite.expect(
        zero_flat_fit && near(zero_flat_fit->pixels_per_value, 120000.0),
        "flat zero values must use the exact 1e-3 minimum padding");

    const auto overshoot_track = make_scalar_track(
        marrow::runtime::Interpolation::cubic_bezier(0.2, 2.0, 0.8, 2.0), 0.0, 1.0);
    const auto overshoot_fit = graph::fit_view(
        overshoot_track, {true, false, false, false}, rect, 60.0);
    const auto overshoot_geometry = overshoot_fit
        ? graph::build_geometry(overshoot_track, {true, false, false, false}, *overshoot_fit, rect, 0.0)
        : std::nullopt;
    double sampled_maximum = -std::numeric_limits<double>::infinity();
    if (overshoot_geometry) {
        for (const auto& point : overshoot_geometry->segments[0].polyline) {
            sampled_maximum = std::max(
                sampled_maximum,
                overshoot_fit->value_center + (120.0 - point.y) / overshoot_fit->pixels_per_value);
        }
    }
    const double fitted_maximum = overshoot_fit
        ? overshoot_fit->value_center + 120.0 / overshoot_fit->pixels_per_value
        : -std::numeric_limits<double>::infinity();
    suite.expect(
        overshoot_geometry && sampled_maximum > 1.0 && fitted_maximum >= sampled_maximum,
        "fitted bounds must contain the actual maximum sampled cubic overshoot");
}

void test_full_component_hit_tie_chain(TestSuite& suite) {
    const std::array<graph::Component, 7U> components{
        graph::Component::Angle,
        graph::Component::X,
        graph::Component::Y,
        graph::Component::Red,
        graph::Component::Green,
        graph::Component::Blue,
        graph::Component::Alpha};
    bool ordered = true;
    for (std::size_t first = 0U; first < components.size(); ++first) {
        graph::Geometry geometry;
        for (std::size_t index = components.size(); index-- > first;) {
            geometry.points.push_back(graph_point(
                0.0, 0.0, 10, 0U, components[index], index));
        }
        const auto hit = graph::hit_test(geometry, 0.0, 0.0);
        ordered = ordered && hit && hit->component == components[first];
    }
    suite.expect(
        ordered,
        "component ties must order Angle, X, Y, R, G, B, then A");
}

void test_subnormal_and_opposite_midpoint_contracts(TestSuite& suite) {
    const double denorm = std::numeric_limits<double>::denorm_min();
    const graph::PlotRect rect{0.0, 0.0, 200.0, 200.0};
    const auto subnormal_fit = graph::fit_view(
        make_scalar_track(marrow::runtime::Interpolation::linear(), denorm, denorm),
        {true, false, false, false}, rect, 60.0);
    suite.expect(
        subnormal_fit && subnormal_fit->value_center == denorm,
        "an equal denorm_min flat fit must preserve its finite midpoint exactly");

    const double maximum = std::numeric_limits<double>::max();
    const graph::PlotRect opposite_rect{0.0, -maximum, 100.0, maximum};
    const graph::View opposite_view{0.0, 100.0, 0.0, 1.0};
    const auto opposite = graph::build_geometry(
        make_scalar_track(marrow::runtime::Interpolation::linear(), 0.0, 0.0),
        {true, false, false, false}, opposite_view, opposite_rect, 2.0);
    suite.expect(
        opposite && opposite->segments.size() == 1U &&
            opposite->segments[0].marker.y == 0.0,
        "opposite-sign finite plot bounds must retain their zero midpoint");
}

void test_overshooting_affine_and_wholly_off_canvas_paths(TestSuite& suite) {
    const double maximum = std::numeric_limits<double>::max();
    const graph::PlotRect rect{0.0, 0.0, 200.0, 200.0};
    const graph::View maximum_view{0.0, 100.0, maximum, 1.0};
    const auto overshooting = graph::build_geometry(
        make_scalar_track(
            marrow::runtime::Interpolation::cubic_bezier(0.25, 2.0, 0.75, 2.0),
            maximum,
            maximum),
        {true, false, false, false}, maximum_view, rect, 0.5);
    suite.expect(
        overshooting && overshooting->segments.size() == 1U &&
            std::isfinite(overshooting->segments[0].marker.x) &&
            std::isfinite(overshooting->segments[0].marker.y) &&
            near(overshooting->segments[0].marker.y, 100.0),
        "equal DBL_MAX cubic endpoints must remain finite under an overshooting alpha");

    const graph::View view{0.0, 100.0, 5.0, 10.0};
    const auto linear = graph::build_geometry(
        make_scalar_track(marrow::runtime::Interpolation::linear(), -20.0, -10.0),
        {true, false, false, false}, view, rect, 2.1);
    const auto stepped = graph::build_geometry(
        make_scalar_track(marrow::runtime::Interpolation::stepped(), -20.0, -10.0),
        {true, false, false, false}, view, rect, 2.1);
    const auto wholly_below = [](const graph::Geometry& geometry) {
        return geometry.segments.size() == 1U && geometry.points.empty() &&
            !geometry.playhead_x.has_value() &&
            std::all_of(
                geometry.segments[0].polyline.begin(),
                geometry.segments[0].polyline.end(),
                [](const graph::PlotPoint& point) { return point.y > 200.0; });
    };
    suite.expect(
        linear && stepped && wholly_below(*linear) && wholly_below(*stepped),
        "wholly nonintersecting Linear and Stepped paths must remain drawable but unhittable");
}

void expect_rejected_projection(
    TestSuite& suite,
    const graph::Projection& projection,
    graph::ProjectionStatus expected_status,
    std::string_view message) {
    suite.expect(
        projection.status == expected_status && !projection.track.has_value(),
        message);
}

void test_player_idle_supported_projection(TestSuite& suite) {
    auto loaded = marrow::editor::load_project("assets/fixtures/player_idle.marrow");
    suite.expect(static_cast<bool>(loaded), "fixture project must load");
    if (!loaded) return;
    const auto* animation = loaded.skeleton_data->find_animation("idle");
    suite.expect(animation != nullptr, "idle animation must resolve");
    if (animation == nullptr) return;

    const auto rows = model::build_tracks(*loaded.skeleton_data, *animation);
    std::size_t parent_count = 0U;
    std::size_t component_count = 0U;
    for (const auto& row : rows) {
        const graph::Projection projected = graph::project_track(*animation, row);
        if (row.kind == model::TimelineTrackKind::Inherit ||
            row.kind == model::TimelineTrackKind::SlotAttachment ||
            row.kind == model::TimelineTrackKind::Deform ||
            row.kind == model::TimelineTrackKind::DrawOrder ||
            row.kind == model::TimelineTrackKind::Event) {
            suite.expect(
                projected.status == graph::ProjectionStatus::UnsupportedTrack,
                "discrete, deform, and inherit rows must stay outside the scalar graph");
            continue;
        }
        if (projected.status != graph::ProjectionStatus::Ready) continue;
        ++parent_count;
        component_count += projected.track->components.size();
        for (const auto& key : projected.track->keys) {
            suite.expect(
                model::key_index(row, key.identity).has_value(),
                "graph key must reuse the dopesheet parent identity");
        }
    }
    suite.expect(parent_count == 7U, "fixture must expose seven supported parent tracks");
    suite.expect(component_count == 14U, "fixture must expose fourteen scalar series");
}

void test_absolute_rotation_and_parent_easing(TestSuite& suite) {
    marrow::runtime::AnimationData animation;
    animation.bone_rotate_timelines.push_back({
        0U,
        30.0,
        {
            {0.0, 5.0, marrow::runtime::Interpolation::stepped()},
            {1.0, 15.0, marrow::runtime::Interpolation::linear()},
        },
    });
    animation.slot_color_timelines.push_back({
        0U,
        {{0.5F,
          marrow::runtime::SlotColor{0.1, 0.2, 0.3, 0.4},
          marrow::runtime::Interpolation::cubic_bezier(0.1, 0.2, 0.8, 0.9)}},
    });

    const model::TrackRow rotate_row{
        "bone:0:Rotate",
        "Bone / synthetic / Rotate",
        "synthetic",
        {0.0, 1.0},
        0U,
        std::nullopt,
        marrow::editor::TransformTimelineChannel::Rotate,
        std::nullopt,
        model::TimelineTrackKind::Rotate};
    const graph::Projection rotate = graph::project_track(animation, rotate_row);
    suite.expect(
        rotate.status == graph::ProjectionStatus::Ready && rotate.track.has_value() &&
            rotate.track->keys.size() == 2U &&
            rotate.track->keys[0].values[0] == 35.0 &&
            rotate.track->keys[1].values[0] == 45.0,
        "Rotate projection must include setup rotation without wrapping authored angles");
    suite.expect(
        rotate.track.has_value() &&
            rotate.track->keys[0].identity == model::key_ref(rotate_row, 0U) &&
            rotate.track->keys[1].identity == model::key_ref(rotate_row, 1U),
        "Rotate keys must reuse their parent dopesheet identities");

    const model::TrackRow color_row{
        "slot:0:Color",
        "Slot / synthetic / Color",
        "synthetic",
        {0.5},
        std::nullopt,
        0U,
        std::nullopt,
        std::nullopt,
        model::TimelineTrackKind::SlotColor};
    const graph::Projection color = graph::project_track(animation, color_row);
    suite.expect(
        color.status == graph::ProjectionStatus::Ready && color.track.has_value() &&
            color.track->components.size() == 4U && color.track->keys.size() == 1U &&
            color.track->keys[0].value_count == 4U &&
            color.track->keys[0].identity == model::key_ref(color_row, 0U) &&
            color.track->keys[0].outgoing_easing.kind() ==
                marrow::runtime::InterpolationKind::CubicBezier,
        "RGBA components must share one parent identity and outgoing easing");
}

void test_missing_source_fails_closed(TestSuite& suite) {
    const marrow::runtime::AnimationData animation;
    const graph::Projection projection = graph::project_track(
        animation,
        translate_row({0.0}));
    expect_rejected_projection(
        suite,
        projection,
        graph::ProjectionStatus::MissingSource,
        "a supported track with no runtime timeline must return MissingSource without a track");
}

void test_source_count_mismatch_fails_closed(TestSuite& suite) {
    marrow::runtime::AnimationData animation;
    animation.bone_translate_timelines.push_back({
        0U,
        {{0.0, 1.0, 2.0}},
    });
    const graph::Projection projection = graph::project_track(
        animation,
        translate_row({0.0, 1.0}));
    expect_rejected_projection(
        suite,
        projection,
        graph::ProjectionStatus::InvalidData,
        "a source and dopesheet key-count mismatch must return InvalidData without a track");
}

void test_time_identity_mismatch_fails_closed(TestSuite& suite) {
    marrow::runtime::AnimationData animation;
    animation.bone_translate_timelines.push_back({
        0U,
        {{0.75, 1.0, 2.0}},
    });
    const graph::Projection projection = graph::project_track(
        animation,
        translate_row({0.5}));
    expect_rejected_projection(
        suite,
        projection,
        graph::ProjectionStatus::InvalidData,
        "a source time whose identity differs from the parent key must return InvalidData without a track");
}

void test_nonfinite_time_or_component_fails_closed(TestSuite& suite) {
    marrow::runtime::AnimationData nonfinite_time;
    nonfinite_time.bone_translate_timelines.push_back({
        0U,
        {{std::numeric_limits<double>::infinity(), 1.0, 2.0}},
    });
    expect_rejected_projection(
        suite,
        graph::project_track(nonfinite_time, translate_row({0.0})),
        graph::ProjectionStatus::InvalidData,
        "a non-finite source key time must return InvalidData without a track");

    marrow::runtime::AnimationData nonfinite_component;
    nonfinite_component.bone_translate_timelines.push_back({
        0U,
        {{0.0, std::numeric_limits<double>::quiet_NaN(), 2.0}},
    });
    expect_rejected_projection(
        suite,
        graph::project_track(nonfinite_component, translate_row({0.0})),
        graph::ProjectionStatus::InvalidData,
        "a non-finite source component must return InvalidData without a track");
}

void test_nonfinite_cubic_control_fails_closed(TestSuite& suite) {
    marrow::runtime::AnimationData animation;
    animation.slot_color_timelines.push_back({
        0U,
        {{0.5F,
          marrow::runtime::SlotColor{0.1, 0.2, 0.3, 0.4},
          marrow::runtime::Interpolation::cubic_bezier(
              0.1,
              std::numeric_limits<double>::infinity(),
              0.8,
              0.9)}},
    });
    const model::TrackRow row{
        "slot:0:Color",
        "Slot / synthetic / Color",
        "synthetic",
        {0.5},
        std::nullopt,
        0U,
        std::nullopt,
        std::nullopt,
        model::TimelineTrackKind::SlotColor};
    expect_rejected_projection(
        suite,
        graph::project_track(animation, row),
        graph::ProjectionStatus::InvalidData,
        "a non-finite cubic control must return InvalidData without a track");
}

} // namespace

int main() {
    TestSuite suite;
    suite.run("segment geometry", [&] {
        test_segment_geometry(suite);
    });
    suite.run("geometry invalid and off-canvas centers", [&] {
        test_geometry_rejects_invalid_and_omits_off_canvas_centers(suite);
    });
    suite.run("fit view and view operations", [&] {
        test_fit_view_and_view_operations(suite);
    });
    suite.run("value zoom clamps and cursor anchor", [&] {
        test_value_zoom_clamps_and_preserves_cursor_anchor(suite);
    });
    suite.run("inclusive hit test and stable ties", [&] {
        test_inclusive_hit_test_and_stable_ties(suite);
    });
    suite.run("near-limit midpoints remain finite", [&] {
        test_near_limit_midpoints_remain_finite(suite);
    });
    suite.run("geometry markers sampling and off-canvas segments", [&] {
        test_geometry_markers_sampling_and_off_canvas_segments(suite);
    });
    suite.run("fit contract edges", [&] {
        test_fit_contract_edges(suite);
    });
    suite.run("full component hit tie chain", [&] {
        test_full_component_hit_tie_chain(suite);
    });
    suite.run("subnormal and opposite midpoint contracts", [&] {
        test_subnormal_and_opposite_midpoint_contracts(suite);
    });
    suite.run("overshooting affine and wholly off-canvas paths", [&] {
        test_overshooting_affine_and_wholly_off_canvas_paths(suite);
    });
    suite.run("player idle supported projection", [&] {
        test_player_idle_supported_projection(suite);
    });
    suite.run("absolute rotation and parent easing", [&] {
        test_absolute_rotation_and_parent_easing(suite);
    });
    suite.run("missing source fails closed", [&] {
        test_missing_source_fails_closed(suite);
    });
    suite.run("source count mismatch fails closed", [&] {
        test_source_count_mismatch_fails_closed(suite);
    });
    suite.run("time identity mismatch fails closed", [&] {
        test_time_identity_mismatch_fails_closed(suite);
    });
    suite.run("nonfinite time or component fails closed", [&] {
        test_nonfinite_time_or_component_fails_closed(suite);
    });
    suite.run("nonfinite cubic control fails closed", [&] {
        test_nonfinite_cubic_control_fails_closed(suite);
    });
    return suite.finish();
}
