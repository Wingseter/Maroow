# MAR-181 Add Core File Path Workflows — Design

- Story: `MAR-181`, "Add core File path workflows", `dependsOn: ["MAR-180"]`
- Branch: `feat/mar-168`
- Date: 2026-08-30

## 0. Where this story sits

MAR-180 finished the **primitives**: atomic save, Save As rebasing, history
rebasing, `EditorSession::create` / `close` / `adopt_runtime_sources`. MAR-181 is
the **shell layer on top of them**. MAR-182 owns the dirty-session intent state
machine; MAR-183 owns recent projects.

The `dependsOn: ["MAR-180"]` edge is real coupling, not sequencing: `create`
(`session.hpp:167`), `save`'s Save As rebasing (`session.cpp` `EditorSession::save`),
and the atomic writer (`atomic_file_write.hpp`) are all consumed directly here.
MAR-181 adds no new model-layer primitive; every session call it makes already
exists and is already tested.

## 1. The measured gap

Everything in this section was read out of the tree on 2026-08-30 and is cited by
`file:line`. Nothing is inferred from the story text.

### 1.1 The File menu has exactly two items, and neither is New / Open / Save

`draw_menu_bar` (`shell_project_panels.cpp:701`) opens `BeginMenu("File")` at
`:713` and emits precisely two items:

| Line | Item | Effect |
|---|---|---|
| `:714-720` | `Reload Project` | `*reload_requested = true` (applied at end of frame) |
| `:721-723` | `Quit` | `ProjectMenuAction::QuitRequested` |
| `:724` | `EndMenu()` | — |

There is **no `New`, no `Open`, no `Save`, no `Save As`, and no `Close` anywhere in
the menu bar.** `grep -n "MenuItem" src/editor/shell_project_panels.cpp` returns
`:714, :721, :728, :735, :749, :763, :779, :831, :835, :845, :853` — the File
menu's two, Edit's Undo/Redo, and View/Window/Help.

Save and Export exist **only as toolbar icon buttons**, in two places:

- `draw_shell_toolbar` (`:618`): Save at `:635` (`save_project_file(state, true)`),
  Export at `:640`, Reload at `:644`.
- `draw_project_window` (`:904`): Reload at `:916`, Save at `:920`, Export at `:929`.

So the as-built inventory for the four story actions is:

| Action | Menu item | Any other surface | Model primitive |
|---|---|---|---|
| **New** | absent | **absent everywhere** | `EditorSession::create` (`session.hpp:167`) exists, unused by the shell |
| **Open** | absent | **absent everywhere** — `reload_project` reaches `session.open` only for the CLI path | `EditorSession::open` (`session.hpp:182`) |
| **Save** | absent | two toolbar buttons (`:635`, `:920`) → `save_project_file` (`shell_core.cpp:647`) | `EditorSession::save` |
| **Save As** | absent | **absent everywhere** | `EditorSession::save(path)` — takes a path, and the shell never passes a different one |

`EditorSession::create` has **zero callers in `src/editor`** outside
`session.cpp` itself. `EditorSession::close` likewise. MAR-180 built them for this
story and nothing has used them yet.

### 1.2 There is no file dialog capability anywhere in the tree

Measured, not assumed. A case-insensitive sweep for every common spelling —
`nfd`, `tinyfiledialog`, `portable-file-dialogs`, `IGFD`, `ImGuiFileDialog`,
`FileDialog`, `OpenFileName`, `NSOpenPanel`, `GtkFileChooser`, `file_dialog` —
over `src/`, `include/`, `tools/`, `CMakeLists.txt`, `AGENTS.md` and `docs/root1/`
returns **exactly one hit**, and it is prose:

```
docs/root1/research-windowing-glfw-vs-sdl3.md:27:
| 네이티브 파일 다이얼로그 | 없음 | `SDL_ShowOpenFileDialog` |
```

That is the SDL3 migration research note observing that SDL3 *offers*
`SDL_ShowOpenFileDialog`. **It is not called anywhere.** No vendored file-dialog
library appears in `THIRD_PARTY.md` or the CMake dependency graph.

Every path in the editor today comes from one of exactly three sources:

1. `--project <path>` on the command line (`shell_main.cpp:96`, `:165`), stored
   into `Options::project_path` and copied to `ShellState::project_path`
   (`shell_main.cpp:735`).
2. Paths stored *inside* the `.marrow` and resolved relative to it
   (`ProjectData::resolve_path`).
3. Hard-coded fixture paths in smokes.

**Consequence for this design:** the story's "native ImGui directory/path modal
with no new external dependency" is not a stylistic preference, it is the only
option that keeps the workflow testable. `SDL_ShowOpenFileDialog` is a blocking,
OS-owned, asynchronous surface that a headless smoke cannot drive at all, and it
would make every acceptance criterion in this story unverifiable. §3.1 records the
decision and its cost.

### 1.3 `save_project_file` writes to `ShellState::project_path`, not to the project

`save_project_file` (`shell_core.cpp:647`) saves to `state->project_path`
(`:656`), and `reload_project` (`:544`) opens `state->project_path` (`:561`). The
session's own `project()->source_path` is used only to decide reload-vs-open
(`:558`).

Meanwhile the **agent** save path saves to `project.source_path`
(`agent_handlers_management.cpp:69-77`).

These two agree today only because nothing ever moves the project. **A Save As
that updates `project()->source_path` without updating `ShellState::project_path`
would leave the toolbar's Save writing back to the old file** — and, after
MAR-180, rebasing the references back to the old directory on the way. Keeping the
two in step is a hard requirement of this story, not a nicety (§4.4, §5 F-rows).

### 1.4 `reload_project` is the shell's only session-adopting path, and it hardcodes "clean"

`reload_project` (`shell_core.cpp:544-645`) is ~100 lines that do three separable
things:

1. `:557-561` decide reload-vs-open and call the session.
2. `:566-599` reset presentation state — box selections, preview pointers,
   timeline editor, skins, slot overrides, playhead, loop, `preview_speed`,
   pending edit action — and then, at **`:592-593`**:
   ```cpp
   state->project_dirty = false;
   state->saved_project_snapshot.clear();
   ```
3. `:601-643` repopulate from the new `load_result`, pick the animation, sync,
   initialize the camera, `reset_runtime_asset_watch`, reconcile selection and the
   hierarchy anchor.

Step 2's `project_dirty = false` is correct for open and reload — both adopt a
project that exists on disk. **It is wrong for New**, which MAR-180 deliberately
made dirty-from-birth (`session.cpp` `create`: `saved_serialized_project.clear();
project_dirty = true;`). A New that reused `reload_project` verbatim would paint
the session clean over a file that does not exist — destroying exactly the warning
MAR-180 built.

