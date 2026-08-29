#include "timeline_model.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "curve_auto.hpp"

#include "marrow/editor/authoring.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/runtime/skeleton.hpp"

namespace model = marrow::editor::timeline_model;

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
            std::cout << "Timeline model: " << case_count_ << " cases passed\n";
            return 0;
        }
        std::cerr << "Timeline model: " << failures_ << " failure(s) across "
                  << case_count_ << " cases\n";
        return 1;
    }

private:
    std::string current_case_;
    int failures_{0};
    int case_count_{0};
};

bool near(double left, double right, double epsilon = 1e-9) {
    return std::abs(left - right) <= epsilon;
}

model::TrackRow track(std::vector<double> key_times) {
    return {
        "global:events",
        "Global / Events",
        "idle",
        std::move(key_times),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        std::nullopt};
}

void test_same_time_identity_and_reconciliation(TestSuite& suite) {
    const model::TrackRow original = track({0.5, 0.5, 1.0});
    const model::KeyRef first = model::key_ref(original, 0U);
    const model::KeyRef second = model::key_ref(original, 1U);
    suite.expect(!(first == second), "same-time keys must retain distinct ordinals");
    suite.expect(
        first.same_time_ordinal == 0U && second.same_time_ordinal == 1U &&
            first.same_time_count == 2U && second.same_time_count == 2U,
        "same-time identity must record ordinal and population");

    const model::TrackRow shifted = track({0.25, 0.5, 0.5, 1.0});
    suite.expect(
        model::key_index(shifted, first) == std::optional<std::size_t>(1U) &&
            model::key_index(shifted, second) == std::optional<std::size_t>(2U),
        "identity must survive unrelated insertion before the key time");

    std::vector<model::KeyRef> selection{first, first, second};
    std::optional<model::KeyRef> active = second;
    model::reconcile_selection(&selection, &active, {shifted});
    suite.expect(
        selection == std::vector<model::KeyRef>{first, second} &&
            active == std::optional<model::KeyRef>(second),
        "reconciliation must preserve order and a valid active identity while removing duplicates");

    const model::TrackRow reduced = track({0.5, 1.0});
    suite.expect(
        !model::key_index(reduced, first).has_value() &&
            !model::key_index(reduced, second).has_value(),
        "a changed same-time population must invalidate every ambiguous identity");
    model::reconcile_selection(&selection, &active, {reduced});
    suite.expect(
        selection.empty() && !active.has_value(),
        "invalid same-time identities must clear the active key");
}

void test_fixture_track_kinds_preserve_identifiers(TestSuite& suite) {
    const auto loaded = marrow::editor::load_project("assets/fixtures/player_idle.marrow");
    suite.expect(static_cast<bool>(loaded), "fixture project must load");
    if (!loaded) return;
    const auto* animation = loaded.skeleton_data->find_animation("idle");
    suite.expect(animation != nullptr, "idle animation must resolve");
    if (animation == nullptr) return;

    const std::vector<std::pair<std::string, model::TimelineTrackKind>> expected{
        {"bone:0:Translate", model::TimelineTrackKind::Translate},
        {"bone:1:Rotate", model::TimelineTrackKind::Rotate},
        {"bone:1:Translate", model::TimelineTrackKind::Translate},
        {"bone:1:Scale", model::TimelineTrackKind::Scale},
        {"bone:1:Shear", model::TimelineTrackKind::Shear},
        {"bone:2:Rotate", model::TimelineTrackKind::Rotate},
        {"slot:0:Attachment", model::TimelineTrackKind::SlotAttachment},
        {"slot:0:Color", model::TimelineTrackKind::SlotColor},
        {"slot:0:deform:body_mesh", model::TimelineTrackKind::Deform},
        {"global:draw-order", model::TimelineTrackKind::DrawOrder},
        {"global:events", model::TimelineTrackKind::Event},
    };
    const auto rows = model::build_tracks(*loaded.skeleton_data, *animation);
    suite.expect(rows.size() == expected.size(), "fixture track count must stay unchanged");
    for (const auto& [id, kind] : expected) {
        const model::TrackRow* row = model::find_track(rows, id);
        suite.expect(
            row != nullptr && row->id == id && row->kind == kind,
            "typed track kind must preserve the established fixture identifier");
    }
}

