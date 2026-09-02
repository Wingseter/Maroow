# MAR-190 Add the PSD Reimport Review UI — Design

Story: **MAR-190 — Add the PSD reimport review UI**
Depends on: **MAR-189** (open, *being planned in parallel and not implemented*),
which depends on MAR-188 (`71465db`), which depends on MAR-187 (`f3a3768`).
Date: 2026-09-01. Written against the tree at `cf6a199`; **HEAD moved to `23b326e`
during writing** (MAR-188's review fixes plus the inversion-harness guard), and
every claim below was re-checked against `23b326e` before this document was
finished. Task 0 re-derives all of them at whatever HEAD is then — `AGENTS.md`
records that a stale line anchor merely fails to resolve while a stale *restore
target* resolves perfectly and destroys work.

Companion plan: `docs/superpowers/plans/2026-09-01-mar-190-psd-reimport-review-ui.md`.

---

## 0. Where this story sits

### 0.1 The story in one sentence

MAR-188 produced a reviewable plan and wrote nothing outside a staging root;
MAR-189 will turn a plan into a journalled, rollback-complete commit. MAR-190 is
the **human decision point between them**: a modal that shows where the art came
from and exactly what would change, lets the user tick which vanished layers to
delete, and either commits *those* choices or leaves the project, the runtime,
the files, the selection and the history byte-for-byte as they were.

### 0.2 MAR-189 is a hard dependency and **none of it exists yet**

> **Superseded — see §0.6.** MAR-189 landed at `7462f67`. This subsection is kept
> as the record of what was true when the document was written; every signature
> below was re-derived at `ac82f3c` and items 1–4 of §0.5 are all present. The
> tense in this subsection is historical, not a present-tense claim about the tree.

Everything MAR-190 calls on the commit side is **prospective**. At the time of
writing, `docs/superpowers/specs/2026-09-01-mar-189-atomic-psd-reimport-commit-design.md`
is untracked (`git status --porcelain` shows it as `??`) and was **modified on
disk while this document was being written**. `src/editor/psd_reimport_commit.cpp`,
`include/marrow/editor/psd_reimport_commit.hpp` and
`src/editor/psd_reimport_commit_internal.hpp` do not exist. `grep -rn
"commit_psd_reimport" src include` returns nothing.

Every reference below to `commit_psd_reimport`, `PsdReimportCommitResult`,
`PsdCommitStep`, the failpoint seam, `staged_texture_path` or a plan digest is
therefore written in the **future tense on purpose**, and §0.5 lists exactly what
must land first. Task 0 re-derives every one of them against the tree as it is
*then*; a signature that does not resolve is a blocking finding, recorded in the
errors table, never silently adapted around.

### 0.3 The centre of gravity is AC2 and AC3

**AC2** — *"Missing-layer deletion checkboxes default off, so missing layers are
preserved unless the user explicitly selects them for deletion."* This is a
statement about a **default**, and a default is the single easiest thing in a
codebase to invert without any test noticing, because the natural assertions
("the commit succeeded", "two layers were deleted") are all compatible with the
flag being wrong. So every deletion assertion in this story compares the **full
set of chosen identities**, reported as a sorted set difference, and never a
count. `AGENTS.md` states the general rule; here it is load-bearing twice over,
because MAR-188 already ships `PsdPlannedLayer::preserve{true}`
(`psd_reimport_plan.hpp:45`) and its own I15 inverted exactly that default.

**What AC2's word "preserved" can mean is not what this document originally
assumed, and Task 0 settled it** (§0.6 T1, §2.3): `preserve` governs the layer's
**stored provenance row**, not its slot or attachment, which the reimport removes
in either direction. AC2 is still satisfiable and still falsifiable — the
observable is typed and compared as a full set — but the artefact it is asserted
against moves, and §2.8 changes the checkbox label so the UI cannot imply the
stronger promise. Read §2.3 before writing D1 or D2.

**AC3** — *"Cancel, modal close, stale-plan detection, planning failure, and
commit failure leave the project, runtime source, files, selection, and history
unchanged."* Five distinct entry paths into **one** invariant. The design's
answer is a single helper, `expect_reimport_no_op()`, asserting the whole
invariant at once, called from five cases (§5.3). Five partial assertions would
let each path prove a different subset and none prove the invariant.

### 0.4 Errors and imprecisions found in the incoming brief and in the source documents

Recorded rather than silently corrected, per `AGENTS.md`'s rule that a correction
naming an artefact must be resolved in the artefact.

| # | Where | Claim | Finding |
|---|---|---|---|
| **G1** | The brief | *"`cmake/CheckFrameBodies.cmake` … compares them **as sets**, so any window MAR-190 adds must appear in **both** bodies or the build breaks."* | **True as stated, and not the constraint this story is under.** The extractor's regex is `draw_[a-z_]+windows?\(` (`cmake/CheckFrameBodies.cmake:60`). A review **modal** — which is what AC1 and AC3 describe, and what every comparable surface in this tree already is (`draw_file_path_modals`, `draw_animation_catalog_popups`, `draw_constraint_catalog_popups`) — matches nothing and is invisible to the gate in **both** directions. MAR-190 consequently adds **no** `draw_*_window` call, the gate's set does not move, and §2.9 replaces it with a guard that can actually fail. |
| **G2** | The brief | *"a default `Selectable` spans the whole content region, pushing a `SameLine()` button past the window's right edge"* | **Correct, and already repaired in the tree.** `src/editor/shell_problems.cpp:169-179` sets `label_width = max(40.0f, GetContentRegionAvail().x - fix_column)` with the measurement in a comment. MAR-190 inherits the *technique*; it does not inherit the bug. Because "we used the technique" is not an assertion, F2 additionally asserts the located checkbox's x lies **inside** the modal's right edge (§6.2), so the repair is measured rather than assumed. |
| **G3** | The brief | *"`tools/inversion/` is committed — plan to use it rather than ad-hoc shell."* | **Committed, and it changed underneath this story while this table was being written.** `git ls-files tools/inversion/` = `README.md invert.sh rebuild.sh snapshot.sh`. At `cf6a199`, `invert.sh:21,28` called `rebuild.sh "$build_dir"` with no object paths and `for o in "$@"` iterated an empty list, so the build fell back to make's mtime comparison — MAR-189's F6, and the exact hazard the harness exists to prevent. **That is now fixed and committed at `23b326e`**: `rebuild.sh` gained a no-argument branch deleting every object under `<build-dir>/CMakeFiles`, with the measurement in its own header (*"130 objects before, 130 after, 0 files recompiled"*). The consequence for MAR-190 is a **procedural** one, not a defect: `git archive <sha>` extracts whichever version that `<sha>` carried, so Task 0.7 measures the extracted tree rather than assuming, and supplies object paths explicitly if it got the pre-`23b326e` form. |
| **G4** | The brief | *"`create_minimal_project`'s `preserved_root` is an empty **object**, not null"* | **Right conclusion, wrong locus, and the correct locus is wider.** `create_minimal_project` (`src/editor/project.cpp`) never touches `preserved_root` at all. The empty object comes from the **member initialiser** — `runtime::json::Value preserved_root{runtime::json::Value::Object{}, {}};` at `include/marrow/editor/project.hpp:657` — so `is_null()` fails for **every** default-constructed `ProjectData`, not only for that factory's output. Same trap, larger blast radius. |
| **G5** | The brief | Registry **66**; bound in `src/samples/agent_dispatch_smoke.cpp` (*"re-derive its line … it measured `:42`"*); eleven `!= 66U` guards across three files; two `== 66` in `tools/mcp/test_client.py` | **All five re-derived; all five hold.** `grep -c '^    {"' src/editor/agent_dispatch.cpp` = **66**; `constexpr std::array<OperationExpectation, 66> kExpectedOperations{{` at **`agent_dispatch_smoke.cpp:42`**; guards split **7** `shell_smoke_graph.cpp` / **2** `shell_smoke_constraints.cpp` / **2** `shell_smoke_timeline.cpp` = 11; `test_client.py:53,55`. MAR-190 adds no operation and no MCP tool (§2.11), so this sweep is a **non-effect** gate and is labelled one. |
| **G6** | `AGENTS.md` / MAR-188 §7.1 prose | *"MAR-188's `invert.py` used absolute paths throughout and every one of its twenty restores is trustworthy for that reason"* | **No `invert.py` is tracked.** `git ls-files tools/inversion/` lists four files and none is `invert.py`. The committed harness is `invert.sh`, whose *fourth argument* is a per-inversion `mutation.py`; that is what the historical note describes. The absolute-path property is real and lives in `invert.sh` (`root="$(cd "$(dirname …)/../.." && pwd)"`, then `cp "$copy" "${root}/${file}"`). Recorded because a plan step reading "run `invert.py`" does not resolve. |
| **G7** | MAR-188 §8 / MAR-189 §8 | `ctest -N` = **22** | **True, and the arithmetic is not obvious.** `grep -c 'add_test(' CMakeLists.txt` returns **25**; three of them (`:1164`, `:1173`, `:1182`) sit inside `if(MARROW_ENABLE_DISPLAY_TESTS)` (`:1056-1102`). Recorded so Task 0 does not spend a cycle chasing a phantom three-test gap. |
| **G8** | The brief | *"F1-style frame cases back neither body, since they render through their own lambdas"* | **Confirmed from the gate script's own header and from MAR-187 §2.12's measurement**, which additionally records that a plain two-file `grep` *passed on a broken tree* because F1's lambda kept the symbol present. Adopted as a constraint, not repeated as a claim: §2.9 states plainly that MAR-190's frame case backs no draw list, and names what does. |
| **G9** | **Nothing in any document** | AC3 lists *"modal close"* as a path distinct from *"cancel"* | **Escape does not close an ImGui modal, so "modal close" cannot be the Escape key.** `NavUpdateCancelRequest` (`external/imgui/imgui.cpp:14844`) reaches its popup-closing arm only under `!(g.OpenPopupStack.back().Window->Flags & ImGuiWindowFlags_Modal)` (`:14873`), and `BeginPopupModal` sets `ImGuiWindowFlags_Modal` unconditionally at `:13103`. The path exists only if the modal is given a `bool*`: `BeginPopupModal(name, p_open, flags)` forwards it to `Begin` (`:13104`), which draws a title-bar close control, and calls `ClosePopupToLevel` when it goes false (`:13105-13109`). §2.6 takes that decision. **This is the one thing AC3 forces that no source document anticipates**, and it is a design decision, not a detail. |
| **G10** | The brief | *"MAR-189's pair … is being planned right now and **may not exist yet**"* | It exists as **untracked files** and changed on disk mid-session (§0.2). Everything quoted from it is prospective and re-derived in Task 0. |

### 0.5 What MAR-189 must deliver before MAR-190 can be implemented

Ordered by how badly MAR-190 is blocked without it.

1. **`commit_psd_reimport(EditorSession&, const PsdReimportPlan&, const PsdReimportCommitOptions&)` returning `PsdReimportCommitResult`** (MAR-189 §2.1). AC4 has no other satisfier. Blocking.
2. **The failpoint seam** in `src/editor/psd_reimport_commit_internal.hpp` (MAR-189 §3). AC3's *commit failure* path cannot be produced any other way without corrupting real files, and N5 is unwritable without it. Blocking.
3. **`preserve` actually consumed** — MAR-189's `PruneUnpreserved` step (§2.6). Without it AC2's "explicitly selects them for deletion" has no effect to observe and D2 degenerates into a witness. Blocking.
4. **`PsdReimportPlan::staged_texture_path` and the `staged_*_filename` options** (MAR-189 §2.2, its F4/F5). If the staged `.matl` still says `"image": "staged.png"`, MAR-190 commits a bundle whose texture does not resolve **and every byte comparison still passes**. Blocking for AC5.
5. **A header-declared plan digest.** MAR-189 §2.8 defines "a digest of the reviewed plan — the ordered row list", but its §4 landing table puts the implementation in `src/editor/agent_dispatch.cpp`. If it stays private there, MAR-190 must write a **second** hand-maintained field list over `PsdPlannedLayer` — precisely the fixed-length-list class `AGENTS.md` records as compiler-blind and which MAR-185 found three live defects in. **Requested: `std::string psd_plan_digest(const PsdReimportPlan&);` declared in `include/marrow/editor/psd_reimport_plan.hpp`.** Not blocking — §2.5 has a fallback — but the fallback is strictly worse and the plan says so.
6. **Not required:** `apply_agent_review`, the `Approve` button, or any MCP change. Those are the *agent* approval path. MAR-190's Confirm is the *editor* path and shares only `commit_psd_reimport`. Stated explicitly so the dependency stays as small as it actually is.

### 0.6 Task 0 findings — measured at `ac82f3c`, after MAR-189 landed

§0.2 and §0.5 were written while MAR-189 was prospective. **MAR-189 is now
implemented** (`7462f67`), plus a harness follow-up (`ac82f3c`), and Task 0
re-derived every claim against it. Items 1–4 of §0.5 are all present, so the
story is unblocked. Nine claims in this document and its plan did **not** survive
that check. Each is recorded here, and each is *also* repaired at its own site —
a row in this table alone would leave the wrong sentence standing where the
implementer reads it.

The tip moved twice during Task 0: `7462f67` → `ac82f3c`, the second as a **new
commit, not an amendment**. `git diff --name-only 7462f67 ac82f3c` touches
`AGENTS.md`, MAR-189's design, and `tools/inversion/{README.md,invert.sh}` —
**no source, no CMake, no assets** — so every build baseline measured on the
`7462f67` tree remains valid at `ac82f3c`.

| # | Where | Claim as written | Measured |
|---|---|---|---|
| **T1** | §2.3, §6.1 D1/D2, §7 I3 | `preserve` retains a `Missing` layer's slot and attachment | **False, and this is the story's most dangerous error.** `preserve` is read in exactly two places. `prune_unpreserved` (`psd_reimport_commit.cpp:463`) erases the slot from the staged skeleton — but against any plan the real importer produces it **never fires**, because `build_skeleton_document` assigns `(*root)["slots"]` wholesale (`psd_import.cpp:1040`) and `erase("skins")` (`:1041`), so the slot is already gone. MAR-189's own comment: *"`preserve` governs the stored provenance IDENTITY, not the art."* The sole observable is `provenance_from_plan` (`:251-287`), which keeps or drops the layer's `PsdLayerProvenance` row. **The slot is removed by the reimport either way.** §2.2a, §2.8, §6.1 and §7 are rewritten on this basis |
| **T2** | §2.4 | *"`apply_psd_reimport_review` is **the only function in the tree** that calls `commit_psd_reimport`"* | **False at HEAD.** `agent_dispatch.cpp:1264` calls it for the agent path — intended and separate per §0.5 item 6, but it breaks the plan's **S2** as worded. Real count is 17 (1 agent, 16 in MAR-189's own cases). S2 rewritten to scope by directory and exclude both owners |
| **T3** | §2.5 | The digest covers a **ten-field** row tuple | **Two fields.** `agent_handlers_management.cpp:82` emits `identity=change;`; `agent_dispatch_internal.hpp:203` documents it as *"The ordered `(identity, change)` row list"*. §2.5 and §9.2 rewritten |
| **T4** | §9.4, §10.1 | *"Reaching it needs a **second** injection seam that MAR-189 explicitly does not build"* | **MAR-189 built it.** `CommitRollbackFailpoint` + `set_psd_commit_rollback_failpoint_for_testing` (`psd_reimport_commit_internal.hpp`), with `rollback_advance` wired at five sites (`psd_reimport_commit.cpp:778`) and a `steps_rolled_back` ledger. **AC6 is satisfiable as worded**; §9.4's disclaimer is withdrawn and replaced by the seam's own narrower residual |
| **T5** | §2.11 | *"`marrow_project_smoke` **aborts** on a partial marker match (`editor_project_smoke.cpp:16144-16153`)"* | **Wrong locus and garbled mechanism.** No `abort()` exists in that file. The gate is `:20584-20604`, and it is **two** branches: no markers → *silent skip, exit 0*; partial match → `return 1`. This document fuses them and attributes "still exits 0" to the partial case; the source comment says that is the counterfactual *without* the branch. The conclusion (never edit the fixture) stands, understated |
| **T6** | §1.4 | `draw_constraint_catalog_popups` at `shell_constraints.cpp:863`, `:927` | Definition `:857`, **exactly one** call at `:2195`. The two-call-site precedent holds for `draw_file_path_modals` (`:722`/`:951`, both inside `draw_menu_bar`, comment verbatim) and `draw_animation_catalog_popups` (`:181`/`:318`), **not** for this one |
| **T7** | §2.8 | *"All four fields exist today (`project.hpp:596-600`)"* | `PsdImportProvenance` has **three** members |
| **T8** | Plan B9 / §6.4 W1 | The fixture byte map covers `player_idle.marrow` and its `.mskl`/`.matl`/`.png` | **`player_idle.png` does not exist.** The texture is `player_fixture.png`; `player_idle.mbin` also exists and was omitted. **W1 must assert each path exists before hashing** — a hash sweep over a nonexistent file is a check with nothing to check that reports success. This is the zero-result rule in a third costume, after the zsh glob and the malformed BRE interval |
| **T9** | Plan §B I14 | *"verify in Task 0: if the provenance edit necessarily moves the digest, this inversion is subsumed"* | **Subsumed by I5**, definitively. The digest *is* the identity list, so any way a chosen identity becomes absent or reclassified moves it, and §2.4 step 2 returns `Stale` before step 3's lookup runs — step 3's `Stale` arm is unreachable defensive code. **Recorded as subsumed; not run and reported as "did not bite."** The two look identical in a log and mean opposite things |

