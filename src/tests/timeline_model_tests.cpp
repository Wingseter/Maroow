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

// ---------------------------------------------------------------------
// MAR-172 loop boundary key synchronization.
// ---------------------------------------------------------------------

/** @brief The fixture project plus its effective skeleton, for lane cases. */
struct LoopSyncFixture {
    marrow::editor::ProjectLoadResult loaded;

    bool ready() const { return static_cast<bool>(loaded); }
    marrow::editor::ProjectData project() const { return *loaded.project; }
    const marrow::runtime::SkeletonData& skeleton() const { return *loaded.skeleton_data; }
};

LoopSyncFixture load_loop_sync_fixture() {
    return LoopSyncFixture{
        marrow::editor::load_project("assets/fixtures/player_idle.marrow")};
}

marrow::editor::TimelineLaneSelector spine_rotate_lane() {
    marrow::editor::TimelineLaneSelector lane;
    lane.kind = marrow::editor::TimelineLaneKind::Transform;
    lane.animation_name = "idle";
    lane.bone_name = "spine";
    lane.transform_channel = marrow::editor::TransformTimelineChannel::Rotate;
    return lane;
}

const marrow::editor::TransformTimelineEdit* spine_rotate(
    const marrow::editor::ProjectData& project) {
    return project.find_transform_timeline_edit(
        "idle", "spine", marrow::editor::TransformTimelineChannel::Rotate);
}

/** @brief `idle` with an explicit duration and `spine`/`rotate` opted in. */
bool prepare_opted_in_idle(
    const LoopSyncFixture& fixture,
    double duration,
    marrow::editor::ProjectData* project_out,
    TestSuite& suite) {
    *project_out = fixture.project();
    const auto authored = marrow::editor::set_animation_duration(
        project_out, fixture.skeleton(), "idle", duration);
    suite.expect(static_cast<bool>(authored), "authoring an explicit duration must succeed");
    if (!authored) return false;
    const auto enabled = marrow::editor::set_timeline_loop_sync(
        project_out, fixture.skeleton(), {spine_rotate_lane()}, true);
    suite.expect(static_cast<bool>(enabled), "enabling loop sync must succeed");
    return static_cast<bool>(enabled);
}

void test_loop_boundary_default_off_and_idempotence(TestSuite& suite) {
    const LoopSyncFixture fixture = load_loop_sync_fixture();
    suite.expect(fixture.ready(), "fixture project must load");
    if (!fixture.ready()) return;

    // Default off does nothing at all, including no resolver pass: the stale
    // automatic curve seeded here is still stale afterwards.
    {
        marrow::editor::ProjectData project = fixture.project();
        auto* lane = project.transform_timeline_edits.data();
        (void)lane;
        auto* rotate = project.find_transform_timeline_edit(
            "idle", "spine", marrow::editor::TransformTimelineChannel::Rotate);
        rotate->keyframes.front().curve_mode = marrow::editor::TimelineCurveMode::Auto;
        rotate->keyframes.front().curve_driver =
            marrow::editor::TimelineScalarComponent::Angle;
        rotate->keyframes.front().interpolation =
            marrow::runtime::Interpolation::cubic_bezier(0.9, 0.1, 0.95, 0.05);
        const std::string before = marrow::editor::serialize_project(project);
        const auto result = marrow::editor::synchronize_loop_boundaries(
            &project, fixture.skeleton());
        suite.expect(static_cast<bool>(result), "default-off sync must not fail");
        suite.expect(result.lane_count == 0U, "default-off sync must find no lane");
        suite.expect(!result.changed, "default-off sync must change nothing");
        suite.expect(
            result.resolved_key_count == 0U,
            "default-off sync must not run the automatic-curve resolver");
        suite.expect(
            marrow::editor::serialize_project(project) == before,
            "default-off sync must leave the stale automatic curve stale");
    }

    // Idempotence: the second call reports no change and the project is
    // byte-identical, which is the two-phase termination argument asserted.
    {
        marrow::editor::ProjectData project;
        if (!prepare_opted_in_idle(fixture, 1.5, &project, suite)) return;
        const std::string before = marrow::editor::serialize_project(project);
        const auto again = marrow::editor::synchronize_loop_boundaries(
            &project, fixture.skeleton());
        suite.expect(static_cast<bool>(again), "a repeat sync must not fail");
        suite.expect(again.lane_count == 1U, "a repeat sync must still see the lane");
        suite.expect(
            again.synchronized_lane_count == 0U && again.created_key_count == 0U &&
                again.moved_key_count == 0U && again.rewritten_key_count == 0U &&
                !again.changed,
            "a repeat sync must report no change");
        suite.expect(
            marrow::editor::serialize_project(project) == before,
            "a repeat sync must leave the project byte-identical");
    }
}

