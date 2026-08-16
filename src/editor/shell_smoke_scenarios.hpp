#pragma once

#include <filesystem>

#include "shell_state.hpp"

namespace marrow::editor::shell {

bool validate_parameter_mode_shell_smoke(
    ShellState* state,
    const Options& options,
    ImGuiIO& io);

bool validate_runtime_asset_hot_reload_smoke(const ShellState& source_state);
bool validate_animation_catalog_smoke(const std::filesystem::path& project_path);
bool validate_animation_duration_shell_smoke(
    const std::filesystem::path& project_path);
bool validate_viewport_camera_smoke(const std::filesystem::path& project_path);
bool validate_viewport_prepared_scene_renderer_smoke(
    const std::filesystem::path& project_path);
bool validate_viewport_ffd_smoke(const std::filesystem::path& project_path);
bool validate_timeline_p0_authoring_smoke(
    const std::filesystem::path& project_path);
bool validate_derived_cache_smoke(ShellState* state);
bool validate_selection_set_shell_smoke(ShellState* state);

bool validate_shell_foundation_smoke(
    ShellState& shell_state,
    const Options& options);

bool validate_viewport_selection_smoke(ShellState& shell_state);

bool validate_timeline_project_smoke(ShellState& shell_state);

bool render_headless_smoke_frames(
    ShellState& shell_state,
    const Options& options,
    ImGuiIO& io);

} // namespace marrow::editor::shell
