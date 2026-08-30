# MAR-183 Persist and Manage Recent Projects — Implementation Plan

Design: `docs/superpowers/specs/2026-08-30-mar-183-recent-projects-design.md`
Story: `MAR-183`, depends on `MAR-182` (`9ad69c6`).
Branch: `feat/mar-168`.

Every `file:line` below is anchored at `9ad69c6`. Task 0 re-measures them.

**Read the design first.** In particular §1.1 (the storage layer already exists
and is finished — this story adds **no** parse, **no** serialize, and **no**
version bump), §2.4 (the arm), §2.5 (never prune), §4 (the gate), and §8 (the
four facts that must be measured, three of which gate other tasks).

**Standing rules for this plan.**

- TDD, strictly: the test goes in first, is **run and seen to fail for the stated
  reason**, and only then is the implementation written. A test that passes on
  first run is a defect in the test until proven otherwise.
- For every inversion, §6 of the design names the *state a test reads* and *why it
  differs*. Five stories in this arc shipped an inversion that could not bite.
  Assume yours is the sixth until you have watched it fail with the exact message.
- When a measurement disagrees with this plan, **the measurement wins**. Record
  the disagreement in the AGENTS.md "Document errors found" table; do not
  silently adapt.
- Never weaken a case to make an inversion bite. Strengthen the case.
- `git add`/`commit` only at Task 9. No `checkout`, no `stash`.

---

## Task 0 — Re-measure everything, before any code

**Depends on:** nothing.

No file is modified in this task. Every step prints a value; a value that
disagrees with the design is a **stop-and-record**, not a silent adaptation.

### 0.1 Rebuild the baseline

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build -N | tail -3
```

Record the `ctest -N` total. The documented baseline is **22** (`AGENTS.md:657`).

### 0.2 Registry, guards, counts — the count-sweep baseline

```bash
sed -n '25,100p' src/editor/agent_dispatch.cpp | grep -c '^\s*{"'
sed -n '25,100p' src/editor/agent_dispatch.cpp \
  | grep -oE '"(edit|inspection|management|validation)"' | sort | uniq -c