void test_loop_boundary_create_adopt_move_rewrite(TestSuite& suite) {
    const LoopSyncFixture fixture = load_loop_sync_fixture();
    suite.expect(fixture.ready(), "fixture project must load");
    if (!fixture.ready()) return;

    // Create: a three-key lane gains a fourth at exactly float32(1.5) whose
    // value and easing are bit-equal to key 0's.
    marrow::editor::ProjectData project;
    if (!prepare_opted_in_idle(fixture, 1.5, &project, suite)) return;
    const auto* lane = spine_rotate(project);
    suite.expect(lane != nullptr && lane->loop_sync, "the lane must be opted in");
    if (lane == nullptr) return;
    suite.expect(lane->keyframes.size() == 4U, "create must append one boundary key");
    if (lane->keyframes.size() != 4U) return;
    const auto& first = lane->keyframes.front();
    const auto& boundary = lane->keyframes.back();
    suite.expect(
        boundary.time ==
            static_cast<double>(static_cast<marrow::runtime::AnimationScalar>(1.5)),
        "the boundary key must sit at exactly float32(duration)");
    suite.expect(boundary.angle == first.angle, "the mirror must be bit-exact on angle");
    suite.expect(
        boundary.interpolation.kind() == first.interpolation.kind() &&
            boundary.interpolation.kind() ==
                marrow::runtime::InterpolationKind::CubicBezier &&
            boundary.interpolation.cubic_bezier().cx1 ==
                first.interpolation.cubic_bezier().cx1 &&
            boundary.interpolation.cubic_bezier().cy1 ==
                first.interpolation.cubic_bezier().cy1 &&
            boundary.interpolation.cubic_bezier().cx2 ==
                first.interpolation.cubic_bezier().cx2 &&
            boundary.interpolation.cubic_bezier().cy2 ==
                first.interpolation.cubic_bezier().cy2,
        "the mirror must be bit-exact on all four control points, not within an epsilon");

    // Adopt: `aim`/`arm_l`/`rotate` is runtime-only, has an explicit duration
    // of 0.5, and already holds a key at 0.5 whose value equals key 0's.
    {
        marrow::editor::ProjectData adopted = fixture.project();
        marrow::editor::TimelineLaneSelector lane_selector;
        lane_selector.kind = marrow::editor::TimelineLaneKind::Transform;
        lane_selector.animation_name = "aim";
        lane_selector.bone_name = "arm_l";
        lane_selector.transform_channel = marrow::editor::TransformTimelineChannel::Rotate;
        suite.expect(
            marrow::editor::ensure_transform_timeline_edit(
                adopted,
                fixture.skeleton(),
                "aim",
                "arm_l",
                marrow::editor::TransformTimelineChannel::Rotate) != nullptr,
            "the runtime-only aim lane must materialize");
        const auto result = marrow::editor::set_timeline_loop_sync(
            &adopted, fixture.skeleton(), {lane_selector}, true);
        suite.expect(static_cast<bool>(result), "adoption must succeed");
        suite.expect(
            !result.lane_actions.empty() &&
                result.lane_actions.front() ==
                    marrow::editor::TimelineLoopBoundaryAction::Adopted,
            "an existing key at the boundary must be adopted, not created");
        suite.expect(result.created_key_count == 0U, "adoption must create no key");
        suite.expect(result.changed_lane_count == 1U, "adoption must flip exactly one flag");
        const auto* aim = adopted.find_transform_timeline_edit(
            "aim", "arm_l", marrow::editor::TransformTimelineChannel::Rotate);
        suite.expect(
            aim != nullptr && aim->keyframes.size() == 2U &&
                near(aim->keyframes[0].angle, 30.0) && near(aim->keyframes[1].angle, 30.0),
            "adoption must keep both key values at 30");
    }

    // Move: shrinking the duration moves the boundary key and nothing else.
    {
        marrow::editor::ProjectData moved = project;
        const auto shrink = marrow::editor::set_animation_duration(
            &moved, fixture.skeleton(), "idle", 1.2);
        suite.expect(
            static_cast<bool>(shrink),
            "an opted-in clip must stay shortenable above the spacing floor: " +
                shrink.error);
        const auto synced =
            marrow::editor::synchronize_loop_boundaries(&moved, fixture.skeleton());
        suite.expect(static_cast<bool>(synced) && synced.changed, "the shrink must resync");
        suite.expect(synced.moved_key_count == 1U, "the shrink must move exactly one key");
        const auto* after = spine_rotate(moved);
        suite.expect(
            after != nullptr && after->keyframes.size() == 4U &&
                after->keyframes[3].time ==
                    static_cast<double>(
                        static_cast<marrow::runtime::AnimationScalar>(1.2)) &&
                near(after->keyframes[0].time, 0.0) &&
                near(after->keyframes[1].time, 0.5) &&
                near(after->keyframes[2].time, 1.0),
            "only the boundary key may move");
    }

    // Rewrite: a first-key value change propagates to the boundary alone.
    {
        marrow::editor::ProjectData rewritten = project;
        auto* mutable_lane = rewritten.find_transform_timeline_edit(
            "idle", "spine", marrow::editor::TransformTimelineChannel::Rotate);
        mutable_lane->keyframes.front().angle = 17.5;
        const auto synced =
            marrow::editor::synchronize_loop_boundaries(&rewritten, fixture.skeleton());
        suite.expect(static_cast<bool>(synced) && synced.changed, "a value change must resync");
        suite.expect(
            synced.rewritten_key_count == 1U && synced.created_key_count == 0U &&
                synced.moved_key_count == 0U,
            "a value change must rewrite exactly one boundary key");
        const auto* after = spine_rotate(rewritten);
        suite.expect(
            after != nullptr && after->keyframes.size() == 4U &&
                near(after->keyframes[3].angle, 17.5) &&
                near(after->keyframes[1].angle, 8.0) &&
                near(after->keyframes[2].angle, -2.0),
            "only the boundary key's value may change");
    }

    // Single-key lane: one key at time zero becomes a two-key constant lane.
    {
        marrow::editor::ProjectData single = fixture.project();
        auto* mutable_lane = single.find_transform_timeline_edit(
            "idle", "spine", marrow::editor::TransformTimelineChannel::Rotate);
        mutable_lane->keyframes.resize(1U);
        const auto authored = marrow::editor::set_animation_duration(
            &single, fixture.skeleton(), "idle", 1.5);
        suite.expect(static_cast<bool>(authored), "the single-key case needs a duration");
        const auto result = marrow::editor::set_timeline_loop_sync(
            &single, fixture.skeleton(), {spine_rotate_lane()}, true);
        suite.expect(static_cast<bool>(result), "a single-key lane must be acceptable");
        const auto* after = spine_rotate(single);
        suite.expect(
            after != nullptr && after->keyframes.size() == 2U &&
                near(after->keyframes[1].time, 1.5),
            "a single-key lane must gain exactly one boundary key");
    }
}

