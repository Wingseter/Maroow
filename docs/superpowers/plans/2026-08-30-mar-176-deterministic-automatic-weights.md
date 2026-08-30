# MAR-176 Deterministic Automatic Weights Implementation Plan

Design spec:
`docs/superpowers/specs/2026-08-30-mar-176-deterministic-automatic-weights-design.md`

Branch base: `feat/mar-168`. MAR-175 (`3a96cc5`) must be merged first
(`dependsOn`) and **is** consumed here — `canonicalize_mesh_weight_vertex`,
`setup_pose_bone_world_transforms`, `inverse_transform_point_safe`, and step 1
of `rebind_mesh_weight_vertex` are all load-bearing.

---

## Global Constraints

1. **Preflight-then-mutate.** `generate_mesh_weights()` copies `ProjectData`,
   resolves and validates everything against the candidate, then performs a
   single `*project = std::move(candidate)`. A rejection must leave
   `serialize_project()` **byte-identical**. Asserted, not assumed.
2. **The canonicalizer is the last step of every generated vertex.** MAR-176
   adds a producer, not a rule. No step of
   `canonicalize_mesh_weight_vertex()` is added, removed, reordered, or
   retuned, and `kMeshWeightEpsilon` / `kMaxMeshWeightInfluences` /
   `kMeshWeightSumTolerance` keep their values.
3. **UI-free math in `marrow_editor`.** The algorithm lives in
   `src/editor/mesh_weight_model.{hpp,cpp}`, which is in the static library so
   `src/tests/` can link it. `src/editor/shell_viewport.cpp` and
   `src/editor/shell_weight_paint.cpp` are in the **executable**
   (`CMakeLists.txt:873`) and can never hold shared logic — this is why §5.2's
   distance function is written fresh rather than shared with
   `shell_viewport.cpp:438-457`.
4. **Behaviour-preserving extractions get an inverted gate.** Task 1 changes no
   observable behaviour: MAR-175's fourteen `marrow_mesh_weight_model_tests`
   cases and every rebind assertion in the project, shell, agent, and MCP
   suites must pass **unchanged** before any new behaviour is written. If one
   moves, stop and find out why.
5. **Determinism is proved, not assumed.** Every accepted-case test runs the
   generator twice and compares with `memcmp`, and separately runs it with the
   candidate list reversed and compares with `memcmp`. At least one test
   asserts a **specific expected value** (`0.5` / `0.5`), not only
   self-consistency.
6. **Export is proved on a MUTATED project.** Mutate → `save_project()` →
   `export_runtime_assets()` → reload → assert decoded values. Asserting on an
   in-memory copy the exporter never saw does not count.
7. **Assert survival, not return codes.** Every scoped write reads back the
   vertices it did not name and an adjacent *attachment* edit and asserts both
   byte-identical.
8. **Guard the agent surface, not just the GUI.** Every rule in AC1–AC4 gets an
   assertion in `marrow_agent_dispatch_smoke`, not only in the shell smoke.
