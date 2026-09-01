#pragma once

#include <filesystem>

#include "shell_state.hpp"

namespace marrow::editor::shell {

bool validate_parameter_mode_shell_smoke(
    ShellState* state,
    const Options& options,
    ImGuiIO& io);

bool validate_runtime_asset_hot_reload_smoke(const ShellState& source_state);
bool validate_mar180_failed_hot_reload_shell_coherence(const ShellState& source_state);
bool validate_mar180_failed_shell_save_preserves_file(const ShellState& source_state);
bool validate_mar181_path_resolution_smoke();
bool validate_mar181_new_project_writes_nothing(const ShellState& source_state);
bool validate_mar181_save_as_moves_the_shell_path(const ShellState& source_state);
bool validate_mar181_failed_save_as_preserves_shell_path(const ShellState& source_state);
bool validate_mar181_failed_open_preserves_shell(const ShellState& source_state);
bool validate_mar181_file_menu_mouse_smoke(const std::filesystem::path& project_path);
bool validate_mar181_save_shortcut_smoke(const std::filesystem::path& project_path);
bool validate_mar181_arm_deferred_action_for_frame_body(ShellState* state);
bool validate_mar181_frame_body_applied_pending(const ShellState& state);
bool validate_mar182_intent_gate_smoke(const ShellState& source_state);
bool validate_mar182_save_completes_intent_smoke(const ShellState& source_state);
bool validate_mar182_failed_save_holds_intent_smoke(const ShellState& source_state);
bool validate_mar182_save_path_cancel_smoke(const ShellState& source_state);
bool validate_mar182_discard_smoke(const ShellState& source_state);
bool validate_mar182_cancel_and_repeat_smoke(const ShellState& source_state);
bool validate_mar182_close_request_smoke(const ShellState& source_state);
bool validate_mar182_dirty_prompt_mouse_smoke(
    const std::filesystem::path& project_path);
bool validate_mar183_shell_list_algebra_smoke(const ShellState& source_state);
bool validate_mar183_recent_gate_smoke(const ShellState& source_state);
bool validate_mar183_recording_policy_smoke(const ShellState& source_state);
bool validate_mar183_missing_entries_smoke(const ShellState& source_state);
bool validate_mar183_non_interference_smoke(const ShellState& source_state);
bool validate_mar183_recent_menu_mouse_smoke(
    const std::filesystem::path& project_path);
bool validate_animation_catalog_smoke(const std::filesystem::path& project_path);
bool validate_animation_duration_shell_smoke(
    const std::filesystem::path& project_path);
bool validate_viewport_camera_smoke(const std::filesystem::path& project_path);
bool validate_viewport_snap_smoke(const std::filesystem::path& project_path);
bool validate_viewport_prepared_scene_renderer_smoke(
    const std::filesystem::path& project_path);
bool validate_viewport_ffd_smoke(const std::filesystem::path& project_path);
bool validate_timeline_graph_shell_smoke(
    const std::filesystem::path& project_path);
bool validate_timeline_graph_edit_shell_smoke(
    const std::filesystem::path& project_path);
bool validate_timeline_graph_easing_shell_smoke(
    const std::filesystem::path& project_path);
bool validate_timeline_curve_preset_shell_smoke(
    const std::filesystem::path& project_path);
bool validate_timeline_curve_mode_shell_smoke(
    const std::filesystem::path& project_path);
bool validate_timeline_loop_sync_shell_smoke(
    const std::filesystem::path& project_path);
bool validate_timeline_scale_shell_smoke(
    const std::filesystem::path& project_path);
bool validate_preview_playback_speed_shell_smoke(
    const std::filesystem::path& project_path);
bool validate_inherit_editing_shell_smoke(
    const std::filesystem::path& project_path);
bool validate_constraint_lifecycle_shell_smoke(
    const std::filesystem::path& project_path);
bool validate_constraint_parameter_shell_smoke(
    const std::filesystem::path& project_path);
bool validate_timeline_p0_authoring_smoke(
    const std::filesystem::path& project_path);
bool validate_derived_cache_smoke(ShellState* state);
bool validate_selection_set_shell_smoke(ShellState* state);

bool validate_mar187_problems_shell_smoke(
    const std::filesystem::path& project_path);

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
