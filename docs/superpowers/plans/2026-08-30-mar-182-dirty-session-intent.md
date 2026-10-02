# MAR-182 Unify Dirty-Session Intent Handling — Implementation Plan

Design: `docs/superpowers/specs/2026-08-30-mar-182-dirty-session-intent-design.md`
Base: `43572f0` (MAR-181) on `feat/mar-168`.

---

## Global constraints

- **Task 0 is mandatory and produces no code.** Every `file:line` in the design
  was read at `43572f0` without a build. Re-measure before touching anything;
  if a measurement disagrees, stop and report rather than adapting silently.
- **TDD.** Each task writes its case first, runs it, and **records the exact
  failure text**. A case that passes before the implementation is a case that
  cannot fail — the arc's most expensive recurring defect (MAR-178/179/180/181
  each shipped one, MAR-180 three, MAR-181's headline gate had to be replaced).
- **Never assert a project is good by parsing it.** `validate_project_for_save`
  takes no base document, so a passing `save()` proves nothing. Every "the
  project is fine" assertion goes through `marrow::editor::load_project` (or
  `reload_project`), which materializes the runtime. MAR-181 I6 measured
  `json::load_document` passing on a file `load_project` could not open.
- **Do not touch `resolve_choice`'s acceptance rule.** MAR-181's design §3.2
  ("`Choose` is disabled whenever the diagnostic is non-empty") contradicts its
  own §3.4 rule 5, and **§3.4 is what shipped**: `Choose` is gated on
  `FilePathChoice::acceptable` alone (`shell_file_paths.cpp:259-260`) and an
  existing Save target is accepted *with* a diagnostic
  (`:508-514`, asserted by C8 row 11 at `shell_smoke_project.cpp:1513-1515`).
  MAR-182's prompt-driven Save reaches this chooser over an existing file in the
  common case; "fixing" it toward §3.2 makes the prompt unresolvable. Task 0
  step 12 confirms this before any code is written.
- **Content-keyed, never provenance-keyed.** The gate reads `session.dirty()`;
  completion reads `!session.dirty()`. Do not introduce a "did the caller ask
  nicely" flag anywhere in this machine.
- **Registry is 64** (39 edit / 12 inspection / 10 management / 3 validation) and
  **does not change**. Prove by empty diff, never by substitution. `AGENTS.md`
  contains `t = 0.62` and "62 lines" (self-referential); `(51, 56, 64)` and
  `rgb(54,57,64)` are colours; `test_client.py:1484` is an ordinal. Never touch
  `IM_COL32(56, 61, 69, 255)`, `"x": 56.0`, `56,995,840`,
  `PhysicsBoneState … 56 bytes/bone`, `IM_COL32(208,134,57,230)`.
- **`~/Library/Application Support/Marrow` must not exist when you finish.**
  Check at Task 0 and again at the end. Set `MARROW_CONFIG_HOME` to a scratch
  directory for every `marrow_editor_shell` invocation.
- **Do not touch** `project.cpp`, `session.cpp`, `include/marrow/c/`, `src/c/`,
  `include/marrow/runtime/`, `agent_dispatch.cpp`, `agent_handlers_*.cpp`,
  `agent_dispatch_smoke.cpp`, `tools/`.

## File and responsibility map

| File | Change |
|---|---|
| `src/editor/shell_file_paths.hpp` | `SessionIntent`, `DirtyIntentPhase`, `DirtyIntentResponse`, `DirtyIntentRequest`, `kDirtyIntentModal`; `begin_session_intent`, `resolve_dirty_intent`, `absorb_close_request`; `FileAction::Reload`; **corrections to the two wrong comments** (design §12 A, B) |
| `src/editor/shell_file_paths.cpp` | `perform_session_intent`, `tick_dirty_intent`, `draw_dirty_intent_modal` (all file-internal except the three exported); `Reload` case in `begin_file_action` and `apply_pending_file_action`; the stale-request fix in `draw_path_chooser_modal` |
| `src/editor/shell_state.hpp` | `std::optional<DirtyIntentRequest> dirty_intent;` and `bool should_exit{false};` |
| `src/editor/shell_project_panels.hpp` | delete `ProjectMenuAction`; drop the `bool*` from the three drawer signatures |
| `src/editor/shell_project_panels.cpp` | four menu items and two icon buttons route to `begin_session_intent`; drop the out-params |
| `src/editor/window_host.hpp` | `+ cancel_close_request()`, `- request_close()` |
| `src/editor/sdl_window_host.cpp` | same, one line each |
| `src/editor/shell_main.cpp` | loop condition, `absorb_close_request`, delete the `reload_requested` local and its `if` |
| `src/editor/shell_smoke_frames.cpp` | delete the `reload_requested` local and its `if` |
| `src/editor/shell_smoke_project.cpp` | cases C12–C19 and their registration |
| `src/editor/shell_smoke_scenarios.hpp` | declarations for C12–C19 |
| `AGENTS.md` | verification line, manual close-request check, validation-results section |

No new file, no new CMake source line, no new dependency, no new CMake target.

---

## Task 0 — Measure the as-built tree (mandatory, no code)

