# MAR-173 Atomic Key Time Scaling Design

**Date:** 2026-08-30

**Status:** Implemented and validated (2026-08-30)

**Authority:** `.agents/tasks/prd-marrow-runtime.json` story `MAR-173` and
`docs/root1/discription.md`

**Depends on:** MAR-172, whose lane-level `loop_sync` flag, derived managed
boundary key, retime pinning rule, and session-seam
`synchronize_loop_boundaries()` are defined in
`docs/superpowers/specs/2026-08-30-mar-172-loop-boundary-key-sync-design.md`.
MAR-172 depends on MAR-171's `resolve_automatic_curves()` and its demotion rule
(`docs/superpowers/specs/2026-08-30-mar-171-project-local-automatic-curve-handles-design.md`),
which depends on MAR-170's preset table
(`docs/superpowers/specs/2026-08-30-mar-170-curve-presets-and-defaults-design.md`),
which depends on MAR-169's interpolation primitive
(`docs/superpowers/specs/2026-08-30-mar-169-graphical-bezier-handles-design.md`),
which inherits MAR-168's gesture, axis-lock, and transaction model
(`docs/superpowers/specs/2026-08-30-mar-168-graph-key-time-value-editing-design.md`).

> **MAR-172 is not implemented while this document is written.** Its spec and
> plan exist; `git log` shows `b19b7b5` (MAR-171) as the last feature commit.
> The registry today is **58** rows, *measured* by counting
> `kOperationSpecs[]` in `src/editor/agent_dispatch.cpp` on `b19b7b5` — not
> assumed. `src/editor/shell_smoke_graph.cpp` carries **five** count guards
> today (lines 146, 634, 1556, 2126, 3140), not the four MAR-172's spec
> recorded; MAR-171 added one. Every MAR-172 contract quoted below is quoted
> from its *spec*. The implementation plan's **Task 0 is mandatory** and must
> reconcile against the code that actually exists, re-measure the registry
> count, and re-count the guards. §4.5 records the specific MAR-172 facts
> MAR-173 depends on and what MAR-173 does if any landed differently.

## 1. Goal

Every other timing edit in Marrow is a *translation*: `retime_keyframes()` adds
one shared delta to a set of key times. An animator who has blocked a 0.5 s
gesture and now needs it to read as 0.8 s has no operation for that. Doing it
by hand means dragging each key in turn, which destroys the relative spacing the
blocking pass established, needs one undo entry per key, and is exactly the work
a scale operation exists to remove.

MAR-173 adds that operation. The current timeline key selection has a time
range `[t_min, t_max]`. Dragging one edge of that range moves it while the
**opposite edge stays fixed as the pivot**, and every selected key moves by

```text
t' = pivot + (t - pivot) * s
```

for one finite, positive ratio `s`. One drag is one transaction, one preview
stream, and one undo entry. The same ratio is available as one new Agent and MCP
operation, `timeline.scale_key_times`.

Concretely, MAR-173 adds exactly five things and nothing else:

1. One new project-domain primitive, `scale_keyframe_times()`, whose contract is
   **reject, not clamp** (§6) — the one place MAR-173 deliberately departs from
   `retime_keyframes()`.
2. Four pure helpers in `timeline_model` that derive a ratio from a pointer, snap
   it through the existing shared frame-snap helper, and compose it incrementally
   (§7).
3. One dopesheet **selection range bar** with two edge grips, driven by the
   MAR-168 candidate/gesture split so the whole drag is testable headlessly
   (§9).
4. One Agent operation, `timeline.scale_key_times`, and its matching MCP tool,
   taking the registry from MAR-172's 59 to exactly **60** (§12).
5. One behaviour-preserving extraction of the `timeline.retime_keyframes`
   selector parser so both operations share it (§12.2), and one
   behaviour-preserving extraction of MAR-172's loop-sync pin predicate so
   scaling *inherits* the rule rather than reimplementing it (§8.5).

No file format, no C ABI, and no `.marrow` schema field changes. Scaling writes
`keyframe.time` and nothing else.

## 2. Approved Product Decisions

The following decisions are fixed for MAR-173:

1. **The pivot is not a free parameter.** It is always the opposite edge of the
   current selection's own time range, computed by the primitive from the
   resolved selector times. Criterion 1 says "uses the opposite edge as a fixed
   pivot"; passing a caller-chosen pivot time would make that criterion
   unenforceable. The API therefore takes an enum naming *which edge stays
   fixed*, never a time (§5.1).
2. **Only finite, strictly positive ratios are accepted.** `s <= 0`, `s = 0`,
   and non-finite `s` are errors with no mutation. Time reversal is **rejected**,
   not supported — criterion 1's words are "finite positive scale ratios only"
   (§6.2, §16).
3. **Collisions reject; they do not clamp.** This is the one place MAR-173
   departs from `retime_keyframes()`, and criterion 2 states it directly: "non-
   event time collisions and intrusion into unselected neighboring keys reject
   the entire operation." A clamped scale is not the scale the user asked for;
   a clamped translation is (§6.4, §16 decision 2).
4. **A rejected frame does not end a live gesture.** The *operation* is
   all-or-nothing; the *gesture* holds its last accepted state, reports the
   reason in its readout, and stays alive. Cancelling a 200 ms drag because the
   pointer crossed a collision on its way somewhere legal would be hostile and
   is not what "reject the entire operation" means (§10.2).
5. **Event ties are preserved unconditionally, and a partial tie is rejected.**
   Two keys at the same time on an event timeline map to the same target because
   the map is a function of time — this is a theorem, not a defensive loop
   (§6.5). A selection that names one member of a tie but not the other would
   split it, so the primitive rejects it by name (§6.5, §16 decision 4).
6. **Frame snapping reshapes the ratio, never the written times.** The moved
   edge's target time is snapped through the existing shared
   `timeline_model::snap_delta_to_frames()`, and the ratio is re-derived from the
   snapped target. Interior keys land wherever the ratio puts them. Quantizing
   every key would break the incremental composition and is not what a scale
   means (§7.2, §16 decision 3).
7. **Scaling can grow an explicit duration and can never shrink one.** Growth is
   the session's existing `auto_extend_explicit_animation_durations()` inside the
   same transaction. MAR-173 writes no `AnimationEdit` of its own (§8.3).
8. **The view transform is frozen for the duration of a drag**, exactly as
   MAR-168 §2.8 freezes the graph view, and for the same reason.
9. **The selection range bar lives in the Dopesheet tab only.** The Graph tab
   keeps MAR-168's point drag (§15).
10. **MAR-173 does not restore shell selection across undo of a committed scale.**
    That is the pre-existing shared behaviour MAR-168 §14 recorded; changing it
    would require history to carry shell selection (§10.5, §15).
11. Preview playback speed (MAR-174) remains out of scope.

## 3. Scope

### 3.1 Surfaces that gain scaling

| Surface | Target | Mechanism |
| --- | --- | --- |
| Dopesheet selection range bar | the current `selected_keys` | drag either edge grip; ratio from pointer |
| Agent `timeline.scale_key_times` | the caller's explicit key selectors | absolute `scale` + `pivot` |
| MCP `timeline.scale_key_times` | same | same |

That is the complete list.

### 3.2 Supported timeline families

Every family `retime_keyframes()` supports, because scaling writes exactly the
same field through the same resolver:

| Family | `TimelineKeyKind` | Minimum separation |
| --- | --- | --- |
| Bone Rotate / Translate / Scale / Shear | `Transform` | `kNonEventKeySpacing` (1 ms) |
| Mesh Deform (FFD) | `Deform` | `kNonEventKeySpacing` |
| Slot Color | `SlotColor` | `kNonEventKeySpacing` |
| Slot Attachment | `SlotAttachment` | `kNonEventKeySpacing` |
| Draw Order | `DrawOrder` | `kNonEventKeySpacing` |
| Event | `Event` | `0.0` (ties are legal) |

The spacing column is **the same table** `include_resolved_retime_bounds()`
already applies (`authoring.cpp:478-536`); §8.4 factors it into one shared
definition so the two rules cannot drift.

### 3.3 Explicit exclusions

MAR-173 does not add:

- preview playback speed, presets, or reverse composition (MAR-174);
- **value** scaling of any kind. MAR-173 scales times; MAR-168's
  `offset_keyframe_scalars()` remains the only value writer;
- a pivot at the playhead, at a numeric anchor, or at any position other than the
  opposite edge of the selection (§16 decision 1);
- negative ratios, time reversal, or key-order inversion (§16 decision 5);
- scaling from the Graph tab (§15);
- a numeric scale entry box in the GUI. The ratio's GUI source is the drag; the
  scripted source is the Agent argument. A third source would need its own
  validation surface and no criterion asks for one;
- scaling across animations. Every selected key already belongs to the selected
  animation because `selected_keys` is rebuilt per animation, and the primitive
  rejects a selector set naming more than one animation (§6.3);
- box selection, insertion, deletion, or clipboard changes;
- any change to `retime_keyframes()`, `offset_keyframe_scalars()`,
  `set_keyframe_interpolation()`, `set_keyframe_curve_mode()`,
  `resolve_automatic_curves()`, `set_timeline_loop_sync()`,
  `synchronize_loop_boundaries()`, `insertable_key_time()`, or
  `clamp_existing_key_time()` **signatures or behaviour**;
- an actual-frame (`shell_smoke_frames.cpp`) scenario. Criterion 6 names
  "Project, shell, agent, and MCP smokes" and does not name frames, unlike
  MAR-168's criterion set. §17.6 records what an actual-frame case would cover
  and why it is deferred;
- manual-visible-UI, Windows 11, or physical-input qualification credit.
  MAR-192 through MAR-210 stay open.

### 3.4 Compatibility boundaries

MAR-173 changes none of the following:

- **`.marrow` schema.** Scaling rewrites existing `time` numbers in existing
  keyframe objects. No new field, no new block, no new version;
- **`.mskl` v1** and **`.mbin` v2**. Unchanged reader and writer;
- **C ABI v1.** `include/marrow/c_api/**` and `src/c_api/**` are untouched;
- **`editor-settings.json` v1.** `kEditorSettingsVersion` stays `1`;
  `src/editor/preferences.cpp` and `include/marrow/editor/preferences.hpp`
  change by zero bytes;
- **`src/runtime/**` and `include/marrow/runtime/**`** in their entirety;
- `SelectionSet` entity identity;
- MAR-168's point drag and value primitive, MAR-169's handle geometry and
  clamp/reject split, MAR-170's preset constants and preference flow, MAR-171's
  resolver and demotion rule, MAR-172's flag, contract, and pinning rule.

The public-header changes are **additive only**:
`include/marrow/editor/authoring.hpp` gains one enum, one result struct, and one
function appended after MAR-172's declarations. No existing declaration is
modified or removed.

Two **behaviour-preserving extractions** are made, each proved by an existing
test that must pass unchanged:

- the `timeline.retime_keyframes` selector-parsing loop moves into
  `timeline_key_selectors_arg()`, parameterized by the operation label so every
  existing retime error string stays byte-identical (§12.2);
- MAR-172's loop-sync pin condition moves out of the inline block inside
  `include_resolved_retime_bounds()` into a file-local
  `resolved_key_is_loop_pinned()` used by both retime and scale (§8.5).

## 4. Existing Boundaries Reused

### 4.1 Reuse table

