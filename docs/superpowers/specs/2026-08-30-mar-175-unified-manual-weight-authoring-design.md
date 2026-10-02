# MAR-175 Unified Manual Weight Authoring Design

Story: `MAR-175`, "Unify manual weight authoring", `dependsOn: ["MAR-174"]`.

```
Move brush, numeric, normalization, binding, and agent weight edits onto one
UI-free canonical weight primitive.

AC1  Paint, Erase, Smooth, Replace, numeric influence edits, Normalize, Rebind,
     and agent mutations all use the same domain implementation.
AC2  Every result removes non-positive influences, merges duplicate bones,
     sorts by descending weight then skeleton order, caps at four, and
     normalizes the sum to one.
AC3  The viewport offers a Replace brush, an active-vertex numeric influence
     table, selected-scope Normalize, and setup-pose Rebind without changing
     topology.
AC4  Invalid bones, non-finite values, empty results, and failed rebinds reject
     atomically with exact preview rollback and one undo step per accepted
     gesture or command.
AC5  Weight bind and numeric mutations have matching C++ registry and Python MCP
     validation, dry-run, affected-vertex, mutation, undo, and export-preview
     behavior.
AC6  Unit, project, shell, agent, and MCP tests prove identical canonical output
     across every manual path, undo/redo, save/reload, and export.
```

---

## 0. Relationship to MAR-168..174

MAR-168 through MAR-173 were one continuous chain inside the timeline/curve
subsystem: every story extended `timeline_model`, `authoring.cpp`'s keyframe
primitives, the dopesheet/graph gestures, and the `timeline.*` operation family.

**MAR-175 starts a different subsystem.** It touches mesh vertex weights:
`ProjectData::mesh_weight_attachment_edits`, `MeshGeometry::VertexWeights`,
`shell_weight_paint.cpp`, and the `set_vertex_weights` / `normalize_weights`
operations. It shares *no* data structure, no selector type, and no primitive
with the timeline chain.

The declared `dependsOn: ["MAR-174"]` is **sequencing, not coupling**. MAR-174
adds transient preview playback speed, which never serializes, never enters
history, and adds no operation. Nothing in MAR-174 is consumed here. The only
real constraint MAR-174 imposes on MAR-175 is arithmetic: MAR-174 leaves the
registry at 60, so MAR-175's count arithmetic starts from 60 (§10).

Nothing from the curve machinery is inherited. There is no `TimelineKeySelector`
analogue, no easing, no pivot, no `snap`, no loop-boundary pin, no explicit
duration growth. A weight edit is not a timing edit and this document does not
pretend otherwise.

MAR-176 ("Generate deterministic automatic weights") depends on this story and
consumes exactly one thing from it: the canonicalization function. Everything
about *choosing* candidate bones and *computing* weights from distance is
MAR-176 and is out of scope here (§11).

---

## 1. Goal

Today five code paths author mesh vertex weights and **four of them disagree**
about what a valid weight list is. They disagree on normalization, on
deduplication, on ordering, on the influence cap, on zero handling, on
non-finite handling, and on which pose the bind offsets are expressed in.

Two of those disagreements can produce a project that Marrow itself refuses to
save, and one silently bakes the current animated pose into a bind offset.

MAR-175 introduces one UI-free canonicalization primitive in the `marrow_editor`
library, routes every manual and agent path through it, and adds the four
authoring surfaces AC3 names — Replace brush, numeric influence table,
selected-scope Normalize, setup-pose Rebind — on top of that one primitive.

This is a **consolidation** story with a small, well-defined feature increment.
It is not a re-architecture of weight storage and it does not touch mesh
topology.

---

## 2. The Divergences, Enumerated

This section is the evidence base for the whole design. Every row was read out
of the tree at `feat/mar-168` (`cf6a199`).

### 2.1 The five paths

| # | Path | Entry point | Normalizer it calls |
|---|---|---|---|
| P1 | Brush: Paint | `apply_paint_weight_to_vertex`, `src/editor/shell_weight_paint.cpp:450` | `normalize_mesh_weight_vertex_edit`, `:174` |
| P2 | Brush: Erase | `apply_erase_weight_to_vertex`, `:514` | `normalize_mesh_weight_vertex_edit`, `:174` |
| P3 | Brush: Smooth | `apply_smooth_weight_to_vertex`, `:557` | `normalize_mesh_weight_vertex_edit`, `:174` |
| P4 | Agent `set_vertex_weights` | `src/editor/agent_handlers_editing.cpp:2568` | `normalize_weight_vertex`, `src/editor/agent_dispatch.cpp:1006` |
| P5 | Agent `normalize_weights` | `src/editor/agent_handlers_editing.cpp:2615` | `normalize_weight_vertex`, `src/editor/agent_dispatch.cpp:1006` |

There is **no numeric-entry path today**. `src/editor/shell_inspector.cpp:87-113`
builds `MeshWeightVertexRow`s and renders them with `ImGui::Text` — the Mesh
Weights tree is strictly read-only. AC3's numeric table is new UI, not a fifth
divergent writer to be reconciled.

There is **no rebind path today**. `grep -rn rebind src include tools` returns
nothing. AC3's Rebind is new.

### 2.2 The two normalizers, side by side

`normalize_mesh_weight_vertex_edit` — `src/editor/shell_weight_paint.cpp:174-215`:

| Step | Behaviour | Line |
|---|---|---|
| Drop non-positive | Yes, `weight <= 1e-6` erased (negatives included) | `:185` |
| Merge duplicate bones | **No** | — |
| Sort | **Only when the list exceeds 4**; `std::stable_sort` by descending weight, so ties break on *insertion order*, not skeleton order | `:192-199` |
| Cap at 4 | Yes, but only inside the `size() > 4U` branch | `:192`, `:200` |
| Normalize sum to 1 | Yes, unless the total is `<= 1e-6`, in which case the list is **cleared** | `:203-215` |
| Non-finite | **Not handled.** `NaN <= 1e-6` is `false`, so a NaN survives the drop filter, poisons `total_weight`, survives the `total_weight <= 1e-6` guard for the same reason, and is then divided by NaN — leaving every influence on that vertex NaN | `:185`, `:207` |

`normalize_weight_vertex` — `src/editor/agent_dispatch.cpp:1006-1020`:

| Step | Behaviour | Line |
|---|---|---|
| Drop non-positive | **No.** Negatives are clamped to `0.0` and the zero-weight entry is **kept** | `:1012`, `:1018` |
| Merge duplicate bones | **No** | — |
| Sort | **No** | — |
| Cap at 4 | **No** | — |
| Normalize sum to 1 | Yes, unless the total is `<= 0.0`, in which case the function **returns having done nothing** (the un-normalized list is kept) | `:1014-1015` |
| Non-finite | Not handled; `std::max(0.0, NaN)` is implementation-defined for the comparison and NaN propagates through the division | `:1012`, `:1018` |

**Divergence D1 — zero-weight influences.** P1..P3 delete them; P4/P5 keep them
at exactly `0.0`.

**Divergence D2 — the influence cap.** P1..P3 truncate to 4 silently. P4 rejects
`> 4` at the argument parser (`agent_handlers_editing.cpp:2588`, mirrored
advisorily by `"maxItems": 4` in `tools/mcp/tools/editing.py:1040`). P5 applies
no cap at all, so an attachment materialized from a runtime document that
already carries more than 4 influences stays over the cap after "normalizing".

**Divergence D3 — ordering.** P1..P3 sort descending *only* when truncating, and
the tie-break is insertion order. P4/P5 never sort. Nothing anywhere sorts by
skeleton order. AC2 requires descending weight then skeleton order, always.

**Divergence D4 — duplicate bones.** Nothing merges them. `set_vertex_weights`
validates only that each bone *exists* (`agent_handlers_editing.cpp:2604`); it
accepts `[{bone: "spine", w: .5}, {bone: "spine", w: .5}]`. Smooth
(`shell_weight_paint.cpp:614-624`) de-duplicates by construction because it keys
`ordered_bones` by name, so it cannot emit duplicates — but it also cannot
*remove* duplicates that were already present in its input.

**Divergence D5 — the empty-result contract.** P1..P3 treat an empty result as
"no change" and return `false`, silently discarding the stroke sample
(`:506`, `:549`, `:676`). P4 writes the empty influence list into the project.