**Every `file:line` in this plan and in the design is anchored to `43572f0`**,
the MAR-181 commit, and was verified against it. Line numbers move as soon as any
task lands. So Task 0 measures the **working tree**, and the two kinds of
disagreement are not the same thing:

- A **shift** — the same construct, a different line — is expected once a task
  has been applied. Re-anchor and continue.
- A **substantive** disagreement — the construct is absent, duplicated, or says
  something else — stops the story. Report it; do not adapt silently.

**Task 1 may already be applied.** At the time of writing, the working tree
already carried Task 1 in full: `FileAction::Reload`, the `Reload` case in
`begin_file_action`, the `Reload` branch in `apply_pending_file_action`, the
three drawer signatures without `bool*`, all three reload triggers routed to
`begin_file_action(state, FileAction::Reload)`, both frame bodies' `if
(reload_requested)` blocks deleted, and the two smoke call sites updated. Check
`git status` and `grep -rn "reload_requested" src/` **first**: no hits means Task
1 is done — verify it against Task 1's checklist, run Task 1's gate, and start at
Task 2. Do not re-apply it. Note that this shifts everything below
`shell_file_paths.cpp:626` by roughly +18 lines.

Run every command. Record every output verbatim.

```sh
# ---- 1. Registry. Design §7 claims 64 / 39-12-10-3.
sed -n '29,96p' src/editor/agent_dispatch.cpp | grep -c '^\s*{'
#   expect: 64
sed -n '29,96p' src/editor/agent_dispatch.cpp \
  | grep -oE '"(edit|inspection|management|validation)"' | sort | uniq -c
#   expect: 39 edit / 12 inspection / 10 management / 3 validation

# ---- 2. Count sites. 10 guards + 1 array + 2 python. NONE move in MAR-182.
grep -rn "!= 64U" src/editor/ src/samples/
#   expect EXACTLY 10, at shell_smoke_constraints.cpp:147,676;
#     shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603;
#     shell_smoke_timeline.cpp:3697
grep -rn "OperationExpectation, 64" src/samples/      # expect 1, :39
grep -n "== 64" tools/mcp/test_client.py              # expect 2, :53 and :55

# ---- 3. THE PREMISE (design §1.1). There are ZERO dirty checks.
grep -rn "dirty" src/editor/shell_file_paths.cpp
#   expect EXACTLY 2 hits: :575 update_project_dirty_state (after a SUCCESSFUL
#   save) and :696 a comment. If a gate already exists, §1.1 is wrong -- stop.

# ---- 4. Every discard path (design §1.2). All five must be where §1.2 says.
grep -n "begin_file_action\|reload_requested\|QuitRequested" src/editor/shell_project_panels.cpp
#   at 43572f0: :645 :702 :724 :727 :732 :736 :740 :744 :922 :929 :941 (+ the
#   :718 comment). AFTER Task 1 `reload_requested` is gone and the three former
#   reload sites read begin_file_action(state, FileAction::Reload) instead.
grep -n "close_requested_" src/editor/sdl_window_host.cpp
#   expect :68 :82 :108 :109 :174 :391  -- and NO clearer other than :68/:174
grep -rn "public EditorWindowHost" src/            # expect exactly 1: sdl_window_host.cpp:34
grep -rn "request_close" src/                      # expect 3: window_host.hpp:43,
#   sdl_window_host.cpp:109, shell_main.cpp:561. If more, §4.2's deletion widens.

# ---- 5. NOT discard paths. Both must hold or the scope grows.
sed -n '2001,2010p' src/editor/session.cpp
#   expect the four runtime fields assigned then `impl_->update_dirty();` and the
#   "Adoption replaces runtime SOURCES, never the authored project" comment.
sed -n '29,96p' src/editor/agent_dispatch.cpp | grep -E '"(open|create|reload|close|new)' 
#   expect NO HITS -- no agent operation replaces a session.

# ---- 6. The two dirty notions (design §1.3).
sed -n '1181,1184p' src/editor/session.cpp     # content-keyed serialize compare
sed -n '251,258p' src/editor/shell_core.cpp    # the shell cache
sed -n '570,574p' src/editor/shell_core.cpp    # expect :572 project_dirty = !project_is_clean
grep -rn "project_dirty" src/editor/shell_project_panels.cpp
#   expect EXACTLY 4 read sites: :656 :932 :1024 :1037. All display.

# ---- 7. saved_project_snapshot stays write-only (design §1.4).
grep -rn "saved_project_snapshot" src/
#   expect EXACTLY 4 lines: shell_state.hpp:854 (decl), shell_core.cpp:573,579,680
#   (writes). ZERO reads, before and after this story.

