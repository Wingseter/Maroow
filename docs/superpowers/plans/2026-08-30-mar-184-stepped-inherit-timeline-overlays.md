# MAR-184 Add Stepped Inherit Timeline Overlays — Implementation Plan

Design: `docs/superpowers/specs/2026-08-30-mar-184-stepped-inherit-timeline-overlays-design.md`
Story: `MAR-184`, depends on `MAR-183` (sequencing only).
Branch: `feat/mar-168`. **Baseline commit: `25bf694`** (`feat: MAR-183 최근 프로젝트
목록 영속화와 관리 구현`).

Every `file:line` below was re-measured against the working tree at `25bf694`
while this plan was written. Task 0 re-measures them again before any code.

**Read the design first**, then read §A of this plan, which records where the
design is wrong. In particular design §0.1 (this story draws nothing — it is a
file format plus a merge primitive), §1.8 (the pruning trap), §2.4 (the
validation table), §2.6 (the primitive) and §5 (inversions, several of which
this plan replaces).

## Standing rules for this plan

- **TDD, strictly.** The case goes in first, is **run and seen to fail for the
  stated reason**, and only then is the implementation written. A case that
  passes on its first run is a defect in the case until proven otherwise.
- **Every case must be proven falsifiable.** §B is the inversion register. For
  each entry: apply the named source mutation, run the named binary, record the
  **exact failure text**, restore, `touch` the restored file, rebuild. A case
  with no recorded failure text is not covered by this story and must be said
  so in AGENTS.md.
- **`touch` after every restore.** `AGENTS.md` "Methodology hazards worth
  recording" H1: a `cp` restore landing in the same mtime second makes `make`
  skip the rebuild, and the next run silently exercises the *inverted* binary.
  The final verification runs against a from-scratch `rm -rf build`.
- **Compare recorded messages with `cmp`/`diff` over a whole string** (H2).
  Never hand-slice a line number out of a reference file.
- **A passing `save()` proves nothing.** `validate_project_for_save` takes no
  base document. Only `load_project(path)` — which reaches
  `build_project_runtime` at `src/editor/project.cpp:7532-7533` — materializes a
  project. Every round-trip assertion in this plan goes through the real load
  path.
- When a measurement disagrees with this plan, **the measurement wins.** Record
  the disagreement in the AGENTS.md "Document errors found" table; do not
  silently adapt.
- Never weaken a case to make an inversion bite. Strengthen the case.
- `git add` / `git commit` only at Task 8. No `checkout`, no `stash`.

---

## §A. Where the design spec is wrong

The design's author never built or ran anything (their §9 says so). Eleven
claims were re-verified for this plan. Eight hold exactly; the rest are below,
and this plan is written against the measured tree, not against the spec.

| # | Design says | Measured |
| --- | --- | --- |
| **A1** | Baseline commit `4a4249d` | The tip of `feat/mar-168` is **`25bf694`**. `4a4249d` exists but is not the baseline. The spec's line numbers were nonetheless checked against the tree and are usable with the drift in A6 |
| **A2** | §6.1: "**Two invocations**: the standing `player_idle.marrow` run … and the self-built `skin_inherit_constraints` projects", and §3.5: add "the new project-smoke command line under `## Current Validation`" | **Wrong, and it would produce a silently skipped suite.** Every editing suite in `marrow_project_smoke` lives inside the marker-gated `else` block of `main()` (`src/samples/editor_project_smoke.cpp:14056-14203`). Pointing the binary at a project built over `skin_inherit_constraints.mskl` takes the `markers.present.empty()` branch (`:14063-14072`), prints `Editing-suite validation skipped:` and runs **none** of it. There is **one** invocation. MAR-184's cases build their throwaway projects **inside** the standing `player_idle.marrow` run, exactly as MAR-177 (`:14123-14143`) and MAR-178 (`:14144-14164`) already do. **No new command line is added to `## Current Validation`** — the existing `AGENTS.md:43` line gains a clause |
| **A3** | §6.2 P2 feeds `time: NaN` through a `.marrow` | **Unreachable through a file.** JSON has no `NaN` literal, and `src/runtime/json.cpp:302-307` fails any number whose `strtod` sets `ERANGE`, so `1e400` is a **tokenizer** error (`invalid numeric value`) at a different layer than the case wants. The only non-finite-ish value that reaches `finite_animation_scalar` (`src/editor/authoring.cpp:322-327`) from a `.marrow` is a **magnitude over float32 max**, e.g. `1e39` — a perfectly finite double. NaN and infinity reach the code only through `merge_inherit_timeline`'s `double` parameter, from C++. This plan splits the case: **P2c** uses `1e39` at the parser, **P10d** uses `quiet_NaN()` at the primitive |
| **A4** | §3.2: "A branch in `build_timeline_edits_value` (`:4381`)" | **Understated, and the omission is silent save-side data loss.** `build_timeline_edits_value` takes **six explicit vector parameters** (`src/editor/project.cpp:4381-4387`), and its sole call site gates the entire `timeline_edits` member on a **six-way `!empty()` disjunction** (`:4832-4847`). A seventh parameter **and** a seventh disjunct are both required. Omitting the disjunct means an inherit-only project serializes **no `timeline_edits` at all**. This is inversion **I9**, the highest-value inversion in the story, and the spec names it nowhere |
| **A5** | §2.6 describes an in-place `ensure_*` + insert, and inversion **I4** is "move the collision check to after `ensure_bone_inherit_timeline_edit`" | **I4 cannot bite under the repo's own convention.** `authoring.cpp` uses preflight-then-mutate at **ten** sites (`ProjectData candidate = *project;` … `*project = std::move(candidate);` — `:2074/2123`, `:2146/2213`, `:2277/2443`, `:2463/2537`, `:2779/2846`, `:3120/3179`, `:3198/3286`, `:3865/3950`, `:3967/4128`, `:4225/4265`). With a candidate copy, early mutation of the **candidate** is invisible to `serialize_project(*project)` and the inversion passes. This plan requires the candidate pattern and **redefines I4** as *removing* it |
| **A6** | Assorted `file:line` anchors | Small drift, all recorded: `InheritKeyframe` at `animation.hpp:169` (spec 168); `parse_inherit_timeline` at `skeleton_parse.cpp:1835` (spec 1845); its mode rejection `:1900-1906` (spec 1901-1904); its strictly-increasing rejection `:1906-1913` (spec 1905-1912); the `.mskl` inherit member at `:5294` (spec 5293); the six `ProjectData` edit vectors at `project.hpp:567-573` (spec 566-572, off by one); `find_transform_timeline_edit` at `project.hpp:614/625` (spec 609/619); the `ensure_*` free functions at `project.hpp:777-812` (spec 790-830); `load_project`'s transform parse at `project.cpp:7419` and `parse_loop_sync` at `:7454` (spec 7417/7451); the MAR-172 ordering comment at `:7450-7451` (spec 7448-7449); `build_runtime_document`'s transform loop at `:5418-5426` (spec 5417-5426); `build_tracks`' inherit arm at `timeline_model.cpp:159-165` (spec 158-166); the graph-model `Inherit` arms at `timeline_graph_model.cpp:393` and `:478` (spec 392/477) |
| **A7** | §1.4's table names a `.marrow` parse for slot color at `:2623` and slot attachment at `:2717` | Those are the **keyframes** parsers. There is no `parse_slot_color_timeline_edits` / `parse_slot_attachment_timeline_edits`; one `parse_slot_timeline_edits` (`:2787`) fills both vectors. Harmless — this plan models on `parse_transform_timeline_edits` (`:1846`), which does exist and does have the shape the spec describes |
| **A8** | §2.4 gives the strictly-increasing message as `timeline keyframe times must be strictly increasing` | That is the **runtime's** wording (`skeleton_parse.cpp:1911`). The project convention prefixes the family: `slot attachment timeline edit keyframe times must be strictly increasing` (`project.cpp:2775`). Use the project convention |