void test_loop_boundary_never_promotes_an_authored_key(TestSuite& suite) {
    const LoopSyncFixture fixture = load_loop_sync_fixture();
    suite.expect(fixture.ready(), "fixture project must load");
    if (!fixture.ready()) return;

    // A hand-edited document, or any future caller that reaches the sync with an
    // opted-in lane whose managed key is gone, must not have its last authored
    // key promoted into the boundary -- moved to the duration and overwritten
    // from key 0. The contract owns the last key only when that key satisfies
    // one half of the contract: it already sits at the boundary, or it is still
    // the bit-exact mirror of key 0 a previous sync wrote.
    marrow::editor::ProjectData project;
    if (!prepare_opted_in_idle(fixture, 1.5, &project, suite)) return;
    auto* lane = project.find_transform_timeline_edit(
        "idle", "spine", marrow::editor::TransformTimelineChannel::Rotate);
    suite.expect(
        lane != nullptr && lane->keyframes.size() == 4U,
        "the promotion case needs its created boundary key");
    if (lane == nullptr || lane->keyframes.size() != 4U) return;
    lane->keyframes.pop_back();

    const auto result =
        marrow::editor::synchronize_loop_boundaries(&project, fixture.skeleton());
    suite.expect(static_cast<bool>(result), "the sync must accept a missing boundary");
    suite.expect(
        result.created_key_count == 1U && result.moved_key_count == 0U,
        "a missing boundary key must be created, never promoted from an authored key");
    const auto* after = project.find_transform_timeline_edit(
        "idle", "spine", marrow::editor::TransformTimelineChannel::Rotate);
    suite.expect(
        after != nullptr && after->keyframes.size() == 4U,
        "the lane must regain a fourth key rather than keep three");
    if (after == nullptr || after->keyframes.size() != 4U) return;
    suite.expect(
        near(after->keyframes[2].time, 1.0) && near(after->keyframes[2].angle, -2.0),
        "the authored key at 1.0 must keep its time and its value");
    suite.expect(
        after->keyframes[3].time ==
                static_cast<double>(
                    static_cast<marrow::runtime::AnimationScalar>(1.5)) &&
            after->keyframes[3].angle == after->keyframes[0].angle,
        "the created boundary key must mirror key 0 at the duration");
}

