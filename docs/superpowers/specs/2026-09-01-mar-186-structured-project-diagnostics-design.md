# MAR-186 Collect Structured Project Diagnostics — Design

Story: `MAR-186`, `.agents/tasks/prd-marrow-runtime.json`, six acceptance
criteria, `dependsOn: ["MAR-185"]`.
Plan: `docs/superpowers/plans/2026-09-01-mar-186-structured-project-diagnostics.md`
First written 2026-09-01 against tip `6986024`. **Corrected 2026-09-01 against tip
`5a56663`** (`feat: MAR-185 상속 타임라인 편집 패리티 완성`).

> **MAR-185 has now LANDED, and the baseline this document was drafted against no
> longer exists.** The first draft was written two stories ahead of the tree and
> said, correctly at the time, that MAR-185 was planned and not implemented. Both
> statements have since changed: MAR-185 is committed at **`5a56663`**, and
> MAR-184's commit is now **`8cc5c57`**, not `6986024` — the hash this document
> originally cited was rewritten. §0.2 records what MAR-185 actually delivered;
> §0.3 records every claim of the first draft that measurement overturned.
>
> **Every `file:line` here is still a hypothesis with a short shelf life.** The
> **symbol** is the anchor; the line is a hint. A citation that no longer names the
> construct described here is a **blocking finding**, not a line to silently
> follow. Two anchors in the first draft had already drifted by the time MAR-185
> landed (§0.3 E12) — which is the whole argument for the plan's Task 0.1 gate.

---

## 0. Where this story sits

### 0.1 The story in one sentence

Give the editor a **UI-free, read-only** function that walks an opened project
and returns a deterministic, stably-identified list of problems — each with a
code, a severity, a message, a typed navigation target, and, only where a repair
is actually safe, an allowlisted fix identifier — and surface that list through
`project.diagnostics` **without disturbing the four fields the operation already
returns**.

MAR-186 ships **no UI**. It draws nothing, focuses nothing, selects nothing, and
fixes nothing. MAR-187 builds the Problems view and the three fixes on top of it;
the whole point of the boundary is that MAR-187 has something to *wire* rather
than something to *untangle*.

### 0.2 MAR-185 is a hard dependency and **none of it exists yet**

MAR-186's `dependsOn` is `["MAR-185"]`. What MAR-186 actually consumes from it is
small — which is deliberate, because the dependency is a scheduling fact, not a
deep coupling:

| # | What MAR-185 had to land | Why MAR-186 needs it | **Measured at `5a56663`** |
| --- | --- | --- | --- |
| **M1** | `rename_all_timeline_edits` / `erase_all_timeline_edits` covering **seven** vectors (adding `bone_inherit_timeline_edits`) | Without the seventh line, a `delete_animation` leaves an inherit overlay behind, and MAR-186's orphan detector fires on a project the editor itself created. The detector is still *correct*; the fixture that proves it becomes ambiguous | **Landed** |
| **M2** | The registry moving 64 → 66 | MAR-186 adds **no** registry row, so its count sweep is a **non-effect** gate: whatever the number is at Task 0, it must be identical at the end | **Landed, and the witness count moved too.** 66 rows in `kOperationSpecs`; `std::array<OperationExpectation, 66>` at `agent_dispatch_smoke.cpp:39`; **eleven** `!= 66U` guards (MAR-185 added one, so the "ten" of the first draft is stale); two `== 66` in `tools/mcp/test_client.py:53,55` |
| **M3** | `TimelineKeyKind::Inherit` and its switch sweep | MAR-186 touches none of it. Listed so the implementer does **not** mistake MAR-185's sweep for MAR-186's | **Landed** — and it is the source of §1.6's corrected `-Wswitch` measurement |
| **M4** | `validate_mar185_inherit_editing` registered at the tail of the marker-gated `else` | MAR-186's suite registers **after** it | **Landed**, at `editor_project_smoke.cpp:16282`; the marker gate is now `:16134` |

**MAR-186 still introduces no dependency on MAR-185's *code*.** The dependency
was, and remains, an ordering constraint. It is now discharged.

### 0.3 Errors found in the incoming brief and documents

Every story in this arc has found errors in its governing documents (175 three,
176 seven, 177 six, 178 six, 179 six, 180 seven, 181 four plus a later fifth,
182 six, 183 six, 184 nine). This design was written by verifying every
structural claim against the source. **Eight** were wrong — and then a review
pass by `plan-mar187`, building on top of it, found **six more, five of them in
this document.** Those are E9-E14. **E9 is the most consequential and it is
inherited, not original**: the team lead's brief asserted the compiler was silent,
the first draft adopted it without a compile, and it would have propagated to a
third story.

| # | Source | Claim | Measured (E1-E8 at `6986024`; E9-E14 at `5a56663`) |
| --- | --- | --- | --- |
| **E1** | The MAR-186 briefing | "`build_project_runtime` is defined at `src/editor/project.cpp:8258`" | **Correct, and re-verified.** `grep -n "^ProjectRuntimeResult build_project_runtime" src/editor/project.cpp` → `8258`. Recorded as a *confirmation*, not an error, because the same brief correctly flags that `:7495` — repeated in several earlier briefs — is slot-color timeline-edit code |
| **E2** | The MAR-186 briefing | "If the agent operation registry changes, the count derives from `std::size(kOperationSpecs)` and self-updates, but these sites are hand-edited … **AC6 spans C++ and Python**" | **AC6 is not about the registry count.** AC6 reads *"Project, agent, and MCP tests cover deterministic results, severity counts, legacy summary compatibility, typed targets, revision changes, and zero-mutation inspection."* It is a **test-surface** requirement, satisfied by three suites (§6). MAR-186 adds **no operation**, so the count sites are a **non-effect** gate, not a substitution task. The brief's mechanism (`std::size(kOperationSpecs)` self-updates; the other thirteen do not) is correct and is preserved in §9 as the gate's rationale |
| **E3** | The MAR-186 briefing | The hand-edited count sites are "`agent_dispatch_smoke.cpp` array size, guards in `shell_smoke_graph.cpp` and `shell_smoke_timeline.cpp`, `tools/mcp/test_client.py`, and prose in `AGENTS.md` and `docs/root1/*.md`" | **Incomplete.** Two of the ten `!= 64U` guards are in **`shell_smoke_constraints.cpp:147,676`**, which the brief does not name. Full list measured: `shell_smoke_constraints.cpp:147,676`, `shell_smoke_timeline.cpp:3697`, `shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603` |
| **E4** | AC4, read naively | "The collector reports **orphan overlays**" reads as *any* overlay naming a missing entity | **Most of that class cannot exist in an opened session.** `skeleton_parse.cpp:5264-5271` makes `"animation references unknown bone '<name>'"` a **hard load error**; `:5367-5373` does the same for an unknown slot; `:1610-1617` does it for `"mesh weight references unknown bone"`. A project carrying any of those **cannot be opened at all**, which AC2 ("limited to normally opened sessions") explicitly excludes. §1.2 draws the boundary and §1.3 names the two orphan classes that *do* survive an open |
| **E5** | A plausible reading of AC3 | `warning_count` keeps its current meaning (`session.dirty() ? 1 : 0`) *and* `issues` is added beside it | The two cannot both hold once AC6 requires **severity counts**. §2.4 reconciles them: `warning_count` becomes the count of `Warning` issues, and a `project.unsaved_changes` **Warning issue** is emitted exactly when `session.dirty()` — so the number is **numerically identical to the legacy value** on any project with no other warnings, which is every project the existing suites use |
| **E6** | A plausible reading of AC5 | "panel-focus metadata" already exists somewhere and the collector resolves through it | **It does not exist.** `grep -rn "panel_focus\|PanelFocus\|focused_panel\|FocusedPanel" src/ include/` returns **zero** hits. The only panel vocabulary in the tree is twelve `constexpr char k*WindowTitle[]` constants in `shell_state.hpp:979-990`, which are **shell** constants a model-layer module must not name. MAR-186 **introduces** the vocabulary (§2.9) |
| **E7** | A plausible reading of "non-canonical weights" | There is a shipped predicate for it | There is a shipped **canonicalizer** (`mesh_weight_model::canonicalize_mesh_weight_vertex`) and **no predicate**. §2.6 defines the predicate as the canonicalizer's own fixed point, which is legitimate only because the header documents it as **bit-exact** (`mesh_weight_model.hpp`, `kMeshWeightSumTolerance`'s doc comment: *"what makes canonicalization a bit-exact fixed point"*) |
| **E8** | A plausible reading of "stale preview references" | A stale reference is a load failure or is repaired on load | Neither. `PreviewController::normalize_state` (`session.cpp:723-758`) **silently substitutes**: an unresolvable `active_animation` is replaced by `data.animations().front().name`, and unresolvable `preview_skins` entries are dropped. The project on disk keeps the stale name, and every open re-substitutes. That silence is exactly what makes the class detectable and worth reporting |
| **E9** | **The team lead's MAR-186 brief**, adopted uncorrected by this document's §1.6, §2.12 and the plan's §0.4 and R11 | *"`CMakeLists.txt` has no `-Wall`, `-Wextra` or `-Wswitch` on any product target. Adding an enum value produces zero diagnostics at every switch site."* | **The premise about the flags is right and the conclusion is wrong. Clang enables `-Wswitch` by default; `-Wall` is not required.** Measured directly: `c++ -std=c++17 -c` with **no flags** on a two-of-three switch emits `warning: enumeration value 'C' not handled in switch [-Wswitch]` (Apple clang 21.0.0). MAR-185 measured **25** real warnings from a throwaway `TimelineKeyKind` value, now recorded at `AGENTS.md:305-334`. The brief inferred silence from a grep for flags and never compiled; the first draft repeated the inference. §1.6 and §2.12 are rewritten. **The real unpoliced class is `if`/`else` chains and fixed-length per-family call lists** — MAR-185 shipped a defect in exactly that class (`clipboard_track_count`, a six-term sum missing the seventh family) |
| **E10** | This document's §1.2, §9's `G-c`, and the plan's §0.10 | **Three** hard parser rejection rules bound AC2 | **Four.** Measured: `skeleton_parse.cpp:1616` (mesh weight unknown bone), `:5270` (animation unknown bone), `:5373` (animation unknown slot, under `slots`), and **`:5430` (animation unknown slot, under `deform`)** — the fourth was missed because the `deform` walk resolves its slot in a second, separate loop. As written the gate **fails on a correct tree**, which is worse than a wrong count: it sends the implementer hunting a regression that is not there |
| **E11** | This document's §1.3 versus its own §2.5 | §1.3 cites **six** overlay merge loops; §2.5's table lists **seven** families | **Seven**, and §2.5 was right. Measured: `grep -n "ensure_object_member(animations, edit.animation_name)" src/editor/project.cpp` → `5722, 5739, 5751, 5761, 5768, 5775, **5784**`. The `slot_attachment` loop at `:5784` was dropped in transcription. **A section-vs-section disagreement inside one document is the signature of a mis-transcribed total**, and finding one is grounds for re-summing its neighbours — which is how E10's fourth rule and M2's eleventh guard were then found |
| **E12** | This document's §1.5 and §A.3-equivalent anchors | Window titles at `shell_state.hpp:979-990`; `ensure_project_loaded` at `agent_dispatch.cpp:531-534` | **`:989-1000`** and **`:536`**. Both drifted under MAR-185. The count — twelve window titles — is unchanged |
| **E13** | This document's §3.1 | `marrow_editor_shell` "adds `src/editor` by hand" to reach a private header | **It does not.** `target_include_directories(marrow_editor_shell …)` (`CMakeLists.txt:916-921`) names only `external` and `src/renderer/generated`; the shell reaches `src/editor/*.hpp` because its own sources **live in that directory**. The substantive conclusion — a public header is required for MAR-187 and `marrow_project_smoke` — still holds, for `marrow_project_smoke`'s sake |
| **E14** | This document's §2.11 | `safe_fix_id` plus the typed target is a sufficient contract for MAR-187 | **It is not, and MAR-187 is blocked on it** (`plan-mar187`'s D6). A fix handler given only `remove_orphan_overlay` cannot tell *which* of the eight overlay vectors to erase from; its only route back is splitting `identity` on `\|` — the exact operation §2.2's escaping exists to make unsafe. §2.2 now carries a typed `DiagnosticCode` and `DiagnosticOverlayFamily` on `DiagnosticIssue`. Adding it here is the right direction; MAR-187 amending MAR-186's header from downstream is not |

