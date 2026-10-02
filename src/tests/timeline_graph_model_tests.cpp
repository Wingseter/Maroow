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


void test_graph_drag_axis_and_unit_mapping(TestSuite& suite) {
    constexpr graph::PlotRect rect{100.0, 40.0, 700.0, 340.0};
    const graph::View view{0.5, 200.0, 10.0, 25.0};

    suite.expect(
        near(graph::time_at_x(rect, view, graph::x_at_time(rect, view, 1.25)), 1.25, 1e-9) &&
            near(graph::value_at_y(rect, view, graph::y_at_value(rect, view, -3.5)), -3.5, 1e-9),
        "pixel and unit mapping must round-trip");

    const graph::View negative_start{-2.25, 37.5, -18.0, 0.125};
    suite.expect(
        near(
            graph::time_at_x(
                rect, negative_start, graph::x_at_time(rect, negative_start, -1.75)),
            -1.75,
            1e-9) &&
            near(
                graph::value_at_y(
                    rect, negative_start, graph::y_at_value(rect, negative_start, 96.5)),
                96.5,
                1e-9),
        "a negative view start and a sub-unit value scale must still round-trip");

    suite.expect(
        graph::decide_drag_axis(300.0, 200.0, 302.0, 201.0, 4.0) ==
            graph::DragAxis::Undecided,
        "a move inside the dead zone must not choose an axis");
    suite.expect(
        graph::decide_drag_axis(300.0, 200.0, 320.0, 203.0, 4.0) == graph::DragAxis::Time,
        "a dominant horizontal move must lock the time axis");
    suite.expect(
        graph::decide_drag_axis(300.0, 200.0, 303.0, 220.0, 4.0) == graph::DragAxis::Value,
        "a dominant vertical move must lock the value axis");
    suite.expect(
        graph::decide_drag_axis(300.0, 200.0, 310.0, 210.0, 4.0) == graph::DragAxis::Value,
        "an exact axis tie must resolve to the value axis");
    // The dead zone is a box compared with >=, so exactly the threshold locks.
    suite.expect(
        graph::decide_drag_axis(300.0, 200.0, 304.0, 200.0, 4.0) == graph::DragAxis::Time &&
            graph::decide_drag_axis(300.0, 200.0, 300.0, 196.0, 4.0) ==
                graph::DragAxis::Value,
        "a move of exactly the dead-zone distance must lock an axis");
    suite.expect(
        graph::decide_drag_axis(300.0, 200.0, 303.999, 203.999, 4.0) ==
            graph::DragAxis::Undecided,
        "a diagonal move just inside the dead-zone box must not lock an axis");
    suite.expect(
        graph::decide_drag_axis(300.0, 200.0, 320.0, 203.0) == graph::DragAxis::Time,
        "the default dead zone must come from kDragDeadZonePixels");
    suite.expect(
        graph::decide_drag_axis(
            std::numeric_limits<double>::quiet_NaN(), 200.0, 310.0, 210.0, 4.0) ==
            graph::DragAxis::Undecided,
        "non-finite pointer input must not choose an axis");
    suite.expect(
        graph::decide_drag_axis(
            300.0, 200.0, 310.0, 210.0,
            std::numeric_limits<double>::infinity()) == graph::DragAxis::Undecided,
        "a non-finite dead zone must not choose an axis");
    suite.expect(
        graph::decide_drag_axis(300.0, 200.0, 310.0, 210.0, -1.0) ==
            graph::DragAxis::Undecided,
        "a negative dead zone must not choose an axis");

    const auto time_delta = graph::drag_time_delta(view, 300.0, 400.0);
    suite.expect(
        time_delta.has_value() && near(*time_delta, 0.5),
        "time delta must divide the pixel delta by pixels per second");
    const auto value_delta = graph::drag_value_delta(view, 300.0, 200.0);
    suite.expect(
        value_delta.has_value() && near(*value_delta, 4.0),
        "value delta must invert screen Y and divide by pixels per value");
    const auto negative_value_delta = graph::drag_value_delta(view, 200.0, 300.0);
    suite.expect(
        negative_value_delta.has_value() && near(*negative_value_delta, -4.0),
        "downward screen motion must produce a negative value delta");

    graph::View degenerate = view;
    degenerate.pixels_per_value = 0.0;
    suite.expect(
        !graph::drag_value_delta(degenerate, 300.0, 200.0).has_value(),
        "a non-positive value scale must reject the drag delta");
    graph::View degenerate_time = view;
    degenerate_time.pixels_per_second = std::numeric_limits<double>::quiet_NaN();
    suite.expect(
        !graph::drag_time_delta(degenerate_time, 300.0, 400.0).has_value(),
        "a non-finite time scale must reject the drag delta");
    suite.expect(
        !graph::drag_time_delta(
             view, std::numeric_limits<double>::infinity(), 400.0).has_value(),
        "a non-finite press coordinate must reject the drag delta");
    suite.expect(
        !graph::drag_value_delta(
             view, 300.0, std::numeric_limits<double>::quiet_NaN()).has_value(),
        "a non-finite pointer coordinate must reject the drag delta");

    graph::View overflow_view = view;
    overflow_view.pixels_per_value = std::numeric_limits<double>::denorm_min();
    suite.expect(
        !graph::drag_value_delta(
             overflow_view, std::numeric_limits<double>::max(), -std::numeric_limits<double>::max())
             .has_value(),
        "an overflowing quotient must reject the drag delta");

    // A rect wide enough to overflow a naive (min + max) * 0.5 must still map
    // through the shared safe midpoint the geometry builder uses.
    const double huge_low = std::numeric_limits<double>::max() * 0.6;
    const double huge_high = std::numeric_limits<double>::max() * 0.9;
    const graph::PlotRect huge_rect{0.0, huge_low, 100.0, huge_high};
    const graph::View unit_view{0.0, 100.0, 0.0, 1.0};
    const double expected_midpoint = std::numeric_limits<double>::max() * 0.75;
    suite.expect(
        !std::isfinite((huge_rect.min_y + huge_rect.max_y) * 0.5) &&
            std::isfinite(graph::y_at_value(huge_rect, unit_view, 2.0)) &&
            near_scaled(
                graph::y_at_value(huge_rect, unit_view, 2.0), expected_midpoint) &&
            near(graph::value_at_y(huge_rect, unit_view, expected_midpoint), 0.0, 1e-9),
        "near-limit rects must map through the shared safe midpoint");

    // The render path and the drag path must agree on every submitted point.
    const graph::View render_view{-0.1, 200.0, 10.0, 25.0};
    const auto track = make_scalar_track(
        marrow::runtime::Interpolation::linear(), 8.0, 12.0);
    const auto geometry = graph::build_geometry(
        track, {true, false, false, false}, render_view, rect, 0.5);
    suite.expect(
        geometry.has_value() && geometry->points.size() == 2U,
        "the mapping comparison requires two submitted graph points");
    if (geometry.has_value()) {
        bool matched = !geometry->points.empty();
        for (const auto& point : geometry->points) {
            const auto key = std::find_if(
                track.keys.begin(),
                track.keys.end(),
                [&](const graph::Key& candidate) {
                    return candidate.identity == point.key;
                });
            if (key == track.keys.end()) {
                matched = false;
                break;
            }
            matched = matched &&
                near(
                    graph::x_at_time(rect, render_view, key->time_seconds),
                    point.position.x,
                    1e-9) &&
                near(
                    graph::y_at_value(
                        rect, render_view, key->values[point.component_index]),
                    point.position.y,
                    1e-9);
        }
        suite.expect(
            matched,
            "x_at_time and y_at_value must reproduce the submitted point coordinates");
    }
}

