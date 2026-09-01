# MAR-189 Commit PSD Reimports Atomically — Implementation Plan

Design: `docs/superpowers/specs/2026-09-01-mar-189-atomic-psd-reimport-commit-design.md`.
Baseline at authoring: `cf6a199` (MAR-188 at `71465db`). **Re-measure every number
at your commit's actual parent** — numbers do not survive a baseline change.

---

## Standing rules for this plan

1. **Build in an isolated tree.** Other agents are mutating tracked files.
   ```sh
   AGENT=plan-mar189-impl            # YOUR agent name, never "mar189"
   SCRATCH=/private/tmp/claude-501/-Users-kwon-Workspace-C-Maroow/<session>/scratchpad/$AGENT
   rm -rf "$SCRATCH/tree"; mkdir -p "$SCRATCH/tree"
   git -C /Users/kwon/Workspace/C/Maroow archive HEAD | tar -x -C "$SCRATCH/tree"
   # then overlay ONLY your own story's files.
   ```
   `git archive | tar -x` **overlays**; a directory you did not create is
   discarded with `rm -rf`, never trusted.
2. **Object deletion is scoped.** `find build/CMakeFiles -name '*.o' -delete`.
   The unscoped form deletes vendored SDL3's objects and re-links `libSDL3.a` at
   96 bytes, which produces `ctest 20/22` with your sources untouched.
3. **Absolute paths in every `cmp`, restore and baseline.** A relative path
   follows whatever `cd` ran earlier in the same command, and a `cmp` of a file
   against itself always passes.
4. **Assert the message, never `!result`.**
5. **A count is almost never the fact.** Full sorted identity lists with
   set-difference messages.
6. **`if (false && …)` does not neuter** — the expression is still type-checked
   and the call may be elided in ways that change nothing. Use `(void)f(...)`.
7. **"No hits" is evidence only once the command is known to have executed; a
   red run is evidence only once the build is known sound.**
8. **Every case must be proven falsifiable and uniquely attributed by RUN
   ORDER**, not by authoring order.
9. **Write no shell surface beyond §Task 7's one branch.** MAR-189 is otherwise
   UI-free.

---

## §A. What was verified for you, what was wrong, and what Task 0 must still gate

### A.1 Errors already found — do not re-litigate, do re-anchor

| # | Where | Finding |
|---|---|---|
| F1 | brief | `RenameCallback` **does not** suffice as the AC3 seam. Design §3.1 |
| F2 | brief | `CheckFrameBodies.cmake` is indeed invisible to MAR-189 — **but** AC5's approval lives in `draw_agent_window`, which the smoke never draws. Design §9.1 |
| F3 | brief | `agent_dispatch_smoke.cpp:42` confirmed. **MAR-189 adds no operation**; registry stays 66 and no guard moves |
| F4 | MAR-188 | `PsdReimportPlan` has **no** `staged_texture_path`; AC1 names textures |
| F5 | MAR-188 | The staged `.matl` says `"image": "staged.png"`. Committing it byte-identically breaks the target atlas **silently**. Design §2.2 |
| F6 | `tools/inversion/README.md` | `invert.sh:32` calls `rebuild.sh "$build_dir"` with **no object paths**, so the advertised object deletion never happens. The two real guards (abort on failed mutation build; refuse a stale baseline) **are** wired |
| F7 | MAR-188 | `preserve` is consumed by MAR-189 (AC6 names deletion inputs); MAR-190 still owns the UI that sets it |

### A.2 Design claims Task 0 must prove by RUNNING

`A1`–`A10` in design §8. Every one is a probe, not a reading. **`A11` is owned by
this plan (§0.10)** — the repo-safety gate — and it is the one Task 0 item that
must exist as *code that aborts* rather than as a recorded measurement.

### A.3 Anchors re-derived at `cf6a199` — re-derive again, do not trust this list

