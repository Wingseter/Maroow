# MAR-185 Complete Inherit Timeline Editing Parity — Design

- Story: `MAR-185`, `.agents/tasks/prd-marrow-runtime.json`
- Depends on: `MAR-184` — **a real code dependency, not sequencing.** See §0.2.
- Baseline commit measured for this document: **`a2981b8`**
  (`feat: MAR-183 최근 프로젝트 목록 영속화와 관리 구현`), the tip of `feat/mar-168`
  on 2026-09-01. The working tree is clean apart from MAR-184's two untracked
  documents.
- Status of this document: design. **Nothing here was built, run, or
  benchmarked.** Every structural claim carries a `file:line` measured at
  `a2981b8`, or is listed in §9 as a Task 0 gate.
- **Every line number in this document is expected to have drifted.** MAR-184's
  implementation was adding to `project.cpp`, `project.hpp`, `authoring.cpp`,
  `authoring.hpp` and `editor_project_smoke.cpp` while this was written. The
  plan's **Task 0.1 is a blocking re-anchor gate** that re-resolves every
  citation against the actual tip before any implementation; a citation that no
  longer names its described construct is a finding to record, not a line to
  silently follow.

---

## 0. Where this story sits

### 0.1 The story in one sentence

MAR-184 gives the project a place to *store* stepped inherit keys and one
UI-free primitive that *writes* them. MAR-185 makes those keys **first-class
timeline keys**: selectable, retimeable, scalable, copy/pasteable, removable,
cascaded by animation rename and delete, and reachable from the agent and MCP
surfaces exactly as the other six families already are.

The measured shape of the work is therefore *not* "add a feature". It is
**"add the seventh member to six existing vocabularies"**, and the single
largest risk in the story is that the compiler will not tell you when you miss
one (§1.1).

### 0.2 MAR-184 is a hard dependency and **none of it exists yet**

Measured at `a2981b8`:

```
$ grep -c "nherit" src/editor/project.cpp                 → 0
$ grep -c "nherit" include/marrow/editor/authoring.hpp    → 0
$ grep -c "nherit" src/editor/authoring.cpp               → 1   (an unrelated
                                                                 linked-mesh
                                                                 doc comment)
$ git status --short → only MAR-184's two untracked .md files
```

MAR-184 is **written but not implemented**. Every symbol below is something
MAR-185 *consumes* and must not build:

| Symbol MAR-185 needs | Owner | Where MAR-184's plan puts it |
| --- | --- | --- |
| `InheritKeyframeEdit { double time; runtime::BoneInherit inherit; }` | MAR-184 | `include/marrow/editor/project.hpp`, beside `SlotAttachmentKeyframeEdit` (`:263`) |
| `BoneInheritTimelineEdit { animation_name; bone_name; keyframes }` | MAR-184 | `project.hpp`, beside `SlotAttachmentTimelineEdit` (`:268`) |
| `ProjectData::bone_inherit_timeline_edits` | MAR-184 | after `transform_timeline_edits` (`project.hpp:567`) |
| `ProjectData::find_bone_inherit_timeline_edit`, const + mutable | MAR-184 | beside `find_transform_timeline_edit` (`project.hpp:608/619`) |
| `ensure_bone_inherit_timeline_edit(ProjectData&, const SkeletonData&, anim, bone)` | MAR-184 | beside `ensure_slot_attachment_timeline_edit` (`project.cpp:7160`) |
| `inherit_mode_from_key` / `inherit_mode_json_key`, namespace scope | MAR-184 | `project.hpp` / `project.cpp` |
| `.marrow` parse + both serializers + the **seventh `!empty()` disjunct** at `project.cpp:4832-4847` | MAR-184 | its §A4 / inversion I9 |
| `merge_inherit_timeline(ProjectData*, const SkeletonData&, const InheritTimelineMergeRequest&)` with `replace_existing_times` | MAR-184 | `authoring.hpp` after `scale_keyframe_times` (`:559`) |

**Task 0 of the plan gates on every row.** If MAR-184 lands with a different
signature — most plausibly a different name for the merge request struct, or
`ensure_bone_inherit_timeline_edit` taking the skeleton by pointer — MAR-185
adapts to what shipped and records the difference; it does not re-open MAR-184's
decisions.

Two MAR-184 decisions MAR-185 **inherits and must not silently revisit**:

1. **`BoneInheritTimelineEdit` carries no `loop_sync`, no `curve_mode`, no
   `curve_driver`** (MAR-184 design §3.1). Every consequence in §2.5 follows
   from this and is a *structural* exclusion — enforced by a missing member, not
   a branch — which is the same technique `read_key_curve_mode` already uses to
   exclude Deform (`src/editor/authoring.cpp:2624-2645`).
2. **A key's time is its identity, at `1e-6`, and two inherit keys can never
   share a time** (MAR-184 design §2.3), because both the runtime parser
   (`src/runtime/skeleton_parse.cpp`, strictly-increasing rejection) and
   MAR-184's project parser refuse a non-increasing pair. §2.2 explains what
   AC1's "stable same-time identities" therefore reduces to.

### 0.3 Errors found in the incoming documents

Recorded here rather than propagated. See the plan's §A for the full register.

| # | Source | Claim | Measured at `a2981b8` |
| --- | --- | --- | --- |
| E1 | The MAR-185 briefing | `build_project_runtime()` is at `project.cpp:7495` | `build_project_runtime` is **defined** at `src/editor/project.cpp:7860` and **called from `load_project`** at `:7532-7533`. `:7495` is inside the atlas-pack cross-validation loop. The substantive point — that only `load_project(path)` materializes — **holds** |
| E2 | MAR-184 plan header | "Baseline commit: `25bf694`" | The tip of `feat/mar-168` is **`a2981b8`**. Both `25bf694` and `4a4249d` exist as commits but neither is the tip. MAR-184's `file:line` anchors were spot-checked against `a2981b8` and hold with the drift its §A6 already records |
| E3 | MAR-184 plan §A2 | The marker-gated `else` is `editor_project_smoke.cpp:14056-14203` | The gate is `:14062-14203`: `main()` at `:14028`, the `create_project` arm at `:14056-14061`, the `markers.present.empty()` skip at `:14062-14072`, the partial-match abort at `:14073-14082`, the `else` at `:14083-14206`. The substantive claim — **one invocation, build throwaway projects inside it** — holds exactly |
| E4 | MAR-184 design §1.2 | `track_is_editable` is at `timeline_model.cpp:240-248` | Correct: `:240-249`. Recorded because MAR-185 **edits this function** and the anchor must be right |

---

## 1. The measured gap

Everything in this section was read at `a2981b8`.

### 1.1 The compiler will not help you. This is the story's defining constraint.

`TimelineKeyKind` has six values (`include/marrow/editor/authoring.hpp:102-109`).
MAR-185 adds a seventh. Across the tree, **33 sites** switch on or enumerate that
type:

```
$ grep -rn "TimelineKeyKind::SlotAttachment" src/ include/ tools/ | wc -l  → 28
   src/editor/authoring.cpp             19
   src/editor/agent_handlers_editing.cpp 7
   src/samples/editor_project_smoke.cpp  5   (test side)
   src/editor/timeline_controller.cpp    2
```
plus three *non-switch* six-element call lists (`rename_all_timeline_edits`,
`erase_all_timeline_edits`, `auto_extend_explicit_animation_durations`) and the
if/else chains in `timeline_key_selector`, `visit_editable_timeline_keys`,
`visit_existing_project_timeline_keys`, `copy_selected_timeline_keys`,
`paste_timeline_clipboard`, and `parse_timeline_key_selectors`.

**And the build has no `-Wswitch`.** Measured:

```
$ grep -n "add_compile_options\|CMAKE_CXX_FLAGS\|target_compile_options" CMakeLists.txt
19:    add_compile_options(/utf-8)              # MSVC only
349:    target_compile_options(marrow_constraint_warning_check ...)
362:    target_compile_options(marrow_thread_sanitizer_flags INTERFACE ...)
```

`-Werror` appears **once**, at `CMakeLists.txt:352`, scoped to
`marrow_constraint_warning_check` — a one-file `EXCLUDE_FROM_ALL` static library
over `src/runtime/skeleton_constraints.cpp` that does not compile
`authoring.cpp`. There is no `-Wall`, no `-Wextra`, and no `-Wswitch` on any
product target with Clang or GCC.

So adding `TimelineKeyKind::Inherit` produces **zero diagnostics**, and every
missed switch silently falls through to whatever the function does after its
`switch`. The table below is the complete inventory of what that fall-through
*is*, per site, and therefore of which omissions are observable at all. It is
the single most important table in this document, because it is what turns
"add an arm everywhere" into a set of falsifiable claims.

