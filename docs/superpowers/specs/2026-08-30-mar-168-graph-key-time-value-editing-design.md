# MAR-168 Graph Key Time and Scalar Value Editing Design

**Date:** 2026-08-30

**Status:** Implemented and validated (2026-08-30)

**Authority:** `.agents/tasks/prd-marrow-runtime.json` story `MAR-168` and
`docs/root1/discription.md`

**Depends on:** MAR-167 (`ff10eaf`), whose read-only graph contract is defined in
`docs/superpowers/specs/2026-08-20-mar-167-synchronized-scalar-graph-design.md`

## 1. Goal

MAR-167 delivered a read-only Graph tab that projects one focused continuous
parent track and shares parent-key identity, selection, `active_key`, focused
track, and playhead with the dopesheet. MAR-168 makes that graph authoritative
for editing by letting a drag on a graph point change either the active scalar
component's value or the whole parent key's time.

Every mutation goes through the same shared authoring primitives the dopesheet
already uses. MAR-168 introduces no parallel key-writing path, no per-component
key identity, and no easing editing.

## 2. Approved Product Decisions

The following decisions are fixed for MAR-168:

1. A left press on a graph point starts a *drag candidate*, not a transaction.
   No `begin_edit()` occurs until the pointer leaves an inclusive 4.0
   logical-pixel dead zone. A press-and-release without motion is exactly the
   MAR-167 selection click.
2. The drag axis is decided once, at the moment the dead zone is left, by
   dominant-axis comparison, and is then locked for the rest of the gesture.
   `|dx| > |dy|` locks the Time axis; otherwise the Value axis. There is no
   per-frame axis re-evaluation and no diagonal edit.
3. A Time-axis drag reuses the dopesheet retime gesture verbatim:
   `begin_timeline_retime_gesture()` / `apply_timeline_retime_delta()` /
   `finish_timeline_retime_gesture()`. It therefore moves the entire current
   selection, snaps to frames, clamps against unselected neighbours, keeps the
   1 ms non-event spacing, rebuilds stable identities, and grows explicit
   durations, identically to the dopesheet.
4. A Value-axis drag edits exactly one scalar component — the component of the
   pressed point — on every selected parent key that belongs to the focused
   graph track. Every other component, the key time, and the outgoing easing of
   those keys are carried through byte-identical.
5. Value edits are expressed as one signed delta, never as an absolute
   per-key assignment. A delta preserves the shape of a multi-key selection and
   is setup-pose invariant for Rotate.
6. Angle, Translate X/Y, Scale X/Y, and Shear X/Y are **not** clamped. Slot
   Color R/G/B/A **is** clamped to `[0, 1]`, group-wide, so a multi-key drag
   stops as one unit instead of collapsing.
7. Frame snapping is one shared setting. The Graph tab exposes the same
   `TimelineEditorState::snap_to_frames` checkbox the Dopesheet tab owns, and
   Alt bypasses it for the current drag exactly as in the dopesheet.
8. The graph view transform is frozen for the duration of a drag. Wheel zoom,
   Shift-wheel value zoom, middle-drag pan, `Fit`, and `needs_fit` are all
   suppressed while a graph drag candidate or gesture is live.
9. The playhead does not follow the dragged key. It is scrubbed once by the
   MAR-167 activation on press and then left alone.
10. Bezier/easing editing, value snapping, point insertion/deletion, graph box
    selection, and time scaling remain out of scope.

## 3. Scope

### 3.1 Supported lane families and editable components

| Parent track | Editable components | Value axis clamp | Project field written |
| --- | --- | --- | --- |
| Bone Rotate | Angle | none | `TransformKeyframeEdit::angle` |
| Bone Translate | X, Y | none | `TransformKeyframeEdit::x` / `::y` |
| Bone Scale | X, Y | none (signed, exact zero preserved) | `TransformKeyframeEdit::x` / `::y` |
| Bone Shear | X, Y | none | `TransformKeyframeEdit::x` / `::y` |
| Slot Color | R, G, B, A | `[0, 1]` group-wide | `SlotColorKeyframeEdit::color.{r,g,b,a}` |

All five families support the Time axis through the shared retime primitive.

### 3.2 Explicit exclusions

MAR-168 does not add:

- Bezier handle or interpolation mutation of any kind (MAR-169);
- curve presets or remembered default curves (MAR-170);
- automatic/manual handle metadata (MAR-171);
- loop-boundary key synchronization (MAR-172);
- selected-key time scaling or a scale pivot (MAR-173);
- preview playback speed (MAR-174);
- graph point insertion, deletion, copy/cut/paste, or box selection;
- value-axis snapping to a grid or to neighbouring values;
- FFD, Inherit, Attachment, Draw Order, or Event editing from the graph;
- any Agent or MCP operation. MAR-169 is the story that adds agent/MCP surface
  for graph authoring; the registry stays at exactly 56 operations.

### 3.3 Compatibility boundaries

MAR-168 changes none of the following:

- `.marrow` project schema (no new fields; existing keyframe fields are written
  by an existing shape);
- `.mskl` v1 and `.mbin` v2;
- C ABI v1 (`include/marrow/c_api/**` and `src/c_api/**` are untouched);
- `SelectionSet` entity identity;
- runtime/GPU resource ownership;
- the exact 56-operation Agent/MCP surface;
- dopesheet behaviour, including the existing retime gesture's status messages,
  clamping, and post-undo selection reconciliation.

The one public-header change is **purely additive**: one enum, one result
struct, and one function appended to `include/marrow/editor/authoring.hpp`
alongside `retime_keyframes()`. No existing declaration is modified or removed.

