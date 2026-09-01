# MAR-186 Collect Structured Project Diagnostics — Implementation Plan

Design: `docs/superpowers/specs/2026-09-01-mar-186-structured-project-diagnostics-design.md`
Story: `MAR-186`, `dependsOn: ["MAR-185"]` — a **scheduling** dependency, not a
deep code one (design §0.2).
Branch: `feat/mar-168`. First drafted against `6986024`; **corrected and
re-anchored against `5a56663`** (`feat: MAR-185 상속 타임라인 편집 패리티 완성`).

> **MAR-185 has LANDED and the first draft's baseline no longer exists.** The
> plan was written two stories ahead of the tree; it is now one. MAR-184's commit
> is **`8cc5c57`**, not the `6986024` originally cited — that hash was rewritten.
> **Task 0.1 remains a blocking re-anchor gate**: two anchors had already drifted
> before MAR-185 even landed (§A.1 A6), which is the argument for running it
> rather than trusting this list. Cite the **symbol**; treat the line as a hint. A
> citation that no longer names its described construct is a **blocking finding**
> written into `AGENTS.md`'s document-errors table, never a line to silently follow.

**Read the design first**, then §A, then §B. In particular design §1.2 (the
runtime parser, not this story, draws AC2's boundary), §2.4 (how `warning_count`
stays numerically identical), §2.5 (the animation-name authority — the single
most likely implementation error), and §5.1 (what is deliberately uninverted).

## Standing rules for this plan

- **TDD, strictly.** The case goes in first, is **run and seen to fail for the
  stated reason**, and only then is the implementation written. A case that
  passes on its first run is a defect in the case until proven otherwise. The
  two exceptions are named and justified in place: **G0** and **G4** are
  witnesses, expected to pass immediately, and their falsifiability is
  demonstrated by named inversions rather than by a first-run failure.
- **Every case must be proven falsifiable, and uniquely attributed.** §B is the
  register. For each entry: apply the named source mutation, **delete the object
  file**, build, run the named binary, record the **exact** failure text, restore,
  delete the object file again, rebuild.
- **Verification builds delete the object file. They do not `touch` the source.**
  `AGENTS.md`'s H1 extension, added by MAR-184: `touch` sets the source's mtime
  to *now*, the preceding restore-build's object is also *now*, and at GNU Make
  3.81's one-second granularity the rebuild is **skipped** — MAR-184 got **nine
  false "did not bite" readings** this way, six of them on mutations that had
  demonstrably bitten minutes earlier. The build here is `Unix Makefiles` with
  **GNU Make 3.81** (measured: `build/CMakeCache.txt`, `make --version`), so the
  hazard is live. §0.13 gives the exact `rm -f` lines.
- **Uniqueness is established by running, not reading.** Where §B says "nothing
  earlier catches this", demonstrate it: neuter the earlier candidate
  (`if (false && …)`), rebuild with the object deleted, and confirm the attributed
  case is still the one that fails.
- **Compare recorded messages with `cmp`/`diff` over a whole recorded string**
  (H2). Never hand-slice a line number out of a reference file.
- **Trace which writes survive to the assertion point** (H4). An assertion that
  reads state a passing run leaves at its default is vacuous. Design §2.10 already
  rules out the one vacuous case this story invites (a `SelectionSet` snapshot
  around a collector that takes no `SelectionSet`); do not add it back.
- **Never assert a count where an identity is available.** Design §11's R4. Every
  case asserts the **full sorted identity list**; a count-only assertion passes
  for the wrong reason, exactly as MAR-184's I2b proved live for `!result`.
- **A passing `save()` proves nothing.** `validate_project_for_save`
  (`src/editor/project.cpp:5825` at `6986024`) takes no base document and
  materializes nothing. Every round trip goes `save_project` → **`load_project(path)`**,
  which calls `build_project_runtime` (defined at `:8258`, called from
  `load_project(const Document&)` at `:7932`).
- **The project smoke has ONE invocation.** Every editing suite sits inside
  `main()`'s marker-gated `else`; pointing the binary at another fixture takes the
  **skip** branch, runs nothing, and exits 0. Build throwaway projects **inside**
  the standing `player_idle.marrow` invocation.
- When a measurement disagrees with this plan, **the measurement wins.** Record it
  in `AGENTS.md`'s "Document errors found" table; do not silently adapt.
- Never weaken a case to make an inversion bite. Strengthen the case.
- `git add` / `git commit` only at Task 11. No `checkout`, no `stash`.

---

## §A. What was verified, what was wrong, and what Task 0 must still gate

### A.1 Errors found in the incoming brief

Design §0.3 carries the full table of **eight**. The three that change what the
implementer does are repeated here:

| # | Source | Claim | Measured (A1-A3 at `6986024`; A4-A8 at `5a56663`) |
| --- | --- | --- | --- |
| **A1** | The team lead's MAR-186 brief | "AC6 spans C++ and Python. If the agent operation registry changes, the count derives from `std::size(kOperationSpecs)` … Note the registry is currently 64" | The registry count is **64** — confirmed. But **AC6 is not about the registry**: it reads *"Project, agent, and MCP tests cover deterministic results, severity counts, legacy summary compatibility, typed targets, revision changes, and zero-mutation inspection."* **MAR-186 adds no operation**, so the count sweep is a **non-effect** gate (Task 9), not a substitution task. MAR-185 moves the number to 66; MAR-186 must leave whatever it finds |
| **A2** | The same brief | The hand-edited count sites are "`agent_dispatch_smoke.cpp` array size, guards in `shell_smoke_graph.cpp` and `shell_smoke_timeline.cpp`, `tools/mcp/test_client.py`, and prose" | **Two guards are missing from that list.** Measured, all ten: `shell_smoke_constraints.cpp:147,676`, `shell_smoke_timeline.cpp:3697`, `shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603` |
| **A3** | The same brief | "`build_project_runtime` is defined at `src/editor/project.cpp:8258` (**not** `:7495`)" | **Confirmed, and still `8258` after MAR-185.** Re-verify anyway at Task 0.1: MAR-185's plan records this same symbol at `:7860` and then `:8258` within one story |
| **A4** | **The same brief, adopted uncorrected into §0.4 and R11 of this plan** | "Adding an enum value produces zero diagnostics at every switch site … the sweep is entirely manual and must be planned as such" | **False. Clang enables `-Wswitch` by default.** Measured with `c++ -std=c++17 -c` and no flags: `warning: enumeration value 'C' not handled in switch [-Wswitch]`. MAR-185 measured **25** warnings from a throwaway enum value (`AGENTS.md:305-334`). §0.4's gate is rewritten from "prove the compiler is silent" to "prove it is not", and R11 is re-aimed at the class that really is unpoliced: **fixed-length call lists**, where MAR-185 shipped `clipboard_track_count`. Design §0.3 E9 |
| **A5** | This plan's §0.10 and design §1.2 | **Three** hard parser rejection rules | **Four**: `skeleton_parse.cpp:1616, 5270, 5373, **5430**`. The `deform` walk resolves its slot in a second loop and emits the same message text as the `slots` walk. **A gate expecting three fails on a correct tree** — worse than a wrong number, because it sends the implementer hunting a phantom regression. Design §0.3 E10 |
| **A6** | This plan's §A.3 | Design §1.3's **six** merge loops; window titles `shell_state.hpp:979-990`; `ensure_project_loaded` `agent_dispatch.cpp:531-534` | **Seven** merge loops (`…, 5784`), contradicting design §2.5's own table of seven — a section-vs-section disagreement, which is the signature of a mis-transcribed total and is why every neighbouring count here was re-summed. Anchors are now **`:989-1000`** and **`:536`**. Design §0.3 E11, E12 |
| **A7** | This plan's §0.4 gate `G-i` | `grep …DiagnosticSeverity…` returns 0 before Task 1 | **It can never return 0**: `SpineImportDiagnosticSeverity` already exists in `src/runtime/spine_import.cpp` with ten-plus uses. The gate as drafted **cannot pass**. Fixed with `grep -rnw` on the exact names. Design §0.3 E13-adjacent |
| **A8** | This plan's Task 1 and design §2.11 | `safe_fix_id` plus a typed target is enough for MAR-187 | **It is not** (`plan-mar187`'s D6). Task 1 now ships a typed `DiagnosticCode` and `DiagnosticOverlayFamily` on `DiagnosticIssue`. Design §0.3 E14 |

### A.2 Design claims that Task 0 must **prove by running**

| # | Design claim | How Task 0 settles it |
| --- | --- | --- |
| **A4** | §2.6 — canonicalization is a **bit-exact fixed point**, which is what makes an `==` comparison the right predicate | Canonicalize a fixture vertex twice; `memcmp`-equivalent field compare. **If the second pass changes anything, §2.6 is wrong and Task 4 changes shape** |
| **A5** | §1.4 / §6.2 G8 — a lone `{spine, 1e-9}` influence survives `parse_mesh_weight_vertices`, `validate_project_for_save` **and** `skeleton_parse.cpp`, so an `Error`-severity issue is reachable from a file | Write it, save, `load_project(path)`. **If the load fails, G8 becomes an in-memory case and `AGENTS.md` records the Error class as unreachable from a file** |
| **A6** | §1.8 — `player_idle.marrow` yields **zero** issues, which is what keeps every shipped assertion unedited | G0 prints the count every run |
| **A7** | §2.5 — `apply_animation_edits` is unreachable from `diagnostics.cpp`, so `authored_animation_names` has to live in `project.cpp` | `grep -n "^namespace\|^} // namespace" src/editor/project.cpp`; confirm `apply_animation_edits`' line falls inside the first `namespace {` block |
| **A8** | §1.2 — the **four** hard parser rules still exist and still make those classes unopenable | `grep -n "animation references unknown bone\|animation references unknown slot\|mesh weight references unknown bone" src/runtime/skeleton_parse.cpp` → **four** hits (`1616, 5270, 5373, 5430`); two share one message text |
| **A9** | §2.9 — `AttachmentSelection` and `MeshWeightAttachmentEdit` really are transposed | Read both declarations |
| **A10** | §1.5 / §1.6 — no diagnostics vocabulary exists, and no product target carries `-Wswitch` | Two greps, both expected empty |

### A.3 Claims re-verified at `5a56663` — do not re-litigate, only re-anchor

- `project.diagnostics` is registered at `agent_dispatch.cpp:41` and handled in
  `handle_inspection_operation`'s `op == "project.diagnostics"` branch,
  `agent_handlers_inspection.cpp:342-355`. Its four members are
  `error_count` (literal `0`), `warning_count` (`session.dirty() ? 1 : 0`),
  `project_dirty` (`session.dirty()`), `review_queue_count`
  (`control.review_queue.size()`).
- `ensure_project_loaded` (`agent_dispatch.cpp:536`, was `:531-534` before
  MAR-185 — A6) checks `has_project()`, `project()` and `runtime_data()` —
  **not** `base_skeleton_document()`.
  `runtime.validate` re-checks it by hand at `agent_handlers_inspection.cpp:44-56`.
- `ProjectData`'s seven animation-scoped overlay vectors plus
  `mesh_weight_attachment_edits`: `project.hpp:590-600`. `MeshWeightAttachmentEdit`
  `:206-211`, `MeshWeightVertexEdit` `:202-204`, `MeshWeightInfluenceEdit` `:195-200`.
