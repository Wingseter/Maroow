#include "shell_asset_watch.hpp"

#include <memory>
#include <optional>
#include <sstream>
#include <system_error>
#include <utility>

#include "shell_preview.hpp"
#include "shell_selection.hpp"
#include "shell_state.hpp"
#include "marrow/allocator.hpp"
#include "marrow/editor/project.hpp"

namespace marrow::editor::shell {
namespace {

std::filesystem::path absolutize_path(const std::filesystem::path& path) {
    if (path.empty() || path.is_absolute()) {
        return path.lexically_normal();
    }

    std::error_code error;
    const std::filesystem::path current_directory = std::filesystem::current_path(error);
    if (error) {
        return path.lexically_normal();
    }

    return (current_directory / path).lexically_normal();
}

std::vector<std::filesystem::path> absolutize_paths(
    const std::vector<std::filesystem::path>& paths) {
    std::vector<std::filesystem::path> absolute_paths;
    absolute_paths.reserve(paths.size());
    for (const auto& path : paths) {
        absolute_paths.push_back(absolutize_path(path));
    }
    return absolute_paths;
}

RuntimeAssetWatchEntry make_runtime_asset_watch_entry(const std::filesystem::path& path) {
    RuntimeAssetWatchEntry entry;
    entry.path = absolutize_path(path);

    std::error_code error;
    entry.exists = std::filesystem::exists(entry.path, error);
    if (error || !entry.exists) {
        entry.exists = false;
        return entry;
    }

    const auto write_time = std::filesystem::last_write_time(entry.path, error);
    if (!error) {
        entry.write_time = write_time;
    }
    return entry;
}

bool runtime_asset_watch_entry_equal(
    const RuntimeAssetWatchEntry& left,
    const RuntimeAssetWatchEntry& right) {
    return left.path == right.path &&
        left.exists == right.exists &&
        left.write_time == right.write_time;
}

std::vector<RuntimeAssetWatchEntry> capture_runtime_asset_watch_entries(
    const std::vector<std::filesystem::path>& paths) {
    std::vector<RuntimeAssetWatchEntry> entries;
    entries.reserve(paths.size());
    for (const auto& path : paths) {
        entries.push_back(make_runtime_asset_watch_entry(path));
    }
    return entries;
}

} // namespace

std::string join_paths(const std::vector<std::filesystem::path>& values) {
    if (values.empty()) {
        return "<none>";
    }

    std::ostringstream stream;
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0) {
            stream << ", ";
        }
        stream << values[index].string();
    }
    return stream.str();
}

std::vector<std::filesystem::path> current_runtime_asset_paths(const ShellState& state) {
    std::vector<std::filesystem::path> paths;
    if (!state.load_result || state.load_result.project == nullptr) {
        return paths;
    }

    paths.push_back(absolutize_path(state.load_result.project->resolved_skeleton_path()));
    for (const auto& atlas_path : state.load_result.project->resolved_atlas_paths()) {
        paths.push_back(absolutize_path(atlas_path));
    }
    return paths;
}

void reset_runtime_asset_watch(ShellState* state) {
    if (state == nullptr) {
        return;
    }
    state->runtime_asset_watch_entries =
        capture_runtime_asset_watch_entries(current_runtime_asset_paths(*state));
}

void materialize_temp_project_runtime_assets(
    const ShellState& state,
    marrow::editor::ProjectData* project) {
    if (!state.load_result || state.load_result.project == nullptr || project == nullptr) {
        return;
    }

    project->runtime_assets.skeleton_path =
        absolutize_path(state.load_result.project->resolved_skeleton_path());
    project->runtime_assets.atlas_paths =
        absolutize_paths(state.load_result.project->resolved_atlas_paths());
}

