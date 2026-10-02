#include "../editor/shell_state.hpp"
#include "../editor/timeline_controller.hpp"
#include "../editor/shell_theme.hpp"

// Host-owned resources, normally defined by shell_main.cpp. This non-rendering
// test host does not load fonts or create a window; lifecycle code is unchanged.
namespace marrow::editor::shell {
ImFont* g_font_semibold = nullptr;
ImFont* g_font_small = nullptr;
ImFont* g_font_display = nullptr;
ImFont* g_font_mono = nullptr;
}

#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace marrow::editor;
using namespace marrow::editor::shell;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

struct Fixture {
    ShellState state;
    std::string project_before;

    Fixture() {
        require(static_cast<bool>(state.session.open("assets/fixtures/player_idle.marrow")),
                "fixture open failed");
        sync_shell_from_editor_session(&state);
        state.session.set_playing(false);
        sync_shell_from_editor_session(&state);
        project_before = serialize_project(*state.session.project());
    }

    void require_rolled_back() {
        require(serialize_project(*state.session.project()) == project_before,
                "cancel did not restore the authored project");
        require(!state.session.dirty() && !state.session.can_undo() &&
                    !state.session.can_redo(),
                "cancel changed dirty state or history");
        require(!authoring_gesture_active(state) && !state.session.transaction_active(),
                "cancel left an authoring owner or live transaction");
        auto next = state.session.begin_edit({EditKind::Generic, "Next edit", {}, false});
        require(static_cast<bool>(next), "cancel left the next edit blocked");
        next.cancel();
        const std::string status = state.status_message;
        cancel_authoring_gestures(&state, "second cancellation");
        require(state.status_message == status, "empty cancellation changed the status");
    }
};

void require_current_views(const ShellState& state) {
    require(state.preview_skeleton() == state.session.preview_skeleton(),
            "shell skeleton view is stale after session mutation");
    require(state.animation_state() == state.session.preview_animation_state(),
            "shell animation-state view is stale after session mutation");
}

void change_project(EditorSession::EditTransaction& transaction) {
    require(static_cast<bool>(transaction), "could not begin the test transaction");
    transaction.project()->editor_metadata.timeline.frames_per_second += 1.0;
}

template<class Gesture>
void exercise_transaction_owner(std::optional<Gesture> ShellState::*member) {
    Fixture fixture;
    auto& slot = fixture.state.*member;
    slot.emplace();
    slot->transaction = fixture.state.session.begin_edit(
        {EditKind::EditProperty, "Lifecycle transaction", {}, false});
    change_project(slot->transaction);
    require(authoring_gesture_active(fixture.state), "transaction owner is not active");
    cancel_authoring_gestures(&fixture.state, "owner test");
    require(!slot.has_value(), "cancel did not clear the transaction owner");
    fixture.require_rolled_back();
}

template<class Gesture>
void exercise_timeline_owner(std::optional<Gesture> TimelineEditorState::*member) {
    Fixture fixture;
    auto& slot = fixture.state.timeline_editor.*member;
    slot.emplace();
    slot->transaction = fixture.state.session.begin_edit(
        {EditKind::EditProperty, "Lifecycle graph transaction", {}, false});
    change_project(slot->transaction);
    require(authoring_gesture_active(fixture.state), "timeline owner is not active");
    cancel_authoring_gestures(&fixture.state, "timeline owner test");
    require(!slot.has_value(), "cancel did not clear the timeline owner");
    fixture.require_rolled_back();
}

void seed_selection(ShellState& state) {
    state.selection.replace(BoneSelection{"root"});
    state.selection.toggle(BoneSelection{"body"});
    state.hierarchy_selection_anchor = BoneSelection{"root"};
    state.selected_timeline_track_id = "original-track";
}