## 4. Existing Boundaries Reused

| Concern | Reused primitive | Location |
| --- | --- | --- |
| Parent-key identity | `TimelineKeyRef`, `timeline_key_ref()`, `timeline_key_index()` | `timeline_model` |
| Selection / active key | `apply_key_activation()`, `reconcile_selection()` | `timeline_model` |
| Graph point activation | `activate_timeline_graph_point()` | `shell_timeline_graph` |
| Frame snapping | `snap_delta_to_frames()` inside `retime_keyframes()` | `timeline_model` / `authoring` |
| Neighbour collision | `include_retime_bounds()` / `include_resolved_retime_bounds()` | `timeline_model` / `authoring` |
| Whole-key retime | `begin/apply/finish_timeline_retime_gesture()` | `timeline_controller` |
| Overlay materialization | `visit_editable_timeline_keys()`, `ensure_*_timeline_edit_index()` | `timeline_controller` |
| Project-domain key resolution | `TimelineKeySelector`, `timeline_key_selector()`, `resolve_timeline_key()` | `authoring` |
| Explicit-duration auto-grow | `auto_extend_explicit_animation_durations()` invoked by `EditTransaction::refresh_runtime()` and `::commit()` | `session` |
| Live preview / one history entry | `EditorSession::EditTransaction` | `session` |
| Gesture cancel on focus loss / shutdown | `cancel_authoring_gestures()` | `shell_core` |
| Gesture mutual exclusion | `authoring_gesture_active()` | `shell_state` |

## 5. Architecture

```text
ImGui graph body (shell_timeline_graph.cpp)
        | plain doubles / bools only
        v
graph point drag driver (shell_timeline_graph.cpp, ImGui-free entry points)
        |                                   |
        | Value axis                        | Time axis
        v                                   v
timeline_controller value gesture    timeline_controller retime gesture
        |                                   |   (unchanged MAR-167 code)
        v                                   v
offset_keyframe_scalars()            retime_keyframes()
        \                                   /
         \_____ EditorSession::EditTransaction _____/
                    (refresh_runtime -> auto-grow -> preview,
                     commit -> exactly one history entry)
```

New drag math lives in `timeline_graph_model` (UI-free, unit tested). Gesture
lifecycle lives in `timeline_controller` (ShellState-aware but ImGui-free, so
the headless shell smoke drives it directly). Only pointer/keyboard sampling
lives in `shell_timeline_graph.cpp`.

## 6. The Drag Model

### 6.1 Press

`draw_timeline_graph_body()` already runs the MAR-167 hit test on
`ImGui::IsItemClicked(ImGuiMouseButton_Left)`. On a hit it calls
`activate_timeline_graph_point()`. MAR-168 appends one step, mirroring the
dopesheet lane exactly:

```text
if activate_timeline_graph_point(...) succeeded
   and the pressed parent key is still in selected_keys
   and timeline_track_is_editable(row)
   and !authoring_gesture_active(*state)
then record a drag candidate
```

The "still selected" condition reproduces the dopesheet rule for free: a plain
click always selects, so it always arms a drag; a Cmd/Ctrl click that *adds* the
key arms a drag; a Cmd/Ctrl click that *removes* it does not.

The drag candidate stores, and never re-reads:

- `item_id` — the plot `InvisibleButton` ID;
- `track_id`, the pressed `TimelineKeyRef`, the pressed `Component`, and its
  `component_index`;
- `press_pointer_x`, `press_pointer_y` in screen logical pixels;
- `frozen_view` and `frozen_plot` captured from this frame;
- `press_time_seconds` and `press_value` read from the projected key.

A drag candidate holds **no transaction**. `authoring_gesture_active()` stays
false while only a candidate exists, so a press cannot block unrelated editing.

### 6.2 Axis decision

Every frame while the candidate is live and the left button is down, the driver
computes `dx = pointer_x - press_pointer_x` and `dy = pointer_y - press_pointer_y`.

```cpp
DragAxis decide_drag_axis(
    double press_x, double press_y,
    double pointer_x, double pointer_y,
    double dead_zone_pixels);   // 4.0
```

Rules:

- Any non-finite input returns `Undecided` and mutates nothing.
- `max(|dx|, |dy|) < dead_zone_pixels` returns `Undecided`.
- Otherwise `|dx| > |dy|` returns `Time`, and `|dx| <= |dy|` returns `Value`.
  The tie goes to `Value` because the graph is the only surface that can edit a
  value, whereas the dopesheet already offers time editing.

The returned axis is written into the candidate once and never recomputed.

**Justification against acceptance criterion 1.** A locked axis is what makes
the criterion literally true rather than approximately true. A Value-locked
gesture never calls any retime primitive, so it provably "edits only the active
scalar component": the key time and every other component are untouched project
bytes. A Time-locked gesture never calls the value primitive, so the key's time
changes and "all of its components" travel with it unchanged, because
`retime_keyframes()` only rewrites `keyframe.time`. A per-frame or free 2-D
model would let a nominally vertical drag emit a one-microsecond time change,
which would violate the criterion, produce spurious frame-snap jumps, and make
the atomic-rollback proof in §10 much weaker.

### 6.3 Time axis

On the transition `Undecided -> Time` the driver calls

```cpp
begin_timeline_retime_gesture(state, item_id, static_cast<float>(press_pointer_x), tracks);
```

which begins the transaction, snapshots `selected_keys` and their original
times, and fails closed if any selected key is unresolvable or its track is not
editable. A failure cancels the candidate and leaves the project untouched.