| Concern | Reused primitive | Location |
| --- | --- | --- |
| Parent-key identity | `TimelineKeyRef`, `timeline_key_ref()`, `timeline_key_index()` | `timeline_model` |
| Selection / active key | `apply_key_activation()`, `reconcile_selection()` | `timeline_model` |
| Key time epsilon and non-event spacing | `kKeyTimeEpsilon` (1e-6), `kNonEventKeySpacing` (0.001) | `timeline_model.hpp:20-21` |
| Frame snapping | `snap_delta_to_frames()` | `timeline_model.cpp:409` |
| Gesture completion / empty-history suppression | `completion_decision()` | `timeline_model.cpp:433` |
| Project-domain key resolution | `TimelineKeySelector`, `resolve_timeline_key()`, `matching_key_index()` | `authoring.cpp:158` |
| Post-write re-sort | `sort_retimed_timelines()` | `authoring.cpp:596` |
| Per-family key spacing | the `kNonEventKeySpacing` / `0.0` split inside `include_resolved_retime_bounds()` | `authoring.cpp:478` |
| Loop-sync pinning | MAR-172's first/last rule inside `include_resolved_retime_bounds()` | `authoring.cpp` (MAR-172) |
| Explicit-duration auto-grow | `auto_extend_explicit_animation_durations()` | `authoring.cpp:1670` |
| Loop-boundary maintenance | `synchronize_loop_boundaries()` at the session seam | `session.cpp` (MAR-172) |
| Automatic curve resolution | `resolve_automatic_curves()` / shell `resolve_timeline_auto_curves()` | `authoring`, `timeline_controller.cpp:1400` |
| Overlay materialization | `visit_editable_timeline_keys()`, `ensure_*_timeline_edit_index()` | `timeline_controller` |
| Live preview / one history entry | `EditorSession::EditTransaction` | `session.cpp` |
| Gesture cancel on focus loss / shutdown | `cancel_authoring_gestures()` | `shell_core.cpp:431` |
| Gesture mutual exclusion | `authoring_gesture_active()` | `shell_state.hpp:803` |
| Candidate-then-gesture drag split | MAR-168's `TimelineGraphPointDrag` / `TimelineGraphValueGesture` | `shell_state.hpp`, `shell_timeline_graph.cpp` |
| Agent dry-run / commit / no-change shape | `commit_or_error()`, `CommitPolicy` | `agent_dispatch_internal.hpp` |
| Agent error classification | `classify_timeline_key_error()` | `agent_handlers_editing.cpp:232` |
| Agent auto-curve resolve | `resolve_agent_auto_curves()` | `agent_handlers_editing.cpp` |
| Agent selector cap | the 4096 cap on `timeline.retime_keyframes` | `agent_handlers_editing.cpp:754` |
| MCP selector schema | `_timeline_retime_key_schema()` | `tools/mcp/tools/editing.py:190` |

MAR-173 introduces exactly two new concepts — the scale ratio with its pivot,
and the whole-target-set validation — and reuses everything else.

### 4.2 Why `retime_keyframes()` is not extended instead

`retime_keyframes()` applies **one shared delta** to every selector. Scaling
applies a **different delta per key**, proportional to the key's distance from
the pivot. The two also differ on their central behaviour: retime *clamps*
against neighbours and only errors on inconsistent bounds, duplicate selectors,
or unresolvable keys; scaling *rejects* on any collision (criterion 2). Adding a
`scale` parameter to `retime_keyframes()` would mean one function with two
incompatible collision policies and a delta that is meaningless in the scale
case. Every existing retime caller — MAR-168's graph time drag, the dopesheet
retime gesture, the Agent's `timeline.retime_keyframes` — would take on the risk
of a change it does not use.

What *is* shared: the selector type, the resolver, the duplicate check, the
per-family spacing table, the loop-sync pin, the post-write sort, the
candidate-copy/single-move shape, and the whole transaction and gesture
lifecycle. §8 shows every one of those being reused rather than re-typed.

### 4.3 What deliberately does *not* change

- `retime_keyframes()`'s signature, clamping behaviour, error set, and status
  strings. Every existing retime test passes unchanged.
- `include_retime_bounds()` and `include_resolved_retime_bounds()`. Scaling does
  not call them — a bound is a translation concept — but §8.5's extraction leaves
  their behaviour bit-identical, and MAR-172's pinning tests prove it.
- `insertable_key_time()` and `clamp_existing_key_time()`. Their `duration`
  parameter stays `(void)`-ignored.
- `timeline.describe`'s payload, `timeline.retime_keyframes`'s arguments and
  response, `timeline.set_interpolation`, `timeline.set_curve_mode`, and
  MAR-172's `timeline.set_loop_sync`.
- `TimelineEditorState::snap_to_frames` and `::frames_per_second` stay the
  existing shared transient fields.

### 4.4 The MAR-168 defect pattern, and how MAR-173 is designed against it

MAR-168 and MAR-169 both shipped a criterion naming export while their tests
mutated `ProjectData` **copies** that never reached the exporter, and `AGENTS.md`
recorded PASS both times. The tell was an exported byte size identical to the
14336 baseline. MAR-170 and MAR-171 got it right, and MAR-171's export check
caught a genuine format-leak bug.

MAR-173's criterion 6 names JSON/MBIN export, so §17.6 requires the mutated
project to be exported and a **specific serialized time value** asserted from the
loaded `.mskl`. But MAR-173 has an honest complication a reviewer must not
mis-read, and it is stated here rather than buried:

> **A pure retime can legitimately leave the exported size unchanged.** `.mbin`
> encodes each key time as a fixed-width `float32`, so scaling changes the bytes
> without changing the count. Even `.mskl` can keep its size when the new
> numeral has the same width as the old.

The acceptance signal is therefore the **loaded value**, not the size. §17.6
picks a ratio (`s = 1.25` about pivot `0.0`, turning `{0.0, 0.5, 1.0}` into
`{0.0, 0.625, 1.25}`) that *does* widen the JSON numerals so the JSON size still
moves and remains a usable secondary signal — while stating explicitly that an
unchanged `.mbin` size is **correct here** and is not the MAR-168/169 defect
recurring. The `.mbin` is instead proved by `validate_binary_export()` and by
`marrow_inspect --compare` against the `.mskl`.

### 4.5 MAR-172 facts MAR-173 depends on, and the fallback if they differ

| MAR-172 fact | MAR-173 use | If it landed differently |
| --- | --- | --- |
| `TransformTimelineEdit` / `SlotColorTimelineEdit` / `MeshDeformTimelineEdit` carry `bool loop_sync` | the pin predicate reads it (§8.5) | if MAR-172 is not merged at all, **stop and report** — MAR-173 `dependsOn` it |
| `include_resolved_retime_bounds()` pins index `0` and `size()-1` on an opted-in lane | extracted into `resolved_key_is_loop_pinned()` and reused by scaling | if MAR-172 implemented pinning elsewhere, extract from wherever it is; if MAR-172 shipped without pinning, drop §8.5 and its tests and record it in the plan |
| `synchronize_loop_boundaries()` runs at the session seam after `auto_extend_...` | scaling inherits boundary maintenance with zero wiring (§8.6) | if the seam is elsewhere, wire nothing new — record where it is and assert the same end state |
| The registry is 59 | MAR-173 makes it 60 | use the count Task 0 **measures**, plus one |
| `shell_smoke_graph.cpp` carries N count guards | all N become the new total | Task 0 re-counts; the number is **six** if MAR-172 added one to today's five |
| `timeline.set_loop_sync` sits after `timeline.set_curve_mode` | MAR-173's row goes immediately after it | insert after whatever the last `timeline.*` row is, keeping them contiguous |

## 5. Architecture

```text
ImGui selection range bar (shell_timeline.cpp)
        | plain doubles / bools only
        v
scale drag driver (shell_timeline.cpp, ImGui-free entry points)
        |
        v  ratio from pointer, snapped through the shared frame-snap helper
timeline_model::scale_from_edge_time / snap_scale_to_frames / incremental_scale_ratio
        |
        v
timeline_controller scale gesture
  begin_timeline_scale_gesture / apply_timeline_scale_ratio / finish_timeline_scale_gesture
        |                                          |
        v                                          v
scale_keyframe_times()                    resolve_timeline_auto_curves()   (MAR-171)
        |                                          |
        \________ EditorSession::EditTransaction __/
                      refresh_runtime()
                        -> auto_extend_explicit_animation_durations()
                        -> synchronize_loop_boundaries()                   (MAR-172)
                        -> build_project_runtime()  (preview)
                      commit() -> exactly one history entry


Agent / MCP
    timeline.scale_key_times  ->  timeline_key_selectors_arg()  (shared with retime)
                              ->  scale_keyframe_times()        (the same primitive)
                              ->  resolve_agent_auto_curves()
                              ->  commit_or_error()
```

New ratio math lives in `timeline_model` (UI-free, unit tested). The primitive
lives in `authoring` beside `retime_keyframes()`. Gesture lifecycle lives in
`timeline_controller` (ShellState-aware but ImGui-free, so the headless shell
smoke drives complete drags). Only pointer/keyboard sampling lives in
`shell_timeline.cpp`.

## 6. The Scaling Operation

### 6.1 The pivot and the formula

Let the resolved selectors have original times `t_0 … t_{n-1}` (the times stored
in the project at the moment of the call), and let

```text
t_min = min t_i        t_max = max t_i        span = t_max - t_min
```

| `pivot` | Meaning | `p` | Which edge moves |
| --- | --- | --- | --- |
| `TimelineScalePivot::RangeStart` | the selection's **earliest** time stays fixed | `t_min` | the late edge |
| `TimelineScalePivot::RangeEnd` | the selection's **latest** time stays fixed | `t_max` | the early edge |

Every selected key's target is

```text
t'_i = p + (t_i - p) * s
```

computed in `double`. The primitive performs **no quantization**: written times
are `double` in `ProjectData`, and `float32` narrowing happens where it already
happens, at runtime build and export.

**The pivot key never moves, bit for bit.** For the key (or keys) whose
`t_i == p`, the expression is `p + 0.0 * s`. For any finite `s`, `0.0 * s` is
`±0.0`, and `p + ±0.0 == p` for every `p >= 0.0` including `p == 0.0`. This is an
IEEE-754 identity, not a tolerance, and §17.1 asserts it directly. It is what
makes the incremental composition in §7.3 sound: the pivot the primitive
recomputes on frame *k+1* is bit-identical to the one it computed on frame *k*.

`span` is required to exceed `kKeyTimeEpsilon`. A **single-key selection**, and
any selection whose keys all share one time, therefore rejects with
`"Scaling requires a selection spanning at least two distinct key times."` The
GUI never offers a range bar for such a selection (§9.2), so this path is
reachable only from the Agent.

### 6.2 The ratio's domain

| `s` | Result |
| --- | --- |
| non-finite (`NaN`, `±inf`) | error, no mutation: `"Timeline scale ratio must be finite and positive."` |
| `s <= 0.0` (including `-0.0` and `0.0`) | same error |
| `\|s - 1\| <= 1e-12` | **no-op**: `changed == false`, empty error, no history entry |
| every target within `1e-12` of its original | **no-op**: same |
| otherwise | the operation proceeds to §6.3 |

The two no-op guards are separate on purpose. The first catches the exact
identity ratio cheaply. The second catches a ratio near 1 applied to a tiny span,
where every target rounds back onto its original. `1e-12` s is one picosecond,
six orders of magnitude below `kKeyTimeEpsilon`, and is the same tolerance
`retime_keyframes()` already uses for its zero-delta guard (`authoring.cpp:1806`).

A no-op returns `AuthoringResult{changed = false, error = ""}`, which is
`operator bool() == true`. The gesture's `completion_decision(commit, changed)`
then cancels rather than committing, so **`s = 1` provably produces no history
entry** (§17.3, §17.5).

`s < 0` is rejected rather than implemented. Criterion 1 says "finite positive
scale ratios only". Time reversal on a *subset* of a timeline is also not
expressible without reversing key order, which would invert every outgoing
easing's meaning (a key's `interpolation` describes the segment *leaving* it) and
would need an easing-permutation rule no criterion asks for. §16 decision 5
records this.

### 6.3 Preflight order

```text
 1. validate arguments: null project; empty selectors; non-finite or
    non-positive `s`
 2. ProjectData candidate = *project              // full copy, nothing shared
 3. resolve every selector against `candidate` through the shared
    resolve_timeline_key(); reject a selector with an empty animation name or a
    non-finite/negative time; reject duplicates by (kind, timeline_index,
    key_index)
 4. reject a selector set naming more than one animation
 5. reject when any resolved key is loop-sync pinned            (§8.5)
 6. compute p and span from the resolved ORIGINAL times; reject span <= eps
 7. compute every target t'_i from the ORIGINAL times           (§6.1)
 8. reject unless every target is finite, >= 0, and inside the float32 range
 9. validate the WHOLE projected key set of every affected timeline (§6.4, §6.5)
10. if nothing moved by more than 1e-12: return a no-change result
11. write every target into `candidate`, reading resolved.original_time, never
    the live value
12. sort_retimed_timelines(&candidate, resolved)                 // stability no-op
13. *project = std::move(candidate)                              // one move
```

**No step between 2 and 12 can leave a partial write in `*project`**, because
every write targets `candidate`. Step 13 is a single move. This is exactly the
shape `retime_keyframes()`, `offset_keyframe_scalars()`,
`set_keyframe_interpolation()`, and MAR-172's `set_timeline_loop_sync()` all use.

**The ordering hazard is removed by construction, not by care.** Step 7 computes
every target from `resolved[i].original_time`, a snapshot taken in step 3 before
any write. Step 11 writes
`keyframes[key_index].time = p + (resolved.original_time - p) * s`, mirroring
`apply_resolved_retime()`'s existing shape (`authoring.cpp:544-549`), which also
reads its snapshot rather than the live value. So no write can observe another
write, no two orderings of step 11 produce different results, and a transient
mid-write state — two selected keys briefly at the same time while the third has
not moved yet — is never observed by any validation, because all validation
happened in step 9 against the fully projected set.

