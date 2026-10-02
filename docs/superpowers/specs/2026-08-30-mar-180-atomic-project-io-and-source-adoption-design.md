# MAR-180 Make Project I/O and Source Adoption Atomic — Design

- Story: `MAR-180`, "Make project I/O and source adoption atomic", `dependsOn: ["MAR-179"]`
- Branch: `feat/mar-168`
- Date: 2026-08-30

## 0. Scope of the dependency, and where this arc starts

MAR-180 opens a new arc. MAR-168..179 were timeline, curve, weight, and constraint
authoring stories; MAR-180..183 are project I/O and File-menu workflows.

**The `dependsOn: ["MAR-179"]` edge is sequencing, not coupling.** This was checked,
not assumed: MAR-179 added eleven IK/physics parameter widgets in
`shell_constraints.cpp`, three arguments to `edit_ik_constraint`, and MCP schema
rows. None of that appears anywhere in `project.cpp`'s I/O functions,
`session.cpp`'s lifecycle functions, `shell_core.cpp`'s `reload_project`, or
`shell_asset_watch.cpp`. MAR-180 touches no constraint code, no widget code, and
no MCP schema. The one thing MAR-180 inherits from MAR-179 is a **measurement
discipline** and a tree state: the agent registry standing at 64 operations, and
the count-sweep hazards MAR-179 catalogued.

## 1. The measured gap

Everything in this section was read out of the tree on 2026-08-30 and is cited by
`file:line`. Nothing here is inferred from the story text.

### 1.1 `save_project` destroys the previous file before it knows the new one is writable

`save_project` (`project.cpp:7846`) is ordered:

```
7849  validate_project_for_save(project, &save_error)   // rejects -> file untouched  OK
7856  create_directories(parent_path)                   // fails    -> file untouched  OK
7866  std::ofstream output(path);                       // <-- TRUNCATES THE DESTINATION
7873  output << serialize_project(project);
7874  if (!output) { ... return failure; }
7880  ProjectData saved_project = project;
7881  saved_project.source_path = path;
```

Line `7866` opens the destination with the default `std::ios::out` mode, which
truncates. From that instant until `output` is destroyed and flushed, the user's
`.marrow` file on disk is **shorter than the project it used to hold**. Three
consequences, all reachable:

1. **Disk full / quota / write error.** `save_project` returns a failure, and the
   caller (`EditorSession::save`, `session.cpp:1802-1804`) returns without
   touching session state — so the *session* is fine. But the file on disk is now
   a truncated fragment of JSON. `load_project` on it fails at the JSON parse, so
   the project is **unopenable**. The user's only surviving copy of their work is
   the dirty in-memory session they are about to lose.
2. **Process crash or power loss between `7866` and the flush.** Same outcome, with
   no error reported to anyone.
3. **A failure that is never detected at all.** The `if (!output)` at `7874` runs
   after `operator<<` but *before* `output`'s destructor flushes the stream buffer
   and closes the file. A write error that only surfaces at close — the ordinary
   shape of a full filesystem — sets the stream's badbit inside `~ofstream`, where
   nothing reads it. `save_project` then returns **success** over a truncated
   file, `EditorSession::save` marks the session clean
   (`session.cpp:1827-1828`), and the dirty flag that was the user's last warning
   is gone.

The same close-blindness exists in `write_text_file` (`project.cpp:6349`, checked
at `:6355`) and `write_binary_file` (`project.cpp:6374`).

**This is a solved problem in this codebase.** `preferences.cpp` already does the
correct thing for the settings file: `write_atomically` (`preferences.cpp:356`)
creates a uniquely named temporary in the *destination directory*, writes it,
explicitly checks `fwrite`, `fflush` **and** `fclose` (`:482-505`), and only then
calls `production_rename` (`:330`) — `::rename` on POSIX,
`MoveFileExW(..., MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)` on Windows.
Every failure path calls `cleanup_temporary()` (`:421-424`) and leaves the
destination byte-for-byte unchanged. It even ships a fault-injection seam,
`detail::RenameCallback` / `set_preference_rename_callback_for_testing`
(`preferences_internal.hpp:36-47`), which `preference_store_tests.cpp:768` uses to
prove exactly the property MAR-180 needs.

**The house pattern exists, is tested, and `save_project` does not use it.**

### 1.2 Save As writes a project that cannot be reopened

`EditorSession::save(path)` (`session.cpp:1788`) forwards to `save_project`, which
copies the project verbatim and changes exactly one field:

```
7880  ProjectData saved_project = project;
7881  saved_project.source_path = path;
```

Every path stored in a `.marrow` is **project-relative by design**.
`ProjectData::resolve_path` (`project.cpp:6674`) resolves a relative reference
against `source_path.parent_path()`. The canonical fixture is relative:

```json
"runtime": { "skeleton": "player_idle.mskl", "atlases": ["player_idle.matl"] }
```

So saving `assets/fixtures/player_idle.marrow` to `/tmp/x/session_project.marrow`
writes a file whose `runtime.skeleton` still reads `player_idle.mskl` — which now
resolves to `/tmp/x/player_idle.mskl`, a file that does not exist.
`load_project` reaches `load_skeleton_document(project_ptr->resolved_skeleton_path())`
(`project.cpp:7485-7490`) and fails. **The saved project is unopenable.**

Five path families are affected, all serialized from struct fields and therefore
all fixable in one place:

| Struct field | `.marrow` key | Serialized at |
|---|---|---|
| `runtime_assets.skeleton_path` | `$.runtime.skeleton` | `project.cpp:4724` |
| `runtime_assets.atlas_paths[]` | `$.runtime.atlases[]` | `project.cpp:4729` |
| `editor_metadata.export_directory` | `$.editor.export_directory` | `project.cpp:4745` |
| `atlas_pack_definitions[].atlas_path` | `$.atlas_packs[].atlas` | `project.cpp:4501` |
| `atlas_pack_definitions[].sprites[].image_path` | `$.atlas_packs[].sprites[].image` | `project.cpp:4521` |

`$.atlas_packs` is rebuilt wholesale from the struct on every save
(`project.cpp:4897-4900`), and `$.runtime` overwrites its preserved members
(`project.cpp:4722-4731`), so rebasing the struct fields is **necessary and
sufficient** for these five. Section 9 states what it is *not* sufficient for.

