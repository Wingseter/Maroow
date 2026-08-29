#include "shell_timeline_graph.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>

#include "shell_derived_cache.hpp"
#include "shell_preferences.hpp"
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
    if (operation_count_before != 57U) {
        std::cerr << "Graph shell smoke requires the exact 57-operation registry.\n";
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
    // MAR-169: an easing rollback must restore the curve as exactly as it
    // restores the values, so the snapshot carries both.
    std::vector<int> graph_segment_kinds;
    std::vector<double> graph_control_points;
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
        snapshot.graph_segment_kinds.push_back(
            static_cast<int>(key.outgoing_easing.kind()));
        // Only a cubic easing carries control points; reading them for a
        // Linear or Stepped key would look past the documented kind guard.
        if (key.outgoing_easing.kind() ==
            marrow::runtime::InterpolationKind::CubicBezier) {
            const auto& points = key.outgoing_easing.cubic_bezier();
            snapshot.graph_control_points.push_back(static_cast<double>(points.cx1));
            snapshot.graph_control_points.push_back(static_cast<double>(points.cy1));
            snapshot.graph_control_points.push_back(static_cast<double>(points.cx2));
            snapshot.graph_control_points.push_back(static_cast<double>(points.cy2));
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
        left.graph_values == right.graph_values &&
        left.graph_segment_kinds == right.graph_segment_kinds &&
        left.graph_control_points == right.graph_control_points;
}

struct ProjectedKey {
    double time_seconds{0.0};
    std::array<double, 4> values{};
    std::size_t value_count{0U};
    marrow::runtime::InterpolationKind easing{
        marrow::runtime::InterpolationKind::Linear};
    std::array<double, 4> control_points{};
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
        if (result.easing == marrow::runtime::InterpolationKind::CubicBezier) {
            const auto& points = candidate.outgoing_easing.cubic_bezier();
            result.control_points = {
                static_cast<double>(points.cx1),
                static_cast<double>(points.cy1),
                static_cast<double>(points.cx2),
                static_cast<double>(points.cy2)};
        }
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
    if (operation_count_before != 57U) {
        std::cerr << "Graph edit shell smoke requires the exact 57-operation registry.\n";
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
        state.status_message = "graph value sentinel";
        finish_timeline_graph_value_gesture(&state, true);
        const auto after = capture_graph_edit_snapshot(&state, "bone:1:Rotate");
        if (authoring_gesture_active(state) || !after.has_value() ||
            !graph_edit_snapshots_match(*before, *after) ||
            state.status_message != "graph value sentinel") {
            std::cerr << "A zero-net graph value drag committed a history entry or "
                         "reported an edit it did not make: status=\""
                      << state.status_message << "\".\n";
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

bool validate_timeline_curve_preset_shell_smoke(
    const std::filesystem::path& project_path) {
    using marrow::editor::CurvePreset;
    using marrow::runtime::AnimationScalar;
    using marrow::runtime::InterpolationKind;

    // Its own isolated config home, so this scenario can exercise the save path
    // without seeing — or leaving behind — any other scenario's settings.
    const ScopedPreferenceIsolation isolation("curve-preset");
    if (!isolation.installed()) {
        std::cerr << "Curve preset shell smoke could not isolate MARROW_CONFIG_HOME.\n";
        return false;
    }

    ShellState state;
    state.project_path = project_path;
    load_shell_preferences(&state);
    if (!reload_project(&state) ||
        !set_selected_animation(&state, "idle", "Curve preset smoke", false, true)) {
        std::cerr << "Curve preset shell smoke could not load player_idle/idle.\n";
        return false;
    }
    const std::size_t operation_count_before =
        marrow::editor::agent_operation_descriptor_count();
    if (operation_count_before != 57U) {
        std::cerr << "Curve preset shell smoke requires the exact 57-operation registry.\n";
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
    const auto stored_easing = [&](std::string_view track_id, const TimelineKeyRef& key)
        -> std::optional<marrow::runtime::Interpolation> {
        const auto projected = projected_key(&state, track_id, key);
        if (!projected.has_value()) return std::nullopt;
        const TimelineTrackRow* row = row_of(track_id);
        if (row == nullptr) return std::nullopt;
        const auto& projection = cached_timeline_graph_projection(&state, *row);
        if (projection.status != GraphProjectionStatus::Ready ||
            !projection.track.has_value()) {
            return std::nullopt;
        }
        for (const auto& candidate : projection.track->keys) {
            if (candidate.identity == key) return candidate.outgoing_easing;
        }
        return std::nullopt;
    };
    const auto is_preset = [](const marrow::runtime::Interpolation& easing,
                              const std::array<double, 4>& expected) {
        if (easing.kind() != InterpolationKind::CubicBezier) return false;
        const auto& points = easing.cubic_bezier();
        return points.cx1 == static_cast<AnimationScalar>(expected[0]) &&
            points.cy1 == static_cast<AnimationScalar>(expected[1]) &&
            points.cx2 == static_cast<AnimationScalar>(expected[2]) &&
            points.cy2 == static_cast<AnimationScalar>(expected[3]);
    };
    constexpr std::array<double, 4> kEase{0.25, 0.1, 0.25, 1.0};
    constexpr std::array<double, 4> kEaseIn{0.42, 0.0, 1.0, 1.0};
    constexpr std::array<double, 4> kEaseOut{0.0, 0.0, 0.58, 1.0};
    constexpr std::array<double, 4> kEaseInOut{0.42, 0.0, 0.58, 1.0};

    // --- Three selected Transform keys, one preset, one history entry, and a
    // duplicate ref collapsed rather than turned into a hard error. ---
    {
        const auto first_key = key_of("bone:1:Translate", 0U);
        const auto second_key = key_of("bone:1:Translate", 1U);
        const auto third_key = key_of("bone:1:Translate", 2U);
        if (!first_key.has_value() || !second_key.has_value() ||
            !third_key.has_value()) {
            std::cerr << "Curve preset smoke requires three spine Translate keys.\n";
            return false;
        }
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        const auto before_first =
            projected_key(&state, "bone:1:Translate", *first_key);
        if (!before.has_value() || !before_first.has_value()) {
            std::cerr << "Curve preset smoke could not capture its baseline.\n";
            return false;
        }
        state.selected_timeline_track_id = std::string("bone:1:Translate");
        state.timeline_editor.selected_keys = {
            *first_key, *second_key, *third_key, *first_key};
        state.timeline_editor.active_key = *first_key;

        const auto result = apply_timeline_curve_preset(
            &state, cached_timeline_tracks(&state), CurvePreset::EaseInOut);
        if (!result.applied || result.changed_key_count != 3U ||
            result.compatible_key_count != 3U || result.skipped_key_count != 1U ||
            !result.error.empty()) {
            std::cerr << "Applying Ease-In-Out to three Transform keys failed: "
                      << result.error << " applied=" << result.applied
                      << " changed=" << result.changed_key_count
                      << " compatible=" << result.compatible_key_count << '\n';
            return false;
        }
        if (state.session.undo_count() != before->undo_count + 1U) {
            std::cerr << "One preset application must add exactly one history entry.\n";
            return false;
        }
        for (const auto& key : {*first_key, *second_key, *third_key}) {
            const auto easing = stored_easing("bone:1:Translate", key);
            if (!easing.has_value() || !is_preset(*easing, kEaseInOut)) {
                std::cerr << "A preset did not store its fixed control points in every "
                             "selected key.\n";
                return false;
            }
        }
        const auto after_first = projected_key(&state, "bone:1:Translate", *first_key);
        if (!after_first.has_value() ||
            after_first->time_seconds != before_first->time_seconds ||
            after_first->values != before_first->values) {
            std::cerr << "A preset moved a key in time or changed a scalar.\n";
            return false;
        }
    }

    // --- The remembered default seeds a newly authored Transform key. ---
    {
        state.preferences.default_curve = CurvePreset::EaseOut;
        const TimelineTrackRow* rotate = row_of("bone:2:Rotate");
        if (rotate == nullptr) {
            std::cerr << "Curve preset smoke requires an arm_l Rotate track.\n";
            return false;
        }
        if (!scrub_timeline_time(&state, 0.75, "Curve preset smoke", false)) {
            std::cerr << "Curve preset smoke could not scrub the playhead.\n";
            return false;
        }
        if (!add_timeline_key_at_playhead(&state, *rotate)) {
            std::cerr << "Curve preset smoke could not add a Transform key.\n";
            return false;
        }
        const auto added = state.timeline_editor.active_key;
        const auto easing =
            added.has_value() ? stored_easing("bone:2:Rotate", *added) : std::nullopt;
        if (!easing.has_value() || !is_preset(*easing, kEaseOut)) {
            std::cerr << "A newly authored Transform key did not take the remembered "
                         "default curve.\n";
            return false;
        }
    }


    // --- A mixed selection: the Event key is skipped and counted, the two
    // compatible keys are written, and it is still one history entry. ---
    {
        const auto translate_key = key_of("bone:1:Translate", 0U);
        const auto color_key = key_of("slot:0:Color", 0U);
        const auto event_key = key_of("global:events", 0U);
        if (!translate_key.has_value() || !color_key.has_value() ||
            !event_key.has_value()) {
            std::cerr << "Curve preset smoke requires Translate, Color, and Event keys.\n";
            return false;
        }
        const std::size_t undo_before = state.session.undo_count();
        state.selected_timeline_track_id = std::string("bone:1:Translate");
        state.timeline_editor.selected_keys = {*translate_key, *event_key, *color_key};
        state.timeline_editor.active_key = *translate_key;
        const auto result = apply_timeline_curve_preset(
            &state, cached_timeline_tracks(&state), CurvePreset::Ease);
        if (!result.applied || result.compatible_key_count != 2U ||
            result.skipped_key_count != 1U || result.changed_key_count != 2U ||
            state.session.undo_count() != undo_before + 1U) {
            std::cerr << "A mixed selection did not skip exactly one easing-free key: "
                      << "applied=" << result.applied
                      << " compatible=" << result.compatible_key_count
                      << " skipped=" << result.skipped_key_count
                      << " changed=" << result.changed_key_count << ".\n";
            return false;
        }
        if (state.status_message.find("2 have no easing") != std::string::npos ||
            state.status_message.find("1 have no easing") == std::string::npos) {
            std::cerr << "A partial preset application did not report the skipped count: "
                      << state.status_message << '\n';
            return false;
        }
    }

    // --- Re-applying the same preset commits nothing. ---
    {
        const auto translate_key = key_of("bone:1:Translate", 0U);
        if (!translate_key.has_value()) return false;
        state.timeline_editor.selected_keys = {*translate_key};
        state.timeline_editor.active_key = *translate_key;
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!before.has_value()) return false;
        const auto repeat = apply_timeline_curve_preset(
            &state, cached_timeline_tracks(&state), CurvePreset::Ease);
        const auto after = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (repeat.applied || repeat.changed_key_count != 0U || !after.has_value() ||
            !graph_edit_snapshots_match(*before, *after)) {
            std::cerr << "Re-applying a preset a key already carries changed the "
                         "project or the history.\n";
            return false;
        }
        if (state.status_message != "Selected keys already use Ease") {
            std::cerr << "Re-applying a preset did not report the already-set message: "
                      << state.status_message << '\n';
            return false;
        }
    }

    // --- An Event-only selection opens no transaction. ---
    {
        const auto event_key = key_of("global:events", 0U);
        if (!event_key.has_value()) return false;
        state.timeline_editor.selected_keys = {*event_key};
        state.timeline_editor.active_key = *event_key;
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!before.has_value()) return false;
        const auto result = apply_timeline_curve_preset(
            &state, cached_timeline_tracks(&state), CurvePreset::EaseIn);
        const auto after = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (result.applied || result.compatible_key_count != 0U || !after.has_value() ||
            !graph_edit_snapshots_match(*before, *after) ||
            state.status_message !=
                "Select one or more Transform, Deform, or Slot Color keys") {
            std::cerr << "An easing-free selection did not fail closed without a "
                         "transaction: " << state.status_message << '\n';
            return false;
        }
    }

    // --- Criterion 5: the easing is segment-wide. Displaying component Y and
    // applying a preset changes the X segment identically, because the
    // primitive has no component parameter. ---
    {
        const auto translate_key = key_of("bone:1:Translate", 1U);
        if (!translate_key.has_value()) return false;
        state.selected_timeline_track_id = std::string("bone:1:Translate");
        state.timeline_editor.selected_keys = {*translate_key};
        state.timeline_editor.active_key = *translate_key;
        state.timeline_editor.graph_view.active_component = GraphComponent::Y;
        const auto before_y = projected_key(&state, "bone:1:Translate", *translate_key);
        if (!before_y.has_value()) return false;
        const auto result = apply_timeline_curve_preset(
            &state, cached_timeline_tracks(&state), CurvePreset::EaseIn);
        if (!result.applied || result.changed_key_count != 1U) {
            std::cerr << "A preset applied while displaying Y did not write.\n";
            return false;
        }
        state.timeline_editor.graph_view.active_component = GraphComponent::X;
        const auto after_x = projected_key(&state, "bone:1:Translate", *translate_key);
        const auto easing = stored_easing("bone:1:Translate", *translate_key);
        if (!after_x.has_value() || !easing.has_value() ||
            !is_preset(*easing, kEaseIn) ||
            after_x->values[0] != before_y->values[0] ||
            after_x->values[1] != before_y->values[1] ||
            after_x->time_seconds != before_y->time_seconds) {
            std::cerr << "A preset applied while displaying Y did not change the X "
                         "segment identically.\n";
            return false;
        }
    }

    // --- Undo then redo restores the curve with a bit-identical selection. ---
    {
        const auto translate_key = key_of("bone:1:Translate", 0U);
        if (!translate_key.has_value()) return false;
        state.selected_timeline_track_id = std::string("bone:1:Translate");
        state.timeline_editor.selected_keys = {*translate_key};
        state.timeline_editor.active_key = *translate_key;
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!before.has_value()) return false;
        const auto result = apply_timeline_curve_preset(
            &state, cached_timeline_tracks(&state), CurvePreset::EaseOut);
        if (!result.applied) return false;
        if (!state.session.undo()) return false;
        sync_shell_from_editor_session(&state);
        reconcile_timeline_key_selection(&state, cached_timeline_tracks(&state));
        const auto undone = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!undone.has_value() || undone->project != before->project ||
            undone->graph_segment_kinds != before->graph_segment_kinds ||
            undone->graph_control_points != before->graph_control_points ||
            undone->dopesheet_key_times != before->dopesheet_key_times ||
            state.timeline_editor.selected_keys !=
                std::vector<TimelineKeyRef>{*translate_key} ||
            !(state.timeline_editor.active_key ==
              std::optional<TimelineKeyRef>(*translate_key))) {
            std::cerr << "Undoing a preset did not restore the curve and the selection.\n";
            return false;
        }
        if (!state.session.redo()) return false;
        sync_shell_from_editor_session(&state);
        reconcile_timeline_key_selection(&state, cached_timeline_tracks(&state));
        const auto redone = stored_easing("bone:1:Translate", *translate_key);
        if (!redone.has_value() || !is_preset(*redone, kEaseOut) ||
            state.timeline_editor.selected_keys !=
                std::vector<TimelineKeyRef>{*translate_key} ||
            !(state.timeline_editor.active_key ==
              std::optional<TimelineKeyRef>(*translate_key))) {
            std::cerr << "Redoing a preset did not restore the curve and the selection.\n";
            return false;
        }
    }

    // --- A preset followed by a MAR-169 handle drag is exactly two history
    // entries, and the readout goes Custom the moment the drag moves. ---
    {
        constexpr timeline_graph_model::PlotRect plot{0.0, 0.0, 640.0, 320.0};
        const timeline_graph_model::View view{0.0, 200.0, 0.0, 10.0};
        state.timeline_editor.graph_view.view = view;
        state.timeline_editor.graph_view.needs_fit = false;
        const auto translate_key = key_of("bone:1:Translate", 0U);
        if (!translate_key.has_value()) return false;
        state.selected_timeline_track_id = std::string("bone:1:Translate");
        state.timeline_editor.selected_keys = {*translate_key};
        state.timeline_editor.active_key = *translate_key;
        state.timeline_editor.graph_view.active_component = GraphComponent::Y;

        const std::size_t undo_before = state.session.undo_count();
        const auto applied = apply_timeline_curve_preset(
            &state, cached_timeline_tracks(&state), CurvePreset::Ease);
        if (!applied.applied) {
            std::cerr << "The preset-then-drag case could not apply Ease.\n";
            return false;
        }
        if (active_outgoing_curve_preset(state, cached_timeline_tracks(&state)) !=
            std::optional<CurvePreset>(CurvePreset::Ease)) {
            std::cerr << "The current-preset readout did not report a just-applied "
                         "preset.\n";
            return false;
        }

        const TimelineTrackRow* translate = row_of("bone:1:Translate");
        if (translate == nullptr) {
            std::cerr << "The preset-then-drag case lost its Translate row.\n";
            return false;
        }
        const auto& projection = cached_timeline_graph_projection(&state, *translate);
        if (projection.status != GraphProjectionStatus::Ready ||
            !projection.track.has_value()) {
            std::cerr << "The preset-then-drag case lost its Translate projection.\n";
            return false;
        }
        const auto handles = timeline_graph_model::build_handle_geometry(
            *projection.track, *translate_key, 1U, view, plot);
        if (!handles.has_value() ||
            handles->kind != timeline_graph_model::SegmentKind::Cubic) {
            std::cerr << "A preset-applied segment did not expose cubic handles.\n";
            return false;
        }
        std::array<double, 4> dragged = handles->control_points;
        dragged[1] += 0.25;
        if (!begin_timeline_graph_handle_gesture(
                &state, 5151U, *translate, *translate_key, handles->frame,
                handles->control_points, InterpolationKind::CubicBezier,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_handle_control_points(
                &state, cached_timeline_tracks(&state), dragged)) {
            std::cerr << "The preset-then-drag case could not run a handle drag.\n";
            return false;
        }
        finish_timeline_graph_handle_gesture(&state, true);
        if (state.session.undo_count() != undo_before + 2U) {
            std::cerr << "A preset followed by a handle drag was not two history "
                         "entries: " << state.session.undo_count() << "/"
                      << undo_before << ".\n";
            return false;
        }
        if (active_outgoing_curve_preset(state, cached_timeline_tracks(&state))
                .has_value()) {
            std::cerr << "A curve dragged away from a preset did not read as Custom.\n";
            return false;
        }
        if (!state.session.undo()) return false;
        sync_shell_from_editor_session(&state);
        reconcile_timeline_key_selection(&state, cached_timeline_tracks(&state));
        const auto restored = stored_easing("bone:1:Translate", *translate_key);
        if (!restored.has_value() || !is_preset(*restored, kEase) ||
            active_outgoing_curve_preset(state, cached_timeline_tracks(&state)) !=
                std::optional<CurvePreset>(CurvePreset::Ease)) {
            std::cerr << "One undo after a handle drag did not restore the preset.\n";
            return false;
        }
    }

    // --- The remembered default seeds new Deform and Slot Color keys too, and
    // a Stepped default seeds a Stepped key. ---
    {
        state.preferences.default_curve = CurvePreset::EaseOut;
        const auto* skeleton = state.session.runtime_data();
        if (skeleton == nullptr) return false;

        const TimelineTrackRow* color = row_of("slot:0:Color");
        const TimelineTrackRow* deform = row_of("slot:0:deform:body_mesh");
        if (color == nullptr || deform == nullptr) {
            std::cerr << "Curve preset smoke requires Slot Color and Deform rows.\n";
            return false;
        }
        if (!scrub_timeline_time(&state, 0.8, "Curve preset smoke", false) ||
            !add_timeline_key_at_playhead(&state, *color) ||
            !add_timeline_key_at_playhead(&state, *deform)) {
            std::cerr << "Curve preset smoke could not add Color and Deform keys.\n";
            return false;
        }
        const auto seeded_easing_at = [&](bool deform_family, double time)
            -> std::optional<marrow::runtime::Interpolation> {
            const auto* animation = state.session.runtime_data()->find_animation("idle");
            if (animation == nullptr) return std::nullopt;
            if (deform_family) {
                for (const auto& timeline : animation->mesh_deform_timelines) {
                    if (timeline.slot_index != 0U ||
                        timeline.attachment_name != "body_mesh") {
                        continue;
                    }
                    for (const auto& keyframe : timeline.keyframes) {
                        if (std::abs(static_cast<double>(keyframe.time) - time) <= 1e-6) {
                            return keyframe.interpolation;
                        }
                    }
                }
                return std::nullopt;
            }
            for (const auto& timeline : animation->slot_color_timelines) {
                if (timeline.slot_index != 0U) continue;
                for (const auto& keyframe : timeline.keyframes) {
                    if (std::abs(static_cast<double>(keyframe.time) - time) <= 1e-6) {
                        return keyframe.interpolation;
                    }
                }
            }
            return std::nullopt;
        };
        const auto color_easing = seeded_easing_at(false, 0.8);
        const auto deform_easing = seeded_easing_at(true, 0.8);
        if (!color_easing.has_value() || !deform_easing.has_value() ||
            !is_preset(*color_easing, kEaseOut) || !is_preset(*deform_easing, kEaseOut)) {
            std::cerr << "New Slot Color and Deform keys did not take the remembered "
                         "default curve.\n";
            return false;
        }

        state.preferences.default_curve = CurvePreset::Stepped;
        const TimelineTrackRow* rotate = row_of("bone:2:Rotate");
        if (rotate == nullptr || !scrub_timeline_time(&state, 0.9, "Curve preset smoke", false) ||
            !add_timeline_key_at_playhead(&state, *rotate)) {
            std::cerr << "Curve preset smoke could not add a Stepped-default key.\n";
            return false;
        }
        const auto added = state.timeline_editor.active_key;
        const auto stepped = added.has_value()
            ? stored_easing("bone:2:Rotate", *added)
            : std::nullopt;
        if (!stepped.has_value() || stepped->kind() != InterpolationKind::Stepped) {
            std::cerr << "A Stepped default did not seed a Stepped new key.\n";
            return false;
        }
    }

    // --- Paste reproduces a key; it is never reseeded with the default. ---
    {
        const auto translate_key = key_of("bone:1:Translate", 0U);
        if (!translate_key.has_value()) return false;
        state.selected_timeline_track_id = std::string("bone:1:Translate");
        state.timeline_editor.selected_keys = {*translate_key};
        state.timeline_editor.active_key = *translate_key;
        state.preferences.default_curve = CurvePreset::EaseIn;
        if (!apply_timeline_curve_preset(
                 &state, cached_timeline_tracks(&state), CurvePreset::EaseIn)
                 .applied) {
            std::cerr << "The paste case could not author its Ease-In source key.\n";
            return false;
        }
        if (!copy_selected_timeline_keys(&state, cached_timeline_tracks(&state))) {
            std::cerr << "The paste case could not copy its Ease-In key.\n";
            return false;
        }
        state.preferences.default_curve = CurvePreset::Stepped;
        if (!scrub_timeline_time(&state, 0.95, "Curve preset smoke", false) ||
            !paste_timeline_clipboard(&state, cached_timeline_tracks(&state))) {
            std::cerr << "The paste case could not paste its clipboard.\n";
            return false;
        }
        const TimelineTrackRow* pasted_row = row_of("bone:1:Translate");
        std::optional<TimelineKeyRef> pasted;
        if (pasted_row != nullptr) {
            for (std::size_t index = 0U; index < pasted_row->key_times.size(); ++index) {
                if (std::abs(pasted_row->key_times[index] - 0.95) <= 1e-6) {
                    pasted = timeline_key_ref(*pasted_row, index);
                    break;
                }
            }
        }
        const auto pasted_easing = pasted.has_value()
            ? stored_easing("bone:1:Translate", *pasted)
            : std::optional<marrow::runtime::Interpolation>{};
        if (!pasted_easing.has_value() || !is_preset(*pasted_easing, kEaseIn)) {
            std::cerr << "A pasted key was reseeded with the remembered default "
                         "instead of keeping the copied curve.\n";
            return false;
        }
    }

    // --- Changing and storing the default never touches the project. ---
    {
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!before.has_value()) return false;
        const std::filesystem::path settings_path = isolation.settings_path();
        if (std::filesystem::exists(settings_path)) {
            std::cerr << "A load-only preference session must create no settings file.\n";
            return false;
        }
        if (!set_shell_default_curve(&state, CurvePreset::EaseInOut)) {
            std::cerr << "Storing a new default curve failed: " << state.error_message
                      << '\n';
            return false;
        }
        const auto after = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!after.has_value() || !graph_edit_snapshots_match(*before, *after)) {
            std::cerr << "Changing the default curve dirtied the project.\n";
            return false;
        }
        if (!std::filesystem::exists(settings_path)) {
            std::cerr << "An explicit default change did not write the settings file.\n";
            return false;
        }
        const auto parsed = marrow::runtime::json::load_document(settings_path);
        const marrow::runtime::json::Value* token =
            parsed ? marrow::runtime::json::find_member(
                         parsed.document->root, "default_curve")
                   : nullptr;
        if (!parsed || token == nullptr || !token->is_string() ||
            token->as_string() != "ease_in_out") {
            std::cerr << "The stored settings file did not carry the ease_in_out token.\n";
            return false;
        }
        // Re-selecting the value already stored performs no write at all.
        const auto written_at =
            std::filesystem::last_write_time(settings_path);
        if (!set_shell_default_curve(&state, CurvePreset::EaseInOut) ||
            std::filesystem::last_write_time(settings_path) != written_at) {
            std::cerr << "Re-selecting the stored default curve rewrote the file.\n";
            return false;
        }
    }

    // --- A malformed settings file falls back to Linear and rewrites nothing. ---
    {
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!before.has_value()) return false;
        const std::filesystem::path settings_path = isolation.settings_path();
        const std::string malformed = "{ not json at all";
        {
            std::ofstream output(settings_path, std::ios::binary | std::ios::trunc);
            output << malformed;
        }
        load_shell_preferences(&state);
        if (state.preferences.default_curve != CurvePreset::Linear ||
            state.preference_status !=
                marrow::editor::PreferenceLoadStatus::Malformed) {
            std::cerr << "A malformed settings file did not fall back to Linear.\n";
            return false;
        }
        std::ifstream input(settings_path, std::ios::binary);
        const std::string reread(
            (std::istreambuf_iterator<char>(input)),
            std::istreambuf_iterator<char>());
        const auto after = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (reread != malformed || !after.has_value() ||
            !graph_edit_snapshots_match(*before, *after)) {
            std::cerr << "Loading a malformed settings file repaired it or changed a "
                         "keyframe.\n";
            return false;
        }
    }

    if (marrow::editor::agent_operation_descriptor_count() != operation_count_before) {
        std::cerr << "Curve preset application changed the Agent operation surface.\n";
        return false;
    }
    return true;
}

