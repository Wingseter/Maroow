# MAR-178 Constraint Rename and Delete Surfaces — Implementation Plan

- Design spec: `docs/superpowers/specs/2026-08-30-mar-178-constraint-rename-delete-surfaces-design.md`
- Story: `MAR-178`, `dependsOn: ["MAR-177"]`
- Branch: `feat/mar-168`

Read the spec first. This plan does not restate its reasoning; it sequences the
work and pins the verification.

---

## Global Constraints

1. **Do not touch `src/editor/project.cpp` or
   `include/marrow/editor/project.hpp`.** They are MAR-177's files and are under
   concurrent review. MAR-178 *calls* `rename_constraint()`,
   `delete_constraint()` and `validate_constraint_lifecycle_operations()`; it
   changes none of them. `git diff --stat` on both must be empty at closure.
2. **Go through the primitives, never around them.** Every mutation of a
   constraint's identity happens inside `rename_constraint()` /
   `delete_constraint()`. Nothing in this story writes
   `constraint_lifecycle_operations`, rewrites a `*_constraint_edits[].name`, or
   touches `skins[*].<family>` directly. That is what keeps the unopenable- and
   unsavable-project guarantees MAR-177 bought.
3. **Assert survival and round-trip, not return codes.** Every delete test reads
   back the **survivors** by name and completes a **save → reload** cycle. The
   worst failure here is on load, not on save — it cannot be caught by checking
   `ok`.
4. **Guard the agent surface, not just the GUI.** Every behaviour the GUI gets,
   the agent gets, and the agent smoke asserts it independently. MAR-172's data
   loss existed because a spec requirement was silently narrowed to the GUI.
5. **Rejection changes nothing, asserted on bytes.** After every rejected edit,
   assert `serialize_project()` is the *identical string* and
   `session.undo_count()` is unchanged. Not "the vector size did not move".
6. **Registry goes 62 → 64, at every site, atomically.** Use the counting patch
   in Task 6: it asserts the before-count is 62 and the after-count is 64 and
   aborts otherwise, so a miscount cannot half-land.
7. **TDD.** Every task writes the failing assertion first and **records the
   observed failure text**. A test that passes before the implementation is a
   broken test — MAR-176 shipped one such inversion and MAR-176's review found a
   tie case that never exercised its own tie-break. Where a task says
   **INVERTED GATE**, the RED step must be demonstrated by a temporary edit and
   then reverted.
8. **Never run `marrow_editor_shell` without `--auto-close`**, and never create
   `~/Library/Application Support/Marrow`.

---

## File and Responsibility Map

| File | Change |
| --- | --- |
| `include/marrow/editor/constraint_catalog.hpp` | **new** — `ConstraintCatalogEditKind`, `ConstraintCatalogEdit`, `ConstraintCatalogResult`, `apply_constraint_catalog_edit()`, `constraint_family_key()`, `parse_constraint_family()` |
| `src/editor/constraint_catalog.cpp` | **new** — the five steps, the affected-skins summary, the selection cascade |
| `src/editor/shell_constraints.hpp` | `+ reconcile_constraint_selection()`, `+ request/confirm/cancel_constraint_delete()`, `+ request/commit/cancel_constraint_rename()`, `+ apply_constraint_catalog_action()` |
| `src/editor/shell_constraints.cpp` | the popup state, the two modals, the four `Rename... / Delete...` rows, the action wrapper, the reconcile helper |
| `src/editor/shell_preview.cpp` | one call to `reconcile_constraint_selection()` in each of `undo_project_change()` and `redo_project_change()` |
| `src/editor/agent_dispatch.cpp` | **+2 `kOperationSpecs` rows** after `:79` |
| `src/editor/agent_handlers_constraints.cpp` | two new branches in `handle_constraint_operation` |
| `src/samples/agent_dispatch_smoke.cpp` | `:38` array size, `+2` expectation rows, new cases |
| `src/editor/shell_smoke_constraints.cpp` | **new** — `validate_constraint_lifecycle_shell_smoke()` |
| `src/editor/shell_smoke_scenarios.hpp` | one declaration |
| `src/editor/shell_smoke.cpp` | one invocation |
| `src/editor/shell_smoke_graph.cpp` | 7 × `!= 62U` and 7 × message |
| `src/editor/shell_smoke_timeline.cpp` | 1 × `!= 62U` and 1 × message |
| `src/samples/editor_project_smoke.cpp` | scenarios A–G |
| `tools/mcp/tools/editing.py` | **+2 `types.Tool`** |
| `tools/mcp/test_client.py` | `:33-44`, `:51`, `:53`, plus the new E2E leg |
| `CMakeLists.txt` | `src/editor/constraint_catalog.cpp`, `src/editor/shell_smoke_constraints.cpp` |
| `AGENTS.md`, `docs/root1/discription.md`, `docs/root1/editing-gap-analysis.md`, `docs/root1/refector.md` | MAR-178 prose and the 62 → 64 sweep |

**Not touched:** `src/editor/project.cpp`, `include/marrow/editor/project.hpp`,
`src/editor/session.cpp`, `include/marrow/editor/session.hpp`,
`src/editor/selection.cpp`, `include/marrow/editor/selection.hpp`,
`src/runtime/**`, `include/marrow/runtime/**`, `include/marrow/marrow_c.h`,
`src/c_api/**`, `src/editor/preferences.*`, `docs/root1/format-spec.md`.

---

## Tasks

### Task 0 — Re-read as-built MAR-177 and MEASURE (mandatory, no code)

Nothing in this task edits a file. Every number below is a claim in the spec
that must be confirmed before it is depended on.