---

## 1. The measured gap

Everything in this section was run against `6986024`.

### 1.1 `project.diagnostics` today is four constants

`handle_inspection_operation` (`src/editor/agent_handlers_inspection.cpp`), the
`op == "project.diagnostics"` branch (`:342-355` at `6986024`):

```cpp
if (op == "project.diagnostics") {
    json::Value::Object diagnostics;
    diagnostics.emplace("error_count", number_value(std::size_t{0}));
    diagnostics.emplace(
        "warning_count",
        number_value(static_cast<std::size_t>(session.dirty() ? 1U : 0U)));
    diagnostics.emplace("project_dirty", bool_value(session.dirty()));
    diagnostics.emplace("review_queue_count", number_value(control.review_queue.size()));
    return make_success(
        "Project diagnostics reported", op, spec, object_value(std::move(diagnostics)));
}
```

`error_count` is a **literal zero**. `warning_count` is a restatement of
`project_dirty`. Nothing walks the project. This is the entire operation.

What already asserts on it, and therefore what AC3's compatibility constraint is
measured against:

| Site | Asserts |
| --- | --- |
| `src/samples/agent_dispatch_smoke.cpp:1082-1085` | the response contains the member `error_count` |
| `:3800-3805` | `project_dirty` before the review sequence |
| `:3881-3885` | `review_queue_count == review_count` after six reviews |
| `:3886-3890` | `project_dirty` is **unchanged** across queueing six reviews |
| `tools/mcp/test_client.py:482` | the operation returns `ok` over the wire |

**`warning_count` is asserted nowhere.** That is freedom, not permission: AC3
names it, so §2.4 keeps it numerically identical wherever the existing suites can
see it, and proves that with a case.

### 1.2 What the load path already refuses — AC2's boundary is drawn by the runtime parser, not by this story

This is the single most consequential measurement in the design, because it
deletes most of what "orphan overlay" sounds like it means.

`build_runtime_document` (`src/editor/project.cpp:5637`) merges every overlay
into a **copy** of the base skeleton document and then `build_project_runtime`
(`:8258`) parses that document. **Four** parser rules are hard errors (E10 — the
first draft said three, and a gate built on three fails on a correct tree):

| Rule | Site at `5a56663` | Consequence |
| --- | --- | --- |
| `"animation references unknown bone '<name>'"` | `skeleton_parse.cpp:5270` | Any `transform` / `inherit` overlay naming a missing bone makes `load_project(path)` **fail** |
| `"animation references unknown slot '<name>'"` — the `slots` walk | `skeleton_parse.cpp:5373` | Any `slot_color` / `slot_attachment` overlay naming a missing slot makes the load **fail** |
| `"animation references unknown slot '<name>'"` — **the separate `deform` walk** | `skeleton_parse.cpp:5430` | Any `deform` overlay naming a missing slot makes the load **fail**. Two distinct sites emit the same message text, which is exactly why a grep on the message returns fewer *rules* than a reader expects |
| `"mesh weight references unknown bone '<name>'"` | `skeleton_parse.cpp:1616` | Any mesh-weight influence naming a missing bone makes the load **fail** |

So in **every session the collector can ever see**, an overlay's bone and slot
resolve, and every weight influence names a live bone. AC2's *"limited to
normally opened sessions; partial or degraded first-open diagnostics … are
excluded"* is not a scoping convenience — it is the parser's own boundary, and
MAR-186 adopts it rather than fighting it.

**This is a positive invariant, not only a restriction.** It is what lets §2.9
attach a `BoneSelection` or `SlotSelection` to an orphan-animation issue with
confidence that the bone or slot still exists.

### 1.3 What survives a normal open — the three detectable families

| # | Family | Mechanism that makes it survivable | Site |
| --- | --- | --- | --- |
| **F1** | **Orphan animation overlay.** A timeline overlay names an animation the project's catalog does not contain | The merge loop calls `ensure_object_member(animations, edit.animation_name)`, which **creates** the animation. The project opens; the phantom animation is exported into every `.mskl` and `.mbin` | **Seven** merge loops (E11 — the first draft said six and disagreed with its own §2.5): `project.cpp:5722, 5739, 5751, 5761, 5768, 5775, 5784`. Re-measured at `5a56663` with `grep -n "ensure_object_member(animations, edit.animation_name)"` |
| **F2** | **Orphan mesh-weight overlay.** A weight overlay names a skin/slot/attachment triple that resolves to nothing | The merge loop `continue`s on `attachment_value == nullptr`. The overlay is dead data, silently ignored, and survives every save | `project.cpp:5696-5706`. The behaviour is documented on purpose at `:5220` — *"the walk continues, exactly as the mesh-weight overlay skips a missing attachment"* |
| **F3** | **Stale preview reference.** `editor_metadata.active_animation` or an entry of `editor_metadata.preview_skins` resolves to nothing | `PreviewController::normalize_state` substitutes the first animation and drops unresolvable skins. The project on disk is never corrected | `session.cpp:723-733` (skins), `:754-758` (animation) |
| **F4** | **Non-canonical mesh weights.** A weight vertex that `canonicalize_mesh_weight_vertex` would change or reject | Neither `parse_mesh_weight_vertices` (`project.cpp:2149-2286`) nor `validate_project_for_save` (`:6157-6221`) requires a unit sum, descending order, or a weight above `kMeshWeightEpsilon`. Both accept any positive weight | §1.4 |

F1 and F2 are AC4's *"orphan overlays"*; F4 is its *"non-canonical weights"*; F3
is its *"stale preview references"*. Each is a real defect a project can carry
today, each survives a save/reload round trip, and each has a repair MAR-187 can
name.

### 1.4 Canonical weights are already defined; nothing checks them

`src/editor/mesh_weight_model.hpp` / `.cpp` own the rules. `canonicalize_mesh_weight_vertex`
runs nine steps: reject empty bone names, reject non-finite weights and offsets,
reject unresolvable bones, merge duplicate bones, drop influences at or below
`kMeshWeightEpsilon` (`1e-6`), total-order sort by descending weight then
ascending bone index, cap at `kMaxMeshWeightInfluences` (4), reject an empty
result, and normalize to a unit sum within `kMeshWeightSumTolerance`
(`4 * DBL_EPSILON`) — re-sorting after the division because it can tie two
weights.

The header states the property this design depends on:

> *"This tolerance is what makes canonicalization a **bit-exact fixed point**: a
> second pass sees a sum already inside it and skips the division entirely."*

What a `.marrow` can carry that is **not** a fixed point, verified against both
gatekeepers:

| Input | `parse_mesh_weight_vertices` | `validate_project_for_save` | `canonicalize_…` |
| --- | --- | --- | --- |
| weights summing to `0.7` | accepts (only `sum > 0`) | accepts (only `sum > 0`) | **normalizes** → non-canonical |
| ascending weights `[0.3, 0.7]` | accepts | accepts | **reorders** → non-canonical |
| a lone influence of weight `1e-9` | accepts (only `> 0`) | accepts (only `> 0`) | **rejects**: `"A weighted vertex must keep at least one positive influence."` |
| duplicate bone in one vertex | rejects (`:2228-2235`) | rejects (`:6206-6212`) | unreachable from a file |
| 5 influences | rejects (`:2186-2194`) | rejects (`:6184-6190`) | unreachable from a file |
| a non-finite weight | unreachable — JSON has no NaN and `json.cpp` refuses an out-of-range exponent at the tokenizer | — | unreachable from a file |
| an unresolvable bone name | accepts | accepts | rejects — **but `skeleton_parse.cpp:1610-1617` makes the project unopenable first** (§1.2) |

Two of those are **file-reachable**, and one of them — the `1e-9` lone influence
— is file-reachable **and uncanonicalizable**, which is what gives MAR-186 a
genuine `Error`-severity issue that is not invented. The runtime accepts the
`1e-9` weight (`skeleton_parse.cpp:1633-1640` requires only `> 0`, then
normalizes at runtime), so such a project opens normally and the collector runs
over it.

### 1.5 There is no panel-focus vocabulary anywhere in the tree

`grep -rn "panel_focus\|PanelFocus\|focused_panel\|FocusedPanel" src/ include/` →
**0 hits**. The nearest things are:

- twelve `constexpr char k*WindowTitle[]` constants at `shell_state.hpp:989-1000`
  (`Project`, `Runtime Assets`, `Constraints`, `Hierarchy`, `Timeline`,
  `Viewport`, `Properties`, `Agent`, `Parameters`, `Shapes / Deformers`,
  `Expressions`, `Lip Sync`); and
- `ShellState::timeline_focus_before` (`:493`, `:543`), which is an
  `std::optional<std::string>` snapshot inside a *drag* record, unrelated.

MAR-186 introduces the vocabulary. It must **not** name a window title: those are
shell constants, and a model-layer module that includes `shell_state.hpp` breaks
AC2's UI-free requirement and MAR-187's ability to reuse the collector headlessly.

### 1.6 The compiler DOES police exhaustive switches. It does not police call lists — and that is where the risk is

