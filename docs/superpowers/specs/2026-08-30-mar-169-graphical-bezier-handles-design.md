# MAR-169 Graphical Shared Bezier Handle Editing Design

**Date:** 2026-08-30

**Status:** Implemented and validated (2026-08-30)

**Authority:** `.agents/tasks/prd-marrow-runtime.json` story `MAR-169` and
`docs/root1/discription.md`

**Depends on:** MAR-168, whose gesture architecture, frozen-view rule,
transaction lifecycle, and UI-free API are defined in
`docs/superpowers/specs/2026-08-30-mar-168-graph-key-time-value-editing-design.md`

## 1. Goal

MAR-167 made the Graph tab a read-only projection of one focused parent track.
MAR-168 made its points draggable in time and in one scalar component, while
deliberately never reading or writing `Interpolation` and never adding an Agent
operation. MAR-169 is the story that does both: it makes the **outgoing shared
easing of the active key** directly draggable in the graph, behind the same
gesture machinery, and exposes the identical UI-free mutation as one new
operation on the C++ Agent registry and the Python MCP facade.

The central invariant this story must make visible and provable is the one
MAR-167 already announces in the UI: **a parent key owns exactly one outgoing
easing, shared by every component of that key.** MAR-169 must edit that one
value graphically without ever creating a per-component curve.

## 2. Approved Product Decisions

1. Handles belong to the **active key's outgoing segment only** — one segment,
   one pair of handles, one gesture, one key. Multi-key easing assignment is a
   preset operation and belongs to MAR-170.
2. Handles are drawn for the **active component only** (falling back to the
   first visible component when `graph_view.active_component` is unset). One
   curve is therefore represented by exactly one pair of on-screen handles even
   when several components are visible.
3. A left press on a handle starts a MAR-168 **drag candidate**, not a
   transaction. `begin_edit()` happens only when the pointer leaves the shared
   inclusive 4.0 logical-pixel dead zone.
4. A handle drag is **free 2-D**. `decide_drag_axis()` is not called. `cx` and
   `cy` are two independent parameters of one curve written by one primitive, so
   MAR-168's axis lock has no purpose here and would make the curve
   un-authorable.
5. `cx1` and `cx2` are **clamped** into `[0, 1]` by the pure pointer-to-control-
   point mapping, so a drag past the boundary stops at the boundary and the
   gesture continues. The authoring primitive independently **rejects** any
   out-of-range or non-finite value, atomically.
6. `cy1` and `cy2` allow unbounded **finite** overshoot. "Finite" means: not
   NaN, not `±inf`, and within `std::numeric_limits<runtime::AnimationScalar>::max()`
   both before and after narrowing to float32.
7. Grabbing a handle on a **Linear or Stepped** segment converts that segment to
   Cubic, seeded with `[1/3, 1/3, 2/3, 2/3]`, inside the same transaction and
   the same undo entry as the drag.
8. The graph view transform stays frozen for the duration of the gesture, per
   MAR-168. MAR-169 additionally disables the component-visibility checkboxes
   and `Fit` while any graph candidate or gesture is live.
9. One new Agent operation, `timeline.set_interpolation`, brings the registry to
   exactly **57** operations. It is purely additive: no existing operation's
   name, category, flags, arguments, or response shape changes.
10. Curve presets, remembered defaults, automatic/manual handle metadata, loop
    boundary synchronization, time scaling, and preview speed remain out of
    scope (MAR-170 through MAR-174).

## 3. Scope

### 3.1 Surfaces that gain easing editing

| Surface | Families | Selection | Mechanism |
| --- | --- | --- | --- |
| Graph tab | Bone Rotate / Translate / Scale / Shear, Slot Color | the single active key | handle drag |
| Agent `timeline.set_interpolation` | Transform (all four channels), Slot Color, **Deform** | any list of up to 4096 selectors | absolute value |
| MCP `timeline.set_interpolation` | same as Agent | same as Agent | same as Agent |

Deform keys carry an `interpolation` field (`DeformKeyframeEdit::interpolation`)
and the existing dopesheet inspector already edits it numerically, so the
project-domain primitive and the agent operation support Deform. The **graph**
continues to exclude Deform exactly as MAR-167 does; that exclusion is a
projection rule, not a data rule.

Draw Order, Event, and Slot Attachment keys have no `interpolation` field at all
(`DrawOrderKeyframeEdit`, `EventKeyframeEdit`, `SlotAttachmentKeyframeEdit`), so
they are rejected by the primitive, by the agent handler, and by the MCP schema.

### 3.2 Explicit exclusions

MAR-169 does not add:

- curve presets or a remembered default curve (MAR-170);
- automatic/manual handle metadata or Fritsch-Carlson tangents (MAR-171);
- loop-boundary key synchronization (MAR-172);
- selected-key time scaling (MAR-173) or preview playback speed (MAR-174);
- multi-key easing assignment from the graph;
- per-component curves of any kind, ever;
- incoming-handle editing. Marrow's runtime model has one **outgoing** easing
  per key and no incoming handle exists to edit;
- easing editing from the Deform, Inherit, Attachment, Draw Order, or Event
  graph lanes (they are not projected);
- any change to `timeline.describe`, `set_transform`, `set_deform_keyframe`, or
  `set_slot_color_keyframe`.

### 3.3 Compatibility boundaries

MAR-169 changes none of the following:

- the `.marrow` project schema. `curve` already accepts `"linear"`,
  `"stepped"`, and `[cx1, cy1, cx2, cy2]` (`src/editor/project.cpp`
  `parse_interpolation` / `build_interpolation_value`). No field is added;
- `.mskl` v1. Identical `curve` encoding
  (`src/runtime/skeleton_parse.cpp`);
- `.mbin` v2. Cubic easing is already four `float32` values
  (`src/runtime/binary.cpp` `append_float32(bytes, interpolation.cubic.cx1)` and
  the matching reader);
- C ABI v1. `include/marrow/c_api/**` and `src/c_api/**` are untouched;
- `SelectionSet` entity identity, runtime/GPU resource ownership, dopesheet
  behaviour, and the MAR-168 point-drag contract.

The only public-header change is **additive**: one result struct and one
function appended to `include/marrow/editor/authoring.hpp` after
`offset_keyframe_scalars()`. The Agent/MCP surface grows from 56 to 57
operations, which is the story's deliverable, not a compatibility break.

**Why the format is safe by construction.** Both the `.marrow` loader and the
`.mskl` loader reject a document whose `curve` array has an x control point
outside `[0, 1]`:

```text
src/editor/project.cpp:1639     "bezier x control points must stay within [0, 1]"
src/runtime/skeleton_parse.cpp:1727  "bezier x control points must stay within [0, 1]"
```

The MAR-169 primitive enforces exactly that invariant, plus finiteness, before
it commits. Any project MAR-169 can produce therefore reloads. This is the
strongest available guarantee: the write gate and the read gate are the same
predicate.

## 4. Existing Boundaries Reused

| Concern | Reused primitive | Location |
| --- | --- | --- |
| Parent-key identity | `TimelineKeyRef`, `timeline_key_ref()`, `timeline_key_index()` | `timeline_model` / `timeline_controller` |
| Pixel/unit mapping | `time_at_x()`, `value_at_y()`, `x_at_time()`, `y_at_value()` | `timeline_graph_model` (added by MAR-168) |
| Drag candidate, dead zone, cancel wiring | `TimelineGraphPointDrag`, `update_timeline_graph_point_drag()`, `cancel_timeline_graph_point_drag()` | `shell_state` / `shell_timeline_graph` (MAR-168) |
| Point activation and `active_component` | `activate_timeline_graph_point()` | `shell_timeline_graph` (MAR-167) |
| Overlay materialization | `visit_editable_timeline_keys()`, `ensure_*_timeline_edit_index()` | `timeline_controller` |
| Project-domain key resolution | `TimelineKeySelector`, `timeline_key_selector()`, `resolve_timeline_key()` | `authoring` |
| Gesture completion rule | `timeline_model::completion_decision()` | `timeline_model` |
| Live preview / one history entry | `EditorSession::EditTransaction` | `session` |
| Gesture cancel on focus loss / shutdown | `cancel_authoring_gestures()` | `shell_core` |
| Gesture mutual exclusion | `authoring_gesture_active()` | `shell_state` |
| Agent key-selector parsing shape | the `timeline.retime_keyframes` `keys` parser | `agent_handlers_editing` |
| Agent dry-run / commit / no-change shape | `commit_or_error()`, `CommitPolicy` | `agent_dispatch_internal` |
| Runtime curve construction and LUT cache | `runtime::Interpolation::cubic_bezier()` | `runtime/animation` |