```bash
cd /Users/kwon/Workspace/C/Maroow

# ---- 1. Registry count and category split. Spec §2.9 claims 62 / edit 37. ----
python3 - <<'PY'
import re, collections
src = open('src/editor/agent_dispatch.cpp').read()
block = src.split('kOperationSpecs[] = {', 1)[1].split('\n};', 1)[0]
rows = re.findall(r'\{"([^"]+)",\s*"([^"]+)"', block)
print('registry:', len(rows), collections.Counter(c for _, c in rows))
PY
#    expect: registry: 62 Counter({'edit': 37, 'inspection': 12, 'management': 10, 'validation': 3})

# ---- 2. Guard sites. Spec §2.9 claims 7+1 comparisons and 7+1 messages. ----
grep -n '!= 62U' src/editor/shell_smoke_graph.cpp     # expect 7: 148 636 1558 2128 3142 3957 4603
grep -n '!= 62U' src/editor/shell_smoke_timeline.cpp  # expect 1: 3697
grep -c 'exact 62-operation registry' src/editor/shell_smoke_graph.cpp     # expect 7
grep -c 'exact 62-operation registry' src/editor/shell_smoke_timeline.cpp  # expect 1
grep -n 'OperationExpectation, 62' src/samples/agent_dispatch_smoke.cpp    # expect 1 line (:38)
grep -n '== 62' tools/mcp/test_client.py                                   # expect 2 (:51, :53)

# ---- 3. Prose sites. Spec §8.6 claims nine live ones. ----
grep -n '\b62\b' AGENTS.md docs/root1/editing-gap-analysis.md docs/root1/refector.md
#    LIVE  (must become 64): AGENTS.md:161; editing-gap-analysis.md:23,85,86,95,192,455;
#                            refector.md:20,112
#    HISTORICAL (must NOT change): AGENTS.md:274,334,336,389,412,442,444,659
#    Any hit outside those two lists invalidates §8.6; re-classify before editing anything.

# ---- 4. The atlas gate (spec §4, ambiguity #4). Decision is CLOSED: keep it. ----
sed -n '286,298p' src/editor/project.cpp
sed -n '5510,5515p' src/editor/project.cpp
#    expect "$.runtime.atlases" / "array must not be empty" and
#           "at least one atlas path is required"
#    The second string is the one Task 5 scenario E asserts VERBATIM.

# ---- 4b. A reload is a full materialization (spec §10.5, ambiguity #4b). ----
grep -n 'build_project_runtime(\*project_ptr' src/editor/project.cpp   # expect :7495
sed -n '7517,7527p' src/editor/project.cpp                             # path overload -> Document overload
#    This is why every delete test must save AND RELOAD. If :7495 is gone,
#    stop: the delete assertions in Tasks 5 and 7 no longer prove anything.

# ---- 4c. Skins are the complete referrer set (spec §2.1, ambiguity #4c). ----
awk 'NR>=5214 && NR<=5500' src/runtime/skeleton_parse.cpp | grep -o '"[a-zA-Z]*"' | sort -u
#    expect exactly: animations attachment bones color deform default drawOrder
#                    events inherit rotate scale shear slots translate
#    ANY of "ik" "path" "transform" "physics" appearing here means a timeline
#    references a constraint and the affected-reference preview is incomplete.

# ---- 5. Selection active-fallback agreement (spec §6.2, ambiguity #5). ----
sed -n '137,207p' src/editor/selection.cpp
#    expect prune :155-159 and remap :195-199 to choose replacement.size()-1U identically

# ---- 6. History carries no selection (spec §6.3, ambiguity #6). ----
sed -n '355,362p' src/editor/shell_state.hpp
sed -n '163,181p' src/editor/shell_core.cpp
#    expect no SelectionSet member and a three-field comparator

# ---- 7. Who reconciles the selection (spec §2.6, ambiguity #7). ----
grep -n 'reconcile_selection_to_runtime' src/editor/*.cpp
#    expect exactly shell_core.cpp:628 (inside reload_project) and shell_asset_watch.cpp:189.
#    If it also appears inside rebuild_project_runtime (shell_core.cpp:498-533),
#    §6.4's helper is redundant and the design must be revisited before Task 2.

# ---- 8. The four GUI insertion seams (spec §7.1). ----
grep -n 'constraints_tab == \|const std::string selected_name\|if (selected_name.empty())' \
  src/editor/shell_constraints.cpp
#    expect selected_name at 732 899 1151 1430 and the empty check at 740 907 1159 1438

# ---- 9. Fixture facts (spec §2.10, ambiguity #12). ----
python3 - <<'PY'
import json
p = json.load(open('assets/fixtures/player_idle.marrow'))
print('project-only:', {k: [e['name'] for e in v]
                        for k, v in p['constraint_edits'].items() if isinstance(v, list)})
print('atlases:', p['runtime']['atlases'])
s = json.load(open('assets/fixtures/skin_inherit_constraints.mskl'))
print('transform:', [c['name'] for c in s['transform']])
print('skins.cape:', s['skins']['cape'])
print('cape_pull occurrences:',
      json.dumps(s).count('cape_pull') - json.dumps(s).count('cape_pull_'))
PY
#    expect ik/editor_arm_reach, path/editor_guide_follow, transform/editor_transform_follow,
#           physics/editor_ribbon_secondary; atlases ["player_idle.matl"];
#           transform ["cape_pull"]; skins.cape {"bones":["cape_target"],"transform":["cape_pull"]};
#           cape_pull occurrences: 2

# ---- 10. Baseline build, so later deltas mean something. ----
cmake -S . -B build && cmake --build build
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_agent_dispatch_smoke
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
ctest --test-dir build --output-on-failure
git status --porcelain    # must be clean
```

**Record in the task log** the measured results and any that differ from the
spec. A difference in (1), (2) or (3) means §8.6's sweep list is wrong and must
be corrected *before* Task 6 runs. A difference in (4b) invalidates every delete
assertion in Tasks 5 and 7. A difference in (4c) means the affected-reference
preview is incomplete and §5.4 must be reopened. A difference in (7) means §6.4
must be revisited. A difference in (9)'s occurrence count means §11's `2` must
be re-derived from the export, not edited.

**Items 4, 4b and 4c were confirmed independently by the MAR-177 review.** They
are re-measured here anyway — not from distrust, but because this plan is
executed later than the review and a fact re-verified at the point of use is the
only kind that stays true.

---

### Task 1 — The UI-free lifecycle command

**Files:** `include/marrow/editor/constraint_catalog.hpp` (new),
`src/editor/constraint_catalog.cpp` (new), `CMakeLists.txt`.

#### TDD step (RED)

In `src/samples/editor_project_smoke.cpp`, add scenario **A** (spec §12.1 A) and
**G** (spec §12.1 G) against `player_idle`:

```cpp
// MAR-178 A: the command over a project-only constraint.
marrow::editor::EditorSession session;
/* open player_idle.marrow */
marrow::editor::ConstraintCatalogEdit edit{
    marrow::editor::ConstraintCatalogEditKind::Rename,
    marrow::editor::ConstraintKind::Ik, "editor_arm_reach", "arm_reach_v2"};
const auto result = marrow::editor::apply_constraint_catalog_edit(
    session, edit, nullptr,
    {marrow::editor::EditKind::EditProperty, "Renamed IK constraint",
     "constraint-catalog", false,
     marrow::editor::EditImpact::Project | marrow::editor::EditImpact::Runtime |
         marrow::editor::EditImpact::Preview});
// ok && changed && !used_operation && changed_upsert && affected_skins.empty()
// undo_count() == before + 1
// the other THREE constraints present, field-for-field unchanged
// timeline_edits / snap / editor_metadata unchanged
// session.save(temp) succeeds; reload matches
```

Build must fail: the header does not exist. **Record the compiler error.**

#### Implementation step (GREEN)

Write the header exactly as spec §5.2, and the TU as spec §5.3:

```cpp
ConstraintCatalogResult apply_constraint_catalog_edit(
    EditorSession& session, const ConstraintCatalogEdit& edit,
    SelectionSet* selection, EditDescriptor descriptor) {
    ConstraintCatalogResult out;
    // 1. Guard: has_project(), base_skeleton_document() != nullptr,
    //    !transaction_active(); a Delete must carry an empty destination.
    // 2. Summarise affected_skins from the PRE-mutation session.runtime_data().
    // 3. begin_edit(descriptor); call the MAR-177 primitive on tx.project();
    //    on !r.ok -> tx.cancel(), out.message = r.message, return.
    // 4. tx.commit(); propagate !commit and !changed as failures.
    // 5. Only now: selection->remap(...) for Rename / Delete.
}
```

Step 2 in full — this is the only part with real content:

```cpp
const runtime::SkeletonData& skeleton = *session.runtime_data();
const auto index = find_family_index(skeleton, edit.family, edit.source);
if (index.has_value()) {
    for (const runtime::SkinData& skin : skeleton.skins()) {
        const std::vector<std::size_t>& refs = family_indices(skin, edit.family);
        if (std::find(refs.begin(), refs.end(), *index) != refs.end()) {
            out.affected_skins.push_back(skin.name);
        }
    }
}
```

using `SkinData::{ik,path,transform,physics}_constraint_indices`
(`runtime/skeleton.hpp:285-288`).

`constraint_family_key()` returns `"ik"`, `"path"`, `"transform"`, `"physics"`;
`parse_constraint_family()` accepts exactly those four and nothing else.

Add `src/editor/constraint_catalog.cpp` to every target that links the editor
library in `CMakeLists.txt`.

#### Verification

```bash
cmake --build build
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

**INVERTED GATE 1.** Temporarily move the `affected_skins` capture from before
the transaction to after `commit()`. Scenario B (Task 5) must then report
`affected_skins` empty for `cape_pull`. Revert.

---

### Task 2 — The selection cascade and the narrow reconcile

**Files:** `src/editor/constraint_catalog.cpp` (step 5),
`src/editor/shell_constraints.hpp`, `src/editor/shell_constraints.cpp`,
`src/editor/shell_preview.cpp`.

#### TDD step (RED)

Project smoke scenario **C** (spec §12.1 C), with a local `SelectionSet`:

```cpp
marrow::editor::SelectionSet selection;
selection.replace(marrow::editor::BoneSelection{"arm_l"});
selection.toggle(marrow::editor::ConstraintSelection{ConstraintKind::Ik, "editor_arm_reach"});
// rename -> items().size() == 2, the constraint member reads "arm_reach_v2",
//           the bone member is untouched, active() is the constraint,
//           result.selection_changed == true
// delete a selected constraint out of a THREE-member set ->
//           the member is gone, order preserved, active is the LAST survivor
// delete an UNSELECTED constraint -> selection_changed == false and the set is byte-equal
// pass nullptr -> the edit still applies, selection_changed == false
```

These must fail before step 5 exists. **Record the failure text.**

#### Implementation step (GREEN)

Step 5 of the command, exactly as spec §5.3 — after a successful commit, never
before.

Then in `shell_constraints.cpp`:

```cpp
bool reconcile_constraint_selection(ShellState* state) {
    if (state == nullptr || !state->load_result ||
        state->load_result.skeleton_data == nullptr) {
        return false;
    }
    const marrow::runtime::SkeletonData& skeleton = *state->load_result.skeleton_data;
    return state->selection.prune(
        [&](const marrow::editor::SelectionItem& item) {
            const auto* constraint = std::get_if<marrow::editor::ConstraintSelection>(&item);
            return constraint == nullptr ||
                marrow::editor::selection_item_exists(item, skeleton);
        });
}
```

Declare it in `shell_constraints.hpp`. Call it once in `undo_project_change()`
and once in `redo_project_change()` (`shell_preview.cpp`), beside the existing
`viewport_ffd::reconcile_selection(state)` call.

**Do not** call `reconcile_selection_to_runtime()` there — spec §6.4 states why,
and INVERTED GATE 3 in Task 3 proves it.

#### Verification

```bash
cmake --build build
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

**INVERTED GATE 2.** Move step 5 to before `tx.commit()`, then force a commit
failure. The rejection assertion must fail on the selection having moved.
Revert.

---

### Task 3 — The GUI affordances

**Files:** `src/editor/shell_constraints.hpp`, `src/editor/shell_constraints.cpp`,
`src/editor/shell_smoke_constraints.cpp` (new),
`src/editor/shell_smoke_scenarios.hpp`, `src/editor/shell_smoke.cpp`,
`CMakeLists.txt`.

#### TDD step (RED)

New TU `src/editor/shell_smoke_constraints.cpp` with
`validate_constraint_lifecycle_shell_smoke(const std::filesystem::path&)`,
declared in `shell_smoke_scenarios.hpp` and invoked from `shell_smoke.cpp`
beside the other `validate_*_shell_smoke` calls. Open its own `ShellState` via
`reload_project()`. Guard on the **64**-op registry in the house style — leave
this at `64U` from the start, so Task 6 does not have to revisit this file:

```cpp
const std::size_t operation_count_before = marrow::editor::agent_operation_descriptor_count();
if (operation_count_before != 64U) {
    std::cerr << "Constraint lifecycle shell smoke requires the exact 64-operation registry.\n";
    return false;
}
```

(It fails until Task 4. That is expected and is noted in the task log.)

Cases, all spec §12.2:

1. **Confirmation.** `request_constraint_delete(&state, kind, name)` then
   `cancel_constraint_delete(&state)` leaves `session.undo_count()` and
   `serialize_project()` unchanged; `confirm_constraint_delete(&state)` applies.
2. **Rename follows the selection.** Select `editor_transform_follow`, rename to
   `transform_follow_v2`, assert
   `state.selection.active_constraint()->constraint_name == "transform_follow_v2"`
   **and** `state.selection.items().size()` unchanged.
3. **Delete prunes the selection.** `items().size()` drops by one; no
   `ConstraintSelection` for the deleted name survives; the *"; N selected"*
   count therefore matches the number of live members.
4. **Undo/redo.** Undo restores the constraint and the exact pre-edit
   `serialize_project()` string; the selection is **not** restored; after the
   reconcile no ghost remains and a co-selected `BoneSelection` is still there.
5. **All four families.** Rename and delete one constraint in each of IK, path,
   transform, physics from `player_idle`, asserting the surviving three by name
   each time.
6. **Base-backed through the shell, with a save → reload.** Write a temp
   `.marrow` over `skin_inherit_constraints.mskl` borrowing `player_idle.matl`,
   `reload_project()` it, rename `cape_pull` → `cape_drag`, and assert the
   rebuilt skeleton's skin `cape` resolves the new name. Then delete it,
   `save_project_file(&state, false)`, `reload_project(&state)`, and assert the
   **reload succeeds** with no transform constraints and no `skins.cape`
   transform indices. `reload_project()` re-runs `build_project_runtime()` and
   `load_skeleton_data` (Task 0 item 4b), so this is the shell-level form of the
   unopenable-project assertion. A `save_project_file()` that merely returns
   `true` proves nothing here.
7. **Message parity.** Force a collision through the shell path; store the
   message for Task 4's comparison.
8. **The atlas message** (spec §4.2). Empty `atlas_paths` in memory, attempt a
   rename, and assert the rendered `state->error_message` contains **both** the
   surface clause and `"at least one atlas path is required"` verbatim, and that
   `serialize_project()` and `undo_count()` are unchanged.

Run: must fail on the missing helpers. **Record the failure text.**

#### Implementation step (GREEN)

**GUI-free helpers first** (this is what makes the smoke possible without
ImGui), in `shell_constraints.cpp`, declared in `shell_constraints.hpp`:

```cpp
enum class ConstraintCatalogAction { Rename, Delete };

bool apply_constraint_catalog_action(
    ShellState* state, ConstraintCatalogAction action,
    ConstraintKind family, std::string_view source, std::string_view destination);

void request_constraint_rename(ShellState*, ConstraintKind, std::string source);
void request_constraint_delete(ShellState*, ConstraintKind, std::string name);
bool confirm_constraint_rename(ShellState*, std::string_view destination);
bool confirm_constraint_delete(ShellState*);
void cancel_constraint_catalog(ShellState*);
bool reconcile_constraint_selection(ShellState*);   // from Task 2
```

`apply_constraint_catalog_action()` mirrors `apply_animation_catalog_action()`
(`shell_project_panels.cpp:533-608`):

- refuse while `authoring_gesture_active(*state)` or
  `state->session.transaction_active()`, with the existing
  `"Finish the active edit before …"` status;
- compose the label with `constraint_kind_label(family)`:
  `"Renamed IK constraint editor_arm_reach to arm_reach_v2"` /
  `"Deleted Physics constraint editor_ribbon_secondary"`;
- call `apply_constraint_catalog_edit(state->session, edit, &state->selection,
  {EditKind::EditProperty, label, "constraint-catalog", false,
   EditImpact::Project | EditImpact::Runtime | EditImpact::Preview})`;
- on failure set `state->error_message` to the result's message and
  `state->status_message` to `"Constraint edit failed"`, then
  `sync_shell_from_editor_session(state)` and return `false`. **One exception,
  spec §4.2**: when the message is `"at least one atlas path is required"`,
  prefix it with `"Cannot rename: the project must reference at least one atlas
  before it can be saved. "` — the validator's sentence stays verbatim, and the
  clause in front of it says which action was refused and why the two are
  connected. Match on the message text, not on a re-derived atlas check, so the
  prefix cannot fire on a state the validator would have accepted;
- on success `sync_shell_from_editor_session(state)`, clear
  `state->error_message`, set `state->status_message` to the label, and
  `state->selected_timeline_track_id.reset()`.

**Then the ImGui layer.** One file-static popup state beside the existing
statics:

```cpp
struct ConstraintCatalogPopupState {
    ConstraintKind family{ConstraintKind::Ik};
    std::string source;
    std::array<char, 128> name{};
    std::string delete_target;
    ConstraintKind delete_family{ConstraintKind::Ik};
    std::vector<std::string> affected_skins;
};
```

`draw_constraint_catalog_popups(ShellState*)` draws both modals, structurally
identical to `draw_animation_catalog_popups()`
(`shell_project_panels.cpp:86-167`), and is called **unconditionally** just
before the `ImGui::End()` at `shell_constraints.cpp:1631`.

The button row goes immediately after each family's `selected_name` computation
and before its `if (selected_name.empty())` — after `:732`, `:899`, `:1151`,
`:1430`:

```cpp
const bool catalog_blocked =
    authoring_gesture_active(*state) || state->session.transaction_active();
ImGui::BeginDisabled(catalog_blocked || selected_name.empty());
if (ImGui::Button("Rename...")) {
    state->error_message.clear();
    request_constraint_rename(state, ConstraintKind::Ik, selected_name);
    ImGui::OpenPopup(kConstraintRenamePopup);
}
ImGui::SameLine();
if (ImGui::Button("Delete...")) {
    state->error_message.clear();
    request_constraint_delete(state, ConstraintKind::Ik, selected_name);
    ImGui::OpenPopup(kConstraintDeletePopup);
}
ImGui::EndDisabled();
```

Rename modal (spec §7.2): `InputText` with
`EnterReturnsTrue | AutoSelectAll`, `SetKeyboardFocusHere()` on
`IsWindowAppearing()`, seeded with the current name; Apply disabled on empty,
on unchanged, and on `constraint_exists(skeleton, family, candidate)`; the
collision line replaces the reference line while showing;
`state->error_message` rendered inline in `theme::kStateErr` with the modal
**staying open**.

Delete modal (spec §7.3): the question line, the
`Also removes it from N skin(s): …` line when non-empty, the
`This action can be undone.` line, `[Delete] [Cancel]`, the same inline error
and the same block gate.

