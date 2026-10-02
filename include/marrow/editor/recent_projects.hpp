#pragma once

/**
 * @file recent_projects.hpp
 * @brief MAR-183: the recent-project list algebra.
 *
 * `EditorPreferences::recent_projects` (`preferences.hpp`) is the STORAGE, and
 * its comment deliberately disclaims every policy question: *"Recent paths
 * remain in their authored order and spelling. Canonicalization,
 * de-duplication, existence checks, MRU promotion, and list bounds belong to
 * the Recent Projects feature rather than this storage layer."* This header is
 * that feature's policy layer, and nothing more.
 *
 * It is compiled into `marrow_editor` rather than `marrow_editor_shell` for one
 * reason: `marrow_preference_tests` links `marrow_editor` only, and
 * `preference_store_tests.cpp` says in as many words not to "fix" that split by
 * linking the shell into a unit test. So the algebra -- which is pure, and is
 * what AC6 asks a preference test to cover -- lives here, and every binding to
 * `ShellState`, the `PreferenceStore` and ImGui lives in
 * `shell_recent_projects.hpp` on the far side of that line. Nothing in this
 * header includes a session, a project, a store or an ImGui type.
 *
 * Every function is total and NOTHING throws: each filesystem call uses the
 * `std::error_code` overload, the same discipline `list_directory` already
 * ships. An entry must be able to outlive its file (AC4), so a path that cannot
 * be resolved is degraded, never rejected.
 */

#include <cstddef>
#include <filesystem>
#include <vector>

namespace marrow::editor {

/// AC1's bound. Ten entries, most-recent first.
///
/// Named, never spelled as a literal at a call site: a count sweep for `10`
/// would collide with the ten management operations and the ten `!= 64U`
/// registry guards.
inline constexpr std::size_t kRecentProjectLimit = 10;

/**
 * @brief Resolves a path to the form the list stores and compares.
 *
 * `weakly_canonical`, deliberately NOT `canonical`: `canonical` fails when the
 * path does not exist, and AC4 requires entries to outlive their files. On
 * error the chain degrades `absolute(p).lexically_normal()` -> `p` as given.
 * An empty input returns empty rather than the process CWD.
 *
 * Canonicalizing is load-bearing rather than cosmetic here.
 * `ShellState::project_path` defaults to the RELATIVE
 * `assets/fixtures/player_idle.marrow`, so without this the stored list would
 * be relative to whichever directory the editor happened to be launched from.
 *
 * @par Identity, and the case question
 * Two entries are the same project iff their canonical forms compare equal
 * bytewise. **This function folds no case of its own**, which is the rule
 * `resolve_choice` already ships for extensions and for the same stated reason:
 * folding would make the editor's own behaviour depend on the host, and on
 * Linux the two really are different files.
 *
 * The consequence on a case-insensitive volume is NOT the simple one, and it
 * was measured rather than assumed. `weakly_canonical` resolves its longest
 * EXISTING prefix through the filesystem, so on macOS:
 *
 * - `/x/A.marrow` and `/x/a.marrow` where the file EXISTS both canonicalize to
 *   the on-disk spelling and collapse into ONE entry;
 * - the same two spellings where the file is MISSING keep their lexical
 *   remainders verbatim and stay TWO entries.
 *
 * Both branches are asserted by `preference_store_tests.cpp`'s
 * "recent project list algebra" case, which prints the measurement every run.
 */
std::filesystem::path canonical_recent_path(const std::filesystem::path& path);

/**
 * @brief Whether @p path names a regular file right now.
 *
 * `is_regular_file`, so a directory or a dangling symlink is false. Evaluated
 * at DISPLAY time, once per entry per frame the submenu is open -- there is no
 * cache, and therefore no invalidation rule. A remounted volume re-enables its
 * entry with no user action.
 */
bool recent_project_exists(const std::filesystem::path& path);

/**
 * @brief Records @p path as the most recent project.
 *
 * `erase-equal -> insert-at-front -> truncate`, in that order. Inserting BEFORE
 * truncating is what guarantees the newest entry is never the one evicted;
 * truncating first is an off-by-one that silently drops the entry the user just
 * asked for.
 *
 * @return Whether @p list changed. Every mutator here reports this because it
 *         is exactly what the settings write is keyed on: a promotion that
 *         changes nothing must not rewrite the preference file.
 */
bool promote_recent_project(
    std::vector<std::filesystem::path>* list,
    const std::filesystem::path& path);

/// @brief Removes every entry equal to @p path. @return Whether @p list changed.
bool forget_recent_path(
    std::vector<std::filesystem::path>* list,
    const std::filesystem::path& path);

/**
 * @brief Drops every entry that is not a regular file right now.
 *
 * Called ONLY from the explicit `Clear Missing` command. Nothing calls this on
 * load, on display or on click: a project on an unmounted volume must not be
 * destroyed by a launch on which the user did nothing.
 *
 * @return Whether @p list changed.
 */
bool drop_missing_recent_paths(std::vector<std::filesystem::path>* list);

/**
 * @brief Brings a list read off disk into the form every other function assumes.
 *
 * Canonicalizes each entry, drops empties, keeps the FIRST of each duplicate
 * group at its original position, and caps at `kRecentProjectLimit`. Missing
 * files are KEPT -- normalizing is not pruning.
 *
 * Idempotent by construction, and asserted to be: a second call returns false.
 * That invariant is what lets every later operation assume an already-canonical
 * list, and it is the reason loading can normalize without ever writing.
 *
 * @return Whether @p list changed.
 */
bool normalize_recent_paths(std::vector<std::filesystem::path>* list);

} // namespace marrow::editor
