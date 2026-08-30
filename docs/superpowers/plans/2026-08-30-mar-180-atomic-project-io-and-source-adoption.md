# MAR-180 Make Project I/O and Source Adoption Atomic — Implementation Plan

Design: `docs/superpowers/specs/2026-08-30-mar-180-atomic-project-io-and-source-adoption-design.md`

Read the design first. This plan does not restate it; every task cites the section
it implements.

## Global constraints

1. **A passing `save()` proves nothing.** `validate_project_for_save`
   (`project.cpp:5503`) takes no base document and cannot resolve a reference.
   Every assertion that a saved project is *good* must go through
   `load_project(path)` — which materializes the references
   (`project.cpp:7485-7510`) and reaches `build_project_runtime` at
   `project.cpp:7495`. **`marrow::runtime::json::load_document` is not an
   acceptable substitute** and is exactly why the existing bug is invisible.
2. **Assert survival, not return codes.** For every failure case, assert the
   *artifact*: the file's bytes, or a six-value session snapshot
   (`serialize_project`, the three revisions, `undo_count`, `dirty`).
3. **State the inversion mechanism.** Each test step below names why its inversion
   is observable at that layer. If an inversion does not bite when run, **strengthen
   the test, do not weaken the gate** — and record it in the results.
4. **No count literal moves.** The registry stays at 64. Task 10 proves it by diff.
5. **`editor_project_smoke.cpp` is being edited concurrently.** Append new
   scenario functions near the end of the file and register them from `main`. Do
   not edit or reformat existing cases. In particular **leave
   `editor_project_smoke.cpp:1530-1534` exactly as it is** — S2 adds the strong
   assertion beside it rather than fixing it in place.
6. **Do not run `marrow_editor_shell` without `--auto-close`**, and do not let any
   run create `~/Library/Application Support/Marrow`; the shell smoke sets
   `MARROW_CONFIG_HOME` for preference isolation — keep it set.

## File and responsibility map

| File | Change |
|---|---|
| `src/editor/atomic_file_write.hpp` | **new** — `detail::RenameCallback`, `set_preference_rename_callback_for_testing`, `write_file_atomically` |
| `src/editor/atomic_file_write.cpp` | **new** — `write_atomically`'s body moved verbatim from `preferences.cpp:356-521`, plus `production_rename` (`:330`), `rename_callback` (`:351`), the two globals (`:41-42`), and the setter (`:586-589`) |
| `src/editor/preferences_internal.hpp` | include the new header; delete the moved declarations |
| `src/editor/preferences.cpp` | delete the moved definitions; `save()` (`:672`) calls `write_file_atomically` |
| `CMakeLists.txt` | one line: `src/editor/atomic_file_write.cpp` in `add_library(marrow_editor STATIC` (`:499-518`) |
| `include/marrow/editor/project.hpp` | declare `rebase_project_paths` |
| `src/editor/project.cpp` | `rebase_project_paths`; `save_project` (`:7846`) rebases then writes atomically |
| `include/marrow/editor/session.hpp` | declare `create`, `close`, `adopt_runtime_sources` |
| `src/editor/session.cpp` | implement the three; extend `save`'s history rebase (`:1807-1814`) |
| `src/editor/session_shell_binding.hpp` | expose `adopt_runtime_sources` |
| `src/editor/shell_asset_watch.cpp` | `reload_runtime_source_assets` (`:128`) delegates; delete `:155-186`'s rollback |
| `src/samples/editor_project_smoke.cpp` | S1-S10 |
| `src/editor/shell_smoke_project.cpp` | C2, C3 |
| `AGENTS.md` | verification lines + results section |

**Not touched:** `src/tests/preference_store_tests.cpp` (zero-line diff is a gate),
`agent_dispatch.cpp`, any `agent_handlers_*.cpp`, `tools/mcp/**`,
`shell_constraints.cpp`, `psd_import.cpp`, `export_runtime_assets`.

---

## Task 0 — Measure the as-built tree (mandatory, no code)

Every number the design asserts is re-measured here. **If any measurement
disagrees, stop and reconcile before writing a line of code.**

