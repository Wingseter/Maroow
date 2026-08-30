# MAR-178 Constraint Rename and Delete Surfaces Design

- Story: `MAR-178`, "Expose constraint rename and delete surfaces"
- `dependsOn`: `["MAR-177"]`
- Date: 2026-08-30
- Branch: `feat/mar-168`

---

## 0. Correcting the premise before designing anything

Every story in this chain has found errors in its governing brief. MAR-176 found
seven, MAR-177 found six. This one found four, and two of them change what gets
built.

**0.1 — The commissioning brief says MAR-177 "deliberately left the registry at
62; MAR-178's AC5 is what adds operations". That is right, and the count is
right, but the brief's site list is incomplete in a way that would ship a
half-renumbered tree.**

The brief names `agent_dispatch_smoke.cpp:39`, seven guards in
`shell_smoke_graph.cpp`, one in `shell_smoke_timeline.cpp`, two in
`tools/mcp/test_client.py`, and prose in `AGENTS.md` and `docs/root1/*.md`. Two
things are missing from that list:

- The **`!= 62U` comparison** sits one line *above* each of the eight guard
  messages (`shell_smoke_graph.cpp:148`, `:636`, `:1558`, `:2128`, `:3142`,
  `:3957`, `:4603`; `shell_smoke_timeline.cpp:3697`). The brief's line numbers
  are the `std::cerr` strings. Editing only the string leaves a smoke that
  demands 62 and prints "requires the exact 64-operation registry" — a guard
  that fails for a reason it misreports.
- **`tools/mcp/tools/editing.py`** is not in the brief's list at all, and it is
  the file that makes `assert len(mcp_names) == 64` true. `test_client.py`'s two
  assertions are the *check*; `editing.py`'s two new `types.Tool` entries are the
  *change*. MAR-176 recorded this exact hazard (`AGENTS.md:444`): deleting a
  `types.Tool` makes the client fail on the count.

§8.6 lists every site, measured.

**0.2 — `refector.md:20` is a live "current surface" sentence, not a historical
one, and the brief names only `:112`.**

`refector.md:20` ends "… and MAR-176's `mesh.generate_weights` raises the
**current** registry to the exact 62-operation total recorded below". MAR-177's
own closure note records that MAR-176's spec made the mirror-image error and had
to be corrected: *"the spec's list of `61 → 62` sites (§12 named only `:20`).
Updated"* (`AGENTS.md:412`). Both `:20` and `:112` carry the current total and
both must move to 64. Applying the brief's prose rule literally — *a sentence
stating what the surface IS gets the new number* — `:20` qualifies on the word
"current", despite opening with a historical progression.

**0.3 — AC2 says the command cascades "SelectionSet active/selected identities",
but no layer that both surfaces share owns a `SelectionSet`.**

`SelectionSet` lives in `ShellState` (`shell_state.hpp`), never in
`EditorSession`. `AgentCommandContext` is exactly
`{EditorSession& session; AgentControlState& control;}`
(`agent_dispatch.hpp:15-18`) — the agent surface has no selection to cascade and
cannot acquire one without a layering inversion. The honest reading, adopted
here: the command takes a **nullable `SelectionSet*`**; the GUI passes
`&state->selection`, the agent passes `nullptr`. §5.2 states the signature and
§6 states the semantics. This is *not* MAR-172's failure shape (a spec
requirement silently narrowed to the GUI) — the cascade requirement is honoured
by every caller that *has* a selection, and the agent's absence of one is a
measured structural fact, recorded here rather than discovered later.

**0.4 — The brief's open question about the atlas-free gate is closed. Keep the
gate; MAR-178 owns the message, not the validation.**

The MAR-177 review resolved this independently and reached the same conclusion
this document does: `load_project()` rejects `$.runtime.atlases` when the array
is empty, with `"array must not be empty"` (`project.cpp:290-295`), so no
project on disk can reach that state; in the transient in-memory state where it
*can* arise, `save_project()` already refuses for the same reason, so the rename
**reports a blocker the user already has rather than creating one**. Dropping
step 5 would be strictly worse — a primitive returning `ok` on a project
`save_project()` then refuses is precisely the MAR-175 failure shape this chain
already paid to fix, and MAR-177's scenario B6 pins the current behaviour
deliberately.

What remains for MAR-178 is therefore **message quality only**: when the
interactive path surfaces the refusal, the user must understand it is about the
missing atlas, not about the rename. §4.2 designs that surfacing. The validation
is not weakened anywhere.

---

## 1. Goal

Give a human and an agent the two verbs MAR-177 taught `.marrow` to speak.
MAR-177 built `rename_constraint()` and `delete_constraint()` and proved they
cannot leave an unsavable or unopenable project. Nothing calls them. MAR-178
wires them to a confirmation UI, to one undoable transaction each, and to two
agent/MCP operations — and decides what a rename or a delete does to a live
selection, which MAR-177 explicitly deferred.

MAR-177 was the model layer. MAR-178 is the surface layer. It adds **no** new
`.marrow` field, **no** new runtime behaviour, and **no** new numeric constant.

---

## 2. What the tree already provides, measured

### 2.1 The model layer, as built by MAR-177 (`0cfd90b`)

```cpp
// include/marrow/editor/project.hpp:952-957
struct ConstraintLifecycleResult {
    bool ok{false};
    std::string message;          ///< Empty on success.
    bool used_operation{false};   ///< True when an ordered record was appended.
    bool changed_upsert{false};   ///< True when a `*_constraint_edits` entry was rewritten or erased.
};

// :979-984, :1002-1006
ConstraintLifecycleResult rename_constraint(
    ProjectData*, const runtime::json::Document& base, ConstraintKind, std::string_view from, std::string_view to);
ConstraintLifecycleResult delete_constraint(
    ProjectData*, const runtime::json::Document& base, ConstraintKind, std::string_view name);
```

Both funnel into `apply_constraint_lifecycle_request()`
(`project.cpp:7586-7692`), which is preflight-then-mutate over a
`ProjectData candidate` copy and returns the ownership outcome in
`used_operation` / `changed_upsert`. Its steps, as built:

1. Replay the project's existing records over a copy of the base document
   (`project.cpp:7608-7611`) — so "what names exist" has exactly one definition
   and it is the same one materialization uses.
2. Resolve `(family, source)` against that replayed base **plus** the family's
   upserts (`:7612-7623`).
