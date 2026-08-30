# MAR-179 Complete Constraint Parameter Widgets — Design

Story: `MAR-179`, "Complete constraint parameter widgets", `dependsOn ["MAR-178"]`.
Branch: `feat/mar-168`. As-built baseline: `cf6a199` plus the MAR-177/178
constraint arc.

> Description, verbatim: *"Expose the remaining runtime-backed IK and physics
> fields in the editor and close the IK agent/MCP parity gap."*

This story's premise is, unusually for this chain, **correct and narrow**. The
first job below was still to measure it rather than accept it, because MAR-177's
brief asserted a missing create path that had already shipped. The measurement
confirms the description exactly: three IK fields and eight physics fields are
absent from the GUI, and the same three IK fields are absent from the agent and
MCP surfaces. Path and transform are complete. Nothing else is missing.

Because the gap is narrow, this document is short. Padding it would hide the two
things that are actually interesting: **why the four families' existing widgets
can author a value the loader rejects**, and **whether a widget story should be
tested with a real mouse**.

---

## 0. The measured gap

Every row below was read from the tree, not inferred. `runtime` is the
authoritative field set (`include/marrow/runtime/skeleton.hpp`); `project` is the
overlay struct (`include/marrow/editor/project.hpp`); `GUI` is
`src/editor/shell_constraints.cpp`; `agent` is
`src/editor/agent_handlers_constraints.cpp`; `MCP` is
`tools/mcp/tools/editing.py`.

### 0.1 IK — `IkConstraintData` (`skeleton.hpp:67-76`), `IkConstraintEdit` (`project.hpp:274-283`)

| Field | Runtime default | GUI today | Agent today | MCP today |
|---|---|---|---|---|
| `bone_indices` / `bone_names` | — | ✅ `shell_constraints.cpp:1128-1145` (+1/2-bone radios `:1095-1126`) | ✅ `:759-770` | ✅ `editing.py:865` |
| `target_bone_index` / `target_bone_name` | — | ✅ `:1147-1158` | ✅ `:751-757` | ✅ `editing.py:862` |
| `mix` | `1.0` | ✅ `:1160-1180` slider `[0,1]` | ✅ `:772-778` | ✅ `editing.py:863` |
| `bend_positive` | `true` | ✅ `:1182-1194` checkbox | ✅ `:780-786` | ✅ `editing.py:864` |
| **`softness`** | `0.0` | ❌ **absent** | ❌ **absent** | ❌ **absent** |
| **`compress`** | `false` | ❌ **absent** | ❌ **absent** | ❌ **absent** |
| **`stretch`** | `false` | ❌ **absent** | ❌ **absent** | ❌ **absent** |

The three absent fields are already carried end to end by everything *below* the
surfaces: `IkConstraintEdit` holds them (`project.hpp:280-282`), the project
parser reads them (`project.cpp:3168-3189`), the serializer writes them
(`project.cpp:4214-4216`), the runtime parser reads them
(`skeleton_parse.cpp:3146-3177`), and the runtime consumes them
(`skeleton_constraints.cpp:1324-1340`, `:1578`). Even the agent's own
materializer copies them (`agent_handlers_constraints.cpp:747-749`) — it copies
values it then offers no way to change. `assets/fixtures/player_idle.marrow`
already stores all three on `editor_arm_reach`.

**So MAR-179's IK work is surface-only. No model, format, or runtime change.**

### 0.2 Physics — `PhysicsConstraintData` (`skeleton.hpp:176-193`), `PhysicsConstraintEdit` (`project.hpp:308-325`)

| Field | Runtime default | GUI today | Agent today | MCP today |
|---|---|---|---|---|
| `bone_indices` | — | ✅ `:1832-1848` | ✅ `:519-522` | ✅ `editing.py:931` |
| **`step`** | `1/60` | ❌ **absent** | ✅ `:529` | ✅ `editing.py:933` |
| **`x`** | `1.0` | ❌ **absent** | ✅ `:530` | ✅ `editing.py:934` |
| **`y`** | `1.0` | ❌ **absent** | ✅ `:531` | ✅ `editing.py:935` |
| **`rotate`** | `1.0` | ❌ **absent** | ✅ `:532` | ✅ `editing.py:936` |
| **`scale_x`** | `1.0` | ❌ **absent** | ✅ `:533-534` | ✅ `editing.py:937` |
| **`shear_x`** | `0.0` | ❌ **absent** | ✅ `:535-536` | ✅ `editing.py:938` |
| **`limit`** | `500.0` | ❌ **absent** | ✅ `:537` | ✅ `editing.py:939` |
| `inertia` | `0.0` | ✅ `:1878-1885` slider `[0,10⁰]` | ✅ `:538-539` | ✅ `editing.py:940` |
| `damping` | `0.0` | ✅ `:1886-1893` slider `[0,10]` | ✅ `:540-541` | ✅ `editing.py:941` |
| `strength` | `0.0` | ✅ `:1894-1901` slider `[0,50]` | ✅ `:542-543` | ✅ `editing.py:942` |
| **`mass_inverse`** | `1.0` | ❌ **absent** | ✅ `:544-549` | ✅ `editing.py:943` |
| `gravity` | `{0,0}` | ✅ `:1939-1953` | ✅ `:551` | ✅ `editing.py:944` |
| `wind` | `{0,0}` | ✅ `:1954-1968` | ✅ `:552` | ✅ `editing.py:945` |
| `mix` | `1.0` | ✅ `:1902-1909` slider `[0,1]` | ✅ `:550` | ✅ `editing.py:946` |

