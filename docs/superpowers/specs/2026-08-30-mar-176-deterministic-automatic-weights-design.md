# MAR-176 Deterministic Automatic Weights Design

Story: `MAR-176`, "Generate deterministic automatic weights",
`dependsOn: ["MAR-175"]`.

```
Compute repeatable top-four weights for existing mesh vertices from an explicit
candidate-bone set in setup pose.

AC1  The operation requires an explicit checked candidate-bone list and never
     silently expands it to the whole skeleton.
AC2  Weights derive from inverse-square distance between each setup-world vertex
     and each candidate setup-world bone segment.
AC3  The closest four candidates are chosen deterministically with skeleton
     order as the distance tie-break, then pass through the MAR-175
     canonicalization rules.
AC4  Zero-length segments and isolated or otherwise degenerate vertices fall
     back to the nearest valid candidate; an empty candidate set rejects without
     mutation.
AC5  GUI and matching C++ registry/Python MCP operations support selected scope,
     dry-run summaries, one-step undo, and export-preview rebuild.
AC6  Unit and smoke tests cover joints, equal distances, zero-length bones,
     isolated vertices, repeated runs, candidate subsets, save/reload, and
     JSON/MBIN export.
```

---

## 0. Relationship to MAR-175, and to MAR-177

MAR-175 built the single canonicalizer every weight write goes through and
routed five divergent paths onto it. MAR-176 is the **last** story in that arc:
it adds one producer of influence lists — a geometric one — and feeds it into
the same canonicalizer. It adds no new rule about what a valid weight list is,
because MAR-175 owns that.

The seam is exactly what MAR-175's §11 predicted:

> MAR-175 canonicalizes what a human or a script names; MAR-176 decides what to
> name. The seam is `canonicalize_mesh_weight_vertex`, which MAR-176 calls as
> its last step.

MAR-176 consumes four things from MAR-175, all as-built:

| Consumed | Where | Used for |
|---|---|---|
| `canonicalize_mesh_weight_vertex()` | `src/editor/mesh_weight_model.cpp:56` | the last step of every generated vertex |
| `setup_pose_bone_world_transforms()` | `:196` | the setup pose the whole algorithm is expressed in |
| `inverse_transform_point_safe()` | `:33` | the generated influences' bind offsets |
| step 1 of `rebind_mesh_weight_vertex()` | `:224-268` | the setup-world position of an existing weighted vertex |

The fourth is not currently callable on its own; §6.1 extracts it, unchanged,
so that generate and rebind cannot drift apart.

MAR-177 ("Add constraint lifecycle project operations") is a different subsystem
entirely — `.marrow.constraint_edits.operations`, rename/delete tombstones, four
constraint families. It shares no data structure, no primitive, and no operation
with weights. Nothing in MAR-176 anticipates it, and MAR-176 is the end of the
weights arc (§11).

---

## 1. Goal

A weighted mesh in Marrow can only get its influences from a human: the brush,
the numeric influence table, or a scripted `set_vertex_weights`. There is no way
to say "bind this mesh to these bones" and have Marrow work out the assignment.
Every vertex must be painted.

MAR-176 adds exactly that, under one governing constraint the story states in
its title: the result must be **deterministic**. The same mesh, the same
skeleton, and the same checked candidate list must produce the same influence
lists — the same bones in the same order with bit-identical weights — every run,
in the GUI, through the agent registry, and through MCP.

That constraint is not decoration. It is what makes the operation reviewable
(a diff of two runs is empty), scriptable (a build step can regenerate weights
and expect a byte-identical project), and testable against a specific expected
value rather than against itself. §7 is the whole design of that property, and
§7.6 states precisely what is and is not guaranteed.

This is a **single-algorithm** story with one new operation and one new GUI
surface. It is not a re-architecture, it changes no file format, and it adds no
`ProjectData` member.

---

## 2. What the tree already provides, measured

Every row here was read out of the tree at `feat/mar-168` (`cf6a199`, after
MAR-175 landed at `3a96cc5`).

### 2.1 There is no bone length in Marrow

`runtime::BoneData` (`include/marrow/runtime/skeleton.hpp:60-65`) is exactly
`{name, parent_index, setup_pose, inherit}`. `BoneTransform` (`:30-55`) is
`{x, y, rotation, scale_x, scale_y, shear_x, shear_y}`. **There is no `length`
field anywhere in the runtime, the parser, or the `.mskl` schema** — `grep -n
length include/marrow/runtime/skeleton.hpp` returns nothing, and the only
`"length"` in `src/runtime/skeleton_parse.cpp` is the path-constraint spacing
mode at `:543`.

So AC2's "bone segment" cannot be Spine's `origin + length * local_x`. It has to
be derived from the hierarchy, and Marrow already has exactly one definition of
it (§2.2).

### 2.2 Marrow already defines a bone's segment: parent origin → own origin

The viewport draws a bone as a line from its **parent's** world origin to its
**own** world origin, and hit-tests it as that segment:

```
src/editor/shell_viewport.cpp:1659-1675   append_colored_line(parent.screen_position, node.screen_position)
src/editor/shell_viewport.cpp:2127-2140   point_segment_distance_squared(position, parent.screen_position, node.screen_position)
```

Both skip a bone with no parent (`:1659`, `:2128`), so a root bone has no drawn
body at all.

This is the definition MAR-176 adopts (§5.1). It is the only one a user can see,
which matters because the user is the one checking the candidate boxes.

### 2.3 A weighted vertex's rest position lives only in its weights

`Skeleton::evaluate_mesh_attachment_pose` (`src/runtime/skeleton_skin.cpp:525-570`)
reads `geometry.vertices` **only for the count**:

```cpp
src/runtime/skeleton_skin.cpp:529
    const std::size_t vertex_count = geometry.vertices.size() / 2;
```

and then never touches its values in the weighted branch — the position comes
entirely from `geometry.weights[i]` at `:542-565`. The `.mskl` parser fills
`vertices` and `weights` from two independent JSON arrays
(`src/runtime/skeleton_parse.cpp:1540-1620`), so for a weighted mesh the
`vertices` array is **decorative**: it is in an unspecified frame, nothing
validates it against the weights, and nothing consumes it.

**Consequence, and it is load-bearing.** The only authoritative setup-world
position of a weighted vertex is

```
    V = ( sum_i w_i * S_i(x_i, y_i) ) / ( sum_i w_i )
```

which is step 1 of `rebind_mesh_weight_vertex()` (`mesh_weight_model.cpp:236-293`),
including the divide-by-total that recovers a weighted *average* from a
materialized runtime vertex whose weights need not sum to one. MAR-176 must use
that, not `geometry.vertices`. §6.1 extracts it rather than re-deriving it.

### 2.4 The as-built canonicalizer, and the two facts that constrain a generator

`canonicalize_mesh_weight_vertex()` (`mesh_weight_model.cpp:56-166`) applies, in
order: reject non-finite → reject empty/unknown bone name → merge duplicate
bones → **drop `weight <= 1e-6`** → sort (weight desc, skeleton index asc) → cap
at 4 → reject empty → normalize if `|sum - 1| > 4 * DBL_EPSILON`, then **sort
again**.

Two of those steps constrain any generator:

**F1 — the drop threshold is absolute, and it is applied *before* normalization**
(`:120-127`). A generator that hands the canonicalizer raw, unnormalized weights
is handing an absolute `1e-6` gate an arbitrary-scale number. §5.4 shows this
breaks on rigs Marrow already ships.

**F2 — the second sort exists because division can create ties**
(`:150-156`). A generator whose top-four weights can be near-ties therefore has
to accept that its own distance ordering is *not* the ordering that ships; the
canonical order is. §7.5 shows this is reachable and gives the witness.

### 2.5 The three project primitives are a template

`set_mesh_vertex_weights` / `normalize_mesh_weights` / `rebind_mesh_weights`
(`include/marrow/editor/authoring.hpp:598-633`,
`src/editor/authoring.cpp:4212-4370`) share one shape: copy `ProjectData`,
`ensure_weight_edit`, `resolve_weight_scope`, stage every vertex, compare with
the **exact** `weight_vertices_equal` (`authoring.cpp:4164-4180`), assign only
what differs, then a single `*project = std::move(candidate)` if anything
changed. `MeshWeightResult` already carries `vertex_count`,
`scoped_vertex_count`, `affected_vertices`, and `changed`.

`generate_mesh_weights` is a fourth member of that family and copies the shape
exactly (§8).

### 2.6 The agent handler is already shaped for a fourth operation

`agent_handlers_editing.cpp:2523-2712` handles the weight family as one block:
one `if` over three op names, a shared `apply_weight_edit` lambda, a shared
`build_payload`, a shared dry-run/transaction/commit path, and per-op
`CommitPolicy` selection. Adding a fourth op is a name in the `if`, a parse
branch, one lambda arm, and one `CommitPolicy`.

### 2.7 Measured counts, as built

