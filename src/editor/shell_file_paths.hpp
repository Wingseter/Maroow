#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace marrow::editor::shell {

struct ShellState;

/** @brief The four File-menu path workflows. */
enum class FileAction {
    New,
    Open,
    Save,
    SaveAs,
};

/** @brief What the shared chooser must be true of the path it returns. */
enum class FilePathMode {
    /// The chosen path must already exist and be a regular file.
    OpenExisting,
    /// The chosen path need not exist; its parent directory must.
    SaveTarget,
};

/**
 * @brief Which field the shared chooser is filling.
 *
 * `Action` is Open's and Save As's own destination; the three `New*` values are
 * the New form's rows, whose `Browse...` buttons raise the same chooser nested
 * inside the form.
 */
enum class FilePathTarget {
    Action,
    NewSkeleton,
    NewAtlas,
    NewProject,
};

/// A popup's ImGui window name is the FULL string passed to BeginPopupModal,
/// `##` suffix included. The smoke finds both modals by these exact strings.
constexpr char kFilePathModal[] = "Choose Path##file_path";
constexpr char kNewProjectModal[] = "New Project##file_new";

/**
 * @brief A live request for the shared path chooser.
 *
 * Lives on `ShellState`, not in a file-static like the two catalog popups, for
 * two independent reasons. First, `ImGui::OpenPopup` hashes its string against
 * `GetCurrentWindow()`'s id stack: called inside `BeginMenu("File")` the current
 * window is the menu popup, which is destroyed the instant the item is clicked,
 * so the id it computes is not the one `BeginPopupModal` computes at root scope.
 * A menu item must therefore RECORD a request and let a root-scope drawer open
 * the popup on a later frame -- `opened` is that latch. Second, MAR-182 must be
 * able to observe a cancel, which is only expressible if the request's lifetime
 * is inspectable state rather than a hidden global.
 */
struct FilePathRequest {
    FileAction action{FileAction::Open};
    FilePathMode mode{FilePathMode::OpenExisting};
    FilePathTarget target{FilePathTarget::Action};
    std::string caption;
    std::string extension;
    std::filesystem::path directory;
    std::array<char, 512> name{};
    bool opened{false};
};

/** @brief The New form's three rows, plus the last validation-pass error. */
struct NewProjectForm {
    std::array<char, 512> skeleton{};
    std::array<char, 512> project{};
    std::vector<std::filesystem::path> atlas_paths;
    int selected_atlas{-1};
    std::string skeleton_error;
    std::string atlas_error;
    bool opened{false};
};

/**
 * @brief A session-replacing action resolved this frame and applied at the next
 *        end of frame.
 *
 * New and Open replace the session, so they are deferred exactly as
 * `Reload Project` already is: windows drawn earlier and later in one frame must
 * describe the same project. Save and Save As replace nothing and apply
 * immediately, exactly as `save_project_file` already does.
 */
struct PendingFileApplication {
    FileAction action{FileAction::Open};
    std::filesystem::path path;
    std::filesystem::path skeleton_path;
    std::vector<std::filesystem::path> atlas_paths;
};

/** @brief What `resolve_choice` made of a browse directory and a typed name. */
struct FilePathChoice {
    std::filesystem::path path;
    /**
     * The single reason the choice is not acceptable, OR -- when `acceptable` is
     * true -- an informational note. `Choose` is gated on `acceptable`, never on
     * this being empty: a SaveTarget over an existing file is accepted AND
     * carries "Replaces the existing file."
     */
    std::string diagnostic;
    bool acceptable{false};
};

/**
 * @brief Resolves a browse directory plus a typed name into a path or a reason.
 *
 * Content-keyed throughout: every decision reads what the path IS on disk right
 * now, never how it was produced. A name picked from the browser list and a name
 * typed by hand go through this identically.
 *
 * The extension comparison is a plain byte comparison with no case folding.
 * macOS and Windows filesystems are usually case-insensitive and Linux is not;
 * folding here would make the editor's own acceptance depend on the host, and a
 * rejected `.MARROW` is a one-keystroke fix while a silently accepted one is a
 * portability bug.
 */
FilePathChoice resolve_choice(
    const std::filesystem::path& directory,
    std::string_view typed_name,
    FilePathMode mode,
    std::string_view extension);

/**
 * @brief New's validation pass: loads the rig, then every atlas.
 * @return The first loader error verbatim, or an empty string on a pass.
 *
 * Not redundant with `EditorSession::create`'s own loads. `create` reports
 * failure through a ProjectLoadResult AFTER the modal has closed; this keeps the
 * modal open with the error next to the field that is wrong. It touches the
 * target path in no way -- New writes nothing.
 */
std::string validate_new_project_sources(
    const std::filesystem::path& skeleton_path,
    const std::vector<std::filesystem::path>& atlas_paths,
    bool* atlas_failed = nullptr);

/**
 * @brief Save As's apply half, UI-free so the smoke can drive it.
 * @return Whether the save succeeded.
 *
 * On failure `ShellState::project_path` is NOT moved: the toolbar's Save writes
 * that field, so moving it would leave Save writing to a file this session never
 * successfully wrote -- and, after MAR-180, rebasing the references toward it.
 */
bool apply_save_as(ShellState* state, const std::filesystem::path& chosen);

/**
 * @brief Entry point for every File action.
 *
 * MAR-182 inserts the dirty-session intent gate at the TOP of this function:
 * when `state->session.dirty()` and the action replaces the session
 * (New, Open), it records a pending intent and returns without reaching the
 * body below. Save and SaveAs never gate -- Save IS the resolution.
 *
 * MAR-181 performs NO dirty check anywhere, so a New over a dirty session
 * discards unsaved work silently. That is a real, stated gap that MAR-182
 * closes; a partial check here would be rework MAR-182 must undo.
 */
void begin_file_action(ShellState* state, FileAction action);

/**
 * @brief Applies a deferred New or Open at end of frame.
 * @return Whether a pending action ran to SUCCESS this frame.
 *
 * The return value is MAR-182's completion signal. `file_path_request` is
 * cleared on Cancel and on success alike, so "the modal was cancelled while an
 * intent was pending" is `!state->file_path_request.has_value() && !applied`.
 */
bool apply_pending_file_action(ShellState* state);

/** @brief Draws the New form and the shared chooser. Root scope only. */
void draw_file_path_modals(ShellState* state);

} // namespace marrow::editor::shell