**This section previously said the opposite, on an inherited and unverified
premise. See E9.** The flag premise is true and the conclusion drawn from it was
false.

`CMakeLists.txt` carries **no `-Wall`, no `-Wextra`, no explicit `-Wswitch`** on
any product target; `-Werror` appears exactly once, at `:352`, scoped to
`marrow_constraint_warning_check`. **None of that matters, because Clang enables
`-Wswitch` by default.** Measured directly, no flags at all:

```
$ printf 'enum class E{A,B,C};\nint f(E e){switch(e){case E::A: return 1; case E::B: return 2;} return 0;}\n' > /tmp/wsw.cpp
$ c++ -std=c++17 -c /tmp/wsw.cpp -o /tmp/wsw.o
/tmp/wsw.cpp:2:19: warning: enumeration value 'C' not handled in switch [-Wswitch]
```

MAR-185 measured this for real: a throwaway seventh `TimelineKeyKind` value
produced **25** warnings, and `AGENTS.md:305-334` now records it. (A
`marrow_editor`-scoped probe sees only 24; the twenty-fifth,
`timeline_key_kind_carries_easing`, is compiled into `marrow_editor_shell`.)

**What this changes for MAR-186.** The two enums it introduces —
`DiagnosticSeverity` and `DiagnosticPanel` — plus the two E14 adds —
`DiagnosticCode` and `DiagnosticOverlayFamily` — are **compiler-policed wherever
they are switched exhaustively**. MAR-187 adding a `DiagnosticPanel` value will
be told about every switch site it missed. That is a reason to *prefer* an
exhaustive switch over an `if`/`else` chain or a lookup table in this module, and
§2.2 and §3.2 are written accordingly: `diagnostic_severity_name`,
`diagnostic_panel_name`, `diagnostic_code_name` and
`diagnostic_overlay_family_name` are each a `switch` with **no `default:` arm**,
because a `default:` silences exactly the diagnostic that makes them safe.

**The unpoliced class is different, and MAR-186 has a member of it.** No compiler
checks a fixed-length list of calls, an `if`/`else` chain, or an N-term sum.
MAR-185 shipped a defect in precisely that class — `clipboard_track_count`, a
six-term sum that was never extended to the seventh timeline family. MAR-186's
equivalent is `collect_orphan_animation_overlays`' **seven family calls** (§2.5):
adding an eighth overlay vector to `ProjectData` and forgetting the eighth call
produces no diagnostic, no test failure from any existing case, and a detector
that silently under-reports. §2.5 and the plan's Task 2 treat that list, not the
enums, as the thing needing a hand-written guard.

The containment decision stands on its own merits: **every switch over these four
enums lives in `src/editor/diagnostics.cpp`**, findable with
`grep -rn "DiagnosticPanel::\|DiagnosticSeverity::\|DiagnosticCode::\|DiagnosticOverlayFamily::"`.
It is no longer a mitigation for a silent compiler — it is a way to keep the
policed surface in one reviewable file.

### 1.7 The registry does not move

MAR-186 adds no agent operation. `project.diagnostics` already exists
(`agent_dispatch.cpp:41`) and already has an MCP tool with an empty `inputSchema`
(`tools/mcp/tools/inspection.py:108-114`). AC3 extends the **payload** of an
existing operation, which is additive and count-neutral.

Measured at `5a56663`, after MAR-185 moved the number:

```
grep -c '^\s*{\s*"' src/editor/agent_dispatch.cpp                     -> 66
grep -n "OperationExpectation, " src/samples/agent_dispatch_smoke.cpp -> :39, 66
grep -rn "!= 66U" src/ | wc -l                                        -> 11
grep -nE "== 66" tools/mcp/test_client.py                             -> :53, :55
ctest --test-dir build -N | tail -1                                   -> Total Tests: 22
```

The guard count is **eleven**, not the ten the first draft recorded at `6986024`:
MAR-185 added one. Re-summed rather than carried forward, per E11's lesson.

MAR-186 registers **no** CTest target either: the collector is one new source
file inside the existing `marrow_editor` static library, and its cases live in
suites that already run.

### 1.8 `player_idle.marrow` is issue-free — measured

The fixture every existing suite runs against:

```
top-level keys : constraint_edits, editor, marrow, runtime, snap, timeline_edits
editor         : active_animation "idle", preview_skins ["default"], name "player_idle_fixture"
mesh_edits     : ABSENT  (no mesh weight overlays at all)
timeline_edits : animations -> { "idle" }   (the only animation any overlay names)
player_idle.mskl animations : aim, attack, idle
player_idle.mskl skins      : default, mage, mage_arm, mesh_base, warrior
```

`idle` exists, `default` exists, there are no weight overlays, and no overlay
names an animation outside the base catalog. **`collect_project_diagnostics` over
`player_idle.marrow` returns zero issues.** That is what makes it the legacy
compatibility witness (§2.4) and the determinism baseline: `error_count` stays
`0` and `warning_count` stays `dirty ? 1 : 0` for every existing assertion in
`agent_dispatch_smoke.cpp` and `test_client.py`, with no edit to either file's
existing lines.

---

## 2. Decisions

### 2.1 Two entry points: a pure collector and a session wrapper

```cpp
// include/marrow/editor/diagnostics.hpp
DiagnosticReport collect_project_diagnostics(
    const ProjectData& project,
    const runtime::SkeletonData& skeleton,
    const runtime::json::Document& base_skeleton_document);

std::optional<DiagnosticReport> collect_session_diagnostics(const EditorSession& session);
```

The **pure** entry point is a function of three `const&` values. It has no
session, no history, no revisions, no preview, no `SelectionSet`, and no ImGui.
It is what `marrow_project_smoke` calls directly, and it is what makes AC2's
"UI-free, read-only" a property of the *signature* rather than a claim.

The **session wrapper** adds only what a session knows: `project_dirty`, the
`project.unsaved_changes` issue (§2.4), and the two revisions the report is
stamped with (§2.2). It returns `std::nullopt` when the session is not normally
opened — `!has_project()`, or a null `project()`, `runtime_data()`, or
`base_skeleton_document()`. That nullopt is AC2's exclusion made mechanical.

Why three parameters and not "the session":

- `base_skeleton_document` is **required**, not optional. An optional input that
  silently disables the F1 detector is the exact shape of a test that cannot
  fail: the fixture passes `nullptr`, the orphan sweep never runs, and every
  assertion about "no orphans" passes for the wrong reason.
- The dispatcher's own guard, `ensure_project_loaded` (`agent_dispatch.cpp:531-534`),
  checks `has_project()`, `project()` and `runtime_data()` — **not**
  `base_skeleton_document()`. `runtime.validate` re-checks it by hand
  (`agent_handlers_inspection.cpp:44-56`) for exactly this reason, and the
  wrapper does the same.

### 2.2 The issue record, and what "stable identity" means

```cpp
enum class DiagnosticSeverity { Error, Warning };
enum class DiagnosticPanel { Project, Timeline, Weights };

/// The issue vocabulary, typed. `code` is NOT a string: see below.
enum class DiagnosticCode {
    OverlayOrphanAnimation,
    OverlayOrphanWeightTarget,
    WeightsNonCanonical,
    WeightsUncanonicalizable,
    PreviewStaleAnimation,
    PreviewStaleSkin,
    ProjectUnsavedChanges,
};

/// Which ProjectData vector an overlay issue came from. `None` for issues that
/// are not about an overlay.
enum class DiagnosticOverlayFamily {
    None,
    Transform, Inherit, Deform, DrawOrder, Event, SlotColor, SlotAttachment,
    MeshWeight,
};

struct DiagnosticTarget {
    DiagnosticPanel panel{DiagnosticPanel::Project};
    std::optional<SelectionItem> selection;      // selection.hpp's variant
    std::string animation_name;                  // empty when not animation-scoped
    std::optional<std::size_t> vertex_index;     // weight issues only
};

struct DiagnosticIssue {
    DiagnosticCode code{DiagnosticCode::ProjectUnsavedChanges};
    DiagnosticOverlayFamily family{DiagnosticOverlayFamily::None};
    DiagnosticSeverity severity{DiagnosticSeverity::Warning};
    std::string identity;
    std::string message;
    DiagnosticTarget target;
    std::string safe_fix_id;                     // EMPTY when no safe fix applies
};

struct DiagnosticReport {
    std::vector<DiagnosticIssue> issues;         // sorted, strictly increasing by identity
    std::size_t error_count{0};
    std::size_t warning_count{0};
    bool project_dirty{false};
    std::uint64_t project_revision{0};
    std::uint64_t runtime_revision{0};
};
```

**`code` and `family` are enums, not strings, and that is a hard requirement, not
a style choice** (E14). A fix handler holding only `safe_fix_id ==
"remove_orphan_overlay"` cannot tell which of the eight overlay vectors to erase
from; its only route back to that fact would be splitting `identity` on `|` —
the exact operation the escaping below exists to make unsafe. `family` gives
MAR-187 the vector directly, `code` gives it the dispatch, and neither can be
mistyped: both are `switch`ed exhaustively in one file and Clang's default
`-Wswitch` (§1.6) reports a missed arm. The wire form is produced by
`diagnostic_code_name` and is unchanged from the first draft's strings
(`"overlay.orphan_animation"` and the rest).

**Identity is derived only from the issue's code and the coordinates of the thing
it is about.** Never from a vector index, never from iteration order, never from
a float. AC1's *"identity across unchanged revisions"* then holds by construction,
and — more usefully — identity survives an *unrelated* edit that shifts the
offending record's position in its vector, which is what MAR-187 needs to keep a
selected row selected across a refresh.

The encoding is `code`, then the scope tokens, joined by `|`:

```
overlay.orphan_animation|transform|ghost|arm_l|rotate
overlay.orphan_animation|inherit|ghost|arm_l
overlay.orphan_animation|deform|ghost|body|body_mesh
overlay.orphan_animation|draw_order|ghost
overlay.orphan_animation|event|ghost
overlay.orphan_animation|slot_color|ghost|body
overlay.orphan_animation|slot_attachment|ghost|body
overlay.orphan_weight_target|mesh_base|body|ghost_mesh
weights.non_canonical|mesh_base|body|body_mesh|7
weights.uncanonicalizable|mesh_base|body|body_mesh|7
preview.stale_animation|ghost
preview.stale_skin|ghost_skin
project.unsaved_changes
```