**No last-constraint gate** (spec §7.5) and **no reuse of
`unique_constraint_name()`** (spec §7.4).

Register both new source files in `CMakeLists.txt` (`shell_smoke_constraints.cpp`
beside `:894-901`).

#### Verification

```bash
cmake --build build
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

(The registry guard still fails until Task 4; note the exact message in the log
and re-run after Task 4.)

**INVERTED GATE 3.** Replace `reconcile_constraint_selection()` with
`reconcile_selection_to_runtime()` in `undo_project_change()`. Case 4's
co-selected `BoneSelection` assertion must fail. Revert.

**INVERTED GATE 4.** Delete the `remap` call on the rename path. Case 2 must
fail on the active constraint name. Revert.

**INVERTED GATE 5.** Delete the `remap` call on the delete path. Case 3 must
fail on `items().size()`. Revert.

**INVERTED GATE 6.** Make `cancel_constraint_delete()` fall through to
`confirm_constraint_delete()`. Case 1 must fail on `undo_count()`. Revert.

---

### Task 4 — The agent operations and the C++ registry

**Files:** `src/editor/agent_dispatch.cpp`,
`src/editor/agent_handlers_constraints.cpp`,
`src/samples/agent_dispatch_smoke.cpp`.

#### TDD step (RED)

In `src/samples/agent_dispatch_smoke.cpp`: change `:38` to
`std::array<OperationExpectation, 64>` and add, after `:89`:

```cpp
{"constraint.rename", "edit", true, false, true},
{"constraint.delete", "edit", true, false, true},
```

Then add the cases of spec §12.3:

- `constraint.rename` dry run → live → `constraints.list` read-back → `undo` →
  read-back, over `player_idle`'s `editor_transform_follow`;
- `constraint.delete` on `editor_ribbon_secondary`, asserting the **surviving**
  three constraints **by name** from `constraints.list`;
- a dry run leaves `constraints.list` byte-identical;
- **dry-run/live message equality** on the same rejection;
- `family` validation: `"bone"`, missing, and a right-name/wrong-family pair;
- a rejected `constraint.rename` followed by `undo` must undo the *previous*
  edit;
- `scene_delta` carries `ownership`, `skins`, `skin_reference_count`,
  `used_operation`, `changed_upsert`, and the dry-run payload equals the live
  payload apart from `"dry_run"`.

`./build/marrow_agent_dispatch_smoke` must fail on the registry size and on
`unknown_operation`. **Record both.**

#### Implementation step (GREEN)

Two rows in `kOperationSpecs`, immediately after `:79`:

```cpp
{"constraint.rename", "edit", true, false, true, true, &handle_constraint_operation},
{"constraint.delete", "edit", true, false, true, true, &handle_constraint_operation},
```

Two branches in `handle_constraint_operation`
(`agent_handlers_constraints.cpp:663-812`), before the final
`unknown_operation` fallthrough. Shared shape:

```cpp
// parse args; family via parse_constraint_family(); from/to or name
if (bool_arg(args, "dry_run")) {
    ProjectData candidate = *session.project();
    const ConstraintLifecycleResult r = /* rename_constraint | delete_constraint */(
        &candidate, *session.base_skeleton_document(), family, source, destination);
    if (!r.ok) return make_error(r.message, op, spec, code_for(r));
    return make_success(validated_message, op, spec,
                        catalog_delta(/*dry_run=*/true, family, source, destination,
                                      r, affected_skins(*session.runtime_data(), family, source)));
}
const ConstraintCatalogResult result = apply_constraint_catalog_edit(
    session, edit, /*selection=*/nullptr,
    {EditKind::EditProperty, label + " via Agent", "constraint-catalog", false,
     EditImpact::Project | EditImpact::Runtime | EditImpact::Preview});
if (!result.ok) return make_error(result.message, op, spec, code_for(result));
return make_success(success_message, op, spec,
                    catalog_delta(/*dry_run=*/false, ..., result));
```

`code_for` maps a "does not exist" message to `"not_found"`, matching the four
existing constraint handlers (`agent_handlers_constraints.cpp:619-621`).

`catalog_delta()` builds spec §8.4's object once, so the dry-run and live
payloads cannot diverge — that is what makes the equality assertion meaningful
rather than tautological.

#### Verification

```bash
cmake --build build
./build/marrow_agent_dispatch_smoke
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2   # Task 3's guard now passes
```

**INVERTED GATE 7.** Replace the dry run's `ProjectData candidate = …` +
primitive call with a hand-written `constraint_exists`-style check. The
dry-run/live **message equality** assertion must fail. Revert.

**INVERTED GATE 8.** Remove the `tx.cancel()` on a primitive rejection in
`constraint_catalog.cpp`. The "rejected rename then undo undoes the previous
edit" case must fail. Revert.

---

### Task 5 — Base-backed, skin, shadowing, and rejection scenarios

**Files:** `src/samples/editor_project_smoke.cpp`.

#### TDD step (RED)

Scenarios **B**, **D**, **E** of spec §12.1, reusing MAR-177's fixture helpers
(`editor_project_smoke.cpp:9104-9155`) rather than writing new ones:

- **B.** Over `skin_inherit_constraints.mskl`: rename `cape_pull` → `cape_drag`
  (`used_operation`, `!changed_upsert`, `ownership == "base"`,
  `affected_skins == ["cape"]`); delete `cape_pull` (no root `transform` key,
  no `skins.cape.transform` key, `cape_target` still in `skins.cape.bones`);
  the **shadowing** case (upsert `cape_pull` through the project vector, then
  delete → `used_operation && changed_upsert`, `ownership == "shadowed"`, and no
  `cape_pull` anywhere in the materialized document).
  **Every delete branch here ends with `session.save()` to a temp path followed
  by `load_project()` of that path, asserting the RELOAD succeeds** — spec
  §10.5. The save's return code is not the assertion; `validate_project_for_save`
  has no base document and cannot see a dangling skin reference, so only the
  reload's `build_project_runtime()` + `load_skeleton_data` can.
- **D.** Six rejections, each capturing `serialize_project()` and
  `undo_count()` before and asserting **string identity** after: taken name;
  same name; empty name; missing source (rename); missing source (delete);
  right name in the wrong family — with that last one asserting the message
  names the family rather than saying "not found".
- **E.** The atlas gate and its message (spec §4.1, §4.2): a project with empty
  `atlas_paths` refuses a rename; the result's message contains
  `"at least one atlas path is required"` **verbatim**; `serialize_project()`
  and `undo_count()` are unchanged. The command layer does **not** prefix the
  message — that is the shell's job (Task 3 case 8) — so this scenario asserts
  the raw sentence and the shell smoke asserts the wrapped one.

#### Verification

```bash
cmake --build build
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