**Line drift only** — construct intact, anchor moved by MAR-189's ~5,700
insertions. Cite the **symbol**; these are recorded so the next reader does not
re-derive them: `shell_main.cpp:583`→`:584` (and the app frame-body set is
`:584-602`, not `:583-597`); `CMakeLists.txt` `:852-854`→`:854-856`,
`:954-962`→`:955-963`, `:1353-1367`→`:1387-1398` (`:1353-1367` now names
`marrow.agent_dispatch_smoke`), display-gated `:1164/:1173/:1182`→`:1165/:1174/:1183`,
`if(MARROW_ENABLE_DISPLAY_TESTS)` `:1056`→`:1057`; `psd_import_smoke.cpp`
`:22/:345/:607/:855`→`:30/:353/:615/:863` (uniform +8);
`shell_smoke_project.cpp:2427-2432`→`:2341-2343`. **G7's arithmetic**:
`grep -c 'add_test('` was 25, now **26**; `ctest -N` was 22, now **23**; 26 − 3
display-gated = 23, so the form holds and both numbers were stale.

**Re-resolved exactly**, because the story leans on them: `CheckFrameBodies.cmake:60`;
`shell_problems.cpp:169-179` and `:186-188`; `shell_state.hpp:1011-1025`;
`session.hpp:189-205` quoted verbatim; **`imgui.cpp:13103`, `:13104`,
`:13105-13109`, `:14844`, `:14873` — all five**, so G9's `bool*` requirement is
confirmed; `project.hpp:657`; `psd_import.cpp:734` and `:814-826`;
`shell_file_paths.hpp:56-58`; `project.cpp:6077` inside anon ns `:25-7114`;
`imgui_widgets.cpp:10033`; `psd_reimport_plan.hpp:45`;
`shell_project_panels.cpp:722`/`:951`/`:954`; `shell_smoke_frames.cpp:72`.