void test_loop_boundary_rejections(TestSuite& suite) {
    const LoopSyncFixture fixture = load_loop_sync_fixture();
    suite.expect(fixture.ready(), "fixture project must load");
    if (!fixture.ready()) return;

    const auto expect_rejected = [&](marrow::editor::ProjectData project,
                                     std::vector<marrow::editor::TimelineLaneSelector> lanes,
                                     std::string_view needle,
                                     std::string_view message) {
        const std::string before = marrow::editor::serialize_project(project);
        const auto result = marrow::editor::set_timeline_loop_sync(
            &project, fixture.skeleton(), lanes, true);
        suite.expect(!result, message);
        suite.expect(
            result.error.find(needle) != std::string::npos,
            "the rejection message must name the reason");
        suite.expect(
            marrow::editor::serialize_project(project) == before,
            "a rejected enable must leave the project byte-identical");
    };

    // No explicit duration -- the fixture's `idle` has none.
    expect_rejected(
        fixture.project(), {spine_rotate_lane()}, "explicit animation duration",
        "enabling without an explicit duration must be rejected");

    // No key at time zero -- `arm_l`'s first key is at 0.25.
    {
        marrow::editor::ProjectData project = fixture.project();
        const auto authored = marrow::editor::set_animation_duration(
            &project, fixture.skeleton(), "idle", 1.5);
        suite.expect(static_cast<bool>(authored), "the time-zero case needs a duration");
        marrow::editor::TimelineLaneSelector arm = spine_rotate_lane();
        arm.bone_name = "arm_l";
        expect_rejected(
            project, {arm}, "key at time zero",
            "enabling a lane whose first key is not at zero must be rejected");
    }

    // A duration below the non-event spacing leaves no room for two keys.
    {
        marrow::editor::ProjectData project = fixture.project();
        auto* lane = project.find_transform_timeline_edit(
            "idle", "spine", marrow::editor::TransformTimelineChannel::Rotate);
        lane->keyframes.resize(1U);
        marrow::editor::AnimationEdit edit;
        edit.kind = marrow::editor::AnimationEditKind::SetDuration;
        edit.name = "idle";
        edit.duration = 0.0;
        project.animation_edits.push_back(edit);
        expect_rejected(
            project, {spine_rotate_lane()}, "one millisecond of room",
            "enabling a zero-duration animation must be rejected");
    }

    // A key past the boundary, and a key crowding it, on the enable path.
    {
        marrow::editor::ProjectData base = fixture.project();
        const auto authored = marrow::editor::set_animation_duration(
            &base, fixture.skeleton(), "idle", 1.5);
        suite.expect(static_cast<bool>(authored), "the past-boundary case needs a duration");
        auto* stretched = base.find_transform_timeline_edit(
            "idle", "spine", marrow::editor::TransformTimelineChannel::Rotate);
        stretched->keyframes.back().time = 2.0;
        expect_rejected(
            base, {spine_rotate_lane()}, "past its boundary",
            "a key past the boundary must be rejected");

        marrow::editor::ProjectData crowded = fixture.project();
        const auto crowded_duration = marrow::editor::set_animation_duration(
            &crowded, fixture.skeleton(), "idle", 1.0005);
        suite.expect(
            static_cast<bool>(crowded_duration), "the crowding case needs a duration");
        expect_rejected(
            crowded, {spine_rotate_lane()}, "within one millisecond",
            "a key inside the one-millisecond spacing must be rejected");
    }

    // Two keys at the boundary time.
    {
        marrow::editor::ProjectData project = fixture.project();
        const auto authored = marrow::editor::set_animation_duration(
            &project, fixture.skeleton(), "idle", 1.0);
        suite.expect(static_cast<bool>(authored), "the duplicate case needs a duration");
        auto* lane = project.find_transform_timeline_edit(
            "idle", "spine", marrow::editor::TransformTimelineChannel::Rotate);
        lane->keyframes.push_back(lane->keyframes.back());
        expect_rejected(
            project, {spine_rotate_lane()}, "keys at its loop boundary",
            "duplicate keys at the boundary must be rejected");
    }

    // Duplicate, unresolvable, and empty lane lists.
    {
        marrow::editor::ProjectData project;
        if (!prepare_opted_in_idle(fixture, 1.5, &project, suite)) return;
        const std::string before = marrow::editor::serialize_project(project);
        const auto duplicated = marrow::editor::set_timeline_loop_sync(
            &project, fixture.skeleton(), {spine_rotate_lane(), spine_rotate_lane()}, false);
        suite.expect(!duplicated, "a duplicate lane selector must be rejected");
        marrow::editor::TimelineLaneSelector missing = spine_rotate_lane();
        missing.bone_name = "no_such_bone";
        const auto unresolved = marrow::editor::set_timeline_loop_sync(
            &project, fixture.skeleton(), {missing}, true);
        suite.expect(!unresolved, "an unresolvable lane selector must be rejected");
        const auto empty = marrow::editor::set_timeline_loop_sync(
            &project, fixture.skeleton(), {}, true);
        suite.expect(!empty, "an empty lane list must be rejected");
        suite.expect(
            marrow::editor::serialize_project(project) == before,
            "every rejected selector list must leave the project byte-identical");
    }

    // Disable never validates. Every state rejected above disables cleanly and
    // the boundary key stays in place.
    {
        marrow::editor::ProjectData project;
        if (!prepare_opted_in_idle(fixture, 1.5, &project, suite)) return;
        // Strand the lane: remove the key at time zero, which the sync rejects.
        auto* lane = project.find_transform_timeline_edit(
            "idle", "spine", marrow::editor::TransformTimelineChannel::Rotate);
        lane->keyframes.erase(lane->keyframes.begin());
        const auto stranded =
            marrow::editor::synchronize_loop_boundaries(&project, fixture.skeleton());
        suite.expect(!stranded, "a lane with no key at zero must reject the sync");
        suite.expect(
            stranded.error.find("key at time zero") != std::string::npos,
            "the sync rejection must name the missing time-zero key");

        const std::size_t key_count =
            spine_rotate(project) != nullptr ? spine_rotate(project)->keyframes.size() : 0U;
        const auto released = marrow::editor::set_timeline_loop_sync(
            &project, fixture.skeleton(), {spine_rotate_lane()}, false);
        suite.expect(
            static_cast<bool>(released) && released.changed,
            "disabling must succeed in an unsatisfiable state");
        suite.expect(
            !released.lane_actions.empty() &&
                released.lane_actions.front() ==
                    marrow::editor::TimelineLoopBoundaryAction::Released,
            "disabling must report Released");
        const auto* after = spine_rotate(project);
        suite.expect(
            after != nullptr && !after->loop_sync && after->keyframes.size() == key_count,
            "disabling must clear the flag and leave every key in place");
    }
}