### Design claims that were re-verified and **hold exactly**

Do not re-litigate these; Task 0 confirms them and moves on.

- The runtime layer is complete and genuinely stepped. `parse_inherit_timeline`
  (`skeleton_parse.cpp:1835-1918`) reads **exactly** `time` and `inherit`;
  rejects an empty array (`:1849-1856`), an unknown mode (`:1900-1906`) and a
  non-increasing time (`:1906-1913`); and **accepts a negative first key**
  because `has_previous_time` starts `false` (`:1860-1861`).
- `grep -c "nherit" src/editor/project.cpp` → **0**.
  `grep -n "nherit" include/marrow/editor/project.hpp` → exactly one hit,
  `:442`, an unrelated linked-mesh doc comment.
- §1.5's silent drop is real: `transform_channel_from_key` returning `nullopt`
  hits a bare `continue` at `project.cpp:1898-1901`.
- §1.7's fixture inventory is exact. `skin_inherit_constraints.mskl` carries one
  inherit timeline, `toggle_inherit` / `child`, keys
  `0.0 normal`, `0.25 noRotationOrReflection`, `0.5 onlyTranslation`,
  `1.0 normal`. All five of its bones (`root`, `controller`, `child`,
  `constrained`, `cape_target`) omit `inherit`, i.e. setup `Normal`
  (`skeleton.hpp:64` defaults it). Every other `assets/fixtures/*.mskl`,
  `player_idle.mskl` included, has **zero**.
- §1.8's pruning trap is real: `skeleton_animation.cpp:275-279`,
  `has_single_key_at_origin` at `:118-120`, run from `skeleton.cpp:116` inside
  `load_skeleton_data`.
- §1.6 holds. `grep -c "nherit" src/runtime/binary.cpp` → **0**;
  `kBinaryVersionGenericDocument = 1` / `kBinaryVersionPackedAnimations = 2` at
  `:22-23`; the packed section re-emits only `"rotate"` / `"translate"`
  (`:1894-1896`), so `inherit` rides the generic document encoder untouched.
- §1.10's registry baseline is exact: **64** operations;
  `agent_dispatch_smoke.cpp:39` declares `std::array<OperationExpectation, 64>`;
  `tools/mcp/test_client.py:53,55` assert `== 64`; **ten** `!= 64U` guards, at
  `shell_smoke_constraints.cpp:147,676`, `shell_smoke_timeline.cpp:3697`, and
  `shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603`.
- Zero `.marrow` in the tree carries `bones.*.inherit` (all three checked).
- `grep -c "TimelineTrackKind::" src/editor/shell_timeline.cpp` → **0**.
- `format-spec.md` anchors: `.marrow` has no version field (`:660-661`), the
  `timeline_edits` family list is `:869-877`, the `curve_mode`/`curve_driver`
  block is `:881-932`, the unknown-member note is `:929-932`.

---

## §B. Inversion register

The rule this story is held to: **an inversion is valid only if the case it is
attributed to is the *first* thing that catches it.** Where an earlier detector
exists it is named, and the attribution moves.