| # | Site (`src/editor/authoring.cpp` unless noted) | Fall-through when the `Inherit` arm is missing | Observable? |
| --- | --- | --- | --- |
| S1 | `resolve_timeline_key` `:195` | `*error_out = "Unsupported timeline key kind."`, returns `nullopt` | **Loudly.** Every inherit retime/scale/interp call rejects |
| S2 | `family_owns_scalar_component` `:337` | trailing `return false` — the correct answer | **No** — the omission is invisible |
| S3 | `read_scalar_component` `:371` | trailing `return false` — correct | **No** |
| S4 | `write_scalar_component` `:425` | falls off both `if`s, writes nothing — correct | **No** |
| S5 | `family_key_spacing` `:524` | trailing `return kNonEventKeySpacing` — the correct 1 ms | **No** |
| S6 | `resolved_key_is_loop_pinned` `:596` | trailing `return false` — correct | **No** |
| S7 | `scale_lane_label` `:621` | trailing `return "timeline key"` — **wrong text** in every scale rejection | **Yes, on the message** |
| S8 | `resolved_stored_key_time` `:684` | trailing `return resolved.original_time` — the **selector's** float32-narrowed time instead of the stored `double` | Yes, but only for a time not exactly representable in float32 — see §5 I13 |
| S9 | `apply_resolved_scale` `:830` | `void`, no trailing statement — **the scale writes nothing** | **Yes** |
| S10 | `include_resolved_retime_bounds` `:863` | `void`, no trailing statement — **no neighbour bound at all** | **Yes** |
| S11 | `apply_resolved_retime` `:942` | `void`, no trailing statement — **the retime writes nothing** | **Yes** |
| S12 | `sort_retimed_timelines` `:986` (a six-call list) | the inherit vector is never re-sorted | **No** — provably: a retime's shared clamped delta and a scale's validated projection both preserve index order. The call is defence in depth and the source says so (`:2434-2435`) |
| S13 | `scale_keyframe_times`'s validate switch `:2372-2412` | `valid` stays `true`, `error` stays empty — **no collision or neighbour validation for inherit at all** | **Yes** — the story's highest-value inversion |
| S14 | `read_key_interpolation` `:2564` | trailing `return nullptr` — correct (inherit carries no easing) | **No** |
| S15 | `write_key_interpolation` `:2589` | unreachable: the caller checks `read_key_interpolation` first | **No** |
| S16-S18 | `read_key_curve_mode` `:2624`, `read_key_curve_driver` `:2670` | trailing `return nullptr` — correct | **No** |
| S19-S20 | `write_key_curve_mode` `:2646`, `write_key_curve_driver` `:2692` | unreachable | **No** |
| S21 | `timeline_key_is_managed_loop_boundary` `:3703` | trailing `return false` — correct | **No** |
| S22 | `rename_all_timeline_edits` `:123-130` | the inherit vector keeps the **old** animation name | **Yes** (AC4) |
| S23 | `erase_all_timeline_edits` `:132-139` | the inherit vector keeps a **deleted** animation's edits | **Yes** (AC4) |
| S24 | `auto_extend_explicit_animation_durations` `:2087-2098` | an inherit key past the explicit duration does not grow it | **Yes** (AC3) |
| S25 | `timeline_key_kind_name` (`agent_handlers_editing.cpp:53-62`) | trailing `return "transform"` — **wrong token** in every agent echo | **Yes, on the echoed string** |
| S26 | `timeline_key_curve_value` (`agent_handlers_editing.cpp:~120-155`) | trailing `return json::Value{}` (null) — correct | **No** |
| S27 | `parse_timeline_key_selectors` (`agent_handlers_editing.cpp:~800-890`) | final `else` → `"Unknown timeline <noun> key kind: inherit"` | **Loudly** |
| S28 | `timeline_key_kind_carries_easing` (`timeline_controller.cpp:1238-1250`) | trailing `return false` — correct | **No** |
| S29 | `timeline_key_selector` (`timeline_controller.cpp:891-940`) | falls past every `if`, returns `nullopt` — every shell workflow silently skips inherit keys | **Yes** |

**Eleven of twenty-nine are unobservable.** That is not a defect to fix — every
one of them lands on the correct answer — but it is a fact the plan must state,
because "I added an arm and the tests still pass" is not evidence for those
eleven, and inventing an inversion for them would be exactly the non-biting
theatre `AGENTS.md`'s H4 warns about. §5 lists them as **deliberately
uninverted**, by name.

The plan's Task 1 therefore treats the enum addition as a **mechanical sweep
with a written inventory**, not as something the build will police.

### 1.2 The Inherit lane renders today and is read-only in five separate places

`build_tracks` emits an Inherit `TrackRow` per `bone_inherit_timelines` entry —
`src/editor/timeline_model.cpp:159-166`, id `bone:<index>:Inherit`, label
`Bone / <name> / Inherit`, `transform_channel == nullopt`, `slot_index ==
nullopt`, `kind == TimelineTrackKind::Inherit`
(`src/editor/timeline_model.hpp:29`). `shell_timeline.cpp` iterates rows
generically (`grep -c "TimelineTrackKind::" src/editor/shell_timeline.cpp` → 0),
so the lane and its diamonds draw today.

Read-only is enforced at five independent sites:

| # | Site | What it does with an Inherit row |
| --- | --- | --- |
| G1 | `track_is_editable`, `timeline_model.cpp:240-249` | Requires a transform channel, a deform attachment name, a slot index with `:Color`/`:Attachment` in the id, or one of the two global ids. An Inherit row has none → **false** |
| G2 | `timeline_key_selector`, `timeline_controller.cpp:891-940` | Falls past all six branches → `nullopt`. Every selection-driven workflow (retime, scale, presets, curve mode) silently skips the key |
| G3 | `visit_editable_timeline_keys`, `timeline_controller.cpp:960-1005` | No branch matches → returns `false`. `add_timeline_key_at_playhead` and `remove_selected_timeline_keys` both bail |
| G4 | `visit_existing_project_timeline_keys`, `timeline_controller.cpp:1008-1069` | Same → `false`. The exact-playhead removal path cannot find an authored key |
| G5 | `draw_transform_timeline_editor`, `shell_timeline.cpp:1499-1540` | The dispatcher's five branches (`:1514-1533`) miss, and the transform body's own guard at `:1535-1540` prints **"The selected timeline row is read-only. Keyframe editing is available for rotate, translate, scale, and shear tracks."** |

And two toolbar tooltips say so in as many words —
`shell_timeline.cpp:2277` and `:2296`, both
`"Select an editable track (Inherit remains read-only)"`. Those two string
literals are the cheapest possible Task 0 witness that MAR-185 actually shipped
UI: a `git grep` for them must return **zero** hits at the end of the story.

### 1.3 The animation cascade covers six vectors, and the clipboard covers none

**Project data.** `rename_all_timeline_edits`
(`src/editor/authoring.cpp:123-130`) and `erase_all_timeline_edits` (`:132-139`)
each name the same six vectors. `rename_animation` (`:1909-1941`) calls the
first at `:1936`; `delete_animation` (`:1944-1976`) calls the second at `:1967`.
A seventh line in each is all AC4's "inherit overlays" half needs.

**Preview.** `EditorSession::edit_animation_catalog`
(`src/editor/session.cpp:2365-2470`) already remaps
`PreviewState::animation_name` and `queued_animation_name` on rename and picks a
replacement on delete. It is family-agnostic and needs **no change**;
`animation_timing_equal` already folds `bone_inherit_timelines` into its
comparison (`src/editor/session.cpp:126`), so a preview refresh sees an inherit
timing change the moment one exists. An empty diff over `session.cpp` is the
proof that nothing was needed.

**Key selection.** `apply_animation_catalog_action`
(`src/editor/shell_project_panels.cpp:536-616`) clears
`selected_keys` / `active_key` / `box_selection` at `:607-611` **when
`selected_animation_name` changed**, and `sync_shell_from_editor_session`
(`src/editor/shell_core.cpp:261`, assignment at `:271`) makes a rename of the
selected animation change that name. So a rename or delete of the *selected*
animation already clears the selection; a rename of an *unselected* one leaves
`selected_keys` alone, which is correct because a `KeyRef` carries only
`track_id` + time and track ids do not encode the animation name
(`timeline_model.cpp:96`).

**Clipboard: measured, and it is broken for every family.**
`TimelineClipboard` stores `animation_name`
(`src/editor/timeline_model.hpp:61-66`) and
`clipboard_time_shift` refuses when it differs from the current animation
(`src/editor/timeline_model.cpp:396-407`). Nothing anywhere rewrites that field:

```
$ grep -rn "clipboard" src/editor/shell_project_panels.cpp   → no hits
$ grep -rn "clipboard.animation_name" src/editor/            → the read in
    timeline_model.cpp:400 and the write in
    timeline_controller.cpp:1940 (the copy), and nothing else
```

So today, copying keys and then renaming that animation **silently disables
Paste**: the button greys out (`shell_timeline.cpp:2311-2312`) with no message,
and deleting the animation leaves a clipboard referring to a name that no longer
exists. AC4 names "clipboard references" explicitly, and this cannot be fixed
inherit-only — `animation_name` is one field for all seven families. §2.9 makes
that a deliberate, stated decision rather than accidental scope.

### 1.4 Duration auto-grow covers six vectors

`auto_extend_explicit_animation_durations`
(`src/editor/authoring.cpp:2067-2124`) folds six vectors into `maximum_time` at
`:2087-2098`, using `include_animation_timeline_maximum_excluding_loop_boundaries`
for the three families that carry `loop_sync` and plain
`include_animation_timeline_maximum` (`timeline_model.hpp:298-313`) for the
three that do not. Inherit joins the second group, because
`BoneInheritTimelineEdit` has no `loop_sync` (§0.2).

### 1.5 The registry is 64 and this story makes it 66

Re-measured today, all four ways MAR-184's plan lists:

```
$ grep -c '^\s*{\s*"' src/editor/agent_dispatch.cpp        → 64
$ grep -n "OperationExpectation, 64" src/samples/agent_dispatch_smoke.cpp
                                                            → 39
$ grep -rn "!= 64U" src/ | wc -l                            → 10
$ grep -n "== 64" tools/mcp/test_client.py                  → 53, 55
```

The ten guards are `shell_smoke_constraints.cpp:147,676`,
`shell_smoke_timeline.cpp:3697`, and
`shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603`.