void test_loop_boundary_retime_pinning(TestSuite& suite) {
    const LoopSyncFixture fixture = load_loop_sync_fixture();
    suite.expect(fixture.ready(), "fixture project must load");
    if (!fixture.ready()) return;
    marrow::editor::ProjectData project;
    if (!prepare_opted_in_idle(fixture, 1.5, &project, suite)) return;

    const auto selector = [](double time) {
        marrow::editor::TimelineKeySelector key;
        key.kind = marrow::editor::TimelineKeyKind::Transform;
        key.animation_name = "idle";
        key.bone_name = "spine";
        key.transform_channel = marrow::editor::TransformTimelineChannel::Rotate;
        key.time = time;
        return key;
    };
    const auto retime = [&](std::vector<marrow::editor::TimelineKeySelector> keys,
                            double delta) {
        marrow::editor::ProjectData candidate = project;
        return marrow::editor::retime_keyframes(&candidate, keys, delta, false, 60.0);
    };

    const auto first_key = retime({selector(0.0)}, 0.1);
    suite.expect(
        static_cast<bool>(first_key) && !first_key.changed &&
            near(first_key.applied_delta, 0.0),
        "the first key of an opted-in lane is immovable");
    const auto last_key = retime({selector(1.5)}, -0.1);
    suite.expect(
        static_cast<bool>(last_key) && !last_key.changed &&
            near(last_key.applied_delta, 0.0),
        "the managed boundary key is immovable");
    const auto mixed = retime({selector(0.5), selector(1.5)}, -0.1);
    suite.expect(
        static_cast<bool>(mixed) && !mixed.changed && near(mixed.applied_delta, 0.0),
        "one pinned key freezes the whole selection");
    const auto middle = retime({selector(0.5)}, 0.1);
    suite.expect(
        static_cast<bool>(middle) && middle.changed && near(middle.applied_delta, 0.1),
        "a selection of only middle keys still moves by the full delta");

    // A lane that is not opted in behaves exactly as before.
    marrow::editor::ProjectData plain = fixture.project();
    const auto unpinned = marrow::editor::retime_keyframes(
        &plain, {selector(0.0)}, 0.1, false, 60.0);
    suite.expect(
        static_cast<bool>(unpinned) && unpinned.changed && near(unpinned.applied_delta, 0.1),
        "a lane that is not opted in retimes its first key exactly as before");
}

