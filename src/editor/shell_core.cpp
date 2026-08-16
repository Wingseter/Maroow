#include "shell_state.hpp"
#include "shell_asset_watch.hpp"
#include "shell_coalesced_edit.hpp"
#include "shell_selection.hpp"
#include "shell_weight_paint.hpp"

#include <algorithm>
#include <cmath>
#include <sstream>

#include "imgui_internal.h"

#include "marrow/editor/project.hpp"

namespace marrow::editor::shell {

std::vector<std::string> normalize_preview_skin_names(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<std::string>& preview_skin_names) {
    std::vector<std::string> result;
    for (const auto& skin_name : preview_skin_names) {
        if (skeleton.find_skin_index(skin_name).has_value()) {
            result.push_back(skin_name);
        }
    }
    return result;
}

const marrow::runtime::AnimationData* selected_animation(const ShellState& state) {
    if (!state.load_result || state.selected_animation_name.empty()) {
        return nullptr;
    }

    return state.load_result.skeleton_data->find_animation(state.selected_animation_name);
}

double selected_animation_duration(const ShellState& state) {
    const marrow::runtime::AnimationData* animation = selected_animation(state);
    return animation != nullptr ? std::max(animation->duration(), 0.0) : 0.0;
}

const marrow::runtime::AnimationData* queued_preview_animation(const ShellState& state) {
    if (!state.load_result || state.preview_queued_animation_name.empty()) {
        return nullptr;
    }

    const auto* animation =
        state.load_result.skeleton_data->find_animation(state.preview_queued_animation_name);
    if (animation == nullptr || animation->name == state.selected_animation_name) {
        return nullptr;
    }

    return animation;
}

std::string default_queued_preview_animation_name(const ShellState& state) {
    if (!state.load_result) {
        return {};
    }

    for (const auto& animation : state.load_result.skeleton_data->animations()) {
        if (animation.name != state.selected_animation_name) {
            return animation.name;
        }
    }

    return {};
}

void normalize_state_preview_settings(ShellState* state) {
    if (state == nullptr || !state->load_result) {
        return;
    }

    if (state->preview_custom_mix_duration < 0.0) {
        state->preview_custom_mix_duration = 0.0;
    }
    if (state->preview_queue_delay < 0.0) {
        state->preview_queue_delay = 0.0;
    }

    const auto* queued_animation = queued_preview_animation(*state);
    if (queued_animation == nullptr) {
        state->preview_queued_animation_name = default_queued_preview_animation_name(*state);
        if (state->preview_queued_animation_name.empty()) {
            state->preview_queue_enabled = false;
        }
    }
}

double timeline_preview_duration(const ShellState& state) {
    const double primary_duration = selected_animation_duration(state);
    if (!state.preview_queue_enabled) {
        return primary_duration;
    }

    const auto* queued_animation = queued_preview_animation(state);
    if (queued_animation == nullptr) {
        return primary_duration;
    }

    return std::max(0.0, primary_duration) +
        std::max(0.0, state.preview_queue_delay) +
        std::max(0.0, queued_animation->duration());
}

static bool attachment_selection_equal(
    const std::optional<PreviewAttachmentSelection>& left,
    const std::optional<PreviewAttachmentSelection>& right) {
    if (left.has_value() != right.has_value()) {
        return false;
    }
    if (!left.has_value()) {
        return true;
    }

    return left->slot_index == right->slot_index &&
        left->skin_index == right->skin_index &&
        left->attachment_name == right->attachment_name;
}

EditorHistorySnapshot capture_history_snapshot(
    const ShellState& state,
    bool include_serialized_project) {
    EditorHistorySnapshot snapshot;
    if (state.load_result.project != nullptr) {
        snapshot.project = *state.load_result.project;
        if (include_serialized_project) {
            snapshot.serialized_project =
                marrow::editor::serialize_project(*state.load_result.project);
        }
    }
    snapshot.preview_state.animation_name = state.selected_animation_name;
    snapshot.preview_state.time_seconds = state.timeline_time_seconds;
    snapshot.preview_state.loop = state.timeline_loop;
    snapshot.preview_state.playing = state.timeline_playing;
    snapshot.preview_state.queue_enabled = state.preview_queue_enabled;
    snapshot.preview_state.queued_animation_name = state.preview_queued_animation_name;
    snapshot.preview_state.queue_delay = state.preview_queue_delay;
    snapshot.preview_state.mix_duration = state.preview_use_custom_mix_duration
        ? std::optional<double>(state.preview_custom_mix_duration)
        : std::nullopt;
    snapshot.preview_state.reverse = state.preview_reverse;
    snapshot.preview_state.skin_names = state.preview_skin_names;
    snapshot.preview_state.slot_overrides.resize(state.preview_slot_overrides.size());
    for (std::size_t slot_index = 0;
         slot_index < state.preview_slot_overrides.size();
         ++slot_index) {
        const auto& selection = state.preview_slot_overrides[slot_index];
        if (selection.has_value()) {
            snapshot.preview_state.slot_overrides[slot_index] =
                marrow::editor::PreviewAttachmentOverride{
                    selection->skin_index,
                    selection->attachment_name};
        }
    }
    snapshot.preview_skin_names = state.preview_skin_names;
    snapshot.preview_slot_overrides = state.preview_slot_overrides;
    snapshot.runtime_revision = state.session.runtime_revision();
    return snapshot;
}

bool history_snapshots_equal(
    const EditorHistorySnapshot& left,
    const EditorHistorySnapshot& right) {
    if (left.serialized_project != right.serialized_project ||
        left.preview_skin_names != right.preview_skin_names ||
        left.preview_slot_overrides.size() != right.preview_slot_overrides.size()) {
        return false;
    }

    for (std::size_t index = 0; index < left.preview_slot_overrides.size(); ++index) {
        if (!attachment_selection_equal(
                left.preview_slot_overrides[index],
                right.preview_slot_overrides[index])) {
            return false;
        }
    }

    return true;
}

void assign_history_snapshot(
    ShellState* state,
    const EditorHistorySnapshot& snapshot) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return;
    }

    *state->load_result.project = snapshot.project;
    state->viewport.onion_skin = snapshot.project.editor_metadata.viewport.onion_skin;
    state->preview_skin_names = snapshot.preview_skin_names;
    state->preview_slot_overrides = snapshot.preview_slot_overrides;
    state->selected_animation_name = snapshot.preview_state.animation_name;
    state->timeline_time_seconds = snapshot.preview_state.time_seconds;
    state->timeline_loop = snapshot.preview_state.loop;
    state->timeline_playing = snapshot.preview_state.playing;
    state->preview_queue_enabled = snapshot.preview_state.queue_enabled;
    state->preview_queued_animation_name = snapshot.preview_state.queued_animation_name;
    state->preview_queue_delay = snapshot.preview_state.queue_delay;
    state->preview_use_custom_mix_duration = snapshot.preview_state.mix_duration.has_value();
    state->preview_custom_mix_duration = snapshot.preview_state.mix_duration.value_or(0.0);
    state->preview_reverse = snapshot.preview_state.reverse;
    marrow::editor::EditorSessionShellBinding::sync_preview_state(
        state->session,
        snapshot.preview_state);
}

