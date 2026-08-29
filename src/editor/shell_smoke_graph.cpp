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

namespace {

struct GraphEditSnapshot {
    std::string project;
    std::size_t undo_count{0U};
    std::size_t redo_count{0U};
    std::uint64_t project_revision{0U};
    bool dirty{false};
    bool shell_dirty{false};
    std::vector<double> dopesheet_key_times;
    std::vector<double> graph_times;
    std::vector<double> graph_values;
};

/**
 * @brief Captures everything an atomic rollback must restore byte-for-byte.
 *
 * Both views are rebuilt from the restored runtime data, so comparing them
 * proves the graph and the dopesheet return to the same pre-gesture truth.
 */
std::optional<GraphEditSnapshot> capture_graph_edit_snapshot(
    ShellState* state,
    std::string_view track_id) {
    if (state == nullptr || state->session.project() == nullptr) return std::nullopt;
    const TimelineTrackRow* row =
        find_timeline_track(cached_timeline_tracks(state), track_id);
    if (row == nullptr) return std::nullopt;
    GraphEditSnapshot snapshot;
    snapshot.project = marrow::editor::serialize_project(*state->session.project());
    snapshot.undo_count = state->session.undo_count();
    snapshot.redo_count = state->session.redo_count();
    snapshot.project_revision = state->session.project_revision();
    snapshot.dirty = state->session.dirty();
    snapshot.shell_dirty = state->project_dirty;
    snapshot.dopesheet_key_times = row->key_times;
    const auto& projection = cached_timeline_graph_projection(state, *row);
    if (projection.status != GraphProjectionStatus::Ready ||
        !projection.track.has_value()) {
        return std::nullopt;
    }
    for (const auto& key : projection.track->keys) {
        snapshot.graph_times.push_back(key.time_seconds);
        for (std::size_t component = 0U; component < key.value_count; ++component) {
            snapshot.graph_values.push_back(key.values[component]);
        }
    }
    return snapshot;
}

bool graph_edit_snapshots_match(
    const GraphEditSnapshot& left,
    const GraphEditSnapshot& right) {
    return left.project == right.project && left.undo_count == right.undo_count &&
        left.redo_count == right.redo_count &&
        left.project_revision == right.project_revision &&
        left.dirty == right.dirty && left.shell_dirty == right.shell_dirty &&
        left.dopesheet_key_times == right.dopesheet_key_times &&
        left.graph_times == right.graph_times &&
        left.graph_values == right.graph_values;
}

struct ProjectedKey {
    double time_seconds{0.0};
    std::array<double, 4> values{};
    std::size_t value_count{0U};
    marrow::runtime::InterpolationKind easing{
        marrow::runtime::InterpolationKind::Linear};
};

std::optional<ProjectedKey> projected_key(
    ShellState* state,
    std::string_view track_id,
    const TimelineKeyRef& key) {
    if (state == nullptr) return std::nullopt;
    const TimelineTrackRow* row =
        find_timeline_track(cached_timeline_tracks(state), track_id);
    if (row == nullptr) return std::nullopt;
    const auto& projection = cached_timeline_graph_projection(state, *row);
    if (projection.status != GraphProjectionStatus::Ready ||
        !projection.track.has_value()) {
        return std::nullopt;
    }
    for (const auto& candidate : projection.track->keys) {
        if (!(candidate.identity == key)) continue;
        ProjectedKey result;
        result.time_seconds = candidate.time_seconds;
        result.values = candidate.values;
        result.value_count = candidate.value_count;
        result.easing = candidate.outgoing_easing.kind();
        return result;
    }
    return std::nullopt;
}

bool near_value(double left, double right, double tolerance = 1e-6) {
    return std::abs(left - right) <= tolerance;
}

} // namespace