void require_selection_restored(const ShellState& state, const SelectionSet& before) {
    require(state.selection.items() == before.items() &&
                state.selection.active() != nullptr && before.active() != nullptr &&
                *state.selection.active() == *before.active(),
            "viewport cancel lost entity selection or its active member");
    require(state.hierarchy_selection_anchor ==
                std::optional<SelectionItem>{BoneSelection{"root"}} &&
                state.selected_timeline_track_id == std::optional<std::string>{"original-track"},
            "viewport cancel lost hierarchy anchor or timeline focus");
}

void exercise_viewport_transform() {
    Fixture fixture;
    auto& state = fixture.state;
    seed_selection(state);
    const SelectionSet selection = state.selection;
    state.viewport_transform_gesture.emplace();
    auto& gesture = *state.viewport_transform_gesture;
    gesture.selection_before = state.selection;
    gesture.hierarchy_anchor_before = state.hierarchy_selection_anchor;
    gesture.timeline_focus_before = state.selected_timeline_track_id;
    gesture.transaction = state.session.begin_edit({EditKind::MoveBone, "Transform", {}, false});
    change_project(gesture.transaction);
    require(static_cast<bool>(gesture.transaction.refresh_runtime()), "transform refresh failed");
    state.selection.clear();
    state.hierarchy_selection_anchor.reset();
    state.selected_timeline_track_id.reset();
    require(authoring_gesture_active(state), "viewport transform is not active");
    cancel_authoring_gestures(&state, "transform test");
    require_selection_restored(state, selection);
    require_current_views(state);
    fixture.require_rolled_back();
}

void exercise_viewport_ffd() {
    Fixture fixture;
    auto& state = fixture.state;
    seed_selection(state);
    const SelectionSet selection = state.selection;
    ViewportFfdSelection vertices;
    vertices.scope = {2U, 1U, "display-mesh", "deform-mesh", 8U};
    vertices.vertex_indices = {1U, 3U, 5U};
    state.viewport_ffd_selection = vertices;
    state.viewport_ffd_gesture.emplace();
    auto& gesture = *state.viewport_ffd_gesture;
    gesture.selection_before = selection;
    gesture.vertex_selection_before = vertices;
    gesture.hierarchy_anchor_before = state.hierarchy_selection_anchor;
    gesture.timeline_focus_before = state.selected_timeline_track_id;
    gesture.transaction = state.session.begin_edit({EditKind::EditProperty, "FFD", {}, false});
    change_project(gesture.transaction);
    require(static_cast<bool>(gesture.transaction.refresh_runtime()), "FFD refresh failed");
    state.selection.clear();
    state.viewport_ffd_selection.reset();
    state.hierarchy_selection_anchor.reset();
    state.selected_timeline_track_id.reset();
    require(authoring_gesture_active(state), "viewport FFD is not active");
    cancel_authoring_gestures(&state, "FFD test");
    require_selection_restored(state, selection);
    require(state.viewport_ffd_selection.has_value(), "FFD cancel lost vertex selection");
    const auto& restored = *state.viewport_ffd_selection;
    require(restored.vertex_indices == vertices.vertex_indices &&
                restored.scope.slot_index == vertices.scope.slot_index &&
                restored.scope.display_skin_index == vertices.scope.display_skin_index &&
                restored.scope.display_attachment_name == vertices.scope.display_attachment_name &&
                restored.scope.deform_attachment_name == vertices.scope.deform_attachment_name &&
                restored.scope.vertex_count == vertices.scope.vertex_count,
            "FFD cancel changed the vertex-selection identity");
    require_current_views(state);
    fixture.require_rolled_back();
}

