#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace marrow::editor::shell {

struct ShellState;

/**
 * @brief The five File-menu path workflows.
 *
 * `Reload` is here because it shares the three properties that put `New` and
 * `Open` here: it is a File-menu action, it replaces the session, and it must
 * therefore land at end of frame. It carries no path of its own and never
 * participates in `FilePathMode`, `FilePathRequest` or `commit_path_choice` --
 * the only writer of `FilePathRequest::action` is `seed_action_request`, called
 * only for `Open` and `SaveAs`.
 */
enum class FileAction {
    New,
    Open,
    Save,
    SaveAs,
    Reload,
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
constexpr char kDirtyIntentModal[] = "Unsaved Changes##dirty_intent";

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

/** @brief Every intent that replaces or ends the session. */
enum class SessionIntent {
    New,
    Open,
    Reload,
    /// Both the File>Quit item and a native OS close request. They are the same
    /// intent from two origins; nothing downstream needs to tell them apart.
    Quit,
};

/** @brief Whether the prompt is up, or a save it asked for is still in flight. */
enum class DirtyIntentPhase {
    Prompting,
    AwaitingSave,
};

/** @brief The three answers. */
enum class DirtyIntentResponse {
    Save,
    Discard,
    Cancel,
};

/**
 * @brief A live dirty-session intent.
 *
 * On `ShellState` for the same two reasons `FilePathRequest` is: a menu item
 * cannot open a root-scope modal (`ImGui::OpenPopup` hashes against the menu
 * popup's id stack), so the item must RECORD and a root-scope drawer must open
 * on a later frame -- `opened` is that latch; and the machine's state has to be
 * inspectable by a UI-free test, because six of this story's eight cases never
 * render a frame.
 */
struct DirtyIntentRequest {
    SessionIntent intent{SessionIntent::New};
    /**
     * The destination, non-empty ONLY for a targeted Open -- which today means
     * exactly one origin, a Recent-projects entry. Empty means "raise the
     * chooser", which is every other origin of every intent.
     *
     * It lives beside `intent` rather than replacing it because a Recent open IS
     * an Open: `SessionIntent` gains no value, for the same reason Quit covers
     * both the menu item and an OS close request. Nothing downstream needs to
     * tell the two origins apart -- only where to land.
     */
    std::filesystem::path path;
    DirtyIntentPhase phase{DirtyIntentPhase::Prompting};
    bool opened{false};
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
 * @brief Performs a File action. This function does NOT gate on dirtiness.
 *
 * MAR-181's comment here said MAR-182 would insert the dirty-session gate at
 * the TOP of this function. It does not, and doing so would be a defect: the
 * intent's own resolution has to reach the body below, so the gate would need a
 * bypass flag or a bypass parameter -- a condition keyed on WHO IS CALLING
 * rather than on what the document is. The layers are split instead:
 * `begin_session_intent` decides, `begin_file_action` performs. Every surface
 * that can discard unsaved work calls the former; `Save` and `Save As` call this
 * one directly, because Save IS the resolution and gating it would deadlock the
 * machine.
 */
void begin_file_action(ShellState* state, FileAction action);

/**
 * @brief Applies a deferred New, Open or Reload at end of frame.
 * @return Whether a pending action ran to SUCCESS this frame.
 *
 * MAR-181's comment here claimed this bool was MAR-182's completion signal, and
 * that a cancelled action was `!state->file_path_request.has_value() &&
 * !applied`. That predicate is also true on every IDLE frame and on every frame
 * after a SUCCESSFUL action, which clears the request too, so it cannot
 * distinguish a cancel from either. MAR-182 does not use it: the intent's own
 * `DirtyIntentPhase` is the signal. This bool keeps its original, narrower
 * meaning -- "a pending action ran to success this frame" -- and its signature
 * is unchanged.
 */
bool apply_pending_file_action(ShellState* state);

/**
 * @brief THE gate. Every session-replacing or terminating surface calls this.
 * @post Either the intent has been performed, or `dirty_intent` holds it.
 *
 * Reads `EditorSession::dirty()`, which is content-keyed -- it compares the live
 * document against the bytes last written or read. It deliberately does not read
 * `ShellState::project_dirty`, which is a display cache refreshed only where
 * someone remembered to call `update_project_dirty_state` and assigned from a
 * caller-supplied boolean in `adopt_session_project_into_shell`.
 *
 * @param path An optional destination, honoured ONLY for `SessionIntent::Open`.
 *             A Recent-projects entry is the one origin that supplies it;
 *             every other caller passes none and gets the chooser. When the
 *             prompt is already up, a second call retargets BOTH fields, so an
 *             intent that carries no path clears any destination the previous
 *             one left behind.
 */
void begin_session_intent(
    ShellState* state,
    SessionIntent intent,
    const std::filesystem::path& path = {});

/**
 * @brief Answers a live prompt.
 *
 * Returns void deliberately. `Save` has THREE outcomes -- completed, waiting on
 * a destination, failed -- and no bool carries three. The state IS the signal,
 * and every field of it is on `ShellState` where a UI-free test can read it.
 */
void resolve_dirty_intent(ShellState* state, DirtyIntentResponse response);

/**
 * @brief Re-evaluates an `AwaitingSave` intent. Called by the modal every frame.
 *
 * Exported so a UI-free test can drive the same evaluation the frame does: the
 * transition out of `AwaitingSave` is the one MAR-182 state change that a
 * headless case cannot otherwise observe.
 */
void tick_dirty_intent(ShellState* state);

/**
 * @brief Folds a host close-request into the intent machine.
 * @return Whether the host's latch must be CLEARED, i.e. the exit is vetoed.
 *
 * Split out of `shell_main.cpp`'s loop on purpose. The loop itself is
 * unreachable from any headless test -- `run_headless_smoke` returns before a
 * window host is ever created -- so the decision lives in a pure function a
 * smoke can call with `true` and `false` and assert both answers.
 */
bool absorb_close_request(ShellState* state, bool host_close_requested);

/** @brief Draws the prompt, the New form and the shared chooser. Root scope only. */
void draw_file_path_modals(ShellState* state);

} // namespace marrow::editor::shell
