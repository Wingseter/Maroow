# MAR-187 Add the Problems View and Safe Fixes — Design

Story: `MAR-187`, `.agents/tasks/prd-marrow-runtime.json`, six acceptance
criteria, `dependsOn: ["MAR-186"]`.
Plan: `docs/superpowers/plans/2026-09-01-mar-187-problems-view-and-safe-fixes.md`
Written 2026-09-01 against tip **`c523977`**
(`feat: MAR-185 상속 타임라인 편집 패리티 완성`), tree clean.

> **Two stories ahead of the tree. MAR-186 is planned and NOT implemented.**
> `docs/superpowers/{specs,plans}/2026-09-01-mar-186-*` are **untracked files**,
> and nothing they describe exists in the source: `grep -rn "collect_project_diagnostics\|DiagnosticIssue\|DiagnosticReport\|is_allowlisted_safe_fix" src/ include/`
> returns **zero** hits at `c523977`. Every MAR-186 artifact named below —
> `DiagnosticReport`, `DiagnosticIssue`, `DiagnosticTarget`, `DiagnosticPanel`,
> the three `kSafeFix*` constants — is named as a **future** dependency and is
> never described as present. §0.2 states exactly what MAR-186 must land, and
> §0.2.1 states the one thing MAR-186's design **does not currently promise** and
> must.
>
> **Every `file:line` here is a hypothesis with a short shelf life.** The
> **symbol** is the anchor; the line is a hint. By the time MAR-187 is
> implemented the tree will have moved at least once (MAR-186's eleven tasks) and
> probably twice. A citation that no longer names the construct described here is
> a **blocking finding**, written into `AGENTS.md`'s document-errors table, never
> a line to silently follow. On MAR-185 that gate found **sixteen** document
> errors, one of which overturned the story's stated central hazard.

---

## 0. Where this story sits

### 0.1 The story in one sentence

Put MAR-186's `DiagnosticReport` on screen as a **Problems** panel that groups
and filters by severity, activates a row into the right selection and the right
panel, refreshes when either revision moves, and offers exactly **three**
repairs — each one an explicitly invoked, preflighted, single-transaction,
single-undo-entry mutation — while **inspection itself never writes anything**.

MAR-187 ships no new diagnostic. It adds no issue code, no severity, and no
`DiagnosticPanel` value. It adds **no agent operation and no MCP tool** (§2.13),
so the registry stays where MAR-186 left it. What it adds is a view model, a
navigation router, a fix applier, one window, and the tests that prove all four.

### 0.2 MAR-186 is a hard dependency and **none of it exists yet**

| # | What MAR-186 must land | Why MAR-187 needs it | Status at `c523977` |
| --- | --- | --- | --- |
| **D1** | `collect_session_diagnostics(const EditorSession&) -> std::optional<DiagnosticReport>` | The only source of issues. MAR-187 collects through the session because it needs `project_revision` and `runtime_revision` on the report (AC3) | **Does not exist** |
| **D2** | `DiagnosticReport{issues, error_count, warning_count, project_dirty, project_revision, runtime_revision}` | AC1's counts and AC3's refresh key come straight off it | Does not exist |
| **D3** | `DiagnosticIssue{code, severity, identity, message, target, safe_fix_id}` with **sorted, de-duplicated, strictly increasing** identities | The row identity that survives a refresh (AC3) is the issue identity. If identities are not unique, two rows collapse and the selected row can jump | Does not exist |
| **D4** | `DiagnosticTarget{panel, selection, animation_name, vertex_index}` and `DiagnosticPanel{Project, Timeline, Weights}` | AC2's navigation. `selection` is `std::optional<SelectionItem>` from `include/marrow/editor/selection.hpp` | Does not exist |
| **D5** | `kSafeFixRemoveOrphanOverlay` / `kSafeFixNormalizeWeights` / `kSafeFixResetPreviewReference` and `is_allowlisted_safe_fix` | AC4's allowlist. MAR-187 enforces it; it does not redefine it | Does not exist |
| **D6** | A **typed discriminator** on `DiagnosticIssue` sufficient to apply a fix without parsing `identity` — see §0.2.1 | Without it, `remove_orphan_overlay` cannot know which of the eight overlay vectors holds the record | **Not promised by MAR-186's design.** Blocking |

**Nothing else about MAR-186 is consumed.** MAR-187 never calls the pure
`collect_project_diagnostics`, never reads `authored_animation_names`, and never
re-derives an identity. If it finds itself doing any of those, MAR-186
under-delivered — which is MAR-186's own §2.11 formulation, applied back to it.

### 0.2.1 The one thing MAR-186's design does not promise, and must

MAR-186's `DiagnosticIssue` carries `code` as a **`std::string`** and encodes the
overlay family only as a token **inside the identity string**
(`overlay.orphan_animation|inherit|ghost|arm_l`). MAR-186's §2.11 hands MAR-187
"three fixes keyed on `safe_fix_id`".

That is not enough to apply a fix:

- `remove_orphan_overlay` must erase **one record** (§2.7) from **one** of eight
  vectors — the seven animation-scoped overlay vectors plus
  `mesh_weight_attachment_edits`. Which one is recoverable **only** by splitting
  the identity on `|` and un-escaping the tokens. Doing that would make MAR-187
  a second parser for MAR-186's private encoding, and a `|` inside a user
  animation name — the exact hazard MAR-186's own §2.2 escaping exists for —
  would silently route the erase to the wrong vector.
- `reset_preview_reference` covers **two** codes (`preview.stale_animation`,
  `preview.stale_skin`) with different arms. Selecting the arm from a
  `std::string` code is an unpoliced if/else chain; from an enum it is a
  `-Wswitch`-policed `switch` (§2.12).

**Required amendment to MAR-186**, in its own header, before MAR-187 starts:

```cpp
enum class DiagnosticCode {
    OverlayOrphanAnimation, OverlayOrphanWeightTarget,
    WeightsNonCanonical,    WeightsUncanonicalizable,
    PreviewStaleAnimation,  PreviewStaleSkin,
    ProjectUnsavedChanges,
};
enum class DiagnosticOverlayFamily {          // meaningful only for OverlayOrphanAnimation
    Transform, Inherit, Deform, DrawOrder, Event, SlotColor, SlotAttachment,
};
struct DiagnosticIssue {
    DiagnosticCode code_kind{};                                  // added
    std::optional<DiagnosticOverlayFamily> overlay_family;       // added
    std::string code;                                            // unchanged, still the wire string
    // … everything else unchanged
};
```

If MAR-186 has already landed without them, **MAR-187's Task 1 adds them to
MAR-186's header and records it as a scope finding in `AGENTS.md`** — an
amendment to a just-landed story is a documented event, not a silent edit. The
plan's Task 0.3 decides which of the two worlds it is in and writes the answer
down.

### 0.3 Errors found in the incoming brief and documents

Every story in this arc has found errors in its governing documents (175 three,
176 seven, 177 six, 178 six, 179 six, 180 seven, 181 four plus a later fifth,
182 six, 183 six, 184 nine, 185 sixteen). This design was written by verifying
every structural claim against the source at `c523977`. **Nine** were wrong, and
one whole class of claim in the incoming brief was **right** and is recorded as a
confirmation because an earlier brief in the same session had it backwards.

| # | Source | Claim | Measured at `c523977` |
| --- | --- | --- | --- |
| **E1** | MAR-186 design §1.6, §2.12; plan §0.4, R11 | "`CMakeLists.txt` carries no `-Wall`, no `-Wextra`, no `-Wswitch` … Adding a value to either enum produces **zero diagnostics at every switch site**" | **The grep is right and the conclusion is false.** Clang enables `-Wswitch` with no flag. Re-measured directly: a two-armed `switch` over a three-value `enum class`, compiled `c++ -std=c++17 -c` with **no** warning flags, emits `warning: enumeration value 'C' not handled in switch [-Wswitch]`. `AGENTS.md`'s `## Repo facts that outlive their story` (`:305-334`, added by MAR-185 **after** MAR-186's design was written against `6986024`) records MAR-185's own measurement: **24** warnings from a throwaway `TimelineKeyKind` value, against **0** on a pristine rebuild. Consequence for MAR-187 in §2.12: switches are compiler-policed, and only **if/else chains and hand-maintained call lists** are blind |
| **E2** | MAR-186 design §1.2, `G-c`; plan §0.10 | "Three hard parser rules", gated as three greps each returning one hit | **Four hits.** `skeleton_parse.cpp:1616` (`mesh weight references unknown bone`), `:5270` (`animation references unknown bone`), and `animation references unknown slot` at **both** `:5373` **and `:5430`**. The gate as written fails on a correct tree |
| **E3** | MAR-186 design §1.3 vs §2.5 | §1.3 lists **six** `ensure_object_member(animations, …)` merge loops (`project.cpp:5722,5739,5751,5761,5768,5775`); §2.5 lists **seven** overlay families | At `c523977` there are **seven** loops: `:5722, :5739, :5751, :5761, :5768, :5775, :5784`. The design's two sections disagree with each other; §2.5 is the correct one |
| **E4** | MAR-186 design `G-i`; plan §0.4 | `grep -rn "DiagnosticSeverity\|DiagnosticPanel\|safe_fix\|issue_count" src/ include/ tools/` → **expect 0** | **Never zero.** `src/runtime/spine_import.cpp` defines and uses `SpineImportDiagnosticSeverity` (ten-plus hits), which contains `DiagnosticSeverity` as a substring. The gate must be anchored (`\bDiagnosticSeverity\b` or `marrow::editor::Diagnostic`) or it reports a false stop |
| **E5** | MAR-186 design §1.5 | The window-title constants are at `shell_state.hpp:979-990` | `:989-1000`. Twelve constants, `kProjectWindowTitle` first |
| **E6** | MAR-186 design §2.1 | `ensure_project_loaded` at `agent_dispatch.cpp:531-534` | `:536`. Called at `:1091` |
| **E7** | MAR-186 design §3.1 | "`marrow_editor`'s `src/editor` include directory is `PRIVATE` and consumers add it by hand" | `target_include_directories(marrow_editor_shell PRIVATE …)` (`CMakeLists.txt:916-921`) lists only `external` and `src/renderer/generated`. The shell reaches `shell_state.hpp` through the **same-directory** rule for quoted includes, because its own sources live in `src/editor/`. The **conclusion** — a public header in `include/marrow/editor/` — stands, and MAR-187 follows it |
| **E8** | MAR-186 design §2.11 | MAR-187 gets "three fixes keyed on `safe_fix_id`" | Insufficient. §0.2.1: a fix also needs the code and the overlay family in typed form |
| **E9** | AC2, read literally | "focuses the appropriate **hierarchy, timeline, inspector, weight, or preview** panel" — five panels | **Two of the five are not panels and one is unreachable.** The shell has exactly twelve windows (`shell_state.hpp:989-1000`): Project, Runtime Assets, Constraints, Hierarchy, Timeline, Viewport, Properties, Agent, Parameters, Shapes / Deformers, Expressions, Lip Sync. There is **no Weight window** — weight authoring is the **Properties** window in `ShellMode::WeightPaint` (`shell_inspector.cpp`, `inspector_bone_pose_editable` at `:489-495`) plus a viewport overlay — and **no Preview window** — the preview reference is shown in the **Project** window (`shell_project_panels.cpp:1023-1039`). And no MAR-186 code targets the Hierarchy. §2.6 resolves this; §10 records the residue |