**Every token is escaped**: `\` → `\\` and `|` → `\|`, applied per token before
joining. Animation, bone, slot, skin and attachment names are arbitrary
user-authored strings; two different issues whose names differ only in where a
`|` falls would otherwise collide on one identity, and a collision is worse than
a wrong string because §2.3's de-duplication would then **delete** one of them.
This is a real hazard, not a hypothetical: nothing in `validate_project_for_save`
constrains the character set of any of those names.

The transform family carries its **channel** token because
`(animation, bone, channel)` is that family's key — two channels on one bone are
two independent overlays.

### 2.3 Deterministic order is lexicographic by identity, and identities are unique

`issues` is sorted by `identity` alone, ascending, byte-wise. Because every
identity begins with its code, that is equivalent to sorting by `(code, scope)`
and it needs no second comparator. After sorting, **equal identities are
collapsed, keeping the first**.

Why de-duplicate rather than assert uniqueness:

- `validate_project_for_save` rejects duplicate mesh-weight edits (`:6167-6176`)
  and duplicate draw-order edits, but a `ProjectData` assembled **in memory** —
  which is exactly what the agent handlers and MAR-187's fixes operate on — can
  hold duplicates that were never saved.
- `editor_metadata.preview_skins` can hold the **same name twice**; nothing
  refuses it. Two identical `preview.stale_skin` issues from one vector is the
  most likely duplicate in practice.

A de-duplicated, sorted list satisfies one assertion that covers both properties
at once: **`identity[i] < identity[i+1]` strictly, for every adjacent pair.**
That single check is the story's determinism invariant, and every case asserts it.

Severity is deliberately **not** part of the sort. MAR-187's AC1 says the view
"groups and filters issues by severity"; grouping is the view's job, and a
collector that pre-groups would force MAR-187 to re-sort to get a stable row
identity.

Iteration determinism is not left to chance either: `runtime::json::Value::Object`
is `std::map<std::string, Value, std::less<>>` (`include/marrow/runtime/json.hpp:24`),
so every walk over a JSON object is already in sorted key order, and every walk
over a `ProjectData` vector is in stored order. The sort makes the *output* order
independent of both.

### 2.4 Severity, and how legacy `error_count` / `warning_count` are preserved

Two severities. `Error` means *the exported runtime bundle is wrong* — data that
silently corrupts what ships. `Warning` means *the project is untidy or a
reference has quietly degraded* — nothing downstream is wrong yet.

| Code | Severity | Reason |
| --- | --- | --- |
| `overlay.orphan_animation` | **Error** | The phantom animation is written into every `.mskl` and `.mbin` export. That is shipped corruption |
| `overlay.orphan_weight_target` | Warning | Dead data. `build_runtime_document` skips it; nothing downstream sees it |
| `weights.non_canonical` | Warning | The runtime normalizes at load, so the rendered result is right; the stored data is merely not the fixed point |
| `weights.uncanonicalizable` | **Error** | A vertex whose entire influence list is below `kMeshWeightEpsilon` has no meaningful binding, and no safe repair exists |
| `preview.stale_animation` | Warning | The preview silently falls back; nothing exported is affected |
| `preview.stale_skin` | Warning | Silently dropped at bind |
| `project.unsaved_changes` | Warning | The legacy signal, preserved (below) |

**AC3's compatibility, discharged precisely.** `error_count` becomes the number
of `Error` issues and `warning_count` the number of `Warning` issues — AC6
requires "severity counts", so they must be issue-derived. That would drop the
shipped `warning_count = dirty ? 1 : 0` behaviour, which AC3 forbids. The
reconciliation is to make the legacy signal an issue in its own right:
`collect_session_diagnostics` appends `project.unsaved_changes` (Warning, no safe
fix) exactly when `session.dirty()`. Then:

- a clean project with no problems → `0 / 0`, identical to today;
- a dirty project with no other problems → `0 / 1`, identical to today;
- **every project any existing suite runs against** is one of those two (§1.8),
  so no shipped assertion changes and none needs editing.

`project_dirty` and `review_queue_count` keep their exact current expressions:
`session.dirty()` and `control.review_queue.size()`. `review_queue_count` is
**not** an issue — the review queue is a permissions concept, not a project
defect, and turning it into one would make `warning_count` move whenever an agent
queues a save. That would break `agent_dispatch_smoke.cpp:3886-3890`, which
asserts the diagnostics response is *unchanged* across queueing six reviews.

The pure collector never emits `project.unsaved_changes` — it has no session and
no notion of dirtiness. Its `project_dirty` is `false` and its `warning_count`
counts only project-derived warnings. The wrapper appends, **re-sorts, and
re-counts**; it does not `push_back` onto a sorted vector and hope.

### 2.5 The seven overlay families, and the animation-name authority

`ProjectData` (`include/marrow/editor/project.hpp:585-614` at `6986024`) holds
**seven** animation-scoped overlay vectors, which the collector sweeps in this
order (the sort makes the order cosmetic, but a fixed order makes the code
readable and the sweep auditable):

| Family token | Vector | Scope tokens after the animation |
| --- | --- | --- |
| `transform` | `transform_timeline_edits` | bone, channel |
| `inherit` | `bone_inherit_timeline_edits` | bone |
| `deform` | `mesh_deform_timeline_edits` | slot, attachment |
| `draw_order` | `draw_order_timeline_edits` | — |
| `event` | `event_timeline_edits` | — |
| `slot_color` | `slot_color_timeline_edits` | slot |
| `slot_attachment` | `slot_attachment_timeline_edits` | slot |

`mesh_weight_attachment_edits` is **not** in this list: it is skin-scoped, not
animation-scoped, and is F2's subject.

**This list is the one thing in MAR-186 no compiler will check** (§1.6). Adding an
eighth overlay vector to `ProjectData` and forgetting the eighth call here yields
no diagnostic, no shipped-case failure, and a detector that silently
under-reports — the class MAR-185 shipped `clipboard_track_count` into. The
mitigation is a hand-written guard, right beside the calls:

```cpp
// Every animation-scoped overlay vector in ProjectData must be swept here.
// NOTHING checks this list -- not the compiler, not any shipped case. If you
// add a vector to ProjectData, add a call AND bump this number.
static_assert(kSweptOverlayFamilyCount == 7,
              "an overlay family was added to ProjectData without a sweep");
```

`kSweptOverlayFamilyCount` is a file-local constant equal to the number of calls
written out below it. The `static_assert` cannot detect the omission on its own —
nothing can, in C++17 without reflection — but it puts the number in the
developer's edit path, and G1 asserts all seven identities so a *changed* number
that is not matched by a call fails loudly. This is a guard, not a proof, and
§10 records it as such.

**The authority for "does this animation exist" is neither the base document nor
the materialized skeleton.**

- The **base document** is wrong because `animation_edits` can `Create` an
  animation that exists only in the project.
- The **materialized `SkeletonData`** is wrong because it already contains the
  phantom animations F1 is trying to find — `ensure_object_member` created them.
  A collector that resolves against `SkeletonData` reports **zero** orphans,
  always, and every test passes.

The authority is the base document's `animations` object **after**
`apply_animation_edits` and **before** the overlay merge — precisely the state
`build_runtime_document` is in at `project.cpp:5694`. `apply_animation_edits` is
in `project.cpp`'s anonymous namespace (the file's `namespace { … }` spans
`:24-6862`), so `diagnostics.cpp` **cannot call it**. This is MAR-184's D2 hazard
in a new place, and the answer is the same one MAR-184 reached for
`finite_animation_scalar`: do not reimplement the fold.

MAR-186 adds one small public read-only helper, **defined in `project.cpp`** where
`apply_animation_edits` is reachable:

```cpp
/// @brief Animation names the project authors, after catalog edits and before overlays.
std::vector<std::string> authored_animation_names(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document);
```

It copies `base_skeleton_document.root`, calls
`apply_animation_edits(&copy, project.animation_edits)`, and returns the sorted
keys of the copy's `animations` object. Reimplementing the Create/Rename/Delete/
SetDuration/Unknown fold in `diagnostics.cpp` would be a second source of truth
that drifts the first time someone adds an `AnimationEditKind` — and, per §1.6,
with no compiler diagnostic to catch it. The document copy is the cost of not
having a second implementation; diagnostics is an on-demand operation, not a
frame-loop one.

Note that `apply_animation_edits` walks `root`, not just `animations`, because a
`Rename` also rewrites `mixing` references (`project.cpp:5450`). Copying the whole
root is therefore correct as well as simple.

### 2.6 Non-canonical weights: the canonicalizer is its own predicate

For each `MeshWeightVertexEdit`, the collector copies it, calls
`mesh_weight_model::canonicalize_mesh_weight_vertex(skeleton, &copy)`, and:

- non-empty returned error → `weights.uncanonicalizable` (Error, **no safe fix**),
  with the canonicalizer's own message quoted verbatim in the issue message;
- empty error and `copy.influences != original.influences` → `weights.non_canonical`
  (Warning, safe fix `normalize_weights`);
- otherwise no issue.

Comparison is **exact** on every member — `bone_name`, `x`, `y`, `weight` — with
`==` on the doubles. That is legitimate *only* because the header documents
canonicalization as a bit-exact fixed point (§1.4); an approximate comparison
would make an already-canonical vertex report as non-canonical on some inputs and
not others, which is the opposite of deterministic. If Task 0's fixed-point gate
(§9, G4) fails, this decision is wrong and the story stops to re-plan.

`normalize_weights` is not a new fix identifier invented here: it is the **shipped
agent operation name** (`agent_dispatch.cpp:73`) and the shipped shell command
(`shell_weight_paint.hpp:62`, `normalize_weights_command`). MAR-187 wires the fix
to the thing that already exists.

**An orphan weight target supersedes its own weight issues.** When F2 fires for a
`MeshWeightAttachmentEdit`, that edit's vertices are **not** examined. The weights
are dead data the runtime never sees; reporting that dead data is also badly
sorted would give the user two rows for one problem, and MAR-187's
`remove_orphan_overlay` fix would make the second row vanish without the user
having addressed it. One edit, one issue.

### 2.7 Stale preview references

Both are resolved against the **materialized** `SkeletonData`, not the base
document:

- `preview.stale_animation` — `!editor_metadata.active_animation.empty() &&
  skeleton.find_animation(active_animation) == nullptr`.
- `preview.stale_skin` — for each **distinct** entry of `editor_metadata.preview_skins`,
  `!name.empty() && !skeleton.find_skin_index(name).has_value()`.

The materialized skeleton is right here because it is what the preview actually
binds against (`session.cpp:1743-1748`): if an orphan overlay has resurrected the
animation the project points at, the preview genuinely works and there is nothing
stale to report.

That produces a real, documented **interaction**: applying MAR-187's
`remove_orphan_overlay` fix to a phantom animation that `active_animation` names
will *create* a `preview.stale_animation` issue. MAR-187's AC3 already requires
the view to refresh from project and runtime revisions, so the interaction is
handled by design rather than hidden — but it is stated here because it is
surprising, and because it is a case worth writing (§6, case G9).

An empty `active_animation` is legal (setup pose) and produces **no** issue. An
empty entry in `preview_skins` produces no issue either — `validate_project_for_save`
already refuses to save one (`project.cpp:5943-5948`), so it is not a state a
loaded project can be in, and reporting it would be an unreachable branch.

### 2.8 Safe-fix IDs are an allowlist of exactly three

```cpp
inline constexpr std::string_view kSafeFixRemoveOrphanOverlay = "remove_orphan_overlay";
inline constexpr std::string_view kSafeFixNormalizeWeights    = "normalize_weights";
inline constexpr std::string_view kSafeFixResetPreviewReference = "reset_preview_reference";