**The physics gap is GUI-only.** The agent and MCP already expose all sixteen
fields through `PhysicsConstraintTraits` — which is exactly why the agent schema
is the strong oracle the brief called it.

### 0.3 Path and transform are complete

Path GUI (`shell_constraints.cpp:1250-1445`): Guide Slot `:1263`, Chain Bone N
`:1311-1327`, Position `:1329-1350`, Spacing `:1352-1373`, Spacing Mode
`:1375-1395`, Rotate Mix `:1399-1420`, Translate Mix `:1423-1444`. That is every
member of `PathConstraintEdit`.

Transform GUI (`shell_constraints.cpp:1500-1730`): Source Bone `:1518`, Target
Bone N `:1574-1595`, Rotate/Translate/Scale/Shear Mix `:1621`/`:1628`/`:1635`/`:1642`,
and all seven offsets `:1678`-`:1726`. That is every member of
`TransformConstraintEdit`.

**MAR-179 adds no path or transform widget.** It touches those two families for
exactly one reason, in §3.

### 0.4 Summary of what MAR-179 closes

1. Three IK widgets: **Softness**, **Compress**, **Stretch**.
2. Eight physics widgets: **Step**, **X**, **Y**, **Rotate**, **Scale X**,
   **Shear X**, **Limit**, **Mass Inverse**.
3. Three IK agent arguments (`softness`, `compress`, `stretch`) plus the
   `edit_ik_constraint` dry-run/live `scene_delta`, which today reports one field.
4. Four MCP schema properties on `edit_ik_constraint` (`softness`, `compress`,
   `stretch`, and `merge` — see §5.3).
5. The clamp hazard in §3, across all four families.

Nothing else. §7 lists the non-goals.

---

## 1. Goal

Every runtime-backed IK and physics field is authorable in the editor, through a
widget whose accepted value range is exactly the range the loader accepts; and
`edit_ik_constraint` accepts and reports the same field set the other three
`edit_*_constraint` operations already do.

---

## 2. Validation, measured at all three layers

This is the part that governs widget choice, so it is measured field by field.
There are three places a constraint value is checked:

- **L1 — runtime skeleton parse** (`src/runtime/skeleton_parse.cpp`). Runs on
  every materialization, because `build_project_runtime()` (`project.cpp:7495`)
  writes the merged document and re-parses it through `load_skeleton_data`. A
  value L1 rejects makes `rebuild_project_runtime(state)` fail.
- **L2 — project parse** (`src/editor/project.cpp`, `parse_*_constraint_edits`).
  Runs on `load_project(path)`. A value L2 rejects makes the `.marrow`
  **unopenable**.
- **L3 — `validate_project_for_save`** (`project.cpp:5989-6110`). Runs on save. A
  value L3 rejects makes the project **unsavable**.

| Family / field | L1 (runtime parse) | L2 (project parse) | L3 (save) |
|---|---|---|---|
| IK `mix` | `[0,1]` `skeleton_parse.cpp:3138-3145` | `[0,1]` `project.cpp:3160-3167` | `[0,1]` `project.cpp:6000-6003` |
| **IK `softness`** | **none** `skeleton_parse.cpp:3146-3153` | **none** `project.cpp:3168-3171` | **none** |
| IK `compress` / `stretch` | type only | type only | none |
| Path `position`/`rotate_mix`/`translate_mix` | `[0,1]` | `[0,1]` | `[0,1]` `project.cpp:6023-6028` |
| Path `spacing` | `>= 0` | `>= 0` | `>= 0` |
| Transform four mixes | `[0,1]` | `[0,1]` `project.cpp:3484-3522` | `[0,1]` `project.cpp:6053-6059` |
| Transform offsets | none | none | none |
| Physics `step` | `> 0` `skeleton_parse.cpp:3877-3890` | `> 0` `project.cpp:3657-3665` | `> 0` `project.cpp:6084` |
| Physics `x`,`y`,`rotate`,`scale_x`,`shear_x` | `>= 0` | `>= 0` `project.cpp:3667-3719` | `>= 0` `project.cpp:6085-6087` |
| Physics `limit` | `>= 0` `skeleton_parse.cpp:3973-3987` | `>= 0` `project.cpp:3721-3729` | `>= 0` `project.cpp:6086` |
| Physics `inertia` | `[0,1]` `skeleton_parse.cpp:3989-4002` | `[0,1]` `project.cpp:3731-3739` | `[0,1]` `project.cpp:6088` |
| Physics `damping`, `strength` | `>= 0` | `>= 0` | `>= 0` `project.cpp:6089` |
| Physics `mass_inverse` | `>= 0` `skeleton_parse.cpp:4035-4048` | `>= 0` `project.cpp:3766-3777` | `>= 0` `project.cpp:6090` |
| Physics `gravity`, `wind` | none | none | none |
| Physics `mix` | `[0,1]` | `[0,1]` | `[0,1]` `project.cpp:6091` |