Each subsequent frame:

```cpp
requested_delta = drag_time_delta(frozen_view, press_pointer_x, pointer_x);
snap            = state->timeline_editor.snap_to_frames && !alt_down;
apply_timeline_retime_delta(state, tracks, requested_delta, snap);
```

`drag_time_delta()` is `(pointer_x - press_x) / view.pixels_per_second`, using
the frozen graph view. Nothing about the dopesheet's `pixels_per_second` is
read, so a graph drag is unaffected by dopesheet zoom and vice versa.

Everything downstream is unchanged MAR-167 code: `apply_timeline_retime_delta()`
materializes overlays once, resolves indices, builds `TimelineKeySelector`s,
calls `retime_keyframes()` with the shared snap/collision rules, calls
`transaction.refresh_runtime()` (which runs
`auto_extend_explicit_animation_durations()`), re-syncs the shell, rebuilds the
selection and `active_key` from the new key indices, and accumulates
`applied_delta`.

Release commits with `finish_timeline_retime_gesture(state, true)`; Escape,
focus loss, shutdown, or a tab switch cancels with `false`.

### 6.4 Value axis

On the transition `Undecided -> Value` the driver calls

```cpp
begin_timeline_graph_value_gesture(state, item_id, row, component, tracks);
```

which:

1. rejects a null state, a non-editable row, an unsupported (row kind,
   component) pairing, an empty selection, or `authoring_gesture_active()`;
2. begins one `EditTransaction` with
   `EditKind::EditProperty`, label `"Edit graph key value"` (plural
   `"Edit graph key values"` for more than one key), group
   `"timeline:graph-value"`, `allow_merge = false`, impact
   `Project | Runtime | Preview`;
3. collects the gesture key set: every entry of
   `state->timeline_editor.selected_keys` whose `track_id` equals the focused
   row's ID, in stable selection order. Keys on other tracks are ignored and
   left untouched;
4. records each key's current projected component value in `original_values`;
5. cancels the transaction and returns false if any gesture key cannot be
   resolved to an index in the row.

Each subsequent frame:

```cpp
requested_delta = drag_value_delta(frozen_view, press_pointer_y, pointer_y);
apply_timeline_graph_value_delta(state, tracks, requested_delta);
```

`drag_value_delta()` is `(press_y - pointer_y) / view.pixels_per_value`. The
sign inversion is the screen-to-value flip: ImGui `y` grows downward while
values grow upward.

`apply_timeline_graph_value_delta()` mirrors `apply_timeline_retime_delta()`
step for step:

1. return early when the requested delta equals the applied delta within `1e-12`;
2. compute the incremental delta with the shared
   `timeline_model::incremental_retime_delta()`; a non-finite result cancels the
   gesture and reports `"Graph value delta must be finite."`;
3. on the first application, materialize each gesture key's track through
   `visit_editable_timeline_keys(state, row, [](auto&){})`, so a first edit
   copies every imported runtime key into the project instead of replacing the
   track. Failure cancels;
4. re-resolve each gesture key's index with `timeline_key_index()`; a lost
   identity cancels with `"The selected graph keys changed during editing"`;
5. build one `TimelineKeySelector` per gesture key with the shared
   `timeline_key_selector()`; failure cancels;
6. call `offset_keyframe_scalars(transaction.project(), selectors, component,
   incremental_delta)`. A failed result cancels and surfaces the primitive's
   error;
7. when nothing changed, return true without touching runtime;
8. call `transaction.refresh_runtime()`; failure cancels;
9. `sync_shell_from_editor_session(state)`;
10. accumulate `applied_delta += result.applied_delta` and set `changed = true`.

Because a value edit never moves a key in time, step 4's identities are stable
by construction and `selected_keys` / `active_key` need no rebuild. The driver
still re-resolves defensively so that an out-of-band project change cancels
rather than writing to the wrong key.

Release commits with `finish_timeline_graph_value_gesture(state, true)`, which
uses the shared `timeline_model::completion_decision(commit, changed)` so an
unchanged gesture cancels instead of pushing an empty history entry — the exact
rule `finish_timeline_retime_gesture()` already uses.

## 7. Value Mapping

### 7.1 Pixels to units

Four pure functions move from `shell_timeline_graph.cpp` (where MAR-167 keeps
them as file-local helpers) into `timeline_graph_model`, so the drag math and
the render math cannot drift apart:

```cpp
double time_at_x(PlotRect rect, const View& view, double x);
double value_at_y(PlotRect rect, const View& view, double y);
double x_at_time(PlotRect rect, const View& view, double time_seconds);
double y_at_value(PlotRect rect, const View& view, double value);
```

with the MAR-167 definitions preserved exactly:

```text
x     = rect.min_x + (time - view.view_start_seconds) * view.pixels_per_second
y     = (rect.min_y + rect.max_y) * 0.5 - (value - view.value_center) * view.pixels_per_value
time  = view.view_start_seconds + (x - rect.min_x) / view.pixels_per_second
value = view.value_center + ((rect.min_y + rect.max_y) * 0.5 - y) / view.pixels_per_value
```

The two delta functions used by dragging need only the `View`:

```cpp
std::optional<double> drag_time_delta(const View& view, double press_x, double pointer_x);
std::optional<double> drag_value_delta(const View& view, double press_y, double pointer_y);
```

Both return `std::nullopt` when any input is non-finite, when the relevant
scale is not finite and positive, or when the quotient is non-finite. A
`std::nullopt` cancels the gesture; it never silently becomes zero.

