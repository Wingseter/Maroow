# MAR-190 Add the PSD Reimport Review UI — Implementation Plan

Design: `docs/superpowers/specs/2026-09-01-mar-190-psd-reimport-review-ui-design.md`.
Date: 2026-09-01. Written against `cf6a199`; **HEAD moved to `23b326e` during
writing** and every command below was re-checked against it. Task 0 re-derives at
whatever HEAD is then, and **never pins a SHA inside a step that will be executed
later** — name the file and say "current HEAD".

> **Task 0 is COMPLETE. Superseded header below; see §0.9 for the record.**
> MAR-189 landed at `7462f67`, plus a harness follow-up at `ac82f3c`. All four
> blocking deliverables are present and the story is unblocked. Nine claims in
> this plan and its design did not survive re-derivation — they are listed in
> design §0.6 and repaired at their own sites. **`ctest -N` is 23, not 22.**

**MAR-189 is not implemented.** *(Historical — true when written.)* Every task
below that names `commit_psd_reimport`, `PsdCommitStep`, `kAllCommitSteps`,
`staged_texture_path` or the failpoint seam is **blocked** until it is, and Task 0
is where that is discovered rather than at Task 5. Design §0.5 lists the six
deliverables; items 1–4 are blocking.

---

## §0. Task 0 — measure before writing a line

Nothing in Tasks 1–10 may proceed on an unmeasured premise. Task 0 is
**blocking**: a citation that no longer names its construct is a finding for the
errors table, never a silent adjustment.

### 0.1 Isolation

```
AGENT=plan-mar190                      # or the implementer's own agent name
SCRATCH=/private/tmp/claude-501/-Users-kwon-Workspace-C-Maroow/<session>/scratchpad/$AGENT
rm -rf "$SCRATCH"                      # discard anything you did not create
mkdir -p "$SCRATCH/tree" && cd /Users/kwon/Workspace/C/Maroow
git archive HEAD | tar -x -C "$SCRATCH/tree"
cmake -S "$SCRATCH/tree" -B "$SCRATCH/build" && cmake --build "$SCRATCH/build" -j8
```

Named after the **agent**, never after the story: every agent working MAR-190
reaches for `t190` and that is exactly why they collide. `git archive | tar -x`
**overlays**; if a directory you did not create is there, `rm -rf` and re-extract.
The shared worktree mutates and restores tracked files while reviews are live, so
**every** verification build in this plan runs from `$SCRATCH/tree`, overlaid with
only MAR-190's own files.

### 0.2 Blocking: does MAR-189 exist?

```
grep -rn "commit_psd_reimport\|PsdReimportCommitResult\|PsdCommitStep\|kAllCommitSteps" \
     "$SCRATCH/tree/src" "$SCRATCH/tree/include"
grep -rn "staged_texture_path\|staged_atlas_filename" "$SCRATCH/tree/include/marrow/editor/psd_reimport_plan.hpp"
ls "$SCRATCH/tree/src/editor/psd_reimport_commit_internal.hpp"
grep -rn "psd_plan_digest" "$SCRATCH/tree/include" "$SCRATCH/tree/src"
```

Record each as present/absent with its file:line. **Absent items 1–4 of design
§0.5 stop the story**; report and wait. Item 5 absent selects §2.5's fallback and
is recorded as a known weakness, not a blocker.

### 0.3 Baselines, at the commit's actual parent

| # | Measure | Command |
|---|---|---|
| B1 | Clean all-target warning count | `find "$SCRATCH/build/CMakeFiles" -name '*.o' -delete && cmake --build "$SCRATCH/build" -j8 2>&1 \| grep -c warning:` |
| B2 | `ctest -N` count | `ctest --test-dir "$SCRATCH/build" -N \| tail -1` — **measured 23** (MAR-189 registered `marrow.psd_import_smoke`); `grep -c 'add_test('` says **26**, three of them display-gated |
| B3 | `ctest` result | `ctest --test-dir "$SCRATCH/build" --output-on-failure` |
| B4 | Agent smoke `[ OK ]` count | `"$SCRATCH/build/marrow_agent_dispatch_smoke" \| grep -c '\[ OK \]'` |
| B5 | Registry / guards | `grep -c '^    {"' src/editor/agent_dispatch.cpp` (66); `grep -rn '!= 66U' src/editor src/samples \| wc -l` (11, split 7/2/2); `grep -n 'std::array<OperationExpectation' src/samples/agent_dispatch_smoke.cpp` (`:42`); `grep -n '== 66' tools/mcp/test_client.py` (`:53,55`) |
| B6 | **Frame-body gate set, verbatim** | `cmake --build "$SCRATCH/build" --target marrow_frame_body_check 2>&1 \| tee "$SCRATCH/framebodies.baseline"` |
| B7 | PSD smoke green | `"$SCRATCH/build/marrow_psd_import_smoke" assets/fixtures/psd_import_sample.psd assets/fixtures/psd_import_sample_reimport.psd` from the repo root |
| B8 | Shell smoke green | `"$SCRATCH/build/marrow_editor_shell" --project assets/fixtures/player_idle.marrow --auto-close 5` from the repo root |
| B9 | Tracked-fixture byte map | `shasum -a 256` over `assets/fixtures/` `psd_import_sample.psd`, `psd_import_sample_reimport.psd`, `player_idle.marrow`, `player_idle.mskl`, `player_idle.matl`, `player_idle.mbin`, **`player_fixture.png`** (NOT `player_idle.png` — it does not exist), into `"$SCRATCH/fixtures.baseline"` with **absolute** paths. **Assert each path exists before hashing** |

### 0.4 Blocking: re-derive every citation this plan and the design make

By **symbol**, against `git show HEAD:<path>` and not the worktree. At minimum:
`CheckFrameBodies.cmake` regex line; `shell_project_panels.cpp`'s
`draw_project_window` / `draw_menu_bar` / `draw_file_path_modals` sites;
`shell_problems.cpp`'s `Selectable` width block; `shell_state.hpp`'s window-title
constants; `session.hpp`'s `adopt_runtime_sources` doc; `imgui.cpp:13081-13111`
and `:14844-14875`; `project.hpp:596-626,657`; `psd_import.cpp:734,814-826`.

