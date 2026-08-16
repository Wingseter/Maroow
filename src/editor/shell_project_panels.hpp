#pragma once

#include "shell_state.hpp"

namespace marrow::editor::shell {

enum class ProjectMenuAction {
    None,
    QuitRequested,
};

enum class AnimationCatalogAction {
    Create,
    Duplicate,
    Rename,
    Delete,
};

ShellMode current_shell_mode(const ShellState* state);
void apply_shell_mode(ShellState* state, ShellMode mode);
bool apply_animation_catalog_action(
    ShellState* state,
    AnimationCatalogAction action,
    std::string_view source_animation,
    std::string_view destination_animation = {});
bool begin_animation_duration_gesture(
    ShellState* state,
    std::string_view animation_name);
bool apply_animation_duration_gesture(ShellState* state, double duration);
bool finish_animation_duration_gesture(ShellState* state, bool commit);
void draw_shell_toolbar(bool* reload_requested, ShellState* state);
ProjectMenuAction draw_menu_bar(
    bool* reload_requested,
    ShellState* state);
void draw_project_window(bool* reload_requested, ShellState* state);
void draw_runtime_window(const ShellState& state);

} // namespace marrow::editor::shell