void restore_history_snapshot(
    ShellState* state,
    const EditorHistorySnapshot& snapshot) {
    if (state == nullptr) {
        return;
    }

    assign_history_snapshot(state, snapshot);
    rebuild_project_runtime(state);
}

bool apply_history_snapshot(ShellState* state, const EditorHistorySnapshot& snapshot) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    const std::string serialized =
        marrow::editor::serialize_project(*state->load_result.project);
    if (serialized == snapshot.serialized_project &&
        state->preview_skin_names == snapshot.preview_skin_names) {
        
        bool overrides_equal = true;
        if (state->preview_slot_overrides.size() != snapshot.preview_slot_overrides.size()) {
            overrides_equal = false;
        } else {
            for (std::size_t i = 0; i < state->preview_slot_overrides.size(); ++i) {
                if (!attachment_selection_equal(state->preview_slot_overrides[i], snapshot.preview_slot_overrides[i])) {
                    overrides_equal = false;
                    break;
                }
            }
        }
        
        if (overrides_equal) {
            return true;
        }
    }

    restore_history_snapshot(state, snapshot);
    return true;
}

void update_project_dirty_state(ShellState* state) {
    if (!state->load_result || state->load_result.project == nullptr) {
        state->project_dirty = false;
        return;
    }

    state->project_dirty = state->session.dirty();
}

