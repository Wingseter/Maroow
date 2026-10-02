# MAR-171 Project-Local Automatic Curve Handles Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Store manual/automatic curve intent plus a driver scalar as two
optional, additive, `.marrow`-only keyframe fields that default every existing
project to today's manual behaviour byte for byte; resolve automatic keys to the
existing shared `[cx1, cy1, cx2, cy2]` easing with monotone Fritsch–Carlson
tangents that never produce a non-finite value and never overshoot; recompute
every affected curve inside the same transaction as the neighbour-time,
neighbour-value, driver, retime, insertion, deletion, paste, or duration change
that invalidated it; demote a segment to manual whenever any absolute easing is
written to it, including MAR-169's handle drag; and expose curve-mode mutation
as the 58th Agent operation `timeline.set_curve_mode` plus one matching MCP
tool — while `.mskl` v1, `.mbin` v2, C ABI v1, and `editor-settings.json` v1 are
all unchanged and the runtime export carries only the resolved `curve`.

**Architecture:** One new pure translation unit `src/editor/curve_auto.hpp/.cpp`
holding the Fritsch–Carlson math with no `ProjectData`, no session, and no
ImGui. `TimelineCurveMode` and the two keyframe members land in
`include/marrow/editor/project.hpp`, with `TimelineScalarComponent` relocated
there from `authoring.hpp`. `src/editor/project.cpp` gains parse, validate, and
serialize for the two optional fields and **no export change at all**.
`include/marrow/editor/authoring.hpp` gains `set_keyframe_curve_mode()` and
`resolve_automatic_curves()`, and `set_keyframe_interpolation()` gains the
demotion side effect. `timeline_controller` gains
`apply_timeline_curve_mode()` and a shared `resolve_timeline_auto_curves()`
called from every timeline transaction. The Agent registry grows by one row.

**Tech Stack:** C++17, Dear ImGui, existing Marrow runtime/editor session APIs,
CMake/CTest, Python MCP SDK, JSON fixtures.

**Spec:** `docs/superpowers/specs/2026-08-30-mar-171-project-local-automatic-curve-handles-design.md`

## Global Constraints

- Read `docs/root1/discription.md` and the MAR-171 PRD story before each
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
  four paths must be empty at every checkpoint. If a task appears to require a
  change there, stop and report — it means the design was misread.
- **Never touch `src/editor/preferences.cpp` or
  `include/marrow/editor/preferences.hpp`.** `kEditorSettingsVersion` stays `1`
  and MAR-171 adds no preference field. Newly authored keys are always manual.
- **Never change `build_runtime_document()` or any `build_runtime_*_value()`
  function in `src/editor/project.cpp`.** Export neutrality is achieved by not
  editing the export path, and Task 4 proves it by reading the exported file as
  text.
- **Never add a `.marrow` field on the timeline-edit (lane) object.** MAR-172
  owns lane metadata. MAR-171 claims exactly two names, `curve_mode` and
  `curve_driver`, and only on Transform and Slot Color **keyframe** objects.
- **`DeformKeyframeEdit` gains no member.** The Deform exclusion is
  compile-enforced. If a Deform branch appears to need a curve mode, stop.
- The Fritsch–Carlson numbers exist in exactly one place —
  `src/editor/curve_auto.cpp`. No `1.0/3.0`, `2.0/3.0`, `3.0` clamp bound, or
  tangent formula may appear in `authoring.cpp`, the shell, the agent handler,
  or the MCP server. Tests may and should spell the expected values out
  literally, because a test that reads the constant it is checking proves
  nothing.
- Every automatic write goes through `resolve_automatic_curves()`; every
  absolute write goes through `set_keyframe_interpolation()`. Never introduce a
  third easing-writing path, and never give either function a component
  parameter.
- Never change MAR-168's point drag, MAR-169's handle geometry, its clamp/reject
  split, or its `[1/3, 1/3, 2/3, 2/3]` conversion seed, or MAR-170's preset
  constants and preference flow — beyond the single documented demotion side
  effect of `set_keyframe_interpolation()` and the single documented `changed`
  extension in the handle gesture.
- The Agent registry becomes exactly **58** in Task 7 and not before. From
  Task 0 through Task 6 inclusive, `std::size(kOperationSpecs)` must still be 57
  and every `57` assertion in the tree must still pass.
- Automated display tests prove the exercised ImGui/display path only. They add
  no manual-visible-UI, Windows 11, or physical-input qualification credit.
  MAR-192 through MAR-210 stay open.

## File and Responsibility Map

| File | MAR-171 responsibility |
| --- | --- |
| `src/editor/curve_auto.hpp` (new) | `Sample`, `kAutoControlPointX1/X2`, `kMinimumSegmentSeconds`, `segment_control_points()` |
| `src/editor/curve_auto.cpp` (new) | the five passes, the clamp, the Hermite→Bezier normalization, every rejection |
| `CMakeLists.txt` | one line adding `src/editor/curve_auto.cpp` to `marrow_editor` |
| `include/marrow/editor/project.hpp` | `TimelineCurveMode`; `TimelineScalarComponent` relocated here; two members on `TransformKeyframeEdit` and `SlotColorKeyframeEdit` |
| `src/editor/project.cpp` | parse, path-validated rejection, `validate_project()` rule, and conditional serialization of the two fields. **No export change** |
| `include/marrow/editor/authoring.hpp` | `TimelineCurveModeResult`, `TimelineAutoCurveResult`, `set_keyframe_curve_mode()`, `resolve_automatic_curves()`, the six token/default helpers; `TimelineScalarComponent` removed (relocated) |
| `src/editor/authoring.cpp` | the two primitives, the token helpers, the `set_keyframe_interpolation()` demotion |
| `src/editor/timeline_controller.hpp/.cpp` | `TimelineCurveModeApplyResult`, `apply_timeline_curve_mode()`, `resolve_timeline_auto_curves()`, `active_outgoing_curve_mode/driver()`, the resolver call at every trigger, the handle-gesture `original_mode` and `changed` rule |
| `src/editor/shell_state.hpp` | one `original_mode` member on `TimelineGraphHandleGesture` |
| `src/editor/shell_timeline_graph.hpp/.cpp` | the `Curve mode:` / `Driver:` row, the auto handle colour, the extended `Outgoing:` readout, the new render stats |
| `src/editor/agent_dispatch.cpp` | one `kOperationSpecs` row; `curve_mode_request_arg()` |
| `src/editor/agent_dispatch_internal.hpp` | the `curve_mode_request_arg()` declaration |
| `src/editor/agent_handlers_editing.cpp` | the `timeline.set_curve_mode` block; the resolver call in six existing blocks |
| `tools/mcp/tools/editing.py` | one `types.Tool` and one key schema helper |
| `src/tests/timeline_model_tests.cpp` | every pure-math case |
| `src/samples/editor_project_smoke.cpp` | `validate_mar171_automatic_curves()` and the mutated-project export block |
| `src/editor/shell_smoke_graph.cpp`, `shell_smoke_scenarios.hpp`, `shell_smoke.cpp` | `validate_timeline_curve_mode_shell_smoke()`; three `57` → `58` guards |
| `src/editor/shell_smoke_frames.cpp` | the actual-frame curve-mode and auto-handle cases |
| `src/samples/agent_dispatch_smoke.cpp` | `58` array size, one expectation row, the behaviour cases |
| `tools/mcp/test_client.py` | `58` counts, the `new_edit_operations` entry, the sequence |
| `AGENTS.md`, `docs/root1/*.md`, `.agents/tasks/prd-marrow-runtime.json` | closure |

