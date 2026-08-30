# MAR-177 Constraint Lifecycle Project Operations Design

- Story: `MAR-177`, "Add constraint lifecycle project operations"
- `dependsOn`: `["MAR-176"]`
- Date: 2026-08-30
- Branch: `feat/mar-168`

---

## 0. Correcting the premise before designing anything

Every story in this chain has found an error in its governing brief. This one
found three, and two of them change what gets built.

**0.1 — The gap is not "create and delete". It is rename and delete.**

The commissioning brief for this plan hypothesised that, because the four agent
operations are all `edit_*`, "the gap is almost certainly create and delete."
The story text says something different and more specific:

> Represent **rename and delete** for all four constraint families as ordered
> optional project-overlay operations without changing runtime format versions.

The code agrees with the story, not with the hypothesis. **Create already
exists** on the GUI surface, for all four families:

| Family | Add button | Default builder |
| --- | --- | --- |
| IK | `shell_constraints.cpp:697` | `make_default_ik_constraint_edit`, `:478` |
| Path | `shell_constraints.cpp:859` | `make_default_path_constraint_edit`, `:504` |
| Transform | `shell_constraints.cpp:1111` | `make_default_transform_constraint_edit`, `:532` |
| Physics | `shell_constraints.cpp:1390` | `make_default_physics_constraint_edit`, `:569` |

Each allocates a fresh name through `unique_constraint_name()`
(`shell_constraints.cpp:225-241`), which probes the *materialized* skeleton via
`constraint_exists()` (`:185-201`) and so cannot collide with either a base or a
project-only constraint. Each default is already savable — this is verified in
§9.3 rather than assumed. **MAR-177 therefore adds no create path, and this
document specifies no defaults for a new constraint**, because it creates none.

What is missing, on every surface, is **rename** and **delete**. There is no
`Rename`, `Delete`, or `Remove` button in `shell_constraints.cpp` for a
constraint itself (`:936`/`:955`/`:1192`/`:1216`/`:1455`/`:1474` add and remove
*bones within* a constraint, not constraints), and no registry operation
(`agent_dispatch.cpp:76-79` registers exactly four, all `edit_*`).

**0.2 — The named edit primitives are not in `authoring.cpp`.**

The brief directs the reader to `include/marrow/editor/authoring.hpp` and
`src/editor/authoring.cpp` for `edit_ik_constraint`, `edit_path_constraint`,
`edit_transform_constraint`, and `edit_physics_constraint`. Those four names do
not exist as functions anywhere in the tree, and the string `constraint` does
not appear in `authoring.hpp` or `authoring.cpp` at all. They are **registry
operation names**, dispatched by `handle_constraint_operation()`
(`agent_handlers_constraints.cpp:664-812`), which mutates
`ProjectData::*_constraint_edits` directly inside an `EditorSession`
transaction. There is no `authoring.cpp` constraint layer to extend, and
MAR-177 does not create one.

**0.3 — `edit_*_constraint` is an upsert into the project, but it is not a
create.** It merges over an existing project entry, or materializes one from the
runtime constraint, and returns `not_found` when neither exists
(`agent_handlers_constraints.cpp:619-621`, `:729-735`). So the project vectors
are upsert vectors, but the agent cannot introduce a constraint that the base
skeleton does not already have.

---

## 1. Goal

Give `.marrow` a way to say **"this constraint is now called something else"**
and **"this constraint is gone"** — including for constraints that live in the
base `.mskl` and that the project therefore cannot edit by upsert — and make
the export pipeline honour both, deterministically and atomically, without
touching `.mskl` v1, `.mbin` v2, or C ABI v1.

MAR-177 is the **model layer**. MAR-178 is the **surface layer**. §3.2 makes
that split explicit and defends it against the story text.

---

## 2. What the tree already provides, measured

### 2.1 The overlay is a two-stage JSON merge, and it has exactly one shape

`build_runtime_document()` (`project.cpp:5014-5168`) deep-copies the base
`.mskl` document and overlays project data onto it. Constraints are the last
thing it does (`:5150-5167`):

```cpp
merge_named_object_array_member(&document.root, "ik",
    build_ik_constraint_edits_value(project.ik_constraint_edits));
// ... path, transform, physics
```

`merge_named_object_array_member()` (`project.cpp:4690-4737`) is a
**name-keyed upsert into a root array**:

- absent key → the whole array is inserted;
- present and an array → for each edit, find the element whose `name` matches;
  replace it in place if found, **append at the end** if not.

That is the entire vocabulary the overlay has today: *replace by name* and
*append*. It has no way to say *remove* and no way to say *this element's name
changed*. `merge_named_object_array_member` is also used for nothing else —
it is a four-call helper — so extending the constraint overlay does not disturb
any other section.

### 2.2 The runtime resolves constraint references by name, and rejects unknown ones

The base `.mskl` root arrays `ik`, `path`, `transform`, `physics` are the
constraint definitions (`skeleton_parse.cpp:2967`, `:3194`, `:3456`, `:3747`).
Names are required non-empty and unique **within a family**
(`skeleton_parse.cpp:3016-3036` for IK, mirrored per family). Two constraints in
*different* families may share a name; nothing forbids it.

Skins reference constraints **by name**:

```cpp
// skeleton_parse.cpp:5169-5207 — once per family, per skin
parse_skin_scope_members(document, *skin_value, "ik", "$.skins." + skin.name,
                         ik_constraints, "ik constraint", &skin.ik_constraint_indices);
```

and `parse_skin_scope_members()` (`skeleton_parse.cpp:5079-5130`) **fails the
whole load** on an unresolvable name:

```cpp
// skeleton_parse.cpp:5115-5123
const auto resolved_index = find_named_index(values, name_value.as_string());
if (!resolved_index.has_value()) {
    return validation_error(document, name_value.location(), item_path,
        "skin references unknown " + std::string(label) + " '" +
            name_value.as_string() + "'");
}
```

