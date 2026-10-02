#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/editor/diagnostics.hpp"
#include "marrow/editor/selection.hpp"

/**
 * @file problems_model.hpp
 * @brief UI-free grouping, filtering, row identity and navigation planning for
 *        the Problems view (MAR-187).
 *
 * Everything AC1 and AC2 decide lives here, in `marrow_editor`, which contains
 * no `shell_*.cpp` and links no ImGui. "UI-free" is therefore a property of the
 * build graph rather than a claim about the bodies: this translation unit
 * physically cannot reach `ShellState`, an ImGui symbol, or a window title.
 *
 * The shell's job on top of this is four writes and one focus request.
 *
 * @par What the compiler polices here
 * Clang enables `-Wswitch` by default with no flag (measured, not inferred).
 * Every dispatch below is written as an exhaustive `switch` with no `default:`
 * arm, and every switch over `ProblemsSeverityFilter` and `DiagnosticCode`
 * lives in `src/editor/problems_model.cpp`, so a future added value produces a
 * warning list that IS the checklist, complete for switches.
 *
 * What is NOT policed is a fixed-length list, and this module deliberately
 * contains none.
 */
namespace marrow::runtime {
class SkeletonData;
}  // namespace marrow::runtime

namespace marrow::editor {

/** @brief Which severities the view shows. */
enum class ProblemsSeverityFilter {
    All,
    ErrorsOnly,
    WarningsOnly,
};

/** @brief One severity's visible rows, as indices into `DiagnosticReport::issues`. */
struct ProblemsGroup {
    DiagnosticSeverity severity{DiagnosticSeverity::Error};
    /// Ascending indices into the report's issue list.
    std::vector<std::size_t> issue_indices;
};

/**
 * @brief What the Problems window draws for one report under one filter.
 *
 * Four decisions live in this shape, each with a mutation invisible to
 * everything else:
 *
 * 1. `error_count` and `warning_count` are **the report's**, never the visible
 *    rows'. AC1 says the view displays collector counts; under `ErrorsOnly` the
 *    view still reports how many warnings the project has, because that is the
 *    number the user filtered away. Re-deriving them from the rows makes the
 *    filtered one read `0`, and that is invisible under `All`.
 * 2. Groups are ordered `Error` then `Warning`, **not** by first appearance.
 *    The report is sorted by identity and identity begins with the code, so a
 *    project whose issues are `overlay.orphan_weight_target` (Warning) and
 *    `weights.uncanonicalizable` (Error) has a Warning first in report order.
 * 3. A group with no rows is **omitted**, never emitted empty, so "nothing to
 *    show" has one shape instead of two.
 * 4. A row's identity is the issue's identity, never its index. That is what
 *    lets a selected row survive a refresh that re-sorts the list.
 */
struct ProblemsView {
    /// Error group first. An empty group is omitted rather than emitted.
    std::vector<ProblemsGroup> groups;
    /// The collector's error count, unfiltered.
    std::size_t error_count{0};
    /// The collector's warning count, unfiltered.
    std::size_t warning_count{0};
    /// Rows this filter shows. The only member a filter moves.
    std::size_t visible_count{0};
    std::uint64_t project_revision{0};
    std::uint64_t runtime_revision{0};
};

/**
 * @brief Where activating an issue takes the user, decided without any UI.
 *
 * `selection` is **absent** when the issue names a target no runtime resolves.
 * That is not a detail: replacing the live selection with an identity nothing
 * resolves is actively harmful — `reconcile_selection_to_runtime` drops it at
 * the next adoption, the weight-paint context skips it while scanning, and in
 * between the Properties panel shows an attachment that is not there. So a
 * missing target leaves the existing selection alone and says what is gone.
 */
struct ProblemsNavigation {
    DiagnosticPanel panel{DiagnosticPanel::Project};
    /// Absent when `target_missing`, or when the issue names nothing selectable.
    std::optional<SelectionItem> selection;
    /// Empty when the issue is not animation-scoped.
    std::string animation_name;
    /// Set for weight issues only.
    std::optional<std::size_t> vertex_index;
    /// True when the issue carries a selection that no runtime resolves.
    bool target_missing{false};
    /// Non-empty exactly when `target_missing`.
    std::string missing_description;
};

/**
 * @brief Groups and filters one report into what the window draws.
 * @param report Collected report. Assumed sorted and strictly increasing by
 *        identity, which `collect_project_diagnostics` guarantees.
 * @param filter Which severities to show.
 * @return The view. Counts come from `report`, never from the visible rows.
 *
 * Never sorts. MAR-186 guarantees `issues` is already sorted and de-duplicated,
 * so each group's `issue_indices` are ascending by construction and the view
 * inherits determinism instead of re-establishing it. If this function ever
 * needs to sort, MAR-186 under-delivered.
 */
ProblemsView build_problems_view(
    const DiagnosticReport& report,
    ProblemsSeverityFilter filter);

/**
 * @brief Finds an issue by its stable identity.
 * @param report Report to search.
 * @param identity Identity remembered from an earlier view.
 * @return Index into `report.issues`, or `std::nullopt` when it is gone.
 *
 * This is how a selected row survives a refresh. An index-derived row identity
 * is stable within one build AND across two builds of an unchanged project, so
 * only an insertion that sorts BEFORE the remembered row exposes the
 * difference.
 */
std::optional<std::size_t> find_issue_by_identity(
    const DiagnosticReport& report,
    std::string_view identity);

/**
 * @brief Reports whether a cached view is stale.
 * @param cached View built from an earlier collection.
 * @param project_revision The session's current project revision.
 * @param runtime_revision The session's current runtime revision.
 * @return True when either revision has moved.
 *
 * Keyed on **both** revisions. `project_revision` is the obvious half; the half
 * AC3 is actually protecting is `runtime_revision`, because
 * `EditorSession::adopt_runtime_sources()` bumps the runtime and preview
 * revisions while leaving `project_revision` unmoved (measured). A hot reload
 * that drops a skin from the `.mskl` therefore creates a `preview.stale_skin`
 * with no project-revision change at all, and a view keyed on `project_revision`
 * alone shows a clean project over a broken one indefinitely.
 *
 * Note the asymmetry: an implementation that always returned `true` would be
 * behaviourally correct and merely re-collect every frame. The quiescence
 * property therefore needs its own assertion or nothing tests it.
 */
bool problems_view_needs_refresh(
    const ProblemsView& cached,
    std::uint64_t project_revision,
    std::uint64_t runtime_revision);

/**
 * @brief Decides where activating an issue should take the user.
 * @param issue Issue whose row was activated.
 * @param skeleton Materialized runtime the selection must resolve against.
 * @return The navigation plan. Applying it is the shell's job.
 *
 * Consults `selection_item_exists` for every issue carrying a selection. There
 * is exactly one issue class where that fires today —
 * `overlay.orphan_weight_target`, whose `AttachmentSelection` deliberately does
 * not resolve, because naming the missing triple is the only way the user can
 * see what is orphaned.
 */
ProblemsNavigation plan_issue_navigation(
    const DiagnosticIssue& issue,
    const runtime::SkeletonData& skeleton);

}  // namespace marrow::editor