- `build_runtime_document` `project.cpp:5637`; `apply_animation_edits` called at
  `:5694`; the mesh-weight skip loop `:5696-5706`; the **seven** overlay merge
  loops `:5722, 5739, 5751, 5761, 5768, 5775, **5784**` (A6 — the first draft
  listed six and contradicted design §2.5's own table of seven).
  `apply_animation_edits` defined `:5423`, inside the anonymous namespace
  spanning `:24-6862`.
- `validate_project_for_save` `project.cpp:5825`; its preview-skin rule
  `:5943-5948`; its mesh-weight rules `:6157-6221`.
- `parse_mesh_weight_vertices` `project.cpp:2149-2286`.
- `mesh_weight_model::canonicalize_mesh_weight_vertex` `mesh_weight_model.cpp:56`;
  `kMaxMeshWeightInfluences`, `kMeshWeightEpsilon`, `kMeshWeightSumTolerance` and
  the fixed-point doc comment in `src/editor/mesh_weight_model.hpp`;
  `mesh_weight_edit_from_runtime` `mesh_weight_model.cpp:163`.
- `PreviewController::normalize_state` `session.cpp:723`; the skin loop `:724-733`;
  the animation substitution `:754-758`. The open path's
  `initial.animation_name` / `initial.skin_names` `:1743-1744`.
- `EditorSession` accessors: `has_project()`, `project()`,
  `base_skeleton_document()`, `runtime_data()`, `dirty()` `:353`,
  `project_revision()` `:354`, `runtime_revision()` `:355`,
  `preview_revision()` `:356`.
- `SelectionSet` and its four items `include/marrow/editor/selection.hpp`;
  `AttachmentSelection`'s fields are `slot_name`, `skin_name`, `attachment_name`
  `:48-52`.
- `runtime::json::Value::Object` is `std::map<std::string, Value, std::less<>>`
  `include/marrow/runtime/json.hpp:24`.
- `SkeletonData::find_animation`, `find_skin_index` `:860`, `find_slot_index` `:854`,
  `find_attachment(skin_name, slot_index, attachment_name)` `:933-936`.
- `skeleton_parse.cpp`, **four** rejection rules (A5): animation unknown bone
  `:5270`, animation unknown slot in the `slots` walk `:5373`, animation unknown
  slot in the **separate `deform` walk** `:5430`, mesh weight unknown bone
  `:1616`; plus the positive-weight rule `:1633-1640`.
- `editor_project_smoke.cpp` **after MAR-185**: the marker gate `:16134`,
  `validate_mar184_inherit_overlays` registered `:16279`,
  `validate_mar185_inherit_editing` registered `:16282` — **MAR-186's suite
  registers after `:16282`**. (Before MAR-185 these were `:15303` / `:15448`; the
  drift is why Task 0.1 exists.)
- `agent_dispatch_smoke.cpp`: `kExpectedOperations` `:38-105` with the bound at
  `:39`; `member` `:115-119`; `string_array_paths` `:533`;
  `exercise_parameter_operations` `:619-956` (the second-project pattern);
  `main()` `:961`; the registry-integrity block `:980-988`; the existing
  `project.diagnostics` assertions `:1082-1085`, `:3800-3805`, `:3881-3890`.
- `tools/mcp/tools/inspection.py`'s `project.diagnostics` tool `:108-114`;
  `tools/mcp/test_client.py` count assertions `:53,55`, the MAR-179 explanatory
  comment `:57-70`, the `project.diagnostics` call `:482`.
- `CMakeLists.txt`: `marrow_editor` sources `:499-520`, its `PRIVATE
  src/editor` include dir `:521-527`, `marrow_c` PUBLIC-links `marrow_editor`
  `:396-400`, `marrow_agent_dispatch_smoke` links only `marrow_c` `:863-867`,
  `marrow_project_smoke` links `marrow_editor` `:820-823`, `-Werror` only at
  `:352`. **`marrow_editor_shell`'s own include dirs `:916-921` name only
  `external` and `src/renderer/generated`** — it reaches `src/editor/*.hpp` by the
  same-directory rule, not by adding the path (design §0.3 E13).
- `ctest --test-dir build -N` → **Total Tests: 22**, unchanged by MAR-185.
- The registry after MAR-185: **66** rows, array bound **66** at
  `agent_dispatch_smoke.cpp:39`, **eleven** `!= 66U` guards, two `== 66` at
  `tools/mcp/test_client.py:53,55`.
- `docs/root1/agent-control.md:70` is the one prose line describing the operation.

---

## §B. Inversion register

An inversion is valid only if the case it names is the **first** thing that
catches it. Where an earlier detector exists it is named and the attribution
moves. Where uniqueness rests on an earlier case, the demonstration is: neuter
that case and re-run.

Predicted failure texts assume the message formats specified per task. When a
measured message differs, **the measured text is authoritative** and the
difference is a document error to record — MAR-184 recorded four of exactly this
kind (its D4, D6, D7, D8).

| # | Mutation | Attributed to | Must fail with | Why nothing earlier catches it |
| --- | --- | --- | --- | --- |
| **I1** | `collect_orphan_animation_overlays` resolves against `skeleton.find_animation(edit.animation_name)` instead of `authored_animation_names` | **G1** | `MAR-186 G1: collect_project_diagnostics returned 0 issues, expected 7. Missing: overlay.orphan_animation\|deform\|ghost\|body\|body_mesh, …draw_order…, …event…, …inherit…, …slot_attachment…, …slot_color…, …transform…` | G0 runs over `player_idle.marrow`, whose only overlay animation (`idle`) exists in the base catalog, so it reports zero either way. **This is the story's central mutation**: the phantom animations are *in* the materialized skeleton, so the substitution makes the collector report a clean project and every count-only assertion pass |
| **I2a-g** | Drop **one** of the seven family calls from the F1 sweep. **Run all seven**, one at a time | **G1** | e.g. for the inherit family: `MAR-186 G1: collect_project_diagnostics returned 6 issues, expected 7. Missing: overlay.orphan_animation\|inherit\|ghost\|arm_l` | The other six still fire, so a count-only or "at least one orphan" assertion passes. G1 compares the **full sorted list** and reports the set difference by name. Seven mutations, seven recorded messages — none of the seven may be inferred from another |
| **I3** | `finalize` returns the issues in collection order (delete the `std::sort`) | **G1** | `MAR-186 G1: issue 0 is 'overlay.orphan_animation\|transform\|ghost\|arm_l\|rotate', expected 'overlay.orphan_animation\|deform\|ghost\|body\|body_mesh'. Collection order is transform, inherit, deform, … and is not the contract; identity order is` | G0 has no issues to order. **G9(c) is an independent detector** — de-duplication is adjacent-unique over the sorted range, so without the sort the duplicate `preview.stale_skin` survives. Demonstrate by neutering G1 and re-running |
| **I4** | Delete the adjacent-unique de-duplication from `finalize` (keep the sort) | **G9(c)** | `MAR-186 G9(c): preview_skins ["default","ghost_skin","ghost_skin"] produced 2 issues, expected 1. Identities are not strictly increasing: 'preview.stale_skin\|ghost_skin' == 'preview.stale_skin\|ghost_skin'` | No other fixture contains a duplicate. `validate_project_for_save` refuses duplicate mesh-weight and draw-order edits, and every other family's fixture is unique by construction. `preview_skins` is the one vector nothing de-duplicates |
| **I5** | Delete `escape_identity_token` (join the raw tokens) | **G12** | `MAR-186 G12: two distinct orphan overlays collapsed to 1 issue. Bones 'a\|b' and 'a' + channel-token collision produced identity 'overlay.orphan_animation\|transform\|ghost\|a\|b\|rotate' twice` | No fixture in the tree has a `\|` or `\\` in any name, and none of G1-G11 authors one. The collision is invisible until a name is chosen to produce it, and de-duplication then **deletes** an issue rather than duplicating one — which is why the case must assert a **count of two** *and* two distinct identities |
| **I6** | Append the edit's index within its vector to the identity | **G1** | `MAR-186 G1: issue 0 is 'overlay.orphan_animation\|deform\|ghost\|body\|body_mesh\|0', expected 'overlay.orphan_animation\|deform\|ghost\|body\|body_mesh'` | G1 compares against literal expected strings, so it fails first. **G3 is the independent detector** for the *property* — an index-derived identity is stable within one collection and only moves when an unrelated overlay is inserted before it. Demonstrate by neutering G1 and confirming G3 fails with `the same overlay produced two identities: '…\|rotate\|0' and '…\|rotate\|2'` |
| **I7** | `collect_weight_canonicality` compares influences with `std::abs(a - b) < 1e-9` instead of `==` | **G7** | `MAR-186 G7: the ULP-perturbed vertex produced 0 issues, expected 1 weights.non_canonical. A tolerance swallows exactly the drift canonicalization exists to remove` | G7's sum-`0.7` and reversed-order sub-cases both differ by far more than `1e-9` and still fire. Only the deliberate one-ULP sub-case distinguishes the predicates |
| **I8** | `weights.uncanonicalizable` is given `safe_fix_id = kSafeFixNormalizeWeights` | **G8** | `MAR-186 G8: weights.uncanonicalizable carries safe_fix_id 'normalize_weights', expected none. Normalization is precisely what cannot repair a vertex the canonicalizer rejects` | The allowlist invariant passes — `normalize_weights` **is** allowlisted, which is exactly why an allowlist check is not a substitute for this assertion. **A4 is an independent detector** at the wire layer; demonstrate by neutering G8 |
| **I9** | The handler emits `safe_fix_id` as `""` rather than omitting the member | **A4** | `MAR-186 A4: the weights.uncanonicalizable issue object carries a 'safe_fix_id' member (value ""), expected the key to be absent` | **No C++ case can see this.** `DiagnosticIssue::safe_fix_id` *is* the empty string in both worlds; the difference exists only in the serialized object. G8 passes unchanged |
| **I10** | Build `AttachmentSelection` from `MeshWeightAttachmentEdit` in field order (`{edit.skin_name, edit.slot_name, edit.attachment_name}`) | **G6** | `MAR-186 G6: the weight issue's AttachmentSelection has slot_name 'mesh_base' and skin_name 'body', expected slot_name 'body' and skin_name 'mesh_base'. The two structs are transposed` | Codes, severities, identities, counts and panels are all still correct — the identity is built from the edit's own fields, not from the selection. Only an assertion that reads `slot_name` and `skin_name` **by name** fails |
| **I11** | Remove the F2 supersession: examine an orphan weight edit's vertices anyway | **G9(e)** | `MAR-186 G9(e): an orphan weight target with non-canonical vertices produced 2 issues, expected 1. Extra: weights.non_canonical\|mesh_base\|body\|ghost_mesh\|0` | G7 and G8 both use a **resolvable** target, so the supersession never runs there. G9(e) is the only fixture where an edit is both orphaned and non-canonical |
| **I12** | Resolve `preview.stale_animation` against `authored_animation_names` instead of the materialized skeleton | **G9(b)** | `MAR-186 G9(b): active_animation 'ghost' with seven orphan overlays present produced a preview.stale_animation issue, expected none. The overlays resurrect 'ghost', so the preview genuinely resolves` | G9(a) has no overlays, so both authorities agree there and it passes. G9(b) is the only fixture where the two disagree |
| **I13** | `collect_session_diagnostics` counts severities **before** appending `project.unsaved_changes` | **G10** | `MAR-186 G10: a dirty session reported warning_count 1, expected 2 (one project warning plus the unsaved-changes warning). issues.size() is 2` | The pure collector is unaffected, so G0-G9's pure-collector assertions all pass. **A2 is an independent detector** over the wire (`warning_count == (project_dirty ? 1 : 0)` on a clean fixture); it runs in a different binary and is recorded as over-determined |
| **I14** | Swap the two accumulators in `finalize`: `Error` increments `warning_count` | **G10** | `MAR-186 G10: error_count 1, warning_count 1 — expected error_count 1, warning_count 1 … ` — **and this is why the case must use asymmetric counts**: G10's fixture carries **one Error and two Warnings**, so the mutation reads `error_count 2, warning_count 1, expected error_count 1, warning_count 2` | A symmetric fixture (one of each) makes this mutation invisible. The asymmetry is load-bearing and the case says so in a comment. **A3 is a second detector** over the wire |
| **I15** | The handler stops emitting `review_queue_count` | **A1** | `MAR-186 A1: the project.diagnostics payload is missing 'review_queue_count'. AC3 requires all four legacy members to survive` | No C++ case reads the payload. The shipped assertion at `agent_dispatch_smoke.cpp:3881-3885` also fires, one step later, and is recorded as the **pre-existing** detector — A1 is the one that names AC3 |
| **I16** | `collect_session_diagnostics` returns a default-constructed `DiagnosticReport` instead of `std::nullopt` for a session with no project | **G11** | `MAR-186 G11: collect_session_diagnostics on a session with no project returned a report (0 issues), expected std::nullopt. AC2 limits collection to normally opened sessions` | The dispatcher's `ensure_project_loaded` rejects first, so **no agent case can reach this branch**. G11 constructs the session directly and is its only detector |
| **I17** | The handler serializes only `issues.front()` | **A3** | `MAR-186 A3: issue_count is 3 but the issues array has 1 element` | A1 runs over `player_idle.marrow`, where the array is empty and truncation is invisible. A3 is the first case with more than one issue on the wire |
| **I18** | `authored_animation_names` returns only the base document's animation names, ignoring `project.animation_edits` | **G2** | `MAR-186 G2: with AnimationEdit{Create,"ghost"} present, collect_project_diagnostics returned 7 issues, expected 0. A project-authored animation is not an orphan` | G1's fixture has an **empty** `animation_edits`, so the fold and the raw base document agree there and G1 passes. G2 is the only case that makes them disagree |
| **I19** | `tools/mcp/tools/inspection.py`'s `project.diagnostics` `inputSchema` gains a bogus `{"verbose": {"type": "boolean"}}` property | **M1's schema half** | `MAR-186 M1: project.diagnostics inputSchema declares properties ['verbose'], expected none` | `MarrowClient.send_command` writes JSON straight to the agent socket and never consults `inputSchema`, so every wire assertion passes. This is the same shape `test_client.py:57-70` already documents for MAR-179 |
| **I26** | `collect_orphan_animation_overlays` sets `family = DiagnosticOverlayFamily::None` for the inherit call | **G1** | `MAR-186 G1: the issue 'overlay.orphan_animation\|inherit\|ghost\|arm_l' carries family 'none', expected 'inherit'. MAR-187's remove_orphan_overlay cannot find its ProjectData vector without it` | Identity, code, severity, safe-fix id and count are all still correct — `family` is carried beside the identity, not derived from it, so nothing that reads the identity notices. This is the assertion that makes design §2.11's contract real rather than declared |
| **I20** | `#include "shell_state.hpp"` in `src/editor/diagnostics.cpp` | **Task 9's include gate** | `grep -nE "imgui\|shell_\|sokol" src/editor/diagnostics.cpp` returns a line | **Not a test** — a scope check, recorded as one rather than counted as coverage. AC2's "UI-free" has no runtime symptom; a shell include compiles fine because `marrow_editor` sees `src/editor/` |
| **I21** | Add a `const_cast` to `collect_session_diagnostics` and call `session.seek(0.5)` | **G0** | `MAR-186 G0: collect_session_diagnostics advanced preview_revision from 1 to 2. Collection must not mutate the session` | **This is what makes G0 non-vacuous.** Without it, G0's seven-value snapshot reads state a passing run leaves at its default, which is exactly hazard H4. The mutation proves the snapshot can move |
| **I22** | Invert the stale-skin test: report skins that **do** resolve | **G0** | `MAR-186 G0: player_idle.marrow produced 1 issue, expected 0: preview.stale_skin\|default` | **G9(c) is an independent detector** (its list becomes `["default"]` instead of `["ghost_skin"]`). G0 runs first and is the false-positive witness; demonstrate G9(c)'s independence by neutering G0 |
| **I23** | Bone-family orphan targets get `DiagnosticPanel::Project` instead of `Timeline` | **G6** | `MAR-186 G6: overlay.orphan_animation\|transform\|ghost\|arm_l\|rotate has panel 'project', expected 'timeline'` | Identities, codes, severities, counts and fixes are unaffected. Only G6 reads `target.panel` |
| **I24** | Delete the `collect_weight_canonicality` call from `collect_project_diagnostics` | **G7** | `MAR-186 G7: the sum-0.7 vertex produced 0 issues, expected 1 weights.non_canonical\|mesh_base\|body\|body_mesh\|0` | G0-G6 have no weight overlays at all. **G8 is an independent detector**; demonstrate by neutering G7 |
| **I25** | One detector emits `"normalise_weights"` (British spelling, not in the allowlist) | **G7's allowlist invariant** | `MAR-186 G7: issue 'weights.non_canonical\|mesh_base\|body\|body_mesh\|0' carries safe_fix_id 'normalise_weights', which is not in the three-entry allowlist` | Codes, identities, severities and counts are all correct; the id is merely wrong. The invariant runs on every case, so G7 is simply the first case with a weight issue |

### B.1 Deliberately uninverted — record verbatim in `AGENTS.md`

Design §5.1. **No inversion is invented for any of these**, and the reason is
stated per item:

- **G4 (identity across unchanged revisions)** is an **idempotence witness**.
  There is no mutation of MAR-186's own code that makes two collections of an
  unchanged project differ without failing G1 far more loudly first. The case
  exists because AC1 names the property, not because a mutation is available.
- **G5 (save → load round trip, and the resurrection)** is an **integration
  witness** over `save_project`, `load_project` and `build_runtime_document`,
  none of which MAR-186 touches. Its value is that it proves the identities are
  computed from data that survives a round trip.
- **G0's byte-identity half** — an unchanged project must serialize to unchanged
  bytes. No story-owned mutation makes it differ without failing something
  louder. G0's *other* halves are inverted (I21, I22).
- **`collect_session_diagnostics`' `base_skeleton_document() == nullptr`
  sub-condition** is unreachable: `open` sets the document and the runtime data
  together. It mirrors `runtime.validate`'s own unexercised null-document branch
  and is recorded as defensive.
- **`diagnostic_panel_name` and `diagnostic_severity_name`** are total over three
  and two values. Every arm is read by a message or a payload string some case
  already asserts.
- **`review_queue_count`'s expression** is copied verbatim from the shipped branch
  and already covered by `agent_dispatch_smoke.cpp:3881-3885`. MAR-186 adds no
  inversion for a guard it did not write.
- **The deferred `-Werror=switch` option** (design §2.12) is a mitigation
  MAR-187 may take. It is recorded in `AGENTS.md` as deferred, never as coverage.

---

## Task 0 — Measure, before any code

**Depends on:** MAR-185 being committed (or an explicit decision to proceed
without it — see 0.3). **No file is modified.** Every step prints a value; a
value that disagrees with §A is a stop-and-record.

### 0.1 **Re-anchor gate — every cited line number, before anything else**

Every `file:line` in these two documents was measured at `6986024` and is
**expected to have drifted**. MAR-185 edits `authoring.{hpp,cpp}`,
`timeline_model.{hpp,cpp}`, `timeline_controller.{hpp,cpp}`, `shell_timeline.cpp`,
`shell_project_panels.cpp`, `agent_dispatch.cpp`, `agent_handlers_editing.cpp`,
`agent_dispatch_smoke.cpp`, `editor_project_smoke.cpp`, `shell_smoke_*.cpp` and
`tools/mcp/` — several of which MAR-186 also touches.

```bash
git rev-parse HEAD
git log --oneline -3
git status --short                       # expect only this story's two docs
git diff --stat 6986024..HEAD -- src/ include/ tools/ CMakeLists.txt
```

For each anchor in §A.3, the check is **not** "does that line still exist" but
**"does that line still name the construct the document says it names"**:

```bash
# One worked example; repeat the shape for EVERY anchor in §A.3.
grep -n '"project.diagnostics"' src/editor/agent_dispatch.cpp src/editor/agent_handlers_inspection.cpp
grep -n "^ProjectRuntimeResult build_project_runtime" src/editor/project.cpp
grep -n "^void apply_animation_edits" src/editor/project.cpp
grep -n "^bool validate_project_for_save" src/editor/project.cpp
grep -n "^std::string canonicalize_mesh_weight_vertex" src/editor/mesh_weight_model.cpp
grep -n "markers.present.empty()" src/samples/editor_project_smoke.cpp
grep -n "OperationExpectation, " src/samples/agent_dispatch_smoke.cpp
grep -n "bool exercise_parameter_operations" src/samples/agent_dispatch_smoke.cpp
grep -n "animation references unknown bone" src/runtime/skeleton_parse.cpp
```

**A citation that no longer resolves to the described construct is a blocking
finding**, written into `AGENTS.md`'s "Document errors found" table in §A.1's
form, before anything else continues.

Three anchors matter more than the rest and are called out by name:

- **The marker gate and MAR-185's registration point.** MAR-186's suite registers
  **after** MAR-185's `validate_mar185_inherit_editing`, which registers after
  `validate_mar184_inherit_overlays`. Record the new line.
- **`apply_animation_edits`' namespace.** If MAR-185 or a review pass moved it out
  of the anonymous namespace, design §2.5's whole rationale changes and
  `authored_animation_names` may not be needed. Re-run A7.
- **The registry count and its thirteen witnesses.** MAR-185 moves them to 66.
  Record whatever they are; MAR-186 must leave them there (Task 9).

### 0.2 Baseline build and inventory

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build -N | tail -3          # 22 at 6986024; record whatever it is
ctest --test-dir build --output-on-failure | tail -5
```

MAR-186 registers **no** new CTest target, so the number must be identical at
Task 10.

### 0.3 **MAR-185 landing gate**

```bash
git log --oneline --grep="MAR-185" -1
grep -n "TimelineKeyKind::Inherit" include/marrow/editor/authoring.hpp
grep -rn "bone_inherit_timeline_edits" src/editor/authoring.cpp | head
```

Record whether MAR-185 landed. **If it did not**, MAR-186 is still implementable
(design §0.2): the only consequences are that the registry number stays at 64 and
that a `delete_animation` in an unrelated suite may leave an inherit overlay
behind. Write the decision down; do not silently assume either state.

### 0.4 **A10 gate — the vocabulary does not already exist, and the compiler is NOT silent**

```bash
# -w is load-bearing: SpineImportDiagnosticSeverity already exists (A7).
grep -rnw "DiagnosticSeverity\|DiagnosticPanel\|DiagnosticCode\|DiagnosticOverlayFamily\|safe_fix_id\|issue_count" \
     src/ include/ tools/                                                    # expect 0
grep -rn "SpineImportDiagnosticSeverity" src/ | wc -l                        # expect >0 -- the reason for -w
grep -rn "panel_focus\|PanelFocus\|focused_panel\|FocusedPanel" src/ include/  # expect 0
grep -nE "Wall|Wextra|Wswitch|Werror" CMakeLists.txt                         # expect only :352
```

**Then compile, do not infer.** The first draft of this gate asked the
implementer to *prove the compiler is silent*, on a premise inherited from the
team lead's brief and never tested (A4). It is wrong:

```bash
printf 'enum class E{A,B,C};\nint f(E e){switch(e){case E::A: return 1; case E::B: return 2;} return 0;}\n' > /tmp/wsw.cpp
c++ -std=c++17 -c /tmp/wsw.cpp -o /tmp/wsw.o     # EXPECT: warning: ... [-Wswitch]
```

**Expected: the warning appears with no flags.** Paste it. `CMakeLists.txt`
carrying no `-Wall` is irrelevant — Clang enables `-Wswitch` by default, and
MAR-185 measured **25** real warnings from a throwaway `TimelineKeyKind` value
(`AGENTS.md:305-334`).

**If the warning does NOT appear**, the toolchain differs from the one measured
here and design §1.6 must be re-planned before Task 1 — the four name functions
are specified as `default:`-less switches precisely because the diagnostic fires.

Finally, record the thing that is genuinely unpoliced, because Task 2 owes it a
guard: **a fixed-length list of calls.** `grep -n "clipboard_track_count" src/`
and read it — MAR-185 shipped a six-term sum there that was never extended to the
seventh timeline family. `collect_orphan_animation_overlays`' seven family calls
are MAR-186's member of that class.

### 0.5 The registry non-effect baseline

```bash
grep -c '^\s*{\s*"' src/editor/agent_dispatch.cpp
grep -n "OperationExpectation, " src/samples/agent_dispatch_smoke.cpp
grep -rn "!= 6[46]U" src/
grep -nE "== 6[46]" tools/mcp/test_client.py
```

Write the four numbers down. Measured at `5a56663`, after MAR-185:
**66 rows / array bound 66 / eleven `!= 66U` guards / two Python `== 66`.** The
guard count is **eleven**, not the ten recorded before MAR-185 — re-summed rather
than carried forward (A6's lesson). **This story moves none of them.** Task 9 diffs
against this list. The look-alikes that must not be mistaken for count sites,
carried forward from MAR-185's Task 0.5: `shell_smoke_graph.cpp`'s `4364U`,
`shell_smoke_viewport.cpp`'s `640`, `IM_COL32(56, 61, 69, 255)`,
`IM_COL32(208,134,57,230)`, `(51, 56, 64)`, `rgb(54,57,64)`, `"x": 56.0`,
`56,995,840`, `PhysicsBoneState … 56 bytes/bone`, `AGENTS.md`'s `t = 0.62` and
its self-referential line counts, and `tools/mcp/test_client.py`'s ordinal.

### 0.6 **A6 gate — the non-effect witness and the zero-issue precondition**

```bash
./build/marrow_project_smoke assets/fixtures/player_idle.marrow > /tmp/mar186-before.txt 2>&1
shasum -a 256 assets/fixtures/player_idle.marrow
python3 -c "
import json
d=json.load(open('assets/fixtures/player_idle.marrow'))
print('editor  :', {k:d.get('editor',{}).get(k) for k in ('active_animation','preview_skins')})
print('mesh    :', sorted(d.get('mesh_edits',{}).keys()))
print('overlays:', sorted(d.get('timeline_edits',{}).get('animations',{}).keys()))
print('anim_ed :', d.get('animation_edits'))
s=json.load(open('assets/fixtures/player_idle.mskl'))
print('skl anim:', sorted(s['animations'].keys()))
print('skl skin:', sorted(k['name'] if isinstance(k,dict) else k for k in
                          (s['skins'] if isinstance(s['skins'],list) else s['skins'])))
"
```

Expected at `6986024`: `active_animation "idle"`, `preview_skins ["default"]`,
**no** `mesh_edits`, overlays only on `idle`, **no** `animation_edits`, skeleton
animations `aim/attack/idle`, skins `default/mage/mage_arm/mesh_base/warrior`.
**Every one of those is a precondition for `player_idle.marrow` yielding zero
issues.** If MAR-185 changed the fixture — it must not; the smoke **aborts** on a
partial marker match — stop and record.

Then record the byte length and SHA-256 of
`serialize_project(load_project("assets/fixtures/player_idle.marrow").project)`.
G0 prints both every run.

### 0.7 **A4 gate — is canonicalization a bit-exact fixed point?**

Build the mesh weight edit for `mesh_base`/`body`/`body_mesh` from the runtime
with `mesh_weight_edit_from_runtime`, canonicalize every vertex once, copy,
canonicalize again, and compare **every** field of **every** influence with `==`.

**Expected: byte-identical, and the returned error empty both times.** Paste the
result. **If the second pass changes anything, design §2.6's `==` predicate is
wrong**, Task 4 changes shape, and the story stops to re-plan rather than
silently adopting a tolerance.

### 0.8 **A5 gate — is the `Error` severity reachable from a file?**

Take that same edit, replace vertex 0's influences with a single
`{bone_name: "spine", x: 0, y: 0, weight: 1e-9}`, `save_project` to a scratch
path, then `load_project(path)`.

**Expected: the save succeeds and the load succeeds**, because
`parse_mesh_weight_vertices` and `validate_project_for_save` require only
`weight > 0`, and `skeleton_parse.cpp:1633-1640` requires only `> 0` before
normalizing at runtime. **If the load fails**, G8 becomes an in-memory case and
`AGENTS.md` records `weights.uncanonicalizable` as unreachable from a file.
Either way, write the answer down.

### 0.9 **A7 gate — `apply_animation_edits` is out of reach**

```bash
grep -n "^namespace\|^} // namespace" src/editor/project.cpp
grep -n "^void apply_animation_edits" src/editor/project.cpp
```

Confirm the definition falls **inside** the first `namespace { … }` block. This
is the same hazard MAR-184 recorded as its D2 for `finite_animation_scalar`. If
it is now reachable, `authored_animation_names` may be unnecessary — record and
re-plan Task 1.

### 0.10 **A8 gate — the four parser rules that draw AC2's boundary**

```bash
grep -n "animation references unknown bone" src/runtime/skeleton_parse.cpp   # 1 hit  (:5270)
grep -n "animation references unknown slot" src/runtime/skeleton_parse.cpp   # 2 hits (:5373, :5430)
grep -n "mesh weight references unknown bone" src/runtime/skeleton_parse.cpp # 1 hit  (:1616)
```

**FOUR hits, not three** (A5). The `slots` walk and the separate `deform` walk
emit the **same message text** from two different sites, which is exactly how the
first draft undercounted — and a gate written for three **fails on a correct
tree**, sending the implementer after a regression that does not exist.

Each must still be a `return validation_error(...)`. **If any became a
`continue`, design §1.2 is wrong**, a whole class of orphans becomes openable, and
§2.9's "bone and slot selections always resolve" guarantee is void.

### 0.11 **A9 gate — the transposition is real**

```bash
grep -n "struct AttachmentSelection" -A 5 include/marrow/editor/selection.hpp
grep -n "struct MeshWeightAttachmentEdit" -A 5 include/marrow/editor/project.hpp
```

Expect `slot_name, skin_name, attachment_name` versus
`skin_name, slot_name, attachment_name`. If they now agree, I10 is
non-reproducing and is recorded as such — **not** replaced with a convenient
substitute.

### 0.12 The UI-free witness, inverted

```bash
test -f src/editor/diagnostics.cpp && echo PRESENT || echo ABSENT     # expect ABSENT
```

At Task 9 the same file must exist and
`grep -nE "imgui|shell_|sokol|ImGui" src/editor/diagnostics.cpp` must return
nothing.

### 0.13 **The inversion harness — object deletion, not `touch`**

Write this script now and use it for **every** inversion in §B. It is the
mitigation for the H1 extension MAR-184 recorded.

```bash
# /tmp/mar186-rebuild.sh  --  delete the object, then build.
set -e
for o in \
  build/CMakeFiles/marrow_editor.dir/src/editor/diagnostics.cpp.o \
  build/CMakeFiles/marrow_editor.dir/src/editor/project.cpp.o \
  build/CMakeFiles/marrow_editor.dir/src/editor/agent_handlers_inspection.cpp.o \
  build/CMakeFiles/marrow_editor.dir/src/editor/mesh_weight_model.cpp.o \
  build/CMakeFiles/marrow_project_smoke.dir/src/samples/editor_project_smoke.cpp.o \
  build/CMakeFiles/marrow_agent_dispatch_smoke.dir/src/samples/agent_dispatch_smoke.cpp.o
do rm -f "$o"; done
cmake --build build
```

Run it **after the mutation and again after the restore**. Do **not** rely on
`touch`: the generator is `Unix Makefiles` and `make --version` is **GNU Make
3.81**, whose one-second mtime granularity is precisely what produced MAR-184's
nine false "did not bite" readings. The asymmetry is what makes it dangerous — a
stale binary can only produce a **false pass**, never a false failure, so every
"it did not bite" reading taken without object deletion is worthless.

### Task 0 exit criteria

- [ ] **0.1 re-anchor gate**: every anchor in §A.3 re-resolved, the old → new
      mapping recorded, and every non-resolving citation written into `AGENTS.md`
      as a blocking finding. The marker gate, `apply_animation_edits`' namespace,
      and the registry witnesses are called out individually.
- [ ] HEAD recorded; `git status --short` clean; `ctest -N` total recorded.
- [ ] **0.3**: MAR-185's landing state recorded in writing.
- [ ] **0.4 (A10)**: the four greps run **and the `-Wswitch` probe compiled**, all
      outputs pasted. A gate answered by inference rather than by `c++` is A4
      repeating itself.
- [ ] **0.5**: the four registry numbers written down.
- [ ] **0.6 (A6)**: the fixture facts confirmed; `serialize_project` length + SHA
      recorded.
- [ ] **0.7 (A4)**: the fixed-point answer pasted.
- [ ] **0.8 (A5)**: the `1e-9` reachability answer pasted.
- [ ] **0.9 (A7)**, **0.10 (A8)**, **0.11 (A9)**: answered in writing.
- [ ] **0.13**: the rebuild script written and its object paths verified to exist
      after a normal build.

---

## Task 1 — The vocabulary, the helper, and an empty collector

**Depends on:** Task 0.

Files: `include/marrow/editor/diagnostics.hpp` (**new**),
`src/editor/diagnostics.cpp` (**new**), `include/marrow/editor/project.hpp`,
`src/editor/project.cpp`, `CMakeLists.txt`.

This task adds **no behaviour a test can see** — it is the substrate every later
task stands on, and it is a task rather than a preamble because it is the only
place `CMakeLists.txt` changes.

### 1.1 The header

Design §2.2's declarations verbatim — **four** enums, not two:
`DiagnosticSeverity`, `DiagnosticPanel`, and the two `plan-mar187`'s D6 requires,
**`DiagnosticCode` and `DiagnosticOverlayFamily`** (A8) — plus
`kSafeFixRemoveOrphanOverlay`, `kSafeFixNormalizeWeights`,
`kSafeFixResetPreviewReference`, `is_allowlisted_safe_fix`,
`diagnostic_severity_name`, `diagnostic_panel_name`, `diagnostic_code_name`,
`diagnostic_overlay_family_name`, and the two entry points.

- **`DiagnosticIssue::code` is a `DiagnosticCode`, not a `std::string`, and
  `DiagnosticIssue::family` is a `DiagnosticOverlayFamily`.** Design §2.11: a fix
  handler holding only `safe_fix_id` cannot find its `ProjectData` vector except
  by splitting `identity` on `|`, which the escaping exists to make unsafe.
  Shipping these here — rather than letting MAR-187 amend this header from
  downstream — is the correct direction of dependency.
- Each of the four name functions is an exhaustive `switch` with **no `default:`
  arm**. A `default:` silences `-Wswitch`, which A4 measured as *on by default* —
  the one automatic check this module gets. Do not add one.
- `DiagnosticPanel` ships **three** values — `Project`, `Timeline`, `Weights`.
  Not five. Design §2.9 explains why dead enumerators would be worse than an
  honest extension point, and `-Wswitch` turns MAR-187's extension into a checklist.
- The header **forward-declares** `class EditorSession` rather than including
  `marrow/editor/session.hpp`, so the pure collector does not drag the session
  into every consumer.
- A doc comment on the four enums records design §1.6 **as corrected**: exhaustive
  switches over these types are compiler-policed; every one lives in
  `src/editor/diagnostics.cpp`;
  `grep -rn "DiagnosticPanel::\|DiagnosticSeverity::\|DiagnosticCode::\|DiagnosticOverlayFamily::"`
  finds them all.

### 1.2 `authored_animation_names`

Declared in `project.hpp` beside `build_project_runtime_document`; defined in
`project.cpp` **after** the anonymous namespace closes (`:6862` at `6986024`) so
`apply_animation_edits` is visible. It copies `base_skeleton_document.root`, calls
`apply_animation_edits(&copy, project.animation_edits)`, and returns the sorted
keys of the copy's `animations` object.

**Do not reimplement the Create/Rename/Delete/SetDuration/Unknown fold.** Design
§2.5; MAR-184's D2 is the precedent for what happens when an unreachable helper
is re-derived instead. The whole-root copy is deliberate: a `Rename` also rewrites
`mixing` references (`project.cpp:5450`).

### 1.3 The empty collector

`collect_project_diagnostics` returns a default `DiagnosticReport`;
`collect_session_diagnostics` returns `std::nullopt` unconditionally. Both
compile, neither does anything. This is the tree Task 2's first case fails against.

### 1.4 CMake

One line: `src/editor/diagnostics.cpp` in `add_library(marrow_editor STATIC …)`,
after `src/editor/mesh_weight_model.cpp`. **No new target. No new test. No new
include directory.**

### 1.5 Verify

```bash
cmake -S . -B build && cmake --build build
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_agent_dispatch_smoke
ctest --test-dir build -N | tail -1        # identical to Task 0.2
```

All green and **unchanged** — nothing calls the collector yet, so this task must
be behaviour-neutral. A test that changes here is a bug.

### 1.6 Inversions

**None from this task alone.** Say exactly this in the task log rather than
inventing one. `authored_animation_names` is attributed to I18 → G2 and is run in
Task 2.

---

## Task 2 — Orphan animation overlays, and `finalize`

**Depends on:** Task 1.

Files: `src/editor/diagnostics.cpp`, `src/samples/editor_project_smoke.cpp`.

### 2.1 Test first — G0, G1, G2

Add `validate_mar186_project_diagnostics(const marrow::editor::ProjectLoadResult&)`
following the file's shape (`validate_mar178_scenario_*`,
`validate_mar184_inherit_overlays`), registered in `main()`'s **marker-gated
`else`** after MAR-185's suite. Per the standing rules that is the only place it
can run.

Throwaway projects use `create_minimal_project` with
`skeleton_path = absolute("assets/fixtures/player_idle.mskl")`,
`atlas_paths = { absolute("assets/fixtures/player_idle.matl") }`, into a scratch
directory, then are mutated in memory.

**G0 — the non-effect and zero-mutation witness.** Two halves:
1. Over the `result` `main()` already holds: `collect_project_diagnostics` returns
   **zero** issues, `error_count == 0`, `warning_count == 0`; and
   `serialize_project(*result.project)` has exactly Task 0.6's byte length and
   SHA-256. Print both every run.
2. Over an `EditorSession` opened on the same path: snapshot `serialize_project`,
   `dirty()`, `undo_count()`, `redo_count()`, `project_revision()`,
   `runtime_revision()`, `preview_revision()`; call `collect_session_diagnostics`;
   assert all seven **bit-identical**.

> **G0 is expected to PASS on its first run.** It is a witness, not a driver.
> Its falsifiability is demonstrated by **I21** (a `const_cast` that seeks the
> preview) and **I22** (an inverted stale-skin test), both run in this task and
> in Task 5 respectively. Do not treat its first-run pass as a defect, and do not
> treat it as covered until I21 has been run.

**G1 — all seven orphan families.** One overlay in each of
`transform_timeline_edits` (bone `arm_l`, channel `Rotate`),
`bone_inherit_timeline_edits` (bone `arm_l`),
`mesh_deform_timeline_edits` (slot `body`, attachment `body_mesh`),
`draw_order_timeline_edits`, `event_timeline_edits`,
`slot_color_timeline_edits` (slot `body`), `slot_attachment_timeline_edits`
(slot `body`) — **all naming animation `ghost`**, which `player_idle.mskl` does
not contain. Assert:
1. exactly **seven** issues;
2. the **full sorted identity list** equals the seven expected literals, and a
   mismatch is reported as a **set difference naming the missing and unexpected
   identities**, never as a bare count;
3. every `code == DiagnosticCode::OverlayOrphanAnimation`, every severity
   `Error`, every `safe_fix_id == "remove_orphan_overlay"`, and **each issue's
   `family` equal to the vector it came from** — `Transform`, `Inherit`, `Deform`,
   `DrawOrder`, `Event`, `SlotColor`, `SlotAttachment`, one each *(I26)*;
4. `identity[i] < identity[i+1]` strictly, for every adjacent pair;
5. `error_count == 7`, `warning_count == 0`.

The seven expected identities, in sorted order:

```
overlay.orphan_animation|deform|ghost|body|body_mesh
overlay.orphan_animation|draw_order|ghost
overlay.orphan_animation|event|ghost
overlay.orphan_animation|inherit|ghost|arm_l
overlay.orphan_animation|slot_attachment|ghost|body
overlay.orphan_animation|slot_color|ghost|body
overlay.orphan_animation|transform|ghost|arm_l|rotate
```

*(I1 → assertions 1-2; I2a-g → assertion 2; I3 → assertion 2 via order; I6 →
assertion 2 via the appended index; I26 → assertion 3's `family` half.)*

**G2 — `authored_animation_names` is the authority.** G1's project plus
`AnimationEdit{Create, "ghost"}` → **zero** issues. Then
`{Create, "ghost"}` followed by `{Delete, "ghost"}` → the seven return.
*(I18 → the first half.)*

**The two invariants**, run at the end of every case in every task:
`identity[i] < identity[i+1]` strictly, and every non-empty `safe_fix_id`
satisfies `is_allowlisted_safe_fix`.

### 2.2 Watch it fail

```bash
cmake --build build --target marrow_project_smoke
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

G1 must fail with `returned 0 issues, expected 7` against Task 1's empty
collector. G0 passes — see the note above.

### 2.3 Implement

In `diagnostics.cpp`: `escape_identity_token`, `join_identity`, `make_issue`,
`collect_orphan_animation_overlays` (seven family calls through one generic
lambda), and `finalize` (sort by identity, adjacent-unique de-duplication,
severity counts).

- **De-duplication is adjacent-unique over the sorted range**, not a hash set.
  That is what makes I3 observable at G9(c) and keeps `finalize` a single pass.
- `escape_identity_token` escapes `\` → `\\` **before** `|` → `\|`. Escaping in
  the other order double-escapes a backslash that precedes a pipe.
- The seven family calls are written out explicitly, in the order of design
  §2.5's table, under `kSweptOverlayFamilyCount` and the `static_assert` from
  design §2.5, with the comment naming §1.6's **corrected** finding: the enums are
  compiler-policed, **this list is not**, and MAR-185 shipped `clipboard_track_count`
  into that exact gap. Each call also sets the issue's
  `DiagnosticOverlayFamily`, which G1 asserts per family.

### 2.4 Verify + inversions

```bash
/tmp/mar186-rebuild.sh && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

Run **I1 → G1**, **I2a-g → G1** (all seven, one at a time, seven recorded
messages), **I3 → G1**, **I6 → G1**, **I26 → G1**, **I18 → G2**, **I21 → G0**. Each: mutate,
`/tmp/mar186-rebuild.sh`, run, capture the failure text to a file, restore,
`/tmp/mar186-rebuild.sh`, confirm green.

For **I3** and **I6**, the uniqueness demonstrations are deferred to Tasks 5 and 3
respectively, because the independent detectors do not exist yet. Say so in the
task log.

---

## Task 3 — Identity properties and the round trip

**Depends on:** Task 2. Files: `src/samples/editor_project_smoke.cpp` **only** —
Task 2 already wrote the code; this task is what makes two of its properties
observable.

### 3.1 Test — G3, G4, G5, G12

**G3 — identity is position-independent.** Two projects with the same single
orphan transform overlay: in one it is `transform_timeline_edits[0]`; in the other
it is `[2]`, after two legitimate overlays on `idle`. Assert both lists are
**non-empty**, equal to each other, and equal to the single expected identity.
*(I6's independent detector.)*

> The non-empty assertion is load-bearing. Under **I1** both lists are empty and
> equal, and a case that only compares them to each other passes for the wrong
> reason — the H4 shape.

**G4 — identity across unchanged revisions.** Collect twice over one unchanged
project; assert the two `issues` vectors are equal member-by-member: code,
severity, identity, message, panel, selection (including the variant alternative),
`animation_name`, `vertex_index`, `safe_fix_id`.

> **G4 is a witness with no story-owned inversion** (§B.1). It is expected to
> pass on its first run. AC1 names the property, so the case exists; do not
> invent a mutation for it.

**G5 — the round trip, and the resurrection.** G1's seven-orphan project through
`save_project` → **`load_project(path)`**. Assert the seven identities survive
byte-for-byte, and that the reloaded `result.skeleton_data->find_animation("ghost")`
is **non-null** — the resurrection F1 exists to report, and the concrete reason
the issue is `Error` rather than `Warning`.

> **G5 is an integration witness with no story-owned inversion** (§B.1). It runs
> over `save_project`, `load_project` and `build_runtime_document`, none of which
> MAR-186 touches. Its value is proving the identities are computed from data that
> survives a round trip, and that the phantom animation is real.

**G12 — the escaping hazard.** Two orphan transform overlays whose bone names are
chosen so that unescaped joining collides — e.g. bone `a|b` on channel `rotate`
and bone `a` with a `|b|rotate`-shaped continuation. Assert **two** issues survive
and their identities differ. *(I5.)*

> Assert the **count of two** as well as the two identities: under I5 the
> collision makes de-duplication **delete** one issue, so a case that only checked
> "both expected identities are present" would report a missing identity rather
> than the collision, and a case that only compared identities pairwise would see
> one element.

Bone names must exist in `player_idle.mskl` or the project will not open
(§A.3's parser rules). Since `a|b` does not, **G12 authors its own throwaway
skeleton is not an option** — the project smoke has one invocation. Instead G12
uses the **animation** name for the collision (`ghost|x` versus `ghost`), which
is unconstrained: an orphan animation name never has to resolve. State this in a
comment; it is the reason G12 collides on animations rather than bones.

### 3.2 Watch them fail

G3, G4 and G5 pass immediately (all three are properties Task 2 already
satisfies); G12 **fails** against Task 2 only if the escaping was omitted. If Task 2's
`escape_identity_token` is already correct, G12 passes on its first run — say so,
and rely on **I5** for its falsifiability.

### 3.3 Verify + inversions

Run **I5 → G12**. Then the deferred uniqueness demonstration for **I6**: apply
I6, neuter G1's identity comparison with `if (false && …)`, `/tmp/mar186-rebuild.sh`,
and confirm **G3** is the case that fails. Restore both.

---

## Task 4 — Weights: orphan targets, canonicality, and the supersession

**Depends on:** Task 3.

Files: `src/editor/diagnostics.cpp`, `src/samples/editor_project_smoke.cpp`.

### 4.1 Test first — G7, G8

Both build their edit with
`mesh_weight_model::mesh_weight_edit_from_runtime(skeleton, "mesh_base", "body", "body_mesh")`
and then perturb it. **A vertex count that does not match the attachment makes the
project unopenable** (`skeleton_parse.cpp`: *"mesh weights must contain one vertex
influence list per vertex"*), so the edit must be derived from the runtime, never
hand-built.

**G7 — non-canonical weights, four sub-cases.**
1. **Control.** The edit untouched → **zero** issues. *(States in a comment that
   this is what makes sub-cases 2-4 mean something: without it, a detector that
   fires on everything would pass them all.)*
2. Vertex 0's weights scaled to sum `0.7` → exactly one `weights.non_canonical`,
   Warning, `normalize_weights`, identity
   `weights.non_canonical|mesh_base|body|body_mesh|0`.
3. Vertex 0's influences reversed (ascending weight) → the same one issue.
4. Vertex 0's largest weight perturbed by **one ULP** (`std::nextafter`) so the
   sum leaves `kMeshWeightSumTolerance` → the same one issue. *(I7 — this is the
   sub-case a tolerance would swallow, and it is the only reason sub-case 4
   exists.)*

*(I24 → sub-case 2; I25 → the allowlist invariant on sub-case 2.)*

**G8 — uncanonicalizable weights, and the absent fix.** Vertex 0's influences
replaced by a single `{bone_name: "spine", x: 0, y: 0, weight: 1e-9}`. Assert:
1. exactly one issue, `weights.uncanonicalizable`, severity **Error**, identity
   `weights.uncanonicalizable|mesh_base|body|body_mesh|0`;
2. `safe_fix_id` is **empty**;
3. the message contains the canonicalizer's own text
   `"A weighted vertex must keep at least one positive influence."`;
4. `save_project` → **`load_project(path)`** succeeds and the issue survives the
   round trip. *(Task 0.8 already answered whether this is reachable. If it said
   no, drop assertion 4 and record it.)*

*(I8 → assertion 2.)*

### 4.2 Watch them fail

Compile-clean, assertion failures: G7 sub-case 2 with `0 issues, expected 1`.

### 4.3 Implement

`collect_orphan_weight_targets` and `collect_weight_canonicality`, both called
from `collect_project_diagnostics`.

- The orphan test resolves the triple against the **materialized skeleton**:
  `skeleton.find_slot_index(edit.slot_name)`, then
  `skeleton.find_attachment(edit.skin_name, *slot_index, edit.attachment_name)`.
  Design §2.5 records the measurement that licenses this: `ensure_object_member`
  is used only on `animations`, on `bones`/`slots`/`deform` **within** an
  animation, and never on the skeleton's skins, slots or attachments, so those
  come from the base document unchanged.
- **The supersession**: when an edit's target does not resolve, emit
  `overlay.orphan_weight_target` and **do not examine its vertices**. One edit,
  one issue.
- The canonicality test copies the vertex, calls
  `canonicalize_mesh_weight_vertex(skeleton, &copy)`, and compares every field
  with `==`. Task 0.7 is what licenses `==`; put its answer in a comment.
- `AttachmentSelection{edit.slot_name, edit.skin_name, edit.attachment_name}` —
  **slot first**. Write the field names in a comment; the two structs are
  transposed (Task 0.11).

### 4.4 Verify + inversions

Run **I7 → G7** (sub-case 4), **I24 → G7** (sub-case 2), **I25 → G7**,
**I8 → G8**. For I24, demonstrate uniqueness: neuter G7 with `if (false && …)`
and confirm **G8** is still the case that fails.

---

## Task 5 — Stale preview references, and the supersession's other half

**Depends on:** Task 4.

Files: `src/editor/diagnostics.cpp`, `src/samples/editor_project_smoke.cpp`.

### 5.1 Test first — G9

Five sub-cases, each its own throwaway project:

| # | Fixture | Assert |
| --- | --- | --- |
| **(a)** | `active_animation = "ghost"`, no overlays | exactly one issue, `preview.stale_animation`, identity `preview.stale_animation\|ghost`, Warning, `reset_preview_reference`, panel `Project`, `animation_name == "ghost"` |
| **(b)** | `active_animation = "ghost"` **plus** G1's seven orphan overlays | the seven orphan issues and **no** `preview.stale_animation` — the overlays resurrect `ghost` in the materialized skeleton, so the preview genuinely resolves *(I12)* |
| **(c)** | `preview_skins = {"default", "ghost_skin", "ghost_skin"}` | **one** `preview.stale_skin`, identity `preview.stale_skin\|ghost_skin`; `default` produces nothing *(I4, I22's independent detector)* |
| **(d)** | `active_animation = ""` | **zero** issues — an empty active animation is the setup pose, not a stale reference |
| **(e)** | A weight edit for `mesh_base`/`body`/**`ghost_mesh`** whose vertex 0 is also non-canonical | **one** issue, `overlay.orphan_weight_target`, identity `overlay.orphan_weight_target\|mesh_base\|body\|ghost_mesh`, Warning, `remove_orphan_overlay` — the supersession *(I11)* |

Sub-case (b) is the case that proves design §2.7's interaction is deliberate;
its comment must say that fixing the orphan overlay would *create* a
`preview.stale_animation` issue and that MAR-187's revision-keyed refresh is what
surfaces it.

Sub-case (e) is where an unresolvable `AttachmentSelection` is emitted on
purpose: MAR-187's AC3 ("handles removed targets") is its consumer.

### 5.2 Watch it fail

(a) fails first with `0 issues, expected 1`.

### 5.3 Implement

`collect_stale_preview_references`: the `active_animation` half against
`skeleton.find_animation`, the `preview_skins` half against
`skeleton.find_skin_index`, skipping empty strings in both. **The materialized
skeleton is the authority for both** (design §2.7) — put the reason in a comment,
because resolving against `authored_animation_names` here is a plausible and
wrong symmetry with Task 2.

De-duplication of repeated skin names is `finalize`'s job, not this function's;
emitting one issue per vector entry and letting `finalize` collapse them is what
makes **I4** observable at G9(c).

### 5.4 Verify + inversions

Run **I4 → G9(c)**, **I11 → G9(e)**, **I12 → G9(b)**, **I22 → G0**. Then the
deferred uniqueness demonstrations:

- **I3** (no sort): neuter G1, `/tmp/mar186-rebuild.sh`, confirm **G9(c)** fails
  because adjacent-unique de-duplication no longer sees the duplicate adjacently.
- **I22**: neuter G0, confirm **G9(c)** fails with `default` reported instead of
  `ghost_skin`.

---

## Task 6 — Typed targets, severity counts, revisions, and the closed session

**Depends on:** Task 5.

Files: `src/samples/editor_project_smoke.cpp` **only** for G6 and G11;
`src/editor/diagnostics.cpp` for the session wrapper G10 needs.

### 6.1 Test first — G6, G10, G11

**G6 — typed targets.** Over G1's seven issues plus G9(e)'s orphan weight issue:
1. every `target.panel`: `Timeline` for the seven, `Weights` for the weight issue;
2. every present `target.selection` fed to `SelectionSet::replace`, then read back
   — `active_bone()->bone_name == "arm_l"` for `transform` and `inherit`;
   `active_slot()->slot_name == "body"` for `deform`, `slot_color` and
   `slot_attachment`; and for the weight issue, `active_attachment()`'s
   **`slot_name == "body"`, `skin_name == "mesh_base"`, `attachment_name ==
   "ghost_mesh"`, each read by name**;
3. `draw_order` and `event` carry **no** selection;
4. every orphan issue's `animation_name == "ghost"`.

*(I10 → assertion 2's by-name reads; I23 → assertion 1.)*

**G10 — severity counts and revisions.** A project carrying **one Error and two
Warnings** — asymmetric on purpose, stated in a comment, because a symmetric
fixture makes **I14** invisible. Assert over the pure collector:
`error_count == 1`, `warning_count == 2`, `issues.size() == 3`. Then over an
`EditorSession` opened on the saved file:
1. `report->project_revision == session.project_revision()` and
   `report->runtime_revision == session.runtime_revision()`;
2. `report->project_dirty == session.dirty()`;
3. on a **dirty** session, `warning_count == 3` — the two project warnings plus
   `project.unsaved_changes` — and the extra identity is exactly
   `project.unsaved_changes` *(I13)*;
4. after one edit through the session, `report->project_revision` **advanced** and
   the issue list changed as expected.

**G11 — a session that is not normally opened.** A default-constructed
`EditorSession`; assert `collect_session_diagnostics` returns `std::nullopt`.
*(I16.)*

### 6.2 Watch them fail

G10's session half fails first — `collect_session_diagnostics` still returns
`std::nullopt` unconditionally from Task 1.

### 6.3 Implement

`collect_session_diagnostics`: the four-way opened-session guard
(`has_project()`, `project()`, `runtime_data()`, `base_skeleton_document()`),
then `collect_project_diagnostics`, then `project_dirty`, the two revisions, and
— **only when `session.dirty()`** — the `project.unsaved_changes` Warning
appended, followed by a **re-sort and a re-count**. Appending to a sorted vector
without re-sorting is the bug I13 describes from the other side; do both.

### 6.4 Verify + inversions

Run **I10 → G6**, **I23 → G6**, **I13 → G10**, **I14 → G10**, **I16 → G11**.

---

## Task 7 — The agent payload

**Depends on:** Task 6.

Files: `src/editor/agent_handlers_inspection.cpp`,
`src/samples/agent_dispatch_smoke.cpp`.

### 7.1 Test first — A1-A5

A local helper `array_member(const json::Value*, std::string_view)` beside
`member` (`:115-119`).

| # | Content |
| --- | --- |
| **A1** | On `player_idle.marrow`: the payload carries `error_count`, `warning_count`, `project_dirty`, `review_queue_count`, `issue_count` and `issues`, with the right JSON types; `issues` is an **empty array** and `issue_count == 0`. *(I15 → the `review_queue_count` member.)* The existing `expect_scene_contains(… "error_count")` at `:1082-1085` stays as-is |
| **A2** | `warning_count == (project_dirty ? 1 : 0)` and `error_count == 0`, asserted **before** any edit and again **after** an edit that dirties the session. This is AC3's numeric half. *(Second detector for I13.)* |
| **A3** | A throwaway `.marrow` written under `/tmp`, carrying one orphan transform overlay (Error), one non-canonical weight vertex (Warning) and one stale preview skin (Warning), opened with the `exercise_parameter_operations` pattern — `marrow_editor_project_load`, `harness.set_project`, assertions, `marrow_editor_project_destroy`. Assert `error_count == 1`, `warning_count == 2 + (project_dirty ? 1 : 0)`, `issue_count == issues.size()`, and, for each of the three issues, `code`, `severity`, `identity`, `target.panel`, and `target.selection.kind`. *(I17 → `issue_count == issues.size()`; second detector for I14.)* |
| **A4** | In a project carrying one `weights.uncanonicalizable` issue: `json::find_member(issue, "safe_fix_id") == nullptr` — the key is **absent**, not empty. *(I9. Second detector for I8.)* |
| **A5** | Three consecutive `project.diagnostics` calls on A3's project: `project_dirty`, `issue_count` and the full `issues` array identical each time, and a following `runtime.validate` still passes. AC6's zero-mutation half at the agent layer |

The array bound at `:39`, the **eleven** `!= 66U` guards, the two Python assertions, and
every existing `project.diagnostics` assertion at `:3800-3890` are **untouched**.
Those become MAR-186's regression witnesses that the legacy members did not move.

### 7.2 Watch them fail

A1 fails first: the payload has no `issues` member.

### 7.3 Implement

Rewrite the `op == "project.diagnostics"` branch:

```
auto report = collect_session_diagnostics(session);
if (!report) return make_error("Project diagnostics are unavailable.", op, spec,
                               "diagnostics_unavailable");
// four legacy members, exact names and expressions
// issue_count, then issues as an array of objects
```

- `error_count` and `warning_count` come from the report; `project_dirty` is
  `session.dirty()`; `review_queue_count` is `control.review_queue.size()` —
  **verbatim from the shipped branch**, not re-derived.
- Each issue object: `code` (`diagnostic_code_name`), `family`
  (`diagnostic_overlay_family_name`, omitted when `None`), `severity`
  (`diagnostic_severity_name`), `identity`,
  `message`, `target` (an object with `panel`, optional `animation`, optional
  `vertex_index`, optional `selection` `{kind, …}`), and `safe_fix_id` **only
  when non-empty**.
- The `diagnostics_unavailable` branch is unreachable through the dispatcher
  (§B.1) and is written to mirror `runtime.validate`'s null-document guard.

### 7.4 Verify + inversions

```bash
/tmp/mar186-rebuild.sh && ./build/marrow_agent_dispatch_smoke
```

Run **I15 → A1**, **I17 → A3**, **I9 → A4**. For **I8**, the deferred second-detector
demonstration: neuter G8 and confirm **A4** fails. For **I13**, confirm **A2** is a
second detector by neutering G10.

---

## Task 8 — MCP parity

**Depends on:** Task 7. Files: `tools/mcp/test_client.py`,
`tools/mcp/tools/inspection.py`.

### 8.1 Test first — M1

At the existing `require_ok("project.diagnostics", …)` (`:482`), keep the call and
capture its result. Assert:
1. `scene_delta` carries all six members with the right Python types;
2. `issue_count == len(issues)`;
3. the schema half: `inspection.get_tools()` still contains a `project.diagnostics`
   tool whose `inputSchema["properties"]` is **empty**. *(I19.)*

Add a comment, in the shape of the MAR-179 comment at `:57-70`, recording that
the two registry-count assertions at `:53,55` **cannot see this story** — MAR-186
adds no operation and no tool — and that the schema assertion exists because
`MarrowClient.send_command` never consults `inputSchema`, so a wire-level test
alone would pass over a schema that grew a bogus argument.

### 8.2 Implement

`tools/mcp/tools/inspection.py`: update the `project.diagnostics` tool's
**`description`** to mention structured issues. Its `inputSchema` stays
`{"type": "object", "properties": {}}`. **No tool added, none removed.**

### 8.3 Verify + inversion

```bash
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
python3 -m py_compile tools/mcp/server.py tools/mcp/test_client.py \
                      tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
```

Run **I19 → M1's schema half**.

---

## Task 9 — Prove the scope did not leak, and the counts did not move

**Depends on:** Tasks 1-8. Not an assertion — a diff and four greps.

```bash
git diff --stat -- src/editor/timeline_model.cpp src/editor/timeline_model.hpp \
                   src/editor/timeline_graph_model.cpp src/editor/timeline_controller.cpp \
                   src/editor/session.cpp src/editor/shell_timeline.cpp \
                   src/editor/authoring.cpp include/marrow/editor/authoring.hpp \
                   include/marrow/c/ src/c/ include/marrow/runtime/ src/runtime/ \
                   assets/fixtures/
```

Must be **empty**. `assets/fixtures/` especially: `marrow_project_smoke`
**aborts** on a partial marker match, so editing a fixture silently disables
roughly two dozen suites and still exits 0.

`CMakeLists.txt`'s diff must be **exactly one added line**
(`src/editor/diagnostics.cpp`).

The count sweep, **as a non-effect gate**:

```bash
git diff -U0 -- src/ tools/ | grep -E '^[+-]' | grep -E '\b(6[3-7])\b'
grep -c '^\s*{\s*"' src/editor/agent_dispatch.cpp                       # Task 0.5's number
grep -n "OperationExpectation, " src/samples/agent_dispatch_smoke.cpp   # unchanged
grep -rn "!= 6[46]U" src/ | wc -l                                       # 10, unchanged
grep -nE "== 6[46]" tools/mcp/test_client.py                            # two, unchanged
ctest --test-dir build -N | tail -1                                     # Task 0.2's number
```

Every line the first grep returns is inspected **by hand** and explained in the
`AGENTS.md` entry. A bare `64` also matches `t = 0.64` and "64 lines"; Task 0.5's
untouchable-literal list is the reference.

The UI-free gate:

```bash
grep -nE "imgui|ImGui|shell_|sokol" src/editor/diagnostics.cpp          # must be empty
grep -n "include" src/editor/diagnostics.cpp
grep -rn "DiagnosticPanel::|DiagnosticSeverity::" src/ include/ | grep -v diagnostics.cpp
```

The third must return only `agent_handlers_inspection.cpp` (which names the enums
through `diagnostic_*_name`, not through a switch) and the two smoke files — **no
switch outside `diagnostics.cpp`**. That is design §1.6's containment, and it is
what makes MAR-187's sweep a one-file job.

Inversion **I20**: add `#include "shell_state.hpp"` to `diagnostics.cpp`, confirm
the first grep becomes non-empty, restore. **Recorded as a scope check, not as
coverage.**

---

## Task 10 — Full verification

**Depends on:** Tasks 1-9. Run every command, record every output.

```bash
rm -rf build                                   # the from-scratch rebuild
cmake -S . -B build
cmake --build build

# Model layers
./build/marrow_unit_tests
./build/marrow_timeline_model_tests
./build/marrow_timeline_graph_model_tests
./build/marrow_mesh_weight_model_tests
./build/marrow_selection_tests
./build/marrow_preference_tests
./build/marrow_viewport_interaction_tests
./build/marrow_windowing_tests
./build/marrow_pen_input_tests
./build/marrow_agent_socket_tests

# Runtime -- unchanged by this story
./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl

# Project layer -- G0-G12, inside the standing invocation
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke --create /tmp/mar186_created.marrow
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
  --export-runtime /tmp/player_idle_project_export.mskl \
  --export-binary  /tmp/player_idle_project_export.mbin
./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin \
                                 /tmp/player_idle_project_export.mskl

# Agent -- A1-A5
./build/marrow_agent_dispatch_smoke

# Shell -- regression witness only; MAR-186 adds no shell code
MARROW_CONFIG_HOME=/tmp/mar186-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2

# CTest guardrails
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -L runtime
ctest --test-dir build --output-on-failure -L editor
ctest --test-dir build --output-on-failure -R marrow.renderer_link_boundary

# Dependencies and warnings
cmake --build build --target marrow_verify_third_party
cmake --build build --target marrow_constraint_warning_check

# MCP -- M1
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py
python3 -m py_compile tools/mcp/server.py tools/mcp/test_client.py \
                      tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
```

**Do not run `./build/marrow_editor_shell` without `--auto-close`**, and do not
create `~/Library/Application Support/Marrow`.

### Full verification checklist

| # | Check | How |
| --- | --- | --- |
| 1 | Every case G0-G12, A1-A5, M1 ran and passed | Each binary's output names them |
| 2 | **Every inversion in §B was run against the FROM-SCRATCH build, bit the named case, and was restored** | Twenty-six entries, of which I2 is seven separate runs — **thirty-two** recorded messages. Compare each with `cmp`/`diff` over a whole recorded string (H2). Any that did not bite is recorded as such, with how the case was strengthened — **not** quietly dropped |
| 3 | **Every verification build deleted the object file** | `/tmp/mar186-rebuild.sh`, never `touch`. State it explicitly. MAR-184's nine false negatives are the reason |
| 4 | **Uniqueness was demonstrated, not asserted** | I3 (neuter G1 → G9(c)), I6 (neuter G1 → G3), I8 (neuter G8 → A4), I13 (neuter G10 → A2), I22 (neuter G0 → G9(c)), I24 (neuter G7 → G8). Six demonstrations, each recorded |
| 5 | **Falsifiability re-demonstrated after any rewrite** | H3. If any case was rewritten after its inversion ran, re-run that inversion against the final tree |
| 6 | G0's and G4's witness status is stated, not hidden | Both pass on first run by design; §B.1 names them and I21/I22 supply G0's falsifiability |
| 7 | `serialize_project(player_idle.marrow)` unchanged | G0's printed length + SHA equal Task 0.6's |
| 8 | The registry is **unchanged** | Task 0.5's four numbers, re-measured |
| 9 | `ctest -N` total unchanged from Task 0.2 | This story registers no CTest |
| 10 | The untouchable trees are byte-identical; `CMakeLists.txt` is +1 line | Task 9's `git diff --stat` |
| 11 | `src/editor/diagnostics.cpp` names no ImGui, shell or sokol symbol | Task 9's grep |
| 12 | No switch over either new enum exists outside `diagnostics.cpp` | Task 9's third grep |
| 13 | `~/Library/Application Support/Marrow` **ABSENT** after the whole run | `test -e ~/Library/Application\ Support/Marrow && echo PRESENT \|\| echo ABSENT`. PRESENT is a hard failure |
| 14 | Task 0's A4-A10 answers recorded | Pasted verbatim into `AGENTS.md` |
| 15 | `marrow_inspect --compare` → `matches` | Runs over `player_idle`, which carries no diagnostics data |
| 16 | Count sweep inspected by hand | Task 9's grep output, every line explained |

---

## Task 11 — Documentation and commit

**Depends on:** Task 10, complete and green.

### 11.1 `docs/root1/agent-control.md`

Line `:70` (`- \`project.diagnostics\`: Returns lightweight project diagnostics.`)
becomes a description of the real payload: the four preserved summary fields, plus
`issue_count` and `issues`, each issue carrying `code`, `severity`, `identity`,
`message`, `target` and an **optional** `safe_fix_id` drawn from a three-entry
allowlist. State that collection is read-only and never dirties the project.
`:28` is a list of operation names and does not change.

**No `docs/root1/format-spec.md` change** — nothing about the file format moves.

### 11.2 `AGENTS.md`

A `## MAR-186 Collect Structured Project Diagnostics Validation Results` section,
matching the MAR-183 and MAR-184 sections' structure:

- **Opening paragraph.** What the story did and did not touch: no format change,
  no runtime file, no fixture, no CMake **target**, no shell file, no timeline or
  graph model, no `session.cpp`; `.mskl` v1 / `.mbin` v2 / C ABI untouched; the
  registry **unchanged** at Task 0.5's number; `ctest -N` unchanged. And the
  correction the title invites: **"diagnostics" here is a UI-free model function
  and one JSON payload — MAR-186 draws nothing.**
- **"AC2's boundary is the runtime parser's, not this story's."** Design §1.2
  stated as a durable fact with the three `skeleton_parse.cpp` messages quoted,
  so the next reader does not look for orphan-bone reporting that cannot exist.
- **"What the compiler does and does not police."** Task 0.4's finding, stated
  as the correction it is: **Clang's `-Wswitch` is on by default**, so the four
  new enums are policed at every exhaustive switch (and the name functions carry
  no `default:` arm, so they stay policed); what is **not** policed is
  `collect_orphan_animation_overlays`' seven-call family list, the class MAR-185
  shipped `clipboard_track_count` into. Record design §2.12's deferred
  `-Werror=switch` recipe and its compiler-id guard for MAR-187.
- **"What was measured before any code was written."** Task 0's values verbatim:
  the re-anchor mapping, the A4 fixed-point answer, the A5 `1e-9` reachability
  answer, the A6 fixture facts and `serialize_project` SHA, the registry numbers,
  the `ctest -N` total, and the A7-A10 answers.
- **"Results"** — one row per case, G0-G12, A1-A5, M1.
- **"Inversions run"** — all twenty-five entries (thirty-one messages), each with
  the case it bit and the exact text, plus checklist row 4's six uniqueness
  demonstrations. Any that did not bite recorded as such.
- **"Methodology"** — state that **every** verification build deleted the object
  file rather than `touch`ing the source, per H1's extension, and that the final
  run was from `rm -rf build`.
- **"Document errors found"** — §A.1's **eight** rows plus everything Task 0
  turned up. **A4 is recorded with its attribution intact**: the `-Wswitch` claim
  came from the team lead's brief, was adopted uncorrected by MAR-186's first
  draft, and was caught by `plan-mar187` building on top — the traceability is the
  point, because it would otherwise have propagated a third time. Every story from
  MAR-175 on has found between three and nine; if fewer than §A.1 already lists
  are found, one has been lost — say so explicitly.
- **"Not independently covered"**, carrying forward and adding:
  - **§B.1 verbatim**: G4 and G5 as witnesses, G0's byte-identity half, the
    unreachable `base_skeleton_document() == nullptr` sub-condition, the two total
    name functions, and `review_queue_count`'s inherited coverage — each with the
    reason no inversion exists.
  - **The largest orphan class is unreachable** (design §10, first bullet).
  - **`weights.uncanonicalizable` has no repair**, and the F2 supersession hides
    weight problems on an orphaned edit until the orphan is fixed.
  - **Fixing an orphan overlay can create a stale-preview issue** — design §2.7's
    interaction, with G9(b) named as where it is asserted.
  - **AC5's selection half is proved structurally, not by assertion** (design
    §2.10), under H4's rule.
  - **`-Wswitch` warns but does not stop the build.** Clang enables it by
    default and MAR-186 did not promote it to `-Werror`; a missed exhaustive-switch
    arm is a warning in the log, not a failure. **GCC needs `-Wall` for the same
    diagnostic**, so a non-Clang build is silent.
  - **No compiler checks `collect_orphan_animation_overlays`' seven family
    calls.** An eighth `ProjectData` overlay vector added without an eighth call
    produces no diagnostic and no failure from any shipped case — the class
    MAR-185 shipped `clipboard_track_count` into. The `static_assert` beside the
    list is a guard, not a proof.
  - **Nothing is persisted.** A diagnostic is recomputed on demand.
  - Carry forward MAR-183's `shell_main.cpp` frame-body and `commit_path_choice`
    notes and MAR-184's runtime-parser asymmetries unchanged, plus whatever
    MAR-185 added.

Extend the existing `## Current Validation` `marrow_project_smoke` line with a
clause naming MAR-186's diagnostics suite. **Do not add a new command line** —
there is one invocation.

### 11.3 The PRD

Set `MAR-186`'s `status` to `"done"` and `completedAt` to the run's date in
`.agents/tasks/prd-marrow-runtime.json`. Touch no other story.

### 11.4 Commit

One commit for the story.

```bash
git add -A
git commit
```

Subject, matching the arc's Korean convention:

```
feat: MAR-186 구조화된 프로젝트 진단 수집기 구현
```

Body, in Korean, covering: 수집기가 UI 없는 순수 함수 두 개(`collect_project_diagnostics`,
`collect_session_diagnostics`)로 나뉘고 전자는 세션을 전혀 모른다는 것; 고아
오버레이의 판정 권한이 머티리얼라이즈된 스켈레톤이 **아니라**
`authored_animation_names`(`apply_animation_edits` 폴드)라는 것과 그 이유(팬텀
애니메이션은 이미 스켈레톤 안에 있으므로 잘못 고르면 모든 테스트가 통과한다);
런타임 파서가 알 수 없는 본/슬롯을 하드 에러로 막기 때문에 AC2의 "정상적으로 열린
세션" 경계는 이 스토리가 아니라 파서가 그은 것이라는 사실; 정체성(identity)이
저장 위치가 아니라 좌표에서만 파생되고 토큰을 이스케이프한다는 것; 정렬 + 인접
중복 제거로 순서와 유일성을 한 번에 보장한다는 것; `warning_count`가 심각도
집계로 바뀌면서도 `project.unsaved_changes` 경고 이슈 덕분에 기존 값과 수치적으로
동일하게 유지된다는 것; 세이프 픽스 ID는 MAR-187의 세 개짜리 허용 목록이며
`weights.uncanonicalizable`에는 의도적으로 없다는 것; 그리고 `.marrow`, `.mskl` 1,
`.mbin` 2, C ABI, 에이전트 레지스트리 개수, `ctest -N`은 전부 그대로라는 명시.

Trailers:

```
Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
Claude-Session: <session id>
```

---

## Risks

| # | Risk | Mitigation |
| --- | --- | --- |
| R1 | **Starting before MAR-185 lands**, or assuming its symbols exist | Task 0.3's landing gate. MAR-186 consumes no MAR-185 code; the gate records the decision either way |
| R2 | **Every cited line has moved.** Two stories of drift | Task 0.1's re-anchor gate. Symbol first, line as a hint, non-resolution blocking |
| R3 | **Resolving orphans against the materialized skeleton.** The single most likely implementation error, and it makes the collector report a clean project | I1 → G1, run first among the inversions. `authored_animation_names` is the authority and G2 proves the authority is the fold |
| R4 | **Asserting `issue_count` instead of identities.** The MAR-186 shape of MAR-184's I2b lesson — many causes produce "some issues" | Every case asserts the **full sorted identity list** and reports a mismatch as a **named set difference** |
| R5 | **A fixture whose issue would fire for another reason.** The MAR-186 shape of the `ensure_*`-on-an-existing-edit trap | G7's control sub-case (untouched edit → zero issues), G3's non-empty assertion, G9(e)'s supersession. Every fixture states in a comment what makes it load-bearing |
| R6 | **`AttachmentSelection`'s transposed field order** | I10 → G6, which reads `slot_name` and `skin_name` **by name**. Task 0.11 re-measures the transposition |
| R7 | **Pointing `marrow_project_smoke` at a new fixture** takes the skip branch, runs nothing, exits 0 | Build throwaway projects inside the standing invocation, as MAR-177/178/184 do |
| R8 | **A passing `save()` proving nothing** | Every round trip goes `save_project` → `load_project(path)` |
| R9 | **An inversion result that lies (H1-H4).** MAR-184 recorded nine false negatives from `touch` alone | `/tmp/mar186-rebuild.sh` deletes the object file on **every** build; `cmp` over whole strings; neuter earlier detectors; final run from `rm -rf build` |
| R10 | **Moving a registry count site by accident.** MAR-186 must move **none** | Task 9's non-effect gate: zero `\b6[3-7]\b` lines in the diff, and the four numbers re-measured |
| R11 | **A silent sweep later — of the call list, not the enums.** Clang's default `-Wswitch` names every missed exhaustive-switch arm (A4), so the enums are policed. `collect_orphan_animation_overlays`' **seven family calls** are not, and MAR-185 shipped `clipboard_track_count` into exactly that class | The four name functions are `default:`-less switches, so the diagnostic is never silenced; every switch lives in `diagnostics.cpp` and Task 9's third grep enforces it; Task 2 writes the `static_assert` guard beside the call list; G1 asserts all seven identities. Design §2.12's `-Werror=switch` recipe is recorded for MAR-187, guarded on compiler id because **GCC needs `-Wall`** for the same warning |
| R12 | **Identity collisions from unescaped names.** A collision **deletes** an issue, because de-duplication cannot tell it from a duplicate | I5 → G12, which asserts a count of **two** as well as two distinct identities |
| R13 | **A vacuous zero-mutation case.** H4's shape: reading state the collector cannot touch | G0 snapshots seven values that a mutation **can** move, and I21 proves it. The `SelectionSet` half is recorded as structural and no vacuous case is written for it |
| R14 | **Editing a fixture.** `marrow_project_smoke` aborts on a partial marker match | Task 9's `git diff --stat -- assets/fixtures/` must be empty |
| R15 | **`weights.uncanonicalizable` turning out to be unreachable from a file** | Task 0.8 answers it **before** G8 is written. If unreachable, G8 becomes in-memory and `AGENTS.md` records the limitation — the case is not weakened to fit |
| R16 | **Canonicalization not being a bit-exact fixed point**, invalidating the `==` predicate | Task 0.7 answers it before Task 4. A failure stops the story to re-plan rather than silently adopting a tolerance |

---

## Dependency graph

```
MAR-185 committed (or its absence recorded)
  └─> Task 0  (measure; A4 fixed-point, A5 reachability, A6 fixture,
      │        A7 namespace, A8 parser rules, A9 transposition, A10 vocabulary,
      │        0.1 re-anchor gate, 0.13 the object-deletion harness)
      └─> Task 1  (header + authored_animation_names + empty collector + CMake)
            └─> Task 2  (orphan animations + finalize)        G0 G1 G2
                  └─> Task 3  (identity properties + round trip)  G3 G4 G5 G12
                        └─> Task 4  (weights)                 G7 G8
                              └─> Task 5  (preview + supersession)  G9
                                    └─> Task 6  (targets, counts, session)  G6 G10 G11
                                          └─> Task 7  (agent payload)       A1-A5
                                                └─> Task 8  (MCP)           M1
                                                      └─> Task 9  (scope + non-effect sweep)  I20
                                                            └─> Task 10 (full verification, from scratch)
                                                                  └─> Task 11 (docs + one commit)
```

Task 9 can start any time after Task 8's last production edit; it is placed late
because a diff is only meaningful over the finished tree. **Task 3 adds no
production code at all** — Task 2 wrote everything G3, G4, G5 and G12 exercise —
so a first-run failure there is a defect in the case until proven otherwise,
except for G12, which fails only if Task 2's escaping was omitted.
