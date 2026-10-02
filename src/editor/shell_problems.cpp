#include "shell_problems.hpp"

#include <algorithm>
#include <string>
#include <utility>

#include "imgui.h"
#include "marrow/editor/problems_model.hpp"
#include "marrow/editor/safe_fix.hpp"
#include "shell_project_panels.hpp"
#include "shell_widgets.hpp"

namespace marrow::editor::shell {

void refresh_problems_if_revised(ShellState* state) {
    if (state == nullptr) {
        return;
    }
    ProblemsPanelState& panel = state->problems;
    if (!state->session.has_project()) {
        panel.report.reset();
        panel.view = {};
        panel.selected_identity.clear();
        return;
    }
    // Keyed on BOTH revisions. `adopt_runtime_sources` moves the runtime one and
    // leaves `project_revision` alone, so a hot reload that drops a skin creates
    // a `preview.stale_skin` with no project-revision change at all.
    if (panel.report.has_value() &&
        !problems_view_needs_refresh(
            panel.view, state->session.project_revision(),
            state->session.runtime_revision())) {
        return;
    }
    panel.report = collect_session_diagnostics(state->session);
    ++panel.collect_count;
    if (!panel.report.has_value()) {
        panel.view = {};
        panel.selected_identity.clear();
        return;
    }
    panel.view = build_problems_view(*panel.report, panel.filter);
    // A remembered row whose problem is gone must be CLEARED, not left pointing
    // at whatever now sits at that position.
    if (!panel.selected_identity.empty() &&
        !find_issue_by_identity(*panel.report, panel.selected_identity).has_value()) {
        panel.selected_identity.clear();
    }
}

std::string_view focus_window_for_panel(DiagnosticPanel panel) {
    // The ONLY switch over `DiagnosticPanel` in the tree, and it has no
    // `default:` arm, so a fourth panel value is a compile warning at exactly
    // one site.
    switch (panel) {
    case DiagnosticPanel::Timeline:
        return kTimelineWindowTitle;
    case DiagnosticPanel::Weights:
        // There is NO Weight window. Weight authoring is the Properties window
        // in `ShellMode::WeightPaint` plus a viewport overlay.
        return kPropertiesWindowTitle;
    case DiagnosticPanel::Project:
        // The preview reference is displayed in the Project window.
        return kProjectWindowTitle;
    }
    return kProjectWindowTitle;
}

void activate_problem_row(ShellState* state, const DiagnosticIssue& issue) {
    if (state == nullptr || state->session.runtime_data() == nullptr) {
        return;
    }
    const ProblemsNavigation navigation =
        plan_issue_navigation(issue, *state->session.runtime_data());

    state->problems.selected_identity = issue.identity;
    state->problems.focus_request = std::string(focus_window_for_panel(navigation.panel));

    if (navigation.target_missing) {
        // Leave the selection ALONE. Replacing it with an identity nothing
        // resolves is not harmless: the next adoption reconciles it away, the
        // weight-paint context skips it while scanning, and in between the
        // Properties panel shows an attachment that is not there.
        state->status_message = navigation.missing_description;
        return;
    }
    if (navigation.selection.has_value()) {
        state->selection.replace(*navigation.selection);
    }
    if (!navigation.animation_name.empty()) {
        state->selected_animation_name = navigation.animation_name;
    }
    if (navigation.panel == DiagnosticPanel::Weights) {
        // The numeric influence table is unreachable outside WeightPaint.
        apply_shell_mode(state, ShellMode::WeightPaint);
        if (navigation.vertex_index.has_value()) {
            const auto* attachment =
                navigation.selection.has_value()
                    ? std::get_if<AttachmentSelection>(&*navigation.selection)
                    : nullptr;
            if (attachment != nullptr) {
                const auto slot_index =
                    state->session.runtime_data()->find_slot_index(attachment->slot_name);
                if (slot_index.has_value()) {
                    ViewportFfdSelection ffd;
                    ffd.scope.slot_index = *slot_index;
                    ffd.scope.display_attachment_name = attachment->attachment_name;
                    ffd.scope.deform_attachment_name = attachment->attachment_name;
                    ffd.scope.display_skin_index =
                        state->session.runtime_data()->find_skin_index(
                            attachment->skin_name);
                    ffd.vertex_indices = {*navigation.vertex_index};
                    state->viewport_ffd_selection = std::move(ffd);
                }
            }
        }
    }
}

void draw_problems_window(ShellState* state) {
    if (state == nullptr) {
        return;
    }
    refresh_problems_if_revised(state);
    if (!ImGui::Begin(kProblemsWindowTitle)) {
        ImGui::End();
        return;
    }
    ProblemsPanelState& panel = state->problems;

    // The severity filter. Rebuilding the view on a change is the whole effect;
    // the filter itself is a view input, never a collector input.
    int filter_index = static_cast<int>(panel.filter);
    if (ImGui::Combo("Severity", &filter_index, "All\0Errors\0Warnings\0")) {
        panel.filter = static_cast<ProblemsSeverityFilter>(filter_index);
        if (panel.report.has_value()) {
            panel.view = build_problems_view(*panel.report, panel.filter);
        }
    }
    // The COLLECTOR's counts, not the visible rows'.
    ImGui::Text(
        "%zu error(s), %zu warning(s); %zu shown", panel.view.error_count,
        panel.view.warning_count, panel.view.visible_count);

    if (!panel.report.has_value()) {
        ImGui::TextUnformatted("No project is open.");
        ImGui::End();
        return;
    }
    if (panel.view.groups.empty()) {
        ImGui::TextUnformatted("No problems to show.");
        ImGui::End();
        return;
    }

    for (const ProblemsGroup& group : panel.view.groups) {
        ImGui::SeparatorText(
            group.severity == DiagnosticSeverity::Error ? "Errors" : "Warnings");
        for (const std::size_t index : group.issue_indices) {
            const DiagnosticIssue& issue = panel.report->issues[index];
            // Each widget carries `##<identity>` rather than sitting under a
            // `PushID(index)`. Two reasons, and the second is the load-bearing
            // one: an index-derived widget id is not stable across a refresh
            // that re-sorts, and a test can compute `window->GetID(label)`
            // directly instead of reproducing a push chain by hand. AGENTS.md
            // records MAR-185 losing time to exactly that reproduction.
            const bool selected = panel.selected_identity == issue.identity;
            const std::string row_label = issue.message + "##row_" + issue.identity;
            // An explicit width. A default `Selectable` spans the whole content
            // region, which pushes anything after `SameLine()` past the window's
            // right edge -- where it is unreachable by a mouse and invisible to
            // a user. Measured: F1's sweep found the severity filter and never
            // found the Fix button until this width was set.
            const float fix_column = 60.0f;
            const float label_width =
                std::max(40.0f, ImGui::GetContentRegionAvail().x - fix_column);
            if (ImGui::Selectable(
                    row_label.c_str(), selected, ImGuiSelectableFlags_None,
                    ImVec2(label_width, 0.0f))) {
                activate_problem_row(state, issue);
            }
            if (!issue.safe_fix_id.empty()) {
                ImGui::SameLine();
                const std::string fix_label = "Fix##fix_" + issue.identity;
                if (ImGui::SmallButton(fix_label.c_str())) {
                    // The button calls `apply_safe_fix` and then resyncs, and
                    // nothing else. This is the ONE call site in the tree.
                    const SafeFixResult applied = apply_safe_fix(state->session, issue);
                    state->status_message = applied.ok
                        ? ("Fixed: " + applied.applied_identity)
                        : applied.error;
                    // `apply_safe_fix` commits through a session transaction whose
                    // descriptor carries Runtime impact, and that commit replaces the
                    // PreviewController wholesale (session.cpp:1527) -- destroying the
                    // Skeleton and AnimationState that `preview_skeleton` and
                    // `animation_state` alias. Unconditional, because the FAILURE arm
                    // can replace it too and `applied.ok` cannot tell you which
                    // happened: `restore_active_transaction` binds a FRESH controller
                    // when the transaction had applied a live refresh
                    // (session.cpp:1284-1299) and only restores the existing one in
                    // place otherwise (session.cpp:1301). The next frame's
                    // `sync_shell_from_editor_session_if_revised` is too late: the
                    // windows drawn after this one in the SAME frame body would read
                    // the freed aliases first.
                    sync_shell_from_editor_session(state);
                }
            }
        }
    }
    ImGui::End();
}

}  // namespace marrow::editor::shell