3. Reject: empty source; not found; `to == from`; `to` already live in the
   family (`:7626-7643`). **A collision is refused, never auto-suffixed**
   (`:7632-7635`, in a comment that names `unique_constraint_name()` as the
   create path's auto-suffixer and declines to be it).
4. Apply the ownership rule: base-backed → append a record; project-only →
   rewrite/erase the upsert; shadowing → **both** (`:7648-7676`).
5. `validate_project_for_save(candidate, &save_error)` **and**
   `validate_constraint_lifecycle_operations(candidate, base)` must both pass
   before `*project = std::move(candidate)` (`:7678-7691`).

**Facts MAR-177 established that MAR-178 must go through, never around:**

- A delete that fails to prune `skins[*].<family>` yields a project that still
  **saves** and can never be **opened** (`skeleton_parse.cpp:5115-5123`, per
  family at `:5169-5207`). The pruning lives in
  `apply_constraint_lifecycle_operations()` (`project.cpp:4922`), reached only
  through the two primitives and through materialization.
- Emptying a family array is fatal (`skeleton_parse.cpp:2985-2991`); the last
  delete in a family erases the key rather than leaving `[]`.
- Identity is `(family, name)`, never an index.
- Rename preserves array position; delete preserves survivor order.
- **`resolve_skin_scopes` is the only constraint-name referrer in the tree.**
  The MAR-177 review confirmed this by reading `parse_animations`
  (`skeleton_parse.cpp:5214-5500`) rather than by grep: the only member keys it
  reads are `animations`, `bones`, `slots`, `attachment`, `color`, `deform`,
  `drawOrder`, `events`, `inherit`, `rotate`, `scale`, `shear`, `translate` —
  there is no constraint-mix timeline family, so a delete cannot orphan a
  keyframe. Constraints never reference other constraints either. **Skins are
  the complete referential-integrity surface for a delete**, and MAR-178's
  affected-reference preview (§5.4) is therefore exhaustive, not a best effort.

MAR-178 adds nothing to this list and subtracts nothing from it.

### 2.2 The GUI surface today

`draw_constraints_window()` (`shell_constraints.cpp:623-1632`) is one
`ImGui::Begin(kConstraintsWindowTitle)` … `ImGui::End()` containing a
four-segment tab strip and four **structurally identical** family branches:

| Family | `constraints_tab` | `Add …` button | `EndChild()` | `selected_name` | `if (selected_name.empty())` |
| --- | --- | --- | --- | --- | --- |
| IK | `== 0`, `:696` | `:697` | `:730` | `:732` | `:740` |
| Path | `== 1`, `:858` | `:859` | `:897` | `:899` | `:907` |
| Transform | `== 2`, `:1110` | `:1111` | `:1149` | `:1151` | `:1159` |
| Physics | `== 3`, `:1389` | `:1390` | `:1428` | `:1430` | `:1438` |

Each list iterates the **materialized** `skeleton.<family>_constraints()`, tags
each row `[project]` or `[runtime]` by probing `find_*_constraint_edit()`, and
computes `selected_name` as the active `ConstraintSelection`'s name *only if it
still resolves in the skeleton*. There is no `Rename`, `Delete` or `Remove`
button for a constraint anywhere in the file (`:936`/`:955`/`:1192`/`:1216`/
`:1455`/`:1474` add and remove *bones within* a constraint).

`unique_constraint_name()` (`:225-241`) probes `constraint_exists()` (`:185-201`)
against the materialized skeleton and returns `<prefix>_N` for the first free
`N` — it is the **create** path's name allocator.

### 2.3 There are two transaction seams, and only one is right for this

| Seam | Where | Shape |
| --- | --- | --- |
| A: mutate-then-record | `apply_project_command_change()` (`shell_preview.cpp:101-141`) | The caller mutates `*project` in place, then hands the *previous* `ProjectData` back; the helper rebuilds, and on failure calls `restore_history_snapshot()`. Every existing constraint widget uses this via `commit_constraint_change` (`shell_constraints.cpp:647-664`). |
| B: session transaction | `EditorSession::begin_edit()` (`session.hpp:297`) → `transaction.project()` → `commit()`/`cancel()` | RAII. Destroying an uncommitted transaction restores the project and preview snapshots. The agent surface uses only this. |

Seam A cannot be shared with the agent: it takes a `ShellState*` and drags in
preview, ImGui-adjacent gesture gating, and shell-only history bookkeeping.
Seam B is UI-free and is what AC2's "one UI-free lifecycle command" must be built
on. §5 builds on B.

### 2.4 The animation catalog is the exact precedent, and it is already shipped

`EditorSession::edit_animation_catalog()` (`session.cpp:2125-2242`,
declared `session.hpp:281-295`) is a UI-free command that:

1. opens one `begin_edit()` transaction,
2. calls a pure authoring primitive (`rename_animation` / `delete_animation`,
   `authoring.hpp:144-153`) on `transaction.project()`,
3. **cancels on rejection** and on `!changed`, returning without a history
   entry (`session.cpp:2171-2181`),
4. computes the transient cascade — which animation the preview should now show,
   whether the queue still resolves — and stores it as
   `pending_preview_state` so it lands in the **same** undo snapshot,
5. `return transaction.commit();`

Its GUI (`shell_project_panels.cpp:32-270`, `:533-608`) is equally directly
reusable:

- `Create... / Duplicate... / Rename...` open one shared name modal
  (`kAnimationNamePopup`, `:88-137`) whose Apply button is disabled on an empty
  name, on an unchanged rename, and while a gesture or transaction is active.
- `Delete...` opens a separate confirmation modal (`kAnimationDeletePopup`,
  `:140-166`) reading *"Delete '%s' and all of its authored timeline edits?"*
  above *"This action can be undone."*
- Both modals surface `state->error_message` inline and **stay open** on a
  rejection.
- `apply_animation_catalog_action()` (`:533-608`) composes the label, calls the
  session command, and on success calls `sync_shell_from_editor_session()` and
  clears the derived per-selection state that the edit invalidated.

Its agent surface is `animation.create` / `animation.duplicate` /
`animation.rename` / `animation.delete` (`agent_dispatch.cpp:56-59`), with
`animation.rename` taking `{from, to}` and `animation.delete` taking `{name}`
(`tools/mcp/tools/editing.py:579-602`), and a dry run that copies the project and
runs the *same* primitive on the copy (`agent_handlers_editing.cpp:344-360`).

**MAR-178 follows this precedent line for line.** Where it deviates, §7.5 and §9
say why.

### 2.5 `SelectionSet` already has exactly the two operations this needs

```cpp
// include/marrow/editor/selection.hpp:122-131
bool prune(const Predicate& predicate);
bool remap(const SelectionItem& from, std::optional<SelectionItem> to);
```

`remap` (`selection.cpp:166-207`) rewrites one exact identity in place, or
removes it for `nullopt`, preserving order; a target collision keeps the
member that appeared first. `prune` (`:137-164`) retains members a predicate
accepts.

The two share their active-index fallback **exactly**: when the active member
disappears, active becomes the last survivor
(`selection.cpp:157-159` and `:197-199`), and both reset it when nothing
survives. This matters in §6.2 — it means a delete's `remap` and a later
`reconcile_selection_to_runtime()` cannot disagree about what is active.

`ConstraintSelection{kind, constraint_name}` (`selection.hpp:68-78`) is one of
the four `SelectionItem` alternatives and compares on both fields, so
`(family, name)` identity is already the selection's identity.

### 2.6 What reconciles the selection today — and what does not

`reconcile_selection_to_runtime()` (`selection.cpp:248-252`, a `prune` over
`selection_item_exists`) is called from exactly two places:

- `reload_project()` (`shell_core.cpp:628-631`) — a full project open/reload;
- `shell_asset_watch.cpp:189` — runtime asset hot reload.

It is **not** called by `rebuild_project_runtime()` (`shell_core.cpp:498-533`),
which is what every ordinary edit goes through, and it is **not** called by
`undo_project_change()` / `redo_project_change()` (`shell_preview.cpp:143-190`),
which call `sync_shell_from_editor_session()` and
`viewport_ffd::reconcile_selection()` and nothing selection-wide.

Two consequences, both load-bearing:

- **A delete leaves a ghost.** The stale `ConstraintSelection` survives until
  the next full reload. `resolve_shell_selection()` filters it
  (`shell_selection.cpp:107-111`, gated on `selection_item_exists`), so no panel
  renders a dead constraint — but `state->selection.items().size()` is what the
  status line reports as *"; N selected"* (`shell_selection.cpp:241`, `:291`,
  `:360`), so the ghost is **user-visible as an inflated count**.
- **A rename would lose the selection outright.** `reconcile` can only prune;
  it has no way to follow a name. Without an explicit `remap`, renaming the
  selected constraint makes `resolve_shell_selection()` return no active
  constraint on the very next frame, and the panel falls back to *"Select an IK
  constraint to edit it."* while the constraint is still right there under its
  new name.

The second is the concrete reason AC2 names the selection cascade at all.

### 2.7 History carries no selection, and must not start to

`EditorHistorySnapshot` (`shell_state.hpp:355-362`) holds the project, its
serialization, `PreviewState`, preview skin names, slot overrides, and a runtime
revision. **No `SelectionSet`.** `history_snapshots_equal()`
(`shell_core.cpp:163-181`) compares only `serialized_project`,
`preview_skin_names`, and the slot overrides.

MAR-174's lesson, recorded in `docs/root1/discription.md:56`: *a field that undo
rewrites but `history_snapshots_equal()` does not compare bounces on Ctrl+Z.*
Adding `SelectionSet` to the snapshot without adding it to the comparator
produces exactly that bounce; adding it to **both** makes a pure selection change
count as a project edit and start creating undo entries. §6.3 takes the third
option.

### 2.8 The agent layer, measured

`handle_constraint_operation()` (`agent_handlers_constraints.cpp:663-812`)
dispatches the four `edit_*_constraint` upserts. It reaches
`session.project()`, `session.runtime_data()`, `session.base_skeleton_document()`
and `session.begin_edit()`, and it already calls across into
`shell::find_named_constraint` — so `agent_*` → `shell::` is an established
direction, but not one worth widening.

`constraints.list` (`agent_dispatch.cpp:834-877`) emits, per constraint,
`{type, name, bones, target|slot|source}` where `type` is exactly
`"ik"|"path"|"transform"|"physics"`. It reports **no** skin references and
**no** ownership. §8.4 puts the affected-reference summary in the new
operations' own `scene_delta` rather than reshaping an inspection op.

`export.preview` (`agent_handlers_inspection.cpp:23-43`) lists target paths
only; it does not render a document. AC5's "export-preview behavior" therefore
means the new operations must leave `export.preview` and `export_runtime`
working over a lifecycle-mutated project — asserted by exporting and
**reloading**, §11.

### 2.9 Measured counts, as built after MAR-177

| Thing | Value | How measured |
| --- | --- | --- |
| Registry operations | **62** | `python3` count of `{"name", "category"` rows in `kOperationSpecs` |
| Category split | inspection 12, validation 3, management 10, **edit 37** | same count, grouped |
| Constraint operations, all `edit_*` | 4 | `agent_dispatch.cpp:76-79` |
| `!= 62U` comparisons in `shell_smoke_graph.cpp` | 7 | `:148`, `:636`, `:1558`, `:2128`, `:3142`, `:3957`, `:4603` |
| Guard **messages** in `shell_smoke_graph.cpp` | 7 | `:149`, `:637`, `:1559`, `:2129`, `:3143`, `:3958`, `:4604` |
| `!= 62U` + message in `shell_smoke_timeline.cpp` | 1 + 1 | `:3697`, `:3698` |
| Hand-edited registry array size | 1 | `agent_dispatch_smoke.cpp:38` — `std::array<OperationExpectation, 62>` |
| MCP parity assertions | 2 | `tools/mcp/test_client.py:51`, `:53` |
| MCP tool definitions | 62 | `tools/mcp/tools/editing.py` + `inspection.py`, checked by `:53` |
| Live prose sites stating the current total | **9** | `AGENTS.md:161`; `editing-gap-analysis.md:23`, `:85`, `:86`, `:95`, `:192`, `:455`; `refector.md:20`, `:112` |

**MAR-178 changes all of these.** §8.6 is the sweep.

### 2.10 Fixtures

```
assets/fixtures/player_idle.marrow                 4 project-only constraints, base has no constraint arrays
                                                   ik editor_arm_reach · path editor_guide_follow
                                                   transform editor_transform_follow · physics editor_ribbon_secondary
                                                   runtime.atlases = ["player_idle.matl"]
assets/fixtures/player_idle.mskl                   no ik/path/transform/physics root arrays at all
assets/fixtures/skin_inherit_constraints.mskl      1 base transform `cape_pull`; skins is an OBJECT;
                                                   skins.cape = {bones:["cape_target"], transform:["cape_pull"]}
                                                   bones root/controller/child/constrained/cape_target
                                                   1 animation `toggle_inherit`; no atlas of its own
assets/fixtures/ik_constraints.mskl                13 base ik, no skins key at all
```

`player_idle` therefore exercises **only** the project-only row of the ownership
table and **cannot** exercise a skin reference. `skin_inherit_constraints` is
still the only fixture in the tree where a skin names a constraint, and it has no
`.marrow`; MAR-177's smoke builds one with `create_minimal_project()` and borrows
`player_idle.matl` because the fixture ships no atlas
(`editor_project_smoke.cpp:9104-9126`, `:9129-9155`). MAR-178 reuses that helper
shape rather than inventing another.

---

## 3. Scope

### 3.1 In scope

1. Rename and confirmed-delete affordances for all four families in
   `shell_constraints.cpp`, with a collision preview and a skin-reference
   preview (AC1).
2. One UI-free lifecycle command, `apply_constraint_catalog_edit()`, that wraps
   one `EditorSession` transaction around MAR-177's primitives and cascades the
   caller's `SelectionSet` (AC2, AC4).
3. Correct ownership behaviour on all three live rows of MAR-177's table,
   inherited by construction because the command calls the primitives (AC3).
4. Two agent operations, `constraint.rename` and `constraint.delete`, with
   metadata, dry run, affected-reference summary, mutation, and undo, plus their
   MCP wrappers and exact 64/64 name parity (AC5).
5. Project, shell, agent, and MCP smoke coverage of all four families, skin
   references, selection remap and prune, collisions, confirmation, rollback,
   save/reload, and export (AC6).

### 3.2 Out of scope, and where each piece goes

| Not here | Where | Why |
| --- | --- | --- |
| IK `softness`/`compress`/`stretch`, physics `step`/`x`/`y`/`rotate`/`scaleX`/`shearX`/`limit`/`massInverse` | **MAR-179** | Its AC1/AC2 name them exactly. MAR-178 renames and deletes constraints; it edits none of their fields. |
| The IK agent/MCP field-parity gap | **MAR-179** AC4 | `handle_constraint_operation` already *materializes* `softness`/`compress`/`stretch` from the runtime (`agent_handlers_constraints.cpp:741-745`) but exposes no argument for them. That is a parameter gap, not a lifecycle gap. |
| Creating constraints | **already shipped** | Four `Add …` buttons and four default builders; MAR-177 §0.1 verified they save. |
| Reordering constraints within a family | Neither | Not lifecycle. Evaluation order is array order and MAR-177 preserved it; MAR-178 exposes no way to change it. |
| Extending `constraints.list` with skin references or ownership | Neither | The summary belongs in the mutating operations' own `scene_delta` (§8.4). Reshaping an inspection op would break `agent_dispatch_smoke`'s existing row assertions for no gain. |
| Persisting selection | Neither | `SelectionSet` is transient by construction (§2.7) and MAR-177 §6 row 3 recorded that no `selection` key exists in `build_project_value`. |
| Per-name debug-overlay flags | Neither | `viewport.debug_overlay.{ik,path,physics}_constraints` are per-**family** booleans (`project.hpp:70-72`). §6.5. |
| A new `.marrow` field | Neither | MAR-177 added `constraint_edits.operations`; `docs/root1/format-spec.md:965-1010` documents it. MAR-178 writes the same records through the same primitives. §3.3. |
| Unknown-key preservation inside `constraint_edits` | Neither | Pre-existing (MAR-177 §2.6); fixing it changes serialization for existing projects. |

### 3.3 Compatibility boundaries

| Boundary | Change |
| --- | --- |
| `.mskl` v1 | none |
| `.mbin` v2 | none |
| C ABI v1 | none |
| `editor-settings.json` v1 | none |
| `.marrow` | **none** — `constraint_edits.operations` already exists (MAR-177), and `docs/root1/format-spec.md` therefore needs no change |
| `ProjectData` | **no new member** |
| Agent registry | 62 → **64** |

Confirmed by reading `docs/root1/format-spec.md:953-1010`: the section already
documents `operations`, the field rules, the ordering argument, the ownership
table, the skin-reference rewrite, and the erase-the-key rule. MAR-178 writes
exactly those records and adds no key. The one documentation file MAR-177 had to
touch is the one MAR-178 does not.

---

## 4. The atlas-free gate: decision, proof, and the one thing MAR-178 owns

### 4.1 Decision: keep MAR-177's blunt gate. Introduce no narrower validation.

MAR-177's implementer flagged that step 5 of both primitives runs
`validate_project_for_save()`, which requires at least one atlas path
(`project.cpp:5511-5514`, `"at least one atlas path is required"`), and called
this *"the chain's rule applied literally, and blunt"*, suggesting MAR-178 might
want a narrower gate once the primitives are wrapped in a transaction. The
MAR-177 review has since closed the question in favour of keeping it (§0.4);
this section records the independent derivation, because the decision is only
as durable as its reasons.

The proof that narrowing buys nothing:

1. **Every interactive `ProjectData` arrives through `load_project()`.** The
   shell's only entry is `reload_project()` (`shell_core.cpp:535`) →
   `EditorSession::open(path)` (`session.cpp:1700`) → `load_project(path)`
   (`project.cpp:7517-7526`). `shell_main.cpp:611` and `:737` are its only
   callers.
2. **`load_project()` already refuses an atlas-free project**, before the shell
   ever sees one:
   ```cpp
   // project.cpp:291-297
   if (atlas_paths->as_array().empty()) {
       return validation_error(document, atlas_paths->location(),
                               "$.runtime.atlases", "array must not be empty");
   }
   ```
   The array is also `require_member(..., Value::Type::Array, ...)`, so absent
   and wrong-typed fail earlier still.
3. **The only atlas-free `ProjectData` objects in the tree are in-memory test
   constructions** from `create_minimal_project()` with empty `atlas_paths`
   (`project.cpp:7308-7335`). Every caller that then wants to save or validate
   already supplies one — MAR-177's own smoke helper borrows
   `player_idle.matl` and says why in a comment
   (`editor_project_smoke.cpp:9118-9123`).
4. **In that transient state the project is already unsavable.**
   `save_project()` refuses it through the same
   `validate_project_for_save()` check. So the rename's refusal **reports a
   blocker the user already has; it does not create one.** That reframing is
   what makes §4.2 the whole of MAR-178's remaining work here.
5. **What narrowing would cost.** MAR-177's step 5 is the structural closure of
   the MAR-175 hole: *no primitive may return success and leave a project
   `save_project()` refuses.* A narrower "constraint-only" validator would have
   to re-derive which of `validate_project_for_save`'s checks are reachable from
   a lifecycle edit — and the honest answer is *all of them*, because a rename
   changes a name that the per-family uniqueness check reads, and a delete can
   empty a family that the upsert cross-check reads. Any narrowing is a claim
   that a subset suffices, and this chain has been bitten twice by exactly that
   kind of claim: MAR-175 left a commit that could not be saved, MAR-177 nearly
   left one that could not be opened.

So the gate stays as MAR-177 built it and MAR-178's command inherits it
unchanged. §12 still makes this a **Task 0 measurement**, not an assumption: the
implementer greps the loader gate and confirms the message text before depending
on it.

### 4.2 The one thing MAR-178 does own: the message

The refusal's text comes from `validate_project_for_save()` and reads
`"at least one atlas path is required"`. Surfaced raw under a *"Rename IK
constraint"* modal, that reads as a non-sequitur — the user asked to rename a
constraint and was told something about atlases, with no indication of which of
the two facts is wrong.

The command therefore **passes the primitive's message through unchanged** (a
surface must never paraphrase a validator, or the two drift), and the *surfaces*
add the missing context around it:

- **GUI.** The modal's inline error line renders
  `Cannot rename: the project must reference at least one atlas before it can
  be saved.` — the primitive's message quoted verbatim after a clause that names
  what was refused and why the two are connected. The modal stays open, and both
  buttons stay enabled, because the user's next action is to fix the project's
  atlas list, not to retype the name.
- **Agent.** `make_error` carries the primitive's message with the code
  `"invalid_project"` rather than `"not_found"` — the distinction matters,
  because a caller retrying a `not_found` will re-send with a different name and
  fail identically forever.

The GUI's added clause is the *only* place in this story where a surface writes
its own words about a validator's verdict, and it is additive: the validator's
own sentence is still present, verbatim, so a grep for the message text still
finds every path that can produce it. §12.1 case E asserts both halves — that
the primitive's exact sentence appears, and that the surface's clause is in
front of it.

---

## 5. The UI-free lifecycle command

### 5.1 Why a free function and not `EditorSession::edit_constraint_catalog`

`edit_animation_catalog` can live on the session because everything it cascades
— `PreviewState` — is session-owned. The constraint analogue cascades
`SelectionSet`, which is not. Putting a `SelectionSet*` out-parameter on an
`EditorSession` method would make the session mutate caller-owned UI state, and
would force `session.hpp` to grow a selection dependency that only one method
uses.

