# MAR-169 Graphical Shared Bezier Handle Editing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the active key's outgoing shared `[cx1, cy1, cx2, cy2]` easing
draggable in the Graph tab, through one new UI-free authoring primitive, with
one transaction per drag, live preview, atomic cancel, `cx` clamped to `[0, 1]`,
finite `cy` overshoot allowed, Linear/Stepped converting to Cubic in the same
undo entry, and the identical mutation exposed as one new C++ Agent operation
and one matching Python MCP tool — taking the registry from 56 to exactly 57
operations while `.marrow`, `.mskl` v1, `.mbin` v2, and C ABI v1 stay unchanged.

**Architecture:** Add pure handle geometry and pointer mapping to
`timeline_graph_model`. Add one additive project-domain primitive
`set_keyframe_interpolation()` next to `offset_keyframe_scalars()`. Add an
ImGui-free handle-gesture trio to `timeline_controller`, and a `Handle` branch to
MAR-168's existing graph drag driver in `shell_timeline_graph`.
`draw_timeline_graph_body()` only samples input and draws. Add
`timeline.set_interpolation` to the Agent registry and the MCP tool list, both
calling the same primitive.

**Tech Stack:** C++17, Dear ImGui, existing Marrow runtime/editor session APIs,
CMake/CTest, Python MCP SDK, JSON fixtures.

**Spec:** `docs/superpowers/specs/2026-08-30-mar-169-graphical-bezier-handles-design.md`

## Global Constraints

- Read `docs/root1/discription.md` and the MAR-169 PRD story before each
  implementation task; current source and tests override stale plan assumptions.
- Follow strict RED-GREEN-REFACTOR: add one focused failing test, observe the
  expected failure, write the minimum production code, then rerun focused and
  affected regression tests.
- Do not reset, discard, stash, commit, push, or create a PR unless the user
  separately requests it. Each task ends with a read-only
  `git diff --check` / `git status --short` checkpoint.
- Never introduce a parallel easing-writing path. Every mutation goes through
  `set_keyframe_interpolation()` inside an `EditorSession::EditTransaction`.
- Never introduce a per-component curve. `set_keyframe_interpolation()` must
  never gain a component parameter, and no code may branch on a component when
  deciding which `interpolation` field to write.
- Never change MAR-168's point-drag behaviour: the axis lock, the value delta
  semantics, the retime reuse, and their status messages stay as built.
- Preserve `.marrow`, `.mskl` v1, `.mbin` v2, C ABI v1, `SelectionSet`, and GPU
  ownership. The only public header change is the additive block in
  `include/marrow/editor/authoring.hpp`.
- The Agent/MCP surface grows from 56 to exactly **57** operations, purely
  additively. No existing operation's name, category, flags, arguments, or
  response shape may change.
- Automated display gates do not establish manual-visible-UI, Windows 11, or
  physical-input qualification. Keep MAR-192 through MAR-210 open.

## File and Responsibility Map

**Create**

- none. MAR-169 extends existing units.

**Modify**

- `src/editor/timeline_graph_model.hpp/.cpp` — `HandleIndex`, `SegmentFrame`,
  `HandleGeometry`, `HandleHit`, the seed constant, `seed_control_points()`,
  `make_segment_frame()`, `build_handle_geometry()`, `hit_test_handle()`,
  `control_points_from_handle_pointer()`.
- `src/tests/timeline_graph_model_tests.cpp` — focused handle-math tests.
- `include/marrow/editor/authoring.hpp` — additive
  `TimelineInterpolationResult` and `set_keyframe_interpolation()`.
- `src/editor/authoring.cpp` — `set_keyframe_interpolation()` implementation.
- `src/samples/editor_project_smoke.cpp` — family, conversion, clamp, overshoot,
  atomicity, save/reload, and export coverage for the new primitive.
- `src/editor/shell_state.hpp` — `GraphDragTarget`,
  `TimelineGraphHandleGesture`, the `TimelineGraphPointDrag` additions, one
  `TimelineEditorState` field, and the `authoring_gesture_active()` update.
- `src/editor/shell_core.cpp` — `cancel_authoring_gestures()` update.
- `src/editor/timeline_controller.hpp/.cpp` — the handle-gesture trio.
- `src/editor/shell_timeline_graph.hpp/.cpp` — handle arming entry point, the
  `Handle` branch of the drag driver, handle rendering, the easing readout, the
  disabled component checkboxes and `Fit`, and the render-stat additions.
- `src/editor/shell_smoke_graph.cpp`, `src/editor/shell_smoke_scenarios.hpp`,
  `src/editor/shell_smoke.cpp` — the new headless easing scenario.
- `src/editor/shell_smoke_frames.cpp` — actual-frame handle coverage.
- `src/editor/agent_dispatch.cpp` — the 57th registry row and
  `interpolation_request_arg()`.
- `src/editor/agent_dispatch_internal.hpp` — `interpolation_request_arg()`
  declaration.
- `src/editor/agent_handlers_editing.cpp` — the
  `timeline.set_interpolation` handler.
- `src/samples/agent_dispatch_smoke.cpp` — expectation array 56 -> 57 plus the
  new row and behavioural coverage.
- `tools/mcp/tools/editing.py` — the new tool and its two helper schemas.
- `tools/mcp/test_client.py` — 56 -> 57 parity and behavioural coverage.
- `AGENTS.md`, `docs/root1/*.md`, the design spec, and
  `.agents/tasks/prd-marrow-runtime.json` — record verified MAR-169 completion
  and make MAR-170 next.

---

### Task 0: Reconcile with the as-built MAR-168

MAR-168 was implemented concurrently with this plan. Its final code may differ
from its plan in naming or in small details. **Do this before writing any
MAR-169 code**, and prefer the as-built source over anything this plan asserts
about MAR-168.

**Files:** read-only.

- [ ] **Step 1: Confirm MAR-168 is complete and green**

```bash
jq -r '.stories[] | select(.id == "MAR-168") | "\(.id) \(.status) \(.completedAt // "-")"' .agents/tasks/prd-marrow-runtime.json
cmake -S . -B build && cmake --build build -j4
./build/marrow_timeline_graph_model_tests
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_agent_dispatch_smoke
```

Expected: MAR-168 is `done`, everything passes, and the registry is still 56.
If MAR-168 is not yet `done`, stop and report; MAR-169 must not start on a
half-built dependency.

- [ ] **Step 2: Read the as-built MAR-168 surfaces and record the deltas**

```bash
sed -n '1,60p' src/editor/timeline_graph_model.hpp
sed -n '150,200p' src/editor/timeline_controller.hpp
grep -n 'TimelineGraphPointDrag\|TimelineGraphValueGesture\|authoring_gesture_active' -A 30 src/editor/shell_state.hpp
grep -n 'begin_timeline_graph_point_drag\|update_timeline_graph_point_drag\|cancel_timeline_graph_point_drag' src/editor/shell_timeline_graph.hpp src/editor/shell_timeline_graph.cpp
grep -n 'TimelineGraphRenderStats' -A 40 src/editor/shell_timeline_graph.hpp
grep -n 'validate_timeline_graph_edit_shell_smoke' src/editor/shell_smoke_scenarios.hpp src/editor/shell_smoke.cpp src/editor/shell_smoke_graph.cpp
grep -rn '\b56\b' --glob '!build*' src tools AGENTS.md docs .agents
```

Write down, in your working notes (not in a file):

1. the exact names and signatures of `time_at_x`, `value_at_y`, `x_at_time`,
   `y_at_value`, `DragAxis`, `decide_drag_axis`, `drag_time_delta`,
   `drag_value_delta`;
2. the exact fields of `TimelineGraphPointDrag` and `TimelineGraphValueGesture`;
3. the exact signature of `update_timeline_graph_point_drag()` and where its
   axis branch lives;
4. the exact fields MAR-168 added to `TimelineGraphRenderStats`;
5. **every** file and line that asserts `56`, including any assertion MAR-168
   added inside `validate_timeline_graph_edit_shell_smoke()`. The table in §14
   of the design spec is a starting point, not a complete list;
6. whether MAR-168 added
   `timeline_graph_component_is_editable(track, component)` or an equivalent
   helper — if so, reuse it rather than re-deriving the component/family check.

- [ ] **Step 3: Reconcile this plan against those facts**

Where the as-built code differs, adapt the MAR-169 signatures below to match it.
Do not rename or reshape MAR-168's API to fit this plan. If a difference makes
a MAR-169 decision impossible (for example, if the drag candidate turns out not
to be extensible), stop and report rather than duplicating MAR-168's gesture
machinery.

---

### Task 1: UI-free handle geometry and pointer mapping

**Files:**

- Modify: `src/editor/timeline_graph_model.hpp`
- Modify: `src/editor/timeline_graph_model.cpp`
- Modify: `src/tests/timeline_graph_model_tests.cpp`

**Interfaces:**

- Consumes: existing `PlotRect`, `View`, `Track`, `Key`, `SegmentKind`,
  `time_at_x()`, `value_at_y()`, `x_at_time()`, `y_at_value()`.
- Produces: `HandleIndex`, `SegmentFrame`, `HandleGeometry`, `HandleHit`,
  `kLinearEquivalentControlPoints`, `kMinimumSegmentSeconds`,
  `kMinimumSegmentValuePixels`, `kFlatSegmentHandlePixels`,
  `seed_control_points()`, `make_segment_frame()`, `build_handle_geometry()`,
  `hit_test_handle()`, `control_points_from_handle_pointer()`
  (full declarations in design spec §11.1).