**This is the single most important fact in this document.** A delete that
removes a constraint from a root array without pruning the skins that name it
does not produce a subtly wrong rig. It produces a `.marrow` project that can
still be *saved* and can no longer be *opened, previewed, or exported*. §6
enumerates every referrer; §11 makes the export test discriminate on exactly
this.

### 2.3 Evaluation order is array order, per family

Each family is evaluated by a plain index loop over `SkeletonData`'s vector:
IK at `skeleton.cpp:370-398` and `:1494-1549`, transform at `:401-433` and
`:948-1028`, path at `:817-825`. Array position therefore *is* evaluation
order. The per-instance enable bitsets `active_ik_constraints_`,
`active_path_constraints_`, `active_transform_constraints_` and
`physics_constraint_states_` are **index-keyed** and sized at `Skeleton`
construction (`skeleton.cpp:317-325`), so any change to the arrays requires a
rebuilt `SkeletonData`, never an in-place patch of a live `Skeleton`.

### 2.4 The project half is four parallel upsert vectors and nothing else

`ProjectData` (`project.hpp:535-560`) holds

```cpp
std::vector<IkConstraintEdit>        ik_constraint_edits;        // :548
std::vector<PathConstraintEdit>      path_constraint_edits;      // :549
std::vector<TransformConstraintEdit> transform_constraint_edits; // :550
std::vector<PhysicsConstraintEdit>   physics_constraint_edits;   // :551
```

with per-family `find_*_constraint_edit(name)` accessors (`project.hpp:685-731`,
defined `project.cpp:6738-6819`). Storage is `constraint_edits.{ik,path,
transform,physics}` in `.marrow`, parsed at `project.cpp:3059`, `:3199`,
`:3371`, `:3568` and rebuilt at `:3995`, `:4019`, `:4045`, `:4077`.

### 2.5 The save validator is the trap MAR-175 warned about

`validate_project_for_save()` (`project.cpp:5175`) checks the four constraint
families at `:5662-5777`. Per family it requires: non-empty name; the family's
required references (IK target, path slot, transform source); a non-empty bone
list (IK additionally ≤ 2); every numeric in range; **name unique within the
family**; bone names unique; and for transform, source ∉ targets.

It takes `(const ProjectData&, ProjectSaveError*)` and **no base document**. It
therefore cannot resolve a name against the base skeleton. §8.1 splits
validation along exactly that seam.

### 2.6 Unknown-field preservation stops at the section boundary

`load_project()` stores the whole parsed root
(`project.cpp:7029`: `project.preserved_root = document.root;`), and
`build_project_value()` (`:4507-4686`) starts from it and overwrites the known
keys. Two consequences, both measured:

- Unknown **top-level** keys survive load/save.
- Unknown keys **inside `constraint_edits`** do **not**, because
  `build_constraint_edits_value()` (`:4260-4280`) builds a fresh
  `Value::Object` and `:4667` assigns it over the preserved subtree.

The second is pre-existing behaviour, shared with `timeline_edits` and
`mesh_edits`. MAR-177 must not regress it and does not improve it (§3.2).

### 2.7 `.mbin` is a generic encoding of the same document

`write_skeleton_binary_document()` (`binary.cpp:2113-2162`) writes
`MBIN` (4 bytes) · `varint(2)` · `varint(string_count)` · per string
`varint(len)+bytes` · boolean block · `encode_value(root)` · animation section.

The table is built by `collect_strings()` (`binary.cpp:165-186`), which interns
**every distinct string once** — object keys included — in document-walk order.
`Value::Object` is `std::map<std::string, Value, std::less<>>`
(`runtime/json.hpp:24`), so the walk is alphabetical and fully deterministic;
`StringTableBuilder::indices` is an `unordered_map` but is used only for lookup
and never for ordering (`binary.cpp:148-163`). §11 derives the export signal
from exactly this.

### 2.8 Measured counts, as built after MAR-176

| Thing | Value | How measured |
| --- | --- | --- |
| Registry operations | **62** | rows of `kOperationSpecs` in `agent_dispatch.cpp` |
| Constraint operations, all `edit_*` | 4 | `agent_dispatch.cpp:76-79` |
| Registry-count guards in `shell_smoke_graph.cpp` | 7 | `:149`, `:637`, `:1559`, `:2129`, `:3143`, `:3958`, `:4604` |
| Registry-count guards in `shell_smoke_timeline.cpp` | 1 | `:3698` |
| Hand-edited registry array size | 1 | `agent_dispatch_smoke.cpp:39` |
| MCP parity assertions | 2 | `tools/mcp/test_client.py:51`, `:53` |
| Prose sites stating what the surface **is** | 6 | `AGENTS.md:161`, `docs/root1/editing-gap-analysis.md:23`, `:85`, `:86`, `:192`, `:455`, plus `refector.md:112` |

**MAR-177 changes none of these.** §10 explains why, and what the implementer
must assert instead.

### 2.9 The fixtures, measured

```
assets/fixtures/player_idle.mskl                  root ik/path/transform/physics: NONE
assets/fixtures/player_idle.marrow                constraint_edits: all four families, project-only
assets/fixtures/ik_constraints.mskl               13 base ik, no skin refs
assets/fixtures/path_transform_constraints.mskl   1 base path, 1 base transform, no skin refs
assets/fixtures/physics_constraints.mskl          1 base physics, no skin refs
assets/fixtures/skin_inherit_constraints.mskl     1 base transform `cape_pull`, skins.cape.transform = ["cape_pull"]
assets/fixtures/spine_import_sample.mskl          1 base ik, 1 base path, 1 base transform, no skin refs
```

Two findings that shape the whole test plan:

1. **The default project fixture has no base-backed constraints at all.** All
   four constraints in `player_idle.marrow` are project-only upserts over a
   base with empty root arrays. A test that only uses `player_idle` cannot
   exercise AC2's base-backed half, and cannot exercise skin references.
2. **`skin_inherit_constraints.mskl` is the only fixture in the tree where a
   skin names a constraint.** It is therefore the only fixture that can catch
   the §2.2 failure. It has no `.marrow` project; the smoke must build one
   in-memory with `create_minimal_project()`.