---

### Task 0: Reconcile with the as-built MAR-169 and MAR-170

**This task is mandatory and blocking.** The design spec was written while
MAR-169's post-review cleanup was landing and before MAR-170 existed. Every
contract quoted below must be checked against the code that is actually present.
Do not write production code in this task.

- [ ] Confirm the working tree is clean and record `git rev-parse HEAD` and
      `git log --oneline -6`. If MAR-170 is not yet merged, **stop and report**:
      MAR-171 depends on it and several assertions below reference its widgets.
- [ ] Read, in full, and record the exact as-built signature or shape of each:
      - `include/marrow/editor/authoring.hpp` — `TimelineKeyKind`,
        `TimelineKeySelector`, `TimelineScalarComponent`,
        `TimelineInterpolationResult`, `set_keyframe_interpolation()`, and
        whatever MAR-170 appended (`kCurvePresets`, `curve_preset_of()`,
        `curve_preset_interpolation()`, `curve_preset_from_token()`).
      - `src/editor/authoring.cpp` — `ResolvedTimelineKey`,
        `resolve_timeline_key()`, `matching_timeline_index()`,
        `matching_key_index()`, `read_key_interpolation()`,
        `write_key_interpolation()`, `same_interpolation()`,
        `valid_bezier_control_point()`, `read_scalar_component()`,
        `set_keyframe_interpolation()`.
      - `include/marrow/editor/project.hpp` — `TransformKeyframeEdit`,
        `SlotColorKeyframeEdit`, `DeformKeyframeEdit`, `ProjectSnapSettings`,
        `ProjectData` (especially `preserved_root`).
      - `src/editor/project.cpp` — `parse_interpolation()`,
        `build_interpolation_value()`, `parse_transform_keyframes()`,
        the slot-colour keyframe parser, `build_transform_keyframes_value()`,
        `build_slot_color_keyframes_value()`,
        `build_runtime_slot_color_keyframes_value()`, `parse_snap_settings()`,
        the `snap` block in the serializer, the `snap` block in
        `validate_project()`, and the `read_optional_boolean` /
        `read_optional_number` / `find_optional_member` / `validation_error`
        helpers.
      - `src/editor/shell_state.hpp` — `TimelineGraphHandleGesture`,
        `TimelineGraphPointDrag`, `TimelineEditorState`,
        `authoring_gesture_active()`.
      - `src/editor/timeline_controller.hpp/.cpp` —
        `begin_timeline_graph_handle_gesture()`,
        `apply_timeline_graph_handle_control_points()`,
        `finish_timeline_graph_handle_gesture()`, `apply_timeline_retime_delta()`,
        `apply_timeline_graph_value_delta()`, `add_timeline_key_at_playhead()`,
        `remove_selected_timeline_keys()`, `cut_selected_timeline_keys()`,
        `paste_timeline_clipboard()`, `timeline_key_selector()`, and MAR-170's
        `apply_timeline_curve_preset()`.
      - `src/editor/shell_timeline_graph.hpp` — `TimelineGraphRenderStats` and
        whatever MAR-170 added to it.
      - `src/editor/agent_handlers_editing.cpp` — the whole
        `timeline.set_interpolation` block, `timeline_key_curve_value()`,
        `interpolation_curve_value()`, `classify_timeline_key_error()`,
        `timeline_key_kind_name()`, `transform_channel_name()`.
- [ ] **Verify the two in-flight deltas the spec §4.3 records.** Confirm whether
      `TimelineGraphHandleGesture` still has `component` / `component_index` /
      `handle`, and whether `begin_timeline_graph_handle_gesture()` takes 8 or 9
      arguments. Confirm whether `apply_timeline_graph_handle_control_points()`
      computes `gesture.changed` as a net-state comparison against
      `original_control_points`. Record both answers; Task 5 depends on them.
- [ ] Run `rg -n 'Interpolation::linear\(\)|set_keyframe_interpolation|retime_keyframes|offset_keyframe_scalars|\.interpolation\s*=' src/editor src/samples`
      and build the **complete** list of code paths that write a keyframe's
      `interpolation` or change a Transform/Slot Color key's time, value, or
      existence. Compare it against spec §9.2's trigger table. Any path in your
      list that is not in the table, and not justified as unreachable, must be
      added to the table before Task 5 — report the delta.
- [ ] Specifically confirm whether the numeric `Interpolation` combo and the
      `Bezier X1/Y1/X2/Y2` fields in `src/editor/shell_timeline.cpp` write
      through `set_keyframe_interpolation()` or assign `interpolation` directly.
      Record the answer; spec §9.4 requires a one-line demotion there if they
      assign directly.
- [ ] Run the `57` sweep and record every hit and its classification:
      ```bash
      rg -n '\b57\b' --glob '!build*' src tools AGENTS.md docs .agents
      rg -n '\b58\b' --glob '!build*' src tools AGENTS.md docs .agents
      ```
      Reconcile against spec §14's table. The `58` sweep must find no registry
      claim before Task 9.
- [ ] Confirm the baseline is green before changing anything:
      ```bash
      cmake -S . -B build && cmake --build build -j10
      ./build/marrow_timeline_model_tests
      ./build/marrow_timeline_graph_model_tests
      ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
      ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
      ./build/marrow_agent_dispatch_smoke
      ctest --test-dir build --output-on-failure
      ```
- [ ] Record the **untouched baseline export sizes**, which Task 4 compares
      against:
      ```bash
      ./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
        --export-runtime /tmp/marrow_mar171_baseline.mskl \
        --export-binary /tmp/marrow_mar171_baseline.mbin
      ./build/marrow_inspect --compare /tmp/marrow_mar171_baseline.mbin /tmp/marrow_mar171_baseline.mskl
      wc -c /tmp/marrow_mar171_baseline.mskl /tmp/marrow_mar171_baseline.mbin
      ```
- [ ] Write a short reconciliation note in the task log: every place the design
      spec's quoted API differs from as-built, and how the plan adapts. Do not
      silently adapt — an unrecorded divergence is how the MAR-169 export defect
      survived review.
- [ ] `git status --short` must be empty. This task changes no file.

---

### Task 1: The pure Fritsch–Carlson resolver

Depends on Task 0. Produces `src/editor/curve_auto.hpp/.cpp` and its unit
coverage. Nothing else in the tree knows about it yet.

**TDD step**