```sh
# ---- 1. Registry. Design §8 claims 64, split edit 39 / inspection 12 /
#         management 10 / validation 3.
awk '/^constexpr OperationSpec kOperationSpecs\[\] = \{/{f=1} f&&/^};/{f=0} f' \
  src/editor/agent_dispatch.cpp | grep -c '^\s*{"'
#   expect: 64
awk '/^constexpr OperationSpec kOperationSpecs\[\] = \{/{f=1} f&&/^};/{f=0} f' \
  src/editor/agent_dispatch.cpp | grep -oE '"(edit|inspection|management|validation)"' \
  | sort | uniq -c
#   expect: 39 edit / 12 inspection / 10 management / 3 validation
#   If not, §8's "registry unchanged" proof and Task 10 are both invalid.

# ---- 2. Count sites. Design §8 claims 10 guards + 10 messages + 1 array + 2 python.
grep -rn "operation_count_before != 64U" src/
#   expect EXACTLY 10, at: shell_smoke_constraints.cpp:147,676;
#          shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603;
#          shell_smoke_timeline.cpp:3697   (+ a message one line below each)
grep -n "OperationExpectation, 64" src/samples/agent_dispatch_smoke.cpp   # expect :39
grep -n "== 64" tools/mcp/test_client.py                                  # expect :53 and :55 only
#   NOTE: AGENTS.md:296 records MAR-179 as "10 -> 11". The tree holds 10.
#   Record the measured list verbatim; Task 10 re-runs this and diffs.
#   NONE of these move in MAR-180.

# ---- 3. The save gap itself (design §1.1). Confirm before building anything.
sed -n '7846,7884p' src/editor/project.cpp
#   expect: validate at :7849, create_directories at :7856,
#           `std::ofstream output(path);` at :7866 (NO temp file, NO rename),
#           `if (!output)` at :7874 (NO close/flush check after it).
#   If a temp+rename already exists, §1.1 is wrong -- stop.

# ---- 4. The Save As gap (design §1.2). Confirm the fixture is relative.
python3 -c "import json;d=json.load(open('assets/fixtures/player_idle.marrow'));print(d['runtime'])"
#   expect: {'atlases': ['player_idle.matl'], 'skeleton': 'player_idle.mskl'} -- RELATIVE.
sed -n '7880,7882p' src/editor/project.cpp
#   expect: `saved_project.source_path = path;` and NOTHING that rewrites
#           runtime_assets.* . If a rebase exists, §1.2 is wrong -- stop.
grep -n "load_document(save_result.project->source_path)" src/samples/editor_project_smoke.cpp
#   expect :1531 -- the weak assertion §1.3 and §10.1-S2 are built around.

# ---- 5. The five path families (design §1.2 table). Verify each is serialized
#         from the struct, so rebasing the struct is sufficient.
grep -n "runtime_assets.skeleton_path.generic_string\|atlas_path.generic_string\|export_directory.generic_string\|image_path.generic_string" \
  src/editor/project.cpp
#   expect :4501, :4521, :4725, :4729, :4745
sed -n '4895,4901p' src/editor/project.cpp
#   expect root["atlas_packs"] rebuilt from project.atlas_pack_definitions
#          (so preserved_root cannot shadow a stale copy).

# ---- 6. PSD provenance (design §9 / §13.1). Confirm it does NOT exist.
grep -rn "import_sources" src/ include/ assets/
#   expect: NO HITS. Any hit means MAR-188 landed early and §4 needs a sixth family.

# ---- 7. The adoption gap (design §1.5).
sed -n '155,186p' src/editor/shell_asset_watch.cpp
#   expect: mutation of load_result.base_skeleton_document/atlas_data at :157-160
#           BEFORE rebuild_project_runtime at :162, two hand-rolled restore blocks,
#           and a discarded `(void)rebuild_project_runtime(state);` at :176.
grep -n "load_result(marrow::editor::EditorSessionShellBinding::load_result" src/editor/shell_state.hpp
#   expect :788 -- ShellState::load_result is a REFERENCE into the session.
#   This is why :157-160 mutates the session and not a shell copy.
sed -n '1691,1730p' src/editor/session.cpp
#   expect open() to load into a LOCAL and commit at :1719-1728. It is already
#   atomic -- do not "fix" it. Same for reload() at :1730.

# ---- 8. The house pattern (design §3.1).
sed -n '356,362p;505,521p' src/editor/preferences.cpp
#   expect write_atomically at :356 and the rename callback dispatch at :509-514.
grep -n "ScopedRenameCallback\|rename_calls == 1" src/tests/preference_store_tests.cpp
#   expect :218, :768, :785 -- the model S1 copies.

# ---- 9. Session lifecycle (design §1.6). Confirm create/close are absent.
grep -n "create\|close" include/marrow/editor/session.hpp
#   expect NO EditorSession::create and NO EditorSession::close declaration.
grep -n "ProjectData create_minimal_project" src/editor/project.cpp   # expect :7308

# ---- 10. Baselines to beat. Record all four.
ctest --test-dir build -N | tail -3
./build/marrow_agent_dispatch_smoke 2>&1 | grep -c "\[ OK \]"
#   AGENTS.md:387 records 408 as of MAR-179. Record the measured number.
./build/marrow_preference_tests 2>&1 | tail -3
./build/marrow_project_smoke assets/fixtures/player_idle.marrow 2>&1 | tail -3
```

Record every measured value. Tasks 10 and 11 compare against them.

---

## Task 1 — Extract the atomic writer (pure refactor, no behaviour change)

Implements design §3.1. **This task changes no observable behaviour.** Its gate is
that `marrow_preference_tests` passes with a zero-line diff in `src/tests/`.

### Implementation

1. Create `src/editor/atomic_file_write.hpp` in `namespace marrow::editor::detail`:

```cpp
using RenameCallback = std::function<std::error_code(
    const std::filesystem::path& source,
    const std::filesystem::path& destination)>;

/**
 * @brief Installs a process-local atomic-rename failure seam for focused tests.
 *
 * HAZARD: the seam is process-global and is now shared by the settings writer and
 * the project writer. Scope every installation with RAII and do not perform an
 * unrelated atomic write inside that scope.
 */
void set_preference_rename_callback_for_testing(RenameCallback callback);

/**
 * @brief Writes `text` to `destination` through a temporary file and an atomic rename.
 * @param subject Noun used in error messages ("settings", "project").
 * @return Empty on success; otherwise a message naming the failed step and cause.
 *
 * Creates the temporary in the destination's own directory so the rename never
 * crosses a filesystem boundary. Every handled failure removes the temporary and
 * leaves `destination` byte-for-byte unchanged. Does not fsync.
 */
std::string write_file_atomically(
    const std::filesystem::path& destination,
    std::string_view text,
    std::string_view subject);
```

2. Create `src/editor/atomic_file_write.cpp`. Move, **verbatim**, from
   `preferences.cpp`: the two globals (`:41-42`), `production_rename` (`:330-348`),
   `rename_callback` (`:351-354`), `write_atomically`'s body (`:356-521`), and
   `set_preference_rename_callback_for_testing` (`:586-590`). Move the
   `#if defined(_WIN32)` include block and `<cerrno>`, `<cstdio>`, `<mutex>`,
   `<atomic>`, `<unistd.h>` with them. The only edits permitted:
   - return `std::string` instead of `PreferenceSaveResult` (the caller sets `path`);
   - replace the literal `"settings"` in the six message strings with the
     `subject` parameter, so `subject = "settings"` reproduces today's text
     **character for character**.
3. `preferences_internal.hpp`: `#include "atomic_file_write.hpp"`; delete the
   `RenameCallback` alias and the setter declaration. `preference_store_tests.cpp`
   includes this header and therefore still sees both names — **do not edit it**.
4. `preferences.cpp`: delete the moved code; `save()` (`:670-674`) becomes

```cpp
PreferenceSaveResult result;
result.path = settings_path_;
result.error = detail::write_file_atomically(
    settings_path_, runtime::json::serialize_pretty_round_trip(root), "settings");
return result;
```

5. `CMakeLists.txt:499-518`: add `src/editor/atomic_file_write.cpp` to
   `add_library(marrow_editor STATIC`.

### Verify

```sh
cmake --build build
./build/marrow_preference_tests
git diff --stat src/tests/            # must be EMPTY
```

`marrow_preference_tests` must pass, including the rename-failure case at
`preference_store_tests.cpp:750-799`. That case asserts the callback fires exactly
once, the destination bytes are unchanged, the exact temporary is removed, and no
other file is left behind — i.e. it already proves the extracted primitive intact.

**Risk (design §12.1):** if the build fails on a Windows-only path, the move was not
verbatim. Re-diff the moved region against `git show HEAD:src/editor/preferences.cpp`
rather than hand-editing.

---

## Task 2 — `save_project` writes atomically

Implements design §1.1, §3.1, §3.4 F1-F5.

### TDD — `marrow_project_smoke`, case S1

Append `validate_atomic_project_save()` to `src/samples/editor_project_smoke.cpp`
and call it from `main`. Add `#include "atomic_file_write.hpp"` — the target
already has `src/editor` on its include path (`CMakeLists.txt:815-818`) and already
includes a private header at `:22`, so **no CMake change**.

Copy `player_idle.marrow`, `.mskl` and `.matl` into a temp directory.
`load_project` the copy. Mutate `editor_metadata.notes`. Read the destination's
bytes into a `std::string`. Install a `ScopedRenameCallback` (copy the RAII class
from `preference_store_tests.cpp:218-231`) that counts calls, records
`(source, destination)` and returns `std::errc::permission_denied`. Call
`save_project(project, destination)`.

Assert, in order:
- `!save_result` and `save_result.error->path == destination`;
- the destination's current bytes **equal** the pre-save read;
- `load_project(destination)` still succeeds and `skeleton_data != nullptr`;
- `rename_calls == 1`;
- `observed_destination == destination`;
- `observed_source.parent_path() == destination.parent_path()` and
  `observed_source != destination`;
- `!std::filesystem::exists(observed_source)`;
- a `directory_iterator` over the temp directory lists exactly the three seeded
  filenames — no orphan temporary.

**Must fail first**, and it does for a reason worth stating: against today's code
the callback is never installed into any code path, so `rename_calls` is `0`
**and** the byte comparison fails, because `std::ofstream output(path)` at `:7866`
already truncated the file. Two independent clauses fail, which distinguishes "the
seam is not wired" from "the seam is wired but the file was still destroyed".

### Implementation

Rewrite `save_project` (`project.cpp:7846-7883`) to:

1. `validate_project_for_save` (unchanged, still first — F1);
2. `create_directories` (unchanged — F2);
3. `const std::string error = detail::write_file_atomically(path, serialize_project(project), "project");`
   and on non-empty `error`, `save_error.message = error; result.error = ...; return result;`
   (F3, F4, F5);
4. the existing `saved_project` / `source_path` tail (`:7880-7882`).

Add `#include "atomic_file_write.hpp"` to `project.cpp`.

### Verify

```sh
cmake --build build && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

---

## Task 3 — `rebase_project_paths`, and `save_project` uses it

Implements design §1.2, §4.1-§4.4. Depends on Task 2 only for ordering of the file.

### TDD — `marrow_project_smoke`, cases S2 and S3

**S2 (cross-directory Save As opens).** `EditorSession::open`
`assets/fixtures/player_idle.marrow`; `session.save(temp_dir / "moved.marrow")`.
Assert:
- the save succeeds;
- **`load_project(temp_dir / "moved.marrow")` succeeds** with non-null
  `skeleton_data`;
- `reloaded->resolved_skeleton_path() == original->resolved_skeleton_path()`;
- `reloaded->resolved_atlas_paths() == original->resolved_atlas_paths()`;
- `reloaded->resolved_export_skeleton_path()` has the **fixture** directory as an
  ancestor (design §4.3's decision, asserted not assumed);
- `session.project()->resolved_skeleton_path()` equals the same absolute path —
  the in-memory session tracks what was written.

*Inversion observability:* delete the `rebase_project_paths` call from
`save_project` and `load_project` fails on a missing `.mskl`. **Also assert, under
that inversion, that `json::load_document` on the same file still succeeds** —
proving the new assertion is the load-bearing one and `editor_project_smoke.cpp:1531`'s
existing one is not.

**S3 (absolute references survive).** Build a `ProjectData` whose
`runtime_assets.skeleton_path` is an **absolute** path to a `.mskl` copied
**inside** the Save As destination directory (so the relative form is always
representable and the inversion cannot be masked by temp-directory layout). Save
As it into that directory; `load_project` it back.
Assert the reloaded `runtime_assets.skeleton_path` is still `is_absolute()` and
string-equal to the input.

*Inversion observability:* delete the `if (ref.is_absolute()) return ref;` branch
and `make_project_relative_path` relativizes the reference, changing the stored
string — which S3 compares directly.

### Implementation

1. `include/marrow/editor/project.hpp`, next to `create_minimal_project`:

```cpp
/**
 * @brief Rewrites project-relative references so they resolve identically from a
 *        new project-file location.
 *
 * Rebases runtime_assets.skeleton_path, runtime_assets.atlas_paths,
 * editor_metadata.export_directory, and every atlas_pack_definitions entry's
 * atlas_path and sprite image_path. Absolute references and empty references are
 * returned unchanged. Paths inside `preserved_root` are opaque and cannot be
 * rebased; when MAR-188 adds `$.editor.import_sources.psd` it must be registered
 * here.
 */
ProjectData rebase_project_paths(
    const ProjectData& project,
    const std::filesystem::path& new_project_path);
```

2. `project.cpp`, beside `create_minimal_project` (`:7308`): implement design
   §4.1's rule with one local lambda applied to all five families, reusing
   `project.resolve_path` and `make_project_relative_path` (`:1374`). Set
   `result.source_path = new_project_path`.
3. `save_project` (`:7846`): first statement becomes
   `const ProjectData rebased = rebase_project_paths(project, path);`
   Everything downstream — validation, serialization, and the `saved_project` tail
   — uses `rebased`. The tail's `saved_project.source_path = path;` is now
   redundant but harmless; keep it as a belt-and-braces assignment.

Every caller inherits the fix: `EditorSession::save` (`session.cpp:1801`), the
agent `save` review path (`agent_handlers_management.cpp:69-78`),
`save_project_file` (`shell_core.cpp:648`), and all existing smokes.

### Verify

```sh
cmake --build build
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

The shell smoke matters here: `shell_smoke_project.cpp:308-314` calls
`create_minimal_project` with **absolute** asset paths and saves into their own
directory. If the rebase mishandles the absolute case, the hot-reload smoke breaks
immediately — a free regression net for S3.

---

## Task 4 — Rebase history across a Save As

Implements design §1.4, §4.5. **Depends on Task 3.**

### TDD — `marrow_project_smoke`, cases S4 and S5

**S4 (undo across Save As still opens).** Open the fixture; `begin_edit` +
`commit` one `editor_metadata.notes` change; `session.save(temp_dir / "moved.marrow")`;
`session.undo()`; `session.save({})`; `load_project(temp_dir / "moved.marrow")`.
Assert every step succeeds, and the reloaded project's `resolved_skeleton_path()`
still equals the fixture's.

*Inversion observability:* restore today's `rebase_history` (source_path only,
`session.cpp:1808-1814`). The undone snapshot carries the old relative
`player_idle.mskl` under the new `source_path`, the second save writes it, and
`load_project` fails on the missing skeleton. **S2 still passes under that
inversion** — which is what proves S4 covers a distinct defect.