Two conclusions follow, and they are the design's two load-bearing facts.

### 2.1 IK `softness` is unvalidated at every layer, and the runtime clamps it

`skeleton_constraints.cpp:1324-1326` applies softness only when `softness > 0.0f`
and then uses `std::max(0.0f, softness)`. A negative softness is therefore
**accepted by all three layers and silently means zero**.

The widget must not invent a bound the format does not have. It enforces
non-negativity only — which is not narrowing, because negative and zero are
already the same value to the runtime. The upper end stays open.

### 2.2 Every physics field MAR-179 adds is validated at L1, so a bad value cannot reach disk

Because L1 runs inside `rebuild_project_runtime()`, and
`apply_coalesced_edit_frame` (`shell_coalesced_edit.hpp:67-81`) **rolls the
project back and reports `failure_status`** when the rebuild fails, the editor
today cannot persist an out-of-range physics number. That is a real safety net,
and it is why this story is not a corruption risk. But it is a *bad* net: the
user's drag is aborted mid-gesture with a status line, and the pending history
entry is discarded. Preventing the value is strictly better than rolling it back.

---

## 3. The clamp hazard, and the one thing MAR-179 fixes outside its two families

Every existing constraint slider is called with **six arguments and no flags**:

```cpp
// shell_constraints.cpp:1160-1166 — IK Mix, representative of ten call sites
const bool mix_changed = ImGui::SliderScalar(
    "Mix", ImGuiDataType_Double, &edited_mix, &kZero, &kOne, "%.2f");
```

ImGui 1.92.6 (`external/imgui/imgui.h:32`) documents this precisely
(`imgui.h:699`): *"Ctrl+Click on any slider to turn them into an input box.
Manually input values aren't clamped by default and can go off-bounds. Use
`ImGuiSliderFlags_AlwaysClamp` to always clamp."* The flag exists at
`imgui.h:2026` and is already the established idiom in this tree —
`shell_parameters.cpp:451`, `shell_viewport_ui.cpp:1641`/`:1686`/`:1718`,
`shell_timeline.cpp:2636`.

So today, Ctrl+clicking IK **Mix** and typing `5` sets `mix = 5.0`; L1 then
rejects it and the frame rolls back with `"IK constraint edit failed"`. The same
holds for path Position/Rotate Mix/Translate Mix, the four transform mixes, and
physics Inertia/Mix. Ten call sites, one missing flag.

**Decision.** MAR-179 adds `ImGuiSliderFlags_AlwaysClamp` to every constraint
widget whose `[min,max]` **equals** the loader's bound, in all four families. The
rule is exact and stated once:

> **A constraint widget's `[min,max]` is the loader's bound, and only then may it
> clamp.** Where the widget's range is narrower than the loader's, clamping would
> refuse a value the format accepts, so the widget is re-formed to the loader's
> bound instead of gaining the flag.

Applying the rule:

| Widget | Loader bound | Today | MAR-179 |
|---|---|---|---|
| IK Mix | `[0,1]` | slider `[0,1]`, no flag | + `AlwaysClamp` |
| Path Position, Rotate Mix, Translate Mix | `[0,1]` | slider `[0,1]`, no flag | + `AlwaysClamp` |
| Transform Rotate/Translate/Scale/Shear Mix | `[0,1]` | slider `[0,1]`, no flag | + `AlwaysClamp` |
| Physics Inertia, Mix | `[0,1]` | slider `[0,1]`, no flag | + `AlwaysClamp` |
| Physics Damping | `>= 0` | slider `[0,10]` — **narrower** | re-formed to the magnitude drag of §4.2 |
| Physics Strength | `>= 0` | slider `[0,50]` — **narrower** | re-formed to the magnitude drag of §4.2 |
| Path Spacing | `>= 0` | slider `[0,1]` — **narrower** | **untouched**, see below |
| Transform offsets, Gravity, Wind | none | unbounded drag | unchanged, correct already |

Path **Spacing** is the one measured narrowness MAR-179 deliberately leaves
alone. Its correct range depends on `spacing_mode`: a percentage in `Percent`
mode, a distance in `Length` mode. Choosing a mode-dependent widget range is a
real design question with its own preview implications, and it belongs to
whatever story revisits path authoring. Widening it here without that thought
would be worse than leaving a known, recorded narrowness. It is listed in §7.

This is the entire justification for MAR-179 touching path and transform: one
flag per call site, closing one hazard class, with no range or behavior change
for any value the loader accepts.

---

## 4. Widget specification

### 4.1 Shared constants

Added at the top of `draw_constraints_window`, beside the existing
`kZero`/`kOne`/`kTen` (`shell_constraints.cpp:976-978`):