void test_loop_boundary_inferred_duration_floor(TestSuite& suite) {
    const LoopSyncFixture fixture = load_loop_sync_fixture();
    suite.expect(fixture.ready(), "fixture project must load");
    if (!fixture.ready()) return;

    // The fast path is bit-exact for every animation of a project with no
    // opted-in lane: one with a runtime-only lane, one with a project overlay,
    // and one with both.
    const marrow::editor::ProjectData untouched = fixture.project();
    for (const auto& animation : fixture.skeleton().animations()) {
        const double excluding =
            marrow::editor::inferred_duration_excluding_loop_boundaries(
                untouched, fixture.skeleton(), animation);
        suite.expect(
            excluding == animation.inferred_duration(),
            "the excluding floor must be bit-exact with no lane opted in");
    }

    marrow::editor::ProjectData project;
    if (!prepare_opted_in_idle(fixture, 1.5, &project, suite)) return;
    const auto* idle = fixture.skeleton().find_animation("idle");
    suite.expect(idle != nullptr, "the fixture must carry the idle animation");
    if (idle == nullptr) return;
    const double floor = marrow::editor::inferred_duration_excluding_loop_boundaries(
        project, fixture.skeleton(), *idle);
    suite.expect(
        near(floor, 1.0),
        "an opted-in lane must contribute its second-to-last key, not its boundary");

    // A single-key opted-in lane contributes nothing.
    marrow::editor::ProjectData single = project;
    auto* lane = single.find_transform_timeline_edit(
        "idle", "spine", marrow::editor::TransformTimelineChannel::Rotate);
    lane->keyframes.resize(1U);
    const double single_floor =
        marrow::editor::inferred_duration_excluding_loop_boundaries(
            single, fixture.skeleton(), *idle);
    suite.expect(
        near(single_floor, 1.0),
        "the other lanes still hold the floor when the opted-in lane has one key");
}

