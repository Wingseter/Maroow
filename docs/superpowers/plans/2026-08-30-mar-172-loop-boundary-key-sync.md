# MAR-172 Loop Boundary Key Synchronization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Store loop-boundary synchronization intent as one optional, additive,
`.marrow`-only **lane** flag that leaves every existing project byte-identical;
guarantee that every opted-in lane carries exactly one managed key at the
animation's **explicit** duration whose value and easing record are a bit-exact
copy of that lane's key at time zero; maintain that contract inside the caller's
open transaction at the one seam that is provably after every duration change;
stop selection, clipboard, retime, insertion, and duration edits from producing a
duplicate or stale boundary key; and expose the mutation as the 59th Agent
operation `timeline.set_loop_sync` plus one matching MCP tool — while `.mskl` v1,
`.mbin` v2, C ABI v1, and `editor-settings.json` v1 are all unchanged and the
exported runtime carries the boundary key as an ordinary keyframe and no flag.

**Architecture:** One `bool loop_sync` on `TransformTimelineEdit`,
`SlotColorTimelineEdit`, and `MeshDeformTimelineEdit` in
`include/marrow/editor/project.hpp`. `src/editor/project.cpp` gains
`parse_loop_sync()` and `build_loop_sync_value()` — a pure projection of those
three booleans into an optional top-level `.marrow` tree — plus two
`validate_project_for_save()` rules and **no export change at all**.
`include/marrow/editor/authoring.hpp` gains `TimelineLaneSelector`,
`set_timeline_loop_sync()`, `synchronize_loop_boundaries()`, and
`inferred_duration_excluding_loop_boundaries()`; `set_animation_duration()`,
`auto_extend_explicit_animation_durations()`, and `retime_keyframes()` each gain
one boundary-aware rule that is a bit-exact no-op when nothing is opted in.
`src/editor/session.cpp` calls the sync at exactly two seams, immediately after
`auto_extend_explicit_animation_durations()`. `timeline_controller` gains one
predicate and three clipboard lines. The Agent registry grows by one row. **No
ImGui file is touched.**

**Tech Stack:** C++17, existing Marrow runtime/editor session APIs, CMake/CTest,
Python MCP SDK, JSON fixtures.

**Spec:** `docs/superpowers/specs/2026-08-30-mar-172-loop-boundary-key-sync-design.md`

## Global Constraints

- Read `docs/root1/discription.md` and the MAR-172 PRD story before each
  implementation task; current source and tests override stale plan assumptions.
- Follow strict RED-GREEN-REFACTOR: add one focused failing test, observe the
  expected failure, write the minimum production code, then rerun focused and
  affected regression tests. A task that reports GREEN without having seen the
  RED is not done.
- Do not reset, discard, stash, commit, push, or create a PR unless the user
  separately requests it. Each task ends with a read-only
  `git diff --check` / `git status --short` checkpoint.
- **Never touch `src/runtime/**`, `include/marrow/runtime/**`,
  `include/marrow/c_api/**`, or `src/c_api/**`.** `.mskl` stays version 1,
  `.mbin` stays version 2, and the C ABI stays v1. `git diff --stat` on those
  four paths must be empty at every checkpoint. In particular
  `AnimationData::inferred_duration()` and `AnimationData::duration()` are read,
  never changed. If a task appears to require a change there, stop and report —
  it means the design was misread.
- **Never touch `src/editor/preferences.cpp` or
  `include/marrow/editor/preferences.hpp`.** `kEditorSettingsVersion` stays `1`.
- **Never touch any ImGui translation unit.** `shell_timeline.cpp`,
  `shell_timeline_graph.cpp`, `shell_viewport*.cpp`, `shell_inspector.cpp`,
  `shell_widgets.cpp`, and `shell_project_panels.cpp` must all be unchanged at
  every checkpoint. MAR-172 is UI-free by the story's own words (design §3.1,
  §16). If a task appears to need a widget, stop and report.
- **Never change `build_runtime_document()` or any `build_runtime_*_value()`
  function in `src/editor/project.cpp`.** Export neutrality for the flag is
  achieved by not editing the export path; the boundary key reaches the export
  because it is an ordinary keyframe. Task 7 proves both halves.
- **Never add a `loop_sync` member inside a lane's own JSON value.** The lane
  value is a bare array and promoting it to an object breaks an older build's
  ability to load an opted-in project (design §8.2). The flag lives in one
  optional top-level `loop_sync` tree.
- **`DrawOrderTimelineEdit`, `EventTimelineEdit`, and
  `SlotAttachmentTimelineEdit` gain no member.** The discrete-family exclusion is
  compile-enforced. If a branch appears to need one, stop.
- **Every boundary-aware rule must be a bit-exact no-op when no lane is opted
  in.** That applies to `synchronize_loop_boundaries()`,
  `inferred_duration_excluding_loop_boundaries()`,
  `auto_extend_explicit_animation_durations()`, and `retime_keyframes()`. Each
  gets a direct regression assertion, not an argument.
- Phase 2 of the sync writes easing through the file-local
  `write_key_interpolation()` helper. It must **never** call
  `set_keyframe_interpolation()`, which MAR-171 makes demote to
  `TimelineCurveMode::Manual` — doing so would silently destroy the mirrored auto
  intent on every transaction.
- Never change `retime_keyframes()`'s, `insertable_key_time()`'s, or
  `clamp_existing_key_time()`'s **signature**. MAR-173 inherits them.
- Never change MAR-169's clamp/reject split, MAR-170's preset constants or
  preference flow, or MAR-171's resolver, demotion rule, or metadata — beyond the
  documented `set_animation_duration()` floor change.
- The Agent registry becomes exactly **59** in Task 8 and not before. From Task 0
  through Task 7 inclusive, `std::size(kOperationSpecs)` must still equal the
  count Task 0 measured, and every count assertion in the tree must still pass.
- Automated display tests prove the exercised ImGui/display path only. They add
  no manual-visible-UI, Windows 11, or physical-input qualification credit.
  MAR-192 through MAR-210 stay open.

## File and Responsibility Map

| File | MAR-172 responsibility |
| --- | --- |
| `include/marrow/editor/project.hpp` | one `bool loop_sync{false}` on `TransformTimelineEdit`, `SlotColorTimelineEdit`, `MeshDeformTimelineEdit` |
| `src/editor/project.cpp` | `parse_loop_sync()`, `build_loop_sync_value()`, the `root["loop_sync"]` / `root.erase("loop_sync")` block, two `validate_project_for_save()` rules. **No export change** |
| `include/marrow/editor/authoring.hpp` | `TimelineLaneKind`, `TimelineLaneSelector`, `TimelineLoopBoundaryAction`, `TimelineLoopSyncResult`, `set_timeline_loop_sync()`, `synchronize_loop_boundaries()`, `inferred_duration_excluding_loop_boundaries()`, the two token helpers |
| `src/editor/authoring.cpp` | the two primitives, the excluding floor, the boundary-aware `auto_extend_...` scan, the retime pinning, the token helpers |
| `src/editor/session.cpp` | the sync call at `refresh_runtime()` and `commit()`, after `auto_extend_...` |
| `src/editor/timeline_controller.hpp/.cpp` | `timeline_key_is_managed_loop_boundary()`, its use in the selection collectors and the removal filter, the clipboard flag clear |
| `src/editor/agent_dispatch.cpp` | one `kOperationSpecs` row |
| `src/editor/agent_dispatch_internal.hpp` | the `timeline_lane_selectors_arg()` declaration |
| `src/editor/agent_handlers_editing.cpp` | the `timeline.set_loop_sync` block and `timeline_lane_selectors_arg()` |
| `tools/mcp/tools/editing.py` | one `types.Tool` and one lane schema helper |
| `src/tests/timeline_model_tests.cpp` | every pure lane-level case |
| `src/samples/editor_project_smoke.cpp` | `validate_mar172_loop_boundary_sync()` and the mutated-project export block |
| `src/editor/shell_smoke_graph.cpp`, `shell_smoke_scenarios.hpp`, `shell_smoke.cpp` | `validate_timeline_loop_sync_shell_smoke()`; the registry-count guards |
| `src/samples/agent_dispatch_smoke.cpp` | array size, one expectation row, the behaviour cases |
| `tools/mcp/test_client.py` | the counts, the `new_edit_operations` entry, the sequence |
| `AGENTS.md`, `docs/root1/*.md`, `.agents/tasks/prd-marrow-runtime.json` | closure |

