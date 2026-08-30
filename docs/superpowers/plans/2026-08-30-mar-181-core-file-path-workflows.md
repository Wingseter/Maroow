# MAR-181 Add Core File Path Workflows — Implementation Plan

Design: `docs/superpowers/specs/2026-08-30-mar-181-core-file-path-workflows-design.md`

Read the design first. This plan does not restate it; every task cites the section
it implements.

## Global constraints

1. **A passing `save()` proves nothing.** `validate_project_for_save`
   (`project.cpp:5503`) takes no base document and cannot resolve a
   cross-reference. Every assertion that a saved or opened project is *good* goes
   through `marrow::editor::load_project(path)`, which materializes the references
   and reaches `build_project_runtime`. **`marrow::runtime::json::load_document` is
   not an acceptable substitute** — it is precisely what hid the pre-MAR-180 Save
   As bug at `editor_project_smoke.cpp:1531`.
2. **Assert the artifact, not the return code.** For every failure case, assert the
   file's bytes on disk, `state.project_path`, and a six-value session snapshot
   (`serialize_project`, the three revisions, `undo_count`, `dirty()`).
3. **State the inversion mechanism.** Each TDD step below names why its inversion
   is observable at that layer. If an inversion does not bite when Task 11 runs it,
   **strengthen the test, do not weaken the gate**, and record it.
4. **No count literal moves.** The registry stays at 64. Task 10 proves it by diff.
5. **`~/Library/Application Support/Marrow` must not come into existence.** Every
   shell run goes through `ScopedPreferenceIsolation`
   (`shell_preferences.hpp:50`), which the smoke installs as its first statement
   (`shell_smoke.cpp:49`). Never run `marrow_editor_shell` without `--auto-close`.
6. **The rename seam is process-global** (`atomic_file_write.hpp`, shared with the
   settings writer). Anything that installs it uses the RAII `ScopedRenameCallback`
   (`shell_smoke_project.cpp:713`) and performs no preference save inside the scope.
7. **Do not implement any dirty-session check.** MAR-182 owns it (design §6). A
   partial one here is rework MAR-182 must undo.
8. **Do not call `EditorSession::close`.** Design §10.5.
9. `src/editor/shell_smoke_project.cpp` and `src/samples/editor_project_smoke.cpp`
   may be edited concurrently by other work. **Append** new scenario functions near
   the end of the file and register them from the existing composition point; do
   not edit or reformat existing cases.

## File and responsibility map

| File | Change |
|---|---|
| `src/editor/shell_file_paths.hpp` | **new** — `FileAction`, `FilePathMode`, `FilePathRequest`, `PendingFileApplication`, `resolve_choice`, `begin_file_action`, `apply_pending_file_action`, `draw_file_path_modals` |
| `src/editor/shell_file_paths.cpp` | **new** — the two modals, the browser, the four actions |
| `CMakeLists.txt` | one line: `src/editor/shell_file_paths.cpp` in the `marrow_editor_shell` source list (`:875-912`) |
| `src/editor/shell_state.hpp` | two fields: `std::optional<FilePathRequest> file_path_request;`, `std::optional<PendingFileApplication> pending_file_application;`; declare `adopt_session_project_into_shell` |
| `src/editor/shell_core.cpp` | extract `shell_core.cpp:566-643` into `adopt_session_project_into_shell`; `reload_project` (`:544`) calls it |
| `src/editor/shell_project_panels.cpp` | four `MenuItem`s in the File menu (`:713-724`); `draw_file_path_modals(state)` at the `:702` early return and after `:900` |
| `src/editor/shell_preview.cpp` | `Ctrl+S` in `handle_project_history_shortcuts` (`:205`) |
| `src/editor/shell_main.cpp` | `apply_pending_file_action(shell_state)` after `:610-612` |
| `src/editor/shell_smoke_frames.cpp` | `apply_pending_file_action(&shell_state)` after `:124-127` |
| `src/editor/shell_smoke_scenarios.hpp` | declare the seven new validators |
| `src/editor/shell_smoke_project.cpp` | cases C4–C10; register them in `validate_shell_foundation_smoke` (`:1446`) beside `:1698`/`:1701` |
| `AGENTS.md` | verification lines + results |

**Not touched — a zero-line diff on each is a gate:** `src/editor/agent_dispatch.cpp`,
every `src/editor/agent_handlers_*.cpp`, `src/samples/agent_dispatch_smoke.cpp`,
`tools/**`, `src/editor/project.cpp`, `src/editor/session.cpp`,
`include/marrow/editor/**`, `src/editor/atomic_file_write.*`,
`src/editor/preferences.cpp`, `src/editor/preferences_internal.hpp`.

---

## Task 0 — Measure the as-built tree (mandatory, no code)

Every number and every line reference the design asserts is re-measured here.
**If any measurement disagrees, stop and reconcile before writing a line of code.**

