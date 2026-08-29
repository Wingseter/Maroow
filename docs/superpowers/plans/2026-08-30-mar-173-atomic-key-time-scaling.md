# MAR-173 Atomic Key Time Scaling Implementation Plan

**Date:** 2026-08-30

**Design:** `docs/superpowers/specs/2026-08-30-mar-173-atomic-key-time-scaling-design.md`

**Authority:** `.agents/tasks/prd-marrow-runtime.json` story `MAR-173`

**Depends on:** MAR-172
(`docs/superpowers/plans/2026-08-30-mar-172-loop-boundary-key-sync.md`) being
merged. MAR-172 depends on MAR-171 (`b19b7b5`).

> **Read the design document first, in full.** This plan does not restate its
> reasoning; it sequences the work and names the gates. Every `§n` reference
> below points at that document.

## Global Constraints

- **No behaviour change to any existing operation.** Two extractions are made
  (§8.4's `family_key_spacing()`, §8.5's `resolved_key_is_loop_pinned()`, and
  §12.2's `timeline_key_selectors_arg()`); each is gated by an existing test
  suite passing **unchanged** before any new behaviour is written.
- **No file-format, C ABI, or preference version change.** `.mskl` stays v1,
  `.mbin` stays v2, `.marrow` gains no field, `kEditorSettingsVersion` stays `1`,
  and `include/marrow/c_api/**`, `src/c_api/**`,
  `include/marrow/editor/preferences.hpp`, and `src/editor/preferences.cpp`
  change by **zero bytes**. Verify with `git diff --stat` at the end.
- **Preference isolation.** Every new smoke scenario installs its own
  `ScopedPreferenceIsolation`, and
  `$HOME/Library/Application Support/Marrow` must not exist before or after any
  gate. Check it explicitly in Task 10.
- **TDD is mandatory.** Every task's RED step must be observed failing for the
  stated reason before the GREEN step is written. "It compiled" is not RED.
- **One history entry per gesture and per Agent call.** Every scenario that
  commits asserts `undo_count()` moved by exactly one.
- **Every rejection test asserts `serialize_project()` is byte-identical**, not
  merely that the call returned an error.
- **The registry count is measured, never assumed.** Task 0 measures it; every
  later "60" in this plan is `measured + 2` (MAR-172's row plus MAR-173's) and
  must be corrected in writing before Task 6 if the measurement disagrees.

## File and Responsibility Map

| File | Change |
| --- | --- |
| `src/editor/timeline_model.hpp` / `.cpp` | `SelectionTimeSpan`, `selection_time_span()`, `scale_from_edge_time()`, `snap_scale_to_frames()`, `incremental_scale_ratio()` |
| `include/marrow/editor/authoring.hpp` | additive: `TimelineScalePivot`, `TimelineScaleResult`, `scale_keyframe_times()` |
| `src/editor/authoring.cpp` | `family_key_spacing()`, `resolved_key_is_loop_pinned()` (both extracted), `scale_keyframe_times()` |
| `src/editor/shell_state.hpp` | `TimelineScaleDragCandidate`, `TimelineScaleGesture`, two `TimelineEditorState` members, one line in `authoring_gesture_active()` |
| `src/editor/shell_core.cpp` | two lines in `cancel_authoring_gestures()` |
| `src/editor/timeline_controller.hpp` / `.cpp` | `begin_timeline_scale_gesture()`, `apply_timeline_scale_ratio()`, `finish_timeline_scale_gesture()`, `timeline_scale_rejection()` |
| `src/editor/shell_timeline.hpp` / `.cpp` | `begin_timeline_scale_drag()`, `update_timeline_scale_drag()`, `cancel_timeline_scale_drag()`, `draw_timeline_selection_range_bar()`, ruler zoom/pan suppression |
| `src/editor/agent_dispatch_internal.hpp` | `timeline_key_selectors_arg()` declaration |
| `src/editor/agent_dispatch.cpp` | one `kOperationSpecs[]` row |
| `src/editor/agent_handlers_editing.cpp` | `timeline_key_selectors_arg()` definition, retime rewired to it, `timeline.scale_key_times` block |
| `tools/mcp/tools/editing.py` | one `types.Tool` |
| `src/editor/timeline_model_tests.cpp` (or its as-built name) | ratio-math cases |
| `src/samples/editor_project_smoke.cpp` (or wherever `validate_mar172_*` lives) | `validate_mar173_key_time_scaling()` + the export block |
| `src/editor/shell_smoke_graph.cpp` | `validate_timeline_scale_shell_smoke()` + count guards |
| `src/editor/shell_smoke_scenarios.hpp`, `src/editor/shell_smoke.cpp` | one declaration, one call |
| `src/samples/agent_dispatch_smoke.cpp` | expectation row, count, behaviour cases |
| `tools/mcp/test_client.py` | counts, `new_edit_operations`, the E2E sequence |
| `AGENTS.md`, `docs/root1/*.md`, `.agents/tasks/prd-marrow-runtime.json` | documentation and closure |

---

## Tasks

### Task 0: Reconcile with the as-built MAR-171 and MAR-172

**This task is mandatory and blocking. Do not write production code in it.**

The design was written when MAR-172 existed only as a spec and plan, `git log`
showed `b19b7b5` (MAR-171) as the last feature commit, and
`std::size(kOperationSpecs)` was **58**. Every MAR-172 contract quoted in the
design must be checked against the code that is actually present.

- [ ] Confirm the working tree is clean; record `git rev-parse HEAD` and
      `git log --oneline -8`.
- [ ] **Confirm MAR-172 is merged.** If `set_timeline_loop_sync()` and
      `synchronize_loop_boundaries()` are absent from
      `include/marrow/editor/authoring.hpp`, **stop and report**: MAR-173
      `dependsOn` MAR-172, and §8.5's pin extraction and §8.6's seam inheritance
      are both meaningless without it.
- [ ] Read, in full, and record the exact as-built signature or shape of each:
      - `src/editor/timeline_model.hpp` — `kKeyTimeEpsilon`, `kNonEventKeySpacing`,
        `KeyRef`, `TrackRow`, `key_ref()`, `key_index()`, `reconcile_selection()`,
        `snap_delta_to_frames()`, `incremental_retime_delta()`,
        `completion_decision()`, `include_retime_bounds()`,
        `include_animation_timeline_maximum()`.
      - `include/marrow/editor/authoring.hpp` — `AuthoringResult`,
        `TimelineKeyKind`, `TimelineKeySelector`, `TimelineRetimeResult`,
        `retime_keyframes()`, `offset_keyframe_scalars()`,
        `set_animation_duration()`,
        `auto_extend_explicit_animation_durations()`,
        `resolve_automatic_curves()`, and everything MAR-172 appended.
      - `src/editor/authoring.cpp` — `ResolvedTimelineKey` (`:115`),
        `resolve_timeline_key()` (`:158`), `matching_timeline_index()`,
        `matching_key_index()`, `include_retime_bounds()` (`:433`),
        `include_timeline_retime_bounds()` (`:453`),
        `include_resolved_retime_bounds()` (`:478`) **including MAR-172's inline
        pin block**, `apply_resolved_retime()` (`:551`),
        `sort_retimed_timelines()` (`:596`), `set_animation_duration()`
        (`:1554`), `auto_extend_explicit_animation_durations()` (`:1670`),
        `retime_keyframes()` (`:1728`), and MAR-172's
        `set_timeline_loop_sync()` / `synchronize_loop_boundaries()`.
      - `src/editor/timeline_controller.cpp` —
        `begin_timeline_retime_gesture()` (`:2125`),
        `finish_timeline_retime_gesture()` (`:2167`),
        `apply_timeline_retime_delta()` (`:2196`) **in full**, and
        `resolve_timeline_auto_curves()` (`:1400`). The scale gesture mirrors
        `apply_timeline_retime_delta()` step for step; copy its actual shape, not
        the design's paraphrase.
      - `src/editor/shell_state.hpp` — `TimelineRetimeGesture` (`:552`),
        `TimelineGraphPointDrag`, `TimelineGraphValueGesture`,
        `TimelineEditorState` (`:680`), `authoring_gesture_active()` (`:803`).
      - `src/editor/shell_core.cpp` — `cancel_authoring_gestures()` (`:431`).
      - `src/editor/shell_timeline.cpp` — `timeline_time_from_x()` /
        `timeline_x_from_time()` (`:104-121`),
        `update_timeline_retime_gesture()` (`:123`), `draw_timeline_ruler()`
        (`:150`), `draw_timeline_lane()`, and the dopesheet body's ordering
        around `draw_timeline_ruler(state, duration_seconds);`
        (**`:2083` today** — re-locate it, MAR-172 may have shifted lines).
      - `src/editor/agent_handlers_editing.cpp` — the whole
        `timeline.retime_keyframes` block (`:737-956`) **verbatim**, plus
        `classify_timeline_key_error()` (`:232`) and `resolve_agent_auto_curves()`.
      - `tools/mcp/tools/editing.py` — `_timeline_retime_key_schema()` (`:190`)
        and the `timeline.retime_keyframes` tool (`:573`).
- [ ] **Record the answer to each row of design §4.5** and note any fallback it
      names. In particular: does `include_resolved_retime_bounds()` contain
      MAR-172's pin block, and in exactly what shape? Where does
      `synchronize_loop_boundaries()` run?
- [ ] **Measure the registry count. Do not assume it.**
      ```bash
      python3 - <<'EOF'
      import re
      src = open('src/editor/agent_dispatch.cpp').read()
      body = re.search(r'kOperationSpecs\[\]\s*=\s*\{(.*?)\n\};', src, re.S).group(1)
      rows = re.findall(r'\n\s*\{\s*"([^"]+)"', body)
      print(len(rows))
      print('\n'.join(f'{i} {n}' for i, n in enumerate(rows)))
      EOF
      ```
      Record the count `N` and the index of the **last** `timeline.*` row.
      MAR-173's row goes at that index + 1 and the new total is `N + 1`.
      **If `N != 59`, every "60" in this plan is `N + 1` instead — write the
      corrected numbers into this plan's checklist before Task 6 and report the
      delta.**
- [ ] **Count the shell-smoke guards. Do not assume it.**
      ```bash
      grep -n "operation_count_before != " src/editor/shell_smoke_graph.cpp
      grep -rn "agent_operation_descriptor_count" src tools | grep -v build
      ```
      On `b19b7b5` there were **five** guards (`:146`, `:634`, `:1556`, `:2126`,
      `:3140`), not the four MAR-172's spec recorded. Record the as-built count
      `G`; every one of them changes, plus MAR-173's own new scenario adds one.
- [ ] Run the count sweep and classify every hit as *current* (becomes `N + 1`)
      or *historical* (unchanged), per design §13:
      ```bash
      rg -n '\b59\b' --glob '!build*' src tools AGENTS.md docs .agents
      rg -n '\b60\b' --glob '!build*' src tools AGENTS.md docs .agents
      ```
      Cross off every known false positive in design §13's list before touching
      anything. **`0.58`, `58px`, `8.58 s`, `12.58 s`, `IM_COL32(…, 57, …)`,
      `rgb(54, 57, 64)`, and PRD timestamps are never edited.**
- [ ] Establish the export baseline in the same run you will compare against:
      ```bash
      cmake -S . -B build && cmake --build build -j8
      ./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
        --export-runtime /tmp/mar173_baseline.mskl \
        --export-binary /tmp/mar173_baseline.mbin
      wc -c /tmp/mar173_baseline.mskl /tmp/mar173_baseline.mbin
      ```
      Record both numbers. Do **not** reuse 14336/3984 from another milestone's
      notes; measure them here.
- [ ] Record the fixture's actual `idle`/`spine`/`rotate` key times and curves,
      and `idle`'s duration state, from `assets/fixtures/player_idle.marrow` and
      `assets/fixtures/player_idle.mskl`. §17.2 and §17.6 assume
      `{0.0, 0.5, 1.0}` with no explicit duration; **verify, and adjust the test
      numbers if MAR-172's plan authored a duration into the fixture.**

**Verification:** a written reconciliation note in the task's own working
record — the as-built `N`, `G`, the export baselines, the MAR-172 pin block's
exact shape, and each §4.5 row's answer. No source file is modified.

---

### Task 1: The two behaviour-preserving extractions in `authoring.cpp`

Design §8.4, §8.5. **No new behaviour.** This task exists so the scale primitive
can share the spacing table and the loop-sync pin instead of restating them.

#### TDD step (RED)

There is no RED here in the usual sense, because the task adds no behaviour. The
gate is inverted and is stricter: **the existing suites must pass unchanged
after the refactor.**

- [ ] Before touching anything, run and record:
      ```bash
      ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
      ./build/marrow_timeline_model_tests
      ./build/marrow_agent_dispatch_smoke
      ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
      ```
- [ ] Add, as the RED, one new `marrow_timeline_model_tests` case per §17.1's
      last bullet: `family_key_spacing(kind)` returns `0.0` for `Event` and
      `kNonEventKeySpacing` for the other five. It fails to compile because the
      function does not exist.

#### Implementation step (GREEN)

- [ ] Add `double family_key_spacing(TimelineKeyKind kind)` to `authoring.cpp`'s
      anonymous namespace with the doc comment from §8.4, and expose it to the
      test through the same mechanism the file already uses for its other
      internal helpers (check how existing tests reach `authoring.cpp` internals;
      if none do, declare it in `src/editor/authoring_internal.hpp` if that
      header exists, otherwise place it in `timeline_model` where `TimelineKeyKind`
      is not visible — **in that case put the test in `marrow_project_smoke`
      instead and record the deviation**).
- [ ] Rewrite `include_resolved_retime_bounds()`'s six cases to pass
      `family_key_spacing(resolved.kind)` instead of the literal constants.
      The switch's structure is otherwise unchanged.
- [ ] Extract MAR-172's inline pin block from `include_resolved_retime_bounds()`
      into `bool resolved_key_is_loop_pinned(const ProjectData&, const
      ResolvedTimelineKey&)` with the doc comment from §8.5, and call it from
      where the block used to be. **The clamping arithmetic stays exactly where
      it was**; only the predicate moves.
      - If Task 0 found MAR-172 implemented pinning differently, extract from
        wherever it actually lives and record the deviation here.
      - If MAR-172 shipped without pinning at all, **skip this bullet entirely**,
        drop §8.5's checks from Task 2 and their tests from Tasks 2 and 7, and
        record it prominently in this plan and in the design's status line.

#### Verification

- [ ] `cmake --build build -j8`
- [ ] `./build/marrow_timeline_model_tests` — the new spacing case passes.
- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` —
      **byte-identical output** to the pre-refactor run recorded above.
- [ ] `./build/marrow_agent_dispatch_smoke` — same case count, same PASS.
- [ ] `ctest --test-dir build --output-on-failure -L editor`
- [ ] MAR-172's retime-pinning cases pass unchanged. Name them explicitly in the
      task record.

---

### Task 2: `scale_keyframe_times()`

Design §6, §11.2.

#### TDD step (RED)

- [ ] Add `validate_mar173_key_time_scaling(const ProjectLoadResult&)` to the
      project smoke beside `validate_mar172_loop_boundary_sync()`, and call it
      from `main` immediately after. Implement §17.2's cases:
      - both pivots on `{0.0, 0.5, 1.0}`, including the `RangeEnd`, `s = 1.25`
        negative-target rejection;
      - the pivot key bit-identical (`==` on the stored `double`), for both
        pivots and for two keys sharing the pivot time;
      - key order unchanged;
      - only `time` written — Transform (`angle`, four control points,
        `curve_mode`, `curve_driver`), Slot Color (four channels), Deform
        (`vertex_offsets`);
      - `s = 1` and a move-nothing ratio both `changed == false`, project
        byte-identical;
      - the fourteen rejections of §17.2, each byte-identical;
      - the `min(spacing, original_gap)` table from §6.4;
      - event ties: two-key and three-key ties bit-identical; partial tie
        rejected; a selected event key landing on an unselected one accepted;
      - one case per family;
      - MAR-171: whole-track scale leaves auto curves byte-identical
        (`resolved_key_count == 0`); subset scale changes them;
      - MAR-172: first-key and last-key pin rejections; middle-key scale clean;
      - save/reload bitwise round trip.
- [ ] Run `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` and
      observe it **fail to compile** on the missing
      `marrow::editor::scale_keyframe_times`. Then stub the declaration only and
      observe the cases **fail at runtime** on the unimplemented body. Record
      both.

#### Implementation step (GREEN)

- [ ] Append `TimelineScalePivot`, `TimelineScaleResult`, and
      `scale_keyframe_times()` to `include/marrow/editor/authoring.hpp` after
      MAR-172's declarations, with the doc comment from §11.2 verbatim. Add
      nothing else to the header.
- [ ] Implement `scale_keyframe_times()` in `authoring.cpp` after
      `retime_keyframes()`, following §6.3's thirteen steps in order:
      1-2. argument validation, then `ProjectData candidate = *project;`
      3. resolve through `resolve_timeline_key()`; reject empty animation names,
         non-finite/negative selector times, and duplicates by
         `(int(kind), timeline_index, key_index)` in a `std::set<std::tuple<...>>`
         — the same shape `retime_keyframes()` uses at `:1764-1770`;
      4. reject a selector set naming more than one animation;
      5. reject any `resolved_key_is_loop_pinned()` key;
      6. compute `p` and `span` from the resolved original times; reject
         `span <= kKeyTimeEpsilon`;
      7. compute every target from `resolved[i].original_time`;
      8. reject a non-finite, negative, or out-of-float32 target — reuse the
         existing `finite_animation_scalar()` helper (`authoring.cpp:281`);
      9. per affected `(kind, timeline_index)` group, build the projected list
         over the timeline's whole `keyframes` vector and apply §6.4's adjacent
         rule plus §6.5's event-tie rule;
      10. `changed == false` when no target moved by more than `1e-12`;
      11. write with a new `apply_resolved_scale(&candidate, resolved, p, s)`
          mirroring `apply_resolved_retime()`'s template shape (`:539-576`),
          writing `p + (resolved.original_time - p) * s`;
      12. `sort_retimed_timelines(&candidate, resolved);`
      13. `*project = std::move(candidate);`
- [ ] Every rejection message must name the animation, the lane, and the
      offending times, per §6.4, §6.5, §8.5.

#### Verification

- [ ] `cmake --build build -j8`
- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` — all
      §17.2 cases pass.
- [ ] `./build/marrow_project_smoke --create /tmp/player_idle.marrow`
- [ ] `./build/marrow_agent_dispatch_smoke` and
      `./build/marrow_timeline_model_tests` still pass unchanged.
- [ ] `git diff --stat include/marrow/editor/authoring.hpp` shows **additions
      only**.

---

### Task 3: The ratio math in `timeline_model`

Design §7, §9.2, §11.1.

#### TDD step (RED)

- [ ] Add §17.1's cases to `marrow_timeline_model_tests`:
      `selection_time_span()` (empty / single / all-same-time / two tracks /
      unresolvable ref), `scale_from_edge_time()` (both directions, every
      `nullopt` path), `snap_scale_to_frames()` (24/30/60 fps, both pivots,
      every `nullopt` path, and the round-trip through
      `scale_from_edge_time()` within `1e-12`), the **equivalence assertion**
      against a direct `snap_delta_to_frames()` call, and
      `incremental_scale_ratio()` including the 5000-frame composition.
- [ ] Run `./build/marrow_timeline_model_tests` and observe the compile failure
      on the four missing symbols. Record it.

#### Implementation step (GREEN)

- [ ] Declare `SelectionTimeSpan`, `selection_time_span()`,
      `scale_from_edge_time()`, `snap_scale_to_frames()`, and
      `incremental_scale_ratio()` in `src/editor/timeline_model.hpp` per §11.1,
      placed after `incremental_retime_delta()` and `completion_decision()`.
- [ ] Implement them in `timeline_model.cpp` exactly as §7.1, §7.2, §7.3, and
      §9.2 specify. `snap_scale_to_frames()` **must call** the existing
      `snap_delta_to_frames()`; do not inline its arithmetic.

#### Verification

- [ ] `cmake --build build -j8`
- [ ] `./build/marrow_timeline_model_tests` — the reported case count grew by
      the number of new cases and all pass.
- [ ] `ctest --test-dir build --output-on-failure -L editor`

---

### Task 4: The shell scale gesture

Design §8.8, §9.3, §9.4, §10, §11.3, §11.5.

#### TDD step (RED)

- [ ] Add `validate_timeline_scale_shell_smoke()` to
      `src/editor/shell_smoke_graph.cpp`, declare it in
      `shell_smoke_scenarios.hpp`, and call it from `shell_smoke.cpp` after
      MAR-172's scenario. Implement §17.5's cases **except** the registry-count
      guard (Task 6 sets its number). Give the scenario its own
      `ScopedPreferenceIsolation`.
- [ ] Run
      `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
      and observe the compile failure on the missing gesture entry points.
      Record it.

#### Implementation step (GREEN)

- [ ] Add `TimelineScaleDragCandidate` and `TimelineScaleGesture` to
      `shell_state.hpp` per §11.5, plus the two `TimelineEditorState` members.
      Add `state.timeline_editor.scale_gesture.has_value()` to
      `authoring_gesture_active()`. **Do not** add `scale_drag` there.
- [ ] Add both to `cancel_authoring_gestures()` in `shell_core.cpp`:
      `scale_gesture` through the existing `cancel_transaction_gesture` lambda,
      `scale_drag` as a plain `.reset()` that also sets `cancelled = true` only
      if a gesture was also live — a bare candidate holds nothing, so cancelling
      it alone must not report a cancelled edit.
- [ ] Implement `begin_timeline_scale_gesture()`,
      `apply_timeline_scale_ratio()`, `finish_timeline_scale_gesture()`, and
      `timeline_scale_rejection()` in `timeline_controller.cpp`, placed after
      `finish_timeline_retime_gesture()`.
      - `begin_*` mirrors `begin_timeline_retime_gesture()` (`:2125`): check
        `authoring_gesture_active()` and a non-empty selection, open the
        transaction with group `"timeline:scale"`, snapshot `keys` and
        `original_times`, and cancel + return false if any key is unresolvable or
        its track is not editable. Additionally compute `pivot_time` and
        `edge_original_time` from `selection_time_span()`, reject a span at or
        below `kKeyTimeEpsilon`, and run the partial-event-tie and loop-sync-pin
        preflights from §9.3.
      - `apply_*` mirrors `apply_timeline_retime_delta()` (`:2196`) step for
        step: early-out on an unchanged request; `incremental_scale_ratio()`;
        materialize once through `visit_editable_timeline_keys()`; re-resolve
        every index and cancel on a lost identity; build selectors through
        `timeline_key_selector()`; call `scale_keyframe_times()`.
        **The one deliberate divergence:** on a `scale_keyframe_times()` error,
        do **not** call `finish_*(false)`. Store the message in
        `gesture.rejection`, leave `applied_scale` alone, and return `true`
        (§10.2). Every *other* failure — materialization, lost identity,
        `resolve_timeline_auto_curves()`, `refresh_runtime()` — cancels exactly
        as retime does.
        On success: clear `gesture.rejection`, call
        `resolve_timeline_auto_curves()`, `refresh_runtime()`,
        `sync_shell_from_editor_session()`, rebuild `selected_keys` and
        `active_key` from the same resolved indices, set
        `gesture.applied_scale = requested_scale` and `gesture.changed = true`.
      - `finish_*` mirrors `finish_timeline_retime_gesture()` (`:2167`) with
        `completion_decision()`, and adds §7.3's **drift check before the
        commit**: re-derive each key's expected time from `original_times`,
        `pivot_time`, and `applied_scale`, and cancel with
        `"Timeline scale drifted; the edit was discarded"` on any deviation
        greater than `kKeyTimeEpsilon`.
      - Status messages: `"Scaled timeline key"` / `"Scaled timeline keys"`,
        `"Cancelled timeline scale"`, `"Timeline scale failed"`.

#### Verification

- [ ] `cmake --build build -j8`
- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
      — every §17.5 case passes.
- [ ] `ctest --test-dir build --output-on-failure -L editor`
- [ ] `$HOME/Library/Application Support/Marrow` still does not exist.

---

### Task 5: The dopesheet selection range bar

Design §9.

#### TDD step (RED)

- [ ] Extend `validate_timeline_scale_shell_smoke()` with the ImGui-free driver
      cases: `begin_timeline_scale_drag()` arming and refusing (active gesture,
      partial tie, pinned key, degenerate span), `update_timeline_scale_drag()`
      holding inside the 4.0 px dead zone and opening the gesture on leaving it,
      the Alt bypass path, and `cancel_timeline_scale_drag()` clearing a bare
      candidate without touching history.
- [ ] Observe the compile failure on the three missing entry points.

#### Implementation step (GREEN)

- [ ] Declare the three entry points in `shell_timeline.hpp` per §11.4 and
      implement them in `shell_timeline.cpp` beside
      `update_timeline_retime_gesture()`.
- [ ] Add `draw_timeline_selection_range_bar(ShellState*, const
      std::vector<TimelineTrackRow>&)` and call it from the dopesheet body
      **immediately after** `draw_timeline_ruler(state, duration_seconds);` and
      before the `timeline_tracks` table.
      - Skip entirely (consuming no layout) when §9.2's conditions do not all
        hold.
      - `InvisibleButton("timeline_selection_range", ImVec2(width, 14.0f))` using
        the same `std::max(96.0f, ImGui::GetContentRegionAvail().x)` width the
        ruler uses, so the x mapping matches.
      - Draw the span bar, the two 7 px grips, and — while a gesture is live —
        the readout from §9.5, with `gesture.rejection` appended in the error
        colour when non-empty.
      - Sample `io.MousePos.x`, `ImGui::IsMouseDown(ImGuiMouseButton_Left)`,
        `io.KeyAlt`, and `ImGui::IsKeyPressed(ImGuiKey_Escape, false)`, and hand
        them to `update_timeline_scale_drag()`. **No pixel-to-ratio arithmetic in
        this function.**
- [ ] In `draw_timeline_ruler()`, suppress the wheel-zoom and middle-drag-pan
      blocks while `scale_drag` or `scale_gesture` is live (§9.6). One
      early-return guard on each of the two existing `if` blocks.
- [ ] Cancel any live candidate or gesture on a `requested_view_mode` transition
      away from Dopesheet, beside the existing MAR-168 graph cancellation.

#### Verification

- [ ] `cmake --build build -j8`
- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- [ ] `ctest --test-dir build --output-on-failure -L editor`
- [ ] Visual confirmation is **not** claimed. Record that no manual-visible-UI
      credit is taken.

---

### Task 6: The Agent operation

Design §12.1-§12.6, §13.

**This task has two halves and the first must be green before the second
starts.**

#### Half A — the extraction (behaviour-preserving)

##### TDD step (RED)

- [ ] Record the current `./build/marrow_agent_dispatch_smoke` output verbatim,
      including the `[ OK ]` case count.
- [ ] The gate is inverted, as in Task 1: after the extraction, the **same** case
      count must pass with the **same** messages.

##### Implementation step (GREEN)

- [ ] Declare `timeline_key_selectors_arg()` in `agent_dispatch_internal.hpp`
      per §12.2, define it in `agent_handlers_editing.cpp` by **moving** the
      body of `timeline.retime_keyframes`'s key loop (`:756-845`) into it and
      replacing every literal `"timeline.retime_keyframes"` in a message with the
      `operation_label` parameter.
- [ ] Rewire `timeline.retime_keyframes` to call it with
      `"timeline.retime_keyframes"`.

##### Verification

- [ ] `cmake --build build -j8`
- [ ] `./build/marrow_agent_dispatch_smoke` — **identical** case count and
      output to the recorded baseline. Any message difference is a bug in the
      extraction, not an acceptable variation.

#### Half B — the new operation

##### TDD step (RED)

- [ ] Add the `timeline.scale_key_times` expectation row to
      `kExpectedOperations` immediately after `timeline.set_loop_sync`, bump the
      `std::array<OperationExpectation, N>` size to the Task 0 measured value + 1,
      and add §17.7's behaviour cases.
- [ ] Run `./build/marrow_agent_dispatch_smoke` and observe it fail on the
      registry size mismatch and on `expect_complete_coverage()`.

##### Implementation step (GREEN)

- [ ] Add the `kOperationSpecs[]` row at the Task 0 measured position:
      ```cpp
      {"timeline.scale_key_times", "edit", true, false, true, true, &handle_editing_operation},
      ```
      matching the exact field order the neighbouring rows use.
- [ ] Add the `if (op == "timeline.scale_key_times") { ... }` block to
      `handle_timeline_editing_operation()` immediately after
      `timeline.set_loop_sync`, implementing §12.3-§12.6: parse through
      `timeline_key_selectors_arg()`, require `scale` and `pivot`, optional
      `snap` (**default `false`**) and `frames_per_second`, snap through
      `timeline_model::snap_scale_to_frames()` on the resolved moved edge, the
      six `ensure_*_timeline_edit()` materializations in the `apply` lambda,
      `scale_keyframe_times()`, the §12.5 response with the 256-entry key cap and
      `keys_truncated`, the dry-run path that never touches the session, and the
      live path with `resolve_agent_auto_curves()` and `commit_or_error()` using
      `CommitPolicy{"Failed to scale timeline keys: "}`.
- [ ] Update **every** count guard Task 0 found in `shell_smoke_graph.cpp`
      (`G` of them) plus MAR-173's own new scenario's guard, and their message
      strings.

##### Verification

- [ ] `cmake --build build -j8`
- [ ] `./build/marrow_agent_dispatch_smoke` — all §17.7 cases pass; the reported
      registry is the new total.
- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
      — every count guard passes.
- [ ] `ctest --test-dir build --output-on-failure`

---

### Task 7: The MCP tool

Design §12.7, §17.8.

#### TDD step (RED)

- [ ] Update `tools/mcp/test_client.py`: both count asserts to the new total, add
      `timeline.scale_key_times` to `new_edit_operations`, and add §17.8's
      sequence and rejection cases.
- [ ] Start the shell and run the client; observe it fail on
      `assert len(mcp_names) == <new total>`:
      ```bash
      ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
      tools/mcp/venv/bin/python tools/mcp/test_client.py
      ```

#### Implementation step (GREEN)

- [ ] Add one `types.Tool` named `timeline.scale_key_times` to
      `tools/mcp/tools/editing.py`, immediately after `timeline.set_loop_sync`,
      with the schema and description from §12.7. Reuse
      `_timeline_retime_key_schema()` for `keys`. Add no other tool and change no
      existing one.

#### Verification

- [ ] `tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py`
- [ ] `tools/mcp/venv/bin/python tools/mcp/test_client.py` against a running
      `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876`
      → `mcp test_client: PASSED`.
- [ ] `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only`
      against `assets/fixtures/parameter_face_basic.marrow`.
- [ ] **Verify the negative once by hand**: comment the new tool out, confirm the
      client fails on the count assert, restore it. Record that you did.
- [ ] `$HOME/Library/Application Support/Marrow` still does not exist after the
      `--agent-port` runs.

---

### Task 8: Export, proved on a mutated project

Design §4.4, §17.6. **This is the task the MAR-168/169 defect pattern targets.**

#### TDD step (RED)

- [ ] Add the export block to `validate_mar173_key_time_scaling()` implementing
      §17.6's six steps, with step 3's loaded-value assertions written **first**
      and the byte-size print written last.
- [ ] Run the smoke and observe step 3 fail before any size is consulted,
      because the export is not yet wired to the mutated project. **If it passes
      immediately, the block is exporting the wrong project — fix that before
      continuing.**

#### Implementation step (GREEN)

- [ ] Ensure `export_runtime_assets()` is called on the `ProjectData` the
      validator **mutated**, not on a copy and not on the untouched load result.
      This is the exact mistake MAR-168 and MAR-169 made; assert it structurally
      by scaling, then exporting, then reloading, then asserting the value.
- [ ] Print the two lines from §17.6 step 6, including the explicit note that an
      unchanged `.mbin` size is correct here.

#### Verification

- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` — prints
      a `.mskl` size **strictly larger** than the Task 0 baseline and passes every
      loaded-value assertion.
- [ ] `./build/marrow_inspect --compare /tmp/marrow_mar173_scale.mbin /tmp/marrow_mar173_scale.mskl`
- [ ] `./build/marrow_fixture_smoke /tmp/marrow_mar173_scale.mskl /tmp/player_idle.matl`
- [ ] `./build/marrow_renderer_sample /tmp/marrow_mar173_scale.mskl /tmp/player_idle.matl`
- [ ] `grep -c loop_sync /tmp/marrow_mar173_scale.mskl` → `0` (MAR-172's
      neutrality still holds through a scale).

---

### Task 9: Duration and cross-milestone interaction sweep

Design §8.3, §8.5, §8.6, §8.7, §17.4.

#### TDD step (RED)

- [ ] Add §17.4's three duration cases to
      `validate_mar173_key_time_scaling()` and the MAR-171/MAR-172 interaction
      cases from §17.2's last three bullets if they were not already written in
      Task 2. Observe them fail.

#### Implementation step (GREEN)

- [ ] **No production code is expected here.** The duration and boundary
      behaviours are inherited from the session seam with zero wiring (§8.3,
      §8.6). If a case fails, the failure is information: either the seam is not
      where Task 0 recorded it, or `auto_extend_explicit_animation_durations()`
      behaves differently than §8.3 assumes. Fix the *design's assumption* in the
      spec, not the seam, and record the correction.

#### Verification

- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
      — §17.5's auto-grow, MAR-171, and MAR-172 shell cases pass.
- [ ] `git diff --stat src/editor/session.cpp` → **no change**.

---

### Task 10: Full validation, documentation, and MAR-173 closure

- [ ] Run the entire "Full verification checklist" below, in order, recording
      every command's actual output.
- [ ] Add a `MAR-173 Atomic Key Time Scaling Validation Results` section to
      `AGENTS.md` in the existing table-plus-command-output format used by the
      MAR-170/171 sections. Include the export line **and** its note verbatim.
- [ ] Update `AGENTS.md`'s `Current Validation` registry line to the new total
      and extend its parenthetical with "timeline key-time scaling".
- [ ] Update `AGENTS.md:6` `Project State`: MAR-173 complete, MAR-174 next.
- [ ] Apply design §13's table to every *current* documentation site and leave
      every *historical* one alone. Re-run the sweep afterwards and confirm no
      false positive was edited:
      ```bash
      git diff --stat
      git diff -- AGENTS.md docs/ | grep -E '^[+-].*\b(0\.58|58px|:5[789]|:60)\b'
      ```
      That last command must print nothing.
- [ ] Add the scaling paragraph to `docs/root1/concepts.md` (§18) and the
      milestone paragraph to `docs/root1/discription.md`.
- [ ] **Confirm** `docs/root1/format-spec.md` needs no change, and say so in the
      task record rather than silently skipping it.
- [ ] Mark `MAR-173` done in `.agents/tasks/prd-marrow-runtime.json` with the
      verified completion date; leave `MAR-174` open; leave MAR-192..MAR-210
      open with no qualification credit.
- [ ] Change the design document's status line to
      `Implemented and validated (<date>)`.
- [ ] `git diff --check` clean.

---

## Full verification checklist

Run in this order. Record actual output for every line; a line with no recorded
output is not a passed gate.

### Build and unit gates

- [ ] `cmake -S . -B build`
- [ ] `cmake --build build -j8`
- [ ] `cmake --build build --target marrow_verify_third_party`
- [ ] `cmake --build build --target marrow_constraint_warning_check`
- [ ] `./build/marrow_unit_tests`
- [ ] `./build/marrow_timeline_model_tests`
- [ ] `./build/marrow_timeline_graph_model_tests`
- [ ] `./build/marrow_selection_tests`
- [ ] `./build/marrow_preference_tests` — **unchanged case count**; MAR-173
      touches no preference code
- [ ] `./build/marrow_viewport_interaction_tests`
- [ ] `./build/marrow_windowing_tests`
- [ ] `./build/marrow_pen_input_tests`

### Project, storage, and export gates

- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- [ ] `./build/marrow_project_smoke --create /tmp/player_idle.marrow`
- [ ] `./build/marrow_parameter_project_smoke assets/fixtures/parameter_face_basic.marrow`
- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl --export-binary /tmp/player_idle_project_export.mbin`
- [ ] `./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin /tmp/player_idle_project_export.mskl`
- [ ] `./build/marrow_inspect --compare /tmp/marrow_mar173_scale.mbin /tmp/marrow_mar173_scale.mskl`
- [ ] `./build/marrow_fixture_smoke /tmp/marrow_mar173_scale.mskl /tmp/player_idle.matl`
- [ ] `./build/marrow_renderer_sample /tmp/marrow_mar173_scale.mskl /tmp/player_idle.matl`
- [ ] The MAR-173 export line reports a `.mskl` **strictly larger** than the Task 0
      baseline, and the printed note about the `.mbin` size is present
- [ ] `grep -c loop_sync /tmp/marrow_mar173_scale.mskl` → `0`
- [ ] `python3 -m json.tool /tmp/marrow_mar173_scale.mskl > /dev/null`

### Shell gates

- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- [ ] `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2`
- [ ] `./build/marrow_editor_shell --verify-launch-focus`
- [ ] The new `validate_timeline_scale_shell_smoke()` scenario appears in the
      shell smoke's output and passes
- [ ] Every registry count guard in `shell_smoke_graph.cpp` carries the new total

### Agent and MCP gates

- [ ] `./build/marrow_agent_dispatch_smoke` — the new total, the new expectation
      row in position, and every §17.7 case
- [ ] `tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py`
- [ ] `tools/mcp/venv/bin/python tools/mcp/test_client.py` against
      `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876`
- [ ] `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only`
      against `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --agent-port 9876`
- [ ] The MCP tool-removal negative was verified by hand once

### Regression gates

- [ ] `ctest --test-dir build -N`
- [ ] `ctest --test-dir build --output-on-failure`
- [ ] `ctest --test-dir build --output-on-failure -L runtime`
- [ ] `ctest --test-dir build --output-on-failure -L editor`
- [ ] `ctest --test-dir build --output-on-failure -R marrow.renderer_link_boundary`
- [ ] Every existing `timeline.retime_keyframes` agent case passes **unchanged**
      after §12.2's extraction
- [ ] MAR-172's retime-pinning cases pass **unchanged** after §8.5's extraction
- [ ] MAR-171's `resolved_key_count == 0` duration assertion passes unchanged
- [ ] MAR-169's and MAR-170's easing cases pass unchanged

### Compatibility gates

- [ ] `git diff --stat include/marrow/c_api src/c_api` → **empty**
- [ ] `git diff --stat include/marrow/editor/preferences.hpp src/editor/preferences.cpp` → **empty**
- [ ] `git diff --stat src/runtime include/marrow/runtime` → **empty**
- [ ] `git diff --stat src/editor/session.cpp` → **empty**
- [ ] `git diff include/marrow/editor/authoring.hpp` shows **additions only**
- [ ] `docs/root1/format-spec.md` unchanged, and the reason recorded
- [ ] `.mskl` version 1, `.mbin` version 2, `kEditorSettingsVersion` 1 — grep and
      confirm
- [ ] `$HOME/Library/Application Support/Marrow` did **not** exist before the run
      and still does not exist after every gate, including the `--agent-port` runs
- [ ] `git diff --check` clean

### Display gates (unchanged coverage)

- [ ] `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j8 && ctest --test-dir build-display --output-on-failure`
- [ ] `ctest --test-dir build-display --output-on-failure -L windowing`
- [ ] `ctest --test-dir build-display --output-on-failure -L display`
- [ ] `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j8 && ctest --test-dir build-platform-release --output-on-failure`
- [ ] No manual-visible-UI, Windows 11, or physical-input qualification credit is
      claimed. MAR-192 through MAR-210 stay open.

---

## Decisions Taken Under Ambiguity

These are the plan-level decisions. The design's own list is §16 and is not
repeated here.

1. **Task 1 and Task 6 Half A are pure refactors with inverted gates.** Both
   extractions are prerequisites for sharing rather than duplicating a rule, and
   both are the kind of change that silently alters an error string. Making each
   its own task, with "the existing suite passes unchanged, byte for byte" as its
   only acceptance criterion, is the cheapest way to keep a refactor bug from
   being attributed to the new feature three tasks later.

2. **Task 0 is blocking and re-measures things MAR-172's own spec got wrong.**
   MAR-172's §14 recorded four count guards in `shell_smoke_graph.cpp`; there are
   **five** on `b19b7b5`, because MAR-171 added one and the spec was written
   before MAR-171 landed. The same class of drift will apply to MAR-173, so Task 0
   counts rather than trusts — including the export baseline, which is measured in
   the same run it will be compared against rather than quoted from another
   milestone's notes.

3. **Task 9 expects to write no production code, and says so.** The duration and
   loop-boundary behaviours are inherited from the session seam. Writing the tests
   anyway is what turns "inherited with zero wiring" from a claim into a checked
   fact, and a failure there is a signal that the design's assumption about the
   seam is wrong — which is worth catching loudly rather than papering over with a
   wiring change that would duplicate MAR-172's mechanism.

4. **The scale gesture is built (Task 4) before its ImGui surface (Task 5).**
   The headless entry points are what the shell smoke drives, so building them
   first means the whole gesture — including every cancel and rejection path — is
   under test before a single ImGui call exists. It also keeps Task 5 small
   enough that "no pixel-to-ratio arithmetic in the draw function" is easy to
   verify by reading it.

5. **The export task is last among the behaviour tasks and is called out by
   name.** MAR-168 and MAR-169 both shipped a fake export criterion. Task 8's RED
   step explicitly requires the loaded-value assertion to fail first, and says
   that a block which passes immediately is exporting the wrong project. The size
   print is written last so it cannot become the thing the implementer optimizes
   for.

6. **Every count in this plan is written as "the Task 0 measured value + 1"
   rather than as a literal, with `60` used only as the expected case.** If
   MAR-172 lands at a different total — or does not land — the plan still names
   the right number and the discrepancy is reported rather than silently encoded.