**MAR-189's deliverables, as shipped.** Items 1–4 present and usable; item 4 is
**better than requested** — `ValidateRequest` refuses the commit unless the staged
atlas's `image`, the staged texture's filename and the target atlas's `image` all
agree, so AC5's byte-comparison hole is closed by MAR-189 rather than left here.
Item 5 (the digest) is **not** header-declared in `include/`, but it is declared
in `src/editor/agent_dispatch_internal.hpp:203` in `namespace
marrow::editor::agent_detail`, and `agent_handlers_management.cpp` is in
`marrow_editor` (`CMakeLists.txt:524`) — so a new `src/editor/psd_reimport_review.cpp`
in the same target reuses it directly. **§2.5's fallback is not taken and no field
list is duplicated.**

**A methodology note that cost a cycle.** `grep -c` **exits 1 when the count is
zero**, so `cmake --build … | grep -c warning:` reports a *failed command* on a
clean build. That is the third distinct mechanism for the zero-result rule, after
zsh glob expansion and the malformed BRE interval. Capture the exit status of the
build itself, never of the counting pipeline.

---

## 1. The measured gap

### 1.1 Nothing in the editor can see a plan

`grep -rn "plan_psd_reimport" src/ include/` resolves to the declaration
(`include/marrow/editor/psd_reimport_plan.hpp`), the definition
(`src/editor/psd_reimport_plan.cpp`) and `src/samples/psd_import_smoke.cpp`. **No
shell source calls it.** There is no window, no menu item, no button and no
`ShellState` field through which a user can reach a reimport plan, and
`PsdPlannedLayer::preserve` is written by the planner and read by nobody.

### 1.2 The synthesiser is TU-local, and the shell cannot borrow it

MAR-188's PSD synthesiser lives inside `namespace mar188` nested in the anonymous
namespace of `src/samples/psd_import_smoke.cpp` (`:22` opens `namespace {`, the
`mar188` blocks are at `:345`, `:607`, `:855`). `marrow_psd_import_smoke` is its
own executable built from that single source (`CMakeLists.txt:852-854`), so
**`marrow_editor_shell` cannot link a line of it**.

This is a real constraint on where MAR-190's cases can live, and §5.1 answers it
without moving anybody else's code: the shell cases build their fixture with the
**real importer**, `import_psd_to_runtime_bundle`, which is public in
`marrow_editor` and which `marrow_editor_shell` already links
(`CMakeLists.txt:954-962`), over the tracked
`assets/fixtures/psd_import_sample.psd` copied into a temp directory. No
synthesiser, no extraction of another story's file, no PSD authored by hand.

### 1.3 The shell smoke's working directory is the repository

`marrow.editor_shell_smoke` runs `marrow_editor_shell --project
assets/fixtures/player_idle.marrow --auto-close 5` with `WORKING_DIRECTORY
${PROJECT_SOURCE_DIR}` (`CMakeLists.txt:1353-1367`). A committing case pointed at
a project inside the source tree **overwrites tracked assets**. MAR-189 records
the identical hazard for the agent smoke (its §9.4) and calls it the single most
likely way that story damages the repository; the same sentence is true here.
Every MAR-190 shell case therefore works entirely under
`std::filesystem::temp_directory_path()`, and **W1** (§6.4) asserts the tracked
PSD fixtures and `player_idle`'s bundle are byte-identical at the end of the run.

### 1.4 Modals in this tree are drawn from inside panels, never from a frame body

Three precedents, all measured:

- `draw_file_path_modals(state)` — called from `draw_menu_bar`
  (`src/editor/shell_project_panels.cpp:722` and `:951`, the two mutually
  exclusive branches), whose own comment records the property MAR-190 depends on:
  *"BOTH frame bodies reach them through their existing `draw_menu_bar` call
  without any frame-body edit."*
- `draw_animation_catalog_popups(state)` — `shell_project_panels.cpp:181`, `:318`.
- `draw_constraint_catalog_popups()` — defined `shell_constraints.cpp:857`, with
  **exactly one** call at `:2195`. (§0.6 T6: the two-call-site shape holds for the
  first two precedents, not this one — it is still a modal drawn from inside a
  panel, which is the property this section is about.)

None of the three appears in either frame body, and none is visible to
`CheckFrameBodies.cmake`. §2.9 takes this as the shape and then closes the hole
the gate leaves.

### 1.5 An auto-resizing modal is a stub on the frame it opens

Two independent measurements already in the tree, both of which a sweep gets
wrong silently:

- `src/editor/shell_smoke_project.cpp:2427-2432` — *"the chooser's `Rect()` one
  frame after it opens is (452,259)-(468,296), a 16x37 stub, so a sweep that
  captured bounds immediately would scan a sliver and find nothing."* Three
  settle frames are what that case uses.
- `AGENTS.md`'s Headless Frame Smoke Notes — a popup's `InnerClipRect` is not
  settled on the frame it opens, the same previous-frame `CursorMaxPos` lag that
  makes `ScrollMax` read `0`.

F2 (§6.2) inherits both: settle frames before bounds are captured, and the
window's **full `Rect()`**, re-fetched after settling.

---

## 2. Decisions

### 2.1 Three layers, and the boundary is the build graph

Exactly MAR-187's shape, for exactly its reason — the boundary is enforced by
what each target links, not by a promise.

| Layer | Files | Target | Cannot reach |
|---|---|---|---|
| **Model** | `include/marrow/editor/psd_reimport_review.hpp`, `src/editor/psd_reimport_review.cpp` | `marrow_editor` | `ShellState`, ImGui, any window title |
| **Shell** | `src/editor/shell_psd_reimport.{hpp,cpp}` | `marrow_editor_shell` | `ProjectData` mutation, `begin_edit`, `commit_psd_reimport` |
| **Cases** | `src/editor/shell_smoke_psd.cpp`, `src/samples/psd_import_smoke.cpp` | shell / psd smoke | — |

`marrow_editor` contains no `shell_*.cpp` and links no ImGui, so the model layer
*physically cannot* name a widget. Task 9 greps `shell_psd_reimport.cpp` for
`begin_edit`, `transaction`, `commit_psd_reimport`, `std::sort` and `.erase(` and
expects **none** — the shell reads a review, emits widgets, and on a click calls
one model function.

### 2.2 The review model

```cpp
// include/marrow/editor/psd_reimport_review.hpp   (marrow_editor)

/// @brief AC1's three groups, computed from a plan and never stored beside it.
struct PsdReviewSections {
    std::vector<std::size_t> added;    ///< Indices into `plan.layers`, ascending.
    std::vector<std::size_t> updated;
    std::vector<std::size_t> missing;
};
PsdReviewSections group_psd_review(const PsdReimportPlan& plan);

/// @brief One reviewed reimport, from the moment a plan is shown to the moment
///        it is confirmed or abandoned.
struct PsdReimportReview {
    PsdReimportPlan plan;                        ///< Exactly as reviewed.
    std::string plan_digest;                     ///< Of `plan`, at review time.
    std::filesystem::path staging_root;          ///< What `plan` staged into.
    std::filesystem::path source_path;           ///< Absolute; what AC1 displays.
    /// @brief EXACTLY the `Missing` identities the user ticked to FORGET.
    ///        Empty by construction -- AC2's default is the absence of an entry,
    ///        not a `false` stored somewhere that a mutation can flip.
    ///
    ///        Named for what it controls: dropping the layer's provenance row.
    ///        It does NOT delete a slot -- the reimport removes a missing
    ///        layer's slot either way (design 2.3). Do not rename this to
    ///        anything that implies otherwise.
    std::vector<std::string> delete_identities;
};

void set_psd_review_deletion(PsdReimportReview* review,
                             std::string_view identity, bool deleted);
/// @return `delete_identities`, sorted ascending. The value AC2's cases compare.
std::vector<std::string> chosen_psd_deletions(const PsdReimportReview& review);

bool psd_review_can_confirm(const PsdReimportReview& review);
```

**AC2's default is expressed as an absence, deliberately.** A parallel
`std::vector<bool> deleted` sized to `plan.layers` would put the default in an
initialiser, where flipping it is a one-character mutation with no other
observable effect. A set that starts *empty* has no initialiser to flip: making
the default "delete" requires populating the set from the `Missing` section,
which is a visible edit and which D1 catches by comparing the whole set.

### 2.3 `preserve` is derived at commit time, never edited in place (the no-op trap)

```cpp
/// @brief The plan MAR-189 is handed. Returned BY VALUE; see below.
PsdReimportPlan build_psd_commit_plan(const PsdReimportReview& review);
```

It copies `review.plan` and sets `preserve = false` on exactly the layers whose
identity is in `delete_identities`, leaving every other layer's `preserve`
untouched at MAR-188's `true`.

**What `preserve` actually controls, measured at Task 0 (§0.6 T1).** MAR-189
reads it in exactly two places, and only one of them is observable:

- `prune_unpreserved` (`psd_reimport_commit.cpp:463`) erases the layer's slot
  from the staged skeleton. Against **any** plan the real importer produces this
  never fires: `build_skeleton_document` assigns `(*root)["slots"]` wholesale
  from the newly parsed PSD (`psd_import.cpp:1040`) and erases `skins` outright
  (`:1041`), so a `Missing` layer's slot is **already absent** and the lookup
  misses. MAR-189's own comment at that site says so: *"`preserve` governs the
  stored provenance IDENTITY, not the art."*