There is a second, narrower defect in the same function: `sync_shell_from_editor_session`
is called only on the **reload** branch (`:622-623`). On the **open** branch the
three `observed_*_revision` fields are never updated, so the first
`sync_shell_from_editor_session_if_revised` of the next frame does a redundant
resync. That is currently harmless — it self-corrects — and MAR-181 does not
change it, but the extraction in §4.5 must not make it worse.

### 1.5 `ShellState::saved_project_snapshot` is write-only

`grep -rn "saved_project_snapshot" src/` returns exactly four lines:

```
src/editor/shell_state.hpp:853:    std::string saved_project_snapshot;
src/editor/shell_core.cpp:593:    state->saved_project_snapshot.clear();
src/editor/shell_core.cpp:599:    state->saved_project_snapshot = serialize_project(...);
src/editor/shell_core.cpp:663:    state->saved_project_snapshot = serialize_project(...);
```

One declaration and three writes. **Zero reads.** The real dirty baseline is the
session's `saved_serialized_project`, compared by `EditorSession::update_dirty`,
and the shell reads it through `state->session.dirty()`
(`update_project_dirty_state`, `shell_core.cpp:251-258`).

This is recorded because a plausible-looking MAR-181 would "keep the snapshot in
step" for New and Save As and add a fourth write to a dead field. **MAR-181 adds
no write to it and removes none** — deletion is a separate cleanup, and touching
`shell_state.hpp` for a dead field while other stories are in flight buys nothing.

### 1.6 What already exists and is therefore not re-solved

Stated plainly so the plan does not rebuild MAR-180:

- **Atomic save.** `save_project` (`project.cpp`) rebases, then writes through
  `detail::write_file_atomically`. Every handled failure leaves the destination
  byte-for-byte unchanged.
- **Save As rebasing.** `rebase_project_paths` (`project.cpp:7339`) rewrites the
  five serialized path families identity-preservingly, leaving empty and absolute
  references alone. `EditorSession::save` rebases **and re-serializes** every
  history snapshot when `source_changed`.
- **`create`.** Refuses during a transaction; runs `load_skeleton_document` →
  `build_project_runtime` → `AtlasLoader::load` per atlas → `PreviewController::bind`
  entirely into locals; commits with `saved_serialized_project` empty and
  `project_dirty = true`; bumps all three revisions. A failure touches nothing.
- **`close`.** Refuses during a transaction; clears everything; **bumps** the three
  revisions.
- **Adoption.** `adopt_runtime_sources` is all-or-nothing.
- **Preview normalization.** `PreviewController::normalize_state`
  (`session.cpp:723-758`) drops unknown skins and falls back to the first
  animation when `animation_name` does not resolve. This is why §4.1's default
  metadata is safe.
- **The modal idiom.** `request_* / confirm_* / cancel_*` UI-free seams plus a
  thin `BeginPopupModal` that only calls them — `apply_constraint_catalog_action`
  (`shell_constraints.hpp:70`), `apply_animation_catalog_action`
  (`shell_project_panels.hpp:22`). MAR-181 follows it exactly.
- **The headless modal harness.** `shell_smoke_constraints.cpp:686-830` renders
  real frames, sweeps a real mouse for `HoveredId`, finds a modal by
  `ImGui::FindWindowByName(<full BeginPopupModal string>)`, and clicks its buttons.
  MAR-181 reuses this shape rather than inventing a second one.

## 2. Goal

Give the File menu the four path workflows the editor is missing, driven by one
dependency-free ImGui modal, on top of MAR-180's primitives, such that:

1. Every action's failure leaves the session, the shell, and the file on disk
   exactly as they were.
2. A New project is dirty in memory and **absent from disk** until an explicit
   Save.
3. `ShellState::project_path` and `project()->source_path` never diverge.
4. MAR-182 can insert its dirty-intent gate at one named seam with no rework.

## 3. The path modal

### 3.1 Why an in-ImGui modal, and what it costs

**Adopted: a modal drawn with ordinary ImGui widgets over
`std::filesystem::directory_iterator`.** Reasons, in order of weight:

- It is the only option a headless smoke can drive. `marrow_editor_shell
  --auto-close N` runs the whole UI through `ImGui::NewFrame` / `ImGui::Render`
  with no window and no OS event loop (`shell_smoke.cpp:54-72`,
  `shell_smoke_frames.cpp:48-125`). A native dialog blocks on the platform and
  returns nothing under that harness, so **every acceptance criterion in this
  story would become unverifiable**.
- The story requires it verbatim: "A native ImGui directory/path modal with no new
  external dependency".
- `THIRD_PARTY.md` is hash-verified by `marrow_verify_third_party`. Adding a
  vendored dialog library is a dependency-provenance change, which is a different
  story.

**The cost, stated rather than hidden:** the user gets a bespoke browser, not the
OS one. No sidebar of favourites, no network volumes, no OS-level recent list, no
drag-and-drop from Finder/Explorer, and no OS "replace existing file?" prompt.
`SDL_ShowOpenFileDialog` is available under the SDL3 backend the tree already
links (`docs/root1/research-windowing-glfw-vs-sdl3.md:27`) and is the right answer
for a *shipping* editor; adopting it is a later story that must first solve the
headless-testing problem it creates. Recorded as a non-goal in §10, not dismissed.

### 3.2 The modal's contract

One modal, one `BeginPopupModal` string, three modes:

```cpp
enum class FilePathMode {
    OpenExisting,   //  the chosen path must already exist and be a regular file
    SaveTarget,     //  the chosen path need not exist; its parent directory must
};
```

```
constexpr char kFilePathModal[] = "Choose Path##file_path";
```

Contents, top to bottom:

| Element | Behaviour |
|---|---|
| Title line | The request's caption, e.g. `Open Project`, `Save Project As`, `New Project — skeleton (.mskl)` |
| Current directory | The absolute, `lexically_normal` directory being browsed |
| `..` row | Present unless the directory equals its own `parent_path()` (the filesystem root) |
| Directory rows | Every subdirectory, sorted by name. Click navigates. |
| File rows | Every regular file whose extension equals the request's filter, sorted by name. Click fills the name field. `OpenExisting` shows only matching files; `SaveTarget` shows them too, so the user can overwrite deliberately. |
| `Name` `InputText` | The file name. `EnterReturnsTrue`. |
| Diagnostic line | The single reason the current selection is not acceptable, or the empty string |
| `Choose` / `Cancel` | `Choose` is disabled whenever the diagnostic is non-empty |

