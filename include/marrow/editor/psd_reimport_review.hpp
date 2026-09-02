#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/editor/psd_reimport_commit.hpp"
#include "marrow/editor/psd_reimport_plan.hpp"
#include "marrow/editor/session.hpp"

/**
 * @file
 * @brief MAR-190. The reviewable form of a PSD reimport plan, with no UI in it.
 *
 * This header belongs to `marrow_editor`, which links no UI toolkit and contains
 * no `shell_*.cpp`, so the model layer *physically cannot* name a widget. The
 * shell reads a review, emits rows, and on a click calls one function here.
 *
 * Task 9's S5 greps this file and its `.cpp` for the widget library's name and
 * expects zero hits, so the boundary is measured rather than promised. Naming it
 * even in a comment reddens that gate -- deliberately: a gate with a prose
 * exception is one somebody later widens into a code exception.
 */

namespace marrow::editor {

/**
 * @brief AC1's three groups, computed from a plan and never stored beside it.
 *
 * Each vector holds indices into `plan.layers`, ascending. Because
 * `plan.layers` is itself lexicographic by `identity` ascending
 * (`plan_psd_reimport` walks a `std::set` of identities), ascending indices and
 * ascending identities are the same order, and the three lists partition
 * `plan.layers` exactly: their union is every index and they are pairwise
 * disjoint.
 */
struct PsdReviewSections {
    std::vector<std::size_t> added;
    std::vector<std::size_t> updated;
    std::vector<std::size_t> missing;
};

/**
 * @brief Groups a plan's layers by change kind, preserving the plan's own order.
 *
 * The grouping lives here rather than in the drawing code so a UI-free case can
 * assert it. A local filter inside the modal would be observable only by
 * rendering a frame.
 *
 * @param plan Plan whose `layers` are grouped. An empty plan yields empty lists.
 * @return Three ordered index lists that partition `plan.layers`.
 */
PsdReviewSections group_psd_review(const PsdReimportPlan& plan);

/**
 * @brief One reviewed reimport, from the moment a plan is shown to the moment
 *        it is confirmed or abandoned.
 */
struct PsdReimportReview {
    PsdReimportPlan plan;               ///< Exactly as reviewed.
    std::string plan_digest;            ///< Of `plan`, at review time.
    std::filesystem::path staging_root; ///< What `plan` staged into.
    std::filesystem::path source_path;  ///< Absolute; what AC1 displays.