**MAR-185 is the first story in the MAR-177…184 chain that actually moves this
number.** Thirteen sites change 64 → 66. That inverts the count-sweep hazard the
last seven stories have carried: instead of "never substitute 64", the rule
becomes "substitute 64 at exactly these thirteen sites and nowhere else". §11 R6
and the plan's Task 7 own the enumeration; the look-alike literals that must
**not** move are the ones MAR-184's design §1.10 already lists —
`shell_smoke_graph.cpp:2977`'s `4364U`, `shell_smoke_viewport.cpp:3504`'s `640`,
`IM_COL32(56, 61, 69, 255)`, `(51, 56, 64)`, `rgb(54,57,64)`, `"x": 56.0`,
`56,995,840`, `AGENTS.md`'s `t = 0.62`, and `test_client.py:1484`'s ordinal.

### 1.6 The agent's timeline description omits inherit

`timeline_description_value` (`src/editor/agent_dispatch.cpp:996-1036`) reports
`bone_rotate_timelines`, `bone_translate_timelines`, `bone_scale_timelines`,
`bone_shear_timelines`, `slot_attachment_timelines`, `slot_color_timelines`,
`mesh_deform_timelines`, `draw_order_keyframes`, and `event_timeline`. There is
no inherit row. That is the natural, additive place for AC5's "affected-key"
observability and for AC6's agent assertions to read.

`export.preview` (`src/editor/agent_handlers_inspection.cpp:23-42`) reports
**only target paths** — it has no timeline content at all. §2.10 says what AC5's
"export-preview behavior" therefore means and what it does not.

### 1.7 The pruning trap, second order: removal can make a lane vanish

MAR-184 §1.8 records the first-order trap. The second-order form is MAR-185's,
because MAR-185 is the story that *removes* keys:

```cpp
// src/runtime/skeleton_animation.cpp:275-279
erase_matching_timelines(&bone_inherit_timelines, [&](const BoneInheritTimeline& t) {
    return has_single_key_at_origin(t) &&            // :118-120
        t.bone_index < bones.size() &&
        t.keyframes.front().inherit == bones[t.bone_index].inherit;
});
```

run from `src/runtime/skeleton.cpp:116` inside `load_skeleton_data`. Every bone
in `skin_inherit_constraints.mskl` and in `player_idle.mskl` has an **absent**
`inherit` member, i.e. setup `Normal` (measured today, both fixtures). So
reducing an inherit lane to exactly one key at `t = 0` with mode `normal`
deletes the timeline at materialization: the `TrackRow` disappears,
`reconcile_selection` drops the refs, and the lane is unreachable from the
dopesheet — while the project edit still exists on disk.

This hazard is **pre-existing and shared**: the identical rule prunes a rotate
lane whose only key is `{0.0, angle 0}` (`skeleton_animation.cpp:269-274`), and
the shell's own remove path already refuses to go below one key
(`timeline_controller.cpp:1898`). §2.4 decides not to diverge, and §10.3 records
the consequence.

### 1.8 The empty-edit resurrection hazard — the reason removal has a floor

MAR-184 §2.10 makes **both** serializers skip an inherit edit whose `keyframes`
is empty. `build_runtime_document` starts from a **copy** of the base document
(`src/editor/project.cpp:5338`) and *assigns into* it, so an edit that is
skipped leaves the base track in place.

Therefore: emptying `child`'s inherit edit does not clear `child`'s inherit
track — it **restores the imported four-key base track**. A removal that is
allowed to reach zero keys is a removal that silently undoes itself on the next
materialize. §2.4's "the last key cannot be removed" rule is exactly the guard
against this, and §5 I9 is the inversion that proves it.

### 1.9 Where each suite can run

| Binary | Fixture rule |
| --- | --- |
| `marrow_project_smoke` | **One invocation.** Every editing suite lives inside `main()`'s marker-gated `else` (`src/samples/editor_project_smoke.cpp:14062-14206`); pointing the binary at a project over `skin_inherit_constraints.mskl` takes the `markers.present.empty()` skip at `:14062-14072` and runs nothing. Build throwaway projects **inside** the standing `player_idle.marrow` run, as MAR-177/178 do — the proven helper is `mar178_open_skin_session` (`:11763-11790`) |
| `marrow_editor_shell` | **No such gate.** Each scenario in `shell_smoke.cpp:60-150` builds its own `ShellState`; `validate_constraint_lifecycle_shell_smoke` already writes a `.marrow` over `skin_inherit_constraints.mskl` inside itself (`shell_smoke_constraints.cpp:108-131`). A new inherit scenario may do the same |
| `marrow_agent_dispatch_smoke` | Loads `argv[1]`, default `assets/fixtures/player_idle.marrow` (`src/samples/agent_dispatch_smoke.cpp:962-963`). Its inherit cases run against `player_idle`, whose bones have **no** base inherit track — so they exercise the project-only path, and §1.7's trap applies to every key they write |
| `tools/mcp/test_client.py` | Connects to a live `marrow_editor_shell --agent-port` over `player_idle.marrow`. Same constraint |
| `marrow_timeline_model_tests` | The UI-free home for the clipboard-cascade algebra (§2.9) |

### 1.10 Baselines to capture at `a2981b8` (Task 0)

- Registry: 64 / the `std::array<OperationExpectation, 64>` at
  `agent_dispatch_smoke.cpp:39` / 10 `!= 64U` guards / 2 `== 64` assertions in
  `test_client.py`.
- `ctest --test-dir build -N` total. MAR-185 registers **no** new CTest target.
- `serialize_project(load_project("assets/fixtures/player_idle.marrow").project)`
  byte length and SHA-256 — the non-effect witness.
- `git grep -c "Inherit remains read-only" src/` → **2** today, **0** at the end.
- The fixture facts: `skin_inherit_constraints.mskl` carries one inherit
  timeline, `toggle_inherit`/`child`, keys `0.0 normal`,
  `0.25 noRotationOrReflection`, `0.5 onlyTranslation`, `1.0 normal`; all five
  bones omit `inherit`; `player_idle.mskl` has zero inherit timelines and
  sixteen bones that all omit `inherit`.
- **Every row of §0.2's MAR-184 dependency table**, by compiling a two-line
  probe that names each symbol.

---

## 2. Decisions

Every decision below is final for this story. No "TBD".

### 2.1 `TimelineKeyKind::Inherit` is appended **last**

```cpp
enum class TimelineKeyKind {
    Transform, Deform, DrawOrder, Event, SlotColor, SlotAttachment,
    Inherit,     // MAR-185
};
```

Appended, never inserted. `scale_keyframe_times` stores
`static_cast<int>(key->kind)` in a `std::set<std::tuple<int, …>>` identity key
and in its `affected` set (`src/editor/authoring.cpp:2299-2300`, `:2363-2366`),
and `TimelineKeySelector::kind` default-initializes to `Transform`
(`authoring.hpp:118`). Nothing persists the numeric value to disk, so
appending is safe *and* keeps every existing comparison bit-identical, which
keeps the diff reviewable.

`TimelineTrackKind::Inherit` already exists at `timeline_model.hpp:29` and is
**not** moved.

### 2.2 Identity is the time, and AC1's same-time clause is a *proof obligation*

AC1 asks for "stable same-time identities". For inherit that reduces to a
provable invariant rather than a mechanism: **two inherit keys can never share a
time**, because MAR-184's project parser and the runtime parser both reject a
non-increasing pair, and every MAR-185 write path keeps the vector strictly
increasing.

So `TimelineKeySelector` gains **no field**. `same_time_ordinal` stays `0` for
inherit, exactly as it does for the other four non-event families
(`resolve_timeline_key` passes a literal `0U` for all of them,
`src/editor/authoring.cpp:216`, `:230`, `:244`, `:271`, `:288`).

The obligation is discharged by a test, not by an assertion in prose: a merge, a
paste, and a retime that each try to land two inherit keys within `1e-6` are all
rejected by name (§6.2 P4, P8, P6).

### 2.3 Add and Edit are one operation, and the seed mode is **sampled**

`add_timeline_key_at_playhead` (`src/editor/timeline_controller.cpp:1095-1226`)
already implements "Add or replace": for every non-event family it calls
`find_keyframe_near_time` and **overwrites in place** when a key is within
`1e-6`, inserting otherwise (`:1198-1206`). Inherit rides that path unchanged,
so AC1's Add and Edit are the same code with the same 1 ms identity window. The
toolbar tooltip already says "Add or replace a keyframe at the playhead"
(`shell_timeline.cpp:2274`).

**The mode a newly inserted key carries is sampled from the effective animation
at the playhead**, via `AnimationData::sample_bone_inherit`
(`include/marrow/runtime/skeleton.hpp:438`), falling back to the bone's setup
`inherit` when the sample returns `nullptr`. A new
`sample_inherit_keyframe(const ShellState&, const TimelineTrackRow&)` in
`timeline_controller.cpp` joins `sample_draw_order_keyframe` (`:583`),
`sample_event_keyframe` (`:596`) and `sample_deform_keyframe` (`:607`).

**Why sampled and not "setup" and not "Normal".** Every sibling seeds a new key
from what the preview currently shows — `sample_transform_keyframe` reads the
preview skeleton, the slot-color path reads `slot_states()[...].color`
(`timeline_controller.cpp:1177-1181`), the attachment path reads
`slot_states()[...].attachment_name` (`:1184-1190`). Seeding an inherit key from
the setup pose instead would make "add a key in the middle of a stepped lane"
change the pose at that instant, which is the one thing an Add must never do.
On `child` at `t = 0.75`, between the fixture's `0.5 onlyTranslation` and
`1.0 normal`, the sampled seed is **`OnlyTranslation`** and the setup seed would
be **`Normal`** — a two-value difference that §5 I1 turns into a biting
inversion.