### 0.5 A8 — the shell fixture is buildable

Write a throwaway probe (deleted before Task 1) that copies
`assets/fixtures/psd_import_sample.psd` into a temp directory, runs
`import_psd_to_runtime_bundle`, and prints the four produced artefacts and
`PsdImportResult::texture_path`. **If this fails, the entire shell surface
(W1, N1–N5, F1, F2) is blocked** and that is the finding to report.

### 0.6 A10 — Escape does not close a modal

Probe: open any existing modal in a headless frame, press Escape, print
`FindWindowByName(title)->Active`. Confirms `imgui.cpp:14873` empirically before
§2.6's `p_open` decision is built on it.

### 0.7 A9 — which `rebuild.sh` did the archive give you?

```
grep -cF 'find "${build_dir}/CMakeFiles"' "$SCRATCH/tree/tools/inversion/rebuild.sh"
```

**`-F` is load-bearing.** Without it BSD `grep` reads `{build_dir}` as a malformed
BRE interval and reports **0 on a file that plainly contains the string** — which
is the same class as `AGENTS.md`'s *"no hits is evidence only once the command is
known to have executed."* Measured while writing this plan: the same pattern
returns `0` without `-F` and `1` with it, against the committed `23b326e` file.

**`0` (with `-F`) means the pre-`23b326e` version**: `invert.sh` deletes no
objects and the build falls back to make's mtime comparison. In that case every
`invert.sh` invocation in §B passes explicit object paths, e.g.
`CMakeFiles/marrow_editor.dir/src/editor/psd_reimport_review.cpp.o`. Do **not**
edit shared tooling mid-flight.

### 0.8 Snapshot for the inversion harness

```
tools/inversion/snapshot.sh "$SCRATCH/build" <every file §B mutates>
```

`invert.sh` refuses a stale baseline (`cmp` guard) and aborts if a mutation fails
to build — both guards are real and wired. Re-snapshot after **every** task that
edits a file §B will mutate, or the guard fires and the inversion is lost.

**Deliverable:** a Task 0 record with every measurement, and an errors table row
for every citation that did not resolve.

### 0.9 Task 0 record — measured at `ac82f3c`

Isolation: `$SCRATCH = …/scratchpad/impl-mar190`, named after the agent. Tree
verified byte-identical to `HEAD` by `diff -r` against a fresh
`git archive HEAD`, with a positive control (a byte appended to
`THIRD_PARTY.md`, `cmp` confirmed it saw the difference, restored, re-verified by
**absolute path**).

**The tip moved twice**: `7462f67` → `ac82f3c`, the second a **new commit, not an
amendment**. It touches `AGENTS.md`, MAR-189's design and
`tools/inversion/{README.md,invert.sh}` only — **no source, no CMake, no
assets** — so baselines measured on the `7462f67` tree remain valid. `ac82f3c`
lands GUARD 3: `invert.sh`'s restore now exits non-zero and prints absolute paths
on both sides, so restores no longer fail silently. **Re-verify the tip again at
commit time** and re-measure every baseline at the commit's actual parent.

| # | Measure | Result |
|---|---|---|
| B1 | Clean-build warnings | **0**, build exit 0 |
| B2 | `ctest -N` | **23** — MAR-189 registered `marrow.psd_import_smoke`. **Not 22** |
| B3 | `ctest` | **100% passed, 0 failed out of 23** |
| B4 | Agent smoke `[ OK ]` | **437** |
| B5 | Registry / guards | registry **66**; eleven `!= 66U` split **7** graph / **2** constraints / **2** timeline; `agent_dispatch_smoke.cpp:42`; `test_client.py:53,55` — all five hold |
| B6 | Frame-body gate | captured to `$SCRATCH/framebodies.baseline`; the ten windows design §2.9 predicted, allowlist `draw_agent_window`, sets agree |
| B7 | PSD smoke | green |
| B8 | Shell smoke | green |
| B9 | Fixture byte map | captured — **corrected file list**, see below |
| A8 | Shell fixture buildable | **PASS** — probe copied the tracked PSD to temp, ran `import_psd_to_runtime_bundle`, produced all four artefacts plus 3 layer files. W1/N1–N5/F1/F2 unblocked |
| A9 | `rebuild.sh` | post-`23b326e` form; the `-F` claim reproduces exactly (**1** with, **0** without), so no explicit object paths are needed in §B |
| A10 | Escape vs modal | `imgui.cpp:13103`, `:13104`, `:13105-13109`, `:14844`, `:14873` all re-resolve exactly. Asserted empirically in **F1**, per design A10 |
| A11 | `git status` | only MAR-190's and MAR-191's docs, `build-specrev187/`, and an in-flight ` M src/samples/psd_import_smoke.cpp` |

**S-gates today**, all red as required: **S1 = 0** (positive control:
`draw_project_window(` = 4, so the pattern matches something). S3/S4/S5/S6 red by
file absence. **S2 = 17 and is broken as worded** — rewritten in Task 9.

**B9's file list was wrong**: `player_idle.png` **does not exist**; the texture is
`player_fixture.png`, and `player_idle.mbin` was omitted. The corrected map covers
`psd_import_sample.psd`, `psd_import_sample_reimport.psd`, `player_idle.marrow`,
`player_idle.mskl`, `player_idle.matl`, `player_idle.mbin`, `player_fixture.png`.
**W1 asserts each path exists before hashing** (design §6.2) — a hash sweep over a
nonexistent file reports success while checking nothing.

**`grep -c` exits 1 when the count is zero**, so `cmake --build … | grep -c
warning:` reports a *failed command* on a clean build. Capture the build's own
exit status, never the counting pipeline's. Third distinct mechanism for the
zero-result rule, after zsh glob expansion and the malformed BRE interval.

