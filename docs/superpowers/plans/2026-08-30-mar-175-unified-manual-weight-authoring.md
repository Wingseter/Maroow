# MAR-175 Unified Manual Weight Authoring Implementation Plan

Design spec:
`docs/superpowers/specs/2026-08-30-mar-175-unified-manual-weight-authoring-design.md`

Branch base: `feat/mar-168` at `cf6a199`. MAR-174 must be merged first
(`dependsOn`), even though nothing in it is consumed here.

---

## Global Constraints

1. **Preflight-then-mutate.** Every project-level primitive copies
   `ProjectData`, resolves and validates everything against the candidate, then
   performs a single `*project = std::move(candidate)`. A rejection must leave
   `serialize_project()` **byte-identical**. This is asserted, not assumed.
2. **UI-free math in `marrow_editor`, ImGui only wires input.** The canonical
   rules live in `src/editor/mesh_weight_model.cpp`, which is in the
   `marrow_editor` static library so unit tests can link it.
   `src/editor/shell_weight_paint.cpp` is in the **executable**
   (`CMakeLists.txt:866`) and can never hold shared logic.
3. **Behaviour-preserving extractions get an inverted gate.** Task 1 changes no
   observable behaviour: the entire existing suite must pass **byte-identically**
   before any new behaviour is written. If a shipped assertion moves in Task 1,
   stop and find out why.
4. **Export is proved on a MUTATED project.** Mutate → `save_project()` →
   `export_runtime_skeleton()` → reload → assert decoded values. Asserting on an
   in-memory copy the exporter never saw does not count (MAR-168/MAR-171 lesson).
5. **Assert survival, not return codes.** Any test touching removal, merging, or
   scoping reads back neighbouring vertices and neighbouring attachment edits
   and asserts they are byte-identical (MAR-172 lesson).
6. **Guard the agent surface, not just the GUI.** Every canonical rule in AC2
   gets an assertion in `marrow_agent_dispatch_smoke`, not only in the shell
   smoke (MAR-172 lesson).