bool validate_timeline_graph_easing_shell_smoke(
    const std::filesystem::path& project_path) {
    using marrow::runtime::InterpolationKind;

    ShellState state;
    state.project_path = project_path;
    if (!reload_project(&state) ||
        !set_selected_animation(&state, "idle", "Graph easing smoke", false, true)) {
        std::cerr << "Graph easing shell smoke could not load player_idle/idle.\n";
        return false;
    }
    const std::size_t operation_count_before =
        marrow::editor::agent_operation_descriptor_count();
    if (operation_count_before != 57U) {
        std::cerr << "Graph easing shell smoke requires the exact 57-operation registry.\n";
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

    constexpr timeline_graph_model::PlotRect plot{0.0, 0.0, 640.0, 320.0};
    const timeline_graph_model::View view{0.0, 200.0, 0.0, 10.0};
    state.timeline_editor.graph_view.view = view;
    state.timeline_editor.graph_view.needs_fit = false;

    const auto handles_for = [&](std::string_view track_id,
                                 const TimelineKeyRef& key,
                                 std::size_t component_index)
        -> std::optional<timeline_graph_model::HandleGeometry> {
        const TimelineTrackRow* row = row_of(track_id);
        if (row == nullptr) return std::nullopt;
        const auto& projection = cached_timeline_graph_projection(&state, *row);
        if (projection.status != GraphProjectionStatus::Ready ||
            !projection.track.has_value()) {
            return std::nullopt;
        }
        return timeline_graph_model::build_handle_geometry(
            *projection.track, key, component_index, view, plot);
    };

    const auto near_points = [](const std::array<double, 4>& left,
                                const std::array<double, 4>& right) {
        for (std::size_t index = 0U; index < 4U; ++index) {
            if (std::abs(left[index] - right[index]) > 1e-6) return false;
        }
        return true;
    };

    constexpr std::array<double, 4> kOvershoot{0.2, -0.4, 0.8, 1.6};

    // --- One handle gesture on the runtime-only spine Translate track:
    // exactly one history entry, the parent key's time/x/y byte-identical, and
    // the one shared curve re-projected identically for X and Y. ---
    {
        const auto first_key = key_of("bone:1:Translate", 0U);
        if (!first_key.has_value()) {
            std::cerr << "Graph easing smoke requires a spine Translate key.\n";
            return false;
        }
        const auto before_key = projected_key(&state, "bone:1:Translate", *first_key);
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!before_key.has_value() || !before.has_value()) {
            std::cerr << "Graph easing smoke could not capture its Translate baseline.\n";
            return false;
        }
        state.selected_timeline_track_id = std::string("bone:1:Translate");
        state.timeline_editor.selected_keys = {*first_key};
        state.timeline_editor.active_key = *first_key;
        state.timeline_editor.graph_view.active_component = GraphComponent::Y;

        const auto handles = handles_for("bone:1:Translate", *first_key, 1U);
        TimelineTrackRow const* translate = row_of("bone:1:Translate");
        if (translate == nullptr || !handles.has_value() ||
            handles->kind != timeline_graph_model::SegmentKind::Linear ||
            handles->control_points !=
                timeline_graph_model::kLinearEquivalentControlPoints) {
            std::cerr << "Graph easing smoke expected a seeded linear outgoing segment.\n";
            return false;
        }
        if (!begin_timeline_graph_handle_gesture(
                &state, 4343U, *translate, *first_key,
                handles->frame,
                handles->control_points, InterpolationKind::Linear,
                cached_timeline_tracks(&state)) ||
            !authoring_gesture_active(state) ||
            state.session.undo_count() != before->undo_count) {
            std::cerr << "Beginning a handle gesture must open exactly one transaction.\n";
            return false;
        }
        if (!apply_timeline_graph_handle_control_points(
                &state, cached_timeline_tracks(&state), kOvershoot) ||
            state.session.undo_count() != before->undo_count) {
            std::cerr << "Applying handle control points did not preview without history.\n";
            return false;
        }
        finish_timeline_graph_handle_gesture(&state, true);
        if (authoring_gesture_active(state) ||
            state.timeline_editor.graph_handle_gesture.has_value() ||
            state.session.undo_count() != before->undo_count + 1U ||
            state.timeline_editor.selected_keys !=
                std::vector<TimelineKeyRef>{*first_key} ||
            !(state.timeline_editor.active_key ==
              std::optional<TimelineKeyRef>(*first_key))) {
            std::cerr << "One handle gesture did not produce one stable-selection undo entry.\n";
            return false;
        }

        const auto after_key = projected_key(&state, "bone:1:Translate", *first_key);
        if (!after_key.has_value() ||
            after_key->time_seconds != before_key->time_seconds ||
            after_key->values != before_key->values ||
            after_key->easing != InterpolationKind::CubicBezier ||
            !near_points(after_key->control_points, kOvershoot)) {
            std::cerr << "A handle drag moved a scalar value or lost its curve.\n";
            return false;
        }

        // Criterion 3, direct: the displayed component was Y, and the X segment
        // re-projects the same single curve.
        const auto x_handles = handles_for("bone:1:Translate", *first_key, 0U);
        const auto y_handles = handles_for("bone:1:Translate", *first_key, 1U);
        if (!x_handles.has_value() || !y_handles.has_value() ||
            x_handles->kind != timeline_graph_model::SegmentKind::Cubic ||
            y_handles->kind != x_handles->kind ||
            x_handles->control_points != y_handles->control_points ||
            !near_points(x_handles->control_points, kOvershoot)) {
            std::cerr << "Editing while displaying Y did not change the X segment identically.\n";
            return false;
        }

        // Materialization copied every runtime key into the project rather
        // than replacing the track with the one edited key.
        const auto* materialized =
            state.session.project()->find_transform_timeline_edit(
                "idle", "spine", marrow::editor::TransformTimelineChannel::Translate);
        if (materialized == nullptr || materialized->keyframes.size() != 3U) {
            std::cerr << "A handle gesture did not materialize the whole runtime track.\n";
            return false;
        }

        // Undo then redo restores the curve with selection bit-identical.
        if (!state.session.undo()) {
            std::cerr << "A handle easing edit could not be undone.\n";
            return false;
        }
        sync_shell_from_editor_session(&state);
        const auto undone = projected_key(&state, "bone:1:Translate", *first_key);
        if (!undone.has_value() || undone->easing != before_key->easing ||
            state.timeline_editor.selected_keys !=
                std::vector<TimelineKeyRef>{*first_key} ||
            !(state.timeline_editor.active_key ==
              std::optional<TimelineKeyRef>(*first_key))) {
            std::cerr << "Undoing a handle easing edit lost its curve or selection.\n";
            return false;
        }
        if (!state.session.redo()) {
            std::cerr << "A handle easing edit could not be redone.\n";
            return false;
        }
        sync_shell_from_editor_session(&state);
        const auto redone = projected_key(&state, "bone:1:Translate", *first_key);
        if (!redone.has_value() || redone->easing != InterpolationKind::CubicBezier ||
            !near_points(redone->control_points, kOvershoot) ||
            state.timeline_editor.selected_keys !=
                std::vector<TimelineKeyRef>{*first_key} ||
            !(state.timeline_editor.active_key ==
              std::optional<TimelineKeyRef>(*first_key))) {
            std::cerr << "Redoing a handle easing edit lost its curve or selection.\n";
            return false;
        }
    }

    // --- With the track already materialized, exactly one interpolation field
    // in the whole project moves when a Stepped segment converts to Cubic. ---
    {
        const auto stepped_key = key_of("bone:1:Translate", 1U);
        if (!stepped_key.has_value()) return false;
        const marrow::editor::ProjectData before = *state.session.project();
        const auto before_key = projected_key(&state, "bone:1:Translate", *stepped_key);
        if (!before_key.has_value() ||
            before_key->easing != InterpolationKind::Stepped) {
            std::cerr << "Graph easing smoke requires a stepped spine Translate segment.\n";
            return false;
        }
        state.timeline_editor.selected_keys = {*stepped_key};
        state.timeline_editor.active_key = *stepped_key;
        const auto handles = handles_for("bone:1:Translate", *stepped_key, 0U);
        TimelineTrackRow const* translate = row_of("bone:1:Translate");
        if (translate == nullptr || !handles.has_value() ||
            handles->kind != timeline_graph_model::SegmentKind::Stepped ||
            handles->control_points !=
                timeline_graph_model::kLinearEquivalentControlPoints) {
            std::cerr << "A stepped segment did not expose the linear-equivalent seed.\n";
            return false;
        }
        const std::size_t undo_before = state.session.undo_count();
        constexpr std::array<double, 4> kSecondCurve{0.4, 0.6, 0.9, 0.2};
        if (!begin_timeline_graph_handle_gesture(
                &state, 4344U, *translate, *stepped_key,
                handles->frame,
                handles->control_points, InterpolationKind::Stepped,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_handle_control_points(
                &state, cached_timeline_tracks(&state), kSecondCurve)) {
            std::cerr << "A stepped-to-cubic handle gesture did not apply.\n";
            return false;
        }
        finish_timeline_graph_handle_gesture(&state, true);
        if (state.session.undo_count() != undo_before + 1U) {
            std::cerr << "A stepped-to-cubic conversion did not commit one entry.\n";
            return false;
        }
        const marrow::editor::ProjectData& after = *state.session.project();
        const auto same_easing = [](const marrow::runtime::Interpolation& left,
                                    const marrow::runtime::Interpolation& right) {
            if (left.kind() != right.kind()) return false;
            if (left.kind() != InterpolationKind::CubicBezier) return true;
            const auto& first = left.cubic_bezier();
            const auto& second = right.cubic_bezier();
            return first.cx1 == second.cx1 && first.cy1 == second.cy1 &&
                first.cx2 == second.cx2 && first.cy2 == second.cy2;
        };
        std::size_t differing = 0U;
        if (before.transform_timeline_edits.size() !=
            after.transform_timeline_edits.size()) {
            std::cerr << "A handle gesture reshaped the project's transform timelines.\n";
            return false;
        }
        for (std::size_t timeline = 0U;
             timeline < before.transform_timeline_edits.size();
             ++timeline) {
            const auto& left = before.transform_timeline_edits[timeline].keyframes;
            const auto& right = after.transform_timeline_edits[timeline].keyframes;
            if (left.size() != right.size()) {
                std::cerr << "A handle gesture reshaped a transform timeline.\n";
                return false;
            }
            for (std::size_t key = 0U; key < left.size(); ++key) {
                if (!same_easing(left[key].interpolation, right[key].interpolation)) {
                    ++differing;
                }
            }
        }
        if (differing != 1U) {
            std::cerr << "A handle gesture changed " << differing
                      << " interpolation fields instead of exactly one.\n";
            return false;
        }
        state.session.clear_history();
    }

    // --- A drag on an already-cubic segment that ends on its original control
    // points commits nothing. ---
    {
        const auto rotate_key = key_of("bone:1:Rotate", 0U);
        const TimelineTrackRow* rotate = row_of("bone:1:Rotate");
        if (!rotate_key.has_value() || rotate == nullptr) return false;
        state.selected_timeline_track_id = std::string("bone:1:Rotate");
        state.timeline_editor.selected_keys = {*rotate_key};
        state.timeline_editor.active_key = *rotate_key;
        state.timeline_editor.graph_view.active_component = GraphComponent::Angle;
        const auto handles = handles_for("bone:1:Rotate", *rotate_key, 0U);
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Rotate");
        if (!handles.has_value() || !before.has_value() ||
            handles->kind != timeline_graph_model::SegmentKind::Cubic) {
            std::cerr << "Graph easing smoke requires a cubic spine Rotate segment.\n";
            return false;
        }
        if (!begin_timeline_graph_handle_gesture(
                &state, 4345U, *rotate, *rotate_key,
                handles->frame,
                handles->control_points, InterpolationKind::CubicBezier,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_handle_control_points(
                &state, cached_timeline_tracks(&state), handles->control_points)) {
            std::cerr << "An unchanged cubic handle gesture did not stay live.\n";
            return false;
        }
        state.status_message = "graph easing sentinel";
        finish_timeline_graph_handle_gesture(&state, true);
        const auto after = capture_graph_edit_snapshot(&state, "bone:1:Rotate");
        if (!after.has_value() || !graph_edit_snapshots_match(*before, *after) ||
            state.status_message != "graph easing sentinel") {
            std::cerr << "An unchanged handle gesture created a history entry or "
                         "reported an edit it did not make: status=\""
                      << state.status_message << "\".\n";
            return false;
        }
    }

    // --- A multi-frame drag that leaves the original control points and comes
    // back to them commits nothing either. The single-frame case above hits the
    // primitive's no-change early return; this one exercises the path where a
    // real mutation happened first and was then undone by the pointer. ---
    {
        const auto rotate_key = key_of("bone:1:Rotate", 0U);
        const TimelineTrackRow* rotate = row_of("bone:1:Rotate");
        if (!rotate_key.has_value() || rotate == nullptr) return false;
        state.selected_timeline_track_id = std::string("bone:1:Rotate");
        state.timeline_editor.selected_keys = {*rotate_key};
        state.timeline_editor.active_key = *rotate_key;
        state.timeline_editor.graph_view.active_component = GraphComponent::Angle;
        const auto handles = handles_for("bone:1:Rotate", *rotate_key, 0U);
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Rotate");
        if (!handles.has_value() || !before.has_value() ||
            handles->kind != timeline_graph_model::SegmentKind::Cubic) {
            return false;
        }
        if (!begin_timeline_graph_handle_gesture(
                &state, 4358U, *rotate, *rotate_key,
                handles->frame,
                handles->control_points, InterpolationKind::CubicBezier,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_handle_control_points(
                &state, cached_timeline_tracks(&state), {0.9, 0.85, 0.1, 0.15}) ||
            !apply_timeline_graph_handle_control_points(
                &state, cached_timeline_tracks(&state), handles->control_points)) {
            std::cerr << "A drag-away-and-back handle gesture did not stay live.\n";
            return false;
        }
        state.status_message = "graph easing sentinel";
        finish_timeline_graph_handle_gesture(&state, true);
        const auto after = capture_graph_edit_snapshot(&state, "bone:1:Rotate");
        if (authoring_gesture_active(state) || !after.has_value() ||
            !graph_edit_snapshots_match(*before, *after) ||
            state.status_message != "graph easing sentinel") {
            std::cerr << "A drag-away-and-back handle gesture created a history entry "
                         "or reported an edit it did not make: status=\""
                      << state.status_message << "\" undo="
                      << state.session.undo_count() << "/" << before->undo_count
                      << " revision=" << state.session.project_revision() << "/"
                      << before->project_revision << ".\n";
            return false;
        }
    }

    // --- A drag that leaves the dead zone on a Stepped segment and returns to
    // the seed still commits, because the authored kind genuinely changed. ---
    {
        const auto stepped_key = key_of("bone:1:Rotate", 1U);
        const TimelineTrackRow* rotate = row_of("bone:1:Rotate");
        if (!stepped_key.has_value() || rotate == nullptr) return false;
        state.timeline_editor.selected_keys = {*stepped_key};
        state.timeline_editor.active_key = *stepped_key;
        const auto handles = handles_for("bone:1:Rotate", *stepped_key, 0U);
        if (!handles.has_value() ||
            handles->kind != timeline_graph_model::SegmentKind::Stepped ||
            handles->control_points !=
                timeline_graph_model::kLinearEquivalentControlPoints) {
            std::cerr << "The seed-return case requires a stepped spine Rotate segment.\n";
            return false;
        }
        const std::size_t undo_before = state.session.undo_count();
        if (!begin_timeline_graph_handle_gesture(
                &state, 4359U, *rotate, *stepped_key,
                handles->frame,
                handles->control_points, InterpolationKind::Stepped,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_handle_control_points(
                &state, cached_timeline_tracks(&state), {0.8, 0.2, 0.9, 0.1}) ||
            !apply_timeline_graph_handle_control_points(
                &state, cached_timeline_tracks(&state), handles->control_points)) {
            std::cerr << "A seed-return handle gesture did not stay live.\n";
            return false;
        }
        state.status_message = "graph easing sentinel";
        finish_timeline_graph_handle_gesture(&state, true);
        const auto after = projected_key(&state, "bone:1:Rotate", *stepped_key);
        if (state.status_message != "Edited key easing" ||
            state.session.undo_count() != undo_before + 1U || !after.has_value() ||
            after->easing != InterpolationKind::CubicBezier ||
            !near_points(
                after->control_points,
                timeline_graph_model::kLinearEquivalentControlPoints)) {
            std::cerr << "Returning to the seed on a stepped segment did not commit "
                         "the Cubic conversion.\n";
            return false;
        }
        if (!state.session.undo()) return false;
        sync_shell_from_editor_session(&state);
        state.session.clear_history();
    }

    // --- The flat Blue segment of slot:0:Color: a drag succeeds through the
    // substituted 100 px reference span while R, G, and A vary across the same
    // shared curve. ---
    {
        const auto color_key = key_of("slot:0:Color", 0U);
        const TimelineTrackRow* color = row_of("slot:0:Color");
        if (!color_key.has_value() || color == nullptr) {
            std::cerr << "Graph easing smoke requires a body Colour key.\n";
            return false;
        }
        state.selected_timeline_track_id = std::string("slot:0:Color");
        state.timeline_editor.selected_keys = {*color_key};
        state.timeline_editor.active_key = *color_key;
        state.timeline_editor.graph_view.active_component = GraphComponent::Blue;
        const auto blue = handles_for("slot:0:Color", *color_key, 2U);
        const auto red = handles_for("slot:0:Color", *color_key, 0U);
        if (!blue.has_value() || !red.has_value() || !blue->frame.flat_value_span ||
            red->frame.flat_value_span) {
            std::cerr << "The body Colour key did not expose a flat Blue segment "
                         "sharing its curve with a varying Red segment.\n";
            return false;
        }
        const std::size_t undo_before = state.session.undo_count();
        constexpr std::array<double, 4> kFlatCurve{0.3, 0.8, 0.7, 0.25};
        if (!begin_timeline_graph_handle_gesture(
                &state, 4346U, *color, *color_key,
                blue->frame,
                blue->control_points, InterpolationKind::Linear,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_handle_control_points(
                &state, cached_timeline_tracks(&state), kFlatCurve)) {
            std::cerr << "A flat-segment handle gesture did not apply.\n";
            return false;
        }
        finish_timeline_graph_handle_gesture(&state, true);
        const auto after = projected_key(&state, "slot:0:Color", *color_key);
        if (state.session.undo_count() != undo_before + 1U || !after.has_value() ||
            after->easing != InterpolationKind::CubicBezier ||
            !near_points(after->control_points, kFlatCurve)) {
            std::cerr << "A flat-segment handle gesture did not persist its curve.\n";
            return false;
        }
        state.session.clear_history();
    }

    // --- Atomic cancel and atomic rejection. ---
    {
        const auto first_key = key_of("bone:1:Translate", 0U);
        TimelineTrackRow const* translate = row_of("bone:1:Translate");
        if (!first_key.has_value() || translate == nullptr) return false;
        state.selected_timeline_track_id = std::string("bone:1:Translate");
        state.timeline_editor.selected_keys = {*first_key};
        state.timeline_editor.active_key = *first_key;
        state.timeline_editor.graph_view.active_component = GraphComponent::X;

        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        const auto handles = handles_for("bone:1:Translate", *first_key, 0U);
        translate = row_of("bone:1:Translate");
        if (!before.has_value() || !handles.has_value() || translate == nullptr) {
            return false;
        }
        if (!begin_timeline_graph_handle_gesture(
                &state, 4347U, *translate, *first_key,
                handles->frame,
                handles->control_points, InterpolationKind::CubicBezier,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_handle_control_points(
                &state, cached_timeline_tracks(&state), {0.15, -1.25, 0.85, 2.5})) {
            std::cerr << "A cancellable handle gesture did not stage its preview.\n";
            return false;
        }
        finish_timeline_graph_handle_gesture(&state, false);
        const auto after = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (state.timeline_editor.graph_handle_gesture.has_value() ||
            authoring_gesture_active(state) || !after.has_value() ||
            !graph_edit_snapshots_match(*before, *after)) {
            std::cerr << "A cancelled handle gesture did not roll back atomically.\n";
            return false;
        }

        // A control point the primitive rejects cancels the whole gesture. The
        // row pointer is re-resolved because the shared track cache is rebuilt
        // whenever a gesture syncs the shell from the session.
        state.error_message.clear();
        translate = row_of("bone:1:Translate");
        if (translate == nullptr) return false;
        if (!begin_timeline_graph_handle_gesture(
                &state, 4348U, *translate, *first_key,
                handles->frame,
                handles->control_points, InterpolationKind::CubicBezier,
                cached_timeline_tracks(&state))) {
            std::cerr << "The rejection case could not stage its gesture.\n";
            return false;
        }
        if (apply_timeline_graph_handle_control_points(
                &state, cached_timeline_tracks(&state), {1.5, 0.0, 0.5, 1.0})) {
            std::cerr << "An out-of-range control point was accepted by the gesture.\n";
            return false;
        }
        const auto rejected = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (state.timeline_editor.graph_handle_gesture.has_value() ||
            authoring_gesture_active(state) || state.error_message.empty() ||
            !rejected.has_value() || !graph_edit_snapshots_match(*before, *rejected)) {
            std::cerr << "A rejected handle control point did not roll back atomically.\n";
            return false;
        }

        // A non-finite request cancels the same way.
        state.error_message.clear();
        translate = row_of("bone:1:Translate");
        if (translate == nullptr) return false;
        if (!begin_timeline_graph_handle_gesture(
                &state, 4349U, *translate, *first_key,
                handles->frame,
                handles->control_points, InterpolationKind::CubicBezier,
                cached_timeline_tracks(&state)) ||
            apply_timeline_graph_handle_control_points(
                &state,
                cached_timeline_tracks(&state),
                {0.2, 0.0, 0.5, std::numeric_limits<double>::quiet_NaN()}) ||
            state.timeline_editor.graph_handle_gesture.has_value() ||
            state.error_message.empty()) {
            std::cerr << "A non-finite handle control point did not cancel the gesture.\n";
            return false;
        }
        const auto after_nonfinite =
            capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!after_nonfinite.has_value() ||
            !graph_edit_snapshots_match(*before, *after_nonfinite)) {
            std::cerr << "A non-finite handle request did not roll back atomically.\n";
            return false;
        }
    }

    // --- Fail-closed begin contracts. ---
    {
        const auto first_key = key_of("bone:1:Translate", 0U);
        const auto last_key = key_of("bone:1:Translate", 2U);
        TimelineTrackRow const* translate = row_of("bone:1:Translate");
        const TimelineTrackRow* attachment = row_of("slot:0:Attachment");
        const auto attachment_key = key_of("slot:0:Attachment", 0U);
        if (!first_key.has_value() || !last_key.has_value() || translate == nullptr ||
            attachment == nullptr || !attachment_key.has_value()) {
            return false;
        }
        state.selected_timeline_track_id = std::string("bone:1:Translate");
        state.timeline_editor.selected_keys = {*first_key};
        state.timeline_editor.active_key = *first_key;
        const auto handles = handles_for("bone:1:Translate", *first_key, 0U);
        if (!handles.has_value()) return false;

        // A row the graph cannot author.
        if (begin_timeline_graph_handle_gesture(
                &state, 4350U, *attachment, *attachment_key,
                handles->frame,
                handles->control_points, InterpolationKind::Linear,
                cached_timeline_tracks(&state)) ||
            state.timeline_editor.graph_handle_gesture.has_value()) {
            std::cerr << "An unsupported row opened a handle gesture.\n";
            return false;
        }
        // The last key owns no outgoing segment.
        translate = row_of("bone:1:Translate");
        if (translate == nullptr) return false;
        if (begin_timeline_graph_handle_gesture(
                &state, 4351U, *translate, *last_key,
                handles->frame,
                handles->control_points, InterpolationKind::Linear,
                cached_timeline_tracks(&state)) ||
            state.timeline_editor.graph_handle_gesture.has_value()) {
            std::cerr << "The last key opened a handle gesture.\n";
            return false;
        }
        // A key that does not belong to the row.
        const TimelineKeyRef stranger{"bone:1:Rotate", 0, 0U, 1U};
        translate = row_of("bone:1:Translate");
        if (translate == nullptr) return false;
        if (begin_timeline_graph_handle_gesture(
                &state, 4352U, *translate, stranger,
                handles->frame,
                handles->control_points, InterpolationKind::Linear,
                cached_timeline_tracks(&state)) ||
            state.timeline_editor.graph_handle_gesture.has_value()) {
            std::cerr << "A key from another track opened a handle gesture.\n";
            return false;
        }
        // A non-finite frame.
        timeline_graph_model::SegmentFrame broken = handles->frame;
        broken.value_span = std::numeric_limits<double>::quiet_NaN();
        translate = row_of("bone:1:Translate");
        if (translate == nullptr) return false;
        if (begin_timeline_graph_handle_gesture(
                &state, 4353U, *translate, *first_key,
                broken,
                handles->control_points, InterpolationKind::Linear,
                cached_timeline_tracks(&state)) ||
            state.timeline_editor.graph_handle_gesture.has_value()) {
            std::cerr << "A non-finite segment frame opened a handle gesture.\n";
            return false;
        }
        // Another live authoring gesture blocks the press entirely.
        translate = row_of("bone:1:Translate");
        if (translate == nullptr) return false;
        if (!begin_timeline_graph_value_gesture(
                &state, 4354U, *translate, GraphComponent::X,
                cached_timeline_tracks(&state))) {
            std::cerr << "Handle exclusivity smoke could not stage its value gesture.\n";
            return false;
        }
        translate = row_of("bone:1:Translate");
        if (translate == nullptr) return false;
        if (begin_timeline_graph_handle_gesture(
                &state, 4355U, *translate, *first_key,
                handles->frame,
                handles->control_points, InterpolationKind::Linear,
                cached_timeline_tracks(&state)) ||
            state.timeline_editor.graph_handle_gesture.has_value()) {
            std::cerr << "A handle gesture opened during another authoring gesture.\n";
            return false;
        }
        finish_timeline_graph_value_gesture(&state, false);
        // A null state never crashes and never opens anything.
        if (apply_timeline_graph_handle_control_points(
                &state, cached_timeline_tracks(&state), kOvershoot)) {
            std::cerr << "Applying control points without a live gesture succeeded.\n";
            return false;
        }
    }

    // --- cancel_authoring_gestures and TimelineEditorState{} adoption both
    // release a live handle gesture. ---
    {
        const auto first_key = key_of("bone:1:Translate", 0U);
        TimelineTrackRow const* translate = row_of("bone:1:Translate");
        if (!first_key.has_value() || translate == nullptr) return false;
        state.selected_timeline_track_id = std::string("bone:1:Translate");
        state.timeline_editor.selected_keys = {*first_key};
        state.timeline_editor.active_key = *first_key;
        const auto handles = handles_for("bone:1:Translate", *first_key, 0U);
        const auto before = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (!handles.has_value() || !before.has_value()) return false;
        if (!begin_timeline_graph_handle_gesture(
                &state, 4356U, *translate, *first_key,
                handles->frame,
                handles->control_points, InterpolationKind::CubicBezier,
                cached_timeline_tracks(&state)) ||
            !apply_timeline_graph_handle_control_points(
                &state, cached_timeline_tracks(&state), {0.1, 0.2, 0.3, 0.4})) {
            std::cerr << "The cancel-all case could not stage its gesture.\n";
            return false;
        }
        cancel_authoring_gestures(&state, "Graph easing smoke");
        const auto after = capture_graph_edit_snapshot(&state, "bone:1:Translate");
        if (state.timeline_editor.graph_handle_gesture.has_value() ||
            authoring_gesture_active(state) || !after.has_value() ||
            !graph_edit_snapshots_match(*before, *after)) {
            std::cerr << "cancel_authoring_gestures did not release the handle gesture.\n";
            return false;
        }

        translate = row_of("bone:1:Translate");
        if (translate == nullptr) return false;
        if (!begin_timeline_graph_handle_gesture(
                &state, 4357U, *translate, *first_key,
                handles->frame,
                handles->control_points, InterpolationKind::CubicBezier,
                cached_timeline_tracks(&state))) {
            std::cerr << "The source-adoption case could not stage its gesture.\n";
            return false;
        }
        finish_timeline_graph_handle_gesture(&state, false);
        state.timeline_editor = TimelineEditorState{};
        if (state.timeline_editor.graph_handle_gesture.has_value() ||
            state.timeline_editor.graph_drag.has_value() ||
            authoring_gesture_active(state)) {
            std::cerr << "TimelineEditorState source adoption did not clear the handle slot.\n";
            return false;
        }
    }

    // --- Full handle drags through the shared MAR-168 drag driver. ---
    {
        const auto first_key = key_of("bone:1:Translate", 0U);
        if (!first_key.has_value()) return false;
        state.selected_timeline_track_id = std::string("bone:1:Translate");
        state.timeline_editor.selected_keys = {*first_key};
        state.timeline_editor.active_key = *first_key;
        state.timeline_editor.graph_view.active_component = GraphComponent::X;

        const auto stage = [&]() -> std::optional<timeline_graph_model::HandleGeometry> {
            state.timeline_editor.graph_view.view = view;
            return handles_for("bone:1:Translate", *first_key, 0U);
        };

        // A press on a handle arms a candidate and opens no transaction.
        {
            const auto handles = stage();
            const TimelineTrackRow* translate = row_of("bone:1:Translate");
            const auto before = capture_graph_edit_snapshot(&state, "bone:1:Translate");
            if (!handles.has_value() || translate == nullptr || !before.has_value()) {
                return false;
            }
            if (!begin_timeline_graph_handle_drag(
                    &state, *translate, *handles,
                    timeline_graph_model::HandleIndex::First, 4360U, plot, view,
                    handles->first_handle.x, handles->first_handle.y) ||
                !state.timeline_editor.graph_drag.has_value() ||
                authoring_gesture_active(state) ||
                state.session.undo_count() != before->undo_count) {
                std::cerr << "A handle press must arm a candidate without a transaction.\n";
                return false;
            }
            // Inside the dead zone nothing starts.
            (void)update_timeline_graph_point_drag(
                &state, cached_timeline_tracks(&state),
                handles->first_handle.x + 2.0, handles->first_handle.y + 1.0,
                true, false, false);
            if (state.timeline_editor.graph_handle_gesture.has_value()) {
                std::cerr << "A handle drag inside the dead zone opened a gesture.\n";
                return false;
            }
            // Leaving the dead zone opens the handle gesture and no other.
            (void)update_timeline_graph_point_drag(
                &state, cached_timeline_tracks(&state),
                handles->first_handle.x + 30.0, handles->first_handle.y - 40.0,
                true, false, false);
            if (!state.timeline_editor.graph_handle_gesture.has_value() ||
                state.timeline_editor.retime_gesture.has_value() ||
                state.timeline_editor.graph_value_gesture.has_value()) {
                std::cerr << "A handle drag must open only the handle gesture.\n";
                return false;
            }
            // Free 2-D: one diagonal frame moves cx and cy together, so no
            // axis lock is applied to handles.
            const auto& live = *state.timeline_editor.graph_handle_gesture;
            if (near_points(live.applied_control_points, handles->control_points) ||
                std::abs(live.applied_control_points[0] - handles->control_points[0]) <=
                    1e-9 ||
                std::abs(live.applied_control_points[1] - handles->control_points[1]) <=
                    1e-9 ||
                live.applied_control_points[2] != handles->control_points[2] ||
                live.applied_control_points[3] != handles->control_points[3]) {
                std::cerr << "A diagonal handle drag did not move cx and cy together, "
                             "or disturbed the untouched handle.\n";
                return false;
            }
            // Releasing commits and clears both the candidate and the gesture.
            (void)update_timeline_graph_point_drag(
                &state, cached_timeline_tracks(&state),
                handles->first_handle.x + 30.0, handles->first_handle.y - 40.0,
                false, false, false);
            if (state.timeline_editor.graph_drag.has_value() ||
                state.timeline_editor.graph_handle_gesture.has_value() ||
                state.session.undo_count() != before->undo_count + 1U) {
                std::cerr << "Releasing a handle drag did not commit exactly one entry.\n";
                return false;
            }
            state.session.clear_history();
        }

        // An X-clamped drag stops at exactly 1.0 and stays live.
        {
            const auto handles = stage();
            const TimelineTrackRow* translate = row_of("bone:1:Translate");
            if (!handles.has_value() || translate == nullptr) return false;
            if (!begin_timeline_graph_handle_drag(
                    &state, *translate, *handles,
                    timeline_graph_model::HandleIndex::First, 4361U, plot, view,
                    handles->first_handle.x, handles->first_handle.y)) {
                std::cerr << "The clamp case could not arm its candidate.\n";
                return false;
            }
            if (!update_timeline_graph_point_drag(
                    &state, cached_timeline_tracks(&state),
                    handles->end_anchor.x + 400.0, handles->first_handle.y,
                    true, false, false) ||
                !state.timeline_editor.graph_handle_gesture.has_value() ||
                state.timeline_editor.graph_handle_gesture->applied_control_points[0] !=
                    1.0) {
                std::cerr << "A drag past the end anchor did not clamp cx to exactly 1.0 "
                             "while staying live.\n";
                return false;
            }
            if (!update_timeline_graph_point_drag(
                    &state, cached_timeline_tracks(&state),
                    handles->start_anchor.x - 400.0, handles->first_handle.y,
                    true, false, false) ||
                !state.timeline_editor.graph_handle_gesture.has_value() ||
                state.timeline_editor.graph_handle_gesture->applied_control_points[0] !=
                    0.0) {
                std::cerr << "A drag past the start anchor did not clamp cx to exactly 0.0 "
                             "while staying live.\n";
                return false;
            }
            cancel_timeline_graph_point_drag(&state);
            if (state.timeline_editor.graph_drag.has_value() ||
                state.timeline_editor.graph_handle_gesture.has_value()) {
                std::cerr << "cancel_timeline_graph_point_drag left a handle gesture live.\n";
                return false;
            }
        }

        // Finite Y overshoot in both directions survives the drag.
        {
            const auto handles = stage();
            const TimelineTrackRow* translate = row_of("bone:1:Translate");
            if (!handles.has_value() || translate == nullptr) return false;
            const double low_y = timeline_graph_model::y_at_value(
                plot, view,
                handles->frame.start_value - 1.5 * handles->frame.value_span);
            const double high_y = timeline_graph_model::y_at_value(
                plot, view,
                handles->frame.start_value + 2.25 * handles->frame.value_span);
            if (!begin_timeline_graph_handle_drag(
                    &state, *translate, *handles,
                    timeline_graph_model::HandleIndex::First, 4362U, plot, view,
                    handles->first_handle.x, handles->first_handle.y) ||
                !update_timeline_graph_point_drag(
                    &state, cached_timeline_tracks(&state),
                    handles->first_handle.x, low_y, true, false, false) ||
                !state.timeline_editor.graph_handle_gesture.has_value() ||
                state.timeline_editor.graph_handle_gesture->applied_control_points[1] >
                    -1.0) {
                std::cerr << "A downward handle drag did not produce negative overshoot.\n";
                return false;
            }
            cancel_timeline_graph_point_drag(&state);

            const auto second = stage();
            translate = row_of("bone:1:Translate");
            if (!second.has_value() || translate == nullptr) return false;
            if (!begin_timeline_graph_handle_drag(
                    &state, *translate, *second,
                    timeline_graph_model::HandleIndex::Second, 4363U, plot, view,
                    second->second_handle.x, second->second_handle.y) ||
                !update_timeline_graph_point_drag(
                    &state, cached_timeline_tracks(&state),
                    second->second_handle.x, high_y, true, false, false) ||
                !state.timeline_editor.graph_handle_gesture.has_value() ||
                state.timeline_editor.graph_handle_gesture->applied_control_points[3] <
                    1.5) {
                std::cerr << "An upward handle drag did not produce positive overshoot.\n";
                return false;
            }
            cancel_timeline_graph_point_drag(&state);
        }

        // cancel_requested, a non-finite pointer, and a mid-drag active-key
        // change each roll back to the pre-press bytes.
        {
            struct CancelCase {
                const char* label;
                int mode;  // 0 = cancel_requested, 1 = non-finite, 2 = key change
            };
            const CancelCase cases[] = {
                {"an Escape-cancelled handle drag", 0},
                {"a non-finite handle pointer", 1},
                {"an active key that changed mid-drag", 2},
            };
            for (const CancelCase& scenario : cases) {
                state.timeline_editor.active_key = *first_key;
                state.timeline_editor.selected_keys = {*first_key};
                const auto handles = stage();
                const TimelineTrackRow* translate = row_of("bone:1:Translate");
                const auto before =
                    capture_graph_edit_snapshot(&state, "bone:1:Translate");
                if (!handles.has_value() || translate == nullptr || !before.has_value()) {
                    return false;
                }
                if (!begin_timeline_graph_handle_drag(
                        &state, *translate, *handles,
                        timeline_graph_model::HandleIndex::First, 4364U, plot, view,
                        handles->first_handle.x, handles->first_handle.y) ||
                    !update_timeline_graph_point_drag(
                        &state, cached_timeline_tracks(&state),
                        handles->first_handle.x + 40.0, handles->first_handle.y - 30.0,
                        true, false, false) ||
                    !state.timeline_editor.graph_handle_gesture.has_value()) {
                    std::cerr << "Cancel case " << scenario.label
                              << " could not stage its live gesture.\n";
                    return false;
                }
                if (scenario.mode == 0) {
                    (void)update_timeline_graph_point_drag(
                        &state, cached_timeline_tracks(&state),
                        handles->first_handle.x + 40.0, handles->first_handle.y - 30.0,
                        true, true, false);
                } else if (scenario.mode == 1) {
                    (void)update_timeline_graph_point_drag(
                        &state, cached_timeline_tracks(&state),
                        std::numeric_limits<double>::quiet_NaN(),
                        handles->first_handle.y, true, false, false);
                } else {
                    state.timeline_editor.active_key = key_of("bone:1:Translate", 1U);
                    (void)update_timeline_graph_point_drag(
                        &state, cached_timeline_tracks(&state),
                        handles->first_handle.x + 50.0, handles->first_handle.y - 35.0,
                        true, false, false);
                }
                const auto after = capture_graph_edit_snapshot(&state, "bone:1:Translate");
                if (state.timeline_editor.graph_drag.has_value() ||
                    state.timeline_editor.graph_handle_gesture.has_value() ||
                    authoring_gesture_active(state) || !after.has_value() ||
                    !graph_edit_snapshots_match(*before, *after)) {
                    std::cerr << "Cancel case " << scenario.label
                              << " did not roll back atomically.\n";
                    return false;
                }
            }
            state.timeline_editor.active_key = *first_key;
            state.timeline_editor.selected_keys = {*first_key};
        }

        // Fail-closed press contracts for the candidate itself.
        {
            const auto handles = stage();
            const TimelineTrackRow* translate = row_of("bone:1:Translate");
            const TimelineTrackRow* attachment = row_of("slot:0:Attachment");
            if (!handles.has_value() || translate == nullptr || attachment == nullptr) {
                return false;
            }
            if (begin_timeline_graph_handle_drag(
                    &state, *attachment, *handles,
                    timeline_graph_model::HandleIndex::First, 4365U, plot, view,
                    handles->first_handle.x, handles->first_handle.y) ||
                state.timeline_editor.graph_drag.has_value()) {
                std::cerr << "An unsupported row armed a handle candidate.\n";
                return false;
            }
            if (begin_timeline_graph_handle_drag(
                    &state, *translate, *handles,
                    timeline_graph_model::HandleIndex::First, 4366U, plot, view,
                    std::numeric_limits<double>::quiet_NaN(), handles->first_handle.y) ||
                state.timeline_editor.graph_drag.has_value()) {
                std::cerr << "A non-finite handle press armed a candidate.\n";
                return false;
            }
            // A geometry whose key is no longer the active key never arms.
            state.timeline_editor.active_key = key_of("bone:1:Translate", 1U);
            if (begin_timeline_graph_handle_drag(
                    &state, *translate, *handles,
                    timeline_graph_model::HandleIndex::First, 4367U, plot, view,
                    handles->first_handle.x, handles->first_handle.y) ||
                state.timeline_editor.graph_drag.has_value()) {
                std::cerr << "A stale handle geometry armed a candidate.\n";
                return false;
            }
            state.timeline_editor.active_key = *first_key;
            // Another live authoring gesture blocks the press entirely.
            translate = row_of("bone:1:Translate");
            if (translate == nullptr ||
                !begin_timeline_graph_value_gesture(
                    &state, 4368U, *translate, GraphComponent::X,
                    cached_timeline_tracks(&state))) {
                std::cerr << "Handle candidate exclusivity could not stage its gesture.\n";
                return false;
            }
            translate = row_of("bone:1:Translate");
            if (translate == nullptr ||
                begin_timeline_graph_handle_drag(
                    &state, *translate, *handles,
                    timeline_graph_model::HandleIndex::First, 4369U, plot, view,
                    handles->first_handle.x, handles->first_handle.y) ||
                state.timeline_editor.graph_drag.has_value()) {
                std::cerr << "A handle press during another authoring gesture armed a candidate.\n";
                return false;
            }
            finish_timeline_graph_value_gesture(&state, false);
        }

        // Hit priority: the handle hit test wins wherever a handle and a key
        // point are both within their radii of the same pointer.
        {
            const auto handles = stage();
            const TimelineTrackRow* translate = row_of("bone:1:Translate");
            if (!handles.has_value() || translate == nullptr) return false;
            const auto& projection =
                cached_timeline_graph_projection(&state, *translate);
            if (!projection.track.has_value()) return false;
            const auto geometry = timeline_graph_model::build_geometry(
                *projection.track,
                state.timeline_editor.graph_view.component_visible,
                view,
                plot,
                state.timeline_time_seconds);
            if (!geometry.has_value()) return false;
            const auto handle_hit = timeline_graph_model::hit_test_handle(
                *handles, handles->first_handle.x, handles->first_handle.y, 7.0);
            const auto point_hit = timeline_graph_model::hit_test(
                *geometry, handles->first_handle.x, handles->first_handle.y, 1e6);
            if (!handle_hit.has_value() || !point_hit.has_value() ||
                !(handle_hit->key == *first_key)) {
                std::cerr << "The handle hit test did not resolve the pressed segment key.\n";
                return false;
            }
        }
    }

    if (marrow::editor::agent_operation_descriptor_count() != operation_count_before) {
        std::cerr << "Graph easing editing changed the Agent operation surface.\n";
        return false;
    }
    return true;
}

} // namespace marrow::editor::shell
