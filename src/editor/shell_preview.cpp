#include "shell_preview.hpp"

#include <algorithm>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "imgui.h"

#include "shell_selection.hpp"
#include "shell_state.hpp"
#include "shell_timeline.hpp"
#include "shell_viewport_ui.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/renderer/module.hpp"
#include "marrow/runtime/animation_state.hpp"

namespace marrow::editor::shell {

void apply_preview_slot_overrides(
    const ShellState& state,
    marrow::runtime::Skeleton* skeleton) {
    if (!state.load_result || skeleton == nullptr) {
        return;
    }

    auto& slot_states = skeleton->slot_states();
    auto& mesh_deforms = skeleton->mesh_deform_states();
    for (std::size_t slot_index = 0;
         slot_index < state.preview_slot_overrides.size() && slot_index < slot_states.size();
         ++slot_index) {
        const auto& override_selection = state.preview_slot_overrides[slot_index];
        if (!override_selection.has_value()) {
            continue;
        }

        if (!resolve_attachment_reference(
                *state.load_result.skeleton_data,
                *override_selection).has_value()) {
            continue;
        }

        slot_states[slot_index].attachment_name = override_selection->attachment_name;
        slot_states[slot_index].attachment_skin_index = override_selection->skin_index;
        if (slot_index < mesh_deforms.size() &&
            mesh_deforms[slot_index].attachment_name != override_selection->attachment_name) {
            mesh_deforms[slot_index].attachment_name.clear();
            mesh_deforms[slot_index].vertex_offsets.clear();
        }
    }
}

void apply_preview_slot_overrides(ShellState* state) {
    if (!state->load_result || !state->preview_skeleton) {
        return;
    }

    for (std::size_t slot_index = 0; slot_index < state->preview_slot_overrides.size(); ++slot_index) {
        const auto& override_selection = state->preview_slot_overrides[slot_index];
        if (!override_selection.has_value()) {
            continue;
        }
        if (!resolve_attachment_reference(
                *state->load_result.skeleton_data,
                *override_selection).has_value()) {
            state->preview_slot_overrides[slot_index].reset();
        }
    }

    apply_preview_slot_overrides(*state, state->preview_skeleton);
}

bool apply_project_command_change(
    ShellState* state,
    const marrow::editor::ProjectData& previous_project,
    EditActionKind kind,
    std::string command_label,
    std::string group,
    bool allow_merge,
    std::string failure_status) {
    if (state == nullptr) {
        return false;
    }
    if (authoring_gesture_active(*state)) {
        if (state->load_result && state->load_result.project != nullptr) {
            *state->load_result.project = previous_project;
        }
        state->status_message = "Finish the active edit before applying another edit";
        return false;
    }
    EditorHistorySnapshot before = capture_history_snapshot(*state);
    before.project = previous_project;
    before.serialized_project = marrow::editor::serialize_project(previous_project);

    if (!rebuild_project_runtime(state)) {
        const std::string rebuild_error = state->error_message;
        restore_history_snapshot(state, before);
        state->error_message = rebuild_error;
        state->status_message = std::move(failure_status);
        return false;
    }
    if (!apply_current_animation_state_to_preview(state)) {
        state->status_message = std::move(failure_status);
        return false;
    }

    return record_action_from_snapshots(
        state,
        before,
        kind,
        std::move(command_label),
        std::move(group),
        allow_merge);
}

bool undo_project_change(ShellState* state) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return false;
    }
    if (authoring_gesture_active(*state)) {
        state->status_message = "Finish the active edit before undoing";
        return false;
    }

    const std::string label(state->session.undo_label());
    const marrow::editor::SessionResult undo_result = state->session.undo();
    if (!undo_result || !undo_result.changed) {
        state->status_message = state->session.can_undo() ? "Undo failed" : "Nothing to undo";
        update_project_dirty_state(state);
        return false;
    }
    sync_shell_from_editor_session(state);
    if (!apply_current_animation_state_to_preview(state)) {
        state->status_message = "Undo failed";
        update_project_dirty_state(state);
        return false;
    }

    update_project_dirty_state(state);
    state->status_message = "Undid " + label;
    return true;
}

bool redo_project_change(ShellState* state) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return false;
    }
    if (authoring_gesture_active(*state)) {
        state->status_message = "Finish the active edit before redoing";
        return false;
    }

    const std::string label(state->session.redo_label());
    const marrow::editor::SessionResult redo_result = state->session.redo();
    if (!redo_result || !redo_result.changed) {
        state->status_message = state->session.can_redo() ? "Redo failed" : "Nothing to redo";
        update_project_dirty_state(state);
        return false;
    }
    sync_shell_from_editor_session(state);
    if (!apply_current_animation_state_to_preview(state)) {
        state->status_message = "Redo failed";
        update_project_dirty_state(state);
        return false;
    }

    update_project_dirty_state(state);
    state->status_message = "Redid " + label;
    return true;
}