| # | Mutation | Attributed to | Must fail with | Why nothing earlier catches it |
| --- | --- | --- | --- | --- |
| **I9** | Drop `!project.bone_inherit_timeline_edits.empty()` from the disjunction at `project.cpp:4832-4837` | **P1** | `P1: the reloaded project carries 0 inherit edits, expected 1` | P4 loads `player_idle.marrow`, which has no inherit data. P1 is the first case that saves one |
| **I2** | Weaken the non-negative rejection to `time > -1.0` | **P2a** | `P2a: expected a load error naming '…[0].time', got a successful load` | Nothing else rejects it. The runtime accepts a negative **first** key (`skeleton_parse.cpp:1860-1861`, measured), and no sibling project parser has a non-negative check (`parse_slot_attachment_keyframes`, `:2748-2752`, has none) |
| **I2b** | Delete the `finite_animation_scalar` call, keep `>= 0` | **P2c** | `P2c: expected message '…time: inherit keyframe time must be finite and non-negative', got '…time: number is outside the runtime float32 range'` | **The inversion still produces an error** — `assign_number` (`skeleton_parse.cpp:1076-1090`) refuses `1e39` for the runtime's `float` field during `build_project_runtime`. P2c therefore **must assert the message text**; asserting only `!result` makes this inversion non-biting. This is the trap A3 exposes |
| **I7** | `inherit_mode_from_key` returns `Normal` for an unknown token | **P2d** | `P2d: expected a load error naming 'noScales', got a successful load` | P2a/P2b/P3/P6 use valid tokens only |
| **I1** | Delete the `curve`-member rejection from `parse_inherit_keyframes` | **P6** | `P6: expected a load error naming 'curve', got a successful load` | The parser is the only gate. No sibling keyframe parser rejects unknown members (`parse_slot_attachment_keyframes` ignores them), so nothing pre-existing bites. **P6 must assert the message**, and its project must reference the **real** fixture skeleton — with a bogus skeleton path the load fails later at `load_skeleton_document` (`project.cpp:7524`) and a bare `!result` assertion passes under inversion |
| **I11** | In `ensure_bone_inherit_timeline_edit`, skip copying the base track's keys (always create an empty edit) | **P13** | `P13: ensure on 'child' produced 0 keyframes, expected the base track's 4` | P13 is the first `ensure` case |
| **I3** | Delete the inherit branch from `build_runtime_document` | **P7** | `P7: the materialized animation has no inherit timeline for 'controller'` | P1's assertions read the reloaded `ProjectData`, not the skeleton; P13 reads the project vector. P7 is the first case that reads a **materialized `SkeletonData`**. *(The design attributed this to P8; P7 fires first. Recorded, not "fixed")* |
| **I3b** | In the `build_runtime_document` inherit branch, replace the animation's whole `bones` object instead of assigning into it | **P8** | `P8: 'child' lost its base inherit timeline; the animation has 1 inherit timeline, expected 2` | P7 asserts only its own bone's row and passes under this mutation. P8 is the only case with a base-backed sibling to lose |
| **I6** | Remove the empty-edit skip from `build_runtime_document` | **P9a** | `P9a: materialization failed: $.animations.toggle_inherit.bones.controller.inherit: inherit timeline must contain at least one keyframe` | P13 reads the project layer; P7/P8 carry non-empty edits |
| **I6b** | Remove the empty-edit skip from `build_timeline_edits_value` | **P9b** | `P9b: reload failed: $.timeline_edits.animations.toggle_inherit.bones.controller.inherit: inherit timeline edits must contain at least one keyframe` | P9a materializes without saving and passes; only P9b saves an empty edit to disk |
| **I5** | Sort the merged keys **descending** | **P5** | `P5: stored times are not strictly increasing: 0.5, 0.3, 0.1` | Both request orders still agree byte-for-byte under a descending sort, so P5's **byte-identity half does not bite** — the strictly-increasing half is the biting half, and P5 asserts in memory without a reload precisely so the runtime's own guard cannot stand in for it |
| **I7c** | In `merge_inherit_timeline`, `continue` past an unknown mode token instead of returning an error | **P10c** | `P10c: expected an error naming 'noScales', got changed=true` | P2d catches the **parser's** table; this catches the **primitive's** use of it. Different code path, different case |
| **I4** | Remove the candidate copy: call `ensure_bone_inherit_timeline_edit(project, …)` directly and hoist it above the collision check | **P10c** | `P10c: the rejected merge changed serialize_project() (28451 → 28613 bytes)` | P10a and P10b name an unknown animation / bone, for which `ensure` returns `nullptr` and writes nothing, so they pass under the mutation. P10c is the first rejection whose animation **and** bone both resolve. **P11 is a second detector and is recorded as over-determined** |
| **I4b** | In the `replace_existing_times` overwrite branch, write the **requested** time onto the existing key instead of keeping the stored one | **P11** | `P11: the replaced key drifted to 0.2500001, expected the stored 0.25` | Nothing else exercises the replace arm |
| **I13** | Change `kBinaryVersionPackedAnimations` to `3` (`binary.cpp:23`) | **P12** | `P12: .mbin version is 3, expected 2` | The `.mskl` half asserts `version == 1` from the copied base document and is unaffected |
| **I8** | Add `Inherit` to `track_is_editable` (`timeline_model.cpp:240-248`) | **Task 6's diff** | `git diff --stat -- src/editor/timeline_model.cpp` is non-empty | **Not a test.** This is a scope check, and the plan says so rather than calling a diff "coverage" |

**P12 has no story-owned inversion beyond I13**, and **P4 has none at all.**
P4 is a non-effect witness (an unchanged project must serialize to unchanged
bytes) and P12's equivalence half rides pre-existing generic-codec code that
this story does not touch. Both facts go in AGENTS.md's "Not independently
covered" verbatim. Do not invent an inversion to fill the row.

---

## Task 0 — Measure, before any code

**Depends on:** nothing. **No file is modified.** Every step prints a value; a
value that disagrees with §A is a stop-and-record.

### 0.1 Baseline build and test inventory

```bash
git rev-parse HEAD                      # expect 25bf694
git status --short                      # expect only the untracked design doc
cmake -S . -B build
cmake --build build
ctest --test-dir build -N | tail -3
```

Record the `ctest -N` total. This story registers **no** new CTest target, so
the number must be identical at Task 7.

### 0.2 The registry and the count-sweep baseline

```bash
grep -c '^\s*{\s*"' src/editor/agent_dispatch.cpp
grep -n "OperationExpectation, 64" src/samples/agent_dispatch_smoke.cpp
grep -rn "!= 64U" src/ | wc -l
grep -n "== 64" tools/mcp/test_client.py
```

Expected **64 / 1 array at `:39` / 10 guards / 2 python assertions**.

**MAR-184 changes the registry not at all**, so the correct diff for every one
of these files is **empty** (Task 6). Never blind-substitute `64`: `AGENTS.md`
carries `t = 0.62`-shaped numbers and self-referential line counts, and the
tree carries `IM_COL32(56, 61, 69, 255)`, `(51, 56, 64)`, `rgb(54,57,64)`,
`"x": 56.0`, `56,995,840`, `shell_smoke_graph.cpp:2977`'s `4364U`,
`shell_smoke_viewport.cpp:3504`'s `640`, and `test_client.py:1484`'s ordinal.

### 0.3 The non-effect witness

```bash
./build/marrow_project_smoke assets/fixtures/player_idle.marrow > /tmp/mar184-smoke-before.txt 2>&1
shasum -a 256 assets/fixtures/player_idle.marrow
```

Then, in a two-line probe (or the first thing P4 prints), record the **byte
length and SHA-256 of `serialize_project(load_project("assets/fixtures/player_idle.marrow").project)`**.
P4 compares against these at the end.

### 0.4 The fixture facts

```bash
python3 -m json.tool assets/fixtures/skin_inherit_constraints.mskl > /dev/null
python3 -c "
import json; d=json.load(open('assets/fixtures/skin_inherit_constraints.mskl'))
print([(b['name'], b.get('inherit','<absent>')) for b in d['bones']])
print(d['animations']['toggle_inherit']['bones']['child']['inherit'])
"
```

Expect five bones all with `<absent>` inherit, and the four keys from §A.
Confirm `player_idle.mskl` still has zero inherit timelines.

### 0.5 **M-gate: the pruning trap.** The single most consequential measurement

Write a throwaway probe (delete it before Task 1 — it is a measurement, not a
test) that builds a project over `skin_inherit_constraints.mskl` carrying a
**single** hand-constructed inherit overlay `{time: 0.0, inherit: normal}` on
`controller`, materializes it through `build_project_runtime`, and prints
whether `find_animation("toggle_inherit")->find_inherit_timeline(controller)`
is null.

Since the overlay type does not exist yet, do this at the JSON layer instead:
take `assets/fixtures/skin_inherit_constraints.mskl`, add
`animations.toggle_inherit.bones.controller.inherit = [{"time":0.0,"inherit":"normal"}]`,
write it to `/tmp/mar184-prune-probe.mskl`, and run:

```bash
./build/marrow_inspect /tmp/mar184-prune-probe.mskl | grep -i inherit
```

**Expected: the `controller` timeline is ABSENT** — pruned by
`skeleton_animation.cpp:275-279` because one key at `t=0` with mode `normal`
equals `controller`'s setup inherit.

**If it is present, §1.8 is wrong**, the test data in Tasks 3-5 can be
simplified, and this is a stop-and-record before writing any case. Every
project-only overlay in this plan is pruning-safe on the assumption that the
gate confirms the trap: **`{0.0, noScale}` + `{0.4, normal}`**, two keys,
neither of which is a lone origin key matching setup.

### 0.6 The runtime witness

```bash
./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
sed -n '1883,2050p' src/samples/runtime_fixture_smoke.cpp
```

Answer in writing: **does `validate_runtime_inherit_timeline_and_skin_constraints`
exercise all five modes?** It samples `toggle_inherit` at `0.0`, `0.25`, `0.5`
and `1.0` (`:1989-2023`), and the fixture's four keys carry only `normal`,
`noRotationOrReflection` and `onlyTranslation` — so `noScale` and
`noScaleOrReflection` are almost certainly **not** exercised there.

**Do not widen it.** AC6's "all five modes" is met by **P3** at the project
layer, which is where this story's code lives, and the fixture is asserted on
by other suites (`shell_smoke_constraints.cpp:108-131` documents its quirks).
Record the answer and move on; `runtime_fixture_smoke` runs as an unchanged
regression witness.

### Task 0 exit criteria

- [ ] `ctest -N` total recorded.
- [ ] Registry: 64 / 1 / 10 / 2 confirmed.
- [ ] `serialize_project(player_idle.marrow)` length + SHA recorded.
- [ ] Fixture facts confirmed (5 bones, 4 keys, `controller` untracked).
- [ ] **M-gate 0.5 answered**, in writing, with the `marrow_inspect` output pasted.
- [ ] 0.6 answered in writing.
- [ ] The probe `.mskl` deleted; `git status --short` unchanged from 0.1.

---

## Task 1 — The `.marrow` schema: structs, mode table, parser, serializer

**Depends on:** Task 0.

Files: `include/marrow/editor/project.hpp`, `src/editor/project.cpp`,
`src/samples/editor_project_smoke.cpp`. **No new translation unit, and
therefore no `CMakeLists.txt` change** (`marrow_project_smoke` is declared at
`CMakeLists.txt:811-823` and its source list does not move).

### 1.1 Test first — `src/samples/editor_project_smoke.cpp`

Add one function, `validate_mar184_inherit_overlays(const ProjectLoadResult&)`,
following the file's shape (`validate_mar178_scenario_*`,
`validate_mar179_constraint_parameters`). Register it in `main()`'s
**marker-gated `else` block**, after `validate_mar180_lifecycle_refuses_active_transaction`
(`:14203-14206`). Per §A2 this is the only place it can run.

Its throwaway projects follow the proven pattern at
`editor_project_smoke.cpp:11763-11790` (`mar178_open_skin_session`):
`MinimalProjectOptions` with
`skeleton_path = absolute("assets/fixtures/skin_inherit_constraints.mskl")`,
`atlas_paths = { absolute("assets/fixtures/player_idle.matl") }` — the fixture
ships no atlas and `load_project` requires at least one — into a scratch
directory under the system temp dir, with
`active_animation = "toggle_inherit"`.

Add, in this order (order matters for §B's attributions):

**P4 — the old-project non-effect witness.** Over the `result` `main()` already
holds for `player_idle.marrow`:
1. `result.project->bone_inherit_timeline_edits.empty()`.
2. `serialize_project(*result.project)` has exactly the byte length and SHA-256
   recorded in Task 0.3.
Print both numbers every run. *(AC2. No inversion — §B.)*

**P1 — save → load round trip.** Author a two-key overlay
`{0.0, NoScale}, {0.4, Normal}` on `controller` directly into
`ProjectData::bone_inherit_timeline_edits`, `save_project`, then
**`load_project(path)`**. Assert the reloaded project carries exactly one
`BoneInheritTimelineEdit`, with `animation_name == "toggle_inherit"`,
`bone_name == "controller"`, two keyframes, times equal to `0.0` and `0.4`
within `1e-9`, and modes `NoScale` and `Normal`. *(AC1, AC6 save/reload. I9.)*

**P2 — parser rejections.** Four separate `.marrow` documents, each written to
disk with a **valid** `runtime.skeleton_path` pointing at the real fixture, each
loaded with `load_project(path)`. For each, assert `!result` **and** that
`result.error->message` contains both the offending JSON path and the expected
message text. `LoadError::message` is `"<json_path>: <message>"`
(`src/runtime/json.cpp:962`), so a `find(...) != npos` on each half is the
assertion.

| Sub-case | Document | Expected `message` substrings |
| --- | --- | --- |
| P2a | one key, `"time": -0.5` | `.bones.controller.inherit[0].time` and `inherit keyframe time must be finite and non-negative` |
| P2b | two keys at `0.4` then `0.2` | `.inherit[1].time` and `inherit timeline edit keyframe times must be strictly increasing` |
| P2c | one key, `"time": 1e39` | `.inherit[0].time` and `inherit keyframe time must be finite and non-negative` |
| P2d | one key, `"inherit": "noScales"` | `.inherit[0].inherit` and `inherit mode must be one of normal, onlyTranslation, noRotationOrReflection, noScale, or noScaleOrReflection` |

Also assert the empty-array rejection (`"inherit": []` →
`inherit timeline edits must contain at least one keyframe`) as **P2e**.

*(AC1. I2 → P2a, I2b → P2c, I7 → P2d.)*

**P3 — all five modes.** One bone, five keys at `0.0, 0.1, 0.2, 0.3, 0.4`
carrying `Normal, OnlyTranslation, NoRotationOrReflection, NoScale,
NoScaleOrReflection` in that order. `save_project` → `load_project(path)` →
read `result.skeleton_data`. Assert all five survive as the matching
`runtime::BoneInherit` **in the materialized skeleton**, in order. *(AC6 "all
five modes". Catches a mode table that broke a real token — the other direction
from I7.)*

**P6 — the curve rejection.** A `.marrow` whose inherit key carries
`"curve": [0, 0, 1, 1]` beside `time` and `inherit`. Assert `!result` **and**
`message` contains `.inherit[0]` and
`inherit keys are stepped and must not carry curve data`. The project's
`runtime.skeleton_path` **must** point at the real fixture (see §B I1).
*(AC2 "continuous curve data rejected". I1.)*

### 1.2 Watch it fail

```bash
cmake --build build --target marrow_project_smoke
```

It must fail **to compile** — `BoneInheritTimelineEdit` does not exist. That is
the correct first failure for a new type.

### 1.3 Implement — `include/marrow/editor/project.hpp`

- `struct InheritKeyframeEdit { double time{0.0}; runtime::BoneInherit inherit{runtime::BoneInherit::Normal}; };`
  beside `SlotAttachmentKeyframeEdit` (`:263`).
- `struct BoneInheritTimelineEdit { std::string animation_name; std::string bone_name; std::vector<InheritKeyframeEdit> keyframes; };`
  beside `SlotAttachmentTimelineEdit` (`:268`).
  **No `loop_sync`** — a stepped lane has no boundary easing, and only
  transform, deform and slot color carry it. **No `curve_mode` / `curve_driver`** —
  `format-spec.md:925-928` already states the discrete families carry none.
- `std::vector<BoneInheritTimelineEdit> bone_inherit_timeline_edits;` on
  `ProjectData`, **immediately after `transform_timeline_edits` (`:567`)**, so
  the member order mirrors the runtime's own bone-track ordering.
- `find_bone_inherit_timeline_edit(animation, bone)`, const and mutable, beside
  `find_transform_timeline_edit` (`:614/625`).
- `std::optional<runtime::BoneInherit> inherit_mode_from_key(std::string_view);`
  and `std::string_view inherit_mode_json_key(runtime::BoneInherit);` declared
  at namespace scope — the merge primitive in Task 4 needs them, so unlike
  `transform_channel_json_key` they cannot stay anonymous.

### 1.4 Implement — `src/editor/project.cpp`

- **One bidirectional table**, in the anonymous namespace beside
  `transform_channel_json_key` (`:1415`) and `path_spacing_mode_json_key`
  (`:1469`), with the namespace-scope wrappers forwarding to it. The five tokens
  are exactly the runtime's (`skeleton_parse.cpp:494-511`).
  **Do not reach into `skeleton_parse.cpp`'s `parse_bone_inherit`** — it is in an
  anonymous namespace, has no inverse, and linking would fail; the risk is a
  duplicated one-way table that drifts.
- `parse_inherit_keyframes(...)`, modelled line-for-line on
  `parse_slot_attachment_keyframes` (`:2717-2783`), implementing design §2.4's
  table with A8's message convention:
  - `require_type` Array; empty → `inherit timeline edits must contain at least one keyframe`;
  - per keyframe: `require_type` Object; `read_required_number` for `time` into
    a `double`;
  - `if (!finite_animation_scalar(keyframe.time) || keyframe.time < 0.0)` →
    `inherit keyframe time must be finite and non-negative` at
    `keyframe_path + ".time"`. Reuse `finite_animation_scalar`
    (`src/editor/authoring.cpp:322`) rather than open-coding `std::isfinite` —
    it also bounds by float32 max, which is the only reachable non-finite arm
    from a file (§A3);
  - `require_member` String for `inherit`, then `inherit_mode_from_key`;
    `nullopt` → the five-token message at `keyframe_path + ".inherit"`;
  - `find_optional_member(keyframe_value, "curve") != nullptr` →
    `inherit keys are stepped and must not carry curve data` at
    `keyframe_path + ".curve"`;
  - strictly increasing, message per A8.
- `parse_bone_inherit_timeline_edits(...)`, modelled on
  `parse_transform_timeline_edits` (`:1846-1919`): same
  `timeline_edits.animations.<a>.bones.<b>` walk, picking the `inherit` member
  only. It must **skip** the four transform channel keys rather than erroring —
  exactly as the transform parser skips `inherit` today.
- `build_inherit_keyframes_value(const BoneInheritTimelineEdit&)` beside
  `build_slot_attachment_keyframes_value` (`:4184`). **One builder serves both
  `.marrow` and `.mskl`**; inherit has no project-only member to strip, so it
  needs no `build_runtime_*` variant.
- **`build_timeline_edits_value` (`:4381`): a seventh parameter and a seventh
  loop**, after the transform loop. **Skip an edit whose `keyframes` is empty**
  (design §2.10; inversion I6b).
- **Its call site (`:4832-4847`): a seventh `!empty()` disjunct and a seventh
  argument.** This is §A4 and inversion I9. Gate on
  `std::any_of(bone_inherit_timeline_edits, [](const auto& e){ return !e.keyframes.empty(); })`
  rather than on `!vector.empty()`, so an all-empty inherit vector cannot make a
  project that previously wrote no `timeline_edits` start writing
  `"timeline_edits": {"animations": {}}`.
- **The `load_project` call**, immediately after
  `parse_transform_timeline_edits`'s block ends at `:7423` and **before**
  `parse_loop_sync` (`:7454`). The comment at `:7450-7451` states the invariant —
  every `timeline_edits` parser runs before loop-sync cross-references them.
  Inherit authors no loop-sync leaf, but the invariant must not be weakened.
- `find_bone_inherit_timeline_edit` definitions beside
  `ProjectData::find_transform_timeline_edit` (`:6709/6724`).

### 1.5 Verify

```bash
cmake --build build
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
grep -c "nherit" src/editor/project.cpp        # was 0; now non-zero, expected
```

P4, P1, P2a-e, P3, P6 green.

### 1.6 Inversions for this task

**I9, I2, I2b, I7, I1** from §B. Run each, record the exact failure text,
restore, **`touch`**, rebuild, confirm green.

---

## Task 2 — The `ensure_*` accessor

**Depends on:** Task 1.

Files: `include/marrow/editor/project.hpp`, `src/editor/project.cpp`,
`src/samples/editor_project_smoke.cpp`.

### 2.1 Test first — P13

Over a fresh project on the fixture, with the effective skeleton from
`load_project(path).skeleton_data`:

1. `ensure_bone_inherit_timeline_edit(project, skeleton, "toggle_inherit", "child")`
   returns non-null, and the created edit carries **4** keyframes with the
   fixture's exact times (`0.0, 0.25, 0.5, 1.0`) and modes (`Normal,
   NoRotationOrReflection, OnlyTranslation, Normal`). *(I11.)*
2. A second call returns the **same pointer** and does not append a second edit
   (`bone_inherit_timeline_edits.size() == 1`).
3. `ensure_..."controller"` (no base track) returns non-null with **zero**
   keyframes, and `size() == 2`.
4. `ensure_..."nosuchbone"` returns `nullptr` and appends nothing.
5. `ensure_...("nosuchanim", "child")` returns `nullptr` and appends nothing.

### 2.2 Watch it fail

Compile failure: the function does not exist.

### 2.3 Implement

`ensure_bone_inherit_timeline_edit(ProjectData&, const runtime::SkeletonData&,
std::string_view animation_name, std::string_view bone_name)` beside
`ensure_slot_attachment_timeline_edit` (`src/editor/project.cpp:7160-7186`),
declared beside its five siblings at `project.hpp:777-812`. Copy that
function's structure exactly: return an existing edit; otherwise resolve
`find_bone_index` and `find_animation`, returning `nullptr` when either fails;
otherwise materialize the base track's keys (via
`animation->find_inherit_timeline(*bone_index)`), widening
`AnimationScalar` → `double`, and push.

**Note for MAR-185, in a code comment:** the effective skeleton handed here has
already been through `prune_constant_timelines`, so a base track that was a
single constant origin key is legitimately absent and materializes as empty.

### 2.4 Verify + inversion

```bash
cmake --build build && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