### 7.2 Per-component semantics

**Rotate / Angle.** The graph plots the absolute unwrapped local angle
`setup_rotation + key.angle`, where the runtime's
`BoneRotateTimeline::setup_rotation` is populated from
`bones[i].setup_pose.rotation` at parse time and is therefore identical to the
value `setup_relative_rotation_key()` subtracts. A **delta** is consequently
identical in absolute and setup-relative space, so `offset_keyframe_scalars()`
adds the delta straight onto `TransformKeyframeEdit::angle` with no setup-pose
lookup at all. Multi-turn angles are neither wrapped nor normalized: an
authored `730deg` stays `730deg`, preserving the MAR-161 raw-angle contract.

**Translate X/Y, Scale X/Y, Shear X/Y.** Plain additive deltas on `x` / `y`. No
clamp, no sign forcing, no minimum magnitude. Scale keeps signed values and
exact zero authorable, which MAR-162's signed local scale gizmo depends on.

**Slot Color R/G/B/A.** Clamped to `[0, 1]`, group-wide, inside the authoring
primitive:

```text
lower = -min(original_values)
upper = 1 - max(original_values)
applied = (upper < lower) ? 0.0 : clamp(requested, lower, upper)
```

The group clamp keeps a multi-key drag rigid: every selected key stops at the
same moment instead of the group flattening against the boundary. The
degenerate `upper < lower` case can only arise from imported data already
outside `[0, 1]`; it yields a zero delta (a no-op frame, not an error) so the
graph neither jumps such a key nor blocks the rest of the gesture. Each written
channel is additionally clamped into `[0, 1]` as a defensive post-condition,
matching the clamp `draw_slot_color_timeline_editor()` already applies.

### 7.3 Range validation

Every written scalar must satisfy
`std::isfinite(value) && std::abs(value) <= std::numeric_limits<runtime::AnimationScalar>::max()`
before the primitive commits its candidate. A violation is an error with no
mutation, which the gesture turns into an atomic cancel. This is the same
float32 boundary `set_animation_duration()` enforces for times.

## 8. Time Mapping

Time editing adds no new rule. It is the dopesheet path with a graph-derived
delta:

- **Frame snap.** `retime_keyframes()` snaps the delta with
  `timeline_model::snap_delta_to_frames(earliest_original_time, delta, fps)`
  using `TimelineEditorState::frames_per_second`. When neighbour clamping binds,
  it re-snaps inward so the written keys still land on a frame boundary, and
  applies nothing when no boundary fits. Alt suppresses snapping for the current
  frame exactly as `update_timeline_retime_gesture()` does.
- **Collision.** `include_resolved_retime_bounds()` clamps the shared delta
  against zero and against the nearest unselected neighbour on each side,
  keeping `timeline_model::kNonEventKeySpacing` (1 ms) for non-event tracks.
  Clamping is *not* a failure: the point stops at the boundary and the drag
  continues, identically to the dopesheet.
- **Hard rejection.** `retime_keyframes()` returns an error, and the gesture
  rolls back atomically, when bounds are inconsistent, a key is selected twice,
  a selector cannot be resolved, or a time is non-finite/negative.
- **Stable identity.** After each successful delta,
  `apply_timeline_retime_delta()` rebuilds `selected_keys` and `active_key` from
  the same resolved key indices in the rebuilt track set, so the graph point
  under the cursor stays the same key across the whole drag.
- **Explicit-duration auto-grow.** `EditTransaction::refresh_runtime()` and
  `::commit()` both call `auto_extend_explicit_animation_durations()`. Dragging
  a key past an authored explicit duration therefore grows that duration inside
  the same transaction and the same undo entry. Animations without an explicit
  duration stay inference-driven. Neither path ever shrinks a duration.

## 9. Component Preservation

The guarantee is structural, not defensive:

1. `offset_keyframe_scalars()` starts from `ProjectData candidate = *project`,
   so every byte not explicitly written is already correct.
2. It resolves each selector through the shared `resolve_timeline_key()` to a
   `(timeline_index, key_index)` pair.
3. It writes exactly one scalar field of that keyframe:
   `angle`, `x`, `y`, or one channel of `color`. `time` and `interpolation` are
   never named in the write path.
4. It rejects any (kind, component) pairing outside §3.1 — for example `Angle`
   on a Translate channel, `X` on a Slot Color track, or any component on a
   Deform, Draw Order, Event, or Slot Attachment key — before writing anything.
5. Only after every write succeeds does `*project = std::move(candidate)`.

For the Time axis the equivalent guarantee is that `apply_resolved_retime()`
writes only `keyframe.time`, so all components of the key move together with
their values and their shared outgoing easing intact.

Both paths preserve the MAR-167 invariant that a parent key owns one outgoing
easing shared by all its components. No code path in MAR-168 reads or writes
`Interpolation`.

## 10. Transaction Lifecycle and Atomic Rollback

### 10.1 One drag, one history entry

| Phase | Time axis | Value axis |
| --- | --- | --- |
| Press | drag candidate only, no transaction | drag candidate only, no transaction |
| Dead zone left | `begin_timeline_retime_gesture()` opens one transaction | `begin_timeline_graph_value_gesture()` opens one transaction |
| Each frame | `apply_timeline_retime_delta()` mutates `transaction.project()` and calls `refresh_runtime()` | `apply_timeline_graph_value_delta()` mutates `transaction.project()` and calls `refresh_runtime()` |
| Release | `finish_timeline_retime_gesture(state, true)` | `finish_timeline_graph_value_gesture(state, true)` |
| Escape / focus loss / shutdown / tab switch / project reload | `finish_timeline_retime_gesture(state, false)` | `finish_timeline_graph_value_gesture(state, false)` |

