#pragma once

#include "shell_state.hpp"

namespace marrow::editor::shell {

enum class ShellMode {
    Setup = 0,
    Animation = 1,
    WeightPaint = 2,
};

ShellMode current_shell_mode(const ShellState* state);
void apply_shell_mode(ShellState* state, ShellMode mode);
void draw_shell_toolbar(bool* reload_requested, ShellState* state);
void draw_menu_bar(
    GLFWwindow* window,
    bool* reload_requested,
    ShellState* state);
void draw_project_window(bool* reload_requested, ShellState* state);
void draw_runtime_window(const ShellState& state);

} // namespace marrow::editor::shell
