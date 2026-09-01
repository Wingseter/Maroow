#pragma once

#include <optional>
#include <string>
#include <string_view>

#include "marrow/editor/diagnostics.hpp"

/**
 * @file safe_fix.hpp
 * @brief The three allowlisted repairs, applied through one validated
 *        transaction and one undo entry (MAR-187).
 *
 * MAR-187 exposes **no** way to apply a fix implicitly. There is no "fix all",
 * no fix on open, no fix on save, and no agent operation. `apply_safe_fix` is
 * called from exactly one place — a per-row button — and Task 9 greps for that
 * being true.
 *
 * This header forward-declares `EditorSession` rather than including
 * `marrow/editor/session.hpp`, so the view model does not drag the session into
 * every consumer.
 */
namespace marrow::editor {

class EditorSession;

/**
 * @brief The three repairs, typed.
 *
 * Total over exactly the three `kSafeFix*` strings MAR-186 allowlists. The
 * string table that maps into this enum is the one list in MAR-187 the compiler
 * cannot check — nothing warns if a fourth entry is added or one is misspelled
 * — so it carries its own corpus gate rather than relying on review.
 */
enum class SafeFixKind {
    RemoveOrphanOverlay,
    NormalizeWeights,
    ResetPreviewReference,
};

/** @brief Outcome of one repair attempt. */
struct SafeFixResult {
    /// False only when the fix was refused. A refusal leaves the session untouched.
    bool ok{false};
    /// True when the project actually changed. A no-op repair records no history.
    bool changed{false};
    /// Empty on success. Never a placeholder.
    std::string error;
    /// The identity that was repaired, echoed back so a caller can drop the row.
    std::string applied_identity;
};

/**
 * @brief Maps an allowlisted safe-fix identifier to its typed kind.
 * @param safe_fix_id Identifier from `DiagnosticIssue::safe_fix_id`.
 * @return The kind, or `std::nullopt` for anything else. An empty string is
 *         **not** accepted.
 *
 * This is the only place a string becomes a fix. It is asserted to agree with
 * MAR-186's `is_allowlisted_safe_fix` on every input the corpus gate sweeps.
 */
std::optional<SafeFixKind> safe_fix_kind_for(std::string_view safe_fix_id);

/**
 * @brief Applies one allowlisted repair to one issue.
 * @param session Session to repair. Untouched unless the fix commits.
 * @param issue The issue whose Fix button was pressed.
 * @return The outcome.
 *
 * Five steps, in this order, and the order is the design:
 *
 * 1. **Session preflight** — no project, or a transaction already open, rejects.
 * 2. **Allowlist preflight** — a `safe_fix_id` outside the three rejects.
 * 3. **Freshness preflight** — the issue is re-collected and must still be
 *    present with the same identity AND the same `safe_fix_id`. A `ProblemsView`
 *    can be one revision behind the session, so a user can click Fix on a row
 *    whose issue another edit already removed; applying it would mutate
 *    something nobody asked about.
 * 4. **One transaction** — mutate the candidate, then `cancel()` on a primitive
 *    error or when nothing changed, otherwise `commit()`.
 * 5. **One undo entry** — `commit()` produces exactly one.
 *
 * Every rejection in steps 1-3 happens **before** `begin_edit`, so
 * `serialize_project()`, `dirty()`, `undo_count()`, `redo_count()` and all three
 * revisions are untouched by a refusal. That ordering is a recorded design
 * choice rather than a tested property: `begin_edit` + `cancel()` was measured
 * to leave all seven of those values identical, so opening the transaction
 * earlier is invisible through the session's entire public surface.
 */
SafeFixResult apply_safe_fix(EditorSession& session, const DiagnosticIssue& issue);

}  // namespace marrow::editor