Instead: a new public header and TU beside the session, in namespace
`marrow::editor` (not `marrow::editor::shell`), so both `shell_constraints.cpp`
and `agent_handlers_constraints.cpp` include it as a peer rather than one
reaching into the other's namespace.

- `include/marrow/editor/constraint_catalog.hpp`
- `src/editor/constraint_catalog.cpp`

### 5.2 Signature

```cpp
#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/editor/selection.hpp"
#include "marrow/editor/session.hpp"

namespace marrow::editor {

/** @brief Which lifecycle verb a constraint catalog edit applies. */
enum class ConstraintCatalogEditKind {
    Rename,
    Delete,
};

/** @brief One lifecycle edit, identified by `(family, source)` and never by index. */
struct ConstraintCatalogEdit {
    ConstraintCatalogEditKind kind{ConstraintCatalogEditKind::Rename};
    ConstraintKind family{ConstraintKind::Ik};
    std::string source;       ///< Current name. Required for both verbs.
    std::string destination;  ///< New name for `Rename`; must be empty for `Delete`.
};

/**
 * @brief Outcome of one catalog edit, including everything a surface must report.
 *
 * `affected_skins` is captured from the pre-mutation runtime, because the skin
 * arrays are already rewritten by the time the edit commits.
 */
struct ConstraintCatalogResult {
    bool ok{false};
    bool changed{false};
    std::string message;                      ///< The primitive's message on rejection; empty on success.
    bool used_operation{false};               ///< An ordered `.marrow` record was appended (base-backed).
    bool changed_upsert{false};               ///< A `*_constraint_edits` entry was rewritten or erased.
    std::vector<std::string> affected_skins;  ///< Skins whose `<family>` array named the constraint.
    bool selection_changed{false};            ///< The supplied SelectionSet was remapped or pruned.
};

/**
 * @brief Applies one constraint rename or delete as a single undoable transaction.
 *
 * Wraps MAR-177's `rename_constraint()` / `delete_constraint()` in one
 * `EditorSession` transaction, so the runtime root arrays, every
 * `skins[*].<family>` reference, the project upserts and the ordered
 * `constraint_edits.operations` records all move together or not at all.
 *
 * @param session Open editor session; its project and base document are used.
 * @param edit The verb and its `(family, source)` identity.
 * @param selection Optional transient selection to cascade. Rename follows the
 *        constraint, delete removes it. Pass `nullptr` from surfaces that own
 *        no selection, such as the agent dispatcher.
 * @param descriptor History descriptor; the caller composes the human label.
 * @return The outcome. On any rejection nothing changed: the project's
 *         serialization is byte-identical, the history is unchanged, and
 *         `selection` is untouched.
 */
ConstraintCatalogResult apply_constraint_catalog_edit(
    EditorSession& session,
    const ConstraintCatalogEdit& edit,
    SelectionSet* selection,
    EditDescriptor descriptor);

/** @brief The `.marrow`/wire spelling of a family: `"ik"`, `"path"`, `"transform"`, `"physics"`. */
std::string_view constraint_family_key(ConstraintKind family) noexcept;
/** @brief Parses a wire family spelling; rejects anything else. */
std::optional<ConstraintKind> parse_constraint_family(std::string_view key) noexcept;

} // namespace marrow::editor
```

### 5.3 The five steps

```
1. Guard.      session.has_project() && session.base_skeleton_document() != nullptr,
               and !session.transaction_active(). Reject with a message, no transaction.
2. Summarise.  Capture affected_skins from the PRE-mutation session.runtime_data():
               find the constraint's index in its family, then collect every skin
               whose <family>_constraint_indices contains it. Must happen here —
               after step 4 the arrays no longer name it.
3. Transact.   auto tx = session.begin_edit(descriptor);  // rejects if one is active
               ConstraintLifecycleResult r = edit.kind == Rename
                   ? rename_constraint(tx.project(), *session.base_skeleton_document(),
                                       edit.family, edit.source, edit.destination)
                   : delete_constraint(tx.project(), *session.base_skeleton_document(),
                                       edit.family, edit.source);
               if (!r.ok) { tx.cancel(); return {false, false, r.message, ...}; }
4. Commit.     SessionResult c = tx.commit();
               if (!c) return {false, false, c.error->format(), ...};   // tx destructor cancels
               if (!c.changed) return {false, false, "…did not change the project.", ...};
5. Cascade.    Only now, and only if selection != nullptr:
               Rename: selection->remap(ConstraintSelection{family, source},
                                        ConstraintSelection{family, destination})
               Delete: selection->remap(ConstraintSelection{family, source}, std::nullopt)
```

Ordering is the whole design. **Step 2 before step 3** because the summary is
read from data the edit destroys. **Step 5 after step 4** because a cancelled or
rejected edit must leave the selection exactly as it was — that is AC4's
"cancellation or failure changes nothing", and it is asserted on
`serialize_project()`, on `session.undo_count()`, and on
`selection.items()` (§12), not inferred from a return code.

A rejection at step 3 cancels explicitly rather than relying on the destructor,
matching `edit_animation_catalog` (`session.cpp:2171-2181`), so that the failure
path is visible in the code rather than in RAII.

### 5.4 The affected-reference summary

For a constraint at index `i` in family `F` of the materialized skeleton, the
affected skins are exactly

```cpp
for (const SkinData& skin : skeleton.skins())
    if (contains(skin.<F>_constraint_indices, i)) affected.push_back(skin.name);
```

using `SkinData::{ik,path,transform,physics}_constraint_indices`
(`runtime/skeleton.hpp:285-288`). This is read from live parsed data, not
re-derived from the document, so it cannot disagree with what materialization
will rewrite.

For `skin_inherit_constraints.mskl` this yields `["cape"]` for `cape_pull`; for
every `player_idle` constraint it yields `[]`, and for every
`ik_constraints.mskl` constraint it yields `[]` (that fixture has no `skins`
key at all).

The summary is identical for both verbs — the same skins are rewritten by a
rename and pruned by a delete — so one code path produces it and the surfaces
choose the wording.

### 5.5 The family spelling, and how it is kept from drifting

`constraint_family_json_key()` already exists but is file-local to
`project.cpp:3809`. MAR-178 declares its own `constraint_family_key()` /
`parse_constraint_family()` pair in the new header rather than exporting the
existing one, for one reason: `src/editor/project.cpp` is MAR-177's file and is
under concurrent review, and a header-visibility refactor of it is exactly the
kind of adjacent change that turns a clean review into a merge.

The drift that duplication risks is closed by a **test, not by construction**:
the project smoke asserts, for each of the four families, that
`constraint_family_key(family)` equals the `"family"` string that
`serialize_project()` actually writes for a lifecycle record of that family.
That assertion fails loudly if either spelling ever moves. §12.1 case G.

---

## 6. Selection and focus

### 6.1 Rename follows; delete prunes

| Verb | Cascade | Why |
| --- | --- | --- |
| Rename | `remap({family, from}, {family, to})` | The constraint still exists and the user is still working on it. Without this, `reconcile_selection_to_runtime()` — which can only prune — drops the selection at the next reload, and `resolve_shell_selection()` reports no active constraint on the very next frame (§2.6). |
| Delete | `remap({family, name}, std::nullopt)` | The identity is gone. Leaving it inflates the *"; N selected"* status count (`shell_selection.cpp:241`) until the next full reload. |

Only the *exact* identity moves. An IK constraint named `arm` and a physics
constraint named `arm` are different `SelectionItem`s
(`selection.hpp:68-78` compares both fields), so renaming one never touches the
other — the same `(family, name)` identity rule the model layer enforces.

`remap` returns `false` when the identity is not in the set, which is the common
case (the user deleted a constraint that was not selected). That is not a
failure; `selection_changed` simply reports `false`.

### 6.2 What becomes active after a delete

`SelectionSet::remap` with `to == nullopt` removes the member; if it was the
active one and others survive, active becomes **the last survivor**
(`selection.cpp:195-199`). `prune` does the same (`:155-159`). So a delete's
`remap` and a later `reconcile_selection_to_runtime()` produce the *same* active
member — there is no path on which the two disagree, and the smoke asserts this
by running a reload after a delete and comparing.

If the deleted constraint was the only member, the set becomes empty and
`resolve_shell_selection()` returns no active constraint. The panel then shows
its existing *"Select a … constraint to edit it."* line.