- [ ] Add `#include "curve_auto.hpp"` and a new `automatic curve control points`
      case to `src/tests/timeline_model_tests.cpp`, following the file's
      existing case style and counter. Assert, in this order:
      - `segment_control_points({})` and a one-sample call return an **empty**
        vector, not `std::nullopt`;
      - two samples `{(0,0),(1,10)}` return exactly one entry equal to
        `{1.0/3.0, 1.0/3.0, 2.0/3.0, 2.0/3.0}` within `1e-12`;
      - the fixture example `{(0,0),(0.5,8),(1,-2)}` returns exactly
        `{1.0/3.0, 1.0/3.0, 2.0/3.0, 1.0}` and
        `{1.0/3.0, 0.0, 2.0/3.0, 2.0/3.0}` within `1e-12`;
      - every returned entry has `cx1 == 1.0/3.0` and `cx2 == 2.0/3.0` bit-exactly,
        and both narrow into `(0, 1)` as `runtime::AnimationScalar`;
      - every returned `cy1` and `cy2` lies in `[0, 1]` for a monotone ramp, a
        spiky series `{0, 10, 0.5, 11, 0}`, a plateau `{5, 5, 9}`, and a fully
        repeated series `{5, 5, 5}`;
      - the plateau's segment 0 is exactly `{1/3, 1/3, 2/3, 2/3}` and its
        segment 1 has `cy1 == 0.0`;
      - sampling `runtime::Interpolation::cubic_bezier(cp[0], cp[1], cp[2], cp[3])
        .transform(alpha)` for `alpha` in `{0, 0.01, ..., 1}` on every entry of
        the spiky series is finite, non-decreasing within `1e-6`, inside
        `[0, 1]` within `1e-6`, exactly `0.0` at `alpha == 0`, and exactly
        `1.0` at `alpha == 1`;
      - the clamp fires: `{0, 1, 1.0001}` over equal spans yields
        `hypot(3*cy1, 3*(1-cy2)) <= 3.0 + 1e-9`;
      - `{0, 1e-300, 1e300}` over equal spans yields finite control points
        inside `[0, 1]` — this is the `std::hypot` case;
      - scale invariance: multiplying every `time_seconds` by `1000.0` and every
        `value` by `-7.0` returns bitwise-identical control points;
      - determinism: two calls on the same input return bitwise-identical
        vectors;
      - `std::nullopt` for a non-finite time, a non-finite value, a
        non-increasing time pair, and a segment of `1e-7` s.
- [ ] Build and run `./build/marrow_timeline_model_tests`. **It must fail to
      compile** (no `curve_auto.hpp`). Record the failure.

**Implementation step**

- [ ] Create `src/editor/curve_auto.hpp` exactly as spec §11.2 declares it.
- [ ] Create `src/editor/curve_auto.cpp` implementing spec §6.3's five passes:
      - reject up front: fewer than two samples returns an empty vector; any
        non-finite `time_seconds` or `value`, any non-increasing time, and any
        `h[i] <= kMinimumSegmentSeconds` returns `std::nullopt`;
      - pass A secants, pass B three-point tangents (arithmetic mean, one-sided
        at both ends), pass C flat zeroing over **all** segments, pass D the
        ascending in-place sign-then-`std::hypot` clamp, pass E the control
        points from the **final** tangent array;
      - the flat branch in pass E writes `cy1 = 1/3`, `cy2 = 2/3`;
      - clamp each `cy` into `[0, 1]` after pass E and return `std::nullopt` if
        any value or its `runtime::AnimationScalar` narrowing is not finite or
        leaves `[0, 1]`;
      - `static_assert` that
        `static_cast<runtime::AnimationScalar>(kAutoControlPointX1) > 0.0f` and
        `< 1.0f`, likewise for `kAutoControlPointX2`, and that
        `kAutoControlPointX1 < kAutoControlPointX2`.
- [ ] Add `src/editor/curve_auto.cpp` to the `add_library(marrow_editor STATIC ...)`
      source list in `CMakeLists.txt`, after `src/editor/authoring.cpp`.
- [ ] Add a doc comment above pass D stating the downward-closure argument from
      spec §6.3, so a future reader does not "fix" the in-place clamp.

**Verification**

- [ ] `cmake -S . -B build && cmake --build build -j10`
- [ ] `./build/marrow_timeline_model_tests` — the new case passes and the case
      counter grew by one.
- [ ] `./build/marrow_timeline_graph_model_tests`
- [ ] `ctest --test-dir build --output-on-failure`
- [ ] `git diff --check` and `git status --short`; the diff touches only
      `CMakeLists.txt`, the two new files, and the test file.

---

### Task 2: `.marrow` storage — the two optional keyframe fields

Depends on Task 1. Adds the data model and its round trip. No authoring
primitive, no resolver call, no UI.

**TDD step**

- [ ] Add `validate_mar171_automatic_curves(const ProjectLoadResult&)` to
      `src/samples/editor_project_smoke.cpp`, declared and called beside the
      MAR-169 and MAR-170 validators in `main()`. In this task assert only the
      storage layer:
      - **Default-off**: `serialize_project(*project_result.project)` contains
        neither `"curve_mode"` nor `"curve_driver"`; save it, reload it, and
        assert `serialize_project()` of the reloaded project is byte-identical
        to the first serialization;
      - set `curve_mode = Auto` and `curve_driver = Angle` directly on a
        `TransformKeyframeEdit` of a `ProjectData` copy, serialize, and assert
        the text contains `"curve_mode"` and `"curve_driver"` exactly once each;
      - save and reload that project and assert the two members survive;
      - assert a `SlotColorKeyframeEdit` with `Auto` + `Alpha` round-trips;
      - assert a manual key with a non-default `curve_driver` in memory
        serializes **neither** field;
      - build six malformed documents in a temp file and assert each is rejected
        with the expected message and JSON path: `"curve_mode": 3`,
        `"curve_mode": "automatic"`, `"curve_driver": 7`,
        `"curve_driver": "z"`, `"curve_driver": "x"` on a `rotate` keyframe, and
        `"curve_driver": "angle"` with no `curve_mode`;
      - build a `ProjectData` in memory with an `Auto` Slot Color key whose
        driver is `Angle` and assert `validate_project()` rejects it with
        `automatic curve drivers must name a component the timeline owns`.
- [ ] Build and run
      `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`.
      **It must fail to compile** (`TimelineCurveMode` does not exist). Record it.

**Implementation step**

- [ ] In `include/marrow/editor/project.hpp`:
      - move `enum class TimelineScalarComponent : std::uint8_t` verbatim from
        `include/marrow/editor/authoring.hpp`, preserving the name, the seven
        enumerators, their order, the underlying type, and the doc comment;
      - add `enum class TimelineCurveMode : std::uint8_t { Manual, Auto };` with
        the doc comment from spec §11.1;
      - add `curve_mode` and `curve_driver` to `TransformKeyframeEdit` and
        `SlotColorKeyframeEdit` exactly as spec §8.1 shows, with the struct
        defaults `Manual` / `Angle` and `Manual` / `Red`.
- [ ] In `include/marrow/editor/authoring.hpp`, delete the relocated enum and
      leave a one-line comment naming its new home. **Do not change
      `offset_keyframe_scalars()`'s signature.**
- [ ] In `src/editor/authoring.cpp`, add the six helpers from spec §11.3:
      `curve_mode_token()`, `curve_mode_from_token()`, `curve_driver_token()`,
      `curve_driver_from_token()`, `default_curve_driver()`, and
      `curve_driver_is_authorable()`. `curve_driver_is_authorable()` must be
      implemented with the same family/component switch
      `read_scalar_component()` uses, so the two can never disagree; refactor
      the shared table into one file-local helper if that is cleaner, but change
      no observable behaviour of `offset_keyframe_scalars()`.
