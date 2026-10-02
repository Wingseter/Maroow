# MAR-172 Loop Boundary Key Synchronization Design

**Date:** 2026-08-30

**Status:** Implemented and validated

**Authority:** `.agents/tasks/prd-marrow-runtime.json` story `MAR-172` and
`docs/root1/discription.md`

**Depends on:** MAR-171, whose per-keyframe `curve_mode` / `curve_driver`
metadata, `resolve_automatic_curves()` resolver, and demotion-inside-
`set_keyframe_interpolation()` rule are defined in
`docs/superpowers/specs/2026-08-30-mar-171-project-local-automatic-curve-handles-design.md`.
MAR-171 depends on MAR-170's preset table and preference boundary
(`docs/superpowers/specs/2026-08-30-mar-170-curve-presets-and-defaults-design.md`),
which depends on MAR-169's interpolation primitive and segment-wide curve
identity
(`docs/superpowers/specs/2026-08-30-mar-169-graphical-bezier-handles-design.md`),
which inherits MAR-168's gesture and transaction model
(`docs/superpowers/specs/2026-08-30-mar-168-graph-key-time-value-editing-design.md`).

> **MAR-171 is not implemented while this document is written.** Its spec and
> plan exist as untracked files; `git log` shows `0a917db` (MAR-170) as the last
> feature commit and the registry is **57** rows in
> `src/editor/agent_dispatch.cpp` today. Every MAR-171 contract quoted below is
> quoted from its *spec*, not from as-built source. The implementation plan's
> **Task 0 is mandatory** and must reconcile against the code that actually
> exists, including the real registry count. §4.4 records the specific MAR-171
> facts MAR-172 depends on and what MAR-172 does if any of them landed
> differently.

## 1. Goal

Marrow's runtime holds a timeline's last value from that key until the clip
ends, then a looping `TrackEntry` wraps `track_time` back through zero
(`src/runtime/animation_state.cpp:874-978`). So a clip whose explicit duration
is 1.5 s and whose `spine` rotate lane ends at 1.0 s holds `angle = -2` from
1.0 s to 1.5 s and then snaps to `angle = 0`. That snap is the pop every
animator hand-fixes by copying the first key to the end of the clip and then
re-copying it every single time the first key changes.

MAR-172 makes that copy the editor's job. A lane can be **opted in** to loop
synchronization; the editor then guarantees, on every transaction, that the lane
carries exactly one **managed boundary key** at the clip's explicit duration
whose value and easing record are a bit-exact copy of the lane's key at time
zero.

Concretely, MAR-172 adds exactly five things and nothing else:

1. One optional, additive, **`.marrow`-only** lane flag — `loop_sync` — absent
   from every existing project and therefore leaving every existing project
   byte-identical (§8).
2. A precise **boundary contract** (§6) and the prerequisites under which it is
   expressible at all: an **explicit** clip duration and a key exactly at time
   zero (§6.2).
3. **`synchronize_loop_boundaries()`**, run inside the caller's already-open
   transaction at the one seam that is provably after every duration change —
   `EditorSession::refresh_runtime()` and `EditorSession::commit()` (§9).
4. **Managed identity rules** that stop selection, clipboard, retime, insertion,
   and duration edits from producing a duplicate or stale boundary key (§10).
5. One new Agent operation, `timeline.set_loop_sync`, and its matching MCP tool,
   taking the registry from MAR-171's 58 to exactly **59** (§13).

The runtime never learns any of this. The managed boundary key is an ordinary
keyframe in `.mskl` and `.mbin`; the `loop_sync` flag is never exported (§8.6).

## 2. Approved Product Decisions

1. **Loop sync is per lane**, not per key and not per project. The story's
   criterion 1 says "Optional additive **lane** metadata"; the boundary contract
   is a statement about one timeline's first and last key, which is exactly a
   lane-scoped property (§8.2).
2. **Managed identity is derived, never stored.** On an opted-in lane, the
   managed boundary key *is* the key at `t == float32(duration)`, which is by
   construction that lane's last key. No per-key marker is added, so no marker
   can travel through a copy/paste into a lane where it would be a lie (§6.4).
3. **Synchronization is one-directional: first key → boundary key.** The key at
   time zero is authored; the boundary key is derived. The first key always wins
   on conflict, and a direct edit of a boundary key is skipped by the GUI and
   rejected by the Agent rather than propagated backwards (§6.3, §10).
4. **The boundary key mirrors the first key exactly**: every value component,
   the shared `interpolation`, and — when MAR-171 has landed — `curve_mode` and
   `curve_driver`. One sentence, no exceptions (§6.3).
5. **Only the three continuous families can be opted in**: Transform (all four
   channels), Slot Color, and Deform. Draw Order, Event, and Slot Attachment are
   piecewise-constant, already wrap without a pop, and an Event key at the
   boundary would fire twice per loop. Their lane structs gain no field, so the
   exclusion is compile-enforced (§7).
6. **The prerequisites are an explicit duration and a key at time zero**, as the
   story states. With an *inferred* duration the boundary key would define the
   duration that defines the boundary key; the recursion has no fixed point and
   the prerequisite is what removes it (§6.2).
7. **Synchronization runs at the session seam**, immediately after
   `auto_extend_explicit_animation_durations()` inside `refresh_runtime()` and
   `commit()`. That is the only position provably after every duration change,
   including the automatic growth the session performs on the caller's behalf
   (§9.1). It never runs at load, save, export, `rebuild_runtime()`, or on a
   timer (§9.5).
8. **Disabling always succeeds.** Clearing the flag evaluates no prerequisite
   and touches no key, so a project that reaches an unsatisfiable state always
   has a one-command escape. The boundary key is left in place as an ordinary,
   fully editable key rather than deleted (§6.6).
9. **A violated invariant fails the whole transaction.** A lane that cannot
   satisfy its contract rejects atomically and cancels the enclosing
   transaction, exactly as MAR-171's resolver does for a zero-duration segment.
   Partial synchronization is the one outcome that silently produces the
   staleness this story exists to remove (§11).
10. One new Agent operation, `timeline.set_loop_sync`, brings the registry to
    exactly **59**. It is purely additive except for one documented change to
    `set_animation_duration()`'s inferred-duration floor, which is a strict
    no-op for every animation with no opted-in lane (§9.3).
11. Selected-key time scaling (MAR-173) and preview playback speed (MAR-174)
    remain out of scope.

## 3. Scope

### 3.1 Surfaces that gain loop-sync authoring

| Surface | Target | Mechanism |
| --- | --- | --- |
| Agent `timeline.set_loop_sync` | the caller's explicit lane selectors | absolute enabled/disabled |
| MCP `timeline.set_loop_sync` | same | same |

That is the complete list. **MAR-172 adds no ImGui widget.** The story's
criterion 5 says "The **UI-free** loop-sync operation", and no criterion
mentions a toolbar, a button, a render statistic, or a visual treatment —
unlike MAR-170's criterion 5 ("GUI, C++ agent, and Python MCP preset
application share the interpolation operation") and MAR-171's criterion 4
(handle dragging). MAR-172 is a data-model and invariant story with a scripted
authoring surface. §16 records the deferred visual treatment and where it would
go.

### 3.2 Surfaces that gain automatic maintenance

Every `EditTransaction` maintains the contract, because the maintenance lives in
`EditorSession::refresh_runtime()` and `EditorSession::commit()` rather than in
a list of call sites (§9.1). That includes MAR-168's value drag, MAR-168's
retime, MAR-169's handle drag, MAR-170's presets, MAR-171's curve-mode
application, key insertion, key removal, cut, paste, duration authoring, and
every Agent editing operation, with no per-site wiring at all.

### 3.3 Explicit exclusions

MAR-172 does not add:

- selected-key time scaling (MAR-173) or preview playback speed (MAR-174);
- loop sync for Deform *drivers*, per-component boundaries, or an incoming-side
  boundary. There is one boundary key per opted-in lane;
- a **project-level or animation-level** "loop" flag. Whether a clip loops at
  runtime is a `TrackEntry` property the playing application sets
  (`AnimationState::set_animation(track, name, loop)`); MAR-172 records only
  that a lane's data should be *shaped* for looping, which is an authoring
  intent independent of any particular playback call;
- automatic opt-in. Every existing project, and every newly created lane, starts
  opted out (§8.3);
- resolution or repair at load, save, export, `rebuild_runtime()`, or on a timer
  (§9.5);
- deletion of the boundary key on disable (§6.6);
- any change to MAR-169's clamp/reject split, MAR-170's preset table or
  preference flow, MAR-171's Fritsch–Carlson resolver, its demotion rule, or its
  `curve_mode` / `curve_driver` parse and serialize;
- relocation of MAR-171's per-call-site `resolve_automatic_curves()` invocations
  (§9.2 explains why they become a harmless redundant first pass and why moving
  them is out of scope);
- a change to `timeline.describe`'s payload, to `timeline.retime_keyframes`'s
  arguments, or to `timeline.set_interpolation`'s arguments;
- manual-visible-UI, Windows 11, or physical-input qualification credit.
  MAR-192 through MAR-210 stay open.

### 3.4 Compatibility boundaries

MAR-172 changes none of the following:

- **`.mskl` v1.** The managed boundary key is an ordinary keyframe in the family
  the lane already writes. No new field, no new encoding, unchanged reader and
  writer (`src/runtime/skeleton_parse.cpp`);
- **`.mbin` v2.** Unchanged reader and writer (`src/runtime/binary.cpp`);
- **C ABI v1.** `include/marrow/c_api/**` and `src/c_api/**` are untouched;
- **`editor-settings.json` v1.** `kEditorSettingsVersion` stays `1`;
  `src/editor/preferences.cpp` and `include/marrow/editor/preferences.hpp`
  change by zero bytes;
- **`src/runtime/**` and `include/marrow/runtime/**`** in their entirety.
  `AnimationData::inferred_duration()` and `duration()` are read, never changed;
- MAR-168's point drag, MAR-169's handle geometry and clamp/reject split,
  MAR-170's preset constants, MAR-171's resolver and demotion rule.

The **`.marrow` change is strictly additive**: one optional top-level
`loop_sync` object, absent unless a lane is opted in. A project with no opted-in
lane serializes byte-identically to today — this is a testable property, not a
claim (§18.2). A build that has never heard of `loop_sync` loads such a project
unchanged, because unknown top-level members ride through
`ProjectData::preserved_root` (`project.cpp:4109`) and the managed boundary key
is an ordinary keyframe it already understands. **The clip still loops
correctly in an older build; it simply stops being maintained.** That graceful
degrade is the reason §8.2 rejects putting the flag inside the lane's own JSON
value.

The public-header changes are additive only:

- `include/marrow/editor/project.hpp` gains one `bool loop_sync{false}` member
  on `TransformTimelineEdit`, `SlotColorTimelineEdit`, and
  `MeshDeformTimelineEdit`;
- `include/marrow/editor/authoring.hpp` gains one selector struct, two result
  structs, and three functions appended after MAR-171's accessors, plus one
  defaulted parameter-free behaviour change documented on
  `set_animation_duration()`.

## 4. Existing Boundaries Reused

### 4.1 Reuse table

| Concern | Reused primitive | Location |
| --- | --- | --- |
| Optional additive project-local block | `ProjectSnapSettings` / `.marrow.snap` | `project.hpp:294`, `project.cpp:305,4193,4757` (MAR-165) |
| Unknown top-level member preservation | `ProjectData::preserved_root` | `project.hpp:498`, `project.cpp:4109` |
| Path-scoped load rejection | `validation_error()`, `find_optional_member()` | `project.cpp` |
| Save-time re-validation | `validate_project_for_save()` | `project.cpp:4757` |
| Lane materialization | `ensure_transform_timeline_edit()`, `ensure_slot_color_timeline_edit()`, `ensure_mesh_deform_timeline_edit()` | `project.hpp:689-723` |
| Shell lane materialization | `ensure_*_timeline_edit_index()` | `timeline_controller.hpp` |
| Key time epsilon and non-event spacing | `kKeyTimeEpsilon` (1e-6), `kNonEventKeySpacing` (0.001) | `timeline_model.hpp:20-21` |
| Retime bound accumulation | `include_retime_bounds()`, `include_resolved_retime_bounds()` | `timeline_model.hpp:288`, `authoring.cpp:443` |
| Duration authoring and its float32 normalization | `set_animation_duration()` | `authoring.cpp:1554` |
| Automatic duration growth | `auto_extend_explicit_animation_durations()` | `authoring.cpp:1635` |
| Per-animation timeline maximum | `include_animation_timeline_maximum()` | `timeline_model.hpp:265` |
| Easing read/write without demotion | `read_key_interpolation()`, `write_key_interpolation()`, `same_interpolation()` | `authoring.cpp:1884-1949` |
| Automatic curve resolution | `resolve_automatic_curves()` | `authoring` (MAR-171) |
| Live preview / one history entry | `EditorSession::EditTransaction` | `session.cpp` |
| Transaction-scoped duration growth and its rollback | the `auto_extend_...` blocks in `refresh_runtime()` / `commit()` | `session.cpp:1317,1413` |
| Agent dry-run / commit / no-change shape | `commit_or_error()`, `CommitPolicy` | `agent_dispatch_internal.hpp:35-71` |
| Agent per-entry echo and 256 cap | the `response_delta` lambda | `agent_handlers_editing.cpp:1035-1086` |
| Agent error classification | `classify_timeline_key_error()` | `agent_handlers_editing.cpp:172-175` |