### 2.4 The removal primitive, and why it has a floor

MAR-184 deliberately ships no deletion (its §8). MAR-185 adds one, in
`authoring.hpp` beside `merge_inherit_timeline`:

```cpp
struct InheritKeyRemovalResult : AuthoringResult {
    std::size_t removed_key_count{0U};
    std::size_t effective_key_count{0U};   // after the removal
};

/**
 * @brief Atomically removes stepped inherit keys by exact time.
 *
 * Every requested time must resolve, within 1e-6, to a key of the effective
 * timeline; a time that matches none rejects the whole call. The last key of a
 * lane cannot be removed: an inherit edit with zero keyframes is skipped by
 * both serializers, and `build_runtime_document` assigns into a COPY of the base
 * document, so an emptied edit silently restores the imported base track
 * instead of clearing it. Preflight-then-mutate: a rejection leaves
 * `serialize_project()` byte-identical.
 */
InheritKeyRemovalResult remove_inherit_timeline_keys(
    ProjectData* project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name,
    std::string_view bone_name,
    const std::vector<double>& times);
```

Validation order — everything before step 7 touches nothing:

1. `project == nullptr` → `"Timeline authoring requires an open project."`
   (the shared wording of `retime_keyframes`, `authoring.cpp:2133`).
2. `times.empty()` → `"inherit removal requires at least one key time"`.
3. `effective_skeleton.find_animation(animation_name) == nullptr` →
   `"animation '<name>' does not exist"`.
4. `find_bone_index(bone_name)` empty → `"bone '<name>' does not exist"`.
5. Each time non-finite or negative →
   `"key time must be finite and non-negative"`.
6. Two requested times within `1e-6` of each other →
   `"requested removals collide at time <t>"` (checked on a sorted copy, so the
   message is order-independent — the same discipline as MAR-184 §2.6 step 6).
7. Resolve every time against the **effective** timeline (the project edit if
   one exists, otherwise the base track). A time matching none →
   `"no inherit key exists at time <t> on bone '<bone>'"`.
8. If the resolved set would leave **zero** keys →
   `"an inherit timeline must keep at least one key; removing every key would
   restore the imported track for bone '<bone>'"`.

Then, and only then: `ProjectData candidate = *project;`
→ `ensure_bone_inherit_timeline_edit(candidate, …)`
→ erase the resolved indices, descending
→ counts → `*project = std::move(candidate);`.

**Preflight-then-mutate is mandatory**, and MAR-184's §A5 lesson applies
verbatim: with a candidate copy, "mutate early" is invisible to
`serialize_project(*project)`, so the inversion that proves atomicity is
*removing the candidate*, not moving a check. §5 I8 is written that way.

**`ensure_` on a bone whose edit already exists is a silent no-op**, so the case
that proves I8 must run on a bone with **no project edit yet** — the same
load-bearing choice MAR-184's plan makes for its P11 (its R6). §6.2 P5 states
this explicitly.

### 2.5 What inherit does **not** participate in, and where each exclusion lives

| Concern | Excluded because | Site |
| --- | --- | --- |
| Easing / `curve` | `InheritKeyframeEdit` has no `interpolation` member (MAR-184 §3.1) | `read_key_interpolation` returns `nullptr` (`authoring.cpp:2564`) → `set_keyframe_interpolation` rejects |
| Curve mode / driver | no `curve_mode` / `curve_driver` member | `read_key_curve_mode` `:2624`, `read_key_curve_driver` `:2670` |
| MAR-170 curve presets | `timeline_key_kind_carries_easing` → false | `timeline_controller.cpp:1238-1250`; `collect_curve_preset_selectors` skips it at `:1287` |
| MAR-171 automatic curves | no easing to resolve | `resolve_automatic_curves` walks only the families with `curve_mode` |
| MAR-172 loop sync | no `loop_sync` member; the three continuous families own it | `TimelineLaneKind` (`authoring.hpp:376`) is unchanged — **inherit adds no lane kind** |
| Scalar offset (MAR-168) | `family_owns_scalar_component` → false | `authoring.cpp:337-366` |
| MAR-167 scalar graph | `BoneInherit` is an unordered enum with no numeric axis | `timeline_graph_model.cpp:393`, `:478` — **unchanged; both files appear in Task 8's empty-diff proof** |

All seven exclusions are already *correct by fall-through* (§1.1 S2-S6, S14-S21,
S28). The arms are added anyway, for the reader; the plan is explicit that adding
them is not falsifiable and says so rather than inventing a case.

The clipboard's `loop_sync` scrub loop (`timeline_controller.cpp:2028-2037`)
needs **no seventh entry** for the same reason. That is a decision, recorded
here so a later reader does not "fix" its absence.

### 2.6 Retime, scale, and what "atomic collision and neighbor validation" means

AC2 asks for inherit lanes in "shared key selection, retime, positive time
scaling, and typed clipboard workflows with atomic collision and neighbor
validation". Concretely:

- **Spacing** is `kNonEventKeySpacing` = 1 ms (`authoring.cpp:150`), the same as
  every non-event family. Inherit keys are strictly increasing, so a zero
  spacing would permit a collision the parser then rejects on save.
- **Retime** clamps. `include_resolved_retime_bounds` gains an Inherit arm
  calling `include_timeline_retime_bounds` with
  `family_key_spacing(TimelineKeyKind::Inherit)`, and **no**
  `include_loop_boundary_retime_pins` call, because inherit lanes carry no
  opt-in. `apply_resolved_retime` and `sort_retimed_timelines` gain their arms.
- **Scale rejects** rather than clamping — `scale_keyframe_times`'s contract
  (`authoring.hpp:540-563`). The Inherit arm goes into the validate switch at
  `authoring.cpp:2372-2412`, calling `validate_projected_scale` over
  `candidate.bone_inherit_timeline_edits[timeline_index]`. Without it the
  switch leaves `valid = true` and inherit keys scale through each other with no
  diagnostic (§1.1 S13).
- **`scale_lane_label`** gains `"inherit key '<bone>'"`, mirroring
  `"transform key '<bone>/<channel>'"` (`authoring.cpp:643-646`). Every scale
  rejection message names the lane, and a missing arm degrades it to the generic
  `"timeline key"`.
- **Positive** in AC2's "positive time scaling" is already the primitive's
  contract: `scale <= 0.0` is rejected at `authoring.cpp:2273-2275`. Nothing
  inherit-specific is needed and nothing is added.

### 2.7 Clipboard: copy, paste, and the remap rule

`copy_selected_timeline_keys` (`timeline_controller.cpp:1932-2048`) gains a
seventh `else if` branch, keyed on `track.kind == TimelineTrackKind::Inherit`,
reading `find_bone_inherit_timeline_edit` first and falling back to a
runtime-derived `make_bone_inherit_timeline_edit(*state, track)` — the seventh
`make_*` helper, modelled on `make_slot_attachment_timeline_edit`
(`timeline_controller.cpp:476-503`). It feeds
`append_selected_timeline_fragment` (`timeline_model.hpp:230-248`), which is
already generic.