**Listing errors are shown, never thrown.** Every filesystem call uses the
`std::error_code` overload — `std::filesystem::directory_iterator(dir, ec)`,
`is_directory(entry, ec)`, `exists(p, ec)`. An unreadable directory renders as a
one-line message in place of the list and leaves navigation available; it is never
an exception and never an empty list that looks like an empty directory. This
matters: the modal browses arbitrary user filesystems, where `EACCES` is ordinary.

**The listing is recomputed every frame.** No cache, no watcher. The directories a
user browses are small and this is an explicit, short-lived modal; a cache would
buy nothing and would need an invalidation rule.

### 3.3 The request lives in `ShellState`, and that is required, not stylistic

```cpp
struct FilePathRequest {
    FileAction     action{FileAction::Open};   // which workflow asked
    FilePathMode   mode{FilePathMode::OpenExisting};
    std::string    caption;
    std::string    extension;                  // ".marrow" / ".mskl" / ".matl"
    std::filesystem::path directory;           // absolute, normalized
    std::array<char, 512> name{};
    bool opened{false};                        // has ImGui::OpenPopup run yet
};
```

`ShellState` gains **one** field: `std::optional<FilePathRequest> file_path_request;`.

Two independent reasons it cannot be a file-static global the way
`g_animation_catalog_popup` (`shell_project_panels.cpp:41`) and
`g_constraint_catalog_popup` are:

1. **`ImGui::OpenPopup` from inside a menu does not open a root-level modal.**
   `OpenPopup(const char*)` hashes the string against `GetCurrentWindow()`'s id
   stack. Inside `BeginMenu("File")` the current window is the *menu popup*, so the
   id it computes is not the id `BeginPopupModal(kFilePathModal)` computes at root
   scope, and the menu popup is destroyed the instant the item is clicked. The two
   existing popups work because their `OpenPopup` and their `BeginPopupModal` are
   both inside `draw_project_window` / the constraints window — the same window
   scope. A menu item must therefore **record a request** and let a root-scope
   drawer open the popup on a later frame. `FilePathRequest::opened` is that latch.
2. **MAR-182 must observe a cancel.** Its acceptance criterion "save-path
   cancellation" means: a pending Quit/New/Open intent chose *Save*, the Save As
   modal opened, and the user cancelled — the pending intent must survive and must
   not fall through. That is only expressible if the request's lifetime is
   inspectable state rather than a hidden global (§6).

The two existing file-static popup states are **not** migrated. That is a
refactor with no story behind it.

### 3.4 Path normalization and the extension rule

One function, used by every mode:

```
resolve_choice(directory, typed_name, mode, extension) -> path or diagnostic
```

1. Trim ASCII whitespace from both ends of `typed_name`. An empty result →
   `"Enter a file name."`
2. Build `candidate = std::filesystem::path(typed_name)`. If it is absolute, use
   it as-is; otherwise `candidate = directory / candidate`. Then
   `candidate = candidate.lexically_normal()`. Typing an absolute path or a
   `../sibling/x.marrow` into the name field therefore works, which is the escape
   hatch for anything the browser makes awkward.
3. **Extension.** If `candidate.extension()` is empty, append `extension`. If it is
   non-empty and does not compare equal to `extension`, that is a diagnostic:
   `"Expected a <extension> file."` The comparison is a plain byte comparison of
   `extension().string()` — no case folding. macOS and Windows filesystems are
   usually case-insensitive and Linux is not; folding here would make the editor's
   own acceptance depend on the host, and a rejected `.MARROW` is a one-keystroke
   fix while a silently-accepted one is a portability bug.
4. `mode == OpenExisting`: `exists(candidate, ec)` must be true and
   `is_regular_file(candidate, ec)` must be true → else
   `"That file does not exist."` / `"That is a directory."`
5. `mode == SaveTarget`: `candidate.parent_path()` must exist and be a directory →
   else `"The destination folder does not exist."`; and `candidate` must not itself
   be an existing directory → else `"That is a directory."` An existing **file** is
   accepted, with the diagnostic slot instead carrying an informational
   `"Replaces the existing file."` — a Save As over an existing project is a
   legitimate, deliberate act, and MAR-180 makes it atomic.

The rule is **content-keyed throughout**: every decision reads what the path *is*
on disk right now, never how it was produced. In particular the modal never
remembers "this came from the browser list" versus "this was typed"; both go
through `resolve_choice` identically.

The initial `directory` is `state->project_path.parent_path()` made absolute, or
the process's `current_path()` when that is empty or does not exist. There is no
persisted "last used directory" — that is preference state, and preference state
for the File workflow is MAR-183's territory.

## 4. The four actions

All four route through one enum and one entry point:

```cpp
enum class FileAction { New, Open, Save, SaveAs };
void begin_file_action(ShellState* state, FileAction action);   // the MAR-182 seam
```

### 4.1 New

`File > New Project…` → `begin_file_action(state, FileAction::New)`.

New needs three inputs, so it uses a **small form modal of its own** —
`"New Project##file_new"` — whose three rows each hold a path text field and a
`Browse…` button that raises the shared modal from §3.2 in the matching mode:

| Row | Mode | Filter | Rule |
|---|---|---|---|
| Skeleton | `OpenExisting` | `.mskl` | required; must load |
| Atlases | `OpenExisting` | `.matl` | at least one; `Add…` appends, `Remove` drops the selected row; each must load |
| Project file | `SaveTarget` | `.marrow` | required; §3.4 rule 5 |

`Create` is disabled until all three rows validate structurally (non-empty,
extensions correct, target parent exists). Pressing `Create` runs the **validation
pass** and then, only if it passes, records a pending New.

**The validation pass**, in this order, so the first thing the user sees named is
the first thing that is wrong:

1. `runtime::load_skeleton_data(skeleton_path)` (`skeleton.hpp:1787`). Failure →
   the loader's error verbatim, modal stays open.
2. For each atlas, `runtime::AtlasLoader::load(atlas_path)`. Failure → the loader's
   error verbatim, naming the atlas, modal stays open.
3. Nothing else. The target path was already checked structurally and **must not
   be touched**: the story requires New to write nothing.

This pass is what the acceptance criterion "New validates an existing skeleton
source, at least one atlas, and a target `.marrow` path **before** constructing a
session" asks for, and it is not redundant with `EditorSession::create`'s own
loads. `create` reports failure through a `ProjectLoadResult` *after* the modal
has closed; the pass keeps the modal open with the field-level error next to the
field that is wrong.