```cpp
constexpr double kUnbounded = std::numeric_limits<double>::max();
// The loader requires step > 0 (skeleton_parse.cpp:3884). The display format is
// "%.4f", so 1e-4 is the smallest value the widget can show distinctly, and it
// is strictly positive. The bound is therefore derived from the format, not
// chosen: any smaller minimum would display as 0.0000 while storing something
// else.
constexpr double kMinPhysicsStep = 1e-4;
constexpr ImGuiSliderFlags kClamp = ImGuiSliderFlags_AlwaysClamp;
```

`kUnbounded` is not an invented ceiling: with `p_min = 0.0` and
`p_max = DBL_MAX`, `AlwaysClamp` enforces exactly `>= 0`, which is exactly the
loader's bound. ImGui uses the range only for clamping in `DragScalar`; the drag
rate comes from `v_speed`.

### 4.2 The two widget forms

**Form A — bounded mix (`SliderScalar`)**, for a field the loader bounds on both
sides:

```cpp
ImGui::SliderScalar(label, ImGuiDataType_Double, &v, &kZero, &kOne, "%.2f", kClamp);
```

**Form B — non-negative magnitude (`DragScalar`)**, for a field the loader bounds
only below:

```cpp
ImGui::DragScalar(label, ImGuiDataType_Double, &v, speed, &lo, &kUnbounded, format, kClamp);
```

### 4.3 The eleven new widgets

| Label | Field | Form | `lo` | speed | format |
|---|---|---|---|---|---|
| `Softness` | `IkConstraintEdit::softness` | B | `kZero` | `0.5f` | `"%.2f"` |
| `Compress` | `IkConstraintEdit::compress` | `ImGui::Checkbox` | — | — | — |
| `Stretch` | `IkConstraintEdit::stretch` | `ImGui::Checkbox` | — | — | — |
| `Step` | `PhysicsConstraintEdit::step` | B | `kMinPhysicsStep` | `0.0005f` | `"%.4f"` |
| `X##physics` | `x` | B | `kZero` | `0.01f` | `"%.3f"` |
| `Y##physics` | `y` | B | `kZero` | `0.01f` | `"%.3f"` |
| `Rotate##physics` | `rotate` | B | `kZero` | `0.01f` | `"%.3f"` |
| `Scale X##physics` | `scale_x` | B | `kZero` | `0.01f` | `"%.3f"` |
| `Shear X##physics` | `shear_x` | B | `kZero` | `0.01f` | `"%.3f"` |
| `Limit` | `limit` | B | `kZero` | `1.0f` | `"%.2f"` |
| `Mass Inverse` | `mass_inverse` | B | `kZero` | `0.01f` | `"%.3f"` |

The `##physics` suffixes follow the existing `"Mix##physics"` precedent
(`shell_constraints.cpp:1903`) and exist because `X`, `Y`, `Rotate` and
`Scale X` would otherwise collide with transform-offset labels if the panels are
ever merged; they also keep the ImGui IDs unambiguous for the smoke in §6.2.

`Softness` needs no suffix — no other constraint widget carries that label.

### 4.4 Physics panel order

Today the panel runs Inertia → Damping → Strength → Mix → Gravity → Wind, while
`PhysicsConstraintData`, the serialized JSON (`project.cpp:4293-4305`) and
`PhysicsConstraintTraits::preview` (`agent_handlers_constraints.cpp:578-591`) all
run step → x → y → rotate → scale_x → shear_x → limit → inertia → damping →
strength → mass_inverse → gravity → wind → mix.

**Decision: the panel adopts the struct order.** Final order:

```
Chain bones
Step, X, Y, Rotate, Scale X, Shear X, Limit,
Inertia, Damping, Strength, Mass Inverse,
Gravity X, Gravity Y, Wind X, Wind Y,
Mix
```

The only move is `Mix##physics` from fourth to last. It costs one relocated call
and buys a property worth having: the panel, the struct, the `.marrow` JSON and
the agent preview enumerate the same fields in the same order, so a future field
added to one is visibly missing from the others.

### 4.5 Booleans go through the agent, exactly like `Bend Positive`

`Compress` and `Stretch` are `ImGui::Checkbox` calls that, on change, build and
dispatch an `edit_ik_constraint` command — byte-for-byte the shape of the
existing `Bend Positive` handler (`shell_constraints.cpp:1182-1194`):

```cpp
bool compress = display_edit.compress;
if (ImGui::Checkbox("Compress", &compress)) {
    namespace json = marrow::runtime::json;
    json::Value::Object cmd_obj;
    cmd_obj.emplace("op", json::Value("edit_ik_constraint", {}));
    json::Value::Object args_obj;
    args_obj.emplace("name", json::Value(selected_name, {}));
    args_obj.emplace("compress", json::Value(compress, {}));
    cmd_obj.emplace("args", json::Value(std::move(args_obj), {}));
    dispatch_agent_command(state, json::Value(std::move(cmd_obj), {}));
}
```

