#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace marrow::editor {
struct ProjectData;
}

namespace marrow::editor::shell {

struct ShellState;

struct RuntimeAssetWatchEntry {
    std::filesystem::path path;
    bool exists{false};
    std::optional<std::filesystem::file_time_type> write_time;
};

enum class RuntimeAssetPollOutcome {
    Unchanged,
    Reloaded,
    Failed,
};

std::string join_paths(const std::vector<std::filesystem::path>& values);
std::vector<std::filesystem::path> current_runtime_asset_paths(
    const ShellState& state);
void reset_runtime_asset_watch(ShellState* state);
bool reload_runtime_source_assets(ShellState* state);
RuntimeAssetPollOutcome poll_runtime_asset_changes(ShellState* state);
void materialize_temp_project_runtime_assets(
    const ShellState& state,
    marrow::editor::ProjectData* project);

} // namespace marrow::editor::shell