# ---- 8. The seam (design §1.5). All three must hold.
grep -rn "draw_file_path_modals" src/
#   expect the decl, the def, and TWO call sites -- BOTH inside draw_menu_bar and
#   MUTUALLY EXCLUSIVE, so exactly one runs per frame:
#     shell_project_panels.cpp:706   (the !BeginMainMenuBar early return; the
#                                     comment at :704-705 says why it must exist)
#     shell_project_panels.cpp:925   (after EndMainMenuBar)
#   This is what makes design §4.4's "no frame-body edit" true. A THIRD call site
#   anywhere, or a call outside draw_menu_bar, means the modal can draw twice --
#   stop. Do NOT "simplify" the two branches into one: the :706 branch is what
#   keeps an open prompt alive while the menu bar is clipped.
grep -rn "apply_pending_file_action" src/editor/shell_main.cpp src/editor/shell_smoke_frames.cpp
#   expect exactly one each: shell_main.cpp:619, shell_smoke_frames.cpp:131
grep -rn "begin_file_action" src/editor/ | grep -v shell_file_paths
#   expect 5 surfaces: shell_project_panels.cpp:724,727,732,736 and
#   shell_preview.cpp:222

# ---- 9. The reload plumbing MAR-182 retires (design §4.1). 8 sites.
grep -rn "reload_requested\|draw_menu_bar\|draw_project_window\|draw_shell_toolbar" src/
#   at 43572f0: exactly the 20 lines design §4.1 assumes, including the two smoke
#   callers at shell_smoke_project.cpp:2307-2308 and :2532-2533.
#   AFTER Task 1: ZERO `reload_requested` hits and every drawer call one argument
#   shorter. Either state is fine; anything in between means Task 1 is half
#   applied -- finish it before Task 2.

# ---- 10. The harness C19 reuses (design §9).
grep -n "imgui_internal.h" src/editor/shell_smoke_project.cpp        # expect :18
grep -n "ScopedRenameCallback\|SessionSnapshot\|capture_session_snapshot\|seed_shell_project_copy" \
    src/editor/shell_smoke_project.cpp | head
#   expect :714 :1571 :1588 :752 -- all four helpers exist and are file-internal
sed -n '2320,2400p' src/editor/shell_smoke_project.cpp
#   read C9's sweep/click_position harness end to end before writing C19.

# ---- 11. Baselines to beat. Record all five.
cmake --build build 2>&1 | tail -5
ctest --test-dir build -N | tail -3                       # expect 22
./build/marrow_agent_dispatch_smoke | grep -c "\[ OK \]"  # expect 408
MARROW_CONFIG_HOME=/tmp/mar182-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2 | tail -3
test -e "$HOME/Library/Application Support/Marrow" && echo "PREEXISTING" || echo "absent"
#   expect: absent

# ---- 12. The chooser's acceptance rule (design §4.5). MAR-181's design §3.2
#         and §3.4 CONTRADICT each other; §3.4 shipped. Confirm before Task 2,
#         because the prompt's Save reaches this chooser over an existing file.
sed -n '101,111p' src/editor/shell_file_paths.hpp     # `acceptable` is a SEPARATE flag
sed -n '259,260p' src/editor/shell_file_paths.cpp     # Choose gated on acceptable ONLY
sed -n '508,514p' src/editor/shell_file_paths.cpp     # existing file -> diagnostic AND acceptable
sed -n '1513,1515p' src/editor/shell_smoke_project.cpp # C8 row 11 asserts both at once
#   If Choose is gated on an empty diagnostic instead, MAR-181 §3.2 shipped and
#   C15 half B cannot pass -- stop and report, do not "fix" the chooser.

