# MAR-179 Complete Constraint Parameter Widgets — Implementation Plan

Design: `docs/superpowers/specs/2026-08-30-mar-179-constraint-parameter-widgets-design.md`
Branch: `feat/mar-168`. Baseline: `cf6a199` + the MAR-177/178 constraint arc.

Read the design first. This plan does not restate it; it sequences it.

---

## Global constraints

1. **No new agent operation.** The registry is 64 before and 64 after. Any task
   that changes `kOperationSpecs` is a bug in this plan — stop and re-read.
2. **No format change.** `.mskl` v1, `.mbin` v2, `.marrow` schema and C ABI v1 are
   untouched. `src/runtime/skeleton_parse.cpp`, `src/runtime/binary.cpp` and
   `include/marrow/**.h` must not appear in the diff.
3. **No validation tightened at L1/L2/L3.** The only new rejection is in
   `agent_handlers_constraints.cpp` (negative `softness`). If a task tempts you to
   add a range check to `project.cpp`, you are breaking backward compatibility —
   design §7.6.
4. **A widget's `[min,max]` equals the loader's bound, and only then may it carry
   `ImGuiSliderFlags_AlwaysClamp`.** Design §3. Never clamp a range narrower than
   the format's.
5. **Assert survival, not return codes.** Every persistence claim goes
   `save_project` → **`load_project`** → `build_project_runtime` and asserts the
   materialized runtime struct. `validate_project_for_save` has no base document
   and cannot see cross-references; a passing `save()` proves nothing.
6. **Every inverted gate must be shown to fail.** Task 9 lists them with the
   mechanism by which each is observable. An inversion that still passes means the
   test is wrong, not that the code is right.
7. **Do not run `marrow_editor_shell` without `--auto-close` or `--agent-port`.**
   `~/Library/Application Support/Marrow` must not be created; every smoke path
   installs `ScopedPreferenceIsolation` first (`shell_smoke.cpp:52`).

---

## File and responsibility map

| File | Change |
|---|---|
| `src/editor/shell_constraints.cpp` | 11 new widgets; `update_positive_value` → `update_magnitude`/`update_mix`; `AlwaysClamp` on 10 sliders; `Mix##physics` relocation |
| `src/editor/agent_handlers_constraints.cpp` | `edit_ik_constraint` restructured: merge-then-branch, 3 new args, `ik_constraint_preview`, live `scene_delta` |
| `tools/mcp/tools/editing.py` | 4 new properties on `edit_ik_constraint` |
| `src/samples/editor_project_smoke.cpp` | S1–S4 |
| `src/editor/shell_smoke_constraints.cpp` | `validate_constraint_parameter_shell_smoke()` (C1–C5) |
| `src/editor/shell_smoke_scenarios.hpp` | one declaration |
| `src/editor/shell_smoke.cpp` | one call site |
| `src/samples/agent_dispatch_smoke.cpp` | new IK cases; one replaced exact-delta |
| `tools/mcp/test_client.py` | IK dry-run → live → read-back → undo |
| `AGENTS.md`, `docs/root1/editing-gap-analysis.md` | evidence + one sentence |

**No `CMakeLists.txt` change.** `shell_smoke_constraints.cpp` is already a source
(`CMakeLists.txt:901`), and the new scenario lives in it.

---

## Task 0 — Re-read as-built MAR-178 and MEASURE (mandatory, no code)

Nothing below may be written until every line here has been run and its output
compared to the claim. The design's numbers were measured on 2026-08-30; confirm
they still hold.