    /**
     * @brief EXACTLY the `Missing` identities the user ticked to FORGET.
     *
     * Empty by construction. AC2's default is the ABSENCE of an entry, not a
     * `false` stored somewhere that a one-character mutation can flip: a
     * parallel `std::vector<bool>` sized to `plan.layers` would put the default
     * in an initialiser, while a set that starts empty has no initialiser to
     * invert. Making the default "forget" requires populating this from the
     * `Missing` section, which is a visible edit.
     *
     * Named for what it controls: dropping the layer's row from the project's
     * stored provenance (`provenance_from_plan`, `psd_reimport_commit.cpp:251`).
     * It does **not** delete a slot. A `Missing` layer's slot and attachment are
     * removed by the reimport in EITHER direction, because the importer assigns
     * `slots` wholesale from the newly parsed PSD and erases `skins`
     * (`psd_import.cpp:1040-1041`). Do not write a case asserting that a
     * preserved layer's slot survives the commit, and do not label the checkbox
     * that feeds this "Delete".
     */
    std::vector<std::string> delete_identities;
};

/**
 * @brief Adds or removes one identity from the review's forget set.
 *
 * @param review   Review to update. Must not be null.
 * @param identity A `PsdPlannedLayer::identity`. Unknown identities are stored
 *                 as given; `apply_psd_reimport_review` is what rejects one that
 *                 the re-plan no longer carries.
 * @param deleted  `true` to forget the mapping, `false` to keep remembering it.
 *                 Setting `false` on an identity that is not present is a no-op.
 */
void set_psd_review_deletion(
    PsdReimportReview* review, std::string_view identity, bool deleted);

/**
 * @brief The review's forget set, sorted ascending and deduplicated.
 *
 * Returns the whole set because AC2's cases compare the whole set. A count is
 * not the fact: "two missing layers, two choices" reads as consistent whether
 * the choices are the right two or the wrong two.
 *
 * @param review Review to read.
 * @return Ascending, unique identities.
 */
std::vector<std::string> chosen_psd_deletions(const PsdReimportReview& review);

/**
 * @brief Whether confirmation may be offered for this review.
 *
 * False while the plan carries an error, and false while the plan has nothing to
 * do in any of the three categories. It does **not** gate on the user having
 * scrolled or acknowledged anything: no acceptance criterion asks for that, and
 * a gate no headless case can satisfy is a gate that gets deleted.
 *
 * @param review Review to test.
 * @return `true` when the review is confirmable.
 */
bool psd_review_can_confirm(const PsdReimportReview& review);

/**
 * @brief The digest of @p plan, by which a review is judged stale.
 *
 * **This forwards to MAR-189's `agent_detail::psd_plan_digest` and defines no
 * field list of its own.** That function is declared in
 * `src/editor/agent_dispatch_internal.hpp`, a `src/editor/` internal header in
 * the same target as this one, so it is reachable without being public. MAR-190
 * asked for it in a public header and did not get one; forwarding is strictly
 * better than the fallback of a second hand-maintained tuple over
 * `PsdPlannedLayer`, which nothing would keep in agreement.
 *
 * **What it covers, and what it does not.** The digest is the ordered
 * `(identity, change)` row list -- TWO fields per layer, not the ten this
 * story's design originally specified. `change` is computed from stored
 * provenance against the PSD's candidates, so the digest does move when a layer
 * is added, vanishes, or is reclassified. It does **not** move when a row's
 * displayed mapping changes without its identity or classification changing: a
 * provenance edit renaming a stored slot leaves the modal showing
 * `current_slot: X` while the commit uses `current_slot: Y`, with no staleness
 * reported.
 *
 * That gap is inherited deliberately rather than repaired here. A stricter
 * MAR-190-local digest would make the same edit stale on the editor path and not
 * on the agent path, for the same operation, and an inconsistent staleness
 * contract between two entry points is worse than one recorded gap.
 *
 * @param plan Plan to digest.
 * @return The ordered `(identity, change)` row list, as a string.
 */
std::string psd_review_plan_digest(const PsdReimportPlan& plan);

/**
 * @brief The plan MAR-189's committer is handed, derived from the review.
 *
 * Copies `review.plan` and sets `preserve = false` on exactly the layers whose
 * `identity` is in the review's forget set, leaving every other layer at
 * MAR-188's `true`. An identity in the set that names no layer is ignored here;
 * `apply_psd_reimport_review` is where a stale choice becomes an outcome.
 *
 * **Returned BY VALUE, and that is a correctness requirement rather than a style
 * preference.** The obvious alternative -- an `annotate_deletions(&review->plan)`
 * that mutates in place, with the caller then passing `review.plan` -- makes the
 * natural inversion (*"the caller passes the un-annotated plan"*) a PROVABLE
 * NO-OP: both expressions name one object, nothing cross-reads, and the mutation
 * cannot be observed. By value gives the two a distinguishable identity and
 * makes that inversion real.
 *
 * What `preserve` actually controls is narrow, and the caller must not promise
 * more: it decides whether the layer keeps its row in the project's stored
 * provenance (`provenance_from_plan`). It does NOT retain the layer's slot or
 * attachment -- a reimport removes those in either direction, because the
 * importer assigns `slots` wholesale and erases `skins`
 * (`psd_import.cpp:1040-1041`).
 *
 * @param review Review whose plan and forget set are combined.
 * @return A copy of `review.plan` with `preserve` set from the forget set.
 */
PsdReimportPlan build_psd_commit_plan(const PsdReimportReview& review);

/** @brief How a review ended. */
enum class PsdReviewOutcome {
    Committed,   ///< The reimport was committed.
    Cancelled,   ///< The user pressed Cancel. Shell-side; never returned here.
    Closed,      ///< The user closed the modal. Shell-side; never returned here.
    Stale,       ///< The plan no longer describes the PSD or the project.
    PlanFailed,  ///< Re-planning refused.
    CommitFailed ///< The commit refused or failed and rolled back.
};

/**
 * @brief Human-readable text for @p outcome.
 *
 * One exhaustive `switch` with no `default:`, so `-Wswitch` names every site if a
 * seventh outcome is added. `AGENTS.md` records that Clang warns without a flag
 * and does not fail, and that GCC is silent without `-Wall`, so this is a
 * convenience rather than a gate.
 *
 * @param outcome Outcome to describe.
 * @return A short status string.
 */
const char* psd_review_outcome_text(PsdReviewOutcome outcome);

/** @brief Where provenance is relativized against, and where the re-plan may write. */
struct PsdReimportReviewOptions {
    /// @brief Project file the stored provenance paths are relativized against.
    std::filesystem::path project_path;
    /**
     * @brief A fresh, empty directory the re-plan stages into.
     *
     * Removed on EVERY exit path, success or failure. The caller owns making it
     * unique: `plan_psd_reimport` refuses a non-empty staging root, so two
     * processes sharing one fixed root collide and the second is refused for a
     * reason its user cannot act on. **A "unique" name is unique only across the
     * scope its mechanism spans** -- a per-process counter is not unique between
     * processes. Include the pid.
     */
    std::filesystem::path restage_root;
};

/** @brief What applying a review did. */
struct PsdReviewApplyResult {
    PsdReviewOutcome outcome{PsdReviewOutcome::CommitFailed};
    /// @brief Never empty unless `outcome` is `Committed`.
    std::string error;
    /// @brief Present iff a commit was attempted.
    std::optional<PsdReimportCommitResult> commit;