void test_scale_ratio_math(TestSuite& suite) {
    using model::SelectionTimeSpan;

    // --- selection_time_span() --------------------------------------------
    model::TrackRow left;
    left.id = "bone:0:Rotate";
    left.kind = model::TimelineTrackKind::Rotate;
    left.key_times = {0.0, 0.5, 1.0};
    model::TrackRow right;
    right.id = "bone:1:Translate";
    right.kind = model::TimelineTrackKind::Translate;
    right.key_times = {0.25, 2.0};
    const std::vector<model::TrackRow> tracks{left, right};

    suite.expect(
        !model::selection_time_span({}, tracks).valid,
        "an empty selection has no span");
    suite.expect(
        !model::selection_time_span({model::key_ref(left, 1U)}, tracks).valid,
        "a single-key selection has no span");
    {
        model::TrackRow flat;
        flat.id = "bone:2:Scale";
        flat.kind = model::TimelineTrackKind::Scale;
        flat.key_times = {0.5, 0.5 + 1e-9};
        const std::vector<model::TrackRow> flat_tracks{flat};
        const auto span = model::selection_time_span(
            {model::key_ref(flat, 0U), model::key_ref(flat, 1U)}, flat_tracks);
        suite.expect(
            !span.valid && span.key_count == 2U,
            "keys sharing one time within the epsilon have no span");
    }
    {
        const auto span = model::selection_time_span(
            {model::key_ref(left, 0U), model::key_ref(right, 1U),
             model::key_ref(left, 1U)},
            tracks);
        suite.expect(
            span.valid && span.key_count == 3U && span.minimum_time == 0.0 &&
                span.maximum_time == 2.0,
            "the span spreads across two tracks");
    }
    {
        model::KeyRef stale;
        stale.track_id = "bone:9:Rotate";
        stale.time_microseconds = 1;
        const auto span = model::selection_time_span(
            {model::key_ref(left, 0U), stale, model::key_ref(left, 2U)}, tracks);
        suite.expect(
            span.valid && span.key_count == 2U && span.maximum_time == 1.0,
            "an unresolvable ref is ignored rather than counted");
    }

    // --- scale_from_edge_time() -------------------------------------------
    const double infinity = std::numeric_limits<double>::infinity();
    const double nan_value = std::numeric_limits<double>::quiet_NaN();
    suite.expect(
        model::scale_from_edge_time(0.0, 1.0, 1.25).value_or(0.0) == 1.25,
        "dragging the late edge out gives the positive ratio");
    suite.expect(
        model::scale_from_edge_time(1.0, 0.0, 0.5).value_or(0.0) == 0.5,
        "dragging the early edge in gives the positive ratio");
    suite.expect(
        !model::scale_from_edge_time(0.0, 1.0, 0.0).has_value(),
        "a target exactly on the pivot yields no ratio");
    suite.expect(
        !model::scale_from_edge_time(0.0, 1.0, -0.5).has_value(),
        "a target past the pivot yields no ratio");
    suite.expect(
        !model::scale_from_edge_time(0.0, 0.0, 1.0).has_value(),
        "a degenerate span yields no ratio");
    suite.expect(
        !model::scale_from_edge_time(nan_value, 1.0, 1.25).has_value() &&
            !model::scale_from_edge_time(0.0, nan_value, 1.25).has_value() &&
            !model::scale_from_edge_time(0.0, 1.0, infinity).has_value(),
        "every non-finite input yields no ratio");

    // --- snap_scale_to_frames() -------------------------------------------
    for (const double fps : {24.0, 30.0, 60.0}) {
        const double frame = 1.0 / fps;
        const auto snapped = model::snap_scale_to_frames(0.0, 1.0, 1.23456, fps);
        suite.expect(snapped.has_value(), "a positive snapped ratio must exist");
        if (!snapped.has_value()) continue;
        const double edge = 0.0 + (1.0 - 0.0) * *snapped;
        suite.expect(
            std::abs(edge / frame - std::round(edge / frame)) < 1e-9,
            "the moved edge lands on a frame boundary");
        // The reuse is real: the same edge time comes from the shared helper.
        const double shared = 1.0 +
            *model::snap_delta_to_frames(1.0, 1.0 * 1.23456 - 1.0, fps);
        suite.expect(
            std::abs(edge - shared) <= 1e-12,
            "snap_scale_to_frames must agree with snap_delta_to_frames");
        // Round trip through scale_from_edge_time().
        const auto back = model::scale_from_edge_time(0.0, 1.0, edge);
        suite.expect(
            back.has_value() && std::abs(*back - *snapped) <= 1e-12,
            "the snapped ratio round-trips through scale_from_edge_time");
    }
    {
        // RangeEnd: the pivot is late, the moved edge early and below it.
        const auto snapped = model::snap_scale_to_frames(1.0, 0.0, 0.4321, 60.0);
        suite.expect(snapped.has_value(), "the RangeEnd direction snaps too");
        if (snapped.has_value()) {
            const double edge = 1.0 + (0.0 - 1.0) * *snapped;
            suite.expect(
                std::abs(edge * 60.0 - std::round(edge * 60.0)) < 1e-9,
                "the RangeEnd moved edge lands on a frame boundary");
        }
    }
    suite.expect(
        !model::snap_scale_to_frames(0.0, 1.0, 1.25, 0.0).has_value() &&
            !model::snap_scale_to_frames(0.0, 1.0, 1.25, -60.0).has_value() &&
            !model::snap_scale_to_frames(0.0, 1.0, -1.0, 60.0).has_value() &&
            !model::snap_scale_to_frames(0.0, 0.0, 1.25, 60.0).has_value() &&
            !model::snap_scale_to_frames(nan_value, 1.0, 1.25, 60.0).has_value(),
        "every degenerate snap input yields no ratio");
    suite.expect(
        !model::snap_scale_to_frames(0.0, 0.001, 0.1, 60.0).has_value(),
        "a snapped target landing on the pivot yields no ratio");

    // --- incremental_scale_ratio() ----------------------------------------
    suite.expect(
        model::incremental_scale_ratio(1.5, 0.5).value_or(0.0) == 3.0,
        "the incremental ratio is requested / applied");
    suite.expect(
        !model::incremental_scale_ratio(1.5, 0.0).has_value() &&
            !model::incremental_scale_ratio(-1.0, 1.0).has_value() &&
            !model::incremental_scale_ratio(nan_value, 1.0).has_value() &&
            !model::incremental_scale_ratio(1.0, infinity).has_value(),
        "non-finite or non-positive ratios on either side yield nothing");
    {
        // 5000 accepted frames sweeping the ratio, composed exactly as the
        // gesture composes them, must land on the last requested ratio.
        double applied = 1.0;
        double composed = 1.0;
        double requested = 1.0;
        for (int frame = 1; frame <= 5000; ++frame) {
            requested = 0.5 + 1.5 * (static_cast<double>(frame) / 5000.0);
            const auto step = model::incremental_scale_ratio(requested, applied);
            suite.expect(step.has_value(), "every sweep frame yields a ratio");
            if (!step.has_value()) break;
            composed *= *step;
            applied = requested;
        }
        suite.expect(
            std::abs(composed - requested) <= 1e-12,
            "5000 composed frames land on the requested ratio");
    }
}

} // namespace