`paste_timeline_clipboard` (`:2050-2238`) gains a seventh loop. The remap rule
matches its siblings: when the clipboard holds exactly one track and an editable
row is selected, paste into that row; otherwise find the row for the **same bone
name** in the current animation. `paste_keys_replace_collisions`
(`timeline_model.hpp:250-297`) is called with
`retain_same_time_source_order = false` — the same argument every non-event
family passes — which collapses a pasted key onto an existing one within `1e-6`
and keeps the vector sorted, i.e. it *is* the collision rule (§2.2's obligation).

`ensure_bone_inherit_timeline_edit_index(ShellState*, const TimelineTrackRow&)`
joins the six existing `ensure_*_timeline_edit_index` helpers
(`timeline_controller.cpp:280`, `:303`, `:374`, `:430`, `:504`, `:524`).

### 2.8 Duration auto-grow

One line in `auto_extend_explicit_animation_durations`
(`authoring.cpp:2087-2098`):

```cpp
include_animation_timeline_maximum(
    candidate.bone_inherit_timeline_edits, animation.name, &maximum_time);
```

Plain, not the loop-boundary-excluding variant, because inherit lanes carry no
`loop_sync` and therefore no managed boundary key to exclude.

### 2.9 The cascade, including the clipboard fix that is not inherit-specific

Three pieces:

1. **Overlays.** A seventh line in `rename_all_timeline_edits` and in
   `erase_all_timeline_edits` (`authoring.cpp:123-139`). Both are already
   inside `rename_animation` / `delete_animation`'s validate-then-write body,
   so atomicity is inherited.
2. **Preview.** Nothing. `session.cpp` is family-agnostic (§1.3) and appears in
   the empty-diff proof.
3. **Selection and clipboard.** A new UI-free pair in `timeline_model`:

   ```cpp
   /** @brief Remaps a clipboard's animation reference, or clears it on delete. */
   void cascade_animation_rename(Clipboard* clipboard,
                                 std::string_view from, std::string_view to);
   void cascade_animation_delete(Clipboard* clipboard,
                                 std::string_view animation_name);
   ```

   unit-tested in `marrow_timeline_model_tests`, called from
   `apply_animation_catalog_action` (`shell_project_panels.cpp:606-614`)
   immediately after `sync_shell_from_editor_session`.

**This fixes all seven families at once, deliberately.**
`Clipboard::animation_name` is one field; there is no inherit-only version of
this bug. AC4 names "clipboard references" and §1.3 measured that no family
cascades them today, so the choice is between fixing it generically and not
satisfying AC4. Recorded as a *decision* so review does not read it as scope
leak.

**Explicit limitation, stated now rather than discovered later:** an
**agent-driven** `animation.rename` does not reach `ShellState` — the agent
handler mutates the session, and the shell observes it through
`sync_shell_from_editor_session_if_revised` (`shell_core.cpp:301`), which cannot
tell a rename from a selection change. So an agent rename still leaves the GUI
clipboard stale. Closing that needs a rename-aware signal on the session, which
is a different story. §10.5 records it; §2.9's clause is GUI-scoped and AC4's
clipboard clause is inherently GUI-scoped, because `TimelineClipboard` lives in
`ShellState` and the agent has no clipboard at all.

### 2.10 The two agent operations

Registry rows, inserted **immediately after** `remove_attachment_keyframe`
(`agent_dispatch.cpp:85`) so the neighbourhood stays the slot/discrete-keyframe
block:

```cpp
{"set_inherit_keyframe",    "edit", true, false, true,  true, &handle_editing_operation},
{"remove_inherit_keyframe", "edit", true, false, false, true, &handle_editing_operation},
```

The `dry_run_supported` flags mirror the family exactly: every `set_*_keyframe`
supports dry-run, every `remove_*_keyframe` does not
(`agent_dispatch.cpp:67-87`). The dispatcher **enforces** this — a `dry_run` on
an op that does not support it is rejected with `dry_run_unsupported`
(`agent_dispatch.cpp:1075-1081`) — so the flags are behaviour, not
documentation.

Arguments:

| Op | Required | Optional |
| --- | --- | --- |
| `set_inherit_keyframe` | `animation` (string), `bone` (string), `time` (number), `inherit` (string, one of the five tokens) | `dry_run` (bool) |
| `remove_inherit_keyframe` | `animation` (string), `bone` (string), `time` (number) | — |

Both delegate to the primitives — `set_inherit_keyframe` to
`merge_inherit_timeline` with a single-key request and
`replace_existing_times = true`, `remove_inherit_keyframe` to
`remove_inherit_timeline_keys` with a single time — so the agent, the GUI and
any future caller cannot disagree about validation. That is the same discipline
`timeline.set_loop_sync` uses via `timeline_key_is_managed_loop_boundary`
(`authoring.hpp:484-487`).

**Affected-key payload.** Both echo an `affected_keys` array of
`{time, inherit}` objects describing the lane's effective keys after the
operation (or, on a dry run, after the operation *would* run), plus
`added_key_count` / `replaced_key_count` / `removed_key_count` and
`effective_key_count` from the primitive's result. The precedent is
`set_vertex_weights`' `affected_vertices` (`agent_handlers_editing.cpp:2698-2703`).

**"Export-preview behavior" (AC5), precisely.** `export.preview` reports target
paths only (§1.6) and has no per-family behaviour to add — so the criterion
cannot mean "export.preview grows an inherit field". It means the two operations
must leave the project in a state the export path accepts. The testable form,
and the one §6.4 uses:

- `runtime.validate` (which runs the real `build_project_runtime`,
  `agent_handlers_inspection.cpp:58-60`) passes after each operation;
- `timeline.describe` reports the new `bone_inherit_timelines` count (§2.11);
- `export.preview` still succeeds and names the same targets, byte for byte, as
  before the edit — a **non-effect** assertion, which is the honest one;
- `compare_runtime_export` / `marrow_inspect --compare` still reports a match.

§10.6 records that the third bullet is a non-effect witness with no story-owned
inversion, rather than pretending otherwise.

### 2.11 `timeline.describe` gains one member

`object.emplace("bone_inherit_timelines", number_value(animation->bone_inherit_timelines.size()));`
in `timeline_description_value` (`agent_dispatch.cpp:1020-1026`), beside the
other bone-timeline counts. Purely additive: every shipped member keeps its
name, type, and position, which is the same rule MAR-175 followed when it added
`weights` to `mesh.describe` (`agent_handlers_inspection.cpp:314-317`).

Note the count is read from the **materialized** `AnimationData`, so it is
subject to §1.7's pruning — a fact the agent tests must respect and §6.4 P-cases
encode by never writing a lone `{0.0, normal}`.

### 2.12 Formats and versions do not move

`.mskl` stays 1, `.mbin` stays 2 (`src/runtime/binary.cpp:22-23`), the C ABI is
untouched, and `.marrow` gains **no new key** — MAR-184 already defined
`timeline_edits.animations.<a>.bones.<b>.inherit`, and MAR-185 writes exactly
that shape. `include/marrow/c/`, `src/c/`, `include/marrow/runtime/`, and
`src/runtime/` all appear in Task 8's empty-diff proof.

### 2.13 The UI, and the one place a UI-free test cannot reach

Five edits:

1. `track_is_editable` (`timeline_model.cpp:240-249`) gains
   `|| track.kind == TimelineTrackKind::Inherit`. Keyed on `kind`, not on an id
   substring — `bone:12:Inherit` contains neither `:Color` nor `:Attachment`
   today, but an id-substring rule is the kind of thing that breaks when a bone
   is named `Attachment`.
2. `timeline_key_selector` (`timeline_controller.cpp:891-940`) gains an Inherit
   branch **before** the trailing slot branches, keyed on
   `track.kind == TimelineTrackKind::Inherit && track.bone_index.has_value()`.
3. `visit_editable_timeline_keys` and `visit_existing_project_timeline_keys`
   (`:960`, `:1008`) each gain a branch.
4. `draw_inherit_timeline_editor(ShellState*, const TimelineTrackRow&)` in
   `shell_timeline.cpp`, modelled on `draw_slot_attachment_timeline_editor`
   (`:1405-1497`) — the closest sibling, being stepped, discrete and
   easing-free. Per key: a `Time` `DragScalar` and a **`Mode` combo** over the
   five tokens. Dispatched from `draw_transform_timeline_editor`'s branch chain
   at `:1514-1533`, which today falls through to the transform body's
   read-only message at `:1535-1540`.
5. The two tooltips at `shell_timeline.cpp:2277` and `:2296` lose their
   "(Inherit remains read-only)" clause.

**The frame-smoke obligation.** `AGENTS.md`'s *Headless Frame Smoke Notes*
records that "a UI-free helper cannot observe a deleted widget" — calling the
function a button calls asserts the handler, not the button, and such a test
passes unchanged after the widget is removed. MAR-184 could answer "the question
does not arise" because it shipped no widget. **MAR-185 ships a widget**, so the
`Mode` combo must be located by a real mouse in a real frame, in
`shell_smoke_frames.cpp`, which already renders `draw_timeline_window` and
sweeps for `HoveredId` (`:71`, `:290-315`, `:1296`, `:1420`). The mechanics that
apply, from that same section:

- Sweep at least one control that already exists in the same window, so a broken
  `FindWindowByName(...)->GetID(label)` seed cannot be mistaken for a missing
  widget.
- The combo must be emitted at plain window scope — inside a `CollapsingHeader`
  under a `BeginChild` that is **closed before** the sweep, matching the shape
  the constraint panels already rely on.
- Advance `io.DeltaTime` past `io.MouseDoubleClickTime` between two gestures at
  the same pixel, or the second is a double click.

§6.5 is the scenario; §5 I15 is its inversion (delete the combo's emission; the
UI-free cases stay green and only the frame case fails).

---

## 3. Where each piece lands

No new translation unit, therefore **no `CMakeLists.txt` change**.

### 3.1 `include/marrow/editor/authoring.hpp`

- `TimelineKeyKind::Inherit`, appended (`:102-109`).
- `InheritKeyRemovalResult` and `remove_inherit_timeline_keys`, declared after
  MAR-184's `merge_inherit_timeline`.
- No change to `TimelineKeySelector`, `TimelineLaneKind`, or
  `TimelineScalePivot`.

### 3.2 `src/editor/authoring.cpp`

Twenty-one edits, every one from §1.1's table: the S1-S21 arms, the S22-S24
seventh lines, and `remove_inherit_timeline_keys`' definition beside
`merge_inherit_timeline`.

### 3.3 `src/editor/timeline_model.{hpp,cpp}`

- `track_is_editable` gains the Inherit disjunct (`:240-249`).
- `cascade_animation_rename` / `cascade_animation_delete` over `Clipboard`,
  declared beside `clipboard_time_shift` (`timeline_model.hpp:108-111`).

### 3.4 `src/editor/timeline_controller.{hpp,cpp}`

- `timeline_key_selector` branch (`:891-940`).
- `visit_editable_timeline_keys` / `visit_existing_project_timeline_keys`
  branches (`:960`, `:1008`).
- `make_bone_inherit_timeline_edit`, `ensure_bone_inherit_timeline_edit_index`,
  `sample_inherit_keyframe` — the seventh member of three existing families.
- `copy_selected_timeline_keys` and `paste_timeline_clipboard` branches
  (`:1932`, `:2050`).
- `timeline_key_kind_carries_easing` arm (`:1238`) — correct by fall-through,
  added for the reader.

### 3.5 `src/editor/shell_timeline.cpp`, `src/editor/shell_project_panels.cpp`

- `draw_inherit_timeline_editor` + its dispatcher branch.
- The two tooltip strings.
- The clipboard cascade call in `apply_animation_catalog_action` (`:606-614`).

### 3.6 `src/editor/agent_dispatch.cpp`, `src/editor/agent_handlers_editing.cpp`

- Two registry rows (`agent_dispatch.cpp:85`+).
- `bone_inherit_timelines` in `timeline_description_value` (`:1020`+).
- `timeline_key_kind_name` arm (`agent_handlers_editing.cpp:53-62`).
- `parse_timeline_key_selectors` `"inherit"` branch, requiring `bone`.
- The two handlers, beside the slot-attachment pair
  (`agent_handlers_editing.cpp:2882-3040`).

### 3.7 `src/samples/agent_dispatch_smoke.cpp`, `tools/mcp/`

- Two `OperationExpectation` rows; `std::array<…, 64>` → `66` at `:39`.
- Ten `!= 64U` → `!= 66U`.
- `tools/mcp/tools/editing.py`: two `types.Tool` entries beside
  `remove_attachment_keyframe` (`:1207-1234`).
- `tools/mcp/test_client.py:53,55`: `== 64` → `== 66`.

### 3.8 Tests

- `src/tests/timeline_model_tests.cpp` — the clipboard cascade algebra.
- `src/samples/editor_project_smoke.cpp` — `validate_mar185_inherit_editing`,
  registered in the marker-gated `else` after
  `validate_mar180_lifecycle_refuses_active_transaction` (`:14204-14206`).
- `src/editor/shell_smoke_timeline.cpp` — the shell scenario.
- `src/editor/shell_smoke_frames.cpp` — the real-mouse combo case.
- `src/samples/agent_dispatch_smoke.cpp` — the agent cases.

### 3.9 Documentation

- `docs/root1/format-spec.md` — MAR-184 adds the `inherit` subsection; MAR-185
  extends it with the editing rules (removal floor, spacing, no easing).
- `AGENTS.md` — a `## MAR-185 … Validation Results` section, and the
  `## Current Validation` shell line gains the new scenario.
- `.agents/tasks/prd-marrow-runtime.json` — `MAR-185` → `done`.

---

## 4. Acceptance criteria → artifacts

| AC | Delivered by | Proved by |
| --- | --- | --- |
| 1 — Add / Edit / exact-playhead Remove, stable same-time identity | §2.2, §2.3, §2.4; `add_timeline_key_at_playhead`'s replace path; `remove_inherit_timeline_keys` | P1, P2, P3, P4, S1, S2 |
| 2 — selection, retime, positive scaling, typed clipboard, atomic collision + neighbour validation | §2.1, §2.6, §2.7 | P6, P7, P8, S3, S4 |
| 3 — duration auto-grow, transaction primitives, rejection leaves project/preview/selection/history unchanged | §2.4, §2.8; `finish_timeline_transaction` (`timeline_controller.cpp:1071-1093`) | P5, P9, S5 |
| 4 — rename/delete cascade overlays, selections, clipboard, preview | §2.9 | U1, U2, P10, S6 |
| 5 — two ops with registry + MCP parity, dry-run, validation, affected keys, mutation, undo, export preview | §2.10, §2.11 | A1-A6, M1 |
| 6 — project, shell, agent, MCP tests over five modes, add/edit/remove, selection, cascade, undo/redo, save/reload, JSON/MBIN | §6 | every case, plus `marrow_inspect --compare` |

---

## 5. Inversions, and the mechanism that makes each observable

The rule this story is held to: **an inversion is valid only if the case it is
attributed to is the *first* thing that catches it**, and the attribution is
established by *running* it, not by reading the source. Where a short-circuit
could hide a later detector, the plan requires neutering the earlier call
(`if (false && …)`) and re-running.

The full register with predicted failure text lives in the plan's §B. This
section states the **mechanism** for each — the layer the assertion runs at and
the reason the inverted code is reachable from it.

| # | Inversion | Attributed to | Mechanism |
| --- | --- | --- | --- |
| I1 | `sample_inherit_keyframe` seeds from the bone's setup `inherit` instead of `sample_bone_inherit` | S1 | The case adds a key at `t = 0.75` on `child`, between `0.5 onlyTranslation` and `1.0 normal`, and asserts the stored mode is `OnlyTranslation`. Setup is `Normal` for every bone of the fixture (measured), so the two seeds differ by construction |
| I2 | `track_is_editable` drops the Inherit disjunct | P1 | Every shell path gates on it (`visit_editable_timeline_keys:963`, the toolbar at `shell_timeline.cpp:2271`), so Add fails with "The selected timeline is read-only" |
| I3 | `timeline_key_selector` drops the Inherit branch | P6 | `track_is_editable` is already true under I3, so the row *looks* editable; the retime silently resolves zero selectors. The case asserts `key_count == 1`, not merely that the call returned |
| I4 | Delete the Inherit arm from `scale_keyframe_times`'s validate switch (`authoring.cpp:2372-2412`) | P8 | `valid` stays `true` and `error` stays empty — no diagnostic anywhere — so a ratio that squeezes two inherit keys to 0.4 ms apart is **accepted**. The case asserts the rejection, not the absence of a crash |
| I5 | Delete the Inherit arm from `scale_lane_label` | P8 | Same case, a **different assertion**: the rejection message degrades from `inherit key 'child'` to `timeline key`. Recorded as a second, independently asserted mutation on one case, not as a duplicate |
| I6 | Delete the Inherit arm from `include_resolved_retime_bounds` | P7 | `void`, no trailing statement → the shared delta is unbounded by inherit neighbours, so a requested +0.4 s is applied in full instead of clamped to the 1 ms floor before the next key. The case asserts `applied_delta`, which is the only observable that differs |
| I7 | Delete the Inherit arm from `apply_resolved_retime` | P6 | `void`, no trailing statement → the retime reports success and moves nothing. The case asserts the **stored** time after a reload, not the returned `applied_delta` |
| I8 | Remove the candidate copy from `remove_inherit_timeline_keys` and call `ensure_bone_inherit_timeline_edit(*project, …)` before step 7 | P5 | The case removes a time that does not exist, on a bone (`child`) with a base track and **no project edit yet**, and requires `serialize_project` byte-identical across the rejection. Under the mutation the materialized 4-key edit is left behind. MAR-184 §A5's redefinition: with the candidate present, "mutate early" is invisible |
| I9 | Delete the "last key" floor (step 8) | P3 | Removing the only key empties the edit; both serializers skip an empty edit (MAR-184 §2.10) and `build_runtime_document` assigns into a copy of the base, so the exported `.mskl` comes back carrying `child`'s **imported four keys**. The case re-parses the export and asserts the key count |
| I10 | Drop the seventh line from `rename_all_timeline_edits` | U1 | The project's inherit edit keeps the old animation name; the reloaded project's edit does not match the renamed animation |
| I11 | Drop the seventh line from `erase_all_timeline_edits` | U2 | The deleted animation's inherit edit survives; on reload `build_runtime_document` re-creates the animation from the orphan edit, so the animation count differs |
| I12 | Make `cascade_animation_rename` a no-op | U3 | A `marrow_timeline_model_tests` case over the pure `Clipboard`, so no session, no frame, and no shell state can stand in for it |
| I13 | Delete the Inherit arm from `resolved_stored_key_time` | P8 | The scale then reads the **selector's** time — narrowed to float32 by the track rows — instead of the stored `double`. Observable only with a stored time that is not float32-exact; the case uses `0.1` on a lane whose other keys force a ratio, and asserts the post-scale time to `1e-12`. **If Task 0's probe shows the difference is below that threshold, this inversion is recorded as non-reproducing rather than converted into a convenient bite** |
| I14 | Drop the seventh `include_animation_timeline_maximum` call | P9 | An inherit key past the explicit duration no longer grows it; the case reads the duration after the auto-grow call |
| I15 | Delete the `Mode` combo's emission from `draw_inherit_timeline_editor` | F1 | The UI-free cases call the controller, not the widget, and stay green — exactly the failure mode `AGENTS.md` recorded when MAR-178's own scenario passed while the frame smoke failed. Only the mouse sweep sees it |
| I16 | `timeline_key_kind_name` loses its Inherit arm | A2 | Falls through to `"transform"`; the agent echo's `kind` field is asserted by string |
| I17 | `parse_timeline_key_selectors` loses its `"inherit"` branch | A5 | The final `else` rejects with `Unknown timeline key kind: inherit`, so an agent retime of an inherit key fails by name |
| I18 | Registry row for `remove_inherit_keyframe` given `dry_run_supported = true` | A4 | The dispatcher's `dry_run_unsupported` guard (`agent_dispatch.cpp:1075-1081`) stops rejecting, so a `dry_run` removal **mutates**. The case asserts the rejection *and* that the key survived |

### 5.1 Deliberately uninverted, by name

These are the eleven §1.1 sites whose omission lands on the correct answer, plus
two witnesses that are non-effect by construction. **No inversion is invented
for any of them**, and `AGENTS.md` must say so verbatim:

- S2-S6, S14-S21, S28 — `family_owns_scalar_component`, `read_scalar_component`,
  `write_scalar_component`, `family_key_spacing`, `resolved_key_is_loop_pinned`,
  `read_key_interpolation`, `write_key_interpolation`, `read_key_curve_mode`,
  `write_key_curve_mode`, `read_key_curve_driver`, `write_key_curve_driver`,
  `timeline_key_is_managed_loop_boundary`, `timeline_key_kind_carries_easing`.
  Each falls through to the correct value or is unreachable.
- S12 — `sort_retimed_timelines`' seventh call. **Provably** a no-op: a retime's
  shared clamped delta preserves index order, and the scale's own comment says
  its sort is "defence in depth, and provably a no-op" (`authoring.cpp:2434-2435`).
- The `export.preview` non-effect assertion in A6 (§2.10).
- `serialize_project(player_idle.marrow)`'s unchanged bytes (the P0 witness).

---

## 6. Test surfaces

### 6.1 Which binaries

| Binary | Role |
| --- | --- |
| `marrow_timeline_model_tests` | **new coverage.** The clipboard cascade algebra (U3) |
| `marrow_project_smoke` | **primary.** P0-P10, U1, U2, inside the single `player_idle.marrow` invocation (§1.9) |
| `marrow_editor_shell --auto-close 2` | S1-S6 (the controller scenario) and F1 (the real-mouse frame case) |
| `marrow_agent_dispatch_smoke` | A1-A6 |
| `tools/mcp/test_client.py` | M1 — registry/MCP parity at 66 |
| `marrow_inspect --compare` | AC5's JSON/binary equivalence |
| `marrow_fixture_smoke`, `marrow_timeline_graph_model_tests`, `marrow_selection_tests`, `marrow_preference_tests` | regression witnesses; all must pass **unchanged** |

### 6.2 Project cases — `validate_mar185_inherit_editing`

Throwaway projects over `assets/fixtures/skin_inherit_constraints.mskl`,
borrowing `assets/fixtures/player_idle.matl`, built with the
`mar178_open_skin_session` pattern (`editor_project_smoke.cpp:11763-11790`).
Base animation `toggle_inherit`; base-backed bone `child` (4 keys); bone with no
base track `controller`. **Every project-only overlay is `{0.0, NoScale}` +
`{0.4, Normal}`**, pruning-safe per §1.7.

| # | Case | Asserts |
| --- | --- | --- |
| P0 | `load_project(player_idle.marrow)` | `bone_inherit_timeline_edits.empty()` and `serialize_project` byte-identical to Task 0's length + SHA. **Non-effect witness; no inversion** |
| P1 | Add at the playhead on `child` via the controller-free primitive path used by the shell | The key lands, the lane is editable, and `save_project` → **`load_project(path)`** round-trips it. *(I2)* |
| P2 | Author all five modes as five keys on `controller`, save → load → `build_project_runtime` | All five survive as the matching `runtime::BoneInherit`, in order. AC6's "all five modes" |
| P3 | Remove keys on `child` down to one, then attempt the last | The first three succeed; the fourth is rejected naming the remedy; export re-parses with **one** key, not the base four. *(I9)* |
| P4 | `merge_inherit_timeline` two keys within `1e-6` in one request; then a paste that lands on an existing time | The first rejects by name; the second collapses to one key and the vector stays strictly increasing. AC1's same-time obligation (§2.2) |
| P5 | `remove_inherit_timeline_keys` for a time that does not exist, on `child`, **with no project edit yet**, `serialize_project` captured before and after | Error names the time and the bone; `changed == false`; strings byte-identical. *(I8 — the `no project edit yet` clause is load-bearing: with an edit present `ensure` is a no-op and I8 cannot bite)* |
| P6 | `retime_keyframes` one inherit key by a delta with room | `key_count == 1`, `applied_delta` equals the request, and the **stored** time after `save` → `load_project` moved. *(I3, I7)* |
| P7 | `retime_keyframes` a key by a delta that would cross its unselected neighbour | `applied_delta` is clamped to `neighbour - 1 ms - original`, not the request. *(I6)* |
| P8 | `scale_keyframe_times` over `child`'s four keys with a ratio that squeezes two below 1 ms | Rejected; the message contains `inherit key 'child'` **and** the projected separation. Then a legal ratio succeeds and the post-scale times match `pivot + (t - pivot) * s` to `1e-12`. *(I4 on the rejection, I5 on the label, I13 on the precision)* |
| P9 | An inherit key past an explicit duration, then `auto_extend_explicit_animation_durations` | The duration grew to the key's time, normalized through float32. *(I14)* |
| P10 | Export a project whose only overlay is inherit to `.mskl` **and** `.mbin`, `load_skeleton_data` on each | Both carry the same keys (float32 tolerance) and identical modes; `.mskl` version 1, `.mbin` version 2 |
| U1 | `rename_animation` over a project with an inherit overlay, then `save` → `load_project` | The reloaded edit names the **new** animation and the materialized skeleton carries the timeline. *(I10)* |
| U2 | `delete_animation` over the same | The inherit edit is gone and the reloaded animation catalog does not contain the deleted name. *(I11)* |

**Every rejection case asserts the message text, never merely `!result`.**
`load_project` and the primitives fail for many reasons; a bare `!result` passes
under its own inversion. This is MAR-184's plan R4, carried forward, and it is
why P3, P4, P5 and P8 all name their expected substrings.

### 6.3 Unit case — `marrow_timeline_model_tests`

| # | Case | Asserts |
| --- | --- | --- |
| U3 | `cascade_animation_rename` / `cascade_animation_delete` over a `Clipboard` | Rename remaps `animation_name` and leaves `earliest_time`, `has_data` and the fragment untouched; a rename of a *different* animation changes nothing; delete clears `has_data` and the fragment. Then `clipboard_time_shift` returns a value against the new name. *(I12)* |

Pure, UI-free, no session — so nothing in the shell can stand in for it.

### 6.4 Shell cases — `shell_smoke_timeline.cpp`

A new `validate_inherit_editing_shell_smoke(const std::filesystem::path&)` that
builds its own project over `skin_inherit_constraints.mskl` (§1.9), registered
in `shell_smoke.cpp` beside `validate_timeline_scale_shell_smoke` (`:124-127`).

| # | Case | Asserts |
| --- | --- | --- |
| S1 | `add_timeline_key_at_playhead` on `bone:2:Inherit` with the playhead at `0.75` | One history entry; the new key's mode is `OnlyTranslation`, sampled not setup. *(I1)* |
| S2 | The same call with the playhead exactly on the `0.5` key | The key count is unchanged and the mode was replaced in place — Add **is** Edit (§2.3) |
| S3 | Select two inherit keys, `copy_selected_timeline_keys`, move the playhead, `paste_timeline_clipboard` | The pasted pair lands at the shifted times and the vector stays strictly increasing |
| S4 | `remove_selected_timeline_keys` with no selection and the playhead exactly on a key | The exact-playhead removal path fires; with the playhead 5 ms off, the status message is `"No authored key exists at the playhead"` |
| S5 | A retime gesture that a `scale_keyframe_times`-shaped rejection would kill, then cancel | Project, preview animation name, `selected_keys`, `active_key`, and `undo_count` are all bit-identical to before the gesture. AC3's "rejection leaves project, preview, selection, and history unchanged" |
| S6 | GUI `apply_animation_catalog_action(Rename)` after a copy | The clipboard's `animation_name` follows the rename and Paste is enabled again. *(I12's shell-side companion; U3 is the first detector)* |