MAR-172 introduces exactly two new concepts — the lane flag and the boundary
contract — and reuses everything else.

### 4.2 The one existing contract that changes

`set_animation_duration()`'s inferred-duration floor is computed with **managed
boundary keys excluded**. A loop-synchronized lane's last key sits at the
explicit duration by construction, so letting it act as a floor on the duration
it follows would make an opted-in clip permanently un-shortenable: the duration
could never drop below the boundary key that only exists because of the
duration. §9.3 states the replacement and proves it is a bit-exact no-op for
every animation with no opted-in lane.

`auto_extend_explicit_animation_durations()` gains the same exclusion, for the
same reason and with the same no-opt-in guarantee.

### 4.3 What deliberately does *not* change

- `retime_keyframes()`'s signature. The pinning rule (§10.2) is expressible with
  `ProjectData` alone, because on an opted-in lane the first key and the boundary
  key are the *first* and *last* elements of `keyframes` by construction — the
  rule needs no duration lookup. MAR-173 inherits an unchanged signature.
- `insertable_key_time()` and `clamp_existing_key_time()`. Their `duration`
  parameter stays `(void)`-ignored. Insertion past the boundary is caught by the
  sync pass's own validation (§11), not by a second, duplicate rule.
- `paste_keys_replace_collisions()`. A paste that lands exactly on the boundary
  time replaces the boundary key and the sync pass rewrites its value back in the
  same transaction; a paste past the boundary is rejected by the sync pass
  (§10.4).

### 4.4 MAR-171 facts MAR-172 depends on, and the fallback if they differ

| MAR-171 fact | MAR-172 use | If it landed differently |
| --- | --- | --- |
| `resolve_automatic_curves(ProjectData*, std::string_view)` exists | called between the sync's two phases (§9.2) | call whatever the as-built resolver is named, with whole-project scope; if MAR-171 is not merged at all, **stop and report** — MAR-172 `dependsOn` it |
| `TransformKeyframeEdit` / `SlotColorKeyframeEdit` carry `curve_mode` and `curve_driver` | mirrored onto the boundary key (§6.3) | mirror only `interpolation`; the contract sentence loses two words and nothing else |
| `DeformKeyframeEdit` carries **no** curve mode | the Deform boundary key mirrors value + `interpolation` only | unchanged either way |
| `set_keyframe_interpolation()` demotes to `Manual` | the sync's phase 2 must **not** call it (§9.2) | still must not call it; phase 2 writes through `write_key_interpolation()` regardless |
| The registry is 58 | MAR-172 makes it 59 | use the count Task 0 measures, plus one |
| MAR-171 §18.3's duration assertion is `resolved_key_count == 0` | scoped to "no opted-in lane" and joined by a new opted-in case (§18.4) | if the assertion is worded differently, keep it and add the new case beside it |

## 5. Architecture

```text
include/marrow/editor/project.hpp
    bool loop_sync on TransformTimelineEdit / SlotColorTimelineEdit /
                       MeshDeformTimelineEdit          (three bools, nothing else)
                      |
                      v
src/editor/project.cpp
    parse_loop_sync()                 <- after timeline_edits, cross-referenced
    build_loop_sync_value()           <- pure projection of the three lane vectors
    validate_project_for_save()       <- first key must be at time zero
    build_runtime_document()          <- UNCHANGED
                      |
                      v
include/marrow/editor/authoring.hpp / src/editor/authoring.cpp
    TimelineLaneSelector                        (new; lane granularity)
    set_timeline_loop_sync()                    <- writes intent, then synchronizes
    synchronize_loop_boundaries()               <- the contract, enforced
    inferred_duration_excluding_loop_boundaries()
    set_animation_duration()                    <- uses the excluding floor
    auto_extend_explicit_animation_durations()  <- excludes boundary keys
    retime_keyframes()                          <- pins first and last on an opted-in lane
                      |
        +-------------+--------------------------------+
        |                                              |
        v                                              v
src/editor/session.cpp                       src/editor/agent_handlers_editing.cpp
    refresh_runtime():                           timeline.set_loop_sync      (NEW)
        auto_extend_...()                        every other operation inherits the
        synchronize_loop_boundaries()   <- HERE  session seam with zero wiring
        build_project_runtime()
    commit(): same two steps, same order                 |
                      |                                  v
                      v                        tools/mcp/tools/editing.py
src/editor/timeline_controller.cpp                 timeline.set_loop_sync    (NEW)
    copy_selected_timeline_keys()  <- clears the flag on the clipboard fragment
    (no other controller change)
```

The whole maintenance story is two lines in `session.cpp`. That is the point:
criterion 3 asks for "in the same transaction", and a rule enforced at the
transaction boundary cannot be forgotten by a future call site, whereas MAR-171's
twelve-call-site table can.

## 6. The Boundary Contract

### 6.1 What the runtime actually does at a loop

From `src/runtime/animation_state.cpp`. A looping `TrackEntry` wraps
`track_time` modulo `animation->duration()`, and `duration()` is
`explicit_duration.value_or(inferred_duration())`
(`src/runtime/skeleton_animation.cpp:665`). `inferred_duration()` is the maximum
last-key time across every timeline of the animation
(`skeleton_animation.cpp:629`). Sampling a continuous timeline past its last key
returns that key's value.

So for a lane with keys at `t[0] < ... < t[n-1]` and a clip duration `D`:

```text
value(t) for t in [t[n-1], D]  ==  v[n-1]          (held)
value(0)                       ==  v[0]            (after the wrap)
```

The loop is continuous **iff `v[n-1] == v[0]`**, and it is continuous *and* has
no dead interval iff additionally `t[n-1] == D` and `t[0] == 0`.

### 6.2 Prerequisites, and why they are exactly these

> A lane may be opted in only when its animation has an **explicit** duration
> `D` with `float32(D) >= kNonEventKeySpacing`, and the lane has a key at
> `t == 0` within `kKeyTimeEpsilon`.

**Why explicit and not inferred.** With an inferred duration, `D` *is* the
lane's last key time. Placing a key at `D` is then a no-op, moving any key
changes `D`, which moves the boundary, which changes `D`. The recursion has no
fixed point and no honest resting state. Requiring an explicit duration makes
`D` an authored constant that the boundary follows, which is the only
formulation in which "the managed boundary key is at the duration" is a
statement rather than a definition. The story's criterion 1 names this
prerequisite for exactly this reason.

**Why a key at time zero.** The contract's source is "the value at the start of
the clip". Without a key at `t == 0` that value is the *first* key's value held
backwards, and the boundary would have to mirror a key whose own time is
arbitrary — so a later insertion of a real key at 0 would silently change what
the boundary mirrors. Requiring `t[0] == 0` makes the source unambiguous and
stable.

**Why `D >= kNonEventKeySpacing`.** The boundary key and the time-zero key are
two distinct keys on the same lane and the project's non-event spacing rule is
1 ms. A duration below that has no room for both.

**A lane opted in always satisfies its prerequisites.** That invariant is
established at enable time (§6.5) and maintained by the retime pinning (§10.2),
the sync pass's own validation (§11), and the fact that disabling is always
possible (§6.6). Every other rule in this document may assume it.

### 6.3 The contract

> **For every opted-in lane, with `B = float32(D)`:**
>
> 1. the lane's **last** key has `time == B` exactly;
> 2. that key's every value component equals the lane's **first** key's
>    corresponding component, bit for bit;
> 3. that key's `interpolation` equals the first key's `interpolation`, bit for
>    bit — and, when MAR-171 has landed, its `curve_mode` and `curve_driver`
>    too;
> 4. no other key of the lane has `time >= B - kNonEventKeySpacing`.

The copied components per family:

| Family | Components copied |
| --- | --- |
| Transform, channel Rotate | `angle` |
| Transform, channel Translate / Scale / Shear | `x`, `y` |
| Slot Color | `color.r`, `color.g`, `color.b`, `color.a` |
| Deform | the whole `vertex_offsets` vector |

**Why the easing is mirrored even though the runtime never reads it.** The
boundary key is the lane's last key, so its outgoing easing is never evaluated.
Three reasons to mirror it anyway. It makes the contract one sentence — "a
bit-exact copy of the first key with `time = B`" — instead of a rule plus an
exception. It is the semantically honest value: at the wrap, the segment leaving
`B` *is* the segment leaving `0`. And it survives a later extension of the clip:
if a key is appended after `B`, the boundary key acquires a real outgoing
segment and, with MAR-171's `curve_mode` mirrored too, resolves correctly on the
next transaction instead of freezing a stale absolute curve.

**Why the direction is first → last and never the reverse.** The first key is
what an animator authors and what every other tool in the editor treats as the
clip's opening pose. The boundary key exists only because the flag is set.
Making the sync symmetric would mean a stray drag at the clip's end silently
rewrites its beginning, and would need a conflict rule where there is no
principled winner. One direction, first key always wins (§10).

### 6.4 Managed identity is derived, not stored

> On an opted-in lane, the managed boundary key is the key at `t == B`, which by
> contract clause 4 is that lane's **last** key.

No per-key marker is added. That is a correctness decision, not an economy:

- **Copy/paste cannot carry a lie.** A stored marker copied onto a key pasted at
  0.3 s in another animation would claim a boundary that is not one, and every
  rule keyed on the marker would then be wrong. A derived identity has nothing
  to carry.
- **Adoption is free.** Criterion 2 says the enable path "creates or **adopts**"
  the managed key. With a derived identity, an existing key at `B` simply *is*
  the boundary key the moment the flag is set; adoption is the absence of code.
- **Undo, redo, and load need no repair.** Identity is recomputed from `(flag,
  duration, key times)`, all of which the project already stores and history
  already restores.
- **The rules that need identity have it.** Retime pinning needs "first or last
  index of an opted-in lane" (§10.2); the sync pass needs "the key at `B`, or
  none" (§11). Both are available from `ProjectData` alone.

### 6.5 Enable: create or adopt

`set_timeline_loop_sync(enabled = true)` on a lane, evaluated on a candidate
copy:

| Situation | Action | Reported |
| --- | --- | --- |
| No key within `kKeyTimeEpsilon` of `B` | insert a key at `B` mirroring key 0 | `created` |
| Exactly one key at `B`, already the last key | overwrite its value and easing from key 0 | `adopted` |
| A key strictly after `B + kKeyTimeEpsilon` exists | **reject** the whole call | — |
| A non-boundary key in `(B - kNonEventKeySpacing, B)` | **reject** the whole call | — |
| Two or more keys within `kKeyTimeEpsilon` of `B` | **reject** the whole call | — |
| No key at `t == 0` | **reject** the whole call | — |
| The animation has no explicit duration | **reject** the whole call | — |
| `float32(D) < kNonEventKeySpacing` | **reject** the whole call | — |
| The flag was already set and the contract already holds | no change | `unchanged` |

Adoption overwrites an authored key's value. That is deliberate and is the
story's own word: the animator asked for the last key to become the loop
boundary, and the boundary's value is defined by the contract. It is one undo
entry and the dry run reports both the previous and the resulting value, so the
overwrite is visible before it happens.

### 6.6 Disable

`set_timeline_loop_sync(enabled = false)`:

- clears `loop_sync`;
- **evaluates no prerequisite** — not the duration, not the time-zero key, not
  the spacing. A project that has reached an unsatisfiable state (a hand-edited
  document, a base skeleton whose duration was removed) must always have an
  escape, and this is it;
- **leaves the boundary key in place**, as an ordinary, fully editable key.
  Reported as `released`.

**Why not delete it.** The boundary key is a real keyframe that shapes the
lane's last segment and appears in the export. Deleting it as a side effect of
clearing a flag would change the exported pose and destroy data the animator may
have come to depend on. Enable → disable is therefore **not** byte-symmetric,
which is the intended trade: never lose keyframe data to a flag. The animator
deletes the key themselves if they want the pre-opt-in shape back.

### 6.7 A worked example on the checked-in fixture

`assets/fixtures/player_idle.marrow`, animation `idle`, bone `spine`, channel
`rotate`. The stored lane today:

| i | `time` | `angle` | `curve` |
| ---: | ---: | ---: | --- |
| 0 | 0.0 | 0 | `[0.330000013113022, 0, 0.670000016689301, 1]` |
| 1 | 0.5 | 8 | `"stepped"` |
| 2 | 1.0 | −2 | `"linear"` |

`idle` has **no** explicit duration in `assets/fixtures/player_idle.mskl` and no
`animation_edits` array in the `.marrow`, so its duration is inferred as 1.0.
Opting in therefore **fails the prerequisite** — which makes this fixture a
direct test of criterion 6's "missing prerequisites" rather than an obstacle.

After `set_animation_duration(project, skeleton, "idle", 1.5)`:

| i | `time` | `angle` | `curve` |
| ---: | ---: | ---: | --- |
| 0 | 0.0 | 0 | `[0.330000013113022, 0, 0.670000016689301, 1]` |
| 1 | 0.5 | 8 | `"stepped"` |
| 2 | 1.0 | −2 | `"linear"` |
| **3** | **1.5** | **0** | **`[0.330000013113022, 0, 0.670000016689301, 1]`** |

Key 3 is created by the enable, mirroring key 0 exactly. Read it back: the clip
now rotates 0 → 8 → −2 → 0 over 1.5 s and wraps with no pop, and the last
segment (1.0 → 1.5) eases with `"linear"` — key 2's easing, which is the
*incoming* easing of the boundary and is untouched by MAR-172.

A second example, animation `aim`, bone `arm_l`, channel `rotate`. This lane is
**runtime-only** — it exists in the `.mskl` and not in the `.marrow` — and `aim`
has an explicit duration of 0.5:

| i | `time` | `angle` |
| ---: | ---: | ---: |
| 0 | 0.0 | 30 |
| 1 | 0.5 | 30 |

Enabling materializes the lane into `timeline_edits` and **adopts** key 1, whose
value already equals key 0's. So `boundary_action` is `adopted`, the lane flag
changes, and the two key values do not — a case that separates
`changed_lane_count` from a value diff, and the exact case §18.3 asserts.

Third example, animation `idle`, slot `body`, `color`. Also runtime-only, keys
at 0.0 `(1,1,1,1)`, 0.5 `(0.6,0.8,1,0.5)` and 1.0 `(1,1,1,1)`. With `idle`'s
explicit duration at 1.5, enabling materializes the lane and **creates** a key
at 1.5 with colour `(1,1,1,1)` and curve `"linear"` — key 0's curve.

## 7. Lane Families

### 7.1 The three that can be opted in

| Family | Lane struct | Why it needs a boundary key |
| --- | --- | --- |
| Transform (Rotate / Translate / Scale / Shear) | `TransformTimelineEdit` | value is interpolated; holding it to the wrap is a visible pop |
| Slot Color | `SlotColorTimelineEdit` | same, on RGBA |
| Deform | `MeshDeformTimelineEdit` | same, on the whole vertex-offset vector |

All three carry a continuous, copyable value and a shared `interpolation`. The
copy is total and length-preserving in every case, including Deform, whose
`vertex_offsets` vector is copied whole.

**Note this is a *wider* family set than MAR-171's**, which excluded Deform.
The asymmetry is deliberate and has an honest reason: MAR-171 needed a
**canonical scalar driver** and a vertex-offset vector has none (its §7.3);
MAR-172 needs a **copyable value**, and a vector is trivially copyable. Nothing
about the boundary contract requires reducing the value to a scalar. The two
exclusions answer two different questions.

### 7.2 The three that cannot

| Family | Why not |
| --- | --- |
| Slot Attachment | piecewise-constant. `[0.5, D)` shows the last attachment and `[0, 0.5)` shows the first; the wrap is an intended discrete change with no interpolation artifact to fix |
| Draw Order | same |
| Event | actively harmful. An event key at `B` and one at `0` both fire in the same wrap, so a boundary key would double every loop's first event |

`SlotAttachmentTimelineEdit`, `DrawOrderTimelineEdit`, and `EventTimelineEdit`
gain **no member**, so the exclusion is compile-enforced rather than
branch-enforced, exactly as MAR-171 §7.3 relies on the missing field of
`DeformKeyframeEdit`. `set_timeline_loop_sync()` rejects such a selector with a
message naming the reason, and the `.marrow` tree shape (§8.1) has no slot to
express one.

## 8. Project-Local Storage

### 8.1 Where and what shape

One optional top-level `.marrow` member, `loop_sync`, whose `animations` tree
mirrors the shape of `timeline_edits.animations` exactly:

```json
"loop_sync": {
  "animations": {
    "idle": {
      "bones": { "spine": { "rotate": true } },
      "slots": { "body":  { "color":  true } },
      "deform": { "body": { "body_mesh": true } }
    },
    "aim": {
      "bones": { "arm_l": { "rotate": true } }
    }
  }
}
```

- Every leaf is a boolean. `true` means opted in.
- Only opted-in lanes are written. `false` is never serialized, and an animation,
  bone, slot, or category object with no opted-in lane under it is not written.
- `false` on load is accepted and means opted out, so a hand-edit can be
  explicit; it round-trips as absence.
- The whole `loop_sync` member is omitted when no lane is opted in, exactly as
  `snap`, `animation_edits`, and `timeline_edits` are omitted when empty
  (`project.cpp:4211-4240`).

In memory:

```cpp
struct TransformTimelineEdit {
    std::string animation_name;
    std::string bone_name;
    TransformTimelineChannel channel{TransformTimelineChannel::Rotate};
    std::vector<TransformKeyframeEdit> keyframes;
    bool loop_sync{false};
};
// SlotColorTimelineEdit and MeshDeformTimelineEdit gain the same member.
```

The on-disk block is a **pure projection** of those three booleans, rebuilt from
the lane vectors on every serialize and scattered back into them on every parse.
An orphan is therefore unrepresentable on the write side and rejected with a
JSON path on the read side (§8.4).

### 8.2 Why a top-level block and not a member of the lane's own value

The `.marrow` lane value is a bare **array**, not an object:
`build_timeline_edits_value()` writes
`bone_value->as_object()[channel] = build_transform_keyframes_value(edit)`
(`project.cpp:3794`) and the corresponding parser requires
`Value::Type::Array` (`project.cpp:1680`). There is no lane object to hold a
member.

Three shapes were considered.

**Promote the lane value to an object** — `"rotate": {"keyframes": [...],
"loop_sync": true}`, with the parser accepting both. Rejected: an older Marrow
build loading an opted-in project fails hard with `timeline edits must be an
array`. MAR-171 explicitly guarantees "the file remains readable by a build that
has never heard of `curve_mode`", and MAR-172 must not be the story that breaks
that guarantee for the projects that use its feature.

**A sibling key beside the lane** — `"rotate_loop_sync": true` under
`bones.<bone>`. Rejected: it collides with the channel namespace and reads as an
accident.

**A top-level tree** — chosen. An older build puts the whole unknown member into
`preserved_root`, round-trips it on save, and loads the timeline normally; the
managed boundary key is an ordinary keyframe it already understands, so **the
clip still loops correctly, it simply stops being maintained.** That is the
right failure mode. It also matches `.marrow.snap`'s discipline exactly (§8.7),
and the object-keyed tree makes duplicate lane entries structurally impossible
because `Value::Object` is keyed.

The hazard a top-level side table normally carries — going stale when the thing
it names moves — is the exact hazard MAR-171 §8.2 used to reject one. It does
not apply here, and the reason is precise: **MAR-171's side table would have
been keyed by `(animation, bone, channel, time)`, and a retime changes `time`.
MAR-172's is keyed by `(animation, bone, channel)`, which no retime, insertion,
deletion, or paste can change.** The only operations that change a lane's
identity are animation rename and delete, and both already move or erase the
whole lane struct — carrying the boolean with it for free, because it is a
struct member and the on-disk tree is only a projection of that member.

### 8.3 The default-off guarantee

| Condition | Result |
| --- | --- |
| No `loop_sync` member | every lane `loop_sync == false`, never read |
| A lane is opted out in memory | **it appears nowhere** in the serialized block |
| No lane is opted in | `root.erase("loop_sync")`; `serialize_project()` output is byte-identical to a pre-MAR-172 build |
| No lane is opted in | `synchronize_loop_boundaries()` returns immediately with `lane_count == 0` and calls no resolver, so runtime behaviour is byte-identical too |

That last row matters as much as the first: the default-off guarantee is
behavioural as well as textual, and §18.2 and §18.5 assert both.

### 8.4 Validation rules

At load, in a new `parse_loop_sync()` that runs **after**
`parse_transform_timeline_edits()`, the slot-colour parser, and the deform
parser, so the cross-reference can be resolved. Each rejection carries a precise
JSON path such as
`$.loop_sync.animations.idle.bones.spine.rotate`:

| Condition | Message |
| --- | --- |
| `loop_sync` present and not an object | `loop_sync must be an object` |
| `loop_sync.animations` missing or not an object | `loop_sync requires an animations object` |
| any animation / `bones` / `slots` / `deform` / bone / slot value not an object | `loop_sync entries must be objects` |
| a `bones.<bone>` key that is not `rotate`, `translate`, `scale`, or `shear` | `loop_sync transform channel must be rotate, translate, scale, or shear` |
| a `slots.<slot>` key that is not `color` | `loop_sync slot entries support only color` |
| a leaf that is not a boolean | `loop_sync entries must be booleans` |
| a leaf that is `true` for a lane with no `timeline_edits` entry | `loop_sync requires a timeline edit for that lane` |
| a leaf that is `true` for a lane whose first keyframe is not at time zero | `loop synchronized timelines require a key at time zero` |

Two rules deliberately **not** enforced at load:

- **the explicit-duration prerequisite.** The parser runs before any animation
  catalog exists, and `animation_edits` in the same document can author the very
  duration in question, so the check has a parse-order dependency with no
  correct answer. It is enforced at enable time and re-enforced by the sync pass
  (§11), whose rejection message names the lane and both remedies;
- **agreement between the boundary key and the first key.** A stale pair is
  legal data, exactly as MAR-171 §9.6 makes a stale `curve_mode` / `curve` pair
  legal. Load never rewrites (§9.5); the first transaction reconciles.

In `validate_project_for_save()` (`project.cpp:4757`, beside the existing `snap`
block):

| Condition | Message |
| --- | --- |
| An opted-in lane whose `keyframes` is empty | `loop synchronized timelines require at least one keyframe` |
| An opted-in lane whose first keyframe time is not within `kKeyTimeEpsilon` of zero | `loop synchronized timelines require a key at time zero` |

These are the two structural rules expressible without a skeleton, and they
mirror MAR-165's `snap`-step re-validation at the same site.

### 8.5 Unknown-field preservation

`loop_sync` is a top-level member, so it inherits `ProjectData::preserved_root`
for free at the root level: an older build round-trips it untouched. Inside the
block, MAR-172 follows `ProjectSnapSettings::preserved_source`
(`project.cpp:320,4194`) and stores the parsed source value on a small
`ProjectLoopSyncSettings`-style holder — **no.** MAR-172 deliberately does
**not** add a `preserved_source`, and the reason is that the block is a pure
projection: it has no fields of its own beyond the lane leaves, every one of
which is round-tripped through a lane struct, so there is nothing for a
`preserved_source` to preserve except future additions this version cannot
interpret.

The honest statement, recorded rather than glossed: **unknown members *inside*
the `loop_sync` tree are dropped on save.** That is a real, bounded limitation.
It is accepted because the alternative — a per-node preserved source down a
five-level tree — costs more complexity than the whole feature, and because the
same limitation already applies to `timeline_edits`, which the tree mirrors and
which has never preserved unknown members inside itself
(`build_timeline_edits_value()` rebuilds the animations object from scratch).
The `snap` precedent for `preserved_source` applies to a *flat settings object*
with room to grow; this is a keyed index, and a future MAR-172-adjacent story
that needs richer per-lane data should promote a leaf from `true` to an object
and add a preserved source at that point.

### 8.6 Runtime-export neutrality, and the one thing that is *not* neutral

`build_runtime_document()` (`project.cpp:4596`) builds runtime keyframe objects
through `build_transform_keyframes_value()` and its siblings, each of which
emits a fixed member list. MAR-172 needs **zero changes** to the export path,
and the `loop_sync` flag reaches nothing.

But the managed boundary key **is** exported, because it is an ordinary
keyframe in the project lane, and that is the entire point. So MAR-172's export
story has two halves and the tests must assert both:

1. **Negative:** the exported `.mskl` text contains no `loop_sync` anywhere.
2. **Positive:** the exported `.mskl` carries the boundary key — the `spine`
   rotate timeline of `idle` has **four** keyframes, the fourth at
   `time == 1.5` with `angle` bit-equal to keyframe 0's and the same cubic
   `curve` — and the exported file is **strictly larger** than the untouched
   baseline, because a whole keyframe object was added.

`.mskl` stays version 1 and `.mbin` stays version 2. Unlike MAR-171, the
**`.mbin` size also grows**, because a real extra keyframe is encoded; that
makes the binary size a valid signal here rather than a false negative (§18.6).

### 8.7 Mapping the MAR-165 `.marrow.snap` pattern

| `.marrow.snap` property | MAR-165 mechanism | MAR-172 equivalent |
| --- | --- | --- |
| Optional and absent by default | `std::optional<ProjectSnapSettings>`; `root.erase("snap")` when absent | `root.erase("loop_sync")` when no lane is opted in (§8.3) |
| Parsed with per-field fallback | `read_optional_boolean` | `find_optional_member` + boolean requirement + absent-means-off |
| Rejected with a JSON path on bad data | `validation_error(document, loc, "$.snap.x", ...)` | same helper, lane-scoped paths (§8.4) |
| Re-validated in the save validator | the step-positivity block at `project.cpp:4757` | the time-zero block (§8.4) |
| Never exported, never versioned | "`snap` never enters `.mskl` or `.mbin`" | §8.6, asserted by a text search on the export |
| Unknown members survive | `preserved_source` | at the root, yes, via `preserved_root`; inside the block, no — stated as a limitation (§8.5) |