void test_graph_handle_geometry_and_pointer_mapping(TestSuite& suite) {
    constexpr graph::PlotRect rect{100.0, 40.0, 700.0, 340.0};
    // A negative view start and a sub-unit value scale, so the mapping is
    // exercised away from any accidental identity.
    const graph::View view{-0.25, 200.0, 10.0, 0.5};

    // --- Seeding. ---
    suite.expect(
        graph::seed_control_points(
            graph::SegmentKind::Linear, {0.9, 0.9, 0.1, 0.1}) ==
                graph::kLinearEquivalentControlPoints &&
            graph::seed_control_points(
                graph::SegmentKind::Stepped, {0.9, 0.9, 0.1, 0.1}) ==
                graph::kLinearEquivalentControlPoints &&
            graph::seed_control_points(
                graph::SegmentKind::Cubic, {0.9, 0.9, 0.1, 0.1}) ==
                std::array<double, 4>{0.9, 0.9, 0.1, 0.1},
        "only a cubic segment keeps its stored control points");

    const auto seeded = marrow::runtime::Interpolation::cubic_bezier(
        graph::kLinearEquivalentControlPoints[0],
        graph::kLinearEquivalentControlPoints[1],
        graph::kLinearEquivalentControlPoints[2],
        graph::kLinearEquivalentControlPoints[3]);
    suite.expect(
        near(seeded.transform(0.25), 0.25, 1e-3) &&
            near(seeded.transform(0.75), 0.75, 1e-3),
        "the conversion seed must evaluate identically to linear");

    // --- Segment frames. ---
    const auto varying_track = make_scalar_track(
        marrow::runtime::Interpolation::linear(), 8.0, 12.0);
    const auto frame = graph::make_segment_frame(varying_track, 0U, 0U, view);
    suite.expect(
        frame.has_value() && !frame->flat_value_span &&
            near(frame->value_span, 4.0, 1e-12) &&
            near(frame->time_span, 1.0, 1e-12) &&
            frame->start_value == 8.0 && frame->end_value == 12.0,
        "a varying segment must use its own value span");

    // 1.0 -> 1.0 with pixels_per_value = 0.5 is flat in every view.
    const auto flat_track = make_scalar_track(
        marrow::runtime::Interpolation::linear(), 1.0, 1.0);
    const auto flat = graph::make_segment_frame(flat_track, 0U, 0U, view);
    suite.expect(
        flat.has_value() && flat->flat_value_span &&
            near(
                flat->value_span,
                graph::kFlatSegmentHandlePixels / view.pixels_per_value,
                1e-12) &&
            flat->value_span > 0.0,
        "a flat segment must substitute the positive fallback span");

    // A raw span of 1e-9 units is 5e-10 px at this scale: still flat, and the
    // substituted span must stay positive even though the raw span is negative.
    const auto near_flat_track = make_scalar_track(
        marrow::runtime::Interpolation::linear(), 1.0, 1.0 - 1e-9);
    const auto near_flat = graph::make_segment_frame(near_flat_track, 0U, 0U, view);
    suite.expect(
        near_flat.has_value() && near_flat->flat_value_span &&
            near_flat->value_span > 0.0,
        "a sub-pixel value span must be treated as flat with a positive span");

    auto zero_duration_track = varying_track;
    zero_duration_track.keys[1].time_seconds = zero_duration_track.keys[0].time_seconds;
    suite.expect(
        !graph::make_segment_frame(zero_duration_track, 0U, 0U, view).has_value(),
        "a zero-duration segment must have no frame");
    suite.expect(
        !graph::make_segment_frame(varying_track, 0U, 9U, view).has_value(),
        "an out-of-range component index must have no frame");
    suite.expect(
        !graph::make_segment_frame(varying_track, 1U, 0U, view).has_value(),
        "the last key must have no outgoing frame");
    auto nonfinite_track = varying_track;
    nonfinite_track.keys[1].values[0] = std::numeric_limits<double>::quiet_NaN();
    suite.expect(
        !graph::make_segment_frame(nonfinite_track, 0U, 0U, view).has_value(),
        "a non-finite anchor must have no frame");

    // --- Handle geometry. ---
    const auto handles = graph::build_handle_geometry(
        varying_track, varying_track.keys[0].identity, 0U, view, rect);
    suite.expect(
        handles.has_value() && handles->kind == graph::SegmentKind::Linear &&
            handles->control_points == graph::kLinearEquivalentControlPoints &&
            handles->key_index == 0U && handles->component_index == 0U,
        "a linear segment must expose seeded handles");
    if (!handles.has_value()) return;
    suite.expect(
        near(
            handles->first_handle.x,
            graph::x_at_time(
                rect,
                view,
                handles->frame.start_time_seconds +
                    handles->control_points[0] * handles->frame.time_span),
            1e-9) &&
            near(
                handles->first_handle.y,
                graph::y_at_value(
                    rect,
                    view,
                    handles->frame.start_value +
                        handles->control_points[1] * handles->frame.value_span),
                1e-9),
        "handle 1 must sit at cx1/cy1 along the frozen frame");
    suite.expect(
        near(
            handles->second_handle.x,
            graph::x_at_time(
                rect,
                view,
                handles->frame.start_time_seconds +
                    handles->control_points[2] * handles->frame.time_span),
            1e-9) &&
            near(
                handles->second_handle.y,
                graph::y_at_value(
                    rect,
                    view,
                    handles->frame.start_value +
                        handles->control_points[3] * handles->frame.value_span),
                1e-9),
        "handle 2 must sit at cx2/cy2 along the frozen frame");
    suite.expect(
        near(
            handles->start_anchor.x,
            graph::x_at_time(rect, view, handles->frame.start_time_seconds),
            1e-9) &&
            near(
                handles->end_anchor.x,
                graph::x_at_time(rect, view, handles->frame.end_time_seconds),
                1e-9),
        "the anchors must sit on the segment's two keys");
    suite.expect(
        !graph::build_handle_geometry(
             varying_track, varying_track.keys.back().identity, 0U, view, rect)
             .has_value(),
        "the last key must expose no outgoing handles");
    suite.expect(
        !graph::build_handle_geometry(
             varying_track, varying_track.keys[0].identity, 3U, view, rect)
             .has_value(),
        "an out-of-range component must expose no handles");
    model::KeyRef stranger{"bone:9:Translate", 0, 0U, 1U};
    suite.expect(
        !graph::build_handle_geometry(varying_track, stranger, 0U, view, rect)
             .has_value(),
        "a key from another track must expose no handles");
    suite.expect(
        !graph::build_handle_geometry(
             zero_duration_track, zero_duration_track.keys[0].identity, 0U, view, rect)
             .has_value(),
        "a zero-duration segment must expose no handles");

    // --- Render/drag agreement: the handle X coordinates must land on the
    // same mapping the rendered polyline uses. ---
    const auto cubic_track = make_scalar_track(
        marrow::runtime::Interpolation::cubic_bezier(0.25, 0.1, 0.75, 0.9), 8.0, 12.0);
    const auto cubic_handles = graph::build_handle_geometry(
        cubic_track, cubic_track.keys[0].identity, 0U, view, rect);
    const auto cubic_geometry = graph::build_geometry(
        cubic_track, {true, false, false, false}, view, rect, 0.0);
    // Stored control points are float32, so the read-back is compared with the
    // narrowing tolerance rather than bitwise.
    suite.expect(
        cubic_handles.has_value() && cubic_geometry.has_value() &&
            cubic_handles->kind == graph::SegmentKind::Cubic &&
            near(cubic_handles->control_points[0], 0.25, 1e-6) &&
            near(cubic_handles->control_points[1], 0.1, 1e-6) &&
            near(cubic_handles->control_points[2], 0.75, 1e-6) &&
            near(cubic_handles->control_points[3], 0.9, 1e-6),
        "a cubic segment must expose its stored control points");
    if (cubic_handles.has_value() && cubic_geometry.has_value() &&
        !cubic_geometry->segments.empty()) {
        const auto& polyline = cubic_geometry->segments.front().polyline;
        suite.expect(
            polyline.size() >= 2U &&
                near(cubic_handles->start_anchor.x, polyline.front().x, 1e-9) &&
                near(cubic_handles->start_anchor.y, polyline.front().y, 1e-9) &&
                near(cubic_handles->end_anchor.x, polyline.back().x, 1e-9) &&
                near(cubic_handles->end_anchor.y, polyline.back().y, 1e-9),
            "the handle anchors and the rendered polyline must share one mapping");
        suite.expect(
            near(
                cubic_handles->first_handle.x,
                graph::x_at_time(rect, view, 0.0 + 0.25 * 1.0),
                1e-9) &&
                near(
                    cubic_handles->second_handle.x,
                    graph::x_at_time(rect, view, 0.0 + 0.75 * 1.0),
                    1e-9),
            "cubic handle X must be x_at_time of the normalized control time");
    }

    // --- Round trip, both handles, varying and flat. ---
    const auto moved = graph::control_points_from_handle_pointer(
        handles->frame, handles->control_points, graph::HandleIndex::Second,
        view, rect, handles->second_handle.x, handles->second_handle.y);
    suite.expect(
        moved.has_value() && near((*moved)[2], handles->control_points[2], 1e-9) &&
            near((*moved)[3], handles->control_points[3], 1e-9) &&
            (*moved)[0] == handles->control_points[0] &&
            (*moved)[1] == handles->control_points[1],
        "mapping a handle back onto itself must be identity and must not touch the other handle");
    const auto moved_first = graph::control_points_from_handle_pointer(
        handles->frame, handles->control_points, graph::HandleIndex::First,
        view, rect, handles->first_handle.x, handles->first_handle.y);
    suite.expect(
        moved_first.has_value() &&
            near((*moved_first)[0], handles->control_points[0], 1e-9) &&
            near((*moved_first)[1], handles->control_points[1], 1e-9) &&
            (*moved_first)[2] == handles->control_points[2] &&
            (*moved_first)[3] == handles->control_points[3],
        "handle 1 must round-trip and leave handle 2 byte-identical");

    const auto flat_handles = graph::build_handle_geometry(
        flat_track, flat_track.keys[0].identity, 0U, view, rect);
    suite.expect(
        flat_handles.has_value() && flat_handles->frame.flat_value_span,
        "a flat segment must still expose handles");
    if (flat_handles.has_value()) {
        const auto flat_moved = graph::control_points_from_handle_pointer(
            flat_handles->frame, flat_handles->control_points,
            graph::HandleIndex::First, view, rect,
            flat_handles->first_handle.x, flat_handles->first_handle.y);
        suite.expect(
            flat_moved.has_value() &&
                near((*flat_moved)[0], flat_handles->control_points[0], 1e-9) &&
                near((*flat_moved)[1], flat_handles->control_points[1], 1e-9),
            "a flat segment's handle must round-trip through the fallback span");
        // 100 logical pixels of upward travel is exactly cy = 1 by definition.
        const auto flat_up = graph::control_points_from_handle_pointer(
            flat_handles->frame, flat_handles->control_points,
            graph::HandleIndex::First, view, rect,
            flat_handles->first_handle.x,
            graph::y_at_value(rect, view, flat_handles->frame.start_value) -
                graph::kFlatSegmentHandlePixels);
        suite.expect(
            flat_up.has_value() && near((*flat_up)[1], 1.0, 1e-9),
            "100 logical pixels above the start anchor must be exactly cy = 1");
    }

    // --- X clamp, Y overshoot. ---
    const auto clamped_low = graph::control_points_from_handle_pointer(
        handles->frame, handles->control_points, graph::HandleIndex::First,
        view, rect, rect.min_x - 5000.0, handles->first_handle.y);
    const auto clamped_high = graph::control_points_from_handle_pointer(
        handles->frame, handles->control_points, graph::HandleIndex::First,
        view, rect, rect.max_x + 5000.0, handles->first_handle.y);
    suite.expect(
        clamped_low.has_value() && (*clamped_low)[0] == 0.0 &&
            clamped_high.has_value() && (*clamped_high)[0] == 1.0,
        "a pointer past either end must clamp cx to exactly 0 or 1");
    const auto overshoot_low = graph::control_points_from_handle_pointer(
        handles->frame, handles->control_points, graph::HandleIndex::First,
        view, rect, handles->first_handle.x,
        graph::y_at_value(
            rect, view, handles->frame.start_value - 2.5 * handles->frame.value_span));
    suite.expect(
        overshoot_low.has_value() && near((*overshoot_low)[1], -2.5, 1e-9),
        "cy must accept finite negative overshoot without clamping");
    const auto overshoot_high = graph::control_points_from_handle_pointer(
        handles->frame, handles->control_points, graph::HandleIndex::Second,
        view, rect, handles->second_handle.x,
        graph::y_at_value(
            rect, view, handles->frame.start_value + 3.75 * handles->frame.value_span));
    suite.expect(
        overshoot_high.has_value() && near((*overshoot_high)[3], 3.75, 1e-9),
        "cy must accept finite positive overshoot without clamping");

    // --- Non-finite and degenerate rejection. ---
    graph::View broken_value = view;
    broken_value.pixels_per_value = 0.0;
    graph::View broken_time = view;
    broken_time.pixels_per_second = 0.0;
    suite.expect(
        !graph::control_points_from_handle_pointer(
             handles->frame, handles->control_points, graph::HandleIndex::First,
             broken_value, rect, 300.0, 200.0).has_value() &&
            !graph::control_points_from_handle_pointer(
                 handles->frame, handles->control_points, graph::HandleIndex::First,
                 broken_time, rect, 300.0, 200.0).has_value() &&
            !graph::control_points_from_handle_pointer(
                 handles->frame, handles->control_points, graph::HandleIndex::First,
                 view, rect, std::numeric_limits<double>::quiet_NaN(), 200.0)
                 .has_value() &&
            !graph::control_points_from_handle_pointer(
                 handles->frame, handles->control_points, graph::HandleIndex::First,
                 view, rect, 300.0, std::numeric_limits<double>::infinity())
                 .has_value(),
        "a non-positive view scale or a non-finite pointer must reject the mapping");
    graph::SegmentFrame degenerate_frame = handles->frame;
    degenerate_frame.value_span = 0.0;
    suite.expect(
        !graph::control_points_from_handle_pointer(
             degenerate_frame, handles->control_points, graph::HandleIndex::First,
             view, rect, 300.0, 200.0).has_value(),
        "a zero value span must reject the mapping");

    // --- Hit test. ---
    // Inclusivity is asserted against the exactly representable distance the
    // offset actually produced, so double rounding of `x + 7.0` cannot decide
    // the outcome instead of the `<=` in the implementation.
    const double probe_x = handles->first_handle.x + 7.0;
    const double probe_distance = std::abs(probe_x - handles->first_handle.x);
    suite.expect(
        graph::hit_test_handle(
            *handles, probe_x, handles->first_handle.y, probe_distance).has_value() &&
            !graph::hit_test_handle(
                 *handles, probe_x, handles->first_handle.y,
                 std::nextafter(probe_distance, 0.0)).has_value(),
        "the handle hit test must be inclusive at exactly its radius");
    suite.expect(
        !graph::hit_test_handle(
             *handles, handles->first_handle.x + 7.5, handles->first_handle.y, 7.0)
             .has_value(),
        "a pointer beyond the radius must miss every handle");
    const auto first_hit = graph::hit_test_handle(
        *handles, handles->first_handle.x, handles->first_handle.y, 7.0);
    const auto second_hit = graph::hit_test_handle(
        *handles, handles->second_handle.x, handles->second_handle.y, 7.0);
    suite.expect(
        first_hit.has_value() && first_hit->handle == graph::HandleIndex::First &&
            first_hit->key == handles->key && second_hit.has_value() &&
            second_hit->handle == graph::HandleIndex::Second,
        "the handle hit test must return the nearer handle and the segment key");
    // Exactly representable coordinates, so the tie really is a tie.
    graph::HandleGeometry tie_geometry = *handles;
    tie_geometry.first_handle = {300.0, 200.0};
    tie_geometry.second_handle = {320.0, 200.0};
    const auto tie = graph::hit_test_handle(tie_geometry, 310.0, 200.0, 32.0);
    suite.expect(
        tie.has_value() && tie->handle == graph::HandleIndex::First,
        "an exact distance tie must resolve to the first handle");
    suite.expect(
        graph::hit_test_handle(tie_geometry, 318.0, 200.0, 32.0).has_value() &&
            graph::hit_test_handle(tie_geometry, 318.0, 200.0, 32.0)->handle ==
                graph::HandleIndex::Second,
        "the nearer handle must win when the two distances differ");
    suite.expect(
        !graph::hit_test_handle(
             *handles, std::numeric_limits<double>::quiet_NaN(),
             handles->first_handle.y, 7.0).has_value() &&
            !graph::hit_test_handle(
                 *handles, handles->first_handle.x, handles->first_handle.y, -1.0)
                 .has_value(),
        "a non-finite pointer or a negative radius must hit nothing");
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
    suite.run("drag axis lock and unit mapping", [&] {
        test_graph_drag_axis_and_unit_mapping(suite);
    });
    suite.run("handle geometry and pointer mapping", [&] {
        test_graph_handle_geometry_and_pointer_mapping(suite);
    });
    return suite.finish();
}