`refresh_runtime()` explicitly does not create history; only `commit()` does.
`completion_decision(commit_requested, changed)` cancels an unchanged gesture,
so a drag that ends where it started leaves `undo_count()` unchanged.

### 10.2 Preflight-then-mutate ordering

Both `retime_keyframes()` and `offset_keyframe_scalars()` follow the same shape,
which is what makes rollback exact:

```text
1. validate arguments (null project, empty selectors, non-finite delta)
2. ProjectData candidate = *project            // full copy, nothing shared
3. resolve every selector against `candidate`; reject duplicates
4. reject every unsupported (kind, component) pairing
5. compute the single applied delta (snap + neighbour clamp, or group clamp)
6. write every key into `candidate`; validate finiteness and float32 range
7. only on total success: *project = std::move(candidate)
```

No step between 2 and 6 can leave a partial write in `*project`, because every
write targets `candidate`. Step 7 is a single move.

### 10.3 Rollback across the shell

A gesture-level failure — a hard collision, a non-finite mapped value, a lost
key identity, a failed materialization, a failed `refresh_runtime()`, or an
invalidated context — calls the gesture's `finish(..., commit = false)` path,
which calls `EditTransaction::cancel()`. Cancel restores the project, the
runtime `SkeletonData`, the preview controller state, and the playback state
captured when the transaction began, then bumps runtime/preview revision so
every consumer re-reads. `sync_shell_from_editor_session(state)` then refreshes
the shell mirrors.

The graph and the dopesheet are therefore byte-identical to gesture start
because both read the *same* restored data:

- the dopesheet lane reads `cached_timeline_tracks()`, which is keyed by runtime
  revision and rebuilds from the restored `SkeletonData`;
- the graph reads `cached_timeline_graph_projection()`, keyed by runtime
  revision, skeleton identity, animation name, and track ID, and therefore also
  rebuilds from the restored `SkeletonData`;
- neither view holds a runtime pointer or key index across frames.

The shell smoke proves this by comparing `serialize_project()` before the press
and after the cancel, plus `undo_count()`, `redo_count()`, `project_revision()`,
`dirty()`, the rebuilt dopesheet `TrackRow::key_times`, and the rebuilt graph
`Key::values` for every component of every key.

### 10.4 Selection and `active_key` stability

- **During a Value drag.** Key times do not change, so every `TimelineKeyRef`
  is bit-identical for the whole gesture. `selected_keys` and `active_key` are
  never rewritten.
- **During a Time drag.** `apply_timeline_retime_delta()` rebuilds both from the
  same key indices after each applied delta, so the identity under the cursor
  survives even though the time component of the ref changes.
- **After cancel.** Both paths restore the pre-gesture project; the graph and
  dopesheet key sets return to their original times, so the pre-gesture refs
  resolve again. The value path additionally never changed them.
- **Across undo/redo of a committed Value edit.** Times are unchanged by the
  edit, so the selection and `active_key` survive undo and redo intact.
- **Across undo of a committed Time edit.** Undo restores the original times,
  so the post-commit refs no longer resolve. The existing shared
  `reconcile_timeline_key_selection()` prunes them by the MAR-167 rule. This is
  pre-existing dopesheet behaviour, not a MAR-168 regression; MAR-168
  deliberately does not change it, because doing so would require history to
  carry shell selection. The shell smoke asserts the deterministic reconciled
  outcome rather than asserting bit-identity, and §14 records this as a
  non-goal.

### 10.5 Gesture exclusivity and cancel registration

- `TimelineGraphValueGesture` is added to `authoring_gesture_active()` in
  `shell_state.hpp` and to `cancel_authoring_gestures()` in `shell_core.cpp`.
  The header comment already requires those two lists to stay in step.
- `TimelineGraphPointDrag` (the candidate) holds no transaction and is therefore
  **not** added to `authoring_gesture_active()`. It is reset by
  `cancel_authoring_gestures()` and by the graph body whenever the left button
  is released.
- Both fields live directly in `TimelineEditorState`, so
  `TimelineEditorState{}` source adoption clears them atomically, exactly as
  MAR-167 does for `graph_view` and `graph_cache`.
- `update_timeline_retime_gesture()` (dopesheet) returns early while
  `view_mode == TimelineViewMode::Graph`, and the graph driver returns early in
  Dopesheet mode, so exactly one surface owns a live retime gesture.
- Leaving the Graph tab, or a `requested_view_mode` transition away from Graph,
  cancels any live graph candidate or gesture before the tab body changes.

## 11. UI-Free API

### 11.1 `src/editor/timeline_graph_model.hpp` (new declarations)

```cpp
enum class DragAxis : std::uint8_t {
    Undecided,
    Time,
    Value,
};

double time_at_x(PlotRect rect, const View& view, double x);
double value_at_y(PlotRect rect, const View& view, double y);
double x_at_time(PlotRect rect, const View& view, double time_seconds);
double y_at_value(PlotRect rect, const View& view, double value);

DragAxis decide_drag_axis(
    double press_x,
    double press_y,
    double pointer_x,
    double pointer_y,
    double dead_zone_pixels = 4.0);

std::optional<double> drag_time_delta(
    const View& view,
    double press_x,
    double pointer_x);

std::optional<double> drag_value_delta(
    const View& view,
    double press_y,
    double pointer_y);
```