Files that must remain **unchanged**: everything under `src/runtime/`,
`include/marrow/runtime/`, `include/marrow/c_api/`, `src/c_api/`,
`src/editor/preferences.cpp`, `include/marrow/editor/preferences.hpp`, every
ImGui translation unit, `src/editor/shell_state.hpp`,
`src/editor/shell_timeline_graph.hpp`, `src/editor/shell_core.cpp`, and
`src/editor/curve_auto.*` (MAR-171's).

---

### Task 0: Reconcile with the as-built MAR-170 and MAR-171

**This task is mandatory and blocking.** The design spec was written when MAR-171
existed only as an untracked spec and plan, `git log` showed `0a917db` (MAR-170)
as the last feature commit, and `std::size(kOperationSpecs)` was **57**. Every
MAR-171 contract quoted in the spec must be checked against the code that is
actually present. Do not write production code in this task.

- [ ] Confirm the working tree is clean and record `git rev-parse HEAD` and
      `git log --oneline -8`.
- [ ] **Confirm MAR-171 is merged.** If `resolve_automatic_curves()` does not
      exist in `include/marrow/editor/authoring.hpp`, **stop and report**:
      MAR-172 `dependsOn` MAR-171 and the sync's phase ordering (design §9.2) is
      meaningless without a resolver.
- [ ] Read, in full, and record the exact as-built signature or shape of each:
      - `include/marrow/editor/project.hpp` — `TransformTimelineEdit`,
        `SlotColorTimelineEdit`, `MeshDeformTimelineEdit`,
        `DrawOrderTimelineEdit`, `EventTimelineEdit`,
        `SlotAttachmentTimelineEdit`, `TransformKeyframeEdit`,
        `SlotColorKeyframeEdit`, `DeformKeyframeEdit`, `ProjectSnapSettings`,
        `ProjectData` (especially `preserved_root`), `ensure_*_timeline_edit()`.
      - `include/marrow/editor/authoring.hpp` — `TimelineKeyKind`,
        `TimelineKeySelector`, `TimelineScalarComponent` (note MAR-171 may have
        relocated it to `project.hpp`), `set_animation_duration()`,
        `auto_extend_explicit_animation_durations()`, `retime_keyframes()`,
        `set_keyframe_interpolation()`, and everything MAR-171 appended.
      - `src/editor/authoring.cpp` — `ResolvedTimelineKey`,
        `resolve_timeline_key()`, `matching_timeline_index()`,
        `matching_key_index()`, `include_animation_timeline_maximum()`,
        `include_resolved_retime_bounds()`, `include_timeline_retime_bounds()`,
        `read_key_interpolation()`, `write_key_interpolation()`,
        `same_interpolation()`, `set_animation_duration()`,
        `auto_extend_explicit_animation_durations()`, `retime_keyframes()`, and
        MAR-171's `resolve_automatic_curves()`.
      - `src/editor/project.cpp` — `parse_snap_settings()` (`:305`), the `snap`
        block in `build_project_value()` (`:4193`), the `root.erase(...)`
        pattern for optional blocks (`:4211-4240`), `parse_transform_keyframes()`
        (`:1674`), the slot-colour and deform keyframe parsers,
        `build_timeline_edits_value()` (`:3777`), `validate_project_for_save()`
        (`:4757`), and the `find_optional_member` / `validation_error` /
        `require_type` helpers.
      - `src/editor/session.cpp` — the `auto_extend_explicit_animation_durations()`
        blocks in `refresh_runtime()` (`:1317`) and `commit()` (`:1413`),
        including the exact error shape and the `runtime_is_current = false`
        handling, and the one at `rebuild_runtime()` (`:2423`) that MAR-172 must
        **not** touch.
      - `src/editor/timeline_controller.cpp` —
        `collect_curve_preset_selectors()` (`:1194`),
        `apply_timeline_curve_preset()`, `remove_selected_timeline_keys()`
        (`:1396`), `copy_selected_timeline_keys()` (`:1494`),
        `paste_timeline_clipboard()` (`:1600`), `apply_timeline_retime_delta()`
        (`:1861`), `apply_timeline_graph_value_delta()`, and whatever MAR-171
        added.
      - `src/editor/agent_handlers_editing.cpp` — the whole
        `timeline.set_interpolation` block (`:875-1160`), the
        `animation.set_duration` block (`:320-460`), `classify_timeline_key_error()`,
        `timeline_key_kind_name()`, `transform_channel_name()`, and MAR-171's
        `timeline.set_curve_mode` block.
- [ ] **Record the answer to each row of design §4.4** and note any fallback the
      table names. In particular: do `TransformKeyframeEdit` and
      `SlotColorKeyframeEdit` carry `curve_mode` / `curve_driver`? Does
      `set_keyframe_interpolation()` demote? What is `resolve_automatic_curves()`'s
      exact signature?
- [ ] **Measure the registry count. Do not assume it.**
      ```bash
      python3 - <<'EOF'
      import re
      src = open('src/editor/agent_dispatch.cpp').read()
      body = re.search(r'kOperationSpecs\[\]\s*=\s*\{(.*?)\n\};', src, re.S).group(1)
      rows = re.findall(r'\n\s*\{\s*"([^"]+)"', body)
      print(len(rows)); print('\n'.join(f'{i} {n}' for i, n in enumerate(rows)))
      EOF
      ```
      Record the count `N` and the index of `timeline.set_curve_mode`. MAR-172's
      row goes at that index + 1 and the new total is `N + 1`. If `N != 58`, every
      "59" in this plan is `N + 1` instead — **write the corrected numbers into
      this plan's checklist before Task 8** and report the delta.
- [ ] Run the count sweep and classify every hit as *current* (becomes `N + 1`)
      or *historical* (unchanged), per design §14:
      ```bash
      rg -n "\b$N\b" --glob '!build*' src tools AGENTS.md docs .agents
      rg -n "\b$((N+1))\b" --glob '!build*' src tools AGENTS.md docs .agents
      ```
      The second sweep must find no registry claim before Task 8. Note that
      `src/editor/shell_smoke_graph.cpp` had **four** count guards at `0a917db`
      (lines 147, 635, 1557, 2127) and MAR-171 likely added more — enumerate them
      rather than trusting any list.
- [ ] Confirm the fixture facts the spec's worked examples depend on:
      ```bash
      python3 -c "import json;d=json.load(open('assets/fixtures/player_idle.mskl'));print({k:v.get('duration') for k,v in d['animations'].items()})"
      python3 -c "import json;d=json.load(open('assets/fixtures/player_idle.marrow'));print(list(d.keys()))"
      ```
      Expected: `idle` has **no** duration, `aim` has `0.5`, `attack` has none;
      the `.marrow` root has no `animation_edits` and no `loop_sync`. If `idle`
      has gained an explicit duration, the §18.3 "missing prerequisites" case must
      pick a different animation — record which.
- [ ] Confirm the baseline is green before changing anything:
      ```bash
      cmake -S . -B build && cmake --build build -j10
      ./build/marrow_timeline_model_tests
      ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
      ./build/marrow_agent_dispatch_smoke
      ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
      ctest --test-dir build --output-on-failure
      ```
- [ ] Record the **baseline export byte sizes**, which Task 7 must beat:
      ```bash
      ./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
        --export-runtime /tmp/mar172_baseline.mskl --export-binary /tmp/mar172_baseline.mbin
      ls -l /tmp/mar172_baseline.mskl /tmp/mar172_baseline.mbin
      ```
      Expected around `14336` bytes JSON and `3984` bytes MBIN. Write the exact
      numbers down; Task 7's assertions are relative to them.
- [ ] `git status --short` must show only the two MAR-172 documents (plus
      MAR-171's, if still untracked). Report the reconciliation before Task 1.

**Do not proceed until every row of design §4.4 has a recorded answer and the
registry count is measured.**

---

### Task 1: The lane flag and its `.marrow` storage

**Depends on:** Task 0.

Adds the in-memory flag and its optional top-level projection. No behaviour, no
boundary key, no synchronization — this task ends with a flag that round-trips
and validates and does nothing else.

#### TDD step (RED)

- [ ] In `src/samples/editor_project_smoke.cpp`, add
      `bool validate_mar172_loop_boundary_sync(const marrow::editor::ProjectLoadResult&)`
      beside MAR-171's validator, and call it from `main` immediately after
      MAR-171's call (near `:4860`). Its first cases, all storage-only:
      - **default-off byte identity**: `serialize_project()` of the loaded fixture
        contains no `loop_sync`; save to `/tmp/marrow_mar172_roundtrip.marrow`,
        reload, serialize again, and assert the two strings are byte-identical;
      - **round trip**: copy the project, set
        `find_transform_timeline_edit("idle","spine",Rotate)->loop_sync = true`
        **and** move its last key to time 0 first is *not* needed here — the
        fixture's `spine` rotate lane already has a key at time 0, which is the
        only structural rule the save validator enforces. Save, reload, and assert
        the flag survived on exactly that lane and on no other;
      - **text shape**: the saved document's `loop_sync` object is exactly
        `{"animations":{"idle":{"bones":{"spine":{"rotate":true}}}}}` — parse it
        with `python3 -m json.tool`-equivalent C++ JSON access, assert no `false`
        leaf exists anywhere, and assert `arm_l` appears nowhere in the block;
      - **save validation**: an in-memory `ProjectData` with an opted-in
        transform lane whose first keyframe time is `0.25` fails
        `save_project()` with `loop synchronized timelines require a key at time
        zero`; one with an opted-in lane whose `keyframes` is empty fails with
        `loop synchronized timelines require at least one keyframe`;
      - **load validation**, each built as a JSON document string and passed to
        `load_project(const Document&)`, each asserting the exact message and the
        exact JSON path: `loop_sync` not an object; `animations` missing;
        `animations` not an object; a leaf that is a string; an unknown transform
        channel `"spinx"`; `slots.body.attachment`; `true` for a lane with no
        `timeline_edits` entry; `true` for a lane whose first key is at `0.25`;
      - **`false` round-trips as absence**: a document with
        `"rotate": false` loads with `loop_sync == false` and re-serializes with
        no `loop_sync` member at all;
      - **root preservation**: a document carrying both `loop_sync` and an unknown
        top-level member `"mar172_probe": 1` round-trips both.
- [ ] Build and run
      `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`.
      **Expected RED:** compilation fails on the missing `loop_sync` member. Fix
      only the member (next step) and re-run to see the remaining assertions
      fail. Record both failures.

#### Implementation step (GREEN)

- [ ] `include/marrow/editor/project.hpp`: add
      `bool loop_sync{false};` as the last member of `TransformTimelineEdit`,
      `SlotColorTimelineEdit`, and `MeshDeformTimelineEdit`, each with the Doxygen
      comment from design §12.1. Add nothing to the three discrete lane structs.
- [ ] `src/editor/project.cpp`, in the anonymous namespace beside
      `parse_snap_settings()`: add
      ```cpp
      std::optional<LoadError> parse_loop_sync(
          const Document& document,
          const Value& root,
          std::vector<TransformTimelineEdit>* transform_edits,
          std::vector<SlotColorTimelineEdit>* slot_color_edits,
          std::vector<MeshDeformTimelineEdit>* deform_edits);
      ```
      It returns `std::nullopt` immediately when `loop_sync` is absent. It walks
      `animations.<name>.{bones.<bone>.<channel>, slots.<slot>.color,
      deform.<slot>.<attachment>}`, rejects every shape violation of design §8.4
      with `validation_error(document, value.location(), path, message)`, and for
      each `true` leaf finds the matching lane in the three vectors. A `true` leaf
      with no matching lane is `loop_sync requires a timeline edit for that lane`;
      a matching lane whose `keyframes.front().time` is not within
      `timeline_model::kKeyTimeEpsilon` of zero is
      `loop synchronized timelines require a key at time zero`. A `false` leaf is
      accepted and sets nothing.
- [ ] Call `parse_loop_sync()` in `load_project`'s parse sequence **after** the
      transform, slot-colour, and deform timeline parsers — locate them near
      `project.cpp:6447` where `parse_animation_edits` is called and place the new
      call after every `timeline_edits` parser has populated its vector. Getting
      this order wrong makes every cross-reference fail; assert it by running the
      "true for a lane with no timeline edit" case, which passes only in the
      correct order.
- [ ] Add `Value build_loop_sync_value(...)` beside `build_timeline_edits_value()`
      (`:3777`), taking the three lane vectors and returning
      `{"animations": {...}}` built only from lanes whose `loop_sync` is true.
      Emit no empty animation, category, bone, or slot object.
- [ ] In `build_project_value()`, immediately after the `timeline_edits` block
      (`:4240`), add:
      ```cpp
      const bool any_loop_sync =
          std::any_of(project.transform_timeline_edits.begin(), ... ) || ...;
      if (any_loop_sync) {
          root["loop_sync"] = build_loop_sync_value(
              project.transform_timeline_edits,
              project.slot_color_timeline_edits,
              project.mesh_deform_timeline_edits);
      } else {
          root.erase("loop_sync");
      }
      ```
      The `erase` branch is what gives the byte-identity guarantee; without it a
      project that opts out still carries an empty object.
- [ ] In `validate_project_for_save()` (`:4757`), beside the existing
      `snap_settings` block, add the two rules from design §8.4 over the three
      continuous lane vectors.
- [ ] Re-run the project smoke. **Expected GREEN** on every Task 1 case.

#### Verification

```bash
cmake --build build -j10
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke --create /tmp/mar172_created.marrow
python3 -m json.tool assets/fixtures/player_idle.marrow > /dev/null
ctest --test-dir build --output-on-failure -L editor
git diff --stat -- src/runtime include/marrow/runtime include/marrow/c_api src/c_api
```

The last command must print nothing. `git diff --stat` must show only
`include/marrow/editor/project.hpp`, `src/editor/project.cpp`, and
`src/samples/editor_project_smoke.cpp`.

---

### Task 2: The boundary contract primitives

**Depends on:** Task 1.

Adds `synchronize_loop_boundaries()` and `set_timeline_loop_sync()` as pure
`ProjectData` primitives. Nothing calls them yet.

#### TDD step (RED)

- [ ] In `src/tests/timeline_model_tests.cpp`, add a MAR-172 section building
      minimal `ProjectData` values and a minimal effective `SkeletonData` (reuse
      whatever helper the existing authoring-boundary cases use; if none exists,
      build a `runtime::SkeletonData` with one bone, one slot, and one animation
      carrying an `explicit_duration`). Assert every case in design §18.1:
      - idempotence: two consecutive `synchronize_loop_boundaries()` calls, the
        second reporting `synchronized_lane_count == 0`, `created_key_count == 0`,
        and a byte-identical `serialize_project()`;
      - **default off does not resolve**: a project with an auto key whose stored
        curve is deliberately not what MAR-171's resolver would produce, and no
        opted-in lane — the call returns `lane_count == 0`, `changed == false`,
        and the stale curve is **still stale**. (If MAR-171 did not land with
        `curve_mode`, assert instead that the call returns immediately with
        `lane_count == 0` and a byte-identical project.);
      - create, adopt, move, rewrite, each asserting the reported
        `TimelineLoopBoundaryAction` and the exact resulting key;
      - the mirror is bit-exact on `AnimationScalar`, not within an epsilon;
      - every rejection of design §6.5, each leaving the project byte-identical;
      - single-key lane accepted; zero-duration animation rejected;
      - discrete-family selectors rejected; duplicate, unresolvable, and empty
        selector lists rejected;
      - **disable never validates**: a lane in each rejected state is disabled
        successfully, the flag clears, and the boundary key is untouched.
- [ ] Build and run `./build/marrow_timeline_model_tests`. **Expected RED:**
      compilation fails on the two missing functions. Record it.

#### Implementation step (GREEN)

- [ ] `include/marrow/editor/authoring.hpp`: append `TimelineLaneKind`,
      `TimelineLaneSelector`, `TimelineLoopBoundaryAction`,
      `TimelineLoopSyncResult`, `set_timeline_loop_sync()`,
      `synchronize_loop_boundaries()`, `timeline_lane_kind_token()`, and
      `timeline_lane_kind_from_token()`, with the Doxygen comments from design
      §12.2. Append after MAR-171's declarations; move nothing.
- [ ] `src/editor/authoring.cpp`, in the anonymous namespace: add
      - `resolve_timeline_lane()` — the lane analogue of `resolve_timeline_key()`,
        returning `(kind, timeline_index)` or an error naming the lane;
      - `animation_explicit_duration(project, skeleton, animation_name)` —
        returns the pending `AnimationEditKind::SetDuration` edit's `duration`
        when one exists (reuse `coalescible_duration_edit()`), else
        `animation->explicit_duration`, else `std::nullopt`;
      - `loop_boundary_time(duration)` —
        `static_cast<double>(static_cast<runtime::AnimationScalar>(duration))`;
      - `copy_boundary_value(lane, from_index, to_index)` per family — `angle` or
        `(x, y)` by channel, the four colour channels, or the whole
        `vertex_offsets` vector;
      - `copy_boundary_easing(lane, from_index, to_index)` — writes
        `interpolation` and, when the as-built structs carry them, `curve_mode`
        and `curve_driver`. **This helper must not call
        `set_keyframe_interpolation()`.**
- [ ] Implement `synchronize_loop_boundaries()` exactly as design §9.2's
      ten-step sketch, including step 0's immediate return when no in-scope lane
      is opted in and step 3's `resolve_automatic_curves(&candidate, {})`.
      Preflight-then-mutate with a single `ProjectData candidate = *project;` and
      a single `*project = std::move(candidate);`.
- [ ] Implement `set_timeline_loop_sync()` exactly as design §11's ten-step
      sketch, calling `synchronize_loop_boundaries()` at step 7 with the union of
      the animations the selectors name.
- [ ] Re-run the focused tests. **Expected GREEN.**

#### Verification

```bash
cmake --build build -j10
./build/marrow_timeline_model_tests
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
ctest --test-dir build --output-on-failure -L editor
git diff --stat -- src/runtime include/marrow/runtime include/marrow/c_api src/c_api
```

Then confirm the resolver rule mechanically:

```bash
rg -n 'set_keyframe_interpolation' src/editor/authoring.cpp
```

Every hit must be inside `set_keyframe_interpolation()`'s own definition. If one
appears inside a MAR-172 helper, the demotion bug is present — fix it before
proceeding.

---

### Task 3: Duration — the boundary-excluding inferred floor

**Depends on:** Task 2.

Makes an opted-in clip shortenable, and proves the change is invisible to every
project that does not opt in.

#### TDD step (RED)

- [ ] In `src/tests/timeline_model_tests.cpp`, add:
      - `inferred_duration_excluding_loop_boundaries()` returns
        `animation.inferred_duration()` **bit for bit** when no lane of the
        animation is opted in, over at least three shaped animations (one with a
        runtime-only lane, one with a project overlay, one with both);
      - with one opted-in lane it drops to that lane's second-to-last key time
        when that lane held the maximum, and is unchanged when another lane held
        it;
      - a single-key opted-in lane contributes `0.0`.
- [ ] In `src/samples/editor_project_smoke.cpp`'s MAR-172 validator, add:
      - **grow**: `set_animation_duration("idle", 1.5)`, enable
        `idle`/`spine`/`rotate`, then `set_animation_duration("idle", 2.0)`
        followed by `synchronize_loop_boundaries()` — the boundary key's time is
        exactly `float32(2.0)` and no other key moved;
      - **shrink**: `set_animation_duration("idle", 1.2)` **succeeds** (it would
        fail before this task), then the sync moves the boundary to
        `float32(1.2)`;
      - **shrink onto the spacing floor**: `1.0005` — `set_animation_duration()`
        accepts (floor is 1.0) and the **sync rejects** with the project
        byte-identical to before the whole sequence;
      - **shrink below a real key**: `0.9` — rejected by
        `set_animation_duration()` with its existing
        `cannot be shorter than the last authored key` message;
      - **no-opt-in regression**: re-run the existing MAR-155 duration
        accept/reject cases on a project with `loop_sync` absent and assert
        identical results **and identical message strings**;
      - **auto-extend does not undo a shrink**: after the 1.2 shrink and the sync,
        `auto_extend_explicit_animation_durations()` reports `changed == false`.
- [ ] Build and run both binaries. **Expected RED:** the shrink cases fail with
      `Animation duration cannot be shorter than the last authored key
      (1.500000 seconds).` — the exact symptom design §9.3 predicts. Record the
      message.

#### Implementation step (GREEN)

- [ ] `include/marrow/editor/authoring.hpp`: declare
      `inferred_duration_excluding_loop_boundaries()` with design §9.3's Doxygen
      comment, and add one sentence to `set_animation_duration()`'s comment
      recording that a managed loop boundary does not constrain the duration it
      follows.
- [ ] `src/editor/authoring.cpp`: implement
      `inferred_duration_excluding_loop_boundaries()`:
      ```text
      if no lane of `animation` in `project` has loop_sync:
          return animation.inferred_duration();          // bit-exact fast path
      floor = 0
      for each project lane of this animation:
          floor = max(floor, lane.loop_sync && size >= 2
                             ? keyframes[size - 2].time
                             : keyframes.back().time)
      for each effective timeline of `animation` that no project lane of this
      animation owns:
          floor = max(floor, last keyframe time)
      return floor
      ```
      Ownership is resolved by mapping the effective timeline's `bone_index` /
      `slot_index` through `effective_skeleton.bones()` / `slots()` to a name and
      comparing against the project lane's `bone_name` + `channel` /
      `slot_name` / `slot_name` + `attachment_name`. Cover the four transform
      timeline vectors, `slot_color_timelines`, and `mesh_deform_timelines`; the
      discrete timelines can never be owned by an opted-in lane, so they always
      contribute their last key.
- [ ] In `set_animation_duration()`, replace **both** uses of
      `animation->inferred_duration()` (`:1579` and `:1603`) with the new helper.
      Leave the project-overlay `include_animation_timeline_maximum()` calls in
      place; they are still needed for the non-opted-in lanes and the helper's
      fast path keeps the combined result identical when nothing is opted in.
- [ ] Add a MAR-172 overload of `include_animation_timeline_maximum` in
      `timeline_model.hpp` (or a file-local one in `authoring.cpp`, whichever
      Task 0 found the existing helper to be) that skips `keyframes.back()` when
      `edit.loop_sync && edit.keyframes.size() >= 2`, and use it for the three
      continuous families in `auto_extend_explicit_animation_durations()`
      (`:1654-1665`). The three discrete families keep the existing helper.
- [ ] Re-run both binaries. **Expected GREEN.**

#### Verification

```bash
cmake --build build -j10
./build/marrow_timeline_model_tests
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
ctest --test-dir build --output-on-failure
git diff --stat -- src/runtime include/marrow/runtime include/marrow/c_api src/c_api
```

The full `ctest` run matters here more than anywhere else: duration validation is
exercised by `shell_smoke_timeline.cpp`'s clip-duration scenarios and by the
existing MAR-155 project cases. A regression there means the fast path is wrong.

---

### Task 4: Retime pinning

**Depends on:** Task 2.

#### TDD step (RED)

- [ ] In `src/tests/timeline_model_tests.cpp`, add design §18.1's retime cases:
      the first key of an opted-in lane returns `changed == false` and
      `applied_delta == 0`; the last key likewise; a selection of both plus a
      middle key likewise; a selection of only middle keys still moves by the
      full requested delta; and a selection on a **not** opted-in lane behaves
      exactly as before (compare against a recorded expected `applied_delta`).
- [ ] Build and run `./build/marrow_timeline_model_tests`. **Expected RED:** the
      pinned cases report a non-zero `applied_delta`.

#### Implementation step (GREEN)

- [ ] In `src/editor/authoring.cpp`'s `include_resolved_retime_bounds()`
      (`:443`), for the three continuous families only, add the pinning block from
      design §10.2 after the existing `include_timeline_retime_bounds()` call.
      The rule is expressible with `ProjectData` alone — first key is index 0,
      boundary key is index `size - 1` — so **`retime_keyframes()`'s signature
      does not change**.
- [ ] Re-run the focused tests. **Expected GREEN.**

#### Verification

```bash
cmake --build build -j10
./build/marrow_timeline_model_tests
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
ctest --test-dir build --output-on-failure -L editor
```

Then confirm the signature is untouched:

```bash
git diff -- include/marrow/editor/authoring.hpp | rg -n 'retime_keyframes'
```

Only the doc comment may appear. If the parameter list changed, revert it —
MAR-173 depends on this signature.

---

### Task 5: The session seam

**Depends on:** Tasks 2 and 3.

Two calls, in the two places design §9.1 names. This is where MAR-172 becomes
automatic.

#### TDD step (RED)

- [ ] Add `bool validate_timeline_loop_sync_shell_smoke(const std::filesystem::path&)`
      to `src/editor/shell_smoke_scenarios.hpp` and call it from
      `src/editor/shell_smoke.cpp` immediately after MAR-171's scenario (near
      `:110`). Implement it in `src/editor/shell_smoke_graph.cpp` with its own
      isolated session, and write **only the auto-extend hole case first**:
      - author an explicit duration of 1.5 on `idle`;
      - enable `idle`/`spine`/`rotate` and assert the boundary key at 1.5;
      - in one transaction, add a key at 2.0 s on the **not** opted-in
        `root`/`translate` lane;
      - assert **one** history entry, that `idle`'s duration grew to 2.0 through
        the session's `auto_extend_explicit_animation_durations()`, **and** that
        the opted-in lane's boundary key moved to 2.0 in the same entry;
      - assert one `undo` restores the duration, the added key, and the boundary
        key together.
- [ ] Build and run
      `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`.
      **Expected RED:** the boundary key is still at 1.5 while the duration is
      2.0 — the exact staleness design §9.1 uses to justify the seam. Record the
      failure output; it is the evidence that a controller-level wiring would not
      have been sufficient.

#### Implementation step (GREEN)

- [ ] `src/editor/session.cpp`, in `Impl::refresh_runtime()`, immediately after
      the `auto_extend_explicit_animation_durations()` block (`:1317-1325`) and
      before `build_project_runtime()`:
      ```cpp
      const TimelineLoopSyncResult loop_sync_result =
          synchronize_loop_boundaries(load.project.get(), *load.skeleton_data);
      if (!loop_sync_result) {
          return SessionResult{
              false,
              make_error(SessionErrorCode::InvalidTransaction, loop_sync_result.error)};
      }
      ```
      Match the surrounding error shape exactly.
- [ ] `src/editor/session.cpp`, in `Impl::commit()`, immediately after the
      `auto_extend_explicit_animation_durations()` block (`:1413-1425`), with the
      same call and:
      - on error, `restore_active_transaction()` then the error result — mirroring
        the auto-extend failure path exactly;
      - on `loop_sync_result.changed`, set `transaction.runtime_is_current = false;`
        and `active_transaction->runtime_is_current = false;`, mirroring the
        auto-extend `changed` handling exactly.
- [ ] **Do not touch `rebuild_runtime()` (`:2423`).** Design §9.1 records why,
      and Task 10's checklist re-verifies it.
- [ ] Re-run the shell smoke. **Expected GREEN.**

#### Verification

```bash
cmake --build build -j10
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_timeline_model_tests
ctest --test-dir build --output-on-failure
git diff -- src/editor/session.cpp | rg -n 'rebuild_runtime'
```

The last command must print nothing. Then confirm the default-off behavioural
guarantee: with no lane opted in, the fixture shell smoke's history counts,
`project_revision()` values, and `serialize_project()` output must be unchanged
from the Task 0 baseline. If any existing scenario's numbers moved, step 0 of
`synchronize_loop_boundaries()` is not returning early — fix that, not the test.

---

### Task 6: Shell managed-identity guards

**Depends on:** Tasks 2 and 5.

#### TDD step (RED)

- [ ] Extend `validate_timeline_loop_sync_shell_smoke()` with design §18.5's
      remaining cases:
      - criterion 3, five ways, each one history entry with the boundary updated:
        a MAR-168 value drag on key 0; a MAR-169 handle drag on key 0; a MAR-170
        preset on key 0; a dopesheet retime of a **middle** key; and a duration
        edit;
      - retime pinning through the gesture: a retime whose selection includes the
        boundary key produces `applied_delta == 0`, no history entry, and an
        unchanged project;
      - selection skip: a preset applied to a selection containing the boundary
        key writes the other keys, reports the skip in the status message, and
        leaves the boundary key's easing equal to key 0's;
      - removal guard: removing only the boundary key changes nothing and sets a
        status message; removing only the time-zero key fails the transaction with
        `serialize_project()` byte-identical;
      - paste: past the boundary fails the transaction with the project
        byte-identical; inside the clip succeeds in one history entry with the
        boundary re-mirrored;
      - clipboard hygiene: after `copy_selected_timeline_keys()` on an opted-in
        lane, every lane in `state.timeline_editor.clipboard.project_fragment` has
        `loop_sync == false`;
      - undo/redo of the enable restores the flag, the boundary key,
        `selected_keys`, and `active_key`;
      - cancel during a value drag on key 0 restores `serialize_project()`,
        `undo_count()`, `redo_count()`, `project_revision()`, `dirty()`, the
        rebuilt dopesheet `key_times`, and the boundary key;
      - materialization: the first enable on the runtime-only `slot:0:Color` lane
        copies all its keys into the project rather than replacing the lane.
- [ ] Build and run the shell smoke. **Expected RED** on the skip, removal, and
      clipboard cases. Record which.

#### Implementation step (GREEN)

- [ ] `src/editor/timeline_controller.hpp`: declare
      `timeline_key_is_managed_loop_boundary()` with design §12.3's comment.
- [ ] `src/editor/timeline_controller.cpp`: implement it by resolving the track to
      its project lane (reuse `timeline_key_selector()`'s resolution shape) and
      returning `lane->loop_sync && key_index + 1 == lane->keyframes.size()`.
- [ ] Use it in `collect_curve_preset_selectors()` (`:1194`) beside the existing
      `timeline_key_kind_carries_easing()` filter, with a comment naming the
      reason, and in whatever collectors MAR-168's value drag, MAR-169's handle
      drag, and MAR-171's curve-mode row use (Task 0 enumerated them). Count the
      skips into the existing `skipped_key_count` and extend the status message.
- [ ] Use it in `remove_selected_timeline_keys()` (`:1396`) to filter `removals`
      before the transaction opens, with a status message when everything was
      filtered.
- [ ] In `copy_selected_timeline_keys()` (`:1494`), immediately after the
      per-track loop and before `clipboard.has_data` is computed, clear the flag
      on the fragment:
      ```cpp
      // The flag is a property of a lane in a project, never of a clipboard
      // fragment: a pasted lane's own opt-in decides, and a fragment that
      // carried one would assert a contract the destination may not satisfy.
      for (auto& edit : clipboard.project_fragment.transform_timeline_edits) edit.loop_sync = false;
      for (auto& edit : clipboard.project_fragment.slot_color_timeline_edits) edit.loop_sync = false;
      for (auto& edit : clipboard.project_fragment.mesh_deform_timeline_edits) edit.loop_sync = false;
      ```
- [ ] Re-run the shell smoke. **Expected GREEN.**

#### Verification

```bash
cmake --build build -j10
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
ctest --test-dir build --output-on-failure
git status --short
```

`git status --short` must show no ImGui translation unit. Confirm explicitly:

```bash
git diff --stat -- src/editor/shell_timeline.cpp src/editor/shell_timeline_graph.cpp \
  src/editor/shell_viewport.cpp src/editor/shell_viewport_ui.cpp \
  src/editor/shell_inspector.cpp src/editor/shell_widgets.cpp \
  src/editor/shell_project_panels.cpp src/editor/shell_state.hpp src/editor/shell_core.cpp
```

This must print nothing.

---

### Task 7: Export, proved on a mutated project

**Depends on:** Tasks 1–6.

This is the criterion two earlier milestones faked. Read design §18.6 before
writing a line.

#### TDD step (RED)

- [ ] In `validate_mar172_loop_boundary_sync()`, add the behaviour cases from
      design §18.3 that the earlier tasks have not already covered — adoption and
      materialization on `aim`/`arm_l`/`rotate`, Slot Color and Deform boundary
      creation, the two missing-prerequisite rejections on the real fixture,
      first-key value and curve propagation, disable, and the stale-pair
      load case.
- [ ] Then add the export block, on the project the validator **just mutated**:
      1. explicit duration 1.5 on `idle`; `idle`/`spine`/`rotate` and
         `idle`/`body`/`color` opted in; boundary keys created;
      2. `export_runtime_assets()` on **that** project into
         `/tmp/marrow_mar172_loop.mskl` and `/tmp/marrow_mar172_loop.mbin`;
      3. `runtime::load_skeleton_data()` on the exported `.mskl`, asserting:
         `idle`'s `duration()` is `1.5`; the `spine` rotate timeline has **four**
         keyframes; keyframe 3 has `time == 1.5f` and `angle` bit-equal to
         keyframe 0's; keyframe 3's interpolation is `CubicBezier` with all four
         control points bit-equal to keyframe 0's; the `body` color timeline has
         four keyframes and its last colour is bit-equal to its first;
      4. read the exported `.mskl` as **text** and assert it contains no
         `loop_sync`;
      5. `validate_binary_export(export.path, *export.binary_path)`;
      6. print `MAR-172 loop boundary export: JSON <n> bytes, MBIN <m> bytes.`
         and, separately, the untouched baseline's sizes;
      7. assert **in code** that the mutated JSON size is strictly greater than
         the baseline JSON size and the mutated MBIN size is strictly greater
         than the baseline MBIN size, with a failure message naming the
         MAR-168/169 defect so a future reviewer sees why the assertion exists.
- [ ] Build and run
      `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`.
      **Expected RED** on at least the four-keyframe assertion, before any byte
      count is consulted. Record it.

#### Implementation step (GREEN)

- [ ] There should be **no production change** in this task. If step 3 fails, the
      bug is in Task 2's boundary creation or Task 5's seam, not in the export
      path — fix it there. If you find yourself editing
      `build_runtime_document()` or a `build_runtime_*_value()` function, stop:
      the design says export needs zero changes and a change there means the
      boundary key is not reaching the project lane.
- [ ] Re-run. **Expected GREEN**, with both byte counts strictly above the Task 0
      baseline (`~14336` JSON, `~3984` MBIN).

#### Verification

```bash
cmake --build build -j10
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
  --export-runtime /tmp/mar172_export.mskl --export-binary /tmp/mar172_export.mbin
./build/marrow_inspect --compare /tmp/mar172_export.mbin /tmp/mar172_export.mskl
./build/marrow_fixture_smoke /tmp/mar172_export.mskl assets/fixtures/player_idle.matl
rg -c 'loop_sync' /tmp/marrow_mar172_loop.mskl || echo "OK: no loop_sync in the export"
git diff -- src/editor/project.cpp | rg -n 'build_runtime'
```

The last command must print nothing.

---

### Task 8: The Agent operation

**Depends on:** Tasks 1–7. **The registry becomes `N + 1` here and not before.**

#### TDD step (RED)

- [ ] In `src/samples/agent_dispatch_smoke.cpp`, change
      `std::array<OperationExpectation, N>` to `N + 1` and insert
      `{"timeline.set_loop_sync", "edit", true, false, true}` immediately after
      the `timeline.set_curve_mode` row.
- [ ] Add design §18.7's behaviour cases: the dry run, the live call, the
      `no_change` repeat, the `aim` adoption, the `set_transform`-propagates case,
      the `animation.set_duration` move-and-undo case, the undo restore, every
      rejection, and the "disable succeeds in a rejected state" case.
- [ ] Update every count guard Task 0 enumerated — `shell_smoke_graph.cpp`'s
      guards and `tools/mcp/test_client.py:46,48` — to `N + 1`, and add
      `timeline.set_loop_sync` to `test_client.py`'s `new_edit_operations` set.
- [ ] Build and run `./build/marrow_agent_dispatch_smoke`. **Expected RED:** the
      registry is still `N` and `expect_complete_coverage()` reports the new row
      as uninvoked.

#### Implementation step (GREEN)

- [ ] `src/editor/agent_dispatch.cpp`: add the `kOperationSpecs` row exactly
      between `timeline.set_curve_mode` and `set_transform`, with
      `category = "edit"`, `mutating = true`, `requires_review = false`,
      `dry_run_supported = true`, `requires_project = true`, and
      `&handle_editing_operation`.
- [ ] `src/editor/agent_dispatch_internal.hpp`: declare
      `timeline_lane_selectors_arg()` with design §13.2's comment.
- [ ] `src/editor/agent_handlers_editing.cpp`: implement
      `timeline_lane_selectors_arg()` and the `timeline.set_loop_sync` block,
      modelled on the `timeline.set_interpolation` block at `:875-1160` and
      MAR-171's `timeline.set_curve_mode` block:
      - validation in design §13.3's exact order, with `classify_timeline_key_error()`
        for the resolution failure;
      - materialization through `ensure_transform_timeline_edit()` /
        `ensure_slot_color_timeline_edit()` / `ensure_mesh_deform_timeline_edit()`
        on both paths;
      - the response payload of design §13.4, capped at 256 lanes with
        `lanes_truncated`, with Deform boundaries reporting `vertex_count` rather
        than the offset array;
      - dry run: candidate copy, snapshot, apply, snapshot, report, discard;
      - live: `begin_edit({EditKind::EditProperty, "Enable/Disable loop
        synchronization via Agent", "timeline:loop-sync", false,
        Project | Runtime | Preview})`, then `commit_or_error()` with
        `CommitPolicy{"Failed to set loop synchronization: "}`.
- [ ] Re-run. **Expected GREEN**, with the registry reporting `N + 1`.

#### Verification

```bash
cmake --build build -j10
./build/marrow_agent_dispatch_smoke
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
ctest --test-dir build --output-on-failure
rg -n "\b$N\b" --glob '!build*' src tools AGENTS.md docs .agents
```

Every remaining hit of the old count must be a *historical* sentence (dated
validation results, milestone paragraphs, past PRD criteria) or one of design
§14's known false positives. Any *current* claim still at the old number is a
missed site.

---

### Task 9: The MCP tool

**Depends on:** Task 8.

#### TDD step (RED)

- [ ] In `tools/mcp/test_client.py`, add design §18.8's sequence: the metadata row
      assertion, the duration → dry run → live enable → read-back → undo →
      read-back sequence asserting `boundary_time == 1.5` and the mirrored angle
      to four decimal places, the `set_transform`-between-dry-runs case, and the
      four rejection cases.
- [ ] Start the shell and run the client:
      ```bash
      ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
      tools/mcp/venv/bin/python tools/mcp/test_client.py
      ```
      **Expected RED:** `len(mcp_names)` is `N`, not `N + 1`, because the tool
      does not exist yet.

#### Implementation step (GREEN)

- [ ] `tools/mcp/tools/editing.py`: add `_timeline_loop_sync_lane_schema()` — a
      `oneOf` over `transform`, `slot_color`, and `deform` with **no `time`
      property** — and one `types.Tool` named `timeline.set_loop_sync` placed
      immediately after `timeline.set_curve_mode`, with
      `"required": ["lanes", "enabled"]` and the description from design §13.6.
      Change no existing tool.
- [ ] Re-run. **Expected GREEN.**

#### Verification

```bash
tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py \
  tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
kill %1
```

---

### Task 10: Full validation, documentation, and MAR-172 closure

**Depends on:** Tasks 0–9.

- [ ] Run the **entire** "Full verification checklist" below and paste each
      command's decisive output into the `AGENTS.md` results section. A command
      whose output you did not read is not a passed gate.
- [ ] `AGENTS.md`:
      - add a `MAR-172 Loop Boundary Key Synchronization Validation Results`
        section in the existing table plus command-output format, including the
        **mutated** export byte sizes beside the baseline's;
      - update `Current Validation`'s registry line to `N + 1` operations and
        extend its parenthetical to name loop-boundary authoring;
      - update `Project State` so MAR-172 is complete and MAR-173 is next;
      - leave every dated MAR-169 / MAR-170 / MAR-171 results block unchanged.
- [ ] `docs/root1/format-spec.md`:
      - add `loop_sync` to the `.marrow` top-level key list;
      - add a `### loop_sync` section between `### snap` and `### animation_edits`
        covering the tree shape, the boolean leaves, absent-means-off, the three
        supported families and why the other three are excluded, the load and save
        validation rules, the derived managed-key identity, and the statement that
        the block never enters `.mskl` or `.mbin` and no format version changes;
      - add one sentence to the `animation_edits` `set_duration` bullet list
        recording that a managed loop boundary does not constrain the duration it
        follows.
- [ ] `docs/root1/concepts.md`: add a short paragraph on why a looping clip needs
      a boundary key, referencing `AnimationData::duration()` and the wrap.
- [ ] `docs/root1/discription.md`, `quick-start.md`, `editing-gap-analysis.md`,
      `refector.md`, `platform-validation.md`: update every *current* statement of
      the registry size, the "MAR-172 is next" wording, and the timeline-authoring
      contract. Apply design §14's historical/current rule to every hit and leave
      every dated milestone paragraph alone.
- [ ] `.agents/tasks/prd-marrow-runtime.json`: mark `MAR-172` `done` with the
      verified completion date; leave `MAR-173` open; leave MAR-192 through
      MAR-210 open and add no platform qualification credit.
- [ ] Change the design spec's status line to `Implemented and validated`.
- [ ] Final read-only checkpoint:
      ```bash
      git status --short
      git diff --check
      git diff --stat -- src/runtime include/marrow/runtime include/marrow/c_api src/c_api \
        src/editor/preferences.cpp include/marrow/editor/preferences.hpp
      ```
      The last command must print nothing.

---

## Full verification checklist

Every command must be run and its output read. `N` is the registry count Task 0
measured; `N + 1` is the count after Task 8.

### Build and unit gates

| # | Command | Expected |
| --- | --- | --- |
| 1 | `cmake -S . -B build` | configures clean |
| 2 | `cmake --build build -j10` | no warnings introduced |
| 3 | `cmake --build build --target marrow_verify_third_party` | passes |
| 4 | `./build/marrow_timeline_model_tests` | passes, including every MAR-172 lane case: idempotence, default-off-does-not-resolve, create/adopt/move/rewrite, every §6.5 rejection, disable-never-validates, retime pinning, the excluding floor's bit-exact fast path |
| 5 | `./build/marrow_timeline_graph_model_tests` | passes, unchanged |
| 6 | `./build/marrow_preference_tests` | passes, unchanged (`kEditorSettingsVersion` is still 1) |
| 7 | `./build/marrow_selection_tests` | passes, unchanged |
| 8 | `./build/marrow_viewport_interaction_tests` | passes, unchanged |
| 9 | `./build/marrow_windowing_tests`, `./build/marrow_pen_input_tests`, `./build/marrow_agent_socket_tests` | pass, unchanged |

### Project, storage, and export gates

| # | Command | Expected |
| --- | --- | --- |
| 10 | `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` | passes, including `validate_mar172_loop_boundary_sync()` and the printed `MAR-172 loop boundary export: JSON <n> bytes, MBIN <m> bytes.` line |
| 11 | the same command's export line | `n` **strictly greater** than the Task 0 baseline JSON size and `m` **strictly greater** than the baseline MBIN size. `14336` / `3984` is the MAR-168/169 defect recurring |
| 12 | `./build/marrow_project_smoke --create /tmp/mar172_created.marrow` | passes; the created project has no `loop_sync` member |
| 13 | `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/mar172_export.mskl --export-binary /tmp/mar172_export.mbin` | passes |
| 14 | `./build/marrow_inspect --compare /tmp/mar172_export.mbin /tmp/mar172_export.mskl` | JSON/binary equivalent |
| 15 | `./build/marrow_fixture_smoke /tmp/mar172_export.mskl assets/fixtures/player_idle.matl` | passes |
| 16 | `rg -c 'loop_sync' /tmp/marrow_mar172_loop.mskl` | **no match** — export neutrality for the flag |
| 17 | `python3 -m json.tool assets/fixtures/player_idle.marrow > /dev/null` | valid; the checked-in fixture is **unchanged** by MAR-172 |
| 18 | `./build/marrow_parameter_project_smoke assets/fixtures/parameter_face_basic.marrow` | passes, unchanged |
| 19 | `./build/marrow_atlas_packer_smoke` | passes, unchanged |

### Shell gates

| # | Command | Expected |
| --- | --- | --- |
| 20 | `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` | passes, including `validate_timeline_loop_sync_shell_smoke()`: the auto-extend hole case, criterion 3 five ways each as one history entry, retime pinning, selection skip, removal guard, paste guard, clipboard hygiene, undo/redo, cancel, materialization, and `agent_operation_descriptor_count() == N + 1` before and after every case |
| 21 | `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2` | passes, unchanged |
| 22 | `./build/marrow_editor_shell --verify-launch-focus` | unchanged |

### Agent and MCP gates

| # | Command | Expected |
| --- | --- | --- |
| 23 | `./build/marrow_agent_dispatch_smoke` | passes against the exact `N + 1`-operation registry, with the `timeline.set_loop_sync` expectation row immediately after `timeline.set_curve_mode` and every §18.7 behaviour and rejection case |
| 24 | `tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py` | compiles |
| 25 | `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` then `tools/mcp/venv/bin/python tools/mcp/test_client.py` | `mcp test_client: PASSED` with `N + 1`/`N + 1` exact C++/Python name parity and the §18.8 sequence |
| 26 | `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only` against `parameter_face_basic.marrow` | passes, unchanged |

### Regression gates

| # | Command | Expected |
| --- | --- | --- |
| 27 | `ctest --test-dir build -N` | discovers the same targets as Task 0 |
| 28 | `ctest --test-dir build --output-on-failure` | all pass |
| 29 | `ctest --test-dir build --output-on-failure -L runtime` | all pass; MAR-172 touched no runtime code |
| 30 | `ctest --test-dir build --output-on-failure -L editor` | all pass |
| 31 | `ctest --test-dir build --output-on-failure -R marrow.renderer_link_boundary` | passes |
| 32 | `./build/marrow_c_smoke` | passes; C ABI v1 unchanged |
| 33 | `./build/marrow_spine_import_smoke assets/fixtures/spine_import_sample.json assets/fixtures/spine_import_sample.atlas` | passes, unchanged |
| 34 | `./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl` | passes, unchanged |
| 35 | `./build/marrow_fixture_smoke assets/fixtures/player_idle.mbin assets/fixtures/player_idle.matl` | passes, unchanged |

### Compatibility gates

| # | Command | Expected |
| --- | --- | --- |
| 36 | `git diff --stat -- src/runtime include/marrow/runtime include/marrow/c_api src/c_api` | **empty** |
| 37 | `git diff --stat -- src/editor/preferences.cpp include/marrow/editor/preferences.hpp` | **empty** |
| 38 | `git diff --stat -- src/editor/shell_timeline.cpp src/editor/shell_timeline_graph.cpp src/editor/shell_timeline_graph.hpp src/editor/shell_viewport.cpp src/editor/shell_viewport_ui.cpp src/editor/shell_inspector.cpp src/editor/shell_widgets.cpp src/editor/shell_project_panels.cpp src/editor/shell_state.hpp src/editor/shell_core.cpp` | **empty** — MAR-172 is UI-free |
| 39 | `git diff -- src/editor/session.cpp \| rg -n 'rebuild_runtime'` | **empty** — the sync is wired only at the two transaction seams |
| 40 | `git diff -- src/editor/project.cpp \| rg -n 'build_runtime'` | **empty** — the export path is untouched |
| 41 | `git diff -- include/marrow/editor/authoring.hpp \| rg -n 'retime_keyframes'` | only the doc comment; the signature is unchanged for MAR-173 |
| 42 | `rg -n 'set_keyframe_interpolation' src/editor/authoring.cpp` | every hit inside that function's own definition; no MAR-172 helper calls it |
| 43 | `rg -n "\b$N\b" --glob '!build*' src tools AGENTS.md docs .agents` | every remaining hit is historical or a design §14 known false positive |
| 44 | `git diff --check` and `git status --short` | clean; no unintended file |

### Display gates (unchanged coverage)

| # | Command | Expected |
| --- | --- | --- |
| 45 | `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON` then `cmake --build build-display` | builds |
| 46 | `ctest --test-dir build-display --output-on-failure -L windowing` | passes, unchanged |
| 47 | `ctest --test-dir build-display --output-on-failure -L display` | passes, unchanged |

MAR-172 adds no ImGui code, so gates 45–47 prove only that existing coverage is
undisturbed. They add no manual-visible-UI, Windows 11, or physical-input
qualification credit; MAR-192 through MAR-210 stay open.

---

## Decisions Taken Under Ambiguity

The full list with reasoning is design §17. The nine that most change what an
implementer writes:

1. **Opt-in is a lane boolean projected into an optional top-level `.marrow`
   tree**, not a member of the lane's own JSON value. The lane value is a bare
   array (`project.cpp:1680,3794`); promoting it to an object would make an
   opted-in project fail to load in an older build with `timeline edits must be
   an array`. The top-level tree degrades gracefully through `preserved_root`
   instead. MAR-171's argument against a side table does not apply, because a
   lane key `(animation, bone, channel)` — unlike a key selector's
   `(…, time)` — is invariant under retime, insertion, deletion, and paste.
2. **Managed identity is derived, never stored.** The boundary key *is* the key
   at `float32(duration)`, which is by contract the lane's last key. A stored
   marker would ride a copy/paste into a lane where it is a lie.
3. **The sync lives at the session seam**, after
   `auto_extend_explicit_animation_durations()` in `refresh_runtime()` and
   `commit()`. A controller-level wiring has a real staleness hole: the session
   grows explicit durations *after* any controller code has run. Task 5's RED
   test is that hole, observed.
4. **Two phases with the MAR-171 resolver between them**, and the ordering
   terminates without iteration because a key's stored easing is never an input
   to any tangent computation and the boundary key has no outgoing segment.
5. **Phase 2 writes easing through `write_key_interpolation()`, never
   `set_keyframe_interpolation()`**, which MAR-171 makes demote to `Manual`.
6. **`set_animation_duration()`'s inferred floor excludes managed boundary
   keys**, and the exclusion is a bit-exact no-op when nothing is opted in.
   Without it an opted-in clip could never be shortened.
7. **A violated invariant cancels the whole transaction**, and that is only
   humane because **disabling always succeeds** without evaluating any
   prerequisite. Both halves must be implemented or neither works.
8. **Disable leaves the boundary key in place**, so enable → disable is not
   byte-symmetric. Never delete keyframe data as a side effect of clearing a
   flag.
9. **Deform is included** even though MAR-171 excluded it: MAR-171 needed a
   canonical scalar driver, MAR-172 needs a copyable value, and a vertex-offset
   vector is trivially copyable. The **discrete** families are excluded and the
   exclusion is compile-enforced.
