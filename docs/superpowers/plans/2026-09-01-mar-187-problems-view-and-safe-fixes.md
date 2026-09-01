# MAR-187 Add the Problems View and Safe Fixes — Implementation Plan

Design: `docs/superpowers/specs/2026-09-01-mar-187-problems-view-and-safe-fixes-design.md`
Story: `MAR-187`, `dependsOn: ["MAR-186"]` — a **deep** dependency, not merely a
scheduling one: without `DiagnosticReport` there is nothing to view.
Branch: `feat/mar-168`. **Baseline measured for this plan: `c523977`**
(`feat: MAR-185 상속 타임라인 편집 패리티 완성`), the tip on 2026-09-01, tree clean
apart from MAR-186's two untracked documents.

> **The real baseline for implementation does not exist yet.** MAR-186 is
> **planned and not implemented** — its spec and plan are untracked files, and
> `grep -rn "collect_session_diagnostics\|DiagnosticReport\|DiagnosticIssue" src/ include/`
> returns **zero** at `c523977`. By the time MAR-187 starts, the tree will have
> moved by MAR-186's eleven tasks, plus any MAR-186 review pass.
> **Task 0.1 is a blocking re-anchor gate.** Every `file:line` below was correct
> at `c523977` and is **expected to have drifted**. Cite the **symbol**; treat the
> line as a hint. A citation that no longer names its described construct is a
> **blocking finding** written into `AGENTS.md`'s document-errors table, never a
> line to silently follow. On MAR-185 that gate found **sixteen** errors,
> including one that overturned the story's stated central hazard and four switch
> sites missing from a section calling itself "the complete inventory".

**Read the design first**, then §A, then §B. In particular design §0.2.1 (the
typed discriminator MAR-186 does not currently promise — a blocking dependency),
§1.2 (there are **two** frame bodies), §2.5 (preflight-then-mutate, and why its
ordering is deliberately untested), §2.7 (one record, not one animation), and
§5.1 (what is deliberately uninverted, and why).

## Standing rules for this plan

- **TDD, strictly.** The case goes in first, is **run and seen to fail for the
  stated reason**, and only then is the implementation written. A case that
  passes on its first run is a defect in the case until proven otherwise. The
  exceptions are named and justified in place: **V0** and **X9** are witnesses,
  expected to pass once their fixture exists, and their falsifiability comes from
  named inversions (I23, I24) rather than from a first-run failure.
- **Every case must be proven falsifiable, and uniquely attributed.** §B is the
  register. For each entry: apply the named source mutation, **delete the object
  file**, build, run the named binary, record the **exact** failure text, restore,
  delete the object file again, rebuild.
- **Verification builds delete the object file. They do not `touch` the source.**
  `AGENTS.md` H1, as amended by MAR-184: `touch` sets the source's mtime to *now*,
  the preceding restore-build's object is also *now*, and at GNU Make 3.81's
  one-second granularity the rebuild is **skipped**. MAR-184 got **nine** false
  "did not bite" readings this way. Both directions are reachable: a skipped
  *mutation* build is a missed inversion; a skipped *restore* build attributes the
  next inversion's failure to the wrong mutation. §0.13 gives the exact `rm -f`
  lines.
- **Uniqueness is established by running, not reading.** Where §B says "nothing
  earlier catches this", demonstrate it: neuter the earlier candidate and confirm
  the attributed case is still the one that fails.
- **Neuter with `(void)expr;`, never with `if (false && expr)`.** MAR-185's I9b
  passed for the wrong reason because `false &&` short-circuited away the very
  call whose side effect was the detector. Where the earlier candidate's *call*
  is what matters, keep the call and discard the result.
- **Compare recorded messages with `cmp`/`diff` over a whole recorded string**
  (H2). Never hand-slice a line number out of a reference file.
- **Trace which writes survive to the assertion point** (H4). An assertion that
  reads state a passing run leaves at its default is vacuous. Design §2.10 names
  the one vacuous case this story invites and refuses it.
- **Never assert a count where an identity is available.** Every case asserts the
  **full sorted identity list** and reports a mismatch as a **named set
  difference**. A count-only assertion passes for the wrong reason — MAR-186's
  plan records that resolving orphans against the materialized skeleton reports a
  clean project while every count-only test passes.
- **Assert the message, not `!result`.** Four times in this arc a `!result`
  assertion passed for the wrong reason, most recently MAR-184's P2b, where the
  load still failed but with the runtime's wording at a different JSON path. Its
  C++ mirror: `NaN < 0.0` is **false**, so a NaN slides through a sign check.
- **A passing `save()` proves nothing.** `validate_project_for_save`
  (`src/editor/project.cpp:5825` at `c523977`) takes no base document and
  materializes nothing. Every round trip goes `save_project` →
  **`load_project(path)`**, which calls `build_project_runtime` (defined at
  `:8258`, called from `load_project(const Document&)` at `:7932`).
- **The project smoke has ONE invocation.** Every editing suite sits inside
  `main()`'s marker-gated `else`; pointing the binary at another fixture takes the
  **skip** branch, runs nothing, and exits 0. Build throwaway projects **inside**
  the standing `player_idle.marrow` invocation.
- **The shell smoke has one rail and one early return.** A project carrying a
  `parameter_model` returns at `shell_smoke.cpp:82-88` and runs nothing else.
  MAR-187's suite goes on the **main** rail.
- **Attribute by run order, not authoring order.** MAR-185's I3 had to be
  re-attributed from P6 to P1 for exactly this.
- When a measurement disagrees with this plan, **the measurement wins.** Record it
  in `AGENTS.md`'s "Document errors found" table; do not silently adapt.
- Never weaken a case to make an inversion bite. Strengthen the case.
- `git add` / `git commit` only at Task 11. No `checkout`, no `stash`. Other
  agents may be editing tracked files concurrently; do not revert or stage their
  work.

---

## §A. What was verified, what was wrong, and what Task 0 must still gate

### A.1 Errors found in the incoming documents