```bash
cd /Users/kwon/Workspace/C/Maroow

# ---- 1. Registry: design §6.5 claims 64, split edit 39 / inspection 12 / management 10 / validation 3.
awk '/^constexpr OperationSpec kOperationSpecs\[\] = \{/,/^\};/' src/editor/agent_dispatch.cpp \
  | grep '^    {"' | sed 's/^    {"[^"]*", "\([a-z]*\)".*/\1/' | sort | uniq -c
awk '/^constexpr OperationSpec kOperationSpecs\[\] = \{/,/^\};/' src/editor/agent_dispatch.cpp \
  | grep -c '^    {"'
#   expect: 39 edit / 12 inspection / 10 management / 3 validation, total 64.
#   If this is not 64, STOP: every count site below and the whole "registry unchanged"
#   claim is invalid.

# ---- 2. Count sites: design §6.5 claims 10 code guards + 10 messages + 2 python.
grep -rn "operation_count_before != 64U\|64-operation" src/ tools/ | grep -v "uint64\|int64"
#   expect exactly: shell_smoke_constraints.cpp:138+:139;
#                   shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603 (+ msg one line below each);
#                   shell_smoke_timeline.cpp:3697+:3698
grep -n "== 64" tools/mcp/test_client.py
#   expect :53 and :55 only. (:1497/:1502 are mesh coordinates -64/64 — NOT counts.)
grep -n "std::array<OperationExpectation, 64>" src/samples/agent_dispatch_smoke.cpp
#   expect :39.
#   NONE of these move in MAR-179. Record the list; Task 8 re-runs it and diffs.

# ---- 3. The gap itself (design §0). Confirm before building anything.
grep -n "Softness\|Compress\|Stretch\|\"Step\"\|\"Limit\"\|Mass Inverse" src/editor/shell_constraints.cpp
#   expect: NO HITS. Any hit means a widget already exists and §0 is wrong.
grep -n "softness\|compress\|stretch" src/editor/agent_handlers_constraints.cpp
#   expect only :747-749 (the materializer) — no arg parsing, no preview key.
grep -n "softness\|compress\|stretch\|merge" tools/mcp/tools/editing.py | sed -n '1,20p'
#   expect no softness/compress/stretch anywhere near edit_ik_constraint (:856-870).

# ---- 4. Validation table (design §2). Spot-check the two rows the plan depends on.
grep -n "softness" src/runtime/skeleton_parse.cpp src/editor/project.cpp
#   expect: skeleton_parse.cpp:3149 and project.cpp:3169 are plain reads with NO
#   range check after them, and NO occurrence in validate_project_for_save.
#   If a range check exists, design §2.1/§5.3/§6.1-S3 are wrong — stop.
sed -n '3877,3891p' src/runtime/skeleton_parse.cpp
#   expect "physics step must be greater than zero" — S2 asserts this string verbatim.
grep -n "physics constraint edit numeric values must stay within their valid ranges" src/editor/project.cpp
#   expect :6096 area — S2 asserts this string verbatim.

# ---- 5. The ID assumption behind C1 (design §6.2, risk 1).
sed -n '1025,1035p' src/editor/shell_constraints.cpp
grep -n "PushID\|PopID" src/editor/shell_widgets.cpp | sed -n '1,6p'
#   expect widgets::seg_toggle with a BALANCED PushID/PopID (:140/:159) and NO
#   ImGui::BeginTabBar/BeginTabItem anywhere in shell_constraints.cpp:
grep -n "BeginTabBar\|BeginTabItem\|BeginChild" src/editor/shell_constraints.cpp
#   expect only the four "*_constraint_list" BeginChild calls, all closed with
#   EndChild BEFORE the parameter widgets. If a parameter widget is inside a
#   child or a pushed ID, C1's window->GetID(label) will not match — fix C1's
#   seed with ImGui::GetIDWithSeed before writing it, do not weaken the assertion.

# ---- 6. Coalescing precedent (design §5.1).
sed -n '34,54p' src/editor/shell_constraints.cpp
sed -n '37,95p' src/editor/shell_coalesced_edit.hpp
#   expect apply_constraint_project_drag -> apply_coalesced_edit_frame, and
#   deactivated_after_edit -> finalize_coalesced_edit (ONE history entry).

# ---- 7. A reload is a full materialization (global constraint 5).
sed -n '7490,7500p' src/editor/project.cpp
#   expect build_project_runtime around :7495 calling load_skeleton_data.
#   If it moved, S1/S3 no longer prove what they claim.

# ---- 8. Baselines to beat. Record all four numbers.
./build/marrow_agent_dispatch_smoke 2>&1 | tail -3
#   record the "[ OK ]" case count (AGENTS.md:330 records 404 as of MAR-178).
./build/marrow_project_smoke assets/fixtures/player_idle.marrow 2>&1 | tail -3
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2 2>&1 | tail -3
python3 -c "import json;d=json.load(open('assets/fixtures/player_idle.marrow'));\
c=d['constraint_edits'];print(c['ik'][0]);print(c['physics'][0])"
#   expect ik softness 0, compress false, stretch false;
#          physics step 0.0166666667, x 1, y 1, rotate 1, scaleX 0.35, shearX 0,
#          limit 30, massInverse 1. S1 overwrites these; record the originals.

# ---- 9. Panel order (design §4.4, risk 3).
grep -n '"Mix##physics"' src/editor/shell_constraints.cpp
#   expect :1903 (fourth scalar). Confirm nothing asserts panel row order:
grep -rn "Mix##physics" src/ --include=*smoke*
#   expect NO HITS. If something asserts it, §4.4's relocation needs its own step.
```