```
src/editor/atomic_file_write.cpp:212           the sole RenameCallback consult
src/editor/psd_reimport_plan.cpp:143-152       staged.mskl / staged.matl / atlas_name="staged"
src/editor/atlas_packer.cpp:880                "image" <- image_path.filename()
src/editor/atlas_packer.cpp:1024-1025          image_path = atlas path with .png
src/editor/psd_import.cpp:1076                 write_imported_layers' remove_all
src/editor/psd_import.cpp:1233-1258            the existing-skeleton merge
src/editor/session.cpp:1895-1965               adopt_runtime_sources
src/editor/agent_dispatch.cpp:97               import.psd_layers registration
src/editor/agent_dispatch.cpp:613-630          agent_path_allowed
src/editor/agent_dispatch.cpp:664-704          enqueue_review
src/editor/agent_handlers_management.cpp:93-190 the import/pack branch
src/editor/shell_agent_panel.cpp:148-181       the Acknowledge branch
src/samples/agent_dispatch_smoke.cpp:42        std::array<OperationExpectation, 66>
src/samples/agent_dispatch_smoke.cpp:1621      import.psd_layers dry-run
src/samples/agent_dispatch_smoke.cpp:4479      import.psd_layers review
src/samples/psd_import_smoke.cpp:1584          main()
tools/mcp/tools/editing.py:1302                the MCP tool
tools/mcp/test_client.py:53,55                 == 66
cmake/CheckFrameBodies.cmake:36                _allowed_app_only
CMakeLists.txt:853                             marrow_psd_import_smoke
CMakeLists.txt:1354-1360                       marrow.agent_dispatch_smoke
```

Read the **committed blob**, not the worktree:
```sh
git -C /Users/kwon/Workspace/C/Maroow show HEAD:src/editor/psd_reimport_plan.cpp | sed -n '138,158p'
```

### A.4 Inversion register

Design §7, I1–I17, plus §7.1's four deliberately-uninverted entries. Record the
**actual** text beside each prediction; where they differ, the difference is the
finding.

---

## Task 0 — Measure, before any code

### 0.1 Re-anchor gate
Every line in §A.3, against the committed blob. A citation that does not resolve
is **blocking** — MAR-188's re-anchor gate found `assign_slot_names`, a symbol
that exists nowhere, cited as a primary anchor in three places.

### 0.2 Baseline
```sh
cd "$SCRATCH/tree"
cmake -S . -B build && find build/CMakeFiles -name '*.o' -delete
cmake --build build -j8 2>&1 | tee /tmp/$AGENT-base.log | grep -c warning:
ctest --test-dir build -N | tail -3
ctest --test-dir build
./build/marrow_agent_dispatch_smoke | grep -c '\[ OK \]'
./build/marrow_psd_import_smoke
ctest --test-dir build -N | grep -c psd_import   # expect 0  (A9)
```

### 0.3 A1/A2 gate — **prove F5 by running**
A short probe linking `libmarrow_editor`:
```
load_project(assets/fixtures/player_idle.marrow)
plan_psd_reimport(project, {psd_path=assets/fixtures/psd_import_sample.psd,
                            staging_root=/tmp/$AGENT-stage})
print plan.staged_atlas_path, and `grep '"image"'` that file
print the atlas artifact's png path via a directory listing of the staging root
```
**Expected: `"image": "staged.png"`.** If it is not, F5 is refuted and design
§2.2 must be reconsidered before Task 1. Record whichever it is.

### 0.4 A3 gate — is the atlas `"name"` load-bearing?
```sh
grep -rn "AtlasInfo\|info_out->name\|\.name" src/runtime/atlas.cpp | head
grep -rn "atlas.*\.name" src/ include/ | grep -v "region\|src/tests\|src/samples"
```
Read the output. "No hits" counts only because the command above provably ran.

### 0.5 A6 gate — binary safety of `write_file_atomically`
Read `assets/fixtures/<any>.png` into a `std::string`, write it through
`write_file_atomically(dst, bytes, "texture")`, then
`cmp /abs/src.png /abs/dst.png`. Absolute paths.