**Metadata.** `MinimalProjectOptions` is filled with `project_path`,
`skeleton_path`, `atlas_paths`, and **nothing else** — `name` (derived by
`create_minimal_project` from the project file's stem), `active_animation`
(`"idle"`), `preview_skins` (`{"default"}`), `export_directory` (`"exports"`) and
`notes` take the primitive's documented defaults.

`active_animation = "idle"` on a rig that has no animation named `idle` is
**accepted deliberately**, and it is safe, by three measured links:

- `PreviewController::normalize_state` (`session.cpp:754-758`) replaces an
  unresolvable `animation_name` with the rig's first animation, so `create`'s bind
  cannot fail on it;
- `validate_project_for_save` checks only non-emptiness of preview skin names
  (`project.cpp:5622-5627`) and does not look at `active_animation` at all, so the
  project saves and reloads;
- the shell's animation pick prefers `editor_metadata.active_animation` only when
  it resolves and otherwise takes the first animation
  (`shell_core.cpp:610-619`).

The one observable consequence is `draw_project_window`'s "Authored default
animation:" line naming a clip the rig may not have, on a project that has not
been saved. Fixing it properly means an animation/skin picker in the New form,
which the story's criteria do not ask for. Recorded in §11 as a known limitation
rather than smuggled in.

**On `Create` accepted:** record a **pending New** (§4.6) — do not call
`EditorSession::create` from inside the popup.

**Applied (end of frame):**

1. `EditorSession::create(options)`. A failure sets `error_message` from the
   result's error and `status_message = "New project failed"`, and **changes
   nothing else** — not `project_path`, not the session, not selection, not
   history, not the dirty flag. `create` is all-or-nothing (MAR-180 F14).
2. On success, `state->project_path = options.project_path` **first**, then the
   §4.5 shell resync with `dirty_is_known_clean = false`.
3. `status_message = "New project (unsaved): " + project_path.string()`.

Post-state: `session.dirty()` true, `undo_count() == 0`, `redo_count() == 0`,
`selection` empty (the resync reconciles it against the new rig, and nothing in it
survives), `project_path` = the chosen target, **and the target does not exist on
disk**. That last clause is the acceptance criterion and it is asserted directly
(§9 C4).

### 4.2 Open

`File > Open Project…` → `begin_file_action(state, FileAction::Open)` → shared
modal, `OpenExisting`, `.marrow`, initial directory = the current project's
directory. `Choose` records a **pending Open**.

**Applied (end of frame):**

1. `state->session.open(chosen)`. Note the argument: **the chosen path, not
   `state->project_path`**. `reload_project` cannot be reused as-is here precisely
   because it opens `state->project_path`, which would require mutating the shell's
   path *before* knowing whether the open succeeds.
2. On failure: `error_message` = the load error's `format()`,
   `status_message = "Project load failed"`, return. `EditorSession::open` is
   already atomic, so the session is untouched — **and `state->project_path` is
   untouched**, which is the point. §1.3's divergence is impossible.
3. On success: `state->project_path = chosen`, then the §4.5 resync with
   `dirty_is_known_clean = true`.
4. `status_message = "Opened " + chosen.string()`.

Opening the project that is already open is not special-cased. It goes through
`session.open` like any other, which is a full reload — the same observable result
as `Reload Project`, reached by a different route.

### 4.3 Save

`File > Save` (enabled when a project is loaded and no authoring gesture is
active) and `Ctrl+S` both call `begin_file_action(state, FileAction::Save)`.

Save takes **no modal**. It calls `save_project_file(state, true)`
(`shell_core.cpp:647`) unchanged — the same function the two toolbar buttons
already call. `ShellState::project_path` is never empty in the shipped shell
(`Options::project_path` defaults to `assets/fixtures/player_idle.marrow`,
`shell_state.hpp:42`, and New requires a target up front), so Save always has a
destination. The one-line guard `if (state->project_path.empty()) { return
begin_file_action(state, FileAction::SaveAs); }` is kept anyway, because the
invariant is an argument and not a type.

Failure behaviour is MAR-180's and is unchanged: the file is byte-for-byte
preserved, `project_dirty` stays true, `status_message = "Project save failed"`.
This is already covered by shell case C3 (`shell_smoke_project.cpp:943`), which
MAR-181 must leave passing.

**`Ctrl+S`** is added to `handle_project_history_shortcuts`
(`shell_preview.cpp:205`), beside the existing `Ctrl+Z` / `Ctrl+Shift+Z` /
`Ctrl+Y` / `Space` / `Home` / `Ctrl+L` bindings. It inherits that function's two
existing gates for free: it returns when no project is loaded (`:206-208`) and
when `io.WantTextInput` (`:211-213`), so typing a name into the path modal cannot
trigger a save. Save is the only one of the four actions that gets a shortcut:
it is the only one that neither replaces the session nor needs a modal, so it
needs no MAR-182 gate and cannot surprise the user.

### 4.4 Save As

`File > Save As…` → `begin_file_action(state, FileAction::SaveAs)` → shared modal,
`SaveTarget`, `.marrow`, initial directory = the current project's directory,
initial name = the current project's filename. `Choose` applies **immediately**,
inside the popup, not at end of frame (§4.6).

**Applied:**

1. `state->session.save(chosen)`.
2. On failure: `error_message` = the save error's `format()`,
   `status_message = "Project save failed"`; **`state->project_path` is not
   changed**; the modal stays open so the user can pick a different destination.
   MAR-180 guarantees the destination file's previous bytes and the session are
   untouched.
3. On success: `state->project_path = chosen`;
   `update_project_dirty_state(state)` (which reads `session.dirty()`, now false);
   `reset_runtime_asset_watch(state)`; `error_message.clear()`;
   `status_message = "Saved project to " + chosen.string()`; close the modal.

`reset_runtime_asset_watch` is called for a reason that is worth stating because
it is also an assertion: the watch list is built from
`absolutize_path(resolved_skeleton_path())` and each `resolved_atlas_paths()` entry
(`shell_asset_watch.cpp:91-100`). MAR-180's rebase is identity-preserving, so
after a Save As the recomputed list must be **element-wise equal** to the list
before it. Recomputing it is therefore free, and comparing it is a cheap,
load-bearing check that the rebase did what it claims (§9 C5).

Save As does **not** touch selection, history, the preview, the playhead, or the
runtime. `EditorSession::save` rebases and re-serializes history snapshots
internally and bumps `project_revision` when the source changed; the shell's
existing `sync_shell_from_editor_session_if_revised` picks that up on the next
frame with no new wiring.

### 4.5 The shell resync, extracted

New and Open both need everything `reload_project` does at `shell_core.cpp:566-643`
except the hardcoded `project_dirty = false`. Extract it, unchanged, into:

```cpp
// shell_core.cpp / shell_state.hpp
void adopt_session_project_into_shell(
    ShellState* state,
    const std::string& previous_animation_name,
    double previous_timeline_time,
    bool previous_timeline_loop,
    bool previous_timeline_playing,
    bool restore_transient_playback,   //  reload_project's `reload_current_project`
    bool project_is_clean);            //  false for New, true for Open and reload
```

Body: `shell_core.cpp:566-643` verbatim, with exactly two substitutions:

- `:592-593`'s `state->project_dirty = false; state->saved_project_snapshot.clear();`
  becomes `state->project_dirty = project_is_clean ? false : true;` followed by the
  same `saved_project_snapshot.clear();`. The snapshot write at `:599` is kept as
  it is — §1.5 established it is dead either way, and changing a dead field's
  contents is churn.
- `:622`'s `if (reload_current_project)` becomes `if (restore_transient_playback)`.

`reload_project` then becomes its first 22 lines plus one call, with
`project_is_clean = true`. **This is a pure refactor with no behaviour change**,
and the gate for it is that the entire existing suite passes with a zero-line diff
in every test file (plan Task 1).

Why `project_is_clean` is a parameter and not `state->session.dirty()`: after
`create` the session *is* dirty, so reading it would give the right answer today.
But the shell's dirty flag is the user's last warning, and a parameter states the
caller's intent at the call site where a reviewer can see it, rather than making it
depend on a session field whose meaning a later story could shift. The plan's
Task 5 asserts both — that New's shell flag is true **and** that
`session.dirty()` is true — so a divergence between them fails.

### 4.6 Immediate versus deferred application

The house discipline, read out of `shell_main.cpp`: the session-replacing action
is **deferred**. `Reload Project` sets `*reload_requested` inside `draw_menu_bar`
(`shell_project_panels.cpp:719`) and `reload_project` runs at
`shell_main.cpp:610`, after every window has drawn. The non-replacing actions are
**immediate**: `save_project_file` is called straight from
`draw_shell_toolbar` (`:635`) and `draw_project_window` (`:920`), mid-frame.

MAR-181 follows exactly that split:

| Action | Replaces the session? | When applied |
|---|---|---|
| New | yes (`create`) | **deferred** — end of frame |
| Open | yes (`open`) | **deferred** — end of frame |
| Save | no | immediate |
| Save As | no | immediate |

Deferral is implemented with a second `std::optional` on `ShellState`,
`pending_file_application`, carrying the action and its resolved inputs, and a
single new function `apply_pending_file_action(ShellState*)` called from the two
frame bodies right after the existing `reload_project` call:

- `shell_main.cpp:610-612` (`render_shell_frame`)
- `shell_smoke_frames.cpp:124-127` (`render_headless_smoke_frames`)

**Those two frame bodies are hand-maintained duplicates of each other.** Editing
one and not the other is the single likeliest way to ship a MAR-181 whose New/Open
work interactively and are invisible to the smoke. The plan makes editing both one
task with an explicit two-site checklist.

Honesty about the strength of the argument: the two derived caches
(`TimelineTrackCache`, `SlotDerivedCache`, `shell_state.hpp:331-351`) are keyed on
`runtime_revision` plus skeleton identity and would self-invalidate under
mid-frame replacement, so deferral is not required for *cache* safety. It is
required so that windows drawn earlier and later in the same frame describe the
same project, and it is what the tree already does for `Reload`. Consistency with
an existing, working discipline beats a new one that happens also to work.

### 4.7 Where the modals are drawn

Both modals are drawn from the tail of `draw_menu_bar`, immediately after
`draw_shell_toolbar(reload_requested, state)` (`shell_project_panels.cpp:900`),
which is root scope — `BeginViewportSideBar` / `End` have already balanced.

One call site, not two, and it is automatically reached by both frame bodies
(`shell_main.cpp:558`, `shell_smoke_frames.cpp:64`) with no edit to either. This
is the reason to put it there rather than in `render_shell_frame`.

`draw_menu_bar`'s early return at `:702-704` — taken when `BeginMainMenuBar`
returns false — must also draw the modals, or an interaction would vanish for as
long as the menu bar is clipped. That branch becomes:

```cpp
if (!ImGui::BeginMainMenuBar()) {
    draw_file_path_modals(state);
    return ProjectMenuAction::None;
}
```

## 5. Failure taxonomy

`P` is the destination `.marrow` on disk; `S` is the live session; `H` is the
shell's `project_path`. "unchanged" means bit-identical, and for `S` it means the
six-value snapshot (`serialize_project`, the three revisions, `undo_count`,
`dirty()`).

| # | Action | Failure point | `P` | `S` | `H` | User-visible |
|---|---|---|---|---|---|---|
| N1 | New | modal cancelled | untouched | unchanged | unchanged | modal closes, no message |
| N2 | New | skeleton fails to load | untouched | unchanged | unchanged | modal **stays open**, loader error on the skeleton row |
| N3 | New | an atlas fails to load | untouched | unchanged | unchanged | modal **stays open**, loader error naming that atlas |
| N4 | New | target's parent directory absent | untouched | unchanged | unchanged | `Create` disabled, diagnostic on the target row |
| N5 | New | `EditorSession::create` fails after the pass | **untouched — nothing is ever written** | unchanged | unchanged | `error_message`, `"New project failed"` |
| N6 | New | success | **untouched — still absent** | replaced, **dirty**, empty history | = target | `"New project (unsaved): …"` |
| O1 | Open | modal cancelled | untouched | unchanged | unchanged | modal closes |
| O2 | Open | chosen file is not valid `.marrow` JSON | untouched | unchanged | **unchanged** | `error_message`, `"Project load failed"` |
| O3 | Open | JSON parses but a referenced `.mskl`/`.matl` is missing | untouched | unchanged | **unchanged** | as O2, with the loader's message |
| O4 | Open | success | untouched | replaced, **clean** | = chosen | `"Opened …"` |
| S1 | Save | authoring gesture active | untouched | unchanged | unchanged | `"Finish the active edit before saving"` |
| S2 | Save | write / rename fails | **previous bytes** | unchanged, **still dirty** | unchanged | `"Project save failed"` |
| S3 | Save | success | new bytes; `load_project(P)` succeeds | clean | unchanged | `"Saved project to …"` |
| A1 | Save As | modal cancelled | untouched | unchanged | unchanged | modal closes |
| A2 | Save As | `validate_project_for_save` rejects | untouched | unchanged, **still dirty** | **unchanged** | modal **stays open** with the message |
| A3 | Save As | write / rename fails | **previous bytes of the destination** | unchanged, **still dirty** | **unchanged** | modal **stays open** |
| A4 | Save As | success | new bytes; `load_project(P)` succeeds **and its resolved asset paths equal the originals'** | clean, history rebased and re-serialized by MAR-180 | = chosen | `"Saved project to …"` |

Two rows carry the load: **O2/O3's `H` unchanged** (§1.3's divergence) and
**A3's `H` unchanged** (the same divergence on the write side). Both are asserted
directly in §9.