    /// @brief Reports whether the reimport was committed.
    /// @return `true` when `outcome` is `Committed`; otherwise `false`.
    explicit operator bool() const {
        return outcome == PsdReviewOutcome::Committed;
    }
};

/**
 * @brief Re-plans, checks the review is still current, and commits the choices.
 *
 * **This is the only function on the editor path that calls
 * `commit_psd_reimport`.** The agent path has its own call site in
 * `agent_dispatch.cpp`, which is a different entry point by design. Task 9's S2
 * asserts this one is unique within `src/editor/` excluding both owners; with two
 * editor call sites, a button wired to the wrong one would be invisible.
 *
 * The reimport is re-planned rather than committed from the reviewed plan,
 * because the PSD or the project may have changed while the modal was open. The
 * plan the user saw is used only as the thing the re-plan is compared against.
 *
 * Steps, in order:
 *   1. Re-plan `review.source_path` into `options.restage_root`. Error -> `PlanFailed`.
 *   2. Digest the re-plan and compare with `review.plan_digest`. Differs -> `Stale`.
 *   3. Carry the review's forget set onto the re-planned plan by identity.
 *   4. `commit_psd_reimport`. Failure -> `CommitFailed`, carrying its result.
 *   5. Remove `options.restage_root` on every exit path.
 *
 * `Cancelled` and `Closed` never come from here; they are shell-side transitions
 * that discard the review without calling this at all.
 *
 * @param session Session whose bundle is replaced. Untouched unless the commit runs.
 * @param review  The reviewed plan, its digest, and the user's forget set.
 * @param options Provenance base path and the re-plan's staging root.
 * @return The outcome, a message on every non-success path, and the commit ledger.
 */
PsdReviewApplyResult apply_psd_reimport_review(
    EditorSession& session,
    const PsdReimportReview& review,
    const PsdReimportReviewOptions& options);

} // namespace marrow::editor
