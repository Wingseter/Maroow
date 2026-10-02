# MAR-171 Project-Local Automatic Curve Handles Design

**Date:** 2026-08-30

**Status:** Implemented and validated (2026-08-30)

**Authority:** `.agents/tasks/prd-marrow-runtime.json` story `MAR-171` and
`docs/root1/discription.md`

**Depends on:** MAR-170, whose preset table, preset identity rule, and
preference boundary are defined in
`docs/superpowers/specs/2026-08-30-mar-170-curve-presets-and-defaults-design.md`.
MAR-170 in turn depends on MAR-169's interpolation primitive, segment-wide
curve identity model, and `timeline.set_interpolation` operation
(`docs/superpowers/specs/2026-08-30-mar-169-graphical-bezier-handles-design.md`),
which inherits MAR-168's gesture and transaction model
(`docs/superpowers/specs/2026-08-30-mar-168-graph-key-time-value-editing-design.md`).

> **MAR-170 is not implemented and MAR-169's post-review cleanup is still
> landing while this document is written.** Every API contract quoted from
> either story below is quoted from its *plan or spec*, not from as-built
> source, except where this document explicitly says "as built" and names a
> line. The implementation plan's **Task 0 is mandatory** and must reconcile
> against the code that actually exists. Two as-built deltas already known to
> be in flight are recorded in §4.3.

## 1. Goal

MAR-169 made one key's shared easing draggable. MAR-170 made six fixed presets
assignable and remembered a per-user default. Both write **absolute** curves: a
number the animator typed, dragged, or picked, which then stays exactly where it
was put no matter what happens to the keys around it. Move the neighbouring key
and the curve is silently wrong; the animator has to re-drag every handle.

MAR-171 adds the other half of the pair: a key can instead record the **intent**
"my outgoing easing is whatever a monotone interpolant through my neighbours
says it should be", and the editor keeps that promise on every edit that
changes the neighbourhood.

Concretely, MAR-171 adds exactly four things and nothing else:

1. Two optional, additive, **`.marrow`-only** per-keyframe fields — `curve_mode`
   (`manual` | `auto`) and `curve_driver` (which scalar series drives the
   computation) — absent from every existing project and therefore defaulting
   every existing project to today's manual behaviour, byte for byte (§8).
2. A pure, unit-testable **monotone Fritsch–Carlson** resolver that turns a
   driver series into one `[cx1, cy1, cx2, cy2]` per outgoing segment, with a
   proof that the produced control points satisfy the `cx ∈ [0, 1]` format
   invariant unconditionally, before and after `float32` narrowing (§6).
3. **Recomputation of every affected auto curve inside the same transaction** as
   the edit that invalidated it — neighbour time, neighbour value, driver,
   retime, key insertion, key removal, paste, and duration (§9).
4. One new Agent operation, `timeline.set_curve_mode`, and its matching MCP
   tool, taking the registry from 57 to exactly **58** (§13).

The runtime never learns any of this. The exported `.mskl` and `.mbin` carry
only the resolved `curve` values they already carry today (§8.6).

## 2. Approved Product Decisions

1. **Curve mode is per key**, not per track and not per project. Story criterion
   4 says "dragging either handle switches **the segment** to manual"; a segment
   is owned by its start key, so the mode is a property of that key.
2. **The driver is per key too**, stored next to the mode, because the two are
   one record of intent and because per-key storage rides through copy, paste,
   retime, delete, and materialization for free (§8.2).
3. **Auto mode always stores a resolved `CubicBezier`.** Resolution is eager:
   the four numbers are written into the pre-existing `interpolation` field at
   the moment of the transaction. The mode field records *why* those numbers are
   what they are; the numbers themselves remain the single source of truth for
   every reader, including the runtime, `.mskl`, `.mbin`, and any older editor
   build (§8.6).
4. **`cx1 = 1/3` and `cx2 = 2/3` always.** The normalized Bezier form of a cubic
   Hermite has evenly spaced x control points by construction. This is not a
   clamp and not a choice; it is what the algebra produces (§6.2). The format
   invariant is therefore satisfied unconditionally rather than defended (§6.5).
5. **Deform is excluded from auto mode.** A deform key's value is a
   high-dimensional offset vector with no canonical scalar; `DeformKeyframeEdit`
   gains no field and the primitive rejects Deform selectors (§7.3).
6. **Any explicit absolute easing write demotes the key to manual**, in the same
   transaction, with no extra history entry. This is enforced *inside*
   `set_keyframe_interpolation()`, so MAR-169's handle drag, MAR-170's presets,
   the numeric inspector, and the Agent all inherit it and none of them can
   forget it (§9.4).
7. **Recomputation is whole-track, not windowed.** A value change perturbs a
   four-segment window and the Fritsch–Carlson clamp can propagate further
   still; recomputing every auto segment of the edited animation is O(keys),
   provably correct, and needs no propagation argument (§9.2).
8. **Load never rewrites.** Opening a project with a stale `curve_mode` /
   `curve` pair does not repair it, does not dirty the project, and does not
   create a history entry. The first auto-affecting transaction re-resolves it,
   and `timeline.set_curve_mode` is the explicit way to force one (§9.6).
9. One new Agent operation, `timeline.set_curve_mode`, brings the registry to
   exactly **58** operations. It is purely additive: no existing operation's
   name, category, flags, or response shape changes. `set_keyframe_interpolation()`
   gains the demotion side effect described in decision 6, which is additive to
   its observable result and is the only change to a MAR-169 contract (§14).
10. Loop-boundary key synchronization, lane metadata, managed boundary keys,
    selected-key time scaling, and preview playback speed remain out of scope
    (MAR-172 through MAR-174).

## 3. Scope

### 3.1 Surfaces that gain curve-mode authoring

| Surface | Target keys | Mechanism |
| --- | --- | --- |
| Graph tab toolbar | every compatible key in `timeline_editor.selected_keys` | `Curve mode:` `Manual`/`Auto` buttons plus a `Driver:` combo |
| Graph tab plot | the active key | auto handles are drawn in the auto colour; grabbing one demotes |
| Agent `timeline.set_curve_mode` | the caller's explicit selectors | absolute mode + driver |
| MCP `timeline.set_curve_mode` | same | same |

Compatible families are the two whose keys carry both an `interpolation` field
and an addressable scalar series: `TransformKeyframeEdit` and
`SlotColorKeyframeEdit`. `DeformKeyframeEdit` carries an easing but no scalar
series (§7.3). `DrawOrderKeyframeEdit`, `EventKeyframeEdit`, and
`SlotAttachmentKeyframeEdit` carry neither.

Note this is a **narrower** family set than MAR-169's and MAR-170's, which both
include Deform. The asymmetry is deliberate and is stated in every rejection
message.

### 3.2 Surfaces that gain automatic recomputation

Every transaction that can change a Transform or Slot Color key's **time**,
**value**, **existence**, or an animation's **explicit duration** calls the
resolver before `refresh_runtime()`, inside the same transaction. The complete
list is enumerated in §9.2 and is the plan's checklist.

### 3.3 Explicit exclusions

MAR-171 does not add:

- loop-boundary key synchronization, lane-level metadata, or managed boundary
  keys (MAR-172) — and critically, **no field on the timeline-edit (lane)
  object**, so MAR-172 starts from an unclaimed namespace exactly as MAR-170
  left the keyframe namespace unclaimed for this story;
- selected-key time scaling (MAR-173) or preview playback speed (MAR-174);
- auto mode for Deform, Draw Order, Event, or Slot Attachment keys;
- a per-component curve of any kind. The driver selects which series is *read*;
  it never affects which bytes are *written*, because
  `set_keyframe_interpolation()` and the resolver both write the one shared
  `interpolation` field (§7.4);
- incoming handles. The runtime has one outgoing easing per key;
- a "default curve mode" preference. `editor-settings.json` v1 and
  `kEditorSettingsVersion` are untouched; newly authored keys are always manual
  (§9.5);
- automatic **tangent smoothing across a loop boundary**. The last key of a
  track has no outgoing segment and its stored easing is left byte-identical
  (§6.4);
- resolution at load, at save, at export, or on a timer (§9.6);
- any change to `timeline.describe`, `timeline.retime_keyframes`'s arguments,
  MAR-170's preset table, MAR-170's preference flow, or MAR-169's clamp/reject
  split;
- refactoring the numeric `Interpolation` combo and `Bezier X1/Y1/X2/Y2` fields
  in `shell_timeline.cpp` onto the shared primitive. MAR-169 §16 and MAR-170
  §17 both deferred it; MAR-171 keeps the deferral, but see §9.4 for the one
  thing that inspector *must* do;
- manual-visible-UI, Windows 11, or physical-input qualification credit.
  MAR-192 through MAR-210 stay open.

### 3.4 Compatibility boundaries

MAR-171 changes none of the following:

- **`.mskl` v1.** Identical `curve` encoding, unchanged reader and writer
  (`src/runtime/skeleton_parse.cpp`). No new field reaches it (§8.6);
- **`.mbin` v2.** Cubic easing stays four `float32`
  (`src/runtime/binary.cpp`), unchanged reader and writer;
- **C ABI v1.** `include/marrow/c_api/**` and `src/c_api/**` are untouched;
- **`editor-settings.json` v1.** `kEditorSettingsVersion` stays `1`;
  `src/editor/preferences.cpp` and `include/marrow/editor/preferences.hpp`
  change by zero bytes;
- **`runtime::Interpolation`, `CubicBezierControlPoints`, and the LUT cache.**
  MAR-171 constructs cubics through the existing
  `Interpolation::cubic_bezier()`, after validation, exactly as MAR-169 does;
- `SelectionSet` entity identity, runtime/GPU resource ownership, dopesheet
  retime behaviour, MAR-168's point drag, MAR-169's handle geometry and
  clamp/reject split, MAR-170's preset constants and preference isolation.

The **`.marrow` change is strictly additive**: two optional members on two
keyframe object kinds, absent unless auto mode is authored. A project with no
auto key serializes byte-identically to today — this is a testable property, not
a claim (§18.2).

The public-header changes are additive plus one relocation:

- `include/marrow/editor/project.hpp` gains `enum class TimelineCurveMode`, two
  members on `TransformKeyframeEdit`, and two members on
  `SlotColorKeyframeEdit`;
- `enum class TimelineScalarComponent` **moves** from
  `include/marrow/editor/authoring.hpp` to
  `include/marrow/editor/project.hpp`, unchanged in name, enumerators, order,
  and underlying type. `authoring.hpp` includes `project.hpp`, so every existing
  include site keeps compiling with zero edits and
  `offset_keyframe_scalars()`'s signature is byte-identical (§11.1);
- `include/marrow/editor/authoring.hpp` gains two result structs and three
  functions appended after MAR-170's preset accessors.

**Why the format is safe by construction.** Both loaders reject a `curve` array
whose x control points leave `[0, 1]`:

```text
src/editor/project.cpp:1639          "bezier x control points must stay within [0, 1]"
src/runtime/skeleton_parse.cpp:1727  "bezier x control points must stay within [0, 1]"
```

Every curve MAR-171 generates has `cx1 = 1/3` and `cx2 = 2/3` **exactly**, as an
algebraic consequence of the Hermite-to-Bezier conversion rather than as a
clamp (§6.2). Both narrow into the open interval `(0, 1)` in `float32`
(`0.3333333432674408f`, `0.6666666865348816f`), so the invariant survives
narrowing with enormous margin. A `static_assert` pins the two constants and a
runtime check rejects any resolved control point that is not finite and inside
`[0, 1]` (§6.6), so the proof is enforced rather than merely stated.

## 4. Existing Boundaries Reused

### 4.1 Reuse table

| Concern | Reused primitive | Location |
| --- | --- | --- |
| Curve write, validation, atomic rollback | `set_keyframe_interpolation()` | `authoring` (MAR-169) |
| Curve write result shape | `TimelineInterpolationResult` | `authoring.hpp` (MAR-169) |
| Preset identity / `Custom` readout | `curve_preset_of()` | `authoring.hpp` (MAR-170) |
| Scalar component vocabulary | `TimelineScalarComponent` | `authoring.hpp` -> `project.hpp` (MAR-168) |
| Family/component authorability | the `read_scalar_component()` switch | `authoring.cpp` (MAR-168) |
| Project-domain key resolution | `TimelineKeySelector`, `resolve_timeline_key()` | `authoring` |
| Optional project-local block pattern | `ProjectSnapSettings` / `.marrow.snap` | `project.hpp:294`, `project.cpp:305,4193,4797,6442` (MAR-165) |
| Cubic parse/serialize and its `[0,1]` gate | `parse_interpolation()`, `build_interpolation_value()` | `project.cpp:1579,1653` |
| Runtime-only export builder | `build_runtime_document()` | `project.cpp:4596` |
| Overlay materialization | `ensure_*_timeline_edit[_index]()` | `project.hpp:689-723`, `timeline_controller` |
| Live preview / one history entry | `EditorSession::EditTransaction` | `session` |
| Gesture mutual exclusion | `authoring_gesture_active()` | `shell_state.hpp:781-796` |
| Gesture cancel registration | `cancel_authoring_gestures()` | `shell_core.cpp:431-491` |
| Handle geometry and hit test | `build_handle_geometry()`, `hit_test_handle()` | `timeline_graph_model` (MAR-169) |
| Agent dry-run / commit / no-change shape | `commit_or_error()`, `CommitPolicy` | `agent_dispatch_internal.hpp:35-71` |
| Agent key-selector parse shape | the `timeline.set_interpolation` `keys` loop | `agent_handlers_editing.cpp:894-972` |
| Agent per-key echo and 256 cap | the `response_delta` lambda | `agent_handlers_editing.cpp:1035-1086` |
| Agent error classification | `classify_timeline_key_error()` | `agent_handlers_editing.cpp:172-175` |
| Runtime curve construction | `runtime::Interpolation::cubic_bezier()` | `runtime/animation` |

MAR-171 introduces exactly two new concepts — the curve mode/driver record and
the Fritsch–Carlson resolver — and reuses everything else.

### 4.2 The one MAR-169 contract that changes