- [ ] In `src/editor/project.cpp`:
      - add a file-local `parse_curve_intent(document, keyframe_value,
        keyframe_path, kind, channel, mode_out, driver_out)` implementing spec
        §8.4's six rules, using `find_optional_member()` and
        `validation_error()` with the keyframe-scoped path;
      - call it from `parse_transform_keyframes()` (which knows the channel) and
        from the slot-colour keyframe parser, immediately after the existing
        `parse_interpolation()` call;
      - in `build_transform_keyframes_value()` and
        `build_slot_color_keyframes_value()`, after the existing
        `emplace("curve", ...)`, emit
        `curve_mode` and `curve_driver` **only when
        `keyframe.curve_mode == TimelineCurveMode::Auto`**;
      - in `validate_project()`, beside the existing `snap` block, add the
        driver-authorability loop over both families;
      - **change nothing** in `build_runtime_document()`,
        `build_runtime_slot_color_keyframes_value()`, or any other
        `build_runtime_*` function.
- [ ] Fix any include or using-declaration fallout from the enum relocation.
      There should be none, because `authoring.hpp` includes `project.hpp`; if a
      translation unit breaks, it was including `authoring.hpp` for the enum
      alone and should now include `project.hpp` instead.

**Verification**

- [ ] `cmake --build build -j10`
- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` — the new
      validator passes.
- [ ] `./build/marrow_project_smoke --create /tmp/player_idle.marrow`
- [ ] `./build/marrow_parameter_project_smoke assets/fixtures/parameter_face_basic.marrow`
- [ ] `python3 -m json.tool assets/fixtures/player_idle.marrow > /dev/null`
- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- [ ] `ctest --test-dir build --output-on-failure`
- [ ] `git diff --stat -- src/runtime include/marrow/runtime include/marrow/c_api src/c_api`
      must be **empty**.
- [ ] `git diff -- src/editor/project.cpp | rg 'build_runtime'` must be **empty**.
- [ ] `git diff --check`, `git status --short`.

---

### Task 3: The two authoring primitives and the demotion rule

Depends on Task 2. Adds `set_keyframe_curve_mode()`,
`resolve_automatic_curves()`, and the `set_keyframe_interpolation()` demotion.
Still no UI and no agent.

**TDD step**

- [ ] Extend `validate_mar171_automatic_curves()` with the spec §18.2 cases that
      exercise the primitives:
      - round trip: `set_keyframe_curve_mode()` on `spine` rotate keys 0 and 1
        with driver `Angle` stores exactly `{1/3, 1/3, 2/3, 1}` and
        `{1/3, 0, 2/3, 2/3}` after `float32` narrowing; save, reload, and assert
        mode, driver, and all four control points survive bitwise;
      - the last key of that track, set to `Auto`, keeps its stored easing
        byte-identical while recording the mode;
      - Deform, Draw Order, Event, and Slot Attachment selectors are each
        rejected atomically with `serialize_project()` byte-identical;
      - duplicate selector, unresolvable selector, and empty selector list are
        each rejected atomically;
      - a driver not authorable on the family is rejected atomically;
      - segment-wide identity: `Auto` with driver `Y` on a Translate key leaves
        exactly one `interpolation` field in the whole project different from
        the snapshot, and both `x` and `y` read through it; repeating with
        driver `X` produces a different curve that is still one shared field;
      - neighbour recomputation: `retime_keyframes()` on key 1 followed by
        `resolve_automatic_curves()` changes both neighbouring segments to the
        values §6.3 predicts for the new spacing, and
        `offset_keyframe_scalars()` on key 1 followed by the resolver does the
        same;
      - manual conversion: `set_keyframe_interpolation()` on an auto key sets
        `curve_mode == Manual`, reports `changed == true` **even when the four
        control points are byte-identical**, and a following resolver call
        reports `resolved_key_count == 0` with a byte-identical project;
      - no-change: `set_keyframe_curve_mode(Auto, Angle)` twice reports
        `changed == false` and a byte-identical project the second time;
      - re-resolve: perturb a neighbour without resolving, then
        `set_keyframe_curve_mode(Auto, Angle)` on an already-auto key reports
        `changed_key_count == 0`, `resolved_key_count > 0`, and `changed`;
      - zero-duration rejection: a two-key track `1e-7` s apart with one auto key
        makes the resolver reject atomically with the project unchanged;
      - `resolve_automatic_curves(project, {})` on a project with no auto key
        reports `auto_key_count == 0`, `resolved_key_count == 0`, and a
        byte-identical project;
      - the stale-pair cases of spec §18.3: a hand-written project with
        `curve_mode: "auto"` beside a curve the resolver would not produce loads
        with no error, serializes byte-identically to its input, and is
        reconciled only by an explicit `set_keyframe_curve_mode(Auto, ...)`;
      - the duration no-op of spec §18.3.
- [ ] Build and run the project smoke. **It must fail to compile.** Record it.

**Implementation step**

- [ ] In `include/marrow/editor/authoring.hpp`, append `TimelineCurveModeResult`,
      `TimelineAutoCurveResult`, `set_keyframe_curve_mode()`, and
      `resolve_automatic_curves()` exactly as spec §11.3 declares them, with
      their doc comments.
- [ ] In `src/editor/authoring.cpp`:
      - add file-local `read_key_curve_mode()` / `write_key_curve_mode()` and
        `read_key_curve_driver()` / `write_key_curve_driver()` mirroring the
        existing `read_key_interpolation()` / `write_key_interpolation()` shape,
        returning `nullptr` / doing nothing for the four unsupported families;
      - add a file-local `build_driver_samples(track, driver)` producing
        `std::vector<curve_auto::Sample>` for a Transform or Slot Color timeline
        edit;
      - implement `resolve_automatic_curves()` exactly as spec §10 orders it:
        candidate copy, skip tracks with no `Auto` key untouched, group by
        driver, call `curve_auto::segment_control_points()` once per distinct
        driver, reject the whole call on `std::nullopt` with an error naming the
        animation, the track, and the segment index, write with
        `write_key_interpolation()` (**not** `set_keyframe_interpolation()`),
        count real changes with `same_interpolation()`, single move-assign;
      - implement `set_keyframe_curve_mode()` exactly as spec §10 orders it,
        ending with an internal `resolve_automatic_curves()` over every
        animation the selectors name;
      - add the demotion to `set_keyframe_interpolation()`: every key it writes
        gets `curve_mode = Manual`, and `changed_key_count` counts a key whose
        mode changed even when `same_interpolation()` is true. Update its doc
        comment in `authoring.hpp` to state the side effect.
- [ ] Grep for existing MAR-169 assertions that a byte-identical
      `set_keyframe_interpolation()` rewrite reports `changed == false` and
      confirm each still holds — they operate on manual keys, so they must.
      Report any that do not.

**Verification**

- [ ] `cmake --build build -j10`
- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- [ ] `./build/marrow_project_smoke --create /tmp/player_idle.marrow`
- [ ] `./build/marrow_timeline_model_tests`, `./build/marrow_timeline_graph_model_tests`
- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- [ ] `./build/marrow_agent_dispatch_smoke` — still exactly 57 operations.
- [ ] `ctest --test-dir build --output-on-failure`
- [ ] `git diff --stat -- src/runtime include/marrow/runtime include/marrow/c_api src/c_api` empty.
- [ ] `git diff --check`, `git status --short`.

---

### Task 4: Export neutrality, proved on a mutated project

Depends on Task 3. **This task exists because the same defect escaped review
twice.** MAR-168 exported the mutated project and reported `JSON 14338 bytes`;
MAR-169 exported only the untouched baseline and reported `14336`, identical to
MAR-167's and MAR-168's baseline, so no authored curve was ever proven to reach
a runtime file even though its criterion named export. Do not repeat it.

**TDD step**

- [ ] Add an export block to `validate_mar171_automatic_curves()` that operates
      on the **already-mutated** `ProjectData`, not on
      `*project_result.project`. Assert, in this order:
      1. author `Auto` with driver `Angle` on `spine` rotate keys 0 and 1 and on
         `arm_l` rotate key 0 (whose stored `curve` is the string `"linear"` in
         the fixture today);
      2. `export_runtime_assets()` that project to
         `/tmp/marrow_mar171_auto.mskl` and `/tmp/marrow_mar171_auto.mbin`;
      3. `runtime::load_skeleton_data("/tmp/marrow_mar171_auto.mskl")` and assert
         the `spine` rotate timeline's keyframe 0 easing is `CubicBezier` with
         `cx1 == static_cast<AnimationScalar>(1.0/3.0)`,
         `cy1 == static_cast<AnimationScalar>(1.0/3.0)`,
         `cx2 == static_cast<AnimationScalar>(2.0/3.0)`, and `cy2 == 1.0f`;
      4. assert the `arm_l` rotate timeline's keyframe 0 easing is
         `CubicBezier`, not `Linear` — the string-to-array conversion;
      5. read `/tmp/marrow_mar171_auto.mskl` as **text** and assert
         `find("curve_mode") == npos` and `find("curve_driver") == npos`;
      6. `validate_binary_export()` on the `.mskl`/`.mbin` pair;
      7. assert `std::filesystem::file_size(auto_json)` is **strictly greater
         than** the untouched baseline export's size, computed in the same run
         rather than hardcoded;
      8. print
         `MAR-171 auto export: JSON <n> bytes, MBIN <m> bytes (baseline JSON <b> bytes).`
- [ ] Build and run the project smoke. Assertions 3, 4, and 5 must **fail first**
      if any of them is wrong; verify by temporarily pointing the export at the
      untouched baseline project and confirming assertions 3 and 4 fail. Restore
      the correct project afterwards. Record both outputs.

**Implementation step**

- [ ] No production code should be needed: the export path is untouched by
      design (spec §8.6). If assertion 5 fails, a `build_runtime_*` function was
      edited in Task 2 — revert that edit rather than filtering the field out at
      export time.
- [ ] If assertion 7 fails because the size is equal, treat it as the defect
      recurring: confirm the exported project is the mutated one and that
      assertions 3 and 4 really ran.

**Verification**

- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- [ ] ```bash
      ./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
        --export-runtime /tmp/marrow_mar171.mskl --export-binary /tmp/marrow_mar171.mbin
      ./build/marrow_inspect --compare /tmp/marrow_mar171.mbin /tmp/marrow_mar171.mskl
      ./build/marrow_fixture_smoke /tmp/marrow_mar171.mskl /tmp/player_idle.matl
      rg -c 'curve_mode|curve_driver' /tmp/marrow_mar171_auto.mskl || echo "OK: no project-local field in the export"
      wc -c /tmp/marrow_mar171_auto.mskl /tmp/marrow_mar171_baseline.mskl
      ```
- [ ] Record the printed `MAR-171 auto export:` line verbatim; it goes into
      `AGENTS.md` in Task 11 and is the reviewer's evidence.
- [ ] `git diff --check`, `git status --short`.

---

### Task 5: Shell controller — application, resolution triggers, and demotion

Depends on Task 4. Adds `apply_timeline_curve_mode()`,
`resolve_timeline_auto_curves()`, the readout accessors, and the resolver call
at every trigger. Still no ImGui.

**TDD step**

- [ ] Add `validate_timeline_curve_mode_shell_smoke()` to
      `src/editor/shell_smoke_graph.cpp`, declared in
      `src/editor/shell_smoke_scenarios.hpp` and called from
      `run_headless_smoke()` in `src/editor/shell_smoke.cpp` after MAR-170's
      scenario. Build an isolated `ShellState` exactly as
      `validate_timeline_graph_easing_shell_smoke()` does. In this task assert
      the controller-level behaviour of spec §18.4:
      - three `spine` Rotate keys set to `Auto` with driver `Angle`: one history
        entry, `changed_key_count == 3`, every `time` and `angle`
        byte-identical, every resolved curve equal to the §6.6 table;
      - a mixed Transform/Slot Color/Event selection skips the Event key,
        `skipped_key_count == 1`, still one history entry;
      - a Deform-only selection opens no transaction and leaves `undo_count()`
        unchanged;
      - re-applying the same mode and driver reports `applied == false` with no
        history entry and byte-identical project bytes;
      - the four recomputation triggers, each **one** history entry with the
        auto curves updated inside it: a MAR-168 value drag on a neighbour, a
        dopesheet retime of a neighbour, `add_timeline_key_at_playhead()`
        between two auto keys, and `remove_selected_timeline_keys()` on a middle
        key;
      - paste: copy an auto key, paste it, and assert the pasted key carries
        `Auto` plus its driver, that the paste is one history entry, and that
        its segment is resolved against its **new** neighbours;
      - the MAR-169 drag demotion: with the Graph displaying component Y on a
        Translate track of auto keys driven by X, one handle drag gives one
        history entry, `Manual` on the dragged key **only**, an identically
        changed X segment, and untouched neighbouring auto keys;
      - drag away and exactly back on an **auto** key: one history entry,
        `Manual`, byte-identical control points;
      - drag away and exactly back on a **manual** key: zero history entries;
      - a MAR-170 preset applied to an auto key: one history entry, `Manual`,
        the preset's exact constants stored;
      - undo then redo restoring mode, driver, and every resolved curve with
        `selected_keys` and `active_key` bit-identical;
      - `cancel_authoring_gestures()` during an auto-key handle drag restoring
        `serialize_project()`, `undo_count()`, `redo_count()`,
        `project_revision()`, `dirty()`, the rebuilt dopesheet `key_times`,
        every rebuilt graph `Key::values`, every rebuilt `Segment::kind`, every
        rebuilt `outgoing_easing`, and the auto mode;
      - materialization: the first `Auto` application to the runtime-only
        `slot:0:Color` track copies all its keys into the project rather than
        replacing the track, and resolves against the copied keys;
      - a track injected with a zero-duration segment makes the application fail
        with the project byte-identical and no history entry;
      - `agent_operation_descriptor_count()` equals the value captured at the
        top of the scenario, before and after every case. Use `57U` in this
        task; Task 7 changes it to `58U`.
- [ ] Build and run
      `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`.
      **It must fail to compile.** Record it.

**Implementation step**

- [ ] In `src/editor/shell_state.hpp`, add the `original_mode` member to
      `TimelineGraphHandleGesture` (spec §11.4), placed beside
      `original_control_points`. Add nothing to `authoring_gesture_active()` and
      nothing to `cancel_authoring_gestures()` — MAR-171 owns no gesture.
- [ ] In `src/editor/timeline_controller.hpp/.cpp`:
      - add `TimelineCurveModeApplyResult`, `apply_timeline_curve_mode()`,
        `resolve_timeline_auto_curves()`, `active_outgoing_curve_mode()`, and
        `active_outgoing_curve_driver()` exactly as spec §11.4 declares them;
      - `apply_timeline_curve_mode()` follows MAR-170's
        `apply_timeline_curve_preset()` shape: resolve the selection in
        selection order, skip incompatible and unresolvable refs into
        `skipped_key_count`, de-duplicate over the full `TimelineKeySelector`,
        materialize every track inside the transaction, call
        `set_keyframe_curve_mode()`, cancel on error or on `!changed`,
        `refresh_runtime()`, commit;
      - the transaction is `EditKind::EditProperty`, label
        `"Set automatic curve"` / `"Set manual curve"` with plural variants,
        group `"timeline:curve-mode"`, `allow_merge = false`, impact
        `Project | Runtime | Preview`;
      - `resolve_timeline_auto_curves()` calls
        `marrow::editor::resolve_automatic_curves(state->session.project(),
        animation)` on the caller's already-open transaction project and returns
        false with `*error_out` on failure;
      - insert the resolver call at every shell trigger in spec §9.2's table,
        after the primary mutation and before `refresh_runtime()`, plus any
        additional site Task 0 discovered;
      - in `begin_timeline_graph_handle_gesture()`, capture the pressed key's
        `curve_mode` into `gesture.original_mode`;
      - in `apply_timeline_graph_handle_control_points()`, extend the `changed`
        rule to
        `changed = (applied != original_control_points) || original_mode == Auto`,
        with a comment citing spec §9.4.
- [ ] If Task 0 found that `shell_timeline.cpp`'s numeric interpolation fields
      assign `interpolation` directly, add the one-line
      `curve_mode = TimelineCurveMode::Manual` there and nothing else. Do not
      refactor that inspector.

**Verification**

- [ ] `cmake --build build -j10`
- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- [ ] `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2`
- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- [ ] `./build/marrow_timeline_model_tests`, `./build/marrow_timeline_graph_model_tests`
- [ ] `./build/marrow_agent_dispatch_smoke` — still 57.
- [ ] `ctest --test-dir build --output-on-failure`
- [ ] `git diff --check`, `git status --short`.

---

### Task 6: ImGui wiring and render statistics

Depends on Task 5. Adds the Graph toolbar row, the auto handle colour, the
extended readout, and the render stats. Dopesheet is deliberately untouched
(spec §17.16).

**TDD step**

- [ ] Add the spec §18.6 actual-frame cases to
      `src/editor/shell_smoke_frames.cpp`, following the MAR-169 handle-drag
      block's structure (`render_graph_frame` / `click_graph_item`):
      - `curve_mode_row_drawn` is true with finite `first_curve_mode_min_x/y`,
        and MAR-167/168/169/170's `fit_*`, `first_component_*`,
        `first_point_*`, `first_handle_*`, and `first_preset_*` rectangles are
        still reported **at their previous coordinates** — capture them before
        the row exists is impossible, so assert instead that the plot rectangle
        still lies inside the timeline window and that every previously asserted
        rectangle is still finite and hoverable;
      - clicking the reported `Auto` button with keys selected adds one history
        entry, sets `active_key_auto`, and stores the expected curve;
      - with nothing selected, `curve_mode_row_enabled` is false and a click
        changes nothing;
      - the row and the driver combo are inert while a MAR-169 handle drag is
        live;
      - pressing the reported `first_handle_x/y` on an auto key, moving 30 px,
        and releasing makes `active_key_auto` false in the next frame's stats
        and adds exactly one history entry.
- [ ] Build and run the shell smoke. **It must fail to compile.** Record it.

**Implementation step**

- [ ] In `src/editor/shell_timeline_graph.hpp`, append the six render-stats
      members from spec §11.6 to `TimelineGraphRenderStats`.
- [ ] In `src/editor/shell_timeline_graph.cpp`:
      - draw the `Curve mode:` row immediately after MAR-170's preset row and
        `Default:` combo, so no existing widget moves;
      - `Manual` and `Auto` `SmallButton`s calling `apply_timeline_curve_mode()`;
      - a `Driver:` combo listing exactly the components the selection's
        families own, disabled with its own tooltip when the selection spans
        families with disjoint component sets;
      - wrap the whole row in `BeginDisabled()/EndDisabled()` when the
        compatible selection is empty or `authoring_gesture_active()`;
      - draw an auto key's handles as **hollow** squares in
        `IM_COL32(0xf0, 0xc0, 0x60, 0xff)` with tangent lines in the same hue at
        `0x80`, keeping the existing filled light-blue style for manual keys;
      - extend the `Outgoing:` readout with the mode clause from spec §12,
        composing on top of MAR-170's preset name;
      - add the sentence to the shared-easing notice;
      - add the status messages from spec §12;
      - publish the new render stats from the submitted rectangles.
- [ ] No Fritsch–Carlson arithmetic, no token parsing, and no project mutation
      may appear in this file. Verify with
      `rg -n '1\.0 ?/ ?3\.0|2\.0 ?/ ?3\.0|hypot|"auto"|"manual"' src/editor/shell_timeline_graph.cpp`
      — the only acceptable hits are display strings routed through
      `curve_mode_token()` or a display-name helper.

**Verification**

- [ ] `cmake --build build -j10`
- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- [ ] `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2`
- [ ] `./build/marrow_editor_shell --verify-launch-focus`
- [ ] `ctest --test-dir build --output-on-failure`
- [ ] `git diff --check`, `git status --short`.

---

### Task 7: The Agent operation

Depends on Task 6. Takes the registry from 57 to 58. **This is the first task in
which any `57` literal may change.**

**TDD step**

- [ ] In `src/samples/agent_dispatch_smoke.cpp`:
      - change `std::array<OperationExpectation, 57>` to `58`;
      - insert `{"timeline.set_curve_mode", "edit", true, false, true},`
        immediately after the `timeline.set_interpolation` row — the order is
        part of the contract that `expect_registry_contract()` checks index by
        index;
      - add the spec §18.7 behaviour cases in a new braced scope after the
        MAR-169 interpolation block, including at least one successful
        invocation (`expect_complete_coverage()` fails on an uninvoked row).
- [ ] Update the three `57U` guards and their messages in
      `src/editor/shell_smoke_graph.cpp` (lines around 143, 631, 1542) plus the
      one in the Task 5 scenario, to `58U`.
- [ ] Build and run `./build/marrow_agent_dispatch_smoke`. **It must fail**
      with a registry-size mismatch. Record it.

**Implementation step**

- [ ] In `src/editor/agent_dispatch_internal.hpp`, declare
      `curve_mode_request_arg()` with the doc comment from spec §13.2.
- [ ] In `src/editor/agent_dispatch.cpp`:
      - implement `curve_mode_request_arg()`: a missing or non-string `mode` is
        an error; only `"manual"` and `"auto"` are accepted; `driver` is
        optional, must be one of the seven tokens, and is an error when supplied
        with `"manual"`. It returns the enum plus an optional driver and
        constructs nothing;
      - add
        `{"timeline.set_curve_mode", "edit", true, false, true, true, &handle_editing_operation},`
        to `kOperationSpecs[]` immediately after the
        `timeline.set_interpolation` row;
      - leave `interpolation_request_arg()` and `interpolation_arg()`
        byte-identical.
- [ ] In `src/editor/agent_handlers_editing.cpp`:
      - add the `if (op == "timeline.set_curve_mode") { ... }` block modelled on
        the `timeline.set_interpolation` block: the same `args`/`keys`/4096
        guards, the same per-key parse loop **minus the `deform` branch** (which
        joins the rejected kinds with a message naming the reason), the same
        `materialize` lambda restricted to the two supported families, a
        `collect_previous` that captures mode, driver, and curve per key, the
        same 256-entry echo cap with `keys_truncated`, the same dry-run and live
        shapes, and `CommitPolicy{"Failed to set timeline curve mode: "}`;
      - the response payload is exactly spec §13.4;
      - **do not copy the dead `apply` lambda** that the
        `timeline.set_interpolation` block defines and never calls;
      - add `resolve_automatic_curves(project, {})` to the six existing blocks
        in spec §9.2's agent table, after each one's own mutation and before its
        commit, cancelling the transaction on a resolver error.
- [ ] Confirm `agent_operation_descriptor_count()` now returns 58 without any
      hand-edited count in `agent_dispatch.cpp` — it derives from
      `std::size(kOperationSpecs)`.

**Verification**

- [ ] `cmake --build build -j10`
- [ ] `./build/marrow_agent_dispatch_smoke` — `agent_dispatch_smoke: PASSED`
      against the exact 58-operation registry.
- [ ] `./build/marrow_agent_socket_tests`
- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- [ ] `ctest --test-dir build --output-on-failure`
- [ ] `rg -n '\b57\b' --glob '!build*' src tools` — every remaining hit must be a
      colour literal or a Task 0 false positive.
- [ ] `git diff --check`, `git status --short`.

---

### Task 8: The MCP tool

Depends on Task 7.

**TDD step**

- [ ] In `tools/mcp/test_client.py`:
      - change `assert len(registry_names) == 57` and
        `assert len(mcp_names) == 57` to `58`;
      - add `"timeline.set_curve_mode"` to `new_edit_operations`;
      - add the explicit registry metadata row assertion beside the
        `timeline.set_interpolation` one;
      - add the spec §18.8 sequence.
- [ ] Start the shell with `--agent-port 9876` and run the client. **It must
      fail** on the count assertion or on the missing tool. Record it.

**Implementation step**

- [ ] In `tools/mcp/tools/editing.py`:
      - add `_timeline_curve_mode_key_schema()`, a `oneOf` over `transform` and
        `slot_color` only, modelled on `_timeline_interpolation_key_schema()`
        with the `deform` branch removed;
      - add one `types.Tool(name="timeline.set_curve_mode", ...)` immediately
        after `timeline.set_interpolation`, with the `mode` and `driver` enums
        and `"required": ["keys", "mode"]` from spec §13.6, and a description
        stating that automatic curves are recomputed when a neighbour moves,
        that they never overshoot, and that dragging a handle switches the
        segment back to manual;
      - leave `_interpolation_schema()`, `_bezier_interpolation_schema()`, and
        `_timeline_interpolation_key_schema()` unchanged, and add or remove no
        other tool.

**Verification**

- [ ] `tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py`
- [ ] ```bash
      ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
      tools/mcp/venv/bin/python tools/mcp/test_client.py
      ```
      -> `mcp test_client: PASSED` with 58/58 exact C++/Python name parity.
- [ ] `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only`
      against `parameter_face_basic.marrow`.
- [ ] `git diff --check`, `git status --short`.

---

### Task 9: Full validation, documentation, and MAR-171 closure

Depends on Task 8. Changes no behaviour.

**Documentation step**

- [ ] `AGENTS.md`:
      - update `Current Validation`'s registry line to
        `Agent registry validation (58 operations, including parameter,
        animation-duration, timeline-interpolation, and timeline-curve-mode
        authoring)`;
      - update `Project State` so MAR-122–128 and MAR-154–171 are complete and
        MAR-172 is the next product milestone depending on MAR-171;
      - add a `MAR-171 Project-Local Automatic Curve Handles Validation Results`
        section in the existing table-plus-command-output format, above the
        MAR-170 section, whose table rows cover: the algorithm and its
        `cx = 1/3, 2/3` invariant; monotonicity, flat and repeated values, and
        the absence of overshoot; the additive default-off `.marrow` fields and
        their strict validation; same-transaction recomputation and undo
        granularity; the handle-drag and preset demotion; Agent/MCP parity at
        58; and export neutrality. **Quote the Task 4 export line verbatim,
        including the baseline size**, so a reader can see the exported artifact
        is not the untouched baseline;
      - leave every dated `Validation Results` section, checkpoint, and
        historical parity row at the number it recorded.
- [ ] `docs/root1/format-spec.md`: add the `curve_mode` / `curve_driver`
      paragraph under `.marrow` → `timeline_edits` per spec §19, and append the
      `cx1 = 1/3, cx2 = 2/3` clause to the existing `cx in [0, 1]` paragraph.
      **No new field in `.mskl`, `.mbin`, or `editor-settings.json`, and no
      version change anywhere.**
- [ ] `docs/root1/discription.md`: add a MAR-171 paragraph in the existing
      Korean style beside the MAR-169 one, and update the milestone-sequence
      sentences at lines 50, 878, and 881 so MAR-171 is complete and MAR-172 is
      next. Leave line 49's MAR-169 paragraph, including its `57번째`, unchanged.
- [ ] `docs/root1/editing-gap-analysis.md`: update lines 23, 84 (57→58 and
      편집 32→33), 85, 191, and 454 to the current surface size; update lines 47,
      92, 223, 279, and 287 so MAR-171 is complete and MAR-172 is next; leave
      line 391's MAR-169 row unchanged; extend the MAR-171 row at line 393 with
      the as-built decisions.
- [ ] `docs/root1/refector.md`: update lines 20 and 112 to 58, line 103's range
      to begin at MAR-172, line 105's completion list, and line 333's
      next-milestone sentence.
- [ ] `docs/root1/quick-start.md` line 99 and `docs/root1/platform-validation.md`
      line 20: replace the MAR-170 forward reference with MAR-172, and note that
      automatic curves never overshoot while manual handle drags may.
- [ ] `docs/root1/concepts.md`: add one sentence that curve mode is project-local
      authoring intent, that the runtime and both runtime formats only ever see
      the resolved easing, and that an automatic curve is recomputed inside the
      transaction that moved its neighbours.
- [ ] `.agents/tasks/prd-marrow-runtime.json`: set `MAR-171`'s `status` to
      `done` with the verified `completedAt` date. Leave `MAR-172` open and
      leave MAR-192–210 open.
- [ ] Change the design spec's status line to `Implemented and validated
      (<date>)`.
- [ ] Re-run both sweeps and classify every hit:
      ```bash
      rg -n '\b57\b' --glob '!build*' src tools AGENTS.md docs .agents
      rg -n '\b58\b' --glob '!build*' src tools AGENTS.md docs .agents
      ```

**Verification** — the full checklist below, every command run and its output
recorded.

---

## Full verification checklist

Run in order. Record the exact output of each; `AGENTS.md`'s MAR-171 section
quotes them.

**Build**

- [ ] `cmake -S . -B build`
- [ ] `cmake --build build -j10`
- [ ] `cmake --build build --target marrow_verify_third_party`
- [ ] `cmake --build build --target marrow_constraint_warning_check`

**Focused unit tests**

- [ ] `./build/marrow_timeline_model_tests` — includes the new automatic-curve
      case; record the case count.
- [ ] `./build/marrow_timeline_graph_model_tests`
- [ ] `./build/marrow_preference_tests` — must be unchanged; MAR-171 touches no
      preference code.
- [ ] `./build/marrow_selection_tests`
- [ ] `./build/marrow_viewport_interaction_tests`
- [ ] `./build/marrow_windowing_tests`, `./build/marrow_pen_input_tests`
- [ ] `./build/marrow_unit_tests`

**Project domain**

- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` — record
      the `MAR-171 automatic curve` line and the `MAR-171 auto export:` line.