void exercise_timeline_selection(bool scale) {
    Fixture fixture;
    auto& state = fixture.state;
    require(state.session.select_animation("idle"), "timeline animation missing");
    sync_shell_from_editor_session(&state);
    const auto* animation = selected_animation(state);
    require(animation != nullptr, "timeline animation selection failed");
    const auto tracks = build_timeline_tracks(*state.session.runtime_data(), *animation);
    const TimelineTrackRow* chosen = nullptr;
    for (const auto& track : tracks) {
        if (track.transform_channel.has_value() && track.key_times.size() >= 2U) {
            chosen = &track;
            break;
        }
    }
    require(chosen != nullptr, "fixture needs a multi-key transform track");
    for (std::size_t i = 0; i < chosen->key_times.size(); ++i) {
        state.timeline_editor.selected_keys.push_back(timeline_model::key_ref(*chosen, i));
    }
    state.timeline_editor.active_key = state.timeline_editor.selected_keys.back();
    const auto keys_before = state.timeline_editor.selected_keys;
    const auto active_before = state.timeline_editor.active_key;
    if (scale) {
        require(begin_timeline_scale_gesture(&state, 43U, TimelineScalePivot::RangeStart, tracks),
                "could not begin real scale gesture");
        require(apply_timeline_scale_ratio(&state, tracks, 1.2), "real scale failed");
    } else {
        require(begin_timeline_retime_gesture(&state, 42U, 0.0f, tracks),
                "could not begin real retime gesture");
        require(apply_timeline_retime_delta(&state, tracks, 0.1, false), "real retime failed");
    }
    require(!(state.timeline_editor.selected_keys == keys_before),
            "test gesture did not actually move key identities");
    require(authoring_gesture_active(state), "timeline gesture is not active");
    cancel_authoring_gestures(&state, "timeline selection test");
    require(state.timeline_editor.selected_keys == keys_before &&
                state.timeline_editor.active_key == active_before,
            "common cancellation did not restore pre-gesture timeline key selection");
    require_current_views(state);
    fixture.require_rolled_back();
}

void exercise_snapshot_owner(bool weight) {
    Fixture fixture;
    auto& state = fixture.state;
    state.preview_speed = 2.0;
    const auto snapshot = capture_history_snapshot(state);
    if (weight) {
        state.weight_paint_stroke.active = true;
        state.weight_paint_stroke.changed = true;
        state.weight_paint_stroke.before_snapshot = snapshot;
        state.weight_paint_stroke.has_last_sample = true;
    } else {
        state.pending_edit_action.emplace();
        state.pending_edit_action->before_snapshot = snapshot;
    }
    state.load_result.project->editor_metadata.timeline.frames_per_second += 1.0;
    require(rebuild_project_runtime(&state), "snapshot test runtime refresh failed");
    require(authoring_gesture_active(state), "snapshot owner is not active");
    cancel_authoring_gestures(&state, "snapshot test");
    require(!state.weight_paint_stroke.active && !state.weight_paint_stroke.has_last_sample,
            "weight cancellation left stroke sampling state");
    require(state.preview_speed == 2.0, "cancellation rewrote shell-private preview speed");
    require_current_views(state);
    fixture.require_rolled_back();
}
} // namespace

