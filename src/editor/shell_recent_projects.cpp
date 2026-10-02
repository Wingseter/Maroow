#include "shell_recent_projects.hpp"

#include <vector>

#include "imgui.h"

#include "shell_file_paths.hpp"

#include "marrow/editor/preferences.hpp"
#include "marrow/editor/recent_projects.hpp"

namespace marrow::editor::shell {

namespace fs = std::filesystem;

std::string recent_menu_label(const fs::path& path) {
    return path.filename().string() + "##" + path.string();
}

bool persist_recent_projects(ShellState* state, bool changed) {
    if (state == nullptr) {
        return false;
    }
    if (!changed) {
        // The no-op skip. Re-recording the current head is the common case --
        // every Save As onto the same path, every re-open of the project already
        // at the front -- and rewriting the settings file for it would churn the
        // user's file, and its mtime, for no change in content.
        return true;
    }
    // Default-constructed at the call site so MARROW_CONFIG_HOME is honoured,
    // exactly as `set_shell_default_curve` does. Atomicity comes free from
    // `write_file_atomically`.
    const marrow::editor::PreferenceStore store;
    // The LOADED preferences, mutated in place -- never a fresh value. That is
    // what preserves `default_curve` and the unknown additive root.
    const marrow::editor::PreferenceSaveResult saved = store.save(state->preferences);
    if (!saved) {
        state->error_message = "Failed to store recent projects: " + saved.error;
        // The in-memory change is KEPT, matching set_shell_default_curve's
        // shipped behaviour: the session behaves as if it persisted and the next
        // launch disagrees. Reported once, deliberately not diverged from.
        return false;
    }
    state->preference_path = saved.path;
    return true;
}

void record_recent_project(ShellState* state, const fs::path& path) {
    if (state == nullptr || path.empty()) {
        return;
    }
    (void)persist_recent_projects(
        state,
        marrow::editor::promote_recent_project(
            &state->preferences.recent_projects, path));
}

void forget_recent_project(ShellState* state, const fs::path& path) {
    if (state == nullptr || path.empty()) {
        return;
    }
    (void)persist_recent_projects(
        state,
        marrow::editor::forget_recent_path(
            &state->preferences.recent_projects, path));
}

void forget_missing_recent_projects(ShellState* state) {
    if (state == nullptr) {
        return;
    }
    (void)persist_recent_projects(
        state,
        marrow::editor::drop_missing_recent_paths(
            &state->preferences.recent_projects));
}

void open_recent_project(ShellState* state, const fs::path& path) {
    if (state == nullptr || path.empty()) {
        return;
    }
    // THE gate, and the whole function. A Recent entry is an Open from a
    // different origin, nothing more: `begin_session_intent`'s first act is to
    // consult `session.dirty()`, so unsaved work raises the prompt here exactly
    // as it does for File > Open Project...
    begin_session_intent(state, SessionIntent::Open, path);
}

void draw_recent_projects_menu(ShellState* state) {
    if (state == nullptr) {
        return;
    }
    // A COPY, deliberately: a click inside either loop below mutates
    // `state->preferences.recent_projects`, and iterating the live vector while
    // an item removes an element from it would invalidate the iterator mid-frame.
    const std::vector<fs::path> entries = state->preferences.recent_projects;

    if (!ImGui::BeginMenu(kRecentMenu, !entries.empty())) {
        return;
    }

    bool any_missing = false;
    for (const fs::path& entry : entries) {
        const bool present = marrow::editor::recent_project_exists(entry);
        any_missing = any_missing || !present;
        // Disabled rather than hidden when the file is gone: AC4 keeps a missing
        // entry VISIBLE so the user can see what happened and remove it
        // deliberately. Nothing prunes it for them -- not on load, not here, not
        // on click.
        if (ImGui::MenuItem(
                recent_menu_label(entry).c_str(), nullptr, false, present)) {
            open_recent_project(state, entry);
        }
        if (present && ImGui::IsItemHovered()) {
            // The label shows only the filename; the tooltip carries the path.
            ImGui::SetTooltip("%s", entry.string().c_str());
        }
    }

    ImGui::Separator();

    // A parallel, ALWAYS-enabled Remove submenu. A missing entry's own row is
    // disabled and cannot be clicked, so Remove cannot live on the row it
    // removes -- and a right-click context menu would not open on a disabled
    // item either, which is precisely the case that needs one.
    if (ImGui::BeginMenu(kRecentRemoveMenu, !entries.empty())) {
        for (const fs::path& entry : entries) {
            if (ImGui::MenuItem(recent_menu_label(entry).c_str())) {
                forget_recent_project(state, entry);
            }
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem(kRecentClearMissing, nullptr, false, any_missing)) {
        forget_missing_recent_projects(state);
    }

    ImGui::EndMenu();
}

} // namespace marrow::editor::shell
