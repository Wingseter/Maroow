# MAR-168 Graph Key Time and Scalar Value Editing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make graph points draggable so a vertical drag edits the active scalar
component and a horizontal drag retimes the whole parent key, both through the
existing shared authoring primitives, with one transaction per drag, live
preview, atomic cancel, and zero change to serialization, the C ABI, or the
56-operation Agent/MCP surface.

**Architecture:** Add pure pixel-to-unit and axis-decision math to
`timeline_graph_model`. Add one additive project-domain primitive
`offset_keyframe_scalars()` next to `retime_keyframes()`. Add an ImGui-free
value-gesture trio to `timeline_controller`, and an ImGui-free drag driver to
`shell_timeline_graph` that locks the axis and then delegates to either the
existing dopesheet retime trio or the new value trio. `draw_timeline_graph_body()`
only samples input.

**Tech Stack:** C++17, Dear ImGui, existing Marrow runtime/editor session APIs,
CMake/CTest, JSON fixtures.

**Spec:** `docs/superpowers/specs/2026-08-30-mar-168-graph-key-time-value-editing-design.md`

## Global Constraints

- Read `docs/root1/discription.md` and the MAR-168 PRD story before each
  implementation task; current source and tests override stale plan assumptions.
- Follow strict RED-GREEN-REFACTOR: add one focused failing test, observe the
  expected failure, write the minimum production code, then rerun focused and
  affected regression tests.
- Do not reset, discard, stash, commit, push, or create a PR unless the user
  separately requests it. Each task ends with a read-only
  `git diff --check` / `git status --short` checkpoint.
- Never introduce a parallel key-writing path. Every mutation goes through
  `retime_keyframes()` or `offset_keyframe_scalars()` inside an
  `EditorSession::EditTransaction`.
- Never read or write `Interpolation` anywhere in MAR-168. Easing editing is
  MAR-169.
- Never introduce per-component key identity. One `TimelineKeyRef` per parent
  key, always.
- Preserve `.marrow`, `.mskl` v1, `.mbin` v2, C ABI v1, `SelectionSet`, GPU
  ownership, and the exact 56-operation Agent/MCP registry. The only public
  header change is the additive block in `include/marrow/editor/authoring.hpp`.
- Do not change dopesheet behaviour, including its retime status messages,
  clamping, or post-undo selection reconciliation.
- Automated display gates do not establish manual-visible-UI, Windows 11, or
  physical-input qualification. Keep MAR-192 through MAR-210 open.

## File and Responsibility Map

**Create**

- none. MAR-168 extends existing units.

**Modify**

- `src/editor/timeline_graph_model.hpp/.cpp` — `DragAxis`, pixel-to-unit
  mapping, `decide_drag_axis()`, `drag_time_delta()`, `drag_value_delta()`.
- `src/tests/timeline_graph_model_tests.cpp` — focused drag-math tests.
- `include/marrow/editor/authoring.hpp` — additive
  `TimelineScalarComponent`, `TimelineScalarOffsetResult`,
  `offset_keyframe_scalars()`.
- `src/editor/authoring.cpp` — `offset_keyframe_scalars()` implementation.
- `src/samples/editor_project_smoke.cpp` — lane-family, clamp, atomicity,
  save/reload, and JSON/MBIN export coverage for the new primitive.
- `src/tests/timeline_model_tests.cpp` — shared completion/incremental-delta
  reuse cases.
- `src/editor/shell_state.hpp` — `TimelineGraphPointDrag`,
  `TimelineGraphValueGesture`, two `TimelineEditorState` fields, and the
  `authoring_gesture_active()` update.
- `src/editor/shell_core.cpp` — `cancel_authoring_gestures()` update.
- `src/editor/timeline_controller.hpp/.cpp` — the value-gesture trio.
- `src/editor/shell_timeline_graph.hpp/.cpp` — drag driver, render-stat
  additions, `Snap` checkbox, drag readout, frozen view, removal of the MAR-167
  read-only notice.
- `src/editor/shell_timeline.cpp` — dopesheet retime updater guarded by view
  mode; tab-switch cancel.
- `src/editor/shell_smoke_graph.cpp` — new headless drag scenario.
- `src/editor/shell_smoke_scenarios.hpp`, `src/editor/shell_smoke.cpp` —
  register the new scenario.
- `src/editor/shell_smoke_frames.cpp` — actual-frame drag coverage.
- `AGENTS.md`, `docs/root1/*.md`, the design spec, and
  `.agents/tasks/prd-marrow-runtime.json` — record verified MAR-168 completion
  and make MAR-169 next.

---

### Task 1: UI-free drag math in `timeline_graph_model`

**Files:**

- Modify: `src/editor/timeline_graph_model.hpp`
- Modify: `src/editor/timeline_graph_model.cpp`
- Modify: `src/tests/timeline_graph_model_tests.cpp`

**Interfaces:**

- Consumes: existing `PlotRect`, `View`.
- Produces:

```cpp
enum class DragAxis : std::uint8_t { Undecided, Time, Value };

double time_at_x(PlotRect rect, const View& view, double x);
double value_at_y(PlotRect rect, const View& view, double y);
double x_at_time(PlotRect rect, const View& view, double time_seconds);
double y_at_value(PlotRect rect, const View& view, double value);

DragAxis decide_drag_axis(
    double press_x, double press_y,
    double pointer_x, double pointer_y,
    double dead_zone_pixels = 4.0);

std::optional<double> drag_time_delta(
    const View& view, double press_x, double pointer_x);
std::optional<double> drag_value_delta(
    const View& view, double press_y, double pointer_y);
```

- [ ] **Step 1: Add failing drag-math tests**

In `src/tests/timeline_graph_model_tests.cpp` add
`test_graph_drag_axis_and_unit_mapping(suite)` and register it in `main()`.
It must assert:

```cpp
constexpr graph::PlotRect rect{100.0, 40.0, 700.0, 340.0};
const graph::View view{0.5, 200.0, 10.0, 25.0};

suite.expect(
    near(graph::time_at_x(rect, view, graph::x_at_time(rect, view, 1.25)), 1.25, 1e-9) &&
        near(graph::value_at_y(rect, view, graph::y_at_value(rect, view, -3.5)), -3.5, 1e-9),
    "pixel and unit mapping must round-trip");

suite.expect(
    graph::decide_drag_axis(300.0, 200.0, 302.0, 201.0, 4.0) ==
        graph::DragAxis::Undecided,
    "a move inside the dead zone must not choose an axis");
suite.expect(
    graph::decide_drag_axis(300.0, 200.0, 320.0, 203.0, 4.0) == graph::DragAxis::Time,
    "a dominant horizontal move must lock the time axis");
suite.expect(
    graph::decide_drag_axis(300.0, 200.0, 303.0, 220.0, 4.0) == graph::DragAxis::Value,
    "a dominant vertical move must lock the value axis");
suite.expect(
    graph::decide_drag_axis(300.0, 200.0, 310.0, 210.0, 4.0) == graph::DragAxis::Value,
    "an exact axis tie must resolve to the value axis");
suite.expect(
    graph::decide_drag_axis(
        std::numeric_limits<double>::quiet_NaN(), 200.0, 310.0, 210.0, 4.0) ==
        graph::DragAxis::Undecided,
    "non-finite pointer input must not choose an axis");

const auto time_delta = graph::drag_time_delta(view, 300.0, 400.0);
suite.expect(
    time_delta.has_value() && near(*time_delta, 0.5),
    "time delta must divide the pixel delta by pixels per second");
const auto value_delta = graph::drag_value_delta(view, 300.0, 200.0);
suite.expect(
    value_delta.has_value() && near(*value_delta, 4.0),
    "value delta must invert screen Y and divide by pixels per value");

graph::View degenerate = view;
degenerate.pixels_per_value = 0.0;
suite.expect(
    !graph::drag_value_delta(degenerate, 300.0, 200.0).has_value(),
    "a non-positive value scale must reject the drag delta");
suite.expect(
    !graph::drag_time_delta(
         view, std::numeric_limits<double>::infinity(), 400.0).has_value(),
    "a non-finite press coordinate must reject the drag delta");
```

Also assert that `x_at_time` / `y_at_value` reproduce, within `1e-9`, the point
coordinates the existing `build_geometry()` produces for the same key and view,
proving render and drag math cannot drift.

- [ ] **Step 2: Build and observe RED**

```bash
cmake --build build --target marrow_timeline_graph_model_tests -j4
```

Expected: compilation fails because `DragAxis`, `time_at_x`, `value_at_y`,
`x_at_time`, `y_at_value`, `decide_drag_axis`, `drag_time_delta`, and
`drag_value_delta` do not exist.

- [ ] **Step 3: Implement the pure math**

Add the declarations above. Implement with the exact MAR-167 formulas:

```cpp
double time_at_x(PlotRect rect, const View& view, double x) {
    return view.view_start_seconds + (x - rect.min_x) / view.pixels_per_second;
}
double value_at_y(PlotRect rect, const View& view, double y) {
    return view.value_center +
        (((rect.min_y + rect.max_y) * 0.5) - y) / view.pixels_per_value;
}
double x_at_time(PlotRect rect, const View& view, double time_seconds) {
    return rect.min_x + (time_seconds - view.view_start_seconds) * view.pixels_per_second;
}
double y_at_value(PlotRect rect, const View& view, double value) {
    return ((rect.min_y + rect.max_y) * 0.5) - (value - view.value_center) * view.pixels_per_value;
}
```

`decide_drag_axis()` returns `Undecided` when any argument is non-finite, when
`dead_zone_pixels` is not finite and non-negative, or when
`max(|dx|, |dy|) < dead_zone_pixels`; then `Time` when `|dx| > |dy|`, else
`Value`.

`drag_time_delta()` returns `std::nullopt` when any argument is non-finite,
when `view.pixels_per_second` is not finite and positive, or when the quotient
is non-finite; otherwise `(pointer_x - press_x) / view.pixels_per_second`.
`drag_value_delta()` mirrors it with `pixels_per_value` and
`(press_y - pointer_y)`.

- [ ] **Step 4: Refactor the shell to consume the shared math**

Delete the file-local `value_from_y()` and `y_from_value()` helpers in
`src/editor/shell_timeline_graph.cpp` and route every remaining use — including
the empty-plot scrub in the left-click branch — through
`timeline_graph_model::time_at_x()` and `::value_at_y()`. No behaviour change.

- [ ] **Step 5: Run focused and affected tests and observe GREEN**

```bash
cmake --build build --target marrow_timeline_graph_model_tests marrow_editor_shell -j4
./build/marrow_timeline_graph_model_tests
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

Expected: the new case and every existing graph-model case pass, and the
existing shell smokes still pass unchanged.

- [ ] **Step 6: Record a no-commit checkpoint**

```bash
git diff --check
git status --short
```

---

### Task 2: Shared `offset_keyframe_scalars()` authoring primitive

**Files:**

- Modify: `include/marrow/editor/authoring.hpp`
- Modify: `src/editor/authoring.cpp`
- Modify: `src/samples/editor_project_smoke.cpp`

**Interfaces:**

- Consumes: `ProjectData`, `TimelineKeySelector`, existing internal
  `resolve_timeline_key()`, `ResolvedTimelineKey`.
- Produces:

```cpp
enum class TimelineScalarComponent : std::uint8_t {
    Angle, X, Y, Red, Green, Blue, Alpha,
};

struct TimelineScalarOffsetResult : AuthoringResult {
    double applied_delta{0.0};
    std::size_t key_count{0U};
};

TimelineScalarOffsetResult offset_keyframe_scalars(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    TimelineScalarComponent component,
    double requested_delta);
```

- [ ] **Step 1: Add failing project-smoke coverage**

In `src/samples/editor_project_smoke.cpp` add
`bool validate_mar168_graph_scalar_authoring(const marrow::editor::ProjectLoadResult&)`
and call it from `main()` next to the existing editing-P0 validation. It must
assert, on a `ProjectData` copy:

```cpp
// One case per lane family; Translate X shown, repeat for Rotate Angle,
// Translate Y, Scale X, Scale Y, Shear X, Shear Y, Colour R/G/B/A.
const std::string before = marrow::editor::serialize_project(project);
const auto moved = marrow::editor::offset_keyframe_scalars(
    &project, {translate_selector}, marrow::editor::TimelineScalarComponent::X, 3.5);