void sync_shell_from_editor_session(ShellState* state) {
    if (state == nullptr || !state->session.has_project()) {
        return;
    }
    state->preview_skeleton =
        marrow::editor::EditorSessionShellBinding::preview_skeleton(state->session);
    state->animation_state =
        marrow::editor::EditorSessionShellBinding::preview_animation_state(state->session);

    const marrow::editor::PreviewState& preview = state->session.preview_state();
    state->selected_animation_name = preview.animation_name;
    state->timeline_time_seconds = preview.time_seconds;
    state->timeline_loop = preview.loop;
    state->timeline_playing = preview.playing;
    state->preview_queue_enabled = preview.queue_enabled;
    state->preview_queued_animation_name = preview.queued_animation_name;
    state->preview_queue_delay = preview.queue_delay;
    state->preview_use_custom_mix_duration = preview.mix_duration.has_value();
    state->preview_custom_mix_duration = preview.mix_duration.value_or(0.0);
    state->preview_reverse = preview.reverse;
    state->preview_skin_names = preview.skin_names;
    state->preview_slot_overrides.assign(preview.slot_overrides.size(), std::nullopt);
    for (std::size_t slot_index = 0; slot_index < preview.slot_overrides.size(); ++slot_index) {
        const auto& override_value = preview.slot_overrides[slot_index];
        if (override_value.has_value()) {
            state->preview_slot_overrides[slot_index] = PreviewAttachmentSelection{
                slot_index,
                override_value->skin_index,
                override_value->attachment_name};
        }
    }
    state->preview_events = state->session.preview_events();
    state->preview_root_motion_delta = state->session.preview_root_motion_delta();
    state->preview_root_motion_total = state->session.preview_root_motion_total();
    state->project_dirty = state->session.dirty();
    state->observed_project_revision = state->session.project_revision();
    state->observed_runtime_revision = state->session.runtime_revision();
    state->observed_preview_revision = state->session.preview_revision();
}

void sync_shell_from_editor_session_if_revised(ShellState* state) {
    if (state == nullptr || !state->session.has_project()) {
        return;
    }
    if (state->observed_project_revision != state->session.project_revision() ||
        state->observed_runtime_revision != state->session.runtime_revision() ||
        state->observed_preview_revision != state->session.preview_revision()) {
        sync_shell_from_editor_session(state);
    }
}

bool record_action_from_snapshots(
    ShellState* state,
    const EditorHistorySnapshot& before,
    EditActionKind kind,
    std::string label,
    std::string group,
    bool allow_merge) {
    const EditorHistorySnapshot after = capture_history_snapshot(*state, true);
    if (history_snapshots_equal(before, after)) {
        return false;
    }

    marrow::editor::EditKind session_kind = marrow::editor::EditKind::EditProperty;
    switch (kind) {
    case EditActionKind::MoveBone:
        session_kind = marrow::editor::EditKind::MoveBone;
        break;
    case EditActionKind::AddKeyframe:
        session_kind = marrow::editor::EditKind::AddKeyframe;
        break;
    case EditActionKind::RemoveKeyframe:
        session_kind = marrow::editor::EditKind::RemoveKeyframe;
        break;
    case EditActionKind::EditProperty:
        session_kind = marrow::editor::EditKind::EditProperty;
        break;
    }

    if (!marrow::editor::EditorSessionShellBinding::sync_preview_state(
            state->session,
            after.preview_state)) {
        state->error_message = "Failed to synchronize the editor preview state.";
        restore_history_snapshot(state, before);
        return false;
    }
    const marrow::editor::SessionResult commit_result =
        marrow::editor::EditorSessionShellBinding::commit_external_edit(
            state->session,
            before.project,
            before.preview_state,
            marrow::editor::EditDescriptor{
                session_kind,
                label,
                group,
                allow_merge,
                marrow::editor::EditImpact::Project |
                    marrow::editor::EditImpact::Runtime |
                    marrow::editor::EditImpact::Preview},
            state->session.runtime_revision() != before.runtime_revision);
    if (!commit_result) {
        state->error_message = commit_result.error->format();
        state->preview_skeleton =
            marrow::editor::EditorSessionShellBinding::preview_skeleton(state->session);
        state->animation_state =
            marrow::editor::EditorSessionShellBinding::preview_animation_state(state->session);
        return false;
    }
    if (!commit_result.changed) {
        return false;
    }
    state->preview_skeleton =
        marrow::editor::EditorSessionShellBinding::preview_skeleton(state->session);
    state->animation_state =
        marrow::editor::EditorSessionShellBinding::preview_animation_state(state->session);
    update_project_dirty_state(state);
    state->error_message.clear();
    state->status_message = std::move(label);
    return true;
}