There is a second-order failure inside the atlas-pack family.
`find_atlas_pack_definition` matches by resolved path
(`project.cpp:7270-7290`: `resolve_path(definition.atlas_path) == resolved_path`).
If `runtime.atlases` were rebased but `atlas_packs[].atlas` were not, the two
would stop resolving to the same file, the lookup at `project.cpp:7930-7932`
would return `nullptr`, and `export_runtime_assets` would silently fall back from
`export_packed_atlas_asset` to `export_atlas_asset` — a **successful export that
quietly stops packing**. A partial rebase is therefore worse than none.

### 1.3 A passing `save()` proves nothing, and the existing test proves it

`validate_project_for_save` (`project.cpp:5503`) takes no base document. It checks
that `skeleton_path` is non-empty (`:5508`) and that `atlas_paths` is non-empty
(`:5512`) — never that either resolves to a file that exists. A Save As that
breaks every reference passes it.

The existing session smoke does exactly the broken Save As and cannot see it.
`editor_project_smoke.cpp:1530` runs

```cpp
const auto save_result = session.save(temporary.path / "session_project.marrow");
if (!save_result || session.dirty() || ... ||
    !marrow::runtime::json::load_document(save_result.project->source_path)) {
```

against a session opened from `assets/fixtures/player_idle.marrow`. That is a
cross-directory Save As. The assertion is `json::load_document` — the **raw JSON
parser**. The written file is valid JSON, so it passes. Nothing in the suite ever
calls `load_project` on it, and `load_project` is the only function that
materializes the references (`project.cpp:7485-7510`) and calls
`load_skeleton_data` via `build_project_runtime` (`project.cpp:7495`).

This is the exact trap the standing lessons name: an operation returns `ok` while
the artifact it produced is unusable. **Every MAR-180 assertion about a saved
project being good must go through `load_project`, not `json::load_document` and
not the `ProjectSaveResult`.**

### 1.4 Save As leaves history entries that undo into an unopenable project

`EditorSession::save` already knows Save As needs history maintenance
(`session.cpp:1805-1814`): when `source_changed`, it walks `undo_entries` and
`redo_entries` and rewrites `entry.before.project.source_path` and
`entry.after.project.source_path`. That is correct as far as it goes, and it is
only correct **today** because `source_path` is not serialized — `build_project_value`
(`project.cpp:4717`) never emits it — so the snapshots' cached
`serialized_project` strings stay valid.

The moment §1.2's fix rebases `runtime_assets.*` and `atlas_pack_definitions[].*`,
that stops holding. Those fields **are** serialized. A history entry rebased only
in `source_path` would carry the *new* directory with the *old* relative
references, so:

- `undo()` restores `entry.before.project` (`session.cpp:2280`) into the live
  session, producing an in-memory project that cannot be saved-and-reopened; and
- the entry's cached `serialized_project` no longer matches what
  `serialize_project` would now produce, so `histories_equal`
  (`session.cpp:1176-1181`) and `update_dirty` compare against a stale string and
  the dirty flag becomes unreliable.

**The history rebase must rebase the same five path families and re-serialize both
snapshots of every entry.** This is the highest-risk part of the change and gets
its own test (§10.1 S4).

### 1.5 Runtime-source adoption mutates the session before it validates

There are three adoption paths — three places that replace the runtime data a live
session is previewing.

**`EditorSession::open` (`session.cpp:1691`) is already atomic.** It calls
`load_project` into a local (`:1702`), binds a new `PreviewController` into a
local (`:1712`), and only then assigns `impl_->load`, `impl_->preview`, history,
dirty and revisions (`:1719-1728`). Every failure returns before any assignment.

**`EditorSession::reload` (`session.cpp:1730`) is already atomic**, by the same
shape (`:1744` local load, `:1758` local bind, `:1772-1781` commit).

**`shell::reload_runtime_source_assets` (`shell_asset_watch.cpp:128`) is not.**
It loads the document and every atlas into locals correctly (`:133-153`), and then:

```
157-160  state->load_result.base_skeleton_document = <new>;   // MUTATES THE SESSION
         state->load_result.atlas_data = <new>;
162      if (!rebuild_project_runtime(state)) { <hand-rolled restore>; return false; }
172      if (!apply_current_animation_state_to_preview(state)) {
176          (void)rebuild_project_runtime(state);            // RESULT DISCARDED
         }
```

`ShellState::load_result` is a **reference to the session's own
`ProjectLoadResult`** (`shell_state.hpp:788`, `:807`, via
`EditorSessionShellBinding::load_result`). So `:157-160` writes directly into
`EditorSession::Impl::load`, before anything has validated that the new sources
can build.

Two defects follow:

- **The discarded rebuild at `:176`.** Reaching `:172` means the *first* rebuild
  succeeded, so `impl_->load.skeleton_data` and `impl_->preview` already hold data
  derived from the **new** document. The rollback restores the **old** document
  and re-runs `rebuild_project_runtime` to bring `skeleton_data` back in line — but
  ignores whether that succeeded. If it fails, the session is left with
  `base_skeleton_document` = old and `skeleton_data` = built-from-new: an
  **incoherent session**, in which `commit()`'s `build_project_runtime(*load.project,
  *load.base_skeleton_document)` (`session.cpp:1471-1473`) validates against one
  document while the preview shows another.
- **The rollback is hand-rolled and duplicated.** Two nearly-identical restore
  blocks (`:163-168`, `:173-181`) that must stay in sync with every future field
  added to `ProjectLoadResult`. `open` and `reload` need no such block because they
  never mutate early. That asymmetry is the actual bug.

`EditTransaction::commit` (`session.cpp:1416`, rebuild at `:1471`) is a fourth
path, and it is **already handled**: on a failed rebuild it restores
`*load.project` and the preview and resets the transaction (`:1477-1489`), and
`restore_active_transaction` (`:1275`) covers the earlier gates. MAR-180 does not
touch it. It is the reference for what a rollback ought to look like, not a target.

### 1.6 `EditorSession::create` and `EditorSession::close` do not exist

Measured, not assumed: `include/marrow/editor/session.hpp` declares `open` (`:161`),
`reload` (`:166`), `save` (`:171`) and `export_runtime` (`:175`). There is no
`create`, no `close`, and no `new_project`. `grep -n "create\|close"` over the
header returns only `:161`/`:166` matches inside doc comments and one unrelated
`creates` in a comment at `:368`.

The building block for `create` already exists: `create_minimal_project`
(`project.cpp:7308`) takes `MinimalProjectOptions` (`project.hpp:922-931`) — a
project path, an existing skeleton path, atlas paths, and metadata — and produces a
`ProjectData` with the asset paths already made project-relative through
`make_project_relative_path` (`:7312`, `:7316`). It is used today by the hot-reload
shell smoke (`shell_smoke_project.cpp:308`).

The story's "without inventing native rig or topology authoring" is a real
constraint and the tree already honours it: the agent registry has no bone,
slot, or skin *creation* operation (§8), and every project references an external
`.mskl`. `create` adopts an existing rig; it never authors one.

### 1.7 What is already correct, and is therefore not in scope

Stated plainly so the plan does not re-solve solved problems:

- `EditorSession::open` and `EditorSession::reload` are already all-or-nothing (§1.5).
- `EditTransaction::commit` already rolls back a failed runtime build (§1.5).
- `validate_project_for_save` already runs **before** any file is touched
  (`project.cpp:7849`), so a validation failure already preserves the file.
- `reload_runtime_source_assets` already loads *every* source into locals before
  mutating (`:133-153`), so a bad atlas cannot leave a half-swapped atlas list.
  The window is narrower than "reads and writes interleaved"; it is precisely
  "validated document swapped in before the runtime built from it was validated".
- `load_project` already rejects an empty `$.runtime.atlases` on disk
  (`project.cpp:291-297`). A *transaction* can still empty `atlas_paths` in memory
  and commit — reachability in memory is not reachability on disk — but MAR-180
  does not change that surface either way.

## 2. Goal

Make the four operations that replace a project or its runtime sources
all-or-nothing, so that no failure and no crash can leave either the file on disk
or the live session in a state that cannot be opened:

1. **Save / Save As** write through a temporary file and an atomic rename.
2. **Save As** rebases every relative reference so the written project opens.
3. **Runtime-source adoption** validates and builds completely before it swaps.
4. **Session lifecycle** gains `create` and `close` with the same all-or-nothing
   contract as `open`.

## 3. What "atomic" means here, concretely

"Atomic" is used in this document in exactly one sense: **an observer of the
destination sees either the complete previous state or the complete new state,
never a partial or mixed one.** There are two destinations, and they need
different mechanisms.

### 3.1 On disk — temp file plus rename

**Adopted, and it is `preferences.cpp`'s pattern verbatim.** Reasons:

- It is the only mechanism that survives the failure modes that matter here.
  `rename(2)` on POSIX and `MoveFileExW` with `MOVEFILE_REPLACE_EXISTING` on
  Windows replace the directory entry as a single operation; a crash lands on one
  side or the other.
- The temporary is created **in the destination directory**, so the rename never
  crosses a filesystem boundary (where `rename` returns `EXDEV` and the atomicity
  guarantee is gone).
- The codebase already ships it, already ships its fault-injection seam, and
  already ships a passing test for it. Reimplementing a second variant would
  double the platform-specific code and give MAR-180 a *less* tested primitive
  than the one already in the tree.

**Therefore: extract, do not copy.** `write_atomically` moves from
`preferences.cpp` into a private `src/editor/atomic_file_write.{hpp,cpp}` in
namespace `marrow::editor::detail`, with `RenameCallback` and
`set_preference_rename_callback_for_testing` moving with it.
`preferences_internal.hpp` includes the new header so
`preference_store_tests.cpp` compiles **unchanged** and keeps proving the pattern.
`save_project` becomes its second caller.

### 3.2 What temp-plus-rename does and does not guarantee

Stated explicitly, because overclaiming here is how an "atomic save" story ships a
false promise:

| Guaranteed | Not guaranteed |
|---|---|
| The destination is never partially written | The temporary is never orphaned. A crash between `mkstemp` and `rename` leaves one `*.tmp.*` file beside the project. Every *handled* failure removes it (`cleanup_temporary`); a crash cannot. |
| Every write, flush and close error is detected and reported | Durability across power loss. The POSIX branch does `fflush` + `fclose` with no `fsync` on the file and no `fsync` on the parent directory. |
| The previous file is byte-for-byte unchanged after any handled failure | Byte-identity across a *successful* same-path save of a project whose stored paths were not already normalized (§4 rebases through `lexically_normal`). |

The missing `fsync` is a **deliberate non-goal**: adding it here would diverge
from the settings writer for no story requirement, and would put a synchronous
disk barrier in the interactive save path. It is recorded so a later story can
take it knowingly. The Windows branch is stronger than the POSIX one today
(`MOVEFILE_WRITE_THROUGH`); that asymmetry is pre-existing and is not changed.

### 3.3 In memory — build fully into locals, then swap

For the session there is no rename. The equivalent discipline is:
**every new object is constructed and validated into a local, and the session's
fields are assigned only after the last thing that can fail has succeeded.**

`open` and `reload` already do this (§1.5). MAR-180 extends the same shape to
`create`, `close`, and source adoption, which removes the need for a rollback path
rather than adding a second one. The test for "atomic" at this layer is therefore
not "does rollback restore the right values" but **"is the session, after a failed
operation, byte-identical to a session that never attempted it"** — compared via
`serialize_project`, `project_revision`, `runtime_revision`, `preview_revision`,
`undo_count`, `redo_count` and `dirty()`.

### 3.4 Failure taxonomy

For every failure point, the required post-state. `P` is the destination
`.marrow`; `S` is the live session.

| # | Failure point | `P` on disk | `S` in memory |
|---|---|---|---|
| **Save / Save As** ||||
| F1 | `validate_project_for_save` rejects | previous bytes; no temp | unchanged, still dirty, history intact |
| F2 | `create_directories` fails | nothing created | unchanged, still dirty |
| F3 | temporary creation fails | previous bytes; no temp | unchanged, still dirty |
| F4 | write / flush / close of temp fails | previous bytes; temp removed | unchanged, still dirty |
| F5 | `rename` fails | previous bytes; temp removed | unchanged, still dirty |
| F6 | crash before `rename` | previous bytes; **one orphan temp may remain** | n/a |
| F7 | crash during `rename` | previous **or** new bytes, never partial | n/a |
| F8 | success | new bytes; `load_project(P)` succeeds | clean, `source_path` = destination, history rebased and re-serialized |
| **Runtime-source adoption** ||||
| F9 | skeleton document load fails | n/a | unchanged; `base_skeleton_document`, `atlas_data`, `skeleton_data`, `preview` all still the old ones |
| F10 | any atlas load fails | n/a | unchanged, as F9 |
| F11 | `build_project_runtime` fails | n/a | unchanged, as F9 — and in particular `base_skeleton_document` and `skeleton_data` stay **mutually derived** |
| F12 | preview bind / playback restore fails | n/a | unchanged, as F9 |
| F13 | success | n/a | all four swapped together; history preserved; dirty unchanged; `runtime_revision` and `preview_revision` bumped |
| **Session lifecycle** ||||
| F14 | `create` with a missing/invalid skeleton or atlas | **nothing written** | previous session, if any, entirely unchanged |
| F15 | `create` succeeds | **nothing written** | new session, **dirty from birth**, empty history |
| F16 | `create` or `close` while a transaction is active | nothing | refused, session unchanged |
| F17 | `close` | nothing | `has_project()` false; history, preview and dirty cleared; all three revisions **bumped** |

F17's revision bump is not cosmetic. The shell drives its refresh off
`observed_project_revision` / `observed_runtime_revision` / `observed_preview_revision`
(`shell_state.hpp:798-800`). Resetting the counters to zero on close would let a
stale observed value compare equal and skip the resync.

## 4. Save As rebasing

### 4.1 The rule

```
rebase(old_project, new_project_path, ref):
    if ref.empty()        -> ref                                  // unchanged
    if ref.is_absolute()  -> ref                                  // unchanged  (AC: "preserving absolute paths")
    absolute = old_project.resolve_path(ref)                      // identity, against the OLD directory
    return make_project_relative_path(new_project_path, absolute)
