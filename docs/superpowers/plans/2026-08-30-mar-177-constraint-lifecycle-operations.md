# MAR-177 Constraint Lifecycle Project Operations — Implementation Plan

- Design spec: `docs/superpowers/specs/2026-08-30-mar-177-constraint-lifecycle-operations-design.md`
- Story: `MAR-177`, `dependsOn: ["MAR-176"]`
- Branch: `feat/mar-168`

Read the spec first. This plan does not restate its reasoning; it sequences the
work and pins the verification.

---

## Global Constraints

1. **Preflight-then-mutate.** Copy `ProjectData`, resolve and validate
   everything, then a single `*project = std::move(candidate)`. A rejection must
   leave `serialize_project()` **byte-identical** — asserted on the string, not
   inferred from a return code.
2. **`validate_project_for_save` is the gate.** No primitive in this story may
   return success and leave a project that `save_project()` refuses. Step 5 of
   each primitive calls the validator on the candidate. Every accepted test path
   calls `save_project()` and checks the result.
3. **Delete survival.** Every delete test asserts what **remains**: the other
   constraints, in order; the neighbouring skin scopes; the unrelated project
   sections. MAR-172 shipped an `ok: true` that destroyed an adjacent key and
   was caught only by reading the neighbour.
4. **Registry stays at exactly 62.** MAR-177 adds no operation. The count sweep
   is a *no-change* assertion (Task 0, Task 8).
5. **Compatibility.** Zero-byte diff on `src/runtime/**`,
   `include/marrow/runtime/**`, `include/marrow/marrow_c.h`, `src/c_api/**`,
   `src/editor/preferences.cpp`, `include/marrow/editor/preferences.hpp`, and
   every `src/editor/shell_*` file. `.marrow` gains exactly one optional member.
6. **TDD.** Every task writes the failing assertion first and **records the
   observed failure text**. A test that passes before the implementation is a
   broken test — MAR-176 shipped one such inversion. Where a task says
   "INVERTED GATE", the RED step must be demonstrated by a temporary edit and
   then reverted.
7. **Do not run `marrow_editor_shell` without `--auto-close`**, and never
   create `~/Library/Application Support/Marrow`.

---

## File and Responsibility Map

| File | Change |
| --- | --- |
| `include/marrow/editor/project.hpp` | `+#include "marrow/editor/selection.hpp"`; `ConstraintLifecycleKind`; `ConstraintLifecycleOperation`; `ProjectData::constraint_lifecycle_operations`; `ConstraintLifecycleResult`; `rename_constraint`; `delete_constraint`; `validate_constraint_lifecycle_operations` declarations |
| `src/editor/project.cpp` | `parse_constraint_lifecycle_operations`; `build_constraint_lifecycle_operations_value`; the `build_constraint_edits_value` signature and its two call sites; the emit gate at `:4663`; Phase A in `build_runtime_document`; the save-validator block; the validator free function; the two primitives; two `load_project` wiring lines; two validator call sites |
| `src/samples/editor_project_smoke.cpp` | Scenarios A–F |
| `docs/root1/format-spec.md` | `constraint_edits` subsection gains `operations` |
| `AGENTS.md`, `docs/root1/discription.md`, `docs/root1/editing-gap-analysis.md` | MAR-177 prose |

**Not touched:** every `src/editor/shell_*`, `agent_*`, `authoring.*`,
`src/runtime/**`, `tools/mcp/**`.

---

## Tasks

### Task 0 — Re-read as-built MAR-176 and MEASURE (mandatory, no code)

Nothing in this task edits a file. Every number below is a claim in the spec
that must be confirmed before it is depended on.