**The incoming brief was accurate.** Every one of its measured claims was
re-verified and held: the registry is **66**; the count sites are
`agent_dispatch_smoke.cpp:39` plus **eleven** `!= 66U` guards spread over
`shell_smoke_constraints.cpp:147,676`, `shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603`
and `shell_smoke_timeline.cpp:3697,4389` — all three files — plus
`tools/mcp/test_client.py:53,55`; `build_project_runtime` is defined at
`project.cpp:8258` and called at `:7932`; `-Wswitch` is on by default; MAR-185
landed at `c523977`. Recorded as confirmations because E1 shows how easily the
opposite belief propagates.

---

## 1. The measured gap

Everything in this section was run against `c523977`.

### 1.1 There is no Problems surface, and no vocabulary for one

```
grep -rn "Problems\|problems_" src/editor/ include/marrow/editor/   -> 0
grep -rn "collect_session_diagnostics\|DiagnosticReport"  src/ include/ -> 0
grep -rn "\bDiagnosticSeverity\b\|\bDiagnosticPanel\b"    src/ include/ -> 0
```

`project.diagnostics` (`agent_dispatch.cpp:41`, handled at
`agent_handlers_inspection.cpp:342`) returns four constants and is the only thing
in the tree that calls itself diagnostics. MAR-186 replaces its payload; MAR-187
never reads it — it calls the C++ collector directly.

### 1.2 There are **two** frame bodies, and a window added to one is invisible to the other

This is the single most consequential shell measurement in the design.

| Frame body | Site | Draws |
| --- | --- | --- |
| The **application** frame | `render_shell_frame`, `shell_main.cpp:522-600` | menu bar, dockspace, mode wash, then `draw_project_window`, `draw_runtime_window`, `draw_constraints_window`, `draw_timeline_window`, `draw_hierarchy_window`, `draw_viewport_window`, `draw_inspector_window`, parameter windows in Parameter mode, agent panel when open |
| The **headless smoke** frame | `render_headless_smoke_frames`, `shell_smoke_frames.cpp:49-77` | a **hand-duplicated** copy of the same list |

They share `ensure_default_dock_layout` (defined `shell_main.cpp:443`, declared
`shell_state.hpp:1210`, also called from `shell_smoke_parameters.cpp:812`) and
nothing else. `AGENTS.md` already records that `shell_main.cpp`'s frame body is
reachable from no test; the corollary MAR-187 must live with is the reverse —
**a window drawn only in the smoke's body ships in no application**, and a window
drawn only in the application's body is invisible to F1. Both call sites are
mandatory, and Task 9 greps for both.

Adjacent measured fact, because the opposite is the natural assumption:
`io.IniFilename = nullptr` in **both** the application (`shell_main.cpp:710`) and
the smoke (`shell_smoke.cpp:63`). No dock layout survives a run,
`default_dock_layout_initialized` is `false` on every launch, and
`kDockLayoutVersion` (`shell_state.hpp:206`, currently **4**) therefore does
**not** need bumping for a new window.

### 1.3 The transaction primitive already gives AC5 exactly what it asks for

`EditorSession::begin_edit` / `EditTransaction::commit` / `::cancel`
(`include/marrow/editor/session.hpp:340,382-427`; `session.cpp`) is the shipped
"one validated transaction, one undo entry" primitive, and `run_weight_command`
(`shell_weight_paint.cpp:975-1031`) is its canonical five-step shape:

```
begin_edit(descriptor)  ->  mutate transaction.project()  ->  !result ? cancel()
                        ->  !result.changed ? cancel()    ->  commit()
```

Measured properties MAR-187 leans on:

- `begin_edit` fails when no project is loaded and when a transaction is already
  active, each with its own message (`session.cpp`, `SessionErrorCode::NoProject`
  / `TransactionAlreadyActive`).
- `commit()` produces exactly **one** history entry.
- `cancel()` restores the project, runtime and preview captured at `begin_edit`.
- **`begin_edit` does not touch the redo stack**, and `cancel` restores exactly.
  This is what makes "open the transaction before the preflight" **unobservable**
  through the session's public surface — see §5.1, where it is recorded as
  deliberately uninverted rather than covered by a case that cannot fail.

### 1.4 The three repairs each already have a primitive, or a reason they cannot

| Fix | Primitive | Reachable? |
| --- | --- | --- |
| `normalize_weights` | `normalize_mesh_weights(ProjectData*, const SkeletonData&, const AttachmentData&, const MeshWeightTarget&, const std::vector<std::size_t>& scope)` — `authoring.hpp:729-734` | **Yes, public.** `MeshWeightResult::affected_vertices` (`:702`) is the "changed nothing" signal, and an explicit `scope` is what limits the repair to the issue's own vertex |
| `remove_orphan_overlay` | `erase_all_timeline_edits(ProjectData*, std::string_view)` — `authoring.cpp:136` | **No.** It sits inside `authoring.cpp`'s anonymous namespace (`namespace {` at `:20`, closed at `:1309`) and is declared in **no** header. §2.7 explains why MAR-187 must **not** reimplement its seven-vector list |
| `reset_preview_reference` | none | There is no primitive. §2.8 defines it as writing the value the runtime **already substitutes**, so the repair changes stored data and not behaviour |

### 1.5 The preview substitution is live, and the project is never corrected

`PreviewController::normalize_state` (`session.cpp:723-806`) is where a stale
preview reference becomes invisible:

```cpp
// :724-733  — skins: keep only those that resolve, AND de-duplicate
// :754-758  — animation:
if (!state->animation_name.empty() && data.find_animation(state->animation_name) == nullptr) {
    state->animation_name = data.animations().empty() ? std::string{} : data.animations().front().name;
}
```

It operates on `PreviewState`, **not** on `ProjectData`. `editor_metadata`
seeds `PreviewState` at open (`session.cpp:1743-1744`, again at `:1813-1814`,
`:1860-1864`) and **nothing writes the normalized value back**: the only writers
of `editor_metadata.active_animation` in the tree are `create_minimal_project`
(`project.cpp:7712-7719`) and the animation-catalog cascade (`session.cpp:2458`).
So the stale name survives every save and every reload, and re-substitutes
silently on every open. That is exactly the state `reset_preview_reference`
repairs, and §2.8's choice of replacement value falls straight out of it.

Two adjacent measurements the fix depends on:

- **An empty `preview_skins` vector is legal for save.**
  `validate_project_for_save` (`project.cpp:5825`) rejects an empty *name*
  (`:5943-5948`, `"preview skin names must not be empty"`) and says nothing about
  an empty vector. `create_minimal_project` inserts `"default"` only when the
  caller supplied none (`:7715-7717`). So dropping the last stale skin is a legal
  end state and needs no fallback.
- **`normalize_state` de-duplicates as well as filters** (`:727-732`). A repair
  that rebuilt `preview_skins` from `preview_state().skin_names` would therefore
  also collapse a **resolvable** duplicate that no issue named — which is why
  §2.8 erases by name instead.

### 1.6 The weight surface is a mode of the Properties window, not a window

`current_mesh_weight_paint_target` /
`resolve_weight_paint_selection_context` (`shell_weight_paint.cpp`) derive the
paint target from `state.selection` — an `AttachmentSelection` in the set is
what makes an attachment the target — and the **numeric influence table** in
`shell_inspector.cpp` additionally requires
`state.viewport_ffd_selection->vertex_indices.size() == 1U` and a matching
`scope.slot_index`. `inspector_bone_pose_editable` (`:489-495`) shows the mode
gate from the other side.

So "focus the weight panel for vertex 7 of `mesh_base/body/body_mesh`" decomposes
into four concrete writes, all of them already the shell's own vocabulary:
`SelectionSet::replace(AttachmentSelection{...})`,
`state->viewport_ffd_selection = {scope, {7}}`,
`apply_shell_mode(state, ShellMode::WeightPaint)`
(`shell_project_panels.cpp:470`), and `ImGui::SetWindowFocus(kPropertiesWindowTitle)`.

### 1.7 The shell smoke has one driver, one early return, and a fixed rail

`run_headless_smoke` (`shell_smoke.cpp:47-154`) is the whole shell suite. Two
shapes matter:

- **An early return at `:82-88`.** A project whose `parameter_model` is present
  runs `validate_parameter_mode_shell_smoke` and **returns**. Every other suite
  is skipped. MAR-187's suite must be on the main rail, not behind that branch.
- The rail is a fixed sequence of `if (!validate_…) { DestroyContext(); return 1; }`
  blocks (`:90-143`) followed by one `&&` chain (`:145-150`) ending in
  `render_headless_smoke_frames` and `validate_mar181_frame_body_applied_pending`.
  MAR-185's `validate_inherit_editing_shell_smoke` is at `:132`.

The project smoke has the mirror-image gate: **one** invocation, every editing
suite inside `main()`'s marker-gated `else`
(`editor_project_smoke.cpp:16133-16134` is the `markers.present.empty()` skip,
`:16144-16153` the partial-match **abort**, the `else` from `:16154`). MAR-185's
`validate_mar185_inherit_editing()` registers at `:16282`. Pointing the binary at
another fixture takes the **skip** branch and runs nothing while exiting 0.

### 1.8 The registry does not move, and a bare numeric grep is unsafe

MAR-187 adds **no** agent operation and **no** MCP tool (§2.13), so the count
sweep is a **non-effect** gate. Measured, and the values Task 0 re-measures:

```
grep -c '^\s*{\s*"' src/editor/agent_dispatch.cpp                        -> 66
grep -n "OperationExpectation, " src/samples/agent_dispatch_smoke.cpp    -> :39  (…, 66>)
grep -rn "!= 66U" src/ | wc -l                                           -> 11
grep -n "== 66" tools/mcp/test_client.py                                 -> :53, :55
```

Eleven guards, in **three** files: `shell_smoke_constraints.cpp:147,676`,
`shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603`,
`shell_smoke_timeline.cpp:3697,4389`. A bare `\b66\b` grep also matches decimals,
colours and line counts, so the diff sweep is inspected by hand against Task 0's
untouchable-literal list.

**MAR-187 must not create a twelfth count site.** X7 (§6.3) reads
`agent_operation_descriptors()` / `agent_operation_descriptor_count()`
(`include/marrow/editor/agent_dispatch.hpp:57-58`) to sweep the registry's names
against the fix allowlist, and asserts **no number** about the registry. Writing
`!= 66U` there would hand the next story a hand-edited site it did not sign up
for.

---

## 2. Decisions

### 2.1 Three layers, and the boundary each one cannot cross

```
include/marrow/editor/problems_model.hpp   (new, public)   -- UI-free. No ImGui, no ShellState.
src/editor/problems_model.cpp              (new)              build_problems_view, plan_issue_navigation,
                                                              find_issue_by_identity, problems_view_needs_refresh
include/marrow/editor/safe_fix.hpp         (new, public)   -- UI-free. Takes EditorSession&.
src/editor/safe_fix.cpp                    (new)              apply_safe_fix, safe_fix_kind_for
src/editor/shell_problems.{hpp,cpp}        (new)           -- ImGui only. Wires input; owns no logic.
```

`problems_model` and `safe_fix` compile into `marrow_editor`; `shell_problems`
into `marrow_editor_shell`. `marrow_editor` contains **no** `shell_*.cpp`
(measured: `CMakeLists.txt:499-519`), so the model layer physically cannot reach
`ShellState`, `ImGui`, or a window title. That makes "UI-free" a property of the
build graph, not a claim.

The shell layer's rule, stated so a reviewer can check it: **`shell_problems.cpp`
contains no filtering, no grouping, no ordering, and no mutation of
`ProjectData`.** It reads a `ProblemsView`, emits widgets, and on a click calls
`plan_issue_navigation` or `apply_safe_fix`. Task 9 greps it for
`transaction`, `begin_edit`, `std::sort` and `erase` and expects none.

**UI-free assertions are not frame coverage.** Every case in §6.2, §6.3 and §6.4
proves a *handler*; F1 (§6.5) is the only case that can observe that a widget is
on screen. `AGENTS.md`'s Headless Frame Smoke Notes record the MAR-178 worked
example where deleting a button body left the UI-free scenario printing its full
success line. Every UI-free case in this story is labelled as such in its own
comment.

### 2.2 The view model

```cpp
// include/marrow/editor/problems_model.hpp
enum class ProblemsSeverityFilter { All, ErrorsOnly, WarningsOnly };

struct ProblemsGroup {
    DiagnosticSeverity severity{DiagnosticSeverity::Error};
    std::vector<std::size_t> issue_indices;   // ascending indices into report.issues
};

struct ProblemsView {
    std::vector<ProblemsGroup> groups;        // Error group first; an EMPTY group is omitted
    std::size_t error_count{0};               // the COLLECTOR's count
    std::size_t warning_count{0};             // the COLLECTOR's count
    std::size_t visible_count{0};             // rows this filter shows
    std::uint64_t project_revision{0};
    std::uint64_t runtime_revision{0};
};

ProblemsView build_problems_view(const DiagnosticReport& report, ProblemsSeverityFilter filter);
std::optional<std::size_t> find_issue_by_identity(const DiagnosticReport&, std::string_view identity);
bool problems_view_needs_refresh(const ProblemsView& cached, std::uint64_t project_revision,
                                 std::uint64_t runtime_revision);
```

Four decisions, each with a mutation that is invisible to everything else:

1. **`error_count` and `warning_count` are the report's, never the filter's.**
   AC1 says the view "displays collector counts". Under `ErrorsOnly` the view
   still reports how many warnings the project has — that is the number the user
   filtered *away*, and re-deriving it from the visible rows makes it read `0`.
   The mutation is invisible under `All`, which is why V3 exists.
2. **Groups are ordered Error, then Warning.** Not by first appearance: the
   report is sorted by **identity**, and identity begins with the code, so a
   project whose only issues are `overlay.orphan_weight_target` (Warning) and
   `weights.uncanonicalizable` (Error) has a **Warning first**. V1's fixture is
   built on exactly that pair so the mutation is observable.
3. **A group with no rows is omitted, not emitted empty.** A `WarningsOnly`
   filter over an errors-only report yields `groups.size() == 0`, not one empty
   Warning group. The view then has one shape for "nothing to show" instead of
   two.
4. **Row identity is the issue identity, never the row index.** `find_issue_by_identity`
   is how a selected row survives a refresh. An index-derived identity is stable
   within one build and across two builds of an unchanged project, so only an
   insertion **before** the remembered row exposes it — V5.

`build_problems_view` never sorts: MAR-186 guarantees `issues` is already sorted
and strictly increasing by identity, so the groups' `issue_indices` are ascending
by construction and the view inherits determinism instead of re-establishing it.
If MAR-187 ever needs to sort, MAR-186 under-delivered.

### 2.3 Refresh is keyed on **both** revisions, and the second one is the interesting one

```cpp
bool problems_view_needs_refresh(const ProblemsView& cached, std::uint64_t p, std::uint64_t r) {
    return cached.project_revision != p || cached.runtime_revision != r;
}
```

`project_revision` is the obvious half. The half AC3 is actually protecting is
`runtime_revision`: `EditorSession::adopt_runtime_sources()`
(`session.hpp:262-283`, `session.cpp:1895`) is documented — and Task 0 re-measures
— to bump the **runtime and preview** revisions while leaving `project_revision`
**unmoved**, because the authored project is untouched. A hot reload that drops a
skin from the `.mskl` therefore creates a `preview.stale_skin` issue with **no
project-revision change at all**. A view keyed on `project_revision` alone shows
a clean project over a broken one, indefinitely.

The shell caches one `ProblemsView` and one `DiagnosticReport` in `ShellState`
and re-collects only when this returns true. Note the asymmetry the plan records:
an implementation that always returns `true` is **behaviourally correct** and
merely re-collects every frame, so V8 asserts the negative arm (an unchanged
session needs no refresh) explicitly — it is a quiescence property, not a
correctness one, and it needs its own assertion or nothing tests it.

### 2.4 Navigation is planned UI-free and applied by the shell

```cpp
struct ProblemsNavigation {
    DiagnosticPanel panel{DiagnosticPanel::Project};
    std::optional<SelectionItem> selection;   // ABSENT when the target does not resolve
    std::string animation_name;               // empty when not animation-scoped
    std::optional<std::size_t> vertex_index;
    bool target_missing{false};
    std::string missing_description;          // non-empty exactly when target_missing
};

ProblemsNavigation plan_issue_navigation(const DiagnosticIssue&, const runtime::SkeletonData&);
```

The whole of AC2's decision-making is here, in `marrow_editor`, testable from
`marrow_project_smoke` without an ImGui context. The shell's job is four writes
and one `SetWindowFocus`.

**`target_missing` is AC3's mechanism.** `selection_item_exists`
(`include/marrow/editor/selection.hpp:143-145`) is consulted for every issue
that carries a `target.selection`. When it returns false the plan carries
**no** selection and a description naming what is gone. There is exactly one
issue class where this fires today and MAR-186 says so in its own §2.9:
`overlay.orphan_weight_target`'s `AttachmentSelection` **deliberately does not
resolve** — naming the missing triple is the only way the user can see what is
orphaned.

Replacing the selection with an identity no runtime resolves is not harmless:
`reconcile_selection_to_runtime` (`selection.hpp:148-150`) drops it at the next
adoption, `resolve_weight_paint_selection_context` skips it while scanning
(`shell_weight_paint.cpp`, the `find_slot_index`/`find_skin_index`/`find_attachment`
guard), and in between, the Properties panel shows an attachment that is not
there. So: **a missing target leaves the existing selection alone.**

### 2.5 Fixes: preflight, then one transaction, then one undo entry