bool is_allowlisted_safe_fix(std::string_view safe_fix_id);
```

They are exactly MAR-187's three: *"The only initial fixes are orphan-overlay
removal, canonical weight normalization, and stale-preview-reference reset."*

| Code | `safe_fix_id` |
| --- | --- |
| `overlay.orphan_animation` | `remove_orphan_overlay` |
| `overlay.orphan_weight_target` | `remove_orphan_overlay` |
| `weights.non_canonical` | `normalize_weights` |
| `weights.uncanonicalizable` | **absent** |
| `preview.stale_animation` | `reset_preview_reference` |
| `preview.stale_skin` | `reset_preview_reference` |
| `project.unsaved_changes` | **absent** |

AC4 says *"with safe-fix IDs only where applicable"*, and the two absences are
what make that clause mean something. `weights.uncanonicalizable` has no safe fix
because normalization is precisely what **cannot** repair it — the canonicalizer
rejects the vertex. `project.unsaved_changes` has no safe fix because saving is a
reviewed, user-initiated operation, never an automatic repair.

On the wire, an absent safe fix **omits the member** rather than emitting `""`.
An empty string is a value a careless consumer treats as present; an absent key
is not. The MCP case asserts absence by key.

The allowlist is enforced, not merely documented: `is_allowlisted_safe_fix` is
the only accepted vocabulary, and one invariant assertion — *every issue's
non-empty `safe_fix_id` is allowlisted* — runs over every issue of every case.

### 2.9 Typed targets and panel focus

`DiagnosticTarget::selection` is `std::optional<SelectionItem>`, the variant from
`include/marrow/editor/selection.hpp` — `BoneSelection`, `SlotSelection`,
`AttachmentSelection`, `ConstraintSelection`. AC5's *"resolve through SelectionSet"*
is satisfied by the target being **expressible in `SelectionSet`'s own vocabulary**:
MAR-187 calls `SelectionSet::replace(*issue.target.selection)` and the right thing
becomes active.

| Code | `panel` | `selection` | `animation_name` | `vertex_index` |
| --- | --- | --- | --- | --- |
| `overlay.orphan_animation`, bone families | `Timeline` | `BoneSelection{bone}` | the orphan name | — |
| `overlay.orphan_animation`, slot families | `Timeline` | `SlotSelection{slot}` | the orphan name | — |
| `overlay.orphan_animation`, `draw_order` / `event` | `Timeline` | **absent** | the orphan name | — |
| `overlay.orphan_weight_target` | `Weights` | `AttachmentSelection{slot, skin, attachment}` | — | — |
| `weights.non_canonical`, `weights.uncanonicalizable` | `Weights` | `AttachmentSelection{slot, skin, attachment}` | — | the vertex |
| `preview.stale_animation`, `preview.stale_skin` | `Project` | **absent** | the stale animation, or empty | — |
| `project.unsaved_changes` | `Project` | **absent** | — | — |

Two of these are load-bearing and easy to get wrong:

- **Bone and slot selections are guaranteed to resolve** — §1.2's parser rules
  mean a project whose overlay names a missing bone or slot cannot be opened. So
  MAR-187 can select them without a existence check.
- **`overlay.orphan_weight_target`'s `AttachmentSelection` deliberately does
  NOT resolve.** It names the missing triple, because naming it is the only way
  the user can see *what* is orphaned. MAR-187's AC3 — *"handles removed targets
  without stale selection or crashes"* — is the clause that covers it, and this
  issue is its first real input.

`AttachmentSelection`'s member order in `selection.hpp` is `slot_name`,
`skin_name`, `attachment_name`, while `MeshWeightAttachmentEdit`'s is `skin_name`,
`slot_name`, `attachment_name`. **The two are transposed.** Aggregate
initialization from the edit in field order silently produces a target with skin
and slot swapped, and every test that only checks `issue_count` passes. §6's
case G6 asserts the three fields by name.

`DiagnosticPanel` ships **three** values — `Project`, `Timeline`, `Weights` —
because those are the three MAR-186 emits. MAR-187's AC2 names five panels
(*"hierarchy, timeline, inspector, weight, or preview"*); the two it adds are
**MAR-187's** to add. Clang's default `-Wswitch` (§1.6) will name every switch
site it misses, which is a further reason to ship three honest values rather than
five speculative ones: the compiler turns the extension into a checklist.
Shipping dead enumerators now would be speculative and, worse, would let MAR-187
believe the sweep was already done.

The enum names a **panel**, never a window. The mapping from `DiagnosticPanel` to
`kTimelineWindowTitle` and friends belongs to MAR-187, in the shell, where those
constants live.

### 2.10 Read-only and zero-mutation, and how each half is actually proved

AC2 and AC6 both demand it, and the two halves need different evidence. `AGENTS.md`'s
hazard **H4** is explicit that an assertion reading state a passing run leaves at
its default is not evidence.

| Half | How it is proved |
| --- | --- |
| The collector cannot mutate the project, runtime or session | **Structurally.** Every parameter is a `const&`; `DiagnosticReport` owns copies. Plus a **measured** case: snapshot `serialize_project(*session.project())`, `session.dirty()`, `undo_count()`, `redo_count()`, `project_revision()`, `runtime_revision()`, `preview_revision()` around a `collect_session_diagnostics` call and assert all seven bit-identical. Those *can* move — `project_revision` moves on any edit — so the assertion is not vacuous |
| The collector does not mutate **selection** | **Structurally only, and recorded as such.** The collector takes no `SelectionSet`, mutable or otherwise; there is nothing for it to write to. A test that builds a `SelectionSet`, runs collection, and asserts it unchanged is **H4-vacuous** and this design does not ask for one. What *is* asserted is the substantive half of AC5: each issue's `target.selection`, fed to `SelectionSet::replace`, makes the expected typed identity active |
| The module is UI-free | `grep -n "include" src/editor/diagnostics.cpp` must name no `imgui`, no `shell_`, no `sokol`. Stated as a command, not as a claim |

### 2.11 What MAR-186 does not do — MAR-187's boundary

MAR-186 ships **no** Problems view, **no** filter, **no** grouping, **no**
navigation, **no** panel focusing, and **no** fix. It ships the *vocabulary* those
need: a **typed code**, a **typed overlay family**, a severity, an identity, a
message, a typed target, a panel, and a fix identifier.

**The typed code and family are part of the contract, not an implementation
detail** (E14). `plan-mar187`'s D6 established that a fix cannot be applied
without them: `safe_fix_id` says *what kind of repair*, `family` says *which
`ProjectData` vector*, and `code` says *which handler*. Without `family`, the
`remove_orphan_overlay` handler would have to recover the vector by parsing
`identity`, which §2.2's escaping deliberately makes unsafe to split. They are
specified here rather than added by MAR-187, because a downstream story amending
an upstream header is the wrong direction of dependency.

MAR-187 consumes `DiagnosticReport` and adds:

- a window that groups and filters by severity;
- activation that calls `SelectionSet::replace` and focuses the panel named by
  `DiagnosticTarget::panel`;
- the two extra `DiagnosticPanel` values it needs — with every missed switch arm
  named by Clang's default `-Wswitch` (§1.6);
- three fixes keyed on `safe_fix_id`, each one validated transaction and one undo
  entry;
- a refresh keyed on `project_revision` / `runtime_revision`, which is why
  `DiagnosticReport` carries both.

The boundary is drawn so MAR-187 writes UI, not model. If MAR-187 finds itself
re-deriving an identity, re-sorting, or re-resolving an animation name, MAR-186
under-delivered.

### 2.12 Formats, registry, versions, and the warning flags do not move

- `.marrow` gains **no** key. Diagnostics are derived, never stored — a stored
  diagnostic would go stale the moment the project changed, and AC1's identity
  guarantee is a *derivation* guarantee.
- `.mskl` stays version **1**; `.mbin` stays version **2**; the C ABI is untouched.
- The agent registry gains **no** row (§1.7). Whatever Task 0 measures — 64 today,
  66 after MAR-185 — must be identical at the end. The gate is a **non-effect**
  gate.
- `ctest -N` is unchanged (**22** today): one new source file inside an existing
  static library, no new target.
- **`-Wswitch` is already on** (§1.6, E9) — nothing needs adding to get the
  diagnostic. What is absent is `-Werror`, i.e. whether a missed arm **stops the
  build** or merely prints. Promoting it for this one file would be
  `set_source_files_properties(src/editor/diagnostics.cpp PROPERTIES COMPILE_OPTIONS
  "$<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Werror=switch>")`.
  It is **deferred**, for a smaller reason than the first draft gave: the warning
  already fires and is visible in the build log, so the marginal value is turning
  a visible warning into a hard stop, and that is a build-policy decision for a
  tree that has deliberately kept exactly one `-Werror`, scoped to a check target.
  The recipe is recorded so MAR-187 does not have to rediscover it. **GCC also
  enables `-Wswitch` only under `-Wall`**, so a non-Clang build would be silent —
  another reason the recipe is written with a compiler-id guard rather than assumed.
- **The obligation §1.6 actually creates is on the unpoliced list**, not the
  enums: `collect_orphan_animation_overlays`' seven family calls get a
  hand-written guard and a comment, because no compiler will ever check them.

---

## 3. Where each piece lands

### 3.1 `include/marrow/editor/diagnostics.hpp` — **new**

The two enums, `DiagnosticTarget`, `DiagnosticIssue`, `DiagnosticReport`, the
three `kSafeFix*` constants, the two collector entry points, and three small
readers: `diagnostic_severity_name`, `diagnostic_panel_name`,
`diagnostic_code_name`, `diagnostic_overlay_family_name`, and
`is_allowlisted_safe_fix`. Each name function is an exhaustive `switch` with **no
`default:` arm** (§1.6).

Public, because **`marrow_project_smoke` cannot reach `src/editor/` headers**:
`marrow_editor`'s `src/editor` include directory is `PRIVATE`
(`CMakeLists.txt:521-527`), and the smoke adds it by hand only for its own use.
*(E13: the first draft also claimed `marrow_editor_shell` adds it by hand. It
does not — `target_include_directories(marrow_editor_shell …)` at
`CMakeLists.txt:916-921` names only `external` and `src/renderer/generated`; the
shell reaches those headers because its own sources live in that directory. The
conclusion is unchanged, for the smoke's sake.)*

Includes: `<cstddef> <cstdint> <optional> <string> <string_view> <vector>`, plus
`marrow/editor/project.hpp`, `marrow/editor/selection.hpp`,
`marrow/runtime/json.hpp`, `marrow/runtime/skeleton.hpp`, and a forward
declaration of `EditorSession` — **not** `marrow/editor/session.hpp`, so the
pure collector does not drag the session into every consumer.

### 3.2 `src/editor/diagnostics.cpp` — **new**

The collector. Includes `mesh_weight_model.hpp` (a `src/editor/` header, legal
because this file is compiled *into* `marrow_editor`) and
`marrow/editor/session.hpp` for the wrapper. **Every** switch over
`DiagnosticSeverity` and `DiagnosticPanel` lives here (§1.6).

Named, so the reviewer has a shape to check against:

```
escape_identity_token()          join_identity()            make_issue()
collect_orphan_animation_overlays()      // F1, seven families
collect_orphan_weight_targets()          // F2
collect_weight_canonicality()            // F4, skipped for an F2 target
collect_stale_preview_references()       // F3, both halves
finalize()                               // sort, de-duplicate, count severities
collect_project_diagnostics()            // pure
collect_session_diagnostics()            // wrapper
```

### 3.3 `include/marrow/editor/project.hpp`, `src/editor/project.cpp`

One declaration and one definition: `authored_animation_names` (§2.5). Declared
beside `build_project_runtime_document` (`project.hpp:1164`); defined in
`project.cpp` **outside** the anonymous namespace but positioned so
`apply_animation_edits` (`:5423`) is already visible — i.e. after `:6862`. **No
other line of `project.cpp` changes.**

### 3.4 `src/editor/agent_handlers_inspection.cpp`

The `op == "project.diagnostics"` branch (`:342-355`) is rewritten to call
`collect_session_diagnostics(session)` and serialize the report. The four legacy
members keep their exact names, types and expressions; `issues` and `issue_count`
are added. On `std::nullopt` it returns `make_error(…, "diagnostics_unavailable")`,
following `runtime.validate`'s shape. **No other operation is touched.**

### 3.5 `src/samples/editor_project_smoke.cpp`

`validate_mar186_project_diagnostics(const ProjectLoadResult&)`, registered in
`main()`'s marker-gated `else` after MAR-185's suite — which is after
`validate_mar185_inherit_editing()` — registered at `editor_project_smoke.cpp:16282`
at `5a56663`, itself immediately after `validate_mar184_inherit_overlays` at
`:16279`. Cases
G0-G12 (§6.2).

### 3.6 `src/samples/agent_dispatch_smoke.cpp`

Cases A1-A5 (§6.3), plus one local helper `array_member`, following `member`'s
shape (`:115-119`). The second-project cases follow the
`exercise_parameter_operations` pattern (`:619-956`): `marrow_editor_project_load`,
`harness.set_project`, assertions, `marrow_editor_project_destroy`. **The array
bound at `:39` and every existing assertion are untouched.**

### 3.7 `tools/mcp/`

`tools/mcp/test_client.py` gains case M1 (§6.4). `tools/mcp/tools/inspection.py`'s
`project.diagnostics` **description** is updated to mention structured issues;
its `inputSchema` stays `{"type": "object", "properties": {}}` — the operation
takes no arguments and gains none. **No tool is added or removed**, so the two
`== 64` assertions at `:53,55` (66 after MAR-185) are untouched.

### 3.8 `CMakeLists.txt`

One line: `src/editor/diagnostics.cpp` in `add_library(marrow_editor STATIC …)`
(`:499-520`), placed after `src/editor/mesh_weight_model.cpp`. **No new target, no
new test, no new include directory.**

### 3.9 Documentation

- `docs/root1/agent-control.md:70` — the `project.diagnostics` line gains the new
  payload members. `:28` is a list of operation names and does not change.
- `AGENTS.md` — a `## MAR-186 …` validation section, and one clause on the
  existing `marrow_project_smoke` line under `## Current Validation`.