CoalescedEditFrame coalesced_edit_frame_from_last_item(bool changed) {
    return CoalescedEditFrame{
        ImGui::GetItemID(),
        ImGui::IsItemActivated(),
        changed,
        ImGui::IsItemDeactivatedAfterEdit(),
        ImGui::IsItemDeactivated()};
}

bool finalize_coalesced_edit(ShellState* state, ImGuiID item_id) {
    if (state == nullptr || !state->pending_edit_action.has_value() ||
        state->pending_edit_action->item_id != item_id) {
        return false;
    }
    PendingEditAction pending = std::move(*state->pending_edit_action);
    state->pending_edit_action.reset();
    return record_action_from_snapshots(
        state,
        pending.before_snapshot,
        pending.kind,
        std::move(pending.label),
        std::move(pending.group),
        pending.allow_merge);
}

void finalize_orphaned_coalesced_edit(ShellState* state) {
    if (state == nullptr || !state->pending_edit_action.has_value()) {
        return;
    }
    const ImGuiID item_id = state->pending_edit_action->item_id;
    const ImGuiContext* context = ImGui::GetCurrentContext();
    const bool item_is_live = context != nullptr &&
        ImGui::GetActiveID() == item_id && context->ActiveIdIsAlive == item_id;
    if (item_is_live) {
        return;
    }

    (void)finalize_coalesced_edit(state, item_id);
}

bool cancel_coalesced_edit(ShellState* state) {
    if (state == nullptr || !state->pending_edit_action.has_value()) {
        return false;
    }
    const EditorHistorySnapshot before = state->pending_edit_action->before_snapshot;
    state->pending_edit_action.reset();
    restore_history_snapshot(state, before);
    return true;
}

void cancel_authoring_gestures(ShellState* state, std::string_view reason) {
    if (state == nullptr) {
        return;
    }

    bool cancelled = false;
    cancelled = cancel_coalesced_edit(state);
    if (state->weight_paint_stroke.active) {
        const EditorHistorySnapshot before =
            state->weight_paint_stroke.before_snapshot;
        reset_weight_paint_stroke(state);
        restore_history_snapshot(state, before);
        cancelled = true;
    }
    // This list must stay in step with authoring_gesture_active
    // (shell_state.hpp); a gesture missing here leaks a live transaction
    // that blocks every future begin_edit.
    const auto cancel_transaction_gesture = [&](auto& gesture_slot) {
        if (gesture_slot.has_value()) {
            gesture_slot->transaction.cancel();
            gesture_slot.reset();
            cancelled = true;
        }
    };
    cancel_transaction_gesture(state->animation_duration_gesture);
    cancel_transaction_gesture(state->inspector_transform_gesture);
    if (state->viewport_transform_gesture.has_value()) {
        ViewportTransformGesture gesture =
            std::move(*state->viewport_transform_gesture);
        state->viewport_transform_gesture.reset();
        gesture.transaction.cancel();
        state->selection = gesture.selection_before;
        state->hierarchy_selection_anchor = gesture.hierarchy_anchor_before;
        state->selected_timeline_track_id = gesture.timeline_focus_before;
        cancelled = true;
    }
    if (state->viewport_ffd_gesture.has_value()) {
        ViewportFfdGesture gesture =
            std::move(*state->viewport_ffd_gesture);
        state->viewport_ffd_gesture.reset();
        gesture.transaction.cancel();
        state->selection = gesture.selection_before;
        state->viewport_ffd_selection = gesture.vertex_selection_before;
        state->hierarchy_selection_anchor = gesture.hierarchy_anchor_before;
        state->selected_timeline_track_id = gesture.timeline_focus_before;
        cancelled = true;
    }
    cancel_transaction_gesture(state->timeline_editor.retime_gesture);
    cancel_transaction_gesture(state->parameter_slider_gesture);
    cancel_transaction_gesture(state->parameter_geometry_gesture);
    state->viewport_ffd_box_selection.reset();
    state->viewport_box_selection.reset();
    state->pointer_mediator.reset();
    sync_shell_from_editor_session(state);
    if (cancelled) {
        state->status_message = "Cancelled active edit: " + std::string(reason);
    }
}