void test_parent_key_activation_and_active_fallback(TestSuite& suite) {
    const model::TrackRow row = track({0.1, 0.2, 0.3});
    const model::KeyRef first = model::key_ref(row, 0U);
    const model::KeyRef second = model::key_ref(row, 1U);
    const model::KeyRef third = model::key_ref(row, 2U);

    std::vector<model::KeyRef> selection{first, second};
    std::optional<model::KeyRef> active = first;

    model::apply_key_activation(&selection, &active, second, false);
    suite.expect(
        selection == std::vector<model::KeyRef>{first, second} &&
            active == std::optional<model::KeyRef>(second),
        "plain activation of a selected key must preserve the group and move active");

    model::apply_key_activation(&selection, &active, third, false);
    suite.expect(
        selection == std::vector<model::KeyRef>{third} &&
            active == std::optional<model::KeyRef>(third),
        "plain activation of an unselected key must replace selection");

    model::apply_key_activation(&selection, &active, first, true);
    suite.expect(
        selection == std::vector<model::KeyRef>{third, first} &&
            active == std::optional<model::KeyRef>(first),
        "additive insertion must append and activate the clicked key");

    model::apply_key_activation(&selection, &active, first, true);
    suite.expect(
        selection == std::vector<model::KeyRef>{third} &&
            active == std::optional<model::KeyRef>(third),
        "removing the active key must choose the last stable remaining key");

    const model::TrackRow reduced = track({0.1});
    model::reconcile_selection(&selection, &active, {reduced});
    suite.expect(
        selection.empty() && !active.has_value(),
        "reconciliation must clear an active key whose identity no longer resolves");
}

void test_clipboard_collision_and_order(TestSuite& suite) {
    using marrow::editor::EventKeyframeEdit;
    std::vector<EventKeyframeEdit> destination{
        {0.1, "keep-before"},
        {0.5, "replace-a"},
        {0.5, "replace-b"},
        {0.9, "keep-after"},
    };
    const std::vector<EventKeyframeEdit> source{
        {0.0, "pasted-a"},
        {0.0, "pasted-b"},
    };
    model::paste_keys_replace_collisions(&destination, source, 0.5, true);
    suite.expect(destination.size() == 4U, "paste must replace every target-time collision");
    suite.expect(
        destination.size() == 4U &&
            destination[0].event_name == "keep-before" &&
            destination[1].event_name == "pasted-a" &&
            destination[2].event_name == "pasted-b" &&
            destination[3].event_name == "keep-after",
        "event paste must preserve source order at the same time");
    suite.expect(
        destination.size() == 4U && near(destination[1].time, 0.5) &&
            near(destination[2].time, 0.5),
        "pasted event keys must share the shifted target time");
}

void test_retime_bounds_snap_and_completion(TestSuite& suite) {
    struct Key {
        double time;
    };
    const std::vector<Key> keys{{0.1}, {0.2}, {0.3}, {0.5}};
    const std::set<std::size_t> selected{1U, 2U};
    model::RetimeBounds bounds;
    model::include_retime_bounds(keys, 1U, 0.2, selected, 0.001, &bounds);
    model::include_retime_bounds(keys, 2U, 0.3, selected, 0.001, &bounds);
    suite.expect(
        near(bounds.minimum_delta, -0.099) && near(bounds.maximum_delta, 0.199),
        "multi-key retime bounds must ignore selected neighbors as one atomic group");

    const auto snapped = model::snap_delta_to_frames(0.101, 0.021, 60.0);
    const double expected = std::round((0.101 + 0.021) * 60.0) / 60.0 - 0.101;
    suite.expect(
        snapped.has_value() && near(*snapped, expected),
        "retime delta must snap the earliest key to the frame grid");
    suite.expect(
        !model::snap_delta_to_frames(0.0, 0.1, 0.0).has_value() &&
            !model::incremental_retime_delta(
                std::numeric_limits<double>::infinity(), 0.0).has_value(),
        "non-finite or invalid retime inputs must fail closed");
    suite.expect(
        model::incremental_retime_delta(0.25, 0.1) ==
            std::optional<double>(0.15),
        "controller delta must remain incremental across preview refreshes");

    struct CompletionCase {
        bool commit;
        bool changed;
        model::CompletionAction action;
        bool report_cancelled;
        std::size_t history_entries;
    };
    const std::vector<CompletionCase> cases{
        {true, true, model::CompletionAction::Commit, false, 1U},
        {true, false, model::CompletionAction::Cancel, false, 0U},
        {false, true, model::CompletionAction::Cancel, true, 0U},
    };
    for (const CompletionCase& test : cases) {
        const model::CompletionDecision decision =
            model::completion_decision(test.commit, test.changed);
        suite.expect(
            decision.action == test.action &&
                decision.report_cancelled == test.report_cancelled &&
                decision.history_entries == test.history_entries,
            "completion must preserve cancel, no-op, and one-undo grouping");
    }
}