**Exit condition.** Every `expect` above matched, or the mismatch is written down
and the affected design section revised *before* Task 1. You may not build the
project in this task beyond running the four existing binaries above.

---

## Task 1 — `edit_ik_constraint` accepts and reports the three fields

Depends on: Task 0. The agent goes first because §4.5's checkboxes cannot work
until it does.

**TDD step.** `src/samples/agent_dispatch_smoke.cpp`. Add, near the existing IK
cases at `:2796-2825`:

- `edit_ik_constraint softness/compress/stretch dry-run` — invoke with
  `{"name":"editor_arm_reach","softness":12.5,"compress":true,"stretch":true,"dry_run":true}`
  and `expect_exact_scene_delta` against the nine-key payload of design §5.3.
- `edit_ik_constraint negative softness` — `{"name":…,"softness":-1}`, expect
  failure and the message `ik constraint softness must be non-negative.`
- `edit_ik_constraint live delta` — the same live invocation, asserting its
  `scene_delta` equals the dry run's apart from `"dry_run"`.
- Replace the expectation at `:2811-2816` from
  `{"dry_run":true,"name":"editor_arm_reach","mix":0.75}` with the full payload.

**Must fail first, and why it is observable at this layer:** the dry-run case
fails because today's handler short-circuits at `:690-711` and emits three keys —
`expect_exact_scene_delta` reports the missing six by name. The negative case
fails because today no `softness` argument is read at all, so the command
*succeeds*. Neither failure depends on any GUI code.

Build and run to see both fail:
```bash
cmake --build build --target marrow_agent_dispatch_smoke && ./build/marrow_agent_dispatch_smoke
```

**Implementation step.** `src/editor/agent_handlers_constraints.cpp`, the
`op == "edit_ik_constraint"` block at `:676-797`. Restructure per design §5.3:

1. Add a file-local `json::Value ik_constraint_preview(const IkConstraintEdit&, bool dry_run)`
   beside the traits structs, emitting the nine keys in the order
   `dry_run, name, bones, target, mix, bend_positive, softness, compress, stretch`
   using the existing `bool_value`/`string_value`/`number_value`/`string_array_value` helpers.
2. Move the merge above the `dry_run` branch: build `merged` from
   `project.find_ik_constraint_edit(*name)` if present, else from the runtime
   constraint via the code currently at `:735-750`, else return the existing
   `not_found` error.
3. Apply `target`, `bone_names`, `mix`, `bend_positive` to `merged` (the existing
   blocks, retargeted from `edit->` to `merged.`), then the three new ones with
   the same idiom:
   `"softness must be number or null."`, `"compress must be bool or null."`,
   `"stretch must be bool or null."`.
4. `if (merged.softness < 0.0) return make_error("ik constraint softness must be non-negative.", op, spec);`
5. `if (bool_arg(args, "dry_run")) return make_success("IK constraint edit validated.", op, spec, ik_constraint_preview(merged, true));`
6. Otherwise: open the transaction (unchanged flags), compute
   `json::Value live_delta = ik_constraint_preview(merged, false);`, upsert
   `merged` into `transaction.project()->ik_constraint_edits` (overwrite by name,
   else `push_back`), commit through the **unchanged**
   `CommitPolicy{"Failed to apply IK constraint edit: "}`, and return
   `make_success("Edited IK constraint successfully.", op, spec, std::move(live_delta))`.