bool rebuild_project_runtime(ShellState* state) {
    if (!state->load_result || state->load_result.project == nullptr ||
        state->load_result.base_skeleton_document == nullptr) {
        return false;
    }

    std::optional<marrow::runtime::AnimationStateSnapshot> playback_snapshot;
    if (state->animation_state != nullptr) {
        playback_snapshot = state->animation_state->capture_state();
    }

    const marrow::editor::SessionResult runtime_result =
        marrow::editor::EditorSessionShellBinding::rebuild_runtime_without_history(
            state->session);
    if (!runtime_result) {
        state->error_message = runtime_result.error->format();
        return false;
    }
    state->preview_skeleton =
        marrow::editor::EditorSessionShellBinding::preview_skeleton(state->session);
    state->animation_state =
        marrow::editor::EditorSessionShellBinding::preview_animation_state(state->session);
    if (playback_snapshot.has_value() && state->animation_state != nullptr) {
        state->animation_state->restore_state(*playback_snapshot);
    }
    state->preview_skin_names = normalize_preview_skin_names(
        *state->load_result.skeleton_data,
        state->preview_skin_names);
    state->preview_slot_overrides.resize(state->load_result.skeleton_data->slots().size());

    if (!state->selected_animation_name.empty() &&
        state->load_result.skeleton_data->find_animation(state->selected_animation_name) == nullptr) {
        state->selected_animation_name.clear();
    }

    return true;
}