### 6.5 Frame case — `shell_smoke_frames.cpp`

| # | Case | Asserts |
| --- | --- | --- |
| F1 | Select the Inherit row, render a frame, sweep the mouse across the editor panel comparing `HoveredId` against `FindWindowByName(kTimelineWindowTitle)->GetID("Mode")` | The combo is **on screen and hoverable**; a control that already exists (the `Time` drag of the same key) is swept in the same pass so a broken seed cannot masquerade as a missing widget. Then the combo is opened and an item clicked, and the stored mode changes. *(I15)* |

### 6.6 Agent cases — `agent_dispatch_smoke.cpp`

Against `player_idle.marrow`, bone `spine`, animation `idle`. **No case writes a
lone `{0.0, "normal"}` key**, per §1.7.

| # | Case | Asserts |
| --- | --- | --- |
| A1 | Registry integrity | `agent_operation_descriptor_count() == 66`; both new names present with `category == "edit"`, `mutating == true`; every descriptor has a handler |
| A2 | `set_inherit_keyframe` `{animation: idle, bone: spine, time: 0.25, inherit: "noScale"}` | `ok`, the echoed `kind` is `"inherit"`, `affected_keys` carries `{0.25, "noScale"}`, and `timeline.describe` reports `bone_inherit_timelines == 1`. *(I16)* |
| A3 | `set_inherit_keyframe` with `dry_run: true` and a different mode | `ok`, `dry_run: true`, `affected_keys` shows the *would-be* result, and a following `timeline.describe` shows the **unchanged** stored mode |
| A4 | `remove_inherit_keyframe` with `dry_run: true` | Rejected with `error.code == "dry_run_unsupported"`, and the key still exists. *(I18)* |
| A5 | `timeline.retime_keyframes` with `keys: [{kind: "inherit", bone: "spine", time: 0.25}]` | `ok` and the key moved. *(I17)* |
| A6 | `set_inherit_keyframe` → `undo` → `redo`; `runtime.validate` and `export.preview` after each | `runtime.validate` passes throughout; `export.preview`'s `targets` array is byte-identical before and after (§2.10's non-effect witness); `undo` restores the previous key count |
| A7 | `set_inherit_keyframe` with `inherit: "noScales"`, then with an unknown bone, then with `time: -1` | Three distinct errors, each naming the offending value; `project.diagnostics`' `project_dirty` unchanged |