**S5 (the re-serialization is load-bearing).** Same setup; assert
`dirty() == false` right after the Save As, `dirty() == true` after `undo()`, and
`dirty() == false` after `redo()`.

*Inversion observability:* rebase each snapshot's `ProjectData` but skip
`serialized_project = serialize_project(...)`. `update_dirty` compares the live
serialization against `saved_serialized_project`, and `histories_equal`
(`session.cpp:1176`) compares the stale cached strings; one of the three dirty
assertions flips. **S4 does not catch this**, because S4's final save
re-serializes from the live project — which is exactly why S5 exists separately.

### Implementation

`session.cpp:1807-1814`, inside the `if (source_changed)` branch, replace the
lambda body with, for each entry and for each of `entry.before` / `entry.after`:

```cpp
snapshot.project = rebase_project_paths(snapshot.project, result.project->source_path);
snapshot.serialized_project = serialize_project(snapshot.project);
```

keeping `entry.descriptor.allow_merge = false`. The `else` branch (same-path save)
is unchanged — it only clears `allow_merge`.

### Verify

```sh
cmake --build build && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

---

## Task 5 — `EditorSession::adopt_runtime_sources`

Implements design §5.1, §3.4 F9-F13. Independent of Tasks 2-4.

### TDD — `marrow_project_smoke`, case S10

Copy the fixture, `.mskl` and `.matl` into a temp directory; `session.open` the
copy. Snapshot six values: `serialize_project(*session.project())`,
`project_revision()`, `runtime_revision()`, `preview_revision()`, `undo_count()`,
`dirty()`. Also record `runtime_data()->bones().size()`.

Overwrite the temp `.mskl` with a document that **parses as JSON but fails
`load_skeleton_data`** — the cheapest reliable form is a bone whose `parent` names
a bone that does not exist, which `build_project_runtime` rejects at
`project.cpp:7836`. Call `session.adopt_runtime_sources()`.

Assert:
- the result is a failure with `SessionErrorCode::RuntimeBuildFailed`;
- all six snapshot values are unchanged;
- `runtime_data()->bones().size()` is unchanged;
- **coherence:** `build_project_runtime(*session.project(), *session.base_skeleton_document())`
  still **succeeds**.

Then restore the original `.mskl` bytes and assert `adopt_runtime_sources()`
succeeds, `runtime_revision()` and `preview_revision()` both increased, and
`project_revision()` did **not**.

*Inversion observability:* re-introduce the early swap — assign
`load.base_skeleton_document` before building — with no rollback. The
`SessionErrorCode` assertion still passes; the **coherence** assertion fails,
because `skeleton_data` is then derived from the old document while
`base_skeleton_document` is the new broken one. That is the defect
`shell_asset_watch.cpp:176`'s discarded rebuild can produce, and the return code
never reveals it.

### Implementation

1. `include/marrow/editor/session.hpp`, after `reload` (`:166`): declare
   `SessionResult adopt_runtime_sources();` with the design §5.1 doc comment.
2. `session.cpp`, immediately after `reload` (`:1730-1785`) so the three adoption
   paths read together: implement design §5.1's six steps. Model the gates on
   `rebuild_runtime_without_history` (`:2434-2447`) and the local-then-commit shape
   on `reload` (`:1744-1781`). Reuse `PreviewController::capture` /
   `restore_playback` / `restore_transient_state` exactly as `reload` does.
   Bump `runtime_revision` and `preview_revision` only; call `update_dirty()`.
3. `session_shell_binding.hpp`, beside `rebuild_runtime_without_history` (`:58-61`):

```cpp
/** Reloads and atomically swaps the project's runtime source assets. */
static SessionResult adopt_runtime_sources(EditorSession& session) {
    return session.adopt_runtime_sources();
}
```

### Verify

```sh
cmake --build build && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

---

## Task 6 — The shell hot-reload path delegates

Implements design §5.2. **Depends on Task 5.**

### TDD — `marrow_editor_shell`, case C2 (C1 is regression-only)

**C1** is `validate_runtime_asset_hot_reload_smoke` (`shell_smoke_project.cpp:290`),
**unmodified**. It drives `poll_runtime_asset_changes` through an add stage and a
delete stage and asserts selection reconciliation, playback continuity and
attachment identity across both. It is the regression net for this rewrite.

**C2**: extend that scenario with a third stage. After the existing stages,
overwrite the temp `.mskl` with the same build-failing document S10 uses. Snapshot
`serialize_project(*state.load_result.project)`, the three session revisions,
`state.selection.active()`, and `state.selected_animation_name`. Call
`poll_runtime_asset_changes(&hot_reload_state)`.

Assert:
- the outcome is `RuntimeAssetPollOutcome::Failed`;
- `state.error_message` is non-empty and `state.status_message` is
  `"Runtime asset hot-reload failed"`;