## 6. The seam MAR-182 fills

MAR-182 must route New, Open, Reload, Quit and OS close requests through one
Save/Discard/Cancel machine. MAR-181 leaves it exactly one insertion point and
does not implement any part of the machine.

```cpp
/**
 * @brief Entry point for every File action.
 *
 * MAR-182 inserts the dirty-session intent gate at the TOP of this function:
 * when `state->session.dirty()` and the action replaces the session
 * (New, Open), it records a pending intent and returns without reaching the
 * body below. Save and SaveAs never gate -- Save IS the resolution.
 */
void begin_file_action(ShellState* state, FileAction action);
```

Four properties make MAR-182 a pure addition rather than a rewrite:

1. **One entry point.** Every File surface MAR-181 adds — four menu items and
   `Ctrl+S` — calls `begin_file_action` and nothing else. Nothing calls the modal
   or the session directly. `Reload Project` and `Quit` keep their current wiring
   (`*reload_requested`, `ProjectMenuAction::QuitRequested`); MAR-182 will route
   those two through the same gate, and MAR-181 does not move them, so it cannot
   conflict.
2. **A separately observable completion.** `apply_pending_file_action` returns a
   `bool` — did a pending action run to success this frame. MAR-182's "Save
   completes the pending intent only after an atomic save succeeds" reads that
   return value; it does not need to re-derive success from shell fields.