grep -rn "!= 64U" src/ | wc -l
grep -rn "OperationExpectation, 64" src/
grep -rn "== 64" tools/mcp/test_client.py
```

Expected: **64**; **39 edit / 12 inspection / 10 management / 3 validation**;
**10** guards; **1** array; **2** python assertions.

**Count-sweep traps — do not blind-substitute.** `AGENTS.md` contains `t = 0.62`
and "62 lines" (self-referential; they shift on every insertion).
`(51, 56, 64)` and `rgb(54,57,64)` in the theme contain 64.
`test_client.py:1484` is an ordinal. Never touch `IM_COL32(56, 61, 69, 255)`,
`"x": 56.0`, `56,995,840`, `PhysicsBoneState … 56 bytes/bone`, or
`IM_COL32(208,134,57,230)`.

This story adds a `10` (`kRecentProjectLimit`). Note it: a future sweep for `10`
would collide with "10 management" and "10 guards". Name the constant, never the
literal, in any later count check.

### 0.3 The settings version and the storage layer

```bash
grep -n "kEditorSettingsVersion" include/marrow/editor/preferences.hpp src/editor/preferences.cpp
grep -n "recent" src/editor/preferences.cpp
grep -rniE "recent|mru|last_?opened|project_history" src/editor/shell_*.cpp src/editor/shell_*.hpp include/ tools/
```

Expected: `kEditorSettingsVersion = 1` at `include/marrow/editor/preferences.hpp:12`
and read at `preferences.cpp:244`/`:249`; the parse at `preferences.cpp:269-295`
and the serialize at `:315-323`; **zero** hits in the third grep.

**If the third grep is non-empty, stop.** A recent-projects surface already
exists somewhere this plan did not look, and the plan is mis-scoped.

**Decision to confirm in writing before proceeding: `kEditorSettingsVersion`
stays at 1.** Design §1.3.

### 0.4 The MAR-182 code as built

Re-read, and confirm each line number or record the shift:

| What | Expected at |
|---|---|
| `DirtyIntentRequest` | `src/editor/shell_file_paths.hpp:144` |
| `begin_session_intent` declaration | `src/editor/shell_file_paths.hpp:246` |
| `commit_path_choice`'s deferred-Open arm | `src/editor/shell_file_paths.cpp:171-178` |
| `begin_file_action` | `src/editor/shell_file_paths.cpp:595-651` |
| `perform_session_intent` | `src/editor/shell_file_paths.cpp:663-678` |
| `begin_session_intent` body, incl. the retarget at `:692-695` | `src/editor/shell_file_paths.cpp:682-703` |
| `tick_dirty_intent` | `src/editor/shell_file_paths.cpp:705-732` |
| `resolve_dirty_intent` | `src/editor/shell_file_paths.cpp:734-763` |
| `apply_pending_file_action`, Open success at `:802-825`, New at `:839-860` | `src/editor/shell_file_paths.cpp:778-861` |
| `apply_save_as` success at `:584-592` | `src/editor/shell_file_paths.cpp:562-593` |
| `save_project_file` | `src/editor/shell_core.cpp:664-688` |
| File menu body | `src/editor/shell_project_panels.cpp:717-752` |
| `load_shell_preferences` / `set_shell_default_curve` | `src/editor/shell_preferences.cpp:73-90` / `:92-108` |
| `ShellState` file fields / preference fields | `src/editor/shell_state.hpp:860-869` / `:884-888` |
| Smoke rail tail | `src/editor/shell_smoke_project.cpp:4119-4170` |
| Probe machinery (`ProbeIdKind`, `MenuProbe`, `probe_id`) | `src/editor/shell_smoke_project.cpp:2252-2278` |
| `ScopedPreferenceIsolation` | `src/editor/shell_preferences.cpp:38-70` |

A shift of a few lines is expected and fine. A **function that is not where the
table says and not within ~30 lines of it** is a substantive disagreement:
re-read the surrounding code before trusting anything else in this plan.

### 0.5 Target layout

```bash
sed -n '499,521p' CMakeLists.txt        # marrow_editor sources
sed -n '675,687p' CMakeLists.txt        # marrow_preference_tests
sed -n '875,909p' CMakeLists.txt        # marrow_editor_shell sources
```

Confirm `marrow_preference_tests` links `marrow_editor` **only**, and that no
`shell_*.cpp` is in `marrow_editor`. Re-read
`src/tests/preference_store_tests.cpp:971-977` — *"Do not 'fix' this split by
linking the shell into a unit test."*

### 0.6 M4 gate — `weakly_canonical` on a missing path

Write a throwaway program in the scratchpad (do **not** add it to the tree):

```cpp
#include <filesystem>
#include <iostream>
int main() {
  namespace fs = std::filesystem;
  std::error_code ec;
  const fs::path missing = fs::temp_directory_path() / "mar183-does-not-exist/x.marrow";
  const fs::path out = fs::weakly_canonical(missing, ec);
  std::cout << "weakly_canonical(missing) -> '" << out.string()
            << "' ec=" << ec.value() << " (" << ec.message() << ")"
            << " absolute=" << out.is_absolute() << '\n';
  const fs::path rel = "assets/fixtures/player_idle.marrow";
  std::error_code ec2;
  std::cout << "weakly_canonical(relative existing) -> '"
            << fs::weakly_canonical(rel, ec2).string() << "' ec=" << ec2.value() << '\n';
  return 0;
}
```

Compile and run it. **Record the output verbatim in the AGENTS.md entry.** If the
missing-path call sets an error code, §2.3's fallback chain is what the P-case
must assert; say so explicitly before writing Task 1's test.

### 0.7 M5 — the preference directory absence, before

```bash
test -e ~/Library/Application\ Support/Marrow && echo PRESENT || echo ABSENT
```

Must print **ABSENT**. If it prints PRESENT at Task 0, stop and report: something
in a previous run escaped isolation, and every later absence check is meaningless.

### 0.8 Establish the current preference-test baseline

```bash
./build/marrow_preference_tests
```

Record the case count (documented: **11**) and that it passes.

### Task 0 exit criteria

Written down, with values: the `ctest -N` total; the registry counts; the
settings version; the three empty greps; the M4 output; ABSENT; the preference
case count; and any line-number shift from §0.4.

---

## Task 1 — The pure list algebra, and its preference test

**Depends on:** Task 0.

This task adds **no** shell code, **no** ImGui, and **no** preference I/O beyond
the store's existing round trip. It is the only task that touches
`marrow_editor`.

### 1.1 Test first — `src/tests/preference_store_tests.cpp`

Add one case, `"recent project list algebra and isolated round trip"`, registered
in `main()` after the existing eleven (`preference_store_tests.cpp:1218-1250`),
taking the case count **11 → 12**.

The case must include, each as a distinct `suite.expect` with a message that
names the property:

1. **M4 print.** Print `canonical_recent_path` of a known-missing path and of a
   known-relative existing path, every run. Assert only that the result is
   absolute and non-empty (whichever branch of §2.3 ran).
2. **Canonicalization.** `canonical_recent_path("assets/fixtures/player_idle.marrow")`
   is absolute and equals `canonical_recent_path` of its own absolute form.
3. **Dedup across spellings.** `promote` the relative path, then `promote` the
   absolute form: the list has **exactly one** entry. *(I6)*
4. **MRU ordering.** `promote(A); promote(B); promote(C)` → `[C, B, A]`.
5. **Promotion, not duplication.** Then `promote(A)` → `[A, C, B]`, size 3.
6. **Eviction at exactly the limit.** Promote 12 distinct paths in order
   `p1..p12`. Assert size `== kRecentProjectLimit`, `front() == p12`,
   `back() == p3`, and that `p1` and `p2` are **absent**. *(I7)*
7. **Changed-bool, both polarities.** `promote` of a new path returns true;
   `promote` of the current head returns **false** and leaves the list equal.
   `forget_recent_path` of an absent path returns false. *(I12's precondition)*
8. **`normalize_recent_paths`.** Over
   `["", "a/b.marrow", "<abs of a/b.marrow>", "c.marrow", …15 more]`:
   the empty entry is dropped, the two spellings collapse to one, the first
   occurrence's position is kept, and the result is capped at
   `kRecentProjectLimit`. Then assert a **second** call returns `false`
   (idempotence).
9. **Missing paths survive `normalize`.** A canonicalized path whose file does
   not exist is still in the normalized list. *(the load half of I5)*
10. **`drop_missing_recent_paths`.** Over a temp directory holding two real
    `.marrow` files plus two never-created paths: exactly the two missing are
    dropped, the two present keep their relative order, and the return is true.
    A second call returns false.
11. **Store round trip, isolated.** Under the file's existing
    `ScopedPreferenceEnvironment` + `TemporaryDirectory` with
    `MARROW_CONFIG_HOME` set: save a normalized 10-entry list through
    `PreferenceStore`, reload, and assert the vector compares equal and
    `default_curve` survived.

Do **not** link the shell into this binary.

### 1.2 Watch it fail

```bash
cmake --build build --target marrow_preference_tests
```

It must fail **to compile** — `marrow/editor/recent_projects.hpp` does not exist.
That is the correct first failure for a new module.

### 1.3 Implement

Create `include/marrow/editor/recent_projects.hpp` and
`src/editor/recent_projects.cpp` exactly as design §3.1 declares them.

Requirements:

- **No** `#include` of `session.hpp`, `project.hpp`, `preferences.hpp`,
  `shell_state.hpp`, or any ImGui header.