void test_duration_growth(TestSuite& suite) {
    std::vector<marrow::editor::TransformTimelineEdit> edits{
        {"idle",
         "root",
         marrow::editor::TransformTimelineChannel::Translate,
         {{0.2}, {1.4}}},
        {"walk",
         "root",
         marrow::editor::TransformTimelineChannel::Translate,
         {{3.0}}},
    };
    double maximum = 0.8;
    model::include_animation_timeline_maximum(edits, "idle", &maximum);
    suite.expect(near(maximum, 1.4), "duration growth must include the last matching key");
    model::include_animation_timeline_maximum(edits, "missing", &maximum);
    suite.expect(near(maximum, 1.4), "unrelated animations must not change duration");
}

void test_imported_curve_materialization(TestSuite& suite) {
    marrow::editor::ProjectLoadResult loaded =
        marrow::editor::load_project("assets/fixtures/player_idle.marrow");
    suite.expect(static_cast<bool>(loaded), "fixture project must load");
    if (!loaded) {
        return;
    }

    suite.expect(
        loaded.project->find_transform_timeline_edit(
            "idle",
            "spine",
            marrow::editor::TransformTimelineChannel::Translate) == nullptr,
        "fixture translate track must begin as imported runtime-only data");
    marrow::editor::TransformTimelineEdit* materialized =
        marrow::editor::ensure_transform_timeline_edit(
            *loaded.project,
            *loaded.skeleton_data,
            "idle",
            "spine",
            marrow::editor::TransformTimelineChannel::Translate);
    suite.expect(materialized != nullptr, "imported translate track must materialize");
    if (materialized == nullptr) {
        return;
    }
    suite.expect(materialized->keyframes.size() == 3U, "materialization must copy every key");
    suite.expect(
        materialized->keyframes.size() == 3U &&
            near(materialized->keyframes[0].time, 0.0) &&
            near(materialized->keyframes[1].time, 0.5) &&
            near(materialized->keyframes[2].time, 1.0),
        "materialization must preserve imported key times");
    suite.expect(
        materialized->keyframes.size() == 3U &&
            materialized->keyframes[0].interpolation.kind() ==
                marrow::runtime::InterpolationKind::Linear &&
            materialized->keyframes[1].interpolation.kind() ==
                marrow::runtime::InterpolationKind::Stepped &&
            materialized->keyframes[2].interpolation.kind() ==
                marrow::runtime::InterpolationKind::Linear,
        "materialization must preserve imported curve kinds");
}

void test_atomic_selector_collision_rejection(TestSuite& suite) {
    marrow::editor::ProjectData project;
    project.transform_timeline_edits.push_back({
        "idle",
        "root",
        marrow::editor::TransformTimelineChannel::Rotate,
        {{0.1, 10.0}, {0.3, 30.0}},
    });
    marrow::editor::TimelineKeySelector selector;
    selector.kind = marrow::editor::TimelineKeyKind::Transform;
    selector.animation_name = "idle";
    selector.bone_name = "root";
    selector.transform_channel = marrow::editor::TransformTimelineChannel::Rotate;
    selector.time = 0.1;

    const std::string before = marrow::editor::serialize_project(project);
    const marrow::editor::TimelineRetimeResult result =
        marrow::editor::retime_keyframes(
            &project, {selector, selector}, 0.05, false, 60.0);
    suite.expect(!result, "duplicate selector collision must be rejected");
    suite.expect(
        result.error == "A timeline key was selected more than once.",
        "collision rejection must retain its error contract");
    suite.expect(
        marrow::editor::serialize_project(project) == before,
        "rejected multi-key retime must leave the project byte-for-byte unchanged");
}