`set_keyframe_interpolation()` gains a documented side effect: **every key it
writes is set to `TimelineCurveMode::Manual`**, and `changed_key_count` counts a
key whose *mode* changed even when its four control points did not.

This is the only way to make decision 6 unforgeable. The alternative — asking
every caller to demote — has four call sites today (MAR-169's drag, MAR-170's
preset row, MAR-170's agent token path, the numeric inspector) and an unbounded
number tomorrow, and a single omission produces a key whose authored curve is
silently overwritten by the next neighbour edit. Putting the rule in the one
function that writes an absolute easing makes the omission unrepresentable.

Existing MAR-169 assertions that a byte-identical rewrite reports
`changed == false` continue to hold for manual keys, which is every key in every
project that predates MAR-171.

### 4.3 As-built deltas already in flight

Two changes to MAR-169 were landing in the working tree while this document was
written. Task 0 must confirm both:

1. `TimelineGraphHandleGesture` lost its `component`, `component_index`, and
   `handle` members, and `begin_timeline_graph_handle_gesture()` lost its
   `HandleIndex` parameter (9 arguments -> 8). MAR-171 adds one member to that
   struct (§11.4) and must add it to whatever shape actually exists.
2. `gesture.changed` became a **net-state** comparison against
   `original_control_points` rather than a "some frame mutated" flag. MAR-171
   depends on this and extends it (§9.4): a drag that leaves and returns to the
   exact original points is a cancel **unless** the key was auto at press, in
   which case the demotion is itself the authored change.

## 5. Architecture

```text
src/editor/curve_auto.hpp / .cpp                (NEW, pure, no ProjectData, no ImGui)
    automatic_curve_control_points(samples) -> per-segment [cx1,cy1,cx2,cy2]
        secants -> three-point tangents -> flat zeroing -> Fritsch-Carlson clamp
        -> Hermite-to-Bezier normalization
                      |
                      v
include/marrow/editor/authoring.hpp / src/editor/authoring.cpp
    set_keyframe_curve_mode()        <- writes intent, then resolves
    resolve_automatic_curves()       <- rewrites every stale auto curve
    set_keyframe_interpolation()     <- MAR-169, now also demotes to manual
                      |
        +-------------+--------------------------+
        |                                        |
        v                                        v
src/editor/timeline_controller.cpp        src/editor/agent_handlers_editing.cpp
    apply_timeline_curve_mode()               timeline.set_curve_mode          (NEW)
    resolve_timeline_auto_curves()            timeline.set_interpolation       (demotes)
        called from every timeline              timeline.retime_keyframes      (resolves)
        transaction listed in 9.2               set_transform                  (resolves)
        |                                       set_slot_color_keyframe        (resolves)
        v                                       remove_transform_keyframe      (resolves)
    EditorSession::EditTransaction              animation.set_duration         (seam)
        (refresh_runtime -> preview,                     |
         commit -> exactly one history entry)            v
                                              tools/mcp/tools/editing.py
                                                  timeline.set_curve_mode      (NEW)

src/editor/project.cpp
    parse / validate / serialize `curve_mode` + `curve_driver`   (.marrow only)
    build_runtime_document()                                     (UNCHANGED)
```

The pure math has no dependency on `ProjectData`, the session, ImGui, or the
runtime, so it is unit-testable in isolation and its monotonicity claims can be
checked against a sampled `runtime::Interpolation::transform()` without a
project. The project-domain layer has no dependency on the shell. The Agent
handler calls the *same* two primitives the shell calls, which is what makes
criterion 5's "matching ... behavior" structural rather than coincidental.

## 6. The Automatic Curve Algorithm

This section is the heart of the story. Every formula here is stated so that an
implementer can transcribe it and a reviewer can check it without re-deriving
anything.

### 6.1 What the runtime actually evaluates

From `src/runtime/animation.cpp`, restated from MAR-169 §6. The curve is a
unit-square cubic Bezier with fixed endpoints:

```text
P0 = (0, 0)      P1 = (cx1, cy1)      P2 = (cx2, cy2)      P3 = (1, 1)
X(t) = 3(1-t)^2 t cx1 + 3(1-t) t^2 cx2 + t^3
Y(t) = 3(1-t)^2 t cy1 + 3(1-t) t^2 cy2 + t^3
```

`Interpolation::transform(alpha)` clamps `alpha` into `[0, 1]`, solves
`X(t) = alpha` for `t`, and returns `Y(t)`. `interpolate_value()` then computes
`from + (to - from) * transform(alpha)`.

For a segment from key `i` to key `i+1` with
`h = t[i+1] - t[i]` and `dv = v[i+1] - v[i]`:

```text
alpha = (t - t[i]) / h
v(t)  = v[i] + dv * transform(alpha)
```

So the easing's job is to express `(v(t) - v[i]) / dv` as a function of
`(t - t[i]) / h`. That is exactly a **normalized** interpolant.

### 6.2 From monotone Hermite to the unit-square Bezier

A cubic Hermite segment from `(t[i], v[i])` to `(t[i+1], v[i+1])` with end
tangents `m[i]` and `m[i+1]` (in value units per second) has the Bezier control
points

```text
B0 = (t[i],           v[i])
B1 = (t[i]   + h/3,   v[i]   + m[i]   * h/3)
B2 = (t[i+1] - h/3,   v[i+1] - m[i+1] * h/3)
B3 = (t[i+1],         v[i+1])
```

Normalizing with `X = (t - t[i]) / h` and `Y = (v - v[i]) / dv`, and writing the
secant slope as `d = dv / h`:

```text
P0 = (0,   0)
P1 = (1/3, (m[i]   * h/3) / dv)     = (1/3,      m[i]   / (3 d))
P2 = (2/3, 1 - (m[i+1] * h/3) / dv) = (2/3, 1 -  m[i+1] / (3 d))
P3 = (1,   1)
```

Therefore, writing `a = m[i] / d` and `b = m[i+1] / d` for the two
**normalized tangents**:

```text
cx1 = 1/3          cy1 = a / 3
cx2 = 2/3          cy2 = 1 - b / 3
```

Three consequences drive everything that follows.

**(1) `cx1` and `cx2` are constants.** They are not derived from the data, not
clamped, and not tunable. Every automatically generated curve in Marrow has
`cx1 = 1/3` and `cx2 = 2/3`.

**(2) `X(t) = t` exactly.** Substituting `cx1 = 1/3`, `cx2 = 2/3`:

```text
X(t) = (1-t)^2 t + 2 (1-t) t^2 + t^3
     = t - 2t^2 + t^3 + 2t^2 - 2t^3 + t^3
     = t
```

so `X'(t) = 1 > 0` on all of `[0, 1]`, the inverse `X^-1` is the identity, and
`transform(alpha) = Y(alpha)` with **no solve at all**. The Newton iteration
converges in one step and the bisection fallback is never needed. This is the
best-conditioned curve the format can express, and it is the whole reason the
Hermite form is the right target: MAR-169 had to *prove* `X` non-decreasing for
arbitrary user-dragged `cx`; MAR-171 gets `X = identity` for free. (After
`float32` narrowing `X(t)` differs from `t` by at most about `3e-8`; `cx1` and
`cx2` remain strictly inside `(0, 1)` with `cx2 > cx1`, so `X` stays strictly
increasing and the solver stays well posed.)

**(3) The whole computation is scale-free.** `a` and `b` are ratios of slopes,
so multiplying every time by any positive constant, or every value by any
non-zero constant, leaves the stored control points bit-identical. Retiming a
whole track uniformly, or changing the unit of a translate channel, cannot
change an auto curve. Only the *shape* of the driver series matters. §18.1 pins
this with a test.

### 6.3 Fritsch–Carlson tangents, exactly

Given a track's key times `t[0] < t[1] < ... < t[n-1]` and the driver series
`v[0] ... v[n-1]`, in four passes. Passes are ordered and each is stated in full
because the order is load-bearing.

**Pass A — secants.** For `i` in `[0, n-2)`:

```text
h[i] = t[i+1] - t[i]
d[i] = (v[i+1] - v[i]) / h[i]
```

**Pass B — three-point tangents.**

```text
m[0]     = d[0]
m[n-1]   = d[n-2]
m[i]     = (d[i-1] + d[i]) / 2        for 0 < i < n-1
```

The arithmetic mean of the two adjacent secants is the standard three-point
formula. The Fritsch–Butland harmonic-mean variant is deliberately **not** used:
it is only defined for same-signed secants, it needs its own zero handling, and
the clamp in pass D already delivers monotonicity from the simpler mean.

**Pass C — flat zeroing.** For every `i` in `[0, n-2)` with `d[i] == 0`:

```text
m[i] = 0
m[i+1] = 0
```

This runs over **all** segments before pass D. It is what makes a plateau
actually flat instead of letting the interpolant bulge through it, and it is the
Fritsch–Carlson rule for a zero secant.

**Pass D — the monotonicity clamp**, in **ascending `i`**, mutating `m` in
place. For every `i` in `[0, n-2)` with `d[i] != 0`:

```text
a = m[i]   / d[i]
b = m[i+1] / d[i]
if (a < 0) { m[i]   = 0; a = 0; }
if (b < 0) { m[i+1] = 0; b = 0; }
norm = hypot(a, b)
if (norm > 3) {
    s = 3 / norm
    m[i]   = s * a * d[i]
    m[i+1] = s * b * d[i]
}
```

`std::hypot` rather than `sqrt(a*a + b*b)` so that a legitimately enormous ratio
(reachable when one secant is denormal-small and its neighbour is huge) scales
correctly instead of overflowing to infinity and collapsing both tangents to
zero.

**Pass E — control points**, from the **final** `m` array, for every segment `i`
in `[0, n-2)`:

```text
if (d[i] == 0)      cy1 = 1/3,  cy2 = 2/3        // see 6.4.2
else                cy1 = (m[i] / d[i]) / 3
                    cy2 = 1 - (m[i+1] / d[i]) / 3
cx1 = 1/3
cx2 = 2/3
```

**Why the ascending in-place clamp is correct and deterministic.** Segment `i`
is the last pass-D iteration that touches `m[i]`: iteration `i-1` reads
`(m[i-1], m[i])` and iteration `i` reads `(m[i], m[i+1])`, so after iteration `i`
completes, `m[i]` is final. `m[i+1]` may still be shrunk by iteration `i+1`.
Every write in pass D moves a tangent strictly toward zero. The Fritsch–Carlson
admissible region restricted to the first quadrant — `a >= 0`, `b >= 0`,
`a² + b² <= 9` — is **downward closed**: if `0 <= a' <= a` and `0 <= b' <= b`
and `a² + b² <= 9` then `a'² + b'² <= 9`. So segment `i`'s guarantee survives
every later shrink, pass E reads a single consistent final array, and the result
depends on nothing but the input. C¹ continuity across each key is preserved,
because both sides of key `i` read the same `m[i]`.

**Why `a² + b² <= 9` is the right condition.** It is the standard sufficient
condition (Fritsch & Carlson, *SIAM J. Numer. Anal.* 17(2), 1980) for a cubic
Hermite with same-signed tangents to be monotone on its segment. It is a disk
strictly inside the exact admissible region, which makes it conservative rather
than sharp — the price is a slightly flatter curve at extreme tangent ratios and
the benefit is a one-line, branch-free test with no case analysis.

**The bound that matters for the format.** `a >= 0`, `b >= 0`, and
`a² + b² <= 9` together give `0 <= a <= 3` and `0 <= b <= 3`, hence

```text
cy1 = a/3       in [0, 1]
cy2 = 1 - b/3   in [0, 1]
```

So an automatic curve **never overshoots**: `Y` is non-decreasing and stays
inside `[0, 1]`, so the evaluated value never leaves `[v[i], v[i+1]]`. This is
exactly criterion 2's "without ... unintended overshoot", and it is the sharp
behavioural line between MAR-171 and MAR-169: overshoot in Marrow is reachable
**only** by a manual handle drag.

### 6.4 Degenerate and boundary cases

#### 6.4.1 The first and last key

`m[0] = d[0]` and `m[n-1] = d[n-2]` are one-sided. No wraparound and no
loop-aware tangent: the runtime holds the last key's value after the last key,
and MAR-171 does not know about looping. MAR-172 is the story that introduces a
boundary key, and it will get the smooth join for free because that key will
have a real successor.

**The last key is never written.** It has no outgoing segment, the runtime never
evaluates its `interpolation`, and inventing a value there would put bytes in
every diff and every export for no observable effect. Its stored easing stays
byte-identical whatever its mode is. Its `curve_mode` may still be `auto` — that
intent becomes live the moment a key is appended after it. The graph reports
`Auto (no outgoing segment)` for such a key.

#### 6.4.2 A flat segment (`d[i] == 0`)

`dv = 0`, so the normalization `Y = (v - v[i]) / dv` is genuinely undefined —
`0/0`, not merely awkward. The driver contributes no information about the
segment's shape.

The runtime evaluates `v[i] + 0 * transform(alpha) = v[i]` for the driver
whatever the easing is, so the choice is unobservable **on the driver**. It is
very observable on every *other* component, because the curve is shared: the
fixture's `body` slot colour has `b = 1.0` at both `t = 0.0` and `t = 0.5` while
`r`, `g`, and `a` all vary across that same shared curve.

**Decision: `cy1 = 1/3`, `cy2 = 2/3`** — MAR-169's
`kLinearEquivalentControlPoints`, the unique evenly spaced cubic whose
`transform()` is the identity. Rationale: with no information from the driver,
the honest answer is "do not shape the other components", and a straight
normalized ramp is the only curve that shapes nothing. The alternatives were
`a = b = 0` (giving `[1/3, 0, 2/3, 1]`, a strong ease-in-out imposed on the
other components on the basis of no data) and refusing to resolve the segment
(leaving a stale manual curve inside an auto track, which is exactly the
staleness this story exists to remove). Note that the limit of the non-flat case
as the driver flattens is genuinely direction-dependent — approach through
`0, ε, 2ε` gives `a = b = 1` and approach through `0, ε, 0` gives `a = 1, b = 0`
— so no limit argument selects a value and the choice is a stated convention.

Pass C still zeroes `m[i]` and `m[i+1]`, so a plateau still flattens the ends of
its **neighbouring** segments. That is the visible, correct behaviour: a value
that arrives at a plateau decelerates into it.

The resulting stored kind is still `CubicBezier`. **Every auto-resolved key
stores `CubicBezier` with `cx1 = 1/3` and `cx2 = 2/3`** — one invariant, no
family of exceptions, trivially assertable.

#### 6.4.3 A zero-duration segment (`h[i] <= 1e-6`)

`d[i]` is `dv/0`. There is no fallback that means anything: `alpha` has no
extent, and the runtime jumps straight to the later key.

**Decision: reject the whole track atomically.** `automatic_curve_control_points()`
returns `std::nullopt`, `resolve_automatic_curves()` returns an error naming the
animation, the track, and the segment index, and the enclosing transaction is
cancelled. Nothing is written anywhere.

Three reasons this is the right severity. The `.marrow` loader already rejects
non-increasing keyframe times
(`"timeline edit keyframe times must be strictly increasing"`,
`project.cpp` `parse_transform_keyframes`), and `retime_keyframes()` and
`insertable_key_time()` both enforce `kNonEventKeySpacing` (1 ms), so the case is
unreachable from any authored project and only arrives via a materialized
runtime-only track carrying duplicate times. Partial resolution would leave one
segment of an auto track carrying a stale manual curve with no way for the user
to see which. And atomic rejection is what every other authoring primitive does.

The threshold is `timeline_model::kKeyTimeEpsilon` (`1e-6` s), the same constant
`make_segment_frame()` uses for the same reason.

A legal-but-short 1 ms segment is **not** degenerate: `a` and `b` are ratios, so
a small `h` is perfectly conditioned (§6.2 consequence 3).

#### 6.4.4 A single-key track (`n == 1`) and an empty track

No segments exist. Nothing is resolved, no error is reported,
`resolved_key_count` is 0, and the key's `curve_mode` is preserved.

#### 6.4.5 A two-key track (`n == 2`)

`m[0] = m[1] = d[0]`, so `a = b = 1`, so `cy1 = 1/3` and `cy2 = 2/3` — exactly
linear. Two points determine a line and a monotone interpolant through two
points with no further information *is* the straight line. Asserting this is the
cheapest possible sanity check on the whole implementation, and §18.1 does.

#### 6.4.6 Non-finite driver values

An imported track can hold a non-finite value. If any `t[i]`, `v[i]`, `h[i]`,
`d[i]`, `a`, `b`, `cy1`, or `cy2` is not finite, the whole track is rejected
atomically, exactly as §6.4.3. The check runs on every intermediate, not only on
the output, because `inf - inf` produces `NaN` and `NaN` passes a naive range
test — the same trap MAR-169 §9.2 documents for the range check.

### 6.5 The format invariant, proved and then enforced

**Claim.** Every `[cx1, cy1, cx2, cy2]` this algorithm produces satisfies
`cx1, cx2 ∈ [0, 1]` and `cy1, cy2 ∈ [0, 1]`, as `double` and after narrowing to
`runtime::AnimationScalar`.

**Proof.** `cx1 = 1/3` and `cx2 = 2/3` identically (§6.2), and
`static_cast<float>(1.0/3.0) == 0.3333333432674408` and
`static_cast<float>(2.0/3.0) == 0.6666666865348816`, both strictly inside
`(0, 1)`. For `cy`: pass D guarantees `a, b ∈ [0, 3]` (§6.3) on every segment
that reaches pass E with `d != 0`, so `cy1 = a/3 ∈ [0, 1]` and
`cy2 = 1 - b/3 ∈ [0, 1]`; the flat branch produces `1/3` and `2/3` directly.
Rounding a `double` in `[0, 1]` to the nearest `float` yields a `float` in
`[0, 1]`, because `0.0` and `1.0` are exactly representable and round-to-nearest
is monotone. ∎

**Enforcement.** The proof is exact in `double`, but `a` and `b` are themselves
computed in floating point and a value sitting exactly on the `a² + b² = 9`
boundary could in principle land one ULP outside after the scale. Rather than
argue about ULPs, the implementation:

- `static_assert`s that the two `cx` constants narrow into `[0, 1]`;
- clamps each `cy` into `[0, 1]` after pass E — a no-op in exact arithmetic and
  a hard guard against a rounding escape;
- rejects the track atomically if any narrowed value is not finite or leaves
  `[0, 1]`, before any `runtime::Interpolation` is constructed, so a rejected
  value never enters the process-wide LUT cache.

**Consequence.** MAR-171 cannot write a `.marrow` or `.mskl` document that
either loader rejects. The write gate and the read gate are the same predicate,
and here the write gate is strictly stronger.

### 6.6 A worked example on the checked-in fixture

`assets/fixtures/player_idle.marrow`, animation `idle`, bone `spine`, channel
`rotate`, driver `angle`. Stored keys:

| i | t[i] | v[i] (angle) |
| ---: | ---: | ---: |
| 0 | 0.00 | 0 |
| 1 | 0.50 | 8 |
| 2 | 1.00 | -2 |

Pass A: `h[0] = h[1] = 0.5`; `d[0] = 8/0.5 = 16`; `d[1] = -10/0.5 = -20`.

Pass B: `m[0] = 16`; `m[1] = (16 + (-20))/2 = -2`; `m[2] = -20`.

Pass C: no `d` is zero; nothing changes.

Pass D, `i = 0` (`d = 16`): `a = 16/16 = 1`; `b = -2/16 = -0.125 < 0`, so
`m[1] = 0` and `b = 0`. `hypot(1, 0) = 1 <= 3`; no scale.
Pass D, `i = 1` (`d = -20`): `a = m[1]/d = 0`; `b = m[2]/d = -20/-20 = 1`.
`hypot(0, 1) = 1 <= 3`; no scale.

Pass E:

| segment | `a` | `b` | stored `[cx1, cy1, cx2, cy2]` |
| --- | ---: | ---: | --- |
| 0 (`t 0.0 -> 0.5`) | 1 | 0 | `[1/3, 1/3, 2/3, 1]` |
| 1 (`t 0.5 -> 1.0`) | 0 | 1 | `[1/3, 0, 2/3, 2/3]` |
| key 2 | — | — | untouched (last key) |

Read it back: key 1 is a local maximum, so Fritsch–Carlson zeroes its tangent.
Segment 0 has `cy2 = 1`, i.e. `P2y = P3y`, i.e. `Y'(1) = 0` — the value arrives
at the peak flat. Segment 1 has `cy1 = 0`, i.e. `P1y = P0y`, i.e. `Y'(0) = 0` —
it leaves the peak flat. Neither overshoots past `8`. That is exactly what an
animator means by "smooth through the neighbours".

The same track's key 0 currently stores
`[0.330000013113022, 0, 0.670000016689301, 1]` in the fixture, so switching it
to auto changes visible bytes in both the project and the export — see §18.5.

Second example, bone `arm_l`, channel `rotate`, driver `angle`, keys
`(0.25, 90)` and `(0.5, 45)`: `n == 2`, so `a = b = 1` and the stored curve is
`[1/3, 1/3, 2/3, 2/3]`, exactly linear (§6.4.5). The fixture stores
`"curve": "linear"` there today, so switching it to auto converts a string to a
4-number array — a visible, byte-level change in the exported `.mskl` that a
reviewer can grep for.

## 7. The Driver

### 7.1 Why a driver is needed at all

A parent key owns **one** easing shared by every component of that key
(MAR-167/168/169, structurally unrepresentable otherwise). A Translate key has
two value series, `x` and `y`; a Slot Color key has four. Fritsch–Carlson needs
**one** series. Something must choose.

Alternatives considered and rejected:

- **A fixed component per family.** Simple, but wrong whenever the interesting
  motion is in the other component, which is most of the time for translate.
- **A vector norm.** `sqrt(dx² + dy²)` is monotone in neither component and
  produces a curve whose monotonicity claim is about a quantity the animator
  never sees. It also inverts on a component that moves the other way.
- **Per-component curves.** Structurally impossible and explicitly forbidden by
  every story in this chain.

So the driver is authored, and it is the second half of the metadata the story's
criterion 1 names.

### 7.2 Vocabulary, family compatibility, and defaults

The driver is a `TimelineScalarComponent` — the MAR-168 enum, reused verbatim,
relocated to `project.hpp` (§3.4). Its seven values, their `.marrow` tokens, and
the families that accept them:

| Enumerator | Token | Rotate | Translate / Scale / Shear | Slot Color |
| --- | --- | :-: | :-: | :-: |
| `Angle` | `angle` | yes | no | no |
| `X` | `x` | no | yes | no |
| `Y` | `y` | no | yes | no |
| `Red` | `r` | no | no | yes |
| `Green` | `g` | no | no | yes |
| `Blue` | `b` | no | no | yes |
| `Alpha` | `a` | no | no | yes |

Compatibility is decided by the same switch `read_scalar_component()` already
uses for `offset_keyframe_scalars()`, so the two operations can never disagree
about what a family owns.

**The family default**, used when `curve_mode` is authored without a driver, is
the family's **lowest-indexed** component: `Angle` for Rotate, `X` for
Translate / Scale / Shear, `Red` for Slot Color. This matches MAR-169 §7.5's
existing rule for choosing a component when `graph_view.active_component` is
unset, so the graph's fallback and the data model's fallback are the same
sentence.

The editor and the Agent always write an explicit driver; the default exists so
a hand-written `.marrow` containing only `"curve_mode": "auto"` loads. On the
next save the effective driver becomes explicit — the same normalization the
`snap` block already performs on its seven fields.

### 7.3 Why Deform has no auto mode

`DeformKeyframeEdit` carries `std::vector<double> vertex_offsets` — hundreds of
numbers with no canonical scalar. The only candidate drivers are a norm (§7.1,
rejected) or a nominated vertex index, which would mean a driver vocabulary that
is unbounded, needs its own validation against the attachment's vertex count,
and goes stale when the mesh is re-topologized.

**Decision: Deform keeps manual mode only.** `DeformKeyframeEdit` gains no
field, so the exclusion is compile-enforced rather than branch-enforced, exactly
as MAR-170 §9.1 relies on the missing `interpolation` member of the discrete
families. `set_keyframe_curve_mode()` rejects a Deform selector with a message
naming the reason. Deform remains fully supported by
`set_keyframe_interpolation()` and by `timeline.set_interpolation`, unchanged.

### 7.4 Mixed drivers on one track, and why the curve is still shared

The driver is per key, so two adjacent keys of one track can name different
drivers. The resolution rule makes that well defined:

> To resolve segment `i`, read the driver of key `i`, build the **whole track's**
> series for that component, run §6.3 over it, and take segment `i`'s control
> points.

Implementation: group the track's auto keys by driver, run the four passes once
per distinct driver present (at most four), and assign each segment from its own
key's run. `O(4n)` with `n` the track's key count.

Properties this rule has:

- **It reduces to the obvious thing.** When every auto key on a track names the
  same driver, which is the only configuration the UI produces, it is exactly
  "run Fritsch–Carlson once over the track".
- **Each segment is individually monotone** with respect to its own driver, and
  each segment's `cy` values still come from one consistent series.
- **A driver change is local.** Changing key `i`'s driver changes segment `i`
  and nothing else, because every other segment's control points come from its
  own key's driver run. That is what makes criterion 3's "driver ... changes
  recompute affected shared curves" a crisp, testable statement.
- **C¹ continuity holds within runs of equal driver** and is not claimed across
  a driver change, where it is not meaningful anyway.

**The driver never affects which bytes are written.** It selects a series to
*read*. The write goes to the one shared `interpolation` field through the same
component-free path MAR-169 §8 established. Choosing driver `y` and choosing
driver `x` produce two different single shared curves, never two curves.

## 8. Project-Local Storage

### 8.1 Where and what shape

Two optional members on the `.marrow` keyframe object, beside the `curve` field
they qualify. Transform keyframes and Slot Color keyframes only.

```json
"timeline_edits": {
  "animations": {
    "idle": {
      "bones": {
        "spine": {
          "rotate": [
            { "time": 0,   "angle": 0,
              "curve": [0.3333333, 0.3333333, 0.6666667, 1],
              "curve_mode": "auto", "curve_driver": "angle" },
            { "time": 0.5, "angle": 8,
              "curve": [0.3333333, 0, 0.6666667, 0.6666667],
              "curve_mode": "auto", "curve_driver": "angle" },
            { "time": 1,   "angle": -2, "curve": "linear" }
          ]
        }
      }
    }
  }
}
```

- `curve_mode`: optional string, exactly `"manual"` or `"auto"`. Absent means
  `"manual"`.
- `curve_driver`: optional string, exactly one of
  `"angle" | "x" | "y" | "r" | "g" | "b" | "a"`. Absent with
  `curve_mode == "auto"` means the family default (§7.2).

In-memory:

```cpp
enum class TimelineCurveMode : std::uint8_t { Manual, Auto };

struct TransformKeyframeEdit {
    double time{0.0};
    double angle{0.0};
    double x{0.0};
    double y{0.0};
    runtime::Interpolation interpolation{};
    TimelineCurveMode curve_mode{TimelineCurveMode::Manual};
    TimelineScalarComponent curve_driver{TimelineScalarComponent::Angle};
};

struct SlotColorKeyframeEdit {
    double time{0.0};
    runtime::SlotColor color{};
    runtime::Interpolation interpolation{};
    TimelineCurveMode curve_mode{TimelineCurveMode::Manual};
    TimelineScalarComponent curve_driver{TimelineScalarComponent::Red};
};
```

`curve_driver` is a plain value rather than an `std::optional` because the
parser resolves the family default eagerly (it knows the channel) and the
serializer only writes the pair when the mode is `Auto`. There is exactly one
representation of "manual" in memory and exactly one on disk.

### 8.2 Why per-keyframe fields and not a top-level block

MAR-165's `.marrow.snap` is a **top-level singleton**: one settings object for
the whole project. MAR-171's state is **per key**. A top-level side table would
have to name its keys by `(animation, bone, channel, time)` — the same
`TimelineKeySelector` tuple the authoring layer uses — and that identity is
**exactly what a retime changes**. Every retime, insertion, deletion, cut, and
paste would have to rewrite the side table in step or leave orphaned and stale
entries, and MAR-172's boundary-key management would inherit the same hazard on
top of its own.

Per-key struct members ride through every one of those operations for free,
because every one of them already moves whole `TransformKeyframeEdit` and
`SlotColorKeyframeEdit` values. `paste_timeline_clipboard()` carries a whole
`ProjectData project_fragment`, so mode and driver survive copy/paste with no
new code at all. This is a correctness argument, not an ergonomic one, and it is
decisive.

What MAR-171 *does* borrow from `.marrow.snap` is its five discipline
properties, applied at the keyframe level (§8.7).

### 8.3 The default-off guarantee

| Condition | Result |
| --- | --- |
| Key object has neither field | `Manual`, family-default driver, never read |
| Key is `Manual` in memory | **Neither field is serialized** |
| Whole project has no auto key | `serialize_project()` output is byte-identical to a pre-MAR-171 build |

That last row is the guarantee criterion 1 means by "defaulting old projects to
manual behavior", and §18.2 asserts it directly by serializing every checked-in
fixture project before and after and comparing bytes.

### 8.4 Validation rules

At load (`parse_transform_keyframes`, `parse_slot_color_keyframes`), each with a
precise JSON path such as
`$.timeline_edits.animations.idle.bones.spine.rotate[0].curve_mode`:

| Condition | Message |
| --- | --- |
| `curve_mode` present and not a string | `curve_mode must be 'manual' or 'auto'` |
| `curve_mode` an unknown token | `curve_mode must be 'manual' or 'auto'` |
| `curve_driver` present and not a string | `curve_driver must be one of angle, x, y, r, g, b, a` |
| `curve_driver` an unknown token | `curve_driver must be one of angle, x, y, r, g, b, a` |
| `curve_driver` not authorable on this family | `curve_driver must name a component this timeline owns` |
| `curve_driver` present while `curve_mode` is absent or `"manual"` | `curve_driver requires curve_mode 'auto'` |

The last row is deliberately strict rather than lenient. Accepting and silently
dropping a driver on a manual key would lose authored data on the next save;
accepting and silently keeping it would create a second, invisible in-memory
state that never round-trips. Rejecting is the only option with neither failure
mode, and it matches how `parse_interpolation()` already rejects an unknown
`curve` string rather than falling back.

In `validate_project()` (`project.cpp:4797` neighbourhood, beside the existing
`snap` block):

| Condition | Message |
| --- | --- |
| Any `Auto` keyframe whose `curve_driver` is not authorable on its family | `automatic curve drivers must name a component the timeline owns` |

There is deliberately **no** validation that a key's stored `curve` agrees with
what the resolver would produce. A stale pair is legal data (§9.6); the stored
numbers are authoritative for every reader and the mode is authoring intent.
Validating agreement would make `validate_project()` depend on the whole track,
would reject hand-edited files that are perfectly loadable, and would force a
rewrite-on-load that decision 8 forbids.

### 8.5 Unknown-field preservation

The `.marrow` document preserves unknown data at exactly two levels today, and
MAR-171 changes neither:

- the whole root, through `ProjectData::preserved_root`
  (`project.hpp:498`, `project.cpp:4109`, `project.cpp:6538`);
- inside the `snap` object, through `ProjectSnapSettings::preserved_source`
  (`project.cpp:320`, `project.cpp:4194`).

**Keyframe objects have never preserved unknown members.**
`parse_transform_keyframes()` builds a fresh `TransformKeyframeEdit` and
`build_transform_keyframes_value()` builds a fresh `Value::Object`, so any
member the current build does not know is dropped on save. That is pre-existing
behaviour, unchanged by MAR-171, which merely moves two names from "unknown and
dropped" to "known and round-tripped".

This is stated explicitly rather than glossed, because it is the one place where
MAR-171 genuinely cannot reproduce the `.marrow.snap` pattern. Adding a
`preserved_source` to every keyframe would multiply project memory by the size
of a JSON object per key and would change the serialization of every existing
project, which is a far larger and riskier change than this story's subject.
Recorded as a known limitation and a possible follow-up, not fixed here.

### 8.6 Runtime-export neutrality

The `.mskl` export builder is a **separate function from the project
serializer**. `build_runtime_document()` (`project.cpp:4596`) constructs runtime
keyframe objects through `build_runtime_slot_color_keyframes_value()`
(`project.cpp:3625`) and the transform/deform equivalents, each of which emits a
fixed member list ending in
`keyframe_object.emplace("curve", build_interpolation_value(...))`.

Therefore MAR-171 needs **zero changes** to the export path, and the resolved
easing reaches the runtime through the field it already used. `.mbin` follows,
because `binary.cpp` serializes a loaded `runtime::SkeletonData`, which never had
a curve-mode concept to lose.

The negative is asserted directly, not assumed: §18.5 requires the export test
to read the produced `.mskl` **as text** and assert that neither `curve_mode`
nor `curve_driver` appears anywhere in it.

`.mskl` stays version 1 and `.mbin` stays version 2.

### 8.7 Mapping the MAR-165 `.marrow.snap` pattern onto per-key fields

| `.marrow.snap` property | MAR-165 mechanism | MAR-171 equivalent |
| --- | --- | --- |
| Optional and absent by default | `std::optional<ProjectSnapSettings>`; `root.erase("snap")` when absent | fields omitted unless `curve_mode == Auto` (§8.3) |
| Parsed with per-field fallback | `read_optional_boolean` / `read_optional_number` | `find_optional_member` + token parse + family default (§8.4) |
| Rejected with a JSON path on bad data | `validation_error(document, loc, "$.snap.x", ...)` | same helper, keyframe-scoped paths (§8.4) |
| Re-validated in `validate_project()` | the step-positivity block at `project.cpp:4797` | the driver-authorability block (§8.4) |
| Never exported, never versioned | "`snap` never enters `.mskl` or `.mbin` export" | §8.6, asserted by a text search on the export |
| Unknown members survive | `preserved_source` | not available at keyframe level; stated as a limitation (§8.5) |

## 9. Resolution

### 9.1 The resolver

```cpp
struct TimelineAutoCurveResult : AuthoringResult {
    std::size_t auto_key_count{0U};      // auto keys with an outgoing segment
    std::size_t resolved_key_count{0U};  // keys whose stored curve changed
};

TimelineAutoCurveResult resolve_automatic_curves(
    ProjectData* project,
    std::string_view animation_name = {});
```

For every `TransformTimelineEdit` and `SlotColorTimelineEdit` whose
`animation_name` matches (or every one, when the argument is empty):

1. If the track has no `Auto` key, skip it entirely — no copy, no work, and
   therefore no possibility of touching a project that opted out.
2. Otherwise run §7.4's grouped §6.3 computation over the whole track.
3. Any track-level rejection (§6.4.3, §6.4.6) aborts the whole call with an
   error and leaves `*project` untouched.
4. Write each resolved segment's control points into its start key's
   `interpolation`, **without** going through `set_keyframe_interpolation()`,
   because that function demotes to manual (§4.2). The resolver uses the same
   file-local `write_key_interpolation()` helper (`authoring.cpp:1923`).
5. `resolved_key_count` counts only keys whose stored easing actually changed,
   compared with `same_interpolation()` (`authoring.cpp:1884`), so a no-op
   resolve reports zero and callers can assert it.

`resolve_automatic_curves()` follows the same preflight-then-mutate shape as
every other primitive (§10).

### 9.2 The complete trigger list

Recomputation happens inside the caller's already-open `EditTransaction`, after
the primary mutation and before `refresh_runtime()`. Whole-animation scope
(decision 7).

**Shell (`timeline_controller.cpp`), through one helper**
`resolve_timeline_auto_curves(ShellState*, animation, error_out)`:

| Call site | Why | Expected effect |
| --- | --- | --- |
| `apply_timeline_retime_delta()` | key times move; `h` and `d` change | resolves |
| `apply_timeline_graph_value_delta()` | driver values move | resolves |
| `add_timeline_key_at_playhead()` | a new neighbour appears | resolves |
| `remove_selected_timeline_keys()` | a neighbour disappears | resolves |
| `cut_selected_timeline_keys()` | delegates to remove | resolves |
| `paste_timeline_clipboard()` | new neighbours appear; pasted keys keep their copied mode and driver | resolves |
| `apply_timeline_graph_handle_control_points()` | MAR-169 drag; the dragged key is demoted by the primitive | resolves nothing (asserted) |
| `apply_timeline_curve_preset()` (MAR-170) | same | resolves nothing (asserted) |
| `apply_timeline_curve_mode()` (new) | intent changed | resolves |
| the animation-duration gesture | criterion 3 names duration | resolves nothing today; see below |

**Agent (`agent_handlers_editing.cpp`)**, with
`resolve_automatic_curves(project, {})` — whole project, since a scripted call
is not in a per-frame loop:

| Operation | Why |
| --- | --- |
| `timeline.retime_keyframes` | key times move |
| `timeline.set_interpolation` | demotion path; resolves nothing (asserted) |
| `timeline.set_curve_mode` | intent changed |
| `set_transform` | writes a Transform key's time and values |
| `set_slot_color_keyframe` | writes a Slot Color key's time and values |
| `remove_transform_keyframe` | a neighbour disappears |
| `animation.set_duration` | criterion 3 names duration; see below |

**Deliberately not wired**, with reasons that must survive review:

- `set_deform_keyframe` and every other Deform path — Deform keys have no
  `curve_mode` member (§7.3) and are never a driver, so no auto curve can
  depend on them.
- `set_event_keyframe` / `remove_event_keyframe` / draw-order / slot-attachment
  paths — those families carry no easing and no scalar series.
- Load, save, and export (§9.6).

**On duration.** In MAR-171 alone, an explicit duration change moves no key time
and no key value, so it cannot change any automatic curve;
`set_animation_duration()` already refuses a duration shorter than the inferred
one. The trigger is nevertheless wired, because criterion 3 names it, because it
costs one call, and because it is precisely the seam MAR-172 needs when a
managed boundary key starts living at `duration`. §18.3 asserts the honest
current behaviour: with auto keys present, a duration change reports
`resolved_key_count == 0` and leaves `serialize_project()` byte-identical apart
from the duration itself.

### 9.3 One transaction, one history entry

Every trigger in §9.2 already owns exactly one `EditTransaction`. The resolver
call is an additional mutation of the same `transaction.project()` before the
same `refresh_runtime()` and the same `commit()`, so:

- a retime that moves three keys and re-resolves eleven curves is **one** undo
  entry;
- `Ctrl+Z` after it restores both the times and the curves, because
  `EditTransaction::cancel()` / the history entry captures the whole project;
- a resolver failure cancels the enclosing transaction, so the primary mutation
  is rolled back too — a partially-resolved project is unrepresentable.

`apply_timeline_curve_mode()` opens its own transaction with
`EditKind::EditProperty`, label `"Set automatic curve"` /
`"Set manual curve"` (plural variants for multi-key), group
`"timeline:curve-mode"`, `allow_merge = false`, impact
`Project | Runtime | Preview`. The group is distinct from MAR-169's
`"timeline:graph-easing"` and MAR-170's `"timeline:curve-preset"`, so nothing
merges across the three.

MAR-171 adds **no gesture**: there is no drag, no dead zone, no entry in
`authoring_gesture_active()`, and no entry in `cancel_authoring_gestures()`. The
two lists that "must stay in step" stay exactly as MAR-169 and MAR-170 leave
them.

### 9.4 Interaction with MAR-169's manual handle drag

Criterion 4: "Dragging either handle switches the segment to manual in that
gesture."

- **Handles are drawn for auto keys**, at the resolved control points, in the
  auto colour (§12). They must be, or the criterion has nothing to switch.
- **Grabbing demotes.** `apply_timeline_graph_handle_control_points()` calls
  `set_keyframe_interpolation()`, which now sets `Manual` (§4.2). No extra code
  is needed at the drag site and no other caller can bypass it.
- **One history entry, not two.** The demotion happens inside the drag's
  existing transaction and commits with it.
- **The `changed` rule is extended.** MAR-169's in-flight net-state rule cancels
  a drag whose final control points equal the press-time points. For an auto
  key that is wrong: the user did author something — the key no longer tracks
  its neighbours. So:

  ```text
  gesture.changed = (applied_control_points != original_control_points)
                 || (original_mode == TimelineCurveMode::Auto)
  ```

  `TimelineGraphHandleGesture` gains `original_mode`, captured at
  `begin_timeline_graph_handle_gesture()`. §18.4 asserts the case directly: drag
  an auto handle away and exactly back, and get one history entry, `Manual`
  mode, and byte-identical control points.
- **Escape still restores auto.** `EditTransaction::cancel()` restores the whole
  project, including `curve_mode`.
- **The Linear/Stepped seed never applies to an auto key**, because an auto
  key's stored kind is always `CubicBezier` (§6.4.2). MAR-169's
  `seed_control_points()` is untouched.

The numeric `Bezier X1/Y1/X2/Y2` inspector in `shell_timeline.cpp` is **not**
refactored onto the shared primitive (§3.3), but it *does* write
`interpolation` directly. Task 0 must confirm whether it writes through
`set_keyframe_interpolation()`; if it does not, MAR-171 adds a one-line
demotion there and nothing else. Leaving that one path able to write an absolute
curve without demoting would produce a silently-overwritten edit, which is the
exact defect this story exists to prevent.

### 9.5 Interaction with MAR-170's presets and defaults

- **Applying a preset to an auto key demotes it**, in the preset's single
  transaction, through the same primitive rule (§4.2). Correct: a preset is an
  absolute authored curve, and leaving the key auto would have the next
  neighbour edit silently discard it.
- **`curve_preset_of()` is unchanged and still applies.** An auto key's resolved
  curve is compared bit-exactly against the six preset constants like any other,
  and virtually always reads `Custom`. The readout composes as
  `Outgoing: Custom Bezier · Auto (Angle)` (§12).
- **Newly authored keys are manual.** MAR-170 seeds them with
  `curve_preset_interpolation(state.preferences.default_curve)`; MAR-171 leaves
  that untouched and does not add a "default curve mode" preference.
  `editor-settings.json` v1 changes by zero bytes (§3.3, §3.4).
- **Two clicks, two history entries.** Preset then mode, or mode then preset,
  each commits its own transaction with `allow_merge = false` and distinct
  groups.

### 9.6 What deliberately never resolves

| Moment | Behaviour | Why |
| --- | --- | --- |
| `load_project()` | no resolve | Load must not dirty the project, must not create history, and must leave load→save byte-stable. |
| `save_project()` / `serialize_project()` | no resolve | Serialization is a pure projection of `ProjectData`. Resolving here would make saving a mutation. |
| `export_runtime_assets()` | no resolve | Export must be a pure function of the project; a resolve here would make the exported file disagree with the saved one. |
| a background timer or frame tick | no resolve | Resolution is a transactional edit and belongs to a user action. |

**The consequence, stated plainly:** a hand-edited `.marrow` can hold
`curve_mode: "auto"` next to a `curve` that the resolver would not produce. That
document loads, validates, evaluates, and exports using the **stored** curve; the
mode is authoring intent that becomes effective on the next auto-affecting
transaction. The explicit way to force reconciliation is
`timeline.set_curve_mode` with `mode: "auto"` on those keys, which resolves them
and reports `resolved_key_count`. §18.3 covers this exact scenario.

### 9.7 Selection stability

The resolver writes only `interpolation`, and `set_keyframe_curve_mode()` writes
only `interpolation`, `curve_mode`, and `curve_driver`. No key time changes on
any MAR-171 path, so every `TimelineKeyRef` is bit-identical across apply,
commit, cancel, undo, and redo; `selected_keys` and `active_key` are never
rewritten and `reconcile_timeline_key_selection()` has nothing to prune. This is
the same argument MAR-169 §10.4 and MAR-170 §10.5 make.

When the resolver runs inside a **retime**, key times do change — but that is the
retime's own effect, already covered by MAR-168's contract, and the resolver
adds nothing to it.

## 10. Preflight-then-Mutate and Atomic Rollback

Both new primitives follow the shape of `set_keyframe_interpolation()`
(`authoring.cpp:1984`), which is what makes rollback exact.

`set_keyframe_curve_mode()`:

```text
 1. validate arguments (null project, empty selectors, driver token)
 2. ProjectData candidate = *project                    // full copy, nothing shared
 3. resolve every selector against `candidate`; reject duplicates by
    (kind, timeline_index, key_index)
 4. reject every selector whose family has no curve mode (Deform, Draw Order,
    Event, Slot Attachment)
 5. resolve the effective driver per selector; reject one not authorable on
    that selector's family
 6. write mode + driver into `candidate`, counting keys whose mode or driver
    actually changed
 7. resolve_automatic_curves(&candidate, <every animation named by a selector>)
 8. on any error in 3..7: return, leaving *project untouched
 9. if nothing in 6 or 7 changed: return a no-change result
10. *project = std::move(candidate)                     // one move
```

`resolve_automatic_curves()`:

```text
1. validate arguments (null project)
2. ProjectData candidate = *project
3. for each in-scope track holding at least one Auto key:
     a. build the driver series; reject non-finite times/values
     b. reject any segment shorter than kKeyTimeEpsilon
     c. compute control points; clamp cy into [0, 1]; reject non-finite or
        out-of-range narrowed values
     d. write into `candidate`, counting real changes
4. on any error: return, leaving *project untouched
5. *project = std::move(candidate)
```

No step between the copy and the final move can leave a partial write in
`*project`. When the resolver is called from inside another primitive's
transaction, there are two independent layers of atomicity: the primitive's own
candidate copy, and the enclosing `EditTransaction::cancel()`.

`EditTransaction::cancel()` restores the project, the runtime `SkeletonData`,
the preview controller state, and the playback state captured at
`begin_edit()`, then bumps the runtime/preview revision;
`sync_shell_from_editor_session(state)` refreshes the shell mirrors. The graph
and the dopesheet are byte-identical to the transaction start because both read
through revision-keyed caches and neither holds a runtime pointer or key index
across frames.

## 11. UI-Free API

### 11.1 `include/marrow/editor/project.hpp`

```cpp
/** @brief Which editable scalar channel of a timeline key a value names. */
enum class TimelineScalarComponent : std::uint8_t {   // MOVED from authoring.hpp
    Angle, X, Y, Red, Green, Blue, Alpha,
};

/**
 * @brief Authored intent for one key's outgoing easing.
 *
 * `Manual` is the pre-MAR-171 behaviour and the default for every keyframe and
 * every project that omits the field: the stored `interpolation` is exactly
 * what the animator put there. `Auto` records that the stored easing is a
 * derived value the editor recomputes from the neighbouring keys of the
 * driver's series. The stored easing remains authoritative for every reader,
 * including both file formats and the runtime; the mode records only why the
 * numbers are what they are, and is never exported.
 */
enum class TimelineCurveMode : std::uint8_t { Manual, Auto };
```

plus the two members on `TransformKeyframeEdit` and `SlotColorKeyframeEdit`
quoted in §8.1.

### 11.2 `src/editor/curve_auto.hpp` (new, editor-private, pure)

```cpp
namespace marrow::editor::curve_auto {

/** @brief One (time, driver value) sample of a track, in ascending time. */
struct Sample {
    double time_seconds{0.0};
    double value{0.0};
};

/** Every automatic curve has these two x control points; see the design §6.2. */
inline constexpr double kAutoControlPointX1 = 1.0 / 3.0;
inline constexpr double kAutoControlPointX2 = 2.0 / 3.0;
/** Shortest segment whose normalized time axis still has usable extent. */
inline constexpr double kMinimumSegmentSeconds = 1e-6;

/**
 * @brief Monotone Fritsch-Carlson control points, one per outgoing segment.
 *
 * Returns `samples.size() - 1` entries, or an empty vector for fewer than two
 * samples. Returns `std::nullopt` when any time or value is non-finite, when
 * times are not strictly increasing, or when any segment is shorter than
 * `kMinimumSegmentSeconds`, because a zero-extent normalized time axis has no
 * honest fallback. Every returned entry has `cx1 == kAutoControlPointX1`,
 * `cx2 == kAutoControlPointX2`, and `cy1`, `cy2` inside `[0, 1]`, so the result
 * satisfies the `.marrow`/`.mskl` `cx in [0, 1]` invariant unconditionally and
 * can never overshoot its segment endpoints.
 */
std::optional<std::vector<std::array<double, 4>>> segment_control_points(
    const std::vector<Sample>& samples);

}  // namespace marrow::editor::curve_auto
```

### 11.3 `include/marrow/editor/authoring.hpp` (additive)

Appended after MAR-170's preset accessors.

```cpp
struct TimelineCurveModeResult : AuthoringResult {
    std::size_t key_count{0U};
    std::size_t changed_key_count{0U};    // mode or driver differed
    std::size_t resolved_key_count{0U};   // stored easing rewritten
};

/**
 * @brief Atomically records manual/automatic curve intent on persisted keys.
 *
 * Only Transform and Slot Color keys carry curve intent: a deform key's value
 * is a vertex-offset vector with no canonical scalar to drive a tangent, and
 * the discrete families carry no easing at all. `driver` must name a component
 * the selected key's family owns; `std::nullopt` selects that family's
 * lowest-indexed component. Setting `Auto` immediately resolves every affected
 * automatic curve of every animation the selectors name, so the stored easing
 * and the recorded intent never disagree after a successful call. A rejected
 * edit leaves the project unchanged.
 */
TimelineCurveModeResult set_keyframe_curve_mode(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    TimelineCurveMode mode,
    std::optional<TimelineScalarComponent> driver = std::nullopt);

struct TimelineAutoCurveResult : AuthoringResult {
    std::size_t auto_key_count{0U};
    std::size_t resolved_key_count{0U};
};

/**
 * @brief Recomputes every automatic curve of one animation, or of the project.
 *
 * Callers run this inside the transaction that changed a key time, a key value,
 * a key's existence, or an explicit duration, so one edit stays one history
 * entry. Tracks with no automatic key are skipped untouched. A track whose
 * driver series is non-finite, or which contains a segment shorter than the key
 * time epsilon, rejects the whole call atomically rather than resolving part of
 * it. This never demotes a key and never writes a manual key.
 */
TimelineAutoCurveResult resolve_automatic_curves(
    ProjectData* project,
    std::string_view animation_name = {});

/** @brief The `.marrow` token for one curve mode, and its inverse. */
std::string_view curve_mode_token(TimelineCurveMode mode);
std::optional<TimelineCurveMode> curve_mode_from_token(std::string_view token);
/** @brief The `.marrow` token for one driver component, and its inverse. */
std::string_view curve_driver_token(TimelineScalarComponent driver);
std::optional<TimelineScalarComponent> curve_driver_from_token(std::string_view token);
/** @brief The lowest-indexed component a family owns, used as its default driver. */
TimelineScalarComponent default_curve_driver(TimelineKeyKind kind,
                                             TransformTimelineChannel channel);
/** @brief Reports whether `driver` is authorable on that family. */
bool curve_driver_is_authorable(TimelineKeyKind kind,
                                TransformTimelineChannel channel,
                                TimelineScalarComponent driver);
```

The token helpers are public because `project.cpp` (parse/serialize), the Agent
handler, and the shell all need them, and one definition beats three.

### 11.4 `src/editor/timeline_controller.hpp` (shell, ImGui-free)

```cpp
/** @brief Result of one curve-mode application, for status text and tests. */
struct TimelineCurveModeApplyResult {
    bool applied{false};
    std::size_t changed_key_count{0U};
    std::size_t resolved_key_count{0U};
    std::size_t compatible_key_count{0U};
    std::size_t skipped_key_count{0U};   // selected keys with no curve mode
    std::string error;
};

/**
 * @brief Applies one curve mode to every compatible selected key.
 *
 * Deform, draw-order, event, and slot-attachment selections are skipped rather
 * than rejected, duplicates are collapsed, runtime-only tracks are materialized,
 * and the whole write plus its automatic resolution is one transaction with
 * live preview and one history entry.
 */
TimelineCurveModeApplyResult apply_timeline_curve_mode(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    marrow::editor::TimelineCurveMode mode,
    std::optional<marrow::editor::TimelineScalarComponent> driver);

/**
 * @brief Resolves the animation's automatic curves inside the caller's open
 *        transaction.
 * @return false with `*error_out` set when the resolve failed; the caller
 *         cancels.
 */
bool resolve_timeline_auto_curves(
    ShellState* state,
    std::string_view animation_name,
    std::string* error_out);

/** @brief The active key's recorded curve mode and effective driver. */
std::optional<marrow::editor::TimelineCurveMode> active_outgoing_curve_mode(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks);
std::optional<marrow::editor::TimelineScalarComponent> active_outgoing_curve_driver(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks);
```

`TimelineGraphHandleGesture` (`shell_state.hpp`) gains one member:

```cpp
    // The curve mode at the press. A drag on an auto key is an authored change
    // even when the control points land back on their starting values, because
    // the key stops tracking its neighbours.
    marrow::editor::TimelineCurveMode original_mode{
        marrow::editor::TimelineCurveMode::Manual};
```

### 11.5 What stays shell-private ImGui code

The Graph toolbar keeps only: drawing the `Curve mode:` buttons and the
`Driver:` combo, gating them on `authoring_gesture_active()` and a non-empty
compatible selection, drawing the tooltips and the extended `Outgoing:` readout,
drawing auto handles in the auto colour, and publishing render stats. No
Fritsch–Carlson arithmetic, no token parsing, and no project mutation lives in
an ImGui translation unit.

### 11.6 Render statistics additions

`TimelineGraphRenderStats` gains:

```cpp
    bool curve_mode_row_drawn{false};
    bool curve_mode_row_enabled{false};
    float first_curve_mode_min_x{0.0f};
    float first_curve_mode_min_y{0.0f};
    float first_curve_mode_max_x{0.0f};
    float first_curve_mode_max_y{0.0f};
    // True when the active key's outgoing segment is automatic, which is
    // exactly when the handles are drawn in the auto colour.
    bool active_key_auto{false};
    // The active key's effective driver, reported as its TimelineScalarComponent
    // index; kDriverCount when there is no outgoing segment.
    std::size_t active_driver_index{0U};
```

`first_curve_mode_*` lets the actual-frame smoke aim a real mouse at a real
button rather than at a hardcoded coordinate, exactly as MAR-167/168/169/170 do
for `Fit`, the component checkboxes, and the preset row.

## 12. Presentation

- **Curve-mode row**, in the Graph toolbar immediately after MAR-170's `Curve:`
  preset row and its `Default:` combo, so no existing widget moves and every
  rectangle the actual-frame smokes aim at keeps its coordinates: a
  `Curve mode:` `TextDisabled` label, then two `ImGui::SmallButton`s labelled
  `Manual` and `Auto`, then a `Driver:` label and a `##curve_driver` combo whose
  entries are exactly the components the selection's families own.
- The row is wrapped in `ImGui::BeginDisabled()`/`EndDisabled()` when the
  compatible selection is empty or any authoring gesture is live. It is
  disabled, not hidden, with a tooltip explaining
  `Select one or more Transform or Slot Color keys`.
- The `Driver:` combo is additionally disabled when the compatible selection
  spans families with disjoint component sets (a Rotate key and a Slot Color key
  together), because no single driver is authorable on both. Its tooltip then
  reads `Select keys of one timeline family to choose a driver`. Applying
  `Auto` in that state is still allowed and gives each key its own family
  default.
- **Auto handles.** MAR-169 draws handles as filled 4.0 px squares in
  `IM_COL32(0x9a, 0xd8, 0xff, 0xff)`. An auto key's handles are drawn as
  **hollow** squares in `IM_COL32(0xf0, 0xc0, 0x60, 0xff)` with the tangent
  lines in the same hue at `0x80` alpha. Hollow-plus-amber reads as "derived,
  not authored" at a glance and stays distinguishable from the gold selection
  ring by shape. They remain fully grabbable; grabbing demotes (§9.4).
- **Readout.** MAR-170 rewrites the `Outgoing:` label to report a preset name or
  `Custom Bezier`. MAR-171 appends a mode clause:
  `Outgoing: Custom Bezier · Auto (Angle)`, `Outgoing: Ease-In-Out · Manual`,
  and `Outgoing: Auto (X), no outgoing segment` for a last key whose mode is
  auto. `No outgoing segment` is preserved verbatim for an unset active key.
- **Shared-easing notice.** The MAR-167/169/170 notice keeps its wording and
  gains one sentence: an automatic curve is computed from the driver's series
  and is still the one shared easing of that key.
- **Status messages**, in the existing voice:
  `"Set 3 keys to automatic curves driven by Angle"`,
  `"Set 3 of 5 selected keys to automatic curves; 2 have no curve mode"`,
  `"Set 3 keys to manual curves"`,
  `"Automatic curves updated 7 segments"`,
  `"Selected keys already use automatic curves"`,
  `"Select one or more Transform or Slot Color keys"`,
  `"Failed to set curve mode: <error>"`,
  `"Failed to update automatic curves: <error>"`.

## 13. Agent and MCP Surface

### 13.1 The operation

| Field | Value |
| --- | --- |
| `name` | `timeline.set_curve_mode` |
| `category` | `edit` |
| `mutating` | `true` |
| `requires_review` | `false` |
| `dry_run_supported` | `true` |
| `requires_project` | `true` |
| `handler` | `&handle_editing_operation` (tail-calls `handle_timeline_editing_operation`) |
| registry position | immediately after `timeline.set_interpolation` |

`kOperationSpecs[]` row order is part of the contract — `expect_registry_contract()`
in `agent_dispatch_smoke.cpp` compares index by index — so the row goes exactly
between `timeline.set_interpolation` and `set_transform`.

**New registry total: 58 operations** (inspection 12, validation 3,
management 10, edit 33).

### 13.2 Arguments

```json
{
  "keys": [
    {"kind": "transform",  "animation": "idle", "bone": "spine", "channel": "rotate", "time": 0.0},
    {"kind": "slot_color", "animation": "idle", "slot": "body",  "time": 0.0}
  ],
  "mode": "auto",
  "driver": "angle",
  "dry_run": false
}
```

- `keys`: required array, 1 to 4096 entries, the same cap
  `timeline.retime_keyframes` and `timeline.set_interpolation` use. Only
  `transform` and `slot_color` are accepted. `deform` is rejected with a message
  naming the reason (§7.3); `draw_order`, `event`, and `slot_attachment` are
  rejected as carrying no easing at all.
- `mode`: **required** string, `"manual"` or `"auto"`. Missing is an error
  rather than a default, for the same reason MAR-169 made `interpolation`
  required: guessing a mode for a caller's whole selection is a destructive
  default.
- `driver`: optional string, one of `angle | x | y | r | g | b | a`. Omitted
  means each key's family default (§7.2). Supplying a driver with
  `"mode": "manual"` is rejected — it would record an intent the mode says is
  inactive, and the shell never produces that combination.
- `dry_run`: optional boolean.

Parsing reuses the `timeline.set_interpolation` `keys` loop verbatim minus the
`deform` branch, and adds one small helper declared in
`agent_dispatch_internal.hpp`:

```cpp
/**
 * @brief Parses a curve-mode request into a mode plus an optional driver.
 *
 * A missing `mode` is an error rather than a silent default, because guessing
 * a mode for the caller's whole selection would be destructive. A driver
 * supplied with `manual` is rejected rather than ignored.
 */
bool curve_mode_request_arg(
    const json::Value& args,
    marrow::editor::TimelineCurveMode* mode_out,
    std::optional<marrow::editor::TimelineScalarComponent>* driver_out,
    std::string* error_out);
```

`interpolation_request_arg()` and `interpolation_arg()` are left byte-identical,
so `timeline.set_interpolation`, `set_transform`, `set_deform_keyframe`, and
`set_slot_color_keyframe` keep their exact argument contracts.

### 13.3 Validation rules

In order:

1. missing `args` object -> `invalid_request`.
2. `keys` missing, not an array, or empty -> `invalid_request`.
3. more than 4096 keys -> `invalid_request`.
4. a key entry that is not an object, or missing `kind`/`animation`/`time`, or
   missing `bone`+`channel` (transform) / `slot` (slot_color) ->
   `invalid_request`.
5. a `deform`, `draw_order`, `event`, or `slot_attachment` kind, or an unknown
   kind -> `invalid_request`, naming the kind.
6. `mode` missing or an unknown token -> `invalid_request`.
7. `driver` an unknown token, or present with `"manual"` -> `invalid_request`.
8. a driver not authorable on a selected key's family -> `invalid_request`,
   enforced by the primitive.
9. a selector that does not resolve -> `not_found`, via
   `classify_timeline_key_error()`, matching `timeline.set_interpolation`.
10. the same key selected twice -> `invalid_request`.
11. a resolver rejection (non-finite driver series, zero-duration segment) ->
    `invalid_request`, naming the animation, track, and segment.
12. live path only: nothing changed -> `no_change`.

### 13.4 Response payload

```json
{
  "dry_run": true,
  "mode": "auto",
  "driver": "angle",
  "key_count": 2,
  "changed_key_count": 2,
  "resolved_key_count": 3,
  "keys_truncated": false,
  "keys": [
    {"kind": "transform", "animation": "idle", "bone": "spine", "channel": "rotate",
     "time": 0.0,
     "previous_mode": "manual", "previous_driver": null,
     "previous_interpolation": [0.33, 0.0, 0.67, 1.0],
     "interpolation": [0.3333333, 0.3333333, 0.6666667, 1.0],
     "changed": true},
    {"kind": "slot_color", "animation": "idle", "slot": "body", "time": 0.0,
     "previous_mode": "manual", "previous_driver": null,
     "previous_interpolation": "linear",
     "interpolation": [0.3333333, 0.3333333, 0.6666667, 0.6666667],
     "changed": true}
  ]
}
```

- `previous_driver` is `null` when the previous mode was `manual`, because a
  manual key has no recorded driver (§8.4).
- `previous_interpolation` and `interpolation` use exactly the `.marrow`/`.mskl`
  `curve` encoding, reusing `timeline_key_curve_value()` and
  `interpolation_curve_value()` unchanged, so a caller can compare against the
  stored bytes.
- `resolved_key_count` counts every key in the project whose stored easing the
  call rewrote — which is a **superset** of the selection, because setting one
  key to auto can change nothing else, but setting a key to manual can leave
  neighbours to re-resolve, and because a re-resolve of an already-auto key
  reports work with `changed_key_count == 0`.
- The `keys` array is capped at 256 entries with `keys_truncated` set when the
  selection is larger; the three counts always describe the whole call.

This per-key echo is the "affected-key reporting" criterion 5 requires and
doubles as the surface's **read-back channel**: a `dry_run` call reports the
current mode, driver, and curve of every selected key without mutating anything.
`timeline.describe`'s payload stays byte-identical.

### 13.5 Dry-run and live paths

Both paths first materialize each selector's track through
`ensure_transform_timeline_edit()` / `ensure_slot_color_timeline_edit()`, so a
runtime-only track is copied into the project rather than reported as missing —
the same shape `timeline.set_interpolation` uses at
`agent_handlers_editing.cpp:987-1019`.

- **Dry run**: `ProjectData candidate = *session.project();` materialize;
  snapshot; apply; snapshot; report; discard. The session is never touched, so
  `project_revision()`, `undo_count()`, and `dirty()` are unchanged.
- **Live**: `session.begin_edit({EditKind::EditProperty, "Set timeline curve
  mode via Agent" / "...modes...", "timeline:curve-mode", false,
  Project | Runtime | Preview})`, materialize, apply, `cancel()` on error or on
  `!result.changed`, then `commit_or_error()` with
  `CommitPolicy{"Failed to set timeline curve mode: "}`.

`set_keyframe_curve_mode()` resolves internally (§10 step 7), so this handler
needs no separate `resolve_automatic_curves()` call. The **other** six
operations in §9.2's agent table do, placed after their own mutation and before
their commit.

Undo/redo is inherited from `EditorSession`: one operation, one history entry,
reversible by the existing `undo` operation, which is exactly what the shell
produces for the same edit.

### 13.6 MCP tool

`tools/mcp/tools/editing.py` gains one `types.Tool` named
`timeline.set_curve_mode`, placed immediately after
`timeline.set_interpolation`, plus one helper schema:

- `_timeline_curve_mode_key_schema()` — a `oneOf` over `transform` and
  `slot_color` only. Deliberately not a reuse of
  `_timeline_interpolation_key_schema()`, which also admits `deform`.

```python
"mode": {"type": "string", "enum": ["manual", "auto"]},
"driver": {"type": "string", "enum": ["angle", "x", "y", "r", "g", "b", "a"]},
"dry_run": {"type": "boolean"},
```

with `"required": ["keys", "mode"]`. The description states that automatic
curves are recomputed whenever a neighbour moves, that they never overshoot, and
that dragging a handle switches the segment back to manual. The schema stays
advisory — the MCP server forwards every call verbatim (`server.py:75-79`), so
the C++ primitive remains the sole authority.

`get_tools()` grows by exactly one entry; no existing tool changes.

## 14. The Operation Count: Every Place 57 Appears

The registry total is derived from `std::size(kOperationSpecs)` and self-updates
when the row is added. Every other site is hand-edited. The rule from MAR-169
§14 and MAR-170 §14.5 applies unchanged: a sentence stating what the surface
**is** becomes 58; a sentence carrying a date, a milestone ID, or the words
"historical", "baseline", or "checkpoint ... passed" stays at whatever it
recorded.

Discover the full list with:

```bash
rg -n '\b57\b' --glob '!build*' src tools AGENTS.md docs .agents
rg -n '\b58\b' --glob '!build*' src tools AGENTS.md docs .agents   # before: no registry claim
```

| Location | Today | After MAR-171 | Kind |
| --- | --- | --- | --- |
| `src/editor/agent_dispatch.cpp` `kOperationSpecs[]` | 57 rows | 58 rows | code; the count self-updates |
| `src/samples/agent_dispatch_smoke.cpp:39` `std::array<OperationExpectation, 57>` | 57 | **58** + one new row after `timeline.set_interpolation` | test |
| `src/samples/agent_dispatch_smoke.cpp` behaviour cases | — | new `timeline.set_curve_mode` cases (required: `expect_complete_coverage()` fails on an uninvoked row) | test |
| `src/editor/shell_smoke_graph.cpp:143,144` | 57 | **58** | test |
| `src/editor/shell_smoke_graph.cpp:631,632` | 57 | **58** | test |
| `src/editor/shell_smoke_graph.cpp:1542,1543` | 57 | **58** | test |
| any `57` MAR-170 adds in its new scenario | 57 | **58** | test |
| `src/editor/shell_smoke_graph.cpp`, `shell_smoke_frames.cpp` end-of-scenario `!= operation_count_before` guards | — | **unchanged**; they are value-agnostic | test |
| `tools/mcp/test_client.py:46` `len(registry_names) == 57` | 57 | **58** | test |
| `tools/mcp/test_client.py:48` `len(mcp_names) == 57` | 57 | **58** | test |
| `tools/mcp/test_client.py:32-41` `new_edit_operations` | 7 names | add `timeline.set_curve_mode` | test |
| `AGENTS.md:161` `Current Validation` "Agent registry validation (57 operations, ...)" | 57 | **58**, and extend the parenthetical | current doc |
| `AGENTS.md:6` `Project State` | MAR-170 next | MAR-171 done, MAR-172 next | current doc |
| `docs/root1/editing-gap-analysis.md:23` "57개 에이전트 오퍼레이션" | 57 | **58** | current doc |
| `docs/root1/editing-gap-analysis.md:84` "57개 오퍼레이션(조회 12, 검증 3, 관리 10, 편집 32)" | 57 / 32 | **58 / 33** | current doc |
| `docs/root1/editing-gap-analysis.md:85` "57-op registry" | 57 | **58** | current doc |
| `docs/root1/editing-gap-analysis.md:191` "현재 57-op parity" | 57 | **58** | current doc |
| `docs/root1/editing-gap-analysis.md:454` "현재 57 ops" | 57 | **58** | current doc |
| `docs/root1/refector.md:20` "raises the **current** registry to the exact 57-operation total" | 57 | **58** | current doc |
| `docs/root1/refector.md:112` "for an exact **current** total of 57" | 57 | **58** | current doc |
| `AGENTS.md:276,278` MAR-169 `Validation Results` | 57 | **unchanged** | historical record |
| MAR-170's own `Validation Results`, once written | 57 | **unchanged** | historical record |
| `docs/root1/discription.md:49` MAR-169 paragraph "57번째 agent operation" | 57 | **unchanged** | historical record |
| `docs/root1/editing-gap-analysis.md:391` MAR-169 row "57번째 agent operation" | 57 | **unchanged** | historical record |
| every other dated `Validation Results`, checkpoint, and past PRD criterion | 49 / 55 / 56 | **unchanged** | historical record |

**Known false positives** the `rg` sweep surfaces that must not be touched:

- `src/editor/shell_viewport_ui.cpp:683` and `src/editor/shell_viewport.cpp:1816`
  — `IM_COL32(208, 134, 57, 230)`;
- `src/editor/shell_theme.hpp:39` — `rgb(54, 57, 64)`;
- every `.agents/tasks/prd-marrow-runtime.json` timestamp containing `:57`;
- MAR-169/170's `IM_COL32(56, 61, 69, 255)`, `(51, 56, 64)`, `"x": 56.0`,
  `56,995,840`-byte, and `PhysicsBoneState ... 56 bytes/bone` hits, which are
  already-known false positives at the previous number and are unaffected.

## 15. Error Handling and Fail-Closed Rules

| Condition | Result |
| --- | --- |
| No project, animation, or resolvable tracks | buttons disabled; no transaction |
| `authoring_gesture_active(*state)` | curve-mode row and driver combo disabled |
| Compatible selection is empty | status message; no transaction; no history |
| A selected key's track is missing or not editable | skipped; counted in `skipped_key_count` |
| A selected key's identity no longer resolves | skipped |
| A selected key is Deform / Draw Order / Event / Slot Attachment | skipped in the GUI, **rejected** by the Agent (the MAR-169/170 asymmetry, restated) |
| Two selected refs resolve to the same key | collapsed to one selector |
| Driver not authorable on a selected key's family | rejected atomically |
| Driver supplied with `manual` | rejected atomically |
| Track's driver series contains a non-finite time or value | resolver rejects atomically; the enclosing transaction cancels |
| Track contains a segment shorter than `kKeyTimeEpsilon` | resolver rejects atomically; the enclosing transaction cancels |
| A resolved `cy` is non-finite or leaves `[0, 1]` after narrowing | resolver rejects atomically (defence in depth; unreachable per §6.5) |
| Materialization failure | `cancel()` with rollback; error message |
| `set_keyframe_curve_mode()` error | `cancel()` with rollback; error message |
| `resolve_automatic_curves()` error inside another transaction | that transaction cancels, rolling back its primary mutation too |
| `refresh_runtime()` failure | `cancel()` with rollback; error message |
| Nothing changed | `cancel()`; no history entry; "already uses" message |
| Loading a project whose stored curve disagrees with its mode | accepted; stored curve wins; no rewrite (§9.6) |

Nothing here can leave a partial write: every project-side failure is either
before a primitive's single move-assign or is a `cancel()` of the enclosing
transaction, and the two layers are independently sufficient.

## 16. Non-Goals

- Everything in §3.3, restated for emphasis: **no lane/timeline-edit field**, no
  managed boundary key, no loop-aware tangent. MAR-172 starts from an unclaimed
  namespace.
- No time scaling (MAR-173) or preview playback speed (MAR-174).
- No auto mode for Deform, and no vertex-index or norm driver.
- No per-component curves in any form.
- No incoming handles.
- No preference field, no `editor-settings.json` version bump, and no change to
  `preferences.cpp`/`preferences.hpp`.
- No resolution at load, save, export, or on a timer.
- No `validate_project()` check that a stored curve agrees with its mode.
- No per-keyframe unknown-field preservation (§8.5).
- No change to MAR-168's point drag, MAR-169's handle geometry, clamp/reject
  split, or `[1/3, 1/3, 2/3, 2/3]` conversion seed, or MAR-170's preset
  constants and preference flow — beyond the single documented demotion side
  effect of `set_keyframe_interpolation()` (§4.2).
- No refactor of the numeric `Interpolation` combo or `Bezier X1/Y1/X2/Y2`
  fields onto the shared primitive; MAR-169 and MAR-170 both deferred it and
  MAR-171 keeps the deferral, adding only the demotion (§9.4).
- No manual-visible-UI, Windows 11, or physical-input qualification credit.

## 17. Decisions Taken Under Ambiguity

1. **Auto curves are resolved eagerly into the existing `curve` field, not
   lazily at export.** The story's own description says the metadata is
   "resolving it to the existing shared runtime easing", and criterion 3 says
   changes "recompute affected shared curves in the same transaction" — both
   describe a write. Eager resolution also means the export path, the runtime,
   `.mskl`, `.mbin`, the C ABI, `marrow_inspect`, and any older editor build all
   need zero changes, and the file remains readable by a build that has never
   heard of `curve_mode`. Lazy resolution would put derived data in the exporter
   and make a saved project disagree with its own export.
2. **The metadata lives on the keyframe object, not in a top-level block.**
   `.marrow.snap` is the reference pattern for *discipline* — optional,
   default-absent, path-validated, re-validated, export-neutral — and MAR-171
   follows all of it (§8.7). But snap is a project-wide singleton and curve
   intent is per key, and a top-level side table would key on the very tuple a
   retime changes, going stale on every retime, insertion, deletion, and paste
   (§8.2). The only property that cannot be reproduced at keyframe level is
   unknown-member preservation, which keyframe objects have never had (§8.5).
3. **Mode is per key; the driver is per key too, and mixed drivers are given a
   precise meaning.** The story records "manual/auto mode and driver scalar" as
   one metadata record and criterion 4 speaks of "the segment", so per-key is
   the story's own granularity. Rather than declare mixed drivers undefined,
   §7.4 defines segment `i` to be resolved from key `i`'s driver over the whole
   track's series, which reduces to the obvious whole-track computation in the
   only configuration the UI produces and makes "a driver change recomputes the
   affected curves" mean exactly one segment.
4. **Deform is excluded from auto mode entirely.** A vertex-offset vector has no
   canonical scalar; a norm driver would make the monotonicity claim about a
   quantity the animator never sees, and a vertex-index driver would need an
   unbounded vocabulary that goes stale on re-topology. Excluding it makes the
   boundary compile-enforced (no field on `DeformKeyframeEdit`) rather than
   branch-enforced. Deform keeps full manual support through MAR-169's
   primitive, so nothing is lost.
5. **A flat segment resolves to `[1/3, 1/3, 2/3, 2/3]`, not `[1/3, 0, 2/3, 1]`
   and not "leave it alone".** The normalization is a genuine `0/0` and the limit
   is direction-dependent, so no mathematics selects a value. The choice is the
   one that shapes nothing, because the curve is shared with components the
   driver says nothing about, and the fixture proves the case is real (`body`
   slot colour has a flat Blue segment sharing its curve with varying R, G, A).
   Leaving the segment unresolved would put a stale manual curve inside an auto
   track with no way to see it (§6.4.2).
6. **A zero-duration segment rejects the whole track rather than skipping the
   segment.** Partial resolution is the one outcome that silently produces the
   staleness this story exists to remove. The case is unreachable from any
   authored project because both loaders enforce strictly increasing times and
   `retime_keyframes()` enforces a 1 ms spacing (§6.4.3).
7. **The last key is never written.** It has no outgoing segment and the runtime
   never reads its easing, so writing there would add bytes to every diff and
   every export for no observable effect. Its recorded mode is preserved and
   becomes live when a key is appended after it (§6.4.1).
8. **Recomputation is whole-track (whole-animation from the shell, whole-project
   from the Agent), not windowed.** A value change perturbs a four-segment
   window and the ascending in-place clamp can propagate further; a linear pass
   needs no propagation argument, is trivially deterministic, and costs
   microseconds on any real project (§9.2).
9. **The ascending in-place Fritsch–Carlson clamp is kept, and correctness is
   argued rather than avoided.** A per-segment clamp that never wrote back would
   be order-independent but would break C¹ continuity at every clamped key. The
   in-place variant preserves C¹, and §6.3's downward-closure argument shows the
   later shrinks cannot invalidate an earlier segment's monotonicity, so nothing
   is given up.
10. **`a² + b² <= 9` rather than the exact admissible region.** The disk is the
    standard sufficient condition, is one branch-free test, and is conservative
    in the direction that produces slightly flatter curves. The exact region
    needs case analysis for a difference no animator can see.
11. **`set_keyframe_interpolation()` performs the demotion, rather than every
    caller.** There are four callers today and an unbounded number tomorrow, and
    one omission silently discards an animator's authored curve on the next
    neighbour edit. This is the one MAR-169 contract MAR-171 changes, and it is
    changed because it is the only place the rule cannot be forgotten (§4.2).
12. **A drag on an auto key commits even when the control points end where they
    started.** MAR-169's in-flight net-state `changed` rule would cancel it; that
    is right for a manual key and wrong for an auto one, where the demotion —
    the key ceasing to track its neighbours — is the authored change (§9.4).
13. **`curve_driver` on a manual key is a load error, not silently dropped or
    silently kept.** Dropping loses authored data on the next save; keeping
    creates an in-memory state that never round-trips. Rejecting is the only
    option with neither failure mode and matches how `parse_interpolation()`
    already treats an unknown `curve` string (§8.4).
14. **Load never resolves, so a stale mode/curve pair is legal data.** Resolving
    on load would dirty a project the user only opened, would break load→save
    byte-stability, and would make opening a file a mutation. The stored numbers
    are authoritative for every reader, and `timeline.set_curve_mode` is the
    explicit reconciliation command (§9.6).
15. **The duration trigger is wired even though it is a no-op today.**
    Criterion 3 names duration; in MAR-171 alone a duration change moves no key,
    so the honest implementation wires the call, asserts
    `resolved_key_count == 0`, and documents that the seam exists for MAR-172's
    boundary key (§9.2).
16. **Curve-mode controls live in the Graph toolbar only, not the Dopesheet.**
    MAR-170 put presets in both because a preset needs no component. A driver is
    a component choice and the Dopesheet has no component notion at all; it
    would have to invent one, and the resulting handles are only visible in the
    Graph anyway.
17. **A new Agent operation, rather than widening
    `timeline.set_interpolation`.** Criterion 5 says "Curve-mode mutation has
    matching C++ agent and Python MCP dry-run, validation, affected-key,
    mutation, and undo behavior" — it asks for the mutation, not for a shared
    argument. Overloading `interpolation` with an `"auto"` token would make one
    operation mean two different things (write these exact numbers / derive
    numbers from neighbours), would make `previous_interpolation` ambiguous, and
    would make the demotion rule of §4.2 self-contradictory.
18. **`TimelineScalarComponent` is moved rather than duplicated.** The keyframe
    struct needs the type in `project.hpp`, and defining a parallel
    `TimelineCurveDriver` enum would create two taxonomies to keep in step
    across the parser, the validator, the agent, the MCP schema, and the UI.
    The move is source-compatible for every existing include site because
    `authoring.hpp` includes `project.hpp` (§3.4).
19. **The pure math gets its own translation unit.** `authoring.cpp` is already
    over 2000 lines, and a `ProjectData`-free module is what lets
    `marrow_timeline_model_tests` assert monotonicity, scale invariance, and the
    format invariant without constructing a project (§18.1).

## 18. Validation Strategy

### 18.1 UI-free focused tests — `marrow_timeline_model_tests`

The natural home: it already links `marrow_editor`, already includes
`src/editor`, and already owns the authoring-boundary cases. New cases, all
against `curve_auto::segment_control_points()`:

- **Constants**: every returned entry has `cx1 == 1.0/3.0` and
  `cx2 == 2.0/3.0` exactly, and both narrow into `(0, 1)`.
- **The fixture example (§6.6)**: samples `{(0,0),(0.5,8),(1,-2)}` produce
  `[1/3, 1/3, 2/3, 1]` and `[1/3, 0, 2/3, 2/3]` to `1e-12`.
- **Two samples** produce exactly `[1/3, 1/3, 2/3, 2/3]`.
- **One sample and zero samples** produce an empty vector, not an error.
- **Monotone ramp**: a strictly increasing series produces every
  `cy1, cy2 ∈ [0, 1]` and, sampled through
  `runtime::Interpolation::cubic_bezier(...).transform(alpha)` over
  `alpha ∈ {0, 0.01, ..., 1}`, a finite, non-decreasing result that is exactly
  `0.0` at `alpha = 0` and `1.0` at `alpha = 1`. This is the "runtime math"
  coverage criterion 6 names, asserted against the real solver rather than
  against the formula.
- **Overshoot never happens**: for a spiky series
  `{0, 10, 0.5, 11, 0}` every sampled `transform(alpha)` stays in `[0, 1]`, so
  no evaluated value leaves its segment endpoints.
- **Flat segment**: `{(0,5),(0.5,5),(1,9)}` gives segment 0 exactly
  `[1/3, 1/3, 2/3, 2/3]` and segment 1 a `cy1 == 0` (the plateau zeroes the
  shared tangent), all finite.
- **Repeated values across three keys** `{5, 5, 5}` give two exactly-linear
  segments with no non-finite output.
- **Clamp fires**: a series whose raw normalized tangents exceed the disk (for
  example `{0, 1, 1.0001}` over equal spans) returns `cy1 <= 1` and
  `cy2 >= 0`, with `hypot(a, b) <= 3 + 1e-9`.
- **Extreme ratio**: `{0, 1e-300, 1e300}` over equal spans returns finite
  control points inside `[0, 1]` — the `std::hypot` path, which
  `sqrt(a*a + b*b)` would collapse to zero.
- **Scale invariance**: multiplying every time by `1000` and every value by
  `-7` returns bitwise-identical control points.
- **Rejections**: a non-finite time, a non-finite value, a non-increasing time,
  and a segment of exactly `1e-7` s each return `std::nullopt`.
- **Determinism**: two calls on the same input return byte-identical vectors.

### 18.2 UI-free focused tests — `marrow_project_smoke`

A new `validate_mar171_automatic_curves(const ProjectLoadResult&)` beside
MAR-169's and MAR-170's validators:

- **Default-off, byte-for-byte**: `serialize_project()` of the untouched fixture
  is byte-identical to a pre-MAR-171 build's output, and contains neither
  `curve_mode` nor `curve_driver`. Save it, reload it, serialize again, compare.
- **Round trip**: set `spine` rotate keys 0 and 1 to auto with driver `angle`;
  assert the stored control points equal §6.6's table; save; reload; assert
  `curve_mode`, `curve_driver`, and all four control points survive bitwise.
- **Manual keys stay clean**: after that edit, the untouched `arm_l` keyframes
  serialize without either field.
- **The `.marrow` text**: the saved document contains
  `"curve_mode": "auto"` and `"curve_driver": "angle"` exactly on the two edited
  keys and nowhere else.
- **Load validation**: hand-built documents each rejected with the expected
  message and JSON path — unknown `curve_mode` token, non-string `curve_mode`,
  unknown `curve_driver` token, `curve_driver` on a Rotate key naming `x`,
  `curve_driver` present with `curve_mode` absent, `curve_driver` present with
  `"manual"`.
- **`validate_project()`**: a `ProjectData` built in memory with an `Auto`
  Slot Color key whose driver is `Angle` is rejected with the driver-authorability
  message.
- **Family rejections**: `set_keyframe_curve_mode()` on a Deform, Draw Order,
  Event, and Slot Attachment selector each rejected atomically with
  `serialize_project()` byte-identical.
- **Duplicate selector**, **unresolvable selector**, and **empty selector list**
  each rejected atomically.
- **Segment-wide identity**: after setting a Translate key to auto with driver
  `y`, both `x` and `y` still read through the one changed `interpolation` and
  exactly one `interpolation` field in the whole project differs from the
  snapshot. Repeat with driver `x` and assert the resulting curve differs from
  the driver-`y` result while still being one shared field.
- **Neighbour recomputation**: move key 1's time with `retime_keyframes()`, call
  `resolve_automatic_curves()`, and assert both neighbouring segments' control
  points changed to the values §6.3 predicts for the new spacing; then offset
  key 1's value with `offset_keyframe_scalars()` and assert the same.
- **Manual conversion**: `set_keyframe_interpolation()` on an auto key sets
  `curve_mode == Manual`, reports `changed == true` even when the four control
  points are byte-identical, and a following `resolve_automatic_curves()`
  reports `resolved_key_count == 0` and leaves the project byte-identical.
- **No-change**: `set_keyframe_curve_mode(Auto, Angle)` twice returns
  `changed == false` with a byte-identical project on the second call.
- **Re-resolve reports work**: perturb a neighbour without resolving, then call
  `set_keyframe_curve_mode(Auto, Angle)` on an already-auto key and assert
  `changed_key_count == 0` while `resolved_key_count > 0` and the result is
  `changed`.
- **Zero-duration rejection**: build a track with two keys `1e-7` s apart, set
  one to auto, and assert the resolver rejects atomically with the project
  unchanged.
- **Last key**: setting the final key of a track to auto leaves its stored
  easing byte-identical while recording the mode.
- **Whole-project resolve**: `resolve_automatic_curves(project, {})` on a
  project with no auto key reports `auto_key_count == 0`,
  `resolved_key_count == 0`, and a byte-identical project.

### 18.3 UI-free focused tests — save/reload and duration

Within the same validator, because they need `save_project()`/`load_project()`:

- **Stale pair is legal**: write a project with `curve_mode: "auto"` beside a
  hand-chosen `curve` the resolver would not produce; assert it loads with no
  error, that `serialize_project()` of the loaded project is byte-identical to
  the input, and that the stored curve is unchanged — proving load never
  resolves.
- **Explicit reconciliation**: on that same project, `set_keyframe_curve_mode(Auto,
  <same driver>)` reports `resolved_key_count >= 1` and rewrites the curve to
  the §6.3 value.
- **Duration is a wired no-op**: with auto keys present, `set_animation_duration()`
  followed by `resolve_automatic_curves()` reports `resolved_key_count == 0` and
  a project that differs from the snapshot only in the duration.

### 18.4 Headless shell smoke — `src/editor/shell_smoke_graph.cpp`

A new `validate_timeline_curve_mode_shell_smoke()` with an isolated session,
declared in `shell_smoke_scenarios.hpp` and called from `shell_smoke.cpp` after
MAR-170's scenario:

- select three `spine` Rotate keys and apply `Auto` with driver `Angle`: one
  history entry, `changed_key_count == 3`, every key's `time` and `angle`
  byte-identical, every resolved curve equal to §6.6's table;
- a selection mixing Transform, Slot Color, and Event keys: the Event key is
  skipped, `skipped_key_count == 1`, the others are written, still one history
  entry;
- a selection of only Deform keys: no transaction, `undo_count()` unchanged,
  status message set;
- re-applying the same mode and driver: `applied == false`, no history entry,
  project bytes identical;
- **criterion 3 direct, four ways**, each asserted to be **one** history entry
  with the auto curves updated inside it:
  a MAR-168 value drag on a neighbouring key; a dopesheet retime of a
  neighbouring key; `add_timeline_key_at_playhead()` between two auto keys; and
  `remove_selected_timeline_keys()` on a middle key;
- **paste**: copy an auto key, paste it into another animation, and assert the
  pasted key carries `Auto` and its driver, that the paste is one history entry,
  and that the pasted key's segment is resolved against its **new** neighbours;
- **criterion 4 direct**: with the Graph tab displaying component Y on a
  Translate track whose keys are auto with driver X, run a MAR-169 handle drag
  and assert one history entry, `curve_mode == Manual` on the dragged key only,
  the X segment's `SegmentKind` and control points changed identically, and the
  neighbouring auto keys untouched;
- **drag away and exactly back on an auto key**: one history entry,
  `Manual` mode, byte-identical control points — the §9.4 rule;
- **drag away and exactly back on a manual key**: zero history entries, proving
  MAR-169's net-state rule is preserved for manual keys;
- **preset on an auto key** (once MAR-170 lands): one history entry, mode
  becomes `Manual`, the preset's exact constants are stored;
- **undo/redo**: undo a curve-mode application and assert the mode, the driver,
  and every resolved curve return to their previous values with `selected_keys`
  and `active_key` bit-identical; redo restores them;
- **cancel**: `cancel_authoring_gestures()` during the handle drag on an auto
  key restores `serialize_project()`, `undo_count()`, `redo_count()`,
  `project_revision()`, `dirty()`, the rebuilt dopesheet `key_times`, every
  rebuilt graph `Key::values`, every rebuilt `Segment::kind`, every rebuilt
  `outgoing_easing`, and the auto mode;
- **materialization**: the first `Auto` application to the runtime-only
  `slot:0:Color` track copies all its keys into the project rather than
  replacing the track, and resolves against the copied keys;
- **fail closed**: a track injected with a zero-duration segment makes the
  curve-mode application fail with the project byte-identical and no history
  entry;
- `agent_operation_descriptor_count() == 58` before and after every case.

### 18.5 Export — the criterion this story must not fake

MAR-168 exported the mutated project and reported `JSON 14338 bytes` against the
untouched baseline's `14336`. MAR-169 exported only the untouched baseline and
reported `14336` — byte-identical to MAR-167's and MAR-168's baseline numbers —
so no authored curve was ever proven to reach a runtime file even though its
criterion 6 named export. **MAR-171 must not repeat that.**

The MAR-171 export block in `marrow_project_smoke` must:

1. take the project it just **mutated** (auto on `spine` Rotate keys 0 and 1,
   and on `arm_l` Rotate key 0, whose stored `curve` is the string `"linear"`
   today);
2. call `export_runtime_assets()` on **that** project into
   `/tmp/marrow_mar171_auto.mskl` and `/tmp/marrow_mar171_auto.mbin`;
3. load the exported `.mskl` with `runtime::load_skeleton_data()` and assert the
   `spine` rotate timeline's keyframe 0 easing is `CubicBezier` with
   `cx1 == static_cast<float>(1.0/3.0)`, `cx2 == static_cast<float>(2.0/3.0)`,
   `cy1 == static_cast<float>(1.0/3.0)`, and `cy2 == 1.0f` — the §6.6 values,
   asserted exactly rather than by byte count;
4. read the exported `.mskl` as **text** and assert it contains neither
   `curve_mode` nor `curve_driver` — the direct proof of §8.6's export
   neutrality;
5. assert `arm_l` rotate key 0 exports a 4-number `curve` array where the
   baseline exports the string `"linear"`;
6. validate the `.mbin` against the `.mskl` with the existing
   `validate_binary_export()` helper;
7. print `MAR-171 auto export: JSON <n> bytes, MBIN <m> bytes.` and, separately,
   the untouched baseline's sizes.

**The expected observable difference.** The baseline export is `14336` bytes.
The auto export must be **strictly larger**, because each auto key replaces
either the string `"linear"` (9 characters) or a shorter 4-number array with a
full 4-number array of `1/3`/`2/3` values. A reviewer seeing `14336` in the
MAR-171 row must treat it as the same defect recurring, and the smoke's step 3
and step 5 assertions must fail before the byte count is ever consulted. The
`.mbin` size may legitimately stay `3984`, because the binary encodes four
`float32` per cubic key regardless of the decimal length — that is not evidence
of the defect, and step 6 covers it.

### 18.6 Actual-frame smoke — `src/editor/shell_smoke_frames.cpp`

Using real ImGui mouse events against real rendered coordinates from
`TimelineGraphRenderStats`, following the MAR-169 block at
`shell_smoke_frames.cpp:968-1240`:

- render a frame and assert `curve_mode_row_drawn`, finite
  `first_curve_mode_min_x/y`, and that MAR-167/168/169/170's `fit_*`,
  `first_component_*`, `first_point_*`, `first_handle_*`, and
  `first_preset_*` rectangles are still reported at their previous coordinates
  — the appended row must displace nothing;
- with keys selected, click the reported `Auto` button and assert one history
  entry, `active_key_auto == true`, and the expected stored curve;
- with nothing selected, assert `curve_mode_row_enabled` is false and a click
  changes nothing;
- assert the curve-mode row and the driver combo are inert while a MAR-169
  handle drag is live;
- with the active key auto, press the reported `first_handle_x/y`, move 30 px,
  release, and assert `active_key_auto` becomes false in the next frame's stats
  and exactly one history entry was added.

### 18.7 Agent smoke — `marrow_agent_dispatch_smoke`

- registry is exactly 58 operations, the new row's metadata is
  (`edit`, mutating, not review, dry-run supported), and it sits immediately
  after `timeline.set_interpolation` in `kExpectedOperations`;
- `timeline.set_curve_mode` dry run leaves `project_revision()`,
  `undo_count()`, and `dirty()` unchanged and reports the correct
  `previous_mode`, `previous_driver`, and `previous_interpolation` per key;
- live call mutates, reports `changed_key_count` and `resolved_key_count`, and
  adds exactly one history entry;
- a second identical live call returns `no_change`;
- a live call with `mode: "auto"` on already-auto keys **after** a
  `timeline.retime_keyframes` reports `changed_key_count == 0`,
  `resolved_key_count > 0`, and succeeds — the explicit reconciliation path;
- `timeline.set_interpolation` on an auto key reports `changed` and a following
  `timeline.set_curve_mode` dry run reports `previous_mode == "manual"` — the
  demotion, proven through the Agent surface;
- `timeline.retime_keyframes` on a neighbour of an auto key changes that key's
  curve, proven by a `timeline.set_interpolation` dry run's
  `previous_interpolation` before and after, in one history entry that one
  `undo` fully reverses;
- `undo` restores mode, driver, and every resolved curve, verified by a
  follow-up dry run;
- rejection cases, each with the project unchanged: missing `mode`, unknown
  `mode`, unknown `driver`, `driver` with `"manual"`, a driver not authorable on
  the family, a `deform` key, a `draw_order` key, an unresolvable selector
  (expect `not_found`), a duplicate selector, and an empty `keys` array;
- `set_transform` on a neighbour of an auto key updates that curve in the same
  history entry.

### 18.8 MCP — `tools/mcp/test_client.py`

- `len(registry_names) == 58`, `len(mcp_names) == 58`,
  `set(registry_names) == set(mcp_names)`, all names unique;
- `timeline.set_curve_mode` present in `new_edit_operations` and in the MCP edit
  tool set, with its registry metadata row asserted explicitly;
- dry run -> live -> read-back -> `undo` -> read-back proving mutation, the
  resolved control points (compared at four decimal places against
  `[0.3333, 0.3333, 0.6667, 1.0]`), and restoration;
- a `timeline.retime_keyframes` between two `timeline.set_interpolation` dry runs
  proving a neighbour move changed an auto key's curve through the MCP surface;
- rejection of `"mode": "automatic"`, of `"driver": "angle"` on a `slot_color`
  key, of a `deform` key, and of a `driver` supplied with `"manual"` — proving
  the schema change did not loosen the C++ gate.

### 18.9 Display and compatibility gates

See the "Full verification checklist" in
`docs/superpowers/plans/2026-08-30-mar-171-project-local-automatic-curve-handles.md`.
Automated display tests prove the exercised ImGui/display path only. They add no
manual-visible-UI, Windows 11, or physical-input qualification credit.

## 19. Documentation and Milestone Closure

After code and every required gate pass:

- add a `MAR-171 Project-Local Automatic Curve Handles Validation Results`
  section to `AGENTS.md` in the existing table plus command-output format, and
  update `Current Validation`'s registry line to 58 operations;
- update `AGENTS.md`'s `Project State` line so MAR-171 is complete and MAR-172
  is next;
- update `docs/root1/discription.md`, `quick-start.md`, `concepts.md`,
  `editing-gap-analysis.md`, `refector.md`, and `platform-validation.md` where
  the "MAR-171 is next" wording, the registry size, or the curve-authoring
  contract is now stale, applying §14's historical/current rule to every hit;
- `docs/root1/format-spec.md` gains, in the `.marrow` section, a new
  `curve_mode` / `curve_driver` paragraph under `timeline_edits` documenting the
  two optional keyframe members, their tokens, their absent-means-manual
  default, the family/driver compatibility table, the strict validation rules,
  and the statement that neither field ever enters `.mskl` or `.mbin` and that
  no format version changes. The existing `cx in [0, 1]` paragraph gains one
  clause noting that automatic curves always use `cx1 = 1/3` and `cx2 = 2/3` and
  therefore satisfy it by construction;
- mark `MAR-171` done with the verified completion date and leave `MAR-172`
  open;
- leave MAR-192 through MAR-210 open and add no platform qualification credit;
- change this document's status to `Implemented and validated` only after every
  gate is green.

## 20. Commercial-Tool Reference Boundary

Automatic/manual curve intent per key is standard in 2D animation tooling; the
interaction direction is informed by the official documentation:

- Spine Graph: <https://us.esotericsoftware.com/spine-graph>
- Live2D Graph Editor: <https://docs.live2d.com/en/cubism-editor-manual/grapheditor/>

The monotone tangent rule is Fritsch, F. N. and Carlson, R. E., "Monotone
Piecewise Cubic Interpolation", *SIAM Journal on Numerical Analysis* 17(2),
1980, pp. 238–246.

Marrow deliberately does **not** copy per-component automatic handles, incoming
handles, free per-component curve ownership, or a smoothing mode that can
overshoot. The runtime's one-outgoing-easing-per-key model, MAR-169's
segment-wide curve identity, and the `cx ∈ [0, 1]` format invariant take
precedence over surface similarity to another editor.