## 9. Synchronization

### 9.1 The seam

```cpp
struct TimelineLoopSyncResult : AuthoringResult {
    std::size_t lane_count{0U};              // opted-in lanes in scope
    std::size_t synchronized_lane_count{0U}; // lanes whose boundary key changed
    std::size_t created_key_count{0U};
    std::size_t moved_key_count{0U};
    std::size_t rewritten_key_count{0U};
    std::size_t resolved_key_count{0U};      // from the MAR-171 resolver pass
};

TimelineLoopSyncResult synchronize_loop_boundaries(
    ProjectData* project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name = {});
```

It is called from exactly **two** places, both in `src/editor/session.cpp`,
immediately after the existing `auto_extend_explicit_animation_durations()`
call and before `build_project_runtime()`:

| Site | Existing shape | MAR-172 addition |
| --- | --- | --- |
| `EditorSession::Impl::refresh_runtime()` (`session.cpp:1317`) | auto-extend, error → `SessionResult{false, ...}` | the sync, same error shape |
| `EditorSession::Impl::commit()` (`session.cpp:1413`) | auto-extend, error → `restore_active_transaction()` + error; `changed` → `runtime_is_current = false` | the sync, same rollback, same `runtime_is_current` invalidation |

**Why this seam and not a list of call sites.** The story's criterion 3 names
"explicit duration" changes, and the session performs duration changes *on the
caller's behalf*: `auto_extend_explicit_animation_durations()` grows an explicit
duration to cover any key the transaction just authored. A sync wired at the
controller would run **before** that growth. Concretely: animation `idle` with
`D = 1.5` and an opted-in `spine/rotate` boundary at 1.5; the user adds a key at
2.0 s on the *not* opted-in `root/translate` lane; auto-extend raises `D` to
2.0; and every boundary key in the animation is now stale at 1.5. Only a sync
positioned after the auto-extend closes that hole, and only the session owns
that position.

The seam also makes criterion 3 structural rather than enumerated: value drags,
retimes, handle drags, presets, curve-mode applications, insertions, removals,
cuts, pastes, and every Agent operation are covered with **zero** per-site
wiring, and a future story cannot forget to add itself to a table.

**Not wired at `rebuild_runtime()`** (`session.cpp:2423`), which also calls
auto-extend. That path runs *outside* any transaction, so a mutation there would
be an unrecorded, non-undoable project change — precisely what MAR-171 decision
8 forbids at load. The bounded consequence: a duration grown by
`rebuild_runtime()`'s auto-extend leaves boundary keys stale until the next
transaction, which repairs them. Stated, tested, and accepted.

### 9.2 The two phases, and why they terminate

```text
synchronize_loop_boundaries(project, skeleton, animation):
  0. if no in-scope lane has loop_sync, return {} immediately          <- default-off
  1. candidate = *project
  2. PHASE 1 — structure and value, for each in-scope opted-in lane:
       a. resolve D (pending SetDuration edit, else explicit_duration); reject if absent
       b. B = float32(D); reject if B < kNonEventKeySpacing
       c. reject if no key within kKeyTimeEpsilon of 0
       d. reject if two or more keys are within kKeyTimeEpsilon of B
       e. reject if any key is > B + kKeyTimeEpsilon
       f. reject if a non-boundary key is in (B - kNonEventKeySpacing, B)
       g. create / move / adopt the boundary key at exactly B
       h. copy key 0's value components onto it
  3. resolve_automatic_curves(&candidate, {})                          <- MAR-171
  4. PHASE 2 — easing mirror, for each in-scope opted-in lane:
       copy key 0's interpolation (+ curve_mode, curve_driver) onto the boundary key,
       through write_key_interpolation(), NEVER set_keyframe_interpolation()
  5. on any error in 2..4: return, leaving *project untouched
  6. if nothing changed in 2 or 4 and resolved_key_count == 0: return a no-change result
  7. *project = std::move(candidate)
```

**Why two phases.** Phase 1 changes the *key set* that MAR-171's resolver reads:
creating a key at `B` gives the previously-last key a real outgoing segment, and
its automatic tangent must be computed with the boundary key present. So the
resolve must come after phase 1. Phase 2 copies the first key's easing, which
the resolve may have just rewritten, so it must come after the resolve. Hence
two passes over the same lanes.

**Why it terminates without iterating to a fixed point.** The resolver's inputs
are key **times** and driver **values**; a key's own stored `interpolation` is
never an input to any tangent computation. Phase 2 writes only
`interpolation`, `curve_mode`, and `curve_driver`, and writes them only on the
boundary key, which is the lane's last key and therefore has no outgoing segment
for the resolver to compute. Phase 2 can therefore not invalidate step 3, and
one pass of each is exact. This is the crux of the design and §18.1 pins it with
a test that runs the sync twice and asserts the second call reports no change.

**Why phase 2 must not call `set_keyframe_interpolation()`.** MAR-171 §4.2 makes
that function demote every key it writes to `TimelineCurveMode::Manual`. Using
it here would demote the boundary key on every single transaction, silently
destroying the mirrored auto intent. Phase 2 uses the same file-local
`write_key_interpolation()` helper the resolver uses (`authoring.cpp:1924`).

**On MAR-171's per-call-site resolver invocations.** MAR-171 wires
`resolve_automatic_curves()` at roughly a dozen controller and agent sites. With
MAR-172's step 3, those become a redundant *first* pass on an opted-in
animation: they run before the boundary key exists, step 3 re-runs after it
does, and the second run is the authoritative one. The first run is idempotent
and cheap (`resolved_key_count` is simply larger than it would otherwise be) and
relocating MAR-171's calls is an explicit **non-goal** (§3.3). On a project with
no opted-in lane, step 0 returns before the resolver is reached, so nothing runs
twice at all.

### 9.3 Duration: the inferred-duration floor

`set_animation_duration()` refuses a duration shorter than the animation's
inferred duration (`authoring.cpp:1606-1612`), computed as the maximum of
`animation->inferred_duration()` on the **effective** skeleton and the project
overlay's own maximum key time. On an opted-in animation the boundary key is
part of both terms, so an opted-in clip could never be shortened: `D` could
never fall below the key that only exists because of `D`.

MAR-172 replaces both uses with:

```cpp
/**
 * @brief The animation's inferred duration with managed loop boundaries excluded.
 *
 * A loop-synchronized lane's last key is placed at the explicit duration by the
 * boundary contract, so it must not act as a floor on the duration it follows.
 * Every opted-in lane is materialized in `project`, so the effective timeline
 * that corresponds to it is a copy of the project lane: this walks the
 * effective animation, skips every timeline an opted-in project lane owns,
 * folds those lanes back in at their second-to-last key time, and takes the
 * maximum with every project lane's own last key.
 *
 * Returns `animation.inferred_duration()` unchanged, bit for bit, when no lane
 * of `animation` is opted in, so every existing project keeps byte-identical
 * duration validation.
 */
double inferred_duration_excluding_loop_boundaries(
    const ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    const runtime::AnimationData& animation);
```

The early return on "no opted-in lane" is not an optimization; it is the
guarantee that MAR-172 cannot change duration validation for any project that
does not use it, and it is asserted directly rather than argued (§18.3).

`auto_extend_explicit_animation_durations()` gains the matching exclusion at the
project-overlay scan: a MAR-172 overload of `include_animation_timeline_maximum`
that skips `keyframes.back()` when `edit.loop_sync && keyframes.size() >= 2`,
applied to the three continuous families only. Without it, shrinking a duration
would be undone one line later: auto-extend would see the boundary key still at
the old `D` and grow the duration straight back.

**The shrink sequence, end to end.** `idle` with `D = 1.5`, opted-in
`spine/rotate` boundary at 1.5, every other key at or below 1.0. The user sets
the duration to 1.2:

1. `set_animation_duration()` computes the floor with boundaries excluded →
   1.0. `1.2 >= 1.0`, accepted; the `SetDuration` edit records 1.2.
2. `refresh_runtime()` → `auto_extend_...` sees `authored_boundary = 1.2` and a
   boundary-excluded project maximum of 1.0 → no growth.
3. `synchronize_loop_boundaries()` → `B = 1.2`; the previous non-boundary key is
   at 1.0 and `1.2 >= 1.0 + 0.001`, so the boundary key **moves** from 1.5 to
   1.2 and is re-mirrored from key 0.
4. `build_project_runtime()` → duration 1.2, lane keys at 0, 0.5, 1.0, 1.2.

A shrink to 1.0005 fails at step 3's spacing check, cancelling the transaction
and rolling step 1 back with it. A shrink to 0.9 fails at step 1 with the
existing "cannot be shorter than the last authored key (1.000000 seconds)"
message, unchanged.

### 9.4 One transaction, one history entry

Every trigger already owns exactly one `EditTransaction`, and the sync is an
additional mutation of that transaction's project before the same
`build_project_runtime()` and the same `commit()`. So:

- a value drag that moves the first key and re-mirrors three lanes' boundaries is
  **one** undo entry;
- `Ctrl+Z` restores both the drag and the boundary keys, because
  `EditTransaction::cancel()` and the history entry capture the whole project;
- a sync failure in `refresh_runtime()` returns an error and the caller cancels;
  a sync failure in `commit()` calls `restore_active_transaction()` — the exact
  rollback path the auto-extend failure already uses — so the primary mutation is
  rolled back too and a partially-synchronized project is unrepresentable.

`set_timeline_loop_sync()` opens its own transaction with
`EditKind::EditProperty`, label `"Enable loop synchronization"` /
`"Disable loop synchronization"` (plural variants for multiple lanes), group
`"timeline:loop-sync"`, `allow_merge = false`, impact
`Project | Runtime | Preview`. The group is distinct from MAR-169's
`"timeline:graph-easing"`, MAR-170's `"timeline:curve-preset"`, and MAR-171's
`"timeline:curve-mode"`, so nothing merges across the four.

MAR-172 adds **no gesture**: no drag, no dead zone, no entry in
`authoring_gesture_active()` (`shell_state.hpp:781-796`), and no entry in
`cancel_authoring_gestures()` (`shell_core.cpp:431-491`). The two lists that
must stay in step stay exactly as MAR-171 leaves them.

### 9.5 What deliberately never synchronizes

| Moment | Behaviour | Why |
| --- | --- | --- |
| `load_project()` | no sync | Load must not dirty the project, must not create history, and must leave load→save byte-stable |
| `serialize_project()` / `save_project()` | no sync | Serialization is a pure projection of `ProjectData` |
| `export_runtime_assets()` | no sync | Export must be a pure function of the project; a sync here would make the exported file disagree with the saved one |
| `EditorSession::rebuild_runtime()` | no sync | Outside any transaction; a mutation there would be unrecorded and non-undoable (§9.1) |
| a background timer or frame tick | no sync | Synchronization is a transactional edit and belongs to a user action |

**The consequence, stated plainly:** a hand-edited `.marrow` can hold
`loop_sync: true` beside a lane whose last key is not at the duration, or whose
value does not match key 0. That document loads, validates structurally,
evaluates, and exports using the **stored** keys. The first transaction of any
kind repairs it — or rejects, if the lane cannot satisfy its contract, with a
message naming the lane and both remedies (author a duration, or disable loop
sync on that lane).

### 9.6 Selection stability

Phase 2 writes only easing fields, so it moves no key. Phase 1 can **create**,
**move**, or **rewrite** the boundary key:

- **rewrite** — no key times change; every `TimelineKeyRef` is bit-identical and
  `reconcile_timeline_key_selection()` has nothing to prune;
- **create** — a key is appended at the end of the lane. Existing keys keep their
  indices and their `time_microseconds`, so every existing `TimelineKeyRef` still
  resolves. The new key is not selected;
- **move** — the boundary key's `time_microseconds` changes, so a `KeyRef`
  naming it goes stale. That is reachable only when the duration changed in the
  same transaction, and the existing `reconcile_selection()` already prunes a ref
  whose time no longer exists on the track (`timeline_model.cpp`). The boundary
  key cannot be part of a live retime selection, because §10.2 pins it.

## 10. Managed Identity Rules

Criterion 4: "Managed identity prevents selection, clipboard, retime, or
duration edits from producing duplicate or stale boundary keys." Four rules, one
per named hazard, plus the sync pass as the authority behind all of them.

### 10.1 Selection-driven value and easing edits

A managed boundary key's value and easing are derived. An edit that writes them
would be undone by the sync pass in the same transaction, which is the worst of
both worlds — the user's drag appears to do nothing and no message explains why.

- **GUI: skip and report.** `collect_curve_preset_selectors()`
  (`timeline_controller.cpp:1194`) and the equivalent collectors for MAR-168's
  value drag, MAR-169's handle drag, and MAR-171's curve-mode row exclude a
  managed boundary key and count it, exactly as they already exclude an
  easing-free lane. The status message says
  `"… ; 1 is a managed loop boundary"`.