7. **No `.marrow` schema change, no `ProjectData` member, no version bump.**
   `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1 unchanged.
8. **Do not run `marrow_editor_shell` without `--auto-close`**, and never in a
   way that could create `~/Library/Application Support/Marrow`.

---

## File and Responsibility Map

| File | Change |
|---|---|
| `src/editor/mesh_weight_model.hpp` | **NEW.** Constants, canonicalization, runtime→edit conversion, setup-pose transforms, rebind |
| `src/editor/mesh_weight_model.cpp` | **NEW.** Implementation |
| `src/tests/mesh_weight_model_tests.cpp` | **NEW.** Unit tests |
| `CMakeLists.txt` | `:507` add the new source to `marrow_editor`; new `marrow_mesh_weight_model_tests` target mirroring `:631-644`; new `add_test` |
| `include/marrow/editor/authoring.hpp` | Additive: `MeshWeightTarget`, `MeshWeightResult`, three primitives |
| `src/editor/authoring.cpp` | Implement the three primitives |
| `src/editor/agent_dispatch.cpp` | Delete `normalize_weight_vertex` (`:1006-1020`) and `mesh_weight_edit_from_runtime` (`:974-1004`); re-point `ensure_mesh_weight_edit` (`:1022-1036`); add `mesh.rebind_weights` after `:72` |
| `src/editor/agent_dispatch_internal.hpp` | Drop the `normalize_weight_vertex` declaration (`:220`) |
| `src/editor/agent_handlers_editing.cpp` | Restructure the weight block (`:2533-2634`); add the rebind handler |
| `src/editor/shell_weight_paint.cpp` | Delete `normalize_mesh_weight_vertex_edit` (`:174-215`) and `build_mesh_weight_attachment_edit_from_runtime` (`:120-150`); route through the primitive; setup-pose bind; Replace mode |
| `src/editor/shell_weight_paint.hpp` | Replace-mode plumbing; Normalize/Rebind command entry points |
| `src/editor/shell_state.hpp` | `WeightPaintMode::Replace` (`:241-245`); cached setup transforms on `MeshWeightStrokeState` (`:350-358`) |
| `src/editor/shell_inspector.cpp` / `.hpp` | Editable active-vertex influence table (`:87-113`, `:270-305`) |
| `src/editor/shell_viewport_ui.cpp` | Replace radio, Normalize/Rebind buttons, readiness gates (`:1070-1076`) |
| `src/editor/project.cpp` | `4U` → `kMaxMeshWeightInfluences` at `:2128`, `:5532` |
| `src/editor/shell_smoke_timeline.cpp` | Extend the weight-paint block (`:1610`-~`1900`) |
| `src/editor/shell_smoke_graph.cpp` | Seven `60U` guards → `61U` |
| `src/samples/editor_project_smoke.cpp` | Weight primitive + export cases |
| `src/samples/agent_dispatch_smoke.cpp` | New expectation row; new cases |
| `tools/mcp/tools/editing.py` | New tool; `vertices` on `normalize_weights` |
| `tools/mcp/test_client.py` | `61`; new coverage |
| `AGENTS.md`, `docs/root1/*.md`, `.agents/tasks/prd-marrow-runtime.json` | Counts, prose, closure |

---

## Tasks

### Task 0 — Reconcile with the as-built MAR-173/MAR-174 and MEASURE (mandatory)

No code. Produce a short written reconciliation before touching anything.

```bash
cd /Users/kwon/Workspace/C/Maroow

# 1. Registry count, as built.
python3 - <<'PY'
import re, pathlib
src = pathlib.Path("src/editor/agent_dispatch.cpp").read_text()
block = src.split("constexpr OperationSpec kOperationSpecs[] = {",1)[1].split("\n};",1)[0]
names = re.findall(r'\{"([^"]+)"', block)
print("as-built registry count:", len(names))
print("weight family:", [n for n in names if "weight" in n or n.startswith("mesh.")])
PY

# 2. Guard count in shell_smoke_graph.cpp.
grep -c '!= 60U' src/editor/shell_smoke_graph.cpp
grep -n '60U\|60-operation' src/editor/shell_smoke_graph.cpp

# 3. Every other hand-edited site.
grep -n 'len(registry_names)\|len(mcp_names)' tools/mcp/test_client.py
grep -rn '\b60\b' AGENTS.md docs/root1/*.md | grep -v '60fps\|60 FPS\|1/60\|1.0/60'

# 4. Did MAR-174 touch anything here?
git log --oneline -5
git diff --stat HEAD~1 -- src/editor/ | tail -20
```

**Gate.** Record the measured registry count, the measured guard count, and the
list of hand-edited sites. If the registry is not 60 or the guard count is not
7, **the numbers in the spec's §9.1 and §10 are stale — use the measured ones**
and update the spec before proceeding. MAR-174 adds no operation; confirm that
by name.

Then re-read, at HEAD:

- `src/editor/shell_weight_paint.cpp` in full.
- `src/editor/agent_handlers_editing.cpp:2533-2634`.
- `src/editor/agent_dispatch.cpp:960-1036`.
- `src/editor/project.cpp:2085-2220` and `:5504-5568`.
- `src/editor/shell_smoke_timeline.cpp:1610-1910`.

Confirm each `file:line` the spec cites still points at what the spec says. Note
every drift in the reconciliation.

---

### Task 1 — Behaviour-preserving extraction into `mesh_weight_model` (INVERTED GATE)

This task introduces the new translation unit, moves the *shell* normalizer into
it **verbatim**, and re-points the shell's four call sites. **No rule changes.**
The agent normalizer is untouched in this task. The point is a clean baseline.

#### TDD step (RED)

Create `src/tests/mesh_weight_model_tests.cpp` and the
`marrow_mesh_weight_model_tests` target. Write the first case only:

```cpp
// Characterization of the CURRENT shell normalizer, moved verbatim.
// Six influences -> the four largest, descending, summing to 1.0.
```

Build it. It must **fail to compile** because `mesh_weight_model.hpp` does not
exist. That is the RED.

#### Implementation step (GREEN)

1. Create `src/editor/mesh_weight_model.{hpp,cpp}` in namespace
   `marrow::editor::mesh_weight_model`.
2. Move `normalize_mesh_weight_vertex_edit` (`shell_weight_paint.cpp:174-215`)
   into it **character-for-character**, renamed
   `canonicalize_mesh_weight_vertex_legacy`, still `void`, still with the
   `size() > 4U` guard, still `stable_sort`, still `kWeightEpsilon = 1e-6`.
   Do not "fix" anything.
3. Move `inverse_transform_point_safe` (`shell_weight_paint.cpp:56-78`) into it
   verbatim as a public helper. Rebind will need it in Task 3.
4. Add the source to `marrow_editor` at `CMakeLists.txt:507`; add the test
   target mirroring `:631-644`; add the `add_test` entry.
5. In `shell_weight_paint.cpp`, delete both moved functions and call the new
   ones at `:505`, `:548`, `:675`, and wherever `inverse_transform_point_safe`
   was used.

#### Verification — the inverted gate

```bash
cmake -S . -B build && cmake --build build
./build/marrow_mesh_weight_model_tests
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
    --export-runtime /tmp/mar175_t1.mskl --export-binary /tmp/mar175_t1.mbin
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_agent_dispatch_smoke
./build/marrow_unit_tests
```

**Every shipped assertion must pass with its existing numbers.** In particular
`shell_smoke_timeline.cpp:1787-1789` (`1.0 / 0.75 / 0.25`), `:1828` (`0.875`),
`:1829` (total `1.0`), `:1821` (one undo entry).

Additionally prove byte-identical export against a pre-Task-1 baseline:

```bash
git stash && cmake --build build && \
  ./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
    --export-runtime /tmp/mar175_base.mskl --export-binary /tmp/mar175_base.mbin
git stash pop && cmake --build build && \
  ./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
    --export-runtime /tmp/mar175_t1.mskl --export-binary /tmp/mar175_t1.mbin
cmp /tmp/mar175_base.mskl /tmp/mar175_t1.mskl && \
cmp /tmp/mar175_base.mbin /tmp/mar175_t1.mbin && echo "T1 byte-identical"
```

If `cmp` reports a difference, Task 1 was not behaviour-preserving. Stop.

---

### Task 2 — `canonicalize_mesh_weight_vertex`, the real rules

#### TDD step (RED)

Extend `src/tests/mesh_weight_model_tests.cpp` with the full case list from
spec §13.1. Every case asserts the **exact** resulting influence vector
bit-exactly on `double`, and every rejection case asserts the message *and* that
`*vertex` is byte-unchanged.

The cases that must fail against the legacy function:

| Case | Legacy result | Required |
|---|---|---|
| `[spine .5, spine .5]` | two entries kept | one `spine 1.0`, bind = weighted mean |
| `[a .5, b 0.0]` via the agent path | `b` kept at `0.0` | `b` dropped |
| `[b .3, a .3]`, `a` before `b` in skeleton | insertion order kept | `a` first (skeleton tie-break) |
| `[a 1, b 2, c 3]` (3 entries, ascending) | order kept, not sorted | descending `c, b, a` |
| `[a NaN, b 1]` | NaN survives, poisons the vertex | reject, vertex unchanged |
| `[a 0.0]` | cleared to empty | reject with the empty-result message |
| `canonicalize(canonicalize([1/3,1/3,1/3]))` | not asserted | **bit-identical** |

Build and run. Every new case must FAIL. Record which.

#### Implementation step (GREEN)

Replace `canonicalize_mesh_weight_vertex_legacy` with:

```cpp
std::string canonicalize_mesh_weight_vertex(
    const runtime::SkeletonData& skeleton,
    MeshWeightVertexEdit* vertex);
```

Implement spec §6.2's eight steps in order, on a local copy, committing to
`*vertex` only on success. Add `kMaxMeshWeightInfluences` and
`kMeshWeightEpsilon`. Sort with `std::sort` and a **total-order** comparator
(`weight` descending, then resolved bone index ascending) — not `stable_sort`;
the tie-break must not depend on input order. In step 8, skip the division when
the sum is already exactly `1.0`, which is what makes idempotence bit-exact.

Replace the editor-side `4U` literals with the constant at
`project.cpp:2128`, `project.cpp:5532`, and (in Task 5)
`agent_handlers_editing.cpp:2588`. Add a cross-reference comment at
`src/runtime/skeleton_parse.cpp:1575`; **do not** make `marrow_runtime` depend
on `marrow_editor`.

#### Verification

```bash
cmake --build build && ./build/marrow_mesh_weight_model_tests
```

Expect the new case count. Then the inverted gate again, on the *shell* only —
the brush now canonicalizes differently for lists of 4 or fewer (sorting), so
run:

```bash
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

If a shipped weight assertion moves, record the old value, the new value, and
which canonical step caused it, in the task notes. Do not adjust a number
without that record.

---

### Task 3 — Setup-pose transforms and `rebind_mesh_weight_vertex`

#### TDD step (RED)

Add to `mesh_weight_model_tests.cpp`, per spec §13.1's rebind block:

- `setup_pose_bone_world_transforms()` on the `player_idle` skeleton returns one
  transform per bone and does **not** disturb any live skeleton.
- Single-influence rebind round-trips the bind offset within `1e-9`.
- Two-influence rebind moves both offsets so that
  `S_a(x_a', y_a') == S_b(x_b', y_b')` within `1e-9`.
- A vertex whose bind offsets were computed from a *non-setup* pose (the D9
  defect, constructed by hand) is corrected by rebind to the setup-consistent
  values.
- A singular setup transform (`|det| <= 1e-8`) rejects with the named message
  and leaves the vertex byte-unchanged.
- Repeat rebind is **bit-identical** across runs; second application is stable
  to `1e-9` but is *not* asserted bit-exact. The test prints the reason:
  `BoneWorldTransform` is float32 (`skeleton.hpp:1009`), bind offsets are
  double, so `S(S^{-1}(V)) != V` bit-for-bit.
- Rebind changes no `weight`.

Build and run. All must fail — the functions do not exist.

#### Implementation step (GREEN)

Add to `mesh_weight_model.cpp`:

```cpp
std::vector<runtime::BoneWorldTransform> setup_pose_bone_world_transforms(
    const std::shared_ptr<const runtime::SkeletonData>& skeleton);

std::string rebind_mesh_weight_vertex(
    const runtime::SkeletonData& skeleton,
    const std::vector<runtime::BoneWorldTransform>& setup_transforms,
    MeshWeightVertexEdit* vertex);
```

`setup_pose_bone_world_transforms` builds a scratch `runtime::Skeleton`
(`skeleton.hpp:1311`), calls `set_to_setup_pose()` (`:1357`),
`update_world_transforms()` (`:1401`), and materializes
`bone_world_transforms()` (`:1417`). The live preview is never touched.

`rebind_mesh_weight_vertex` implements spec §6.4's two steps on a local copy,
using the moved `inverse_transform_point_safe`, then runs
`canonicalize_mesh_weight_vertex` on the result and commits only on success.

#### Verification

```bash
cmake --build build && ./build/marrow_mesh_weight_model_tests
./build/marrow_unit_tests && ./build/marrow_fixture_smoke
```

---

### Task 4 — The brush routes through the primitive; setup-pose bind; Replace

#### TDD step (RED)

Extend `src/editor/shell_smoke_timeline.cpp`'s weight-paint block:

- **Replace:** `strength = 1.0` drives the active bone to `1.0` in one sample;
  `strength = 0.4` yields `0.4`; falloff and pressure compose.
- **Setup-pose bind (D9):** scrub to the attack pose (the block already does
  this at `:1701`), paint a bone the vertex does **not** have, then read back
  the new influence's `(x, y)` and assert they equal the values the *setup* pose
  produces — bit-exactly against
  `setup_pose_bone_world_transforms()` applied through the primitive. Before
  this task the assertion must fail, because the shipped code inverts the
  *current* pose (`shell_weight_paint.cpp:487-491`).
- **Stroke atomicity:** a stroke that sweeps across a vertex whose result would
  be empty does not abort; the other vertices in the stroke still commit, and
  the stroke is still one history entry.

Build and run `marrow_editor_shell --auto-close 2`. The new assertions must fail.

#### Implementation step (GREEN)

1. `shell_state.hpp:241-245`: add `Replace` to `WeightPaintMode` after `Smooth`.
2. `shell_state.hpp:350-358`: add
   `std::vector<runtime::BoneWorldTransform> setup_transforms;` to
   `MeshWeightStrokeState`.
3. `shell_weight_paint.cpp:19-30`: `weight_paint_mode_name` returns `"Replace"`.
   `:684-706`: `weight_paint_stroke_label` returns
   `"Replaced <bone> weights on <attachment>"`.
4. `begin_weight_paint_stroke` (`:720-739`) fills
   `state->weight_paint_stroke.setup_transforms` **once per stroke**.
   `reset_weight_paint_stroke` (`:708-718`) clears it.
5. `apply_paint_weight_to_vertex` (`:450-512`): the new-influence branch
   (`:478-502`) inverts the **setup** transform against the **setup-world**
   vertex position derived from the vertex's existing influences, not
   `preview_skeleton->bone_world_transforms()` and not the current-pose
   `overlay.vertices[...].world_position`. The FFD-offset subtraction at
   `:479-486` is no longer needed and is removed — a setup-pose position carries
   no animation FFD by construction. Verify that claim against the smoke.
6. Add `apply_replace_weight_to_vertex`, a near-copy of
   `apply_paint_weight_to_vertex` differing only in
   `influence_it->weight = stamp_strength;` versus `+=`.
7. `apply_weight_paint_sample` (`:765-893`): add the `Replace` case to the
   switch at `:818-841`; add `Replace` to the bone-readiness gate at `:775-781`.
8. All four `apply_*` helpers call `canonicalize_mesh_weight_vertex` and return
   `false` on rejection (spec §7.1's snippet). Keep
   `mesh_weight_vertex_equal`'s `1e-6` change-detection tolerance.
9. Delete `build_mesh_weight_attachment_edit_from_runtime` (`:120-150`); call
   `mesh_weight_model::mesh_weight_edit_from_runtime` at `:783-786`.
10. `shell_viewport_ui.cpp`: add `Replace` to the brush-mode radio group and to
    `weight_mode_ready` (`:1070-1072`).

#### Verification

```bash
cmake --build build
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_mesh_weight_model_tests
```

All shipped weight assertions plus the new ones. Record any shipped number that
moved, with its cause.

---

### Task 5 — The three project-level primitives, and the agent handler restructure

Split into two halves; the first is behaviour-preserving.

#### Half A — extraction (behaviour-preserving)

##### TDD step (RED)

In `src/samples/editor_project_smoke.cpp`, add a case that calls the new
`set_mesh_vertex_weights()` primitive directly and asserts the result equals
what `set_vertex_weights` produces through the agent. It must fail to compile.

##### Implementation step (GREEN)

Add to `include/marrow/editor/authoring.hpp` (additive, after the timeline
block):

```cpp
struct MeshWeightTarget {
    std::string skin_name;
    std::string slot_name;
    std::string attachment_name;
};

struct MeshWeightResult : AuthoringResult {
    std::size_t vertex_count{0U};          // vertices in the attachment
    std::size_t scoped_vertex_count{0U};   // vertices this call addressed
    std::vector<std::size_t> affected_vertices;  // ascending
};

/// Replaces the named vertices' influence lists. `vertices` maps vertex index
/// to its intended (pre-canonical) influence list. Every entry is canonicalized
/// before anything is written; one rejection rejects the whole call.
MeshWeightResult set_mesh_vertex_weights(
    ProjectData* project,
    const runtime::SkeletonData& skeleton,
    const MeshWeightTarget& target,
    const std::vector<std::pair<std::size_t, MeshWeightVertexEdit>>& vertices);

/// Canonicalizes existing vertices in place. Empty `scope` means every vertex.
MeshWeightResult normalize_mesh_weights(
    ProjectData* project,
    const runtime::SkeletonData& skeleton,
    const MeshWeightTarget& target,
    const std::vector<std::size_t>& scope);

/// Re-expresses bind offsets in each bone's setup frame. Empty `scope` means
/// every vertex. Never changes which bones influence a vertex, never changes a
/// weight, never touches topology.
MeshWeightResult rebind_mesh_weights(
    ProjectData* project,
    const runtime::SkeletonData& skeleton,
    const MeshWeightTarget& target,
    const std::vector<std::size_t>& scope);
```

Implement in `src/editor/authoring.cpp` with the standard shape:
`ProjectData candidate = *project;` → resolve the attachment → materialize via
`ensure_mesh_weight_edit` on the candidate → validate and canonicalize every
scoped vertex → `*project = std::move(candidate)` on success only.

Then rewrite `agent_handlers_editing.cpp:2533-2634` to call them, per spec §7.2:
parse everything into locals first, canonicalize, then open the transaction. Add
the explicit `transaction.cancel()` on every error path. Move the `dry_run`
early-return (`:2549-2553`) to **after** validation. Replace `4U` at `:2588`
with `kMaxMeshWeightInfluences`.

Delete `normalize_weight_vertex` (`agent_dispatch.cpp:1006-1020`) and its
declaration (`agent_dispatch_internal.hpp:220`). Delete
`mesh_weight_edit_from_runtime` (`agent_dispatch.cpp:974-1004`) and re-point
`ensure_mesh_weight_edit` (`:1022-1036`) at the shared converter.

##### Verification

```bash
cmake --build build
./build/marrow_agent_dispatch_smoke
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

`agent_dispatch_smoke`'s shipped rows must still pass, **including**
`normalize_weights idempotent` (`:2932-2941`) with its `no_change` message and
null scene delta, and the dry-run rows (`:1164-1170`).

#### Half B — the canonical rules on the agent surface

##### TDD step (RED)

In `src/samples/agent_dispatch_smoke.cpp`, add the cases from spec §13.4:
duplicate-bone merge, zero-weight drop, NaN rejection with a byte-identical
project, `"normalize": false` rejection, `normalize_weights` with `vertices: [1]`
leaving `0/2/3` byte-identical, and the adjacent-attachment survival read-back.

Run. The merge, drop, NaN, and `normalize:false` cases must fail.

##### Implementation step (GREEN)

The primitives from Half A already enforce the rules; the remaining work is:

- `set_vertex_weights`: reject an explicit `"normalize": false` with
  `invalid_request` and the message *"normalize:false is no longer supported;
  weight writes are always canonicalized."* (spec §12, C1). Absent and `true`
  behave as before.
- `normalize_weights`: accept the optional `vertices` array with the index
  validation from spec §9.2's table.
- Both: return the `scoped_vertex_count` / `affected_vertices` / `changed`
  payload on **both** the dry-run and the live path (spec §9.4), keeping
  `vertex_count`'s shipped meaning and position.

##### Verification

```bash
cmake --build build && ./build/marrow_agent_dispatch_smoke
```

---

### Task 6 — `mesh.rebind_weights`

#### TDD step (RED)

1. `src/samples/agent_dispatch_smoke.cpp:82-83`: add the expectation row
   `{"mesh.rebind_weights", "edit", true, false, true}` after
   `normalize_weights`. Update the registry total to the Task-0 measured count
   plus one.
2. Add the dry-run → live → read-back → `no_change` → undo → read-back sequence
   and every rejection from spec §9.2's table, each followed by a
   byte-identical-project assertion.

Run. The registry-count assertion and every new case must fail.

#### Implementation step (GREEN)

1. `src/editor/agent_dispatch.cpp`: insert
   `{"mesh.rebind_weights", "edit", true, false, true, true, &handle_editing_operation},`
   immediately after `:72`.
2. `src/editor/agent_handlers_editing.cpp`: extend the weight block's `op` test
   to include `mesh.rebind_weights` and dispatch to `rebind_mesh_weights()`.
   Success: `"Rebound mesh weights successfully."` No-change: the
   `normalize_weights` `CommitPolicy` shape with
   `"Mesh weights already bound to the setup pose."`
3. `src/editor/shell_smoke_graph.cpp`: all **seven** `!= 60U` guards and their
   seven message strings → `61U` / `"61-operation"`. Write the patch as an
   asserting `sed` so a miscount aborts:

```bash
n=$(grep -c '!= 60U' src/editor/shell_smoke_graph.cpp)
test "$n" -eq 7 || { echo "guard count changed: $n (expected 7)"; exit 1; }
sed -i '' 's/!= 60U/!= 61U/g; s/exact 60-operation registry/exact 61-operation registry/g' \
    src/editor/shell_smoke_graph.cpp
test "$(grep -c '!= 61U' src/editor/shell_smoke_graph.cpp)" -eq 7 || exit 1
```

#### Verification

```bash
cmake --build build
./build/marrow_agent_dispatch_smoke
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

---

### Task 7 — The three GUI surfaces

#### TDD step (RED)

Extend `src/editor/shell_smoke_timeline.cpp`'s weight block with spec §13.3's
new coverage for the numeric table, selected-scope Normalize, and Rebind,
including:

- Exactly one history entry per committed numeric field.
- The renormalized redisplay after typing a pre-normalization value.
- Add-influence disabled at four; removing the last influence refused.
- Normalize over two selected vertices leaves the other two **byte-identical**.
- Rebind changes `x`/`y`, changes no `weight`, changes no `triangles` / `uvs` /
  base `vertices`; undo restores the offsets bit-exactly.
- The **AC1/AC6 cross-path identity test**: brush, numeric table, and
  `set_vertex_weights` driven to the same intended influence set produce a
  bit-identical `MeshWeightVertexEdit`.
- Each of AC4's four rejection classes leaves `serialize_project()`
  byte-identical and `undo_count()` unchanged.

Run. All must fail.

#### Implementation step (GREEN)

1. `shell_inspector.cpp:87-113` and `:270-305`: make the Mesh Weights table
   editable for the single active vertex (spec §8.2). Resolve the active vertex
   from `state.viewport_ffd_selection` when it holds exactly one index and its
   `ViewportFfdSelectionScope` matches `current_mesh_weight_paint_target`;
   otherwise render read-only with a reason. `InputDouble` per weight,
   `IsItemDeactivatedAfterEdit()` commit, one `set_mesh_vertex_weights` call per
   commit, grouped through `shell_coalesced_edit.hpp`. Bind `x`/`y` read-only.
   Add-influence combo; remove button; `Σ 1.000` line.
2. `shell_weight_paint.{hpp,cpp}`: add UI-free
   `normalize_weights_command(ShellState*)` and
   `rebind_weights_command(ShellState*)` that resolve the scope, call the
   primitive inside one `EditorSession::EditTransaction`, and record one history
   entry labelled `"Normalized weights on <attachment>"` /
   `"Rebound weights on <attachment>"`. A no-op scope neither dirties nor adds
   history.
3. `shell_viewport_ui.cpp`: add the two buttons to the weight panel, enabled
   only when a weight target resolves.

#### Verification

```bash
cmake --build build
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_selection_tests && ./build/marrow_viewport_interaction_tests
```

---

### Task 8 — Export, proved on a mutated project

#### TDD step (RED)

In `src/samples/editor_project_smoke.cpp`, add the two export cases from spec
§13.2, plus the survival and round-trip cases:

- **Case A (topology-changing).** Mutate a vertex from 4 influences (one
  duplicate, one zero) down to 2 through `set_mesh_vertex_weights` →
  `save_project()` → `export_runtime_skeleton()` + binary → reload. Assert the
  `.mbin` **shrank** by the computed amount, assert the two surviving decoded
  weights, and print the reasoning.
- **Case B (value-only).** Renormalize a vertex whose influence *set* is
  unchanged → same pipeline. Assert the `.mbin` size is **identical**, assert
  the decoded value as `static_cast<float>(expected)`, and print:
  *"size is not a signal for a value-only weight edit; the decoded value is."*
  Do **not** assert the float32 weights sum to `1.0f`.
- **Survival.** Every mutation case reads back an adjacent vertex and an
  adjacent attachment edit and asserts both are byte-identical.
- **Round trip.** Canonical output is a fixed point of the `.marrow` loader:
  save → reload → canonicalize again → bit-identical.
- **Negative.** A hand-built project with a duplicate bone is rejected by
  `save_project()` with the exact message from `project.cpp:5554` — the V1/V2
  regression guard.
- **`"<unresolved>"` guard.** Assert no canonical write path stores a
  `skin_name` that `find_skin_attachment_value` cannot resolve (spec §2.5).

Run. All must fail.

#### Implementation step (GREEN)

Test-only in the normal case. If Case A's size arithmetic does not match,
recompute it from `src/runtime/binary.cpp`'s encoding (`Number` tag byte +
4-byte float32; arrays length-prefixed varint) rather than relaxing the
assertion to "smaller".

#### Verification

```bash
cmake --build build
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
    --export-runtime /tmp/mar175_export.mskl --export-binary /tmp/mar175_export.mbin
./build/marrow_inspect --compare /tmp/mar175_export.mbin /tmp/mar175_export.mskl
./build/marrow_fixture_smoke /tmp/mar175_export.mskl assets/fixtures/player_idle.matl
./build/marrow_project_smoke --create /tmp/mar175_created.marrow
```

---

### Task 9 — MCP tools and parity

#### TDD step (RED)

In `tools/mcp/test_client.py`: change `:49` and `:51` to `61`; add the
registry-metadata row for `mesh.rebind_weights`; add its dry-run → live →
read-back → undo → read-back sequence; add `normalize_weights` with and without
`vertices`; add the rejections from spec §13.5, including the five-influence
`set_vertex_weights` that proves the advisory `maxItems: 4` did not loosen the
C++ gate, and `"normalize": false`.

Run against a live shell. The parity assertion must fail (Python has 60 tools,
C++ has 61).

#### Implementation step (GREEN)

`tools/mcp/tools/editing.py`: add the `mesh.rebind_weights` `types.Tool`
immediately after `normalize_weights` (`:1052-1065`); add
`"vertices": {"type": "array", "items": {"type": "number", "minimum": 0}}` to
`normalize_weights`; add the C1 description to `set_vertex_weights`'s
`normalize` property. Do not change `_influence_schema()` (`:225-235`).

#### Verification

```bash
tools/mcp/venv/bin/python -m py_compile \
    tools/mcp/server.py tools/mcp/test_client.py \
    tools/mcp/tools/editing.py tools/mcp/tools/inspection.py

./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
sleep 3
tools/mcp/venv/bin/python tools/mcp/test_client.py
kill %1
```

Expect `mcp test_client: PASSED` with `61/61`. Then the negative, once, by hand:
delete the new `types.Tool` and confirm the client fails on
`assert len(mcp_names) == 61`; restore it.

```bash
./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --agent-port 9876 &
sleep 3
tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only
kill %1
```

---

### Task 10 — Cross-milestone sweep, documentation, closure

#### Sweep

Confirm MAR-175 did not disturb the timeline chain or the runtime:

```bash
./build/marrow_timeline_model_tests
./build/marrow_timeline_graph_model_tests
./build/marrow_unit_tests
./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
./build/marrow_fixture_smoke assets/fixtures/player_idle.mbin assets/fixtures/player_idle.matl
./build/marrow_spine_import_smoke assets/fixtures/spine_import_sample.json \
    assets/fixtures/spine_import_sample.atlas
./build/marrow_renderer_sample --skip-render assets/fixtures/player_idle.mskl \
    assets/fixtures/player_idle.matl
./build/marrow_c_smoke
```

`marrow_spine_import_smoke` carries the owl zero-weight weighted-mesh and tank
weighted-clipping regressions (`AGENTS.md:120`) and is the primary guard that
canonicalization did not change importer behaviour. The importer does **not**
route through the editor primitive; if this smoke moves, something leaked.

#### Documentation

Apply spec §15 exactly. Use the Task-0 measured count. Check every `60` before
changing it against §10's false-positive list: `IM_COL32(56, 61, 69, 255)`,
`(51, 56, 64)`, `"x": 56.0`, `56,995,840`, `PhysicsBoneState … 56 bytes/bone`,
`IM_COL32(208,134,57,230)`, `rgb(54,57,64)`, `shell_smoke_graph.cpp:2810`'s
`4360U`, and every `60 FPS` / `1.0/60.0`.

Historical sentences that **stay at their old number**:
`docs/root1/discription.md:44` (MAR-164's "56-operation") and `:53` (MAR-173's
"정확히 60 operation이 됐다"); the 44/49/56/57/58/59 recital in
`docs/root1/refector.md:20` up to its final "**current**" clause.

`AGENTS.md` gains a MAR-175 validation table in the shape MAR-173 used, and the
C1/C2 migration note.

#### Closure

`.agents/tasks/prd-marrow-runtime.json`: MAR-175 `status: "done"`,
`completedAt: "2026-08-30"`.

---

## Full Verification Checklist

### Build and unit gates

- [ ] `cmake -S . -B build` configures clean
- [ ] `cmake --build build` with no new warnings
- [ ] `cmake --build build --target marrow_verify_third_party`
- [ ] `./build/marrow_mesh_weight_model_tests` → all canonical, rejection,
      idempotence, and rebind cases pass; the float32-narrowing note is printed
- [ ] `./build/marrow_unit_tests`
- [ ] `./build/marrow_timeline_model_tests`
- [ ] `./build/marrow_timeline_graph_model_tests`
- [ ] `./build/marrow_viewport_interaction_tests`
- [ ] `./build/marrow_selection_tests`
- [ ] `./build/marrow_preference_tests`
- [ ] `./build/marrow_windowing_tests`, `./build/marrow_pen_input_tests`
- [ ] `./build/marrow_agent_socket_tests`

### Project, storage, and export gates

- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- [ ] `./build/marrow_project_smoke --create /tmp/mar175_created.marrow`
- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/mar175_export.mskl --export-binary /tmp/mar175_export.mbin`
- [ ] Case A: `.mbin` **shrank** by the computed amount; both surviving weights
      decoded and asserted
- [ ] Case B: `.mbin` size **identical**; the decoded float32 weight asserted;
      the "size is not a signal here" reasoning printed
- [ ] Adjacent vertices and the adjacent attachment edit byte-identical after
      every accepted mutation
- [ ] Every rejection leaves `serialize_project()` byte-identical
- [ ] Save → reload → re-canonicalize is a bit-identical fixed point
- [ ] A hand-built duplicate-bone project is refused by `save_project()`
- [ ] `./build/marrow_inspect --compare /tmp/mar175_export.mbin /tmp/mar175_export.mskl`
- [ ] `./build/marrow_fixture_smoke /tmp/mar175_export.mskl assets/fixtures/player_idle.matl`
- [ ] `./build/marrow_parameter_project_smoke assets/fixtures/parameter_face_basic.marrow`

### Shell gates

- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- [ ] Shipped weight assertions unchanged: `1.0 / 0.75 / 0.25`, the
      blue/green/yellow/red ramp, Paint `0.75 → 0.875`, per-vertex total `1.0`,
      one undo entry per stroke, Erase, Smooth, undo/redo
- [ ] Replace at `1.0` and `0.4`; falloff and pressure compose
- [ ] New-influence bind offsets come from the **setup** pose while scrubbed off
      setup pose
- [ ] Numeric table: one history entry per commit; renormalized redisplay;
      add disabled at four; last-influence removal refused
- [ ] Selected-scope Normalize: unselected vertices byte-identical; no-op scope
      adds no history and does not dirty
- [ ] Rebind: `x`/`y` change; no `weight` changes; `triangles`/`uvs`/base
      `vertices` unchanged; undo restores offsets bit-exactly
- [ ] AC1/AC6 cross-path identity: brush, numeric table, and
      `set_vertex_weights` produce a bit-identical vertex
- [ ] AC4: all four rejection classes leave the project byte-identical and
      `undo_count()` unchanged
- [ ] `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2`

### Agent and MCP gates

- [ ] `./build/marrow_agent_dispatch_smoke` against the exact **61**-operation
      registry
- [ ] The `mesh.rebind_weights` expectation row sits immediately after
      `normalize_weights`
- [ ] Shipped rows unchanged: both dry-runs, `normalize_weights idempotent`
      (`no_change` + null scene delta), `"Edited mesh weights successfully."`
- [ ] Duplicate-bone merge, zero-weight drop, NaN rejection, `>4` rejection
- [ ] `"normalize": false` rejects with `invalid_request` (C1)
- [ ] `normalize_weights` with `vertices: [1]` leaves `0/2/3` byte-identical
- [ ] `mesh.rebind_weights` dry-run/live/read-back/`no_change`/undo, and every
      rejection with a proven-unchanged project
- [ ] Adjacent-attachment survival read-back after an accepted mutation
- [ ] Dry runs change neither `project_revision()`, `undo_count()`, nor `dirty()`
- [ ] `tools/mcp/venv/bin/python -m py_compile ...` on all four MCP files
- [ ] `tools/mcp/venv/bin/python tools/mcp/test_client.py` → `61/61` parity
- [ ] The MCP tool-removal negative verified by hand once and restored
- [ ] `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only`
      unchanged

### Regression gates

- [ ] `./build/marrow_fixture_smoke` on `.mskl` and `.mbin`
- [ ] `./build/marrow_spine_import_smoke ...` — owl zero-weight and tank
      weighted-clipping regressions unchanged
- [ ] `./build/marrow_renderer_sample --skip-render ...` — GPU-skinned
      weighted-mesh preparation unchanged
- [ ] `./build/marrow_c_smoke`
- [ ] `./build/marrow_psd_import_smoke ...`
- [ ] `./build/marrow_atlas_packer_smoke`
- [ ] `./build/marrow_bootstrap`
- [ ] `python3 -m json.tool assets/fixtures/player_idle.marrow > /dev/null`

### Compatibility gates

- [ ] `.mskl` v1 unchanged
- [ ] `.mbin` v2 unchanged (`kBinaryVersionPackedAnimations`,
      `src/runtime/binary.cpp:23`)
- [ ] `.marrow` schema unchanged — no field added, removed, or retyped in
      `mesh_edits.weights`
- [ ] C ABI v1 unchanged (`include/marrow/marrow_c.h`)
- [ ] `editor-settings.json` v1 unchanged
- [ ] `ProjectData` gained no member
- [ ] `~/Library/Application Support/Marrow` still does not exist

---

## Decisions Taken Under Ambiguity

Design-level decisions are in spec §14 (D-1 … D-14). Plan-level ones:

**P-1. The canonical primitive gets its own translation unit and its own test
target, rather than joining `authoring.cpp`.** `authoring.cpp` is already large
and is entirely timeline/parameter work; a weight-only file makes the subsystem
boundary explicit and gives MAR-176 an obvious place to attach. The project-level
primitives *do* go into `authoring.cpp`, because they are `ProjectData`
transactions like every other primitive there.

**P-2. Task 1 moves the shell normalizer verbatim, including its known defects.**
The inverted gate only means something if the extraction changes nothing. Fixing
NaN handling in the same commit that moves the function would make a failure
ambiguous between "the move broke it" and "the fix broke it".

**P-3. Task 1 asserts byte-identical export against a stashed baseline.** The
shell smoke's numeric assertions have tolerances; `cmp` does not. For a
behaviour-preserving task, `cmp` is the honest gate.

**P-4. The D9 fix lands in Task 4, not Task 2.** It is a *shell* change (which
transform the brush inverts), not a canonical-rule change. Keeping them in
separate tasks keeps the blame clear if a shipped smoke number moves.

**P-5. Task 5 is split A/B on the extraction boundary.** Half A moves the agent
onto the primitives with no rule change and re-runs the shipped agent smoke
unchanged; Half B then turns on the new rules. Same discipline as Task 1, at the
agent surface.

**P-6. The `shell_smoke_graph.cpp` guard patch asserts its own count before and
after.** Seven guards is the count measured at `cf6a199`; the number has grown
in most stories. An asserting `sed` fails loudly on a miscount instead of
leaving a stale guard that silently stops guarding.

**P-7. If a shipped smoke number moves, it is recorded with its cause before it
is changed.** Sorting influences descending (D3) legitimately changes serialized
order, and `skeleton_skin.cpp:563-564` sums in array order, so a multi-influence
vertex's skinned position can move in the last ULP. That is expected. A weight
*value* moving is not, and must be investigated before the expectation is
touched.

**P-8. Case A's `.mbin` size delta is computed, not observed.** Writing down the
expected byte count from `binary.cpp`'s encoding and asserting equality catches
an encoding change; asserting only "smaller" would not.