bool reload_project(ShellState* state) {
    if (state == nullptr) return false;
    if (authoring_gesture_active(*state)) {
        state->status_message = "Finish the active edit before reloading";
        return false;
    }

    const std::string previous_animation_name = state->selected_animation_name;
    const double previous_timeline_time = state->timeline_time_seconds;
    const bool previous_timeline_loop = state->timeline_loop;
    const bool previous_timeline_playing = state->timeline_playing;

    const bool reload_current_project =
        state->session.has_project() && state->session.project() != nullptr &&
        state->session.project()->source_path == state->project_path;
    const marrow::editor::ProjectLoadResult attempted_load = reload_current_project
        ? state->session.reload()
        : state->session.open(state->project_path);
    if (!attempted_load) {
        state->status_message = "Project load failed";
        if (attempted_load.error.has_value()) {
            state->error_message = attempted_load.error->format();
        } else {
            state->error_message = "Unknown project load failure.";
        }
        return false;
    }

    // A source adoption invalidates the screen-space rectangle captured by an
    // in-flight viewport box gesture, even when every selected identity survives.
    state->viewport_ffd_selection.reset();
    state->viewport_ffd_box_selection.reset();
    state->viewport_box_selection.reset();
    state->preview_skeleton = nullptr;
    state->animation_state = nullptr;
    state->selected_timeline_track_id.reset();
    state->timeline_editor = TimelineEditorState{};
    state->preview_skin_names.clear();
    state->preview_slot_overrides.clear();
    state->selected_animation_name.clear();
    state->timeline_time_seconds = 0.0;
    state->timeline_loop = previous_timeline_loop;
    state->timeline_playing = false;
    state->pending_edit_action.reset();
    
    state->project_dirty = false;
    state->saved_project_snapshot.clear();
    state->error_message.clear();

    state->viewport = state->load_result.project->editor_metadata.viewport;
    state->timeline_editor.frames_per_second =
        state->load_result.project->editor_metadata.timeline.frames_per_second;
    state->saved_project_snapshot =
        marrow::editor::serialize_project(*state->load_result.project);
    state->preview_skeleton =
        marrow::editor::EditorSessionShellBinding::preview_skeleton(state->session);
    state->animation_state =
        marrow::editor::EditorSessionShellBinding::preview_animation_state(state->session);
    state->preview_skin_names = normalize_preview_skin_names(
        *state->load_result.skeleton_data,
        state->load_result.project->editor_metadata.preview_skins);
    state->preview_slot_overrides.resize(state->load_result.skeleton_data->slots().size());

    const auto& animations = state->load_result.skeleton_data->animations();
    if (!previous_animation_name.empty() &&
        state->load_result.skeleton_data->find_animation(previous_animation_name) != nullptr) {
        state->selected_animation_name = previous_animation_name;
    } else if (!state->load_result.project->editor_metadata.active_animation.empty() &&
               state->load_result.skeleton_data->find_animation(
                   state->load_result.project->editor_metadata.active_animation) != nullptr) {
        state->selected_animation_name = state->load_result.project->editor_metadata.active_animation;
    } else if (!animations.empty()) {
        state->selected_animation_name = animations.front().name;
    }
    normalize_state_preview_settings(state);
    if (reload_current_project) {
        sync_shell_from_editor_session(state);
    } else if (!state->selected_animation_name.empty()) {
        state->timeline_time_seconds = std::clamp(
            previous_timeline_time,
            0.0,
            timeline_preview_duration(*state));
        state->timeline_playing = previous_timeline_playing;
        state->session.select_animation(state->selected_animation_name, true);
        state->session.set_loop(state->timeline_loop);
        state->session.seek(state->timeline_time_seconds);
        state->session.set_playing(state->timeline_playing);
    }
    (void)initialize_viewport_camera_from_preview_pose(state);
    reset_runtime_asset_watch(state);
    marrow::editor::reconcile_selection_to_runtime(
        state->selection,
        *state->load_result.skeleton_data);
    reconcile_hierarchy_anchor_to_runtime(
        state,
        *state->load_result.skeleton_data);

    return true;
}

bool save_project_file(ShellState* state, bool update_status_message) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return false;
    }
    if (authoring_gesture_active(*state)) {
        state->status_message = "Finish the active edit before saving";
        return false;
    }

    const auto save_result = state->session.save(state->project_path);
    if (!save_result) {
        state->error_message = save_result.error->format();
        state->status_message = "Project save failed";
        return false;
    }

    state->saved_project_snapshot =
        marrow::editor::serialize_project(*state->load_result.project);
    state->project_dirty = state->session.dirty();
    state->error_message.clear();
    if (update_status_message) {
        state->status_message = "Saved project to " + state->project_path.string();
    }
    return true;
}

bool export_runtime_assets_file(ShellState* state, bool update_status_message) {
    if (!state->load_result || state->load_result.project == nullptr ||
        state->load_result.base_skeleton_document == nullptr) {
        return false;
    }
    if (authoring_gesture_active(*state)) {
        state->status_message = "Finish the active edit before exporting";
        return false;
    }

    marrow::editor::ProjectExportOptions export_options;
    if (state->export_binary_output) {
        export_options.binary_output_path =
            state->load_result.project->resolved_export_binary_path();
    }

    const auto export_result = state->session.export_runtime(export_options);
    if (!export_result) {
        state->error_message = export_result.error->format();
        state->status_message = "Runtime export failed";
        return false;
    }

    state->error_message.clear();
    if (update_status_message) {
        std::string message = "Exported runtime assets to " + export_result.path.string();
        if (!export_result.atlas_paths.empty()) {
            message += " with " + std::to_string(export_result.atlas_paths.size()) +
                " atlas file";
            if (export_result.atlas_paths.size() != 1U) {
                message += "s";
            }
        }
        if (export_result.binary_path.has_value()) {
            message += " and " + export_result.binary_path->filename().string();
        }
        state->status_message = std::move(message);
    }
    return true;
}

} // namespace marrow::editor::shell
