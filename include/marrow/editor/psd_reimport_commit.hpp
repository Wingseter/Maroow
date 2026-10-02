#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "marrow/editor/psd_reimport_plan.hpp"
#include "marrow/editor/session.hpp"

namespace marrow::editor {

/**
 * @brief Every step a PSD reimport commit performs, in execution order.
 *
 * This enum is the ONLY list. `kAllCommitSteps` iterates it, the ledger records
 * it, and the failpoint seam is keyed on it, so a step that exists cannot be
 * absent from the sweep that has to cover "an injected failure after every
 * commit step". A `static_assert` against a hand-maintained count was rejected:
 * `AGENTS.md` records that shape as a proven tautology that catches nothing.
 *
 * It lives in the PUBLIC header rather than beside the test seam, because it is
 * the element type of `PsdReimportCommitResult::steps_executed`. A caller that
 * cannot name the type cannot read the result.
 */
enum class PsdCommitStep {
    ValidateRequest,
    PruneUnpreserved,
    ValidateStagedBundle,
    OpenJournal,
    BackupLayers,
    BackupTexture,
    BackupAtlas,
    BackupSkeleton,
    PlaceLayers,
    PlaceTexture,
    PlaceAtlas,
    PlaceSkeleton,
    AdoptRuntimeSources,
    UpdateProvenance,
    CleanJournal,
};

/// @brief Every step, in execution order. The only list.
extern const std::array<PsdCommitStep, 15> kAllCommitSteps;

/// @brief Names a commit step for an error message or a ledger dump.
/// @param step Step to name.
/// @return A stable identifier matching the enumerator's own spelling.
const char* psd_commit_step_name(PsdCommitStep step);

/** @brief Where the commit writes, and what it is allowed to update. */
struct PsdReimportCommitOptions {
    /// @brief Project file the stored provenance paths are relativized against.
    /// Empty means "the session's own `source_path`".
    std::filesystem::path project_path;
    /// @brief When false, the commit stops after adoption and leaves provenance alone.
    bool update_provenance{true};
};

/**
 * @brief What a commit did, and -- when it failed -- what it undid.
 *
 * `steps_executed` records a step only AFTER its body succeeded, which is what
 * makes "the failing step is absent from the ledger" a meaningful assertion
 * rather than a restatement of the enum.
 */
struct PsdReimportCommitResult {
    bool ok{false};
    /**
     * @brief Names the step and the cause.
     *
     * `ok` is the verdict and this is the diagnostic; they are NOT complements.
     * A failure after `CleanJournal` sets both -- the reimport is committed and
     * only the journal cleanup is in question, and rolling a completed reimport
     * back over that would be the destructive answer.
     */
    std::string error;
    std::optional<PsdCommitStep> failed_step;
    std::vector<PsdCommitStep> steps_executed;
    /// @brief Steps whose effect the rollback undid, in the order it undid them.
    std::vector<PsdCommitStep> steps_rolled_back;
    /// @brief The target bundle is as it was. False on the success path.
    bool rolled_back{false};
    /// @brief Set only when the rollback itself failed; the bundle is then unknown.
    std::string rollback_error;
    /// @brief Backups or manifests `CleanJournal` could not remove. Never fatal.
    std::vector<std::filesystem::path> journal_residue;

    /// @brief Reports whether the commit completed.
    /// @return `true` when the reimport was committed; otherwise `false`.
    explicit operator bool() const {
        return ok;
    }
};

/**
 * @brief Commits a planned PSD reimport onto the session's own project bundle.
 *
 * Takes the PLAN, never a PSD path: the bundle that is committed is the bundle
 * that was reviewed, because it is the same files on disk. Nothing here
 * re-parses and nothing re-packs.
 *
 * Every target is moved aside before anything is written, so a rollback returns
 * the ORIGINAL bytes rather than a re-encoding of them -- the byte-for-byte
 * guarantee is a property of the mechanism, not of a writer's fidelity.
 *
 * @param session Session whose project bundle is replaced and whose runtime is re-adopted.
 * @param plan A successful plan whose staged artefacts still exist on disk.
 * @param options Provenance base path and whether provenance is updated.
 * @return The step ledger plus, on failure, what was rolled back.
 */
PsdReimportCommitResult commit_psd_reimport(
    EditorSession& session,
    const PsdReimportPlan& plan,
    const PsdReimportCommitOptions& options);

} // namespace marrow::editor