```

Applied to the five fields in §1.2's table, and to nothing else.

The rule is **identity-preserving**: after a Save As, every reference resolves to
the same absolute file it resolved to before. That is the only reading of "rebase"
that is testable, and it makes Save As a pure relocation of the project *file*
with zero change of meaning for anything it points at.

### 4.2 Reusing `make_project_relative_path`, and what it does

`make_project_relative_path` (`project.cpp:1374`) is already the house rule —
`create_minimal_project` uses it (`:7312`, `:7316`). Its behaviour, read out of
`:1388-1399`:

- computes `std::filesystem::relative(absolute_reference, new_project_directory)`;
- if that result is `..` or begins with `../`, it returns the **absolute** path
  instead (`:1394-1397`);
- otherwise returns the normalized relative path.

So a Save As **into a subdirectory of, or a sibling of, the original** turns
relative references into absolute ones, because the relative form would need
`../`. Same-directory Save As leaves them relative and unchanged. This is correct
(the project opens either way) and non-portable (an absolute path does not travel
with the project directory). It is accepted rather than changed: altering
`make_project_relative_path` would silently change `create_minimal_project`, which
is out of scope, and inventing a second relativization rule would give the
codebase two.

### 4.3 The `export_directory` decision

`editor_metadata.export_directory` defaults to `"exports"` (`project.hpp:555`) and
names an **output** location, not an existing input. Two readings compete:

- **(a) rebase by identity** — Save As to `/tmp/x` turns `"exports"` into the
  absolute `…/assets/fixtures/exports`; exports keep landing where they landed.
- **(b) keep the token** — exports follow the project into `/tmp/x/exports`.

**(a) is adopted**, for three reasons. The acceptance criterion names export paths
in the same breath as asset paths ("rebases every relative asset, export,
atlas-pack, and PSD provenance path"). Uniformity means one rule and one test, and
no field where the reader has to remember an exception. And `atlas_pack_definitions[].atlas_path`
is *also* an output path that must be rebased by identity anyway, because it has
to keep matching `runtime.atlases` (§1.2) — so (b) would need an exception to its
own exception.

The tension is real and is recorded here rather than hidden: a user who Saves As
into a new folder will find exports still going to the old one. Offering the
choice is a UI question, and the UI is MAR-181. Non-goal, stated in §11.

### 4.4 Where the rebase runs

A pure function in `project.cpp`, declared in `project.hpp`:

```cpp
ProjectData rebase_project_paths(
    const ProjectData& project,
    const std::filesystem::path& new_project_path);
```

`save_project` calls it as its **first** step, before `validate_project_for_save`,
and serializes the rebased copy. Consequences:

- Every existing caller — `EditorSession::save`, the agent `save` review path
  (`agent_handlers_management.cpp:69-78`), `save_project_file`
  (`shell_core.cpp:648`), and every smoke — gets correct Save As with no change.
- A same-path save is a **no-op rebase** by construction: the reference resolves to
  the same absolute file, and relativizing it against the same directory returns
  the same relative path.
- `ProjectSaveResult::project` (`project.hpp:902`) already carries the saved
  `ProjectData` back to the caller, so `EditorSession::save` receives the rebased
  project and installs it (`session.cpp:1806`) — the in-memory session is
  automatically consistent with what was written. That existing wiring is why the
  rebase belongs in `save_project` and not in `EditorSession::save`.

### 4.5 History rebasing

`EditorSession::save`'s existing `source_changed` branch (`session.cpp:1807-1814`)
is extended: for each entry in `undo_entries` and `redo_entries`, both the `before`
and `after` snapshot get

1. `snapshot.project = rebase_project_paths(snapshot.project, new_path)`, and
2. `snapshot.serialized_project = serialize_project(snapshot.project)`.

Step 2 is not optional. `HistorySnapshot::serialized_project` (`session.cpp:1126`)
is the value `histories_equal` (`:1176`) and `update_dirty` compare, and the five
rebased fields are all serialized. Skipping it leaves every entry's cached string
describing a project that no longer exists.

`entry.descriptor.allow_merge = false` stays as-is: a save is a history boundary.

## 5. Runtime-source adoption

### 5.1 The primitive

A new session method, mirroring `open`'s shape:

```cpp
/**
 * @brief Reloads the project's runtime sources from disk and swaps them atomically.
 *
 * Loads the skeleton document and every atlas, rebuilds runtime data and rebinds
 * the preview entirely into locals; the session is mutated only after all of them
 * succeed. A failure leaves the session byte-identical, including the invariant
 * that skeleton_data is derived from base_skeleton_document.
 */
