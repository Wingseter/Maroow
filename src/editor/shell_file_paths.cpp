#include "shell_file_paths.hpp"

#include "shell_recent_projects.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "imgui.h"

#include "shell_asset_watch.hpp"
#include "shell_state.hpp"
#include "shell_theme.hpp"
#include "marrow/editor/project.hpp"
#include "marrow/runtime/atlas.hpp"
#include "marrow/runtime/skeleton.hpp"

namespace marrow::editor::shell {
namespace {

namespace th = marrow::editor::shell::theme;

void assign_buffer(std::array<char, 512>& buffer, const std::string& text) {
    const std::size_t length = std::min(text.size(), buffer.size() - 1U);
    std::memcpy(buffer.data(), text.data(), length);
    buffer[length] = '\0';
}

/**
 * @brief The directory the chooser opens on.
 *
 * Derived per invocation from the current project. There is no persisted
 * "last used directory": that is preference state, and preference state for the
 * File workflow is MAR-183's territory.
 */
std::filesystem::path initial_browse_directory(const ShellState& state) {
    std::error_code error;
    std::filesystem::path directory = state.project_path.parent_path();
    if (!directory.empty()) {
        if (!directory.is_absolute()) {
            const std::filesystem::path current = std::filesystem::current_path(error);
            if (!error) {
                directory = current / directory;
            }
            error.clear();
        }
        directory = directory.lexically_normal();
        if (std::filesystem::is_directory(directory, error) && !error) {
            return directory;
        }
        error.clear();
    }
    const std::filesystem::path current = std::filesystem::current_path(error);
    if (error) {
        return {};
    }
    return current.lexically_normal();
}

struct DirectoryListing {
    std::vector<std::filesystem::path> directories;
    std::vector<std::filesystem::path> files;
    std::string error;
};

/**
 * @brief Lists one directory, showing errors rather than throwing them.
 *
 * Every filesystem call uses the std::error_code overload. This modal browses
 * arbitrary user filesystems where EACCES is ordinary, and an unreadable
 * directory must render as a message rather than as an empty list that looks
 * like an empty directory.
 *
 * Recomputed every frame: no cache, no watcher. The directories a user browses
 * are small and this is an explicit, short-lived modal, so a cache would buy
 * nothing and would need an invalidation rule.
 */
DirectoryListing list_directory(
    const std::filesystem::path& directory,
    const std::string& extension) {
    DirectoryListing listing;
    std::error_code error;
    std::filesystem::directory_iterator iterator(directory, error);
    if (error) {
        listing.error = "Cannot read this folder: " + error.message();
        return listing;
    }

    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        const std::filesystem::path entry = iterator->path();
        std::error_code entry_error;
        if (std::filesystem::is_directory(entry, entry_error) && !entry_error) {
            listing.directories.push_back(entry);
        } else {
            entry_error.clear();
            if (std::filesystem::is_regular_file(entry, entry_error) && !entry_error &&
                entry.extension().string() == extension) {
                listing.files.push_back(entry);
            }
        }
        iterator.increment(error);
        if (error) {
            listing.error = "Stopped reading this folder: " + error.message();
            break;
        }
    }

