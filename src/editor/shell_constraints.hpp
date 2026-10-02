#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "constraint_lookup.hpp"
#include "marrow/editor/selection.hpp"

namespace marrow::editor::shell {

struct ShellState;

const char* constraint_kind_label(ConstraintKind kind);
void select_constraint(
    ShellState* state,
    ConstraintKind kind,
    std::string_view name,
    std::string_view source,
    bool update_status_message);
std::string unique_constraint_name(
    const ShellState& state,
    ConstraintKind kind,
    std::string_view prefix);
std::optional<std::size_t> ensure_ik_constraint_edit_index(
    ShellState* state,
    std::string_view name);
std::optional<std::size_t> ensure_path_constraint_edit_index(
    ShellState* state,
    std::string_view name);
std::optional<std::size_t> ensure_transform_constraint_edit_index(
    ShellState* state,
    std::string_view name);
std::optional<std::size_t> ensure_physics_constraint_edit_index(
    ShellState* state,
    std::string_view name);
/** @brief Which lifecycle verb a constraint catalog affordance applies. */
enum class ConstraintCatalogAction {
    Rename,
    Delete,
};

/**
 * @brief Applies one constraint rename or delete and refreshes the shell.
 *
 * UI-free: the modals call it, and the headless smoke calls it directly. It
 * refuses while an authoring gesture or another transaction is live, composes
 * the history label, cascades `state->selection`, and reports the primitive's
 * message verbatim on a rejection.
 */
bool apply_constraint_catalog_action(
    ShellState* state,
    ConstraintCatalogAction action,
    ConstraintKind family,
    std::string_view source,
    std::string_view destination);

/** @brief Seeds the rename modal with a constraint's current name. */
void request_constraint_rename(
    ShellState* state,
    ConstraintKind family,
    std::string source);
/** @brief Seeds the delete confirmation with a constraint and its skin references. */
void request_constraint_delete(
    ShellState* state,
    ConstraintKind family,
    std::string name);
/** @brief Applies the pending rename to `destination`. */
bool confirm_constraint_rename(ShellState* state, std::string_view destination);
/** @brief Applies the pending delete. */
bool confirm_constraint_delete(ShellState* state);
/** @brief Abandons a pending rename or delete without opening a transaction. */
void cancel_constraint_catalog(ShellState* state);
/** @brief The skins the pending request would rewrite; empty when none. */
const std::vector<std::string>& pending_constraint_affected_skins() noexcept;

/**
 * @brief Drops constraint selections that no longer resolve; leaves every other kind alone.
 *
 * `rebuild_project_runtime()` does not reconcile the selection -- only
 * `reload_project()` and the runtime asset watch do -- so an undone or redone
 * lifecycle edit would otherwise leave a stale `ConstraintSelection` that
 * inflates the status line's "; N selected" count. Widening this to
 * `reconcile_selection_to_runtime()` would prune bone, slot and attachment
 * selections after every undo in the editor, which is far outside this story.
 *
 * @return True when at least one member was dropped.
 */
bool reconcile_constraint_selection(ShellState* state);

void draw_constraints_window(ShellState* state);

} // namespace marrow::editor::shell