| Thing | As built | Where |
|---|---|---|
| `std::size(kOperationSpecs)` | **61** (inspection 12, validation 3, management 10, edit 36) | `src/editor/agent_dispatch.cpp:29-91` |
| `kExpectedOperations` array size | **61** | `src/samples/agent_dispatch_smoke.cpp:39` |
| `!= 61U` guards in `shell_smoke_graph.cpp` | **7** | `:148, :636, :1558, :2128, :3142, :3957, :4603` |
| `!= 61U` guards in `shell_smoke_timeline.cpp` | **1** | `:3419` |
| MCP parity asserts | **2** | `tools/mcp/test_client.py:50, :52` |

Task 0 re-measures all of these rather than trusting this table.

---

## 3. Scope

### 3.1 In scope

1. Setup-pose bone **segments** derived from the hierarchy, in `mesh_weight_model` (§5.1).
2. A double-precision, `sqrt`-free point-to-segment squared distance in the same
   translation unit (§5.2).
3. `generate_mesh_weight_vertex()` — the per-vertex algorithm (§5.3-§5.6).
4. `setup_world_position_of_weight_vertex()` — extracted verbatim from rebind's
   step 1 so generate and rebind share it (§6.1).
5. `generate_mesh_weights()` — the `ProjectData` primitive (§8).
6. One new agent operation, `mesh.generate_weights`, and its MCP tool (§9).
   Registry **61 → 62**.
7. A candidate-bone checklist and a `Generate` button in the viewport weight
   panel (§10).

### 3.2 Out of scope

**Everything about *which* bones a user should check.** No automatic candidate
selection, no "bones within a radius", no "bones of the slot's skin", no
heuristic seed. AC1 forbids it in as many words, and §4 explains why a radius
parameter is a worse version of the checklist.

**Bone heat, geodesic distance, and any diffusion solver.** AC2 names
inverse-square distance. §4.1 records why the alternatives were not available
anyway.

**Adding weights to an unweighted mesh.** The story says "for existing mesh
vertices". An unweighted mesh attachment has no `weights` array, therefore no
`MeshWeightVertexEdit`, therefore no setup-world position (§2.3) — its rest
position is defined against the *slot's* bone through a different evaluation
path. Turning an unweighted mesh into a weighted one changes the attachment's
kind in the exported `.mskl`, which is a format-visible change this story does
not make. Rejected with a named error (§9.2, D-6).

**Mesh topology of every kind.** Vertices, triangles, UVs, hulls, automesh,
Weld. `mesh_edits` stays a weights-only overlay
(`docs/root1/format-spec.md:934-939`).

**Weight mirroring, copy/paste, and per-bone envelopes.** Unscheduled
(`docs/root1/editing-gap-analysis.md:161`).

**Everything in MAR-177.** Constraint rename/delete, `constraint_edits.operations`,
tombstones. A different subsystem.

### 3.3 Compatibility boundaries — all unchanged

| Surface | Version | Change |
|---|---|---|
| `.mskl` | v1 | none |
| `.mbin` | v2 | none |
| `.marrow` schema | current | **none** — `mesh_edits.weights` keeps its exact shape |
| C ABI | v1 | none |
| `editor-settings.json` | v1 | none — the candidate checklist is a tool setting and is not persisted (§10.3) |
| `ProjectData` | — | **no new member** |

The generator's output is a canonical vertex, which MAR-175 already proved is
strictly narrower than what the `.marrow` loader and `validate_project_for_save`
accept. There is no migration and no version bump.

---

## 4. Choosing the algorithm

AC2 fixes it: *"Weights derive from inverse-square distance between each
setup-world vertex and each candidate setup-world bone segment."* This section
records why that is also the right choice against what Marrow actually has, so
that the constraint is understood rather than merely obeyed.

### 4.1 Why not bone heat, and why not a diffusion solver

Bone heat (Baran & Popović) solves a Laplace equation over the mesh interior,
which needs (a) a manifold triangulation with reliable interior/exterior
classification, (b) a linear solver, and (c) a visibility test between each
vertex and each bone. Marrow's `MeshGeometry` is a flat 2D triangle soup
(`skeleton.hpp:212-228`) with no manifold guarantee, no half-edge structure, and
no interior. The fixture mesh is two triangles. There is no linear-algebra
dependency in the tree, and adding an iterative solver would make bit-exact
determinism a *convergence* question rather than an arithmetic one — which is
precisely the property this story is named after.

Envelope/falloff-radius schemes need a per-bone radius. There is no bone length
to derive one from (§2.1), so every radius would be a hand-tuned number, i.e.
exactly the kind of hidden knob that makes two runs disagree when a rig is
rescaled.

Inverse-square distance needs nothing but the two things Marrow does have: bone
world origins in setup pose, and vertex positions in the same frame. It is a
closed-form expression over `+ - * /`, which is what makes §7 possible.

### 4.2 The one genuinely free choice, and it is taken away

Inverse-square has a natural free parameter — the exponent — and AC2 fixes it at
2. MAR-176 therefore introduces **no new tunable numeric constant**. There is no
falloff radius, no smoothing pass, no minimum-weight slider, no exponent. The
two constants in play are MAR-175's, unchanged and reused by name:
`kMaxMeshWeightInfluences = 4` and `kMeshWeightEpsilon = 1e-6`.

This is stated as a design property, not an omission: every parameter that does
not exist is a parameter two runs cannot disagree about.

### 4.3 Why the candidate list is a checklist and not a radius

AC1 requires an explicit checked list. The alternative — "every bone within
R units" — is worse on three counts, and it is worth writing down because it is
the obvious suggestion:

1. `R` is scale-dependent, and Marrow's shipped rigs span an order of magnitude
   (fixture max `|world|` ≈ 230; `tank` ≈ 2395, measured in §5.4). One default
   cannot serve both.
2. A radius silently *expands* the candidate set as the rig changes, which is
   the exact behaviour AC1 forbids.
3. A radius does not remove the need for a checklist. A user binding an arm mesh
   does not want the spine included merely because it is close.

The candidate list **is** the locality control. There is no radius.

---

## 5. The algorithm

### 5.1 Setup-pose bone segments

```cpp
/** @brief One bone's setup-pose body, as the viewport draws it. */
struct BoneSetupSegment {
    double start_x{0.0};   ///< parent's setup-world origin, or own for a root
    double start_y{0.0};
    double end_x{0.0};     ///< own setup-world origin
    double end_y{0.0};
};

std::vector<BoneSetupSegment> bone_setup_segments(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<marrow::runtime::BoneWorldTransform>& setup_transforms);
```

For bone `i` with `parent_index p`:

- `end` is always `(setup_transforms[i].world_x, setup_transforms[i].world_y)`,
  promoted from `float` to `double`.
- `start` is the parent's world origin when `p` resolves and `*p < size`,
  otherwise `end` — i.e. a root bone, or a bone whose parent index is out of
  range, degenerates to the **point** at its own origin.