Run **I11**. Record the failure text.

---

## Task 3 — Materialization

**Depends on:** Task 2.

Files: `src/editor/project.cpp`, `src/samples/editor_project_smoke.cpp`.

### 3.1 Test first

**P7 — a project-only overlay materializes.** Overlay `{0.0, NoScale},
{0.4, Normal}` on `controller`. `save_project` → `load_project(path)`. Assert
`result.skeleton_data->find_animation("toggle_inherit")->find_inherit_timeline(controller_index)`
is non-null with two keys at `0.0` and `0.4` (float32 tolerance) and modes
`NoScale`, `Normal`. Then feed `result.skeleton_data`'s animation to
`timeline_model::build_tracks` and assert a `TrackRow` of kind
`TimelineTrackKind::Inherit` exists for that bone carrying those key times.
*(AC3; design §2.9. I3.)*

**This is a UI-free model-layer assertion, not a frame smoke.** It proves the
row is *produced* by a pure function over `SkeletonData`. It does **not** prove
the dopesheet draws a pixel. Say exactly this in AGENTS.md; do not describe it
as UI coverage.

**P8 — unrelated animation data survives.** Same overlay on `controller`, in a
project that also carries an unrelated transform overlay on `root`. Materialize
and assert:
1. `toggle_inherit` has **two** inherit timelines — `child`'s untouched base
   track (4 keys, exact fixture times and modes) **and** `controller`'s new one.