SessionResult EditorSession::adopt_runtime_sources();
```

Sequence, with the invariant each step protects:

1. Refuse if `!loaded()` (`NoProject`) or if a transaction is active
   (`TransactionAlreadyActive`). Same gates as `rebuild_runtime_without_history`
   (`session.cpp:2434-2447`).
2. `load_skeleton_document(load.project->resolved_skeleton_path())` into a local.
   Failure → F9.
3. Load every `resolved_atlas_paths()` entry into a local vector. Failure → F10.
   *(This ordering already exists in `shell_asset_watch.cpp:133-153` and is kept.)*
4. `build_project_runtime(*load.project, *local_document)` into a local. Failure →
   F11. **This is the step whose result the current code cannot un-apply**, because
   it runs after the document has already been swapped in.
5. Bind a local `PreviewController` against the local skeleton data, preserving the
   current `PreviewState`, and restore the playback snapshot. Failure → F12.
6. Commit: assign `load.base_skeleton_document`, `load.atlas_data`,
   `load.skeleton_data` and `preview` together; `restore_transient_state`;
   `update_dirty()`; `++runtime_revision`; `++preview_revision`.

`project_revision` is **not** bumped: adoption replaces runtime sources, never the
authored project. `undo_entries` / `redo_entries` are untouched — their snapshots
hold `ProjectData` only (`session.cpp:1124-1127`), which adoption does not change,
so they stay valid across it.

### 5.2 What the shell keeps

`reload_runtime_source_assets` (`shell_asset_watch.cpp:128`) reduces to:

- call `EditorSessionShellBinding::adopt_runtime_sources(state->session)`;
- on failure: set `error_message` from the `SessionError` and
  `status_message = "Runtime asset hot-reload failed"`, restore the two transient
  marquee fields it snapshots, return `false`. **No `load_result` restoration at
  all** — the session never moved.
- on success: keep every line it already has — reset `viewport_ffd_selection`,
  `viewport_ffd_box_selection`, `viewport_box_selection`,
  `reconcile_selection_to_runtime`, `reconcile_hierarchy_anchor_to_runtime`,
  re-fetch `preview_skeleton` / `animation_state`, set the status message.

The presentation-layer rollback the shell does today is correct and stays. The
*model*-layer rollback (`:163-168`, `:173-181`) is deleted, along with the
discarded `(void)rebuild_project_runtime(state)` at `:176`.

The binding gets one line, alongside `rebuild_runtime_without_history`
(`session_shell_binding.hpp:58-61`).

## 6. `create` and `close`

### 6.1 `create`

```cpp
/**
 * @brief Builds a new in-memory project around an existing rig and adopts it.
 *
 * Writes nothing. The session becomes dirty immediately so the caller must save
 * before the project exists on disk. A failure leaves any current session
 * entirely unchanged.
 */
ProjectLoadResult EditorSession::create(const MinimalProjectOptions& options);
```

1. Refuse while a transaction is active — the same gate `open` uses
   (`session.cpp:1692-1699`).
2. `ProjectData project = create_minimal_project(options)` (`project.cpp:7308`).
   This already relativizes the skeleton and atlas paths against
   `options.project_path` (`:7312`, `:7316`), so `create` inherits §4's rule for
   free and needs no rebase of its own.
3. Run **the same materialization `load_project` runs** on the in-memory project:
   `load_skeleton_document(resolved_skeleton_path())`, then
   `build_project_runtime`, then `AtlasLoader::load` per
   `resolved_atlas_paths()` — the sequence at `project.cpp:7485-7510`. Any failure
   returns a `ProjectLoadResult` carrying that error and **touches nothing**
   (F14).
4. Bind a local `PreviewController` from `editor_metadata.active_animation` and
   `preview_skins`, exactly as `open` does (`session.cpp:1705-1716`).
5. Commit as `open` commits (`:1719-1728`), with **one difference**:
   `saved_serialized_project` is set to the empty string and `project_dirty` to
   `true`. `open` sets `saved_serialized_project = serialize_project(...)` and
   `project_dirty = false` because the project it loaded exists on disk; a created
   project does not.

Step 5 is what satisfies MAR-181's "A valid New starts a dirty in-memory
imported-rig project and does not write the target path until the user explicitly
saves". `update_dirty()` compares the current serialization against
`saved_serialized_project`, so the empty baseline can never compare equal and the
session cannot silently look clean.

Step 3 is also where "does not invent native rig authoring" is enforced by
construction: `create` cannot succeed without an existing, loadable `.mskl` and at
least one loadable atlas.

### 6.2 `close`

```cpp
/**
 * @brief Discards the session's project, runtime, preview and history.
 * @return false when an edit transaction is active; the session is then unchanged.
 */