**Do not** add `validate_bone_names` or any pre-transaction target check. Design
§5.3: the absence of that check is what keeps `edit_ik_constraint invalid target
rollback` (`:2796-2807`) the suite's only constraint commit-time rollback case.

**Verify.**
```bash
cmake --build build --target marrow_agent_dispatch_smoke && ./build/marrow_agent_dispatch_smoke
```
`agent_dispatch_smoke: PASSED`, with `edit_ik_constraint invalid target rollback`
still passing **unchanged** and the `[ OK ]` count above Task 0's baseline.

---

## Task 2 — the three IK widgets

Depends on: Task 1 (the checkboxes dispatch the arguments it added).

**TDD step.** `src/editor/shell_smoke_constraints.cpp`. Write
`validate_constraint_parameter_shell_smoke(const std::filesystem::path&)` with
C1 (IK half) and C2 and C4 from design §6.2. Declare it in
`src/editor/shell_smoke_scenarios.hpp` beside
`validate_constraint_lifecycle_shell_smoke`, and call it in
`src/editor/shell_smoke.cpp` immediately after that call at `:132`.

Structure, following `validate_constraint_lifecycle_shell_smoke:120-145` for the
preamble and `shell_smoke_frames.cpp:300-316` for the scroll-until-visible loop:

```cpp
const ScopedPreferenceIsolation isolation("constraint-parameters");
// ... require operation_count_before == 64U, same message shape as :138-140 ...
// ShellState + reload_project(project_path), select ik/editor_arm_reach.
// render_constraints_frame(): io.DeltaTime; ImGui::NewFrame();
//   DockSpaceOverViewport; ensure_default_dock_layout; draw_constraints_window;
//   ImGui::Render();
```

- **C1**: after one frame, `ImGuiWindow* w = ImGui::FindWindowByName(kConstraintsWindowTitle)`;
  build `targets = {w->GetID("Softness"), w->GetID("Compress"), w->GetID("Stretch")}`;
  sweep `io.AddMousePosEvent(x, y)` down `w->InnerClipRect` in 4px steps, one
  frame each, collecting `ImGui::GetCurrentContext()->HoveredId`; `SetScrollY`
  and re-sweep while `w->ScrollMax.y` allows. Assert every target was seen.
  **A label not found is `return false`**, never a skip.
- **C2**: press at the y where `Softness` was hovered, move +6px per frame for
  four frames, release. Assert `project->ik_constraint_edits[i].softness`
  changed, `undo_count()` == before + 1, and `undo()` restores
  `serialize_project()` byte-for-byte.
- **C4**: click `Compress`, then `Stretch`. Assert each flipped, each added
  exactly one history entry, and each redo restores it.

**Must fail first, and why it is observable:** C1 cannot find
`w->GetID("Softness")` in the hovered set because no widget with that label is
emitted, so no id equal to it can ever become `HoveredId`. This is a genuine
frame-level observation, not a proxy: deleting the widget later reproduces the
same failure. C2 and C4 fail for the same reason — there is nothing to press.

Run to see it fail:
```bash
cmake --build build --target marrow_editor_shell \
  && ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

**Implementation step.** `src/editor/shell_constraints.cpp`, IK branch.

1. At `:976-978`, add `kUnbounded`, `kMinPhysicsStep` and `kClamp` per design §4.1
   (`#include <limits>` if absent).
2. At `:1160-1166`, add `kClamp` as `SliderScalar`'s seventh argument.
3. After the Mix block (`:1180`), insert `Softness` as Form B, driven by
   `apply_constraint_project_drag` with
   `label = "Updated IK softness on " + selected_name`,
   `group = constraint_group(ConstraintKind::Ik, selected_name)`,
   `allow_merge = false`, `failure_status = "IK constraint edit failed"`, and the
   mutator writing `project->ik_constraint_edits[*edit_index].softness`.