bool reload_runtime_source_assets(ShellState* state) {
    if (!state->load_result || state->load_result.project == nullptr) {
        return false;
    }

    // A source adoption can reorder or move bones, so an in-flight screen-space
    // marquee is meaningless afterwards. These two are PRESENTATION state the
    // session knows nothing about, so the shell still snapshots and restores them.
    const auto previous_ffd_selection = state->viewport_ffd_selection;
    const auto previous_ffd_box_selection = state->viewport_ffd_box_selection;

    // The shell used to load the new document and atlases, assign them into
    // `state->load_result` -- which is a REFERENCE into the session -- and only
    // then rebuild, undoing the assignment by hand when the rebuild failed. The
    // second rollback even discarded its own rebuild result, so `skeleton_data`
    // could be left derived from a different document than
    // `base_skeleton_document`. The session now does all of it into locals and
    // swaps once, so there is no model-layer rollback to keep in sync.
    std::optional<marrow::runtime::AnimationStateSnapshot> playback_snapshot;
    if (state->animation_state() != nullptr) {
        playback_snapshot = state->animation_state()->capture_state();
    }

    const marrow::editor::SessionResult adoption =
        marrow::editor::EditorSessionShellBinding::adopt_runtime_sources(state->session);
    if (!adoption) {
        state->error_message = adoption.error->format();
        state->status_message = "Runtime asset hot-reload failed";
        state->viewport_ffd_selection = previous_ffd_selection;
        state->viewport_ffd_box_selection = previous_ffd_box_selection;
        return false;
    }

    // Reconcile shell working composition after source adoption. Runtime views
    // already resolve the newly adopted session objects without rebinding.
    normalize_shell_preview_composition_to_runtime(state);
    if (playback_snapshot.has_value() && state->animation_state() != nullptr) {
        state->animation_state()->restore_state(*playback_snapshot);
    }
    if (!apply_current_animation_state_to_preview(state)) {
        state->status_message = "Runtime asset hot-reload failed";
        state->viewport_ffd_selection = previous_ffd_selection;
        state->viewport_ffd_box_selection = previous_ffd_box_selection;
        return false;
    }

    // Runtime source adoption may reorder or move bones. Never carry a stale
    // screen-space marquee into the newly adopted preview.
    state->viewport_ffd_selection.reset();
    state->viewport_ffd_box_selection.reset();
    state->viewport_box_selection.reset();
    marrow::editor::reconcile_selection_to_runtime(
        state->selection,
        *state->load_result.skeleton_data);
    reconcile_hierarchy_anchor_to_runtime(
        state,
        *state->load_result.skeleton_data);

    state->status_message =
        "Hot-reloaded runtime assets: " + join_paths(current_runtime_asset_paths(*state));
    state->error_message.clear();
    return true;
}

RuntimeAssetPollOutcome poll_runtime_asset_changes(ShellState* state) {
    if (state == nullptr || !state->load_result || state->load_result.project == nullptr) {
        return RuntimeAssetPollOutcome::Unchanged;
    }

    const std::vector<std::filesystem::path> current_paths = current_runtime_asset_paths(*state);
    if (state->runtime_asset_watch_entries.size() != current_paths.size()) {
        reset_runtime_asset_watch(state);
        return RuntimeAssetPollOutcome::Unchanged;
    }

    for (std::size_t index = 0; index < current_paths.size(); ++index) {
        if (state->runtime_asset_watch_entries[index].path != current_paths[index]) {
            reset_runtime_asset_watch(state);
            return RuntimeAssetPollOutcome::Unchanged;
        }
    }

    const std::vector<RuntimeAssetWatchEntry> current_entries =
        capture_runtime_asset_watch_entries(current_paths);
    bool changed = false;
    for (std::size_t index = 0; index < current_entries.size(); ++index) {
        if (!runtime_asset_watch_entry_equal(
                current_entries[index],
                state->runtime_asset_watch_entries[index])) {
            changed = true;
            break;
        }
    }

    if (!changed) {
        return RuntimeAssetPollOutcome::Unchanged;
    }

    if (!reload_runtime_source_assets(state)) {
        return RuntimeAssetPollOutcome::Failed;
    }

    reset_runtime_asset_watch(state);
    return RuntimeAssetPollOutcome::Reloaded;
}

} // namespace marrow::editor::shell