- [ ] `./build/marrow_project_smoke --create /tmp/player_idle.marrow`
- [ ] `./build/marrow_parameter_project_smoke assets/fixtures/parameter_face_basic.marrow`
- [ ] `python3 -m json.tool assets/fixtures/player_idle.marrow > /dev/null`
- [ ] `python3 -m json.tool assets/fixtures/parameter_face_basic.marrow > /dev/null`

**Export — the criterion this story must not fake**

- [ ] ```bash
      ./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
        --export-runtime /tmp/marrow_mar171.mskl --export-binary /tmp/marrow_mar171.mbin
      ```
- [ ] `./build/marrow_inspect --compare /tmp/marrow_mar171.mbin /tmp/marrow_mar171.mskl`
- [ ] `./build/marrow_fixture_smoke /tmp/marrow_mar171.mskl /tmp/player_idle.matl`
- [ ] `rg -c 'curve_mode|curve_driver' /tmp/marrow_mar171_auto.mskl` — **must find
      nothing**; a non-zero count is a hard failure.
- [ ] `wc -c /tmp/marrow_mar171_auto.mskl /tmp/marrow_mar171_baseline.mskl` — the
      auto export must be **strictly larger**. An equal size means the exported
      artifact is the untouched baseline, which is the MAR-169 defect recurring;
      stop and fix rather than recording the number.