---

## 3. Scope

### 3.1 In scope

1. A new optional, ordered `.marrow` field `constraint_edits.operations`, with
   `rename` and `delete` records for all four families (AC1).
2. `ProjectData::constraint_lifecycle_operations`, its parser, its serializer,
   and its save-time validation.
3. The ownership rule: base-backed → operation record; project-only → direct
   rewrite of the upsert entry (AC2). Shipped as two pure, UI-free
   `ProjectData` primitives so that the rule has exactly one implementation.
4. Deterministic materialization: lifecycle operations applied in array order
   **before** upserts, over both the root constraint arrays and every
   `skins[*].<family>` name array (AC3).
5. Atomic rejection of missing sources, duplicate targets, family mismatches,
   and invalid order, with `serialize_project()` byte-identical after a
   rejection (AC3).
6. Compatibility: absent field, old projects, and unknown fields behave exactly
   as before; `.mskl` v1, `.mbin` v2, C ABI v1 untouched (AC4).
7. Project smoke coverage of mixed base/project constraints, ordered chains,
   tombstones, collision rollback, save/reload, `.marrow` merge, and JSON/MBIN
   export (AC5).

### 3.2 Out of scope, and where each piece goes

| Not here | Where | Why |
| --- | --- | --- |
| Rename/delete buttons, confirmation dialogs, reference previews | **MAR-178** AC1 | The story's own AC5 names only "Project smoke"; MAR-178's AC6 names "Project, shell, agent, and MCP smokes". The split is the story's, not mine. |
| The undoable `EditorSession` domain command | **MAR-178** AC2/AC4 | "One UI-free lifecycle command atomically cascades … SelectionSet active/selected identities." |
| `SelectionSet` remap/prune on rename/delete | **MAR-178** AC2 | Selection is transient and never persisted (§6, referrer 3). |
| New registry / MCP operations | **MAR-178** AC5 | Registry stays at exactly 62 (§10). |
| IK `softness`/`compress`/`stretch` and physics field parity | **MAR-179** | Parameter widgets, not lifecycle. |
| Creating constraints | **already shipped** | §0.1. |
| Preserving unknown keys *inside* `constraint_edits` | Neither | Pre-existing (§2.6); fixing it would change serialization for existing projects, which AC4 forbids. |
| Reordering constraints within a family | Neither | Not "lifecycle"; the runtime's evaluation-order contract (§7.3) is preserved, not exposed. |

MAR-177 is the model layer; MAR-178 is the UI and agent layer. Stated plainly,
as the brief asked.

### 3.3 Compatibility boundaries

| Boundary | Change |
| --- | --- |
| `.mskl` v1 | none |
| `.mbin` v2 | none |
| C ABI v1 | none |
| `editor-settings.json` v1 | none |
| `.marrow` | **one** strictly additive optional member, `constraint_edits.operations`, omitted when empty |

The `.marrow` addition follows MAR-165's `.marrow.snap` discipline exactly:
absent by default, absent when logically empty, and the `erase` branch is what
keeps every existing project byte-identical (§8.3).

---

## 4. The representation

### 4.1 Why an ordered array and not per-family maps

AC1 says "**ordered** rename and delete records" and AC3 says materialization
"applies lifecycle operations deterministically" and rejects "invalid order".
Order is load-bearing, and a map cannot carry it. Three cases prove it:

- **Chain**: `rename A→B`, then `rename B→C`. As a map keyed by source, the
  second entry's source is a name that only exists after the first ran.
- **Swap**: `rename A→tmp`, `rename B→A`, `rename tmp→B`. Every intermediate
  state is legal; every reordering is not.
- **Reuse**: `delete A`, then `rename B→A`. Legal in this order, a duplicate
  target in the other.

So: one array, applied front to back.

### 4.2 The JSON shape

```json
"constraint_edits": {
  "ik": [ ... ],
  "operations": [
    { "op": "rename", "family": "transform", "from": "cape_pull", "to": "cape_drag" },
    { "op": "delete", "family": "ik",        "name": "arm_zero_parent" }
  ],
  "path": [ ... ],
  "physics": [ ... ],
  "transform": [ ... ]
}
```

`Value::Object` is a `std::map`, so `operations` serializes between `ik` and
`path` — alphabetically, deterministically, and with no effect on the existing
four keys' bytes.

Field rules, enforced on load:

| Field | Rule |
| --- | --- |
| `op` | required string, exactly `"rename"` or `"delete"` |
| `family` | required string, exactly one of `"ik"`, `"path"`, `"transform"`, `"physics"` |
| `from` | required non-empty string when `op == "rename"`; rejected otherwise |
| `to` | required non-empty string when `op == "rename"`; rejected otherwise; must differ from `from` |
| `name` | required non-empty string when `op == "delete"`; rejected otherwise |

Rejecting the *wrong* key for an `op` (a `name` on a rename, a `to` on a
delete) rather than ignoring it is deliberate: a silently ignored key is how a
future writer's typo becomes a silent no-op. Unknown *additional* keys inside an
operation record are ignored on load and dropped on save, matching every other
record type in `constraint_edits` (§2.6).

### 4.3 The typed model

In `include/marrow/editor/project.hpp`, reusing the enum that already exists:

```cpp
#include "marrow/editor/selection.hpp"   // marrow::editor::ConstraintKind

/** @brief Which lifecycle transition an ordered constraint operation records. */
enum class ConstraintLifecycleKind {
    Rename,
    Delete,
};

/**
 * @brief One ordered rename or delete applied to a base-backed constraint.
 *
 * Records are applied front to back before any `*_constraint_edits` upsert is
 * merged. `new_name` is meaningful only for `Rename`.
 */
struct ConstraintLifecycleOperation {
    ConstraintLifecycleKind kind{ConstraintLifecycleKind::Rename};
    ConstraintKind family{ConstraintKind::Ik};
    std::string name;      ///< `from` for a rename, the target for a delete.
    std::string new_name;  ///< `to` for a rename; empty for a delete.
};
```

