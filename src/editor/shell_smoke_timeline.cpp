#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

#include "imgui.h"
#include "imgui_internal.h"

#include "shell_constraints.hpp"
#include "shell_asset_watch.hpp"
#include "shell_agent_panel.hpp"
#include "shell_coalesced_edit.hpp"
#include "shell_derived_cache.hpp"
#include "shell_inspector.hpp"
#include "shell_project_panels.hpp"
#include "shell_parameters.hpp"
#include "shell_smoke_scenarios.hpp"
#include "shell_preview.hpp"
#include "shell_selection.hpp"
#include "shell_timeline.hpp"
#include "shell_weight_paint.hpp"
#include "shell_viewport_ui.hpp"
#include "shell_state.hpp"
#include "viewport_renderer.hpp"
#include "marrow/allocator.hpp"
#include "marrow/editor/module.hpp"
#include "marrow/editor/authoring.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/renderer/module.hpp"
#include "marrow/runtime/animation_state.hpp"
#include "marrow/runtime/profiler.hpp"

namespace marrow::editor::shell {

bool validate_timeline_p0_authoring_smoke(const std::filesystem::path& project_path) {
    ShellState state;
    state.project_path = project_path;
    if (!reload_project(&state) ||
        !set_selected_animation(&state, "idle", "Timeline P0 smoke", false, true)) {
        std::cerr << "Timeline P0 smoke could not load the idle animation.\n";
        return false;
    }
    const auto build_tracks = [&]() {
        const auto* animation = selected_animation(state);
        return animation != nullptr
            ? build_timeline_tracks(*state.load_result.skeleton_data, *animation)
            : std::vector<TimelineTrackRow>{};
    };
    std::vector<TimelineTrackRow> tracks = build_tracks();
    const auto find_track = [&](std::string_view suffix) -> const TimelineTrackRow* {
        const auto iterator = std::find_if(
            tracks.begin(), tracks.end(), [&](const TimelineTrackRow& track) {
                return track.id.find(suffix) != std::string::npos;
            });
        return iterator == tracks.end() ? nullptr : &(*iterator);
    };
    const TimelineTrackRow* color_track = find_track(":Color");
    const TimelineTrackRow* attachment_track = find_track(":Attachment");
    const TimelineTrackRow* event_track = find_track("global:events");
    if (color_track == nullptr || attachment_track == nullptr || event_track == nullptr ||
        !color_track->slot_index.has_value()) {
        std::cerr << "Timeline P0 smoke could not resolve slot and event lanes.\n";
        return false;
    }
    const auto imported_transform = std::find_if(
        tracks.begin(), tracks.end(), [&](const TimelineTrackRow& track) {
            if (!track.transform_channel.has_value() || !track.bone_index.has_value() ||
                track.key_times.empty()) {
                return false;
            }
            const std::string& bone_name =
                state.load_result.skeleton_data->bones()[*track.bone_index].name;
            return state.load_result.project->find_transform_timeline_edit(
                "idle", bone_name, *track.transform_channel) == nullptr;
        });
    if (imported_transform == tracks.end()) {
        std::cerr << "Timeline P0 smoke could not resolve an imported transform lane.\n";
        return false;
    }
    state.selected_timeline_track_id = imported_transform->id;
    state.timeline_editor.selected_keys.clear();
    if (!scrub_timeline_time(
            &state, imported_transform->key_times.front(), "Timeline P0 smoke", false) ||
        remove_selected_timeline_keys(&state, tracks) ||
        state.status_message.find("imported") == std::string::npos) {
        std::cerr << "Timeline P0 toolbar remove did not preserve an unselected imported key.\n";
        return false;
    }
    const std::string slot_name =
        state.load_result.skeleton_data->slots()[*color_track->slot_index].name;
    if (!ensure_slot_color_timeline_edit_index(&state, *color_track).has_value() ||
        !ensure_slot_attachment_timeline_edit_index(&state, *attachment_track).has_value()) {
        std::cerr << "Timeline P0 smoke could not materialize typed slot edit overlays.\n";
        return false;
    }
    const auto* baseline_color = state.load_result.project->find_slot_color_timeline_edit(
        "idle", slot_name);
    const auto* baseline_attachment =
        state.load_result.project->find_slot_attachment_timeline_edit("idle", slot_name);
    if (baseline_color == nullptr || baseline_attachment == nullptr) {
        std::cerr << "Timeline P0 smoke did not load typed slot edit overlays.\n";
        return false;
    }
    const std::size_t baseline_color_count = baseline_color->keyframes.size();
    const std::size_t baseline_attachment_count = baseline_attachment->keyframes.size();

    const std::string epsilon_add_baseline =
        marrow::editor::serialize_project(*state.session.project());
    const double epsilon_add_time = baseline_color->keyframes[1].time + 0.5e-6;
    state.selected_timeline_track_id = color_track->id;
    if (!scrub_timeline_time(&state, epsilon_add_time, "Timeline P0 smoke", false) ||
        !add_timeline_key_at_playhead(&state, *color_track)) {
        std::cerr << "Timeline P0 epsilon replacement could not run.\n";
        return false;
    }
    const auto* epsilon_color = state.load_result.project->find_slot_color_timeline_edit(
        "idle", slot_name);
    if (epsilon_color == nullptr || epsilon_color->keyframes.size() != baseline_color_count ||
        !state.session.undo()) {
        std::cerr << "Timeline P0 epsilon replacement inserted a duplicate key.\n";
        return false;
    }
    sync_shell_from_editor_session(&state);
    state.session.clear_history();
    if (marrow::editor::serialize_project(*state.session.project()) !=
        epsilon_add_baseline) {
        std::cerr << "Timeline P0 epsilon replacement did not undo exactly.\n";
        return false;
    }

    if (!scrub_timeline_time(&state, 0.333, "Timeline P0 smoke", false) ||
        !add_timeline_key_at_playhead(&state, *color_track)) {
        std::cerr << "Timeline P0 toolbar add did not create a slot-color key.\n";
        return false;
    }
    const auto* added_color = state.load_result.project->find_slot_color_timeline_edit(
        "idle", slot_name);
    if (added_color == nullptr || added_color->keyframes.size() != baseline_color_count + 1U ||
        state.timeline_editor.selected_keys.size() != 1U) {
        std::cerr << "Timeline P0 toolbar add did not retain the inserted slot-color selection"
                  << " (baseline=" << baseline_color_count
                  << ", current=" << (added_color != nullptr ? added_color->keyframes.size() : 0U)
                  << ", selected=" << state.timeline_editor.selected_keys.size() << ").\n";
        return false;
    }
    tracks = build_tracks();
    color_track = find_track(":Color");
    reconcile_timeline_key_selection(&state, tracks);
    if (color_track == nullptr || state.timeline_editor.selected_keys.size() != 1U) {
        std::cerr << "Timeline P0 inserted-key selection did not survive the runtime rebuild.\n";
        return false;
    }
    if (!remove_selected_timeline_keys(&state, tracks)) {
        std::cerr << "Timeline P0 toolbar remove did not remove the selected slot-color key.\n";
        return false;
    }
    const auto* restored_color = state.load_result.project->find_slot_color_timeline_edit(
        "idle", slot_name);
    if (restored_color == nullptr || restored_color->keyframes.size() != baseline_color_count) {
        std::cerr << "Timeline P0 toolbar remove left the slot-color lane changed.\n";
        return false;
    }
    state.selected_timeline_track_id = color_track->id;
    state.timeline_editor.selected_keys.clear();
    if (!scrub_timeline_time(&state, 0.337, "Timeline P0 smoke", false) ||
        remove_selected_timeline_keys(&state, tracks) ||
        state.load_result.project->find_slot_color_timeline_edit("idle", slot_name)
                ->keyframes.size() != baseline_color_count) {
        std::cerr << "Timeline P0 toolbar remove chose a nearest key away from the playhead.\n";
        return false;
    }
    const double exact_authored_time = restored_color->keyframes.front().time;
    if (!scrub_timeline_time(&state, exact_authored_time, "Timeline P0 smoke", false) ||
        !remove_selected_timeline_keys(&state, tracks)) {
        std::cerr << "Timeline P0 toolbar remove did not remove an exact authored key.\n";
        return false;
    }
    tracks = build_tracks();
    color_track = find_track(":Color");
    if (!add_timeline_key_at_playhead(&state, *color_track)) {
        std::cerr << "Timeline P0 smoke could not restore the exact-key removal fixture.\n";
        return false;
    }

    tracks = build_tracks();
    color_track = find_track(":Color");
    attachment_track = find_track(":Attachment");
    event_track = find_track("global:events");
    state.timeline_editor.selected_keys = {
        timeline_key_ref(*color_track, 0U),
        timeline_key_ref(*attachment_track, 0U)};
    if (!copy_selected_timeline_keys(&state, tracks) ||
        state.timeline_editor.clipboard.project_fragment.slot_color_timeline_edits.size() != 1U ||
        state.timeline_editor.clipboard.project_fragment.slot_attachment_timeline_edits.size() != 1U ||
        !scrub_timeline_time(&state, 0.75, "Timeline P0 smoke", false) ||
        !paste_timeline_clipboard(&state, tracks)) {
        std::cerr << "Timeline P0 typed cross-lane copy/paste failed.\n";
        return false;
    }
    const auto* pasted_color = state.load_result.project->find_slot_color_timeline_edit(
        "idle", slot_name);
    const auto* pasted_attachment =
        state.load_result.project->find_slot_attachment_timeline_edit("idle", slot_name);
    const auto has_key_at = [](const auto* edit, double time) {
        return edit != nullptr && std::any_of(
            edit->keyframes.begin(), edit->keyframes.end(), [&](const auto& key) {
                return std::abs(key.time - time) <= 1e-6;
            });
    };
    if (!has_key_at(pasted_color, 0.75) || !has_key_at(pasted_attachment, 0.75) ||
        pasted_color->keyframes.size() != baseline_color_count + 1U ||
        pasted_attachment->keyframes.size() != baseline_attachment_count + 1U) {
        std::cerr << "Timeline P0 paste did not align the earliest typed keys to the playhead.\n";
        return false;
    }

    tracks = build_tracks();
    event_track = find_track("global:events");
    if (event_track == nullptr || event_track->key_times.size() < 2U ||
        std::abs(event_track->key_times[0] - event_track->key_times[1]) > 1e-6) {
        std::cerr << "Timeline P0 smoke requires the two stable same-time fixture events.\n";
        return false;
    }
    state.timeline_editor.selected_keys = {
        timeline_key_ref(*event_track, 0U), timeline_key_ref(*event_track, 1U)};
    if (state.timeline_editor.selected_keys[0] == state.timeline_editor.selected_keys[1] ||
        timeline_key_index(*event_track, state.timeline_editor.selected_keys[0]) != 0U ||
        timeline_key_index(*event_track, state.timeline_editor.selected_keys[1]) != 1U) {
        std::cerr << "Timeline P0 same-time event identities are not distinct.\n";
        return false;
    }
    TimelineTrackRow shifted_event_track = *event_track;
    shifted_event_track.key_times.insert(
        shifted_event_track.key_times.begin(), event_track->key_times.front() - 0.1);
    if (timeline_key_index(shifted_event_track, state.timeline_editor.selected_keys[0]) != 1U ||
        timeline_key_index(shifted_event_track, state.timeline_editor.selected_keys[1]) != 2U) {
        std::cerr << "Timeline P0 key identities did not survive an unrelated insertion.\n";
        return false;
    }
    const std::vector<TimelineKeyRef> same_time_event_selection =
        state.timeline_editor.selected_keys;
    reconcile_timeline_key_selection(&state, {shifted_event_track});
    if (state.timeline_editor.selected_keys != same_time_event_selection) {
        std::cerr << "Timeline P0 selection did not survive a rebuilt shifted lane.\n";
        return false;
    }
    TimelineTrackRow reduced_same_time_track = *event_track;
    reduced_same_time_track.key_times.erase(reduced_same_time_track.key_times.begin());
    if (timeline_key_index(
            reduced_same_time_track, state.timeline_editor.selected_keys[1]).has_value()) {
        std::cerr << "Timeline P0 key identity silently retargeted after same-time removal.\n";
        return false;
    }
    state.timeline_editor.selected_keys = same_time_event_selection;
    reconcile_timeline_key_selection(&state, {reduced_same_time_track});
    if (!state.timeline_editor.selected_keys.empty()) {
        std::cerr << "Timeline P0 selection retained an ambiguous same-time identity.\n";
        return false;
    }
    state.timeline_editor.selected_keys = same_time_event_selection;
    if (!copy_selected_timeline_keys(&state, tracks) ||
        !scrub_timeline_time(&state, 0.8, "Timeline P0 smoke", false) ||
        !paste_timeline_clipboard(&state, tracks)) {
        std::cerr << "Timeline P0 same-time event paste failed.\n";
        return false;
    }
    const auto* events = state.load_result.project->find_event_timeline_edit("idle");
    std::vector<std::string> pasted_event_names;
    if (events != nullptr) {
        for (const auto& key : events->keyframes) {
            if (std::abs(key.time - 0.8) <= 1e-6) pasted_event_names.push_back(key.event_name);
        }
    }
    if (pasted_event_names.size() != 2U ||
        pasted_event_names[0] != "footstep" || pasted_event_names[1] != "dust_vfx") {
        std::cerr << "Timeline P0 paste did not preserve stable same-time event ordering.\n";
        return false;
    }

    tracks = build_tracks();
    std::vector<const TimelineTrackRow*> translate_tracks;
    for (const TimelineTrackRow& track : tracks) {
        if (track.transform_channel ==
            std::optional<marrow::editor::TransformTimelineChannel>(
                marrow::editor::TransformTimelineChannel::Translate)) {
            translate_tracks.push_back(&track);
        }
    }
    if (translate_tracks.size() < 2U) {
        std::cerr << "Timeline P0 smoke requires two compatible translate lanes.\n";
        return false;
    }
    const TimelineTrackRow& source_translate = *translate_tracks[0];
    const TimelineTrackRow& target_translate = *translate_tracks[1];
    state.timeline_editor.selected_keys = {timeline_key_ref(source_translate, 0U)};
    if (!copy_selected_timeline_keys(&state, tracks)) {
        std::cerr << "Timeline P0 compatible-lane copy failed.\n";
        return false;
    }
    state.selected_timeline_track_id = target_translate.id;
    constexpr double kRemappedPasteTime = 0.913;
    if (!scrub_timeline_time(&state, kRemappedPasteTime, "Timeline P0 smoke", false) ||
        !paste_timeline_clipboard(&state, tracks)) {
        std::cerr << "Timeline P0 compatible single-lane remap paste failed.\n";
        return false;
    }
    const std::string& target_bone_name =
        state.load_result.skeleton_data->bones()[*target_translate.bone_index].name;
    const auto* remapped_translate =
        state.load_result.project->find_transform_timeline_edit(
            "idle",
            target_bone_name,
            marrow::editor::TransformTimelineChannel::Translate);
    if (!has_key_at(remapped_translate, kRemappedPasteTime) ||
        state.selected_timeline_track_id != target_translate.id) {
        std::cerr << "Timeline P0 paste did not remap the single compatible lane.\n";
        return false;
    }

    const auto build_runtime_tracks = [&]() {
        const auto* animation =
            state.session.runtime_data()->find_animation("idle");
        return animation != nullptr
            ? build_timeline_tracks(*state.session.runtime_data(), *animation)
            : std::vector<TimelineTrackRow>{};
    };
    tracks = build_runtime_tracks();
    color_track = find_track(":Color");
    attachment_track = find_track(":Attachment");
    if (color_track == nullptr || attachment_track == nullptr ||
        color_track->key_times.size() < 2U ||
        attachment_track->key_times.size() < 2U) {
        std::cerr << "Timeline P0 retime smoke could not rebuild editable lanes.\n";
        return false;
    }
    state.session.clear_history();
    const std::string retime_baseline =
        marrow::editor::serialize_project(*state.session.project());

    state.timeline_editor.selected_keys = {timeline_key_ref(*color_track, 0U)};
    const double snap_anchor = color_track->key_times[0];
    const double frame_seconds = 1.0 / state.timeline_editor.frames_per_second;
    const double expected_snapped =
        std::round((snap_anchor + 0.021) / frame_seconds) * frame_seconds -
        snap_anchor;
    if (!begin_timeline_retime_gesture(
            &state, 0U, ImGui::GetIO().MousePos.x, tracks) ||
        !apply_timeline_retime_delta(&state, tracks, 0.021, true) ||
        !state.timeline_editor.retime_gesture.has_value() ||
        std::abs(
            state.timeline_editor.retime_gesture->applied_delta -
            expected_snapped) > 1e-6) {
        std::cerr << "Timeline P0 production retime path did not snap to 60 FPS.\n";
        return false;
    }
    finish_timeline_retime_gesture(&state, false);
    if (marrow::editor::serialize_project(*state.session.project()) != retime_baseline) {
        std::cerr << "Timeline P0 snapped retime did not roll back exactly.\n";
        return false;
    }

    tracks = build_runtime_tracks();
    color_track = find_track(":Color");
    state.timeline_editor.selected_keys = {timeline_key_ref(*color_track, 0U)};
    const double expected_clamped =
        color_track->key_times[1] - 0.001 - color_track->key_times[0];
    if (!begin_timeline_retime_gesture(
            &state, 0U, ImGui::GetIO().MousePos.x, tracks) ||
        !apply_timeline_retime_delta(&state, tracks, 1.0, false) ||
        !state.timeline_editor.retime_gesture.has_value() ||
        std::abs(
            state.timeline_editor.retime_gesture->applied_delta -
            expected_clamped) > 1e-6) {
        std::cerr << "Timeline P0 production retime path crossed an unselected neighbor.\n";
        return false;
    }
    finish_timeline_retime_gesture(&state, false);
    if (marrow::editor::serialize_project(*state.session.project()) != retime_baseline) {
        std::cerr << "Timeline P0 clamped retime did not roll back exactly.\n";
        return false;
    }

    tracks = build_runtime_tracks();
    color_track = find_track(":Color");
    attachment_track = find_track(":Attachment");
    state.timeline_editor.selected_keys = {
        timeline_key_ref(*color_track, 0U),
        timeline_key_ref(*attachment_track, 0U)};
    if (!begin_timeline_retime_gesture(
            &state, 0U, ImGui::GetIO().MousePos.x, tracks) ||
        !apply_timeline_retime_delta(&state, tracks, 0.02, false) ||
        marrow::editor::serialize_project(*state.session.project()) == retime_baseline) {
        std::cerr << "Timeline P0 live retime did not update the project preview.\n";
        return false;
    }
    finish_timeline_retime_gesture(&state, false);
    if (marrow::editor::serialize_project(*state.session.project()) != retime_baseline ||
        state.session.can_undo()) {
        std::cerr << "Timeline P0 retime Escape rollback was not exact.\n";
        return false;
    }

    tracks = build_runtime_tracks();
    color_track = find_track(":Color");
    attachment_track = find_track(":Attachment");
    state.timeline_editor.selected_keys = {
        timeline_key_ref(*color_track, 0U),
        timeline_key_ref(*attachment_track, 0U)};
    if (!begin_timeline_retime_gesture(
            &state, 0U, ImGui::GetIO().MousePos.x, tracks) ||
        !apply_timeline_retime_delta(&state, tracks, 0.02, false)) {
        std::cerr << "Timeline P0 committed retime could not start.\n";
        return false;
    }
    finish_timeline_retime_gesture(&state, true);
    const std::string retime_committed =
        marrow::editor::serialize_project(*state.session.project());
    if (retime_committed == retime_baseline || state.session.undo_count() != 1U ||
        !state.session.undo() ||
        marrow::editor::serialize_project(*state.session.project()) != retime_baseline ||
        !state.session.redo() ||
        marrow::editor::serialize_project(*state.session.project()) != retime_committed ||
        !state.session.undo()) {
        std::cerr << "Timeline P0 retime did not commit/undo/redo as one transaction.\n";
        return false;
    }
    sync_shell_from_editor_session(&state);
    state.session.clear_history();
    return true;
}

bool validate_derived_cache_smoke(ShellState* state) {
    if (state == nullptr || !state->load_result ||
        state->load_result.project == nullptr ||
        state->load_result.skeleton_data == nullptr ||
        state->session.runtime_data() == nullptr) {
        std::cerr << "Derived cache smoke requires a loaded runtime project.\n";
        return false;
    }

    if (state->selected_animation_name.empty() ||
        state->session.runtime_data()->find_animation(
            state->selected_animation_name) == nullptr) {
        if (state->session.runtime_data()->animations().empty() ||
            !state->session.select_animation(
                state->session.runtime_data()->animations().front().name)) {
            std::cerr << "Derived cache smoke requires one selectable animation.\n";
            return false;
        }
        sync_shell_from_editor_session(state);
    }

    const auto arm_slot_index =
        state->load_result.skeleton_data->find_slot_index("arm_l");
    if (!arm_slot_index.has_value()) {
        std::cerr << "Derived cache smoke requires the arm_l slot.\n";
        return false;
    }

    (void)cached_timeline_tracks(state);
    const auto& attachment_refs =
        cached_slot_attachments(state, *arm_slot_index);
    const auto& attachment_names =
        cached_timeline_attachment_names(state, *arm_slot_index);
    const std::uint64_t initial_timeline_generation =
        state->timeline_track_cache.generation;
    const std::uint64_t initial_slot_generation =
        state->slot_derived_cache.generation;

    std::vector<std::size_t> duplicate_skin_indices;
    std::vector<const marrow::runtime::AttachmentData*> duplicate_attachments;
    for (const SlotAttachmentReference& reference : attachment_refs) {
        if (reference.attachment != nullptr &&
            reference.attachment->name == "mage_arm_l" &&
            reference.skin_index.has_value()) {
            duplicate_skin_indices.push_back(*reference.skin_index);
            duplicate_attachments.push_back(reference.attachment);
        }
    }
    if (duplicate_skin_indices.size() != 2U ||
        duplicate_skin_indices[0] == duplicate_skin_indices[1] ||
        duplicate_attachments[0] == duplicate_attachments[1] ||
        !std::is_sorted(attachment_names.begin(), attachment_names.end()) ||
        std::adjacent_find(attachment_names.begin(), attachment_names.end()) !=
            attachment_names.end() ||
        std::count(
            attachment_names.begin(), attachment_names.end(), "mage_arm_l") != 1 ||
        state->slot_derived_cache.runtime.get() !=
            state->load_result.skeleton_data.get()) {
        std::cerr <<
            "Slot cache did not preserve authored skin identity and sorted-unique names.\n";
        return false;
    }

    (void)cached_timeline_tracks(state);
    (void)cached_slot_attachments(state, *arm_slot_index);
    (void)cached_timeline_attachment_names(state, *arm_slot_index);
    if (state->timeline_track_cache.generation != initial_timeline_generation ||
        state->slot_derived_cache.generation != initial_slot_generation) {
        std::cerr << "Repeated derived-cache lookup rebuilt an unchanged key.\n";
        return false;
    }

    const bool loop_before = state->session.preview_state().loop;
    const std::uint64_t runtime_revision_before_preview =
        state->session.runtime_revision();
    const std::uint64_t preview_revision_before = state->session.preview_revision();
    if (!state->session.set_loop(!loop_before)) {
        std::cerr << "Derived cache smoke could not stage a preview-only revision.\n";
        return false;
    }
    sync_shell_from_editor_session(state);
    (void)cached_timeline_tracks(state);
    (void)cached_slot_attachments(state, *arm_slot_index);
    if (state->session.preview_revision() <= preview_revision_before ||
        state->session.runtime_revision() != runtime_revision_before_preview ||
        state->timeline_track_cache.generation != initial_timeline_generation ||
        state->slot_derived_cache.generation != initial_slot_generation ||
        !state->session.set_loop(loop_before)) {
        std::cerr << "Preview-only state invalidated a runtime-derived cache.\n";
        return false;
    }
    sync_shell_from_editor_session(state);

    std::uint64_t timeline_generation_before_runtime =
        initial_timeline_generation;
    const std::string original_animation_name = state->selected_animation_name;
    const double original_time = state->timeline_time_seconds;
    const auto alternate_animation = std::find_if(
        state->session.runtime_data()->animations().begin(),
        state->session.runtime_data()->animations().end(),
        [&](const auto& candidate) {
            return candidate.name != original_animation_name;
        });
    if (alternate_animation != state->session.runtime_data()->animations().end()) {
        if (!state->session.select_animation(alternate_animation->name)) {
            std::cerr << "Derived cache smoke could not select an alternate animation.\n";
            return false;
        }
        sync_shell_from_editor_session(state);
        (void)cached_timeline_tracks(state);
        if (state->timeline_track_cache.generation !=
                initial_timeline_generation + 1U ||
            state->slot_derived_cache.generation != initial_slot_generation ||
            !state->session.select_animation(original_animation_name) ||
            !state->session.seek(original_time)) {
            std::cerr << "Animation-name cache key did not refresh independently.\n";
            return false;
        }
        sync_shell_from_editor_session(state);
        (void)cached_timeline_tracks(state);
        if (state->timeline_track_cache.generation !=
                initial_timeline_generation + 2U ||
            state->slot_derived_cache.generation != initial_slot_generation) {
            std::cerr << "Restored animation cache key did not rebuild exactly once.\n";
            return false;
        }
        timeline_generation_before_runtime = initial_timeline_generation + 2U;
    }

    const std::string project_before =
        marrow::editor::serialize_project(*state->session.project());
    const auto* animation = state->session.runtime_data()->find_animation(
        state->selected_animation_name);
    const std::size_t bone_index = state->session.runtime_data()
        ->find_bone_index("spine")
        .value_or(0U);
    if (animation == nullptr || bone_index >= state->session.runtime_data()->bones().size()) {
        std::cerr << "Derived cache smoke could not resolve an editable track.\n";
        return false;
    }
    const std::string bone_name = state->session.runtime_data()->bones()[bone_index].name;
    const double edit_time = animation->duration() > 0.002
        ? std::min(animation->duration() - 0.001, animation->duration() * 0.371)
        : 0.0;
    const double edit_rotation =
        state->session.runtime_data()->bones()[bone_index].setup_pose.rotation + 17.25;
    state->session.clear_history();
    auto transaction = state->session.begin_edit({
        marrow::editor::EditKind::AddKeyframe,
        "Derived cache runtime edit smoke",
        {},
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        std::cerr << transaction.error()->format() << '\n';
        return false;
    }
    marrow::editor::upsert_transform_keyframe(
        *transaction.project(),
        *state->session.runtime_data(),
        state->selected_animation_name,
        bone_name,
        marrow::editor::TransformTimelineChannel::Rotate,
        edit_time,
        marrow::editor::TransformKeyframePatch{
            edit_rotation,
            std::nullopt,
            std::nullopt});
    const marrow::editor::SessionResult committed = transaction.commit();
    sync_shell_from_editor_session(state);
    if (!committed || !committed.changed) {
        std::cerr << "Derived cache smoke runtime edit did not commit.\n";
        return false;
    }

    const auto refresh_and_check_generation = [&](std::uint64_t timeline_generation,
                                                   std::uint64_t slot_generation) {
        (void)cached_timeline_tracks(state);
        (void)cached_slot_attachments(state, *arm_slot_index);
        return state->timeline_track_cache.generation == timeline_generation &&
            state->slot_derived_cache.generation == slot_generation &&
            state->slot_derived_cache.runtime.get() ==
                state->load_result.skeleton_data.get();
    };
    if (!refresh_and_check_generation(
            timeline_generation_before_runtime + 1U,
            initial_slot_generation + 1U) ||
        !state->session.undo()) {
        std::cerr << "Runtime edit did not refresh both derived caches.\n";
        return false;
    }
    sync_shell_from_editor_session(state);
    if (!refresh_and_check_generation(
            timeline_generation_before_runtime + 2U,
            initial_slot_generation + 2U) ||
        !state->session.redo()) {
        std::cerr << "Runtime undo did not refresh both derived caches.\n";
        return false;
    }
    sync_shell_from_editor_session(state);
    if (!refresh_and_check_generation(
            timeline_generation_before_runtime + 3U,
            initial_slot_generation + 3U) ||
        !state->session.undo()) {
        std::cerr << "Runtime redo did not refresh both derived caches.\n";
        return false;
    }
    sync_shell_from_editor_session(state);
    if (!refresh_and_check_generation(
            timeline_generation_before_runtime + 4U,
            initial_slot_generation + 4U) ||
        marrow::editor::serialize_project(*state->session.project()) != project_before) {
        std::cerr << "Final cache smoke rollback did not restore the source project.\n";
        return false;
    }
    state->session.clear_history();
    return true;
}


bool validate_timeline_project_smoke(ShellState& shell_state) {
    const auto spine_index =
        shell_state.load_result.skeleton_data->find_bone_index("spine");
    if (!spine_index.has_value()) {
        std::cerr << "Timeline smoke validation requires the spine bone.\n";
        return false;
    }

    if (!set_selected_animation(&shell_state, "idle", "Smoke", false, true)) {
        std::cerr << "Animation selection smoke validation failed for idle.\n";
        return false;
    }
    const marrow::runtime::AnimationData* idle_animation = selected_animation(shell_state);
    if (idle_animation == nullptr) {
        std::cerr << "Timeline smoke validation could not resolve the idle animation.\n";
        return false;
    }

    const std::vector<TimelineTrackRow> idle_tracks =
        build_timeline_tracks(*shell_state.load_result.skeleton_data, *idle_animation);
    if (idle_tracks.empty()) {
        std::cerr << "Timeline panel did not expose keyed tracks for the idle animation.\n";
        return false;
    }

    const auto find_onion_ghost =
        [](const std::vector<OnionSkinGhostPose>& ghosts,
           double time_seconds,
           bool before_current) -> const OnionSkinGhostPose* {
            const auto iterator = std::find_if(
                ghosts.begin(),
                ghosts.end(),
                [&](const OnionSkinGhostPose& ghost) {
                    return ghost.before_current == before_current &&
                        std::abs(ghost.time_seconds - time_seconds) <= 1e-6;
                });
            return iterator != ghosts.end() ? &(*iterator) : nullptr;
        };

    shell_state.viewport.onion_skin.enabled = true;
    shell_state.viewport.onion_skin.mode = marrow::editor::OnionSkinMode::Frame;
    shell_state.viewport.onion_skin.anchor_to_zero = false;
    shell_state.viewport.onion_skin.before_count = 2;
    shell_state.viewport.onion_skin.after_count = 2;
    shell_state.viewport.onion_skin.step = 15;
    if (!scrub_timeline_time(&shell_state, 0.5, "Smoke", false)) {
        std::cerr << "Onion-skin smoke could not scrub the idle clip to 0.5s.\n";
        return false;
    }
    const auto onion_layout = build_viewport_layout(
        shell_state,
        ImVec2(0.0f, 0.0f),
        ImVec2(1280.0f, 720.0f));
    if (!onion_layout.has_value()) {
        std::cerr << "Onion-skin smoke could not build a viewport layout.\n";
        return false;
    }
    const std::vector<OnionSkinGhostPose> frame_ghosts =
        build_onion_skin_ghost_poses(shell_state, *onion_layout);
    const OnionSkinGhostPose* frame_before_near = find_onion_ghost(frame_ghosts, 0.25, true);
    const OnionSkinGhostPose* frame_before_far = find_onion_ghost(frame_ghosts, 0.0, true);
    const OnionSkinGhostPose* frame_after_near = find_onion_ghost(frame_ghosts, 0.75, false);
    const OnionSkinGhostPose* frame_after_far = find_onion_ghost(frame_ghosts, 1.0, false);
    if (frame_ghosts.size() != 4U ||
        frame_before_near == nullptr ||
        frame_before_far == nullptr ||
        frame_after_near == nullptr ||
        frame_after_far == nullptr) {
        std::cerr << "Frame onion-skin smoke did not generate the expected 2+2 ghost samples.\n";
        return false;
    }
    const ImVec4 before_near_color = ImGui::ColorConvertU32ToFloat4(frame_before_near->line_color);
    const ImVec4 before_far_color = ImGui::ColorConvertU32ToFloat4(frame_before_far->line_color);
    const ImVec4 after_near_color = ImGui::ColorConvertU32ToFloat4(frame_after_near->line_color);
    const ImVec4 after_far_color = ImGui::ColorConvertU32ToFloat4(frame_after_far->line_color);
    if (!(before_near_color.z > before_near_color.x) ||
        !(after_near_color.x > after_near_color.z) ||
        !(before_near_color.w > before_far_color.w) ||
        !(after_near_color.w > after_far_color.w)) {
        std::cerr << "Frame onion-skin smoke did not apply the expected blue/red tint and alpha falloff.\n";
        return false;
    }

    std::vector<ViewportRenderVertex> baseline_line_vertices;
    std::vector<ViewportRenderVertex> baseline_triangle_vertices;
    build_viewport_render_geometry(
        shell_state,
        *onion_layout,
        {},
        std::nullopt,
        nullptr,
        &baseline_line_vertices,
        &baseline_triangle_vertices);
    std::vector<ViewportRenderVertex> onion_line_vertices;
    std::vector<ViewportRenderVertex> onion_triangle_vertices;
    build_viewport_render_geometry(
        shell_state,
        *onion_layout,
        frame_ghosts,
        std::nullopt,
        nullptr,
        &onion_line_vertices,
        &onion_triangle_vertices);
    if (onion_line_vertices.size() <= baseline_line_vertices.size() ||
        onion_triangle_vertices.size() <= baseline_triangle_vertices.size()) {
        std::cerr << "Viewport onion-skin smoke did not add ghost geometry to the render pass.\n";
        return false;
    }

    shell_state.viewport.onion_skin.anchor_to_zero = true;
    shell_state.viewport.onion_skin.before_count = 1;
    shell_state.viewport.onion_skin.after_count = 1;
    if (!scrub_timeline_time(&shell_state, 0.55, "Smoke", false)) {
        std::cerr << "Onion-skin anchor smoke could not scrub the idle clip to 0.55s.\n";
        return false;
    }
    const std::vector<OnionSkinGhostPose> anchored_ghosts =
        build_onion_skin_ghost_poses(shell_state, *onion_layout);
    if (anchored_ghosts.size() != 2U ||
        find_onion_ghost(anchored_ghosts, 0.5, true) == nullptr ||
        find_onion_ghost(anchored_ghosts, 0.75, false) == nullptr) {
        std::cerr << "Anchor onion-skin smoke did not snap samples to frame-0 intervals.\n";
        return false;
    }

    shell_state.viewport.onion_skin.mode = marrow::editor::OnionSkinMode::Keyframe;
    shell_state.viewport.onion_skin.anchor_to_zero = false;
    shell_state.viewport.onion_skin.before_count = 2;
    shell_state.viewport.onion_skin.after_count = 2;
    shell_state.viewport.onion_skin.step = 1;
    if (!scrub_timeline_time(&shell_state, 0.6, "Smoke", false)) {
        std::cerr << "Onion-skin keyframe smoke could not scrub the idle clip to 0.6s.\n";
        return false;
    }
    const std::vector<OnionSkinGhostPose> keyframe_ghosts =
        build_onion_skin_ghost_poses(shell_state, *onion_layout);
    if (keyframe_ghosts.size() != 4U ||
        find_onion_ghost(keyframe_ghosts, 0.5, true) == nullptr ||
        find_onion_ghost(keyframe_ghosts, 0.25, true) == nullptr ||
        find_onion_ghost(keyframe_ghosts, 0.8, false) == nullptr ||
        find_onion_ghost(keyframe_ghosts, 1.0, false) == nullptr) {
        std::cerr << "Keyframe onion-skin smoke did not sample the expected authored key positions.\n";
        return false;
    }

    shell_state.viewport.onion_skin = {};
    if (!scrub_timeline_time(&shell_state, 0.5, "Smoke", false)) {
        std::cerr << "Onion-skin smoke could not restore the idle playhead.\n";
        return false;
    }

    const auto require_debug_overlay_stats =
        [](const DebugOverlayStats& stats,
           std::size_t ik_constraints,
           std::size_t path_constraints,
           std::size_t physics_constraints,
           std::size_t mesh_attachments,
           std::size_t bounding_boxes,
           std::string_view label) {
            if (stats.ik_constraint_count != ik_constraints ||
                stats.path_constraint_count != path_constraints ||
                stats.physics_constraint_count != physics_constraints ||
                stats.mesh_attachment_count != mesh_attachments ||
                stats.bounding_box_count != bounding_boxes) {
                std::cerr << label
                          << " expected counts IK=" << ik_constraints
                          << ", Path=" << path_constraints
                          << ", Physics=" << physics_constraints
                          << ", Meshes=" << mesh_attachments
                          << ", Bounds=" << bounding_boxes
                          << " but observed IK=" << stats.ik_constraint_count
                          << ", Path=" << stats.path_constraint_count
                          << ", Physics=" << stats.physics_constraint_count
                          << ", Meshes=" << stats.mesh_attachment_count
                          << ", Bounds=" << stats.bounding_box_count << ".\n";
                return false;
            }
            return true;
        };

    if (!shell_state.viewport.debug_overlay.bones ||
        !shell_state.viewport.debug_overlay.ik_constraints ||
        !shell_state.viewport.debug_overlay.path_constraints ||
        !shell_state.viewport.debug_overlay.physics_constraints ||
        !shell_state.viewport.debug_overlay.mesh_wireframes ||
        !shell_state.viewport.debug_overlay.bounding_boxes) {
        std::cerr << "Debug overlay smoke expected the fixture viewport toggles to load as enabled.\n";
        return false;
    }

    const auto debug_warrior_skin_index =
        shell_state.load_result.skeleton_data->find_skin_index("warrior");
    if (!debug_warrior_skin_index.has_value() ||
        !set_preview_skin_enabled(&shell_state, *debug_warrior_skin_index, true, false)) {
        std::cerr << "Debug overlay smoke could not enable the warrior linked-mesh preview.\n";
        return false;
    }

    const marrow::editor::DebugOverlaySettings baseline_debug_overlay =
        shell_state.viewport.debug_overlay;
    const DebugOverlayGeometry baseline_debug_geometry =
        build_debug_overlay_geometry(shell_state, *onion_layout);
    if (!require_debug_overlay_stats(
            baseline_debug_geometry.stats,
            1U,
            1U,
            1U,
            1U,
            1U,
            "Debug overlay smoke")) {
        return false;
    }

    std::vector<ViewportRenderVertex> debug_on_line_vertices;
    std::vector<ViewportRenderVertex> debug_on_triangle_vertices;
    build_viewport_render_geometry(
        shell_state,
        *onion_layout,
        {},
        std::nullopt,
        nullptr,
        &debug_on_line_vertices,
        &debug_on_triangle_vertices);

    shell_state.viewport.debug_overlay.bones = false;
    const DebugOverlayGeometry bones_hidden_geometry =
        build_debug_overlay_geometry(shell_state, *onion_layout);
    if (!require_debug_overlay_stats(
            bones_hidden_geometry.stats,
            1U,
            1U,
            1U,
            1U,
            1U,
            "Debug overlay bones-off smoke")) {
        return false;
    }
    std::vector<ViewportRenderVertex> debug_without_bones_line_vertices;
    std::vector<ViewportRenderVertex> debug_without_bones_triangle_vertices;
    build_viewport_render_geometry(
        shell_state,
        *onion_layout,
        {},
        std::nullopt,
        nullptr,
        &debug_without_bones_line_vertices,
        &debug_without_bones_triangle_vertices);
    if (debug_without_bones_line_vertices.size() >= debug_on_line_vertices.size() ||
        debug_without_bones_triangle_vertices.size() >= debug_on_triangle_vertices.size()) {
        std::cerr << "Debug overlay smoke expected hiding bones to remove viewport geometry.\n";
        return false;
    }
    shell_state.viewport.debug_overlay = baseline_debug_overlay;

    shell_state.viewport.debug_overlay.ik_constraints = false;
    if (!require_debug_overlay_stats(
            build_debug_overlay_geometry(shell_state, *onion_layout).stats,
            0U,
            1U,
            1U,
            1U,
            1U,
            "Debug overlay IK toggle smoke")) {
        return false;
    }
    shell_state.viewport.debug_overlay = baseline_debug_overlay;

    shell_state.viewport.debug_overlay.path_constraints = false;
    if (!require_debug_overlay_stats(
            build_debug_overlay_geometry(shell_state, *onion_layout).stats,
            1U,
            0U,
            1U,
            1U,
            1U,
            "Debug overlay path toggle smoke")) {
        return false;
    }
    shell_state.viewport.debug_overlay = baseline_debug_overlay;

    shell_state.viewport.debug_overlay.physics_constraints = false;
    if (!require_debug_overlay_stats(
            build_debug_overlay_geometry(shell_state, *onion_layout).stats,
            1U,
            1U,
            0U,
            1U,
            1U,
            "Debug overlay physics toggle smoke")) {
        return false;
    }
    shell_state.viewport.debug_overlay = baseline_debug_overlay;

    shell_state.viewport.debug_overlay.mesh_wireframes = false;
    if (!require_debug_overlay_stats(
            build_debug_overlay_geometry(shell_state, *onion_layout).stats,
            1U,
            1U,
            1U,
            0U,
            1U,
            "Debug overlay mesh toggle smoke")) {
        return false;
    }
    shell_state.viewport.debug_overlay = baseline_debug_overlay;

    shell_state.viewport.debug_overlay.bounding_boxes = false;
    if (!require_debug_overlay_stats(
            build_debug_overlay_geometry(shell_state, *onion_layout).stats,
            1U,
            1U,
            1U,
            1U,
            0U,
            "Debug overlay bounds toggle smoke")) {
        return false;
    }
    shell_state.viewport.debug_overlay = baseline_debug_overlay;

    shell_state.hud_overlay_enabled = true;
    shell_state.hud_overlay_frame = build_preview_profiler_frame(shell_state);
    if (!shell_state.hud_overlay_frame.has_value()) {
        std::cerr << "Performance HUD smoke could not build a preview profiler frame.\n";
        return false;
    }
    const marrow::runtime::ProfilerFrame hud_frame = *shell_state.hud_overlay_frame;
    if (hud_frame.skeleton_count != 1U ||
        hud_frame.draw_calls == 0U ||
        hud_frame.vertices == 0U ||
        hud_frame.total_us == 0U ||
        hud_frame.render_us == 0U) {
        std::cerr << "Performance HUD smoke did not report the expected skeleton, draw, vertex, and frame counters.\n";
        return false;
    }
    const std::vector<std::string> hud_lines =
        marrow::runtime::profiler_hud_lines(hud_frame);
    if (hud_lines.size() != 3U ||
        hud_lines[0].find("SKELS 1") == std::string::npos ||
        hud_lines[0].find("DRAWS ") == std::string::npos ||
        hud_lines[1].find("FRAME ") == std::string::npos ||
        hud_lines[2].find("BREAKS T") == std::string::npos) {
        std::cerr << "Performance HUD smoke did not emit the expected overlay text.\n";
        return false;
    }
    shell_state.hud_overlay_enabled = false;
    shell_state.hud_overlay_frame.reset();

    const auto draw_order_track = std::find_if(
        idle_tracks.begin(),
        idle_tracks.end(),
        [&](const TimelineTrackRow& track) { return track.id == "global:draw-order"; });
    if (draw_order_track == idle_tracks.end()) {
        std::cerr << "Timeline smoke validation could not find the global draw-order track.\n";
        return false;
    }

    const auto event_track = std::find_if(
        idle_tracks.begin(),
        idle_tracks.end(),
        [&](const TimelineTrackRow& track) { return track.id == "global:events"; });
    if (event_track == idle_tracks.end()) {
        std::cerr << "Timeline smoke validation could not find the global event track.\n";
        return false;
    }

    const std::string expected_spine_track_id = "bone:" + std::to_string(*spine_index) + ":Rotate";
    const auto spine_track = std::find_if(
        idle_tracks.begin(),
        idle_tracks.end(),
        [&](const TimelineTrackRow& track) { return track.id == expected_spine_track_id; });
    if (spine_track == idle_tracks.end()) {
        std::cerr << "Timeline smoke validation could not find the spine rotate track.\n";
        return false;
    }

    if (!focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
        std::cerr << "Timeline track focus did not scrub the idle clip.\n";
        return false;
    }
    if (selected_bone_index(shell_state) != spine_index) {
        std::cerr << "Timeline track focus did not synchronize bone selection.\n";
        return false;
    }
    if (shell_state.load_result.project->transform_timeline_edits.empty()) {
        std::cerr << "Project fixture did not load any transform timeline edits.\n";
        return false;
    }
    if (shell_state.load_result.project->mesh_deform_timeline_edits.empty()) {
        std::cerr << "Project fixture did not load any mesh deform timeline edits.\n";
        return false;
    }
    if (shell_state.load_result.project->draw_order_timeline_edits.empty()) {
        std::cerr << "Project fixture did not load any draw-order timeline edits.\n";
        return false;
    }
    if (shell_state.load_result.project->event_timeline_edits.empty()) {
        std::cerr << "Project fixture did not load any event timeline edits.\n";
        return false;
    }
    if (std::abs(
            shell_state.preview_skeleton->bone_poses()[*spine_index].local_pose.rotation - 8.0f) >
        1e-3f) {
        std::cerr << "Timeline track focus did not apply the project-authored spine rotation.\n";
        return false;
    }

    if (const auto body_slot_index = shell_state.load_result.skeleton_data->find_slot_index("body")) {
        const auto spark_fx_slot_index =
            shell_state.load_result.skeleton_data->find_slot_index("spark_fx");
        if (!spark_fx_slot_index.has_value() ||
            !focus_timeline_track(&shell_state, *draw_order_track, 0.5, "Smoke", false)) {
            std::cerr << "Timeline draw-order focus did not scrub the idle clip.\n";
            return false;
        }
        const auto body_position =
            draw_order_position(*shell_state.preview_skeleton, *body_slot_index);
        const auto spark_fx_position =
            draw_order_position(*shell_state.preview_skeleton, *spark_fx_slot_index);
        if (!body_position.has_value() || !spark_fx_position.has_value() ||
            *body_position != 0U || *spark_fx_position != 2U) {
            std::cerr << "Timeline draw-order focus did not apply the project-authored slot order.\n";
            return false;
        }
    }

    if (!focus_timeline_track(&shell_state, *event_track, 0.25, "Smoke", false) ||
        shell_state.preview_events.size() != 2U ||
        shell_state.preview_events[0].name != "footstep" ||
        shell_state.preview_events[0].int_value != 7 ||
        shell_state.preview_events[0].string_value != "editor_left" ||
        shell_state.preview_events[1].name != "dust_vfx" ||
        std::abs(shell_state.preview_events[1].float_value - 0.9) > 1e-3 ||
        shell_state.preview_events[1].string_value != "editor_dust") {
        std::cerr << "Timeline event focus did not expose the authored preview events.\n";
        return false;
    }
    if (!focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
        std::cerr << "Timeline smoke could not restore the spine rotate focus.\n";
        return false;
    }

    {
        marrow::editor::ProjectData previous_project = *shell_state.load_result.project;
        const auto edit_index = ensure_transform_timeline_edit_index(&shell_state, *spine_track);
        if (!edit_index.has_value()) {
            std::cerr << "Timeline editor smoke could not materialize the spine rotate edit.\n";
            return false;
        }

        auto& rotate_edit =
            shell_state.load_result.project->transform_timeline_edits[*edit_index];
        if (rotate_edit.keyframes.size() != 3) {
            std::cerr << "Timeline editor smoke expected the fixture rotate edit to start with 3 keys.\n";
            return false;
        }

        rotate_edit.keyframes[1].angle = 9.0;
        rotate_edit.keyframes[1].interpolation =
            marrow::runtime::Interpolation::linear();
        marrow::editor::TransformKeyframeEdit new_key;
        new_key.time = 0.75;
        new_key.angle = 12.0;
        new_key.interpolation = marrow::runtime::Interpolation::stepped();
        rotate_edit.keyframes.insert(rotate_edit.keyframes.begin() + 2, std::move(new_key));

        if (!rebuild_project_runtime(&shell_state)) {
            std::cerr << shell_state.error_message;
            return false;
        }
        shell_state.project_dirty = true;

        if (!focus_timeline_track(&shell_state, *spine_track, 0.625, "Smoke", false) ||
            std::abs(
                shell_state.preview_skeleton->bone_poses()[*spine_index].local_pose.rotation -
                10.5f) > 1e-3f) {
            std::cerr << "Timeline editor smoke did not apply edited linear interpolation.\n";
            return false;
        }
        if (!scrub_timeline_time(&shell_state, 0.875, "Smoke", false) ||
            std::abs(
                shell_state.preview_skeleton->bone_poses()[*spine_index].local_pose.rotation -
                12.0f) > 1e-3f) {
            std::cerr << "Timeline editor smoke did not apply the inserted stepped key.\n";
            return false;
        }

        marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
        temp_project.source_path = "/tmp/marrow_editor_shell_smoke.marrow";
        materialize_temp_project_runtime_assets(shell_state, &temp_project);

        const auto save_result =
            marrow::editor::save_project(temp_project, temp_project.source_path);
        if (!save_result) {
            std::cerr << save_result.error->format() << '\n';
            return false;
        }
        const auto export_result = marrow::editor::export_runtime_skeleton(
            *save_result.project,
            *shell_state.load_result.base_skeleton_document,
            "/tmp/marrow_editor_shell_smoke.mskl");
        if (!export_result) {
            std::cerr << export_result.error->format() << '\n';
            return false;
        }

        const auto exported_skeleton =
            marrow::runtime::load_skeleton_data(export_result.path);
        if (!exported_skeleton) {
            std::cerr << exported_skeleton.error->format();
            return false;
        }
        const auto* exported_idle = exported_skeleton.skeleton_data->find_animation("idle");
        const auto exported_spine_index =
            exported_skeleton.skeleton_data->find_bone_index("spine");
        if (exported_idle == nullptr || !exported_spine_index.has_value()) {
            std::cerr << "Exported shell smoke skeleton lost the idle spine rotate track.\n";
            return false;
        }
        const auto* exported_rotate =
            exported_idle->find_rotate_timeline(*exported_spine_index);
        if (exported_rotate == nullptr || exported_rotate->keyframes.size() != 4 ||
            exported_rotate->keyframes[0].interpolation.kind() !=
                marrow::runtime::InterpolationKind::CubicBezier ||
            exported_rotate->keyframes[1].interpolation.kind() !=
                marrow::runtime::InterpolationKind::Linear ||
            exported_rotate->keyframes[2].interpolation.kind() !=
                marrow::runtime::InterpolationKind::Stepped ||
            std::abs(exported_rotate->keyframes[1].angle - 9.0f) > 1e-3f ||
            std::abs(exported_rotate->keyframes[2].angle - 12.0f) > 1e-3f) {
            std::cerr << "Timeline editor smoke export did not round-trip the edited rotate curve.\n";
            return false;
        }

        *shell_state.load_result.project = std::move(previous_project);
        if (!rebuild_project_runtime(&shell_state)) {
            std::cerr << shell_state.error_message;
            return false;
        }
        shell_state.project_dirty = false;
        if (!focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
            std::cerr << "Timeline smoke failed to restore the original fixture edit.\n";
            return false;
        }
    }

    {
        marrow::editor::ProjectData previous_project = *shell_state.load_result.project;
        const auto edit_index = ensure_draw_order_timeline_edit_index(&shell_state, *draw_order_track);
        if (!edit_index.has_value()) {
            std::cerr << "Timeline editor smoke could not materialize the draw-order edit.\n";
            return false;
        }

        auto& draw_order_edit =
            shell_state.load_result.project->draw_order_timeline_edits[*edit_index];
        if (draw_order_edit.keyframes.size() != 3U) {
            std::cerr << "Timeline editor smoke expected the draw-order edit to start with 3 keys.\n";
            return false;
        }

        draw_order_edit.keyframes[1].slot_names = {
            "body", "spark_fx", "arm_l", "fx_mask", "spawn_anchor", "hurtbox", "guide"};
        marrow::editor::DrawOrderKeyframeEdit new_draw_order_key;
        new_draw_order_key.time = 0.75;
        new_draw_order_key.slot_names = {
            "spark_fx", "body", "arm_l", "fx_mask", "spawn_anchor", "hurtbox", "guide"};
        draw_order_edit.keyframes.insert(
            draw_order_edit.keyframes.begin() + 2,
            std::move(new_draw_order_key));

        if (!rebuild_project_runtime(&shell_state)) {
            std::cerr << shell_state.error_message;
            return false;
        }
        shell_state.project_dirty = true;

        const auto body_slot_index = shell_state.load_result.skeleton_data->find_slot_index("body");
        const auto spark_fx_slot_index =
            shell_state.load_result.skeleton_data->find_slot_index("spark_fx");
        if (!body_slot_index.has_value() || !spark_fx_slot_index.has_value()) {
            std::cerr << "Timeline draw-order smoke could not resolve the edited slot indices.\n";
            return false;
        }
        if (!focus_timeline_track(&shell_state, *draw_order_track, 0.5, "Smoke", false)) {
            std::cerr << "Timeline draw-order smoke could not refocus the edited draw-order track.\n";
            return false;
        }
        const auto edited_body_position =
            draw_order_position(*shell_state.preview_skeleton, *body_slot_index);
        const auto edited_spark_fx_position =
            draw_order_position(*shell_state.preview_skeleton, *spark_fx_slot_index);
        if (!edited_body_position.has_value() || !edited_spark_fx_position.has_value() ||
            *edited_body_position != 0U || *edited_spark_fx_position != 1U) {
            std::cerr << "Timeline editor smoke did not apply the edited draw-order key.\n";
            return false;
        }
        if (!scrub_timeline_time(&shell_state, 0.875, "Smoke", false)) {
            std::cerr << "Timeline draw-order smoke could not scrub to the inserted key.\n";
            return false;
        }
        const auto inserted_body_position =
            draw_order_position(*shell_state.preview_skeleton, *body_slot_index);
        const auto inserted_spark_fx_position =
            draw_order_position(*shell_state.preview_skeleton, *spark_fx_slot_index);
        if (!inserted_body_position.has_value() || !inserted_spark_fx_position.has_value() ||
            *inserted_body_position != 1U || *inserted_spark_fx_position != 0U) {
            std::cerr << "Timeline editor smoke did not apply the inserted draw-order key.\n";
            return false;
        }

        marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
        temp_project.source_path = "/tmp/marrow_editor_shell_draw_order_smoke.marrow";
        materialize_temp_project_runtime_assets(shell_state, &temp_project);

        const auto save_result =
            marrow::editor::save_project(temp_project, temp_project.source_path);
        if (!save_result) {
            std::cerr << save_result.error->format() << '\n';
            return false;
        }
        const auto export_result = marrow::editor::export_runtime_skeleton(
            *save_result.project,
            *shell_state.load_result.base_skeleton_document,
            "/tmp/marrow_editor_shell_draw_order_smoke.mskl");
        if (!export_result) {
            std::cerr << export_result.error->format() << '\n';
            return false;
        }

        const auto exported_skeleton =
            marrow::runtime::load_skeleton_data(export_result.path);
        if (!exported_skeleton) {
            std::cerr << exported_skeleton.error->format();
            return false;
        }
        const auto* exported_idle = exported_skeleton.skeleton_data->find_animation("idle");
        const auto* exported_draw_order =
            exported_idle != nullptr ? exported_idle->find_draw_order_timeline() : nullptr;
        if (exported_draw_order == nullptr || exported_draw_order->keyframes.size() != 4U ||
            exported_draw_order->keyframes[1].slot_indices.size() < 3U ||
            exported_skeleton.skeleton_data->slots()[exported_draw_order->keyframes[1].slot_indices[0]].name !=
                "body" ||
            exported_skeleton.skeleton_data->slots()[exported_draw_order->keyframes[1].slot_indices[1]].name !=
                "spark_fx" ||
            exported_skeleton.skeleton_data->slots()[exported_draw_order->keyframes[2].slot_indices[0]].name !=
                "spark_fx") {
            std::cerr << "Timeline editor smoke export did not round-trip the edited draw-order keys.\n";
            return false;
        }

        *shell_state.load_result.project = std::move(previous_project);
        if (!rebuild_project_runtime(&shell_state) ||
            !focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
            std::cerr << "Timeline smoke failed to restore the original draw-order fixture edit.\n";
            return false;
        }
        shell_state.project_dirty = false;
    }

    {
        marrow::editor::ProjectData previous_project = *shell_state.load_result.project;
        const auto edit_index = ensure_event_timeline_edit_index(&shell_state, *event_track);
        if (!edit_index.has_value()) {
            std::cerr << "Timeline editor smoke could not materialize the event edit.\n";
            return false;
        }

        auto& event_edit =
            shell_state.load_result.project->event_timeline_edits[*edit_index];
        if (event_edit.keyframes.size() != 3U) {
            std::cerr << "Timeline editor smoke expected the event edit to start with 3 keys.\n";
            return false;
        }

        event_edit.keyframes[0].int_value = 11;
        marrow::editor::EventKeyframeEdit new_event_key;
        new_event_key.time = 0.9;
        new_event_key.event_name = "dust_vfx";
        new_event_key.float_value = 0.55;
        new_event_key.string_value = "editor_trail";
        event_edit.keyframes.push_back(std::move(new_event_key));

        if (!rebuild_project_runtime(&shell_state)) {
            std::cerr << shell_state.error_message;
            return false;
        }
        shell_state.project_dirty = true;

        if (!focus_timeline_track(&shell_state, *event_track, 0.9, "Smoke", false) ||
            shell_state.preview_events.size() != 4U ||
            shell_state.preview_events.front().int_value != 11 ||
            shell_state.preview_events.back().name != "dust_vfx" ||
            std::abs(shell_state.preview_events.back().float_value - 0.55) > 1e-3 ||
            shell_state.preview_events.back().string_value != "editor_trail") {
            std::cerr << "Timeline editor smoke did not apply the edited event payloads.\n";
            return false;
        }

        marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
        temp_project.source_path = "/tmp/marrow_editor_shell_event_smoke.marrow";
        materialize_temp_project_runtime_assets(shell_state, &temp_project);

        const auto save_result =
            marrow::editor::save_project(temp_project, temp_project.source_path);
        if (!save_result) {
            std::cerr << save_result.error->format() << '\n';
            return false;
        }
        const auto export_result = marrow::editor::export_runtime_skeleton(
            *save_result.project,
            *shell_state.load_result.base_skeleton_document,
            "/tmp/marrow_editor_shell_event_smoke.mskl");
        if (!export_result) {
            std::cerr << export_result.error->format() << '\n';
            return false;
        }

        const auto exported_skeleton =
            marrow::runtime::load_skeleton_data(export_result.path);
        if (!exported_skeleton) {
            std::cerr << exported_skeleton.error->format();
            return false;
        }
        const auto* exported_idle = exported_skeleton.skeleton_data->find_animation("idle");
        const auto* exported_events =
            exported_idle != nullptr ? exported_idle->find_event_timeline() : nullptr;
        if (exported_events == nullptr || exported_events->keyframes.size() != 4U ||
            exported_events->keyframes.front().int_value != std::optional<int>(11) ||
            exported_events->keyframes.back().event_index >=
                exported_skeleton.skeleton_data->events().size() ||
            exported_skeleton.skeleton_data
                    ->events()[exported_events->keyframes.back().event_index]
                    .name != "dust_vfx" ||
            exported_events->keyframes.back().string_value !=
                std::optional<std::string>("editor_trail")) {
            std::cerr << "Timeline editor smoke export did not round-trip the edited event keys.\n";
            return false;
        }

        *shell_state.load_result.project = std::move(previous_project);
        if (!rebuild_project_runtime(&shell_state) ||
            !focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
            std::cerr << "Timeline smoke failed to restore the original event fixture edit.\n";
            return false;
        }
        shell_state.project_dirty = false;
    }

    if (const auto body_slot_index = shell_state.load_result.skeleton_data->find_slot_index("body")) {
        const std::string deform_track_id =
            "slot:" + std::to_string(*body_slot_index) + ":deform:body_mesh";
        const auto deform_track = std::find_if(
            idle_tracks.begin(),
            idle_tracks.end(),
            [&](const TimelineTrackRow& track) { return track.id == deform_track_id; });
        if (deform_track == idle_tracks.end()) {
            std::cerr << "Timeline smoke validation could not find the body mesh deform track.\n";
            return false;
        }
        if (!focus_timeline_track(&shell_state, *deform_track, 0.5, "Smoke", false) ||
            selected_slot_index(shell_state) != body_slot_index) {
            std::cerr << "Timeline deform track focus did not synchronize slot selection.\n";
            return false;
        }
        const std::vector<double>* fixture_offsets =
            shell_state.preview_skeleton->current_mesh_vertex_offsets(*body_slot_index);
        if (fixture_offsets == nullptr || fixture_offsets->size() != 8U ||
            std::abs((*fixture_offsets)[2] - 12.0) > 1e-3 ||
            std::abs((*fixture_offsets)[3] + 8.0) > 1e-3 ||
            std::abs((*fixture_offsets)[4] - 16.0) > 1e-3 ||
            std::abs((*fixture_offsets)[5] - 20.0) > 1e-3) {
            std::cerr << "Timeline deform track focus did not apply the project-authored FFD key.\n";
            return false;
        }

        {
            marrow::editor::ProjectData previous_project = *shell_state.load_result.project;
            const auto edit_index = ensure_mesh_deform_timeline_edit_index(&shell_state, *deform_track);
            if (!edit_index.has_value()) {
                std::cerr << "Timeline editor smoke could not materialize the deform edit.\n";
                return false;
            }

            auto& deform_edit =
                shell_state.load_result.project->mesh_deform_timeline_edits[*edit_index];
            if (deform_edit.keyframes.size() != 3U) {
                std::cerr << "Timeline editor smoke expected the deform edit to start with 3 keys.\n";
                return false;
            }

            deform_edit.keyframes[1].vertex_offsets = {0.0, 0.0, 14.0, -10.0, 18.0, 24.0, -6.0, 12.0};
            marrow::editor::DeformKeyframeEdit new_deform_key;
            new_deform_key.time = 0.75;
            new_deform_key.vertex_offsets = {0.0, 0.0, 8.0, -6.0, 10.0, 14.0, -3.0, 7.0};
            new_deform_key.interpolation = marrow::runtime::Interpolation::stepped();
            deform_edit.keyframes.insert(
                deform_edit.keyframes.begin() + 2,
                std::move(new_deform_key));

            if (!rebuild_project_runtime(&shell_state)) {
                std::cerr << shell_state.error_message;
                return false;
            }
            shell_state.project_dirty = true;

            if (!focus_timeline_track(&shell_state, *deform_track, 0.5, "Smoke", false)) {
                std::cerr << "Timeline deform smoke could not refocus the edited deform track.\n";
                return false;
            }
            const std::vector<double>* edited_mid_offsets =
                shell_state.preview_skeleton->current_mesh_vertex_offsets(*body_slot_index);
            if (edited_mid_offsets == nullptr || edited_mid_offsets->size() != 8U ||
                std::abs((*edited_mid_offsets)[2] - 14.0) > 1e-3 ||
                std::abs((*edited_mid_offsets)[3] + 10.0) > 1e-3 ||
                std::abs((*edited_mid_offsets)[4] - 18.0) > 1e-3 ||
                std::abs((*edited_mid_offsets)[5] - 24.0) > 1e-3) {
                std::cerr << "Timeline editor smoke did not apply edited deform offsets.\n";
                return false;
            }
            if (!scrub_timeline_time(&shell_state, 0.875, "Smoke", false)) {
                std::cerr << "Timeline deform smoke could not scrub to the inserted key.\n";
                return false;
            }
            const std::vector<double>* inserted_offsets =
                shell_state.preview_skeleton->current_mesh_vertex_offsets(*body_slot_index);
            if (inserted_offsets == nullptr || inserted_offsets->size() != 8U ||
                std::abs((*inserted_offsets)[2] - 8.0) > 1e-3 ||
                std::abs((*inserted_offsets)[3] + 6.0) > 1e-3 ||
                std::abs((*inserted_offsets)[4] - 10.0) > 1e-3 ||
                std::abs((*inserted_offsets)[5] - 14.0) > 1e-3) {
                std::cerr << "Timeline editor smoke did not apply the inserted deform key.\n";
                return false;
            }

            marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
            temp_project.source_path = "/tmp/marrow_editor_shell_deform_smoke.marrow";
            materialize_temp_project_runtime_assets(shell_state, &temp_project);

            const auto save_result =
                marrow::editor::save_project(temp_project, temp_project.source_path);
            if (!save_result) {
                std::cerr << save_result.error->format() << '\n';
                return false;
            }
            const auto export_result = marrow::editor::export_runtime_skeleton(
                *save_result.project,
                *shell_state.load_result.base_skeleton_document,
                "/tmp/marrow_editor_shell_deform_smoke.mskl");
            if (!export_result) {
                std::cerr << export_result.error->format() << '\n';
                return false;
            }

            const auto exported_skeleton =
                marrow::runtime::load_skeleton_data(export_result.path);
            if (!exported_skeleton) {
                std::cerr << exported_skeleton.error->format();
                return false;
            }

            const auto exported_body_slot_index =
                exported_skeleton.skeleton_data->find_slot_index("body");
            const auto* exported_idle =
                exported_skeleton.skeleton_data->find_animation("idle");
            if (!exported_body_slot_index.has_value() || exported_idle == nullptr) {
                std::cerr << "Exported shell smoke skeleton lost the idle deform track.\n";
                return false;
            }
            const auto* exported_deform =
                exported_idle->find_deform_timeline(*exported_body_slot_index, "body_mesh");
            if (exported_deform == nullptr || exported_deform->keyframes.size() != 4U ||
                std::abs(exported_deform->keyframes[1].vertex_offsets[2] - 14.0f) > 1e-3f ||
                std::abs(exported_deform->keyframes[2].vertex_offsets[2] - 8.0f) > 1e-3f ||
                exported_deform->keyframes[2].interpolation.kind() !=
                    marrow::runtime::InterpolationKind::Stepped) {
                std::cerr << "Timeline editor smoke export did not round-trip the edited deform keys.\n";
                return false;
            }

            marrow::runtime::Skeleton exported_preview(exported_skeleton.skeleton_data);
            if (!exported_preview.set_skin("warrior")) {
                std::cerr << "Exported deform smoke could not activate the warrior skin.\n";
                return false;
            }
            exported_preview.apply_animation(*exported_idle, 0.875);
            const std::vector<double>* exported_offsets =
                exported_preview.current_mesh_vertex_offsets(*exported_body_slot_index);
            if (exported_offsets == nullptr || exported_offsets->size() != 8U ||
                std::abs((*exported_offsets)[2] - 8.0) > 1e-3 ||
                std::abs((*exported_offsets)[5] - 14.0) > 1e-3) {
                std::cerr << "Runtime playback did not preserve the exported deform offsets.\n";
                return false;
            }

            *shell_state.load_result.project = std::move(previous_project);
            if (!rebuild_project_runtime(&shell_state) ||
                !focus_timeline_track(&shell_state, *spine_track, 0.5, "Smoke", false)) {
                std::cerr << "Timeline smoke failed to restore the original deform fixture edit.\n";
                return false;
            }
            shell_state.project_dirty = false;
        }

        const auto* animated_attachment =
            shell_state.preview_skeleton->current_attachment(*body_slot_index);
        if (animated_attachment == nullptr || animated_attachment->name != "warrior_body") {
            std::cerr << "Timeline scrub did not synchronize the animated body attachment.\n";
            return false;
        }
    }

    if (const auto body_slot_index = shell_state.load_result.skeleton_data->find_slot_index("body")) {
        select_slot(&shell_state, *body_slot_index, "Smoke", false);
        if (!selected_slot_index(shell_state).has_value() ||
            *selected_slot_index(shell_state) != *body_slot_index) {
            std::cerr << "Slot selection did not update the inspector slot state.\n";
            return false;
        }
        if (!selected_attachment(shell_state).has_value() ||
            selected_attachment(shell_state)->attachment_name != "warrior_body") {
            std::cerr << "Slot selection did not resolve the animated body attachment at t=0.5.\n";
            return false;
        }

        const auto selected_attachment_reference = resolve_attachment_reference(
            *shell_state.load_result.skeleton_data,
            *selected_attachment(shell_state));
        if (!selected_attachment_reference.has_value()) {
            std::cerr << "Slot selection did not resolve the selected body attachment reference.\n";
            return false;
        }
        const std::vector<MeshWeightVertexRow> weight_rows = build_mesh_weight_rows(
            *shell_state.load_result.skeleton_data,
            *selected_attachment_reference->attachment);
        if (weight_rows.size() != 4U ||
            weight_rows[1].influences.size() != 2U ||
            weight_rows[1].influences[0].bone_name != "spine" ||
            std::abs(weight_rows[1].influences[0].weight - 0.75) > 1e-3 ||
            weight_rows[1].influences[1].bone_name != "arm_l" ||
            std::abs(weight_rows[1].influences[1].weight - 0.25) > 1e-3) {
            std::cerr << "Mesh weight inspector data did not expose the expected weighted influences.\n";
            return false;
        }

        const auto warrior_skin_index =
            shell_state.load_result.skeleton_data->find_skin_index("warrior");
        if (!warrior_skin_index.has_value() ||
            !set_preview_skin_enabled(&shell_state, *warrior_skin_index, true, false)) {
            std::cerr << "Failed to enable the warrior preview skin in smoke validation.\n";
            if (!shell_state.error_message.empty()) {
                std::cerr << shell_state.error_message << '\n';
            }
            return false;
        }

        const auto* warrior_attachment =
            shell_state.preview_skeleton->current_attachment(*body_slot_index);
        if (warrior_attachment == nullptr || warrior_attachment->name != "warrior_body" ||
            !warrior_attachment->linked_mesh.has_value()) {
            std::cerr << "Skin preview did not activate the warrior linked mesh attachment.\n";
            return false;
        }

        const auto mage_skin_index =
            shell_state.load_result.skeleton_data->find_skin_index("mage");
        if (!mage_skin_index.has_value()) {
            std::cerr << "Mage skin is missing from the sample project.\n";
            return false;
        }

        const PreviewAttachmentSelection mage_attachment_selection{
            *body_slot_index,
            *mage_skin_index,
            "mage_body"};
        const auto mage_attachment_reference = resolve_attachment_reference(
            *shell_state.load_result.skeleton_data,
            mage_attachment_selection);
        if (!mage_attachment_reference.has_value() ||
            !mage_attachment_reference->attachment->linked_mesh.has_value()) {
            std::cerr << "Attachment inspector smoke could not resolve the mage linked mesh.\n";
            return false;
        }

        select_attachment(&shell_state, mage_attachment_selection, "Smoke", false);
        if (!apply_attachment_selection_to_preview_slot(
                &shell_state,
                mage_attachment_selection,
                "Smoke",
                false)) {
            std::cerr << "Attachment preview override failed for mage_body.\n";
            return false;
        }

        const auto* mage_attachment =
            shell_state.preview_skeleton->current_attachment(*body_slot_index);
        if (mage_attachment == nullptr || mage_attachment->name != "mage_body" ||
            !mage_attachment->linked_mesh.has_value() ||
            mage_attachment->linked_mesh->parent_attachment != "body_mesh") {
            std::cerr << "Attachment override did not expose the linked mesh relationship.\n";
            return false;
        }

        if (!reset_preview_slot_to_skin_selection(&shell_state, *body_slot_index, "Smoke", false)) {
            std::cerr << "Failed to reset the body slot to the active skin preview.\n";
            return false;
        }

        const auto* restored_attachment =
            shell_state.preview_skeleton->current_attachment(*body_slot_index);
        if (restored_attachment == nullptr || restored_attachment->name != "warrior_body") {
            std::cerr << "Resetting the slot preview did not restore the warrior skin state.\n";
            return false;
        }

        const auto paint_bone_index =
            shell_state.load_result.skeleton_data->find_bone_index("spine");
        if (!paint_bone_index.has_value()) {
            std::cerr << "Weight paint smoke could not resolve the body slot's owning bone.\n";
            return false;
        }
        if (!set_selected_animation(&shell_state, "attack", "Smoke", false, true) ||
            !scrub_timeline_time(&shell_state, 0.2, "Smoke", false)) {
            std::cerr << "Weight paint smoke could not scrub the attack preview pose.\n";
            return false;
        }
        select_slot(&shell_state, *body_slot_index, "Smoke", false);
        if (selected_bone_index(shell_state) != paint_bone_index) {
            std::cerr << "Slot selection did not resolve its owning bone for weight paint.\n";
            return false;
        }
        shell_state.weight_paint.enabled = true;
        shell_state.weight_paint.show_heatmap = true;
        shell_state.weight_paint.radius_pixels = 20.0f;
        shell_state.weight_paint.strength = 1.0f;
        shell_state.weight_paint.mode = WeightPaintMode::Paint;

        const auto require_weight_near =
            [](auto actual, double expected, double epsilon, std::string_view label) {
                const double actual_value = static_cast<double>(actual);
                if (std::abs(actual_value - expected) <= epsilon) {
                    return true;
                }

                std::cerr << label << " expected " << expected << " but was "
                          << actual_value << '\n';
                return false;
            };
        const auto build_weight_overlay = [&]() -> std::optional<MeshWeightOverlay> {
            const auto layout = build_viewport_layout(
                shell_state,
                ImVec2(0.0f, 0.0f),
                ImVec2(1280.0f, 720.0f));
            if (!layout.has_value()) {
                return std::nullopt;
            }
            return build_mesh_weight_overlay(shell_state, *layout);
        };
        const auto current_weight_target = [&]() -> std::optional<MeshWeightPaintTarget> {
            return current_mesh_weight_paint_target(shell_state);
        };
        const auto current_paint_weight = [&](std::size_t vertex_index) -> std::optional<double> {
            const std::optional<MeshWeightPaintTarget> target = current_weight_target();
            if (!target.has_value() ||
                target->source_attachment == nullptr ||
                target->source_attachment->mesh_geometry == nullptr ||
                vertex_index >= target->source_attachment->mesh_geometry->weights.size()) {
                return std::nullopt;
            }

            return weight_for_bone(
                target->source_attachment->mesh_geometry->weights[vertex_index],
                *paint_bone_index);
        };
        const auto current_vertex_weight_total = [&](std::size_t vertex_index) -> std::optional<double> {
            const std::optional<MeshWeightPaintTarget> target = current_weight_target();
            if (!target.has_value() ||
                target->source_attachment == nullptr ||
                target->source_attachment->mesh_geometry == nullptr ||
                vertex_index >= target->source_attachment->mesh_geometry->weights.size()) {
                return std::nullopt;
            }

            double total = 0.0;
            for (const auto& influence :
                 target->source_attachment->mesh_geometry->weights[vertex_index].influences) {
                total += influence.weight;
            }
            return total;
        };
        const auto current_body_pose = [&]() -> std::optional<marrow::runtime::MeshAttachmentPose> {
            return shell_state.preview_skeleton->evaluate_current_mesh_attachment(*body_slot_index);
        };

        const EditorHistorySnapshot weight_paint_baseline =
            capture_history_snapshot(shell_state);
        shell_state.session.clear_history();
        shell_state.pending_edit_action.reset();
        update_project_dirty_state(&shell_state);

        const std::optional<MeshWeightOverlay> baseline_overlay = build_weight_overlay();
        const std::optional<marrow::runtime::MeshAttachmentPose> baseline_pose =
            current_body_pose();
        if (!baseline_overlay.has_value() ||
            !baseline_pose.has_value() ||
            baseline_overlay->target.source_skin_name != "mesh_base" ||
            baseline_overlay->target.source_attachment_name != "body_mesh" ||
            baseline_overlay->target.display_attachment_name != "warrior_body" ||
            baseline_overlay->vertices.size() != 4U ||
            !require_weight_near(baseline_overlay->vertices[0].weight, 1.0, 1e-6, "baseline vertex0 active-bone weight") ||
            !require_weight_near(baseline_overlay->vertices[1].weight, 0.75, 1e-6, "baseline vertex1 active-bone weight") ||
            !require_weight_near(baseline_overlay->vertices[2].weight, 0.25, 1e-6, "baseline vertex2 active-bone weight")) {
            std::cerr << "Weight paint smoke did not resolve the expected linked-mesh paint target.\n";
            return false;
        }

        const ImVec4 heat_blue = mesh_weight_heatmap_color(0.0, 1.0f);
        const ImVec4 heat_green = mesh_weight_heatmap_color(1.0 / 3.0, 1.0f);
        const ImVec4 heat_yellow = mesh_weight_heatmap_color(2.0 / 3.0, 1.0f);
        const ImVec4 heat_red = mesh_weight_heatmap_color(1.0, 1.0f);
        if (!(heat_blue.z > heat_blue.y && heat_blue.z > heat_blue.x) ||
            !(heat_green.y > heat_green.x && heat_green.y > heat_green.z) ||
            !(heat_yellow.x > 0.8f && heat_yellow.y > 0.7f && heat_yellow.z < 0.4f) ||
            !(heat_red.x > heat_red.y && heat_red.x > heat_red.z)) {
            std::cerr << "Weight paint smoke did not expose the expected blue/green/yellow/red heat-map ramp.\n";
            return false;
        }

        begin_weight_paint_stroke(&shell_state, baseline_overlay->target);
        if (!apply_weight_paint_sample(
                &shell_state,
                *baseline_overlay,
                baseline_overlay->vertices[1].screen_position) ||
            !finish_weight_paint_stroke(&shell_state)) {
            std::cerr << "Weight paint smoke could not apply a paint stroke.\n";
            return false;
        }

        const std::optional<MeshWeightOverlay> painted_overlay = build_weight_overlay();
        const std::optional<marrow::runtime::MeshAttachmentPose> painted_pose =
            current_body_pose();
        const std::optional<double> painted_active_weight = current_paint_weight(1U);
        const std::optional<double> painted_total_weight = current_vertex_weight_total(1U);
        if (shell_state.session.undo_count() != 1U ||
            shell_state.session.redo_count() != 0U ||
            shell_state.load_result.project->mesh_weight_attachment_edits.size() != 1U ||
            !painted_overlay.has_value() ||
            !painted_pose.has_value() ||
            !painted_active_weight.has_value() ||
            !painted_total_weight.has_value() ||
            !require_weight_near(*painted_active_weight, 0.875, 1e-6, "painted vertex1 active-bone weight") ||
            !require_weight_near(*painted_total_weight, 1.0, 1e-6, "painted vertex1 total weight") ||
            !require_weight_near(painted_overlay->vertices[1].weight, 0.875, 1e-6, "painted overlay vertex1 active-bone weight") ||
            !(std::abs(painted_pose->vertices[1].x - baseline_pose->vertices[1].x) > 1e-3 ||
              std::abs(painted_pose->vertices[1].y - baseline_pose->vertices[1].y) > 1e-3)) {
            std::cerr << "Weight paint smoke did not apply the painted weight, normalization, or live preview deformation.\n";
            return false;
        }

        if (!undo_project_change(&shell_state)) {
            std::cerr << "Weight paint smoke could not undo the paint stroke.\n";
            return false;
        }
        const std::optional<double> undone_paint_weight = current_paint_weight(1U);
        const std::optional<marrow::runtime::MeshAttachmentPose> undone_paint_pose =
            current_body_pose();
        if (shell_state.session.undo_count() != 0U ||
            shell_state.session.redo_count() != 1U ||
            !undone_paint_weight.has_value() ||
            !undone_paint_pose.has_value() ||
            !require_weight_near(*undone_paint_weight, 0.75, 1e-6, "undone painted vertex1 active-bone weight") ||
            !require_weight_near(undone_paint_pose->vertices[1].x, baseline_pose->vertices[1].x, 1e-6, "undone painted vertex1 x") ||
            !require_weight_near(undone_paint_pose->vertices[1].y, baseline_pose->vertices[1].y, 1e-6, "undone painted vertex1 y")) {
            std::cerr << "Weight paint smoke undo did not restore the baseline mesh state.\n";
            return false;
        }

        if (!redo_project_change(&shell_state)) {
            std::cerr << "Weight paint smoke could not redo the paint stroke.\n";
            return false;
        }
        const std::optional<double> redone_paint_weight = current_paint_weight(1U);
        const std::optional<marrow::runtime::MeshAttachmentPose> redone_paint_pose =
            current_body_pose();
        if (shell_state.session.undo_count() != 1U ||
            shell_state.session.redo_count() != 0U ||
            !redone_paint_weight.has_value() ||
            !redone_paint_pose.has_value() ||
            !require_weight_near(*redone_paint_weight, 0.875, 1e-6, "redone painted vertex1 active-bone weight") ||
            !require_weight_near(redone_paint_pose->vertices[1].x, painted_pose->vertices[1].x, 1e-6, "redone painted vertex1 x") ||
            !require_weight_near(redone_paint_pose->vertices[1].y, painted_pose->vertices[1].y, 1e-6, "redone painted vertex1 y")) {
            std::cerr << "Weight paint smoke redo did not restore the painted mesh state.\n";
            return false;
        }

        shell_state.weight_paint.mode = WeightPaintMode::Erase;
        const std::optional<MeshWeightOverlay> erase_overlay = build_weight_overlay();
        if (!erase_overlay.has_value()) {
            std::cerr << "Weight paint smoke could not build the erase overlay.\n";
            return false;
        }
        begin_weight_paint_stroke(&shell_state, erase_overlay->target);
        if (!apply_weight_paint_sample(
                &shell_state,
                *erase_overlay,
                erase_overlay->vertices[1].screen_position) ||
            !finish_weight_paint_stroke(&shell_state)) {
            std::cerr << "Weight paint smoke could not apply an erase stroke.\n";
            return false;
        }

        const std::optional<double> erased_active_weight = current_paint_weight(1U);
        const std::optional<double> erased_total_weight = current_vertex_weight_total(1U);
        if (shell_state.session.undo_count() != 2U ||
            shell_state.session.redo_count() != 0U ||
            !erased_active_weight.has_value() ||
            !erased_total_weight.has_value() ||
            !require_weight_near(*erased_active_weight, 0.0, 1e-6, "erased vertex1 active-bone weight") ||
            !require_weight_near(*erased_total_weight, 1.0, 1e-6, "erased vertex1 total weight")) {
            std::cerr << "Weight paint smoke did not erase the active bone influence.\n";
            return false;
        }

        if (!undo_project_change(&shell_state)) {
            std::cerr << "Weight paint smoke could not undo the erase stroke.\n";
            return false;
        }
        const std::optional<double> undone_erase_weight = current_paint_weight(1U);
        if (!undone_erase_weight.has_value() ||
            !require_weight_near(*undone_erase_weight, 0.875, 1e-6, "undone erased vertex1 active-bone weight")) {
            std::cerr << "Weight paint smoke undo did not restore the painted influence.\n";
            return false;
        }

        if (!redo_project_change(&shell_state)) {
            std::cerr << "Weight paint smoke could not redo the erase stroke.\n";
            return false;
        }
        const std::optional<double> redone_erase_weight = current_paint_weight(1U);
        if (!redone_erase_weight.has_value() ||
            !require_weight_near(*redone_erase_weight, 0.0, 1e-6, "redone erased vertex1 active-bone weight")) {
            std::cerr << "Weight paint smoke redo did not restore the erased influence.\n";
            return false;
        }

        if (!apply_history_snapshot(&shell_state, weight_paint_baseline) ||
            !apply_current_animation_state_to_preview(&shell_state)) {
            std::cerr << "Weight paint smoke could not restore the baseline mesh state.\n";
            return false;
        }
        shell_state.session.clear_history();
        shell_state.pending_edit_action.reset();
        update_project_dirty_state(&shell_state);

        shell_state.weight_paint.mode = WeightPaintMode::Smooth;
        const std::optional<MeshWeightOverlay> smooth_overlay = build_weight_overlay();
        if (!smooth_overlay.has_value()) {
            std::cerr << "Weight paint smoke could not build the smooth overlay.\n";
            return false;
        }
        begin_weight_paint_stroke(&shell_state, smooth_overlay->target);
        if (!apply_weight_paint_sample(
                &shell_state,
                *smooth_overlay,
                smooth_overlay->vertices[0].screen_position) ||
            !finish_weight_paint_stroke(&shell_state)) {
            std::cerr << "Weight paint smoke could not apply a smooth stroke.\n";
            return false;
        }

        const std::optional<double> smoothed_active_weight = current_paint_weight(0U);
        const std::optional<double> smoothed_total_weight = current_vertex_weight_total(0U);
        if (shell_state.session.undo_count() != 1U ||
            shell_state.session.redo_count() != 0U ||
            !smoothed_active_weight.has_value() ||
            !smoothed_total_weight.has_value() ||
            !require_weight_near(*smoothed_active_weight, 0.6875, 1e-6, "smoothed vertex0 active-bone weight") ||
            !require_weight_near(*smoothed_total_weight, 1.0, 1e-6, "smoothed vertex0 total weight")) {
            std::cerr << "Weight paint smoke did not smooth the neighboring influences.\n";
            return false;
        }

        marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
        temp_project.source_path = "/tmp/marrow_editor_shell_weight_paint_smoke.marrow";
        materialize_temp_project_runtime_assets(shell_state, &temp_project);
        const auto save_result =
            marrow::editor::save_project(temp_project, temp_project.source_path);
        if (!save_result) {
            std::cerr << save_result.error->format() << '\n';
            return false;
        }
        const auto export_result = marrow::editor::export_runtime_skeleton(
            *save_result.project,
            *shell_state.load_result.base_skeleton_document,
            "/tmp/marrow_editor_shell_weight_paint_smoke.mskl");
        if (!export_result) {
            std::cerr << export_result.error->format() << '\n';
            return false;
        }

        const auto exported_skeleton =
            marrow::runtime::load_skeleton_data(export_result.path);
        if (!exported_skeleton) {
            std::cerr << exported_skeleton.error->format();
            return false;
        }
        const auto exported_body_slot_index =
            exported_skeleton.skeleton_data->find_slot_index("body");
        const auto exported_paint_bone_index =
            exported_skeleton.skeleton_data->find_bone_index("spine");
        const auto* exported_attachment =
            exported_body_slot_index.has_value()
                ? exported_skeleton.skeleton_data->find_attachment(
                      "mesh_base",
                      *exported_body_slot_index,
                      "body_mesh")
                : nullptr;
        if (!exported_body_slot_index.has_value() ||
            !exported_paint_bone_index.has_value() ||
            exported_attachment == nullptr ||
            exported_attachment->mesh_geometry == nullptr ||
            exported_attachment->mesh_geometry->weights.size() <= 0U ||
            !require_weight_near(
                weight_for_bone(
                    exported_attachment->mesh_geometry->weights[0],
                    *exported_paint_bone_index),
                0.6875,
                1e-6,
                "exported smoothed vertex0 active-bone weight")) {
            std::cerr << "Weight paint smoke export did not round-trip the authored mesh weights.\n";
            return false;
        }

        if (!undo_project_change(&shell_state) ||
            shell_state.session.undo_count() != 0U ||
            shell_state.session.redo_count() != 1U) {
            std::cerr << "Weight paint smoke could not undo the smooth stroke.\n";
            return false;
        }
        const std::optional<double> undone_smooth_weight = current_paint_weight(0U);
        if (!undone_smooth_weight.has_value() ||
            !require_weight_near(*undone_smooth_weight, 1.0, 1e-6, "undone smoothed vertex0 active-bone weight")) {
            std::cerr << "Weight paint smoke undo did not restore the unsmoothed mesh weights.\n";
            return false;
        }

        if (!redo_project_change(&shell_state) ||
            shell_state.session.undo_count() != 1U ||
            shell_state.session.redo_count() != 0U) {
            std::cerr << "Weight paint smoke could not redo the smooth stroke.\n";
            return false;
        }
        const std::optional<double> redone_smooth_weight = current_paint_weight(0U);
        if (!redone_smooth_weight.has_value() ||
            !require_weight_near(*redone_smooth_weight, 0.6875, 1e-6, "redone smoothed vertex0 active-bone weight")) {
            std::cerr << "Weight paint smoke redo did not restore the smoothed mesh weights.\n";
            return false;
        }

        if (!apply_history_snapshot(&shell_state, weight_paint_baseline) ||
            !apply_current_animation_state_to_preview(&shell_state)) {
            std::cerr << "Weight paint smoke could not restore the baseline state after export validation.\n";
            return false;
        }
        reset_weight_paint_stroke(&shell_state);
        shell_state.weight_paint.enabled = false;
        shell_state.session.clear_history();
        shell_state.pending_edit_action.reset();
        update_project_dirty_state(&shell_state);
    }

    if (!set_selected_animation(&shell_state, "attack", "Smoke", false, true)) {
        std::cerr << "State preview smoke could not select the attack animation.\n";
        return false;
    }
    shell_state.preview_queue_enabled = true;
    shell_state.preview_queued_animation_name = "aim";
    shell_state.preview_queue_delay = 0.0;
    shell_state.preview_use_custom_mix_duration = true;
    shell_state.preview_custom_mix_duration = 0.1;
    shell_state.preview_reverse = false;
    if (!scrub_timeline_time(&shell_state, 0.45, "Smoke", false)) {
        std::cerr << "State preview smoke could not scrub the queued attack->aim transition.\n";
        return false;
    }
    if (std::abs(timeline_preview_duration(shell_state) - 0.9) > 1e-6) {
        std::cerr << "State preview smoke did not compute the queued preview duration.\n";
        return false;
    }
    if (const auto arm_index = shell_state.load_result.skeleton_data->find_bone_index("arm_l")) {
        const double mixed_rotation =
            static_cast<double>(
                shell_state.preview_skeleton->bone_poses()[*arm_index].local_pose.rotation);
        if (std::abs(mixed_rotation - 45.0) > 1e-3) {
            std::cerr << "State preview smoke did not apply the queued mix pose.\n";
            return false;
        }
    }

    shell_state.preview_queue_enabled = false;
    shell_state.preview_use_custom_mix_duration = false;
    shell_state.preview_reverse = true;
    if (!set_selected_animation(&shell_state, "idle", "Smoke", false, true) ||
        !scrub_timeline_time(&shell_state, 0.35, "Smoke", false)) {
        std::cerr << "State preview smoke could not scrub reverse idle playback.\n";
        return false;
    }
    if (std::abs(shell_state.preview_root_motion_total.x + 14.0) > 1e-3 ||
        std::abs(shell_state.preview_root_motion_total.y - 7.0) > 1e-3) {
        std::cerr << "State preview smoke did not surface reverse root-motion playback.\n";
        return false;
    }
    shell_state.preview_reverse = false;

    const auto require_smoke_near =
        [](auto actual, auto expected, double epsilon, std::string_view label) {
            const double actual_value = static_cast<double>(actual);
            const double expected_value = static_cast<double>(expected);
            if (std::abs(actual_value - expected_value) <= epsilon) {
                return true;
            }

            std::cerr << label << " expected " << expected_value << " but was " << actual_value
                      << ".\n";
            return false;
        };
    constexpr double kConstraintPreviewPositionEpsilon = 5e-2;

    shell_state.preview_skin_names = {"default"};
    shell_state.preview_slot_overrides.assign(
        shell_state.preview_slot_overrides.size(), std::nullopt);
    if (!set_selected_animation(&shell_state, "idle", "Smoke", false, true)) {
        std::cerr << "Constraint smoke could not restore the idle preview state.\n";
        return false;
    }

    const auto ik_tip_index = shell_state.load_result.skeleton_data->find_bone_index("ik_tip");
    const auto ik_target_index = shell_state.load_result.skeleton_data->find_bone_index("ik_target");
    const auto path_a_index = shell_state.load_result.skeleton_data->find_bone_index("path_a");
    const auto path_b_index = shell_state.load_result.skeleton_data->find_bone_index("path_b");
    const auto path_c_index = shell_state.load_result.skeleton_data->find_bone_index("path_c");
    const auto transform_source_index =
        shell_state.load_result.skeleton_data->find_bone_index("transform_source");
    const auto transform_target_index =
        shell_state.load_result.skeleton_data->find_bone_index("transform_target");
    const auto pivot_index = shell_state.load_result.skeleton_data->find_bone_index("pivot");
    const auto ribbon_tip_index =
        shell_state.load_result.skeleton_data->find_bone_index("ribbon_tip");
    if (!ik_tip_index.has_value() || !ik_target_index.has_value() || !path_a_index.has_value() ||
        !path_b_index.has_value() || !path_c_index.has_value() ||
        !transform_source_index.has_value() || !transform_target_index.has_value() ||
        !pivot_index.has_value() || !ribbon_tip_index.has_value()) {
        std::cerr << "Constraint smoke fixture lost the helper bones needed for authoring validation.\n";
        return false;
    }

    const auto* authored_ik = find_named_constraint(
        shell_state.load_result.skeleton_data->ik_constraints(),
        "editor_arm_reach");
    const auto* authored_path = find_named_constraint(
        shell_state.load_result.skeleton_data->path_constraints(),
        "editor_guide_follow");
    const auto* authored_transform = find_named_constraint(
        shell_state.load_result.skeleton_data->transform_constraints(),
        "editor_transform_follow");
    const auto* authored_physics = find_named_constraint(
        shell_state.load_result.skeleton_data->physics_constraints(),
        "editor_ribbon_secondary");
    if (shell_state.load_result.project->ik_constraint_edits.size() != 1U ||
        shell_state.load_result.project->path_constraint_edits.size() != 1U ||
        shell_state.load_result.project->transform_constraint_edits.size() != 1U ||
        shell_state.load_result.project->physics_constraint_edits.size() != 1U ||
        authored_ik == nullptr || authored_path == nullptr ||
        authored_transform == nullptr || authored_physics == nullptr) {
        std::cerr << "Constraint smoke fixture did not expose the expected authored constraint edits.\n";
        return false;
    }

    select_constraint(
        &shell_state,
        ConstraintKind::Transform,
        "editor_transform_follow",
        "Smoke",
        false);
    if (!selected_constraint(shell_state).has_value() ||
        selected_constraint(shell_state)->kind != ConstraintKind::Transform ||
        selected_constraint(shell_state)->constraint_name != "editor_transform_follow") {
        std::cerr << "Constraint selection smoke did not preserve the chosen transform constraint.\n";
        return false;
    }

    const auto& ik_tip_world =
        shell_state.preview_skeleton->bone_world_transforms()[*ik_tip_index];
    const auto& ik_target_world =
        shell_state.preview_skeleton->bone_world_transforms()[*ik_target_index];
    const double setup_ik_tip_x = -40.0;
    const double setup_ik_tip_y = 140.0;
    const double setup_ik_distance = std::hypot(
        setup_ik_tip_x - static_cast<double>(ik_target_world.world_x),
        setup_ik_tip_y - static_cast<double>(ik_target_world.world_y));
    const double preview_ik_distance = std::hypot(
        static_cast<double>(ik_tip_world.world_x - ik_target_world.world_x),
        static_cast<double>(ik_tip_world.world_y - ik_target_world.world_y));
    if (preview_ik_distance >= setup_ik_distance * 0.5) {
        std::cerr << "Constraint preview did not apply the authored partial IK reach.\n";
        return false;
    }

    const auto& path_a_world =
        shell_state.preview_skeleton->bone_world_transforms()[*path_a_index];
    const auto& path_b_world =
        shell_state.preview_skeleton->bone_world_transforms()[*path_b_index];
    const auto& path_c_world =
        shell_state.preview_skeleton->bone_world_transforms()[*path_c_index];
    if (!require_smoke_near(
            path_a_world.world_x,
            20.0,
            kConstraintPreviewPositionEpsilon,
            "constraint preview path_a x") ||
        !require_smoke_near(
            path_a_world.world_y,
            0.0,
            kConstraintPreviewPositionEpsilon,
            "constraint preview path_a y") ||
        !require_smoke_near(
            path_b_world.world_x,
            80.0,
            kConstraintPreviewPositionEpsilon,
            "constraint preview path_b x") ||
        !require_smoke_near(
            path_b_world.world_y,
            0.0,
            kConstraintPreviewPositionEpsilon,
            "constraint preview path_b y") ||
        !require_smoke_near(
            path_c_world.world_x,
            100.0,
            kConstraintPreviewPositionEpsilon,
            "constraint preview path_c x") ||
        !require_smoke_near(
            path_c_world.world_y,
            40.0,
            kConstraintPreviewPositionEpsilon,
            "constraint preview path_c y")) {
        std::cerr << "Constraint preview did not place the path chain on the authored guide.\n";
        return false;
    }

    const double expected_transform_rotation = 12.5;
    const double expected_transform_x = 215.0;
    const double expected_transform_y = 7.5;
    const double expected_transform_scale_x = 1.5;
    const double expected_transform_scale_y = 0.7;
    const double expected_transform_shear_x = 9.75;
    const double expected_transform_shear_y = -4.5;
    const double kPi = 3.14159265358979323846;
    const double transform_x_radians =
        (expected_transform_rotation + expected_transform_shear_x) * kPi / 180.0;
    const double transform_y_radians =
        (expected_transform_rotation + 90.0 + expected_transform_shear_y) * kPi / 180.0;
    const auto& transform_target_world =
        shell_state.preview_skeleton->bone_world_transforms()[*transform_target_index];
    if (!require_smoke_near(
            transform_target_world.world_x,
            expected_transform_x,
            kConstraintPreviewPositionEpsilon,
            "constraint preview transform target x") ||
        !require_smoke_near(
            transform_target_world.world_y,
            expected_transform_y,
            kConstraintPreviewPositionEpsilon,
            "constraint preview transform target y") ||
        !require_smoke_near(
            transform_target_world.a,
            std::cos(transform_x_radians) * expected_transform_scale_x,
            1e-3,
            "constraint preview transform target axis a") ||
        !require_smoke_near(
            transform_target_world.b,
            std::cos(transform_y_radians) * expected_transform_scale_y,
            1e-3,
            "constraint preview transform target axis b") ||
        !require_smoke_near(
            transform_target_world.c,
            std::sin(transform_x_radians) * expected_transform_scale_x,
            1e-3,
            "constraint preview transform target axis c") ||
        !require_smoke_near(
            transform_target_world.d,
            std::sin(transform_y_radians) * expected_transform_scale_y,
            1e-3,
            "constraint preview transform target axis d")) {
        std::cerr << "Constraint preview did not apply the authored transform constraint.\n";
        return false;
    }

    const auto& setup_ribbon_tip =
        shell_state.preview_skeleton->bone_world_transforms()[*ribbon_tip_index];
    if (!require_smoke_near(
            setup_ribbon_tip.world_x,
            230.0,
            5.0,
            "constraint preview setup ribbon tip x") ||
        !require_smoke_near(
            setup_ribbon_tip.world_y,
            -120.0,
            5.0,
            "constraint preview setup ribbon tip y")) {
        std::cerr << "Constraint preview lost the helper ribbon setup pose.\n";
        return false;
    }

    {
        marrow::editor::ProjectData previous_project = *shell_state.load_result.project;
        const auto ik_edit_index =
            ensure_ik_constraint_edit_index(&shell_state, "editor_arm_reach");
        const auto path_edit_index =
            ensure_path_constraint_edit_index(&shell_state, "editor_guide_follow");
        const auto transform_edit_index =
            ensure_transform_constraint_edit_index(&shell_state, "editor_transform_follow");
        const auto physics_edit_index =
            ensure_physics_constraint_edit_index(&shell_state, "editor_ribbon_secondary");
        if (!ik_edit_index.has_value() || !path_edit_index.has_value() ||
            !transform_edit_index.has_value() || !physics_edit_index.has_value()) {
            std::cerr << "Constraint editor smoke could not materialize the authored constraint edits.\n";
            return false;
        }

        shell_state.load_result.project->path_constraint_edits[*path_edit_index].position = 0.0;
        shell_state.load_result.project->transform_constraint_edits[*transform_edit_index]
            .translate_mix = 0.5;
        shell_state.load_result.project->physics_constraint_edits[*physics_edit_index].wind.x =
            18.0;

        marrow::editor::IkConstraintEdit created_ik;
        created_ik.name =
            unique_constraint_name(shell_state, ConstraintKind::Ik, "editor_created_ik");
        created_ik.bone_names = {"transform_target"};
        created_ik.target_bone_name = "transform_source";
        created_ik.mix = 0.0;
        created_ik.bend_positive = true;
        shell_state.load_result.project->ik_constraint_edits.push_back(created_ik);
        const std::string created_ik_name = created_ik.name;

        select_constraint(
            &shell_state,
            ConstraintKind::Ik,
            created_ik_name,
            "Smoke",
            false);

        if (!rebuild_project_runtime(&shell_state)) {
            std::cerr << shell_state.error_message;
            return false;
        }
        shell_state.project_dirty = true;

        const auto* created_runtime_ik = find_named_constraint(
            shell_state.load_result.skeleton_data->ik_constraints(),
            created_ik_name);
        const auto* edited_runtime_path = find_named_constraint(
            shell_state.load_result.skeleton_data->path_constraints(),
            "editor_guide_follow");
        const auto* edited_runtime_transform = find_named_constraint(
            shell_state.load_result.skeleton_data->transform_constraints(),
            "editor_transform_follow");
        const auto* edited_runtime_physics = find_named_constraint(
            shell_state.load_result.skeleton_data->physics_constraints(),
            "editor_ribbon_secondary");
        if (created_runtime_ik == nullptr || edited_runtime_path == nullptr ||
            edited_runtime_transform == nullptr || edited_runtime_physics == nullptr ||
            !require_smoke_near(
                edited_runtime_path->position,
                0.0,
                1e-6,
                "edited runtime path position") ||
            !require_smoke_near(
                edited_runtime_transform->translate_mix,
                0.5,
                1e-6,
                "edited runtime transform translate mix") ||
            !require_smoke_near(
                edited_runtime_physics->wind.x,
                18.0,
                1e-6,
                "edited runtime physics wind.x") ||
            !require_smoke_near(created_runtime_ik->mix, 0.0, 1e-6, "created runtime IK mix")) {
            std::cerr << "Constraint editor smoke did not rebuild the edited runtime constraint data.\n";
            return false;
        }

        if (!set_selected_animation(&shell_state, "idle", "Smoke", false, true)) {
            std::cerr << "Constraint editor smoke could not refresh the idle preview after edits.\n";
            return false;
        }

        const auto& edited_path_a_world =
            shell_state.preview_skeleton->bone_world_transforms()[*path_a_index];
        const auto& edited_path_b_world =
            shell_state.preview_skeleton->bone_world_transforms()[*path_b_index];
        const auto& edited_path_c_world =
            shell_state.preview_skeleton->bone_world_transforms()[*path_c_index];
        if (!require_smoke_near(
                edited_path_a_world.world_x,
                0.0,
                kConstraintPreviewPositionEpsilon,
                "edited constraint preview path_a x") ||
            !require_smoke_near(
                edited_path_a_world.world_y,
                0.0,
                kConstraintPreviewPositionEpsilon,
                "edited constraint preview path_a y") ||
            !require_smoke_near(
                edited_path_b_world.world_x,
                60.0,
                kConstraintPreviewPositionEpsilon,
                "edited constraint preview path_b x") ||
            !require_smoke_near(
                edited_path_b_world.world_y,
                0.0,
                kConstraintPreviewPositionEpsilon,
                "edited constraint preview path_b y") ||
            !require_smoke_near(
                edited_path_c_world.world_x,
                100.0,
                kConstraintPreviewPositionEpsilon,
                "edited constraint preview path_c x") ||
            !require_smoke_near(
                edited_path_c_world.world_y,
                20.0,
                kConstraintPreviewPositionEpsilon,
                "edited constraint preview path_c y")) {
            std::cerr << "Constraint editor smoke did not re-preview the edited path constraint.\n";
            return false;
        }

        const auto& edited_transform_target_world =
            shell_state.preview_skeleton->bone_world_transforms()[*transform_target_index];
        if (!require_smoke_near(
                edited_transform_target_world.world_x,
                200.0,
                kConstraintPreviewPositionEpsilon,
                "edited constraint preview transform target x") ||
            !require_smoke_near(
                edited_transform_target_world.world_y,
                25.0,
                kConstraintPreviewPositionEpsilon,
                "edited constraint preview transform target y")) {
            std::cerr << "Constraint editor smoke did not re-preview the edited transform constraint.\n";
            return false;
        }

        shell_state.preview_skeleton->bone_poses()[*pivot_index].local_pose.rotation = 90.0;
        shell_state.preview_skeleton->update_world_transforms();
        const auto lagged_ribbon_tip =
            shell_state.preview_skeleton->bone_world_transforms()[*ribbon_tip_index];
        shell_state.preview_skeleton->update_physics(1.0 / 60.0);
        const auto stepped_ribbon_tip =
            shell_state.preview_skeleton->bone_world_transforms()[*ribbon_tip_index];
        const double preview_physics_motion = std::hypot(
            static_cast<double>(stepped_ribbon_tip.world_x - lagged_ribbon_tip.world_x),
            static_cast<double>(stepped_ribbon_tip.world_y - lagged_ribbon_tip.world_y));
        if (preview_physics_motion <= 0.1) {
            std::cerr << "Constraint editor smoke did not advance the preview physics chain.\n";
            return false;
        }

        marrow::editor::ProjectData temp_project = *shell_state.load_result.project;
        temp_project.source_path = "/tmp/marrow_editor_shell_constraints_smoke.marrow";
        materialize_temp_project_runtime_assets(shell_state, &temp_project);

        const auto save_result =
            marrow::editor::save_project(temp_project, temp_project.source_path);
        if (!save_result) {
            std::cerr << save_result.error->format() << '\n';
            return false;
        }
        const auto export_result = marrow::editor::export_runtime_skeleton(
            *save_result.project,
            *shell_state.load_result.base_skeleton_document,
            "/tmp/marrow_editor_shell_constraints_smoke.mskl");
        if (!export_result) {
            std::cerr << export_result.error->format() << '\n';
            return false;
        }

        const auto exported_skeleton =
            marrow::runtime::load_skeleton_data(export_result.path);
        if (!exported_skeleton) {
            std::cerr << exported_skeleton.error->format();
            return false;
        }

        const auto* exported_created_ik = find_named_constraint(
            exported_skeleton.skeleton_data->ik_constraints(),
            created_ik_name);
        const auto* exported_path = find_named_constraint(
            exported_skeleton.skeleton_data->path_constraints(),
            "editor_guide_follow");
        const auto* exported_transform = find_named_constraint(
            exported_skeleton.skeleton_data->transform_constraints(),
            "editor_transform_follow");
        const auto* exported_physics = find_named_constraint(
            exported_skeleton.skeleton_data->physics_constraints(),
            "editor_ribbon_secondary");
        if (exported_created_ik == nullptr || exported_path == nullptr ||
            exported_transform == nullptr || exported_physics == nullptr ||
            exported_created_ik->bone_indices !=
                std::vector<std::size_t>{*transform_target_index} ||
            exported_created_ik->target_bone_index != *transform_source_index ||
            !require_smoke_near(
                exported_path->position,
                0.0,
                1e-6,
                "exported path position") ||
            !require_smoke_near(
                exported_transform->translate_mix,
                0.5,
                1e-6,
                "exported transform translate mix") ||
            !require_smoke_near(
                exported_physics->wind.x,
                18.0,
                1e-6,
                "exported physics wind.x")) {
            std::cerr << "Constraint editor smoke export did not round-trip the edited constraints.\n";
            return false;
        }

        *shell_state.load_result.project = std::move(previous_project);
        if (!rebuild_project_runtime(&shell_state) ||
            !set_selected_animation(&shell_state, "idle", "Smoke", false, true)) {
            std::cerr << "Constraint editor smoke failed to restore the original project state.\n";
            return false;
        }
        shell_state.project_dirty = false;
    }

    {
        if (const auto body_slot_index = shell_state.load_result.skeleton_data->find_slot_index("body")) {
            const auto warrior_skin_index =
                shell_state.load_result.skeleton_data->find_skin_index("warrior");
            const auto mage_skin_index =
                shell_state.load_result.skeleton_data->find_skin_index("mage");
            const auto transform_edit_index =
                ensure_transform_timeline_edit_index(&shell_state, *spine_track);
            const auto transform_constraint_edit_index =
                ensure_transform_constraint_edit_index(&shell_state, "editor_transform_follow");
            if (!warrior_skin_index.has_value() || !mage_skin_index.has_value() ||
                !transform_edit_index.has_value() ||
                !transform_constraint_edit_index.has_value()) {
                std::cerr << "Undo smoke could not resolve the baseline editor state.\n";
                return false;
            }

            shell_state.session.clear_history();
            shell_state.pending_edit_action.reset();
            shell_state.preview_skin_names = {"default"};
            shell_state.preview_slot_overrides.assign(
                shell_state.preview_slot_overrides.size(),
                std::nullopt);
            if (!apply_preview_skin_selection(&shell_state, "Smoke", false)) {
                std::cerr << "Undo smoke could not restore the default preview skin selection.\n";
                return false;
            }
            select_slot(&shell_state, *body_slot_index, "Smoke", false);
            update_project_dirty_state(&shell_state);

            const EditorHistorySnapshot undo_smoke_baseline =
                capture_history_snapshot(shell_state);
            const auto undo_track_group = [&]() {
                return std::string("timeline:") + spine_track->id;
            };
            const auto undo_key_group = [&](std::size_t key_index) {
                return undo_track_group() + ":key:" + std::to_string(key_index);
            };

            auto& rotate_edit =
                shell_state.load_result.project->transform_timeline_edits[*transform_edit_index];
            const std::size_t baseline_key_count = rotate_edit.keyframes.size();
            if (const auto insert_time = insertable_key_time(
                    rotate_edit.keyframes,
                    0.625,
                    selected_animation_duration(shell_state))) {
                marrow::editor::ProjectData previous_project =
                    *shell_state.load_result.project;
                marrow::editor::TransformKeyframeEdit inserted_key =
                    sample_transform_keyframe(shell_state, *spine_track);
                inserted_key.time = *insert_time;
                inserted_key.angle = 11.0;
                inserted_key.interpolation = marrow::runtime::Interpolation::linear();
                const auto insert_iterator = std::upper_bound(
                    rotate_edit.keyframes.begin(),
                    rotate_edit.keyframes.end(),
                    inserted_key.time,
                    [](double time, const marrow::editor::TransformKeyframeEdit& keyframe) {
                        return time < keyframe.time;
                    });
                const std::size_t inserted_index = static_cast<std::size_t>(
                    std::distance(rotate_edit.keyframes.begin(), insert_iterator));
                rotate_edit.keyframes.insert(insert_iterator, inserted_key);
                const std::uint64_t runtime_revision_before_command =
                    shell_state.session.runtime_revision();
                if (!apply_project_command_change(
                        &shell_state,
                        previous_project,
                        EditActionKind::AddKeyframe,
                        "Added smoke undo key on spine",
                        undo_track_group(),
                        false,
                        "Undo smoke add-key action failed")) {
                    std::cerr << "Undo smoke could not record the add-key action.\n";
                    return false;
                }
                if (shell_state.session.runtime_revision() !=
                    runtime_revision_before_command + 1U) {
                    std::cerr << "Shell command edit rebuilt the session runtime more than once.\n";
                    return false;
                }

                previous_project = *shell_state.load_result.project;
                shell_state.load_result.project->transform_timeline_edits[*transform_edit_index]
                    .keyframes[inserted_index]
                    .angle = 18.0;
                if (!apply_project_command_change(
                        &shell_state,
                        previous_project,
                        EditActionKind::MoveBone,
                        "Moved smoke undo key on spine",
                        undo_key_group(inserted_index),
                        false,
                        "Undo smoke move action failed")) {
                    std::cerr << "Undo smoke could not record the move-key action.\n";
                    return false;
                }

                previous_project = *shell_state.load_result.project;
                const double baseline_translate_mix =
                    shell_state.load_result.project
                        ->transform_constraint_edits[*transform_constraint_edit_index]
                        .translate_mix;
                shell_state.load_result.project
                    ->transform_constraint_edits[*transform_constraint_edit_index]
                    .translate_mix = 0.5;
                if (!apply_project_command_change(
                        &shell_state,
                        previous_project,
                        EditActionKind::EditProperty,
                        "Edited smoke constraint translate mix",
                        "constraint:Transform:editor_transform_follow",
                        true,
                        "Undo smoke constraint action failed")) {
                    std::cerr << "Undo smoke could not record the constraint edit action.\n";
                    return false;
                }

                if (!set_preview_skin_enabled(
                        &shell_state,
                        *warrior_skin_index,
                        true,
                        false,
                        true)) {
                    std::cerr << "Undo smoke could not record the preview skin action.\n";
                    return false;
                }

                const PreviewAttachmentSelection mage_attachment_selection{
                    *body_slot_index,
                    *mage_skin_index,
                    "mage_body"};
                if (!apply_attachment_selection_to_preview_slot(
                        &shell_state,
                        mage_attachment_selection,
                        "Smoke",
                        false,
                        true)) {
                    std::cerr << "Undo smoke could not record the preview attachment swap.\n";
                    return false;
                }

                previous_project = *shell_state.load_result.project;
                shell_state.load_result.project->transform_timeline_edits[*transform_edit_index]
                    .keyframes.erase(
                        shell_state.load_result.project->transform_timeline_edits[*transform_edit_index]
                            .keyframes.begin() +
                        static_cast<std::ptrdiff_t>(inserted_index));
                if (!apply_project_command_change(
                        &shell_state,
                        previous_project,
                        EditActionKind::RemoveKeyframe,
                        "Removed smoke undo key on spine",
                        undo_track_group(),
                        false,
                        "Undo smoke remove-key action failed")) {
                    std::cerr << "Undo smoke could not record the remove-key action.\n";
                    return false;
                }

                const EditorHistorySnapshot undo_smoke_final =
                    capture_history_snapshot(shell_state);
                const auto* edited_constraint = find_named_constraint(
                    shell_state.load_result.skeleton_data->transform_constraints(),
                    "editor_transform_follow");
                const auto* overridden_attachment =
                    shell_state.preview_skeleton->current_attachment(*body_slot_index);
                const bool warrior_enabled = std::find(
                    shell_state.preview_skin_names.begin(),
                    shell_state.preview_skin_names.end(),
                    "warrior") != shell_state.preview_skin_names.end();
                if (shell_state.session.undo_count() != 6U ||
                    shell_state.session.redo_count() != 0U ||
                    shell_state.load_result.project->transform_timeline_edits[*transform_edit_index]
                            .keyframes.size() != baseline_key_count ||
                    edited_constraint == nullptr ||
                    !require_smoke_near(
                        edited_constraint->translate_mix,
                        0.5,
                        1e-6,
                        "undo smoke constraint translate mix") ||
                    !warrior_enabled ||
                    !shell_state.preview_slot_overrides[*body_slot_index].has_value() ||
                    shell_state.preview_slot_overrides[*body_slot_index]->attachment_name !=
                        "mage_body" ||
                    overridden_attachment == nullptr ||
                    overridden_attachment->name != "mage_body") {
                    std::cerr << "Undo smoke did not preserve the final edited state before undo.\n";
                    return false;
                }

                for (int undo_index = 0; undo_index < 6; ++undo_index) {
                    if (!undo_project_change(&shell_state)) {
                        std::cerr << "Undo smoke failed while rewinding the editor history.\n";
                        return false;
                    }
                }
                if (shell_state.session.undo_count() != 0U ||
                    shell_state.session.redo_count() != 6U ||
                    !history_snapshots_equal(
                        capture_history_snapshot(shell_state),
                        undo_smoke_baseline) ||
                    shell_state.project_dirty) {
                    std::cerr << "Undo smoke did not restore the baseline project and preview state.\n";
                    return false;
                }

                for (int redo_index = 0; redo_index < 6; ++redo_index) {
                    if (!redo_project_change(&shell_state)) {
                        std::cerr << "Undo smoke failed while replaying the editor history.\n";
                        return false;
                    }
                }
                if (shell_state.session.undo_count() != 6U ||
                    shell_state.session.redo_count() != 0U ||
                    !history_snapshots_equal(
                        capture_history_snapshot(shell_state),
                        undo_smoke_final)) {
                    std::cerr << "Undo smoke did not reapply the recorded action sequence.\n";
                    return false;
                }

                if (!undo_project_change(&shell_state) ||
                    shell_state.session.redo_count() != 1U) {
                    std::cerr << "Undo smoke could not prepare the redo-clear validation.\n";
                    return false;
                }

                previous_project = *shell_state.load_result.project;
                shell_state.load_result.project
                    ->transform_constraint_edits[*transform_constraint_edit_index]
                    .translate_mix = baseline_translate_mix;
                if (!apply_project_command_change(
                        &shell_state,
                        previous_project,
                        EditActionKind::EditProperty,
                        "Branched smoke constraint translate mix",
                        "constraint:Transform:editor_transform_follow",
                        true,
                        "Undo smoke redo-clear action failed")) {
                    std::cerr << "Undo smoke could not validate redo clearing after a new edit.\n";
                    return false;
                }
                if (shell_state.session.redo_count() != 0U) {
                    std::cerr << "Undo smoke did not clear redo history after a new edit.\n";
                    return false;
                }
            } else {
                std::cerr << "Undo smoke could not find room for an inserted spine key.\n";
                return false;
            }

            if (!apply_history_snapshot(&shell_state, undo_smoke_baseline) ||
                !apply_current_animation_state_to_preview(&shell_state)) {
                std::cerr << "Undo smoke failed to restore the baseline shell state.\n";
                return false;
            }
            shell_state.session.clear_history();
            shell_state.pending_edit_action.reset();
            update_project_dirty_state(&shell_state);
            if (shell_state.project_dirty) {
                std::cerr << "Undo smoke left the project marked dirty after baseline restore.\n";
                return false;
            }
        } else {
            std::cerr << "Undo smoke could not resolve the body slot for preview attachment validation.\n";
            return false;
        }
    }
    return true;
}

} // namespace marrow::editor::shell