| # | Source | Claim | Measured at `c523977` |
| --- | --- | --- | --- |
| **A1** | MAR-186 design §1.6, §2.12; plan §0.4, R11 | "no `-Wall`/`-Wextra`/`-Wswitch` … adding a value to either enum produces **zero diagnostics at every switch site**" | **The grep is right; the conclusion is false.** Re-measured directly: `c++ -std=c++17 -c` with **no** flags on a two-armed switch over a three-value `enum class` emits `warning: enumeration value 'C' not handled in switch [-Wswitch]`. `AGENTS.md:305-334` (added by MAR-185, **after** MAR-186's design was written against `6986024`) records MAR-185's own measurement: **24** warnings from a throwaway `TimelineKeyKind` value against **0** on a pristine rebuild. MAR-187 relies on the opposite of MAR-186's premise (design §2.12) |
| **A2** | MAR-186 design §1.2, `G-c`; plan §0.10 | "**three** hard parser rules", gated as three greps each returning one hit | **Four hits.** `skeleton_parse.cpp:1616`, `:5270`, and `animation references unknown slot` at **both** `:5373` and `:5430`. MAR-186's Task 0.10 gate fails on a correct tree |
| **A3** | MAR-186 design §1.3 vs §2.5 | §1.3 lists **six** `ensure_object_member(animations, …)` merge loops; §2.5 lists **seven** families | **Seven loops** at `project.cpp:5722,5739,5751,5761,5768,5775,5784`. The design contradicts itself; §2.5 is right |
| **A4** | MAR-186 design `G-i`; plan §0.4 | `grep -rn "DiagnosticSeverity\|DiagnosticPanel\|safe_fix\|issue_count" src/ include/ tools/` → **expect 0** | **Never zero.** `src/runtime/spine_import.cpp` uses `SpineImportDiagnosticSeverity`, which contains the substring. The gate must be word-anchored or it reports a false stop |
| **A5** | MAR-186 design §1.5 | Window titles at `shell_state.hpp:979-990` | `:989-1000` |
| **A6** | MAR-186 design §2.1 | `ensure_project_loaded` at `agent_dispatch.cpp:531-534` | `:536`; called at `:1091` |
| **A7** | MAR-186 design §3.1 | "`marrow_editor`'s `src/editor` include directory is `PRIVATE` and consumers add it by hand" | `target_include_directories(marrow_editor_shell PRIVATE …)` (`CMakeLists.txt:916-921`) lists only `external` and `src/renderer/generated`. The shell reaches `shell_state.hpp` by the same-directory rule. The **conclusion** (public header in `include/marrow/editor/`) stands |
| **A8** | MAR-186 design §2.11 | MAR-187 gets "three fixes keyed on `safe_fix_id`" | **Insufficient.** Applying `remove_orphan_overlay` to one record needs the overlay family, and `reset_preview_reference` needs the code; neither exists in typed form. Design §0.2.1 specifies the amendment. **Blocking** |
| **A9** | AC2, read literally | "focuses the appropriate hierarchy, timeline, inspector, weight, or preview panel" | **Two of the five are not panels and one is unreachable.** No Weight window, no Preview window (twelve titles at `shell_state.hpp:989-1000`); no MAR-186 code targets the Hierarchy. Design §2.6 maps three panels onto four destinations; §10 records the residue |

**The incoming brief was accurate and is recorded as confirmations**, because
E1/A1 shows how quickly the opposite belief propagates: registry **66**;
`agent_dispatch_smoke.cpp:39` plus **eleven** `!= 66U` guards in **three** files
(`shell_smoke_constraints.cpp:147,676`;
`shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603`;
`shell_smoke_timeline.cpp:3697,4389`) plus `tools/mcp/test_client.py:53,55`;
`build_project_runtime` at `project.cpp:8258`, called at `:7932`; `-Wswitch` on by
default; MAR-185 landed at `c523977`. All re-verified.

### A.2 Design claims that Task 0 must **prove by running**

| # | Claim | Gate |
| --- | --- | --- |
| **A10** | Design §1.3 / §5.1 — `begin_edit` + `cancel()` leaves the session **bit-identical**, which is why "mutate the candidate early" is unobservable | Task 0.7. **If anything moves, §5.1's uninverted entry becomes a real inversion and must be added** |
| **A11** | Design §2.3 / V8(c) — `adopt_runtime_sources()` bumps `runtime_revision` **without** moving `project_revision` | Task 0.8. **If it moves `project_revision`, V8(c) needs another driver and the design records it** |
| **A12** | Design §1.5 / §2.8 — an **empty** `preview_skins` vector passes `validate_project_for_save` | Task 0.9. If it does not, X6's end state needs a fallback |
| **A13** | Design §2.9 — `AttachmentSelection` and `MeshWeightTarget` really are transposed, and `normalize_mesh_weights` rejects a swapped target with a quotable message | Task 0.10. I7's predicted text is a prediction until this runs |
| **A14** | Design §2.7 — `erase_all_timeline_edits` is still unreachable from another translation unit | Task 0.11 |
| **A15** | Design §2.12 / A1 — `-Wswitch` fires here, on this build, for one of MAR-187's own enums | Task 0.5. **Paste the count and the text** |
| **A16** | Design §1.2 — there are **two** frame bodies and no persisted dock layout | Task 0.6 |

### A.3 Claims re-verified at `c523977` — do not re-litigate, only re-anchor

| Anchor | Construct |
| --- | --- |
| `src/editor/shell_state.hpp:989-1000` | twelve `k*WindowTitle[]` constants |
| `src/editor/shell_state.hpp:206` | `kDockLayoutVersion = 4` |
| `src/editor/shell_state.hpp:208-216` | `DockLayoutState` |
| `src/editor/shell_state.hpp:251-256` | `enum class ShellMode { Setup, Animation, WeightPaint, Parameter }` |
| `src/editor/shell_state.hpp:808-811` | `EditorSession session;` + the three `observed_*_revision` |
| `src/editor/shell_state.hpp:1210` | `ensure_default_dock_layout` declaration |
| `src/editor/shell_main.cpp:443-513` | `ensure_default_dock_layout` definition; `DockBuilderDockWindow` block at `:497-509` |
| `src/editor/shell_main.cpp:522-600` | `render_shell_frame`; the draw list at `:583-598`; `io.IniFilename = nullptr` at `:710` |
| `src/editor/shell_smoke_frames.cpp:49-77` | `render_headless_smoke_frames` and its **duplicated** draw list |
| `src/editor/shell_smoke_frames.cpp:1605-1607` | "a hidden dock tab renders no rows at all" |
| `src/editor/shell_smoke_frames.cpp:1643-1902` | MAR-185 F1, the real-mouse pattern |
| `src/editor/shell_smoke.cpp:47-154` | `run_headless_smoke`; parameter-mode early return at `:82-88`; rail at `:90-143`; `&&` chain at `:145-150` |
| `src/editor/shell_smoke_project.cpp:5662-5988` | `validate_shell_foundation_smoke` |
| `src/editor/shell_weight_paint.cpp:975-1031` | `run_weight_command`, the five-step transaction shape |
| `src/editor/shell_weight_paint.cpp:1057-1069` | `normalize_weights_command`, whose scope is **every vertex** when unnarrowed |
| `src/editor/shell_project_panels.cpp:470` | `apply_shell_mode` |
| `src/editor/shell_project_panels.cpp:1023-1039` | the preview reference display, in the **Project** window |
| `src/editor/shell_inspector.cpp:489-495` | `inspector_bone_pose_editable`, the mode gate |
| `src/editor/session.cpp:723-806` | `PreviewController::normalize_state`; skins `:724-733`, animation `:754-758` |
| `src/editor/session.cpp:1743-1744`, `:1813-1814`, `:1860-1864` | `editor_metadata` seeds `PreviewState` |
| `src/editor/session.cpp:1895` | `EditorSession::adopt_runtime_sources` |
| `include/marrow/editor/session.hpp:340,382-427` | `begin_edit`, `EditTransaction` |
| `include/marrow/editor/session.hpp:346-356` | `undo/redo/can_undo/undo_count/redo_count/dirty/project_revision/runtime_revision/preview_revision` |
| `include/marrow/editor/selection.hpp:49-52,86-90,99-140,143-150` | `AttachmentSelection`, `SelectionItem`, `SelectionSet`, `selection_item_exists`, `reconcile_selection_to_runtime` |
| `include/marrow/editor/authoring.hpp:690-694,696-703,729-734` | `MeshWeightTarget`, `MeshWeightResult`, `normalize_mesh_weights` |
| `include/marrow/editor/project.hpp:590-600` | `animation_edits` + the seven overlay vectors + `mesh_weight_attachment_edits` |
| `include/marrow/editor/agent_dispatch.hpp:57-58` | `agent_operation_descriptors`, `agent_operation_descriptor_count` |
| `src/editor/authoring.cpp:20-1309` | the anonymous namespace holding `rename_all_timeline_edits` (`:123`) and `erase_all_timeline_edits` (`:136`) |
| `src/editor/project.cpp:5825`, `:5943-5948`, `:7712-7719`, `:8258`, `:7932` | `validate_project_for_save`, the preview-skin rule, `create_minimal_project`'s defaults, `build_project_runtime` and its caller |
| `src/samples/editor_project_smoke.cpp:16133-16134`, `:16144-16153`, `:16154`, `:16282` | the marker skip, the partial-match **abort**, the `else`, MAR-185's registration |
| `CMakeLists.txt:352`, `:499-519`, `:876-914`, `:916-921` | the one `-Werror`, `marrow_editor`'s sources, `marrow_editor_shell`'s sources, its include dirs |

---

## §B. Inversion register

Twenty-five entries. Each names the **source mutation**, the case it must bite
**by run order**, the **predicted failure text**, and why nothing earlier catches
it. I11 is **eight** separate runs (one per overlay-family arm), so the register
is **thirty-two** recorded messages.

Predicted texts are predictions. Record the **actual** text and `cmp` it against
an independently recorded first-run string (H2).

| # | Mutation | Bites | Predicted failure text | Why nothing earlier |
| --- | --- | --- | --- | --- |
| **I1** | `build_problems_view` recomputes `error_count`/`warning_count` from the visible rows | **V3** | `MAR-187 V3: under ErrorsOnly the view reported warning_count 0, expected 1 (the collector's). AC1 requires the view to display COLLECTOR counts, not filtered ones.` | V0 has no issues; V1 and V2 read identities and group shapes, not counts; under `All` the two derivations agree, so nothing before V3 can distinguish them |
| **I2** | Emit groups in first-encountered order instead of Error-then-Warning | **V1** | `MAR-187 V1: group 0 has severity 'warning', expected 'error'. This fixture's LOWEST identity is a Warning (overlay.orphan_weight_target|mesh_base|body|ghost_mesh), which is what makes the two orders distinguishable.` | V0 has no groups. The report is identity-sorted, and most fixtures put an Error first; V1's fixture is built on `overlay.orphan_weight_target` (Warning) sorting before `weights.uncanonicalizable` (Error) **on purpose**, and says so in a comment |
| **I3** | Emit an empty group instead of omitting it | **V2** | `MAR-187 V2: WarningsOnly over an errors-only report produced 1 group, expected 0. Group 0 is 'warning' with 0 rows.` | V1's fixture has both severities, so no group is ever empty there |
| **I4** | Swap the `ErrorsOnly` and `WarningsOnly` arms | **V2** | `MAR-187 V2: WarningsOnly over an errors-only report produced 1 group with 2 rows, expected 0 groups. Rows: overlay.orphan_animation|transform|ghost|arm_l|rotate, weights.uncanonicalizable|mesh_base|body|body_mesh|0.` | **Attributed by run order, not authoring order.** V4 is the identity-level detector and was written for this mutation, but V2 runs first and its errors-only fixture makes the swap visible as a group-count failure. Both are recorded |
| **I5** | Derive the row identity from the row's index in `groups[g].issue_indices` | **V5** | `MAR-187 V5: after inserting an unrelated deform orphan that sorts first, the remembered row resolved to 'overlay.orphan_animation|deform|ghost|body|body_mesh', expected 'overlay.orphan_animation|transform|ghost|arm_l|rotate'.` | An index-derived identity is stable within one build **and** across two builds of an unchanged project. V0-V4 never rebuild after an insertion |
| **I6** | `plan_issue_navigation` skips `selection_item_exists` and always carries the selection | **V6** | `MAR-187 V6: the orphan weight target's navigation carried AttachmentSelection{slot 'body', skin 'mesh_base', attachment 'ghost_mesh'} for a triple no runtime resolves; expected target_missing with no selection.` | Every other issue's target resolves, so the check is a no-op for V7's rows and for every case before V6 |
| **I7** | Build `MeshWeightTarget` from `AttachmentSelection` in field order | **X4** | Predicted: `MAR-187 X4: normalize_weights was rejected — "<primitive's message>". AttachmentSelection is {slot, skin, attachment}; MeshWeightTarget is {skin, slot, attachment}. The two are transposed.` **Record the primitive's actual text from Task 0.10.** | V7 reads the *selection's* fields, which are correct; only the fix consumes them in the other struct's order. Codes, identities, severities, panels and counts are unaffected |
| **I8** | `apply_safe_fix` skips the freshness re-collection (step 3) | **X8(c)** | `MAR-187 X8(c): a stale issue whose identity no longer exists mutated the project — serialize_project() changed (12844 -> 12801 bytes) and undo_count went 0 -> 1. A fix must re-validate against a fresh collection.` | X1-X7 all pass an issue collected from the current revision, where the re-collection is a no-op |
| **I9** | `apply_safe_fix` commits on the "nothing changed" path instead of cancelling | **X4's control arm** | `MAR-187 X4 control: normalizing an already-canonical vertex recorded an undo entry (0 -> 1) and left dirty()=true. A repair with nothing to repair must cancel.` | X1, X2, X3, X5 and X6 all change something, so the branch never runs before X4's control arm |
| **I10** | `remove_orphan_overlay` erases **every** overlay naming the animation | **X1** | `MAR-187 X1: removing one of seven orphan overlays left 0 issues, expected 6. Missing: overlay.orphan_animation|deform|ghost|body|body_mesh, …|draw_order|ghost, …|event|ghost, …|slot_attachment|ghost|body, …|slot_color|ghost|body, …|transform|ghost|arm_l|rotate.` | Nothing before X1 has more than one record for one animation |
| **I11a-h** | Point **one** arm of the family switch at the **wrong** vector. **Run all eight** (seven overlay families + the weight-target arm), one at a time | **X1** (a-g), **X3** (h) | e.g. for the inherit arm: `MAR-187 X1: removing the inherit overlay left overlay.orphan_animation|inherit|ghost|arm_l in the list and erased overlay.orphan_animation|transform|ghost|arm_l|rotate instead. Remaining: …` | The other six/seven arms still work, so a count-only or "fewer issues than before" assertion passes. X1 compares the **full remaining list** by name. **Note explicitly:** *omitting* an arm is a `-Wswitch` **compile warning** (A15), not a test failure — only a wrong-vector arm needs an inversion, and that asymmetry is recorded in `AGENTS.md` rather than hidden |
| **I12** | `reset_preview_reference` **clears** `active_animation` instead of writing the substituted value | **X5** | `MAR-187 X5: active_animation is '' after the reset, expected 'aim' — the value normalize_state already substitutes. Clearing changes what the editor shows; the repair must change stored data and not behaviour.` | X1-X4 never touch `editor_metadata`. The issue disappears under either implementation, so only an assertion on the **resulting value** sees it |
| **I13** | `reset_preview_reference` rebuilds `preview_skins` from `preview_state().skin_names` | **X6** | `MAR-187 X6: preview_skins is ["default"], expected ["default","default"]. A repair asked only to drop 'ghost_skin' also collapsed a RESOLVABLE duplicate that no issue named — normalize_state de-duplicates as well as filters (session.cpp:727-732).` | X6 is the only fixture carrying a resolvable duplicate; everywhere else the two implementations agree |
| **I14** | `safe_fix_kind_for` accepts any non-empty id (drops the string table) | **X7** | `MAR-187 X7: the accepted fix-id set has 69 members, expected exactly 3. Unexpected (sorted): agent.review_approve, animation.create, … Missing: none.` | X1-X6 all pass allowlisted ids, so the table's *rejections* are never exercised before X7 |
| **I15** | Add a fourth reachable id (`"rebind_weights"` → a new `SafeFixKind` arm) | **X7** | `MAR-187 X7: 'rebind_weights' was accepted. Expected exactly {normalize_weights, remove_orphan_overlay, reset_preview_reference}.` | **This is AC4's own gate.** No other case enumerates what must be rejected |
| **I16** | `apply_safe_fix` returns the rejection **after** committing | **X8(a)** | `MAR-187 X8(a): a rejected fix left serialize_project() changed (12844 -> 12801 bytes) and undo_count 0 -> 1. A rejection must leave the session untouched.` | X1-X7 all succeed, so no rejection path runs before X8 |
| **I17** | `problems_view_needs_refresh` compares only `project_revision` | **V8(c)** | `MAR-187 V8(c): after adopt_runtime_sources() bumped runtime_revision 3 -> 4 with project_revision unchanged at 7, the view reported no refresh needed. The new report carries preview.stale_skin|mage_arm, which the cached view does not. AC3 names BOTH revisions.` | Every ordinary edit moves both revisions together, so V8(a) and V8(b) pass unchanged. `adopt_runtime_sources` is the only driver that separates them (A11) |
| **I18** | `problems_view_needs_refresh` returns `true` unconditionally | **V8(a)** | `MAR-187 V8(a): an unchanged session reported that a refresh was needed; the view would re-collect on every frame.` | Nothing else asserts the negative. **Recorded as a quiescence property, not a correctness one** — an always-refresh implementation is behaviourally correct, which is exactly why this arm has to exist or the property is untested |
| **I19** | The shell activates a row but never calls `SelectionSet::replace` | **S1** | `MAR-187 S1: activating the transform-overlay row left the selection empty; expected active bone 'arm_l'.` | Every project-layer case tests `plan_issue_navigation`, which is unaffected; only a shell case observes whether the plan was **applied** |
| **I20** | `focus_window_for_panel` returns `kTimelineWindowTitle` for every panel | **S2** | `MAR-187 S2: the weight issue requested focus on 'Timeline', expected 'Properties'.` | S1's issue **is** a timeline issue, so it passes under the mutation |
| **I21** | The shell omits `apply_shell_mode(ShellMode::WeightPaint)` for a `Weights` panel | **S2** | `MAR-187 S2: shell_mode is 'Animation' after activating a weight issue. The numeric influence table is unreachable outside WeightPaint (shell_inspector.cpp:489-495).` | Selection, FFD selection and focus are all still right; only the mode is wrong, and only S2 reads it |
| **I22** | The shell calls `SelectionSet::replace` for a `target_missing` plan | **S4** | `MAR-187 S4: activating the orphan weight-target row replaced the selection with AttachmentSelection{body, mesh_base, ghost_mesh}, which no runtime resolves. Expected the previous BoneSelection{spine} to survive.` | V6 asserts the **plan**; S4 asserts what the shell does with it. The two can disagree, and only S4 sees it |
| **I23** | Add a `const_cast` to the refresh path and call `session.seek(0.5)` | **V0** | `MAR-187 V0: building the Problems view advanced preview_revision from 1 to 2. Inspection must not mutate the session.` | **This is what makes V0 non-vacuous.** Without it, V0's seven-value snapshot reads state a passing run leaves unchanged, which is hazard H4 exactly. The mutation proves the snapshot can move |
| **I24** | Delete `draw_problems_window`'s body (keep the function and both call sites) | **F1** | `MAR-187 F1: no widget in the "Problems" window matched the id of the severity filter. A real mouse swept every position and HoveredId never equalled it, so the widget is absent or unreachable.` | **Every UI-free case still passes** — S1-S6 call the handler, not the button. `AGENTS.md`'s Headless Frame Smoke Notes record the MAR-178 worked example where deleting `draw_constraint_catalog_buttons()`'s body left that story's own scenario printing its full success line while the frame smoke failed by name |
| **I25** | Wire the row's Fix button to `normalize_weights_command` instead of `apply_safe_fix` | **X4** (project layer) and **S5** (shell layer) | `MAR-187 X4: normalizing vertex 0 also canonicalized vertex 12. Remaining issues: <empty>, expected [weights.non_canonical|mesh_base|body|body_mesh|12]. weight_command_scope returns EMPTY — meaning EVERY vertex — unless an FFD selection narrows it (shell_weight_paint.cpp:1057-1069).` | X1-X3 have no second non-canonical vertex. The clicked vertex **is** repaired either way, so only a second one that must survive exposes the wider scope |

### B.1 Deliberately uninverted — record verbatim in `AGENTS.md`

Design §5.1. **No inversion is invented for any of these**, and the reason is
stated per item:

- **Opening the transaction before the preflight.** Task 0.7 (A10) measures that
  `begin_edit` + `cancel()` leaves `serialize_project()`, `dirty()`,
  `undo_count()`, `redo_count()` and all three revisions identical, so the
  mutation is invisible through the session's entire public surface. The
  preflight ordering is a **design choice, recorded, not tested**. If Task 0.7
  measures otherwise, the inversion exists and **must** be added.
- **`problems_view_needs_refresh` returning `true` unconditionally is
  behaviourally correct.** I18 asserts quiescence, not correctness, and V8(a)
  says so in a comment.
- **`DiagnosticPanel::Hierarchy` / `Inspector`.** MAR-187 adds no enumerator, so
  there is nothing to invert. Design §10 records the gap.
- **Omitting a `switch` arm** over `SafeFixKind`, `DiagnosticOverlayFamily`,
  `DiagnosticCode`, `ProblemsSeverityFilter` or `DiagnosticPanel` is a
  **`-Wswitch` compile warning** (A15), not a test failure. Recorded as
  compiler-covered, with Task 0.5's measured text pasted, rather than dressed up
  as an inversion. I11 covers the wrong-arm case, which the compiler cannot see.
- **V0's byte-identity half.** No story-owned mutation makes an unchanged project
  serialize differently without failing something louder first. V0's other halves
  are inverted (I23).
- **MAR-186's own guarantees** — sorting, de-duplication, identity escaping,
  severity assignment, `is_allowlisted_safe_fix`. MAR-187 asserts them where it
  depends on them (V1's strictly-increasing check, X7's agreement check) but
  writes **no inversion for code it did not write**.
- **`X9`'s "the project was not repaired on open"** shares a detector with I23 at
  the shell layer (S6). It is kept because AC5 names it explicitly, and it is
  recorded as over-determined rather than removed.

---

## Task 0 — Measure, before any code

**Depends on:** MAR-186 being committed. **No file is modified.** Every step
prints a value; a value that disagrees with §A is a stop-and-record.

### 0.1 **Re-anchor gate — every cited line number, before anything else**

Every anchor in §A.3 was measured at `c523977` and is **expected to have
drifted**. MAR-186 edits `include/marrow/editor/project.hpp`, `src/editor/project.cpp`,
`src/editor/agent_handlers_inspection.cpp`, `src/samples/editor_project_smoke.cpp`,
`src/samples/agent_dispatch_smoke.cpp`, `tools/mcp/`, and `CMakeLists.txt` — three
of which MAR-187 also touches.

```bash
git rev-parse HEAD
git log --oneline -3
git status --short
git diff --stat c523977..HEAD -- src/ include/ tools/ CMakeLists.txt
```

For each anchor, the check is **not** "does that line still exist" but **"does
that line still name the construct §A.3 says it names"**:

```bash
# One worked example; repeat the shape for EVERY anchor in §A.3.
grep -n "WindowTitle\[\]"                     src/editor/shell_state.hpp
grep -n "kDockLayoutVersion"                  src/editor/shell_state.hpp
grep -n "^void ensure_default_dock_layout"    src/editor/shell_main.cpp
grep -n "draw_inspector_window"               src/editor/shell_main.cpp src/editor/shell_smoke_frames.cpp
grep -n "^int run_headless_smoke"             src/editor/shell_smoke.cpp
grep -n "^bool run_weight_command"            src/editor/shell_weight_paint.cpp
grep -n "^bool normalize_weights_command"     src/editor/shell_weight_paint.cpp
grep -n "^void apply_shell_mode"              src/editor/shell_project_panels.cpp
grep -n "normalize_state"                     src/editor/session.cpp
grep -n "SessionResult EditorSession::adopt_runtime_sources" src/editor/session.cpp
grep -n "struct AttachmentSelection" -A 5     include/marrow/editor/selection.hpp
grep -n "struct MeshWeightTarget" -A 5        include/marrow/editor/authoring.hpp
grep -n "MeshWeightResult normalize_mesh_weights" -A 6 include/marrow/editor/authoring.hpp
grep -n "timeline_edits;"                     include/marrow/editor/project.hpp
grep -n "^bool validate_project_for_save"     src/editor/project.cpp
grep -n "^ProjectRuntimeResult build_project_runtime" src/editor/project.cpp
grep -n "markers.present.empty()"             src/samples/editor_project_smoke.cpp
grep -n "^namespace {\|^} // namespace"       src/editor/authoring.cpp | head -4
```

**A citation that no longer resolves to the described construct is a blocking
finding**, written into `AGENTS.md`'s "Document errors found" table in §A.1's
form, before anything else continues.

Three anchors matter more than the rest and are called out by name:

- **The two frame bodies.** Record both current line numbers. Adding the window
  to one and not the other is R4 and is not compiler-detectable.
- **MAR-186's registration point** in `editor_project_smoke.cpp`'s marker-gated
  `else`. MAR-187's two suites register after it.
- **The shell rail's parameter-mode early return** (`shell_smoke.cpp:82-88`).
  MAR-187's suite goes **after** it, on the main rail.

### 0.2 Baseline build and inventory

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build -N | tail -3          # 22 at c523977; record whatever it is
ctest --test-dir build --output-on-failure | tail -5
```

MAR-187 registers **no** new CTest target, so the number must be identical at
Task 10.

### 0.3 **MAR-186 landing gate — and the D6 blocking check**

```bash
git log --oneline --grep="MAR-186" -1
sed -n '1,120p' include/marrow/editor/diagnostics.hpp
grep -n "DiagnosticCode\|DiagnosticOverlayFamily\|code_kind\|overlay_family" include/marrow/editor/diagnostics.hpp
grep -n "collect_session_diagnostics\|is_allowlisted_safe_fix\|kSafeFix" include/marrow/editor/diagnostics.hpp
```

Confirm design §0.2's **D1-D5** are present, with their exact signatures, and
write the actual shapes down — MAR-186 may have renamed something.

**D6 is the blocking one.** If `DiagnosticIssue` carries no typed code and no
typed overlay family:

1. Record it as a **blocking finding** in `AGENTS.md`'s document-errors table,
   phrased as A8.
2. Task 1 adds them to `include/marrow/editor/diagnostics.hpp` and populates them
   in `src/editor/diagnostics.cpp`, as a **named amendment to MAR-186**.
3. **Never** recover the family by splitting `identity` on `|`. MAR-186's own
   escaping (its §2.2) exists because names can contain `|`, and a split-based
   route would silently erase from the wrong vector for exactly those names.

### 0.4 **The vocabulary gate, word-anchored (A4)**

```bash
grep -rnE "\bProblemsView\b|\bProblemsGroup\b|\bSafeFixKind\b|apply_safe_fix|kProblemsWindowTitle" src/ include/   # expect 0
grep -rnE "\bDiagnosticSeverity\b" src/ include/ | grep -v SpineImport                                              # MAR-186's only
```

The second grep is A4's correction: an unanchored pattern matches
`SpineImportDiagnosticSeverity` in `src/runtime/spine_import.cpp` and can never
return zero. Paste both outputs.

### 0.5 **A15 gate — prove the compiler is NOT silent.** The most consequential step

MAR-186's design asserts the opposite (A1). Settle it here, on this build, for
one of MAR-187's own enums.

```bash
# 1. Pristine warning count.
rm -rf /tmp/mar187-warn && cmake --build build 2>&1 | grep -c "warning:"
# 2. Add a throwaway 4th value to ProblemsSeverityFilter (after Task 1 creates it),
#    delete the objects, rebuild, count and READ the warnings.
/tmp/mar187-rebuild.sh 2>&1 | tee /tmp/mar187-warn/with-extra.txt | grep -c "warning:"
grep -n "\[-Wswitch\]" /tmp/mar187-warn/with-extra.txt
# 3. Remove it, delete the objects, rebuild, confirm the count returns to (1).
```

**Expected: non-zero in step 2, and every hit a `[-Wswitch]` naming one of the
files design §2.12 lists.** Paste the list. If it is zero, A1 is wrong in the
other direction and design §2.12's whole containment argument changes — stop and
re-plan.

Run this immediately after Task 1 creates the first enum, and record it under
Task 0's heading anyway; it is a measurement, not an implementation step.

### 0.6 **A16 gate — two frame bodies, no persisted layout**

```bash
grep -n "draw_project_window\|draw_inspector_window\|draw_timeline_window" \
     src/editor/shell_main.cpp src/editor/shell_smoke_frames.cpp
grep -n "IniFilename" src/editor/shell_main.cpp src/editor/shell_smoke.cpp
grep -n "kDockLayoutVersion" src/editor/shell_state.hpp
```

Expect the draw list to appear **twice**, in two files, and `IniFilename =
nullptr` in both. Record the two insertion points. If a layout is now persisted,
`kDockLayoutVersion` must be bumped and that becomes a Task 6 step.

### 0.7 **A10 gate — is `begin_edit` + `cancel` bit-identical?**

Open a session on `player_idle.marrow`. Snapshot `serialize_project(*session.project())`,
`dirty()`, `undo_count()`, `redo_count()`, `project_revision()`,
`runtime_revision()`, `preview_revision()`. Do one `undo()` first so
`redo_count()` is **non-zero** — a zero redo stack cannot detect being cleared.
Then `begin_edit(...)`, `cancel()`, and compare all seven.

**Expected: identical.** Paste the seven before/after pairs. **If any moves, §B.1's
first uninverted entry becomes a real inversion** ("open the transaction before
the preflight") and must be added to §B with the case that catches it.

### 0.8 **A11 gate — does a runtime adoption move `project_revision`?**

Copy `player_idle.mskl`/`.matl`/`.marrow` to a scratch directory, open the copy,
print `project_revision()` / `runtime_revision()` / `preview_revision()`, rewrite
the scratch `.mskl` to drop the skin `mage_arm`, call `adopt_runtime_sources()`,
print all three again.

**Expected: `runtime_revision` and `preview_revision` advance;
`project_revision` does NOT.** (`session.hpp:262-283` documents it; measure it.)
**If `project_revision` moves, V8(c) has no driver** — record it and find another
(the asset watcher, `shell_asset_watch.cpp:153`) or record V8(c) as unreachable.

### 0.9 **A12 gate — an empty `preview_skins` is savable**

Build a project, set `editor_metadata.preview_skins = {}`, call
`validate_project_for_save`, then `save_project` → **`load_project(path)`**.

**Expected: all three succeed** (`project.cpp:5943-5948` rejects an empty
**name**, not an empty vector). If not, X6's end state needs a fallback and
design §2.8 changes.

### 0.10 **A13 gate — the transposition, and the primitive's real message**

```bash
grep -n "struct AttachmentSelection" -A 5 include/marrow/editor/selection.hpp
grep -n "struct MeshWeightTarget"    -A 5 include/marrow/editor/authoring.hpp
```

Expect `slot_name, skin_name, attachment_name` versus
`skin_name, slot_name, attachment_name`. If they now agree, I7 is
non-reproducing and is recorded as such — **not** replaced with a convenient
substitute.

Then call `normalize_mesh_weights` with skin and slot **swapped** and paste the
returned `error` verbatim. That string is I7's predicted text; until this runs it
is a guess.

### 0.11 **A14 gate — `erase_all_timeline_edits` is out of reach**

```bash
grep -n "^namespace {\|^} // namespace" src/editor/authoring.cpp | head -4
grep -n "erase_all_timeline_edits\|rename_all_timeline_edits" src/editor/authoring.cpp include/marrow/editor/authoring.hpp
```

Confirm both definitions fall **inside** the first `namespace { … }` block and
appear in **no** header. Design §2.7's decision — a `switch` over the family
enum rather than a second seven-vector call list — depends on it. If they are now
public, record it and re-plan Task 4: the *whole-animation* erase becomes
available, and the design's per-record choice must be restated as a choice rather
than a constraint.

### 0.12 The registry non-effect baseline

```bash
grep -c '^\s*{\s*"' src/editor/agent_dispatch.cpp
grep -n "OperationExpectation, " src/samples/agent_dispatch_smoke.cpp
grep -rn "!= 6[0-9]U" src/
grep -nE "== 6[0-9]" tools/mcp/test_client.py
```

Write the four numbers down. **This story moves none of them**, and X7 must not
create a twelfth guard. Task 9 diffs against this list. Look-alikes that must not
be mistaken for count sites, carried forward from MAR-185 and MAR-186:
`shell_smoke_graph.cpp`'s `4364U`, `shell_smoke_viewport.cpp`'s `640`,
`IM_COL32(56, 61, 69, 255)`, `IM_COL32(208,134,57,230)`, `(51, 56, 64)`,
`rgb(54,57,64)`, `"x": 56.0`, `56,995,840`, `PhysicsBoneState … 56 bytes/bone`,
`AGENTS.md`'s `t = 0.62` and its self-referential line counts, and
`tools/mcp/test_client.py`'s ordinal.

### 0.13 **The inversion harness — object deletion, not `touch`**

Write this now and use it for **every** inversion in §B and for §0.5.

```bash
# /tmp/mar187-rebuild.sh  --  delete the objects, then build.
set -e
for o in \
  build/CMakeFiles/marrow_editor.dir/src/editor/problems_model.cpp.o \
  build/CMakeFiles/marrow_editor.dir/src/editor/safe_fix.cpp.o \
  build/CMakeFiles/marrow_editor.dir/src/editor/diagnostics.cpp.o \
  build/CMakeFiles/marrow_editor.dir/src/editor/authoring.cpp.o \
  build/CMakeFiles/marrow_editor_shell.dir/src/editor/shell_problems.cpp.o \
  build/CMakeFiles/marrow_editor_shell.dir/src/editor/shell_main.cpp.o \
  build/CMakeFiles/marrow_editor_shell.dir/src/editor/shell_smoke.cpp.o \
  build/CMakeFiles/marrow_editor_shell.dir/src/editor/shell_smoke_project.cpp.o \
  build/CMakeFiles/marrow_editor_shell.dir/src/editor/shell_smoke_frames.cpp.o \
  build/CMakeFiles/marrow_project_smoke.dir/src/samples/editor_project_smoke.cpp.o
do rm -f "$o"; done
cmake --build build
```

Run it **after the mutation and again after the restore**. Do **not** rely on
`touch`: the generator is `Unix Makefiles` with **GNU Make 3.81** (confirm:
`grep CMAKE_GENERATOR build/CMakeCache.txt`, `make --version`), whose one-second
mtime granularity produced MAR-184's nine false readings. Verify every path in
the list exists after a normal build — a typo silently makes the script a no-op,
which is a false **pass** generator.

### 0.14 The fixture and non-effect witness

```bash
./build/marrow_project_smoke assets/fixtures/player_idle.marrow > /tmp/mar187-before.txt 2>&1
shasum -a 256 assets/fixtures/player_idle.marrow
```

Record the byte length and SHA-256 of
`serialize_project(load_project("assets/fixtures/player_idle.marrow").project)`.
V0 prints both every run. Confirm MAR-186's finding that the fixture yields
**zero** issues; if it does not, V0's premise is gone and the fixture facts must
be re-derived before anything else.

### Task 0 exit criteria

- [ ] **0.1 re-anchor gate**: every §A.3 anchor re-resolved, old → new recorded,
      every non-resolving citation written into `AGENTS.md` as a blocking finding.
      The two frame bodies, MAR-186's registration point, and the shell rail's
      early return called out individually.
- [ ] HEAD recorded; `git status --short` inspected; `ctest -N` total recorded.
- [ ] **0.3**: MAR-186's landing state and the **D1-D6** answer written down;
      a missing D6 recorded as blocking with the Task 1 amendment decided.
- [ ] **0.4**: both greps run and pasted, word-anchored.
- [ ] **0.5 (A15)**: the `-Wswitch` warning count and text pasted (may run right
      after Task 1's first enum, but recorded here).
- [ ] **0.6 (A16)**: both frame-body insertion points and the `IniFilename`
      answer recorded.
- [ ] **0.7 (A10)**: seven before/after pairs pasted, with a **non-zero** redo
      stack. §B.1's first entry confirmed or converted into an inversion.
- [ ] **0.8 (A11)**: three revisions before and after `adopt_runtime_sources`.
- [ ] **0.9 (A12)**, **0.10 (A13, plus the primitive's message)**,
      **0.11 (A14)**: answered in writing.
- [ ] **0.12**: the four registry numbers written down.
- [ ] **0.13**: the rebuild script written and **every** object path verified to
      exist after a normal build.
- [ ] **0.14**: fixture SHA and the zero-issue answer recorded.

---

## Task 1 — The vocabulary, and two empty functions

**Depends on:** Task 0.

Files: `include/marrow/editor/problems_model.hpp` (**new**),
`src/editor/problems_model.cpp` (**new**),
`include/marrow/editor/safe_fix.hpp` (**new**), `src/editor/safe_fix.cpp`
(**new**), `CMakeLists.txt`, and — only if Task 0.3 found D6 missing —
`include/marrow/editor/diagnostics.hpp` and `src/editor/diagnostics.cpp`.

### 1.1 The headers

Exactly design §2.2, §2.4 and §2.5. `problems_model.hpp` includes
`<cstddef> <cstdint> <optional> <string> <string_view> <vector>`,
`marrow/editor/diagnostics.hpp`, `marrow/editor/selection.hpp`, and
**forward-declares** `runtime::SkeletonData`. `safe_fix.hpp` forward-declares
`EditorSession` — **not** `marrow/editor/session.hpp`, so the view model does not
drag the session into every consumer.

### 1.2 The D6 amendment, if Task 0.3 requires it

Add `DiagnosticCode`, `DiagnosticOverlayFamily`, and the two `DiagnosticIssue`
members to MAR-186's header; populate them in `src/editor/diagnostics.cpp`'s
`make_issue`. Assert in MAR-186's own suite (or a new project case) that
`code_kind` and `code` agree for every code MAR-186 emits — otherwise the two
representations can drift and only one of them is tested.

Record the amendment in `AGENTS.md`'s document-errors table.

### 1.3 The empty implementations

`build_problems_view` returns `{}`; `plan_issue_navigation` returns `{}`;
`find_issue_by_identity` returns `std::nullopt`; `problems_view_needs_refresh`
returns `true`; `safe_fix_kind_for` returns `std::nullopt`; `apply_safe_fix`
returns `{false, false, "not implemented", {}}`. Nothing calls them yet.

### 1.4 CMake

Two lines in `add_library(marrow_editor …)` (`CMakeLists.txt:499-519`), after
`src/editor/mesh_weight_model.cpp`. **No new target.**

### 1.5 Verify

```bash
cmake -S . -B build && cmake --build build
ctest --test-dir build -N | tail -1     # Task 0.2's number, unchanged
```

### 1.6 Run Task 0.5 now

The first enum exists. Add a throwaway fourth `ProblemsSeverityFilter` value,
run `/tmp/mar187-rebuild.sh`, count and read the `[-Wswitch]` warnings, remove
it, rebuild. Paste the result under Task 0.5.

---

## Task 2 — Grouping, filtering, and the counts

**Depends on:** Task 1. Files: `src/editor/problems_model.cpp`,
`src/samples/editor_project_smoke.cpp`.

### 2.1 Test first — V0, V1, V2, V3, V4

`validate_mar187_problems_view(const ProjectLoadResult&)`, registered inside
`main()`'s marker-gated `else` after MAR-186's suite (Task 0.1's recorded line).

| # | Case |
| --- | --- |
| **V0** | Design §6.2. The zero-issue witness plus the seven-value snapshot. Prints the `serialize_project` length and SHA every run. *(I23.)* Expected to pass on first run — a **witness**; say so in a comment |
| **V1** | Two groups, Error first, full identity list per group, `identity[i] < identity[i+1]` strictly. **The fixture's lowest identity must be a Warning** (`overlay.orphan_weight_target` sorts before `weights.uncanonicalizable`) and the comment must say why. *(I2.)* |
| **V2** | An errors-only fixture: `WarningsOnly` → **0** groups; `ErrorsOnly` → one group with the exact list. *(I3, I4.)* |
| **V3** | V1's fixture under all three filters: `error_count` and `warning_count` identical in all three and equal to the report's; only `visible_count` moves. Printed as three triples. *(I1.)* |
| **V4** | Each filter's exact identity list, reported as a set difference. *(Second detector for I4.)* |

Fixtures are built with `create_minimal_project` over
`assets/fixtures/player_idle.mskl` into a scratch directory and then mutated in
memory, **inside** the standing invocation. Every fixture carries a comment
stating what makes it load-bearing.

### 2.2 Watch them fail

V1 fails first: `build_problems_view` returns `{}`, so `groups.size()` is 0.
Record the exact text.

### 2.3 Implement

`build_problems_view` only. Two passes: partition by severity into at most two
groups in the fixed order `Error, Warning`, dropping an empty one; copy
`error_count`, `warning_count`, `project_revision` and `runtime_revision` **from
the report**; set `visible_count` from the rows kept. **No sort** — MAR-186
guarantees the order (design §2.2).

### 2.4 Verify + inversions

```bash
/tmp/mar187-rebuild.sh && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

Run **I1 → V3**, **I2 → V1**, **I3 → V2**, **I4 → V2** (recording V4 as the
second detector), **I23 → V0**.

---

## Task 3 — Row identity and the refresh key

**Depends on:** Task 2. Files: `src/editor/problems_model.cpp`,
`src/samples/editor_project_smoke.cpp`.

### 3.1 Test — V5, V8

| # | Case |
| --- | --- |
| **V5** | Design §6.2. Remember an identity; insert an unrelated orphan that sorts **before** it; re-collect and re-build; the identity resolves to a **different index** and is byte-identical. *(I5.)* |
| **V8** | Three arms. (a) unchanged session → **false** *(I18; a quiescence property, labelled)*. (b) one project edit → **true**, `project_revision` advanced. (c) **runtime-only**: scratch copy, rewrite the `.mskl` to drop `mage_arm`, `adopt_runtime_sources()`, assert `runtime_revision` advanced and `project_revision` did **not** (Task 0.8's measured behaviour), refresh reported needed, and the new report carries a `preview.stale_skin` the old one did not. *(I17.)* |

V8(c) follows `validate_runtime_asset_hot_reload_smoke`'s pattern
(`shell_smoke_project.cpp`, `rewrite_selection_source_for_reload`) but lives in
the project smoke, which already drives `adopt_runtime_sources`
(`editor_project_smoke.cpp:13722` at `c523977`).

### 3.2 Watch them fail

V8(a) fails first: `problems_view_needs_refresh` returns `true` unconditionally.

### 3.3 Implement

`find_issue_by_identity` (binary search over the sorted identities, or a linear
scan — MAR-186 guarantees strict ordering, so either is correct; state which) and
`problems_view_needs_refresh` (design §2.3).

### 3.4 Verify + inversions

Run **I5 → V5**, **I17 → V8(c)**, **I18 → V8(a)**.

---

## Task 4 — Navigation, and removed targets

**Depends on:** Task 3. Files: `src/editor/problems_model.cpp`,
`src/samples/editor_project_smoke.cpp`.

### 4.1 Test first — V6, V7

| # | Case |
| --- | --- |
| **V6** | The `overlay.orphan_weight_target` issue: `target_missing == true`, **no** selection, `missing_description` names skin, slot and attachment. Every other issue: `target_missing == false`, selection present. **By identity, never by count.** *(I6.)* |
| **V7** | One fixture carrying an issue of every MAR-186 code that has a target. For each: `panel`; the selection's fields **by name** — including all three `AttachmentSelection` fields, because the transposition (Task 0.10) is real; `animation_name`; `vertex_index`. Then `SelectionSet::replace` each present selection and assert the expected typed identity is active |

### 4.2 Watch them fail

V7 fails first: `plan_issue_navigation` returns `{}`.

### 4.3 Implement

A `switch` over `DiagnosticCode` (design §2.4). One arm per code, each producing
the panel and, where the issue has one, the selection — after consulting
`selection_item_exists`. `-Wswitch` makes a forgotten arm a compile warning
(Task 0.5), so no inversion is written for omission (§B.1).

### 4.4 Verify + inversions

Run **I6 → V6**.

---

## Task 5 — The three fixes

**Depends on:** Task 4. Files: `src/editor/safe_fix.cpp`,
`src/samples/editor_project_smoke.cpp`.

### 5.1 Test first — X1-X9

`validate_mar187_safe_fixes(const ProjectLoadResult&)`, registered after
`validate_mar187_problems_view`. Design §6.3 has the full text; the attribution
map:

| # | Covers | Inversions |
| --- | --- | --- |
| **X1** | one record not seven; six remaining identities by name; `undo_count()` **+1 exactly**; `undo()` byte-identical; `redo()` reproduces | I10, I11a-g |
| **X2** | the last of seven; `save_project` → **`load_project`**; `ghost` absent from the materialized skeleton; the `preview.stale_animation` interaction **asserted** | — (integration witness; §B.1) |
| **X3** | the orphan weight target; a second resolvable weight edit untouched | I11h |
| **X4** | one vertex not one attachment; the transposition; **control arm**: an already-canonical vertex changes nothing and records nothing | I7, I9, I25 |
| **X5** | `active_animation` becomes the **substituted** value; survives a round trip | I12 |
| **X6** | both copies of the stale skin gone, both copies of the resolvable one kept | I13 |
| **X7** | the 74-string corpus (three ids + `""` + four near misses + all 66 registry names); accepted set is **exactly** three, as a sorted set difference; `is_allowlisted_safe_fix` agrees on every entry; **asserts no number about the registry** | I14, I15 |
| **X8** | three rejection arms, each **on its message**, each byte-identical with `undo_count()`/`redo_count()` unchanged | I16 (a), I8 (c) |
| **X9** | fresh open: `dirty() == false`, `undo_count() == 0`, **identity list equal to what was written**; three collections idempotent | witness; over-determined with S6 |

### 5.2 Watch them fail

X1 fails first: `apply_safe_fix` returns `"not implemented"`.

### 5.3 Implement

Design §2.5's five steps, in order. Then:

- **`RemoveOrphanOverlay`** — a `switch` over `DiagnosticOverlayFamily` for the
  seven animation-scoped vectors, plus an arm keyed on
  `DiagnosticCode::OverlayOrphanWeightTarget` for `mesh_weight_attachment_edits`.
  Erase by matching the record's own key fields. **Do not** reimplement
  `erase_all_timeline_edits`' call list (Task 0.11, design §2.7).
- **`NormalizeWeights`** — resolve the attachment through
  `skeleton.find_skin_index` / `find_slot_index` / `find_attachment`, build
  `MeshWeightTarget{skin_name, slot_name, attachment_name}` **in that order**
  from an `AttachmentSelection` whose order is `{slot, skin, attachment}`, and
  pass `scope = {*issue.target.vertex_index}`.
- **`ResetPreviewReference`** — a `switch` over `DiagnosticCode`: the animation
  arm writes `session.preview_state().animation_name`; the skin arm erases every
  entry equal to the issue's named skin.

### 5.4 Verify + inversions

```bash
/tmp/mar187-rebuild.sh && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow
```

Run **I7 → X4** (using Task 0.10's recorded message), **I8 → X8(c)**,
**I9 → X4's control arm**, **I10 → X1**, **I11a-h** — **eight separate runs**,
seven against X1 and one against X3 — **I12 → X5**, **I13 → X6**,
**I14 → X7**, **I15 → X7**, **I16 → X8(a)**, **I25 → X4**.

For **I11**, additionally record that *omitting* an arm is a compile warning, not
a test failure, with the text Task 0.5 measured.

---

## Task 6 — The shell: the window, the router, the wiring

**Depends on:** Task 5. Files: `src/editor/shell_problems.hpp` (**new**),
`src/editor/shell_problems.cpp` (**new**), `src/editor/shell_state.hpp`,
`src/editor/shell_main.cpp`, `src/editor/shell_smoke_frames.cpp`,
`src/editor/shell_smoke_project.cpp`, `src/editor/shell_smoke_scenarios.hpp`,
`src/editor/shell_smoke.cpp`, `CMakeLists.txt`.

### 6.1 Test first — S1-S6

`validate_mar187_problems_shell_smoke(const ShellState& source_state)`, declared
in `shell_smoke_scenarios.hpp`, defined in `shell_smoke_project.cpp`, registered
on `run_headless_smoke`'s **main** rail after
`validate_constraint_parameter_shell_smoke` (`shell_smoke.cpp:140-143` at
`c523977`) — **after** the parameter-mode early return at `:82-88`.

Design §6.4 has the full text. Every case carries a comment stating that it is
**UI-free with respect to widgets** and proves the router, not the drawing.

| # | Inversions |
| --- | --- |
| **S1** | I19 |
| **S2** | I20, I21 |
| **S3** | — |
| **S4** | I22 |
| **S5** | I25's shell half |
| **S6** | over-determined with X9; the collector-ran-once assertion is its own |

### 6.2 Watch them fail

S1 fails first: nothing applies the navigation.

### 6.3 Implement

1. `kProblemsWindowTitle` beside the twelve at `shell_state.hpp:989-1000`;
   `ProblemsPanelState` and one member (design §2.11).
2. `shell_problems.cpp`:
   - `refresh_problems_if_revised(ShellState*)` — collects only when
     `problems_view_needs_refresh` says so, then rebuilds the view and clears
     `selected_identity` if `find_issue_by_identity` no longer resolves it.
   - `focus_window_for_panel(DiagnosticPanel)` — the **only** `switch` over
     `DiagnosticPanel` in the tree.
   - `activate_problem_row(ShellState*, const DiagnosticIssue&)` — calls
     `plan_issue_navigation`, then applies: `SelectionSet::replace` **only when
     the plan carries a selection**, `selected_animation_name`,
     `viewport_ffd_selection`, `apply_shell_mode`, and the focus request. On
     `target_missing`, write `missing_description` into `status_message` and
     leave the selection alone.
   - `draw_problems_window(ShellState*)` — the severity filter, the two group
     headers with the collector's counts, one row per issue with its code,
     message and identity, and a **Fix** button on rows whose `safe_fix_id` is
     non-empty. The button calls `apply_safe_fix` and nothing else. No sorting,
     no filtering, no `ProjectData` mutation in this file.
3. **Both** frame bodies: one `draw_problems_window` call in
   `shell_main.cpp`'s `render_shell_frame` and one in
   `shell_smoke_frames.cpp`'s `render_headless_smoke_frames` (Task 0.6's
   recorded insertion points).
4. One `DockBuilderDockWindow(kProblemsWindowTitle, dock_bottom_id)` in
   `ensure_default_dock_layout`. `kDockLayoutVersion` is **not** bumped
   (Task 0.6).
5. Two lines in `add_executable(marrow_editor_shell …)`.

### 6.4 Verify + inversions

```bash
/tmp/mar187-rebuild.sh
MARROW_CONFIG_HOME=/tmp/mar187-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2
```

Run **I19 → S1**, **I20 → S2**, **I21 → S2**, **I22 → S4**, **I25's shell
half → S5**.

---

## Task 7 — The frame that can see the widget

**Depends on:** Task 6. Files: `src/editor/shell_smoke_frames.cpp`.

### 7.1 Test first — F1

Design §6.5. The seven-step shape, after MAR-185's F1
(`shell_smoke_frames.cpp:1643-1902` at `c523977`). Non-negotiables, each from
`AGENTS.md`'s Headless Frame Smoke Notes:

- `SetWindowFocus(kProblemsWindowTitle)` **confined to the opening frames** — the
  window is a dock tab and renders no rows until focused, and forcing focus later
  closes any open popup.
- **Reproduce the full ID seed chain.** If the rows are inside a `BeginTable` or
  `BeginChild`, `window->GetID(label)` is the wrong seed and the sweep reports
  the widget "absent" for the wrong reason. Sweep at least one control that
  already exists so a broken seed cannot be mistaken for a missing widget.
- Re-read `ScrollMax` every iteration; it is computed in the window's `End()` and
  reads `0` on the first frame after a layout change.
- The **Fix** button needs its **own sweep column** — a column at the row's left
  edge misses a button after a `SameLine()`.
- Advance `io.DeltaTime` past `io.MouseDoubleClickTime` between the two clicks.
- Print the located coordinates every run.

### 7.2 Watch it fail

Before Task 6's `draw_problems_window` body exists, F1's sweep finds nothing.
Since Task 6 already wrote it, **delete the body temporarily** to see the
failure, then restore — this *is* I24, run early, and it must be recorded as
such.

### 7.3 Verify + inversion

Run **I24 → F1**. Then run the second half of R4's guard: remove the
`draw_problems_window` call from **`shell_main.cpp` only**, rebuild, and confirm
F1 **still passes** — that is the half no test can see, and it is why Task 9
greps both files. Record the result as a **scope finding**, not as coverage.

---

## Task 8 — Not needed

MAR-187 adds no agent operation and no MCP tool (design §2.13). This task number
is deliberately empty so the task list matches the arc's shape; the agent and MCP
suites run at Task 10 as **regression witnesses** only.

---

## Task 9 — Prove the scope did not leak, and the counts did not move

**Depends on:** Tasks 1-7. Not an assertion — a diff and a set of greps.

```bash
git diff --stat -- src/editor/timeline_model.cpp src/editor/timeline_model.hpp \
                   src/editor/timeline_graph_model.cpp src/editor/timeline_controller.cpp \
                   src/editor/authoring.cpp include/marrow/editor/authoring.hpp \
                   src/editor/session.cpp include/marrow/editor/session.hpp \
                   src/editor/agent_dispatch.cpp src/editor/agent_handlers_*.cpp \
                   include/marrow/c/ src/c/ include/marrow/runtime/ src/runtime/ \
                   tools/mcp/ assets/fixtures/
```

Must be **empty**. `assets/fixtures/` especially: `marrow_project_smoke`
**aborts** on a partial marker match, so editing a fixture silently disables
roughly two dozen suites and still exits 0.

`CMakeLists.txt`'s diff must be **three added source lines** (two into
`marrow_editor`, one into `marrow_editor_shell` -- the plan said four),
plus the frame-body check's `add_custom_command`/`add_custom_target` block.

The count sweep, **as a non-effect gate**:

```bash
git diff -U0 -- src/ tools/ | grep -E '^[+-]' | grep -E '\b(6[0-9])\b'
grep -c '^\s*{\s*"' src/editor/agent_dispatch.cpp                       # Task 0.12's number
grep -n "OperationExpectation, " src/samples/agent_dispatch_smoke.cpp   # unchanged
grep -rn "!= 6[0-9]U" src/ | wc -l                                      # 11, unchanged
grep -nE "== 6[0-9]" tools/mcp/test_client.py                           # two, unchanged
ctest --test-dir build -N | tail -1                                     # Task 0.2's number
```

Every line the first grep returns is inspected **by hand** and explained in the
`AGENTS.md` entry, against Task 0.12's untouchable-literal list.

The scope greps:

```bash
# 1. The window is drawn in BOTH frame bodies. R4.
#    NOT this grep -- it is satisfied by F1's scenario-local lambda and passes on
#    a tree whose shared smoke draw list has lost the call. Use the committed
#    check, which runs POST_BUILD on marrow_editor_shell anyway:
cmake --build build --target marrow_frame_body_check
# 2. The model layer is UI-free.
grep -nE "imgui|ImGui|shell_|sokol" src/editor/problems_model.cpp src/editor/safe_fix.cpp   # empty
grep -n "include" src/editor/problems_model.cpp src/editor/safe_fix.cpp
# 3. The shell layer owns no logic.
grep -nE "begin_edit|transaction|std::sort|\.erase\(" src/editor/shell_problems.cpp          # empty
# 4. A fix has exactly ONE call site, and it is a button.
grep -rn "apply_safe_fix" src/ | grep -v "safe_fix\.\(cpp\|hpp\)"
# 5. Every switch over a MAR-187 enum is contained.
grep -rn "SafeFixKind::\|ProblemsSeverityFilter::\|DiagnosticPanel::" src/ include/
```

(1) must return **two** files. (4) must return `shell_problems.cpp` once plus the
test files. (5) must show `DiagnosticPanel::` switched only in
`shell_problems.cpp`, `SafeFixKind::` only in `safe_fix.cpp`, and
`ProblemsSeverityFilter::` only in `problems_model.cpp` — design §2.12's
containment, and what makes a future added value a one-file job.

Inversion **I24's scope half** (Task 7.3) is recorded here as a scope finding,
not as coverage.

---

## Task 10 — Full verification

**Depends on:** Tasks 1-9. Run every command, record every output.

```bash
rm -rf build                                   # the from-scratch rebuild
cmake -S . -B build
cmake --build build 2>&1 | grep -c "warning:"  # compare against Task 0.5 step 1

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

# Project layer -- V0-V8, X1-X9, inside the standing invocation
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_project_smoke --create /tmp/mar187_created.marrow
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
  --export-runtime /tmp/player_idle_project_export.mskl \
  --export-binary  /tmp/player_idle_project_export.mbin
./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin \
                                 /tmp/player_idle_project_export.mskl

# Shell -- S1-S6, F1
MARROW_CONFIG_HOME=/tmp/mar187-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2

# Agent / MCP -- regression witnesses; MAR-187 adds no operation
./build/marrow_agent_dispatch_smoke

# CTest guardrails
ctest --test-dir build -N
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -L runtime
ctest --test-dir build --output-on-failure -L editor
ctest --test-dir build --output-on-failure -R marrow.renderer_link_boundary

# Dependencies and warnings
cmake --build build --target marrow_verify_third_party
cmake --build build --target marrow_constraint_warning_check

# MCP -- regression witness
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
| 1 | Every case V0-V8, X1-X9, S1-S6, F1 ran and passed | Each binary's output names them |
| 2 | **Every inversion in §B was run against the FROM-SCRATCH build, bit the named case, and was restored** | Twenty-five entries, of which I11 is **eight** separate runs — **thirty-two** recorded messages. `cmp`/`diff` each against an independently recorded first-run string (H2). Any that did not bite is recorded as such, with how the case was strengthened — **not** quietly dropped |
| 3 | **Every verification build deleted the object file** | `/tmp/mar187-rebuild.sh`, never `touch`. State it explicitly, and state that every path in the script was verified to exist |
| 4 | **Uniqueness was demonstrated, not asserted** | I4 (neuter V2 with `(void)` → V4 fails), I9 (neuter X1 → X4's control arm), I11h (neuter X1 → X3), I18 (neuter V8(a) → nothing fails; record that), I25 (neuter X4 → S5). **Never `if (false && …)`** — R10 |
| 5 | **Falsifiability re-demonstrated after any rewrite** | H3. If any case was rewritten after its inversion ran, re-run that inversion against the final tree |
| 6 | V0's and X9's witness status is stated, not hidden | Both pass on first run by design; §B.1 names them and I23 supplies V0's falsifiability |
| 7 | `serialize_project(player_idle.marrow)` unchanged | V0's printed length + SHA equal Task 0.14's |
| 8 | The registry is **unchanged**, and no twelfth `!= NNU` guard exists | Task 0.12's four numbers, re-measured; Task 9's grep |
| 9 | `ctest -N` total unchanged from Task 0.2 | This story registers no CTest |
| 10 | The untouchable trees are byte-identical; `CMakeLists.txt` is +4 lines | Task 9's `git diff --stat` |
| 11 | `problems_model.cpp` and `safe_fix.cpp` name no ImGui, shell or sokol symbol | Task 9 grep 2 |
| 12 | `draw_problems_window` appears in **both** frame bodies | Task 9 grep 1 |
| 13 | `apply_safe_fix` has exactly one non-test call site | Task 9 grep 4 |
| 14 | No switch over a MAR-187 enum outside its owning file | Task 9 grep 5 |
| 15 | The warning count on a from-scratch build equals Task 0.5 step 1's | Task 10's first `grep -c` |
| 16 | `~/Library/Application Support/Marrow` **ABSENT** after the whole run | `test -e ~/Library/Application\ Support/Marrow && echo PRESENT \|\| echo ABSENT`. PRESENT is a hard failure |
| 17 | Task 0's A10-A16 answers recorded | Pasted verbatim into `AGENTS.md` |
| 18 | `marrow_inspect --compare` → `matches` | Runs over `player_idle`, which carries no diagnostics data |
| 19 | Count sweep inspected by hand | Task 9's grep output, every line explained |

---

## Task 11 — Documentation and commit

**Depends on:** Task 10, complete and green.

### 11.1 `AGENTS.md`

A `## MAR-187 Add the Problems View and Safe Fixes Validation Results` section,
matching the MAR-183/184/185 sections' structure:

- **Opening paragraph.** What the story did and did not touch: no format change,
  no runtime file, no fixture, no CMake **target**, no timeline or graph model, no
  `session.cpp`, no `authoring.cpp`; `.mskl` v1 / `.mbin` v2 / C ABI untouched;
  the agent registry **unchanged** at Task 0.12's number and **no twelfth count
  site created**; `ctest -N` unchanged. And the correction the title invites:
  **MAR-187 adds no diagnostic — it adds a view, a router, three repairs, and one
  window.**
- **"MAR-186's `-Wswitch` premise was wrong, and MAR-187 depends on it being
  wrong."** A1 and Task 0.5's measured warning list, cross-referenced to
  `AGENTS.md`'s existing `## Repo facts that outlive their story` entry, so the
  claim is recorded once and pointed at twice.
- **"AC2's five destinations are three panels."** A9 and design §2.6, stated as a
  durable fact with the twelve window titles listed, so the next reader does not
  look for a Weight window.
- **"There are two frame bodies."** Design §1.2, with both line numbers and the
  Task 7.3 finding that removing the application's call is invisible to every
  test.
- **"What was measured before any code was written."** Task 0's values verbatim:
  the re-anchor mapping, the D1-D6 answer (and the amendment, if one was made),
  the A10 transaction-identity pairs, the A11 revision answer, A12, A13's
  primitive message, A14, A15's warning list, A16, the registry numbers, the
  `ctest -N` total, and the fixture SHA.
- **"Results"** — one row per case, V0-V8, X1-X9, S1-S6, F1.
- **"Inversions run"** — all twenty-five entries (**thirty-two** messages), each
  with the case it bit and the exact text, plus checklist row 4's five uniqueness
  demonstrations. Any that did not bite recorded as such.
- **"Methodology"** — every verification build deleted the object file rather
  than `touch`ing the source (H1); every uniqueness demonstration used
  `(void)expr;` and **not** `if (false && …)` (MAR-185's I9b); the final run was
  from `rm -rf build`.
- **"Document errors found"** — §A.1's nine rows plus everything Task 0 turned
  up. Every story from MAR-175 on has found between three and sixteen; if fewer
  than §A.1 already lists are found, one has been lost — say so explicitly.
- **"Not independently covered"**, carrying forward and adding:
  - **§B.1 verbatim**, in particular that **the preflight ordering is untested**
    because `begin_edit` + `cancel` is bit-identical (Task 0.7), and that
    omitting a `switch` arm is compiler-covered rather than inverted.
  - **AC2's hierarchy destination is never focused in its own right** (A9,
    design §10), with the reason.
  - **A fix repairs one record**, so seven orphan overlays need seven
    activations and the phantom animation survives until the last one goes
    (design §2.7, X2).
  - **`weights.uncanonicalizable` has no repair** — MAR-186's decision, shown as
    a row with no Fix button.
  - **F1 proves one row and one button**; grouping and filtering are UI-free.
  - **The refresh's quiescence half is a performance property**, not a
    correctness one.
  - Carry forward MAR-183's `shell_main.cpp` frame-body and `commit_path_choice`
    notes, MAR-184's runtime-parser asymmetries, MAR-185's `original_time` seam,
    and whatever MAR-186 added — unchanged.

Extend `## Current Validation` with **two** clauses: one on the existing
`marrow_project_smoke` line naming MAR-187's view and fix suites, and one on the
`marrow_editor_shell` line naming S1-S6 and F1. **Do not add a new project-smoke
command line** — there is one invocation.

### 11.2 The PRD

Set `MAR-187`'s `status` to `"done"` and `completedAt` to the run's date in
`.agents/tasks/prd-marrow-runtime.json`. Touch no other story.

### 11.3 Commit

One commit for the story.

```bash
git add -A
git commit
```

Subject, matching the arc's Korean convention:

```
feat: MAR-187 문제 뷰와 안전한 수정 추가
```

Body, in Korean, covering: 뷰 모델(`build_problems_view`)과 내비게이션
계획(`plan_issue_navigation`), 수정 적용기(`apply_safe_fix`)가 전부 UI 없는
`marrow_editor` 계층에 있고 `shell_problems.cpp`는 입력만 배선한다는 것; 뷰가
표시하는 개수는 **필터가 아니라 수집기의 것**이라는 것과 그 이유; 그룹 순서가
등장 순서가 아니라 Error→Warning 고정이며 정체성 정렬 때문에 이 차이가 특정
픽스처에서만 관찰된다는 것; 행 정체성이 인덱스가 아니라 이슈 identity라서 새로고침
후에도 선택이 유지된다는 것; 새로고침 키가 `project_revision`과
`runtime_revision` **둘 다**이며 `adopt_runtime_sources()`가 후자만 올린다는
측정 사실; 수정이 **레코드 하나**만 지우고 애니메이션 전체를 지우지 않는다는
결정과 그 결과(마지막 오버레이를 지울 때까지 팬텀 애니메이션이 남는다); 세 가지
수정이 정확히 허용 목록 세 개이며 66개 에이전트 연산 이름을 포함한 코퍼스로
증명된다는 것; 선검증-후변경 순서 덕분에 거부가 `serialize_project()`를
바이트 동일하게 남긴다는 것과, `begin_edit`+`cancel`이 비트 동일하기 때문에 그
순서 자체는 테스트 불가능하다고 기록했다는 것; 검사(inspection)는 절대 프로젝트를
더럽히지 않고 프로젝트 열기 시 자동 수정이 없다는 것; 그리고 `.marrow`, `.mskl` 1,
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
| R1 | **MAR-186 has not landed**, or landed without D6 | Task 0.3's gate. D6 missing is **blocking**: Task 1 amends MAR-186's header and records it. Never split `identity` to recover the family |
| R2 | **Every cited line has moved** | Task 0.1's re-anchor gate. Symbol first, line as a hint, non-resolution blocking |
| R3 | **Adding the window to one frame body only** | Task 9 grep 1 over both files; Task 7.3 demonstrates that F1 cannot see the application half |
| R4 | **`AttachmentSelection` → `MeshWeightTarget` transposition** | Task 0.10 re-measures it and records the primitive's real message; X4 reads the fields by name; I7 |
| R5 | **Asserting a count where an identity is available** | Every case asserts the full sorted identity list and reports a **named set difference** |
| R6 | **Asserting `!result` instead of the message** | X8's three arms each name their expected substring. The C++ mirror (`NaN < 0.0` is false) is recorded in the design |
| R7 | **`if (false && …)` neutering** — MAR-185's I9b passed for the wrong reason | Checklist row 4 mandates `(void)expr;` |
| R8 | **A vacuous inspection case** (H4) | V0 snapshots seven values a mutation **can** move; I23 proves it. No `SelectionSet`-around-a-`const`-function case is written |
| R9 | **Wiring the Fix button to `normalize_weights_command`** — its unnarrowed scope is *every* vertex | X4's second non-canonical vertex must survive; I25, run at both layers |
| R10 | **Creating a twelfth count site** in X7 | X7 reads `agent_operation_descriptors()` and asserts **no** number about the registry. Task 9's diff sweep catches a stray literal |
| R11 | **Registering the shell suite behind the parameter-mode early return** | Task 0.1 calls the branch out by name; Task 6.1 registers on the main rail |
| R12 | **Pointing `marrow_project_smoke` at a new fixture** takes the skip branch, runs nothing, exits 0 | Build throwaway projects inside the standing invocation |
| R13 | **Editing a fixture** — the smoke **aborts** on a partial marker match | Task 9's `git diff --stat -- assets/fixtures/` must be empty |
| R14 | **A passing `save()` proving nothing** | Every round trip goes `save_project` → `load_project(path)` |
| R15 | **An inversion result that lies (H1-H4)** | `/tmp/mar187-rebuild.sh` deletes the object on **every** build, and every path in it is verified to exist; `cmp` over whole strings; neuter earlier detectors with `(void)`; final run from `rm -rf build` |
| R16 | **Believing the compiler is silent about enums** — MAR-186's design says so | Task 0.5 re-measures. Switches are policed; `safe_fix_kind_for`'s string table (guarded by X7) and the two frame-body call lists (guarded by Task 9 grep 1) are not |
| R17 | **A case written later running earlier** — MAR-185's I3 | §B attributes by run order; I4's attribution to V2 rather than to V4, its author's target, is the worked example |
| R18 | **F1 sweeping with the wrong ID seed** — MAR-185 lost time to `BeginTabItem("Dopesheet")` | Task 7.1 requires reproducing the full chain and sweeping a control known to exist |
| R19 | **The Problems window is a dock tab and renders nothing until focused** | Task 7.1's opening-frame `SetWindowFocus`; `shell_smoke_frames.cpp:1605-1607` records the same property for the Hierarchy |

---

## Dependency graph

```
MAR-186 committed, with D1-D6 present (or D6 amended at Task 1)
  └─> Task 0  (measure; 0.1 re-anchor, 0.3 D6 gate, 0.5 -Wswitch, 0.6 two frame
      │        bodies, 0.7 transaction identity, 0.8 runtime revision,
      │        0.9 empty preview_skins, 0.10 transposition + message,
      │        0.11 authoring reach, 0.12 registry, 0.13 object-deletion harness)
      └─> Task 1  (two headers, two empty .cpp, CMake, the D6 amendment; then 0.5)
            └─> Task 2  (grouping, filters, counts)     V0 V1 V2 V3 V4
                  └─> Task 3  (row identity, refresh key)   V5 V8
                        └─> Task 4  (navigation, removed targets)  V6 V7
                              └─> Task 5  (the three fixes)   X1-X9
                                    └─> Task 6  (window, router, both frame bodies)  S1-S6
                                          └─> Task 7  (the real-mouse frame)   F1, I24
                                                └─> (Task 8 deliberately empty)
                                                      └─> Task 9  (scope + non-effect sweep)
                                                            └─> Task 10 (full verification, from scratch)
                                                                  └─> Task 11 (docs + one commit)
```

Task 9 can start any time after Task 7's last production edit; it is placed late
because a diff is only meaningful over the finished tree. **Task 4 adds no
production code the earlier tasks did not need** — `plan_issue_navigation` is new,
but V6 and V7 exercise nothing Task 2 or 3 wrote — so a first-run failure there
is expected and is the whole point of the ordering. **Task 7 writes no production
code at all**: Task 6 drew the window, and F1 exists solely to prove that the
drawing is reachable by a mouse, which no UI-free case in this story can.
