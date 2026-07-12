#include "shell_asset_watch.hpp"

#include <memory>
#include <sstream>
#include <system_error>
#include <utility>

#include "shell_constraints.hpp"
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
    const ShellState& state) {
    const std::vector<std::filesystem::path> paths = current_runtime_asset_paths(state);
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
    state->runtime_asset_watch_entries = capture_runtime_asset_watch_entries(*state);
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

    std::optional<std::string> previous_selection_name;
    if (const auto selection_name = selected_bone_name(*state)) {
        previous_selection_name = std::string(*selection_name);
    }
    std::optional<std::string> previous_slot_name;
    if (state->selected_slot_index.has_value() &&
        *state->selected_slot_index < state->load_result.skeleton_data->slots().size()) {
        previous_slot_name =
            state->load_result.skeleton_data->slots()[*state->selected_slot_index].name;
    }

    const auto document_result = marrow::runtime::load_skeleton_document(
        state->load_result.project->resolved_skeleton_path());
    if (!document_result) {
        state->error_message = document_result.error->format();
        state->status_message = "Runtime asset hot-reload failed";
        return false;
    }

    std::vector<std::shared_ptr<const marrow::runtime::AtlasData>> atlas_data;
    atlas_data.reserve(state->load_result.project->resolved_atlas_paths().size());
    for (const auto& atlas_path : state->load_result.project->resolved_atlas_paths()) {
        const auto atlas_result = marrow::runtime::AtlasLoader::load(atlas_path);
        if (!atlas_result) {
            state->error_message = atlas_result.error->format();
            state->status_message = "Runtime asset hot-reload failed";
            return false;
        }
        atlas_data.push_back(atlas_result.atlas_data);
    }

    const auto previous_document = state->load_result.base_skeleton_document;
    const auto previous_atlas_data = state->load_result.atlas_data;
    state->load_result.base_skeleton_document =
        marrow::allocate_shared<marrow::runtime::json::Document>(
            std::move(*document_result.document));
    state->load_result.atlas_data = std::move(atlas_data);

    if (!rebuild_project_runtime(state)) {
        const std::string reload_error = state->error_message;
        state->load_result.base_skeleton_document = previous_document;
        state->load_result.atlas_data = previous_atlas_data;
        state->error_message = reload_error;
        state->status_message = "Runtime asset hot-reload failed";
        return false;
    }
    if (!apply_current_animation_state_to_preview(state)) {
        const std::string reload_error = state->error_message;
        state->load_result.base_skeleton_document = previous_document;
        state->load_result.atlas_data = previous_atlas_data;
        (void)rebuild_project_runtime(state);
        state->error_message = reload_error;
        state->status_message = "Runtime asset hot-reload failed";
        return false;
    }

    if (previous_selection_name.has_value()) {
        state->selected_bone_index =
            state->load_result.skeleton_data->find_bone_index(*previous_selection_name);
    }
    if (previous_slot_name.has_value()) {
        state->selected_slot_index =
            state->load_result.skeleton_data->find_slot_index(*previous_slot_name);
    }
    if (state->selected_slot_index.has_value()) {
        sync_attachment_selection_for_slot(state, *state->selected_slot_index);
    }
    validate_selected_constraint(state);

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
        capture_runtime_asset_watch_entries(*state);
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