## 5. Architecture

```text
ImGui graph body (shell_timeline_graph.cpp)
        | plain doubles / bools only
        v
graph drag driver (shell_timeline_graph.cpp, ImGui-free entry points)
        |                        |                       |
        | Point / Time           | Point / Value         | Handle  (NEW)
        v                        v                       v
retime gesture trio      value gesture trio      handle gesture trio (NEW)
        |                        |                       |
        v                        v                       v
retime_keyframes()    offset_keyframe_scalars()  set_keyframe_interpolation() (NEW)
        \                        |                       /
         \_______ EditorSession::EditTransaction ________/
                    (refresh_runtime -> preview,
                     commit -> exactly one history entry)

Agent `timeline.set_interpolation` (agent_handlers_editing.cpp)  ---> the same
MCP  `timeline.set_interpolation` (tools/mcp/tools/editing.py)   ---> primitive
```

All new handle math lives in `timeline_graph_model` (UI-free, unit tested). The
gesture lifecycle lives in `timeline_controller` (ShellState-aware, ImGui-free).
Only pointer sampling and drawing live in `shell_timeline_graph.cpp`. The Agent
handler calls the *same* `set_keyframe_interpolation()` the shell gesture calls,
which is what makes criterion 5's "matching ... behavior" structural rather than
coincidental.

## 6. Runtime Semantics of `[cx1, cy1, cx2, cy2]`

This section fixes the exact meaning the graph must render and edit. It is read
from `src/runtime/animation.cpp`, not assumed.

The curve is a unit-square cubic Bezier with fixed endpoints:

```text
P0 = (0, 0)      P1 = (cx1, cy1)      P2 = (cx2, cy2)      P3 = (1, 1)
X(t) = 3(1-t)^2 t cx1 + 3(1-t) t^2 cx2 + t^3
Y(t) = 3(1-t)^2 t cy1 + 3(1-t) t^2 cy2 + t^3
```

`Interpolation::transform(alpha)` clamps `alpha` into `[0, 1]`, solves
`X(t) = alpha` for `t` (Newton with a bisection fallback, or a 64-entry cached
LUT), and returns `Y(t)`. `interpolate_value()` then computes
`from + (to - from) * transform(alpha)`.

Three consequences drive the design:

1. **X must stay in `[0, 1]` for the inverse to be well posed.** With
   `P0x = 0`, `P3x = 1` and `cx1, cx2 in [0, 1]`, `X'(t)/3` equals
   `a u^2 + 2(b - a) u v + (1 - b) v^2` with `a = cx1`, `b = cx2`, `u = 1 - t`,
   `v = t`, `u, v >= 0`. That quadratic form is non-negative on the whole domain
   because `(b - a)^2 - a(1 - b) <= 0` for every `(a, b)` in `[0, 1]^2`, with
   equality only at `(1, 0)`. `X` is therefore non-decreasing and the runtime's
   solver has a unique answer. Allowing `cx` outside `[0, 1]` would let `X`
   fold back on itself, making `transform()` depend on solver luck. The
   `[0, 1]` clamp is a correctness requirement, not a style rule.
2. **Y overshoot is meaningful and must be preserved.**
   `sample_cubic_component()` applies no clamp to the returned `Y`, so
   `cy < 0` or `cy > 1` produces an eased alpha outside `[0, 1]` and therefore a
   value that overshoots the segment's endpoints. This is the "anticipation and
   follow-through" curve every 2D animator expects, and criterion 2 requires it.
3. **Storage is `float32`.** `CubicBezierControlPoints` holds four
   `AnimationScalar` (= `float`) fields, and `Interpolation::cubic_bezier()`
   narrows its `double` arguments on construction. Validation therefore has to
   run on the `double` input *and* on the narrowed `float`, so that a value like
   `1e300` is rejected rather than silently stored as `inf`.

Note also that `Interpolation::cubic_bezier()` populates a process-wide LUT
cache keyed on the four float bit patterns. The primitive validates the four
doubles **before** constructing an `Interpolation`, so rejected agent input
never enters that cache.

## 7. Handle Geometry

### 7.1 The frozen segment frame

A handle drag needs a stable mapping between pixels and `[cx, cy]`. The mapping
depends on the segment's two anchors, which a handle drag can never move (it
writes only `interpolation`). MAR-169 therefore snapshots them once, at press,
into a `SegmentFrame`, exactly as MAR-168 freezes the `View` and `PlotRect`:

```cpp
struct SegmentFrame {
    double start_time_seconds{0.0};
    double end_time_seconds{0.0};
    double start_value{0.0};
    double end_value{0.0};
    double time_span{0.0};    // end_time - start_time, always > kMinimumSegmentSeconds
    double value_span{0.0};   // resolved; never zero (see 7.3)
    bool flat_value_span{false};
};
```

### 7.2 Forward and inverse mapping

For a control point `(cx, cy)`:

```text
handle_time  = frame.start_time_seconds + cx * frame.time_span
handle_value = frame.start_value        + cy * frame.value_span
handle_x     = x_at_time(rect, view, handle_time)
handle_y     = y_at_value(rect, view, handle_value)
```

The inverse, used by the drag:

```text
cx_raw = (time_at_x(rect, view, pointer_x)  - frame.start_time_seconds) / frame.time_span
cy     = (value_at_y(rect, view, pointer_y) - frame.start_value)        / frame.value_span
cx     = clamp(cx_raw, 0.0, 1.0)
```

Both directions reuse the MAR-168 mapping helpers, so the drawn handle and the
grabbed handle cannot drift apart. A unit test asserts the round trip to `1e-9`
for a representative view including a negative `view_start_seconds` and a
sub-unit `pixels_per_value`.

Any non-finite intermediate makes the inverse return `std::nullopt`, which
cancels the gesture with rollback. It never silently becomes zero.

### 7.3 The flat segment (`end_value == start_value`)

When the segment's two endpoint values are equal, the value denominator
degenerates and `cy` is unrecoverable from a pixel. This is not a rare corner:
`player_idle`'s `body` slot colour has `b = 1.0` at `t = 0.0` and `b = 1.0` at
`t = 0.5`, so the Blue component's first segment is exactly flat while R, G, and
A vary across the *same shared curve*.

Refusing to edit a flat segment would therefore make the shared curve
un-editable purely because of which component happens to be displayed, which
directly contradicts criterion 3. MAR-169 instead substitutes a **fallback value
span**.

The flatness test is view-relative rather than absolute, because the failure
mode is numerical conditioning, not exact equality:

```text
raw_span = frame.end_value - frame.start_value
flat     = !std::isfinite(raw_span) ||
           std::abs(raw_span) * view.pixels_per_value < kMinimumSegmentValuePixels   // 1.0
value_span = flat ? (kFlatSegmentHandlePixels / view.pixels_per_value)   // 100.0 px
                  : raw_span
```

That is: if the two anchors are less than one logical pixel apart vertically,
the segment's own value axis cannot express a handle position, so 100 logical
pixels of vertical travel is defined to equal `cy = 1`. The substituted span is
**always positive**, so "drag up increases `cy`" holds regardless of the
infinitesimal sign of `raw_span`. Because the `View` is frozen for the whole
gesture (MAR-168 §6.1), the substituted span is a constant during the drag, and
the mapping is stable in the sense criterion 1 requires.

On a flat segment the drawn curve is a straight horizontal line whatever the
easing is. The handles are therefore drawn *off* the curve, above and below the
flat line. That is correct and informative: it shows the user that this
component cannot display the curve while another component of the same key can.
The drag readout says so explicitly.

`flat_value_span` is recorded in the frame so the readout and the render stats
can report it, and so a test can assert the fallback path was taken.

### 7.4 The zero-duration segment (`end_time == start_time`)

Unlike the flat case, a zero-duration segment has **no fallback that means
anything**: the runtime never evaluates the curve at all, because sampling jumps
straight to the later key. `cx` would be a ratio over a zero span.