if (!moved || !moved.changed || moved.key_count != 1U ||
    std::abs(moved.applied_delta - 3.5) > 1e-12) {
    std::cerr << "MAR-168 could not offset a translate X key.\n";
    return false;
}
// The sibling component, the key time, and the interpolation are untouched.
if (std::abs(key.y - original_y) > 1e-12 ||
    std::abs(key.time - original_time) > 1e-12 ||
    key.interpolation.kind() != original_interpolation_kind) {
    std::cerr << "MAR-168 scalar offset did not preserve the parent key.\n";
    return false;
}
```

Plus these cases:

1. **Rotate has no setup conversion.** On a bone with a nonzero setup rotation,
   a `+10` Angle delta moves the stored setup-relative `angle` by exactly `+10`.
2. **Signed scale.** A negative Scale X stays negative, and a delta that lands
   on `0.0` produces exactly `0.0`.
3. **Slot Color group clamp.** Two colour keys with `a = 0.4` and `a = 0.9`,
   requested delta `+0.5`: `applied_delta == 0.1`, the keys become `0.5` and
   `1.0`, and their difference is preserved.
4. **Degenerate clamp.** A colour key already at `1.4` with another at `0.2`
   yields `applied_delta == 0.0`, `changed == false`, and a byte-identical
   `serialize_project()`.
5. **Atomic rejection.** Each of: an unsupported pairing (`Angle` on a Translate
   channel, `X` on a Slot Color track, any component on a Deform/DrawOrder/Event/
   SlotAttachment selector), an unresolvable selector, a duplicated selector, a
   non-finite delta, and a delta whose result exceeds
   `std::numeric_limits<marrow::runtime::AnimationScalar>::max()` must return a
   falsy result and leave `serialize_project(project)` equal to `before`.
6. **Save/reload and export.** Save the offset project, reload it, and assert
   the offset values survive; then let the existing
   `--export-runtime` / `--export-binary` path and
   `marrow_inspect --compare` cover JSON/MBIN equivalence.

- [ ] **Step 2: Build and observe RED**

```bash
cmake --build build --target marrow_project_smoke -j4
```

Expected: compilation fails because `TimelineScalarComponent`,
`TimelineScalarOffsetResult`, and `offset_keyframe_scalars` do not exist.

- [ ] **Step 3: Implement the primitive**

Append the declarations to `include/marrow/editor/authoring.hpp` after
`retime_keyframes()`. Implement in `src/editor/authoring.cpp` immediately after
`retime_keyframes()`, following its exact shape:

```text
1. reject null project, empty selectors, non-finite requested_delta
2. ProjectData candidate = *project
3. for each selector:
     reject empty animation name / non-finite or negative time
     resolve_timeline_key(candidate, selector, &error) -> ResolvedTimelineKey
     reject duplicate (kind, timeline_index, key_index)
     reject an unsupported (kind, channel, component) pairing
     record the key's current scalar value
4. applied_delta = requested_delta
   if component is Red/Green/Blue/Alpha:
       lower = -min(original_values)
       upper = 1 - max(original_values)
       applied_delta = (upper < lower) ? 0.0 : clamp(requested_delta, lower, upper)
5. if |applied_delta| <= 1e-12 -> return {{false, {}}, 0.0, resolved.size()}
6. for each resolved key:
       write value + applied_delta into `candidate`
       clamp Red/Green/Blue/Alpha into [0, 1] as a post-condition
       reject non-finite results and |value| > AnimationScalar max
7. *project = std::move(candidate)
   return {{true, {}}, applied_delta, resolved.size()}
```

Supported pairings, rejecting everything else:

| Selector kind | Channel | Allowed components |
| --- | --- | --- |
| `Transform` | `Rotate` | `Angle` |
| `Transform` | `Translate`, `Scale`, `Shear` | `X`, `Y` |
| `SlotColor` | n/a | `Red`, `Green`, `Blue`, `Alpha` |
| `Deform`, `DrawOrder`, `Event`, `SlotAttachment` | n/a | none |

The step-5 "no change" return mirrors `retime_keyframes()`: falsy, no error
string, `key_count` populated. Callers treat it as a no-op frame, not a failure.

- [ ] **Step 4: Run the project smoke and observe GREEN**

```bash
cmake --build build --target marrow_project_smoke -j4
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

Expected: every new MAR-168 authoring case and all existing project-smoke cases
pass.

- [ ] **Step 5: Confirm no surface drift**

```bash
./build/marrow_agent_dispatch_smoke
./build/marrow_c_smoke
git diff --check
git status --short
```

Expected: the registry is still exactly 56 operations, the C ABI smoke is
unchanged, and only `authoring.hpp`, `authoring.cpp`, and
`editor_project_smoke.cpp` appear in the diff for this task.

---

### Task 3: Shell value gesture in `timeline_controller`

**Files:**

- Modify: `src/editor/shell_state.hpp`
- Modify: `src/editor/shell_core.cpp`
- Modify: `src/editor/timeline_controller.hpp`
- Modify: `src/editor/timeline_controller.cpp`
- Modify: `src/editor/shell_smoke_graph.cpp`
- Modify: `src/editor/shell_smoke_scenarios.hpp`
- Modify: `src/editor/shell_smoke.cpp`
- Modify: `src/tests/timeline_model_tests.cpp`

**Interfaces:**

- Consumes: `offset_keyframe_scalars()`, `visit_editable_timeline_keys()`,
  `timeline_key_selector()`, `timeline_key_index()`,
  `timeline_model::incremental_retime_delta()`,
  `timeline_model::completion_decision()`.
- Produces:

```cpp
bool begin_timeline_graph_value_gesture(
    ShellState* state,
    std::uint32_t item_id,
    const TimelineTrackRow& track,
    timeline_graph_model::Component component,
    const std::vector<TimelineTrackRow>& tracks);
bool apply_timeline_graph_value_delta(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    double requested_delta);
void finish_timeline_graph_value_gesture(ShellState* state, bool commit);
```