- **Agent: reject atomically**, naming the reason and the remedy:
  `"That key is a managed loop boundary; edit the key at time 0 of the same
  timeline instead."` This is the same GUI-skips / Agent-rejects asymmetry
  MAR-169, MAR-170, and MAR-171 all use, and for the same reason: a dopesheet box
  selection routinely spans a boundary key, while a scripted selector list does
  not.
- **The sync pass re-asserts the contract regardless**, so a path that neither
  skips nor rejects still cannot leave a stale boundary. Defence in depth, not
  the primary mechanism.

### 10.2 Retime

> **On an opted-in lane, the first key and the last key are immovable.**

Implemented inside `include_resolved_retime_bounds()` (`authoring.cpp:443`), per
family, using nothing but `ProjectData`:

```cpp
// A loop-synchronized lane pins both ends: the first key defines the boundary
// value at t = 0 and the last key IS the managed boundary at the clip duration.
// Moving either would leave the lane without the prerequisites its opt-in
// asserts, so both behave as immovable neighbours.
if (timeline.loop_sync &&
    (resolved.key_index == 0U ||
     resolved.key_index + 1U == timeline.keyframes.size())) {
    *minimum_delta = std::max(*minimum_delta, 0.0);
    *maximum_delta = std::min(*maximum_delta, 0.0);
}
```

This composes with the existing shared-bounds model rather than fighting it:
`include_retime_bounds()` already pins `minimum_delta` to `-original_time` for
every key, so a selection containing a `t = 0` key already cannot move left, and
one selected immovable key already freezes the whole selection. A drag that
includes a pinned key collapses to `applied_delta == 0` and
`retime_keyframes()` returns its existing `changed == false` result — no error,
no history entry, no special case. §18.3 asserts the exact numbers.

`retime_keyframes()`'s signature is unchanged, so MAR-173 inherits it as it
stands.

### 10.3 Removal

- Removing the **boundary key** directly: skipped in the GUI (it is filtered out
  of `removals` with a status message), rejected by the Agent. The lane's
  contract requires it to exist.
- Removing the **time-zero key**: the sync pass's phase 1 step (c) then rejects,
  cancelling the transaction, with
  `"Animation 'idle' timeline 'spine/rotate' is loop synchronized and requires a
  key at time zero; disable loop synchronization before removing it."` Fail-closed,
  with the remedy in the message.
- Removing a **middle key** that would leave the boundary within
  `kNonEventKeySpacing` of the new previous key: impossible — removing a key can
  only *increase* the gap before the boundary.

### 10.4 Clipboard

- **Copy** builds a `ProjectData project_fragment` through
  `append_selected_timeline_fragment()` (`timeline_model.hpp:198`), which does
  `Timeline copied = source;` and would therefore carry `loop_sync` into the
  fragment. `copy_selected_timeline_keys()` clears it on the fragment's three
  continuous-family vectors immediately after building it. The flag is a property
  of a lane in a project, never of a clipboard fragment, and §18.3 asserts the
  fragment's flags are all false.
- **Paste** writes only `source.keyframes` into a destination lane found through
  `ensure_*_timeline_edit_index()` (`timeline_controller.cpp:1660-1770`); the
  destination lane's own `loop_sync` is untouched by construction, so no code
  change is needed there and none is made.
- A paste landing exactly on the boundary time replaces the boundary key
  (`paste_keys_replace_collisions()` erases collisions within
  `kKeyTimeEpsilon` first) and the sync pass rewrites its value back in the same
  transaction — so the paste is value-neutral at that one time. Documented, not
  prevented: preventing it would need a second copy of the boundary rule inside
  a template that knows nothing about lanes.
- A paste landing **past** the boundary is rejected by the sync pass's phase 1
  step (e), cancelling the transaction with a message naming the lane.

### 10.5 Duration