- `.agents/tasks/prd-marrow-runtime.json` — `MAR-186` → `done`.
- **No `docs/root1/format-spec.md` change**: nothing about the file format moves.

---

## 4. Acceptance criteria → artifacts

| AC | Artifact | Proof |
| --- | --- | --- |
| **AC1** — stable code, severity, message, typed target, optional safe-fix ID; deterministic ordering and identity across unchanged revisions | `DiagnosticIssue` (§2.2); identity from coordinates only, escaped (§2.2); sorted + de-duplicated (§2.3) | G1 (the exact sorted identity list, strictly increasing), G3 (identity is position-independent), G4 (two collections at one revision are identical), G12 (`\|` in a name does not collide) |
| **AC2** — UI-free, read-only, normally-opened sessions only; partial/degraded first-open and automatic correction excluded | `const&`-only signature (§2.1); `std::nullopt` for a non-opened session; no ImGui include | G0 (include gate + zero-mutation snapshot), G11 (nullopt on a session with no project), and the fact that §1.2's classes cannot be opened at all |
| **AC3** — `project.diagnostics` preserves `error_count`, `warning_count`, `project_dirty`, `review_queue_count` while adding `issues` and `issue_count` | §2.4; `agent_handlers_inspection.cpp` | A1 (all six members present, four legacy ones by name and type), A2 (`warning_count` is numerically the legacy value on a clean fixture, dirty and not), M1 over the wire |
| **AC4** — orphan overlays, non-canonical weights, stale preview references, safe-fix IDs only where applicable | F1/F2 (§2.5), F4 (§2.6), F3 (§2.7), the three-entry allowlist (§2.8) | G1, G2, G5 (orphan animations); G9(e) (orphan weight targets); G7 (non-canonical); G8 (uncanonicalizable, asserted to carry **no** fix); G9(a-d) (stale preview); the allowlist invariant runs on every case |
| **AC5** — typed targets resolve through `SelectionSet` and panel-focus metadata, without mutating selection during collection | `DiagnosticTarget` (§2.9); `DiagnosticPanel`; §2.10's split | G6 (each target replayed through `SelectionSet::replace`, including the transposed-field trap), G0's snapshot; the selection half recorded as structural |
| **AC6** — project, agent, and MCP tests cover deterministic results, severity counts, legacy summary compatibility, typed targets, revision changes, zero-mutation inspection | Three suites (§6) | deterministic → G1/G3/G4; severity counts → G10/A3; legacy compat → A1/A2/M1; typed targets → G6; revision changes → G10; zero-mutation → G0/A5 |

---

## 5. Inversions, and the mechanism that makes each observable

The register with predicted failure texts, per-case attribution, and the
uniqueness demonstrations lives in the plan's §B. What belongs here is the
**mechanism** — why each mutation is invisible to everything except the case
named, because that is a claim about the code, not about the tests.

