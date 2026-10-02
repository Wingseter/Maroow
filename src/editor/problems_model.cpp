#include "marrow/editor/problems_model.hpp"

#include <utility>

#include "marrow/runtime/skeleton.hpp"

namespace marrow::editor {

namespace {

/**
 * @brief Whether a filter shows a severity.
 *
 * A `switch` rather than an if/else chain, and with no `default:`. Clang's
 * default `-Wswitch` then makes a fourth filter value a warning at this site,
 * which is the only mechanism that finds it -- a chain would compile silently
 * and quietly show nothing.
 */
bool filter_admits(ProblemsSeverityFilter filter, DiagnosticSeverity severity) {
    switch (filter) {
    case ProblemsSeverityFilter::All:
        return true;
    case ProblemsSeverityFilter::ErrorsOnly:
        return severity == DiagnosticSeverity::Error;
    case ProblemsSeverityFilter::WarningsOnly:
        return severity == DiagnosticSeverity::Warning;
    }
    return true;
}

}  // namespace

ProblemsView build_problems_view(
    const DiagnosticReport& report,
    ProblemsSeverityFilter filter) {
    ProblemsView view;
    // The counts are copied from the REPORT and never re-derived from the rows
    // this filter kept. Under `ErrorsOnly` the view still reports how many
    // warnings the project has, because that is the number the user filtered
    // away. The two derivations agree under `All`, which is why the difference
    // is only observable in a case that reads them under a narrowing filter.
    view.error_count = report.error_count;
    view.warning_count = report.warning_count;
    view.project_revision = report.project_revision;
    view.runtime_revision = report.runtime_revision;

    // Fixed order, Error then Warning -- NOT first-encountered order. The report
    // is sorted by identity and identity begins with the code, so a project
    // whose lowest identity is a Warning would otherwise show its Warning group
    // first.
    const DiagnosticSeverity severities[] = {
        DiagnosticSeverity::Error, DiagnosticSeverity::Warning};
    for (const DiagnosticSeverity severity : severities) {
        if (!filter_admits(filter, severity)) {
            continue;
        }
        ProblemsGroup group;
        group.severity = severity;
        for (std::size_t index = 0; index < report.issues.size(); ++index) {
            if (report.issues[index].severity == severity) {
                group.issue_indices.push_back(index);
            }
        }
        // An empty group is OMITTED rather than emitted empty, so "nothing to
        // show" has one shape instead of two.
        if (group.issue_indices.empty()) {
            continue;
        }
        view.visible_count += group.issue_indices.size();
        view.groups.push_back(std::move(group));
    }

    // No sort. `collect_project_diagnostics` guarantees `issues` is sorted and
    // strictly increasing by identity, so each group's indices are ascending by
    // construction. If this function ever needs to sort, MAR-186 under-delivered.
    return view;
}

std::optional<std::size_t> find_issue_by_identity(
    const DiagnosticReport& report,
    std::string_view identity) {
    // A LINEAR scan, deliberately, over a list MAR-186 guarantees is sorted and
    // strictly increasing. A binary search would be correct under that
    // guarantee and is the obvious choice for a sorted range -- but it would
    // also return `nullopt` for an identity that IS present the moment the
    // guarantee is violated, and the failure would surface here, in MAR-187,
    // blaming the wrong story. The list is tens of entries; there is nothing to
    // buy with the coupling.
    for (std::size_t index = 0; index < report.issues.size(); ++index) {
        if (report.issues[index].identity == identity) {
            return index;
        }
    }
    return std::nullopt;
}

bool problems_view_needs_refresh(
    const ProblemsView& cached,
    std::uint64_t project_revision,
    std::uint64_t runtime_revision) {
    // BOTH revisions. `runtime_revision` is the half AC3 is protecting:
    // `adopt_runtime_sources()` bumps it while leaving `project_revision`
    // unmoved (measured -- 2/1 -> 2/2 on a hot reload that drops a skin), so a
    // key on `project_revision` alone shows a clean project over a broken one
    // for as long as the session stays open.
    return cached.project_revision != project_revision ||
        cached.runtime_revision != runtime_revision;
}

namespace {

/**
 * @brief Says, in the issue's own vocabulary, what a vanished target named.
 *
 * A `switch` over `DiagnosticCode` with no `default:`, and this is the switch
 * design ss2.12 lists for this file. It is deliberately NOT where the panel comes
 * from: the panel is MAR-186's, carried on `DiagnosticTarget`, and re-deriving
 * it here would be a second authority for one fact -- the exact shape that lets
 * two representations drift while only one of them is tested.
 *
 * What genuinely needs a per-code decision is this message, because "what is
 * gone" reads differently for an attachment, a bone and a preview reference.
 */
std::string describe_missing_target(const DiagnosticIssue& issue) {
    const OverlayRecordKey& key = issue.overlay_record;
    switch (issue.code) {
    case DiagnosticCode::OverlayOrphanWeightTarget:
        return "The mesh-weight overlay for skin '" + key.skin_name + "', slot '" +
            key.slot_name + "', attachment '" + key.attachment_name +
            "' names an attachment this runtime does not have, so there is "
            "nothing to select.";
    case DiagnosticCode::WeightsNonCanonical:
    case DiagnosticCode::WeightsUncanonicalizable:
        return "Skin '" + key.skin_name + "', slot '" + key.slot_name +
            "', attachment '" + key.attachment_name +
            "' is no longer in this runtime, so there is nothing to select.";
    case DiagnosticCode::OverlayOrphanAnimation:
        return "The overlay on animation '" + key.animation_name +
            "' names something this runtime no longer has, so there is nothing "
            "to select.";
    case DiagnosticCode::PreviewStaleAnimation:
    case DiagnosticCode::PreviewStaleSkin:
    case DiagnosticCode::ProjectUnsavedChanges:
        // None of these three carries a selection at all, so this arm is
        // unreachable through `plan_issue_navigation`'s own guard. It is written
        // because the compiler named it and because a silent fall-through would
        // be a message that says nothing.
        return "This problem names nothing selectable.";
    }
    return "This problem names nothing selectable.";
}

}  // namespace

ProblemsNavigation plan_issue_navigation(
    const DiagnosticIssue& issue,
    const runtime::SkeletonData& skeleton) {
    ProblemsNavigation navigation;
    // Panel, animation and vertex are MAR-186's own decisions, carried through
    // unchanged. This function's job is the one decision MAR-186 cannot make,
    // because it takes no runtime to check against: whether the selection still
    // resolves.
    navigation.panel = issue.target.panel;
    navigation.animation_name = issue.target.animation_name;
    navigation.vertex_index = issue.target.vertex_index;

    if (!issue.target.selection.has_value()) {
        return navigation;
    }
    if (!selection_item_exists(*issue.target.selection, skeleton)) {
        // The selection is DROPPED, not carried. Replacing the live selection
        // with an identity nothing resolves is not harmless: the next runtime
        // adoption reconciles it away, the weight-paint context skips it while
        // scanning, and in between the Properties panel shows an attachment that
        // is not there. Leaving the previous selection alone is the correct
        // behaviour, and saying what is gone is the substitute for selecting it.
        navigation.target_missing = true;
        navigation.missing_description = describe_missing_target(issue);
        return navigation;
    }
    navigation.selection = issue.target.selection;
    return navigation;
}

}  // namespace marrow::editor