3. **A cancel MAR-182 can see.** `state->file_path_request` is cleared on Cancel
   and on success alike, but `apply_pending_file_action` returns `false` in the
   first case and `true` in the second. So "the Save As modal was cancelled while
   a Quit intent was pending" is `!state->file_path_request.has_value() &&
   !applied`, with the pending intent — MAR-182's own field — still set. This is
   the criterion MAR-181 exists to make expressible.
4. **No dirty check anywhere in MAR-181.** Not in the menu items' enabled state,
   not in the modal, not in `apply_pending_file_action`. A New over a dirty session
   in a MAR-181-only tree therefore **discards unsaved work silently**. That is a
   real, stated gap that MAR-182 closes, and it is written down here rather than
   half-implemented — a partial dirty check in MAR-181 is exactly the rework
   MAR-182 would have to undo.

## 7. Registry

**Unchanged at 64.** MAR-181 is entirely shell-layer: menu items, two modals, one
shell function extraction, one shortcut. It adds no agent operation, changes no
`OperationSpec` row, and adds no `types.Tool`. New, Open, Save As and the path
modal have **no agent or MCP surface at all** — the agent already has `save`
(`agent_dispatch.cpp:87`, management, mutating, requires review) and needs no
"open a different project" verb, which would let a remote agent replace the
operator's session out from under them.

Measured on 2026-08-30, to be re-measured in plan Task 0:

| Site | Measured |
|---|---|
| `kOperationSpecs` rows (`agent_dispatch.cpp:29-95`) | **64** |
| Split | **edit 39 / inspection 12 / management 10 / validation 3** |
| `operation_count_before != 64U` guards | **10** — `shell_smoke_constraints.cpp:147,676`; `shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603`; `shell_smoke_timeline.cpp:3697` |
| Guard messages | **10**, one line below each guard |
| `std::array<OperationExpectation, 64>` | **1** — `agent_dispatch_smoke.cpp:39` |
| Python assertions | **2** — `test_client.py:53,55` |

The claim is proved **by zero-line diff**, as MAR-179 and MAR-180 proved it:
`git diff --stat` must show **no change at all** to `src/editor/agent_dispatch.cpp`,
`src/editor/agent_handlers_*.cpp`, `src/samples/agent_dispatch_smoke.cpp` or
`tools/`, and `git diff -U0` filtered for count-shaped literals must be empty.

Blind substitution is unsafe and is forbidden. `AGENTS.md` carries `t = 0.62` and
a self-referential "62 lines"; the theme carries `(51, 56, 64)` and `rgb(54,57,64)`;
`test_client.py:1525` / `:1530` carry mesh coordinates `-64` / `64` while `:1484`
is an ordinal. Never touch `IM_COL32(56, 61, 69, 255)`, `"x": 56.0`, `56,995,840`,
`PhysicsBoneState … 56 bytes/bone`, or `IM_COL32(208,134,57,230)`.

## 8. Compatibility

- **`.marrow` schema: unchanged.** MAR-181 writes projects only through
  `EditorSession::save` → `save_project`, which is MAR-180's code untouched. New
  produces a project through `create_minimal_project`, which emits the same
  document shape as any other project. No key is added, removed or renamed.
- **`.mskl` v1, `.mbin` v2: unchanged.** MAR-181 reads skeletons only through
  `runtime::load_skeleton_data` and atlases only through `runtime::AtlasLoader`,
  both unmodified.
- **C ABI v1: unchanged.** Nothing in `include/marrow/c/` is touched.
- **Agent / MCP protocol: unchanged** (§7).
- **`EditorPreferences`: unchanged.** No new preference key. The modal's starting
  directory is derived per-invocation from `project_path` (§3.4). Persisting a
  last-used directory or a recent list is MAR-183.
- **Existing shell behaviour: unchanged.** The toolbar's three buttons, `Reload
  Project`, `Quit`, and every existing keyboard shortcut behave exactly as they do
  today. The one edit to existing behaviour is §4.5's extraction, which is a pure
  refactor gated on a zero-line test diff.

## 9. Validation strategy

Two binaries. `marrow_editor_shell` carries all the new coverage, because every
one of these behaviours lives in the shell; `marrow_project_smoke` carries none —
its layer was fully covered by MAR-180's S1–S10 and MAR-181 adds no model
primitive.

**Global rule, inherited and non-negotiable: every "the project is still good"
assertion reloads from disk via `marrow::editor::load_project(path)`.**
`validate_project_for_save` takes no base document (`project.cpp:5503`) and cannot
resolve a cross-reference, so a `ProjectSaveResult` that is `ok` proves nothing,
and `json::load_document` proves only that the bytes parse — which is exactly how
the pre-MAR-180 Save As bug stayed invisible at
`editor_project_smoke.cpp:1531`.

Cases are numbered **C4 onwards**; C1–C3 are MAR-179's and MAR-180's and stay
untouched. All live in `src/editor/shell_smoke_project.cpp`, registered from
`validate_shell_foundation_smoke` (`:1446`) beside the existing
`validate_mar180_*` calls (`:1698`, `:1701`), and all seed their own temp project
via the existing `seed_shell_project_copy` (`:751`).

---

**C4 — New writes nothing, starts dirty, and is only real after Save.**

Seed a temp directory holding a copy of the fixture's `.mskl`, `.matl` and `.png`
but **no `.marrow`**. From a loaded shell state, run New with those sources and a
target `new_project.marrow` in that directory.

Assert, in order: `std::filesystem::exists(target)` is **false**;
`state.session.dirty()` and `state.project_dirty` are both **true**;
`state.session.undo_count() == 0` and `redo_count() == 0`;
`state.project_path == target`; `state.load_result.skeleton_data != nullptr`;
`state.preview_skeleton != nullptr`. Then `save_project_file(&state, true)` and
assert it succeeds, `exists(target)` is now **true**, `dirty()` is **false**, and
**`marrow::editor::load_project(target)` succeeds with a non-null
`skeleton_data`**.

*Observable because:* `exists(target) == false` before the save can only hold if no
code path wrote it, and it is checked against the filesystem, not a flag. Invert by
making the New path call `save_project_file` after `create` — the first `exists`
assertion fails immediately. Invert instead by passing
`project_is_clean = true` into §4.5's resync and the `dirty()` assertion fails,
while every other assertion still passes — which is what proves the two inversions
cover different defects.

**C5 — a cross-directory Save As moves the shell's path and keeps every asset
resolving.**

Load a seeded project in directory `A`. Record
`current_runtime_asset_paths(state)` (`shell_asset_watch.hpp:28`). Save As to
`B/moved.marrow`, a sibling directory.

Assert: the save succeeds; `state.project_path == B/moved.marrow`;
`state.session.project()->source_path == B/moved.marrow` — **the two agree**;
`state.project_dirty` is false; **`load_project(B/moved.marrow)` succeeds** and its
`resolved_skeleton_path()` equals the original project's `resolved_skeleton_path()`;
the same for every entry of `resolved_atlas_paths()`; and
`current_runtime_asset_paths(state)` after the Save As is **element-wise equal** to
the recording taken before it. Finally assert `A`'s original `.marrow` still exists
and still opens.

*Observable because:* the `project_path` equality is the §1.3 divergence and
nothing else in the suite checks it — invert by omitting
`state->project_path = chosen` from the success branch and this single line fails
while every model-layer MAR-180 case still passes. The watch-list equality is the
identity-preservation of MAR-180's rebase observed at the shell layer; invert by
removing `rebase_project_paths` from `save_project` and both the watch-list
comparison **and** the `load_project` reload fail, whereas a
`json::load_document` assertion in the same place would pass. **Run that second
inversion and confirm the `json::load_document` form passes under it** — that is
the proof the reload is the load-bearing assertion.

**C6 — a failed Save As leaves the shell pointing where it was.**

Load a seeded project in `A`, dirty it with one committed transaction, record
`state.project_path` and the bytes of a pre-existing `B/target.marrow`. Inside an
RAII `ScopedRenameCallback` (`shell_smoke_project.cpp:713`) returning
`std::errc::permission_denied`, run Save As to `B/target.marrow`.

Assert: the action reports failure; `state.project_path` is **still `A`'s path**;
`state.session.project()->source_path` is still `A`'s path; `state.project_dirty`
and `state.session.dirty()` are both still **true**; `B/target.marrow`'s bytes are
identical to the recording and it still **opens** via `load_project`; and `A`'s
project still opens. Then release the seam, retry, and assert success plus a
`load_project` of the new file.

*Observable because:* the rename seam is the only thing between a written
temporary and the destination, so the failure is injected, not simulated. Invert by
moving `state->project_path = chosen` above the `session.save` call — the
`project_path` assertion fails and every byte assertion still passes, isolating
exactly the §1.3 defect. The RAII scope is mandatory: the seam is
**process-global** (`atomic_file_write.hpp`), shared with the settings writer, and
no preference save may happen inside it.

**C7 — a failed Open changes nothing, including the shell's path.**

Load a seeded project. Write a `broken.marrow` that is valid JSON but names a
`.mskl` that does not exist. Record the six-value session snapshot and
`state.project_path`. Run Open on `broken.marrow`.

Assert: the action reports failure; `error_message` is non-empty and
`status_message == "Project load failed"`; **`state.project_path` is unchanged**;
the six-value snapshot is unchanged; `state.preview_skeleton` and
`state.animation_state` are the same non-null pointers as before. Then Open the
good project and assert success, `project_path` updated, and
`state.session.dirty()` false.

*Observable because:* "valid JSON, unresolvable reference" is the exact shape that
`json::load_document` accepts and `load_project` rejects, so the case fails only if
the shell adopts a project it could not materialize. Invert by assigning
`state->project_path = chosen` before calling `session.open` and the path assertion
fails alone — the session assertions still pass, because `open` is already atomic.
That separation is the point: **C7 tests the shell's bookkeeping, not the
session's atomicity, which MAR-180 already proved.**

**C8 — the path modal's acceptance rule.**

Table-driven, UI-free, against `resolve_choice` (§3.4) directly: empty name;
whitespace-only name; name with no extension (→ extension appended); name with the
wrong extension (→ rejected); absolute typed path (→ used verbatim); `../x.marrow`
(→ normalized against the browse directory); `OpenExisting` on a missing file
(→ rejected); `OpenExisting` on a directory (→ rejected); `SaveTarget` whose parent
is missing (→ rejected); `SaveTarget` that is an existing directory (→ rejected);
`SaveTarget` that is an existing file (→ **accepted**, informational diagnostic).

*Observable because:* each row asserts both the returned path **and** whether a
diagnostic was produced. Invert any single branch of `resolve_choice` and exactly
one row flips. The `SaveTarget`-over-an-existing-file row is the one that would be
silently lost to an over-eager "safety" check, so it asserts acceptance, not
rejection.

**C9 — the modal and the menu items exist and a real mouse reaches them.**

Modelled directly on MAR-179's C6 (`shell_smoke_constraints.cpp:1006-1080`). Drive
real frames of `draw_menu_bar`. Sweep a real mouse across `##MainMenuBar` until
`HoveredId` equals `window->GetID("File")`; click; then sweep the opened File menu
popup for the ids of `"New Project..."`, `"Open Project..."`, `"Save"`,
`"Save As..."`, `"Reload Project"` and `"Quit"`. Then click `"Open Project..."` and
assert `ImGui::FindWindowByName("Choose Path##file_path")` is non-null and
`Active`; sweep that modal for `"Cancel"`, click it, and assert
`state.file_path_request` is `std::nullopt` and the session is unchanged.