```sh
# ---- 1. Registry. Design §7 claims 64, split edit 39 / inspection 12 /
#         management 10 / validation 3.
awk '/^constexpr OperationSpec kOperationSpecs\[\] = \{/{f=1} f&&/^};/{f=0} f' \
  src/editor/agent_dispatch.cpp | grep -c '^\s*{"'
#   expect: 64
awk '/^constexpr OperationSpec kOperationSpecs\[\] = \{/{f=1} f&&/^};/{f=0} f' \
  src/editor/agent_dispatch.cpp | grep -oE '"(edit|inspection|management|validation)"' \
  | sort | uniq -c
#   expect: 39 edit / 12 inspection / 10 management / 3 validation
#   If not, §7's "registry unchanged" proof and Task 10 are both invalid.

# ---- 2. Count sites. Design §7 claims 10 guards + 10 messages + 1 array + 2 python.
grep -rn "operation_count_before != 64U" src/
#   expect EXACTLY 10, at: shell_smoke_constraints.cpp:147,676;
#          shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603;
#          shell_smoke_timeline.cpp:3697   (+ a message one line below each)
grep -n "OperationExpectation, 64" src/samples/agent_dispatch_smoke.cpp   # expect :39
grep -n "== 64" tools/mcp/test_client.py                                  # expect :53 and :55 only
#   Record the measured list verbatim; Task 10 re-runs this and diffs.
#   NONE of these move in MAR-181.

# ---- 3. The File menu gap (design §1.1). Confirm before building anything.
sed -n '701,726p' src/editor/shell_project_panels.cpp
#   expect: BeginMenu("File") at :713, "Reload Project" at :714-720,
#           "Quit" at :721-723, EndMenu at :724 -- and NOTHING ELSE.
#   If New/Open/Save/Save As already exist, §1.1 is wrong -- stop.
grep -rn "EditorSession::create\|session.create(\|\.create(" src/editor/ | grep -v session.cpp
#   expect: NO HITS. MAR-180 built `create` and nothing calls it yet.

# ---- 4. No file dialog anywhere (design §1.2). This shapes the whole plan.
grep -rniE "nfd|tinyfiledialog|portable-file-dialogs|IGFD|ImGuiFileDialog|FileDialog|OpenFileName|NSOpenPanel|GtkFileChooser|file_dialog" \
  src include tools CMakeLists.txt THIRD_PARTY.md AGENTS.md docs/root1
#   expect EXACTLY ONE hit, and it is prose:
#     docs/root1/research-windowing-glfw-vs-sdl3.md:27
#   Any code hit means a dialog exists and §3.1 must be re-argued.

# ---- 5. The path divergence (design §1.3).
grep -n "state->project_path" src/editor/shell_core.cpp
#   expect :558, :561, :656, :668 -- reload opens it, save writes it.
grep -n "project.source_path" src/editor/agent_handlers_management.cpp
#   expect :76 -- the AGENT save uses the PROJECT's path, not the shell's.

# ---- 6. reload_project's hardcoded clean (design §1.4) and the extraction range.
sed -n '588,600p' src/editor/shell_core.cpp
#   expect :592 `state->project_dirty = false;`
#          :593 `state->saved_project_snapshot.clear();`
sed -n '620,626p' src/editor/shell_core.cpp
#   expect :622 `if (reload_current_project) {` -- the second substitution site.

# ---- 7. saved_project_snapshot is write-only (design §1.5).
grep -rn "saved_project_snapshot" src/
#   expect EXACTLY 4 lines: shell_state.hpp:853 (decl) and
#   shell_core.cpp:593, :599, :663 (writes). ZERO reads.
#   If a read exists, §1.5 is wrong and §4.5's extraction must preserve it.

# ---- 8. The primitives MAR-181 consumes (design §1.6). All must exist.
grep -n "ProjectLoadResult create(\|bool close();\|ProjectSaveResult save(" \
  include/marrow/editor/session.hpp                      # expect :167, :177, :213
grep -n "^ProjectData rebase_project_paths" src/editor/project.cpp        # expect :7339
grep -n "load_skeleton_data(const std::filesystem::path" \
  include/marrow/runtime/skeleton.hpp                    # expect :1787
grep -n "animations() const\|skins() const\|find_skin_index" \
  include/marrow/runtime/skeleton.hpp                    # expect :806, :809, :860

# ---- 9. The normalize-state links §4.1 depends on. Verify all three.
sed -n '754,758p' src/editor/session.cpp
#   expect the animation_name fallback to data.animations().front().name
sed -n '5622,5627p' src/editor/project.cpp
#   expect preview-skin NON-EMPTINESS only -- no cross-reference check
sed -n '608,620p' src/editor/shell_core.cpp
#   expect the active_animation-then-first-animation pick
#   If any differs, §4.1's "the defaults are safe" argument is wrong -- stop.

# ---- 10. The two frame bodies (design §4.6 / risk R1). BOTH must be edited.
grep -n "reload_project(shell_state)" src/editor/shell_main.cpp           # expect :611
grep -n "reload_project(&shell_state)" src/editor/shell_smoke_frames.cpp  # expect :124
grep -n "draw_menu_bar" src/editor/shell_main.cpp src/editor/shell_smoke_frames.cpp
#   expect shell_main.cpp:558 and shell_smoke_frames.cpp:64 -- the ONE call site
#   that makes draw_file_path_modals reachable from both, per §4.7.

# ---- 11. The modal harness this story reuses (design §1.6 / §9 C9).
sed -n '686,700p' src/editor/shell_smoke_constraints.cpp   # render_frame
sed -n '1019,1032p' src/editor/shell_smoke_constraints.cpp # modal_is_open by full title
grep -n "class ScopedRenameCallback" src/editor/shell_smoke_project.cpp   # expect :713
grep -n "^bool seed_shell_project_copy" src/editor/shell_smoke_project.cpp # expect :751
grep -n "validate_mar180_failed" src/editor/shell_smoke_project.cpp       # expect :838, :943
grep -n "validate_mar180_failed" src/editor/shell_smoke_scenarios.hpp     # registration point

# ---- 12. Baselines to beat. Record all four.
grep -c "" src/editor/shell_project_panels.cpp   # expect 1110
grep -c "" src/editor/shell_core.cpp             # expect 714
grep -c "" src/editor/shell_smoke_project.cpp    # expect 1707
grep -c "" src/editor/shell_state.hpp            # expect 1157
```

**Deliverable:** the measured values written into the task results. Any
disagreement with the design is reconciled *in the design* before Task 1 begins.

---

## Task 1 — Extract the shell resync (pure refactor, zero behaviour change)

Implements design §4.5.

### TDD

There is no new test. **The gate is that the entire existing suite passes with a
zero-line diff in every test file.** This is stronger than a new test would be: the
extraction's whole claim is "nothing changed", and the ~40 existing shell smokes
that call `reload_project` are a far better witness than anything written for it.

### Implementation

1. Declare in `shell_state.hpp`, beside `save_project_file`/`export_runtime_assets_file`
   (`:1003-1004`):
   ```cpp
   void adopt_session_project_into_shell(
       ShellState* state,
       const std::string& previous_animation_name,
       double previous_timeline_time,
       bool previous_timeline_loop,
       bool previous_timeline_playing,
       bool restore_transient_playback,
       bool project_is_clean);
   ```
2. Move `shell_core.cpp:566-643` into it **verbatim**, with exactly two edits:
   - `:592` `state->project_dirty = false;` → `state->project_dirty = !project_is_clean;`
   - `:622` `if (reload_current_project) {` → `if (restore_transient_playback) {`
   Leave `:593` and `:599`'s `saved_project_snapshot` writes exactly as they are
   (Task 0 step 7 proved the field is dead; changing dead state is churn).
3. `reload_project` keeps `:544-565` and ends with:
   ```cpp
   adopt_session_project_into_shell(
       state, previous_animation_name, previous_timeline_time,
       previous_timeline_loop, previous_timeline_playing,
       /*restore_transient_playback=*/reload_current_project,
       /*project_is_clean=*/true);
   return true;
   ```

### Verify

```sh
cmake --build build
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
ctest --test-dir build --output-on-failure -L editor
git diff --stat -- src/editor/shell_smoke_*.cpp src/samples/*.cpp src/tests/
#   MUST be empty. A non-empty diff means the refactor changed behaviour.
```

---

## Task 2 — The path modal primitive

Implements design §3.

### TDD — `marrow_editor_shell`, case C8

New file `src/editor/shell_smoke_project.cpp` scenario
`validate_mar181_path_resolution_smoke()`, table-driven against `resolve_choice`.
Eleven rows, exactly design §9 C8's list, each asserting **both** the returned path
and whether a diagnostic was produced:

| Input | Mode | Expected |
|---|---|---|
| `""` | either | diagnostic `"Enter a file name."` |
| `"   "` | either | same |
| `"x"` | `SaveTarget` `.marrow` | path `dir/x.marrow`, no diagnostic |
| `"x.mskl"` | `SaveTarget` `.marrow` | diagnostic naming the expected extension |
| `"/abs/y.marrow"` | `SaveTarget` | path `/abs/y.marrow` verbatim |
| `"../sib/y.marrow"` | `SaveTarget` | path = `(dir/../sib/y.marrow).lexically_normal()` |
| missing file | `OpenExisting` | diagnostic `"That file does not exist."` |
| an existing directory | `OpenExisting` | diagnostic `"That is a directory."` |
| parent missing | `SaveTarget` | diagnostic `"The destination folder does not exist."` |
| an existing directory | `SaveTarget` | diagnostic `"That is a directory."` |
| **an existing file** | `SaveTarget` | **accepted**, informational diagnostic |

Must fail first: the function does not exist, so the smoke does not compile.

*Inversion observability:* each row exercises exactly one branch of
`resolve_choice`; deleting any single branch flips exactly one row and leaves the
other ten green. The final row is the one an over-eager "safety" edit would break,
so it asserts **acceptance** — a `SaveTarget` over an existing file is a legitimate
overwrite that MAR-180 made atomic.

### Implementation

1. `src/editor/shell_file_paths.hpp` — the enums, `FilePathRequest`,
   `PendingFileApplication`, and free-function declarations from design §3.2/§3.3/§4.
2. `src/editor/shell_file_paths.cpp` — `resolve_choice` per §3.4 (steps 1–5, in
   order), and `draw_file_path_modals(ShellState*)` per §3.2:
   - if `state->file_path_request` has a value and `!opened`, call
     `ImGui::OpenPopup(kFilePathModal)` and set `opened = true`;
   - `BeginPopupModal(kFilePathModal, nullptr, ImGuiWindowFlags_AlwaysAutoResize)`;
   - the directory line, the `..` row, sorted directory rows, sorted filtered file
     rows, the `Name` `InputText` with `EnterReturnsTrue`, the diagnostic line, and
     `Choose` / `Cancel`;
   - **every** filesystem call uses the `std::error_code` overload; an unreadable
     directory renders one line of error text in place of the list.
3. `ShellState` gains the two `std::optional` fields.
4. `CMakeLists.txt`: add `src/editor/shell_file_paths.cpp` to `marrow_editor_shell`.
5. `shell_smoke_scenarios.hpp`: declare the new validator; register it in
   `validate_shell_foundation_smoke`.

Nothing calls `draw_file_path_modals` yet — that is Task 6.

### Verify

```sh
cmake --build build
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

---

## Task 3 — Open

Implements design §4.2 and the O-rows of §5.

### TDD — `marrow_editor_shell`, case C7

`validate_mar181_failed_open_preserves_shell()`:

1. `seed_shell_project_copy` into `…/marrow_mar181_c7`, load it into a `ShellState`.
2. Record `state.project_path`, the six-value session snapshot, and the
   `preview_skeleton` / `animation_state` pointers.
3. Write `broken.marrow` beside it: **valid JSON**, produced by copying the seeded
   `.marrow` and rewriting `$.runtime.skeleton` to `does_not_exist.mskl`.
4. Run the Open action on `broken.marrow`.
5. Assert failure; `error_message` non-empty; `status_message == "Project load failed"`;
   **`state.project_path` unchanged**; the six-value snapshot unchanged; both
   pointers identical.
6. Open the good project; assert success, `project_path` updated,
   `session.dirty()` false, and `load_result.skeleton_data != nullptr`.

Must fail first: the Open action does not exist.

*Inversion observability:* "valid JSON, unresolvable reference" is exactly the
shape `json::load_document` accepts and `load_project` rejects, so step 5 fails
only if the shell adopts a project it could not materialize. The decisive
inversion is moving `state->project_path = chosen` **above** the `session.open`
call: the path assertion fails **alone**, because `open` is already atomic and
every session assertion still passes. That separation is the point — C7 tests the
shell's bookkeeping, not the session's atomicity, which MAR-180 already proved.

### Implementation

In `shell_file_paths.cpp`:

- `begin_file_action(state, FileAction::Open)` → seed a `FilePathRequest`
  (`OpenExisting`, `".marrow"`, caption `"Open Project"`, directory =
  `state->project_path.parent_path()` absolutized, name = empty).
- The modal's `Choose` → `state->pending_file_application = {FileAction::Open, path}`,
  clear `file_path_request`, `ImGui::CloseCurrentPopup()`.
- `apply_pending_file_action` handles `Open` per §4.2: `session.open(chosen)`;
  on failure set the two messages and **return false without touching
  `project_path`**; on success set `project_path`, then
  `adopt_session_project_into_shell(..., /*restore_transient_playback=*/false,
  /*project_is_clean=*/true)`, set the status message, return true.

### Verify

```sh
cmake --build build
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
ls ~/Library/Application\ Support/ 2>/dev/null | grep -i marrow    # MUST print nothing
```

---

## Task 4 — Save As, plus Save and `Ctrl+S`

Implements design §4.3, §4.4 and the S-/A-rows of §5.

### TDD — `marrow_editor_shell`, cases C5, C6 and C10

**C5 — `validate_mar181_save_as_moves_the_shell_path()`** — design §9 C5.
Seed into `A`, load, record `current_runtime_asset_paths(state)`. Save As to
`B/moved.marrow` (`B` a sibling of `A`, created by the test).
Assert: success; `state.project_path == B/moved.marrow`;
`state.session.project()->source_path == B/moved.marrow`;
`state.project_dirty == false`; **`load_project(B/moved.marrow)` succeeds** with
non-null `skeleton_data`; its `resolved_skeleton_path()` equals the original
project's; every `resolved_atlas_paths()` entry likewise;
`current_runtime_asset_paths(state)` **element-wise equal** to the recording; and
`A`'s original `.marrow` still exists and still opens.

*Inversion observability:* the `project_path` equality is design §1.3's divergence
and nothing else in the suite checks it — omit `state->project_path = chosen` and
that one line fails while every MAR-180 model case stays green. Removing
`rebase_project_paths` from `save_project` fails **both** the watch-list comparison
and the `load_project` reload, whereas a `json::load_document` assertion in the
same place would pass. **Run that inversion and confirm the `load_document` form
passes under it**, and record the result — that is the proof the reload is the
load-bearing assertion.

**C6 — `validate_mar181_failed_save_as_preserves_shell_path()`** — design §9 C6.
Seed into `A`, load, dirty it with one committed transaction. Create
`B/target.marrow` by copying the seeded project; record its bytes and
`state.project_path`. Inside a `ScopedRenameCallback` returning
`std::errc::permission_denied`, run Save As to `B/target.marrow`.
Assert: failure; `state.project_path` still `A`'s; `session.project()->source_path`
still `A`'s; `project_dirty` and `session.dirty()` both still true;
`B/target.marrow` bytes identical and it still **opens**; `A`'s project still
opens. Release the seam, retry, assert success and a `load_project` of the new file.

*Inversion observability:* the failure is injected at the process-global rename
seam — the only thing between a fully written temporary and the destination — so
it is real, not simulated. Moving `state->project_path = chosen` above the
`session.save` call fails the path assertion while every byte assertion still
passes, isolating exactly the §1.3 defect. The `ScopedRenameCallback` must be RAII
and no preference save may occur inside its scope.

**C10 — `validate_mar181_save_shortcut_smoke()`** — design §9 C10.
Drive real frames. Dirty the project, send `Ctrl` + `S` via `io.AddKeyEvent`,
assert `session.dirty()` becomes false and **`load_project(state.project_path)`
succeeds**. Then raise the path modal, focus its `Name` field so
`io.WantTextInput` is true, dirty the project again, send the same chord, and
assert `session.dirty()` is still true.

*Inversion observability:* placing the `Ctrl+S` handler above
`handle_project_history_shortcuts`' `io.WantTextInput` guard
(`shell_preview.cpp:211-213`) fails the second half while the first still passes.

All three must fail first: the Save As action and the shortcut do not exist.

### Implementation

1. `begin_file_action(state, FileAction::SaveAs)` → `FilePathRequest`
   (`SaveTarget`, `".marrow"`, caption `"Save Project As"`, directory =
   `state->project_path.parent_path()` absolutized, name =
   `state->project_path.filename()`).
2. The modal's `Choose` for `SaveAs` applies **immediately, inside the popup**
   (§4.6): `session.save(chosen)`; on failure set `error_message` /
   `status_message = "Project save failed"`, **do not** change `project_path`, and
   **leave the modal open**; on success set `project_path`, call
   `update_project_dirty_state(state)` and `reset_runtime_asset_watch(state)`,
   clear `error_message`, set the status message, clear `file_path_request`, and
   `ImGui::CloseCurrentPopup()`.
3. `begin_file_action(state, FileAction::Save)` → the empty-path guard from §4.3,
   then `save_project_file(state, true)` unchanged.
4. `shell_preview.cpp`, in `handle_project_history_shortcuts` **below** the
   `io.WantTextInput` guard at `:211-213` and beside the other `ImGui::Shortcut`
   calls:
   ```cpp
   if (ImGui::Shortcut(ImGuiMod_Ctrl | ImGuiKey_S, ImGuiInputFlags_RouteGlobal)) {
       begin_file_action(state, FileAction::Save);
       return;
   }
   ```

### Verify

```sh
cmake --build build
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

---

## Task 5 — New

Implements design §4.1 and the N-rows of §5.

### TDD — `marrow_editor_shell`, case C4

`validate_mar181_new_project_writes_nothing()` — design §9 C4.

1. Build a temp directory holding copies of the fixture's `.mskl`, `.matl` and its
   `player_fixture.png`, and **no `.marrow`** (`seed_shell_project_copy`'s asset
   half; do not create the project file).
2. From a loaded `ShellState`, run New with those sources and target
   `new_project.marrow` in that directory.
3. Assert **`std::filesystem::exists(target) == false`**; `session.dirty()` and
   `state.project_dirty` both **true**; `undo_count() == 0`; `redo_count() == 0`;
   `state.project_path == target`; `load_result.skeleton_data != nullptr`;
   `preview_skeleton != nullptr`.
4. `save_project_file(&state, true)`; assert it succeeds, `exists(target)` is now
   true, `dirty()` false, and **`load_project(target)` succeeds** with non-null
   `skeleton_data`.
5. Then the failure half: run New again with a skeleton path that does not exist.
   Assert it fails, `error_message` is non-empty, and `state.project_path`, the
   six-value snapshot, and `exists(target)` are all unchanged from step 4.

Must fail first: the New action does not exist.

*Inversion observability:* `exists(target) == false` is checked against the
filesystem, not a flag, so it can only hold if no code path wrote it — make the
New action call `save_project_file` after `create` and it fails immediately.
A **second, distinct** inversion: pass `project_is_clean = true` into §4.5's
resync and only the `dirty()` assertions fail, every other assertion staying
green. Two inversions failing different assertions is what proves the case covers
two defects.

### Implementation

1. `begin_file_action(state, FileAction::New)` → raise the New form modal
   `"New Project##file_new"` with three empty rows.
2. The form: skeleton row (`Browse…` → shared modal, `OpenExisting`, `.mskl`),
   atlas list with `Add…` / `Remove` (`OpenExisting`, `.matl`), target row
   (`Browse…` → shared modal, `SaveTarget`, `.marrow`). `Create` disabled until all
   three rows pass §3.4's structural rules.
3. `Create` runs the §4.1 validation pass in order — `runtime::load_skeleton_data`,
   then `runtime::AtlasLoader::load` per atlas — reporting the loader's error
   verbatim next to the offending row and **leaving the modal open** on failure.
   It touches the target path in no way.
4. On pass: `state->pending_file_application = {FileAction::New, options}` and
   close the form.
5. `apply_pending_file_action` handles `New` per §4.1: `session.create(options)`;
   on failure set `error_message` from the result's error and
   `status_message = "New project failed"` and change nothing else; on success set
   `project_path` **first**, then
   `adopt_session_project_into_shell(..., /*restore_transient_playback=*/false,
   /*project_is_clean=*/false)`, then the status message.
6. `MinimalProjectOptions` carries `project_path`, `skeleton_path` and
   `atlas_paths` only — §4.1's default-metadata decision.

### Verify

```sh
cmake --build build
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

---

## Task 6 — Menu wiring, both frame bodies, and the MAR-182 seam

Implements design §4.6, §4.7 and §6. **This is the task risk R1 names.**

### TDD — `marrow_editor_shell`, case C9

`validate_mar181_file_menu_mouse_smoke()` — design §9 C9. Reuse MAR-179's harness
shape (`shell_smoke_constraints.cpp:686-830`: `render_frame`, `sweep_for_widgets`,
`click_widget`, `modal_is_open`).

1. Sweep a real mouse across `##MainMenuBar` until `HoveredId ==
   window->GetID("File")`; click.
2. **Measure** the opened menu popup's ImGui window name via
   `ImGui::GetCurrentContext()->OpenPopupStack` and **record it in the results.**
3. Sweep that popup for the ids of `"New Project..."`, `"Open Project..."`,
   `"Save"`, `"Save As..."`, `"Reload Project"`, `"Quit"`. A label never hovered at
   any scanned position is a **failure, never a skip**.
4. Click `"Open Project..."`; assert
   `ImGui::FindWindowByName("Choose Path##file_path")` is non-null and `Active`,
   and `state.file_path_request.has_value()`.
5. Sweep the modal for `"Cancel"`, click it; assert the modal is no longer active,
   `state.file_path_request == std::nullopt`, and the six-value session snapshot is
   unchanged.

Must fail first: the menu items do not exist, so step 3 finds no ids.

*Inversion observability:* a widget that is not emitted cannot own an id equal to
`window->GetID(label)`. This is the one observation C4–C7 cannot make — they call
the shell seams directly and would all pass with every menu item deleted.

*If the menu popup proves undrivable under this harness:* keep step 4–5 (an
ordinary popup window, known drivable from MAR-179 C6) and **report steps 1–3 as
not achieved** in the results. Do not delete them quietly and do not weaken the
gate.

### Implementation

1. `shell_project_panels.cpp`, in the File menu between `:713` and `:724`, in this
   order: `New Project...`, `Open Project...`, separator, `Save`, `Save As...`,
   separator, the existing `Reload Project`, separator, the existing `Quit`. Each
   new item calls **only** `begin_file_action(state, …)`. Enabled state: the four
   are disabled while `authoring_gesture_active(*state)`; `Save` and `Save As` are
   additionally disabled when `state->load_result.project == nullptr`. **No dirty
   check anywhere** (design §6 property 4).
2. `shell_project_panels.cpp:702-704` early return → `draw_file_path_modals(state);
   return ProjectMenuAction::None;`
3. `shell_project_panels.cpp` after `:900`'s `draw_shell_toolbar(...)` →
   `draw_file_path_modals(state);` then `return action;`
4. **Both** frame bodies, and this is the two-site checklist:
   - [ ] `src/editor/shell_main.cpp`, after `:610-612`'s reload block:
     `apply_pending_file_action(shell_state);`
   - [ ] `src/editor/shell_smoke_frames.cpp`, after `:124-127`'s reload block:
     `(void)apply_pending_file_action(&shell_state);`
   Ordering is reload-then-file-action; the two are mutually exclusive in practice
   and `apply_pending_file_action` is a no-op when nothing is pending.
5. `begin_file_action` carries the design §6 doc comment naming MAR-182's insertion
   point verbatim. `apply_pending_file_action` returns `bool`.

### Verify

```sh
cmake --build build
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
grep -n "apply_pending_file_action" src/editor/shell_main.cpp src/editor/shell_smoke_frames.cpp
#   MUST print one hit in EACH file. One hit total means C4 and C7 are testing
#   a code path the shipped shell does not run, or vice versa.
```

---

## Task 7 — Register every case and run the full suite

1. `shell_smoke_scenarios.hpp`: declare all seven validators beside the existing
   `validate_mar180_*` declarations.
2. `shell_smoke_project.cpp`: register them in `validate_shell_foundation_smoke`
   (`:1446`) immediately after the `validate_mar180_*` calls at `:1698`/`:1701`,
   each with its own `if (!…) return false;` and a one-line success `std::cout`
   in the house voice.
3. Every case removes its temp tree with `std::filesystem::remove_all(root, ignored)`
   on the success path, as C2/C3 do.

### Verify — the full sweep

```sh
cmake -S . -B build && cmake --build build
cmake --build build --target marrow_verify_third_party
./build/marrow_unit_tests
./build/marrow_windowing_tests
./build/marrow_pen_input_tests
./build/marrow_preference_tests
./build/marrow_agent_socket_tests
./build/marrow_selection_tests
./build/marrow_viewport_interaction_tests
./build/marrow_timeline_model_tests
./build/marrow_timeline_graph_model_tests
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
./build/marrow_c_smoke
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -L editor
ctest --test-dir build --output-on-failure -L runtime
cmake --build build --target marrow_constraint_warning_check
python3 tools/mcp/test_client.py
ls ~/Library/Application\ Support/ 2>/dev/null | grep -i marrow    # MUST print nothing
```

---

## Task 8 — Compatibility proof

Two checks that the design's §8 claims are true rather than asserted:

```sh
# 1. A project written by this build opens on a project written before it.
#    Save As the fixture into a temp dir, then diff the KEY SET against the fixture.
python3 - <<'PY'
import json, subprocess, tempfile, pathlib
a = json.load(open('assets/fixtures/player_idle.marrow'))
print(sorted(a.keys()))
print(sorted(a['runtime'].keys()), sorted(a['editor'].keys()))
PY
#   Re-run after a Save As produced by the new UI path and diff the key sets.
#   Expect IDENTICAL key sets: MAR-181 adds no schema key.

# 2. Binary and C ABI untouched.
git diff --stat -- include/marrow/c/ src/c/ include/marrow/runtime/
#   MUST be empty.
```

---

## Task 9 — Documentation

`AGENTS.md`, in **Current Validation**, one line beside the MAR-180 entries:

> - Core File path workflows — New/Open/Save/Save As through the dependency-free
>   ImGui path modal, path-resolution rules, failed-Open and failed-Save-As shell
>   preservation, and the mouse-driven File menu (C4–C10):
>   `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`

And in the results section, the measured facts from Task 0 and Task 6 step 2 (the
menu popup's ImGui window name), plus the Task 11 inversion outcomes.

Do **not** edit `AGENTS.md`'s "MAR-175 is the next product milestone" line or any
count-shaped literal in it. `t = 0.62` and the self-referential "62 lines" are
traps (design §7).

---

## Task 10 — Count-sweep proof

MAR-181 adds **no** count literal, so this is a pure no-change proof.

```sh
# 1. The four untouchable trees.
git diff --stat -- src/editor/agent_dispatch.cpp src/editor/agent_handlers_*.cpp \
                   src/samples/agent_dispatch_smoke.cpp tools/
#   MUST be completely empty.

# 2. Re-measure everything Task 0 measured in step 1 and step 2.
#    Expect byte-identical output to the Task 0 recording.

# 3. No count-shaped literal moved anywhere.
git diff -U0 | grep -E '^[+-]' | grep -vE '^(\+\+\+|---)' | grep -E '\b6[0-9]\b'
#   Inspect EVERY hit by hand. MAR-181 should produce none; if one appears it is
#   almost certainly an unrelated coordinate or colour and must be justified.
#   NEVER blind-substitute: (51, 56, 64) and rgb(54,57,64) in the theme,
#   IM_COL32(56, 61, 69, 255), "x": 56.0, 56,995,840,
#   PhysicsBoneState ... 56 bytes/bone, IM_COL32(208,134,57,230),
#   test_client.py:1484 (an ordinal), :1525/:1530 (mesh coordinates)
#   are all off limits.

# 4. The registry is still exactly what Task 0 measured.
python3 tools/mcp/test_client.py     # asserts len == 64 twice, :53 and :55
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

---

## Task 11 — Inversions

Run each, confirm it **fails the named case and only the named case**, revert, and
record the result. An inversion that does not bite means the test is weaker than
the plan claims; **strengthen the test, do not weaken the gate.**

| # | Inversion | Must fail | Must still pass |
|---|---|---|---|
| I1 | New calls `save_project_file` right after `create` | C4 step 3's `exists(target) == false` | C4 step 4, C5, C6, C7 |
| I2 | Pass `project_is_clean = true` for New | C4's two `dirty()` assertions | every other C4 assertion |
| I3 | Delete `state->project_path = chosen` from the Save As success branch | C5's `project_path` equality | all of C5's `load_project` assertions |
| I4 | Move `state->project_path = chosen` above `session.save` in Save As | C6's `project_path` assertion | every byte assertion in C6 |
| I5 | Move `state->project_path = chosen` above `session.open` in Open | C7's `project_path` assertion **alone** | C7's six-value snapshot assertions |
| I6 | Replace C5's `load_project` reload with `json::load_document` **and** delete `rebase_project_paths` from `save_project` | nothing — **it passes** | — this is the *point*: record that the weak assertion is blind to the bug the strong one catches |
| I7 | Delete the `Ctrl+S` handler's position and put it above the `io.WantTextInput` guard | C10's second half | C10's first half |
| I8 | Delete `"Open Project..."` from the File menu | C9 step 3 | C4–C7, C8, C10 — they call the seams directly |
| I9 | Omit the `apply_pending_file_action` call from `shell_smoke_frames.cpp` only | C4 and C7 | the interactive shell would still work — this is why the two-site checklist exists |
| I10 | Reject an existing file in `SaveTarget` mode | C8's final row | C8's other ten rows |

I6 is not a defect-finding inversion; it is the **evidence** that the reload
assertion is load-bearing and the parse assertion is not. Record its output
verbatim.

---

## Full verification checklist

- [ ] Task 0's twelve measurements recorded, and every one agrees with the design
      (or the design was corrected first).
- [ ] `cmake -S . -B build && cmake --build build` — clean, no new warnings.
- [ ] `cmake --build build --target marrow_verify_third_party` — passes. **No new
      dependency** (design §10.2).
- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` — passes,
      including MAR-180 S1–S10 unchanged.
- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
      — passes, including C1–C3 unchanged and C4–C10 new.
- [ ] `ctest --test-dir build --output-on-failure` — passes.
- [ ] `ctest --test-dir build --output-on-failure -L editor` — passes.
- [ ] `ctest --test-dir build --output-on-failure -L runtime` — passes.
- [ ] `./build/marrow_preference_tests` — passes (the rename seam is shared).
- [ ] `python3 tools/mcp/test_client.py` — passes; both `== 64` assertions hold.
- [ ] `cmake --build build --target marrow_constraint_warning_check` — passes.
- [ ] Task 1's gate: `git diff --stat -- src/editor/shell_smoke_*.cpp
      src/samples/*.cpp src/tests/` was **empty** after the extraction alone.
- [ ] Task 6's gate: `apply_pending_file_action` appears **once in each** of
      `shell_main.cpp` and `shell_smoke_frames.cpp`.
- [ ] Task 8: the `.marrow` key set is identical before and after; `git diff --stat`
      on `include/marrow/c/`, `src/c/`, `include/marrow/runtime/` is empty.
- [ ] Task 10: `git diff --stat` on `agent_dispatch.cpp`, `agent_handlers_*.cpp`,
      `agent_dispatch_smoke.cpp` and `tools/` is **empty**; the registry re-measures
      to 64 / 39-12-10-3; the ten guards, one array and two Python assertions are
      byte-identical to Task 0's recording.
- [ ] Task 11: all ten inversions run; each failed the named case and only that
      case; I6's output recorded verbatim.
- [ ] `ls ~/Library/Application\ Support/ | grep -i marrow` prints **nothing**.
- [ ] `AGENTS.md` updated with the verification line, the measured menu-popup
      window name, and the inversion results.
- [ ] No dirty-session check exists anywhere in the diff (design §6 property 4,
      §10.3). `grep -n "dirty()" src/editor/shell_file_paths.cpp` returns only
      assertions/status text, never a gate.
- [ ] `EditorSession::close` has no caller (design §10.5).