void handle_project_history_shortcuts(ShellState* state) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return;
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) {
        return;
    }

    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Z, ImGuiInputFlags_RouteGlobal)) {
        undo_project_change(state);
        return;
    }

    if (ImGui::Shortcut(
            ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z,
            ImGuiInputFlags_RouteGlobal) ||
        ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_Y, ImGuiInputFlags_RouteGlobal)) {
        redo_project_change(state);
    }

    if (ImGui::Shortcut(ImGuiKey_Space, ImGuiInputFlags_RouteGlobal)) {
        if (selected_animation(*state) != nullptr) {
            state->timeline_playing = !state->timeline_playing;
            if (state->timeline_playing) {
                refresh_preview_pose(state);
            }
            state->status_message =
                std::string(state->timeline_playing ? "Playing " : "Paused ") +
                state->selected_animation_name;
        }
    }

    if (ImGui::Shortcut(ImGuiKey_Home, ImGuiInputFlags_RouteGlobal)) {
        state->timeline_playing = false;
        scrub_timeline_time(state, 0.0, "Shortcut", true);
    }

    if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_L,
                        ImGuiInputFlags_RouteGlobal)) {
        state->show_agent_panel = !state->show_agent_panel;
        state->status_message =
            state->show_agent_panel ? "Agent panel opened (Ctrl+L)"
                                    : "Agent panel closed (Ctrl+L)";
    }

    if (ImGui::Shortcut(ImGuiKey_F, ImGuiInputFlags_RouteGlobal)) {
        const ImVec2 canvas_size = ImGui::GetContentRegionAvail();
        auto_frame_skeleton(state, canvas_size);
    }
}

std::optional<std::size_t> preview_root_bone_index(
    const marrow::runtime::SkeletonData& skeleton) {
    if (const auto root_index = skeleton.find_bone_index("root")) {
        return root_index;
    }

    for (std::size_t bone_index = 0; bone_index < skeleton.bones().size(); ++bone_index) {
        if (!skeleton.bones()[bone_index].parent_index.has_value()) {
            return bone_index;
        }
    }

    if (!skeleton.bones().empty()) {
        return 0U;
    }

    return std::nullopt;
}

bool apply_current_animation_state_to_preview(ShellState* state) {
    if (!state->load_result || !state->preview_skeleton || !state->animation_state) {
        return false;
    }

    const auto& skeleton = *state->load_result.skeleton_data;
    normalize_state_preview_settings(state);
    state->preview_skin_names =
        normalize_preview_skin_names(skeleton, state->preview_skin_names);

    std::vector<std::string_view> skin_names;
    skin_names.reserve(state->preview_skin_names.size());
    for (const std::string& skin_name : state->preview_skin_names) {
        skin_names.push_back(skin_name);
    }

    if (!state->preview_skeleton->set_skin_composition(skin_names)) {
        state->error_message = "Failed to apply the requested preview skin composition.";
        return false;
    }

    state->preview_root_motion_delta = {};
    state->preview_root_motion_total = {};
    state->preview_events.clear();
    state->preview_skeleton->set_attachment_playback_time(state->timeline_time_seconds);
    state->animation_state->apply(*state->preview_skeleton);
    apply_preview_slot_overrides(state);
    state->error_message.clear();
    return true;
}

bool restore_preview_playback(
    ShellState* state,
    const marrow::runtime::AnimationStateSnapshot& snapshot) {
    if (!state->animation_state || !state->preview_skeleton || !state->load_result) {
        return false;
    }

    state->animation_state->restore_state(snapshot);

    if (const std::shared_ptr<marrow::runtime::TrackEntry> current =
            state->animation_state->get_current(0);
        current != nullptr && !current->is_empty &&
        state->load_result.skeleton_data->find_animation(current->animation_name) != nullptr) {
        state->selected_animation_name = current->animation_name;
        state->timeline_time_seconds = std::clamp(
            current->track_time,
            0.0,
            timeline_preview_duration(*state));
    }

    return apply_current_animation_state_to_preview(state);
}

bool refresh_preview_pose(ShellState* state) {
    if (!state->load_result || !state->preview_skeleton) {
        return false;
    }
    normalize_state_preview_settings(state);
    state->preview_skin_names = normalize_preview_skin_names(
        *state->load_result.skeleton_data,
        state->preview_skin_names);
    const marrow::editor::PreviewState desired =
        capture_history_snapshot(*state, false).preview_state;
    if (!marrow::editor::EditorSessionShellBinding::sync_preview_state(
            state->session,
            desired)) {
        state->error_message = "Failed to refresh the editor preview state.";
        return false;
    }
    sync_shell_from_editor_session(state);
    state->error_message.clear();
    return true;
}