Decision: `build_handle_geometry()` returns `std::nullopt` when
`frame.time_span <= kMinimumSegmentSeconds` (`timeline_model::kKeyTimeEpsilon`,
`1e-6` s). No handles are drawn, no press arms a candidate, and no gesture can
start. Since `retime_keyframes()` and `insertable_key_time()` enforce
`kNonEventKeySpacing` (1 ms) for every non-event track, this case is reachable
only from imported data.

The **agent** operation deliberately still accepts such a key. `interpolation`
is a property of the key, it round-trips through both formats, and
`set_transform` already writes it on any key. Rejecting it in the agent path
would break round-tripping of imported documents. The geometric path refuses
because geometry is undefined; the value path allows because the value is
defined.

### 7.5 Which segment gets handles

`build_handle_geometry()` returns `std::nullopt`, and no handle is drawn or
grabbable, unless all of the following hold:

| Condition | Reason |
| --- | --- |
| the projection status is `Ready` | MAR-167 fail-closed rule |
| `state->timeline_editor.active_key` has a value | handles belong to the active key |
| `active_key->track_id` equals the displayed row's ID | the active key is on another track |
| the active key resolves to an index in the projected track | identity was lost |
| that index is not the last key | the last key has no outgoing segment |
| `frame.time_span > kMinimumSegmentSeconds` | §7.4 |
| the chosen component index is visible and within the track's component count | nothing to anchor to |
| every computed coordinate is finite | fail closed |

The chosen component is `graph_view.active_component` when it is set and
visible, otherwise the lowest-indexed visible component. Both the geometry
builder and the gesture read the component the same way, from one shared helper,
so the drawn handle and the grabbed handle always agree.

### 7.6 Seeding a Linear or Stepped segment

```cpp
constexpr std::array<double, 4> kLinearEquivalentControlPoints{
    1.0 / 3.0, 1.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0};

std::array<double, 4> seed_control_points(
    SegmentKind kind,
    const std::array<double, 4>& existing);   // Cubic -> existing, otherwise the constant
```

Handles are drawn for Linear and Stepped segments too, at the seed positions, so
the conversion is discoverable rather than hidden behind a combo box.

`[1/3, 1/3, 2/3, 2/3]` is chosen because it is the unique **evenly spaced**
cubic that is exactly identical to Linear: with `cx1 = cy1` and `cx2 = cy2`,
`Y(t) = X(t)`, so `transform(alpha) = Y(X^-1(alpha)) = alpha`. Grabbing a Linear
handle therefore changes the evaluated curve by nothing at all until the pointer
actually moves; only the authored *kind* changes.

`[0, 0, 1, 1]` — the `CubicBezierControlPoints` default — is also exactly
linear, but it places handle 1 on top of the start anchor and handle 2 on top of
the end anchor, where they are neither visible nor grabbable. It is rejected for
that reason.

Stepped uses the same seed. Stepped is discontinuous and has **no** cubic
equivalent, so every possible seed changes the evaluated curve; choosing the
neutral (linear) one avoids inventing an arbitrary "stepped-ish" approximation
and keeps `seed_control_points()` a single stateless rule with one constant.
The change is inside the drag's transaction, so Escape and undo restore Stepped
exactly.

### 7.7 Conversion and `changed`

`apply_timeline_graph_handle_control_points()` always writes
`InterpolationKind::CubicBezier`. The Linear/Stepped conversion is therefore
implicit in the write, with no separate conversion step and no separate history
entry.

A drag that leaves the dead zone and returns to the exact seed still reports
`changed = true` and commits one entry, because the authored kind genuinely
changed from Linear/Stepped to Cubic. A drag on a segment that was *already*
Cubic and ends on its exact original control points reports `changed = false`,
and `completion_decision()` cancels it with no history entry — the same rule
`finish_timeline_retime_gesture()` uses.

## 8. Segment-Wide Curve Identity

Criterion 3 is satisfied structurally, in three independent layers.

**Layer 1 — the data model has one field.** A parent key is one
`TransformKeyframeEdit` (`angle`, `x`, `y`, one `interpolation`) or one
`SlotColorKeyframeEdit` (`color`, one `interpolation`). There is no
per-component interpolation storage anywhere in `.marrow`, `.mskl`, `.mbin`, or
the runtime. A per-component curve is not merely disallowed; it is
unrepresentable.

**Layer 2 — the primitive has no component parameter.**

```cpp
TimelineInterpolationResult set_keyframe_interpolation(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    runtime::InterpolationKind kind,
    const std::array<double, 4>& control_points);
```

`TimelineKeySelector` names an animation, a bone/slot, a channel, and a time —
never a component. There is no argument through which a component could
influence which bytes are written. Compare `offset_keyframe_scalars()`, which
*does* take a `TimelineScalarComponent`: that asymmetry is the design.

**Layer 3 — the gesture cannot fork mid-drag.** `active_component` is written
only by `activate_timeline_graph_point()`, which runs from the press branch of
`draw_timeline_graph_body()`. That branch is guarded by
`!authoring_gesture_active(*state)` and cannot fire while a handle gesture is
live. In addition, the component index is frozen into the drag candidate at
press, the component-visibility checkboxes are disabled while any graph
candidate or gesture is live, and
`update_timeline_graph_point_drag()` cancels the gesture if the resolved active
component or active key changes between frames.

The observable consequence, asserted by the shell smoke: display Translate **Y**,
drag a handle, and the Translate **X** segment's `SegmentKind` and control points
change identically — because they are literally the same `interpolation` field
re-projected. Switching the displayed component between two gestures on the same
key reads and writes the same single value; a second curve never comes into
existence.

## 9. Validation, Clamping, and Rejection

### 9.1 Where each rule lives

| Rule | Location | Behaviour |
| --- | --- | --- |
| `cx in [0, 1]` during a drag | `timeline_graph_model::control_points_from_handle_pointer()` | clamp; the handle stops at the boundary and the drag continues |
| `cx in [0, 1]` at persistence | `authoring::set_keyframe_interpolation()` | **reject** atomically, error string, no mutation |
| finite `cx`/`cy` (double) | `authoring::set_keyframe_interpolation()` | reject atomically |
| finite `cx`/`cy` after `float32` narrowing | `authoring::set_keyframe_interpolation()` | reject atomically |
| unsupported key kind | `authoring::set_keyframe_interpolation()` | reject atomically |
| duplicate selector | `authoring::set_keyframe_interpolation()` | reject atomically |
| unsupported key kind (early) | MCP `inputSchema` + agent handler | reject before the socket / before the primitive |

The authoritative gate is the primitive, because the Agent surface calls it with
arbitrary JSON-derived numbers and must be safe on its own. The graph-model
clamp exists so that the *drag* never produces an out-of-range request in the
first place, giving the boundary a natural feel instead of an error. Both apply
the identical predicate; a unit test feeds a pointer far outside the plot to the
mapping function and asserts the result is exactly what the primitive accepts.

The ImGui layer contains **no** clamping and no arithmetic that maps pixels to
control points.

### 9.2 "Finite" precisely

For each of the four values `v` supplied to the primitive:

```text
reject unless std::isfinite(v)
reject unless std::abs(v) <= std::numeric_limits<runtime::AnimationScalar>::max()
narrowed = static_cast<double>(static_cast<runtime::AnimationScalar>(v))
reject unless std::isfinite(narrowed)
for cx1 and cx2 additionally:
    reject unless v >= 0.0 && v <= 1.0
    reject unless narrowed >= 0.0 && narrowed <= 1.0
```

The finiteness test must run **before** the range test, because `NaN < 0.0` and
`NaN > 1.0` are both false and a NaN would otherwise pass a naive range check.
This mirrors the float32 boundary `set_animation_duration()` and
`offset_keyframe_scalars()` already enforce.

`kind == Linear` or `kind == Stepped` ignores `control_points` entirely and
performs no range validation, because the stored value carries no control
points.

### 9.3 Extreme but legal curves

A finite `cy` of, say, `1e30` is accepted by the primitive because criterion 2
allows finite overshoot without an artificial bound. Such a curve makes
`build_geometry()` produce a non-finite plot point, so
`draw_timeline_graph_body()` shows its existing fail-closed message, "The
focused graph track contains invalid or non-finite data." That is pre-existing
MAR-167 behaviour for extreme values, it does not crash, and it is asserted by a
test rather than left to chance. A handle **drag** can never produce such a
value, because the pointer is bounded by the plot and the mapping is affine.

## 10. Transaction Lifecycle and Atomic Rollback

### 10.1 One drag, one history entry

