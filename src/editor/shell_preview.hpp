#pragma once

#include <cstddef>
#include <optional>
#include <string>

#include "marrow/runtime/profiler.hpp"

namespace marrow::editor {
struct ProjectData;
}

namespace marrow::runtime {
struct AnimationStateSnapshot;
class Skeleton;
class SkeletonData;
}

namespace marrow::editor::shell {

struct ShellState;
enum class EditActionKind;

bool apply_project_command_change(
    ShellState* state,
    const marrow::editor::ProjectData& previous_project,
    EditActionKind kind,
    std::string command_label,
    std::string group,
    bool allow_merge,
    std::string failure_status);
bool undo_project_change(ShellState* state);
bool redo_project_change(ShellState* state);
void handle_project_history_shortcuts(ShellState* state);

std::optional<std::size_t> preview_root_bone_index(
    const marrow::runtime::SkeletonData& skeleton);
void apply_preview_slot_overrides(
    const ShellState& state,
    marrow::runtime::Skeleton* skeleton,
    std::optional<double> sample_time = std::nullopt);
void apply_preview_slot_overrides(ShellState* state);
bool apply_current_animation_state_to_preview(ShellState* state);
bool restore_preview_playback(
    ShellState* state,
    const marrow::runtime::AnimationStateSnapshot& snapshot);
bool refresh_preview_pose(ShellState* state);
std::optional<marrow::runtime::ProfilerFrame> build_preview_profiler_frame(
    const ShellState& state);

} // namespace marrow::editor::shell