placed immediately after `PhysicsConstraintEdit` (`project.hpp:307-315`), with
the vector member

```cpp
std::vector<ConstraintLifecycleOperation> constraint_lifecycle_operations;
```

immediately after `physics_constraint_edits` (`project.hpp:551`).

`marrow::editor::ConstraintKind` (`selection.hpp:17-23`) already has exactly the
four values, and `selection.hpp` includes nothing from `marrow::editor`
(`selection.hpp:1-12`), so `project.hpp` can include it with no cycle. A second
parallel four-value enum is the kind of duplication that drifts.

### 4.4 Identity

**A constraint's identity is `(family, name)`.** Not an index, and not a name
alone.

- **Family is part of identity** because the runtime enforces uniqueness only
  within a family (§2.2), so an IK constraint and a physics constraint may both
  be called `arm`. Every operation record therefore carries `family`, and
  applying `{op:"delete", family:"ik", name:"arm"}` must not touch the physics
  `arm`. That is AC3's "family mismatches".
- **Index is not identity** because a delete renumbers everything after it.
  No record, no reference, and no persisted structure anywhere in the tree
  stores a constraint index — §6 checked all of them. Indices exist only inside
  a materialized `SkeletonData` and inside a live `Skeleton`'s bitsets
  (`skeleton.cpp:317-325`), both rebuilt wholesale from the document.

A **name collision** is a hard, atomic rejection at both layers (§8), never a
silent overwrite and never an auto-suffix. `unique_constraint_name()` already
owns auto-suffixing for the create path (§0.1); a rename is a user's explicit
choice of a specific name, and quietly giving them a different one is worse than
refusing.

---

## 5. Materialization

### 5.1 The two phases

`build_runtime_document()` gains one phase, inserted immediately before the
existing four `merge_named_object_array_member` calls
(`project.cpp:5150-5167`):

```
Phase A — lifecycle   apply constraint_lifecycle_operations, in array order,
                      to document.root's constraint arrays and to every
                      skins[*].<family> name array
Phase B — upserts     the four existing merge_named_object_array_member calls,
                      byte-for-byte unchanged
```

**Phase A strictly precedes Phase B.** This is AC3's "before later upserts", and
it is what makes the ownership rule in §5.3 coherent: a project-only rename that
rewrote its upsert entry directly lands *after* every base rename has already
run, so the two can never race for a name.

### 5.2 Phase A, per operation

Let `F` be the JSON key for the operation's family (`"ik"`, `"path"`,
`"transform"`, `"physics"`).

**`rename` from `X` to `Y`:**

1. Find the element of `root[F]` whose `name` is `X`. Absent → the operation is
   unresolvable (§8.2).
2. If any element of `root[F]` other than that one already has `name == Y` →
   unresolvable (duplicate target).
3. Set that element's `name` to `Y`.
4. For every skin in `root.skins`, for every element of `skin[F]` equal to `X`,
   set it to `Y`.

**`delete` `X`:**

1. Find the element of `root[F]` whose `name` is `X`. Absent → unresolvable.
2. Erase it, preserving the relative order of the survivors.
3. For every skin in `root.skins`, erase every element of `skin[F]` equal to
   `X`, preserving order.
