# MAR-170 Fixed Curve Presets and Remembered Defaults Design

**Date:** 2026-08-30

**Status:** Implemented and validated (2026-08-30)

**Authority:** `.agents/tasks/prd-marrow-runtime.json` story `MAR-170` and
`docs/root1/discription.md`

**Depends on:** MAR-169, whose interpolation primitive, segment-wide curve
identity model, Linear/Stepped conversion rule, and
`timeline.set_interpolation` operation are defined in
`docs/superpowers/specs/2026-08-30-mar-169-graphical-bezier-handles-design.md`.
MAR-169 in turn inherits MAR-168's gesture and transaction model from
`docs/superpowers/specs/2026-08-30-mar-168-graph-key-time-value-editing-design.md`.

> **MAR-169 is in flight while this document is written.** Every API contract
> quoted from it below is quoted from its *plan*, not from as-built source. The
> implementation plan's Task 0 is mandatory and must reconcile against the code
> that actually exists.

## 1. Goal

MAR-169 makes one key's easing draggable and exposes the identical mutation as
`timeline.set_interpolation`. It deliberately leaves two things undone: there is
no way to assign a *named* curve to many keys at once, and every newly authored
key is hardcoded to Linear regardless of what the animator has been using all
day.

MAR-170 closes both, and nothing else:

1. Six fixed, deterministic presets — Linear, Stepped, Ease, Ease-In, Ease-Out,
   Ease-In-Out — applicable to the compatible selected keys as **one** previewed,
   undoable transaction, from the GUI, the C++ Agent, and the Python MCP facade,
   all through MAR-169's single `set_keyframe_interpolation()` primitive.
2. A remembered **default curve** that seeds newly authored Transform, Deform,
   and Slot Color keys, stored in the versioned user-local `editor-settings.json`
   that MAR-156 already built, never in the `.marrow` project, and never
   dirtying the project.

The story's own framing is the design constraint: presets are *fixed*. They are
constants in the source, not user-editable curve libraries, not project data,
and not a format concern.

## 2. Approved Product Decisions

1. The preset set and its exact control points are fixed by acceptance criterion
   1 and are the CSS Easing Level 1 constants. They are declared once, in
   `include/marrow/editor/authoring.hpp`, as a `constexpr` table (§6).
2. Applying a preset writes **every compatible key in the current timeline
   selection** — deliberately the complement of MAR-169, whose handle drag is
   active-key-only. MAR-169 §17.1 already records that multi-key easing
   assignment belongs here.
3. **The GUI filters, the Agent rejects.** A selection containing Draw Order,
   Event, or Slot Attachment keys applies to the compatible subset and reports
   the skipped count. An Agent call naming an incompatible key is rejected
   atomically, as MAR-169 already specifies (§10.3).
4. One preset application is **one** `EditTransaction`, one history entry, and
   one `refresh_runtime()` preview, whatever the key count. MAR-170 adds **no
   gesture**: there is no drag, no dead zone, no entry in
   `authoring_gesture_active()`, and no entry in `cancel_authoring_gestures()`.
5. The remembered default lives in **preferences**, in the `default_curve` field
   that `editor-settings.json` v1 already defines and `PreferenceStore` already
   parses and writes. MAR-170 adds **no preference field and no version bump**
   (§8).
6. Applying a preset does **not** change the remembered default. The default is
   changed only by the explicit `Default:` control. A one-off edit must not have
   a persistent, cross-project side effect (§18.4).
7. The default seeds only keys created by the **shell's** "Add Key At Playhead"
   path. Agent-created keys, pasted keys, and MAR-169's Linear/Stepped→Cubic
   conversion seed are untouched (§9.2).
8. The UI's "current preset" readout is a **pure function of the stored curve**,
   recomputed every frame, compared bit-exactly in `float32`. Dragging a handle
   away from a preset makes it read `Custom` with no state to invalidate (§7).
9. MAR-170 adds **no Agent operation**. It widens the existing
   `timeline.set_interpolation` argument vocabulary additively. The registry
   stays at exactly **57** operations and every count assertion in the tree is
   unchanged (§14).
10. Automatic/manual handle metadata, Fritsch-Carlson tangents, project-local
    curve intent, loop boundary synchronization, time scaling, and preview speed
    remain out of scope (MAR-171 through MAR-174).

## 3. Scope

### 3.1 Surfaces that gain preset application

| Surface | Target keys | Mechanism |
| --- | --- | --- |
| Graph tab toolbar | every compatible key in `timeline_editor.selected_keys` | six preset buttons |
| Dopesheet toolbar | same | six preset buttons |
| Graph + Dopesheet toolbars | — | one `Default:` combo, preference-only |
| Agent `timeline.set_interpolation` | the caller's explicit selectors | four new `interpolation` string tokens |
| MCP `timeline.set_interpolation` | same | same |

Both timeline tabs read the same `TimelineEditorState::selected_keys`, so one
ImGui-free helper serves both and there is exactly one selection semantics.

Compatible families are the three that carry an `interpolation` field:
`TransformKeyframeEdit`, `DeformKeyframeEdit`, `SlotColorKeyframeEdit`.
`DrawOrderKeyframeEdit`, `EventKeyframeEdit`, and `SlotAttachmentKeyframeEdit`
have no such field, which is precisely what "continuous" means in criterion 3.