- [ ] **Step 1: Add failing shared-primitive reuse tests**

In `src/tests/timeline_model_tests.cpp` add
`test_graph_value_gesture_completion_reuse(suite)` asserting that
`completion_decision(true, false).action == CompletionAction::Cancel` with
`history_entries == 0`, that `completion_decision(true, true)` commits one
entry, and that `incremental_retime_delta()` rejects non-finite input. Register
it in `main()`. These are the exact helpers the new gesture must reuse instead
of re-deriving.

- [ ] **Step 2: Add the failing headless gesture smoke**

In `src/editor/shell_smoke_graph.cpp` add

```cpp
bool validate_timeline_graph_edit_shell_smoke(
    const std::filesystem::path& project_path);
```

declared in `src/editor/shell_smoke_scenarios.hpp` and called from
`run_headless_smoke()` in `src/editor/shell_smoke.cpp` immediately after
`validate_timeline_graph_shell_smoke()`, with its own isolated
`ShellState`/session.

For this task the scenario covers only the value trio, driven directly:

```cpp
const std::string project_before =
    marrow::editor::serialize_project(*state.session.project());
const std::size_t undo_before = state.session.undo_count();

state.timeline_editor.selected_keys = {first_key};
state.timeline_editor.active_key = first_key;
if (!begin_timeline_graph_value_gesture(
        &state, 4242U, *translate, GraphComponent::X, tracks) ||
    !authoring_gesture_active(state)) {
    std::cerr << "The graph value gesture did not open one live transaction.\n";
    return false;
}
if (!apply_timeline_graph_value_delta(&state, tracks, 3.0) ||
    !apply_timeline_graph_value_delta(&state, tracks, 5.0)) {
    std::cerr << "The graph value gesture did not preview its delta.\n";
    return false;
}
finish_timeline_graph_value_gesture(&state, true);
if (authoring_gesture_active(state) ||
    state.session.undo_count() != undo_before + 1U ||
    state.timeline_editor.selected_keys != std::vector<TimelineKeyRef>{first_key} ||
    !(state.timeline_editor.active_key == std::optional<TimelineKeyRef>(first_key))) {
    std::cerr << "One graph value drag did not produce one stable-selection undo entry.\n";
    return false;
}
```

and, in the same scenario:

- the projected X value moved by exactly `5.0` while Y, the key time, and the
  outgoing easing kind are unchanged;
- a second gesture cancelled with `finish_timeline_graph_value_gesture(&state, false)`
  restores `serialize_project()`, `undo_count()`, `redo_count()`,
  `project_revision()`, `dirty()`, the rebuilt dopesheet `key_times`, and every
  rebuilt graph component value;
- a gesture whose net delta is zero commits nothing (`undo_count()` unchanged);
- a Slot Color Alpha gesture over two selected keys stops at the group clamp and
  preserves their difference;
- a Rotate Angle gesture moves the absolute projected angle by the delta;
- a Scale X gesture reaches exactly `0.0`;
- `apply_timeline_graph_value_delta(&state, tracks, NaN)` cancels the gesture,
  restores the project bytes, and leaves `authoring_gesture_active(state)` false;
- `agent_operation_descriptor_count() == 56` before and after.

- [ ] **Step 3: Build and observe RED**

```bash
cmake --build build --target marrow_editor_shell marrow_timeline_model_tests -j4
```

Expected: compilation fails because `TimelineGraphValueGesture`,
`TimelineEditorState::graph_value_gesture`, and the three controller functions
do not exist.

- [ ] **Step 4: Add the transient state contract**

In `src/editor/shell_state.hpp` add `TimelineGraphPointDrag` and
`TimelineGraphValueGesture` exactly as declared in design spec §11.5, then add
to `TimelineEditorState`, after `graph_cache`:

```cpp
std::optional<TimelineGraphPointDrag> graph_drag;
std::optional<TimelineGraphValueGesture> graph_value_gesture;
```

Add `state.timeline_editor.graph_value_gesture.has_value() ||` to
`authoring_gesture_active()`. Do **not** add `graph_drag`; it holds no
transaction.

In `src/editor/shell_core.cpp`, inside `cancel_authoring_gestures()`, add
`cancel_transaction_gesture(state->timeline_editor.graph_value_gesture);`
directly after the existing `retime_gesture` line, and
`state->timeline_editor.graph_drag.reset();` next to the other
non-transactional resets.

- [ ] **Step 5: Implement the value gesture trio**

Add the three functions to `timeline_controller.hpp/.cpp` directly after
`finish_timeline_retime_gesture()`, following design spec §6.4.

`begin_timeline_graph_value_gesture()`:

1. reject a null state, `authoring_gesture_active(*state)`,
   `!timeline_track_is_editable(track)`, an empty selection, and an unsupported
   `(track.kind, component)` pairing;
2. `begin_edit({EditKind::EditProperty, label, "timeline:graph-value", false,
   Project | Runtime | Preview})` where `label` is `"Edit graph key value"` for
   one key and `"Edit graph key values"` otherwise;
3. collect gesture keys: entries of `selected_keys` whose `track_id ==
   track.id`, in stable selection order; cancel and return false when empty or
   when any key fails `timeline_key_index()`;
4. record each key's current component value from the projected track;
5. store the gesture in `state->timeline_editor.graph_value_gesture`.

`apply_timeline_graph_value_delta()` follows the ten numbered steps of design
spec §6.4 verbatim. Map `timeline_graph_model::Component` to
`marrow::editor::TimelineScalarComponent` with one small local switch.

`finish_timeline_graph_value_gesture()` mirrors
`finish_timeline_retime_gesture()`: move the gesture out, reset the optional,
use `completion_decision(commit, gesture.changed)`, cancel or commit, call
`sync_shell_from_editor_session(state)`, and set the §12 status message.

- [ ] **Step 6: Run focused and affected tests and observe GREEN**

