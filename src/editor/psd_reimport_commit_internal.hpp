#pragma once

#include <functional>
#include <string>

#include "marrow/editor/psd_reimport_commit.hpp"

namespace marrow::editor::detail {

/**
 * @brief Returns a non-empty string to inject that error AFTER @p step completes.
 *
 * `atomic_file_write.hpp`'s `RenameCallback` was evaluated for this job and
 * rejected. It is consulted at one point inside `write_file_atomically`, so it
 * reaches at most the steps that write through that function -- five of fifteen
 * here, and the importer writes staging through raw `std::ofstream`, so it
 * reaches ZERO of them today. It is also keyed on a `(source, destination)` pair,
 * which cannot distinguish two steps writing into the same directory, and it
 * fires DURING a write rather than after a completed step.
 *
 * The tension worth stating, because it is the cost of this seam rather than an
 * argument for it: the byte-fidelity guarantee comes from rollback being a
 * RENAME of an untouched original, and every step this seam adds to its own
 * coverage is a step whose rollback stops being a rename. The seam reaches
 * 15/15 by construction; the two `Validate*` steps and `CleanJournal` are the
 * arms where that costs nothing, and the `Place*` arms are the ones the byte map
 * has to police.
 *
 * HAZARD: process-global and mutex-guarded, exactly like the rename seam it
 * replaces. Scope every installation with RAII and run no unrelated commit
 * inside that scope.
 */
using CommitFailpoint = std::function<std::string(PsdCommitStep)>;

/// @brief Installs the commit failpoint. An empty callback restores production behaviour.
void set_psd_commit_failpoint_for_testing(CommitFailpoint callback);

/**
 * @brief Returns a non-empty string to fail the ROLLBACK after it undoes @p step.
 *
 * A second, independent global. `commit_psd_reimport`'s `advance()` runs only in
 * the commit body, so a failpoint installed there can never fire while the
 * rollback is running -- and the rollback is the half AC3 is about. Without this
 * the only way to observe a broken rollback is through its byte map, which says
 * that something is wrong and never which undo step did it.
 *
 * It does NOT close the one irreversible window: a failure INSIDE
 * `adopt_runtime_sources` during the re-adopt of a rolled-back
 * `UpdateProvenance` needs a seam inside `EditorSession`, which this story does
 * not add. The seam exists, and it stops at the session boundary.
 */
using CommitRollbackFailpoint = std::function<std::string(PsdCommitStep)>;

/// @brief Installs the rollback failpoint. An empty callback restores production behaviour.
void set_psd_commit_rollback_failpoint_for_testing(CommitRollbackFailpoint callback);

} // namespace marrow::editor::detail
