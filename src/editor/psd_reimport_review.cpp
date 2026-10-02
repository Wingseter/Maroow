#include "marrow/editor/psd_reimport_review.hpp"

#include <algorithm>

#include "agent_dispatch_internal.hpp"
#include "marrow/runtime/json.hpp"

namespace marrow::editor {
namespace {

/**
 * @brief The bare PNG name an atlas document points at, or empty.
 *
 * The staged bundle must be named after the TARGET, not after the planner's
 * defaults. `commit_psd_reimport`'s `ValidateRequest` refuses a staged atlas
 * whose `image` disagrees with the target atlas's, because placement is a byte
 * copy and `image` resolves against the file's own directory -- a staged
 * `staged.matl` says `"image": "staged.png"` and, copied onto the project's
 * atlas, is byte-perfect while naming a file that is not there.
 */
std::string atlas_image_name(const std::filesystem::path& atlas_path) {
    const runtime::json::LoadResult loaded = runtime::json::load_document(atlas_path);
    if (!loaded) {
        return {};
    }
    const runtime::json::Value* atlas =
        runtime::json::find_member(loaded.document->root, "atlas");
    if (atlas == nullptr || !atlas->is_object()) {
        return {};
    }
    const runtime::json::Value* image = runtime::json::find_member(*atlas, "image");
    if (image == nullptr || !image->is_string()) {
        return {};
    }
    return image->as_string();
}

} // namespace

std::string psd_review_plan_digest(const PsdReimportPlan& plan) {
    // Forwarded, never reimplemented. A second field list over `PsdPlannedLayer`
    // would drift the moment a field is added, and no inversion can prove two
    // lists agree. See the header for what this digest does and does not cover.
    return agent_detail::psd_plan_digest(plan);
}

PsdReviewSections group_psd_review(const PsdReimportPlan& plan) {
    PsdReviewSections sections;
    // Forward iteration, deliberately. `plan.layers` is lexicographic by
    // `identity` ascending, so walking it in order gives each section both
    // ascending indices and ascending identities in one pass -- no sort here,
    // and none needed. Iterating in reverse is what V1's element-wise clause
    // exists to catch.
    for (std::size_t index = 0; index < plan.layers.size(); ++index) {
        switch (plan.layers[index].change) {
            case PsdLayerChangeKind::Added:
                sections.added.push_back(index);
                break;
            case PsdLayerChangeKind::Updated:
                sections.updated.push_back(index);
                break;
            case PsdLayerChangeKind::Missing:
                sections.missing.push_back(index);
                break;
        }
    }
    return sections;
}

void set_psd_review_deletion(
    PsdReimportReview* review, std::string_view identity, bool deleted) {
    if (review == nullptr) {
        return;
    }
    const auto found = std::find(
        review->delete_identities.begin(), review->delete_identities.end(), identity);
    if (deleted) {
        if (found == review->delete_identities.end()) {
            review->delete_identities.emplace_back(identity);
        }
        return;
    }
    if (found != review->delete_identities.end()) {
        review->delete_identities.erase(found);
    }
}

std::vector<std::string> chosen_psd_deletions(const PsdReimportReview& review) {
    std::vector<std::string> chosen = review.delete_identities;
    std::sort(chosen.begin(), chosen.end());
    chosen.erase(std::unique(chosen.begin(), chosen.end()), chosen.end());
    return chosen;
}

PsdReimportPlan build_psd_commit_plan(const PsdReimportReview& review) {
    // A copy, returned by value. See the header: mutating `review.plan` in place
    // would make "the caller passed the un-annotated plan" unobservable.
    PsdReimportPlan derived = review.plan;
    for (PsdPlannedLayer& layer : derived.layers) {
        const bool forget =
            std::find(
                review.delete_identities.begin(), review.delete_identities.end(),
                layer.identity) != review.delete_identities.end();
        if (forget) {
            layer.preserve = false;
        }
    }
    return derived;
}

bool psd_review_can_confirm(const PsdReimportReview& review) {
    if (review.plan.error.has_value()) {
        return false;
    }
    return !review.plan.layers.empty();
}

const char* psd_review_outcome_text(PsdReviewOutcome outcome) {
    // No `default:`. A seventh outcome makes Clang name this site.
    switch (outcome) {
        case PsdReviewOutcome::Committed:
            return "Reimport committed.";
        case PsdReviewOutcome::Cancelled:
            return "Reimport cancelled; nothing was changed.";
        case PsdReviewOutcome::Closed:
            return "Reimport dismissed; nothing was changed.";
        case PsdReviewOutcome::Stale:
            return "The PSD or the project changed while the review was open; "
                   "nothing was changed.";
        case PsdReviewOutcome::PlanFailed:
            return "The PSD could not be re-planned; nothing was changed.";
        case PsdReviewOutcome::CommitFailed:
            return "The reimport failed and was rolled back; nothing was changed.";
    }
    return "Unknown reimport outcome.";
}

PsdReviewApplyResult apply_psd_reimport_review(
    EditorSession& session,
    const PsdReimportReview& review,
    const PsdReimportReviewOptions& options) {
    PsdReviewApplyResult result;

    // Step 5, armed FIRST so it covers every exit path including the early
    // returns below. A `remove_all` written at the end of the happy path is one
    // `return` away from leaking the staging tree, and staging is not a target,
    // so no byte-map clause anywhere would notice.
    struct RestageCleanup {
        std::filesystem::path root;
        ~RestageCleanup() {
            if (root.empty()) {
                return;
            }
            std::error_code error;
            std::filesystem::remove_all(root, error);
        }
    } cleanup{options.restage_root};

    if (!session.has_project() || session.project() == nullptr) {
        result.outcome = PsdReviewOutcome::PlanFailed;
        result.error = "no project is open";
        return result;
    }

    // -- Step 1: re-plan. The PSD may have changed while the modal was open, so
    // the committed bundle is the one that was just planned, never the reviewed
    // one. The reviewed plan is only the thing this is compared against.
    const ProjectData& project = *session.project();
    const std::vector<std::filesystem::path> atlases = project.resolved_atlas_paths();
    if (atlases.empty()) {
        result.outcome = PsdReviewOutcome::PlanFailed;
        result.error = "the project references no atlas";
        return result;
    }
    const std::string target_image = atlas_image_name(atlases.front());
    if (target_image.empty()) {
        result.outcome = PsdReviewOutcome::PlanFailed;
        result.error = "the project's atlas declares no usable 'image' member";
        return result;
    }

    PsdReimportPlanOptions plan_options;
    plan_options.psd_path = review.source_path;
    plan_options.staging_root = options.restage_root;
    // Stage under the TARGET's own names. Left at the planner's defaults, the
    // staged atlas would say `"image": "staged.png"` and `ValidateRequest` would
    // refuse the commit -- so this is what makes step 4 reachable at all, not a
    // tidiness measure. The atlas name comes from the CURRENT TEXTURE's stem
    // rather than the atlas's, because the packer writes the atlas document's
    // `image` from the staged atlas path's stem.
    plan_options.staged_skeleton_filename =
        project.resolved_skeleton_path().filename().generic_string();
    plan_options.staged_atlas_filename =
        std::filesystem::path(target_image).stem().generic_string() + ".matl";
    const PsdReimportPlan replanned = plan_psd_reimport(project, plan_options);
    if (!replanned) {
        result.outcome = PsdReviewOutcome::PlanFailed;
        result.error = replanned.error->format();
        return result;
    }

    // -- Step 2: staleness. Covers both inputs, because `change` is computed
    // from the stored provenance against the PSD's candidates.
    const std::string current_digest = psd_review_plan_digest(replanned);
    if (current_digest != review.plan_digest) {
        result.outcome = PsdReviewOutcome::Stale;
        result.error =
            "the reviewed plan no longer matches the PSD and the project; "
            "review the reimport again";
        return result;
    }

    // -- Step 3: carry the forget set onto the RE-PLANNED plan, by identity.
    PsdReimportReview carried;
    carried.plan = replanned;
    carried.delete_identities = review.delete_identities;
    for (const std::string& identity : carried.delete_identities) {
        const auto found = std::find_if(
            replanned.layers.begin(), replanned.layers.end(),
            [&identity](const PsdPlannedLayer& layer) { return layer.identity == identity; });
        if (found == replanned.layers.end()) {
            // Defensive and, with the current digest, UNREACHABLE: the digest is
            // the ordered identity list, so an identity that vanished or was
            // reclassified already failed step 2. Kept because a future digest
            // change could reach it; NOT claimed as a detector, and the register
            // records the corresponding inversion as subsumed rather than run.
            result.outcome = PsdReviewOutcome::Stale;
            result.error = "the chosen layer '" + identity +
                "' is no longer in the plan; review the reimport again";
            return result;
        }
    }

    // -- Step 4: commit. THE editor path's only call site.
    PsdReimportCommitOptions commit_options;
    commit_options.project_path = options.project_path;
    const PsdReimportPlan commit_plan = build_psd_commit_plan(carried);
    result.commit = commit_psd_reimport(session, commit_plan, commit_options);
    if (!result.commit->ok) {
        result.outcome = PsdReviewOutcome::CommitFailed;
        result.error = result.commit->error;
        return result;
    }

    result.outcome = PsdReviewOutcome::Committed;
    return result;
}

} // namespace marrow::editor
