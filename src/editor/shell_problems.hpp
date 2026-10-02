#pragma once

#include <string_view>

#include "marrow/editor/diagnostics.hpp"
#include "shell_state.hpp"

namespace marrow::editor::shell {

/**
 * @file shell_problems.hpp
 * @brief The Problems window and its input wiring (MAR-187).
 *
 * This layer owns NO logic. It reads a `ProblemsView`, emits widgets, and on a
 * click calls `plan_issue_navigation` or `apply_safe_fix`. It does no filtering,
 * no grouping, no ordering and no `ProjectData` mutation -- Task 9 greps this
 * file for `begin_edit`, `transaction`, `std::sort` and `.erase(` and expects
 * none.
 */

/// @brief Re-collects and rebuilds only when either revision has moved.
void refresh_problems_if_revised(ShellState* state);

/// @brief The window an issue's panel maps onto. The ONLY switch over `DiagnosticPanel`.
std::string_view focus_window_for_panel(DiagnosticPanel panel);

/// @brief Applies one row's navigation plan to the shell.
void activate_problem_row(ShellState* state, const DiagnosticIssue& issue);

/// @brief Draws the Problems window. Called from BOTH frame bodies.
void draw_problems_window(ShellState* state);

}  // namespace marrow::editor::shell