- [ ] **Step 1: Add failing handle-math tests**

In `src/tests/timeline_graph_model_tests.cpp` add
`test_graph_handle_geometry_and_pointer_mapping(suite)` and register it in
`main()`. Build the track fixtures locally, in the style the existing cases
already use. It must assert:

```cpp
constexpr graph::PlotRect rect{100.0, 40.0, 700.0, 340.0};
const graph::View view{-0.25, 200.0, 10.0, 0.5};   // negative start, sub-unit ppv

// Seeding.
suite.expect(
    graph::seed_control_points(
        graph::SegmentKind::Linear, {0.9, 0.9, 0.1, 0.1}) ==
        graph::kLinearEquivalentControlPoints &&
    graph::seed_control_points(
        graph::SegmentKind::Stepped, {0.9, 0.9, 0.1, 0.1}) ==
        graph::kLinearEquivalentControlPoints &&
    graph::seed_control_points(
        graph::SegmentKind::Cubic, {0.9, 0.9, 0.1, 0.1}) ==
        std::array<double, 4>{0.9, 0.9, 0.1, 0.1},
    "only a cubic segment keeps its stored control points");

// The linear-equivalent seed really is linear.
const auto seeded = marrow::runtime::Interpolation::cubic_bezier(
    graph::kLinearEquivalentControlPoints[0], graph::kLinearEquivalentControlPoints[1],
    graph::kLinearEquivalentControlPoints[2], graph::kLinearEquivalentControlPoints[3]);
suite.expect(
    near(seeded.transform(0.25), 0.25, 1e-3) && near(seeded.transform(0.75), 0.75, 1e-3),
    "the conversion seed must evaluate identically to linear");

// Varying segment frame.
const auto frame = graph::make_segment_frame(varying_track, 0U, 0U, view);
suite.expect(
    frame.has_value() && !frame->flat_value_span &&
        near(frame->value_span, varying_end_value - varying_start_value, 1e-12),
    "a varying segment must use its own value span");

// Flat segment frame: 1.0 -> 1.0 with pixels_per_value = 0.5.
const auto flat = graph::make_segment_frame(flat_track, 0U, 0U, view);
suite.expect(
    flat.has_value() && flat->flat_value_span &&
        near(*(&flat->value_span), graph::kFlatSegmentHandlePixels / view.pixels_per_value, 1e-12),
    "a flat segment must substitute the positive fallback span");

// Degenerate rejections.
suite.expect(
    !graph::make_segment_frame(zero_duration_track, 0U, 0U, view).has_value(),
    "a zero-duration segment must have no frame");
suite.expect(
    !graph::make_segment_frame(varying_track, 0U, 9U, view).has_value(),
    "an out-of-range component index must have no frame");

// Handle geometry.
const auto handles = graph::build_handle_geometry(
    varying_track, varying_track.keys[0].identity, 0U, view, rect);
suite.expect(
    handles.has_value() &&
        near(handles->first_handle.x,
             graph::x_at_time(rect, view,
                 handles->frame.start_time_seconds +
                     handles->control_points[0] * handles->frame.time_span), 1e-9) &&
        near(handles->first_handle.y,
             graph::y_at_value(rect, view,
                 handles->frame.start_value +
                     handles->control_points[1] * handles->frame.value_span), 1e-9),
    "handle 1 must sit at cx1/cy1 along the frozen frame");
suite.expect(
    !graph::build_handle_geometry(
         varying_track, varying_track.keys.back().identity, 0U, view, rect).has_value(),
    "the last key must expose no outgoing handles");

// Round trip, both handles, varying and flat.
const auto moved = graph::control_points_from_handle_pointer(
    handles->frame, handles->control_points, graph::HandleIndex::Second,
    view, rect, handles->second_handle.x, handles->second_handle.y);
suite.expect(
    moved.has_value() &&
        near((*moved)[2], handles->control_points[2], 1e-9) &&
        near((*moved)[3], handles->control_points[3], 1e-9) &&
        (*moved)[0] == handles->control_points[0] &&
        (*moved)[1] == handles->control_points[1],
    "mapping a handle back onto itself must be identity and must not touch the other handle");

// X clamp, Y overshoot.
const auto clamped_low = graph::control_points_from_handle_pointer(
    handles->frame, handles->control_points, graph::HandleIndex::First,
    view, rect, rect.min_x - 5000.0, handles->first_handle.y);
const auto clamped_high = graph::control_points_from_handle_pointer(
    handles->frame, handles->control_points, graph::HandleIndex::First,
    view, rect, rect.max_x + 5000.0, handles->first_handle.y);
suite.expect(
    clamped_low.has_value() && (*clamped_low)[0] == 0.0 &&
        clamped_high.has_value() && (*clamped_high)[0] == 1.0,
    "a pointer past either end must clamp cx to exactly 0 or 1");
const auto overshoot = graph::control_points_from_handle_pointer(
    handles->frame, handles->control_points, graph::HandleIndex::First,
    view, rect,
    handles->first_handle.x,
    graph::y_at_value(rect, view, handles->frame.start_value - 2.5 * handles->frame.value_span));
suite.expect(
    overshoot.has_value() && near((*overshoot)[1], -2.5, 1e-9),
    "cy must accept finite overshoot without clamping");

// Non-finite and degenerate view rejection.
graph::View broken = view;
broken.pixels_per_value = 0.0;
suite.expect(
    !graph::control_points_from_handle_pointer(
         handles->frame, handles->control_points, graph::HandleIndex::First,
         broken, rect, 300.0, 200.0).has_value() &&
    !graph::control_points_from_handle_pointer(
         handles->frame, handles->control_points, graph::HandleIndex::First,
         view, rect, std::numeric_limits<double>::quiet_NaN(), 200.0).has_value(),
    "a non-positive value scale or a non-finite pointer must reject the mapping");

// Hit test.
suite.expect(
    graph::hit_test_handle(*handles, handles->first_handle.x + 7.0,
                           handles->first_handle.y, 7.0).has_value() &&
    !graph::hit_test_handle(*handles, handles->first_handle.x + 7.5,
                            handles->first_handle.y, 7.0).has_value(),
    "the handle hit test must be inclusive at exactly its radius");
```

Additionally assert that, for a Cubic segment, the geometry's
`first_handle`/`second_handle` and the polyline `build_geometry()` produces for
the same key and view agree at `t = cx1` and `t = cx2` within `1e-9` on the X
axis, proving the render and drag math share one mapping.

- [ ] **Step 2: Build and observe RED**

```bash
cmake --build build --target marrow_timeline_graph_model_tests -j4
```

Expected: compilation fails because `HandleIndex`, `SegmentFrame`,
`HandleGeometry`, `HandleHit`, `kLinearEquivalentControlPoints`,
`seed_control_points`, `make_segment_frame`, `build_handle_geometry`,
`hit_test_handle`, and `control_points_from_handle_pointer` do not exist.

- [ ] **Step 3: Implement the pure handle math**

Add the declarations from design spec §11.1 to
`src/editor/timeline_graph_model.hpp` (the header gains `#include <array>` if it
does not already have it) and implement them in `timeline_graph_model.cpp`:

`make_segment_frame(track, key_index, component_index, view)`:

```text
reject when key_index + 1 >= track.keys.size()
reject when component_index >= track.components.size()
reject when any of start/end time or start/end value is non-finite
time_span = end_time - start_time
reject when !(time_span > kMinimumSegmentSeconds)
raw_span = end_value - start_value
flat = !std::isfinite(raw_span) ||
       std::abs(raw_span) * view.pixels_per_value < kMinimumSegmentValuePixels
value_span = flat ? (kFlatSegmentHandlePixels / view.pixels_per_value) : raw_span
reject when !std::isfinite(value_span) || value_span == 0.0
```

`build_handle_geometry(track, active_key, component_index, view, rect)`:
resolve `active_key` to an index with `timeline_model::key_index()`; build the
frame; take `segment_kind(key.outgoing_easing)`; take
`seed_control_points(kind, stored_control_points)`; compute the two anchors and
the two handle points through `x_at_time()` / `y_at_value()`; return
`std::nullopt` if any coordinate is non-finite. Handle points are **not**
clipped to `rect` — the caller clips drawing and gates hit testing on plot
containment.

`control_points_from_handle_pointer()`: per design spec §7.2. Compute `cx_raw`
and `cy`; reject non-finite; `cx = std::clamp(cx_raw, 0.0, 1.0)`; copy `current`
and overwrite only the selected handle's pair.

`hit_test_handle()`: `std::hypot` distance to each handle, inclusive `<=`
radius, nearer handle wins, `HandleIndex::First` on an exact tie.

`seed_control_points()`: `Cubic` returns `existing`, everything else returns
`kLinearEquivalentControlPoints`.

- [ ] **Step 4: Run focused and affected tests and observe GREEN**