```bash
cmake --build build --target marrow_editor_shell marrow_timeline_model_tests -j4
./build/marrow_timeline_model_tests
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

Expected: the new model case, the new headless value-gesture scenario, and every
existing shell scenario pass.

- [ ] **Step 7: Record a no-commit checkpoint**

```bash
git diff --check
git status --short
```

---

### Task 4: Graph point drag driver and axis lock

**Files:**

- Modify: `src/editor/shell_timeline_graph.hpp`
- Modify: `src/editor/shell_timeline_graph.cpp`
- Modify: `src/editor/shell_smoke_graph.cpp`

**Interfaces:**

- Consumes: Task 1 drag math, Task 3 value trio, the existing
  `begin/apply/finish_timeline_retime_gesture()` trio, and
  `activate_timeline_graph_point()`.
- Produces:

```cpp
bool begin_timeline_graph_point_drag(
    ShellState* state,
    const TimelineTrackRow& track,
    const timeline_graph_model::PointHit& point,
    std::uint32_t item_id,
    timeline_graph_model::PlotRect plot,
    const timeline_graph_model::View& view,
    double pointer_x,
    double pointer_y);
bool update_timeline_graph_point_drag(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    double pointer_x,
    double pointer_y,
    bool pointer_down,
    bool cancel_requested,
    bool bypass_frame_snap);
void cancel_timeline_graph_point_drag(ShellState* state);
```

- [ ] **Step 1: Extend the headless smoke with full drag gestures**

In `validate_timeline_graph_edit_shell_smoke()` add, driving the driver directly
with no ImGui frame:

```cpp
// A press alone must not open a transaction or a history entry.
if (!begin_timeline_graph_point_drag(
        &state, *translate, hit, 4242U, plot, view, 320.0, 180.0) ||
    authoring_gesture_active(state) ||
    state.session.undo_count() != undo_before) {
    std::cerr << "A graph press must arm a candidate without a transaction.\n";
    return false;
}
// Dominant vertical motion must lock the value axis.
(void)update_timeline_graph_point_drag(
    &state, tracks, 322.0, 220.0, true, false, false);
if (!state.timeline_editor.graph_value_gesture.has_value() ||
    state.timeline_editor.retime_gesture.has_value() ||
    state.timeline_editor.graph_drag->axis != timeline_graph_model::DragAxis::Value) {
    std::cerr << "A dominant vertical graph drag did not lock the value axis.\n";
    return false;
}
```

Add these cases:

- a dominant horizontal drag opens `retime_gesture` and never opens
  `graph_value_gesture`, and the resulting key time is frame-snapped when
  `snap_to_frames` is true;
- `bypass_frame_snap = true` reproduces the Alt path and lands off the frame
  grid;
- a locked axis never changes: after locking `Value`, later horizontal motion
  leaves every key time byte-identical; after locking `Time`, later vertical
  motion leaves every non-time component byte-identical (this is the direct
  acceptance-criterion-1 assertion);
- a Time drag toward an unselected neighbour clamps at
  `timeline_model::kNonEventKeySpacing` and the gesture stays live;
- explicit-duration auto-grow: on an animation with an authored explicit
  duration, dragging the last key later grows the duration inside the same
  transaction and one undo entry;
- `cancel_requested = true` and `cancel_timeline_graph_point_drag()` both roll
  back to byte-identical project, history, revision, dopesheet `key_times`, and
  graph values, on both axes;
- `pointer_down = false` commits and clears both the candidate and the gesture;
- a press on a non-editable or unsupported focused row arms no candidate;
- a press while `authoring_gesture_active(state)` arms no candidate;
- undo then redo of a committed value drag restores values with `selected_keys`
  and `active_key` intact; undo of a committed time drag yields the
  deterministic reconciled selection from
  `reconcile_timeline_key_selection()`.

- [ ] **Step 2: Build and observe RED**

```bash
cmake --build build --target marrow_editor_shell -j4
```

Expected: compilation fails because the three driver functions do not exist.

- [ ] **Step 3: Implement the driver**

In `shell_timeline_graph.cpp`:

`begin_timeline_graph_point_drag()` — reject a null state,
`authoring_gesture_active()`, a non-editable track, a projection that is not
`Ready`, a `PointHit` whose key is not in `selected_keys`, and non-finite
pointer/view input. Otherwise populate `TimelineGraphPointDrag` with the frozen
`view`/`plot`, the pressed key/component/component index, the press pointer
coordinates, `press_time_seconds` and `press_value` read from the projected key,
and `axis = Undecided`.

`update_timeline_graph_point_drag()`:

```text
if no candidate -> return false
if cancel_requested:
    finish the live axis gesture with commit = false
    reset the candidate; return false
if !pointer_down:
    finish the live axis gesture with commit = true
    reset the candidate; return false
if axis == Undecided:
    axis = decide_drag_axis(press_x, press_y, pointer_x, pointer_y, 4.0)
    if still Undecided -> return true
    if axis == Time:
        if !begin_timeline_retime_gesture(state, item_id, press_x, tracks)
            -> reset candidate; return false
    else:
        if !begin_timeline_graph_value_gesture(state, item_id, row, component, tracks)
            -> reset candidate; return false
if axis == Time:
    delta = drag_time_delta(frozen_view, press_x, pointer_x)
    if !delta -> finish_timeline_retime_gesture(state, false); reset; return false
    snap = state->timeline_editor.snap_to_frames && !bypass_frame_snap
    if !apply_timeline_retime_delta(state, tracks, *delta, snap)
        -> reset candidate; return false      // apply_* already cancelled
else:
    delta = drag_value_delta(frozen_view, press_y, pointer_y)
    if !delta -> finish_timeline_graph_value_gesture(state, false); reset; return false
    if !apply_timeline_graph_value_delta(state, tracks, *delta)
        -> reset candidate; return false      // apply_* already cancelled
return true
```

The candidate must also self-cancel when the focused row disappears, the
projection stops being `Ready`, or the track ID no longer matches — resolve the
row from `tracks` by `graph_drag->track_id` each update.

`cancel_timeline_graph_point_drag()` finishes whichever axis gesture is live
with `commit = false` and resets the candidate.

- [ ] **Step 4: Run the shell smoke and observe GREEN**

```bash
cmake --build build --target marrow_editor_shell -j4
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

Expected: every drag case passes and the MAR-167 graph scenario is unchanged.