| Mutation | Why nothing else notices |
| --- | --- |
| Resolve orphan animations against the **materialized skeleton** instead of `authored_animation_names` | The phantom animations are *in* the materialized skeleton. The sweep runs, finds nothing, and the collector reports a clean project. Every count-only assertion passes. Only a case that authors a known orphan and asserts its **identity string** fails |
| Drop one of the seven families from the F1 sweep | The other six still fire. Only a case asserting all seven identities notices, and it notices by a **missing** entry, so the assertion must compare the whole sorted list, never a count |
| Drop the identity escaping | Only observable when a name contains `|` or `\`. No fixture in the tree has one; the case must author one deliberately |
| Derive identity from the vector index | Stable within one collection, and stable across two collections of an unchanged project. Only an insertion **before** the offending record moves it |
| Remove the sort | The output is still deterministic *for a given input order*, so a two-collection comparison passes. Only a case that authors the same two issues in two different vector orders fails |
| Remove the de-duplication | Needs a duplicate to exist. `validate_project_for_save` refuses most; `preview_skins` does not, so the duplicate-skin case is the one that bites |
| Compare weight vertices with a tolerance instead of `==` | A vertex perturbed by exactly one ULP is then "canonical". The case must perturb by something a tolerance would swallow |
| Emit `""` instead of omitting an absent `safe_fix_id` | Every C++ assertion on `issue.safe_fix_id.empty()` still passes — the C++ field **is** empty. Only the **wire** case, asserting the key is absent from the JSON object, fails |
| Build `AttachmentSelection` from `MeshWeightAttachmentEdit` in field order | Skin and slot are transposed (§2.9). Counts, codes, severities and identities are all still right. Only a case that reads `selection.slot_name` and `selection.skin_name` **by name** fails |
| Emit `project.unsaved_changes` from the **pure** collector | The session wrapper's counts are unchanged, so every agent-layer assertion passes. Only a pure-collector case asserting an exact issue list on a dirty session's project fails |
| Count severities before appending `project.unsaved_changes` | `warning_count` is then one short on every dirty project — but only where a dirty project is actually inspected |
| Skip the F2 supersession, or apply it to the wrong edit | Produces one extra issue, or one fewer. Only a case with an orphan weight target that *also* has non-canonical vertices distinguishes them |

### 5.1 Deliberately uninverted, by name

No inversion is invented for these, and the reason is recorded per item so the
absence is a decision rather than a gap:

- **The `std::nullopt` branch of `collect_session_diagnostics` is unreachable
  through the agent.** `ensure_project_loaded` (`agent_dispatch.cpp:531-534`)
  rejects first. It is reachable from C++ (case G11 constructs a session with no
  project), and G11 is its only detector. The `base_skeleton_document() == nullptr`
  sub-condition is **not** separately reachable at all — `open` sets the document
  and the runtime data together — and is recorded as defensive, mirroring
  `runtime.validate`'s own unexercised null-document branch.
- **`diagnostic_panel_name` and `diagnostic_severity_name` are total by
  construction** over three and two values respectively. There is no mutation
  that breaks one without breaking a message or a payload string that some case
  already asserts.
- **`review_queue_count`'s expression** is copied verbatim from the shipped
  branch and is already covered by `agent_dispatch_smoke.cpp:3881-3885`. MAR-186
  adds no inversion for a guard it did not write.
- **`player_idle.marrow`'s zero-issue witness (G0's byte-identity half)** is a
  non-effect assertion. There is no mutation of MAR-186's own code that makes an
  unchanged project serialize differently without failing something louder first.
- **The `-Werror=switch` option (§2.12)** is deferred, so it has no inversion. It
  is recorded in `AGENTS.md` as a deferred mitigation, not as coverage.

---

## 6. Test surfaces

### 6.1 Which binaries, and why no fourth

| Binary | Runs | Why |
| --- | --- | --- |
| `marrow_project_smoke` | G0-G12 | It links `marrow_editor` and can call the pure collector directly with an in-memory `ProjectData`. Every case runs **inside the standing `player_idle.marrow` invocation** — see below |
| `marrow_agent_dispatch_smoke` | A1-A5 | AC3 and AC6's "agent tests". It reaches `project.diagnostics` over the C ABI, which is the only way to see the JSON shape |
| `tools/mcp/test_client.py` | M1 | AC6's "MCP tests", over the real socket |

**No new binary and no new CTest target.** A `marrow_diagnostics_tests`
executable would be the natural home for a pure model function, and it is
deliberately not created: it would move `ctest -N` from its Task 0 value, and
`marrow_project_smoke` already links `marrow_editor` and already hosts every
comparable pure-model suite (MAR-177, MAR-178, MAR-184).

**The project smoke has exactly one invocation, not two.** Every editing suite
sits inside `main()`'s marker-gated `else`. At `5a56663` the gate is
`editor_project_smoke.cpp:16134`, and the two registered suites nearest the tail
are `:16279` and `:16282`. (Before MAR-185 the gate was `:15303`; that drift
inside one story is the argument for the plan's Task 0.1.) The shape is
unchanged: a `--create` arm, the load, a **skip** branch, a partial-match
**abort**, then the `else`. Pointing the binary at a project built over another
skeleton takes the **skip** branch and runs nothing while still exiting 0. G0-G12
therefore build their throwaway projects **inside** the standing invocation, as
MAR-177, MAR-178 and MAR-184 already do. No new command line is added; the
existing `AGENTS.md` line gains a clause.

The agent smoke has **no** such gate: `main()` loads `argv[1]` or
`player_idle.marrow` (`:962-968`), and `exercise_parameter_operations`
(`:619-956`) already demonstrates loading a **second** project with
`marrow_editor_project_load` + `harness.set_project` + `marrow_editor_project_destroy`.
A3-A5 follow that pattern.

### 6.2 Project cases — `validate_mar186_project_diagnostics`

**The table is in execution order, and the order is load-bearing** — the plan's
§B attributes each mutation to the *first* case that catches it, so moving a case
invalidates an attribution.

Fixtures are built with `create_minimal_project` over
`assets/fixtures/player_idle.mskl` (sixteen bones, skins `default`, `mage`,
`mage_arm`, `mesh_base`, `warrior`, animations `aim`, `attack`, `idle`) into a
scratch directory, then mutated in memory. Round trips go through
`save_project` → **`load_project(path)`**, never through a bare `save()`:
`validate_project_for_save` (`project.cpp:5825`) takes no base document and
materializes nothing, so a passing save proves nothing about what the runtime
will do.

| # | Case |
| --- | --- |
| **G0** | **The non-effect and zero-mutation witness.** Over the `result` `main()` already holds for `player_idle.marrow`: `collect_project_diagnostics` returns **zero** issues, `error_count == 0`, `warning_count == 0`. Then, over a real `EditorSession` opened on the same file: snapshot `serialize_project`, `dirty()`, `undo_count()`, `redo_count()`, and all three revisions; call `collect_session_diagnostics`; assert all seven bit-identical, and the serialized bytes byte-identical. Print the byte length and SHA-256 every run |
| **G1** | **All seven orphan families.** One orphan overlay in each of the seven vectors, all naming animation `ghost`. Assert **exactly seven** issues, each `overlay.orphan_animation`, each `Error`, each `safe_fix_id == "remove_orphan_overlay"`, and the **full sorted identity list** equal to the seven expected strings. Assert `identity[i] < identity[i+1]` strictly. A missing entry is reported **by name**, not as a count mismatch |
| **G2** | **`authored_animation_names` is the authority.** G1's project, with an `AnimationEdit{Create, "ghost"}` added. Assert **zero** issues — the overlay is now legitimate. Then a `{Create, "ghost"}` followed by `{Delete, "ghost"}`; assert the seven issues return |
| **G3** | **Identity is position-independent.** Two projects carrying the same single orphan transform overlay, one with the orphan first in `transform_timeline_edits` and one with it last after two legitimate overlays. Assert both lists are **non-empty**, equal to each other, and equal to the one expected identity |
| **G4** | **Identity across unchanged revisions.** Collect twice over one unchanged project; assert the two `issues` vectors are equal member-by-member — code, severity, identity, message, panel, selection, safe fix |
| **G5** | **Round trip.** G1's project through `save_project` → `load_project(path)`; assert the seven identities survive byte-for-byte, and that the materialized skeleton **does** contain `ghost` — the resurrection F1 exists to report |
| **G6** | **Typed targets.** For G1's seven issues plus an F2 issue: assert each `target.panel`; feed each present `target.selection` to `SelectionSet::replace` and assert `active_bone()->bone_name` / `active_slot()->slot_name` / the three `AttachmentSelection` fields **by name**; assert `draw_order` and `event` carry no selection; assert every `animation_name` is `"ghost"` |
| **G7** | **Non-canonical weights.** Build a `MeshWeightAttachmentEdit` for `mesh_base`/`body`/`body_mesh` with `mesh_weight_edit_from_runtime`, then perturb vertex 0 two ways in two projects: weights scaled to sum `0.7`, and the influence order reversed. Each yields exactly one `weights.non_canonical` Warning with `normalize_weights`, identity ending `|0`. A third project leaves the edit untouched and yields **zero** issues |
| **G8** | **Uncanonicalizable weights, and the absent fix.** Vertex 0's influences replaced by a single `{spine, 1e-9}`. Assert one `weights.uncanonicalizable`, severity **Error**, `safe_fix_id` **empty**, and the message containing the canonicalizer's own text `"A weighted vertex must keep at least one positive influence."`. Then save → **load** and assert both gatekeepers let it through and the issue survives the round trip |
| **G9** | **Stale preview references, and the supersession.** (a) `active_animation = "ghost"` with no overlay → one `preview.stale_animation`, `reset_preview_reference`, panel `Project`. (b) `active_animation = "ghost"` **plus** G3's orphan overlays → the animation resolves in the materialized skeleton, so **no** `preview.stale_animation` (§2.7's interaction, asserted). (c) `preview_skins = ["default", "ghost_skin", "ghost_skin"]` → **one** `preview.stale_skin`, not two. (d) `active_animation = ""` → no issue. (e) An orphan weight target whose vertices are also non-canonical → **one** issue, `overlay.orphan_weight_target` (§2.6's supersession) |
| **G10** | **Severity counts and revisions.** A project carrying one Error and one Warning: assert `error_count == 1`, `warning_count == 1`, `issues.size() == 2`. Over a session: assert `project_revision` and `runtime_revision` on the report equal the session's; make one edit; assert the report's `project_revision` **advanced** and the issue list changed as expected |
| **G11** | **A session that is not normally opened.** A default-constructed `EditorSession`; assert `collect_session_diagnostics` returns `std::nullopt` |
| **G12** | **The escaping hazard.** Two orphan overlays whose **animation** names are chosen so that unescaped joining collides — e.g. `ghost\|x` on one bone and `ghost` on another whose remaining tokens reproduce `\|x`. The collision must be built on the *animation* name, not a bone name: §1.2's parser rules mean an overlay's bone must exist in the skeleton, and no fixture bone contains a `\|`, whereas an **orphan** animation name never has to resolve. Assert a **count of two** and two distinct identities — under the mutation, de-duplication *deletes* one issue rather than duplicating it |

Every case additionally runs the two invariants: **every non-empty `safe_fix_id`
is allowlisted**, and **identities are strictly increasing**.

### 6.3 Agent cases — `agent_dispatch_smoke.cpp`

| # | Case |
| --- | --- |
| **A1** | **Legacy shape.** `project.diagnostics` on `player_idle.marrow` returns `error_count` (number), `warning_count` (number), `project_dirty` (bool), `review_queue_count` (number), plus `issue_count` (number) and `issues` (array). Assert `issues` is **empty** and `issue_count == 0` (§1.8) |
| **A2** | **Legacy numeric compatibility.** Assert `warning_count == (project_dirty ? 1 : 0)` and `error_count == 0` both **before** any edit and **after** an edit that dirties the session — the two states the shipped behaviour had |
| **A3** | **A project with real issues, over the wire.** A throwaway `.marrow` written beside `/tmp`, carrying one orphan transform overlay, one non-canonical weight vertex, and one stale preview skin, loaded with the `exercise_parameter_operations` pattern. Assert `error_count == 1`, `warning_count == 2 + (dirty ? 1 : 0)`, `issue_count == issues.size()`, and each issue object's `code`, `severity`, `identity`, `safe_fix_id` presence, `target.panel`, and `target.selection.kind` |
| **A4** | **The absent safe fix is an absent key.** In A3's project, one `weights.uncanonicalizable` issue: assert `json::find_member(issue, "safe_fix_id") == nullptr`, not that it is an empty string |
| **A5** | **Inspection dirties nothing.** Around three consecutive `project.diagnostics` calls on A3's project, assert `project_dirty`, `issue_count`, and the full `issues` array are identical each time, and that a following `runtime.validate` still passes |

`review_queue_count`'s existing assertions at `:3881-3890` are **not** modified;
they become MAR-186's regression witnesses that the legacy members did not move.

### 6.4 MCP case — `tools/mcp/test_client.py`

**M1.** After the existing `require_ok("project.diagnostics", …)` at `:482`, keep
that line and assert the returned `scene_delta` carries all six members with the
right Python types, that `issue_count == len(issues)`, and — with a comment in the
file explaining why, mirroring the MAR-179 comment at `:57-70` — that the two
registry-count assertions at `:53,55` are **unchanged**, because MAR-186 adds no
operation and no tool and therefore cannot be seen by them.

Add a second, schema-level assertion: `inspection.get_tools()` still contains a
`project.diagnostics` tool whose `inputSchema` declares **no** properties. A wire
call carrying arguments would succeed regardless — `MarrowClient.send_command`
never consults `inputSchema` — so a sequence test alone cannot see a schema that
grew a bogus argument.

---

## 7. Verification commands

```bash
cmake -S . -B build && cmake --build build

./build/marrow_project_smoke assets/fixtures/player_idle.marrow      # G0-G12
./build/marrow_project_smoke --create /tmp/mar186_created.marrow
./build/marrow_project_smoke assets/fixtures/player_idle.marrow \
  --export-runtime /tmp/player_idle_project_export.mskl \
  --export-binary  /tmp/player_idle_project_export.mbin
./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin \
                                 /tmp/player_idle_project_export.mskl

./build/marrow_agent_dispatch_smoke                                  # A1-A5

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

MARROW_CONFIG_HOME=/tmp/mar186-cfg ./build/marrow_editor_shell \
  --project assets/fixtures/player_idle.marrow --auto-close 2