- `provenance_from_plan` (`:251-287`) keeps the layer's `PsdLayerProvenance` row
  when `preserve` is true, carrying its **current** slot/attachment/bone/image,
  and drops the row when false. **This is the entire observable.**

So the honest reading of AC2's *"preserved"* is **"the project keeps remembering
this layer and what it mapped to"**, and nothing stronger. The slot and
attachment are gone after the reimport in both cases. Downstream this is
load-bearing rather than cosmetic: the planner builds its `stored` map from those
rows and classifies `candidate × stored` (`psd_reimport_plan.cpp:242-247,288-293`),
so a kept row means the layer keeps appearing as `Missing` on every future
reimport, and a dropped row means a returning layer arrives as `Added` with a
fresh slot.

**AC2 remains fully falsifiable** — the observable is typed, it is a set, and the
full-set comparison rule applies unchanged. Only the artefact moves: from the
committed skeleton's slots to `import_sources->psd->layers`. §6.1's D1 and D2 are
rewritten accordingly, and §2.8 fixes the label so the UI cannot imply the
stronger meaning.

**Returning by value is a correctness requirement, not a style preference.** The
obvious alternative — `annotate_deletions(&review->plan)` mutating in place, with
Confirm then passing `review.plan` — makes the natural inversion (*"Confirm
passes the un-annotated plan"*) a **provable no-op**: both expressions name the
same object, nothing cross-reads, and the mutation cannot be observed. That is
exactly MAR-188's I7, which `AGENTS.md` records as *"not a weak inversion; it is
not an inversion at all"*. By-value gives the mutation two distinguishable
objects and makes **I3** (§7) real.

### 2.4 The one call site

```cpp
enum class PsdReviewOutcome {
    Committed, Cancelled, Closed, Stale, PlanFailed, CommitFailed
};

struct PsdReimportReviewOptions {
    std::filesystem::path project_path;   ///< Provenance is relativized against this.
    std::filesystem::path restage_root;   ///< A fresh, empty directory for the re-plan.
};

struct PsdReviewApplyResult {
    PsdReviewOutcome outcome{PsdReviewOutcome::CommitFailed};
    std::string error;                          ///< Never empty unless Committed.
    std::optional<PsdReimportCommitResult> commit;  ///< Present iff a commit was attempted.
};

PsdReviewApplyResult apply_psd_reimport_review(
    EditorSession& session,
    const PsdReimportReview& review,
    const PsdReimportReviewOptions& options);
```

`apply_psd_reimport_review` is **the only function on the EDITOR path that calls
`commit_psd_reimport`**, mirroring MAR-187's *"The button calls `apply_safe_fix`
and nothing else. This is the ONE call site in the tree."*
(`shell_problems.cpp:186-188`). Task 9 asserts it by grep, and the assertion is
what makes the frame case's Confirm click meaningful: with two call sites on this
path, a button wired to the wrong one is invisible.

**"The only function in the tree" would be false, and Task 0 (§0.6 T2) measured
it.** `agent_dispatch.cpp:1264` calls `commit_psd_reimport` for the *agent*
approval path — intended, and exactly the separation §0.5 item 6 draws. MAR-189's
own cases call it 16 more times. The gate must therefore be scoped by directory
and exclude both owners; the plan's **S2** carries the corrected command. A gate
asserting "exactly 1" over `src/` would be red forever, which is the failure mode
where a real guard gets deleted for crying wolf.

Its steps, in order:

1. Re-plan `review.source_path` into `options.restage_root`. **Error → `PlanFailed`.**
2. Digest the re-plan; compare with `review.plan_digest`. **Differs → `Stale`.**
3. `build_psd_commit_plan` on the re-planned plan, carrying deletions by identity.
   A chosen identity absent from the re-plan → `Stale` (it is a staleness
   symptom, not a separate error, and is reported with the identity by name).
   **This arm is unreachable defensive code and is kept as such** — Task 0 (§0.6
   T9) measured that the digest *is* the ordered identity list, so any way a
   chosen identity vanishes or is reclassified moves the digest and step 2
   returns `Stale` first. It is written because a future digest change could
   reach it, and it is **not** claimed as a detector: **I14 is subsumed by I5**.
4. `commit_psd_reimport`. **Failure → `CommitFailed`, carrying MAR-189's result.**
5. `remove_all(options.restage_root)` on **every** exit path.

`Cancelled` and `Closed` never reach this function; they are shell-side
transitions that discard the review. They appear in the enum because the shell
records the outcome for its status line and because a `switch` over the enum is
how the status text is produced — one exhaustive `switch`, no `default:`, so
`-Wswitch` finds every site if a seventh outcome is ever added (`AGENTS.md`:
Clang warns without a flag; it does not fail).

### 2.5 Staleness is defined, and its edge is stated rather than hidden

**Measured at Task 0 (§0.6 T3): the shipped digest is two fields, not ten.**
`psd_plan_digest` (`agent_handlers_management.cpp:82`) emits `identity=change;`
per layer, in the plan's sorted-by-identity order, and
`agent_dispatch_internal.hpp:203` documents it honestly as *"The ordered
`(identity, change)` row list a reviewer saw."* The ten-field tuple below was
this design's prospective request; it is **not** what exists.

MAR-190 **reuses it** rather than defining its own. The decisive reason is not
the two-drifting-lists hazard §0.5 item 5 names, though that is real: it is that
a second, stricter digest would make **the editor path stale where the agent path
is not**, for the same edit and the same operation. An inconsistent staleness
contract between two entry points is worse than a known, recorded gap. The narrow
digest is raised as a follow-up for MAR-189's owner, not repaired inside this
story.

What the two-field digest does and does not cover, stated exactly, because §9.2
depends on it: `change` is computed from stored provenance × PSD candidates
(`psd_reimport_plan.cpp:288-293`), so the digest **is** a function of both inputs
and moves whenever a layer is added, vanishes, or changes classification. It does
**not** move when a row's displayed *mapping* changes without its identity or
classification changing. §9.2 carries that as a limitation in its own terms.

The digest that was requested and not built covered the **ordered row list** —
`(identity, change, current_slot, current_attachment, current_bone, current_image,
proposed_slot, proposed_attachment, proposed_bone, proposed_image)` for every
layer — which would have been a function of **both** inputs in a stronger sense.
Changing either invalidates the review, which is what "stale" has to mean for a
decision the user made about both.

Two consequences, both stated:

- **A pixel-only edit to the PSD is not stale.** Same layers, same targets, same
  digest; the commit proceeds and the new pixels ship. That is almost certainly
  what the user wants — they asked to reimport — but it is a *choice*, and the
  rejected alternative (folding file size and mtime into the digest) would refuse
  a reimport for doing its job. §9 carries it as a limitation.
- **A provenance edit that DROPS a layer between review and Confirm *is* stale**,
  and this is what makes the shell's N3 case writable at all without authoring a
  second PSD: the two checked-in fixtures differ in **4 bytes** and have
  **identical layer sets** (MAR-188 §0.3 E7), so swapping one for the other does
  **not** change the digest. N3 mutates the project's `import_sources` through a
  real transaction instead — which is both a realistic race and a stronger claim,
  because it proves the digest covers the project side. **N3 must drop a whole
  layer, not merely edit a mapping**: dropping one removes its row from the
  identity list and moves the digest, while a mapping edit does not (§9.2).

**The fallback is not taken.** Task 0 (§0.6) found `psd_plan_digest` declared in
`src/editor/agent_dispatch_internal.hpp:203` — a `src/editor/` internal header in
`namespace marrow::editor::agent_detail`, not the public header §0.5 item 5
requested, but in the **same target** as MAR-190's model layer
(`CMakeLists.txt:499,524`). `src/editor/psd_reimport_review.cpp` includes it and
calls it. No second field list exists, so the drift hazard §0.5 item 5 describes
does not arise. Inversion **I5** still proves the digest is not a stub.

### 2.6 The modal, and where it is drawn

```cpp
constexpr char kPsdReimportModal[] = "Reimport PSD##psd_reimport";
```

joining the thirteen window-title constants at `shell_state.hpp:1011-1025` as a
**modal** constant, beside `kFilePathModal`/`kNewProjectModal`/`kDirtyIntentModal`
in the same spirit (`shell_file_paths.hpp:56-58`), because *"a popup's ImGui
window name is the FULL string passed to `BeginPopupModal`, `##` suffix
included"* and the frame case finds it by that exact string.

`draw_psd_reimport_modal(ShellState*)` lives in `src/editor/shell_psd_reimport.cpp`
and is called from **exactly one place**: inside `draw_project_window`
(`shell_project_panels.cpp:954-1084`), which both frame bodies already call
unconditionally (`shell_main.cpp:583`, `shell_smoke_frames.cpp` shared list).
This is §2.9's whole argument: there is no duplicated list to keep in sync, so
there is nothing for a gate to guard.

**`BeginPopupModal(kPsdReimportModal, &state->psd_reimport->open, flags)` — the
`bool*` is required by AC3 (G9).** Escape cannot close a modal
(`imgui.cpp:14873`), so without a `p_open` there is no "modal close" path
distinct from Cancel and one fifth of AC3 is unsatisfiable. With it, `Begin`
draws a title-bar close control that a real mouse can reach and
`ClosePopupToLevel` fires when it goes false (`imgui.cpp:13104-13109`).

Flags: **not** `ImGuiWindowFlags_AlwaysAutoResize`. The modal uses
`SetNextWindowSizeConstraints` with a max height so that a long plan scrolls
**the modal itself** rather than an inner child — see §2.7.

### 2.7 No `BeginChild`, no `BeginTable`, no `PushID` around the rows

This is a testability constraint promoted to a design rule, and it has a gate.

`AGENTS.md`'s Headless Frame Smoke Notes: *"`window->GetID(label)` is the wrong
seed if anything pushed an ID"* — `BeginTabItem` cost MAR-185 real time with a
widget that was drawn and hoverable the whole time, and the sweep reported it
*absent*. A scrolling child around the rows would do the same to F2, and the
failure mode is a green-looking "widget missing" error that blames the wrong
thing.

So the rows are emitted at plain modal scope with explicit `##<identity>`
suffixes, exactly as `shell_problems.cpp:160-190` does, and **Task 9 greps
`shell_psd_reimport.cpp` for `BeginChild`, `BeginTable`, `BeginTabItem` and
`PushID` and expects none.** That grep is falsifiable — it is red the moment
somebody wraps the list for scrolling — which a comment is not.

Row layout, inheriting G2's measured repair verbatim:

```
[ label Selectable, width = max(40, GetContentRegionAvail().x - kControlColumn) ]
  SameLine()
[ Checkbox "##delete_<identity>" ]        // Missing rows only
```

`kControlColumn` is a named constant, not a literal at the call site, so F2 can
assert against the same number the drawing code uses.

### 2.8 What AC1 displays, and what "before enabling confirmation" means

The modal's body, top to bottom:

1. **Provenance** — the absolute source PSD path, the project-relative
   `layers_directory`, and the count of layers the project remembers
   (`import_sources->psd->layers.size()`). `PsdImportProvenance` has **three**
   members and all three exist today (`project.hpp:596-600`); the earlier "four
   fields" was a miscount (§0.6 T7).
2. **Three labelled sections** — `Added (n)`, `Updated (n)`, `Missing (n)` — in
   that order, each listing its rows in the plan's own sorted-by-identity order.
   Section membership comes from `group_psd_review`, never from a local filter,
   so the model layer is what a UI-free case asserts.
3. **`Forget mapping` checkboxes on `Missing` rows only.** An `Added` or
   `Updated` row has no mapping to forget, and drawing an inert checkbox there
   would make F2's sweep ambiguous.

   **The label is not `Delete`, and this is a correctness decision, not a
   wording preference.** Task 0 (§0.6 T1) measured that a `Missing` layer's slot
   and attachment are removed by the reimport **whether the box is ticked or
   not** — the importer replaces `slots` wholesale. On a modal whose every other
   row is about slots and attachments, a box labelled `Delete` reads as *"delete
   the slot"*, and a user who leaves it unticked would form the false belief that
   their rig data is being kept. The checkbox governs only whether the project
   keeps **remembering** the layer.

   The section therefore carries a body line stating **both** halves, because
   either one alone is misleading:

   > *This layer is no longer in the PSD. Its slot and attachment are removed by
   > the reimport either way. Leave unticked to keep remembering where it used to
   > map — it will keep appearing here on future reimports. Tick to forget the
   > mapping; if the layer ever returns it arrives as a new layer.*

   The second half is not decoration: forgetting the mapping drops the layer's
   `PsdLayerProvenance` row, and the planner classifies against exactly those
   rows (`psd_reimport_plan.cpp:242-247,288-293`), so a returning layer arrives
   as **`Added`** with a fresh slot rather than as `Updated`. A user who reads
   only the checkbox must not be able to form a false belief about their art.
4. **Confirm / Cancel**, plus the title-bar close control.

*"Before enabling confirmation"* is read literally: `psd_review_can_confirm`
returns false — and Confirm is drawn `ImGui::BeginDisabled` — while
`review.plan.error` is engaged, and while the plan is empty of all three
categories (nothing to do). It does **not** gate on the user having scrolled or
acknowledged anything; no acceptance criterion asks for that, and a gate nobody
can satisfy from a headless case is a gate that gets deleted.

### 2.9 How the frame-body gate is satisfied, stated without inflation

**MAR-190 adds no `draw_*_window` call to either body.** The gate's set is,
measured at `cf6a199`:

```
application (shell_main.cpp:583-597):
  draw_agent_window draw_constraints_window draw_hierarchy_window
  draw_inspector_window draw_parameter_windows draw_problems_window
  draw_project_window draw_runtime_window draw_timeline_window
  draw_viewport_window                                          (10)
smoke (shell_smoke_frames.cpp shared list):
  the same nine, without draw_agent_window                       (9)
allowlist: draw_agent_window                        -> sets agree
```

After MAR-190 the two lists are **character-identical to the above**, and the
`marrow_frame_body_check` STATUS line names the same ten. That is a
**non-effect** gate — the same shape as MAR-187's unchanged registry sweep — and
it is labelled one. It proves MAR-190 did not disturb somebody else's invariant.
**It provides no coverage whatsoever of MAR-190's own surface**, for two
independent reasons: the regex `draw_[a-z_]+windows?\(` never matches
`draw_psd_reimport_modal(`, and there is only one call site to disagree with
itself.

What replaces it, and what can actually fail:

- **Task 9's single-call-site grep.** `grep -rn "draw_psd_reimport_modal(" src/`
  must return exactly three lines — the declaration in `shell_psd_reimport.hpp`,
  the definition in `shell_psd_reimport.cpp`, and **one** call in
  `shell_project_panels.cpp`. A second call site is the moment a duplicated list
  exists, and the grep goes red then and not later. It is red today (zero hits),
  so it is a gate and not a witness.
- **F2, the frame case**, which is the only thing in the story that can observe
  a widget. It backs **neither** draw list — it renders through its own lambda,
  exactly as MAR-187 measured — and this design does not pretend otherwise.

### 2.10 History, and what "preserving undo/redo history" means on success

`EditorSession::adopt_runtime_sources` *"bumps the runtime and preview revisions
on success. The authored project is untouched, so `project_revision()` does not
move and history stays valid"* (`session.hpp:189-205`). AC5's runtime half is
therefore free.

Provenance is a different matter: MAR-189's `UpdateProvenance` edits
`ProjectData`, which is a history event. MAR-190 reads AC5's *"preserving …
undo/redo history"* as **preserving what is already there**, not as adding
nothing — the same reading MAR-187 took when a fix produced *"exactly one undo
entry"*. So:

- a successful commit leaves `undo_count()` at **before + 1**, with the new entry
  on top;
- **every pre-existing overlay survives byte-identically.** V6 proves it by full
  identity rather than by spot checks: serialize before, serialize after, then
  overwrite the *after* project's `editor_metadata.import_sources` with the
  *before* value, re-serialize, and require **byte equality** with the before
  string. One field excused by name; everything else asserted. A dropped
  animation edit, curve, constraint, inherit, mesh or editor overlay fails it.

The 17-significant-digit serializer property (`AGENTS.md`) is respected: both
strings are produced **in memory** from live `ProjectData`, never round-tripped
through a file, so nothing drifts under the comparison.

### 2.11 What does not move

- **No agent operation, no MCP tool.** The registry stays **66** and none of the
  eleven `!= 66U` guards, `agent_dispatch_smoke.cpp:42`, or `test_client.py:53,55`
  changes. AC5 of *MAR-189* owns the agent path; MAR-190's Confirm is the editor
  path.
- **No format change.** `.marrow` gains no key — MAR-188 added
  `$.editor.import_sources` and MAR-190 only displays and updates it. `.mskl`
  stays 1, `.mbin` stays 2, the C ABI is untouched, `docs/root1/format-spec.md`
  gets an empty diff.
- **No fixture edit.** The gate is `editor_project_smoke.cpp:20584-20604`, and it
  is **two** branches, not one (§0.6 T5). A project carrying *no* editing-fixture
  markers takes the **skip** branch: it prints a line and **exits 0**, running
  none of the editing suites. A project carrying only *some* of them is treated
  as a corrupted fixture and **`return 1`**s — the branch's own comment records
  that without it, *"player_idle losing one bone would quietly stop running
  roughly two dozen suites and still exit 0."* So the loud arm exists only for
  partial damage; wholesale replacement is silent. W1 asserts every tracked
  fixture is byte-identical after the run.
- **No new window, no `kDockLayoutVersion` bump, no new `ctest` test.** `ctest -N`
  must equal Task 0's number -- **measured 23**, because MAR-189 registered
  `marrow.psd_import_smoke` -- at Task 10. (The parenthetical read "expected 22"
  until MAR-191 found it: written before MAR-189 landed, and stale from the moment
  it did. The RULE is anchored to Task 0's own measurement, which is why the rule
  survived the drift and only the annotation did not.)
- **No change to `psd_import.cpp`, the atlas packer, or the planner's
  classification.**

---

## 3. Where each piece lands

| File | Change |
|---|---|
| `include/marrow/editor/psd_reimport_review.hpp` | **new** — §2.2, §2.3, §2.4 |
| `src/editor/psd_reimport_review.cpp` | **new** — grouping, deletion set, commit-plan derivation, the one call site |
| `src/editor/shell_psd_reimport.hpp` | **new** — `draw_psd_reimport_modal`, `begin_psd_reimport_review`, `close_psd_reimport_review` |
| `src/editor/shell_psd_reimport.cpp` | **new** — the modal; no logic (Task 9 greps it) |
| `src/editor/shell_state.hpp` | `PsdReimportPanelState`, `kPsdReimportModal`, one `ShellState` field |
| `src/editor/shell_project_panels.cpp` | the `Reimport PSD...` button in `draw_project_window`, and the **one** `draw_psd_reimport_modal(state)` call |
| `src/editor/shell_smoke_psd.cpp` | **new** — shell cases W1, N1–N5, F1, F2 |
| `src/editor/shell_smoke_scenarios.hpp` | two declarations |
| `src/editor/shell_smoke.cpp` | one scenario call, before `validate_shell_foundation_smoke` |
| `src/samples/psd_import_smoke.cpp` | `validate_mar190_reimport_review` — V1–V8 |
| `CMakeLists.txt` | three source lines into two existing targets; no new target, no new test |
| `AGENTS.md` | `## Current Validation` entry; `## MAR-190 … Validation Results` |
| `docs/root1/format-spec.md` | **none** — proved by an empty diff (Task 10) |

`src/editor/shell_smoke_project.cpp` is **6396 lines and shared with in-flight
stories**; MAR-190 adds a new `shell_smoke_psd.cpp` instead, following MAR-187's
precedent of creating its own files rather than editing an upstream story's. That
leaves five small shared edits, each of which is staged as **hunks** with `git
apply --cached` per the story checklist.

---

## 4. Acceptance criteria → artifacts

| AC | Satisfied by | Proved by |
|---|---|---|
| **AC1** provenance + grouped added/updated/missing before confirmation | §2.2 `group_psd_review`, §2.8 | V1, V2, **F1** (rows on screen), I1, I2 |
| **AC2** deletion checkboxes default off; missing preserved unless selected | §2.2 (default as absence), §2.3 | **D1** (full set == `{}`), **D2** (full set == one named identity), **F2** (a real mouse ticks one), I3, I4 |
| **AC3** cancel / close / stale / plan failure / commit failure change nothing | §2.4, §2.6, §5.3 | **N1–N5**, each through `expect_reimport_no_op`, I6, I7, I8 |
| **AC4** confirmation commits exactly the reviewed choices | §2.3, §2.4 | V5, D2, I3 |
| **AC5** success adopts runtime source, preserves overlays + history, refreshes | §2.10 | **V6** (full-identity minus one named field), V7, I9, I10 |
| **AC6** shell and PSD tests cover all eight named behaviours | §5, §6 | V1–V8, D1–D2, N1–N5, F1–F2, W1 |

---

## 5. Test surfaces and the rules every case obeys

### 5.1 Which binaries, and why no third

| Binary | Cases | Why here |
|---|---|---|
| `marrow_psd_import_smoke` | V1–V8, D1, D2 | The synthesiser lives here (§1.2), so *classification-shaped* inputs — an added layer, a renamed layer, a PSD-side stale change — are cheap. It links `marrow_editor` and so reaches the model layer and `commit_psd_reimport` |
| `marrow_editor_shell` | W1, N1–N5, F1, F2 | The only binary that has a `ShellState`, a selection, an ImGui context and a real mouse |

No third binary. `marrow_project_smoke` would need `editor_project_smoke.cpp`,
which is 20781 lines and contended.

**MAR-189 registered `marrow_psd_import_smoke`** (`CMakeLists.txt:1369-1372`,
`marrow.psd_import_smoke`), so Task 0 measured **`ctest -N` = 23**, not the 22
this document and the plan predicted. MAR-190 does **not** touch the test list:
Task 10's T2 target is **23**, and any other value is a regression to
investigate, never a number to "fix". Stated so it is never adjusted in the wrong
direction.

### 5.2 The rules

1. **Assert the message, not `!result`.** Every failure clause names the outcome
   *and* matches a substring of `error`. `!result` passes under an unrelated
   failure and is how a story ships a case that measures nothing.
2. **A count is almost never the fact.** Deletion choices, section membership and
   layer identities are compared as **full sorted lists**, reported as set
   differences. Counts appear only as redundant clauses beside a list, and are
   labelled redundant (MAR-188's I17 is the demonstration that such a clause
   earns its place exactly once).
3. **Never assert on a substring of `serialize_project()`.** `$.editor.import_sources`
   round-trips through `preserved_root` with zero code (MAR-188 §0.3 E1), so a
   text search passes on an empty commit. Assert the typed struct.
4. **Author 17-digit values in memory, never through a file** (`AGENTS.md`).
5. **Every case is proven falsifiable**, and attribution is by **run order**:
   when one mutation reddens several cases, the register names the first.
6. **A case green before the implementation exists is a WITNESS**, labelled one,
   naming the inversion that catches its subject or stating plainly that none
   does.
7. **`(void)f(...)` for neutering, never `if (false && …)`.**
8. **Absolute paths in every `cmp`, every restore and every baseline.**

### 5.3 `expect_reimport_no_op` — AC3's whole invariant, in one helper

Defined once per binary (the shell copy over `ShellState`, the PSD copy over
`EditorSession`), and called by **five** cases. Its clauses, all of them, every
time:

| # | Clause | Why not something weaker |
|---|---|---|
| a | `serialize_project(*session.project())` **byte-identical** to the baseline string captured in memory before the attempt | A dirty flag, a revision number or "the project still loads" are all compatible with an authored edit |
| b | `project_revision()`, `runtime_revision()`, `preview_revision()` all unchanged | (a) cannot see a runtime swap that was rolled back into identical *files* but left the session rebuilt |
| c | `undo_count()` **and** `redo_count()` unchanged | A failed path that opened and rolled back a transaction can leave depth right and redo wrong |
| d | The **byte map** of the four target artefacts — recursive listing of the layer directory with each file's contents, plus texture, atlas, skeleton — identical, reporting the **first differing path and offset** | MAR-189 §10.3: `rolled_back == true` is compatible with every byte being wrong |
| e | The **absolute path** of the session's active skeleton and every atlas unchanged | AC3 names the runtime *source*, which is a path, not only bytes |
| f | *(shell only)* the selection, serialized as an ordered identity list, unchanged | AC3 names selection explicitly; a count of selected items is not the fact |

Deliberately **not** asserted: `status_message`. A failed reimport *should* say
so, and AC3 does not list it. Stated here so a future reader does not read its
absence as an oversight.

---

## 6. Cases

### 6.1 PSD cases — `validate_mar190_reimport_review` (`psd_import_smoke.cpp`)

Every case builds its own project under `temp_directory_path() / "mar190_<case>"`
from a synthesised PSD, so the checked-in fixtures are read-only throughout.

| # | Case | Asserts |
|---|---|---|
| **V1** | **Grouping is the model's, not the view's.** A synthesised plan with 2 Added, 3 Updated, 2 Missing | `group_psd_review` returns three **ordered index lists**, compared element-wise; each list's identities are ascending; the three lists partition `plan.layers` exactly (union == all indices, pairwise disjoint) |
| **V2** | **Confirmation gating.** A plan carrying `error`, and an empty plan | `psd_review_can_confirm` false for both, true for V1's plan; the error text is the planner's own, matched by substring |
| **D1** | **AC2's default.** V1's plan, freshly reviewed, then confirmed with no interaction | `chosen_psd_deletions(review)` == `{}` — **the full set**; `build_psd_commit_plan(review)` has `preserve == true` on **all seven** layers, asserted as the full ordered `(identity, preserve)` list; after the commit **both `Missing` layers' provenance rows are present** in `import_sources->psd->layers`, compared as the full ordered `(group_path, layer_name, slot, attachment, bone, image_file)` tuple list, each carrying its **current** (pre-reimport) mapping |
| **D2** | **AC2's opt-in.** Tick exactly one of the two `Missing` identities | `chosen_psd_deletions` == `{"<that identity>"}` exactly, as a set difference; the derived plan's `preserve` list differs from D1's in **exactly that one position**, asserted as the full list; after the commit the ticked layer's provenance row is **absent** and the other `Missing` layer's row is **present**, again as the full ordered tuple list — the set difference against D1's list is exactly one row |

> **D1 and D2 previously asserted the committed skeleton's slots, and that would
> have failed on correct code** (§0.6 T1) — the first degenerate shape, sitting
> inside AC2's own row. A `Missing` layer's slot is absent after the commit
> whether it was preserved or not, so *"present by name in the committed
> skeleton"* is false for a correct implementation. Both now assert the
> provenance row list, which is `preserve`'s only observable. **Neither case may
> assert a slot's presence for a `Missing` layer.**
| **V3** | **Stale, PSD side.** Review a plan, then re-synthesise the PSD with one extra layer, then apply | `outcome == Stale`; `error` names the digest mismatch; `expect_reimport_no_op` (all clauses) |
| **V4** | **Planning failure.** Review a plan, then truncate the PSD to 64 bytes, then apply | `outcome == PlanFailed`; `error` carries the planner's own rejection text; `expect_reimport_no_op` |
| **V5** | **Success commits the reviewed choices.** D2's review, applied | `outcome == Committed`; the committed skeleton's slot set equals the derived plan's expected slot set, compared as a **full sorted list**; `commit->steps_executed` covers `kAllCommitSteps` |
| **V6** | **AC5's overlay preservation, by full identity.** Seed an animation edit, a curve, a constraint, an inherit overlay and a mesh-weight overlay; commit | §2.10's construction: after-project with `import_sources` overwritten by the before value re-serializes **byte-identically** to the before string; `undo_count() == before + 1`; `redo_count() == 0` |
| **V7** | **Provenance is refreshed, typed.** Same commit | `import_sources->psd->layers` equals the expected full ordered list of `(group_path, layer_name, slot, attachment, bone, image_file)` tuples; `image_file` is a **bare name** for every entry; `source_path` and `layers_directory` are project-relative |
| **V8** | **Commit failure and rollback, every step.** MAR-189's failpoint seam, swept over `kAllCommitSteps` | For each step: `outcome == CommitFailed`, `error` names **that step**, and `expect_reimport_no_op` — with the one expected asymmetry at `CleanJournal` handled explicitly rather than by a uniform `!ok` (MAR-189 §10.2 records that a uniform sweep is wrong about the fourteenth arm in the direction that destroys a completed reimport). If MAR-189 ships a different step list, V8 iterates whatever `kAllCommitSteps` actually contains and never a hand-written list |

### 6.2 Shell cases — `shell_smoke_psd.cpp`

`validate_mar190_psd_reimport_shell_smoke(const std::filesystem::path&)` builds
its fixture once (§1.2: copy `assets/fixtures/psd_import_sample.psd` to temp,
`import_psd_to_runtime_bundle` into temp, `create_minimal_project`,
`make_psd_provenance`, `save_project`, `load_project` — the last step matters,
because *only* `load_project` → `build_project_runtime` materializes, and a
passing `save()` proves nothing since `validate_project_for_save` is unreachable
from another TU inside `project.cpp`'s anonymous namespace (`:6077`, namespace
`:25-7114`); the route is `save_project`).