### 11.2 `include/marrow/editor/authoring.hpp` (additive)

```cpp
enum class TimelineScalarComponent : std::uint8_t {
    Angle,
    X,
    Y,
    Red,
    Green,
    Blue,
    Alpha,
};

struct TimelineScalarOffsetResult : AuthoringResult {
    double applied_delta{0.0};
    std::size_t key_count{0U};
};

/**
 * @brief Atomically offsets one scalar component of persisted timeline keys.
 *
 * Every selector must resolve to a Transform or Slot Color key whose family
 * supports `component`. Slot Color deltas are clamped group-wide into [0, 1];
 * Angle, X, and Y are unclamped. Rotate angles are setup-relative in the
 * project and absolute in the graph, but a delta is identical in both spaces,
 * so no setup-pose conversion occurs. Times and interpolations are never
 * written. A rejected edit leaves the project unchanged.
 */
TimelineScalarOffsetResult offset_keyframe_scalars(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    TimelineScalarComponent component,
    double requested_delta);
```

### 11.3 `src/editor/timeline_controller.hpp` (shell, ImGui-free)

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

### 11.4 `src/editor/shell_timeline_graph.hpp` (shell, ImGui-free entry points)

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

/**
 * @brief Advances or terminates the live graph drag from sampled input.
 * @return true while a candidate or gesture remains live.
 */
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

These take only plain scalars, so `shell_smoke_graph.cpp` drives complete drag
gestures headlessly without an ImGui frame.

### 11.5 `src/editor/shell_state.hpp` (transient shell state)

```cpp
struct TimelineGraphPointDrag {
    std::uint32_t item_id{0U};
    timeline_graph_model::DragAxis axis{timeline_graph_model::DragAxis::Undecided};
    std::string track_id;
    TimelineKeyRef pressed_key;
    timeline_graph_model::Component component{timeline_graph_model::Component::Angle};
    std::size_t component_index{0U};
    double press_pointer_x{0.0};
    double press_pointer_y{0.0};
    double press_time_seconds{0.0};
    double press_value{0.0};
    timeline_graph_model::View frozen_view{};
    timeline_graph_model::PlotRect frozen_plot{};
};

struct TimelineGraphValueGesture {
    std::uint32_t item_id{0U};
    std::string track_id;
    timeline_graph_model::Component component{timeline_graph_model::Component::Angle};
    std::vector<TimelineKeyRef> keys;
    std::vector<double> original_values;
    double applied_delta{0.0};
    bool materialized{false};
    bool changed{false};
    marrow::editor::EditorSession::EditTransaction transaction;
};
```

added to `TimelineEditorState` as
`std::optional<TimelineGraphPointDrag> graph_drag;` and
`std::optional<TimelineGraphValueGesture> graph_value_gesture;`.

### 11.6 What stays shell-private ImGui code

`draw_timeline_graph_body()` keeps only: sampling `io.MousePos`,
`ImGui::IsItemClicked`, `ImGui::IsMouseDown`, `io.KeyAlt`,
`ImGui::IsKeyPressed(ImGuiKey_Escape)`, and `ImGui::IsItemActive`; suppressing
zoom/pan/Fit while a drag is live; drawing the drag readout; and drawing the
shared `Snap` checkbox. It contains no arithmetic that maps pixels to units and
no project mutation.

### 11.7 Render statistics additions

`TimelineGraphRenderStats` gains, so the actual-frame smoke can aim a real mouse
at a real point and observe the gesture:

```cpp
float active_point_x{0.0f};
float active_point_y{0.0f};
bool active_point_valid{false};
float first_point_x{0.0f};
float first_point_y{0.0f};
bool first_point_valid{false};
bool drag_candidate_active{false};
bool value_gesture_active{false};
bool retime_gesture_active{false};
timeline_graph_model::DragAxis drag_axis{timeline_graph_model::DragAxis::Undecided};
```

## 12. Presentation

- The Graph toolbar gains a `Snap` checkbox bound directly to
  `state->timeline_editor.snap_to_frames`. It is the same field the Dopesheet
  tab edits; toggling it in either tab is visible in the other.
- The MAR-167 notice `"Value/time dragging is available in MAR-168"` is removed
  from `shell_timeline_graph.cpp`.
- While a drag is live the graph shows one readout line:
  - Time axis: `Time  <original> -> <current>  (delta <applied>s, <frames> f)`;
  - Value axis: `<ComponentLabel>  <original> -> <current>  (delta <applied>)`.
    Values use six decimals for Slot Color and three otherwise.
- The dragged component's points keep their component colour; selected parent
  keys keep the gold outline and the active component keeps its light centre
  mark, unchanged from MAR-167.
- The shared-easing notice from MAR-167 is unchanged and still displayed.
- Status messages: the Time axis reuses the existing
  `"Retimed timeline key(s)"` / `"Cancelled timeline retime"` strings verbatim.
  The Value axis adds `"Edited graph key value"`, `"Edited graph key values"`,
  `"Cancelled graph value edit"`, and `"Graph value edit failed"`.

## 13. Error Handling and Fail-Closed Rules

The graph refuses to start a drag when any of these holds, and cancels
mid-gesture when any becomes true:

| Condition | Result |
| --- | --- |
| No project, no animation, or no resolvable focused row | no candidate; live gesture cancels |
| Projection status is not `Ready` | no candidate; live gesture cancels |
| `!timeline_track_is_editable(row)` | no candidate |
| `authoring_gesture_active(*state)` | no candidate |
| Pressed key not in `selected_keys` after activation | no candidate |
| Unsupported (track kind, component) pairing | no candidate; primitive rejects |
| Non-finite pointer, view scale, or mapped delta | cancel with rollback |
| Value outside finite float32 range | cancel with rollback |
| Lost `TimelineKeyRef` identity | cancel with rollback |
| Materialization failure | cancel with rollback |
| `retime_keyframes()` / `offset_keyframe_scalars()` error | cancel with rollback |
| `refresh_runtime()` failure | cancel with rollback |
| Escape, window focus loss, editor shutdown, tab switch, project reload | cancel with rollback |

Neighbour collision that only *clamps* is not a failure. It stops the point at
the boundary and the drag continues, exactly as in the dopesheet.

## 14. Non-Goals

- Bezier handles, easing kind changes, curve presets, or automatic handles.
- Value-axis snapping of any kind.
- Diagonal (simultaneous time and value) editing.
- Graph point insertion, deletion, clipboard, or box selection.
- Time scaling around a pivot, or preview playback speed.
- Editing FFD, Inherit, Attachment, Draw Order, or Event lanes from the graph.
- Any Agent or MCP operation, dry-run, or descriptor. The registry stays at
  exactly 56 operations.
- Restoring shell key selection across undo of a *time* edit. That is a
  pre-existing shared dopesheet behaviour; changing it would require history to
  carry shell selection and is out of this story's vertical slice.
- Persisting graph view, drag, or snap-bypass state. `snap_to_frames` remains
  the existing transient `TimelineEditorState` field.
- Manual-visible-UI, Windows 11, or physical-input qualification credit.
  MAR-192 through MAR-210 stay open.

## 15. Decisions Taken Under Ambiguity

1. **Axis lock instead of free 2-D dragging.** Acceptance criterion 1 separates
   vertical and horizontal effects but does not say whether they can combine. A
   dominant-axis lock at gesture start was chosen because it makes the criterion
   provable (see §6.2), keeps frame snapping from firing during a value-only
   drag, and keeps each gesture bound to exactly one authoring primitive so the
   rollback proof in §10.2 stays a single-path argument.
2. **Vertical drag moves the whole selection, not just the pressed key.**
   "Only the active scalar component" constrains the *component*, not the key
   set. Moving every selected key on the focused track by one shared delta is
   symmetric with the horizontal path (which already moves the whole selection
   through `retime_keyframes()`), matches the behaviour of comparable graph
   editors, and keeps "one drag creates one transaction" honest for a
   box-selected group. Selected keys on other tracks are ignored because the
   graph displays one track at a time.
3. **Delta semantics rather than absolute value assignment.** A delta preserves
   the shape of a multi-key selection, and it removes the setup-pose conversion
   from the Rotate path entirely, which eliminates a class of round-tripping
   errors between the graph's absolute angle and the project's setup-relative
   angle.
4. **Group-wide RGBA clamp rather than per-key clamp.** A per-key clamp would
   let a multi-key drag flatten against the boundary and silently destroy the
   authored relationship between keys. The group clamp stops all keys together.
   The `upper < lower` degenerate case yields a zero delta rather than an error
   so imported out-of-range data does not block editing.
5. **The value primitive lives in `authoring.hpp` next to `retime_keyframes()`.**
   Placing it in an editor-internal header would have avoided a public-header
   change, but it belongs with the other project-domain timeline primitives so
   MAR-169/MAR-173 can reuse it. The change is strictly additive and touches no
   C ABI, file format, or agent surface.
6. **The graph tab gets its own `Snap` checkbox bound to the shared field.**
   MAR-167 moved `Snap to Frames` inside the Dopesheet tab, which would leave a
   graph-only user unable to see or change the setting their drags obey.
   Exposing the same field, rather than a graph-private copy, satisfies "reuses
   common frame snap" literally.
7. **The view transform is frozen during a drag.** Allowing zoom mid-drag would
   make the pixel-to-unit mapping time-varying and untestable, and a
   revision-triggered auto-fit could teleport the dragged point. Freezing costs
   nothing a user wants during a 200 ms drag.
8. **The playhead does not follow the drag.** Calling `scrub_timeline_time()`
   each frame would re-seek and refresh preview in competition with the
   transaction's `refresh_runtime()`, and the dopesheet retime does not move the
   playhead either.
9. **Tie in the axis comparison goes to Value.** `|dx| == |dy|` is rare but must
   be deterministic. Value wins because time editing is already available in the
   dopesheet whereas value editing is unique to the graph.
10. **A press alone opens no transaction.** Deferring `begin_edit()` until the
    dead zone is left keeps MAR-167's click-to-select behaviour byte-identical
    and guarantees that a click can never produce an empty history entry.

## 16. Validation Strategy

### 16.1 UI-free focused tests — `marrow_timeline_graph_model_tests`

- `time_at_x` / `x_at_time` and `value_at_y` / `y_at_value` round-trip within
  `1e-9` for representative views, including a negative `view_start_seconds` and
  a sub-unit `pixels_per_value`.
- `decide_drag_axis` returns `Undecided` inside the dead zone, `Time` for a
  dominant horizontal move, `Value` for a dominant vertical move, `Value` on an
  exact tie, and `Undecided` for every non-finite input.
- `drag_time_delta` and `drag_value_delta` produce the expected signed values,
  invert screen Y correctly, and return `std::nullopt` for non-finite input, a
  non-positive scale, and an overflowing quotient.
- The new mapping helpers produce the same coordinates the MAR-167 geometry
  builder produces for the same key, proving render and drag math agree.