- [ ] `./build/marrow_inspect /tmp/marrow_mar171_auto.mskl`
- [ ] `python3 -m json.tool /tmp/marrow_mar171_auto.mskl > /dev/null`

**Shell**

- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
      — includes the new `validate_timeline_curve_mode_shell_smoke` scenario and
      the actual-frame curve-mode and auto-handle cases.
- [ ] `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2`
- [ ] `./build/marrow_editor_shell --verify-launch-focus`

**Agent and MCP**

- [ ] `./build/marrow_agent_dispatch_smoke` — exact 58-operation registry.
- [ ] `./build/marrow_agent_socket_tests`
- [ ] `./build/marrow_c_smoke`
- [ ] `tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py`
- [ ] `tools/mcp/venv/bin/python tools/mcp/test_client.py` against
      `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876`
      — 58/58 parity.
- [ ] `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only`
      against `parameter_face_basic.marrow`.

**CTest**

- [ ] `ctest --test-dir build -N`
- [ ] `ctest --test-dir build --output-on-failure`
- [ ] `ctest --test-dir build --output-on-failure -L runtime`
- [ ] `ctest --test-dir build --output-on-failure -L editor`
- [ ] `ctest --test-dir build --output-on-failure -R marrow.renderer_link_boundary`

**Display and release suites (automated evidence only)**