# ---- 13. TWO ImGui facts C19 must MEASURE, not assume (design §9).
#   (a) Does Escape close a modal opened with p_open == nullptr under this
#       harness? Write a throwaway probe inside C19's scaffold, print the answer,
#       and pick the close route from it.
#   (b) The AlwaysAutoResize first-frame stub rect. MAR-181 measured the chooser
#       at (452,259)-(468,296), 16x37 px one frame after opening. Re-measure for
#       kDirtyIntentModal and render settle frames before sweeping.
#   Read AGENTS.md "Headless Frame Smoke Notes" in full first.
```

---

## Task 1 — Retire `reload_requested`; Reload joins the deferred rail

**Depends on:** Task 0 only.

Pure refactor. Design §4.1. Behaviour must not change, so the gate is a **diff**,
not a new test — the ~40 existing smokes that already exercise these drawers are
a better witness than anything written today (this is MAR-181's Task 1 pattern).

### TDD

There is no new assertion. The gate is:

```sh
git diff --stat -- src/editor/shell_smoke_*.cpp src/samples/ src/tests/
#   MUST be empty except for the two mechanical call-site edits in
#   shell_smoke_project.cpp:2307-2308 and :2532-2533 (dropping an argument).
ctest --test-dir build --output-on-failure -L editor      # 12/12, unchanged
```

### Implementation

1. `shell_file_paths.hpp`: add `Reload` to `FileAction`; update the enum's doc
   comment from "four File-menu path workflows" to five, and state that `Reload`
   never participates in `FilePathMode`, `FilePathRequest` or
   `commit_path_choice` (only `seed_action_request` writes
   `FilePathRequest::action`, and only for `Open` and `SaveAs`).
2. `shell_file_paths.cpp`, `begin_file_action`: add the `Reload` case — reset
   `new_project_form` and `file_path_request`, arm `pending_file_application`
   with `action = Reload` and an **empty** `path` (`reload_project` reads
   `state->project_path` itself; a copy would be a second source of truth).
3. `shell_file_paths.cpp`, `apply_pending_file_action`: before the `is_open`
   block, `if (pending.action == FileAction::Reload) return reload_project(state);`.
4. `shell_project_panels.hpp` / `.cpp`: drop the `bool* reload_requested`
   parameter from `draw_menu_bar`, `draw_shell_toolbar`, `draw_project_window`.
   The three `*reload_requested = true` sites (`:645`, `:740`, `:941`) become
   `begin_file_action(state, FileAction::Reload);` — **temporarily**; Task 3
   moves them to `begin_session_intent`.
5. `shell_main.cpp`: delete the `bool reload_requested` local (`:558`) and the
   `if (reload_requested) { reload_project(shell_state); }` block (`:611-613`);
   update the three drawer calls.
6. `shell_smoke_frames.cpp`: delete the local (`:64`) and the
   `if (reload_requested && !reload_project(...))` block (`:125-128`); update the
   two drawer calls. **Note in the commit that this drops an abort-on-failed-
   reload that no smoke frame reaches** — a deliberate behaviour change, not an
   oversight.
7. `shell_smoke_project.cpp:2307-2308`, `:2532-2533`: drop the argument.

### Verify

```sh
cmake --build build
ctest --test-dir build --output-on-failure -L editor
git diff --stat -- src/editor/shell_smoke_*.cpp | cat      # only the 4 lines above
grep -rn "reload_requested" src/                           # expect NO HITS
grep -rn "ProjectMenuAction" src/                          # still present; Task 3 removes it
```

---

## Task 2 — The machine, UI-free (C12, C13, C14, C15, C16, C17)

**Depends on Task 1**, and not optionally: C12's clean-session row and C16 both
assert `pending_file_application->action == FileAction::Reload`, which does not
exist until Task 1 adds it. Attempting Task 2 first makes two cases uncompilable.

Design §3. This is the story's core and it is entirely testable without a frame.

### TDD — `marrow_editor_shell`, cases C12–C17

Write all six before any of §3's implementation. Each needs a dirty project; use
`seed_shell_project_copy` + `reload_project` + a `begin_edit` transaction that
appends to `editor_metadata.notes` (the C10 pattern at
`shell_smoke_project.cpp:2553-2568`), then assert `state.session.dirty()`.

**C12 — the gate.** Table-driven over the four `SessionIntent` values. For each,
on a **dirty** session assert: `dirty_intent` holds that intent with
`phase == Prompting`; `pending_file_application`, `new_project_form` and
`file_path_request` are all empty; `should_exit == false`; and
`capture_session_snapshot(state) == before`. Then `resolve_dirty_intent(Cancel)`
and repeat on a **clean** session, asserting the intent's *own* field instead:
New → `new_project_form.has_value()`; Open → `file_path_request->action == Open`;
Reload → `pending_file_application->action == Reload`; Quit → `should_exit`.
*Four different fields is the point — see design §9.*

**C13 — Save completes the intent.** Dirty session, `begin_session_intent(Open)`,
`resolve_dirty_intent(Save)`. Assert `!state.session.dirty()`; `dirty_intent`
empty; `file_path_request->action == FileAction::Open` (the intent was
performed); and **`marrow::editor::load_project(temp_project)` succeeds with a
non-null `skeleton_data`** — a passing `save()` proves nothing.

**C14 — Save failure never falls through.** Read the destination's bytes. Under
`ScopedRenameCallback` returning `std::errc::permission_denied` (the seam at
`shell_smoke_project.cpp:714-727`, used by C6 at `:995` and `:1991`), dirty
session, `begin_session_intent(Quit)`, `resolve_dirty_intent(Save)`. Assert:
`should_exit == false`; `dirty_intent->intent == Quit` and
`phase == DirtyIntentPhase::Prompting`; `session.dirty()`; `error_message`
non-empty; the destination **byte-identical** and `load_project`-able. Release
the seam (leave the RAII scope), `resolve_dirty_intent(Save)` again, assert
`should_exit == true` and the file reloads.

**C15 — save-path cancellation, then save-path completion over an existing
file.** Dirty session, then `state.project_path.clear()` — the documented guard
branch at `shell_file_paths.cpp:622-625`; say so in the case's comment so it does
not read as a hack. `begin_session_intent(Reload)`, `resolve_dirty_intent(Save)`.
Assert `dirty_intent->phase == AwaitingSave` and `file_path_request.has_value()`
with `action == SaveAs`.

*Half A — cancellation.* Simulate the chooser's Cancel
(`state.file_path_request.reset()`), call the same tick the frame calls, and
assert `phase == Prompting`, `intent == Reload`, `session.dirty()`,
`pending_file_application` empty. Then `resolve_dirty_intent(Cancel)` and assert
a bit-identical snapshot.

*Half B — completion over a file that already exists* (design §4.5; this is the
common case and the one MAR-181's contradictory §3.2 would break). Re-enter
`AwaitingSave`. Against a destination that **already exists**, assert
`resolve_choice(...)` returns `acceptable == true` **and**
`diagnostic == "Replaces the existing file."` — both, in one assertion pair, or
the case is blind to exactly the defect. Commit it via `apply_save_as(&state,
existing_target)`, clear `file_path_request` as `commit_path_choice` does, tick,
and assert the intent completed: `dirty_intent` empty,
`pending_file_application->action == FileAction::Reload`, and the overwritten
file **RELOADS** through `marrow::editor::load_project`.

*The tick must be reachable from the test.* Either export it as
`void tick_dirty_intent(ShellState*)` or have `resolve_dirty_intent` and the
modal both call it; exporting it is simpler and is what C15 drives.

**C16 — Discard performs without persisting.** Record the destination's bytes and
`capture_session_snapshot`. Dirty session, `begin_session_intent(Reload)`,
`resolve_dirty_intent(Discard)`. Assert: `pending_file_application->action ==
FileAction::Reload`; `session.dirty()` **still true**; the file **byte-identical**.
Then `apply_pending_file_action(&state)` returns true; assert
`!session.dirty()` and that the note appended earlier is **gone** from
`state.session.project()->editor_metadata.notes`. *Two independent comparisons —
bytes catch a secret save, the document catches a secret keep.*

**C17 — Cancel and repeats.** Dirty session. `begin_session_intent(New)` twice →
one intent, still `New`, `phase == Prompting`. `begin_session_intent(Quit)` →
`intent == Quit` (last wish wins). `resolve_dirty_intent(Cancel)` →
`dirty_intent` empty, snapshot bit-identical, `should_exit == false`, nothing
armed. `begin_session_intent(New)` again → arms cleanly (AC3's deterministic
retry). Then drive to `AwaitingSave` (as C15) and assert
`begin_session_intent(Quit)` leaves `phase` and `intent` unchanged.

Register all six in `validate_shell_foundation_smoke`
(`shell_smoke_project.cpp:2954-2985`), after the MAR-181 block and **before**
`validate_mar181_arm_deferred_action_for_frame_body` at `:2979` — that call arms
C11's deferred action and nothing may run between it and the frames.

**Run them now.** All six must fail to compile (the symbols do not exist).
Record the errors. That is the "must fail first" evidence for this task.

### Implementation

Design §3, in `shell_file_paths.{hpp,cpp}`:

1. Header: the four types, `kDirtyIntentModal`, and the three exported functions
   plus `tick_dirty_intent`. **Correct the two wrong comments now**
   (`shell_file_paths.hpp:158-166` and `:170-177`, design §12 A and B) — leaving
   them to contradict the code is how the next story inherits a false premise.
2. `shell_state.hpp`: `std::optional<DirtyIntentRequest> dirty_intent;` and
   `bool should_exit{false};`, beside the MAR-181 block at `:860-862`, with the
   comment explaining that `should_exit` is the main loop's only exit condition.
3. `shell_file_paths.cpp`: file-internal `perform_session_intent`; exported
   `begin_session_intent`, `resolve_dirty_intent`, `tick_dirty_intent`. Follow
   the pseudocode in design §3.2/§3.3 exactly — in particular
   `perform_session_intent` must be called from **exactly two** places, and
   `tick_dirty_intent`'s three branches must be in the design's order
   (`!dirty` → `file_path_request` → fall back to `Prompting`).

### Verify

```sh
cmake --build build
MARROW_CONFIG_HOME=/tmp/mar182-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2
#   expect C12..C17 lines then "Headless editor shell smoke rendered 2 frame(s)."
ctest --test-dir build --output-on-failure -L editor
grep -rn "saved_project_snapshot" src/ | wc -l          # still 4
grep -rn "project_dirty" src/editor/shell_file_paths.cpp # unchanged from Task 0
```

### Inversions for this task

Apply, build, run, record, revert, and byte-compare the tree afterwards.

- **I1** — drop the `session.dirty()` test from `begin_session_intent`. Expect
  **C12 only**.
- **I2** — gate on `state->project_dirty` instead. Expect **C12 and C16**; C16 is
  the interesting one, because it dirties through a transaction without calling
  `update_project_dirty_state`, so the cache is stale and the gate opens. This
  inversion *is* the design's §1.3 argument, executed.
- **I3** — call `perform_session_intent` before the dirty check in the Save path.
  Expect **C13 and C14**, C14 sharply (`should_exit` true with the file
  unwritten).
- **I4** — key completion on `save_project_file`'s return instead of
  `!session.dirty()`. Expect **C15** (the Save As branch has no return to read,
  so `AwaitingSave` resolves immediately).
- **I5** — make `Discard` save first. Expect **C16 only**, on the byte compare.
- **I6** — make `Cancel` reset the preview. Expect **C17 only**, on the snapshot.
- **I7** — make `begin_session_intent` stack instead of replace. Expect **C17
  only**, on `intent == Quit`.

If any inversion does not bite, **strengthen the case; never weaken the gate.**
MAR-181's I9 predicted two failures and produced zero, and the answer was a new
case (C11), not a softened assertion.

---

## Task 3 — Quit, native close, and the menu wiring (C18)

**Depends on Task 2** — C18 calls `resolve_dirty_intent(Discard)`.

Design §4.2.

### TDD — `marrow_editor_shell`, case C18

`validate_mar182_close_request_smoke(const ShellState& source_state)`, UI-free.

Clean seeded project: `absorb_close_request(&state, true)` returns **false** and
`state.should_exit == true`. Fresh state, dirtied: `absorb_close_request(&state,
true)` returns **true**, `should_exit == false`, `dirty_intent->intent ==
SessionIntent::Quit`. Call it again → still **true**, still one intent.
`resolve_dirty_intent(&state, Discard)` → `should_exit == true`. Call it again →
**false** (a confirmed exit passes through; anything else deadlocks the loop).
And `absorb_close_request(&state, false)` → **false** with no intent armed.

Run it. It must fail to compile. Record the error.

### Implementation

1. `window_host.hpp`: add `virtual void cancel_close_request() noexcept = 0;`,
   **delete** `virtual void request_close() noexcept = 0;`.
2. `sdl_window_host.cpp:109`: replace `request_close` with
   `void cancel_close_request() noexcept override { close_requested_ = false; }`.
3. `shell_file_paths.{hpp,cpp}`: `absorb_close_request`, per design §4.2(c).
4. `shell_project_panels.cpp:743-745`: the `Quit` item calls
   `begin_session_intent(state, SessionIntent::Quit);` and returns nothing.
   Delete `ProjectMenuAction` from `shell_project_panels.hpp:7-10` and make
   `draw_menu_bar` return `void`.
5. `shell_project_panels.cpp:724,727` and the three former reload sites
   (`:645,:740,:941`, now calling `begin_file_action(…::Reload)` from Task 1)
   become `begin_session_intent(state, SessionIntent::New | Open | Reload)`.
   **`Save` (`:732`) and `Save As` (`:736`) keep calling `begin_file_action` and
   must not be routed through the machine** — Save is the resolution; gating it
   deadlocks.
6. `shell_preview.cpp:222` (`Ctrl+S`) is **unchanged**, for the same reason. Its
   existing comment at `:217-220` already says so and is now correct.
7. `shell_main.cpp`: loop condition → `while (!shell_state.should_exit)`; after
   `poll_events`, `if (absorb_close_request(&shell_state,
   window_host->should_close())) { window_host->cancel_close_request(); }`;
   delete the `draw_menu_bar(...) == QuitRequested` block at `:559-562`.

### Verify

```sh
cmake --build build          # the request_close deletion is compiler-enforced
grep -rn "request_close" src/            # expect ONLY cancel_close_request hits
grep -rn "ProjectMenuAction" src/        # expect NO HITS
grep -rn "should_close()" src/editor/shell_main.cpp
#   expect exactly ONE hit, inside the absorb_close_request call
grep -n "while (!" src/editor/shell_main.cpp
#   expect the loop to test shell_state.should_exit and nothing else
MARROW_CONFIG_HOME=/tmp/mar182-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2
ctest --test-dir build --output-on-failure
```

### What this task's detectors do and do not catch

Say it in the commit and in `AGENTS.md`, not only here:

| Line | Detector | Half caught |
|---|---|---|
| `absorb_close_request`'s decision | **C18** | fully, both polarities |
| the *call* to it in `shell_main.cpp`'s loop | **none** | manual check only (design R1) |
| `cancel_close_request()` on the host | **none** | manual check only |
| the menu's `Quit` item routing | **C19** (Task 4) | both frame bodies, via `draw_menu_bar` |

The middle two are unreachable from any headless test: `run_headless_smoke`
returns before a window host exists. They are backstopped structurally —
`should_exit` is the loop's only exit condition, so omitting the call makes the
editor unclosable, and `request_close()` is deleted so the old bypass cannot be
reached by accident — and by the manual check in Task 5. Do not claim they are
tested.

This sits next to a pre-existing gap you should know about and must **not** try
to fix here: MAR-181's C11 catches deleting `apply_pending_file_action` from the
**smoke's** frame body and is blind to deleting it from `shell_main.cpp` — the
exact inverse of the inversion it was built for. Closing it needs the two
hand-maintained frame bodies unified, which is out of scope. MAR-182 adds no line
to either body (its only frame-body edits are Task 1's deletions), so it neither
widens nor closes that gap.

### Inversions

- **I8** — make `absorb_close_request` return `false` on a dirty project. Expect
  **C18 only**; the return value is asserted directly in both polarities.
- **I8b** — make it return `true` even after `should_exit`. Expect **C18 only**,
  on the pass-through clause. (Without this clause the editor can never close.)

---

## Task 4 — The prompt, the stale-request fix, and the mouse (C19)

**Depends on Task 3**, and not optionally: C19 clicks `Reload Project` in the real
menu and expects a `Quit`-capable machine behind it, so the menu items must
already be routed to `begin_session_intent`. MAR-181's plan shipped a case (its
C10) that could not complete before a later task; do not repeat it — if C19 will
not run at this point, the ordering is wrong, not the case.

Design §4.3, §4.4. **Read `AGENTS.md` "Headless Frame Smoke Notes" in full
before writing a line of this case.**

### TDD — `marrow_editor_shell`, case C19

`validate_mar182_dirty_prompt_mouse_smoke(const std::filesystem::path&)`,
modelled directly on C9 (`shell_smoke_project.cpp:2320-2485`) — reuse its
`sweep`, `click_position` and `MenuProbe` scaffolding, its
`io.ConfigMacOSXBehaviors = false`, and its three-settle-frame rule for
`AlwaysAutoResize` rects.

Its `render_frame` must call, in order: `ImGui::NewFrame()`,
`handle_project_history_shortcuts`, `draw_menu_bar` (which reaches
`draw_file_path_modals` at `shell_project_panels.cpp:925`), `ImGui::Render()`,
then `apply_pending_file_action` — the frame body in miniature.

**Phase 1 — the prompt exists and the menu reaches it.** Dirty the project.
Click `File`; measure the menu popup's window name (MAR-181 measured
`File###Menu_00`, but measure again). Click `Reload Project`. Assert
`ImGui::FindWindowByName(kDirtyIntentModal)` is non-null and `Active`, and
`state.dirty_intent->intent == SessionIntent::Reload`. Sweep the modal for
`Save`, `Discard` and `Cancel` — **all three**, because a missing button is
indistinguishable from a broken id seed unless you also find one that exists.