- Every filesystem call uses the `std::error_code` overload. Nothing throws.
- `canonical_recent_path`: `weakly_canonical(p, ec)`; on error
  `absolute(p, ec2).lexically_normal()`; on error `p`. An empty input returns
  empty.
- `promote_recent_project`: canonicalize; return false on empty; erase every
  equal entry; `insert(begin())`; `if (size > kRecentProjectLimit) resize(kRecentProjectLimit)`.
  Return whether the vector differs from its input. **Insert before truncating** —
  truncating first is inversion I7.
- Comparison is `a == b` on `std::filesystem::path` (bytewise on `native()`).
  **No case folding.** Put design §2.2's reason in the header comment, including
  the macOS consequence.
- `normalize_recent_paths`: canonicalize each, drop empties, keep the **first**
  of each duplicate group, cap. Idempotent.

Add both files to `marrow_editor` in `CMakeLists.txt:499-521`, alphabetically
adjacent to `preferences.cpp`.

### 1.4 Verify

```bash
cmake --build build
./build/marrow_preference_tests
```

12 cases, all green. Then:

```bash
grep -nE "session|ShellState|PreferenceStore|imgui|ProjectData" src/editor/recent_projects.cpp include/marrow/editor/recent_projects.hpp
```

Must return **0 lines** (R7).

### 1.5 Inversions for this task

Run each, confirm the exact failure, restore:

| Inversion | Must fail with |
|---|---|
| **I6** — return `path` unchanged from `canonical_recent_path` | Sub-assertion 3: two entries where one was required |
| **I7a** — `resize` before `insert` | Sub-assertion 6: `front()` is `p11`, not `p12` |
| **I7b** — `if (size > limit + 1)` | Sub-assertion 6: size 11 |
| **I6b** — dedup keeping the **last** occurrence in `normalize` | Sub-assertion 8: the surviving entry is at the wrong index |
| — | `normalize` non-idempotent (e.g. re-canonicalizing into a different form) | Sub-assertion 8's second call returns true |

---

## Task 2 — The targeted Open through the MAR-182 gate

**Depends on:** Task 0. (Independent of Task 1 — it touches no list.)

This task changes the intent machine only. Nothing records anything yet.

### 2.1 Test first — `src/editor/shell_smoke_project.cpp`

