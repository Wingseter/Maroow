# MAR-184 Add Stepped Inherit Timeline Overlays — Design

- Story: `MAR-184`, `.agents/tasks/prd-marrow-runtime.json`
- Depends on: `MAR-183` (sequencing only — see §0.2)
- Followed by: `MAR-185` "Complete inherit timeline editing parity"
- Baseline commit: `4a4249d`
- Status of this document: design. **Nothing here was built or benchmarked by the
  author.** Every claim carries a `file:line` or is listed in §9 as a Task 0 gate.

---

## 0. Where this story sits

### 0.1 The title is misleading and the description is authoritative

The title reads "Add stepped inherit timeline **overlays**". Read alone it
suggests rendering — a stepped line drawn over a lane, the visual sibling of
MAR-167's scalar graph and MAR-168's key handles.

**It is not that.** The story's own description is unambiguous:

> Add an optional project **overlay** and UI-free **merge path** for authoring
> runtime-compatible bone inherit timelines.

"Overlay" here is the word this codebase already uses for
`ProjectData::*_timeline_edits` — additive `.marrow` data layered over an
imported `.mskl` at materialization time (`build_runtime_document`,
`src/editor/project.cpp:5335`). Every acceptance criterion confirms it:

| AC | Layer it names |
| --- | --- |
| 1 | "an optional additive `.marrow` overlay" — project file schema |
| 2 | "Absent overlay data preserves old-project behavior" — project loader |
| 3 | "Materialization ... without mutating shared `SkeletonData`" — `build_runtime_document` |
| 4 | "The **UI-free** merge primitive" — `authoring.cpp`-class primitive |
| 5 | "Runtime export emits ... in `.mskl` and `.mbin`" — export path |
| 6 | "**Project and runtime** tests" — `marrow_project_smoke`, `marrow_fixture_smoke` |

Not one criterion names the timeline panel, the graph, the dopesheet, the
playhead, selection, or key activation. AC4 says *UI-free* in as many words, and
AC6 names project and runtime test surfaces and no shell surface.

**MAR-184 draws nothing.** There is no ImGui code in this story, no frame smoke,
and no widget. §2.9 states what the user nonetheless sees, and why that is a
consequence of existing code rather than new code.

This is the fifth story in the MAR-180…184 chain whose brief-level premise did
not survive measurement (MAR-177 assumed a missing create that shipped; MAR-182
said "unify" where there were zero checks; MAR-183's storage layer already
existed complete). Recording it here so the next planner reads the description,
not the title.

### 0.2 The MAR-183 dependency is sequencing, not coupling

`dependsOn: ["MAR-183"]` is queue order. MAR-183 delivered a bounded
recent-project list in the versioned preference store
(`src/editor/recent_projects.cpp`, `src/editor/shell_recent_projects.cpp`).
MAR-184 touches:

- no preference file,
- no `MARROW_CONFIG_HOME`,
- no File menu, no dirty-intent machine, no path modal,
- no shell state at all.

The two stories share no symbol. **Do not pull recent-projects or File-workflow
machinery into this plan.** The only inherited obligation is the standing one:
do not regress C4–C25 in `marrow_editor_shell`, which the full verification run
covers as a regression witness.

### 0.3 The MAR-185 boundary, stated once and designed against

| Concern | MAR-184 | MAR-185 |
| --- | --- | --- |
| `.marrow` schema for inherit keys | **owns** | consumes |
| Parse / serialize / round-trip | **owns** | consumes |
| Materialization into the effective skeleton | **owns** | consumes |
| Runtime export (`.mskl`, `.mbin`) | **owns** | regression only |
| One UI-free merge primitive, validate-before-mutate | **owns** | builds on |
| `find_*` / `ensure_*` accessors | **owns** | consumes |
| Add / Edit / exact-playhead Remove in the editor | — | owns |
| `TimelineKeyKind::Inherit`, shared selection, retime, scale, clipboard | — | owns |
| Duration auto-grow, transaction primitives, rejection atomicity | — | owns |
| Animation rename/delete cascade over inherit overlays | — | owns |
| `set_inherit_keyframe`, `remove_inherit_keyframe` (C++ registry + Python MCP) | — | owns |
| Undo/redo, preview, selection reconciliation for inherit edits | — | owns |

The rule that keeps MAR-185 from reworking MAR-184: **the merge primitive is the
only mutation path**, it validates completely before it writes, and it leaves the
project bytewise unchanged on any rejection (§2.6). MAR-185's transaction and
auto-grow layers wrap that primitive; they do not replace it. MAR-185's
`remove_inherit_keyframe` is a *deletion*, which this story deliberately does not
provide (§8).