**No neighbour is auto-selected.** The animation catalog does pick a replacement
(`session.cpp:2216-2233`) because the preview must show *some* animation. A
constraint panel has no such requirement; auto-selecting the next constraint
would put a different constraint under a Delete button the user is already
clicking. Stated as a decision because the precedent points the other way.

### 6.3 Undo and redo do not restore the selection

`EditorHistorySnapshot` deliberately carries no `SelectionSet` (§2.7), and
adding one is the MAR-174 bounce in either of its two forms. So:

- **Undo of a rename** restores the old name; the selection still names the new
  one and resolves to nothing.
- **Undo of a delete** restores the constraint; the selection does not come
  back.

This is the documented, tested behaviour, not an accident. It is stated here so
that a future reader does not "fix" it by adding a field to the snapshot.

### 6.4 The narrow reconcile, and why it is narrow

To keep undo from leaving the ghost of §2.6, `undo_project_change()` and
`redo_project_change()` (`shell_preview.cpp:143-190`) each gain **one** call to a
new shell helper:

```cpp
/** @brief Drops constraint selections that no longer resolve; leaves every other kind alone. */
bool reconcile_constraint_selection(ShellState* state);
```

implemented as a `selection.prune` whose predicate returns `true` for every
non-`ConstraintSelection` alternative and, for a `ConstraintSelection`, returns
`selection_item_exists(item, *state->load_result.skeleton_data)`.

The obvious alternative — calling the existing
`reconcile_selection_to_runtime()` from `undo_project_change()` — is rejected: it
would prune bone, slot and attachment selections after **every** undo in the
editor, which is a behaviour change far outside this story, in code four other
milestones depend on. The narrow helper has zero blast radius outside
constraints and is trivially assertable: the smoke selects a bone *and* a
constraint, deletes the constraint, undoes, and asserts the bone is still
selected while the constraint ghost is gone.

### 6.5 Debug overlay flags: no action, in either direction

`viewport.debug_overlay.{ik,path,physics}_constraints` (`project.hpp:70-72`,
parsed `project.cpp:1089-1105`) are **per-family** booleans persisted in
`.marrow`. They name no constraint, so a rename cannot affect them.

A delete does not clear them either, including the delete that empties a family.
Two reasons: the flag means *"draw this family's overlay"*, which stays
meaningful over an empty family (`DebugOverlayStats` simply reports a count of
zero, `shell_state.hpp:319-322`); and clearing it would write a persisted
`.marrow` change into the lifecycle transaction, so undo would restore it and
redo would clear it again — a setting flickering as a side effect of an unrelated
edit. Note there is no `transform_constraints` overlay flag at all, so a rule
that cleared flags would be inconsistent across the four families by
construction.

---

## 7. The GUI affordances

### 7.1 Placement

In each of the four family branches of `draw_constraints_window()`, a two-button
row goes **immediately after** the `selected_name` computation and **before**
`if (selected_name.empty())` — that is, after `:732`, `:899`, `:1151`, `:1430`.
`selected_name` is exactly the right gate: it is non-empty only when the active
selection is of this family *and* still resolves in the materialized skeleton.

```
[Add IK Constraint]
┌─ ik_constraint_list ───────────────┐
│ editor_arm_reach       [project]   │
│ arm_positive           [runtime]   │
└────────────────────────────────────┘
[Rename...] [Delete...]              ← new, disabled unless a constraint of this family is selected
────────────────────────────────────
Name: editor_arm_reach
…
```

Both buttons are additionally disabled while `authoring_gesture_active(*state)`
or `state->session.transaction_active()`, matching the animation catalog's
`edit_blocked` gate (`shell_project_panels.cpp:646-648`) and the existing
refusal in `apply_project_command_change` (`shell_preview.cpp:112-118`).

One shared pair of modals is drawn once, unconditionally, immediately before the
`ImGui::End()` at `:1631` — so they survive a tab switch mid-dialog and are not
duplicated four times. The popup state is one file-static struct carrying the
family, the source name, the `std::array<char, 128>` name buffer, and the
pending delete target, mirroring `g_animation_catalog_popup`
(`shell_project_panels.cpp:42`).

### 7.2 The rename modal

```
Rename IK constraint 'editor_arm_reach'.
Name: [editor_arm_reach_______________]
1 skin references this constraint: cape        ← omitted when the list is empty
                                               ← replaced by the collision line when taken
[Rename]  [Cancel]
```

- `InputText` with `EnterReturnsTrue | AutoSelectAll`, keyboard focus set on
  `IsWindowAppearing()`, seeded with the **current** name.
- Apply is disabled when the candidate is empty, when it equals the source, or
  when `constraint_exists(skeleton, family, candidate)` is true. The third is
  the GUI's *preview* of the primitive's rejection at
  `project.cpp:7636-7641`, not a substitute for it — the primitive still runs and
  is still authoritative, and the smoke asserts that a rejection it produces is
  surfaced verbatim.
- The collision line reads `'<name>' is already taken by another <family>
  constraint.` and replaces the reference line while it is showing.
- `state->error_message` is rendered inline in `theme::kStateErr` and the modal
  **stays open**, exactly as `kAnimationNamePopup` does
  (`shell_project_panels.cpp:113-117`).

### 7.3 The delete modal

```
Delete IK constraint 'editor_arm_reach'?
Also removes it from 1 skin: cape              ← omitted when the list is empty
This action can be undone.
[Delete]  [Cancel]
```

**A delete is confirmed; a rename is not.** The rename modal already requires a
deliberate typed input and a second click, and a rename is reversible by
renaming back. A delete is not: it discards the constraint's authored
parameters, and for a base-backed constraint it writes a tombstone whose only
inverse is undo. It also silently rewrites every skin that named it. That is
strictly more destructive than an animation delete, which already gets a modal
carrying the same *"This action can be undone."* reassurance
(`shell_project_panels.cpp:145-147`) — so MAR-178 uses the same wording rather
than inventing a new register.

### 7.4 Rename does not reuse `unique_constraint_name()`

`unique_constraint_name()` (`shell_constraints.cpp:225-241`) exists to allocate
a *fresh* name for the create path, by probing `<prefix>_1`, `<prefix>_2`, …
until `constraint_exists()` says no. Applying it to a rename would mean that a
user who types a taken name silently gets a *different* name — which is exactly
what MAR-177's primitive refuses to do, in a comment that names this function:

> *"A collision is refused rather than auto-suffixed. `unique_constraint_name()`
> owns auto-suffixing for the create path; a rename is the user's choice of a
> specific name, and quietly giving them a different one is worse than
> declining."* — `project.cpp:7632-7635`

So rename seeds with the current name, validates with `constraint_exists()`, and
never suffixes. The two paths keep their separate jobs.

### 7.5 There is no last-constraint gate

The animation catalog disables Delete when only one animation remains
(`shell_project_panels.cpp:669`, with the explanatory
*"The last animation cannot be deleted."*) because the preview must have an
animation to show.

Constraint families may legitimately be empty — that is precisely why MAR-177
made an emptied family array **erase its key** rather than leave `[]`
(`skeleton_parse.cpp:2985-2991` rejects `[]`). So Delete stays enabled for the
last constraint of a family, and §12 asserts that deleting it succeeds, that the
exported document carries no root `<family>` key and no `skins.*.<family>` key,
and that the export **reloads**.

### 7.6 What a cancel leaves

Nothing. Cancel closes the popup without ever calling the command, so no
transaction is begun, `session.undo_count()` is unchanged, and
`serialize_project()` is byte-identical. The name buffer is re-seeded from the
current name on the next open, so a half-typed abandoned name never reappears.

A *rejection* leaves the same nothing, by §5.3 step 3's explicit
`transaction.cancel()`, with the modal still open and the primitive's message
shown.

---

## 8. The agent and MCP surface

### 8.1 Two operations, not eight

`constraint.rename` and `constraint.delete`, each taking `family` as an
argument.

The alternative — `constraint.rename_ik`, `constraint.rename_path`, … — would be
eight rows. It is rejected because identity is `(family, name)` and the family is
data, not a verb; because `constraints.list` already emits the family as a
`"type"` field with exactly these four spellings
(`agent_dispatch.cpp:839`, `:850`, `:859`, `:872`), so an agent that just listed
constraints has the value in hand; and because the four legacy
`edit_*_constraint` operations are the only per-family rows in the registry and
they predate the dotted convention that everything since MAR-155 uses.

Names follow `animation.rename` / `animation.delete` (`agent_dispatch.cpp:58-59`)
exactly, including the argument spellings.

### 8.2 Registry rows and schemas

Inserted immediately after the four `edit_*_constraint` rows
(`agent_dispatch.cpp:76-79`), so the family's operations stay contiguous — the
placement `mesh.generate_weights` used relative to `mesh.rebind_weights`:

```cpp
{"constraint.rename", "edit", true, false, true, true, &handle_constraint_operation},
{"constraint.delete", "edit", true, false, true, true, &handle_constraint_operation},
```

`(name, category, mutating, requires_review, dry_run_supported, requires_project,
handler)` — `edit`, mutating, **not** review-gated (no existing `edit` row is),
dry-run supported, project required.