**Errors table: nine substantive findings** — recorded in design **§0.6** (T1–T9)
and repaired at each site, plus the line-drift list. The headline is **T1**:
`preserve` governs the stored provenance row, **not** the slot, so the checkbox is
labelled `Forget mapping` and D1/D2 assert `import_sources->psd->layers` rather
than the committed skeleton's slots.

---

## §1. Tasks

Each task lists its exact files and ends with a build from `$SCRATCH/tree`. No
task is complete while a case it introduces has not been **watched fail** and
then watched pass.

### Task 1 — the review model's data and grouping

**Files:** `include/marrow/editor/psd_reimport_review.hpp` (new),
`src/editor/psd_reimport_review.cpp` (new), `CMakeLists.txt` (one source line
into `marrow_editor`).

`PsdReviewSections`, `group_psd_review`, `PsdReimportReview`,
`set_psd_review_deletion`, `chosen_psd_deletions`, `psd_review_can_confirm`.
Deletion choices are an **absence-defaulted set** (design §2.2), never a
`vector<bool>`.

**Cases:** V1, V2 in `src/samples/psd_import_smoke.cpp`, in a new
`validate_mar190_reimport_review(const std::filesystem::path& scratch)` called
from `main` **after** MAR-188's `validate_mar188_reimport_planning`, so a
synthesiser regression is still attributed to Q0.

**Falsifiability:** both are compile-red before this task (they call functions
that do not exist). Record the pre-implementation state per design §5.2 rule 6.

**Verify:** `marrow_psd_import_smoke` green; `grep -c "ImGui" src/editor/psd_reimport_review.cpp` = 0.

### Task 2 — the commit-plan derivation, by value

**Files:** `psd_reimport_review.{hpp,cpp}`.

`build_psd_commit_plan(const PsdReimportReview&) -> PsdReimportPlan`. Copies,
sets `preserve = false` on exactly the chosen identities, leaves everything else
at MAR-188's `true`. **Returns by value** — design §2.3; in-place mutation makes
I3 a provable no-op.

**Cases:** D1, D2 (the `preserve`-list halves; their commit halves land in Task 5).

**Verify:** D1's full `(identity, preserve)` list is asserted as an **ordered
list**, not a count of `false`s.

### Task 3 — the digest, or the fallback

**Files:** `psd_reimport_review.{hpp,cpp}`.

If Task 0.2 found `psd_plan_digest` header-declared, **use it** and add nothing.
Otherwise define it here over the ten-field row tuple (design §2.5) with a comment
naming MAR-189's copy as the site that must be kept in agreement, and record the
duplication in `AGENTS.md`'s "Not independently covered".

**Cases:** none of its own — V3 and N3 are its detectors, and I5 is what proves it
is not a stub.

### Task 4 — `apply_psd_reimport_review`, the one call site

**Files:** `psd_reimport_review.{hpp,cpp}`.

The five ordered steps of design §2.4, including `remove_all(restage_root)` on
**every** exit path. `PsdReviewOutcome`'s status text is one exhaustive `switch`
with no `default:`.

**Cases:** V3 (stale, PSD side), V4 (planning failure), plus
`expect_reimport_no_op` itself — written **once** in `psd_import_smoke.cpp` with
all six clauses (design §5.3) before any case calls it.

**Verify:** `grep -rn "commit_psd_reimport(" src/ | grep -v psd_reimport_commit`
returns **exactly one** line, in `psd_reimport_review.cpp`.

### Task 5 — the commit path (**blocked on MAR-189**)

**Files:** `psd_reimport_review.cpp`; `src/samples/psd_import_smoke.cpp`.

Wire step 4 to `commit_psd_reimport`. Cases V5, V6, V7, V8, plus D1's and D2's
commit halves.

- **V6** uses design §2.10's construction — serialize before, overwrite the after
  project's `import_sources` with the before value, re-serialize, require **byte
  equality**. Both strings produced **in memory**; never through a file
  (`AGENTS.md`: `serialize_project` is not bit-exact at 17 significant digits).
- **V8** iterates `kAllCommitSteps` as it actually is, never a copied list, and
  handles the succeeding arm explicitly — a uniform `!ok` sweep is wrong about
  the last step in the direction that destroys a completed reimport
  (MAR-189 §10.2).

### Task 6 — `ShellState` and the shell seam

**Files:** `src/editor/shell_state.hpp` (a `PsdReimportPanelState` struct,
`kPsdReimportModal`, one `ShellState` field),
`src/editor/shell_psd_reimport.{hpp,cpp}` (new) — `begin_psd_reimport_review`,
`close_psd_reimport_review`, `draw_psd_reimport_modal`; `CMakeLists.txt` (one
source line into `marrow_editor_shell`).

The modal: `BeginPopupModal(kPsdReimportModal, &panel.open, flags)`,
`SetNextWindowSizeConstraints` rather than `AlwaysAutoResize`, rows at **plain
modal scope** with `##<identity>` suffixes, `Selectable` width `max(40.0f,
GetContentRegionAvail().x - kControlColumn)`, checkbox after `SameLine()` on
`Missing` rows only, Confirm inside `BeginDisabled(!psd_review_can_confirm(...))`.

**Cases:** N1, N2 in the new `src/editor/shell_smoke_psd.cpp`, with the shell copy
of `expect_reimport_no_op` (all six clauses including selection) written first.

### Task 7 — the entry point, and the single call site

**Files:** `src/editor/shell_project_panels.cpp` — a `Reimport PSD...` button
inside `draw_project_window`, shown only when
`import_sources->psd` is engaged, and **one** `draw_psd_reimport_modal(state)`
call in the same function. `src/editor/shell_smoke_scenarios.hpp`,
`src/editor/shell_smoke.cpp` (one scenario call before
`validate_shell_foundation_smoke`).

**Neither frame body is edited.** Design §2.9.

**Cases:** N3, N4, N5, W1.

**Verify immediately:** `cmake --build "$SCRATCH/build" --target marrow_frame_body_check`
output is **character-identical** to `$SCRATCH/framebodies.baseline` (B6).

### Task 8 — F1 and F2, the real mouse

**File:** `src/editor/shell_smoke_psd.cpp`.

Order of operations, each step of which `AGENTS.md` records as producing a
*silently wrong* test if skipped:

1. Build the fixture (design §6.2), `load_project`, `clear_history()`.
2. **Control sweep first**, on a control that already exists in the Project
   window, so a broken id seed cannot be misread as a missing widget.
3. Sweep for `Reimport PSD...`; click it; confirm `FindWindowByName(kPsdReimportModal)`
   is non-null **and** `->Active`.
4. **Three settle frames** before capturing the modal's bounds; use the full
   `Rect()`, re-fetched after settling (`shell_smoke_project.cpp:2427-2432`: a
   16x37 stub one frame after opening).
5. Assert A10: Escape leaves the modal `Active`; the `p_open` control closes it.
   Re-open for the rest.
6. Locate one row per section by `modal->GetID(row_label)`. Print every
   coordinate.
7. Sweep the `Missing` row's checkbox **in its own column**, stepping the inset
   across the modal's width; assert the located x is **strictly inside**
   `modal->Pos.x + modal->Size.x - kControlColumn` (I11's detector).
8. Click; assert `chosen_psd_deletions` == `{that identity}` exactly.
9. Advance `io.DeltaTime` past `io.MouseDoubleClickTime` before the next gesture.
10. Sweep and click Confirm; assert `undo_count() == before + 1` and the outcome.

Leave `io.ConfigMacOSXBehaviors` alone — no modifier is needed. If one is ever
added, clear it for the gesture, restore it, and assert `ImGuiContext::TempInputId`.

### Task 9 — the structural gates

Each is a command whose output is recorded, and each is **red today**, so none is
a witness.

| # | Command | Expectation |
|---|---|---|
| S1 | `grep -rn "draw_psd_reimport_modal(" src/editor/ \| grep -v -e shell_smoke -e shell_psd_reimport` | exactly **1** line, **and that line must be in `shell_project_panels.cpp`** (I12's detector). **Restated twice, both times by running it.** First: the original "exactly 3 lines over `src/`" broke once N2 legitimately called the function to drive the `p_open` route, and a doc comment naming the function inflated it to 4 -- the comment was reworded rather than the gate loosened. Second, and worse: the count-only form **did not catch I12 at all.** Moving the call from `draw_project_window` into `render_shell_frame` leaves the count at exactly 1, because `shell_main.cpp` is also under `src/editor/` -- measured, not reasoned. A gate that counts call sites cannot see a call site MOVE, and I12 is precisely a move. The file assertion is what makes S1 the structural detector it claims to be. |
| S2 | `grep -rn "commit_psd_reimport(" src/editor/ \| grep -v -e psd_reimport_commit -e agent_dispatch` | exactly **1**, in `psd_reimport_review.cpp` — the **editor** path's single call site. Scoped and double-excluded because `agent_dispatch.cpp:1264` legitimately calls it for the *agent* path (design §0.5 item 6) and MAR-189's cases call it 16 times: the unscoped form measures **17** and would be red forever, which is how a real guard gets deleted for crying wolf |
| S3 | `grep -nE "BeginChild\|BeginTable\|BeginTabItem\|PushID" src/editor/shell_psd_reimport.cpp` | **no hits** (design §2.7 — protects F2's id seed) |
| S4 | `grep -nE "begin_edit\|transaction\|std::sort\|\.erase\(" src/editor/shell_psd_reimport.cpp` | **no hits** (the shell layer owns no logic) |
| S5 | `grep -c "ImGui" src/editor/psd_reimport_review.cpp include/marrow/editor/psd_reimport_review.hpp` | **0** each (the model layer cannot name a widget) |
| S6 | `grep -n "serialize_project" src/samples/psd_import_smoke.cpp src/editor/shell_smoke_psd.cpp` | every hit is a **struct or full-string** comparison; **no substring search** (design §5.2 rule 3) |

**"No hits" is evidence only once the command is known to have executed.** Every
S-gate above is run once against a deliberately planted hit, which is then
removed, so a typo'd path cannot be read as a pass.

### Task 10 — non-effect sweeps and the record

| # | Check | Expectation |
|---|---|---|
| T1 | `cmake --build "$SCRATCH/build" --target marrow_frame_body_check` | the **`MAR-187 frame-body check` STATUS line** character-identical to B6. Compare that line, not the whole output: a standalone target invocation omits the `[100%]` build-progress prefixes that a full build emits, so a whole-output diff reports a difference that is not the gate's. Run the comparison against a planted change first, so a passing diff is known to be capable of failing. |
| T2 | `ctest --test-dir "$SCRATCH/build" -N` | equals B2 = **23**. MAR-190 registers no test; any other value is a regression to investigate, never a number to adjust |
| T3 | `ctest --test-dir "$SCRATCH/build" --output-on-failure` | equals B3 |
| T4 | Registry / guards / bound / `test_client.py` | all equal B5 — a **non-effect** sweep, labelled one |
| T5 | `find "$SCRATCH/build/CMakeFiles" -name '*.o' -delete && cmake --build "$SCRATCH/build" -j8 2>&1 \| grep -c warning:` | equals B1 |
| T6 | `git diff --stat docs/root1/format-spec.md` | **empty** |
| T7 | `shasum -a 256` over B9's paths | equals `$SCRATCH/fixtures.baseline`, **absolute paths in the comparison** |
| T8 | `git status --porcelain` | only MAR-190's own paths, plus whatever other agents already had |

Then `AGENTS.md`: a `## Current Validation` line for the new shell scenario, and a
`## MAR-190 Add the PSD Reimport Review UI Validation Results` section carrying
the errors table, the inversion outcomes **as run** (never as predicted), P1's
witness label, and §9's limitations. **Do not "correct" any historical
measurement under a `## MAR-NNN` heading** — the repair for a confusing historical
number is a clarifying parenthetical naming the commit, never a new value.

**Two durable entries are already written and must not be duplicated.** At the
lead's instruction they were landed during planning, as a **62-insertion,
0-deletion** edit to `AGENTS.md` that is **uncommitted at the time this plan was
written**:

- `## Headless Frame Smoke Notes` gains a bullet: **Escape does not close a
  MODAL** (design §0.4 G9). It is a property of ImGui, not of this story, and the
  next modal in this tree will meet it.
- `## Repo facts that outlive their story` gains
  `### A zero result is evidence only once the pattern is known to match something`
  — the widened form of the "no hits" rule, now covering **two** causes (zsh glob
  expansion, and a malformed BRE interval), with the reproduction above.

Task 10 must **check whether they are still present** before writing anything: if
another story's commit swept or dropped them, re-land them; if they are there, add
only MAR-190's own two sections. `git log -1 --format=%H -- AGENTS.md` and a
`grep -F` for each heading answer it in one command each — and per the entry those
greps themselves add, a zero from either is only evidence once the pattern is
known to match something.

### Task 11 — the commit

One commit. Korean subject and body. Stage **only** MAR-190's own paths, by name;
for the five shared files (`shell_state.hpp`, `shell_project_panels.cpp`,
`shell_smoke.cpp`, `shell_smoke_scenarios.hpp`, `CMakeLists.txt`) stage **hunks**
with `git apply --cached` — git stages whole files, and MAR-187's landing was
about to overwrite ~493 lines of MAR-188's in-progress work exactly this way.
Both trailers, **contiguous, no blank line between them**:

```
Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: <copied from `git log -1 --format='%(trailers)' HEAD`>
```

Re-measure every baseline number at the commit's **actual parent** before writing
the body. Numbers do not survive a baseline change.

---

## §B. Inversion register

Run with `tools/inversion/invert.sh "$SCRATCH/build" <label> <file> <mutation.py>
<run-command>` — the harness whose restore uses absolute paths throughout
(`invert.sh` derives `root` with `cd … && pwd`). **There is no `invert.py`**
(design §0.4 G6); the fourth argument is a per-inversion mutation script you
write. If Task 0.7 found the unfixed `rebuild.sh`, append the object paths.

Every row names its **mutation**, its **predicted failure text**, and **why
nothing earlier in run order catches it first**. Attribution is by run order:
PSD smoke (V1, V2, D1, D2, V3, V4, V5, V6, V7, V8) then shell smoke (W1, N1, N2,
N3, N4, N5, F1, F2).

| # | File | Mutation | Predicted failure | Why nothing earlier catches it |
|---|---|---|---|---|
| **I1** | `psd_reimport_review.cpp` | `group_psd_review` appends `Missing` indices to `updated` | **V1**: `"MAR-190 V1: the three sections do not partition plan.layers -- index 5 appears in {updated, missing}"` | V1 is the first case to run, and it is the only one that asserts a partition. Every count clause anywhere still sums correctly; F1 draws a row either way |
| **I2** | `psd_reimport_review.cpp` | bucket the sections while iterating `plan.layers` in **reverse**, so each section is emitted descending | **V1**: `"MAR-190 V1: section 'updated' element 0 is 'torso\|body', expected 'torso\|arm_l' (ordered comparison)"` | Nothing before V1 runs. Set-membership and partition clauses both pass — order is the only observable, which is why V1 compares element-wise |

> **I2's original mutation was a provable no-op and was replaced.** It read
> *"emit each section in `plan.layers` encounter order after a
> `std::stable_partition`, dropping the identity sort."* There **is** no sort to
> drop: `plan_psd_reimport` builds `plan.layers` by iterating a
> `std::set<std::string>` of identities (`psd_reimport_plan.cpp:254-262`), and its
> own comment says *"the ordering is a property of the container rather than of a
> sort call a mutation could remove."* Encounter order **is** identity order, and a
> stable partition over an already-sorted vector reproduces it exactly — two
> spellings of one result, nothing cross-reads, the mutation cannot be observed.
> Reversing the iteration is a real one-token edit that leaves every membership and
> partition clause green and reddens only the ordered comparison. Found while
> implementing Task 1; the fourth degenerate shape, in this register.
| **I3** | `psd_reimport_review.cpp` | `apply_psd_reimport_review` step 3 passes `review.plan` instead of `build_psd_commit_plan(...)` | **D2**: `"MAR-190 D2: provenance still carries the row for 'psd\|shadow'; the reviewed choice to forget it was not applied (set difference: +1)"` | V1, V2 do not commit. **D1 passes by construction** — with no deletions the two plans are equal, which is precisely why D1 alone cannot cover AC4. Observable at all only because §2.3 returns by value; in-place mutation makes this a provable no-op. **The detector is the provenance row list, not the skeleton** (design §0.6 T1): the slot is absent either way, so the original predicted text named a condition that never holds and this inversion would have read as "did not bite" |
| **I4** | `psd_reimport_review.cpp` | `PsdReimportReview` construction seeds `delete_identities` from the `Missing` section | **D1**: `"MAR-190 D1: chosen deletions are {psd\|shadow, psd\|torso\|arm_l}, expected {} (set difference: +2)"` | V1 and V2 never read the set. D1 is the first case that does, and it compares the **whole set** — a count clause would read "2 missing layers, 2 choices" as consistent |
| **I5** | `psd_reimport_review.cpp` | `psd_plan_digest` returns `"digest"` | **V3**: `"MAR-190 V3: expected outcome Stale, got Committed; the re-planned digest compared equal to the reviewed one"` | Every earlier case is non-stale, and a constant compares equal to itself. N3 would catch it too, later and on the project side; V3 runs first. Both exist because one input alone does not prove the digest covers the other |
| **I6** | `psd_reimport_review.cpp` | delete the `remove_all(options.restage_root)` on the failure paths | **N4**: `"MAR-190 N4: restage root <abs path> still exists after a planning failure"` | Staging is not a target, so **every** clause of `expect_reimport_no_op` passes in V3, V4 and N3 — the byte map covers the four target artefacts and nothing else. N4's residue clause is the only detector, which is what makes it non-decorative |
| **I7** | `shell_psd_reimport.cpp` | Cancel calls `ImGui::CloseCurrentPopup()` but leaves `state->psd_reimport` engaged | **N1**: `"MAR-190 N1: the review is still engaged after Cancel"` | The invariant clauses cannot see it — cancel changes nothing either way, which is the case's own premise. F1/F2 would see the stuck modal block a later click, but they run after N1 |
| **I8** | `shell_psd_reimport.cpp` | `BeginPopupModal(kPsdReimportModal, nullptr, flags)` | **N2**: `"MAR-190 N2: the modal is still Active after the close control was driven"` | N1 passes — Cancel is a different control on a different code path. This inversion is the entire reason N2 is a separate case and not a second clause inside N1 |
| **I9** | `psd_reimport_review.cpp` | neuter adoption with `(void)session.adopt_runtime_sources();` and ignore its result | **V7**: `"MAR-190 V7: the session's active skeleton is <old abs path>; expected the committed bundle"` | V6 passes: `serialize_project` is *correct*, because provenance was still updated. Only V7 reads the runtime source and the revision movement. Uses `(void)f(...)`, never `if (false && …)` |
| **I10** | `psd_reimport_review.cpp` | add `session.clear_history();` after a successful commit | **V6**: `"MAR-190 V6: undo_count() is 0, expected 3 (2 pre-existing + 1 provenance)"` | Every byte clause and every provenance clause passes — history depth is invisible to all of them. This is why depth is asserted on the **success** path, not only on the five failure paths |
| **I11** | `shell_psd_reimport.cpp` | drop the explicit `Selectable` width, restoring the default full-content-region span | **F2**: `"MAR-190 F2: the delete checkbox for 'psd\|shadow' was located at x=1187.0, outside the modal's usable right edge 1132.0 -- a default Selectable spans the whole content region and pushes a SameLine() control off the window"` | **Every UI-free case stays green**, which is the whole content of the finding: N1–N5 call the seams directly and never render. F1 finds the rows and would find the checkbox by sweeping wide enough; only F2's in-bounds assertion converts "found somewhere" into "reachable" |
| **I12** | `shell_project_panels.cpp` + `shell_main.cpp` | move the `draw_psd_reimport_modal(state)` call out of `draw_project_window` and into `render_shell_frame` | **Task 9 S1**: `"MAR-190 S1: draw_psd_reimport_modal( has 2 call sites, expected 1 (shell_main.cpp:597, shell_project_panels.cpp removed)"` | The application still works, so N1–N5 and F1/F2 in a *manual* run pass. The headless smoke's shared body never calls it, so the shell scenario's frames draw no modal — and **`CheckFrameBodies.cmake` cannot see it either**, because `draw_psd_reimport_modal(` does not match `draw_[a-z_]+windows?\(`. S1 is the story's only structural detector, and this inversion exists to prove it |
| **I13** | `psd_reimport_review.cpp` | `psd_review_can_confirm` returns `true` unconditionally | **V2**: `"MAR-190 V2: confirmation is enabled on a plan carrying error 'PSD folder end marker appeared without an open folder.'"` | V1 does not call it. V2 is the first, and it is the only case that asserts the *negative* — every other case works with a confirmable plan |
| ~~**I14**~~ | — | ~~step 3 accepts a chosen identity absent from the re-plan~~ | **NOT RUN — SUBSUMED BY I5** | **Task 0 settled this** (design §0.6 T9). The shipped digest is `identity=change;` per layer, so the ordered identity list **is** the digest: any way a chosen identity vanishes or is reclassified moves it, and §2.4 step 2 returns `Stale` before step 3's lookup can run. Step 3's arm is unreachable defensive code, so the mutation is a **provable no-op**. Recorded as subsumed and **not** run — a subsumed inversion and a non-biting one are indistinguishable in a log and mean opposite things |

### B.0 Findings from implementing Tasks 1-2

Recorded as they were measured, per design §5.2 rule 5.

| # | Finding |
|---|---|
| **F-a** | **I2's original mutation was a provable no-op.** Recorded in full at the I2 row above. Replaced with reverse iteration, which bites as *same set, wrong order*. |
| **F-b** | **D1's by-value clause was vacuous, and an inversion is what proved it.** D1 asserted "deriving the commit plan did not mutate the review" -- but D1's forget set is **empty**, so the derivation sets nothing to `false` and the clause cannot fail whatever the implementation does. A gate that passes on unchanged code, in this story's own case. Measured by **i3a** (below), which deliberately derives through a `const_cast` on `review.plan`: D1 stayed **green**. The clause now lives in **D2**, whose forget set is non-empty, and i3a reddens it by name. The lesson generalises: *an invariant clause belongs in the case that actually produces the state it forbids, not in the first case that can compile it.* |
| **F-c** | **The register's predicted failure texts use a `psd\|` identity prefix that does not exist.** Identities are `shadow`, `torso\|arm_l`, `fx\|glow` -- `build_identity` joins group segments and the layer name, with no source prefix. Harmless to the cases, but a predicted string that cannot match verbatim is the same family as a citation that does not resolve. |
| **F-d** | **Two build hazards, both promoted to `AGENTS.md`'s durable section** rather than recorded only here, because both produce *another story's case failing before this story's code runs* and the correct diagnosis differed each time: an editing tool's cached file state used as a write-back base, and `git archive`'s commit-time mtimes leaving stale objects in an existing build directory. See *"An editing tool's cached file state is a stale restore target"* and *"`git archive | tar -x` restores commit-time mtimes"*. |

| **F-e** | **MAR-190's scratch root was the last fixed one in the tree, and it is fixed.** `validate_mar190_reimport_review` now takes `scratch_root("mar190_review")`, which appends the pid, rather than `temp_directory_path() / "mar190_review"`. **Measured both ways**, because "eight concurrent runs passed" is not evidence unless the old form fails: fixed root -> **3 passes, 5 failures of 8**, 0 distinct roots; per-process -> **8 passes, 0 failures**, 8 distinct roots. The negative control ran through `invert.sh`, restore verified by absolute-path `cmp`. **Without the first row the second is a story about timing, not a measurement of the fix** -- and that is the same rule as *a case green before the implementation exists is a witness* and *run every "expect zero" gate against a planted hit*, in a third vocabulary. Its general form is now durable in `AGENTS.md` as *"A passing result is evidence only once you have seen the failing form fail"*. The general form, which bears on Task 4's `restage_root`: *a "unique" name is unique only across the scope its mechanism spans* -- a `static` counter is unique per process, and two processes under one fixed root collide on `plan-1`. |
| **F-f** | **A mixed-tip verification tree produced a failure that belonged to neither tip.** Regenerating only `psd_import_smoke.cpp` from current HEAD while the rest of `$SCRATCH/tree` came from an older extraction gave a tree whose *case* expected a message the *production code* did not yet emit -- a red run attributable to no commit that exists. Both a single run and eight concurrent runs failed, and the concurrency framing nearly sent the diagnosis in the wrong direction entirely. Durable in `AGENTS.md` as *"A tree assembled from two reads of 'HEAD' belongs to no commit that exists"*, with the discriminator that redirected the diagnosis: **a single run reproduced what looked like a concurrency failure**. The fix is procedural and now scripted: **the whole tree and every regenerated file come from ONE `git rev-parse HEAD`**, captured once and reused, never from two reads of "HEAD" taken minutes apart. |

| **F-g** | **F2 found an ImGui abort that no UI-free case could reach.** The Confirm branch returns early from inside `BeginDisabled(...)`, so it skipped `EndDisabled()` and the run aborted with `In window 'Reimport PSD##psd_reimport': Missing EndDisabled()`. Every N-case calls the seams directly and never enters that scope; only a real click on a real button does. This is the concrete answer to *"why does a frame case earn its cost"* -- and it is the same class as MAR-178's deleted button body, in a form a grep cannot see. |
| **F-h** | **The window a sweep starts from needs settle frames too, not just the modal.** The Project window's rect one frame after submission is a **32x37 stub, measured at (60,60)-(92,97)**, so the control sweep scanned a sliver and reported the control absent. F1 settled the modal and not the window it swept first. The control sweep is what caught it: a known-present widget failing is unambiguous, where a missing button would have read as "the button is not drawn". **A positive control converts a silent wrong answer into a loud one**, which is its entire job. |
| **F-i** | **F1 first grouped a plan the production path never fills.** It called `group_psd_review(fixture.plan)` -- but the button builds its own plan, so `fixture.plan` was default-constructed and F1 reported 0 Updated / 0 Missing **while the modal on screen was drawing rows**. Now it groups `state.psd_reimport.review->plan`, i.e. the plan the modal is actually showing. A frame case that asserts against a value it computed itself is not observing the UI. |
| **F-j** | **The shell fixture had no `Missing` row at all.** It reimports the sample PSD over itself, which yields only `Updated`, so there was no checkbox for F2 to reach and the case could not fail. The fixture now appends one remembered layer the PSD does not contain -- exactly the situation the checkbox exists for. |
| **F-k** | **I11 bites as "never hovered", not "located out of bounds".** A control pushed past the window's right edge is **clipped and not hoverable at all**, so the sweep never sees it rather than finding it in the wrong place. Both of F2's clauses earn their place: the found-clause fires here, the in-bounds clause guards the case where it is found but outside the reserved control column `[Pos.x + Size.x - kPsdReviewControlColumn, Pos.x + Size.x)`. |

**New register row, added during implementation:**

| # | File | Mutation | Predicted failure | Why nothing earlier catches it |
|---|---|---|---|---|
| **i3a** | `psd_reimport_review.cpp` | `build_psd_commit_plan` derives through `const_cast<PsdReimportPlan&>(review.plan)` instead of copying -- the by-value property removed | **D2**: `"MAR-190 D2: deriving the commit plan mutated the REVIEW's own plan; layer 'torso\|arm_l' now has preserve=false. The derivation must return a copy."` | Every ordered `preserve`-list clause passes: the derived plan is *correct*, and only the review's own copy is collateral. D1 cannot see it (empty forget set, nothing set to `false` -- F-b). This row exists because it is what keeps **I3** a real inversion in Task 5 rather than two names for one object, and it is the direct evidence for design §2.3's by-value requirement |

### B.2 AC3's history clause -- the defect, the fix, and its three inversions

**V8 measured MAR-190's own AC3 failing.** AC3 names five conditions in one breath
and "commit failure" is one of them: *"...leave the project, runtime source,
files, selection, and **history** unchanged."* The shell cases already asserted
`redo_count()` unchanged for Cancel and modal close, so the commit-failure arm was
being held to a weaker bar than the other four with no principled reason for the
difference. The first version of V8 asserted `redo == before + 1` on that arm,
which **encoded the defect as a requirement** -- the most comfortable way to ship
one.

**TWO defects had to be separated before either could be fixed**, and a reader who sees only the stash/restore trio will wonder why the simple fix was not enough. The wrong-primitive defect (an armed Redo) was the one the analysis started from. The second -- `push_history` clearing the redo stack on **every** commit -- was found only by writing V9: it is correct for a real edit and wrong for a speculative one that gets rolled back, it would have destroyed the user's redo branch on **every** failed reimport, and no clause aimed at the first defect would have caught it. Fixing either alone leaves AC3 or AC5 broken.

**Two distinct defects, and only after separating them was either fixable:**

| Symptom | Verdict |
|---|---|
| `redo_count()` gains an entry after a rolled-back `UpdateProvenance` | **Defect.** The rollback called `session_.undo()`, whose contract is to make the entry redoable. Pressing Redo re-applied the provenance edit over rolled-back files. |
| The user's PRE-EXISTING redo stack is emptied | **Defect, and a different one.** `push_history` clears redo whenever any edit commits (`session.cpp:1393`) -- correct in general, wrong for a speculative edit that gets reverted. |
| `project_revision()` advances across the revert | **INTENDED.** `apply_history` bumps it whenever the project changed (`session.cpp:1672`), in either direction. It counts CHANGES and is a change detector, not a state identifier; monotonicity is what makes it usable as one. Recorded as intended, not excused -- *an excused clause and an intended behaviour read identically in a test and mean opposite things.* |

**The fix, in the session layer where the stacks live:** `revert_last_edit()`
(reverts without pushing a redo entry, keeping `undo()`'s two guards) plus a
matched `stash_redo_stack()` / `restore_stashed_redo()` / `drop_redo_stash()`
trio. `clear_history()` was NOT used: it wipes both stacks, which fixes AC3 by
breaking AC5's *"preserving existing unsaved overlays and undo/redo history."*

| # | Mutation | Outcome |
|---|---|---|
| **I15** | the rollback goes back to `session_.undo()` | **BIT** -- V9: *"the user's redo stack was 1 before the failed reimport and is 2 after"* |
| **I16** | the displaced redo branch is never restored | **BIT** -- V9: *"...was 1 before ... and is 0 after"*. This is the faithful test of the AC5 trade: 1 -> 0 is exactly what a blanket clear produces |
| ~~I17~~ | `clear_history()` in place of the revert | **MIS-AIMED, retired.** It does not isolate the AC5 trade, because it also skips the revert -- MAR-189's **R3** catches the un-reverted provenance first (`row list mismatch ... got 'source=..._candidate.psd'`). Recorded rather than counted: an inversion caught by a different story's case has not tested this story's clause |

**I15 did not bite on first run, and the reason is worth keeping.** The rollback
originally restored the stash *after* reverting -- and `restore_stashed_redo`
ASSIGNS `redo_entries`, so it overwrote whatever the revert left behind, making
`undo()` and `revert_last_edit()` produce identical final stacks. **Two correct
components in the wrong order made each other unobservable.** Restoring first
makes each carry its own weight, and the comment at the site says so.

**V9** is the case that proves AC3 was not fixed by breaking AC5: it primes a real
redo stack (edit, then undo), fails a reimport at `UpdateProvenance`, and asserts
the primed entry survives **by depth, by label, and by still replaying** -- a
depth check alone passes if the rollback swapped its own entry in.

### B.1 Deliberately uninverted, by name

- **The four provenance `ImGui::Text` calls.** MAR-188's P-cases already cover the
  fields' typed round trip; inverting the formatter proves nothing an acceptance
  criterion asks about.
- **`PsdReviewOutcome`'s status-text `switch`.** Exhaustive, no `default:`;
  Clang's `-Wswitch` produces the checklist when a seventh value arrives — as a
  **warning that does not fail the build**, and **GCC needs `-Wall`**, which this
  tree does not pass. A per-arm inversion would prove the compiler works.
- **P1**, the compatibility witness — **no story-owned inversion, by design**
  (design §6.3). Recorded, not repaired.
- **MAR-189's fourteen commit steps.** V8 sweeps them; inverting the commit body
  belongs to MAR-189's register.
- **`expect_reimport_no_op`'s clause (e)** (runtime-source paths) is inverted only
  through I9; the remaining five clauses are each the named detector of at least
  one row above, so no clause is unattributed.

### B.2 The three degenerate shapes, per-case

| Shape | Instance found while planning | Resolution |
|---|---|---|
| Fails on correct code | N3 pinning its no-op baseline **before** its own deliberate provenance transaction | Baseline is captured **after** the transaction (design §6.2) |
| Fails on correct code | V1 asserting insertion order, when `PsdReimportPlan::layers` is documented *"Lexicographic by `identity`, ascending"* | V1 asserts the documented order |
| Passes on unchanged code | Any AC1 clause written as a text search over `serialize_project()` — `preserved_root` re-emits `$.editor.import_sources` with zero code | Task 9 **S6** forbids it structurally; every such clause asserts the typed struct |
| Passes on unchanged code | **P1** | Labelled a witness in the source, the plan and `AGENTS.md`; no inversion claimed |
| Provable no-op | **I3**, if `build_psd_commit_plan` mutated `review.plan` in place — two names, one object, no cross-field read | Design §2.3 returns by value **for this reason**; I3 is real only because of it |
| Provable no-op | **I14**, if the provenance edit necessarily moves the digest | Task 0 measures it; if subsumed, it is recorded as subsumed by I5 and **not** reported as "did not bite" |

---

## §C. Verification commands

From `$SCRATCH/tree`, with `$SCRATCH/build`:

```
cmake --build "$SCRATCH/build" -j8                      # the frame-body gate runs POST_BUILD
cmake --build "$SCRATCH/build" --target marrow_frame_body_check
ctest --test-dir "$SCRATCH/build" -N
ctest --test-dir "$SCRATCH/build" --output-on-failure
"$SCRATCH/build/marrow_psd_import_smoke" \
    assets/fixtures/psd_import_sample.psd \
    assets/fixtures/psd_import_sample_reimport.psd   # from the repo root
"$SCRATCH/build/marrow_editor_shell" \
    --project assets/fixtures/player_idle.marrow --auto-close 5   # from the repo root
find "$SCRATCH/build/CMakeFiles" -name '*.o' -delete && cmake --build "$SCRATCH/build" -j8 2>&1 | grep warning:
```

**`find build/CMakeFiles -name '*.o' -delete`, scoped.** The unscoped
`find build -name '*.o' -delete` also deletes vendored SDL3's objects;
`libSDL3.a` is then re-created incrementally at 96 bytes, the link fails, and
`ctest` reports a phantom two-test regression with MAR-190's sources provably
untouched. **A red run is evidence only once the build is known sound.**

---

## §D. Report-back contract

The implementer's report must state, for each item: measured value, and the
command that produced it.

1. Whether every MAR-189 deliverable in design §0.5 items 1–4 was present at Task
   0, with file:line — and if not, that the story stopped there.
2. B1–B9 and T1–T8, with T-values compared against B-values.
3. Every inversion's **actual** outcome, including any that did not bite, each
   with the reasoning that proves it was not a no-op.
4. Every place this plan, the design, or a source document turned out wrong.
5. Which acceptance criteria are satisfied at a weaker standard than their wording
   — at minimum AC6's *"rollback errors"* (design §9.4).
6. What is **not independently covered**: P1, the scrolling path (§9.6), the
   button's absence (§9.3), and — if Task 3 took the fallback — the duplicated
   digest field list.