- [ ] `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON`
- [ ] `cmake --build build-display -j10`
- [ ] `ctest --test-dir build-display --output-on-failure`
- [ ] `ctest --test-dir build-display --output-on-failure -L windowing`
- [ ] `ctest --test-dir build-display --output-on-failure -L display`
- [ ] `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON`
- [ ] `cmake --build build-platform-release -j10`
- [ ] `ctest --test-dir build-platform-release --output-on-failure`

**Compatibility gates — all must be empty or unchanged**

- [ ] `git diff --stat -- include/marrow/c_api src/c_api` — empty.
- [ ] `git diff --stat -- src/runtime include/marrow/runtime` — empty.
- [ ] `git diff --stat -- src/editor/preferences.cpp include/marrow/editor/preferences.hpp`
      — empty.
- [ ] `git diff -- src/editor/project.cpp | rg 'build_runtime'` — empty.
- [ ] `git diff --stat -- src/tests/preference_store_tests.cpp` — empty unless a
      relocated-enum include was required.
- [ ] `./build/marrow_inspect assets/fixtures/player_idle.mskl` and
      `./build/marrow_inspect assets/fixtures/player_idle.mbin` — unchanged
      versions (`.mskl` v1, `.mbin` v2).
- [ ] `git diff --check`
- [ ] `git status --short` — only the files this plan names.
- [ ] `git lfs status` — no LFS object staged or queued to push.