MCP schemas, in `tools/mcp/tools/editing.py`, beside the existing constraint
tools:

```python
types.Tool(
    name="constraint.rename",
    description="Rename one constraint and cascade its skin references.",
    inputSchema={
        "type": "object",
        "properties": {
            "family": {"type": "string", "enum": ["ik", "path", "transform", "physics"]},
            "from": {"type": "string", "minLength": 1},
            "to": {"type": "string", "minLength": 1},
            "dry_run": {"type": "boolean"},
        },
        "required": ["family", "from", "to"],
        "additionalProperties": False,
    },
),
types.Tool(
    name="constraint.delete",
    description="Delete one constraint and prune it from every skin that names it.",
    inputSchema={
        "type": "object",
        "properties": {
            "family": {"type": "string", "enum": ["ik", "path", "transform", "physics"]},
            "name": {"type": "string", "minLength": 1},
            "dry_run": {"type": "boolean"},
        },
        "required": ["family", "name"],
        "additionalProperties": False,
    },
),
```

Validation in `handle_constraint_operation`, before any transaction:

| Fault | Response |
| --- | --- |
| missing `args` object | `"constraint.rename requires 'args' object."` |
| missing/non-string `family` | `"constraint.rename requires 'family' string."` |
| `family` not one of the four | `"Unknown constraint family '<x>'; expected ik, path, transform, or physics."` |
| missing/non-string `from`/`to`/`name` | `"constraint.rename requires 'from' string."` etc. |
| constraint not found | the primitive's message, `code: "not_found"` |
| target already taken, empty, or equal to source | the primitive's message |

`family` is parsed with `parse_constraint_family()` (§5.5), so the wire spelling
is the `.marrow` spelling by construction.

### 8.3 The dry run runs the live preflight

`dry_run` copies the project and runs the **same primitive** on the copy, exactly
as the animation catalog does (`agent_handlers_editing.cpp:344-360`):

```cpp
ProjectData candidate = *session.project();
ConstraintLifecycleResult r = /* rename_constraint | delete_constraint */ (
    &candidate, *session.base_skeleton_document(), family, source, destination);
if (!r.ok) return make_error(r.message, op, spec, code_for(r.message));
// success: emit the same scene_delta the live call will emit, plus "dry_run": true
```

This is not a convenience. AGENTS.md records it as the standard MAR-176 was held
to (`:389`): *"Dry runs run the identical preflight a live call runs and change
nothing."* A hand-written existence check would drift from the primitive's
rejection messages, which is why §12.3 asserts **message equality between the
dry run and the live call on the same rejection** rather than merely asserting
that both fail.

The dry run begins no transaction, so `session.undo_count()` and
`serialize_project()` are unchanged — asserted, not assumed.

### 8.4 `scene_delta`

```json
{
  "family": "transform",
  "from": "cape_pull",
  "to": "cape_drag",
  "ownership": "base",
  "used_operation": true,
  "changed_upsert": false,
  "skins": ["cape"],
  "skin_reference_count": 1
}
```

For `constraint.delete`, `from`/`to` are replaced by `"name"`. `dry_run` adds
`"dry_run": true` and is otherwise byte-identical to the live payload — the
property MAR-176 asserted for its three weight operations, and the cheapest way
to prove the two paths ran the same code.

`ownership` is derived from the primitive's two flags, which is the only place in
the tree that knows the answer:

| `used_operation` | `changed_upsert` | `ownership` |
| --- | --- | --- |
| true | false | `"base"` |
| true | true | `"shadowed"` |
| false | true | `"project"` |

The fourth combination is unreachable: a primitive that changed neither could not
have returned `ok`.

`skins` is §5.4's list, captured pre-mutation. It is the "affected-reference
summary" AC5 asks for, and it is the only field that reports the failure mode
this story is actually about.

### 8.5 Undo

Both operations produce exactly one history entry, so the existing `undo`
operation (`agent_dispatch.cpp:49`) reverses one rename or one delete. The MCP
test asserts the full round trip: **dry-run → live → read back via
`constraints.list` → undo → read back**, with the constraint present under its
original name and, for the delete case, present at all. Nothing new is needed on
the undo path.

### 8.6 Registry 62 → 64, and every hand-edited site

| # | File | Site | Change |
| --- | --- | --- | --- |
| 1 | `src/editor/agent_dispatch.cpp` | after `:79` | **+2 rows** in `kOperationSpecs`. This is the source of truth; `agent_operation_descriptor_count()` (`:1143`) derives from `std::size`. |
| 2 | `src/editor/agent_handlers_constraints.cpp` | `handle_constraint_operation`, `:663-812` | two new `if (op == …)` branches |
| 3 | `src/samples/agent_dispatch_smoke.cpp` | `:38` | `std::array<OperationExpectation, 62>` → `64` |
| 4 | `src/samples/agent_dispatch_smoke.cpp` | after `:89` | **+2 expectation rows**, in registry order |
| 5 | `src/editor/shell_smoke_graph.cpp` | `:148`, `:636`, `:1558`, `:2128`, `:3142`, `:3957`, `:4603` | `!= 62U` → `!= 64U` |
| 6 | `src/editor/shell_smoke_graph.cpp` | `:149`, `:637`, `:1559`, `:2129`, `:3143`, `:3958`, `:4604` | message `62-operation` → `64-operation` |
| 7 | `src/editor/shell_smoke_timeline.cpp` | `:3697`, `:3698` | same pair |
| 8 | `tools/mcp/tools/editing.py` | beside the constraint tools | **+2 `types.Tool`** — the change that makes the count true |
| 9 | `tools/mcp/test_client.py` | `:51`, `:53` | `== 62` → `== 64` |
| 10 | `tools/mcp/test_client.py` | `:33-44` | add both names to `new_edit_operations` |
| 11 | `AGENTS.md` | `:161` | "(62 operations, …)" → 64, naming constraint rename/delete |
| 12 | `docs/root1/editing-gap-analysis.md` | `:23` | `62개 에이전트 오퍼레이션` → 64 |
| 13 | `docs/root1/editing-gap-analysis.md` | `:85` | `62개 오퍼레이션(… 편집 37)` → `64개 …(… 편집 39)` — **the category breakdown moves too** |
| 14 | `docs/root1/editing-gap-analysis.md` | `:86` | `62-op registry` → 64 |
| 15 | `docs/root1/editing-gap-analysis.md` | `:95` | the MAR-177 status bullet says GUI/undo/selection/agent are MAR-178 scope and *"registry는 여전히 정확히 62 ops"* — **rewrite it for MAR-178** |
| 16 | `docs/root1/editing-gap-analysis.md` | `:192` | `현재 62-op parity` → 64 |
| 17 | `docs/root1/editing-gap-analysis.md` | `:455` | `현재 62 ops다` → 64 |
| 18 | `docs/root1/refector.md` | `:20` | extend the progression and the **current** total to 64 (§0.2) |
| 19 | `docs/root1/refector.md` | `:112` | `exact **current** total of 62` → 64 |
| 20 | `docs/root1/discription.md` | append | a new MAR-178 paragraph |

**Historical sentences that must NOT change**: `AGENTS.md:274`, `:334`, `:336`,
`:389`, `:412`, `:442`, `:444`, `:659` — all inside dated MAR-176/MAR-177
checkpoint records, where "62" describes what was true then.

**Literals that must never be touched while grepping**: `IM_COL32(56, 61, 69,
255)`, `(51, 56, 64)`, `"x": 56.0`, `56,995,840` bytes,
`PhysicsBoneState … 56 bytes/bone`, `IM_COL32(208,134,57,230)`,
`rgb(54,57,64)`. None contains `62`, but `64` appears in `(51, 56, 64)` and
`rgb(54,57,64)`, so **a blind `62` → `64` substitution is safe while a `64`
search is not** — the sweep greps for `62`, never for `64`.

---

## 9. The GUI-vs-agent asymmetry does not apply here

This codebase has repeatedly established that a **batch** operation skips
incompatible members on the GUI and reports a count, while the agent rejects the
whole request. MAR-173's key scaling and MAR-176's weight generation both work
that way, for a good reason: a human dragging a box selection did not
individually choose each member, so preserving the compatible majority is what
they meant; a scripted caller enumerated the members deliberately, so a silent
partial application hides a bug.

**MAR-178's operations are single-target.** `(family, name)` names exactly one
constraint. There is no set to skip within, and "skipped 1 of 1, changed
nothing" is a rejection with a worse message. So **both surfaces reject,
identically, with the same message text**, produced by the same primitive.

The only differences are presentational, and both are stated so the smoke can
assert them:

- The GUI **pre-disables** its Apply button on a name it can already see is
  taken, and shows the reason inline; the agent has nothing to disable and
  returns `ok: false`.
- The GUI keeps the modal open on a rejection so the user can correct the name;
  the agent's caller retries.

Neither difference changes what is accepted. §12.2 asserts that a GUI-path
rejection message is byte-identical to the agent-path rejection message for the
same `(family, source, destination)`.

---

## 10. Transaction and undo granularity

- **One transaction per accepted rename; one per accepted delete.** Never a
  batch. `EditorSession` refuses nesting (`session.cpp:2141-2147`), and the
  command guards on `transaction_active()` before opening one.
