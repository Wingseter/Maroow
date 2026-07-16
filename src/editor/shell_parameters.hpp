#pragma once

#include "shell_state.hpp"

namespace marrow::editor::shell {

/** @brief Draws the four Parameter Modeling authoring surfaces. */
void draw_parameter_windows(ShellState* state);

/** @brief Runs the parameter-fixture-specific headless shell acceptance path. */
bool validate_parameter_mode_shell_smoke(
    ShellState* state,
    const Options& options,
    ImGuiIO& io);

} // namespace marrow::editor::shell