| Phase | Action |
| --- | --- |
| Press on a handle | drag candidate with `target = Handle` only; no transaction; `authoring_gesture_active()` stays false |
| Dead zone left (4.0 px) | `begin_timeline_graph_handle_gesture()` opens one `EditTransaction` |
| Each frame | map pointer to `[cx, cy]`, clamp X, `apply_timeline_graph_handle_control_points()`, `refresh_runtime()`, `sync_shell_from_editor_session()` |
| Release | `finish_timeline_graph_handle_gesture(state, true)` |
| Escape / focus loss / shutdown / tab switch / project reload / lost context | `finish_timeline_graph_handle_gesture(state, false)` |

The transaction is opened with `EditKind::EditProperty`, label
`"Edit key easing"`, group `"timeline:graph-easing"`, `allow_merge = false`,
impact `Project | Runtime | Preview`.

`refresh_runtime()` creates no history; only `commit()` does.
`completion_decision(commit_requested, changed)` cancels an unchanged gesture,
so a press-and-release, or a drag that ends on the original Cubic control
points, leaves `undo_count()` unchanged.

Because a handle drag never writes `time`, `auto_extend_explicit_animation_durations()`
never has anything to grow. Explicit durations are untouched.

### 10.2 Preflight-then-mutate ordering

`set_keyframe_interpolation()` follows the shape of `retime_keyframes()` and
`offset_keyframe_scalars()`, which is what makes rollback exact:

```text
1. validate arguments (null project, empty selectors, kind, control points)
2. ProjectData candidate = *project              // full copy, nothing shared
3. resolve every selector against `candidate`; reject duplicates
4. reject every selector whose kind has no interpolation field
5. build the runtime::Interpolation once, after validation
6. count how many resolved keys differ from it; if none, return a no-change result
7. write the interpolation into every resolved key of `candidate`
8. only on total success: *project = std::move(candidate)
```

No step between 2 and 7 can leave a partial write in `*project`, because every
write targets `candidate`. Step 8 is a single move.

### 10.3 Rollback across the shell

A gesture-level failure — a non-finite mapped control point, a lost key
identity, a failed materialization, a failed `refresh_runtime()`, a rejection
from the primitive, or an invalidated context — calls
`finish_timeline_graph_handle_gesture(state, false)`, which calls
`EditTransaction::cancel()`. Cancel restores the project, the runtime
`SkeletonData`, the preview controller state, and the playback state captured
when the transaction began, then bumps runtime/preview revision.
`sync_shell_from_editor_session(state)` refreshes the shell mirrors.

The graph and the dopesheet are byte-identical to gesture start because both
read the restored data through revision-keyed caches and neither holds a runtime
pointer or key index across frames (MAR-167/MAR-168 §10.3).

The shell smoke proves this by comparing, before the press and after the cancel:
`serialize_project()`, `undo_count()`, `redo_count()`, `project_revision()`,
`dirty()`, the rebuilt dopesheet `TrackRow::key_times`, every rebuilt graph
`Key::values`, and — new for MAR-169 — every rebuilt graph `Segment::kind` and
every rebuilt `Key::outgoing_easing` control point.

### 10.4 Selection and `active_key` stability

A handle drag writes only `interpolation`. No key time changes, so every
`TimelineKeyRef` is bit-identical for the whole gesture and across commit,
cancel, undo, and redo. `selected_keys` and `active_key` are never rewritten by
this path, and `reconcile_timeline_key_selection()` never has anything to prune.

### 10.5 Gesture exclusivity and cancel registration

- `TimelineGraphHandleGesture` is added to `authoring_gesture_active()` in
  `shell_state.hpp` and to `cancel_authoring_gestures()` in `shell_core.cpp`.
  The comment in `cancel_authoring_gestures()` already requires the two lists to
  stay in step.
- The drag candidate holds no transaction and is therefore **not** added to
  `authoring_gesture_active()`; it is reset by `cancel_authoring_gestures()` and
  on left-button release, exactly as MAR-168 specifies.
- Both live in `TimelineEditorState`, so `TimelineEditorState{}` source adoption
  clears them atomically.
- Leaving the Graph tab, or a `requested_view_mode` transition away from Graph,
  cancels the candidate and the gesture before the tab body changes.

## 11. UI-Free API

### 11.1 `src/editor/timeline_graph_model.hpp` (new declarations)

```cpp
enum class HandleIndex : std::uint8_t { First, Second };

/** @brief Frozen anchor geometry of one outgoing segment. */
struct SegmentFrame {
    double start_time_seconds{0.0};
    double end_time_seconds{0.0};
    double start_value{0.0};
    double end_value{0.0};
    double time_span{0.0};
    double value_span{0.0};
    bool flat_value_span{false};
};

struct HandleGeometry {
    timeline_model::KeyRef key;
    Component component{Component::Angle};
    std::size_t component_index{0U};
    std::size_t key_index{0U};
    SegmentKind kind{SegmentKind::Linear};
    SegmentFrame frame{};
    std::array<double, 4> control_points{};   // seeded for Linear/Stepped
    PlotPoint start_anchor{};
    PlotPoint end_anchor{};
    PlotPoint first_handle{};
    PlotPoint second_handle{};
};

struct HandleHit {
    timeline_model::KeyRef key;
    HandleIndex handle{HandleIndex::First};
};

inline constexpr std::array<double, 4> kLinearEquivalentControlPoints{
    1.0 / 3.0, 1.0 / 3.0, 2.0 / 3.0, 2.0 / 3.0};
inline constexpr double kMinimumSegmentSeconds = 1e-6;
inline constexpr double kMinimumSegmentValuePixels = 1.0;
inline constexpr double kFlatSegmentHandlePixels = 100.0;

std::array<double, 4> seed_control_points(
    SegmentKind kind,
    const std::array<double, 4>& existing);

/** @brief Freezes the anchors of the segment starting at `key_index`. */
std::optional<SegmentFrame> make_segment_frame(
    const Track& track,
    std::size_t key_index,
    std::size_t component_index,
    const View& view);

/** @brief Builds the drawable/grabbable handles of the active key's segment. */
std::optional<HandleGeometry> build_handle_geometry(
    const Track& track,
    const timeline_model::KeyRef& active_key,
    std::size_t component_index,
    const View& view,
    PlotRect rect);

std::optional<HandleHit> hit_test_handle(
    const HandleGeometry& geometry,
    double pointer_x,
    double pointer_y,
    double inclusive_radius = 7.0);

/**
 * @brief Maps a pointer to one moved control point, clamping X into [0, 1].
 *
 * Returns nullopt for any non-finite input or intermediate. The untouched
 * control point is carried through unchanged. Y overshoot is not clamped.
 */
std::optional<std::array<double, 4>> control_points_from_handle_pointer(
    const SegmentFrame& frame,
    const std::array<double, 4>& current,
    HandleIndex handle,
    const View& view,
    PlotRect rect,
    double pointer_x,
    double pointer_y);
```

### 11.2 `include/marrow/editor/authoring.hpp` (additive)

```cpp
struct TimelineInterpolationResult : AuthoringResult {
    std::size_t key_count{0U};
    std::size_t changed_key_count{0U};
};

/**
 * @brief Atomically replaces the outgoing easing of persisted timeline keys.
 *
 * The easing is a property of the whole parent key and is shared by every
 * component of that key, so this operation takes no component argument. Cubic
 * control points must be finite, must survive float32 narrowing, and must keep
 * `cx1`/`cx2` inside [0, 1], which is the same invariant the `.marrow` and
 * `.mskl` loaders enforce. Finite Y overshoot is allowed. Draw-order, event,
 * and slot-attachment keys carry no easing and are rejected. Callers
 * materialize imported runtime-only tracks through the shared
 * `ensure_*_timeline_edit` project operations first. A rejected edit leaves the
 * project unchanged.
 */
TimelineInterpolationResult set_keyframe_interpolation(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    runtime::InterpolationKind kind,
    const std::array<double, 4>& control_points = {0.0, 0.0, 1.0, 1.0});
```

`runtime::InterpolationKind` is reused rather than duplicated; it is already
visible through `project.hpp`'s include of `marrow/runtime/animation.hpp`. The
header gains `#include <array>`.

### 11.3 `src/editor/timeline_controller.hpp` (shell, ImGui-free)