*Observable because:* a widget that is not emitted cannot own an id equal to
`window->GetID(label)`, so a never-hovered label is a failure and never a skip.
This is the one observation the UI-free cases cannot make: C4–C7 call the shell
seams directly and would all pass with every menu item deleted.

*Measurement required, not assumed:* the ImGui window name of an open `BeginMenu`
popup is not documented in this design. The implementer **must measure it** (via
`ImGui::GetCurrentContext()->OpenPopupStack` after the click) and record the
measured name in the plan's results. If the menu popup proves undrivable under
this harness, the required fallback is to keep the modal half of C9 — which
targets an ordinary popup window and is known drivable from MAR-179 C6 — and to
**report the menu half as not achieved**, never to delete it quietly.

**C10 — Ctrl+S saves, and does not fire while typing.**

Drive frames with `io.AddKeyEvent` for `Ctrl` + `S`. Assert a dirty project becomes
clean and the file on disk **opens** via `load_project`. Then open the path modal,
focus its `Name` field so `io.WantTextInput` is true, send the same chord, and
assert `session.dirty()` is unchanged.

*Observable because:* `handle_project_history_shortcuts` returns early on
`io.WantTextInput` (`shell_preview.cpp:211-213`). Invert by putting the `Ctrl+S`
handler above that guard and the second half fails while the first still passes.