Covered in full by §9.3. The three outcomes are: grow (the boundary moves
forward, always possible), shrink above the spacing floor (the boundary moves
back), and shrink below it (rejected atomically, by `set_animation_duration()`'s
own floor or by the sync pass's spacing check, whichever binds first).

## 11. Preflight-then-Mutate and Atomic Rollback

Both new primitives follow the shape of `set_keyframe_interpolation()`
(`authoring.cpp:1984`), which is what makes rollback exact.

`set_timeline_loop_sync()`:

```text
 1. validate arguments (null project, empty selectors)
 2. ProjectData candidate = *project                    // full copy, nothing shared
 3. resolve every lane selector against `candidate`; reject duplicates by
    (kind, timeline_index)
 4. reject every selector whose family carries no loop sync (Draw Order, Event,
    Slot Attachment)
 5. when enabling: check every prerequisite of section 6.5 per lane
 6. write the flag into `candidate`, counting lanes whose flag actually changed
 7. synchronize_loop_boundaries(&candidate, skeleton, <every animation named>)
 8. on any error in 3..7: return, leaving *project untouched
 9. if nothing in 6 or 7 changed: return a no-change result
10. *project = std::move(candidate)                     // one move
```

Step 7 runs inside the primitive for the same reason MAR-171's
`set_keyframe_curve_mode()` calls the resolver internally: the operation's own
dry run must report the resulting boundary key, not a promise that the session
seam will produce one later.

`synchronize_loop_boundaries()` is the ten-step shape in §9.2, whose steps 1 and
7 are the same copy-then-single-move.

No step between the copy and the final move can leave a partial write in
`*project`. When the sync runs from inside `refresh_runtime()` or `commit()`,
there are two independent layers of atomicity: the primitive's own candidate
copy, and the enclosing `EditTransaction::cancel()` /
`restore_active_transaction()`.

`EditTransaction::cancel()` restores the project, the runtime `SkeletonData`,
the preview controller state, and the playback state captured at `begin_edit()`,
then bumps the runtime/preview revision;
`sync_shell_from_editor_session(state)` refreshes the shell mirrors. The graph
and the dopesheet are byte-identical to the transaction start because both read
through revision-keyed caches and neither holds a runtime pointer or key index
across frames.

## 12. UI-Free API

### 12.1 `include/marrow/editor/project.hpp`

```cpp
struct TransformTimelineEdit {
    std::string animation_name;
    std::string bone_name;
    TransformTimelineChannel channel{TransformTimelineChannel::Rotate};
    std::vector<TransformKeyframeEdit> keyframes;
    /**
     * @brief Loop-boundary synchronization intent for this timeline.
     *
     * When true, the editor maintains one managed key at the animation's
     * explicit duration whose value and easing mirror this timeline's key at
     * time zero, so a looping clip wraps without a pop. Absent from every
     * project that has not opted in, never exported, and default-off for every
     * newly created timeline.
     */
    bool loop_sync{false};
};
```

The same member, with the same comment, on `SlotColorTimelineEdit` and
`MeshDeformTimelineEdit`. `DrawOrderTimelineEdit`, `EventTimelineEdit`, and
`SlotAttachmentTimelineEdit` gain nothing (§7.2).

### 12.2 `include/marrow/editor/authoring.hpp` (additive)

Appended after MAR-171's accessors.

```cpp
/** @brief Which timeline family a lane selector names. */
enum class TimelineLaneKind : std::uint8_t { Transform, SlotColor, Deform };

/**
 * @brief Stable project-domain selector for one persisted timeline lane.
 *
 * Unlike `TimelineKeySelector`, this names a whole timeline and carries no
 * time, because loop synchronization is a lane-level property whose identity
 * no retime, insertion, deletion, or paste can change. Fields not used by the
 * selected kind remain empty.
 */
struct TimelineLaneSelector {
    TimelineLaneKind kind{TimelineLaneKind::Transform};
    std::string animation_name;
    std::string bone_name;
    TransformTimelineChannel transform_channel{TransformTimelineChannel::Rotate};
    std::string slot_name;
    std::string attachment_name;
};

/** @brief What the contract did to one lane's boundary key. */
enum class TimelineLoopBoundaryAction : std::uint8_t {
    Unchanged, Created, Adopted, Moved, Rewritten, Released,
};

struct TimelineLoopSyncResult : AuthoringResult {
    std::size_t lane_count{0U};
    std::size_t changed_lane_count{0U};       // the flag differed
    std::size_t synchronized_lane_count{0U};  // the boundary key differed
    std::size_t created_key_count{0U};
    std::size_t moved_key_count{0U};
    std::size_t rewritten_key_count{0U};
    std::size_t resolved_key_count{0U};       // from the MAR-171 resolver pass
    std::vector<TimelineLoopBoundaryAction> lane_actions;  // parallel to selectors
};

/**
 * @brief Atomically records loop-boundary synchronization intent on lanes.
 *
 * Only Transform, Slot Color, and Deform timelines can be synchronized: the
 * discrete families are piecewise constant, already wrap without a pop, and an
 * event key at the boundary would fire twice per loop. Enabling requires the
 * lane's animation to carry an explicit duration of at least one millisecond
 * and the lane to hold a key exactly at time zero, and immediately creates or
 * adopts one managed key at that duration mirroring the time-zero key.
 * Disabling evaluates no prerequisite and leaves the managed key in place as an
 * ordinary key, so a project that has reached an unsatisfiable state always has
 * an escape. A rejected edit leaves the project unchanged.
 */
TimelineLoopSyncResult set_timeline_loop_sync(
    ProjectData* project,
    const runtime::SkeletonData& effective_skeleton,
    const std::vector<TimelineLaneSelector>& lanes,
    bool enabled);

/**
 * @brief Re-establishes the loop-boundary contract on every opted-in lane.
 *
 * Callers run this inside the transaction that changed a key value, a key time,
 * a key's existence, an automatic curve, or an explicit duration, so one edit
 * stays one history entry. Projects with no opted-in lane return immediately
 * having done nothing at all, including no automatic-curve resolution. A lane
 * that cannot satisfy its contract rejects the whole call atomically rather
 * than synchronizing part of it, naming the animation, the timeline, and the
 * remedy.
 */
TimelineLoopSyncResult synchronize_loop_boundaries(
    ProjectData* project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name = {});

/** @brief The inferred duration with managed loop-boundary keys excluded. */
double inferred_duration_excluding_loop_boundaries(
    const ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    const runtime::AnimationData& animation);

/** @brief The `.marrow` token for one lane kind, and its inverse. */
std::string_view timeline_lane_kind_token(TimelineLaneKind kind);
std::optional<TimelineLaneKind> timeline_lane_kind_from_token(std::string_view token);
```

`set_animation_duration()`'s declaration is unchanged; only its documented floor
changes (§9.3), and the doc comment gains one sentence recording that a managed
loop boundary does not constrain the duration it follows.

### 12.3 `src/editor/timeline_controller.hpp` (shell, ImGui-free)

One change only:

```cpp
/**
 * @brief Reports whether one key of one lane is a managed loop boundary.
 *
 * True only when the lane is loop synchronized and `key_index` is its last key,
 * which is exactly the contract's definition of the managed boundary. Used by
 * every selection collector so a derived key is never offered for direct value
 * or easing authoring.
 */
bool timeline_key_is_managed_loop_boundary(
    const ShellState& state,
    const TimelineTrackRow& track,
    std::size_t key_index);
```

`copy_selected_timeline_keys()` gains three flag-clearing lines (§10.4). No
other controller function changes, no new gesture, no new render statistic, and
no ImGui file is touched.

## 13. Agent and MCP Surface

### 13.1 The operation

| Field | Value |
| --- | --- |
| `name` | `timeline.set_loop_sync` |
| `category` | `edit` |
| `mutating` | `true` |
| `requires_review` | `false` |
| `dry_run_supported` | `true` |
| `requires_project` | `true` |
| `handler` | `&handle_editing_operation` (tail-calls `handle_timeline_editing_operation`) |
| registry position | immediately after `timeline.set_curve_mode` |

`kOperationSpecs[]` row order is part of the contract —
`expect_registry_contract()` in `agent_dispatch_smoke.cpp` compares index by
index — so the row goes exactly between MAR-171's `timeline.set_curve_mode` and
`set_transform`, keeping the `timeline.*` editing operations contiguous.

**New registry total: 59 operations** (inspection 12, validation 3, management
10, edit 34), assuming MAR-171 landed at 58. §14 requires the count to be
measured, not assumed.

### 13.2 Arguments

```json
{
  "lanes": [
    {"kind": "transform",  "animation": "idle", "bone": "spine", "channel": "rotate"},
    {"kind": "slot_color", "animation": "idle", "slot": "body"},
    {"kind": "deform",     "animation": "idle", "slot": "body", "attachment": "body_mesh"}
  ],
  "enabled": true,
  "dry_run": false
}
```

- `lanes`: required array, 1 to 4096 entries, the same cap
  `timeline.retime_keyframes`, `timeline.set_interpolation`, and
  `timeline.set_curve_mode` use. Only `transform`, `slot_color`, and `deform`
  are accepted; `draw_order`, `event`, and `slot_attachment` are rejected with a
  message naming the kind and the reason (§7.2). **Note the entries carry no
  `time`** — this is the surface's one structural difference from every other
  `timeline.*` operation, and it is the point: loop sync is a lane property.
- `enabled`: **required** boolean. Missing is an error rather than a default,
  for the same reason MAR-169 made `interpolation` required and MAR-171 made
  `mode` required: guessing for a caller's whole selection is destructive.
- `dry_run`: optional boolean.

Parsing reuses the `timeline.set_interpolation` `keys` loop's shape minus the
`time` requirement, and adds one helper declared in
`agent_dispatch_internal.hpp`:

```cpp
/**
 * @brief Parses one lane selector array into project-domain lane selectors.
 *
 * Lane selectors carry no time, because loop synchronization is a property of
 * a whole timeline. Draw-order, event, and slot-attachment kinds are rejected
 * rather than ignored: those families are piecewise constant and need no
 * boundary key at all.
 */
bool timeline_lane_selectors_arg(
    const json::Value& args,
    std::vector<marrow::editor::TimelineLaneSelector>* lanes_out,
    std::string* error_out);
```

`interpolation_request_arg()`, `interpolation_arg()`, and MAR-171's
`curve_mode_request_arg()` are left byte-identical, so every existing operation
keeps its exact argument contract.

### 13.3 Validation rules

In order:

1. missing `args` object → `invalid_request`.
2. `lanes` missing, not an array, or empty → `invalid_request`.
3. more than 4096 lanes → `invalid_request`.
4. a lane entry that is not an object, or missing `kind`/`animation`, or missing
   `bone`+`channel` (transform) / `slot` (slot_color) / `slot`+`attachment`
   (deform) → `invalid_request`.
5. a `draw_order`, `event`, or `slot_attachment` kind, or an unknown kind →
   `invalid_request`, naming the kind.
6. an unknown transform `channel` → `invalid_request`.
7. `enabled` missing or not a boolean → `invalid_request`.
8. the same lane selected twice → `invalid_request`.
9. a lane that does not resolve after materialization → `not_found`, via
   `classify_timeline_key_error()`.
10. enabling with no explicit duration, no time-zero key, a duration below
    `kNonEventKeySpacing`, a key past the boundary, a key crowding the boundary,
    or duplicate keys at the boundary → `invalid_request`, naming the animation,
    the timeline, and the failing prerequisite.
11. a sync rejection on any *other* opted-in lane of the same animation →
    `invalid_request`, naming that lane.
12. live path only: nothing changed → `no_change`.

Note rule 10 does **not** apply to `"enabled": false` (§6.6).

### 13.4 Response payload

```json
{
  "dry_run": true,
  "enabled": true,
  "lane_count": 2,
  "changed_lane_count": 2,
  "synchronized_lane_count": 2,
  "created_key_count": 1,
  "moved_key_count": 0,
  "rewritten_key_count": 0,
  "resolved_key_count": 0,
  "lanes_truncated": false,
  "lanes": [
    {"kind": "transform", "animation": "idle", "bone": "spine", "channel": "rotate",
     "previous_enabled": false, "enabled": true,
     "duration": 1.5, "boundary_time": 1.5,
     "boundary_action": "created",
     "previous_boundary": null,
     "boundary": {"time": 1.5, "angle": 0.0,
                  "curve": [0.330000013113022, 0.0, 0.670000016689301, 1.0]},
     "changed": true},
    {"kind": "transform", "animation": "aim", "bone": "arm_l", "channel": "rotate",
     "previous_enabled": false, "enabled": true,
     "duration": 0.5, "boundary_time": 0.5,
     "boundary_action": "adopted",
     "previous_boundary": {"time": 0.5, "angle": 30.0, "curve": "linear"},
     "boundary": {"time": 0.5, "angle": 30.0, "curve": "linear"},
     "changed": true}
  ]
}
```

- `boundary_action` is one of `created`, `adopted`, `moved`, `rewritten`,
  `released`, `unchanged`. This is the **affected-lane reporting** criterion 5
  requires.
- `previous_boundary` is `null` when no key existed at the boundary time before
  the call.
- `boundary` reports the resulting key using exactly the `.marrow` keyframe
  encoding — the family's value members plus `curve` through the existing
  `build_interpolation_value()`/`interpolation_curve_value()` helpers — so a
  caller can compare against the stored bytes. Deform boundaries report
  `vertex_count` instead of the full offset array, because a mesh's offsets are
  hundreds of numbers and the response is a report, not a copy.
- `changed` is true when the lane's flag or its boundary key changed.
- The `lanes` array is capped at 256 entries with `lanes_truncated` set when the
  selection is larger; the counts always describe the whole call.

The per-lane echo doubles as the surface's **read-back channel**: a `dry_run`
call reports the current flag, the current boundary key, and the resulting one
for every named lane without mutating anything. `timeline.describe`'s payload
stays byte-identical.

### 13.5 Dry-run and live paths

Both paths first materialize each selector's lane through
`ensure_transform_timeline_edit()` / `ensure_slot_color_timeline_edit()` /
`ensure_mesh_deform_timeline_edit()`, so a runtime-only lane is copied into the
project rather than reported as missing — the same shape
`timeline.set_interpolation` uses at `agent_handlers_editing.cpp:987-1019`. The
`aim` / `arm_l` / `rotate` case in §6.7 is exactly this path.

- **Dry run**: `ProjectData candidate = *session.project();` materialize;
  snapshot; apply; snapshot; report; discard. The session is never touched, so
  `project_revision()`, `undo_count()`, and `dirty()` are unchanged.
- **Live**: `session.begin_edit({EditKind::EditProperty, "Enable loop
  synchronization via Agent" / "Disable …", "timeline:loop-sync", false,
  Project | Runtime | Preview})`, materialize, apply, `cancel()` on error or on
  `!result.changed`, then `commit_or_error()` with
  `CommitPolicy{"Failed to set loop synchronization: "}`.

`set_timeline_loop_sync()` synchronizes internally (§11 step 7), and the session
seam synchronizes again at `commit()`; the second run is a no-op and reports
`synchronized_lane_count == 0`, which §18.1 asserts.

**Every other operation's dry run is unaffected.** A dry run does not go through
the session, so it does not see the session-seam sync. That is correct rather
than a gap: each operation's dry-run payload describes its own effect, and none
of them claims to report boundary keys.

Undo/redo is inherited from `EditorSession`: one operation, one history entry,
reversible by the existing `undo` operation, which is exactly what the same edit
produces from any other surface.

### 13.6 MCP tool

`tools/mcp/tools/editing.py` gains one `types.Tool` named
`timeline.set_loop_sync`, placed immediately after MAR-171's
`timeline.set_curve_mode`, plus one helper schema:

- `_timeline_loop_sync_lane_schema()` — a `oneOf` over `transform`,
  `slot_color`, and `deform`, with **no `time` property**. Deliberately not a
  reuse of `_timeline_interpolation_key_schema()`, whose entries require a
  `time`.

```python
"enabled": {"type": "boolean"},
"dry_run": {"type": "boolean"},
```

with `"required": ["lanes", "enabled"]`. The description states that enabling
requires an explicit clip duration and a key at time zero, that the managed key
mirrors the time-zero key on every edit, that disabling leaves the key in place,
and that draw-order, event, and slot-attachment lanes are not supported. The
schema stays advisory — the MCP server forwards every call verbatim
(`server.py:75-79`), so the C++ primitive remains the sole authority.

`get_tools()` grows by exactly one entry; no existing tool changes.

## 14. The Operation Count: Every Place the Registry Total Appears

The registry total is derived from `std::size(kOperationSpecs)` and self-updates
when the row is added. Every other site is hand-edited. The rule from MAR-169
§14, MAR-170 §14.5, and MAR-171 §14 applies unchanged: **a sentence stating what
the surface *is* becomes the new number; a sentence carrying a date, a milestone
ID, or the words "historical", "baseline", or "checkpoint … passed" stays at
whatever it recorded.**

**The count is 57 today** (`std::size(kOperationSpecs)` measured on
`0a917db`). MAR-171 takes it to 58; MAR-172 takes it to 59. The plan's Task 0
**measures** the as-built count and rejects this table's numbers if they
disagree.

Discover the full list with:

```bash
rg -n '\b58\b' --glob '!build*' src tools AGENTS.md docs .agents
rg -n '\b59\b' --glob '!build*' src tools AGENTS.md docs .agents   # before: no registry claim
```

| Location | Kind | After MAR-172 |
| --- | --- | --- |
| `src/editor/agent_dispatch.cpp` `kOperationSpecs[]` | code | one more row; the count self-updates |
| `src/samples/agent_dispatch_smoke.cpp:39` `std::array<OperationExpectation, N>` | test | **59** + one new row after `timeline.set_curve_mode` |
| `src/samples/agent_dispatch_smoke.cpp` behaviour cases | test | new `timeline.set_loop_sync` cases (required: `expect_complete_coverage()` fails on an uninvoked row) |
| `src/editor/shell_smoke_graph.cpp:147,635,1557,2127` (four sites as of `0a917db`; MAR-171 adds more) | test | **59** |
| `tools/mcp/test_client.py:46,48` | test | **59** |
| `tools/mcp/test_client.py:32-41` `new_edit_operations` | test | add `timeline.set_loop_sync` |
| `AGENTS.md:161` `Current Validation` registry line | current doc | **59**, and extend the parenthetical |
| `AGENTS.md:6` `Project State` | current doc | MAR-172 done, MAR-173 next |
| `docs/root1/editing-gap-analysis.md:23,85,86,192,455` | current doc | **59** (and edit `32` → `34`) |
| `docs/root1/refector.md:20,112` | current doc | **59** |
| `AGENTS.md` MAR-169 / MAR-170 / MAR-171 `Validation Results` blocks | historical | **unchanged** |
| `docs/root1/discription.md:49,50` MAR-169 / MAR-170 paragraphs | historical | **unchanged** |
| `docs/root1/editing-gap-analysis.md:391,392,393` milestone rows | historical | **unchanged** |
| every other dated `Validation Results`, checkpoint, and past PRD criterion | historical | **unchanged** |

**Known false positives** the sweep surfaces that must not be touched:

- `IM_COL32(56, 61, 69, 255)` and `(51, 56, 64)` in the shell theme and graph;
- `IM_COL32(208, 134, 57, 230)` at `shell_viewport_ui.cpp:683` and
  `shell_viewport.cpp:1816`;
- `rgb(54, 57, 64)` at `shell_theme.hpp:39` and
  `docs/design/maroow-editor-visual-renewal-spec.md:100`;
- `"x": 56.0`, `56,995,840` bytes, and `PhysicsBoneState … 56 bytes/bone`;
- every `.agents/tasks/prd-marrow-runtime.json` timestamp containing `:57`,
  `:58`, or `:59`.

## 15. Error Handling and Fail-Closed Rules

| Condition | Result |
| --- | --- |
| No project, animation, or resolvable lanes | Agent `not_found`; no transaction |
| A lane's family carries no loop sync | skipped in the GUI collectors, **rejected** by the Agent |
| Two selectors resolve to the same lane | rejected atomically |
| Enabling without an explicit duration | rejected atomically, naming the animation |
| Enabling without a key at time zero | rejected atomically, naming the timeline |
| Enabling with `float32(D) < kNonEventKeySpacing` | rejected atomically |
| Enabling with a key past the boundary | rejected atomically, naming the key time |
| Enabling with a non-boundary key inside the 1 ms spacing before the boundary | rejected atomically, naming both times |
| Enabling with duplicate keys at the boundary time | rejected atomically |
| **Disabling**, in every one of the above states | **succeeds**; flag cleared, boundary key left in place (§6.6) |
| A managed boundary key in a value, easing, preset, or curve-mode selection | skipped in the GUI, **rejected** by the Agent |
| A managed boundary key or a time-zero key in a retime selection | pinned; the whole retime collapses to `changed == false` |
| Removing a managed boundary key | skipped in the GUI, rejected by the Agent |
| Removing an opted-in lane's time-zero key | the sync rejects; the transaction cancels |
| Pasting past an opted-in lane's boundary | the sync rejects; the transaction cancels |
| Shrinking a duration below the boundary spacing floor | rejected atomically by the sync; `set_animation_duration()` rolls back with it |
| Shrinking a duration below a real authored key | rejected by `set_animation_duration()`'s existing message, unchanged |
| `synchronize_loop_boundaries()` error inside `refresh_runtime()` | error returned; the caller cancels |
| `synchronize_loop_boundaries()` error inside `commit()` | `restore_active_transaction()`; the primary mutation rolls back too |
| Materialization failure | `cancel()` with rollback; error message |
| Loading a project whose boundary key disagrees with its time-zero key | accepted; stored keys win; no rewrite (§9.5) |
| Loading a project whose `loop_sync` names a missing lane | **rejected at load** with a JSON path (§8.4) |

Nothing here can leave a partial write: every project-side failure is either
before a primitive's single move-assign or is a cancel/restore of the enclosing
transaction, and the two layers are independently sufficient.

## 16. Non-Goals

- Everything in §3.3, restated for emphasis: **no ImGui widget, no render
  statistic, no actual-frame smoke.** MAR-172 is UI-free by the story's own
  words. A visual treatment for managed boundary keys — a distinct glyph in the
  dopesheet row and a hollow point in the graph plot — is a real ergonomic gap
  and is deferred; it would live in `shell_timeline.cpp` and
  `shell_timeline_graph.cpp` beside MAR-169's handle drawing, and would need
  `TimelineGraphRenderStats` fields and an actual-frame smoke, none of which any
  MAR-172 criterion asks for.
- No time scaling (MAR-173) or preview playback speed (MAR-174). In particular,
  MAR-173's selection-edge scaling must inherit §10.2's pinning rather than
  reimplement it, and MAR-172 leaves `retime_keyframes()`'s signature untouched
  so that it can.
- No loop sync for Draw Order, Event, or Slot Attachment lanes.
- No animation-level or project-level loop flag, and no change to how a playing
  application chooses to loop a `TrackEntry`.
- No relocation of MAR-171's per-call-site `resolve_automatic_curves()` calls.
- No change to `retime_keyframes()`, `insertable_key_time()`, or
  `clamp_existing_key_time()` signatures.
- No `preserved_source` inside the `loop_sync` block (§8.5).
- No `validate_project_for_save()` check that a boundary key agrees with its
  time-zero key; a stale pair is legal data.
- No synchronization at load, save, export, `rebuild_runtime()`, or on a timer.
- No deletion of the boundary key on disable.
- No change to `timeline.describe`, `timeline.retime_keyframes`,
  `timeline.set_interpolation`, MAR-170's preset table or preference flow, or
  MAR-171's resolver, demotion rule, or metadata.
- No `.mskl`, `.mbin`, C ABI, or `editor-settings.json` version change.
- No manual-visible-UI, Windows 11, or physical-input qualification credit.

## 17. Decisions Taken Under Ambiguity

1. **"Loop boundary keys" means one key per opted-in lane at the clip's explicit
   duration, mirroring that lane's key at time zero.** The story names "duration-
   boundary keys", "explicit duration", and "a key exactly at time zero" in one
   sentence, which fixes both times. `t = 0` is the mirror's *source*, not a
   second managed key: it is authored, and MAR-172 only pins it against retime.
2. **Opt-in is per lane, stored as one boolean on the lane struct, projected to
   an optional top-level `.marrow` tree.** The story says "lane metadata". The
   lane's own JSON value is a bare array with no room for a member (§8.2), and
   promoting it to an object would make an opted-in project unloadable by an
   older build — a regression on MAR-171's explicit forward-compatibility
   guarantee. The top-level tree degrades gracefully instead: an older build
   preserves the unknown member, loads the timeline, and plays the loop
   correctly; it simply stops maintaining it.
3. **MAR-171's §8.2 argument against a top-level side table does not apply.**
   That argument was about a table keyed by `(animation, bone, channel, time)`,
   which a retime invalidates. A lane table is keyed by
   `(animation, bone, channel)`, which no retime, insertion, deletion, or paste
   can change, and the only operations that do change it — animation rename and
   delete — already move the whole lane struct that owns the boolean.
4. **Managed identity is derived from `(flag, duration, key index)`, never
   stored.** A stored marker would ride a copy/paste into a lane where it is a
   lie, would need pruning on every clipboard and retime path, and would be a
   second source of truth to keep in step with the duration. Deriving it makes
   adoption (criterion 2's word) the absence of code and makes undo, redo, and
   load need no repair (§6.4).
5. **Synchronization is one-directional, first key → boundary key, and the first
   key always wins.** The story says the managed key's "value and shared curve
   follow the first-key boundary contract" — *follow* is directional. A symmetric
   rule would let a stray edit at the clip's end silently rewrite its opening
   pose and would need a conflict winner with no principled choice.
6. **Both the value and the easing are mirrored, even though the runtime never
   evaluates the boundary key's easing.** It makes the contract one sentence, it
   is the semantically honest value at the wrap, and it is what lets a later
   extension of the clip resolve correctly instead of freezing a stale curve
   (§6.3).
7. **Deform is included even though MAR-171 excluded it.** MAR-171 needed a
   canonical scalar *driver* and a vertex-offset vector has none; MAR-172 needs a
   copyable *value* and a vector is trivially copyable. Excluding Deform would
   make a breathing loop pop while a colour loop does not, for no reason the code
   could state (§7.1).
8. **The discrete families are excluded, compile-enforced.** Draw Order and Slot
   Attachment are piecewise constant and already wrap without an artifact; an
   Event key at the boundary would fire twice per loop, which is a bug, not a
   feature (§7.2).
9. **Synchronization lives at the session seam, not at a list of call sites.**
   `auto_extend_explicit_animation_durations()` changes explicit durations
   *inside* `refresh_runtime()` and `commit()`, after any controller-level sync
   would have run, so a controller-wired sync has a real staleness hole (§9.1).
   The seam also makes criterion 3's "in the same transaction" structural.
10. **The sync runs the MAR-171 resolver between its two phases, and phase 2
    writes easing directly.** Creating the boundary key changes the key set the
    resolver reads, and the resolver may rewrite the first key's curve that phase
    2 mirrors — so the order is forced. Phase 2 must not use
    `set_keyframe_interpolation()`, which would demote the boundary key to manual
    on every transaction (§9.2).
11. **`set_animation_duration()`'s inferred floor excludes managed boundary
    keys.** Without it an opted-in clip could never be shortened, because the
    duration could not fall below the key that only exists because of the
    duration. The exclusion is a bit-exact no-op for every animation with no
    opted-in lane, and that is asserted rather than argued (§9.3).
12. **A violated invariant cancels the whole transaction.** Partial
    synchronization silently produces exactly the staleness this story removes,
    and MAR-171 §6.4.3 set the precedent of rejecting a whole track atomically.
    The rejection is humane only because disabling always succeeds (§6.6), so the
    message names both remedies.
13. **Disabling leaves the boundary key in place.** It is a real keyframe that
    shapes the last segment and appears in the export; deleting it as a side
    effect of clearing a flag would destroy authored data and change the exported
    pose. Enable → disable is therefore not byte-symmetric, and that is the
    intended trade (§6.6).
14. **Retime pins both ends of an opted-in lane rather than rejecting.** The
    existing shared-bounds model already lets one immovable key freeze a whole
    selection, and a dopesheet box selection routinely spans a boundary key;
    rejecting would make the dopesheet unusable on an opted-in animation, while
    pinning produces the existing, silent `changed == false` result (§10.2).
15. **The GUI skips a managed boundary key and the Agent rejects it.** The same
    asymmetry MAR-169, MAR-170, and MAR-171 all use, for the same reason: a box
    selection is loose and a scripted selector list is not.
16. **The prerequisite check is not run at load.** The parser runs before any
    animation catalog exists and `animation_edits` in the same document can
    author the very duration in question, so the check would have a parse-order
    dependency with no correct answer. The structural checks that *are*
    expressible at load are enforced there, and the rest is enforced at enable
    time and by the sync (§8.4).
17. **MAR-172 adds no ImGui code.** The story's criterion 5 says "The **UI-free**
    loop-sync operation", and no criterion names a widget, unlike MAR-170's and
    MAR-171's. Adding one would cost render-statistic plumbing and an
    actual-frame smoke that no criterion asks for. The gap is recorded as a
    deferred follow-up with its exact location (§16).
18. **A new Agent operation rather than an argument on an existing one.**
    Criterion 5 asks for a "loop-sync operation" with its own dry-run,
    validation, affected-lane, mutation, and undo behaviour. Its selector shape
    is genuinely different — lanes, not keys, and no `time` — so overloading
    `timeline.set_interpolation` or `timeline.set_curve_mode` would make one
    operation take two incompatible selector arrays.
19. **The `.marrow` block stores booleans in a mirror of the `timeline_edits`
    tree, not a flat array of lane descriptors.** The tree makes duplicate
    entries structurally impossible because `Value::Object` is keyed, it reads as
    obviously parallel to the thing it qualifies, and its parser reuses the
    existing traversal shape.

## 18. Validation Strategy

### 18.1 UI-free focused tests — `marrow_timeline_model_tests`

The natural home for the pure lane-level rules: it already links
`marrow_editor`, already includes `src/editor`, and already owns the
authoring-boundary cases. New cases, all against in-memory `ProjectData` values
with a minimal effective skeleton:

- **Idempotence**: `synchronize_loop_boundaries()` twice in a row — the second
  call reports `synchronized_lane_count == 0`, `created_key_count == 0`, and a
  byte-identical project. This is §9.2's termination argument, asserted.
- **Default off**: a project with no opted-in lane returns `lane_count == 0`,
  `resolved_key_count == 0`, `changed == false`, and a byte-identical project —
  and, critically, **does not call the resolver at all**, proven by a project
  containing an auto key whose curve is deliberately stale and which the call
  leaves stale.
- **Create**: a three-key lane with `D = 1.5` gains a fourth key at exactly
  `float32(1.5)` whose value and `interpolation` are bit-equal to key 0's.
- **Adopt**: an existing key at `D` is overwritten in place; the key count does
  not change and `boundary_action == Adopted`.
- **Move**: after the duration changes from 1.5 to 1.2, the boundary key's time
  becomes exactly `float32(1.2)` and no other key moves.
- **Rewrite**: after key 0's value changes, only the boundary key's value
  changes; `created_key_count == 0` and `moved_key_count == 0`.
- **Mirror is bit-exact**: for a cubic key-0 curve, the boundary's four control
  points compare `==` as `AnimationScalar`, not within an epsilon.
- **Every rejection of §6.5**, each leaving the project byte-identical: no
  explicit duration; no key at time zero; `D` below `kNonEventKeySpacing`; a key
  past the boundary; a key inside the 1 ms spacing before it; two keys at the
  boundary time.
- **Single-key lane**: one key at `t = 0`, `D = 1.5` → a two-key constant lane,
  accepted.
- **Zero-duration animation**: explicit `D = 0` → rejected.
- **Discrete families**: `set_timeline_loop_sync()` on a Draw Order, Event, or
  Slot Attachment selector is rejected atomically (and the lane structs have no
  member to set, which the compiler enforces).
- **Duplicate lane selector**, **unresolvable lane selector**, and **empty lane
  list** each rejected atomically.
- **Disable never validates**: a lane in every one of the rejected states above
  is successfully disabled, the flag clears, and the boundary key is still there.
- **Retime pinning**: `retime_keyframes()` on the first key of an opted-in lane
  returns `changed == false` and `applied_delta == 0`; the same for the last key;
  the same for a selection containing either plus an unpinned middle key; and a
  selection of only middle keys still moves normally.
- **Inferred-duration floor**: `inferred_duration_excluding_loop_boundaries()`
  returns `animation.inferred_duration()` bit for bit when no lane is opted in,
  and drops to the second-to-last key time when one is.

### 18.2 UI-free focused tests — `marrow_project_smoke`, storage

A new `validate_mar172_loop_boundary_sync(const ProjectLoadResult&)` beside
MAR-169's, MAR-170's, and MAR-171's validators, called from `main` after
`validate_mar171_automatic_curves()`:

- **Default-off, byte-for-byte**: `serialize_project()` of the untouched fixture
  is byte-identical to a pre-MAR-172 build's output and contains no `loop_sync`.
  Save it, reload it, serialize again, compare.
- **Round trip**: author `set_animation_duration("idle", 1.5)`, enable
  `idle`/`spine`/`rotate`, assert the created boundary key equals §6.7's table;
  save; reload; assert the flag, the key time, the angle, and all four control
  points survive bitwise.
- **The `.marrow` text**: the saved document contains
  `"loop_sync"` with `"rotate": true` under `idle.bones.spine` and nowhere else,
  and no `false` leaf anywhere.
- **Opted-out lanes stay clean**: `arm_l` rotate serializes with no trace of the
  block.
- **Load validation**: hand-built documents each rejected with the expected
  message and JSON path — non-object `loop_sync`; missing `animations`; a
  non-boolean leaf; an unknown transform channel; `slots.<slot>.attachment`;
  `true` for a lane with no `timeline_edits` entry; `true` for a lane whose first
  key is at 0.25.
- **`validate_project_for_save()`**: a `ProjectData` built in memory with an
  opted-in lane whose first key is at 0.25 fails to save with the time-zero
  message, and one with an empty `keyframes` fails with the keyframe message.
- **Unknown-member preservation at the root**: a document carrying both
  `loop_sync` and an unknown top-level member round-trips both.

### 18.3 UI-free focused tests — `marrow_project_smoke`, behaviour

Within the same validator:

- **Adoption and materialization**: enable `aim`/`arm_l`/`rotate`, a
  runtime-only lane with an explicit duration of 0.5 and an existing key at 0.5.
  Assert the lane is materialized with **both** its keys copied (not replaced),
  `boundary_action == Adopted`, `changed_lane_count == 1`, and both key values
  unchanged at 30.
- **Slot Color and Deform**: enable `idle`/`body`/`color` and
  `idle`/`body`/`body_mesh` after the duration edit; assert a created boundary
  key with colour `(1,1,1,1)` and with an all-zero eight-element offset vector
  respectively, each mirroring key 0.
- **Missing prerequisites, on the real fixture**: enabling
  `idle`/`spine`/`rotate` **before** authoring a duration is rejected (no
  explicit duration), and enabling `idle`/`arm_l`/`rotate` after authoring one is
  rejected (its first key is at 0.25). Both leave the project byte-identical.
- **First-key value change propagates**: `offset_keyframe_scalars()` on key 0
  followed by `synchronize_loop_boundaries()` rewrites only the boundary key's
  value, to the same number.
- **First-key curve change propagates**: `set_keyframe_interpolation()` on key 0
  followed by the sync rewrites only the boundary key's easing, bit-exactly.
- **Duration grow and shrink**: 1.5 → 2.0 moves the boundary to 2.0; 2.0 → 1.2
  moves it to 1.2; 1.2 → 1.0005 is rejected with the project byte-identical;
  1.2 → 0.9 is rejected by `set_animation_duration()`'s own message.
- **Duration validation is unchanged with no opt-in**: the whole
  `set_animation_duration()` accept/reject table from the existing MAR-155 cases
  is re-run on a project with `loop_sync` absent and produces identical results
  and identical messages.
- **Clipboard hygiene**: after `copy_selected_timeline_keys()` on an opted-in
  lane, every lane in `clipboard.project_fragment` has `loop_sync == false`.
- **Disable**: `set_timeline_loop_sync(false)` clears the flag, leaves the
  boundary key present and unchanged, reports `boundary_action == Released`, and
  the following sync reports `lane_count == 0`.
- **Stale pair is legal**: write a project with `loop_sync: true` beside a
  boundary key whose value does not match key 0; assert it loads with no error
  and that `serialize_project()` of the loaded project is byte-identical to the
  input — proving load never synchronizes. Then one sync repairs it.

### 18.4 The MAR-171 seam

MAR-171 §9.2 wires the duration trigger as an honest no-op and §18.3 asserts
`resolved_key_count == 0`. MAR-172 changes what that seam does, and the
assertion must be **split, not replaced**:

- **Keep MAR-171's assertion verbatim, scoped**: with auto keys present and
  **no lane opted in**, a duration change still reports
  `resolved_key_count == 0` and leaves `serialize_project()` byte-identical apart
  from the duration itself. Add the words "with no lane opted in" to the test's
  failure message; change nothing else about it. If the assertion is reworded
  rather than scoped, a future regression that starts resolving on every duration
  change goes undetected.
- **Add the opted-in case**: with auto keys present on `idle`/`spine`/`rotate`
  **and that lane opted in**, a duration change from 1.5 to 2.0 moves the
  boundary key, changes the last segment's span, and therefore reports
  `resolved_key_count >= 1`. Assert the specific segment whose control points
  changed and that the boundary key's own easing is the post-resolve mirror of
  key 0's, not the pre-resolve one — which is §9.2's phase ordering, asserted.
- **Add the demotion guard**: after a sync on a lane whose key 0 is `Auto`, the
  boundary key's `curve_mode` is `Auto` and key 0's is still `Auto`. If phase 2
  ever calls `set_keyframe_interpolation()`, both become `Manual` and this test
  fails — which is exactly the point.

If MAR-171 landed without `curve_mode`, this section reduces to the first two
bullets and the plan records that.

### 18.5 Headless shell smoke — `src/editor/shell_smoke_graph.cpp`

A new `validate_timeline_loop_sync_shell_smoke()` with an isolated session,
declared in `shell_smoke_scenarios.hpp` and called from `shell_smoke.cpp` after
MAR-171's scenario:

- author an explicit duration, enable `spine`/`rotate` through
  `set_timeline_loop_sync()` inside one transaction, and assert **one** history
  entry, the created boundary key, and a rebuilt dopesheet whose `key_times` has
  four entries;
- **criterion 3 direct, five ways**, each asserted to be **one** history entry
  with the boundary updated inside it: a MAR-168 value drag on key 0; a MAR-169
  handle drag on key 0; a MAR-170 preset applied to key 0; a dopesheet retime of
  a **middle** key; and an `animation.set_duration`-equivalent duration edit;
- **retime pinning**: a retime gesture whose selection includes the boundary key
  produces `applied_delta == 0`, no history entry, and an unchanged project;
- **selection skip**: a preset applied to a selection containing the boundary key
  writes the other keys, reports the boundary as skipped in the status message,
  and leaves the boundary key's easing equal to key 0's;
- **removal guard**: `remove_selected_timeline_keys()` with only the boundary key
  selected changes nothing and sets a status message; with only the time-zero key
  selected, the transaction fails and `serialize_project()` is byte-identical;
- **paste**: pasting a copied key at a time past the boundary fails the
  transaction with the project byte-identical; pasting inside the clip succeeds
  in one history entry with the boundary re-mirrored;
- **auto-extend interaction**: adding a key at a time past the duration on a
  **different, not opted-in** lane grows the duration through the session's
  auto-extend **and** moves the opted-in lane's boundary key to the new duration
  in the same history entry — the §9.1 hole, asserted;
- **undo/redo**: undo the enable and assert the flag, the boundary key, and
  `selected_keys` / `active_key` all return; redo restores them;
- **cancel**: `cancel_authoring_gestures()` during a value drag on key 0 restores
  `serialize_project()`, `undo_count()`, `redo_count()`, `project_revision()`,
  `dirty()`, the rebuilt dopesheet `key_times`, and the boundary key;
- **materialization**: the first enable on the runtime-only `slot:0:Color` lane
  copies all its keys into the project rather than replacing the lane;
- `agent_operation_descriptor_count() == 59` before and after every case.

### 18.6 Export — the criterion this story must not fake

MAR-168 exported the mutated project and reported `JSON 14338 bytes` against the
untouched baseline's `14336`. MAR-169 exported only the **untouched** baseline
and reported `14336`, so no authored curve was ever proven to reach a runtime
file even though its criterion 6 named export, and `AGENTS.md` recorded PASS.
MAR-170 got it right by exporting the mutated project and asserting the exact
serialized values. **MAR-172 must follow MAR-170.**

The MAR-172 export block in `marrow_project_smoke` must:

1. take the project it just **mutated** — explicit duration 1.5 on `idle`,
   `spine`/`rotate` and `body`/`color` opted in, boundary keys created;
2. call `export_runtime_assets()` on **that** project into
   `/tmp/marrow_mar172_loop.mskl` and `/tmp/marrow_mar172_loop.mbin`;
3. load the exported `.mskl` with `runtime::load_skeleton_data()` and assert:
   - `idle`'s `duration` is `1.5`;
   - the `spine` rotate timeline has **four** keyframes, up from three;
   - keyframe 3 has `time == 1.5f` and `angle` **bit-equal** to keyframe 0's;
   - keyframe 3's `interpolation` is `CubicBezier` with all four control points
     bit-equal to keyframe 0's;
   - the `body` color timeline has four keyframes and its last colour is
     bit-equal to its first;
4. read the exported `.mskl` as **text** and assert it contains no `loop_sync` —
   the direct proof of §8.6's export neutrality;
5. validate the `.mbin` against the `.mskl` with the existing
   `validate_binary_export()` helper;
6. print `MAR-172 loop boundary export: JSON <n> bytes, MBIN <m> bytes.` and,
   separately, the untouched baseline's sizes.

**The expected observable difference.** The baseline export is `14336` bytes.
The synchronized export must be **strictly larger in both files**: the `.mskl`
gains two whole keyframe objects plus a `duration` member, and — unlike MAR-171's
curve-only change — the `.mbin` gains two real keyframe records, so its size must
grow too. A reviewer seeing `14336` in the MAR-172 JSON row, or the baseline
`3984` in the MBIN row, must treat it as the MAR-168/169 defect recurring; step 3
must fail before any byte count is consulted.

### 18.7 Agent smoke — `marrow_agent_dispatch_smoke`

- the registry is exactly 59 operations, the new row's metadata is (`edit`,
  mutating, not review, dry-run supported), and it sits immediately after
  `timeline.set_curve_mode` in `kExpectedOperations`;
- `timeline.set_loop_sync` dry run leaves `project_revision()`, `undo_count()`,
  and `dirty()` unchanged and reports the correct `previous_enabled`,
  `previous_boundary`, `boundary_action`, and resulting `boundary` per lane;
- a live call mutates, reports `changed_lane_count`, `created_key_count`, and
  `boundary_action == "created"`, and adds exactly one history entry;
- a second identical live call returns `no_change`;
- the `aim`/`arm_l` runtime-only lane reports `boundary_action == "adopted"` and
  a live call materializes it;
- `set_transform` on the time-zero key of an opted-in lane updates the boundary
  key in the **same** history entry, proven by a follow-up
  `timeline.set_loop_sync` dry run's `previous_boundary`;
- `animation.set_duration` on an opted-in animation moves the boundary key in one
  history entry that one `undo` fully reverses, and a shrink below the spacing
  floor is rejected with the project unchanged;
- `undo` restores the flag and the boundary key, verified by a follow-up dry run;
- rejection cases, each with the project unchanged: missing `enabled`,
  non-boolean `enabled`, a `deform` lane missing `attachment`, a `draw_order`
  lane, an `event` lane, a `slot_attachment` lane, an unknown kind, an unknown
  transform channel, an unresolvable lane (expect `not_found`), a duplicate lane,
  an empty `lanes` array, enabling without an explicit duration, and enabling a
  lane with no time-zero key;
- disabling a lane in a rejected state **succeeds**, proving §6.6 through the
  Agent surface.

### 18.8 MCP — `tools/mcp/test_client.py`

- `len(registry_names) == 59`, `len(mcp_names) == 59`,
  `set(registry_names) == set(mcp_names)`, all names unique;
- `timeline.set_loop_sync` present in `new_edit_operations` and in the MCP edit
  tool set, with its registry metadata row asserted explicitly;
- `animation.set_duration` → dry run → live enable → read-back → `undo` →
  read-back, proving the boundary key is created, reported at
  `boundary_time == 1.5` with `angle` matching key 0 to four decimal places, and
  restored;
- a `set_transform` on the time-zero key between two `timeline.set_loop_sync` dry
  runs, proving the boundary followed the first key through the MCP surface;
- rejection of `"enabled": "yes"`, of a `draw_order` lane, of a lane entry
  carrying a `time` field that the schema does not declare but the C++ gate
  ignores, and of enabling a lane whose animation has no explicit duration —
  proving the schema did not loosen the C++ gate.

### 18.9 Display and compatibility gates

See the "Full verification checklist" in
`docs/superpowers/plans/2026-08-30-mar-172-loop-boundary-key-sync.md`.
MAR-172 adds no ImGui code and therefore no display-test coverage of its own; the
existing display gates must still pass unchanged. Automated display tests prove
the exercised ImGui/display path only and add no manual-visible-UI, Windows 11,
or physical-input qualification credit.

## 19. Documentation and Milestone Closure

After code and every required gate pass:

- add a `MAR-172 Loop Boundary Key Synchronization Validation Results` section to
  `AGENTS.md` in the existing table plus command-output format, and update
  `Current Validation`'s registry line to 59 operations;
- update `AGENTS.md`'s `Project State` line so MAR-172 is complete and MAR-173 is
  next;
- update `docs/root1/discription.md`, `quick-start.md`, `concepts.md`,
  `editing-gap-analysis.md`, `refector.md`, and `platform-validation.md` where
  the "MAR-172 is next" wording, the registry size, or the timeline-authoring
  contract is now stale, applying §14's historical/current rule to every hit;
- `docs/root1/format-spec.md` gains a new `### loop_sync` section in the
  `.marrow` chapter, between `snap` and `animation_edits`, documenting the tree
  shape, the boolean leaves, the absent-means-off default, the three supported
  families and why the other three are excluded, the load and save validation
  rules, the derived managed-key identity, and the statement that the block never
  enters `.mskl` or `.mbin` and that no format version changes. The `loop_sync`
  key is added to the top-level key list. The `animation_edits` `set_duration`
  paragraph gains one sentence recording that a managed loop boundary does not
  constrain the duration it follows;
- `docs/root1/concepts.md` gains a short paragraph on why a looping clip needs a
  boundary key, referencing `AnimationData::duration()` and the wrap;
- mark `MAR-172` done with the verified completion date and leave `MAR-173` open;
- leave MAR-192 through MAR-210 open and add no platform qualification credit;
- change this document's status to `Implemented and validated` only after every
  gate is green.

## 20. Commercial-Tool Reference Boundary

Loop-boundary key management is standard in 2D animation tooling; the
interaction direction is informed by the official documentation:

- Spine, animation duration and looping:
  <https://esotericsoftware.com/spine-animations>
- Live2D Cubism, animation looping and keyform management:
  <https://docs.live2d.com/en/cubism-editor-manual/animation-basic/>

Marrow deliberately does **not** copy a global "make loopable" command that
rewrites every timeline at once, a symmetric first/last sync, an automatic
opt-in, or a boundary key on discrete timelines. The per-lane opt-in, the
explicit-duration prerequisite, the one-directional contract, and the derived
managed identity take precedence over surface similarity to another editor.