```cpp
bool begin_timeline_graph_handle_gesture(
    ShellState* state,
    std::uint32_t item_id,
    const TimelineTrackRow& track,
    const TimelineKeyRef& key,
    timeline_graph_model::HandleIndex handle,
    const timeline_graph_model::SegmentFrame& frame,
    const std::array<double, 4>& seed_control_points,
    marrow::runtime::InterpolationKind original_kind,
    const std::vector<TimelineTrackRow>& tracks);
bool apply_timeline_graph_handle_control_points(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    const std::array<double, 4>& requested_control_points);
void finish_timeline_graph_handle_gesture(ShellState* state, bool commit);
```

### 11.4 `src/editor/shell_timeline_graph.hpp` (shell, ImGui-free entry points)

MAR-168's `begin_timeline_graph_point_drag()` /
`update_timeline_graph_point_drag()` / `cancel_timeline_graph_point_drag()` are
reused. MAR-169 adds one sibling entry point so a handle press is armed through
the same candidate:

```cpp
bool begin_timeline_graph_handle_drag(
    ShellState* state,
    const TimelineTrackRow& track,
    const timeline_graph_model::HandleGeometry& handles,
    timeline_graph_model::HandleIndex handle,
    std::uint32_t item_id,
    timeline_graph_model::PlotRect plot,
    const timeline_graph_model::View& view,
    double pointer_x,
    double pointer_y);
```

`update_timeline_graph_point_drag()` gains a `Handle` branch; its signature does
not change, so `shell_smoke_graph.cpp` keeps driving whole gestures headlessly
with plain scalars.

### 11.5 `src/editor/shell_state.hpp` (transient shell state)

```cpp
enum class GraphDragTarget : std::uint8_t { Point, Handle };

struct TimelineGraphHandleGesture {
    std::uint32_t item_id{0U};
    std::string track_id;
    TimelineKeyRef key;
    timeline_graph_model::Component component{timeline_graph_model::Component::Angle};
    std::size_t component_index{0U};
    timeline_graph_model::HandleIndex handle{timeline_graph_model::HandleIndex::First};
    timeline_graph_model::SegmentFrame frame{};
    marrow::runtime::InterpolationKind original_kind{
        marrow::runtime::InterpolationKind::Linear};
    std::array<double, 4> original_control_points{};
    std::array<double, 4> applied_control_points{};
    bool materialized{false};
    bool changed{false};
    marrow::editor::EditorSession::EditTransaction transaction;
};
```

`TimelineGraphPointDrag` (MAR-168) gains:

```cpp
GraphDragTarget target{GraphDragTarget::Point};
timeline_graph_model::HandleIndex handle{timeline_graph_model::HandleIndex::First};
timeline_graph_model::SegmentFrame frame{};
std::array<double, 4> seed_control_points{};
marrow::runtime::InterpolationKind segment_kind{
    marrow::runtime::InterpolationKind::Linear};
```

`TimelineEditorState` gains
`std::optional<TimelineGraphHandleGesture> graph_handle_gesture;`.

### 11.6 What stays shell-private ImGui code

`draw_timeline_graph_body()` keeps only: sampling the pointer and modifiers;
running `hit_test_handle()` before `hit_test()` on a left click; drawing the two
tangent lines and two handle squares; disabling the component checkboxes and
`Fit` while a candidate or gesture is live; drawing the easing readout; and
publishing render stats. It performs no clamping, no pixel-to-control-point
arithmetic, and no project mutation.

### 11.7 Render statistics additions

`TimelineGraphRenderStats` gains:

```cpp
bool handles_drawn{false};
float first_handle_x{0.0f};
float first_handle_y{0.0f};
float second_handle_x{0.0f};
float second_handle_y{0.0f};
bool handle_flat_value_span{false};
bool handle_gesture_active{false};
timeline_graph_model::SegmentKind active_segment_kind{
    timeline_graph_model::SegmentKind::Linear};
```

## 12. Presentation

- Handles are drawn after the segments and before the key points, inside the
  existing plot `PushClipRect`, so an off-plot handle is clipped rather than
  bleeding over the axis labels.
- Two thin tangent lines, start anchor to handle 1 and end anchor to handle 2,
  in `IM_COL32(0x9a, 0xd8, 0xff, 0x80)`.
- Two filled 4.0 px squares at the handle positions in
  `IM_COL32(0x9a, 0xd8, 0xff, 0xff)`. Squares, not circles, so they never read
  as key points, and light blue so they are distinguishable from the gold
  selection ring and from every component colour by shape even where the hue is
  close.
- A Linear or Stepped segment draws its seeded handles with the same geometry.
  The `Outgoing:` toolbar label already reports the actual kind.
- The MAR-167 shared-easing notice keeps its current wording and gains one
  sentence: dragging a handle edits that one shared curve for every component of
  the key.
- Drag readout, one line:
  `Easing  <original kind> -> [cx1, cy1, cx2, cy2]` with three decimals, plus
  `  (X clamped)` when the clamp bound this frame, plus
  `  (flat segment: 100 px = 1.0)` when `frame.flat_value_span` is set.
- The component checkboxes and `Fit` are wrapped in
  `ImGui::BeginDisabled()/EndDisabled()` while any graph candidate or gesture is
  live. This also protects MAR-168's value drag, whose frozen `component_index`
  would otherwise be left pointing at a hidden component; it is an intentional,
  small extension of MAR-168's frozen-view rule rather than a change to its
  contract.
- Status messages: `"Edited key easing"`, `"Cancelled easing edit"`,
  `"Easing edit failed"`.

## 13. Agent and MCP Surface

### 13.1 The operation

| Field | Value |
| --- | --- |
| `name` | `timeline.set_interpolation` |
| `category` | `edit` |
| `mutating` | `true` |
| `requires_review` | `false` |
| `dry_run_supported` | `true` |
| `requires_project` | `true` |
| `handler` | `&handle_editing_operation` (delegates to `handle_timeline_editing_operation`) |
| registry position | immediately after `timeline.retime_keyframes` |

The dotted `timeline.` prefix follows the newer convention shared by
`timeline.retime_keyframes`, `animation.set_duration`, and `timeline.describe`,
rather than the legacy flat `set_transform` style.

**New registry total: 57 operations** (inspection 12, validation 3,
management 10, edit 32).

### 13.2 Arguments

```json
{
  "keys": [
    {"kind": "transform",  "animation": "idle", "bone": "spine", "channel": "translate", "time": 0.0},
    {"kind": "slot_color", "animation": "idle", "slot": "body",  "time": 0.0},
    {"kind": "deform",     "animation": "idle", "slot": "body",  "attachment": "body_mesh", "time": 0.0}
  ],
  "interpolation": [0.2, -0.4, 0.8, 1.6],
  "dry_run": false
}
```

- `keys`: required array, 1 to 4096 entries, same cap as
  `timeline.retime_keyframes`. Only `transform`, `deform`, and `slot_color` are
  accepted; `draw_order`, `event`, and `slot_attachment` are rejected with a
  message naming the unsupported kind.
- `interpolation`: **required**. `"linear"`, `"stepped"`, or a 4-number array.
  Unlike `interpolation_arg()`, a missing value is an error rather than a silent
  linearization, because silently linearizing every selected key would be a
  destructive default.
- `dry_run`: optional boolean.

Parsing uses a new internal helper declared in `agent_dispatch_internal.hpp`:

```cpp
bool interpolation_request_arg(
    const json::Value& args,
    std::string_view name,
    marrow::runtime::InterpolationKind* kind_out,
    std::array<double, 4>* control_points_out,
    std::string* error_out);
```

It returns the raw doubles and does **not** construct a `runtime::Interpolation`,
so rejected input never enters the process-wide LUT cache. The existing
`interpolation_arg()` is left byte-identical, so `set_transform`,
`set_deform_keyframe`, and `set_slot_color_keyframe` are unaffected.

### 13.3 Validation rules

In order:

1. missing `args` object -> `invalid_request`.
2. `keys` missing, not an array, or empty -> `invalid_request`.
3. more than 4096 keys -> `invalid_request`.
4. a key entry that is not an object, or missing `kind`/`animation`/`time`,
   or missing `bone`+`channel` (transform) / `slot`+`attachment` (deform) /
   `slot` (slot_color) -> `invalid_request`.
5. an unknown or unsupported `kind` -> `invalid_request`.
6. `interpolation` missing or malformed -> `invalid_request`.
7. every rule from §9.2, enforced by the primitive -> `invalid_request`.
8. a selector that does not resolve -> `not_found` (matching
   `timeline.retime_keyframes`).