- every snapshotted value is unchanged;
- `state.preview_skeleton != nullptr`, `state.animation_state != nullptr`, and
  `state.animation_state->get_current(0) != nullptr` — the cached pointers are
  still usable;
- after restoring the original `.mskl` bytes, `reload_project(&state)` succeeds.

*Inversion observability:* make `adopt_runtime_sources` commit
`load.skeleton_data` before the preview bind. The model layer's S10 still passes
(its snapshot never dereferences a cached pointer), but C2's "still usable" clause
fails, because only the shell caches `preview_skeleton` / `animation_state` raw
pointers (`shell_state.hpp:825-826`). That asymmetry is the reason C2 lives at the
shell layer.

### Implementation

Rewrite `reload_runtime_source_assets` (`shell_asset_watch.cpp:128-201`):

- keep the `!state->load_result` guard and the two transient marquee snapshots;
- **delete** the document load (`:135-141`), the atlas loop (`:143-153`), the
  mutation (`:155-160`), and both rollback blocks (`:162-186`) — all of that moves
  into the session;
- call `EditorSessionShellBinding::adopt_runtime_sources(state->session)`;
- on failure: `state->error_message = result.error->format();`
  `state->status_message = "Runtime asset hot-reload failed";` restore the two
  marquee fields; `return false;`
- on success: keep every existing line from `:188` onward verbatim — the three
  selection resets, `reconcile_selection_to_runtime`,
  `reconcile_hierarchy_anchor_to_runtime`, the status message, the
  `error_message.clear()` — and add the two `EditorSessionShellBinding` re-fetches
  of `preview_skeleton` / `animation_state` that `rebuild_project_runtime`
  (`shell_core.cpp:516-519`) used to perform on this path.

The last point is the easy miss: today those pointers are refreshed as a side
effect of `rebuild_project_runtime`, which this path no longer calls. C2's
"still usable" clause is what catches forgetting it.

### Verify

```sh
cmake --build build
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

---

## Task 7 — `EditorSession::create` and `EditorSession::close`

Implements design §1.6, §6, §3.4 F14-F17. Independent of Tasks 2-6.

### TDD — `marrow_project_smoke`, cases S6-S9

**S6** — `create` into a temp `project_path` with the fixture's **absolute**
`.mskl` and `.matl`. Assert: success; `has_project()`; **`dirty()` is true**;
`undo_count() == 0`; `redo_count() == 0`; `runtime_data() != nullptr`;
`preview_skeleton() != nullptr`; **`!std::filesystem::exists(project_path)`**.
Then `session.save({})` and assert `load_project(project_path)` succeeds and
`dirty()` is false.
*Inversion:* set `saved_serialized_project = serialize_project(project)` in the
commit (i.e. copy `open`'s line, `session.cpp:1722`). Only the `dirty()` clause
fails — which is precisely the MAR-181 requirement this case protects.

**S7** — open the fixture, snapshot the six values from S10, call `create` with a
non-existent `skeleton_path`. Assert: failure with a load error; all six values
unchanged; `project()->source_path` still the fixture.
*Inversion:* assign `impl_->load.project` before the skeleton load. The
`source_path` and `project_revision` clauses both fail.

**S8** — open the fixture, record the three revisions, `close()`. Assert:
returned true; `has_project()` false; `project()` null; `runtime_data()` null;
`can_undo()` and `can_redo()` false; `dirty()` false; each revision **strictly
greater** than recorded. Then `open` the fixture again and assert success.
*Inversion:* zero the counters in `close`. Only the three "strictly greater"
clauses fail.

**S9** — with a live `EditTransaction`, call `close()` and `create(...)`. Assert
both are refused, then `transaction.commit()` still succeeds and the committed
value is readable through `session.project()`.
*Inversion:* remove either gate. The transaction's `project()` pointer
(`session.cpp:2544-2551`) then addresses a replaced `ProjectData` and the
post-commit read returns the wrong value.

### Implementation

1. `session.hpp`, before `open` (`:161`), declare `create` and `close` with the
   design §6 doc comments.
2. `session.cpp`, immediately before `open` (`:1691`):
   - `create`: design §6.1's five steps. Steps 3-4 duplicate the materialization
     `load_project` performs (`project.cpp:7485-7510`) and the preview bind `open`
     performs (`session.cpp:1705-1716`); factor the shared commit tail into a
     private `Impl` helper only if it comes out cleanly — **do not refactor `open`
     to share code with `create`**, because `open`'s dirty/baseline lines are the
     one thing that differs and merging them is how S6's inversion gets written by
     accident.
   - `close`: design §6.2's four steps. **Bump, never reset**, the three revisions;
     do **not** reset `next_transaction_id`.

### Verify

```sh
cmake --build build && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

---

## Task 8 — The shell save path reports and preserves

Implements design §3.4 F1-F5 at the shell layer. **Depends on Task 2.**

### TDD — `marrow_editor_shell`, case C3