- **`allow_merge = false`.** Merging two renames of the same constraint would
  make one Ctrl+Z jump two names back. Worse, the merge key would have to be the
  constraint's *name*, which is the thing the operation changes — so a merge
  group would be keyed on a moving value. The group string is
  `"constraint-catalog"`, mirroring `"animation-catalog"`.
- **The label is composed by the caller**, as `apply_animation_catalog_action`
  does: `"Renamed <family> constraint <from> to <to>"` /
  `"Deleted <family> constraint <name>"`, using
  `shell::constraint_kind_label()` in the GUI and
  `"… via Agent"` in the dispatcher, matching
  `"Edit IK Constraint via Agent"` (`agent_handlers_constraints.cpp:713`).
- **A cancelled rename leaves nothing** (§7.6). A rejected one leaves nothing
  (§5.3). Both are asserted on `serialize_project()` byte-identity and on
  `session.undo_count()`, never on the return code.
- **Redo re-applies the same single entry**, restoring the renamed name or
  re-deleting, with the selection *not* restored (§6.3) and the constraint ghost
  reconciled away (§6.4).

---

## 10.5 Why every delete test must save **and reload**

`load_project(path)` parses the document and then calls
`build_project_runtime(*project_ptr, *result.base_skeleton_document)`
(`project.cpp:7495`, reached from the path overload at `:7517-7526`). **A reload
is therefore a full materialization plus `load_skeleton_data`**, not a
deserialization of the `.marrow` alone.

That single fact is why this story's tests are shaped the way they are. The
failure mode MAR-177 named — a delete that leaves a dangling
`skins[*].<family>` reference — is invisible at save time, because
`validate_project_for_save()` has no base document and cannot resolve a name
against a skeleton (`project.cpp:5503` onward). It becomes visible only when
something re-materializes and re-parses, and the reload is exactly that.

So a test that asserts `save_project()` returned `ok` proves nothing about this
failure. **Every delete test in this story completes a save → reload cycle**,
and the assertion that matters is that the *reload* succeeds — §12.1 B and F,
§12.2 case 6, §12.4's E2E leg. Where a test can only reach the runtime and not
the file, it asserts on `build_project_runtime()` succeeding, which is the same
code the reload runs.

---

## 11. Export signal

MAR-177 already owns the derived byte deltas for this fixture and asserts them in
`marrow_project_smoke`: renaming `cape_pull` (9 B) → `cape_pull_renamed` (17 B)
gives `.mskl` +16 and `.mbin` +8, the `16 : 8` ratio being the occurrence count;
deleting it drops the `.mbin` string table 39 → 35, removing exactly
`[cape_pull, source, transform, translateMix]`.

**MAR-178 does not restate those numbers.** Re-deriving a neighbour story's
constant is how MAR-176's plan ended up citing an export baseline from the wrong
project. MAR-178 asserts a *different property of the same fixture*, measured
directly rather than as a byte delta, over the **surface** path rather than the
primitive path:

Export the project after a **command-path** rename `cape_pull → cape_drag`
(`EditorSession::export_runtime`), then assert:

1. the exported `.mskl` contains **0** occurrences of `cape_pull` and exactly
   **2** of `cape_drag` — one at `root.transform[0].name`, one at
   `root.skins.cape.transform[0]`. A root-only rewrite gives 1 and is the exact
   failure MAR-177 reproduced by hand;
2. `load_skeleton_document()` + the typed parse of the exported file
   **succeeds** — the save → reload cycle, which is the only assertion that
   catches the unopenable-project failure, because it fails on *load*, not on
   save;
3. the reloaded skeleton's skin `cape` resolves one transform-constraint index,
   and it points at `cape_drag`.

And after a **command-path** delete of `cape_pull`:

4. the exported `.mskl` has **no** root `transform` key and **no** `transform`
   key inside `skins.cape` — MAR-177 §5.2 step 4, without which (5) fails on
   `"transform constraints must not be empty when provided"`;
5. the export **reloads**, `transform_constraints()` is empty, and skin `cape`
   still carries its `cape_target` bone — the adjacent-scope survival assertion
   that MAR-172's failure shape demands;
6. `.mskl` occurrences of `cape_pull` are **0**.

The occurrence count 2 is a Task 0 measurement against the *exported* document,
not a constant carried from MAR-177's spec. If it measures differently, the
correct response is to re-count from the export, not to edit the number.

---

## 12. Validation strategy

Four binaries, matching AC6's four named surfaces.

### 12.1 `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`

The project smoke already constructs `EditorSession` directly
(`editor_project_smoke.cpp:1115`), so it can exercise the command and the
selection cascade with no shell.

**A — command over project-only constraints (AC2, AC3).** `player_idle`, whose
four constraints are all project-only over an empty base.

- Rename `editor_arm_reach` → `arm_reach_v2`: `ok`, `changed`,
  `used_operation == false`, `changed_upsert == true`, `ownership == "project"`,
  `affected_skins` empty, `session.undo_count()` +1.
- **Survival**: the other three constraints are present and field-for-field
  unchanged; `timeline_edits`, `snap`, and the editor metadata are unchanged.
- `session.save()` to a temp path succeeds and the reload matches.
- Delete `editor_ribbon_secondary`: the physics upsert is erased,
  `constraint_lifecycle_operations` stays **empty**, the other three survive.
- Undo restores each; redo re-applies; `serialize_project()` returns to the
  pre-edit string after undo, compared as a string.

**B — command over base-backed constraints and skins (AC2, AC3).**
`skin_inherit_constraints.mskl` with an in-memory project (borrowing
`player_idle.matl`, per §2.10).

- Rename `cape_pull` → `cape_drag`: one record appended,
  `used_operation == true`, `changed_upsert == false`,
  `ownership == "base"`, `affected_skins == ["cape"]`.
- Delete `cape_pull`: one tombstone; the materialized document has no root
  `transform` key and no `skins.cape.transform` key; `cape_target` survives in
  `skins.cape.bones`. Then **`session.save()` to a temp path and
  `load_project()` it back** — the reload re-materializes and re-parses
  (§10.5), so this is the assertion that would catch a dangling skin
  reference. `save()` returning `ok` is *not* the assertion.
- **Shadowing**: upsert `cape_pull` through the project vector, then delete.
  Assert `used_operation && changed_upsert`, `ownership == "shadowed"`, and that
  the materialized document contains no `cape_pull` — the base did not
  resurrect.

**C — selection cascade with no shell (AC2).** A local `SelectionSet` passed to
the command.

- Select `{Ik, "editor_arm_reach"}` plus a `BoneSelection`. Rename. Assert the
  set still has two members, the constraint member now reads `arm_reach_v2`, the
  bone member is untouched, and the active member is the constraint.
- Delete a *selected* constraint out of a three-member set: assert the member is
  gone, order is preserved, and the new active member is the **last** survivor
  (`selection.cpp:195-199`).
- Delete an *unselected* constraint: `selection_changed == false`, set
  byte-identical.
- Pass `nullptr`: the edit still applies and `selection_changed == false`.

**D — rejection leaves nothing (AC4).** Capture `serialize_project()` and
`session.undo_count()` before each of: rename to a taken name; rename to the
source name; rename to an empty name; rename/delete of a name that does not
exist; delete of a name that exists only in a *different* family. After each,
assert the serialized string is **identical** and the undo count is unchanged.
The family-mismatch case additionally asserts the message distinguishes the
family from "not found", which MAR-177's validator already does
(`project.cpp:7789-7801`).

**E — the atlas gate and its message (§4).** Build a project with empty
`atlas_paths`, attempt a rename, and assert three things: it is refused; the
result's message contains `"at least one atlas path is required"` **verbatim**,
so the validator's own sentence survives the surface; and `serialize_project()`
plus `undo_count()` are unchanged. The shell smoke (§12.2) additionally asserts
that the GUI's rendered line puts the *"Cannot rename: the project must
reference at least one atlas before it can be saved."* clause in front of it.
This pins both the decision and the message so a future reader sees they were
chosen, not overlooked.

**F — export and reload (§11).** The six assertions of §11.

**G — family spelling anti-drift (§5.5).** For each of the four families,
append one lifecycle record of that family, serialize, and assert the emitted
`"family"` string equals `constraint_family_key(family)`.

### 12.2 `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`

