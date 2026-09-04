#include "shell_psd_reimport.hpp"

#include <algorithm>
#include <string>
#include <system_error>

#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif

#include "imgui.h"
#include "marrow/editor/psd_reimport_review.hpp"

namespace marrow::editor::shell {
namespace {

/**
 * @brief A staging root for the re-plan that no other process can collide with.
 *
 * `plan_psd_reimport` REFUSES a non-empty staging root, and
 * `unique_staging_directory` numbers from a `static` counter that restarts at 1
 * in every process. Two editors each confirming a reimport under one fixed root
 * therefore both pick `<root>/plan-1`, and the second user's confirmation fails
 * with "staging root must be empty or absent" for a reason they cannot act on.
 * **A "unique" name is unique only across the scope its mechanism spans**, so the
 * pid is part of the name -- written here at the point the directory is created,
 * not added later after a flake.
 */


/** @brief `Added (n)` / `Updated (n)` / `Missing (n)`, in that order. */
void draw_section(
    const char* title,
    const PsdReimportPlan& plan,
    const std::vector<std::size_t>& indices,
    bool missing_section,
    PsdReimportReview* review) {
    ImGui::TextUnformatted(
        (std::string(title) + " (" + std::to_string(indices.size()) + ")").c_str());
    for (const std::size_t index : indices) {
        const PsdPlannedLayer& layer = plan.layers[index];
        // Rows at PLAIN MODAL SCOPE with explicit `##<identity>` suffixes. No
        // child, no table, no pushed id -- see the header.
        const std::string row_label = layer.identity + "##psd_row_" + layer.identity;
        // An explicit width. A default `Selectable` spans the whole content
        // region, which pushes anything after `SameLine()` past the window's
        // right edge, where it is unreachable by a mouse and invisible to a
        // user. MAR-187 measured exactly that; F2 asserts the checkbox's x lies
        // INSIDE the usable edge so the repair is measured, not assumed.
        const float label_width =
            std::max(40.0f, ImGui::GetContentRegionAvail().x - kPsdReviewControlColumn);
        ImGui::Selectable(
            row_label.c_str(), false, ImGuiSelectableFlags_None,
            ImVec2(label_width, 0.0f));
        if (!missing_section) {
            continue;
        }
        ImGui::SameLine();
        // "Forget mapping", never "Delete". The reimport removes this layer's
        // slot and attachment EITHER WAY -- the importer assigns `slots`
        // wholesale and erases `skins` -- so a box labelled Delete would let a
        // user believe leaving it unticked keeps their rig data. It does not.
        // What the box controls is whether the project keeps REMEMBERING the
        // layer's mapping.
        bool forget = false;
        for (const std::string& identity : review->delete_identities) {
            if (identity == layer.identity) {
                forget = true;
                break;
            }
        }
        const std::string box_label = "Forget##psd_forget_" + layer.identity;
        if (ImGui::Checkbox(box_label.c_str(), &forget)) {
            set_psd_review_deletion(review, layer.identity, forget);
        }
    }
}

} // namespace

std::filesystem::path psd_reimport_staging_root() {
#ifdef _WIN32
    const long long pid = static_cast<long long>(_getpid());
#else
    const long long pid = static_cast<long long>(::getpid());
#endif
    static int sequence = 0;
    // The pid separates PROCESSES; the counter separates repeated opens within
    // one. Either alone collides -- a bare pid reuses the root on the second
    // open of the same session, which the planner refuses as non-empty.
    ++sequence;
    return std::filesystem::temp_directory_path() /
        ("marrow_psd_staging-" + std::to_string(pid) + "-" + std::to_string(sequence));
}

void begin_psd_reimport_review(
    ShellState* state,
    const PsdReimportPlan& plan,
    const std::filesystem::path& staging,
    const std::filesystem::path& source) {
    if (state == nullptr) {
        return;
    }
    PsdReimportReview review;
    review.plan = plan;
    review.plan_digest = psd_review_plan_digest(plan);
    review.source_path = source;
    state->psd_reimport.review = std::move(review);
    state->psd_reimport.staging_root = staging;
    state->psd_reimport.source_path = source;
    state->psd_reimport.open = true;
    state->psd_reimport.last_outcome.clear();
}

void close_psd_reimport_review(ShellState* state, PsdReviewOutcome outcome) {
    if (state == nullptr) {
        return;
    }
    if (!state->psd_reimport.staging_root.empty()) {
        std::error_code error;
        std::filesystem::remove_all(state->psd_reimport.staging_root, error);
    }
    state->psd_reimport.review.reset();
    state->psd_reimport.open = false;
    state->psd_reimport.staging_root.clear();
    state->psd_reimport.source_path.clear();
    state->psd_reimport.last_outcome = psd_review_outcome_text(outcome);
}

void draw_psd_reimport_modal(ShellState* state) {
    if (state == nullptr || !state->psd_reimport.review.has_value()) {
        return;
    }
    if (state->psd_reimport.open && !ImGui::IsPopupOpen(kPsdReimportModal)) {
        ImGui::OpenPopup(kPsdReimportModal);
    }

    // NOT `AlwaysAutoResize`: a long plan must scroll the MODAL rather than an
    // inner child, because a child would break the id seed every sweep depends on.
    ImGui::SetNextWindowSizeConstraints(ImVec2(420.0f, 200.0f), ImVec2(900.0f, 640.0f));

    // The `bool*` is REQUIRED by AC3, and it is passed here rather than when a
    // case needs it. Escape cannot close a modal, so without a `p_open` there is
    // no "modal close" path distinct from Cancel and one fifth of AC3 is
    // unimplementable rather than merely unimplemented.
    if (!ImGui::BeginPopupModal(kPsdReimportModal, &state->psd_reimport.open,
                                ImGuiWindowFlags_NoSavedSettings)) {
        // `BeginPopupModal` returns false and closes the popup when `p_open`
        // goes false, so this is the modal-close path arriving.
        if (!state->psd_reimport.open && state->psd_reimport.review.has_value()) {
            close_psd_reimport_review(state, PsdReviewOutcome::Closed);
        }
        return;
    }

    PsdReimportReview& review = *state->psd_reimport.review;

    ImGui::TextUnformatted("Source PSD:");
    ImGui::SameLine();
    ImGui::TextUnformatted(state->psd_reimport.source_path.generic_string().c_str());
    ImGui::TextUnformatted(
        ("Layers the project remembers: " + std::to_string(review.plan.layers.size()))
            .c_str());
    ImGui::Separator();

    const PsdReviewSections sections = group_psd_review(review.plan);
    draw_section("Added", review.plan, sections.added, false, &review);
    draw_section("Updated", review.plan, sections.updated, false, &review);
    if (!sections.missing.empty()) {
        ImGui::Separator();
        // Both halves, because either alone misleads: the slot goes regardless,
        // and forgetting means a returning layer comes back as a NEW layer.
        ImGui::TextWrapped(
            "These layers are no longer in the PSD. Their slots and attachments are "
            "removed by the reimport either way. Leave unticked to keep remembering "
            "where each one used to map -- it will keep appearing here on future "
            "reimports. Tick to forget the mapping; if the layer ever returns it "
            "arrives as a new layer.");
    }
    draw_section("Missing", review.plan, sections.missing, true, &review);

    ImGui::Separator();
    {
        const bool can_confirm = psd_review_can_confirm(review);
        ImGui::BeginDisabled(!can_confirm);
        if (ImGui::Button("Confirm##psd_confirm")) {
            // The button calls `apply_psd_reimport_review` and nothing else.
            // That function is the editor path's ONE call site for
            // `commit_psd_reimport`; with two, a button wired to the wrong one
            // would be invisible.
            PsdReimportReviewOptions options;
            options.project_path = state->session.has_project()
                ? state->session.project()->source_path
                : std::filesystem::path();
            options.restage_root = psd_reimport_staging_root();
            const PsdReviewApplyResult applied =
                apply_psd_reimport_review(state->session, review, options);
            state->status_message = applied.outcome == PsdReviewOutcome::Committed
                ? std::string(psd_review_outcome_text(applied.outcome))
                : applied.error;
            ImGui::CloseCurrentPopup();
            // EndDisabled BEFORE EndPopup, and before the early return: this
            // branch is inside the BeginDisabled block, so returning without it
            // aborts with "Missing EndDisabled()". A real click found it; no
            // UI-free case could, because none of them enters this scope.
            ImGui::EndDisabled();
            ImGui::EndPopup();
            close_psd_reimport_review(state, applied.outcome);
            // A committed reimport reaches `EditorSession::adopt_runtime_sources`,
            // which replaces the PreviewController and destroys the Skeleton and
            // AnimationState that `preview_skeleton` and `animation_state` alias.
            // This modal is drawn from `draw_project_window`, the FIRST window in
            // both frame bodies, so timeline, hierarchy, viewport and inspector all
            // still dereference those aliases later in THIS frame -- and
            // `load_result` is a reference into the session, so without this the
            // viewport pairs the NEW atlas against the freed skeleton. Ordering
            // against `close_psd_reimport_review` is free: the two touch disjoint
            // fields.
            sync_shell_from_editor_session(state);
            return;
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel##psd_cancel")) {
        // Closes the popup AND disengages the review. Doing only the first
        // leaves a stuck review that blocks every later interaction.
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        close_psd_reimport_review(state, PsdReviewOutcome::Cancelled);
        return;
    }

    ImGui::EndPopup();
}

} // namespace marrow::editor::shell