### 0.6 A7 gate — does a directory rename cross a volume here?
```sh
mkdir -p /tmp/$AGENT-x/d && \
  python3 -c "import os;os.rename('/tmp/$AGENT-x/d','/Users/kwon/Workspace/C/Maroow/.x-$AGENT')" \
  ; echo "exit=$?"; rm -rf "/Users/kwon/Workspace/C/Maroow/.x-$AGENT" /tmp/$AGENT-x
```
`EXDEV` here means design §2.4's "placement copies rather than renames" is
load-bearing rather than merely tidy. Either answer is fine; record it.

### 0.7 A8 gate — does a provenance-only transaction commit against the OLD document?
Probe: open the fixture, `begin_edit`, set
`project()->editor_metadata.import_sources`, `commit()`. **If it fails**,
`UpdateProvenance` must move above `AdoptRuntimeSources` and design §2.7's
window inverts — a blocking finding, so run this before Task 5.

### 0.8 A4/A5 gate — the numbers
```sh
grep -c '^    {"' src/editor/agent_dispatch.cpp                 # expect 66
grep -rn '!= 66U' src/ | wc -l                                  # expect 11
grep -rn '!= 66U' src/ | sed 's/:.*//' | sort | uniq -c         # expect 7/2/2
grep -n 'OperationExpectation, ' src/samples/agent_dispatch_smoke.cpp
grep -n '== 66' tools/mcp/test_client.py
```

### 0.9 A10 gate — the inversion harness (F6)
```sh
sed -n '30,35p' tools/inversion/invert.sh    # confirm rebuild.sh gets no object args
```
Then write **your own** wrapper in your scratch dir that deletes the objects and
delegates, rather than editing shared tooling while other stories are in flight:
```sh
# $SCRATCH/rebuild189.sh <build-dir>
rm -f "$1"/CMakeFiles/marrow_editor.dir/src/editor/psd_reimport_commit.cpp.o \
      "$1"/CMakeFiles/marrow_editor.dir/src/editor/psd_reimport_plan.cpp.o \
      "$1"/CMakeFiles/marrow_editor.dir/src/editor/agent_dispatch.cpp.o \
      "$1"/CMakeFiles/marrow_editor.dir/src/editor/agent_handlers_management.cpp.o \
      "$1"/CMakeFiles/marrow_psd_import_smoke.dir/src/samples/psd_import_smoke.cpp.o \
      "$1"/CMakeFiles/marrow_agent_dispatch_smoke.dir/src/samples/agent_dispatch_smoke.cpp.o
cmake --build "$1" -j8
```
Re-derive those object paths from the real `build/CMakeFiles` tree; do not trust
the spelling above.

### 0.10 **A11 — the repo-safety gate. Write it before any committing test exists.**

This story's subject is the atomic replacement of asset bundles, and
`agent_path_allowed` whitelists the **project directory**
(`agent_dispatch.cpp:626-629`). `marrow.agent_dispatch_smoke` runs with
`WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}` and opens
`assets/fixtures/player_idle.marrow`, whose `.mskl`, `.matl`, `.png` and layer
directory are **tracked files**. A committing test pointed at that session
overwrites the user's repository. The blast radius is not a red test.

So this is a **gate that aborts, not a note**. Add to `psd_import_smoke.cpp` and
`agent_dispatch_smoke.cpp` a shared precondition, called by **every** case that
can write — R1, R3–R7 and A1–A6 — before it touches anything:

```cpp
// Aborts the process. A case that writes into the repository is not a failing
// test; it is damage that a later green run would hide.
void require_disposable_target(const std::filesystem::path& target, const char* case_name);
//   1. weakly_canonical(target) is under weakly_canonical(temp_directory_path())
//   2. weakly_canonical(target) is NOT under the repository root
//      (located by walking up for a `.git` entry, NOT by a compiled-in path)
//   3. the target is not any path reachable from the tracked fixture project
```