A new scenario, `validate_constraint_lifecycle_shell_smoke(project_path)`, in a
new TU `src/editor/shell_smoke_constraints.cpp` — a new file rather than a fifth
scenario bolted into `shell_smoke_timeline.cpp` (already 3 700 lines, and the
file another agent is reviewing for MAR-177's registry guard). Declared in
`shell_smoke_scenarios.hpp`, invoked from `shell_smoke.cpp` beside the other
`validate_*_shell_smoke` calls, registered in `CMakeLists.txt:894-901`.

It opens its own `ShellState` via `reload_project()`, guards on the **64**-op
registry like every other scenario, and drives the same helper functions the
modals call — never ImGui.

- **Confirmation**: `request_constraint_delete(...)` then
  `cancel_constraint_delete(...)` leaves `session.undo_count()` and
  `serialize_project()` unchanged; `confirm_constraint_delete(...)` applies.
- **Rename follows the selection**: select `editor_transform_follow`, rename,
  assert `state.selection.active_constraint()->constraint_name` is the new name
  and `items().size()` is unchanged. *Inversion: deleting the `remap` call must
  make this fail.*
- **Delete prunes the selection**: assert `items().size()` drops by one and no
  `ConstraintSelection` for the deleted name remains. *Inversion: deleting the
  `remap` call must make this fail on the count, which is what the status line
  reports.*
- **Undo/redo**: undo restores the constraint and the project string; the
  selection is **not** restored (§6.3), and after
  `reconcile_constraint_selection()` no ghost remains while a co-selected
  `BoneSelection` is still present. *Inversion: widening the helper to
  `reconcile_selection_to_runtime` must make the bone assertion fail.*
- **All four families**: rename and delete one constraint in each of IK, path,
  transform, physics, from `player_idle`, asserting the family tab and the
  surviving three constraints each time.
- **Base-backed through the shell**: write a temp `.marrow` over
  `skin_inherit_constraints.mskl`, `reload_project()` it, rename `cape_pull`,
  and assert `skins.cape` in the rebuilt skeleton resolves the new name.
- **Save → reload after a delete** (§10.5): delete `cape_pull`,
  `save_project_file()`, then `reload_project()` and assert the reload
  **succeeds** and the skeleton has no transform constraints and no
  `skins.cape` transform indices. `reload_project()` re-runs
  `build_project_runtime()` and `load_skeleton_data`, so this is the shell-level
  form of the unopenable-project assertion.
- **Message parity**: force a collision through the shell path and record the
  message for comparison with §12.3.
- **The atlas message** (§4.2): with a project whose `atlas_paths` were
  emptied in memory, attempt a rename and assert the rendered error contains
  both the surface's clause and the validator's verbatim sentence, and that
  nothing changed.

### 12.3 `./build/marrow_agent_dispatch_smoke`

- Registry is exactly **64**, and the two new rows appear in registry order with
  `(edit, mutating, not review, dry-run supported)`.
- `constraint.rename` dry run → live → `constraints.list` read-back → `undo` →
  read-back, on `player_idle`'s transform constraint.
- `constraint.delete` on `editor_ribbon_secondary`, asserting the **survivors**
  by name via `constraints.list` — not the return code.
- Dry run changes nothing: `constraints.list` before and after a dry run are
  byte-identical.
- **Dry-run/live message equality** on a rejection: the same taken-name rename
  produces byte-identical `message` from `dry_run: true` and `dry_run: false`.
  *This is the inversion that bites a hand-written dry-run check.*
- `family` validation: `"bone"` is rejected with the family message; a missing
  `family` is rejected; a physics name given with `family: "ik"` is rejected as
  not found in that family.
- Rejection leaves history clean: a rejected `constraint.rename` followed by
  `undo` must undo the *previous* edit, not the rejected one.
- `scene_delta` shape: `ownership`, `skins`, `skin_reference_count`,
  `used_operation`, `changed_upsert` all present, and the dry-run payload equal
  to the live payload apart from `"dry_run"`.

### 12.4 `tools/mcp/test_client.py`

- `assert len(registry_names) == 64` and `assert len(mcp_names) == 64`, with
  `set(registry_names) == set(mcp_names)` unchanged.
- Both new names added to `new_edit_operations`.
- Explicit registry metadata rows for both, in the style of the existing
  `timeline.set_interpolation` assertion (`test_client.py:80+`).
- `constraint.rename` dry run, live, `constraints.list` read-back, `undo`,
  read-back; then `export_runtime` and an assertion that the exported skeleton
  no longer names the old constraint — AC5's export-preview leg.
- The tool-removal negative, verified by hand once as MAR-176 did
  (`AGENTS.md:444`): deleting one new `types.Tool` from `editing.py` must fail
  `assert len(mcp_names) == 64`, then restore it.
- `python -m py_compile` over all four MCP files.

### 12.5 Regression and compatibility gates

- `./build/marrow_fixture_smoke`, `./build/marrow_unit_tests`,
  `ctest --test-dir build --output-on-failure` unchanged.
- `./build/marrow_project_smoke --create /tmp/player_idle.marrow` unchanged.
- Zero-byte diff on `src/runtime/**`, `include/marrow/runtime/**`,
  `include/marrow/marrow_c.h`, `src/c_api/**`, `src/editor/preferences.cpp`,
  `include/marrow/editor/preferences.hpp`, and **`src/editor/project.cpp` and
  `include/marrow/editor/project.hpp`** — MAR-177's files, under concurrent
  review, which MAR-178 must not touch.
- `docs/root1/format-spec.md` unchanged (§3.3).
- Never run `marrow_editor_shell` without `--auto-close`, and never create
  `~/Library/Application Support/Marrow`.

---

## 13. Non-goals, restated for the reviewer

1. No new `.marrow` field; the schema MAR-177 added is sufficient and
   `format-spec.md` needs no edit.
2. No change to `rename_constraint`, `delete_constraint`,
   `validate_constraint_lifecycle_operations`, or Phase A. MAR-178 calls them;
   it does not touch `project.cpp` or `project.hpp`.
3. No narrower save validation (§4).
4. No constraint parameter widgets and no IK agent field parity — MAR-179.
5. No constraint creation and no reordering.
6. No selection persistence and no selection in `EditorHistorySnapshot`.
7. No reshaping of `constraints.list`.
8. No debug-overlay flag mutation.
9. No new tunable numeric constant. MAR-178 introduces none.

---

## 14. Decisions taken under ambiguity

Marked because they could not be verified without building, which this document
was not permitted to do. Each is a Task 0 gate in the plan.

| # | Decision / claim | Basis | Gate |
| --- | --- | --- | --- |
| 1 | Registry is **62** and becomes **64**; category split `edit 37 → 39` | counted `kOperationSpecs` rows and grouped by category with `python3` | Task 0 re-measures both |
| 2 | 7 + 1 `!= 62U` comparisons and 7 + 1 guard messages, at the listed lines | `grep -n '!= 62U'` and `grep -n 'exact 62-operation registry'` | Task 0 re-measures; a different count means the sweep list is wrong, not the guard |
| 3 | Nine live prose sites carry the current total | `grep -n '62'` over `AGENTS.md` and `docs/root1/*.md`, then classified by the brief's IS-vs-historical rule | Task 0 re-greps and re-classifies |
| 4 | `load_project()` rejects an empty `$.runtime.atlases` with `"array must not be empty"` | read `project.cpp:290-295`; **independently confirmed by the MAR-177 review** | Task 0 confirms the text; §4's decision rests on it |
| 4b | `load_project(path)` calls `build_project_runtime()`, so a reload is a full materialization plus `load_skeleton_data` | read `project.cpp:7495` and `:7517-7526`; **independently confirmed by the MAR-177 review** | Task 0 confirms; §10.5 and every delete test rest on it |
| 4c | `parse_animations` names no constraint; skins are the complete referrer set | read `skeleton_parse.cpp:5214-5500` and enumerated its member keys; **independently confirmed by the MAR-177 review** | Task 0 re-enumerates; §5.4's summary is exhaustive only if this holds |
| 5 | `remap(x, nullopt)` and `prune` choose the **last survivor** as active | read `selection.cpp:155-159`, `:195-199` | Task 0 confirms; §6.2 rests on the two agreeing |
| 6 | `EditorHistorySnapshot` has no `SelectionSet` and `history_snapshots_equal` compares three things | read `shell_state.hpp:355-362`, `shell_core.cpp:163-181` | Task 0 confirms; §6.3 rests on it |
| 7 | `rebuild_project_runtime()` does **not** reconcile the selection; `reload_project()` does | read `shell_core.cpp:498-533` vs `:628-631` | Task 0 confirms; §2.6 and §6.4 rest on it |
| 8 | `cape_pull` occurs exactly **2×** in the exported document | MAR-177 measured it in the fixture; MAR-178 asserts it in the **export** | Task 0 re-counts from the exported text; if it differs, re-derive, do not edit the constant |
| 9 | `SkinData::<family>_constraint_indices` is the right source for `affected_skins` | read `runtime/skeleton.hpp:285-288` and `skeleton_parse.cpp:5169-5207` | Task 1 compiles and Task 6 asserts `["cape"]` |
| 10 | `agent_handlers_constraints.cpp` can include `marrow/editor/constraint_catalog.hpp` with no cycle | it already includes `session.hpp` and `shell_constraints.hpp` | Task 4 compiles it |
| 11 | A new `src/editor/shell_smoke_constraints.cpp` needs only a `CMakeLists.txt:894-901` line | read the target's source list | Task 3 builds it |
| 12 | `player_idle`'s four constraints are `editor_arm_reach`, `editor_guide_follow`, `editor_transform_follow`, `editor_ribbon_secondary` | read `assets/fixtures/player_idle.marrow` | Task 0 re-reads |

Numbers 2, 3 and 8 are the ones most likely to be wrong, and each is wrong in a
useful way: 2 and 3 abort the sweep on a count mismatch, and 8 carries its
derivation so a mismatch identifies which premise failed.