**Documentation**

- [ ] `AGENTS.md` MAR-171 section added; `Current Validation` registry line is
      58; `Project State` names MAR-172 next.
- [ ] `docs/root1/format-spec.md`, `discription.md`, `editing-gap-analysis.md`,
      `refector.md`, `quick-start.md`, `platform-validation.md`, and
      `concepts.md` updated per Task 9, with every historical number left alone.
- [ ] `.agents/tasks/prd-marrow-runtime.json` — MAR-171 `done` with the verified
      date; MAR-172 open; MAR-192–210 open.
- [ ] `python3 -c "import json;json.load(open('.agents/tasks/prd-marrow-runtime.json'))"`
- [ ] The design spec's status line reads `Implemented and validated (<date>)`.
- [ ] The MAR-171 section states explicitly that the display suites are automated
      evidence only and add no manual-visible-UI, Windows 11, physical-input, or
      platform qualification credit.

## Decisions Taken Under Ambiguity

The design spec's §17 is authoritative and is not repeated here. Two decisions
belong to the plan rather than the design:

1. **The pure math gets its own translation unit and its own CMake line, rather
   than living in `authoring.cpp`.** `authoring.cpp` is already over 2000 lines,
   and a `ProjectData`-free module is what lets `marrow_timeline_model_tests`
   assert monotonicity, scale invariance, and the format invariant without
   constructing a project. The cost is one line in `CMakeLists.txt`.
2. **The registry stays at 57 through Task 6 and becomes 58 in Task 7.**
   Splitting the count change into its own task keeps every earlier task's
   regression run comparable against the Task 0 baseline, and makes the
   `57` → `58` sweep a single reviewable diff rather than noise spread across
   nine tasks.