Add `validate_mar183_recent_gate_smoke(const ShellState&)` — **C22** — declared in
`shell_smoke_scenarios.hpp` after the MAR-182 block, and called from the rail at
`shell_smoke_project.cpp:4161` (after `validate_mar182_dirty_prompt_mouse_smoke`,
before C11's arm at `:4165`).

Reuse the existing helpers in that file: `load_seeded_project`,
`dirty_the_session`, `capture_session_snapshot`, `SessionSnapshot::operator==`.
Install a nested `ScopedPreferenceIsolation("mar183-c22")`.

Phases:

1. **Clean session, targeted Open performs immediately.**
   `begin_session_intent(&state, SessionIntent::Open, target)` →
   `state.pending_file_application` holds `{FileAction::Open, target}` and
   `state.dirty_intent` is empty. *(This is the whole new code path.)*
2. **Clean session, untargeted Open still raises the chooser.**
   `begin_session_intent(&state, SessionIntent::Open)` (no path) →
   `state.file_path_request` has a value with `action == FileAction::Open`, and
   `pending_file_application` is empty. **This is the regression guard**: it is
   what fails if the empty-path branch is dropped.
3. **Dirty session, the gate holds.** Dirty via `dirty_the_session`; snapshot;
   `begin_session_intent(&state, SessionIntent::Open, target)` →
   `dirty_intent == {Open, target, Prompting}`, `pending_file_application`
   **empty**, `file_path_request` **empty**, and
   `capture_session_snapshot(state) == before`. *(I3)*
4. **Discard performs the targeted intent.**
   `resolve_dirty_intent(&state, DirtyIntentResponse::Discard)` →
   `pending_file_application->path == target`.
5. **Retarget carries the path.** Fresh dirty state.
   `begin_session_intent(&state, Open, target_a)`; then
   `begin_session_intent(&state, Open, target_b)`; assert
   `state.dirty_intent->path == target_b`; then Discard and assert
   `pending_file_application->path == target_b`. *(I2)*
6. **Cancel leaves everything.** Fresh dirty state; arm with a path;
   `resolve_dirty_intent(Cancel)` → `dirty_intent` empty,
   `pending_file_application` empty, snapshot equal.
7. **A retarget from a targeted Open to a pathless intent clears the path.**
   Arm `{Open, target}`, then `begin_session_intent(&state, SessionIntent::Reload)`
   → `dirty_intent->intent == Reload` **and** `dirty_intent->path` is **empty**.
   Then Discard and assert a Reload was armed (`pending_file_application->action
   == FileAction::Reload` with an **empty** path, per `begin_file_action`'s
   `Reload` case at `shell_file_paths.cpp:642-647`). *(This is the other half of
   I2: retargeting must overwrite the path, not merely assign it when non-empty.)*

Every failure message must name what was expected and what was measured, in the
style of the surrounding cases.

### 2.2 Watch it fail

```bash
cmake --build build --target marrow_editor_shell
```

Must fail to compile: `begin_session_intent` takes two arguments and
`DirtyIntentRequest` has no `path`.

### 2.3 Implement — `src/editor/shell_file_paths.hpp`

- `DirtyIntentRequest` gains
  `std::filesystem::path path;` between `intent` and `phase`, with the comment
  from design §4.1 (non-empty **only** for a targeted Open; empty means "raise
  the chooser").
- `begin_session_intent` gains a defaulted third parameter:
  `void begin_session_intent(ShellState* state, SessionIntent intent, const std::filesystem::path& path = {});`
  Extend its doc comment to say the path is honoured only for
  `SessionIntent::Open`, and that a Recent entry is the only origin that supplies
  one.

### 2.4 Implement — `src/editor/shell_file_paths.cpp`

1. Add a file-local `arm_open(ShellState* state, const std::filesystem::path& path)`
   holding the four lines currently at `:174-178`, and have `commit_path_choice`
   call it. This is a **pure refactor**: the resulting binary must behave
   identically, which C4-C19 already witness.
2. `perform_session_intent` takes `const std::filesystem::path& path` and its
   `Open` case becomes the two-line branch in design §4.1.
3. `begin_session_intent`:
   - the `AwaitingSave` early return is unchanged;
   - the retarget assigns **both** `intent` and `path` — *both, unconditionally*;
   - the clean branch and the arm both pass `path` through;
   - `state->dirty_intent = DirtyIntentRequest{intent, path, DirtyIntentPhase::Prompting, false};`
4. `tick_dirty_intent` and `resolve_dirty_intent`: capture `path` alongside
   `intent` before the `reset()`, and pass both to `perform_session_intent`.

Nothing else in the machine moves. `begin_file_action`,
`apply_pending_file_action`, `absorb_close_request` and `draw_dirty_intent_modal`
are untouched by this task.

### 2.5 Verify

```bash
cmake --build build
MARROW_CONFIG_HOME=/tmp/mar183-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2
```

C12-C19 must **all still pass** — this task's refactor is behaviour-preserving
for every existing caller, and nineteen existing cases are the witness.

### 2.6 Inversions

| Inversion | Must fail with |
|---|---|
| **I2** — retarget `intent` only, leave `path` | C22 phase 5: the destination is `target_a` |
| **I2b** — assign `path` only when non-empty | C22 phase 7: a Reload retarget inherits the stale Open path |
| **I3-precursor** — `perform_session_intent`'s Open case ignores `path` and always calls `begin_file_action` | C22 phase 1: a chooser is raised instead of a deferred Open |
| — drop the empty-path branch (always `arm_open`) | C22 phase 2: `File > Open Project...` no longer opens the chooser — it arms an Open of an empty path |

---

## Task 3 — Recording policy: Open, Save As, and the New arm

**Depends on:** Task 1 (the algebra) and Task 2 (nothing yet, but Task 4 needs
both and this keeps the smoke rail in one order).

### 3.1 Test first — C21

Add `validate_mar183_recording_policy_smoke(const ShellState&)` to
`shell_smoke_project.cpp`, declared in `shell_smoke_scenarios.hpp`, called on the
rail immediately after C22.

Nested `ScopedPreferenceIsolation("mar183-c21")`. The isolation's
`settings_path()` is what the byte comparisons read.

Seven phases, one per row of design §2.4:

1. **Open records.** Seed and load a project; `begin_session_intent(&state,
   Open, seeded)`; `apply_pending_file_action(&state)` returns true; assert
   `state.preferences.recent_projects.front() == canonical_recent_path(seeded)`,
   size 1, and that the settings file now **exists** and reloads with that entry.
2. **Failed Open records nothing and preserves order.** Build MAR-181 C7's
   broken project (valid JSON naming a missing `.mskl`) — copy the construction
   from `validate_mar181_failed_open_preserves_shell`
   (`shell_smoke_project.cpp:1658`). Record the list before; attempt the open;
   assert `apply_pending_file_action` returned **false** and the list is
   **element-wise equal** to before. *(I4)*
3. **Save As records the new path.** `apply_save_as(&state, other_dir/x.marrow)`
   returns true → the new path is at the head, the previous head is at index 1,
   size 2.
4. **Failed Save As records nothing.** Under an RAII rename-seam failure
   (copy the scope discipline from
   `validate_mar181_failed_save_as_preserves_shell_path`,
   `shell_smoke_project.cpp:1934`): `apply_save_as` returns false, the list is
   unchanged, and the settings file is **byte-identical**. **No settings write
   may occur inside the seam scope** — assert this by comparing the file bytes
   taken immediately before installing the callback.
5. **New records nothing; its first save records.** Arm a New via
   `pending_file_application = {New, target, skeleton, atlases}` and run
   `apply_pending_file_action`; assert `create` succeeded, the file does **not**
   exist, and the list is unchanged; assert
   `state.pending_recent_on_first_save == target`. Then `save_project_file(&state,
   true)` → true; assert `target` is now at the head and
   `pending_recent_on_first_save` is **empty**.
6. **A startup project's ordinary Save records nothing.** *(I1 — the sharp one.)*
   A **fresh** `ShellState` in a **fresh** nested isolation; set
   `project_path` and call `reload_project` (the startup path, which records
   nothing); `load_shell_preferences` → empty list, settings file **absent**;
   dirty the session; `save_project_file(&state, true)` → true; assert the list is
   **still empty** and the settings file is **still absent**. The naive shape
   (open A then Ctrl+S) cannot bite — A would already be the head and the no-op
   skip would suppress the write. Do not use it.
7. **Reload records nothing.** From phase 1's state, `reload_project(&state)` →
   the list is element-wise equal.

### 3.2 Watch it fail

Compile error: `record_recent_project` and
`ShellState::pending_recent_on_first_save` do not exist.

### 3.3 Implement

**`src/editor/shell_state.hpp`** — after `pending_file_application` (`:862`):

```cpp
/// MAR-183: the path a New session was created at, awaiting its FIRST
/// successful save. Set ONLY by apply_pending_file_action's create branch;
/// consumed ONLY by the save_project_file that writes this exact path. No
/// property of the document distinguishes "created" from "opened", so the
/// discriminator is explicit, single-purpose and readable by a UI-free test.
std::optional<std::filesystem::path> pending_recent_on_first_save;
```

**`src/editor/shell_recent_projects.{hpp,cpp}`** — new, in `marrow_editor_shell`
(`CMakeLists.txt:875-909`, beside `shell_preferences.cpp`). This task implements
only `persist_recent_projects`, `record_recent_project`,
`forget_recent_project`, `forget_missing_recent_projects` and
`recent_menu_label`; the menu and `open_recent_project` land in Task 5.

`persist_recent_projects(ShellState* state, bool changed)`:

```
if (!changed) return true;                       // the no-op skip (I12)
const PreferenceStore store;                     // honours MARROW_CONFIG_HOME
const auto saved = store.save(state->preferences);   // the LOADED prefs, mutated
if (!saved) { state->error_message = "Failed to store recent projects: " + saved.error;
              return false; }                    // in-memory change is KEPT
state->preference_path = saved.path;
return true;
```

Mirror `set_shell_default_curve` (`shell_preferences.cpp:92-108`) exactly — same
default-constructed store, same failure semantics, same preservation of
`default_curve` and `preserved_root` by mutating the loaded value.

**`src/editor/shell_file_paths.cpp`** — three call sites, each after the existing
`state->project_path = …` assignment so the recorded path and the shell agree:

- `apply_pending_file_action`, Open success (`:814`):
  `record_recent_project(state, pending.path);` and
  `state->pending_recent_on_first_save.reset();`
- `apply_pending_file_action`, New success (`:847`):
  `state->pending_recent_on_first_save = pending.path;` — and **no record**.
- `apply_save_as` success (`:584`): `record_recent_project(state, chosen);` and
  `state->pending_recent_on_first_save.reset();` (design §2.4's tidy-up note).

**`src/editor/shell_core.cpp`** — `save_project_file`, after the success block at
`:682`, before the status message:

```cpp
// MAR-183: ONLY the first successful save of a New session records. An
// ordinary Save is the same function with no arm.
if (state->pending_recent_on_first_save.has_value() &&
    *state->pending_recent_on_first_save == state->project_path) {
    state->pending_recent_on_first_save.reset();
    record_recent_project(state, state->project_path);
}
```

Note the ordering: `reset()` **before** the record, so a failure inside the
recorder cannot leave the arm live for a second save.

### 3.4 Verify

```bash
cmake --build build
MARROW_CONFIG_HOME=/tmp/mar183-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2
```

C4-C22 all green. Pay attention to **C4, C5, C6, C7** — design §11 R2: Save As
and Open now write the settings file on success, inside cases that previously
wrote none.

### 3.5 Inversions

| Inversion | Must fail with |
|---|---|
| **I1** — drop the arm check; record on every successful save | C21 phase 6: the list is `[X]` and the settings file exists, where both must be empty/absent |
| **I1b** — record at `create` instead of at the first save | C21 phase 5: the list is non-empty while the file does not exist |
| **I4** — move the record above the `!attempted` early return | C21 phase 2: the broken path is at the head |
| — record `state->project_path` instead of `chosen` in `apply_save_as` | C21 phase 3: the head is the old path |

---

## Task 4 — Load-time normalization, and the never-prune guarantee

**Depends on:** Task 1, Task 3.

### 4.1 Test first — C23

`validate_mar183_missing_entries_smoke(const ShellState&)`, on the rail after C21.

Nested isolation. Hand-write a settings file into the isolated config home —
this is the only way to observe the load path, since nothing else can produce a
file naming a missing project:

```json
{"version":1,"default_curve":"ease","recent_projects":[
  "<abs present.marrow>", "<abs gone.marrow>", "<relative spelling of present>", ""]}
```

Assertions:

1. **Byte-identity across load.** Capture the file's bytes; `load_shell_preferences(&state)`;
   re-read: **byte-identical**. *(I11)*
2. **Missing entries survive.** `gone.marrow` is present in
   `state.preferences.recent_projects`. *(I5)*
3. **Normalization ran in memory.** The relative spelling collapsed into the
   present entry (size 2, not 3), the empty string is gone (the store already
   skips it — `preferences.cpp:281` — so this asserts the shipped behaviour), and
   both surviving entries are absolute.
4. **`default_curve` survived** as `Ease`, and `preference_status` is `Loaded` or
   `LoadedWithDefaults` (the empty entry makes it the latter — assert whichever
   Task 0's re-read of `preferences.cpp:281-294` says, and print it).
5. **`recent_project_exists`** is true for the present entry, false for the
   missing one.
6. **`forget_recent_project(&state, gone)`** removes exactly it; the file is
   rewritten; reloading through a fresh `PreferenceStore` yields the remaining
   entry and the preserved `default_curve`.
7. **`forget_missing_recent_projects`** over a list of two present and two
   missing removes exactly the two missing and preserves the present pair's
   order; a second call is a **no-op** and does not rewrite the file (compare
   bytes and mtime). *(I12)*
8. **A settings file that is absent stays absent across a load.** Fresh nested
   isolation, no file: `load_shell_preferences` → empty list, `FirstRun`, and
   `!fs::exists(settings_path)`. *(I11, first-run half)*

### 4.2 Implement

**`src/editor/shell_preferences.cpp`**, `load_shell_preferences`, immediately
after `state->preferences = result.preferences;` (`:79`):

```cpp
// MAR-183: normalize IN MEMORY only. Loading never writes -- a settings file
// the user is mid-way through hand-editing survives untouched, and an entry
// whose volume is merely unmounted is never destroyed. Design §2.5.
(void)marrow::editor::normalize_recent_paths(&state->preferences.recent_projects);
```

The discarded bool is deliberate and commented: there is no write to skip here.

Add `#include "marrow/editor/recent_projects.hpp"` to `shell_preferences.cpp`.
`shell_preferences.hpp`'s class comment gains one sentence recording that the
recent list is normalized on load and never written on load.

`forget_recent_project` / `forget_missing_recent_projects` (stubbed in Task 3)
become `persist_recent_projects(state, forget_recent_path(&list, p))` and
`persist_recent_projects(state, drop_missing_recent_paths(&list))`.

### 4.3 Verify

```bash
cmake --build build && ./build/marrow_preference_tests
MARROW_CONFIG_HOME=/tmp/mar183-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2
```

### 4.4 Inversions

| Inversion | Must fail with |
|---|---|
| **I5** — call `drop_missing_recent_paths` inside `load_shell_preferences` | C23 assertion 2: the missing entry is gone |
| **I11** — `persist_recent_projects(state, true)` after normalizing on load | C23 assertion 1 (bytes differ) and assertion 8 (a first run created a file) |
| **I12** — `persist_recent_projects` ignores `changed` and always saves | C23 assertion 7: the second `forget_missing` rewrote the file |

---

## Task 5 — The surface: `Open Recent`, `Remove`, `Clear Missing`

**Depends on:** Task 2 (the targeted intent), Task 3 (recording), Task 4
(missing-entry semantics). **All three.**

### 5.1 Test first — C25, the mouse case

`validate_mar183_recent_menu_mouse_smoke(const std::filesystem::path&)`, modelled
on `validate_mar182_dirty_prompt_mouse_smoke` (`shell_smoke_project.cpp:3530`).
Copy its `render_frame` / `sweep` / `click_position` / `fail` scaffolding
verbatim; those are the measured-correct harness primitives.

**Phase 0 — the two M-gate measurements, printed every run.**

- **M2.** Open `File`, hover/click `Open Recent`, read
  `context->OpenPopupStack[Size-1].Window->Name`, and **print it**, exactly as
  C19 prints `File###Menu_00` (`:3706-3708`). If clicking does not open the child
  popup, try a hover-plus-settle and print which worked. If neither does, **stop**
  and report — the submenu is undrivable under this harness and §2.6's shape must
  be revisited before any more test is written.
- **M3.** Seed one **missing** entry, sweep the submenu, and print
  `"MAR-183 C25 measured: a DISABLED MenuItem IS / is NOT reachable by HoveredId
  under this harness."` **This measurement selects assertion 4 below.** Do not
  write assertion 4 before the line has printed.

Then:

1. **The submenu exists and is wired.** `File` emits `Open Recent`
   (`ProbeIdKind::Direct` — design M1: `BeginMenuEx` uses `window->GetID(label)`,
   and a submenu inside a popup goes through no `##MenuBar` push. Confirm against
   the printed hover id). *(I9)*
2. **Every seeded entry is emitted.** Seed 3 recent entries (2 present, 1
   missing) by calling `record_recent_project` directly, then sweep for each
   `recent_menu_label(p)`. A label never hovered at any swept position is a
   **failure**, never a skip.
3. **A click over a DIRTY session raises the prompt.** Dirty the session, click a
   present entry: `kDirtyIntentModal` is `Active`,
   `state.dirty_intent->intent == SessionIntent::Open`,
   `state.dirty_intent->path == <that entry>`, `pending_file_application` is
   **empty**. This is the end-to-end proof of design §4. *(I3, I9)*
4. **The missing entry is not actionable.** Chosen by M3:
   - *if disabled items are hoverable* — the label is found, and a click at its
     position leaves `dirty_intent`, `pending_file_application` and
     `project_path` all unchanged;
   - *if they are not* — the label is **absent** from the sweep while both
     present labels are found, and a click at the position where it would be
     leaves the same three unchanged.
   Print which branch ran.
5. **`Remove` reaches the missing entry.** Open `Open Recent > Remove`, sweep for
   the **missing** entry's label, click it, and assert the list shrank by exactly
   that entry and the settings file reloads to match. *(I10)*
6. **`Clear Missing` exists and removes only the missing.** Re-seed one missing
   entry; click `Clear Missing`; assert every present entry survives in order and
   every missing one is gone.
7. **The empty-list case.** Clear the list; assert `Open Recent` is emitted but
   yields no child popup, or is not clickable — whichever the disabled-`BeginMenu`
   behaviour turns out to be. Print it.

### 5.2 Implement

**`src/editor/shell_recent_projects.cpp`** — `open_recent_project` and
`draw_recent_projects_menu` per design §2.6 and §3.2.

`open_recent_project` is exactly:

```cpp
void open_recent_project(ShellState* state, const std::filesystem::path& path) {
    if (state == nullptr || path.empty()) return;
    // THE gate. A Recent entry is an Open from a different origin, nothing more.
    begin_session_intent(state, SessionIntent::Open, path);
}
```

`draw_recent_projects_menu`:

```
if (!BeginMenu(kRecentMenu, !list.empty())) return;
for each entry:  MenuItem(recent_menu_label(p), nullptr, false, recent_project_exists(p))
                 -> open_recent_project(state, p)
                 tooltip: the full path
Separator();
if (BeginMenu(kRecentRemoveMenu)):
    for each entry: MenuItem(recent_menu_label(p))  // ALWAYS enabled
                    -> forget_recent_project(state, p)
    EndMenu();
MenuItem(kRecentClearMissing, nullptr, false, any_missing) -> forget_missing_recent_projects(state);
EndMenu();
```

**Iterate over a copy of the list**, or defer the mutation to after the loop: a
click inside the loop mutates the vector being iterated. Take
`const std::vector<fs::path> entries = state->preferences.recent_projects;` at
the top and iterate that.

`recent_menu_label(p)` returns `p.filename().string() + "##" + p.string()`.
Header comment: everything after `##` is excluded from display and included in
the id — the fact `shell_file_paths.hpp:52-54` already records for the modal
names — which is what makes two same-named files in different directories
distinct, and what makes the mouse probe exact.

**`src/editor/shell_project_panels.cpp`** — one call inside `BeginMenu("File")`,
immediately after the `Open Project...` item (`:727-729`):

```cpp
draw_recent_projects_menu(state);
```

Add `#include "shell_recent_projects.hpp"`. Extend the existing comment at
`:718-721` to say that a Recent entry is an Open like any other and enters the
same gate.

**Do not touch `shell_main.cpp` or `shell_smoke_frames.cpp`.** The menu draws
inside `draw_menu_bar`, which both frame bodies already call, and the deferral
rides `apply_pending_file_action`, which MAR-181's C11 already pins on the smoke
side. If this task finds itself editing either file, **stop** — that is a design
deviation (design §11 R3) and it needs a detector this plan has not specified.

### 5.3 Verify

```bash
cmake --build build
MARROW_CONFIG_HOME=/tmp/mar183-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2
```

Confirm C9 (MAR-181's six-item File-menu sweep) still passes: the new submenu
sits between `Open Project...` and the `Save` separator and must not displace any
probed item's discoverability.

### 5.4 Inversions

| Inversion | Must fail with |
|---|---|
| **I9** — delete the `draw_recent_projects_menu` call from the File menu | C25 assertion 1: `Open Recent` never emitted a widget with that id |
| **I3** — `open_recent_project` calls `session.open` directly | C25 assertion 3: no prompt, and the session was replaced under a dirty document. C22 phase 3 also fires |
| **I10** — pass `recent_project_exists(p)` as the `Remove` items' enabled flag | C25 assertion 5: the missing entry cannot be removed |
| — iterate the live vector instead of a copy | Expect a crash or a skipped entry under assertion 5; if it does **not** reproduce, say so and keep the copy anyway |

---

## Task 6 — Non-interference and write failure

**Depends on:** Task 3, Task 4.

### 6.1 Test — C24

`validate_mar183_non_interference_smoke(const ShellState&)`, on the rail after C23.

1. **A record does not dirty the project or touch history.** Load a seeded
   project into a **clean** state; capture `session.dirty()`, `can_undo()`,
   `can_redo()` and `serialize_project(*state.load_result.project)`;
   `record_recent_project(&state, other_path)`; assert all four are **identical**.
   *(I8)*
2. **Re-recording the head writes nothing.** Record `p`; capture the settings
   file's bytes **and** `last_write_time`; record `p` again; assert both are
   unchanged. *(I12)*
3. **Write failure preserves.** Under an **RAII-scoped**
   `set_preference_rename_callback_for_testing` returning
   `std::errc::permission_denied`:
   `record_recent_project` leaves `state.error_message` non-empty, leaves the
   **in-memory** list carrying the new entry (matching
   `set_shell_default_curve`'s documented behaviour), and leaves the settings
   file **byte-identical**. After the scope closes, a re-record succeeds and the
   file reloads with the entry.
   **Perform no project save inside that scope** — the seam is process-global and
   shared with the project writer (`atomic_file_write.hpp:22-25`).
4. **The store round trip preserves everything else.** After the successful
   re-record, reload through a fresh `PreferenceStore` and assert `default_curve`
   and any unknown additive field written into the seeded settings file both
   survived. (Seed the file with `"payload":"keep"` and assert it is still there —
   the `preserved_root` guarantee at `preferences.cpp:313-321`.)

### 6.2 Inversions

| Inversion | Must fail with |
|---|---|
| **I8** — wrap `record_recent_project` in `session.begin_edit` / `commit` | C24 assertion 1: `dirty()` and `can_undo()` both flipped |
| — construct a fresh `EditorPreferences` in `persist_recent_projects` instead of mutating the loaded one | C24 assertion 4: `default_curve` reset to Linear and `payload` gone |
| — return true on a failed save | C24 assertion 3: `error_message` empty |

---

## Task 7 — Prove the registry and the untouchable trees are unchanged

**Depends on:** Tasks 1-6.

Not an assertion — a diff.

```bash
git diff --stat -- src/editor/agent_dispatch.cpp src/editor/agent_handlers_*.cpp \
                   src/samples/agent_dispatch_smoke.cpp tools/
```

Must be **empty**. Then re-measure Task 0.2's five numbers and confirm each is
identical. Then the count-sweep check:

```bash
git diff -U0 -- src/ tools/ CMakeLists.txt | grep -E '^[+-]' | grep -E '\b(62|63|64|65)\b'
```

Any line that comes back must be inspected by hand and explained in the AGENTS.md
entry. MAR-181's run returned two lines, both `1.0f / 60.0f` frame deltas; expect
the same shape here from the new mouse harness.

Also confirm the C ABI and format trees are untouched:

```bash
git diff --stat -- include/marrow/c/ src/c/ include/marrow/runtime/ \
                   src/editor/project.cpp src/editor/session.cpp \
                   src/editor/atomic_file_write.cpp src/editor/preferences.cpp
```

Must be **empty**. `preferences.cpp` in particular: design §1.1 is the claim that
this story needs no storage change, and an empty diff is the proof.

---

## Task 8 — Full verification

**Depends on:** Tasks 1-7.

Run every command, record every output.

```bash
# Build
cmake -S . -B build
cmake --build build

# Unit and model layers
./build/marrow_preference_tests              # 12 cases (was 11)
./build/marrow_unit_tests
./build/marrow_windowing_tests
./build/marrow_pen_input_tests
./build/marrow_agent_socket_tests
./build/marrow_selection_tests
./build/marrow_viewport_interaction_tests
./build/marrow_timeline_model_tests
./build/marrow_timeline_graph_model_tests
./build/marrow_sokol_imgui_runtime_probe

# Project model (unchanged by this story, run as a regression witness)
./build/marrow_project_smoke assets/fixtures/player_idle.marrow

# Agent registry
./build/marrow_agent_dispatch_smoke          # 408 [ OK ] cases against 64 operations

# The shell -- this story's coverage, C20-C25 plus C4-C19
MARROW_CONFIG_HOME=/tmp/mar183-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2

# CTest guardrails
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -L runtime
ctest --test-dir build --output-on-failure -L editor
ctest --test-dir build --output-on-failure -R marrow.renderer_link_boundary

# Dependencies and warnings
cmake --build build --target marrow_verify_third_party
cmake --build build --target marrow_constraint_warning_check
```

MCP parity (the registry is unchanged, so this must be identical to Task 0's run):

```bash
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
python3 -m py_compile tools/mcp/server.py tools/mcp/test_client.py \
                      tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
```

### Full verification checklist

| # | Check | How |
|---|---|---|
| 1 | **`~/Library/Application Support/Marrow` is ABSENT after the entire run** | `test -e ~/Library/Application\ Support/Marrow && echo PRESENT \|\| echo ABSENT`. Run it **after** every command above, not only after the shell. **PRESENT is a hard failure of this story**, not a note |
| 2 | No stray config home escaped | `ls -d /tmp/marrow-shell-config-* 2>/dev/null` — every `ScopedPreferenceIsolation` removes its directory on destruction, so this must be empty (barring an unrelated crashed run) |
| 3 | `/tmp/mar183-cfg/editor-settings.json`, if created, contains only what the smoke wrote | `cat` it; it must be version 1 with a `recent_projects` array |
| 4 | `kEditorSettingsVersion` is still **1** | `grep -n "kEditorSettingsVersion" include/marrow/editor/preferences.hpp` |
| 5 | Registry unchanged at **64** | Task 7's empty diff + the re-measured 64 / 39-12-10-3 / 10 guards / 1 array / 2 python assertions |
| 6 | `preferences.cpp`, `project.cpp`, `session.cpp`, `atomic_file_write.cpp` untouched | Task 7's second `git diff --stat` |
| 7 | `marrow_editor` isolation intact | `test_editor_session_isolation` green, and the §1.4 grep over `recent_projects.cpp` returns 0 lines |
| 8 | `marrow_preference_tests` links no shell | `sed -n '675,687p' CMakeLists.txt` unchanged apart from nothing — this target is not edited by this story |
| 9 | Neither frame body was edited | `git diff --stat -- src/editor/shell_main.cpp src/editor/shell_smoke_frames.cpp` is **empty** |
| 10 | The four bypass greps (design §4.3, P1-P4) | All four return what §4.3 requires |
| 11 | Every inversion in design §6 was run, bit the named case, and was restored | Twelve entries, each with the exact failure message |
| 12 | The four M-gates were measured and printed | M2, M3, M4 printed in the run's output; M5 checked before and after |
| 13 | `ctest -N` total unchanged from Task 0 | This story adds no CTest registration |
| 14 | All 13 test binaries pass | Above |

---

## Task 9 — Documentation and commit

**Depends on:** Task 8, complete and green.

### 9.1 `AGENTS.md`

Add a **"MAR-183 Persist and Manage Recent Projects Validation Results"**
section, matching the MAR-181 and MAR-182 sections' structure:

- Opening paragraph stating what the story did and did **not** touch: no
  `.marrow` schema change, `.mskl` v1 / `.mbin` v2 / C ABI v1 untouched,
  `preferences.cpp` untouched (the storage already shipped),
  `kEditorSettingsVersion` still **1**, registry unchanged at **64** proved by an
  empty diff.
- **"What was measured before any code was written"** — Task 0's table verbatim,
  including the M4 output and the `ctest -N` total.
- **"Result"** — one row per check, with evidence.
- **"Inversions run"** — all twelve, each with the case it bit and the exact
  message, and any that **did not bite** recorded as such along with how the case
  was strengthened. Five stories in this arc had one; expect one.
- **"Document errors found"** — every disagreement between this plan or the
  design and the measured tree. Every story from MAR-175 on has found between
  three and seven. If you find zero, you have not looked hard enough; say so
  explicitly rather than leaving the section out.
- **"Not independently covered"** — carry forward MAR-182's frame-body note, plus
  design §10's seven limitations.

Add to **"Current Validation"** (the list at `AGENTS.md:33-47`), beside the
existing MAR-181/182 entries:

```
- Recent projects: canonicalization, MRU ordering, de-duplication, eviction at the
  bound, missing-entry visibility, Remove/Clear Missing, the dirty-gated Recent
  open, and failed-action preservation (C20-C25):
  `MARROW_CONFIG_HOME=/tmp/mar183-cfg ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
```

and extend the existing `marrow_preference_tests` line to name the recent-project
list algebra.

### 9.2 The PRD

Set `MAR-183`'s `status` to `"done"` and `completedAt` to the run's date in
`.agents/tasks/prd-marrow-runtime.json`. Do not touch any other story.

### 9.3 Commit

```bash
git add -A
git commit
```

Message, matching the arc's Korean convention:

```
feat: MAR-183 최근 프로젝트 목록 영속화와 관리 구현
```

Body: the list shape (canonical, deduped, MRU, capped at 10), the never-prune
decision, the three recording sites plus the New arm, the targeted-Open route
through MAR-182's gate, and the explicit statement that
`kEditorSettingsVersion` stays 1 and the registry stays 64.

---

## Dependency graph

```
Task 0 (measure)
  ├─> Task 1 (pure algebra + preference test)      ─┐
  ├─> Task 2 (targeted Open through the gate)      ─┤
  │                                                 ├─> Task 5 (the menu surface)
  ├──────> Task 3 (recording policy) ──────────────┤        [needs 2, 3, 4]
  │           [needs 1]                             │
  └──────> Task 4 (load normalization) ────────────┘
              [needs 1, 3]
                                    Task 6 (non-interference) [needs 3, 4]
                                    Task 7 (registry diff)    [needs 1-6]
                                    Task 8 (full verification)[needs 1-7]
                                    Task 9 (docs + commit)    [needs 8]
```

MAR-181 shipped a case that could not complete because a later task had not
landed. The "Depends on" line at the head of every task above exists to stop that
recurring: **do not begin a task whose dependencies are not green.**