- [ ] **Step 5: Record a no-commit checkpoint**

```bash
git diff --check
git status --short
```

---

### Task 5: ImGui wiring, frozen view, Snap control, and actual-frame smoke

**Files:**

- Modify: `src/editor/shell_timeline_graph.hpp`
- Modify: `src/editor/shell_timeline_graph.cpp`
- Modify: `src/editor/shell_timeline.cpp`
- Modify: `src/editor/shell_smoke_frames.cpp`

**Interfaces:**

- Consumes: Task 4 driver.
- Produces: the `TimelineGraphRenderStats` additions from design spec §11.7.

- [ ] **Step 1: Add a failing actual-frame drag test**

In `render_headless_smoke_frames()`, after the existing graph frames, add a
block that:

1. focuses `bone:1:Translate`, requests Graph mode, and renders one frame to
   obtain `stats.active_point_x/_y` and `stats.active_point_valid`;
2. presses the left button at the reported active point, renders, moves the
   pointer `+40` px vertically, renders, and asserts
   `stats.drag_axis == DragAxis::Value` and `stats.value_gesture_active`;
3. releases and asserts one new undo entry, an unchanged key time, and an
   unchanged Y component;
4. repeats with a `+40` px horizontal move and asserts
   `stats.drag_axis == DragAxis::Time` and `stats.retime_gesture_active`;
5. asserts that during a live drag a wheel event does not change
   `graph_view.view.pixels_per_second`, a middle drag does not change
   `view_start_seconds`, and clicking `Fit` does not change the view;
6. switches to the Dopesheet tab mid-drag and asserts the project bytes,
   `undo_count()`, and `runtime_revision()` return to their pre-press values;
7. asserts the dopesheet lane still starts and commits a normal retime
   afterwards.

Capture `serialize_project()`, `dirty()`, `undo_count()`, `redo_count()`,
project/runtime revision, and `agent_operation_descriptor_count()` before the
block; assert the Agent count and serialization/history invariants for every
cancelled case.

- [ ] **Step 2: Build and observe RED**

```bash
cmake --build build --target marrow_editor_shell -j4
```

Expected: compilation fails because `TimelineGraphRenderStats::active_point_x`,
`drag_axis`, `value_gesture_active`, and `retime_gesture_active` do not exist.

- [ ] **Step 3: Extend render statistics**

Add the fields from design spec §11.7 to `TimelineGraphRenderStats`. Populate
`first_point_*` from the first entry of `Geometry::points`, and
`active_point_*` from the point whose `key == active_key` and
`component == graph_view.active_component`, both only when actually submitted to
the draw list. Populate the three gesture booleans and `drag_axis` from
`TimelineEditorState`.

- [ ] **Step 4: Wire input in `draw_timeline_graph_body()`**

- Add the `Snap` checkbox on the toolbar row, bound directly to
  `&state->timeline_editor.snap_to_frames`, immediately before the `Fit` button.
- Compute `const bool drag_live = state->timeline_editor.graph_drag.has_value();`
  and skip wheel zoom, Shift-wheel value zoom, middle-drag pan, the `Fit`
  button/`F` key, and the `needs_fit` auto-fit while `drag_live` is true.
- Replace the MAR-167 pressed-point bookkeeping (the
  `timeline_graph_pressed_point` `ImGuiStorage` slot and the
  `point_drag_notice` block, including the string
  `"Value/time dragging is available in MAR-168"`) with:

```cpp
if (plot_hovered && ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
    const auto hit = timeline_graph_model::hit_test(
        *geometry, io.MousePos.x, io.MousePos.y, 8.0);
    if (hit.has_value()) {
        const bool additive = io.KeyCtrl || io.KeySuper;
        if (activate_timeline_graph_point(state, *row, *hit, additive, "Timeline Graph")) {
            (void)begin_timeline_graph_point_drag(
                state, *row, *hit, ImGui::GetItemID(), plot, graph_view.view,
                io.MousePos.x, io.MousePos.y);
        }
    } else {
        // unchanged MAR-167 empty-plot scrub branch
    }
}
if (state->timeline_editor.graph_drag.has_value()) {
    (void)update_timeline_graph_point_drag(
        state,
        tracks,
        io.MousePos.x,
        io.MousePos.y,
        ImGui::IsMouseDown(ImGuiMouseButton_Left),
        ImGui::IsKeyPressed(ImGuiKey_Escape, false),
        io.KeyAlt);
}
```

- Draw the §12 drag readout line while a gesture is live.

- [ ] **Step 5: Guard the dopesheet updater and the tab switch**

In `src/editor/shell_timeline.cpp`:

- make `update_timeline_retime_gesture()` return immediately when
  `state->timeline_editor.view_mode == TimelineViewMode::Graph`, so exactly one
  surface drives a live retime;
- in `draw_timeline_window()`, before the Dopesheet tab body runs or a
  `requested_view_mode` transition away from Graph is applied, call
  `cancel_timeline_graph_point_drag(state)`.

- [ ] **Step 6: Run focused frame and shell tests and observe GREEN**