Step 12 is called for defence in depth and is provably a no-op: step 9
guarantees the projected sequence is non-decreasing in original index order, so
the `stable_sort` cannot reorder anything. §17.1 asserts the key order is
unchanged after a scale.

### 6.4 The whole-target-set validation

Step 9 groups the resolved keys by `(kind, timeline_index)`. For each affected
timeline it builds the **projected list** over that timeline's *entire*
`keyframes` vector — not just the selected keys:

```text
projected[j].time     = selected(j) ? p + (t_j - p) * s : t_j
projected[j].selected = whether index j is in this call's selector set
```

Indices are already sorted ascending by time in every stored timeline. For each
adjacent pair `(j, j+1)`:

```text
spacing        = family_key_spacing(kind)          // 1 ms, or 0.0 for Event
original_gap   = t_{j+1} - t_j
required_gap   = min(spacing, original_gap)
projected_gap  = projected[j+1].time - projected[j].time

reject unless projected_gap >= required_gap - kKeyTimeEpsilon
```

**Why `min(spacing, original_gap)` and not `spacing` flat.** A stored timeline
can already carry a gap below 1 ms — an imported skeleton, a hand-edited
`.marrow`, or a MAR-172 boundary adopted right next to a neighbouring key. A flat
`>= spacing` rule would make *every* scale on such a timeline impossible even
when the scale does not make the gap worse. The `min` form says exactly what is
meant: **a gap that satisfied the spacing must still satisfy it, and a gap that
was already tighter must not get tighter.** It never blocks a legal project and
it still catches every real collision:

| Case | `original_gap` | `projected_gap` | Verdict |
| --- | --- | --- | --- |
| Two selected keys, `s > 1` | `g` | `g·s > g` | pass |
| Two selected keys, `s < 1`, `g·s < 1 ms <= g` | `g` | `g·s` | **reject** — collision |
| Two selected keys, `g = 0.4 ms`, `s = 0.9` | `0.4 ms` | `0.36 ms` | **reject** — got tighter |
| Two selected keys, `g = 0.4 ms`, `s = 1.5` | `0.4 ms` | `0.6 ms` | pass |
| Selected key moving toward an unselected neighbour | `g` | `< 1 ms` | **reject** — intrusion |
| Selected key crossing an unselected neighbour | `g` | negative | **reject** — intrusion |
| Unselected pair, untouched | `g` | `g` | pass |

The last two rows are criterion 2's "intrusion into unselected neighboring keys".
Because the projected list spans the whole timeline, the check covers a selected
key colliding with an unselected one on *either* side with one rule, and needs no
notion of "the nearest unselected neighbour" at all.

The rejection message names the animation, the family and lane, both times, and
which key is selected, e.g.

```text
Scaling would place animation 'idle' transform key 'spine/rotate' at 0.500500 s,
0.000500 s from the unselected key at 0.501000 s; the minimum separation is
0.001000 s.
```

### 6.5 Event ties

**Preservation is a theorem.** Two keys on the same event timeline with
`t_a == t_b` produce `p + (t_a - p) * s` and `p + (t_b - p) * s` from
bit-identical inputs through the same expression, so the results are bit-identical
doubles. Ties are preserved exactly, with no tolerance and no special-casing.
§17.1 asserts it on a two-key tie and a three-key tie.

`family_key_spacing(Event) == 0.0`, so `required_gap` for an original tie is
`0.0` and the projected tie passes. It is also legal for two originally distinct
event keys to become tied — retime already allows an event key to land on an
unselected one (`include_resolved_retime_bounds()` passes `0.0` for
`TimelineKeyKind::Event`, `authoring.cpp:512-519`) and criterion 2 restricts
collisions to *non-event* times.

**A partial tie is rejected.** If index `j` and `j+1` are within
`kKeyTimeEpsilon` of each other on an event timeline and exactly one of them is
selected, the operation rejects:

```text
Event keys sharing time 0.750000 s in animation 'idle' must be scaled together;
select every event key at that time.
```

Without this rule a selection naming one member of a tie would silently split it
whenever `s != 1`, contradicting criterion 2's first clause. The GUI cannot
produce a partial tie from a box selection (a box takes every key in its time
span), but `activate_timeline_key()` on a single event key can, and the Agent
certainly can. §9.3 has the shell gesture perform the same check at `begin` and
refuse to arm with a status message, so the GUI reports the problem before the
drag rather than on the first frame. **The gesture never widens the selection**;
silently selecting keys the user did not select would violate criterion 4's
"stable selection".

### 6.6 What the operation writes

Exactly `keyframe.time`, on exactly the resolved keys. Not `interpolation`, not
`curve_mode`, not `curve_driver`, not `angle`/`x`/`y`/`color`/`vertex_offsets`,
not `animation_edits`, not `loop_sync`. The guarantee is structural: the write
in step 11 names `.time` and nothing else, and every other byte of `candidate`
came from the copy in step 2.

Downstream, MAR-171's resolver *will* rewrite automatic curves (because segment
spans changed) and MAR-172's sync *may* rewrite a boundary key — but both happen
in the caller's transaction through their own primitives, after
`scale_keyframe_times()` has returned (§8.6, §8.7).

## 7. Ratio Derivation

### 7.1 From a pointer

```cpp
// timeline_model
std::optional<double> scale_from_edge_time(
    double pivot_time,
    double edge_original_time,
    double edge_target_time);
```

```text
span = edge_original_time - pivot_time      // signed
if any input non-finite, or |span| <= kKeyTimeEpsilon      -> nullopt
s = (edge_target_time - pivot_time) / span
return finite(s) && s > 0.0 ? s : nullopt
```

The signed `span` makes one function serve both pivots: dragging the late edge
has `span > 0`, dragging the early edge has `span < 0`, and the quotient is
positive in both cases exactly when the dragged edge is still on its own side of
the pivot.

The shell computes `edge_target_time` from the pointer with the frozen view:

```text
edge_target_time = frozen_view_start_seconds
                 + (pointer_x - frozen_lane_min_x) / frozen_pixels_per_second
```

A `nullopt` — a non-finite pointer, a degenerate span, or the pointer at or past
the pivot — makes the frame a **no-op**. The gesture holds its last accepted
ratio and stays alive. Dragging the edge across the pivot therefore parks the
selection at its last valid shape instead of cancelling or inverting, which is
the only behaviour compatible with "finite positive ratios only" that does not
punish an overshoot.

### 7.2 Frame snapping

```cpp
// timeline_model
std::optional<double> snap_scale_to_frames(
    double pivot_time,
    double edge_original_time,
    double requested_scale,
    double frames_per_second);
```

```text
span = edge_original_time - pivot_time
if any input non-finite, fps <= 0, requested_scale <= 0, or |span| <= eps -> nullopt
target        = pivot_time + span * requested_scale
delta         = target - edge_original_time
snapped_delta = *snap_delta_to_frames(edge_original_time, delta, fps)   // SHARED
snapped_scale = (edge_original_time + snapped_delta - pivot_time) / span
return finite(snapped_scale) && snapped_scale > 0.0 ? snapped_scale : nullopt
```

This is literal reuse of the existing `timeline_model::snap_delta_to_frames()`
(`timeline_model.cpp:409`), the same helper `retime_keyframes()` uses, applied to
the one key the pointer is dragging. Criterion 3's "frame snapping … reuse the
common authoring rules" is satisfied by call, not by re-derivation.

**Only the moved edge is snapped.** Interior keys land wherever the ratio puts
them. Three reasons:

1. **It is what a scale means.** Quantizing every key would change the ratios
   between them, so the result would no longer be `t' = p + (t-p)·s` for any
   single `s` — the operation would stop being a scale.
2. **It keeps the composition exact.** §7.3's incremental ratio relies on
   `t_current = p + (t_orig - p) · s_applied` holding for every key. Per-key
   quantization breaks that identity on the first frame.
3. **It is the affordance the user is manipulating.** The pointer is on the edge
   grip; the edge is what should land on a frame boundary.

`nullopt` (no positive snapped ratio exists — the snapped target landed on or
past the pivot) makes the frame a no-op, mirroring `retime_keyframes()`'s "apply
nothing when no frame boundary fits inside the bounds" (`authoring.cpp:1795-1804`).

Alt suppresses snapping for the current frame, exactly as
`update_timeline_retime_gesture()` already does
(`shell_timeline.cpp:145-146`).

### 7.3 Incremental composition

```cpp
// timeline_model
std::optional<double> incremental_scale_ratio(
    double requested_scale,
    double applied_scale);
```

```text
if either non-finite or non-positive -> nullopt
r = requested_scale / applied_scale
return finite(r) && r > 0.0 ? r : nullopt
```

This mirrors `incremental_retime_delta()` (`timeline_model.cpp:423`) with
multiplication in place of subtraction.

**Why it is exact enough.** After a frame with applied ratio `s_a`, every
selected key stores `t = p + (t_orig - p)·s_a`. Applying `r = s_t / s_a` about
the same `p` gives `p + (t_orig - p)·s_a·r = p + (t_orig - p)·s_t`. The pivot is
bit-identical each frame (§6.1), so the composition telescopes exactly in exact
arithmetic. In `double` arithmetic each accepted frame adds at most two roundings
per key, so after `N` accepted frames the relative error is bounded by `2·N·u`
with `u = 2^-53`. At 60 fps a 167-second continuous drag is `N = 10^4`, giving a
relative error of `2.2e-12` — about 2 picoseconds on a 1-second key time, six
orders of magnitude below `kKeyTimeEpsilon` and eight below the 1 ms spacing.

**And the bound is enforced, not just argued.** `finish_timeline_scale_gesture(state, true)`
re-derives every key's expected time from the stored `original_times` and
`applied_scale` and compares against the transaction's project. A deviation
greater than `kKeyTimeEpsilon` **cancels instead of committing**, with
`"Timeline scale drifted; the edit was discarded"`. §17.5 drives 5000 synthetic
frames through `apply_timeline_scale_ratio()` and asserts the commit path
accepts.

## 8. Interaction With Everything Already Built

### 8.1 MAR-168 — the gesture and transaction model