**Divergence D6 — non-finite values.** Neither normalizer rejects NaN or Inf.
`set_vertex_weights` accepts any `number_arg` for `weight`, `x`, and `y` without
a finiteness check (`agent_handlers_editing.cpp:2597-2608`).

**Divergence D7 — the runtime→edit conversion is written twice, differently.**

- `build_mesh_weight_attachment_edit_from_runtime`, `shell_weight_paint.cpp:120-150`,
  synthesizes a placeholder name `"<bone 17>"` for an out-of-range bone index
  (`:136-139`) and keeps the influence.
- `mesh_weight_edit_from_runtime`, `agent_dispatch.cpp:974-1004`, **skips**
  out-of-range influences entirely (`:992-994`).

Same conversion, two files, opposite policies for the same malformed input.

**Divergence D8 — the transaction shape.** P4/P5 run inside a real
`EditorSession::EditTransaction` (`agent_handlers_editing.cpp:2555-2563`). P1..P3
do not: `apply_weight_paint_sample` (`shell_weight_paint.cpp:765-893`) mutates
`state->load_result.project` directly and hand-rolls a single-attachment
rollback buffer (`:850-877`), because deep-copying `ProjectData` at brush-sample
rate is unaffordable. Undo granularity is then reconstructed by
`begin_weight_paint_stroke` / `finish_weight_paint_stroke` (`:720-763`) from an
`EditorHistorySnapshot`.

This divergence is **deliberate and is preserved** (§7.1). It is listed because
it constrains where the shared primitive can live: the primitive must be
callable both from inside a transaction and from a raw `ProjectData*`.

**Divergence D9 — the pose the bind offsets come from.** When Paint adds a bone
that the vertex did not previously have, it computes the new bind offset by
inverting **the current preview pose**:

```
src/editor/shell_weight_paint.cpp:487
    const auto bind_position = inverse_transform_point_safe(
        state.preview_skeleton
            ->bone_world_transforms()[*context.influence_bone_index],
        overlay.vertices[vertex_index].world_position.x,
        overlay.vertices[vertex_index].world_position.y);
```

`state.preview_skeleton->bone_world_transforms()` is the skeleton at the current
playhead. `overlay.vertices[...].world_position` comes from
`evaluate_current_mesh_attachment` (`:403-404`) — also the current pose. Lines
`:479-486` subtract the animation-FFD offsets, so FFD is removed, but the **bone
animation is not**.

The existing shell smoke proves this is a live code path and not a theoretical
one: `src/editor/shell_smoke_timeline.cpp:1701` scrubs to "the attack preview
pose" and then paints. Painting a *new* bone influence off setup pose therefore
writes a pose-dependent bind offset into the project. Existing influences are
untouched, so the damage is scoped to newly introduced bones.

This is precisely the defect AC3's "setup-pose Rebind" exists to repair, and
§6.4 also fixes it at the source.

### 2.3 Two divergences that produce unsavable projects

`validate_project_for_save` — `src/editor/project.cpp:5172`, weight block at
`:5504-5568` — enforces, per vertex: at least one influence (`:5527`), at most
four (`:5532`), non-empty bone names (`:5541`), **strictly positive** weights
(`:5546`), **no repeated bone** (`:5551`), positive total (`:5562`). The
`.marrow` loader enforces the same set independently at `:2085-2220`
(`:2121`, `:2128`, `:2169`, `:2195`, `:2208`).

Neither `commit_or_error` nor the runtime rebuild runs these checks. So:

**Defect V1 — zero weights survive to save.** `set_vertex_weights` with
`normalize: true` (the default) routes through `normalize_weight_vertex`, which
keeps a `0.0` entry (D1). The commit succeeds, the project is dirtied, and the
subsequent `save` fails with *"mesh weight edit influences must preserve
positive weights"*. The user's edit is accepted and then unsavable.

**Defect V2 — duplicate bones survive to save.** `set_vertex_weights` never
rejects a repeated bone (D4). Commit succeeds; `save` fails with *"mesh weight
edit vertices must not repeat the same bone"*.

Both defects are exactly the class the canonical primitive eliminates: after
unification, no accepted write can produce a project that
`validate_project_for_save` refuses, because the canonicalizer's post-condition
is strictly stronger than the save validator's precondition (§6.2).

### 2.4 One correctness point that is *not* a defect

Every early `return make_error(...)` in the weight handler between
`agent_handlers_editing.cpp:2571` and `:2605` returns **without** calling
`transaction.cancel()`, unlike every neighbouring handler (`:2510`, `:2515`,
`:2497`, `:2485`, …). This is stylistically inconsistent but **not** an
atomicity bug: `EditorSession::EditTransaction`'s destructor cancels an
uncommitted transaction and restores the project and preview snapshots
(`include/marrow/editor/session.hpp:333-338`), and `transaction` is a local. The
rejection therefore does roll back, including the `ensure_mesh_weight_edit`
materialization at `:2565` that happens before any vertex is validated.

MAR-175 adds the explicit `transaction.cancel()` calls anyway, for consistency
with the rest of the file and so that a future refactor that widens the
transaction's scope cannot silently turn this into a real bug. This is recorded
here so the implementer does not report it as a fixed defect.

### 2.5 One narrow risk, flagged not claimed

`source_skin_name` (`src/editor/shell_selection.cpp:786-794`) returns the string
`"<unresolved>"` when the skin index does not resolve. The brush writes that
value straight into `MeshWeightAttachmentEdit::skin_name`
(`shell_weight_paint.cpp:69`, target field set at `:336`). `"<unresolved>"` is
non-empty, so `validate_project_for_save:5506` passes it, but
`find_skin_attachment_value` at export (`project.cpp:5071-5078`) finds nothing
and `continue`s — the weight override is silently dropped from the exported
`.mskl`.

`current_mesh_weight_paint_target` returns `nullopt` whenever
`target.source_attachment` is null (`shell_weight_paint.cpp:332-334`), which
makes an unresolved skin index hard to reach in practice. This design does not
claim it is reachable. Task 8 adds a cheap assertion that the canonical write
path never stores an unresolvable skin name, so the question is closed either
way.

---

## 3. Scope

### 3.1 In scope

1. One canonicalization primitive in the `marrow_editor` library (§6.2).
2. One setup-pose rebind primitive in the same place (§6.4).
3. Routing P1..P5 through the canonicalizer, deleting both existing normalizers
   and one of the two duplicated runtime→edit converters (§7).
4. A `Replace` brush mode (§8.1).
5. An editable active-vertex numeric influence table in the inspector (§8.2).
6. A selected-scope Normalize command (§8.3).
7. A setup-pose Rebind command (§8.4).
8. One new agent operation `mesh.rebind_weights`, plus a `vertices` scope
   argument on `normalize_weights`, plus tightened `set_vertex_weights`
   validation (§9).
9. The matching MCP tools (§9.5).

### 3.2 Out of scope

Mesh topology of every kind: adding, moving, or deleting vertices; triangles;
UVs; hulls; automesh. `mesh_edits` remains a weights-only overlay
(`docs/root1/format-spec.md:934-939`), and AC3 says "without changing topology"
in as many words.

Weight copy/paste, mirroring, and Weld — those are unscheduled
(`docs/root1/editing-gap-analysis.md:161`).

Everything in MAR-176: candidate-bone checklists, inverse-square distance,
vertex-to-bone-segment geometry, nearest-candidate fallback for isolated
vertices, and any automatic assignment of *which* bones influence a vertex.
MAR-175 never invents an influence the caller did not name. Painting adds
exactly the one active bone; Smooth adds only bones already present on a
topological neighbour; Rebind adds none.

Skinning evaluation, GPU skinning, the renderer, and `skeleton_skin.cpp`.

### 3.3 Compatibility boundaries — all unchanged

| Surface | Version | Change |
|---|---|---|
| `.mskl` | v1 | none |
| `.mbin` | v2 | none |
| `.marrow` schema | current | **none** — `mesh_edits.weights` keeps its exact shape; no field added, removed, or retyped |
| C ABI | v1 | none |
| `editor-settings.json` | v1 | none |
| `ProjectData` | — | **no new member.** Everything is expressed inside the existing `mesh_weight_attachment_edits` |

The canonical rules are strictly *narrower* than what the `.marrow` schema
already accepts, so every project Marrow can write after MAR-175 is a project
Marrow could already write and load before it. There is no migration and no
version bump.

---

## 4. What Must Not Change