Run it as a **Task 0 self-test, before Task 1**, with three rows asserting the
predicate itself is not vacuous:

| Row | Input | Expected |
|---|---|---|
| a | `<temp>/mar189/player_idle.mskl` | accepted |
| b | `<repo>/assets/fixtures/player_idle.mskl` | **aborts**, message names the repository root |
| c | `<repo>/build/x.mskl` | **aborts** — inside the repo is inside the repo, build directory or not |

Row (b) is the one that matters, and a predicate that passes row (b) is worse
than no predicate: it is a gate that **passes on the exact input it exists to
catch**. Prove it aborts before trusting it.

Then, and separately, keep the post-run check as a second, independent
witness — `git status --porcelain assets/fixtures/` must be empty in the final
verification (case **A7**). Two mechanisms, because the precondition can be
forgotten at a call site and the post-check cannot tell you *which* case did it.

### 0.11 Task 0 exit criteria
Every anchor resolves; A1–A11 recorded with their measured values; **A11's three
rows demonstrated, row (b) aborting**; baseline warning count, `ctest -N`,
`ctest`, `[ OK ]` count written down. **Any blocking finding is reported before
Task 1 begins.**

---

## Task 1 — Stage under the target names (design §2.2)

**Files:** `include/marrow/editor/psd_reimport_plan.hpp`,
`src/editor/psd_reimport_plan.cpp`.

1. Add `staged_skeleton_filename{"staged.mskl"}` and
   `staged_atlas_filename{"staged.matl"}` to `PsdReimportPlanOptions`, and
   `staged_texture_path` to `PsdReimportPlan`.
2. Use them for `staged_skeleton_path` / `staged_atlas_path`; set
   `import_options.atlas_name` from `staged_atlas_filename`'s **stem**.
3. Set `plan.staged_texture_path = imported.texture_path` — **copied from the
   import result**, never recomputed, so the packer's rule stays the only rule.
4. Extend the containment check at the end of `plan_psd_reimport` to include
   `staged_texture_path`. Empty filenames, or ones with a directory separator,
   are refused with their own message.

**Tests (in `psd_import_smoke.cpp`, added to MAR-188's Q-suite region):**
- **Q12** — defaults unchanged: with neither filename supplied the plan's three
  staged paths are byte-identical strings to what MAR-188 produced. *This is a
  compatibility witness, not a gate* — it is green before Task 1 exists.
- **Q13** — custom filenames: `staged_skeleton_path`, `staged_atlas_path` and
  `staged_texture_path` all end in the requested names, all are inside
  `staging_root`, and the staged `.matl`'s `"image"` member equals the requested
  atlas stem + `.png`. **This is the gate**, and it is the direct detector for
  I6.
- **Q14** — a filename containing `/` is refused, asserted on its message.

**Verify:** `./build/marrow_psd_import_smoke` green; MAR-188's Q0–Q11 unchanged.
**Inversion:** I6 (revert step 2) → Q13's `"image"` clause.

---

## Task 2 — The failpoint seam and the step ledger (design §3)

**Files (new):** `src/editor/psd_reimport_commit_internal.hpp`,
`include/marrow/editor/psd_reimport_commit.hpp`,
`src/editor/psd_reimport_commit.cpp`. **`CMakeLists.txt`**: add the source to
`marrow_editor`.

1. `PsdCommitStep` (15 values, design §3.2), `kAllCommitSteps`,
   `psd_commit_step_name` as a **`switch` with no `default:`**.
2. `set_psd_commit_failpoint_for_testing`, mutex-guarded, with a HAZARD comment
   modelled on `atomic_file_write.hpp`'s.
3. `commit_psd_reimport` with **only** `ValidateRequest` implemented, plus the
   single `advance(step)` helper — the sole place that appends to
   `steps_executed` and the sole place that consults the failpoint.

**Test-first — R0 (skeleton form) and R8:**
- R8's four refusals (plan carrying an `error`; empty `staged_skeleton_path`;
  missing staging root; session with no project), each on its message, each with
  a byte-map-identical clause.