Note this is a **broader** family set than the Graph tab projects: Deform is not
graphed (MAR-167's projection rule, reaffirmed by MAR-169 §3.1) but a Deform key
selected in the Dopesheet is a legitimate preset target. The preset path is a
selection operation, not a projection operation.

### 3.2 Surfaces that gain the remembered default

| Site | Today | After MAR-170 |
| --- | --- | --- |
| `timeline_controller.cpp` `sample_transform_keyframe()` | `Interpolation::linear()` | `curve_preset_interpolation(state.preferences.default_curve)` |
| `timeline_controller.cpp` `sample_deform_keyframe()` | `Interpolation::linear()` | same |
| `timeline_controller.cpp` `add_timeline_key_at_playhead()` Slot Color branch | `Interpolation::linear()` | same |

Those three literals are the complete set of "newly authored continuous
segment" sites in the shell. They are found by
`rg -n 'Interpolation::linear\(\)' src/editor/timeline_controller.cpp` and the
implementation plan asserts the count.

### 3.3 Explicit exclusions

MAR-170 does not add:

- automatic/manual curve intent, a driver scalar, or Fritsch-Carlson tangents
  (MAR-171) — and critically, **no `.marrow` field of any kind**, so MAR-171
  remains free to define the project-local metadata block it needs;
- loop-boundary key synchronization (MAR-172), selected-key time scaling
  (MAR-173), preview playback speed (MAR-174);
- user-defined, editable, or named custom presets. The six are constants;
- a curve library, curve copy/paste, or a "match previous key's curve" action;
- per-component curves in any form (structurally impossible, MAR-169 §8);
- incoming handles (the runtime has one outgoing easing per key);
- any change to MAR-169's handle drag, its `[1/3, 1/3, 2/3, 2/3]` conversion
  seed, its clamp/reject split, or its status messages;
- any new Agent operation, and any change to `timeline.describe`,
  `set_transform`, `set_deform_keyframe`, `set_slot_color_keyframe`, or the
  legacy `interpolation_arg()` those three use;
- an Agent or MCP surface for reading or writing the user's preference file
  (§18.9);
- wiring `EditorPreferences::recent_projects` to a Recent Projects menu. MAR-170
  must **round-trip** that field untouched but must not consume it;
- refactoring the per-key `Interpolation` combo and `Bezier X1/Y1/X2/Y2` fields
  in `shell_timeline.cpp` onto the shared primitive. MAR-169 §16 already
  recorded that as a deliberate follow-up; MAR-170 does not enlarge it either,
  and the resulting inconsistency (that inspector still writes
  `cubic_bezier(0.25, 0.1, 0.75, 0.9)` for its "Bezier" entry) is noted, not
  fixed;
- manual-visible-UI, Windows 11, or physical-input qualification credit.
  MAR-192 through MAR-210 stay open.

### 3.4 Compatibility boundaries

MAR-170 changes none of the following:

- **`.marrow` project schema.** No field added, removed, or reinterpreted. The
  only bytes MAR-170 can change in a project are the existing `curve` values,
  written through MAR-169's primitive;
- **`.mskl` v1** and **`.mbin` v2** — identical `curve` encoding, unchanged
  readers and writers;
- **C ABI v1** — `include/marrow/c_api/**` and `src/c_api/**` untouched;
- **`editor-settings.json` v1** — `kEditorSettingsVersion` stays `1`, the
  `default_curve` field, its six tokens, its parser, its Linear fallback, its
  unknown-field preservation, and its atomic temp-plus-rename write are all
  MAR-156 code and are used, not modified. **MAR-170 changes zero bytes of
  `src/editor/preferences.cpp` and `include/marrow/editor/preferences.hpp`;**
- **Agent/MCP registry size** — exactly 57, unchanged;
- `SelectionSet` entity identity, runtime/GPU resource ownership, the dopesheet
  retime contract, MAR-168's point drag, and MAR-169's handle drag.

The only public-header change is **additive**: the preset table and three
accessors appended to `include/marrow/editor/authoring.hpp` after MAR-169's
`set_keyframe_interpolation()`.

**Why the format is safe by construction.** Both loaders reject a `curve` array
whose x control points leave `[0, 1]`:

```text
src/editor/project.cpp:1639          "bezier x control points must stay within [0, 1]"
src/runtime/skeleton_parse.cpp:1727  "bezier x control points must stay within [0, 1]"
```

Every MAR-170 curve comes from one of exactly six compile-time constants whose
x values are `0.25`, `0.25`, `0.42`, `1.0`, `0.0`, `0.58`, `0.42`, `0.58` — all
inside `[0, 1]` as `double` **and** after `float32` narrowing (§6.3) — and every
write still passes through MAR-169's `set_keyframe_interpolation()`, which
enforces the same predicate the loaders enforce. A `static_assert` over the
table makes a violating preset a compile error, not a runtime failure (§6.4).

## 4. Existing Boundaries Reused

| Concern | Reused primitive | Location |
| --- | --- | --- |
| Curve write, validation, atomic rollback | `set_keyframe_interpolation()` | `authoring` (MAR-169) |
| Curve write result shape | `TimelineInterpolationResult` | `authoring.hpp` (MAR-169) |
| Preset token vocabulary and enum | `CurvePreset`, `curve_preset_token()`, `parse_curve_preset()` | `preferences.hpp` / `preferences.cpp` (MAR-156) |
| Versioned atomic settings I/O | `PreferenceStore::load()` / `save()` | `preferences.cpp` (MAR-156) |
| Cross-platform settings path + test override | `resolve_preference_settings_path()`, `MARROW_CONFIG_HOME` | `preferences_internal.hpp` (MAR-156) |
| Project-domain key resolution | `TimelineKeySelector`, `timeline_key_selector()` | `authoring` / `timeline_controller` |
| Overlay materialization | `ensure_transform_timeline_edit_index()` and siblings | `timeline_controller` |
| Live preview / one history entry | `EditorSession::EditTransaction` | `session` |
| Gesture mutual exclusion | `authoring_gesture_active()` | `shell_state` |
| Agent dry-run / commit / no-change shape | `commit_or_error()`, `CommitPolicy` | `agent_dispatch_internal` |
| Agent easing argument parsing | `interpolation_request_arg()` | `agent_dispatch` (MAR-169) |
| Runtime curve construction and LUT cache | `runtime::Interpolation::cubic_bezier()` | `runtime/animation` |

MAR-170 introduces exactly one new concept — the preset table — and reuses
everything else.

## 5. Architecture

```text
include/marrow/editor/authoring.hpp
    kCurvePresets[6]                       <- the only place the numbers live
    curve_preset_definition(CurvePreset)
    curve_preset_interpolation(CurvePreset)
    curve_preset_of(const Interpolation&)  <- readout / identity
          |                    |                        |
          |                    |                        |
   shell UI            key seeding                agent handler
          |                    |                        |
          v                    v                        v
 apply_timeline_curve_preset  sample_*_keyframe   timeline.set_interpolation
   (timeline_controller)      (timeline_controller)  (agent_handlers_editing)
          |                                              |
          +----------> set_keyframe_interpolation() <-----+
                          (MAR-169 primitive)
                                  |
                       EditorSession::EditTransaction

src/editor/shell_preferences.hpp/.cpp   (NEW, shell-only, ImGui-free)
    load_shell_preferences(ShellState*)        <- once, at startup
    set_shell_default_curve(ShellState*, CurvePreset)  <- on the combo change
          |
          v
    marrow::editor::PreferenceStore   (MAR-156, unmodified)
```

Two independent flows meet only at the preset table. The preference flow never
touches `EditorSession`, `ProjectData`, the runtime, or the history; the preset
application flow never touches the preference file. That separation is what
makes criterion 3's "without dirtying the project" provable rather than
asserted.

## 6. The Six Presets

### 6.1 The table

| Preset | Token | Display | Kind | `[cx1, cy1, cx2, cy2]` |
| --- | --- | --- | --- | --- |
| `CurvePreset::Linear` | `linear` | `Linear` | `Linear` | — |
| `CurvePreset::Stepped` | `stepped` | `Stepped` | `Stepped` | — |
| `CurvePreset::Ease` | `ease` | `Ease` | `CubicBezier` | `[0.25, 0.1, 0.25, 1.0]` |
| `CurvePreset::EaseIn` | `ease_in` | `Ease-In` | `CubicBezier` | `[0.42, 0.0, 1.0, 1.0]` |
| `CurvePreset::EaseOut` | `ease_out` | `Ease-Out` | `CubicBezier` | `[0.0, 0.0, 0.58, 1.0]` |
| `CurvePreset::EaseInOut` | `ease_in_out` | `Ease-In-Out` | `CubicBezier` | `[0.42, 0.0, 0.58, 1.0]` |

The enum, the tokens, and their order are **not new** — `CurvePreset` and
`curve_preset_token()` are MAR-156 code
(`include/marrow/editor/preferences.hpp:15`, `src/editor/preferences.cpp:131`)
that has round-tripped through `marrow_preference_tests` since 2026-07-18 with
no consumer. MAR-170 is that consumer. The table order is the enum order is the
UI order is the identity-search order, so there is exactly one sequence to keep
in step.

Linear and Stepped carry `control_points = {0, 0, 0, 0}`, which is never read
because `set_keyframe_interpolation()` ignores control points for those kinds
(MAR-169 §9.2).

### 6.2 Why these four quadruples

They are the CSS Easing Level 1 `ease`, `ease-in`, `ease-out`, and `ease-in-out`
timing functions, reproduced exactly. Three independent reasons:

1. **They are the values the story fixes.** Criterion 1 states them verbatim.
   There is no design freedom here; the numeric justification below exists to
   prove they are *safe*, not to choose them.
2. **They are the vocabulary animators and the models driving the MCP surface
   already have.** An agent asked for "ease-in" produces the same curve a web
   designer, a Spine user, and an After Effects user expect. Inventing Marrow's
   own quadruples would make every cross-tool description wrong.
3. **Each is provably well-posed for this runtime.** Verified below.

**X monotonicity.** MAR-169 §6 establishes that `transform(alpha)` inverts
`X(t)` numerically, so `X` must be non-decreasing on `[0, 1]` or the solve is
ill-posed. With `a = cx1`, `b = cx2`, `u = 1 - t`, `v = t` (`u, v >= 0`):

```text
X'(t)/3 = a·u² + 2(b - a)·u·v + (1 - b)·v²
```

On the closed first quadrant this is non-negative whenever `a >= 0`,
`1 - b >= 0`, and `b - a >= 0`, because all three coefficients are then
non-negative. Every preset satisfies `cx2 >= cx1`:

| Preset | `cx1` | `cx2` | `cx2 - cx1` | `1 - cx2` | X non-decreasing |
| --- | ---: | ---: | ---: | ---: | --- |
| Ease | 0.25 | 0.25 | 0.00 | 0.75 | yes |
| Ease-In | 0.42 | 1.00 | 0.58 | 0.00 | yes |
| Ease-Out | 0.00 | 0.58 | 0.58 | 0.42 | yes |
| Ease-In-Out | 0.42 | 0.58 | 0.16 | 0.42 | yes |

No preset needs the discriminant branch of the argument at all, which makes the
proof a sign check rather than an inequality. (For the record, MAR-169 §6 states
the stronger claim that `(b - a)² - a(1 - b) <= 0` holds on all of `[0, 1]²`;
that specific inequality is false at, for example, `(a, b) = (0, 1)`, where it
evaluates to `1`. The **conclusion** — `X` non-decreasing — is nonetheless
correct there and everywhere in `[0, 1]²`, because when `b >= a` the cross term
is non-negative and no discriminant condition is required, and when `b < a` the
discriminant condition does hold. MAR-170 relies only on the sign argument, so
nothing here depends on resolving that wording.)

**Ease-In's degenerate endpoint.** Ease-In has `P2 = (1, 1) = P3`, so
`X'(1) = 0`. The inverse is still unique — `X` is strictly increasing on
`[0, 1)` and continuous — but Newton's iteration has zero gradient exactly at
the right endpoint. `Interpolation::transform()` clamps `alpha` into `[0, 1]`
and falls back to bisection, and `X(1) = 1` maps to `Y(1) = 1` exactly. A
runtime test samples each preset on a grid and asserts finiteness, monotonicity,
and exact endpoints rather than assuming the solver copes (§19.1).

**No overshoot.** Every preset has `0 <= cy1 <= cy2 <= 1`, so `Y` is
non-decreasing and stays in `[0, 1]`. A preset can therefore never push a value
past its segment endpoints. Overshoot in Marrow is reachable **only** by
MAR-169's manual handle drag. This is a user-visible contract worth stating in
the docs, and it is asserted by a test.

### 6.3 Float32 narrowing

`CubicBezierControlPoints` holds four `runtime::AnimationScalar` (= `float`), so
every stored preset is the narrowed value:

| Preset | stored `cx1` | stored `cy1` | stored `cx2` | stored `cy2` |
| --- | --- | --- | --- | --- |
| Ease | `0.25` | `0.10000000149011612` | `0.25` | `1.0` |
| Ease-In | `0.41999998688697815` | `0.0` | `1.0` | `1.0` |
| Ease-Out | `0.0` | `0.0` | `0.5799999833106995` | `1.0` |
| Ease-In-Out | `0.41999998688697815` | `0.0` | `0.5799999833106995` | `1.0` |

Two consequences the implementation must honour:

1. **The `[0, 1]` invariant survives narrowing.** `0.42` narrows *down* and
   `0.58` narrows *down*; `0.0`, `0.25`, and `1.0` are exactly representable.
   No preset x value crosses a boundary, so MAR-169 §9.2's requirement to
   validate both the `double` and the narrowed `float` is satisfied by every
   preset with margin.
2. **Preset identity must compare narrowed values.** Comparing the stored
   `float` `0.41999998688697815` against the `double` literal `0.42` fails.
   `curve_preset_of()` therefore narrows the table entry before comparing
   (§7.2). Getting this wrong makes a just-applied preset immediately read
   `Custom`, which is the single most likely implementation bug in this story.

### 6.4 Where the constants live, and why not in `preferences.hpp`

`CurvePreset` lives in `include/marrow/editor/preferences.hpp`. The natural urge
is to put the quadruples next to it. That is rejected.

`preferences.hpp` includes only `<filesystem>`, `<string>`, `<vector>`, and
`marrow/runtime/json.hpp`. MAR-156's whole deliverable was a preference service
provably isolated from the project, session, runtime, and agent surfaces —
`docs/root1/editing-gap-analysis.md:294` records that isolation as the
checkpoint, and `preference_store_tests.cpp`'s
`test_editor_session_isolation` case pins it. Pulling
`marrow/runtime/animation.hpp` into `preferences.hpp` to name
`runtime::InterpolationKind` would breach it.

The table therefore lives in `include/marrow/editor/authoring.hpp`, which
already includes `marrow/editor/project.hpp` and through it
`marrow/runtime/animation.hpp`, and which gains
`#include "marrow/editor/preferences.hpp"` for the enum alone. The dependency
arrow is **authoring → preferences**, one-directional, and `PreferenceStore`
stays exactly as isolated as MAR-156 left it.

```cpp
/** @brief One fixed, deterministic easing preset. */
struct CurvePresetDefinition {
    CurvePreset preset{CurvePreset::Linear};
    std::string_view token;          // the editor-settings.json token
    std::string_view display_name;   // "Ease-In-Out"
    runtime::InterpolationKind kind{runtime::InterpolationKind::Linear};
    std::array<double, 4> control_points{};   // unused for Linear and Stepped
};

/** @brief The six fixed presets in stable enum, UI, and search order. */
inline constexpr std::array<CurvePresetDefinition, 6> kCurvePresets{{
    {CurvePreset::Linear,    "linear",      "Linear",
     runtime::InterpolationKind::Linear,  {0.0, 0.0, 0.0, 0.0}},
    {CurvePreset::Stepped,   "stepped",     "Stepped",
     runtime::InterpolationKind::Stepped, {0.0, 0.0, 0.0, 0.0}},
    {CurvePreset::Ease,      "ease",        "Ease",
     runtime::InterpolationKind::CubicBezier, {0.25, 0.1, 0.25, 1.0}},
    {CurvePreset::EaseIn,    "ease_in",     "Ease-In",
     runtime::InterpolationKind::CubicBezier, {0.42, 0.0, 1.0, 1.0}},
    {CurvePreset::EaseOut,   "ease_out",    "Ease-Out",
     runtime::InterpolationKind::CubicBezier, {0.0, 0.0, 0.58, 1.0}},
    {CurvePreset::EaseInOut, "ease_in_out", "Ease-In-Out",
     runtime::InterpolationKind::CubicBezier, {0.42, 0.0, 0.58, 1.0}},
}};
```

A `constexpr` predicate plus `static_assert` in `src/editor/authoring.cpp`
proves, at compile time, that every cubic entry keeps `cx1` and `cx2` inside
`[0, 1]`, that every entry's `preset` field equals its index in enum order, and
that the table has one entry per enumerator. A preset that would violate the
format invariant cannot be compiled, let alone written to a file.

## 7. Preset Identity and the `Custom` Readout

### 7.1 The question

Criterion 5's "preserve segment-wide easing" and the product question "what does
the UI show as the current preset after a handle drag" both need one answer:
given a stored `Interpolation`, which preset is it?

### 7.2 The rule

```cpp
/** @brief Names the preset an easing exactly is, or nullopt for a custom curve. */
std::optional<CurvePreset> curve_preset_of(const runtime::Interpolation& interpolation);
```

```text
kind == Linear   -> CurvePreset::Linear
kind == Stepped  -> CurvePreset::Stepped
kind == CubicBezier:
    for each cubic entry of kCurvePresets, in table order:
        if  stored.cx1 == static_cast<AnimationScalar>(entry.control_points[0])
        and stored.cy1 == static_cast<AnimationScalar>(entry.control_points[1])
        and stored.cx2 == static_cast<AnimationScalar>(entry.control_points[2])
        and stored.cy2 == static_cast<AnimationScalar>(entry.control_points[3])
            -> entry.preset
    -> std::nullopt   ("Custom")
```

Four properties this rule has, each deliberate:

1. **Stateless.** There is no "last applied preset" flag anywhere — not in
   `ShellState`, not in `TimelineEditorState`, not in the project. The readout
   is recomputed from the stored curve every frame, so it cannot go stale, and
   undo/redo/reload need no invalidation. This is the same discipline MAR-167
   used for the whole graph projection.
2. **Exact, not approximate.** Comparison is bit-exact in `float32`. No epsilon.
   An epsilon would report "Ease" for a hand-dragged curve that a save/reload
   would show as different numbers, which is a lie the user cannot act on.
   Because the preset was itself stored through the same narrowing, a preset
   applied by MAR-170 always reads back as that preset — including after
   `serialize_project()`, save, reload, `.mskl` export, and `.mbin` export
   (asserted in §19.2).
3. **Answers the handle-drag question directly.** Applying Ease and then
   dragging either handle by one pixel changes at least one stored `float`, so
   the readout becomes `Custom` on the very next frame. Dragging back to the
   exact stored values restores `Ease` — correctly, because the stored curve
   genuinely *is* Ease again.
4. **Table-order first match.** No two presets share a quadruple, so the order
   never matters in practice; fixing it makes the function total and testable
   anyway.

### 7.3 Not a persistence concern

`curve_preset_of()` reads; it never writes. No project, `.mskl`, `.mbin`, or
preference byte records which preset a curve "is". A curve is its four numbers.
This is what keeps MAR-170 free of any format change, and it is also what keeps
MAR-171 free to introduce project-local *intent* metadata without conflicting
with anything MAR-170 stored.

## 8. The Remembered Default

### 8.1 Preferences, not the project

Criterion 3 names the store: "atomically stored in editor-settings.json without
dirtying the project." Beyond the story text, the split is principled and the
codebase already demonstrates both sides:

| Kind of setting | Store | Precedent | Why |
| --- | --- | --- | --- |
| Reproducible authoring rules that change what the file contains for everyone | `.marrow` | `ProjectData::snap_settings` (MAR-165) | Two animators on one project must snap identically or the geometry diverges |
| Per-user habit that seeds the *next* action on *this* machine | `editor-settings.json` | `EditorPreferences::default_curve` (MAR-156) | It changes no existing data; two animators may reasonably want different defaults |

The default curve is squarely the second. It never alters an existing key, it
produces no diff, and putting it in `.marrow` would make opening a colleague's
project silently change your authoring default.

**MAR-171 boundary.** MAR-171 is the story that adds project-local curve
metadata (manual/auto intent plus a driver scalar, as optional additive
`.marrow`-only fields). MAR-170 must not preempt it, and does not: MAR-170 adds
**zero** `.marrow` fields, stores **zero** per-key or per-segment state, and
records no notion of "this curve came from a preset". Every byte MAR-170 can put
in a project is a value in the pre-existing `curve` field. MAR-171 therefore
starts from an unclaimed field namespace.

### 8.2 What already exists and is not modified

`editor-settings.json` v1 already has everything MAR-170 needs:

```json
{
  "version": 1,
  "default_curve": "ease_in_out",
  "recent_projects": []
}
```

- `CurvePreset` enum with all six enumerators — `preferences.hpp:15`
- `curve_preset_token()` / `parse_curve_preset()` — `preferences.cpp:131,149`
- Load with per-field fallback and diagnostics — `preferences.cpp:258`
- Write, preserving unknown additive fields and the on-disk root —
  `preferences.cpp:305`
- Same-directory temp file plus atomic replace (`rename` / `MoveFileExW`) —
  `preferences.cpp:356`
- Refusal to overwrite an unsupported future version — `preferences.cpp:655`
- Cross-platform path resolution with `MARROW_CONFIG_HOME` override —
  `preferences.cpp:526`

**MAR-170 modifies none of it.** `kEditorSettingsVersion` stays `1`. There is no
migration to write, because there is no schema change.

### 8.3 Fallback matrix

Criterion 4 — "Missing or invalid preference data falls back to Linear and never
rewrites existing curves" — is satisfied by MAR-156's existing behaviour. The
complete matrix, each row already implemented and already covered by
`marrow_preference_tests`, with the MAR-170 shell consequence added:

| On-disk condition | `PreferenceLoadStatus` | `default_curve` | Shell behaviour |
| --- | --- | --- | --- |
| File absent (first run) | `FirstRun` | `Linear` | Linear; **no file is created** |
| `default_curve` absent | `LoadedWithDefaults` | `Linear` | Linear; diagnostic surfaced once |
| `default_curve` not a string | `LoadedWithDefaults` | `Linear` | Linear |
| `default_curve` unknown token | `LoadedWithDefaults` | `Linear` | Linear |
| `version` missing / fractional / non-object root / malformed JSON | `Malformed` | `Linear` | Linear |
| `version` != 1 | `UnsupportedVersion` | `Linear` | Linear; a later save is **refused**, preserving the future file byte-for-byte |
| Unreadable path or I/O failure | `IoError` | `Linear` | Linear |
| Valid v1 with a known token | `Loaded` | that preset | that preset |

"Never rewrites existing curves" is structural: the load path has no access to
`EditorSession` or `ProjectData`. It writes one enum into `ShellState`. Nothing
downstream of a load touches a keyframe. The shell also **never saves on load**
— a `FirstRun` or `Malformed` result does not trigger a repair write, so a
malformed file the user is mid-way through hand-editing is left alone until they
explicitly change the default.

### 8.4 How a remembered default is validated before it can reach a keyframe

This is the chain criterion 4 and the format invariant jointly demand, stated
end to end:

```text
1. bytes on disk
2. runtime::json::parse_document()          -> malformed bytes never become a token
3. parse_curve_preset(std::string_view)     -> only six exact tokens produce a value;
                                               anything else yields std::nullopt
                                               and the Linear fallback
4. CurvePreset                              -> a closed enum; no other value exists
5. curve_preset_definition(preset)          -> indexes the constexpr table
6. kCurvePresets                            -> compile-time asserted cx in [0, 1]
7. curve_preset_interpolation(preset)       -> Interpolation::cubic_bezier(...)
8. set_keyframe_interpolation()             -> MAR-169's finiteness-then-range gate,
                                               on the double and the narrowed float
9. keyframe
```

The decisive property is at step 3→5: **the file supplies a token, never a
number.** No control point in `editor-settings.json` can ever reach a keyframe,
because none is stored there. A hostile or corrupt settings file can at worst
select a different one of six compile-time-verified constants, or fall back to
Linear. Step 8 remains in place as defence in depth and because the same code
path serves the Agent, which *does* accept arbitrary numbers.

### 8.5 Lifecycle

- **Load: exactly once**, from `load_shell_preferences(&state)` called from
  `shell_main.cpp` immediately after `ShellState` construction and before
  `reload_project()`, and from `run_headless_smoke()` after the test isolation
  in §16. Never re-read; the file is not watched.
- **Save: only** from `set_shell_default_curve()`, i.e. only when the user
  changes the `Default:` combo to a different value. Not on shutdown, not on
  project save, not on project load, not per frame. Selecting the value that is
  already stored performs no write.
- **Save failure** sets `state->error_message` from
  `PreferenceSaveResult::error`, leaves the in-memory default changed for this
  session, does not block editing, and does not touch the project.
- **Never dirties the project**: `set_shell_default_curve()` opens no
  transaction and does not reference `state->session`. A test asserts
  `serialize_project()`, `dirty()`, `undo_count()`, `redo_count()`, and
  `project_revision()` are identical across a default change plus save.

### 8.6 What the save must preserve

`PreferenceStore::save()` takes the whole `EditorPreferences`. The shell must
therefore hand back the value it loaded, with only `default_curve` changed, so
that `recent_projects` and `preserved_root` survive. Concretely:
`set_shell_default_curve()` mutates `state->preferences.default_curve` and
passes `state->preferences`; it must never construct a fresh
`EditorPreferences{}`. `PreferenceStore::save()` additionally re-reads the
on-disk root and overlays it, so unknown additive fields written by a newer
editor survive too. A test writes a settings file containing an unknown field
and a non-empty `recent_projects`, changes the default through the shell, and
asserts both survive verbatim.

## 9. Seeding Newly Authored Keys

### 9.1 The three sites

`add_timeline_key_at_playhead()` in `src/editor/timeline_controller.cpp` is the
shell's only key-creation path. Its three `Interpolation::linear()` literals —
in `sample_transform_keyframe()`, in `sample_deform_keyframe()`, and in the
Slot Color branch of the `visit_editable_timeline_keys()` lambda — become
`curve_preset_interpolation(state.preferences.default_curve)`.

Draw Order, Event, and Slot Attachment branches are untouched because those
structs have no `interpolation` member. The compiler enforces that; there is no
runtime branch to get wrong.

### 9.2 What is deliberately *not* seeded

| Path | Behaviour | Why |
| --- | --- | --- |
| Paste (`paste_timeline_clipboard`, four `copied.interpolation = keyframe.interpolation` sites) | copies the source curve | A paste reproduces a key; it does not author a new one. Overwriting the copied curve would silently destroy the thing being pasted |
| Agent `set_transform` / `set_deform_keyframe` / `set_slot_color_keyframe` | unchanged `interpolation_arg()` default of Linear | A headless agent's output must not depend on the invoking human's GUI preference file. Reproducibility beats convenience here, and MAR-169 already froze `interpolation_arg()` |
| MAR-169's Linear/Stepped → Cubic conversion seed `[1/3, 1/3, 2/3, 2/3]` | unchanged | That seed is chosen precisely because it evaluates identically to Linear, so grabbing a handle changes nothing until the pointer moves (MAR-169 §7.6). Seeding Ease there would make merely *touching* a handle jump the curve |
| Inserting a key inside an existing segment | the new key gets the default, the preceding key keeps its own curve | Criterion 3 says the default initializes newly authored segments. Shape-preserving insertion is a different feature and belongs with MAR-171's auto mode (§18.7) |
| `--create` minimal project generation | unchanged | Fixture generation must be byte-deterministic across machines and must not read a user settings file |

### 9.3 A `Stepped` default is legal

Stepped is one of the six presets and a legal default, even though a stepped
segment is not "continuous" in the mathematical sense. Criterion 3's "continuous
segments" names the **families that carry an easing field** — Transform, Deform,
Slot Color — as opposed to the discrete Draw Order / Event / Slot Attachment
lanes. A user whose default is Stepped gets stepped new keys, which is exactly
what choosing it means.

## 10. Applying a Preset

### 10.1 Selection resolution

```text
1. resolve the current animation and its tracks (build_timeline_tracks)
2. for each ref in state->timeline_editor.selected_keys, in selection order:
     a. find its track; skip if the track is missing or !timeline_track_is_editable
     b. resolve the key index (timeline_key_index); skip if identity is lost
     c. build the TimelineKeySelector (timeline_key_selector)
     d. skip if the selector's kind is DrawOrder, Event, or SlotAttachment
     e. skip if an equal selector is already in the list   <- de-duplication
     f. append
3. compatible_count = list size
   skipped_count    = selected_keys.size() - compatible_count
4. if compatible_count == 0: status message only; no transaction; no history
```

Three details that matter:

- **Order is the selection order**, which is stable and reproducible, so the
  same selection always yields the same selector vector. Combined with the
  primitive writing an identical absolute value into every key, this is what
  makes criterion 2's "deterministic" true: the result does not depend on
  iteration order, on which key is active, or on how the selection was built.
- **De-duplication is mandatory, not defensive.**
  `set_keyframe_interpolation()` rejects a duplicate selector atomically
  (MAR-169 §9.1). Two `TimelineKeyRef`s that resolve to the same parent key —
  reachable through box selection across a re-projection, or through a track
  appearing under two rows — would otherwise turn a legal user selection into a
  hard error. Equality is over the full `TimelineKeySelector` (kind, animation,
  bone, channel, slot, attachment, time, ordinal).
- **Materialization happens first.** Every selector's track is materialized
  through `ensure_transform_timeline_edit_index()` /
  `ensure_mesh_deform_timeline_edit_index()` /
  `ensure_slot_color_timeline_edit_index()` inside the transaction and before
  the primitive runs, exactly as MAR-169's agent path does, so a preset applies
  to a runtime-only imported track by copying it into the project rather than
  failing.

### 10.2 One transaction

```text
begin_edit({EditKind::EditProperty,
            "Apply <Display> curve" | "Apply <Display> curve to N keys",
            "timeline:curve-preset",
            /*allow_merge=*/false,
            Project | Runtime | Preview})
  materialize every track
  set_keyframe_interpolation(project, selectors, kind, control_points)
  on error            -> cancel(); error_message; return false
  on !result.changed  -> cancel(); "Curve already <Display>"; return false
  refresh_runtime()
  on refresh failure  -> cancel(); error_message; return false
commit()
```

One call to the primitive, one candidate copy, one move-assign, one history
entry — regardless of whether the selection is 1 key or 4096. Undo granularity
is therefore "one preset application", which is what a user pressing Ctrl+Z
after clicking `Ease-In` expects.

`allow_merge = false` and a distinct group (`"timeline:curve-preset"`, separate
from MAR-169's `"timeline:graph-easing"`) mean two consecutive preset clicks
produce two undo entries. That is correct: they are two deliberate discrete
decisions, and merging them would make Ctrl+Z jump back two curves.

The no-change path uses `cancel()` rather than `commit()`, matching
`completion_decision()`'s rule for gestures and the agent's `no_change` result,
so re-applying the preset a key already has adds nothing to the history.

### 10.3 Preview

Criterion 2 says "previewed". A preset click is discrete, so there is no
intermediate state to preview across frames; "previewed" means the transaction
refreshes the runtime and the preview *before* it commits, so the same
`EditImpact::Project | Runtime | Preview` path that a drag uses re-evaluates the
skeleton, and a failure there cancels the whole thing atomically instead of
leaving a committed project with a stale runtime. The viewport and the graph
show the new curve in the same frame as the click.

### 10.4 Rollback

Every failure path calls `EditTransaction::cancel()`, which restores the
project, the runtime `SkeletonData`, the preview controller state, and the
playback state captured at `begin_edit()`, then bumps the runtime/preview
revision; `sync_shell_from_editor_session(state)` refreshes the shell mirrors.
Because `set_keyframe_interpolation()` is itself preflight-then-mutate
(MAR-169 §10.2), a rejected write leaves the project untouched even before the
cancel runs — the two layers are independently sufficient.

### 10.5 Selection stability

A preset writes only `interpolation`. No key time changes, so every
`TimelineKeyRef` is bit-identical across apply, commit, cancel, undo, and redo;
`selected_keys` and `active_key` are never rewritten and
`reconcile_timeline_key_selection()` has nothing to prune. This is the same
argument MAR-169 §10.4 makes for the handle drag.

### 10.6 Guards

Applying is refused, and the buttons are disabled, when any of these holds:

| Condition | Reason |
| --- | --- |
| no project, no animation, or no resolvable tracks | nothing to write |
| `authoring_gesture_active(*state)` | a drag owns the session; MAR-170 never opens a second transaction |
| the compatible selection is empty | nothing to write; status message only |

MAR-170 adds nothing to `authoring_gesture_active()` and nothing to
`cancel_authoring_gestures()`, because it owns no long-lived transaction. The
two lists that "must stay in step" stay exactly as MAR-169 leaves them.

## 11. Interaction with MAR-169's Manual Handle Editing

The lead's two explicit questions, answered:

**Q. Preset then handle drag — one history entry or two?**
**Two.** The preset commits when it is clicked; the drag opens its own
transaction when the pointer leaves the dead zone. They have different labels,
different groups, and `allow_merge = false` on both, so no merge is possible at
any layer. Undo after the drag restores the preset curve; undo again restores
the pre-preset curve. This is the behaviour a user gets from every other pair of
discrete-then-continuous edits in the editor, and merging them would make one
Ctrl+Z discard a decision the user made minutes earlier.

**Q. What does the UI show as "current preset" once a handle has been dragged
away from it?**
`Custom`. The readout is `curve_preset_of(stored)` recomputed every frame
(§7.2). There is no remembered "last applied preset" to go stale, so the
transition needs no invalidation, survives undo/redo and project reload for
free, and reads `Ease` again if the user drags exactly back.

Two further interactions:

- **A preset applied to a Linear or Stepped key converts it**, in the same
  single transaction, with no separate conversion step — because
  `set_keyframe_interpolation()` writes an absolute kind plus control points.
  This is MAR-169's rule (§7.7) reused unchanged, not a new one.
- **A preset applied while the Graph tab displays component Y changes the X
  segment identically**, because the primitive has no component parameter and
  the field is shared. This is criterion 5's "preserve segment-wide easing" and
  it is inherited structurally from MAR-169 §8, not re-implemented. The shell
  smoke asserts it for the preset path anyway, because the criterion names it.

## 12. UI-Free API

### 12.1 `include/marrow/editor/authoring.hpp` (additive)

Appended after MAR-169's `set_keyframe_interpolation()`. The header gains
`#include <array>` (if MAR-169 has not already added it), `#include <optional>`,
and `#include "marrow/editor/preferences.hpp"`.

```cpp
struct CurvePresetDefinition {
    CurvePreset preset{CurvePreset::Linear};
    std::string_view token;
    std::string_view display_name;
    runtime::InterpolationKind kind{runtime::InterpolationKind::Linear};
    std::array<double, 4> control_points{};
};

/** @brief The six fixed presets in stable enum, UI, and identity-search order. */
inline constexpr std::array<CurvePresetDefinition, 6> kCurvePresets{{ /* §6.4 */ }};

/** @brief The definition of one preset; total over the closed enum. */
const CurvePresetDefinition& curve_preset_definition(CurvePreset preset);

/** @brief The runtime easing one preset denotes. */
runtime::Interpolation curve_preset_interpolation(CurvePreset preset);

/**
 * @brief Names the preset an easing exactly equals, or nullopt for a custom curve.
 *
 * Cubic control points are compared bit-exactly after narrowing the table's
 * doubles to `runtime::AnimationScalar`, so a preset written by this editor
 * always reads back as that preset, including after save, reload, and export.
 */
std::optional<CurvePreset> curve_preset_of(const runtime::Interpolation& interpolation);

/** @brief Parses one preset token; the same six tokens `editor-settings.json` uses. */
std::optional<CurvePreset> curve_preset_from_token(std::string_view token);
```

`curve_preset_from_token()` is a thin public re-export of the logic
`preferences.cpp` keeps file-local, so the Agent handler can accept the same
vocabulary without either duplicating the token strings or breaching the
preference module's isolation. It reads `kCurvePresets[i].token`, so the tokens
still have exactly one definition — the table — and `preferences.cpp`'s private
copy is asserted equal to it by a test (§19.1) rather than by a refactor of
untouched MAR-156 code.

### 12.2 `src/editor/timeline_controller.hpp` (shell, ImGui-free)

```cpp
/** @brief Result of one preset application, for status text and tests. */
struct TimelineCurvePresetResult {
    bool applied{false};
    std::size_t changed_key_count{0U};
    std::size_t compatible_key_count{0U};
    std::size_t skipped_key_count{0U};   // selected keys with no easing field
    std::string error;
};

/**
 * @brief Applies one fixed preset to every compatible selected key.
 *
 * Draw-order, event, and slot-attachment selections are skipped rather than
 * rejected, duplicates are collapsed, runtime-only tracks are materialized, and
 * the whole write is one transaction with live preview and one history entry.
 * A selection with no compatible key, or a selection already carrying the
 * preset, leaves the project and the history untouched.
 */
TimelineCurvePresetResult apply_timeline_curve_preset(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks,
    marrow::editor::CurvePreset preset);

/** @brief The preset the active key's outgoing easing is, or nullopt for custom. */
std::optional<marrow::editor::CurvePreset> active_outgoing_curve_preset(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks);
```

### 12.3 `src/editor/shell_preferences.hpp` / `.cpp` (new, shell-only, ImGui-free)

```cpp
/**
 * @brief Loads the user-local editor settings once, at shell startup.
 *
 * Never fails: every missing, malformed, unsupported, or unreadable case leaves
 * `state->preferences.default_curve` at `CurvePreset::Linear`. Nothing is
 * written, so a first run creates no file and a malformed file is preserved.
 * This function does not reference EditorSession, ProjectData, or the runtime.
 */
void load_shell_preferences(ShellState* state);

/**
 * @brief Records a new default curve and atomically persists the settings file.
 *
 * Preserves `recent_projects` and every unknown additive field by saving the
 * loaded `EditorPreferences`. Never opens a transaction and never dirties the
 * project. Returns false and sets `state->error_message` when the write fails,
 * leaving the in-memory default changed for this session.
 */
bool set_shell_default_curve(ShellState* state, marrow::editor::CurvePreset preset);
```

### 12.4 `src/editor/shell_state.hpp` (additive)

```cpp
    // MAR-170: user-local editor settings, loaded once at startup. Never
    // participates in project history, dirty state, or revisions.
    marrow::editor::EditorPreferences preferences{};
    marrow::editor::PreferenceLoadStatus preference_status{
        marrow::editor::PreferenceLoadStatus::FirstRun};
    std::filesystem::path preference_path;
    std::string preference_diagnostic;
```

`ShellState` gains no gesture, no optional, and no constructor work. It does not
hold a `PreferenceStore`: the store resolves a filesystem path in its default
constructor, and keeping that out of `ShellState`'s constructor is what lets
`run_headless_smoke()` install its environment isolation before any I/O happens
(§16).

### 12.5 What stays shell-private ImGui code

Both toolbars keep only: drawing the six buttons and the `Default:` combo,
gating them on `authoring_gesture_active()` and a non-empty compatible
selection, drawing the tooltips, drawing the `Outgoing:` readout, and publishing
render stats. No preset arithmetic, no preference I/O, and no project mutation
lives in an ImGui translation unit.

### 12.6 Render statistics additions

`TimelineGraphRenderStats` gains:

```cpp
bool curve_preset_row_drawn{false};
bool curve_preset_row_enabled{false};
float first_preset_min_x{0.0f};
float first_preset_min_y{0.0f};
float first_preset_max_x{0.0f};
float first_preset_max_y{0.0f};
// nullopt is reported as kCurvePresets.size() ("Custom").
std::size_t active_preset_index{0U};
std::size_t default_preset_index{0U};
```

`first_preset_*` lets the actual-frame smoke aim a real mouse at a real button
rather than at a hardcoded coordinate, exactly as MAR-167/168 do for `Fit` and
the component checkboxes.

## 13. Presentation

- **Preset row**, in the Graph toolbar after the existing `Outgoing:` label and
  in the Dopesheet toolbar after `Snap to Frames`, so no existing widget moves:
  six `ImGui::SmallButton`s labelled `Linear`, `Stepped`, `Ease`, `In`, `Out`,
  `In-Out`, preceded by a `Curve:` `TextDisabled` label. Each button's tooltip
  gives the full display name and the exact quadruple, e.g.
  `Ease-In-Out  [0.42, 0, 0.58, 1]`.
- The row is wrapped in `ImGui::BeginDisabled()`/`EndDisabled()` when the
  compatible selection is empty or any authoring gesture is live. It is not
  hidden — a disabled row with a tooltip explaining "Select one or more
  Transform, Deform, or Slot Color keys" teaches the constraint; a vanished row
  does not.
- **Default control**, immediately after the preset row:
  `Default:` plus a `##curve_default` combo of the six display names, width
  `ImGui::CalcTextSize("Ease-In-Out").x + frame padding`. Changing it calls
  `set_shell_default_curve()`. Its tooltip: "Seeds newly added Transform,
  Deform, and Slot Color keys. Stored per user in editor-settings.json; it
  never changes existing keys and never modifies the project."
- **Readout**: the Graph toolbar's existing `Outgoing: %s` label reports the
  preset name instead of the bare kind — `Linear`, `Stepped`, `Ease`,
  `Ease-In`, `Ease-Out`, `Ease-In-Out`, or `Custom Bezier` — and keeps
  `No outgoing segment` for a last key or an unset active key.
  `outgoing_kind_label()` is rewritten to call `curve_preset_of()`; its
  signature and call site are unchanged.
- **Status messages**, in the existing voice:
  `"Applied Ease-In-Out to 7 keys"`,
  `"Applied Ease-In-Out to 7 of 9 selected keys; 2 have no easing"`,
  `"Selected keys already use Ease-In-Out"`,
  `"Select one or more Transform, Deform, or Slot Color keys"`,
  `"Failed to apply curve preset: <error>"`,
  `"Default curve set to Ease-In-Out"`,
  `"Failed to store the default curve: <error>"`.
- **Load diagnostic**: when `load_shell_preferences()` returns a status other
  than `Loaded` or `FirstRun`, the shell shows one status line —
  `"Editor settings could not be read; using the Linear default curve"` — once,
  at startup, and stores the detail in `preference_diagnostic`. It never
  auto-repairs the file.
- The MAR-167/169 shared-easing notice keeps its wording and gains one sentence:
  a preset applies to the whole selection and, like a handle drag, writes the
  single shared easing of each key.

## 14. Agent and MCP Surface

### 14.1 No new operation

Criterion 5 requires that "GUI, C++ agent, and Python MCP preset application
**share the interpolation operation**." MAR-169's `timeline.set_interpolation`
already accepts `"linear"`, `"stepped"`, or a 4-number array — and `"linear"`
and `"stepped"` are literally two of the six presets. MAR-170 completes that
vocabulary rather than adding a parallel operation.

**Registry total: 57 operations. Unchanged.**

### 14.2 The additive argument change

`interpolation_request_arg()` (MAR-169's helper in `agent_dispatch.cpp`,
declared in `agent_dispatch_internal.hpp`) accepts four more string tokens:

| `interpolation` value | Before MAR-170 | After MAR-170 |
| --- | --- | --- |
| `"linear"` | Linear | Linear (unchanged) |
| `"stepped"` | Stepped | Stepped (unchanged) |
| `[cx1, cy1, cx2, cy2]` | Cubic with those values | unchanged |
| `"ease"` | error | Cubic `[0.25, 0.1, 0.25, 1]` |
| `"ease_in"` | error | Cubic `[0.42, 0, 1, 1]` |
| `"ease_out"` | error | Cubic `[0, 0, 0.58, 1]` |
| `"ease_in_out"` | error | Cubic `[0.42, 0, 0.58, 1]` |
| missing | error | error (unchanged) |
| anything else | error | error (unchanged) |

Implementation: the string branch calls `curve_preset_from_token()` and, on a
match, reads `kCurvePresets`. Every previously valid call keeps its exact
behaviour, and the error message becomes
`"<name> is required and must be linear, stepped, ease, ease_in, ease_out, ease_in_out, or a 4-number bezier array."`

The legacy `interpolation_arg()` used by `set_transform`,
`set_deform_keyframe`, and `set_slot_color_keyframe` is left **byte-identical**,
so those three operations' contracts do not change and their key-creation
default stays Linear (§9.2).

Token spelling is `snake_case` only, matching `editor-settings.json` and the
rest of the agent surface. `"ease-in"` with a hyphen is **not** accepted: one
spelling per concept, because an API vocabulary with silent aliases is harder to
validate than a slightly stricter parser.

### 14.3 Response payload

Unchanged from MAR-169 §13.4. In particular `interpolation` and
`previous_interpolation` continue to echo the `.marrow`/`.mskl` `curve`
encoding — the 4-number array for a cubic — **not** the preset token. Two
reasons: the echo's purpose is to let a caller compare against the stored bytes,
and a token would be lossy for every custom curve. Callers wanting the preset
name compute it from the four numbers, exactly as the GUI does.

### 14.4 MCP tool

`tools/mcp/tools/editing.py`'s `_bezier_interpolation_schema()` — the helper
MAR-169 adds for `timeline.set_interpolation` only — grows its string enum:

```python
{"type": "string",
 "enum": ["linear", "stepped", "ease", "ease_in", "ease_out", "ease_in_out"]}
```

with the description naming each preset's quadruple so the model can reason
about which to pick. The array branch is unchanged, `_interpolation_schema()`
(used by the other tools) is untouched, no tool is added or removed, and
`get_tools()` returns the same count. The schema stays advisory — the MCP server
forwards calls verbatim and the C++ primitive remains the sole authority.

### 14.5 Every site that asserts the operation count

MAR-170 changes **none** of them. They are listed so the implementer verifies
each is still 57 after the change rather than assuming:

| Location | Value | Kind |
| --- | --- | --- |
| `src/editor/agent_dispatch.cpp` `kOperationSpecs[]` | 57 rows | code — the count is derived from `std::size(kOperationSpecs)` and self-updates; adding no row is what keeps it 57 |
| `src/samples/agent_dispatch_smoke.cpp` `std::array<OperationExpectation, 57>` | 57 | hand-edited test — unchanged, but gains preset behaviour cases |
| `src/editor/shell_smoke_graph.cpp` (two `operation_count_before != 57U` sites and their messages) | 57 | hand-edited test — unchanged; the new MAR-170 scenario asserts 57 too |
| `tools/mcp/test_client.py` `len(registry_names) == 57`, `len(mcp_names) == 57` | 57 | hand-edited test — unchanged |
| `AGENTS.md` `Current Validation` registry line | 57 | current prose — **unchanged** |
| `docs/root1/editing-gap-analysis.md` lines 23, 83, 190 | 57 / 편집 32 | current prose — **unchanged** |
| `docs/root1/refector.md` lines 20, 112 | 57 | current prose — **unchanged** |
| every `Validation Results` section, dated checkpoint, and past PRD criterion | 49 / 55 / 56 | historical record — **unchanged** |

The prose rule from MAR-169 §14 still applies and still resolves to "change
nothing": a sentence stating what the surface *is* reads 57 and already does; a
sentence carrying a date, a milestone ID, or the words "historical", "baseline",
or "checkpoint … passed" stays at whatever it recorded. Since MAR-170 adds no
operation, the only new prose is MAR-170's own `Validation Results` section,
which states that the surface stayed at 57 and that the growth was in one
operation's argument vocabulary.

Verification sweep for the implementer:

```bash
rg -n '\b57\b' --glob '!build*' src tools AGENTS.md docs .agents
rg -n '\b58\b' --glob '!build*' src tools AGENTS.md docs .agents   # must find no registry claim
```

## 15. Error Handling and Fail-Closed Rules

| Condition | Result |
| --- | --- |
| No project, animation, or resolvable tracks | buttons disabled; no transaction |
| `authoring_gesture_active(*state)` | buttons and the default combo disabled |
| Compatible selection is empty | status message; no transaction; no history |
| A selected key's track is missing or not editable | that key is skipped and counted in `skipped_key_count` |
| A selected key's identity no longer resolves | skipped |
| A selected key is Draw Order / Event / Slot Attachment | skipped; reported in the status message |
| Two selected refs resolve to the same key | collapsed to one selector |
| Materialization failure | `cancel()` with rollback; error message |
| `set_keyframe_interpolation()` error | `cancel()` with rollback; error message |
| `refresh_runtime()` failure | `cancel()` with rollback; error message |
| Nothing changed | `cancel()`; no history entry; "already uses" message |
| Preference load: absent / malformed / unsupported / I/O error | Linear; no write; one diagnostic |
| Preference save failure | error message; in-memory default kept for the session; project untouched |
| Preference file has an unsupported future `version` | `PreferenceStore::save()` refuses; the file is preserved byte-for-byte; error message |
| Agent `interpolation` is an unknown token | `invalid_request`, listing the six accepted tokens |

Nothing here can leave a partial write: every project-side failure is either
before `set_keyframe_interpolation()`'s single move-assign or is a
`cancel()` of the enclosing transaction.

## 16. Test Isolation for the Preference File

This is the highest-risk operational detail in the story, and it needs an
explicit rule.

`PreferenceStore`'s default constructor resolves the **real** user path:
`~/Library/Application Support/Marrow/editor-settings.json` on macOS,
`$XDG_CONFIG_HOME/marrow/…` or `~/.config/marrow/…` on Linux,
`%APPDATA%\Marrow\…` on Windows. Once the shell loads preferences at startup,
every invocation of `marrow_editor_shell` — including
`./build/marrow_editor_shell --project … --auto-close 2`, which is a CI gate —
reads that file, and any scenario exercising the save path would **write** the
developer's real settings.

**Rule: every test process that constructs a `ShellState` and loads preferences
must first point `MARROW_CONFIG_HOME` at a fresh temporary directory, and must
restore the previous value afterwards.**

`resolve_preference_settings_path()` already honours `MARROW_CONFIG_HOME` first
on **every** platform (`preferences.cpp:540`), and
`marrow_preference_tests`' "process environment priority and restoration" case
already proves it. No new injection seam is added to `PreferenceStore`; the
existing, tested override is the mechanism, which is exactly what criterion 6's
"MARROW_CONFIG_HOME restoration" asks for.

Concretely:

- `run_headless_smoke()` in `src/editor/shell_smoke.cpp` sets
  `MARROW_CONFIG_HOME` to a unique temp directory as its **first** statement,
  before `ShellState shell_state;`, and removes the directory and restores the
  variable before returning on every path.
- Each isolated shell-smoke scenario that constructs its own session does the
  same for its own directory, so scenarios cannot see each other's settings.
- The load must be an explicit `load_shell_preferences(&state)` call rather than
  work in `ShellState`'s constructor, so the isolation can be installed first.
  This is why §12.4 keeps the store out of `ShellState`.

A test asserts the negative directly: with `MARROW_CONFIG_HOME` pointed at an
empty temp directory, a full smoke run leaves that directory **empty** — proving
the shell creates no settings file unless the user changes the default.

## 17. Non-Goals

- Everything in §3.3, restated for emphasis: **no `.marrow` field**, no
  project-local curve intent, no auto/manual mode, no driver scalar, no
  Fritsch-Carlson tangents. All of that is MAR-171 and MAR-170 leaves its
  namespace completely unclaimed.
- No loop-boundary synchronization (MAR-172), time scaling (MAR-173), or preview
  speed (MAR-174).
- No user-editable, named, or per-project preset library. The six are constants.
- No new Agent operation and no agent access to the preference file.
- No preference schema change, no `kEditorSettingsVersion` bump, and no
  modification to `preferences.cpp` or `preferences.hpp`.
- No Recent Projects UI. The field is round-tripped, not consumed.
- No change to MAR-168's point drag or MAR-169's handle drag, including
  MAR-169's `[1/3, 1/3, 2/3, 2/3]` conversion seed.
- No refactor of the per-key `Interpolation` combo or the `Bezier X1/Y1/X2/Y2`
  fields in `shell_timeline.cpp` onto the shared primitive; MAR-169 deferred it
  and MAR-170 keeps that deferral.
- No manual-visible-UI, Windows 11, or physical-input qualification credit.

## 18. Decisions Taken Under Ambiguity

1. **The remembered default lives in preferences, not the project.** Criterion 3
   names `editor-settings.json` explicitly, and the codebase already
   demonstrates the principled split: `ProjectSnapSettings` is project-local
   because two animators must snap identically, while a default curve changes no
   existing data and may reasonably differ per person. Putting it in `.marrow`
   would also collide with MAR-171's project-local metadata, which is the whole
   point of keeping MAR-170 out of the file format.
2. **No new Agent operation; the existing one's string vocabulary grows.**
   Criterion 5's wording is "share the interpolation operation", and `"linear"`
   and `"stepped"` are already two of the six preset tokens, so extending the
   enum is the minimal reading. The alternative — a `timeline.apply_curve_preset`
   operation — would take the registry to 58, duplicate every validation path,
   and make "share" false. Rejected.
3. **Applying a preset targets the whole compatible selection, not the active
   key.** Criterion 2 says "compatible selected keys". MAR-169 §17.1 already
   deferred multi-key assignment to this story. The active key alone would leave
   criterion 2's "keys" plural unsatisfied.
4. **Applying a preset does not change the remembered default.** The tempting
   reading of "the last chosen default" is "whatever you last applied". Rejected:
   applying Stepped once to fix one key would then silently change what every
   future key in every future project gets, with no visible cause. The default
   is changed only through the control that says `Default:`. The cost is one
   extra widget; the benefit is that a persistent cross-project setting is never
   changed as a side effect of a local edit.
5. **The GUI skips incompatible selected keys; the Agent rejects them.** A
   dopesheet box selection routinely spans an Event lane, so rejecting the whole
   command would make the feature unusable in the surface where selections are
   built loosely. A scripted caller passes explicit selectors, where a silent
   partial application would hide a caller bug. This is the same asymmetry
   MAR-169 §17.1 and §17.12 already established between the graph and the agent.
6. **Preset identity is bit-exact `float32`, with no epsilon and no stored
   flag.** An epsilon would label a hand-dragged curve "Ease" when a save/reload
   shows different numbers. A stored "this came from a preset" flag would be a
   `.marrow` field — forbidden by §3.4 — and would go stale under undo, redo,
   external edits, and MAR-169's drags. A pure function of the four stored
   floats is the only option with none of those failure modes.
7. **Inserting a key gives it the default curve rather than inheriting the
   surrounding segment's curve.** Inheriting would preserve the visual shape
   across an insertion, which is arguably better animation behaviour, but
   criterion 3 says the default initializes newly authored segments, and
   shape-preserving insertion is properly a function of MAR-171's auto-curve
   mode. Recorded as a rejected alternative rather than silently chosen.
8. **Agent-created keys keep the Linear default; they do not read the
   preference.** A headless agent's output must be reproducible independent of
   which human's machine it runs on. This also keeps `interpolation_arg()` and
   the three operations using it byte-identical, which MAR-169 already required.
9. **No agent or MCP access to the preference file.** Every operation in the
   registry acts on `ProjectData`; a mutation reaching outside the project into
   the user's persistent GUI settings would be a new and surprising class of
   side effect, and the story does not ask for it.
10. **Preset constants live in `authoring.hpp`, not `preferences.hpp`.**
    Naming `runtime::InterpolationKind` in `preferences.hpp` would pull the
    runtime animation header into the module MAR-156 deliberately isolated.
    `authoring.hpp` already has both dependencies, so the arrow points
    authoring → preferences and the isolation survives.
11. **A `Stepped` default is legal.** "Continuous segments" in criterion 3 names
    the key families that carry an easing field, not a restriction to non-stepped
    curves. Refusing Stepped as a default would make one of the six presets
    second-class for no stated reason.
12. **MAR-170 adds no gesture and no entry to `authoring_gesture_active()`.** A
    preset click is discrete and its transaction opens and commits within one
    frame. Adding a gesture struct would create a state that
    `cancel_authoring_gestures()` must release without any window in which it
    could be live.
13. **`MARROW_CONFIG_HOME` is the test isolation mechanism; no new seam is
    added.** The override already exists on every platform and is already
    tested. Adding a `PreferenceStore` injection point for the shell would be a
    second mechanism to keep correct.
14. **Preset tokens are `snake_case` only, with no hyphenated aliases.** One
    spelling per concept keeps the parser, the schema enum, the settings file,
    and the documentation trivially consistent.
15. **The preset row goes after the existing toolbar widgets in both tabs.**
    Inserting it earlier would move `Fit`, the component checkboxes, and the
    Copy/Cut/Paste cluster, whose on-screen rectangles the actual-frame smokes
    aim a real mouse at. Appending changes no existing widget's position.

## 19. Validation Strategy

### 19.1 UI-free focused tests — `marrow_preference_tests`

The natural home for constant and token coverage, since it already links
`marrow_editor` and already owns the settings-file cases. New cases:

- **Preset constants**: every entry of `kCurvePresets` has the exact quadruple
  from §6.1; the table has six entries, one per `CurvePreset` enumerator, in
  enum order; `kCurvePresets[i].preset` equals the `i`-th enumerator.
- **Token agreement**: for every enumerator,
  `curve_preset_from_token(kCurvePresets[i].token) == kCurvePresets[i].preset`,
  and a settings file written with that token loads back to the same enumerator
  — proving `authoring.hpp`'s table and `preferences.cpp`'s private token list
  agree without either being refactored.
- **Format invariant**: every cubic entry keeps `cx1` and `cx2` inside `[0, 1]`
  both as `double` and after narrowing to `runtime::AnimationScalar`.
- **Runtime well-posedness**: for each cubic preset,
  `Interpolation::cubic_bezier(...).transform(alpha)` over `alpha` in
  `{0, 0.01, …, 1}` is finite, non-decreasing, inside `[0, 1]` (no overshoot),
  and exactly `0.0` at `alpha = 0` and `1.0` at `alpha = 1` — including Ease-In,
  whose `X'(1) = 0` (§6.2).
- **Identity round trip**: `curve_preset_of(curve_preset_interpolation(p)) == p`
  for all six; `curve_preset_of()` returns `std::nullopt` for
  `[0.2, -0.4, 0.8, 1.6]`, for `[1/3, 1/3, 2/3, 2/3]` (MAR-169's conversion
  seed, which must **not** be mistaken for a preset), and for a curve one ULP
  away from Ease.
- **Fallback matrix**: the §8.3 table, re-asserted for each row, with the
  existing helper `expect_default_preferences()`.
- **Preservation**: a v1 file with `recent_projects` and an unknown additive
  field, saved with only `default_curve` changed, retains both.
- **`MARROW_CONFIG_HOME` restoration**: extend the existing environment case to
  cover a settings file written and read back through the override, then assert
  the variable is restored.

### 19.2 UI-free focused tests — `marrow_project_smoke`

A new `validate_mar170_curve_presets(const ProjectLoadResult&)` beside
MAR-169's `validate_mar169_graph_interpolation_authoring()`:

- each of the six presets written through `set_keyframe_interpolation()` onto a
  Transform, a Deform, and a Slot Color key, asserting the stored kind and
  control points and that `time`, `angle`, `x`, `y`, `color`, and
  `vertex_offsets` are byte-identical;
- **segment-wide identity**: after applying a preset to a Translate key, both
  `x` and `y` read through the one changed `interpolation`, and exactly one
  `interpolation` field in the whole project differs from the snapshot;
- **multi-key determinism**: the same preset applied to a 12-key selector list
  in two different orders produces byte-identical `serialize_project()` output;
- **no-change**: applying the same preset twice returns `changed == false` with
  a byte-identical project;
- **save/reload preserves preset identity**: save, reload, and assert
  `curve_preset_of()` still names the same preset for every key — the direct
  proof that §7.2's exactness survives JSON round-tripping;
- **export**: `--export-runtime` / `--export-binary` plus
  `marrow_inspect --compare` for JSON/MBIN equivalence, and
  `curve_preset_of()` on the reloaded `.mskl` still naming the preset;
- **loader acceptance**: a project written with all six presets reloads with no
  validation error, proving no preset can violate
  `project.cpp:1639` / `skeleton_parse.cpp:1727`.

### 19.3 Headless shell smoke — `src/editor/shell_smoke_graph.cpp`

A new `validate_timeline_curve_preset_shell_smoke()` with an isolated session
and its own `MARROW_CONFIG_HOME` directory, registered in
`shell_smoke_scenarios.hpp` and `shell_smoke.cpp`:

- select three Transform keys and apply `Ease-In-Out`: exactly one history
  entry, `changed_key_count == 3`, every key's `time` and scalars byte-identical,
  every stored curve equal to the constant;
- a selection mixing Transform, Slot Color, and Event keys: the Event key is
  skipped, `skipped_key_count == 1`, the others are written, still one history
  entry;
- a selection of only Event keys: no transaction, `undo_count()` unchanged,
  status message set;
- re-applying the same preset: `applied == false`, no history entry, project
  bytes identical;
- **criterion 5 direct**: with the Graph tab displaying component Y, apply a
  preset and assert the rebuilt X segment's `SegmentKind` and `outgoing_easing`
  changed identically;
- undo then redo restoring the curve with `selected_keys` and `active_key`
  bit-identical;
- **preset then handle drag is two entries**: apply `Ease`, then run a MAR-169
  handle drag, and assert `undo_count()` grew by exactly 2, that one undo
  restores `Ease` exactly, and that `curve_preset_of()` reads `Ease` again;
- **`Custom` readout**: after the drag, `active_outgoing_curve_preset()` is
  `std::nullopt`;
- **default seeding**: set the default to `Ease-Out`, add a key at the playhead
  on a Transform track, a Deform track, and a Slot Color track, and assert each
  new key's easing is exactly `[0, 0, 0.58, 1]`; set the default to `Stepped`
  and assert the next new key is Stepped;
- **paste is not reseeded**: copy a key carrying `Ease-In`, change the default
  to `Stepped`, paste, and assert the pasted key still carries `Ease-In`;
- **no project dirtying**: change the default and save the preference, then
  assert `serialize_project()`, `dirty()`, `undo_count()`, `redo_count()`, and
  `project_revision()` are unchanged;
- **preference isolation**: the scenario's `MARROW_CONFIG_HOME` directory
  contains `editor-settings.json` only after an explicit default change, and its
  parsed `default_curve` token matches;
- **fallback**: write a malformed settings file into the scenario directory,
  reload preferences, and assert the default is `Linear` and that no keyframe in
  the project changed;
- `agent_operation_descriptor_count() == 57` before and after every case.

### 19.4 Actual-frame smoke — `src/editor/shell_smoke_frames.cpp`

Using real ImGui mouse events against real rendered coordinates:

- render a frame and assert `curve_preset_row_drawn`, finite
  `first_preset_min_x/y`, and that the plot rectangle (`plot_min_y`,
  `plot_max_y`) still lies inside the timeline window after the toolbar grew;
- assert MAR-167/168's `fit_*` and `first_component_*` rectangles are still
  reported and still hoverable — the appended row must not displace them;
- with keys selected, click the reported first preset button and assert one
  history entry and the expected stored curve;
- with nothing selected, assert `curve_preset_row_enabled` is false and a click
  changes nothing;
- assert the preset row and the `Default:` combo are inert while a MAR-169
  handle drag is live.

### 19.5 Agent smoke — `marrow_agent_dispatch_smoke`

- the registry is **still exactly 57** operations and
  `timeline.set_interpolation`'s metadata row is unchanged;
- each of the four new tokens applies the exact quadruple, verified through a
  follow-up dry run's `previous_interpolation`;
- `"linear"` and `"stepped"` behave exactly as before;
- the 4-number array form behaves exactly as before, including the
  `[0.2, -0.4, 0.8, 1.6]` overshoot case MAR-169 added;
- an unknown token (`"ease-in"`, `"easeIn"`, `"bounce"`) is rejected with
  `invalid_request` and an unchanged project;
- a multi-key preset call adds exactly one history entry and `undo` restores
  every key;
- `set_transform` with no `interpolation` still creates a Linear key, proving
  `interpolation_arg()` and the agent's key-creation default are untouched.

### 19.6 MCP — `tools/mcp/test_client.py`

- `len(registry_names) == 57` and `len(mcp_names) == 57` — unchanged;
- `set(registry_names) == set(mcp_names)`, all names unique;
- a dry-run → live → read-back → `undo` sequence using `"ease_in_out"`, with the
  read-back's `previous_interpolation` asserted equal to
  `[0.42, 0.0, 0.58, 1.0]` at four decimal places;
- rejection of `"ease-in"` and of an out-of-range array, proving the schema
  change did not loosen the C++ gate.

### 19.7 Display and compatibility gates

See the "Full verification checklist" in
`docs/superpowers/plans/2026-08-30-mar-170-curve-presets-and-defaults.md`.
Automated display tests prove the exercised ImGui/display path only. They add no
manual-visible-UI, Windows 11, or physical-input qualification credit.

## 20. Documentation and Milestone Closure

After code and every required gate pass:

- add a `MAR-170 Fixed Curve Presets and Remembered Defaults Validation Results`
  section to `AGENTS.md` in the existing table plus command-output format;
- **leave `Current Validation`'s registry line at 57** and add one line for the
  preference gate if it is not already listed (it is:
  `./build/marrow_preference_tests`);
- update `AGENTS.md`'s `Project State` line so MAR-170 is complete and MAR-171
  is next;
- update `docs/root1/discription.md`, `quick-start.md`, `concepts.md`,
  `editing-gap-analysis.md`, `refector.md`, and `platform-validation.md` where
  the "MAR-170 is next" wording or the easing-authoring contract is now stale.
  **Do not change any operation count**;
- `docs/root1/format-spec.md` gains no new field and no version change. If
  MAR-169 added its `[0, 1]` sentence, MAR-170 appends one clause noting the six
  fixed presets all satisfy it. Otherwise no change;
- `docs/root1/discription.md` line 36's MAR-156 paragraph gains one sentence:
  MAR-170 is the consumer of `default_curve`, and the preference still never
  touches project dirty/history/revision, the runtime formats, the C ABI, or the
  Agent/MCP surface;
- mark `MAR-170` done with the verified completion date and leave `MAR-171`
  open;
- leave MAR-192 through MAR-210 open and add no platform qualification credit;
- change this document's status to `Implemented and validated` only after every
  gate is green.

## 21. Commercial-Tool Reference Boundary

The preset vocabulary follows the CSS Easing Level 1 timing functions
(<https://www.w3.org/TR/css-easing-1/>), which Spine's curve presets and Live2D
Cubism's easing menu also broadly track:

- Spine Graph: <https://us.esotericsoftware.com/spine-graph>
- Live2D Graph Editor: <https://docs.live2d.com/en/cubism-editor-manual/grapheditor/>

Marrow deliberately does **not** copy user-editable preset libraries,
per-project preset sets, or per-component preset assignment. The runtime's
one-outgoing-easing-per-key model, MAR-169's segment-wide curve identity, and
MAR-156's project-independent preference boundary take precedence over surface
similarity to another editor.