| # | Case | Asserts |
|---|---|---|
| **W1** | **The repository is not written.** Runs last | Every tracked path this story could plausibly touch — both `.psd` fixtures, `player_idle.marrow`, `player_idle.mskl`, `player_idle.matl`, `player_idle.mbin` and **`player_fixture.png`** — byte-identical to a map captured at case start, reported by absolute path. **Each path is asserted to EXIST before it is hashed** (below) |

> **W1's file list was wrong, in the way that would have made W1 pass on nothing**
> (§0.6 T8). It named `player_idle.png`, which **does not exist** — the texture
> `player_idle.matl` references is `player_fixture.png` — and it omitted
> `player_idle.mbin`. A hash sweep that skips or silently tolerates a missing path
> is a check with nothing to check, reporting success: **the zero-result rule in a
> third costume**, after zsh glob expansion and the malformed BRE interval. So
> W1's first clause is an existence assertion over the whole list, failing by
> **absolute path** if any entry is absent, and only then does it compare bytes. A
> future rename of a fixture must break W1 loudly rather than quietly emptying it.
| **N1** | **Cancel.** Open the review, tick one deletion, click Cancel (UI-free entry point) | `expect_reimport_no_op` (all six clauses); `state.psd_reimport` disengaged |
| **N2** | **Modal close.** Same, through the `p_open` path | identical clauses; the two paths are asserted **separately** because they are separate code (§2.6) |
| **N3** | **Stale, project side.** Open the review; commit a real transaction that drops one layer from `import_sources`; apply | `outcome == Stale`; `expect_reimport_no_op` **relative to the post-transaction baseline** — the deliberate edit is the baseline, and asserting against the pre-edit state would be a gate that fails on correct code |
| **N4** | **Planning failure.** Delete the temp PSD, then apply | `outcome == PlanFailed`; `expect_reimport_no_op` |
| **N5** | **Commit failure.** MAR-189's seam, injected after one representative step (V8 owns the exhaustive sweep) | `outcome == CommitFailed`; `expect_reimport_no_op` |
| **F1** | **The rows are on screen, by a real mouse.** | The `Reimport PSD...` button in the **Project window** is located by a `HoveredId` sweep — **control sweep first**, against a control that already exists in that window, so a broken id seed cannot be misread as a missing widget. Clicking it opens `kPsdReimportModal`, found by `FindWindowByName` on the **full** title string. After **three settle frames** (§1.5) the modal's `Rect()` is captured and one row per section is located by `modal->GetID(row_label)`. Prints every located coordinate |
| **F2** | **The deletion checkbox is reachable, and ticking it is what sets the choice.** | The `Missing` row's checkbox is swept **in its own column** (a column at the row's left edge reaches the `Selectable` and misses everything after `SameLine()`); its located x is asserted **strictly inside** `modal->Pos.x + modal->Size.x - kControlColumn`, which is G2's measured failure made into an assertion rather than a comment; a real click sets `chosen_psd_deletions` to exactly `{that identity}`; `io.DeltaTime` is advanced past `io.MouseDoubleClickTime` before any further gesture; then Confirm is located, clicked, and `undo_count()` advances by exactly one |