bool EditorSession::close();
```

1. Refuse while a transaction is active (F16).
2. Reset `load` to a default `ProjectLoadResult`; reset `preview` to a
   default-constructed `PreviewController`; clear `undo_entries`, `redo_entries`,
   `saved_serialized_project`; set `project_dirty = false`; reset
   `active_transaction`.
3. `++project_revision; ++runtime_revision; ++preview_revision` — **bump, never
   reset** (F17, and the reason is in §3.4).
4. `next_transaction_id` is **not** reset. Transaction ids must stay unique for the
   lifetime of the session object, or a stale `EditTransaction` handle from before
   the close could match a new transaction's id in `EditTransaction::project()`
   (`session.cpp:2544-2551`) and write into a different project.

After `close`, `loaded()` (`session.cpp:1159-1162`) is false, so every accessor and
every mutator already returns the `NoProject` error or a null pointer. No call site
needs a new guard.

## 7. Schema

**No `.marrow` schema change.** Save As rewrites the *values* of five existing
fields; it adds, removes and renames nothing. `preserved_root` round-tripping is
unaffected. A project written by a MAR-180 build loads on a MAR-179 build and vice
versa. Section 9 records the one place this is not the whole story.

## 8. Registry

**Unchanged at 64.** MAR-180 changes the *behaviour* of the existing `save`
operation (`agent_dispatch.cpp:89` — `management`, mutating, requires review,
handler `handle_management_operation`), not its metadata. It adds no operation:
`create`, `close`, Save As and hot reload are all editor-shell and session
surfaces, and MAR-181/182/183 own the UI that reaches them. No `kOperationSpecs`
row is added, moved or edited; no `types.Tool` is added in
`tools/mcp/tools/editing.py`.

**Measured on 2026-08-30**, and to be re-measured in Task 0:

| Site | Measured |
|---|---|
| `kOperationSpecs` rows (`agent_dispatch.cpp:29-95`) | **64** |
| Split | **edit 39 / inspection 12 / management 10 / validation 3** |
| `operation_count_before != 64U` code guards | **10** — `shell_smoke_constraints.cpp:147,676`; `shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603`; `shell_smoke_timeline.cpp:3697` |
| Guard messages | **10**, one line below each guard |
| `std::array<OperationExpectation, 64>` | **1** — `agent_dispatch_smoke.cpp:39` |
| Python assertions | **2** — `test_client.py:53,55` |

The claim is proved **by diff**, the way MAR-179 proved it: after the change,
`git diff -U0` filtered for count-shaped literals must contain **zero** lines.
A blind substitution is unsafe — `AGENTS.md` carries `t = 0.62` and "62 lines",
the theme carries `(51, 56, 64)` and `rgb(54,57,64)`, and `test_client.py:1525`
/ `:1530` carry mesh coordinates `-64` / `64` while `:1484` is an ordinal.
MAR-180 adds no count literal at all, so the sweep is a pure no-change proof.

## 9. Compatibility, and one honest limitation

- **Files stay loadable both ways.** No schema change (§7).
- **A same-path save is a no-op rebase** (§4.4), so the overwhelming majority of
  saves produce identical output to today. Byte-identity is guaranteed only for
  projects whose stored paths are already normalized; `lexically_normal` inside
  `make_project_relative_path` will collapse a `./a/../b` into `b`. This is a
  one-time normalization, and it is why §10.1 S1's byte-identity assertion is
  written against the *previous* file after a **failed** save, not against a
  successful re-save.
- **`preserved_root` can hide paths that rebasing cannot reach.** Unknown additive
  members are round-tripped opaquely (`project.cpp:4719-4721`). If a future story
  stores a path in one, Save As will not rebase it. This is not hypothetical:
  `docs/root1/editing-gap-analysis.md:430` schedules
  `.marrow.editor.import_sources.psd` — project-relative PSD provenance — for
  **MAR-188**. It is verified absent today: `grep -rn "import_sources" src/ include/ assets/`
  returns nothing, and `psd_path` exists only as a transient
  `PsdImportOptions` field (`psd_import.hpp:31`), never persisted.

  **The story's acceptance criterion names "PSD provenance path" among the things
  Save As must rebase. There is no such path in the schema yet.** MAR-180 cannot
  rebase a field that does not exist. What MAR-180 does instead is make
  `rebase_project_paths` the single, documented place where a new project-relative
  field is registered, and carry a comment naming MAR-188 so the field is added
  there when it lands. This is called out as a story-premise correction, not
  quietly dropped.

## 10. Validation strategy

Two binaries carry the new coverage; two more are regression-only. Every case
states **why its inversion is observable at the layer under test** — a test that
cannot fail is worse than no test.

### 10.1 `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`

`marrow_project_smoke` already has `${PROJECT_SOURCE_DIR}/src/editor` on its
include path (`CMakeLists.txt:815-818`) and already includes a private header
(`editor_project_smoke.cpp:22` includes `mesh_weight_model.hpp`). It can therefore
include `atomic_file_write.hpp` and drive the rename seam **with no CMake change**.

**S1 — a failed save preserves the previous file byte-for-byte.**
Copy the fixture into a temp directory, `load_project` it, mutate
`editor_metadata.notes`, read the destination's bytes, install a
`RenameCallback` returning `std::errc::permission_denied`, call `save_project`.
Assert: the result is a failure carrying the destination path; the destination's
bytes are **identical** to the pre-save read; `load_project` on it still succeeds;
the callback was invoked **exactly once**; the source it was handed lives in the
destination directory and is not the destination; that temporary no longer exists;
and a `directory_iterator` over the temp directory lists **only** the expected
files — no orphan. Modelled directly on `preference_store_tests.cpp:750-799`.
*Observable because:* the injected callback is the only thing between a
fully-written temporary and the destination. Invert by removing the temp-and-rename
(restoring the direct `ofstream`) and the callback is never called, so
`rename_calls == 1` fails **and** the byte comparison fails, because the direct
write truncated the file it was supposed to preserve.

**S2 — Save As to another directory produces a project that opens.**
`EditorSession::open` the fixture; `session.save(temp_dir / "moved.marrow")`.
Assert: the result succeeds; **`load_project(moved.marrow)` succeeds** and its
`skeleton_data` is non-null; the reloaded project's `resolved_skeleton_path()`
equals the fixture's `resolved_skeleton_path()` — the same absolute file;
likewise for every entry of `resolved_atlas_paths()`; and
`resolved_export_skeleton_path()` resolves under the *original* fixture directory
(§4.3's decision, asserted rather than assumed).
*Observable because:* it asserts through `load_project`, which materializes the
references (`project.cpp:7485-7510`) — not through `json::load_document`, which is
what `editor_project_smoke.cpp:1531` uses today and is exactly why the bug is
invisible now. Invert by deleting the `rebase_project_paths` call from
`save_project` and S2's `load_project` fails on a missing `.mskl`, while a
hypothetical `json::load_document` assertion would still pass. **Run that
inversion and confirm the old-style assertion passes under it** — that is the
proof the new assertion is the load-bearing one.

**S3 — absolute references survive Save As unchanged.**
Build a project whose `runtime_assets.skeleton_path` is the fixture's *absolute*
`.mskl` path, Save As it into a temp directory, reload.
Assert: the stored `runtime_assets.skeleton_path` in the reloaded project is
still absolute and **string-equal** to what went in.
*Observable because:* the rule's `is_absolute()` early return is the only thing
producing this. Invert by deleting that branch, and
`make_project_relative_path` relativizes the path whenever the temp directory
happens to be an ancestor — and the stored string changes, which the assertion
compares directly. To keep the inversion reliable regardless of temp-directory
layout, the case places the referenced asset **inside** the Save As destination
directory so the relative form is always representable.

**S4 — undo across a Save As still yields a project that opens.**
Open the fixture, commit one edit, `session.save(temp_dir / "moved.marrow")`,
`session.undo()`, `session.save()` (no argument — same new path), then
`load_project` the file.
Assert: the undo succeeds; the save succeeds; **`load_project` succeeds**; and the
reloaded project's `resolved_skeleton_path()` still points at the original
`.mskl`.
*Observable because:* undo restores `entry.before.project` verbatim
(`session.cpp:2280`). If §4.5's history rebase is missing, the restored project
carries the *old* relative paths under the *new* `source_path`, and the save that
follows writes an unopenable file — caught by `load_project`, and by nothing else.
Invert by rebasing only `source_path` in `rebase_history` (today's code) and S4
fails at the reload while S2 still passes, which is what proves S4 covers a
distinct defect.

**S5 — a re-serialization-free history rebase is caught by the dirty flag.**
Same setup as S4, but assert immediately after the Save As that
`session.dirty()` is `false`, then `session.undo()` and assert `dirty()` is `true`,
then `session.redo()` and assert `dirty()` is `false` again.
*Observable because:* `update_dirty` compares `serialize_project(*load.project)`
against `saved_serialized_project`, and `histories_equal` compares the snapshots'
cached strings. Invert by rebasing the snapshot `ProjectData` but skipping
`serialized_project = serialize_project(...)`: the cached string still describes
the pre-rebase paths, the comparison disagrees with reality, and the dirty flag
flips wrongly on one of the three assertions. This is the inversion that catches
step 2 of §4.5 being forgotten — S4 alone would not, because S4's final save
re-serializes from the live project.

**S6 — `create` builds a dirty, unwritten, openable-after-save session.**
`EditorSession::create` with `project_path` in a temp directory,
`skeleton_path` / `atlas_paths` pointing at the fixture's absolute assets.
Assert: the result succeeds; `has_project()`; `dirty()` is **true**;
`undo_count() == 0` and `redo_count() == 0`; `runtime_data() != nullptr`;
`preview_skeleton() != nullptr`; and **`std::filesystem::exists(project_path)` is
false**. Then `session.save({})`, and assert `load_project(project_path)` succeeds
and `dirty()` is false.
*Observable because:* each clause has a distinct failure. Invert by setting
`saved_serialized_project = serialize_project(project)` in step 5 (i.e. copying
`open`'s line) and the `dirty()` assertion fails while everything else passes —
which is the precise MAR-181 requirement this case exists to protect.

**S7 — a `create` against a missing rig changes nothing.**
Open the fixture first, snapshot `serialize_project(*session.project())`,
`project_revision()`, `runtime_revision()`, `preview_revision()`, `undo_count()`
and `dirty()`. Call `create` with a `skeleton_path` that does not exist. Assert:
the result is a failure carrying a load error; and **every one of the six
snapshotted values is unchanged**, and `project()->source_path` is still the
fixture.
*Observable because:* the six-value comparison is a total description of the
session's authoring state. Invert by assigning `impl_->load.project` before the
skeleton load (the mistake `reload_runtime_source_assets` makes) and the
`source_path` and `project_revision` assertions both fail.

**S8 — `close` clears the session and bumps revisions.**
Open the fixture, record the three revisions, `close()`. Assert: `close()`
returned true; `has_project()` is false; `project()` is null; `runtime_data()` is
null; `can_undo()` and `can_redo()` are false; `dirty()` is false; and each of the
three revisions is **strictly greater** than its recorded value. Then `open` the
fixture again and assert it succeeds — a closed session is reusable.
*Observable because:* "strictly greater" is what a reset-to-zero implementation
fails. Invert by zeroing the counters in `close` and the three comparisons fail
while every other clause passes.

**S9 — `close` and `create` refuse an active transaction.**
With a live `EditTransaction`, call `close()` and `create(...)`. Assert both are
refused, the transaction is still usable, and `commit()` afterwards still succeeds.
*Observable because:* invert by removing either gate and the commit that follows
operates on a session whose `load.project` was replaced underneath it —
`EditTransaction::project()` returns a pointer into a different `ProjectData` and
the committed content is wrong.

**S10 — a failed adoption leaves a coherent session.**
Copy the fixture and its `.mskl`/`.matl` into a temp directory, open the copy,
then overwrite the `.mskl` with a document that parses as JSON but **fails
`build_project_runtime`** (F11 — e.g. a bone whose `parent` names a bone that does
not exist, so `load_skeleton_data` rejects it). Call `adopt_runtime_sources()`.
Assert: the result is a failure with `SessionErrorCode::RuntimeBuildFailed`; the
six-value session snapshot from S7 is unchanged; `runtime_data()` still resolves
the pre-overwrite bone set; and — the coherence invariant —
`build_project_runtime(*session.project(), *session.base_skeleton_document())`
**still succeeds**, proving `skeleton_data` and `base_skeleton_document` are still
mutually derived.
*Observable because:* that last clause is precisely what
`shell_asset_watch.cpp:176`'s discarded rebuild can violate. Invert by restoring
the early swap (assign `base_skeleton_document` before building) with no rollback,
and the coherence assertion fails while the `SessionErrorCode` assertion still
passes — which is the whole point: the return code was never the problem.

### 10.2 `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`

**C1 — the hot-reload smoke still passes, unchanged.**
`validate_runtime_asset_hot_reload_smoke` (`shell_smoke_scenarios.hpp:14`,
implemented from `shell_smoke_project.cpp:290`) already drives
`poll_runtime_asset_changes` through an add stage and a delete stage and asserts
selection reconciliation, playback continuity and attachment identity across both.
It is the regression net for §5.2's rewrite and **is not modified**.
*Observable because:* it asserts post-adoption identities, not the call's return
value. If the rewrite drops `reconcile_selection_to_runtime` or the marquee resets,
its existing assertions fail on their own.

**C2 — a failed hot reload preserves the shell's session and its presentation.**
Extend the existing scenario with a third stage: rewrite the temp `.mskl` into a
document that parses but fails to build, snapshot the shell's
`serialize_project(*state.load_result.project)`, the three session revisions, the
selection's active item and `state.selected_animation_name`, then call
`poll_runtime_asset_changes`. Assert: the outcome is
`RuntimeAssetPollOutcome::Failed`; `state.error_message` is non-empty;
every snapshotted value is unchanged; `state.preview_skeleton` and
`state.animation_state` are both non-null and still usable
(`animation_state->get_current(0)` non-null); and a following
`reload_project(&state)` — after the `.mskl` is restored — succeeds.
*Observable because:* the "still usable" clause reaches through the shell's
cached raw pointers (`shell_state.hpp:825-826`), which are the thing a failed
rollback leaves dangling. Invert by having `adopt_runtime_sources` commit
`load.skeleton_data` before the preview bind, and the preview pointers point into
freed data — caught here and not at the model layer, because only the shell caches
them.

**C3 — a failed save preserves the file and the dirty flag.**
Copy the fixture into a temp directory, drive the shell onto it, make one edit so
`state.project_dirty` is true, read the `.marrow` bytes, install a failing
`RenameCallback`, call `save_project_file(&state, true)`. Assert: it returns
`false`; `state.error_message` is non-empty; `state.status_message` is
`"Project save failed"`; `state.project_dirty` is **still true**;
`session.dirty()` is still true; and the file's bytes are unchanged.
*Observable because:* `save_project_file` (`shell_core.cpp:639`) sets
`project_dirty = state->session.dirty()` only on the success path (`:657`). The
combination of "returned false" and "dirty still true" and "bytes unchanged" is
what a save that reported success over a truncated file would fail. Invert by
restoring the direct `ofstream` write: the bytes change and the dirty assertion
becomes the only survivor — run it and confirm.

The `RenameCallback` seam is process-global. `marrow_editor_shell` also saves
preferences through `write_atomically`. **C3 must scope the callback with an RAII
guard and must not touch preferences inside that scope.** This hazard is recorded
in §12.

### 10.3 Regression-only

- `./build/marrow_preference_tests` — must pass **unchanged**, including
  `preference_store_tests.cpp:750-799`'s rename-failure case. This is the proof
  that §3.1's extraction preserved the settings writer exactly.
- `./build/marrow_agent_dispatch_smoke` — registry still 64, case count unchanged
  from the MAR-179 baseline (AGENTS.md records 408).
- `tools/mcp/test_client.py` — 64/64 name parity, unchanged.

### 10.4 What is deliberately not asserted

- **No `.mbin` / `.mskl` equivalence assertion.** MAR-180 changes no numeric
  content. `.mbin` v2 narrows every number to float32 (`binary.cpp`,
  `append_float32`), so any such claim would need the same narrowing applied to
  both sides before comparing — and there is nothing here to compare.
- **No crash-injection test for F6/F7.** Killing a process mid-`rename` is not
  reproducible in this suite. F5 (injected rename failure) is the strongest
  mechanically testable proxy, and §3.2 states plainly what remains unproven.

## 11. Non-goals

Named so the boundaries of this arc are explicit.

**Not MAR-180 — belongs to MAR-181** (core File path workflows): the ImGui
directory/path modal; wiring New / Open / Save / Save As to menu items; validating
user input before constructing a session; a UI choice about whether exports follow
a Save As (§4.3).

**Not MAR-180 — belongs to MAR-182** (dirty-session intent): the Save / Discard /
Cancel state machine; intercepting Quit and native OS close; retrying a pending
intent. MAR-180 supplies the primitive MAR-182's "Save completes the pending
intent only after an atomic save succeeds" depends on, and nothing more.

**Not MAR-180 — belongs to MAR-183** (recent projects): any recent-project list,
its ordering, eviction or canonicalization. MAR-180 touches `preferences.cpp` only
to *move* `write_atomically` out of it; `EditorPreferences::recent_projects`
(`preferences.cpp:319-328`) is read and written exactly as it is today.

**Not MAR-180 — belongs to MAR-188**: PSD provenance (§9). `psd_import.cpp` is not
modified.

**Not MAR-180 at all:**
- **Export atomicity.** `export_runtime_assets` (`project.cpp:7886`) writes a
  `.mskl`, then N atlases and their textures, then an optional `.mbin`, with no
  rollback. It is genuinely non-atomic, and it is not in the acceptance criteria:
  export writes to a separate output directory and a partial export cannot make a
  *project* unopenable. The extracted primitive is available if a later story wants
  it. `write_text_file` / `write_binary_file` keep their current implementations.
- **`fsync` durability** (§3.2).
- **The in-memory empty-`atlas_paths` window.** A transaction can empty
  `atlas_paths` and commit, even though `load_project` rejects it on disk
  (`project.cpp:291-297`). Pre-existing, orthogonal, untouched.
- **No new agent operation and no MCP change** (§8).

## 12. Risks

1. **Extracting `write_atomically` breaks the settings writer.** ~150 lines of
   `#if defined(_WIN32)`-split code moving between translation units. Mitigation:
   move it verbatim — no reformatting, no signature change; keep `RenameCallback`
   and `set_preference_rename_callback_for_testing` under the same names in the
   same `marrow::editor::detail` namespace; have `preferences_internal.hpp` include
   the new header so `preference_store_tests.cpp` is not edited at all. The gate is
   `./build/marrow_preference_tests` passing with a **zero-line diff** in
   `src/tests/`.
