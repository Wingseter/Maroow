#include "shell_state.hpp"

#include <utility>

#include "shell_coalesced_edit.hpp"
#include "shell_weight_paint.hpp"
#include "timeline_controller.hpp"

namespace marrow::editor::shell {
namespace {

// Register each authoring owner ONCE, in cancellation order. Both the activity
// gate and cancellation dispatch use this list. A new optional owner must have
// a transaction or an explicit cancel_gesture overload; it cannot silently gain
// activity detection without a cancellation policy.
template<class State, class Visitor>
void visit_authoring_gestures(State& state, Visitor&& visit) {
    visit(state.pending_edit_action);
    visit(state.weight_paint_stroke);
    visit(state.animation_duration_gesture);
    visit(state.inspector_transform_gesture);
    visit(state.viewport_transform_gesture);
    visit(state.viewport_ffd_gesture);
    visit(state.timeline_editor.retime_gesture);
    visit(state.timeline_editor.scale_gesture);
    visit(state.timeline_editor.graph_value_gesture);
    visit(state.timeline_editor.graph_handle_gesture);
    visit(state.parameter_slider_gesture);
    visit(state.parameter_geometry_gesture);
}

template<class Gesture>
bool gesture_active(const std::optional<Gesture>& slot) noexcept {
    return slot.has_value();
}

bool gesture_active(const MeshWeightStrokeState& stroke) noexcept {
    return stroke.active;
}

// Dispatch only calls a policy for an active owner. Snapshot-backed gestures
// and gestures with shell-owned selection rollback have explicit overloads.
template<class Gesture>
void cancel_gesture(ShellState&, std::optional<Gesture>& slot) {
    slot->transaction.cancel();
    slot.reset();
}

void cancel_gesture(ShellState& state, std::optional<PendingEditAction>&) {
    (void)cancel_coalesced_edit(&state);
}

void cancel_gesture(ShellState& state, MeshWeightStrokeState& stroke) {
    const EditorHistorySnapshot before = stroke.before_snapshot;
    reset_weight_paint_stroke(&state);
    restore_history_snapshot(&state, before);
}

void cancel_gesture(ShellState& state, std::optional<ViewportTransformGesture>& slot) {
    ViewportTransformGesture gesture = std::move(*slot);
    slot.reset();
    gesture.transaction.cancel();
    state.selection = std::move(gesture.selection_before);
    state.hierarchy_selection_anchor = std::move(gesture.hierarchy_anchor_before);
    state.selected_timeline_track_id = std::move(gesture.timeline_focus_before);
}

void cancel_gesture(ShellState& state, std::optional<ViewportFfdGesture>& slot) {
    ViewportFfdGesture gesture = std::move(*slot);
    slot.reset();
    gesture.transaction.cancel();
    state.selection = std::move(gesture.selection_before);
    state.viewport_ffd_selection = std::move(gesture.vertex_selection_before);
    state.hierarchy_selection_anchor = std::move(gesture.hierarchy_anchor_before);
    state.selected_timeline_track_id = std::move(gesture.timeline_focus_before);
}

void cancel_gesture(ShellState& state, std::optional<TimelineRetimeGesture>&) {
    // Cancelling the project also requires restoring the old key identities.
    finish_timeline_retime_gesture(&state, false);
}

void cancel_gesture(ShellState& state, std::optional<TimelineScaleGesture>&) {
    finish_timeline_scale_gesture(&state, false);
}

} // namespace

bool authoring_gesture_active(const ShellState& state) noexcept {
    bool active = false;
    visit_authoring_gestures(state, [&](const auto& owner) {
        active = gesture_active(owner) || active;
    });
    return active;
}

void cancel_authoring_gestures(ShellState* state, std::string_view reason) {
    if (state == nullptr) {
        return;
    }
    bool cancelled = false;
    visit_authoring_gestures(*state, [&](auto& owner) {
        if (gesture_active(owner)) {
            cancel_gesture(*state, owner);
            cancelled = true;
        }
    });

    // Bare candidates own no authoring transaction. Clear their input state,
    // but do not report a cancelled edit when only a candidate was present.
    state->timeline_editor.scale_drag.reset();
    state->timeline_editor.graph_drag.reset();
    state->viewport_ffd_box_selection.reset();
    state->viewport_box_selection.reset();
    state->pointer_mediator.reset();
    sync_shell_from_editor_session(state);
    if (cancelled) {
        state->status_message = "Cancelled active edit: " + std::string(reason);
    }
}

} // namespace marrow::editor::shell