> `json::Value(compress, {})` binds correctly here because `compress` is a real
> `bool`. The known `json::Value` pitfall is the reverse — a *string literal*
> binding to the `bool` constructor — so any string argument must be wrapped in
> `std::string`, as `selected_name` already is.

**This is why the GUI gap and the agent gap are one story rather than two.** The
checkboxes physically cannot work until `edit_ik_constraint` accepts `compress`
and `stretch`. AC1 and AC4 are the same change seen from two sides.

`Softness` is a number and follows `Mix`: `apply_constraint_project_drag`
directly against `project->ik_constraint_edits[*index].softness`, not the agent.
That split is inherited, not invented — instantaneous toggles dispatch; dragged
scalars coalesce.

### 4.6 Physics widgets reuse the existing local helpers

The eight new physics scalars all take Form B, so they extend the existing
`update_force` shape (`shell_constraints.cpp:1911-1937`) rather than
`update_positive_value` (`:1849-1876`). Concretely, `update_positive_value` is
**replaced** by a `update_magnitude(label, value, setter, lo, speed, format,
status)` helper emitting Form B, and a separate `update_mix(label, value, setter,
status)` helper emitting Form A for Inertia and Mix. Both route through the
existing `apply_constraint_project_drag`, unchanged.

---

## 5. Undo, selection, preview, and the agent surface

### 5.1 Undo granularity: one history entry per drag, one per typed edit

Unchanged from the established pattern; MAR-179 introduces no new mechanism.
`apply_constraint_project_drag` (`shell_constraints.cpp:34-54`) forwards to
`apply_coalesced_edit_frame` (`shell_coalesced_edit.hpp:37-95`), which:

- on `frame.activated` captures a history snapshot and opens `pending_edit_action`
  keyed by the ImGui item id (`:47-58`);
- on every `frame.changed` mutates and rebuilds, rolling back on failure
  (`:66-83`);
- on `frame.deactivated_after_edit` finalizes **one** history entry (`:85-89`).

A 40-frame drag and a Ctrl+click typed edit both traverse
activate → change(s) → deactivate-after-edit, so both produce exactly one entry.
Every new scalar passes `allow_merge = false` and
`group = constraint_group(kind, selected_name)`, matching all ten existing
constraint scalars (e.g. `:1167-1180`), so two successive drags on the same field
stay two entries.

The two checkboxes produce one `EditorSession` transaction per click via
`dispatch_agent_command`, exactly as `Bend Positive` does.

### 5.2 Selection, focus, preview

- **Selection**: unchanged. A parameter edit does not rename or delete, so
  nothing in `SelectionSet` can go stale and MAR-178's
  `reconcile_constraint_selection()` is not called. The active constraint, the
  active family tab (`state->constraints_tab`) and any co-selected bones are
  untouched.
- **Focus**: ImGui owns it. `ActiveId` stays on the dragged item for the whole
  gesture — which is precisely what `apply_coalesced_edit_frame` keys on. Nothing
  in `draw_constraints_window` steals focus, and MAR-179 adds nothing that does.
  Note the existing `select_constraint(state, kind, name, "", false)` inside
  `commit_constraint_change` (`:1000`) is on the *structural* path (Add /
  chain-bone edits), not the scalar path, and is not extended.
- **Preview**: immediate. Every scalar carries
  `EditImpact::Project | Runtime | Preview` (`shell_coalesced_edit.hpp:57-60`)
  and each changed frame runs `rebuild_project_runtime(state)`, so the viewport
  reflects the value while the mouse is still down. The two checkboxes get the
  same impact triple from `dispatch_agent_command`'s
  `EditKind::EditProperty` transaction (`agent_handlers_constraints.cpp:714-719`).

### 5.3 `edit_ik_constraint`: three arguments, a real dry run, and a live delta

The current handler (`agent_handlers_constraints.cpp:676-797`) is hand-written
rather than a `…Traits` specialization, and it shows in three ways:

1. It parses only `target`, `bone_names`, `mix`, `bend_positive`.
2. Its dry run (`:690-711`) short-circuits **before** any merge and echoes a
   three-key object — `{"dry_run","name","mix"}` — where the other three families
   echo the full merged edit. `agent_dispatch_smoke.cpp:2811-2816` pins that
   payload today.
3. Its live path returns `make_success(...)` with **no** `scene_delta`, where the
   other three return `Traits::preview(merged, false)`.

**Decision: extend the hand-written handler; do not convert IK to
`handle_constraint_edit<IkConstraintTraits>`.**

Converting looks tempting and is wrong here. The template calls
`validate_bone_names(skeleton, …)` inside `merge` and returns a *pre-transaction*
error. The IK handler's absence of that check is what makes
`agent_dispatch_smoke.cpp:2796-2807` — `edit_ik_constraint invalid target
rollback`, asserting the `"Failed to apply IK constraint edit: "` **commit-time**
prefix — the only case in the whole suite that proves constraint commit-time
rollback. Converting would silently delete that coverage in exchange for
symmetry. Keep the handler; give it the template's *shape*.