```bash
cd /Users/kwon/Workspace/C/Maroow

# 1. Registry count. Spec §2.8 claims 62.
awk '/kOperationSpecs\[\]/{f=1} f&&/^    \{"/{c++} f&&/^};/{print "registry:",c; exit}' \
  src/editor/agent_dispatch.cpp
grep -c 'std::array<OperationExpectation, 62>' src/samples/agent_dispatch_smoke.cpp   # expect 1
grep -c '== 62' tools/mcp/test_client.py                                              # expect 2

# 2. Guard counts. Spec claims 7 + 1.
grep -c 'exact 62-operation registry' src/editor/shell_smoke_graph.cpp                # expect 7
grep -c 'exact 62-operation registry' src/editor/shell_smoke_timeline.cpp             # expect 1

# 3. Prose sites that state what the surface IS (must stay 62 after this story).
grep -n '\b62\b' AGENTS.md docs/root1/*.md | grep -v 'MAR-1[0-7][0-9]은\|checkpoint\|historical'

# 4. The four constraint edit ops, still exactly four and still all edit_*.
grep -n 'constraint' src/editor/agent_dispatch.cpp | grep -c 'edit_'                  # expect 4

# 5. All four families reject an empty root array (spec §5.2 step 4, ambiguity #7).
grep -n 'must not be empty when provided' src/runtime/skeleton_parse.cpp              # expect 4 lines

# 6. No animation timeline names a constraint (ambiguity #10).
grep -n '"ik"\|"path"\|"transform"\|"physics"' src/runtime/skeleton_parse.cpp
#    expect ONLY: 699-700 (is_skin_scope_key), 2972/3194/3456/3747 (root arrays),
#    5172/5182/5192/5202 (skin scopes). Any other hit invalidates spec §6 row 7.

# 7. The emit gate that will silently drop a tombstone-only project.
sed -n '4663,4672p' src/editor/project.cpp

# 8. Fixture occurrence count (ambiguity #5).
python3 - <<'PY'
import json
d = json.load(open('assets/fixtures/skin_inherit_constraints.mskl'))
s = json.dumps(d)
print('cape_pull occurrences in re-serialized doc:', s.count('cape_pull') - s.count('cape_pull_'))
print('root transform:', [c['name'] for c in d['transform']])
print('skins.cape:', d['skins']['cape'])
PY
#    expect 2 occurrences.

# 9. Baseline build, so later deltas mean something.
cmake -S . -B build && cmake --build build
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_agent_dispatch_smoke
git status --porcelain    # must be clean
```