```bash
cmake --build build --target marrow_timeline_graph_model_tests marrow_editor_shell -j4
./build/marrow_timeline_graph_model_tests
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

Expected: the new case and every existing graph-model case pass, and the
existing shell smokes pass unchanged.

- [ ] **Step 5: Record a no-commit checkpoint**

```bash
git diff --check
git status --short
```

---

### Task 2: Shared `set_keyframe_interpolation()` authoring primitive

**Files:**

- Modify: `include/marrow/editor/authoring.hpp`
- Modify: `src/editor/authoring.cpp`
- Modify: `src/samples/editor_project_smoke.cpp`

**Interfaces:**

- Consumes: `ProjectData`, `TimelineKeySelector`, the existing internal
  `resolve_timeline_key()` and `ResolvedTimelineKey`,
  `runtime::Interpolation::cubic_bezier()`.
- Produces: `TimelineInterpolationResult` and `set_keyframe_interpolation()`
  (design spec §11.2).

- [ ] **Step 1: Add failing project-smoke coverage**

In `src/samples/editor_project_smoke.cpp` add
`bool validate_mar169_graph_interpolation_authoring(const marrow::editor::ProjectLoadResult&)`
and call it from `main()` immediately after
`validate_mar168_graph_scalar_authoring(result)`. It must assert, on a
`ProjectData` copy:

```cpp
const std::string before = marrow::editor::serialize_project(project);

// Linear -> Cubic on spine translate key 0 (a runtime-only track, so the
// caller materializes it first through ensure_transform_timeline_edit).
const auto set = marrow::editor::set_keyframe_interpolation(
    &project,
    {translate_key0_selector},
    marrow::runtime::InterpolationKind::CubicBezier,
    {0.2, -0.4, 0.8, 1.6});
if (!set || !set.changed || set.key_count != 1U || set.changed_key_count != 1U) {
    std::cerr << "MAR-169 could not convert a linear segment to cubic.\n";
    return false;
}
// The key's values and time are byte-identical; only `interpolation` moved.
if (key.time != original_time || key.x != original_x || key.y != original_y ||
    key.angle != original_angle ||
    key.interpolation.kind() != marrow::runtime::InterpolationKind::CubicBezier ||
    key.interpolation.cubic_bezier().cx1 != 0.2f ||
    key.interpolation.cubic_bezier().cy1 != -0.4f ||
    key.interpolation.cubic_bezier().cx2 != 0.8f ||
    key.interpolation.cubic_bezier().cy2 != 1.6f) {
    std::cerr << "MAR-169 interpolation write did not preserve the parent key.\n";
    return false;
}
```

Plus these cases:

1. **Every supported family.** Transform Rotate, Translate, Scale, Shear, Slot
   Color, and Deform selectors each accept a cubic write and preserve every
   other field of the key.
2. **Segment-wide identity.** After writing a curve on one Translate key,
   exactly one `interpolation` field in the whole serialized project differs
   from `before`, and the key's `x` and `y` are unchanged. Assert this by
   counting `"curve"` differences between the two serializations, not by
   inspecting the graph.
3. **Stepped -> Cubic** on `spine` translate key 1 (the fixture's stepped key),
   and **Cubic -> Linear** and **Cubic -> Stepped** on `spine` rotate key 0
   (the fixture's cubic overlay key), asserting the serialized `curve` becomes
   the string form with no control points.
4. **Overshoot round trip.** `[0.2, -0.4, 0.8, 1.6]` survives
   `serialize_project()` -> reload bitwise.
5. **X limits.** `[0.0, 0.0, 1.0, 1.0]` is accepted. Each of `[-1e-6, 0, 0.5, 1]`,
   `[0, 0, 1.0000001, 1]`, `[NaN, 0, 0.5, 1]`, `[inf, 0, 0.5, 1]`,
   `[-inf, 0, 0.5, 1]`, `[0, NaN, 0.5, 1]`, and `[0, 1e300, 0.5, 1]` returns a
   falsy result and leaves `serialize_project(project)` equal to `before`.
   Include a NaN case for `cx` specifically: it proves the finiteness test runs
   before the range test.
6. **Unsupported kinds.** `DrawOrder`, `Event`, and `SlotAttachment` selectors
   each reject atomically.
7. **Structural rejections.** A null project, an empty selector list, a
   duplicated selector, and an unresolvable selector each reject atomically.
8. **No change.** Writing the identical curve twice returns
   `changed == false`, `changed_key_count == 0`, `key_count == 1`, an empty
   `error`, and a byte-identical project. Writing the same curve to a two-key
   selection where only one key differs returns `changed == true` with
   `key_count == 2` and `changed_key_count == 1`.
9. **Save/reload.** Save the edited project to a temporary path, reload it, and
   assert the four control points and the kind survive bitwise. This is the
   direct proof that the primitive cannot write a document the loader rejects.
10. **Export.** The existing `--export-runtime` / `--export-binary` path plus
    `marrow_inspect --compare` covers JSON/MBIN equivalence.

- [ ] **Step 2: Build and observe RED**

```bash
cmake --build build --target marrow_project_smoke -j4
```

Expected: compilation fails because `TimelineInterpolationResult` and
`set_keyframe_interpolation` do not exist.

- [ ] **Step 3: Implement the primitive**

Append the declarations from design spec §11.2 to
`include/marrow/editor/authoring.hpp` after `offset_keyframe_scalars()`, adding
`#include <array>`. Implement in `src/editor/authoring.cpp` immediately after
`offset_keyframe_scalars()`:

```text
1. reject null project, empty selectors
2. if kind == CubicBezier: validate all four control points (see below);
   otherwise ignore control_points entirely
3. ProjectData candidate = *project
4. for each selector:
     reject empty animation name / non-finite or negative time
     resolve_timeline_key(candidate, selector, &error) -> ResolvedTimelineKey
     reject duplicate (kind, timeline_index, key_index)
     reject DrawOrder / Event / SlotAttachment kinds
5. build the runtime::Interpolation once, after validation succeeds
6. changed_key_count = number of resolved keys whose stored interpolation
   differs from the new one
7. if changed_key_count == 0 -> return {{false, {}}, resolved.size(), 0U}
8. write the interpolation into every resolved key of `candidate`
9. *project = std::move(candidate)
   return {{true, {}}, resolved.size(), changed_key_count}
```

Control-point validation, in this exact order for each of the four values:

```cpp
if (!std::isfinite(v)) return reject("Bezier control points must be finite.");
if (std::abs(v) > static_cast<double>(
        std::numeric_limits<marrow::runtime::AnimationScalar>::max())) {
    return reject("Bezier control points must fit the runtime float32 range.");
}
const double narrowed =
    static_cast<double>(static_cast<marrow::runtime::AnimationScalar>(v));
if (!std::isfinite(narrowed)) return reject(...);
// cx1 (index 0) and cx2 (index 2) only:
if (v < 0.0 || v > 1.0 || narrowed < 0.0 || narrowed > 1.0) {
    return reject("Bezier x control points must stay within [0, 1].");
}
```

The finiteness test **must** precede the range test, because `NaN < 0.0` and
`NaN > 1.0` are both false. Reuse the loaders' wording so the error string
matches `src/editor/project.cpp` and `src/runtime/skeleton_parse.cpp`.

Add a file-local comparison helper, because `runtime::Interpolation` has no
`operator==`:

```cpp
bool same_interpolation(
    const marrow::runtime::Interpolation& left,
    const marrow::runtime::Interpolation& right) {
    if (left.kind() != right.kind()) return false;
    if (left.kind() != marrow::runtime::InterpolationKind::CubicBezier) return true;
    const auto& l = left.cubic_bezier();
    const auto& r = right.cubic_bezier();
    return l.cx1 == r.cx1 && l.cy1 == r.cy1 && l.cx2 == r.cx2 && l.cy2 == r.cy2;
}
```

Supported selector kinds, rejecting everything else:

| Selector kind | Written field |
| --- | --- |
| `Transform` (all four channels) | `TransformKeyframeEdit::interpolation` |
| `SlotColor` | `SlotColorKeyframeEdit::interpolation` |
| `Deform` | `DeformKeyframeEdit::interpolation` |
| `DrawOrder`, `Event`, `SlotAttachment` | rejected — no such field exists |

The step-7 "no change" return mirrors `retime_keyframes()` and
`offset_keyframe_scalars()`: falsy, no error string, counts populated. Callers
treat it as a no-op frame, not a failure.

- [ ] **Step 4: Run the project smoke and observe GREEN**

```bash
cmake --build build --target marrow_project_smoke -j4
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

Expected: every new MAR-169 authoring case and all existing project-smoke cases
pass.

- [ ] **Step 5: Confirm the surface has not drifted yet**

```bash
./build/marrow_agent_dispatch_smoke
./build/marrow_c_smoke
git diff --stat -- include/marrow/c_api src/c_api
git diff -- include/marrow/editor/authoring.hpp
git diff --check
git status --short
```

Expected: the registry is still exactly 56 (the agent operation arrives in
Task 6), the C ABI paths show zero changes, the `authoring.hpp` diff contains
only added lines, and only `authoring.hpp`, `authoring.cpp`, and
`editor_project_smoke.cpp` appear beyond Task 1's files.

---

### Task 3: Shell handle gesture in `timeline_controller`

**Files:**

- Modify: `src/editor/shell_state.hpp`
- Modify: `src/editor/shell_core.cpp`
- Modify: `src/editor/timeline_controller.hpp`
- Modify: `src/editor/timeline_controller.cpp`
- Modify: `src/editor/shell_smoke_graph.cpp`
- Modify: `src/editor/shell_smoke_scenarios.hpp`
- Modify: `src/editor/shell_smoke.cpp`

**Interfaces:**

- Consumes: `set_keyframe_interpolation()`, `visit_editable_timeline_keys()`,
  `timeline_key_selector()`, `timeline_key_index()`,
  `timeline_model::completion_decision()`.
- Produces: `GraphDragTarget`, `TimelineGraphHandleGesture`, the
  `TimelineEditorState::graph_handle_gesture` field, and the trio from design
  spec §11.3.

- [ ] **Step 1: Add the failing headless easing smoke**

In `src/editor/shell_smoke_graph.cpp` add

```cpp
bool validate_timeline_graph_easing_shell_smoke(
    const std::filesystem::path& project_path);