    std::sort(listing.directories.begin(), listing.directories.end());
    std::sort(listing.files.begin(), listing.files.end());
    return listing;
}

void seed_action_request(
    ShellState* state,
    FileAction action,
    FilePathMode mode,
    std::string caption,
    std::string extension,
    const std::string& initial_name) {
    FilePathRequest request;
    request.action = action;
    request.mode = mode;
    request.target = FilePathTarget::Action;
    request.caption = std::move(caption);
    request.extension = std::move(extension);
    request.directory = initial_browse_directory(*state);
    assign_buffer(request.name, initial_name);
    state->file_path_request = std::move(request);
}

void seed_browse_request(
    ShellState* state,
    FilePathTarget target,
    FilePathMode mode,
    std::string caption,
    std::string extension) {
    FilePathRequest request;
    request.action = FileAction::New;
    request.mode = mode;
    request.target = target;
    request.caption = std::move(caption);
    request.extension = std::move(extension);
    request.directory = initial_browse_directory(*state);
    state->file_path_request = std::move(request);
}

/** @brief Routes a committed choice to whatever asked for it. */
/**
 * @brief Arms a deferred Open of one specific path.
 *
 * The single writer of a targeted `pending_file_application`, with two callers:
 * the chooser's `commit_path_choice`, and `perform_session_intent` when a
 * Recent entry supplied a destination. Extracted rather than duplicated so the
 * two origins cannot drift.
 */
void arm_open(ShellState* state, const std::filesystem::path& path) {
    PendingFileApplication pending;
    pending.action = FileAction::Open;
    pending.path = path;
    state->pending_file_application = std::move(pending);
}

void commit_path_choice(ShellState* state, const std::filesystem::path& chosen) {
    const FilePathTarget target = state->file_path_request->target;
    const FileAction action = state->file_path_request->action;

    switch (target) {
        case FilePathTarget::NewSkeleton:
            if (state->new_project_form.has_value()) {
                assign_buffer(state->new_project_form->skeleton, chosen.string());
                state->new_project_form->skeleton_error.clear();
            }
            break;
        case FilePathTarget::NewAtlas:
            if (state->new_project_form.has_value()) {
                state->new_project_form->atlas_paths.push_back(chosen);
                state->new_project_form->selected_atlas =
                    static_cast<int>(state->new_project_form->atlas_paths.size()) - 1;
                state->new_project_form->atlas_error.clear();
            }
            break;
        case FilePathTarget::NewProject:
            if (state->new_project_form.has_value()) {
                assign_buffer(state->new_project_form->project, chosen.string());
            }
            break;
        case FilePathTarget::Action:
            if (action == FileAction::Open) {
                // Deferred: Open replaces the session, so it lands at end of
                // frame exactly as `Reload Project` already does.
                arm_open(state, chosen);
            } else if (action == FileAction::SaveAs) {
                // Immediate: Save As replaces nothing, exactly as
                // `save_project_file` already applies mid-frame. On failure the
                // modal STAYS OPEN so the user can pick another destination.
                if (!apply_save_as(state, chosen)) {
                    return;
                }
            }
            break;
    }

    state->file_path_request.reset();
    ImGui::CloseCurrentPopup();
}

void draw_path_chooser_modal(ShellState* state) {
    if (!state->file_path_request.has_value()) {
        return;
    }
    const bool was_open = state->file_path_request->opened;
    if (!was_open) {
        ImGui::OpenPopup(kFilePathModal);
        state->file_path_request->opened = true;
    }
    if (!ImGui::BeginPopupModal(
            kFilePathModal, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (was_open) {
            // It was open last frame and ImGui has closed it by some route other
            // than Choose or Cancel -- Escape being the obvious one. An external
            // close of a chooser IS a cancel, and Cancel and success already
            // clear this request identically. Leaving it set would strand the
            // optional: `tick_dirty_intent` reads it as "a destination is still
            // being chosen", so an AwaitingSave intent would never resolve.
            state->file_path_request.reset();
        }
        return;
    }

    FilePathRequest& request = *state->file_path_request;
    ImGui::TextUnformatted(request.caption.c_str());
    ImGui::Separator();
    ImGui::TextColored(th::kFaint, "%s", request.directory.string().c_str());

    std::filesystem::path navigate_to;
    const DirectoryListing listing = list_directory(request.directory, request.extension);
    ImGui::BeginChild("##file_path_entries", ImVec2(520.0f, 240.0f), ImGuiChildFlags_Borders);
    if (!listing.error.empty()) {
        ImGui::TextColored(th::kStateWarn, "%s", listing.error.c_str());
    } else {
        const std::filesystem::path parent = request.directory.parent_path();
        if (!parent.empty() && parent != request.directory) {
            if (ImGui::Selectable("..")) {
                navigate_to = parent;
            }
        }
        for (const std::filesystem::path& directory : listing.directories) {
            const std::string label = "[" + directory.filename().string() + "]";
            if (ImGui::Selectable(label.c_str())) {
                navigate_to = directory;
            }
        }
        for (const std::filesystem::path& file : listing.files) {
            if (ImGui::Selectable(file.filename().string().c_str())) {
                assign_buffer(request.name, file.filename().string());
            }
        }
    }
    ImGui::EndChild();

    ImGui::SetNextItemWidth(400.0f);
    const bool entered = ImGui::InputText(
        "Name",
        request.name.data(),
        request.name.size(),
        ImGuiInputTextFlags_EnterReturnsTrue);

    const FilePathChoice choice = resolve_choice(
        request.directory,
        std::string_view(request.name.data()),
        request.mode,
        request.extension);
    if (choice.diagnostic.empty()) {
        ImGui::TextUnformatted(" ");
    } else {
        ImGui::TextColored(
            choice.acceptable ? th::kFaint : th::kStateWarn,
            "%s",
            choice.diagnostic.c_str());
    }

    ImGui::BeginDisabled(!choice.acceptable);
    const bool chose = ImGui::Button("Choose") || (entered && choice.acceptable);
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancelled = ImGui::Button("Cancel");

    if (cancelled) {
        // Cleared on Cancel and on success alike; `apply_pending_file_action`
        // returns false in the first case and true in the second, which is how
        // MAR-182 tells a cancelled save path from a completed one.
        state->file_path_request.reset();
        ImGui::CloseCurrentPopup();
    } else if (chose) {
        commit_path_choice(state, choice.path);
    } else if (!navigate_to.empty()) {
        request.directory = navigate_to.lexically_normal();
    }

    ImGui::EndPopup();
}

void draw_new_project_modal(ShellState* state) {
    if (!state->new_project_form.has_value()) {
        return;
    }
    if (!state->new_project_form->opened) {
        ImGui::OpenPopup(kNewProjectModal);
        state->new_project_form->opened = true;
    }
    if (!ImGui::BeginPopupModal(
            kNewProjectModal, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        return;
    }

    NewProjectForm& form = *state->new_project_form;
    const std::filesystem::path browse_directory = initial_browse_directory(*state);

    ImGui::TextUnformatted("New Project");
    ImGui::Separator();

    ImGui::SetNextItemWidth(420.0f);
    ImGui::InputText("Skeleton (.mskl)", form.skeleton.data(), form.skeleton.size());
    ImGui::SameLine();
    if (ImGui::Button("Browse...##new_skeleton")) {
        seed_browse_request(
            state,
            FilePathTarget::NewSkeleton,
            FilePathMode::OpenExisting,
            "New Project -- skeleton (.mskl)",
            ".mskl");
    }
    const FilePathChoice skeleton_choice = resolve_choice(
        browse_directory,
        std::string_view(form.skeleton.data()),
        FilePathMode::OpenExisting,
        ".mskl");
    if (!form.skeleton_error.empty()) {
        ImGui::TextColored(th::kStateWarn, "%s", form.skeleton_error.c_str());
    } else if (!skeleton_choice.diagnostic.empty()) {
        ImGui::TextColored(th::kStateWarn, "%s", skeleton_choice.diagnostic.c_str());
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Atlases (.matl)");
    ImGui::BeginChild("##new_atlases", ImVec2(520.0f, 110.0f), ImGuiChildFlags_Borders);
    for (int index = 0; index < static_cast<int>(form.atlas_paths.size()); ++index) {
        const bool selected = form.selected_atlas == index;
        if (ImGui::Selectable(form.atlas_paths[static_cast<std::size_t>(index)]
                                  .string()
                                  .c_str(),
                              selected)) {
            form.selected_atlas = index;
        }
    }
    ImGui::EndChild();
    if (ImGui::Button("Add...##new_atlas")) {
        seed_browse_request(
            state,
            FilePathTarget::NewAtlas,
            FilePathMode::OpenExisting,
            "New Project -- atlas (.matl)",
            ".matl");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(
        form.selected_atlas < 0 ||
        form.selected_atlas >= static_cast<int>(form.atlas_paths.size()));
    if (ImGui::Button("Remove##new_atlas")) {
        form.atlas_paths.erase(
            form.atlas_paths.begin() + static_cast<std::ptrdiff_t>(form.selected_atlas));
        form.selected_atlas = form.atlas_paths.empty() ? -1 : 0;
        form.atlas_error.clear();
    }
    ImGui::EndDisabled();
    if (!form.atlas_error.empty()) {
        ImGui::TextColored(th::kStateWarn, "%s", form.atlas_error.c_str());
    } else if (form.atlas_paths.empty()) {
        ImGui::TextColored(th::kStateWarn, "Add at least one atlas.");
    }

    ImGui::Separator();
    ImGui::SetNextItemWidth(420.0f);
    ImGui::InputText("Project file (.marrow)", form.project.data(), form.project.size());
    ImGui::SameLine();
    if (ImGui::Button("Browse...##new_project")) {
        seed_browse_request(
            state,
            FilePathTarget::NewProject,
            FilePathMode::SaveTarget,
            "New Project -- project file (.marrow)",
            ".marrow");
    }
    const FilePathChoice project_choice = resolve_choice(
        browse_directory,
        std::string_view(form.project.data()),
        FilePathMode::SaveTarget,
        ".marrow");
    if (!project_choice.diagnostic.empty()) {
        ImGui::TextColored(
            project_choice.acceptable ? th::kFaint : th::kStateWarn,
            "%s",
            project_choice.diagnostic.c_str());
    }

    ImGui::Separator();
    const bool structurally_valid = skeleton_choice.acceptable &&
        !form.atlas_paths.empty() && project_choice.acceptable;
    ImGui::BeginDisabled(!structurally_valid);
    const bool create = ImGui::Button("Create");
    ImGui::EndDisabled();
    ImGui::SameLine();
    const bool cancelled = ImGui::Button("Cancel##new_project");

    if (cancelled) {
        state->new_project_form.reset();
        state->file_path_request.reset();
        ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
        return;
    }
    if (create) {
        // The validation pass, in the order that names the FIRST thing wrong.
        // `EditorSession::create` reports failure through a ProjectLoadResult
        // AFTER the modal has closed; this keeps the modal open with the error
        // next to the field that is wrong. It touches the target in no way --
        // New writes nothing.
        form.skeleton_error.clear();
        form.atlas_error.clear();
        bool atlas_failed = false;
        const std::string error = validate_new_project_sources(
            skeleton_choice.path, form.atlas_paths, &atlas_failed);
        if (error.empty()) {
            PendingFileApplication pending;
            pending.action = FileAction::New;
            pending.path = project_choice.path;
            pending.skeleton_path = skeleton_choice.path;
            pending.atlas_paths = form.atlas_paths;
            state->pending_file_application = std::move(pending);
            state->new_project_form.reset();
            state->file_path_request.reset();
            ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
            return;
        }
        if (atlas_failed) {
            form.atlas_error = error;
        } else {
            form.skeleton_error = error;
        }
    }

    // Nested so ImGui stacks the chooser ABOVE this modal. Opening it at root
    // scope would close the form: OpenPopup pushes at the CURRENT popup depth,
    // and a root-scope open at depth 0 discards everything deeper.
    draw_path_chooser_modal(state);
    ImGui::EndPopup();
}

} // namespace

FilePathChoice resolve_choice(
    const std::filesystem::path& directory,
    std::string_view typed_name,
    FilePathMode mode,
    std::string_view extension) {
    FilePathChoice choice;

    std::size_t begin = 0U;
    std::size_t end = typed_name.size();
    const auto is_space = [](char value) {
        return std::isspace(static_cast<unsigned char>(value)) != 0;
    };
    while (begin < end && is_space(typed_name[begin])) ++begin;
    while (end > begin && is_space(typed_name[end - 1U])) --end;
    const std::string trimmed(typed_name.substr(begin, end - begin));
    if (trimmed.empty()) {
        choice.diagnostic = "Enter a file name.";
        return choice;
    }

    // Typing an absolute path, or a `../sibling/x.marrow`, into the name field
    // works: it is the escape hatch for anything the bespoke browser makes
    // awkward.
    std::filesystem::path candidate(trimmed);
    if (!candidate.is_absolute()) {
        candidate = directory / candidate;
    }
    candidate = candidate.lexically_normal();

    const std::string expected_extension(extension);
    const std::string candidate_extension = candidate.extension().string();
    if (candidate_extension.empty()) {
        candidate += expected_extension;
    } else if (candidate_extension != expected_extension) {
        choice.path = candidate;
        choice.diagnostic = "Expected a " + expected_extension + " file.";
        return choice;
    }
    choice.path = candidate;

    // Every filesystem call below uses the std::error_code overload. This modal
    // browses arbitrary user filesystems, where EACCES is ordinary; a throw here
    // would take down a frame.
    std::error_code error;
    if (mode == FilePathMode::OpenExisting) {
        if (std::filesystem::is_directory(candidate, error) && !error) {
            choice.diagnostic = "That is a directory.";
            return choice;
        }
        error.clear();
        if (!std::filesystem::is_regular_file(candidate, error) || error) {
            choice.diagnostic = "That file does not exist.";
            return choice;
        }
        choice.acceptable = true;
        return choice;
    }

    if (std::filesystem::is_directory(candidate, error) && !error) {
        choice.diagnostic = "That is a directory.";
        return choice;
    }
    error.clear();
    const std::filesystem::path parent = candidate.parent_path();
    if (parent.empty() || !std::filesystem::is_directory(parent, error) || error) {
        choice.diagnostic = "The destination folder does not exist.";
        return choice;
    }
    error.clear();
    if (std::filesystem::exists(candidate, error) && !error) {
        // Accepted, not rejected: a Save As over an existing project is a
        // legitimate, deliberate act, and MAR-180 made it atomic. The diagnostic
        // slot carries the note instead of a refusal.
        choice.diagnostic = "Replaces the existing file.";
    }
    choice.acceptable = true;
    return choice;
}

std::string validate_new_project_sources(
    const std::filesystem::path& skeleton_path,
    const std::vector<std::filesystem::path>& atlas_paths,
    bool* atlas_failed) {
    if (atlas_failed != nullptr) {
        *atlas_failed = false;
    }
    const marrow::runtime::SkeletonDataResult skeleton =
        marrow::runtime::load_skeleton_data(skeleton_path);
    if (!skeleton) {
        return skeleton.error.has_value() ? skeleton.error->format()
                                          : "The skeleton could not be loaded.";
    }
    if (atlas_paths.empty()) {
        if (atlas_failed != nullptr) {
            *atlas_failed = true;
        }
        return "A project needs at least one atlas.";
    }
    for (const std::filesystem::path& atlas_path : atlas_paths) {
        const marrow::runtime::AtlasDataResult atlas =
            marrow::runtime::AtlasLoader::load(atlas_path);
        if (!atlas) {
            if (atlas_failed != nullptr) {
                *atlas_failed = true;
            }
            return atlas.error.has_value()
                ? atlas.error->format()
                : atlas_path.string() + " could not be loaded.";
        }
    }
    return {};
}

bool apply_save_as(ShellState* state, const std::filesystem::path& chosen) {
    if (state == nullptr || !state->load_result ||
        state->load_result.project == nullptr) {
        return false;
    }
    if (authoring_gesture_active(*state)) {
        state->status_message = "Finish the active edit before saving";
        return false;
    }

    const marrow::editor::ProjectSaveResult result = state->session.save(chosen);
    if (!result) {
        state->error_message = result.error.has_value()
            ? result.error->format()
            : "Unknown project save failure.";
        state->status_message = "Project save failed";
        // ShellState::project_path is NOT moved. The toolbar's Save writes it,
        // so moving it on a failed Save As would leave Save writing to a file
        // this session never successfully wrote.
        return false;
    }

    state->project_path = chosen;
    // BEFORE the record, exactly as `save_project_file` orders it. The project
    // write succeeded, so any earlier error is stale -- but `record_recent_project`
    // SETS error_message when the settings write fails, and clearing afterwards
    // would swallow the single report design §10.7 grants that failure ("reported
    // once and then forgotten"), leaving it reported zero times.
    state->error_message.clear();
    // MAR-183: Save As records its NEW destination. A failed Save As returned
    // above without moving the path, and records nothing.
    record_recent_project(state, chosen);
    // The session has been written somewhere the New arm did not name, so the
    // arm can never be consumed and must not outlive this save.
    state->pending_recent_on_first_save.reset();
    update_project_dirty_state(state);
    // MAR-180's rebase is identity-preserving, so the recomputed watch list must
    // be element-wise equal to the one before the move. Recomputing it is free,
    // and C5 compares it as a cheap check that the rebase did what it claims.
    reset_runtime_asset_watch(state);
    state->status_message = "Saved project to " + chosen.string();
    return true;
}

void begin_file_action(ShellState* state, FileAction action) {
    if (state == nullptr) {
        return;
    }

    switch (action) {
        case FileAction::New: {
            state->file_path_request.reset();
            state->new_project_form = NewProjectForm{};
            return;
        }
        case FileAction::Open: {
            state->new_project_form.reset();
            seed_action_request(
                state,
                FileAction::Open,
                FilePathMode::OpenExisting,
                "Open Project",
                ".marrow",
                {});
            return;
        }
        case FileAction::SaveAs: {
            state->new_project_form.reset();
            seed_action_request(
                state,
                FileAction::SaveAs,
                FilePathMode::SaveTarget,
                "Save Project As",
                ".marrow",
                state->project_path.filename().string());
            return;
        }
        case FileAction::Save: {
            // The shipped shell never has an empty project_path -- Options
            // defaults it and New requires a target up front -- but the
            // invariant is an argument, not a type, so the guard stays.
            if (state->project_path.empty()) {
                begin_file_action(state, FileAction::SaveAs);
                return;
            }
            (void)save_project_file(state, true);
            return;
        }
        case FileAction::Reload: {
            state->new_project_form.reset();
            state->file_path_request.reset();
            // The path stays EMPTY: `reload_project` reads state->project_path
            // itself, and copying it here would create a second source of truth
            // for the same fact.
            PendingFileApplication pending;
            pending.action = FileAction::Reload;
            state->pending_file_application = std::move(pending);
            return;
        }
    }
}

namespace {

/**
 * @brief Performs an intent that has cleared the gate.
 *
 * Called from EXACTLY two places -- `begin_session_intent`'s clean-session
 * branch and `tick_dirty_intent`'s `!session.dirty()` branch. Neither is
 * reachable from a failure, which is the structural reason a failed save can
 * never fall through to a discard, a replacement or a shutdown.
 *
 * @param path Honoured only by `Open`, and empty for every other intent because
 *             nothing else supplies one.
 */
void perform_session_intent(
    ShellState* state, SessionIntent intent, const std::filesystem::path& path) {
    switch (intent) {
        case SessionIntent::New:
            begin_file_action(state, FileAction::New);
            return;
        case SessionIntent::Open:
            if (!path.empty()) {
                // A Recent entry: the destination is already known, so there is
                // nothing to choose. It still arrives here, AFTER the gate.
                arm_open(state, path);
                return;
            }
            begin_file_action(state, FileAction::Open);
            return;
        case SessionIntent::Reload:
            begin_file_action(state, FileAction::Reload);
            return;
        case SessionIntent::Quit:
            state->should_exit = true;
            return;
    }
}

} // namespace

void begin_session_intent(
    ShellState* state, SessionIntent intent, const std::filesystem::path& path) {
    if (state == nullptr) {
        return;
    }
    if (state->dirty_intent.has_value()) {
        if (state->dirty_intent->phase == DirtyIntentPhase::AwaitingSave) {
            // A save is in flight and must land. The intent is re-expressible;
            // the surfaces are all still there.
            return;
        }
        // Last wish wins: the prompt is already up, so retarget it rather than
        // stacking a queue the user cannot see. BOTH fields, UNCONDITIONALLY:
        // retargeting the intent alone would open the first Recent entry when
        // the user asked for the second, and assigning the path only when it is
        // non-empty would leave a stale Open destination on a later Reload.
        state->dirty_intent->intent = intent;
        state->dirty_intent->path = path;
        return;
    }
    if (!state->session.dirty()) {
        perform_session_intent(state, intent, path);
        return;
    }
    state->dirty_intent =
        DirtyIntentRequest{intent, path, DirtyIntentPhase::Prompting, false};
}

void tick_dirty_intent(ShellState* state) {
    if (state == nullptr || !state->dirty_intent.has_value() ||
        state->dirty_intent->phase != DirtyIntentPhase::AwaitingSave) {
        return;
    }

    if (!state->session.dirty()) {
        // (1) Content-keyed: the bytes on disk now match memory, which is exactly
        // the precondition for letting the intent proceed. Keying on
        // `save_project_file`'s return instead would be unavailable through the
        // deferred Save As branch, and wrong on the immediate one -- it also
        // returns false, without saving, while an authoring gesture is live.
        const SessionIntent intent = state->dirty_intent->intent;
        const std::filesystem::path path = state->dirty_intent->path;
        state->dirty_intent.reset();
        perform_session_intent(state, intent, path);
        return;
    }
    if (state->file_path_request.has_value()) {
        // (2) A destination is still being chosen. Depends on the optional being
        // truthful, which is why `draw_path_chooser_modal` clears it on an
        // external close.
        return;
    }
    // (3) The save failed or its destination was cancelled. The intent survives,
    // the prompt comes back, and `perform_session_intent` was never reached.
    state->dirty_intent->phase = DirtyIntentPhase::Prompting;
    state->dirty_intent->opened = false;
}

void resolve_dirty_intent(ShellState* state, DirtyIntentResponse response) {
    if (state == nullptr || !state->dirty_intent.has_value()) {
        return;
    }

    switch (response) {
        case DirtyIntentResponse::Cancel:
            // Cancel is the response that does nothing, by definition. The
            // session, selection, preview, history and project_dirty are all
            // untouched, and the intent is re-expressible rather than retained.
            state->dirty_intent.reset();
            return;
        case DirtyIntentResponse::Discard: {
            // Discard declines to SAVE; it does not clear, revert or overwrite
            // anything. For New/Open/Reload the unsaved work survives right up
            // until the atomic replacement lands.
            const SessionIntent intent = state->dirty_intent->intent;
            const std::filesystem::path path = state->dirty_intent->path;
            state->dirty_intent.reset();
            perform_session_intent(state, intent, path);
            return;
        }
        case DirtyIntentResponse::Save:
            state->dirty_intent->phase = DirtyIntentPhase::AwaitingSave;
            begin_file_action(state, FileAction::Save);
            // The same evaluation the frame runs: an immediate save has already
            // landed or failed by now, and only a Save As leaves a chooser up.
            tick_dirty_intent(state);
            return;
    }
}

bool absorb_close_request(ShellState* state, bool host_close_requested) {
    if (state == nullptr || !host_close_requested) {
        return false;
    }
    if (state->should_exit) {
        // A confirmed exit passes through. Vetoing here would deadlock the loop:
        // the machine has already said yes and nothing would ever say it again.
        return false;
    }
    begin_session_intent(state, SessionIntent::Quit);
    return !state->should_exit;
}

bool apply_pending_file_action(ShellState* state) {
    if (state == nullptr || !state->pending_file_application.has_value()) {
        return false;
    }
    const PendingFileApplication pending = *state->pending_file_application;
    state->pending_file_application.reset();

    if (pending.action == FileAction::Reload) {
        // `reload_project` carries its own previous-animation/timeline capture
        // and its own adoption call, so Reload returns here rather than sharing
        // the New/Open preamble below.
        return reload_project(state);
    }

    const bool is_open = pending.action == FileAction::Open;
    // A New project has no "previous" animation: the pick then falls through to
    // the authored active_animation and, when that does not resolve, to the
    // rig's first clip.
    const std::string previous_animation_name =
        is_open ? state->selected_animation_name : std::string{};
    const double previous_timeline_time = is_open ? state->timeline_time_seconds : 0.0;
    const bool previous_timeline_loop = state->timeline_loop;
    const bool previous_timeline_playing = is_open ? state->timeline_playing : false;

    if (is_open) {
        // The CHOSEN path, not state->project_path: reusing reload_project would
        // require moving the shell's path before knowing the open succeeded.
        const marrow::editor::ProjectLoadResult attempted =
            state->session.open(pending.path);
        if (!attempted) {
            state->status_message = "Project load failed";
            state->error_message = attempted.error.has_value()
                ? attempted.error->format()
                : "Unknown project load failure.";
            return false;
        }
        state->project_path = pending.path;
        adopt_session_project_into_shell(
            state,
            previous_animation_name,
            previous_timeline_time,
            previous_timeline_loop,
            previous_timeline_playing,
            /*restore_transient_playback=*/false,
            /*project_is_clean=*/true);
        // MAR-183: recorded AFTER the open succeeded and after project_path
        // moved, so the recorded path and the shell agree, and a failed open --
        // which returned above -- records nothing.
        record_recent_project(state, pending.path);
        // This session was OPENED, not created: any arm a previous New left
        // behind belongs to a session that no longer exists.
        state->pending_recent_on_first_save.reset();
        state->status_message = "Opened " + pending.path.string();
        return true;
    }

    if (pending.action != FileAction::New) {
        return false;
    }

    // Only the three source fields. `name` (derived from the project file's
    // stem), `active_animation`, `preview_skins`, `export_directory` and `notes`
    // take create_minimal_project's documented defaults.
    marrow::editor::MinimalProjectOptions options;
    options.project_path = pending.path;
    options.skeleton_path = pending.skeleton_path;
    options.atlas_paths = pending.atlas_paths;

    const marrow::editor::ProjectLoadResult attempted = state->session.create(options);
    if (!attempted) {
        state->status_message = "New project failed";
        state->error_message = attempted.error.has_value()
            ? attempted.error->format()
            : "Unknown project creation failure.";
        return false;
    }
    state->project_path = pending.path;
    // MAR-183: ARM, do not record. `create` writes nothing to disk, so there is
    // no file to put in a list of things the user can re-open. The first save
    // that writes THIS path consumes the arm.
    state->pending_recent_on_first_save = pending.path;
    // project_is_clean is FALSE: MAR-180 made `create` dirty-from-birth on
    // purpose, and reload_project's hardcoded `project_dirty = false` would
    // paint the session clean over a file that does not exist.
    adopt_session_project_into_shell(
        state,
        previous_animation_name,
        previous_timeline_time,
        previous_timeline_loop,
        previous_timeline_playing,
        /*restore_transient_playback=*/false,
        /*project_is_clean=*/false);
    state->status_message = "New project (unsaved): " + pending.path.string();
    return true;
}

namespace {

/**
 * @brief Draws the Save / Discard / Cancel prompt.
 * @return Whether the prompt owns this frame, i.e. nothing else may draw.
 *
 * Returns false in `AwaitingSave` on purpose: the chooser the save raised must
 * still be drawn underneath, and it is the caller that draws it.
 */
bool draw_dirty_intent_modal(ShellState* state) {
    // Evaluate FIRST, so an AwaitingSave intent that last frame's chooser
    // resolved is seen before anything is drawn this frame.
    tick_dirty_intent(state);
    if (!state->dirty_intent.has_value() ||
        state->dirty_intent->phase != DirtyIntentPhase::Prompting) {
        return false;
    }

    const bool was_open = state->dirty_intent->opened;
    if (!was_open) {
        ImGui::OpenPopup(kDirtyIntentModal);
        state->dirty_intent->opened = true;
    }
    if (!ImGui::BeginPopupModal(
            kDirtyIntentModal, nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        if (was_open) {
            // Closed by a route other than the three buttons. A closed prompt is
            // a Cancel -- the same rule the chooser follows.
            state->dirty_intent.reset();
        }
        return false;
    }

    const char* consequence = "";
    switch (state->dirty_intent->intent) {
        case SessionIntent::New:
            consequence = "Creating a new project will discard them.";
            break;
        case SessionIntent::Open:
            consequence = "Opening another project will discard them.";
            break;
        case SessionIntent::Reload:
            consequence = "Reloading will discard them.";
            break;
        case SessionIntent::Quit:
            consequence = "Quitting will discard them.";
            break;
    }

    ImGui::TextUnformatted("This project has unsaved changes.");
    ImGui::TextColored(th::kFaint, "%s", state->project_path.string().c_str());
    ImGui::TextUnformatted(consequence);
    ImGui::Separator();

    bool resolved = false;
    if (ImGui::Button("Save")) {
        resolve_dirty_intent(state, DirtyIntentResponse::Save);
        resolved = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Discard")) {
        resolve_dirty_intent(state, DirtyIntentResponse::Discard);
        resolved = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel")) {
        resolve_dirty_intent(state, DirtyIntentResponse::Cancel);
        resolved = true;
    }
    if (resolved) {
        // `state->dirty_intent` may already be gone here; nothing below reads it.
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
    return true;
}

} // namespace

void draw_file_path_modals(ShellState* state) {
    if (state == nullptr) {
        return;
    }
    if (draw_dirty_intent_modal(state)) {
        return;
    }
    if (state->new_project_form.has_value()) {
        // The form draws the chooser nested inside itself, so when the form is
        // open the chooser can only belong to one of its rows.
        draw_new_project_modal(state);
        return;
    }
    draw_path_chooser_modal(state);
}

} // namespace marrow::editor::shell