**Then, one written measurement** (ambiguity #9): add a throwaway block to
`editor_project_smoke.cpp` that constructs one default constraint of each family
through the same field values `make_default_*_constraint_edit` produces for
`player_idle` (IK `ik_upper`/`ik_lower`→`ik_target`; path `guide` +
`path_a`/`path_b`/`path_c`; transform `transform_source`→`transform_target`;
physics `ribbon_01`/`ribbon_02`), calls `save_project()` to a temp path, and
prints the result. Confirm all four save. **Delete the block before Task 1.**

**Record in the task log:** the eleven measured numbers, and any that differ
from the spec. If the registry is not 62, stop and report — every later
assertion depends on it.

---

### Task 1 — Typed model, parser, serializer, and the emit gate

Storage only. No materialization, no primitives.

#### TDD step (RED)

`src/samples/editor_project_smoke.cpp`, new scenario **A**:

1. Load `assets/fixtures/player_idle.marrow`. Assert
   `project->constraint_lifecycle_operations.empty()` and that
   `serialize_project()` equals the on-disk file byte-for-byte.
2. Append one `Rename` (`family = Ik`, `name = "editor_arm_reach"`,
   `new_name = "arm_reach_v2"`). Serialize; assert the text contains
   `"operations"` and that the record's five fields appear.
3. Reload the serialized text; assert one operation with all four fields equal.
4. **The tombstone-only case.** Build a `ProjectData` whose four
   `*_constraint_edits` vectors are **all empty** and whose
   `constraint_lifecycle_operations` holds one `Delete`. Serialize; assert the
   output has a `constraint_edits` object containing `operations`. Reload;
   assert the operation survived.
5. Malformed-load rejections, one assertion each, checking the error message
   and JSON path: `op` absent / not a string / `"remove"`; `family` absent /
   `"bone"`; rename with empty `from`; rename with empty `to`; rename with
   `to == from`; rename carrying `name`; delete carrying `to`; delete with
   empty `name`; `operations` not an array; an element not an object.
6. An unknown top-level `.marrow` key round-trips unchanged.

Build and run. **Expect a compile failure** (`constraint_lifecycle_operations`
does not exist). Record it.

#### Implementation step (GREEN)

`include/marrow/editor/project.hpp`:

- `#include "marrow/editor/selection.hpp"` (ambiguity #6 — if this cycles,
  stop and report; do **not** silently declare a duplicate enum).
- `ConstraintLifecycleKind` and `ConstraintLifecycleOperation` after
  `PhysicsConstraintEdit` (`:307-315`), doc-commented in the file's style.
- `std::vector<ConstraintLifecycleOperation> constraint_lifecycle_operations;`
  immediately after `physics_constraint_edits` (`:551`).

`src/editor/project.cpp`:

- `parse_constraint_lifecycle_operations(document, root, out)` modelled on
  `parse_ik_constraint_edits` (`:3059`): find optional `constraint_edits`,
  require object, find optional `operations`, require array, then per element
  apply §4.2's table using the file's existing `require_*` / `find_optional_member`
  helpers so errors carry a location and a `$.constraint_edits.operations[i]`
  path.
- `build_constraint_lifecycle_operations_value(ops)` next to `:4077`.
- Extend `build_constraint_edits_value` (`:4260`) with a fifth parameter and
  `constraint_edits.emplace("operations", ...)` when non-empty.
- **The emit gate** at `:4663-4666`: add `|| !project.constraint_lifecycle_operations.empty()`
  to the condition and pass the vector at `:4667-4671`. *Without this line
  scenario A step 4 fails and a tombstone-only project is silently destroyed on
  save.*
- Wire the parser into `load_project` after the four existing calls
  (`:6976-6994`).

#### Verification

```bash
cmake --build build && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
git diff --stat src/runtime include/marrow/runtime src/c_api include/marrow/marrow_c.h   # empty
```

**INVERTED GATE.** Revert only the `|| !project.constraint_lifecycle_operations.empty()`
clause, rebuild, and confirm scenario A step 4 **fails**. Restore it. Record
both outputs.

---

### Task 2 — Save-time validation (symbolic replay)

#### TDD step (RED)

Scenario **A2**, appended to A. For each, build the `ProjectData` directly,
call `save_project()` to a temp path, and assert it **fails** with the expected
message:

| Case | Operations | Expect |
| --- | --- | --- |
| empty source name | `Rename{Ik, "", "b"}` | reject |
| empty target | `Rename{Ik, "a", ""}` | reject |
| self rename | `Rename{Ik, "a", "a"}` | reject |
| delete carrying `new_name` | `Delete{Ik, "a", "b"}` | reject |
| order, source | `Rename{Ik,"a","b"}, Delete{Ik,"a"}` | reject, "already renamed or deleted" |
| order, source | `Delete{Ik,"a"}, Rename{Ik,"a","c"}` | reject |
| order, target | `Rename{Ik,"a","c"}, Rename{Ik,"b","c"}` | reject, "duplicate target" |
| legal chain | `Rename{Ik,"a","b"}, Rename{Ik,"b","c"}` | **accept** |
| legal reuse | `Delete{Ik,"a"}, Rename{Ik,"b","a"}` | **accept** |
| legal swap | `R{Ik,"a","t"}, R{Ik,"b","a"}, R{Ik,"t","b"}` | **accept** |
| cross-family independence | `Delete{Ik,"a"}, Delete{Physics,"a"}` | **accept** |
| upsert collision | `Rename{Ik,"a","editor_arm_reach"}` with that IK upsert present | reject |
| upsert stranded by consume | `Rename{Ik,"editor_arm_reach","x"}` with the upsert still named `editor_arm_reach` | reject |

The three "legal" rows and the cross-family row are the ones that prove the
validator is not simply refusing everything. Run: **expect the reject rows to
pass trivially** (nothing validates yet, so `save_project` succeeds and the
assertion that it *fails* fires). Record.

#### Implementation step (GREEN)

Append the §8.1 replay to `validate_project_for_save()` after the physics block
(`project.cpp:5777`). Per family maintain `consumed` and `introduced` as sorted
`std::vector<std::string>` (matching the `seen_*_names` idiom already in that
function — no hash containers on a determinism-sensitive path). Then the §8.1
step 6 cross-check against the four upsert vectors.

Messages must name the family and the offending name, e.g.
`"ik constraint lifecycle rename target 'c' is already introduced by an earlier operation"`.

#### Verification

```bash
cmake --build build && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

All A2 rows pass. Diff `src/runtime` etc. still empty.

---

### Task 3 — Phase A materialization

#### TDD step (RED)

Scenario **C**, using `assets/fixtures/skin_inherit_constraints.mskl` via
`create_minimal_project()`:

1. **Baseline.** `build_project_runtime()` succeeds; one transform constraint
   `cape_pull`; skin `cape` has one transform constraint index and one bone
   index (`cape_target`).
2. **Rename via record.** Set `constraint_lifecycle_operations =
   {Rename{Transform, "cape_pull", "cape_drag"}}` directly. Materialize with
   `build_project_runtime_document()`; assert root `transform[0].name ==
   "cape_drag"` **and** `skins.cape.transform == ["cape_drag"]`. Then
   `build_project_runtime()` succeeds and
   `transform_constraints()[0].name == "cape_drag"`, and skin `cape` still
   resolves one transform index and still has `cape_target`.
3. **Delete via tombstone.** `{Delete{Transform, "cape_pull"}}`. Materialized
   document has **no** root `transform` key and **no** `transform` key in
   `skins.cape` (spec §5.2 step 4). `build_project_runtime()` succeeds;
   `transform_constraints().empty()`; skin `cape` still has `cape_target`.
4. **Order preservation.** Against `ik_constraints.mskl` (13 base IK
   constraints, names in spec §2.9): delete the 1st, the 7th, and the 13th.
   Assert the surviving **10** names as an exact ordered sequence.
5. **Family isolation.** Against `path_transform_constraints.mskl`, a
   `Delete{Ik, "rope_follow"}` — a name that exists as a *path* constraint —
   leaves both base constraints intact when materialized defensively (spec
   §8.3), and is rejected by Task 4's validator.
6. **Phase A before Phase B.** A `Rename{Ik,"arm_positive","arm_renamed"}`
   record plus an IK upsert named `arm_renamed`: the materialized array has
   **one** `arm_renamed` carrying the upsert's field values, not two elements.

Run: **expect steps 2–6 to fail** — Phase A does not exist, so the document is
unchanged and the assertions on the renamed/deleted names fire. Record.

#### Implementation step (GREEN)

In `build_runtime_document()` (`project.cpp:5014`), immediately before the four
`merge_named_object_array_member` calls at `:5150`:

```cpp
apply_constraint_lifecycle_operations(
    &document.root, project.constraint_lifecycle_operations);
```

The helper, defined near `merge_named_object_array_member` (`:4690`):

- maps `ConstraintKind` → `"ik"|"path"|"transform"|"physics"`;
- per operation, performs §5.2 exactly;
- an unresolvable operation is a **no-op and `continue`**, matching the
  mesh-weight loop's `continue` at `:5076-5083`. Never partially applies.
- erases an emptied family key at root and inside each skin.

Leave the four `merge_named_object_array_member` calls untouched.

#### Verification

```bash
cmake --build build && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_fixture_smoke
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

**INVERTED GATE.** Temporarily delete the skin-rewrite half of the `rename`
branch, rebuild, and confirm scenario C step 2 fails **at
`build_project_runtime()`** with `skin references unknown transform constraint
'cape_pull'`. This is the §2.2 failure and it must be reachable. Restore.
Record the message verbatim.

---

### Task 4 — `validate_constraint_lifecycle_operations`

#### TDD step (RED)

Scenario **D**, over `ik_constraints.mskl` and `path_transform_constraints.mskl`.
For each, set the operations vector, call `build_project_runtime()`, assert it
returns an **error** and that the message identifies the cause:

| Case | Expected cause |
| --- | --- |
| `Rename{Ik,"no_such","x"}` | missing source |
| `Delete{Ik,"no_such"}` | missing source |
| `Rename{Ik,"arm_positive","arm_negative"}` | duplicate target |
| `Delete{Ik,"rope_follow"}` on `path_transform_constraints` | **family mismatch**, distinct from missing source |
| `Rename{Transform,"mirror_source","rope_follow"}` | duplicate target across the *path* family? **no** — this must be **accepted**, families are independent |
| `Delete{Ik,"arm_positive"}, Rename{Ik,"arm_positive","x"}` | invalid order |

Then, for each rejecting case, call `export_runtime_assets()` to a temp
directory and assert (a) it returns an error and (b) **no file was written** at
either output path.

Also: capture `serialize_project()` before and after each rejection and assert
byte-identity.

Run: expect every row to fail (no validator yet, so `build_project_runtime`
succeeds because Phase A silently no-ops). Record.

#### Implementation step (GREEN)

`validate_constraint_lifecycle_operations(project, base_skeleton_document)`
returning `std::optional<runtime::json::LoadError>`, next to
`validate_animation_edit_sequence`. It builds the four live name-sets from the
base document's root arrays, replays the operations, and reports the four
distinct causes. The family-mismatch report requires checking the *other three*
families for the name before concluding "missing source".

Call it in both entry points, immediately after the existing
`validate_animation_edit_sequence` call:

- `build_project_runtime()` — `project.cpp:7078-7083`
- `export_runtime_assets()` — `project.cpp:7145-7152`

#### Verification

```bash
cmake --build build && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
ctest --test-dir build --output-on-failure -L editor
```

---

### Task 5 — The two primitives

#### TDD step (RED)

Scenario **B** (project-only, over `player_idle.marrow`) and scenario **C2**
(base-backed and shadowing, over `skin_inherit_constraints.mskl`), matching spec
§12.1 B and C:

- `rename_constraint(project, base, Ik, "editor_arm_reach", "arm_reach_v2")` →
  `ok`, `used_operation == false`, `changed_upsert == true`,
  `constraint_lifecycle_operations.empty()`, the IK upsert renamed. **Survival:**
  the path/transform/physics upserts field-for-field unchanged;
  `timeline_edits` and `snap` unchanged.
- `delete_constraint(..., Physics, "editor_ribbon_secondary")` → the physics
  vector empty, operations still empty, **the other three upserts intact**.
- Base-backed rename over `skin_inherit_constraints`: `used_operation == true`,
  `changed_upsert == false`, one record appended.
- **Shadowing**: upsert `cape_pull` into `transform_constraint_edits`, then
  `delete_constraint(..., Transform, "cape_pull")` → **both** a tombstone and an
  erased upsert; materialize and assert `cape_pull` appears nowhere (the base
  did not resurrect).
- Rejections: unknown name; empty `to`; `to == from`; `to` colliding with a
  live constraint in the same family. After each, assert `serialize_project()`
  is byte-identical to the pre-call capture.
- After every accepted call, `save_project()` to a temp path and assert success,
  then reload and assert the state round-tripped.

Run: expect compile failure. Record.

#### Implementation step (GREEN)

Implement `rename_constraint` / `delete_constraint` per spec §9.2, in
`project.cpp` near the other `ProjectData`-level free functions. The
materialized name set in step 2 is obtained by calling the Task 3 helper on a
scratch copy of the base root and then scanning the upsert vectors — one code
path, no second definition of "what names exist".

Step 5 (`validate_project_for_save` on the candidate) is not optional. It is the
structural closure of the MAR-175 hole.

#### Verification

```bash
cmake --build build && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

**INVERTED GATE.** Temporarily remove step 5's `validate_project_for_save`
call, and add a scenario that renames a constraint to a name already used by
another constraint *in a way the primitive's own preflight would miss* — the
simplest is to bypass preflight by asserting that the removed call is what
catches a hand-built duplicate. If no such case exists, say so explicitly in the
task log rather than fabricating one: it means preflight already subsumes the
validator, which is a stronger result and should be recorded as such.

---

### Task 6 — Export, on a mutated project

#### TDD step (RED)

Scenario **F**, spec §11. Over `skin_inherit_constraints.mskl`:

1. Export baseline to `/tmp/marrow_mar177_base.mskl` + `.mbin`. Record both
   sizes.
2. `rename_constraint(..., Transform, "cape_pull", "cape_pull_renamed")`.
3. Export to `/tmp/marrow_mar177_rename.mskl` + `.mbin`. Assert the export
   **succeeded** (a stale skin reference would fail here, before any byte is
   written).
4. Assert `Δ(.mskl) == +16`, with the derivation in the message:

```
// Derivation (design §11.2): the exported .mskl is serialize_pretty() of the
// materialized document. `cape_pull` occurs in it exactly twice -- the root
// transform[0].name and the skins.cape.transform[0] reference -- and nowhere
// else (it is not a bone, slot, skin, animation, or object key). Renaming to a
// name 8 UTF-8 bytes longer therefore grows the text by 2 * 8 = 16.
//
// +8 instead of +16 means the skin reference was NOT rewritten.
// Any other value means the occurrence count changed: re-derive it from the
// exported text, do not edit this constant.
```

Also assert the occurrence count directly in the exported text: 2 before, 0
after (`cape_pull` as a whole token), 2 for `cape_pull_renamed` after.

5. Assert `Δ(.mbin) == +8`, with its derivation:

```
// Derivation (design §11.3): collect_strings() (binary.cpp:165-186) interns
// each DISTINCT string once, so the name occupies one string-table entry no
// matter how many times it occurs. The new name occurs nowhere else and the old
// name occurs nowhere else, so the table keeps its entry count and every index;
// every index varint in encode_value is unchanged, as is the boolean block and
// the animation section. The only delta is that entry's varint(len)+bytes, and
// both 9 and 17 are < 128 so both length varints are one byte:
//     (1 + 17) - (1 + 9) = 8.
//
// A delta of 16 means the .mbin is counting occurrences, i.e. the encoding
// stopped interning. Any other value means the string table reshuffled or the
// encoding moved; the message must say which by also reporting the table's
// entry count before and after (equal => the encoding moved).
```

6. Read the `.mbin` string-table entry count directly in the test — `MBIN`
   (4 bytes), `varint(version)`, `varint(count)` — with a five-line varint
   reader, and assert it is **equal** before and after the rename. Report it in
   the failure message for (5).
7. Delete case (spec §11.5): after `delete_constraint`, assert
   (a) `cape_pull` occurs 0 times in the exported `.mskl`;
   (b) the exported `.mskl` has no root `transform` key and no
       `skins.cape.transform` key;
   (c) reloading the `.mbin` with `load_skeleton_document()` succeeds and yields
       `transform_constraints().empty()` and an empty
       `skins["cape"].transform_constraint_indices`;
   (d) the `.mbin` string-table count decreased by **exactly 1**.

Run: expect (2) onward to fail. Record.

#### Verification

```bash
cmake --build build && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_inspect /tmp/marrow_mar177_rename.mbin
```

If `Δ(.mskl)` or `Δ(.mbin)` differs from 16 / 8: **do not edit the constant.**
Print the exported occurrence count and the string-table counts, re-derive, and
record the corrected derivation in the task log and in spec §14 rows 3–5.

---

### Task 7 — Chains, collisions, merge, reload

#### TDD step (RED)

Scenario **D2** and **E**, spec §12.1 D and E:

- The chain, swap, and reuse sequences from Task 2's table, but now run through
  `build_project_runtime()` against real fixtures so the *materialized* result
  is asserted, not just the save validator.
- After the 3-delete order test on `ik_constraints.mskl`, assert the surviving
  10 names as an ordered sequence.
- Load a `.marrow` carrying unknown top-level keys plus a populated
  `operations` array; run one accepted rename; `save_project()`; reload; assert
  the operations round-trip **in order** and the unknown top-level keys survive.
- Assert that a project with a populated `operations` array and *no* upserts
  round-trips (the Task 1 emit gate, exercised through the real save path).

#### Verification

```bash
cmake --build build && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
ctest --test-dir build --output-on-failure
```

---

### Task 8 — Sweep, documentation, closure

#### Sweep

```bash
# The registry did NOT change. All must still report 62.
awk '/kOperationSpecs\[\]/{f=1} f&&/^    \{"/{c++} f&&/^};/{print c; exit}' src/editor/agent_dispatch.cpp
grep -c 'exact 62-operation registry' src/editor/shell_smoke_graph.cpp     # 7
grep -c 'exact 62-operation registry' src/editor/shell_smoke_timeline.cpp  # 1
grep -c '== 62' tools/mcp/test_client.py                                   # 2
grep -c 'std::array<OperationExpectation, 62>' src/samples/agent_dispatch_smoke.cpp  # 1

# No forbidden file was touched.
git diff --stat src/runtime include/marrow/runtime include/marrow/marrow_c.h src/c_api \
                src/editor/preferences.cpp include/marrow/editor/preferences.hpp     # empty
git diff --stat -- 'src/editor/shell_*'                                              # empty
git diff --stat -- src/editor/agent_dispatch.cpp 'src/editor/agent_handlers_*' tools/mcp  # empty

# The colour/size literals the brief protects are untouched.
git diff -U0 | grep -n 'IM_COL32(56\|(51, 56, 64)\|"x": 56.0\|56,995,840\|56 bytes/bone\|IM_COL32(208,134,57,230)\|rgb(54,57,64)'  # empty
```

#### Documentation

- `docs/root1/format-spec.md`, under `### constraint_edits` (`:953`): add
  `operations` to the field list and a short subsection giving the two record
  shapes, the field rules, the ordering semantics, the base-backed vs
  project-only ownership rule, the "omitted when empty" guarantee, and the
  statement that the section never enters `.mskl`/`.mbin` and changes no
  runtime format version.
- `AGENTS.md`: a MAR-177 validation row. **Do not change the `62` at `:161`,
  `:273`, `:318`, or `:320`** — MAR-177 adds no operation. Add a compatibility
  row stating `.marrow` gained exactly one optional member and that `.mskl` v1,
  `.mbin` v2, and C ABI v1 are unchanged with a zero-byte diff.
- `docs/root1/discription.md`: a MAR-177 paragraph in the established style,
  covering: create already existed and rename/delete did not; why the record is
  an ordered array; the base-backed/project-only ownership rule; that skins name
  constraints and an unpruned reference makes the project unopenable, not merely
  wrong; and the `16 : 8` export pair and what each half proves.
- `docs/root1/editing-gap-analysis.md`: update the constraint rows to say
  rename/delete now exist at the **project layer**, with the surfaces still
  pending in MAR-178. **Leave every `62` as-is.**

#### Closure

Update `.agents/tasks/prd-marrow-runtime.json`: MAR-177 `status: "done"`,
`completedAt: "2026-08-30"`. **Only** if every gate below passed.

---

## Full Verification Checklist

### Build

- [ ] `cmake -S . -B build` clean
- [ ] `cmake --build build` clean, no new warnings
- [ ] `cmake --build build --target marrow_constraint_warning_check`

### Storage and validation

- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` → PASSED
- [ ] Scenario A: absent `operations` → byte-identical `player_idle.marrow`
- [ ] Scenario A step 4: tombstone-only project round-trips (**inverted gate
      demonstrated and recorded**)
- [ ] Scenario A step 5: all eleven malformed-load rejections, each with a
      located error
- [ ] Scenario A2: every reject row rejects, every legal row (chain, reuse,
      swap, cross-family) is accepted
- [ ] Every rejection leaves `serialize_project()` byte-identical

### Materialization

- [ ] Scenario C: base rename rewrites root **and** `skins.cape.transform`
- [ ] Scenario C: base delete removes the root `transform` key and the
      `skins.cape.transform` key, and the runtime still loads
- [ ] Scenario C: skin `cape` keeps `cape_target` after both
- [ ] Order test: 10 survivors of 13, asserted as an ordered sequence
- [ ] Phase A precedes Phase B: a renamed base + matching upsert yields one
      element
- [ ] **Inverted gate**: removing the skin rewrite makes
      `build_project_runtime()` fail with `skin references unknown transform
      constraint 'cape_pull'` — message recorded verbatim

### Primitives

- [ ] Project-only rename/delete touch only the upsert; operations stay empty
- [ ] Base-backed rename/delete append exactly one record
- [ ] Shadowing delete emits **both** a tombstone and an upsert erase; the base
      does not resurrect
- [ ] **Survival**: after every delete, the other constraints, the neighbouring
      skin scopes, `timeline_edits`, and `snap` are asserted unchanged
- [ ] Every accepted call is followed by a successful `save_project()` and a
      matching reload

### Export

- [ ] `Δ(.mskl) == +16`, with the occurrence derivation in the message
- [ ] `Δ(.mbin) == +8`, with the interning derivation in the message
- [ ] `.mbin` string-table entry count **unchanged** across the rename
- [ ] Delete: `cape_pull` occurs 0 times; no empty `transform` keys; the `.mbin`
      reloads; table count −1
- [ ] A rejected project writes **no** output file

### Regression

- [ ] `./build/marrow_unit_tests`
- [ ] `./build/marrow_fixture_smoke`
- [ ] `./build/marrow_selection_tests`
- [ ] `./build/marrow_timeline_model_tests`
- [ ] `./build/marrow_agent_dispatch_smoke` → PASSED, **62** operations
- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
      → passed, unchanged
- [ ] `ctest --test-dir build --output-on-failure`
- [ ] `ctest --test-dir build --output-on-failure -L runtime`
- [ ] `ctest --test-dir build --output-on-failure -L editor`

### Compatibility

- [ ] Zero-byte diff: `src/runtime/**`, `include/marrow/runtime/**`,
      `include/marrow/marrow_c.h`, `src/c_api/**`
- [ ] Zero-byte diff: every `src/editor/shell_*`, `agent_*`, `authoring.*`,
      `tools/mcp/**`
- [ ] Zero-byte diff: `src/editor/preferences.cpp`,
      `include/marrow/editor/preferences.hpp`
- [ ] `.marrow` gains exactly one optional member; `format-spec.md` updated
- [ ] Every `62` in code, tests, and prose is still `62`
- [ ] `~/Library/Application Support/Marrow` still does not exist

### Documentation

- [ ] `docs/root1/format-spec.md` `constraint_edits.operations` subsection
- [ ] `AGENTS.md` MAR-177 validation and compatibility rows
- [ ] `docs/root1/discription.md` MAR-177 paragraph
- [ ] `docs/root1/editing-gap-analysis.md` constraint rows
- [ ] `.agents/tasks/prd-marrow-runtime.json` MAR-177 closed

---

## Decisions Taken Under Ambiguity

Carried from spec §14. Each is measured in Task 0 or the task that depends on
it. **If a measurement disagrees with the spec, correct the spec and record the
correction — do not select a witness that happens to pass.**

| # | Claim | Gate |
| --- | --- | --- |
| 1 | Registry is 62 | Task 0 |
| 2 | 7 + 1 guards | Task 0 |
| 3 | `Δ(.mskl) == +16` | Task 6 |
| 4 | `Δ(.mbin) == +8` | Task 6 |
| 5 | `cape_pull` occurs twice | Task 0, re-checked against the exported text in Task 6 |
| 6 | `project.hpp` may include `selection.hpp` | Task 1 |
| 7 | All four families reject an empty array | Task 0 |
| 8 | `create_minimal_project` over `skin_inherit_constraints.mskl` exports without an atlas | Task 5 |
| 9 | The four GUI defaults pass `validate_project_for_save` | Task 0 |
| 10 | No animation timeline names a constraint | Task 0 |