Copy the fixture and its assets into a temp directory; drive a `ShellState` onto
the copy with `reload_project`; make one edit so `state.project_dirty` is true;
read the `.marrow` bytes; install a `ScopedRenameCallback` returning
`std::errc::permission_denied`; call `save_project_file(&state, true)`.

Assert: returns `false`; `state.error_message` non-empty;
`state.status_message == "Project save failed"`; `state.project_dirty` **still
true**; `state.session.dirty()` still true; the file's bytes unchanged;
`load_project` on the file still succeeds. Then drop the callback, call
`save_project_file` again, assert it returns `true`, `state.project_dirty` is
false, and `load_project` succeeds.

*Inversion observability:* restore the direct `std::ofstream` write in
`save_project`. The byte comparison fails immediately. Also run the weaker variant
— keep the atomic write but move `state->project_dirty = state->session.dirty();`
above the failure check in `save_project_file` (`shell_core.cpp:650-654`) — and
confirm the dirty clause is the one that fails, so the two clauses are known to be
independent.

**Scope the callback with RAII and perform no preference save inside that scope**
(design §12.2): `marrow_editor_shell` writes settings through the same seam.

### Implementation

None expected — `save_project_file` (`shell_core.cpp:639-671`) already returns
early on failure without clearing `project_dirty`. If C3 fails on the dirty
clause, fix `save_project_file`; if it fails only on the byte clause, Task 2 is
incomplete. **Do not "fix" a passing implementation to make the test author feel
useful** — record that the shell layer was already correct.

### Verify

```sh
cmake --build build
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

---

## Task 9 — Full-suite regression

```sh
cmake --build build
cmake --build build --target marrow_verify_third_party
cmake --build build --target marrow_constraint_warning_check

./build/marrow_unit_tests
./build/marrow_preference_tests
./build/marrow_windowing_tests
./build/marrow_pen_input_tests
./build/marrow_selection_tests
./build/marrow_viewport_interaction_tests
./build/marrow_timeline_model_tests
./build/marrow_timeline_graph_model_tests
./build/marrow_agent_socket_tests

./build/marrow_bootstrap
./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
./build/marrow_fixture_smoke assets/fixtures/player_idle.mbin assets/fixtures/player_idle.matl
./build/marrow_c_smoke
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_parameter_project_smoke
./build/marrow_atlas_packer_smoke
./build/marrow_psd_import_smoke assets/fixtures/psd_import_sample.psd assets/fixtures/psd_import_sample_reimport.psd
./build/marrow_spine_import_smoke assets/fixtures/spine_import_sample.json assets/fixtures/spine_import_sample.atlas
./build/marrow_agent_dispatch_smoke
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2

ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -L runtime
ctest --test-dir build --output-on-failure -L editor
```

`marrow_psd_import_smoke`, `marrow_atlas_packer_smoke` and
`marrow_spine_import_smoke` are in the list because they exercise
`write_text_file` / `write_binary_file` and the atlas-pack path families that
Task 3 rebases; a regression there is the most likely way this change breaks
something outside its own tests.

MCP parity, unchanged (see Task 10):

```sh
MARROW_CONFIG_HOME=/tmp/marrow-mar180-config \
  ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
python3 -m py_compile tools/mcp/server.py tools/mcp/test_client.py \
  tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
```

Compare `marrow_agent_dispatch_smoke`'s `[ OK ]` count against Task 0's recorded
baseline. It must be **identical** — MAR-180 adds no agent case.

---

## Task 10 — Count-sweep proof and documentation

### The sweep

```sh
git diff -U0 -- src/ tools/ CMakeLists.txt | grep -E '^[+-]' | grep -E '\b(62|63|64|65)\b'
```

**Expect zero lines.** MAR-180 adds no count literal and moves none. If any line
appears, inspect it — a blind substitution is unsafe here: `AGENTS.md` carries
`t = 0.62` and "62 lines", the theme carries `(51, 56, 64)` and `rgb(54,57,64)`,
and `test_client.py:1525`/`:1530` carry mesh coordinates `-64`/`64` while `:1484`
is an ordinal.

Then re-run Task 0's steps 1 and 2 and diff the output against the recorded
baseline. Registry **64**, split **39/12/10/3**, **10** guards at the same
file:line positions (allowing for line drift only in files this change edits —
which is none of the three), **1** array size, **2** python assertions.

### `AGENTS.md`

Add to the verification list, in the existing style:

- Atomic project save, cross-directory Save As rebasing, history rebasing, session
  `create`/`close`, and failure-safe runtime-source adoption:
  `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- Shell hot-reload failure coherence and save-failure preservation:
  `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`

Add a `## MAR-180 …Validation Results` section recording: the Task 0 measurements
verbatim; the two baselines (`marrow_agent_dispatch_smoke` `[ OK ]` count,
`marrow_project_smoke`); the registry-unchanged proof by diff; and Task 11's
inversion log.

**Also correct `AGENTS.md:296`**, which records MAR-179's guard count as
"10 → 11" where the tree holds 10 (design §13.2). Correct the number in place;
do not delete the sentence.