**INVERTED GATE 9.** In scenario B's shadowing case, drop the tombstone (make
the primitive path erase only the upsert — by temporarily passing a
project-only-looking family key). The "no `cape_pull` in the materialized
document" assertion must fail, because the base constraint resurrects. Revert.

---

### Task 6 — The 62 → 64 sweep, counted

**Files:** `src/editor/shell_smoke_graph.cpp`,
`src/editor/shell_smoke_timeline.cpp`, `tools/mcp/tools/editing.py`,
`tools/mcp/test_client.py`, and the nine prose sites.

Task 4 already moved `agent_dispatch.cpp` and `agent_dispatch_smoke.cpp`, and
Task 3 already wrote `64U` into the new shell smoke. This task moves everything
else, **atomically and with a count assertion**, so a miscount aborts instead of
half-landing.

#### The counting patch

```bash
cd /Users/kwon/Workspace/C/Maroow
python3 - <<'PY'
import re, sys, pathlib

CODE = {
    'src/editor/shell_smoke_graph.cpp':    7,
    'src/editor/shell_smoke_timeline.cpp': 1,
}
before = 0
for path, expect in CODE.items():
    text = pathlib.Path(path).read_text()
    n_cmp = text.count('!= 62U')
    n_msg = text.count('exact 62-operation registry')
    if n_cmp != expect or n_msg != expect:
        sys.exit(f'ABORT {path}: expected {expect}/{expect}, measured {n_cmp}/{n_msg}')
    before += n_cmp + n_msg

py = pathlib.Path('tools/mcp/test_client.py').read_text()
if py.count('== 62') != 2:
    sys.exit(f'ABORT test_client.py: expected 2 "== 62", measured {py.count("== 62")}')

for path, expect in CODE.items():
    p = pathlib.Path(path)
    t = p.read_text()
    t = t.replace('!= 62U', '!= 64U').replace('exact 62-operation registry',
                                              'exact 64-operation registry')
    if t.count('!= 64U') != expect or t.count('exact 64-operation registry') != expect:
        sys.exit(f'ABORT {path}: post-count mismatch')
    p.write_text(t)

p = pathlib.Path('tools/mcp/test_client.py')
t = p.read_text().replace('== 62', '== 64')
if t.count('== 64') != 2:
    sys.exit('ABORT test_client.py: post-count mismatch')
p.write_text(t)
print('code sites moved:', before, '-> 62->64 done')
PY

# Nothing else in the tree may still demand 62 as a live count.
grep -rn '62U\|== 62\|OperationExpectation, 62' src tools | grep -v '\.o'
#    expect: no output
```

The forbidden literals are safe here by construction: the patch matches only
`!= 62U`, `exact 62-operation registry` and `== 62`, none of which appears in
`IM_COL32(56, 61, 69, 255)`, `(51, 56, 64)`, `"x": 56.0`, `56,995,840`,
`PhysicsBoneState … 56 bytes/bone`, `IM_COL32(208,134,57,230)` or
`rgb(54,57,64)`. **Never run a bare search-and-replace on `64`** — two of those
literals contain it.

#### MCP tools

In `tools/mcp/tools/editing.py`, add the two `types.Tool` entries of spec §8.2
beside the existing constraint tools. This is the change that makes
`assert len(mcp_names) == 64` true; the assertion alone does not.

In `tools/mcp/test_client.py`, add both names to `new_edit_operations`
(`:33-44`), add explicit registry-metadata rows in the style of the
`timeline.set_interpolation` assertion, and add the E2E leg of spec §12.4:
dry run → live → `constraints.list` read-back → `undo` → read-back →
`export_runtime` → assert the exported skeleton no longer names the old
constraint.

#### Prose

Move the nine live sites (spec §8.6 rows 11–19). Leave every historical site
untouched: `AGENTS.md:274`, `:334`, `:336`, `:389`, `:412`, `:442`, `:444`,
`:659`. Specifically:

- `AGENTS.md:161` — 64, naming constraint rename/delete.
- `editing-gap-analysis.md:85` — `64개 오퍼레이션(조회 12, 검증 3, 관리 10, 편집 39)`.
  **The category breakdown moves with the total.**
- `editing-gap-analysis.md:95` — rewrite: MAR-177 shipped the model layer,
  MAR-178 ships the GUI affordances, the undoable command, the selection
  cascade and the two agent/MCP operations, so the registry is now exactly 64.
- `refector.md:20` and `:112` — extend the progression and the **current** total.
- `docs/root1/discription.md` — a new MAR-178 paragraph.

Add a MAR-178 checkpoint section to `AGENTS.md` in the shape of MAR-177's,
including the export figures actually observed and the inverted gates actually
demonstrated.

#### Verification

```bash
cmake --build build
./build/marrow_agent_dispatch_smoke
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py \
  tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only
```

**INVERTED GATE 10.** Delete one of the two new `types.Tool` entries from
`editing.py`. `assert len(mcp_names) == 64` must fail. Restore. (MAR-176
verified this by hand once; do the same and record it.)

---

### Task 7 — Export, save/reload, and closure

**Files:** `src/samples/editor_project_smoke.cpp` (scenario F), docs.

#### TDD step (RED)