2. **The rename seam is process-global.** Two subsystems now share it, and
   `marrow_editor_shell` exercises both. Mitigation: RAII scoping in every test
   that installs it, and a comment on the setter naming the hazard. Not a
   production concern — the callback is empty unless a test installs it.
3. **The history rebase is the subtlest change in the story** (§1.4, §4.5).
   Forgetting the re-serialization produces a bug that no return code reveals.
   Mitigation: S5 exists solely to catch it, with an inversion that isolates it
   from S4.
4. **`editor_project_smoke.cpp` is being edited concurrently** by another agent.
   Mitigation: MAR-180's cases go in new functions appended near the end of the
   file and registered from `main`; rebase before running, and do not reformat
   neighbouring code. `editor_project_smoke.cpp:1530-1534`'s weak assertion is
   **left in place** — S2 adds the strong one rather than editing the existing
   case, so the two agents do not collide on the same lines.
5. **`export_directory` rebasing will surprise someone** (§4.3). It is the
   acceptance criterion's literal reading and it is asserted explicitly in S2, so
   the behaviour is documented rather than emergent. If MAR-181 wants the other
   behaviour, it changes one call site and one assertion.
6. **`make_project_relative_path` turns cross-directory references absolute**
   (§4.2). Correct but non-portable. Accepted, documented, and the reason S3 places
   its asset inside the destination directory.