int main() {
    using Test = std::pair<const char*, std::function<void()>>;
    const std::vector<Test> tests{
        {"runtime views follow direct session replacement", [] {
            Fixture fixture;
            require(static_cast<bool>(fixture.state.session.open(
                        "assets/fixtures/parameter_face_basic.marrow")), "replacement failed");
            require_current_views(fixture.state);
        }},
        {"runtime views become null immediately on close", [] {
            Fixture fixture;
            require(fixture.state.session.close(), "session close failed");
            require_current_views(fixture.state);
        }},
        {"closed-session revision synchronizes working preview", [] {
            Fixture fixture;
            auto& state = fixture.state;
            require(state.session.select_animation("idle"), "idle missing");
            require(state.session.seek(0.3), "seek failed");
            sync_shell_from_editor_session(&state);
            require(state.session.close(), "close failed");
            sync_shell_from_editor_session_if_revised(&state);
            require(state.selected_animation_name.empty() && state.preview_skin_names.empty() &&
                        state.preview_slot_overrides.empty() && state.preview_events.empty() &&
                        state.timeline_time_seconds == 0.0 && !state.timeline_playing &&
                        !state.project_dirty,
                    "closed session left stale preview working values");
            require(state.observed_project_revision == state.session.project_revision() &&
                        state.observed_runtime_revision == state.session.runtime_revision() &&
                        state.observed_preview_revision == state.session.preview_revision(),
                    "closed session revisions were not observed");
        }},
        {"runtime views follow transaction refresh and cancel", [] {
            Fixture fixture;
            auto edit = fixture.state.session.begin_edit({EditKind::EditProperty, "Refresh", {}, false});
            change_project(edit);
            require(static_cast<bool>(edit.refresh_runtime()), "runtime refresh failed");
            require_current_views(fixture.state);
            edit.cancel();
            require_current_views(fixture.state);
            fixture.require_rolled_back();
        }},
        {"runtime views follow commit undo and redo", [] {
            Fixture fixture;
            auto& state = fixture.state;
            auto edit = state.session.begin_edit({EditKind::EditProperty, "Commit", {}, false});
            change_project(edit);
            require(static_cast<bool>(edit.commit()), "commit failed");
            require_current_views(state);
            require(static_cast<bool>(state.session.undo()), "undo failed");
            require_current_views(state);
            require(static_cast<bool>(state.session.redo()), "redo failed");
            require_current_views(state);
        }},
        {"failed open retains runtime views", [] {
            Fixture fixture;
            const auto* skeleton = fixture.state.preview_skeleton();
            const auto* animation = fixture.state.animation_state();
            require(!fixture.state.session.open("assets/fixtures/__missing_lifecycle__.marrow"),
                    "missing fixture unexpectedly loaded");
            require_current_views(fixture.state);
            require(fixture.state.preview_skeleton() == skeleton &&
                        fixture.state.animation_state() == animation, "failed open changed runtime views");
        }},
        {"animation duration owner", [] { exercise_transaction_owner(&ShellState::animation_duration_gesture); }},
        {"inspector transform owner", [] { exercise_transaction_owner(&ShellState::inspector_transform_gesture); }},
        {"parameter slider owner", [] { exercise_transaction_owner(&ShellState::parameter_slider_gesture); }},
        {"parameter geometry owner", [] { exercise_transaction_owner(&ShellState::parameter_geometry_gesture); }},
        {"graph value owner", [] { exercise_timeline_owner(&TimelineEditorState::graph_value_gesture); }},
        {"graph handle owner", [] { exercise_timeline_owner(&TimelineEditorState::graph_handle_gesture); }},
        {"viewport transform selection rollback", exercise_viewport_transform},
        {"viewport FFD selection rollback", exercise_viewport_ffd},
        {"retime original-key rollback", [] { exercise_timeline_selection(false); }},
        {"scale original-key rollback", [] { exercise_timeline_selection(true); }},
        {"weight stroke snapshot rollback", [] { exercise_snapshot_owner(true); }},
        {"coalesced snapshot rollback", [] { exercise_snapshot_owner(false); }},
        {"bare candidates clear without cancelling an edit", [] {
            Fixture fixture;
            auto& state = fixture.state;
            state.timeline_editor.scale_drag.emplace();
            state.timeline_editor.graph_drag.emplace();
            state.viewport_ffd_box_selection.emplace();
            state.viewport_box_selection.emplace();
            state.status_message = "No authoring edit";
            require(!authoring_gesture_active(state), "bare candidate counted as authoring");
            cancel_authoring_gestures(&state, "candidate test");
            require(!state.timeline_editor.scale_drag && !state.timeline_editor.graph_drag &&
                        !state.viewport_ffd_box_selection && !state.viewport_box_selection,
                    "cancel left a bare drag candidate");
            require(state.status_message == "No authoring edit", "candidate reported cancelled edit");
            fixture.require_rolled_back();
        }},
        {"empty and null cancellation are harmless", [] {
            cancel_authoring_gestures(nullptr, "null");
            ShellState state;
            cancel_authoring_gestures(&state, "empty");
            require(!authoring_gesture_active(state), "empty shell has active gesture");
            require_current_views(state);
        }},
    };
    std::size_t failures = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "PASS: " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
        }
    }
    std::cout << "Shell lifecycle: " << tests.size() - failures << '/' << tests.size()
              << " passed\n";
    return failures == 0 ? 0 : 1;
}