`set_vertex_weights` and `normalize_weights` are a public agent/MCP surface and
have shipped. The following are load-bearing and are preserved verbatim:

- Both operation **names**, their `"edit"` category, `mutating: true`,
  `review: false`, `dry_run: true` flags (`src/editor/agent_dispatch.cpp:71-72`,
  asserted in `src/samples/agent_dispatch_smoke.cpp:82-83`), and their position
  in `kOperationSpecs`.
- The required argument triple `skin` / `slot` / `attachment`.
- The `vertices[].index` / `vertices[].influences[]` shape with
  `{bone, x, y, weight}`, and the 1..4 `influences` bound.
- `normalize_weights`'s `no_change` disposition and its exact message
  `"Mesh weights already normalized."`
  (`agent_handlers_editing.cpp:2621-2628`), which
  `agent_dispatch_smoke.cpp:2932-2941` asserts alongside a null scene delta.
- `"Edited mesh weights successfully."` on success and
  `"Mesh weight edit validated."` on dry-run.
- One history entry per call, labelled `"Edit mesh weights via Agent"`.

Two observable behaviours **do** change. Both are deliberate and both are
justified below and repeated in §12.

**Change C1 — `set_vertex_weights` with `"normalize": false` now rejects.**
Today the flag defaults to `true` (`agent_handlers_editing.cpp:2610`) and `false`
writes the caller's raw numbers. Under unification, canonicalization is
unconditional, so `false` has no implementable meaning: honouring it re-opens
defects V1 and V2, and silently ignoring it would return `ok: true` for a
request whose stated intent was not carried out. The operation therefore rejects
an explicit `"normalize": false` with `invalid_request` and the message
*"normalize:false is no longer supported; weight writes are always
canonicalized."* Scripts that omit the flag — the documented default and every
in-tree caller — are unaffected. This is a loud, discoverable, one-line break
rather than a silent numerical one.

**Change C2 — `normalize_weights` now does more than rescale.** It gains
dropping, merging, sorting, and capping. Justification: the narrow version can
leave a project that `validate_project_for_save` refuses (§2.3), and it cannot
repair an attachment materialized from a runtime document carrying more than
four influences. The widened version is the only one whose output is guaranteed
to round-trip. Idempotence is preserved and strengthened: on already-canonical
data the operation still returns `no_change` with the existing message, which is
what the shipped smoke asserts.

Both go into `AGENTS.md` and `docs/root1/agent-control.md` as an explicit
migration note.

---

## 5. Architecture

```
                        marrow_editor (STATIC)              <- unit-testable
  ┌───────────────────────────────────────────────────────────────────────┐
  │  include/marrow/editor/authoring.hpp   (additive declarations)        │
  │  src/editor/mesh_weight_model.{hpp,cpp}   NEW                         │
  │                                                                       │
  │    kMaxMeshWeightInfluences = 4                                       │
  │    kMeshWeightEpsilon       = 1e-6                                    │
  │                                                                       │
  │    canonicalize_mesh_weight_vertex(skeleton, MeshWeightVertexEdit*)   │
  │    mesh_weight_edit_from_runtime(skeleton, skin, slot, att, data)     │
  │    setup_pose_bone_world_transforms(skeleton)                         │
  │    rebind_mesh_weight_vertex(setup_transforms, bones, vertex*)        │
  │                                                                       │
  │  src/editor/authoring.cpp   (additive project-level primitives)       │
  │    set_mesh_vertex_weights(project*, skeleton, target, vertices)      │
  │    normalize_mesh_weights(project*, skeleton, target, scope)          │
  │    rebind_mesh_weights(project*, skeleton, target, scope)             │
  └───────────────────────────────────────────────────────────────────────┘
        ▲                         ▲                          ▲
        │                         │                          │
   ┌────┴─────┐        ┌──────────┴──────────┐     ┌─────────┴──────────┐
   │  brush   │        │  numeric table /    │     │  agent handlers    │
   │ P1 P2 P3 │        │  Normalize / Rebind │     │  P4 P5 + rebind    │
   │ + Replace│        │  commands           │     │                    │
   └──────────┘        └─────────────────────┘     └────────────────────┘
   shell_weight_paint   shell_inspector /           agent_handlers_editing
        .cpp            shell_weight_paint.cpp            .cpp
   (marrow_editor_shell executable)                 (marrow_editor library)
```

### 5.1 Why the primitive cannot live in `shell_weight_paint.cpp`

`src/editor/shell_weight_paint.cpp` is compiled into the **`marrow_editor_shell`
executable** (`CMakeLists.txt:866`), not into the `marrow_editor` static library
(`CMakeLists.txt:499-516`). Nothing in `src/tests/` can link it: every model test
target links `marrow_editor` (e.g. `CMakeLists.txt:631-644`).

So the "UI-free math with unit tests" rule is not satisfiable by hoisting
`normalize_mesh_weight_vertex_edit` in place. The canonical primitive must be a
new translation unit added to the `marrow_editor` source list, which is also the
only way the agent handlers (already in that library) and the shell can share
one implementation.

`src/editor/mesh_weight_model.{hpp,cpp}` is added to `marrow_editor` at
`CMakeLists.txt:507` (next to `authoring.cpp`), and a new
`marrow_mesh_weight_model_tests` target mirrors `marrow_timeline_model_tests`
exactly (`CMakeLists.txt:631-644`).

### 5.2 Two layers, deliberately

- **`mesh_weight_model`** is pure: it takes a `SkeletonData` and a
  `MeshWeightVertexEdit` and returns a canonical one or an error string. It
  never sees `ProjectData`, never allocates a transaction, and is trivially
  unit-testable. Both the brush (which cannot afford a `ProjectData` copy per
  sample) and the agent (which must have one) call it.
- **`authoring.cpp`** adds the three project-level primitives that follow the
  established preflight-then-mutate discipline: copy `ProjectData`, resolve and
  validate everything against the candidate, then a single
  `*project = std::move(candidate)`. These are what the numeric table, the two
  commands, and the agent handlers call.

The brush deliberately calls only the pure layer (§7.1).

---

## 6. The Canonical Primitive

### 6.1 Constants

```cpp
// src/editor/mesh_weight_model.hpp
namespace marrow::editor::mesh_weight_model {

/// The runtime, the `.mskl` loader, and the `.marrow` loader all cap a vertex
/// at four bone influences. This is the editor-side name for that number.
/// Runtime peers: src/runtime/skeleton_parse.cpp:1575,
/// src/editor/project.cpp:2128 and :5532.
inline constexpr std::size_t kMaxMeshWeightInfluences = 4U;

/// Weights at or below this are treated as absent. Chosen to equal the value
/// the brush already used (shell_weight_paint.cpp:17) so brush behaviour on
/// existing fixtures is bit-identical.
inline constexpr double kMeshWeightEpsilon = 1e-6;

} // namespace
```

Every editor-side `4U` literal is replaced by `kMaxMeshWeightInfluences`:
`agent_handlers_editing.cpp:2588`, `project.cpp:2128`, `project.cpp:5532`. The
runtime's own limit at `skeleton_parse.cpp:1575` stays a runtime-layer literal —
`marrow_runtime` must not depend on `marrow_editor` — but gains a comment naming
its editor peer. `tools/mcp/tools/editing.py:1040`'s `"maxItems": 4` stays a
literal because the MCP schema is advisory; the C++ gate is authoritative and
Task 9's test proves the advisory schema did not loosen it.

### 6.2 `canonicalize_mesh_weight_vertex`

```cpp
/**
 * @brief Rewrites one vertex's influence list into the single canonical form.
 *
 * Returns an empty string on success. On rejection it returns a message naming
 * the offending bone or value and leaves `*vertex` **untouched**, so a caller
 * that canonicalizes a list of vertices and stops at the first error has
 * mutated nothing it has not already accepted.
 *
 * The post-condition is strictly stronger than every precondition
 * `validate_project_for_save()` (project.cpp:5504-5568) and the `.marrow`
 * loader (project.cpp:2085-2220) impose on a weight vertex, so a project built
 * only from canonical vertices can always be saved and reloaded.
 */
std::string canonicalize_mesh_weight_vertex(
    const runtime::SkeletonData& skeleton,
    MeshWeightVertexEdit* vertex);
```

Steps, in this exact order:

**1. Reject non-finite input.** Any influence whose `weight`, `x`, or `y` fails
`std::isfinite` rejects the whole vertex:
`"Bone 'arm_l' has a non-finite weight."` / `"... non-finite bind offset."`
This closes D6 and stops the NaN-poisoning path at `shell_weight_paint.cpp:185`.

**2. Reject unknown and empty bone names.** Each `bone_name` must be non-empty
and must resolve through `skeleton.find_bone_index()`:
`"Bone not found: <name>"`. This replaces D7's two contradictory policies with
one: a name that does not resolve is an error, never a placeholder and never a
silent drop. `mesh_weight_edit_from_runtime` (§6.3) guarantees no such name can
originate inside Marrow, so this rejection only ever fires on external input.

**3. Merge duplicate bones.** Entries sharing a resolved bone index collapse
into one. The merged weight is the **sum** of the parts. The merged bind offset
is the **weight-weighted mean** of the parts, matching the averaging Smooth
already does (`shell_weight_paint.cpp:590-592`, `:656-658`); when the summed
weight is not positive the merged entry is dropped by step 4 anyway, so the
degenerate mean is never observed. Closes D4 and defect V2.

**4. Drop non-positive influences.** `weight <= kMeshWeightEpsilon` is removed.
Negative weights are *dropped*, not clamped-and-kept — the two differ only in
whether a zero survives, and after this step they cannot. Closes D1 and defect V1.

**5. Sort.** Descending by `weight`; ties break on **ascending skeleton bone
index**. Implemented with `std::sort` and a total-order comparator, so the
result does not depend on input order at all. This is AC2's exact wording and
closes D3. Note the tie-break is skeleton order, *not* bone name order and *not*
insertion order.

**6. Cap.** Truncate to `kMaxMeshWeightInfluences`. Because sorting is total,
which four survive is fully determined by the input set, never by the order the
caller happened to supply. Closes D2.

**7. Reject an empty result.** If nothing survives:
`"A weighted vertex must keep at least one positive influence."` This closes D5
in the strict direction: the brush's current silent-discard is replaced by an
explicit rejection at the primitive, and the brush's *sample* layer converts
that into "this sample changed nothing" (§7.1) so a stroke is never killed
mid-gesture.

**8. Normalize.** Divide every surviving weight by their sum. The sum is
provably positive and finite after steps 1, 4, 6, and 7; the implementation
still asserts it and returns
`"Weighted vertex influences must sum to a positive weight."` if the assertion
is ever violated.

**Idempotence.** `canonicalize(canonicalize(v)) == canonicalize(v)`, bit-exactly.
Steps 1-3 and 5-7 are no-ops on a canonical list. Step 8 divides by a sum that a
canonical list already has: because IEEE-754 division by a value is not
guaranteed to reproduce the input when that value is not exactly `1.0`, the
implementation **skips the division entirely when the sum is already exactly
`1.0`**. That one branch is what makes the property bit-exact rather than
approximate, and it is asserted directly in the unit tests.

### 6.3 The single runtime→edit converter

D7's two converters collapse into one, promoted into `mesh_weight_model`:

```cpp
MeshWeightAttachmentEdit mesh_weight_edit_from_runtime(
    const runtime::SkeletonData& skeleton,
    std::string_view skin_name,
    std::string_view slot_name,
    std::string_view attachment_name,
    const runtime::AttachmentData& attachment);
```

Policy: an out-of-range `bone_index` is **skipped**, which is the agent's
existing behaviour (`agent_dispatch.cpp:992-994`) and the safe one — the shell's
`"<bone 17>"` placeholder (`shell_weight_paint.cpp:136-139`) is a name that
cannot resolve, so under §6.2 step 2 it would turn every subsequent
canonicalization of that vertex into a hard rejection. Skipping preserves the
ability to keep authoring. A vertex left with zero influences by the skip is
handled by §6.2 step 7 the first time anything writes to it.

`agent_dispatch.cpp:974-1004` and `shell_weight_paint.cpp:120-150` are both
deleted; `ensure_mesh_weight_edit` (`agent_dispatch.cpp:1022-1036`) is retained
and re-pointed at the shared converter.

This conversion is **not** canonicalized on materialization. Materializing an
imported attachment must not by itself dirty the project's weights; a runtime
document that carries a non-canonical weight list keeps it until something
writes. `normalize_weights` is the explicit command that makes it canonical
(§4, C2). This is the answer to "implicit on every write, or an explicit
operation" — it is **both, at different moments**: implicit on every *write*,
explicit for *repair* of data Marrow did not author.

### 6.4 Setup-pose rebind

```cpp
/// Bone world transforms with every bone at its setup pose. Built on a scratch
/// `runtime::Skeleton` so the live preview is never disturbed.
std::vector<runtime::BoneWorldTransform> setup_pose_bone_world_transforms(
    const std::shared_ptr<const runtime::SkeletonData>& skeleton);

/**
 * @brief Re-expresses each influence's bind offset in its own bone's setup
 *        frame, without changing which bones influence the vertex.
 */
std::string rebind_mesh_weight_vertex(
    const runtime::SkeletonData& skeleton,
    const std::vector<runtime::BoneWorldTransform>& setup_transforms,
    MeshWeightVertexEdit* vertex);
```

The math, stated once:

```
Skinning (src/runtime/skeleton_skin.cpp:547-565) evaluates

        V  =  sum_i  w_i * T_i( x_i , y_i )

where T_i is bone i's world transform and (x_i, y_i) is the vertex expressed in
bone i's local frame. Rebind holds V and the w_i fixed and solves for a
*consistent* set of local offsets in the setup pose:

    step 1   V_setup  =  sum_i  w_i * S_i( x_i , y_i )        S_i = setup xform
    step 2   (x_i', y_i')  =  S_i^-1( V_setup )               for every i
```

After a rebind every bone's local offset points at the same setup-world
location, which is the definition of a correct bind and the invariant D9's paint
path violates for newly added bones.

Rejections, each atomic for the whole call:

- Any `S_i` with `|det| <= 1e-8` — the existing threshold in
  `inverse_transform_point_safe` (`shell_weight_paint.cpp:60-63`), whose
  double-promoting body is moved into `mesh_weight_model` unchanged and shared:
  `"Bone 'x' has a singular setup transform and cannot be rebound."`
- Any non-finite intermediate: `"Rebinding produced a non-finite bind offset."`
- Any bone index out of range against `setup_transforms`.

`weight` values are not touched by rebind. The result is still passed through
`canonicalize_mesh_weight_vertex`, which is a no-op on already-canonical input
and which catches a non-finite offset that slipped through.

**Precision, stated honestly.** `BoneWorldTransform` packs to six **`float`**s
(`include/marrow/runtime/skeleton.hpp:1009-1010`), while `MeshWeightInfluenceEdit::x`
and `::y` are `double` (`include/marrow/editor/project.hpp:196-197`). So the
transform is float32, the arithmetic and the result are double, and
`S_i(S_i^{-1}(V))` does not reproduce `V` bit-for-bit. Rebind is therefore
**deterministic** — the same input always yields the same doubles — but it is
**not bit-exactly idempotent**. The tests assert repeat-run determinism
bit-exactly and second-application stability to `1e-9` relative, and the test
prints that distinction. This is the one place in MAR-175 where narrowing enters
the math, and it enters through the transform, not through the weight.

### 6.5 Where float32 narrowing does and does not happen