```cpp
// include/marrow/editor/safe_fix.hpp
enum class SafeFixKind { RemoveOrphanOverlay, NormalizeWeights, ResetPreviewReference };

struct SafeFixResult {
    bool ok{false};
    bool changed{false};
    std::string error;              // empty on success
    std::string applied_identity;   // the identity that was repaired
};

std::optional<SafeFixKind> safe_fix_kind_for(std::string_view safe_fix_id);
SafeFixResult apply_safe_fix(EditorSession& session, const DiagnosticIssue& issue);
```

Five steps, in this order, and the order is the design:

1. **Session preflight.** `!session.has_project()` or `session.transaction_active()`
   → reject, named message. No transaction is opened.
2. **Allowlist preflight.** `safe_fix_kind_for(issue.safe_fix_id)` → `nullopt`
   rejects. The mapping is the **only** place a string becomes a fix, it is total
   over exactly three strings, and `is_allowlisted_safe_fix` (MAR-186's, D5) is
   asserted to agree with it on every input X7 sweeps.
3. **Freshness preflight.** Re-run `collect_session_diagnostics(session)` and
   require an issue with the **same identity** and the **same `safe_fix_id`** to
   still be present. A `ProblemsView` can be one revision behind the session — the
   user clicks Fix on a row whose issue another edit already removed — and a fix
   applied from a stale row mutates something nobody asked about. Reject with a
   message naming the identity.
4. **One transaction.** `session.begin_edit({EditKind::EditProperty, label, group, false, Project|Runtime|Preview})`,
   mutate `transaction.project()` per §2.6-2.8, and:
   - primitive reported an error → `cancel()`, return the primitive's message
     **verbatim**;
   - nothing changed → `cancel()`, return `ok=true, changed=false`. A repair that
     finds nothing to repair must not record history, exactly as
     `run_weight_command` (`shell_weight_paint.cpp:1019-1023`) already decides;
   - otherwise `commit()`.
5. **One undo entry.** `commit()` produces exactly one. Every fix case asserts
   `undo_count()` advanced by **exactly one**, that `undo()` restores
   `serialize_project()` **byte-identical**, and that `redo()` reproduces the fix.

Every rejection in steps 1-3 happens **before** `begin_edit`, so
`serialize_project()`, `dirty()`, `undo_count()`, `redo_count()` and all three
revisions are untouched. §5.1 records honestly that this ordering is a **design
choice and not a tested property**: measured at `c523977`, `begin_edit` captures
snapshots without touching the redo stack and `cancel()` restores exactly, so
opening the transaction earlier is invisible through the session's entire public
surface. The plan's Task 0 gate M6 re-measures it, and if a future `begin_edit`
ever clears the redo stack the inversion becomes available and must be added.

**MAR-187 exposes no way to apply a fix implicitly.** There is no "fix all", no
fix on open, no fix on save. `apply_safe_fix` is called from exactly one place —
a per-row button in `shell_problems.cpp` — and Task 9 greps for that being true.

### 2.6 Panels: three enum values, four destinations, one honest gap

MAR-187 adds **no** `DiagnosticPanel` value. MAR-186 emits three, and three is
what the router is total over:

| `DiagnosticPanel` | Window focused | Additional shell writes | AC2 destination served |
| --- | --- | --- | --- |
| `Timeline` | `kTimelineWindowTitle` | `selected_animation_name = navigation.animation_name`; `SelectionSet::replace` for a bone or slot target | **timeline** — and, through the selection, the **hierarchy** row |
| `Weights` | `kPropertiesWindowTitle` | `apply_shell_mode(ShellMode::WeightPaint)`; `SelectionSet::replace(AttachmentSelection)`; `viewport_ffd_selection = {scope, {vertex_index}}` when the issue carries one | **weight**, and **inspector** (the same window) |
| `Project` | `kProjectWindowTitle` | none — the preview reference has no selectable identity | **preview** |

AC2's fifth destination, a **hierarchy panel** focused in its own right, is never
requested, because no MAR-186 code is a hierarchy-scoped problem: the bone- and
slot-targeted issues are *timeline* overlays that happen to name a bone. What the
router does deliver for them is the selection, which is what makes the Hierarchy
row active (`shell_selection.cpp`, `draw_hierarchy_window` reads
`state.selection`). §10 records the residue rather than manufacturing a
`DiagnosticPanel::Hierarchy` that nothing emits — MAR-186 refused exactly that
for the same reason, and a dead enumerator would additionally let a future story
believe the sweep was already done.

The mapping is written as a **`switch`** over `DiagnosticPanel`, in
`shell_problems.cpp` and nowhere else, so a future added value is a compiler
error at exactly one site (§2.12).

### 2.7 `remove_orphan_overlay` removes **one record**, not one animation

The fix erases exactly the record the activated row names. It does **not** erase
every overlay that names the phantom animation.

Why, given that all seven would be orphans:

- **The row is the contract.** The view shows one row per issue; the button on
  that row repairs that row. Removing six records the user did not select is a
  surprise, and it makes "one undo entry" cover a mutation whose extent the user
  could not see.
- **It keeps the assertions exact.** X1 asserts the six *remaining identities by
  name*. A whole-animation erase reduces every fix case to "the list is empty",
  which is the count-shaped assertion `AGENTS.md` keeps recording as the way a
  case passes for the wrong reason.
- **The consequence is real and is asserted, not hidden.** Removing one of seven
  leaves `ghost` still resurrected by the other six, so nothing user-visible
  improves until the last one goes — and a `preview.stale_animation` appears only
  when the **last** overlay is removed, if `active_animation` named it. That is
  the mirror of MAR-186's own §2.7 interaction, and X2 asserts it.

Implementation: a `switch` over `DiagnosticOverlayFamily` (§0.2.1) picking one of
seven `std::vector<…TimelineEdit>` members of `ProjectData`
(`include/marrow/editor/project.hpp:591-600`), plus a separate arm for
`DiagnosticCode::OverlayOrphanWeightTarget` erasing from
`mesh_weight_attachment_edits` (`:596`). Erase by matching the record's own
key fields, not by index.

**MAR-187 does not reimplement `erase_all_timeline_edits`' seven-vector list**
and does not make it public. That list (`authoring.cpp:136-147`) is a
hand-maintained call list of exactly the kind `AGENTS.md:321-326` records as
compiler-blind — *"three of MAR-185's biting inversions lived there"* — and a
second copy of it in `safe_fix.cpp` would be a second thing to forget. A
`switch` over a typed family is the shape the compiler **does** police, which is
the whole reason §0.2.1 asks MAR-186 for the enum.

### 2.8 `reset_preview_reference` writes what the runtime already substituted

Two arms, selected by `DiagnosticCode`:

- **`PreviewStaleAnimation`** → `editor_metadata.active_animation =
  session.preview_state().animation_name`. That value is the output of
  `normalize_state` (§1.5): it is either an animation the skeleton has, or empty
  when the skeleton has none. Writing it makes the stored data agree with what
  the editor has been showing all along, so the repair changes **data** and not
  **behaviour**. Clearing the field instead would be a behaviour change (setup
  pose instead of the substituted animation) and is the inversion I12 exists for.
- **`PreviewStaleSkin`** → erase from `editor_metadata.preview_skins` every entry
  equal to the issue's named skin, and nothing else. MAR-186 emits one issue per
  **distinct** unresolvable name, so erasing all copies of that one name is
  exactly the issue's extent. Rebuilding the vector from
  `preview_state().skin_names` would additionally collapse a **resolvable**
  duplicate that no issue named (§1.5), and is the inversion I13 exists for.

`preview_skins` may legally end up empty (§1.5), so there is no fallback insert.

### 2.9 `normalize_weights` repairs one vertex, not one attachment

`normalize_mesh_weights(project, skeleton, attachment, target, {*issue.target.vertex_index})`.
The **scope vector is the point**: `normalize_weights_command`
(`shell_weight_paint.cpp:1057-1069`) passes `weight_command_scope(state)`, which
is empty — meaning *every vertex* — whenever the FFD selection does not narrow
it. Wiring the Problems row to the shipped command would canonicalize the whole
attachment and silently repair vertices the user never saw a row for. I25.

Building the `MeshWeightTarget` is where the story's oldest trap bites a second
time. Measured at `c523977`:

```cpp
struct AttachmentSelection { std::string slot_name; std::string skin_name; std::string attachment_name; };   // selection.hpp:49-52
struct MeshWeightTarget    { std::string skin_name; std::string slot_name; std::string attachment_name; };   // authoring.hpp:690-694
```

**Transposed.** Aggregate-initializing one from the other in field order compiles,
runs, and produces a target naming a skin called `body` — and every assertion
about codes, identities, severities and counts still passes. MAR-186's G6 guards
the *selection*; MAR-187's I7 guards the *conversion*, and X4 reads the fields
**by name**.

### 2.10 Inspection never dirties — how each half is proved

AC5's first clause and AC6's last. The two halves need different evidence, and
`AGENTS.md`'s **H4** forbids an assertion that reads state a passing run leaves at
its default.

| Half | How it is proved |
| --- | --- |
| Building the view cannot mutate | **Structurally**: `build_problems_view`, `plan_issue_navigation` and `find_issue_by_identity` take `const&` and return values. Plus a **measured** case (V0): snapshot `serialize_project`, `dirty()`, `undo_count()`, `redo_count()` and all three revisions around a collect + build + plan, and assert all seven identical. Those values **can** move — I23 makes them move — so the assertion is not vacuous |
| No automatic fix on project open | **X9**: open the issue-carrying project in a *fresh* `EditorSession`, assert `dirty() == false`, `undo_count() == 0`, and that the collected identity list is **the same one the project was written with**. A repaired-on-open project would have a shorter list and a clean dirty flag, so the identity list is the load-bearing half and the flags alone would not be enough |
| Rejections leave nothing behind | **X8**: three rejection arms, each asserted **on its message** (never on `!result`), each with `serialize_project()` byte-identical and `undo_count()`/`redo_count()` unchanged |
| The shell does not fix on its own | **Task 9 grep**: `apply_safe_fix` appears in `shell_problems.cpp` exactly once, and in no other shell file. A scope check, recorded as one, not counted as coverage |

**Assert the message, not `!result`.** This trap has bitten four times in this
arc — most recently MAR-184's P2b, where a load still failed but with the
runtime's wording at a different JSON path, so `!result` passed for the wrong
reason. Every rejection assertion in §6 names the expected substring. Its C++
mirror is live here too: a `NaN` weight compares `false` against every bound, so
a sign check slides it through; MAR-186's canonicalizer rejects non-finite
weights and MAR-187 never re-checks one, but the rule stands for any predicate
this story adds.

### 2.11 The window, and where it is drawn

`constexpr char kProblemsWindowTitle[] = "Problems";` joins the twelve constants
at `shell_state.hpp:989-1000`, making thirteen. `draw_problems_window(ShellState*)`
lives in `shell_problems.cpp` and is called from **both** frame bodies (§1.2):
`shell_main.cpp`'s `render_shell_frame` and `shell_smoke_frames.cpp`'s
`render_headless_smoke_frames`. `ensure_default_dock_layout`
(`shell_main.cpp:443-513`) docks it into `dock_bottom_id` beside the Timeline, so
it is a **tab** — which means it renders **no rows at all** until it is focused.
That is not incidental: `shell_smoke_frames.cpp:1605-1607` already records the
same property for the Hierarchy, and F1 must `SetWindowFocus(kProblemsWindowTitle)`
on its opening frames before sweeping. `kDockLayoutVersion` is not bumped (§1.2).

The window is always drawn — no `show_problems_panel` toggle. A toggle would add
a dock-layout rebuild path (`shell_main.cpp:594-598`'s agent-panel dance) and a
persisted preference, neither of which any acceptance criterion asks for.

`ShellState` gains four fields, grouped in one struct so the diff is one block:

```cpp
struct ProblemsPanelState {
    std::optional<marrow::editor::DiagnosticReport> report;
    marrow::editor::ProblemsView view;
    marrow::editor::ProblemsSeverityFilter filter{ProblemsSeverityFilter::All};
    std::string selected_identity;      // "" = no row selected; survives a refresh by identity
};
```

### 2.12 What the compiler polices, and the two lists it does not

**`-Wswitch` is on by default** (§0.3 E1; `AGENTS.md:305-334`). MAR-187 therefore
writes **every** dispatch as a `switch`:

| Dispatch | Enum | Site |
| --- | --- | --- |
| filter → predicate | `ProblemsSeverityFilter` | `problems_model.cpp` |
| issue → navigation | `DiagnosticCode` | `problems_model.cpp` |
| issue → fix arm | `SafeFixKind`, `DiagnosticCode`, `DiagnosticOverlayFamily` | `safe_fix.cpp` |
| panel → window + mode | `DiagnosticPanel` | `shell_problems.cpp` |

One file per enum family, so a future added value produces a warning list that
**is** the checklist, and it is complete for switches. Adding
`-Werror=switch` on these three files is recorded in MAR-186 §2.12 as a deferred
option and stays deferred; MAR-187 does not become the second `-Werror` in a build
that has deliberately kept exactly one (`CMakeLists.txt:352`, scoped to
`marrow_constraint_warning_check`).

**Two lists are genuinely blind and get their own guards:**

1. **`safe_fix_kind_for`'s string table.** Three `std::string_view` comparisons.
   Nothing warns if a fourth is added or one is misspelled. Guard: X7's corpus
   sweep — the three ids, the empty string, four near-miss spellings, and **all
   66 shipped agent operation names** — asserting the accepted set is exactly
   three, reported as a sorted set difference.
2. **The two frame-body call lists** (§1.2). Nothing warns if
   `draw_problems_window` is added to one and not the other.

   **This paragraph originally claimed F1 backs the grep up. It does not, on
   either half.** Measured by deleting each call in turn: the shell smoke passes
   green with the *application's* call missing AND with the *smoke's shared draw
   list* call missing, F1 included both times — because F1 renders through a
   scenario-local lambda of its own and so exercises neither body. A plain
   two-file `grep` is worse than merely unsupported: F1's lambda keeps the symbol
   present in `shell_smoke_frames.cpp`, so the grep returns hits from both files
   and **passes on a broken tree**.

   Guard: `cmake/CheckFrameBodies.cmake`, run **POST_BUILD** on
   `marrow_editor_shell` and available as `marrow_frame_body_check`. It compares
   the two draw lists as **sets** (so it guards every window, not just this
   story's), bounds each region by its own delimiters so a scenario lambda cannot
   satisfy it, refuses to pass vacuously if either region yields no calls, and
   fails the build naming the window and which direction it is missing.

### 2.13 What does not move

- **No agent operation, no MCP tool.** Fixes are an editor gesture. Exposing one
  would move the registry to 67 across fifteen hand-edited sites for a capability
  no acceptance criterion names. `docs/root1/agent-control.md` gains nothing.
- **No format change.** `.marrow` gains no key — a Problems view is derived state
  and the filter is transient. `.mskl` stays 1, `.mbin` stays 2, the C ABI is
  untouched.
- **No new CTest target.** Two new sources into `marrow_editor`, two into
  `marrow_editor_shell`. `ctest -N` must equal Task 0's number at Task 10.
- **No new `DiagnosticPanel`, `DiagnosticSeverity` or issue code.**
- **No fixture edit.** `marrow_project_smoke` **aborts** on a partial marker
  match (`editor_project_smoke.cpp:16144-16153`), so touching
  `assets/fixtures/player_idle.marrow` silently disables roughly two dozen suites
  and still exits 0.
- **`kEditorSettingsVersion`, `kDockLayoutVersion`** — unchanged (§1.2).

---

## 3. Where each piece lands

| File | Change |
| --- | --- |
| `include/marrow/editor/problems_model.hpp` | **New.** `ProblemsSeverityFilter`, `ProblemsGroup`, `ProblemsView`, `ProblemsNavigation`, four function declarations. Includes `<cstddef> <cstdint> <optional> <string> <string_view> <vector>`, `marrow/editor/diagnostics.hpp`, `marrow/editor/selection.hpp`, and forward-declares `runtime::SkeletonData` |
| `src/editor/problems_model.cpp` | **New.** The four functions. Every switch over `ProblemsSeverityFilter` and `DiagnosticCode` lives here |
| `include/marrow/editor/safe_fix.hpp` | **New.** `SafeFixKind`, `SafeFixResult`, `safe_fix_kind_for`, `apply_safe_fix`. Forward-declares `EditorSession` |
| `src/editor/safe_fix.cpp` | **New.** The applier. Includes `marrow/editor/session.hpp` and `marrow/editor/authoring.hpp` |
| `src/editor/shell_problems.hpp` / `.cpp` | **New.** `draw_problems_window(ShellState*)`, `refresh_problems_if_revised(ShellState*)`, `activate_problem_row(ShellState*, const DiagnosticIssue&)`, `focus_window_for_panel(DiagnosticPanel)`. The only `switch` over `DiagnosticPanel` |
| `src/editor/shell_state.hpp` | `kProblemsWindowTitle` beside the twelve at `:989-1000`; `ProblemsPanelState` and one `ProblemsPanelState problems;` member |
| `src/editor/shell_main.cpp` | One `DockBuilderDockWindow` line in `ensure_default_dock_layout` (`:498` neighbourhood); one `draw_problems_window` call in `render_shell_frame` (`:583` neighbourhood) |
| `src/editor/shell_smoke_frames.cpp` | One `draw_problems_window` call in `render_headless_smoke_frames` (`:76` neighbourhood); F1 |
| `src/editor/shell_smoke_project.cpp` | `validate_mar187_problems_shell_smoke(...)`, S1-S6 |
| `src/editor/shell_smoke_scenarios.hpp` | One declaration |
| `src/editor/shell_smoke.cpp` | One rail entry, on the **main** rail, after `validate_constraint_parameter_shell_smoke` (`:140-143`) and before the `&&` chain |
| `src/samples/editor_project_smoke.cpp` | `validate_mar187_problems_view(...)` and `validate_mar187_safe_fixes(...)`, registered after MAR-186's suite inside the marker-gated `else` (after `:16282`'s neighbourhood) |
| `CMakeLists.txt` | Two lines in `add_library(marrow_editor …)` (`:499-519`), two in `add_executable(marrow_editor_shell …)` (`:876-914`). **No new target** |
| `AGENTS.md`, PRD | §11 of the plan |

---

## 4. Acceptance criteria → artifacts

| AC | Artifact | Proof |
| --- | --- | --- |
| **AC1** — groups and filters by severity, displays collector counts, without changing project state | `build_problems_view` (§2.2) | V1 (group order + full identity list per group), V2 (empty group omitted), V3 (**counts are the collector's under every filter**), V4 (each filter's exact identity list), V0 (zero-mutation snapshot) |
| **AC2** — activating an issue selects its typed target and focuses the appropriate panel **when the target still exists** | `plan_issue_navigation` (§2.4), the panel router (§2.6) | V7 (panel, selection fields **by name**, animation, vertex — for every code), S1 (timeline + bone), S2 (weights + mode + FFD vertex), S3 (project, no selection) |
| **AC3** — refreshes from project **and runtime** revisions; handles removed targets without stale selection or crashes | `problems_view_needs_refresh` (§2.3); `target_missing` (§2.4) | V8 (both arms, including runtime-only via `adopt_runtime_sources`), V5 (row identity survives a re-sort), V6 (`target_missing`, no selection), S4 (the shell leaves the previous selection alone) |
| **AC4** — the only initial fixes are the three | `safe_fix_kind_for` (§2.5), `SafeFixKind` | **X7**: the accepted set over a 74-string corpus is exactly three, reported as a sorted set difference. X1/X3 (orphan overlay), X4 (weights), X5/X6 (preview) |
| **AC5** — inspection never dirties; an explicitly invoked fix uses one validated transaction and one undo entry; no automatic fix on open | Preflight-then-mutate (§2.5); §2.10 | V0, X1-X6 (`undo_count()` **+1 exactly**, `undo()` byte-identical, `redo()` reproduces), X8 (three rejection arms on their messages, byte-identity), **X9** (fresh open: not dirty, `undo_count()==0`, identity list unchanged), S6, Task 9's single-call-site grep |
| **AC6** — shell and project tests cover grouping, filters, navigation, revision refresh, all three fixes, rejection, undo/redo, and inspection invariants | Three surfaces (§6) | grouping/filters → V1-V4; navigation → V6, V7, S1-S4; revision refresh → V8, S5; three fixes → X1-X6; rejection → X8; undo/redo → X1-X6; inspection invariants → V0, X9, S6; and F1 as the only case that can see the widget |

---

## 5. Inversions, and the mechanism that makes each observable

The register with predicted failure texts, per-case attribution **by run order**,
and the uniqueness demonstrations lives in the plan's §B. What belongs here is the
**mechanism** — why each mutation is invisible to everything except the case
named, because that is a claim about the code, not about the tests.

| Mutation | Why nothing else notices |
| --- | --- |
| Recompute the view's counts from the visible rows | Identical under `All`, which is the filter every other case uses. Only a case that reads `warning_count` under `ErrorsOnly` fails |
| Emit groups in first-encountered order | The report is identity-sorted, and most fixtures happen to put an Error first. Only a fixture whose lowest identity is a **Warning** (`overlay.orphan_weight_target` before `weights.uncanonicalizable`) can tell the two orders apart |
| Emit an empty group instead of omitting it | Needs a filter that empties a group. Every mixed-severity fixture keeps both groups non-empty |
| Derive the row identity from the row index | Stable within a build and across two builds of an unchanged project. Only an insertion **before** the remembered row moves it |
| Always carry the selection, skipping `selection_item_exists` | Every issue except `overlay.orphan_weight_target` has a resolving target, so the check is a no-op everywhere else |
| Build `MeshWeightTarget` from `AttachmentSelection` in field order | Compiles and runs; codes, identities, severities, panels and counts stay right. Only the *fix* consumes the fields in the other struct's order, and only a case reading them **by name** — or the primitive's own rejection — sees it |
| Skip the freshness re-collection | Needs a **stale** issue. Every other case passes an issue collected from the current revision |
| Commit on the "nothing changed" path | Needs a repair with nothing to repair. Every other fix case changes something |
| Erase every overlay naming the animation | Needs more than one record for one animation. Only the seven-family fixture distinguishes one from seven |
| Key the refresh on `project_revision` alone | Every ordinary edit moves both revisions together. Only `adopt_runtime_sources()` moves runtime without project |
| Accept any non-empty `safe_fix_id` | The three real ids still work. Only a corpus sweep of ids that must be **rejected** fails |
| Rebuild `preview_skins` from `preview_state().skin_names` | Correct whenever no resolvable duplicate exists — which is every fixture but one |
| Clear `active_animation` instead of writing the substituted value | The issue disappears either way and the project is no longer stale. Only a case asserting the **resulting value** fails |
| Wire the Fix button to `normalize_weights_command` | The clicked vertex **is** repaired. Only a second non-canonical vertex in the same attachment, which must survive, exposes the wider scope |
| Delete `draw_problems_window`'s body | Every UI-free case still passes — it calls the handler, not the button. `AGENTS.md` records the MAR-178 worked example verbatim. Only a real-mouse frame can see it |
| Add `draw_problems_window` to one frame body only | The smoke passes if the *application's* body is the missing one. Only a grep over both files sees that half |

### 5.1 Deliberately uninverted, by name

- **Opening the transaction before the preflight.** Measured at `c523977`:
  `begin_edit` (`session.cpp`) captures a history snapshot, a preview snapshot and
  the skeleton pointer, and **does not touch the redo stack**; `cancel()` calls
  `restore_active_transaction()` and restores exactly. `serialize_project()`,
  `dirty()`, `undo_count()`, `redo_count()` and all three revisions are identical
  whether the transaction was opened and cancelled or never opened at all. The
  preflight ordering in §2.5 is therefore a **design choice, recorded, and not a
  tested property**. Task 0's gate M6 re-measures it; if `begin_edit` ever clears
  the redo stack, the inversion becomes available and must be added.
- **`problems_view_needs_refresh` returning `true` unconditionally** is
  behaviourally **correct** — it merely re-collects every frame. V8's negative arm
  asserts quiescence, not correctness, and is labelled as such.
- **`DiagnosticPanel::Hierarchy` / `Inspector`.** MAR-187 adds no enumerator, so
  there is nothing to invert. §10 records the gap.
- **Omitting an arm from any `switch` over `SafeFixKind`,
  `DiagnosticOverlayFamily`, `DiagnosticCode`, `ProblemsSeverityFilter` or
  `DiagnosticPanel`** is a **compile-time `-Wswitch` warning**, not a test
  failure. Recorded as compiler-covered, with the measured warning text pasted
  into `AGENTS.md`, rather than dressed up as an inversion. What still needs an
  inversion is an arm that does the **wrong** thing (I11).
- **V0's byte-identity half** — an unchanged project must serialize to unchanged
  bytes. No story-owned mutation makes it differ without failing something louder
  first. V0's other halves are inverted (I23).
- **MAR-186's own guarantees** — sorting, de-duplication, identity escaping,
  severity assignment. MAR-187 asserts them where it depends on them (V1's
  strictly-increasing check) but writes **no inversion for code it did not
  write**.

---

## 6. Test surfaces

### 6.1 Which binaries, and why no fourth

| Binary | Runs | Why |
| --- | --- | --- |
| `marrow_project_smoke` | V0-V8, X1-X9 | It links `marrow_editor`, constructs real `EditorSession`s (80 mentions at `c523977`, e.g. `:1270`, `:11296`) and already drives `adopt_runtime_sources` (`:13722`). **Inside the standing `player_idle.marrow` invocation** — §1.7 |
| `marrow_editor_shell` (headless smoke) | S1-S6, F1 | AC6's "shell tests". The only place `ShellState`, `apply_shell_mode` and an ImGui context exist together |
| — | — | **No new binary, no new CTest target.** `ctest -N` must be unchanged |

**No agent or MCP case**, because MAR-187 adds no operation and no tool (§2.13),
and AC6 names only shell and project tests.

### 6.2 Project cases — `validate_mar187_problems_view`

In execution order; the order is load-bearing because the plan's §B attributes
each mutation to the **first** case that catches it.

| # | Case |
| --- | --- |
| **V0** | **Zero-issue witness and zero-mutation.** Over `player_idle.marrow` (which MAR-186 §1.8 establishes is issue-free): `collect_session_diagnostics` returns a report with no issues; `build_problems_view(report, All)` has **zero** groups, `visible_count == 0`, `error_count == 0`, `warning_count == 0`. Around a collect + build + `plan_issue_navigation` over every issue, snapshot `serialize_project`, `dirty()`, `undo_count()`, `redo_count()`, `project_revision()`, `runtime_revision()`, `preview_revision()`; assert all seven identical and the bytes byte-identical. Print the byte length and SHA-256 every run |
| **V1** | **Grouping.** A fixture carrying **one** `overlay.orphan_weight_target` (Warning), **one** `weights.uncanonicalizable` (Error) and **two** `overlay.orphan_animation` (Error): exactly **two** groups; `groups[0].severity == Error`; each group's **full identity list** asserted by name; `identity[i] < identity[i+1]` strictly across the whole report. A comment states why the fixture's *lowest* identity is a Warning and that this is what makes group order observable |
| **V2** | **Empty groups are omitted.** An errors-only fixture under `WarningsOnly` → `groups.size() == 0` and `visible_count == 0`, **not** one empty Warning group; under `ErrorsOnly` → one group with the exact identity list |
| **V3** | **Counts are the collector's.** V1's fixture under all three filters: `error_count` and `warning_count` are **identical in all three**, and equal to the report's; only `visible_count` moves. Asserted as three triples, printed |
| **V4** | **Filter arms.** `ErrorsOnly` yields exactly the Error identities, `WarningsOnly` exactly the Warning identities, `All` their concatenation in report order. Full lists, reported as set differences |
| **V5** | **Row identity survives a re-sort.** Remember `overlay.orphan_animation\|transform\|ghost\|arm_l\|rotate`'s row index; add an unrelated orphan `deform` overlay that sorts **before** it; re-collect and re-build; assert `find_issue_by_identity` resolves the remembered identity to a **different index**, and that the identity string is byte-identical |
| **V6** | **Removed targets.** For the `overlay.orphan_weight_target` issue: `target_missing == true`, `selection` is **absent**, `missing_description` names the skin, slot and attachment. For every other issue in V1's fixture: `target_missing == false` and `selection` present. Asserted by identity, never by count |
| **V7** | **Typed navigation, field by field.** One fixture carrying an issue of each MAR-186 code that has a target. For each: `panel`; the selection's `bone_name` / `slot_name` / the three `AttachmentSelection` fields **by name**; `animation_name`; `vertex_index`. Then feed each present selection to `SelectionSet::replace` and assert the expected typed identity is active |
| **V8** | **Refresh keying, both arms.** (a) Unchanged session → `problems_view_needs_refresh` is **false** (the quiescence arm; §5.1 records why it needs asserting). (b) One project edit → **true**, and `project_revision` on the new view advanced. (c) **Runtime-only**: copy the fixture's `.mskl` and `.marrow` to a scratch directory, rewrite the skeleton to drop a skin, `adopt_runtime_sources()`, assert `runtime_revision` **advanced** while `project_revision` **did not**, and that the refresh is still reported as needed and the new report carries a `preview.stale_skin` issue the old one did not |

### 6.3 Project cases — `validate_mar187_safe_fixes`

| # | Case |
| --- | --- |
| **X1** | **One record, not seven.** A project with all seven orphan families naming `ghost`. Apply `remove_orphan_overlay` to the `inherit` issue. Assert: `ok`, `changed`, `applied_identity` equal to that issue's; the remaining **six identities by name**; `undo_count()` advanced by **exactly one**; `undo()` restores `serialize_project()` **byte-identical** to the pre-fix bytes; `redo()` reproduces the six-issue list |
| **X2** | **The last one, and the documented interaction.** Remove the remaining six one at a time (six fixes, six undo entries). After the last: `save_project` → **`load_project(path)`**; assert `ghost` is **absent** from the materialized skeleton, and that with `active_animation == "ghost"` a `preview.stale_animation` issue now exists that did not before. Asserted, not merely noted |
| **X3** | **The orphan weight target.** `remove_orphan_overlay` on an `overlay.orphan_weight_target`: exactly that `MeshWeightAttachmentEdit` erased, a second, resolvable weight edit untouched (asserted by identity **and** by the surviving edit's own fields), one undo entry, `undo()` byte-identical |
| **X4** | **One vertex, not one attachment — and the transposition.** An attachment whose vertices 0 **and** 12 are non-canonical. `normalize_weights` on vertex 0: assert exactly one issue remains, `weights.non_canonical\|mesh_base\|body\|body_mesh\|12`, one undo entry, byte-identical undo. **Control arm:** re-apply the same fix to a vertex that is already canonical → `ok == true`, `changed == false`, `undo_count()` **unchanged**, `dirty()` unchanged, bytes byte-identical |
| **X5** | **Preview animation reset.** `active_animation = "ghost"` with no overlay resurrecting it. Assert the session's live `preview_state().animation_name` first (it is the substituted value), then apply the fix and assert `editor_metadata.active_animation` **equals that value**, the issue is gone, one undo entry, and the value survives `save_project` → `load_project(path)` |
| **X6** | **Preview skin reset, and the duplicate that must survive.** `preview_skins = ["default", "ghost_skin", "default", "ghost_skin"]`. Apply the fix to the single `preview.stale_skin\|ghost_skin` issue. Assert `preview_skins == ["default", "default"]` — **both** copies of the stale name gone, **both** copies of the resolvable one kept — one undo entry, byte-identical undo |
| **X7** | **AC4's allowlist gate.** Build a corpus: the three allowlisted ids, `""`, four near misses (`normalise_weights`, `remove_orphan_overlays`, `reset_preview`, `rebind_weights`), and **all** names from `agent_operation_descriptors()` / `agent_operation_descriptor_count()`. Assert the set for which `safe_fix_kind_for` returns a value is **exactly** the three, reported as a sorted set difference naming every unexpected acceptance and every missing one; and that `is_allowlisted_safe_fix` agrees on **every** corpus entry. **Asserts no number about the registry** (§1.8) |
| **X8** | **Rejections, on their messages.** Three arms, each asserting the returned `error` **contains the named substring** and that `serialize_project()`, `undo_count()` and `redo_count()` are all unchanged: (a) a non-allowlisted id; (b) a session with no project; (c) a **stale** issue — collect, remove the offending overlay by another route, then apply the stale issue and assert the freshness rejection names the identity |
| **X9** | **No automatic fix on open, and inspection is idempotent.** Write the issue-carrying project to disk, then open it in a **fresh** `EditorSession`. Assert `dirty() == false`, `undo_count() == 0`, and the collected **identity list equals the one the project was written with** — a repaired-on-open project would report fewer. Then collect and build the view three times; assert the identity lists are equal member-by-member and `serialize_project()` byte-identical each time |

Every case additionally runs two invariants: **every non-empty `safe_fix_id` on
every issue is allowlisted**, and **identities are strictly increasing**.

### 6.4 Shell cases — `validate_mar187_problems_shell_smoke`

UI-free with respect to widgets; they prove the router and the wiring, not that
anything is drawn. Each carries that statement in a comment.

| # | Case |
| --- | --- |
| **S1** | **Timeline activation.** Activate a bone-targeted `overlay.orphan_animation` row: `state.selection.active_bone()->bone_name` is the bone, `state.selected_animation_name` is the orphan animation, and the recorded focus request is `kTimelineWindowTitle` |
| **S2** | **Weight activation.** Activate a `weights.non_canonical` row: the active `AttachmentSelection`'s three fields **by name**, `state.viewport_ffd_selection->vertex_indices == {7}` with a matching `scope.slot_index`, `current_shell_mode(&state) == ShellMode::WeightPaint`, focus request `kPropertiesWindowTitle` |
| **S3** | **Preview activation.** Activate a `preview.stale_skin` row: focus request `kProjectWindowTitle`, and the selection set **unchanged** (there is no identity to select) |
| **S4** | **A removed target changes nothing it should not.** Seed `SelectionSet` with `BoneSelection{spine}`, activate the `overlay.orphan_weight_target` row: the selection is **still** `BoneSelection{spine}`, `state.status_message` names the missing skin/slot/attachment, focus request is still `kPropertiesWindowTitle` |
| **S5** | **A fix through the shell.** `apply_safe_fix` via the shell path: exactly one entry appears in the shell's history, `sync_shell_from_editor_session` ran (`observed_project_revision` caught up), `refresh_problems_if_revised` produced a **smaller identity list** asserted by name, and the previously selected row's identity — now gone — leaves `problems.selected_identity` cleared rather than pointing at a different row |
| **S6** | **Inspection invariants in the shell.** Over ten `refresh_problems_if_revised` calls with no edit in between: `session.dirty()` stays false, `serialize_project()` byte-identical, `undo_count()` unchanged, and the collector ran **once** (a counter, or the cached report's identity is pointer-stable) |

### 6.5 The frame case — `shell_smoke_frames.cpp`

**F1.** The only case that can observe the widget. Shape, following MAR-185's F1
(`:1643-1902`) and `AGENTS.md`'s Headless Frame Smoke Notes:

1. Seed a project state carrying at least one Error and one Warning row.
2. Render two frames with `SetWindowFocus(kProblemsWindowTitle)` **confined to
   the opening frames** — forcing focus later closes any open popup — because the
   window is a **dock tab beside the Timeline** and renders no rows until focused
   (§2.11).
3. Locate the severity filter control by sweeping the mouse and comparing
   `ImGuiContext::HoveredId` against the window's id for the label. **Reproduce
   the full ID seed chain**: if the rows are drawn inside a `BeginTable` or a
   `BeginChild`, `window->GetID(label)` is the wrong seed and the sweep reports
   the widget "absent" — MAR-185 lost time to exactly this with
   `BeginTabItem("Dopesheet")`. Sweep at least one control that already exists so
   a broken seed cannot be mistaken for a missing widget.
4. Click a row; assert the selection changed and `problems.selected_identity` is
   the row's identity.
5. Sweep for that row's **Fix** button — **its own sweep column**, because a
   button on a `SameLine()` after the row label is missed entirely by a column at
   the row's left edge — click it, and assert `undo_count()` advanced by exactly
   one and the row is gone.
6. Advance `io.DeltaTime` past `io.MouseDoubleClickTime` between the two clicks;
   two presses at the same pixel are a double click.
7. `io.ConfigMacOSXBehaviors` is left alone — F1 needs no modifier. If a later
   revision adds one, clear it for the gesture and restore it, and assert
   `ImGuiContext::TempInputId`.

Print the located coordinates every run, as MAR-185's F1 does.

---

## 7. Verification commands

```bash
cmake -S . -B build && cmake --build build

./build/marrow_project_smoke assets/fixtures/player_idle.marrow      # V0-V8, X1-X9
./build/marrow_project_smoke --create /tmp/mar187_created.marrow
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
  --export-runtime /tmp/player_idle_project_export.mskl \
  --export-binary  /tmp/player_idle_project_export.mbin
./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin \
                                 /tmp/player_idle_project_export.mskl

MARROW_CONFIG_HOME=/tmp/mar187-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2      # S1-S6, F1

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
./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
./build/marrow_agent_dispatch_smoke                                  # regression witness

ctest --test-dir build -N
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -L runtime
ctest --test-dir build --output-on-failure -L editor
ctest --test-dir build --output-on-failure -R marrow.renderer_link_boundary
cmake --build build --target marrow_verify_third_party
cmake --build build --target marrow_constraint_warning_check

./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py                   # regression witness
python3 -m py_compile tools/mcp/server.py tools/mcp/test_client.py \
                      tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
```

Never run `./build/marrow_editor_shell` without `--auto-close`, and
`~/Library/Application Support/Marrow` must be **ABSENT** after the whole run.

---

## 8. Non-goals

- **No new diagnostic.** No issue code, no severity, no `DiagnosticPanel` value.
- **No fix-all, no fix-on-open, no fix-on-save.** AC5 forbids the second; the
  other two are simply not asked for, and each would multiply the undo story.
- **No agent operation, no MCP tool, no registry movement** (§2.13).
- **No format change**, no `.marrow` key, `.mskl` 1, `.mbin` 2, C ABI untouched.
- **No new CTest target and no new test binary.**
- **No repair for `weights.uncanonicalizable`.** MAR-186 gives it no
  `safe_fix_id`, deliberately; MAR-187 does not invent one. The Problems view
  shows it with no Fix button, which is what an absent fix id must look like.
- **No fixture edit.**
- **No `-Werror=switch`.** Recorded as deferred by MAR-186 §2.12 and left there.
- **No persistence of the filter or the selected row.** Transient view state.

---

## 9. Facts to MEASURE in Task 0, not assume

| # | Fact | How |
| --- | --- | --- |
| **M1** | **Every `file:line` in this document, re-resolved against the tip.** MAR-186 will have moved most of them | The plan's Task 0.1 gate. Symbol first, line as a hint. A citation that no longer names its described construct is a **blocking finding** |
| **M2** | MAR-186's landing state, and whether D1-D6 (§0.2) are all present | `grep -n "collect_session_diagnostics\|DiagnosticReport\|DiagnosticCode\|DiagnosticOverlayFamily" include/marrow/editor/diagnostics.hpp`. **A missing D6 is a blocking finding**, resolved by Task 1 amending MAR-186's header and recording it |
| **M3** | The registry count and its fourteen witnesses, whatever MAR-186 left them at | `grep -c '^\s*{\s*"' src/editor/agent_dispatch.cpp`; the array bound; `grep -rn "!= 6[0-9]U" src/`; `grep -nE "== 6[0-9]" tools/mcp/test_client.py`. **Non-effect baseline** |
| **M4** | `ctest -N` total | **22** at `c523977`; record whatever it is. Must be identical at the end |
| **M5** | **`-Wswitch` is on with no flag**, and what it produces here | Add a throwaway enumerator to one of MAR-187's new enums, **delete the object files**, build, count the warnings, remove it. **Expected: non-zero.** Design §2.12 depends on it, and MAR-186's design asserts the opposite (§0.3 E1) |
| **M6** | **Does `begin_edit` + `cancel` leave the session bit-identical?** | Snapshot `serialize_project`, `dirty()`, `undo_count()`, `redo_count()` and all three revisions; `begin_edit`; `cancel()`; compare. **Expected: identical.** If any moves, §5.1's uninverted entry becomes an inversion and must be added |
| **M7** | **Does `adopt_runtime_sources()` bump `runtime_revision` without moving `project_revision`?** | Run it and print both before and after. **V8(c) depends on it.** If it moves `project_revision`, V8(c) needs another driver and the design records it |
| **M8** | The panel inventory: twelve window titles, **no** Weight window, **no** Preview window | `grep -n "WindowTitle\[\]" src/editor/shell_state.hpp`. §0.3 E9 and §2.6 depend on it |
| **M9** | **Two frame bodies**, and their current line numbers | `grep -n "draw_inspector_window" src/editor/shell_main.cpp src/editor/shell_smoke_frames.cpp`. §1.2 |
| **M10** | `normalize_mesh_weights`' **actual** rejection text for a transposed target | Call it with skin and slot swapped and paste the message. I7's predicted text is a prediction until this runs |
| **M11** | `erase_all_timeline_edits` / `rename_all_timeline_edits` are still unreachable | `grep -n "^namespace {\|^} // namespace" src/editor/authoring.cpp` and confirm `:136` falls inside the first block. §2.7 |
| **M12** | An **empty** `preview_skins` vector passes `validate_project_for_save` | Construct one and call it. §1.5, §2.8 |
| **M13** | The parser's **four** hard rules (not three) | `grep -n "references unknown" src/runtime/skeleton_parse.cpp`. §0.3 E2 |
| **M14** | `player_idle.marrow` yields **zero** issues, and the fixture is untouched | V0 prints the count and the `serialize_project` SHA every run; Task 9 diffs `assets/fixtures/` |
| **M15** | `io.IniFilename == nullptr` in both the app and the smoke, so `kDockLayoutVersion` need not move | `grep -n "IniFilename" src/editor/shell_main.cpp src/editor/shell_smoke.cpp`. §1.2 |
| **M16** | The shell rail's parameter-mode early return is still at `shell_smoke.cpp:82-88` | Read it. MAR-187's suite must be on the main rail |

---

## 10. Known limitations, stated rather than hidden

- **AC2's five destinations are three panels.** There is no Weight window and no
  Preview window in this shell; weight authoring is the Properties window in
  `WeightPaint` mode and the preview reference lives in the Project window
  (§0.3 E9, §2.6). **A hierarchy panel is never focused in its own right**,
  because no MAR-186 code is a hierarchy-scoped problem; what bone- and
  slot-targeted issues deliver is the *selection*, which is what makes the
  Hierarchy row active. Adding `DiagnosticPanel::Hierarchy` would be a dead
  enumerator, and MAR-186 refused exactly that pattern for exactly this reason.
- **A fix repairs one record.** Seven orphan overlays on one phantom animation
  need seven activations, and the phantom animation survives — and keeps being
  exported — until the last one goes (§2.7, X2). Deliberate; the alternative
  makes one undo entry cover a mutation the user could not see.
- **`weights.uncanonicalizable` has no repair.** Reported, shown with no Fix
  button, and left. MAR-186 owns that decision.
- **Removing the last orphan overlay can create a stale-preview issue** (X2), and
  fixing a stale skin can empty `preview_skins` (§1.5). Both are legal states and
  both are asserted rather than avoided.
- **The preflight ordering is untested** (§5.1). Opening the transaction earlier
  is invisible through the session's public surface at `c523977`.
- **The refresh's quiescence half is a performance property**, not a correctness
  one (§2.3). An always-refresh implementation is correct and slower.
- **Nothing is persisted.** The filter and the selected row die with the session,
  and no diagnostic is ever written to `.marrow`. That is also what keeps AC5's
  "inspection never dirties" total.
- **F1 proves one row and one button.** Grouping, filtering and the other panels'
  focus behaviour are proved UI-free; if the filter combo is later removed, F1 is
  the only case that would notice, and only because step 3 sweeps for it.
- **Carried forward, unchanged by this story:** `shell_main.cpp`'s frame body is
  still reachable from no test (and MAR-187 adds one more line to it that only a
  grep guards); `commit_path_choice` still has zero end-to-end coverage; the
  runtime still accepts a negative first inherit key time and inert `curve` data
  on an inherit key; the empty-edit hazard is still fixed only for the inherit
  family; the build still has no `-Wall`/`-Wextra` and only the one `-Werror` at
  `CMakeLists.txt:352`.

---

## 11. Risks for the implementer

| # | Risk | Mitigation |
| --- | --- | --- |
| **R1** | **Starting before MAR-186 lands**, or assuming its symbols exist | §0.2's table and the plan's Task 0.3. Unlike MAR-186's own dependency, this one is **deep**: without `DiagnosticReport` there is nothing to view |
| **R2** | **D6 is missing** — no typed code or overlay family (§0.2.1) | Task 0.3 makes it a blocking finding; Task 1 amends MAR-186's header and records the amendment. **Never** recover the family by splitting the identity string |
| **R3** | **Every cited line has moved.** A whole story of drift | Task 0.1's re-anchor gate. Symbol first, line as a hint, non-resolution blocking |
| **R4** | **Adding the window to one frame body only** (§1.2) | Task 9's grep over both files, plus F1. The application's body is the half no test can see |
| **R5** | **`AttachmentSelection` → `MeshWeightTarget` transposition** (§2.9) | X4 reads the fields by name; M10 records the primitive's real rejection text; I7 |
| **R6** | **Asserting a count where an identity is available** | Every case asserts the **full sorted identity list** and reports mismatches as a **named set difference**. MAR-186's plan flags the same trap: resolving orphans against the materialized skeleton reports a clean project while every count-only test passes |
| **R7** | **Wiring the Fix button to `normalize_weights_command`** — the shipped command's empty scope means *every vertex* (§2.9) | X4's second non-canonical vertex must survive; I25 |
| **R8** | **A vacuous inspection case** (H4) | V0 snapshots seven values a mutation **can** move, and I23 proves it. No `SelectionSet`-around-a-const-function case is written |
| **R9** | **Asserting `!result` instead of the message** — four times in this arc | Every rejection in X8 names its substring. §2.10 |
| **R10** | **`if (false && …)` as a neutering idiom.** MAR-185's I9b passed for the wrong reason because `false &&` short-circuited away the very call whose side effect was the detector | Where §B requires a uniqueness demonstration by neutering, use `(void)expr;` — evaluate the call, discard the result — never `if (false && expr)` |
| **R11** | **An inversion result that lies (H1-H4)** | **Delete the object file** before every verification build; never `touch`. `cmp` over whole recorded strings. Final run from `rm -rf build` |
| **R12** | **Pointing `marrow_project_smoke` at a new fixture** takes the skip branch, runs nothing, exits 0 | Build throwaway projects **inside** the standing invocation (§1.7) |
| **R13** | **A passing `save()` proving nothing.** `validate_project_for_save` (`project.cpp:5825`) takes no base document and materializes nothing | Every round trip goes `save_project` → **`load_project(path)`** |
| **R14** | **Registering the shell suite behind the parameter-mode early return** (`shell_smoke.cpp:82-88`) | Register on the main rail; M16 re-measures the branch |
| **R15** | **Creating a twelfth `!= 66U` count site** in X7 (§1.8) | X7 reads `agent_operation_descriptors()` and asserts **no** number about the registry. Task 9's diff sweep catches a stray literal |
| **R16** | **Believing the compiler is silent about enums** (§0.3 E1) | M5 re-measures. Switches are policed; `safe_fix_kind_for`'s string table and the two frame-body call lists are not, and each has a named guard (§2.12) |
| **R17** | **Editing a fixture.** `marrow_project_smoke` **aborts** on a partial marker match | Task 9's `git diff --stat -- assets/fixtures/` must be empty |
| **R18** | **A case written later running earlier.** MAR-185's I3 had to be re-attributed from P6 to P1 for exactly this | §B attributes every inversion by **run order**, and the case tables in §6.2-6.4 are in execution order |