- R0 at this stage asserts only `steps_executed == {ValidateRequest}`.

**Watch them fail** before implementing (a link error is not a failing test —
stub the function first, then watch the assertions go red).

**Verify + inversions:** I1 (`advance` does not append) → R0.

---

## Task 3 — The byte map, prune, and staged validation (AC1)

**Files:** `src/editor/psd_reimport_commit.cpp`; `src/samples/psd_import_smoke.cpp`.

1. **`bundle_bytes()` in the smoke first** (design §6.2): a
   `std::map<std::string,std::string>` over the layers directory (recursive),
   texture, atlas, skeleton and the `.marrow`, plus a comparison helper whose
   message names paths-only-in-before, paths-only-in-after, and the first
   differing offset with both byte values. **Nothing else in this story is
   trustworthy until this exists.**
2. `PruneUnpreserved`: for each planned layer with `change == Missing &&
   !preserve`, remove its slot and attachment from the **staged skeleton
   document**, using the provenance record's own `slot_name` /
   `attachment_name`. A named identity whose slot is absent is an error naming
   the identity. Re-serialise the staged skeleton in place.
3. `ValidateStagedBundle`: design §2.3's four sub-steps, mirroring
   `session.cpp:1919-1965` against the staged artefacts.

**Tests:** R2(a) (**label it a witness**), R2(b).
**Inversions:** I2 (`advance` before the body) → R2(b)'s absent-from-ledger
clause; I7 (validate against the current document) → R2(b).

---

## Task 4 — Journal, backup, place, rollback (AC2, AC3 mechanics)

**Files:** `src/editor/psd_reimport_commit.cpp`.

1. `OpenJournal` — write the manifest (design §2.5) into the project directory
   through `write_file_atomically`.
2. `Backup{Layers,Texture,Atlas,Skeleton}` — `std::filesystem::rename` each
   target to `<target>.marrow-journal-<id>.bak` **in its own parent directory**.
   A target that does not exist is recorded as `absent` in the manifest and
   restored by deletion.
3. `Place{Layers,Texture,Atlas,Skeleton}` — read the staged bytes and write
   through `write_file_atomically(target, bytes, subject)`. `PlaceLayers`
   creates the directory and places each staged layer file.
4. `rollback()` — reverse order: remove what was placed, rename each backup
   back, remove the manifest. Populates `rolled_back` and, on failure,
   `rollback_error`.
5. `CleanJournal` — remove every backup and the manifest; **never rolls back**;
   populates `journal_residue` on failure and still reports `ok`.

**Tests:** R1, **R1b**, **R3 (the sweep)**, R7. Write **R1b first** — the atlas
`"image"` case is the only thing that sees F5, and every byte clause passes
without it (design §6.5).

R3's shape, written as data so a new step cannot escape it:
```cpp
struct StepExpectation { PsdCommitStep step; enum { RollsBack, SucceedsWithResidue } outcome; };
// one row per kAllCommitSteps entry; a size mismatch between the table and
// kAllCommitSteps is itself an asserted failure naming the missing step.
for (const StepExpectation& row : kStepExpectations) { … }
```
Per rolling-back iteration: `!ok`; the error text contains
`psd_commit_step_name(row.step)`; `bundle_bytes` identical to the pre-commit
map; `session.runtime_revision()` unmoved; provenance unchanged; a recursive
listing of every bundle directory contains no `*.marrow-journal*` and no
`*.tmp.*`.

**Inversions:** I3, I4, I5, I12, I13 — each predicted in design §7 with the arm
it should redden. Run each with your Task 0.9 wrapper so the objects are really
deleted; a mutation that does not rebuild is a false "did not bite".

---

## Task 5 — Adoption, provenance, overlays, preservation (AC2, AC4, AC6)

**Files:** `src/editor/psd_reimport_commit.cpp`.