Restructured handler, in order:

1. Resolve `name`; build `merged` from the project edit if present, else
   `materialize` from the runtime constraint (the code already at `:735-750`),
   else return `not_found`.
2. Apply `target`, `bone_names`, `mix`, `bend_positive`, **`softness`**,
   **`compress`**, **`stretch`** to `merged`, each with the existing
   `"<name> must be <type> or null."` idiom.
3. Range guard: `softness < 0.0` → `"ik constraint softness must be
   non-negative."`; `mix` outside `[0,1]` is left to commit-time L1 exactly as
   today, so the rollback case above still bites.
4. If `dry_run`, return `ik_constraint_preview(merged, true)`.
5. Otherwise open the transaction, upsert `merged`, commit with the unchanged
   `"Failed to apply IK constraint edit: "` policy, and return
   `ik_constraint_preview(merged, false)` as the live `scene_delta`.

`ik_constraint_preview` mirrors `PathConstraintTraits::preview`'s shape:

```json
{"dry_run":<bool>,"name":…,"bones":[…],"target":…,"mix":…,
 "bend_positive":…,"softness":…,"compress":…,"stretch":…}
```

This makes the dry-run and live payloads identical apart from `"dry_run"`, which
is the invariant `AGENTS.md:271` records for the other constraint operations, and
it is the assertion that carries AC4.

**The `softness` asymmetry, stated plainly.** The agent rejects a negative
softness; the loader accepts one (§2.1). This is deliberate and is the
codebase's GUI-clamps / agent-rejects convention applied where the format itself
is permissive: the runtime treats a negative as zero, so authoring one is always
a mistake, but tightening L2 would make an existing `.marrow` carrying a negative
value **unopenable**, which is a compatibility break MAR-179 will not take. The
guard is a surface guard. §6.1 asserts both halves.

### 5.4 MCP

`tools/mcp/tools/editing.py:856-870` gains four properties on
`edit_ik_constraint`:

```python
"softness": {"type": ["number", "null"]},
"compress": {"type": ["boolean", "null"]},
"stretch":  {"type": ["boolean", "null"]},
"merge":    {"type": "boolean"},
```

`merge` is a fourth measured parity gap: the C++ handler has always read
`bool_arg(args, "merge")` (`agent_handlers_constraints.cpp:714`) and the path,
transform and physics schemas all declare it (`editing.py:886`, `:947`), but the
IK schema never has.

### 5.5 `constraints.list` is not extended

`constraints_value()` (`agent_dispatch.cpp:836-878`) reports only
`type`/`name`/`bones`/`target`/`slot`/`source` for every family — no parameter
values, for any of the four. Widening it is a change to an inspection payload
that `agent_dispatch_smoke` pins byte-exactly, it benefits all four families
equally, and it is not what this story asks for. **The read-back channel for
parameter values is `edit_*_constraint` with `dry_run: true`**, which is how
MAR-178's `test_client.py` reads back names. Recorded as a non-goal in §7.

---

## 6. Validation strategy

Four binaries, in dependency order. The commands are quoted verbatim from
`AGENTS.md`.

### 6.1 `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`

Model-layer truth for the eleven fields. `player_idle.marrow` already stores all
of them (`constraint_edits.ik[0]`, `constraint_edits.physics[0]`), so no fixture
changes.

- **S1 — boundary round-trip.** Set IK `softness = 0.0` then `12.5`,
  `compress = true`, `stretch = true`; physics `step = kMinPhysicsStep`,
  `x = 0.0`, `y = 2.5`, `rotate = 0.0`, `scale_x = 0.0`, `shear_x = 0.75`,
  `limit = 0.0`, `mass_inverse = 0.0`. `save_project` → **`load_project`** →
  `build_project_runtime` → assert every value on the materialized
  `IkConstraintData`/`PhysicsConstraintData`, not on the `ProjectData`. Reload is
  mandatory, not stylistic: L3 is the only layer a bare `save()` exercises, and
  L3 sees no base document.
- **S2 — invalid rollback, per layer.** `step = 0.0` and `step = -1.0` →
  `build_project_runtime` fails carrying `physics step must be greater than
  zero`; `mass_inverse = -1.0` → `physics massInverse must be non-negative`;
  the same values → `save_project` fails with `physics constraint edit numeric
  values must stay within their valid ranges`. Assert the project is unchanged
  after each.
- **S3 — the `softness` asymmetry.** A `.marrow` carrying `softness = -3.0`
  **loads**, **materializes**, **saves** and **reloads**; the materialized
  `IkConstraintData::softness` is `-3.0`. This is the compatibility assertion
  behind §5.3 and it must be seen to pass, not assumed.
- **S4 — export equivalence.** `--export-runtime` + `--export-binary`, re-parse
  both, assert the eleven fields agree. `.mbin` needs no work: `binary.cpp` is a
  generic JSON document encoder (`NodeTag` at `:32-40`, `encode_value` at `:223`)
  with no constraint-specific code path at all — `grep -n physics
  src/runtime/binary.cpp` returns nothing. The assertion exists to keep that
  true, not because a change is expected.