`io.ConfigMacOSXBehaviors` is left alone — F1 and F2 need no modifier. If a later
revision adds one, `AGENTS.md`'s recipe applies: clear it for the gesture,
restore it, and assert `ImGuiContext::TempInputId`.

### 6.3 The witness, labelled

**P1 (witness, not a gate).** A project with **no** PSD provenance draws no
`Reimport PSD...` button and `serialize_project` is byte-identical before and
after MAR-190. This is **green on the pristine tree** and will stay green: it
asserts backward compatibility, which is a real and otherwise-unmade claim, and
it is evidence that no MAR-190 code works. **No story-owned inversion turns it
red**, and that is recorded rather than repaired — manufacturing one would mean
inventing a mutation nobody would make. It is labelled in the source, in the
plan, and in `AGENTS.md`.

---

## 7. Inversion register — the mechanism

The mutations, predicted failure texts and per-case attribution are in the plan's
§B. What belongs here is the claim about the **code**: why each mutation is
invisible to everything except the case named.

| Mutation | Why nothing else notices |
|---|---|
| **I1** `group_psd_review` puts `Missing` rows in the `Updated` list | Counts still sum to `plan.layers.size()`, so any count clause passes. Only **V1's partition clause** — union and pairwise disjointness over *ordered index lists* — fails. This is why V1 asserts a partition and not three sizes |
| **I2** Sections emitted in candidate order instead of the plan's sorted order | Set membership is unchanged, so every "contains" clause passes. Only **V1's element-wise ordered comparison** fails, and **F1** cannot see it at all — a row is on screen either way |
| **I3** Confirm passes `review.plan` instead of `build_psd_commit_plan(review)` | Only observable because §2.3 returns by value. D1 passes (no deletions, so the two plans are equal). Only **D2** and **V5** see it, and they see it as a *provenance row that should have been dropped and was not* — **not** as a stray slot, which is what §0.6 T1 corrected: the slot is absent either way, so a skeleton-side clause could not detect this. If `build_psd_commit_plan` mutated in place, this inversion would be a provable no-op and would prove nothing — MAR-188's I7 in a new setting |
| **I4** `delete_identities` seeded from the `Missing` section at review time (AC2's default inverted) | Every commit still succeeds, and every count of "layers processed" is unchanged. **D1's full-set clause** (`== {}`) fails first by run order; D1's `preserve` list clause fails too and names the layers. Nothing else in the story distinguishes it |
| **I5** `psd_plan_digest` returns a constant | Every non-stale case passes — a constant compares equal to itself. Only **V3** (PSD side) and **N3** (project side) fail. Both exist because one digest input alone would not prove the digest covers the other |
| **I6** `apply_psd_reimport_review` omits `remove_all(restage_root)` on the failure paths | Every outcome, error string and byte-map clause passes: staging is not a target. Only **N4's** staging-residue clause sees it. Recorded as the demonstration that the residue clause is not decorative |
| **I7** Cancel closes the modal but leaves `state.psd_reimport` engaged | The invariant clauses all pass — cancel changes nothing either way. Only **N1's** disengagement clause fails, and only **F1** would notice the stuck modal blocking a later click. `AGENTS.md`: *"a modal stays open until something calls `CloseCurrentPopup()`"* |
| **I8** The `p_open` argument is replaced by `nullptr` | Cancel still works, so N1 passes. Only **N2** — and, in a frame, only a sweep for the title-bar control — fails. This inversion is the reason N2 is a separate case from N1 rather than a second clause inside it |
| **I9** `UpdateProvenance`'s result is written but `adopt_runtime_sources` is skipped | `serialize_project` is *correct* — provenance updated — so V6's identity clause passes. Only **V7's** runtime-source clause and the `runtime_revision()` movement fail |
| **I10** The success path calls `session.clear_history()` "to tidy up" | Every byte clause and every provenance clause passes. Only **V6's** `undo_count() == before + 1` fails. This is the inversion that justifies asserting depth on the *success* path and not only on the failure paths |
| **I11** The row `Selectable` loses its explicit width (G2's repair reverted) | Every UI-free case stays green — this is the whole point of the finding. Only **F2's** in-bounds x assertion fails, and it fails with a coordinate rather than with "absent", which is what makes it diagnosable |
| **I12** `draw_psd_reimport_modal` is called from `render_shell_frame` **instead of** from `draw_project_window` | The application still works. The headless smoke's shared body never calls it, so no scenario can see it — and `CheckFrameBodies.cmake` **cannot** see it either, because the name does not match its regex (§2.9). Only **Task 9's single-call-site grep** fails. This inversion exists to prove the grep is the story's real structural guard |

### 7.1 Deliberately uninverted, by name

- **The provenance display strings.** Four `ImGui::Text` calls over fields that
  already have typed round-trip coverage in MAR-188's P-cases. Inverting them
  proves the text formatter, which no acceptance criterion is about.
- **`PsdReviewOutcome`'s status-text `switch`.** Exhaustive, no `default:`;
  `-Wswitch` finds a missing arm on Clang as a warning (`AGENTS.md`: it warns, it
  does not fail, and **GCC needs `-Wall`**). Adding a seventh value is what
  produces the checklist; a per-arm inversion would prove the compiler works.
- **`P1`**, per §6.3 — no story-owned inversion, by design, recorded not repaired.
- **MAR-189's fourteen steps.** V8 sweeps them; inverting the commit itself is
  MAR-189's register, not this one.

### 7.2 The three degenerate gate shapes, checked against this design

| Shape | Where this story was exposed | What was done |
|---|---|---|
| **Fails on correct code** | N3 asserting the no-op invariant against the **pre**-transaction baseline, when N3's own setup deliberately edits the project | §6.2 pins N3's baseline **after** its transaction. Also: V1 asserting insertion order, when `PsdReimportPlan::layers` is documented *"Lexicographic by `identity`, ascending"* — V1 asserts the documented order |
| **Passes on unchanged code** | Any AC1 assertion written as a text search over `serialize_project()`, because `preserved_root` pre-arms it (MAR-188 §0.3 E1); and P1 | §5.2 rule 3 forbids the text search structurally; P1 is labelled a witness (§6.3) |
| **A provable no-op mutation** | I3, if `build_psd_commit_plan` mutated `review.plan` in place — two names, one object, no cross-field read | §2.3 returns by value **for this reason** |

---

## 8. Facts to MEASURE in Task 0, not assume

| # | Claim | How |
|---|---|---|
| **A1** | Every MAR-189 signature in §0.5 exists **by symbol** in the tree as it is then | `grep -rn` for each; a miss is a **blocking** finding recorded in the errors table |
| **A2** | Whether `psd_plan_digest` is header-declared or private to `agent_dispatch.cpp` | Decides §2.5's fallback |
| **A3** | `kAllCommitSteps`' actual contents and which arm succeeds on failure injection | V8 iterates the enum, never a copied list |
| **A4** | The frame-body gate's exact set, from `cmake --build build --target marrow_frame_body_check`, captured verbatim | Task 10 compares character-for-character |
| **A5** | Baseline: all-target clean-object build warning count, `ctest -N`, `ctest`, `marrow_agent_dispatch_smoke` `[ OK ]` count, every smoke green — **at the commit's actual parent** | Numbers do not survive a baseline change |
| **A6** | Registry **66**; the eleven guards' split; `agent_dispatch_smoke.cpp` bound line; `test_client.py` lines | Re-derive; do **not** copy from this document |
| **A7** | Whether `marrow_psd_import_smoke` has been added to CTest by MAR-189 | Decides whether Task 10's `ctest -N` target is 22 or 23 |
| **A8** | `import_psd_to_runtime_bundle` over the tracked sample PSD succeeds from a temp directory and produces all four artefacts | The shell fixture depends on it entirely (§1.2) |
| **A9** | Which `tools/inversion/rebuild.sh` the isolated tree got (§0.4 G3) | `grep -cF 'find "${build_dir}/CMakeFiles"' <tree>/tools/inversion/rebuild.sh` — **`-F` is load-bearing**: without it BSD `grep` reads `{build_dir}` as a malformed BRE interval and reports `0` on a file that contains the string. Measured both ways while writing this |
| **A10** | Escape does **not** close `kPsdReimportModal`, and the `p_open` control does | Assert it in F1 rather than trusting `imgui.cpp:14873` — a measured negative is worth one clause |
| **A11** | `git status --porcelain` before and after, confirming no other agent's tracked file moved | Two agents share this worktree |

---

## 9. Known limitations, stated rather than hidden

### 9.1 Photoshop's real layer-record order is unverifiable here, and this UI displays its consequences

`psd_import.cpp` requires each group's `lsct` header **before** its children
(`:734` is the rejection). Both checked-in fixtures are authored that way, and
MAR-188 §2.8 made that ordering property observable through its I18. **No
Photoshop-authored PSD exists in this repository and obtaining one requires
Photoshop**, which is not available in this environment. Three stories have now
carried this.

MAR-190 is the first story where it becomes **user-visible**: the modal displays
a plan derived from that parse. If the real order is the inverse, a
group-bearing real-world PSD produces a planning error, and what the user sees is
MAR-190's **planning-failure** path — N4 — reporting the parser's own message.
So the limitation is *contained* by AC3 and *not removed*, and the design says so
rather than letting it pass silently. MAR-190 changes no parser code.

### 9.2 The staleness digest covers identity and classification, and nothing else

**Wider than this section originally claimed** (§0.6 T3). The shipped
`psd_plan_digest` is `identity=change;` per layer — two fields, not the ten §2.5
requested — so two distinct edits slip past it:

- **A pixel-only PSD edit is not stale.** Same layers, same classification, same
  digest; the commit proceeds and the new pixels ship. Almost certainly the
  desired behaviour, but it is a decision and not a property. The rejected
  alternative (folding file size and mtime into the digest) would refuse a
  reimport for doing its job.
- **A mapping edit is not stale, and this one is a real gap.** A provenance edit
  that renames a stored `slot_name` between review and Confirm changes what the
  plan's `current_slot_name` says, but not the layer's identity or its
  classification. **The modal showed `current_slot: X` and the commit uses
  `current_slot: Y`, with no staleness reported.** The reviewer approved one
  thing and a different thing is committed.

MAR-190 **inherits** this rather than repairing it, deliberately. A stricter
MAR-190-local digest would make the same edit stale on the editor path and not on
the agent path, for the same operation — an inconsistent staleness contract
between two entry points, which is worse than one known and recorded gap. The
narrow digest belongs to `agent_handlers_management.cpp:82` and is raised as a
follow-up for MAR-189's owner.

N3 is unaffected: it drops a whole layer, which removes a row from the identity
list and does move the digest.

### 9.3 The `Reimport PSD...` button's *absence* is only witnessed, never gated

P1 (§6.3) observes that a provenance-free project draws no button, but it is
green on the pristine tree because nothing draws the button there either. Only F1
proves the button exists when provenance does. The negative direction — *the
button is correctly hidden* — has no gate, and no inversion produces one.

### 9.4 A failed **rollback** is reachable — the disclaimer is withdrawn

**This section previously said the opposite, and Task 0 measured it wrong**
(§0.6 T4). MAR-189 **did** build the second injection seam it was documented as
declining: `CommitRollbackFailpoint` and
`set_psd_commit_rollback_failpoint_for_testing`
(`src/editor/psd_reimport_commit_internal.hpp`), a second independent global
whose comment states the reason — *"`commit_psd_reimport`'s `advance()` runs only
in the commit body, so a failpoint installed there can never fire while the
rollback is running -- and the rollback is the half AC3 is about."*
`rollback_advance` is wired at five sites (`psd_reimport_commit.cpp:778`) and the
result carries a `steps_rolled_back` ledger.

**AC6's *"rollback errors"* is therefore satisfiable as literally worded**, per
step, and V8/N5 assert a failing rollback directly rather than settling for
commit-failure-with-successful-rollback. No caveat is carried into §10.1.

The residual, in the seam's own words, is far narrower than the withdrawn claim:
it *"stops at the session boundary."* A failure **inside**
`adopt_runtime_sources` while re-adopting a rolled-back `UpdateProvenance` needs
a seam within `EditorSession` that MAR-189 does not add. That single window is
the whole of what remains irreversible, and it is not what AC6 asks about.

### 9.5 Slot names remain unstable across imports

MAR-188 §1.5: `parse_psd_document` (`psd_import.cpp:814-826`) dedups slot names
from a document-global census, so adding a layer can rename an untouched slot.
The user sees it as `proposed_slot_name` differing from `current_slot_name` on an
`Updated` row — the honest surface, and MAR-190 displays it without editorial.
MAR-190 does not refuse, repair, or warn about it.

### 9.6 The scrolling path is not exercised by a mouse

F1 and F2 use a four-row plan that fits without scrolling, so the modal's
`ScrollMax` path (`AGENTS.md`: one frame behind, reads `0` on the first pass after
a layout change) is never entered. A plan with fifty `Added` rows is drawn and
never clicked by any test. Recorded rather than half-covered.

### 9.7 Inherited and untouched

- `marrow_psd_import_smoke` **is** in CTest as of MAR-189 (`marrow.psd_import_smoke`); `ctest -N` = 23 (§5.1).
- Save As still cannot rebase a path stored under an unparsed `preserved_root`
  key. Permanently open; not MAR-190's.
- No fsync; durability across power loss remains MAR-180's stated non-goal.

---

## 10. Risks for the implementer

1. **AC6's "rollback errors" DOES mean a failed rollback, and it is reachable**
   (§9.4, §0.6 T4). MAR-189 shipped the rollback failpoint this document was
   written believing it would decline. Assert a failing rollback per step in V8;
   do not settle for commit-failure-with-successful-rollback, which is the weaker
   reading this section used to mandate.
2. **The `Missing` checkbox does not retain rig data** (§0.6 T1, §2.3, §2.8).
   Label it `Forget mapping`, ship the body line, and never assert a preserved
   layer's slot survives the commit — it does not, in either direction. This is
   the single most likely way MAR-190 ships a lie to a user.
3. **MAR-189 exists** (`7462f67`); §0.2's "none of it exists yet" is historical.
   §0.5 items 1–4 all re-derived at `ac82f3c`. Nothing here reimplements the
   commit.
4. **`build_psd_commit_plan` must return by value** (§2.3). In-place mutation
   makes I3 a provable no-op and quietly costs the story its AC4 detector.
5. **Do not wrap the rows in a `BeginChild` for scrolling** (§2.7). It breaks
   F2's id seed and the failure reports the widget as *absent*, which sends the
   next person looking in the wrong place. Task 9's grep is the guard.
6. **Do not add a `draw_*_window`** (§2.9). The gate would then demand a second
   frame-body edit for a modal that has no business in either body.
7. **The shell smoke's working directory is the repository** (§1.3). Everything
   under `temp_directory_path()`; W1 is not optional.
8. **Scope the object deletion**: `find build/CMakeFiles -name '*.o' -delete`.
   The unscoped form rebuilds `libSDL3.a` at 96 bytes and hands you a red run
   with your sources provably untouched.
9. **Name your scratch and build directories after yourself, not after MAR-190.**
   The session scratchpad is shared; MAR-188's implementer found another agent's
   build tree under the story-named path. `git archive HEAD | tar -x` **overlays**
   rather than replaces.
10. **`cmp` with absolute paths only.** A relative path follows whatever `cd` ran
   earlier in the same command, and a `cmp` of a file against itself always
   passes — which is how four assertions stayed neutered through two inversions
   in MAR-188.
11. **One commit. Korean subject and body. Both trailers in one contiguous block
    with no blank line between them.** Three consecutive stories shipped without
    `Claude-Session:` and each needed an amend.