// The graph value gesture must reuse these shared helpers rather than
// re-deriving completion or incremental-delta rules of its own.
void test_graph_value_gesture_completion_reuse(TestSuite& suite) {
    const auto unchanged = model::completion_decision(true, false);
    suite.expect(
        unchanged.action == model::CompletionAction::Cancel &&
            unchanged.history_entries == 0U,
        "an unchanged graph value gesture must cancel without a history entry");
    const auto changed = model::completion_decision(true, true);
    suite.expect(
        changed.action == model::CompletionAction::Commit &&
            changed.history_entries == 1U,
        "a changed graph value gesture must commit exactly one history entry");
    const auto cancelled = model::completion_decision(false, true);
    suite.expect(
        cancelled.action == model::CompletionAction::Cancel &&
            cancelled.history_entries == 0U && cancelled.report_cancelled,
        "an explicitly cancelled changed gesture must report its cancellation");

    const auto incremental = model::incremental_retime_delta(5.0, 3.0);
    suite.expect(
        incremental.has_value() && near(*incremental, 2.0),
        "the incremental delta must subtract the already applied delta");
    suite.expect(
        !model::incremental_retime_delta(
             std::numeric_limits<double>::quiet_NaN(), 0.0).has_value() &&
            !model::incremental_retime_delta(
                 1.0, std::numeric_limits<double>::infinity()).has_value(),
        "a non-finite requested or applied delta must reject the increment");
}

/**
 * @brief MAR-171: the pure monotone Fritsch-Carlson resolver.
 *
 * Every expected number is spelled out literally rather than recomputed from
 * `curve_auto`'s own constants, because a test that reads the constant it is
 * checking proves nothing.
 */