4. After the `Bend Positive` checkbox (`:1194`), insert `Compress` then `Stretch`
   as the dispatching checkboxes of design §4.5.

**Verify.**
```bash
cmake --build build --target marrow_editor_shell \
  && ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```
Prints the frame line and the new scenario's pass line.

---

## Task 3 — the eight physics widgets, the two re-formed sliders, the order

Depends on: Task 2 (reuses the scenario harness and the constants).

**TDD step.** Extend `validate_constraint_parameter_shell_smoke` with the physics
half of C1 (eight labels), C3, and C5.

- Switch `state.constraints_tab = 3`, select `physics/editor_ribbon_secondary`,
  re-render, and sweep for `Step`, `X##physics`, `Y##physics`,
  `Rotate##physics`, `Scale X##physics`, `Shear X##physics`, `Limit`,
  `Mass Inverse`.
- **C3**: press-drag-release on `Step`; value changed, `undo_count()` + 1
  exactly, undo restores byte-for-byte.
- **C5**: Ctrl+click `Inertia`, `io.AddInputCharacter('5')`, `ImGuiKey_Enter`;
  assert the committed `inertia` is `1.0`. If the harness cannot drive that path,
  apply design §6.2's fallback — a project-layer assertion that `inertia = 5.0`
  makes `build_project_runtime` fail with
  `physics inertia must stay within [0, 1]` — **and record the substitution in
  `AGENTS.md`** in Task 8.

**Must fail first, and why it is observable:** the eight labels are not emitted,
so their ids cannot appear in `HoveredId`. C5 fails distinguishably in both
directions: with the flag missing, ImGui stores `5.0`, `rebuild_project_runtime`
rejects it at L1, `apply_coalesced_edit_frame` rolls back
(`shell_coalesced_edit.hpp:70-80`) and `inertia` stays at the fixture's `0.85` —
neither `5.0` nor `1.0`, so the assertion on `1.0` fails either way.

**Implementation step.** `src/editor/shell_constraints.cpp`, physics branch
`:1849-1968`.

1. Replace `update_positive_value` (`:1849-1876`) with two helpers over the same
   `apply_constraint_project_drag` call:
   - `update_magnitude(label, value, setter, lo, speed, format, status)` →
     `ImGui::DragScalar(label, ImGuiDataType_Double, &v, speed, &lo, &kUnbounded, format, kClamp)`
   - `update_mix(label, value, setter, status)` →
     `ImGui::SliderScalar(label, ImGuiDataType_Double, &v, &kZero, &kOne, "%.2f", kClamp)`
2. Emit the scalars in design §4.4's order, with §4.3's parameters:
   `Step`(mag, `kMinPhysicsStep`, .0005, `%.4f`), `X##physics`, `Y##physics`,
   `Rotate##physics`, `Scale X##physics`, `Shear X##physics` (mag, `kZero`, .01,
   `%.3f`), `Limit` (mag, `kZero`, 1.0, `%.2f`), `Inertia` (mix),
   `Damping` (mag, `kZero`, .05, `%.2f`), `Strength` (mag, `kZero`, .1, `%.2f`),
   `Mass Inverse` (mag, `kZero`, .01, `%.3f`), the four unchanged
   `update_force` calls, then `Mix##physics` (mix) **last**.
3. Status strings follow the existing wording: `"Updated physics <field> on " + selected_name`.

`kTen` and the `50.0` literal disappear with `update_positive_value`; remove
`kTen` if nothing else uses it (`grep -n kTen src/editor/shell_constraints.cpp`).

**Verify.** Same command as Task 2.

---

## Task 4 — `AlwaysClamp` on the seven path/transform sliders

Depends on: Task 3. Isolated and mechanical; kept separate so it is reviewable on
its own and so a regression bisects cleanly.

**TDD step.** None of its own. This task adds no behavior for any value the
loader accepts — design §3 — so a new test would assert nothing new. What guards
it is Task 3's C5 (the same flag, same mechanism, on `Inertia`) plus the
existing path/transform coverage in `marrow_project_smoke`, which must stay
green. State this in the commit rather than inventing a test that cannot fail.

