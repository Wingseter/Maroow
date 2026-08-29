#include "shell_timeline_graph.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <string>

#include "shell_derived_cache.hpp"
#include "shell_selection.hpp"
#include "timeline_controller.hpp"
#include "marrow/editor/agent_dispatch.hpp"
#include "marrow/editor/project.hpp"

namespace marrow::editor::shell {
namespace {

using GraphComponent = timeline_graph_model::Component;
using GraphProjectionStatus = timeline_graph_model::ProjectionStatus;

bool finite_view(const timeline_graph_model::View& view) {
    return std::isfinite(view.view_start_seconds) &&
        std::isfinite(view.pixels_per_second) && view.pixels_per_second > 0.0 &&
        std::isfinite(view.value_center) &&
        std::isfinite(view.pixels_per_value) && view.pixels_per_value > 0.0;
}

bool same_view(
    const timeline_graph_model::View& left,
    const timeline_graph_model::View& right) {
    return left.view_start_seconds == right.view_start_seconds &&
        left.pixels_per_second == right.pixels_per_second &&
        left.value_center == right.value_center &&
        left.pixels_per_value == right.pixels_per_value;
}

const TimelineTrackRow* require_track(
    const std::vector<TimelineTrackRow>& tracks,
    std::string_view id) {
    const TimelineTrackRow* track = find_timeline_track(tracks, id);
    if (track == nullptr) {
        std::cerr << "Graph shell smoke is missing track " << id << ".\n";
    }
    return track;
}

bool fit_graph_view(
    ShellState* state,
    const timeline_graph_model::Projection& projection,
    timeline_graph_model::PlotRect rect) {
    if (state == nullptr || !projection.track.has_value()) return false;
    const auto fitted = timeline_graph_model::fit_view(
        *projection.track,
        state->timeline_editor.graph_view.component_visible,
        rect,
        state->timeline_editor.frames_per_second);
    if (!fitted.has_value()) return false;
    state->timeline_editor.graph_view.view = *fitted;
    state->timeline_editor.graph_view.fitted_track_id = projection.track->track_id;
    state->timeline_editor.graph_view.fitted_animation_name =
        state->selected_animation_name;
    state->timeline_editor.graph_view.needs_fit = false;
    return true;
}

} // namespace

bool validate_timeline_graph_shell_smoke(
    const std::filesystem::path& project_path) {
    ShellState state;
    state.project_path = project_path;
    if (!reload_project(&state) ||
        !set_selected_animation(&state, "idle", "Graph smoke", false, true)) {
        std::cerr << "Graph shell smoke could not load player_idle/idle.\n";
        return false;
    }

    const auto& tracks = cached_timeline_tracks(&state);
    const TimelineTrackRow* translate = require_track(tracks, "bone:1:Translate");
    const TimelineTrackRow* rotate = require_track(tracks, "bone:1:Rotate");
    const TimelineTrackRow* attachment = require_track(tracks, "slot:0:Attachment");
    if (translate == nullptr || rotate == nullptr || attachment == nullptr) {
        return false;
    }

    state.selection.replace(marrow::editor::BoneSelection{"spine"});
    state.selected_timeline_track_id = translate->id;
    if (resolve_timeline_graph_track(state, tracks) != translate) {
        std::cerr << "Graph focus did not win over active-selection fallback.\n";
        return false;
    }

    state.selected_timeline_track_id = "missing-focused-row";
    const auto focus_before_fallback = state.selected_timeline_track_id;
    if (resolve_timeline_graph_track(state, tracks) != rotate ||
        state.selected_timeline_track_id != focus_before_fallback) {
        std::cerr << "Graph fallback did not use the first supported active Bone row.\n";
        return false;
    }

    state.selected_timeline_track_id = translate->id;
    const auto& translate_projection = cached_timeline_graph_projection(&state, *translate);
    if (translate_projection.status != GraphProjectionStatus::Ready ||
        !translate_projection.track.has_value() ||
        translate_projection.track->components.size() != 2U) {
        std::cerr << "Graph shell smoke could not project Translate X/Y.\n";
        return false;
    }
    const std::uint64_t translate_generation = state.timeline_editor.graph_cache.generation;

    state.selected_timeline_track_id = attachment->id;
    if (resolve_timeline_graph_track(state, tracks) != attachment) {
        std::cerr << "An unsupported focused graph row did not remain authoritative.\n";
        return false;
    }
    const auto& unsupported = cached_timeline_graph_projection(&state, *attachment);
    if (unsupported.status != GraphProjectionStatus::UnsupportedTrack ||
        unsupported.track.has_value() ||
        state.timeline_editor.graph_cache.generation != translate_generation + 1U) {
        std::cerr << "Unsupported graph focus reused the previous supported projection.\n";
        return false;
    }

    state.selected_timeline_track_id = translate->id;
    const auto& projection = cached_timeline_graph_projection(&state, *translate);
    if (projection.status != GraphProjectionStatus::Ready ||
        !projection.track.has_value() || projection.track->keys.empty()) {
        std::cerr << "Graph Translate projection disappeared after unsupported focus.\n";
        return false;
    }

    const std::string project_before =
        marrow::editor::serialize_project(*state.session.project());
    const std::size_t undo_before = state.session.undo_count();
    const std::size_t redo_before = state.session.redo_count();
    const std::uint64_t project_revision_before = state.session.project_revision();
    const std::uint64_t runtime_revision_before = state.session.runtime_revision();
    const std::size_t operation_count_before =
        marrow::editor::agent_operation_descriptor_count();
    const bool dirty_before = state.session.dirty();
    const bool shell_dirty_before = state.project_dirty;
    if (operation_count_before != 56U) {
        std::cerr << "Graph shell smoke requires the unchanged 56-operation registry.\n";
        return false;
    }

    const auto& parent_key = projection.track->keys.front();
    const timeline_graph_model::PointHit point{
        parent_key.identity, GraphComponent::X, 0U};
    if (!activate_timeline_graph_point(
            &state, *translate, point, false, "Graph smoke") ||
        state.timeline_editor.selected_keys !=
            std::vector<TimelineKeyRef>{parent_key.identity} ||
        !(state.timeline_editor.active_key ==
          std::optional<TimelineKeyRef>(parent_key.identity)) ||
        state.timeline_editor.graph_view.active_component != GraphComponent::X ||
        state.selected_timeline_track_id != translate->id ||
        state.timeline_time_seconds != parent_key.time_seconds) {
        std::cerr << "Graph point activation did not synchronize its parent key.\n";
        return false;
    }
    if (!activate_timeline_graph_point(
            &state, *translate, point, true, "Graph smoke") ||
        !state.timeline_editor.selected_keys.empty() ||
        state.timeline_editor.active_key.has_value() ||
        state.timeline_editor.graph_view.active_component != GraphComponent::X) {
        std::cerr << "Additive graph activation did not share dopesheet toggle semantics.\n";
        return false;
    }

    constexpr timeline_graph_model::PlotRect plot{0.0, 0.0, 640.0, 320.0};
    const double dopesheet_view_start_before =
        state.timeline_editor.view_start_seconds;
    const double dopesheet_pixels_per_second_before =
        state.timeline_editor.pixels_per_second;
    if (!fit_graph_view(&state, projection, plot) ||
        !timeline_graph_model::pan_view(
            &state.timeline_editor.graph_view.view, 17.0, -9.0) ||
        !timeline_graph_model::zoom_time_at(
            &state.timeline_editor.graph_view.view, plot, 200.0, 1.0) ||
        !timeline_graph_model::zoom_value_at(
            &state.timeline_editor.graph_view.view, plot, 120.0, -1.0) ||
        !finite_view(state.timeline_editor.graph_view.view) ||
        state.timeline_editor.view_start_seconds !=
            dopesheet_view_start_before ||
        state.timeline_editor.pixels_per_second !=
            dopesheet_pixels_per_second_before) {
        std::cerr << "Graph transient fit/pan/zoom did not retain a finite view.\n";
        return false;
    }

    if (marrow::editor::serialize_project(*state.session.project()) != project_before ||
        state.session.undo_count() != undo_before ||
        state.session.redo_count() != redo_before ||
        state.session.project_revision() != project_revision_before ||
        state.session.runtime_revision() != runtime_revision_before ||
        marrow::editor::agent_operation_descriptor_count() != operation_count_before ||
        state.session.dirty() != dirty_before ||
        state.project_dirty != shell_dirty_before) {
        std::cerr << "Graph display synchronization leaked into persistent state/history.\n";
        return false;
    }


    const std::uint64_t stable_generation = state.timeline_editor.graph_cache.generation;
    (void)cached_timeline_graph_projection(&state, *translate);
    if (state.timeline_editor.graph_cache.generation != stable_generation) {
        std::cerr << "An unchanged graph cache key rebuilt its projection.\n";
        return false;
    }

    const std::uint64_t expected_cache_revision = state.session.runtime_revision();
    const marrow::runtime::SkeletonData* expected_cache_skeleton =
        state.session.runtime_data();
    state.timeline_editor.graph_cache.runtime_revision =
        expected_cache_revision == std::numeric_limits<std::uint64_t>::max()
        ? expected_cache_revision - 1U
        : expected_cache_revision + 1U;
    const std::uint64_t generation_before_revision_mismatch =
        state.timeline_editor.graph_cache.generation;
    (void)cached_timeline_graph_projection(&state, *translate);
    if (state.timeline_editor.graph_cache.generation !=
            generation_before_revision_mismatch + 1U ||
        state.timeline_editor.graph_cache.runtime_revision !=
            expected_cache_revision ||
        state.timeline_editor.graph_cache.skeleton_identity !=
            expected_cache_skeleton) {
        std::cerr << "An isolated runtime-revision cache-key mismatch did not rebuild projection.\n";
        return false;
    }

    state.timeline_editor.graph_cache.skeleton_identity = nullptr;
    const std::uint64_t generation_before_skeleton_mismatch =
        state.timeline_editor.graph_cache.generation;
    (void)cached_timeline_graph_projection(&state, *translate);
    if (expected_cache_skeleton == nullptr ||
        state.timeline_editor.graph_cache.generation !=
            generation_before_skeleton_mismatch + 1U ||
        state.timeline_editor.graph_cache.runtime_revision !=
            expected_cache_revision ||
        state.timeline_editor.graph_cache.skeleton_identity !=
            expected_cache_skeleton) {
        std::cerr << "An isolated skeleton-identity cache-key mismatch did not rebuild projection.\n";
        return false;
    }

    const TimelineTrackRow* color = require_track(tracks, "slot:0:Color");
    if (color == nullptr) return false;
    state.timeline_editor.view_mode = TimelineViewMode::Graph;
    state.timeline_editor.graph_view.active_component = GraphComponent::X;
    const auto& color_projection = cached_timeline_graph_projection(&state, *color);
    if (color_projection.status != GraphProjectionStatus::Ready ||
        !color_projection.track.has_value() ||
        !state.timeline_editor.graph_view.needs_fit ||
        state.timeline_editor.graph_view.active_component.has_value() ||
        state.timeline_editor.graph_view.component_visible !=
            std::array<bool, 4>{true, true, true, true} ||
        !fit_graph_view(&state, color_projection, plot) ||
        state.timeline_editor.graph_view.fitted_track_id != color->id ||
        state.timeline_editor.graph_view.fitted_animation_name != "idle" ||
        state.timeline_editor.view_mode != TimelineViewMode::Graph) {
        std::cerr << "A graph track-context change did not reset components and fit.\n";
        return false;
    }

    if (!set_selected_animation(&state, "attack", "Graph smoke", false, true)) {
        std::cerr << "Graph lifecycle smoke could not select attack.\n";
        return false;
    }
    const auto& attack_tracks = cached_timeline_tracks(&state);
    const auto attack_it = std::find_if(
        attack_tracks.begin(), attack_tracks.end(), [](const TimelineTrackRow& row) {
            return timeline_graph_model::track_is_supported(row);
        });
    if (attack_it == attack_tracks.end()) {
        std::cerr << "Graph lifecycle smoke requires a supported attack track.\n";
        return false;
    }
    const auto& attack_projection =
        cached_timeline_graph_projection(&state, *attack_it);
    if (attack_projection.status != GraphProjectionStatus::Ready ||
        !attack_projection.track.has_value() ||
        !state.timeline_editor.graph_view.needs_fit ||
        !fit_graph_view(&state, attack_projection, plot) ||
        state.timeline_editor.graph_view.fitted_track_id != attack_it->id ||
        state.timeline_editor.graph_view.fitted_animation_name != "attack" ||
        state.timeline_editor.view_mode != TimelineViewMode::Graph) {
        std::cerr << "A graph animation-context change did not request and record fit.\n";
        return false;
    }

    if (!set_selected_animation(&state, "idle", "Graph smoke", false, true)) {
        std::cerr << "Graph lifecycle smoke could not restore idle.\n";
        return false;
    }
    const auto& idle_tracks = cached_timeline_tracks(&state);
    const TimelineTrackRow* lifecycle_translate =
        require_track(idle_tracks, "bone:1:Translate");
    if (lifecycle_translate == nullptr) return false;
    const auto& lifecycle_projection =
        cached_timeline_graph_projection(&state, *lifecycle_translate);
    if (lifecycle_projection.status != GraphProjectionStatus::Ready ||
        !lifecycle_projection.track.has_value() ||
        lifecycle_projection.track->keys.empty() ||
        !fit_graph_view(&state, lifecycle_projection, plot) ||
        !timeline_graph_model::pan_view(
            &state.timeline_editor.graph_view.view, 23.0, 11.0)) {
        std::cerr << "Graph lifecycle smoke could not stage its same-track view.\n";
        return false;
    }
    const timeline_graph_model::View view_before_runtime =
        state.timeline_editor.graph_view.view;
    const std::uint64_t generation_before_runtime =
        state.timeline_editor.graph_cache.generation;
    const std::uint64_t revision_before_edit = state.session.runtime_revision();
    const TimelineKeyRef survivor = lifecycle_projection.track->keys.front().identity;

    double inserted_time = 0.33337;
    while (std::any_of(
        lifecycle_translate->key_times.begin(),
        lifecycle_translate->key_times.end(),
        [&](double time) { return std::abs(time - inserted_time) <= 1e-6; })) {
        inserted_time += 0.01337;
    }
    const std::string project_before_lifecycle =
        marrow::editor::serialize_project(*state.session.project());
    state.session.clear_history();
    auto transaction = state.session.begin_edit({
        marrow::editor::EditKind::AddKeyframe,
        "Graph cache lifecycle smoke",
        {},
        false,
        marrow::editor::EditImpact::Project |
            marrow::editor::EditImpact::Runtime |
            marrow::editor::EditImpact::Preview});
    if (!transaction) {
        std::cerr << "Graph lifecycle smoke could not begin its runtime edit.\n";
        return false;
    }
    marrow::editor::upsert_transform_keyframe(
        *transaction.project(),
        *state.session.runtime_data(),
        "idle",
        "spine",
        marrow::editor::TransformTimelineChannel::Translate,
        inserted_time,
        marrow::editor::TransformKeyframePatch{
            std::nullopt,
            9.25,
            -4.5});
    const marrow::editor::SessionResult committed = transaction.commit();
    sync_shell_from_editor_session(&state);
    if (!committed || !committed.changed ||
        state.session.runtime_revision() <= revision_before_edit) {
        std::cerr << "Graph lifecycle smoke runtime edit did not commit.\n";
        return false;
    }

    const auto& edited_tracks = cached_timeline_tracks(&state);
    const TimelineTrackRow* edited_translate =
        require_track(edited_tracks, "bone:1:Translate");
    if (edited_translate == nullptr) return false;
    const auto& edited_projection =
        cached_timeline_graph_projection(&state, *edited_translate);
    if (edited_projection.status != GraphProjectionStatus::Ready ||
        !edited_projection.track.has_value()) {
        std::cerr << "Graph lifecycle smoke lost its edited projection.\n";
        return false;
    }
    const auto inserted_it = std::find_if(
        edited_projection.track->keys.begin(),
        edited_projection.track->keys.end(),
        [&](const timeline_graph_model::Key& key) {
            return std::abs(key.time_seconds - inserted_time) <= 1e-6;
        });
    if (inserted_it == edited_projection.track->keys.end() ||
        state.timeline_editor.graph_cache.generation != generation_before_runtime + 1U ||
        state.timeline_editor.graph_view.needs_fit ||
        !same_view(state.timeline_editor.graph_view.view, view_before_runtime)) {
        std::cerr << "Same-track runtime revision did not rebuild while preserving view.\n";
        return false;
    }

    const TimelineKeyRef inserted = inserted_it->identity;
    state.timeline_editor.selected_keys = {survivor, inserted};
    state.timeline_editor.active_key = inserted;
    const timeline_graph_model::View view_before_history =
        state.timeline_editor.graph_view.view;
    if (!state.session.undo()) {
        std::cerr << "Graph lifecycle smoke could not undo its runtime edit.\n";
        return false;
    }
    sync_shell_from_editor_session(&state);
    const auto& undone_tracks = cached_timeline_tracks(&state);
    const TimelineTrackRow* undone_translate =
        require_track(undone_tracks, "bone:1:Translate");
    if (undone_translate == nullptr) return false;
    (void)cached_timeline_graph_projection(&state, *undone_translate);
    reconcile_timeline_key_selection(&state, undone_tracks);
    if (state.timeline_editor.selected_keys != std::vector<TimelineKeyRef>{survivor} ||
        !(state.timeline_editor.active_key == std::optional<TimelineKeyRef>(survivor)) ||
        !same_view(state.timeline_editor.graph_view.view, view_before_history) ||
        state.timeline_editor.graph_view.needs_fit) {
        std::cerr << "Graph undo did not prune only the stale active identity.\n";
        return false;
    }

    if (!state.session.redo()) {
        std::cerr << "Graph lifecycle smoke could not redo its runtime edit.\n";
        return false;
    }
    sync_shell_from_editor_session(&state);
    const auto& redone_tracks = cached_timeline_tracks(&state);
    const TimelineTrackRow* redone_translate =
        require_track(redone_tracks, "bone:1:Translate");
    if (redone_translate == nullptr) return false;
    (void)cached_timeline_graph_projection(&state, *redone_translate);
    reconcile_timeline_key_selection(&state, redone_tracks);
    if (state.timeline_editor.selected_keys != std::vector<TimelineKeyRef>{survivor} ||
        !(state.timeline_editor.active_key == std::optional<TimelineKeyRef>(survivor)) ||
        !same_view(state.timeline_editor.graph_view.view, view_before_history) ||
        state.timeline_editor.graph_view.needs_fit ||
        !state.session.undo()) {
        std::cerr << "Graph redo changed the finite same-track view or stable identity.\n";
        return false;
    }
    sync_shell_from_editor_session(&state);
    state.session.clear_history();
    if (marrow::editor::serialize_project(*state.session.project()) !=
            project_before_lifecycle) {
        std::cerr << "Graph lifecycle smoke did not restore its isolated project.\n";
        return false;
    }

    state.timeline_editor.view_mode = TimelineViewMode::Graph;
    state.timeline_editor.requested_view_mode = TimelineViewMode::Dopesheet;
    state.timeline_editor.graph_view.component_visible = {false, true, false, true};
    state.timeline_editor.graph_view.active_component = GraphComponent::Alpha;
    state.timeline_editor.graph_view.fitted_track_id = "seeded-fitted-track";
    state.timeline_editor.graph_view.fitted_animation_name = "seeded-fitted-animation";
    state.timeline_editor.graph_view.needs_fit = false;
    state.timeline_editor.graph_view.view = {-12.5, 321.0, 45.5, 0.125};
    state.timeline_editor.graph_cache.runtime_revision = 77U;
    state.timeline_editor.graph_cache.skeleton_identity = state.session.runtime_data();
    state.timeline_editor.graph_cache.animation_name = "seeded-cache-animation";
    state.timeline_editor.graph_cache.track_id = "seeded-cache-track";
    state.timeline_editor.graph_cache.projection.status = GraphProjectionStatus::Ready;
    state.timeline_editor.graph_cache.projection.track.emplace();
    state.timeline_editor.graph_cache.projection.track->track_id =
        "seeded-ready-projection";
    state.timeline_editor.graph_cache.projection.track->label =
        "Seeded ready projection";
    state.timeline_editor.graph_cache.projection.track->kind =
        timeline_graph_model::TrackKind::SlotColor;
    state.timeline_editor.graph_cache.projection.track->components = {
        {GraphComponent::Red, "Red"}};
    state.timeline_editor.graph_cache.valid = true;
    state.timeline_editor.graph_cache.generation = 99U;
    state.timeline_editor.selected_keys = {survivor};
    state.timeline_editor.active_key = survivor;
    if (!reload_project(&state)) {
        std::cerr << "Graph lifecycle smoke could not exercise source adoption.\n";
        return false;
    }
    const timeline_graph_model::View default_view{};
    if (state.timeline_editor.view_mode != TimelineViewMode::Dopesheet ||
        state.timeline_editor.requested_view_mode.has_value() ||
        state.timeline_editor.graph_view.component_visible !=
            std::array<bool, 4>{true, true, true, true} ||
        state.timeline_editor.graph_view.active_component.has_value() ||
        !state.timeline_editor.graph_view.fitted_track_id.empty() ||
        !state.timeline_editor.graph_view.fitted_animation_name.empty() ||
        !same_view(state.timeline_editor.graph_view.view, default_view) ||
        !state.timeline_editor.graph_view.needs_fit ||
        state.timeline_editor.graph_cache.runtime_revision != 0U ||
        state.timeline_editor.graph_cache.skeleton_identity != nullptr ||
        !state.timeline_editor.graph_cache.animation_name.empty() ||
        !state.timeline_editor.graph_cache.track_id.empty() ||
        state.timeline_editor.graph_cache.projection.status !=
            GraphProjectionStatus::UnsupportedTrack ||
        state.timeline_editor.graph_cache.projection.track.has_value() ||
        state.timeline_editor.graph_cache.valid ||
        state.timeline_editor.graph_cache.generation != 0U ||
        !state.timeline_editor.selected_keys.empty() ||
        state.timeline_editor.active_key.has_value()) {
        std::cerr << "TimelineEditorState source adoption did not reset graph state atomically.\n";
        return false;
    }
    return true;
}

} // namespace marrow::editor::shell