```bash
cmake --build build --target marrow_editor_shell marrow_timeline_graph_model_tests -j4
./build/marrow_timeline_graph_model_tests
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

Expected: both actual-frame drags report the expected locked axis and gesture,
the suppression assertions hold, the tab-switch cancel is byte-exact, and every
existing scenario still passes.

- [ ] **Step 7: Refactor only after GREEN**

Remove any leftover duplicated pixel-to-unit arithmetic, the unused
`timeline_graph_pressed_point` storage ID, and the MAR-167 read-only notice
string. Do not touch `SelectionSet`, the dopesheet lane, or unrelated timeline
authoring functions.

- [ ] **Step 8: Record a no-commit checkpoint**

```bash
git diff --check
git status --short
```

---

### Task 6: Full validation, documentation, and MAR-168 closure

**Files:**

- Modify: `AGENTS.md`
- Modify: `docs/root1/discription.md`
- Modify: `docs/root1/quick-start.md`
- Modify: `docs/root1/concepts.md`
- Modify: `docs/root1/editing-gap-analysis.md`
- Modify: `docs/root1/refector.md`
- Modify: `docs/root1/platform-validation.md` (only the stale "next MAR-168" line)
- Modify: `docs/root1/fixtures.md` if the fixture's editing role is not explicit
- Modify: `docs/superpowers/specs/2026-08-30-mar-168-graph-key-time-value-editing-design.md`
- Modify: `.agents/tasks/prd-marrow-runtime.json`

**Interfaces:**

- Consumes: completed Tasks 1–5 and fresh command output.
- Produces: synchronized documentation, exact validation evidence,
  `MAR-168 status=done`, and `MAR-169 status=open`.

- [ ] **Step 1: Run the fresh default/focused gate**

```bash
cmake -S . -B build &&
cmake --build build -j4 &&
./build/marrow_timeline_graph_model_tests &&
./build/marrow_timeline_model_tests &&
./build/marrow_project_smoke assets/fixtures/player_idle.marrow &&
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2 &&
ctest --test-dir build --output-on-failure
```

Expected: every command exits zero. MAR-168 registers no new CTest, so the
default suite should still report 21 tests; confirm with
`ctest --test-dir build -N` and document the actual justified count if it
differs.

- [ ] **Step 2: Run Debug and Release display-enabled gates**

```bash
cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON &&
cmake --build build-display -j4 &&
ctest --test-dir build-display --output-on-failure &&
cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON &&
cmake --build build-platform-release -j4 &&
ctest --test-dir build-platform-release --output-on-failure
```

Expected: both display-enabled suites pass, still 24 tests each including the
three display tests. Label the evidence automated; claim no manual UI or
Windows qualification.

- [ ] **Step 3: Run export/runtime compatibility gates**

```bash
./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar168.mskl --export-binary /tmp/marrow_mar168.mbin
./build/marrow_inspect --compare /tmp/marrow_mar168.mbin /tmp/marrow_mar168.mskl
./build/marrow_fixture_smoke /tmp/marrow_mar168.mskl /tmp/player_idle.matl
```

Expected: export, JSON/MBIN comparison, and runtime fixture smoke pass. Record
exact comparison errors and output sizes from the fresh run.

- [ ] **Step 4: Run unchanged-surface and integrity gates**

```bash
./build/marrow_agent_dispatch_smoke
./build/marrow_agent_socket_tests
./build/marrow_c_smoke
tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
cmake --build build --target marrow_verify_third_party
python3 -m json.tool assets/fixtures/player_idle.marrow >/dev/null
python3 -m json.tool .agents/tasks/prd-marrow-runtime.json >/dev/null
git diff --check
git lfs status
```

Expected: all commands exit zero, the Agent registry remains exactly 56
operations, and no LFS object is staged or normalized.

- [ ] **Step 5: Prove the compatibility claim explicitly**

```bash
git diff --stat -- include/marrow/c_api src/c_api
git diff -- include/marrow/editor/authoring.hpp
git diff --stat -- src/editor/agent_dispatch.cpp src/editor/agent_handlers_editing.cpp tools/mcp
```

Expected: the C ABI paths and the agent/MCP paths show zero changes, and the
`authoring.hpp` diff contains only added lines.

- [ ] **Step 6: Synchronize user and architecture documentation**

Write only evidence supported by Steps 1–5:

- `quick-start.md`: replace the "MAR-167 is read-only / point dragging displays
  the MAR-168 boundary" paragraph with the drag contract — press a point, the
  axis locks on the first 4 px of motion, vertical edits the active component,
  horizontal retimes the whole key and every selected key, `Snap` and Alt,
  Escape cancels, one drag is one undo, RGBA clamps to `[0, 1]` and other
  components do not, and Bezier editing is still MAR-169.
- `concepts.md`: replace "MAR-167 performs no graph authoring; point time/value
  dragging begins at MAR-168" with the shared-primitive statement — the graph
  writes through `retime_keyframes()` and `offset_keyframe_scalars()` inside one
  `EditTransaction`, non-edited components and the shared outgoing easing are
  carried through unchanged, and explicit-duration auto-grow happens inside the
  same transaction.
- `discription.md`: add a dated MAR-168 contract paragraph, make MAR-169 next,
  and update the two milestone-history sentences that currently end at MAR-167.
- `editing-gap-analysis.md`: update the MAR-167 "value/time drag는 MAR-168
  경계다" sentence, the MAR-168 roadmap row, the "다음 직접 제품 milestone" line,
  and the P1 chain sentence so MAR-169 is next.
- `refector.md`: update the `MAR-168–191` row and the two sentences naming
  MAR-168 as the next milestone.
- `platform-validation.md`: update the "next MAR-168 product milestone" line to
  MAR-169 without changing any qualification status.
- `fixtures.md`: note that `player_idle` supplies the Rotate/Translate/Scale/
  Shear/Slot Color lanes MAR-168 edits, if not already explicit.
- `AGENTS.md`: update the Project State line so MAR-168 is complete and MAR-169
  is next; add the new validation command lines; add the
  `MAR-168 Graph Key Time and Value Editing Validation Results` section using
  the MAR-167 table plus command-output format with these slices:

  | Slice | Verification | Result |
  | --- | --- | --- |
  | Axis lock and component preservation | Locked-axis drags: vertical changes only the active component; horizontal changes only key times. Graph-model unit tests plus headless and actual-frame drags | |
  | Shared authoring reuse | Frame snap, neighbour collision with 1 ms spacing, stable identity, and explicit-duration auto-grow come from `retime_keyframes()`/`refresh_runtime()`; values come from `offset_keyframe_scalars()` | |
  | Atomic rollback | Escape, focus loss, tab switch, non-finite value, and hard rejection restore project bytes, history, revision, dopesheet key times, and graph values | |
  | One drag, one transaction | Press-only opens none; a zero-net drag commits none; a committed drag adds exactly one undo entry with stable selection | |
  | Persistence and compatibility | Save/reload, JSON/MBIN export, `.mskl` v1, `.mbin` v2, C ABI v1, and the 56-operation Agent/MCP surface unchanged | |

- design spec: change status from `Approved design, implementation not started`
  to `Implemented and validated` only after every gate is green.

Add no file-format documentation: MAR-168 persists no new field.

- [ ] **Step 7: Close only MAR-168 in the PRD**

```json
{
  "id": "MAR-168",
  "status": "done",
  "completedAt": "<verified date>"
}
```

Keep MAR-169 open and dependent on MAR-168. Update the overview so the remaining
strict chain begins at MAR-169. Do not alter MAR-192 through MAR-210.

- [ ] **Step 8: Run documentation/roadmap integrity checks**

```bash
python3 -m json.tool .agents/tasks/prd-marrow-runtime.json >/dev/null
jq -e '
  (.stories[] | select(.id == "MAR-168") | .status) == "done" and
  (.stories[] | select(.id == "MAR-169") | .status) == "open" and
  ([.stories[] | select(.id >= "MAR-192" and .id <= "MAR-210") | .status] | all(. == "open"))