9. the same key selected twice -> `invalid_request`.
10. live path only: nothing changed -> `no_change`.

### 13.4 Response payload

```json
{
  "dry_run": true,
  "interpolation": [0.2, -0.4, 0.8, 1.6],
  "key_count": 2,
  "changed_key_count": 2,
  "keys_truncated": false,
  "keys": [
    {"kind": "transform", "animation": "idle", "bone": "spine",
     "channel": "translate", "time": 0.0,
     "previous_interpolation": "linear", "changed": true},
    {"kind": "slot_color", "animation": "idle", "slot": "body", "time": 0.0,
     "previous_interpolation": [0.33, 0.0, 0.67, 1.0], "changed": true}
  ]
}
```

`previous_interpolation` uses exactly the `.marrow`/`.mskl` `curve` encoding, so
a caller can compare it against what it wrote. The `keys` array is capped at 256
entries with `keys_truncated` set when the selection is larger; `key_count` and
`changed_key_count` always describe the full selection.

This per-key echo is the "affected-key reporting" criterion 5 requires, and it
doubles as the surface's **read-back channel**: a `dry_run` call reports the
current stored curve of every selected key without mutating anything. MAR-169
therefore does not need to extend `timeline.describe`, and that inspection
operation's payload stays byte-identical.

### 13.5 Dry-run and live paths

Both paths run the same `apply` lambda used by `timeline.retime_keyframes`:
first `ensure_transform_timeline_edit()` / `ensure_mesh_deform_timeline_edit()` /
`ensure_slot_color_timeline_edit()` for each selector, so a runtime-only track
is materialized rather than reported as missing; then
`set_keyframe_interpolation()`.

- **Dry run**: `ProjectData candidate = *session.project();` apply; report;
  discard. The session is never touched, so `project_revision()`,
  `undo_count()`, and `dirty()` are unchanged. `marrow_agent_dispatch_smoke`
  asserts this the way it already does for every other dry-run operation.
- **Live**: `session.begin_edit({EditKind::EditProperty, "Set timeline key
  easing via Agent" / "...keys...", "timeline:interpolation", false,
  Project | Runtime | Preview})`, apply, `cancel()` on error or on
  `!result.changed`, then `commit_or_error()` with
  `CommitPolicy{"Failed to set timeline key easing: "}`.

Undo/redo behaviour is inherited from `EditorSession`: one operation, one
history entry, reversible by the existing `undo` operation, which is exactly
what the shell gesture produces for the same edit.

### 13.6 MCP tool

`tools/mcp/tools/editing.py` gains one `types.Tool` named
`timeline.set_interpolation`, placed immediately after
`timeline.retime_keyframes`, plus two helper schemas:

- `_timeline_interpolation_key_schema()` — a `oneOf` over the three supported
  kinds only. Deliberately not a reuse of `_timeline_retime_key_schema()`, which
  admits `draw_order`, `event`, and `slot_attachment`.
- `_bezier_interpolation_schema()` — the tuple form that expresses the X
  constraint the C++ side enforces:

```python
{
    "oneOf": [
        {"type": "string", "enum": ["linear", "stepped"]},
        {
            "type": "array",
            "items": [
                {"type": "number", "minimum": 0, "maximum": 1},
                {"type": "number"},
                {"type": "number", "minimum": 0, "maximum": 1},
                {"type": "number"},
            ],
            "minItems": 4,
            "maxItems": 4,
        },
    ]
}
```

The existing `_interpolation_schema()` is left unchanged so the other tools keep
their current contract. The schema is advisory — the MCP server forwards every
call verbatim — so the C++ primitive remains the sole authority; the schema
exists to give the model an accurate description of the limits.

## 14. The Operation Count: Every Place 56 Appears

The implementer must change only the places that state the **current** surface
size, and must not rewrite the historical `Validation Results` sections, which
record what was true at an earlier milestone.

Discover the full list with:

```bash
rg -n '\b56\b' --glob '!build*' src tools AGENTS.md docs .agents
```

| Location | Today | After MAR-169 | Kind |
| --- | --- | --- | --- |
| `src/editor/agent_dispatch.cpp` `kOperationSpecs[]` | 56 rows | 57 rows | code |
| `src/samples/agent_dispatch_smoke.cpp:39` `std::array<OperationExpectation, 56>` | 56 | **57** + one new row | test |
| `src/editor/shell_smoke_graph.cpp:143-144` `operation_count_before != 56U` and its message | 56 | **57** | test |
| any `56` MAR-168 adds in `validate_timeline_graph_edit_shell_smoke()` | 56 | **57** | test |
| `tools/mcp/test_client.py:45` `len(registry_names) == 56` | 56 | **57** | test |
| `tools/mcp/test_client.py:47` `len(mcp_names) == 56` | 56 | **57** | test |
| `tools/mcp/test_client.py` `new_edit_operations` set | 6 names | add `timeline.set_interpolation` | test |
| `AGENTS.md` `Current Validation` "Agent registry validation (56 operations, ...)" | 56 | **57** | current doc |
| `AGENTS.md` `Project State` line | MAR-168 next | MAR-169 done, MAR-170 next | current doc |
| `docs/root1/editing-gap-analysis.md:23` "56개 에이전트 오퍼레이션" | 56 | **57** | current doc |
| `docs/root1/editing-gap-analysis.md:83` "56개 오퍼레이션(조회 12, 검증 3, 관리 10, 편집 31)" | 56 / 31 | **57 / 32** | current doc |
| `docs/root1/editing-gap-analysis.md:190` "현재 56-op parity" | 56 | **57** | current doc |
| `docs/root1/refector.md:20` "raised the **current** registry to the exact 56-operation total" | 56 | **57** | current doc |
| `docs/root1/refector.md:112` "for an exact **current** total of 56" | 56 | **57** | current doc |
| `AGENTS.md` MAR-155/158/159/…/MAR-167 and MAR-168 `Validation Results` sections | 56 | **unchanged** | historical record |
| `AGENTS.md:679,687` MAR-128/MAR-155 historical parity rows | 49 / 55 / 56 | **unchanged** | historical record |
| `docs/root1/editing-gap-analysis.md:293,302` MAR-155 checkpoint text | 56 | **unchanged** | historical record |
| `docs/root1/discription.md:47` MAR-167 paragraph | 56 | **unchanged** | historical record |
| `docs/root1/refector.md:161,305,321` Task #28 checkpoint text (dated 2026-08-16) | 56 | **unchanged** | historical record |
| `docs/root1/platform-validation.md:35` Task #28 regression refresh (dated 2026-08-16) | 56 | **unchanged** | historical record |
| `.agents/tasks/prd-marrow-runtime.json` acceptance criteria of MAR-162/163/… | 56 | **unchanged** | historical record |

The new `AGENTS.md` MAR-169 section states 57 and says the growth is additive.

**Known false positives** that the `rg` sweep will surface and that must not be
touched: `src/editor/shell_viewport_ui.cpp` (`IM_COL32(56, 61, 69, 255)`),
`docs/root1/ui-design-spec.md` (`(51, 56, 64)`),
`src/tests/runtime_math_tests.cpp` (`"x": 56.0`),
`docs/root1/platform-validation.md:238` (a `56,995,840`-byte binary size), and
`.agents/tasks/prd-marrow-runtime.json` timestamps and the
`PhysicsBoneState ... 56 bytes/bone` note.

The rule to apply when classifying a hit: if the sentence carries a date, a
milestone ID, or the words "historical", "baseline", or "checkpoint ... passed",
it is a record of what was true then and stays at 56. If it states what the
surface *is*, it becomes 57.

## 15. Error Handling and Fail-Closed Rules

The graph refuses to arm a handle drag when any of these holds, and cancels
mid-gesture when any becomes true:

| Condition | Result |
| --- | --- |
| No project, no animation, or no resolvable focused row | no candidate; live gesture cancels |
| Projection status is not `Ready` | no candidate; live gesture cancels |
| `!timeline_track_is_editable(row)` | no candidate |
| `authoring_gesture_active(*state)` | no candidate |
| No `active_key`, or it is on another track | no handles; no candidate |
| Active key is the last key of the track | no handles; no candidate |
| `frame.time_span <= kMinimumSegmentSeconds` | no handles; no candidate |
| Active component index is hidden or out of range | no handles; no candidate |
| Non-finite pointer, view scale, or mapped control point | cancel with rollback |
| Control point outside `[0, 1]` reaching the primitive | cancel with rollback |
| Non-finite or out-of-float32-range control point | cancel with rollback |
| Lost `TimelineKeyRef` identity or changed active component | cancel with rollback |
| Materialization failure | cancel with rollback |
| `set_keyframe_interpolation()` error | cancel with rollback |
| `refresh_runtime()` failure | cancel with rollback |
| Escape, focus loss, shutdown, tab switch, project reload | cancel with rollback |