2. The `root` transform track is present and unchanged.
3. Every other bone in `toggle_inherit` that had a base timeline still has it.
*(AC3 "without losing unrelated animation data". I3b.)*

**P9 — the empty-edit guards.**
- **P9a**: `ensure_bone_inherit_timeline_edit` for `controller` (no base track),
  **no merge**, then `build_project_runtime` in memory. Assert it **succeeds**
  and that `controller` has no inherit timeline. *(I6.)*
- **P9b**: the same project, `save_project` → `load_project(path)`. Assert the
  written `.marrow` text contains **no** `"inherit"` member under
  `bones.controller`, and that the reload succeeds. *(I6b.)*

### 3.2 Watch it fail

P7 fails first, with the animation carrying no inherit timeline.

### 3.3 Implement

In `build_runtime_document` (`src/editor/project.cpp:5335`), after the transform
loop (`:5418-5426`):

```cpp
for (const BoneInheritTimelineEdit& edit : project.bone_inherit_timeline_edits) {
    if (edit.keyframes.empty()) {
        continue;                       // design §2.10; inversion I6
    }
    Value* animation_value = ensure_object_member(animations, edit.animation_name);
    Value* bones_value = ensure_object_member(animation_value, "bones");
    Value* bone_value = ensure_object_member(bones_value, edit.bone_name);
    if (bone_value != nullptr) {
        bone_value->as_object()["inherit"] = build_inherit_keyframes_value(edit);
    }
}
```

`ensure_object_member` is what makes P8 pass: it reaches into the copied base
document rather than replacing it, so sibling bones and channels survive.

### 3.4 Verify + inversions

**I3 → P7, I3b → P8, I6 → P9a, I6b → P9b.** Four inversions, four recorded
failure texts.

---

## Task 4 — `merge_inherit_timeline`

**Depends on:** Task 3.

Files: `include/marrow/editor/authoring.hpp`, `src/editor/authoring.cpp`,
`src/samples/editor_project_smoke.cpp`.

