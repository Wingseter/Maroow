#include "timeline_model.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
    return suite.finish();
}