bool validate_timeline_graph_edit_shell_smoke(
    const std::filesystem::path& project_path) {
    ShellState state;
    state.project_path = project_path;
    if (!reload_project(&state) ||
        !set_selected_animation(&state, "idle", "Graph edit smoke", false, true)) {
        std::cerr << "Graph edit shell smoke could not load player_idle/idle.\n";
        return false;
    }
    const std::size_t operation_count_before =
        marrow::editor::agent_operation_descriptor_count();
    if (operation_count_before != 56U) {
        std::cerr << "Graph edit shell smoke requires the unchanged 56-operation registry.\n";
        return false;
    }

    const auto row_of = [&](std::string_view id) {
        return find_timeline_track(cached_timeline_tracks(&state), id);
    };
    const auto key_of = [&](std::string_view id, std::size_t index)
        -> std::optional<TimelineKeyRef> {
        const TimelineTrackRow* row = row_of(id);
        if (row == nullptr || index >= row->key_times.size()) return std::nullopt;
        return timeline_key_ref(*row, index);
    };

    // --- One Translate X value drag: one transaction, one undo entry, stable
    // selection, and untouched Y / time / easing. ---
    {
        const auto first_key = key_of("bone:1:Translate", 0U);
        if (!first_key.has_value()) {
            std::cerr << "Graph edit smoke requires a spine Translate key.\n";
            return false;
        }
        const auto before_key = projected_key(&state, "bone:1:Translate", *first_key);
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!before_key.has_value() || !before.has_value()) {
            std::cerr << "Graph edit smoke could not capture its Translate baseline.\n";
            return false;
        }
        state.timeline_editor.selected_keys = {*first_key};
        state.timeline_editor.active_key = *first_key;
        const TimelineTrackRow* translate = row_of("bone:1:Translate");
        if (translate == nullptr ||
            !begin_timeline_graph_value_gesture(
                &state, 4242U, *translate, GraphComponent::X,
                cached_timeline_tracks(&state)) ||
            !authoring_gesture_active(state)) {
            std::cerr << "The graph value gesture did not open one live transaction.\n";
            return false;
        }
        if (!apply_timeline_graph_value_delta(&state, cached_timeline_tracks(&state), 3.0) ||
            !apply_timeline_graph_value_delta(&state, cached_timeline_tracks(&state), 5.0)) {
            std::cerr << "The graph value gesture did not preview its delta.\n";
            return false;
        }
        if (state.session.undo_count() != before->undo_count) {
            std::cerr << "A live graph value preview created a history entry.\n";
            return false;
        }
        finish_timeline_graph_value_gesture(&state, true);
        if (authoring_gesture_active(state) ||
            state.session.undo_count() != before->undo_count + 1U ||
            state.timeline_editor.selected_keys !=
                std::vector<TimelineKeyRef>{*first_key} ||
            !(state.timeline_editor.active_key ==
              std::optional<TimelineKeyRef>(*first_key))) {
            std::cerr << "One graph value drag did not produce one stable-selection undo entry.\n";
            return false;
        }
        const auto after_key = projected_key(&state, "bone:1:Translate", *first_key);
        if (!after_key.has_value() ||
            !near_value(after_key->values[0], before_key->values[0] + 5.0) ||
            !near_value(after_key->values[1], before_key->values[1]) ||
            after_key->time_seconds != before_key->time_seconds ||
            after_key->easing != before_key->easing) {
            std::cerr << "A graph X drag did not move only the active component.\n";
            return false;
        }

        // Undo and redo of a committed value edit keep selection intact,
        // because the edit never moved a key in time.
        if (!state.session.undo()) {
            std::cerr << "Graph value edit could not be undone.\n";
            return false;
        }
        sync_shell_from_editor_session(&state);
        const auto undone_key = projected_key(&state, "bone:1:Translate", *first_key);
        if (!undone_key.has_value() ||
            !near_value(undone_key->values[0], before_key->values[0]) ||
            state.timeline_editor.selected_keys !=
                std::vector<TimelineKeyRef>{*first_key} ||
            !(state.timeline_editor.active_key ==
              std::optional<TimelineKeyRef>(*first_key))) {
            std::cerr << "Undoing a graph value edit lost its value or selection.\n";
            return false;
        }
        if (!state.session.redo()) {
            std::cerr << "Graph value edit could not be redone.\n";
            return false;
        }
        sync_shell_from_editor_session(&state);
        const auto redone_key = projected_key(&state, "bone:1:Translate", *first_key);
        if (!redone_key.has_value() ||
            !near_value(redone_key->values[0], before_key->values[0] + 5.0) ||
            state.timeline_editor.selected_keys !=
                std::vector<TimelineKeyRef>{*first_key} ||
            !(state.timeline_editor.active_key ==
              std::optional<TimelineKeyRef>(*first_key))) {
            std::cerr << "Redoing a graph value edit lost its value or selection.\n";
            return false;
        }
        if (!state.session.undo()) {
            std::cerr << "Graph value smoke could not restore its baseline.\n";
            return false;
        }
        sync_shell_from_editor_session(&state);
        state.session.clear_history();
    }

    // --- A cancelled gesture restores project, history, revision, dopesheet
    // key times, and every graph component value. ---
    {
        const auto first_key = key_of("bone:1:Translate", 0U);
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!first_key.has_value() || !before.has_value()) return false;
        state.timeline_editor.selected_keys = {*first_key};
        state.timeline_editor.active_key = *first_key;
        const TimelineTrackRow* translate = row_of("bone:1:Translate");
        if (translate == nullptr ||
            !begin_timeline_graph_value_gesture(
                &state, 4242U, *translate, GraphComponent::Y,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_value_delta(
                &state, cached_timeline_tracks(&state), -9.5)) {
            std::cerr << "Graph value cancel smoke could not stage its gesture.\n";
            return false;
        }
        finish_timeline_graph_value_gesture(&state, false);
        const auto after = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (authoring_gesture_active(state) || !after.has_value() ||
            !graph_edit_snapshots_match(*before, *after) ||
            state.timeline_editor.selected_keys !=
                std::vector<TimelineKeyRef>{*first_key}) {
            std::cerr << "A cancelled graph value gesture did not roll back atomically.\n";
            return false;
        }
    }

    // --- A non-finite delta cancels the gesture with the same rollback. ---
    {
        const auto first_key = key_of("bone:1:Translate", 0U);
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!first_key.has_value() || !before.has_value()) return false;
        state.timeline_editor.selected_keys = {*first_key};
        state.timeline_editor.active_key = *first_key;
        const TimelineTrackRow* translate = row_of("bone:1:Translate");
        if (translate == nullptr ||
            !begin_timeline_graph_value_gesture(
                &state, 4242U, *translate, GraphComponent::X,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_value_delta(
                &state, cached_timeline_tracks(&state), 2.0)) {
            std::cerr << "Graph value non-finite smoke could not stage its gesture.\n";
            return false;
        }
        if (apply_timeline_graph_value_delta(
                &state,
                cached_timeline_tracks(&state),
                std::numeric_limits<double>::quiet_NaN()) ||
            authoring_gesture_active(state)) {
            std::cerr << "A non-finite graph value delta did not cancel its gesture.\n";
            return false;
        }
        const auto after = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!after.has_value() || !graph_edit_snapshots_match(*before, *after)) {
            std::cerr << "A non-finite graph value delta left the project changed.\n";
            return false;
        }
    }

    // --- A zero-net drag commits nothing. The focused row is already authored
    // in the fixture project, so materialization is a no-op and the shared
    // history-equality rule can prove the drag left no entry. ---
    {
        const auto rotate_key = key_of("bone:1:Rotate", 0U);
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Rotate");
        if (!rotate_key.has_value() || !before.has_value()) return false;
        state.timeline_editor.selected_keys = {*rotate_key};
        state.timeline_editor.active_key = *rotate_key;
        const TimelineTrackRow* rotate = row_of("bone:1:Rotate");
        if (rotate == nullptr ||
            !begin_timeline_graph_value_gesture(
                &state, 4242U, *rotate, GraphComponent::Angle,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_value_delta(
                &state, cached_timeline_tracks(&state), 4.0) ||
            !apply_timeline_graph_value_delta(
                &state, cached_timeline_tracks(&state), 0.0)) {
            std::cerr << "Graph zero-net smoke could not stage its gesture.\n";
            return false;
        }
        finish_timeline_graph_value_gesture(&state, true);
        const auto after = capture_graph_edit_snapshot(&state, "bone:1:Rotate");
        if (authoring_gesture_active(state) || !after.has_value() ||
            !graph_edit_snapshots_match(*before, *after)) {
            std::cerr << "A zero-net graph value drag committed a history entry.\n";
            return false;
        }
    }

    // --- Multi-key drag preserves the difference between the selected keys. ---
    {
        const auto first_key = key_of("bone:1:Translate", 0U);
        const auto second_key = key_of("bone:1:Translate", 1U);
        if (!first_key.has_value() || !second_key.has_value()) return false;
        const auto before_first = projected_key(&state, "bone:1:Translate", *first_key);
        const auto before_second = projected_key(&state, "bone:1:Translate", *second_key);
        const std::size_t undo_before = state.session.undo_count();
        if (!before_first.has_value() || !before_second.has_value()) return false;
        state.timeline_editor.selected_keys = {*first_key, *second_key};
        state.timeline_editor.active_key = *first_key;
        const TimelineTrackRow* translate = row_of("bone:1:Translate");
        if (translate == nullptr ||
            !begin_timeline_graph_value_gesture(
                &state, 4242U, *translate, GraphComponent::X,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_value_delta(
                &state, cached_timeline_tracks(&state), 3.0)) {
            std::cerr << "Graph multi-key smoke could not stage its gesture.\n";
            return false;
        }
        finish_timeline_graph_value_gesture(&state, true);
        const auto after_first = projected_key(&state, "bone:1:Translate", *first_key);
        const auto after_second = projected_key(&state, "bone:1:Translate", *second_key);
        if (!after_first.has_value() || !after_second.has_value() ||
            state.session.undo_count() != undo_before + 1U ||
            !near_value(after_first->values[0], before_first->values[0] + 3.0) ||
            !near_value(after_second->values[0], before_second->values[0] + 3.0) ||
            !near_value(
                after_second->values[0] - after_first->values[0],
                before_second->values[0] - before_first->values[0]) ||
            state.timeline_editor.selected_keys !=
                std::vector<TimelineKeyRef>{*first_key, *second_key}) {
            std::cerr << "A multi-key graph value drag did not preserve key spacing.\n";
            return false;
        }
        if (!state.session.undo()) return false;
        sync_shell_from_editor_session(&state);
        state.session.clear_history();
    }

    // --- Rotate Angle moves the absolute projected angle by the delta. ---
    {
        const auto rotate_key = key_of("bone:1:Rotate", 0U);
        if (!rotate_key.has_value()) return false;
        const auto before_key = projected_key(&state, "bone:1:Rotate", *rotate_key);
        if (!before_key.has_value()) return false;
        state.timeline_editor.selected_keys = {*rotate_key};
        state.timeline_editor.active_key = *rotate_key;
        const TimelineTrackRow* rotate = row_of("bone:1:Rotate");
        if (rotate == nullptr ||
            !begin_timeline_graph_value_gesture(
                &state, 4242U, *rotate, GraphComponent::Angle,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_value_delta(
                &state, cached_timeline_tracks(&state), 12.5)) {
            std::cerr << "Graph rotate smoke could not stage its gesture.\n";
            return false;
        }
        finish_timeline_graph_value_gesture(&state, true);
        const auto after_key = projected_key(&state, "bone:1:Rotate", *rotate_key);
        if (!after_key.has_value() ||
            !near_value(after_key->values[0], before_key->values[0] + 12.5) ||
            after_key->time_seconds != before_key->time_seconds ||
            after_key->easing != before_key->easing) {
            std::cerr << "A graph Angle drag did not move the absolute projected angle.\n";
            return false;
        }
        if (!state.session.undo()) return false;
        sync_shell_from_editor_session(&state);
        state.session.clear_history();
    }

    // --- Scale X reaches exactly zero and stays authorable. ---
    {
        const auto scale_key = key_of("bone:1:Scale", 0U);
        if (!scale_key.has_value()) return false;
        const auto before_key = projected_key(&state, "bone:1:Scale", *scale_key);
        if (!before_key.has_value() || !near_value(before_key->values[0], 1.0)) {
            std::cerr << "Graph scale smoke requires a unit scale key.\n";
            return false;
        }
        state.timeline_editor.selected_keys = {*scale_key};
        state.timeline_editor.active_key = *scale_key;
        const TimelineTrackRow* scale = row_of("bone:1:Scale");
        if (scale == nullptr ||
            !begin_timeline_graph_value_gesture(
                &state, 4242U, *scale, GraphComponent::X,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_value_delta(
                &state, cached_timeline_tracks(&state), -1.0)) {
            std::cerr << "Graph scale smoke could not stage its gesture.\n";
            return false;
        }
        finish_timeline_graph_value_gesture(&state, true);
        const auto after_key = projected_key(&state, "bone:1:Scale", *scale_key);
        if (!after_key.has_value() || after_key->values[0] != 0.0 ||
            !near_value(after_key->values[1], before_key->values[1])) {
            std::cerr << "A graph Scale X drag did not reach exact zero.\n";
            return false;
        }
        if (!state.session.undo()) return false;
        sync_shell_from_editor_session(&state);
        state.session.clear_history();
    }

    // --- Shear Y edits only its own component. ---
    {
        const auto shear_key = key_of("bone:1:Shear", 1U);
        if (!shear_key.has_value()) return false;
        const auto before_key = projected_key(&state, "bone:1:Shear", *shear_key);
        if (!before_key.has_value()) return false;
        state.timeline_editor.selected_keys = {*shear_key};
        state.timeline_editor.active_key = *shear_key;
        const TimelineTrackRow* shear = row_of("bone:1:Shear");
        if (shear == nullptr ||
            !begin_timeline_graph_value_gesture(
                &state, 4242U, *shear, GraphComponent::Y,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_value_delta(
                &state, cached_timeline_tracks(&state), 7.25)) {
            std::cerr << "Graph shear smoke could not stage its gesture.\n";
            return false;
        }
        finish_timeline_graph_value_gesture(&state, true);
        const auto after_key = projected_key(&state, "bone:1:Shear", *shear_key);
        if (!after_key.has_value() ||
            !near_value(after_key->values[1], before_key->values[1] + 7.25) ||
            !near_value(after_key->values[0], before_key->values[0]) ||
            after_key->time_seconds != before_key->time_seconds) {
            std::cerr << "A graph Shear Y drag did not preserve X.\n";
            return false;
        }
        if (!state.session.undo()) return false;
        sync_shell_from_editor_session(&state);
        state.session.clear_history();
    }

    // --- Slot Color Alpha clamps group-wide: both keys stop together. ---
    {
        const auto alpha_first = key_of("slot:0:Color", 0U);
        const auto alpha_second = key_of("slot:0:Color", 1U);
        if (!alpha_first.has_value() || !alpha_second.has_value()) return false;
        const auto before_first = projected_key(&state, "slot:0:Color", *alpha_first);
        const auto before_second = projected_key(&state, "slot:0:Color", *alpha_second);
        if (!before_first.has_value() || !before_second.has_value() ||
            !near_value(before_first->values[3], 1.0) ||
            !near_value(before_second->values[3], 0.5)) {
            std::cerr << "Graph colour smoke requires alpha keys at 1.0 and 0.5.\n";
            return false;
        }
        state.timeline_editor.selected_keys = {*alpha_first, *alpha_second};
        state.timeline_editor.active_key = *alpha_second;
        const TimelineTrackRow* color = row_of("slot:0:Color");
        if (color == nullptr ||
            !begin_timeline_graph_value_gesture(
                &state, 4242U, *color, GraphComponent::Alpha,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_value_delta(
                &state, cached_timeline_tracks(&state), -0.9)) {
            std::cerr << "Graph colour smoke could not stage its gesture.\n";
            return false;
        }
        finish_timeline_graph_value_gesture(&state, true);
        const auto after_first = projected_key(&state, "slot:0:Color", *alpha_first);
        const auto after_second = projected_key(&state, "slot:0:Color", *alpha_second);
        if (!after_first.has_value() || !after_second.has_value() ||
            !near_value(after_first->values[3], 0.5) ||
            !near_value(after_second->values[3], 0.0) ||
            !near_value(
                after_first->values[3] - after_second->values[3],
                before_first->values[3] - before_second->values[3]) ||
            !near_value(after_first->values[0], before_first->values[0]) ||
            !near_value(after_first->values[1], before_first->values[1]) ||
            !near_value(after_first->values[2], before_first->values[2])) {
            std::cerr << "A graph Alpha drag did not stop both keys at the group clamp.\n";
            return false;
        }
        if (!state.session.undo()) return false;
        sync_shell_from_editor_session(&state);
        state.session.clear_history();
    }

    // --- Fail-closed contracts. ---
    {
        const auto attachment_key = key_of("slot:0:Attachment", 0U);
        const auto translate_key = key_of("bone:1:Translate", 0U);
        if (!attachment_key.has_value() || !translate_key.has_value()) return false;
        const TimelineTrackRow* attachment = row_of("slot:0:Attachment");
        if (attachment != nullptr) {
            state.timeline_editor.selected_keys = {*attachment_key};
            state.timeline_editor.active_key = *attachment_key;
            if (begin_timeline_graph_value_gesture(
                    &state, 4242U, *attachment, GraphComponent::X,
                    cached_timeline_tracks(&state)) ||
                authoring_gesture_active(state)) {
                std::cerr << "An unsupported graph row opened a value gesture.\n";
                return false;
            }
        }
        const TimelineTrackRow* translate = row_of("bone:1:Translate");
        state.timeline_editor.selected_keys = {*translate_key};
        state.timeline_editor.active_key = *translate_key;
        if (translate == nullptr ||
            begin_timeline_graph_value_gesture(
                &state, 4242U, *translate, GraphComponent::Alpha,
                cached_timeline_tracks(&state)) ||
            authoring_gesture_active(state)) {
            std::cerr << "An unsupported (row, component) pairing opened a value gesture.\n";
            return false;
        }
        state.timeline_editor.selected_keys.clear();
        state.timeline_editor.active_key.reset();
        if (begin_timeline_graph_value_gesture(
                &state, 4242U, *row_of("bone:1:Translate"), GraphComponent::X,
                cached_timeline_tracks(&state)) ||
            authoring_gesture_active(state)) {
            std::cerr << "An empty selection opened a graph value gesture.\n";
            return false;
        }
        // A live gesture blocks another, and cancel_authoring_gestures releases it.
        state.timeline_editor.selected_keys = {*translate_key};
        state.timeline_editor.active_key = *translate_key;
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!before.has_value() ||
            !begin_timeline_graph_value_gesture(
                &state, 4242U, *row_of("bone:1:Translate"), GraphComponent::X,
                cached_timeline_tracks(&state)) ||
            begin_timeline_graph_value_gesture(
                &state, 4243U, *row_of("bone:1:Translate"), GraphComponent::Y,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_value_delta(
                &state, cached_timeline_tracks(&state), 6.0)) {
            std::cerr << "Graph value exclusivity smoke could not stage its gesture.\n";
            return false;
        }
        cancel_authoring_gestures(&state, "graph value smoke");
        const auto after = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (authoring_gesture_active(state) ||
            state.timeline_editor.graph_value_gesture.has_value() ||
            !after.has_value() || !graph_edit_snapshots_match(*before, *after)) {
            std::cerr << "cancel_authoring_gestures did not release the graph value gesture.\n";
            return false;
        }
    }


    // --- Graph point drag driver: dead zone, axis lock, and shared retime. ---
    // A fixed view keeps the pixel-to-unit mapping exact: x = 200 * time and
    // y = 160 - 10 * value on this 640x320 plot.
    constexpr timeline_graph_model::PlotRect drag_plot{0.0, 0.0, 640.0, 320.0};
    const timeline_graph_model::View drag_view{0.0, 200.0, 0.0, 10.0};
    const auto press_x_of = [&](double time_seconds) {
        return timeline_graph_model::x_at_time(drag_plot, drag_view, time_seconds);
    };
    const auto press_y_of = [&](double value) {
        return timeline_graph_model::y_at_value(drag_plot, drag_view, value);
    };
    const auto arm_drag = [&](std::string_view track_id,
                              std::size_t key_index,
                              GraphComponent component,
                              std::size_t component_index) -> bool {
        const auto key = key_of(track_id, key_index);
        const TimelineTrackRow* row = row_of(track_id);
        if (!key.has_value() || row == nullptr) return false;
        const auto projected = projected_key(&state, track_id, *key);
        if (!projected.has_value() || component_index >= projected->value_count) {
            return false;
        }
        state.timeline_editor.selected_keys = {*key};
        state.timeline_editor.active_key = *key;
        const timeline_graph_model::PointHit hit{*key, component, component_index};
        return begin_timeline_graph_point_drag(
            &state,
            *row_of(track_id),
            hit,
            4242U,
            drag_plot,
            drag_view,
            press_x_of(projected->time_seconds),
            press_y_of(projected->values[component_index]));
    };

    // A press alone arms a candidate and opens no transaction or history entry.
    {
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!before.has_value() || !arm_drag("bone:1:Translate", 0U, GraphComponent::X, 0U) ||
            !state.timeline_editor.graph_drag.has_value() ||
            authoring_gesture_active(state) ||
            state.session.undo_count() != before->undo_count) {
            std::cerr << "A graph press must arm a candidate without a transaction.\n";
            return false;
        }
        const auto key = key_of("bone:1:Translate", 0U);
        const auto projected = projected_key(&state, "bone:1:Translate", *key);
        const double press_x = press_x_of(projected->time_seconds);
        const double press_y = press_y_of(projected->values[0]);
        // Inside the dead zone nothing is decided and nothing is written.
        if (!update_timeline_graph_point_drag(
                &state, cached_timeline_tracks(&state),
                press_x + 3.0, press_y + 2.0, true, false, false) ||
            state.timeline_editor.graph_drag->axis !=
                timeline_graph_model::DragAxis::Undecided ||
            authoring_gesture_active(state)) {
            std::cerr << "Motion inside the graph dead zone opened a gesture.\n";
            return false;
        }
        // Dominant vertical motion locks the value axis.
        (void)update_timeline_graph_point_drag(
            &state, cached_timeline_tracks(&state),
            press_x + 2.0, press_y - 40.0, true, false, false);
        if (!state.timeline_editor.graph_value_gesture.has_value() ||
            state.timeline_editor.retime_gesture.has_value() ||
            !state.timeline_editor.graph_drag.has_value() ||
            state.timeline_editor.graph_drag->axis !=
                timeline_graph_model::DragAxis::Value) {
            std::cerr << "A dominant vertical graph drag did not lock the value axis.\n";
            return false;
        }
        // A locked Value axis never edits time, however far the pointer travels
        // horizontally afterwards. This is the acceptance-criterion-1 proof.
        (void)update_timeline_graph_point_drag(
            &state, cached_timeline_tracks(&state),
            press_x + 400.0, press_y - 40.0, true, false, false);
        if (state.timeline_editor.retime_gesture.has_value() ||
            state.timeline_editor.graph_drag->axis !=
                timeline_graph_model::DragAxis::Value) {
            std::cerr << "Later horizontal motion re-decided a locked value axis.\n";
            return false;
        }
        // Releasing commits exactly one entry and clears both slots.
        if (update_timeline_graph_point_drag(
                &state, cached_timeline_tracks(&state),
                press_x + 400.0, press_y - 40.0, false, false, false) ||
            state.timeline_editor.graph_drag.has_value() ||
            authoring_gesture_active(state) ||
            state.session.undo_count() != before->undo_count + 1U) {
            std::cerr << "Releasing a graph value drag did not commit one entry.\n";
            return false;
        }
        const auto after = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!after.has_value() ||
            after->dopesheet_key_times != before->dopesheet_key_times ||
            after->graph_times != before->graph_times) {
            std::cerr << "A locked value drag changed a key time.\n";
            return false;
        }
        const auto moved = projected_key(&state, "bone:1:Translate", *key);
        if (!moved.has_value() ||
            !near_value(moved->values[0], projected->values[0] + 4.0) ||
            !near_value(moved->values[1], projected->values[1])) {
            std::cerr << "A locked value drag did not apply its pixel delta.\n";
            return false;
        }
        if (!state.session.undo()) return false;
        sync_shell_from_editor_session(&state);
        state.session.clear_history();
    }

    // A dominant horizontal drag opens the shared retime gesture and lands on a
    // frame boundary while Snap is on.
    {
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        const auto key = key_of("bone:1:Translate", 0U);
        if (!before.has_value() || !key.has_value()) return false;
        const auto projected = projected_key(&state, "bone:1:Translate", *key);
        if (!projected.has_value()) return false;
        const double press_x = press_x_of(projected->time_seconds);
        const double press_y = press_y_of(projected->values[0]);
        state.timeline_editor.snap_to_frames = true;
        state.timeline_editor.frames_per_second = 60.0;
        if (!arm_drag("bone:1:Translate", 0U, GraphComponent::X, 0U)) return false;
        // 82 px / 200 px per second = 0.41 s, which is not on the 60 fps grid.
        (void)update_timeline_graph_point_drag(
            &state, cached_timeline_tracks(&state),
            press_x + 82.0, press_y + 1.0, true, false, false);
        if (!state.timeline_editor.retime_gesture.has_value() ||
            state.timeline_editor.graph_value_gesture.has_value() ||
            state.timeline_editor.graph_drag->axis !=
                timeline_graph_model::DragAxis::Time) {
            std::cerr << "A dominant horizontal graph drag did not lock the time axis.\n";
            return false;
        }
        // A locked Time axis never edits a value, however far the pointer
        // travels vertically afterwards.
        const auto values_before_vertical =
            capture_graph_edit_snapshot(&state, "bone:1:Translate");
        (void)update_timeline_graph_point_drag(
            &state, cached_timeline_tracks(&state),
            press_x + 82.0, press_y - 300.0, true, false, false);
        if (state.timeline_editor.graph_value_gesture.has_value() ||
            state.timeline_editor.graph_drag->axis !=
                timeline_graph_model::DragAxis::Time ||
            !values_before_vertical.has_value()) {
            std::cerr << "Later vertical motion re-decided a locked time axis.\n";
            return false;
        }
        (void)update_timeline_graph_point_drag(
            &state, cached_timeline_tracks(&state),
            press_x + 82.0, press_y - 300.0, false, false, false);
        const auto after = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!after.has_value() || authoring_gesture_active(state) ||
            state.timeline_editor.graph_drag.has_value() ||
            after->graph_values != values_before_vertical->graph_values ||
            state.session.undo_count() != before->undo_count + 1U) {
            std::cerr << "A locked time drag changed a component value or history.\n";
            return false;
        }
        const double snapped_frames = after->dopesheet_key_times.front() * 60.0;
        if (std::abs(snapped_frames - std::round(snapped_frames)) > 1e-4 ||
            !near_value(after->dopesheet_key_times.front(), 25.0 / 60.0, 1e-4)) {
            std::cerr << "A snapped graph time drag did not land on a frame boundary: "
                      << after->dopesheet_key_times.front() << ".\n";
            return false;
        }
        // Undo of a committed time edit reconciles the moved selection away by
        // the shared dopesheet rule; MAR-168 deliberately does not change it.
        if (!state.session.undo()) return false;
        sync_shell_from_editor_session(&state);
        reconcile_timeline_key_selection(&state, cached_timeline_tracks(&state));
        if (!state.timeline_editor.selected_keys.empty() ||
            state.timeline_editor.active_key.has_value()) {
            std::cerr << "Undoing a graph time drag did not reconcile its moved selection.\n";
            return false;
        }
        state.session.clear_history();
    }

    // Alt bypasses frame snapping for the current drag.
    {
        const auto key = key_of("bone:1:Translate", 0U);
        if (!key.has_value()) return false;
        const auto projected = projected_key(&state, "bone:1:Translate", *key);
        if (!projected.has_value()) return false;
        const double press_x = press_x_of(projected->time_seconds);
        const double press_y = press_y_of(projected->values[0]);
        state.timeline_editor.snap_to_frames = true;
        if (!arm_drag("bone:1:Translate", 0U, GraphComponent::X, 0U)) return false;
        (void)update_timeline_graph_point_drag(
            &state, cached_timeline_tracks(&state),
            press_x + 82.0, press_y, true, false, true);
        (void)update_timeline_graph_point_drag(
            &state, cached_timeline_tracks(&state),
            press_x + 82.0, press_y, false, false, true);
        const auto after = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!after.has_value()) return false;
        const double raw_frames = after->dopesheet_key_times.front() * 60.0;
        if (!near_value(after->dopesheet_key_times.front(), 0.41, 1e-4) ||
            std::abs(raw_frames - std::round(raw_frames)) < 1e-3) {
            std::cerr << "The Alt bypass path landed on the frame grid: "
                      << after->dopesheet_key_times.front() << ".\n";
            return false;
        }
        if (!state.session.undo()) return false;
        sync_shell_from_editor_session(&state);
        state.session.clear_history();
    }

    // A time drag toward an unselected neighbour clamps and keeps the drag live.
    {
        const auto key = key_of("bone:1:Translate", 0U);
        if (!key.has_value()) return false;
        const auto projected = projected_key(&state, "bone:1:Translate", *key);
        if (!projected.has_value()) return false;
        const double press_x = press_x_of(projected->time_seconds);
        const double press_y = press_y_of(projected->values[0]);
        state.timeline_editor.snap_to_frames = false;
        if (!arm_drag("bone:1:Translate", 0U, GraphComponent::X, 0U)) return false;
        if (!update_timeline_graph_point_drag(
                &state, cached_timeline_tracks(&state),
                press_x + 180.0, press_y, true, false, false) ||
            !state.timeline_editor.retime_gesture.has_value() ||
            !state.timeline_editor.graph_drag.has_value()) {
            std::cerr << "A clamped graph time drag ended its gesture.\n";
            return false;
        }
        const auto clamped = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!clamped.has_value() ||
            !near_value(
                clamped->dopesheet_key_times.front(),
                clamped->dopesheet_key_times[1U] - timeline_model::kNonEventKeySpacing,
                1e-5)) {
            std::cerr << "A graph time drag did not clamp at the 1 ms neighbour spacing.\n";
            return false;
        }
        (void)update_timeline_graph_point_drag(
            &state, cached_timeline_tracks(&state),
            press_x + 180.0, press_y, false, false, false);
        if (!state.session.undo()) return false;
        sync_shell_from_editor_session(&state);
        state.session.clear_history();
        state.timeline_editor.snap_to_frames = true;
    }

    // Explicit-duration auto-grow happens inside the same transaction and the
    // same undo entry as the drag that caused it.
    {
        const auto* animation = state.session.runtime_data()->find_animation("idle");
        if (animation == nullptr) return false;
        const double inferred_duration = animation->duration();
        {
            auto transaction = state.session.begin_edit({
                marrow::editor::EditKind::EditProperty,
                "Graph auto-grow smoke duration",
                {},
                false,
                marrow::editor::EditImpact::Project |
                    marrow::editor::EditImpact::Runtime |
                    marrow::editor::EditImpact::Preview});
            const auto authored = marrow::editor::set_animation_duration(
                transaction.project(), *state.session.runtime_data(), "idle",
                inferred_duration);
            if (!authored || !transaction.commit()) {
                std::cerr << "Graph auto-grow smoke could not author an explicit duration.\n";
                return false;
            }
        }
        sync_shell_from_editor_session(&state);
        const std::size_t undo_before = state.session.undo_count();
        const TimelineTrackRow* translate = row_of("bone:1:Translate");
        if (translate == nullptr || translate->key_times.size() < 3U) return false;
        const std::size_t last_index = translate->key_times.size() - 1U;
        const auto key = key_of("bone:1:Translate", last_index);
        if (!key.has_value()) return false;
        const auto projected = projected_key(&state, "bone:1:Translate", *key);
        if (!projected.has_value()) return false;
        const double press_x = press_x_of(projected->time_seconds);
        const double press_y = press_y_of(projected->values[0]);
        state.timeline_editor.snap_to_frames = false;
        if (!arm_drag("bone:1:Translate", last_index, GraphComponent::X, 0U)) return false;
        (void)update_timeline_graph_point_drag(
            &state, cached_timeline_tracks(&state),
            press_x + 40.0, press_y, true, false, false);
        (void)update_timeline_graph_point_drag(
            &state, cached_timeline_tracks(&state),
            press_x + 40.0, press_y, false, false, false);
        const auto* grown = state.session.runtime_data()->find_animation("idle");
        if (grown == nullptr || state.session.undo_count() != undo_before + 1U ||
            !near_value(grown->duration(), inferred_duration + 0.2, 1e-4)) {
            std::cerr << "A graph time drag past an explicit duration did not auto-grow it in one entry.\n";
            return false;
        }
        if (!state.session.undo()) return false;
        sync_shell_from_editor_session(&state);
        const auto* restored = state.session.runtime_data()->find_animation("idle");
        if (restored == nullptr || !near_value(restored->duration(), inferred_duration, 1e-4)) {
            std::cerr << "Undoing a graph time drag did not undo its duration growth.\n";
            return false;
        }
        if (!state.session.undo()) return false;
        sync_shell_from_editor_session(&state);
        state.session.clear_history();
        state.timeline_editor.snap_to_frames = true;
    }

    // Cancel paths on both axes roll back to byte-identical state.
    {
        const auto key = key_of("bone:1:Translate", 0U);
        if (!key.has_value()) return false;
        const auto projected = projected_key(&state, "bone:1:Translate", *key);
        if (!projected.has_value()) return false;
        const double press_x = press_x_of(projected->time_seconds);
        const double press_y = press_y_of(projected->values[0]);

        const auto before_value = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!before_value.has_value() ||
            !arm_drag("bone:1:Translate", 0U, GraphComponent::X, 0U)) {
            return false;
        }
        (void)update_timeline_graph_point_drag(
            &state, cached_timeline_tracks(&state),
            press_x, press_y - 60.0, true, false, false);
        if (update_timeline_graph_point_drag(
                &state, cached_timeline_tracks(&state),
                press_x, press_y - 60.0, true, true, false) ||
            state.timeline_editor.graph_drag.has_value() ||
            authoring_gesture_active(state)) {
            std::cerr << "An Escape on a graph value drag did not end the gesture.\n";
            return false;
        }
        const auto after_value = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!after_value.has_value() ||
            !graph_edit_snapshots_match(*before_value, *after_value)) {
            std::cerr << "A cancelled graph value drag did not roll back atomically.\n";
            return false;
        }

        const auto before_time = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!before_time.has_value() ||
            !arm_drag("bone:1:Translate", 0U, GraphComponent::X, 0U)) {
            return false;
        }
        (void)update_timeline_graph_point_drag(
            &state, cached_timeline_tracks(&state),
            press_x + 60.0, press_y, true, false, false);
        cancel_timeline_graph_point_drag(&state);
        const auto after_time = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (state.timeline_editor.graph_drag.has_value() ||
            authoring_gesture_active(state) || !after_time.has_value() ||
            !graph_edit_snapshots_match(*before_time, *after_time)) {
            std::cerr << "A cancelled graph time drag did not roll back atomically.\n";
            return false;
        }
    }

    // Fail-closed press contracts.
    {
        const auto attachment_key = key_of("slot:0:Attachment", 0U);
        const TimelineTrackRow* attachment = row_of("slot:0:Attachment");
        if (attachment_key.has_value() && attachment != nullptr) {
            state.timeline_editor.selected_keys = {*attachment_key};
            state.timeline_editor.active_key = *attachment_key;
            const timeline_graph_model::PointHit hit{
                *attachment_key, GraphComponent::X, 0U};
            if (begin_timeline_graph_point_drag(
                    &state, *attachment, hit, 4242U, drag_plot, drag_view, 10.0, 10.0) ||
                state.timeline_editor.graph_drag.has_value()) {
                std::cerr << "An unsupported graph row armed a drag candidate.\n";
                return false;
            }
        }
        const auto key = key_of("bone:1:Translate", 0U);
        const TimelineTrackRow* translate = row_of("bone:1:Translate");
        if (!key.has_value() || translate == nullptr) return false;
        // A key that activation left unselected must not arm a drag.
        state.timeline_editor.selected_keys.clear();
        state.timeline_editor.active_key.reset();
        const timeline_graph_model::PointHit hit{*key, GraphComponent::X, 0U};
        if (begin_timeline_graph_point_drag(
                &state, *translate, hit, 4242U, drag_plot, drag_view, 0.0, 160.0) ||
            state.timeline_editor.graph_drag.has_value()) {
            std::cerr << "An unselected graph key armed a drag candidate.\n";
            return false;
        }
        // Another live authoring gesture blocks the press entirely.
        state.timeline_editor.selected_keys = {*key};
        state.timeline_editor.active_key = *key;
        if (!begin_timeline_graph_value_gesture(
                &state, 9999U, *row_of("bone:1:Translate"), GraphComponent::X,
                cached_timeline_tracks(&state))) {
            std::cerr << "Graph press exclusivity smoke could not stage its gesture.\n";
            return false;
        }
        if (begin_timeline_graph_point_drag(
                &state, *row_of("bone:1:Translate"), hit, 4242U, drag_plot,
                drag_view, 0.0, 160.0) ||
            state.timeline_editor.graph_drag.has_value()) {
            std::cerr << "A press during a live authoring gesture armed a candidate.\n";
            return false;
        }
        finish_timeline_graph_value_gesture(&state, false);
        // Non-finite pointer input never arms a candidate.
        if (begin_timeline_graph_point_drag(
                &state, *row_of("bone:1:Translate"), hit, 4242U, drag_plot,
                drag_view, std::numeric_limits<double>::quiet_NaN(), 160.0) ||
            state.timeline_editor.graph_drag.has_value()) {
            std::cerr << "A non-finite graph press armed a drag candidate.\n";
            return false;
        }
    }

    // TimelineEditorState{} source adoption clears both drag slots atomically.
    {
        const auto key = key_of("bone:1:Translate", 0U);
        const TimelineTrackRow* translate = row_of("bone:1:Translate");
        if (!key.has_value() || translate == nullptr) return false;
        state.timeline_editor.selected_keys = {*key};
        state.timeline_editor.active_key = *key;
        const timeline_graph_model::PointHit hit{*key, GraphComponent::X, 0U};
        if (!begin_timeline_graph_point_drag(
                &state, *translate, hit, 4242U, drag_plot, drag_view, 0.0, 160.0) ||
            !state.timeline_editor.graph_drag.has_value()) {
            std::cerr << "Graph source-adoption smoke could not arm its candidate.\n";
            return false;
        }
        if (!reload_project(&state)) {
            std::cerr << "Graph source-adoption smoke could not reload its project.\n";
            return false;
        }
        if (state.timeline_editor.graph_drag.has_value() ||
            state.timeline_editor.graph_value_gesture.has_value() ||
            authoring_gesture_active(state)) {
            std::cerr << "TimelineEditorState source adoption did not clear the graph drag slots.\n";
            return false;
        }
    }

    if (marrow::editor::agent_operation_descriptor_count() != operation_count_before) {
        std::cerr << "Graph value editing changed the Agent operation surface.\n";
        return false;
    }
    return true;
}

} // namespace marrow::editor::shell