' .agents/tasks/prd-marrow-runtime.json >/dev/null
! rg -n "MAR-168 (is )?(the )?next|MAR-168.*status.*open|MAR-167 is the next" AGENTS.md docs/root1 .agents/tasks/prd-marrow-runtime.json
! rg -n "Value/time dragging is available in MAR-168" src docs
rg -n "MAR-169" AGENTS.md docs/root1/discription.md docs/root1/editing-gap-analysis.md .agents/tasks/prd-marrow-runtime.json
git diff --check
```

Expected: JSON and jq checks pass, the stale "MAR-168 next/open" and read-only
notice searches return no match, and current MAR-169 references exist.

- [ ] **Step 9: Perform requirement-by-requirement completion audit**

For every MAR-168 acceptance criterion, point to both source and executed
evidence:

1. Vertical edits only the active component, horizontal changes the whole key's
   time and all components — `decide_drag_axis` unit tests, the locked-axis
   headless assertions, and the actual-frame axis assertions.
2. Shared frame snap, collision, neighbouring-key, stable-identity, and
   explicit-duration auto-grow — `retime_keyframes()` reuse via the unchanged
   retime trio, the neighbour-clamp and frame-snap headless cases, and the
   auto-grow case.
3. Atomic rollback on collision, non-finite value, or invalid context —
   `offset_keyframe_scalars()` rejection cases in the project smoke plus every
   cancel case's byte-identical project/history/revision/dopesheet/graph
   comparison.
4. One drag, one transaction, live preview, cancel, undo/redo, stable selection
   — `undo_count()` deltas, `refresh_runtime()` preview assertions, and the
   selection/`active_key` assertions across commit, cancel, undo, and redo.
5. Shell and project smokes over every lane family, component preservation,
   frame snap, auto-grow, collision cancellation, save/reload, and JSON/MBIN
   export — the per-family project-smoke cases plus the headless and
   actual-frame scenarios plus Step 3's export gate.

Treat any missing or indirect evidence as incomplete. Fix via a new RED-GREEN
cycle, rerun the affected focused test, then rerun the full gate that supports
the claim.

- [ ] **Step 10: Final no-commit checkpoint**

```bash
git status --short
git diff --stat
git diff --check
git lfs status
```

Report the changed files, fresh test totals, export comparison metrics, the
exact remaining qualification limitations, and that no commit, push, or reset
occurred.

---

## Full verification checklist

Every command below must pass before MAR-168 is considered complete.

```bash
# Configure and build
cmake -S . -B build
cmake --build build -j4

# Focused UI-free tests
./build/marrow_timeline_graph_model_tests
./build/marrow_timeline_model_tests
./build/marrow_viewport_interaction_tests
./build/marrow_selection_tests

# Project and shell smokes
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke --create /tmp/player_idle.marrow
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2

# Default CTest registry
ctest --test-dir build -N
ctest --test-dir build --output-on-failure

# Debug display-enabled suite
cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON
cmake --build build-display -j4
ctest --test-dir build-display --output-on-failure

# Release display-enabled suite
cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON
cmake --build build-platform-release -j4
ctest --test-dir build-platform-release --output-on-failure

# Export, compare, and runtime fixture gates
./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar168.mskl --export-binary /tmp/marrow_mar168.mbin
./build/marrow_inspect --compare /tmp/marrow_mar168.mbin /tmp/marrow_mar168.mskl
./build/marrow_fixture_smoke /tmp/marrow_mar168.mskl /tmp/player_idle.matl

# Unchanged-surface gates
./build/marrow_agent_dispatch_smoke
./build/marrow_agent_socket_tests
./build/marrow_c_smoke
tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
cmake --build build --target marrow_verify_third_party

# JSON and repository integrity
python3 -m json.tool assets/fixtures/player_idle.marrow >/dev/null
python3 -m json.tool assets/fixtures/player_idle.mskl >/dev/null
python3 -m json.tool .agents/tasks/prd-marrow-runtime.json >/dev/null
git diff --check
git lfs status

# Compatibility proof
git diff --stat -- include/marrow/c_api src/c_api
git diff --stat -- src/editor/agent_dispatch.cpp src/editor/agent_handlers_editing.cpp tools/mcp
```

Expected results to record in `AGENTS.md`:

- graph model tests: all cases pass, including the new drag-math case;
- timeline model tests: all cases pass, including the new completion-reuse case;
- project smoke: passes, including every new MAR-168 authoring case;
- shell smoke: passes, including the new headless drag scenario and the
  actual-frame drag frames;
- default CTest: 21/21 (MAR-168 adds no CTest; record the actual count from
  `ctest --test-dir build -N`);
- Debug and Release display suites: 24/24 each, including 3 display tests;
- `marrow_inspect --compare`: match, with the recorded key counts and byte sizes;
- `marrow_agent_dispatch_smoke`: exactly 56 operations;
- `git diff --stat` over `include/marrow/c_api`, `src/c_api`,
  `src/editor/agent_dispatch.cpp`, `src/editor/agent_handlers_editing.cpp`, and
  `tools/mcp`: empty.

The display suites are automated evidence only. This checkpoint adds no manual
visible-UI, Windows 11, physical-input, or platform qualification credit.
MAR-192 through MAR-210 remain open, and support qualification remains governed
by `docs/root1/platform-validation.md`.
