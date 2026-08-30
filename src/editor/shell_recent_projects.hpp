#pragma once

/**
 * @file shell_recent_projects.hpp
 * @brief MAR-183: the recent-project list bound to the shell.
 *
 * The pure list algebra lives in `marrow/editor/recent_projects.hpp`, inside
 * `marrow_editor`, so `marrow_preference_tests` can cover it without linking the
 * shell. THIS header is the far side of that line: everything that needs a
 * `ShellState`, a `PreferenceStore`, or ImGui.
 *
 * @par The one rule that matters
 * A Recent entry is an Open like any other. `open_recent_project` delegates to
 * `begin_session_intent` and does nothing else -- it never calls
 * `session.open`, never touches `pending_file_application`, and never calls
 * `begin_file_action`. Those absences are the structural reason a Recent click
 * over unsaved work cannot bypass MAR-182's Save/Discard/Cancel gate, and they
 * are checked by grep rather than argued in prose.
 */

#include <filesystem>
#include <string>

#include "shell_state.hpp"

namespace marrow::editor::shell {

/// The `Open Recent` submenu's label, and the two commands inside it.
constexpr char kRecentMenu[] = "Open Recent";
constexpr char kRecentRemoveMenu[] = "Remove";
constexpr char kRecentClearMissing[] = "Clear Missing";

/**
 * @brief Builds a menu label of the form `"<filename>##<canonical path>"`.
 *
 * Everything after `##` is excluded from the DISPLAY and included in the ImGui
 * id -- the same fact `shell_file_paths.hpp` already records for the modal
 * names. That gives a short label, an id that cannot collide between two
 * directories holding the same filename, and an exact probe string for the
 * mouse case.
 *
 * A path containing `##` is pathological but harmless: it shortens the display
 * and never the id. The label is always passed as a `%s` argument and never as
 * a format string, so a `%` in a path is inert.
 */
std::string recent_menu_label(const std::filesystem::path& path);

/**
 * @brief The ONE writer of `editor-settings.json` in this feature.
 *
 * @param changed Whether the list operation actually changed the vector. When
 *                false this returns immediately without touching the disk, so a
 *                re-record of the current head costs no write. Every mutator in
 *                the algebra returns exactly this boolean.
 *
 * Mirrors `set_shell_default_curve`: it saves the LOADED `state->preferences`
 * mutated in place -- never a fresh value -- so `default_curve` and every
 * unknown additive field survive. On failure it sets `state->error_message`,
 * KEEPS the in-memory change, and returns false.
 */
bool persist_recent_projects(ShellState* state, bool changed);

/// @brief Promotes @p path to the head of the list and persists if it changed.
void record_recent_project(ShellState* state, const std::filesystem::path& path);

/// @brief Removes @p path from the list and persists if it changed.
void forget_recent_project(ShellState* state, const std::filesystem::path& path);

/**
 * @brief Removes every entry whose file is not there right now.
 *
 * The ONLY pruning in this feature, and it runs only when the user asks. Never
 * on load, never on display, never on click: an entry on an unmounted volume
 * must survive a launch on which the user did nothing.
 */
void forget_missing_recent_projects(ShellState* state);

/**
 * @brief Opens a recent entry -- through the gate, like every other Open.
 *
 * The entire body is one `begin_session_intent` call. A dirty session therefore
 * gets the Save/Discard/Cancel prompt before anything is replaced, and the
 * promotion happens only after the open SUCCEEDS.
 */
void open_recent_project(ShellState* state, const std::filesystem::path& path);

/**
 * @brief Draws the `Open Recent` submenu. Called from inside `BeginMenu("File")`.
 *
 * Existence is read per entry, per frame, while the submenu is open -- there is
 * no cache and therefore no invalidation rule, so a remounted volume re-enables
 * its entry with no user action. The cost is up to `kRecentProjectLimit` stat
 * calls per frame while a user-opened menu is up.
 */
void draw_recent_projects_menu(ShellState* state);

} // namespace marrow::editor::shell