void test_automatic_curve_control_points(TestSuite& suite) {
    using marrow::editor::curve_auto::Sample;
    using marrow::editor::curve_auto::segment_control_points;
    using Points = std::array<double, 4>;

    constexpr double kX1 = 1.0 / 3.0;
    constexpr double kX2 = 2.0 / 3.0;

    const auto samples_from = [](const std::vector<double>& values,
                                 double spacing = 0.5) {
        std::vector<Sample> samples;
        samples.reserve(values.size());
        for (std::size_t index = 0U; index < values.size(); ++index) {
            samples.push_back(
                Sample{static_cast<double>(index) * spacing, values[index]});
        }
        return samples;
    };

    // Fewer than two samples yield no segment at all, which is not an error.
    const auto empty = segment_control_points({});
    suite.expect(
        empty.has_value() && empty->empty(),
        "an empty sample list must produce an empty vector, not an error");
    const auto single = segment_control_points({Sample{0.0, 5.0}});
    suite.expect(
        single.has_value() && single->empty(),
        "a one-sample track has no segment and must produce an empty vector");

    // Two points determine a line: a monotone interpolant with no further
    // information is exactly the straight normalized ramp.
    const auto pair = segment_control_points({Sample{0.0, 0.0}, Sample{1.0, 10.0}});
    suite.expect(
        pair.has_value() && pair->size() == 1U && near((*pair)[0][0], kX1, 1e-12) &&
            near((*pair)[0][1], kX1, 1e-12) && near((*pair)[0][2], kX2, 1e-12) &&
            near((*pair)[0][3], kX2, 1e-12),
        "two samples must resolve to exactly the linear-equivalent curve");

    // The design's worked fixture example: spine rotate 0/8/-2 over 0.5 s steps.
    const auto fixture = segment_control_points(
        {Sample{0.0, 0.0}, Sample{0.5, 8.0}, Sample{1.0, -2.0}});
    suite.expect(
        fixture.has_value() && fixture->size() == 2U &&
            near((*fixture)[0][0], kX1, 1e-12) && near((*fixture)[0][1], kX1, 1e-12) &&
            near((*fixture)[0][2], kX2, 1e-12) && near((*fixture)[0][3], 1.0, 1e-12),
        "the fixture example's first segment must be [1/3, 1/3, 2/3, 1]");
    suite.expect(
        fixture.has_value() && fixture->size() == 2U &&
            near((*fixture)[1][0], kX1, 1e-12) && near((*fixture)[1][1], 0.0, 1e-12) &&
            near((*fixture)[1][2], kX2, 1e-12) && near((*fixture)[1][3], kX2, 1e-12),
        "the fixture example's second segment must be [1/3, 0, 2/3, 2/3]");

    // The format invariant, checked bit-exactly and after float32 narrowing.
    const auto x_constants_hold = [&](const std::vector<Points>& entries) {
        for (const Points& entry : entries) {
            if (entry[0] != kX1 || entry[2] != kX2) return false;
            const auto narrowed_x1 =
                static_cast<marrow::runtime::AnimationScalar>(entry[0]);
            const auto narrowed_x2 =
                static_cast<marrow::runtime::AnimationScalar>(entry[2]);
            if (!(narrowed_x1 > 0.0f && narrowed_x1 < 1.0f)) return false;
            if (!(narrowed_x2 > 0.0f && narrowed_x2 < 1.0f)) return false;
        }
        return true;
    };
    const auto y_in_unit_range = [&](const std::vector<Points>& entries) {
        for (const Points& entry : entries) {
            for (const std::size_t index : {1U, 3U}) {
                if (!std::isfinite(entry[index])) return false;
                if (entry[index] < 0.0 || entry[index] > 1.0) return false;
            }
        }
        return true;
    };

    const auto ramp = segment_control_points(samples_from({0.0, 1.0, 3.0, 6.0, 10.0}));
    const auto spiky = segment_control_points(samples_from({0.0, 10.0, 0.5, 11.0, 0.0}));
    const auto plateau = segment_control_points(samples_from({5.0, 5.0, 9.0}));
    const auto repeated = segment_control_points(samples_from({5.0, 5.0, 5.0}));
    suite.expect(
        ramp.has_value() && spiky.has_value() && plateau.has_value() &&
            repeated.has_value() && x_constants_hold(*ramp) &&
            x_constants_hold(*spiky) && x_constants_hold(*plateau) &&
            x_constants_hold(*repeated),
        "every automatic curve must store cx1 = 1/3 and cx2 = 2/3 bit-exactly");
    suite.expect(
        ramp.has_value() && spiky.has_value() && plateau.has_value() &&
            repeated.has_value() && y_in_unit_range(*ramp) &&
            y_in_unit_range(*spiky) && y_in_unit_range(*plateau) &&
            y_in_unit_range(*repeated),
        "every automatic cy must stay inside [0, 1], so no curve overshoots");

    suite.expect(
        plateau.has_value() && plateau->size() == 2U &&
            near((*plateau)[0][1], kX1, 1e-12) && near((*plateau)[0][3], kX2, 1e-12),
        "a flat segment must resolve to the neutral linear-equivalent curve");
    suite.expect(
        plateau.has_value() && plateau->size() == 2U && (*plateau)[1][1] == 0.0,
        "a plateau must zero the shared tangent of the following segment");
    suite.expect(
        repeated.has_value() && repeated->size() == 2U &&
            near((*repeated)[0][1], kX1, 1e-12) &&
            near((*repeated)[0][3], kX2, 1e-12) &&
            near((*repeated)[1][1], kX1, 1e-12) &&
            near((*repeated)[1][3], kX2, 1e-12),
        "a fully repeated series must resolve to two exactly linear segments");

    // Monotonicity asserted against the real runtime solver, not the formula.
    bool sampling_is_monotone = true;
    if (spiky.has_value()) {
        for (const Points& entry : *spiky) {
            const auto easing = marrow::runtime::Interpolation::cubic_bezier(
                entry[0], entry[1], entry[2], entry[3]);
            double previous = -1.0;
            for (int step = 0; step <= 100; ++step) {
                const double alpha = static_cast<double>(step) / 100.0;
                const double value = static_cast<double>(
                    easing.transform(static_cast<marrow::runtime::AnimationScalar>(alpha)));
                if (!std::isfinite(value) || value < previous - 1e-6 ||
                    value < -1e-6 || value > 1.0 + 1e-6) {
                    sampling_is_monotone = false;
                }
                previous = value;
            }
            if (easing.transform(0.0f) != 0.0f || easing.transform(1.0f) != 1.0f) {
                sampling_is_monotone = false;
            }
        }
    }
    suite.expect(
        spiky.has_value() && sampling_is_monotone,
        "every resolved curve must sample finite, non-decreasing, and inside [0, 1]");

    // The clamp actually fires on a raw tangent ratio outside the disk.
    const auto clamped = segment_control_points(samples_from({0.0, 1.0, 1.0001}));
    bool clamp_holds = clamped.has_value() && !clamped->empty();
    if (clamped.has_value()) {
        for (const Points& entry : *clamped) {
            const double a = entry[1] * 3.0;
            const double b = (1.0 - entry[3]) * 3.0;
            if (std::hypot(a, b) > 3.0 + 1e-9) clamp_holds = false;
        }
    }
    suite.expect(clamp_holds, "the Fritsch-Carlson disk clamp must bound every tangent");

    // The std::hypot path: sqrt(a*a + b*b) would overflow and zero both sides.
    const auto extreme = segment_control_points(samples_from({0.0, 1e-300, 1e300}));
    suite.expect(
        extreme.has_value() && x_constants_hold(*extreme) && y_in_unit_range(*extreme),
        "an extreme secant ratio must still produce finite in-range control points");

    // Scale invariance: a and b are ratios, so the stored bytes cannot move.
    const auto scaled = segment_control_points(
        {Sample{0.0, 0.0}, Sample{500.0, -56.0}, Sample{1000.0, 14.0}});
    bool scale_invariant = scaled.has_value() && fixture.has_value() &&
        scaled->size() == fixture->size();
    if (scale_invariant) {
        for (std::size_t index = 0U; index < scaled->size(); ++index) {
            for (std::size_t axis = 0U; axis < 4U; ++axis) {
                if ((*scaled)[index][axis] != (*fixture)[index][axis]) {
                    scale_invariant = false;
                }
            }
        }
    }
    suite.expect(
        scale_invariant,
        "scaling every time by 1000 and every value by -7 must be bit-identical");

    // Determinism.
    const auto repeat_call = segment_control_points(samples_from({0.0, 10.0, 0.5, 11.0, 0.0}));
    bool deterministic = repeat_call.has_value() && spiky.has_value() &&
        repeat_call->size() == spiky->size();
    if (deterministic) {
        for (std::size_t index = 0U; index < repeat_call->size(); ++index) {
            for (std::size_t axis = 0U; axis < 4U; ++axis) {
                if ((*repeat_call)[index][axis] != (*spiky)[index][axis]) {
                    deterministic = false;
                }
            }
        }
    }
    suite.expect(deterministic, "two calls on the same input must be bit-identical");

    // Rejections, each atomic.
    const double nan_value = std::numeric_limits<double>::quiet_NaN();
    const double infinity = std::numeric_limits<double>::infinity();
    suite.expect(
        !segment_control_points({Sample{0.0, 0.0}, Sample{nan_value, 1.0}}).has_value(),
        "a non-finite time must reject the whole track");
    suite.expect(
        !segment_control_points({Sample{0.0, 0.0}, Sample{1.0, infinity}}).has_value(),
        "a non-finite value must reject the whole track");
    suite.expect(
        !segment_control_points({Sample{1.0, 0.0}, Sample{0.5, 1.0}}).has_value(),
        "a non-increasing time pair must reject the whole track");
    suite.expect(
        !segment_control_points({Sample{0.0, 0.0}, Sample{1e-7, 1.0}}).has_value(),
        "a segment shorter than the key time epsilon must reject the whole track");
}

} // namespace

int main() {
    TestSuite suite;
    suite.run("same-time identity and reconciliation", [&] {
        test_same_time_identity_and_reconciliation(suite);
    });
    suite.run("fixture track kinds preserve identifiers", [&] {
        test_fixture_track_kinds_preserve_identifiers(suite);
    });
    suite.run("parent key activation and active fallback", [&] {
        test_parent_key_activation_and_active_fallback(suite);
    });
    suite.run("clipboard collision and stable ordering", [&] {
        test_clipboard_collision_and_order(suite);
    });
    suite.run("retime bounds, snap, and completion", [&] {
        test_retime_bounds_snap_and_completion(suite);
    });
    suite.run("duration growth", [&] { test_duration_growth(suite); });
    suite.run("imported curve materialization", [&] {
        test_imported_curve_materialization(suite);
    });
    suite.run("atomic selector collision rejection", [&] {
        test_atomic_selector_collision_rejection(suite);
    });
    suite.run("graph value gesture completion reuse", [&] {
        test_graph_value_gesture_completion_reuse(suite);
    });
    suite.run("automatic curve control points", [&] {
        test_automatic_curve_control_points(suite);
    });
    return suite.finish();
}