9. **No `.marrow` schema change, no `ProjectData` member, no version bump.**
   `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1 unchanged.
10. **No new tunable numeric constant.** No radius, no exponent, no smoothing
    parameter, no minimum-weight slider (spec §4.2).
11. **No hash-ordered container, no parallelism, no `sqrt`/`hypot`/`pow`/`fma`**
    anywhere on the generator's path (spec §7.2, §7.3).
12. **Do not run `marrow_editor_shell` without `--auto-close`**, and never in a
    way that could create `~/Library/Application Support/Marrow`.

---

## File and Responsibility Map

| File | Change |
|---|---|
| `src/editor/mesh_weight_model.hpp` | `BoneSetupSegment`; `bone_setup_segments()`; `point_segment_distance_squared()`; `setup_world_position_of_weight_vertex()` (extracted); `generate_mesh_weight_vertex()` |
| `src/editor/mesh_weight_model.cpp` | Implementations; `rebind_mesh_weight_vertex()` re-pointed at the extracted helper |
| `src/tests/mesh_weight_model_tests.cpp` | New cases (spec §14.1); the fourteen shipped cases untouched |
| `include/marrow/editor/authoring.hpp` | Additive: `generate_mesh_weights()` declaration |
| `src/editor/authoring.cpp` | `generate_mesh_weights()` implementation |
| `src/editor/agent_dispatch.cpp` | `{"mesh.generate_weights", ...}` after `:74` |
| `src/editor/agent_handlers_editing.cpp` | Fourth op in the weight block (`:2523-2712`): name in the `if`, `bones` parsing, one `apply_weight_edit` arm, one `CommitPolicy`, one conditional payload field |
| `src/editor/shell_state.hpp` | `WeightPaintSettings::candidate_bone_names` (`:257-263`) |
| `src/editor/shell_weight_paint.hpp` / `.cpp` | `generate_weights_command()` beside `:1057-1083` |
| `src/editor/shell_viewport_ui.cpp` | Candidate checklist + `Generate` button in the weight panel (`:1884-1975`) |
| `src/editor/shell_smoke_timeline.cpp` | New generate coverage in the weight block; the `61U` guard at `:3419` → `62U` |
| `src/editor/shell_smoke_graph.cpp` | Seven `61U` guards → `62U` |
| `src/samples/editor_project_smoke.cpp` | Primitive, survival, determinism, save round trip, export Cases A and B |
| `src/samples/agent_dispatch_smoke.cpp` | `:39` array size → 62; new expectation row; new cases |
| `tools/mcp/tools/editing.py` | New `types.Tool` after `:1104` |
| `tools/mcp/test_client.py` | `:50`, `:52` → 62; new coverage |
| `AGENTS.md`, `docs/root1/*.md`, `.agents/tasks/prd-marrow-runtime.json` | Counts, prose, closure |

No `CMakeLists.txt` change: `marrow_mesh_weight_model_tests` already exists
(`:646-658`) and already has its `add_test` entry (`:1210`).

---

## Tasks

### Task 0 — Reconcile with the as-built MAR-175 and MEASURE (mandatory)

No code. Produce a short written reconciliation before touching anything.

```bash
cd /Users/kwon/Workspace/C/Maroow

# 1. Registry count and the weight family's position, as built.
python3 - <<'PY'
import re, pathlib
src = pathlib.Path("src/editor/agent_dispatch.cpp").read_text()
block = src.split("constexpr OperationSpec kOperationSpecs[] = {",1)[1].split("\n};",1)[0]
names = re.findall(r'\{"([^"]+)"', block)
print("as-built registry count:", len(names))
print("weight family:", [(i,n) for i,n in enumerate(names) if "weight" in n or n.startswith("mesh.")])
PY

# 2. Guard counts.
grep -c '!= 61U' src/editor/shell_smoke_graph.cpp
grep -c '!= 61U' src/editor/shell_smoke_timeline.cpp
grep -n '61U\|61-operation' src/editor/shell_smoke_graph.cpp src/editor/shell_smoke_timeline.cpp

# 3. Every other hand-edited site.
grep -n 'OperationExpectation, ' src/samples/agent_dispatch_smoke.cpp
grep -n 'len(registry_names)\|len(mcp_names)' tools/mcp/test_client.py
grep -rn '\b61\b' AGENTS.md docs/root1/*.md

# 4. Did anything MAR-175 shipped drift?
git log --oneline -3
git show --stat 3a96cc5 | tail -30
```

**Gate A — counts.** Record the measured registry count, both guard counts, and
the full list of hand-edited sites. If the registry is not **61**, or the graph
guards are not **7**, or the timeline guard is not **1**, or
`agent_dispatch_smoke.cpp:39` is not `std::array<OperationExpectation, 61>`,
then the spec's §2.7 and §12 are stale — **use the measured numbers** and
correct the spec before proceeding.

**Gate B — the export model.** Build once and measure the baseline the export
assertions will be written against:

```bash
cmake -S . -B build && cmake --build build
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
  --export-runtime /tmp/mar176_baseline.mskl --export-binary /tmp/mar176_baseline.mbin
wc -c /tmp/mar176_baseline.mbin
python3 -c "import json;d=json.load(open('/tmp/mar176_baseline.mskl'));print(json.dumps(d['skins']['mesh_base']['body']['weights']))"
```

The spec predicts **4015 bytes** and **25 bytes per influence** (`18 + K`,
`K = 7`, spec §13, derived from MAR-175's measured `4065 → 4040 → 4015` chain).
If the measured baseline differs, **re-derive `K` from `src/runtime/binary.cpp`**
— object tag, member-count varint, four key-index varints, one `String` tag plus
the bone-name-index varint, three `Number` tags, three float32 — and correct
spec §13. Do **not** fit the constant to whatever the build reports.

**Gate C — the setup pose the runtime actually reports.** The spec's worked
example (§5.6) assumes exact `float32` bone origins. `AGENTS.md:295` records
that the runtime composes them in `float32` and the fixture's setup pose
"carries ~1e-6 of error". Measure the actual values before choosing any
tolerance:

```
Add a temporary print (or a scratch case in marrow_mesh_weight_model_tests) that
dumps setup_pose_bone_world_transforms() for player_idle: for root, spine,
arm_l, and pivot, print world_x/world_y with std::setprecision(17).
```

Record whether `spine` is exactly `(0, 50)` and `arm_l` exactly `(-30, 60)`.
Every tolerance in Tasks 2, 3, 5, and 6 is set from this measurement, **not**
from the phrase "~1e-6". The `0.5 / 0.5` acceptance value is exact regardless
(spec §7.4) and is the assertion to lean on if the origins turn out inexact.

Then re-read, at HEAD:

- `src/editor/mesh_weight_model.hpp` and `.cpp` in full.
- `src/editor/authoring.cpp:4133-4370` (the weight family).
- `src/editor/agent_handlers_editing.cpp:2523-2712`.
- `src/editor/shell_weight_paint.cpp:960-1106`.
- `src/editor/shell_viewport_ui.cpp:1884-1975`.
- `src/runtime/skeleton_skin.cpp:525-570` — confirm the weighted branch still
  reads `geometry.vertices` only for the count (spec D-2 depends on it).
- `include/marrow/runtime/skeleton.hpp:60-65` — confirm `BoneData` still has no
  `length` (spec D-1 depends on it).

Confirm each `file:line` the spec cites still points at what the spec says. Note
every drift in the reconciliation.

---

### Task 1 — Extract rebind's step 1 (INVERTED GATE)

Behaviour-preserving. **No new behaviour, no rule change, no message change.**

#### TDD step (RED)

There is nothing to make fail: the extraction is a refactor. The gate is
inverted — the *existing* suite is the test, and it must pass byte-identically.

Before touching anything, capture the baseline:

```bash
./build/marrow_mesh_weight_model_tests > /tmp/mar176_t1_before.txt
./build/marrow_project_smoke assets/fixtures/player_idle.marrow > /tmp/mar176_t1_proj_before.txt
./build/marrow_agent_dispatch_smoke > /tmp/mar176_t1_agent_before.txt
```

#### Implementation step (GREEN)

`src/editor/mesh_weight_model.hpp` / `.cpp`:

1. Add `setup_world_position_of_weight_vertex(skeleton, setup_transforms, vertex, double* x, double* y)`
   with the doc comment from spec §6.1.
2. Move the body of `rebind_mesh_weight_vertex()`'s step 1
   (`mesh_weight_model.cpp:236-293`, from the `resolved` loop through the
   `!std::isfinite(world_x)` guard) into it **verbatim** — same messages, same
   order, same `kMeshWeightSumTolerance` branch.
3. `rebind_mesh_weight_vertex()` calls it, keeps its own `resolved` vector for
   step 2's per-influence inverse (it needs the bone indices again), and is
   otherwise untouched.

Watch for the one subtlety: rebind's step 1 loop indexes
`vertex->influences[index]` in lockstep with `resolved[index]`. The extracted
function must resolve the bone indices itself and must reproduce that pairing
exactly.

#### Verification — the inverted gate

```bash
cmake --build build
./build/marrow_mesh_weight_model_tests | diff - /tmp/mar176_t1_before.txt
./build/marrow_project_smoke assets/fixtures/player_idle.marrow | diff - /tmp/mar176_t1_proj_before.txt
./build/marrow_agent_dispatch_smoke | diff - /tmp/mar176_t1_agent_before.txt
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

All three diffs must be **empty**. `Mesh weight model: 14 cases passed`
unchanged. If any rebind number moves, the extraction was not verbatim — revert
and redo it, do not adjust the expectation.

---

### Task 2 — Segments, distance, and the per-vertex generator

#### TDD step (RED)

Add to `src/tests/mesh_weight_model_tests.cpp`, all failing to compile first
(the functions do not exist), then failing to pass:

1. `bone_setup_segments` — root → point at its own origin; child → parent→own;
   local `(0,0)` → zero-length; an out-of-range parent index → point.
2. `point_segment_distance_squared` — projection before start clamps to start;
   after end clamps to end; interior projects; zero-length returns the point
   distance. Exact small integers only, so every intermediate is exact.
3. **The fixture table** (spec §5.6), all four vertices, candidates
   `{spine, arm_l}`, asserting the weights bit-exactly on `double` and the bind
   offsets against the values in §5.6 within the tolerance Gate C measured.
4. **`0.5 / 0.5`.** Vertex 2 → `spine` first at exactly `0.5`, `arm_l` at
   exactly `0.5`. Assert with `==` on `double`, not `require_near`. Repeat with
   the candidate list reversed and `memcmp` the results.
5. **Three-way tie.** Candidates `{root, spine, arm_l, pivot}` on vertex 0 →
   three identical weights ordered `root(0)`, `spine(1)`, `pivot(12)`, then
   `arm_l(2)` at `0.15248443413502624`.
6. **Coincident.** A vertex placed exactly on a candidate's segment → one
   influence, weight exactly `1.0`.
7. **Zero-length candidate** → its point distance, no rejection, no `NaN`.
8. **Single candidate** → one influence, weight exactly `1.0`.
9. **More than four candidates** → exactly the four nearest, identical for
   several permutations of the input list.
10. **Scale regression.** Build a synthetic skeleton at 1× and at 100× the
    fixture's coordinates and assert the two produce the same relative
    assignment (same bones in the same order; weights equal to within `1e-12`
    relative). Message must say: *"this case fails if the generator hands raw
    1/d^2 to the canonicalizer; the absolute 1e-6 drop is not scale-free."*
11. **Determinism.** Every accepted case above runs twice and the two influence
    vectors are compared with `memcmp`.
12. **Rejections**, each asserting the message **and** that `*vertex` is
    byte-unchanged (`memcmp`, because `NaN != NaN`): empty candidate list,
    unknown bone, repeated bone, singular setup transform, a vertex with no
    influences, a vertex whose existing weights sum to zero.
13. **Extraction check.** `setup_world_position_of_weight_vertex()` on the four
    fixture vertices returns the `V` values in spec §5.6.

Run: every new case must **fail** before the implementation lands. Record which
ones fail for the right reason.

#### Implementation step (GREEN)

`src/editor/mesh_weight_model.hpp` — add `BoneSetupSegment`,
`bone_setup_segments()`, `point_segment_distance_squared()`, and
`generate_mesh_weight_vertex()` with the signatures and doc comments from spec
§5.1–§5.3.

`src/editor/mesh_weight_model.cpp` — implement spec §5.3's nine steps, in that
order. Points the implementer must not get wrong:

- Step 2's distances go into a `std::vector<std::pair<double, std::size_t>>`
  built in candidate order, then `std::sort`ed with
  `(d2 asc, bone_index asc)`. **Not** `std::stable_sort` — the comparator is a
  total order, and using `stable_sort` would hide a duplicate-index bug.
- Step 4 tests `d2 == 0.0` exactly. There is no epsilon here (spec §5.2).
- Step 5 caps **before** step 7 normalizes (spec §5.5).
- Step 7 accumulates `S` left to right over the sorted top-`K`, in a `double`,
  in one loop. Do not use `std::accumulate` with an execution policy and do not
  reorder for "accuracy".
- Step 9 hands the influences to `canonicalize_mesh_weight_vertex()` and assigns
  `*vertex` **only** if it returns empty.
- No `sqrt`, no `hypot`, no `pow`, no `fma`, no unordered container.

#### Verification

```bash
cmake --build build
./build/marrow_mesh_weight_model_tests
```

Expect the shipped 14 plus the new cases; the fourteen shipped names and their
output must be unchanged. Print the measured setup-pose origins from Gate C in
one of the new cases so the tolerance choice is visible in the log.

**Teeth, proved by inversion.** Temporarily:

- delete step 7's normalization → the scale case (10) fails with the
  canonicalizer's *"must keep at least one positive influence"*;
- change the tie-break to descending bone index → cases 4 and 5 fail;
- replace `std::sort` with `std::stable_sort` and shuffle the candidate order →
  case 9's permutation assertions fail.

Restore each. Record all three in the reconciliation.

---

### Task 3 — `generate_mesh_weights()`, the project primitive

#### TDD step (RED)

Add to `src/samples/editor_project_smoke.cpp`:

- Scoped generate over `{0}` with `{spine, arm_l}`: vertex 0 gains `arm_l`;
  vertices `1`, `2`, `3` **byte-identical**; an adjacent attachment edit
  byte-identical.
- Unscoped generate touches all four; `affected_vertices` is ascending.
- **Determinism at project level.** Generate twice from the same starting
  project into two `ProjectData` copies and assert `serialize_project()` is
  byte-identical between them.
- **Idempotence is not asserted.** Generate on a previous generate's output,
  assert every weight is stable to a tolerance, and **print the measured
  maximum delta** with a note that this is the `float32`-transform asymmetry and
  not nondeterminism (spec §7.6).
- **A real `save_project()` round trip** after every accepted generate — the
  gate MAR-175's V1/V2 defects escaped.
- Save → reload → generate again → byte-identical project.
- Every rejection from spec §9.2's table leaves `serialize_project()`
  byte-identical: empty candidate list, unknown bone, repeated bone, singular
  candidate transform, an unweighted attachment, an out-of-range vertex index, a
  repeated vertex index, an empty explicit `vertices` array.

Run `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` — must
fail to compile, then fail.

#### Implementation step (GREEN)

`include/marrow/editor/authoring.hpp` — declare `generate_mesh_weights()` after
`rebind_mesh_weights()` (`:628-633`), with spec §8.1's doc comment.

`src/editor/authoring.cpp` — implement immediately after `rebind_mesh_weights()`
(`:4321-4370`), copying its shape exactly:

1. null-project guard;
2. `setup_pose_bone_world_transforms(skeleton)`; empty → *"The skeleton has no
   bones to generate weights against."*;
3. **candidate validation** (spec §8.2), all five rules, before anything else,
   producing a `std::vector<std::size_t>` sorted ascending;
4. `bone_setup_segments()` once per call;
5. `ProjectData candidate = *project;` and `ensure_weight_edit(...)`;
6. `edit.vertices.empty()` → *"mesh.generate_weights requires a weighted mesh
   attachment."*;
7. `resolve_weight_scope(scope, edit.vertices.size(), &resolved)`;
8. stage every scoped vertex through `generate_mesh_weight_vertex()`, bailing on
   the first rejection;
9. compare with the exact `weight_vertices_equal` and assign only what differs;
10. `result.changed` → single `*project = std::move(candidate)`.

The segments and setup transforms are built **once per call**, never per vertex.

#### Verification

```bash
cmake --build build
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke --create /tmp/mar176_created.marrow
./build/marrow_parameter_project_smoke assets/fixtures/parameter_face_basic.marrow
```

---

### Task 4 — `mesh.generate_weights` on the agent registry

#### TDD step (RED)

`src/samples/agent_dispatch_smoke.cpp`:

- `:39` → `std::array<OperationExpectation, 62>` and a
  `{"mesh.generate_weights", "edit", true, false, true}` row immediately after
  the `mesh.rebind_weights` row (`:83`). This alone makes the smoke fail against
  the 61-entry registry, which is the RED state.
- New cases: dry run → live → read-back through `mesh.describe` → undo →
  read-back asserting the pre-generate influences return bit-exactly.
- Two dry runs report identical `affected_vertices`.
- Dry runs change neither `project_revision()`, `undo_count()`, nor `dirty()`.
- Scoped `vertices: [0]` leaves `1`, `2`, `3` byte-identical and an authored
  weight edit on a **different** attachment byte-identical.
- Every rejection from spec §9.2 with its exact message and code, each followed
  by a proven-unchanged project.
- The three shipped weight operations' dry-run payloads
  (`:1164-1170`) and the `normalize_weights idempotent` row (`:2932-2941`) are
  **unchanged** — they must not gain `candidate_bone_count`.

#### Implementation step (GREEN)

`src/editor/agent_dispatch.cpp` — insert after `:74`:

```cpp
    {"mesh.generate_weights", "edit", true, false, true, true, &handle_editing_operation},
```

`src/editor/agent_handlers_editing.cpp`, the weight block:

- `:2523-2524` — add `|| op == "mesh.generate_weights"` to the `if`.
- After the attachment resolution, add a `requested_bones`
  (`std::vector<std::string>`) parse branch, gated on the op name, implementing
  spec §9.2's `bones` rules. `bones` is **required**; omission is a rejection.
- The `else if (const json::Value* scope = ...)` branch at `:2605` already
  parses `vertices` for the non-`set_vertex_weights` ops, so generate inherits
  the scope parsing for free — verify that reading, do not duplicate it.
- `apply_weight_edit` gains one arm calling `generate_mesh_weights(...)`.
- `build_payload` gains one conditional
  `if (op == "mesh.generate_weights") payload.emplace("candidate_bone_count", number_value(requested_bones.size()));`
  so the other three payloads are byte-identical (spec §9.4, D-13).
- Dry-run message: `"Mesh weight generation validated."`
- `CommitPolicy`: `NoChangeResult::Success` with
  `"Mesh weights already match the generated candidates."`
- Success message: `"Generated mesh weights successfully."`

**Count sites, `61 → 62`.** Apply with an asserting patch so a miscount aborts:

```bash
# expect 7 then 1, and fail loudly otherwise
test "$(grep -c '!= 61U' src/editor/shell_smoke_graph.cpp)" = "7" || { echo "GRAPH GUARD COUNT DRIFTED"; exit 1; }
test "$(grep -c '!= 61U' src/editor/shell_smoke_timeline.cpp)" = "1" || { echo "TIMELINE GUARD COUNT DRIFTED"; exit 1; }
sed -i '' 's/!= 61U/!= 62U/g; s/exact 61-operation registry/exact 62-operation registry/g' \
  src/editor/shell_smoke_graph.cpp src/editor/shell_smoke_timeline.cpp
test "$(grep -c '!= 62U' src/editor/shell_smoke_graph.cpp)" = "7" || { echo "GRAPH REWRITE FAILED"; exit 1; }
test "$(grep -c '!= 62U' src/editor/shell_smoke_timeline.cpp)" = "1" || { echo "TIMELINE REWRITE FAILED"; exit 1; }
```

Then by hand: `agent_dispatch_smoke.cpp:39`, `tools/mcp/test_client.py:50/:52`,
`AGENTS.md:161`, `docs/root1/editing-gap-analysis.md:23/:85/:86/:192/:455`,
`docs/root1/refector.md:20`'s final clause. **Do not touch** `AGENTS.md:311`,
`:313`, or `docs/root1/discription.md:53-55` — historical records (spec §12).

#### Verification

```bash
cmake --build build
./build/marrow_agent_dispatch_smoke
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

`agent_dispatch_smoke: PASSED` against the exact **62**-operation registry.

**Teeth.** Temporarily default `bones` to "every bone" when omitted → the
AC1 rejection case must fail. Restore.

---

### Task 5 — The GUI checklist and Generate command

#### TDD step (RED)

`src/editor/shell_smoke_timeline.cpp`, appended to the existing weight block
(`:1610`-~`1900`):

- The checklist starts empty; `Generate` rejects with the empty-candidate
  message and leaves `serialize_project()` byte-identical.
- Filling the checklist from a two-bone selection produces exactly those two
  candidates.
- Generate with a two-vertex FFD scope changes exactly those two; the other two
  are byte-identical.
- Generate with no selection changes all four.
- Exactly one history entry per Generate; undo restores the previous influences
  **bit-exactly**; redo re-applies them bit-exactly.
- A Generate whose scope changes nothing adds no history entry and does not
  dirty.
- **Pose independence.** Scrub to `attack@0.2` (the block already does this at
  `:1701`) and Generate; assert the result is **bit-identical** to generating at
  setup pose. This is the direct descendant of MAR-175's D9 defect.
- A candidate name that no longer resolves rejects and leaves the project
  byte-identical.
- **Cross-path identity.** The GUI command and `mesh.generate_weights` with the
  same candidates and scope produce a bit-identical `MeshWeightVertexEdit`.

Every shipped assertion in the block must keep passing unchanged.

#### Implementation step (GREEN)

`src/editor/shell_state.hpp:257-263` — `WeightPaintSettings` gains
`std::vector<std::string> candidate_bone_names;`. Not serialized, not in
history, not in `PreviewState`, not in `editor-settings.json` (spec §10.3).

`src/editor/shell_weight_paint.hpp` / `.cpp` — `generate_weights_command()`
beside `normalize_weights_command` / `rebind_weights_command` (`:1057-1083`),
routed through `run_weight_command` with label prefix `"Generated"` so the
one-transaction / one-history-entry / no-op-commits-nothing contract is
inherited rather than reimplemented.

`src/editor/shell_viewport_ui.cpp:1930-1950` — inside the existing
`BeginDisabled(!paint_target.has_value())` block, after the Normalize/Rebind
row: a `Generate` button (additionally disabled when the checklist is empty,
with the reason shown), then a `Candidate bones` child region with one
`ImGui::Checkbox` per bone **in skeleton order**, `All` / `None` /
`From selection` buttons, and a `candidates: %zu` readout. `From selection`
reads the `BoneSelection` items out of `state->selection.items()`
(`include/marrow/editor/selection.hpp:25-34`) once — it does not bind.

#### Verification

```bash
cmake --build build
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2
ls ~/Library/Application\ Support/ | grep -i marrow && echo "PREFERENCE LEAK" || echo "no preference dir"
```

---

### Task 6 — Export, proved on a mutated project

#### TDD step (RED)

`src/samples/editor_project_smoke.cpp`, using the Gate B baseline:

**Case A — count-changing.** Generate over vertex `0` only with
`{spine, arm_l}`, then `save_project()` → `export_runtime_assets()` → reload.

- Assert the `.mbin` grew by exactly the `18 + K` amount Gate B confirmed
  (predicted **25** bytes: `4015 → 4040`). The printed message must state the
  model — object tag, member count, four key varints, `String` tag plus
  bone-index varint, three `Number` tags, three float32 — not just the number.
- Assert both decoded `.mskl` weights bit-exactly:
  `spine 0.64945270839180458`, `arm_l 0.35054729160819531`, in that order.
- Assert no new string was interned: both bone names already appear in the
  baseline document.

**Case B — value-only.** Generate over vertex `2` only, same candidates.

- Assert the `.mbin` size is **identical** to the baseline, and print
  *"float32 is fixed width, so size is not a signal for a value-only weight
  edit; the decoded value and the influence order are."*
- Assert the decoded `.mskl` weights are exactly `0.5` and `0.5`, with `spine`
  **first** — the order flip from the fixture's `arm_l 0.7499999999999999,
  spine 0.25` is part of the assertion.
- Assert the bind offsets are unchanged from the fixture's, to the Gate C
  tolerance.

Both cases read back an adjacent unscoped vertex and an adjacent attachment edit
and assert them byte-identical. No assertion anywhere claims the float32 weights
sum to exactly `1.0f`.

#### Verification

```bash
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
  --export-runtime /tmp/mar176_export.mskl --export-binary /tmp/mar176_export.mbin
./build/marrow_inspect --compare /tmp/mar176_export.mbin /tmp/mar176_export.mskl
./build/marrow_fixture_smoke /tmp/mar176_export.mskl assets/fixtures/player_idle.matl
```

If the Case A delta is not the value Gate B's model predicts, **stop**: the
encoding model is wrong. Re-derive it from `src/runtime/binary.cpp:225-256` and
correct spec §13 rather than changing the assertion to match the build.

---

### Task 7 — MCP tool and parity

#### TDD step (RED)

`tools/mcp/test_client.py`:

- `:50` and `:52` → `62`, which fails until the tool exists.
- An explicit registry-metadata row for `mesh.generate_weights`
  (`edit`, mutating, not review, dry-run supported).
- Dry run → live → read-back → second application → undo → read-back, reporting
  the **measured** second-application stability rather than asserting
  bit-identity (spec §7.6).
- Candidate subsets: `{spine}` alone gives weight `1.0`; `{spine, arm_l}` gives
  the §5.6 values; vertex 2 gives exactly `0.5 / 0.5`.
- Rejections proving the advisory schema did not loosen the C++ gate: an empty
  `bones` array (forbidden by `minItems: 1`, so it must be sent deliberately), a
  duplicate bone name, an unknown bone, a missing `bones`, and an out-of-range
  vertex index.

#### Implementation step (GREEN)

`tools/mcp/tools/editing.py` — one `types.Tool` immediately after
`mesh.rebind_weights` (`:1082-1104`), with the schema from spec §9.5.
`bones` is in `required`.

#### Verification

```bash
tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py \
  tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only   # against parameter_face_basic
```

`62/62` exact C++/Python name parity. Verify the tool-removal negative by hand
once — deleting the new `types.Tool` must make the client fail on
`assert len(mcp_names) == 62` — then restore it.

---

### Task 8 — Cross-milestone sweep, documentation, closure

#### Sweep

```bash
# no stale 61 that states what the surface IS
grep -rn '\b61\b' AGENTS.md docs/root1/*.md src tools | grep -v 'IM_COL32\|rgb('

# the generator's forbidden dependencies
grep -n 'unordered_\|std::map\|sqrt\|hypot\|std::pow\|::fma\|execution::' src/editor/mesh_weight_model.cpp

# the constants MAR-175 owns are untouched
git diff 3a96cc5 -- src/editor/mesh_weight_model.hpp | grep -n 'kMeshWeightEpsilon\|kMaxMeshWeightInfluences\|kMeshWeightSumTolerance'
```

The first must return only historical sentences (`AGENTS.md:311`, `:313`,
`discription.md:53-55`). The second must return **nothing**. The third must show
no change to any constant's value.

Also confirm `git diff` is empty on `src/runtime/**`, `include/marrow/marrow_c.h`,
and `src/editor/preferences.cpp`.

#### Documentation

- `AGENTS.md`: `:161` → 62 with `mesh.generate_weights` named; `:165` gains the
  candidate checklist and Generate; a new MAR-176 validation table in the
  established shape with rows for the algorithm, the segment definition, the
  pre-normalization scale evidence, the `0.5/0.5` exact tie, determinism versus
  idempotence with the measured figure, the export cases, survival, the agent
  and MCP surface at 62, and compatibility.
- `docs/root1/discription.md`: a new MAR-176 paragraph in the §55 style
  (spec §16).
- `docs/root1/editing-gap-analysis.md`: `:23`, `:85`, `:86`, `:192`, `:455` →
  62; the weight-painting row gains automatic generation; the
  "weight bind/auto-weight" P1 line at `:461` marked done.
- `docs/root1/agent-control.md`: the new operation, its required `bones`
  argument, and the determinism note.
- `docs/root1/refector.md`: `:20`'s final clause → 62.
- `docs/root1/format-spec.md`: one sentence — generated weights use the same
  `mesh_edits.weights` shape, no schema change.

#### Closure

`.agents/tasks/prd-marrow-runtime.json`: MAR-176 `status: "done"`,
`completedAt: "<the actual date>"`.

Record explicitly in `AGENTS.md` what was **not** run: the interactive macOS
confirmation that the candidate checklist and the Generate button render and
respond in the viewport weight panel. The headless smoke drives the command
directly and cannot see layout.

---

## Full Verification Checklist

### Build and unit gates

- [ ] `cmake -S . -B build` configures clean
- [ ] `cmake --build build` with no new warnings
- [ ] `cmake --build build --target marrow_verify_third_party`
- [ ] `./build/marrow_mesh_weight_model_tests` → the shipped 14 cases unchanged
      plus the new ones; the measured setup-pose origins and the
      determinism-versus-idempotence note printed
- [ ] Task 2's three inversion tests each fail the case they should, and are
      restored
- [ ] `./build/marrow_unit_tests`
- [ ] `./build/marrow_timeline_model_tests`, `marrow_timeline_graph_model_tests`
- [ ] `./build/marrow_viewport_interaction_tests`, `marrow_selection_tests`
- [ ] `./build/marrow_preference_tests`, `marrow_windowing_tests`,
      `marrow_pen_input_tests`, `marrow_agent_socket_tests`
- [ ] `ctest --test-dir build` → all tests pass (22 as built; no target added)

### Project, storage, and export gates

- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- [ ] `./build/marrow_project_smoke --create /tmp/mar176_created.marrow`
- [ ] Scoped generate leaves unscoped vertices and an adjacent attachment edit
      **byte-identical**
- [ ] Two generates from the same starting project → byte-identical
      `serialize_project()`
- [ ] A generate on a previous generate's output is asserted **stable**, and the
      measured maximum delta is printed
- [ ] An actual `save_project()` succeeds after every accepted generate
- [ ] Save → reload → generate again → byte-identical project
- [ ] Every rejection leaves `serialize_project()` byte-identical
- [ ] Case A: `.mbin` grew by exactly the `18 + K` amount; both decoded weights
      bit-exact; no new string interned
- [ ] Case B: `.mbin` size identical; decoded weights exactly `0.5` / `0.5` with
      `spine` first; the "size is not a signal here" reasoning printed
- [ ] `./build/marrow_inspect --compare /tmp/mar176_export.mbin /tmp/mar176_export.mskl`
- [ ] `./build/marrow_fixture_smoke /tmp/mar176_export.mskl assets/fixtures/player_idle.matl`
- [ ] `./build/marrow_parameter_project_smoke assets/fixtures/parameter_face_basic.marrow`

### Shell gates

- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- [ ] Every shipped MAR-175 weight assertion in the block unchanged
- [ ] Empty checklist → `Generate` rejects, project byte-identical
- [ ] `From selection` fills the checklist once and does not bind
- [ ] Selected scope changes exactly the selected vertices
- [ ] One history entry per Generate; undo and redo restore bit-exactly
- [ ] A no-op Generate adds no history and does not dirty
- [ ] Generating at `attack@0.2` is **bit-identical** to generating at setup pose
- [ ] Cross-path identity: GUI command and `mesh.generate_weights` agree
      bit-exactly
- [ ] `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2`

### Agent and MCP gates

- [ ] `./build/marrow_agent_dispatch_smoke` against the exact **62**-operation
      registry
- [ ] The `mesh.generate_weights` row sits immediately after
      `mesh.rebind_weights`
- [ ] The three shipped weight operations' payloads are byte-identical — none
      gained `candidate_bone_count`
- [ ] `normalize_weights idempotent` (`no_change` + null scene delta) unchanged
- [ ] Missing `bones`, empty `bones`, unknown bone, repeated bone, non-string
      entry, singular candidate, unweighted attachment, bad vertex index — each
      with its exact message and code, each leaving the project unchanged
- [ ] Scoped generate leaves unscoped vertices and an adjacent attachment
      byte-identical
- [ ] Two dry runs report identical `affected_vertices`
- [ ] Dry runs change neither `project_revision()`, `undo_count()`, nor `dirty()`
- [ ] `tools/mcp/venv/bin/python -m py_compile` over all four MCP files
- [ ] `tools/mcp/venv/bin/python tools/mcp/test_client.py` → `62/62` parity
- [ ] The MCP tool-removal negative verified by hand once and restored
- [ ] `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only`
      unchanged

### Regression gates

- [ ] `./build/marrow_fixture_smoke` on `.mskl` and `.mbin`
- [ ] `./build/marrow_spine_import_smoke assets/fixtures/spine_import_sample.json assets/fixtures/spine_import_sample.atlas`
      — owl zero-weight and tank weighted-clipping regressions unchanged
- [ ] `./build/marrow_renderer_sample --skip-render assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl`
- [ ] `./build/marrow_c_smoke`, `marrow_psd_import_smoke`,
      `marrow_atlas_packer_smoke`, `marrow_bootstrap`
- [ ] `python3 -m json.tool assets/fixtures/player_idle.marrow > /dev/null`

### Compatibility gates

- [ ] `.mskl` v1 unchanged
- [ ] `.mbin` v2 unchanged (`src/runtime/binary.cpp:23`)
- [ ] `.marrow` schema unchanged — no field added, removed, or retyped in
      `mesh_edits.weights`
- [ ] C ABI v1 unchanged (`include/marrow/marrow_c.h`)
- [ ] `editor-settings.json` v1 unchanged — the candidate checklist is not
      persisted
- [ ] `ProjectData` gained no member
- [ ] `git diff` empty on `src/runtime/**`
- [ ] `kMeshWeightEpsilon`, `kMaxMeshWeightInfluences`, and
      `kMeshWeightSumTolerance` keep their values
- [ ] `grep` for `unordered_`, `std::map`, `sqrt`, `hypot`, `pow`, `fma`,
      `execution::` in `mesh_weight_model.cpp` returns nothing
- [ ] `~/Library/Application Support/Marrow` still does not exist

---

## Decisions Taken Under Ambiguity

Design-level decisions are in spec §15 (D-1 … D-14). Plan-level ones:

**P-1. The extraction (Task 1) is a separate task with an inverted gate, not a
line inside Task 2.** Extracting rebind's step 1 touches code that four shipped
suites assert on. Bundling it with new behaviour would make a moved rebind
number ambiguous between "the extraction was not verbatim" and "the generator is
wrong". MAR-175 used the same split and it is the reason its D9 fix was
attributable.

**P-2. No new test target.** `marrow_mesh_weight_model_tests` already exists and
already has a `ctest` entry. Adding cases to it keeps `ctest` at 22 and keeps
one place to look for weight-model behaviour.

**P-3. Task 4 lands the registry count before Task 5 and Task 7.** The count
touches nine files across C++, Python, and prose; doing it in one task with an
asserting patch is what makes a miscount abort rather than leave one stale guard
behind. Both later tasks then build on a consistent 62.

**P-4. The export task comes after the agent task, not with the primitive.**
Task 3 proves the primitive; Task 6 proves what the *exporter* writes. MAR-168
and MAR-171 both shipped an export gap by asserting on an in-memory copy, and
separating the tasks makes the mutate → save → export → reload sequence a
visible deliverable rather than a step someone might collapse.

**P-5. Gate C (measuring the setup pose) blocks tolerance selection, not the
design.** The `0.5 / 0.5` acceptance value is exact regardless of the `float32`
pose error (spec §7.4), so the story can be built and its primary assertion
written before Gate C returns. Only the bind-offset and non-tie weight
tolerances depend on it, and those must be set from the measurement rather than
from `AGENTS.md`'s "~1e-6" phrasing.

**P-6. The `61 → 62` prose edits are hand-applied, the guard edits are
scripted.** The guards are mechanical and identical, so a `sed` with a
before-and-after count assertion is safer than by hand. The prose sentences need
the is-versus-was judgement (spec §12), which a script cannot make — and
`docs/root1/editing-gap-analysis.md:86` is the site MAR-175's own spec missed,
so it is called out by line number in the map above rather than left to a grep.