std::optional<marrow::runtime::ProfilerFrame> build_preview_profiler_frame(
    const ShellState& state) {
    if (!state.load_result || state.load_result.atlas_data.empty()) {
        return std::nullopt;
    }

    const auto& skeleton_data = *state.load_result.skeleton_data;
    marrow::runtime::ProfilerCapture profiler(true);
    profiler.begin_frame();
    bool render_ready = true;

    marrow::runtime::Skeleton scratch_skeleton(state.load_result.skeleton_data);
    const std::vector<std::string> preview_skin_names =
        normalize_preview_skin_names(skeleton_data, state.preview_skin_names);
    std::vector<std::string_view> skin_names;
    skin_names.reserve(preview_skin_names.size());
    for (const std::string& skin_name : preview_skin_names) {
        skin_names.push_back(skin_name);
    }
    if (!scratch_skeleton.set_skin_composition(skin_names)) {
        return std::nullopt;
    }

    const double preview_duration = timeline_preview_duration(state);
    const double sampled_time =
        preview_duration > 0.0 ? std::clamp(state.timeline_time_seconds, 0.0, preview_duration)
                               : 0.0;
    scratch_skeleton.set_attachment_playback_time(sampled_time);

    if (const marrow::runtime::AnimationData* animation = selected_animation(state)) {
        marrow::runtime::AnimationState scratch_animation_state(state.load_result.skeleton_data);
        marrow::runtime::profile_phase(
            &profiler,
            marrow::runtime::ProfilerPhase::Animation,
            [&]() {
                scratch_animation_state.clear_tracks();
                const bool primary_loop = state.preview_queue_enabled ? false : state.timeline_loop;
                std::shared_ptr<marrow::runtime::TrackEntry> current =
                    scratch_animation_state.set_animation(0, animation->name, primary_loop, 0.0);
                current->reverse = state.preview_reverse;
                current->alpha = 1.0;

                if (state.preview_queue_enabled) {
                    if (const auto* queued_animation = queued_preview_animation(state)) {
                        const std::optional<double> mix_duration =
                            state.preview_use_custom_mix_duration
                                ? std::optional<double>(state.preview_custom_mix_duration)
                                : std::nullopt;
                        std::shared_ptr<marrow::runtime::TrackEntry> queued_entry =
                            scratch_animation_state.add_animation(
                                0,
                                queued_animation->name,
                                false,
                                state.preview_queue_delay,
                                mix_duration);
                        queued_entry->reverse = state.preview_reverse;
                    }
                }

                constexpr double kPreviewStep = 1.0 / 60.0;
                double elapsed_time = 0.0;
                while ((elapsed_time + kPreviewStep) < (sampled_time - 1e-9)) {
                    scratch_animation_state.update(kPreviewStep);
                    elapsed_time += kPreviewStep;
                }
                const double final_step = sampled_time - elapsed_time;
                if (final_step > 1e-9) {
                    scratch_animation_state.update(final_step);
                }

                scratch_animation_state.apply_pose(scratch_skeleton);
            });
    }

    marrow::runtime::WorldTransformTimingBreakdown timing_breakdown;
    scratch_skeleton.update_world_transforms(
        marrow::runtime::PhysicsMode::Pose,
        &timing_breakdown);
    profiler.add_world_transform_timing(timing_breakdown);

    apply_preview_slot_overrides(state, &scratch_skeleton);

    marrow::runtime::profile_phase(
        &profiler,
        marrow::runtime::ProfilerPhase::Skinning,
        [&]() {
            for (std::size_t slot_index = 0;
                 slot_index < scratch_skeleton.slot_states().size();
                 ++slot_index) {
                const auto* attachment = scratch_skeleton.current_attachment(slot_index);
                if (attachment == nullptr ||
                    (attachment->kind != marrow::runtime::AttachmentKind::Mesh &&
                     attachment->kind != marrow::runtime::AttachmentKind::LinkedMesh)) {
                    continue;
                }

                if (!scratch_skeleton.evaluate_current_mesh_attachment(slot_index).has_value()) {
                    render_ready = false;
                    return;
                }
            }
        });
    if (!render_ready) {
        return std::nullopt;
    }

    marrow::runtime::profile_phase(
        &profiler,
        marrow::runtime::ProfilerPhase::Render,
        [&]() {
            const marrow::renderer::PreparedSceneResult scene_result =
                marrow::renderer::prepare_setup_pose_scene(
                    scratch_skeleton,
                    *state.load_result.atlas_data.front());
            if (!scene_result) {
                render_ready = false;
                return;
            }

            const marrow::renderer::PreparedSceneBatchSummary batch_summary =
                marrow::renderer::summarize_prepared_scene_batches(*scene_result.scene);
            if (!batch_summary) {
                render_ready = false;
                return;
            }

            marrow::runtime::ProfilerDrawStats draw_stats;
            draw_stats.skeleton_count = batch_summary.skeleton_count;
            draw_stats.draw_calls = batch_summary.draw_call_count;
            draw_stats.vertices = batch_summary.vertex_count;
            draw_stats.batch_merges = batch_summary.merged_draw_calls;
            draw_stats.break_reasons.texture_changes =
                batch_summary.break_reasons.texture_changes;
            draw_stats.break_reasons.blend_changes =
                batch_summary.break_reasons.blend_changes;
            draw_stats.break_reasons.clip_changes =
                batch_summary.break_reasons.clip_changes;
            draw_stats.break_reasons.shader_changes =
                batch_summary.break_reasons.shader_changes;
            profiler.add_draw_stats(draw_stats);
        });
    if (!render_ready) {
        return std::nullopt;
    }

    profiler.end_frame();
    return marrow::runtime::marrow_profiler_frame(profiler);
}

} // namespace marrow::editor::shell