**Implementation step.** Add `kClamp` as the trailing argument to exactly seven
`SliderScalar` calls, all already `[0,1]` against a `[0,1]` loader bound:

| File:line | Widget |
|---|---|
| `shell_constraints.cpp:1331` | Path `Position` |
| `shell_constraints.cpp:1401` | Path `Rotate Mix` |
| `shell_constraints.cpp:1425` | Path `Translate Mix` |
| `shell_constraints.cpp:1621` | Transform `Rotate Mix` |
| `shell_constraints.cpp:1628` | Transform `Translate Mix` |
| `shell_constraints.cpp:1635` | Transform `Scale Mix` |
| `shell_constraints.cpp:1642` | Transform `Shear Mix` |

**Do not touch** `Spacing` (`:1354`) — design §3 and §7.5: its `[0,1]` range is
narrower than the loader's `>= 0`, so clamping it would refuse valid values.

**Verify.**
```bash
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```
Both unchanged from Task 3.

---

## Task 5 — model-layer round-trip, boundaries, rollback, export

Depends on: Task 3 (widget ranges are settled; the assertions encode them).

**TDD step + implementation step are one edit here** —
`src/samples/editor_project_smoke.cpp` is a test binary. Add scenarios S1–S4 from
design §6.1 against `assets/fixtures/player_idle.marrow`.

- **S1** boundary round-trip, **`save_project` → `load_project` →
  `build_project_runtime`**, asserting on the materialized
  `IkConstraintData`/`PhysicsConstraintData`, never on `ProjectData`.
- **S2** invalid rollback: `step ∈ {0.0, -1.0}` and `mass_inverse = -1.0`;
  `build_project_runtime` fails carrying `physics step must be greater than zero`
  / `physics massInverse must be non-negative`; `save_project` fails with
  `physics constraint edit numeric values must stay within their valid ranges`;
  the project is unchanged after each.
- **S3** the `softness` asymmetry: a project with `softness = -3.0` loads,
  materializes, saves and **reloads**, and the materialized
  `IkConstraintData::softness` is `-3.0`.
- **S4** export equivalence via `--export-runtime` + `--export-binary`, re-parsing
  both and comparing the eleven fields.

**Must fail first, and why it is observable:** S1 fails on the current tree only
if it writes values the tree cannot round-trip — it can, because the model shipped
before this story. **So S1 alone is a test that cannot fail, and it is not the
gate.** Its job is regression protection for Tasks 2–4. The gates in this task are
S2 and S3, which *do* fail against a wrong implementation: invert
`project.cpp:3661`'s `edit.step <= 0.0` to `< 0.0` and S2's first case passes a
zero step through to a division-by-step in the physics integrator; delete
`skeleton_parse.cpp:3884`'s check and S2's `build_project_runtime` assertion
fails. Both inversions are run in Task 9. S3 fails the moment anyone "fixes" the
missing softness validation — which is exactly the regression it exists to catch.

**Verify.**
```bash
cmake --build build --target marrow_project_smoke
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
  --export-runtime /tmp/mar179_export.mskl --export-binary /tmp/mar179_export.mbin
./build/marrow_project_smoke --create /tmp/mar179_created.marrow
```

---

## Task 6 — MCP schema and client coverage

Depends on: Task 1.

**TDD step.** `tools/mcp/test_client.py`. Add an `edit_ik_constraint` sequence:
dry run with `softness`/`compress`/`stretch` → live → dry-run read-back showing
the new values → `undo` → dry-run read-back showing the originals. Assert the
negative-softness rejection reports
`ik constraint softness must be non-negative.` verbatim.

**Must fail first, and why it is observable:** the MCP server rejects the call
before it reaches C++ — the four properties are absent from the tool's
`inputSchema`, so the client's arguments are dropped or refused at the schema
boundary. Distinct from Task 1's failure, which was C++-side.

**Implementation step.** `tools/mcp/tools/editing.py:856-870`, add the four
properties of design §5.4. `required` stays `["name"]`.