/**
 * @brief MAR-185 U3: the clipboard's animation reference follows a rename.
 *
 * Pure, UI-free, no session and no frame -- nothing in the shell can stand in
 * for it, because `Clipboard` is plain data and the cascade is plain algebra.
 */
void test_clipboard_animation_cascade(TestSuite& suite) {
    const auto seeded = [] {
        model::Clipboard clipboard;
        clipboard.has_data = true;
        clipboard.animation_name = "toggle_inherit";
        clipboard.earliest_time = 0.25;
        clipboard.project_fragment.bone_inherit_timeline_edits.push_back(
            marrow::editor::BoneInheritTimelineEdit{
                "toggle_inherit",
                "child",
                {{0.25, marrow::runtime::BoneInherit::NoScale}}});
        return clipboard;
    };

    model::Clipboard clipboard = seeded();
    suite.expect(
        !model::clipboard_time_shift(clipboard, "toggle_two", 0.5).has_value(),
        "a clipboard naming another animation must refuse to paste before the "
        "cascade runs");

    model::cascade_animation_rename(&clipboard, "toggle_inherit", "toggle_two");
    suite.expect(
        clipboard.animation_name == "toggle_two",
        "the rename must remap the clipboard's animation, measured '" +
            clipboard.animation_name + "'");
    suite.expect(clipboard.has_data, "the rename must not clear the clipboard");
    suite.expect(
        near(clipboard.earliest_time, 0.25),
        "the rename must not move the clipboard's earliest time");
    suite.expect(
        clipboard.project_fragment.bone_inherit_timeline_edits.size() == 1U,
        "the rename must not touch the copied fragment");
    const auto shift = model::clipboard_time_shift(clipboard, "toggle_two", 0.5);
    suite.expect(
        shift.has_value() && near(*shift, 0.25),
        "after the cascade the clipboard must paste into the renamed animation");

    model::cascade_animation_rename(&clipboard, "some_other_animation", "x");
    suite.expect(
        clipboard.animation_name == "toggle_two",
        "renaming an unrelated animation must change nothing");

    model::cascade_animation_delete(&clipboard, "toggle_two");
    suite.expect(!clipboard.has_data, "the delete must clear the clipboard");
    suite.expect(
        clipboard.project_fragment.bone_inherit_timeline_edits.empty(),
        "the delete must clear the copied fragment");
    suite.expect(
        !model::clipboard_time_shift(clipboard, "toggle_two", 0.5).has_value(),
        "a cleared clipboard must refuse to paste");

    model::Clipboard untouched = seeded();
    model::cascade_animation_delete(&untouched, "some_other_animation");
    suite.expect(
        untouched.has_data && untouched.animation_name == "toggle_inherit" &&
            untouched.project_fragment.bone_inherit_timeline_edits.size() == 1U,
        "deleting an unrelated animation must leave the clipboard alone");
}

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
    suite.run("loop boundary default off and idempotence", [&] {
        test_loop_boundary_default_off_and_idempotence(suite);
    });
    suite.run("loop boundary create, adopt, move, and rewrite", [&] {
        test_loop_boundary_create_adopt_move_rewrite(suite);
    });
    suite.run("loop boundary never promotes an authored key", [&] {
        test_loop_boundary_never_promotes_an_authored_key(suite);
    });
    suite.run("loop boundary rejections and disable", [&] {
        test_loop_boundary_rejections(suite);
    });
    suite.run("loop boundary retime pinning", [&] {
        test_loop_boundary_retime_pinning(suite);
    });
    suite.run("loop boundary inferred duration floor", [&] {
        test_loop_boundary_inferred_duration_floor(suite);
    });
    suite.run("scale ratio math", [&] { test_scale_ratio_math(suite); });
    suite.run("clipboard animation cascade", [&] {
        test_clipboard_animation_cascade(suite);
    });
    return suite.finish();
}