Scenario **F**, spec §11, over `skin_inherit_constraints.mskl` through the
**command** path (not the primitive path — that is MAR-177's territory):

```
rename cape_pull -> cape_drag, then EditorSession::export_runtime:
  1. exported .mskl: 0 occurrences of "cape_pull", exactly 2 of "cape_drag"
  2. load_skeleton_document() + typed parse of the export SUCCEEDS
  3. reloaded skin "cape" resolves one transform-constraint index, naming cape_drag

delete cape_pull, then export:
  4. no root "transform" key and no "transform" key inside skins.cape
  5. the export RELOADS; transform_constraints() empty; skins.cape still has cape_target
  6. 0 occurrences of "cape_pull"
```

Assertion messages carry their derivation: *"the rename must rewrite both the
root array element and the skin reference; a root-only rewrite gives 1
occurrence and would fail the reload at
`skeleton_parse.cpp:5115-5123`."*

#### Verification

```bash
cmake --build build
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
  --export-runtime /tmp/marrow_mar178.mskl --export-binary /tmp/marrow_mar178.mbin
./build/marrow_inspect --compare /tmp/marrow_mar178.mbin /tmp/marrow_mar178.mskl
```

**INVERTED GATE 11.** In `constraint_catalog.cpp`, bypass the primitive and
rewrite only `tx.project()->transform_constraint_edits[i].name`. Assertion (1)
must report 1 occurrence and assertion (2) must fail the reload with
`$.skins.cape.transform[0]: skin references unknown transform constraint
'cape_pull'`. **Record that exact message.** Revert.

#### Closure

```bash
# MAR-177's files are untouched.
git diff --stat -- src/editor/project.cpp include/marrow/editor/project.hpp \
  src/editor/session.cpp include/marrow/editor/session.hpp \
  src/editor/selection.cpp include/marrow/editor/selection.hpp \
  docs/root1/format-spec.md
#    expect: no output

# No forbidden boundary moved.
git diff --stat -- src/runtime include/marrow/runtime include/marrow/marrow_c.h \
  src/c_api src/editor/preferences.cpp include/marrow/editor/preferences.hpp
#    expect: no output

# The protected literals are untouched.
grep -rn 'IM_COL32(56, 61, 69, 255)\|(51, 56, 64)\|"x": 56.0\|56,995,840\|56 bytes/bone\|IM_COL32(208,134,57,230)\|rgb(54,57,64)' \
  src include docs tools | wc -l    # compare against the Task 0 baseline count
```

Set `MAR-178` `status: "done"` and `completedAt: "2026-08-30"` in
`.agents/tasks/prd-marrow-runtime.json`.

---

## Full Verification Checklist

### Build

- [ ] `cmake -S . -B build && cmake --build build` — clean, no new warnings
- [ ] `ctest --test-dir build --output-on-failure` — all tests pass, count
      unchanged from Task 0's baseline

### The command (AC2, AC4)

- [ ] One `EditorSession` transaction per accepted rename and per accepted
      delete; `undo_count()` +1 each
- [ ] `allow_merge == false`; group `"constraint-catalog"`
- [ ] A rejection cancels the transaction: `serialize_project()` is the
      **identical string** and `undo_count()` is unchanged
- [ ] A cancelled modal begins no transaction at all
- [ ] `affected_skins` is captured pre-mutation and reports `["cape"]` for
      `cape_pull`, `[]` for every `player_idle` constraint
- [ ] `selection` cascade runs only after a successful commit
- [ ] `selection == nullptr` is accepted and reports `selection_changed == false`

### Ownership (AC3)

- [ ] project-only rename/delete → `!used_operation && changed_upsert`,
      `ownership == "project"`, `constraint_lifecycle_operations` stays empty
- [ ] base-backed → `used_operation && !changed_upsert`, `ownership == "base"`
- [ ] shadowing → **both** flags, `ownership == "shadowed"`, and the base does
      **not** resurrect

### Selection and focus (AC2)

- [ ] Rename: the selected constraint follows; `items().size()` unchanged;
      active unchanged; co-selected bone untouched
- [ ] Delete: the member is pruned; active becomes the **last** survivor;
      order preserved
- [ ] Deleting an unselected constraint reports `selection_changed == false`
      and leaves the set byte-equal
- [ ] A reload after a delete produces the **same** active member the `remap`
      chose
- [ ] Undo/redo do not restore the selection, and leave no ghost after
      `reconcile_constraint_selection()`
- [ ] `reconcile_constraint_selection()` leaves bone/slot/attachment selections
      alone

### GUI (AC1, AC4)

- [ ] `Rename... / Delete...` present in all four family branches, disabled with
      no selection and while a gesture or transaction is active
- [ ] Rename modal seeds with the current name, disables Apply on empty /
      unchanged / taken, and shows the collision reason
- [ ] Delete modal confirms, shows the skin-reference count, and says
      "This action can be undone."
- [ ] Both modals stay open on a rejection and show the primitive's message
- [ ] No last-constraint gate: deleting the last constraint of a family succeeds
- [ ] `unique_constraint_name()` is **not** called on the rename path

### Agent and MCP (AC5)

- [ ] Registry is exactly **64**; category split is inspection 12, validation 3,
      management 10, **edit 39**
- [ ] `constraint.rename` and `constraint.delete` are `(edit, mutating, not
      review, dry-run supported, requires project)` and sit immediately after
      the four `edit_*_constraint` rows
- [ ] Dry run copies the project and runs the **same primitive**; its message on
      a rejection is byte-identical to the live call's
- [ ] Dry run changes nothing: `constraints.list` byte-identical before/after,
      `undo_count()` unchanged
- [ ] `scene_delta` carries `ownership`, `skins`, `skin_reference_count`,
      `used_operation`, `changed_upsert`; the dry-run payload differs from the
      live payload only by `"dry_run"`
- [ ] `family` validation rejects an unknown family, a missing family, and a
      right-name/wrong-family pair, with distinct messages
- [ ] `undo` reverses exactly one lifecycle edit
- [ ] `marrow_agent_dispatch_smoke` → `PASSED` against the exact 64-operation
      registry, with the `[ OK ]` case count recorded
- [ ] `tools/mcp/test_client.py` → `PASSED` with **64/64** exact C++/Python name
      parity; `--parameter-only` → `PASSED`; `py_compile` clean on all four MCP
      files

### Save → reload, the unopenable-project gate (AC6)

- [ ] Every delete branch in the project smoke that can reach the FILE ends with
      `session.save()` then `load_project()`, and asserts the **reload** succeeded.
      Per design §10.5's carve-out, a branch that can only reach the runtime
      asserts on `build_project_runtime()` succeeding instead — that is the same
      code the reload runs (`Impl::commit()` and `load_project()` both call it),
      so do NOT add reloads to those branches to satisfy a broader reading
- [ ] The shell smoke deletes, `save_project_file()`, `reload_project()`, and
      asserts the reload succeeded with no transform constraints and no
      `skins.cape` transform indices
- [ ] No delete test anywhere treats a successful `save` as the assertion

### The atlas gate and its message (§4)

- [ ] The gate is unchanged: `validate_project_for_save()` still runs as step 5
      of both primitives, and `constraint_catalog.cpp` adds no narrower check
- [ ] A rename on an atlas-free project is refused, and
      `serialize_project()` / `undo_count()` are unchanged
- [ ] The command's message carries `"at least one atlas path is required"`
      **verbatim**
- [ ] The GUI's rendered error puts the surface clause in front of that
      sentence, and the sentence survives intact
- [ ] The agent's error code is `"invalid_project"`, not `"not_found"`

### Export and round trip (AC6)

- [ ] Rename export: 0 × `cape_pull`, exactly 2 × `cape_drag`, and the export
      **reloads**
- [ ] Reloaded skin `cape` resolves one transform-constraint index naming
      `cape_drag`
- [ ] Delete export: no root `transform` key, no `skins.cape.transform` key,
      0 × `cape_pull`, and the export **reloads**
- [ ] Reloaded `transform_constraints()` is empty and `skins.cape` still carries
      `cape_target`
- [ ] `marrow_inspect --compare` matches on the exported pair

### Regression

- [ ] `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` → passed
- [ ] `./build/marrow_project_smoke --create /tmp/player_idle.marrow` → passed
- [ ] `./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl
      assets/fixtures/player_idle.matl` → passed
- [ ] `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow
      --auto-close 2` → passed, including the new constraint lifecycle scenario
- [ ] `./build/marrow_editor_shell --project
      assets/fixtures/parameter_face_basic.marrow --auto-close 2` → passed
- [ ] `~/Library/Application Support/Marrow` still does not exist

### Compatibility

- [ ] Zero-byte diff on `src/editor/project.cpp`,
      `include/marrow/editor/project.hpp`, `src/editor/session.cpp`,
      `include/marrow/editor/session.hpp`, `src/editor/selection.cpp`,
      `include/marrow/editor/selection.hpp`
- [ ] Zero-byte diff on `src/runtime/**`, `include/marrow/runtime/**`,
      `include/marrow/marrow_c.h`, `src/c_api/**`,
      `src/editor/preferences.cpp`, `include/marrow/editor/preferences.hpp`
- [ ] Zero-byte diff on `docs/root1/format-spec.md` — `.marrow` gains no field
- [ ] `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1 unchanged
- [ ] `ProjectData` gains no member
- [ ] No new tunable numeric constant anywhere in the diff

### Inverted gates demonstrated and reverted

- [ ] 1 — `affected_skins` captured post-commit → empty for `cape_pull`
- [ ] 2 — selection cascade before commit → rejection leaves the selection moved
- [ ] 3 — widened reconcile → co-selected bone dropped on undo
- [ ] 4 — no rename `remap` → active constraint name wrong
- [ ] 5 — no delete `remap` → `items().size()` wrong
- [ ] 6 — cancel falls through to confirm → `undo_count()` grows
- [ ] 7 — hand-written dry-run check → dry-run/live message equality fails
- [ ] 8 — no `tx.cancel()` on rejection → undo undoes the wrong edit
- [ ] 9 — shadowing without the tombstone → base constraint resurrects
- [ ] 10 — one MCP `types.Tool` removed → `len(mcp_names) == 64` fails
- [ ] 11 — skin rewrite bypassed → export reload fails with
      `$.skins.cape.transform[0]: skin references unknown transform constraint
      'cape_pull'`

### Documentation

- [ ] Nine live prose sites at 64, including `editing-gap-analysis.md:85`'s
      `편집 37 → 39` and both `refector.md` sites
- [ ] Eight historical `AGENTS.md` sites unchanged
- [ ] `editing-gap-analysis.md:95` rewritten for MAR-178
- [ ] New MAR-178 paragraph in `docs/root1/discription.md`
- [ ] New MAR-178 checkpoint section in `AGENTS.md` with observed figures
- [ ] `.agents/tasks/prd-marrow-runtime.json` → `MAR-178` done

---

## Decisions Taken Under Ambiguity

Carried from spec §14. Each is measured in Task 0; if a measurement disagrees,
**stop and correct the spec before writing code**.

| # | Claim | Where it is gated |
| --- | --- | --- |
| 1 | Registry 62, edit 37 → 64, edit 39 | Task 0 (1); Task 6's counting patch aborts on a mismatch |
| 2 | 7+1 comparisons and 7+1 messages at the listed lines | Task 0 (2); Task 6 aborts |
| 3 | Nine live prose sites, eight historical | Task 0 (3) re-greps and re-classifies |
| 4 | `load_project()` rejects an empty `$.runtime.atlases` — the whole basis for keeping the blunt atlas gate. **Confirmed by the MAR-177 review; the decision is closed** | Task 0 (4); Task 5 scenario E asserts the raw refusal, Task 3 case 8 the wrapped one |
| 4b | `load_project(path)` calls `build_project_runtime()`, so a reload is a full materialization plus `load_skeleton_data`. **Confirmed by the MAR-177 review** | Task 0 (4b). **If `:7495` is gone, every delete assertion in Tasks 5 and 7 stops proving anything — stop and redesign them** |
| 4c | `parse_animations` names no constraint, so skins are the complete referrer set. **Confirmed by the MAR-177 review** | Task 0 (4c). A constraint family key appearing there means §5.4's preview is incomplete — reopen it before Task 1 |
| 5 | `prune` and `remap` agree on the last-survivor active fallback | Task 0 (5); Task 3 case 4 asserts the agreement after a reload |
| 6 | `EditorHistorySnapshot` has no `SelectionSet` | Task 0 (6) |
| 7 | `rebuild_project_runtime()` does not reconcile the selection | Task 0 (7). **If it does, §6.4's helper is redundant — revisit before Task 2.** |
| 8 | `cape_pull` occurs exactly 2× in the exported document | Task 0 (9) counts the fixture; Task 7 counts the **export**. A mismatch means re-derive from the export, never edit the constant |
| 9 | `SkinData::<family>_constraint_indices` is the right summary source | Task 1 compiles; Task 5 asserts `["cape"]` |
| 10 | The new header is includable from both surfaces with no cycle | Tasks 3 and 4 compile |
| 11 | Two `CMakeLists.txt` source lines suffice | Tasks 1 and 3 build |
| 12 | `player_idle`'s four constraint names | Task 0 (9) |

Items 2, 3 and 8 are the most likely to be wrong, and each fails usefully: 2 and
3 abort the sweep, and 8 carries its derivation so a mismatch names the premise
that broke.