**Verify.**
```bash
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
tools/mcp/venv/bin/python -m py_compile tools/mcp/*.py tools/mcp/tools/*.py
```
`mcp test_client: PASSED` with **64/64** name parity — both `== 64` assertions at
`test_client.py:53`/`:55` unchanged.

---

## Task 7 — full-suite regression

Depends on: Tasks 1–6.

```bash
cmake --build build -j
./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke --create /tmp/mar179_created.marrow
./build/marrow_project_smoke assets/fixtures/atlas_pack_smoke/atlas_pack_project.marrow \
  --export-runtime /tmp/mar179_atlas_export.mskl
./build/marrow_parameter_project_smoke
./build/marrow_agent_dispatch_smoke
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2
./build/marrow_c_smoke
cmake --build build --target marrow_constraint_warning_check
python3 -m json.tool assets/fixtures/ik_constraints.mskl > /dev/null
python3 -m json.tool assets/fixtures/physics_constraints.mskl > /dev/null
python3 -m json.tool assets/fixtures/skin_inherit_constraints.mskl > /dev/null
python3 -m json.tool assets/fixtures/path_transform_constraints.mskl > /dev/null
```

`marrow_c_smoke` proves the C ABI is untouched; the fixture smoke proves the
runtime constraint evaluation is unchanged.

---

## Task 8 — count sweep proof and documentation

Depends on: Task 7.

**Prove nothing moved.** Re-run Task 0 step 2 verbatim and diff against the
recorded list. The only permitted difference is **one** new
`operation_count_before != 64U` guard plus its message, inside the new scenario in
`shell_smoke_constraints.cpp` — ten code guards become eleven, and every literal
stays `64`.

```bash
git diff --stat
git diff -U0 | grep -E '^[+-].*\b(62|63|64|65)\b' | grep -v '^[+-].*uint64'
#   Every surviving line must be the new guard/message. A change to any OTHER
#   64 is a bug: AGENTS.md carries "t = 0.62" and "62 lines"; the theme carries
#   (51, 56, 64) and rgb(54,57,64); test_client.py:1497/:1502 carry mesh
#   coordinates -64/64; test_client.py:1484 says "the 62nd operation" (an
#   ordinal, not a count). None of them may appear in this diff.
```

**Documentation.**

- `docs/root1/editing-gap-analysis.md:95` — rewrite only the trailing clause
  *"누락 위젯(IK `softness`/`compress`/`stretch`, physics `step`/`x`/`y` 등)은
  MAR-179 범위로 남는다"* to record the gap as closed by MAR-179, naming the
  eleven widgets and the IK agent/MCP parity. Leave `:23`, `:85`, `:86`, `:192`,
  `:455` — all registry-count prose — **untouched**, because the count did not
  change.
- `AGENTS.md` — a MAR-179 evidence section in the house style: the measured
  gap, the widget/range table, the clamp rule of design §3, the registry-unchanged
  proof from the sweep above, the four run transcripts with their `[ OK ]` counts,
  and the inversion table from Task 9. If C5 took its fallback, say so explicitly
  and say what the weaker claim is.
- `AGENTS.md:165` — extend the shell-smoke feature list with the constraint
  parameter widget scenario, matching the existing comma-separated style.

---

## Task 9 — inversions

Depends on: Task 8. Run each, confirm the named failure, **restore**. Record the
observed message in `AGENTS.md`.