4. If `root[F]` is now empty, **erase the key** rather than leaving `[]`.
   `skeleton_parse.cpp:2985-2991` rejects an empty `ik` array outright ("ik
   constraints must not be empty when provided"), and the other three families
   parse identically. Deleting the last constraint of a family must therefore
   remove the key, not empty it. Same for an emptied `skin[F]`.

Step 4 is not cosmetic. It is the difference between "the last IK constraint was
deleted" and "the project can no longer be opened".

### 5.3 The ownership rule (AC2)

For a constraint identified by `(family, name)`, at the moment a lifecycle
change is applied to `ProjectData`:

| Present in base `root[F]`? | Present in `*_constraint_edits`? | Rename | Delete |
| --- | --- | --- | --- |
| yes | no | append `rename` record | append `delete` tombstone |
| yes | yes (an upsert shadowing the base) | append `rename` record **and** rewrite the upsert's `name` | append `delete` tombstone **and** erase the upsert entry |
| no | yes (project-only) | rewrite the upsert's `name`; **no record** | erase the upsert entry; **no record** |
| no | no | reject: not found | reject: not found |

The middle row is the one that is easy to get wrong. An upsert whose name also
exists in the base is *shadowing* it — `merge_named_object_array_member`
replaces the base element in place (`project.cpp:4731-4735`). Erasing only the
upsert therefore **resurrects the base constraint**, which reads to the user as
"delete did nothing". A tombstone is required in addition. Symmetrically, an
upsert-only rename with a live base entry would leave the old base constraint
standing beside the renamed one — two constraints where there was one.

### 5.4 Ordering of the result (AC3 determinism)

- **Rename never moves an element.** Position, and therefore evaluation order
  (§2.3), is preserved.
- **Delete never reorders survivors.** They shift down by one; their relative
  order — the only thing evaluation depends on — is unchanged.
- **A project-only upsert of a name not in the base appends at the end** of its
  family array (`project.cpp:4735`), so it evaluates last within its family.
  This is pre-existing behaviour and MAR-177 does not change it.
- Operations are applied front to back; families do not interleave because each
  operation names its own family.

There is no hash-ordered container and no sort anywhere on the path. Phase A is
a sequence of linear scans over `std::vector<Value>` and `std::map`.

---

## 6. Referential integrity: every referrer, enumerated

MAR-172 shipped an `ok: true` that destroyed an adjacent key. The equivalent
here is an `ok: true` that leaves the project unopenable. Every referrer of a
constraint name in the tree, found by grepping every file that mentions
`constraint` outside `external/`:

| # | Referrer | Where | Persisted? | Rename | Delete |
| --- | --- | --- | --- | --- | --- |
| 1 | `skins[*].{ik,path,transform,physics}` — arrays of **names** | `skeleton_parse.cpp:5169-5207`; resolution and hard error at `:5115-5123` | **yes**, in `.mskl` | **rewrite** | **prune** (and drop the key if emptied) |
| 2 | `ProjectData::*_constraint_edits[].name` | `project.hpp:548-551` | yes, in `.marrow` | rewrite, per §5.3 | erase, per §5.3 |
| 3 | `ConstraintSelection{kind, constraint_name}` | `selection.hpp:69-78`, `shell_selection.cpp:107-110` | **no** — no `selection` key exists in `build_project_value` | MAR-178 | MAR-178 |
| 4 | `ShellState` derived counts | `shell_state.hpp:318-320` | no | rebuilt | rebuilt |
| 5 | `viewport.debug_overlay.{ik,path,physics}_constraints` | `project.hpp:70-72`, `project.cpp:1089-1105` | yes | **no action** — per-*family* booleans, never per-name | no action |
| 6 | `Skeleton::active_*_constraints_`, `physics_constraint_states_` | `skeleton.cpp:317-325` | no | rebuilt from `SkeletonData` | rebuilt |
| 7 | Animation timelines | — | — | **none exist** | **none exist** |
| 8 | Constraint→constraint references | — | — | **none exist** | **none exist** |

Two negative findings are worth stating explicitly, because they remove work
that a reasonable designer would otherwise plan for:

- **No animation timeline names a constraint.** `parse_animations`
  (`skeleton_parse.cpp:5212+`) has no IK/transform/path constraint-mix timeline
  family. The only occurrences of the strings `"ik"`, `"path"`, `"transform"`,
  `"physics"` in `skeleton_parse.cpp` are the four root arrays (`:2972`,
  `:3194`, `:3456`, `:3747`), the four skin scopes (`:5172`-`:5202`), and
  `is_skin_scope_key` (`:699-700`). Deleting a constraint cannot orphan a
  keyframe.
- **Constraints never reference other constraints.** A transform constraint
  names bones (`source`, `bones`), a path constraint names a slot and bones, IK
  names bones and a target bone, physics names bones. There is no ordering
  graph, no parent constraint, and no dependency to repair.

So the whole of referential integrity is referrer 1 — and it is a hard failure,
not a soft one.

---

## 7. Semantics under composition

### 7.1 Interaction with the upsert layer

Phase A runs on the base; Phase B merges upserts on top. Therefore:

- A `rename X→Y` record plus a project upsert named `X` that was *not* rewritten
  would re-introduce `X` alongside `Y`. §5.3's middle row forbids producing that
  state, and §8.1's save validator rejects it if it is hand-authored.
- A `delete X` tombstone plus a project upsert named `X` that was not erased
  would resurrect `X` after the tombstone removed it. Same rule, same rejection.
- A `rename X→Y` record plus a project upsert named `Y` is a **duplicate
  target** after Phase B, even though Phase A alone sees no collision. This case
  is invisible to a naive Phase-A-only check and must be caught. §8.1 catches it
  without the base document.

### 7.2 Idempotence and replay

Operations are **not** idempotent and are not meant to be: `rename A→B` applied
twice fails the second time because `A` no longer exists. This is correct — the
array is a *log applied once per materialization*, not a desired-state
declaration. Materializing the same project twice from the same base yields
byte-identical documents, which is the determinism AC3 asks for.

### 7.3 Evaluation-order contract

Stated for the record, because MAR-178 and MAR-179 will both lean on it: within
a family, evaluation order is array order (§2.3); rename preserves it; delete
preserves the relative order of survivors; a newly upserted constraint lands
last. MAR-177 exposes no way to reorder.

---

## 8. Validation, in two layers

### 8.1 Save-time, without the base document

`validate_project_for_save()` has no skeleton (§2.5). It can still catch
everything that is intrinsically broken, by **symbolic replay**: walk the
operations front to back, per family, maintaining two sets — `consumed` (names
that have been renamed away or deleted) and `introduced` (names a rename has
created).

Appended to `validate_project_for_save()` after the physics block
(`project.cpp:5777`):

1. `kind` is `Rename` or `Delete`; `family` is one of the four values. (Enum
   types make this structural, but a defaulted enum still needs the name checks
   below.)
2. `name` non-empty. For `Rename`, `new_name` non-empty and `new_name != name`.
   For `Delete`, `new_name` empty.
3. **Invalid order, source side.** `name` must not already be in `consumed` for
   that family. Catches `rename A→B; delete A` and `delete A; rename A→C`.
4. **Invalid order, target side.** For a rename, `new_name` must not already be
   in `introduced` for that family and must not be a name that no earlier
   operation consumed but that a later one still needs — expressed simply:
   `new_name ∈ introduced` → reject. Catches `rename A→C; rename B→C`.
5. `name` is removed from `introduced` and added to `consumed`;
   for a rename, `new_name` is added to `introduced` and removed from
   `consumed`.
6. **Cross-check against the upserts** (§7.1). For each family, after the replay:
   every `introduced` name must not collide with a `*_constraint_edits` entry
   whose name is not itself `introduced` by that same rename; and no
   `*_constraint_edits` entry may be named by a `consumed` name.

Steps 3–5 are the whole of "invalid order" that is knowable without the base.
Step 6 is the whole of §7.1.

**This is the MAR-175 gate.** No state that any MAR-177 primitive or any
MAR-178 command can produce may pass its own preflight and then fail
`validate_project_for_save`. §12 makes that an assertion, not an aspiration:
every mutating test path calls `save_project()` and checks the result, not just
the primitive's return code.

### 8.2 Materialization-time, with the base document

A new free function, mirroring `validate_animation_edit_sequence()`, which is
the established shape for "validate the overlay against the base":

```cpp
std::optional<runtime::json::LoadError> validate_constraint_lifecycle_operations(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document);
```

It replays Phase A over a scratch name-set derived from the base document's four
root arrays and rejects:

- **missing source** — `name` not present when the operation runs;
- **duplicate target** — `new_name` already live in that family when the rename
  runs;
- **family mismatch** — `name` exists, but in a different family than the record
  claims. Reported distinctly from "missing source", because the two have very
  different fixes and AC3 lists them separately;
- **invalid order** — any of the above arising from replay position rather than
  from the record in isolation.

It is called in **both** entry points that build a runtime, immediately after
the existing `validate_animation_edit_sequence` call:

- `build_project_runtime()` — `project.cpp:7075-7083`
- `export_runtime_assets()` — `project.cpp:7145-7152`

so a rejected project produces a `ProjectRuntimeResult`/`ProjectExportResult`
error and **writes nothing**. `export_runtime_assets()` writes its first byte at
`project.cpp:7160`, well after the validation point, so a rejection cannot leave
a truncated `.mskl` or a stale `.mbin`.

### 8.3 Phase A is defensive, the validator is authoritative

`build_project_runtime_document()` is public (`project.hpp:932-937`) and can be
called without the validator. Phase A therefore treats an unresolvable operation
as a **no-op and continues**, exactly as the mesh-weight overlay loop already
skips a missing attachment (`project.cpp:5076-5083`). It never throws, never
partially applies a rename, and never leaves a dangling skin reference: each
operation either applies completely or not at all. Rejection is the validator's
job, and both real callers run it.

---

## 9. The two project primitives

MAR-177 ships the ownership rule (§5.3) as code, not as prose, so that MAR-178's
command and the project smoke share one implementation.

### 9.1 Signatures

In `include/marrow/editor/project.hpp`:

```cpp
/** @brief Outcome of a constraint lifecycle primitive. */
struct ConstraintLifecycleResult {
    bool ok{false};
    std::string message;          ///< Empty on success.
    bool used_operation{false};   ///< True when a record was appended (base-backed).
    bool changed_upsert{false};   ///< True when a `*_constraint_edits` entry was rewritten or erased.
};

ConstraintLifecycleResult rename_constraint(
    ProjectData* project,
    const runtime::json::Document& base_skeleton_document,
    ConstraintKind family,
    std::string_view from,
    std::string_view to);

ConstraintLifecycleResult delete_constraint(
    ProjectData* project,
    const runtime::json::Document& base_skeleton_document,
    ConstraintKind family,
    std::string_view name);
```

No `EditorSession`, no `ShellState`, no ImGui. MAR-178 wraps these in a
transaction; the smoke calls them directly.

### 9.2 Preflight-then-mutate

Both follow the house discipline exactly:

1. Copy: `ProjectData candidate = *project;`
2. Resolve `(family, from|name)` against the **materialized** name set — the
   base root array for `family`, with the project's lifecycle operations already
   replayed, then its upserts merged. This is the same name set the user sees.
3. Reject on: not found; `to` empty; `to == from`; `to` already live in that
   family; any of §8.2's conditions once the new record is appended.
4. Apply §5.3's row to `candidate`.
5. `validate_project_for_save(candidate, &error)` — if it fails, reject. This
   closes the MAR-175 hole structurally rather than by inspection.
6. `*project = std::move(candidate);`

On any rejection the caller's `ProjectData` is untouched and
`serialize_project()` is byte-identical. §12 asserts that byte-identity
directly, not by inspection of the return code.

### 9.3 Defaults, and why this section is short

MAR-177 creates no constraint, so it specifies no defaults (§0.1). The existing
GUI defaults are nonetheless a dependency of the smoke — every scenario that
adds a constraint before renaming it goes through
`make_default_*_constraint_edit`. Their validity under
`validate_project_for_save` is therefore **asserted** in Task 0 rather than
assumed: the plan's Task 0 constructs one default of each family against
`player_idle` and calls `save_project()`.

---

## 10. Why the registry stays at exactly 62

MAR-177 adds no entry to `kOperationSpecs`. The story's AC5 names one test
surface, "Project smoke"; its AC1–AC4 are about `.marrow` load/save,
materialization, and compatibility. MAR-178's AC5 is the one that says "C++
registry and Python MCP operations provide matching metadata, dry-run,
affected-reference summaries, mutation, undo, and export-preview behavior."

So the eleven-site count sweep is a **no-change assertion**, not an edit. The
implementer must still run it, because the value it protects is the value
MAR-178 will change:

```bash
# must print 62, twice
awk '/kOperationSpecs\[\]/{f=1} f&&/^    \{"/{c++} f&&/^};/{print c; exit}' src/editor/agent_dispatch.cpp
grep -c 'exact 62-operation registry' src/editor/shell_smoke_graph.cpp src/editor/shell_smoke_timeline.cpp | paste -sd+ | bc
```

Never touch, when grepping for `62`: none of the forbidden literals listed in
the brief contain `62`, but the same rule applies — `IM_COL32(56, 61, 69, 255)`,
`(51, 56, 64)`, `"x": 56.0`, `56,995,840`, `PhysicsBoneState … 56 bytes/bone`,
`IM_COL32(208,134,57,230)`, `rgb(54,57,64)` are colours, sizes, and coordinates,
not counts.

---

## 11. Export signal

### 11.1 Choosing a signal that discriminates

MAR-175 asserted `25 B/influence` against an `18 + K` model; MAR-176 re-derived
`K` from the decoded string table when the plan's baseline turned out wrong. The
standard is: **export the mutated project, assert a specific serialized value,
derived from the encoding rather than observed.**

The fixture is `assets/fixtures/skin_inherit_constraints.mskl` — the only one
where a skin names a constraint (§2.9), and therefore the only one where the
§2.2 failure is reachable.

**Setup.** Build a project over it with `create_minimal_project()`. Export
baseline `.mskl` + `.mbin`. Then rename the base transform constraint

```
cape_pull          →  cape_pull_renamed
9 UTF-8 bytes         17 UTF-8 bytes            Δlen = +8
```

and export again to fresh paths.

### 11.2 The `.mskl` delta: +16 bytes, derived from occurrence count

The exported `.mskl` is `serialize_pretty(runtime_document.root)`
(`project.cpp:7159-7163`). The string `cape_pull` occurs in the document exactly
**twice**:

1. `root.transform[0].name`
2. `root.skins.cape.transform[0]`

and nowhere else — it is not a bone (`root`, `controller`, `child`,
`constrained`, `cape_target`), not a slot (`dummy`), not a skin (`default`,
`cape`), not an animation (`toggle_inherit`), and not an object key. Pretty
printing renders each as `"cape_pull"` with identical surrounding punctuation
before and after, so:

```
Δ(.mskl) = 2 × (17 − 9) = +16 bytes, exactly
```

**This is the discriminating half.** An implementation that renames the root
array element but forgets the skin reference produces `Δ = +8`. It would also
fail `load_skeleton_data` at `skeleton_parse.cpp:5115-5123` and never write a
byte — so the test asserts *both* that the export succeeded and that the delta
is 16, and reports which one failed.

### 11.3 The `.mbin` delta: +8 bytes, derived from interning

`collect_strings` (`binary.cpp:165-186`) interns each **distinct** string once.
Walking a `std::map`-ordered document, root keys are visited alphabetically —
`animations, bones, marrow, skeleton, skins, slots, transform, version` — so
`cape_pull` is first interned from `skins.cape.transform[0]` and the root array
element reuses that index. After the rename the same slot holds
`cape_pull_renamed`, because:

- the new name occurs nowhere else in the document, so it cannot be interned
  earlier;
- the old name occurs nowhere else, so its slot is not retained by another use.

Therefore:

- `strings.values.size()` is **unchanged** → the header's count varint is
  unchanged;
- every string-table index is unchanged → every index varint inside
  `encode_value` is byte-identical;
- the boolean block (`collect_booleans`, `binary.cpp:188+`) is unchanged;
- the animation section is unchanged.

The only delta is that one table entry's `varint(len) + bytes`. Both 9 and 17
are below 128, so both length varints are one byte:

```
Δ(.mbin) = (1 + 17) − (1 + 9) = +8 bytes, exactly
```

### 11.4 Why the pair is the signal, and not either half

`Δ(.mskl) = 16` and `Δ(.mbin) = 8` measure *different properties of the same
rename*: JSON counts **occurrences**, MBIN counts **distinct strings**. The pair
therefore pins both the reference rewrite and the encoding model, and the ratio
`16 : 8 = 2 : 1` is exactly the fixture's occurrence count. A future change to
either encoding breaks one number and not the other, and the assertion message
says which.

Following MAR-175's practice (`editor_project_smoke.cpp:8206-8228`), each
assertion carries the derivation and distinguishes *"the encoding moved"* from
*"the fixture's string table reshuffled"* — the latter being possible if a
future fixture edit introduces `cape_pull` elsewhere, in which case the correct
response is to re-derive the occurrence count, not to edit the constant.

### 11.5 The delete signal

For the delete case the byte model is messier (the table loses an entry and
every later index shifts), so the assertion is on **decoded** structure plus one
directly-read header field:

1. `.mskl` occurrences of `cape_pull` go `2 → 0`.
2. Reloading the exported `.mbin` via `load_skeleton_document()` yields
   `transform_constraints().size() == 0` and, for skin `cape`,
   `transform_constraint_indices.empty()`.
3. The `.mbin` string-table entry count, read directly from the header
   (`MBIN` 4 bytes · `varint(version)` · `varint(count)`), decreases by
   **exactly 1**.
4. The exported `.mskl` has **no** `transform` key at root and **no**
   `transform` key inside `skins.cape` — §5.2 step 4, without which the reload
   in (2) would fail on "transform constraints must not be empty when
   provided".

Point 4 is the one that would otherwise ship broken.

---

## 12. Validation strategy

One binary carries this story: `marrow_project_smoke`. That matches AC5, which
names only the project smoke.

### 12.1 `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`

**A — schema round trip (AC1, AC4).**

- A project with an empty operations vector serializes **byte-identically** to
  today's output; `constraint_edits` has no `operations` key.
- A project whose *only* constraint content is one tombstone (no upserts at all)
  still emits `constraint_edits` with `operations`, and reloading recovers it.
  *This is the §8.3-adjacent trap:* `project.cpp:4663-4666` gates the whole
  `constraint_edits` key on the four vectors being non-empty, and would
  otherwise drop the tombstone silently — the MAR-172 failure shape exactly.
- Every malformed record from §4.2 is rejected on load with a located error:
  bad `op`, bad `family`, missing/empty `from`/`to`/`name`, `to == from`, a
  `name` on a rename, a `to` on a delete.
- An unknown top-level `.marrow` key survives load/save unchanged (AC4).
- A `player_idle.marrow` loaded and re-saved with no edits is byte-identical to
  the file on disk (AC4's "old projects retain existing behavior").

**B — project-only lifecycle (AC2).** `player_idle` has four project-only
constraints over an empty base (§2.9), so it is the natural fixture for the
third row of §5.3's table.

- Rename `editor_arm_reach` → `arm_reach_v2`: the IK upsert's name changes,
  `constraint_lifecycle_operations` stays **empty**, `used_operation == false`.
- Delete `editor_ribbon_secondary`: the physics upsert is erased,
  `constraint_lifecycle_operations` stays empty.
- **Survival**: after each, the *other three* constraints are asserted present
  and field-for-field unchanged, and `timeline_edits`, `snap`, and the editor
  metadata are asserted unchanged. This is MAR-172's lesson — read back the
  neighbour, not the return code.
- `save_project()` succeeds and the reloaded project matches.

**C — base-backed lifecycle and skin references (AC2, AC3).** Fixture
`skin_inherit_constraints.mskl`, project built with
`create_minimal_project()`.

- Rename `cape_pull` → `cape_drag`: one `rename` record appended,
  `used_operation == true`, no upsert created. Materialize: root
  `transform[0].name == "cape_drag"`, `skins.cape.transform == ["cape_drag"]`,
  and `build_project_runtime()` **succeeds** — the parse would reject a stale
  reference.
- Delete `cape_pull`: one `delete` record; materialized document has **no**
  root `transform` key and **no** `skins.cape.transform` key (§5.2 step 4); the
  runtime loads; `skins.cape.bone_indices` still contains `cape_target`
  (survival of the adjacent skin scope).
- The shadowing row: upsert `cape_pull` via the project vector *and* delete it.
  Assert both a tombstone **and** an erased upsert, and that the materialized
  document contains no `cape_pull` — i.e. the base did not resurrect.

**D — ordered chains and collisions (AC3).**

- Chain: `rename A→B`, `rename B→C`, `delete C` over `ik_constraints.mskl`
  (13 base IK constraints). Materialized: `A`, `B`, `C` all absent; the other
  **12** present, **in their original relative order** (§5.4). Assert the order
  as a name sequence, not just a count.
- Swap: `rename A→tmp`, `rename B→A`, `rename tmp→B`. Both constraints keep
  their original array positions and exchange names.
- Reject `rename A→C` then `rename B→C` (duplicate target).
- Reject `delete A` then `rename A→B` (missing source, from order).
- Reject `{family: "physics", name: "arm_positive"}` against `ik_constraints`
  (family mismatch) — and assert the message names the mismatch, not "not
  found".
- Reject a rename whose target collides with a *project upsert* name (§7.1) —
  invisible to a Phase-A-only check.
- **Rollback byte-identity**: capture `serialize_project()` before each
  rejection and assert the string is identical after. Not "the vector size is
  unchanged" — the serialized bytes.
- After every *accepted* chain, `save_project()` to a temp path and assert it
  succeeds. **This is the MAR-175 gate**; a return code is not evidence.

**E — merge and reload (AC5).** Load a `.marrow` that already carries unknown
top-level keys and a populated `operations` array; mutate; save; reload; assert
the operations round-trip in order and the unknown keys survive.

**F — export (AC5, §11).** The `.mskl` +16 / `.mbin` +8 rename pair, and the
delete signal's four assertions.

### 12.2 Regression and compatibility gates

- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
  unchanged — MAR-177 touches no shell file.
- `./build/marrow_agent_dispatch_smoke` unchanged at 62 operations.
- `./build/marrow_fixture_smoke`, `./build/marrow_unit_tests`,
  `ctest --test-dir build --output-on-failure` unchanged.
- **Zero-byte diff** on `src/runtime/**`, `include/marrow/runtime/**`,
  `include/marrow/marrow_c.h`, `src/c_api/**`, `src/editor/preferences.cpp`,
  `include/marrow/editor/preferences.hpp`, and every `src/editor/shell_*` file.
- `docs/root1/format-spec.md` **does** change — one new subsection under
  `constraint_edits` — because `.marrow` gains a field. This is the one
  documentation site MAR-177 must touch.

---

## 13. Non-goals, restated for the reviewer

1. No create operation — it exists (§0.1).
2. No GUI. No agent operation. No MCP tool. Registry stays 62 (§10).
3. No `SelectionSet` remap (MAR-178).
4. No constraint reordering.
5. No fix for unknown keys inside `constraint_edits` (§2.6) — it would change
   serialization for existing projects, which AC4 forbids.
6. No change to `merge_named_object_array_member`'s upsert semantics; Phase B is
   byte-for-byte the code that is there today.
7. No new tunable numeric constant. MAR-177 introduces none.

---

## 14. Decisions taken under ambiguity

Marked because they could not be verified without building, which this document
was not permitted to do. Each is a Task 0 gate in the plan.

| # | Decision / claim | Basis | Gate |
| --- | --- | --- | --- |
| 1 | Registry is **62** | counted `kOperationSpecs` rows with `awk`; consistent with `agent_dispatch_smoke.cpp:39` and `test_client.py:51` | Task 0 re-measures |
| 2 | 7 guards in `shell_smoke_graph.cpp`, 1 in `shell_smoke_timeline.cpp` | grep, listed in §2.8 | Task 0 re-measures |
| 3 | `.mskl` rename delta is **+16** | derived: 2 occurrences × 8 bytes (§11.2) | Task 6 measures; if it differs, re-derive the occurrence count from the *exported* text, do not edit the constant |
| 4 | `.mbin` rename delta is **+8** | derived from `collect_strings` interning + 1-byte length varints (§11.3) | Task 6 measures; a mismatch means the table reshuffled or the encoding moved, and the message must say which |
| 5 | `cape_pull` occurs exactly twice in `skin_inherit_constraints.mskl` | read the fixture; not a bone/slot/skin/animation/key name | Task 0 re-counts against the **exported** document, which is what is measured |
| 6 | `project.hpp` may include `selection.hpp` with no cycle | `selection.hpp:1-12` includes only `<cstddef> <functional> <optional> <string> <variant> <vector>` and forward-declares `runtime::SkeletonData` | Task 1 compiles it |
| 7 | Empty family arrays must have their key erased, not left `[]` | `skeleton_parse.cpp:2985-2991` rejects an empty `ik` array; the other three parse identically | Task 0 confirms all four reject-empty branches exist |
| 8 | `create_minimal_project()` over `skin_inherit_constraints.mskl` exports without an atlas | `export_runtime_assets` iterates `resolved_atlas_paths()`, which is empty for a project with no atlases (`project.cpp:7167-7188`) | Task 5 exercises it; if it needs a `.matl`, use the fixture's own or add none and assert the empty-atlas path |
| 9 | The existing four GUI defaults pass `validate_project_for_save` | read the builders and the validator; not executed | Task 0 executes it |
| 10 | No animation timeline references a constraint | exhaustive grep of `skeleton_parse.cpp` for the four family keys | Task 0 re-greps |

Numbers 3 and 4 are the ones most likely to be wrong, and they are wrong in a
*useful* way: the assertions carry their derivations, so a mismatch identifies
which premise failed rather than merely failing.