**Run Task 0.7's A8 probe first.** If a provenance-only transaction cannot
commit against the old document, swap `AdoptRuntimeSources` and
`UpdateProvenance` and re-word design §2.7 — a plan change, recorded, not a
silent reorder.

1. `AdoptRuntimeSources` — `session.adopt_runtime_sources()`; on failure, roll
   back the files (the session is already unchanged, per MAR-180).
2. `UpdateProvenance` — `begin_edit` → build `PsdImportProvenance` **from
   `plan.layers`**, in the plan's order, dropping `Missing && !preserve` rows;
   relativize `source_path` and `layers_directory` with `project_relative_path`;
   `commit()`. On failure: restore files, then re-adopt; if the re-adopt fails,
   set `rollback_error` (design §9.2).

**Tests:** R4, R5, R6.

R4 compares overlay vectors **in memory** — never through a save round trip
(`Value::Object` is a `std::map` and normalises order; 17-digit doubles drift).
R5 uses a plan whose `preserve` field the test sets directly, since
`plan_psd_reimport` always sets `true`.

**Inversions:** I8 (omit adoption) → **R0**, with R1/R6 as the semantic
detectors; I9 (candidate order) → R4; I10 (drop `bone_name`) → R4's full-tuple
clause; I11 (ignore `preserve`) → R5.

---

## Task 6 — The agent operation (AC5, first half)

**Files:** `include/marrow/editor/agent_control.hpp`,
`src/editor/agent_dispatch.cpp`, `src/editor/agent_handlers_management.cpp`,
`src/samples/agent_dispatch_smoke.cpp`.

1. `AgentReviewRequest` gains `input_path` and `plan_digest`; carry both through
   `enqueue_review` and `review_to_json`.
2. The `import.psd_layers` branch: project-derived targets with a
   `not_project_bundle` refusal; a whitelist check on `staging_root`; a dry run
   that plans, emits the rows and counts, and removes its staging root.
   `import.spine_json`, `import.spine_atlas` and `atlas.pack` keep today's
   behaviour — **read the shared branch carefully; the four ops share one block.**
3. Update `agent_dispatch_smoke.cpp:1621` and `:4479` to the project's own
   bundle. Their `/tmp` entries leave `reviewed_temp_targets` (`:1343-1349`);
   re-derive that array's contents rather than editing it from memory.

**Tests:** A1–A4, A7. **All A-cases run on a temp copy** (design §9.4): copy the
`.marrow`, `.mskl`, `.matl`, `.png` and the layers directory into a temp tree,
`marrow_editor_project_load` it, `harness.set_project(...)` — the pattern
`exercise_mar186_diagnostics` already uses at `:1398`/`:1400`.

**Inversions:** I16 (dry run stages into the target) → A1; I17 (no staging
whitelist) → A3.

---

## Task 7 — Approval executes (AC5, second half) and MCP parity

**Files:** `include/marrow/editor/agent_dispatch.hpp`,
`src/editor/agent_dispatch.cpp`, `src/editor/shell_agent_panel.cpp`,
`tools/mcp/tools/editing.py`, `tools/mcp/test_client.py`.

1. `apply_agent_review(session, control, review_id)` — design §2.9. Checks
   `allowed`, re-plans into a fresh staging root, compares `plan_digest`, calls
   `commit_psd_reimport`, removes the request.
2. `shell_agent_panel.cpp`: the `executes` predicate gains `ImportOrPack`; the
   branch calls `apply_agent_review` and reports its message. **Delete the
   "not executed; run the CLI importer" text — it becomes false.**
3. MCP: `staging_root` in the schema; `test_client.py` M1–M3.

**Tests:** A5, A6, M1–M3.
**Inversions:** I14 (digest always equal) → A6; I15 (ignore `allowed`) → A6.

**State plainly in the write-up** that nothing tests the button itself
(design §9.1). Optional **Task 7b**, only if the reviewer asks for it: a
`CheckFrameBodies`-style script asserting the `ImportOrPack` branch names
`apply_agent_review`.