### 4.1 Test first

**P5 — determinism.** Into an empty overlay on `controller`, merge
`{0.5, "noScale"}, {0.1, "normal"}, {0.3, "onlyTranslation"}`. Separately, into
a second identical project, merge the same three **in reverse order**. Assert:
1. Both `serialize_project` results are **byte-identical**.
2. The stored times are **strictly increasing** (`0.1, 0.3, 0.5`), asserted on
   the in-memory vector, with **no save and no reload** — so the runtime's own
   strictly-increasing guard cannot stand in for this assertion (§B I5).
3. `added_key_count == 3`, `replaced_key_count == 0`,
   `effective_key_count == 3`.
*(AC4 determinism. I5.)*

**P10 — validation before mutation.** Four rejections, each asserting
`result.changed == false` and a `result.error` containing the offending value:

| Sub-case | Request | `error` must name |
| --- | --- | --- |
| P10a | `animation_name = "nosuchanim"` | `nosuchanim` and `does not exist` |
| P10b | `bone_name = "nosuchbone"` | `nosuchbone` and `does not exist` |
| P10c | valid animation + bone, `mode = "noScales"` | `noScales` |
| P10d | valid, `time = std::numeric_limits<double>::quiet_NaN()` | `finite and non-negative` |

**P10c additionally captures `serialize_project` before and after and asserts
byte-identity.** P10a and P10b deliberately do **not** — for them `ensure` would
write nothing anyway, so a byte-identity assertion there would be a check that
cannot fail. P10c is the first rejection whose animation **and** bone both
resolve, and is therefore the exclusive first detector of **I4** (§B).
*(AC4. I7c and I4 → P10c.)*

**P11 — collision, both arms.** On `child`, which has a 4-key base track and
**no project edit yet** — this is load-bearing: with an edit already present,
`ensure` is a no-op and I4 could not bite.
1. `replace_existing_times = false`, one key at `0.25` (colliding with the base
   key). Assert `!result`, `error` contains `a key already exists at time` and
   `0.25`, `changed == false`, and `serialize_project` is byte-identical
   before/after. *(Second detector for I4; recorded as over-determined.)*
2. Then the same merge with `replace_existing_times = true` and time
   `0.2500001` (inside the `1e-6` window), mode `NoScale`. Assert
   `result.changed == true`, `replaced_key_count == 1`, `added_key_count == 0`,
   `effective_key_count == 4`, the key's mode is now `NoScale`, and **its stored
   time is still exactly `0.25`, not `0.2500001`**. *(I4b.)*