| # | Inversion | Fails | Why it is observable at that layer |
|---|---|---|---|
| 1 | Delete the `Softness` widget from `shell_constraints.cpp` | C1 (IK) | No item with that label is emitted, so `w->GetID("Softness")` can never equal `HoveredId` at any scanned position |
| 2 | Delete the `Step` widget | C1 (physics) | Same mechanism, other family — proves C1 is not IK-specific |
| 3 | Pass `allow_merge = true` on `Softness` | C2 | Consecutive coalesced groups merge into the previous entry; `undo_count()` grows by 0 on the second drag, not 1 |
| 4 | Drop `finalize_coalesced_edit` for the `Step` drag (return before `:85-89`) | C3 | The pending action is discarded, so the multi-frame drag commits **no** history entry; `undo_count()` unchanged and `undo()` reverts an older edit |
| 5 | Remove `kClamp` from `Inertia` | C5 | ImGui stores `5.0`; L1 rejects (`physics inertia must stay within [0, 1]`); `apply_coalesced_edit_frame` rolls back; the value stays `0.85` — distinguishable from the asserted `1.0` in both directions |
| 6 | `project.cpp:3661` `edit.step <= 0.0` → `< 0.0` | S2 | A zero step now loads, so `build_project_runtime` succeeds where S2 requires it to fail |
| 7 | Add `if (edit.softness < 0.0) return validation_error(...)` to `project.cpp:3171` | S3 | The `softness = -3.0` project no longer **reloads** — the exact backward-compatibility break design §7.6 forbids |
| 8 | Remove `softness` from `ik_constraint_preview` | agent dry-run + live cases | `expect_exact_scene_delta` reports the missing key by name in both payloads |
| 9 | Remove the `merged.softness < 0.0` guard | agent negative-softness case | The command succeeds and the project carries `-1`, where the case requires a rejection with the exact message |
| 10 | Convert `edit_ik_constraint` to `handle_constraint_edit<IkConstraintTraits>` | `edit_ik_constraint invalid target rollback` (`:2796-2807`) | `validate_bone_names` rejects **before** the transaction, so the `"Failed to apply IK constraint edit: "` commit-time prefix never appears — this is the measurement behind design §5.3's decision, and it must be seen |
| 11 | Remove one of the four new `edit_ik_constraint` properties from `editing.py` | `test_client.py` IK sequence | The argument is dropped at the schema boundary and the dry-run read-back shows the old value; `len(mcp_names) == 64` still passes, proving the check is about the property, not the tool count |

Inversion 10 is the one most likely to be skipped as "obviously true". Run it. The
MAR-178 retrospective records a specified gate that could not bite because the
destructor had already cancelled; the only defence is executing every row.

---

## Full verification checklist

```bash
cd /Users/kwon/Workspace/C/Maroow

# Build
cmake --build build -j

# Runtime and C ABI unchanged
./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
./build/marrow_c_smoke
cmake --build build --target marrow_constraint_warning_check

# Model layer: S1-S4
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke --create /tmp/mar179_created.marrow
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
  --export-runtime /tmp/mar179_export.mskl --export-binary /tmp/mar179_export.mbin
./build/marrow_project_smoke assets/fixtures/atlas_pack_smoke/atlas_pack_project.marrow \
  --export-runtime /tmp/mar179_atlas_export.mskl
./build/marrow_parameter_project_smoke

# Agent registry: 64, unchanged
./build/marrow_agent_dispatch_smoke

# Shell: C1-C5 plus every existing scenario
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2

# MCP: 64/64 parity, IK round-trip
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only \
  assets/fixtures/parameter_face_basic.marrow
tools/mcp/venv/bin/python -m py_compile tools/mcp/*.py tools/mcp/tools/*.py

# Fixtures still parse
python3 -m json.tool assets/fixtures/ik_constraints.mskl > /dev/null
python3 -m json.tool assets/fixtures/physics_constraints.mskl > /dev/null
python3 -m json.tool assets/fixtures/path_transform_constraints.mskl > /dev/null
python3 -m json.tool assets/fixtures/skin_inherit_constraints.mskl > /dev/null

# Count sweep: only the new guard+message moved
git diff -U0 | grep -E '^[+-].*\b(62|63|64|65)\b' | grep -v '^[+-].*uint64'

# Format boundary: these files must NOT appear
git diff --name-only | grep -E 'skeleton_parse\.cpp|binary\.cpp|include/marrow/.*\.h$|CMakeLists\.txt'
#   expect NO OUTPUT
```

**Done means:** every command above passes; all eleven inversions in Task 9 were
run, failed as predicted, and were restored; the registry is 64 before and after;
`AGENTS.md` records the measured evidence including any C5 substitution; and
`~/Library/Application Support/Marrow` still does not exist.