MAR-173 inherits it wholesale: a press arms a **candidate** holding no
transaction; leaving a 4.0 px dead zone opens **one** `EditTransaction`; each
frame mutates `transaction.project()` and calls `refresh_runtime()`; release
commits through `completion_decision()`; Escape, focus loss, shutdown, tab
switch, and project reload cancel. `TimelineScaleGesture` joins
`authoring_gesture_active()` and `cancel_authoring_gestures()`;
`TimelineScaleDragCandidate` joins only the latter, because it holds no
transaction (MAR-168 §10.5's exact rule).

### 8.2 MAR-169 / MAR-170 — easing

Scaling writes no `interpolation` and no `curve_mode`, so it neither applies nor
demotes anything. A key's outgoing easing is a *shape in normalized segment
space*; scaling a segment's span leaves that shape's control points unchanged and
therefore leaves a manual curve looking identical, just stretched in time. That
is the correct and expected behaviour and §17.2 asserts a manual key's four
control points are byte-identical after a scale.

### 8.3 Explicit duration

`scale_keyframe_times()` writes no `AnimationEdit`. Duration behaviour comes
entirely from the session seam:

| Situation | Behaviour |
| --- | --- |
| No explicit duration | the animation stays inference-driven; `inferred_duration()` follows the scaled keys |
| Explicit `D`, `s > 1` pushing a key past `D` | `auto_extend_explicit_animation_durations()` grows `D` to the new maximum **inside the same transaction and the same undo entry** |
| Explicit `D`, `s < 1` | `D` is **unchanged**. Auto-extend never shrinks, and MAR-173 adds no shrink |
| Explicit `D`, MAR-172 lane opted in | the boundary key is pinned (§8.5), so a scale touching it rejects before any of this |

**Scaling can grow an animation's explicit duration and can never shrink one.**
Shrinking would silently discard authored intent (the animator chose `D`), and
the safe shrink path already exists as `animation.set_duration`. §17.5 asserts
both directions.

### 8.4 The shared per-family spacing

`include_resolved_retime_bounds()` (`authoring.cpp:478-536`) hard-codes
`kNonEventKeySpacing` for five families and `0.0` for `Event` at six call sites.
MAR-173 factors that into one file-local helper:

```cpp
/**
 * @brief The minimum time separation two keys of one family must keep.
 *
 * Event timelines return 0.0 because same-time event keys are legal and are
 * distinguished by `same_time_ordinal`; every other family returns the shared
 * `kNonEventKeySpacing`. This is the single definition
 * `include_resolved_retime_bounds()` and `scale_keyframe_times()` share, so a
 * retime and a scale can never disagree about what a collision is.
 */
double family_key_spacing(TimelineKeyKind kind);
```

`include_resolved_retime_bounds()` is rewritten to call it, which is a
behaviour-preserving change proved by the existing retime tests passing
unchanged.

### 8.5 MAR-172 — loop-sync pinning, inherited not reimplemented

MAR-172 §10.2 pins the first and last key of an opted-in lane by clamping the
retime delta to zero from both sides, and MAR-172 §16 states directly:
*"MAR-173's selection-edge scaling must inherit §10.2's pinning rather than
reimplement it."*

MAR-173 extracts MAR-172's condition into one file-local predicate:

```cpp
/**
 * @brief Reports whether one resolved key is pinned by loop synchronization.
 *
 * On a loop-synchronized lane the key at index 0 defines the boundary value at
 * t = 0 and the last key IS the managed boundary at the clip duration, so
 * moving either would leave the lane without the prerequisites its opt-in
 * asserts. This is the single definition `include_resolved_retime_bounds()`
 * and `scale_keyframe_times()` share.
 */
bool resolved_key_is_loop_pinned(
    const ProjectData& project,
    const ResolvedTimelineKey& resolved);
```

`include_resolved_retime_bounds()` calls it in place of MAR-172's inline block —
behaviour-preserving, proved by MAR-172's own pinning tests passing unchanged.

**Retime pins; scaling rejects.** The two differ, and the difference is
principled:

- A retime selection that contains a pinned key collapses to `applied_delta == 0`
  and reports `changed == false`. Nothing moves, no error, no history entry. That
  works because a retime is one shared delta: pinning one key pins all of them
  consistently.
- A scale selection has a **different delta per key**. Pinning one key while the
  rest scale would produce a shape that is not `p + (t-p)·s` for any `s` — it
  would silently deliver something other than what the user asked for. So
  `scale_keyframe_times()` rejects, naming the lane and the remedy:

  ```text
  Animation 'idle' timeline 'spine/rotate' is loop synchronized; its first and
  last keys are pinned. Disable loop synchronization on that timeline to scale
  them.
  ```

This is checked in step 5, **before** the target computation, so it is a clean
preflight rejection with the project untouched.

A selection that avoids both pinned keys scales normally, and the boundary key
stays where the contract requires (§8.6).

### 8.6 MAR-172 — the session seam

`EditorSession::refresh_runtime()` and `::commit()` run
`auto_extend_explicit_animation_durations()` and then
`synchronize_loop_boundaries()`. A scale gesture inherits both with **zero
wiring**:

- growth of an explicit duration lands in the same transaction (§8.3);
- every opted-in lane's boundary key is re-mirrored and, if the duration grew,
  moved to the new duration — all inside the same history entry.

A sync rejection (a scaled key pushed past the boundary of an opted-in lane the
user did not select) fails `refresh_runtime()`, and the gesture cancels with full
rollback. §17.5 covers it.

### 8.7 MAR-171 — automatic curves

Automatic curves are computed from neighbouring key **times**, so a scale
invalidates every automatic curve of the animation. The gesture calls the shell
wrapper `resolve_timeline_auto_curves()` after a successful
`scale_keyframe_times()` and **before** `refresh_runtime()`, exactly as
`apply_timeline_retime_delta()` does (`timeline_controller.cpp:2274-2281`). The
Agent handler calls `resolve_agent_auto_curves()` at the same point in its
sequence, exactly as `timeline.retime_keyframes` does.

A resolve failure cancels the gesture / the Agent transaction with the project
untouched.

Note that MAR-172's session seam re-runs `resolve_automatic_curves()` between its
two sync phases, making the gesture's call a redundant first pass on an opted-in
animation. That redundancy is MAR-172 §9.2's documented, idempotent design and
MAR-173 changes nothing about it.

**Why the ratio itself is not scale-invariant for auto curves.** A monotone
Fritsch–Carlson tangent is computed from `(Δt, Δv)` pairs. Uniformly scaling
every `Δt` of a segment chain by `s` scales every slope by `1/s`, and the
normalized cubic control points MAR-171 stores are `[1/3, a/3, 2/3, 1 - b/3]`
with `a, b` derived from *ratios* of slopes — which are invariant under a uniform
`Δt` scale. **So a scale that moves every key of a track by the same ratio leaves
its automatic curves byte-identical, and a scale that moves only part of a track
changes them.** §17.2 asserts both, and the first is a genuine
`resolved_key_count == 0` case worth pinning.

### 8.8 Selection and `active_key`

Positive `s` makes `t ↦ p + (t - p)·s` strictly increasing, and §6.4 rejects any
projected order inversion, so **every key's index within its timeline is
unchanged by a scale**. The gesture therefore rebuilds `selected_keys` and
`active_key` from the same resolved indices in the rebuilt track set after each
accepted frame, exactly as `apply_timeline_retime_delta()` does
(`timeline_controller.cpp:2296-2320`). The grip under the cursor stays the same
key for the whole drag, and every rejected frame leaves the selection untouched
because it leaves the project untouched.

Post-undo behaviour is §10.5.

## 9. The Selection Range Bar

### 9.1 Where it lives

One new 14 px `InvisibleButton` strip, `timeline_selection_range`, drawn in
`draw_dopesheet_body()` **immediately after** `draw_timeline_ruler()` and before
the `timeline_tracks` table. It spans the same width and uses the same
`timeline_x_from_time()` mapping as the ruler and every lane, so the span it
draws lines up with the key diamonds beneath it.

The ruler is **not** modified. It keeps its wheel zoom, middle-drag pan, and
left-click scrub. Putting the grips on the ruler would have made a scrub and a
scale ambiguous at the same pixel.

### 9.2 When it is drawn

All of:

- `view_mode == TimelineViewMode::Dopesheet`;
- a project, an animation, and a non-empty `tracks` list exist;
- `selected_keys` contains at least one key on an editable track;
- the selection's time span exceeds `kKeyTimeEpsilon`.

Otherwise the strip is skipped entirely and no layout space is consumed, so a
single-key selection and an empty selection look exactly as they do today.

The span comes from one shared helper so the bar and the primitive cannot
disagree about what the selection's range is:

```cpp
// timeline_model
struct SelectionTimeSpan {
    double minimum_time{0.0};
    double maximum_time{0.0};
    std::size_t key_count{0U};
    bool valid{false};      // key_count >= 2 and maximum - minimum > kKeyTimeEpsilon
};

SelectionTimeSpan selection_time_span(
    const std::vector<KeyRef>& selection,
    const std::vector<TrackRow>& tracks);
```

### 9.3 Arming

On `ImGui::IsItemClicked(ImGuiMouseButton_Left)` inside a grip's 7 px half-width
hit box, and when `!authoring_gesture_active(*state)`, the driver records a
**drag candidate** — no transaction:

```text
begin_timeline_scale_drag(state, item_id, pivot, pointer_x, plot geometry)
```

The candidate stores, and never re-reads: the item ID, which pivot the grip
implies, `press_pointer_x`, the pivot time, the moved edge's original time, and
the frozen `pixels_per_second` / `view_start_seconds` / lane origin.

`begin_timeline_scale_drag()` refuses when the selection contains a partial event
tie (§6.5) or a loop-sync pinned key (§8.5), setting a status message naming the
reason. Refusing at arm time rather than on the first frame means the user is
told before anything appears to happen.

### 9.4 The drag

Every frame while the candidate is live and the left button is down:

```text
dx = pointer_x - press_pointer_x
if |dx| < 4.0                       -> still a candidate, nothing happens
on the first frame with |dx| >= 4.0 -> begin_timeline_scale_gesture(...)

edge_target = frozen_view_start + (pointer_x - frozen_lane_min_x) / frozen_pps
requested   = *scale_from_edge_time(pivot_time, edge_original_time, edge_target)
if snap_to_frames && !alt:
    requested = *snap_scale_to_frames(pivot_time, edge_original_time, requested, fps)
apply_timeline_scale_ratio(state, tracks, requested)
```

A `nullopt` at either step makes the frame a no-op (§7.1, §7.2).

The dead zone is MAR-168's, for MAR-168's reason: a press that never moves must
produce no transaction and no history entry, and the candidate holding no
transaction keeps `authoring_gesture_active()` false so a press cannot block
unrelated editing.

Release commits with `finish_timeline_scale_gesture(state, true)`; Escape, focus
loss, shutdown, a tab switch, or a project reload cancels with `false`.

### 9.5 What it draws

- The selection span as a filled bar from `x(t_min)` to `x(t_max)`, with the two
  grips drawn as 7 px-wide handles at each end.
- While a gesture is live, one readout line:
  `Scale 1.250x   span 0.500s -> 0.625s   (pivot 0.250s)`.
- When the last frame was rejected, the reason is appended in the error colour
  and the bar keeps its last accepted geometry. This is the only place a
  rejection is visible; it is not a status-bar message, because a rejection during
  a live drag is transient state, not an event.

Status messages on completion: `"Scaled timeline keys"`,
`"Cancelled timeline scale"`, `"Timeline scale failed"`.

### 9.6 View freeze and mutual exclusion

While a scale candidate or gesture is live:

- the ruler's wheel zoom and middle-drag pan are suppressed, so
  `pixels_per_second` and `view_start_seconds` cannot move under the pointer;
- `update_timeline_retime_gesture()` cannot start, because arming a lane retime
  goes through `begin_timeline_retime_gesture()`, which checks
  `authoring_gesture_active()`;
- the strip returns early in Graph view mode, and a `requested_view_mode`
  transition away from Dopesheet cancels any live candidate or gesture before the
  tab body changes.

MAR-168 §16 decision 7 argued the freeze; the argument transfers verbatim.

## 10. Transaction Lifecycle and Atomicity

### 10.1 Three nested layers

Criterion 4 says "Preview, cancel, stable selection, undo, redo, and save/reload
treat one scaling gesture as one atomic edit". "Atomic" is used at three scales
in this design and they are stated separately so no claim is louder than what it
proves:

| Layer | What is all-or-nothing | Mechanism |
| --- | --- | --- |
| **One primitive call** | the project either has every scaled time or none of them | candidate copy + single move-assign (§6.3) |
| **One gesture frame** | a rejected frame leaves the transaction's project exactly as the last accepted frame left it | the primitive mutated only its own candidate; the gesture does not cancel (§10.2) |
| **One gesture** | one history entry, one preview stream, one undo, one redo | `EditTransaction` + `completion_decision()` (§10.3) |

### 10.2 A rejected frame is not a cancelled gesture

| Frame outcome | Gesture response |
| --- | --- |
| `scale_keyframe_times()` returned an error | keep the last accepted state; store the message in `gesture.rejection`; **stay alive** |
| `scale_keyframe_times()` returned `changed == false` | no-op; clear `gesture.rejection`; stay alive |
| success | clear `gesture.rejection`; resolve auto curves; `refresh_runtime()`; rebuild selection; `applied_scale = requested` |
| a lost `TimelineKeyRef` identity | **cancel** with rollback |
| a materialization failure | **cancel** with rollback |
| `resolve_timeline_auto_curves()` failure | **cancel** with rollback |
| `refresh_runtime()` failure (including a MAR-172 sync rejection) | **cancel** with rollback |
| a non-finite pointer or frozen view scale | **cancel** with rollback |

The split is the whole ergonomic argument for this design. Shrinking a selection
until two keys collide, then dragging back out to a legal ratio, is a completely
ordinary thing to do with a scale handle. If the first colliding frame killed the
transaction, the drag would die mid-motion and the user would lose the edit for
touching a boundary. Holding instead means the bar simply stops shrinking, the
readout explains why, and the drag continues.

The five cancelling rows are all *structural* failures — the world changed under
the gesture — and none of them is reachable by moving the pointer.

### 10.3 One gesture, one history entry

| Phase | Action |
| --- | --- |
| Press on a grip | drag candidate only, no transaction |
| Dead zone left | `begin_timeline_scale_gesture()` opens one `EditTransaction` |
| Each frame | `apply_timeline_scale_ratio()` mutates `transaction.project()` and calls `refresh_runtime()` |
| Release | `finish_timeline_scale_gesture(state, true)` |
| Escape / focus loss / shutdown / tab switch / project reload | `finish_timeline_scale_gesture(state, false)` |

The transaction:

```cpp
{ EditKind::EditProperty,
  keys.size() == 1U ? "Scale timeline key" : "Scale timeline keys",
  "timeline:scale",                 // distinct from timeline:retime,
  false,                            // timeline:graph-value, timeline:graph-easing,
  EditImpact::Project |             // timeline:curve-preset, timeline:curve-mode,
  EditImpact::Runtime |             // timeline:loop-sync — nothing merges across them
  EditImpact::Preview }
```

`refresh_runtime()` explicitly does not create history; only `commit()` does.
`completion_decision(commit_requested, changed)` cancels an unchanged gesture, so
a drag that ends at `s = 1` leaves `undo_count()` unchanged.

### 10.4 Rollback across the shell

Identical to MAR-168 §10.3. `EditTransaction::cancel()` restores the project, the
runtime `SkeletonData`, the preview controller state, and the playback state
captured at `begin_edit()`, then bumps runtime/preview revision;
`sync_shell_from_editor_session(state)` refreshes the shell mirrors. The
dopesheet reads `cached_timeline_tracks()` and the graph reads
`cached_timeline_graph_projection()`, both revision-keyed, so both rebuild from
the restored data. Neither holds a runtime pointer or key index across frames.

§17.5 proves it by comparing `serialize_project()`, `undo_count()`,
`redo_count()`, `project_revision()`, `dirty()`, the rebuilt dopesheet
`TrackRow::key_times`, and the selection before the press and after the cancel.

### 10.5 Selection stability, and the one honest limit

| Moment | Behaviour |
| --- | --- |
| Every accepted frame | rebuilt from the same resolved indices; the grip stays on the same key (§8.8) |
| Every rejected frame | untouched — the project did not change |
| Cancel | the pre-gesture times are restored, so the pre-gesture refs resolve again |
| Commit | `selected_keys` and `active_key` name the scaled times and resolve |
| Redo of a committed scale | the scaled times return; the post-commit refs resolve |
| **Undo of a committed scale** | the original times return; the post-commit refs do **not** resolve, and the shared `reconcile_timeline_key_selection()` prunes them |

The last row is **pre-existing shared dopesheet behaviour**, recorded by MAR-168
§10.4 and §14 for a committed *time* edit, and MAR-173 deliberately does not
change it. Doing so would require `EditorSession`'s history to carry shell
selection, which is a cross-cutting change to every existing history entry and to
undo/redo for every feature — far outside this story's vertical slice, and named
by no MAR-173 criterion. §17.5 asserts the **deterministic reconciled outcome**
rather than bit-identity, so the behaviour is pinned rather than merely tolerated,
and §15 records it as a non-goal with the exact location a fix would live.

## 11. UI-Free API

### 11.1 `src/editor/timeline_model.hpp` (new declarations)

```cpp
struct SelectionTimeSpan {
    double minimum_time{0.0};
    double maximum_time{0.0};
    std::size_t key_count{0U};
    bool valid{false};
};

/** @brief The selected keys' time range, resolved against the current tracks. */
SelectionTimeSpan selection_time_span(
    const std::vector<KeyRef>& selection,
    const std::vector<TrackRow>& tracks);

/** @brief The positive ratio that moves `edge_original_time` to `edge_target_time`. */
std::optional<double> scale_from_edge_time(
    double pivot_time,
    double edge_original_time,
    double edge_target_time);

/**
 * @brief The nearest ratio whose moved edge lands on a frame boundary.
 *
 * Snaps only the dragged edge, through the shared `snap_delta_to_frames()`;
 * interior keys keep the ratio's exact placement, because quantizing them would
 * stop the result from being a scale at all.
 */
std::optional<double> snap_scale_to_frames(
    double pivot_time,
    double edge_original_time,
    double requested_scale,
    double frames_per_second);

/** @brief The ratio to apply now, given what a gesture has already applied. */
std::optional<double> incremental_scale_ratio(
    double requested_scale,
    double applied_scale);
```

### 11.2 `include/marrow/editor/authoring.hpp` (additive)

Appended after MAR-172's declarations.

```cpp
/** @brief Which edge of the selection's time range stays fixed while scaling. */
enum class TimelineScalePivot : std::uint8_t {
    RangeStart,  // the earliest selected time is the pivot; the late edge moves
    RangeEnd,    // the latest selected time is the pivot; the early edge moves
};

struct TimelineScaleResult : AuthoringResult {
    double pivot_time{0.0};
    double applied_scale{1.0};
    double original_span{0.0};
    double scaled_span{0.0};
    std::size_t key_count{0U};
    std::size_t moved_key_count{0U};
};

/**
 * @brief Atomically scales persisted key times about one edge of their range.
 *
 * The pivot is never a caller-supplied time: it is the opposite edge of the
 * resolved selectors' own time range, so `t' = pivot + (t - pivot) * scale`
 * leaves the pivot key bit-identical by construction. `scale` must be finite
 * and strictly positive; a ratio of exactly one, or one that moves nothing,
 * reports `changed == false` with no error and writes nothing.
 *
 * Unlike `retime_keyframes()`, this **rejects** rather than clamps. Any
 * projected pair on an affected timeline that would fall closer than the
 * family's minimum separation — including a selected key intruding on an
 * unselected neighbour — rejects the whole call. Event keys sharing a time are
 * carried together because the mapping is a function of time, and a selection
 * naming only part of such a tie is rejected by name. A key pinned by loop
 * synchronization rejects rather than pinning, because a partially pinned scale
 * is not a scale.
 *
 * Only `keyframe.time` is written. Callers materialize imported runtime-only
 * tracks through the shared `ensure_*_timeline_edit` project operations first,
 * and re-resolve automatic curves afterwards inside the same transaction. A
 * rejected edit leaves the project unchanged.
 */
TimelineScaleResult scale_keyframe_times(
    ProjectData* project,
    const std::vector<TimelineKeySelector>& selectors,
    TimelineScalePivot pivot,
    double scale);
```

### 11.3 `src/editor/timeline_controller.hpp` (shell, ImGui-free)

```cpp
/**
 * @brief Opens one live transaction that scales the current key selection.
 *
 * Fails closed on a null state, another live authoring gesture, an empty or
 * single-time selection, a non-editable or unresolvable track, a partial event
 * tie, and a loop-synchronization pinned key.
 */
bool begin_timeline_scale_gesture(
    ShellState* state,
    std::uint32_t item_id,
    marrow::editor::TimelineScalePivot pivot,
    const std::vector<TimelineTrackRow>& tracks);

/**
 * @brief Applies one absolute ratio, holding the last accepted state on a
 *        rejection and cancelling atomically on a structural failure.
 * @return false when the gesture ended; the gesture is already gone.
 */
bool apply_timeline_scale_ratio(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    double requested_scale);

void finish_timeline_scale_gesture(ShellState* state, bool commit);

/** @brief The last rejected frame's reason, or empty while the scale is legal. */
std::string_view timeline_scale_rejection(const ShellState& state);
```

### 11.4 `src/editor/shell_timeline.hpp` (shell, ImGui-free entry points)

```cpp
bool begin_timeline_scale_drag(
    ShellState* state,
    std::uint32_t item_id,
    marrow::editor::TimelineScalePivot pivot,
    double pointer_x,
    double lane_min_x,
    double pixels_per_second,
    double view_start_seconds,
    const std::vector<TimelineTrackRow>& tracks);

/**
 * @brief Advances or terminates the live scale drag from sampled input.
 * @return true while a candidate or gesture remains live.
 */
bool update_timeline_scale_drag(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    double pointer_x,
    bool pointer_down,
    bool cancel_requested,
    bool bypass_frame_snap);

void cancel_timeline_scale_drag(ShellState* state);
```

These take only plain scalars, so `shell_smoke_graph.cpp` drives complete scale
gestures headlessly without an ImGui frame — the same shape MAR-168 §11.4 uses.

### 11.5 `src/editor/shell_state.hpp` (transient shell state)

```cpp
struct TimelineScaleDragCandidate {
    std::uint32_t item_id{0U};
    marrow::editor::TimelineScalePivot pivot{
        marrow::editor::TimelineScalePivot::RangeStart};
    double press_pointer_x{0.0};
    double pivot_time{0.0};
    double edge_original_time{0.0};
    double frozen_pixels_per_second{160.0};
    double frozen_view_start_seconds{0.0};
    double frozen_lane_min_x{0.0};
};

struct TimelineScaleGesture {
    std::uint32_t item_id{0U};
    marrow::editor::TimelineScalePivot pivot{
        marrow::editor::TimelineScalePivot::RangeStart};
    std::vector<TimelineKeyRef> keys;
    std::vector<double> original_times;
    double pivot_time{0.0};
    double edge_original_time{0.0};
    double applied_scale{1.0};
    bool materialized{false};
    bool changed{false};
    std::string rejection;   // last rejected frame's reason, for the readout
    marrow::editor::EditorSession::EditTransaction transaction;
};
```

added to `TimelineEditorState` as
`std::optional<TimelineScaleDragCandidate> scale_drag;` and
`std::optional<TimelineScaleGesture> scale_gesture;`.

`scale_gesture` is added to `authoring_gesture_active()` (`shell_state.hpp:803`)
and to `cancel_authoring_gestures()` (`shell_core.cpp:431`). `scale_drag` is
added to the cancel list only. The header comment already requires those two
lists to stay in step. Both live directly in `TimelineEditorState`, so
`TimelineEditorState{}` source adoption clears them atomically.

### 11.6 What stays shell-private ImGui code

`draw_timeline_selection_range_bar()` keeps only: `ImGui::InvisibleButton`,
`ImGui::IsItemClicked`, `ImGui::IsMouseDown`, `io.MousePos`, `io.KeyAlt`,
`ImGui::IsKeyPressed(ImGuiKey_Escape)`, the `AddRectFilled`/`AddRect`/`AddText`
draw calls, and the suppression of ruler zoom/pan while a drag is live. It
contains no arithmetic that maps pixels to ratios and no project mutation.

## 12. Agent and MCP Surface

### 12.1 The operation

| Field | Value |
| --- | --- |
| `name` | `timeline.scale_key_times` |
| `category` | `edit` |
| `mutating` | `true` |
| `requires_review` | `false` |
| `dry_run_supported` | `true` |
| `requires_project` | `true` |
| `handler` | `&handle_editing_operation` (tail-calls `handle_timeline_editing_operation`) |
| registry position | immediately after MAR-172's `timeline.set_loop_sync` |

`kOperationSpecs[]` row order is part of the contract —
`expect_registry_contract()` in `agent_dispatch_smoke.cpp` compares index by
index — so the row goes at the end of the contiguous `timeline.*` editing block
and before `set_transform`.

**New registry total: 60 operations**, assuming MAR-172 landed at 59. §13
requires the count to be measured, not assumed.

### 12.2 The shared selector parser

`timeline.retime_keyframes` currently parses its `keys` array inline
(`agent_handlers_editing.cpp:756-845`). `timeline.scale_key_times` needs the
**identical** selector shape — same six kinds, same `bone`/`channel`,
`slot`/`attachment`, `ordinal` rules, same `time`. Duplicating ninety lines
would guarantee they drift.

MAR-173 extracts the loop into `agent_dispatch_internal.hpp`:

```cpp
/**
 * @brief Parses one timeline key selector array into project-domain selectors.
 *
 * `operation_label` is embedded in every message so each caller keeps its own
 * error strings byte-identical. This is the single parser
 * `timeline.retime_keyframes` and `timeline.scale_key_times` share.
 */
bool timeline_key_selectors_arg(
    const json::Value& keys_value,
    std::string_view operation_label,
    std::vector<marrow::editor::TimelineKeySelector>* selectors_out,
    std::string* error_out);
```

This is a **behaviour-preserving extraction**, not a redesign: every existing
`timeline.retime_keyframes` message is reproduced exactly by passing
`"timeline.retime_keyframes"` as the label. The plan makes it its own task with
its own gate — extract, run `marrow_agent_dispatch_smoke` **unchanged**, and only
then add the new operation (plan Task 5). `interpolation_request_arg()`,
`curve_mode_request_arg()`, and MAR-172's `timeline_lane_selectors_arg()` are
left byte-identical.

### 12.3 Arguments

```json
{
  "keys": [
    {"kind": "transform", "animation": "idle", "bone": "spine",
     "channel": "rotate", "time": 0.5},
    {"kind": "transform", "animation": "idle", "bone": "spine",
     "channel": "rotate", "time": 1.0}
  ],
  "scale": 1.25,
  "pivot": "start",
  "snap": false,
  "frames_per_second": 60,
  "dry_run": false
}
```

- `keys`: required array, 1 to 4096 entries, the same cap and the same entry
  shape `timeline.retime_keyframes` uses.
- `scale`: **required** number. Missing is an error, not a default — the same
  rule MAR-169 applied to `interpolation`, MAR-171 to `mode`, and MAR-172 to
  `enabled`: guessing a ratio for a caller's whole selection is destructive.
- `pivot`: **required** string, `"start"` or `"end"`, naming which edge of the
  selection's time range stays fixed. Also required rather than defaulted: the
  two pivots produce different results for the same `scale`, and neither is a
  safe guess.
- `snap`: optional boolean, **default `false`**. `timeline.retime_keyframes`
  defaults `snap` to `true` because a delta is a pointer-shaped quantity; a
  scripted ratio is exact, and silently reshaping it would surprise a caller who
  computed it. §16 decision 6 records this asymmetry.
- `frames_per_second`: optional number, used only when `snap` is true, defaulting
  to `session.project()->editor_metadata.timeline.frames_per_second` exactly as
  retime does.
- `dry_run`: optional boolean.

When `snap` is true the handler snaps through the same
`timeline_model::snap_scale_to_frames()` the gesture uses, with the moved edge
derived from the resolved selectors: `RangeStart` moves `t_max`, `RangeEnd` moves
`t_min`. A `nullopt` snap result is `invalid_request`
(`"No frame boundary produces a positive scale ratio."`) rather than a silent
fallback to the unsnapped ratio.

### 12.4 Validation rules

In order:

1. missing `args` object → `invalid_request`.
2. `keys` missing, not an array, or empty → `invalid_request`.
3. more than 4096 keys → `invalid_request`.
4. a key entry that is not an object, or missing `kind`/`animation`/`time`, or
   missing `bone`+`channel` (transform) / `slot`+`attachment` (deform) / `slot`
   (slot_color, slot_attachment), or an unknown kind, or an unknown transform
   channel, or a negative event `ordinal` → `invalid_request`, from
   `timeline_key_selectors_arg()`.
5. `scale` missing or not a number → `invalid_request`.
6. `pivot` missing or not `"start"` / `"end"` → `invalid_request`, naming both
   accepted values.
7. `snap` true with a non-finite or non-positive `frames_per_second` →
   `invalid_request`.
8. `scale` non-finite or `<= 0` → `invalid_request` (from the primitive).
9. selectors naming more than one animation → `invalid_request`.
10. the same key selected twice → `invalid_request`.
11. a key that does not resolve after materialization → `not_found`, via
    `classify_timeline_key_error()`.
12. a selection whose span is at most `kKeyTimeEpsilon` → `invalid_request`.
13. a loop-synchronization pinned key → `invalid_request`, naming the lane and
    the remedy.
14. a partial event tie → `invalid_request`, naming the time.
15. a non-event collision or an intrusion into an unselected neighbour →
    `invalid_request`, naming both times and the required separation.
16. an automatic-curve resolve failure → `invalid_request`.
17. live path only: nothing changed → `no_change`.

### 12.5 Response payload

```json
{
  "dry_run": true,
  "requested_scale": 1.25,
  "applied_scale": 1.25,
  "pivot": "start",
  "pivot_time": 0.0,
  "original_span": 1.0,
  "scaled_span": 1.25,
  "snap": false,
  "frames_per_second": 60.0,
  "key_count": 3,
  "moved_key_count": 2,
  "keys_truncated": false,
  "keys": [
    {"kind": "transform", "animation": "idle", "bone": "spine",
     "channel": "rotate", "previous_time": 0.0, "time": 0.0, "moved": false},
    {"kind": "transform", "animation": "idle", "bone": "spine",
     "channel": "rotate", "previous_time": 0.5, "time": 0.625, "moved": true},
    {"kind": "transform", "animation": "idle", "bone": "spine",
     "channel": "rotate", "previous_time": 1.0, "time": 1.25, "moved": true}
  ]
}
```

- The `keys` array is the **affected-key reporting** criterion 5 requires. Each
  entry echoes the selector's identity plus `previous_time`, the resulting
  `time`, and `moved`. It is capped at 256 entries with `keys_truncated` set when
  the selection is larger, matching MAR-172's cap discipline; the counts always
  describe the whole call.
- `applied_scale` differs from `requested_scale` only when `snap` reshaped it.
- `pivot_time` is the pivot the primitive computed, so a caller can verify which
  edge stayed fixed without recomputing the selection's range.
- A `dry_run` call reports the full result and mutates nothing, so it doubles as
  a **read-back channel** for "what would this ratio do".

`timeline.describe`'s payload stays byte-identical.

### 12.6 Dry-run and live paths

Both first materialize each selector's lane through the six
`ensure_*_timeline_edit()` project operations — the same `apply` lambda shape
`timeline.retime_keyframes` uses (`agent_handlers_editing.cpp:851-892`) — so a
runtime-only lane is copied into the project rather than reported as missing.

- **Dry run**: `ProjectData candidate = *session.project();` materialize; snapshot
  the selected keys' times; `scale_keyframe_times(&candidate, …)`; snapshot again;
  report; discard. The session is never touched, so `project_revision()`,
  `undo_count()`, and `dirty()` are unchanged.
- **Live**: `session.begin_edit({EditKind::EditProperty, "Scale timeline key(s)
  via Agent", "timeline:scale", false, Project | Runtime | Preview})`,
  materialize, apply, `cancel()` on error or on `!result.changed`,
  `resolve_agent_auto_curves()`, then `commit_or_error()` with
  `CommitPolicy{"Failed to scale timeline keys: "}`.

**Export preview.** Criterion 5 names "export-preview behavior". `export.preview`
is an existing inspection operation that reads the session project, so a live
`timeline.scale_key_times` is reflected in it with zero wiring. §17.7 asserts it
rather than assuming it: `export.preview` is called before and after a live scale
and the reported payload must differ, and one `undo` must restore the earlier
payload.

Undo/redo is inherited from `EditorSession`: one operation, one history entry,
reversible by the existing `undo` operation, which is exactly what the same edit
produces from the GUI.

### 12.7 MCP tool

`tools/mcp/tools/editing.py` gains one `types.Tool` named
`timeline.scale_key_times`, placed immediately after MAR-172's
`timeline.set_loop_sync`. Its `keys` items **reuse `_timeline_retime_key_schema()`
verbatim**, because the selector shape is identical to retime's — this is the
opposite of MAR-172's lane schema, which needed its own helper precisely because
it carried no `time`.

```python
"scale": {"type": "number", "exclusiveMinimum": 0},
"pivot": {"type": "string", "enum": ["start", "end"]},
"snap": {"type": "boolean"},
"frames_per_second": {"type": "number", "exclusiveMinimum": 0},
"dry_run": {"type": "boolean"},
```

with `"required": ["keys", "scale", "pivot"]`. The description states that the
pivot names which edge of the selection stays fixed, that only finite positive
ratios are accepted, that collisions and intrusions reject the whole call rather
than clamping, that event ties move together, and that snapping applies to the
moved edge only. The schema stays advisory — the MCP server forwards every call
verbatim (`server.py:75-79`), so the C++ primitive remains the sole authority.
§17.8 proves that by sending `"scale": -1` and `"pivot": "middle"` and asserting
the C++ gate rejects them.

`get_tools()` grows by exactly one entry; no existing tool changes.

## 13. The Operation Count: Every Place the Registry Total Appears

The registry total is derived from `std::size(kOperationSpecs)` and self-updates
when the row is added. Every other site is hand-edited. The rule from MAR-169
§14, MAR-170 §14.5, MAR-171 §14, and MAR-172 §14 applies unchanged: **a sentence
stating what the surface *is* becomes the new number; a sentence carrying a date,
a milestone ID, or the words "historical", "baseline", or "checkpoint … passed"
stays at whatever it recorded.**

**The count is 58 today**, measured on `b19b7b5` by counting `kOperationSpecs[]`
rows. MAR-172 takes it to 59; MAR-173 takes it to 60. The plan's Task 0
**measures** the as-built count and rejects this table's numbers if they disagree.

Discover the full list with:

```bash
rg -n '\b59\b' --glob '!build*' src tools AGENTS.md docs .agents
rg -n '\b60\b' --glob '!build*' src tools AGENTS.md docs .agents   # before: no registry claim
```

| Location | Kind | After MAR-173 |
| --- | --- | --- |
| `src/editor/agent_dispatch.cpp` `kOperationSpecs[]` | code | one more row; the count self-updates |
| `src/samples/agent_dispatch_smoke.cpp:39` `std::array<OperationExpectation, N>` | test | **60** + one new row after `timeline.set_loop_sync` |
| `src/samples/agent_dispatch_smoke.cpp` behaviour cases | test | new `timeline.scale_key_times` cases (required: `expect_complete_coverage()` fails on an uninvoked row) |
| `src/editor/shell_smoke_graph.cpp` count guards | test | **60** at every guard. **Five today** (`:146`, `:634`, `:1556`, `:2126`, `:3140`); MAR-172 adds one; MAR-173's own new scenario adds one more. **Count them, do not assume** |
| `tools/mcp/test_client.py:47,49` | test | **60** |
| `tools/mcp/test_client.py:32-41` `new_edit_operations` | test | add `timeline.scale_key_times` |
| `AGENTS.md:161` `Current Validation` registry line | current doc | **60**, and extend the parenthetical |
| `AGENTS.md:6` `Project State` | current doc | MAR-173 done, MAR-174 next |
| `docs/root1/editing-gap-analysis.md:23` "58개 에이전트 오퍼레이션" | current doc | **60** |
| `docs/root1/editing-gap-analysis.md:86` "58-op registry" | current doc | **60** |
| `docs/root1/editing-gap-analysis.md:192` "58개 오퍼레이션(조회 12, 검증 3, 관리 10, 편집 33)" | current doc | **60**, and edit `33` → `35` |
| `docs/root1/editing-gap-analysis.md:455` "현재 58 ops" | current doc | **60** |
| `docs/root1/refector.md:20` "the **current** registry to the exact 58-operation total" | current doc | **60** |
| `docs/root1/concepts.md` timeline-authoring paragraphs | current doc | one new paragraph on scaling; the registry number appears only if already present |
| `AGENTS.md:247,268,287,289` MAR-171 `Validation Results` block | historical | **unchanged at 58** |
| `AGENTS.md:369` MAR-170 block (`57/57`) | historical | **unchanged** |
| `docs/root1/discription.md:51` MAR-171 paragraph ("정확히 58 operation") | historical | **unchanged** |
| `docs/root1/editing-gap-analysis.md:271,316,322,332,340` (49 / 55 / 56) | historical | **unchanged** |
| `docs/root1/refector.md:83` MAR-130 row (44 / 49) | historical | **unchanged** |
| every other dated `Validation Results`, checkpoint, and past PRD criterion | historical | **unchanged** |

**Known false positives** the sweep surfaces that must **not** be touched:

- `IM_COL32(56, 61, 69, 255)` and `(51, 56, 64)` in the shell theme and graph;
- `IM_COL32(208, 134, 57, 230)` at `shell_viewport_ui.cpp:683` and
  `shell_viewport.cpp:1816`;
- `rgb(54, 57, 64)` at `shell_theme.hpp:39` and
  `docs/design/maroow-editor-visual-renewal-spec.md:100`;
- **the CSS easing constants** `0.58` — `[0, 0, 0.58, 1]` and
  `[0.42, 0, 0.58, 1]` — at `AGENTS.md:318,319,342`, `docs/root1/concepts.md`,
  `shell_smoke_graph.cpp:1597,1598,3750`, and the preset table;
- **`58px` rotation ring** at `AGENTS.md:654,683,689` and in the viewport gizmo
  sources;
- CTest timings `8.58 s` and `12.58 s` at `AGENTS.md:469,506`;
- `"x": 56.0`, `56,995,840` bytes, and `PhysicsBoneState … 56 bytes/bone`;
- every `.agents/tasks/prd-marrow-runtime.json` timestamp containing `:57`,
  `:58`, `:59`, or `:60`.

## 14. Error Handling and Fail-Closed Rules

| Condition | Result |
| --- | --- |
| No project, no animation, or no resolvable track | no candidate; Agent `not_found` |
| Empty selection, or a selection spanning one time | no range bar; Agent `invalid_request` |
| No selected key on an editable track | no range bar; Agent `not_found` |
| `authoring_gesture_active(*state)` | no candidate |
| Partial event tie in the selection | no candidate (status message); Agent `invalid_request` |
| Loop-synchronization pinned key in the selection | no candidate (status message); Agent `invalid_request` |
| Non-finite `scale`, or `scale <= 0` | frame no-op (gesture) / `invalid_request` (Agent) |
| Pointer at or past the pivot | frame no-op; the bar holds its last shape |
| No frame boundary yields a positive ratio | frame no-op; Agent `invalid_request` |
| Non-event collision, or intrusion into an unselected neighbour | **frame held with a readout reason**; Agent `invalid_request` |
| Target time non-finite, negative, or outside float32 | same |
| Selectors naming more than one animation | Agent `invalid_request` |
| Duplicate selector | Agent `invalid_request` |
| Lost `TimelineKeyRef` identity | cancel with rollback |
| Materialization failure | cancel with rollback |
| `resolve_timeline_auto_curves()` failure | cancel with rollback |
| `refresh_runtime()` failure, including a MAR-172 sync rejection | cancel with rollback |
| Commit-time drift beyond `kKeyTimeEpsilon` (§7.3) | cancel instead of commit |
| Escape, window focus loss, editor shutdown, tab switch, project reload | cancel with rollback |

Nothing here can leave a partial write: every project-side failure is either
before the primitive's single move-assign or is a cancel of the enclosing
transaction, and the two layers are independently sufficient.

## 15. Non-Goals

- Preview playback speed, its bounds, or its presets (MAR-174).
- Value scaling of any kind. MAR-173 scales times only.
- A pivot at the playhead, at a numeric anchor, or anywhere but the opposite edge
  of the selection.
- Negative ratios, time reversal, or key-order inversion.
- A numeric scale entry box, a scale toolbar button, or a scale keyboard shortcut.
- Scaling from the Graph tab. The Graph plots one track against a *frozen graph
  view* whose `pixels_per_second` is independent of the dopesheet's; a range bar
  there would need a second gesture driver, a second frozen-view story, and
  `TimelineGraphRenderStats` fields plus an actual-frame smoke, none of which any
  MAR-173 criterion asks for. It would live in `shell_timeline_graph.cpp` beside
  MAR-168's point drag and MAR-169's handle drawing.
- An actual-frame (`shell_smoke_frames.cpp`) scenario. Criterion 6 names project,
  shell, agent, and MCP smokes only. A frames case would press a real mouse on
  the rendered grip coordinates and assert the gesture arms — it would live beside
  MAR-168's graph-drag frames cases and would need two new
  `TimelineGraphRenderStats`-style fields for the grip positions. §17.6 records
  what it would cover.
- Restoring shell key selection across undo of a committed scale. Pre-existing
  shared dopesheet behaviour (MAR-168 §14); a fix would require
  `EditorSession`'s history entries to carry shell selection, in `session.cpp`'s
  snapshot/restore path, and would change undo/redo for every feature.
- Changing `retime_keyframes()`'s clamp behaviour to match scaling's reject
  behaviour, or vice versa. The two policies are correct for their two
  operations (§4.2).
- Any change to MAR-169's clamp/reject split, MAR-170's preset table or
  preference flow, MAR-171's resolver or demotion rule, or MAR-172's flag,
  contract, sync seam, or pinning rule beyond the two behaviour-preserving
  extractions in §8.4 and §8.5.
- Any `.mskl`, `.mbin`, C ABI, `.marrow` schema, or `editor-settings.json`
  version change.
- Manual-visible-UI, Windows 11, or physical-input qualification credit.
  MAR-192 through MAR-210 stay open.

## 16. Decisions Taken Under Ambiguity

1. **The pivot is an enum naming an edge, not a time.** Criterion 1 says the
   pivot *is* the opposite edge, so exposing a free `pivot_time` would let a
   caller — the Agent in particular — violate the criterion while still calling
   the operation. Computing the pivot inside the primitive from the resolved
   selectors makes the criterion structural rather than documented, and the
   result echoes the computed `pivot_time` so nothing is hidden. The cost is that
   a future "scale about the playhead" story needs a new argument; that story
   does not exist and MAR-174 is not it.

2. **Collisions reject; they do not clamp.** `retime_keyframes()` clamps, and
   inheriting that would have been the smaller change. Criterion 2 is explicit —
   "reject the entire operation" — and the reason it differs from retime is
   real: a clamped *translation* still delivers a translation, just a shorter
   one, whereas a clamped *scale* would have to either stop every key at the
   first collision (producing a ratio the user did not choose and cannot see) or
   move keys by different ratios (producing something that is not a scale at
   all). Rejecting and telling the user which pair collided is the only honest
   option.

3. **A rejected frame holds the gesture rather than cancelling it.** This is the
   design's largest interpretive step, because criterion 2 says "reject the
   entire operation" and criterion 4 says one gesture is one atomic edit, and a
   naive reading could combine them into "one bad frame kills the drag". That
   reading makes the feature unusable: dragging a scale handle inward past a
   collision and back out again is ordinary. The resolution is that "operation"
   means one `scale_keyframe_times()` call — which genuinely is all-or-nothing —
   and the gesture's atomicity is about the *history entry*, which a held frame
   does not affect. §10.1 states the three layers separately so neither claim
   borrows the other's strength.

4. **A partial event tie is rejected rather than auto-completed.** Criterion 2
   promises tied event keys stay tied, and a selection naming one member of a tie
   cannot honour that promise. Two repairs were possible: silently add the
   missing keys to the selection, or reject. Auto-completing would mutate
   `selected_keys` behind the user's back, which criterion 4's "stable selection"
   forbids and which would make the committed edit differ from the one the
   selection displayed. Rejecting — at arm time in the GUI, with a message naming
   the time — is visible and correct. This is the GUI-skips / Agent-rejects
   asymmetry MAR-169, MAR-170, MAR-171, and MAR-172 all use, in its one variant
   where the GUI cannot usefully skip anything, so it refuses to start instead.

5. **`s < 0` is rejected rather than implemented as time reversal.** Criterion 1
   says "finite positive scale ratios only", which settles it, but the deeper
   reason is worth recording: reversing a *subset* of a timeline inverts key
   order, and a key's stored `interpolation` describes the segment *leaving* it,
   so a correct reversal would have to permute easings between keys — a rule no
   criterion states and which has no obviously right answer at the selection's
   boundaries. Rejecting keeps `scale_keyframe_times()` a pure monotone remap.

6. **The Agent's `snap` defaults to `false` while retime's defaults to `true`.**
   The asymmetry is deliberate. A retime `delta` is a pointer-shaped quantity and
   snapping it to a frame is almost always what a caller wants. A `scale` is a
   ratio a script computed — `4.0 / 3.0`, say — and silently reshaping it so one
   edge lands on a frame would change the ratio the caller asked for without
   being asked. Snapping stays available and uses the identical helper; it is
   simply opt-in on the scripted surface and opt-out (via Alt) on the pointer
   surface.

7. **Snapping reshapes the ratio and snaps only the moved edge.** Snapping every
   key would make the result not a scale (§7.2), and snapping nothing would make
   the shared `snap_to_frames` setting silently inert on this surface. Snapping
   the edge the pointer is holding is the only choice that keeps the operation a
   scale *and* honours the setting. The consequence — interior keys land off
   frame boundaries — is correct and is what every comparable editor does.

8. **The minimum-separation rule is `min(spacing, original_gap)`, not `spacing`.**
   A flat rule would make scaling impossible on any timeline that already carries
   a sub-millisecond gap, which imported data and MAR-172 boundary adoption can
   both produce. The `min` form is the precise statement of "do not make it
   worse", never blocks a legal project, and still rejects every gap that a scale
   actually tightens (§6.4).

9. **Loop-sync pinning becomes a rejection here, though retime clamps.** MAR-172
   §10.2's pin collapses a retime to zero movement, which is coherent because a
   retime is one shared delta. Under a scale, pinning one key and scaling the
   rest would produce a shape that is not `p + (t-p)·s` for any `s`. Rejecting is
   the same decision as §16.2, applied to the same underlying reason. The
   predicate itself is *extracted from* MAR-172's code rather than restated, so
   the two surfaces can never disagree about which keys are pinned.

10. **The range bar is a new strip, not an addition to the ruler.** The ruler
    already owns left-click scrubbing across its whole width; putting grips on it
    would make one pixel mean two things. A dedicated 14 px strip is drawn only
    when a multi-time selection exists, so the dopesheet's layout is byte-identical
    in every other state.

11. **The gesture keeps MAR-168's dead zone even though a grip is unambiguous.**
    A grip press has no second meaning, so arming immediately would have been
    defensible. Keeping the 4.0 px dead zone and the transaction-free candidate
    means a click on a grip cannot open a transaction that momentarily blocks
    every other authoring path, and it keeps one drag model across the graph and
    the dopesheet rather than two.

12. **Commit-time drift is enforced, not merely bounded.** §7.3's error analysis
    shows the incremental composition is safe by six orders of magnitude, but an
    argument in a document does not fail a build. Re-deriving every key's expected
    time at commit and cancelling on a deviation greater than `kKeyTimeEpsilon`
    turns the analysis into a runtime invariant for the cost of one loop per
    commit.

13. **The `.mbin` size is expected not to change, and that is stated loudly.**
    MAR-168 and MAR-169 shipped a fake export criterion whose tell was an
    unchanged byte size, and MAR-172's spec made a growing size the signal. For a
    pure retime the `.mbin` is fixed-width and *correctly* keeps its size, so
    reusing that signal here would train a reviewer to reject a correct result.
    §4.4 and §17.6 make the loaded key time the acceptance signal and demote size
    to a reported secondary observation, with the reason spelled out in the test's
    own output.

14. **`timeline.scale_key_times` is a new operation rather than an argument on
    `timeline.retime_keyframes`.** Criterion 5 asks for a registry operation with
    its own dry-run, affected-key, validation, mutation, undo, and export-preview
    behaviour. Overloading retime would give one operation two mutually exclusive
    required arguments (`delta` xor `scale`), two collision policies, and two
    response shapes.

15. **The retime selector parser is extracted rather than copied.** MAR-172 chose
    to add a new helper and leave the existing parsers byte-identical, because its
    selector shape genuinely differed (no `time`). MAR-173's shape is *identical*
    to retime's, so copying would create two definitions of the same six-kind
    grammar that would drift on the next family added. The extraction is
    behaviour-preserving, parameterized by the operation label so every existing
    message survives byte-for-byte, and gated by its own task in which
    `marrow_agent_dispatch_smoke` must pass **unchanged** before the new operation
    is written.

## 17. Validation Strategy

### 17.1 UI-free focused tests — `marrow_timeline_model_tests`

The natural home for the pure ratio math and the model-level helpers.

- `selection_time_span()` reports `valid == false` for an empty selection, a
  single key, and a multi-key selection whose keys all share one time within
  `kKeyTimeEpsilon`; reports the correct min/max/count across two tracks; and
  ignores refs that no longer resolve.
- `scale_from_edge_time()` returns the expected positive ratio for both pivot
  directions, `std::nullopt` for every non-finite input, for a degenerate span,
  for a target exactly on the pivot, and for a target past the pivot.
- `snap_scale_to_frames()` lands the moved edge exactly on a frame boundary for
  both pivots at 24, 30, and 60 fps; returns `std::nullopt` for a non-positive
  fps, a non-finite input, and a snapped target at or past the pivot; and its
  result composed back through `scale_from_edge_time()` round-trips within `1e-12`.
- `snap_scale_to_frames()` produces the **same** moved-edge time as calling
  `snap_delta_to_frames()` directly on the equivalent delta, proving the reuse is
  real rather than a re-derivation.
- `incremental_scale_ratio()` returns `requested / applied`, `std::nullopt` for
  non-finite or non-positive inputs on either side, and composes to the requested
  ratio within `1e-12` over 5000 synthetic frames.
- `family_key_spacing()` returns `0.0` for `Event` and `kNonEventKeySpacing` for
  the other five kinds, and the result matches the constant
  `include_resolved_retime_bounds()` passes for the same kind.

### 17.2 UI-free focused tests — `marrow_project_smoke`, primitive behaviour

Direct `scale_keyframe_times()` coverage, in a new
`validate_mar173_key_time_scaling(const ProjectLoadResult&)` beside MAR-169's,
MAR-170's, MAR-171's, and MAR-172's validators, called from `main` after
`validate_mar172_loop_boundary_sync()`.

- **Both pivots** on a three-key transform lane `{0.0, 0.5, 1.0}`:
  - `RangeStart`, `s = 1.25` → `{0.0, 0.625, 1.25}`, `pivot_time == 0.0`,
    `original_span == 1.0`, `scaled_span == 1.25`, `moved_key_count == 2`;
  - `RangeEnd`, `s = 0.5` → `{0.5, 0.75, 1.0}`, `pivot_time == 1.0`,
    `scaled_span == 0.5`, `moved_key_count == 2`;
  - `RangeEnd`, `s = 1.25` → the first key's target is `1.0 + (0.0 - 1.0)·1.25 =
    -0.25`, so the call is **rejected** by the non-negative check in step 8 with
    the project byte-identical. This is the concrete "push a key below zero" case
    and it is a rejection, not a clamp.
- **The pivot key is bit-identical**, compared with `==` on the stored `double`,
  for both pivots and for a selection with two keys sharing the pivot time.
- **Key order is unchanged**: the timeline's key sequence, compared index by
  index against the pre-call sequence with each element mapped through the
  formula, matches exactly.
- **Only `time` is written**: for a Transform key the `angle`/`x`/`y`, the four
  `interpolation` control points, and MAR-171's `curve_mode`/`curve_driver` are
  byte-identical; for a Slot Color key all four channels are; for a Deform key the
  whole `vertex_offsets` vector is.
- **`s = 1`** returns `changed == false`, an empty error, and a byte-identical
  `serialize_project()`.
- **A ratio that moves nothing** (`s = 1 + 1e-15` on a 0.001 s span) also returns
  `changed == false` with a byte-identical project.
- **Every rejection leaves `serialize_project()` byte-identical**: `s = 0`,
  `s = -1`, `s = NaN`, `s = inf`, an empty selector list, a single-key selection,
  an all-same-time selection, a duplicate selector, an unresolvable selector, a
  selector set naming two animations, a non-event collision, an intrusion into an
  unselected neighbour on the left, the same on the right, a target below zero,
  and a target beyond the float32 range.
- **The `min(spacing, original_gap)` rule**: a lane with an authored 0.4 ms gap
  accepts `s = 1.5` on that pair and rejects `s = 0.9`; a lane with a 10 ms gap
  rejects the `s` that would take it to 0.9 ms and accepts the `s` that takes it
  to 1.0 ms.
- **Event ties**: a two-key tie and a three-key tie both scale to bit-identical
  times; a selection naming one member of a tie is rejected with the tie message
  and a byte-identical project; a selected event key landing exactly on an
  unselected event key is **accepted**.
- **Non-event families**: one case each for Transform, Deform, Slot Color, Slot
  Attachment, and Draw Order, asserting the 1 ms rule applies and the values are
  untouched.
- **MAR-171 interaction**: scaling **every** key of a track whose keys are `Auto`
  leaves the resolved control points byte-identical (`resolved_key_count == 0`),
  because the normalized tangents depend only on ratios of `Δt`; scaling a
  **subset** changes them. Both asserted.
- **MAR-172 interaction**: a selection containing the first key of an opted-in
  lane rejects with the pin message; the same for the last key; a selection of
  only middle keys of an opted-in lane scales normally and the following
  `synchronize_loop_boundaries()` reports `synchronized_lane_count == 0`.
- **Save/reload**: scale, `save_project()`, reload, and assert every scaled time
  survives bitwise through the `.marrow` round trip.

### 17.3 UI-free focused tests — `marrow_timeline_model_tests`, gesture algebra

- `completion_decision(true, false)` yields `CompletionAction::Cancel` with zero
  history entries — the `s = 1` gesture case.
- `completion_decision(true, true)` yields `Commit` with one entry.
- `completion_decision(false, true)` yields `Cancel` with `report_cancelled`.

### 17.4 UI-free focused tests — `marrow_project_smoke`, duration

- **Grow**: an explicit duration of 1.0 with keys at `{0.0, 0.5, 1.0}` scaled by
  `s = 2.0` about `RangeStart` grows to 2.0 through
  `auto_extend_explicit_animation_durations()`, and the growth and the scale are
  in the same project state.
- **Never shrink**: the same animation scaled by `s = 0.5` keeps its explicit
  duration at 1.0 while the keys move to `{0.0, 0.25, 0.5}`.
- **Inference-driven**: an animation with no explicit duration gains none, and
  its `inferred_duration()` follows the scaled keys.

### 17.5 Headless shell smoke — `src/editor/shell_smoke_graph.cpp`

A new `validate_timeline_scale_shell_smoke()` scenario with an isolated session
and its own `ScopedPreferenceIsolation`, declared in `shell_smoke_scenarios.hpp`
and called from `shell_smoke.cpp` after MAR-172's scenario:

- a complete drag on the **late** grip: one history entry, the pivot key's time
  bit-identical throughout, `selected_keys` and `active_key` resolving on every
  frame, `applied_scale` matching the pixel target divided by the frozen
  `pixels_per_second`;
- the same on the **early** grip, asserting the opposite pivot;
- **snap on**: the moved edge lands exactly on a frame boundary at 60 fps;
  **Alt bypass**: the same drag lands off it;
- **a rejected frame holds the gesture**: drag inward until a collision rejects,
  assert the gesture is still live, `timeline_scale_rejection()` is non-empty, the
  project equals the last accepted state, and then drag back outward and assert
  the gesture accepts again and `timeline_scale_rejection()` is empty;
- **press without motion** produces no transaction and no history entry;
- **a zero-net drag** (out and exactly back) commits nothing;
- **explicit-duration auto-grow** in the same transaction and the same undo entry;
- **MAR-172 interaction**: with an opted-in lane, arming on a selection
  containing its boundary key is refused with a status message and no
  transaction; a legal middle-key scale commits one entry and leaves the boundary
  key at the duration;
- **MAR-171 interaction**: with `Auto` keys on the scaled track, one history
  entry contains both the scaled times and the re-resolved curves;
- **cancel paths** — Escape, `cancel_authoring_gestures()`, a track disappearing
  mid-gesture, and a forced non-finite pointer — each restoring
  `serialize_project()`, `undo_count()`, `redo_count()`, `project_revision()`,
  `dirty()`, the rebuilt dopesheet `key_times`, and `selected_keys`;
- **undo/redo**: undo of a committed scale restores the times and yields the
  **deterministic reconciled selection** (asserted by value, not by bit-identity,
  per §10.5); redo restores the scaled times;
- **drift enforcement**: 5000 successive `apply_timeline_scale_ratio()` calls
  sweeping the ratio, then a commit, asserting the commit path accepted and every
  final time is within `kKeyTimeEpsilon` of `pivot + (original - pivot) *
  applied_scale`;
- **`TimelineEditorState{}` source adoption** clears `scale_drag` and
  `scale_gesture`;
- **mutual exclusion**: `begin_timeline_retime_gesture()` refuses while a scale
  gesture is live, and `begin_timeline_scale_drag()` refuses while a retime
  gesture is live;
- `agent_operation_descriptor_count() == 60` before and after every case.

### 17.6 Export — the criterion this story must not fake

Per §4.4, the MAR-173 export block in `marrow_project_smoke` must:

1. take the project it just **mutated** — `idle`/`spine`/`rotate` keys
   `{0.0, 0.5, 1.0}` scaled about `RangeStart` by `s = 1.25` to
   `{0.0, 0.625, 1.25}`;
2. call `export_runtime_assets()` on **that** project into
   `/tmp/marrow_mar173_scale.mskl` and `/tmp/marrow_mar173_scale.mbin`;
3. load the exported `.mskl` with `runtime::load_skeleton_data()` and assert the
   `spine` rotate timeline's three key times are exactly
   `float32(0.0)`, `float32(0.625)`, `float32(1.25)`, that the three `angle`
   values are bit-identical to the pre-scale ones, and that the three
   `interpolation` records are bit-identical;
4. read the exported `.mskl` as **text** and assert it contains `0.625` and no
   longer contains the pre-scale `"time": 0.5` for that timeline;
5. validate the `.mbin` against the `.mskl` with the existing
   `validate_binary_export()` helper, and run
   `./build/marrow_inspect --compare` on the pair as a separate gate;
6. print
   `MAR-173 scale export: JSON <n> bytes, MBIN <m> bytes (baseline JSON 14336 bytes, MBIN 3984 bytes).`
   followed, on its own line, by
   `MAR-173 note: the MBIN size is expected to be unchanged — key times are fixed-width float32. The acceptance signal is the loaded key time asserted in step 3, not the byte count.`

**The expected observable difference.** The `.mskl` must be **strictly larger**
than the 14336 baseline, because `0.5` widens to `0.625` and `1` widens to `1.25`.
The `.mbin` size is expected to be **unchanged at 3984**, and that is correct.
A reviewer seeing `14336` in the MAR-173 JSON row must treat it as the
MAR-168/169 defect recurring; step 3 must fail before any byte count is consulted.

### 17.7 Agent smoke — `marrow_agent_dispatch_smoke`

- the registry is exactly 60 operations, the new row's metadata is (`edit`,
  mutating, not review, dry-run supported), and it sits immediately after
  `timeline.set_loop_sync` in `kExpectedOperations`;
- **the extraction is behaviour-preserving**: every existing
  `timeline.retime_keyframes` case, including all of its rejection cases, passes
  **unchanged** after `timeline_key_selectors_arg()` is introduced;
- `timeline.scale_key_times` dry run leaves `project_revision()`, `undo_count()`,
  and `dirty()` unchanged and reports the correct `pivot_time`, `applied_scale`,
  `original_span`, `scaled_span`, and per-key `previous_time` / `time` / `moved`;
- a live call mutates, reports `moved_key_count`, and adds exactly one history
  entry;
- **both pivots** produce the documented, different results for the same `scale`;
- a second identical live call returns `no_change`;
- `snap: true` with `frames_per_second: 60` lands the moved edge on a frame
  boundary and reports an `applied_scale` different from `requested_scale`;
- **export preview**: `export.preview` before and after a live scale returns
  different payloads, and one `undo` restores the earlier payload;
- `undo` restores every key time, verified by a follow-up dry run's
  `previous_time` values;
- rejection cases, each with the project provably unchanged: missing `scale`,
  non-numeric `scale`, `scale: 0`, `scale: -1`, missing `pivot`,
  `pivot: "middle"`, an empty `keys` array, a duplicate key, an unresolvable key
  (expect `not_found`), a single-key selection, keys from two animations, a
  non-event collision, an intrusion into an unselected neighbour, a partial event
  tie, a loop-synchronization pinned key, and `snap: true` with
  `frames_per_second: 0`;
- the `keys` echo is capped at 256 with `keys_truncated` set, while `key_count`
  still reports the whole call.

### 17.8 MCP — `tools/mcp/test_client.py`

- `len(registry_names) == 60`, `len(mcp_names) == 60`,
  `set(registry_names) == set(mcp_names)`, all names unique;
- `timeline.scale_key_times` present in `new_edit_operations` and in the MCP edit
  tool set, with its registry metadata row asserted explicitly;
- dry run → live → read-back → `undo` → read-back, proving the scaled times are
  applied, reported to four decimal places, and restored;
- both pivots exercised through the MCP surface with the same `scale`, asserting
  the two different results;
- rejection of `"scale": -1`, of `"scale": "1.5"`, of `"pivot": "middle"`, of a
  missing `pivot`, and of a single-key selection — proving the schema did not
  loosen the C++ gate;
- removing the new MCP tool must make the client fail on
  `assert len(mcp_names) == 60`, and that must be verified once by hand.

### 17.9 Display and compatibility gates

See the "Full verification checklist" in
`docs/superpowers/plans/2026-08-30-mar-173-atomic-key-time-scaling.md`. MAR-173
adds ImGui code (the range bar) but no new display-labelled test; the existing
display gates must pass unchanged. Automated display tests prove the exercised
ImGui/display path only and add no manual-visible-UI, Windows 11, or
physical-input qualification credit.

## 18. Documentation and Milestone Closure

After code and every required gate pass:

- add a `MAR-173 Atomic Key Time Scaling Validation Results` section to
  `AGENTS.md` in the existing table plus command-output format, and update
  `Current Validation`'s registry line to 60 operations;
- update `AGENTS.md`'s `Project State` line so MAR-173 is complete and MAR-174 is
  next;
- update `docs/root1/discription.md`, `quick-start.md`, `concepts.md`,
  `editing-gap-analysis.md`, `refector.md`, and `platform-validation.md` where
  the "MAR-173 is next" wording, the registry size, the edit-operation count, or
  the timeline-authoring contract is now stale, applying §13's
  historical/current rule to every hit;
- `docs/root1/concepts.md` gains a paragraph on why scaling rejects where
  retiming clamps, and on the pivot being the selection's own opposite edge;
- `docs/root1/format-spec.md` needs **no change** — MAR-173 adds no `.marrow`
  field. Confirm this rather than assume it;
- mark `MAR-173` done with the verified completion date and leave `MAR-174` open;
- leave MAR-192 through MAR-210 open and add no platform qualification credit;
- change this document's status to `Implemented and validated` only after every
  gate is green.

## 19. Commercial-Tool Reference Boundary

Selection-range scaling is standard in 2D animation tooling; the interaction
direction is informed by the official documentation:

- Spine Dopesheet, selection and key manipulation:
  <https://us.esotericsoftware.com/spine-dopesheet>
- Live2D Cubism, timeline key operations:
  <https://docs.live2d.com/en/cubism-editor-manual/timeline-palette/>

Marrow deliberately does **not** copy a free numeric pivot, a scale that silently
clamps at collisions, negative-ratio time reversal, per-key frame quantization, or
value scaling on the same handle. The fixed opposite-edge pivot, the reject-not-
clamp policy, the positive-ratio domain, and the edge-only frame snap take
precedence over surface similarity to another editor.