A bone whose local translation is `(0, 0)` also degenerates to a point, because
its world origin equals its parent's. That is AC4's "zero-length segment", and
it needs no special case beyond §5.2's `ab2 == 0` branch. The shipped fixture
has one root (`root`) and no zero-length child; the `.mskl` importer produces
zero-length bones routinely (a chain's terminal helper), which is why the branch
is exercised by a synthetic case rather than only by the fixture.

Ordering: the returned vector is indexed by skeleton bone index, built by a
single ascending loop. No map, no set, no name lookup.

### 5.2 Point-to-segment squared distance, in double, without `sqrt`

```cpp
/**
 * @brief Squared distance from a world point to a bone's setup segment.
 *
 * Squared, not linear, on purpose: the weight is `1 / d^2`, so the square root
 * would be taken only to be undone. Removing it removes two roundings per
 * candidate and one library call whose exactness is not guaranteed by IEEE-754
 * for every implementation.
 */
double point_segment_distance_squared(
    double point_x, double point_y, const BoneSetupSegment& segment);
```

```
    ab  = (end - start)
    ab2 = ab.x*ab.x + ab.y*ab.y
    if !(ab2 > 0):                       // zero-length segment, or a NaN
        return |point - start|^2         // AC4's zero-length branch
    t   = clamp( ((point-start) . ab) / ab2 , 0, 1 )
    c   = start + ab * t
    return |point - c|^2
```

This mirrors `shell_viewport.cpp:438-457` structurally, deliberately, so that
the number the generator uses and the number the hit-test uses are the same
geometric quantity. It does **not** share the code: the shell version is `float`
in screen space and lives in the executable, which `src/tests/` cannot link
(the constraint MAR-175 recorded at spec §5.1).

The degenerate test is `!(ab2 > 0.0)`, not `ab2 <= 1e-6f` as the screen-space
version uses. Two reasons, both deliberate:

- An epsilon here would be a **world-space** threshold, i.e. a scale-dependent
  knob, which §4.2 rules out.
- No epsilon is needed for correctness. For any `ab2 > 0` the division is
  finite, `t` is clamped into `[0, 1]`, and the closest point lies on the
  segment. A segment of length `1e-15` returns the same answer a point does, to
  within the segment's own length.

Writing it as `!(ab2 > 0.0)` rather than `ab2 == 0.0` also routes a NaN — which
can only arrive from a non-finite setup transform — into the point branch, where
the non-finite guard in §5.3 catches it, instead of into an unclamped `t`.

### 5.3 The per-vertex generator

```cpp
/**
 * @brief Replaces one vertex's influences with the inverse-square assignment
 *        over an explicit candidate set.
 *
 * Returns an empty string on success and leaves `*vertex` untouched on any
 * rejection. `candidate_bone_indices` must be non-empty, strictly ascending,
 * and free of duplicates -- the caller validates that once per call.
 */
std::string generate_mesh_weight_vertex(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<marrow::runtime::BoneWorldTransform>& setup_transforms,
    const std::vector<BoneSetupSegment>& segments,
    const std::vector<std::size_t>& candidate_bone_indices,
    marrow::editor::MeshWeightVertexEdit* vertex);
```

Steps, in this exact order:

**1. Recover the vertex's setup-world position.**
`setup_world_position_of_weight_vertex()` (§6.1) on the vertex's **existing**
influences. Rejects, atomically, on: an empty influence list
(`"A weighted vertex must keep at least one positive influence."`), an unknown
bone, a bone outside the setup pose, a non-finite intermediate, or a
non-positive weight total
(`"Weighted vertex influences must sum to a positive weight."`). These are
rebind's messages, reused verbatim, because it is rebind's code.

**2. Measure.** For each candidate `i`, `d2[i] = point_segment_distance_squared(V, segments[i])`.
If any `d2[i]` is not finite, reject the vertex:
`"Bone '<name>' has a non-finite setup-pose segment."`

**3. Order.** Sort the `(d2, bone_index)` pairs by ascending `d2`, then
ascending `bone_index`. Bone indices are unique by precondition, so this is a
**strict total order** and the sorted permutation is unique (§7.2).

**4. The nearest-candidate fallback.** If `d2[0] == 0.0` — the vertex lies
exactly on the first candidate's segment — emit **one** influence: the first
candidate, weight `1.0`. This is AC4's "isolated or otherwise degenerate
vertices fall back to the nearest valid candidate" and it is also the only
correct answer: `1/0` is not a weight.

**5. Cap.** `K = min(4, candidates.size())`; keep the first `K`. Because step 3
is a total order, *which* four survive is a function of the input set alone,
never of the order the caller supplied them in.

**6. Weigh.** `r[j] = 1.0 / d2[j]` for `j < K`. If any `r[j]` is not finite —
reachable only if `d2[j]` underflowed to a denormal — fall back to step 4's
single nearest candidate rather than rejecting: a vertex sitting `1e-160` units
from a bone is the same authoring intent as a vertex sitting on it.

**7. Normalize, before handing over.** `S = r[0] + r[1] + ... + r[K-1]`
accumulated left to right in the step-3 order. If `S` is not finite or not
positive, fall back to step 4. Otherwise `w[j] = r[j] / S`.

**8. Bind.** For each surviving candidate `j`,
`(x_j, y_j) = inverse_transform_point_safe(setup_transforms[bone_j], V)`.
A singular transform cannot occur here because the caller rejected singular
candidates once per call (§8.2); the per-vertex code still checks and returns
`"Bone '<name>' has a singular setup transform and cannot be used as a weight candidate."`
rather than trusting a precondition.

**9. Canonicalize.** Build a `MeshWeightVertexEdit` from the `K` influences in
step-3 order and call `canonicalize_mesh_weight_vertex()`. Assign to `*vertex`
only if it returns empty.

### 5.4 Why step 7 exists: without it, the operation breaks on rigs Marrow ships

This is the design's single most important non-obvious decision, and it was
verified numerically rather than argued.

The canonicalizer drops any influence with `weight <= 1e-6` **before**
normalizing (F1, `mesh_weight_model.cpp:114-124`). A raw inverse-square weight
is `1/d²` in world units squared, so the gate `1/d² <= 1e-6` is the gate
`d >= 1000 world units`. Measured against the fixture's own geometry:

| `d²` (fixture, candidates `spine`/`arm_l`) | raw `1/d²` at 1× | at 10× | at 100× |
|---|---|---|---|
| 4996 | 2.00e-04 keep | 2.00e-06 keep | 2.00e-08 **dropped** |
| 9256 | 1.08e-04 keep | 1.08e-06 keep | 1.08e-08 **dropped** |
| 10496 | 9.53e-05 keep | **9.53e-07 dropped** | 9.53e-09 **dropped** |

At **ten times** the fixture's scale a legitimate influence is already silently
deleted, and at a hundred times every influence on the vertex is deleted, which
makes `canonicalize_mesh_weight_vertex()` reject with *"A weighted vertex must
keep at least one positive influence."* — the operation fails outright.

That is not a hypothetical scale. Maximum bone world-origin magnitude in the
rigs the repository already tests against
(`assets/spine-examples/*/`, computed by walking `bones[]`):

| rig | bones | max &#124;world&#124; | median |
|---|---|---|---|
| `player_idle` (fixture) | 16 | 230 | ~140 |
| `goblins` | 21 | 291 | 139 |
| `spineboy` | 67 | 645 | 290 |
| `raptor` | 76 | 723 | 451 |
| `owl` | 20 | 809 | 429 |
| `tank` | 115 | **2395** | 674 |

`tank` is ten times the fixture. A design that hands raw weights to the
canonicalizer is a design that works on the fixture and fails on `tank`.

Pre-normalizing turns the absolute `1e-6` gate into a **relative** one — "this
candidate contributes less than one part in `10^6` of the four nearest" — which
is the semantics anyone would want, and it makes the whole assignment
mathematically invariant under a uniform rescale of the rig (bit-invariance is
not claimed; see §7.6).

It also buys a second, smaller property: with `K <= 4` weights summing to one,
the largest is at least `0.25`, so **the canonicalizer's empty-result rejection
is unreachable from the generator**. A generated vertex always survives.

### 5.5 Cap first, then normalize — and why not the other way round

Steps 5 and 7 are in that order. Normalizing over all `N` candidates first and
letting the canonicalizer cap and renormalize would give a different (also
deterministic) answer, and a worse one for two reasons:

- It guarantees a second division inside the canonicalizer, so the numbers the
  generator computes are never the numbers that ship.
- The relative drop threshold would then be relative to *all* candidates rather
  than to the four that were kept, which is not what "the closest four" means.

Capping first means that whenever nothing is dropped and the sum lands inside
`4 * DBL_EPSILON` of one, the canonicalizer's normalization branch does not
fire and the generator's own arithmetic is what reaches the project bit for bit.
Verified for all four fixture vertices across three candidate sets: the largest
`|sum - 1|` observed was `2.22e-16`, against a tolerance of `8.88e-16`. This is
a measured convenience, not a guaranteed one, and no test asserts the branch is
skipped.

### 5.6 Worked example, on the shipped fixture

`assets/fixtures/player_idle.mskl`, skin `mesh_base`, slot `body`, attachment
`body_mesh`, four vertices. Setup-pose bone world origins:
`root (0,0)`, `spine (0,50)`, `arm_l (-30,60)`. Segments:
`spine = [(0,0),(0,50)]`, `arm_l = [(0,50),(-30,60)]`, `root` = the point `(0,0)`.

Setup-world vertex positions, recovered by §6.1 from the fixture's own weights
(and exactly, as doubles — `51.2/0.8` is `64.0` and `104.0/0.8` is `130.0`):

| vertex | fixture influences | `V` |
|---|---|---|
| 0 | `spine(-64,-80) w1.0` | `(-64, -30)` |
| 1 | `spine(64,-80) w0.6`, `arm_l(94,-90) w0.2` | `(64, -30)` |
| 2 | `spine(64,80) w0.2`, `arm_l(94,70) w0.6` | `(64, 130)` |
| 3 | `spine(-64,80) w0.6`, `arm_l(-34,70) w0.2` | `(-64, 130)` |

Generating with candidates `{spine, arm_l}`:

| v | closest on `spine` | `d²` | closest on `arm_l` | `d²` | `w(spine)` | `w(arm_l)` |
|---|---|---|---|---|---|---|
| 0 | `(0,0)` (t=-0.6→0) | 4996 | `(-30,60)` (t=1.12→1) | 9256 | `0.64945270839180458` | `0.35054729160819531` |
| 1 | `(0,0)` (t=-0.6→0) | 4996 | `(0,50)` (t=-2.72→0) | 10496 | `0.67751097340562871` | `0.32248902659437134` |
| 2 | `(0,50)` (t=2.6→1) | 10496 | `(0,50)` (t=-1.12→0) | 10496 | **`0.5`** | **`0.5`** |
| 3 | `(0,50)` (t=2.6→1) | 10496 | `(-30,60)` (t=2.72→1) | 6056 | `0.36587723537941036` | `0.6341227646205897` |

The generated bind offsets are `V - spine_origin` and `V - arm_l_origin`:

| v | generated `spine` bind | generated `arm_l` bind |
|---|---|---|
| 0 | `(-64, -80)` | `(-34, -90)` |
| 1 | `(64, -80)` | `(94, -90)` |
| 2 | `(64, 80)` | `(94, 70)` |
| 3 | `(-64, 80)` | `(-34, 70)` |

Every one of those is **the offset the fixture already carries**, and the one
new offset, `arm_l(-34, -90)` on vertex 0, is the exact value MAR-175 recorded
as "what setup requires" when it fixed the paint-pose defect
(`AGENTS.md:295` and the D9 row). Generating with `{spine, arm_l}` therefore
reproduces the fixture's authored geometry and changes only the weights — which
is a strong sanity signal in its own right, and it is why this candidate set is
the one the tests use.

Vertex 2 is the acceptance signal, and §7.4 proves it is exact.

---

## 6. Changes to MAR-175's primitive

### 6.1 Extract rebind's step 1 (behaviour-preserving)

```cpp
/**
 * @brief The setup-world position a weighted vertex's influences describe.
 *
 * `V = (sum_i w_i * S_i(x_i,y_i)) / (sum_i w_i)`, with the division performed
 * only when the total is outside `kMeshWeightSumTolerance` -- a canonical
 * vertex is therefore unaffected bit for bit, while a vertex materialized from
 * a runtime document whose weights do not sum to one still yields its weighted
 * average rather than a scaled skinning sum.
 *
 * Rejects atomically on an empty influence list, an unknown bone, a bone
 * outside the setup pose, a non-finite intermediate, or a non-positive total.
 */
std::string setup_world_position_of_weight_vertex(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<marrow::runtime::BoneWorldTransform>& setup_transforms,
    const marrow::editor::MeshWeightVertexEdit& vertex,
    double* world_x,
    double* world_y);
```

This is `mesh_weight_model.cpp:236-293` moved out **verbatim** — the same
resolution loop, the same messages, the same order of operations, the same
tolerance test. `rebind_mesh_weight_vertex()` then calls it. The extraction
changes no behaviour and is gated as such: MAR-175's fourteen
`marrow_mesh_weight_model_tests` cases, including the rebind determinism cases,
must pass unchanged before anything new is written (plan Task 1).

The alternative — letting the generator re-derive `V` — is rejected outright.
Two implementations of the same formula, one of which contains a
`kMeshWeightSumTolerance` branch and the other of which might not, is exactly
the D7-shaped defect MAR-175 spent a story eliminating.

### 6.2 Nothing else changes

`canonicalize_mesh_weight_vertex`, `mesh_weight_edit_from_runtime`,
`setup_pose_bone_world_transforms`, `inverse_transform_point_safe`, and the
public behaviour of `rebind_mesh_weight_vertex` are untouched. No constant is
retuned. No step is added, removed, or reordered.

---

## 7. Determinism

This is the story's title and this section is its specification.

### 7.1 What is guaranteed

For a fixed `(SkeletonData, MeshWeightAttachmentEdit, candidate list, scope)`
and a fixed binary, `generate_mesh_weights()` produces a **bit-identical**
`ProjectData` — identical bone names in identical order with identical
`double` weights and identical `double` bind offsets:

- across repeated calls in one process;
- across separate runs of the process;
- across the three entry points (GUI command, agent operation, MCP tool), which
  share one implementation;
- independently of the order the caller listed the candidate bones in;
- independently of the playhead, the current animation, the preview pose, the
  selection, the FFD overlay, and any brush state — the algorithm reads only the
  setup pose (`setup_pose_bone_world_transforms()` builds a scratch
  `runtime::Skeleton` and calls `set_to_setup_pose()`,
  `mesh_weight_model.cpp:201-217`).

### 7.2 Iteration order depends on nothing implicit

Every container in the algorithm is a `std::vector` indexed by, or sorted on,
the **skeleton bone index**. There is no `std::unordered_map`, no
`std::unordered_set`, no `std::map`, no pointer-keyed container, and no
iteration over a hash table anywhere in the generator or in the code it calls:

| Container | Ordering guarantee |
|---|---|
| `setup_transforms` | `std::vector`, index = bone index, built by an ascending loop (`mesh_weight_model.cpp:209-216`) |
| `segments` | `std::vector`, index = bone index, ascending loop (§5.1) |
| `candidate_bone_indices` | `std::vector<std::size_t>`, sorted ascending and de-duplicated by the caller (§8.2) |
| the `(d2, index)` array | `std::vector`, built in candidate order, then `std::sort`ed on a **total** order |
| the staged vertices | `std::vector<std::pair<index, vertex>>`, built over `resolve_weight_scope`'s ascending output (`authoring.cpp:4182-4207`) |
| `MeshWeightVertexEdit::influences` | `std::vector`, written in canonical order by the canonicalizer |

`std::sort` is not stable, and that is irrelevant here: the comparator
`(d2 asc, bone_index asc)` is a strict total order because bone indices are
unique after de-duplication, so the sorted sequence is unique and independent of
which algorithm the standard library picked. This is the same argument
`canonical_order` already carries at `mesh_weight_model.cpp:23-28`.

The single `std::unordered_map` anywhere near this code is
`StringTableBuilder::indices` (`src/runtime/binary.cpp:149`), used only for
lookup during `.mbin` encoding — the table's *order* comes from
`values`, a `std::vector`, and it is not on the generator's path.

### 7.3 Floating point

- **Operations used:** `+`, `-`, `*`, `/`, and comparison. Nothing else.
  All four are correctly rounded by IEEE-754, so each one is reproducible on any
  conformant implementation given identical operands.
- **Not used:** `std::sqrt` (removed by §5.2's squared formulation),
  `std::hypot`, `std::pow`, any trig, `std::fma`, `std::accumulate` with an
  execution policy, and any `<numeric>` reduction.
- **No parallelism.** The vertex loop, the candidate loop, and the summation are
  plain sequential loops. No OpenMP, no `std::execution`, no thread pool. Marrow
  has no parallel-algorithm dependency and this story does not add one.
- **Summation order is fixed and stated:** `S` is accumulated left to right over
  the top-`K` candidates **in the sorted order** (§5.3 step 7), which is a
  deterministic function of the input. It is never accumulated in candidate
  order, in bone-index order, or in whatever order a container happened to
  yield.
- **Accumulation width:** every accumulator is a `double`. The only `float`
  values that enter are the six components of each `BoneWorldTransform`
  (`skeleton.hpp:1000-1007`), which are promoted to `double` before any
  arithmetic — the same promotion `inverse_transform_point_safe` already
  performs (`mesh_weight_model.cpp:36-40`).

### 7.4 The acceptance value, and why it is exact

Vertex 2 with candidates `{spine, arm_l}` generates **exactly `spine 0.5`, then
`arm_l 0.5`** — not "0.5 to within a tolerance". The proof does not depend on
the fixture's numbers being round:

- `V = (64, 130)` is above and right of the `spine` segment's end, so
  `t = 2.6` clamps to `1` and the closest point on `spine` is
  **`spine`'s world origin**.
- `V` is on the far side of `arm_l`'s segment start, so `t = -1.12` clamps to
  `0` and the closest point on `arm_l` is its segment *start*, which is
  **`spine`'s world origin** — the same `double` pair, by construction (§5.1).
- Both distances therefore evaluate the *identical expression*
  `(V.x - sx)² + (V.y - sy)²` on the *identical* operands, so they are equal
  bit for bit whatever `(sx, sy)` happens to be, and whatever the compiler does
  about contraction, because it does the same thing to both.
- `r = 1/d²` is then the same `double` twice, `S = r + r` is exact (a power-of-two
  multiple), and `r / S` is exactly `0.5`.

Verified by perturbing the setup origins by `0`, `1e-6`, `-3.7e-6`, and `1e-4`:
the two `d²` values stayed bit-identical and the weights stayed exactly
`(0.5, 0.5)` in every case. This matters because §7.6 says the setup pose is
composed in `float32` and carries real error — this signal is immune to it.

The same argument gives a **three-way** exact tie: with candidates
`{root, spine, arm_l, pivot}`, vertex 0 at `(-64,-30)` is behind all three of
`root`'s degenerate point, `spine`'s segment, and `pivot`'s segment, so all
three clamp to `root`'s world origin and all three yield `d² = 4996` bit for
bit. The three weights are then identical doubles
(`0.28250518862165791`) and the **skeleton-index tie-break decides the whole
order**: `root(0)`, `spine(1)`, `pivot(12)`, then `arm_l(2)` at
`0.15248443413502624`. AC3's tie-break is not a corner the tests have to
synthesize; the shipped fixture contains it.

### 7.5 The generator can produce a tie the canonicalizer has to re-sort

MAR-175's `mesh_weight_model.hpp:106-115` warns that division can collapse two
weights differing by one ULP into an exact tie, which is why the canonicalizer
sorts a second time. Two facts about that, both verified rather than assumed:

- **Distinct distances can give identical raw weights.** Searching adjacent
  doubles upward from `3.0`: `d² = 3.0000000000000004` and
  `d² = 3.000000000000001` are distinct doubles whose reciprocals are both
  `0.33333333333333326`. So the generator's distance order and the
  canonicalizer's weight order genuinely can disagree, and when they do the
  **canonical order is the one that ships**. The generator does not promise its
  distance order survives; it promises the canonical order.
- **Post-normalization collapse is reachable at four influences.** With two
  raws `0.0997656434843038` and `nextafter(that, 0)` plus two others, the
  normalized weights are both exactly `0.260400641574814`. A two-influence
  search over 400 000 random pairs found no collapse; a four-influence search
  found one within 500 000. So MAR-175's second sort is live for a generated
  vertex, and a generator on a symmetric rig — where two candidates are very
  nearly equidistant — is precisely the thing that reaches it.

Neither observation changes the algorithm. Both are recorded so that nobody
later "simplifies" the canonicalizer's second sort on the grounds that only
hand-authored input could reach it.

### 7.6 What is **not** guaranteed, stated plainly

**Cross-architecture and cross-compiler bit-identity is not claimed.** The
hazard is floating-point contraction: a compiler may fuse `a*b + c` into an
`fma`, changing the result by up to half an ULP, and whether it does depends on
the target ISA and the compiler's `-ffp-contract` default. `ab2 = ax*ax + ay*ay`
and the dot product in §5.2 are exactly that shape. Marrow adds no
`-ffast-math` and no `-ffp-contract` flag of its own (`CMakeLists.txt` has one
`add_compile_options`, `/utf-8` at `:19`), so the default applies. This story
does not attempt to defeat it: pinning contraction would need a per-translation-unit
pragma whose spelling differs per compiler, and the property it would buy —
byte-identical `.marrow` files across a Mac and a Linux CI box — is not
something any acceptance criterion asks for.

What *is* claimed, and tested, is bit-identity within one binary (§7.1), plus a
primary acceptance value chosen so that it survives contraction anyway (§7.4).

**Generate is deterministic but not bit-exactly idempotent**, and for exactly
the reason MAR-175 recorded for rebind (`mesh_weight_model.hpp:168-179`,
`AGENTS.md:295`): `BoneWorldTransform` is six `float`s while bind offsets are
`double`, so `S(S⁻¹(V))` does not reproduce `V` bit for bit. A second generate
recovers `V` from the influences the first one wrote, so its `d²` values differ
in the last ULPs and its weights can differ in the last ULPs.

Consequences that must be designed for rather than discovered:

- A repeated `mesh.generate_weights` on the same scope may report
  `changed: true` with `affected_vertices` non-empty and deltas of a few ULPs.
  That is the transform asymmetry, not nondeterminism. The `no_change`
  disposition exists (§9.3) and fires when nothing moves, but **no test asserts
  that a second generate returns `no_change`**, because that would be asserting
  a property this design does not have.
- The tests separate the two properties explicitly: two generates *from the same
  starting project* are asserted bit-identical (that is determinism, and it does
  hold); a generate applied to a previous generate's output is asserted *stable*
  to a measured tolerance and the measurement is printed (that is idempotence,
  and it does not hold exactly). MAR-175 measured the analogous rebind figure at
  `9.948e-14`; MAR-176 measures its own rather than predicting it.
- The vertex-2 signal is immune to this too (§7.4), so the primary assertion is
  bit-exact even on a re-run.

---

## 8. The project primitive

### 8.1 Signature

```cpp
/**
 * @brief Regenerates the scoped vertices' influences from an explicit
 *        candidate-bone set, in setup pose.
 *
 * An empty `scope` means every vertex, matching the rest of the family.
 * `candidate_bone_names` is required and is never expanded: an empty list, an
 * unresolvable name, and a repeated name are all rejections.
 *
 * Every vertex keeps its setup-world position; only which bones hold it, with
 * what weights and what bind offsets, changes.
 */
MeshWeightResult generate_mesh_weights(
    ProjectData* project,
    const runtime::SkeletonData& skeleton,
    const runtime::AttachmentData& attachment,
    const MeshWeightTarget& target,
    const std::vector<std::string>& candidate_bone_names,
    const std::vector<std::size_t>& scope);
```

`MeshWeightResult` is reused unchanged. No new result type, no new field.

### 8.2 Once-per-call candidate validation, before any vertex is touched

| Rule | Message |
|---|---|
| non-empty | `mesh.generate_weights requires at least one candidate bone.` |
| each name non-empty and resolvable | `Bone not found: <name>` |
| each within `setup_transforms.size()` | `Bone '<name>' is outside the setup pose.` |
| no repeated bone | `A candidate bone was listed more than once.` |
| each setup transform invertible (`\|det\| > 1e-8`) | `Bone '<name>' has a singular setup transform and cannot be used as a weight candidate.` |

The singular-transform check is **up front and fatal for the whole call**, not
per vertex and not a silent exclusion. Silently dropping a bone the user
explicitly checked is the mirror image of AC1's forbidden silent expansion; a
zero-scale bone in a candidate list is an authoring mistake, and it should be
named once rather than producing a quietly different assignment on every vertex.

After validation the names are converted to a `std::vector<std::size_t>` of
skeleton indices, **sorted ascending**. Sorting here rather than preserving the
caller's order is what makes §7.1's "independently of the order the caller
listed the candidates in" true by construction rather than by accident of the
later sort.

### 8.3 The rest of the shape

Identical to `rebind_mesh_weights` (`authoring.cpp:4321-4370`): copy
`ProjectData`, `ensure_weight_edit`, `resolve_weight_scope`, build the setup
transforms and segments **once per call** (not per vertex), stage every vertex
through `generate_mesh_weight_vertex`, bail on the first rejection with the
candidate discarded, then compare with the exact `weight_vertices_equal` and
assign only what differs. A call in which nothing differs commits nothing.

An attachment with no weighted vertices — an unweighted mesh — resolves to
`edit.vertices.empty()`, and the primitive rejects before scope resolution:
`mesh.generate_weights requires a weighted mesh attachment.` (§3.2, D-6.)

---

## 9. Agent and MCP surface

### 9.1 The registry goes from 61 to 62

Measured as-built (§2.7): `std::size(kOperationSpecs)` is **61**. MAR-176 adds
exactly one:

```cpp
    {"mesh.generate_weights", "edit", true, false, true, true, &handle_editing_operation},
```

inserted immediately after `{"mesh.rebind_weights", ...}`
(`agent_dispatch.cpp:74`), keeping the weight family contiguous. Registry:
**62**.

Why one and not zero: AC5 says "GUI **and matching** C++ registry/Python MCP
operations", and `docs/root1/editing-gap-analysis.md:461` lists
"weight bind/auto-weight" among the P1 persistent mutations that must be exposed
on both surfaces. Why not two: the candidate list is an *argument*, and the
selected scope is the `vertices` argument the family already has.

Naming follows `mesh.rebind_weights` — dotted, `mesh.` namespace, verb-object.

### 9.2 `mesh.generate_weights`

```jsonc
{
  "op": "mesh.generate_weights",
  "args": {
    "skin": "mesh_base",              // required
    "slot": "body",                   // required
    "attachment": "body_mesh",        // required
    "bones": ["spine", "arm_l"],      // REQUIRED, non-empty, no duplicates
    "vertices": [0, 2],               // optional; absent = every vertex
    "dry_run": false                  // optional, default false
  }
}
```

`bones` is **required**. Omitting it is not "use every bone"; it is a rejection.
That is AC1 expressed on the wire, and it is the one place where a convenience
default would silently violate an acceptance criterion.

Validation, all before any mutation:

| Rule | Error | Code |
|---|---|---|
| `args` object present | `mesh.generate_weights requires 'args' object.` | default |
| `skin`/`slot`/`attachment` present | `mesh.generate_weights requires skin, slot, and attachment.` | default |
| attachment resolves and is a mesh | `Mesh attachment not found.` | `not_found` |
| attachment is weighted | `mesh.generate_weights requires a weighted mesh attachment.` | `invalid_request` |
| `bones` present and an array | `mesh.generate_weights requires a 'bones' array of candidate bone names.` | default |
| `bones` non-empty | `mesh.generate_weights requires at least one candidate bone.` | default |
| each entry a non-empty string | `candidate bone names must be non-empty strings.` | default |
| each resolves | `Bone not found: <name>` | `not_found` |
| no repeated bone | `A candidate bone was listed more than once.` | default |
| each invertible in setup | `Bone '<name>' has a singular setup transform and cannot be used as a weight candidate.` | `invalid_request` |
| each `vertices[i]` a non-negative integer | `vertex index must be a non-negative integer.` | default |
| each in range | `vertex index is outside the target mesh.` | default |
| `vertices` present but empty | `mesh.generate_weights requires at least one vertex when 'vertices' is given.` | default |
| no repeated index | `A vertex was selected more than once.` | default |
| every scoped vertex has influences to derive `V` from | `A weighted vertex must keep at least one positive influence.` | `invalid_request` |
| every scoped vertex's existing weights sum positive | `Weighted vertex influences must sum to a positive weight.` | `invalid_request` |

`not_found` is reserved for unresolvable *names* — the attachment and the bones
— matching the convention MAR-173 recorded and MAR-175 followed.

### 9.3 Dispositions and messages

| Situation | Message |
|---|---|
| success | `Generated mesh weights successfully.` |
| dry run | `Mesh weight generation validated.` |
| nothing changed | `Mesh weights already match the generated candidates.` (`NoChangeResult::Success`) |

The `no_change` branch exists for a scope whose vertices already hold exactly
the generated assignment — most commonly a re-run over a subset that was not
touched in between. Per §7.6 it is **not** asserted on a second application.

One history entry per call, reusing the family's existing label
`"Edit mesh weights via Agent"` (`agent_handlers_editing.cpp:2678`), because the
undo entry names the *surface*, not the operation, for every member of this
family.

### 9.4 The dry-run payload

Same shape as the other three, plus one field that only this operation emits:

```jsonc
{
  "dry_run": true,
  "vertex_count": 4,
  "scoped_vertex_count": 2,
  "affected_vertices": [0, 2],
  "changed": true,
  "candidate_bone_count": 2        // mesh.generate_weights only
}
```

`build_payload` (`agent_handlers_editing.cpp:2651-2671`) gains one conditional
`emplace` guarded on the op name, so the payloads of `set_vertex_weights`,
`normalize_weights`, and `mesh.rebind_weights` are **byte-identical** to what
ships today — every shipped field keeps its name, type, and position, which is
what `agent_dispatch_smoke.cpp:1164-1170` asserts.

A dry run runs the identical preflight a live call runs, against a copy, and
must not change `project_revision()`, `undo_count()`, or `dirty()`.

### 9.5 MCP tool

`tools/mcp/tools/editing.py` gains one `types.Tool` immediately after
`mesh.rebind_weights` (`:1082-1104`):

```python
types.Tool(
    name="mesh.generate_weights",
    description=(
        "Generate deterministic top-four weights for a weighted mesh from an "
        "explicit candidate-bone list, using inverse-square distance to each "
        "candidate's setup-pose bone segment."
    ),
    inputSchema={
        "type": "object",
        "properties": {
            "skin": {"type": "string"},
            "slot": {"type": "string"},
            "attachment": {"type": "string"},
            "bones": {
                "type": "array",
                "items": {"type": "string"},
                "minItems": 1,
                "description": "Candidate bone names. Required; never expanded to the whole skeleton."
            },
            "vertices": {
                "type": "array",
                "items": {"type": "number", "minimum": 0},
                "description": "Vertex indices to regenerate. Absent means every vertex."
            },
            "dry_run": {"type": "boolean"}
        },
        "required": ["skin", "slot", "attachment", "bones"]
    }
),
```

The schema stays advisory; the C++ gate is authoritative, and `test_client.py`
proves it by sending an empty `bones` array (which `minItems` forbids) and
asserting C++ rejects it.

`tools/mcp/test_client.py:50` and `:52` become `62`.

---

## 10. GUI

### 10.1 The candidate-bone checklist

A `Candidate bones` sub-section inside the existing `Weight Paint` collapsing
header (`shell_viewport_ui.cpp:1884-1975`), below the Normalize/Rebind row:

- a scrollable child region listing every bone in **skeleton order** with an
  `ImGui::Checkbox` each — skeleton order, not alphabetical, so the list matches
  the tie-break the algorithm uses and the order the hierarchy panel shows;
- `All` / `None` / `From selection` buttons, the last filling the checklist from
  the `BoneSelection` items in `state->selection.items()`
  (`include/marrow/editor/selection.hpp:25-34`);
- a `candidates: N` readout;
- a `Generate` button beside `Normalize` and `Rebind`, disabled when `N == 0`
  with the reason shown, and using the same scope rule
  (`weight_command_scope()`, `shell_weight_paint.hpp:56`).

`From selection` is a **one-shot fill**, not a live binding. Binding the
candidate set to the transient bone selection would mean the same click produces
different weights depending on what was selected a moment earlier, which is the
opposite of what this story is for.

### 10.2 The command

`generate_weights_command(ShellState*)` in `shell_weight_paint.cpp`, alongside
`normalize_weights_command` and `rebind_weights_command` (`:1057-1083`), routed
through the existing `run_weight_command` helper (`:975-1032`) so it inherits
the one-transaction / one-history-entry / no-op-commits-nothing contract for
free. Label: `"Generated weights on <attachment>"`.

### 10.3 Where the checklist lives, and where it does not

`WeightPaintSettings` (`shell_state.hpp:257-263`) gains
`std::vector<std::string> candidate_bone_names`. It is stored by **name**, not
by index, so a project reload that renumbers bones does not silently retarget
the set; a name that no longer resolves is shown struck through and makes
`Generate` reject rather than being dropped.

It is a tool setting, so it is deliberately **not** in `ProjectData`, **not**
serialized to `.marrow`, **not** in `editor-settings.json`, **not** in
`PreviewState`, and **not** in the history snapshot. This is MAR-174's precedent
applied unchanged: a field that undo rewrites but that
`history_snapshots_equal()` does not compare will silently jump on Ctrl+Z.

---

## 11. Non-goals

1. **Everything in MAR-177.** Constraint rename/delete, `constraint_edits.operations`,
   tombstones, materialization ordering. A different subsystem with no shared
   type.
2. **Automatic candidate selection.** AC1. No radius, no skin scoping, no
   heuristic seed (§4.3).
3. **Bone heat, geodesic distance, and any iterative solver** (§4.1).
4. **A falloff exponent, a radius, or any new tunable constant** (§4.2).
5. **Binding an unweighted mesh** (§3.2, §8.3).
6. **Mesh topology.** Vertices, triangles, UVs, hulls, automesh, Weld.
7. **Weight mirroring and copy/paste.** Unscheduled.
8. **Changing the brush, the numeric table, Normalize, or Rebind.** MAR-176 adds
   a fourth producer; it does not revisit the three MAR-175 shipped.
9. **A `.marrow` schema change, a `ProjectData` member, or a version bump** (§3.3).
10. **Skinning evaluation, GPU skinning, and the renderer.**
11. **Cross-architecture byte-identical output** (§7.6).

---

## 12. Every place the registry total appears

Derived automatically — no edit needed:

- `agent_operation_descriptor_count()`, `agent_dispatch.cpp:1142-1144`, returns
  `std::size(kOperationSpecs)`.
- `agent_dispatch.cpp:725`, `:1127` reserve from the same expression.

Hand-edited, `61 → 62`:

| Site | Occurrences |
|---|---|
| `src/samples/agent_dispatch_smoke.cpp` | `:39` `std::array<OperationExpectation, 61>`, plus a new `mesh.generate_weights` row after `:83` |
| `src/editor/shell_smoke_graph.cpp` | **seven** `!= 61U` guards + seven message strings: `:148/:149`, `:636/:637`, `:1558/:1559`, `:2128/:2129`, `:3142/:3143`, `:3957/:3958`, `:4603/:4604` |
| `src/editor/shell_smoke_timeline.cpp` | **one** guard + message: `:3419/:3420` |
| `tools/mcp/test_client.py` | `:50`, `:52` |
| `AGENTS.md` | `:161` (registry validation line — states what the surface **is**) |
| `docs/root1/editing-gap-analysis.md` | `:23`, `:85`, `:86`, `:192`, `:455` — all five state what the surface **is** |
| `docs/root1/refector.md` | `:20` — the final clause says "**current**", so it updates; the 44/49/56/57/58/59/60/61 recital before it is historical and stays |

`:86` is the row MAR-175's own spec missed (its §10 listed `:23`, `:85`, `:192`,
`:455` only); it reads `61-op registry와 Parameter shell을 ... 검증` and states
what the surface is, so it takes 62.

**Prose rule, unchanged from MAR-175.** A sentence stating what the surface *is*
takes 62. A sentence carrying a date, milestone, or checkpoint stays historical.
Specifically **historical, do not touch**: `AGENTS.md:311` and `:313` (the
MAR-175 validation record, "354 `[ OK ]` cases against the exact 61-operation
registry", "**61/61** exact C++/Python name parity"),
`docs/root1/discription.md:53`, `:54`, `:55` (the MAR-173/174/175 records, which
say "정확히 60 operation이 됐다" and "정확히 61 operation이 됐고").

**Known false positives — never touch:** `IM_COL32(56, 61, 69, 255)`,
`(51, 56, 64)`, `"x": 56.0`, `56,995,840` bytes,
`PhysicsBoneState … 56 bytes/bone`, `IM_COL32(208,134,57,230)`,
`rgb(54,57,64)`, `shell_smoke_graph.cpp:2873`'s `4361U`, and every `60 FPS` /
`1.0 / 60.0` in `editing-gap-analysis.md`, `format-spec.md`, and
`quick-start.md`.

The guard count grows most often. Task 0 re-greps it, and the patch is written
so a miscount aborts the build rather than leaving a stale guard.

---

## 13. Export signal

**Lesson 1 (the export gap).** Every export assertion operates on a project that
was actually mutated and then written: mutate through
`generate_mesh_weights()` → `save_project()` → `export_runtime_assets()` →
reload → assert the decoded values. Asserting on an in-memory candidate the
exporter never saw does not count.

**Lesson 2 (choose a signal that discriminates).** The `.mbin` encoder writes
every JSON number as a fixed-width float32 (`src/runtime/binary.cpp:141-146`,
reached from `encode_value`'s `Number` case at `:236`), so **size is a signal
for a change in influence count and is not a signal for a change in weight
value**. MAR-176 has one case of each.

The byte cost of one influence object, derived from the encoder rather than
fitted:

```
{"bone": <string>, "x": <num>, "y": <num>, "weight": <num>}

  1  Object tag                                    binary.cpp:250
  1  varint member count (4)                       binary.cpp:251
  +  varint(key index) per member          x4      binary.cpp:253
  1  String tag  + varint(bone name index)         binary.cpp:238-240
  3  Number tag  x3                                binary.cpp:235
 12  float32 x3                                    binary.cpp:141-146, :236
 ---
 18 + K   where K = the four key-index varints plus the bone-name-index varint
```

MAR-175 measured `K = 7` for this fixture, i.e. **25 bytes per influence**, over
a `4065 → 4040 → 4015` chain (`AGENTS.md:295`, the export row). That chain ended
at the fixture's natural two-influence state, so the **predicted** natural export
size is `4015` bytes. Task 0 measures it; if the measurement disagrees, the
model is wrong and the design is corrected before Task 6 is written.

**Case A — count-changing.** Generate over vertex `0` only, candidates
`{spine, arm_l}`. Vertex 0 goes from one influence to two (§5.6), so the encoded
document gains exactly one influence object.

- Both bone names are already interned — every bone name is in the string table
  from `bones[]` — so **no new string is added and no existing string index
  shifts**. This is why the candidate set is `{spine, arm_l}` and not a set
  containing an otherwise-unused bone: a new string would perturb the table and
  destroy the clean delta.
- Assert `.mbin` grew by **exactly 25 bytes** (`4015 → 4040` on the predicted
  baseline), and assert the model in the message rather than the constant.
- Assert both decoded weights: `spine` and `arm_l` at `0.64945270839180458` and
  `0.35054729160819531` in the `.mskl` (doubles, bit-exact), and the `.mbin`
  equivalence delegated to `validate_binary_export` / `marrow_inspect --compare`.
  Never assert that the float32 weights sum to exactly `1.0f`.

**Case B — value-only.** Generate over vertex `2` only, same candidates. Vertex 2
already holds `{spine, arm_l}` and its generated bind offsets are the ones it
already carries (§5.6), so the influence *set*, the count, and the offsets are
all unchanged. What moves is the pair of weights and the pair's **order**: the
fixture canonicalizes to `arm_l 0.7499999999999999`, `spine 0.25` (weight
descending), and generation produces `spine 0.5`, `arm_l 0.5` (the tie broken on
skeleton index, so `spine` moves to the front).

- Assert `.mbin` size **identical**. Both bone names are already interned and
  only their positions inside one array swap, so the encoding is the same
  length. Print the reason: *"float32 is fixed width, so size is not a signal
  for a value-only weight edit; the decoded value and the influence order are."*
- Assert the decoded `.mskl` weights are **exactly `0.5` and `0.5`**, and that
  `spine` precedes `arm_l`. This is the acceptance value of §7.4 and it is the
  one number in the whole story that is simultaneously exactly representable,
  independent of the `float32` setup-pose error, independent of FP contraction,
  and a direct consequence of AC3's tie-break rather than of the arithmetic
  happening to land there.

Both cases additionally read back an adjacent, unscoped vertex and an adjacent
attachment edit and assert them **byte-identical** (Lesson 3, survival).

---

## 14. Validation strategy

### 14.1 `marrow_mesh_weight_model_tests` — the algorithm

Extends the existing target (14 cases as built). New cases, each asserting the
resulting influence vector bit-exactly on `double`:

- **Segments.** A root bone yields a point at its own origin; a child yields
  parent→own; a bone with local `(0,0)` yields a zero-length segment; an
  out-of-range parent index degenerates to a point rather than reading out of
  bounds.
- **Distance.** Projection before the start clamps to the start; after the end
  clamps to the end; inside projects; a zero-length segment returns the point
  distance. Asserted on exact small integers so every intermediate is exact.
- **Two candidates, the fixture geometry.** §5.6's table, all four vertices,
  bit-exact.
- **Equal distance.** Vertex 2 → exactly `0.5 / 0.5` with `spine` first, and the
  same result with the candidate list supplied in the opposite order.
- **Three-way tie.** `{root, spine, arm_l, pivot}` on vertex 0 → three identical
  weights ordered `root, spine, pivot` by skeleton index, then `arm_l`.
- **Coincident vertex.** A vertex placed exactly on a candidate's segment →
  one influence, weight exactly `1.0`.
- **Zero-length bone in the candidate set** → treated as its point, no
  rejection, no `NaN`.
- **Single-bone skeleton / single candidate** → one influence, weight `1.0`.
- **More than four candidates** → exactly the four nearest, chosen identically
  for every permutation of the input list.
- **Scale.** The same relative assignment at 1× and at 100× the fixture scale —
  the §5.4 regression. This case **fails** if step 7's pre-normalization is
  removed, and the test says so in its message.
- **Determinism.** Every accepted case runs twice and the two results are
  compared with `memcmp`; every case is also run with the candidate list
  reversed and compared with `memcmp`.
- **Rejections**, each asserting the message **and** that `*vertex` is
  byte-unchanged (`memcmp`, because a `NaN` never equals itself): empty
  candidate list, unknown bone, repeated bone, singular setup transform, a
  vertex with no influences, a vertex whose weights sum to zero.
- **Extraction gate.** MAR-175's fourteen shipped cases pass unchanged, and one
  new case asserts `setup_world_position_of_weight_vertex()` reproduces the
  value rebind derives internally.

### 14.2 `marrow_project_smoke` — the primitive, survival, and export

- Scoped generate over `{0}` leaves vertices `1`, `2`, `3` **byte-identical**,
  and an adjacent attachment edit byte-identical.
- Unscoped generate touches all four.
- Two generates from the same starting project produce a **byte-identical**
  `serialize_project()` — the determinism assertion at project level.
- A generate applied to a previous generate's output is asserted **stable**, not
  identical, and the measured maximum weight delta is printed (§7.6).
- Every rejection from §9.2's table leaves `serialize_project()` byte-identical.
- Save → reload → generate again reproduces the same project, proving the
  generated form is a fixed point of the `.marrow` loader.
- **An actual `save_project()` round trip after every accepted generate**, since
  `validate_project_for_save` is the gate MAR-175's V1/V2 defects escaped
  (`project.cpp:5504-5568`).
- Export Cases A and B from §13.

### 14.3 `marrow_editor_shell --auto-close 2` — the GUI

Added to the existing weight block in
`src/editor/shell_smoke_timeline.cpp` (`:1610`-~`1900`). Every shipped
assertion in that block must keep passing unchanged.

- The checklist starts empty and `Generate` is disabled; checking one bone
  enables it.
- `From selection` fills the checklist from a two-bone selection and the
  subsequent generate uses exactly those two.
- Generate with a selected two-vertex FFD scope changes exactly those two and
  leaves the others byte-identical; with no selection it changes all four.
- One history entry per Generate; undo restores the previous influences
  **bit-exactly**; redo re-applies them bit-exactly.
- A generate whose scope changes nothing adds no history entry and does not
  dirty the project.
- Generating while scrubbed to `attack@0.2` produces the **same** result as
  generating at setup pose — the pose-independence assertion, and the direct
  descendant of MAR-175's D9 defect.
- Rejection: an empty checklist, and a checklist naming a bone that no longer
  resolves, each leave `serialize_project()` byte-identical and `undo_count()`
  unchanged.
- Cross-path identity: the GUI command and `mesh.generate_weights` with the same
  candidates and scope produce a **bit-identical** `MeshWeightVertexEdit`.

### 14.4 `marrow_agent_dispatch_smoke` — the agent surface

- Registry total **62**; the `mesh.generate_weights` expectation row sits
  immediately after `mesh.rebind_weights`.
- Every shipped weight row keeps passing verbatim, including
  `normalize_weights idempotent` (`no_change` + null scene delta,
  `:2932-2941`) and the dry-run payload rows (`:1164-1170`).
- Dry run → live → read-back through `mesh.describe` (which MAR-175 extended to
  report per-vertex influence values) → undo → read-back, asserting the
  pre-generate influences return bit-exactly.
- Two dry runs report identical `affected_vertices`.
- `bones` omitted, `bones: []`, `bones: ["nope"]`, `bones: ["spine","spine"]`,
  a non-string entry, an out-of-range vertex index, an empty `vertices` array,
  and an unweighted attachment — each rejects with its exact message and code
  and leaves the project provably unchanged.
- A scoped generate over `vertices: [0]` leaves `1`, `2`, `3` byte-identical,
  and an authored weight edit on a *different* attachment byte-identical.
- Dry runs change neither `project_revision()`, `undo_count()`, nor `dirty()`.

### 14.5 `tools/mcp/test_client.py` — parity

- `62/62` exact C++/Python name parity (`:50`, `:52`).
- An explicit registry-metadata row for `mesh.generate_weights`.
- Dry-run → live → read-back → second-application → undo → read-back, reporting
  the measured second-application stability rather than asserting bit-identity
  (§7.6).
- Candidate-subset coverage: `{spine}` alone, then `{spine, arm_l}`, asserting
  the one-candidate case gives weight `1.0`.
- Rejection of an empty `bones` array — proving the advisory `minItems: 1` did
  not loosen the C++ gate — plus a duplicate bone name and an unknown bone.
- `--parameter-only` unchanged.

### 14.6 Regression and compatibility gates

Unchanged, all must pass: `marrow_unit_tests`, `marrow_fixture_smoke` (JSON and
`.mbin`), `marrow_spine_import_smoke` (the owl zero-weight weighted-mesh and
tank weighted-clipping regressions — the importer does not route through the
editor primitive, so this is the guard that generation did not leak),
`marrow_renderer_sample --skip-render`, `marrow_c_smoke`,
`marrow_timeline_model_tests`, `marrow_timeline_graph_model_tests`,
`marrow_viewport_interaction_tests`, `marrow_selection_tests`,
`marrow_parameter_project_smoke`, and `marrow_inspect --compare` on both the
fixture and the exported bundle.

---

## 15. Decisions taken under ambiguity

**D-1. A bone's segment is parent origin → own origin.** There is no bone length
in Marrow (§2.1), so the segment has to come from the hierarchy, and the
viewport already defines and hit-tests exactly this segment
(`shell_viewport.cpp:1659-1675`, `:2127-2140`). Matching it means the geometry
the user checks boxes against is the geometry the algorithm measures. The
alternative — origin → each child's origin, giving a bone as many segments as it
has children — would make a leaf bone a point and a branching bone a fan, which
neither the viewport nor the user sees.

**D-2. The setup-world vertex position comes from the weights, not from
`geometry.vertices`.** Verified: the weighted branch of
`evaluate_mesh_attachment_pose` reads `vertices` only for its size
(`skeleton_skin.cpp:529`) and the two arrays are parsed independently
(`skeleton_parse.cpp:1540-1620`), so `vertices` is unvalidated and potentially
stale for a weighted mesh. Rebind's step 1 is the only authoritative derivation,
and §6.1 shares it rather than copying it.

**D-3. The generator normalizes before handing over.** §5.4. Verified
numerically: at 10× the fixture scale a legitimate influence is dropped, and at
100× the operation fails outright, on rigs (`tank`) the repository already
tests. This is the one place where the design departs from the naive reading of
AC3 ("then pass through the MAR-175 canonicalization rules") — the raw weights
are pre-scaled so those rules see a relative threshold instead of an absolute
one. Nothing in the canonicalization rules is changed.

**D-4. Cap to four, then normalize.** §5.5. The alternative gives a different,
also-deterministic answer and guarantees a second division. Measured: with
cap-then-normalize, all twelve fixture cases land inside
`kMeshWeightSumTolerance`, so the canonicalizer's division does not fire and the
generator's own arithmetic ships. No test asserts that.

**D-5. Existing painted weights are overwritten, not blended.** Blending would
make the result depend on prior content, which is the negation of the story's
title. Overwriting also makes the operation's output a function of
`(geometry, candidates)` alone, which is what lets §14.2 assert a *specific*
expected value.

**D-6. An unweighted mesh rejects.** §3.2. "Existing mesh vertices" in the story
text, no setup-world position available (D-2), and turning an unweighted
attachment into a weighted one is a format-visible change. Named rejection, not
a silent no-op.

**D-7. A singular candidate setup transform rejects the whole call, once.**
§8.2. Silently excluding a checked bone is the mirror image of AC1's forbidden
silent expansion, and a per-vertex rejection would make the failure depend on
which vertices happened to be in scope.

**D-8. `bones` is required on the wire, with no default.** §9.2. A default of
"every bone" is precisely what AC1 forbids, and a default of "the bones already
influencing the vertex" would make the operation a no-op for its main use.

**D-9. The candidate checklist is a tool setting, stored by name.** §10.3.
Bone names survive a reload that renumbers bones; a name that stops resolving is
shown and rejected rather than dropped. Keeping it out of `PreviewState` is
MAR-174's lesson applied unchanged.

**D-10. `From selection` is a one-shot fill, not a live binding.** §10.1. A live
binding would make the same button produce different weights depending on
transient selection state.

**D-11. Generate is deterministic but not bit-exactly idempotent, and the tests
say so.** §7.6. The asymmetry is inherited from `BoneWorldTransform` being
`float32` while bind offsets are `double` — the same one MAR-175 documented for
rebind and measured at `9.948e-14`. MAR-176 measures its own figure rather than
predicting it, and asserts determinism (two runs from the same input) rather
than idempotence (a run on a previous run's output).

**D-12. Cross-architecture bit-identity is not claimed.** §7.6. FP contraction
is compiler- and ISA-dependent and Marrow sets no `-ffp-contract`. Rather than
assert a property the build cannot guarantee, the design states the guarantee's
scope and picks a primary acceptance value that survives contraction anyway
(§7.4).

**D-13. `candidate_bone_count` is emitted only by the new operation.** §9.4.
Adding it unconditionally would change the shipped payload of three operations
whose exact shape `agent_dispatch_smoke.cpp:1164-1170` asserts.

**D-14. The predicted export baseline is `4015` bytes and `25` bytes per
influence, and Task 0 must verify it.** §13. The figure is derived from
`binary.cpp`'s encoder and from MAR-175's measured `4065 → 4040 → 4015` chain,
not measured directly by this document — no build was run while writing it. If
Task 0 measures something else, the encoding model in §13 is wrong and must be
re-derived before the export assertions are written. It must not be fitted to
whatever the build reports.

**Not verified in this document.** The following are stated from source reading
and arithmetic, not from a run, because writing this design involved no build:
the `4015` baseline (D-14); the exact magnitude of the `float32` setup-pose
error on this fixture, which `AGENTS.md:295` describes as "~1e-6" and which the
tolerances in §14 must be set from a measurement, not from that phrase; and the
claim that the canonicalizer's normalization branch does not fire for the
fixture cases (§5.5), which was verified in double-precision Python against
exact transforms but not against the `float32`-composed transforms the runtime
actually produces.

---

## 16. Documentation and milestone closure

- `AGENTS.md`: registry line `:161` → 62 and the operation named; the shell-smoke
  line `:165` gains the candidate checklist and Generate alongside the MAR-175
  surfaces; a new MAR-176 validation table in the established shape, including
  the determinism-versus-idempotence row and the measured stability figure.
- `docs/root1/discription.md`: a new MAR-176 paragraph in the §55 style —
  the segment definition and why (no bone length), the setup-world derivation
  and why (`geometry.vertices` is decorative for a weighted mesh), the
  pre-normalization and the 10×-scale evidence, the exact `0.5/0.5` tie signal,
  the registry going to 62, the determinism guarantee and its stated limits, and
  that `.mskl` v1 / `.mbin` v2 / C ABI v1 / the `.marrow` schema are unchanged.
  The MAR-173/174/175 historical counts stay.
- `docs/root1/editing-gap-analysis.md`: `:23`, `:85`, `:86`, `:192`, `:455` → 62;
  the weight-painting row gains automatic weight generation; the
  "weight bind/auto-weight" P1 line at `:461` is marked done.
- `docs/root1/agent-control.md`: `mesh.generate_weights`, its required `bones`
  argument, and the determinism note.
- `docs/root1/refector.md`: `:20`'s final clause → 62.
- `docs/root1/format-spec.md`: one sentence recording that generated weights use
  the same `mesh_edits.weights` shape — no schema change.
- `.agents/tasks/prd-marrow-runtime.json`: MAR-176 `status: "done"`,
  `completedAt`.