---

## Task 8 — Prove the scope did not leak

```sh
# 1. The registry did not move. Expect 66 and an empty diff of the table.
grep -c '^    {"' src/editor/agent_dispatch.cpp
git diff HEAD -- src/editor/agent_dispatch.cpp | grep -E '^\+.*\{"' | grep -v input_path

# 2. No schema change. Expect an EMPTY diff.
git diff --stat HEAD -- docs/root1/format-spec.md

# 3. The importer and the parser are untouched. Expect an EMPTY diff.
git diff --stat HEAD -- src/editor/psd_import.cpp src/editor/atlas_packer.cpp

# 4. The committer is UI-free. Expect NO hits.
grep -rn "imgui\|sokol\|shell_" include/marrow/editor/psd_reimport_commit.hpp \
    src/editor/psd_reimport_commit.cpp src/editor/psd_reimport_commit_internal.hpp

# 5. The guards did not move. Expect 11, split 7/2/2, and 53,55.
grep -rn '!= 66U' src/ | sed 's/:.*//' | sort | uniq -c
grep -n '== 66' tools/mcp/test_client.py
```
Read the output of each. **"No hits" is evidence only because the command ran.**

Then:
- register `marrow_psd_import_smoke` in CTest as `marrow.psd_import_smoke`
  (`WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}`, labels `editor;psd;smoke;noninteractive`);
  `ctest -N` goes 25 → 26 and `ctest` 22/22 → 23/23. **Re-measure; do not
  assume.**
- `AGENTS.md`: one `## Current Validation` bullet, and a
  `## MAR-189 Commit PSD Reimports Atomically Validation Results` section
  carrying the measured baselines, the inversion table with **actual** outcomes,
  the document-errors table (F1–F7 plus anything Task 0 finds), and a "Not
  independently covered" section naming design §9.1, §9.2, §9.3 and §9.5.
- **`AGENTS.md`'s `## Repo facts that outlive their story`: one new entry**,
  *"Byte-identity is the wrong invariant when the bytes encode a path"* — design
  §6.5, written as a durable fact rather than left inside this story's section,
  because the next person to commit a bundle by copying will not be reading about
  PSD reimports. State it with its measured instance (`atlas_packer.cpp:880`
  writes `"image"` from `image_path.filename()`; the staged `.matl` therefore
  says `staged.png`), name **R1b** as the only case that sees it, and name the
  I6 measurement that proves every byte clause passes without it. Phrase the
  claim so refuting it is one command — that is what made four wrong premises in
  this chain cost minutes.
- **Correct no historical measurement.** A number under a `## MAR-NNN` heading is
  that story's record of its own run; the repair for a stale one is a clarifying
  parenthetical naming the commit, never a new value.

---

## Final verification (run all of it, in this order)

```sh
cd "$SCRATCH/tree"
find build/CMakeFiles -name '*.o' -delete
cmake --build build -j8 2>&1 | tee /tmp/$AGENT-final.log
grep -c 'warning:' /tmp/$AGENT-final.log        # against the Task 0 baseline
grep -c 'Wswitch'  /tmp/$AGENT-final.log        # a new enum arm shows up here
ctest --test-dir build -N | tail -3
ctest --test-dir build
./build/marrow_psd_import_smoke
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_agent_dispatch_smoke | grep -c '\[ OK \]'
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
cmake --build build --target marrow_frame_body_check
git status --porcelain assets/fixtures/        # MUST be empty (design §9.4)
```

The last line is not decoration. It is the check that this story did not
overwrite tracked art while proving that it can overwrite art.

---

## Commit

One commit. Korean subject and body. Stage **only your own paths, explicitly
named**; if a path is shared with another in-flight story, stage only your own
hunks with `git apply --cached`. Trailers **contiguous, no blank line between
them**:

```
Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: <the session URL>
```

Copy the session value from the parent: `git log -1 --format='%(trailers)' HEAD`.
Three consecutive stories have shipped without it.