---

**Regression, unchanged and must stay passing:** `marrow_project_smoke`'s
MAR-180 S1–S10; shell C1 (hot reload), C2 (failed hot reload coherence,
`shell_smoke_project.cpp:838`), C3 (failed shell save, `:943`); the animation
catalog and duration smokes; and the whole `ctest` editor label. §4.5's extraction
is a pure refactor and its gate is that **no existing test file changes by a
single line**.

## 10. Non-goals

Stated so the plan cannot drift into them:

1. **`SDL_ShowOpenFileDialog` or any native OS dialog.** §3.1.
2. **Any new external or vendored dependency.** The story forbids it and
   `THIRD_PARTY.md` is hash-verified.
3. **The dirty-session check.** MAR-182 owns it entirely. §6 property 4 states the
   resulting gap explicitly.
4. **Recent projects, and any persisted last-used directory.** MAR-183.
5. **`File > Close`.** `EditorSession::close` exists (MAR-180) and MAR-181 does not
   call it. Every shell panel except `draw_project_window` (`:955-963`) assumes a
   loaded project; a projectless UI state is a design problem of its own and no
   acceptance criterion asks for it.
6. **A "Revert" item.** `Reload Project` already reloads from disk and is
   unchanged.
7. **Choosing where exports go after a Save As.** MAR-180 rebased
   `export_directory` by identity, so exports keep landing in the original folder.
   Offering the choice is a UI question this story's criteria do not raise.
8. **An animation/skin picker in the New form.** §4.1 and §11.
9. **Deleting the dead `saved_project_snapshot` field.** §1.5.
10. **Multi-select in the shared modal.** New's atlas list is built by repeated
    single choices through `Add…`.
11. **Any agent or MCP surface for New / Open / Save As.** §7.

## 11. Risks and known limitations

| # | Risk | Mitigation / status |
|---|---|---|
| R1 | The two frame bodies (`shell_main.cpp:610-612`, `shell_smoke_frames.cpp:124-127`) are duplicates; editing one leaves New/Open untested | Plan makes both edits one task with a two-site checklist; C4 and C7 fail outright if the smoke's site is missed |
| R2 | The menu-popup window name in C9 is unmeasured | Explicit measure-or-report instruction in §9 C9; no fallback that silently drops coverage |
| R3 | §4.5's extraction touches the most heavily depended-on shell function in the tree | Pure refactor, done first and alone, gated on a zero-line diff across every test file |
| R4 | A New over a dirty session discards work silently | **Real and accepted.** §6 property 4; MAR-182 closes it. A partial check here is rework |
| R5 | New writes `active_animation: "idle"` for a rig with no `idle` clip | Behaviourally inert by three measured links (§4.1); visible only as one metadata line in the Project panel on an unsaved project |
| R6 | The bespoke browser is worse than the OS dialog for real filesystems | §3.1, with the absolute-path escape hatch in §3.4 rule 2 |
| R7 | The rename test seam is process-global and shared with the settings writer | Every case that installs it uses the RAII `ScopedRenameCallback` and does no preference save inside the scope; the existing C3 already does this |
| R8 | `directory_iterator` on an unreadable directory | Every filesystem call uses the `error_code` overload; §3.2 renders the error rather than an empty list |

## 12. Story-premise corrections

Every story in this arc has found errors in its own governing documents. MAR-181's:

1. **"Connect New, Open, Save, and Save As to … modals"** implies Save needs a
   modal. It does not, and giving it one would be wrong: Save has a destination by
   construction. Only Save As, Open and New raise a modal. §4.3.
2. **The description says "the atomic `EditorSession` lifecycle"**, which reads as
   though `close` is in play. It is not — no acceptance criterion mentions closing
   a project, and `File > Close` is a non-goal (§10.5). MAR-181 uses `create`,
   `open` and `save`; `close` stays unused.
3. **"A native ImGui directory/path modal"** is a single artefact in the story's
   language, but New needs three paths and a list. This design ships **two**
   modals: the shared path chooser (§3.2) and a small New form (§4.1) that raises
   it three ways. One modal cannot express "one skeleton, N atlases, one target"
   without becoming a mode-switch soup.
4. **The criteria never mention keyboard shortcuts**, yet a Save with no `Ctrl+S`
   is not a "core File path workflow" in any editor. `Ctrl+S` is added (§4.3) and
   is the only shortcut added — flagged here as a deliberate, small widening rather
   than left unremarked.
5. **`docs/root1/editing-gap-analysis.md:207`** lists "File 메뉴: New/Open/Save/Save
   As/Recent Projects" as one row and marks it 🟡 "Save가 툴바에만 있음". That is
   accurate for Save and understates the rest: New, Open and Save As are not
   partially present, they are **entirely absent** (§1.1). The row should read
   🔴 for those three.
