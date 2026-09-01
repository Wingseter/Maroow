# MAR-185 Complete Inherit Timeline Editing Parity — Implementation Plan

Design: `docs/superpowers/specs/2026-09-01-mar-185-inherit-timeline-editing-parity-design.md`
Story: `MAR-185`, depends on `MAR-184` — **a real code dependency** (design §0.2).
Branch: `feat/mar-168`. **Baseline measured for this plan: `a2981b8`**
(`feat: MAR-183 최근 프로젝트 목록 영속화와 관리 구현`), the tip of `feat/mar-168` on
2026-09-01, with only MAR-184's two untracked documents in the tree.

**The real baseline for implementation is MAR-184's commit, which does not exist
yet.** Every `file:line` below was correct at `a2981b8` and is **expected to have
drifted** — `impl-mar184` was adding to `project.cpp`, `project.hpp`,
`authoring.cpp`, `authoring.hpp` and `editor_project_smoke.cpp` while these
documents were written. **Task 0.1 is a blocking re-anchor gate**: re-resolve
every citation against the actual tip before any implementation, and treat one
that no longer names its described construct as a finding to record, never as a
line to silently follow.

**Read the design first**, then §A of this plan, then §B. In particular design
§1.1 (the compiler is silent — this is the story's defining constraint), §2.4
(the removal floor and why it exists), §2.9 (the clipboard cascade is not
inherit-specific, deliberately), and §5.1 (the eleven arms that are deliberately
uninverted).

## Standing rules for this plan

- **TDD, strictly.** The case goes in first, is **run and seen to fail for the
  stated reason**, and only then is the implementation written. A case that
  passes on its first run is a defect in the case until proven otherwise.
- **Every case must be proven falsifiable, and uniquely attributed.** §B is the
  register. For each entry: apply the named source mutation, run the named
  binary, record the **exact** failure text, restore, `touch`, rebuild. A case
  with no recorded failure text is not covered by this story and AGENTS.md must
  say so.
- **Uniqueness is established by running, not reading.** Where §B claims
  "nothing earlier catches this", demonstrate it. Where an earlier
  short-circuit could hide a later detector, **neuter the earlier call**
  (`if (false && …)`) and re-run to confirm the later one fires.
- **`touch` after every restore** (`AGENTS.md` H1). A `cp` restore landing in
  the same mtime second makes `make` skip the rebuild and the next run silently
  exercises the *inverted* binary. Final verification runs from `rm -rf build`.
- **Compare recorded messages with `cmp`/`diff` over a whole string** (H2).
  Never hand-slice a line number out of a reference file.
- **Trace which writes survive to the assertion point** (H4). An assertion that
  reads state a passing run leaves at its default is vacuous. If a mutation
  cannot change what an assertion reads, the assertion is not evidence.
- **A passing `save()` proves nothing.** `validate_project_for_save`
  (`src/editor/project.cpp:5504`) takes no base document. Only
  `load_project(path)` materializes — it calls `build_project_runtime` at
  `src/editor/project.cpp:7532-7533` (the function itself is defined at
  `:7860`). Every round-trip assertion goes through the real load path.
- **Preflight-then-mutate.** `authoring.cpp` uses `ProjectData candidate =
  *project;` … `*project = std::move(candidate);` at ten sites. This makes
  "mutate the candidate early" **invisible** as an inversion, so I8 is defined
  as *removing* the candidate (design §5, MAR-184 §A5).
- When a measurement disagrees with this plan, **the measurement wins.** Record
  it in AGENTS.md's "Document errors found" table; do not silently adapt.
- Never weaken a case to make an inversion bite. Strengthen the case.
- `git add` / `git commit` only at Task 10. No `checkout`, no `stash`.

---

## §A. What was verified, what was wrong, and what Task 0 must still gate

### A.1 Errors found in the incoming documents

**Every "measured" value in this table is a value at `a2981b8` and has drifted
since.** Task 0.1 re-resolves them all; the numbers below are the record of what
was wrong, not a substitute for that gate.

| # | Source | Claim | Measured at `a2981b8` |
| --- | --- | --- | --- |
| **A1** | The MAR-185 briefing, and **the team lead's briefs for several earlier stories** | `build_project_runtime()` at `project.cpp:7495` | **Wrong, and confirmed wrong by its author.** `:7495` is slot-color timeline-edit code inside `load_project(const Document&)`. `build_project_runtime` is defined at **`:7860`** and called from `load_project` at **`:7532-7533`**. The substantive claim — only `load_project(path)` materializes, so round-trips go through it — **holds**. Recorded here specifically so the `:7495` figure stops being copied forward out of story briefs. Note that `:7860` has itself already moved: partway through MAR-184's implementation it resolved to **`:8258`**, which is Task 0.1's worked example |
| **A2** | MAR-184 plan header | Baseline `25bf694` | The tip is **`a2981b8`**. `25bf694` and `4a4249d` both exist but neither is the tip. MAR-184's own anchors were spot-checked and hold with its §A6 drift |
| **A3** | MAR-184 plan §A2 | The marker gate is `editor_project_smoke.cpp:14056-14203` | It is **`:14062-14206`**: `main()` `:14028`, the `--create` arm `:14056-14061`, the skip `:14062-14072`, the partial-match abort `:14073-14082`, the `else` `:14083-14206`. The substantive rule — **one invocation** — holds exactly, and MAR-185 obeys it |
| **A4** | MAR-184 design §1.2 | `track_is_editable` at `timeline_model.cpp:240-248` | `:240-249`. Recorded because MAR-185 **edits this function** |
| **A5** | MAR-184 design §1.4 table | `ProjectData` edit vectors at `project.hpp:566-572`; `find_transform_timeline_edit` at `:609/619` | The six vectors are at **`:566-573`** with `mesh_weight_attachment_edits` interleaved at `:568`; the two `find_transform_timeline_edit` declarations are at **`:608`** and **`:619`**. MAR-184's own §A6 already corrects most of this; recorded again because MAR-185 inserts beside them |

### A.2 Claims in the MAR-185 design that Task 0 must **prove by building**

These are the ones that cannot be settled by reading:

| # | Design claim | How Task 0 settles it |
| --- | --- | --- |
| **A6** | §1.1 — the build has no `-Wswitch`, so a seventh `TimelineKeyKind` produces **zero** diagnostics at 29 sites | Add a throwaway seventh enum value, build `marrow_editor`, record that the build is clean. **If any diagnostic appears, §1.1's framing is wrong and Task 1 changes shape** |
| **A7** | §0.2 — every MAR-184 symbol MAR-185 consumes | Compile a probe naming each one. A differing signature is a stop-and-record |
| **A8** | §1.7 — the pruning rule really deletes a lone `{0.0, normal}` inherit overlay | Re-run MAR-184's plan §0.5 M-gate against MAR-184's committed tree and paste the output |
| **A9** | §5 I13 — the selector-vs-stored time difference is observable at `1e-12` for the times P8 uses | Probe it before writing P8's precision assertion. **If it is not, I13 is recorded as non-reproducing, not converted into a convenient bite** |
| **A10** | §1.3 — nothing anywhere rewrites `Clipboard::animation_name` | `grep -rn "clipboard" src/editor/shell_project_panels.cpp` → expect **0**; `grep -rn "animation_name" src/editor/timeline_controller.cpp \| grep clipboard` → expect only the copy-path write |

### A.3 Design claims re-verified at `a2981b8` — do not re-litigate

- `TimelineKeyKind` has exactly six values, `authoring.hpp:102-109`;
  `TimelineKeySelector` `:117-126`.
- `grep -rn "TimelineKeyKind::SlotAttachment" src/ include/ tools/` → **28**
  hits across four files (19 `authoring.cpp`, 7 `agent_handlers_editing.cpp`,
  5 `editor_project_smoke.cpp`, 2 `timeline_controller.cpp`).
- `CMakeLists.txt` has `add_compile_options(/utf-8)` at `:19` (MSVC) and
  `-Werror` **only** at `:352`, scoped to `marrow_constraint_warning_check`.
  No `-Wall`, no `-Wswitch`, on any product target.
- Registry **64**; `std::array<OperationExpectation, 64>` at
  `agent_dispatch_smoke.cpp:39`; **ten** `!= 64U` guards at
  `shell_smoke_constraints.cpp:147,676`, `shell_smoke_timeline.cpp:3697`,
  `shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603`; `== 64` twice at
  `tools/mcp/test_client.py:53,55`.
- `rename_all_timeline_edits` `authoring.cpp:123-130`,
  `erase_all_timeline_edits` `:132-139`, both six-vector lists;
  `rename_animation` `:1909`, `delete_animation` `:1944`.
- `auto_extend_explicit_animation_durations` `:2067`, its six folds `:2087-2098`.
- `scale_keyframe_times` `:2258`, its validate switch `:2372-2412`, its
  "provably a no-op" sort comment `:2434-2435`.
- `track_is_editable` `timeline_model.cpp:240-249`; `build_tracks`' inherit arm
  `:159-166`; `TimelineTrackKind::Inherit` `timeline_model.hpp:29`.
- `timeline_key_selector` `timeline_controller.cpp:891-940`;
  `visit_editable_timeline_keys` `:960`; `visit_existing_project_timeline_keys`
  `:1008`; `finish_timeline_transaction` `:1071`;
  `add_timeline_key_at_playhead` `:1095`, its replace-in-place at `:1198-1206`;
  `remove_selected_timeline_keys` `:1791`, its one-key floor `:1898`;
  `copy_selected_timeline_keys` `:1932`; `paste_timeline_clipboard` `:2050`.
- `draw_transform_timeline_editor` `shell_timeline.cpp:1499`, its dispatch chain
  `:1514-1533`, its read-only message `:1535-1540`;
  `draw_slot_attachment_timeline_editor` `:1405-1497`; the two tooltips `:2277`,
  `:2296`.
- `apply_animation_catalog_action` `shell_project_panels.cpp:536-616`, its
  selection clear `:607-611`; `sync_shell_from_editor_session`
  `shell_core.cpp:261`, its animation-name assignment `:271`.
- `timeline_description_value` `agent_dispatch.cpp:996-1036`;
  the `dry_run_unsupported` guard `:1075-1081`; `export.preview`
  `agent_handlers_inspection.cpp:23-42`.
- `prune_constant_timelines`' inherit arm `skeleton_animation.cpp:275-279`,
  `has_single_key_at_origin` `:118-120`, run from `skeleton.cpp:116`.
- Fixtures: `skin_inherit_constraints.mskl` — five bones (`root`, `controller`,
  `child`, `constrained`, `cape_target`), all with **absent** `inherit`; one
  animation `toggle_inherit`; `child`'s four keys `0.0 normal`,
  `0.25 noRotationOrReflection`, `0.5 onlyTranslation`, `1.0 normal`.
  `player_idle.mskl` — sixteen bones, all absent `inherit`; zero inherit
  timelines; animations `idle`, `attack`, `aim`.
- `mar178_open_skin_session` `editor_project_smoke.cpp:11763-11790`;
  `write_skin_fixture_project` `shell_smoke_constraints.cpp:115-131`.

---

## §B. Inversion register

An inversion is valid only if the case it names is the **first** thing that
catches it. Where an earlier detector exists it is named and the attribution
moves. Where uniqueness rests on a short-circuit, the demonstration is: neuter
the earlier call and re-run.

| # | Mutation | Attributed to | Must fail with | Why nothing earlier catches it |
| --- | --- | --- | --- | --- |
| **I2** | `track_is_editable` (`timeline_model.cpp:240-249`) drops the Inherit disjunct | **P1** | `P1: adding a key on bone:2:Inherit failed; status was "The selected timeline is read-only"` | P0 loads `player_idle.marrow`, which has no inherit data at all. P1 is the first case that touches the lane |
| **I1** | `sample_inherit_keyframe` seeds from `bones()[i].inherit` instead of `sample_bone_inherit` | **S1** | `S1: the key added at 0.75 s carries mode "normal", expected the sampled "onlyTranslation"` | P1-P10 author modes explicitly and never exercise the seed; only the shell's Add path calls the sampler. **Every bone of the fixture is setup-`Normal`, measured**, so the two seeds differ by construction |
| **I3** | `timeline_key_selector` (`timeline_controller.cpp:891-940`) drops the Inherit branch | **P6** | `P6: retime_keyframes resolved 0 keys, expected 1` | Under I3 `track_is_editable` is still true, so the row looks editable and nothing rejects; the retime just resolves nothing. P6 asserts `key_count`, **not** `bool(result)` |
| **I7** | Delete the Inherit arm from `apply_resolved_retime` (`authoring.cpp:942`) | **P6** | `P6: after save -> load the key is still at 0.250000, expected 0.400000` | `void`, no trailing statement: the retime reports `changed == true` and `applied_delta == 0.15` while writing nothing. P6 must assert the **stored** time after a real `load_project`, not the returned delta. A second, distinct assertion on the same case as I3 |
| **I6** | Delete the Inherit arm from `include_resolved_retime_bounds` (`:863`) | **P7** | `P7: applied_delta is 0.400000, expected the clamped 0.249000` | `void`, no trailing statement: the delta is unbounded by inherit neighbours. P6 uses a delta with room and passes under I6; P7 is the first case whose delta must be clamped |
| **I4** | Delete the Inherit arm from `scale_keyframe_times`'s validate switch (`:2372-2412`) | **P8** | `P8: the colliding scale was accepted (changed=true); expected a rejection naming the separation` | The switch has no `default` and no trailing statement, so `valid` stays `true` and `error` stays empty — **no diagnostic anywhere**. P6/P7 are retimes, which clamp through a different path |
| **I5** | Delete the Inherit arm from `scale_lane_label` (`:621`) | **P8** | `P8: rejection message is "...timeline key at 0.502000 s..."; expected it to name "inherit key 'child'"` | A **second, independently asserted mutation on the same case.** Under I5 the rejection still happens (I4's arm is present), so only the message text differs. Recorded as two mutations, not one |
| **I13** | Delete the Inherit arm from `resolved_stored_key_time` (`:684`) | **P8** | `P8: the scaled time is 0.5000000149, expected 0.5000000000 within 1e-12` | The scale then reads the selector's float32-narrowed time. **Gate A9 first**: if the difference is below `1e-12` for P8's data, record I13 as **non-reproducing** and say so in AGENTS.md rather than inventing a bite |
| **I8** | Remove the candidate copy from `remove_inherit_timeline_keys` and hoist `ensure_bone_inherit_timeline_edit(*project, …)` above step 7 | **P5** | `P5: the rejected removal changed serialize_project() (2841 -> 3106 bytes)` | P5 names a **time that does not exist**, on `child`, which has a base track and **no project edit yet** — so `ensure` materializes four keys the rejection then abandons. On a bone with an existing edit `ensure` is a no-op and I8 could not bite; on an unknown animation or bone `ensure` returns `nullptr` and writes nothing. P5 is the only rejection whose animation **and** bone resolve while no edit exists |
| **I9** | Delete step 8 (the last-key floor) from `remove_inherit_timeline_keys` | **P3** | `P3: the exported .mskl carries 4 inherit keys for 'child', expected the 1 that survived removal` | P3's first three removals succeed under I9 too. The floor is only observable after the **fourth**, and only through the export: the emptied edit is skipped by both serializers and `build_runtime_document` assigns into a **copy** of the base, so the imported track returns. P5 rejects before touching the project and cannot see this |
| **I10** | Drop the seventh line from `rename_all_timeline_edits` (`:123-130`) | **U1** | `U1: after renaming toggle_inherit -> toggle_two, the reloaded project's inherit edit still names 'toggle_inherit'` | The first case that renames an animation carrying an inherit overlay |
| **I11** | Drop the seventh line from `erase_all_timeline_edits` (`:132-139`) | **U2** | `U2: the reloaded skeleton still has animation 'toggle_inherit', re-created from an orphan inherit edit` | U1 renames and passes under I11. U2 is the first delete |
| **I12** | Make `cascade_animation_rename` a no-op | **U3** | `U3: clipboard.animation_name is "toggle_inherit" after the rename, expected "toggle_two"` | A `marrow_timeline_model_tests` case over a bare `Clipboard`: no session, no shell, no frame can stand in for it. **S6 is a second detector at the shell layer and is recorded as over-determined**; U3 runs first in the verification order |
| **I14** | Drop the seventh `include_animation_timeline_maximum` from `auto_extend_explicit_animation_durations` (`:2087-2098`) | **P9** | `P9: the explicit duration is 1.000000 after auto-grow, expected 1.400000` | P1-P8 never author a key past an explicit duration; P9 is the first |
| **I15** | Delete the `Mode` combo's emission from `draw_inherit_timeline_editor` | **F1** | `F1: no widget in the Timeline window matched GetID("Mode") across the sweep (the control 'Time' matched at x=412, so the seed is sound)` | Every UI-free case calls the controller, not the widget, and stays green — the exact failure `AGENTS.md` records for MAR-178, whose own scenario printed success while the frame smoke failed by name. The `Time` half of the assertion is what distinguishes "widget deleted" from "broken `GetID` seed" |
| **I16** | `timeline_key_kind_name` (`agent_handlers_editing.cpp:53-62`) loses its Inherit arm | **A2** | `A2: set_inherit_keyframe echoed kind "transform", expected "inherit"` | Falls through to the trailing `return "transform"`. A1 asserts only the registry shape; A2 is the first case that reads an echo |
| **I17** | `parse_timeline_key_selectors` loses its `"inherit"` branch | **A5** | `A5: timeline.retime_keyframes failed: Unknown timeline key kind: inherit` | A2-A4 use the two dedicated ops, which build their selector in C++. A5 is the only case that sends `kind: "inherit"` over the wire |
| **I18** | Give `remove_inherit_keyframe`'s registry row `dry_run_supported = true` | **A4** | `A4: remove_inherit_keyframe accepted dry_run and removed the key; expected error.code "dry_run_unsupported"` | The dispatcher's guard (`agent_dispatch.cpp:1075-1081`) is registry-driven; nothing in the handler re-checks. A1 asserts the count and the names but not this flag |
| **I19** | Leave `agent_dispatch_smoke.cpp:39`'s array at `64` while adding two registry rows | **A1** | `A1: descriptor count must match the operation protocol contract` (the existing message at `agent_dispatch_smoke.cpp:985-988`) | A pre-existing guard, re-used. Recorded because it is the **shipped** detector for the count sweep, not a new one |
| **I20** | Add `Inherit` to `track_is_graphable` (`timeline_graph_model.cpp:393`) | **Task 9's diff** | `git diff --stat -- src/editor/timeline_graph_model.cpp` is non-empty | **Not a test.** A scope check, and the plan says so rather than calling a diff "coverage" |

### B.1 Deliberately uninverted — record verbatim in AGENTS.md

Design §5.1. **No inversion is invented for any of these**, and the reason is
stated per group:

- **Eleven switch arms fall through to the correct answer**, so removing them
  changes nothing observable: `family_owns_scalar_component` (`:337`),
  `read_scalar_component` (`:371`), `write_scalar_component` (`:425`),
  `family_key_spacing` (`:524`), `resolved_key_is_loop_pinned` (`:596`),
  `read_key_interpolation` (`:2564`), `read_key_curve_mode` (`:2624`),
  `read_key_curve_driver` (`:2670`), `timeline_key_is_managed_loop_boundary`
  (`:3703`), `timeline_key_kind_carries_easing`
  (`timeline_controller.cpp:1238`), and `timeline_key_curve_value`
  (`agent_handlers_editing.cpp`). Three more —
  `write_key_interpolation` (`:2589`), `write_key_curve_mode` (`:2646`),
  `write_key_curve_driver` (`:2692`) — are unreachable for inherit because
  their callers check the matching `read_*` first.
- **`sort_retimed_timelines`' seventh call (`:986`) is provably a no-op.** A
  retime applies one shared clamped delta, which preserves index order; a scale
  validates the whole projected sequence first, and the source itself says its
  sort is "defence in depth, and provably a no-op" (`:2434-2435`).
- **`export.preview`'s non-effect assertion in A6.** It reports target paths
  only; there is nothing inherit-specific for a mutation to break.
- **P0's byte-identity witness.** An unchanged project must serialize to
  unchanged bytes; there is no story-owned mutation that makes it differ.

---

## Task 0 — Measure, before any code

**Depends on:** MAR-184 being committed. **No file is modified.** Every step
prints a value; a value that disagrees with §A is a stop-and-record.

### 0.1 **Re-anchor gate — every cited line number, before anything else**

**Every `file:line` in these two documents was measured at `a2981b8` and is
expected to have drifted by the time MAR-185 starts.** They were correct when
written; they are not correct now. `impl-mar184` was editing `project.cpp`,
`project.hpp`, `authoring.cpp`, `authoring.hpp` and `editor_project_smoke.cpp`
while these documents were being authored — roughly 1800 added lines by the time
they were finished — and MAR-184's commit adds more. The drift is already
measurable: `build_project_runtime`, cited as `project.cpp:7860` in design §0.3
and in this plan's standing rules, resolved to **`:8258`** partway through
MAR-184's implementation.

So, **before Task 0.3's compile probe and before any implementation**, re-resolve
every cited anchor against the actual tip and record the mapping. The list to
re-resolve is not "the ones that look important" — it is every anchor in §A.3 of
this plan plus design §1.1's table, §1.2's five read-only gates, §1.5's thirteen
count sites, and §3's per-file landing points.

```bash
git rev-parse HEAD
git diff --stat a2981b8..HEAD -- src/editor/ include/marrow/editor/ src/samples/
```

For each anchor, the check is **not** "does that line still exist" but **"does
that line still name the construct the document says it names"**:

```bash
# One worked example; repeat the shape for every anchor.
sed -n '4381,4390p' src/editor/project.cpp   # build_timeline_edits_value's
                                             # parameter list -- expect SEVEN
                                             # vectors after MAR-184
sed -n '4826,4856p' src/editor/project.cpp   # its call site's !empty() gate
grep -n "^std::optional<ResolvedTimelineKey> resolve_timeline_key" src/editor/authoring.cpp
grep -n "^bool track_is_editable" src/editor/timeline_model.cpp
grep -rn "TimelineKeyKind::SlotAttachment" src/ include/ tools/ | wc -l
```

**A citation that no longer resolves to the described construct is a blocking
finding, not a silent adjustment.** Write the old and new line numbers into
AGENTS.md's "Document errors found" table, in the same form as §A.1, and only
then continue. Silently following a moved line is how a sweep misses a site, and
design §1.1 measured that nothing else will catch it.

Two anchors matter more than the rest and are called out by name:

- **`build_timeline_edits_value`'s parameter list and its call-site `!empty()`
  disjunction** (`project.cpp:4381-4387` and `:4826-4852` at `a2981b8`). Task
  0.3 already gates on MAR-184's seventh disjunct being present; this step is
  where its **new** line numbers are recorded, because omitting a disjunct there
  drops data on save with every test green.
- **The 29 `TimelineKeyKind` switch sites** in design §1.1's table. Re-derive
  the count with the grep above before Task 1 starts sweeping them; a count that
  is no longer 28 `SlotAttachment` hits across four files means MAR-184 added or
  moved a site and the table needs a row.

### 0.2 Baseline build and inventory

```bash
git rev-parse HEAD                      # MAR-184's commit; record it
git status --short                      # expect only this story's two docs
cmake -S . -B build
cmake --build build
ctest --test-dir build -N | tail -3
```

Record the `ctest -N` total. MAR-185 registers **no** new CTest target, so the
number must be identical at Task 9.

### 0.3 **A7 gate — every MAR-184 symbol MAR-185 consumes**

Write `/tmp/mar185-dep-probe.cpp` naming every row of design §0.2:

```cpp
#include "marrow/editor/authoring.hpp"
#include "marrow/editor/project.hpp"
using namespace marrow::editor;
void probe(ProjectData* p, const runtime::SkeletonData& s) {
    BoneInheritTimelineEdit e{};
    e.animation_name = "a"; e.bone_name = "b";
    e.keyframes.push_back(InheritKeyframeEdit{0.0, runtime::BoneInherit::NoScale});
    p->bone_inherit_timeline_edits.push_back(e);
    (void)p->find_bone_inherit_timeline_edit("a", "b");
    (void)ensure_bone_inherit_timeline_edit(*p, s, "a", "b");
    (void)inherit_mode_from_key("noScale");
    (void)inherit_mode_json_key(runtime::BoneInherit::NoScale);
    InheritTimelineMergeRequest r{};
    r.replace_existing_times = true;
    (void)merge_inherit_timeline(p, s, r);
}
```

Compile it against the editor include paths. **Any name or signature that
differs is a stop-and-record**: adapt this plan's call sites to what shipped and
write the difference into AGENTS.md's document-errors table. Delete the probe
before Task 1.

Also confirm MAR-184's seventh serializer disjunct actually landed:

```bash
sed -n '4826,4852p' src/editor/project.cpp   # expect a SEVEN-way !empty() gate
```

Its absence silently drops every inherit overlay on save with every test green
(MAR-184 §A4 / its I9). If it is missing, **stop and tell MAR-184's implementer**;
do not fix it here.

### 0.4 **A6 gate — prove the compiler is silent.** The most consequential step

Add a throwaway seventh value to `TimelineKeyKind`
(`include/marrow/editor/authoring.hpp:102-109`), build, and record:

```bash
cmake --build build --target marrow_editor 2>&1 | tee /tmp/mar185-wswitch.txt
grep -ci "warning" /tmp/mar185-wswitch.txt          # expect 0
git checkout -- include/marrow/editor/authoring.hpp && touch include/marrow/editor/authoring.hpp
```

**Expected: a clean build with no diagnostic.** If warnings appear, design §1.1's
framing is wrong, the sweep is compiler-assisted, and Task 1 changes shape —
record it and re-plan Task 1 before writing code.

### 0.5 The registry and the count-sweep baseline

```bash
grep -c '^\s*{\s*"' src/editor/agent_dispatch.cpp
grep -n "OperationExpectation, 64" src/samples/agent_dispatch_smoke.cpp
grep -rn "!= 64U" src/
grep -n "== 64" tools/mcp/test_client.py
```

Expect **64 / one array at `:39` / ten guards / two Python assertions**.

**This story moves the number to 66 at thirteen sites and nowhere else.** Write
the thirteen down now; Task 9 diffs against the list. The look-alikes that must
**not** move: `shell_smoke_graph.cpp:2977`'s `4364U`,
`shell_smoke_viewport.cpp:3504`'s `640`, `IM_COL32(56, 61, 69, 255)`,
`IM_COL32(208,134,57,230)`, `(51, 56, 64)`, `rgb(54,57,64)`, `"x": 56.0`,
`56,995,840`, `PhysicsBoneState … 56 bytes/bone`, `AGENTS.md`'s `t = 0.62` and
its self-referential line counts, and `tools/mcp/test_client.py:1484`'s ordinal.

### 0.6 The non-effect witness

```bash
./build/marrow_project_smoke assets/fixtures/player_idle.marrow > /tmp/mar185-before.txt 2>&1
shasum -a 256 assets/fixtures/player_idle.marrow
```

Then record the byte length and SHA-256 of
`serialize_project(load_project("assets/fixtures/player_idle.marrow").project)`.
P0 compares against these and prints both every run.

### 0.7 The UI witness

```bash
git grep -c "Inherit remains read-only" -- src/     # expect 2
git grep -n "The selected timeline row is read-only" -- src/editor/shell_timeline.cpp
```

Both must be **zero** and unreachable-for-inherit respectively at Task 9.

### 0.8 The fixture facts

```bash
python3 -c "
import json
d=json.load(open('assets/fixtures/skin_inherit_constraints.mskl'))
print([(b['name'], b.get('inherit','<absent>')) for b in d['bones']])
print(d['animations']['toggle_inherit']['bones']['child']['inherit'])
p=json.load(open('assets/fixtures/player_idle.mskl'))
print(len(p['bones']), sorted({b.get('inherit','<absent>') for b in p['bones']}))
print(sum('inherit' in bb for a in p['animations'].values()
          for bb in a.get('bones',{}).values()))
"
```

Expect five bones all `<absent>`, the four `child` keys, sixteen `player_idle`
bones all `<absent>`, and **0** inherit timelines in `player_idle`.

### 0.9 **A8 gate — re-run MAR-184's pruning M-gate**

Take `assets/fixtures/skin_inherit_constraints.mskl`, add
`animations.toggle_inherit.bones.controller.inherit = [{"time":0.0,"inherit":"normal"}]`,
write `/tmp/mar185-prune-probe.mskl`, and run:

```bash
./build/marrow_inspect /tmp/mar185-prune-probe.mskl | grep -i inherit
```

**Expected: the `controller` timeline is ABSENT.** Paste the output into
AGENTS.md. Every fixture choice in Tasks 2-8 assumes it; if it is present, the
test data can be simplified and design §1.7, §2.11 and §10.3 are all wrong.

### 0.10 **A9 gate — is I13 viable?**

Probe whether the selector's float32-narrowed time and the stored `double`
differ by more than `1e-12` for the times P8 will use. Print both. **Record the
answer in writing.** If they do not differ, I13 is recorded as
non-reproducing — do not weaken P8 or invent a different bite.

### 0.11 **A10 gate — the clipboard really has no cascade**

```bash
grep -rn "clipboard" src/editor/shell_project_panels.cpp        # expect nothing
grep -rn "clipboard.animation_name" src/editor/                 # expect the
                                                                # read at
                                                                # timeline_model.cpp:400
                                                                # and the write at
                                                                # timeline_controller.cpp:1940
```

### Task 0 exit criteria

- [ ] **0.1 re-anchor gate**: every cited `file:line` in both documents
      re-resolved against the tip, with the old → new mapping recorded and any
      citation that no longer names its described construct written into
      AGENTS.md's document-errors table as a blocking finding. The
      `build_timeline_edits_value` parameter list, its call-site `!empty()`
      disjunction, and the `TimelineKeyKind` site count are called out
      individually.
- [ ] MAR-184's commit hash recorded; `ctest -N` total recorded.
- [ ] **A7**: the dependency probe compiled and linked; any signature drift
      recorded. MAR-184's seventh `!empty()` disjunct confirmed present.
- [ ] **A6**: the `-Wswitch` probe built clean, with the output pasted.
- [ ] Registry 64 / 1 / 10 / 2 confirmed; the thirteen sites written down.
- [ ] `serialize_project(player_idle.marrow)` length + SHA recorded.
- [ ] `git grep -c "Inherit remains read-only"` → 2.
- [ ] Fixture facts confirmed.
- [ ] **A8**: the pruning M-gate answered in writing with `marrow_inspect`
      output pasted.
- [ ] **A9** and **A10** answered in writing.
- [ ] The probes deleted; `git status --short` unchanged from 0.2.

---

## Task 1 — The seventh vocabulary member, swept by hand

**Depends on:** Task 0.

Files: `include/marrow/editor/authoring.hpp`, `src/editor/authoring.cpp`,
`src/editor/agent_handlers_editing.cpp`, `src/editor/timeline_controller.cpp`.
**No new translation unit; no `CMakeLists.txt` change.**

This task adds **no behaviour a test can see on its own** — it is the
substrate every later task stands on. It is a task rather than a preamble
because design §1.1 measured that the compiler will not police it.

### 1.1 The enum

`TimelineKeyKind::Inherit`, **appended last** (design §2.1). Never inserted:
`scale_keyframe_times` stores `static_cast<int>(kind)` in its identity tuple
(`authoring.cpp:2299-2300`) and its `affected` set (`:2363-2366`).

### 1.2 Sweep `src/editor/authoring.cpp` against design §1.1's table

Work the table top to bottom, S1 → S21, and **write the arm even where the
fall-through is already correct.** For each, record in a scratch file whether
the arm is observable; that scratch file becomes §B.1's AGENTS.md paragraph.

The five that are silently *wrong* without an arm, and therefore the ones to get
right first:

- **S1 `resolve_timeline_key` (`:195`)** — a new
  `case TimelineKeyKind::Inherit:` matching on
  `edit.animation_name == selector.animation_name && edit.bone_name ==
  selector.bone_name`, then `matching_key_index(…, selector.time, 0U)`, failing
  with `fail("inherit")`. Ordinal is a literal `0U`, exactly as the other four
  non-event families do (`:216`, `:230`, `:244`, `:271`, `:288`).
- **S7 `scale_lane_label` (`:621`)** — `"inherit key '" + edit.bone_name + "'"`.
- **S9 `apply_resolved_scale` (`:830`)**, **S10 `include_resolved_retime_bounds`
  (`:863`)**, **S11 `apply_resolved_retime` (`:942`)** — all three are `void`
  with no trailing statement. S10 calls `include_timeline_retime_bounds` with
  `family_key_spacing(resolved.kind)` and **no**
  `include_loop_boundary_retime_pins` (inherit carries no `loop_sync`).
- **S13 `scale_keyframe_times`'s validate switch (`:2372-2412`)** —
  `validate_projected_scale(candidate.bone_inherit_timeline_edits[timeline_index], …)`.

Plus the three six-element lists: **S12** `sort_retimed_timelines` (`:986`),
**S22** `rename_all_timeline_edits` (`:123-130`), **S23**
`erase_all_timeline_edits` (`:132-139`), and **S24**
`auto_extend_explicit_animation_durations` (`:2087-2098`) with plain
`include_animation_timeline_maximum` (design §2.8).

### 1.3 Sweep the other two files

- `agent_handlers_editing.cpp`: **S25** `timeline_key_kind_name` (`:53-62`) →
  `"inherit"`; **S26** `timeline_key_curve_value` → the null arm; **S27**
  `parse_timeline_key_selectors`'s `"inherit"` branch, requiring `bone` and
  rejecting `"Inherit … keys require bone."` when absent.
- `timeline_controller.cpp`: **S28** `timeline_key_kind_carries_easing`
  (`:1238-1250`) → `false`.

### 1.4 Verify

```bash
cmake --build build
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_agent_dispatch_smoke
MARROW_CONFIG_HOME=/tmp/mar185-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2
```

All green and **unchanged** — nothing yet constructs an inherit selector, so
this task must be behaviour-neutral. A test that changes here is a bug.

### 1.5 Inversions

**None from this task alone.** The five observable arms are attributed to cases
that do not exist yet (I3/I7 → P6, I6 → P7, I4/I5 → P8, I10 → U1, I11 → U2,
I14 → P9, I16 → A2, I17 → A5) and are run in the tasks that add those cases.
Say exactly this in the task log rather than inventing a Task 1 inversion.

---

## Task 2 — The removal primitive

**Depends on:** Task 1.

Files: `include/marrow/editor/authoring.hpp`, `src/editor/authoring.cpp`,
`src/samples/editor_project_smoke.cpp`.

### 2.1 Test first — `validate_mar185_inherit_editing`

Add one function following the file's shape
(`validate_mar178_scenario_*`, `validate_mar179_constraint_parameters`),
registered in `main()`'s **marker-gated `else`** after
`validate_mar180_lifecycle_refuses_active_transaction`
(`editor_project_smoke.cpp:14204-14206`). Per §A3 that is the only place it can
run.

Its throwaway projects follow `mar178_open_skin_session` (`:11763-11790`):
`MinimalProjectOptions` with
`skeleton_path = absolute("assets/fixtures/skin_inherit_constraints.mskl")`,
`atlas_paths = { absolute("assets/fixtures/player_idle.matl") }`,
`active_animation = "toggle_inherit"`, into a scratch directory. **Every
project-only overlay is `{0.0, NoScale}` + `{0.4, Normal}`** — pruning-safe per
Task 0.9.

Add, **in this order** (order matters for §B's attributions):

**P0 — the non-effect witness.** Over the `result` `main()` already holds for
`player_idle.marrow`: `bone_inherit_timeline_edits.empty()`, and
`serialize_project(*result.project)` has exactly Task 0.6's byte length and
SHA-256. Print both every run. *(No inversion — §B.1.)*

**P3 — removal, including the floor.** On `child` (4 base keys), through
`ensure_bone_inherit_timeline_edit` + `remove_inherit_timeline_keys`:
1. Remove `1.0`, then `0.5`, then `0.25` — each succeeds, `removed_key_count ==
   1`, `effective_key_count` counts down 3, 2, 1.
2. Remove `0.0` — **rejected**, `changed == false`, and
   `result.error` contains `must keep at least one key` **and** `child`.
3. `save_project` → `load_project(path)` → `export_runtime_assets`, re-parse the
   `.mskl` with `load_skeleton_data`, and assert `child`'s inherit timeline has
   **1** key at `0.0` with mode `Normal` — **not** the imported four.
*(AC1's Remove. I9 → assertion 3.)*

**P5 — rejection atomicity.** On `child`, **with no project edit yet** — this is
load-bearing, and the case must say so in a comment: with an edit present
`ensure` is a silent no-op and I8 cannot bite. Capture
`serialize_project(*project)` before, call `remove_inherit_timeline_keys` for
time `0.31` (matching no key), and assert:
1. `!result`, `error` contains `no inherit key exists at time` and `0.31` and
   `child`;
2. `changed == false`;
3. `serialize_project` byte-identical.
*(AC3's "rejection leaves the project unchanged". I8 → assertion 3.)*

Also assert the four cheap rejections, each on its **message**, never on a bare
`!result`: unknown animation, unknown bone, a non-finite time, and two requested
times within `1e-6` of each other.

### 2.2 Watch it fail

```bash
cmake --build build --target marrow_project_smoke
```

Must fail **to compile** — `remove_inherit_timeline_keys` does not exist. That
is the correct first failure for a new function.

### 2.3 Implement

`InheritKeyRemovalResult` and `remove_inherit_timeline_keys` declared in
`authoring.hpp` after MAR-184's `merge_inherit_timeline`, defined in
`authoring.cpp` beside it. Steps 1-8 exactly as design §2.4 lists them, then the
candidate copy, `ensure`, the descending erase, the counts, and
`*project = std::move(candidate);`.

Copy the candidate shape from `authoring.cpp:2074/2123`. The `1e-6` epsilon is
the file-local `kKeyTimeEpsilon` (`:149`), which is
`timeline_model::kKeyTimeEpsilon` (`timeline_model.hpp:20`) and
`find_keyframe_near_time`'s default (`project.hpp:147-150`).

### 2.4 Verify + inversions

```bash
cmake --build build && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

Run **I8 → P5** and **I9 → P3**. For I9, additionally demonstrate uniqueness:
neuter P5's rejection assertions with `if (false && …)` and confirm P3 is still
the case that fails. Record both exact failure texts; restore; `touch`; rebuild.

---

## Task 3 — Retime and scale

**Depends on:** Task 2.

Files: `src/samples/editor_project_smoke.cpp` only. **No production code in this
task** — Task 1 already wrote every arm; this task is what makes four of them
observable.

### 3.1 Test — P6, P7, P8

**P6 — a retime that has room.** One `TimelineKeySelector{Inherit,
"toggle_inherit", "child", time 0.25}` after materializing the edit; delta
`+0.15`, no frame snapping. Assert:
1. `bool(result)` and `key_count == 1`;
2. `applied_delta == 0.15` within `1e-12`;
3. after `save_project` → **`load_project(path)`**, the stored key time is
   `0.40`, not `0.25`.
*(I3 → assertion 1, I7 → assertion 3. Two different mutations, two different
assertions, one case; both are recorded separately.)*

**P7 — a retime that must clamp.** The same key, delta `+0.40`, which would
cross the unselected `0.5` neighbour. Assert `applied_delta == 0.249` within
`1e-9` (`0.5 - 0.001 - 0.25`), and that the stored time after reload is `0.499`.
*(I6.)*

**P8 — scale, three assertions.** Select `child`'s keys at `0.25` and `0.5`,
pivot `RangeStart`.
1. A ratio that projects them 0.4 ms apart is **rejected**, `changed == false`,
   and `error` contains `inherit key 'child'` **and** `minimum separation`.
   *(I4 on the rejection existing; I5 on the label.)*
2. A legal ratio succeeds and every moved time equals
   `pivot + (t - pivot) * scale` to `1e-12`. *(I13 — **only if Task 0.10 said it
   is viable**.)*
3. `serialize_project` is byte-identical across the rejection in assertion 1.

### 3.2 Watch them fail

Against Task 1's tree these fail at the assertions, not at compile: P6 with
`key_count == 0` (nothing constructs an inherit selector until now), P8 with the
scale accepted.

### 3.3 Verify + inversions

**I3 → P6, I7 → P6, I6 → P7, I4 → P8, I5 → P8, I13 → P8.** Six mutations, six
recorded failure texts. For I6, demonstrate uniqueness by neutering P6 and
confirming P7 still fails. For I5, confirm I4's arm is present so the rejection
still occurs and only the message differs.

---

## Task 4 — Duration auto-grow and the animation cascade

**Depends on:** Task 3.

Files: `src/samples/editor_project_smoke.cpp` only for P9/U1/U2 — Task 1 wrote
the three seventh lines.

### 4.1 Test — P9, U1, U2

**P9.** Set an explicit duration of `1.0` on `toggle_inherit`, author an inherit
key at `1.4`, call `auto_extend_explicit_animation_durations`, and assert the
duration is now `float32(1.4)`. *(I14.)*

**U1.** A project with an inherit overlay on `controller`; `rename_animation`
`toggle_inherit` → `toggle_two`; `save_project` → `load_project(path)`. Assert
the reloaded edit's `animation_name` is `toggle_two` **and** the materialized
skeleton's `toggle_two` carries the inherit timeline. *(I10.)*

**U2.** Same project plus a second animation so the last-animation guard
(`authoring.cpp:1960-1962`) does not fire; `delete_animation toggle_two`; save →
load. Assert `bone_inherit_timeline_edits` no longer names it **and** the
reloaded catalog does not contain it. *(I11.)*

U2's second assertion is the one that bites: under I11 the orphan edit makes
`build_runtime_document` re-create the animation from `ensure_object_member`.

### 4.2 Verify + inversions

**I14 → P9, I10 → U1, I11 → U2.** Three recorded failure texts. Demonstrate
U1/U2 uniqueness by neutering U1 and confirming U2 still fails under I11.

---

## Task 5 — The clipboard cascade, UI-free

**Depends on:** Task 4.

Files: `src/editor/timeline_model.{hpp,cpp}`, `src/tests/timeline_model_tests.cpp`.

### 5.1 Test first — U3

In `marrow_timeline_model_tests`, over a bare `Clipboard` with
`has_data = true`, `animation_name = "toggle_inherit"`, `earliest_time = 0.25`,
and one fragment entry:

1. `cascade_animation_rename(&c, "toggle_inherit", "toggle_two")` →
   `animation_name == "toggle_two"`, `earliest_time` and `has_data` and the
   fragment bit-unchanged.
2. `cascade_animation_rename(&c, "other", "x")` → nothing changes.
3. `clipboard_time_shift(c, "toggle_two", 0.5)` → `0.25`, where before the
   cascade it was `nullopt`.
4. `cascade_animation_delete(&c, "toggle_two")` → `has_data == false` and the
   fragment cleared; `clipboard_time_shift` → `nullopt`.
5. `cascade_animation_delete(&c, "other")` on a fresh clipboard → nothing
   changes.

*(I12. Pure, UI-free, no session — nothing in the shell can stand in for it.)*

### 5.2 Watch it fail

Compile failure: the two functions do not exist.

### 5.3 Implement + call site

Declare beside `clipboard_time_shift` (`timeline_model.hpp:108-111`), define in
`timeline_model.cpp` beside it (`:396-407`). Call both from
`apply_animation_catalog_action` (`shell_project_panels.cpp:606-614`),
immediately after `sync_shell_from_editor_session(state)` and before the
selection clear, keyed on the action kind.

**Record in a code comment** that this fixes all seven families because
`Clipboard::animation_name` is one field, and that the **agent** rename path
still leaves it stale (design §2.9, §10.5).

### 5.4 Verify + inversion

```bash
cmake --build build && ./build/marrow_timeline_model_tests
```

Run **I12 → U3**. Record the failure text.

---

## Task 6 — The shell: selection, add/edit, remove, clipboard

**Depends on:** Task 5.

Files: `src/editor/timeline_model.cpp`, `src/editor/timeline_controller.{hpp,cpp}`,
`src/editor/shell_smoke_timeline.cpp`, `src/editor/shell_smoke_scenarios.hpp`,
`src/editor/shell_smoke.cpp`.

### 6.1 Test first — S1-S6 and P1

**P1** (in `marrow_project_smoke`, so it runs first in the verification order):
after making the lane editable, assert `track_is_editable` is true for a
`bone:<i>:Inherit` row built by `build_tracks` over a materialized skeleton, and
that `timeline_key_selector`'s project-layer equivalent resolves. *(I2.)*

**S1-S6** in a new `validate_inherit_editing_shell_smoke(const
std::filesystem::path&)` in `shell_smoke_timeline.cpp`, declared in
`shell_smoke_scenarios.hpp` and called from `shell_smoke.cpp` beside
`validate_timeline_scale_shell_smoke` (`:124-127`). It builds its **own**
project over `skin_inherit_constraints.mskl` (the `write_skin_fixture_project`
pattern, `shell_smoke_constraints.cpp:115-131`) — the shell smoke has no marker
gate, unlike `marrow_project_smoke` (§A3).

Wrap it in a `ScopedPreferenceIsolation`, as every sibling scenario does, and
guard on `agent_operation_descriptor_count() != 66U` — **the new number**, in
the same shape as the ten existing guards.

| # | Content |
| --- | --- |
| S1 | Playhead `0.75`, select `bone:2:Inherit`, `add_timeline_key_at_playhead`. Assert one new key, `undo_count() == before + 1`, and mode `OnlyTranslation`. *(I1.)* |
| S2 | Playhead exactly `0.5`, same call. Assert the key **count is unchanged** and the mode was replaced in place — Add is Edit |
| S3 | Select the keys at `0.25` and `0.5`, `copy_selected_timeline_keys`, move the playhead to `0.6`, `paste_timeline_clipboard`. Assert the pasted pair lands at `0.6` and `0.85`, the vector is strictly increasing, and one history entry was added |
| S4 | Clear the selection, playhead exactly on a key, `remove_selected_timeline_keys` → the key is gone. Then playhead 5 ms off → `status_message == "No authored key exists at the playhead"` and nothing changed |
| S5 | Snapshot `serialize_project`, the preview animation name, `selected_keys`, `active_key`, and `undo_count()`. Open a retime gesture, apply a delta, **cancel**. Assert all five are bit-identical. AC3 |
| S6 | Copy, then `apply_animation_catalog_action(Rename)`. Assert `clipboard.animation_name` followed and `clipboard_time_shift` now returns a value. *(Second detector for I12; U3 is first)* |

### 6.2 Watch them fail

S1 fails first: `add_timeline_key_at_playhead` returns false because
`timeline_track_is_editable` is false.

### 6.3 Implement

1. `track_is_editable` (`timeline_model.cpp:240-249`) gains
   `|| track.kind == TimelineTrackKind::Inherit`. **Keyed on `kind`, not on an
   id substring** (design §2.13).
2. `timeline_key_selector` (`timeline_controller.cpp:891-940`) gains an Inherit
   branch **before** the two slot branches, keyed on
   `track.kind == TimelineTrackKind::Inherit && track.bone_index.has_value() &&
   *track.bone_index < skeleton.bones().size()`, setting
   `selector.kind` and `selector.bone_name`.
3. `make_bone_inherit_timeline_edit`, `ensure_bone_inherit_timeline_edit_index`,
   and `sample_inherit_keyframe` — the seventh member of three families
   (`:476-503`, `:524-543`, `:583-641`). `sample_inherit_keyframe` calls
   `AnimationData::sample_bone_inherit`
   (`include/marrow/runtime/skeleton.hpp:438`) on the effective animation at
   `state->timeline_time_seconds`, falling back to
   `skeleton.bones()[*track.bone_index].inherit`.
4. `visit_editable_timeline_keys` (`:960`) and
   `visit_existing_project_timeline_keys` (`:1008`) each gain a branch.
5. `add_timeline_key_at_playhead`'s `if constexpr` chain (`:1150-1191`) gains an
   `InheritKeyframeEdit` arm calling `sample_inherit_keyframe`. The generic
   `find_keyframe_near_time` replace path (`:1198-1206`) needs no change.
6. `copy_selected_timeline_keys` (`:1932`) and `paste_timeline_clipboard`
   (`:2050`) each gain a branch. **The `loop_sync` scrub loop at `:2028-2037`
   gets no seventh entry** — `BoneInheritTimelineEdit` has no such member
   (design §2.5); leave a comment saying so.

### 6.4 Verify + inversions

```bash
cmake --build build
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
MARROW_CONFIG_HOME=/tmp/mar185-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2
```

Run **I2 → P1** and **I1 → S1**. For I1, demonstrate uniqueness: neuter S2
(which authors no seed) and confirm S1 is still the failing case.

---

## Task 7 — The widget, and the frame that can see it

**Depends on:** Task 6.

Files: `src/editor/shell_timeline.cpp`, `src/editor/shell_smoke_frames.cpp`.

### 7.1 Test first — F1

In `shell_smoke_frames.cpp`, following the established mechanics from
`AGENTS.md`'s **Headless Frame Smoke Notes**:

1. Select the Inherit row and render one `draw_timeline_window(&shell_state)`
   frame.
2. Sweep the mouse across the editor panel, comparing `ImGuiContext::HoveredId`
   against `ImGui::FindWindowByName(kTimelineWindowTitle)->GetID("Mode")`
   **and** against `GetID("Time")`.
3. Assert **both** matched. The `Time` half is what distinguishes "the combo was
   deleted" from "the `GetID` seed is broken" — the note's own rule: always
   sweep at least one control that already exists.
4. Open the combo, click `noScale`, and assert the stored mode changed and one
   history entry was added.

Mechanics that must be honoured or the case is silently wrong:

- The combo must sit at **plain window scope** — inside the `CollapsingHeader`,
  with every `BeginChild` closed before it. A surviving `PushID`, a
  `BeginTabItem`, or a wrapping child breaks the `GetID` seed and the sweep
  reports the widget "absent".
- A widget on a `SameLine()` needs its own sweep column. Put the combo on its
  own line, or sweep two columns.
- Advance `io.DeltaTime` past `io.MouseDoubleClickTime` between the sweep and
  the click, or ImGui reads two presses at one pixel as a double click.
- Do **not** try `io.AddKeyEvent(ImGuiMod_Ctrl, …)` anywhere in this case; on
  macOS `io.ConfigMacOSXBehaviors` swaps Cmd and Ctrl at the event layer and
  turns the left press into a right click.

### 7.2 Watch it fail

The sweep finds `Time` and not `Mode`, because
`draw_inherit_timeline_editor` does not exist and the dispatcher falls through
to the transform body's read-only message (`shell_timeline.cpp:1535-1540`).

### 7.3 Implement

- `draw_inherit_timeline_editor(ShellState*, const TimelineTrackRow&)` modelled
  on `draw_slot_attachment_timeline_editor` (`:1405-1497`) — the closest
  sibling: stepped, discrete, easing-free. Per key, a `Time` `DragScalar` and a
  `Mode` combo over the five tokens, each write going through the same
  `apply_project_command_change` path the siblings use.
- A dispatcher branch in `draw_transform_timeline_editor`'s chain
  (`:1514-1533`), keyed on `track->kind == TimelineTrackKind::Inherit`.
- The two tooltips at `:2277` and `:2296` lose "(Inherit remains read-only)".

### 7.4 Verify + inversion

```bash
cmake --build build
MARROW_CONFIG_HOME=/tmp/mar185-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2
git grep -c "Inherit remains read-only" -- src/    # must now be 0
```

Run **I15 → F1**: delete the combo's emission, confirm **every UI-free case
stays green** and only F1 fails, and record both facts. That contrast is the
evidence, not F1's failure alone.

---

## Task 8 — The two agent operations and MCP parity

**Depends on:** Task 7.

Files: `src/editor/agent_dispatch.cpp`, `src/editor/agent_handlers_editing.cpp`,
`src/samples/agent_dispatch_smoke.cpp`, `src/editor/shell_smoke_constraints.cpp`,
`src/editor/shell_smoke_timeline.cpp`, `src/editor/shell_smoke_graph.cpp`,
`tools/mcp/tools/editing.py`, `tools/mcp/test_client.py`.

### 8.1 Test first — A1-A7

In `agent_dispatch_smoke.cpp`, against `player_idle.marrow`, animation `idle`,
bone `spine`. **No case writes a lone `{0.0, "normal"}` key** — `player_idle`'s
bones are all setup-`Normal` and Task 0.9 measured what that does.

Two `OperationExpectation` rows beside the attachment pair (`:94-95`):

```cpp
{"set_inherit_keyframe",    "edit", true, false, true},
{"remove_inherit_keyframe", "edit", true, false, false},
```

and the array bound at `:39` becomes `66`.

| # | Content |
| --- | --- |
| A1 | The existing registry-integrity block already asserts `agent_operation_descriptor_count() == kExpectedOperations.size()` (`:985-988`). *(I19 — a **shipped** detector, recorded as such)* |
| A2 | `set_inherit_keyframe {idle, spine, 0.25, "noScale"}` → `ok`; echoed `kind == "inherit"`; `affected_keys` contains `{0.25, "noScale"}`; `timeline.describe {animation: idle}` reports `bone_inherit_timelines == 1`. *(I16 on the echo)* |
| A3 | The same with `dry_run: true` and `"onlyTranslation"` → `ok`, `dry_run: true`, `affected_keys` shows the would-be mode, and a following `timeline.describe` plus a `set`-free read shows the **stored** mode is still `noScale` |
| A4 | `remove_inherit_keyframe {…, dry_run: true}` → `!ok`, `error.code == "dry_run_unsupported"`, **and the key still exists**. *(I18 — the second half is what makes it bite)* |
| A5 | `timeline.retime_keyframes {keys: [{kind: "inherit", bone: "spine", time: 0.25}], delta: 0.1}` → `ok` and the key moved. *(I17)* |
| A6 | `set_inherit_keyframe` → `export.preview` → `undo` → `export.preview` → `redo`. `runtime.validate` passes after each; `export.preview`'s `targets` array is byte-identical throughout; `undo` restores the previous `bone_inherit_timelines` count |
| A7 | Three rejections: `inherit: "noScales"`, an unknown bone, `time: -1`. Each names its offending value; `project.diagnostics`' `project_dirty` is unchanged across all three |

### 8.2 Implement

- **Registry** (`agent_dispatch.cpp:85`+): the two rows from design §2.10.
  `set_inherit_keyframe` has `dry_run_supported = true`;
  `remove_inherit_keyframe` has `false` — matching every other
  `set_*`/`remove_*` pair (`:67-87`).
- **Handlers** beside the slot pair (`agent_handlers_editing.cpp:2882-3040`).
  `set_inherit_keyframe` builds a one-key `InheritTimelineMergeRequest` with
  `replace_existing_times = true` and calls MAR-184's
  `merge_inherit_timeline`; `remove_inherit_keyframe` calls
  `remove_inherit_timeline_keys` with one time. **Neither reimplements
  validation** — that is what keeps the GUI and the agent from disagreeing.
- `bone_inherit_timelines` in `timeline_description_value`
  (`agent_dispatch.cpp:1020`+), additive.
- **The thirteen count sites**: `agent_dispatch_smoke.cpp:39`; the ten
  `!= 64U` guards; `tools/mcp/test_client.py:53,55`.
- **MCP**: two `types.Tool` entries beside `remove_attachment_keyframe`
  (`tools/mcp/tools/editing.py:1207-1234`), with `required` =
  `["animation","bone","time","inherit"]` for the setter and
  `["animation","bone","time"]` for the remover, and `dry_run` in the setter's
  `properties` only.

### 8.3 Verify + inversions

```bash
cmake --build build
./build/marrow_agent_dispatch_smoke
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
python3 -m py_compile tools/mcp/server.py tools/mcp/test_client.py \
                      tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
```

Run **I16 → A2, I17 → A5, I18 → A4, I19 → A1**. For I18, confirm both halves:
the missing error code **and** the removed key.

---

## Task 9 — Prove the scope did not leak, and the count sweep is complete

**Depends on:** Tasks 1-8. Not an assertion — a diff.

```bash
git diff --stat -- src/editor/timeline_graph_model.cpp \
                   src/editor/timeline_graph_model.hpp \
                   src/editor/session.cpp \
                   include/marrow/c/ src/c/ \
                   include/marrow/runtime/ src/runtime/ \
                   assets/fixtures/ CMakeLists.txt
```

Must be **empty**.

- `timeline_graph_model.*`: MAR-167 excluded `Inherit` deliberately (`:393`,
  `:478`) because `BoneInherit` is an unordered enum with no numeric axis.
  Inversion **I20**: add `Inherit` to `track_is_graphable`, confirm this diff
  becomes non-empty, restore, `touch`.
- `session.cpp`: `animation_timing_equal` already folds
  `bone_inherit_timelines` in (`:126`) and `edit_animation_catalog` is
  family-agnostic (`:2365-2470`). An empty diff is the proof that nothing was
  needed.
- `include/marrow/runtime/`, `src/runtime/`, `include/marrow/c/`, `src/c/`:
  `.mskl` stays 1, `.mbin` stays 2, the C ABI is untouched.
- `assets/fixtures/`: `marrow_project_smoke` **aborts** on a partial marker
  match (`editor_project_smoke.cpp:14073-14082`). Never edit a fixture.
- `CMakeLists.txt`: no new translation unit.

Then the count sweep, **inverted from every prior story** — this one *does* move
the number, so the failure mode is an incomplete substitution:

```bash
git diff -U0 -- src/ tools/ | grep -E '^[+-]' | grep -E '\b(6[3-7])\b'
grep -rn "!= 64U" src/                # must be empty
grep -rn "!= 66U" src/ | wc -l        # must be 10
grep -n "OperationExpectation, " src/samples/agent_dispatch_smoke.cpp   # 66
grep -n "== 6[46]" tools/mcp/test_client.py                             # two 66s
grep -c '^\s*{\s*"' src/editor/agent_dispatch.cpp                       # 66
```

Every line the first grep returns is inspected **by hand** and explained in the
AGENTS.md entry. A bare `64` also matches `t = 0.64` and "64 lines"; the
untouchable literals are Task 0.5's list.

---

## Task 10 — Full verification

**Depends on:** Tasks 1-9. Run every command, record every output.

```bash
rm -rf build                                   # H1: the from-scratch rebuild
cmake -S . -B build
cmake --build build

# Model layers
./build/marrow_unit_tests
./build/marrow_timeline_model_tests            # U3 -- this story's coverage
./build/marrow_timeline_graph_model_tests
./build/marrow_selection_tests
./build/marrow_preference_tests
./build/marrow_viewport_interaction_tests
./build/marrow_windowing_tests
./build/marrow_pen_input_tests
./build/marrow_agent_socket_tests

# Runtime -- unchanged by this story
./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
python3 -m json.tool assets/fixtures/skin_inherit_constraints.mskl > /dev/null

# Project layer -- P0-P10, U1, U2, inside the standing invocation
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke --create /tmp/mar185_created.marrow
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
  --export-runtime /tmp/player_idle_project_export.mskl \
  --export-binary  /tmp/player_idle_project_export.mbin
./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin \
                                 /tmp/player_idle_project_export.mskl

# Agent -- A1-A7
./build/marrow_agent_dispatch_smoke

# Shell -- S1-S6, F1, plus C4-C25 as regression witnesses
MARROW_CONFIG_HOME=/tmp/mar185-cfg ./build/marrow_editor_shell \
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

# MCP parity -- coverage, not a witness: the registry moved
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
| 1 | Every case P0-P10, U1-U3, S1-S6, F1, A1-A7, M1 ran and passed | Each binary's output names them |
| 2 | **Every inversion in §B was run against the FROM-SCRATCH build, bit the named case, and was restored** | Nineteen entries. Record the **exact** failure text for each, compared with `cmp`/`diff` over a whole recorded string (H2). Any that did not bite is recorded as such, with how the case was strengthened — **not** quietly dropped |
| 3 | **Uniqueness was demonstrated, not asserted** | For I6, I9, I1, I11: neuter the earlier candidate detector with `if (false && …)`, re-run, confirm the attributed case is still the first to fail. Record each |
| 4 | Every restore was followed by `touch` and a rebuild | H1. State it explicitly |
| 5 | **Falsifiability re-demonstrated after any rewrite** | H3. If any case was rewritten or recovered after its inversion ran, re-run that inversion against the final tree |
| 6 | `serialize_project(player_idle.marrow)` unchanged | P0's printed length + SHA equal Task 0.6's |
| 7 | Registry is **66** everywhere | 66 rows / array bound 66 / ten `!= 66U` / two Python `== 66`; zero `!= 64U` remaining |
| 8 | The untouchable trees are byte-identical | Task 9's `git diff --stat` |
| 9 | `ctest -N` total unchanged from Task 0.2 | This story registers no CTest |
| 10 | `~/Library/Application Support/Marrow` **ABSENT** after the whole run | `test -e ~/Library/Application\ Support/Marrow && echo PRESENT \|\| echo ABSENT`. PRESENT is a hard failure |
| 11 | `git grep -c "Inherit remains read-only" -- src/` → **0** | Task 0.7's witness, inverted |
| 12 | Task 0's A6/A7/A8/A9/A10 answers recorded | Pasted verbatim into AGENTS.md |
| 13 | `marrow_inspect --compare` → `matches` | Runs over `player_idle`, which now **can** carry inherit data if A2's edit persisted — so state whether it did |
| 14 | Count sweep inspected by hand | Task 9's grep output, every line explained |

---

## Task 11 — Documentation and commit

**Depends on:** Task 10, complete and green.

### 11.1 `docs/root1/format-spec.md`

MAR-184 adds the `#### inherit` subsection. MAR-185 extends it with the
**editing** rules, since the on-disk shape does not change:

- Inherit keys are stepped and carry no `curve`, no `curve_mode`, no
  `curve_driver`, and no `loop_sync` — with the reason (a stepped lane has no
  boundary easing and no scalar to drive a tangent).
- Two inherit keys of one lane can never share a time; the minimum authored
  separation the editor enforces is 1 ms, the same as every non-event family.
- **An inherit timeline edit must keep at least one keyframe.** Emptying it does
  not clear the lane — both serializers skip an empty edit and materialization
  assigns into a copy of the base document, so an emptied edit restores the
  imported track. The editor refuses the last removal and names the remedy.
- A lane reduced to one key at time zero whose mode equals the bone's setup
  `inherit` is **pruned by the runtime** at load
  (`skeleton_animation.cpp:275-279`), exactly as a rotate lane with a single
  zero-angle origin key is. Pre-existing, shared, and not diverged from.
- `.mskl` stays version 1, `.mbin` stays version 2, `.marrow` gains no key.

### 11.2 `AGENTS.md`

A `## MAR-185 Complete Inherit Timeline Editing Parity Validation Results`
section, matching the MAR-183 and MAR-184 sections' structure:

- **Opening paragraph.** What the story did and did not touch: no format
  change, no runtime file, no fixture, no CMake target, no graph-model change,
  no `session.cpp` change; `.mskl` v1 / `.mbin` v2 / C ABI untouched. And the
  one number that **did** move: the registry, 64 → **66**, at thirteen sites.
- **"The compiler does not police this."** The `-Wswitch` finding (Task 0.4)
  stated as a durable fact for the next person who adds a `TimelineKeyKind`
  value, with design §1.1's table referenced by name.
- **"What was measured before any code was written."** Task 0's values verbatim,
  including the A6 build output, the A8 pruning M-gate output, and the `ctest -N`
  total.
- **"Result"** — one row per check.
- **"Inversions run"** — all nineteen, each with the case it bit and the exact
  message, plus the four uniqueness demonstrations from checklist row 3. Any
  that did not bite recorded as such.
- **"Document errors found"** — §A.1's five rows, plus anything Task 0 turned
  up. Every story from MAR-175 on has found between three and seven; if fewer
  than §A.1 already lists are found, one has been lost — say so explicitly.
- **"Not independently covered"**, carrying forward and adding:
  - **§B.1 verbatim**: the eleven fall-through arms, the three unreachable
    write arms, `sort_retimed_timelines`' provably-no-op seventh call,
    `export.preview`'s non-effect assertion, and P0's byte-identity witness —
    each with the reason no inversion exists.
  - **The build still has no `-Wswitch`**, and MAR-185 did not add it. The next
    `TimelineKeyKind` value faces the same silent sweep.
  - **An inherit lane can still be pruned out of existence** by reducing it to
    one key at t = 0 matching the bone's setup inherit — identical to rotate,
    translate, scale and shear, and deliberately not diverged from. Once pruned
    the lane cannot be re-created from the dopesheet.
  - **An agent-driven `animation.rename` leaves the GUI clipboard stale.** The
    GUI path cascades; the agent path has no shell hook that distinguishes a
    rename from a selection change.
  - **Only the `Mode` combo is proved on screen.** The lane's diamonds, the
    key editor's `Time` drag, and the toolbar buttons are covered UI-free only.
  - **`merge_inherit_timeline`'s `replace_existing_times = false` arm** has no
    product caller; only MAR-184's own case exercises it.
  - Carry forward MAR-183's `shell_main.cpp` frame-body and `commit_path_choice`
    notes, and MAR-184's runtime-parser asymmetries, unchanged.

Extend the existing `## Current Validation` shell line (the
`marrow_editor_shell` entry) with MAR-185's scenario, and note that
`marrow_timeline_model_tests` now also covers the clipboard cascade. **Do not
add a new `marrow_project_smoke` command line** — the existing
`AGENTS.md:43` line gains a clause, per §A3.

### 11.3 The PRD

Set `MAR-185`'s `status` to `"done"` and `completedAt` to the run's date in
`.agents/tasks/prd-marrow-runtime.json`. Touch no other story.

### 11.4 Commit

One commit for the story.

```bash
git add -A
git commit
```

Subject, matching the arc's Korean convention:

```
feat: MAR-185 상속 타임라인 편집 패리티 완성
```

Body, in Korean, covering: `TimelineKeyKind::Inherit`를 마지막에 추가하면서
`-Wswitch`가 없어 컴파일러 경고가 전혀 나오지 않는다는 사실과 그래서 29개
스위치 지점을 표로 손수 훑었다는 것; `remove_inherit_timeline_keys`의
preflight-then-mutate 계약과 "마지막 키는 지울 수 없다"는 바닥 규칙 및 그 이유
(빈 편집은 두 직렬화기 모두가 건너뛰므로 임포트된 기본 트랙이 되살아난다);
리타임의 클램프와 스케일의 거부가 1 ms 간격 규칙을 공유한다는 것; 클립보드
캐스케이드는 `Clipboard::animation_name`이 단일 필드라 일곱 패밀리 전부를 한
번에 고친다는 의도된 결정과 에이전트 경로는 여전히 남는다는 한계; 두 에이전트
연산과 레지스트리 64 → 66 이동이 열세 곳에서만 일어났다는 것; 그리고 `.mskl`
1, `.mbin` 2, C ABI, 그래프 모델, `session.cpp`는 전부 그대로라는 명시.

Trailers:

```
Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: <session id>
```

---

## Risks

| # | Risk | Mitigation |
| --- | --- | --- |
| R1 | **Starting before MAR-184 lands.** None of its symbols exist at `a2981b8` | Task 0.3's dependency probe must compile and link first |
| R2 | **A missed switch arm with no compiler diagnostic.** Five of twenty-nine are silently wrong | Design §1.1's table is the checklist; Task 0.4 proves the compiler is silent; I3/I4/I6/I7 are the inversions that catch the five |
| R3 | **A rejection case that asserts `!result`** rather than the message | Every rejection case in Tasks 2, 3 and 8 names its expected substrings |
| R4 | **An `ensure_*` on an entity whose edit already exists**, making it a no-op and disarming I8 | P5 runs on `child` with **no project edit yet**, stated as load-bearing in a code comment |
| R5 | **The candidate copy silently disarms "mutate early"** | I8 is defined as *removing* the candidate |
| R6 | **An incomplete 64 → 66 substitution.** This is the first story to move the number | Thirteen sites listed in Task 0.5; Task 9 greps `\b6[3-7]\b` over the diff and requires zero `!= 64U` |
| R7 | **Pointing `marrow_project_smoke` at a new fixture** takes the skip branch and runs nothing | §A3. Build throwaway projects inside the single `player_idle.marrow` invocation, as MAR-177/178 do |
| R8 | **The pruning trap** in the agent smoke, where every `player_idle` bone is setup-`Normal` | Task 0.9's M-gate; no case writes a lone `{0.0, "normal"}` |
| R9 | **A removal that empties an edit resurrects the base track** | Task 2's step 8 and I9 |
| R10 | **A widget with only UI-free coverage.** MAR-178's own scenario printed success while the frame smoke failed by name | F1 is a real mouse in a real frame, and I15's evidence is the *contrast*: the UI-free cases stay green |
| R11 | **A broken `GetID` seed reported as a missing widget** | F1 sweeps `Time` in the same pass; both must match |
| R12 | **An inversion result that lies (H1-H4)** | `touch` after every restore; final run from `rm -rf build`; `cmp` over whole strings; re-demonstrate falsifiability after any rewrite; neuter earlier detectors to prove uniqueness |
| R13 | **Non-biting inversions.** Fourteen sites fall through to the correct answer | §B.1 names every one and **no inversion is invented for it**. AGENTS.md records them verbatim |

---

## Dependency graph

```
MAR-184 committed
  └─> Task 0 (measure; A6 -Wswitch gate, A7 dependency probe, A8 pruning gate)
        └─> Task 1 (the seventh vocabulary member -- behaviour-neutral sweep)
              └─> Task 2 (remove_inherit_timeline_keys)          P0 P3 P5
                    └─> Task 3 (retime + scale)                  P6 P7 P8
                          └─> Task 4 (auto-grow + cascade)       P9 U1 U2
                                └─> Task 5 (clipboard cascade)   U3
                                      └─> Task 6 (shell)         P1 P2 P4 P10 S1-S6
                                            └─> Task 7 (widget)  F1
                                                  └─> Task 8 (agent + MCP)  A1-A7 M1
                                                        └─> Task 9 (scope + count sweep)  I20
                                                              └─> Task 10 (full verification, from scratch)
                                                                    └─> Task 11 (docs + one commit)
```

P2 (all five modes), P4 (same-time collision) and P10 (`.mskl`/`.mbin` export)
are project-layer cases with no ordering constraint beyond Task 2's function
existing; they are placed in Task 6 so the whole project suite is written before
the shell work begins. Task 9 can start any time after Task 8's last production
edit; it is placed late because a diff is only meaningful over the finished tree.