MAR-184 does **not** need a mutation path for its own sake — nothing in ACs 1–3
or 5 requires writing to a project. AC4 requires it explicitly ("The UI-free
merge primitive produces … before mutation"), so the primitive is in scope
because the story asks for it by name, not because display needed it.

---

## 1. The measured gap

Everything in this section was read at `4a4249d`.

### 1.1 The runtime layer is complete. Do not rebuild it.

| Concern | Where | State |
| --- | --- | --- |
| Mode enum, five values | `include/marrow/runtime/animation.hpp:125-132` | complete |
| `InheritKeyframe { AnimationScalar time; BoneInherit inherit; }` | `include/marrow/runtime/animation.hpp:168-177` | complete |
| `BoneInheritTimeline { bone_index; keyframes }` | `include/marrow/runtime/animation.hpp:185-188` | complete |
| Storage on `AnimationData` | `include/marrow/runtime/skeleton.hpp:332` | complete |
| `find_inherit_timeline`, `sample_bone_inherit` | `include/marrow/runtime/skeleton.hpp:374,438` | complete |
| `.mskl` parse of `animations.<a>.bones.<b>.inherit` | `src/runtime/skeleton_parse.cpp:5293-5306` | complete |
| Keyframe parser + validation | `src/runtime/skeleton_parse.cpp:1845-1918` | complete |
| Mode token map (both directions in) | `src/runtime/skeleton_parse.cpp:494-511` | complete |
| Sampling | `src/runtime/animation.cpp:432-437` | complete |
| Application to the pose | `src/runtime/animation_state.cpp:1559-1577` | complete |
| Mix-out / snapshot restore | `src/runtime/animation_state.cpp:1664`, `:1781` | complete |
| Constant-timeline pruning | `src/runtime/skeleton_animation.cpp:275-279` | complete |
| World-transform consumption of the five modes | `src/runtime/skeleton_world.cpp:119,141,595,608,655,664` | complete |
| Runtime acceptance test | `src/samples/runtime_fixture_smoke.cpp:1883` | complete |
| Fixture with a real inherit timeline | `assets/fixtures/skin_inherit_constraints.mskl` | present |

**It is genuinely stepped.** `sample_inherit_timeline`
(`src/runtime/animation.cpp:432-437`) forwards to `sample_stepped_keyframe` —
piecewise constant, the same evaluator the attachment and draw-order families
use. There is no interpolation, no `curve` member is read
(`src/runtime/skeleton_parse.cpp:1876-1904` reads exactly `time` and `inherit`),
and no easing is stored. The word "stepped" in the title is a factual
description of the existing runtime, not a new behaviour to build.

### 1.2 The dopesheet already shows an Inherit lane

`build_tracks` emits an Inherit `TrackRow` for every
`animation.bone_inherit_timelines` entry —
`src/editor/timeline_model.cpp:158-166`, kind
`TimelineTrackKind::Inherit` (`src/editor/timeline_model.hpp:29`). Its key times
feed `collect_animation_key_times` (`src/editor/timeline_model.cpp:32`) and the
track count (`:77`). `src/editor/shell_timeline.cpp` iterates `TrackRow`s
generically and contains **zero** references to `TimelineTrackKind` (measured:
`grep -n "TimelineTrackKind::" src/editor/shell_timeline.cpp` returns nothing).

So an inherit lane and its key diamonds render today, for any animation whose
loaded skeleton carries an inherit timeline. **Nothing in MAR-184 needs to draw
it.** What does not exist is a lane whose keys came from the project, because
the project cannot hold any.

The lane is **not editable**: `track_is_editable`
(`src/editor/timeline_model.cpp:240-248`) returns true only for tracks with a
transform channel, a deform attachment, a slot Color/Attachment id, or the two
global ids. An Inherit row has `transform_channel == nullopt`, no
`deform_attachment_name`, no `slot_index`, and an id of the bone form. It
returns **false**. That is MAR-185's gap, and it stays open here.

### 1.3 MAR-167's exclusion of Inherit from the graph stands, untouched

`src/editor/timeline_graph_model.cpp:392-399` (`track_is_graphable`) and
`:477-483` (projection) both list `TimelineTrackKind::Inherit` in the
`return false` / `UnsupportedTrack` arm, beside `SlotAttachment`, `Deform`,
`DrawOrder` and `Event`. That is correct and deliberate: the scalar graph plots
a continuous value against time, and a `BoneInherit` is an unordered enum with no
numeric axis. Interpolating between `noScale` and `onlyTranslation` is
meaningless.

**MAR-184 does not touch the graph.** `timeline_graph_model.cpp` must appear in
the empty-diff proof of Task 7 (§5, inversion I8). If MAR-185 ever wants a
stepped visual, the dopesheet lane — which already exists — is where it belongs,
not the scalar graph.

### 1.4 The project layer has no inherit anything. Not one line.

Six timeline families have a complete project representation. Inherit is the
missing seventh:

| Family | Keyframe struct | Timeline struct | `ProjectData` vector | `find_*` | `ensure_*` | `.marrow` parse | `.marrow` build | runtime build |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Transform | `project.hpp:102` | `:112` | `:566` | `:609/619` | `:790` | `project.cpp:1846` | `:4010` | `:5417` |
| Mesh deform | `:172` | `:178` | `:567` | `:632/643` | `:796` | `:2017` | `:4059` | `:5426` |
| Draw order | `:213` | `:218` | `:569` | `:672/679` | `:807` | `:2394` | `:4099` | `:5437` |
| Event | `:223` | `:234` | `:570` | `:686/694` | `:812` | `:2569` | `:4118` | `:5444` |
| Slot color | `:239` | `:247` | `:571` | `:698/701` | `:817` | `:2623` | `:4149` | `:5451` |
| Slot attachment | `:263` | `:268` | `:572` | `:705/708` | `:824` | `:2717` | `:4184` | `:5459` |
| **Inherit** | — | — | — | — | — | — | — | — |

(`project.hpp` line numbers are struct declarations; `ensure_*` line numbers are
the free-function declarations near `project.hpp:790-830`.)

Measured: `grep -rn "nherit" src/editor/project.cpp` returns nothing.
`grep -n "nherit" include/marrow/editor/project.hpp` returns one hit,
`:442`, an unrelated doc comment about linked-mesh deform inheritance.

### 1.5 Why an inherit key in a `.marrow` is silently dropped today

`parse_transform_timeline_edits` walks `timeline_edits.animations.<a>.bones.<b>`
and, for each member, calls `transform_channel_from_key`
(`src/editor/project.cpp:1430-1445`, which knows `rotate`/`translate`/`scale`/
`shear`). An unrecognised key hits:

```cpp
// src/editor/project.cpp:1897-1901
const auto channel = transform_channel_from_key(channel_name);
if (!channel.has_value()) {
    continue;
}
```

**`continue`, not an error.** So a hand-written `.marrow` carrying
`bones.child.inherit` loads clean today and the data vanishes: it is not stored,
not serialized on the next save, and not exported. That silent drop is the
behaviour MAR-184 replaces, and it is also the reason AC2's "absent overlay data
preserves old-project behavior" is nearly free — every project in the tree omits
the key entirely (measured: zero `bones.*.inherit` members across
`assets/fixtures/player_idle.marrow`,
`assets/fixtures/parameter_face_basic.marrow`,
`assets/fixtures/atlas_pack_smoke/atlas_pack_project.marrow`).

### 1.6 `.mbin` needs no inherit-specific code, and that is measurable

`src/runtime/binary.cpp` contains **zero** occurrences of `inherit` (measured).
It is a generic JSON-document codec — `NodeTag { Null, Boolean, Number, String,
Array, Object }` at `:32-39` — plus one optional packed animation section whose
channel kinds are exactly `PackedChannelKind { Rotate = 0, Translate = 1 }`
(`:41-44`). Every other timeline family, inherit included, rides the generic
document encoder unchanged.

Consequence for AC5: **`.mbin` support is achieved by the JSON export being
correct.** `.mskl` stays version 1 and `.mbin` stays version 2
(`kBinaryVersionGenericDocument = 1`, `kBinaryVersionPackedAnimations = 2`,
`src/runtime/binary.cpp:22-23`). "JSON/binary equivalence" is proved with the
existing `./build/marrow_inspect --compare` tool, not with new binary code. If
the implementer finds themselves editing `binary.cpp`, they have taken a wrong
turn.

### 1.7 Fixture inventory — the constraint that shapes the test plan

Measured across every `assets/fixtures/*.mskl`:

| Fixture | Inherit timelines |
| --- | --- |
| `skin_inherit_constraints.mskl` | **1** — `toggle_inherit` / `child`, 4 keys: `0.0 normal`, `0.25 noRotationOrReflection`, `0.5 onlyTranslation`, `1.0 normal` |
| every other fixture, **`player_idle.mskl` included** | 0 |

`inherit_modes_nonuniform_scale.mskl`, despite its name, carries **setup-pose**
inherit modes on bones and no animated inherit timeline.

This matters because `marrow_project_smoke` is normally pointed at
`assets/fixtures/player_idle.marrow`, and that skeleton has no base inherit
timeline. AC6 demands "empty/base-backed/project-only timelines", so the
base-backed case needs `skin_inherit_constraints.mskl`.

**The pattern for that already exists and is proven.** `editor_project_smoke.cpp`
already builds throwaway projects over that exact fixture with
`create_minimal_project` — `src/samples/editor_project_smoke.cpp:9481-9500`,
`:9915-9925`, `:11769-11780`, `:12135-12143`. `shell_smoke_constraints.cpp:108-131`
documents the fixture's quirk: it ships no atlas, so the project borrows
`player_idle.matl` and nothing cross-validates the pairing. **Reuse that
pattern. Do not modify `player_idle.mskl` or `player_idle.marrow`** — AGENTS.md
records that `marrow_project_smoke` aborts on a partial marker match, and the
shared fixture is asserted on by a dozen other suites.

### 1.8 The pruning hazard — the single most likely way to write a broken test

```cpp
// src/runtime/skeleton_animation.cpp:275-279
erase_matching_timelines(&bone_inherit_timelines, [&](const BoneInheritTimeline& timeline) {
    return has_single_key_at_origin(timeline) &&
        timeline.bone_index < bones.size() &&
        timeline.keyframes.front().inherit == bones[timeline.bone_index].inherit;
});
```

`has_single_key_at_origin` is `keyframes.size() == 1 && time == AnimationScalar{0.0f}`
(`src/runtime/skeleton_animation.cpp:118-121`), and `prune_constant_timelines`
runs during skeleton construction (`src/runtime/skeleton.cpp:116`) — i.e. inside
`build_project_runtime` on the way out of `load_skeleton_data`.

So: **a project-only overlay of exactly one key at time `0.0` whose mode equals
the bone's setup inherit is deleted by the runtime before any test can see it.**
Every bone in `skin_inherit_constraints.mskl` has an absent `inherit` member,
i.e. setup `Normal` (measured). A test that authors `[{time: 0, inherit:
"normal"}]` on `child` and then asserts a timeline exists **will fail, and the
overlay code will not be the reason.**

Legal project-only test data, therefore, is either a single key at `t > 0`, a
single key at `t = 0` with a **non-`normal`** mode, or two or more keys. §6.2
pins the exact fixtures.

### 1.9 One asymmetry found in passing, recorded and not fixed

Every runtime timeline parser rejects an empty keyframe array — eight sites,
`src/runtime/skeleton_parse.cpp:1768, 1854, 1946, 2056, 2145, 2288, 2391, 2633`.
The project-side `ensure_*_timeline_edit` helpers, however, will create an edit
with **zero** keyframes when the base animation has no such timeline (e.g.
`ensure_slot_attachment_timeline_edit`, `src/editor/project.cpp:7160-7186`), and
neither `build_timeline_edits_value` (`:4381`) nor `build_runtime_document`
(`:5335`) skips an empty edit. An empty slot-attachment edit reaching export
would emit `"attachment": []` and produce an unloadable `.mskl`.

That is pre-existing and out of scope. **MAR-184 must not reproduce it**: §2.10
requires both inherit serializers to skip an empty edit, and §5's inversion I6
proves the guard bites.

### 1.10 Baselines to capture at `4a4249d` (Task 0)

- Agent registry: **64** operations. Measured three ways today —
  `src/samples/agent_dispatch_smoke.cpp:39` declares
  `std::array<OperationExpectation, 64>`;
  `grep -c '^\s*{\s*"' src/editor/agent_dispatch.cpp` → `64`;
  `tools/mcp/test_client.py:53,55` assert `== 64`.
- Guard sites asserting `!= 64U`: `src/editor/shell_smoke_constraints.cpp:147,676`;
  `src/editor/shell_smoke_timeline.cpp:3697`;
  `src/editor/shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603`. **Ten**
  sites. Task 0 re-counts them.
- **Count-sweep traps — never blind-substitute `64`.** `AGENTS.md` contains
  `t = 0.62` and a self-referential "62 lines". Do not touch
  `src/editor/shell_smoke_graph.cpp:2977`'s `4364U`,
  `src/editor/shell_smoke_viewport.cpp:3504`'s `640`,
  `IM_COL32(56, 61, 69, 255)`, `IM_COL32(208,134,57,230)`, `(51, 56, 64)`,
  `rgb(54,57,64)`, `"x": 56.0`, `56,995,840`, `PhysicsBoneState … 56 bytes/bone`,
  or `tools/mcp/test_client.py:1484`'s ordinal.
  MAR-184 changes the registry **not at all**, so the correct diff for every one
  of these files is empty.
- `serialize_project(assets/fixtures/player_idle.marrow)` byte length and SHA —
  the non-effect witness for §11.

---

## 2. Decisions

Every decision below is final for this story. No "TBD".

### 2.1 The `.marrow` schema mirrors the runtime exactly

```json
"timeline_edits": {
  "animations": {
    "toggle_inherit": {
      "bones": {
        "child": {
          "inherit": [
            { "time": 0.0,  "inherit": "normal" },
            { "time": 0.25, "inherit": "noRotationOrReflection" },
            { "time": 0.5,  "inherit": "onlyTranslation" }
          ]
        }
      }
    }
  }
}
```

Key `"inherit"` sits beside `rotate`/`translate`/`scale`/`shear` under a bone —
the same nesting the runtime uses (`src/runtime/skeleton_parse.cpp:5293`) and the
same nesting `build_timeline_edits_value` already produces for transform channels
(`src/editor/project.cpp:4389-4401`).

**Why identical to the runtime, rather than a project-specific shape:** every
sibling family already mirrors its runtime layout, `build_runtime_document`'s
overlay is a direct member assignment, and a divergent shape would need a
translation layer that buys nothing. The one thing the project form adds over the
runtime form is a `double` time where the runtime narrows to `float`
(`AnimationScalar = float`, `include/marrow/runtime/animation.hpp:14`) — the same
narrowing every family already has.

The five mode tokens are exactly the runtime's:
`normal`, `onlyTranslation`, `noRotationOrReflection`, `noScale`,
`noScaleOrReflection` (`src/runtime/skeleton_parse.cpp:494-511`). AC1 names the
same five.

### 2.2 Overlay semantics: whole-timeline replacement, keyed by (animation, bone)

Consistent with every sibling. In `build_runtime_document`, an inherit edit
performs `bone_value->as_object()["inherit"] = build_inherit_keyframes_value(edit)`
— a full replace of that one bone's inherit track inside that one animation.

"Combines imported base timelines and project overlays" (AC3) is therefore
satisfied at two levels:

1. **Across the document.** Every animation, bone, and channel the project does
   not override survives untouched, because `build_runtime_document` starts from
   a **copy** of the base document (`Document document = base_skeleton_document;`,
   `src/editor/project.cpp:5338`) and assigns into it.
2. **Within one timeline.** `ensure_bone_inherit_timeline_edit` materializes the
   base track's keys into the project on first touch, so a first merge extends
   the imported track instead of replacing it. This is exactly
   `ensure_slot_attachment_timeline_edit`'s contract
   (`src/editor/project.cpp:7160-7186`).

"Without mutating shared `SkeletonData`" (AC3) is a property of the existing
architecture, not new code: the project never writes to a `SkeletonData`. It
builds a JSON document and calls `load_skeleton_data`
(`src/editor/project.cpp:7874-7875`), which allocates a fresh one. The design
adds nothing that could break this; §5's inversion I3 proves the base document is
not mutated.

### 2.3 Stable key identity is the key time, at 1e-6

AC1 says "stable key identity". For inherit that is the **time**, compared with
the shared `find_keyframe_near_time` epsilon of `1e-6`
(`include/marrow/editor/project.hpp:145-168`; `kKeyTimeEpsilon`,
`src/editor/timeline_model.hpp:20`).

There is no same-time ordinal, because inherit times are **strictly increasing**
by construction — the runtime rejects `time <= previous_time`
(`src/runtime/skeleton_parse.cpp:1905-1912`) and §2.4 mirrors that in the project
parser. Contrast with events, which are the one family that permits stable
same-time entries and therefore carries `same_time_ordinal`
(`include/marrow/editor/authoring.hpp:117-126`).

MAR-185's "preserving stable same-time identities" clause reduces, for inherit,
to "the time is the identity and two keys can never share one". That is a
simplification MAR-185 inherits, and it is why `TimelineKeySelector` needs no new
field beyond `TimelineKeyKind::Inherit` when MAR-185 adds it.

### 2.4 Validation, exhaustively

The project-side keyframe parser rejects each of these with a `LoadError` naming
the offending JSON path — the shape every sibling parser uses
(`validation_error`, `src/editor/project.cpp:32-40`):

| Condition | Message shape | Runtime also rejects? |
| --- | --- | --- |
| timeline value is not an array | `require_type` | yes |
| array is empty | `inherit timeline edits must contain at least one keyframe` | yes (`:1854`) |
| keyframe is not an object | `require_type` | yes |
| `time` missing or non-number | `read_required_number` | yes |
| `time` not finite (NaN/inf) | `keyframe time must be finite and non-negative` | **no** |
| `time` negative | `keyframe time must be finite and non-negative` | **no** |
| `inherit` missing or non-string | `require_member` | yes |
| `inherit` not one of the five | `inherit mode must be one of normal, onlyTranslation, noRotationOrReflection, noScale, or noScaleOrReflection` | yes (`:1901-1904`) |
| times not strictly increasing | `timeline keyframe times must be strictly increasing` | yes (`:1905-1912`) |
| keyframe carries a `curve` member | `inherit keys are stepped and must not carry curve data` | **no** (§2.5) |

Two rows are strictly stronger than the runtime and are required by AC1's
"finite non-negative time" and AC2's "continuous curve data rejected".

The **finite non-negative** rule is a genuine addition: the runtime's parser
starts `previous_time = 0.0; has_previous_time = false`
(`src/runtime/skeleton_parse.cpp:1859-1860`) and only compares against a
*previous* key, so a first key at `time: -5` passes the runtime today. The
project layer refuses it.

Reuse the existing `finite_animation_scalar` helper
(`src/editor/authoring.cpp:322`) rather than open-coding an `std::isfinite`.

### 2.5 The `curve` rejection lives in the project parser, **not** the runtime

`parse_inherit_timeline` reads exactly `time` and `inherit`
(`src/runtime/skeleton_parse.cpp:1876-1904`) and ignores every other member. A
`.mskl` carrying `{"time": 0, "inherit": "normal", "curve": "stepped"}` loads
clean today.

**Decision: leave that alone.** Adding a rejection to the runtime parser would
make previously-valid `.mskl` files fail to load — a format compatibility break
that AC5 explicitly forbids ("retaining current format versions"). The rejection
belongs where AC2 puts it: on the **overlay**.

The practical effect is the one that matters. The editor is the only thing that
writes an inherit overlay, the overlay is the only thing that can introduce one
into a project, and a hand-edited project with a `curve` member fails loudly at
load with a JSON path instead of silently dropping the easing on the next save.
The `.marrow` keyframe object has never preserved unknown members
(`docs/root1/format-spec.md:929-932`), so accepting and dropping is the outcome
we are avoiding.

Recorded as a known limitation in §10: a `.mskl` may still carry inert `curve`
data on an inherit key, and Marrow ignores it.

### 2.6 The merge primitive

```cpp
// include/marrow/editor/authoring.hpp

/** @brief One requested stepped inherit key, in its wire form. */
struct InheritKeyRequest {
    double time{0.0};
    std::string mode;   ///< One of the five tokens; validated by the primitive.
};

struct InheritTimelineMergeRequest {
    std::string animation_name;
    std::string bone_name;
    std::vector<InheritKeyRequest> keys;   ///< Any order; result is sorted.
    /// When false (default) a key landing on an existing key's time is an
    /// error. When true it overwrites that key's mode in place.
    bool replace_existing_times{false};
};

struct InheritTimelineMergeResult : AuthoringResult {
    std::size_t added_key_count{0U};
    std::size_t replaced_key_count{0U};
    std::size_t effective_key_count{0U};
};

InheritTimelineMergeResult merge_inherit_timeline(
    ProjectData* project,
    const runtime::SkeletonData& effective_skeleton,
    const InheritTimelineMergeRequest& request);
```

`AuthoringResult` is the established base (`include/marrow/editor/authoring.hpp:18-25`):
`bool changed`, `std::string error`, `std::vector<std::string> dependencies`,
`operator bool()` true when `error` is empty. `TimelineRetimeResult` (`:128`) and
`TimelineScaleResult` (`:499`) are the precedents for extending it.

**Why the request carries a mode *string* and the stored edit carries the typed
enum.** AC4 requires the primitive to report "invalid modes". With a typed
`BoneInherit` parameter an invalid mode is unrepresentable and the criterion
could not be met at this layer. Taking the token here makes the primitive the
single place the five-token vocabulary is enforced, and hands MAR-185's
`set_inherit_keyframe` agent operation its validation for free — the agent layer
receives a JSON string. The precedent is `upsert_lip_sync_mapping(ProjectData*,
Value mapping)` (`include/marrow/editor/authoring.hpp:70`), which takes JSON in
and stores typed data.

**Validate-before-mutate, in this order** (all checks complete before any write;
`*project` is bytewise identical on any error):

1. `project == nullptr` → `missing_project_result()`-shaped error.
2. `request.keys.empty()` → `"inherit merge requires at least one key"`.
3. Animation: `effective_skeleton.find_animation(animation_name) == nullptr` →
   `"animation '<name>' does not exist"`.
4. Bone: `effective_skeleton.find_bone_index(bone_name)` empty →
   `"bone '<name>' does not exist"`.
5. Every requested key, in request order, first offender wins:
   - non-finite or negative time → `"key time must be finite and non-negative"`;
   - unknown mode token → the five-token message from §2.4, quoting the token.
6. Collision **within the request**: two requested keys within `1e-6` →
   `"requested keys collide at time <t>"`. Checked on a sorted copy, so the
   message is deterministic regardless of request order.
7. Collision **against the existing effective timeline**, only when
   `replace_existing_times == false`: a requested key within `1e-6` of a key
   already present after materialization → `"a key already exists at time <t>"`.

**Then, and only then**, mutate:

8. `ensure_bone_inherit_timeline_edit(...)` — materializes the base track's keys
   into the project if this (animation, bone) has no edit yet.
9. Insert or overwrite each requested key, keeping the vector sorted ascending by
   time. Overwrite is mode-only; the stored time keeps its existing value so the
   1e-6 window never drifts a key.
10. Set `changed`, `added_key_count`, `replaced_key_count`,
    `effective_key_count`.

**Determinism** (AC4: "produces deterministic effective timelines"): the stored
vector is always sorted ascending with strictly increasing times, so two runs
with the same request in different key orders produce a byte-identical
`serialize_project`. That is the assertion, not a claim — §6.2 case P5.

`changed` is `false` with an empty `error` in exactly one case: a request whose
every key already exists at that time with that mode, under
`replace_existing_times = true`. The project is then untouched and no revision
moves. MAR-185's transaction layer relies on this to avoid empty undo entries.

### 2.7 The registry does not change

MAR-185 owns `set_inherit_keyframe` and `remove_inherit_keyframe`, by name, in
its own acceptance criteria. MAR-184 adds no agent operation and no MCP tool.

The proof is an **empty diff**, not an assertion: `src/editor/agent_dispatch.cpp`,
`src/editor/agent_handlers_*.cpp` (all five), `src/samples/agent_dispatch_smoke.cpp`,
and `tools/` must be byte-identical to `4a4249d` at the end of the story
(Task 7). `agent_operation_descriptor_count()` stays **64** and all ten `!= 64U`
guard sites stay untouched.

### 2.8 No UI file changes

`src/editor/shell_*.cpp`, `src/editor/timeline_controller.cpp`,
`src/editor/timeline_graph_model.cpp`, `src/editor/timeline_model.{hpp,cpp}` are
all out of scope, and all appear in Task 7's empty-diff proof.

`timeline_model.cpp` in particular is tempting and must be resisted: adding
`Inherit` to `track_is_editable` would open editing without any of MAR-185's
selection, retime, clipboard, cascade, or undo work behind it, producing a lane
the user can click and that then behaves incorrectly.

**Consequence for the frame-smoke question in the brief.** The brief asks which
half of the hand-maintained duplicate frame bodies (`shell_main.cpp` /
`shell_smoke_frames.cpp`) a detector catches. The answer for this story: **the
question does not arise.** MAR-184 touches neither file, adds no widget, and
deletes none, so the one-directional coverage hazard has nothing to bite on.
`AGENTS.md`'s "A UI-free helper cannot observe a deleted widget" note applies to
stories that ship a widget; this one ships a file format and a primitive, and a
UI-free assertion is the *correct* instrument for both. No real-mouse sweep is
planned, and §10 records what that consciously leaves uncovered.

### 2.9 What the user sees, and why it costs nothing

Once a project overlay materializes, `build_project_runtime` returns a
`SkeletonData` whose `AnimationData::bone_inherit_timelines` contains the merged
track. `build_tracks` (`src/editor/timeline_model.cpp:158-166`) then emits an
Inherit `TrackRow` for it, and the dopesheet draws it — with no editor change at
all, because the panel iterates rows generically (§1.2).

This is a **claim about existing code and must be tested, not asserted.** §6.2
case P7 builds the tracks from a project-materialized skeleton and asserts the
Inherit row and its key times are present. It runs in `marrow_project_smoke`
against `timeline_model::build_tracks` — a pure function over `SkeletonData`,
with no ImGui and no shell — so it is honest about what it covers: the row is
*produced*. Whether a pixel is *drawn* is not covered here, and §10 says so.

`src/editor/session.cpp:125-126` already includes `bone_inherit_timelines` in
`animation_timing_equal`, so preview and selection reconciliation see an inherit
timing change the moment one exists. No change needed there either — and
`session.cpp` is in the empty-diff proof.

### 2.10 Empty edits never reach either serializer

Both `build_timeline_edits_value` and `build_runtime_document` **skip** an inherit
edit whose `keyframes` vector is empty.

Rationale in §1.9: the runtime rejects an empty inherit array
(`src/runtime/skeleton_parse.cpp:1851-1857`), and
`ensure_bone_inherit_timeline_edit` can legitimately create an empty edit for a
bone with no base track — that is what "project-only timeline" means before the
first key lands. Without the guard, an `ensure` followed by a failed merge would
write `"inherit": []` and produce an unloadable export.

The `.marrow` parser rejects an empty array on the way in (§2.4), so an empty
edit is only ever a transient in-memory state. Inversion I6 proves the guard.

### 2.11 Formats and versions do not move

`.mskl` stays version `1`; `.mbin` stays version `2`
(`src/runtime/binary.cpp:22-23`). No new C ABI surface. `.marrow` gains no
version field — it has none (`docs/root1/format-spec.md:661-662`) — and the new
key is additive, so a build that has never heard of it reads the file and hits
the `continue` at `src/editor/project.cpp:1900`, exactly as today.

---

## 3. Where each piece lands

No new translation unit. Every change extends an existing file, in the place its
six siblings already occupy.

### 3.1 `include/marrow/editor/project.hpp`

- `struct InheritKeyframeEdit { double time; runtime::BoneInherit inherit; };`
  beside `SlotAttachmentKeyframeEdit` (`:263`).
- `struct BoneInheritTimelineEdit { std::string animation_name; std::string
  bone_name; std::vector<InheritKeyframeEdit> keyframes; };` — **no** `loop_sync`
  (a stepped lane has no boundary easing to mirror; the three families that carry
  it are transform, deform and slot color), **no** `curve_mode`/`curve_driver`
  (`docs/root1/format-spec.md:925-928` already states the discrete families carry
  none).
- `std::vector<BoneInheritTimelineEdit> bone_inherit_timeline_edits;` on
  `ProjectData`, placed **after** `transform_timeline_edits` (`:566`) to mirror
  the runtime's own ordering of bone tracks.
- `find_bone_inherit_timeline_edit(animation, bone)`, const and mutable.
- `ensure_bone_inherit_timeline_edit(ProjectData&, const runtime::SkeletonData&,
  animation, bone)`, beside the other five `ensure_*` free functions.

### 3.2 `src/editor/project.cpp`

- `inherit_mode_json_key(runtime::BoneInherit) -> std::string_view` and
  `inherit_mode_from_key(std::string_view) -> std::optional<runtime::BoneInherit>`
  in the anonymous namespace, beside `transform_channel_json_key` (`:1415`) and
  `path_spacing_mode_json_key` (`:1469`). **Both directions, one table.** These
  are the single vocabulary source for the parser, both serializers, and the merge
  primitive. Do **not** reach into `src/runtime/skeleton_parse.cpp`'s
  `parse_bone_inherit` — it is in an anonymous namespace and has no inverse.
- `parse_inherit_keyframes(...)` modelled on `parse_slot_attachment_keyframes`
  (`:2717`), implementing §2.4's full table.
- `parse_bone_inherit_timeline_edits(...)` modelled on
  `parse_transform_timeline_edits` (`:1846`), walking the same
  `timeline_edits.animations.<a>.bones.<b>` tree and picking the `inherit`
  member. It must **skip** the four transform channel keys rather than erroring,
  exactly as the transform parser skips `inherit` today.
- `build_inherit_keyframes_value(const BoneInheritTimelineEdit&)` beside
  `build_slot_attachment_keyframes_value` (`:4184`). One builder serves both
  `.marrow` and `.mskl` — unlike transform and slot color, which need a
  `build_runtime_*` variant to strip `curve_mode`/`curve_driver`; inherit has no
  project-only member to strip.
- A branch in `build_timeline_edits_value` (`:4381`), after the transform loop.
- A branch in `build_runtime_document` (`:5335`), after the transform loop at
  `:5417-5426`.
- The `parse_bone_inherit_timeline_edits` call in `load_project`, placed
  **immediately after** `parse_transform_timeline_edits` (`:7417-7421`) and
  **before** `parse_loop_sync` (`:7451`) — the comment at `:7448-7449` requires
  every `timeline_edits` parser to have run before loop-sync cross-references
  them. Inherit authors no loop-sync leaf, but the ordering invariant is stated
  in the code and must not be weakened.
- `find_*` / `ensure_*` definitions beside their siblings (`:6994-7018`,
  `:7160-7186`).

### 3.3 `include/marrow/editor/authoring.hpp` + `src/editor/authoring.cpp`

`InheritKeyRequest`, `InheritTimelineMergeRequest`,
`InheritTimelineMergeResult`, `merge_inherit_timeline` — declared after
`TimelineScaleResult` / `scale_keyframe_times` (`:494-560`), defined in
`authoring.cpp` near the other timeline primitives.

The primitive uses `effective_skeleton.find_animation` and
`find_bone_index` for existence checks, and the shared `1e-6` epsilon. It does
**not** duplicate the mode table — it calls the project-layer
`inherit_mode_from_key`, which therefore needs a declaration in
`include/marrow/editor/project.hpp` rather than staying anonymous.

### 3.4 Tests

- `src/samples/editor_project_smoke.cpp` — the project-layer cases (§6.2).
- `src/samples/runtime_fixture_smoke.cpp` — extend the existing
  `validate_runtime_inherit_timeline_and_skin_constraints` (`:1883`) only if
  Task 0 finds a runtime behaviour uncovered; §6.3 explains why the expected
  answer is "no change".

### 3.5 Documentation

- `docs/root1/format-spec.md` — the `timeline_edits` list at `:869-877` gains
  "bone inherit edits"; a new `#### inherit (MAR-184, stepped)` subsection after
  the `curve_mode`/`curve_driver` block at `:881-932` carries §2.4's validation
  table and the §2.5 asymmetry. The `.mskl` `animations` prose at `:169-176`
  already lists inherit and needs no change.
- `AGENTS.md` — one new `## MAR-184 …Validation Results` section and the new
  project-smoke command line under `## Current Validation`.
- `.agents/tasks/prd-marrow-runtime.json` — `MAR-184` → `done`, `completedAt`.

---

## 4. Acceptance criteria → artifacts

| AC | Delivered by | Proved by |
| --- | --- | --- |
| 1 — optional additive overlay, animation/bone/identity/time/five modes | §2.1, §2.3, §2.4; `project.hpp` structs; `parse_inherit_keyframes` | P1, P2, P3 |
| 2 — absent data preserves old behavior; stepped-only; curve rejected | §2.4, §2.5; §1.5's `continue` is unchanged for unknown keys | P4, P6, I1 |
| 3 — materialization combines base + overlay, no `SkeletonData` mutation, no unrelated loss | §2.2; `build_runtime_document` branch; `ensure_*` | P8, P9, I3 |
| 4 — deterministic merge; missing animation/bone, invalid mode, time collision reported before mutation | §2.6 `merge_inherit_timeline` | P5, P10, P11, I4 |
| 5 — export to `.mskl` and `.mbin`, versions retained, JSON/binary equivalence | §1.6, §2.11; `build_runtime_document` | P12, and `marrow_inspect --compare` |
| 6 — project + runtime tests: five modes, empty/base-backed/project-only, old-project load, save/reload, merge errors, export | §6 | P1–P12, R1 |

---

## 5. Inversions, and the mechanism that makes each observable

The standing lesson from MAR-178…183: *every* one of those stories specified at
least one inversion that could not bite. Each row below names the **layer** the
assertion runs at and the **reason** the inverted code is reachable from it.

| # | Inversion | Observable because |
| --- | --- | --- |
| I1 | Delete the `curve`-member rejection from `parse_inherit_keyframes` | P6 loads a `.marrow` **string** containing `"curve"` on an inherit key through `load_project` and requires `result.error`. The parser is the only thing between the string and the result; with the check gone the load succeeds and P6 fails by name. |
| I2 | Change the negative-time rejection to `time > -1.0` | P2 feeds `time: -0.5`. Not defended anywhere else: §2.4 measured that the **runtime** accepts a negative first key (`src/runtime/skeleton_parse.cpp:1859-1860`), so the project parser is the sole gate and the case cannot pass by accident. |
| I3 | In `build_runtime_document`, drop the inherit branch | P8 exports a project whose only overlay is inherit and re-parses the result; the animation comes back with zero inherit timelines. Reachable because the assertion reads the **materialized `SkeletonData`**, not the project. |
| I4 | In `merge_inherit_timeline`, move the collision check to after `ensure_bone_inherit_timeline_edit` | P11 captures `serialize_project` before a colliding merge and requires it byte-identical after the rejection. Moving the check leaves a newly materialized edit vector behind, so the strings differ. **This is the inversion that proves "before mutation" is real** rather than merely ordered in the source. |
| I5 | Sort the merged keys descending instead of ascending | P5 merges the same three keys in two different request orders and requires identical `serialize_project` **and** a strictly increasing stored time sequence. A descending sort fails the second half even when both runs agree. |
| I6 | Remove the empty-edit skip from `build_runtime_document` | P9 calls `ensure_bone_inherit_timeline_edit` for a bone with no base track, performs **no** merge, and exports. Without the skip the export writes `"inherit": []` and the re-parse fails with the runtime's own `must contain at least one keyframe` — a loud, named failure. |
| I7 | Return `Normal` instead of `std::nullopt` from `inherit_mode_from_key` for an unknown token | P3 asserts on **all five** modes round-tripping *and* P10 requires an unknown token to be an error naming the token. A silent `Normal` fallback fails P10; a fallback that also broke a real mode fails P3. Both directions of the table are covered. |
| I8 | Add `Inherit` to `track_is_editable` (`src/editor/timeline_model.cpp:240`) | Task 7's empty-diff proof over `src/editor/timeline_model.cpp` fails. This is a *scope* inversion: it verifies MAR-185's work has not leaked in. |

Inversion methodology, from `AGENTS.md`'s recorded hazard: after restoring a
file between inversions, **`touch` it** — a restore that lands in the same mtime
second makes `make` skip the rebuild and the "it still passes" reading is an
artifact. After the final inversion, `rm -rf build` and do one clean configure +
build + full run.

---

## 6. Test surfaces

### 6.1 Which binaries

| Binary | Role in this story |
| --- | --- |
| `marrow_project_smoke` | **primary.** Every new case. Two invocations: the standing `player_idle.marrow` run (regression + the old-project case) and the self-built `skin_inherit_constraints` projects. |
| `marrow_fixture_smoke` | regression witness for the runtime layer (`assets/fixtures/skin_inherit_constraints.mskl` already covered at `runtime_fixture_smoke.cpp:1883`). |
| `marrow_inspect --compare` | AC5's JSON/binary equivalence. |
| `marrow_agent_dispatch_smoke` | must pass **unchanged**; §2.7's witness. |
| `marrow_editor_shell --auto-close 2` | regression only; C4–C25 must still pass. |
| `marrow_timeline_model_tests` | unchanged; runs as a witness that §2.8 held. |

The new cases live in one new function,
`validate_mar184_inherit_overlays(...)`, following the file's existing shape
(`validate_mar177_scenario_*`, `validate_mar179_constraint_parameters`, …).

### 6.2 Project cases

Fixture strategy, per §1.7: build throwaway projects over
`assets/fixtures/skin_inherit_constraints.mskl` with `create_minimal_project`,
borrowing `assets/fixtures/player_idle.matl`, into a scratch directory — the
exact pattern at `src/samples/editor_project_smoke.cpp:11769-11780`. The base
animation is `toggle_inherit`; the base-backed bone is `child` (4 keys); a bone
with **no** base track is `controller`.

**Pruning-safe data, per §1.8:** every bone in that fixture has setup inherit
`Normal`, so no project-only case may use a lone `{t: 0, "normal"}` key. The
project-only cases below use `{0.0, "noScale"}` plus `{0.4, "normal"}`.

| # | Case | Asserts |
| --- | --- | --- |
| P1 | Author a two-key overlay on `controller`, `save_project` → `load_project` | The reloaded `ProjectData` carries one `BoneInheritTimelineEdit` with the same animation, bone, times and modes. Save/reload, AC6. |
| P2 | Load a `.marrow` string with `time: -0.5`; another with `time: NaN`; another with a non-increasing pair | Each is a `LoadError` whose path names the offending keyframe. AC1. |
| P3 | Round-trip **all five** modes as five keys on one bone, through save → load → `build_project_runtime` | All five survive as the matching `runtime::BoneInherit`. AC6's "all five modes". |
| P4 | `load_project(assets/fixtures/player_idle.marrow)` | `bone_inherit_timeline_edits.empty()`, and `serialize_project` is byte-identical to the Task 0 baseline. AC2's old-project clause. |
| P5 | Merge `{0.5,noScale}, {0.1,normal}, {0.3,onlyTranslation}` into an empty overlay; separately merge the same three in reverse order | Both `serialize_project` results are byte-identical, and stored times are strictly increasing. AC4 determinism. |
| P6 | Load a `.marrow` whose inherit key carries `"curve": [0,0,1,1]` | `LoadError` naming the keyframe path. AC2. |
| P7 | `build_project_runtime` on a project-only overlay, then `timeline_model::build_tracks` | A `TrackRow` of kind `Inherit` exists for that bone with the overlay's key times. §2.9. |
| P8 | Export a project whose only edit is an inherit overlay on `controller`, re-parse the `.mskl` | The animation has **two** inherit timelines — `child`'s untouched base track and `controller`'s new one — and every other timeline family in the document is unchanged. AC3's "without losing unrelated animation data". |
| P9 | `ensure_bone_inherit_timeline_edit` for `controller` (no base track), no merge, then export | The exported `.mskl` contains **no** `inherit` member for `controller`, and re-parses clean. §2.10. |
| P10 | `merge_inherit_timeline` with (a) an unknown animation, (b) an unknown bone, (c) mode `"noScales"`, (d) a non-finite time | Four distinct errors, each naming the offending value; `result.changed == false` in all four. AC4. |
| P11 | `merge_inherit_timeline` colliding with an existing key at `replace_existing_times = false`, with `serialize_project` captured before and after | Error returned; strings byte-identical. Then the same merge with `replace_existing_times = true` succeeds, `replaced_key_count == 1`, `effective_key_count` unchanged. AC4 "before mutation". |
| P12 | Export a base-backed overlay on `child` to `.mskl` **and** `.mbin`, then `load_skeleton_data` on each | Both carry the merged timeline with equal times (within float32) and identical modes. `.mskl` version 1, `.mbin` version 2. AC5. |

P4 also carries the **non-effect proof**: an unchanged project must serialize to
the same bytes as at `4a4249d`.

### 6.3 Runtime cases

`validate_runtime_inherit_timeline_and_skin_constraints`
(`src/samples/runtime_fixture_smoke.cpp:1883`) already samples `toggle_inherit`
at `0.0` and `0.5` and compares against reference world transforms per mode.
MAR-184 changes no runtime file, so the expected change here is **none** — the
suite runs as a regression witness.

Task 0 gates this: if the measurement finds the existing case does not exercise
all five modes, add the missing ones there. Do **not** widen it speculatively;
AC6's "all five modes" is already met by P3 at the project layer, which is where
this story's code lives.

### 6.4 Old-project compatibility, stated as a mechanism

AC2's guarantee is structural: `parse_bone_inherit_timeline_edits` returns early
with an empty vector when `timeline_edits` is absent (the pattern at
`src/editor/project.cpp:1850-1854`), and the serializers skip an empty vector, so
a project with no overlay serializes to the same bytes it did before. P4 is the
witness.

---

## 7. Verification commands

From `AGENTS.md` `## Current Validation`, verbatim:

```
cmake -S . -B build
cmake --build build

./build/marrow_unit_tests
./build/marrow_timeline_model_tests
./build/marrow_timeline_graph_model_tests
./build/marrow_selection_tests
./build/marrow_preference_tests

./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
python3 -m json.tool assets/fixtures/skin_inherit_constraints.mskl > /dev/null

./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
  --export-runtime /tmp/player_idle_project_export.mskl \
  --export-binary  /tmp/player_idle_project_export.mbin
./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin /tmp/player_idle_project_export.mskl

./build/marrow_agent_dispatch_smoke
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2

ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -L runtime
ctest --test-dir build --output-on-failure -L editor
cmake --build build --target marrow_constraint_warning_check
cmake --build build --target marrow_verify_third_party
```

Relevant ctest names, measured: `marrow.project_smoke` (#15),
`marrow.runtime_fixture_smoke` (#13), `marrow.agent_dispatch_smoke` (#20),
`marrow.timeline_model` (#8), `marrow.timeline_graph_model` (#9).

**Do not run `./build/marrow_editor_shell` without `--auto-close`,** and do not
create `~/Library/Application Support/Marrow`.

---

## 8. Non-goals

- Any ImGui code, widget, panel, or frame smoke.
- Key **deletion** — MAR-185's `remove_inherit_keyframe`.
- Selection, retime, time scaling, clipboard, loop-sync, duration auto-grow, or
  undo integration for inherit lanes — MAR-185, every one.
- Animation rename/delete cascade over inherit overlays — MAR-185.
- Any agent operation or MCP tool — MAR-185.
- Making the Inherit lane editable (`track_is_editable`) — MAR-185.
- Plotting Inherit in the scalar graph — MAR-167 excluded it deliberately (§1.3)
  and nothing here revisits that.
- Changing the runtime's `.mskl` inherit parser, including adding a `curve`
  rejection (§2.5) or a negative-time rejection there.
- Any `.mskl` / `.mbin` / C ABI version movement.
- Editing `assets/fixtures/player_idle.mskl` or `player_idle.marrow`.

---

## 9. Facts to MEASURE in Task 0, not assume

1. Registry count and the ten `!= 64U` guard sites (§1.10). This document says
   64; re-derive it.
2. `serialize_project(player_idle.marrow)` byte length + SHA at `4a4249d`.
3. That `assets/fixtures/skin_inherit_constraints.mskl` still carries exactly one
   inherit timeline, `toggle_inherit`/`child`, with those four keys, and that all
   five of its bones have setup inherit `Normal`.
4. That `player_idle.mskl` still carries zero inherit timelines.
5. That `grep -c "nherit" src/runtime/binary.cpp` is `0`.
6. That `grep -n "TimelineTrackKind::" src/editor/shell_timeline.cpp` is empty.
7. Whether `runtime_fixture_smoke.cpp:1883` exercises all five modes (§6.3).
8. That the pruning behaviour of §1.8 is real: build a two-line probe project
   with a lone `{0.0,"normal"}` overlay on `controller`, materialize, and confirm
   the timeline is **absent**. If it is present, §1.8 is wrong and the test data
   in §6.2 can be simplified. **This is the single most consequential gate in
   Task 0.**

The author of this document did not build, did not run any binary, and did not
benchmark. Every performance-shaped or behaviour-shaped claim above is a reading
of source at `4a4249d`.

---

## 10. Known limitations, stated rather than hidden

1. **No pixel is asserted.** P7 proves an Inherit `TrackRow` is produced from a
   project-materialized skeleton. It does not prove the dopesheet draws it. That
   gap is acceptable because MAR-184 adds no drawing code — the row and its
   renderer both predate this story — but it means a regression that deleted the
   lane's draw call would not be caught here. MAR-185, which does ship UI, is
   where a real-mouse scenario belongs.
2. **A `.mskl` may carry inert `curve` data on an inherit key** and Marrow
   ignores it (§2.5). Only the overlay rejects it.
3. **The runtime still accepts a negative first inherit key time.** Only the
   project layer refuses (§2.4). A `.mskl` authored by another tool can carry one.
4. **The empty-edit hazard is fixed only for inherit.** §1.9's sibling families
   keep the pre-existing behaviour; that is out of scope and recorded, not fixed.
5. **`replace_existing_times` has no caller in this story.** It exists because
   AC4's collision semantics need a defined counterpart and MAR-185's
   `set_inherit_keyframe` will be it. P11 covers both arms so it is not dead code
   in the untested sense, but it is unexercised by any product surface until
   MAR-185.
6. **Time is `double` in the project and `float` in the runtime.** A project time
   that is not exactly representable in float32 will not compare equal after
   materialization. P12 compares with a float32-aware tolerance, as the existing
   `.mbin` narrowing cases already do (`AGENTS.md`, MAR-179 line).

---

## 11. Risks for the implementer

| # | Risk | Mitigation |
| --- | --- | --- |
| R1 | **Building the wrong story.** The title says "overlays"; a plausible reading is rendering. Half a day disappears into the graph view. | §0.1. The description and all six ACs are the authority. If you are editing `timeline_graph_model.cpp`, stop. |
| R2 | **The pruning trap** (§1.8) makes a correct implementation look broken, and the failure points at the overlay code. | Task 0 gate 8 measures it first. Test data in §6.2 is already pruning-safe. |
| R3 | Adding the inherit parser makes previously-loading projects fail, if any carried the key. | Measured: **zero** `.marrow` files in the tree carry `bones.*.inherit`. Re-measure in Task 0. |
| R4 | Parser ordering: placing the new call after `parse_loop_sync` breaks the invariant stated at `src/editor/project.cpp:7448-7449`. | §3.2 pins the insertion point. |
| R5 | Scope leak into MAR-185 — making the lane editable, or adding a registry op — passes tests and creates a half-built feature. | Task 7's empty-diff proof over `timeline_model.cpp`, `timeline_controller.cpp`, `shell_*.cpp`, `agent_dispatch.cpp`, `agent_handlers_*.cpp`, `agent_dispatch_smoke.cpp`, `tools/`, `timeline_graph_model.cpp`. Inversion I8. |
| R6 | Count-sweep damage from a blind `64` substitution. | Nothing in this story changes the count. §1.10 lists the literals that must not be touched. |
| R7 | An inversion that cannot bite — the standing failure of MAR-178…183. | §5 states the layer and the mechanism for each. Plus `touch` after restore, and a final `rm -rf build`. |
| R8 | `merge_inherit_timeline` validating in source order but mutating early. | I4 is designed exactly to catch it, with a byte-identical `serialize_project` before/after. |
| R9 | Reaching into `skeleton_parse.cpp`'s anonymous-namespace `parse_bone_inherit`. | §3.2: the project layer owns its own bidirectional table. Linking would fail anyway; the risk is a duplicated one-way table that drifts. |