### 6.2 `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`

**Decision on the real-mouse capability: yes, use it, for the widget-existence
and coalescing claims only.**

MAR-178 disclosed that it asserted its modals through the UI-free helpers the
buttons call. That was right for MAR-178, whose interesting behavior lived in
`apply_constraint_catalog_edit()` and could be reached without a frame. It is
wrong for MAR-179, whose entire deliverable **is** "the widget is on the screen."
A headless test that calls `edit.softness = 12.5` directly proves the model
already worked before this story started — it is precisely the test that cannot
fail. The mechanism must be able to observe a *deleted widget*.

It can, with stock ImGui and no production instrumentation:

- The four family panels use `widgets::seg_toggle` (`shell_constraints.cpp:1027-1033`),
  a custom segmented strip with a balanced `PushID`/`PopID`
  (`shell_widgets.cpp:140`,`:159`) — **not** `BeginTabItem`, which would push an
  override ID. Every parameter widget is therefore emitted at plain window scope,
  so `ImGui::FindWindowByName(kConstraintsWindowTitle)->GetID("Softness")` is
  exactly the slider's id.
- `ImGuiContext::HoveredId` (`imgui_internal.h:2443`) names whatever the mouse is
  over. This tree already reaches into the context this way in production —
  `ImGui::GetActiveID()` plus `context->ActiveIdIsAlive` at `shell_core.cpp:413`
  and `shell_inspector.cpp:654`.

So the scenario, a new `validate_constraint_parameter_shell_smoke()` in
`src/editor/shell_smoke_constraints.cpp` (no new file, no CMake change), called
from `shell_smoke.cpp` beside `validate_constraint_lifecycle_shell_smoke` at
`:132`:

- **C1 — presence, all eleven.** Select `editor_arm_reach`; render; sweep the
  mouse down the constraints window's inner column one frame per step,
  `SetScrollY`-ing when the target is below the clip rect exactly as
  `shell_smoke_frames.cpp:300-316` already does for the graph plot; record every
  `HoveredId` seen. Assert the set contains `window->GetID(label)` for
  `Softness`, `Compress`, `Stretch`. Switch `constraints_tab` to Physics, select
  `editor_ribbon_secondary`, repeat for the eight physics labels. **Failing to
  find a label is a failure, never a skip** — a skip here would reintroduce the
  test that cannot fail.
- **C2 — one drag, one entry.** Press on `Softness`, move across several frames,
  release. Assert the project value changed, `undo_count()` grew by **exactly
  one**, and `undo()` restores the prior serialization byte-for-byte.
- **C3 — one drag, one entry, physics.** Same on `Step`.
- **C4 — the checkboxes reach the agent.** Click `Compress`, then `Stretch`.
  Assert `ik_constraint_edits[0].compress`/`.stretch` flipped, `undo_count()`
  grew by exactly one per click, and the redo of each restores it.
- **C5 — the clamp bites.** Ctrl+click `Inertia`, type `5`, Enter. Assert the
  committed value is **`1.0`**.

  *Fallback, stated now so it cannot become a silent drop:* if driving ImGui's
  Ctrl+click text path headlessly (`io.AddKeyEvent(ImGuiMod_Ctrl, …)` +
  `io.AddInputCharacter` + `ImGuiKey_Enter`) proves not to work in this harness,
  C5 is replaced by a project-layer assertion that `inertia = 5.0` makes
  `build_project_runtime` fail with `physics inertia must stay within [0, 1]`
  — and the substitution is **recorded in `AGENTS.md`**, because it downgrades
  the claim from "the widget clamps" to "an unclamped value would be rejected".

### 6.3 `./build/marrow_agent_dispatch_smoke`

- IK dry run with `softness`/`compress`/`stretch` → `expect_exact_scene_delta`
  against the full nine-key payload of §5.3.
- The **existing** expectation at `agent_dispatch_smoke.cpp:2811-2816`,
  `{"dry_run":true,"name":"editor_arm_reach","mix":0.75}`, is replaced by the
  full payload. This is not incidental churn — it *is* the assertion that the
  fields are exposed.
- The existing commit-time rollback case at `:2796-2807` must still pass
  **unchanged**, proving §5.3's decision not to convert IK to the traits template
  held.
- New: live `edit_ik_constraint` returns a `scene_delta` byte-identical to the
  dry run apart from `"dry_run"`.
- New: `softness: -1` → rejected, project unchanged, message
  `ik constraint softness must be non-negative.`
- Registry: `kExpectedOperations` stays `std::array<…, 64>`
  (`agent_dispatch_smoke.cpp:39`) and the `edit_ik_constraint` row at `:86` is
  untouched.

### 6.4 `tools/mcp/test_client.py`

Run against `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876`.

- `assert len(registry_names) == 64` and `assert len(mcp_names) == 64`
  (`test_client.py:53`,`:55`) **must not change**.
- New: `edit_ik_constraint` dry run → live with `softness`/`compress`/`stretch`
  → dry-run read-back showing the new values → `undo` → read-back showing the
  originals.