---

## Task 11 — Inversions

Run each, confirm the named case fails with the named symptom, restore, re-verify.
Record every one in `AGENTS.md`, **including any that does not bite** — a
specified inversion that cannot fire is a defect in the test, not a formality.

| # | Inversion | Must fail | Symptom |
|---|---|---|---|
| 1 | `save_project` restores the direct `std::ofstream` | S1 | `rename_calls == 0` **and** the byte comparison fails |
| 2 | The rename-failure cleanup (`cleanup_temporary`) removed | S1 | the `directory_iterator` clause finds an orphan `*.tmp.*` |
| 3 | `rebase_project_paths` call deleted from `save_project` | S2 | `load_project` fails on a missing `.mskl`; **confirm `json::load_document` still succeeds under it** |
| 4 | `if (ref.is_absolute()) return ref;` deleted | S3 | the stored path comes back relative |
| 5 | `rebase_history` rebases only `source_path` (today's code) | S4 | the post-undo save writes an unopenable project; **S2 still passes** |
| 6 | `serialized_project` re-serialization skipped in the history rebase | S5 | one of the three `dirty()` assertions flips; **S4 still passes** |
| 7 | `adopt_runtime_sources` swaps `base_skeleton_document` before building | S10 | the coherence clause fails while the `SessionErrorCode` clause passes |
| 8 | `adopt_runtime_sources` commits `load.skeleton_data` before the preview bind | C2 | the "pointers still usable" clause fails; **S10 still passes** |
| 9 | The `preview_skeleton`/`animation_state` re-fetch omitted in the rewritten shell path | C1 | the existing hot-reload smoke's playback-continuity assertion fails |
| 10 | `create` sets `saved_serialized_project` the way `open` does | S6 | `dirty()` is false on a project that was never written |
| 11 | `create` assigns `impl_->load.project` before loading the skeleton | S7 | `source_path` and `project_revision` both moved on a failed create |
| 12 | `close` zeroes the three revisions instead of bumping them | S8 | the three "strictly greater" clauses fail |
| 13 | The transaction gate removed from `close` | S9 | the post-commit read returns the pre-transaction value |
| 14 | `state->project_dirty = session.dirty()` moved above the failure check in `save_project_file` | C3 | the dirty clause fails while the byte clause passes |

Inversions 5, 6, 8 and 14 each carry an explicit "the other case still passes"
column. That is the point: it is what proves the two cases cover different
defects rather than the same one twice.

---

## Full verification checklist

- [ ] **Task 0 run and every value recorded.** Registry 64, split 39/12/10/3; 10
      guards + 10 messages + 1 array + 2 python assertions; `save_project`'s direct
      `ofstream` at `project.cpp:7866` with no temp/rename; the fixture's `runtime`
      paths relative; `import_sources` absent; `shell_asset_watch.cpp:157-160`
      mutating before `:162` builds; `ShellState::load_result` a reference at
      `shell_state.hpp:788`; `open`/`reload` already atomic; no
      `EditorSession::create`/`close`; baselines for
      `marrow_agent_dispatch_smoke`, `marrow_preference_tests`,
      `marrow_project_smoke`, `ctest -N`.
- [ ] `cmake --build build` clean.
- [ ] `cmake --build build --target marrow_verify_third_party`.
- [ ] `cmake --build build --target marrow_constraint_warning_check`.
- [ ] `./build/marrow_preference_tests` passes and `git diff --stat src/tests/` is
      **empty** — the extraction preserved the settings writer exactly.
- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` passes,
      covering S1-S10.
- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
      passes, covering C1 (unmodified), C2, C3.
- [ ] `./build/marrow_agent_dispatch_smoke` passes with an `[ OK ]` count
      **identical** to Task 0's baseline, against the exact 64-operation registry.
- [ ] `tools/mcp/test_client.py` reports 64/64 name parity; `py_compile` clean over
      all four MCP files.
- [ ] Every other binary in Task 9 passes, `marrow_psd_import_smoke`,
      `marrow_atlas_packer_smoke` and `marrow_spine_import_smoke` included.
- [ ] `ctest --test-dir build --output-on-failure`, plus the `-L runtime` and
      `-L editor` runs.
- [ ] **Count sweep returns zero lines**, and Task 0's steps 1-2 re-measure
      identically.
- [ ] **No `.marrow` schema change**: `git diff` touches no parser and no
      serializer key. The fixture round-trips.
- [ ] **No registry change**: `kOperationSpecs` has no added, removed, moved or
      edited row; `tools/mcp/tools/editing.py` has no added `types.Tool`.
- [ ] All 14 inversions run, each failing its named case with its named symptom,
      each restored, each recorded — including any that did not bite and what was
      strengthened in response.
- [ ] `AGENTS.md` updated: two verification lines, a MAR-180 results section, and
      the `:296` guard-count correction.
- [ ] `~/Library/Application Support/Marrow` still does not exist.