```

declared in `src/editor/shell_smoke_scenarios.hpp` and called from
`run_headless_smoke()` in `src/editor/shell_smoke.cpp` immediately after
`validate_timeline_graph_edit_shell_smoke()`, with its own isolated
`ShellState`/session.

For this task the scenario covers only the gesture trio, driven directly:

```cpp
const std::string project_before =
    marrow::editor::serialize_project(*state.session.project());
const std::size_t undo_before = state.session.undo_count();

state.selected_timeline_track_id = translate->id;
state.timeline_editor.selected_keys = {first_key};
state.timeline_editor.active_key = first_key;
state.timeline_editor.graph_view.active_component = GraphComponent::Y;

const auto& projection = cached_timeline_graph_projection(&state, *translate);
const auto handles = timeline_graph_model::build_handle_geometry(
    *projection.track, first_key, /*component_index=*/1U,
    state.timeline_editor.graph_view.view, plot);
if (!handles.has_value() ||
    handles->kind != timeline_graph_model::SegmentKind::Linear) {
    std::cerr << "Graph easing smoke expected a linear outgoing segment.\n";
    return false;
}

if (!begin_timeline_graph_handle_gesture(
        &state, 4343U, *translate, first_key,
        timeline_graph_model::HandleIndex::First, handles->frame,
        handles->control_points,
        marrow::runtime::InterpolationKind::Linear, tracks) ||
    !authoring_gesture_active(state) ||
    state.session.undo_count() != undo_before) {
    std::cerr << "Beginning a handle gesture must open exactly one transaction.\n";
    return false;
}
if (!apply_timeline_graph_handle_control_points(
        &state, tracks, {0.2, -0.4, 0.8, 1.6})) {
    std::cerr << "Applying handle control points failed.\n";
    return false;
}
finish_timeline_graph_handle_gesture(&state, true);
if (state.session.undo_count() != undo_before + 1U) {
    std::cerr << "One handle gesture must produce exactly one history entry.\n";
    return false;
}
```

Add these cases:

- **criterion 3, direct**: after the commit above, rebuild the projection and
  assert that the segment for component **X** (index 0) reports
  `SegmentKind::Cubic` with control points `{0.2, -0.4, 0.8, 1.6}`, identical to
  component **Y** (index 1), and that the key's `x`, `y`, and `time` are
  byte-identical to the snapshot;
- **Stepped -> Cubic** on the fixture's stepped translate key, and a case on the
  already-cubic `spine` rotate overlay key that keeps its stored points when
  seeded;
- **flat segment**: on `slot:0:Color` component Blue (index 2), whose first
  segment is `1.0 -> 1.0`, assert `frame.flat_value_span` is true, the gesture
  succeeds, and the resulting `cy1` equals the requested value;
- **no change cancels**: begin a gesture on the already-cubic rotate key, apply
  its exact original control points, finish with `commit = true`, and assert
  `undo_count()` is unchanged and the project is byte-identical;
- **atomic cancel**: begin, apply an overshoot curve, then
  `finish_timeline_graph_handle_gesture(&state, false)`, and assert
  `serialize_project()`, `undo_count()`, `redo_count()`, `project_revision()`,
  `dirty()`, the rebuilt dopesheet `key_times`, every rebuilt graph
  `Key::values`, every rebuilt `Segment::kind`, and every rebuilt
  `outgoing_easing` control point match the pre-gesture snapshot;
- **rejection cancels atomically**: apply `{1.5, 0.0, 0.5, 1.0}` and assert the
  gesture is gone, the project is byte-identical, and
  `state.error_message` is non-empty;
- **materialization**: the first gesture on the runtime-only `spine` Translate
  track copies all three keys into
  `project->transform_timeline_edits` rather than replacing the track;
- **begin fails closed**: a non-editable row, an empty active key, a key on
  another track, and `authoring_gesture_active(state) == true` each return
  false without opening a transaction;
- **undo/redo**: undo restores the previous easing and redo re-applies it, with
  `selected_keys` and `active_key` bit-identical throughout;
- **source adoption**: `state.timeline_editor = TimelineEditorState{};` clears
  `graph_handle_gesture`;
- `marrow::editor::agent_operation_descriptor_count() == 56` before and after
  every case in this task. Task 6 flips it to 57.

- [ ] **Step 2: Build and observe RED**

```bash
cmake --build build --target marrow_editor_shell -j4
```

Expected: compilation fails because `TimelineGraphHandleGesture`,
`graph_handle_gesture`, and the three gesture functions do not exist.

- [ ] **Step 3: Implement the state and the trio**

In `src/editor/shell_state.hpp`, add `GraphDragTarget` and
`TimelineGraphHandleGesture` (design spec §11.5), add
`std::optional<TimelineGraphHandleGesture> graph_handle_gesture;` to
`TimelineEditorState`, and add `state.timeline_editor.graph_handle_gesture.has_value()`
to `authoring_gesture_active()`.

In `src/editor/shell_core.cpp`, add
`cancel_transaction_gesture(state->timeline_editor.graph_handle_gesture);`
to `cancel_authoring_gestures()`, next to the MAR-168 value gesture, keeping the
list in step with `authoring_gesture_active()` as the existing comment requires.

In `timeline_controller.cpp`:

`begin_timeline_graph_handle_gesture()`:

1. reject a null state, `authoring_gesture_active()`, a non-editable track, a
   key that does not resolve to an index in `track`, a key index that is the
   last key, and a non-finite frame;
2. `state->session.begin_edit({EditKind::EditProperty, "Edit key easing",
   "timeline:graph-easing", false, Project | Runtime | Preview})`; on failure set
   `state->error_message` and return false;
3. populate the gesture, including `original_kind`, `original_control_points`,
   and `applied_control_points = seed`;
4. `state->timeline_editor.graph_handle_gesture.emplace(std::move(gesture))`.

`apply_timeline_graph_handle_control_points()`, mirroring
`apply_timeline_graph_value_delta()` step for step:

1. return false when there is no live gesture;
2. return true when every requested value equals the applied value within
   `1e-12`;
3. return false with a cancel when any requested value is non-finite;
4. on the first application, materialize with
   `visit_editable_timeline_keys(state, row, [](auto&) {})`; a failure cancels
   with `"Could not materialize the selected timeline key"`;
5. re-resolve the key index with `timeline_key_index()`; a lost identity cancels
   with `"The selected graph key changed during editing"`;
6. build the `TimelineKeySelector` with `timeline_key_selector()`; a failure
   cancels;
7. call `set_keyframe_interpolation(gesture.transaction.project(), {selector},
   InterpolationKind::CubicBezier, requested)`; a falsy result **with a
   non-empty error** cancels and surfaces it; a falsy result with an empty error
   is a no-change frame — update `applied_control_points` and return true;
8. `gesture.transaction.refresh_runtime()`; failure cancels;
9. `sync_shell_from_editor_session(state)`;
10. `gesture.applied_control_points = requested; gesture.changed = true;`.

`finish_timeline_graph_handle_gesture()`: use
`timeline_model::completion_decision(commit, gesture.changed)`; on `Cancel`,
`transaction.cancel()`, `sync_shell_from_editor_session()`, and set
`"Cancelled easing edit"` when `report_cancelled`; on `Commit`,
`transaction.commit()`, `sync_shell_from_editor_session()`, then
`"Edited key easing"` or `"Easing edit failed"`.

- [ ] **Step 4: Run the shell smoke and observe GREEN**

```bash
cmake --build build --target marrow_editor_shell -j4
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

Expected: every new easing case passes and the MAR-167/MAR-168 graph scenarios
are unchanged.

- [ ] **Step 5: Record a no-commit checkpoint**

```bash
git diff --check
git status --short
```

---

### Task 4: Handle branch of the graph drag driver

**Files:**

- Modify: `src/editor/shell_timeline_graph.hpp`
- Modify: `src/editor/shell_timeline_graph.cpp`
- Modify: `src/editor/shell_smoke_graph.cpp`

**Interfaces:**

- Consumes: Task 1's handle math, Task 3's gesture trio, and MAR-168's
  `TimelineGraphPointDrag` / `update_timeline_graph_point_drag()` /
  `cancel_timeline_graph_point_drag()`.
- Produces: `begin_timeline_graph_handle_drag()` (design spec §11.4) and the
  `Handle` branch inside `update_timeline_graph_point_drag()`.

- [ ] **Step 1: Extend the headless smoke with full handle drags**

In `validate_timeline_graph_easing_shell_smoke()` add, driving the driver
directly with no ImGui frame:

```cpp
// A press on a handle must arm a candidate without a transaction.
if (!begin_timeline_graph_handle_drag(
        &state, *translate, *handles, timeline_graph_model::HandleIndex::First,
        4343U, plot, view, handles->first_handle.x, handles->first_handle.y) ||
    authoring_gesture_active(state) ||
    state.session.undo_count() != undo_before) {
    std::cerr << "A handle press must arm a candidate without a transaction.\n";
    return false;
}
// Inside the dead zone nothing starts.
(void)update_timeline_graph_point_drag(
    &state, tracks, handles->first_handle.x + 2.0, handles->first_handle.y + 1.0,
    true, false, false);
if (state.timeline_editor.graph_handle_gesture.has_value()) {
    std::cerr << "A handle drag inside the dead zone must not open a gesture.\n";
    return false;
}
// Leaving the dead zone opens the handle gesture and no other gesture.
(void)update_timeline_graph_point_drag(
    &state, tracks, handles->first_handle.x + 30.0, handles->first_handle.y - 40.0,
    true, false, false);
if (!state.timeline_editor.graph_handle_gesture.has_value() ||
    state.timeline_editor.retime_gesture.has_value() ||
    state.timeline_editor.graph_value_gesture.has_value()) {
    std::cerr << "A handle drag must open only the handle gesture.\n";
    return false;
}
```

Add these cases:

- a diagonal handle drag changes **both** `cx` and `cy` in one frame, proving no
  axis lock is applied to handles;
- an X-clamped drag: push the pointer 400 px past the end anchor, assert the
  stored `cx1` is exactly `1.0` and the gesture is still live;
- an overshoot drag producing `cy1 < 0` and a second producing `cy2 > 1`;
- `pointer_down = false` commits and clears both the candidate and the gesture;
- `cancel_requested = true` and `cancel_timeline_graph_point_drag()` both roll
  back to a byte-identical project, history, revision, dopesheet `key_times`,
  graph values, graph segment kinds, and graph control points;
- a forced non-finite pointer cancels with rollback;
- a press on a handle while `authoring_gesture_active(state)` arms no candidate;
- a press on a non-editable or unsupported focused row arms no candidate;
- the active key changing mid-drag cancels with rollback;
- **hit priority**: with a handle deliberately placed within 8 px of a key
  point, `hit_test_handle()` wins and the MAR-168 point path never arms.

- [ ] **Step 2: Build and observe RED**

```bash
cmake --build build --target marrow_editor_shell -j4
```

Expected: compilation fails because `begin_timeline_graph_handle_drag` does not
exist.

- [ ] **Step 3: Implement the driver branch**

In `shell_timeline_graph.cpp`:

`begin_timeline_graph_handle_drag()` — reject a null state,
`authoring_gesture_active()`, a non-editable track, a projection that is not
`Ready`, a `HandleGeometry` whose key is not the current `active_key`, and any
non-finite pointer/view input. Otherwise populate the shared
`TimelineGraphPointDrag` with `target = GraphDragTarget::Handle`, the frozen
`view`/`plot`, the chosen `HandleIndex`, `frame`, `seed_control_points`,
`segment_kind`, the pressed key, the component and its index, the press pointer
coordinates, and `axis = DragAxis::Undecided` (unused for handles).

In `update_timeline_graph_point_drag()`, add the `Handle` branch:

```text
if no candidate -> return false
if cancel_requested:
    finish the live gesture with commit = false
    reset the candidate; return false
if !pointer_down:
    finish the live gesture with commit = true
    reset the candidate; return false
resolve the row from `tracks` by graph_drag->track_id; a missing row, a
    projection that is no longer Ready, a changed active key, or a changed
    resolved component cancels with rollback

if target == Handle:
    if no handle gesture is live:
        if max(|dx|, |dy|) < 4.0 -> return true        // shared dead zone
        if !begin_timeline_graph_handle_gesture(
               state, item_id, row, pressed_key, handle, frame,
               seed_control_points, segment_kind, tracks)
            -> reset candidate; return false
    control_points = control_points_from_handle_pointer(
        frame, gesture.applied_control_points, handle,
        frozen_view, frozen_plot, pointer_x, pointer_y)
    if !control_points ->
        finish_timeline_graph_handle_gesture(state, false); reset; return false
    if !apply_timeline_graph_handle_control_points(state, tracks, *control_points)
        -> reset candidate; return false               // apply_* already cancelled
    return true
else:
    ... unchanged MAR-168 axis-lock branch ...
```