ctest --test-dir build -N
ctest --test-dir build --output-on-failure
ctest --test-dir build --output-on-failure -L runtime
ctest --test-dir build --output-on-failure -L editor
ctest --test-dir build --output-on-failure -R marrow.renderer_link_boundary
cmake --build build --target marrow_verify_third_party
cmake --build build --target marrow_constraint_warning_check

./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876 &
tools/mcp/venv/bin/python tools/mcp/test_client.py                   # M1
python3 -m py_compile tools/mcp/server.py tools/mcp/test_client.py \
                      tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
```

Never run `./build/marrow_editor_shell` without `--auto-close`, and
`~/Library/Application Support/Marrow` must be **ABSENT** after the whole run.

---

## 8. Non-goals

- **No Problems view, no navigation, no focus, no fix.** All MAR-187.
- **No automatic correction, ever.** AC2 excludes it and MAR-187's AC5 repeats it
  ("no automatic fix on project open"). The collector has no mutating entry point
  at all, which is the strongest form of that guarantee.
- **No first-open or degraded diagnostics.** AC2 excludes them and §1.2 shows the
  parser already made them unrepresentable.
- **No new agent operation, no new MCP tool, no registry movement.**
- **No format change.** `.marrow` gains no key; `.mskl` stays 1; `.mbin` stays 2.
- **No shell or frame coverage.** MAR-186 adds no drawing code, so there is no
  pixel to assert. `AGENTS.md`'s Headless Frame Smoke Notes are not engaged.
- **No repair of the classes §1.2 makes unopenable.** A project whose overlay
  names a missing bone still fails to open with the runtime's message, and MAR-186
  does not soften that.
- **No `-Wswitch`.** §2.12; deferred with the exact recipe recorded.

---

## 9. Facts to MEASURE in Task 0, not assume

| # | Fact | How |
| --- | --- | --- |
| **G-a** | **Every `file:line` in this document, re-resolved against the tip.** MAR-185 and any MAR-184 review pass will have moved most of them | The plan's Task 0.1 gate. A citation that no longer names its described construct is a **blocking finding** |
| **G-b** | `apply_animation_edits` is still in `project.cpp`'s anonymous namespace, and `authored_animation_names` therefore has to live in `project.cpp` | `grep -n "^namespace\|^} // namespace" src/editor/project.cpp` and check that `apply_animation_edits`' line falls inside the first `namespace {` block |
| **G-c** | The **four** hard parser rules of §1.2 still exist | `grep -n "animation references unknown bone\|animation references unknown slot\|mesh weight references unknown bone" src/runtime/skeleton_parse.cpp` → **four** hits (`1616, 5270, 5373, 5430`). Two of them carry the same message text, from the `slots` walk and the separate `deform` walk — E10, and the reason the first draft undercounted |
| **G-d** | **Canonicalization is a bit-exact fixed point.** §2.6's `==` comparison depends on it | Canonicalize a fixture vertex twice; assert the second pass returns byte-identical influences. **If it does not, §2.6 is wrong and the story stops** |
| **G-e** | A lone `{spine, 1e-9}` influence survives `parse_mesh_weight_vertices`, `validate_project_for_save` **and** the runtime, so G8 is reachable | Write it, save, `load_project(path)`, assert success. **If the load fails, G8 moves to an in-memory case and AC4's Error class is recorded as unreachable from a file** |
| **G-f** | `player_idle.marrow` yields **zero** issues | G0 prints the count every run |
| **G-g** | The registry count and its thirteen witnesses, whatever MAR-185 left them at | `grep -c '^\s*{\s*"' src/editor/agent_dispatch.cpp`, the array bound, `grep -rn "!= 6[46]U" src/ \| wc -l`, `grep -n "== 6[46]" tools/mcp/test_client.py`. Recorded as the **non-effect** baseline |
| **G-h** | `ctest -N` total | **22** at `6986024`. Must be identical at the end |
| **G-i** | Nothing else in the tree defines **this** diagnostics vocabulary | `grep -rnw "DiagnosticSeverity\|DiagnosticPanel\|DiagnosticCode\|DiagnosticOverlayFamily\|safe_fix_id\|issue_count" src/ include/ tools/` → expect **0** before Task 1. **`-w` is load-bearing**: `SpineImportDiagnosticSeverity` already exists in `src/runtime/spine_import.cpp` (ten-plus uses), so an unanchored grep can never return zero and the gate as first drafted could not pass |
| **G-j** | **`-Wswitch` fires with no flags**, so exhaustive switches over the new enums are policed | `printf 'enum class E{A,B,C};\nint f(E e){switch(e){case E::A: return 1; case E::B: return 2;} return 0;}\n' > /tmp/wsw.cpp && c++ -std=c++17 -c /tmp/wsw.cpp -o /tmp/wsw.o` → expect `[-Wswitch]`. **Compile it; do not infer it from `grep`ing `CMakeLists.txt` for flags** — that inference is E9 |
| **G-k** | `AttachmentSelection`'s field order is still `slot_name, skin_name, attachment_name` while `MeshWeightAttachmentEdit`'s is `skin_name, slot_name, attachment_name` | Read both. §2.9's trap depends on the transposition being real |
| **G-l** | The marker gate's current line range and the insertion point after MAR-185's suite | `grep -n "markers.present.empty()" src/samples/editor_project_smoke.cpp` |

---

## 10. Known limitations, stated rather than hidden

- **The largest orphan class is unreachable.** An overlay naming a missing bone
  or slot, or a weight influence naming a missing bone, makes the project
  **unopenable** (§1.2). MAR-186 reports none of them, by AC2. The user's
  experience of that class is the runtime's load error, unchanged. Anyone reading
  "the collector reports orphan overlays" should read §1.3's F1/F2 for what that
  actually covers.
- **Only the project layer is inspected.** Runtime-only problems — a constraint
  that cannot converge, an atlas page that failed to load — are `runtime.validate`'s
  and the asset watcher's business. MAR-186 adds no runtime diagnostics.
- **`weights.uncanonicalizable` has no repair.** It is reported and left. A user
  must edit the vertex by hand or delete the overlay.
- **The F2 supersession hides real weight problems** on an orphaned edit until the
  orphan is fixed (§2.6). Deliberate; MAR-187's refresh surfaces them afterwards.
- **Fixing an orphan overlay can create a stale-preview issue** (§2.7). Deliberate
  and documented; MAR-187's revision-keyed refresh is what makes it visible.
- **The selection half of AC5 is proved structurally, not by assertion.** §2.10.
  Recorded under H4's rule rather than papered over with a vacuous case.
- **`collect_session_diagnostics`' `nullopt` branch is unreachable through the
  agent** and its `base_skeleton_document() == nullptr` sub-condition is
  unreachable at all (§5.1).
- **`-Wswitch` warns but does not stop the build.** MAR-186 introduces four
  enums; Clang reports a missed exhaustive-switch arm by default (§1.6), and
  MAR-186 does **not** promote that to `-Werror` (§2.12), so a missed arm is a
  warning in the log rather than a failure. **GCC needs `-Wall` for the same
  diagnostic**, so a non-Clang build is silent — the recipe in §2.12 is guarded on
  compiler id for that reason.
- **No compiler checks `collect_orphan_animation_overlays`' seven family calls.**
  An eighth `ProjectData` overlay vector added without an eighth call produces no
  diagnostic and no failure from any shipped case. This is the class MAR-185
  shipped a defect in (`clipboard_track_count`); §2.5's hand-written guard and its
  comment are the only thing standing there.
- **Nothing is persisted.** A diagnostic is recomputed on demand. A project that
  is opened, inspected, and closed leaves no trace of what was found — which is
  also what makes AC2's "read-only" total.
- **Carried forward, unchanged by this story:** `shell_main.cpp`'s frame body is
  still reachable from no test; `commit_path_choice` still has zero end-to-end
  coverage; the runtime still accepts a negative first inherit key time and inert
  `curve` data on an inherit key; the empty-edit hazard is still fixed only for
  the inherit family.

---

## 11. Risks for the implementer

| # | Risk | Mitigation |
| --- | --- | --- |
| **R1** | **Starting before MAR-185 lands**, or assuming its symbols exist | §0.2's table. MAR-186 consumes no MAR-185 *code*; the plan's Task 0 records what actually shipped |
| **R2** | **Every cited line has moved.** Two stories of drift | The plan's Task 0.1 re-anchor gate. Symbol first, line as a hint, non-resolution is blocking |
| **R3** | **Resolving orphans against the materialized skeleton.** The single most likely implementation error, and it makes every test pass | §2.5. `authored_animation_names` is the authority; G1 catches the substitution and G2 proves the authority is the *fold*, not the base document |
| **R4** | **Asserting `issue_count` instead of identities.** The MAR-186 shape of MAR-184's I2b lesson: many causes produce "some issues", so a count assertion passes for the wrong reason | Every case asserts the **full sorted identity list**. No case asserts only a count |
| **R5** | **A fixture whose issue would fire for a different reason.** The MAR-186 shape of the `ensure_*`-on-an-existing-edit trap | G7's control project (untouched edit → zero issues) and G9(e)'s supersession case. Every fixture states in a comment what makes it load-bearing |
| **R6** | **`AttachmentSelection`'s transposed field order** (§2.9) | G6 asserts the three fields by name; G-k re-measures the transposition |
| **R7** | **Pointing `marrow_project_smoke` at a new fixture** takes the skip branch and runs nothing while exiting 0 | §6.1. Build throwaway projects **inside** the single invocation |
| **R8** | **A passing `save()` proving nothing.** `validate_project_for_save` takes no base document and materializes nothing | Every round trip goes through `save_project` → `load_project(path)` |
| **R9** | **An inversion result that lies (H1-H4).** MAR-184 recorded nine false "did not bite" readings from `touch` alone | **Delete the object file** before every verification build; `cmp` over whole recorded strings; neuter earlier detectors to prove uniqueness; final run from `rm -rf build` |
| **R10** | **Moving a registry count site by accident** | The count sweep is a **non-effect** gate: zero `\b6[3-7]\b` lines in the diff over `src/` and `tools/` |
| **R11** | **A silent sweep later — but not of the enums.** Clang's default `-Wswitch` names every missed exhaustive-switch arm (§1.6, E9). What nothing checks is `collect_orphan_animation_overlays`' **seven-call family list** — MAR-185 shipped `clipboard_track_count` into exactly that class | Name functions are exhaustive `switch`es with **no `default:` arm**, so the compiler is not silenced; every switch lives in `diagnostics.cpp`; §2.5's `static_assert` guard sits beside the call list; G1 asserts all seven identities. §2.12 records the `-Werror=switch` promotion, guarded on compiler id because **GCC needs `-Wall`** for the same diagnostic |
| **R12** | **Identity collisions from unescaped names** | §2.2's escaping and G12. De-duplication makes a collision *delete* an issue, which is worse than a wrong string |