3. A third merge, `replace_existing_times = true`, requesting exactly what is
   now stored. Assert `result.changed == false` with an **empty** `error`, and
   `serialize_project` byte-identical. *(Design §2.6's no-op contract, which
   MAR-185's transaction layer depends on to avoid empty undo entries.)*

**P12 lives in Task 5.**

### 4.2 Watch it fail

Compile failure: `merge_inherit_timeline` does not exist.

### 4.3 Implement — `include/marrow/editor/authoring.hpp`

Declared after `scale_keyframe_times` (`:555-560`):

```cpp
/** @brief One requested stepped inherit key, in its wire form. */
struct InheritKeyRequest {
    double time{0.0};
    std::string mode;   ///< One of the five tokens; validated by the primitive.
};

struct InheritTimelineMergeRequest {
    std::string animation_name;
    std::string bone_name;
    std::vector<InheritKeyRequest> keys;   ///< Any order; the result is sorted.
    /// When false (default) a key landing within 1e-6 of an existing key is an
    /// error. When true it overwrites that key's MODE in place, keeping the
    /// STORED time so the 1e-6 window can never drift a key.
    bool replace_existing_times{false};
};

struct InheritTimelineMergeResult : AuthoringResult {
    std::size_t added_key_count{0U};
    std::size_t replaced_key_count{0U};
    std::size_t effective_key_count{0U};
};

InheritTimelineMergeResult merge_inherit_timeline(
    ProjectData* project,
    const runtime::SkeletonData& effective_skeleton,
    const InheritTimelineMergeRequest& request);
```

`AuthoringResult` is the established base (`authoring.hpp:18-24`);
`TimelineRetimeResult` (`:128`) and `TimelineScaleResult` (`:498`) are the
precedents for extending it.

The request carries the mode as a **string** because AC4 requires the primitive
to report "invalid modes" — with a typed `BoneInherit` parameter an invalid mode
is unrepresentable and the criterion could not be met at this layer. The
precedent is `upsert_lip_sync_mapping` (`authoring.hpp:70`), which takes JSON in
and stores typed data.

### 4.4 Implement — `src/editor/authoring.cpp`

**Preflight-then-mutate, using the repo's candidate pattern** (§A5). Ten sites
in this file already do it; copy the shape at `:2074/2123`.

```
1.  project == nullptr                    -> missing_project_result()-shaped error
2.  request.keys.empty()                  -> "inherit merge requires at least one key"
3.  find_animation(name) == nullptr       -> "animation '<name>' does not exist"
4.  find_bone_index(name) empty           -> "bone '<name>' does not exist"
5.  per key, request order, first wins:
      !finite_animation_scalar(t) || t<0  -> "key time must be finite and non-negative"
      inherit_mode_from_key(mode) empty   -> the five-token message, quoting the token
6.  collision WITHIN the request (sorted copy, 1e-6)
                                          -> "requested keys collide at time <t>"
7.  collision against the EFFECTIVE timeline, only when !replace_existing_times
                                          -> "a key already exists at time <t>"
--- everything above touches nothing ---
8.  ProjectData candidate = *project;
9.  ensure_bone_inherit_timeline_edit(candidate, effective_skeleton, ...)
10. insert or overwrite each key, keeping the vector sorted ascending;
    overwrite is MODE-ONLY, the stored time is kept
11. counts; if nothing changed, return {changed=false} WITHOUT assigning
12. *project = std::move(candidate);
```

Step 6 runs on a **sorted copy** so the reported time is deterministic
regardless of request order. Step 7 needs the effective timeline: the base
track's keys plus any keys already in the project edit — read it the same way
`ensure` materializes, without calling `ensure`.

Use `inherit_mode_from_key` from `project.hpp` — **do not duplicate the table**
(design R9). Use `find_keyframe_near_time`'s `1e-6`
(`project.hpp:145-169`), the same epsilon `timeline_model::kKeyTimeEpsilon`
(`timeline_model.hpp:20`) uses.

**Note in a comment:** after a successful merge the caller's
`effective_skeleton` is stale and must be rebuilt. MAR-185's transaction layer
owns that; MAR-184 has no caller.

### 4.5 Verify + inversions

**I5 → P5, I7c → P10c, I4 → P10c, I4b → P11.** Four inversions. For I4, the
mutation is: delete the `candidate` copy and call
`ensure_bone_inherit_timeline_edit(*project, …)` hoisted above step 7.

---

## Task 5 — Runtime export

**Depends on:** Task 4.

Files: `src/samples/editor_project_smoke.cpp` only. **No production code
changes in this task** — export rides `build_runtime_document`, which Task 3
already extended, and `.mbin` rides the generic document codec, which this story
does not touch (§A verified: zero `inherit` in `binary.cpp`).

### 5.1 Test — P12

Build a project whose `child` overlay is base-backed and merged (via
`merge_inherit_timeline`, so this also exercises the primitive end to end), then
call `export_runtime_assets` (`project.hpp:1102-1105`) with both
`skeleton_output_path` and `binary_output_path` into a scratch directory.
Assert:

1. `load_skeleton_data` on the exported `.mskl` yields the merged timeline —
   same key count, times equal within float32 tolerance, modes identical.
2. `load_skeleton_data` on the exported `.mbin` yields the same, compared the
   same way.
3. The exported `.mskl`'s `version` member is **1**.
4. The exported `.mbin`'s version varint is **2**. *(I13.)*

**Float32 tolerance is mandatory, not optional.** Project time is `double`;
`AnimationScalar` is `float` (`animation.hpp:14`). A project time not exactly
representable in float32 will not compare equal after materialization. The
existing `.mbin` narrowing cases (AGENTS.md, MAR-179 line) are the precedent.

`marrow_inspect --compare` is the second, external witness for AC5 and runs in
Task 7 — but it runs over `player_idle`, which carries no inherit data, so it is
a **regression witness only** and P12 is the actual coverage. Say so.

### 5.2 Verify + inversion

**I13 → P12.** Record the failure text.

---

## Task 6 — Prove the scope did not leak

**Depends on:** Tasks 1-5. Not an assertion — a diff.

```bash
git diff --stat -- src/editor/agent_dispatch.cpp src/editor/agent_handlers_*.cpp \
                   src/samples/agent_dispatch_smoke.cpp tools/
```

Must be **empty**. MAR-185 owns `set_inherit_keyframe` and
`remove_inherit_keyframe` by name in its own acceptance criteria; MAR-184 adds
no agent operation and no MCP tool. `agent_operation_descriptor_count()` stays
**64** and all ten `!= 64U` guard sites stay untouched.

```bash
git diff --stat -- src/editor/shell_*.cpp src/editor/shell_*.hpp \
                   src/editor/timeline_controller.cpp \
                   src/editor/timeline_graph_model.cpp \
                   src/editor/timeline_model.cpp src/editor/timeline_model.hpp \
                   src/editor/session.cpp \
                   include/marrow/c/ src/c/ include/marrow/runtime/ src/runtime/ \
                   assets/fixtures/ CMakeLists.txt
```

Must be **empty**.

- `timeline_model.cpp` in particular: adding `Inherit` to `track_is_editable`
  (`:240-248`) would open editing without any of MAR-185's selection, retime,
  clipboard, cascade or undo work behind it — a lane the user can click that
  then behaves incorrectly. This is inversion **I8**.
- `timeline_graph_model.cpp`: MAR-167 excluded `Inherit` from the scalar graph
  deliberately (`:393`, `:478`) because `BoneInherit` is an unordered enum with
  no numeric axis. Nothing here revisits that.
- `session.cpp`: `animation_timing_equal` already includes
  `bone_inherit_timelines` (`:125-126`), so preview and selection reconciliation
  see an inherit timing change the moment one exists. An empty diff is the proof
  that no change was needed.
- `assets/fixtures/`: `marrow_project_smoke` **aborts** on a partial marker
  match (`editor_project_smoke.cpp:14074-14084`), and `player_idle` is asserted
  on by a dozen suites. Never edit a fixture.
- `CMakeLists.txt`: no new translation unit, so no target changes.

Then the count sweep:

```bash
git diff -U0 -- src/ tools/ CMakeLists.txt | grep -E '^[+-]' | grep -E '\b(62|63|64|65)\b'
```

Every returned line is inspected by hand and explained in the AGENTS.md entry.
A bare `64` also matches `t = 0.64` and "64 lines"; never blind-substitute.

Finally, run **I8**: add `Inherit` to `track_is_editable`, confirm the first
`git diff --stat` above becomes non-empty, restore, `touch`.

---

## Task 7 — Full verification

**Depends on:** Tasks 1-6. Run every command, record every output.

```bash
rm -rf build                                   # H1: the from-scratch rebuild
cmake -S . -B build
cmake --build build

# Model layers -- unchanged by this story, run as regression witnesses
./build/marrow_unit_tests
./build/marrow_timeline_model_tests
./build/marrow_timeline_graph_model_tests
./build/marrow_selection_tests
./build/marrow_preference_tests
./build/marrow_viewport_interaction_tests
./build/marrow_windowing_tests
./build/marrow_pen_input_tests
./build/marrow_agent_socket_tests

# Runtime -- the inherit fixture's existing coverage, unchanged
./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
python3 -m json.tool assets/fixtures/skin_inherit_constraints.mskl > /dev/null

# THIS STORY'S COVERAGE -- P1-P13, inside the standing invocation (see §A2)
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke --create /tmp/mar184_created.marrow
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
  --export-runtime /tmp/player_idle_project_export.mskl \
  --export-binary  /tmp/player_idle_project_export.mbin
./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin \
                                 /tmp/player_idle_project_export.mskl

# The registry -- must be identical to Task 0
./build/marrow_agent_dispatch_smoke

# The shell -- regression only; C4-C25 must still pass
MARROW_CONFIG_HOME=/tmp/mar184-cfg ./build/marrow_editor_shell \
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

MCP parity — the registry is unchanged, so this must match Task 0 exactly:

```bash
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
python3 -m py_compile tools/mcp/server.py tools/mcp/test_client.py \
                      tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
```

**Do not run `./build/marrow_editor_shell` without `--auto-close`**, and do not
create `~/Library/Application Support/Marrow`.

### Full verification checklist

| # | Check | How |
| --- | --- | --- |
| 1 | Every case P1-P13 ran and passed | The `marrow_project_smoke` output names each |
| 2 | **Every inversion in §B was run against the FROM-SCRATCH build, bit the named case, and was restored** | Sixteen entries. Record the **exact** failure text for each, compared with `cmp`/`diff` against a whole recorded string (H2). Any that did not bite is recorded as such, with how the case was strengthened — **not** quietly dropped |
| 3 | Every restore was followed by `touch` and a rebuild | H1. State it explicitly |
| 4 | `serialize_project(player_idle.marrow)` unchanged | P4's printed length + SHA equal Task 0.3's |
| 5 | Registry unchanged at **64** | Task 6's empty diff + re-measured 64 / 1 array / 10 guards / 2 python assertions |
| 6 | The untouchable trees are byte-identical | Task 6's second `git diff --stat` |
| 7 | `ctest -N` total unchanged from Task 0.1 | This story registers no CTest |
| 8 | `~/Library/Application Support/Marrow` **ABSENT** after the whole run | `test -e ~/Library/Application\ Support/Marrow && echo PRESENT \|\| echo ABSENT`. PRESENT is a hard failure |
| 9 | The M-gate answer from Task 0.5 is recorded | Pasted `marrow_inspect` output |
| 10 | The 0.6 answer is recorded, and `runtime_fixture_smoke` was **not** widened | `git diff --stat -- src/samples/runtime_fixture_smoke.cpp` empty |
| 11 | `marrow_inspect --compare` → `matches` | Regression witness only; it carries no inherit data |
| 12 | Count sweep inspected by hand | Task 6's `grep` output, every line explained |

---

## Task 8 — Documentation and commit

**Depends on:** Task 7, complete and green.

### 8.1 `docs/root1/format-spec.md`

- The `timeline_edits` family list (`:869-877`) gains **"bone inherit edits"**.
- A new `#### inherit (MAR-184, stepped)` subsection after the
  `curve_mode`/`curve_driver` block (`:881-932`), carrying: the schema from
  design §2.1, the full validation table from §2.4 with A8's message wording,
  and the §2.5 asymmetry — **a `.mskl` may carry inert `curve` data on an
  inherit key and Marrow ignores it; only the overlay rejects it.** State why:
  adding the rejection to the runtime parser would make previously-valid `.mskl`
  files fail to load, which AC5's "retaining current format versions" forbids.
- The `.mskl` `animations` prose (`:169-176`) already lists inherit; no change.
- State that `.mskl` stays version 1, `.mbin` stays version 2, `.marrow` gains
  no version field, and the new key is additive — a build that has never heard
  of it hits the `continue` at `project.cpp:1899-1901`, exactly as today.

### 8.2 `AGENTS.md`

Add a `## MAR-184 Add Stepped Inherit Timeline Overlays Validation Results`
section, matching the MAR-182 and MAR-183 sections' structure:

- **Opening paragraph** stating what the story did and did **not** touch: no
  ImGui, no widget, no frame smoke, no agent operation, no MCP tool, no runtime
  file, no fixture, no CMake target; `.mskl` v1 / `.mbin` v2 / C ABI untouched;
  registry unchanged at 64, proved by an empty diff. And the title correction —
  "overlay" here is `ProjectData::*_timeline_edits`, not rendering.
- **"What was measured before any code was written"** — Task 0's values
  verbatim, including the M-gate 0.5 output and the `ctest -N` total.
- **"Result"** — one row per check.
- **"Inversions run"** — all sixteen, each with the case it bit and the exact
  message. Any that did not bite recorded as such.
- **"Document errors found"** — §A's eight rows, plus anything Task 0 turned up.
  Every story from MAR-175 on has found between three and seven; if you find
  fewer than §A already lists, you have lost one — say so explicitly.
- **"Not independently covered"**, carrying forward and adding:
  - **No pixel is asserted.** P7 proves an Inherit `TrackRow` is *produced* from
    a project-materialized skeleton by a pure function. It does **not** prove the
    dopesheet draws it. MAR-184 adds no drawing code — the row and its renderer
    both predate this story — but a regression that deleted the lane's draw call
    would not be caught here. MAR-185 ships UI and is where a real-mouse
    scenario belongs.
  - **P4 and the equivalence half of P12 have no story-owned inversion.**
  - **The runtime still accepts a negative first inherit key time**, and a
    `.mskl` may carry inert `curve` data on an inherit key. Only the project
    layer refuses either.
  - **The "finite" half of AC1 is only partially reachable from a file.** JSON
    has no NaN literal and the tokenizer refuses an out-of-range exponent
    (`json.cpp:302-307`), so from a `.marrow` the check bites only on a
    magnitude over float32 max (P2c). NaN and infinity reach it only through the
    primitive's `double` parameter (P10d).
  - **The empty-edit hazard is fixed only for inherit.** Design §1.9's sibling
    families keep the pre-existing behaviour — an empty
    `ensure_slot_attachment_timeline_edit` reaching export would still emit
    `"attachment": []` and produce an unloadable `.mskl`. Out of scope,
    recorded, not fixed.
  - **`replace_existing_times` has no product caller** until MAR-185's
    `set_inherit_keyframe`. P11 covers both arms, so it is not dead in the
    untested sense.
  - Carry forward MAR-183's `shell_main.cpp` frame-body and `commit_path_choice`
    notes unchanged — MAR-184 adds no line to either.

Extend the existing `## Current Validation` line at **`AGENTS.md:43`** — do not
add a new one (§A2):

```
- Editor project authoring smoke including `offset_keyframe_scalars` and MAR-184's
  stepped inherit overlays (schema round trip, all five modes, the four parser
  rejections, the curve rejection, base-backed/project-only/empty materialization,
  the merge primitive's four rejections and both collision arms, and `.mskl`/`.mbin`
  export equivalence -- P1-P13):
  `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
```

### 8.3 The PRD

Set `MAR-184`'s `status` to `"done"` and `completedAt` to the run's date in
`.agents/tasks/prd-marrow-runtime.json`. Touch no other story.

### 8.4 Commit

One commit for the story.

```bash
git add -A
git commit
```

Subject, matching the arc's Korean convention:

```
feat: MAR-184 스텝 상속 타임라인 오버레이 추가
```

Body, in Korean, covering: the additive `.marrow` schema and its five mode
tokens; the validation table including the two rules stronger than the runtime
(finite non-negative time, and the stepped-only `curve` rejection) and why the
runtime parser was deliberately left alone; whole-timeline replacement keyed by
(animation, bone) with `ensure_*` materializing the base track on first touch;
the empty-edit skip in both serializers; `merge_inherit_timeline`'s
preflight-then-mutate contract and its deterministic sorted output; and the
explicit statements that `.mskl` stays 1, `.mbin` stays 2, the registry stays 64,
and no UI file was touched.

Trailers:

```
Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: <session id>
```

---

## Risks

| # | Risk | Mitigation |
| --- | --- | --- |
| R1 | **Building the wrong story.** The title says "overlays"; a plausible reading is rendering | Design §0.1. The description and all six ACs are the authority. If you are editing `timeline_graph_model.cpp`, stop |
| R2 | **The pruning trap** makes a correct implementation look broken, and the failure points at the overlay code | Task 0.5 measures it first. Every project-only fixture in this plan is `{0.0, noScale} + {0.4, normal}`, which is pruning-safe |
| R3 | **The forgotten seventh disjunct** at `project.cpp:4832` silently drops every inherit overlay on save | §A4, and inversion I9 attributed to P1 |
| R4 | **A non-biting load-rejection case.** `load_project` fails for many reasons; a case asserting `!result` passes under its own inversion | Every rejection case asserts on `result.error->message` content. Stated at P2, P6 and in §B for I1 and I2b |
| R5 | **The candidate pattern silently disarms I4** | §A5 redefines I4 as removing the candidate. P10c, not P10a/b, is the attribution |
| R6 | **P11 on a bone whose edit already exists** makes `ensure` a no-op and I4 non-biting | P11 uses `child` with **no project edit yet**, stated as load-bearing |
| R7 | Parser ordering: placing the new call after `parse_loop_sync` breaks the invariant at `project.cpp:7450-7451` | §1.4 pins the insertion point between `:7423` and `:7454` |
| R8 | Scope leak into MAR-185 — making the lane editable, or adding a registry op | Task 6's empty-diff proof; inversion I8 |
| R9 | Reaching into `skeleton_parse.cpp`'s anonymous `parse_bone_inherit` | §1.4: the project layer owns its own bidirectional table. Linking would fail; the real risk is a duplicated one-way table that drifts |
| R10 | An inversion result that lies (H1/H2/H3) | `touch` after every restore; final run from `rm -rf build`; compare whole recorded strings with `cmp`; re-demonstrate falsifiability after any rewrite of test code |

---

## Dependency graph

```
Task 0 (measure, incl. the pruning M-gate)
  └─> Task 1 (schema: structs, table, parser, serializers, load wiring)   P4 P1 P2 P3 P6
        └─> Task 2 (ensure_* accessor)                                    P13
              └─> Task 3 (materialization + empty-edit skips)             P7 P8 P9
                    └─> Task 4 (merge_inherit_timeline)                   P5 P10 P11
                          └─> Task 5 (export .mskl / .mbin)               P12
                                └─> Task 6 (scope: empty diffs)           I8
                                      └─> Task 7 (full verification, from scratch)
                                            └─> Task 8 (docs + one commit)
```

Task 6 can start any time after Task 5's last production edit; it is placed last
because a diff is only meaningful over the finished tree.