- `python -m py_compile` over all four MCP files.

### 6.5 Registry and count sweep: nothing moves

MAR-179 adds **no operation**. `kOperationSpecs` stays at 64 with the same split
(edit 39, inspection 12, management 10, validation 3), measured in Task 0. Every
count site stays at 64 and must be proved untouched, not merely left alone:

| Site | Count |
|---|---|
| `src/samples/agent_dispatch_smoke.cpp:39` | array size 64 |
| `src/editor/shell_smoke_graph.cpp` | 7 guards at `:148`, `:636`, `:1558`, `:2128`, `:3142`, `:3957`, `:4603` + 7 messages one line below each |
| `src/editor/shell_smoke_timeline.cpp:3697` + `:3698` | 1 guard + 1 message |
| `src/editor/shell_smoke_constraints.cpp:138` + `:139` | 1 guard + 1 message (added by MAR-178) |
| `tools/mcp/test_client.py:53`, `:55` | 2 assertions |
| Prose: `AGENTS.md:161`; `docs/root1/editing-gap-analysis.md:23`,`:85`,`:86`,`:95`,`:192`,`:455`; `docs/root1/refector.md:20`,`:112` | unchanged |

The new shell scenario added in §6.2 carries the same
`operation_count_before != 64U` guard as its nine siblings, taking the code sites
from ten to eleven.

`editing-gap-analysis.md:95` currently reads *"누락 위젯(IK `softness`/`compress`/`stretch`,
physics `step`/`x`/`y` 등)은 MAR-179 범위로 남는다"* — that sentence, and only
that sentence, is rewritten to record the gap as closed.

---

## 7. Non-goals

1. **Everything in MAR-180.** Atomic save via temp-file+rename, Save As path
   rebasing, `EditorSession::create`/`close`, failure-safe runtime-source
   adoption, file dialogs. MAR-179 writes projects through the existing
   `save_project` and must not touch it.
2. **No new agent operation.** Registry stays 64.
3. **No `constraints.list` widening** (§5.5). Parameter read-back is via
   `edit_*_constraint` dry run.
4. **No path or transform widget added or removed.** Those families are complete
   (§0.3). The only path/transform change is `ImGuiSliderFlags_AlwaysClamp` on
   seven sliders whose range already equals the loader's bound.
5. **Path `Spacing`'s `[0,1]` ceiling is not widened** (§3). Its correct range is
   `spacing_mode`-dependent and that is a design question of its own. Recorded
   here as a known, deliberate leftover.
6. **No validation is tightened at L1, L2 or L3.** In particular IK `softness`
   stays unvalidated in the format, so no existing `.marrow` becomes unopenable.
   The only new rejection is at the agent surface (§5.3).
7. **No new IK bone-count affordance.** The 1/2-bone radios stay as they are.
8. **No constraint create/rename/delete change.** MAR-177 and MAR-178 own those.
9. **No fixture change.** `player_idle.marrow` already carries every field.

---

## 8. Compatibility

| Surface | Change |
|---|---|
| `.mskl` v1 | **none** — no key added, removed or re-ranged; `skeleton_parse.cpp` untouched |
| `.mbin` v2 | **none** — generic document encoder, no constraint-specific path (`binary.cpp`) |
| `.marrow` schema | **none** — every field already parsed and serialized (`project.cpp:3168-3189`, `:3657-3777`, `:4214-4216`, `:4293-4305`) |
| C ABI v1 | **none** — `include/marrow/` C headers untouched |
| Agent registry | **none** — 64 operations, same category split |
| Agent wire | **additive** — three new optional `edit_ik_constraint` args; `edit_ik_constraint`'s `scene_delta` grows from three keys to nine and gains a live payload |
| MCP schema | **additive** — four new optional properties on one tool |
| Old projects | open, save and reload unchanged; §6.1 S3 asserts the one value (`softness < 0`) where the surfaces are now stricter than the format |

---

## 9. Risks

1. **C1's ID assumption.** If any future change wraps the parameter widgets in a
   `BeginChild`, a `PushID`, or a real `BeginTabItem`, `window->GetID(label)` no
   longer matches and C1 fails *loudly*. That is the correct failure mode, but the
   implementer must confirm the assumption in Task 0 rather than inherit it.
2. **C5's Ctrl+click path.** Explicitly bounded by the fallback in §6.2.
3. **The `Mix##physics` relocation** (§4.4) changes a rendered panel's row order.
   Nothing asserts that order today; Task 0 confirms it.
4. **`DragScalar` with `p_max = DBL_MAX`.** Verified against ImGui 1.92.6
   semantics — the range is used only for clamping in `DragScalar` — but it is an
   unusual argument and Task 0 measures the round trip rather than trusting it.
5. **The replaced exact-delta expectation** at `agent_dispatch_smoke.cpp:2811`.
   Its new value must be *measured from the run*, not transcribed from this
   document, because `json::Value::Object` key ordering is a property of the
   container, not of this spec.
