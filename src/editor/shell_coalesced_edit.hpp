#pragma once

#include <string>
#include <utility>

#include "shell_state.hpp"

namespace marrow::editor::shell {

enum class CoalescedEditPolicy {
    ProjectRuntime,
    ProjectMetadataOnly,
};

struct CoalescedEditFrame {
    ImGuiID item_id{0};
    bool activated{false};
    bool changed{false};
    bool deactivated_after_edit{false};
    bool deactivated{false};
};

struct CoalescedEditDescriptor {
    EditActionKind kind{EditActionKind::EditProperty};
    std::string label;
    std::string group;
    bool allow_merge{false};
    CoalescedEditPolicy policy{CoalescedEditPolicy::ProjectRuntime};
    std::string failure_status;
};

CoalescedEditFrame coalesced_edit_frame_from_last_item(bool changed);
bool finalize_coalesced_edit(ShellState* state, ImGuiID item_id);
void finalize_orphaned_coalesced_edit(ShellState* state);
bool cancel_coalesced_edit(ShellState* state);

template <typename MutateFn>
bool apply_coalesced_edit_frame(
    ShellState* state,
    const CoalescedEditFrame& frame,
    CoalescedEditDescriptor descriptor,
    MutateFn mutate) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    if (frame.activated) {
        state->pending_edit_action = PendingEditAction{
            frame.item_id,
            descriptor.kind,
            std::move(descriptor.label),
            std::move(descriptor.group),
            descriptor.allow_merge,
            capture_history_snapshot(*state)};
    }

    if (frame.changed) {
        if (descriptor.policy == CoalescedEditPolicy::ProjectRuntime) {
            const EditorHistorySnapshot rollback = capture_history_snapshot(*state, false);
            mutate();
            if (!rebuild_project_runtime(state)) {
                const std::string rebuild_error = state->error_message;
                restore_history_snapshot(state, rollback);
                state->pending_edit_action.reset();
                state->error_message = rebuild_error;
                state->status_message = std::move(descriptor.failure_status);
                return false;
            }
        } else {
            mutate();
            update_project_dirty_state(state);
        }
    }

    if (frame.deactivated_after_edit && state->pending_edit_action.has_value() &&
        state->pending_edit_action->item_id == frame.item_id) {
        return finalize_coalesced_edit(state, frame.item_id);
    }

    if (frame.deactivated && state->pending_edit_action.has_value() &&
        state->pending_edit_action->item_id == frame.item_id) {
        state->pending_edit_action.reset();
    }

    return true;
}

} // namespace marrow::editor::shell