The X clamp is **not** a failure: the handle stops at the boundary and the drag
continues, exactly as neighbour clamping does for the dopesheet retime.

## 16. Non-Goals

- Curve presets, remembered defaults, automatic handles, loop-boundary sync,
  time scaling, preview speed (MAR-170 through MAR-174).
- Multi-key easing assignment from the graph.
- Incoming handles. The runtime has one outgoing easing per key.
- Per-component curves in any form.
- Easing editing from the Deform graph lane; Deform stays excluded from the
  graph projection while remaining supported by the primitive and the agent.
- Changing `timeline.describe`, `set_transform`, `set_deform_keyframe`, or
  `set_slot_color_keyframe`.
- Refactoring the existing numeric `Bezier X1/Y1/X2/Y2` inspector fields in
  `shell_timeline.cpp` onto the new primitive. It is tempting and would be a
  net improvement, but it is a behaviour-preserving refactor of a surface this
  story does not otherwise touch, and it would enlarge the diff that has to be
  proven byte-safe. It is recorded as a follow-up, not done here.
- Persisting graph view, drag, handle, or curve-mode state. All of it stays
  transient `TimelineEditorState`.
- Manual-visible-UI, Windows 11, or physical-input qualification credit.
  MAR-192 through MAR-210 stay open.

## 17. Decisions Taken Under Ambiguity

1. **Handles belong to the active key only, not to the whole selection.** The
   story says "the active key's outgoing shared four-value easing", singular.
   Applying one absolute curve to many keys is a preset operation and is
   MAR-170's explicit subject; doing it here would pre-empt that story and would
   make the "one drag, one transaction" proof cover a key set the user never
   pointed at. The **agent** operation does accept many selectors, because a
   scripted caller has no cursor and multi-key assignment is the natural API
   shape there.
2. **Handles are drawn for one component only.** One curve should have one pair
   of handles. Drawing a pair per visible component would suggest, visually,
   that each component has its own curve — the exact misconception criterion 3
   exists to prevent.
3. **No axis lock for handle drags.** MAR-168 locked the axis because horizontal
   and vertical motion went to two different primitives writing two different
   key sets, so a lock kept the rollback proof single-path. A handle drag sends
   both coordinates to one primitive writing one field, so the proof is already
   single-path and a lock would only make the curve un-authorable.
4. **Flat segments get a 100-pixel fallback span rather than being refused.**
   The fixture proves the case is real and common: `body` slot colour has a flat
   Blue segment sharing its curve with varying R, G, and A. Refusing would make
   which component you happen to be displaying decide whether the shared curve
   is editable, which contradicts criterion 3 and criterion 6's requirement to
   cover flat curves. The span is view-relative so it is well conditioned at any
   zoom, and the view is frozen so it is constant within a gesture.
5. **The flatness test is "less than one logical pixel apart", not exact
   equality.** Exact equality would leave a `1e-10`-degree Rotate span
   technically non-flat while making `cy = dv / 1e-10` numerically explosive.
   The pixel test expresses the real condition: the segment's value axis cannot
   represent a handle position.
6. **The fallback span is always positive.** Keeping the sign of an
   infinitesimal raw span would let a `1e-18` sign flip invert the handle's
   vertical direction between two visually identical segments.
7. **Zero-duration segments get no handles, but the agent still accepts them.**
   Geometry is undefined when the normalized time axis has no extent, so there
   is no honest fallback. The stored value, however, is well defined and
   round-trips through both formats, and `set_transform` already writes it, so
   rejecting it in the value path would break imported documents for no gain.
8. **`[1/3, 1/3, 2/3, 2/3]` is the conversion seed for both Linear and
   Stepped.** It is the unique evenly spaced cubic exactly equal to Linear, so
   the common conversion changes no evaluated value. The struct default
   `[0, 0, 1, 1]` is also exactly linear but puts both handles on top of the
   anchors, where they cannot be seen or grabbed. Stepped has no cubic
   equivalent at all, so any seed changes it; using the same neutral constant
   avoids inventing an arbitrary approximation and keeps the rule a single
   stateless constant.
9. **X clamps in the drag but rejects in the primitive.** Clamping in the drag
   gives the boundary a natural feel and matches how neighbour collision already
   behaves for retime. Rejecting in the primitive is required because the Agent
   calls it with arbitrary numbers and because `[0, 1]` is exactly the invariant
   both file loaders enforce — a clamping primitive would silently rewrite a
   caller's explicit request, which is worse API behaviour than an error.
10. **No artificial bound on Y.** Criterion 2 says "finite Y overshoot is
    allowed" without qualification. An extreme but finite curve is legal data;
    the graph's existing fail-closed invalid-data message covers it, and a test
    pins that behaviour rather than leaving it to chance.
11. **`interpolation` is required by the agent operation.** `interpolation_arg()`
    defaults to linear when absent, which is correct for `set_transform` (where
    a new key needs *some* easing) and destructive here (where it would silently
    linearize every selected key).
12. **Deform is supported by the primitive and the agent but not by the graph.**
    `DeformKeyframeEdit` really does carry an easing and the inspector already
    edits it. Excluding it from the primitive would force MAR-171 to add a
    second, near-identical primitive. Excluding it from the *graph* preserves
    MAR-167's projection contract unchanged.
13. **The dry-run `previous_interpolation` echo is the read-back channel.**
    The alternative was extending `timeline.describe` to report per-key curves.
    The echo satisfies criterion 5's affected-key requirement anyway, so reusing
    it keeps a second inspection operation's payload untouched.
14. **`runtime::InterpolationKind` is reused instead of a new
    `TimelineEasingKind`.** Same taxonomy, already reachable from
    `authoring.hpp`, one fewer public type to keep in sync.
15. **The primitive takes four `double`s plus a kind rather than a constructed
    `runtime::Interpolation`.** Constructing the `Interpolation` first would
    narrow to float32 before validation could see the original value, and would
    add every rejected agent input to the process-wide LUT cache.
16. **Component checkboxes and `Fit` are disabled during any graph drag.**
    MAR-168 froze the view but left the checkboxes live; hiding the dragged
    component mid-gesture would leave an invisible drag running. Scoping the fix
    to both drag kinds fixes the same latent hazard in MAR-168's value drag
    without changing its contract.

## 18. Validation Strategy

### 18.1 UI-free focused tests — `marrow_timeline_graph_model_tests`

- `seed_control_points()` returns `existing` for Cubic and
  `kLinearEquivalentControlPoints` for Linear and Stepped.
- `make_segment_frame()` produces the raw value span for a varying segment, the
  100-pixel fallback with `flat_value_span == true` for the flat
  `1.0 -> 1.0` colour segment, and `std::nullopt` for a zero-duration segment,
  a non-finite anchor, and an out-of-range component index.
- `build_handle_geometry()` places handle 1 at `cx1`/`cy1` and handle 2 at
  `cx2`/`cy2` along the frozen frame, and returns `std::nullopt` for a missing
  active key, the last key, a hidden component, and a degenerate segment.
- `build_handle_geometry()` -> `control_points_from_handle_pointer()` round-trips
  each control point to `1e-9` for a view with a negative `view_start_seconds`
  and a sub-unit `pixels_per_value`, on both a varying and a flat segment.
- `control_points_from_handle_pointer()` clamps `cx` to exactly `0.0` and `1.0`
  for pointers far off both ends, leaves `cy` unclamped at `-2.5` and `+3.75`,
  carries the untouched control point through byte-identically, and returns
  `std::nullopt` for every non-finite pointer, a non-positive
  `pixels_per_value`, and a non-positive `pixels_per_second`.
- The handle positions agree, within `1e-9`, with the polyline
  `build_geometry()` produces for the same control points, proving the render
  and drag math cannot drift.
- `hit_test_handle()` is inclusive at exactly the radius, prefers the nearer
  handle, and misses beyond the radius.