## 13. Errors found in this story's own governing documents

Recorded per the standing lesson that every story in this chain has found some.

1. **The story requires rebasing a "PSD provenance path" that does not exist.**
   `.marrow.editor.import_sources.psd` is scheduled for MAR-188
   (`docs/root1/editing-gap-analysis.md:430`) and is absent from the schema,
   `ProjectData`, the loader and the serializer today. §9 states what MAR-180 does
   instead.
2. **`AGENTS.md:296` records the MAR-179 guard count as "10 → 11".** The tree
   holds **10** `operation_count_before != 64U` sites, and MAR-179's own Task 0
   enumerated **9** before its change. Both ends of that record are off by one.
   §8's table is the measured list; Task 0 re-measures it.
3. **The story's framing implies session-level adoption is broadly non-atomic.**
   Measured, `EditorSession::open` and `EditorSession::reload` already are, and
   `EditTransaction::commit` already rolls back a failed rebuild (§1.5, §1.7). The
   single non-atomic adoption path is `shell::reload_runtime_source_assets`. The
   plan is scaled to that, not to a rewrite of the session.
4. **"Project save writes a temporary file … so failure preserves the previous
   file byte-for-byte" understates the current defect.** The bigger problem is not
   only that a *reported* failure destroys the file, but that a close-time write
   error is **never reported at all** (§1.1, case 3), so the session is marked
   clean over a truncated file. The atomic writer fixes both; only the first is in
   the criterion.
5. **"Save As rebases every relative … path" cannot be satisfied for paths inside
   `preserved_root`.** Unknown additive members are opaque by design (§9). The
   criterion is satisfiable only for the five known fields, which is what §4
   specifies.