### 6.7 MCP case

| # | Case | Asserts |
| --- | --- | --- |
| M1 | `tools/mcp/test_client.py` | `len(registry_names) == 66`, `len(mcp_names) == 66`, the two sets equal, and both new tools carry `animation`/`bone`/`time` in `required` — plus `inherit` for the setter and `dry_run` in `properties` for the setter only |

---

## 7. Verification commands

From `AGENTS.md` `## Current Validation`, verbatim, plus this story's additions:

```
rm -rf build
cmake -S . -B build
cmake --build build

./build/marrow_unit_tests
./build/marrow_timeline_model_tests
./build/marrow_timeline_graph_model_tests
./build/marrow_selection_tests
./build/marrow_preference_tests
./build/marrow_viewport_interaction_tests
./build/marrow_windowing_tests
./build/marrow_pen_input_tests
./build/marrow_agent_socket_tests

./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
python3 -m json.tool assets/fixtures/skin_inherit_constraints.mskl > /dev/null

./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke --create /tmp/mar185_created.marrow
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
  --export-runtime /tmp/player_idle_project_export.mskl \
  --export-binary  /tmp/player_idle_project_export.mbin
./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin \
                                 /tmp/player_idle_project_export.mskl

./build/marrow_agent_dispatch_smoke
MARROW_CONFIG_HOME=/tmp/mar185-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2

ctest --test-dir build -N
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -L runtime
ctest --test-dir build --output-on-failure -L editor
ctest --test-dir build --output-on-failure -R marrow.renderer_link_boundary

cmake --build build --target marrow_verify_third_party
cmake --build build --target marrow_constraint_warning_check
```