### 18.2 UI-free focused tests — `marrow_project_smoke`

Direct `set_keyframe_interpolation()` coverage, alongside
`validate_mar168_graph_scalar_authoring()`:

- one case per supported family (Transform Rotate/Translate/Scale/Shear,
  Slot Color, Deform) asserting the four control points and the kind landed and
  that `time`, `angle`, `x`, `y`, `color`, and `vertex_offsets` are byte-identical;
- **segment-wide identity**: after writing a curve on a Translate key, both `x`
  and `y` of that key still read through the one changed `interpolation`, and
  no other keyframe in the project changed;
- **Linear -> Cubic** and **Stepped -> Cubic** conversion on the fixture's
  `spine` translate keys 0 (linear) and 1 (stepped), asserting the resulting
  kind and control points;
- **Cubic -> Linear** and **Cubic -> Stepped**, asserting the control points are
  no longer serialized;
- **overshoot** `[0.2, -0.4, 0.8, 1.6]` accepted and round-tripped exactly;
- **X limits**: `[0.0, 0.0, 1.0, 1.0]` accepted; `[-1e-6, 0, 0.5, 1]`,
  `[0, 0, 1.0000001, 1]`, `[NaN, 0, 0.5, 1]`, `[inf, 0, 0.5, 1]`, and
  `[0, 1e300, 0.5, 1]` each rejected with `serialize_project()` byte-identical;
- **unsupported kinds**: `DrawOrder`, `Event`, and `SlotAttachment` selectors
  each rejected atomically;
- **duplicate selector**, **unresolvable selector**, and **empty selector list**
  each rejected atomically;
- **no-change**: writing the identical curve twice returns
  `changed == false`, `key_count` populated, no error, and a byte-identical
  project;
- **save/reload**: save the edited project, reload it, and assert the four
  control points survive bitwise — this is the direct proof that MAR-169 cannot
  write a document the loader rejects;
- **export**: `--export-runtime` / `--export-binary` plus
  `marrow_inspect --compare` for JSON/MBIN equivalence of the edited curve.

### 18.3 UI-free focused tests — `marrow_timeline_model_tests`

- `completion_decision(true, false)` cancels with zero history entries and
  `completion_decision(true, true)` commits one, reasserted for the easing
  gesture's reuse of the shared helper.

### 18.4 Headless shell smoke — `src/editor/shell_smoke_graph.cpp`

A new `validate_timeline_graph_easing_shell_smoke()` scenario with an isolated
session, registered in `shell_smoke_scenarios.hpp` and `shell_smoke.cpp`,
covering:

- a complete handle drag on `spine` Translate: exactly one history entry, the
  key's `time`/`x`/`y` byte-identical, the stored control points matching the
  pointer-derived values;
- **criterion 3, direct**: set `active_component` to `Y`, drag handle 1, then
  rebuild the projection and assert the `X` segment's `SegmentKind` and
  `outgoing_easing` control points changed identically, and that exactly one
  `interpolation` field in the whole project differs from the snapshot;
- Linear -> Cubic on key 0 and Stepped -> Cubic on key 1, both inside one undo
  entry each;
- the flat Blue segment of `slot:0:Color`: a drag succeeds, `flat_value_span` is
  reported, and the resulting `cy` matches `pixels / 100.0`;
- an X-clamped drag: the pointer is pushed 400 px past the end anchor, `cx`
  lands on exactly `1.0`, and the gesture stays live;
- an overshoot drag producing `cy > 1` and `cy < 0`;
- a press without motion producing no transaction and no history entry;
- a drag on an already-Cubic segment that ends on its original control points
  committing nothing;
- cancel paths — `cancel_requested`, `cancel_timeline_graph_point_drag()`,
  `cancel_authoring_gestures()`, a forced non-finite pointer, and the active key
  disappearing mid-gesture — each restoring `serialize_project()`,
  `undo_count()`, `redo_count()`, `project_revision()`, `dirty()`, the rebuilt
  dopesheet `key_times`, every rebuilt graph `Key::values`, every rebuilt
  `Segment::kind`, and every rebuilt `outgoing_easing` control point;
- undo then redo of a committed easing edit restoring the curve with
  `selected_keys` and `active_key` bit-identical;
- no handles on the last key, on a zero-duration segment injected into the
  projection, and while another authoring gesture is active;
- materialization: the first drag on the runtime-only `spine` Translate track
  copies all three keys into the project rather than replacing the track;
- `agent_operation_descriptor_count() == 57` before and after every case.

### 18.5 Actual-frame smoke — `src/editor/shell_smoke_frames.cpp`

Using real ImGui mouse events against real rendered coordinates from
`TimelineGraphRenderStats`:

- render a frame with an active key on `bone:1:Translate`, assert
  `handles_drawn` and finite `first_handle_x/y`;
- press on the reported first handle, move 30 px, assert
  `handle_gesture_active` and that MAR-168's `value_gesture_active` and
  `retime_gesture_active` stay false — the handle hit test wins over the point
  hit test;
- press on a key point instead and assert MAR-168's point path still wins where
  no handle is under the cursor;
- assert the component checkboxes and `Fit` are inert while a handle drag is
  live;
- assert switching to the Dopesheet tab mid-drag cancels with rollback, and that
  the dopesheet still retimes normally afterwards.

### 18.6 Agent smoke — `marrow_agent_dispatch_smoke`

- registry is exactly 57 operations with the new row's metadata
  (`edit`, mutating, not review, dry-run supported);
- `timeline.set_interpolation` dry run leaves `project_revision()`,
  `undo_count()`, and `dirty()` unchanged and reports the correct
  `previous_interpolation` for every selected key;
- live call mutates, reports `changed_key_count`, and adds exactly one history
  entry;
- a second identical live call returns the `no_change` error code;
- `undo` restores the previous curve, verified by a follow-up dry run;
- rejection cases: X out of range, non-finite, missing `interpolation`,
  unsupported `kind`, unresolvable selector, duplicate selector, and an empty
  `keys` array;
- a Deform selector succeeds, proving the family split in §3.1.

### 18.7 MCP — `tools/mcp/test_client.py`

- `len(registry_names) == 57`, `len(mcp_names) == 57`,
  `set(registry_names) == set(mcp_names)`, all names unique;
- `timeline.set_interpolation` present in `new_edit_operations` and in the MCP
  edit tool set, with the registry metadata row asserted explicitly;
- dry run -> live -> dry run -> `undo` -> dry run sequence proving mutation,
  overshoot preservation, and undo through the echoed
  `previous_interpolation`;
- rejection of an out-of-range X and of a `draw_order` key.

### 18.8 Display and compatibility gates

See the "Full verification checklist" in
`docs/superpowers/plans/2026-08-30-mar-169-graphical-bezier-handles.md`.
Automated display tests prove the exercised ImGui/display path only. They add no
manual-visible-UI, Windows 11, or physical-input qualification credit.

## 19. Documentation and Milestone Closure

After code and every required gate pass:

- add a `MAR-169 Graphical Shared Bezier Handle Editing Validation Results`
  section to `AGENTS.md` in the existing table plus command-output format, and
  update `Current Validation`'s registry line to 57 operations;
- update `docs/root1/discription.md`, `quick-start.md`, `concepts.md`,
  `editing-gap-analysis.md`, `refector.md`, `platform-validation.md`, and
  `format-spec.md` where the current surface size, the "MAR-169 is next"
  wording, or the graph's editing contract is now stale;
- `format-spec.md` gains one clarifying sentence that the editor guarantees
  `cx1`/`cx2` stay in `[0, 1]`, matching the loaders' existing rule. **No new
  field and no version change**;
- mark `MAR-169` done with the verified completion date and leave `MAR-170`
  open;
- leave MAR-192 through MAR-210 open and add no platform qualification credit;
- change this document's status to `Implemented and validated` only after every
  gate is green.

## 20. Commercial-Tool Reference Boundary

The interaction direction is informed by the official Spine Graph and Live2D
Cubism Graph Editor documentation:

- Spine Graph: <https://us.esotericsoftware.com/spine-graph>
- Live2D Graph Editor: <https://docs.live2d.com/en/cubism-editor-manual/grapheditor/>

Marrow intentionally does not copy per-component Bezier handles, incoming
handles, or free per-component curve ownership. The runtime's one-outgoing-easing-
per-key model and Marrow's shared parent-key contract take precedence over
surface similarity to another editor.