| Value | Editor | `.marrow` | `.mskl` | `.mbin` |
|---|---|---|---|---|
| `weight` | `double` (`project.hpp:198`) | JSON number, full double | JSON number, full double | **float32** (`src/runtime/binary.cpp:141-146`, reached from `encode_value`'s `Number` case at `:236`) |
| bind `x`, `y` | `double` (`project.hpp:196-197`) | double | double | **float32** |
| bone world transform | **float32** (`skeleton.hpp:1009`) | n/a | n/a | n/a |

Consequences the tests must respect:

- Identity comparisons on the project and on `.mskl` are **bit-exact on
  `double`**. `MeshWeightVertexEdit` equality in tests compares raw doubles, not
  `require_near`.
- After normalization the weights sum to exactly `1.0` as doubles. They do
  **not** sum to exactly `1.0f` after `.mbin` narrowing. No test may assert that
  they do; `.mbin` assertions compare `static_cast<float>(expected)` against the
  decoded float.

---

## 7. Routing the Existing Paths

### 7.1 The brush (P1, P2, P3, and the new Replace)

`normalize_mesh_weight_vertex_edit` (`shell_weight_paint.cpp:174-215`) is
**deleted**. Its four call sites (`:505`, `:548`, `:675`, and the new Replace
site) call `canonicalize_mesh_weight_vertex` instead.

The three `apply_*_weight_to_vertex` helpers keep their signatures and their
`bool` "did this sample change anything" contract. The change is local:

```cpp
    influence_it->weight += stamp_strength;
    if (!canonicalize_mesh_weight_vertex(skeleton, &updated).empty()) {
        return false;              // this sample changes nothing
    }
    if (mesh_weight_vertex_equal(*vertex, updated)) {
        return false;
    }
    *vertex = std::move(updated);
    return true;
```

A canonicalization rejection inside a stroke returns `false` — the sample is a
no-op — rather than aborting the gesture. Rationale, and it is the same one
MAR-173 recorded for a rejected scale frame: a brush stroke that sweeps across a
vertex whose influence list cannot be made canonical must not destroy the rest
of the stroke.

`mesh_weight_vertex_equal`'s `1e-6` tolerance (`:152-171`) is **kept** for the
change-detection comparison. It exists to stop sub-epsilon brush jitter from
dirtying the project every frame, which is a different question from identity.
Tests that assert identity compare bit-exactly and do not use it.

**D8 is preserved.** The brush still writes `state->load_result.project`
directly and still uses the one-attachment rollback buffer at `:850-877`. It is
not moved onto `EditorSession::EditTransaction`. Copying `ProjectData` per brush
sample was measured-away once and re-introducing it is a regression, not a
cleanup. The primitive's pure layer exists precisely so this path can share the
canonical rules without sharing the transaction shape.

**D9 is fixed at the source.** The new-influence bind computation at `:479-502`
switches from `state.preview_skeleton->bone_world_transforms()` (current pose)
to `setup_pose_bone_world_transforms()` (§6.4), and from the current-pose world
vertex to the setup-pose world vertex derived from the vertex's existing
influences. Painting a new bone therefore writes a setup-frame bind offset no
matter where the playhead is. The setup transform vector is computed **once per
stroke** in `begin_weight_paint_stroke` and cached on `MeshWeightStrokeState`,
not once per sample.

This is an observable behaviour change for paint-off-setup-pose, and it changes
one shipped smoke expectation. §13.3 covers it.

### 7.2 The agent handler (P4, P5)

`normalize_weight_vertex` (`agent_dispatch.cpp:1006-1020`) is **deleted**;
`agent_dispatch_internal.hpp:220` loses its declaration.

`handle_editing_operation`'s weight block (`agent_handlers_editing.cpp:2533-2634`)
is restructured to the preflight-then-mutate shape the file's other handlers
use:

1. Parse and validate **every** vertex entry and **every** influence into a
   local `std::vector<std::pair<std::size_t, MeshWeightVertexEdit>>` before
   touching the transaction. This fixes the ordering problem at `:2568-2614`,
   where the loop writes `edit->vertices[i]` for earlier entries and can then
   reject on a later one. The destructor rolls that back today (§2.4), but the
   restructure makes the atomicity local and visible instead of relying on it.
2. Canonicalize each parsed vertex; a rejection returns the primitive's message
   with `invalid_request`.
3. Open the transaction, `ensure_mesh_weight_edit`, assign, commit.
4. Add the missing explicit `transaction.cancel()` on every error path (§2.4).

`dry_run` (`:2549-2553`) currently returns before any vertex is validated, so a
dry run of a malformed payload reports success. It moves to **after** step 2, so
a dry run performs the identical validation a live call performs, and its
payload gains the affected-vertex report AC5 requires (§9.4).

---

## 8. The Four New Authoring Surfaces

### 8.1 Replace brush

`WeightPaintMode` (`src/editor/shell_state.hpp:241-245`) gains `Replace` after
`Smooth`. `weight_paint_mode_name` (`shell_weight_paint.cpp:19-30`) returns
`"Replace"`; `weight_paint_stroke_label` (`:684-706`) returns
`"Replaced <bone> weights on <attachment>"`.

Semantics: Paint is additive (`influence_it->weight += stamp_strength`, `:504`).
Replace **assigns** the active bone's weight to the stamp value and leaves the
other influences' raw weights alone, letting canonicalization renormalize:

```cpp
    influence_it->weight = stamp_strength;   // stamp in [0, 1]
```

`stamp_strength` is `pressure_scaled_strength(strength, pressure, falloff)`
(`:809-812`), which is already in `[0, 1]`. Where Paint is a rate, Replace is a
target — dragging with `strength = 1.0` drives the active bone to a full 1.0 on
every vertex under the cursor in one pass, which is the standard authoring
motion a rate brush cannot express.

Replace requires an active influence bone, like Paint and Erase; it joins them
in the readiness gate at `shell_weight_paint.cpp:775-781` and in
`weight_mode_ready` (`shell_viewport_ui.cpp:1070-1072`), where only `Smooth`
does not need a bone.

`Replace` also enters the brush-mode radio group in the viewport weight panel
and the `MeshWeightOverlay` status line (`shell_viewport_ui.cpp:891-900`).

### 8.2 Active-vertex numeric influence table

`shell_inspector.cpp:87-113` becomes editable, for **one vertex at a time**.

The active vertex is `state.viewport_ffd_selection->vertex_indices` when that
selection holds exactly one index and its
`ViewportFfdSelectionScope` (`shell_state.hpp:480-491`) resolves to the same
slot and attachment as `current_mesh_weight_paint_target`. Otherwise the table
stays read-only and shows a one-line reason. Multi-vertex numeric editing is
deliberately not offered: AC3 says "an active-vertex numeric influence table",
and a numeric field that writes a different value into each of N vertices has no
single correct semantics.

Per row: an `ImGui::InputDouble` for `weight`, and a remove button. Below the
table, an add-influence combo listing bones not already present, disabled once
the vertex holds `kMaxMeshWeightInfluences`. Bind offsets `x` / `y` stay
**read-only** — they are geometry, and Rebind is the supported way to change
them.

Commit granularity: each field commits on `ImGui::IsItemDeactivatedAfterEdit()`,
producing exactly one `set_mesh_vertex_weights` call, one transaction, one
history entry. Dragging inside a single `InputDouble` produces no intermediate
transactions. The existing `shell_coalesced_edit.hpp` pattern supplies the
grouping key so that repeated edits to the same field within one interaction
merge, matching how every other inspector numeric field behaves.

The value the user types is the **pre-normalization** weight. Typing `2.0` into
a two-bone vertex whose other bone is `1.0` yields `0.667 / 0.333`, and the
field redisplays `0.667`. The table shows a `Σ 1.000` line so the renormalization
is visible rather than surprising.

### 8.3 Selected-scope Normalize

A `Normalize` button in the viewport weight panel. Scope:

- If a valid multi-vertex FFD selection resolves against the current weight
  target, the scope is exactly those vertex indices.
- Otherwise the scope is every vertex of the attachment — which is the current
  `normalize_weights` behaviour, so the button's no-selection case matches the
  shipped agent operation exactly.

One transaction, one history entry, labelled
`"Normalized weights on <attachment>"`. A scope in which no vertex changes is a
no-op that neither dirties the project nor adds history — the GUI analogue of
`normalize_weights`'s `no_change`.

Unselected vertices must be byte-identical afterwards. That is asserted, not
assumed (§13.4).

### 8.4 Setup-pose Rebind

A `Rebind` button beside Normalize, with the same scope rule and the same
one-transaction/one-entry contract, labelled
`"Rebound weights on <attachment>"`.

Rebind changes only `x` and `y`. It does not change which bones influence a
vertex, it does not change any `weight`, and it does not touch `vertices`,
`triangles`, or `uvs` — this is AC3's "without changing topology", asserted
directly by comparing the attachment's non-weight geometry before and after.

A failed rebind (singular setup transform, non-finite result) rejects the
**entire** command, leaving the project byte-identical, per AC4.

---

## 9. Agent and MCP Surface

### 9.1 The registry goes from 60 to 61

Measured as-built at `cf6a199`: `std::size(kOperationSpecs)` is **60**
(`src/editor/agent_dispatch.cpp:28-89`), and `shell_smoke_graph.cpp` carries
**seven** `!= 60U` guards (`:148`, `:636`, `:1558`, `:2128`, `:3142`, `:3957`,
`:4603`). Task 0 re-measures both rather than trusting these numbers.

MAR-175 adds exactly **one** operation:

```cpp
    {"mesh.rebind_weights", "edit", true, false, true, true, &handle_editing_operation},
```

inserted immediately after `{"normalize_weights", ...}` at
`agent_dispatch.cpp:72`, keeping the weight family contiguous. Registry: **61**.

Why one and not zero, and why not two:

- **Rebind must be an operation.** AC5 names "weight bind ... mutations" as
  needing matching registry and MCP behaviour, and
  `docs/root1/editing-gap-analysis.md:461` lists "weight bind/auto-weight"
  among the P1 persistent mutations that "C++ operation registry와 Python MCP에
  동시에 노출한다". Lesson 4 from MAR-172 is that narrowing a spec requirement
  to the GUI is exactly how a defect ships. Rebind gets an operation.
- **Selected-scope Normalize does not need one.** It is a *scope* on an
  operation that already exists. `normalize_weights` gains an optional
  `vertices` array of indices; absent means "every vertex", which is the shipped
  behaviour, so the change is additive and backward-compatible.
- **The numeric table does not need one.** `set_vertex_weights` already is that
  operation.

The naming follows the dotted convention every operation added since MAR-128
uses (`timeline.scale_key_times`, `animation.set_duration`), and `mesh.` already
exists as a namespace via the `mesh.describe` inspection operation
(`agent_dispatch.cpp:39`). The two legacy undotted names stay as they are (§4).

### 9.2 `mesh.rebind_weights`

```jsonc
{
  "op": "mesh.rebind_weights",
  "args": {
    "skin": "mesh_base",           // required
    "slot": "body",                // required
    "attachment": "body_mesh",     // required
    "vertices": [1, 2],            // optional; absent = every vertex
    "dry_run": false               // optional, default false
  }
}
```

Validation, all before any mutation:

| Rule | Error | Code |
|---|---|---|
| `args` object present | `mesh.rebind_weights requires 'args' object.` | default |
| `skin`/`slot`/`attachment` present | `mesh.rebind_weights requires skin, slot, and attachment.` | default |
| attachment resolves and is a mesh | `Mesh attachment not found.` | `not_found` |
| each `vertices[i]` a non-negative integer | `vertex index must be a non-negative integer.` | default |
| each in range | `vertex index is outside the target mesh.` | default |
| `vertices` present but empty | `mesh.rebind_weights requires at least one vertex when 'vertices' is given.` | default |
| no repeated index | `A vertex was selected more than once.` | default |
| setup transform invertible | `Bone 'x' has a singular setup transform and cannot be rebound.` | `invalid_request` |
| result finite | `Rebinding produced a non-finite bind offset.` | `invalid_request` |

`not_found` is reserved for the unresolvable attachment, matching the
convention MAR-173 recorded.

Success message: `"Rebound mesh weights successfully."` A rebind in which no
offset moves returns `no_change` with `"Mesh weights already bound to the setup
pose."`, using the same `CommitPolicy` shape `normalize_weights` uses at
`agent_handlers_editing.cpp:2621-2628`.

### 9.3 `normalize_weights` gains `vertices`

Same optional array, same index validation, same "absent means all" rule. Every
shipped call — which cannot contain the field — behaves identically.

### 9.4 Dry-run and the affected-vertex report

AC5 requires "affected-vertex" behaviour. All three weight mutations return the
same payload shape, on both the dry-run and the live path:

```jsonc
{
  "dry_run": true,
  "vertex_count": 4,               // vertices in the attachment (shipped field)
  "scoped_vertex_count": 2,        // vertices this call addresses
  "affected_vertices": [1, 2],     // ascending; those whose canonical form differs
  "changed": true
}
```

`vertex_count` keeps its shipped meaning and position so the existing
`agent_dispatch_smoke` dry-run rows (`:1164-1170`) keep passing unchanged.

A dry run runs the **entire** preflight — parse, canonicalize, rebind — against
a `ProjectData` copy and then discards it. It must not change
`project_revision()`, `undo_count()`, or `dirty()`. That triple is asserted for
all three operations.

### 9.5 MCP tools

`tools/mcp/tools/editing.py` gains one `types.Tool` for `mesh.rebind_weights`
placed immediately after `normalize_weights` (`:1052-1065`), and a `vertices`
property on `normalize_weights`:

```python
"vertices": {"type": "array", "items": {"type": "number", "minimum": 0}}
```

`set_vertex_weights`'s schema is unchanged except that its `normalize` property
gains a description recording C1. The schema stays advisory: the C++ gate is
authoritative, and `test_client.py` proves an explicit `"normalize": false` is
rejected by C++ even though the JSON schema still admits it.

`tools/mcp/test_client.py` asserts `len(registry_names) == 61` (`:49`) and
`len(mcp_names) == 61` (`:51`).

---

## 10. Every Place the Registry Total Appears

Derived automatically — no edit needed:

- `agent_operation_descriptor_count()`, `agent_dispatch.cpp:1187-1189`, returns
  `std::size(kOperationSpecs)`.
- `agent_dispatch.cpp:723`, `:1172` reserve from the same expression.

Hand-edited, `60 → 61`:

| Site | Occurrences |
|---|---|
| `src/samples/agent_dispatch_smoke.cpp` | the expectation table gains a `mesh.rebind_weights` row after `:83`; any explicit total |
| `src/editor/shell_smoke_graph.cpp` | **seven** `!= 60U` guards + seven message strings: `:148/:149`, `:636/:637`, `:1558/:1559`, `:2128/:2129`, `:3142/:3143`, `:3957/:3958`, `:4603/:4604` |
| `tools/mcp/test_client.py` | `:49`, `:51` |
| `AGENTS.md` | `:161` (registry validation line — states what the surface **is**) |
| `docs/root1/editing-gap-analysis.md` | `:23`, `:85`, `:192`, `:455` — all four state what the surface **is** |
| `docs/root1/refector.md` | `:20`, `:112` — both say "**current**", so both update |

**Prose rule.** A sentence stating what the surface *is* takes 61. A sentence
carrying a date, milestone, or checkpoint stays historical.
`docs/root1/discription.md:44` ("...56-operation Agent/MCP surface는 변경하지
않는다" inside the MAR-164 paragraph) and `:53`'s MAR-173 record ("정확히 60
operation이 됐다") are historical and **stay**. `refector.md:20`'s recital of the
44/49/56/57/58/59/60 progression is historical up to the final clause, which is
the one that changes.

The guard count is the number that grows most often. Task 0 re-greps it and the
patch is written so a miscount aborts the build rather than silently leaving a
stale guard.

**Known false positives — never touch:** `IM_COL32(56, 61, 69, 255)`,
`(51, 56, 64)`, `"x": 56.0`, `56,995,840` bytes,
`PhysicsBoneState … 56 bytes/bone`, `IM_COL32(208,134,57,230)`,
`rgb(54,57,64)`, `shell_smoke_graph.cpp:2810`'s `4360U`.

---

## 11. Non-Goals

1. **Everything in MAR-176.** No candidate-bone checklist, no inverse-square
   distance, no vertex-to-segment geometry, no nearest-candidate fallback, no
   automatic choice of which bones influence a vertex. MAR-175 canonicalizes
   what a human or a script names; MAR-176 decides what to name. The seam is
   `canonicalize_mesh_weight_vertex`, which MAR-176 calls as its last step.
2. **Mesh topology.** Vertices, triangles, UVs, hulls, automesh, Weld.
3. **Weight copy/paste and mirroring.** Unscheduled.
4. **Multi-vertex numeric entry.** §8.2.
5. **Changing the brush transaction model.** §7.1.
6. **A `.marrow` schema change.** §3.3.
7. **Skinning evaluation and the renderer.**
8. **Preview playback speed.** MAR-174.
9. **Weight-aware Problems entries.** `docs/root1/editing-gap-analysis.md:435`
   lists "weight canonical normalize" in the Problems safe-fix allowlist. That
   allowlist is a later story; MAR-175 only supplies the primitive it will call.

---

## 12. Compatibility Decisions Requiring Justification

Restating §4's two changes as formal decisions, because they alter a shipped
public surface.

**C1 — `set_vertex_weights` rejects `"normalize": false`.**
*Alternatives considered:* (a) honour it — re-opens defects V1 and V2, so a
successful call can produce an unsavable project; rejected. (b) silently ignore
it — returns `ok: true` for a request whose stated intent was not performed,
which is the exact shape of the MAR-172 defect; rejected. (c) reject —
chosen. *Blast radius:* only callers that send the field explicitly and set it
to `false`. `true` and omission are unaffected, and no in-tree caller sends it.
*Migration note:* `AGENTS.md` and `docs/root1/agent-control.md:91`.

**C2 — `normalize_weights` also drops, merges, sorts, and caps.**
*Justification:* the shipped narrow version cannot produce canonical output and
cannot repair an over-cap attachment materialized from a runtime document, so
"normalized" did not mean "savable". *Preserved:* the operation name, arguments,
`no_change` disposition, and the exact message
`"Mesh weights already normalized."` *Blast radius:* the numeric output of the
operation on non-canonical input, which is by construction input that could not
be saved before.

**A third, GUI-only change:** brush strokes now emit influences sorted
descending by weight where a list of four or fewer previously kept insertion
order (D3). This changes the *serialized order* of `mesh_edits.weights[v]`, so
the `.marrow` bytes for a repainted attachment differ from what the same stroke
produced before MAR-175. It does not change any weight *value*. Because
`skeleton_skin.cpp:563-564` accumulates in array order and floating-point
addition is not associative, a multi-influence vertex's skinned position can
differ in the last ULP. Existing tolerances (`require_near`) absorb this; it is
recorded so nobody later reports it as a regression.

---

## 13. Validation Strategy

Designed against the five recorded lessons. Each subsection names the binary.

### 13.1 `marrow_mesh_weight_model_tests` — the canonical rules (NEW)

New target mirroring `marrow_timeline_model_tests` (`CMakeLists.txt:631-644`).
Cases, each asserting the exact resulting influence vector bit-exactly on
`double`:

- Drop: a `0.0` entry, a `-0.5` entry, a `1e-9` entry — each removed.
- Merge: `[spine .5, spine .5]` → one `spine 1.0` with the weight-weighted mean
  bind offset; `[spine .25 @(10,0), spine .75 @(20,0)]` → bind `(17.5, 0)`.
- Sort: four bones supplied in ascending weight order come back descending.
- Tie-break: two bones with **identical** weights come back in ascending
  skeleton-index order, and the same pair supplied in the opposite input order
  produces a **bit-identical** result — the property that proves the comparator
  is a total order and not `stable_sort`.
- Cap: six influences → the four largest, chosen independently of input order.
- Cap after merge: five entries that merge to four are kept, not truncated to
  three.
- Normalize: sum exactly `1.0` as a double.
- **Bit-exact idempotence:** `canonicalize(canonicalize(v))` byte-equals
  `canonicalize(v)` for every case above, including one whose normalized weights
  are not representable exactly (`1/3, 1/3, 1/3`). This is the case that fails
  without §6.2 step 8's exact-`1.0` skip.
- Reject: NaN weight, `+Inf` weight, NaN bind `x`, unknown bone, empty bone
  name, all-zero list. Each asserts the message **and** that `*vertex` is
  byte-unchanged.
- Rebind: single-influence round-trip within `1e-9`; two-influence rebind moves
  both offsets to the same setup-world point within `1e-9`; a singular setup
  transform rejects and leaves the vertex unchanged; repeat rebind is
  **bit-identical** across runs. The determinism-vs-idempotence distinction from
  §6.4 is printed by the test.

### 13.2 `marrow_project_smoke` — the project primitives and export

**Lesson 1, the export gap.** Every export assertion here operates on a project
that was *actually mutated and then written*, not on a copy the exporter never
saw. The sequence is fixed: mutate through the primitive → `save_project()` to a
temp path → `export_runtime_skeleton()` → reload the `.mskl` → assert the
decoded weight values. A test that asserts on the in-memory candidate only does
not count.

**Lesson 2, choosing a signal that discriminates.** Two export cases, and the
test prints why each signal was chosen:

- **Case A, topology-changing.** Canonicalize a vertex that has a duplicate bone
  and a zero-weight entry, so the influence count drops from 4 to 2. Every
  influence contributes 4 JSON numbers, each encoded as a `Number` tag plus a
  fixed-width float32 (`src/runtime/binary.cpp:141-146`, `:236`), and arrays are
  length-prefixed varints — so the `.mbin` **size must shrink**, by a computable
  amount. Assert size decreased *and* assert the two surviving decoded weights.
- **Case B, value-only.** Renormalize a vertex whose influence *set* is
  unchanged. The `.mbin` size **must be identical**, because float32 is fixed
  width — the same tell MAR-173 recorded for key times. Assert size unchanged
  and assert the specific decoded float32 weight
  (`static_cast<float>(expected)`), never that the float32 sum is exactly `1.0f`
  (§6.5). The test prints: *"size is not a signal for a value-only weight edit;
  the decoded value is."*

**Lesson 3, survival.** Every mutation case reads back at least one *adjacent*
vertex and asserts it is byte-identical, and one *adjacent attachment edit* and
asserts the same. A scoped `normalize_mesh_weights` over vertices `{1}` must
leave `0`, `2`, `3` bit-unchanged. A `rebind_mesh_weights` must leave every
`weight` bit-unchanged and every `triangle`, `uv`, and base `vertices` entry
identical.

Rejection cases assert `serialize_project()` is **byte-identical** before and
after, which is the project-wide statement of AC4.

Plus: a save → reload → re-canonicalize round trip proving canonical output is a
fixed point of the `.marrow` loader, and a negative proving a hand-built
non-canonical project (duplicate bone) is rejected by `save_project()` — the
regression guard for defects V1/V2.

### 13.3 `marrow_editor_shell --auto-close 2` — the shell smokes

The existing weight-paint block in `validate_timeline_project_smoke`
(`src/editor/shell_smoke_timeline.cpp:1610-~1900`) is **extended, not replaced**.
Its shipped expectations — baseline weights `1.0 / 0.75 / 0.25`, the
blue/green/yellow/red heat ramp (`:1794-1804`), Paint `0.75 → 0.875` (`:1828`),
per-vertex total `1.0` (`:1829`), one undo entry per stroke (`:1821`), the
Erase and Smooth sequences, and undo/redo — must keep passing **unchanged**,
because they are the inverted gate for the behaviour-preserving extraction
(Task 1).

The one exception is D9's fix (§7.1). The existing block scrubs to the attack
pose at `:1701` before painting; once new-influence binds come from the setup
pose, any expectation that depends on a *newly added* bone's bind offset
changes. The shipped assertions are all about **weight values** on bones the
vertices already have, so the expectation is that none of them move — but Task 4
must verify that empirically and, if one does move, record the old and new
numbers and the reason rather than adjusting the number quietly.

New coverage, added to the same block:

- Replace at `strength = 1.0` drives the active bone to `1.0` in one sample;
  Replace at `0.4` yields `0.4` on the active bone; Replace composes with
  falloff and pressure.
- The numeric table: reading it on a single-vertex FFD selection; committing a
  weight; exactly one history entry per commit; the renormalized redisplay;
  add-influence disabled at four; remove-influence; removing the last influence
  is refused.
- Selected-scope Normalize: two vertices selected, two normalized, the other two
  **byte-identical**; no selection normalizes all; a no-op scope adds no history
  and does not dirty.
- Rebind: bind offsets change, weights do not, `triangles`/`uvs`/base
  `vertices` do not; one history entry; undo restores the offsets bit-exactly.
- Rejection: each of the four AC4 classes (invalid bone, non-finite value, empty
  result, failed rebind) leaves `serialize_project()` byte-identical, the
  preview un-rolled-back, and `undo_count()` unchanged.
- **The AC1/AC6 cross-path identity test.** Drive one vertex to the same
  intended influence set through (i) the brush, (ii) the numeric table, (iii)
  `set_vertex_weights`, and assert all three produce a **bit-identical**
  `MeshWeightVertexEdit`. This is the single test that proves "identical
  canonical output across every manual path" rather than merely asserting each
  path individually.
- Save → reload → assert the reloaded project's weight vertices are bit-identical
  to the pre-save ones.

### 13.4 `marrow_agent_dispatch_smoke` — the agent surface

**Lesson 4: every canonical rule is asserted on the agent surface too**, not
only in the GUI.

- The expectation table gains the `mesh.rebind_weights` row after `:83`; the
  registry total becomes 61.
- The shipped dry-run rows (`:1164-1170`) keep passing, and gain
  `scoped_vertex_count` / `affected_vertices` assertions.
- The shipped `normalize_weights idempotent` row (`:2932-2941`) — `no_change`
  and a null scene delta — keeps passing verbatim.
- New: `set_vertex_weights` with a duplicate bone now **succeeds and merges**
  (previously it succeeded and produced an unsavable project); the read-back
  proves one merged influence with the summed weight.
- New: `set_vertex_weights` with a zero-weight influence drops it, and the
  read-back proves the survivor sums to `1.0`.
- New: `set_vertex_weights` with a NaN weight **rejects**, and
  `serialize_project()` is byte-identical.
- New: `"normalize": false` rejects with `invalid_request` (C1).
- New: `normalize_weights` with `vertices: [1]` leaves vertices `0`, `2`, `3`
  **byte-identical** — the survival assertion.
- New: `mesh.rebind_weights` dry-run → live → read-back → `no_change` on repeat
  → undo → read-back, asserting the pre-rebind offsets return bit-exactly.
- New: every rejection case for the new operation from §9.2's table, each
  followed by a proven-unchanged project.
- New: an **adjacent-data survival** check in the MAR-172 shape — after any
  accepted weight mutation on attachment A, an authored weight edit on
  attachment B is read back and asserted byte-identical.

### 13.5 `tools/mcp/test_client.py` — parity

- `61/61` exact C++/Python name parity (`:49`, `:51`).
- An explicit registry-metadata row for `mesh.rebind_weights`.
- Dry-run → live → read-back → undo → read-back for `mesh.rebind_weights`.
- `normalize_weights` with and without `vertices`.
- Rejection of `"normalize": false`, a non-integer vertex index, an
  out-of-range index, an empty `vertices` array, and a five-influence
  `set_vertex_weights` — the last proving the advisory `maxItems: 4` did not
  loosen the C++ gate.
- `--parameter-only` unchanged.

### 13.6 Regression and compatibility gates

Unchanged, all must pass: `marrow_unit_tests`, `marrow_fixture_smoke` (JSON and
`.mbin`), `marrow_spine_import_smoke` (which carries the owl zero-weight
weighted-mesh and tank weighted-clipping regressions —
`AGENTS.md:120`), `marrow_renderer_sample --skip-render` (GPU-skinned
weighted-mesh preparation), `marrow_c_smoke`, `marrow_timeline_model_tests`,
`marrow_timeline_graph_model_tests`, `marrow_viewport_interaction_tests`,
`marrow_selection_tests`, and `marrow_inspect --compare` on both the fixture and
the exported bundle.

---

## 14. Decisions Taken Under Ambiguity

**D-1. The registry grows by one, to 61.** The brief allows for MAR-175 being
pure consolidation at 60. It is not: AC5 names weight *bind* as needing matching
C++ registry and MCP behaviour, and no rebind operation exists. Rebind becomes
`mesh.rebind_weights`. Selected-scope Normalize is a new *argument*, not a new
operation, and the numeric table is served by the existing
`set_vertex_weights` — so exactly one is added, not three.

**D-2. `normalize: false` rejects rather than being ignored.** §12, C1. The
alternative that preserves the wire contract most literally (honour it) is the
one that preserves defects V1 and V2, so literal preservation is not available.
Loud rejection beats silent divergence, and only an explicit `false` is
affected.

**D-3. Materialization from the runtime does not canonicalize.** Opening a
project or materializing an imported attachment must not silently rewrite
weights the user never touched. Canonicalization is implicit on **write** and
explicit via `normalize_weights` for **repair**. AC2 says "every *result*",
which is a statement about what an authoring operation produces, not about what
a read produces.

**D-4. The tie-break is skeleton bone index, ascending.** AC2 says "descending
weight then skeleton order". Skeleton order means `SkeletonData::bones()` index,
which is stable across a load, is what the runtime's `bone_index` already is,
and is what MAR-176's AC3 also names as its distance tie-break — so the two
stories share one definition.

**D-5. `kMeshWeightEpsilon` stays `1e-6`.** It is the brush's shipped value
(`shell_weight_paint.cpp:17`). Choosing a smaller one would change brush
behaviour on existing fixtures for no stated benefit; the story is about making
the paths agree, and they agree at `1e-6`.

**D-6. Empty result rejects at the primitive; the brush degrades it to a no-op
sample.** The primitive must reject, because both file-format loaders require at
least one influence. The brush must not abort a stroke over one bad vertex.
These are reconciled at the sample layer, not by weakening the primitive
(§7.1).

**D-7. The brush keeps its non-transactional per-sample path.** D8 is not a
divergence to eliminate. `apply_weight_paint_sample` explicitly documents why it
does not deep-copy `ProjectData` (`shell_weight_paint.cpp:850-852`). Unification
is about the *rules*, not the transaction mechanics; the two-layer split in §5.2
is what lets those differ safely.

**D-8. Rebind is deterministic but not bit-exactly idempotent, and the tests say
so.** `BoneWorldTransform` is float32 (`skeleton.hpp:1009`) while bind offsets
are double, so `S(S^{-1}(V)) != V` bit-for-bit. Rather than assert a property
that is false, the tests assert repeat-run determinism bit-exactly and
second-application stability to `1e-9`, and print the distinction.

**D-9. Bind `x`/`y` are read-only in the numeric table.** They are geometry, and
a hand-typed offset in one bone's frame with no corresponding change in the
others' is exactly the inconsistency Rebind exists to fix. Offering both an
un-validated editor and a repair button for the same field invites the user to
create work for themselves.

**D-10. The numeric table is single-vertex.** §8.2. AC3 says "active-vertex".

**D-11. `"<unresolved>"` is asserted against rather than fixed.** §2.5. The path
appears unreachable and this story does not have the evidence to claim
otherwise, so it adds a cheap assertion on the canonical write path instead of
restructuring skin resolution.

**D-12. `set_vertex_weights` keeps rejecting more than four influences at the
argument parser.** It is a shipped, documented gate mirrored in the MCP schema.
Canonicalization truncates for paths that can legitimately overflow (Smooth,
merge-then-cap); the agent's explicit gate stays because a script sending five
bones has made an error, not expressed an intent.

**D-13. No `.marrow` schema change and no new `ProjectData` member.** Everything
MAR-175 does is expressible inside the existing `mesh_edits.weights` overlay,
and `docs/root1/editing-gap-analysis.md:166` says so explicitly ("이 작업은 기존
`mesh_edits.weights` overlay 안에서 끝난다").

**D-14. `mesh.` is the namespace for the new operation.** `mesh.describe`
already establishes it (`agent_dispatch.cpp:39`), and every operation added
since MAR-128 is dotted. The two legacy undotted weight names are not renamed —
renaming a shipped operation is a break with no benefit here.

---

## 15. Documentation and Milestone Closure

- `AGENTS.md`: registry line `:161` → 61; the shell-smoke line `:165` gains
  Replace/numeric/Normalize/Rebind alongside "brush-based mesh weight painting";
  a new MAR-175 validation table in the established shape; the C1/C2 migration
  note.
- `docs/root1/discription.md`: a new MAR-175 paragraph in the §53 style, stating
  the canonical rule, the two-layer split, the setup-pose bind fix, the
  registry going to 61, and that `.mskl` v1 / `.mbin` v2 / C ABI v1 / the
  `.marrow` schema are unchanged. The MAR-164 and MAR-173 historical counts stay.
- `docs/root1/editing-gap-analysis.md`: `:23`, `:85`, `:192`, `:455` → 61;
  `:69`'s weight-painting row gains the four new surfaces; `:157-160`'s missing
  features table drops the "웨이트 Replace 모드·수치 직접 편집·명시적 정규화
  버튼" row; `:164-166`'s paragraph is rewritten in the past tense; `:411`'s
  canonical-rule sentence gets a pointer to the primitive.
- `docs/root1/agent-control.md`: `:36` and `:91` gain `mesh.rebind_weights`, the
  `vertices` scope, and the C1 note.
- `docs/root1/refector.md`: `:20`, `:112` → 61.
- `docs/root1/format-spec.md`: `:937-939` gains one sentence recording that
  `mesh_edits.weights` vertices are canonical on write — same schema, narrower
  values.
- `.agents/tasks/prd-marrow-runtime.json`: MAR-175 `status: "done"`,
  `completedAt: "2026-08-30"`.