**Phase 2 — Cancel.** `capture_session_snapshot` before; click `Cancel`; two
settle frames (`CloseCurrentPopup` only takes effect on the next `NewFrame`, per
C9's comment at `:2455-2456`); assert the modal is inactive, `dirty_intent` is
empty, and the snapshot is bit-identical.

**Phase 3 — external close of the prompt (AC5 "modal close").** Re-raise via
File > Reload Project, then close the popup by whichever route Task 0 step 12(a)
measured (Escape if it works; otherwise `imgui_internal.h`, already included at
`:18`). Assert `dirty_intent` is empty afterwards.

**Phase 4 — external close of the chooser (§4.3, C15's precondition).**
`begin_file_action(&state, FileAction::SaveAs)`, settle frames, confirm
`kFilePathModal` is Active, close it externally, settle, assert
**`!state.file_path_request.has_value()`** — not merely that the popup is gone.

Run it. It must fail: `kDirtyIntentModal` does not exist yet. Record the text.

### Implementation

1. `shell_file_paths.cpp`: `draw_dirty_intent_modal(ShellState*) -> bool`,
   file-internal. Runs `tick_dirty_intent` first, returns `false` unless
   `dirty_intent && phase == Prompting`, latches `opened` + `OpenPopup` exactly
   as `draw_path_chooser_modal` does, and — if `opened && !BeginPopupModal(...)`
   — treats the close as `Cancel` and returns `false`. Body: the project path,
   one line naming the intent's consequence, a separator, and three plain-label
   buttons calling `resolve_dirty_intent`.
2. `draw_file_path_modals`: `if (draw_dirty_intent_modal(state)) return;` as the
   first statement after the null check, ahead of the `new_project_form` branch
   (design §4.3). **This is the only wiring needed** — the function has one call
   site and both frame bodies reach it.
3. `draw_path_chooser_modal`: the stale-request fix (design §4.3) — when
   `opened` is already true and `BeginPopupModal` returns false, clear
   `file_path_request` before returning.
4. Correct the comment above `draw_file_path_modals(state)` in
   `shell_project_panels.cpp` (at `43572f0`, `:923-924`): it says "One call
   site", and there are **two** — this one and the `!BeginMainMenuBar()` early
   return at `:706`. Both are inside `draw_menu_bar` and mutually exclusive, so
   the *claim it is making* (both frame bodies reach the modals through one
   function) is right while the sentence is wrong. Leaving it is how the next
   story inherits a false premise.

### Verify

```sh
cmake --build build
MARROW_CONFIG_HOME=/tmp/mar182-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2
grep -rn "draw_file_path_modals" src/       # STILL exactly the two Task 0 sites
#   shell_project_panels.cpp:706 and :925, both inside draw_menu_bar. A third is
#   a double-draw; a call from a frame body is the hazard §4.4 exists to avoid.
grep -rn "draw_dirty_intent_modal" src/     # file-internal: def + 1 call, no header
git diff --stat -- src/editor/shell_main.cpp src/editor/shell_smoke_frames.cpp
#   the frame-body deltas must be Task 1's DELETIONS and Task 3's loop change only
```

### Inversions

- **I9** — restore the menu's `Quit` item to `ProjectMenuAction::QuitRequested`
  and re-add `shell_main.cpp`'s handler. **Expect C19 to fail. If it passes, the
  gate is wrong, not the code** — this is MAR-181's I9 repeating, and the answer
  is a stronger case (probe the `Quit` item and assert the dirty click arms a
  `Quit` intent rather than exiting), never a softer assertion. Note that C12–C18
  will all pass under this inversion, by construction: they drive the seams
  directly.
- **I10** — skip the stale-request fix. Expect **C15 and C19 phase 4**.
- **I11** — draw the prompt from `shell_main.cpp` instead of
  `draw_file_path_modals`. Expect **C19 to fail** (the smoke's `render_frame`
  never calls `shell_main.cpp`), which is the duplicate-frame-body hazard caught
  at the layer that owns it.

---

## Task 5 — Documentation and the registry proof

1. `AGENTS.md` verification list: extend the MAR-181 line at `:48` or add one
   beside it — "Unified dirty-session intent: the Save/Discard/Cancel machine in
   front of New/Open/Reload/Quit/OS-close, save failure, save-path cancellation,
   repeated requests, modal close, and the mouse-driven prompt (C12-C19):
   `MARROW_CONFIG_HOME=… ./build/marrow_editor_shell --project
   assets/fixtures/player_idle.marrow --auto-close 2`".
2. `AGENTS.md` **manual** check, in the style of the macOS launch-focus note at
   `:172` — design R1: "MAR-182 window-close veto: `shell_main.cpp`'s two lines
   of loop glue are not reachable from any headless test. On an interactive host,
   dirty a project, click the window's close button, confirm the Unsaved Changes
   prompt appears and Cancel leaves the window open."
3. `AGENTS.md` validation-results section, matching the MAR-180/181 sections:
   what was measured before any code, the result table, the inversion table with
   **actual** outcomes (not predictions), the corrections from design §12
   (**five**, including MAR-181's §3.2/§3.4 contradiction), and a **"what is not
   covered"** subsection reproducing Task 3's detector table verbatim — including
   the pre-existing C11 inverse gap, stated as inherited and not addressed.
4. Registry proof — run and paste:

```sh
git diff --stat -- src/editor/agent_dispatch.cpp src/editor/agent_handlers_*.cpp \
                   src/samples/agent_dispatch_smoke.cpp tools/     # MUST be empty
git diff -U0 -- src/ tools/ CMakeLists.txt | grep -E '^[+-]' | grep -E '\b(62|63|64|65)\b'
#   Every surviving line must be explained. Expect frame deltas (1.0f / 60.0f)
#   and nothing else.
sed -n '29,96p' src/editor/agent_dispatch.cpp | grep -c '^\s*{'        # 64
grep -rn "!= 64U" src/editor/ src/samples/ | wc -l                     # 10
grep -rn "OperationExpectation, 64" src/samples/ | wc -l               # 1
grep -n "== 64" tools/mcp/test_client.py | wc -l                       # 2
```

---

## Full verification checklist

```sh
# --- Build
cmake --build build
cmake --build build --target marrow_verify_third_party      # no new dependency
cmake --build build --target marrow_constraint_warning_check

# --- The story's own coverage (C12-C19)
MARROW_CONFIG_HOME=/tmp/mar182-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2
MARROW_CONFIG_HOME=/tmp/mar182-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/parameter_face_basic.marrow --auto-close 2

# --- Registry unchanged at 64, proved by diff (Task 5)
git diff --stat -- src/editor/agent_dispatch.cpp src/editor/agent_handlers_*.cpp \
                   src/samples/agent_dispatch_smoke.cpp tools/       # empty
./build/marrow_agent_dispatch_smoke | grep -c "\[ OK \]"             # 408, unchanged
MARROW_CONFIG_HOME=/tmp/mar182-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py                   # 64/64 name parity
python3 -m py_compile tools/mcp/server.py tools/mcp/test_client.py \
                      tools/mcp/tools/editing.py tools/mcp/tools/inspection.py

# --- No format / ABI / schema change
git diff --stat -- include/marrow/c/ src/c/ include/marrow/runtime/ \
                   src/editor/project.cpp src/editor/session.cpp \
                   src/editor/atomic_file_write.cpp docs/root1/format-spec.md   # empty

# --- Structural invariants this story must not break
grep -rn "saved_project_snapshot" src/ | wc -l          # 4 (1 decl, 3 writes, 0 reads)
grep -rn "draw_file_path_modals(state)" src/            # exactly 2, both in
                                                        # draw_menu_bar (:706, :925)
grep -rn "apply_pending_file_action" src/editor/shell_main.cpp \
                                     src/editor/shell_smoke_frames.cpp   # one each
grep -rn "reload_requested\|ProjectMenuAction\|request_close" src/       # NO HITS
grep -rn "project_dirty" src/editor/shell_file_paths.cpp                 # unchanged
grep -n "while (!" src/editor/shell_main.cpp            # tests should_exit only

# --- Suites
ctest --test-dir build -N                               # still 22
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -L editor    # 12/12
ctest --test-dir build --output-on-failure -L runtime   # 4/4
./build/marrow_project_smoke assets/fixtures/player_idle.marrow

# --- Preference isolation (must be true before AND after)
test -e "$HOME/Library/Application Support/Marrow" && echo "LEAKED" || echo "absent"

# --- Inversions: I1-I11 applied, built, run, recorded, reverted, byte-compared.
#     I9 in particular MUST bite. If it does not, add coverage; do not relax C19.
```

### Manual, on an interactive host (design R1)

1. Dirty a project, then **File > Quit**: the prompt appears; `Cancel` leaves the
   editor running; `Discard` exits; `Save` writes and exits.
2. Dirty a project, then click the **window's close button**: same prompt; the
   window **stays open** on `Cancel`.
3. Dirty a project, then **File > Reload Project**: the prompt appears; `Save`
   writes and then reloads.
4. On a **clean** project, all four of New / Open / Reload / Quit proceed with no
   prompt at all.