MCP parity — the registry **does** change here, so this is coverage, not a
witness:

```
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
python3 -m py_compile tools/mcp/server.py tools/mcp/test_client.py \
                      tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
```

**Do not run `./build/marrow_editor_shell` without `--auto-close`**, and do not
create `~/Library/Application Support/Marrow`.

---

## 8. Non-goals

- Plotting Inherit in the scalar graph. MAR-167 excluded it deliberately
  (`timeline_graph_model.cpp:393`, `:478`) because `BoneInherit` is an unordered
  enum with no numeric axis. `timeline_graph_model.{hpp,cpp}` is in the
  empty-diff proof.
- Loop synchronization for inherit lanes (`TimelineLaneKind` gains no member).
- Easing, curve presets, curve mode/driver, or automatic curves for inherit.
- Changing the runtime's `.mskl` inherit parser in any way.
- Any `.mskl` / `.mbin` / C ABI version movement, or any new `.marrow` key.
- Editing `assets/fixtures/*` — `marrow_project_smoke` **aborts** on a partial
  marker match (`editor_project_smoke.cpp:14073-14082`).
- Making the runtime's pruning rule (§1.7) inherit-specific, or adding a
  prune-avoidance rejection that the six sibling families do not have.
- A rename-aware session signal so an **agent** rename cascades the GUI
  clipboard (§2.9, §10.5).
- Anything MAR-184 owns: the schema, the parse, the serializers, the
  materialization, or `merge_inherit_timeline` itself.

---

## 9. Facts to MEASURE in Task 0, not assume

1. **Every row of §0.2's dependency table**, by compiling a probe that names each
   MAR-184 symbol. A signature that differs is a stop-and-record, not a silent
   adaptation.
2. Registry: 64 / the array at `agent_dispatch_smoke.cpp:39` / 10 guards / 2
   Python assertions. Re-derive; do not trust this document.
3. `ctest -N` total.
4. `serialize_project(player_idle.marrow)` byte length + SHA-256.
5. `git grep -c "Inherit remains read-only" src/` → expect **2**.
6. That `skin_inherit_constraints.mskl` still carries exactly the four
   `toggle_inherit`/`child` keys and five bones with absent `inherit`; that
   `player_idle.mskl` still has zero inherit timelines and sixteen bones with
   absent `inherit`.
7. **Re-run MAR-184's pruning M-gate** (its plan §0.5) against the tree as MAR-184
   left it, and record the output. §1.7, §2.11 and every fixture choice in §6
   depend on it.
8. **The `-Wswitch` claim.** Confirm by *building*: add a throwaway seventh enum
   value, compile `marrow_editor`, and record that the build is clean. If any
   diagnostic appears, §1.1's whole framing is wrong and the plan's Task 1
   changes shape. **This is the most consequential gate in Task 0.**
9. Whether `resolved_stored_key_time`'s selector-vs-stored difference is
   observable at `1e-12` for the times §6.2 P8 uses (I13's viability).
10. That `apply_animation_catalog_action` still clears the key selection at
    `shell_project_panels.cpp:607-611` and still never mentions the clipboard.

The author of this document did not build, did not run any binary, and did not
benchmark. Every claim above is a reading of source at `a2981b8`.

---

## 10. Known limitations, stated rather than hidden

1. **Eleven switch arms are added with no way to prove they are needed** (§5.1).
   Each falls through to the correct answer today. They are added because the
   next enum value might not, and because a reader should not have to derive the
   fall-through to know inherit is excluded.
2. **The build still has no `-Wswitch`.** MAR-185 does not add it. Turning on
   `-Wall -Wswitch` across `marrow_editor` would be a large, unrelated
   diff — every existing warning in the tree would surface at once — and is its
   own piece of work. The consequence is recorded, not fixed: the **next** person
   to add a `TimelineKeyKind` value faces the same silent sweep, and §1.1's table
   is the artifact that makes it survivable.
3. **An inherit lane can still be pruned out of existence** by reducing it to one
   key at `t = 0` whose mode equals the bone's setup inherit (§1.7). Identical to
   the shipped behaviour for rotate, translate, scale and shear; deliberately not
   diverged from. Once pruned, the lane cannot be re-created from the dopesheet,
   because a row only exists for a materialized timeline.
4. **`replace_existing_times = true` is the only mode `set_inherit_keyframe`
   uses.** The `false` arm of MAR-184's primitive remains exercised only by
   MAR-184's own P11.
5. **An agent-driven `animation.rename` leaves the GUI clipboard stale** (§2.9).
   The GUI path cascades; the agent path has no shell hook that distinguishes a
   rename from a selection change.
6. **`export.preview` carries no inherit data and never will** — it reports
   target paths only (`agent_handlers_inspection.cpp:23-42`). AC5's
   "export-preview behavior" is discharged as a non-effect assertion (A6) with
   no story-owned inversion, and `marrow_inspect --compare` runs over
   `player_idle`'s export, which is the real equivalence witness.
7. **No pixel is asserted beyond F1's combo.** F1 proves the `Mode` combo is on
   screen and clickable; the lane's diamonds, the key editor's `Time` drag and
   the toolbar buttons are covered UI-free only. A regression that deleted the
   Inherit lane's *draw* call would not be caught.
8. **Time is `double` in the project and `float` in the runtime.** Every
   round-trip assertion uses a float32-aware tolerance, and I13's viability
   depends on that same narrowing (§9 gate 9).

---

## 11. Risks for the implementer

| # | Risk | Mitigation |
| --- | --- | --- |
| R1 | **Building on MAR-184 before it exists.** None of §0.2's symbols are in the tree at `a2981b8` | Task 0 gate 1 compiles a probe naming every one. Do not start Task 1 until it links |
| R2 | **A missed switch arm with no compiler diagnostic.** Eighteen of twenty-nine sites are silent, and five of those are silently *wrong* | §1.1's table is the checklist; Task 0 gate 8 proves the compiler is silent; §5's I3/I4/I6/I7 are the inversions that catch the five that matter |
| R3 | **A rejection case that asserts `!result` instead of the message.** Many causes produce a failure, so the case passes for the wrong reason | Every rejection case in §6 names its expected substrings. MAR-184 plan R4, carried forward |
| R4 | **An `ensure_*` call on an entity whose edit already exists**, making it a silent no-op and disarming the atomicity inversion | §6.2 P5 runs on `child` with **no project edit yet**, stated as load-bearing. MAR-184 plan R6, carried forward |
| R5 | **Preflight-then-mutate makes "mutate early" invisible.** With a candidate copy, an early write to the *candidate* cannot be seen by `serialize_project(*project)` | I8 is defined as **removing** the candidate, not moving a check. MAR-184 §A5 |
| R6 | **The count sweep, inverted.** This is the first story that moves 64, so the danger is now an *incomplete* substitution, not an over-eager one | Thirteen sites enumerated in §1.5 and §3.7; Task 7 greps `\b6[456]\b` over the whole diff and explains every line by hand |
| R7 | **Pointing `marrow_project_smoke` at a new fixture.** The marker gate takes the *skip* branch and runs nothing | §1.9. Build throwaway projects inside the single `player_idle.marrow` invocation |
| R8 | **The pruning trap** makes a correct implementation look broken, in the agent smoke especially, where `player_idle`'s bones are all setup-`Normal` | §1.7; Task 0 gate 7 re-runs MAR-184's M-gate; no case anywhere writes a lone `{0.0, "normal"}` |
| R9 | **A removal that empties an edit resurrects the imported base track** | §2.4 step 8 and I9 |
| R10 | **Shipping a widget with only UI-free coverage.** MAR-178 shipped a scenario that printed success while the frame smoke failed by name | F1 is a real-mouse case in `shell_smoke_frames.cpp`, and it sweeps an existing control in the same pass so a broken seed cannot masquerade as a missing widget |
| R11 | **An inversion result that lies (H1-H4).** A `cp` restore in the same mtime second exercises the inverted binary; a hand-sliced reference line reports a false diff; a rewritten case can be thinner; an assertion can be vacuous | `touch` after every restore; final run from `rm -rf build`; compare whole strings with `cmp`; re-demonstrate falsifiability after any rewrite; and for every "nothing earlier catches this" claim, neuter the earlier detector with `if (false && …)` and re-run |