### 16.2 UI-free focused tests — `marrow_project_smoke`

Direct `offset_keyframe_scalars()` coverage, mirroring where `retime_keyframes()`
is already tested:

- one case per lane family (Rotate Angle, Translate X, Translate Y, Scale X,
  Scale Y, Shear X, Shear Y, Slot Color R/G/B/A) asserting the target field
  moved by the delta and that every sibling field, the time, and the
  interpolation are unchanged;
- Rotate delta applied with no setup-pose conversion, verified against a bone
  with a nonzero setup rotation;
- Scale keeps a negative value negative and can reach exact zero;
- Slot Color group clamp: a two-key drag stops when the higher key reaches 1.0
  and both keys retain their difference; the reported `applied_delta` equals the
  clamped delta;
- the degenerate `upper < lower` case returns a zero delta and mutates nothing;
- rejection atomicity: an unsupported pairing, an unresolvable selector, a
  duplicated selector, a non-finite delta, and an out-of-float32-range result
  each leave `serialize_project()` byte-identical;
- save/reload round trip of an offset project, then
  `--export-runtime` / `--export-binary` and a JSON/MBIN comparison.

### 16.3 UI-free focused tests — `marrow_timeline_model_tests`

- The graph value gesture reuses `incremental_retime_delta()` and
  `completion_decision()`; add cases proving an unchanged gesture yields
  `CompletionAction::Cancel` with zero history entries, and that a non-finite
  requested delta yields `std::nullopt`.

### 16.4 Headless shell smoke — `src/editor/shell_smoke_graph.cpp`

A new `validate_timeline_graph_edit_shell_smoke()` scenario with an isolated
session, registered in `shell_smoke_scenarios.hpp` and `shell_smoke.cpp`,
covering:

- a complete Value drag on Translate X: one history entry, `active_key` and
  `selected_keys` bit-identical throughout, Y and the key time unchanged,
  `applied_delta` matching the pixel delta divided by `pixels_per_value`;
- the same for Rotate Angle, Scale X (including reaching exact zero), Shear Y,
  and Slot Color Alpha;
- a multi-key Value drag preserving the difference between the two keys;
- the Slot Color group clamp stopping both keys together;
- a Time drag with `snap_to_frames = true` landing on a frame boundary, and the
  Alt-bypass path landing off it;
- a Time drag clamped by an unselected neighbour, asserting the 1 ms spacing and
  that the drag continues rather than failing;
- explicit-duration auto-grow: an animation with an authored duration grows in
  the same transaction and the same undo entry when a key is dragged past it;
- cancel paths — Escape, `cancel_authoring_gestures()`, an unsupported focused
  row appearing mid-gesture, and a forced non-finite pointer — each restoring
  `serialize_project()`, `undo_count()`, `redo_count()`, `project_revision()`,
  `dirty()`, the rebuilt dopesheet `key_times`, and every rebuilt graph
  component value;
- press-without-motion producing no transaction and no history entry;
- a zero-net drag committing nothing;
- undo/redo of a committed Value edit restoring values and leaving selection and
  `active_key` intact;
- undo of a committed Time edit yielding the deterministic reconciled selection;
- `TimelineEditorState{}` source adoption clearing `graph_drag` and
  `graph_value_gesture`;
- `agent_operation_descriptor_count() == 56` before and after every case.

### 16.5 Actual-frame smoke — `src/editor/shell_smoke_frames.cpp`

Using real ImGui mouse events against real rendered point coordinates from
`TimelineGraphRenderStats`:

- press on the reported active point, move vertically past the dead zone, and
  assert `drag_axis == Value` and `value_gesture_active`;
- press and move horizontally, and assert `drag_axis == Time` and
  `retime_gesture_active`;
- assert wheel zoom, middle-drag pan, and `Fit` are inert while a drag is live;
- assert switching to the Dopesheet tab mid-drag cancels with rollback;
- assert the Dopesheet tab still retimes normally afterwards.

### 16.6 Display and compatibility gates

See the "Full verification checklist" in
`docs/superpowers/plans/2026-08-30-mar-168-graph-key-time-value-editing.md`.
Automated display tests prove the exercised ImGui/display path only. They add no
manual-visible-UI, Windows 11, or physical-input qualification credit.

## 17. Documentation and Milestone Closure

After code and every required gate pass:

- add a `MAR-168 Graph Key Time and Value Editing Validation Results` section to
  `AGENTS.md` following the existing table plus command-output format, and add
  any new validation command lines to `Current Validation`;
- update `docs/root1/discription.md`, `quick-start.md`, `concepts.md`,
  `editing-gap-analysis.md`, `refector.md`, and `platform-validation.md` where
  the "point dragging arrives in MAR-168" wording is now stale;
- mark `MAR-168` done with the verified completion date and leave `MAR-169`
  open;
- leave MAR-192 through MAR-210 open and add no platform qualification credit;
- change this document's status to `Implemented and validated` only after every
  gate is green.

## 18. Commercial-Tool Reference Boundary

The interaction direction is informed by the official Spine Graph and Live2D
Cubism Graph Editor documentation:

- Spine Graph: <https://us.esotericsoftware.com/spine-graph>
- Spine Dopesheet: <https://us.esotericsoftware.com/spine-dopesheet>
- Live2D Graph Editor: <https://docs.live2d.com/en/cubism-editor-manual/grapheditor/>

Marrow intentionally does not copy free 2-D point dragging, per-component key
ownership, or per-component Bezier handles. Runtime truth and Marrow's shared
parent-key contract take precedence over surface similarity to another editor.