The dead-zone comparison reuses whatever MAR-168 built (either
`decide_drag_axis()`'s `dead_zone_pixels` constant or an extracted helper). Do
not duplicate the constant; if MAR-168 left it file-local, promote it to a named
constant in `timeline_graph_model.hpp` and have both branches use it.

`cancel_timeline_graph_point_drag()` gains
`finish_timeline_graph_handle_gesture(state, false)` alongside its existing
value/retime cancels.

Note `control_points_from_handle_pointer()` is fed
`gesture.applied_control_points`, not the seed, so a second frame that moves the
*other* handle would carry the first handle's committed position. Within one
gesture only one handle moves, so this is equivalent; using the applied set
keeps the invariant that the untouched pair always reflects what is stored.

- [ ] **Step 4: Run the shell smoke and observe GREEN**

```bash
cmake --build build --target marrow_editor_shell -j4
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

Expected: every handle-drag case passes and the MAR-167/MAR-168 graph scenarios
are unchanged.

- [ ] **Step 5: Record a no-commit checkpoint**

```bash
git diff --check
git status --short
```

---

### Task 5: ImGui wiring, handle rendering, and the actual-frame smoke

**Files:**

- Modify: `src/editor/shell_timeline_graph.hpp`
- Modify: `src/editor/shell_timeline_graph.cpp`
- Modify: `src/editor/shell_smoke_frames.cpp`

**Interfaces:**

- Consumes: Tasks 1, 3, and 4.
- Produces: the `TimelineGraphRenderStats` additions from design spec §11.7 and
  the drawn handles.

- [ ] **Step 1: Add failing actual-frame coverage**

In `src/editor/shell_smoke_frames.cpp`, extend the graph section with:

```cpp
// Handles are drawn for the active key's outgoing segment.
shell_state.selected_timeline_track_id = translate_track->id;
shell_state.timeline_editor.active_key = first_translate_key;
shell_state.timeline_editor.graph_view.active_component =
    timeline_graph_model::Component::Y;
render_graph_frame(&handle_stats);
render_graph_frame(&handle_stats);
if (!handle_stats.handles_drawn ||
    !std::isfinite(handle_stats.first_handle_x) ||
    !std::isfinite(handle_stats.first_handle_y) ||
    handle_stats.active_segment_kind != timeline_graph_model::SegmentKind::Linear) {
    std::cerr << "Actual-frame graph smoke did not draw the active key handles.\n";
    return false;
}
// Pressing the drawn handle and moving must open the handle gesture, not a
// point gesture.
press_and_move(ImVec2(handle_stats.first_handle_x, handle_stats.first_handle_y),
               ImVec2(handle_stats.first_handle_x + 26.0f,
                      handle_stats.first_handle_y - 34.0f),
               &handle_stats);
if (!handle_stats.handle_gesture_active ||
    shell_state.timeline_editor.graph_value_gesture.has_value() ||
    shell_state.timeline_editor.retime_gesture.has_value()) {
    std::cerr << "An actual-frame handle drag did not open the handle gesture.\n";
    return false;
}
```

Plus:

- pressing on a rendered key point where no handle is within the handle radius
  still opens MAR-168's point path;
- the component checkboxes and `Fit` are inert while a handle drag is live
  (click each and assert `component_visible` and the fitted view are unchanged);
- wheel zoom and middle-drag pan are inert while a handle drag is live;
- switching to the Dopesheet tab mid-drag cancels with rollback, and the
  dopesheet still retimes normally afterwards.

- [ ] **Step 2: Build and observe RED**

```bash
cmake --build build --target marrow_editor_shell -j4
```

Expected: compilation fails because the new `TimelineGraphRenderStats` fields do
not exist.

- [ ] **Step 3: Implement the ImGui layer**

In `shell_timeline_graph.hpp`, add the render-stat fields from design spec
§11.7.

In `draw_timeline_graph_body()`:

1. Resolve the handle component once, through a shared helper used by both the
   geometry call and the press branch: `graph_view.active_component` when set
   and visible, otherwise the lowest-indexed visible component.
2. Wrap the component checkboxes and the `Fit` button in
   `ImGui::BeginDisabled(graph_drag_or_gesture_live)` /
   `ImGui::EndDisabled()`, where `graph_drag_or_gesture_live` covers MAR-168's
   candidate and value gesture as well as MAR-169's handle gesture.
3. After `build_geometry()`, call `build_handle_geometry()` with the active key
   and the resolved component index, and publish
   `handles_drawn`, `first_handle_x/y`, `second_handle_x/y`,
   `handle_flat_value_span`, and `active_segment_kind`.
4. In the left-click branch, run `hit_test_handle()` **before**
   `timeline_graph_model::hit_test()`, gated on `plot_hovered`. On a handle hit,
   call `begin_timeline_graph_handle_drag()` and do **not** call
   `activate_timeline_graph_point()` — a handle press must not change the
   selection or scrub the playhead.
5. Draw, inside the existing `PushClipRect` and after the segments but before the
   key points: two tangent lines
   (`IM_COL32(0x9a, 0xd8, 0xff, 0x80)`, 1.0 px) from `start_anchor` to
   `first_handle` and from `end_anchor` to `second_handle`, and two filled 4.0 px
   squares (`IM_COL32(0x9a, 0xd8, 0xff, 0xff)`) at the handle positions.
6. Append one sentence to the existing shared-easing notice: dragging a handle
   edits that one shared curve for every component of the key.
7. While a handle gesture is live, draw the readout line
   `Easing  <original kind> -> [%.3f, %.3f, %.3f, %.3f]`, appending
   `  (X clamped)` when the clamp bound this frame and
   `  (flat segment: 100 px = 1.0)` when `frame.flat_value_span` is set.
8. Publish `handle_gesture_active`.

Keep every existing MAR-167/MAR-168 behaviour byte-identical. No clamping and no
pixel-to-control-point arithmetic may appear in this file.

In `src/editor/shell_timeline.cpp`, extend the existing tab-switch cancel so a
`requested_view_mode` transition away from Graph also cancels a live handle
candidate or gesture. If MAR-168 already routes this through
`cancel_timeline_graph_point_drag()`, Task 4's change covers it and no edit is
needed here — verify rather than assume.

- [ ] **Step 4: Run the display gates and observe GREEN**

```bash
cmake --build build --target marrow_editor_shell -j4
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
cmake --build build-display -j4 && ctest --test-dir build-display --output-on-failure
```

Expected: the actual-frame handle cases pass and the display suite is unchanged
in count.

- [ ] **Step 5: Record a no-commit checkpoint**

```bash
git diff --check
git status --short
```

---

### Task 6: The 57th Agent operation `timeline.set_interpolation`

**Files:**

- Modify: `src/editor/agent_dispatch.cpp`
- Modify: `src/editor/agent_dispatch_internal.hpp`
- Modify: `src/editor/agent_handlers_editing.cpp`
- Modify: `src/samples/agent_dispatch_smoke.cpp`
- Modify: `src/editor/shell_smoke_graph.cpp` (56 -> 57 assertions)

**Interfaces:**

- Consumes: `set_keyframe_interpolation()`, `commit_or_error()`,
  `ensure_transform_timeline_edit()`, `ensure_mesh_deform_timeline_edit()`,
  `ensure_slot_color_timeline_edit()`.
- Produces: one registry row, `interpolation_request_arg()`, and one handler
  branch.

- [ ] **Step 1: Add the failing agent expectation and behaviour**

In `src/samples/agent_dispatch_smoke.cpp`:

```cpp
constexpr std::array<OperationExpectation, 57> kExpectedOperations{{
    ...
    {"timeline.retime_keyframes", "edit", true, false, true},
    {"timeline.set_interpolation", "edit", true, false, true},   // NEW
    ...
}};
```

and add behavioural coverage in the same style the file already uses for
`timeline.retime_keyframes`:

- dry run leaves `project_revision()`, `undo_count()`, and `dirty()` unchanged
  and reports `previous_interpolation` for every selected key;
- a live cubic write on a Transform key succeeds with `changed_key_count == 1`
  and adds exactly one history entry;
- a second identical live call returns error code `no_change`;
- `undo` restores the previous curve, verified by a follow-up dry run whose
  `previous_interpolation` is the original;
- a Slot Color key and a Deform key each succeed;
- rejections, each asserting `ok == false` and an unchanged project:
  `interpolation` missing; `[1.5, 0, 0.5, 1]`; `[0, 0, 0.5]` (wrong length);
  `"quadratic"`; a `draw_order` key; a `slot_attachment` key; an empty `keys`
  array; a duplicated key; an unresolvable key (`not_found`).

Also update the `56` assertions in `src/editor/shell_smoke_graph.cpp` — both the
MAR-167 scenario and whatever MAR-168 added — to `57`, using the list you
recorded in Task 0 Step 2.

- [ ] **Step 2: Build and observe RED**

```bash
cmake --build build --target marrow_agent_dispatch_smoke marrow_editor_shell -j4
./build/marrow_agent_dispatch_smoke
```

Expected: the smoke fails because the registry has 56 rows and
`timeline.set_interpolation` is unknown.

- [ ] **Step 3: Implement the registry row and the parser helper**

In `src/editor/agent_dispatch.cpp`, insert into `kOperationSpecs[]`
**immediately after** the `timeline.retime_keyframes` row:

```cpp
{"timeline.set_interpolation", "edit", true, false, true, true, &handle_editing_operation},
```

Add `interpolation_request_arg()` next to the existing `interpolation_arg()`,
declared in `agent_dispatch_internal.hpp`:

```cpp
bool interpolation_request_arg(
    const json::Value& args,
    std::string_view name,
    marrow::runtime::InterpolationKind* kind_out,
    std::array<double, 4>* control_points_out,
    std::string* error_out);
```

It accepts `"linear"`, `"stepped"`, or a 4-number array, and returns the raw
doubles **without** constructing a `runtime::Interpolation`, so rejected input
never enters the process-wide LUT cache. A missing member is an error
(`"<name> is required and must be linear, stepped, or a 4-number bezier
array."`). Leave `interpolation_arg()` byte-identical.

- [ ] **Step 4: Implement the handler**

In `src/editor/agent_handlers_editing.cpp`, add a
`if (op == "timeline.set_interpolation") { ... }` block to
`handle_timeline_editing_operation()`, immediately after the
`timeline.retime_keyframes` block. Reuse that block's key-parsing shape exactly,
restricted to `transform`, `deform`, and `slot_color`:

```text
1. require an args object
2. require keys(array, 1..4096)
3. parse each key entry into a TimelineKeySelector; reject unsupported kinds
   with "timeline.set_interpolation does not support <kind> keys."
4. interpolation_request_arg(*args, "interpolation", &kind, &points, &error)
5. apply = [&](ProjectData* project) {
       for each selector: ensure_transform_timeline_edit /
           ensure_mesh_deform_timeline_edit / ensure_slot_color_timeline_edit
       return set_keyframe_interpolation(project, selectors, kind, points);
   }
6. response_delta(result, dry_run, previous_curves) builds the payload from
   design spec §13.4, capping `keys` at 256 entries and setting
   `keys_truncated` accordingly
7. dry_run: ProjectData candidate = *session.project(); collect the previous
   curves from `candidate` before applying; apply; report; discard
8. live: begin_edit({EditKind::EditProperty,
       selectors.size() == 1U ? "Set timeline key easing via Agent"
                              : "Set timeline key easings via Agent",
       "timeline:interpolation", false,
       Project | Runtime | Preview});
   collect previous curves; apply; cancel + error on failure;
   cancel + "No changes made." / "no_change" when !result.changed;
   commit_or_error(transaction, op, spec,
       CommitPolicy{"Failed to set timeline key easing: "});
   make_success("Set timeline key easing successfully.", ...)
```

Add a file-local `interpolation_curve_value(const runtime::Interpolation&)`
returning the `.marrow`/`.mskl` `curve` encoding as a `json::Value` — the string
form for Linear/Stepped, the 4-number array for Cubic — used for both the
request echo and `previous_interpolation`.

Collect `previous_interpolation` from the **materialized candidate**, after the
`ensure_*` calls and before `set_keyframe_interpolation()`, so a runtime-only
track reports the runtime curve rather than "not found".

- [ ] **Step 5: Run the agent gates and observe GREEN**

```bash
cmake --build build --target marrow_agent_dispatch_smoke marrow_editor_shell -j4
./build/marrow_agent_dispatch_smoke
./build/marrow_agent_socket_tests
./build/marrow_c_smoke
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

Expected: the registry is exactly 57, every new behavioural case passes, the
socket tests are 4/4, the C ABI smoke is unchanged, and the shell smokes pass
with their updated 57 assertions.

- [ ] **Step 6: Prove no existing operation changed**

```bash
git diff -- src/editor/agent_dispatch.cpp
git diff -- src/editor/agent_handlers_editing.cpp
git diff --stat -- include/marrow/c_api src/c_api
git diff --check
git status --short
```

Expected: the `agent_dispatch.cpp` diff contains only the one added registry row
and the added parser helper; the `agent_handlers_editing.cpp` diff contains only
the added handler block and the added curve helper; the C ABI paths show zero
changes.

---

### Task 7: The matching Python MCP tool

**Files:**

- Modify: `tools/mcp/tools/editing.py`
- Modify: `tools/mcp/test_client.py`

**Interfaces:**

- Consumes: the Task 6 operation.
- Produces: `_timeline_interpolation_key_schema()`,
  `_bezier_interpolation_schema()`, and one `types.Tool`.

- [ ] **Step 1: Add the failing parity and behaviour coverage**

In `tools/mcp/test_client.py`:

```python
new_edit_operations = {
    "animation.create",
    "animation.duplicate",
    "animation.rename",
    "animation.delete",
    "animation.set_duration",
    "timeline.retime_keyframes",
    "timeline.set_interpolation",
}
...
assert len(registry_names) == 57
assert len(registry_names) == len(set(registry_names))
assert len(mcp_names) == 57
assert len(mcp_names) == len(set(mcp_names))
assert set(registry_names) == set(mcp_names)
...
assert registry_by_name["timeline.set_interpolation"] == {
    "name": "timeline.set_interpolation",
    "category": "edit",
    "mutating": True,
    "requires_review": False,
    "dry_run_supported": True,
}
```

and, next to the existing `timeline.retime_keyframes` dry-run block, add the
full sequence:

```python
interpolation_key = {
    "kind": "transform",
    "animation": "idle",
    "bone": "spine",
    "channel": "translate",
    "time": 0.0,
}

before = require_ok(
    "timeline.set_interpolation dry-run",
    await client.send_command(
        "timeline.set_interpolation",
        {"keys": [interpolation_key], "interpolation": [0.2, -0.4, 0.8, 1.6],
         "dry_run": True},
    ),
)
assert before["scene_delta"]["key_count"] == 1
original_curve = before["scene_delta"]["keys"][0]["previous_interpolation"]

require_ok(
    "timeline.set_interpolation",
    await client.send_command(
        "timeline.set_interpolation",
        {"keys": [interpolation_key], "interpolation": [0.2, -0.4, 0.8, 1.6]},
    ),
)
after = require_ok(
    "timeline.set_interpolation read-back",
    await client.send_command(
        "timeline.set_interpolation",
        {"keys": [interpolation_key], "interpolation": ["linear"][0],
         "dry_run": True},
    ),
)
stored = after["scene_delta"]["keys"][0]["previous_interpolation"]
assert [round(value, 4) for value in stored] == [0.2, -0.4, 0.8, 1.6]

require_rejected(
    "timeline.set_interpolation rejects out-of-range x",
    await client.send_command(
        "timeline.set_interpolation",
        {"keys": [interpolation_key], "interpolation": [1.5, 0.0, 0.8, 1.0]},
    ),
)
require_rejected(
    "timeline.set_interpolation rejects draw_order keys",
    await client.send_command(
        "timeline.set_interpolation",
        {"keys": [{"kind": "draw_order", "animation": "idle", "time": 0.0}],
         "interpolation": "linear"},
    ),
)

require_ok("undo timeline interpolation", await client.send_command("undo"))
restored = require_ok(
    "timeline.set_interpolation after undo",
    await client.send_command(
        "timeline.set_interpolation",
        {"keys": [interpolation_key], "interpolation": "linear", "dry_run": True},
    ),
)
assert restored["scene_delta"]["keys"][0]["previous_interpolation"] == original_curve
```

Adjust the read-back call's `interpolation` argument to any valid value; the
dry run reports `previous_interpolation` regardless of what is requested, which
is the point of the read-back channel.

- [ ] **Step 2: Observe RED**

```bash
tools/mcp/venv/bin/python -m py_compile tools/mcp/test_client.py
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
kill %1
```

Expected: the parity assertion fails because `editing.get_tools()` still returns
56 names in total with `inspection.get_tools()`.

- [ ] **Step 3: Implement the MCP tool**

In `tools/mcp/tools/editing.py`, add the two helper schemas from design spec
§13.6 next to `_timeline_retime_key_schema()`, and add the tool immediately
after `timeline.retime_keyframes` in `get_tools()`:

```python
types.Tool(
    name="timeline.set_interpolation",
    description=(
        "Replace the outgoing easing of timeline keys. The easing is shared by "
        "every component of a key, so this never creates per-component curves. "
        "Bezier x control points must stay in [0, 1]; finite y overshoot is "
        "allowed. A dry run reports each key's current curve without mutating."
    ),
    inputSchema={
        "type": "object",
        "properties": {
            "keys": {
                "type": "array",
                "items": _timeline_interpolation_key_schema(),
                "minItems": 1,
                "maxItems": 4096,
            },
            "interpolation": _bezier_interpolation_schema(),
            "dry_run": {"type": "boolean"},
        },
        "required": ["keys", "interpolation"],
    },
),
```

Leave `_interpolation_schema()` untouched so the other tools keep their current
contract.

- [ ] **Step 4: Run the MCP gates and observe GREEN**

```bash
tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
kill %1
```

Expected: `py_compile` passes and the end-to-end client run passes, including
57/57 parity and the new behavioural sequence.

- [ ] **Step 5: Record a no-commit checkpoint**

```bash
git diff --check
git status --short
```

---

### Task 8: Full validation, documentation, and MAR-169 closure

**Files:**

- Modify: `AGENTS.md`
- Modify: `docs/root1/discription.md`
- Modify: `docs/root1/quick-start.md`
- Modify: `docs/root1/concepts.md`
- Modify: `docs/root1/editing-gap-analysis.md`
- Modify: `docs/root1/refector.md`
- Modify: `docs/root1/platform-validation.md` (only the stale "next milestone" line)
- Modify: `docs/root1/format-spec.md`
- Modify: `docs/superpowers/specs/2026-08-30-mar-169-graphical-bezier-handles-design.md`
- Modify: `.agents/tasks/prd-marrow-runtime.json`

**Interfaces:**

- Consumes: completed Tasks 0-7 and fresh command output.
- Produces: synchronized documentation, exact validation evidence,
  `MAR-169 status=done`, and `MAR-170 status=open`.

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

Expected: every command exits zero. MAR-169 registers no new CTest, so the
default suite should still report the MAR-168 count; confirm with
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

Expected: both display-enabled suites pass with the MAR-168 counts, including
the three display tests. Label the evidence automated; claim no manual UI or
Windows qualification.

- [ ] **Step 3: Run export/runtime compatibility gates**

```bash
./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar169.mskl --export-binary /tmp/marrow_mar169.mbin
./build/marrow_inspect --compare /tmp/marrow_mar169.mbin /tmp/marrow_mar169.mskl
./build/marrow_fixture_smoke /tmp/marrow_mar169.mskl /tmp/player_idle.matl
python3 -m json.tool /tmp/marrow_mar169.mskl >/dev/null
```

Expected: export, JSON/MBIN comparison, and the runtime fixture smoke pass.
Record the **exact** comparison errors and output sizes from this run rather
than copying MAR-167/MAR-168's numbers. An authored curve can change whether a
segment stays packable by the MBIN AKEY optimization, so the numbers are
expected to move; a `match` verdict is what must hold.

- [ ] **Step 4: Run the Agent/MCP and integrity gates**

```bash
./build/marrow_agent_dispatch_smoke
./build/marrow_agent_socket_tests
./build/marrow_c_smoke
tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
kill %1
cmake --build build --target marrow_verify_third_party
python3 -m json.tool assets/fixtures/player_idle.marrow >/dev/null
python3 -m json.tool .agents/tasks/prd-marrow-runtime.json >/dev/null
git diff --check
git lfs status
```

Expected: all commands exit zero, the Agent registry is exactly 57 operations
with matching C++/Python parity, and no LFS object is staged or normalized.

- [ ] **Step 5: Prove the compatibility claim explicitly**

```bash
git diff --stat -- include/marrow/c_api src/c_api
git diff -- include/marrow/editor/authoring.hpp
git diff --stat -- src/editor/project.cpp src/runtime/skeleton_parse.cpp src/runtime/binary.cpp
rg -n '"version"' assets/fixtures/player_idle.mskl | head -3
```

Expected: the C ABI paths show zero changes; the `authoring.hpp` diff contains
only added lines; the format parser/writer paths show zero changes, which is the
concrete proof that `.marrow`, `.mskl` v1, and `.mbin` v2 are untouched because
MAR-169 writes only into the `curve` field that already existed.

- [ ] **Step 6: Verify the operation count is consistent everywhere**

```bash
rg -n '\b56\b' --glob '!build*' src tools AGENTS.md docs .agents
rg -n '\b57\b' --glob '!build*' src tools AGENTS.md docs .agents
rg -n 'timeline\.set_interpolation' src tools AGENTS.md docs
```

Expected: every remaining `56` is one of (a) a **historical** `Validation
Results` section or dated checkpoint sentence (MAR-155 through MAR-168, Task
#28, the refactor baseline, past PRD acceptance criteria), or (b) one of the
known false positives listed in design spec §14 (an `IM_COL32` channel, a colour
table, a fixture coordinate, a byte size, a timestamp, a struct size). Every
**current** claim reads 57.

Apply this classification rule to any hit not already in design spec §14's
table: a sentence carrying a date, a milestone ID, or the words "historical",
"baseline", or "checkpoint ... passed" is a record of what was true then and
stays at 56; a sentence stating what the surface *is* becomes 57. Do not rewrite
history — a MAR-167 section saying "the 56-operation surface is unchanged" was
true when written and stays.

- [ ] **Step 7: Synchronize user and architecture documentation**

Write only evidence supported by Steps 1-6:

- `quick-start.md`: replace the MAR-168 boundary paragraph with the handle
  contract — the active key's outgoing segment shows two handles for the active
  component, dragging one converts Linear or Stepped to Bezier in the same undo
  entry, X stops at 0 and 1, Y overshoot is allowed, the curve is shared by every
  component of that key, Escape cancels, one drag is one undo, and presets
  arrive in MAR-170.
- `concepts.md`: replace "MAR-167 performs no graph authoring; point time/value
  dragging begins at MAR-168" (as MAR-168 rewrote it) with the shared-easing
  statement — the graph writes easing through `set_keyframe_interpolation()`
  inside one `EditTransaction`, the primitive has no component parameter, and
  the same operation is available to the Agent and MCP surfaces.
- `discription.md`: add a dated MAR-169 contract paragraph, make MAR-170 next,
  and update the milestone-history sentences that currently end at MAR-168. Note
  the Agent/MCP surface is now 57 operations.
- `editing-gap-analysis.md`: update line 23 (56 -> 57), line 83 (56 -> 57 and
  편집 31 -> 32), line 190 (56-op -> 57-op parity), the MAR-169 roadmap row to
  completed, the "다음 직접 제품 milestone" line, and the P1 chain sentence so
  MAR-170 is next. Leave the MAR-155 historical rows at 56.
- `refector.md`: update the `MAR-168-191` row and the sentences naming the next
  milestone, and update the two **present-tense** registry-size sentences at
  lines 20 and 112 ("the current registry", "an exact current total of 56") to
  57. Leave lines 161, 305, and 321 at 56 — they are the dated Task #28
  checkpoint record.
- `platform-validation.md`: update the "next product milestone" line to MAR-170
  without changing any qualification status.
- `format-spec.md`: in the `curve` encoding list, add one sentence that the
  editor's authoring path guarantees `cx1` and `cx2` stay inside `[0, 1]` while
  `cy1` and `cy2` allow finite overshoot, matching the loaders' existing
  validation. **No new field, no version change.**
- `AGENTS.md`:
  - update the `Project State` line so MAR-169 is complete and MAR-170 is next;
  - update `Current Validation`'s registry line to
    "Agent registry validation (57 operations, including parameter,
    animation-duration, and timeline-interpolation authoring):
    `./build/marrow_agent_dispatch_smoke`";
  - add a `MAR-169 Graphical Shared Bezier Handle Editing Validation Results`
    section using the MAR-167/MAR-168 table plus command-output format with
    these slices:

  | Slice | Verification | Result |
  | --- | --- | --- |
  | Handle mapping | Handles sit at `[cx1, cy1]` and `[cx2, cy2]` along the frozen segment frame; pixel/control-point mapping round-trips within `1e-9` and agrees with the rendered polyline; a flat segment substitutes a positive 100 px reference span and a zero-duration segment exposes no handles | |
  | X limits and Y overshoot | Drags clamp `cx` to exactly `[0, 1]` and keep the gesture live; the primitive rejects out-of-range, NaN, infinite, and out-of-float32-range values atomically; finite `cy` overshoot round-trips through save/reload and export | |
  | Segment-wide curve identity | `set_keyframe_interpolation()` takes no component parameter; editing while displaying Y changes the X segment identically; exactly one `interpolation` field in the project differs; switching the displayed scalar never forks the curve | |
  | Conversion and transaction | Grabbing a Linear or Stepped handle converts to Cubic seeded at `[1/3, 1/3, 2/3, 2/3]` inside one transaction; press-only opens none; an unchanged drag commits none; Escape, cancel, tab switch, and invalid input restore project bytes, history, revision, dopesheet key times, graph values, segment kinds, and control points | |
  | Agent and MCP parity | `timeline.set_interpolation` is the 57th operation with matching C++/Python dry-run, validation, affected-key reporting, mutation, and undo behaviour; the growth is purely additive | |
  | Persistence and compatibility | Save/reload, JSON/MBIN export, `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1, and `SelectionSet` unchanged; only the pre-existing `curve` field is written | |

- design spec: change status from `Approved design, implementation not started`
  to `Implemented and validated` only after every gate is green.

- [ ] **Step 8: Close only MAR-169 in the PRD**

```json
{
  "id": "MAR-169",
  "status": "done",
  "completedAt": "<verified date>"
}
```

Keep MAR-170 open and dependent on MAR-169. Update the overview so the remaining
strict chain begins at MAR-170. Do not alter MAR-192 through MAR-210.

- [ ] **Step 9: Run documentation/roadmap integrity checks**

```bash
python3 -m json.tool .agents/tasks/prd-marrow-runtime.json >/dev/null
jq -e '
  (.stories[] | select(.id == "MAR-168") | .status) == "done" and
  (.stories[] | select(.id == "MAR-169") | .status) == "done" and
  (.stories[] | select(.id == "MAR-170") | .status) == "open" and
  ([.stories[] | select(.id >= "MAR-192" and .id <= "MAR-210") | .status] | all(. == "open"))
' .agents/tasks/prd-marrow-runtime.json >/dev/null
! rg -n "MAR-169 (is )?(the )?next|MAR-169.*status.*open" AGENTS.md docs/root1 .agents/tasks/prd-marrow-runtime.json
! rg -n "Bezier handle editing (is |remains )?MAR-169|MAR-169 경계" src docs
rg -n "MAR-170" AGENTS.md docs/root1/discription.md docs/root1/editing-gap-analysis.md .agents/tasks/prd-marrow-runtime.json
rg -n "57 operations|57-op" AGENTS.md docs/root1/editing-gap-analysis.md
git diff --check
```

Expected: JSON and jq checks pass, the stale "MAR-169 next/boundary" searches
return no match, current MAR-170 references exist, and the current-surface
documents read 57.

- [ ] **Step 10: Perform requirement-by-requirement completion audit**

For every MAR-169 acceptance criterion, point to both source and executed
evidence:

1. **Outgoing `[cx1, cy1, cx2, cy2]` with stable normalized-time and
   scalar-value mapping** — `make_segment_frame()` / `build_handle_geometry()` /
   `control_points_from_handle_pointer()` unit tests including the frozen-frame
   round trip, the flat-span fallback, and agreement with `build_geometry()`.
2. **X in `[0, 1]`, finite Y overshoot** — the clamp unit tests, the primitive's
   ordered finiteness-then-range rejection cases in the project smoke, the
   headless clamped-drag case, and the overshoot save/reload/export round trip.
3. **Segment-wide across Transform or RGBA components** — the primitive's
   component-free signature, the project smoke's "exactly one `curve`
   difference" assertion, and the headless case that edits while displaying Y
   and asserts the X segment changed identically.
4. **One drag, one transaction with preview, cancel, undo/redo, exact
   invalid-input rollback** — `undo_count()` deltas across press-only, unchanged
   drag, and committed drag; `refresh_runtime()` preview assertions; and every
   cancel case's byte-identical project/history/revision/dopesheet/graph
   comparison including segment kinds and control points.
5. **Matching C++ and MCP dry-run, validation, affected-key, mutation, undo** —
   `marrow_agent_dispatch_smoke`'s 57-row registry and behavioural cases plus
   `test_client.py`'s parity assertions and dry-run/live/read-back/undo
   sequence, both calling the same primitive.
6. **Runtime, shell, agent, and MCP tests over flat and overshoot curves, X
   limits, linear/stepped conversion, save/reload, and export** — the flat Blue
   colour segment case, the overshoot cases, the X-limit cases, the Linear and
   Stepped conversion cases, the save/reload bitwise assertion, and Step 3's
   export/compare gate.

Treat any missing or indirect evidence as incomplete. Fix via a new RED-GREEN
cycle, rerun the affected focused test, then rerun the full gate that supports
the claim.

- [ ] **Step 11: Final no-commit checkpoint**

```bash
git status --short
git diff --stat
git diff --check
git lfs status
```

Report the changed files, fresh test totals, export comparison metrics, the new
operation count, the exact remaining qualification limitations, and that no
commit, push, or reset occurred.

---

## Full verification checklist

Every command below must pass before MAR-169 is considered complete.

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
./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar169.mskl --export-binary /tmp/marrow_mar169.mbin
./build/marrow_inspect --compare /tmp/marrow_mar169.mbin /tmp/marrow_mar169.mskl
./build/marrow_fixture_smoke /tmp/marrow_mar169.mskl /tmp/player_idle.matl

# Agent and MCP gates (the surface that MAR-169 grows to 57)
./build/marrow_agent_dispatch_smoke
./build/marrow_agent_socket_tests
./build/marrow_c_smoke
tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
kill %1

# Repository integrity
cmake --build build --target marrow_verify_third_party
python3 -m json.tool assets/fixtures/player_idle.marrow >/dev/null
python3 -m json.tool assets/fixtures/player_idle.mskl >/dev/null
python3 -m json.tool .agents/tasks/prd-marrow-runtime.json >/dev/null
git diff --check
git lfs status

# Compatibility proof
git diff --stat -- include/marrow/c_api src/c_api
git diff --stat -- src/editor/project.cpp src/runtime/skeleton_parse.cpp src/runtime/binary.cpp
git diff -- include/marrow/editor/authoring.hpp

# Operation-count consistency
rg -n '\b56\b' --glob '!build*' src tools AGENTS.md docs .agents
rg -n '\b57\b' --glob '!build*' src tools AGENTS.md docs .agents
```

Expected results to record in `AGENTS.md`:

- graph model tests: all cases pass, including the new handle-geometry case;
- timeline model tests: all cases pass;
- project smoke: passes, including every new MAR-169 interpolation case;
- shell smoke: passes, including the new headless easing scenario and the
  actual-frame handle frames;
- default CTest: unchanged count (MAR-169 adds no CTest; record the actual count
  from `ctest --test-dir build -N`);
- Debug and Release display suites: unchanged counts, including 3 display tests;
- `marrow_inspect --compare`: match, with the freshly recorded key counts and
  byte sizes;
- `marrow_agent_dispatch_smoke`: exactly **57** operations;
- `tools/mcp/test_client.py`: 57/57 C++/Python parity, plus the
  dry-run/live/read-back/undo sequence;
- `git diff --stat` over `include/marrow/c_api`, `src/c_api`,
  `src/editor/project.cpp`, `src/runtime/skeleton_parse.cpp`, and
  `src/runtime/binary.cpp`: empty;
- every remaining `56` in the tree is a historical record, and every current
  statement of the surface size reads 57.

The display suites are automated evidence only. This checkpoint adds no manual
visible-UI, Windows 11, physical-input, or platform qualification credit.
MAR-192 through MAR-210 remain open, and support qualification remains governed
by `docs/root1/platform-validation.md`.
