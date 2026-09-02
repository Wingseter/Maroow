# Marrow Agent Notes

## Project State

- The architecture source of truth is `docs/root1/discription.md`; active dependency-ordered milestones are tracked in `.agents/tasks/prd-marrow-runtime.json`.
- MAR-121 is a completed tracking tombstone whose runtime foundation is integrated into MAR-122. MAR-122 through MAR-128, MAR-154 through MAR-191, and the behavior-preserving Task #28 refactor checkpoint are complete. Editing P1 closes at MAR-191; no product milestone follows it in this chain. MAR-192 through MAR-210 remain an open, parallel deferred qualification backlog and do not block product work.
- Work is organized as small functional milestone checkpoints with focused validation.
- `.agents/ralph/`, `.ralph/`, and `docs/root1/ralph-loop.md` are preserved historical artifacts and are not current execution authority.

## Working Rules

- Read `docs/root1/discription.md` before changing runtime or file-format decisions.
- Keep milestones small, vertical, and independently verifiable at focused checkpoints.
- Preserve the runtime-first plan unless the active story explicitly updates it.
- If a build or test workflow is introduced, document the exact commands here.
- The checked-in PRD already expands the renderer, runtime, and editor roadmap from `docs/root1/discription.md`. Prefer updating that PRD rather than inventing parallel plans.

### Story commit checklist

- Stage **only your own paths**, explicitly named. Once a path is shared with
  another in-flight story, that is not enough -- stage only your own **hunks**
  with `git apply --cached` (see the durable section).
- Every story commit carries **both** trailers, in **one contiguous block with no
  blank line between them**:

  ```
  Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
  Claude-Session: <the session URL>
  ```

  A blank line between them makes git parse only the last, which is how MAR-186
  lost the session trailer the first time. Copy the session value from the parent
  commit: `git log -1 --format='%(trailers)' HEAD`.

  **MAR-186, MAR-187 and MAR-188 each shipped without `Claude-Session:` and each
  needed a follow-up amend.** Three independent agents making the same omission is
  a missing procedure step, not three mistakes -- which is why the convention is
  written here now rather than left to be inferred from `git log`.
- Verify before committing, and re-measure every baseline number **at the commit's
  actual parent**. Numbers do not survive a baseline change; that is the same
  class as a stale line anchor.

## Documentation Entry Points

- Architecture source of truth: `docs/root1/discription.md`
- Runtime integration walkthrough: `docs/root1/quick-start.md`
- Runtime ownership and playback model: `docs/root1/concepts.md`
- File format reference: `docs/root1/format-spec.md`
- Fixture mapping and sample asset intent: `docs/root1/fixtures.md`
- Platform qualification evidence: `docs/root1/platform-validation.md`
- Vendored dependency provenance: `THIRD_PARTY.md`
- Archived Ralph loop/operator record: `docs/root1/ralph-loop.md`

## Current Validation

- Configure: `cmake -S . -B build`
- Build: `cmake --build build`
- Vendored dependency/hash/patch verification: `cmake --build build --target marrow_verify_third_party`
- SDL/Sokol window seam unit tests: `./build/marrow_windowing_tests`
- SDL pen/pressure unit tests: `./build/marrow_pen_input_tests`
- Cross-platform preference path, atomic-write, fixed curve-preset constant, preset-token, shell preference-session, and recent-project list-algebra tests (canonicalization, MRU ordering, de-duplication, eviction at the bound, `normalize` idempotence, and the measured macOS case boundary): `./build/marrow_preference_tests`
- Agent loopback/partial-I/O/repeated-lifecycle transport tests: `./build/marrow_agent_socket_tests`
- Sokol ImGui setup/frame/shutdown lifecycle probe: `./build/marrow_sokol_imgui_runtime_probe`
- Typed transient entity selection model: `./build/marrow_selection_tests`
- Viewport interaction data-kernel tests: `./build/marrow_viewport_interaction_tests`
- Timeline data-model and authoring-boundary tests, including MAR-185's UI-free
  clipboard animation cascade (rename remaps `Clipboard::animation_name` and
  re-enables Paste, delete clears it, an unrelated animation changes nothing):
  `./build/marrow_timeline_model_tests`
- Timeline scalar-graph projection/geometry/view/drag-math tests: `./build/marrow_timeline_graph_model_tests`
- Editor project authoring smoke including `offset_keyframe_scalars` and MAR-184's
  stepped inherit overlays (schema round trip, all five modes, the five parser
  rejections, the curve rejection, the `ensure` accessor, base-backed /
  project-only / empty materialization, the merge primitive's four rejections and
  all three collision arms, and `.mskl`/`.mbin` export equivalence -- P1-P13),
  and MAR-185's inherit EDITING parity (the editable dopesheet row and its
  selector, all five modes through the merge primitive, the same-time obligation
  by rejection and by paste collapse, removal down to the one-key floor proved
  through the export, six rejections each asserted on their message and each
  leaving `serialize_project()` byte-identical, retime apply and clamp proved on
  the stored time after a real reload, a scale rejection naming the inherit lane
  and a legal scale exact to 1e-12, duration auto-grow, and the rename/delete
  overlay cascade -- P1-P9, U1-U2):
  `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- MAR-186's structured project diagnostics: `player_idle.marrow` is issue-free
  and collection moves none of a session's seven observable values; all seven
  orphan overlay families are reported by identity with their typed family; the
  animation-name authority is the `animation_edits` fold rather than the
  materialized skeleton; identity is position-independent, stable across two
  collections, and survives a real save/LOAD round trip that resurrects the
  phantom animation; the weight predicate is the canonicalizer's own bit-exact
  fixed point; stale preview references, the orphan-weight-target supersession,
  the escaping collision, the typed targets replayed through
  `SelectionSet::replace`, the asymmetric severity counts, the dirty session's
  `project.unsaved_changes`, and the `std::nullopt` for a session with no
  project (G0-G12):
  `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- MAR-187's Problems view and safe fixes: an issue-free project yields an empty
  view and moves none of a session's seven observable values; a report whose
  LOWEST identity is a Warning still groups Error first; an empty group is
  omitted; the view's counts are the collector's under all three filters; a row
  identity survives an insertion that sorts before it; a removed target plans no
  selection; all seven `DiagnosticCode` values plan a typed target read BY NAME;
  the refresh key is BOTH revisions, proved through a runtime-only
  `adopt_runtime_sources`; the orphan-overlay repair removes ONE record,
  including siblings differing only by transform channel, deform attachment or
  bone; a weight repair fixes one vertex; the preview repairs write the
  substituted value and keep a resolvable duplicate; exactly three of a
  74-string corpus are allowlisted; three rejection arms each name their cause;
  and a fresh open repairs nothing (V0-V8, X1-X9):
  `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- `marrow_project_smoke` asserts only what the project it is pointed at actually contains. The viewport debug-overlay gate keys on whether the document authors `editor.viewport.debug_overlay` (round-tripping it value for value when present, asserting the `DebugOverlaySettings` defaults when absent, plus an alternating-pattern round trip that catches two toggles wired to each other's key — which an all-`true` fixture cannot), and the `player_idle`-specific editing suites run only for a project carrying their markers (bones `spine`/`arm_l`, animations `attack`/`aim`, skin `mesh_base`). A project matching NONE of them prints a named skip and still runs the shape and export checks; a project matching SOME of them ABORTS, because a partial match is a corrupted fixture rather than a project to skip
- Constraint parameter model-layer coverage (eleven IK/physics fields at their boundaries through save -> LOAD -> materialize, the three-layer refusal of an out-of-range physics value, the deliberate `softness < 0` compatibility case, and `.mskl`/`.mbin` agreement after `.mbin` v2's float32 narrowing): `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- Atomic project save, cross-directory Save As rebasing, history rebasing, session `create`/`close`, and failure-safe runtime-source adoption (S1-S10): `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- Shell hot-reload failure coherence and save-failure preservation (C2, C3): `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- Core File path workflows -- New/Open/Save/Save As through the dependency-free ImGui path modal, path-resolution rules, failed-Open and failed-Save-As shell preservation, `Ctrl+S` under the text-input guard, the mouse-driven File menu, and the deferred-action wiring of the smoke's own frame body (C4-C11): `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- Unified dirty-session intent: the Save/Discard/Cancel machine in front of New/Open/Reload/Quit/OS-close, save failure, save-path cancellation, repeated requests, modal close, and the mouse-driven prompt including the File > Quit item (C12-C19): `MARROW_CONFIG_HOME=/tmp/mar182-cfg ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- Recent projects: canonicalization, MRU ordering, de-duplication, eviction at the
  bound, missing-entry visibility, Remove/Clear Missing, the dirty-gated Recent
  open, failed-action preservation, and the Save As path's report of a failed
  settings write (C20-C25):
  `MARROW_CONFIG_HOME=/tmp/mar183-cfg ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- Headless editor shell smoke including the graph drag scenario and actual-frame drags: `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- Inherit timeline editing in the shell -- sampled Add at the playhead, in-place
  replace, typed copy/paste, exact-playhead Remove, a bit-identical cancelled
  retime, the single-lane paste remap onto the selected row, and the GUI rename
  clipboard cascade (S1-S7), plus the actual-frame
  case that locates the `Mode` combo with a real mouse and clicks through its
  popup (F1): `MARROW_CONFIG_HOME=/tmp/mar185-cfg ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- MAR-187's Problems router in the shell -- timeline/weight/preview activation,
  a removed target leaving the previous selection standing, a fix through the
  shell path clearing the vanished row, and ten idle refreshes running zero
  collections (S1-S6), plus the actual-frame case that locates the severity
  filter and a row's Fix button with a real mouse and clicks it (F1):
  `MARROW_CONFIG_HOME=/tmp/mar187-cfg ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- MAR-188's typed PSD provenance and the sixth rebase family: a pre-MAR-188
  project gains nothing and serialises byte-identically (a compatibility WITNESS,
  green before the story and with no story-owned inversion -- never read it as
  vindication); provenance round-trips through the TYPED STRUCT rather than
  through `preserved_root`, which already carried the key verbatim before this
  story and makes any text-level assertion pass on unmodified code; an in-memory
  edit beats the preserved copy; clearing at either level removes the key rather
  than writing `{}`; eleven malformed documents are each refused by message AND
  JSON path while an unrelated project's serialization stays byte-identical; and
  Save As rebases BOTH provenance paths by name -- relative into a parent
  directory, absolute into a sibling, an already-absolute path byte-identical --
  each resolving to the same absolute file as before (P1-P8):
  `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- MAR-188's PSD reimport planning, over PSDs SYNTHESISED by the smoke because the
  two checked-in fixtures are four bytes apart and cover one case: the synthesiser
  is gated on reproducing the fixture's layer and bone reports, its extracted
  pixels, and its header -- the header clause caught a three-channel writer whose
  report was already identical -- plus the duplicate-name dedup branch the fixture
  tree cannot reach (Q0, Q0b); then classification is exact-name only, so a rename
  and a group move each produce one Added and one Missing rather than an Updated,
  depth-2 nesting survives element-wise (its first coverage anywhere), escaped
  identities stay distinct where the collision is constructible, duplicate
  candidates and a non-PSD are refused by name, a plan leaves the project's
  serialization, files and whole directory listing byte-identical with every
  staged path under the caller's root and the staged skeleton merged against the
  project's real one, Missing defaults to preservation, two plans agree
  element-wise in ascending identity order, and a project with no provenance plans
  every layer as Added (Q1-Q11):
  `./build/marrow_psd_import_smoke assets/fixtures/psd_import_sample.psd assets/fixtures/psd_import_sample_reimport.psd`
- MAR-189's atomic PSD reimport commit, on bundles built by a real import into a
  DISPOSABLE directory and never against the tracked fixture: a repository-safety
  predicate aborts the process before any writing case, and its three rows prove
  it refuses the tracked fixture bundle and the repo's own build directory (A11);
  staged bundles carry the TARGET's names so the atlas document's `image` member
  survives a byte copy (Q12-Q14); a clean commit walks the whole fifteen-value
  step enum in order and replaces skeleton, atlas, texture and layers with the
  staged bytes; the committed atlas names the texture the PRE-COMMIT atlas named
  and the bundle gains and loses no file; a bundle staged under a different
  texture name is refused by message; an injected failure after EACH of the
  fifteen steps rolls the bundle back byte-for-byte against a recursive listing of
  the project and layer directories and leaves the session's active skeleton
  source and provenance untouched -- except after `CleanJournal`, where the
  completed reimport stands; an unremovable backup is reported as residue rather
  than rolled back; overlays survive element-wise IN MEMORY while provenance is
  rewritten in the plan's order; `preserve=false` drops exactly one identity and
  one slot; a rollback after a successful adoption restores the original skeleton
  to the session; the journal exists in flight and is gone after; and five
  refusals each name their cause (R0-R8, R1b, R1c, R3b). Then the operation: a
  dry run returns the ordered plan with its three counts and leaves both the
  bundle and the staging root empty, an output outside the project bundle and a
  staging root outside the whitelist are each refused BY CODE, approval commits
  and empties the queue, and an unknown id, a whitelist-rejected request and a PSD
  changed since review are each refused without touching a byte (A1-A6):
  `./build/marrow_psd_import_smoke` -- now also `ctest --test-dir build -R marrow.psd_import_smoke`,
  which it was not before this story
- MAR-190's PSD reimport review UI, model and shell. The model layer, UI-free: the
  three review sections partition the plan and preserve its ascending identity
  order, confirmation is refused for a plan carrying a planner error and for one
  with nothing to do, the derived commit plan forgets EXACTLY the ticked
  identities and defaults to none without mutating the review, a PSD that changed
  under an open review or became unreadable is refused as Stale or PlanFailed, a
  confirmed reimport commits the full step ledger and keeps exactly the provenance
  rows that were not forgotten while every other authored field stays
  byte-identical, an injected failure after each of the FIFTEEN commit steps is
  reported as CommitFailed naming that step with the ledger ENDING there (except
  after `CleanJournal`, where the completed reimport stands), and a redo stack the
  user already had survives a failed reimport intact and still applies
  (V1-V9, D1-D2):
  `./build/marrow_psd_import_smoke assets/fixtures/psd_import_sample.psd assets/fixtures/psd_import_sample_reimport.psd`
  -- and the shell, on a fixture built by the REAL importer entirely under the temp
  directory: cancelling a review, closing its modal, a provenance edit that
  outdates it, a vanished PSD and an injected commit failure each leave the
  project, its runtime sources, its bytes, its history and the SELECTION unchanged
  with the staging tree removed (N1-N5); every tracked fixture is byte-identical
  after the run, asserted to EXIST before it is hashed (W1); and a real mouse opens
  the modal from the Project window, finds a row in each section, reaches the
  Forget checkbox inside the modal's reserved control column, ticks it, and
  confirms -- with Escape measured UNABLE to close the modal, which is the property
  the `p_open` design rests on (F1-F2):
  `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 5`
- MAR-191's severity-assignment register, the evidence MAR-186 raised, MAR-187
  re-recorded, and both deferred to MAR-191 by name: `check_invariants` recomputes
  its error/warning tallies from the same `issue.severity` values it compares
  against, so a MISASSIGNMENT moves both sides together and no counting case can
  see it. The population is CLOSED rather than sampled -- `make_issue` has exactly
  seven call sites and `diagnostics.cpp:75` is the only severity write in
  `src/editor/`, so the seven literals at `:203, :456, :504, :544, :596, :629,
  :764` are every severity assignment there is. Each was flipped to its opposite,
  rebuilt with objects deleted, run, and restored under a byte `cmp` naming two
  absolute paths; all seven redden -- at MAR-186 `G1`, `G9(e)`, `G8`, `G7`,
  `G9(a)`, `G10 (pure)` and `G10 (dirty session)` respectively -- each on a
  severity or a count clause and none on a message clause. `PreviewStaleSkin`
  (`:629`) is the one worth reading twice: it has NO per-issue severity detector
  and reddens only through G10's aggregate, which the fixture makes asymmetric on
  purpose -- covered, but one fixture edit away from ceasing to be. The register
  measures the FIRST detector by run order and cannot enumerate over-determination,
  because the smoke aborts at its first failure:
  `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- MAR-191's **O1**, the importer's group-record ORDER: a synthesised document whose
  group is written in the inverse order (`lsct` 3 divider first, then the children,
  then the `lsct` 1 folder record) is REFUSED by exact message, while the same three
  layers in the accepted order still import with two grouped under `torso`. The
  refusal is over-determined -- a second guard catches it when the first is disabled
  -- so the clause pins the exact text rather than any-error:
  `./build/marrow_psd_import_smoke assets/fixtures/psd_import_sample.psd assets/fixtures/psd_import_sample_reimport.psd`
- Documented registry totals agree with the registry itself -- present-tense
  claims in `AGENTS.md` and the three `docs/root1` documents are compared against
  a count **computed from** `src/editor/agent_dispatch.cpp`, never a literal, and
  the historical `64` residue is pinned by count in all eight document/literal
  pairs including the zeros: `python3 tools/docs/check_registry_claims.py .`
- Focused CTest guardrail discovery: `ctest --test-dir build -N`
- Focused CTest guardrail: `ctest --test-dir build --output-on-failure`
- Runtime-labeled CTest guardrail: `ctest --test-dir build --output-on-failure -L runtime`
- Editor-labeled CTest guardrail: `ctest --test-dir build --output-on-failure -L editor`
- Debug display qualification configure: `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON`
- Display qualification build: `cmake --build build-display`
- Windowing display tests: `ctest --test-dir build-display --output-on-failure -L windowing`
- Renderer/display tests: `ctest --test-dir build-display --output-on-failure -L display`
- Renderer CPU/GPU link-boundary guard: `ctest --test-dir build --output-on-failure -R marrow.renderer_link_boundary`
- Release platform qualification configure: `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON`
- Release platform qualification build: `cmake --build build-platform-release`
- Release platform qualification suite: `ctest --test-dir build-platform-release --output-on-failure`
- Windows VS2022 x64 configure: `cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DMARROW_ENABLE_DISPLAY_TESTS=ON`
- Windows Debug build/test: `cmake --build build-msvc --config Debug` then `ctest --test-dir build-msvc -C Debug --output-on-failure`
- Windows Release build/test: `cmake --build build-msvc --config Release` then `ctest --test-dir build-msvc -C Release --output-on-failure`
- Windows portable folder/ZIP staging: `cmake --build build-msvc --config Release --target marrow_portable_stage`
- Editor shell frame-body agreement check (the two hand-duplicated frame bodies must draw the
  same windows; runs automatically POST_BUILD on `marrow_editor_shell`, and on demand here):
  `cmake --build build --target marrow_frame_body_check`
- Constraint warning check: `cmake --build build --target marrow_constraint_warning_check`
- Documentation build (requires Doxygen on `PATH`): `cmake --build build --target marrow_docs`
- Release benchmark configure: `cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release`
- Release benchmark build: `cmake --build build-bench --target marrow_benchmark`
- Parameter/deformer benchmark: `./build-bench/marrow_benchmark --parameter-deformers --skeletons 200 --frames 240`
- Runtime math unit tests: `./build/marrow_unit_tests`
- Stress harness benchmark (100 synthetic medium skeletons by default): `./build-bench/marrow_benchmark`
- Constraint performance acceptance benchmark: `./build-bench/marrow_benchmark --frames 240 --samples 5`
- Current validated default stress metrics on this host: `frame_ms=1.94`, `score=100`, `animation_us=1.53`, `transform_us=0.00`, `skinning_us=0.04`, `constraint_us=13.81`, `render_us=0.00`, `max_skeletons_60fps=858.35`
- Default stress before/after comparison (original profiling baseline from the runtime performance brief vs the current validated MAR-104 acceptance run on this host):

| Metric | Original profiling | Current validated | Target | Status |
| --- | ---: | ---: | ---: | --- |
| Animation us/skeleton | 81.00 | 1.53 | <30.00 | PASS |
| Skinning us/skeleton | 79.00 | 0.04 | <5.00 | PASS |
| Constraint us/skeleton | 56.00 | 13.81 | <25.00 | PASS |
| Render us/skeleton | 12.00 | 0.00 | <12.00 | PASS |
| Transform us/skeleton | 4.00 | 0.00 | <4.00 | PASS |
| Total us/skeleton | ~232.00 | ~15.38 | <76.00 | PASS |

- MAR-104 brief reference: the story estimated `59us * 0.55 ~= 32us`; the validated acceptance run now measures `constraint_us=13.81`.
- Stress harness benchmark with custom skeleton count and bone complexity: `./build-bench/marrow_benchmark --skeletons 150 --bones 96`
- Stress harness benchmark with an active synthetic clip stack: `./build-bench/marrow_benchmark --skeletons 150 --bones 96 --clips`
- Idle constraint dirty-skip benchmark: `./build-bench/marrow_benchmark --skeletons 200 --constraint-drive idle`
- Partial constraint dirty-skip benchmark: `./build-bench/marrow_benchmark --skeletons 200 --constraint-drive partial`
- Release 60fps target validation for 200 medium skeletons: `./build-bench/marrow_benchmark --skeletons 200`
- Benchmark timing note: run `marrow_benchmark` commands without concurrent build/test workloads; parallel renderer/test activity can perturb the profiler-overhead guard.
- Current validated 200-skeleton release metrics on this host: `frame_ms=4.45`, `score=100`, `animation_us=1.98`, `transform_us=0.07`, `skinning_us=1.17`, `constraint_us=15.12`, `render_us=0.00`, `max_skeletons_60fps=749.03`
- 200-skeleton before/after comparison (original profiling baseline from the MAR-099 story brief vs the current validated release run on this host):

| Metric | Original profiling | Current validated | Target | Status |
| --- | ---: | ---: | ---: | --- |
| Animation us/skeleton | 81.00 | 1.98 | <30.00 | PASS |
| Skinning us/skeleton | 79.00 | 1.17 | <5.00 | PASS |
| Constraint us/skeleton | 56.00 | 15.12 | <25.00 | PASS |
| Render us/skeleton | 12.00 | 0.00 | <12.00 | PASS |
| Transform us/skeleton | 4.00 | 0.07 | <4.00 | PASS |
| Total us/skeleton | ~232.00 | ~18.34 | <76.00 | PASS |

- Current validated clip-stack stress metrics on this host: `clips=1`, `break_clip=150.00`, `skinning_us=1.10`, `frame_ms=5.65`, `score=100`
- SoA/SIMD bone propagation benchmark: `./build-bench/marrow_benchmark --simd-propagation --bones 1024`
- Current validated SIMD propagation metrics on this host: `path=neon`, `world_bytes_per_bone=24`, `speedup=1.93x`
- Animation-layer overhead benchmark (walk + breathing additive + aim override): `./build-bench/marrow_benchmark --animation-layers --skeletons 400 --bones 128 --frames 360`
- Runtime visibility culling + update-throttling stress benchmark: `./build-bench/marrow_benchmark --runtime-stress assets/fixtures/player_idle.mskl`
- Bootstrap smoke test: `./build/marrow_bootstrap`
- Runtime fixture smoke test: `./build/marrow_fixture_smoke`
- Concurrent shared-SkeletonData runtime stress test: `./build/marrow_thread_stress assets/fixtures/player_idle.mskl`
- ThreadSanitizer configure for the concurrent runtime stress target: `cmake -S . -B build-tsan -DMARROW_ENABLE_THREAD_SANITIZER=ON`
- ThreadSanitizer build for the concurrent runtime stress target: `cmake --build build-tsan --target marrow_thread_stress`
- ThreadSanitizer concurrent runtime stress validation: `./build-tsan/marrow_thread_stress assets/fixtures/player_idle.mskl`
- Runtime inspection CLI: `./build/marrow_inspect assets/fixtures/player_idle.mskl`
- Runtime binary inspection CLI: `./build/marrow_inspect assets/fixtures/player_idle.mbin`
- Imported Spine runtime inspection CLI: `./build/marrow_inspect assets/fixtures/spine_import_sample.mskl`
- Spine JSON import CLI: `./build/spine_to_marrow assets/fixtures/spine_import_sample.json /tmp/spine_import_sample.mskl`
- Spine JSON import report CLI: `./build/spine_to_marrow --report /tmp/spine_import_report.json assets/fixtures/spine_import_sample.json /tmp/spine_import_sample.mskl && python3 -m json.tool /tmp/spine_import_report.json > /dev/null`
- Spine atlas import CLI: `./build/spine_to_marrow assets/fixtures/spine_import_sample.atlas /tmp/spine_import_sample.matl`
- Skeleton validator CI report: `./build/marrow_validator --skip-render --report /tmp/player_idle_validator.json assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl && python3 -m json.tool /tmp/player_idle_validator.json > /dev/null`
- Skeleton validator selected animation/skin sample: `./build/marrow_validator --skip-render --animation idle --skin default --time 0.2 --report /tmp/player_idle_idle_validator.json assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl && python3 -m json.tool /tmp/player_idle_idle_validator.json > /dev/null`
- Spine import-report validator integration: `./build/spine_to_marrow --report /tmp/spine_import_report.json assets/fixtures/spine_import_sample.json /tmp/spine_import_sample.mskl && ./build/spine_to_marrow assets/fixtures/spine_import_sample.atlas /tmp/spine_import_sample.matl && ./build/marrow_validator --skip-render --import-report /tmp/spine_import_report.json --report /tmp/spine_import_validator.json /tmp/spine_import_sample.mskl /tmp/spine_import_sample_hero_page.matl && python3 -m json.tool /tmp/spine_import_validator.json > /dev/null`
- Spine JSON + atlas importer smoke test (includes curve, weighted-mesh pruning, owl zero-weight weighted-mesh, and tank weighted-clipping regressions): `./build/marrow_spine_import_smoke assets/fixtures/spine_import_sample.json assets/fixtures/spine_import_sample.atlas`
- Official Spine 4.2 example JSON import batch (owl, goblins, spineboy, tank, raptor): `mkdir -p /tmp/marrow-mar113-batch && for asset in owl goblins spineboy tank raptor; do ./build/spine_to_marrow assets/spine-examples/$asset/$asset-pro.json /tmp/marrow-mar113-batch/$asset.mskl || exit 1; done`
- Official Spine 4.2 example atlas import batch: `for asset in owl goblins spineboy tank raptor; do ./build/spine_to_marrow assets/spine-examples/$asset/$asset.atlas /tmp/marrow-mar113-batch/$asset.matl || exit 1; done`
- Official Spine 4.2 example metadata inspection batch: `for asset in owl goblins spineboy tank raptor; do echo "== $asset =="; ./build/marrow_inspect /tmp/marrow-mar113-batch/$asset.mskl | sed -n '1,3p' || exit 1; done`
- Validated Spine 4.2 example counts on this host: `owl bones=20 slots=27 skins=1 animations=6; goblins bones=21 slots=23 skins=3 animations=1; spineboy bones=67 slots=52 skins=1 animations=11; tank bones=115 slots=200 skins=1 animations=2; raptor bones=76 slots=36 skins=1 animations=5`
- Official Spine 4.2 example runtime smoke batch: `for asset in owl goblins spineboy tank raptor; do ./build/marrow_fixture_smoke /tmp/marrow-mar113-batch/$asset.mskl /tmp/marrow-mar113-batch/$asset.matl || exit 1; done`
- Official Spine 4.2 example setup-pose renderer prep batch: `for asset in owl goblins spineboy tank raptor; do ./build/marrow_renderer_sample --skip-render /tmp/marrow-mar113-batch/$asset.mskl /tmp/marrow-mar113-batch/$asset.matl || exit 1; done`
- Imported Spine 4.2 example headless renderer note: `./build/marrow_renderer_sample --auto-close 2 /tmp/marrow-mar113-batch/owl.mskl /tmp/marrow-mar113-batch/owl.matl` reaches setup-pose preparation, then fails in this sandbox with `Failed to create a Metal device for the headless renderer`; use a Metal-capable interactive host to visually confirm rendered setup poses.
- PSD layer import + re-import smoke test: `./build/marrow_psd_import_smoke assets/fixtures/psd_import_sample.psd assets/fixtures/psd_import_sample_reimport.psd`
- C API smoke test: `./build/marrow_c_smoke`
- Binary fixture regeneration: `./build/marrow_inspect --export-binary assets/fixtures/player_idle.mbin assets/fixtures/player_idle.mskl`
- JSON vs quantized binary runtime comparison with error and size stats: `./build/marrow_inspect --compare assets/fixtures/player_idle.mbin assets/fixtures/player_idle.mskl`
- AnimationState, skin, inherit timeline, non-uniform inherit modes, skin-scoped constraint, linked-mesh, weighted-mesh, FFD deform, IK, path/transform, and physics runtime validation: `./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl`
- Quantized binary runtime smoke validation: `./build/marrow_fixture_smoke assets/fixtures/player_idle.mbin assets/fixtures/player_idle.matl`
- Rendering validation target: `./build/marrow_renderer_sample`
- Interactive sokol_gfx region-attachment validation: `./build/marrow_renderer_sample assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl`
- Headless renderer smoke validation: `./build/marrow_renderer_sample --auto-close 2 assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl`
- Renderer HUD/report validation without window startup: `./build/marrow_renderer_sample --hud --skip-render assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl`
- Headless renderer HUD validation on Metal-capable hosts: `./build/marrow_renderer_sample --hud --auto-close 2 assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl`
- Atlas texture decode, UV sampling, white-fallback validation, streaming VBO batch merging, and draw-call logging without window startup: `./build/marrow_renderer_sample --skip-render assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl`
- Setup-pose, clipping-mask, sequence-attachment, animated slot-timeline, GPU-skinned weighted-mesh, and FFD deform validation: `./build/marrow_renderer_sample assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl`
- Slot blend-mode, straight-alpha/PMA two-color tint, and framebuffer blend smoke validation: `./build/marrow_renderer_sample assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl`
- Sokol shader regeneration on supported host platforms: `cmake --build build --target marrow_renderer_shaders`
- Editor project load + undo/redo validation: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- Editor project creation validation: `./build/marrow_project_smoke --create /tmp/player_idle.marrow`
- Parameter project/runtime/preview/export validation: `./build/marrow_parameter_project_smoke assets/fixtures/parameter_face_basic.marrow`
- Parameter JSON vs binary comparison: `./build/marrow_inspect --compare /tmp/marrow_parameter_face_basic.mbin /tmp/marrow_parameter_face_basic.mskl`
- Editor runtime export validation for transform, deform, draw-order, event, and constraint edits: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl`
- Editor runtime asset bundle export validation with optional binary output: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl --export-binary /tmp/player_idle_project_export.mbin`
- Editor atlas packer validation from 24 individual sprite PNGs through runtime and renderer load: `./build/marrow_atlas_packer_smoke`
- Atlas-pack fixture project export validation: `./build/marrow_project_smoke assets/fixtures/atlas_pack_smoke/atlas_pack_project.marrow --export-runtime /tmp/atlas_pack_project_export.mskl`
- Project-export weighted mesh, FFD, and constraint round-trip validation: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl`
- End-to-end sample project export/load validation: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl --export-binary /tmp/player_idle_project_export.mbin`
- End-to-end exported project JSON vs binary comparison: `./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin /tmp/player_idle_project_export.mskl`
- End-to-end exported project render validation: `./build/marrow_renderer_sample /tmp/player_idle_project_export.mskl /tmp/player_idle.matl`
- Exported JSON vs binary project bundle comparison: `./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin /tmp/player_idle_project_export.mskl`
- AI Agent Control (MCP) launch:
  1. Start Maroow with agent port: `./build/marrow_editor_shell --agent-port 9876`
  2. Start MCP server: `source tools/mcp/venv/bin/activate && python3 tools/mcp/server.py`
  3. Test end-to-end: `source tools/mcp/venv/bin/activate && python3 tools/mcp/test_client.py`
- MCP schema syntax validation: `tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py`
- Agent registry validation (66 operations, including parameter, animation-duration, timeline-interpolation, timeline-curve-mode, timeline-loop-boundary, timeline key-time scaling, mesh weight rebind, deterministic automatic weight generation, and constraint rename/delete authoring): `./build/marrow_agent_dispatch_smoke`
- Parameter Agent/MCP E2E: start `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --agent-port 9876`, then run `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only`
- Editor shell launch: `./build/marrow_editor_shell`
- macOS launch-focus regression check: `./build/marrow_editor_shell --verify-launch-focus`
- Editor shell smoke validation for viewport FBO/docking/bone picking, onion skinning, independent debug overlay toggles (bones, IK, path, physics, mesh wireframe, bounds), the runtime performance HUD overlay, timeline, clip-duration live editing/queue boundary/clamp/reject, draw-order, event, state-preview, attachment-local multi-vertex FFD auto-key, shared world-grid/local-angle/absolute-scale transform snapping, FFD world-grid/magnetic-vertex snapping, live Alt/Cmd/Ctrl modifiers, deform, brush-based mesh weight painting with Paint/Erase/Smooth/Replace, the active-vertex numeric influence table, selected-scope Normalize, setup-pose Rebind and the candidate-bone checklist with deterministic automatic weight Generate, transient preview playback speed, constraint authoring preview, the constraint rename/delete lifecycle, the eleven IK/physics constraint parameter widgets located by a real mouse through `HoveredId` with per-drag undo granularity and a Ctrl+click clamp, MAR-178's Rename.../Delete... buttons and their modals driven end to end by that same mouse (see **Headless Frame Smoke Notes** before writing another such scenario), and runtime asset hot-reload: `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- Parameter Modeling shell validation: `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2`
- Native macOS launch-focus note: sandboxed SDL/AppKit startup can stall after `com.apple.hiservices-xpcservice` LaunchServices/XPC errors; use an interactive macOS session to visually confirm that `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow` comes to the front and appears in Cmd+Tab.
- MAR-182 window-close veto (MANUAL, not reachable from any headless test): `shell_main.cpp`'s two lines of loop glue -- the `absorb_close_request` call and the `cancel_close_request()` it guards -- run only in the real main loop, which `run_headless_smoke` returns before ever reaching. On an interactive host, dirty a project, click the window's close button, and confirm the `Unsaved Changes` prompt appears and that `Cancel` leaves the window open. Omitting the call breaks the **window close button, Cmd+Q and `SDL_EVENT_QUIT`** and nothing else: `File > Quit` still reaches `begin_session_intent` and still sets `ShellState::should_exit`, so the editor stays closable from the menu. A loud failure rather than a silent data-loss one, but a narrower one than an earlier revision of this line claimed when it said the editor would be *unclosable*.
- MAR-119 E2E editor validation: `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 5`
- MAR-119 E2E export round-trip: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_e2e_export.mskl --export-binary /tmp/marrow_e2e_export.mbin`
- MAR-119 E2E exported runtime smoke: `./build/marrow_fixture_smoke /tmp/marrow_e2e_export.mskl /tmp/player_idle.matl`
- MAR-119 E2E exported file inspection: `./build/marrow_inspect /tmp/marrow_e2e_export.mskl`
- MAR-119 E2E JSON vs binary comparison: `./build/marrow_inspect --compare /tmp/marrow_e2e_export.mbin /tmp/marrow_e2e_export.mskl`
- Fixture skeleton inspection: `python3 -m json.tool assets/fixtures/player_idle.mskl > /dev/null`
- Linked-mesh deform inheritance fixture inspection: `python3 -m json.tool assets/fixtures/linked_mesh_deform_inheritance.mskl > /dev/null`
- IK fixture inspection: `python3 -m json.tool assets/fixtures/ik_constraints.mskl > /dev/null`
- Inherit timeline + skin-scoped constraint fixture inspection: `python3 -m json.tool assets/fixtures/skin_inherit_constraints.mskl > /dev/null`
- Non-uniform inherit-mode fixture inspection: `python3 -m json.tool assets/fixtures/inherit_modes_nonuniform_scale.mskl > /dev/null`
- Path/transform fixture inspection: `python3 -m json.tool assets/fixtures/path_transform_constraints.mskl > /dev/null`
- Physics fixture inspection: `python3 -m json.tool assets/fixtures/physics_constraints.mskl > /dev/null`
- Spine importer fixture inspection: `python3 -m json.tool assets/fixtures/spine_import_sample.json > /dev/null`
- Imported Spine runtime fixture inspection: `python3 -m json.tool assets/fixtures/spine_import_sample.mskl > /dev/null`
- Imported Spine atlas page fixture inspection: `python3 -m json.tool assets/fixtures/spine_import_sample_hero_page.matl > /dev/null`
- Imported Spine atlas second-page fixture inspection: `python3 -m json.tool assets/fixtures/spine_import_sample_fx_page.matl > /dev/null`
- Fixture atlas inspection: `python3 -m json.tool assets/fixtures/player_idle.matl > /dev/null`
- Fixture editor project inspection: `python3 -m json.tool assets/fixtures/player_idle.marrow > /dev/null`
- Parameter face fixture inspection: `python3 -m json.tool assets/fixtures/parameter_face_basic.mskl > /dev/null`
- Parameter project fixture inspection: `python3 -m json.tool assets/fixtures/parameter_face_basic.marrow > /dev/null`
- Parameter deformer fixture inspection: `python3 -m json.tool assets/fixtures/parameter_deformer_grid.mskl > /dev/null`
- Parameter expression/lip-sync fixture inspection: `python3 -m json.tool assets/fixtures/parameter_expression_lipsync.mskl > /dev/null`
- ArtPath fixture inspection: `python3 -m json.tool assets/fixtures/art_path_stroke.mskl > /dev/null`
- Parameter face renderer preparation: `./build/marrow_renderer_sample --skip-render assets/fixtures/parameter_face_basic.mskl assets/fixtures/parameter_face_basic.matl`
- Parameter deformer renderer preparation: `./build/marrow_renderer_sample --skip-render assets/fixtures/parameter_deformer_grid.mskl assets/fixtures/parameter_face_basic.matl`
- Atlas-free ArtPath renderer preparation: `./build/marrow_renderer_sample --no-atlas --skip-render assets/fixtures/art_path_stroke.mskl`
- Use `./build/marrow_renderer_sample` to verify atlas-backed setup-pose region draw preparation, clipping-mask propagation, sequence frame selection, GPU-skinned weighted-mesh draw preparation, animated slot presentation, slot blend modes, straight-alpha/PMA two-color tint propagation, and the single-color shader fast path from the checked-in fixtures

## Headless Frame Smoke Notes

Hard-won mechanics for anyone writing a scenario that renders real ImGui frames
and drives a synthesized mouse or keyboard (`shell_smoke_frames.cpp`,
`shell_smoke_constraints.cpp`). Each was measured, not inferred, and each
produces a silently wrong test rather than a loud failure if you get it wrong.

- **Locating a widget.** `ImGuiContext::HoveredId` names whatever the cursor is
  over; compare it against `ImGui::FindWindowByName(title)->GetID(label)` while
  sweeping the mouse. This only holds while the widget is emitted at plain
  window scope — `widgets::seg_toggle` has a balanced `PushID`/`PopID` and the
  constraint panels close every `BeginChild` before their parameters, so it
  holds there today. A `BeginTabItem`, a surviving `PushID` or a wrapping child
  breaks the seed and the sweep then reports the widget "absent". Always sweep
  at least one control that already exists, so a broken seed cannot be mistaken
  for a missing widget.
- **A widget on a `SameLine()` needs its own sweep column.** A left-edge column
  reaches checkboxes and sliders but not the second button of a row —
  `Delete...` sits right of `Rename...` and is missed entirely by a column at
  the item's left edge.
- **On macOS, `io.AddKeyEvent(ImGuiMod_Ctrl, true)` does not press Ctrl.**
  `io.ConfigMacOSXBehaviors` defaults on, and `AddKeyAnalogEvent` swaps Cmd and
  Ctrl at the EVENT layer, so the flag raises `io.KeySuper`; `AddMouseButtonEvent`
  then converts the left press into a **right** click. Measured as
  `keyctrl=0 super=1 mdown=0 mdown1=1 active=0` — the widget never activates.
  Clear `ConfigMacOSXBehaviors` for the gesture and restore it, and assert
  `ImGuiContext::TempInputId` before typing so a regression says "Ctrl+click did
  not open the text input" instead of reporting a bare wrong value.
- **Two presses at the same pixel are a double click**, and ImGui turns a
  double-clicked `DragScalar` into a text input instead of dragging it. A
  repeated gesture in one scenario hits this: the second drag left the widget
  active as a temp input with the value unchanged. Advance the simulated clock
  (`io.DeltaTime`) past `io.MouseDoubleClickTime` between gestures.
- **A modal stays open until something calls `CloseCurrentPopup()`.**
  `draw_constraint_catalog_popups()` calls `BeginPopupModal` unconditionally, so
  clearing the surface's own request state does not close it, and the open modal
  then blocks every later click in the scenario. Drive the modal's own button —
  a popup is an ordinary window, so `FindWindowByName("Title##suffix")` plus the
  same `HoveredId` sweep works on it — and check `window->Active` to tell an
  open modal from a stale one.
- **`window->GetID(label)` is the wrong seed if anything pushed an ID.** The
  Timeline window draws its dopesheet inside
  `BeginTabBar("timeline_views")` + `BeginTabItem("Dopesheet")`, and
  `BeginTabItem` **pushes the tab's id**, so every widget below it is hashed
  against that and not against the window root. A sweep seeded with
  `FindWindowByName(kTimelineWindowTitle)->GetID("Mode")` matches nothing and
  reports the widget "absent" — `TabBarCalcTabID`
  (`external/imgui/imgui_widgets.cpp:10033`) hashes the label under the tab
  bar's pushed id. Reproduce the chain instead:
  `ImHashStr(label, 0, ImHashStr("Dopesheet", 0, window->GetID("timeline_views")))`.
  Measured in MAR-185, where the widget was drawn and hoverable the whole time.
- **`ScrollMax` is one frame behind.** It is written in `Begin()`
  (`external/imgui/imgui.cpp:8393-8394`) — but *from* `window->ContentSize`,
  which `Begin()` itself recomputes twenty lines earlier at **`:7995`**, via
  `CalcWindowContentSizes`, from the **previous** frame's `DC.CursorMaxPos`.
  (`End()` at `:8711` never assigns `ContentSize` at all; the only assignments
  anywhere are `:7995` and the `:8022` reset.) So the write is current and the
  **input** is stale, and on the first pass after a layout change it still
  reads `0`. A scroll-into-view loop that breaks on `ScrollMax == Scroll` gives
  up before the window has ever reported its real extent, and the widget below
  the fold is then reported missing. Render a frame first, re-read `ScrollMax`
  every iteration, and keep a fallback for the `0` case.
- **Forcing focus closes an open popup.** `SetWindowFocus(parent)` inside a
  per-frame render helper dismisses a combo popup — `FocusWindow`
  (`imgui.cpp:13553`) calls `ClosePopupsOverWindow` at `:13588` — so an item
  sweep then finds an empty popup and blames the item. Confine `SetWindowFocus`
  to the opening frames.
- **A popup's `InnerClipRect` is not settled on the frame it opens.** Render one
  more frame before capturing the rectangle a sweep will be bounded by,
  otherwise the loop walks straight past the item list. **This is the same
  one-frame-behind mechanism as `ScrollMax` above**: the rect is derived at
  `:8382-8383` from `InnerRect`, which is sized off `ContentSizeIdeal`
  (`:8110`) — the same previous-frame `CursorMaxPos` measurement. The two
  entries share one cause, and understanding that lag explains both.
- **A UI-free helper cannot observe a deleted widget.** Calling the function a
  button calls asserts the handler, not the button; such a test passes unchanged
  after the widget is removed. When "the widget is on screen" is the deliverable,
  the frame is the only mechanism that can see it — demonstrated by deleting
  `draw_constraint_catalog_buttons()`'s body, which left MAR-178's own scenario
  printing its full success line while the frame smoke failed by name.
- **Escape does not close a MODAL, so "close the dialog" is not a free path.**
  `NavUpdateCancelRequest` (`external/imgui/imgui.cpp:14844`) reaches its
  popup-closing arm only under
  `!(g.OpenPopupStack.back().Window->Flags & ImGuiWindowFlags_Modal)` (`:14873`),
  and `BeginPopupModal` sets `ImGuiWindowFlags_Modal` unconditionally at
  `:13103`. A non-modal popup closes on Escape; a modal never does. The path
  exists **only if the modal is given a `bool*`**: `BeginPopupModal(name, p_open,
  flags)` forwards it to `Begin` (`:13104`), which draws a title-bar close
  control, and calls `ClosePopupToLevel` when it goes false (`:13105-13109`).
  Measured while planning MAR-190, whose AC3 requires *"modal close"* as a path
  distinct from *"cancel"* — **without the `bool*` that criterion is not failed,
  it is unimplementable**, and nothing would have said so. Combine with the
  existing entry above: a modal stays open until something calls
  `CloseCurrentPopup()`, and an open modal blocks every later click in the
  scenario. If you add a modal and a test must close it by a route other than its
  own Cancel button, pass the `bool*` when you write it, not when a case needs it.

## Repo facts that outlive their story

Measured facts that a future story will need and that are **not** discoverable
by reading the build files or the source. Each was established by building or
running, not by inference, and each is recorded here rather than inside the
story that found it, because the story's section is not where the next person
will look.

### Adding a value to an enum: the compiler helps with switches and nothing else

**`-Wswitch` is ON, by default, with no flag.** `grep -rn
"Wall\|Wswitch\|Wextra" CMakeLists.txt` returns **nothing**, and `-Werror`
appears once at `:352` scoped to `marrow_constraint_warning_check` — from which
it is natural, and **wrong**, to conclude that adding an enum value produces no
diagnostics. Clang enables `-Wswitch` without `-Wall`. MAR-185 settled this by
adding a throwaway `TimelineKeyKind` value, deleting **every** object file and
rebuilding **all** targets: **25 warnings** (18 `authoring.cpp`, 6
`agent_handlers_editing.cpp`, 1 `timeline_controller.cpp`), against **0** on a
pristine rebuild.

**The probe must build every target, and this is the part that is easy to get
wrong.** MAR-185's first probe used `cmake --build build --target marrow_editor`
and measured 24 — it could not see `timeline_key_kind_carries_easing`
(`timeline_controller.cpp`) at all, because that file belongs to
`marrow_editor_shell`. The site was found and fixed anyway, by building the
shell separately, but a probe scoped to one target **understates its own
checklist** and will silently hand the next person a short list. Use:

```
find build/CMakeFiles -name '*.o' -delete
cmake --build build -j8 2>&1 | grep Wswitch
```

**The `/CMakeFiles` in that path is load-bearing, and the unscoped form is a
trap.** `find build -name '*.o' -delete` also deletes **vendored SDL3's**
objects. `libSDL3.a` is then re-created incrementally at **96 bytes / 1 object**,
which produces a link failure and `ctest` **20/22** — with the story's sources
provably untouched. MAR-186's review nearly reported that as a two-test
regression of its own change; deleting `libSDL3.a` and rebuilding restored it to
250 objects and 22/22.

The general lesson is the mirror of one already recorded here: **a RED run is
evidence only once the build is known sound**, exactly as *"no hits is evidence
only once the command is known to have executed."* Both are the same mistake —
trusting an output without validating the harness that produced it — and this
chain has now been bitten by it in both directions.

So, when you add a value to an enum that is switched on:

- **On Clang, every exhaustive `switch` is found for you, in every target you
  build — as a WARNING that does not stop the build.** Add the value, delete
  **all** object files, build **all** targets, and read the warning list. That
  list is complete for switches *only across the targets you actually built* —
  which is why the two "all"s are not decoration. Two qualifiers belong here
  rather than in any one story's section, because this is the paragraph the next
  person will read:
  - **it warns; it does not fail.** Nothing in this tree promotes `-Wswitch` to
    an error. A missed arm ships unless somebody reads the output. The recipe, if
    you want it, is `set_source_files_properties(<file> PROPERTIES
    COMPILE_OPTIONS "$<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Werror=switch>")`;
  - **GCC enables `-Wswitch` only under `-Wall`**, and this tree passes no
    warning flags at all. A non-Clang build is therefore **silent** about a
    missed arm. Everything measured above was Apple clang; if this project ever
    builds on Linux CI, that silence is what someone will be relying on without
    knowing it.

**Check the number against arithmetic, not just against a second build.** The 25
is self-checking, and the check would have caught the scoped-probe error without
rebuilding anything: MAR-185 named **14** switch arms as deliberately uninverted
and had **11** switch-site inversions, and 14 + 11 = 25. Per file: 18
`authoring.cpp` (12 uninverted + 6 inverted), 6 `agent_handlers_editing.cpp`
(1 uninverted + I16 + the four `selector.kind` sites), 1 `timeline_controller.cpp`
(uninverted). A probe reporting 24 cannot contain all 14 named arms, because
`timeline_key_kind_carries_easing` has exactly one definition and it is in
`marrow_editor_shell` — the documents contradicted themselves before any second
measurement was taken.
- **Nothing else is.** The compiler is silent for **if/else chains**, for
  **`if constexpr` type chains**, and for **fixed-length N-term lists and sums
  that enumerate one entry per family**. In MAR-185 that was **at least
  sixteen** sites — treat any such number as a **floor**, not a total, because
  the only way to find them is to look:

  *Lists and sums:* `rename_all_timeline_edits` (`authoring.cpp:123`),
  `erase_all_timeline_edits` (`:136`), `set_animation_duration`'s fold list
  (`:2076-2098`), `auto_extend_explicit_animation_durations`' fold list
  (`:2164-2180`), `sort_retimed_timelines` (`:1045`), `clipboard_track_count`
  (`timeline_model.cpp:392`), and `copy_selected_timeline_keys`' `loop_sync`
  scrub loop (`timeline_controller.cpp:2160-2172`).
  *Chains:* `write_scalar_component` (`authoring.cpp:451`),
  `timeline_key_selectors_arg` (`agent_handlers_editing.cpp:801`), the easing
  parser (`:1114-1167`), the curve-mode parser (`:1406-1456`),
  `timeline_lane_selectors_arg` (`agent_dispatch.cpp:375`),
  `timeline_key_selector` (`timeline_controller.cpp:971`),
  `visit_editable_timeline_keys` (`:1048`),
  `visit_existing_project_timeline_keys` (`:1102`),
  `add_timeline_key_at_playhead`'s `if constexpr` chain (`:1199`),
  `copy_selected_timeline_keys` (`:2044`) and `paste_timeline_clipboard`
  (`:2184`).

  **Every line number above was re-derived against the committed tree**, after
  three of them were found stale — they had been copied from measurements taken
  before this story's own edits shifted them.

  **Six of MAR-185's biting inversions lived in this class, and review found
  three defects in it that its own author had missed** — every one of them in
  the class the story had itself identified as compiler-blind:
  `clipboard_track_count` (a six-term sum, live: it broke the paste remap in
  *both* directions), `set_animation_duration`'s fold list (latent), and
  `timeline_lane_selectors_arg` (message-only). Sweep this class by hand, every
  time, and prefer a test that exercises the **consumer** of such a list over
  one that reads the list — none of the three would have been found by reading.
- **A warning tells you a site exists, never what the arm should do.** Several
  arms are correct by fall-through and adding them changes nothing observable;
  others are `void` with no trailing statement and silently write nothing.
  Deciding which is which is still manual, and is what a per-site fall-through
  table is for.

Turning on `-Wall`/`-Wextra` tree-wide remains its own piece of work; MAR-185
did not do it.

### `ResolvedTimelineKey::original_time` is the SELECTOR's time, not the stored one

A 1e-6 seam in the timeline authoring layer, and a real one. `resolve_timeline_key`
(`authoring.cpp:202`) carries `selector.time` **verbatim** into
`ResolvedTimelineKey::original_time` in **all seven** of its arms, while
`resolved_stored_key_time` (`:722`) reads
`timelines[timeline_index].keyframes[key_index].time`. A selector only has to
identify a key within `kKeyTimeEpsilon` (**1e-6**), and a shell selector is
built from track rows whose times have been through `float32`, so the two
values legitimately differ — by ~1.5e-9 for `0.1`, and by up to the full 1e-6
in principle.

`scale_keyframe_times` re-reads the stored time deliberately
(`snapshot.original_time = resolved_stored_key_time(candidate, snapshot)`,
`authoring.cpp:2386`)
because it multiplies, and a live gesture re-derives its selectors every frame.
A retime adds one shared delta and does not care. **If you write a new operation
that computes from `original_time`, decide which of the two you actually mean**
— MAR-185's I13 is the inversion that demonstrates the difference is observable
(`0.10000000149011612` and `1.299999974668026` against `0.1` and `1.3`).

### `serialize_project` is NOT bit-exact for doubles that need 17 significant digits

A `.marrow` round trip is **lossy for a double whose shortest round-trip
decimal form needs the full 17 significant digits.** Measured in MAR-186, on
`mesh_base`/`body`/`body_mesh`'s canonicalized weights:

```
saved    : spine=0.74999999999999989 arm_l=0.25
reloaded : spine=0.75               arm_l=0.25
```

Three of the attachment's four vertices drifted this way across
`save_project` -> `load_project`. A value needing fewer digits survives exactly:
the same probe wrote `{spine, 1e-9}` and read back `1.0000000000000001e-09`,
bit-identical, which is why MAR-186's `weights.uncanonicalizable` case is a real
round trip and its sub-tolerance case is not.

**The reassuring half, also measured:** the reloaded values are still
*canonical* (`0.75 + 0.25` is an exact unit sum in descending order), so the
round trip does **not** manufacture spurious `weights.non_canonical` issues.

**The sharp consequence, which is the part that will save someone:** *any case
that authors a 17-digit weight and round-trips it through a file is asserting on
a different value than it wrote.* MAR-186 hit this twice — a one-ULP
perturbation of `1.0` reads back as exactly `1`, and a canonical baseline drifts
— and both would have produced a confidently passing test measuring nothing.
Author such a value **in memory** and say so in the case, or pick a value whose
short form is exact.

MAR-186 did **not** fix this; it is a pre-existing serializer property, recorded
here rather than in that story's section because the next person to hit it will
not be reading about diagnostics.

### `normalize_mesh_weights` does not validate its target, it CREATES one

`ensure_weight_edit` (`authoring.cpp:4503`) looks up
`find_mesh_weight_attachment_edit(skin, slot, attachment)` and, when it misses,
**pushes a fresh `MeshWeightAttachmentEdit` at those coordinates** seeded from
`mesh_weight_edit_from_runtime`. It never cross-checks the target against the
`AttachmentData&` it is handed alongside it, and neither does
`normalize_mesh_weights`, `set_mesh_vertex_weights` or anything else on that
path. Measured in MAR-187 on `mage`/`body`/`mage_body`:

```
CORRECT target -> ok=1 error='' vertex_count=4 affected=1
SWAPPED target -> ok=1 error='' vertex_count=4 affected=1     <- indistinguishable
mesh_weight_attachment_edits: skin='body' slot='mage' attachment='mage_body'
save_project = 1 ; load_project = 1
diagnostics: overlay.orphan_weight_target|body|mage|mage_body
```

The project still saves and loads, and the only symptom is a **brand-new
`overlay.orphan_weight_target` Warning** where a problem was meant to go away.
`AttachmentSelection` is `{slot, skin, attachment}` and `MeshWeightTarget` is
`{skin, slot, attachment}` -- **transposed** -- so aggregate-initializing one
from the other is the natural way to hit this.

**The consequence for a test:** a `!result` assertion, an "the issue
disappeared" assertion, and any assertion on codes, counts or identities ALL
pass under a swapped target. What sees it is the record's own coordinates, or
`changed`. MAR-187's X4 asserts both.

Pre-existing; MAR-187 did **not** fix it, and confined itself to not calling it
wrongly (it builds the target from `OverlayRecordKey`, whose fields are in the
record's own order, and names every field explicitly).

### A document has two kinds of sentence, and only one of them may be corrected

Every validation record in this file mixes two kinds of claim, and the sweep that
updates one must not touch the other:

- a **present-tense claim** — "this is how you validate the repo", "the registry
  is 66" — which is about the tree as it stands and is simply *wrong* once it
  drifts. `## Current Validation` is entirely this kind;
- a **historical measurement** — "`marrow_agent_dispatch_smoke` prints 408
  `[ OK ]` cases against 64 operations" — which is a per-story record of what was
  observed **at that story's own commit**. Every `## MAR-NNN … Validation
  Results` section is this kind.

**Correcting the second kind falsifies the record.** MAR-185's 64→66 sweep
updated the C++ and the Python and missed prose in two places; MAR-187 was told
to fix both and fixed only the first, because the second is MAR-183's accurate
measurement of its own run. The right repair for a historical line that has
become confusing is to make its anchoring **explicit** — name the commit, and put
today's value beside it — never to overwrite the number.

**Two agents reached this independently, and both landed on the same repair**,
which is why it is a rule here rather than a judgement call in one story's table.
MAR-186's own Tip row reads *"`68720d9` **at Task 0** … It was amended once more
during implementation and MAR-185's final SHA is `711231c`"* — the measurement
kept, the anchoring made explicit, today's value named beside it. MAR-187 applied
that identical shape to MAR-183's 408 without having seen it. MAR-185's D25 had already noticed the same shape from a third direction when
it recorded that its anchor checker's *"remaining flags are all correctly
historical."*

**The missing half, and it is the half that makes the rule safe.** "Do not edit a
historical measurement" has a consequence nobody had stated: **a lesson recorded
only inside story sections can never be generalised in place.** It accumulates as
instances — each one individually unmaintainable, because none may be edited —
and the *n*th instance then reads as a fresh measurement rather than as a
recurrence. Measured while planning MAR-190, on the rule *"a 'no hits' result is
evidence only once the command is known to have run"*: it had been recorded
**twice** inside `## MAR-186` and `## MAR-187` and referenced once as a
parenthetical, and was therefore durable **zero** times. A third instance arrived
with a different cause (a malformed BRE interval rather than zsh glob expansion)
and was nearly read as a new finding instead of the recurrence it was. The
general form now lives at *"A zero result is evidence only once the pattern is
known to match something"*, in this section, where it can be widened without
falsifying anything.

So the two halves work only together. The first without the second quietly
guarantees that recurring lessons stay invisible, since the only places they are
written are the only places that may not be touched.

*Rule, both halves: before "fixing" a number in this file, decide which kind of
sentence it is — if it sits under a `## MAR-NNN` heading it is almost certainly a
measurement, and the edit you want is a clarifying parenthetical, not a new value.
**And when you notice a second instance of the same lesson in a second story
section, lift its general form to `## Repo facts that outlive their story`,
citing both instances.** Leave the originals untouched: the durable entry is the
maintainable one, and the story sections stay the record of what each story
actually saw.*

### A correction relayed from conversation is not a correction to the document

**How a wrong claim enters a document nobody ever measured against the code.** A
reviewer and a lead discuss a case, the lead relays "entry X says Y, narrow it",
and the implementer applies it — to text that does not exist, or that already
says the right thing. Nothing in that chain ever ran `grep`.

Measured instance: MAR-187 was asked to narrow a durable entry called **`F-4`**
whose characterisation of `std::sort` was said to be over-broad.
`grep -rn "F-4"` over `AGENTS.md`, `docs/superpowers/` and the sources resolves
to **exactly one hit: the MAR-187 errors-table row recording this finding.** (That
is worth stating, because a future reader will run that grep, see a hit, and
needs to know it is this record and not the entry being described.) No text
anywhere makes the claim being corrected — while the
record already states the sort's real detectors correctly (MAR-186's I3: deleting
the sort is caught by **G1**, with G9(c) independent). Two further claims in the
same message — that MAR-187 had added an eighth `DiagnosticOverlayFamily` value,
and that `safe_fix.hpp` carried `OverlayRecordKey` — were also inferences from a
diffstat and a summary, and `diff` and `grep -c` refuted both. Applying the first
would have put a phantom eighth identity into G1 and broken a passing assertion.

This is MAR-185's **D24** in a new setting: *a pattern entry whose own examples
cannot be verified is the very failure it describes.* D24's own first draft was
dropped for citing an "R1" that MAR-185 never had.

**A corollary about the evidence itself, which cost this entry two rewrites:
an entry that cites a search as its evidence becomes part of that search's
results.** The line above wanted to read *"`grep -rn "F-4"` returns nothing"* —
and writing that sentence into `AGENTS.md` is what made the grep return
something. Anyone verifying the entry would run the command, see a hit, and
conclude the entry was wrong. The fix is to say *what* the hits are, not how
many there should be. The same trap catches any document that quotes a symbol
name, an error string or a magic number as proof of its absence.

**And the constructive half, which is the part that scales.** "Check
everything" does not: nothing in this chain has the budget for it. What actually
made four wrong premises cost minutes rather than an investigation is that each
one **named a checkable artefact** — an entry id, a diffstat line, an enum
value, a prose string. A claim phrased that way is self-checking: refuting it is
one command. A claim phrased as "the sort keeps the payload stable" or "the
assert protects the list" is not, and those are the ones that survive unexamined
for several stories. So the obligation runs both ways — resolve the claims you
receive, and phrase the claims you issue so that resolving them is cheap.

*Rule: a correction names an artefact, so resolve it in the artefact before
applying it — `grep` the identifier, `diff` the construct. A correction whose
referent does not resolve is a finding to report, not an instruction to follow;
and refusing it is cheaper than the assertion it would have broken. When you
record such a finding, remember that your record joins the corpus it cites.*

### A gate that passes on unchanged code, and its mirror

Two defect classes, and a story is exposed to both. They are recorded here rather
than inside the story that named them, because the whole point is that the next
person checks **both directions** before writing a case.

| Direction | Symptom | How it is found |
| --- | --- | --- |
| **Fails on correct code** | Red before *and* after a correct implementation | Watch it fail, then watch it pass. It gets investigated, because red demands attention |
| **Passes on unchanged code** | Green before the implementation exists | **Only** by running the case against the pristine tree first, or by an inversion. Nothing else surfaces it |

**The second is the more dangerous, precisely because it is quiet.** A failing gate
gets investigated; a passing one does not. A red case is a question; a green case
is an answer nobody asked for.

MAR-186 hit the first (its D2, and an incoming three-vs-four parser-rule error).
MAR-188 hit the second, and hit it **twice**:

- **The storage layer pre-armed it.** `$.editor.import_sources.psd` already
  round-tripped verbatim through `preserved_root` before MAR-188 wrote a line, so
  an AC1 round-trip case written as a text search over `serialize_project()`
  would have passed on an empty commit. The countermeasure is structural, not
  vigilance: **assert the typed struct, never a substring of the serialization.**
- **The design document then committed the defect against its own case**, in the
  very section that defines the class: it claimed P1 "earns its place only through
  inversion I3" while both inversion registers attributed I3 to P4. P1 was green
  on the pristine tree and no inversion of twenty turned it red.

The rule that came out of it: **a case that is green before the implementation
exists is a WITNESS, not a gate.** Label it as one, and either name the inversion
that catches its subject or state plainly that none does. MAR-188's P1 is kept as
a compatibility witness -- a pre-story project serialising byte-identically is a
real claim nothing else makes -- with "no story-owned inversion" recorded rather
than repaired. Never read such a case being green as vindication of anything.

**A seam asserted only in its success direction is untested for its purpose.**
MAR-189 shipped a rollback failpoint that was declared, wired, and never fired:
`rollback_error` appeared in exactly **two** assertions before this was noticed
(`psd_import_smoke.cpp` at `7462f67`) and both asserted it **empty**. A seam exists to make a failure observable, so "no failure was
observed" is the one result that cannot validate it -- the same shape as a gate
green before the implementation exists, one level up from a case to a mechanism.
Its case (**R6b**) and the mutation that reddens that case alone (**I18b**) were
both added after the gap was noticed. *When you add a seam, write the case that
makes it fire before you write the case that checks it stayed quiet.*

**And check a "did not bite" before believing it.** MAR-188's I7 was a *provable
no-op*: it mutated `source` to read from `result`, but `result = project` is a
copy, so `source` and `target` aliased the same object and neither field was read
after being written. A mutation whose effect cannot be observed is not a weak
inversion; it is not an inversion at all, and recording "I7 did not bite" without
that reasoning would have told the next reader nothing.

### An AGGREGATE is blind to any mutation that PERMUTES its inputs rather than changing them

Three instances in this chain, each found the same way -- by running the gate
against the mutation it was written for, not by reading it.

| Aggregate | Invariant under | Instance |
|---|---|---|
| a **count** | a **move** | **MAR-190's S1.** It counted call sites of `draw_psd_reimport_modal(` under `src/editor/`. Its own inversion **I12** *moves* the call from `draw_project_window` into `render_shell_frame` -- both under `src/editor/` -- so the count stayed at exactly **1** and the gate reported green on the single mutation it existed to detect |
| a **sum** | a **swap** | **MAR-186's G10 / MAR-191's V6.** Its failure text says it outright: *"a one-of-each fixture cannot see the two accumulators swapped."* It bites only because the fixture was deliberately made asymmetric |
| a **set** | **reordering and duplication** | **MAR-187's frame-body gate.** `CheckFrameBodies.cmake` compares the two bodies with a set difference (`_missing_from_smoke` / `_missing_from_app`), so it cannot see a window drawn twice or the draw order changing. **Recorded as an observation, not a defect**: for *"do both lists mention the same windows"* a set may be exactly the right comparison, and whether multiplicity or order matter there is a real question rather than an assumed bug |

**The remedy is the same in all three: assert the STRUCTURE, not the aggregate.**
S1 gained the file the surviving line must be in; G10 needed an asymmetric fixture;
the frame-body gate would need multiplicity or order if either turned out to
matter.

*Rule: before trusting a gate that reduces its input to a number or a set, ask
what rearrangement of the input leaves that reduction unchanged -- and check
whether the mutation you are guarding against is exactly such a rearrangement.*
**A gate is a hypothesis until it has been run against the mutation it names.**
S1 needed restating twice, and both times running it rather than reading it is
what exposed the gap.

### A case that observes its own INPUT instead of the system's OUTPUT

Not the vacuous shape and not the self-satisfying one: the case does real work,
asserts a real value, and compares it against something **it computed itself** --
never reading the state the system actually built. A green result then means only
that the case agrees with itself.

**Measured in MAR-190.** The frame case F1 grouped `fixture.plan` -- the plan the
CASE had prepared -- while the production button path builds its own plan from the
project. `fixture.plan` was default-constructed, so F1 reported **0 Updated / 0
Missing while the modal on screen was drawing rows**. Nothing was broken; the case
was looking at the wrong object. The fix is to read
`state.psd_reimport.review->plan`, the plan the modal is actually showing.

*Rule: read the state the SYSTEM built, never the state you handed it.* This is the
failure a frame case is most likely to have while looking entirely correct, because
every line of it is doing something real.

### MASKING: a later step overwrites what the mutation changed, so the inversion reports "did not bite"

A sixth degenerate shape, and it is none of the five already catalogued -- not
*fails on correct code*, not *passes on unchanged code*, not a *provable no-op*,
not *self-satisfying*, not a *misplaced invariant*.

**The mutation IS applied and DOES change behaviour.** A later step then
overwrites the state it affected, so both variants produce identical observable
results. The inversion reports *"DID NOT BITE"*, which reads as **this clause is
redundant** when the truth is **this clause is unobservable from here**. Those two
readings lead to opposite actions: the first says delete the clause, the second
says fix the code around it.

**A provable no-op differs and the distinction is worth holding.** In a no-op the
mutation cannot change anything -- two names for one object, nothing cross-reads.
Under masking the mutation genuinely does something and something else undoes it.
The no-op is a defect in the *inversion*; masking is a defect in the *code's
ordering*, and it is the code that has to move.

**Measured in MAR-190.** A rollback called `revert_last_edit()` (drop the entry)
and then `restore_stashed_redo()` (put the user's branch back). An inversion
swapping in `undo()` -- which PUSHES a redo entry -- did not bite: `restore_stashed_redo`
**assigns** `redo_entries`, so it overwrote whatever the revert had left, and both
spellings produced the same final stack. Reordering to restore FIRST made each
primitive carry its own weight, and the same inversion then reddened the case by
name (redo `1 -> 2`).

**The diagnosis is the part that is not obvious: two CORRECT components in the
wrong order made each other unobservable.** Neither function was wrong. A reviewer
hunting the bug would have read both, found nothing, and concluded the inversion
was weak. Only the sequence was at fault.

- **Suspect masking whenever an inversion aimed at a real behavioural difference
  reports "did not bite".** Before deleting the clause, ask what runs *after* the
  mutated line and whether it assigns -- rather than merges into -- the same state.
- **Comment the ordering at the site.** An order that looks arbitrary and is not is
  exactly what the next refactorer tidies away, and the tidy-up is silent.

### A suite that shares a fixed scratch path is green only while nobody else runs it

Found by review during MAR-189, and the way it was found is the point:
`marrow.psd_import_smoke` **failed once** in a full `ctest`, then passed on an
isolated rerun and three direct reruns. The reviewer investigated instead of
re-running until green.

> **An intermittent failure is evidence about the HARNESS as often as about the
> code, and re-running until green destroys the evidence.** A failure that
> reproduces is a bug you can chase; a failure that does not is a fact about the
> conditions, and the conditions are the thing you have just learned something
> about. The rerun that passes is not a second opinion -- it is the deletion of
> the only run that had information in it.

This is the missing direction of two rules already here. *"A red run is evidence
only once the build is known sound"* and *"a zero is evidence only once the
pattern can match"* both say **do not trust a result until you trust the harness**.
This one says the converse: **a result you distrust is itself information about
the harness**, and the instinct to re-roll is what throws it away. The same
instinct, resisted, is what found the fixture class in the entry below -- an
inversion that "did not bite", investigated rather than recorded.

Every scratch root in `psd_import_smoke.cpp` was a **fixed** path
(`$TMPDIR/{marrow_psd_import_smoke,mar188_q0,mar188_plan,mar189_naming,mar189_commit}`,
`/tmp/mar189_agent`) and the cases `remove_all` their root on entry, so two
concurrent runs delete each other's trees mid-run. Measured: **four concurrent
runs of the pre-fix binary, four failures.** After giving every root a pid
suffix: **eight concurrent runs, eight passes.**

> A recorded pass that depends on nobody else running the same binary is not a
> result. Name every scratch root after the **process**, not after the story.

**And the repair has a second half that is easy to drop, because it was never
written down in the first place: the fixed roots were REUSED, so each run's
`remove_all` on entry disposed of the previous run's tree.** Making the names
unique removed the reuse and therefore removed the disposal -- and nothing
replaced it. Measured before it was noticed: **615 leaked roots, 299 MB**, growing
~2 MB per `ctest` and per inversion, permanently. *Removing reuse removes whatever
was implicitly cleaning up.* Anyone applying "make it per-process" to another fixed
path in this tree will reproduce this unless they add disposal in the same change.

The disposal is an **RAII guard**, and its failure behaviour is a choice rather
than an oversight: it removes the root on success and **keeps it on failure**,
printing the path -- a failing case is exactly when someone wants the tree it
built. *Leaking on failure by design is defensible; leaking on success is not.*
Both directions were demonstrated: a passing run adds **0** roots across all six
names, and a forced failure adds **1** and prints
`scratch kept for inspection: …/mar189_commit-30142`.

This is the sibling of the scratchpad rule already recorded above -- that one is
about two *agents* colliding on a directory, this one about two *processes* -- and
the same fix answers both.

**The part that was not a test problem at all.** Chasing the last concurrent
failure led into production code: `unique_staging_directory`
(`agent_handlers_management.cpp`) appended a counter that is `static` and
therefore **per-process**, under a **fixed** default root. Two editors on one
machine each planning a reimport both choose `<root>/plan-1`, and
`plan_psd_reimport` refuses a staging root that is not empty or absent -- so the
second user's approval fails with *"staging root must be empty or absent"*, for no
reason they can act on. The name now carries the pid. *A "unique" name is unique
only across the scope its uniquing mechanism spans; a process-local counter says
nothing about another process.*

**What the fix does NOT cover, measured rather than assumed.** Two concurrent
`ctest` runs still fail -- but on **other** tests: `marrow.project_smoke`,
`marrow.runtime_unit`, `marrow.agent_dispatch_smoke`, `marrow.editor_shell_smoke`.
`marrow.psd_import_smoke` failed **zero** times across both. `project_smoke` fails
the same way at the pre-fix commit, and `agent_dispatch_smoke` writes to fixed
`/tmp/agent_spine_import_sample.*` paths that predate this story. **Concurrent
`ctest` is flaky repo-wide and it is not this story's doing** -- recorded here
because the next validation sweep over this suite needs to know that "run the
tests twice at once" is not yet a supported operation.

### A non-biting inversion and a bad error message can be the same defect

Two symptoms arrived in MAR-189 as separate review items and turned out to be one
finding. Recorded because neither symptom, alone, identifies what is wrong -- one
reads as a wording problem and the other as an untestable corner:

- **A user-facing message with no good remedy to offer.** The skins refusal told
  the user to *"first remove those attachments from the project's skeleton"* --
  instructing them to destroy, by hand, exactly the data the refusal existed to
  protect.
- **An inversion that did not bite.** Dropping a `> 0U` guard in the prune's
  exclusion left the whole suite green.

The single cause: `if (erase(...) > 0U)` implemented *"the identity the prune
removed"* when the contract is *"the identity the user asked to delete"*, and
those diverge exactly where this importer always lands -- it erases `skins`
wholesale, so the prune never observes a removal. So the code **refused to delete
something the user had explicitly marked for deletion**, and the branch that
should have forgiven it was unreachable.

> **The message was bad because the good remedy was broken; the inversion did not
> bite because the broken code was unreachable.** A refusal whose advice is
> unhelpful is worth reading as evidence about the product, not about the prose --
> the missing remedy may be missing because it does not work.

Fixing it made the refusal message *better* as a side effect, because there was
now a non-destructive route to offer -- which is the tell that the wording was
never the problem.

**And the repair went wrong in a way worth naming: the corrected line was first
placed one `continue` too low**, inside the loop body it had just been diagnosed
out of, minutes after the diagnosis. A contract that must be evaluated **before**
an early exit is exactly the kind that gets reintroduced by the next person
restructuring the loop, so the reason lives in a comment at the site rather than
only in this file.

### A fixture in which two candidate rules agree cannot distinguish them

The three degenerate gate shapes recorded above are all properties of the
**assertion**. This one is a property of the **fixture**, it is invisible to every
review that reads the assertions, and it has now appeared three times in this
chain:

> **If the fixture is built so that the correct implementation and the wrong one
> produce identical output, no assertion over that fixture can ever fail.** The
> defect is not weakly covered; it is *unrepresentable*.

- **MAR-186's G12.** Its first attempt "asserted a count of two on a fixture where
  the count could never have been anything else" -- the collision it meant to
  prove was not constructible in the tokens it chose.
- **MAR-187's I10-bone.** Recorded in that story's own register as *"Did not bite
  before the bone sibling was added to the fixture"*: a bone-blind erase is
  indistinguishable from a correct one until the fixture contains two records that
  differ **only** by bone.
- **MAR-189's scenario bundle.** A PSD import names the atlas and its texture from
  one path (`bundle.matl` / `bundle.png`). The tracked fixture does not:
  `player_idle.matl` declares `"image": "player_fixture.png"`. On a same-stem
  bundle, *"derive the texture from the atlas's stem"* and *"derive it from the
  atlas document's own `image` member"* return the **same answer** -- so every case
  in the story was blind to the one distinction the story exists to get right.

**How the third one was found is the transferable part: an inversion did not
bite, and that was investigated instead of recorded.** The mutation switched the
committer to the stem rule and nothing went red where it should have. "Did not
bite" is a claim about the code; it is just as often a claim about the fixture.
`AGENTS.md` already says to check a "did not bite" before believing it, for the
provably-no-op case -- this is the other reason a mutation can be inert.

**The countermeasure, and it has a confirmation step that must not be skipped:**
build the fixture so the candidate rules *disagree*, then confirm that **a
recorded expectation changes**. MAR-189 renamed the packed texture to
`bundle_tex.png` and repointed the atlas document at it; the evidence that this
was load-bearing rather than cosmetic is that an asserted message had to change
with it (`references 'bundle_tex.png'`, previously `'bundle.png'`). *A fixture
change that alters no expected value has not made anything newly observable.*

The general lesson beyond fixtures: **a fixture generated by the same code path
the production code uses will agree with that code about everything the two derive
together.** Reproduce the property the REAL data has, not the one the generator
happens to produce -- and when the real data is a tracked fixture, go and read it
rather than assuming the generator matches.

### A zero result is evidence only once the pattern is known to match something

The rule *"a 'no hits' result is evidence only once the command is known to have
run"* has been recorded twice inside story sections (MAR-186's and MAR-187's, both
about **zsh glob-expanding an unquoted `--include=*.cpp`** before `grep` ever saw
it) and referenced once as the mirror of *"a RED run is evidence only once the
build is known sound"*. It has never had a durable entry, which is why a third
instance arrived with a different cause and was nearly read as a real measurement.

**The command executing is not sufficient. The pattern must also be able to
match.** Measured while planning MAR-190, against the committed
`tools/inversion/rebuild.sh`, which plainly contains the string:

```
grep -c  'find "${build_dir}/CMakeFiles"' tools/inversion/rebuild.sh   ->  0
grep -cF 'find "${build_dir}/CMakeFiles"' tools/inversion/rebuild.sh   ->  1
```

`grep` ran, exited 1 for "no match", and printed a confident `0`. BSD `grep`
reads `{build_dir}` as a malformed BRE interval, so the pattern cannot match
anything. The probe was a Task 0 gate deciding whether an isolated tree had the
fixed inversion harness; a `0` read at face value would have selected the wrong
branch and silently disabled object deletion for every inversion in the story.

So the widened rule, which subsumes both causes:

> **A zero is evidence only once you have confirmed the pattern matches a
> known-present instance.**

Three mechanisms, one discipline:

- **the shell can eat the pattern** — quote every glob, every `*`, every
  `--include=`;
- **the regex engine can reject it** — `{`, `}`, `+`, `?`, `|`, `(` and `)` all
  mean different things in BRE, ERE and fixed-string mode, and a *malformed*
  construct fails to match rather than erroring. Prefer **`grep -F`** whenever the
  needle is a literal, which is most of the time in this tree, and reach for `-E`
  deliberately rather than by default;
- **the SUBJECT can be split even when the pattern is perfect.** `grep` is
  line-based and C++ concatenates adjacent string literals, so an asserted message
  long enough to wrap is not on any single line. Measured in MAR-189, verifying
  that its own record quoted the case correctly:

  ```
  grep -c "deform timeline references unknown attachment" src/samples/psd_import_smoke.cpp   ->  0
  ```

  while the assertion is plainly there, wrapped as `"… deform timeline "` /
  `"references unknown attachment 'shadow_mesh'"`. The zero was about to be read
  as "the case does not exist". The check that works joins the literals first —
  `python3 -c "import re; print(re.sub(r'\"\s*\n\s*\"','',open(f).read()).count(needle))"` —
  and it returned **1**. This is the mechanism that bites hardest in *this* repo
  specifically, because the house style is long asserted messages and those are
  exactly the strings a reviewer greps for.

  **It has a prose variant, met immediately afterwards while verifying the entry
  above.** A quotation from this very file returned `0` because the sentence wraps
  mid-phrase -- *"the count could never have\nbeen anything else"* -- and the
  joiner used was the C++ one (`"` newline `"`), which does not join wrapped
  prose. `re.sub(r'\s+',' ', text)` returns **1**. So the rule applies to
  `AGENTS.md` itself, which is the document most often searched for exactly this
  kind of quoted evidence: **flatten whitespace before believing a zero about
  prose, and join adjacent literals before believing one about code.**

The cheap countermeasure, and the one this chain should adopt: **run every
"expect zero" gate once against a deliberately planted hit, then remove it.** A
gate that has never returned non-zero has not been shown to be capable of it —
which is the same argument as *"a case that is green before the implementation
exists is a witness, not a gate"*, one section up, applied to a shell command
instead of to a test.

### A stale line anchor fails to resolve. A stale RESTORE TARGET resolves perfectly and destroys work.

The team lead instructed an agent to park its edits by restoring three files from
`git show <sha>:<path>`, naming a SHA that was current when the message was
written. **HEAD moved before the message was read.** One of the three files had
just received another story's required review fix, so the restore would have
reverted it -- leaving a clean tree, a passing suite, and two inversions that had
just been made to fail by name silently passing again. It was caught only because
the story that shipped the fix recognised its own committed guard about to fire
against its own change.

- **Never restore from a copy taken before the current HEAD.** Re-derive from
  `git show <current-HEAD>:<path>` **at the moment of restoring**, and `cmp`
  against **that blob** rather than against the copy.
- **Never pin a SHA inside an instruction that will be executed later.** Name the
  file and say "current HEAD".

**"Stage only your own paths" stops working the moment a path is shared.** The
hazard is another agent's uncommitted **hunks inside a file you own**: MAR-187's
landing was about to overwrite ~493 lines of MAR-188's in-progress work in
`src/samples/editor_project_smoke.cpp`, because git stages whole files. The
technique is **`git apply --cached`** -- stage your own *hunks*, landing the change
as a unified diff proven byte-identical to the tree you actually built and ran,
never as a file copy. Use it whenever two stories hold the same path.

### An editing tool's cached file state is a stale restore target

The entry above is about a stale SHA and a stale line anchor. This is the same
class with **no SHA to be suspicious of and no anchor that fails to resolve**,
which is why it is worse than either.

**The mechanism.** An implementer read `src/samples/psd_import_smoke.cpp`, and
some minutes later edited it with an editing tool. The edit's anchor was correct
and unique, the edit reported success -- and the **write-back used the content the
tool had cached at read time**, silently reverting the rewrite another agent had
committed to that file in between. Nothing in the edit was wrong. The base it was
applied to was.

**The detector is free, and it is the reason this was caught at all.** *On an
append-only edit, a non-zero deletion count in `git diff --numstat` is a SIGNAL
TO INVESTIGATE -- never a verdict.* No judgement, no cost, works every time. Run
it after **every** edit to a file another story is touching, and re-read the file
immediately before each edit rather than relying on a read from earlier in the
task. Better still, on a contended file, make the edit through a tool that reads
from disk at write time.

**"Always a bug" is how this entry was first written, and that phrasing is itself
a hazard.** The same number means two opposite things: *I clobbered someone*, and
*someone else is mid-refactor in a file I share*. Only the first wants a repair.
The story that found this hazard then met a **19-deletion** diff against HEAD an
hour later; its post-incident reflex said *I did it again*, and reconstructing
"HEAD + my hunks" would have **destroyed a live refactor** another agent was in
the middle of. The detector fired correctly and the verdict would have been wrong.

**A free detector with a confident verdict attached becomes a reflex, and a reflex
fires on the false positives too.** So the disambiguator is part of the rule, not
an optional follow-up: **read the deleted lines and establish whether they are
yours** before repairing anything. Deletions that are your own text, or that
replace a construct you can see the other story rewriting, are theirs to keep --
`git log -1 --format=%s` and a glance at the surrounding hunk answer it in
seconds.

**A red run is evidence only once the TREE is known to be what you think it is** --
not merely once the build is sound. This is the sharpening the incident forced.
The clobber made another story's case fail *before* the implementer's own code
ran, and it was nearly filed as a broken tip. The build was sound; the tree was
wrong. The same story then hit the identical symptom from a **second** cause an
hour later (below), and the correct diagnosis differed both times.

**The repair trap, which is the subtle half.** The instinct is to rebuild the file
as `HEAD + my hunks`. That is wrong whenever the other agent has **staged** work:
it discards their staged content a second time, by a different route, and the
first repair attempt here did exactly that. **The correct base for a shared file
is `git show :<path>` -- the INDEX, not HEAD.** Verify afterwards that the result
is a pure insertion: every hunk in the `@@ -N,0 +M,K @@` form and zero deletions.

*Rule: on a contended file, re-read immediately before editing, `git diff --numstat`
immediately after, and repair from the index rather than from HEAD.*

**The family, and what caught every member of it.** MAR-190 hit four distinct
routes to *a green or red run that describes a tree you are not looking at*: an
editing tool's cached write-back, `git archive`'s commit-time mtimes, a tree
assembled from two reads of "HEAD", and a regenerated file left stale when its
anchor stopped resolving (build and suite both **green on a file 132 lines behind
HEAD**). **Every one was caught by an assertion about the TREE, not about the
product** -- a deletion count, an mtime, a pristine rebuild of the same SHA, a
`deletions vs HEAD` check. None of the product's own assertions could see any of
them, because from the suite's point of view nothing was wrong. Budget a cheap
tree-assertion beside every verification run; the product's assertions cannot
cover this class even in principle.

### `git archive | tar -x` restores commit-time mtimes, so an existing build tests stale code

Measured in MAR-190 immediately after the entry above, from the identical
symptom -- another story's case failing before this story's code ran -- and with a
completely different cause, which is why both are recorded rather than one.

An isolated verification tree is refreshed with `rm -rf tree && git archive HEAD |
tar -x`. **`git archive` stamps each file with the commit's timestamp**, not the
current time. Against a build directory whose objects were compiled minutes ago,
those sources are *older* than their own `.o` files, `make` considers them up to
date, and the binary keeps linking the **previous tip's** object files. The suite
then reports failures belonging to code that is no longer in the tree, with the
sources on disk provably correct -- and a pristine build of the same commit passes,
which is the measurement that separates this from a genuine regression.

Measured: source `22:04:48`, its object `22:16:53`, no recompilation, a red case
that a fresh build of the same SHA ran green.

- **`touch` the tree after extracting it**, or delete the objects **scoped**:
  `find "<build>/CMakeFiles" -name '*.o' -delete`. The unscoped
  `find <build> -name '*.o' -delete` also deletes vendored SDL3's objects,
  `libSDL3.a` is re-created incrementally at 96 bytes, and the link then fails
  with the story's sources untouched.
- **When a case outside your story fails, build the same SHA pristine before
  reporting anything.** It costs one build and it is the difference between "the
  tip is broken" and "my build directory is lying to me".

**The nastiest part is that the extraction is the thing people do to be safe.**
`git archive` into a scratch tree is the standard way to verify a story in
isolation, and it is sound -- but **only while the build directory is fresh too**,
and neither half of that is obvious from either half. A reviewer who extracts into
a *new* build directory every time is immune by luck rather than by design; the
moment a build directory is reused across two extractions, the guarantee is gone
and nothing announces it.

**A third route to the same place, measured in the same task: a tree assembled
from two reads of "HEAD".** Regenerating one file from current HEAD while the rest
of the tree came from an earlier extraction produced a tree whose *case* expected a
message its *production code* did not yet emit -- a red run attributable to **no
commit that exists**, and one whose first symptom (every concurrent run failing)
pointed at a race that was not there. A single run reproduced it, which is what
broke the wrong hypothesis. Capture the SHA **once**, with
`SHA="$(git rev-parse HEAD)"`, and derive the tree and every regenerated file from
that one value.

### A tree assembled from two reads of "HEAD" belongs to no commit that exists

The two entries above describe a tree that is **some** real commit -- an older one.
This one describes a tree that is **none of them**, which is worse, because every
instinct that says *"work out which commit this is"* fails.

Measured in MAR-190. An isolated verification tree was refreshed from
`git archive HEAD`, and one file inside it was separately regenerated from
`git show HEAD:<path>` a few minutes later. Another story committed in between. The
result was a tree whose **case** expected a rejection message the newer commit
introduced, and whose **production code** still emitted the older one -- a red run
that no commit in the repository would ever produce, and that no amount of asking
"is the tip broken?" could explain.

**The symptom pointed away from the cause, and one measurement redirected it.**
The first observation was *eight concurrent runs, eight failures*, which reads
unmistakably as a race. What settled it was that **a single run reproduced it**.
That discriminator belongs beside the one in the entry above:

- a fresh build of the same SHA passes and the incremental one fails -> **stale objects**;
- a single run reproduces what looked like a concurrency failure -> **not a race**;
  suspect the tree before the timing.

*Rule: capture the SHA ONCE -- `SHA="$(git rev-parse HEAD)"` -- and derive the tree
and every regenerated file from that one value. In a worktree with other agents in
it, "HEAD" is not a constant, and two reads of it minutes apart are two different
trees.*

### A passing result is evidence only once you have seen the failing form fail

This chain has written this rule three times in three vocabularies without
noticing it was one rule:

- **"a case green before the implementation exists is a WITNESS"** -- test-side;
- **"a zero result is evidence only once the pattern is known to match something"**,
  hence running every "expect zero" gate against a **planted hit** -- grep-side;
- **a fix verified by a negative control** -- MAR-190 made a scratch root
  per-process and measured **both** forms: the fixed root gave **3 passes, 5
  failures of 8, 0 distinct roots**; the per-process one gave **8 passes, 8
  distinct roots**. Without the first row, the second is a story about timing
  rather than a measurement of the fix.

They are the same idea, and a reader meeting the **fourth** instance in a fourth
vocabulary will not recognise it as a recurrence -- which is precisely the failure
the "lift the general form" half of the historical-measurement rule exists to
prevent.

**The general form: a green result carries information only in proportion to what
would have made it red.** Before believing a pass, name the version of the world
that fails and run it. If you cannot construct one, the check is a witness and
must be labelled one.

**Its shell-side twin, which is the same shape once seen.** A concurrency harness
reported `passes=0 failures=1` because zsh's `nomatch` aborted the command before
`wait` ever ran; the numbers described no execution at all. And a pattern-based
hunk filter reporting "clean" is trustworthy exactly as far as its patterns are.
**A filter reporting clean, a vacuous clause reporting pass, and an aborted
harness reporting a count are one failure**: success reported about a check that
never executed. Assert that the check ran before reading what it says.

### Two agents share one scratchpad, and a restore is only verified by an absolute path

Both were measured in MAR-188's Task 0 and both silently produce a *confident wrong
answer* rather than an error.

**The session scratchpad is shared across every agent in the session.** MAR-188's
implementer extracted an isolated tree into `<scratchpad>/t188` and found it
already populated: the planner agent had used the same path and left a `build/`
directory and its own probe sources there. `git archive HEAD | tar -x` **overlays**
rather than replaces, so the extraction landed on top of another agent's build
output. A tree seeded that way can compile against stale objects and report either
colour wrongly.

**Name the directory after your own agent or task, never after the story.** Every
agent working a story reaches for the story's own number, which is exactly why
they collide. If a directory you did not create is already there, discard it --
`rm -rf` and re-extract -- rather than trusting it.

The same reasoning applies to the shared worktree itself: while a review is live it
mutates and restores tracked files continuously (MAR-188 watched
` M src/editor/safe_fix.cpp` appear and vanish twice). A build tree seeded from
`git archive HEAD` and overlaid with **only your own story's files** is immune to
that, and is what MAR-188 used for every one of its verification builds.

**A restore is only verified if the `cmp` names an ABSOLUTE path.** MAR-188 neutered
four assertions, ran a probe, restored them, and got `cmp ... IDENTICAL`. The
restore had in fact written into the *build tree*, because an earlier `cd` in the
same shell command was still in effect, and the `cmp` then compared the baseline
against the copy it had just made. The assertions stayed neutered for two more
inversions, which is what made their attribution wrong -- and the wrongness was
only visible because a later inversion blamed the wrong case.

Relative paths in a restore step follow whatever `cd` ran earlier in the same
command. Use absolute paths for the file, the baseline and the `cmp`, and prefer a
script over an inline shell sequence: MAR-188's `invert.py` used absolute paths
throughout and every one of its twenty restores is trustworthy for that reason
alone.

**And the absolute path is only half of it: a `cmp` whose FAILURE is silent is not
a verification.** MAR-191's planner found the third instance of this family in
`tools/inversion/invert.sh` while MAR-189 was using the harness. The construct was

```
cmp -s "$copy" "${root}/${file}" && echo "  restore verified by cmp"
```

in a script with `set -uo pipefail` and **no `-e`**, with `rebuild.sh` running
last -- so a failed restore printed no verdict of its own and returned
`rebuild.sh`'s status. **Exit 2 is also what a clean "the mutation did not
compile, tree restored" abort returns**, so the two were indistinguishable, and
the tree that the *next* inversion ran against was not the tree anyone believed
it was. That is worse than the two call-site bugs fixed at `23b326e`: those made
a restore skip recompilation; this one **attributes a failure to innocent code**.

MAR-189 fixed it as GUARD 3 and demonstrated it in both shapes, on a throwaway
subject outside the source tree, because *"the code is present"* is not the check:

| Restore failure | Old script | With guard 3 |
| --- | --- | --- |
| `cp` cannot write (read-only target) | `cp`'s own stderr, **no verdict**, exit 2, mutation left in the tree | `RESTORE FAILED` naming both absolute paths, **exit 4** |
| `cp` **succeeds** and the file still differs (target is a symlink to `/dev/null`) | **nothing at all**, exit 2 | `RESTORE FAILED`, **exit 4** |

The second row is the one that matters, because it is the shape with no `cp`
error to notice. *Rule: any check whose whole job is to catch a rare failure must
be shown failing before it is trusted -- and a check written as `cond && echo`
cannot fail, it can only go quiet.* This is the same argument as *"a case green
before the implementation exists is a witness, not a gate"* and *"a zero is
evidence only once the pattern matches a known-present instance"*, applied to a
shell conditional.

**The independent check that saves you meanwhile**, and the one that retrospectively
validated all sixteen of MAR-189's restores: `invert.sh`'s **guard 2** re-runs the
baseline `cmp` before every mutation, so a restore that failed makes the *next*
inversion abort with `stale baseline`. A chain of inversions that all ran is a
chain whose restores all succeeded -- except the last one, which needs its own
`cmp` against `git show <current-HEAD>:<path>`, re-derived at the moment of
comparing rather than from any earlier copy.

### An identity collision needs a token whose neighbours are unconstrained

MAR-186 escapes `|` and `\` in every identity token, and the reason a collision
is dangerous is that de-duplication cannot tell one from a duplicate and
**deletes** an issue. But a collision is only *constructible* where the
surrounding tokens are free, and that is rarer than it looks:

- each of the seven orphan-animation families has **fixed arity** (transform and
  deform 4 tokens, inherit/slot_color/slot_attachment 3, draw_order and event 2),
  so absorbing a separator into a name changes the arity and the identities can
  no longer coincide;
- the family token is a literal and differs per family, so nothing collides
  across families;
- of what remains, only the **animation** name is unconstrained — a bone or slot
  that does not resolve is a hard runtime load error (see the four
  `skeleton_parse.cpp` rules), and the transform channel is a closed enum.

The one place a genuine collision **can** be built is
`overlay.orphan_weight_target|<skin>|<slot>|<attachment>`, whose three tokens
are all free precisely because an orphan weight target resolves to nothing by
definition and therefore escapes the parser's bone/slot rules entirely.
MAR-186's G12b builds it (`mesh_base`/`body|x`/`ghost_mesh` against
`mesh_base`/`body`/`x|ghost_mesh`) and measured the collapse from two issues to
one. **If you add an identity scheme, ask which of its tokens are unconstrained
before writing the case that proves the escaping works** — MAR-186's first
attempt asserted a count of two on a fixture where the count could never have
been anything else.

### Byte-identity is the wrong invariant when the bytes encode a path

The rule this repository's transactional stories are built on -- *compare bytes,
never a success flag* -- has an exception, and it is the one that makes a
byte-perfect copy **wrong**. Measured in MAR-189:

- `build_atlas_document_text` writes `"image"` from `image_path.filename()`
  (`atlas_packer.cpp:880`), and `build_packed_atlas_artifact` derives that image
  as the atlas path with `.png` (`:1024-1025`);
- `atlas.cpp` resolves that member against **the atlas file's own directory**.

So the `.matl` MAR-188 staged as `staged.matl` says `"image": "staged.png"`.
Copy it byte-identically onto a project's atlas and the copy is byte-perfect and
broken: it names a PNG that is not beside it. **Every byte-comparison clause
passes.** The general form, for anything committed by copying:

> Before asserting byte-identity between a source and a destination, ask which
> bytes are **location-dependent**. Those bytes need a *semantic* clause -- does
> the reference still resolve? -- and byte-identity is evidence *against*
> correctness for them, not for it.

**And the invariant has to be anchored on state captured BEFORE the operation.**
MAR-189's design first specified the semantic clause as three statements about
the committed bundle -- *"`image` equals the target png's file name"*, *"that
file exists beside the atlas"*, *"the atlas loads"* -- and all three pass on the
broken arm. The second is satisfied **because the commit's own `PlaceTexture`
step creates that file**, and the first is circular, because the staging rule is
what defines the target png. The clause that works compares the committed
atlas's `$.atlas.image` against the **pre-commit** atlas's, which is a fact the
operation cannot manufacture. This is a fourth degenerate gate shape beside the
three already recorded one section up: **self-satisfying** -- a clause checking
for an artefact the operation under test creates.

**The second half, which is where the target name actually comes from.** The
obvious fix -- stage under the target *atlas*'s stem -- is also wrong here, and
this repo's own fixture is the counter-example: `assets/fixtures/player_idle.matl`
declares `"image": "player_fixture.png"`. Staging as `player_idle.matl` produces
`player_idle.png`, the commit places it, and `player_fixture.png` is **orphaned**
while the atlas points at a file the project never had. The name has to come from
the **current texture's** stem, i.e. the pre-commit `$.atlas.image`, which is
also the only rule the runtime itself uses -- `atlas_path.parent_path() /
atlas.info().image`, at three sites, with no resolved-texture accessor on
`ProjectData`.

Refuting all of this is one command:

```
grep -n '"image"' assets/fixtures/player_idle.matl     ->  "image": "player_fixture.png"
```

MAR-189's **R1b** is the only case in the tree that sees any of it; its **R1c**
is the refusal a commit issues when the two names disagree. `AGENTS.md`'s
existing rule that *a case green before the implementation exists is a witness*
is the same argument applied to a test; this is it applied to an invariant.

## Methodology hazards worth recording -- the comparison itself can lie

*Promoted to a top-level section by MAR-191. These are present-tense
methodology rules, and they had been living inside `## MAR-183 …
Validation Results`, whose content is a historical measurement of one
story. Every durable entry added here since was therefore an edit to a
story section, which the chain's own rule forbids. MAR-183's own content
is untouched; only this misfiled subsection moved.*

**This section generalises past MAR-183.** Sixteen stories in this chain have
scrutinised the *code under test* while treating the **comparison mechanics** as
trustworthy. An inversion result is not a claim about code; it is a claim about a
**comparison** -- "this case, built from this source, produced this message." Any
link in that chain can break without the code being wrong, and when one does the
result is a confident, false, and completely plausible-looking claim. Three
distinct instances were found here, two of them near-misses caught only by luck.
**H4 was added later**, by the review pass that read this section. It is H3's
sibling, not a restatement of it. **H3 is historical**: a recovery or rewrite
silently thinned a case that *was* once falsifiable, so its remedy is to re-run
every inversion after any recovery. **H4 is authorial**: the assertion was
**never** falsifiable and no recovery was involved, so its remedy is to trace
which writes actually survive to the assertion point. Both rest on the same
principle -- a passing case is no evidence that it detects anything.

**H1 -- a `cp` restore that does not rebuild.** Restoring an inverted source file
with `cp` can leave the restored file and its stale object sharing the **same
second** in their mtimes, in which case `make` does **not** rebuild and the next
run silently exercises the *inverted* binary. This bit once:
`marrow_preference_tests` reported two failures against a pristine source tree,
and C23 assertion 3 failed for the same reason. Every "the inversion bit" claim
across this whole chain rests on the restore actually rebuilding.
**MAR-184 found that `touch` is not sufficient, and that the failure is not
one-directional.** `touch` sets the source's mtime to *now*, and the object
written by the immediately preceding build is also from *now*; at one-second
granularity the object is not older than the source, so an automated loop that
restores, `touch`es, rebuilds, then mutates, `touch`es and rebuilds skips builds
anyway. Nine of sixteen inversions read as "did not bite" from this alone.

**Both directions are reachable, and the difference matters.** If the *mutation*
build is skipped, the stale object is the pristine one and the run is a false
**pass** -- a missed inversion. But if the *restore* build is skipped, the stale
object is the **mutated** one, and the next inversion in a **different**
translation unit compiles and links cleanly against it; its failure is then
attributed to the wrong mutation. That is a false **positive**, from a single
skipped build. MAR-184's mutations spanned four translation units, so this was
reachable rather than hypothetical. Do **not** reason that a stale binary can
only cost you a missed inversion -- under that belief, skipping the deletion when
you are in a hurry looks free, and it is not.

*Rule: do not rely on mtime. **Delete the object file before every verification
build** (`rm -f build/CMakeFiles/<target>.dir/<path>.o`), which removes the
comparison from the question entirely; `touch` is sufficient for a single
interactive restore and is **not** sufficient in an automated loop. Run final
verification against a from-scratch `rm -rf build`. What makes an inversion table
trustworthy is not any asymmetry argument but **H2** -- comparing each message
with `cmp` against an independently recorded first-run text. See the MAR-184
section for the nine-false-negative worked example.*

**H2 -- a hand-sliced reference line.** Comparing a measured message against a
recorded one with `sed -n '3p'` pulled the **wrong line** out of the reference
file and printed `DIFFERS` for a message that was in fact byte-identical. Caught
only because the diff output was visibly nonsense; had the off-by-one landed on a
*similar* line it would have passed unnoticed in either direction.
*Rule: compare with `cmp`/`diff` against a whole recorded string. Never
hand-slice line numbers out of a reference file, and never eyeball a
byte-identity claim.*

**H3 -- a recovered case can be thinner than the one the inversion was run
against.** After test code was lost and rebuilt, the recovered cases **passed**
-- but passing is not evidence, because *a thinned case passes too.* The property
destroyed by the loss was **falsifiability**, and falsifiability is invisible to a
passing run. Confirming C20/C24/C25 green after recovery proved nothing about
whether they still detect anything.
*Rule: after any recovery, restoration or rewrite of test code, **re-demonstrate
falsifiability** -- re-run the inversions against the recovered tree. Do not
substitute a green run for it.*

**H4 -- an assertion can be VACUOUS at the point it runs.** C25 assertion 4 read
three pieces of state after clicking a disabled entry (`dirty_intent`,
`pending_file_application`, `project_path`) and **none of them could have been
set on that click**, on a clean session, no matter what the menu did: the arm is
created and consumed inside the same `render_frame`, and the failed open returns
before it assigns the path. The case passed, the inversion table listed it, and
the property it named -- AC4's "visible but *disabled*" half -- had **no failing
detector anywhere**. Its author reasoned about what *should* be observable
instead of tracing what actually survives the frame.
*Rule: an assertion earns its place by FAILING under the mutation it names. Run
that mutation. Reading state that a passing run leaves at its default is not
evidence -- trace which writes survive to the assertion point, and assert on
those. The reusable shape to watch for, which H3 has nothing to say about: state
ARMED and CONSUMED inside a single `render_frame`, leaving every later reader at
a default it would have held anyway.*

**What was actually done here.** Every inversion restore in this story is followed
by `touch`. The final verification ran against a from-scratch rebuild. Two
inversion results (I7b, I6b) were re-run after H1 was found, and the three
headline inversions (I1, I2a, I2b) were re-verified against the clean build with
identical messages. After H3, **every** inversion originally run against the three
rebuilt cases (C20, C24, C25) was re-run against the committed tree: I6 -> C20;
I9, I10, I3 and the live-vector variant -> C25; the failed-save, fresh-preferences
and transaction variants -> C24. Five reproduced byte-identically (verified with
`cmp`, per H2), one reproduced its first detector with the second stated as
unchanged-by-inference only, and the live-vector variant **again did not
reproduce** -- reported as a second non-reproduction rather than converted into a
convenient bite. C21/C22/C23 were never exposed to H3: they were restored from a
byte-exact file copy rather than rewritten.

### A historical sentence carries its own anchor, and correcting it is a falsification

**A present-tense claim may be corrected; a historical measurement must not.**
The criterion for "historical" is wider than a date. A sentence is anchored, and
must be left alone, when it is bound by **a heading, a date, or a narrated
event** — and the third is the one that gets missed, because it looks
present-tense.

Two measured instances, in two different documents:

- `docs/root1/discription.md:59-60` open *"MAR-182는 2026-08-30에 … 완료했다"* and
  then state *"64-operation Agent/MCP surface는 모두 그대로다"*. The **date**
  anchors them. The registry is 66 today; overwriting the 64 would make the
  sentence claim MAR-182 shipped against a 66-operation surface, which is false.
- `AGENTS.md:500` — *"250 objects and 22/22"* — sits inside a **durable** entry,
  narrating MAR-186's review incident. `ctest` is **23** today. The **narrated
  event** anchors it; the number is correct for the moment it describes.

MAR-191 met the same shape a third time and nearly got it wrong from its own
plan. Its Task 5 table ordered three `64` sites overwritten to `66`, and all
three were consequences attributed to MAR-177, MAR-178 and MAR-179:

> MAR-178이 … 올렸다. 그래서 registry는 이제 정확히 64 ops다.

**MAR-185** raised the registry to 66. Overwriting would have traded a true stale
sentence for a false current one.

**The repair for an anchored sentence is to extend the narration, not to edit the
numeral**: put the historical clause in the past tense and append what happened
next. Every attribution stays true and the endpoint becomes current. Where even
that is too invasive, append a parenthetical and leave the original untouched.

**The corollary for gates.** A gate that asserts a stale string has *disappeared*
encodes the assumption that every instance was correctable, and that assumption
is false wherever a historical instance exists. MAR-191's X2 pre-registered
*"all six patterns return zero"* and **could not reach it** without either
falsifying history or rewording prose purely to defeat a string search — the
second being the self-satisfying shape, where the gate is satisfied by editing
the thing it searches rather than by fixing anything. It was recorded as
**falsified**, and replaced by a **derived** check
(`tools/docs/check_registry_claims.py`): present-tense totals must equal a count
**computed from `src/editor/agent_dispatch.cpp`**, never a literal, while
historical totals are pinned by anchor and count. Its proof is the inversion that
adds a 67th operation to the registry and reddens the gate **with no document
touched** — which a hardcoded `66` could never do.

**That gate reddened on this very entry**, because the paragraph above quotes the
stale sentence it is about. A gate over prose *claims* must not read quoted
*examples*, so it now skips fenced code and blockquotes — and the quotation above
is a blockquote for that reason. The cost was measured rather than assumed: a
genuine stale claim written inside a blockquote is now **invisible** to it. That
is the right trade for a repository whose methodology section exists to quote
stale claims, and it is recorded here so the next person knows the blind spot is
deliberate.

### A pattern-based filter over another agent's live code is worth exactly as much as the check you run after it

MAR-189's implementer used a pattern-based cutter to reconstruct hunks around a
collaborator's code. It failed **three times in one story**: a `\n}\n` matched
early inside a neighbouring function body; a `namespace` block boundary shifted;
and the block's shape changed because MAR-190 adopted the `ScratchRoot` guard.
**Every one was caught by the check that ran after it — none by the script's own
success report.**

The aggravating condition is worth stating plainly: the third failure was
triggered by a **legitimate** change in the other agent's code. The cutter breaks
when your collaborator does something correct, which is why *"it worked last
time"* carries no information on a contended file. Re-read the file immediately
before each edit rather than relying on a read from earlier in the task, and
prefer an edit that matches an exact string and **refuses** when the match count
is not what you expect. MAR-191 anchored every documentation edit that way; the
guard earned its place immediately, refusing a basis-list edit whose anchor
turned out to wrap between `MAR-167` and `Synchronized`.

### `grep -c` counts lines, not occurrences — and a count without its scope is not a measurement

`grep -c` reports **matching lines**. A second occurrence on an already-matching
line is invisible. Measured instance: `64 ops` in
`docs/root1/editing-gap-analysis.md` was **2 lines but 3 occurrences**, so a gate
phrased over `grep -c` would have reported success while an occurrence survived.
Use `grep -o … | wc -l` wherever the expectation is a number.

**A zero is exempt** — no matching line means no occurrence — so this only
threatens **non-zero** expectations. MAR-191 swept the repo's existing gates and
found **none affected**: the `[ OK ]` census measures 437 lines and 437
occurrences; the registry census `^    {"` is line-anchored and immune by
construction; and the remaining stated expectations are zeros. The hazard is
real, the repo is clean today, and both halves of that sentence needed measuring.

**State the scope beside every count.** The same pattern `64 ops` measures **3
occurrences** across the four documents MAR-191 targeted and **13** across the
whole repository. Neither number is wrong; a number reported without its scope
is, and two people comparing scopeless counts will conclude one of them is
mistaken.

## Editing P1 Limitation Inventory

**What this is, and why it exists.** Editing P1 (MAR-154–191) recorded its
limitations story by story, in nine `### Not independently covered` sections and
in prose scattered across `## Repo facts that outlive their story`, MAR-180's
fsync paragraphs, MAR-181's `### Two gaps recorded rather than half-closed`, and
MAR-190's design. `shell_main.cpp`'s frame body had to be written **seven times**
and `commit_path_choice` **five** before anyone could see they were one entry
each. This table is the maintainable copy.

**The rule that keeps it maintainable.** The `## MAR-NNN` sections stay
**untouched** — a story section is the record of what that story saw, and it is a
historical measurement. When a later story closes an entry it edits **this row**,
adding "closed by MAR-NNN", and adds nothing to and removes nothing from any
story section. Reference a row by its `L#` rather than restating it.

**How it was derived, and how to falsify it.** From **84 bullets across 9
sections** measured at `435250e` **before this table existed**, plus the
non-section sources named above. Re-running that census today reports **96 across
11**: MAR-190 landed at `947191b` with six of its own, and MAR-191's validation
section adds six more. That is the census moving, not the derivation being wrong;
the basis is anchored to the SHA above. **Do not re-derive the row count by subtracting collapses
from 84**: bullets 27, 41, 57 and 66 are *compound* "carried forward" entries
naming five or six limitations each, so the bullet count and the row count are
not related by arithmetic. Count rows directly.

| Class | Meaning | Who can close it |
|---|---|---|
| **A** | Coverage gap — a surface exists and no test reaches it | A story with a test budget |
| **B** | Evidence gap — covered in form; no mutation proves the cover bites | An inversion; cheap |
| **C** | Unreachable — written, correct by inspection, no input reaches it | Nobody — record, do not "fix" |
| **D** | Compiler blindness — a list or chain the compiler cannot police | A consumer-side test, or `-Werror=switch` |
| **E** | Known product defect — wrong or surprising behaviour, unfixed | A product story |
| **F** | Permanently open — quantified over a set the program cannot enumerate | A decision nobody has taken |
| **G** | Human/hardware required — Photoshop, an interactive host, a GPU, a second volume | Not an agent |
| **I** | Deliberate decision — intended behaviour, recorded so it is not re-litigated | Nobody, unless the decision changes |

**Class I is a MAR-191 split, and it is the most useful thing in this table.**
The eight classes this story inherited put "a PSD reimport erases every skin"
and "`--auto-close` bypasses the prompt by design" in the same bucket. Fourteen
rows below are a recorded decision rather than a defect. A reader scanning for
work to do should read **E**, not **E ∪ I**.

### A — coverage gap

| L# | Limitation | Recorded by |
|---|---|---|
| L-A1 | `shell_main.cpp`'s **interactive** frame body is reachable from no test; C11 pins only the smoke's copy, so deleting the interactive `apply_pending_file_action` call is invisible | 181, 182, 183, 184, 185, 186, 187 |
| L-A2 | `commit_path_choice` (`shell_file_paths.cpp`, one call site inside the chooser modal) has zero end-to-end coverage — no smoke in `src/editor/` clicks `"Choose"` | 181, 183, 184, 185, 186, 187 |
| L-A3 | MAR-189's `Approve` button is not proven to call `apply_agent_review`; `CheckFrameBodies.cmake` lists `draw_agent_window` as app-only | 189 |
| L-A4 | MAR-189's rollback seam cannot fail the **session call** inside a rollback step | 189 |
| L-A5 | `make_psd_provenance` has no production caller outside `psd_import_smoke.cpp`. (`plan_psd_reimport` gained one at `agent_handlers_management.cpp:99`; that half is **closed by MAR-189**) | 188 |
| L-A6 | MAR-188's `preserve` is exposed and nothing consumes it | 188 |
| L-A7 | MAR-187's F1 proves the severity filter and one Fix button on screen; the group headers, the row Selectables' text and the counts line are covered UI-free only | 187 |
| L-A8 | MAR-185's **scale** materialize arm (`agent_handlers_editing.cpp:2064`) has no case; the byte-identical retime arm at `:997` does | 185 |
| L-A9 | MCP coverage for MAR-185's two tools is count-only — `test_client.py` asserts set equality and nothing about either schema or `required` set | 185 |
| L-A10 | Only the `Mode` combo is proved on screen; the lane's diamonds, the toolbar buttons and the dopesheet row are covered UI-free only | 185 |
| L-A11 | `merge_inherit_timeline`'s `replace_existing_times = false` arm has no product caller | 184, 185 |
| L-A12 | No pixel is asserted for the Inherit dopesheet row; `build_tracks` producing it is not the dopesheet drawing it | 184 |
| L-A13 | MAR-182's `absorb_close_request` / `cancel_close_request()` call site lives in the real main loop and is covered only by a manual check | 182 |
| L-A14 | MAR-189's A5/A6 do not go through the C ABI — `MarrowProject` is opaque outside `marrow_c.cpp` | 189 |
| L-A15 | MAR-190's scrolling path is never exercised by a mouse — F1/F2 use a plan that fits the modal | 190 |

### B — evidence gap

| L# | Limitation | Recorded by |
|---|---|---|
| L-B1 | MAR-186's seven severity **assignments** had no inversion; `check_invariants` recomputes its tallies from the same `issue.severity` it compares against, so a misassignment moves both sides together. **Closed by MAR-191** (V1–V7, `diagnostics.cpp:203, 456, 504, 544, 596, 629, 764`) | 186, 187, closed by 191 |
| L-B2 | `PreviewStaleSkin` (`diagnostics.cpp:629`) has **no per-issue severity detector**; it reddens only through G10's aggregate counts, which the fixture makes asymmetric on purpose. Covered, but one fixture edit from ceasing to be, and nothing would announce that | 191 (measured) |
| L-B3 | MAR-186's V2/V5 failure messages state the expectation and never print the actual severity (`expected a … Warning …; got code '…' fix '…'`), so a reader must re-run to learn what the harness already knew. G7's message does print it | 191 (measured) |
| L-B4 | The severity register measures the **first** detector by run order and cannot enumerate over-determination, because `marrow_project_smoke` aborts at its first failure | 191 (measured) |
| L-B5 | MAR-186's G4/G5 are witnesses with no story-owned inversion, and G0's byte-identity half is a non-effect assertion | 186 |
| L-B6 | MAR-186's AC5 selection half is proved structurally (the collector takes no `SelectionSet`), not by assertion | 186 |
| L-B7 | MAR-187's V0 zero-mutation half is structural, not measured | 187 |
| L-B8 | Which of the eight erase arms a wrong-**record** mutation is caught in is a per-arm claim; the first X1 covered three arms while the prose implied all eight | 187 |
| L-B9 | Fourteen `TimelineKeyKind` switch arms are deliberately uninverted — eleven fall through to the correct answer, three are unreachable | 185 |
| L-B10 | `export.preview` carries no inherit data; A6's assertion is a non-effect witness with no story-owned inversion | 185 |
| L-B11 | MAR-185's A4 "the key still exists" half is not falsifiable, and its error code is over-determined by a pre-existing registry-metadata guard | 185 |
| L-B12 | Inherit undo/redo has no failing detector MAR-185 owns | 185 |
| L-B13 | MAR-184's P4 and the equivalence half of P12 have no story-owned inversion | 184, 185 |
| L-B14 | MAR-184's P10a/P10b have no clean inversion — dropping the bone check fails P10b with the same message by a different route | 184 |
| L-B15 | `marrow_inspect --compare` is a regression witness only for MAR-184: it runs over `player_idle`, which carries no inherit data | 184 |
| L-B16 | MAR-181's load-bearing C11 assertion is `pending_file_application.has_value()`; the `project_path.filename()` check beside it is near-tautological | 181 |
| L-B17 | MAR-189's R1b `image` clause is a witness for a structural reason — its remaining degree of freedom is supplied by the test | 189 |
| L-B18 | MAR-190's P1 is a compatibility **witness**: a provenance-free project draws no `Reimport PSD...` button, green on the pristine tree, and **the negative direction — that the button is correctly hidden — has no gate** | 190 |

### C — unreachable

| L# | Limitation | Recorded by |
|---|---|---|
| L-C1 | MAR-187's `nothing changed → cancel()` branch — the freshness preflight guarantees a live issue always has something to repair | 187 |
| L-C2 | `describe_missing_target`'s `PreviewStaleAnimation`, `PreviewStaleSkin` and `ProjectUnsavedChanges` arms — they carry no selection, so the guard above the switch returns first | 187 |
| L-C3 | `reset_preview_reference`'s five non-preview arms — MAR-186 attaches that fix id to exactly two codes | 187 |
| L-C4 | `collect_session_diagnostics`' `nullopt` branch is unreachable through the agent (`agent_dispatch.cpp:542` rejects first); its `base_skeleton_document() == nullptr` sub-condition is not separately reachable at all | 186 |
| L-C5 | No save-time validation of `image_file`: every write path constructs it from `filename()` and structurally cannot produce a separator | 188 |
| L-C6 | `sort_retimed_timelines`' seventh call is provably a no-op — a retime applies one shared clamped delta, which preserves index order | 185 |
| L-C7 | `set_animation_duration`'s seventh fold is correct by inspection and unreachable by test (four production call sites, not two) | 185 |
| L-C8 | AC1's "finite" half is only partially reachable from a file — JSON has no NaN literal and the tokenizer refuses an out-of-range exponent, so from a `.marrow` the rule bites only on a finite magnitude over float32 max | 184 |
| L-C9 | `EXDEV` is **structurally** unreachable on the placement path, not merely unobserved: `write_file_atomically` places its temporary in the destination's own directory (`atomic_file_write.cpp:61-78`) | 189 |
| L-C10 | `DiagnosticPanel::Hierarchy` and `::Inspector` do not exist; three panels serve four of AC2's five destinations, and no MAR-186 code is a hierarchy-scoped problem | 187 |

### D — compiler blindness

| L# | Limitation | Recorded by |
|---|---|---|
| L-D1 | Populating `OverlayRecordKey` is not compiler-checked, and the `static_assert` beside `collect_orphan_animation_overlays`' seven family calls is a **tautology** — `kSweptOverlayFamilyCount` is initialised from the same literal it is compared against | 186, 187 |
| L-D2 | `-Wswitch` warns but does not stop the build, GCC needs `-Wall` for the same diagnostic, and the build still has no `-Wall`/`-Wextra` | 185, 186 |
| L-D3 | `ProblemsSeverityFilter::` and `DiagnosticPanel::` appear outside their switch files as member defaults and test expectations | 187 |
| L-D4 | MAR-189's staged-atlas naming rule has **two** implementations kept in sync by hand — `plan_scenario` re-derives it instead of calling `plan_project_reimport`. The cost is locality, not coverage: MAR-189's case A5 guards it in both directions, but a break surfaces in an agent-approval case rather than beside the rule | 189 |
| L-D5 | The two hand-maintained frame bodies are policed only by `cmake/CheckFrameBodies.cmake`, whose regex `draw_[a-z_]+windows?\(` **does not match modals** | 182, 187, 191 |

### E — known product defect

| L# | Limitation | Recorded by |
|---|---|---|
| L-E1 | **A PSD reimport erases every skin.** `psd_import.cpp:1041` is `root->erase("skins")`, with `bones` and `slots` replaced wholesale above it. MAR-189 refuses to build on it rather than papering over it; the importer is unfixed | 188, 189 |
| L-E2 | Slot names are unstable across imports — adding a layer can rename an untouched slot, because the dedup uses a document-global census (`psd_import.cpp:814-826`), surfaced as `proposed_slot_name` differing from `current_slot_name` on an `Updated` row | 188, 189, 190 |
| L-E3 | `normalize_mesh_weights` **creates** its target instead of validating it, and `AttachmentSelection` and `MeshWeightTarget` are transposed | 187 |
| L-E4 | `serialize_project` is not bit-exact for doubles needing 17 significant digits | 186 |
| L-E5 | The runtime accepts a negative first inherit key time, and a `.mskl` may carry inert `curve` data on an inherit key. Only the project layer refuses either | 184, 186, 187 |
| L-E6 | The empty-edit hazard is fixed only for the inherit family; siblings still emit `"attachment": []` and produce an unloadable `.mskl` | 184, 186, 187 |
| L-E7 | An inherit lane can be pruned out of existence and cannot be re-created from the dopesheet, because a row exists only for a materialized timeline | 185 |
| L-E8 | An agent-driven `animation.rename` leaves the GUI clipboard stale; `sync_shell_from_editor_session_if_revised` cannot tell a rename from a selection change | 185 |
| L-E9 | macOS case duplicates of a **missing** file remain two recent-project entries — `weakly_canonical` converges only for files that exist | 183, 186, 187 |
| L-E10 | A settings write failure is reported once and then forgotten; the in-memory list keeps the change, so the session behaves as if it persisted and the next launch disagrees | 183 |
| L-E11 | Up to `kRecentProjectLimit` `stat` calls per frame while the submenu is open, and a `stat` on a dead network mount can block one | 183 |
| L-E12 | **The largest orphan class is unreachable.** An overlay naming a missing bone or slot makes the project **unopenable**, and it is reported by nobody — the user sees only the runtime's load error | 186 |
| L-E13 | The New form keeps the stale-`opened` hole the chooser lost; it self-heals on the next `begin_file_action(New)` and nothing in the machine reads it | 182 |
| L-E14 | New writes `active_animation: "idle"` for a rig that may have no `idle` clip | 181 |
| L-E15 | MAR-187's Fix button was **off-window** until F1 found it — a default `Selectable` spans the content region, so the `SameLine()` button landed past the right edge, unreachable by mouse. Fixed; recorded because every UI-free case passed the whole time | 187 |
| L-E16 | **A pixel-only PSD edit is not stale.** Same layers, same classification, same digest; the reimport proceeds and the new pixels ship | 190 |

### F — permanently open

| L# | Limitation | Recorded by |
|---|---|---|
| L-F1 | MAR-180's "rebases every relative path" quantifies over a set the program cannot enumerate: `preserved_root` is by definition what the code does not understand. **It must not be written up as "six families, done"** | 180, 190 |
| L-F2 | Nothing is persisted — a diagnostic is recomputed on demand, and a project opened, inspected and closed leaves no trace of what was found | 186 |
| L-F3 | No `fsync`. A crash between the temporary and the rename leaves one orphan `*.tmp.*`, beside the project and beside `editor-settings.json` alike | 180, 183, 190 |

### G — human or hardware required

| L# | Limitation | Recorded by |
|---|---|---|
| L-G1 | **Photoshop's real layer-record order is unverifiable here and requires Photoshop.** MAR-191's **O1** measured the half that *is* measurable — the importer **refuses** the inverse record order by exact message, over-determined by a second guard — which settles what would happen, not what Photoshop emits. The row stays open and stays class **G**. If the real order is the inverse, a group-bearing real PSD produces a planning error and the user sees MAR-190's planning-failure path (N4) reporting the parser's own message — contained, not removed. The parser requires each group's `lsct` 1/2 header before its children and the `lsct` 3 divider after; both fixtures and the synthesiser are authored that way. This is a **G**, not a macOS limitation — no future agent story can close it | 188, 190 |
| L-G2 | The MCP client half (`tools/mcp/test_client.py`) is hand-run and not in CTest; it needs a live editor with the agent socket listening | 185, 189, 191 |
| L-G3 | This host cannot create a Metal device for the headless renderer, so `marrow_renderer_sample` is exercised with `--skip-render` and no frame is rendered | 191 |
| L-G4 | A real cross-device rename needs a second volume; this machine has one | 180, 189 |
| L-G5 | The interactive frame body and the window host need a real display session; `run_headless_smoke` returns before a window host is created | 182 |

### I — deliberate decision, recorded so it is not re-litigated

| L# | Decision | Recorded by |
|---|---|---|
| L-I1 | `weights.uncanonicalizable` has no repair. Normalization is precisely what cannot fix it, which is why its `safe_fix_id` is deliberately absent | 186 |
| L-I2 | The orphan-weight-target supersession hides real weight problems on an orphaned edit until the orphan is fixed — the weights are dead data the runtime never sees | 186 |
| L-I3 | Fixing an orphan overlay can **create** a stale-preview issue, and both polarities are asserted | 186 |
| L-I4 | An oversized on-disk recent list stays oversized until the next real mutation, because **loading never writes** — cleaning at load would let one unmounted volume delete a user's bookmarks permanently | 183 |
| L-I5 | A native close during a live authoring gesture re-raises the prompt; `absorb_close_request` is deliberately not gated on `authoring_gesture_active` | 183 |
| L-I6 | `AwaitingSave` ignores a close request rather than queueing it — a save in flight must land | 182 |
| L-I7 | A New project created over an existing file, then saved, is recorded in Recents: the arm keys on the create, not on the file's prior absence | 183 |
| L-I8 | A persisted "last used directory" for the chooser is deliberately deferred — a different preference with a different lifetime and failure mode | 183 |
| L-I9 | `--auto-close` bypasses the dirty prompt by design; smokes end on frame count, and any other choice hangs CI | 182 |
| L-I10 | The clipboard cascade fixes all seven families at once, and the copied fragment's own `animation_name` is deliberately **not** rewritten | 185 |
| L-I11 | The review queue is deliberately **not** a diagnostic issue — making it one would move `warning_count` whenever an agent queues a save | 186 |
| L-I12 | MAR-189's AC5 approval clause is read as editor-only; MCP gets no approve tool, following `agent.resume`'s "only the editor can restore access". A stricter reading needs a 67th operation | 189 |
| L-I13 | MAR-189's skins refusal is **unconditional** for a project carrying hand-authored skins on layers the PSD still produces — narrow sense: no remedy exists; wide sense: validation refuses. Both were measured and both are true; they are different senses of "blocked" | 189 |
| L-I14 | MAR-190's staleness digest covers identity and classification only — it is **MAR-189's two-field `identity=change;` list**, not the ten-field tuple MAR-190's design specified, so a provenance edit renaming a stored `slot_name` leaves the modal showing `current_slot: X` while the commit uses `current_slot: Y`. **Inherited deliberately**: a stricter MAR-190-local digest would make the same edit stale on the editor path and not on the agent path, and an inconsistent contract between two entry points is worse than one recorded gap. Raised as a follow-up for the digest's owner | 190 |

### The count, and where it disagrees with its own prediction

**86 rows in 8 classes** — A 15, B 18, C 10, D 5, E 16, F 3, G 5, I 14 —
against a pre-registered **73 in 8 classes** (A 12, B 11, C 9, D 4, E 22, F 3,
G 5, H 7). The prediction was made against **74 bullets in 8 sections**; the
sweep ran against **84 in 9**, because MAR-189 landed in between. Reported as a
disagreement rather than reconciled, per the plan.

Where the two differ, and why:

- **E 22 → E 15 + I 13.** The single largest difference is the class split, not
  a change in what was found. E ∪ I is 28 against a predicted 22; six of those
  are rows the prediction did not enumerate rather than rows invented here.
- **B 11 → 17.** Four rows are new **measurements** MAR-191 made rather than
  inherited: B2, B3 and B4 come out of the severity register, and B1 moves to
  closed. An inventory that only copies forward cannot grow this way.
- **H 7 → 0, and the class is gone.** MAR-189 shipped, then MAR-190 shipped at
  `947191b`, so nothing is prospective any more. Its rows were re-derived against
  the delivered code, as this section instructed, and redistributed: the scrolling
  gap to **A**, P1's ungated negative direction to **B**, the pixel-only staleness
  hole to **E**, and the deliberately-inherited digest scope to **I**. **One row was
  dropped rather than moved** — "a failed rollback is reachable" was read from
  MAR-190's design, and the delivered story measured it and withdrew the disclaimer,
  so it is not a limitation. A prospective row that survives delivery unexamined is
  exactly the unmeasured claim this class was flagged to prevent.
- **C 9 → 10, F 3 → 3, G 5 → 5.** Essentially confirmed.

**The prediction was not falsified so much as superseded by a story landing**,
and the evidence for that reading is that the per-story bullet counts matched it
exactly — 6/12/14/16/9/10/5/2 for MAR-188 down to MAR-181 — with MAR-189's ten
as the entire difference.

## MAR-192–210 Platform Program Local Implementation Checkpoint

Validated locally on 2026-08-09 without closing any platform story. The source
implements the SDL3/Sokol architecture and Windows compile-time/service/package
paths. A 2026-08-12 scope decision makes Ubuntu/Linux, Windows 10, and a separate-PC
portable run `NOT REQUIRED`; their code paths do not gain support claims. Windows 11
high-DPI/manual UI, physical Windows Ink, and fixed legacy/Sokol A/B evidence remain
required by MAR-210.

- `cmake --build build -j4` -> all default targets built.
- `cmake --build build --target marrow_verify_third_party` -> pinned SDL3,
  ImGui, Sokol, patched sokol_imgui, three sokol-shdc binaries, generated
  Metal/GL headers, and removed legacy backends verified.
- `ctest --test-dir build --output-on-failure` -> 17/17 noninteractive tests passed.
- `ctest --test-dir build-display --output-on-failure` -> 20/20 passed,
  including three actual SDL/Metal display tests.
- `ctest --test-dir build-platform-release --output-on-failure` -> Release
  display-enabled suite 20/20 passed.
- `marrow_window_host_smoke` -> 20 host lifecycles; logical `640x480`, drawable
  `1280x960`, scale `2x2`, BGRA8 + depth-stencil, 4x MSAA.
- `marrow_gpu_parity_smoke` -> top-left RGBA8 corner/center/bottom-right probes
  within `2/255`, 20 device lifecycles, and 100 image/view lifecycles.
- `marrow_editor_display_smoke` -> actual editor offscreen viewport, main
  sokol_imgui pass, commit, and present completed for 20 frames.
- `nm -gU build/marrow_editor_shell` -> zero `sapp_*`/`sglue_*` symbols;
  standalone `marrow_renderer_sample` retains the adapter.
- CMake link ownership -> `marrow_editor_shell` links `marrow_renderer_core`;
  only the standalone compatibility renderer links `marrow_renderer_sapp_host`.
- `otool -L build/marrow_editor_shell` -> no OpenGL framework or GLFW library.
- `./build/marrow_editor_shell --verify-launch-focus` -> SDL high-DPI window
  and both AppKit/process Regular activation policies verified.
- Current qualification authority and explicit NOT RUN rows:
  `docs/root1/platform-validation.md`.

## MAR-191 Validate and Document Editing P1 Validation Results

MAR-191 closes Editing P1. Its job is different from the twenty-three stories
before it: it **verifies and documents what they built**, adds one measurement of
its own, and repairs exactly one debt that was assigned to it by name. It changes
**no compiled code** — every file it touches is documentation, a tracking JSON, or
a standalone checker script that `CMakeLists.txt` does not reference.

### What was measured before anything was written

Fifteen gates, in a `git archive` extraction of the parent SHA rather than the
live worktree, because two other stories were writing into it. Five were
blocking; **three fired**.

| Gate | Expected | Measured | |
|---|---|---|---|
| G1 | parent + both trailers | present, contiguous | pass |
| G2 | `ctest -N` 22 | **23** | drift — MAR-189 registered `marrow.psd_import_smoke` |
| G3 | `-L runtime` 4, `-L editor` 12 | **4 / 13** | differs — see below |
| G4 | `grep -c add_test` 25, 3 guarded | **26**, 3 guarded → 26 − 3 = 23 | pass |
| G5 | 0 warnings | build exit 0, **0 warnings, 0 errors** | pass |
| G6 | `[ OK ]` 437 | **437**, re-measured not carried | pass |
| G7 | registry 66 | **66** | pass |
| G8 | 11 guards, array bound, 2 py asserts | **11** = 7 graph / 2 constraints / 2 timeline; `agent_dispatch_smoke.cpp:42`; `test_client.py:53,55` | pass |
| **G9** | 7 severity sites | **exact**, and proven complete | pass |
| **G10** | MAR-189 absent | **PRESENT** | **fired — inverted** |
| **G11** | MAR-190 absent | absent, but the gate is **defective** | **fired** |
| **G12** | 74 bullets / 8 sections | **84 / 9** | **fired — material** |
| **G13** | six stale strings each > 0 | 2/1/1/1/1/1 | pass |
| G14 | MCP venv present, gitignored | present via `tools/mcp/.gitignore:1`, absent from the archive | pass |
| G15 | worktree dirty | dirty **and moving**; `AGENTS.md` was staged by another agent mid-measurement | pass |

**G3 is a correction worth carrying.** The two labels **overlap** —
`marrow.parameter_project_smoke` carries both — so `4 + 13` **double-counts** and
the union is **16 of 23**, with **7 tests carrying neither**. The labels are an
overlapping subset check, never a sum and never a partition.

**G11 was a defective gate, and it was in this story's own plan.** Its pattern
`draw_psd_reimport_modal` matches nothing in the parent tree **and nothing in the
live worktree either**, where MAR-190's files exist — so its zero carried no
information. MAR-190's surface is a model layer (`apply_psd_reimport_review`,
`group_psd_review`, `psd_review_can_confirm`, `build_psd_commit_plan`) with no
`draw_` function at all. *A zero is evidence only once the pattern is known to
match something*, and the rule existed before the plan that broke it.

**G12 was not falsified so much as superseded.** The per-story counts matched the
prediction **exactly** — 6/12/14/16/9/10/5/2 for MAR-188 down to MAR-181 — with
MAR-189's ten as the entire difference.

### Result, by acceptance criterion

**Four of six are witnesses, and that is a property of the story, not a
shortfall.** MAR-191 changes no compiled code, so no suite can fail *because of*
it; a run that passes regardless of what this story did is a witness by
definition. Saying so is the point of the label.

| AC | Verdict | Kind | Evidence |
|---|---|---|---|
| AC1 | met | **witness** | The P1 surfaces are covered by MAR-154–189's own cases, re-run green here. PSD reimport is covered at the **plan and commit** layers (MAR-188/189); the **review UI** layer is MAR-190's and is not in this tree |
| AC2 | met | **gate** | Zero-line diff over `src/runtime/`, `include/marrow/runtime/`, `include/marrow/marrow.h`, and over `project.cpp`'s schema-shaped edits; `format-spec.md` untouched. Each proven capable of matching (below) |
| AC3 | met | **witness** | Registry **66**, MCP **66**, eleven `!= 66U` guards, two `== 66` Python asserts — all pre-existing and re-measured, none authored here |
| AC4 | met | **witness** | MAR-189's rollback failpoint sweep, re-run green. MAR-191 authored none of it |
| AC5 | met | **gate** | The documentation edits below, gated by `tools/docs/check_registry_claims.py` with **five** proven failure modes |
| AC6 | met | **witness** | Run **twice**. **8a** on the tree this commit produces, before MAR-190 existed; **8b** at `cde3bb6`, with all of Editing P1 present, which is the one that counts — *a final validation of Editing P1 that omits the last story of Editing P1 is not a P1 sign-off*. Both below |

### The full run (8a), and exactly which tree it ran in

Run in a fresh extraction of the parent SHA **with this story's hunks applied and
MAR-190's excluded** — verified: their `AGENTS.md` entry absent, their
`psd_reimport_review.cpp` absent, `psd_import_smoke.cpp` byte-identical to HEAD.
The live worktree was **not** used for this half, because it carries MAR-190's
uncommitted work across eight files and building it would measure a tree
belonging to no commit.

| Command | Result |
|---|---|
| `cmake --build build` | exit 0, **0 warnings, 0 errors** |
| `marrow_verify_third_party` | hashes verified |
| `marrow_constraint_warning_check` | built |
| `marrow_frame_body_check` | both frame bodies draw the same ten windows |
| `ctest` | **100% passed, 0 failed out of 23** |
| `ctest -L runtime` | **4/4** |
| `ctest -L editor` | **13/13** |
| `marrow_project_smoke` | exit 0 |
| `… --export-runtime/--export-binary` | exit 0 |
| `marrow_inspect --compare` | `matches` |
| `marrow_agent_dispatch_smoke` | exit 0, **437** `[ OK ]` |
| `marrow_psd_import_smoke` | exit 0 |
| `marrow_renderer_sample --skip-render` | exit 0 — **`--skip-render` is what AC6 names; this host cannot create a Metal device, so no frame was rendered** |
| `marrow_editor_shell --auto-close 2` | exit 0, `MARROW_CONFIG_HOME` set; `$HOME/Library/Application Support/Marrow` absent **before and after** |
| `check_registry_claims.py` | X2 PASSED |
| `git diff --check` | clean |

### The sign-off run (8b), at `cde3bb6`, with all of Editing P1 present

Re-run once MAR-190 landed, because a P1 sign-off that omits P1's last story is
not a sign-off. Same tree plus MAR-191's own hunks.

| Command | Result |
|---|---|
| `cmake --build build` | exit 0, **0 warnings** |
| `marrow_verify_third_party` / `marrow_constraint_warning_check` / `marrow_frame_body_check` | all built; both frame bodies draw the same windows |
| `ctest` | **100% passed, 0 failed out of 24** |
| `ctest -L runtime` / `-L editor` / `-L docs` | **4** / **13** / **1** |
| `marrow_project_smoke`, `+ exports`, `marrow_inspect --compare` | exit 0; `matches` |
| `marrow_agent_dispatch_smoke` | exit 0, **437** `[ OK ]`, registry **66** |
| `marrow_psd_import_smoke` | exit 0 — **now including O1** |
| `marrow_renderer_sample --skip-render` | exit 0, no frame rendered on this host |
| `marrow_editor_shell --auto-close 2` | exit 0; `$HOME/Library/Application Support/Marrow` absent before and after |
| `marrow.registry_claims` alone | **Passed**; and `***Failed` on a planted claim, so the CTest entry is a gate rather than a green light |
| `git diff --check` | clean |

The MCP half ran in the **live** worktree, because the venv is gitignored and
exists only there; `tools/mcp/` is untouched by both stories in flight, verified
by `git status` and by a diff against the parent SHA. `py_compile` over
`server.py`, `test_client.py`, `tools/editing.py`, `tools/inspection.py` — exit 0.
**The MCP client run itself is manual and is reported as manual**, the same
standard MAR-180 and MAR-185 met.

### Inversions run

**V1–V7 — the severity register, complete over its class.** MAR-186 raised this
gap, MAR-187 re-recorded it, and both deferred it **to MAR-191 by name**:
`check_invariants` recomputes its error/warning tallies from the same
`issue.severity` values it compares against, so a **misassignment** moves both
sides together and no counting case can see it.

The population is **closed, not sampled**: `make_issue` has exactly seven call
sites, and `diagnostics.cpp:75` is the only severity write in `src/editor/`. The
struct default at `diagnostics.hpp:211` is always overwritten on that path; other
`DiagnosticIssue` constructions exist only in test code.

Each literal was flipped to its opposite, rebuilt with objects deleted, run, and
restored under a `cmp` naming two absolute paths.

| # | Site | Code | First reddening case | Clause |
|---|---|---|---|---|
| V1 | `:203` | `OverlayOrphanAnimation` | MAR-186 **G1** | severity |
| V2 | `:456` | `OverlayOrphanWeightTarget` | MAR-186 **G9(e)** | severity |
| V3 | `:504` | `WeightsUncanonicalizable` | MAR-186 **G8** | severity |
| V4 | `:544` | `WeightsNonCanonical` | MAR-186 **G7** | severity |
| V5 | `:596` | `PreviewStaleAnimation` | MAR-186 **G9(a)** | severity |
| V6 | `:629` | `PreviewStaleSkin` | MAR-186 **G10 (pure)** | **count** |
| V7 | `:764` | `ProjectUnsavedChanges` | MAR-186 **G10 (dirty session)** | count |

The seven texts, verbatim:

- **V1** — `issue 'overlay.orphan_animation|deform|ghost|body|body_mesh' is a warning, expected an Error -- the phantom animation is written into every .mskl and .mbin export, which is shipped corruption.`
- **V2** — `expected an overlay.orphan_weight_target Warning carrying 'remove_orphan_overlay'; got code 'overlay.orphan_weight_target' fix 'remove_orphan_overlay'.`
- **V3** — `weights.uncanonicalizable is a 'warning', expected an Error -- a vertex whose whole influence list is below kMeshWeightEpsilon has no meaningful binding.`
- **V4** — `expected a weights.non_canonical Warning carrying 'normalize_weights', got code 'weights.non_canonical' severity 'error' fix 'normalize_weights'.`
- **V5** — `expected a Warning carrying 'reset_preview_reference' on panel 'project' naming animation 'ghost'; got fix 'reset_preview_reference' panel 'project' animation 'ghost'.`
- **V6** — `error_count 2, warning_count 1 -- expected error_count 1, warning_count 2. The fixture is asymmetric on purpose: a one-of-each fixture cannot see the two accumulators swapped.`
- **V7** — `reported error_count 2 warning_count 2, expected 1 and 3 -- the two project warnings plus the unsaved-changes warning. Counting severities BEFORE appending project.unsaved_changes leaves warning_count one short on every dirty project.`

Every clause is a **severity or count** clause; **no message clause reddened**, so
no mutation was over-broad and all seven are credited.

**V6 is the result worth reading twice, and it is not a clean win.**
`PreviewStaleSkin` bit, so the predicted "no detector" outcome did not occur and
no assertion needed inventing. But it reddened through G10's **aggregate**, not
through a per-issue check — the other five name the offending issue and V6
reports two integers being swapped. Its own failure text says why: *"a one-of-each
fixture cannot see the two accumulators swapped."* The assignment is covered, but
by a fixture asymmetry that one edit could remove, and nothing would announce
that. Recorded as **L-B2**, not as a gap closed at the V1–V5 standard.

**A limitation of the register that could not be removed.** It measures the
**first** detector by run order and **cannot observe over-determination**, because
`marrow_project_smoke` aborts at its first failure. Measured, not assumed: all
seven logs are exactly 99 lines against the baseline's 119, lines 1–98 are
byte-identical across all seven, and the baseline's 20 MAR-187/188 lines are
absent from every mutated run. So **"one case per site" must not be read as "one
detector per site"** — different claims, and only the first is in evidence. A
continue-on-failure mode would make the second measurable and is another story's
change.

**I-X2 — five proven failure modes.** X2's original form (*"all six patterns
return zero"*) is recorded as **falsified**; see the document errors below. Its
replacement derives the oracle from the product.

| Mode | Mutation | Result |
|---|---|---|
| a | one present-tense `66` → `64` | exit 1, `claims 64, registry has 66` |
| b | a pinned historical count drifts | exit 1, `'64 ops' x1 (pinned 2)` |
| c | **a 67th operation added to the registry** | exit 1, `oracle: 67` — **no document touched** |
| d | a bare claim with no present-tense marker, in an unpinned document | exit 1 **after** zero-pinning; **exit 0 before** |
| e | the same shape via `64-operation` in `AGENTS.md` | exit 1 |

**Mode (c) is what makes it a derived check** rather than a hardcoded one: the
gate reddens because the *product* moved, which a literal `66` could never do.
Mode (d) was a hole found by review after the gate was first written and closed by
pinning **all eight** document/literal pairs **including the zeros** — a zero pin
converts *"this literal happens to be absent"* into *"this literal is asserted
absent."*

**O1 and I-O1 — run after MAR-190 landed and released the file.** Task 2 was held
for most of the story because `src/samples/psd_import_smoke.cpp` was under
continuous edit; it was taken up once MAR-190 committed.

`write_synthetic_psd` gained an `inverse_group_order` mode that emits the mirror
image — the `lsct` 3 divider where the folder record goes and the `lsct` 1 folder
record where the divider goes. **O1** asserts that
`import_psd_to_runtime_bundle` refuses that document with **exactly**
`"PSD folder end marker appeared without an open folder."`, and its **control
arm** asserts the same three layers in the accepted order still import with two of
them grouped under `torso` — without which a synthesiser that emitted nothing
parseable would satisfy the refusal clause for entirely the wrong reason.

**I-O1** makes the pop on an empty `active_groups` a no-op (`psd_import.cpp:732-737`)
and O1 reddens:

> `O1: refused, but not for the reason claimed.`
> `  expected: PSD folder end marker appeared without an open folder.`
> `  actual:   PSD folder markers were unbalanced.`

**The refusal turns out to be over-determined**, and that is the result worth
keeping: a *second*, later guard catches the inverse order when the first is
disabled. O1's **exact-message** clause is what discriminates between them — a
substring or an any-error clause would have passed under the mutation and been
recorded as "no bite". Q0, which runs before O1, stayed green under the mutation.

**What this does and does not settle.** It measures the **importer**, which was
the measurable half. It says nothing about what Photoshop emits; **L-G1** stays
open and stays class **G**, because no Photoshop-authored PSD exists here and none
can be produced.

### Document errors found

Nine, all measured rather than reasoned:

1. **This story's plan ordered three historical sentences overwritten.** Its Task 5
   table listed `editing-gap-analysis.md:95`, `:455` and `refector.md:20` as
   present-tense `64` claims. All three are **narrated consequences** of MAR-177,
   MAR-178 and MAR-179 — and **MAR-185** raised the registry to 66. Overwriting
   would have traded a true stale sentence for a false current one. Repaired by
   extending the narration; the general rule is now a durable entry.
2. **X2's pre-registered form was unreachable.** *"All six patterns return zero"*
   could not be achieved without either falsifying history or rewording prose
   purely to defeat a string search — the **self-satisfying** shape, where a gate
   is satisfied by editing the thing it searches. Recorded as falsified and
   replaced by the derived check, rather than quietly re-specified.
3. **`grep -c` counts lines, not occurrences.** `64 ops` in
   `editing-gap-analysis.md` is **2 lines but 3 occurrences**. A sweep of the
   repository's other gates found **none** affected — the `[ OK ]` census is 437
   both ways, the registry census is `^`-anchored, and every other stated
   expectation is a zero, which is exempt because no matching line means no
   occurrence.
4. **`### Methodology hazards worth recording` was misfiled** inside
   `## MAR-183 … Validation Results`. Its entries are present-tense rules; that
   section's content is one story's historical measurement, so every durable entry
   added there was an edit to a story section. **Promoted to top level** by this
   story, with MAR-183's remaining content proven byte-identical to
   before-minus-block (295 → 195 lines, matching the 100 moved).
5. **The plan directed MAR-190 to be marked complete.** It has not shipped. Left
   unmarked; the plan assumed a landing order that did not happen.
6. **Six story-table rows were shipped but unmarked** — MAR-184 through MAR-189 —
   found by measuring against which stories actually have validation sections
   rather than by trusting the plan's list of three.
7. **Three `done` stories carried no `completedAt`**: MAR-188, MAR-157, and this
   story's own row. Found by sweeping the whole file rather than the story range;
   a range-scoped check had reported one. MAR-157's date was corroborated twice
   before writing it. The file-wide invariant now holds: **188 done, 0 undated**.
8. **The gate reddened on its own documentation.** Writing the historical-sentence
   entry — which necessarily *quotes* a stale claim — made X2 fail. A gate over
   prose **claims** must not read quoted **examples**; it now skips blockquotes and
   fenced code. It was **re-proved after being modified**, on the principle that a
   modified gate is no longer a proven gate, and the **cost was measured**: a
   genuine stale claim inside a blockquote is now invisible to clause (1). That
   blind spot is recorded as deliberate so it is not "fixed" back.
9. **`-L editor` was reported to this story as 12 with six unlabelled.** Measured:
   **13**, seven unlabelled, and the labels overlap.

### Not independently covered

**This is the last such section the chain writes.** Everything Editing P1 knows
about its own limitations is now in `## Editing P1 Limitation Inventory` —
**86 rows in 8 classes**, derived from 84 bullets across 9 sections plus the
non-section sources. Read that, not this. What follows is only what MAR-191
itself leaves open:

- **O1 was never run.** The importer's group-order behaviour is unmeasured because
  `psd_import_smoke.cpp` was locked by MAR-190 for the duration. **L-G1** stays
  open, and the half of it that *is* measurable stays unmeasured.
- **AC6's 8b run measures a tree that is not this commit.** It ran at `cde3bb6`
  with every Editing P1 story present but without MAR-191's own hunks, which are
  documentation, one test case and one CTest registration. The build and suite
  results therefore describe the code this story validates, not the diff it adds.
- **Four of six acceptance criteria are witnesses**, because this story changes no
  compiled code. No suite here can fail because of MAR-191, and a passing run is
  therefore weak evidence about MAR-191 specifically — it is strong evidence about
  the twenty-three stories it verifies.
- **The inventory's own class boundaries are a judgement.** The **E/I** split —
  product defect versus deliberate decision — moved fourteen rows out of the
  "things to fix" bucket. A re-derivation that moves some back is a better answer,
  not a worse one; what must not change is that the count's role is to be
  falsifiable.
- **The severity register cannot see over-determination**, as measured above, and
  `PreviewStaleSkin`'s coverage rests on a fixture asymmetry (**L-B2**).
- **`ctest -N` moves 23 → 24, and the delta is this story.** `marrow.registry_claims`
  is registered (label `docs`). MAR-190's Task 10 gate compares `ctest -N` against
  its own Task 0 baseline of 23 and says *any other value is a regression to
  investigate, never a number to adjust* — correctly written, but **not scoped to
  authorship**, so it cannot tell "MAR-190 added a test" from "someone else did".
  Registration was deliberately held until MAR-190 committed for exactly that
  reason. MAR-190's five *recorded* 23s are historical and stay 23.

## MAR-190 Add the PSD Reimport Review UI Validation Results

Measured at `435250e`. Baselines re-derived at Task 0 and again before the commit.

### The finding that changed the product, not just the tests

**AC2's word "preserved" does not mean what the design assumed, and the UI would
have said so.** `preserve` is read in exactly two places. `prune_unpreserved`
(`psd_reimport_commit.cpp`) erases a `Missing` layer's slot from the staged
skeleton -- but against ANY plan the real importer produces it never fires, because
`build_skeleton_document` assigns `(*root)["slots"]` wholesale from the newly
parsed PSD and erases `skins` (`psd_import.cpp:1040-1041`), so the slot is already
gone. The sole observable is `provenance_from_plan`, which keeps or drops the
layer's row in `$.editor.import_sources.psd`.

So a `Missing` layer's slot and attachment are removed by the reimport **in either
direction**. The checkbox governs only whether the project keeps REMEMBERING the
mapping -- and, downstream, whether a layer that returns arrives as `Updated` or as
a brand-new `Added` (`psd_reimport_plan.cpp:242-247,288-293`).

Three consequences, all shipped:

- The checkbox reads **`Forget`**, never `Delete`. On a modal whose every other row
  names slots and attachments, `Delete` reads as *"delete the slot"*, and a user
  leaving it unticked would form a false belief about their art.
- The `Missing` section carries a body line stating **both** halves: the slot goes
  either way, and forgetting means a returning layer arrives as a new layer.
- **D1/D2 assert the provenance row list, not the committed skeleton's slots.** The
  design's original clauses (*"both Missing layers' slots are present after the
  commit"*) **fail on correct code** -- the first degenerate shape, inside AC2's own
  row. **V5 now asserts the committed slot set is IDENTICAL whether a layer was
  forgotten or preserved**, so the finding is a standing gate rather than a claim.

### AC3 was failing, and fixing it needed two defects separated

V8 measured MAR-190's own AC3 -- *"...commit failure leave the project, runtime
source, files, selection, and **history** unchanged"* -- failing. The shell cases
already demanded `redo_count()` unchanged for Cancel and modal close, so the
commit-failure arm was held to a weaker bar for no principled reason. The first
version of V8 asserted `redo == before + 1`, which **encoded the defect as a
requirement**.

| Symptom | Verdict |
|---|---|
| `redo_count()` gains an entry after a rolled-back `UpdateProvenance` | **Defect.** The rollback called `session_.undo()`, whose contract is to make the entry redoable. Redo then re-applied the provenance edit over rolled-back files |
| The user's PRE-EXISTING redo stack is emptied | **A second, different defect.** `push_history` clears redo on every commit (`session.cpp:1393`) -- right for a real edit, wrong for a speculative one that is rolled back |
| `project_revision()` advances across the revert | **INTENDED.** `apply_history` bumps it whenever the project changed (`session.cpp:1672`), in either direction. It is a change detector, not a state identifier. Recorded as intended, not excused -- *an excused clause and an intended behaviour read identically in a test and mean opposite things* |

**Fixing either defect alone leaves AC3 or AC5 broken.** The fix adds
`EditorSession::revert_last_edit()` (revert without recording a redo entry, keeping
`undo()`'s two guards) and a matched `stash_redo_stack()` /
`restore_stashed_redo()` / `drop_redo_stash()` trio. **`clear_history()` was not
used**: it wipes both stacks, which fixes AC3 by breaking AC5's *"preserving
existing unsaved overlays and undo/redo history."*

### Inversions -- actual outcomes, not predictions

| # | Mutation | Outcome |
|---|---|---|
| I1 | `Missing` indices appended to `updated` | **BIT** -- V1, ordered section rows |
| I2 | sections bucketed in REVERSE order | **BIT** -- V1, *"first order difference at index 0"*. The register's original I2 (drop the identity sort) was a **provable no-op**: `plan.layers` is ordered by a `std::set`, so there is no sort to drop |
| I3a | `build_psd_commit_plan` derives through a `const_cast` | **BIT (after relocation)** -- D2. It did NOT bite in D1, whose forget set is empty, so the clause could not fail there |
| I4 | AC2's default inverted | **BIT** -- D1, ordered `(identity, preserve)` list |
| I5 | digest returns a constant | **BIT** -- V3 |
| I6 | restage root never removed | **BIT** -- V3, by absolute path |
| I7 | Cancel leaves the review engaged | **BIT** -- N1 |
| I8 | `BeginPopupModal` given `nullptr` | **BIT** -- N2 |
| I10 | `clear_history()` on the success path | **BIT** -- D1c |
| I11 | row `Selectable` loses its explicit width | **BIT** -- F2, as *"never hovered"*: a control pushed past the window edge is CLIPPED, so the sweep never sees it rather than finding it misplaced |
| I15 | rollback returns to `session_.undo()` | **BIT (after reordering)** -- V9, redo `1 -> 2` |
| I16 | displaced redo branch never restored | **BIT** -- V9, redo `1 -> 0`. This is the faithful test of the AC3/AC5 trade |
| ~~I14~~ | chosen identity absent from the re-plan | **SUBSUMED by I5, not run.** The digest IS the ordered identity list, so step 2 returns `Stale` before step 3's lookup can run. Step 3's arm is unreachable defensive code |
| ~~I17~~ | `clear_history()` in place of the revert | **MIS-AIMED, retired.** It bit, but MAR-189's **R3** caught the un-reverted provenance first, so this story's clause was never exercised |
| ~~I9~~ | neuter `adopt_runtime_sources` | **NOT THIS STORY'S.** It targets code inside `commit_psd_reimport`; recorded as belonging to MAR-189's register rather than reported as "did not bite" |

**I15 did not bite on its first run, and the diagnosis is the durable part.** The
rollback restored the stash AFTER reverting, and `restore_stashed_redo` *assigns*
`redo_entries` -- so it overwrote whatever the revert left, and `undo()` and
`revert_last_edit()` produced identical stacks. **Two correct components in the
wrong order made each other unobservable.** The general form is recorded above as
**masking**, a sixth degenerate gate shape.

### Gates, and the two that were wrong when written

Every "expect zero" gate was run against a **planted hit** and returned to green
after a restore verified by absolute-path `cmp`: S1 `1 -> 2`, S2 `1 -> 2`,
S3 `0 -> 1`, S4 `0 -> 1`, S5 `0/0 -> 1/1`, S6's `.find(` pattern shown to match.

- **S1 was restated twice, both times by RUNNING it.** *"Exactly 3 lines"* broke
  once N2 legitimately called the function to drive the `p_open` route. Worse, the
  count-only form **did not catch I12 at all**: moving the call into
  `render_shell_frame` leaves the count at 1, because `shell_main.cpp` is also
  under `src/editor/`. **A gate that counts call sites cannot see a call site
  MOVE.** S1 now asserts the count AND that the line is in
  `shell_project_panels.cpp`.
- **T1 was comparing build progress.** A whole-output diff against the baseline
  differs only in `[100%]` prefixes, which a standalone target invocation omits.
  T1 compares the `MAR-187 frame-body check` STATUS line, and the narrowed
  comparison was itself proven capable of failing against a planted change.

The frame-body gate remains a **non-effect** check for this story and is labelled
one: its regex `draw_[a-z_]+windows?\(` cannot match `draw_psd_reimport_modal`, in
either direction.

### What a real mouse proved that nothing else could

**F2 found an ImGui abort no UI-free case in this story could reach**: the Confirm
branch returned early from inside `BeginDisabled(...)`, skipping `EndDisabled()`
and aborting with *"In window 'Reimport PSD##psd_reimport': Missing
EndDisabled()"*. Every N-case calls the seams directly and never enters that scope.

Also measured there: **the window a sweep starts from needs settle frames too**,
not just the modal -- the Project window's rect one frame after submission is a
**32x37 stub at (60,60)-(92,97)**, and the CONTROL sweep is what caught it, because
a widget that shipped years ago failing to hover is unambiguous where a missing
button is not. And **Escape is measured unable to close the modal**, which is the
property AC3's "modal close" path rests on.

### Not independently covered

- **P1**, the compatibility witness: a provenance-free project draws no
  `Reimport PSD...` button. Green on the pristine tree and labelled a WITNESS; no
  story-owned inversion turns it red, and the negative direction -- *the button is
  correctly hidden* -- has no gate.
- **The staleness digest covers identity and classification only.** It is MAR-189's
  two-field `identity=change;` list, not the ten-field tuple this story's design
  specified. A provenance edit that renames a stored `slot_name` leaves the modal
  showing `current_slot: X` while the commit uses `current_slot: Y`, with no
  staleness reported. **Inherited deliberately**: a stricter MAR-190-local digest
  would make the same edit stale on the editor path and not on the agent path, and
  an inconsistent staleness contract between two entry points is worse than one
  recorded gap. Raised as a follow-up for the digest's owner.
- **A pixel-only PSD edit is not stale.** Same layers, same classification, same
  digest; the reimport proceeds and the new pixels ship.
- **The scrolling path is never exercised by a mouse.** F1/F2 use a plan that fits.
- **Photoshop's real layer-record order is unverifiable here.** No
  Photoshop-authored PSD exists in this repository. If the real order is the
  inverse, a group-bearing real-world PSD produces a planning error, and what the
  user sees is this story's planning-failure path (N4) reporting the parser's own
  message. Contained by AC3, not removed.
- **Slot names remain unstable across imports** (`psd_import.cpp:814-826` dedups
  from a document-global census), surfaced as `proposed_slot_name` differing from
  `current_slot_name` on an `Updated` row. Displayed without editorial.

## MAR-189 Commit PSD Reimports Atomically Validation Results

MAR-188 produced a *reviewable* plan and wrote nothing outside a caller-supplied
staging root. MAR-189 takes that staged bundle, validates it against the live
project overlay **before touching anything**, replaces the project's skeleton,
atlas, texture and layer directory through a journal of renames, adopts the new
runtime sources, updates provenance -- and, on a failure after any of fifteen
steps, puts the original bytes back.

### What was measured before any code was written

All at the commit's actual parent, **`b127048`**, in a tree extracted with
`git archive HEAD | tar -x` and overlaid with only this story's files. The plan
and design were authored against `cf6a199` / `71465db`, **34 commits stale**;
every number below was re-derived rather than copied.

| # | Measurement | Value at `b127048` |
|---|---|---|
| A4 | clean all-target build, `find build/CMakeFiles -name '*.o' -delete` | **0** `warning:`, **0** `Wswitch` |
| A4 | `ctest --test-dir build -N` / `ctest` | **22** / **22 passed, 0 failed** |
| A4 | `./build/marrow_agent_dispatch_smoke \| grep -c '[ OK ]'` | **437** |
| A9 | `ctest -N \| grep -c psd_import` | **0** -- built since MAR-176 and never run by CTest |
| A5 | `grep -c '^    {"' src/editor/agent_dispatch.cpp` | **66**; guards **11**, split **7** `shell_smoke_graph.cpp` / **2** `shell_smoke_constraints.cpp` / **2** `shell_smoke_timeline.cpp`; `test_client.py:53,55` |
| A1 | the staged `.matl` under MAR-188's defaults | `"image": "staged.png"` |
| A3 | the atlas `"name"` member outside tests/samples | display only; region lookup is by region name |
| A7 | a directory `rename` between `/tmp` and the project directory | **same volume here -- but that is the weaker half of the answer.** `write_file_atomically` builds its temporary as `parent / …` where `parent` is the DESTINATION's own directory (`atomic_file_write.cpp:61-78`), so every rename it performs is within one directory and **`EXDEV` is structurally unreachable in production**, not merely untested on this machine. §2.4's "placement copies rather than renames" is what makes that true |
| A8 | a provenance-only `EditTransaction::commit()` against the OLD document | succeeds; and it writes **no `.marrow`**, so AC4's provenance update is **in memory** |
| -- | `adopt_runtime_sources` while a transaction is active | refuses (`session.cpp:1901-1907`). The rollback's re-adopt is safe only because `UpdateProvenance`'s transaction is a local that is destroyed first |
| -- | the tracked bundle | `player_idle.{marrow,matl,mskl,mbin}` + `player_fixture.png`. **There is no layer directory**, and `player_idle.matl` declares `"image": "player_fixture.png"` |

### Result

| Measurement | Before (`b127048`) | After |
|---|---|---|
| clean build `warning:` / `Wswitch` | 0 / 0 | **0 / 0** |
| `ctest -N` / `ctest` | 22 / 22 passed | **23 / 23 passed, 0 failed** |
| `marrow_agent_dispatch_smoke` `[ OK ]` | 437 | **437** |
| `marrow_psd_import_smoke` | green, absent from CTest | green, registered as **`marrow.psd_import_smoke`** |
| the "built but never run by CTest" row | open since MAR-176 | **discharged** -- `ctest -N` 22 -> 23 |
| registry / guards / `test_client.py` | 66 / 11 (7/2/2) / 53,55 | **unchanged** |
| `git status --porcelain assets/fixtures/` | empty | **empty** |

`import.psd_layers` was reworked in place, so no operation was added and nothing
in the 66-guard chain moved.

### The safety gate, and the two defects that made it unwritable as specified

This story's subject is the atomic replacement of asset bundles, and
`agent_path_allowed` whitelists the **project directory**. `marrow.agent_dispatch_smoke`
runs with `WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}` against
`assets/fixtures/player_idle.marrow`, whose `.mskl`, `.matl` and `.png` are
tracked. A committing case pointed at that session overwrites the user's
repository, so the gate **aborts the process** rather than returning false.

Two specified defects had to be fixed before any case could run, and both were
confirmed by running:

- **The allowed set and the whitelist were disjoint.** The plan's clause 1
  demanded `temp_directory_path()`, which on macOS is `$TMPDIR`
  (`/var/folders/...`); `agent_path_allowed` whitelists `/tmp` and `/private/tmp`
  and **not** `$TMPDIR` (`agent_dispatch.cpp:626-629`). Measured live: the first
  A-case sited under `temp_directory_path()` was refused with **"Input path is
  outside the agent whitelist."** before it could test anything. The gate's
  allowed set is `$TMPDIR ∪ {/tmp, /private/tmp}`, and the A-cases are sited
  under `/tmp` deliberately.
- **Clause order decides whether a clause exists.** With the temp clause first,
  every repository path trips it and the repository clause is dead code -- so the
  specified message that *names the repository root* is unreachable. Checked
  repository-first, both are live. The same trap fired one level down and was
  caught by running the self-test: with the general repository clause before the
  narrower fixture clause, row (b)'s message named the repository root and never
  the tracked fixtures. The order is narrowest-first.

The root is located by walking up for `assets/fixtures/player_idle.marrow`, **not**
by `.git`: an isolated verification tree extracted with `git archive` has no
`.git`, and a gate that cannot find its root would pass on the input it exists to
catch. Three self-test rows run before any committing case; row (b) is the one
that matters. The predicate is what the rows assert; the aborting wrapper is one
unbranched call to it, and that is recorded rather than claimed as covered.

### The seam, and the tension it buys

`RenameCallback` (`atomic_file_write.cpp:212`) was evaluated and rejected. It
reaches **0 of 15** steps today -- `write_file_atomically` has three production
call sites and the importer writes staging through raw `std::ofstream` -- and at
most 5 after this story. It is keyed on `(source, destination)`, which cannot
distinguish two steps writing into one directory, and it fires *during* a write
rather than *after* a completed step, which is what AC3 names.

The design's own `advance(step)` seam reaches **15/15 by construction**, and the
argument belongs beside it rather than in its favour: **the seam's reach and the
byte-fidelity guarantee are in direct tension.** Rollback returns the original
bytes because the original was *moved*, never read or re-encoded; every step
added to the seam's coverage is a step whose rollback stops being a rename. The
two `Validate*` steps and `CleanJournal` are where that costs nothing; the four
`Place*` arms are the ones the byte map has to police.

**A second, independent rollback seam was added** (`set_psd_commit_rollback_failpoint_for_testing`,
plus a `steps_rolled_back` ledger), because `advance` runs only in the commit
body and can never fire while the rollback -- the half AC3 is about -- is
running. It does **not** close §9.2's corner: a failure *inside*
`adopt_runtime_sources` during the re-adopt needs a seam inside `EditorSession`.
**The seam exists and it stops at the session boundary.**

### The scenario fixture could not exercise this story's headline hazard, and did not until it was fixed

Recorded as a correction to this section's own first version, because it is the
sharpest instance of a gate measuring nothing. Every R-case builds its bundle by
running a real import, and an import names the atlas and its texture from **one**
path -- `bundle.matl` and `bundle.png`. The tracked fixture does not:
`player_idle.matl` declares `"image": "player_fixture.png"`. So on a
same-stem bundle, *"derive the target texture from the atlas's stem"* and
*"derive it from the atlas document's own `image` member"* return the **same
answer**, and every case was blind to the difference between them -- which is the
one difference this story exists to get right.

It surfaced only when inversion **I19** mutated the committer to the stem rule and
the suite had to be checked for what would catch it. `open_scenario` now renames
the packed texture to `bundle_tex.png` and repoints the atlas document at it, so
the two rules disagree in every scenario. **R1c's asserted message changed as a
direct result** (`references 'bundle_tex.png'`, not `'bundle.png'`), which is the
evidence that the fixture change is load-bearing rather than cosmetic.

*The general form: a fixture built by the same code path that the production code
uses will agree with that code about anything the two derive together. Reproduce
the property the REAL data has, not the one the generator happens to produce.*

### The byte map is a recursive listing, not an enumerated set

An enumerated five-item map cannot see a file the commit newly created beside the
ones it knew about -- an orphaned texture under an unpredicted name, a surviving
`.bak`, a `*.tmp.*` from an interrupted atomic write. `bundle_bytes` is a
recursive listing of the project directory **and** the resolved layers directory,
so "paths only in after" catches all three for free, and only because nothing
decides in advance what to look at.

**Demonstrated, not assumed.** A throwaway mutation planted
`planted_orphan.png` beside the atlas after a successful commit -- exactly the
shape a wrongly-named texture or a surviving `.bak` takes -- and R1b reported
`appeared: …/r1b/planted_orphan.png`. An enumerated five-item map would have
returned green.

### Inversions -- actual outcomes, not predictions

Sixteen mutations, each rebuilt with its dependent objects deleted by name and
each restored by a `cmp` naming **absolute** paths. Attribution is by **run
order**. The predicted case is the design's; the text is what actually printed.

| # | Mutation | Predicted | Actual first failure |
|---|---|---|---|
| **I1** | `advance()` consults the failpoint but does not append to the ledger | R0 | **R0** -- `step ledger mismatch` / `missing: ValidateRequest …` |
| **I2** | `advance()` appends **before** the step body runs | R2(b) | **R2(b)** -- `ValidateStagedBundle must not appear in steps_executed when it failed` |
| **I3** | rollback re-writes the **staged** bytes instead of renaming the backup back | R3/PlaceSkeleton | **R3/BackupLayers** -- `only in after: …/bundle_layers.marrow-journal-24838-5.bak/shadow.png (78 bytes)`. Earlier in run order than predicted: `BackupLayers` is the FIRST arm at which a backup exists to restore, and the design attributed it to the first arm at which a *placement* exists |
| **I4** | rollback removes the placed file but never renames the backup back | R3/PlaceAtlas | **R3/BackupLayers** -- same arm, same reason |
| **I5** | `PlaceTexture` is skipped (atlas placed, texture not) | R1 | **R1** -- `the committed texture ('…/bundle.png') must equal the staged bytes; sizes 0 and 117` |
| **I6** | revert §2.2: stage under the hardcoded `staged` stem | R1b | **Q13** -- `staged_skeleton_path must be named 'player_idle.mskl'; got '…/staged.mskl'`. The image clause is NOT the detector at planner level, and the reason is worth keeping: the packer derives BOTH the staged atlas's file name and its `image` member from `atlas_output_path`, so inside the planner they cannot disagree. Only at COMMIT level, where the atlas file is *renamed* on placement, is `image` an independent fact -- which is what R1b and R1c assert |
| **I7** | `ValidateStagedBundle` builds against the session's **current** document | R2(b) | **not run.** Superseded: `ValidateStagedBundle` re-reads the staged document from disk and R2(b)'s refusal (`$.ik[0].bones[0]: ik constraint references unknown bone 'torso'`) is a property of the staged bones, so the mutation is the same edit as I2's family. Recorded rather than fabricated |
| **I8** | omit the `AdoptRuntimeSources` step entirely | R0 | **R0** -- `missing: AdoptRuntimeSources` |
| **I9** | write provenance layers in reverse plan order | R4 | **R4(provenance)** -- `index 4: expected 'torso\|body slot=body …', got 'shadow slot=shadow …'` |
| **I10** | drop `bone_name` when copying a planned layer into provenance | R4 | **R4(provenance)** -- `index 4: … got 'torso\|body slot=body attachment=body bone= image=body.png'`. The ordered-identity clause passes; only the full tuple sees it |
| **I11** | ignore `preserve`: never prune | R5 | **R5(drop)** -- `slot 'shadow' must be removed by a preserve=false Missing layer; the committed slots are: body arm_l shadow` |
| **I12** | a failure after `CleanJournal` rolls the commit back | R3/CleanJournal | **R3/CleanJournal** -- `a failure after the last step must still report success; the commit reported ok=0` |
| **I13** | rollback does not delete the backups | R3/BackupTexture | **not run, and it is a no-op by construction.** A rollback consumes each backup by renaming it back; there is no separate deletion to remove. Kept out of the register rather than recorded as "did not bite" |
| **I14** | the digest comparison always reports equal | A6 | **A6(changed)** -- `a PSD changed since review must be refused with psd_changed_since_review; got ok=1 … 'Committed PSD reimport for review #1.'` |
| **I15** | `apply_agent_review` ignores `request.allowed` | A6 | **A6(rejected)** -- `approving a whitelist-rejected request must refuse; got ok=0 code='psd_changed_since_review'` |
| **I16** | the dry run leaves its staging root behind | A1 | **A1** -- `the dry run left '/tmp/mar189_agent/a1_staging/plan-1' behind` |
| **I17** | the staging root is not whitelist-checked | A3 | **A3** -- `expected forbidden_path …; got ok=0 code='psd_plan_failed' message='… staging root could not be created'`. **The mutation still fails the operation** -- `/usr/local` is unwritable -- so a `!result` assertion would have passed here and recorded a false "did not bite". Only asserting the CODE sees it |
| **I17b** | delete ONE of `ValidateRequest`'s two name guards (found by review) | R1c | **A second live instance of I17's shape.** With the `staged_image != image` guard gone the *other* guard fires -- the staged texture is `staged.png` and the target's `image` is `bundle_tex.png` -- so the op still fails, for a different reason, and `!result` would be green. **R1c sees it only because it asserts the full message.** Two independent instances in one story is why "assert the message" is not style advice |
| **I18** | `rollback_advance` inverts the seam's polarity | R6b | **R3/OpenJournal** -- `the commit must roll back cleanly; rollback_error='rollback of OpenJournal failed: injected failure: '`. Earlier than intended: a spurious rollback error reddens the sweep long before the case that reads the error's text |
| **I18b** | `rollback_advance` consults the seam and **discards its message**, so the rollback still stops and reports nothing | R6b | **R6b** -- `a failing rollback must NAME the step it was undoing. Expected 'rollback of PlaceAtlas failed: injected failure: disk went away'; got ''`. R3 stays green, because nothing there reads `rollback_error`'s content. This is the arm that makes the rollback seam load-bearing |
| **I19** | derive the target texture from the ATLAS's stem instead of the atlas document's `image` -- the "obvious wrong fix" -- with both `ValidateRequest` name guards disabled so it reaches placement | R1b | **R1** -- `the committed texture ('…/bundle_tex.png') must equal the staged bytes; sizes 137 and 117`. R1's byte clause overlaps R1b's subject for any mutation that misplaces content; see the note below |
| **STRAY** | plant `planted_orphan.png` beside the atlas after a successful commit | R1b | **R1b** -- `appeared: …/r1b/planted_orphan.png` |
| **I23** | record the deletion intent only when the prune observably erased something -- i.e. revert to contract (a) | R5(d)/delete | **R5(d)/delete** -- `marking the layer for deletion must let the reimport proceed -- losing that attachment is the requested outcome, not destruction`. The two contracts are indistinguishable without this case |
| **I24** | forgive EVERY lost identity, not only deleted slots | R2(d) | **R2(d)** -- `a staged bundle that erases a hand-authored skin must be refused even when NOTHING references it; the commit reported success`. A quietly widened exclusion is not green |
| **I22** | delete the exclusion of requested deletions from `lost` | R5(c) | **R5(c)** -- `a preserve=false deletion must not be refused as destruction by the very step that performed it`. **R2(d) still refuses under the same mutation**, so the exclusion narrows the check without disarming it |
| **I20** | delete the structural `<skin>/<slot>` comparison, leaving the runtime build | R2(d) | **R2(d)** -- `a staged bundle that erases a hand-authored skin must be refused even when NOTHING references it; the commit reported success`. **R2(c) passes unchanged under the same mutation**, on its own deform message -- which is what makes R2(d) the unique detector for the structural half |

**I17 is the clearest instance in this story of why `!result` is not an
assertion.** Removing the whitelist check does not make the operation succeed; it
makes it fail *later and for a different reason*. A gate written as "the dry run
must be refused" would be green on the mutated code.

**I11 was run twice, and the first run is the finding.** The planted `shadow`
slot that makes the pruning path reachable was authored as `{name, bone}`, and
the un-pruned document then failed validation with
`$.slots[2].attachment: missing required member` -- so the case reddened for the
wrong reason and never reached the slot list under test. With a valid planted
slot the mutation is caught by the clause that is supposed to catch it. *A test
fixture that is invalid in a way the code under test happens to notice is a gate
measuring the validator, not the feature.*

### The harness gained a third guard mid-story, and every restore was re-verified

MAR-191's planner found `invert.sh`'s restore verification **silent on failure**
while this story was using it. Fixed here as GUARD 3 and demonstrated in both
failure shapes; the general lesson is under *"Two agents share one scratchpad, and
a restore is only verified by an absolute path"*.

All sixteen of this story's restores were then re-verified **independently of the
script**, by `cmp` against `git show <current-HEAD>:<path>` with both sides named
by absolute path and the blob re-derived at the moment of comparing: four files,
all identical, with a planted-hit control proving the `cmp` was capable of
reporting a difference. Fifteen of the sixteen were already proven sound by a
second mechanism -- guard 2 re-runs the baseline `cmp` before every mutation, so
each inversion that ran is evidence that the previous restore succeeded.

Most of this story's inversions went through a **scoped** wrapper that deletes only
the objects whose `.d` files name the mutated source, because `invert.sh` rebuilds
every marrow object (correctly -- `rebuild.sh` deletes `*.o` under
`<build-dir>/CMakeFiles`, leaving vendored SDL3 alone -- but at minutes per
inversion). **The wrapper carried guards 1 and 2 and printed a restore verdict,
but shared the exact defect described above: its failure branch was not non-zero
either.** Guard 3 did not exist while those runs happened, which is precisely why
the independent re-verification was done rather than asserted. **I6 alone ran
through the committed `invert.sh`.** E16's *"both of the harness's guards"* below
is accurate as of that row's own writing; there are three as of this commit.

### R1b is a gate for one clause and a witness for the other, and the difference was measured

**R1b's path-set clause bites**: the planted stray above is caught, so the clause
is demonstrably capable of failing.

**R1b's `image`-comparison clause has no single-file mutation that reddens it
FIRST**, and the reason is structural rather than an oversight. Any mutation that
changes what gets *placed* is caught by R1's byte clause, which runs earlier; the
only mutation that leaves the bytes correct and the *name-to-content relationship*
wrong is the planner-naming revert -- and that trips **Q13** first, because Q13
asserts the planner honours the requested names. So the `image` clause is the only
statement of the invariant anywhere in the tree, and nothing in this story's
register reddens it on its own.

What does carry evidence for it is **R1c**, the refusal: with the naming reverted
the commit stops at `ValidateRequest` with the two names quoted, and R1c asserts
that message. Recorded this way rather than claiming a bite the register cannot
produce -- which is the same discipline as *"a case green before the
implementation exists is a witness, not a gate"*, applied one level down to a
clause rather than a case.

### Deliberately uninverted, by name

- **The failpoint seams themselves.** Both are test-only; neutering either makes
  R3 vacuous rather than red, which is why R0's ledger identity -- which uses no
  seam -- exists.
- **The `Approve` button** (`shell_agent_panel.cpp`). Unobservable; see below.
- **`rollback_error` on the post-adoption unrecoverable corner.** Reaching it
  needs a second failure injected *inside* `adopt_runtime_sources` during the
  rollback of the first. The rollback seam this story added gets one step closer
  and still stops at the session boundary.
- **The journal manifest's contents.** R7 asserts it exists in flight and is gone
  after; nothing replays it, so a mutation of its *fields* changes nothing
  observable -- a provable no-op of MAR-188's I7 shape.

### Witnesses, not gates

**AC3 is satisfied in form by 15 arms and in evidence by 12.** R3's
`ValidateRequest`, `PruneUnpreserved` and `ValidateStagedBundle` arms touch
nothing in the target bundle: they stay green with the rollback entirely broken,
because there is nothing to roll back. They are real coverage of the *ledger* and
of the refusal path, and they are not evidence about restoration. Named here
rather than counted.

**R2(a)** is a witness in the other sense: a corrupt PSD never produces a plan,
which is MAR-188's behaviour, green before this story, and no MAR-189 inversion
reddens it. **Q12** likewise asserts MAR-188's defaults are unchanged.

### Document errors found (17)

| # | Where | Finding |
|---|---|---|
| E1 | both MAR-189 documents | Authored against `cf6a199` / `71465db`, **34 commits** behind `b127048`. Every baseline number re-measured |
| E2 | design §0.3 F1, §3.1, §4's table, §7's I12, §10's risk 2 | **Five** prose sites said *"fourteen steps"* / *"thirteen of fourteen arms"* while the enum, `kAllCommitSteps` and §2.7 all say **fifteen**. **Corrected in the design in this commit**, so a reader grepping `fourteen` there now finds only the one legitimate hit -- I12's *"passes fourteen arms and is wrong about the fifteenth"* |
| E3 | plan §0.10 clause 1 | Demands `TMPDIR`; `agent_path_allowed` whitelists `/tmp` and `/private/tmp` and **not** `TMPDIR`. **Disjoint** -- no A-case was writable as specified, and the first one written that way was refused with *"Input path is outside the agent whitelist."* |
| E4 | plan §0.10 clause order | Ordered as written, clause 2 is dead code and §0.10's specified message is unachievable. Reordered narrowest-first; the same trap fired once more one level down and the self-test caught it |
| E5 | plan §0.10's premise | Names a **layer directory** in the tracked bundle. There is none: `player_idle.{marrow,matl,mskl,mbin}` + `player_fixture.png` |
| E6 | design §2.2, §6.5 | The staged atlas is named from the target **atlas**'s stem. `player_idle.matl` declares `"image": "player_fixture.png"`, so that orphans the PNG. The name comes from the **current texture's** stem |
| E7 | design §6.3's R1b | All three specified clauses pass on the broken arm -- one is self-satisfying (`PlaceTexture` creates the file it looks for) and one is circular. The invariant that holds compares against the **pre-commit** `$.atlas.image` |
| E8 | design §6.2 | A fixed five-item byte map cannot see a newly created sibling, a `.bak`, or a `*.tmp.*`. Replaced by a recursive listing |
| E9 | design §6.3's R3 | *"`runtime_revision()` unmoved"* for every rolling-back arm. False for `AdoptRuntimeSources` and `UpdateProvenance`: adoption bumps it and so does the rollback's re-adopt. Those arms assert the stronger clause -- the **active skeleton source** is byte-identical |
| E10 | design §6.3's R3 | The `CleanJournal` row expects a non-empty `journal_residue`. An injection *after* `CleanJournal` cannot produce residue, because `CleanJournal` already succeeded. Split: the sweep arm asserts the commit stands, and a new **R3b** makes the removals genuinely fail |
| E11 | design §6.3's R7 | Predicts **three** `.bak` at `BackupSkeleton`. The seam fires **after** a step's body, so there are **four** |
| E12 | design §2.6 | *"A Missing layer with `preserve == true` survives because the importer merges the candidate onto the existing skeleton."* Measured: `build_skeleton_document` assigns `(*root)["slots"]` from the candidate and calls `root->erase("skins")` (`psd_import.cpp:1038-1041`). The slot is gone either way. **`preserve` governs the stored provenance identity, not the art** -- and "a `preserve == false` layer whose slot is absent is an error" would have failed every such decision against the only importer that exists |
| E13 | plan Task 1 steps 3-4 | Mutually unsatisfiable: the containment guard runs before the import (MAR-188 moved it), but `staged_texture_path` comes from `imported.texture_path`, which does not exist yet. Resolved by deriving it before and **asserting equality after** |
| E14 | design §2.1 vs §3.2 | `PsdCommitStep` is placed in the test-only internal header while the public result exposes `std::vector<PsdCommitStep>`. The enum is the element type of a public field and lives in the public header |
| E15 | MAR-188's write-up | `make_psd_provenance` **still has no production caller** after MAR-189: the committer takes the plan and never re-parses, so it never holds a `PsdImportResult` |
| E16 | plan §0.9 | `rebuild189.sh` is obsolete -- `rebuild.sh` was fixed at `23b326e`. `invert.sh` still passes no object paths, which is now *correct but slow* (a full marrow rebuild per inversion); a scoped wrapper deleting only the dependent objects was used instead, keeping both of the harness's guards |
| E17 | plan Task 4's R3 shape | *"a size mismatch between the table and `kAllCommitSteps` is itself an asserted failure"*. A size check passes on a table with the right count and the wrong members; the table is compared to the enum **by identity** |

### Not independently covered

- **The `Approve` button.** `CheckFrameBodies.cmake:36` lists `draw_agent_window`
  as app-only because the headless smoke never sets `show_agent_panel`. A5 proves
  `apply_agent_review` commits; **nothing proves the button calls it.** The same
  shape as MAR-181's `apply_pending_file_action` gap, and the same one-directional
  mitigation. Task 7b (a frame-body-style script asserting the branch names
  `apply_agent_review`) was offered and not taken.
- **§9.2's irreversible window.** A failure inside `adopt_runtime_sources` during
  the rollback's re-adopt. The rollback seam added here can fail a rollback
  *step* -- and now **does**, at **every** step it can reach. `R6b` asserts
  `rollback_error`'s text and the reverse order (`PlaceSkeleton`, `PlaceAtlas`);
  **R6c** is the parallel of R3 for the rollback path, one arm per step
  `rollback_advance` can reach -- eleven of them (`UpdateProvenance`, the four
  `Place*`, the four `Backup*`, `OpenJournal`, `AdoptRuntimeSources`) -- each
  failing the commit at `UpdateProvenance` so the rollback walks the whole journal,
  and asserting the ledger **ends** at the injected step rather than merely
  containing it. *"Injectable per step" and "asserted at one step" are different
  claims, and only the sweep makes the first one evidence.*

  **The sweep's own list was compared to nothing until review caught it** -- E17's
  lesson one level up. R3's table is checked against `kAllCommitSteps` **by
  identity** precisely because a size check passes on a table with the right count
  and the wrong members; R6c's list was a hand-maintained literal with no such
  check, so a new `rollback_advance` call site would have been silently uncovered
  while the sweep still read complete. There is no product-side list to compare
  against -- the honest reason it was written that way -- but one is **derivable**:
  a commit failing at the last step before `CleanJournal` rolls the whole journal
  back, and **the ledger it produces IS the set of reachable steps**. R6c now runs
  that probe first and asserts its own list equals it, which turns completeness
  from a reading of five call sites into a measurement. It still cannot fail
  the session call inside a rollback step.
  Until R6b existed the seam was declared and never fired and `rollback_error` was
  only ever asserted **empty**, which is a seam with no evidence behind it;
  **I18b** is the arm that proves R6b sees a discarded message.
- **The skins erasure is refused, and this row records what the refusal does and
  does not inspect.** It was a DISCLOSURE first, and the transition is the
  evidence: written originally as a reasoned aside ("refused by nothing in one
  narrow case"), then measured as **R2(d)** and found materially worse -- a rig
  with hand-authored skins and mesh-weight overlays, **no IK constraint and no
  deform timeline**, was **committed**, the committed skeleton had **no `skins`
  member**, and `import.psd_layers` reported success. Silent, unrecoverable data
  loss.

  R2(c)'s refusal is triggered by a deform timeline that **names** the lost
  attachment -- and its mesh is named something no slot names, because with
  `skins` absent the parser synthesises a default skin from the slots' own
  `attachment` members (`skeleton_parse.cpp:4933-4935`), so a mesh named after the
  slot's attachment would still resolve. The runtime build cannot see a loss
  nothing references, which is why a **structural** comparison was added rather
  than a runtime-build one.
  `ValidateStagedBundle` now compares the current skeleton file's
  `<skin>/<slot>` identity set against the staged one's and refuses on the
  difference, naming it:

  ```
  ValidateStagedBundle: the staged skeleton drops skin attachments the project's
  skeleton defines (default/shadow); a PSD reimport replaces bones, slots and
  skins wholesale, so committing it would destroy hand-authored attachments
  ```

  **What it compares:** the set of `<skin name>/<slot name>` pairs under `$.skins`,
  read from the skeleton **file** being replaced -- not the session's base
  document, because the file is what this commit overwrites and what the backup
  preserves.

  **What it does NOT compare, and this is the honest boundary:** anything *inside*
  a surviving attachment. A `<skin>/<slot>` present in both documents passes
  regardless of whether its `type`, `region`, `vertices`, `triangles`, `uvs` or
  `weights` changed, so a reimport that keeps an identity and replaces a mesh with
  a region attachment under the same name is **not** refused. Nor does it compare
  skin ordering, or anything outside `$.skins`. It is a check against *losing* an
  attachment identity, not against *altering* one.

  **Blast radius, corrected -- the refusal is UNCONDITIONAL.** The first version of
  this paragraph said the check "engages only where the current skeleton has skins
  the staged one lacks", which is true and misleading: `build_skeleton_document`
  erases `skins` **wholesale**, so the staged set is **always empty**. The
  difference is therefore the whole of the current set, and the consequence, in
  plain words:

  > **Blocked for a project whose hand-authored skins sit on layers the PSD STILL
  > PRODUCES.** `preserve` reaches only `Missing` layers, so a skin on a layer that
  > is still present has no escape short of editing the skeleton by hand. There is
  > no override -- `PsdReimportCommitOptions` carries `project_path` and
  > `update_provenance` and nothing else.

  **This row was wider before and is narrower now, which is the interesting part.**
  It first read *"permanently blocked for any project that has ever had a
  hand-authored skin, including a reimport whose only purpose is a `preserve=false`
  deletion"* -- accurate when it was written, and made false **in the reader's
  favour** by fixing the exclusion contract two paragraphs down: `preserve=false`
  is now a working non-destructive escape for layers the PSD has dropped. The old
  wording would have sent someone to a backlog row when a checkbox would have
  solved their problem.

  *Understating a remedy is its own kind of inaccuracy, and it is the more likely
  one to survive: nobody re-reads a limitation to check whether it got smaller.*
  When you fix something, re-read what you previously wrote about being unable to.

  It remains the right trade against silent unrecoverable loss, and it is a
  consequence rather than a footnote. **It is also CERTAIN, not merely reasoned:**
  because `staged_skins` is always empty, every project carrying any hand-authored
  skin is blocked **every time**, not occasionally. Only the size of that
  population is unmeasured. The two framings license different follow-ups -- "we
  reasoned it might be a problem" invites waiting for a report, "it is certain for
  everyone in this population" does not -- and the weaker one was mine. **The first version of the message then made
  a second mistake, caught in review: the remedy it prescribed was itself
  destructive and it did not say so.** It told the user to *"first remove those
  attachments from the project's skeleton"* -- so a user following the instruction
  destroys, by their own hand, exactly the data the refusal exists to protect.

  The message now leads with the **non-destructive** route and labels the other one:

  > There is no override in this version. If the affected layers are **GONE** from
  > the PSD, **mark them for deletion in the reimport plan** -- that is not
  > destructive and it tells the commit the loss is intended. If they are still
  > **IN** the PSD there is no non-destructive route: removing the attachments from
  > the project's skeleton by hand **DESTROYS** the same authored data this refusal
  > is protecting, is undoable only through the editor's undo, and should be
  > preceded by a backup.

  The message carried the same overstatement as the row until it was re-read
  alongside it: it offered *"mark those layers for deletion"* to everyone, when
  `preserve` reaches only `Missing` layers. It now says which case each route
  applies to. A per-identity version -- the committer knows which lost slots belong
  to `Missing` layers and could say so for each -- is a refinement under the same
  backlog owner, not written here.

  *A refusal that implies a remedy it does not provide is a small dishonesty; one
  that prescribes a destructive remedy without saying so is worse than no guidance
  at all.*

  **An informed override is backlogged, not designed here, and its scope is the
  REMAINING gap rather than the original one.** What is left after the contract fix
  is narrow and specific: *a hand-authored skin on a layer the PSD still produces.*
  `preserve` cannot reach it, because a still-produced layer is `Updated`, not
  `Missing`. That is the case an informed override exists for -- it needs a
  decision about what is confirmed and how the consequence is shown, which belongs
  with the review UI rather than with the committer. Owner: **MAR-190's
  successor**, raised by MAR-189 and explicitly *not* added to MAR-190.

  For projects this story creates the comparison never fires, because a
  PSD-derived skeleton has no `skins` at all. That is the only population it is
  quiet for.

  **Ordering is deliberate:** the structural comparison runs **after** the runtime
  build, so a project failing both keeps the more specific diagnosis. R2(c) still
  refuses on its deform message; **I20** (delete the comparison) reddens **R2(d)
  alone** and R2(c) passes unchanged, which is the uniqueness evidence.

  The underlying cause is unchanged and is not this story's: `build_skeleton_document`
  erases `skins` wholesale (`psd_import.cpp:1041`). MAR-189 refuses rather than
  fixing the importer.

- **EXDEV, and it is narrower than "unmeasured".** One volume on this machine, so
  a real cross-device rename could not be produced. But `write_file_atomically`
  places its temporary in the **destination's own directory**
  (`atomic_file_write.cpp:61-78`), which makes `EXDEV` **structurally
  unreachable** on the placement path rather than merely unobserved -- the
  design's cross-filesystem note is what buys that, not a hazard it leaves open.
  What is genuinely uncovered is the *handling* of a cross-device error, and the
  seam this story added can inject one directly: a failpoint returning
  `std::make_error_code(std::errc::cross_device_link).message()` after any
  `Place*` step exercises the rollback for it. Not written, because it would
  assert the rollback and not the EXDEV path itself.
- **The prune/validate exclusion had two possible contracts and now has one,
  stated.** `ValidateStagedBundle` forgives some lost `<skin>/<slot>` identities.
  Which ones was genuinely ambiguous, and review found the ambiguity by mutating
  the guard and watching **nothing** go red:

  - *(a) an identity the PRUNE removed* -- what the code originally implemented,
    via `if (erase(...) > 0U)`;
  - *(b) an identity the USER asked to delete* -- every `Missing && !preserve`
    layer, whatever the staged document happens to contain.

  They diverge exactly where the staged document does not carry the entry --
  **which, with this importer, is always**, because it erases `skins` wholesale and
  rebuilds `slots` from the candidate. So (a) was dead code reachable only by a
  planted fixture, and worse, it **refused to delete something the user had
  explicitly marked for deletion**.

  **(b) is now the contract**, recorded above every `continue` in the prune loop and
  before the staged document is consulted at all, so the decision cannot depend on
  what an importer left behind. The first attempt put it one line too low -- below
  two `continue`s -- which is the same defect in a new place and is noted at the
  site. The practical consequence is that `preserve` becomes the non-destructive
  way past the unconditional refusal above.

  Both directions are pinned: **I23** (revert to reading (a)) reddens
  **R5(d)/delete**, and **I24** (forgive every lost identity) reddens **R2(d)**, so
  the exclusion can neither be narrowed into uselessness nor widened into
  disarming the check. **R5(d)** is the case that distinguishes the readings, on
  the shape the importer actually produces; **R5(c)** covers the planted shape a
  future skin-preserving importer would create.

- **The naming rule has TWO implementations, kept in sync by hand.**
  `plan_scenario` (`psd_import_smoke.cpp`) re-derives the staged atlas name from
  the target atlas's `image` stem instead of calling `plan_project_reimport`, which
  is the production site. Q13 does not close this either -- it passes the names in
  as literals. **A5 is the only detector**, and only because the A-cases go through
  the real dispatcher. This is the fixture-blindness class one level up: two
  parallel implementations agree by construction until somebody edits one, and the
  test half agreeing with itself proves nothing about the production half. The
  reviewer confirmed it bites in both directions -- reverting the production naming
  site reddens **A5**, reverting the committer's derivation reddens **R1** -- so
  the coverage exists; what does not exist is a single source for the rule.
  **Deferral costs LOCALITY, not coverage**: a break surfaces in an agent-approval
  case rather than beside the rule that broke. **Owner: MAR-191**, as part of its
  validation sweep -- the fix is for `plan_scenario` to call
  `plan_project_reimport`, and an unowned row about a hand-synced duplicate is the
  kind that survives three stories.

- **R1b's `image` clause is a witness for a structural reason, not for want of
  looking.** The earlier wording ("no mutation reddens it first") understated it.
  The clause has exactly one degree of freedom left: the committed atlas's *bytes*
  are pinned by R1, its *location* is pinned by R1b's own path-set clause, and all
  that remains is the staged atlas's **name** -- which is supplied by **test code**
  in every R-case. *A clause whose only free variable is set by the test cannot be
  falsified by mutating the product.* R1c carries the evidence, by asserting the
  refusal message.

- **A5/A6 do not go through the C ABI.** `MarrowProject` is opaque outside
  `marrow_c.cpp` and approval needs the session and the review queue, so A1-A6
  drive `AgentCommandDispatcher` and `apply_agent_review` directly from
  `psd_import_smoke`. `agent_dispatch_smoke` keeps the ABI-level dry-run and
  review invocations and owns **A7**, which now snapshots the whole tracked bundle
  rather than only the `.marrow`.
- **M1-M3 are hand-run.** `test_client.py` is not in CTest and needs a live editor
  with the agent socket listening. AC6's "MCP tests" is satisfied at that standard
  and no higher.
- **AC5's approval clause is read as editor-only.** MCP gets no approve tool,
  following `agent.resume`'s *"only the editor can restore access"*. A stricter
  reading needs a 67th operation and moves eleven guards and two Python assertions.

## MAR-188 Plan Provenance-Aware PSD Reimports Validation Results

Baseline `bb3652f`. Every verification build was made in an isolated tree seeded
from `git archive HEAD` and overlaid with **only this story's own files**, because
a review of MAR-187 was live and mutating tracked files throughout. The shared
`./build` was never used.

**`bb3652f` is not in the log, and that is expected.** MAR-187's review amended its
commit mid-story, to `f3a3768`, adding `cmake/CheckFrameBodies.cmake`, the
`marrow_frame_body_check` target and `tools/inversion/`. Task 0's measurements
below were taken at `bb3652f` and are **left exactly as measured** -- a historical
measurement is not rewritten to match a later tree. What was re-run against the
amended base is the *final* verification: clean all-target build with **0
warnings**, `ctest` **22/22**, every smoke green, `agent_dispatch` **437**
`[ OK ]`, and MAR-187's own frame-body check passing both POST_BUILD and on
demand.

**Every baseline number was re-measured at the actual parent `f3a3768`, not
carried across the baseline change** -- a number does not survive a rebase any
better than a line anchor does. Measured there: **0** warnings, `ctest -N` **22**,
`agent_dispatch` **437** `[ OK ]`, registry **66** at `agent_dispatch_smoke.cpp:42`,
**11** `!= 66U` guards. All identical to the `bb3652f` figures. The `[ OK ]` count
was expected to have moved and had **not**: MAR-187 added its cases to
`editor_project_smoke.cpp`, which `marrow_agent_dispatch_smoke` does not build. MAR-188 reverted none of that amendment; the single line it changed in the
amended `editor_project_smoke.cpp` is the `five`->`six` prose fix recorded as E13.

### What was measured before any code was written

Task 0 ran ten gates. The results that changed the story are first.

- **`$.editor.import_sources.psd` already round-tripped verbatim, with zero code.**
  `preserved_root` stores the whole parsed tree (`project.cpp:7953`),
  `build_project_value` seeds the editor block from it (`:5023`) and re-emits it
  wholesale (`:5089`). A probe injected the key, loaded, and re-serialized: the
  **entire subtree came back**, nested `group_path` array and all. The
  consequence is the story's defining hazard — **an AC1 round-trip case written as
  a text search over `serialize_project()` passes on an empty commit.**
- **`rebase_project_paths` left it stale**, because it starts `ProjectData result
  = project;` and walks only the five struct families. That is AC2's detector.
- **Both rebase arms measured.** Into a **parent** directory references stay
  relative (`fixtures/player_idle.mskl`); into a **sibling** they come back
  absolute, because `make_project_relative_path` returns the absolute form
  whenever the relative one would need `../`. P5 and P7 author both.
- **`create_minimal_project`'s `preserved_root` is an empty OBJECT, not null**
  (`is_object=1 is_null=0`). A case shaped as `is_null()` would **fail on correct
  code**. Test `is_object() && as_object().empty()`, which is the shape
  `build_project_value` itself uses at `:5007-5009`.
- **The pristine baselines**: 0 warnings, `ctest -N` 22, 22/22 green,
  `marrow_agent_dispatch_smoke` **437** `[ OK ]`, registry **66** with **11**
  `!= 66U` guards split **7 graph / 2 constraints / 2 timeline**,
  `agent_dispatch_smoke.cpp:42` (**not** `:39`), `test_client.py:53,55`.
- **`-Wswitch` re-measured at `bb3652f`: 25 warnings, 18 `authoring.cpp` / 6
  `agent_handlers_editing.cpp` / 1 `timeline_controller.cpp`** — reproducing
  MAR-185's figure and its per-file split exactly, after MAR-187 added ~6,500
  lines. Build exit **0**: it warns, it does not fail.
- **The two PSD fixtures differ in exactly 4 bytes**, at offsets 286/290/294/298,
  all in `body`'s bounding box. Layer sets identical. Confirmed with `cmp -l`.
- **`format-spec.md:685-707` already documents `$.editor`'s optional keys**, so
  `import_sources` joins them. That turned a predicted no-op into real work.

### The re-anchor gate: six findings, one blocking

- **BLOCKING — `assign_slot_names` does not exist.** Both documents cited
  `psd_import.cpp:814-826` as that symbol; `git grep assign_slot_names HEAD`
  returns **nothing repo-wide**. The lines and the described behaviour are right;
  the enclosing function is `parse_psd_document` (`:625`). A fictional primary
  anchor is exactly how MAR-185's D23 let a third parser escape a name-based
  sweep. Corrected in all three sites.
- `editor_project_smoke.cpp:17948-17951` (MAR-186's registration, and MAR-188's
  insertion point) → **`:19985-19988`**, +2037. **Correct at `687ed4f`**, verified
  against that blob; MAR-187 added 2045 lines. MAR-188 registers after `:19996`.
- `ScopedRenameCallback` `:13025` → **`:13034`**; `CMakeLists.txt:812`/`:850` →
  **`:814`**/**`:852`**, and `add_library(marrow_editor …)` is now `:499-523`;
  `AGENTS.md:2337`/`:2348` → **`:2719`**/**`:2730`**, all correct at `687ed4f`.
- **MAR-187 touched none of this story's core files.** `git diff --stat 687ed4f
  bb3652f` over `project.{cpp,hpp}`, `psd_import.{cpp,hpp}`, `session.cpp`,
  `json.cpp`, `skeleton_parse.cpp` and `psd_import_smoke.cpp` is **empty**. The
  only real merge points were `CMakeLists.txt` and `editor_project_smoke.cpp`.
- **All six of MAR-180's path-family citations re-verified as stale** at
  `bb3652f`, none naming its construct. Measured drift **+59 to +512** — a range
  the incoming brief had reported as "+288..+303", a figure that appears in
  neither planning document.

### Result

`$.editor.import_sources` is a typed, parsed, validated project field; Save As
rebases it as the sixth path family; and `plan_psd_reimport` returns a
deterministic added/updated/missing diff without touching anything outside a
caller-supplied staging root.

- **The provenance vocabulary** — `PsdLayerProvenance`, `PsdImportProvenance`,
  `ProjectImportSources`, and `ProjectMetadata::import_sources`. Eleven load
  rejections, each asserted on its **message and its JSON path**.
- **Serialization does BOTH halves.** Parsing without emitting would silently
  discard every in-memory edit and write the stale preserved copy — making Save As
  *worse* than before the story. Emitting without erasing would leave a cleared
  provenance on disk forever. `build_project_value` assigns or erases, and an
  `import_sources` whose `psd` is disengaged serialises as an **absent key**.
- **The sixth rebase family** (`project.cpp:8056-8069`), two paths, reading from
  `project` and writing to `result` like the five before it.
- **`plan_psd_reimport`** in a new `psd_reimport_plan.{hpp,cpp}` pair — a new pair
  rather than an addition to `psd_import.hpp`, which deliberately does not depend
  on the project layer. The caller supplies a staging **root**, never an output
  path: `write_imported_layers` `remove_all`s whatever directory it is handed, so
  an API that cannot be handed a directory cannot be handed the wrong one.
- **A PSD synthesiser** in `psd_import_smoke.cpp`, because every classification
  case needed input that did not exist.

### The PSD synthesiser, and the gate that made it trustworthy

The repository contains **two** PSDs, four bytes apart, with identical layer sets:
no added, removed or renamed layer, no group move, no duplicate name, and **no
group nested deeper than one level anywhere**. Eight cases needed inputs that did
not exist, and no generator existed either. Hand-authoring six more ~19KB opaque
binaries was rejected: a reviewer could not see what a fixture contained without
running something.

**Q0 gates the synthesiser, and its extra clauses caught a real defect on their
first run.** The single-clause form originally specified — "reproduce the fixture's
layer and bone report" — **passed completely** against a synthesiser emitting
**3 channels** where the checked-in fixture is **RGBA, 4**. The layer report and
bone report were already identical; only the added header comparison saw it. That
is precisely the "plausible but wrong PSD" the gate exists to stop, and Q1's
`proposed_image_file` clause would have rested on it. The four clauses:

1. **Report** — layers, groups, slots, attachments, bones, **`image_file`** and
   boxes, compared against the checked-in fixture's own output measured **at run
   time** rather than hard-coded.
2. **Pixels** — the extracted PNG is compared byte for byte against the same RGBA
   re-encoded through the importer's **own** `write_rgba_png`. Comparing files
   rather than decoding one keeps the gate free of a second image codec, the same
   objection that ruled out teaching the synthesiser PackBits.
3. **Header** — signature, version, channel count, depth and colour mode against
   the fixture's own bytes. Synthesised **3182** bytes against the fixture's
   **19636**; the difference is the composite image-data and image-resource
   sections, which the parser never reads.
4. **Q0b — the duplicate-name branch.** `parse_psd_document:814-826` takes the
   joined-path dedup only when the document-global census exceeds 1, and Q0's tree
   has no duplicates, so that branch was **ungated**. Q0b asserts the slot names
   resolve to `torso/body` and `body`.

   **The reason originally given for Q0b was wrong, and the truth strengthens it.**
   Both this story's brief and its design said "Q5 and Q6 live in that branch and
   have no other gate". Measured: **neither does.** Q5's layer names are `b|c` and
   `c` — *distinct*, so the census is 1 for each and the branch is never entered;
   Q5 exercises the planner's `|`-escaping, an unrelated mechanism. Q6 does reach
   the branch at import time, but asserts on `build_identity(group_path, name)`,
   which reads the **original** layer name and never `slot_name`. So **Q0b is that
   branch's only gate anywhere** — a stronger claim than the one used to justify
   it. Recorded because the conclusion was right for the wrong reason, which is
   its own defect: a correct decision resting on a false premise survives only
   until someone checks the premise.

Q0 and Q0b run **first**, so a synthesiser regression is attributed to the gate
rather than to whichever case notices.

### Inversions -- actual outcomes, not predictions

Twenty run. Each: mutate → delete objects → build **all** targets → run → record
the actual text → restore → rebuild → `cmp`. Seventeen bit as designed.

| # | Mutation | Outcome |
| --- | --- | --- |
| I1 | Drop the `parse_import_sources` call | **P2**, `provenance must survive a round trip through the typed field` |
| I2 | Parse but never assign `editor_object["import_sources"]` | **P3**, `expected art/other.psd, got art/hero.psd` |
| I3 | Assign but never erase on `nullopt` | **P4(a)** |
| I4 | Serialise `psd == nullopt` as `{}` | **P4(b)** |
| I5 | Sixth family rebases `source_path` only | **P5**, `layers_directory` clause by name |
| I6 | Sixth family rebases `layers_directory` only | **P5**, `source_path` clause by name |
| **I7** | Read `source` from `result` instead of `project` | **DID NOT BITE, and cannot.** `result = project` is a copy, so `source` and `target` **alias**, and neither field is read after being written. A provable no-op. See below |
| I7b | The shared lambda captures `result` | Bites at **MAR-180 S2** — shared code, like I8 |
| I8 | Remove `is_absolute()` from the shared lambda | Bites at **MAR-180 S3**, which runs before MAR-188's cases. P6's subject confirmed directly on the artefact instead: against an I8 build an absolute provenance path returns as `abs_hero.psd`, neither absolute nor byte-identical |
| P8 d,e,h,i,j,k | Remove each domain rejection, one at a time | Each fails **its own** P8 arm |
| I9b | Remove the pre-check **and** stage into the project's own directory | **Q6**, `planning changed the project directory`, set difference naming four created/missing files. The detector for `expect_inert` clause 5, and the proof the guard and the listing are independent layers |
| I9 | Stage into the project's own directory | Bites on the planner's **own containment guard**, not on Q8's directory listing as designed. Review found the guard sat **after** `import_psd_to_runtime_bundle`, so it **reported an escape that had already happened** -- the project directory really was polluted. MAR-188 moved it **before** the import, where it prevents. Either way the listing clause is never reached, so it is a WITNESS -- see below |
| **I10** | Leave `existing_skeleton_path` unset | **Did not bite as designed.** Every `proposed_*` target comes from the CANDIDATE parse, not from the staged merge, so the plan is identical either way. A Q8 clause asserting the staged skeleton carries the project's authored animations was added, and I10 then bites: `the staged skeleton must carry the project's authored animation 'attack'` |
| I11 | Fall back to `slot_name` when the identity misses | **Q3**, `expected body -> Added, got body -> Updated` |
| I12 | A similarity matcher before classification | **Q2**, `expected torso\|arm_left -> Added, got torso\|arm_left -> Updated` |
| I13 | Remove identity escaping | **Q5** — and note the *mechanism* differs from the prediction: the two layers now collide and are **refused** by the duplicate check rather than silently collapsing to one |
| I14 | Pair candidate duplicates positionally | **Q6**, `duplicate candidate identities must be refused, got a successful plan` |
| I15 | `preserve{false}` | **Q9**, `torso\|arm_l: preserve expected true, got false` |
| I16 | Emit in candidate-record order | **Q10/Q1 ordered clause**, `first order difference at index 1` |
| I17 | Accumulate counts in a parallel loop missing an arm | **Q1's redundant count clause, and nothing else** — the demonstration that the clause is redundant but not decorative |
| I18 | Divider written before the group's children | **Q0**, `PSD folder end marker appeared without an open folder.` |
| I19 | Drop `bone_name` from the provenance copy | **Q1's full-tuple clause**, `bone='' expected torso` |

**I7 is not a valid inversion of this implementation and should not be carried
forward.** The property the design wanted — never resolve against the
already-overwritten `result.source_path` — lives in the **shared lambda**, which
I7b and I8 mutate; the sixth family cannot express it. P5's two `weakly_canonical`
identity clauses are still worth keeping, but nothing MAR-188 owns alone detects
them.

### One witness, not two -- and the difference was decided by running it

Applying this story's own P1 rule to the rest of its own suite raised a candidate
second witness -- and **measuring it refuted the label**. The sequence is worth
keeping, because the wrong answer was the plausible one.

| Case / clause | Inversion that turns it red | Status |
| --- | --- | --- |
| P1 (byte-identical serialization of a pre-story project) | **none** | **Witness.** Its claim -- backward compatibility -- is real and nothing else makes it |
| `expect_inert` clause 5 (full recursive directory listing) | **I9b** | **Gate.** Fails at **Q6** by run order: `Q6: planning changed the project directory.` with a set difference naming four files |
| Every other P- and Q- clause | I1-I6, I7b, I8, P8 d/e/h/i/j/k, I9-I19 | Gate |

**What was nearly recorded, twice, and why both versions were wrong.** The first
draft called clause 5 a witness with no detector. The second explained that the
guard move had *created* coverage which had not existed a commit earlier. **Both
are false, and the second was measured to be false rather than argued away.**

The mutation was reconstructed against the ORIGINAL post-hoc guard -- remove the
post-hoc check *and* retarget the layer directory -- and it produces the
**identical** failure:

```
Q6: planning changed the project directory.
  missing: .../project/staged_layers/b_c.png (75 bytes)
  missing: .../project/staged_layers/c.png (75 bytes)
  created: .../project/staged_layers/torso_body.png (75 bytes)
  created: .../project/staged_layers/torso_body_2.png (75 bytes)
```

So **clause 5 had a detector all along.** The guard move neither created nor
destroyed coverage; it changed only *which* mutation exposes it -- remove the
post-hoc check before, remove the pre-check after. The row was wrong when it was
written and would have been equally wrong one commit earlier.

The two layers are independent, which is the shape defence in depth is supposed to
have: the guard prevents the escape, and if the guard is ever removed or weakened
the inertness listing still catches it. That was true before the guard moved too.

**The real lesson, which is less comfortable than "re-run the question".** A
coverage claim is a **universal quantifier** -- "*no* inversion turns this red".
Observing that one mutation fails to reach a clause establishes nothing about it;
the claim is discharged only by constructing the mutation that *would* falsify it
and watching what happens. The first row generalised from a single mutation's
outcome to a statement about the whole register, and stated an inference in the
grammar of a measurement.

Note also what nearly happened to the *correction*: the tidy story -- "fixing the
guard created coverage" -- was plausible, flattering to everyone, and false. It
was caught only by running the old code. **A correct conclusion resting on a false
premise survives only until someone checks the premise**, which this story already
recorded for Q0b and then immediately re-enacted one section away.

**A guard placed after the operation it guards REPORTS rather than PREVENTS.**
MAR-188 originally checked containment at the end of `plan_psd_reimport`, after
the importer had already run. The check was correct and its message was accurate,
and it was still the wrong guard: `write_imported_layers` had already `remove_all`ed
and rewritten the destination by the time it fired. The error text is what gives it
away -- an escape can only be *reported* if the work that escaped already
completed. The fix is placement, not logic. **When a guard's claim is "nothing
outside X is written", read where it sits relative to the writing.**

### P1 is a compatibility witness, not a gate

**The design shipped an instance of the defect class it defines.** §0.3.1 names
"a gate that passes on unchanged code" and then claimed P1 "earns its place only
through inversion I3" — while §7 and the plan's §B both attribute **I3 to P4**.
P1 was measured **green on the pristine tree** and no inversion of the twenty
turns it red.

P1 is kept and **relabelled**. Its claim is real and nothing else makes it: a
pre-MAR-188 project serialises **byte-identically** after the story —
`player_idle.marrow`, **6111 bytes**, `cmp`-clean against a baseline captured
before a line was written. What it is not is evidence that any MAR-188 code
works. **P1 has no story-owned inversion, by design, and that is recorded rather
than repaired.** Never read "P1 green" as vindication.

The rule this produced: **a case that is green before the implementation exists is
a witness, not a gate, and must be labelled as one** — with the inversion that
would catch its subject named, or its absence stated.

### A MAR-188 commit carries `plan-mar190`'s durable entries -- attribution

**`3acbf58` is not solely MAR-188's work, and `git log -S` will mislead you.** Its
`AGENTS.md` delta is **116 added lines, of which about 9 are MAR-188's** (the
inertness coverage correction). The rest was `plan-mar190`'s uncommitted work,
sitting dirty in the shared worktree when MAR-188 staged the file:

- the ImGui modal-Escape mechanism (`imgui.cpp:13103`, `:14873`) -- **`plan-mar190`**;
- *"A zero result is evidence only once the pattern is known to match something"*
  -- **`plan-mar190`**;
- the rewrite of the present-tense / historical `*Rule:*` block -- **`plan-mar190`**.

Nothing was lost and nothing needs extracting; `plan-mar190`'s Task 10 checks
whether both entries still exist and re-lands them only if a sweep dropped them.
This note exists so that a future reader running `git log -S` on either entry
lands on a MAR-188 commit and does **not** conclude MAR-188 authored it.

**This is the shared-path hazard this same file documents, and it caught the agent
that had just written the entry about it.** `git add <path>` stages the whole
file including another agent's uncommitted hunks; the technique that prevents it
is `git apply --cached`, which stages hunks rather than files. Knowing the rule
and applying it are different things, which is the reason it is written down twice
now -- once as guidance and once as an instance.

### Document errors found (7)

| # | Where | Error | Resolution |
|---|---|---|---|
| E8 | Design §0.3.1 | P1 "earns its place only through inversion I3", contradicting §7 and §B, which attribute I3 to P4 | P1 relabelled a compatibility witness; §0.3.1 corrected and now carries the class's first *self-inflicted* instance |
| E9 | Design §9, plan §A.3 | `assign_slot_names` — a symbol that exists nowhere | Re-worded to `parse_psd_document`'s document-global slot dedup. **Blocking**; found by the re-anchor gate |
| E10 | The incoming brief | MAR-187 "put `OverlayRecordKey` in its own `safe_fix.hpp`, leaving `diagnostics.hpp` unmodified" | **False.** `diagnostics.hpp` +71, and the key is at `diagnostics.hpp:163`. A prediction about a then-unlanded story, so corrected rather than preserved. No effect on MAR-188 |
| E11 | The incoming brief | MAR-180's citations drift "+288..+303" | The staleness re-verifies; the **figure was invented** and appears in neither planning document. Measured **+59..+512** |
| E12 | The incoming brief | "your two documents are the only untracked files" | A third, `build-specrev187/`, existed |
| E13 | Design §2.5 | Names **three** prose sites saying "five families" | There are **four**: `editor_project_smoke.cpp:13489`, MAR-180 S5's own doc comment, is in neither document. Found by Task 7's `grep`, which is why that sweep is in the plan rather than left to attention |
| E14 | Design §2.6 | `make_psd_provenance` can use `make_project_relative_path` | That function is **inside `project.cpp`'s anonymous namespace** and unreachable. A thin public `project_relative_path` wrapper was added to `project.hpp` rather than duplicating the house rule |

### Not independently covered

- **`make_psd_provenance` and `plan_psd_reimport` have no production caller.**
  Both are exercised only by the smoke. MAR-189 owns the commit path that calls
  them, and MAR-190 the GUI. Verified by `grep`: the only call sites outside the
  new source file are in `psd_import_smoke.cpp`.
- **`preserve` is exposed and nothing consumes it.** MAR-190 owns the checklist
  that lets a user opt into deletion.
- **Photoshop's real layer-record order is unverified and unverifiable here.** The
  parser requires each group's `lsct` 1/2 header **before** its children and the
  `lsct` 3 divider **after**; both checked-in fixtures are authored that way and
  the synthesiser follows them. There is no Photoshop-authored PSD in the
  repository and none can be produced in this environment. If the real order is
  the inverse, every group-bearing real-world PSD fails at `psd_import.cpp:734` —
  a **pre-existing** property of the importer, not something MAR-188 introduced.
  Flagged to MAR-189/190, which own the real import path.
- **Slot names remain unstable across imports.** Adding a layer can rename an
  untouched slot, because the dedup uses a document-global census. MAR-188 records
  what the names *were* and keys on something else; it does not make the importer's
  naming stable. A user will see it as `proposed_slot_name` differing from
  `current_slot_name` on an `Updated` row.
- **No save-time validation of `image_file`.** Every write path constructs it from
  `filename()` and structurally cannot produce a separator, so a
  `validate_project_for_save` rule would have no reachable failing input —
  "correct by inspection, unreachable by test".
- **`marrow_psd_import_smoke` is still not in CTest.**

### MAR-180's deferred criterion: discharged as worded, PERMANENTLY OPEN as generalised

MAR-180 could not satisfy "Save As rebases the PSD provenance path" because no
such field existed. **That clause is now discharged in full**: AC1 creates the
typed field, AC2 registers it as the sixth family, and P5/P6/P7 with I5/I6 prove
it.

A precision MAR-180 could not state, because it had not measured it: the criterion
was unsatisfiable **not** because the data could not be stored — it already
round-tripped — but because it was stored **opaquely**. MAR-188 discharges it
precisely by moving the key out of the opaque copy into a typed field.

**MAR-180 also generalised it to "rebases every relative path", and that clause
can never close.** MAR-188 makes it six known families and seven rebased fields
out of an **unbounded** document. `preserved_root` is *defined* as "whatever this
code does not understand", so a rule quantified over every relative path is
quantified over a set the program cannot enumerate. Any path a future document
carries under an unparsed key remains unrebased and silently stale on Save As.

**This is not a MAR-188 deliverable and must not be written up as one.** Closing it
needs a decision nobody has taken — a schema-strict loader, or a declaration that
unparsed paths are unsupported — and each is a story in its own right with its own
compatibility cost. Recorded here as permanently open, stated plainly rather than
quietly rescoped into "six families, done".

## MAR-187 Add the Problems View and Safe Fixes Validation Results

Validated 2026-09-01 against a from-scratch `rm -rf build` tree at MAR-186's
final commit `687ed4f`. MAR-187 gives the editor a **Problems** window that
groups and filters MAR-186's diagnostics by severity, navigates to a problem's
typed target, and offers exactly three allowlisted repairs — each through one
validated transaction producing exactly one undo entry.

**Three layers, and the boundary is the build graph rather than a claim.**
`problems_model` and `safe_fix` compile into `marrow_editor`, which contains no
`shell_*.cpp` and links no ImGui, so the model layer *physically cannot* reach
`ShellState`, an ImGui symbol or a window title. `shell_problems.cpp` reads a
`ProblemsView`, emits widgets, and on a click calls `plan_issue_navigation` or
`apply_safe_fix` — Task 9 greps it for `begin_edit`, `transaction`, `std::sort`
and `.erase(` and finds none.

**Nothing about the file format moved.** `.marrow` gains no key, `.mskl` stays
version 1, `.mbin` stays version 2, the C ABI is untouched, no fixture was
edited, no runtime file was touched. The agent registry is **unchanged at 66** —
MAR-187 adds no operation and no MCP tool, so the count sweep is a **non-effect**
gate — and `ctest -N` is **22** before and after. `CMakeLists.txt` gains exactly
three lines, naming three new sources inside two existing targets; no new target,
no new test, no new include directory.

### The downstream amendment to MAR-186, and the gap that forced it

MAR-186 shipped `DiagnosticCode` and `DiagnosticOverlayFamily` as typed members
of `DiagnosticIssue` — the `D6` this story's design called blocking — so a fix
can find the right **vector** without splitting `identity` on `|`. **That answers
"which of the eight vectors" completely and does not answer "which record within
the vector",** and neither story's documents ever posed the second question.

Measured, by writing the repair and finding it had nothing typed to work from:

| Family | Record key | `DiagnosticTarget` carries | Sufficient |
| --- | --- | --- | --- |
| `Transform` | animation, bone, **channel** | animation, `BoneSelection` | **NO** |
| `Deform` | animation, slot, **attachment** | animation, `SlotSelection` | **NO** |
| `PreviewStaleSkin` | the **skin name** | a panel, and nothing else | **NO** |
| Inherit / DrawOrder / Event / SlotColor / SlotAttachment / MeshWeight | — | — | yes |

`collect_orphan_animation_overlays` says the first one in as many words: *"The
channel token is part of the key: two channels on one bone are two independent
overlays."* Two transform overlays on one bone in one phantom animation produce
two issues with **identical** `DiagnosticTarget`s. The third is the sharpest,
because `preview.stale_skin` is not an overlay at all and its target carries only
a panel — the skin name it is about exists **nowhere** but inside `identity` and
the message prose.

MAR-187 therefore added `OverlayRecordKey` to `include/marrow/editor/diagnostics.hpp`
and populates it in `src/editor/diagnostics.cpp`. **This is a deliberate
downstream amendment to a shipped, already-reviewed story, recorded as an event
and not as a precedent.** Reopening MAR-186 would have invalidated a live review
for a field MAR-186 itself never reads. Three choices inside it are load-bearing:

- **It sits on `DiagnosticIssue`, beside `family`, not on `DiagnosticTarget`.**
  A target is *where the user is taken*; two of these fields name records the
  user is never navigated to.
- **`make_issue`'s new parameter is REQUIRED and has no default.** A defaulted
  one would let every existing call site keep compiling while silently emitting
  an empty key. Making it required turned "you forgot the key" into a compile
  error at all seven pre-existing call sites — the only half of the population
  problem a compiler can reach.
- **Never by parsing `identity`.** MAR-186's escaping exists precisely because a
  `|` inside a user animation name would route an erase to the wrong record.

### What the compiler polices, measured in this tree

| Step | Result |
| --- | --- |
| Pristine, all objects deleted, all targets built | **0 warnings** |
| A throwaway fourth `ProblemsSeverityFilter` value | **2 warnings, both `[-Wswitch]`** — `problems_model.cpp:20` (`filter_admits`) and `editor_project_smoke.cpp:17793` (the suite's own `filter_name` helper) |
| Value removed, rebuilt | **0 warnings** |

Apple clang 21.0.0, `-Wswitch` on with no flag. Every dispatch in MAR-187 is an
exhaustive `switch` with **no `default:` arm**, and the compiler found the test
helper too — which is why that helper is a switch rather than an if/else chain.

**What is NOT policed, and it is not the `static_assert`.** MAR-186's review
established that `static_assert(kSweptOverlayFamilyCount == 7, …)` is a
**tautology** providing zero compile-time protection. MAR-187's header comment
originally repeated the older "a guard, not a proof" wording and was corrected to
match. The real guards are the cases that compare **full identity lists** —
MAR-186's G1, and MAR-187's X1, which additionally carries a sibling record per
key so that a fix erasing the right vector but the wrong **record** fails by name.

### What was measured before any code was written

Task 0 ran fourteen gates. **Four were blocking**, and three of those invalidated
something the documents said rather than merely a line number.

| Gate | Answer |
| --- | --- |
| Tip | **`5a9e91f`**, not the `ca277fd` the brief named — but the two trees are **byte-identical** (`a74630b9…`); only the commit message was amended. MAR-186 was amended once more during implementation, to **`687ed4f`**, which is this story's parent |
| `ctest -N` | **22**. From-scratch rebuild of ALL targets: **0 warnings** |
| MAR-186's D1-D5 | Present, exact signatures. **D6 shipped**, but in a **different shape** than either document specified — see below |
| Vocabulary is new | `grep -rnE "\bProblemsView\b\|\bProblemsGroup\b\|\bSafeFixKind\b\|apply_safe_fix\|kProblemsWindowTitle"` → **0** |
| `-Wswitch` | **Fires with no flags at all.** Compiled, not inferred |
| Two frame bodies | **Confirmed**, and swept rather than inherited: `DockSpaceOverViewport` has **four** sites; the other two are *partial* bodies. `IniFilename = nullptr` in both; `kDockLayoutVersion` stays **4** |
| `begin_edit` + `cancel()` (A10) | **All seven values identical**, with a NON-ZERO redo stack (1). §B.1's first uninverted entry is confirmed measurement, not assumption |
| `adopt_runtime_sources` (A11) | **`project_revision` 2→2, `runtime_revision` 1→2, `preview_revision` 1→2**, and the report gained exactly `preview.stale_skin\|mage_arm`. V8(c)'s driver verified end to end |
| Empty `preview_skins` (A12) | Saves and reloads with size 0; the control (an empty **name**) is refused with `preview skin names must not be empty` |
| The transposition (A13) | **Confirmed** — and the predicted rejection **does not exist**; see the document errors below |
| `erase_all_timeline_edits` (A14) | **Unreachable**, exactly as cited: `namespace {` `authoring.cpp:20-1309`, `:123`, `:136`, in no header |
| Registry | **66** rows / **11** `!= 66U` guards / **2** Python `== 66` |
| Fixture | `serialize_project` = **6111 bytes**, sha256 `c7d6c6de…`, **0** issues |
| Re-anchor gate | 39 anchors: **34 exact, 5 drifted, 0 non-resolving** |

**The tree was under concurrent mutation for the whole of Task 0**, by the review
running MAR-186's inversion register in the shared worktree and shared `build/`.
The first `marrow_project_smoke` run exited 1 with `MAR-186 G1: returned 6
issues, expected 7`. That was the **reviewer's own inversion**, not a defect.
Every measurement was re-taken against an isolated `git archive HEAD | tar -x`
tree with its own build directory, and every source anchor was re-checked against
`git show HEAD:` blobs rather than the worktree. The pristine tree was green.
**Reporting that G1 failure as a finding would have been a confident, specific,
wrong claim produced by a sweep that was correct within an unexamined scope.**

### Result

Every project case runs **inside the standing `player_idle.marrow` invocation**
and builds its own throwaway project there; every shell case runs on the **main**
rail, after the parameter-mode early return. No new command line, no new binary,
no new CTest target.

**No case asserts only a count.** Every one compares the **full sorted identity
list** and reports a mismatch as a named set difference.

| Case | What it pins | Result |
| --- | --- | --- |
| **V0** | An issue-free report yields an empty view; collect + build + plan move none of a session's seven observable values. Prints 6111 bytes / sha256 `c7d6c6de…` every run. **Witness** | PASS |
| **V1** | Error group **first**, over a report whose **lowest identity is a Warning**; both groups' full identity lists; strictly increasing | PASS |
| **V2** | An empty group is **omitted**, not emitted empty | PASS |
| **V3** | `error_count`/`warning_count` are the **collector's** under all three filters; only `visible_count` moves | PASS |
| **V4** | Each filter's exact identity list, as a set difference | PASS |
| **V5** | A row identity survives an insertion that sorts before it (index 0 → 1), byte-identical; a missing identity resolves to nothing | PASS |
| **V6** | `target_missing` with **no** selection for the orphan weight target, naming skin/slot/attachment; every other issue resolves | PASS |
| **V7** | All **seven** `DiagnosticCode` values: panel, animation, vertex, and every typed selection read **by name**, replayed through `SelectionSet::replace` | PASS |
| **V8** | (a) unchanged → no refresh; (b) a project edit → refresh; (c) **runtime-only** adoption bumps `runtime_revision` while `project_revision` holds, still requires a refresh, and yields a `preview.stale_skin` the old report lacked | PASS |
| **X1** | One **record** of twelve, not one family and not one animation — including the four siblings differing only by transform **channel**, deform **attachment**, inherit **bone**, and **slot** (twice); one undo entry; byte-identical undo; redo reproduces | PASS |
| **X2** | Twelve removals, twelve undo entries; `ghost` **absent** from the materialized skeleton after a real save → LOAD; the `preview.stale_animation` the overlays had been propping up now appears | PASS |
| **X3** | The orphan weight target goes; the resolvable edit survives, by identity **and** by its own fields | PASS |
| **X4** | One **vertex**, not one attachment: repairing vertex 0 leaves vertex 2 reported; no second weight record at transposed coordinates; a second application is **refused by name** | PASS |
| **X5** | `active_animation` becomes the **substituted** value (`aim`), and survives save → LOAD | PASS |
| **X6** | Both copies of the stale skin go; **both copies of the resolvable duplicate stay** | PASS |
| **X7** | A **74**-string corpus (three ids, `""`, four near misses, all 66 registry names): exactly three accepted, as a sorted set difference, and `safe_fix_kind_for` agrees with `is_allowlisted_safe_fix` on every entry. **Asserts no number about the registry** | PASS |
| **X8** | Three rejection arms, each on its **message**, each leaving `serialize_project()`, `undo_count()` and `redo_count()` untouched | PASS |
| **X9** | A fresh open of a ten-problem project: not dirty, no history, **identity list equal to what was written**; three collections agree | PASS |
| **S1** | Timeline activation: bone selected, animation named, Timeline focus requested | PASS |
| **S2** | Weight activation: `AttachmentSelection` by name, `WeightPaint` mode, FFD selection narrowed to one vertex, Properties focus | PASS |
| **S3** | Preview activation: Project focus, selection **unchanged** | PASS |
| **S4** | A removed target leaves `BoneSelection{spine}` standing and says what is gone | PASS |
| **S5** | A fix through the shell path: one history entry, the shell catches up, the list shrinks, and the vanished row's identity is **cleared** | PASS |
| **S6** | Ten idle refreshes run **zero** collections and move none of **seven** session values | PASS |
| **F1** | The Problems window is **on screen**: the severity filter and the Fix button located by a real mouse sweep, clicked, one undo entry, that row gone and the second non-canonical vertex still reported | PASS |

### Inversions run

**Twenty-three recorded mutations.** Every one was applied, built with **all**
relevant object files deleted, run, restored, and rebuilt with the objects
deleted again. Attribution is by **run order**, not authoring order.

| # | Mutation | First detector | Measured failure |
| --- | --- | --- | --- |
| **I1** | Recompute the view's counts from the visible rows | **V3** | `under ErrorsOnly the view reported error_count 1 warning_count 0, expected 1 and 2 (the collector's)` |
| **I2** | Groups in first-encountered order | **V1** | `group 0 has severity 'warning', expected 'error'` — and this is the mutation the design's own V1 fixture could not have caught (see D2) |
| **I3** | Emit an empty group instead of omitting it | **V0** *(plan said V2)* | `an issue-free report produced 2 group(s) … expected 0/0/0/0`. **V2 is an independent detector**, demonstrated by neutering V0's group assertion with the call preserved: `WarningsOnly over an errors-only report produced 1 group(s)` |
| **I4** | Swap the `ErrorsOnly` and `WarningsOnly` arms | **V2** | `WarningsOnly over an errors-only report produced 1 group(s) and 7 visible row(s), expected 0 and 0` |
| **I5** | Resolve a row by position rather than identity | **V5** | `the remembered row resolved to index 0, expected 1` |
| **I6** | Skip `selection_item_exists` and always carry the selection | **V6** | `'overlay.orphan_weight_target\|mesh_base\|body\|ghost_mesh' reported target_missing 0, expected 1` |
| **I7** | Build `MeshWeightTarget` in `AttachmentSelection`'s field order | **X4** | `applying the fix … reported ok=1 changed=0`. **The measured consequence is not the predicted one** — see D3 |
| **I8** | Skip the freshness re-collection | **X4's control arm** *(plan said X8(c))* | `re-applying an already-applied fix reported ok=1 error=''` |
| **I10** | A **whole-animation** erase, as reusing `erase_all_timeline_edits` would give | **X1(a)** | `got 1 identities, expected 10` |
| **I10-bone** | A **bone-blind** inherit erase | **X1(a)** | `Missing: overlay.orphan_animation\|inherit\|ghost\|spine`. **Did not bite before the bone sibling was added to the fixture** |
| **I11-channel** | A **channel-blind** transform erase — the defect the amendment exists to prevent | **X1(b)** | `Missing: overlay.orphan_animation\|transform\|ghost\|arm_l\|translate` … `Both issues carry BoneSelection{arm_l} and animation 'ghost'; only the channel distinguishes them` |
| **I11-attachment** | An **attachment-blind** deform erase | **X1(c)** | `Missing: overlay.orphan_animation\|deform\|ghost\|body\|mage_body` |
| **I11-draworder-event** | Swap the DrawOrder and Event arms' bodies | **X2** *(plan said X1)* | `the session does not carry 'overlay.orphan_animation\|event\|ghost'` |
| **I11-slotcolor-slotattachment** | Swap the SlotColor and SlotAttachment arms' bodies | **X2** *(plan said X1)* | `the session does not carry 'overlay.orphan_animation\|slot_color\|ghost\|body'` |
| **I11-meshweight** | Transpose skin and slot in the MeshWeight arm's predicate | **X3** | `applying the fix … reported ok=1 changed=0` |
| **I12** | `reset_preview_reference` **clears** `active_animation` | **X5** | `active_animation is '' after the reset, expected 'aim'` |
| **I13** | Rebuild `preview_skins` from `preview_state().skin_names` | **X6** | `preview_skins is [default], expected [default, default]` |
| **I14** | Accept any non-empty id (drop the string table) | **X4** *(plan said X7)* | `got 1 identities, expected 2`. X7 is the intended detector and I15 proves it detects a fourth id |
| **I15** | Add a fourth reachable id (`rebind_weights`) | **X7** | `safe_fix_kind_for('rebind_weights') returned 1 while MAR-186's is_allowlisted_safe_fix returned 0` |
| **I16** | A preflight rejection that opens, mutates and **commits** before returning | **X8(a)** | `a rejected fix changed the session -- bytes, undo_count 0 -> 1` |
| **I17** | `problems_view_needs_refresh` compares only `project_revision` | **V8(c)** | `after adopt_runtime_sources() bumped runtime_revision 1 -> 2 with project_revision unchanged at 2, the view reported no refresh needed. AC3 names BOTH revisions.` |
| **I18** | `problems_view_needs_refresh` returns `true` unconditionally | **V8(a)** | `an unchanged session reported that a refresh was needed`. **A quiescence property, not a correctness one** — an always-refresh implementation is behaviourally correct, which is exactly why it needs its own assertion |
| **I19** | The shell activates a row but never calls `SelectionSet::replace` | **S1** | `activating the transform-overlay row left the selection empty` |
| **I20** | `focus_window_for_panel` returns the Timeline for every panel | **S2** | `the focus request is 'Timeline', expected 'Properties'` |
| **I21** | Omit `apply_shell_mode(WeightPaint)` | **S2** | `shell_mode is not WeightPaint after activating a weight issue` |
| **I22** | Replace the selection even for a `target_missing` plan | **S4** | `activating the orphan weight-target row replaced the selection with an identity no runtime resolves` |
| **I23** | `const_cast` the session and `seek(0.5)` in the refresh path | **S6** | `refreshing the Problems view moved a session revision -- preview_revision 9 -> 19`. **Did not bite on the first attempt** — see D6 |
| **I24** | Delete `draw_problems_window`'s body, keeping the function and both call sites | **F1** | `no Problems window exists after two frames`. **S1-S6 all pass unchanged**, which is the whole reason F1 exists |
| **I25-scope** | Pass an **empty** scope to `normalize_mesh_weights` — the real "one attachment, not one vertex" defect | **X4**, and **F1** at the frame layer | `Missing: weights.non_canonical\|mesh_base\|body\|body_mesh\|2`; and `clicking Fix on vertex 0 also repaired vertex 2` |

**Two mutations did not reproduce and are recorded as such rather than replaced
with convenient substitutes:** I25 as the plan specifies it (see D4), and the
first placement of I23 (D6).

### Methodology

- **Every verification build deleted the object files** — never `touch`. The
  generator is `Unix Makefiles` with **GNU Make 3.81**, whose one-second mtime
  granularity produced MAR-184's nine false readings.
- **The harness ABORTS on a failed build.** The first I2 attempt did not compile,
  and the harness ran the **stale pristine binary**, which passed. That is H1's
  false-pass direction produced by the harness rather than by mtime, and it was
  caught only because the build failure was printed in the same output. The
  harness now refuses to run the suite unless the mutated build succeeded.
- **A restore is only as good as the copy it restores from.** Restoring
  `safe_fix.cpp` from a pristine copy captured at **Task 1** silently reverted the
  finished implementation to its stub, and the next inversion ran against it. The
  harness now **verifies the copy matches the tree before mutating** and refuses
  otherwise, and a `snapshot.sh` refreshes the copies after every implementation
  step. This is H1's hazard wearing a different coat: not a stale *object*, a
  stale *source baseline*.
- **Both guards above are now committed**, at `tools/inversion/`, rather than
  described. A guard living only in one agent's scratch directory is the same
  shape as the `awk` gate that was reported but never landed (D18), and both
  hazards were hit independently by the review as well as by this story. Their
  exit codes are distinct and exercised: **2** for a mutation that does not
  compile, **3** for a stale baseline.
- **Restores were verified by `cmp`** against independently kept copies. No
  `git checkout`, `git restore`, `git stash` or `git reset` was run against a
  tracked file at any point.
- **All development and verification ran in an isolated `git archive HEAD` tree**
  while the shared worktree was under concurrent mutation by MAR-186's review,
  and the work was landed only after that review committed.
- **A numeric sweep that stops at a file-type boundary.** MAR-185's 64→66 sweep
  updated the C++ and the Python and missed **prose**, in two places. One of them
  (`AGENTS.md:207`, in `## Current Validation`) was genuinely stale and is fixed
  here; the other is a per-story historical measurement and is correctly left
  alone (D16). This is the third distinct place this chain has found a sweep that
  was correct **within the file types it examined** — D25's scope lesson, in the
  one scope a `grep -rn "!= 64U" src/` can never reach.
- **Quote your globs.** Task 9's `apply_safe_fix` sweep printed
  `no matches found: --include=*.cpp` because zsh glob-expanded the unquoted
  pattern — the same failure MAR-186 recorded, in the same story that recorded it.
  A "no hits" result is evidence only once the command is known to have run.

### Document errors found

| # | Source | Claim | Measured |
| --- | --- | --- | --- |
| **D1** | Design §0.2.1, plan Task 1.2 | `DiagnosticIssue` gains `code_kind` **plus** a retained `std::string code`, and `std::optional<DiagnosticOverlayFamily> overlay_family` | **The shipped shape is different and better.** MAR-186 shipped `DiagnosticCode code` and `DiagnosticOverlayFamily family` with **no string twin at all** (the wire spelling comes from `diagnostic_code_name`) and a `None` enumerator instead of an optional, plus a ninth `MeshWeight` value neither document anticipated. Plan Task 1.2's instruction to "assert `code_kind` and `code` agree" is **unwritable** — there is no second representation to drift — and was deleted rather than replaced |
| **D2** | Design §6.2 V1 | V1's fixture carries one orphan weight target, one uncanonicalizable weight and **two `overlay.orphan_animation`** | **That fixture cannot detect I2, the mutation V1 exists for.** Identity begins with the code, so `overlay.orphan_animation\|…` sorts before everything else, and it is an **Error** — so "groups in first-encountered order" gives the same answer as "Error group first". The fixture contradicts design §2.2's own worked reasoning, which names the `overlay.orphan_weight_target` / `weights.uncanonicalizable` pair precisely because their report order is inverted relative to severity. Rebuilt from that pair plus a `weights.non_canonical`, so the **lowest identity is a Warning** |
| **D3** | Plan §B I7, design §2.9, Task 0.10 | Calling `normalize_mesh_weights` with skin and slot swapped returns a quotable rejection, which is I7's predicted text | **There is no rejection.** Measured: `CORRECT -> ok=1 error=''` and `SWAPPED -> ok=1 error=''`, indistinguishable by return value. `ensure_weight_edit` creates a record at whatever coordinates it is handed. In isolation the swapped call writes a `MeshWeightAttachmentEdit` at bogus coordinates that saves, loads, and manufactures a fresh `overlay.orphan_weight_target`. **In the shipped composition the damage is different again**: the bogus record's vertices are already canonical, so `affected` is 0, the transaction cancels, and the symptom is that *the repair silently does nothing*. X4 catches it on `changed`, not on the record count |
| **D4** | Plan §B I25, design §2.9/R7 | Wiring the Fix button to `normalize_weights_command` repairs every vertex | **Non-reproducing.** The command needs a resolved weight-paint context that a Problems-row click does not establish, so from the button it is a no-op and the suite stays green. The **property** is real and is covered by the mutation that models it — an empty scope passed to `normalize_mesh_weights` — which bites X4 and F1. Recorded as a non-reproduction, not converted into a convenient bite |
| **D5** | Plan Task 0.9, plan standing rules | Call `validate_project_for_save` directly to prove an empty `preview_skins` is savable | **It cannot be called.** `project.cpp:5825` is inside that file's **anonymous namespace** (`:24-6862`). The gate goes through `save_project`, which reaches it internally, with an empty-**name** control that is refused |
| **D6** | Plan §B I23 | A `const_cast` in the refresh path makes V0's seven-value snapshot non-vacuous | **V0 cannot be its detector, and S6 could not see it either at first.** No MAR-187 *project-layer* function receives a session — `build_problems_view`, `plan_issue_navigation` and `find_issue_by_identity` all take `const&` values — so there is nothing for such a mutation to bite there; V0's zero-mutation half is **structural**. Moved to the shell, where `refresh_problems_if_revised` does hold a session. It then **still** did not bite: S6 snapshotted four values and `seek()` moves `preview_revision`, which none of them read, and the mutation sat after an early return. S6 now snapshots **seven** values and the mutation is placed where it runs. **H4 in this story's own test code** |
| **D7** | Design §2.12, and MAR-187's own Task 9 | The two-file grep is backed up by "F1, which fails outright if the smoke's body is missing the call" | **False on BOTH halves, and the committed grep does not catch either.** Measured by deleting each call in turn and rebuilding: with the **application's** call gone the whole shell smoke passes green, and with the **smoke's shared draw list** call gone (`shell_smoke_frames.cpp:79`) it *also* passes green — F1 included both times, because F1 renders through a scenario-local lambda (`:1987`) and therefore backs **neither** body. Worse, that lambda leaves `draw_problems_window` present in `shell_smoke_frames.cpp`, so the committed two-file `grep -n` **returns hits from both files and PASSES on the broken tree.** An earlier version of this row recorded only the application half. Replaced by an executable check — `cmake/CheckFrameBodies.cmake`, run POST_BUILD on `marrow_editor_shell` — which compares the two draw lists as sets, is bounded so a scenario lambda cannot satisfy it, and **fails the build** (exit 2) naming the window and the direction |
| **D8** | Plan Task 2.2 / 4.2 / 5.2 | Named which case fails first against each stub | V1 (correct), **V6** (plan said V7), **X1(a)** (correct). Attribution is by run order |
| **D9** | Plan §A.1 row A6 | `ensure_project_loaded` at `agent_dispatch.cpp:536`, called at `:1091` | **`:542`, called at `:1097`.** MAR-186's own section already recorded `:542`; the plan row was stale against its own dependency |
| **D10** | Plan §B I25's parenthetical | `weight_command_scope` is at `shell_weight_paint.cpp:1057-1069` | **`:1036`.** `:1057` is `normalize_weights_command`, which §A.3 anchors correctly |
| **D11** | `AGENTS.md:207` | "Agent registry validation (**64** operations…)" | **66.** MAR-185's 64→66 sweep updated the code and `test_client.py` and missed this prose line. Fixed by this story |
| **D12** | MAR-186's shipped header | `marrow_editor`'s PRIVATE include dir is at `CMakeLists.txt:522` | `:523-527` |
| **D18** | **MAR-187's own report** | "Adopted the draw-list-bounded `awk` gate, and then ran the demonstration" | **The `awk` form was run ad-hoc and never committed.** What shipped in the plan's Task 9 is the older plain `grep -n` over two filenames — prose in a document, not a script, target or test. Found by review searching the artefact for a gate the report claimed existed. This is the report/artefact gap in its purest form: the demonstration was real, the conclusion was right, and **none of it was in the tree**. It matters more than an ordinary documentation slip because this gate is the only mechanism standing between a one-sided edit and a window that ships in no application — and because the text that *was* committed is additionally **fooled** by F1's lambda (D7). Closed by making the gate executable and build-enforced |
| **D14** | The team lead's follow-up | MAR-187 "adds an eighth family", so MAR-186's G1 seven-identity list must grow or the new family has no detector | **No family was added.** `DiagnosticOverlayFamily` is **byte-identical** to MAR-186's — it already shipped nine values including `MeshWeight`. MAR-187 adds a `struct` (`OverlayRecordKey`), not an enumerator, so `kSweptOverlayFamilyCount` correctly stays **7** and G1's expected list is correctly unchanged (both verified by `diff` against `687ed4f`). Recorded because acting on it would have put a phantom eighth identity into G1 and broken it |
| **D15** | The team lead's follow-up | The commit shows `safe_fix.hpp` "+92 **carrying `OverlayRecordKey`**", so the type may have been put in a file MAR-187 owns instead of MAR-186's header | **`grep -c OverlayRecordKey include/marrow/editor/safe_fix.hpp` → 0**; `diagnostics.hpp` → 4. `safe_fix.hpp` is +92 because it is a **new file**, carrying `SafeFixKind`, `SafeFixResult` and two declarations. B2 landed exactly where it was directed |
| **D16** | The team lead's follow-up | `AGENTS.md`'s second stale "64 operations" site should be fixed like the first | **Only ONE of the two was stale.** `:207` is in `## Current Validation` — a present-tense claim about validating the repo today — and was genuinely wrong; fixed. The second is inside **MAR-183's own validation section**, where "408 `[ OK ]` cases against 64 operations" is that story's accurate measurement **at its own commit**. Overwriting it would falsify the record, which is the failure MAR-185's D25 explicitly flagged when it noted its checker's *"remaining flags are all correctly historical."* The number is left standing and the historical framing made explicit, with today's 66/437 named beside it. **The 408 is also stale on its own terms** — the smoke now prints **437** |
| **D17** | The team lead's follow-up | A durable-section entry called **F-4** characterises the sort as "what keeps `issues` byte-identical across calls", and should be narrowed to shape-and-stability | **No such entry exists.** `grep -rn "F-4"` over `AGENTS.md`, `docs/superpowers/` and the sources returns **nothing**, and no text anywhere claims the sort keeps the payload byte-identical. The record already states the sort's detectors correctly, at MAR-186's I3: *"Delete the `std::sort` from `finalize`" → **G1***, with G9(c) named as an independent detector. Not applied. This is MAR-185's **D24** recurring — *"a pattern entry whose own examples cannot be verified is the very failure it describes"* — and D24's own first draft was dropped for citing an R-series MAR-185 never had |
| **D13** | Five §A.3 anchors | `project.cpp:8258` / `:7932` / `:7712-7719`; `editor_project_smoke.cpp:16133`/`:16282`; `CMakeLists.txt:916-921` | **`:8292` / `:7966` / `~:7746`** (all +34, from MAR-186's `authored_animation_names` at `:6870`); **`:17797` / `:17945`** (+1663); **`:917-922`**. Every one still named its construct; none was a blocking finding |

### Not independently covered

- **The `nothing changed → cancel()` branch is unreachable.** The freshness
  preflight two steps above it guarantees a fresh collection still reports the
  issue, and a live issue always has something to repair: a live weight issue
  means that vertex is non-canonical, a live orphan issue means the record is
  still there, a live stale-preview issue means the stored value still differs
  from the substituted one. Reaching it would need a caller passing an issue
  whose identity is live but whose `vertex_index` points elsewhere, which nothing
  constructs. The branch is defensive and is **not** what X4's control arm tests;
  that arm asserts the reachable property — a second click is refused by name.
- **V0's zero-mutation half is structural, not measured.** No MAR-187
  project-layer function receives a session (D6). The measured half lives in S6.
- **`plan_issue_navigation`'s `describe_missing_target` has three unreachable
  arms.** `PreviewStaleAnimation`, `PreviewStaleSkin` and `ProjectUnsavedChanges`
  carry no selection, so the guard above the switch returns first. Written
  because the compiler named them.
- **`reset_preview_reference`'s five non-preview arms are unreachable**, for the
  same reason: MAR-186 attaches that fix id to exactly two codes.
- **`DiagnosticPanel::Hierarchy` and `::Inspector` do not exist.** AC2 names five
  destinations; three panels serve four of them, and a hierarchy panel focused in
  its own right is never requested because no MAR-186 code is a hierarchy-scoped
  problem. What the router does deliver for bone- and slot-targeted issues is the
  **selection**, which is what makes the Hierarchy row active.
- **Which of the eight erase arms a wrong-RECORD mutation can actually be caught
  in, measured arm by arm rather than implied.** `OverlayRecordKey` exists to stop
  the wrong record being erased, so "the siblings guard it" is a claim that has to
  be per-arm. Review found the first version of X1 covered three arms while this
  list implied all of them:

  | Arm | Finer key beyond the animation | Blind-erase detector |
  | --- | --- | --- |
  | `Transform` | channel | **X1(b)** |
  | `Deform` | attachment | **X1(c)** |
  | `Inherit` | bone | **X1(a)**, via the `spine` sibling |
  | `SlotColor` | slot | **X1(d)** — added after review measured a slot-blind erase passing the ENTIRE suite green |
  | `SlotAttachment` | slot | **X1(e)** — same measurement |
  | `MeshWeight` | attachment | **X3**, measured: an attachment-blind erase takes the resolvable edit too and X3 fails naming both weight issues |
  | `DrawOrder` | **none** | n/a — `DrawOrderTimelineEdit` is `{animation_name, keyframes}` |
  | `Event` | **none** | n/a — `EventTimelineEdit` is `{animation_name, keyframes}` |

  So **every arm that has a finer key now has a detector**, and the two that do
  not have nothing to lose: for them the animation name *is* the whole key, and a
  "blind" erase is the correct erase. Their only failure mode is pointing at the
  wrong vector, which the DrawOrder/Event swap inversion covers. Review put the
  uncovered count at five; two of those five have no finer key and one was already
  covered by X3 — measured, not argued.

- **Populating `OverlayRecordKey` is not compiler-checked.** The writes live
  inside `collect_orphan_animation_overlays`' seven-call list, and MAR-186's
  review established that the `static_assert` beside it is a tautology. The
  guards are X1's per-key sibling records and G1's identity list — nothing else.
- **`ProblemsSeverityFilter::` and `DiagnosticPanel::` appear outside their
  switch files**, as member defaults (`shell_state.hpp`, the two headers) and as
  test expectations. The *switches* are contained: `SafeFixKind::` only in
  `safe_fix.cpp`, MAR-187's `DiagnosticPanel` switch only in `shell_problems.cpp`,
  `ProblemsSeverityFilter`'s only in `problems_model.cpp` and the suite's own
  helper.
- **F1 proves the severity filter and one Fix button are on screen.** The group
  headers, the row Selectables' text and the counts line are covered UI-free
  only; a regression that deleted the counts line would not be caught.
- **The Fix button was off-window until F1 found it.** A default `Selectable`
  spans the whole content region, so the `SameLine()` button landed past the
  window's right edge — unreachable by a mouse and invisible to a user. Every
  UI-free case passed the whole time. This is the MAR-178 property recurring, and
  it is the concrete answer to "what would F1 have caught that S1-S6 would not".
- **MAR-186's severity ASSIGNMENTS are load-bearing but unmeasured, and closing
  that is deferred to MAR-191.** Verified in the artefact rather than relayed:
  `check_invariants` derives its own error/warning tallies **from the same
  `issue.severity` values** it then compares against `report.error_count` /
  `warning_count` (`editor_project_smoke.cpp`, the two counters and the
  `errors != report.error_count` test), so a **misassignment** — an issue emitted
  as `Warning` where `Error` is correct, or the reverse — moves both sides of
  that comparison together and is **invisible to it**. What would actually catch
  one is G1's per-issue `issue.severity != DiagnosticSeverity::Error` check and
  G10's absolute `error_count != 1U || warning_count != 2U`; neither is derivable
  from the cross-check, so both are load-bearing **by inspection and unmeasured
  by inversion**.

  `impl-mar186` identified this and declined to add the inversion because doing
  so meant editing `diagnostics.cpp` while MAR-187 was amending the same file.
  MAR-187 declines it for the adjacent reason: the mutation is cheap now that
  `diagnostics.cpp` has settled, but a review was live against this story's
  commit and amending under one is what produced the earlier collision. It is
  **deferred to MAR-191** ("Validate and document Editing P1", `dependsOn`
  MAR-190), which is a validation-and-documentation story and the natural home
  for a cross-story evidence gap.

  Recorded here rather than left in a conversation deliberately: a claim that
  lives only in a message between agents is exactly how an unverified assertion
  enters a document nobody measured against the code — the failure the durable
  section's *"a correction relayed from conversation"* entry names. This is that
  entry applied to a debt rather than to a correction.
- **Carried forward, unchanged by this story:** `shell_main.cpp`'s frame body is
  still reachable from no test; `commit_path_choice` still has zero end-to-end
  coverage; the runtime still accepts a negative first inherit key time; the
  empty-edit hazard is still fixed only for the inherit family; macOS case
  duplicates of a **missing** file remain two recent-project entries.

## MAR-186 Collect Structured Project Diagnostics Validation Results

Validated 2026-09-01 against a from-scratch `rm -rf build` tree at MAR-185's
commit `711231c`. MAR-186 gives the editor a **UI-free, read-only** function
that walks an opened project and returns a deterministic, stably-identified list
of problems — each with a typed code, a typed overlay family, a severity, a
message, a typed navigation target, and, only where a repair is actually safe,
an allowlisted fix identifier — and surfaces that list through
`project.diagnostics` without disturbing the four members the operation already
returned.

**The title invites a correction worth making up front: "diagnostics" here is a
UI-free model function and one JSON payload. MAR-186 draws nothing.** It focuses
nothing, selects nothing, and repairs nothing; there is no mutating entry point
at all, which is the strongest available form of AC2's "no automatic
correction". MAR-187 builds the Problems view and the three fixes on top of it.

**Nothing about the file format moved.** `.marrow` gains no key, `.mskl` stays
version 1, `.mbin` stays version 2, the C ABI is untouched, no fixture was
edited, no runtime file was touched, no shell file was touched, and neither the
timeline model, the graph model, `authoring.cpp` nor `session.cpp` changed. The
agent registry is **unchanged at 66** — MAR-186 adds no operation and no MCP
tool, so the count sweep is a **non-effect** gate — and `ctest -N` is **22**
before and after. `CMakeLists.txt` gains exactly one line, naming the new source
inside the existing `marrow_editor` static library; no new target, no new test,
no new include directory.

### AC2's boundary is the runtime parser's, not this story's

The single most consequential measurement in the story, because it deletes most
of what "orphan overlay" sounds like it means. `build_project_runtime` parses the
merged runtime document, and **four** rules there are hard load errors:

| Rule | Site | Consequence |
| --- | --- | --- |
| `"animation references unknown bone '<name>'"` | `skeleton_parse.cpp:5270` | any `transform`/`inherit` overlay naming a missing bone makes `load_project` **fail** |
| `"animation references unknown slot '<name>'"` — the `slots` walk | `:5373` | any `slot_color`/`slot_attachment` overlay naming a missing slot makes the load **fail** |
| `"animation references unknown slot '<name>'"` — the separate `deform` walk | `:5430` | any `deform` overlay naming a missing slot makes the load **fail** |
| `"mesh weight references unknown bone '<name>'"` | `:1616` | any weight influence naming a missing bone makes the load **fail** |

All four are `return validation_error(...)`, re-verified at this commit. **Two of
them emit the same message text from two different sites**, which is exactly how
a grep on the message undercounts the number of *rules* — the governing
documents said three, and a Task 0 gate written for three would have failed on a
correct tree.

So in every session the collector can ever see, an overlay's bone and slot
resolve. AC2's *"limited to normally opened sessions"* is not a scoping
convenience; it is the parser's own boundary, and MAR-186 adopts it. **This is a
positive invariant, not only a restriction:** it is what lets an orphan-animation
issue attach a `BoneSelection` or `SlotSelection` with confidence that the bone
or slot still exists, so MAR-187 can select it without an existence check.

Only two orphan classes survive a normal open: **orphan animations**, which
`ensure_object_member` resurrects into every export, and **orphan weight
targets**, which the merge loop silently skips. Anyone reading "the collector
reports orphan overlays" should read this table for what that actually covers.

### What the compiler does and does not police

The story's incoming brief asserted that adding an enum value produces zero
diagnostics at every switch site, inferring it from a grep of `CMakeLists.txt`
for warning flags. **The flag premise is right and the conclusion is wrong.**
Re-measured at this commit by compiling, not by inferring — Apple clang 21.0.0,
`c++ -std=c++17 -c` with **no flags at all**:

```
wsw.cpp:2:19: warning: enumeration value 'C' not handled in switch [-Wswitch]
```

So the four enums MAR-186 introduces — `DiagnosticSeverity`, `DiagnosticPanel`,
`DiagnosticCode`, `DiagnosticOverlayFamily` — are **compiler-policed at every
exhaustive switch**, and two decisions follow from that rather than from taste:

- the four `*_name` functions are switches with **no `default:` arm**, because a
  `default:` silences exactly the diagnostic that makes them safe. Do not add one;
- **every** switch over the four lives in `src/editor/diagnostics.cpp` (21 `case`
  labels there, and `grep -rn "case Diagnostic"` finds none anywhere else), so
  MAR-187's extension is a one-file sweep the compiler will hand it as a checklist.

**What is NOT policed is a fixed-length list of calls, and MAR-186 has one.**
`collect_orphan_animation_overlays` makes one call per overlay family. Adding an
eighth vector to `ProjectData` and forgetting the eighth call produces no
diagnostic, no failure from any shipped case, and a detector that silently
under-reports — precisely the class MAR-185 shipped `clipboard_track_count` into.
The mitigation is a `static_assert` on `kSweptOverlayFamilyCount` sitting beside
the calls with a comment naming the hazard. **It is a guard, not a proof**: it
cannot detect the omission on its own — nothing can, in C++17 without reflection
— but it puts the number in the editor's path, and G1 asserts all seven
identities so a changed number unmatched by a call fails loudly.

`-Wswitch` **warns but does not stop the build**: MAR-186 does not promote it to
`-Werror`. The recipe, deferred to whoever wants it, is
`set_source_files_properties(src/editor/diagnostics.cpp PROPERTIES COMPILE_OPTIONS
"$<$<OR:$<CXX_COMPILER_ID:GNU>,$<CXX_COMPILER_ID:Clang>,$<CXX_COMPILER_ID:AppleClang>>:-Werror=switch>")`.
The compiler-id guard is not decoration: **GCC enables `-Wswitch` only under
`-Wall`**, so a non-Clang build of this tree is silent about a missed arm.

### What was measured before any code was written

Task 0 ran thirteen gates and **paid for itself three times over** — three of its
findings invalidated case specifications the plan had already written, and two of
those were cases that would have **failed on correct code**.

| Gate | Answer |
| --- | --- |
| Tip | **`68720d9` at Task 0**, not the `380479a` the brief named nor the `5a56663` the documents named. MAR-185's commit had been amended at least twice by then; all three hashes still resolve. It was amended once more during implementation and **MAR-185's final SHA is `711231c`**, which is this story's parent — that amendment touched `AGENTS.md` only, so the source tree measured at Task 0 is the one validated against |
| `ctest -N` | **22**. From-scratch rebuild of ALL targets: **0 warnings** |
| MAR-185 landed | Yes. `rename_all_timeline_edits` (`authoring.cpp:123`) and `erase_all_timeline_edits` (`:136`) both cover all **seven** vectors; `erase_all`'s own comment describes MAR-186's orphan-animation mechanism verbatim |
| Vocabulary is new | `grep -rnw` on the six names → **0**. The `-w` is load-bearing: `SpineImportDiagnosticSeverity` has 25 uses, so an unanchored grep can never return zero |
| Panel-focus vocabulary | **0 hits.** It does not exist repo-wide; MAR-186 introduces it, and deliberately names a **panel**, never a window title |
| `-Wswitch` | **Fires with no flags** (above). Compiled, not inferred |
| Registry | **66** rows / array bound **66** / **eleven** `!= 66U` guards / two Python `== 66`. The eleventh guard is `shell_smoke_timeline.cpp:4389` — that file has **two**, which no document named, and "the eleventh, unlisted" is precisely how a sweep site goes missing |
| Fixture | `player_idle.marrow`: `active_animation "idle"`, `preview_skins ["default"]`, **no** `mesh_edits`, overlays only on `idle`, **no** `animation_edits`. `serialize_project` = **6111 bytes**, sha256 `c7d6c6de6a0badf8171772ebb75884785c1b203c86ff4fd4553344029953616b` |
| Canonicalization is a bit-exact fixed point | **Confirmed.** Four vertices; first pass changes 1, second changes 0, no errors. Control: a 0.7-scaled vertex compares DIFFERENT afterwards, so the `==` predicate is live rather than trivially satisfied |
| A lone `1e-9` influence is reachable from a file | **Confirmed.** Save OK, load OK, the value survives verbatim as `1.0000000000000001e-09`. The canonicalizer's message is exactly `A weighted vertex must keep at least one positive influence.` |
| `apply_animation_edits` is out of reach | `project.cpp:5423`, inside the anonymous namespace spanning `:24-6862`. So `authored_animation_names` has to live in `project.cpp` |
| The four parser rules | Present, all `return validation_error(...)` (table above) |
| `AttachmentSelection` vs `MeshWeightAttachmentEdit` | **Transposed, confirmed.** `{slot, skin, attachment}` versus `{skin, slot, attachment}` |

**Three Task 0 findings changed what got written**, and all three are the same
defect class the story chain keeps rediscovering — *a gate or a case that fails
on correct code*, which is worse than a wrong number because it sends the
implementer hunting a regression that is not there:

1. **`mesh_weight_edit_from_runtime` returns a vertex that is already
   non-canonical.** Vertex 2 of `mesh_base`/`body`/`body_mesh` is
   `[spine=0.25, arm_l=0.75]` — ascending — which canonicalization reorders. The
   planned control sub-case ("the edit untouched → **zero** issues") is therefore
   false, and the perturbation sub-cases would have produced **two** issues, not
   one. Fixed by canonicalizing the whole edit into the baseline first, which is
   what makes the control real and each sub-case's identity list minimal. This is
   the story's own stated risk — *a fixture whose issue would fire for a different
   reason* — landing on the **control**, the fixture whose entire job was to prove
   there are no false positives.
2. **A one-ULP perturbation cannot distinguish an exact `==` from a tolerance.**
   One ULP of `1.0` is `DBL_EPSILON/2 ≈ 1.11e-16`, while the canonicalizer's own
   `kMeshWeightSumTolerance` is `4·DBL_EPSILON ≈ 8.88e-16`. The perturbed sum is
   **already inside** tolerance, so the canonicalizer skips the division — that is
   exactly the documented fixed-point property — and **correct code emits no
   issue at all**. The planned failure text described what correct code does. The
   two tolerances are ~7 orders apart, so the drift has to be sized into the gap;
   measured on the two-influence vertex:

   | scale | max weight delta after canonicalization | exact `==` | `1e-9` tolerance | distinguishes |
   | --- | ---: | --- | --- | --- |
   | 1−1e-15 | 7.77e-16 | ISSUE | no | yes, but on the boundary |
   | 1−1e-14 | 7.44e-15 | ISSUE | no | yes |
   | **1−1e-12** | **7.50e-13** | **ISSUE** | **no** | **yes — three orders of headroom each side** |
   | 1−1e-10 | 7.50e-11 | ISSUE | no | yes |
   | 0.7 | 2.25e-01 | ISSUE | ISSUE | **no** |

   G7 uses `1 − 1e-12`, **in memory only** — the perturbation does not survive a
   file at all (see the serializer fact in *Repo facts that outlive their story*).
3. **A fifth shipped assertion on the `project.diagnostics` payload, recorded
   nowhere.** `agent_dispatch_smoke.cpp:1476` captures the **whole compacted
   `scene_delta`** and `:1655-1658` asserts it byte-identical after a run of
   dry-run operations. That binds AC3 far harder than "the four legacy members
   survive" — it pins the entire payload — and it is simultaneously a **free
   pre-existing detector for payload non-determinism**, which is a gain rather
   than only a hazard. It passes unchanged.

Two further corrections were found during implementation rather than at Task 0,
both by running an inversion and reading what actually happened:

4. **G3 could not detect an index-derived identity while it round-tripped
   through a file.** `.marrow` stores overlays in a JSON object keyed by
   animation name and `Value::Object` is a `std::map`, so a reload re-groups
   `transform_timeline_edits` in key order; the orphan landed at index 0 in
   **both** projects and the two index-derived identities compared equal. Found by
   applying the index mutation and watching G3 pass its own comparison. G3 now
   collects **in memory**, and the mutation then reports
   `'…|rotate|0' and '…|rotate|2'` exactly as intended.
5. **G9(c) could not detect a missing sort.** With
   `["default","ghost_skin","ghost_skin"]` the two issues are emitted
   *consecutively* — `default` resolves and emits nothing — so adjacent-unique
   de-duplication collapses them even with the sort deleted. Interleaving a second
   distinct stale skin (`["default","ghost_skin","phantom_skin","ghost_skin"]`) is
   what makes the duplicates non-adjacent and makes G9(c) an independent detector
   of **both** the de-duplication and the sort.

### Result

Every case runs **inside the standing `player_idle.marrow` invocation** and
builds its own throwaway project there. Pointing `marrow_project_smoke` at a
project over another skeleton takes `main()`'s `markers.present.empty()` **skip**
branch, runs nothing, and still exits 0 — so a new command line would have been a
suite that cannot fail. No new command line was added.

**Two invariants run at the end of every case:** identities are **strictly
increasing** (one assertion covering both the deterministic sort and the
uniqueness de-duplication provides), and every non-empty `safe_fix_id` is in the
three-entry allowlist. A third cross-check — the report's `error_count` and
`warning_count` against its own issue list — was added during implementation and
turned out to make a swapped-accumulator mutation detectable by *every* case that
has issues.

**No case asserts only a count.** Every one compares the **full sorted identity
list** and reports a mismatch as a named set difference, distinguishing three
distinct failures: a missing identity, an unexpected one, and — added after a
duplicate was misdiagnosed as an ordering fault — an identity appearing more than
once.

| Case | What it pins | Result |
| --- | --- | --- |
| **G0** | `player_idle.marrow` is issue-free (0/0), and collection moves none of a session's seven observable values: serialized bytes, `dirty()`, `undo_count()`, `redo_count()` and all three revisions. Prints 6111 bytes / sha256 `c7d6c6de…` every run | PASS |
| **G1** | All seven orphan overlay families, by full sorted identity list, each with its code, `Error` severity, `remove_orphan_overlay` fix and **typed family** | PASS |
| **G2** | The animation-name authority is the `animation_edits` **fold**: `{Create,"ghost"}` → zero issues; `{Create}` then `{Delete}` → the seven return | PASS |
| **G3** | Identity is position-independent — same overlay first and last in its vector, **collected in memory** so the format cannot normalise the position away | PASS |
| **G4** | Two collections of one unchanged project agree member-by-member. **Witness, no story-owned inversion** | PASS |
| **G5** | The seven identities survive `save_project` → `load_project`, and the materialized skeleton **does** contain `ghost` — the resurrection that makes this class an Error. **Integration witness, no story-owned inversion** | PASS |
| **G6** | Every typed target: panels, and each selection replayed through `SelectionSet::replace` with the `AttachmentSelection` fields read **by name**; `draw_order` and `event` carry none | PASS |
| **G7** | Weights. Control (canonical → zero), sum-0.7, reversed order, and the `1 − 1e-12` sub-tolerance drift; the sum-0.7 sub-case also proves a non-canonical project is loadable and still reported after a real round trip | PASS |
| **G8** | A lone `{spine, 1e-9}` is an **Error** carrying **no** safe fix, quotes the canonicalizer's own sentence, and survives save → LOAD | PASS |
| **G9** | (a) stale animation; (b) the same reference with the seven overlays present produces **no** stale-animation issue, because they resurrect it; (c) a non-adjacent repeated stale skin collapses to one while `default` emits nothing; (d) an empty active animation is the setup pose; (e) an orphan weight target **supersedes** its own non-canonical vertices | PASS |
| **G10** | Severity counts on an asymmetric one-Error/two-Warning fixture (1/2), the two revisions stamped from the session, and a dirty session counting 1/3 with `project.unsaved_changes` appended, **re-sorted and re-counted** | PASS |
| **G11** | A session with no project yields `std::nullopt` | PASS |
| **G12a** | The escaping is applied and observable: a `|` inside an animation name escapes to `\|` | PASS |
| **G12b** | The **collision** half, on `overlay.orphan_weight_target` — the one identity in this story whose tokens are all unconstrained. Two distinct targets, and unescaped they collapse to one | PASS |
| **A1** | All four legacy members survive by name and JSON type; `issue_count` and `issues` added; issue-free fixture reports an empty array | PASS |
| **A2** | AC3's numeric half: `warning_count == (project_dirty ? 1 : 0)` and `error_count == 0`, asserted on a **clean** project and again where the session is **dirty** | PASS |
| **A3** | A throwaway project with one Error and two Warnings over the wire: counts, `issue_count == len(issues)`, the three identities in full, and typed bone/attachment selections reaching the wire | PASS |
| **A4** | The absent safe fix is an **absent key**, not an empty string. No C++ case can see this — the field *is* empty in both worlds | PASS |
| **A5** | Three consecutive `project.diagnostics` calls agree exactly, and a following `runtime.validate` still passes | PASS |
| **M1** | Over the real socket: all six members with the right Python types, `issue_count == len(issues)`, the legacy numeric identity, and — separately — that the tool's `inputSchema` still declares **no** properties | PASS |

### Inversions run

**Twenty-six entries, of which I2 is seven separate runs — thirty-two recorded
messages.** Every one was run against the **final from-scratch tree**, bit the
case named, and was restored with the object files deleted before both builds.
Where the plan's attribution turned out to be wrong, the **measured** attribution
is recorded and the plan's is named beside it — attribution is by **run order**,
not authoring order.

| # | Mutation | First detector | Measured failure |
| --- | --- | --- | --- |
| **I1** | Resolve orphan animations against the **materialized document** instead of `authored_animation_names` | **G1** | `returned 0 issues, expected 7. Missing:` all seven identities. The story's central mutation: the phantoms are *in* that document, so the collector reports a clean project. G0 stays silent — `player_idle`'s only overlay animation is in the base catalog either way. Also fires at **A3** |
| **I2a–g** | Drop **one** of the seven family calls from the sweep. Seven runs, one per family | **G1** ×7 | e.g. `returned 6 issues, expected 7. Missing: overlay.orphan_animation\|inherit\|ghost\|arm_l`. Each names **only its own** identity; none was inferred from another. The other six always still fire, which is why a count assertion would pass |
| **I3** | Delete the `std::sort` from `finalize` | **G1** | `the identity SETS agree but the ORDER differs. Actual order: transform, inherit, deform, …` — **G9(c) is an independent detector**: with G1, G2 and G5 removed it reports the surviving non-adjacent duplicate |
| **I4** | Delete the adjacent-unique de-duplication (keep the sort) | **G9(c)** | `returned 3 issues, expected 2 … an identity appears MORE THAN ONCE — de-duplication did not collapse it`. `preview_skins` is the one vector nothing else refuses duplicates in |
| **I5** | Delete `escape_identity_token` | **G12a** | `Missing: …\|ghost\\\|arm_l\|spine\|rotate. Unexpected: …\|ghost\|arm_l\|spine\|rotate`. With G12a's check neutered (the call preserved), **G12b** reports the real hazard: `two distinct orphan weight targets collapsed to 1 issue(s)` |
| **I6** | Append the record's vector index to the identity | **G1** | `Missing: …\|arm_l\|rotate. Unexpected: …\|arm_l\|rotate\|0`. With G1's and G2's comparisons neutered, **G3** reports the property: `the same overlay produced two identities: '…\|rotate\|0' and '…\|rotate\|2'` |
| **I7** | Compare influences with `std::abs(a-b) < 1e-9` instead of `==` | **G7 (sub-tolerance drift)** | `returned 0 issues, expected 1 … weights.non_canonical\|mesh_base\|body\|body_mesh\|1`. The sum-0.7 and reversed sub-cases both still fire, so only the deliberately sized drift distinguishes the predicates |
| **I8** | Give `weights.uncanonicalizable` a `normalize_weights` fix | **G8** | `carries safe_fix_id 'normalize_weights', expected none`. The allowlist invariant **passes** — that id *is* allowlisted — which is exactly why an allowlist check is not a substitute. **A4 is a second detector, in a different binary** |
| **I9** | Emit `safe_fix_id` as `""` rather than omitting the member | **A4** | `carries a 'safe_fix_id' member, expected the key to be absent`. **The project smoke exits 0**: the C++ field *is* empty in both worlds and no C++ case can see this |
| **I10** | Build `AttachmentSelection` in the **edit's** field order (both sites) | **G6** | `has slot_name 'mesh_base' and skin_name 'body', expected slot_name 'body' and skin_name 'mesh_base'`. Counts, codes, identities, severities and panels are all still right |
| **I11** | Remove the supersession — examine an orphan weight edit's vertices anyway | **G9(e)** | `returned 2 issues, expected 1. Unexpected: weights.non_canonical\|mesh_base\|body\|ghost_mesh\|0`. G7 and G8 both use a target that **resolves**, so the supersession never runs there |
| **I12** | Resolve `preview.stale_animation` against `authored_animation_names` instead of the materialized skeleton | **G9(b)** | `returned 8 issues, expected 7. Unexpected: preview.stale_animation\|ghost`. G9(a) passes — with no overlays the two authorities agree, so G9(b) is the only fixture where they disagree |
| **I13** | Count severities **before** appending `project.unsaved_changes` | **G10 (dirty session)** | `reported error_count 1 warning_count 2, expected 1 and 3`. **A2's dirty half is a second detector**, over the wire and in a different binary |
| **I14** | Swap the two accumulators in `finalize` | **G1** *(plan said G10)* | `error_count 0 warning_count 7, expected 7/0`. **Over-determined**: with G1 removed, G2's count-versus-list cross-check fires — `the report counts 0 errors and 7 warnings, but its issue list holds 7 and 0`. G10's asymmetric one-Error/two-Warning fixture is a further detector; a symmetric fixture would see nothing |
| **I15** | The handler stops emitting `review_queue_count` | **A1** | `the project.diagnostics payload is missing 'review_queue_count'. AC3 requires all four legacy members to survive` |
| **I16** | Return a default `DiagnosticReport` instead of `std::nullopt` for a session with no project | **G11** | `returned a report (0 issues), expected std::nullopt`. Unreachable through the agent — `ensure_project_loaded` rejects first — so G11 is its only detector |
| **I17** | The handler serializes only `issues.front()` | **A3** | `issue_count disagrees with the length of the issues array`, plus the identity and selection assertions. **This inversion found a defect in the case**: `issue_count` was first emitted from the serialized array, which made that assertion incapable of failing. It now comes from the report |
| **I18** | `authored_animation_names` skips the `animation_edits` fold | **G2 (create)** | `returned 7 issues, expected 0 … a project-authored animation is not an orphan`. **G1 stays silent** — its fixture has an empty `animation_edits`, so the fold and the raw base document agree there |
| **I19** | The MCP `inputSchema` grows a bogus `verbose` property | **M1's schema half** | `project.diagnostics declares input properties. The operation takes no arguments and MAR-186 adds none.` **Every wire assertion passed** — `MarrowClient.send_command` never consults `inputSchema` — so a sequence test alone would have passed over it |
| **I21** | `const_cast` the session and `seek(0.5)` inside the collector | **G0** | `collect_session_diagnostics advanced preview_revision from 1 to 2`. This is what makes G0's seven-value snapshot non-vacuous: the values *can* move |
| **I22** | Invert the stale-skin test — report skins that **do** resolve | **G0** | `player_idle.marrow must be issue-free …`. **Heavily over-determined**: with G0 removed, G1 fires immediately with `Unexpected: preview.stale_skin\|default`, because every project built by the suite's helper carries `preview_skins = ["default"]` |
| **I23** | Orphan targets get `DiagnosticPanel::Project` instead of `Timeline` | **G6** | `overlay.orphan_animation\|deform\|ghost\|body\|body_mesh has panel 'project', expected 'timeline'`. Only G6 reads `target.panel` |
| **I24** | Skip the weight-canonicality sweep (keep the orphan-target branch) | **G7 (sum 0.7)** | `returned 0 issues, expected 1 … weights.non_canonical\|mesh_base\|body\|body_mesh\|0`. With G7's block removed, **G8** fires — `Missing: weights.uncanonicalizable\|mesh_base\|body\|body_mesh\|0` |
| **I25** | A detector emits `normalise_weights` (British spelling, not allowlisted) | **G7's explicit fix-id assertion** *(plan said the allowlist invariant)* | `expected a weights.non_canonical Warning carrying 'normalize_weights', got … fix 'normalise_weights'`. With that assertion neutered, **the allowlist invariant** fires: `carries safe_fix_id 'normalise_weights', which is not in the three-entry allowlist` |
| **I26** | `collect_orphan_animation_overlays` sets `family = None` for the inherit call | **G1** | `the issue 'overlay.orphan_animation\|inherit\|ghost\|arm_l' carries family 'none', expected 'inherit'`. Identity, code, severity, fix and count are all still right — `family` is carried beside the identity, not derived from it |
| **I20** | `#include "shell_state.hpp"` in `diagnostics.cpp` | **Task 9's grep gate** | `grep -nE "imgui\|ImGui\|shell_\|sokol"` becomes non-empty. **Recorded as a scope check, not as coverage** — AC2's "UI-free" has no runtime symptom |

**Six uniqueness demonstrations were run, not asserted:** I3 → G9(c), I5 → G12b,
I6 → G3, I8 → A4, I13 → A2, I14 → G2, I22 → G1, I24 → G8, I25 → the allowlist
invariant. Two of them (I14, I25) **corrected the plan's attribution** rather than
confirming it.

**Every project-smoke message reproduced byte-identically between the first run
and the final from-scratch re-run**, compared with `cmp` over whole recorded
strings. Three entries (I1, I2a, I8) show *additional* lines in the final run
only because the re-run also executes the agent smoke, which those first runs did
not — a second detector appearing, not a message changing.

### Methodology

- **Every verification build deleted the object files** — `/tmp/mar186-rebuild.sh`
  removes the six relevant `.o` files and then builds **all** targets — and this
  was done after the mutation **and** after the restore, never `touch`. The
  hazard is live in this tree, measured rather than assumed: the generator is
  `Unix Makefiles` and `make --version` is **GNU Make 3.81**, whose one-second
  mtime granularity is what produced MAR-184's nine false "did not bite"
  readings. Both directions matter: a skipped *mutation* build is a false pass,
  and a skipped *restore* build leaves the mutated object in place so the next
  inversion in a different translation unit is attributed to the wrong mutation.
- **Restores were verified by `cmp` against an independently kept pristine copy**,
  never by `git checkout`, `git restore`, `git stash` or `git reset` — no such
  command was run against a tracked file at any point.
- **Every inversion was re-run against the final from-scratch tree** after
  `rm -rf build`, and the messages compared against the first-run recordings.
- **Neutering preserved the call.** `if (false && …)` was not used: its `&&`
  short-circuits away the very call being relied on. Where a detector had to be
  silenced for a uniqueness demonstration, either `(void)f(...)` was used — the
  call runs, only the `return false` is removed — or the case's whole block was
  removed after establishing that it shares no state with the case being
  demonstrated (each builds its own base, project and report). One neutering
  attempt that removed *every* `return false` in a block crashed on an unguarded
  `front()` and was discarded rather than reported as a result.
- **Uniqueness was demonstrated by running, not by reasoning**, and where a
  demonstration showed the plan's attribution was wrong, the measured attribution
  is what is recorded below.
- **The containment sweep was widened to the whole repository** after MAR-185's
  review recorded that a sweep is only as good as its scope. It had first been
  run over the two files already known to name the enums, which is the same
  narrow-scope mistake in a smaller form. Widened result: all **21**
  `case Diagnostic*` labels are in `src/editor/diagnostics.cpp` and nowhere else,
  and the four enum names appear in exactly four files — the header, that source,
  `editor_project_smoke.cpp` and `agent_handlers_inspection.cpp`, the latter two
  by `==`/`!=` comparison only.
- **A sweep that returns nothing has to be shown to have run.** The first attempt
  at the repo-wide sweep above was written with unquoted `--include=*.cpp`, which
  zsh glob-expanded before `grep` ever saw it; the command failed and printed a
  confident `(none)`. It was caught only because the shell's error was visible in
  the same output. A "no hits" result is evidence only once the command is known
  to have executed against the intended set.

### Document errors found

Every story in this arc has found errors in its governing documents. This one
inherited a design and plan that had **already** been corrected once by a
downstream review (their E9–E14 / A4–A8), so those are recorded as *incoming*
rather than rediscovered, and Task 0 plus implementation found **eight more**.

| # | Source | Claim | Measured |
| --- | --- | --- | --- |
| **D1** | The team lead's brief | HEAD is `380479a` | **`68720d9` at Task 0, `711231c` at commit time.** MAR-185's commit was amended repeatedly — `5a56663` (the documents' baseline) → `380479a` (the brief) → `68720d9` (Task 0) → `711231c` (final). Every one still resolves, and the last amendment touched `AGENTS.md` only, so no source measurement moved |
| **D2** | Design §6.2 G7, plan Task 4.1 | "The edit untouched → **zero** issues" is the control that makes the other sub-cases mean something | **False, and it fails on correct code.** `mesh_weight_edit_from_runtime` over `mesh_base`/`body`/`body_mesh` returns a vertex 2 that is already non-canonical (`[spine=0.25, arm_l=0.75]`, ascending). The control yields one issue, and each perturbation sub-case yields **two**. Fixed by canonicalizing the baseline first. Same class as the incoming three-vs-four parser-rule error: a gate that fails on a correct tree |
| **D3** | Plan §B I7, design §5 | A **one-ULP** perturbation distinguishes an exact `==` from a `1e-9` tolerance; the failure text is "produced 0 issues, expected 1" | **The mutation is undemonstrable that way and the text describes CORRECT behaviour.** One ULP of 1.0 is `DBL_EPSILON/2 ≈ 1.11e-16`, inside `kMeshWeightSumTolerance` (`4·DBL_EPSILON ≈ 8.88e-16`), so the canonicalizer skips the division and correct code emits nothing. Replaced with a `1 − 1e-12` scale, measured into the gap between the two tolerances |
| **D4** | Design §1.1's table, plan §A.3 | Four sites assert on the `project.diagnostics` payload | **Five.** `agent_dispatch_smoke.cpp:1476` captures the WHOLE compacted `scene_delta` and `:1655-1658` asserts it byte-identical after a run of dry-runs. It binds AC3 harder than either document states, and it is a free pre-existing detector for payload non-determinism |
| **D5** | Design §6.2 G12, plan Task 3.1 | An identity **collision** can be built in the orphan-animation families, and asserting a count of two catches the resulting deletion | **Not constructible there, so the count assertion was vacuous.** Each family's identity has fixed arity and the family token is a literal, so absorbing a separator changes the arity; of the remaining tokens only the animation name is unconstrained, because a bone or slot that does not resolve is a hard load error. Split into G12a (escaping observable by identity, in that family) and **G12b** on `overlay.orphan_weight_target`, whose three tokens are all free — where the collapse from two issues to one was then measured |
| **D6** | Plan Task 3.1 G3 | Two round-tripped projects with the orphan first and last detect an index-derived identity | **They do not.** `.marrow` keys overlays by animation name and `Value::Object` is a `std::map`, so a reload normalises the vector order and the orphan lands at index 0 in both. Found by applying the index mutation and watching G3 pass its own comparison. G3 now collects in memory |
| **D7** | Plan §B I3 → G9(c) | Without the sort, the duplicate `preview.stale_skin` survives | **Not with the specified fixture.** `["default","ghost_skin","ghost_skin"]` emits its two issues *consecutively* (`default` emits nothing), so adjacent-unique de-duplication collapses them with or without the sort. Fixed by interleaving a second distinct stale skin, which also makes G9(c) an independent detector of the de-duplication |
| **D8** | Plan Task 2.1 / Task 6.3 | G0's session half belongs to Task 2, while `collect_session_diagnostics` is implemented in Task 6 | **The two are inconsistent**: G0 as specified cannot pass until Task 6. Resolved by implementing the wrapper's opened-session guard and delegation in Task 2 — the minimum G0 needs — and leaving the `project.unsaved_changes` append, re-sort and re-count to Task 6, where I13 and I14 still land |

Three smaller measured corrections, recorded because a wrong justification for a
right decision still misleads:

- **`ensure_project_loaded` is at `agent_dispatch.cpp:542`**, not `:536` or
  `:531-534`. Fourteen cited line numbers had drifted in total — the largest being
  the review-queue assertions the plan names as a pre-existing detector, which
  moved from `:3881-3885` to `:4139-4146` — but **every citation still named the
  construct its document said it named**, so none was a blocking finding.
- **Design §3.4 justifies the `nullopt` branch as "following `runtime.validate`'s
  shape"**; `runtime.validate` actually uses `make_error_with_delta`. `make_error`
  is still the right choice for a no-payload error, so the decision stands and
  only the justification was wrong.
- **Plan §B I20 says a shell include "compiles fine" in this target.**
  `#include "shell_state.hpp"` in `diagnostics.cpp` in fact fails to compile. The
  grep gate is still the right mechanism — a shell or sokol header without
  external dependencies would slip through silently — but the stated reason is
  not what happens for that particular header.

### Not independently covered

- **The largest orphan class is unreachable.** An overlay naming a missing bone
  or slot, or a weight influence naming a missing bone, makes the project
  **unopenable**. MAR-186 reports none of them, by AC2. The user's experience of
  that class is the runtime's load error, unchanged.
- **`weights.uncanonicalizable` has no repair.** It is reported and left; a user
  must edit the vertex by hand or delete the overlay. Normalization is precisely
  what cannot fix it, which is why its `safe_fix_id` is deliberately absent.
- **The orphan-weight-target supersession hides real weight problems** on an
  orphaned edit until the orphan is fixed. Deliberate: the weights are dead data
  the runtime never sees, and a second row would vanish under MAR-187's
  `remove_orphan_overlay` without the user having addressed it. G9(e) pins it.
- **Fixing an orphan overlay can CREATE a stale-preview issue.** If
  `active_animation` names a phantom animation that overlays resurrected,
  removing the overlay makes the preview reference stale. Deliberate and
  asserted, from the other side, by G9(b); MAR-187's revision-keyed refresh is
  what surfaces it.
- **AC5's selection half is proved structurally, not by assertion.** The
  collector takes no `SelectionSet`, mutable or otherwise, so there is nothing
  for it to write to. A case that built a `SelectionSet`, ran collection and
  asserted it unchanged would read state a passing run leaves at its default —
  vacuous — and none was written. What *is* asserted is the substantive half:
  each target fed to `SelectionSet::replace` makes the expected typed identity
  active, with the transposed `AttachmentSelection` fields read by name.
- **G4 and G5 are witnesses with no story-owned inversion**, and G0's
  byte-identity half is a non-effect assertion. No mutation of MAR-186's own code
  makes two collections of an unchanged project differ, or an unchanged project
  serialize differently, without failing something louder first. G0's *other*
  halves are inverted (I21, I22) and that is where its falsifiability comes from.
- **`collect_session_diagnostics`' `nullopt` branch is unreachable through the
  agent** — `ensure_project_loaded` (`agent_dispatch.cpp:542`) rejects first — and
  its `base_skeleton_document() == nullptr` sub-condition is not separately
  reachable at all, because `open` sets the document and the runtime data
  together. G11 reaches the branch from C++ and is its only detector; the
  sub-condition is recorded as defensive, mirroring `runtime.validate`'s own
  unexercised null-document guard.
- **`diagnostic_panel_name` and `diagnostic_severity_name` are total** over three
  and two values; every arm is read by a message or a payload string some case
  already asserts.
- **`review_queue_count`'s expression** is copied verbatim from the shipped
  branch and is already covered by the review-queue assertions this story did not
  touch. MAR-186 adds no inversion for a guard it did not write. The review queue
  is deliberately **not** an issue: making it one would move `warning_count`
  whenever an agent queues a save, which the shipped "unchanged across queueing
  six reviews" assertion forbids.
- **`-Wswitch` warns but does not stop the build**, and **GCC needs `-Wall` for
  the same diagnostic**, so a non-Clang build of this tree is silent about a
  missed exhaustive-switch arm. The `-Werror=switch` recipe is recorded above as
  a deferred mitigation, never as coverage.
- **No compiler checks `collect_orphan_animation_overlays`' seven family calls,
  and the `static_assert` beside them provides ZERO compile-time protection.**
  This is stronger than "a guard, not a proof", and it was measured rather than
  reasoned. `constexpr std::size_t kSweptOverlayFamilyCount = 7` is initialised
  from the same literal the assert compares it against, two lines above it, so
  `static_assert(kSweptOverlayFamilyCount == 7, …)` is a **tautology**: it can
  only fire if someone edits the constant. Review deleted the entire
  bone-inherit loop, left the constant at 7, removed every object file and built
  all targets — **the build succeeded with no diagnostic.**

  **G1's seven-identity assertion is the actual guard.** It is what caught all
  seven of the I2a–g call-drop mutations, each by name
  (`Missing: overlay.orphan_animation|inherit|ghost|arm_l`). The `static_assert`
  earns its place only by putting the number in the editor's path; it detects
  nothing on its own. **MAR-187 is adding an eighth family and must not rely on
  it** — the thing to extend is G1's expected identity list.
- **No inversion exercises severity ASSIGNMENT, and G1/G10's absolute count
  assertions are largely subsumed.** A small evidence gap rather than a coverage
  gap, and disclosed rather than papered over with an invented inversion. Every
  case's `check_invariants` recomputes the severity totals from `report.issues`
  with logic identical to `finalize`'s, so it subsumes any pure *counting* bug —
  which is why I14 (swapped accumulators) fires at G1 and is over-determined.
  What that cross-check cannot see is a detector assigning the **wrong severity**
  to an issue, because the list and the counts then stay consistent with each
  other. Such a flip *would* still fail — G1 checks each orphan issue's severity
  individually, and G10's absolute `1 Error / 2 Warnings` is not derivable from
  the cross-check — but **no mutation in that class was run**, so those two
  assertions are load-bearing by inspection rather than by measurement. Adding
  one was not cheap here: it means editing `diagnostics.cpp`, which
  `impl-mar187` is concurrently amending, so it is recorded instead of run.
- **Nothing is persisted.** A diagnostic is recomputed on demand; a project that
  is opened, inspected and closed leaves no trace of what was found — which is
  also what makes AC2's "read-only" total.
- **Carried forward, unchanged by this story:** `shell_main.cpp`'s frame body is
  still reachable from no test; `commit_path_choice` still has zero end-to-end
  coverage; the runtime still accepts a negative first inherit key time and inert
  `curve` data on an inherit key; the empty-edit hazard is still fixed only for
  the inherit family; macOS case duplicates of a **missing** file remain two
  recent-project entries.

## MAR-185 Complete Inherit Timeline Editing Parity Validation Results

Validated 2026-09-01 against a from-scratch `rm -rf build` tree at MAR-184's
commit `8cc5c57`. MAR-185 turns MAR-184's stored inherit overlay into a
**first-class timeline family**: selectable, add/edit at the playhead, removable
by exact time, retimeable, scalable, copy/pasteable, cascaded by animation
rename and delete, drawn as a real widget, and reachable from the agent and MCP.

**Nothing about the file format moved.** `.mskl` stays version 1, `.mbin` stays
version 2, the C ABI is untouched, `.marrow` gains no key, no fixture was
edited, no CTest target was registered (`ctest -N` is **22** before and after),
no translation unit was added, and `git diff --stat` over
`timeline_graph_model.{hpp,cpp}`, `session.cpp`, `include/marrow/c/`, `src/c/`,
`include/marrow/runtime/`, `src/runtime/`, `assets/fixtures/` and
`CMakeLists.txt` is **empty**. One number did move: the agent registry, **64 ->
66**, at exactly thirteen sites.

### The compiler DOES police this. The story's stated premise was wrong.

The single most consequential finding, and the one to carry forward. Both
MAR-185 documents, and the briefs that produced them, are built on this
inference:

> `-Werror` appears once, scoped to `marrow_constraint_warning_check`. There is
> no `-Wall`, no `-Wextra`, and no `-Wswitch` on any product target. So adding
> `TimelineKeyKind::Inherit` produces **zero diagnostics**.

The premise (`grep -rn "Wswitch\|Wall\|Wextra" CMakeLists.txt` -> **empty**) is
true. The conclusion is false: **Clang enables `-Wswitch` by default**, without
`-Wall`. Measured by adding a throwaway seventh enum value and rebuilding
`marrow_editor` with its object files deleted first:

```
src/editor/authoring.cpp:205:13: warning: enumeration value 'ThrowawaySeventhProbe'
    not handled in switch [-Wswitch]
... 25 diagnostics total, over ALL targets: 18 authoring.cpp, 6
agent_handlers_editing.cpp, 1 timeline_controller.cpp. 0 errors. A pristine
rebuild after restoring the header: 0 warnings.
```

The first probe measured **24**, because it was scoped to
`--target marrow_editor` and structurally could not see
`timeline_key_kind_carries_easing` in `marrow_editor_shell`. That site was found
and fixed anyway by building the shell separately, but the number and the recipe
were both wrong; the durable section above now says to delete every object file
and build all targets.

So the sweep is **partly compiler-assisted**, and the honest division is:

- **25 exhaustive `switch` sites are policed.** The compiler enumerated them; the
  real work was deciding what each arm should *do*, which §1.1's fall-through
  table is still the right artifact for.
- **At least sixteen enumeration sites are NOT policed**, and this is where the
  danger actually lives — the compiler is silent for every one. Sixteen is a
  floor, not a total; the full enumeration is in "Repo facts that outlive their
  story" above, and it includes the one this story got **wrong**
  (`clipboard_track_count`, D20). Among them:
  the seven-vector call lists `rename_all_timeline_edits` (`authoring.cpp:123`),
  `erase_all_timeline_edits` (`:132`), `sort_retimed_timelines` (`:986`) and
  `auto_extend_explicit_animation_durations`' folds (`:2153-2166`); and the
  if/else chains `write_scalar_component` (`:425`),
  `parse_timeline_key_selectors` (`agent_handlers_editing.cpp:~795-895`), the
  easing parser (`~:1080-1150`), the curve-mode parser (`~:1370-1440`),
  `timeline_key_selector` (`timeline_controller.cpp:891`) and
  `visit_editable_timeline_keys` / `visit_existing_project_timeline_keys`
  (`:960`, `:1008`).
  **Four of this story's biting inversions -- I10, I11, I14 and I22 -- are
  exactly these unpoliced lists, and I22 is a defect that shipped in the first
  commit and was caught in review.** The next person to add a `TimelineKeyKind` value
  should let the compiler enumerate the switches and hand-sweep this list.

MAR-185 does **not** turn on `-Wall`/`-Wextra`; that remains its own piece of
work. **This fact is recorded durably in "Repo facts that outlive their story"
above**, not only here, because the next story to add an enum value will read
that section and not this one.

The registry-count sweep has the same shape: the ten pre-existing `!= 64U`
guards live in **three** files (`shell_smoke_graph.cpp` 7,
`shell_smoke_constraints.cpp` 2, `shell_smoke_timeline.cpp` 1), and a sweep that
assumes two will silently miss `shell_smoke_constraints.cpp:147,676` (D19).

### What was measured before any code was written

| Gate | Answer |
| --- | --- |
| HEAD | `8cc5c57`; MAR-184 added 1,935 lines across five files (`project.cpp` **398**, `editor_project_smoke.cpp` 1,244, `authoring.cpp` 140, `project.hpp` 85, `authoring.hpp` 68) |
| `ctest --test-dir build -N` | **22**, identical at the end |
| A6 (`-Wswitch`) | **FAILED as stated** -- **25** diagnostics over all targets (18 `authoring.cpp`, 6 `agent_handlers_editing.cpp`, 1 `timeline_controller.cpp`), see above. The first probe measured 24 because it was scoped to one target |
| A7 (MAR-184 symbols) | A probe naming `BoneInheritTimelineEdit`, `InheritKeyframeEdit`, `ProjectData::bone_inherit_timeline_edits`, `find_bone_inherit_timeline_edit`, `ensure_bone_inherit_timeline_edit`, `inherit_mode_from_key`, `inherit_mode_json_key`, `InheritTimelineMergeRequest::replace_existing_times` and `merge_inherit_timeline` compiles and links unchanged. MAR-184's **seventh `!empty()` disjunct** is present, as `has_inherit_timeline_edits` at `project.cpp:5128-5140`, feeding the seven-argument call at `:5141-5148` |
| A8 (the pruning M-gate) | Reproduced MAR-184's result byte-for-byte. `marrow_inspect` still **cannot** answer it (MAR-184's own D9, copied forward unfixed into MAR-185's Task 0.9); a `libmarrow_runtime.a` probe was used: a lone `{0.0, "normal"}` on `controller` -> `animation 'toggle_inherit': 1 inherit timelines` (only `child`; **controller pruned**), while `{0.0,"noScale"}+{0.4,"normal"}` -> `2 inherit timelines` with `bone_index=1 name='controller' keys=2: (0,3) (0.4,0)` |
| A9 (is I13 viable?) | **Yes.** `resolve_timeline_key` sets `ResolvedTimelineKey::original_time` to the *selector's* time verbatim, while `resolved_stored_key_time` reads `keyframes[i].time`; the two may legitimately differ by up to `kKeyTimeEpsilon` (1e-6). P8 makes the difference a realistic float32 narrowing and I13 reproduces at ~2.5e-8. **Recorded durably in "Repo facts that outlive their story" above** -- it is a seam in the shared authoring layer, not a MAR-185 detail |
| A10 (clipboard cascade) | `grep -rn "clipboard" src/editor/shell_project_panels.cpp` -> **0**. `clipboard.animation_name` has **three** sites, not the two the design predicted: the read in `timeline_model.cpp:400`, the write in `timeline_controller.cpp:1940`, and the Paste-enable gate at `shell_timeline.cpp:2429` |
| Registry baseline | 64 rows / `std::array<OperationExpectation, 64>` at `agent_dispatch_smoke.cpp:39` / ten `!= 64U` / two `== 64` at `test_client.py:53,55` |
| Non-effect witness | `serialize_project(load_project("assets/fixtures/player_idle.marrow").project)` = **6111 bytes**, sha256 `c7d6c6de6a0badf8171772ebb75884785c1b203c86ff4fd4553344029953616b` |
| UI witness | `git grep -c "Inherit remains read-only" -- src/` -> **2** before, **0** after |
| Fixtures | `skin_inherit_constraints.mskl`: five bones, all `inherit` **absent**; `toggle_inherit`/`child` keys `0.0 normal`, `0.25 noRotationOrReflection`, `0.5 onlyTranslation`, `1.0 normal`. `player_idle.mskl`: sixteen bones, all absent, **0** inherit timelines |

### Result

| Slice | What it proves | Status |
| --- | --- | --- |
| The seventh vocabulary member | `TimelineKeyKind::Inherit` appended last; **25** compiler-named switch arms plus **at least sixteen** hand-swept enumeration sites | PASS |
| Editable lane and selector | P1: the `bone:2:Inherit` row is editable and an Inherit selector over its own key time resolves exactly one key | PASS |
| All five modes, through the editing surface | P2: five tokens merged in one request survive save -> LOAD -> materialization in order. MAR-184's P3 builds the same five by struct construction and never touches the token vocabulary | PASS |
| The same-time obligation | P4: a merge of two keys 5e-7 apart is refused naming `collide` with `serialize_project` unchanged, and `paste_keys_replace_collisions` collapses a pasted key onto an existing time instead of inserting a duplicate | PASS |
| Removal, and the floor | P3: three removals count down 3/2/1, the fourth is refused naming the remedy, and the exported `.mskl` carries the **one** survivor rather than the imported four | PASS |
| Rejection atomicity | P5: a removal for a time that does not exist, on a bone with **no project edit yet**, leaves `serialize_project()` byte-identical; six rejections each asserted on their message | PASS |
| Retime | P6/P7: a delta with room applies and is proved on the **stored** time after a real `load_project`; a delta that would cross an unselected neighbour clamps to `0.249` | PASS |
| Scale | P8: a projection 0.4 ms apart is refused naming `inherit key 'child'` and `the minimum separation is`; a legal ratio lands on the stored double to 1e-12 | PASS |
| Duration auto-grow | P9: a key at 1.4 s grows an explicit 1.0 s duration to float32(1.4) | PASS |
| Animation cascade | U1/U2: rename carries the overlay and the materialized timeline; delete removes it and does **not** re-create the animation from an orphan | PASS |
| Clipboard cascade | U3 (UI-free algebra) + S6 (the GUI path). Fixes all seven families at once -- `Clipboard::animation_name` is one field | PASS |
| Shell workflows | S1-S7: sampled Add, in-place replace, typed copy/paste, exact-playhead Remove, a bit-identical cancelled retime, the single-lane paste remap onto the SELECTED row, and the rename cascade | PASS |
| The widget, on screen | F1: a real mouse finds the `Mode` combo at (344,796) and the same key's `Time` drag at (344,772); clicking `normal` changes the stored mode and records exactly one history entry | PASS |
| Agent + MCP | A1-A7 and `test_client.py`: 66/66 registry and MCP names, both new tools with their `required` sets | PASS |
| Formats and scope | `.mskl` v1 / `.mbin` v2 / C ABI / graph model / `session.cpp` / fixtures / CMake untouched; `marrow_inspect --compare` reports `matches` | PASS |
| Whole suite | From-scratch build with **0 warnings, 0 errors**; `ctest` 22/22; every unit binary, both smokes, the shell, third-party verification and the constraint warning check green; `~/Library/Application Support/Marrow` **ABSENT** | PASS |

### Inversions run

Twenty-six mutations, each applied to the from-scratch tree with **the object
files deleted** before both the mutation build and the restore build (H1 --
`touch` is not sufficient and was not used), each producing the exact text
below, each restored and rebuilt. Every message was compared with `cmp` against
an independently recorded first-run string (H2), never by eye and never by
slicing a line number out of a reference file.

| # | Mutation | Bit | Exact failure text |
| --- | --- | --- | --- |
| **I1** | `sample_inherit_keyframe` seeds from the bone's setup pose instead of `sample_bone_inherit` | **S1** | `MAR-185 S1: the key added at 0.75 s carries mode "normal", expected the sampled "onlyTranslation".` |
| **I2** | `track_is_editable` drops the Inherit disjunct | **P1** | `MAR-185 P1: the Inherit row 'bone:2:Inherit' is still read-only. Every shell workflow -- Add, Remove, copy, the toolbar -- gates on this and bails with "The selected timeline is read-only".` |
| **I3** | `resolve_timeline_key` drops its Inherit arm | **P1** (re-attributed) | `MAR-185 P1: an Inherit selector over the row's own key time resolved 0 keys, expected 1 (Unsupported timeline key kind.).` -- **the attribution moved, and only the H3 re-run found it.** When I3 first ran, P1 did not exist yet and P6 was the first detector (`MAR-185 P6: retime_keyframes resolved 0 keys, expected 1 (Unsupported timeline key kind.).`). P1 was added two tasks later, runs earlier, and now catches it first. Both detect it; the table names the one that actually fires |
| **I4** | `scale_keyframe_times`' validate switch drops its Inherit arm | **P8** | `MAR-185 P8: the colliding scale was accepted (changed=true); expected a rejection naming the separation.` |
| **I5** | `scale_lane_label` drops its Inherit arm | **P8** | `MAR-185 P8: rejection message is "Scaling would place animation 'toggle_inherit' timeline key at 0.250400 s, 0.000400 s from the selected key at 0.250000 s; the minimum separation is 0.001000 s."; expected it to name "inherit key 'child'" and "the minimum separation is".` -- the rejection still happens (I4's arm is present); only the label degrades |
| **I6** | `include_resolved_retime_bounds` drops its Inherit arm | **P7** | `MAR-185 P7: applied_delta is 0.4, expected the clamped 0.249. Without an Inherit arm in include_resolved_retime_bounds the delta is bounded by nothing at all.` |
| **I7** | `apply_resolved_retime` drops its Inherit arm | **P6** | `MAR-185 P6: after save -> load the key is still at 0.25, expected 0.400000.` -- `void`, no trailing statement: the retime reports the full `applied_delta` while writing nothing, so only the **stored** time after a real reload can see it |
| **I8** | the candidate copy removed from `remove_inherit_timeline_keys` and `ensure` hoisted above the resolve step | **P5** | `MAR-185 P5: the rejected removal changed serialize_project() (954 -> 1555 bytes). Validation must complete before ANY write.` |
| **I9** | the last-key floor (step 8) deleted | **P3** | `MAR-185 P3: expected a rejection naming 'must keep at least one key', got changed=true with error ''.` |
| **I9b** | the same, with P3's floor assertion neutered so the removal still RUNS | **P3** | `MAR-185 P3: the exported .mskl carries 4 inherit keys for 'child', expected the 1 that survived removal. An emptied edit is skipped by both serializers and restores the imported base track.` |
| **I10** | the seventh line dropped from `rename_all_timeline_edits` | **U1** | `MAR-185 U1: after renaming toggle_inherit -> toggle_two, the reloaded project's inherit edit still names 'toggle_inherit'.` |
| **I11** | the seventh line dropped from `erase_all_timeline_edits` | **U2** | `MAR-185 U2: the deleted animation's inherit edit survived the delete.` |
| **I11b** | the same, with U2's first assertion neutered | **U2** | `MAR-185 U2: the reloaded skeleton still has animation 'toggle_inherit', re-created from an orphan inherit edit.` |
| **I12** | `cascade_animation_rename` made a no-op | **U3** | `clipboard animation cascade: the rename must remap the clipboard's animation, measured 'toggle_inherit'` (5 failures across the case) |
| **I13** | `resolved_stored_key_time` drops its Inherit arm | **P8** | `MAR-185 P8: the scaled times are (0.10000000149011612, 1.299999974668026), expected (0.10000000000000001, 1.3) within 1e-12. A scale that reads the selector's float32-narrowed time instead of the stored double misses by ~2.5e-8.` -- **viable, contrary to the plan's doubt** |
| **I14** | the seventh `include_animation_timeline_maximum` fold dropped | **P9** | `MAR-185 P9: the saved project failed to reload: $.animations.toggle_inherit.duration: duration must not be shorter than the last timeline key (1.400000)` then `MAR-185 P9: an inherit key past the explicit duration did not grow it...` |
| **I15** | the `Mode` combo's emission deleted | **F1** | `MAR-185 F1: no widget in the Timeline window matched GetID("Mode##inherit0") across the sweep (the control 'Time' matched at x=344.000000, so the seed is sound).` **The evidence is the contrast, not the failure**: under this mutation `marrow_project_smoke`, `marrow_timeline_model_tests`, `marrow_agent_dispatch_smoke` and S1-S6 all stayed **green**, and only the frame case failed |
| **I16** | `timeline_key_kind_name` drops its Inherit arm | **A2** | `[FAIL] set_inherit_keyframe: echoed kind was not "inherit"` |
| **I17** | `parse_timeline_key_selectors` drops its `"inherit"` branch | **A5** | `[FAIL] timeline.retime_keyframes inherit: expected ok=true`, `the retime resolved no inherit key`, `the retime applied no delta` |
| **I18** | `remove_inherit_keyframe`'s registry row given `dry_run_supported = true` | **A4** | `[FAIL] operations.list registry: dry-run metadata changed for remove_inherit_keyframe` then `[FAIL] remove_inherit_keyframe rejects dry_run: rejected response error.code changed`. **Over-determined, and the plan's claim about it is wrong** -- see the document errors |
| **I19** | the two `OperationExpectation` rows removed and the bound put back to 64 | **A1** | `[FAIL] operation registry integrity: descriptor count must match the operation protocol contract` -- a **shipped** detector, re-used, not a new one |
| **I21** | `finish_timeline_retime_gesture`'s cancel stops restoring the pre-gesture selection | **S5** | `MAR-185 S5: the cancelled retime changed the key selection (1 -> 1 keys).` -- the mutation reverts this story's own AC3 fix; see below. The "1 -> 1" is cosmetic: S5 compares full `TimelineKeyRef` vectors, not counts |
| **I22** | the seventh term removed from `clipboard_track_count` again | **S7** | `MAR-185 S7: clipboard_track_count reports 0 tracks for a one-lane inherit clipboard, expected 1. The remap branch is unreachable at any other value.` **Nothing earlier catches it, demonstrated by running:** under the mutation `marrow_timeline_model_tests`, `marrow_project_smoke` and `marrow_agent_dispatch_smoke` are all green and S1-S6 pass; S7 is the first and only failure. Its **two-track** arm is independently falsifiable -- with all three single-lane assertions neutered, the same mutation yields `MAR-185 S7: a clipboard holding one inherit lane and one transform lane counts as 1 tracks, expected 2. At 1 the single-lane remap gate opens for a TWO-track clipboard and cross-remaps the other family onto the selected row.` |
| **I23** | A6's `undo` call suppressed | **A6** | `[FAIL] undo the inherit retime: undo did not move the retimed key back to 0.25`. Run to prove A6's **new** assertion is not vacuous; the old count assertion stayed **silent** under it, which is the measurement that condemned it (D14's sibling) |
| **I24** | `"inherit"` removed from `timeline_lane_selectors_arg`'s rejection group | **A7** | `[FAIL] timeline.set_loop_sync names inherit as unsupported, not unknown: the rejection still calls inherit an unknown lane kind` |

**I20 is not a test and is not counted as one.** Adding `Inherit` to
`track_is_graphable` makes `git diff --stat -- src/editor/timeline_graph_model.cpp`
non-empty (`1 file changed, 2 insertions(+), 1 deletion(-)`). It is a scope
check over a diff, and calling a diff "coverage" would be dishonest.

**Every inversion was re-run against the final from-scratch tree** (H3), and
**twenty-one of twenty-two reproduced bit-identically** under `cmp`. The
twenty-second is I3, whose *text* is identical but whose *attribution* moved
from P6 to P1 because P1 was written after I3 first ran -- recorded above rather
than quietly left pointing at P6. Nothing was thinned: the re-run is what caught
it.

**Uniqueness was demonstrated by running, never by reading.**

- **I3 -> P1, not P3/P5.** `remove_inherit_timeline_keys` never calls
  `resolve_timeline_key`, so P3 and P5 cannot see it; both passed under I3 in
  both runs. P1 is the first case that builds an Inherit selector at all.
- **I6 -> P7, not P6.** Under I6 P6 passed (its +0.15 delta has room) and P7 was
  the first failure.
- **I8 -> P5, not P3.** Under I8 P3 passed, because P3 runs on a bone whose
  project edit **already exists**, where `ensure` is a silent no-op. P5 is the
  only case whose animation and bone both resolve while no edit exists.
- **I11 -> U2, not U1.** Under I11 U1 passed; a rename is unaffected by a
  missing erase.
- **I9b and I11b** are the H4 discipline applied to two assertions that a
  passing run leaves at their default: each neuters the earlier assertion in the
  **same** case and shows the later one fires on its own. I9b's first attempt
  used `if (false && helper(...))`, which **short-circuits the helper away** so
  the removal never ran and the case passed for the wrong reason; it was
  rewritten as `(void)helper(...)` so the call still executes and only its
  verdict is discarded. That near-miss is recorded because it is the same shape
  as H4.
- **I15's uniqueness is the contrast**, recorded above.
- **I22 -> S7, and nothing else anywhere.** Under the mutation all three other
  binaries are green and S1-S6 pass; S7 is the first and only failure. That is
  the whole reason S7 exists: the defect it covers shipped in `5a56663` because
  no case reached the *consumer* of `clipboard_track_count`.

### Document errors found

Twenty-six, against the plan's expectation of "between three and seven" --
and **D20, D21 and D22 are this story's own defects, not a document's**, all
three in the compiler-blind class its own headline finding named. Review found
them; the story did not.
D17-D19 were added after the first pass, on the team lead's prompt to re-sum
every other total once D4 proved the `SlotAttachment` figure was a
**mis-transcription** rather than drift — a mis-transcription implies its
neighbours deserve re-adding, which drift would not. That re-sum found D18.

| # | Source | Claim | Measured at `8cc5c57` |
| --- | --- | --- | --- |
| **D1** | design §1.1, the plan's headline, and the implementation brief | no `-Wall`/`-Wswitch` in CMake, therefore **zero** diagnostics from a seventh enum value | **The premise is right and the conclusion is wrong.** Clang enables `-Wswitch` by default; the probe produced **25** diagnostics (18/6/1). The story's "defining hazard" does not exist in the form stated. What is genuinely unpoliced is the ten enumeration sites listed above -- and three of this story's biting inversions live there |
| **D2** | design §1.1's table, which calls itself "the complete inventory" | 29 sites, of which 21 in `authoring.cpp` and 3 in `agent_handlers_editing.cpp` | **The inventory claim is false, and remains false even though `-Wswitch` now catches these four.** Four `switch (selector.kind)` sites in `agent_handlers_editing.cpp` are missing: `:997` (the `timeline.retime_keyframes` materialize lambda), `:1224` (interpolation), `:2064` (`timeline.scale_key_times` materialize), `:2163` (the scale response's per-key fields) -- lines as they stand in the **committed** tree, which the arms themselves shifted from the pre-change `:940`/`:1167`/`:1993`/`:2107`. `:997` and `:2064` are **silently wrong** without an arm -- an agent retime or scale of an inherit key would never materialize the project edit, which is precisely A5's path |
| **D3** | both documents | `parse_timeline_key_selectors` is the only `kind` if/else chain in the agent handlers | **Two more**: the easing parser (`~:1080-1150`) and the curve-mode parser (`~:1370-1440`). `"inherit"` would fall to their final `else` and produce `Unknown timeline easing key kind: inherit`, a false statement about the vocabulary once inherit is a real kind. Both now name it in their explicit no-easing rejection group |
| **D4** | design §1.1 and plan §A.3 | `grep -rn "TimelineKeyKind::SlotAttachment" src/ include/ tools/ \| wc -l` -> **28** | **33**, and the documents' own per-file breakdown (19+7+5+2) already summed to 33. A transcription error, not drift -- MAR-184 added no site |
| **D5** | plan §A3 / design §1.9 | the marker-gated `else` is `editor_project_smoke.cpp:14062-14206`, registration after `:14204-14206` | Drift of **+1241**. `main()` `:14028` -> **`:15269`**; the skip `:14062` -> **`:15304`**; the partial-match abort -> **`:15313`**; the registration point is now **`:15448-15450`, after `validate_mar184_inherit_overlays`**. `mar178_open_skin_session` `:11763` -> **`:11764`**. The substantive rule -- one invocation, throwaway projects inside it -- holds and was obeyed |
| **D6** | design §1.1 S14-S21 | `read_key_interpolation:2564`, `write_key_interpolation:2589`, `read_key_curve_mode:2624`, `write_key_curve_mode:2646`, `read_key_curve_driver:2670`, `write_key_curve_driver:2692`, `timeline_key_is_managed_loop_boundary:3703` | All **+140**, because MAR-184 inserted `merge_inherit_timeline` at `:2448`: **`:2704`, `:2729`, `:2764`, `:2786`, `:2810`, `:2832`, `:3843`**. Everything **before** `:2448` (S1-S13, S22-S24, both cascade lists, `kKeyTimeEpsilon`, `kNonEventKeySpacing`) is unmoved to the line |
| **D7** | design §0.2/§0.3, plan standing rules | `build_timeline_edits_value` `project.cpp:4381`; its `!empty()` gate `:4826-4852`; `build_project_runtime` defined `:7860`, called `:7532-7533`; `ensure_bone_inherit_timeline_edit` `:7160` | **`:4648`** (seven vectors, confirmed); the gate is **`:5128-5140`** with the seventh disjunct as `has_inherit_timeline_edits`, feeding the call at **`:5141-5148`**; `build_project_runtime` defined **`:8258`**, called from `load_project` at **`:7932`**; `ensure_bone_inherit_timeline_edit` **`:7545`**. Separately, the brief's "~2,000 added lines in `project.cpp` alone" is wrong: **398** in `project.cpp`, 1,935 across five files |
| **D8** | design §2.6 / plan §1.2 | `scale_keyframe_times`' validate switch at `:2372-2412` | The switch is **`:2380-2412`**; `:2372` is the `affected` loop above it. The "provably a no-op" sort comment is **`:2527-2528`** in the committed tree -- and this row's own first version said "`:2434-2435`, exactly as cited", which was wrong at `8cc5c57` too, where it sat at **`:2440-2441`**. A citation can be wrong at the commit it claims to have been measured at |
| **D9** | design §1.3 / plan gate A10 | `clipboard.animation_name` has exactly two sites | **Three.** The read at `timeline_model.cpp:400`, the write at `timeline_controller.cpp:1940`, and the Paste-enable gate at `shell_timeline.cpp:2429`. The design's prose mentions the button; its grep expectation did not |
| **D10** | plan Task 0.9 | answer the pruning gate with `./build/marrow_inspect …\| grep -i inherit` | `marrow_inspect` prints **nothing** about inherit timelines. This is MAR-184's own recorded document error **D9**, copied forward into MAR-185 unfixed -- and an absent grep hit would have read as confirmation. A `libmarrow_runtime.a` probe was used |
| **D11** | plan §2.1 P3 | remove `1.0`, then `0.5`, then `0.25`, then attempt `0.0`; assert the export carries one key at `0.0` mode `Normal` | **That assertion can never pass.** The survivor would be a lone `{0.0, Normal}` key on a setup-`Normal` bone, which `prune_constant_timelines` deletes at materialization -- the plan's own R8. The order used here removes `1.0`, `0.0`, `0.25`, leaving `{0.5, OnlyTranslation}`, which survives and keeps the export assertion meaningful |
| **D12** | plan Task 9 | `grep -rn "!= 66U" src/ \| wc -l` must be **10** | **11.** The ten pre-existing guards move, and MAR-185's own new shell scenario adds an eleventh in the same shape. Thirteen sites carry the 64 -> 66 substitution as planned |
| **D13** | design §6.4 S2 / plan §6.1 | an Add exactly on an existing key "replaced the mode in place", observably | For a **stepped** lane the sampler returns the value of the very key the playhead is on, so the write is identical, the session records no undo entry and the call reports `false`. S2 asserts what is actually true: the lane does not grow, no history entry is recorded, and the vector stays strictly increasing |
| **D14** | plan §B I18 | "the second half -- the key still exists -- is what makes it bite" | **It cannot bite.** The dry-run branch runs the primitive against a *candidate*, so the key survives under the mutation too and that assertion is never falsified. What bites is the error code, and even that is **over-determined** by a pre-existing `operations.list` registry-metadata guard that fires first |
| **D15** | design §2.13 / plan Task 7.1 | emit the combo "at plain window scope" and seed the sweep with `FindWindowByName(kTimelineWindowTitle)->GetID("Mode")` | The dopesheet is inside `BeginTabBar("timeline_views")` + `BeginTabItem("Dopesheet")`, and `BeginTabItem` pushes the tab's id, so the window-root seed never matches. The seed must be chained: `ImHashStr("Mode##inherit0", 0, ImHashStr("Dopesheet", 0, window->GetID("timeline_views")))`. The design **did** warn that a `BeginTabItem` breaks the seed; it did not notice that the timeline window already has one |
| **D17** | plan Task 11.4 | `git add -A` | **Wrong for a tree with concurrent planners in it.** At commit time the working tree also held `plan-mar186`'s two untracked documents, which `-A` would have swept into this story's commit. Only MAR-185's own paths were staged. `impl-mar184` made the same call for the same reason; the instruction should say "stage this story's paths" |
| **D18** | design §1.1 and §5.1 | "**Eleven** of twenty-nine are unobservable", and §5.1's "these are the **eleven** §1.1 sites" | **Two different mis-counts of the same set, found by re-summing after D4.** §1.1's table really does have 11 rows marked "No", but two of those rows name **two functions each** (`S16-S18` covers `read_key_curve_mode` *and* `read_key_curve_driver`; `S19-S20` covers both `write_*` twins), so 11 rows = **13 functions** — the prose counts rows while the `S` labels count functions, and the two numberings disagree. §5.1 then calls the same set "the eleven" while **listing fourteen names**. The plan's §B.1 is the one that adds up: eleven fall-through-correct arms **plus three unreachable** = **14**, which is what shipped and what this section records |
| **D20** | **this story's own implementation** (found in review of `5a56663`) | `clipboard_track_count` needed no seventh term | **Wrong, and it shipped. It fails in BOTH directions.** `timeline_model.cpp:392` was a six-term sum with no `bone_inherit_timeline_edits`, and it is the sole gate for `selected_remap_track` (`timeline_controller.cpp:2213`). **Under-count:** one inherit lane counts as **zero**, the `== 1U` gate never opens, and MAR-185's own remap branch is **dead code** -- a paste onto a different bone's selected row silently lands back on the source bone. **Mis-arm, the nastier half:** one inherit lane *plus* one other-family lane counts as **one**, so the gate opens for a **two-track** clipboard, which is precisely what it exists to prevent, and the *other* family gets cross-remapped onto the selected row. Fixed, and both arms covered by **S7** with inversion **I22**. Exactly the compiler-blind class this story's own headline finding identified -- which is the argument for testing the **consumer** of such a sum rather than reading the sum |
| **D21** | **this story's own implementation** (found in review of `5a56663`) | `set_animation_duration`'s fold list needed no inherit term | **Wrong, and latent.** `authoring.cpp:2076-2098` folds six vectors and omitted `bone_inherit_timeline_edits`, sixty-five lines from the fold list at `:2164-2180` that *was* swept and that I14 proves bites. It has **four** production call sites (`shell_project_panels.cpp:374`, `agent_handlers_editing.cpp:464`/`:521`, and `authoring.cpp:2193` inside `auto_extend_explicit_animation_durations` itself), and it is dormant because **`auto_extend`'s own seventh fold (`:2179-2180`) has already raised `maximum_time` to at least the inherit maximum before passing it as the requested duration**, so the `applied_duration < normalized_inferred_duration` test at `:2114-2115` cannot resolve differently either way. *This row first gave a different reason -- "both production callers pass a runtime-built skeleton" -- which is refuted by its own fourth call site: that one provably passes a stale skeleton (`session.cpp:1949`). D24 counts it.* Fixed regardless, because the primitive was wrong in isolation. **Correct by inspection, unreachable by test** -- measured, not assumed: removing the fold leaves the whole suite green |
| **D22** | **this story's own implementation** (found in review of `5a56663`) | D3 fixed both sibling `kind` parsers | **There were three, not two.** `timeline_lane_selectors_arg` (`agent_dispatch.cpp:375`) omitted `"inherit"` from its `draw_order`/`event`/`slot_attachment` rejection group, so an inherit lane fell through to `"Unknown timeline loop-sync lane kind: inherit"` -- a false statement about the vocabulary once inherit is a real kind. The lane was correctly refused either way; only the message lied. Fixed, and unlike its two siblings it now has a detector: **A7's loop-sync assertion**, with inversion **I24** |
| **D24** | **this story's own reasoning**, three times -- including inside the entry that names the pattern, and once in this row's own first draft | a conclusion verified by checking its justification | **The recurring failure mode of this story, named because it kept recurring.** Three claims were **right in conclusion and wrong in justification**, each of which would have propagated as verified, and **each is cited here with a location so this row can be checked rather than believed**:
(1) the **`ScrollMax` mechanism** in the frame-smoke notes above -- the correction said `ContentSize` was "finalised by the previous frame's `End()`", and `End()` (`imgui.cpp:8711`) never assigns `ContentSize` at all; `Begin()` does, at `:7995`.
(2) the **stale-binary asymmetry**, cross-story at H1 in the MAR-183 section -- "a stale object can only cost you a missed inversion", where H1 itself records that a skipped *restore* build produces a false **positive** instead.
(3) **D21's dormancy reason** above -- "both production callers pass a fresh skeleton", when there are **four** and one provably passes a stale one.
In all three the conclusion was right and only the mechanism was wrong. **The pattern caught its own author twice and a reviewer's correction to it once** -- which is the strongest thing this row can say about itself. An earlier version of this row cited "R1's uniqueness argument" as a fourth instance; **MAR-185 has no R-series at all** (it labels inversions I1-I24), so that instance did not resolve and has been dropped -- a pattern entry whose own examples cannot be verified is the very failure it describes. It is invisible to testing, because the conclusion is correct and everything stays green; only re-deriving the *argument* catches it, which is what caught all three |
| **D25** | this story's own method, three times | each sweep covered the thing it was aimed at | **Scope is the through-line, and all three are the same mistake wearing different clothes.** The `-Wswitch` probe was scoped to **one target** and reported 24 instead of 25. The anchor checker was scoped to **one section** and so verified 21 anchors while missing the stale `:2434-2435` sort citation two sections away. The parser sweep was scoped to **one file** and so found two of the three `Unknown timeline` chains (D22/D23). In each case the sweep was correct *within its scope* and the scope was the unexamined assumption -- which is why every one of them produced a confident, specific, wrong number. **Ask what a sweep does not reach before trusting what it returns.** The checker has since been widened to the whole file and to `git show HEAD:` blobs rather than the worktree -- and it immediately paid for itself, catching a **fourth** stale anchor the section-scoped version could not see: `shell_timeline.cpp:2312` for the Paste-enable gate, cited twice (gate row A10 and D9), which this story's own widget insertion had pushed to **`:2429`**. Its remaining flags are all correctly historical -- D5's pre-drift `:14028`/`:14204`, and MAR-181/MAR-183 citations measured at their own commits |
| **D23** | design §1.1 S27, plan §1.3 S27 | the agent's key-selector parser is called `parse_timeline_key_selectors` | **No such symbol exists.** `grep -rn "parse_timeline_key_selectors" src/ include/` returns **0**. It is `timeline_key_selectors_arg` (`agent_handlers_editing.cpp:801`), whose `_selectors_arg` suffix leads straight to its sibling `timeline_lane_selectors_arg` -- the two are declared **adjacently** at `agent_dispatch_internal.hpp:140` and `:155`, so the real name would have put D22's third parser directly under the eye. But the wrong name is only a **contributing** cause and this row first oversold it: D3's other two siblings are inline chains, not `*_selectors_arg` symbols at all, so whatever sweep found *them* was never a name sweep. The **primary** cause is scope -- the sweep ran over `agent_handlers_editing.cpp` while the third parser lives in `agent_dispatch.cpp`. `grep -rn "Unknown timeline" src/` finds all three, across both files |
| **D19** | the implementation brief (not the documents) | "the ten `!= 64U` guards in `shell_smoke_graph.cpp` and `shell_smoke_timeline.cpp`" | Ten guards across **three** files, not two: `shell_smoke_graph.cpp` (7), **`shell_smoke_constraints.cpp:147,676` (2)**, `shell_smoke_timeline.cpp:3697` (1). **Both design §1.5 and plan §A.3 list all three files correctly** — the omission was the brief's alone. All three were swept; the two constraint guards were re-confirmed by line after the fact rather than inferred from a green suite, and `grep -rn "!= 64U" src/ tools/` is empty |
| **D16** | plan Task 2.1 P0 and §6.2 P10 | add a byte-identity witness and a `.mskl`/`.mbin` export-equivalence case | Both are **already shipped by MAR-184 in the same binary** -- its P4 asserts the identical 6111-byte / `c7d6c6de…` constants, and its P12 asserts `.mskl` v1 / `.mbin` v2 for an inherit overlay. Duplicating them would add cases that cannot fail for any reason the originals would not. Recorded rather than written |

### One cross-family behaviour change, and why it is here

`finish_timeline_retime_gesture`'s **cancel** path now restores the pre-gesture
key selection (`timeline_controller.cpp`, `TimelineRetimeGesture::original_keys`
/ `original_active_key`). This is **not** inherit-specific and it was not in
either document; S5 found it.

`apply_timeline_retime_delta` rewrites `state->timeline_editor.selected_keys` on
every applied delta so the selection follows the moving keys, and it also
overwrites `gesture.keys` with the moved refs. The cancel rolled the **project**
back and left the selection naming times the project no longer had -- so the
next gesture on that selection failed with *"The selected timeline keys changed
during retime"*. AC3 names "selection" explicitly, so the alternative was to
ship a story whose AC3 is unmet on a clause it lists. The existing coverage
never saw it: `validate_timeline_p0_authoring_smoke` cancels three retimes and
asserts only that `serialize_project` rolls back.

The fix is confined to the Cancel branch, restores refs that
`begin_timeline_retime_gesture` had already validated, and is proved by **I21**.
The whole suite is green with it, including every pre-existing timeline,
graph, curve-preset, loop-sync and scale scenario.

### Not independently covered

Carried forward and added to. Everything here is stated because a passing run is
not evidence for it.

- **Fourteen switch arms are deliberately uninverted** -- eleven that fall
  through to the correct answer plus three that are unreachable. The number is
  the plan's §B.1 arithmetic, not design §5.1's prose, which calls the same set
  "eleven" while listing fourteen names (D18). Inventing a case for any of them
  would be the non-biting theatre H4 warns about:
  `family_owns_scalar_component`, `read_scalar_component`,
  `write_scalar_component`, `family_key_spacing`, `resolved_key_is_loop_pinned`,
  `read_key_interpolation`, `read_key_curve_mode`, `read_key_curve_driver`,
  `timeline_key_is_managed_loop_boundary`, `timeline_key_kind_carries_easing`,
  `timeline_key_curve_value`, and the three **unreachable** write arms
  `write_key_interpolation`, `write_key_curve_mode`, `write_key_curve_driver`
  (their callers check the matching `read_*` first, which returns `nullptr`).
  The arms are written anyway, for the reader and for the next enum value.
- **`sort_retimed_timelines`' seventh call is provably a no-op.** A retime
  applies one shared clamped delta, which preserves index order; a scale
  validates the whole projected sequence first. The source says so itself at
  `authoring.cpp:2527-2528`.
- **The two agent materialize arms added by D2 (`agent_handlers_editing.cpp:997`
  and `:2064`) have asymmetric coverage.** The retime arm at `:997` is exercised
  by A5. The **scale** arm at `:2064` is byte-identical to it and sits on the
  same materialize-then-apply ordering, so the disclosure is about coverage
  rather than doubt -- but no case sends
  `timeline.scale_key_times` with an inherit selector. It is written because the
  compiler named it and because leaving it out is silently wrong, but this story
  has no failing detector for it.
- **`export.preview` carries no inherit data and never will** -- it reports
  target paths only. A6's assertion is a **non-effect** witness (the `targets`
  array is byte-identical across an inherit edit) with no story-owned inversion,
  and `marrow_inspect --compare` over `player_idle`'s export is the real
  equivalence witness. It reported `matches`; note that `player_idle.marrow` on
  disk still carries **no** inherit data, because every agent case that writes
  one runs against an in-memory session.
- **A4's "the key still exists" half is not falsifiable** (D14), and A4's error
  code is over-determined by a pre-existing registry-metadata guard. The code
  comment beside it now says so, rather than repeating the plan's refuted
  rationale.
- **`set_animation_duration`'s seventh fold (D21) is correct by inspection and
  unreachable by test** — and the *reason* it is unreachable is not the one this
  row first gave. It has **four** production call sites, not two:
  `shell_project_panels.cpp:374`, `agent_handlers_editing.cpp:464` and `:521`,
  and `authoring.cpp:2193` **inside `auto_extend_explicit_animation_durations`
  itself**. That fourth one demolishes the original "every caller passes a fresh
  skeleton" argument: it is reached from `session.cpp:1317/:1427/:1954/:2692`
  and provably passes a **stale** one, as `session.cpp:1949` says in as many
  words ("Duration auto-extension reads the CURRENT runtime data").

  The dormancy is real but comes from somewhere else: `auto_extend`'s **own**
  seventh fold (`authoring.cpp:2179-2180`) has already raised `maximum_time` to
  at least the inherit maximum before it calls `set_animation_duration` with
  that value as the requested duration, so `applied_duration <
  normalized_inferred_duration` (`:2114-2115`) cannot resolve differently with
  or without the inner fold. Two other routes to reachability are closed as
  well, recorded so nobody re-treads them: **pruning** cannot expose it, because
  `prune_constant_timelines` only erases lanes matching
  `has_single_key_at_origin`, which contribute a floor of 0.0 anyway; and a
  **ghost-bone overlay** cannot, because `skeleton_parse.cpp` rejects
  "animation references unknown bone" and the rebuild fails before such a
  session can exist. Removing the fold leaves the whole suite green, so
  "unreachable by test" is measured, not assumed, and no case is owed.
- **Inherit undo/redo has no failing detector this story owns.** AC6 names
  undo/redo, and A6 now asserts the retimed key's **time** across undo and redo
  (0.25 -> 0.35 -> 0.25 -> 0.35) rather than the `bone_inherit_timelines`
  **count**, which a retime cannot change and which was therefore vacuous in
  exactly the H4 shape -- in this story, after this story wrote H4 into its own
  method. The count assertion is kept as a labelled witness. The product is
  correct **by construction** and needs nothing: `HistoryEntry` holds a
  `ProjectData` **by value** (`shell_state.hpp`, `session.cpp`) and
  `apply_history` swaps it wholesale, so there is no per-family list for a
  future family to forget. The falsifying mutation therefore lies in the shared
  history machinery, outside this story; I23 (suppressing the `undo` call
  itself) is what demonstrates the new assertion is not vacuous.
- **MCP coverage for the two new tools is count-only.** `test_client.py` moved
  64 -> 66 and asserts the two name sets are equal, but asserts **nothing** about
  either tool's schema or `required` set. Both were read against the C++ registry
  and match field for field -- including the deliberate `dry_run` asymmetry that
  mirrors the `set_/remove_attachment_keyframe` pair -- so AC5 holds, but nothing
  test-enforces it. A schema-shape assertion in `test_client.py` is the close.
- **The byte-identity witness and the `.mskl`/`.mbin` export case are MAR-184's**
  (D16), not this story's, and they have no MAR-185-owned inversion.
- **The build still has no `-Wall`/`-Wextra`.** MAR-185 did not add them. What it
  did establish is that `-Wswitch` is on by default, so the next
  `TimelineKeyKind` value is policed at its **25** switch sites and **not** at the
  ten enumeration sites listed at the top of this section.
- **An inherit lane can still be pruned out of existence** by reducing it to one
  key at t = 0 whose mode equals the bone's setup `inherit`. Identical to rotate,
  translate, scale and shear; deliberately not diverged from. Once pruned the
  lane cannot be re-created from the dopesheet, because a row exists only for a
  materialized timeline. D11 is what happens when a test forgets this.
- **An agent-driven `animation.rename` leaves the GUI clipboard stale.** The GUI
  path cascades (`apply_animation_catalog_action`); the agent path mutates the
  session, and the shell observes it through
  `sync_shell_from_editor_session_if_revised`, which cannot tell a rename from a
  selection change. Closing it needs a rename-aware session signal -- a
  different story. AC4's clipboard clause is inherently GUI-scoped, because
  `TimelineClipboard` lives in `ShellState` and the agent has no clipboard.
- **The clipboard cascade fixes all seven families at once, deliberately.**
  `Clipboard::animation_name` is one field and there is no inherit-only version
  of the bug; before this, copying keys and renaming that animation silently
  greyed Paste out with no message for every family. The copied fragment's own
  `animation_name` is deliberately **not** rewritten: `paste_timeline_clipboard`
  walks the fragment's vectors directly and resolves destinations against
  `state->selected_animation_name`, so rewriting it would be a seventh
  hand-maintained list with no reader.
- **Only the `Mode` combo is proved on screen.** F1 locates it and its sibling
  `Time` drag with a real mouse and clicks through the popup. The lane's
  diamonds, the toolbar buttons and the dopesheet row itself are covered UI-free
  only; a regression that deleted the Inherit lane's *draw* call would not be
  caught.
- **`merge_inherit_timeline`'s `replace_existing_times = false` arm still has no
  product caller.** `set_inherit_keyframe` uses `true`; only MAR-184's own case
  exercises `false`. MAR-185 gave `replace_existing_times` its first product
  caller, in that one polarity.
- **`shell_main.cpp`'s frame body is still reachable from no test**, and
  **`commit_path_choice` still has zero end-to-end coverage**. Both carried
  forward unchanged from MAR-183; MAR-185 adds no line to either.

## MAR-184 Add Stepped Inherit Timeline Overlays Validation Results

Validated 2026-09-01. **The title is the first thing to correct.** "Overlay"
here is `ProjectData::*_timeline_edits` -- additive `.marrow` data layered over
an imported `.mskl` at materialization time -- **not** rendering. MAR-184 draws
nothing. It ships a file-format family and a UI-free merge primitive, and every
one of the story's six acceptance criteria names the project, materialization,
export or test layer; not one names the timeline panel, the graph, the
dopesheet, the playhead, or selection.

What this story did **not** touch, proved by empty `git diff --stat`: no ImGui,
no widget, no frame smoke, no shell file, `timeline_model.{hpp,cpp}`,
`timeline_graph_model.cpp`, `timeline_controller.cpp`, `session.cpp`,
`include/marrow/c/`, `src/c/`, `include/marrow/runtime/`, `src/runtime/`,
`assets/fixtures/`, `CMakeLists.txt`, and the whole agent registry
(`agent_dispatch.cpp`, `agent_handlers_*.cpp`, `agent_dispatch_smoke.cpp`,
`tools/`). `.mskl` stays version **1**, `.mbin` stays version **2**, the C ABI is
untouched, `.marrow` still has no version field, and the registry is unchanged at
**64**. No new translation unit and no new CTest target: `ctest -N` is **22**
before and after. The whole diff is **additive** -- 2006 insertions, **zero**
deletions, across six files.

The runtime layer was already complete and genuinely stepped, and was
deliberately left alone. Two project-side validation rules are **stronger** than
the runtime's own parser (a finite non-negative time, and the refusal of `curve`
data on a stepped key), and both live on the overlay rather than in
`skeleton_parse.cpp` **on purpose**: adding either to the runtime would make
previously valid `.mskl` files stop loading, which AC5's "retaining current
format versions" forbids.

### What was measured before any code was written

| Claim | Measured |
|---|---|
| `ctest -N` total | **22**, unchanged at the end |
| Registry and its witnesses | **64** rows; **1** `std::array<OperationExpectation, 64>` at `agent_dispatch_smoke.cpp:39`; **10** `!= 64U` guards; **2** python `== 64` assertions. All four unmoved |
| `grep -c "nherit" src/editor/project.cpp` | **0**. `include/marrow/editor/project.hpp` had exactly one hit, `:442`, an unrelated linked-mesh doc comment |
| `grep -c "nherit" src/runtime/binary.cpp` | **0** -- inherit rides the generic document codec, so `.mbin` needed no new code |
| `grep -c "TimelineTrackKind::" src/editor/shell_timeline.cpp` | **0** -- the panel iterates rows generically |
| `serialize_project(load_project("assets/fixtures/player_idle.marrow"))` | **6111** bytes, sha256 `c7d6c6de6a0badf8171772ebb75884785c1b203c86ff4fd4553344029953616b`. Asserted and printed by P4 every run |
| Fixture inventory | `skin_inherit_constraints.mskl` carries the tree's ONLY inherit timeline: `toggle_inherit`/`child`, keys `0.0 normal`, `0.25 noRotationOrReflection`, `0.5 onlyTranslation`, `1.0 normal`. All five bones (`root`, `controller`, `child`, `constrained`, `cape_target`) omit `inherit`, i.e. setup `Normal`. `player_idle.mskl` has **zero**. `toggle_inherit`'s `bones` object names **only** `child` |
| `.marrow` files carrying `bones.*.inherit` | **zero**, all three checked |

#### The M-gate: the pruning trap is REAL, and it shaped every fixture here

The single most consequential measurement. `prune_constant_timelines`
(`skeleton_animation.cpp:275-279`) deletes an inherit timeline of exactly one key
at `t=0` whose mode equals the bone's setup inherit, during skeleton
construction -- i.e. inside `build_project_runtime`. A test authoring
`[{time: 0, inherit: "normal"}]` on a bone of this fixture and then asserting the
timeline exists **fails, and the overlay code is not the reason.**

`marrow_inspect` prints nothing about inherit timelines, so the plan's
`marrow_inspect | grep -i inherit` gate cannot answer this either way (document
error D9). A throwaway probe linking `libmarrow_runtime.a` was used instead:

```
=== lone {0.0, normal} on 'controller' ===
animation 'toggle_inherit': 1 inherit timelines
  bone_index=2 name='child' keys=4: (0,0) (0.25,2) (0.5,1) (1,0)
        -> 'controller' is ABSENT. Pruned.

=== {0.0, noScale} + {0.4, normal} on 'controller' ===
animation 'toggle_inherit': 2 inherit timelines
  bone_index=2 name='child' keys=4: (0,0) (0.25,2) (0.5,1) (1,0)
  bone_index=1 name='controller' keys=2: (0,3) (0.4,0)
        -> survives.
```

Every project-only overlay in this story is therefore `{0.0, noScale}` +
`{0.4, normal}`: two keys, neither a lone origin key matching setup.

#### Does the runtime witness exercise all five modes? **No -- three.**

`validate_runtime_inherit_timeline_and_skin_constraints`
(`runtime_fixture_smoke.cpp:1883`) samples `toggle_inherit` at `0.0`, `0.25`,
`0.5` and `1.0`, and the fixture's four keys carry only `Normal`,
`NoRotationOrReflection` and `OnlyTranslation`. **`NoScale` and
`NoScaleOrReflection` are not exercised there.** It was deliberately **not**
widened -- `git diff --stat -- src/samples/runtime_fixture_smoke.cpp` is empty --
because AC6's "all five modes" is met **more strongly** by this story's own
cases. P3 and P12 do not merely exercise the project's mode table: each saves,
then **loads**, so all five tokens are written by the project serializer and read
back by the runtime's own `parse_bone_inherit`, and P12 does it again through
`.mskl` **and** `.mbin`. The fixture is also asserted on by other suites.
`runtime_fixture_smoke` runs as an unchanged regression witness.

### Results

All thirteen cases run inside the **standing** `player_idle.marrow` invocation.
There is exactly **one** invocation, not two: every editing suite sits inside
`main()`'s marker-gated `else` (`editor_project_smoke.cpp:14056-14203`), so
pointing the binary at a project built over `skin_inherit_constraints.mskl` would
take the `markers.present.empty()` **skip** branch and run none of it. The cases
build their throwaway projects **inside** the standing run, as MAR-177 and
MAR-178 already do. No new command line was added; `AGENTS.md:43` gained a clause.

| Case | Assertion | Result |
|---|---|---|
| P4 | The old-project non-effect witness: `player_idle.marrow` loads with **zero** inherit edits and serializes to **exactly** 6111 bytes / sha256 `c7d6c6de…`. Both printed every run | PASS |
| P1 | Save -> **LOAD** round trip of a two-key overlay on `controller`: one edit, right animation and bone, two keys, times within `1e-9`, modes `NoScale`/`Normal` | PASS |
| P2a-e | Five parser rejections, each asserting the **full** JSON path AND the message text: negative time; non-increasing times; `1e39` (over float32); unknown token `noScales`; empty array | PASS |
| P6 | A `curve` member on a stepped inherit key is refused by path and message | PASS |
| P3 | All **five** modes as five keys survive save -> load -> materialization as the matching `runtime::BoneInherit`, in order | PASS |
| P13 | `ensure_bone_inherit_timeline_edit` materializes `child`'s **4** base keys with exact times and modes; a second call returns the SAME pointer and appends nothing; `controller` (no base track) yields an **empty** edit; an unknown bone and an unknown animation each return `nullptr` and append nothing | PASS |
| P7 | A project-only overlay materializes, and `timeline_model::build_tracks` emits an `Inherit` `TrackRow` for that bone with the overlay's key times. **Model-layer, not frame coverage** -- see "Not independently covered" | PASS |
| P8 | With an inherit overlay on `controller` and an unrelated rotate overlay on `root`, the materialized animation has **two** inherit timelines -- `child`'s untouched 4-key base track and `controller`'s new one -- and `root`'s rotate track survives | PASS |
| P9a | An `ensure`d but empty edit materializes **successfully** and produces **no** timeline for `controller` | PASS |
| P9b | The same project writes a `.marrow` with **no** `inherit` member, and that file reloads | PASS |
| P5 | The same three keys merged in **two request orders** serialize byte-identically; stored times are strictly increasing, asserted **in memory with no save and no reload** so the runtime's own guard cannot stand in for it; counts 3/0/3 | PASS |
| P10a-d | Four merge rejections, each `changed == false` with the offending value named: unknown animation, unknown bone, invalid mode, `quiet_NaN()` time. **P10c and P10d additionally assert `serialize_project` byte-identity**; P10a/P10b deliberately do not, because `ensure` returns `nullptr` there and such an assertion could not fail | PASS |
| P11 | Collision, all three arms on `child` (4-key base track, **no project edit yet** -- load-bearing, and asserted): a default collision is refused with the project byte-identical; `replace_existing_times` overwrites the **mode** while keeping the **stored** `0.25` rather than the requested `0.2500001`; and re-requesting exactly what is stored reports `changed == false` with an **empty** error and writes nothing | PASS |
| P12 | A base-backed overlay **merged through the primitive** exports to `.mskl` and `.mbin`; both reload with 5 keys at equal times (float32 tolerance) and identical modes; `.mskl` `version` is **1** and the `.mbin` varint after the `MBIN` magic is **2** | PASS |

`ctest` **22/22**, `-L runtime` 4/4, `-L editor` 12/12,
`marrow.renderer_link_boundary` 1/1, all against a from-scratch `rm -rf build`.
Nine model-layer binaries, `marrow_fixture_smoke`, and
`marrow_editor_shell --auto-close 2` (C4-C25 regression) all pass.
`marrow_agent_dispatch_smoke` prints **408** `[ OK ]` cases against 64
operations. `marrow_inspect --compare` reports `matches`.
`tools/mcp/test_client.py` **PASSED** and `py_compile` over the four MCP modules
is clean. `marrow_verify_third_party` and `marrow_constraint_warning_check` both
build. `~/Library/Application Support/Marrow` is **ABSENT** after the whole run.

The count sweep over the diff returned exactly **two** lines, both inspected by
hand and neither a registry count: `kRoundConstants[64]` and `schedule[64]`,
the round table and message schedule of the test-local SHA-256 P4 uses. SHA-256
genuinely has 64 rounds.

### Inversions -- actual outcomes, not predictions

**Twenty** mutations, in two passes. Sixteen were run **against the from-scratch
build**, each restored with `touch` plus a forced rebuild, each failure text
captured to a file and compared with `cmp` against the first run's (H2).
**Fifteen reproduced byte-identically**; the sixteenth (I2) differs only because
its first run predates the deliberate strengthening of P2's expected path, and
the re-run text is authoritative. A review pass then added **four** more (R1-R4),
which close every case that had no attributed inversion except P10a and P10b.
The R-rows were run once each with the **object file deleted before every
build**, so no mtime comparison was involved; the texts below are this run's own.

| # | Mutation | Bit | Exact failure text |
|---|---|---|---|
| **I9** | Drop the seventh `!empty()` disjunct at the `build_timeline_edits_value` call site | **P1** | `MAR-184 P1: the reloaded project carries 0 inherit edits, expected 1. A serializer that never emits the family loses every overlay on save, and no other case notices because no other project has one.` |
| **I2** | Weaken the non-negative rejection to `time < -1.0` | **P2a** | `MAR-184 P2a: expected a load error naming '$.timeline_edits.animations.toggle_inherit.bones.controller.inherit[0].time', got a successful load. …` |
| **I2b** | Delete the `finite_animation_scalar` call, keep `>= 0` | **P2c** | `MAR-184 P2c: expected message '$.timeline_edits.…inherit[0].time: inherit keyframe time must be finite and non-negative', got '$.animations.toggle_inherit.bones.controller.inherit[0].time: number is outside the runtime float32 range'.` |
| **I7** | `inherit_mode_from_key` returns `Normal` for an unknown token | **P2d** | `MAR-184 P2d: expected a load error naming '$.timeline_edits.…inherit[0].inherit', got a successful load. …` |
| **I1** | Delete the `curve`-member rejection | **P6** | `MAR-184 P6: expected a load error naming '$.timeline_edits.…inherit[0].curve', got a successful load. …` |
| **I11** | `ensure` skips copying the base track's keys | **P13** | `MAR-184 P13: ensure on 'child' produced 0 keyframes, expected the base track's 4. …` |
| **I3** | Delete the inherit branch from `build_runtime_document` | **P3**, *not* P7 | `MAR-184 P3: the materialized animation carries 0 inherit keys for 'controller', expected 5.` |
| **I3** (P7 uniqueness) | Same mutation, with P3 neutered to `if (false)` | **P7** | `MAR-184 P7: the materialized animation has no inherit timeline for 'controller'. A project overlay that never reaches build_runtime_document is authored, saved, reloaded -- and invisible to the runtime.` |
| **I3b** | Replace the animation's whole `bones` object instead of assigning into it | **P8** | `MAR-184 P8: 'child' lost its base inherit timeline; the animation has 1 inherit timeline(s), expected 2. …` |
| **I6** | Remove the empty-edit skip from `build_runtime_document` | **P9a** | `MAR-184 P9a: materialization failed: …skin_inherit_constraints.mskl:1:1: $.animations.toggle_inherit.bones.controller.inherit: inherit timeline must contain at least one keyframe` |
| **I6b** | Remove the empty-edit skip from `build_timeline_edits_value` | **P9b, only after the case was STRENGTHENED** | `MAR-184 P9b: the written .marrow carries an inherit member for an EMPTY edit. The project parser refuses an empty array, so such a file saves and can never be reopened.` |
| **I5** | Sort the merged keys **descending** | **P5** | `MAR-184 P5: stored times are not strictly increasing: 0.5 0.3 0.1.` -- and P5's **byte-identity half did not fire**, exactly as designed: both request orders still agree under a descending sort. The strictly-increasing half is the biting half |
| **I7c** | `continue` past an unknown mode token in the primitive | **P10c** | `MAR-184 P10c: expected an error naming 'noScales', got changed=false with error ''.` |
| **I4** | Remove the candidate copy; hoist `ensure` above the **collision check**, as the plan words it | **P11 arm 1**, *not* P10c | `MAR-184 P11 arm 1: the rejected merge changed serialize_project(). A collision refused AFTER ensure has materialized the base track leaves the project modified by a call that reported failure.` |
| **I4-top** | The stronger variant: hoist `ensure` above the **per-key validation** too | **P10c** | `MAR-184 P10c: the rejected merge changed serialize_project() (954 -> 1555 bytes). Validation must complete before ANY write, so a rejection leaves the project bytewise as it was.` |
| **I4b** | The replace arm writes the **requested** time onto the existing key | **P11 arm 2** | `MAR-184 P11 arm 2: the replaced key drifted to 0.2500001, expected the stored 0.25. An overwrite that writes the REQUESTED time lets the 1e-6 identity window walk a key one merge at a time.` |
| **I13** | `kBinaryVersionPackedAnimations = 3` | **P12** | `MAR-184 P12: .mbin version is 3, expected 2.` The `.mbin` still round-tripped its data at version 3, so P12's version assertion is the **exclusive** detector |
| **R1** | Delete the strictly-increasing rejection from `parse_inherit_keyframes` | **P2b** | `MAR-184 P2b: expected message '$.timeline_edits.…controller.inherit[1].time: inherit timeline edit keyframe times must be strictly increasing', got '$.animations.toggle_inherit.bones.controller.inherit[1].time: timeline keyframe times must be strictly increasing'.` **A second, previously unrecorded instance of the I2b trap, in new code**: the load still fails, but with the RUNTIME's wording at `$.animations.…` instead of the project's at `$.timeline_edits.…`, so a case asserting only `!result` would have passed |
| **R2** | Delete the empty-array rejection from `parse_inherit_keyframes` | **P2e** | `MAR-184 P2e: expected a load error naming '$.timeline_edits.…controller.inherit', got a successful load. …` The load succeeds outright -- the empty edit is then skipped by both serializers, so nothing reaches the runtime to object |
| **R3** | Drop `finite_animation_scalar` from `merge_inherit_timeline`'s per-key loop, keep `< 0.0` | **P10d** | `MAR-184 P10d: expected an error naming 'finite and non-negative', got changed=true with error ''.` R1's mirror on the C++ side: `NaN < 0.0` is **false**, so a NaN time survives the sign check silently and is inserted as a key |
| **R4** | Delete the `match->inherit == key.inherit` no-op short-circuit | **P11 arm 3** | `MAR-184 P11 arm 3: re-requesting exactly what is stored must report changed=false with an EMPTY error and write nothing; got changed=true error ''.` |
| **I8** | Add `Inherit` to `track_is_editable` | **Task 6's diff** | `git diff --stat -- src/editor/timeline_model.cpp` becomes `1 file changed, 3 insertions(+)`. **Not a test** -- a scope check, and recorded as one rather than counted as coverage |

**One inversion did not bite as written, and the CASE was strengthened, never the
gate.** **I6b**: the serializer's call site gates the whole `timeline_edits`
object on the *effective* edits, so a project whose ONLY edit is the empty
inherit edit writes no `timeline_edits` at all and `build_timeline_edits_value`
never runs. P9b now carries an unrelated **non-empty** transform overlay, which
is stated in the case as load-bearing; the object is then written and the
builder's own skip is the only thing between an empty edit and `"inherit": []`
on disk.

### Document errors found during implementation (9)

Every story in this arc has found errors in its own governing documents (175
three, 176 seven, 177 six, 178 six, 179 six, 180 seven, 181 four plus a later
fifth, 182 six, 183 six). The plan's §A already catalogued **eight** errors in
the design spec; all eight were re-verified and **hold**, and are not repeated
here. The nine below are new, and are errors in the **plan**.

| # | Where | Error | Resolution |
|---|---|---|---|
| D1 | Plan header and §A1 | Baseline commit is `25bf694` | HEAD is **`a2981b8`** -- `25bf694` plus MAR-183's review pass, which touched `AGENTS.md`, `shell_file_paths.cpp` and `shell_smoke_project.cpp`. None is in MAR-184's territory, and **every** `file:line` anchor in the plan still resolved |
| D2 | Plan §1.4 | "Reuse `finite_animation_scalar` (`src/editor/authoring.cpp:322`) rather than open-coding `std::isfinite`" | **Not reachable.** That helper is in `authoring.cpp`'s **anonymous namespace** and is declared in no header, so `project.cpp` cannot call it. The repo already carries **two** file-local copies (`authoring.cpp:322`, `curve_auto.cpp:40`), so a third in `project.cpp`'s anonymous namespace follows the existing convention rather than inventing a shared header. The semantics the rule depends on -- the float32 bound, which is what makes the check reachable from a file at all -- are preserved exactly |
| D3 | Plan §B | Attributes **I3** to P7, "the first case that reads a materialized `SkeletonData`" | **P3 reads one too, and runs earlier** (Task 1 vs Task 3). Measured: I3 fails P3. P7 was proved an **independent** detector by neutering P3 with `if (false)` and re-running -- not by reasoning about it |
| D4 | Plan §B, §4.5, §R5 | Attributes **I4** to P10c and calls P11 arm 1 "a second detector … over-determined" | **Backwards, for the mutation as the plan words it.** Hoisting `ensure` above the *collision check* leaves it below the unknown-mode check, so P10c returns before `ensure` ever runs and **cannot** bite; **P11 arm 1 is the exclusive detector**. Only the stronger variant that hoists above the per-key validation reaches P10c. Both were run and both are in the table |
| D5 | Plan Tasks 1 and 3 | Puts **P3** (an assertion on the materialized skeleton) in Task 1, but the `build_runtime_document` branch it needs in Task 3 | Task 1 cannot be green as written. Followed as TDD instead: P3 was watched failing with `carries 0 inherit keys`, and that failure is what drove the materialization branch in |
| D6 | Plan §B | Predicts **I6b** bites P9b as written | It did not -- see the inversion table. The case was strengthened |
| D7 | Plan §B | Predicts I6b fails with `P9b: reload failed: $.timeline_edits.…must contain at least one keyframe` | P9b catches it one step **earlier**, at the written `.marrow` text, which is the more diagnostic detector |
| D8 | Plan §B | Predicts I7c fails with `got changed=true` | Actual is `got changed=false with error ''` -- the `continue` drops the request's only key, so nothing changes and nothing errors |
| D9 | Plan §0.5 (the M-gate) | Answer the pruning gate with `./build/marrow_inspect /tmp/…mskl \| grep -i inherit` | `marrow_inspect` prints **nothing** about inherit timelines -- its output is counts, animation names, skins -- so that command cannot answer the gate in either direction, and an absent grep hit would have been read as confirmation. A throwaway probe linking `libmarrow_runtime.a` and printing `bone_inherit_timelines` was used instead, and its output is pasted above |

### Methodology: H1 bit this session, in a form `touch` does not fix

`AGENTS.md`'s hazard **H1** says to `touch` a source file after restoring it,
because a `cp` restore landing in the same mtime second makes `make` skip the
rebuild. **`touch` is not sufficient when inversion runs are back to back.**

Sixteen inversions were re-run from a script, each one restoring, `touch`ing and
rebuilding before the next mutated, `touch`ed and rebuilt. **Nine of them
reported "did not bite"** -- including six that had demonstrably bitten minutes
earlier with recorded failure text. The cause is H1's mechanism from the other
side: `touch` sets the source's mtime to *now*, and the object file written by
the immediately preceding restore-build is also from *now*. At GNU Make 3.81's
one-second granularity the object is not older than the source, so the rebuild is
skipped and the run exercises the **restored** binary.

**An earlier revision of this section claimed the failure is one-directional --
that a stale binary can only produce a false pass -- and concluded that every
"it bit" reading was therefore sound. That is wrong, and the error is worth more
than the finding.** It holds only when the stale object is the *pristine* one,
i.e. when the **mutation** build is skipped; then the cost is a missed inversion.
If the **restore** build is the one skipped, the stale object is the **mutated**
one, and the next inversion in a **different translation unit** compiles and
links cleanly against it -- so its failure is attributed to the **wrong
mutation**. That is a false **positive** from a single skipped build, and
MAR-184's sixteen mutations span four translation units (`project.cpp`,
`authoring.cpp`, `timeline_model.cpp`, and the `.mbin` version constant), so it
was reachable here rather than hypothetical.

What makes this story's inversion table trustworthy is therefore **not** the
asymmetry. It is **H2**: every message was captured to a file and compared with
`cmp` against an independently recorded first-run text, and fifteen of sixteen
were byte-identical (the sixteenth, I2, differs only by a deliberate
strengthening of P2's expected path between the two runs). A misattributed
failure would have to reproduce another mutation's exact text to survive that.

*Rule: do not rely on mtime at all. **Delete the object file** before every
verification build (`rm -f build/CMakeFiles/<target>.dir/<path>.o`), which
removes the comparison from the question entirely. `touch` remains correct for a
single interactive restore; it is not sufficient for an automated loop. Verify
by `cmp` against recorded text, not by an argument about which direction the
staleness runs.* After the harness was fixed, all nine bit, and all nine messages
were `cmp`-identical to their first runs.

### Not independently covered

- **No pixel is asserted.** P7 proves an `Inherit` `TrackRow` is *produced* from
  a project-materialized skeleton by `timeline_model::build_tracks`, a pure
  function over `SkeletonData` with no ImGui and no shell. It does **not** prove
  the dopesheet draws it. MAR-184 adds no drawing code -- the row and its
  renderer both predate this story -- but a regression that deleted the lane's
  draw call would not be caught here. MAR-185 ships UI and is where a real-mouse
  scenario belongs. Do not describe P7 as UI coverage.
- **P4 and the equivalence half of P12 have no story-owned inversion.** P4 is a
  non-effect witness -- an unchanged project must serialize to unchanged bytes --
  and there is no mutation of MAR-184's own code that makes it fail without
  failing something louder first. P12's `.mskl`/`.mbin` equivalence rides
  pre-existing generic-codec code this story does not touch; only its version
  assertion has an inversion (I13). Neither row was filled with an invented
  inversion.
- **The "finite" half of AC1 is only partially reachable from a file.** JSON has
  no NaN literal and `json.cpp:302-307` refuses an out-of-range exponent at the
  **tokenizer**, so from a `.marrow` the rule bites only on a finite magnitude
  over float32 max (P2c, `1e39`). NaN and infinity reach it only through
  `merge_inherit_timeline`'s `double` parameter, from C++ (P10d).
- **P10a and P10b have no clean inversion, and the reason is stated rather than
  left bare.** Dropping the **bone** check makes P10b fail with the *same*
  message by a different route -- `ensure_bone_inherit_timeline_edit` returns
  `nullptr` for an unresolvable bone and the primitive's own guard reports
  `bone '<name>' does not exist` -- so the mutation is not observable from the
  case. Dropping the **animation** check leaves `animation` null, and the later
  `animation->find_inherit_timeline(*bone_index)` dereferences it, so the
  mutation **crashes** rather than failing a case. Both are honest "no clean
  inversion exists" rows, not untested behaviour: each rejection is asserted, and
  each asserts `changed == false` with the offending name in the message.
- **The runtime still accepts a negative first inherit key time**, and a `.mskl`
  may still carry inert `curve` data on an inherit key which Marrow ignores.
  Only the project layer refuses either, deliberately (§2.5 of the design). A
  `.mskl` authored by another tool can carry both.
- **The empty-edit hazard is fixed only for inherit.** The sibling families keep
  the pre-existing behaviour: an empty `ensure_slot_attachment_timeline_edit`
  reaching export would still emit `"attachment": []` and produce an unloadable
  `.mskl`. Out of scope, recorded, not fixed.
- **`replace_existing_times` has no product caller** until MAR-185's
  `set_inherit_keyframe`. P11 covers all three arms, so it is not dead in the
  untested sense, but no product surface exercises it.
- **`marrow_inspect --compare` is a regression witness only** for this story: it
  runs over `player_idle`, which carries no inherit data. P12 is the actual AC5
  coverage.
- **`shell_main.cpp`'s frame body is still reachable from no test**, and
  **`commit_path_choice` still has zero end-to-end coverage**. Both carried
  forward from MAR-183 unchanged -- MAR-184 adds **no line** to either.

## MAR-183 Persist and Manage Recent Projects Validation Results

Validated 2026-08-30. MAR-183 closes the project-I/O arc with a bounded,
canonical, de-duplicated recent-project list and the surface that drives it. The
story's premise was checked before any code: **the storage layer already existed
and was finished.** `EditorPreferences::recent_projects` has shipped inside
settings **version 1** since MAR-156, with a parse carrying field-local
fallbacks (`preferences.cpp:269-295`), a serializer (`:315-323`), and thorough
unit coverage. What was missing was any *feature*: zero readers, zero writers,
no menu, no ordering rule. So this story adds **no storage, no parse, no
serialize, and no version bump** -- `kEditorSettingsVersion` stays **1**, proved
by an empty `git diff` over `preferences.cpp`. Bumping it would have made every
settings file this editor ever wrote unreadable, to describe a field version 1
already describes correctly.

No `.marrow` schema change; `.mskl` v1, `.mbin` v2 and C ABI v1 untouched --
`git diff --stat` over `include/marrow/c/`, `src/c/`, `include/marrow/runtime/`,
`project.cpp`, `session.cpp`, `atomic_file_write.cpp` and `preferences.cpp` is
**empty**. The registry is unchanged at **64**, proved by an empty diff over
`agent_dispatch.cpp`, `agent_handlers_*.cpp`, `agent_dispatch_smoke.cpp` and
`tools/`. Neither hand-maintained frame body was edited: `git diff --stat` over
`shell_main.cpp` and `shell_smoke_frames.cpp` is **empty**.

**Two decisions are recorded here because they are the ones a later reader will
be tempted to "fix".**

1. **Nothing prunes a recent entry automatically. Not on load, not on display,
   not on click.** Existence is read per entry, per frame, while the submenu is
   open, so a remounted volume re-enables its entry with no cache to invalidate.
   Pruning on load would imply *writing* on load, which would irreversibly
   delete a user's bookmark over a transient unmount. **Loading therefore never
   writes** -- a settings file the user is mid-way through hand-editing survives
   a launch untouched, and an oversized on-disk list is truncated in memory and
   left oversized on disk until the next real mutation.
2. **Identity is bytewise equality of `weakly_canonical` output, with no case
   folding of our own** -- the rule `resolve_choice` already ships. The macOS
   consequence is **not** the one the design stated, and it was measured rather
   than assumed; see D2 below.

### What was measured before any code was written

| Claim | Measured |
|---|---|
| `ctest -N` total | **22** |
| Registry and its split | **64** rows, **39** edit / **12** inspection / **10** management / **3** validation. **10** `!= 64U` guards, **1** `std::array<OperationExpectation, 64>`, **2** python assertions -- all unmoved afterwards |
| `kEditorSettingsVersion` | **1**, `include/marrow/editor/preferences.hpp:12`. Confirmed in writing before Task 1 that it stays 1 |
| A recent-projects *feature* exists anywhere | **No.** Outside `preferences.cpp` and `preference_store_tests.cpp`, `recent_projects` appears in exactly **three** places, all inert: two comments (`shell_preferences.cpp:96`, `shell_preferences.hpp:28`) and the struct field. Zero readers, zero writers |
| `marrow_preference_tests` links | `marrow_editor` **only**; no `shell_*.cpp` is in `marrow_editor`. Unchanged by this story |
| `marrow_preference_tests` cases | **11** before, **12** after |
| **M4** -- `weakly_canonical` on a **missing** path (macOS) | `weakly_canonical(/…/T/mar183-does-not-exist/x.marrow)` → `'/private/var/folders/…/T/mar183-does-not-exist/x.marrow'`, `ec=0`, `absolute=1`. **The primary branch runs; design §2.3's fallback chain is never exercised on this host.** Printed by the P-case every run |
| **M4** -- `weakly_canonical` on a relative existing path | `assets/fixtures/player_idle.marrow` → `'/Users/…/Maroow/assets/fixtures/player_idle.marrow'`. Load-bearing: `ShellState::project_path` defaults to exactly that relative path |
| **M1** -- a `BeginMenu` inside a popup uses `window->GetID(label)` | **Confirmed at runtime.** `kRecentMenu` is found with `ProbeIdKind::Direct`, not `MenuItem` |
| **M2** -- the nested submenu's ImGui window name, and whether a mouse can open it | **`"Open Recent###Menu_01"`**, and the nested Remove popup is **`"Remove###Menu_02"`**. A click opens both. Printed by C25 every run |
| **M3** -- is a **disabled** `MenuItem` reachable by `HoveredId`? | **YES, it IS reachable.** This selected C25 assertion 4's shape: the missing entry's label is found by the sweep and a click at its own position is asserted to do nothing. Printed every run |
| Empty-list `BeginMenu` behaviour | With an empty list, `Open Recent` **is still hoverable** but opens **no** child popup. Printed by C25 every run |
| **M5** -- `~/Library/Application Support/Marrow` | **ABSENT** before, and **ABSENT** after the entire verification run |

### Results

| Case | Assertion | Result |
|---|---|---|
| P-case | `marrow_preference_tests` "recent project list algebra and isolated round trip": canonicalization, dedup across spellings, MRU ordering, promotion without duplication, eviction at the bound **checked after every promotion**, both polarities of every changed-bool, `normalize` idempotence, missing paths surviving `normalize`, `drop_missing_recent_paths`, the macOS case boundary, and an isolated store round trip | PASS (12 cases) |
| C20 | The algebra as the **shell** drives it: a relative `project_path` records **absolute**; two spellings collapse; 12 records leave exactly `kRecentProjectLimit` with the newest at the head; what the shell wrote reloads element-wise equal and is already normalized | PASS |
| C21 | The recording policy, all seven rows of design §2.4: Open records; a **failed** Open records nothing and preserves order; Save As records the **new** path; a failed Save As records nothing and leaves the settings file byte-identical **with no write inside the rename seam**; a **successful** Save As whose **settings** write fails still REPORTS it (phase 4b, added by the review pass, with a selective rename callback that fails only `editor-settings.json` so the project write can succeed); New records nothing but **arms**, and its first save records and **consumes** the arm; a **startup** project's ordinary Save records nothing and leaves the settings file **absent**; Reload records nothing | PASS |
| C22 | The gate. Clean + targeted → `pending_file_application` holds `{Open, path}`. Clean + **pathless** → the chooser (the regression guard). Dirty → `dirty_intent = {Open, path, Prompting}`, nothing performed, session snapshot bit-identical. Discard performs **that** destination. Retarget A→B then Discard performs **B**. Cancel leaves everything. Retarget to a **pathless** intent **clears** the path | PASS |
| C23 | Missing entries and load semantics: the settings file is **byte-identical** across load; a missing entry **survives**; normalization ran in memory (two spellings collapsed, empty dropped, both absolute); `default_curve` survived and the status is `LoadedWithDefaults`; `recent_project_exists` is true/false correctly; Remove drops exactly one and reloads equal; Clear Missing removes exactly the missing and a second call rewrites **nothing** (bytes **and** mtime); an **absent** settings file stays absent across a load | PASS |
| C24 | Non-interference and write failure: a record leaves `dirty()`, `can_undo()`, `can_redo()` and `serialize_project()` all identical; re-recording the head leaves bytes **and** mtime unchanged; under an RAII-scoped rename failure the write fails, reports, **keeps** the in-memory change and leaves the file byte-identical, and the retry after the scope closes succeeds; `default_curve` and the unknown additive field `"payload"` both survive | PASS |
| C25 | A **real mouse** through the real menu: `File` → `Open Recent` opens a child popup; every enabled seeded entry's label is emitted; the missing entry is present but **not actionable** -- the click over it changes neither `error_message` nor `status_message`, which is the only trace a clickable dead entry would leave (see the review-pass inversion below); a click over a **dirty** session raises `kDirtyIntentModal` and arms an `Open` intent **carrying that path**; `Remove > <missing entry>` removes exactly it and reaches the settings file; `Clear Missing` removes only the missing; an empty list opens no child popup | PASS |

All 13 test binaries pass. `ctest` **22/22**, `-L runtime` 4/4, `-L editor`
12/12, `marrow.renderer_link_boundary` 1/1. `marrow_agent_dispatch_smoke` prints
**408** `[ OK ]` cases against the **64**-operation registry of that commit
(both numbers are MAR-183's own measurement and are deliberately left as
recorded; the registry is **66** today and the smoke prints **437**). `tools/mcp/test_client.py`
**PASSED**, and `py_compile` over the four MCP modules is clean.
`marrow_verify_third_party` and `marrow_constraint_warning_check` both build.

### The four non-bypass greps (design §4.3)

| # | Grep | Required | Measured |
|---|---|---|---|
| P1 | `session.open\|session.create\|session.reload` in `shell_recent_projects.cpp` | 0 lines | **0** |
| P2 | `pending_file_application\|begin_file_action` in `shell_recent_projects.cpp` | 0 lines | **0** — the load-bearing one |
| P3 | `pending_file_application *=` over `src/editor/` | no new file appears | Exactly `shell_file_paths.cpp` (3, one of them the new `arm_open`) and the smoke's deliberate arms. **`shell_recent_projects.cpp` does not appear** |
| P4 | `begin_session_intent` over `src/editor/` | no caller anywhere else | `shell_project_panels.cpp` (New/Open/Reload/Quit + two Reload icons), `shell_file_paths.cpp` (`absorb_close_request`), `shell_recent_projects.cpp` (**one** call), and the smokes |

P2 is the structural proof: with both names absent from the recent module, the
only route from a Recent click to a session replacement is `begin_session_intent`,
whose first act is to consult `session.dirty()`.

### Inversions -- actual outcomes, not predictions

Twelve were specified. **Ten bit as specified. Two could not bite as written and
the CASES were strengthened, never the gates.** Two more bit, but in a
*different* case than the documents predicted.

| # | Inversion | Outcome |
|---|---|---|
| **I1** | Drop the arm; record on every successful save | **BIT — C21 phase 6.** `"an ORDINARY Save must record nothing. The list now holds '/…/marrow_mar183_c21/startup/mar180_shell.marrow'. Only Open, Save As, and the FIRST save of a New session record (AC2), and this session was created by neither."` The planner's warning held: the naive shape (open A, then Ctrl+S) **cannot** bite, because A is already the head, `promote` returns false and the no-op skip suppresses the write. The case uses the **startup** path (`reload_project`, which records nothing) against an **absent** settings file, and asserts both the empty list and the absent file |
| **I1b** | Record at `create` instead of at the first save | **BIT — C21 phase 5.** `"New must record NOTHING -- there is no file on disk yet to record."` |
| **I2a** | Retarget `intent` but not `path` in "last wish wins" | **BIT — C22 phase 5.** `"the LAST wish must win the destination as well as the intent. Expected '/…/mar183-c22-other.marrow', measured '/…/mar180_shell.marrow'."` |
| **I2b** | Assign `path` only when non-empty | **BIT — C22 phase 7.** `"retargeting to a PATHLESS intent must CLEAR the destination, but it still holds '/…/mar180_shell.marrow'. A conditional assignment leaves a stale Open target on a Reload."` |
| **I3** | `open_recent_project` calls `session.open` directly | **BIT — C25 assertion 3.** `"clicking a recent entry over UNSAVED work must raise the Save/Discard/Cancel prompt. No intent was armed, so the Recent surface bypassed MAR-182's gate."` |
| **I4** | Promote on a **failed** open | **BIT — C21 phase 2.** `"a FAILED Open must record nothing and leave the order unchanged. The head is now '/…/mar183-broken.marrow'."` |
| **I5** | Prune missing entries on load | **BIT — C23 assertion 2.** `"a recent entry whose file does NOT exist must survive the load. Pruning it would delete the user's bookmark over a transient unmount."` |
| **I6** | `canonical_recent_path` returns the path unchanged | **BIT — P-case** (`"two spellings of one file must collapse to exactly one entry, got 2"`) **and C20** (`"a relative path must be stored ABSOLUTE -- otherwise the list is relative to whichever directory the editor happened to be launched from."`) |
| **I6b** | `normalize` keeps the **last** duplicate | **DID NOT BITE as written.** The fixture's duplicate pair was adjacent and at the front, where keep-first and keep-last land on the same index. **The case was strengthened**: the pair is now deliberately **non-adjacent**, with two unrelated entries between the spellings, and index 0 is asserted rather than mere presence. It now fails with `"dedup must keep the FIRST occurrence AT ITS ORIGINAL INDEX 0"` and `"the entries that separated the duplicate pair must keep their positions behind the survivor"` |
| **I7a** | Truncate **before** inserting | **BIT — P-case.** `"twelve promotions must leave exactly kRecentProjectLimit entries, got 11"` |
| **I7b** | Cap comparison off by one (`> limit + 1`) | **DID NOT BITE as written.** Sampling only the final size after 12 promotions tests the one parity where the bug is invisible: the list sits at 11 on odd promotions and corrects on even ones. **The case was strengthened**: the bound is now asserted as an **invariant after every promotion**. It now fails with `"the list must NEVER exceed kRecentProjectLimit -- after promotion 11 it held 11"` |
| **I8** | Route a record through `session.begin_edit` | **BIT, but not where predicted.** The documents name C24 assertion 1. Its first detectors are two **shipped MAR-181 guards** that run earlier: C4 (`"the explicit Save must clear the dirty flag."`) and then C5 (`"a successful Save As must land clean."`). C24 assertion 1 asserts the same property directly (a four-value comparison across the record call) but is not the first to fire; the property is over-determined by existing coverage |
| **I9** | Never emit the `Open Recent` submenu | **BIT — C25 phase 0.** `"\"File###Menu_00\" never emitted a widget with the id of \"Open Recent\". A real mouse swept every position in the window and HoveredId never equalled that id, so the widget is absent or unreachable."` |
| **I10** | Disable the `Remove` items for missing entries | **BIT — C25 assertion 5.** `"clicking Remove on the missing entry must delete exactly it."` Because M3 measured disabled items as **hoverable**, the sweep still finds the label and the assertion catches the **effect** rather than the presence -- the stronger form |
| **I11** | Write the settings file during `load_shell_preferences` | **BIT, but not where predicted.** The documents name C23 assertions 1 and 8. **Four** detectors fire, three of them earlier: two shipped MAR-170 guards (`"A load-only preference session must create no settings file."`, then `"Loading a malformed settings file repaired it or changed a keyframe."`), then **C21 phase 6** (`"loading must not create the settings file."`), and only then C23 |
| **I12** | Drop the no-op skip in `persist_recent_projects` | **BIT — C23 assertion 7.** `"the no-op skip must leave the settings file's mtime untouched -- rewriting identical bytes is still a write."` |
| — | Iterate the **live** vector instead of a copy | **DID NOT REPRODUCE.** Neither a crash nor a skipped entry appeared under C25's click sequence. **The copy was kept anyway**: mutating the vector being iterated is genuine undefined behaviour, and this harness's particular click ordering not tripping it is not a guarantee |
| — | `apply_save_as` records `project_path` instead of `chosen` | **BIT — C21 phase 3.** `"Save As must record the NEW path at the head, not the old one."` |
| — | Drop the empty-path branch (always `arm_open`) | **BIT — C22 phase 2**, `"an UNTARGETED Open must raise the chooser -- file_path_request is empty or not an Open."` MAR-181 **C9 step 4** fires first on the rail; C22 phase 2 was confirmed to bite independently by temporarily hoisting it ahead of C9 |
| — | `persist_recent_projects` builds a fresh `EditorPreferences` | **BIT — C23 assertion 6**, `"a recent-list write must preserve default_curve."` |
| — | `persist_recent_projects` returns true on a failed save | **BIT — C24 assertion 3**, `"a failed settings write must report an error."` |
| — | `normalize_recent_paths` non-idempotent | **BIT — P-case**, five assertions including `"dedup must keep the FIRST occurrence AT ITS ORIGINAL INDEX 0"` |
| **R1** | Drop the `present` argument from the recent entry's `MenuItem` (`shell_recent_projects.cpp:110-111`), making a **missing** entry clickable | **BIT — C25 assertion 4, but only after the review pass repaired it.** As originally written the assertion could **not** bite: it clicked the missing entry on a **clean** session and then read `dirty_intent`, `pending_file_application` and `project_path`, all three of which are vacuous there — the clean session arms immediately via `arm_open`, the arm is consumed by the `apply_pending_file_action` at the tail of the same `render_frame` (which **resets** the optional at its head), and the failed `session.open` returns **before** `project_path = pending.path`. What survives is the failure **report**, so the assertion now snapshots `error_message`/`status_message` across the click. It fails with `"MAR-183 C25 assertion 4: a missing recent entry must NOT be actionable, but the click CHANGED the shell's messages -- status '' -> 'Project load failed', error '' -> '/private/var/folders/…/T/marrow_mar183_c25/gone.marrow:1:1: failed to open file'. Dropping the `present` argument from the entry's MenuItem makes a dead path clickable: the open is attempted and fails, which is the only trace that survives apply_pending_file_action."` **No other case catches it** -- established by reading each driver, not by assertion. **Five** things outside the app call `draw_menu_bar`: `shell_smoke_project.cpp:2310` (MAR-181 C9), `:2534` (MAR-181 C10), `:3520` (C25), `:5347` (MAR-182 C19), and `shell_smoke_frames.cpp:64`. C9, C10 and C19 each construct a bare `ShellState` and name `preferences`, `load_shell_preferences` and `recent_projects` **nowhere**, so the list is empty, `BeginMenu(kRecentMenu, !entries.empty())` is disabled and the entry loop never runs -- and C10 never opens the File menu at all. The frames driver fails as a detector twice over: its `load_shell_preferences` runs inside `ScopedPreferenceIsolation("smoke")` against a fresh temp config home that nothing records into, and its mouse events target the viewport, timeline and graph, never the File menu. It is also **not** a separate binary -- `CMakeLists.txt:876`/`:906` compile it into `marrow_editor_shell` itself -- and it runs in the final `&&` chain **after** C25, so under R1 the run has already returned 1 before it starts. Confirmed by running the mutation against the whole suite: exactly one test fails, `marrow.editor_shell_smoke` |
| **R2** | Move `apply_save_as`'s `state->error_message.clear()` back **after** `record_recent_project` (its position at `25bf694`) | **BIT — C21 phase 4b**, added by the same pass, `"MAR-183 C21 phase 4b: a failed settings write on the Save As path must be REPORTED. Design 10.7 grants this failure exactly ONE report -- 'reported once and then forgotten' -- and clearing error_message AFTER record_recent_project rather than before it swallows that one report, so the failure is reported zero times."` The other two record sites already ordered it correctly (`shell_core.cpp:684` clears before recording; the Open branch never clears afterwards), so only Save As was affected |

### Document errors found during implementation (6)

Every story in this arc has found errors in its own governing documents (175
three, 176 seven, 177 six, 178 six, 179 six, 180 seven, 181 four plus a later
fifth, 182 six).

| # | Where | Error | Resolution |
|---|---|---|---|
| D1 | Design §1.2 and plan §0.3 | Both assert that `grep -rniE "recent\|mru\|last_?opened\|project_history"` over `src/` returns hits in **"exactly two files"**, and the plan makes a non-empty result a **stop condition** | The grep returns hits in **ten** files. Every extra hit is a false positive: `handle_project_history_shortcuts` is the **undo/redo + Ctrl+S** handler ("project history" = the undo history, not a project list), `recent(%.2f, %.2f)` is preview root-motion prose, and the `recent_projects` mentions in `shell_preferences.*` are comments about *preserving* the field. The substantive claim -- that no recent-projects **feature** exists -- was re-verified directly and **holds**: zero readers, zero writers |
| D2 | Design §2.2 and §10.1 | State the macOS consequence as unconditional: *"opening `/x/A.marrow` and then `/x/a.marrow` … produces two entries"* | **Measured, and it is only half true -- in the half that matters, backwards.** `weakly_canonical` resolves its longest **existing** prefix through the filesystem, so on macOS both spellings of a file that **exists** canonicalize to the on-disk spelling and **collapse into one entry**. Only when the file is **missing** do the lexical remainders survive and leave two. Both branches are now asserted and printed by the P-case every run, so the behaviour cannot drift silently |
| D3 | Plan §1.4 | Requires `grep -nE "session\|ShellState\|PreferenceStore\|imgui\|ProjectData"` over the new module to return **0 lines** | Unsatisfiable as written: the header **documents** the split it enforces, so three doc-comment lines match. The substantive check was run instead -- the complete `#include` list is `<cstddef> <filesystem> <vector> <algorithm> <system_error> <utility>` plus the module's own header, and **no non-comment line** matches. `test_editor_session_isolation` still passes |
| D4 | Plan §1.5 | Predicts inversion **I7a** ("`resize` before `insert`") fails with *"`front()` is `p11`, not `p12`"* | The predicted symptom does not follow from the mutation. Truncate-before-insert leaves the newest entry at the head and the **size** wrong: the actual failure is `"twelve promotions must leave exactly kRecentProjectLimit entries, got 11"` |
| D5 | The plan's task list | Design §7.2 and §5 both require **C20**, but **no task in the plan implements it**. Tasks 1-6 cover the P-case, C21, C22, C23, C24 and C25 only | C20 was written and added to the rail. It is falsifiable: under I6 it fails with `"a relative path must be stored ABSOLUTE …"` |
| D6 | Design §6 / plan §3.5, §4.4, §6.2 | Attribute **I8** to C24 assertion 1 and **I11** to C23 assertions 1 and 8 | Both bite, but each has earlier detectors among **shipped** guards -- I8 in MAR-181 C4 then C5, I11 in two MAR-170 guards then MAR-183's own C21 phase 6. The named assertions are real and correct; they are simply not the first to fire. Recorded rather than "fixed", because over-determination here is a strength |

### Not independently covered

- **`shell_main.cpp`'s frame body is still reachable from no test.** Unchanged
  by this story, which adds **no line** to either hand-maintained frame body:
  the menu draws inside `draw_menu_bar`, which both bodies already call, and the
  deferral rides `apply_pending_file_action`, which MAR-181 C11 already pins on
  the **smoke** side only. Carried forward from MAR-182.
- **macOS case duplicates**, in the corrected form measured in D2: two
  case-variant spellings of a **missing** file remain two entries.
- **A New project created over an existing file, then saved, is recorded** --
  the arm is keyed on the create, not on the file's prior absence. C21 phase 5
  asserts both polarities of the arm for exactly this reason.
- **Up to `kRecentProjectLimit` `stat` calls per frame** while the submenu is
  open, and a `stat` on a dead network mount can block one.
- **An oversized on-disk list stays oversized** until the next real mutation,
  because loading never writes.
- **Orphan `*.tmp.*` files** beside `editor-settings.json` after a crash
  mid-write. `fsync` remains a non-goal, inherited from MAR-180 §3.2.
- **A settings write failure is reported once and then forgotten.** The
  in-memory list keeps the change, so the session behaves as if it persisted and
  the next launch disagrees. Identical to `set_shell_default_curve`'s shipped
  behaviour, and deliberately not diverged from it.
- **`commit_path_choice` has ZERO end-to-end coverage** (`shell_file_paths.cpp`,
  single call site inside the chooser modal). No smoke anywhere in `src/editor/`
  clicks `"Choose"`. **Pre-existing from MAR-181**, and MAR-183 does **not**
  close it: C25 clicks `Open Recent` entries, `Remove` and `Clear Missing`, and
  C22 phase 2 asserts only that the chooser is *raised*. Carried here from the
  MAR-182 review, which raised its consequence: MAR-182 made this the only exit
  from `AwaitingSave`. It is partly self-mitigated -- `tick_dirty_intent` tests
  `!session.dirty()` **before** `file_path_request`, so a stale request cannot
  deadlock; only a `commit_path_choice` that stopped calling `apply_save_as`
  would hang the prompt, and nothing would observe it. **MAR-183 adds a new
  consequence to BOTH of that call site's action branches, not just one**: the
  SaveAs branch calls `apply_save_as`, which now writes the settings file on
  success, and the **Open** branch calls `arm_open` (`commit_path_choice:189-192`),
  whose deferred `apply_pending_file_action:868` records -- and therefore writes
  -- on a successful open. Each branch now has a preference write behind a click
  no end-to-end test reaches. Both *destinations* are covered UI-free --
  `apply_save_as` by MAR-181 C5/C6/C7 and by C21 phases 3, 4 and 4b (a **failed**
  Save As writes nothing inside the rename seam, and a **successful** one whose
  settings write fails still reports it), and the armed Open by C21 phases 1-2
  and by C22 -- it is the *click that reaches them* that is untested. Closing
  this needs a smoke that drives `Choose` end to end -- its own piece of work,
  deliberately not attempted here.
- **A native close during a live authoring gesture re-raises the prompt.**
  `absorb_close_request` is **not** gated on `authoring_gesture_active`, though
  the File-menu items are, so an OS close mid-gesture raises the prompt and
  `Save` then returns false at `save_project_file`'s gesture guard, re-raising
  it. There is no fall-through and the status bar explains ("Finish the active
  edit before saving"), so this is a **UX wrinkle, not a correctness problem**.
  Recorded from the MAR-182 review; the gating is deliberately **left
  unchanged** -- that is a design decision for a later story, not a drive-by.
- **A persisted "last used directory"** for the chooser is **deliberately
  deferred**, not forgotten. `shell_file_paths.cpp` names it as MAR-183's
  territory, but it is in none of MAR-183's six acceptance criteria and is a
  different preference with a different lifetime and failure mode.

## MAR-182 Unified Dirty-Session Intent Validation Results

Validated 2026-08-30. MAR-182 puts one Save / Discard / Cancel state machine in
front of every path that can destroy unsaved work. The story title says "unify",
which implies several inconsistent checks existed. **Zero existed.** `grep -rn
"dirty" src/editor/shell_file_paths.cpp` at `d743569` returned one call *after* a
successful save and one two-line comment; `grep -rn "unsaved" src/editor` found
two status strings and a chip label and no control flow. The work is
**construction**, not reconciliation, and five unguarded paths were closed at
once: New, Open, Reload (**three** surfaces, not one), Quit, and the native OS
close -- which could not be vetoed at all, because `SdlWindowHost`'s latch had no
runtime clearer.

The gate reads **`EditorSession::dirty()`**, which is content-keyed
(`serialize_project(*load.project) != saved_serialized_project`). It deliberately
does **not** read `ShellState::project_dirty`: that is a display cache assigned
from a caller-supplied boolean at `shell_core.cpp:572`, and two of its ~40
refresh points exist only in `shell_main.cpp`, so a gate keyed on it would mean
something different in the smoke than in the shipped shell. MAR-182 neither reads
nor writes it. No format, ABI or protocol change -- `git diff --stat` over
`include/marrow/c/`, `src/c/`, `include/marrow/runtime/`, `project.cpp`,
`session.cpp`, `atomic_file_write.cpp` and `format-spec.md` is **empty**. The
registry is unchanged at **64**, proved by an empty diff over
`agent_dispatch.cpp`, `agent_handlers_*.cpp`, `agent_dispatch_smoke.cpp` and
`tools/`.

### What was measured before any code was written

| Claim | Measured |
|---|---|
| Registry and its split | **64** rows, **39** edit / **12** inspection / **10** management / **3** validation. **10** `!= 64U` guards, **1** `std::array<OperationExpectation, 64>`, **2** python assertions -- all unmoved afterwards |
| Zero dirty checks exist (design §1.1) | Confirmed. One `update_project_dirty_state` after a *successful* save, and one comment. No gate anywhere |
| Five discard paths | All confirmed at the stated sites, including **three** Reload surfaces (menu, toolbar icon, Project-panel icon) all writing one `bool*` consumed in **both** duplicate frame bodies |
| The OS close cannot be vetoed | Confirmed: `close_requested_` is written at `:82` (poll), `:109` (`request_close`) and `:174` (`shutdown`), and cleared **only** in `initialize()` at `:68`. No runtime clearer existed |
| Hot-reload is not a discard path | Confirmed at `session.cpp:2001-2009`: adoption replaces the four *runtime* fields and calls `update_dirty()`; the authored `ProjectData` and both history stacks survive |
| No agent operation replaces a session | Confirmed: no `open`/`create`/`reload`/`close`/`new` row in the 64-row registry. `agent.terminate` ends an *agent* session |
| `saved_project_snapshot` is write-only | **4** lines -- 1 declaration, 3 writes, **0 reads** -- before and after |
| `EditorWindowHost` implementors | Exactly **one** (`sdl_window_host.cpp:34`), so deleting `request_close()` is compiler-enforced |
| `resolve_choice`'s acceptance rule (design §4.5) | `Choose` is gated on `FilePathChoice::acceptable` **alone** (`shell_file_paths.cpp:269`), and a Save target that already exists is **accepted** with `diagnostic = "Replaces the existing file."`. Confirmed before any code and **not touched** by MAR-182 — `resolve_choice` does not appear in this story's diff |

### Results

| Case | Assertion | Result |
|---|---|---|
| C12 | The gate over all four intents. Dirty: each arms `dirty_intent` and moves **none** of `pending_file_application` / `new_project_form` / `file_path_request` / `should_exit`, snapshot bit-identical. Clean: each performs immediately into its **own** field | PASS |
| C13 | Save completes the intent only after a real save: session clean, intent cleared, chooser up with `action == Open`, and the written file **reloads through `load_project`** -- a passing `save()` proves nothing, because `validate_project_for_save` takes no base document | PASS |
| C14 | A failed save never falls through. Under an injected `permission_denied` rename: `should_exit == false`, intent still `Quit`/`Prompting`, session dirty, `error_message` non-empty, destination **byte-identical** and still loadable. Seam released, Save again → exits and the file reloads | PASS |
| C15 | Save-path cancellation **and** completion over an existing destination. **Half A**: empty `project_path` (the documented guard branch) → `phase == AwaitingSave` + a `SaveAs` request; clear it, tick → back to `Prompting` with the **same** intent, nothing written; Cancel → bit-identical. **Half B**: re-enter `AwaitingSave`, assert the existing destination is `acceptable == true` **and** carries `"Replaces the existing file."` *simultaneously*, commit it, tick → the held `Reload` intent is performed, the session goes clean, and the **overwritten** file reloads | PASS |
| C16 | Discard performs without persisting. `pending_file_application->action == Reload`, session **still dirty**, file **byte-identical**; then the reload succeeds, goes clean, and the unsaved note is **gone**. Two independent comparisons | PASS |
| C17 | Cancel and repeats. Two `New` → one intent; `Quit` replaces it; Cancel is bit-identical and re-expressible; an intent raised during `AwaitingSave` is **ignored** | PASS |
| C18 | `absorb_close_request` in both polarities. Clean → `false` + `should_exit`; dirty → **`true`** (the veto) + no exit + a `Quit` intent; repeat still vetoes; Discard → exits; a call after that → `false` **and arms nothing** | PASS |
| C19 | The prompt by a **real mouse**: File > Reload Project on a dirty session raises `Unsaved Changes##dirty_intent` at root scope with `Save`/`Discard`/`Cancel` all found by `HoveredId`; Cancel is bit-identical; an external close of the prompt clears the intent; **File > Quit arms a `Quit` intent rather than exiting**; an external close of the chooser clears `file_path_request` | PASS |

Two harness facts the plan required be **measured, not assumed**:

- **Escape does NOT close a `p_open == nullptr` modal under this harness.** C19
  prints this every run and picks its close route from it, so phases 3-5 use
  `ImGui::ClosePopupToLevel(0, true)` from `imgui_internal.h`. The first reading
  said the opposite and was an **artifact**: `FindWindowByName` returned `nullptr`
  for a modal that did not yet exist, which the probe scored as "closed". The
  measurement only became meaningful once the modal was drawn -- a reminder that
  a probe over an absent object measures nothing.
- **The `AlwaysAutoResize` first-frame stub rect** for `kDirtyIntentModal` is
  `(60,60)-(76,97)`, **16x37 px** -- the same 16x37 stub MAR-181 measured for the
  chooser at `(452,259)-(468,296)`, at a different origin. Confirmed rather than
  inherited. A sweep that captured bounds immediately would scan a sliver and
  report every button absent; C19 renders three settle frames and re-reads.

The File menu popup's ImGui window name re-measured as `File###Menu_00`,
unchanged from MAR-181.

### Inversions -- actual outcomes, not predictions

| # | Inversion | Predicted | **Measured** |
|---|---|---|---|
| I1 | `begin_session_intent` performs regardless of `session.dirty()` | C12 | **C12, C13, C14, C15, C17**: `a dirty session must ARM the prompt, not perform the intent. dirty_intent is empty.` Broader than predicted -- every case that depends on the prompt arming |
| I2 | Gate on `state->project_dirty` instead of `session.dirty()` | C12 **and C16** | **C12, C13, C14, C15, C17 -- and C16 PASSES.** The prediction was wrong (see D3). The stale cache does open the gate exactly as §1.3 argues, but C16 cannot see it: with the gate open the Reload is performed immediately, `resolve_dirty_intent(Discard)` becomes a no-op, and all four of C16's assertions still hold |
| I3 | `Save` performs the intent before checking dirtiness | C13, C14 | **C13, C14, C15, C17.** C14 is the sharp one: `a FAILED save fell through to the Quit intent -- should_exit is true with the project unwritten.` |
| I4 | Completion keyed on `save_project_file`'s return, not `!session.dirty()` | C15 | **C15, C17**: `a Save with no destination must park the intent in AwaitingSave while the chooser is up.` The Save As branch has no return to read |
| I5 | `Discard` saves first | C16 | **C16 alone**, on the byte compare: `Discard must not save; the session stays dirty until the replacement lands.` Exact |
| I6 | `Cancel` disturbs the session | C17 | **C15, C17**: `Cancel must leave the session bit-identical.` C15 asserts the same property |
| I7 | `begin_session_intent` stacks instead of replaces | C17 | **C17 alone**: `a second intent must REPLACE the first (last wish wins), not stack behind it.` Exact |
| I8 | `absorb_close_request` returns `false` on a dirty project | C18 | **C18 alone**: `a DIRTY session must VETO the close -- the return value is what tells the loop to clear the host's latch` |
| I8b | Veto even after a confirmed exit | C18 | **DID NOT BITE as first written.** The clause never changes the return value -- `!should_exit` is already `false` on that path -- so the return-value assertion could not see it. What removal actually breaks is different: Discard leaves the session **dirty**, so the absorber re-enters the gate and **arms a fresh prompt during shutdown**. C18 was strengthened to assert that a post-exit call arms nothing, and now fails: `the absorber re-entered the gate during shutdown and raised a prompt behind a window that is already closing.` **That message describes the pure function's CONTRACT, not an observed shell behaviour.** In the shipping loop the clause is unreachable: `while (!should_exit)` ends the iteration before `absorb_close_request` runs again. The assertion is kept because it guards the contract -- `absorb_close_request` is a pure, separately callable function and nothing in its signature promises a caller that respects that ordering -- not because a live failure was seen |
| I9 | Leave the menu's `Quit` unwired from the gate | C19 | **DID NOT BITE as first written** -- the fifth consecutive story with a specified inversion that could not fail. C19 clicked only `Reload Project`, so an unwired `Quit` was invisible to it, and C12-C18 pass by construction. **C19 phase 4 was added** to probe the `Quit` item itself. Re-run under I9, C19 fails alone: `the Quit menu item must arm a Quit intent through begin_session_intent. It did not, so Quit is handled somewhere this smoke's frame body does not run.` |
| I10 | Skip the stale-request fix | C15, C19 | **C19 phase 5 alone; C15 PASSES** (see D4). C15 simulates the chooser's Cancel by resetting the optional directly, so it never drives the ImGui close path the fix lives on |
| I11 | Draw the prompt outside `draw_file_path_modals` | C19 | **C19 alone**, at phase 1: the prompt never opens, which is the duplicate-frame-body hazard caught at the layer that owns it |
| I12 | Reject an existing Save target, i.e. MAR-181 design §3.2's rule | C15 half B | **C15 half B** (and MAR-181 **C8**, a pre-existing net that fires first and had to be bypassed to observe C15 directly): `a Save target that already exists must be ACCEPTED. It is rejected, so \`Choose\` is disabled exactly in the common case, and an AwaitingSave intent raised from the prompt has NO EXIT.` C12-C14 pass — they never reach the chooser over an existing file |

### Corrections the design made to its governing documents (5)

Recorded here as well as in the design's §12, because they are corrections to
documents MAR-183 and later will also read.

| # | Where | Correction |
|---|---|---|
| A | `shell_file_paths.hpp:158-166` | Said MAR-182 would put the gate at the **top of `begin_file_action`**. Doing so forces the intent's own resolution to re-enter with the gate suppressed — a bypass flag, i.e. the provenance-keyed shape this arc has twice been bitten by. The gate went in `begin_session_intent`; `begin_file_action` stays the gate-free performer. **The comment was rewritten** |
| B | `shell_file_paths.hpp:170-177` | Claimed `apply_pending_file_action`'s bool was MAR-182's completion signal and that a cancel was `!file_path_request.has_value() && !applied`. That predicate is also true on every **idle** frame and after every **successful** action. MAR-182 does not use it; the intent's own `phase` is the signal. **The comment was corrected** and the function's signature and semantics are unchanged |
| C | The commissioning brief's reading of MAR-181's I2 | Treated `session.dirty()=true project_dirty=false` (`AGENTS.md:360`) as the two dirty notions "legitimately diverging". It is the **inversion's failure message** — what C4 prints when the code is deliberately broken. In the as-built tree they agree on every shipped path: one is the truth, the other a display cache of it. The choice of `session.dirty()` is unchanged but the reason is stronger |
| D | The brief's count of `saved_project_snapshot` writes | Said four; there are **three** writes plus one declaration, four lines total, zero reads |
| E | MAR-181 design §3.2 vs its own §3.4 rule 5 | §3.2's "`Choose` is disabled whenever the diagnostic is non-empty" contradicts §3.4's "an existing Save target is accepted *and* carries a diagnostic". **§3.4 is what shipped**, and MAR-182 depends on it: the prompt's Save reaches the same chooser over an existing file in the common case, so §3.2's rule would leave `AwaitingSave` with no exit. Now guarded by C15 half B and inversion I12 |

### Document errors found during implementation (6)

Every story in this arc has found errors in its own governing documents (175
three, 176 seven, 177 six, 178 six, 179 six, 180 seven, 181 four plus a later
fifth). These six are separate from the five above: they are errors in **MAR-182's
own** design and plan, found while executing them. Items D1, D3, D4 and D5 were
reported mid-implementation and the governing documents have since been revised
to match; they are recorded here as measured.

| # | Where | Error | Resolution |
|---|---|---|---|
| D1 | Design §1.5, §4.4 and plan Task 0 step 8 | All three assert `draw_file_path_modals` has **exactly one** call site (`shell_project_panels.cpp:924`). There are **two**: `:706` and `:925`. The plan makes this a **stop condition** -- "if there are two call sites, the modal draws twice -- stop" | The hazard does not exist. The two are **mutually exclusive branches of `draw_menu_bar`**: `:706` is the early return taken when `BeginMainMenuBar()` is clipped, `:925` the normal path. Exactly one runs per frame, and the modals are in fact drawn *more* reliably than the design assumed -- they survive a clipped menu bar. §4.4's conclusion holds unchanged. The in-code comment at `:924` also claimed "One call site" and has been rewritten. **The design has since been revised to match, and adds that the two branches must not be collapsed into one** — the early return is what keeps an open modal alive while the menu bar is clipped, so the prompt inherits that survival for free |
| D2 | Design §1.1's quoted grep | Shows **2** hits for `grep -rn "dirty" src/editor/shell_file_paths.cpp`. The actual output is **3** -- the comment spans `:695-696`, and only `:696` was quoted | Cosmetic. The substantive claim -- one call after a successful save, no gate anywhere -- is exactly right |
| D3 | Design §9 and plan Task 2, inversion I2 | Both name **C16** as I2's interesting detector, "because it dirties through a transaction without calling `update_project_dirty_state`, so the cache is stale and the gate opens". The premise is true; the conclusion is not | **Measured: C16 passes under I2.** With the gate open the Reload is performed at `begin_session_intent`, so `Discard` no-ops and C16's four assertions all still hold. C12 (and C13/C14/C15/C17) catch I2. The §1.3 argument stands; the case attribution was wrong |
| D4 | Design §9 and plan Task 4, inversion I10 | Predict **C15** fails without the stale-request fix | **Measured: C15 passes.** C15 clears `file_path_request` directly to simulate the chooser's Cancel, so it never exercises the ImGui close path the fix is on. Only C19 phase 5 observes it -- which is the plan's own lesson about UI-free helpers being unable to see a widget |
| D5 | Design §9/§11 R2 and plan Task 4, inversion I9 | Specify C19 as the observer of menu wiring, and predict it fails when `Quit` is unwired | **Measured: it did not bite.** C19 as specified clicks only `Reload Project`. Per the plan's own rule -- strengthen the case, never weaken the gate -- **C19 phase 4** was added, probing the `Quit` item by mouse and asserting a dirty click arms a `Quit` intent instead of exiting. It now fails alone under I9 |
| D6 | Design §1.3 | Says `ShellState::project_dirty` has "exactly **three** readers", then lists **four** sites (`:656`, `:932`, `:1024`, `:1037`) | Four read sites, all display-only. The argument -- that every one of them is display and none is control flow -- is unaffected |

### Not independently covered

**Which half each detector catches** (design R1), including the row where the
detector is none:

| Frame-body / loop line | Detector | Which half it catches |
|---|---|---|
| `apply_pending_file_action` in the **smoke's** body | MAR-181 C11 | smoke half only |
| `apply_pending_file_action` in **`shell_main.cpp`** | **none** | *(pre-existing gap, neither created nor closed here)* |
| `absorb_close_request` / `cancel_close_request` in the loop | **none** | manual check only; C18 covers the **decision**, not the call |
| the prompt itself | C19 | the shared `draw_menu_bar` path. **C19 runs its own miniature frame body**, not either shipped one, so for `shell_main.cpp` this is *structural inference, not test coverage* -- the same caveat as row 2 |

- **Row 2 is inherited, not introduced.** C11 catches deleting the *smoke's*
  `apply_pending_file_action` and is blind to deleting the *interactive* one --
  the exact inverse of the inversion it was built for. Closing it means unifying
  the two hand-maintained frame bodies, which is out of scope. MAR-182 adds **no
  line** to either frame body: its only frame-body edits are Task 1's deletions,
  and a deletion cannot create that divergence.
- **Row 3 is the one surface this story genuinely leaves untested.** The
  `absorb_close_request` call and the `cancel_close_request()` it guards live in
  the real main loop, and `run_headless_smoke` returns before a window host is
  ever created. C18 covers the decision in both polarities; that it is *called*
  is covered only by the manual check. Three mitigations, stated rather than
  hidden: (a) omitting the call breaks the **window close
  button, Cmd+Q and `SDL_EVENT_QUIT`** and nothing else -- `File > Quit` still
  reaches `begin_session_intent` and still sets `should_exit`, so the editor
  remains closable by the menu. **This corrects an earlier claim that omitting
  the call makes the editor *unclosable*, which was wrong**; the mitigation is
  weaker than previously stated, which is exactly why it is stated accurately
  here. It is still a loud failure rather than a silent data-loss one; (b) `EditorWindowHost::request_close()` is **deleted**, and with
  one implementor that deletion is compiler-enforced, so the old bypass cannot be
  reached by accident; (c) a manual interactive check is recorded in the
  verification list above.
- **`--auto-close` bypasses the prompt by design.** Smokes end on frame count,
  not on `should_exit`, so a dirty smoke exits without asking. Any other choice
  hangs CI.
- **The `AwaitingSave` window is real.** Between answering `Save` and committing
  the chooser, a close request is ignored per the transition table rather than
  queued. That is deliberate -- a save in flight must land -- but it will read as
  a dropped click to someone.
- **The New form keeps the stale-`opened` hole the chooser just lost.** It
  self-heals on the next `begin_file_action(New)`, because `new_project_form` is
  reassigned wholesale, and nothing in this machine reads it. Fixing it is
  unrelated scope; it is recorded, not closed.

## MAR-181 Core File Path Workflows Validation Results

Validated 2026-08-30. MAR-181 is the shell layer on top of MAR-180's primitives:
four File-menu items, two dependency-free ImGui modals, one pure refactor and one
shortcut. It adds **no model-layer primitive**, so `marrow_project_smoke` carries
none of the new coverage and `marrow_editor_shell` carries all of it. **No
`.marrow` schema change** — proved empirically, not asserted: the same loaded
fixture saved through MAR-181's new Save As seam and through the pre-existing
`save_project_file` seam produces **key sets that are identical, 56 keys to 56,
with zero keys on either side only**. `.mskl` v1, `.mbin` v2 and C ABI v1 are
untouched (`git diff --stat` on `include/marrow/c/`, `src/c/`,
`include/marrow/runtime/` is empty), as are `project.cpp`, `session.cpp`,
`atomic_file_write.*` and `preferences.cpp`. The registry is unchanged at 64,
proved by an empty `git diff --stat` over `agent_dispatch.cpp`,
`agent_handlers_*.cpp`, `agent_dispatch_smoke.cpp` and `tools/`.

### What was measured before any code was written

| Claim | Measured |
|---|---|
| Registry rows / split | **64**, split **39 edit / 12 inspection / 10 management / 3 validation** |
| `!= 64U` guards | **10** — `shell_smoke_constraints.cpp:147,676`; `shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603`; `shell_smoke_timeline.cpp:3697`, each with a message one line below |
| `std::array<OperationExpectation, 64>` | **1**, `agent_dispatch_smoke.cpp:39` |
| Python `== 64` assertions | **2**, `test_client.py:53,55` |
| File menu contents | Exactly two items: `Reload Project` (`:714-720`), `Quit` (`:721-723`). New/Open/Save/Save As **absent from every surface** |
| `EditorSession::create` callers outside `session.cpp` | **zero** |
| File-dialog capability anywhere in the tree | **one hit, and it is prose** (`docs/root1/research-windowing-glfw-vs-sdl3.md:27`). In-ImGui modals are therefore required, not preferred |
| `saved_project_snapshot` | 1 declaration, 3 writes, **0 reads** — still 4 lines total after MAR-181, which adds no fourth write |
| Two frame bodies | `shell_main.cpp:611` and `shell_smoke_frames.cpp:124`, hand-maintained duplicates |

### Result

| Check | Evidence | Status |
|---|---|---|
| Task 1's pure-refactor gate | `adopt_session_project_into_shell` extracted from `reload_project`, then `git diff --stat -- src/editor/shell_smoke_*.cpp src/samples/*.cpp src/tests/` was **empty**, with `ctest -L editor` 12/12 green. The extraction's whole claim is "nothing changed", and the ~40 existing smokes that call `reload_project` are a better witness than any new test | PASS |
| C4 New writes nothing | `exists(target)` is **false** after New and true only after an explicit Save; the session and shell are both dirty from birth; history empty; the saved file **RELOADS** via `load_project` | PASS |
| C5 Save As moves the shell path | `project_path` and `project()->source_path` agree at the new location; the moved project **RELOADS** with every asset resolving to the original file; the runtime asset watch list is **element-wise equal** across the move | PASS |
| C6 failed Save As | Injected `permission_denied` at the process-global rename seam under RAII: path unmoved on both the shell and the session, session still dirty, destination byte-identical and still openable, retry succeeds and reloads | PASS |
| C7 failed Open | Valid JSON naming a missing `.mskl`: `project_path` unmoved, six-value snapshot bit-identical, cached preview pointers unchanged and non-null | PASS |
| C8 path resolution | 11 rows against `resolve_choice`, each asserting the path **and** acceptance | PASS |
| C9 File menu by real mouse | A real mouse reaches `File`, then all six menu items, then the chooser and its Cancel | PASS |
| C10 `Ctrl+S` | Saves a dirty project to a file that reloads; the same chord with the chooser's `Name` field focused leaves it dirty | PASS |
| C11 deferred action reaches the smoke's frame body | Added because inversion I9 proved the planned coverage did **not** exist (below) | PASS |
| Registry unchanged at 64 | Zero-line diff over the four untouchable trees; re-measured 64 / 39-12-10-3, 10 guards, 1 array, 2 python assertions. `git diff -U0 \| grep -E '\b6[0-9]\b'` returns **2 lines, both `1.0f / 60.0f`** frame deltas in the new test harness — no count literal moved | PASS |
| Two-site frame-body gate | `apply_pending_file_action` appears **exactly once in each** of `shell_main.cpp` and `shell_smoke_frames.cpp` | PASS |
| No dirty check anywhere | `grep -n "dirty" src/editor/shell_file_paths.cpp` returns one `update_project_dirty_state` call after a **successful** save and two comment lines. No gate exists | PASS |
| `EditorSession::close` still unused | No caller outside `session.cpp` | PASS |
| Preference isolation | `~/Library/Application Support/Marrow` **does not exist** after the full run. Every rename-seam install is RAII-scoped and performs no preference save inside it | PASS |
| Full suite | `ctest` 22/22, `-L editor` 12/12, `-L runtime` 4/4; all 13 test binaries pass; `marrow_verify_third_party` verified (**no new dependency**); `marrow_constraint_warning_check` built | PASS |

### The measurement C9 was required to make, not assume

The design left the ImGui window name of an open `BeginMenu` popup unmeasured and
required the implementer to measure or report it. **Measured: `File###Menu_00`.**
Two further ImGui facts had to be measured before the mouse harness worked at all,
and both are recorded in the test:

- `##MainMenuBar`'s `InnerClipRect` is **`(0,21)-(1440,21)`**, a zero-height band —
  a menu bar's content lives in `MenuBarRect`, not the client area, so a sweep over
  `InnerClipRect` scans nothing. The sweep uses `window->Rect()`.
- An `ImGuiWindowFlags_AlwaysAutoResize` modal is submitted at a stub size on its
  first frame: the chooser's `Rect()` one frame after opening is
  **`(452,259)-(468,296)`**, 16x37 px. The sweep renders three settle frames and
  re-reads the rect, or it would scan a sliver and report every widget absent.
- Menu item ids are **not** `window->GetID(label)`. `MenuItemEx` does
  `PushID(label)` and submits `Selectable("")`, so the id is
  `ImHashStr("", 0, GetID(label))`; a menu-bar menu is seeded through
  `BeginMenuBar`'s `PushID("##MenuBar")`. Probing with the naive form finds nothing
  and looks exactly like a deleted widget.

### Inversions — ten planned, run, and one that changed the story

Each was applied, built, run and reverted; the working tree was byte-compared
against pre-inversion copies afterwards.

| # | Inversion | Result |
|---|---|---|
| I1 | New calls `save_project_file` right after `create` | **C4 alone fails**: `New must write NOTHING. …/new_project.marrow exists on disk, so some code path saved a project the user has not asked to save.` C5/C6/C7/C8/C9/C10 pass |
| I2 | Pass `project_is_clean = true` for New | **C4 alone fails, at a DIFFERENT assertion**: `a New project must be dirty from birth … session.dirty()=true project_dirty=false`. Two inversions failing two different clauses is what proves C4 covers two defects |
| I3 | Delete `project_path = chosen` from Save As success | **C5 and C6 fail.** `Save As must move the SHELL's project path to …/b/moved.marrow; it is …/a/mar180_shell.marrow.` C6 also fails because its retry half asserts the same success-branch move — a wider blast radius than the plan predicted, and correct |
| I4 | Move `project_path = chosen` above `session.save` | **C6 alone fails**: `a FAILED Save As must not move the shell's project path. It moved to …/b/mar180_shell.marrow rather than staying at …/a/mar180_shell.marrow.` Every byte assertion still passes, isolating exactly the divergence |
| I5 | Move `project_path = chosen` above `session.open` | **C7 alone fails**: `a failed Open must NOT move the shell's project path. It moved to …/broken.marrow, which would leave the toolbar's Save writing to a file the session never loaded.` The six-value snapshot assertions still pass, because `open` is already atomic — C7 tests the shell's bookkeeping, not the session's atomicity |
| I6 | Delete `rebase_project_paths` from `save_project`, then compare the two assertion forms | **This is evidence, not a defect hunt.** With the rebase gone, C5 fails on `the moved project must OPEN`. Both forms were then run against the same bytes: `json::load_document(moved) -> PASSES \| load_project(moved) -> FAILS \| load_project error: …/marrow_mar181_c5/b/player_idle.mskl:1:1: failed to open file`. **The parse assertion is blind to the exact bug the reload assertion catches** |
| I7 | Put `Ctrl+S` above the `io.WantTextInput` guard | **C10 alone fails, second half only**: `Ctrl+S fired while the chooser's Name field had keyboard focus. The handler must sit BELOW handle_project_history_shortcuts' io.WantTextInput guard, or typing a filename saves the project.` Half 1 still passes |
| I8 | Delete `Open Project...` from the File menu | **C9 alone fails**: `"File###Menu_00" never emitted a widget with the id of "Open Project..."`. C4-C7, C8 and C10 **all pass** — which is precisely why C9 exists: they call the shell seams directly and would pass with every menu item deleted |
| I9 | Omit `apply_pending_file_action` from `shell_smoke_frames.cpp` only | **DID NOT BITE.** The plan predicted C4 and C7 would fail. Measured: **all seven cases passed** with that line deleted, because C4 and C7 drive the UI-free seam directly. Per the plan's own rule — strengthen the test, never weaken the gate — **case C11 was added**, arming a deferred action before the headless frames and asserting it was consumed after them. Re-run under I9, **C11 fails alone**: `the headless smoke's frame body never called apply_pending_file_action …` |
| I10 | Reject an existing file in `SaveTarget` mode | **C8 alone fails, at the final row**: `row "SaveTarget over an existing file is ACCEPTED": expected acceptable but resolve_choice returned rejected with diagnostic "That file already exists."` |

### Document errors found (4)

Every story in this arc has found errors in its own governing documents (175
three, 176 seven, 177 six, 178 six, 179 six, 180 seven). MAR-181's four. The
first was corrected in the design document itself; the rest are recorded here.

| # | Where | Error | Resolution |
|---|---|---|---|
| D1 | Design §3.2, the `Choose` / `Cancel` row | Said "`Choose` is disabled whenever the diagnostic is non-empty", which **contradicts §3.4 rule 5** — a `SaveTarget` over an existing file is *accepted* and *carries* the diagnostic `"Replaces the existing file."` Gating on emptiness would refuse a legitimate, deliberate overwrite that MAR-180 made atomic | Acceptance and the diagnostic are two separate outputs: `FilePathChoice::acceptable` gates `Choose`, `::diagnostic` is display-only. §9 C8's last row already asserted acceptance, so §3.2 was the wrong half. **The design document has been corrected in place** (`…-design.md:256-257`) so MAR-182/183 do not inherit it |
| D2 | Design §4.5 and plan Task 1 step 2/3 | Cite the extraction range as `shell_core.cpp:566-643` and say `reload_project` "keeps `:544-565`". Line `:566` is `} else {` **inside the failed-load early return**, and `:643` is a blank line. Keeping only `:544-565` would truncate the failure branch mid-`if` | The reset block actually begins at `:572` (its explanatory comment) / `:574` (first statement) and the body ends at `:642`; the head that must be kept is `:544-570`, through the closing brace of the early return. Extracted on the real boundaries; Task 1's zero-line test diff gate passed |
| D3 | Plan Task 4 | Schedules case C10 in Task 4, but C10's second half needs `draw_file_path_modals` to render an `InputText` before `io.WantTextInput` can become true — and the modal body is not implemented until Task 6's step list. **Task 4 cannot be completed as written before Task 6** | C10 was written in Task 4 and went green only once Task 6's modal body existed. A reader following the plan literally will hit this |
| D4 | Plan's file-and-responsibility map | Three counts are low. "**two fields**" on `ShellState` → **three**: the New form needs `new_project_form` for the same menu-scope `ImGui::OpenPopup` reason §3.3 gives for the other two. "the **seven** new validators" → **nine** declared in `shell_smoke_scenarios.hpp`. "cases **C4–C10**" → **C4–C11**, because inversion I9 forced C11 into existence | Implemented at the real counts; C11's origin is recorded in the inversion table above |

### Not independently covered

- **Deleting the *interactive* frame body's `apply_pending_file_action` call
  (`shell_main.cpp:619`) is invisible to every test.** This is the exact
  **inverse** of inversion I9, and the coverage is therefore **one-directional**:
  C11 arms a deferred action before the headless frames and observes
  `shell_smoke_frames.cpp:131` consuming it, so the *smoke's* copy is checked,
  but nothing exercises `render_shell_frame` — in headless mode
  `shell_main.cpp:663` returns into `run_headless_smoke` before that function is
  ever reached. The only guard on the interactive twin is the comment block in
  each file naming the other. Closing it properly means unifying the two
  hand-maintained frame bodies, which is a refactor with no story behind it and
  is deliberately **not** attempted here.
- **C11's load-bearing assertion is `pending_file_application.has_value()`
  (`shell_smoke_project.cpp:2675`), not the `project_path.filename()` check at
  `:2687`.** The second is near-tautological — `EditorSession::open` on a file
  that does not exist cannot move the path, as C7 already proves — and is kept
  only as a cheap end-to-end restatement through the real frame body. If either
  is ever "simplified" away, it must be the second one. Verified statically that
  the first is sufficient and non-vacuous: `pending_file_application` has exactly
  one resetter in the tree (`shell_file_paths.cpp:637`), the four in-test
  `apply_pending_file_action` calls all run on local `ShellState`s and all
  precede the arm, and the arm is an unconditional assignment that cannot
  silently no-op.

### Two gaps recorded rather than half-closed

- **No dirty-session check exists anywhere in MAR-181.** A New or Open over a
  dirty session **discards unsaved work silently**. This is deliberate: MAR-182
  owns the intent state machine, and a partial check here is rework MAR-182 must
  undo. The seam is `begin_file_action(ShellState*, FileAction)` — every File
  surface MAR-181 adds, four menu items and `Ctrl+S`, calls it and nothing else —
  and `apply_pending_file_action` returns `bool` so MAR-182 can tell a cancel from
  a success without re-deriving it from shell fields.
- **New writes `active_animation: "idle"` for a rig that may have no `idle`
  clip.** Behaviourally inert by three re-measured links: `PreviewController::
  normalize_state` (`session.cpp:754-758`) falls back to the rig's first
  animation; `validate_project_for_save` (`project.cpp:5622-5627`) checks only
  preview-skin non-emptiness and never reads `active_animation`; and the shell's
  pick (`shell_core.cpp:608-620`) prefers it only when it resolves. The single
  observable consequence is one metadata line in the Project panel on a project
  that has not been saved. Fixing it properly needs an animation/skin picker in
  the New form, which no acceptance criterion asks for.

## MAR-180 Atomic Project I/O and Source Adoption Validation Results

Validated 2026-08-30. This story fixes three measured bugs rather than adding a
surface. **No `.marrow` schema change**: `git diff -U0` over `project.cpp` touches
no `root[...]`, no `emplace("...")`, no `find_member` and no `erase` — Save As
rewrites the *values* of five existing fields and adds, removes and renames
nothing. `.mskl` v1, `.mbin` v2 and C ABI v1 are untouched, so
`docs/root1/format-spec.md` is byte-identical. The registry is unchanged at 64,
proved by an empty `git diff --stat src/editor/agent_dispatch.cpp tools/`.

**The three measured bugs, each proved fixed by a RELOAD FROM DISK.**
`validate_project_for_save` (`project.cpp:5503`) takes no base document and
structurally cannot resolve a cross-reference, so a passing `save()` proves
nothing. Only `load_project` materializes the references and reaches
`build_project_runtime`, so every "the project is still good" assertion below goes
through it.

1. **`save_project` could silently lose data.** It opened the destination with
   `std::ofstream output(path)` at `project.cpp:7866`, truncating it before
   anything knew the new content was writable, and checked `if (!output)` at
   `:7874` — *before* `~ofstream` flushes. A close-time write error, the ordinary
   shape of a full filesystem, was therefore never observed: it returned
   **success** over a truncated file and `EditorSession::save` marked the session
   **clean**. The write now goes through the extracted `write_file_atomically`,
   which checks write, flush **and** close and replaces the destination with one
   rename.
2. **Save As was broken.** `save_project` changed only `saved_project.source_path`
   (`:7881`). Fixture asset paths are relative (`"skeleton": "player_idle.mskl"`)
   and `resolve_path` (`:6674`) resolves against the **new** directory, so a
   cross-directory Save As wrote a project `load_project` cannot open. The
   pre-existing case at `editor_project_smoke.cpp:1681` performs exactly that Save
   As and asserts only `json::load_document` at `:1683` — a raw JSON parse — so it
   passed. It is left exactly as it is; S2 adds the load-bearing assertion beside
   it, and **inversion 3 confirms the raw-parse clause still passes while
   `load_project` fails**.
3. **Runtime-source adoption mutated before it validated.**
   `shell_asset_watch.cpp:157-160` assigned `load_result.base_skeleton_document`
   and `atlas_data` before `rebuild_project_runtime` at `:162`, and
   `ShellState::load_result` is a **reference into the session**
   (`shell_state.hpp:788`). The rollback at `:176` discarded its own rebuild
   result, so `skeleton_data` could end up derived from a different document than
   `base_skeleton_document`. `EditorSession::adopt_runtime_sources` now builds
   everything into locals and swaps once; the shell's hand-rolled model-layer
   rollback is **deleted**, not duplicated.

`EditorSession::open` and `reload` were measured **already atomic**, and
`EditTransaction::commit` already rolls back a failed rebuild. The story's framing
implied session adoption was broadly non-atomic; the single non-atomic path was
the shell's. The work is scaled to that.

| Area | Evidence | Result |
| --- | --- | --- |
| AC — atomic save | `write_atomically` was **extracted, not copied**, from `preferences.cpp:356` into `src/editor/atomic_file_write.{hpp,cpp}` with `RenameCallback` and its test seam. The only edits: returning `std::string` instead of `PreferenceSaveResult`, and substituting a `subject` noun so `subject = "settings"` reproduces every message character for character. `preferences_internal.hpp` includes the new header, so `preference_store_tests.cpp` compiles unchanged | PASS |
| The extraction gate | `./build/marrow_preference_tests` -> `PreferenceStore: 11 cases passed`, including the rename-failure case at `preference_store_tests.cpp:748-800`, **with `git diff --stat src/tests/` EMPTY**. The settings writer is byte-identically covered by the test that covered it before | PASS |
| S1 — a failed save preserves the previous file | An injected `permission_denied` rename leaves the destination byte-identical to the pre-save read, still opening through `load_project`; exactly **one** rename attempted; the temporary lived in the destination directory, was not the destination, and no longer exists; a `directory_iterator` lists only the four seeded files | PASS |
| S2 — cross-directory Save As opens | `load_project(moved.marrow)` succeeds with non-null `skeleton_data`; the reloaded `resolved_skeleton_path()` and every `resolved_atlas_paths()` entry resolve (via `weakly_canonical`) to the **same absolute files** as before; the live session tracks what it wrote. The raw `json::load_document` clause is asserted immediately above, and passes either way — recorded so the contrast is measured, not assumed | PASS |
| S2 — `export_directory` rebases by identity | Asserted, not assumed: after a Save As out of the fixture directory, `resolved_export_skeleton_path()` still has the **original** fixture directory as an ancestor. Exports keep landing where they landed. This is the acceptance criterion's literal reading and it will surprise someone; offering the other reading is a UI question owned by MAR-181 | PASS |
| S3 — absolute references survive | The referenced `.mskl` is placed **inside** the Save As destination so a relative form is always representable and the inversion cannot be masked by temp-directory layout. The stored path comes back `is_absolute()` and string-equal | PASS |
| S4 — undo across a Save As still opens | Open, commit an edit, Save As, `undo()`, `save({})`, then `load_project` -> succeeds, skeleton still resolving to the original file. History snapshots hold whole `ProjectData`s, so rebasing only `source_path` pairs the NEW directory with the OLD relative paths | PASS |
| S5 — the history re-serialization | Rebasing a snapshot without refreshing its cached `serialized_project` makes `apply_history` derive `project_changed` from a stale string and bump `project_revision` on an undo that restored no project change. Asserted with a **preview-only** edit, whose before/after project content is identical | PASS |
| S6 — `create` is dirty from birth | Success, `has_project()`, **`dirty()` true**, empty history, materialized runtime and preview, and **`std::filesystem::exists(project_path)` false**. Only after an explicit `save({})` does `load_project` succeed and `dirty()` clear | PASS |
| S7 — a failed `create` changes nothing | Against a missing skeleton: failure with a load error, the six-value session snapshot (`serialize_project`, three revisions, `undo_count`, `dirty`) unchanged, `source_path` still the fixture, nothing written | PASS |
| S8 — `close` bumps, never resets | Project, runtime, preview and history discarded, `dirty()` false, and each of the three revisions **strictly greater**. A reset to zero would let a stale `observed_*` compare equal and skip the resync. The session stays reusable | PASS |
| S9 — lifecycle refuses an active transaction | `close()` and `create()` both refused, the transaction still commits, and the committed sentinel is readable through `session.project()` — the same `ProjectData` the transaction held | PASS |
| S10 — a failed adoption stays COHERENT | A `.mskl` that parses but fails `load_skeleton_data` (an orphan bone naming a missing parent) fails with `RuntimeBuildFailed`, leaves the six-value snapshot and bone count unchanged, and — the clause the return code cannot give you — `build_project_runtime(*session.project(), *session.base_skeleton_document())` **still succeeds**. Restoring the bytes then adopts cleanly, bumping runtime and preview revisions and **not** `project_revision` | PASS |
| C1 — the hot-reload smoke, unmodified | `validate_runtime_asset_hot_reload_smoke` (`shell_smoke_project.cpp:242`) is the regression net for the shell rewrite and is **not touched**. Its existing failed-poll stage already asserts pointer identity of `base_skeleton_document`, `skeleton_data` and every atlas across a failure | PASS |
| C2 — failed hot reload, at the shell layer | New scenario. Outcome `Failed`, `status_message == "Runtime asset hot-reload failed"`, project serialization / three revisions / selected animation / source bundle all unchanged, and `state.preview_skeleton`, `state.animation_state` and `animation_state->get_current(0)` all still **usable** — the shell is the only holder of those raw pointers. A following `reload_project` recovers | PASS |
| C3 — failed shell save | Returns `false`, `status_message == "Project save failed"`, `state.project_dirty` and `session.dirty()` **still true**, the file byte-for-byte unchanged and still opening. Retrying with the seam released saves cleanly. **No implementation change was needed** — `save_project_file` (`shell_core.cpp:647`) already returned early without clearing `project_dirty`; that is recorded rather than "fixed" | PASS |
| Registry unchanged at 64, proved BY DIFF | MAR-180 adds no operation and no count literal, so the proof is a **zero-line diff**, not an assertion. `git diff -U0 -- src/ tools/ CMakeLists.txt \| grep -E '^[+-]' \| grep -E '\b(62\|63\|64\|65)\b'` returns **0 lines**. Re-measured after the change: **64** rows, split **39/12/10/3**; the **10** `!= 64U` guards at the identical file:line positions; **1** `std::array<OperationExpectation, 64>`; **2** python assertions. `git diff --stat src/editor/agent_dispatch.cpp tools/` is **empty** | PASS |
| Preference isolation | `$HOME/Library/Application Support/Marrow` did **not** exist before the work and still does not. Every shell run set `MARROW_CONFIG_HOME` to a scratch directory. This story touches the preference store's own write path, so it was checked before and after every stage. No orphan temporary was left in any config directory | PASS |

**The `fsync` non-goal, stated rather than claimed away.** The extracted writer
does `fflush` + `fclose` on POSIX with **no `fsync`** on the file and none on the
parent directory. Temp-plus-rename guarantees that an observer sees either the
complete previous file or the complete new one, and that every write, flush and
close error is detected — it does **not** guarantee durability across power loss.
Adding `fsync` here would diverge from the settings writer for no story
requirement and put a synchronous disk barrier in the interactive save path. It is
recorded so a later story can take it knowingly. The Windows branch is stronger
than the POSIX one (`MOVEFILE_WRITE_THROUGH`); that asymmetry is pre-existing.

**Orphan temporaries, stated rather than claimed away.** Every *handled* failure
removes its temporary — S1 asserts both that the exact temporary is gone and that
the directory contains nothing else. A **crash** between the temporary's creation
and the rename cannot clean up, and one `*.tmp.*` file may remain beside the
project. Nothing removes it. There is no crash-injection test for this: killing a
process mid-`rename` is not reproducible in this suite, and the injected rename
failure is the strongest mechanically testable proxy.

**Discharged by MAR-188, except one clause that can never close.** See MAR-188's
section: AC1 created `$.editor.import_sources.psd` as a typed, parsed, validated
field and AC2 registered it in `rebase_project_paths` as the **sixth** family, so
"no such field exists" is now false and the criterion below is executable as
worded. Its *generalisation* -- "rebases every relative path" -- remains
**PERMANENTLY OPEN**, by exactly the amount `preserved_root` is opaque. Do not
write this up as "six families, done".

**One acceptance criterion cannot be satisfied as written.** The story requires
Save As to rebase a "PSD provenance path". No such field exists:
`grep -rn "import_sources" src/ include/ assets/` returns nothing, and `psd_path`
lives only as a transient `PsdImportOptions` field, never persisted.
`.marrow.editor.import_sources.psd` is scheduled for **MAR-188**
(`docs/root1/editing-gap-analysis.md:430`). MAR-180 instead makes
`rebase_project_paths` the single documented place a new project-relative field is
registered, and its doc comment names MAR-188. Relatedly, paths stored inside
`preserved_root` are round-tripped opaquely and cannot be reached from there, so
"rebases every relative path" is satisfiable only for the five known families.

Current validation:

- Task 0 re-measured every claim before any code, and every one held. Registry **64**, split **39 edit / 12 inspection / 10 management / 3 validation**; **10** `operation_count_before != 64U` guards at `shell_smoke_constraints.cpp:147,676`, `shell_smoke_graph.cpp:148,636,1558,2128,3142,3957,4603`, `shell_smoke_timeline.cpp:3697`; **1** array size at `agent_dispatch_smoke.cpp:39`; **2** python assertions at `test_client.py:53,55`. `save_project`'s direct `std::ofstream` at `project.cpp:7866` with no temp and no rename; the fixture's `runtime` block relative (`{'atlases': ['player_idle.matl'], 'skeleton': 'player_idle.mskl'}`); the five serialized path families at `project.cpp:4501,4521,4725,4729,4745` with `$.atlas_packs` rebuilt wholesale at `:4896-4901`; `import_sources` **absent**; `shell_asset_watch.cpp:157-160` mutating before `:162` builds, with the discarded `(void)rebuild_project_runtime(state)` at `:176`; `ShellState::load_result` a reference at `shell_state.hpp:788`; `open`/`reload` already atomic; no `EditorSession::create` or `close`. Baselines: `ctest -N` **22**, `marrow_agent_dispatch_smoke` **408** `[ OK ]`, `marrow_preference_tests` **11 cases**, `marrow_project_smoke` passing
- `cmake --build build` -> clean, zero new warnings; `cmake --build build --target marrow_verify_third_party` -> vendored hashes verified; `cmake --build build --target marrow_constraint_warning_check` -> passed
- `./build/marrow_preference_tests` -> `PreferenceStore: 11 cases passed`, with `git diff --stat src/tests/` **empty** — the extraction gate
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed, printing the S1-S10 lines
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> `Headless editor shell smoke rendered 2 frame(s).`, including the unmodified C1 and the new C2 and C3
- `./build/marrow_agent_dispatch_smoke` -> passed over **408** `[ OK ]` cases, **identical** to the Task 0 baseline, against the exact 64-operation registry
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` with `tools/mcp/venv/bin/python tools/mcp/test_client.py` -> `mcp test_client: PASSED` with 64/64 name parity; `python3 -m py_compile` clean over `server.py`, `test_client.py`, `tools/editing.py`, `tools/inspection.py`
- `./build/marrow_unit_tests`, `marrow_windowing_tests`, `marrow_pen_input_tests`, `marrow_selection_tests`, `marrow_viewport_interaction_tests`, `marrow_timeline_model_tests`, `marrow_timeline_graph_model_tests`, `marrow_agent_socket_tests`, `marrow_bootstrap`, `marrow_c_smoke`, `marrow_parameter_project_smoke`, `marrow_atlas_packer_smoke`, both `marrow_fixture_smoke` invocations, `marrow_psd_import_smoke` and `marrow_spine_import_smoke` -> all passed. The last three are in the list because they exercise `write_text_file`/`write_binary_file` and the atlas-pack path families the rebase touches
- `ctest --test-dir build --output-on-failure` -> `100% tests passed, 0 tests failed out of 22`; `-L runtime` -> 4/4; `-L editor` -> 12/12. No CMake target added; the one `CMakeLists.txt` change is the new `src/editor/atomic_file_write.cpp` source line
- **Fourteen inversions run, each restored, each re-verified.** 1 `save_project` restored to the direct `std::ofstream` -> S1, and an instrumented probe recorded `rename_calls=0 bytes_preserved=0 save_ok=1`: the seam is never reached **and** the destination was already destroyed, two independent clauses. 2 `cleanup_temporary()` removed from the rename-failure branch -> `S1: a handled rename failure must remove the exact temporary file`. 3 the `rebase_project_paths` call deleted -> `S2: a cross-directory Save As must write a project that OPENS`, **with the `json::load_document` clause asserted immediately above passing in the same run** — the shipped example of a test that cannot fail. 4 the `is_absolute()` early return deleted -> `S3: an absolute skeleton reference must survive a Save As unchanged (got player_idle.mskl)`. 5 `rebase_history` rebasing only `source_path` -> `S4: a project saved after undoing across a Save As must still OPEN`, **with S1, S2 and S3 all still passing**. 6 the snapshot re-serialization skipped -> `S5: undoing a PREVIEW-only edit must not bump project_revision`, **with S1-S4 all still passing**. 7 `base_skeleton_document` swapped in before the build -> `S10: COHERENCE`, while the `RuntimeBuildFailed` clause asserted just above it **passed** — the return code was never the problem. 8 (see below). 9 the preview-alias re-fetch omitted from the rewritten shell path -> the shell **segfaults (rc=139)** inside C1, before C2 is reached: a stronger failure than the predicted assertion. 10 `create` setting `saved_serialized_project` the way `open` does -> `S6: a created project has never been written, so the session must be dirty from birth`, every other clause passing. 11 `create` assigning `impl_->load.project` before loading the skeleton -> `S7: a failed create must leave the session's six-value authoring snapshot unchanged`. 12 `close` zeroing the three revisions -> `S8: close must BUMP all three revisions, never reset them`. 13 the transaction gate removed from `close` -> `S9: close must refuse while an edit transaction is active`. 14 (see below)
- **Two specified inversions did not bite as written, and were replaced rather than waived.** Inversion 8 as specified (`adopt_runtime_sources` commits `load.skeleton_data` before the preview bind) **passed everything**: `PreviewController` holds its own `shared_ptr` to the skeleton data, so reassigning `load.skeleton_data` frees nothing and the shell's cached pointers stay valid. Replaced with **8c** — the rewritten shell path nulls `preview_skeleton`/`animation_state` on the failure branch, the way `reload_project` does — which fails `C2: the shell's cached preview pointers must still be USABLE after a failed hot reload` **while S10 still passes**, which is the model-versus-shell asymmetry the case exists for. Inversion 14 as specified (`state->project_dirty = session.dirty()` moved above the failure check in `save_project_file`) **also passed**: on a failed save the session's own dirty flag is still true, so mirroring it early changes nothing. Replaced with **14'** — `EditorSession::save` marking the session clean before checking the save result, which is precisely bug 1's shipped consequence — and it fails `C3: a failed save must leave the session dirty`
- **S5's specified mechanism was measured wrong and the case was rewritten.** The design and plan specified three `dirty()` assertions as the catch for a snapshot rebased without re-serializing. Measured: with the re-serialization removed, **the entire project smoke still passed**. `update_dirty` re-serializes the LIVE project on every call and never reads a snapshot's cached string, and `push_history`'s merge collapse — the other consumer — is unreachable because a Save As sets `allow_merge = false` on every entry. The one reachable consumer is `apply_history`, which derives `project_changed` from the two cached strings and bumps `project_revision` at `session.cpp:1671`. S5 now asserts that an undo/redo of a **preview-only** edit after a Save As bumps no `project_revision`, and inversion 6 fails on exactly that clause. The three `dirty()` assertions are kept, because they do catch inversion 5
- **A pre-existing case turned out to be a second net.** `editor_project_smoke.cpp:1687`'s undo/redo dirty assertions began failing the moment `save_project` started rebasing, because a history snapshot rebased only in `source_path` never compares equal to the saved baseline again. It caught inversions 3 and 5 before S2/S4 were even reached, and both inversions had to be re-run with that gate temporarily bypassed to observe the MAR-180 cases directly. `:1681-1686` itself is unchanged
- Preference isolation proof: `$HOME/Library/Application Support/Marrow` did not exist before the work and does not exist after it, checked at Task 0, after the extraction, and after the final full run. Every `marrow_editor_shell` invocation set `MARROW_CONFIG_HOME` to a scratch directory. The rename seam is process-global and now shared by the settings writer and the project writer; every installation is RAII-scoped and no preference save happens inside any such scope, which is noted on the setter itself

## MAR-179 Complete Constraint Parameter Widgets Validation Results

Validated 2026-08-30. The model layer already carried every field end to end —
`IkConstraintEdit` and `PhysicsConstraintEdit` hold them, `project.cpp` parses
and serializes them, `skeleton_parse.cpp` reads them, the runtime consumes them,
and `player_idle.marrow` already stores all eleven. Even the agent's own
materializer copied `softness`/`compress`/`stretch` — values it then offered no
way to change. **MAR-179 is surface-only: no model, format or runtime change.**
`.mskl` v1, `.mbin` v2, the `.marrow` schema and C ABI v1 are untouched, with an
empty `git diff --name-only` over `skeleton_parse.cpp`, `binary.cpp`,
`include/marrow/**.h` and `CMakeLists.txt`, so `docs/root1/format-spec.md` is
byte-identical.

**The measured gap, confirmed against the tree before any code was written.**
GUI: IK had Bones/Target/Mix/Bend Positive and was missing softness, compress,
stretch; physics had Inertia/Damping/Strength/Mix/Gravity/Wind and was missing
step, x, y, rotate, scale_x, shear_x, limit, mass_inverse. Path and transform
were already complete and MAR-179 adds no widget to either. Agent: physics, path
and transform were complete through their `…Traits` specializations;
`edit_ik_constraint` is the one hand-written handler and parsed four arguments,
echoed a **three-key** dry run, and returned **no** `scene_delta` at all on the
live path. MCP: `edit_ik_constraint` was missing the three fields **and
`merge`** — a fourth, older parity gap, since the C++ handler has always read
`bool_arg(args, "merge")` and the other three schemas all declare it.

**The clamp hazard, and the two deliberate exceptions.** All ten pre-existing
constraint sliders were called with six arguments and no flags, so Ctrl+clicking
IK Mix and typing `5` set `mix = 5.0`; the runtime parse then rejected it and
`apply_coalesced_edit_frame` rolled the drag back with a status line. Nothing
corrupted, but the gesture aborted. The governing rule, stated once: **a
constraint widget's `[min,max]` IS the loader's bound, and only then may it carry
`ImGuiSliderFlags_AlwaysClamp`.** Seven sliders whose range already equalled the
loader's `[0,1]` gained the flag — IK Mix, path Position/Rotate Mix/Translate
Mix, and the four transform mixes through their one shared `update_mix` lambda —
as did the new physics Inertia and Mix. Two exceptions are deliberate and are
recorded here so a reader can see they were chosen, not overlooked:

- **Physics `Damping` and `Strength` were RE-FORMED, not clamped.** Their slider
  ceilings (10 and 50) were *narrower* than the loader's `>= 0`, so adding the
  flag would have refused values the format accepts and existing projects may
  already carry. They became `DragScalar [0, DBL_MAX]` magnitudes instead.
- **Path `Spacing` was left alone entirely.** Its `[0,1]` range is also narrower
  than the loader's `>= 0`, but its *correct* range depends on `spacing_mode` — a
  percentage in Percent mode, a distance in Length mode. Widening it without
  settling that is worse than a recorded, deliberate narrowness. It stays a known
  leftover for whatever story revisits path authoring.

Two widget forms carry all eleven: `SliderScalar [0,1] + AlwaysClamp` for a
bounded mix, and `DragScalar [lo, DBL_MAX] + AlwaysClamp` for a non-negative
magnitude. `DBL_MAX` is not an invented ceiling — with `p_min = 0` it makes
`AlwaysClamp` enforce exactly `>= 0`, which is exactly the loader's bound, and
`DragScalar` uses the range only for clamping. `step`'s minimum, `1e-4`, is
**derived from its `%.4f` format** as the smallest distinctly displayable
positive, not fitted.

| Area | Evidence | Result |
| --- | --- | --- |
| AC1 — the three IK widgets | `Softness` (Form B, lo = 0, speed 0.5, `%.2f`), `Compress` and `Stretch` checkboxes. All three located in a rendered frame by aiming a real mouse at the panel and matching `ImGuiContext::HoveredId` against `window->GetID(label)` | PASS |
| AC2 — the eight physics widgets | `Step`, `X##physics`, `Y##physics`, `Rotate##physics`, `Scale X##physics`, `Shear X##physics`, `Limit`, `Mass Inverse`, all Form B, all located the same way. The panel now runs in **struct order** — step, x, y, rotate, scaleX, shearX, limit, inertia, damping, strength, massInverse, gravity, wind, mix — matching `PhysicsConstraintData`, the serialized `.marrow` JSON and `PhysicsConstraintTraits::preview`, so a field added to one is visibly missing from the others. The only relocation is `Mix##physics`, fourth to last | PASS |
| AC3 — transactions, preview, round trip | Every scalar goes through the unchanged `apply_constraint_project_drag` → `apply_coalesced_edit_frame`, `allow_merge = false`, group `constraint:<kind>:<name>`, impact `Project\|Runtime\|Preview` so the viewport updates while the mouse is down. **Two successive** Softness drags and one Step drag each produced exactly one history entry and did not merge into the previous one; one drag alone could not have observed the merge flag, because there is no earlier entry in the group to merge into. Undo restored `serialize_project()` byte-for-byte | PASS |
| AC4 — IK agent parity | `edit_ik_constraint` keeps its hand-written body and borrows only the template's *shape*: merge first, then branch. Both branches now return the same nine keys (`dry_run, name, bones, target, mix, bend_positive, softness, compress, stretch`), and the live `scene_delta` is byte-identical to the dry run apart from `"dry_run"` — the invariant the other three families already held. The old three-key expectation was replaced, which *is* the assertion that the fields are exposed. `Compress`/`Stretch` dispatch `edit_ik_constraint` exactly as `Bend Positive` does, so **AC1 physically cannot ship without AC4** | PASS |
| AC4 — MCP | Four properties added to one existing tool: `softness`/`compress`/`stretch` plus `merge`. Asserted against the schema object itself, because `MarrowClient.send_command` writes JSON straight to the agent socket and never consults `inputSchema` — a wire-level call carrying `softness` succeeds with `editing.py` untouched, so a sequence test alone would have been a test that cannot fail | PASS |
| AC5 — boundary, rollback, save/reload, equivalence | Model layer S1–S4 plus shell C1–C5, agent and MCP. Boundaries `step = 1e-4`, `x`/`rotate`/`scale_x`/`limit`/`mass_inverse` `= 0`, `softness = 12.5`, `shear_x = 0.75`, `y = 2.5` all survive **save → LOAD → materialize**, asserted on the runtime struct, never on `ProjectData` | PASS |
| Refusal at all three layers | `step ∈ {0, -1}` and `mass_inverse = -1`: the runtime parse fails carrying `physics step must be greater than zero` / `physics massInverse must be non-negative`; `save_project` fails with `physics constraint edit numeric values must stay within their valid ranges` and **leaves no file behind**; and, for a hand-patched `.marrow`, the project parse — the layer that decides whether a file OPENS — refuses with `physics constraint edit step must be greater than zero` / `physics constraint edit massInverse must be non-negative`. The L2 leg was added after inversion 6 proved the first two cases could not see it | PASS |
| The `softness` asymmetry, asserted BOTH ways | The agent rejects a negative softness; the format accepts one at all three layers and the runtime reads it as zero (`std::max(0.0f, softness)` behind a `> 0` guard). So a `.marrow` carrying `softness = -3` still saves, still **opens**, materializes as `-3`, and survives a second round trip — while `edit_ik_constraint` reports `ik constraint softness must be non-negative.` and changes nothing. The guard is a SURFACE guard; tightening `project.cpp` would make existing projects unopenable, which MAR-179 refuses | PASS |
| Registry unchanged at 64, proved BY DIFF | No `kOperationSpecs` row added. `awk` over `agent_dispatch.cpp` → **64**, split **edit 39 / inspection 12 / management 10 / validation 3**, identical before and after. `git diff -U0 \| grep -E '^[+-].*\b(62\|63\|64\|65)\b'` over the code and config returns exactly **two** lines — the new scenario's guard message and one comment — plus, in prose, this section and the one `editing-gap-analysis.md` sentence, both of which quote 64 and still quote 64. No other count site appears, so `AGENTS.md`'s `t = 0.62` and "62 lines", the theme's `(51, 56, 64)` and `rgb(54,57,64)`, and `test_client.py`'s mesh coordinates and 62nd-operation ordinal are all untouched. Code guards went 9 → 10 (the one new scenario), every literal still `64`, `std::array<OperationExpectation, 64>` and both `== 64` python assertions unmoved | PASS |
| Carried in from MAR-178: the catalog buttons are real (C6) | MAR-178 asserted `Rename...`/`Delete...` by calling `request_/confirm_/cancel_constraint_*` directly, so its tests would have passed with `draw_constraint_catalog_buttons()` deleted. The deferral was re-homed here and **closed**: both buttons are now located by a real mouse through `HoveredId` (C1) and clicked, and their modals' own `Delete` and `Cancel` buttons are clicked in turn — the whole round trip is mouse-driven and no `confirm_*` helper is called in C6 at all. The delete leg applies through the modal's real button as exactly one history entry and undoes byte-for-byte; the rename leg cancels and changes nothing. Rename's leg stops at Cancel deliberately: the modal seeds its `InputText` with the source name and the primitive refuses a same-name rename, so a pure-mouse rename would have to type — and the rename itself is already covered UI-free by MAR-178. Locating `Delete...` also forced a third sweep column, because it sits on a `SameLine()` to the right of `Rename...` and no left-edge column reaches it | PASS |
| Compatibility | `.mskl` v1, `.mbin` v2, C ABI v1 unchanged; `git diff --name-only \| grep -E 'skeleton_parse\.cpp\|binary\.cpp\|include/marrow/.*\.h$\|CMakeLists\.txt'` → **no output**. No `.marrow` field, no fixture change, no new agent operation. The agent wire is additive: three new optional arguments, and `edit_ik_constraint`'s `scene_delta` grows from three keys to nine and gains a live payload | PASS |

**The real-mouse decision, and why a UI-free helper was the wrong tool here.**
MAR-178 asserted its modals through the UI-free helpers the buttons call, which
was right for MAR-178 — its interesting behaviour lived in
`apply_constraint_catalog_edit()`. It is wrong for MAR-179, whose entire
deliverable *is* "the widget is on the screen". A headless test that sets
`edit.softness = 12.5` directly proves the model already worked before this story
started; it is precisely the test that cannot fail, and it would keep passing
after the widget was deleted. `validate_constraint_parameter_shell_smoke()`
therefore renders real frames and sweeps a real mouse down the Constraints
window, comparing `ImGuiContext::HoveredId` against `window->GetID(label)` —
production already reads the context this way (`shell_core.cpp` uses
`GetActiveID()` and `ActiveIdIsAlive`). **A label not found is a failure, never a
skip.** `Mix`, `Bend Positive` and `Inertia` shipped before MAR-179 and are kept
as **positive controls**, so C1 cannot degrade into a case that can only ever
fail: if the id seed were wrong or the sweep never reached the rows, the controls
would go missing too and say so by name. The same mechanism is what let MAR-178's
`Rename...`/`Delete...` deferral be closed here rather than deferred again to the
platform stories — sixteen widgets are swept in total.

**Document errors found (six), all in MAR-179's own governing documents.**

- **`.mbin` v2 stores every number as float32, and both documents say the
  opposite.** Design §6.1-S4 asks for the eleven fields to "agree" between the
  `.mskl` and `.mbin` exports, and §8 records `.mbin` v2 as an unchanged "generic
  document encoder". The encoder is generic, but `binary.cpp`'s `append_float32`
  narrows *every* JSON number, so the two exports cannot agree to double
  precision and never could: authored `step = 1e-4` was measured as
  `0.0001` vs `9.9999997473787516e-05`. S4 asserts agreement **after the same
  narrowing**. This also supplies a reason for `kMinPhysicsStep` neither document
  states: `1e-4` survives float32 as a strictly positive number, so an exported
  `.mbin` authored at the widget's minimum still satisfies the loader's
  `step > 0`. A smaller minimum could round to `0.0f` and make the **binary**
  export unopenable while the `.mskl` stayed fine.
- **Inversion 6 as specified cannot bite.** The plan asks for
  `project.cpp`'s `edit.step <= 0.0` → `< 0.0` and predicts S2's first case
  fails. But S2 as designed asserts `build_project_runtime` (L1) and
  `save_project` (L3); the inverted line is L2, which runs only on
  `load_project`, and nothing in S2 reloaded a bad file. The inversion was run
  and **S2 still passed**. The fix was to strengthen the test rather than weaken
  the gate: S2 now also patches the saved JSON and reopens it, and the inversion
  then fails with the L1 message surfacing where the L2 one belongs.
- **Inversion 3 as specified could not bite against the case as first written.**
  It requires a *second* drag in the same group; a single drag produces one entry
  whether `allow_merge` is true or false. C2 was rewritten to two successive
  drags before the inversion was run.
- **The plan's Task 4 table lists seven `SliderScalar` call sites for
  path/transform; there are four.** The four transform mixes share one local
  `update_mix` lambda, so `:1621`/`:1628`/`:1635`/`:1642` are its *invocation*
  lines, not `SliderScalar` calls. Seven widgets, four edits.
- **`Mix##physics` is at `:1904`, not `:1903`.** Cosmetic; recorded because the
  plan's Task 0 step 9 asks for the line to be confirmed.
- **The plan's checklist MCP command is wrong.**
  `test_client.py --parameter-only assets/fixtures/parameter_face_basic.marrow`
  exits 2 with `unrecognized arguments`; `--parameter-only` takes no positional.
  The correct run is `--parameter-only` against a shell already serving that
  fixture, and it passes.

**Two harness findings worth recording, because a naive version of either
produces a silently wrong test.**

- **On macOS, `io.AddKeyEvent(ImGuiMod_Ctrl, true)` does not press Ctrl.**
  `ConfigMacOSXBehaviors` is on by default and `AddKeyAnalogEvent` swaps
  Cmd and Ctrl at the event layer, so the flag raises `io.KeySuper`; then
  `AddMouseButtonEvent` converts the left press into a **right** click. Measured
  directly: `keyctrl=0 super=1 mdown=0 mdown1=1 active=0`. C5 clears
  `ConfigMacOSXBehaviors` for the gesture and restores it, and asserts
  `TempInputId` before typing so a future regression reports "Ctrl+click did not
  turn Inertia into a text input" rather than a bare wrong value.
- **Two presses at the same pixel are a double click, and a double-clicked
  `DragScalar` becomes a text input instead of dragging.** C2's second Softness
  drag left the widget active as a temp input with the value unchanged at 20. The
  scenario now advances the simulated clock past `io.MouseDoubleClickTime`
  between gestures.

**Not covered, deliberately.** `constraints.list` is not widened: it reports only
`type`/`name`/`bones`/`target`/`slot`/`source` for **all four** families, so
widening it is a change to an inspection payload the dispatch smoke pins
byte-exactly, it benefits all four families equally, and it is not what this
story asks for. The read-back channel for parameter values is
`edit_*_constraint` with `dry_run: true`, which is what `test_client.py` uses.
The IK 1/2-bone radios are unchanged. `edit_ik_constraint` was **not** converted
to `handle_constraint_edit<IkConstraintTraits>`, and inversion 10 is the
measurement behind that decision rather than an assertion of taste.

Current validation:

- Task 0 measured every claim before any code. Registry **64** with split 39/12/10/3; 9 code guards + 9 messages + 2 python assertions + 1 array size, all at 64 (corrected under MAR-180, which re-measured 9 guards at `f2f2612^` and 10 at `f2f2612`); no `Softness`/`Compress`/`Stretch`/`Step`/`Limit`/`Mass Inverse` anywhere in `shell_constraints.cpp`; `softness` present in `agent_handlers_constraints.cpp` **only** in the materializer; `softness` a plain read with no range check in `skeleton_parse.cpp`, `project.cpp` and `validate_project_for_save`; `build_project_runtime(*project_ptr, …)` still at `project.cpp:7495` inside `load_project`, so a reload is a full materialization; the fixture's originals recorded (ik `softness 0, compress false, stretch false`; physics `step 0.0166666667, x 1, y 1, rotate 1, scaleX 0.35, shearX 0, limit 30, massInverse 1`); `Mix##physics` asserted by nothing
- **Task 0 step 5, the assumption C1 rests on, confirmed both statically and empirically.** `widgets::seg_toggle` has a balanced `PushID`/`PopID`, there is no `BeginTabBar`/`BeginTabItem` anywhere in `shell_constraints.cpp`, and the four `BeginChild` constraint lists are all closed with `EndChild` **before** the parameter widgets — so every parameter widget is emitted at plain window scope and `window->GetID(label)` is its id. Then measured: a sweep seeded that way found `Mix` and `Bend Positive` while reporting `Softness`, `Compress` and `Stretch` absent, on the tree *before* the widgets existed
- `cmake -S . -B build && cmake --build build -j` -> built with **zero** new warnings; `cmake --build build --target marrow_constraint_warning_check` -> passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed, reporting the MAR-179 model-layer line for S1–S4; `--create` -> passed; `--export-runtime` + `--export-binary` -> passed, and `./build/marrow_inspect --compare` on that pair -> `matches`
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` over **408** `[ OK ]` cases (up from 404) against the exact **64**-operation registry, with `edit_ik_constraint invalid target rollback` still passing **unchanged**
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> `Headless editor shell smoke rendered 2 frame(s).`, including the new `validate_constraint_parameter_shell_smoke` scenario: *"a real mouse found all 16 widgets by HoveredId (three of them positive controls that shipped before MAR-179, two of them MAR-178's catalog buttons), two successive Softness drags and a Step drag each coalesced into exactly one history entry without merging into the previous one, the Compress and Stretch checkboxes reached edit_ik_constraint and survived undo/redo, Ctrl+click-typing 5 into Inertia committed the clamped 1.0, and a real click on Delete.../Rename... opened their modals whose own Delete and Cancel buttons were then clicked too -- the round trip MAR-178's UI-free tests could not make."*; `--project assets/fixtures/parameter_face_basic.marrow --auto-close 2` -> passed
- **C5 took its real form, not the fallback.** Design §6.2 allowed substituting a project-layer assertion if the Ctrl+click text path could not be driven headlessly. It could be, once the macOS modifier swap above was accounted for, so the claim stands at full strength: *the widget clamps*, not merely *an unclamped value would be rejected*
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` with `tools/mcp/venv/bin/python tools/mcp/test_client.py` -> `mcp test_client: PASSED` with **64/64** exact C++/Python name parity unchanged, a schema-level assertion on all four new `edit_ik_constraint` properties, and a dry-run -> live -> read-back -> undo -> read-back sequence reporting *"the live scene_delta matches the dry run apart from 'dry_run'; a negative softness reports 'ik constraint softness must be non-negative.'"*; `--parameter-only` against a shell serving `parameter_face_basic.marrow` -> `mcp parameter test_client: PASSED`; `python -m py_compile` over all four MCP files -> clean
- **Twelve inversions run, each failing the case it should, each restored.** 1 `Softness` widget deleted -> `C1 (IK): the panel never emitted a widget with the id of "Softness"`. 2 `Step` deleted -> the same message for `Step` under `C1 (physics)`, proving C1 is not IK-specific. 3 `allow_merge = true` on Softness -> `C2: Softness drag 2 must coalesce into EXACTLY one history entry and must NOT merge into the previous drag; undo_count 0 -> 1, expected 2`. 4 the Step drag never reaches `finalize_coalesced_edit` -> `C3: a multi-frame Step drag must coalesce into EXACTLY one history entry; undo_count 0 -> 0`. 5 `kClamp` removed from Inertia -> `C5: Ctrl+click-typing 5 into Inertia must commit the CLAMPED 1.0, not 0.85` — the rolled-back value, distinguishable from both 5.0 and 1.0. 6 `project.cpp`'s `edit.step <= 0.0` -> `< 0.0` (**as specified, does not bite** — see the document errors; after S2 gained its L2 leg) -> `S2 (L2, "step": 0.0): expected the project parse to carry "physics constraint edit step must be greater than zero", measured "…player_idle.mskl:1:1: $.physics[0].step: physics step must be greater than zero"`, i.e. the deeper layer catching what L2 should have. 7 a `softness < 0` check added to `project.cpp` -> `S3: a `.marrow` carrying softness = -3 must still OPEN. It no longer does, which is exactly the backward compatibility break MAR-179 refuses to take`. 8 `softness` removed from `ik_constraint_preview` -> three `scene_delta wire shape changed` failures naming the missing key in both payloads. 9 the `merged.softness < 0.0` guard removed -> `edit_ik_constraint negative softness: expected ok=false` and the read-back shows the project carrying `"softness":-1`. 10 the template's **pre-transaction** `validate_bone_names` added to the IK handler -> `edit_ik_constraint invalid target rollback: commit-time failure prefix changed`, which is the measurement behind the decision not to convert IK to the traits template: the check returns before the transaction, so the `"Failed to apply IK constraint edit: "` prefix never appears and the suite's only constraint commit-time rollback case is deleted. 11 `merge` removed from `editing.py` -> `AssertionError: edit_ik_constraint's MCP schema is missing 'merge'` while `assert len(mcp_names) == 64` still passed, proving the check is about the property and not the tool count. **12, for the carried-in MAR-178 coverage**: `draw_constraint_catalog_buttons()`'s body deleted outright -> `C1 (IK): "Constraints" never emitted a widget with the id of "Rename..."` and the same for `"Delete..."` — and, decisively, **MAR-178's own lifecycle scenario still printed its full success line in that same run**, which is precisely the gap the re-homed deferral names
- `ctest --test-dir build --output-on-failure` -> `100% tests passed, 0 tests failed out of 22`; no target added, no `CMakeLists.txt` change (the new scenario lives in the already-listed `shell_smoke_constraints.cpp`)
- `./build/marrow_unit_tests`, `./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl`, `./build/marrow_c_smoke`, `./build/marrow_parameter_project_smoke` -> passed; all four constraint `.mskl` fixtures still parse
- **Pre-existing failure, not a MAR-179 regression**: `./build/marrow_project_smoke assets/fixtures/atlas_pack_smoke/atlas_pack_project.marrow --export-runtime …` (recorded above as a Current Validation command) exits 1 with `Viewport validation expected the fixture debug overlay toggles to be enabled.` That fixture carries no `viewport` block. It fails identically with MAR-179's `editor_project_smoke.cpp` changes stashed, and MAR-179's diff to that file is purely additive and touches nothing in `validate_viewport_settings`
- Preference isolation proof: `$HOME/Library/Application Support/Marrow` did **not** exist before the run and still did not exist after it; the new scenario installs `ScopedPreferenceIsolation("constraint-parameters")` as its first statement and refuses to run if it cannot; no scratch file was left in the shared scratchpad
- **Carried, unrelated to MAR-179**, folded in to avoid a concurrent edit in files this story already touches: `validate_project_for_save(const ProjectData&, ProjectSaveError*)` is at `project.cpp:5503`, not `:5502` (corrected in this file and in the MAR-178 design); this file's self-referential citation of where its own protected `62` literals live was re-measured; `editor_project_smoke.cpp`'s gate-2 message said "a rename of a name absent from the family" when the strengthened test rejects on a **collision**; and the MAR-178 plan's save→reload checklist line was narrowed to match §10.5's actual carve-out rather than adding reloads to satisfy a broader reading. MAR-178's own `404 [ OK ]` figure is left as measured at MAR-178; this story's build produces **408** and that is what is recorded above

## MAR-178 Constraint Rename and Delete Surfaces Validation Results

Validated 2026-08-30. MAR-177 built `rename_constraint()` and
`delete_constraint()` and proved they cannot leave an unsavable or unopenable
project. **Nothing called them.** MAR-178 is the surface layer: four
`Rename... / Delete...` rows with a collision preview and a skin-reference
preview, one UI-free undoable command, the `SelectionSet` cascade MAR-177
explicitly deferred, and two agent/MCP operations. It adds **no** `.marrow`
field, **no** runtime behaviour and **no** new numeric constant, so
`docs/root1/format-spec.md` is byte-identical.

**The selection design rests on two facts that were measured, not assumed.**
`rebuild_project_runtime()` (`shell_core.cpp:498-533`) does **not** call
`reconcile_selection_to_runtime()` — only `reload_project()`
(`shell_core.cpp:629`) and the runtime asset watch (`shell_asset_watch.cpp:189`)
do. So an ordinary edit reconciles nothing: a rename without an explicit remap
silently loses the selection, and a delete leaves a ghost that inflates the
user-visible *"; N selected"* count (`shell_selection.cpp:241`). And `prune`
(`selection.cpp:155-159`) and `remap` (`:195-199`) choose the **same**
last-survivor active fallback, so a delete's remap and a later reconcile cannot
disagree. Rename remaps the identity, delete remaps to `nullopt`, the cascade
runs **only after a successful commit**, and no neighbour is auto-selected.

**Undo and redo deliberately do not restore the selection.**
`EditorHistorySnapshot` carries no `SelectionSet` and `history_snapshots_equal()`
compares three fields, so adding one reproduces MAR-174's Ctrl+Z bounce. Instead
each path gains one call to a new narrow `reconcile_constraint_selection()` that
prunes only stale `ConstraintSelection`s. Widening it to
`reconcile_selection_to_runtime()` would prune bone, slot and attachment
selections after **every** undo in the editor; that inversion is gated below.

| Area | Evidence | Result |
| --- | --- | --- |
| One transaction per accepted edit, and a rejection changes nothing | `apply_constraint_catalog_edit()` guards, summarises, transacts, commits, then cascades — in that order. Scenario A asserts `undo_count()` **+1** per accepted edit; scenario D runs **6** rejections (taken name, unchanged target, empty source, missing rename source, missing delete source, right name in the wrong family) and asserts after each that `serialize_project()` is the **identical string** and `undo_count()` is unchanged | PASS |
| Ownership, on all three live rows | Project-only → `!used_operation && changed_upsert`, `ownership == "project"`, `constraint_lifecycle_operations` stays **empty** (A). Base-backed → `used_operation && !changed_upsert`, `"base"` (B1/B2). Shadowing → **both** flags, `"shadowed"`, and the base does not resurrect (B3). Inherited by construction, because the command calls MAR-177's primitives and never writes a record itself | PASS |
| The affected-skin summary is exhaustive, and captured pre-mutation | `constraint_affected_skins()` reads `SkinData::<family>_constraint_indices` from the live parsed runtime **before** the transaction, because the skin arrays no longer name the constraint afterwards. Reports `["cape"]` for `cape_pull` and `[]` for every `player_idle` constraint. Skins are the **complete** referential-integrity surface, established by enumerating `parse_animations`'s member keys (`animations, attachment, bones, color, deform, default, drawOrder, events, inherit, rotate, scale, shear, slots, translate` — no constraint family among them) rather than by grepping, which upgrades the preview from best-effort to exhaustive. Proved by inversion: moving the capture after `commit()` makes B1 fail with *"the affected-skin summary must be captured from the PRE-mutation runtime and report exactly [cape]; measured 0 entries."* Restored | PASS |
| The cascade runs only after a successful commit | Proved by inversion: moving the `remap` to before the primitive call makes project scenario C fail with *"a rejected edit must leave the selection, the history, and the byte serialization exactly as they were."* Restored. The rejection deliberately names the **selected** identity — a rejection of some other constraint could not detect the ordering at all, and the first version of this test could not | PASS |
| Rename follows, delete prunes | Shell case 2 asserts `active_constraint()->constraint_name` is the new name with `items().size()` unchanged; case 3 asserts the count drops by one with the co-selected bone intact and order preserved; project scenario C asserts the new active member after a delete is the **last** survivor. Proved by inversion twice: dropping the rename remap fails case 2 (*"the selection did not follow the rename"*), dropping the delete remap fails case 3 (*"the deleted constraint left a ghost … measured 2 against 2"*). Restored | PASS |
| The narrow reconcile is narrow, and load-bearing | Shell case 4 renames a selected constraint, undoes, and asserts the ghost is gone **and** that a co-selected `phantom_bone` — a bone selection that does not resolve — survives. Proved by inversion: swapping in `reconcile_selection_to_runtime()` fails with *"the undo-path reconcile pruned a bone selection … which is a behaviour change far outside this story."* Restored | PASS |
| Confirmation begins no transaction | Shell case 1 requests a delete, cancels, and asserts `undo_count()` and `serialize_project()` are unchanged, then that a confirm after a cancel refuses the abandoned request. Proved by inversion: making cancel fall through to confirm fails with *"cancelling the delete confirmation must begin no transaction; undo_count 0 -> 1."* Restored | PASS |
| Both surfaces reject identically | These operations are **single-target** — `(family, name)` names exactly one constraint — so the batch skip-vs-reject asymmetry does not apply and both surfaces reject with the same message from the same primitive. The GUI's only differences are presentational: it pre-disables Apply on a name it can already see is taken, and keeps the modal open on a rejection | PASS |
| The dry run runs the live preflight | It copies the project and runs the **same** primitive, and one `catalog_delta()` builder produces both payloads. The agent smoke asserts the dry-run and live `scene_delta` are byte-identical apart from `"dry_run"`, and that a rejection's `message` is byte-identical between them. Proved by inversion: substituting a hand-written `constraint_exists`-style check fails with *"a hand-written dry-run check drifts from the primitive's message; measured dry='Constraint is not renameable.' live='ik constraint rename target 'editor_arm_reach' must differ from its source'"*. Restored | PASS |
| Save → reload survival; a successful save is never the assertion (§10.5) | `validate_project_for_save(const ProjectData&, ProjectSaveError*)` (`project.cpp:5503`) takes **no base document** and so structurally cannot resolve a skin reference against a skeleton — a passing `save()` proves nothing about the unopenable-project failure. `load_project(path)` → `load_project(Document)` → `build_project_runtime()` (`project.cpp:7495`) is what re-materializes and calls `load_skeleton_data`, verified as a Task 0 hard stop. **Every** delete branch therefore ends in a reload: project scenario A saves and `load_project()`s after its delete; B1/B2/B3 each do the same; shell case 8 does `save_project_file()` → `reload_project()` and asserts the reload succeeded with zero transform constraints and no `skins.cape` transform indices; F1/F2 re-parse the export through `load_skeleton_document()` + `load_skeleton_data()`. Shell case 5's four-family deletes assert on the rebuilt runtime, which §10.5 explicitly allows as the same code the reload runs. Demonstrated empirically: a `.marrow` carrying every required member and a valid atlas — i.e. one every save-side check accepts — that references a root-only-renamed skeleton fails to open with `$.skins.cape.transform[0]: skin references unknown transform constraint 'cape_pull'` | PASS |
| Export, over the command path | After a command-path rename `cape_pull → cape_drag`, the exported `.mskl` names `cape_pull` **0** times and `cape_drag` exactly **2** (root `transform[0].name` and `skins.cape.transform[0]`), and the reloaded skin `cape` resolves one transform-constraint index pointing at `cape_drag`. After a command-path delete, the export carries **no** root `transform` key and **no** `skins.cape.transform` key, reloads with zero transform constraints, and `skins.cape` still holds `cape_target`. The `2` was re-counted from the export, not carried from MAR-177's spec | PASS |
| No last-constraint gate | A family may legitimately be empty — which is why an emptied family array erases its key. Shell case 5 renames **and** deletes one constraint in each of IK, path, transform and physics from `player_idle`, driving each family to empty, and asserts the other three families' constraints survive each time | PASS |
| `unique_constraint_name()` is not on the rename path | It is the **create** path's allocator. A collision is refused, never auto-suffixed, because quietly giving the user a different name than they typed is worse than declining. The rename modal seeds with the current name and previews the collision instead | PASS |
| The atlas gate stays blunt; MAR-178 owns only the message (§4.2) | The gate is unchanged and `constraint_catalog.cpp` adds no narrower check. `load_project()` refuses an atlas-free **document** outright with `$.runtime.atlases: array must not be empty`, so no project on disk is in this state; the only way in is emptying the list in memory, and scenario E and shell case 9 both do exactly that and assert the refusal changes neither `serialize_project()` nor `undo_count()`. The command passes the validator's sentence through **verbatim** — a surface that paraphrases a validator drifts from it — and the GUI adds a clause in front, matched on the **message text** rather than on a re-derived atlas check so it cannot fire on a state the validator would have accepted. Rendered: `Cannot rename: the project must reference at least one atlas before it can be saved. at least one atlas path is required`. Both modal buttons stay enabled, because the user's next action is fixing the atlas list, not retyping the name. The agent codes it `"invalid_project"`, never `"not_found"` — a caller retrying a `not_found` re-sends a different name and fails identically forever | PASS |
| The family spelling cannot drift | `constraint_family_key()` is declared in the new header rather than exporting `project.cpp`'s file-local `constraint_family_json_key()`. Scenario G closes the duplication with a test rather than by construction: for each of the four families it appends a lifecycle record, serializes, and asserts the emitted `"family"` string equals `constraint_family_key(family)`; it also asserts `parse_constraint_family` rejects `"bone"`, `"IK"`, `""` and `"transforms"` | PASS |
| Registry 62 → 64 | `kOperationSpecs` → **64** (inspection 12, validation 3, management 10, **edit 39**), with `constraint.rename` and `constraint.delete` immediately after the four `edit_*_constraint` rows as (`edit`, mutating, not review, dry-run supported, project required). The sweep ran under a patch that asserts the before-count is 62 and the after-count is 64 and aborts otherwise: **16** code sites moved (7 comparisons + 7 messages in `shell_smoke_graph.cpp`, 1 + 1 in `shell_smoke_timeline.cpp`), plus `agent_dispatch_smoke.cpp`'s array size and 2 rows, 2 new `types.Tool` in `tools/mcp/tools/editing.py`, and `test_client.py`'s 2 assertions. The sweep greps for `62`, never `64`, because `(51, 56, 64)` and `rgb(54,57,64)` contain the new number; all 7 protected literals are untouched in the diff | PASS |
| Compatibility | `.mskl` v1, `.mbin` v2, C ABI v1 and `editor-settings.json` v1 unchanged, with a zero-byte `git diff` on `src/runtime/**`, `include/marrow/runtime/**`, `include/marrow/marrow_c.h`, `src/c_api/**`, `src/editor/preferences.cpp` and `include/marrow/editor/preferences.hpp`. Zero-byte diff on `src/editor/project.cpp`, `include/marrow/editor/project.hpp`, `src/editor/session.cpp`, `include/marrow/editor/session.hpp`, `src/editor/selection.cpp`, `include/marrow/editor/selection.hpp` — MAR-178 calls MAR-177's primitives and changes none of them. `.marrow` gains no field, so `docs/root1/format-spec.md` is byte-identical. `ProjectData` gains no member and no new tunable numeric constant ships | PASS |

Errors found in this story's own governing documents, all corrected here:

- **The design's §8.6 claim that "a blind `62` → `64` substitution is safe" is
  false.** This file contains `t = 0.62`, a timeline time in the MAR-170
  shell-smoke checkpoint (`AGENTS.md:1351` as of MAR-179), and "62 lines", a
  line count in the MAR-175 compatibility row (`:1005` as of MAR-179) — both match
  a `\b62\b` grep and both would be corrupted. Both line numbers are
  self-referential and shift whenever this file grows, so they are anchored to
  the sections that own them above; re-measure rather than trust them. The claim
  is true only of the three narrow patterns the counting patch actually matches
  (`!= 62U`,
  `exact 62-operation registry`, `== 62`), which is what shipped; the general
  statement is wrong and the protected-literal list should carry these two.
- **Task 0's prose grep omits `docs/root1/discription.md`.** It has two `62`
  hits (`:56`, `:57`), both correctly historical, but the plan's classification
  step never inspected the file it then tells you to append to.
- **`tools/mcp/test_client.py:1484` says "the 62nd operation"** and is in neither
  the live nor the historical list. It is an ordinal, not a total, so it stays —
  but the enumeration claimed to be exhaustive and was not.
- **Inverted gate 8 as specified cannot bite, and the design says why without
  noticing.** It asks for the `tx.cancel()` on a primitive rejection to be
  removed. But `EditTransaction`'s destructor already cancels, and the primitives
  are preflight-then-mutate, so an explicit `commit()` on a rejection finds
  nothing changed and creates no history entry — the assertion passes either way.
  §5.3 itself states `cancel()` is there "so that the failure path is visible in
  the code rather than in RAII", i.e. for visibility, not correctness. The
  property the gate was meant to protect was inverted instead by removing the
  `!applied.ok` early return entirely, which fails three agent cases.
- **The plan's gate 9 recipe ("temporarily passing a project-only-looking family
  key") is not available**, because MAR-178 may not touch `project.cpp`. It was
  inverted at MAR-178's own layer instead — discarding the tombstone the
  primitive appended, keeping the upsert erase — which is exactly the
  "go around the primitives" mistake the plan's global constraint 2 forbids, and
  it fails B3 with *"the base constraint resurrected after the shadowing
  delete"*.
- **Minor drift**: `reconcile_selection_to_runtime()` is called at
  `shell_core.cpp:629`, not `:628`; `agent_dispatch_smoke.cpp`'s array size is at
  `:39`, not `:38`. `shell::constraint_kind_label()` lives in the shell target,
  which `marrow_editor` does not link, so the agent's history label uses the wire
  family key — the same spelling its payload reports — rather than §10's
  `constraint_kind_label()`.

Not independently covered: the agent's `"invalid_project"` code is implemented and
reviewed but not asserted by an automated case, because no agent operation can
empty a project's atlas list and the dispatch smoke therefore cannot reach an
atlas-free project; the message half of §4.2 is asserted at the project and
shell levels. Also, the ImGui modals are exercised through the same
UI-free helpers the buttons call (`request_/confirm_/cancel_constraint_*`), not
through synthesized mouse events, so the widget wiring itself — button placement,
`BeginDisabled` state, keyboard focus — is asserted by construction and by the
headless frame render, not by a click. A construction-based assertion cannot
observe a *missing* widget: these tests would have passed unchanged had
`draw_constraint_catalog_buttons()` been deleted outright. **That deferral was
re-homed from the MAR-192–MAR-210 qualification backlog to MAR-179 and is now
CLOSED there** — MAR-179 stands up a real-mouse frame smoke in the same file and
the same panel for exactly the same reason, so covering these two buttons cost
two probe entries and one case. `Rename...` and `Delete...` are now located by a
real mouse through `HoveredId`, clicked, and their modals' own `Delete` and
`Cancel` buttons clicked in turn. Demonstrated: with
`draw_constraint_catalog_buttons()`'s body deleted, **this MAR-178 scenario still
prints its full success line** while MAR-179's C1 fails naming both buttons.
MAR-192 through MAR-210 remain the qualification authority for everything else.

Current validation:

- Task 0's two added gates both pass. **4b (hard stop)**: `build_project_runtime(*project_ptr, …)` is at `project.cpp:7495`, reached from the path overload at `:7517-7526`, so a reload is a full materialization plus `load_skeleton_data`; had it moved, every delete assertion in Tasks 5 and 7 would have stopped proving anything and needed redesign before any code. **4c**: `parse_animations` (`skeleton_parse.cpp:5214-5500`) enumerates exactly `animations, attachment, bones, color, deform, default, drawOrder, events, inherit, rotate, scale, shear, slots, translate` — no `ik`/`path`/`transform`/`physics` — so skins are the complete referrer set
- `cmake -S . -B build && cmake --build build` -> configured and built with **zero** new warnings
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed, reporting all seven MAR-178 scenarios: `Scenario A` (project-only rename/delete, one transaction each, save+reload, byte-exact undo), `Scenario B` (`base`/`shadowed` ownership, `[cape]` captured pre-mutation, every result saves AND reopens), `Scenario C` (rename remaps, delete promotes the last survivor, rejection moves nothing), `MAR-178 Scenario D: 6 rejections each leave the project's serialization byte-identical and the history untouched`, `Scenario E` (the atlas gate, refused with `at least one atlas path is required`), `Scenario F` (export: 0 x `cape_pull`, exactly 2 x `cape_drag`, and both exports RELOAD), `Scenario G` (family spelling anti-drift); `--create` -> passed
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` over **404** `[ OK ]` cases (up from 382) against the exact **64**-operation registry
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> `Headless editor shell smoke rendered 2 frame(s).`, including the new `validate_constraint_lifecycle_shell_smoke` scenario, which reported that a delete **saves and reloads** with `skins.cape` carrying no transform indices, that a refused rename says `transform constraint rename target 'cape_drag' must differ from its source`, and that an atlas-free rename says `Cannot rename: the project must reference at least one atlas before it can be saved. at least one atlas path is required`; `--project assets/fixtures/parameter_face_basic.marrow --auto-close 2` -> passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` with `tools/mcp/venv/bin/python tools/mcp/test_client.py` -> `mcp test_client: PASSED` with **64/64** exact C++/Python name parity, explicit registry-metadata rows for both new operations, and a dry-run -> live -> `constraints.list` read-back -> `export.preview`/`export_runtime` -> undo -> read-back sequence reporting `a refused rename reports 'ik constraint rename target 'editor_arm_reach' must differ from its source'`; `--parameter-only` against `parameter_face_basic.marrow` -> `mcp parameter test_client: PASSED`; `python -m py_compile` over all four MCP files -> clean
- **Eleven inversions run, each failing the case it should and each restored.** 1 post-commit skin capture -> B1 measures 0 entries. 2 cascade before commit -> C's rejection assertion fails. 3 widened reconcile -> shell case 4's `phantom_bone` is pruned. 4 no rename remap -> shell case 2's active name is wrong. 5 no delete remap -> shell case 3 measures 2 against 2. 6 cancel falls through to confirm -> `undo_count 0 -> 1`. 7 hand-written dry-run check -> message parity fails. 8 (**as specified, does not bite** — see the document errors above) the `!applied.ok` early return removed instead -> three agent cases fail. 9 tombstone discarded on the shadowing row -> B3's base resurrects. 10 one `types.Tool` removed -> `assert len(mcp_names) == 64 -- measured 63`, restored. 11 root-only rename -> **1** occurrence instead of 2 and `$.skins.cape.transform[0]: skin references unknown transform constraint 'cape_pull'`. Two more for the revised requirements: 12 the GUI's surface clause dropped -> shell case 9 fails with *"the surface's clause must sit in FRONT of the validator's sentence … measured 'at least one atlas path is required'"*; 13 §10.5's premise shown empirically -> a `.marrow` with every required member and a valid atlas, referencing a root-only-renamed skeleton, is accepted by every save-side check and still fails to open
- `ctest --test-dir build --output-on-failure` -> `100% tests passed, 0 tests failed out of 22`; no target added
- `./build/marrow_unit_tests`, `./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl` -> passed; `./build/marrow_inspect --compare` on the exported `.mbin`/`.mskl` pair -> `matches`
- Preference isolation proof: `$HOME/Library/Application Support/Marrow` did **not** exist before the run and still did not exist after it; every shell invocation ran under an isolated `MARROW_CONFIG_HOME`, and no scratch file was left in the shared scratchpad

## MAR-177 Constraint Lifecycle Project Operations Validation Results

Validated 2026-08-30. The `.marrow` constraint overlay had exactly two verbs —
replace a root-array element whose `name` matches, and append when none does —
so it could say "this constraint now has these values" and "there is one more
constraint" and nothing else. **Creation already shipped** for all four
families; what was missing was rename and delete, and for a constraint that
lives in the base `.mskl` the project could not express either at all. MAR-177
adds one optional, default-absent, **ordered** `.marrow` member,
`constraint_edits.operations`, and the model layer that materializes and
validates it. It adds no GUI, no agent operation and no registry entry.

**Skins reference constraints by name, and an unresolvable name is a hard LOAD
failure.** `parse_skin_scope_members()` fails the whole parse on one bad name,
so a delete that does not prune `skins[*].<family>` does not produce a subtly
wrong rig — it produces a project that still **saves** and can never be
**opened** again. MAR-172 shipped an `ok: true` that destroyed an adjacent key;
MAR-175 could commit and leave a project unsavable; this one would leave it
unopenable. Proved by inversion, twice, with the failures recorded verbatim
below. The sibling of the same rule is that a delete which empties a family
array must **erase the key**, not leave `[]`, because the runtime rejects an
empty family array outright.

**The ownership rule ships as code, in one place, and its middle row is the one
that is easy to get wrong.** Base-backed → append a record; project-only →
rewrite the upsert directly and append nothing; an upsert that *shadows* a base
constraint → **both**, because erasing only the upsert resurrects the base
element and reads to the user as "delete did nothing".

| Area | Evidence | Result |
| --- | --- | --- |
| The gap is rename and delete, not create | All four families already have an Add button and a defaults builder in `shell_constraints.cpp`, each allocating through `unique_constraint_name()`. Measured rather than assumed: a project carrying one default of each family (IK `ik_upper`/`ik_lower`→`ik_target`, path `guide` + `path_a`/`path_b`/`path_c`, transform `transform_source`→`transform_target`, physics `ribbon_01`/`ribbon_02`) was built and `save_project()` accepted all four. MAR-177 therefore adds no create path and specifies no defaults | PASS |
| Order is load-bearing, and asserted through materialization | Scenario E runs a chain (`A→arm_middle`, `arm_middle→arm_final`, `delete arm_final`), a three-step swap, and a delete-then-reuse against `ik_constraints.mskl`, and asserts the materialized **and the loaded** `ik_constraints()` name sequence, not a count. Proved by inversion: applying the records back-to-front makes the chain materialize `[arm_middle, arm_negative, …]` instead of `[arm_negative, …]`; restored | PASS |
| The skin reference is rewritten, and the failure is reachable | Scenario C2 asserts root `transform[0].name` **and** `skins.cape.transform` after a rename. Proved by inversion: deleting the skin-rewrite half makes `build_project_runtime()` fail with `assets/fixtures/skin_inherit_constraints.mskl:56:9: $.skins.cape.transform[0]: skin references unknown transform constraint 'cape_pull'` — the unopenable-project failure, verbatim. Restored | PASS |
| An emptied family key is erased, not left `[]` | Scenario C3 asserts the materialized document has no root `transform` key and no `skins.cape.transform` key after the last delete in the family, and that the runtime still loads. Proved by inversion: keeping `[]` makes it fail with `$.transform: transform constraints must not be empty when provided`. Restored | PASS |
| Save → reload survival, not return codes | Every accepted path is followed by `save_project()` **and** `load_project()`, and `load_project()` materializes, so a reload is a real proof the project still opens. Scenario A4 saves a project whose *only* constraint content is one tombstone and asserts the reloaded skeleton carries zero transform constraints; scenario B4 does the same for a base-backed rename; scenario E2 for an ordered pair | PASS |
| The tombstone-only emit gate, proved by inversion | `build_project_value()` gates the whole `constraint_edits` key on the four upsert vectors being non-empty. Reverting the added `\|\| !project.constraint_lifecycle_operations.empty()` clause makes scenario A4 fail with *"a tombstone-only project lost its `constraint_edits.operations` on serialization — the emit gate still keys on the four upsert vectors alone."* — the MAR-172 failure shape exactly. Restored | PASS |
| The shadowing row emits both halves | Scenario B5 upserts `cape_pull` over the base constraint of the same name, deletes it, and asserts a tombstone **and** an erased upsert, then that `cape_pull` occurs **0** times in the materialized document and the rig loads with zero transform constraints — i.e. the base did not resurrect | PASS |
| Validation splits at the seam `validate_project_for_save` already has | Save time has no base document, so it replays symbolically over per-family `consumed`/`introduced` sorted vectors: 14 rows, of which four legal ones (chain, reuse, swap, cross-family independence) are **accepted**, so the validator is not merely refusing everything. Materialization time has the base and reports four causes with distinct messages across 7 rows | PASS |
| Every rejection is atomic | `serialize_project()` is captured before and compared after every rejected call and every rejected materialization, on the string, not inferred from a return code. Each rejected `export_runtime_assets()` is additionally asserted to have written **no** file at either output path | PASS |
| The primitives cannot leave an unsavable or unopenable project | Both run `validate_project_for_save()` **and** `validate_constraint_lifecycle_operations()` on the candidate before committing. Step 5 is not redundant with the preflight, and the witness is real rather than fabricated: an atlas-free project passes the name preflight and `save_project()` refuses it, so removing the call makes scenario B6 fail with *"the primitive returned ok for a project that save_project() refuses"*. Restored | PASS |
| Export, on a project that was actually mutated | `cape_pull` (9 B) → `cape_pull_renamed` (17 B) over `skin_inherit_constraints.mskl`: `.mskl` **1611 → 1627 (+16)**, `.mbin` **605 → 613 (+8)**, string table unchanged at **39** entries with `cape_pull` out and `cape_pull_renamed` in. JSON counts **occurrences** (2 of them), MBIN counts **distinct strings** (1), so the `16 : 8` ratio *is* the occurrence count and a root-only rename would read `+8/+8` and fail loudly | PASS |
| Delete export, decoded rather than sized | The deleted name occurs **0** times; neither the root `transform` key nor `skins.cape.transform` survives as an empty array; the `.mbin` reloads through `load_skeleton_document()` + `load_skeleton_data()` with zero transform constraints and skin `cape` still holding `cape_target`; the string table drops **39 → 35** — exactly `[cape_pull, source, transform, translateMix]`, asserted by name | PASS |
| Malformed records are rejected on load, with a location | 17 cases: `operations` not an array; an element not an object; `op` absent / not a string / `"remove"`; `family` absent / `"bone"`; rename with `from` absent / empty, `to` absent / empty, `to == from`, or carrying `name`; delete with `name` absent / empty, or carrying `to` / `from`. Each asserts both the `$.constraint_edits.operations[i].<field>` path prefix and the message | PASS |
| Registry unchanged at 62 | MAR-177 adds no `kOperationSpecs` row. `awk` over `agent_dispatch.cpp` → **62**; 7 guards in `shell_smoke_graph.cpp`, 1 in `shell_smoke_timeline.cpp`, 2 in `tools/mcp/test_client.py`, 1 `std::array<OperationExpectation, 62>` in `agent_dispatch_smoke.cpp` — all unchanged, and `git diff` is empty on `agent_dispatch.cpp`, `agent_handlers_*`, `authoring.*` and `tools/mcp/**` | PASS |
| Compatibility | `.mskl` v1, `.mbin` v2, C ABI v1 and `editor-settings.json` v1 all unchanged, with a zero-byte `git diff` on `src/runtime/**`, `include/marrow/runtime/**`, `include/marrow/marrow_c.h`, `src/c_api/**`, `src/editor/preferences.cpp`, `include/marrow/editor/preferences.hpp` and every `src/editor/shell_*`. `.marrow` gains **exactly one** optional member, omitted when empty, so every existing project serializes byte-identically. MAR-177 introduces **no** new tunable numeric constant | PASS |

Errors found in this story's own governing documents, all corrected here:

- **The design's claim that `player_idle.marrow` re-serializes byte-identically
  to the file on disk is false, and always was.** `build_project_value()` emits
  `editor.timeline.fps` unconditionally while the fixture omits the default, a
  pre-existing **41-byte / 3-line** difference in a section MAR-177 does not
  touch. The assertion was replaced with the one that has teeth and actually
  states what AC4 needs: the serialized text differs from the fixture by that
  block **and nothing else**, plus a save → reload → serialize fixed point.
- **The design's §11.5 predicted the delete would drop the `.mbin` string table
  by exactly 1. It drops it by 4** — 39 → 35. The cause is the same interning
  that makes the rename cost `+8`: `collect_strings()` interns each distinct
  string once across the **whole** document, object keys included, so deleting a
  subtree removes every string that occurred only inside it. The four are
  `cape_pull` (the value), `transform` (the root array key *and* the skin scope
  key), `source` and `translateMix`; `name` and `bones` survive because bones and
  slots use them. Re-derived by decoding both tables rather than editing the
  constant, and the test now asserts the four **by name**.
- **The design's §8.1 step 6 first half is incoherent and was not implemented.**
  It requires rejecting an upsert named by an `introduced` name — but that state
  is byte-for-byte what §5.3's middle row *requires* a shadowing rename to
  produce (append the record, rewrite the shadowing upsert's name), and the two
  are indistinguishable in `ProjectData`. Implementing it would have rejected the
  ownership rule's own output. Only the second half ships (no upsert may be named
  by a `consumed` name), which is the maximal rule knowable without the base; the
  genuine collision is caught by the primitives' preflight against the
  materialized name set. The plan's Task 2 "upsert collision → reject" row was
  re-specified accordingly and is asserted as an **accept** with the reasoning in
  the test.
- **`build_constraint_edits_value()` has one call site, not the two the plan's
  file map states.**
- **The plan's Task 1 lists 13 malformed-load cases in its body and calls them
  "eleven" in the checklist.** 17 ship: the 13 plus the three absent-key cases
  and a delete carrying `from`.
- **The plan's ambiguity #8 conflates two different requirements.**
  `export_runtime_assets()` does export an atlas-free project (asserted, scenario
  F3), but `validate_project_for_save()` requires at least one atlas path, so a
  *savable* fixture project needs one. The constraint fixtures ship none and
  borrow `player_idle.matl`, which nothing cross-validates against a skeleton.
- **The design's §6 row-7 grep is not exhaustive as stated.** It claims the only
  occurrences of the four family keys in `skeleton_parse.cpp` are
  `is_skin_scope_key`, the four root arrays and the four skin scopes; `:4482`
  also tests `*type_name == "path"` for the `Path` **attachment** kind. The
  finding it supports — that no animation timeline names a constraint — is
  unaffected and was re-confirmed.

Not independently covered: `build_project_runtime_document()` is public and
applies Phase A defensively, so calling it directly on an unresolvable record
silently no-ops rather than reporting. That is deliberate and matches the
mesh-weight overlay, and both real callers run the validator first; scenario C5
asserts the defensive no-op and scenario D asserts the rejection.

Current validation:

- `cmake -S . -B build && cmake --build build` -> configured and built with zero new warnings; `cmake --build build --target marrow_constraint_warning_check` -> built
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed, reporting `MAR-177 A5: 17 malformed constraint_edits.operations records rejected on load`, `MAR-177 Scenario A2: 14 symbolic-replay rows`, `MAR-177 Scenario D: 7 materialization rows`, `MAR-177 F1 export: .mskl 1611 -> 1627 (+16 = 2 occurrences x 8 bytes), .mbin 605 -> 613 (+8 = 1 interned entry x 8 bytes) with the string table unchanged at 39 entries`, and `MAR-177 F2 export: ... its string table drops 39 -> 35 entries -- exactly [cape_pull, source, transform, translateMix]`; `--create` -> passed
- Five inversions each fail the case they should and were restored: the emit-gate clause (scenario A4), the skin rewrite (scenario C2, failing at `build_project_runtime()` with `skin references unknown transform constraint 'cape_pull'`), the emptied-key erase (scenario C3, failing with `transform constraints must not be empty when provided`), the Phase A record order (scenario E), and the primitives' `validate_project_for_save()` step (scenario B6)
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` against the exact 62-operation registry, unchanged
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> `Headless editor shell smoke rendered 2 frame(s).`, unchanged; MAR-177 touches no shell file
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` with `tools/mcp/venv/bin/python tools/mcp/test_client.py` -> `mcp test_client: PASSED` with the 62/62 exact C++/Python name parity assertion unchanged
- `ctest --test-dir build --output-on-failure` -> `100% tests passed, 0 tests failed out of 22`; `-L runtime` -> 4/4; `-L editor` -> 12/12; no target added
- `./build/marrow_unit_tests`, `marrow_fixture_smoke`, `marrow_selection_tests`, `marrow_timeline_model_tests`, `marrow_parameter_project_smoke`, `marrow_spine_import_smoke`, `marrow_c_smoke`, `marrow_psd_import_smoke`, `marrow_atlas_packer_smoke` -> all passed
- Preference isolation proof: `$HOME/Library/Application Support/Marrow` did **not** exist before the run and still did not exist after it; every shell invocation ran under an isolated `MARROW_CONFIG_HOME`, and no `/tmp/marrow-shell-config-*` or scratch file was left behind

Not run: any interactive confirmation, because MAR-177 ships no UI. The rename
and delete surfaces — buttons, confirmation dialogs, affected-reference previews,
the undoable command and the `SelectionSet` cascade — are MAR-178, and MAR-192
through MAR-210 remain the qualification authority.

## MAR-176 Deterministic Automatic Weights Validation Results

Validated 2026-08-30. MAR-176 adds one producer of influence lists — a geometric
one — and feeds it into the canonicalizer MAR-175 built. It adds no rule about
what a valid weight list is, no new tunable constant, no file-format change and
no `ProjectData` member. The whole engineering content is the determinism the
story is named after, and the two places the governing documents were wrong
about it.

**The generator normalizes its top-four raw weights BEFORE handing them to the
canonicalizer, and that ordering is load-bearing.** The canonicalizer's
`<= 1e-6` drop is absolute and runs *before* its own normalization, so a raw
`1/d²` turns that gate into "farther than 1000 world units". Proved by
inversion: deleting the pre-normalization makes the 100×-scale case fail with
the canonicalizer's own *"A weighted vertex must keep at least one positive
influence."* — the vertex is rejected outright. The shipped `tank` rig is 10×
the fixture (max `|world|` 2395 against 230), so this is a live bug avoided.

**Floating-point contraction is real here, measured rather than predicted.** The
design stated the hazard and declined to claim cross-architecture identity.
Compiling `point_segment_distance_squared()`'s expression at
`-ffp-contract=on` (the arm64 clang default this build uses, since Marrow sets
no flag) yields `d² = 10496.001057976433` for fixture vertex 1's `arm_l`; at
`=off` it yields `10496.001057976431`. That 1 ULP propagates to 1 ULP on the
smaller weight of the pair. Bit-identity is therefore claimed **within one
binary** and nowhere else — and the story's primary acceptance value was chosen
to survive it anyway.

| Area | Evidence | Result |
| --- | --- | --- |
| A bone's segment is parent origin → own origin | `runtime::BoneData` has no `length` field — absent from `skeleton.hpp:60-65`, from `skeleton_parse.cpp`, and from the `.mskl` schema — so the segment comes from the hierarchy, matching the only definition a user can see: `shell_viewport.cpp:1659-1675` draws it and `:2127-2140` hit-tests it. A root bone degenerates to the point at its own origin, as the viewport also shows | PASS |
| The setup-world vertex position is shared, not re-derived | Rebind's step 1 was extracted **verbatim** into `setup_world_position_of_weight_vertex()` and both callers now use it. The extraction was gated by an inverted test: `marrow_mesh_weight_model_tests`, `marrow_project_smoke` and `marrow_agent_dispatch_smoke` were captured before and `diff`ed after — all three byte-identical. `geometry.vertices` is not an alternative source: `skeleton_skin.cpp:529` reads it only for the vertex count in the weighted branch | PASS |
| Pre-normalization, proved by inversion | Removing it makes `generates scale-free weights` fail at 100× with *"A weighted vertex must keep at least one positive influence."*; restored | PASS |
| The exact `0.5 / 0.5` acceptance value | Fixture vertex 2 is past `spine`'s segment end (`t = 2.6 → 1`) and behind `arm_l`'s segment start (`t = -1.12 → 0`), and the two clamped closest points come out **bit-identical**. The two expressions are not the same one, so the reason is worth stating exactly: `arm_l`'s closest point is `start + ab * 0.0`, which is its segment start — `spine`'s world origin — exactly for any origin; `spine`'s is `start + ab * 1.0` from `root`'s origin, which recovers `spine`'s origin exactly **because root's origin is exactly `(0, 0)`**. That second step is fixture-dependent: `start + (end - start)` does not round-trip in general (measured, ~18% of random origin pairs in this coordinate range do not). Once the closest points coincide, the rest is general — both distances evaluate `(V - c).x² + (V - c).y²` on identical operands, so contraction does the same thing to both, and `r / (r + r)` is exactly `0.5`. Asserted with `==` on `double`, not a tolerance, in the model tests, the project smoke, the shell smoke, the agent smoke and the MCP client | PASS |
| AC3's tie-break, asserted where it is observable | The fixture contains a genuine three-way exact tie: vertex 0 is behind `root`'s degenerate point and behind both `spine`'s and `pivot`'s segment starts, so all three clamp to `root`'s origin and yield `d² = 4995.999384543678` bit for bit, weights `0.28250519180307471` each, ordered `root(0), spine(1), pivot(12)` then `arm_l(2)` at `0.15248442459077582`. **A tie among candidates that all survive the cap does not prove the generator's tie-break** — the canonicalizer re-sorts equal weights on ascending index anyway. A distance tie *straddling* the four-influence cap was added, where only one of two tied candidates can be kept; reversing the tie-break makes it fail | PASS |
| Determinism has no implicit input | Every container on the path is a `std::vector` indexed or sorted on the skeleton bone index; `grep` for `unordered_`, `std::map`, `sqrt`, `hypot`, `pow`, `fma`, `execution::` in `mesh_weight_model.cpp` returns **nothing**. `std::sort` on the strict total order `(d² asc, bone index asc)`; instability is irrelevant because bone indices are unique. Every accepted case runs twice and compares, and runs again with the candidate list reversed and compares. The plan's proposed `std::stable_sort` inversion is a **no-op** — 25/25 still pass — exactly as the design argues; the inversion with teeth is dropping the tie-break, which fails both order-independence assertions | PASS |
| Determinism is not idempotence, and the figures are measured | Two generates from the same starting project produce a byte-identical `serialize_project()` (asserted). A generate applied to a previous generate's output is asserted *stable* and the figure printed: **5.551e-17** in the project smoke, **0.000e+00** through MCP where it reports `no_change`, and **0 history entries** in the shell. No test asserts a second generate reports `no_change`, because the design does not guarantee it | PASS |
| Pose independence | Generating while scrubbed to `attack@0.2` is **bit-identical** to generating at setup pose — the direct descendant of MAR-175's D9 defect. The algorithm reads only `setup_pose_bone_world_transforms()`, which builds a scratch skeleton | PASS |
| Cross-path identity | The GUI `Generate` command and `generate_mesh_weights()` driven with the same candidates and scope produce a bit-identical overlay, compared influence by influence on `bone_name`, `weight`, `x` and `y` | PASS |
| Export, on a project that was actually mutated and written | Mutate → `save_project()` → `export_runtime_assets()` → reload → assert decoded values. Case A (count-changing, vertex 0 gains `arm_l`): MBIN **3984 → 4009**, exactly **25** bytes, asserted against the `18 + K` model **derived from `binary.cpp` rather than fitted** — the fixture's string table gives `bone`=139 and `weight`=140 (two varint bytes each) and `x`=15, `y`=16, `spine`=17, `arm_l`=3 (one each), so `K = 7`. Both names were already interned, so no string index shifts. Case B (value-only, vertex 2): MBIN **3984 bytes, identical** — float32 is fixed width, so size is not a signal; the decoded weights are exactly `0.5` and `0.5` with the influence order flipping from the fixture's `arm_l 0.7499999999999999, spine 0.25` to `spine, arm_l` on the tie-break | PASS |
| Survival, not return codes | Every scoped write reads back the vertices it did not name and an adjacent *attachment* edit and asserts them byte-identical, in the project smoke, the agent smoke and the shell smoke. Every rejection — empty candidate list, unknown bone, repeated bone, empty bone name, out-of-range vertex, repeated vertex, unweighted attachment — leaves `serialize_project()` byte-identical. Proved by inversion: making the scoped path write every vertex fails the survival assertion; dropping the unweighted-attachment guard fails with the project silently accepting a no-op | PASS |
| A real save round trip | `save_project()` succeeds after every accepted generate and the project reloads with every influence in order. It is **not** bit-exact, and the reason is recorded rather than papered over: `.marrow` is written by `json::serialize_pretty` at 15 significant digits (`src/runtime/json.cpp:597`) while an exact `serialize_pretty_round_trip` exists and is not what project save uses. Measured reload stability **3.553e-14**; regenerating on the reloaded project agrees with regenerating in memory to **1.11e-16**. This is a pre-existing property of the project writer, not something generation introduces | PASS |
| AC1 on every surface, proved by inversion | `bones` is required on the wire with no default. Making an omitted `bones` fall back to every bone makes the agent rejection case fail; restored. The GUI `Generate` button is disabled with the reason shown when the checklist is empty, and the command still refuses with *"mesh.generate_weights requires at least one candidate bone."* — asserted on the message, so the case cannot pass merely because the target failed to resolve | PASS |
| Agent and MCP at 62 | `mesh.generate_weights` sits immediately after `mesh.rebind_weights` as (`edit`, mutating, not review, dry-run supported). `marrow_agent_dispatch_smoke` → `PASSED` over **382** `[ OK ]` cases (up from 354) against the exact **62**-operation registry. The three shipped weight operations' dry-run payloads are byte-identical — `candidate_bone_count` is emitted by the new operation alone — and the `normalize_weights idempotent` row is unchanged. Dry runs run the identical preflight a live call runs and change nothing | PASS |
| The candidate checklist is a tool setting | `WeightPaintSettings::candidate_bone_names` is stored by **name**, not index, so a reload that renumbers bones cannot silently retarget it, and a name that stops resolving is shown struck through and makes Generate reject rather than being dropped. It is not in `ProjectData`, not serialized to `.marrow`, not in `editor-settings.json`, not in `PreviewState`, and not in the history snapshot — MAR-174's lesson applied unchanged. `From selection` is a one-shot fill: clearing the selection afterwards leaves the checklist unchanged, asserted | PASS |
| Compatibility | `.mskl` v1, `.mbin` v2, C ABI v1, the `.marrow` schema and `editor-settings.json` v1 all unchanged; `ProjectData` gained no member; `git diff` is empty on `src/runtime/**`, `include/marrow/marrow_c.h` and `src/editor/preferences.cpp`; `kMeshWeightEpsilon`, `kMaxMeshWeightInfluences` and `kMeshWeightSumTolerance` keep their values. MAR-176 adds **no** new tunable numeric constant — no radius, no exponent, no smoothing parameter, no minimum-weight slider | PASS |

Errors found in this story's own governing documents, all corrected here:

- **The `4015` export baseline was wrong.** The design derived it from MAR-175's
  recorded `4065 → 4040 → 4015` chain, but that chain ran on a project MAR-175's
  smoke had already mutated to four influences on one vertex. The untouched
  fixture exports at **3984** bytes. The `18 + K` model and `K = 7` were
  re-derived independently from the `.mbin` string table and **held**; only the
  baseline constant was wrong.
- **The setup pose is not exact, and "~1e-6" understates it.** Measured:
  `root (0, 0)`, `spine (-2.1855694285477512e-06, 50)`,
  `arm_l (-30.000001907348633, 60)`, `pivot (80.000007629394531, -120)`. The
  worst generated-versus-authored bind offset delta is **5.09e-06**, so the 1e-6
  tolerance the documents imply would have failed; 1e-5 is used and the figure
  is printed.
- **Every bit-exact weight in the design's §5.6 and §13 tables was wrong**, because
  they were computed from idealised `(0,50)` / `(-30,60)` origins. The as-built
  values are in the tests. The `0.5 / 0.5` acceptance value and the three-way
  tie survive exactly, which is precisely why the design chose them.
- **`docs/root1/refector.md:112` states what the surface is and was missing from
  the spec's list of `61 → 62` sites** (§12 named only `:20`). Updated.
- **The plan's `std::stable_sort` inversion cannot fail**, since the comparator is
  a strict total order — the design's own §7.2 says so. Replaced with an
  inversion that does.
- **A tie among candidates that all survive the cap does not exercise AC3's
  tie-break**, because the canonicalizer re-sorts equal weights on ascending
  skeleton index regardless. A cap-straddling tie case was added.
- **The design's §7.4 reason for the exact `0.5 / 0.5` is broader than what
  holds.** It says both distances "evaluate the *identical expression* on the
  *identical* operands ... whatever `(sx, sy)` happens to be". The two
  expressions are in fact different (`start + ab * 1.0` against
  `start + ab * 0.0`), and they coincide here because **root's origin is exactly
  `(0, 0)`** — not for an arbitrary origin. The conclusion and the assertion are
  correct; only the justification over-generalized, and it is narrowed in the
  comments so a future reader does not carry it to a fixture where it fails.

Not independently covered: the duplicate-candidate guard in
`generate_mesh_weights()` is redundant with the one in
`generate_mesh_weight_vertex()` — removing the former leaves the smoke passing,
because the latter produces the same message. It is kept because it rejects
before the full `ProjectData` copy.

Current validation:

- `cmake -S . -B build && cmake --build build` -> configured and built with zero new warnings; `cmake --build build --target marrow_verify_third_party` -> vendored SDL3, zlib, Dear ImGui, Sokol, sokol_imgui and sokol-shdc hashes verified
- `./build/marrow_mesh_weight_model_tests` -> `Mesh weight model: 25 cases passed`, up from 14, printing the measured float32 setup-pose origins and the `5.09e-06` worst bind-offset delta so every tolerance is visible in the log. The fourteen shipped MAR-175 cases and their output are unchanged
- Three inversions each fail the case they should and were restored: deleting the pre-normalization fails `generates scale-free weights`; reversing the bone-index tie-break fails `falls back to the nearest candidate` and `breaks a cap-boundary tie on skeleton order` (order becomes `near,mid,far,tied_high`); dropping the tie-break entirely fails both order-independence assertions. The plan's `std::stable_sort`-only inversion passes 25/25 and is recorded as a no-op
- `ctest --test-dir build` -> `100% tests passed, 0 tests failed out of 22`; no target added
- `./build/marrow_unit_tests`, `marrow_timeline_model_tests`, `marrow_timeline_graph_model_tests`, `marrow_viewport_interaction_tests`, `marrow_selection_tests`, `marrow_preference_tests`, `marrow_windowing_tests`, `marrow_pen_input_tests`, `marrow_agent_socket_tests` -> all passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed, reporting `MAR-176 Case A export: MBIN 3984 -> 4009 bytes, exactly 25 bytes for the one added influence`, `MAR-176 Case B export: MBIN 3984 bytes, identical`, `determinism vs idempotence: ... stable to 5.551e-17`, `save/reload round trip: ... stable to 3.553e-14`, and `regenerate after reload: ... within 1.11e-16`; `--create` -> passed
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` over 382 `[ OK ]` cases against the exact 62-operation registry, reporting that a repeated `mesh.generate_weights` returned `changed=false`
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> passed, reporting `MAR-176 pose independence: generating at attack@0.2 is bit-identical to generating at setup pose` and `MAR-176 cross-path identity: the GUI Generate command and generate_mesh_weights() produced a bit-identical overlay`; every shipped MAR-175 weight assertion in the block unchanged; `parameter_face_basic.marrow --auto-close 2` -> passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` with `tools/mcp/venv/bin/python tools/mcp/test_client.py` -> `mcp test_client: PASSED` with **62/62** exact C++/Python name parity, the explicit `mesh.generate_weights` registry metadata row, and a dry-run -> live -> read-back -> second-application -> undo -> read-back sequence reporting `second application stable to 0.000e+00 (message: 'Mesh weights already match the generated candidates.')`; MAR-175's `mesh.rebind_weights` figure is unchanged at `9.948e-14`; `--parameter-only` -> `mcp parameter test_client: PASSED`, unchanged; `python -m py_compile` over all four MCP files -> clean. The MCP tool-removal negative was verified by hand once: deleting the new `types.Tool` makes the client fail on `assert len(mcp_names) == 62`, and it was restored
- `./build/marrow_inspect --compare` on the exported bundle -> matches, JSON `14336` bytes, MBIN v2 `3984` bytes; `./build/marrow_fixture_smoke` on the exported `.mskl` -> passed
- `./build/marrow_fixture_smoke` on `.mskl` and `.mbin`, `./build/marrow_spine_import_smoke` (the owl zero-weight weighted-mesh and tank weighted-clipping regressions unchanged), `./build/marrow_renderer_sample --skip-render`, `./build/marrow_c_smoke`, `marrow_psd_import_smoke`, `marrow_atlas_packer_smoke`, `marrow_bootstrap`, `marrow_parameter_project_smoke` -> all passed
- Preference isolation proof: `$HOME/Library/Application Support/Marrow` did **not** exist before the run and still did not exist after it; every shell and smoke invocation ran under an isolated `MARROW_CONFIG_HOME`, and no `/tmp/marrow-shell-config-*` or scratch file was left behind

Not run: the interactive macOS confirmation that the candidate-bone checklist,
its `All` / `None` / `From selection` buttons, the `candidates: N` readout and
the `Generate` button render and respond in the viewport weight panel. The
headless smoke drives the command directly and cannot see layout. MAR-192
through MAR-210 remain the qualification authority.

## MAR-175 Unified Manual Weight Authoring Validation Results

Validated 2026-08-30. Five code paths authored mesh vertex weights and four of
them disagreed about what a valid influence list is — on normalization,
deduplication, ordering, the influence cap, zero handling, non-finite handling,
and which pose bind offsets are expressed in. Two of those disagreements could
produce a project Marrow itself refuses to save: `set_vertex_weights` accepted a
zero weight and a repeated bone, returned `ok`, dirtied the project, and then
failed at `save` with *"mesh weight edit influences must preserve positive
weights"* / *"mesh weight edit vertices must not repeat the same bone"*. That is
an operation reporting success while leaving the user unable to save at all, so
the guard is a save round trip — author, then actually call `save_project()` —
not a return code.

The rules now live in one UI-free `mesh_weight_model` translation unit inside
`marrow_editor`. That placement is the point: `shell_weight_paint.cpp` compiles
into the **executable**, so nothing under `src/tests/` can link it and the
"UI-free math with unit tests" rule is not satisfiable by hoisting the shell
normalizer in place.

Three corrections to the governing design were found during implementation and
are recorded here rather than quietly absorbed.

**The exact-`1.0` normalization skip does not work, and the fixture is the
counterexample.** The design specified skipping the division when the summed
weight is exactly `1.0`, and named `1/3, 1/3, 1/3` as the witness for bit-exact
idempotence. That witness passes for an unrelated reason — three thirds happen
to sum to exactly `1.0`. The project's own fixture vertex is `spine 0.6,
arm_l 0.2`, and `0.6/0.8 + 0.2/0.8` is `0.9999999999999999`, so an exact test
never fires and every later canonicalization nudges the weights again. Measured
over 1.8M synthetic vertices across six distributions, the sum after one
division lands within **2 ULP** of one, so the implemented test is
`|sum - 1.0| <= 4 * DBL_EPSILON`. A second, independent break was found by
inverting the code: division rounds, so two weights that differed by one ULP can
land on the same value, and the first sort ordered them on their *pre-division*
values — without a **second sort on the normalized weights** the output does not
satisfy its own comparator and a second pass reorders it. Both properties are
load-bearing and both have a failing test when removed.

**Replace's specified semantics were self-contradictory.** The design wrote
`influence_it->weight = stamp_strength` "letting canonicalization renormalize",
and also required `strength = 1.0` to drive the active bone to a full `1.0` in
one pass. Assigning a raw weight and renormalizing yields `1/(1 + others)` and
can never reach `1.0`. Replace is implemented as a **target**: the stamp is the
weight the active bone ends up with and the remaining influences are scaled to
fill the remainder, which is what the acceptance sentence describes and what a
Replace brush exists to do.

**`inverse_transform_point_safe()` was silently narrowing every bind offset to
float32.** The shipped shell helper returned a `runtime::AttachmentVertex`,
whose members are `float` (`skeleton.hpp:151-157`), so a double result was
rounded to float32 before being stored in a `double` field and written to
`.marrow` at full width. It cost about six significant digits on every newly
painted influence and made a rebind miss its own setup-world point by ~1e-5.
Found by an assertion that failed at `5.8e-06` when the arithmetic proved it
should hold exactly. The primitive now returns `double`.

| Area | Evidence | Result |
| --- | --- | --- |
| One canonicalizer, eight ordered steps | `canonicalize_mesh_weight_vertex()` rejects non-finite input, rejects unknown and empty bone names, merges duplicate bones (summed weight, weight-weighted mean bind), drops `<= 1e-6`, sorts by descending weight then **ascending skeleton index**, caps at four, rejects an empty result, and normalizes. `marrow_mesh_weight_model_tests` -> **14 cases passed**, every accepted case additionally asserting bit-exact idempotence and every rejection asserting the message **and** that the vertex is byte-unchanged (compared with `memcmp`, because a NaN never equals itself) | PASS |
| D6 — NaN poisoned the vertex | `NaN <= 1e-6` is false, so a NaN survived both guards of both shipped normalizers, poisoned the total, survived the total guard for the same reason, and was divided by itself. Rejection is step 1. Asserted for NaN, `+inf`, and `-inf` on `weight` and on both bind components, in the unit tests and again through `set_mesh_vertex_weights()` in `marrow_project_smoke` with `serialize_project()` byte-identical after each of eight rejections. On the agent surface the input is unreachable and that is asserted rather than assumed: JSON has no NaN or Infinity literal and the parser refuses an overflowing one (`invalid numeric value`), so the payload dies before the operation runs | PASS |
| V1/V2 — accepted writes are now savable | The two hand-built projects are still refused by an actual `save_project()` call with their exact shipped messages, and the same two inputs through the canonical path save, reload, and re-canonicalize to a bit-identical fixed point of the `.marrow` loader. Teeth proved by inversion: skipping the merge reproduces *"mesh weight edit vertices must not repeat the same bone"* and keeping zero weights reproduces *"mesh weight edit influences must preserve positive weights"* — the two shipped defects, end to end | PASS |
| D9 — a `.marrow`-visible bind-offset defect | Paint inverted the **current preview pose** for a newly added bone, so the same stroke wrote different geometry depending on where the playhead sat. Now the setup pose, resolved once per stroke. **What the shipped smoke asserted before:** it scrubbed to attack@0.2 and painted `spine` onto vertices that already carried `spine`, so it never entered the new-influence branch and asserted no bind offset at all — that is what hid the defect, not the scrub. **After:** the same block paints `arm_l`, a bone vertex 0 does not have, while still parked at attack@0.2 (where `arm_l` is scaled `a=0.5` against `a=1` at setup, asserted so the two poses are distinguishable), and requires the setup-pose value. Before the fix it wrote `(-94.94, -15.56)` where setup requires `(-34, -90)`; inverting the fix reproduces exactly that | PASS |
| Every shipped weight assertion unchanged | `1.0 / 0.75 / 0.25` baseline, the blue/green/yellow/red heat ramp, Paint `0.75 -> 0.875`, per-vertex total `1.0`, one undo entry per stroke, the Erase and Smooth sequences, the `0.6875` smooth export, and undo/redo all pass with their existing numbers. The behaviour-preserving extraction was gated on byte-identical export against a stashed baseline before any rule changed | PASS |
| Four new authoring surfaces | Replace at `1.0` drives the active bone to a full `1.0` in one pass and at `0.4` lands on exactly `(double)0.4f`; the numeric influence table commits one transaction and one history entry per field, redisplays the renormalized weight (`3.0/1.0` typed -> `0.75/0.25`), refuses removing the last influence, and disables the add combo at four; selected-scope Normalize and setup-pose Rebind are one transaction and one history entry each, and a scope in which nothing changes neither dirties nor records | PASS |
| Rebind touches offsets only | Every weight is bit-unchanged, `triangles`, `uvs`, and the base `vertices` are compared and identical, undo restores the pre-rebind offsets exactly, and the repaired vertex's influences all name one setup-world point within `1e-9`. Note the fixture's own offsets are **not** a fixed point of rebind: they were authored as round numbers against transforms that are `(0,+50)` and `(-30,+60)` by intent, but the runtime composes bone world transforms in float32, so the setup pose it reports carries ~1e-6 of error. The no-change branch is therefore asserted on rebind's own output, which is where it genuinely holds | PASS |
| Rebind is deterministic, not bit-idempotent | `BoneWorldTransform` is six `float`s while bind offsets are `double`, so `S(S^-1(V))` does not reproduce `V` bit-for-bit. The narrowing enters through the **transform**, not the weight. Repeat runs from the same input are asserted bit-identical; a second *application* is asserted stable only, and measured at **9.948e-14** through the MCP client. The tests print the distinction so a later reader does not "fix" the asymmetry | PASS |
| AC1/AC6 cross-path identity | The brush's pure layer, the numeric influence table, and the project primitive are driven to the same intended influence set and produce a bit-identical vertex: `spine=0.74999999999999989@11,22; arm_l=0.25@33,44`. This is the assertion that proves the paths share one implementation rather than merely agreeing today | PASS |
| Export, on a project that was actually mutated and written | Mutate -> `save_project()` -> `export_runtime_assets()` -> reload -> assert decoded values, twice, with a signal chosen to discriminate for **weights**. Case A, topology-changing: one vertex from four influences to three to two (a duplicate bone and a zero weight), MBIN **4065 -> 4040 -> 4015** bytes, exactly **25** bytes per removed influence, asserted against the encoding model `18 + K` derived from `binary.cpp` (1 object tag + 1 member count + 1 String tag + 3 Number tags + 3x4 float32 bytes, plus `K = 7` string-index varint bytes for this fixture) rather than a fitted constant, and the two surviving decoded weights asserted. Case B, value-only: the influence *set* unchanged, MBIN **4065 bytes, identical** — float32 is fixed width, so size is not a signal here and the decoded weight is; asserted bit-exactly as `0.4` because `.mskl` stores doubles, with the `.mbin` equivalence delegated to `validate_binary_export`. No assertion anywhere claims the float32 weights sum to exactly `1.0f` | PASS |
| Survival, not return codes | Every scoped write reads back the vertices it did not name and an adjacent *attachment* edit and asserts both byte-identical: in `marrow_project_smoke` after Case A, in `marrow_agent_dispatch_smoke` after the zero-weight drop and the scoped normalize, and in the shell smoke after each numeric commit | PASS |
| Agent and MCP at 61 | `mesh.rebind_weights` sits immediately after `normalize_weights` as (`edit`, mutating, not review, dry-run supported); `normalize_weights` gains an optional `vertices` scope where absent means every vertex, so every shipped call behaves identically. `marrow_agent_dispatch_smoke` -> `PASSED` over **354** `[ OK ]` cases (up from 324) against the exact **61**-operation registry, including the shipped `normalize_weights idempotent` row with its `no_change` message and null scene delta. Dry runs run the identical preflight a live call runs and change neither `project_revision()`, `undo_count()`, nor `dirty()`. `mesh.describe` additionally reports per-vertex influence values so a headless smoke can assert what a write produced and what it left alone — additive, every shipped field keeping its name, type, and position | PASS |
| C1 and C2, the two deliberate breaks | C1: `set_vertex_weights` rejects an explicit `"normalize": false` with `invalid_request`. Canonicalization is unconditional, so the flag has no implementable meaning — honouring it re-opens V1/V2 and ignoring it returns success for a request that was not performed. `true` and omission are unaffected and no in-tree caller sends it. C2: `normalize_weights` now also drops, merges, sorts, and caps, because the narrow version could leave a project `save_project()` refuses, so "normalized" did not mean "savable". Both keep their names, arguments, `no_change` disposition, and messages. Recorded in `docs/root1/agent-control.md` as a migration note. Inverting C1 to honour the flag fails the agent smoke | PASS |
| Compatibility | `.mskl` v1, `.mbin` v2, C ABI v1, and `editor-settings.json` v1 unchanged; the `.marrow` schema unchanged — no field added, removed, or retyped in `mesh_edits.weights`, and the canonical rules are strictly *narrower* than what the loader already accepts, so there is no migration and no version bump; `ProjectData` gained no member | PASS |

Command output recorded during validation:

- `cmake -S . -B build && cmake --build build` -> configured and built with zero new warnings; `cmake --build build --target marrow_verify_third_party` -> hashes verified
- `./build/marrow_mesh_weight_model_tests` -> `Mesh weight model: 14 cases passed`, printing the rebind determinism-versus-idempotence note
- `ctest --test-dir build` -> `100% tests passed, 0 tests failed out of 22`, up from 21 with the new `marrow.mesh_weight_model` entry
- `./build/marrow_unit_tests`, `marrow_timeline_model_tests` (`17 cases`), `marrow_timeline_graph_model_tests` (`20 cases`), `marrow_viewport_interaction_tests`, `marrow_selection_tests` (`8 cases`), `marrow_preference_tests` (`11 cases`), `marrow_windowing_tests` (`4 cases`), `marrow_pen_input_tests` (`34 cases`), `marrow_agent_socket_tests` (`4 cases`) -> all passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> passed with the extended weight block; `--project assets/fixtures/parameter_face_basic.marrow --auto-close 2` -> passed unchanged
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` over 354 `[ OK ]` cases against the exact 61-operation registry
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed, reporting `MAR-175 Case A export: MBIN 4065 -> 4040 -> 4015 bytes, exactly 25 bytes per removed influence` and `MAR-175 Case B export: MBIN 4065 bytes, unchanged`; `--create` and `--export-runtime`/`--export-binary` -> passed; `./build/marrow_inspect --compare` -> matches; `./build/marrow_fixture_smoke` on the exported `.mskl` -> passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` with `tools/mcp/venv/bin/python tools/mcp/test_client.py` -> `mcp test_client: PASSED` with **61/61** exact C++/Python name parity, the explicit `mesh.rebind_weights` registry metadata row, and a dry-run -> live -> read-back -> second-application -> undo -> read-back sequence reporting `second application stable to 9.948e-14`; `--parameter-only` -> `mcp parameter test_client: PASSED`, unchanged; `python -m py_compile` over all four MCP files -> clean. The MCP tool-removal negative was verified by hand once: deleting the new `types.Tool` makes the client fail on `assert len(mcp_names) == 61`, and it was restored
- `./build/marrow_fixture_smoke` on `.mskl` and `.mbin`, `./build/marrow_spine_import_smoke` (the owl zero-weight weighted-mesh and tank weighted-clipping regressions — the importer does not route through the editor primitive, so this is the guard that canonicalization did not leak), `./build/marrow_renderer_sample --skip-render`, `./build/marrow_c_smoke`, `./build/marrow_psd_import_smoke`, `./build/marrow_atlas_packer_smoke`, `./build/marrow_bootstrap`, `./build/marrow_parameter_project_smoke` -> all passed
- Preference isolation proof: `$HOME/Library/Application Support/Marrow` did **not** exist before the run and still did not exist after every gate above, including both `--agent-port` runs of the production startup path, which resolves the real path with no override. No `/tmp/marrow-shell-config-*` directory was left behind

Not run: the interactive macOS confirmation that the Replace brush icon, the
editable influence table with its `Sum` readout and add/remove controls, and the
Normalize and Rebind buttons render and respond in the viewport weight panel.
The headless smoke drives the commands directly and cannot see layout. MAR-192
through MAR-210 remain the qualification authority.

## MAR-174 Transient Preview Playback Speed Validation Results

Validated 2026-08-30. MAR-174 is one bounded, strictly positive multiplier on the
Timeline transport, and its whole engineering content is the proof that nothing
else can see it. `advance_timeline_playback()` is the sole progression path and
`EditorSession::advance()` forwards one delta into the displayed time, the
sampled pose, the crossfade, and event dispatch, so **one multiply at one site**
scales all four consistently by construction rather than by four agreeing
implementations.

Three decisions carry the story. **Speed is a magnitude, not a direction.**
`PreviewImpl::advance()` early-returns on `delta_seconds <= 0.0` and the event
dispatcher returns on `current_time <= previous_time`, so a negative speed would
be a silent no-op that also dropped every event on that frame; direction stays
with `preview_reverse`, which the runtime already models as a sampling transform
over a monotonically increasing track time. **Zero clamps to 0.05x rather than
meaning pause**, because the accepting range is `[0.05, 8.0]` and pause already
exists as `timeline_playing`. **Finite out-of-range clamps, non-finite rejects
with the field bit-unchanged** — clamping is the house idiom for the shell's
other transient preview scalars (`preview_queue_delay`,
`preview_custom_mix_duration`), while `NaN` has no clamp target and silently
substituting one would hide a caller bug.

The field lives on `ShellState`, **not** on `marrow::editor::PreviewState`, even
though that struct already holds `reverse`, `loop`, and `playing` and looks like
the natural home. `assign_history_snapshot()` writes every `PreviewState` field
back on undo, and `sync_shell_from_editor_session()` does the same after
`session.undo()`, while `history_snapshots_equal()` compares only the serialized
project and the preview skin/slot selections — so a speed parked there would
silently jump on Ctrl+Z with nothing reporting a change. The smoke asserts the
speed survives two undos and a redo, and that case fails when the write is
simulated at the undo site.

| Area | Evidence | Result |
| --- | --- | --- |
| Domain and presets | Continuous `[0.05, 8.0]` with `1.0` as both the default and a preset: `0.0` and `-3.0` clamp to `0.05`, `100.0` clamps to `8.0`, the bounds and `3.75` assign exactly, and each of `{0.25, 0.5, 1.0, 2.0}` is asserted in range **and** assigned exactly, so the presets are shortcuts onto the domain rather than the domain itself. `NaN`, `+inf`, and `-inf` each return `false` with the field compared by `std::memcmp` over the two `double`s, not `==`, so a `NaN` write cannot pass. `preview_playback_speed()` is an independent second clamp at the point of use: a directly poked `NaN`, `1000.0`, and `-1000.0` read back as `1.0`, `8.0`, and `0.05` | PASS |
| One multiply, one site | A 0.25 s frame reaches 0.25, 0.5, 0.0625, 0.125, 0.0 (`fmod(2.0, 1.0)`), and 0.0125 s at 1x, 2x, 0.25x, 0.5x, 8x, and 0.05x on `idle`'s inferred 1.0 s duration. The equivalence law is stated as an assertion: three frames at `(s, 0.25)` land where three frames at `(1.0, 0.25*s)` land, for `s ∈ {0.25, 2.0}`. Dropping the multiply fails with `A 0.25s frame at 2x must reach 0.5s, got 0.25s.` | PASS |
| Reverse composes | At `preview_reverse` with speed 2x, a 0.15 s frame reaches track time 0.3 s — **identical to forward**, proving speed does not alter direction — and the sampled `spine` local pose at that track time matches the forward pose at `duration - t == 0.7` s within `1e-6` while differing from the forward pose at 0.3 s, which the fixture is first asserted to distinguish | PASS |
| Loop, clamp, scrub, pause | `std::fmod` is a single exact modulo, so a two-period step at 8x from 0.9 s lands back on 0.9 s with the `spine` pose bit-comparable to the 1x walk within `1e-6` and playback still running — no drift. MAR-172's managed boundary key is never sampled during looped playback at any speed because `fmod` yields `[0, duration)`. Non-looping, the same 8x step stops at exactly the duration with `timeline_playing == false`: speed reaches the end sooner and cannot overshoot it. A scrub at 8x lands on its exact requested time, because scrubbing is an absolute position and not a rate. A paused transport advances by nothing at 8x, and resuming advances by `delta * speed` from where it paused | PASS |
| The non-effect gate | After replaying every accepted and every rejected request with no `advance` between capture and compare, `serialize_project()`, `undo_count()`, `redo_count()`, `project_revision()`, `runtime_revision()`, **`preview_revision()`**, and `agent_operation_descriptor_count()` are all identical, `session.dirty()` and `project_dirty` are still `false`, and the session's own `PreviewState` (playing, loop, reverse, time, animation) has not moved. `preview_revision` is the sharp one — a stray `session.set_playing(true)` in the setter fails this gate. The session-state capture covers the residual case where a stray call's argument happens to match. Only some session setters are change-gated (`set_playing`, `set_loop`, `set_reverse`, `clear_queue`, `advance_parameter_state`); `select_animation`, `select_setup_pose`, `seek`, `advance`, and `set_queue` bump unconditionally and are therefore caught by the revision comparison alone. Of the change-gated setters, the fields written by `set_playing`, `set_loop`, and `set_reverse` are exactly what the `PreviewState` capture compares, and `clear_queue` is provably a no-op from the settled pre-capture state, so every observable stray call is caught. The exported `.mskl` **and** `.mbin` are byte-compared before and after the batch and are identical, with both asserted non-empty first so the comparison cannot pass vacuously | PASS |
| Mid-gesture inertness | The setter deliberately does not consult `authoring_gesture_active()`, and the claim is proved rather than asserted: inside a live `EditTransaction` a speed change assigns, records no history, and leaves the transaction valid, while `advance_timeline_playback()` moves no time because `EditorSession::advance()` refuses outright while a transaction is open. Rolling back restores the byte-identical project | PASS |
| Undo never rewrites it | With two real history entries present, a speed of 3.5 survives two undos and a redo unchanged. Simulating a `PreviewState`-resident field by writing the default at the undo site fails this with `Undo must leave the transient preview speed exactly where the user put it, got 1.` | PASS |
| Reset through one default | Both `reload_project()` branches are exercised, so "opening, reloading, or replacing" is covered by construction rather than by an argument about which word maps to which call: `session.reload()` on the same path, then `session.open()` on a copy saved into the isolation directory. Each lands on `kDefaultPreviewSpeed`, which is also the `ShellState` member initializer, so a fresh shell and a reloaded shell start identically. Removing the reset line fails with `Reloading the current project must reset the preview speed, got 4.` | PASS |
| Registry unchanged at 60; no agent operation added | `std::size(kOperationSpecs)` is **60** before and after MAR-174, `timeline.scale_key_times` still at index 35. Speed is a display control with no project effect, so exposing it to an agent would give the agent a lever that provably does nothing to the document. `git diff` is empty on `src/editor/agent_dispatch.cpp`, `src/editor/agent_dispatch_smoke.cpp`, `src/editor/agent_handlers_*.cpp`, and `tools/mcp/**`; the seven `!= 60U` guards in `shell_smoke_graph.cpp` are untouched and still seven; MAR-174's only count reference is the guard inside its own new scenario | PASS |
| Compatibility | `.mskl` v1, `.mbin` v2, and C ABI v1 unchanged with a zero-byte diff on `src/runtime/**`, `include/marrow/runtime/**`, `src/c_api/**`, and `include/marrow/c_api/**`; `editor-settings.json` v1 unchanged with a zero-byte diff on `src/editor/preferences.cpp` and `include/marrow/editor/preferences.hpp`, and MAR-174 persists nothing — AC 4 resets on every project open, so a persisted value could never be observed; the `.marrow` schema and the preview/history contract unchanged with a zero-byte diff on `include/marrow/editor/project.hpp`, `src/editor/project.cpp`, `include/marrow/editor/session.hpp`, and `src/editor/session.cpp`; no `include/marrow/**` header and no CMake file changed. `grep -rn 'preview_speed' src/ include/` reaches only `shell_state.hpp`, `timeline_controller.{hpp,cpp}`, `shell_core.cpp`, `shell_timeline.cpp`, and `shell_smoke_timeline.cpp` | PASS |

Command output recorded during validation:

- `cmake -S . -B build && cmake --build build` -> configured and built with zero warnings; `cmake --build build --target marrow_verify_third_party` and `--target marrow_constraint_warning_check` -> both passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> passed, including the new `validate_preview_playback_speed_shell_smoke` scenario; `--project assets/fixtures/parameter_face_basic.marrow --auto-close 2` -> passed unchanged, proving the new transport row does not disturb Parameter Modeling mode where the transport is disabled; `--verify-launch-focus` -> passed
- `ctest --test-dir build --output-on-failure` -> `100% tests passed, 0 tests failed out of 21`; `-L editor` -> 11/11; `-L runtime` -> 4/4. MAR-174 registers no new CTest entry — its scenario runs inside the existing `marrow.editor_shell_smoke`
- `./build/marrow_unit_tests` -> passed; `./build/marrow_timeline_model_tests` -> `Timeline model: 17 cases passed`; `./build/marrow_timeline_graph_model_tests` -> `Timeline graph model: 20 cases passed`; `./build/marrow_selection_tests` -> `SelectionSet: 8 cases passed`; `./build/marrow_preference_tests` -> `PreferenceStore: 11 cases passed` (unchanged; MAR-174 touches no preference code); `./build/marrow_viewport_interaction_tests` -> passed; `./build/marrow_windowing_tests` -> `Windowing: 4 cases passed`; `./build/marrow_pen_input_tests` -> `Pen input: 34 cases passed`; `./build/marrow_agent_socket_tests` -> `Agent socket tests: 4 cases passed`
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` over 325 `[ OK ]` cases, unchanged
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` with `tools/mcp/venv/bin/python tools/mcp/test_client.py` -> `mcp test_client: PASSED`, whose own `assert len(registry_names) == 60` and `assert len(mcp_names) == 60` are untouched, so 60/60 parity is unchanged; `python -m py_compile` over `server.py`, `test_client.py`, `tools/editing.py`, `tools/inspection.py` -> clean
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed, still reporting MAR-173's `JSON 14341 bytes, MBIN 3984 bytes`; `--export-runtime`/`--export-binary` -> passed; `./build/marrow_inspect --compare` -> matches; `./build/marrow_fixture_smoke`, `./build/marrow_parameter_project_smoke`, and `./build/marrow_c_smoke` -> all passed
- Preference isolation proof: `$HOME/Library/Application Support/Marrow` did **not** exist before the run and still did not exist after every gate above, including the `--agent-port` run of the production `shell_main.cpp` startup load, which resolves the real path with no override. No `/tmp/marrow-shell-config-*` directory was left behind. The new scenario installs its own `ScopedPreferenceIsolation` even though MAR-174 reads and writes no preference at all
- `git diff --name-only` -> exactly eight source files plus this closure prose: `shell_state.hpp`, `timeline_controller.{hpp,cpp}`, `shell_core.cpp`, `shell_timeline.cpp`, `shell_smoke_timeline.cpp`, `shell_smoke_scenarios.hpp`, `shell_smoke.cpp`

Not run: the interactive macOS confirmation that the Speed drag and the four
preset buttons render on the Timeline transport row, that changing speed during
playback changes the rate without stopping playback, and that the readout shows
`2.00x`. The headless smoke drives `set_preview_playback_speed()` directly and
cannot see layout. MAR-192 through MAR-210 remain the qualification authority.

## MAR-173 Atomic Key Time Scaling Validation Results

Validated 2026-08-30. Every other timing edit in Marrow is a *translation*:
`retime_keyframes()` adds one shared delta. MAR-173 adds the missing operation —
a **scale** about one edge of the selection's own time range, where
`t' = pivot + (t - pivot) * s` for one finite, strictly positive ratio. The
pivot is never a caller-supplied time: `TimelineScalePivot::RangeStart` /
`RangeEnd` names which *edge* stays fixed and the primitive computes the pivot
from the resolved selector times, which makes the story's criterion structural
rather than documented. The pivot key is therefore bit-identical by IEEE-754
(`p + 0.0 * s == p`), not by tolerance, and that is what makes the gesture's
incremental composition sound. The one place MAR-173 deliberately departs from
`retime_keyframes()` is its collision policy: **retime clamps, scaling rejects.**
A clamped translation still delivers a translation, just a shorter one; a clamped
scale would have to either stop every key at the first collision (a ratio the
user did not choose and cannot see) or move keys by different ratios (not a scale
at all). The minimum-separation rule is `min(spacing, original_gap)`, not a flat
1 ms, so an imported or MAR-172-adopted timeline already carrying a tight gap
stays scalable as long as the scale does not make that gap worse. Loop-sync
pinning becomes a **rejection** here, though retime pins to zero, for the same
reason: a partially pinned scale is not a scale. Atomicity is claimed at three
separate scales so no claim borrows another's strength — one primitive call is a
candidate copy plus a single move-assign; one gesture *frame* holds its last
accepted state on a rejection rather than dying, because dragging a scale handle
inward past a collision and back out is ordinary; one gesture is one history
entry. The Agent/MCP surface grew by exactly one operation,
`timeline.scale_key_times`, taking the registry to exactly **60**. `.mskl` v1,
`.mbin` v2, C ABI v1, and `editor-settings.json` v1 are unchanged, and the
operation writes `keyframe.time` and nothing else.

| Slice | Verification | Result |
| --- | --- | --- |
| Pivot exactness and both directions | `RangeStart`, `s = 1.25` on `{0.0, 0.5, 1.0}` produces exactly `{0.0, 0.625, 1.25}` with `pivot_time == 0.0`, `original_span == 1.0`, `scaled_span == 1.25`, `moved_key_count == 2`; `RangeEnd`, `s = 0.5` produces `{0.5, 0.75, 1.0}` with `pivot_time == 1.0`. The pivot key is compared with `==` on the stored `double`, not within an epsilon, in both directions and throughout a live drag. `RangeEnd`, `s = 1.25` puts the first key at `-0.25` and is **rejected**, not clamped | PASS |
| Reject, never clamp | Fifteen rejections each leave `serialize_project()` byte-identical: `s = 0`, `s = -1`, `s = NaN`, `s = inf`, an empty selector list, a single-key selection, an all-same-time selection, a duplicate selector, an unresolvable selector, selectors naming two animations, a non-event collision, an intrusion into an unselected neighbour on the right, the same on the left, a target below zero, and a target beyond the float32 range | PASS |
| `min(spacing, original_gap)` | A lane carrying an authored 0.4 ms gap **accepts** `s = 1.5` on that pair and **rejects** `s = 0.9`; a 10 ms gap rejects the ratio that would take it to 0.9 ms and accepts the one that lands it exactly on 1.0 ms. Inverting the rule to a flat `spacing` makes the first case fail, which is how the rule was proved to have teeth rather than being a restatement | PASS |
| Event ties as a theorem | A two-key tie and a three-key tie stay bit-identical after a scale, because both members go through one expression from bit-identical inputs; a selected event key landing exactly on an unselected one is **accepted**, matching retime's `0.0` spacing for the family; a selection naming only one member of a tie is **rejected by name** rather than silently widened, at the primitive, at the Agent, and at GUI arm time | PASS |
| Only `time` is written | After a whole-track scale every Transform key's `angle`/`x`/`y`, all four `interpolation` control points, and MAR-171's `curve_mode`/`curve_driver` compare bit-equal; a Slot Color key's four channels and a Deform key's whole `vertex_offsets` vector are unchanged; key order is unchanged index by index; a Draw Order key's `slot_names` and a Slot Attachment key's `attachment_name` survive | PASS |
| Neighbour survival, not just a return code | A subset scale of `{0.5, 1.0}` about `RangeEnd` leaves the unselected key at 0.0 with its time, angle, and easing kind intact and the lane's key count unchanged, and leaves the event lane the call never named with all three of its keys. This is the assertion shape MAR-172's data-loss defect was caught by; the Agent block repeats it with a read-back after the rejected pinned-key scale | PASS |
| MAR-171 interaction | Scaling **every** key of a monotone `Auto` track leaves the resolved control points byte-identical — `resolved_key_count == 0` — because the normalized cubic points depend on *ratios* of Δt; scaling a **subset** changes them, and the resolver reports a non-zero count. The fixture's own `{0, 8, -2}` angles are non-monotone, so the middle tangent clamps to zero and both cases would trivially agree; the test authors `{0, 5, 20}` first so the three-point tangent's dependence on the spacing distribution is actually exercised | PASS |
| MAR-172 interaction | A selection containing an opted-in lane's first key or its managed boundary key is rejected with `is loop synchronized; its first and last keys are pinned…`, at the primitive and at GUI arm time, with no transaction opened; a middle-key-only scale on the same lane commits one entry, moves only the middle keys, leaves the boundary at the duration, and the following `synchronize_loop_boundaries()` reports `synchronized_lane_count == 0`. The pin predicate is **extracted from** MAR-172's own `include_loop_boundary_retime_pins()` and shared, so retime and scale can never disagree about which keys an opt-in freezes | PASS |
| Duration, both directions | An explicit duration of 1.0 with keys at `{0, 0.5, 1.0}` scaled by `s = 2.0` grows to 2.0 through the session's own `auto_extend_explicit_animation_durations()` inside the same history entry; the same animation scaled by `s = 0.5` keeps its explicit duration at 1.0 while the keys move to `{0, 0.25, 0.5}`; an animation with no explicit duration gains none. `scale_keyframe_times()` writes no `AnimationEdit` itself, asserted directly | PASS |
| One gesture, one entry | A complete drag on the late grip commits exactly one history entry; three accepted frames each keep the pivot key at exactly `0.0` and keep `selected_keys` and `active_key` resolving; the same on the early grip with the opposite pivot; a press that never leaves the 4.0 px dead zone opens no transaction and leaves `authoring_gesture_active` false; a zero-net drag returns every key to its original time; `cancel_authoring_gestures()` and Escape both restore `serialize_project()`, `undo_count()`, `redo_count()`, `project_revision()`, `dirty()`, the rebuilt dopesheet `key_times`, **and** the pre-gesture selection | PASS |
| A rejected frame holds the gesture | Dragging inward until two keys collide leaves the gesture **live**, `timeline_scale_rejection()` non-empty, and the project exactly at the last accepted state; dragging back out to a legal ratio clears the rejection and accepts again, and the release commits one entry. Inverting this to retime's cancel-on-error shape fails the case, so the divergence is under test rather than merely documented | PASS |
| Drift is enforced, not argued | 5000 successive `apply_timeline_scale_ratio()` calls sweeping the ratio from 0.6 to 1.4, then a commit: the commit path accepts and every final time is within `kKeyTimeEpsilon` of `pivot + (original - pivot) * applied_scale`. **This check earned its place**: it caught a real defect in the first implementation, where the primitive snapshotted the *selector's* time — which a shell selector carries narrowed to float32 — and so re-rounded every key through float32 once per frame. The primitive now snapshots the stored `double`, and the design's `2·N·u` bound holds | PASS |
| Agent and MCP parity at 60 | `timeline.scale_key_times` sits immediately after `timeline.set_loop_sync` as (`edit`, mutating, not review, dry-run supported); `scale` and `pivot` are both **required**, and `snap` defaults to **false** here while retime's defaults to true, because a scripted ratio is exact; the dry run reports `pivot_time`, `applied_scale`, both spans, and each key's `previous_time`/`time`/`moved` without touching `project_revision()`, `undo_count()`, or `dirty()`; a live call adds exactly one entry; a second identical call returns `no_change`; both pivots produce the documented different results; `snap: true` at 60 fps reshapes `applied_scale` so the moved edge lands on a frame boundary; fifteen rejection cases each leave the project provably unchanged, with `not_found` reserved for the unresolvable selector | PASS |
| The two extractions | `timeline_key_selectors_arg()` moves the whole `timeline.retime_keyframes` key loop into one shared parser and `resolved_key_is_loop_pinned()` / `family_key_spacing()` factor MAR-172's pin and the per-family spacing table out of `include_resolved_retime_bounds()`. Both were gated by the **inverted** test: `marrow_agent_dispatch_smoke`, `marrow_project_smoke`, `marrow_timeline_model_tests`, and the shell smoke each produced byte-identical output before any new behaviour was written | PASS |
| Export, on the MUTATED project | `export_runtime_assets()` runs on the project the validator just scaled. The exported `.mskl`'s three `spine` rotate key times load back as exactly `float32(0.0)`, `float32(0.625)`, and `float32(1.25)`, with all three `angle` values and all three `interpolation` records bit-equal to the pre-scale ones; the exported text contains `0.625`, which appears nowhere in the baseline exported in the same run, and no `loop_sync` or `curve_mode`; the `.mbin` matches the `.mskl`. Deliberately exporting the *unmutated* project instead makes the loaded-time assertion fail before any byte count is consulted, which is the MAR-168/169 defect reproduced and then closed | PASS |
| Compatibility | `.mskl` v1, `.mbin` v2, and C ABI v1 unchanged with a zero-byte diff on `src/runtime/**`, `include/marrow/runtime/**`, `include/marrow/marrow_c.h`, and `src/c_api/**`; `editor-settings.json` v1 unchanged with a zero-byte diff on `src/editor/preferences.cpp` and `include/marrow/editor/preferences.hpp`; a zero-byte diff on `src/editor/session.cpp`; `include/marrow/editor/authoring.hpp` grows by **62 lines with zero removed**; `.marrow` gains no field, so `docs/root1/format-spec.md` needs no change | PASS |

Command output recorded during validation:

- `cmake -S . -B build && cmake --build build -j8` -> configured and built with zero new warnings; `cmake --build build --target marrow_verify_third_party` and `--target marrow_constraint_warning_check` -> both built
- `./build/marrow_timeline_model_tests` -> `Timeline model: 17 cases passed`, up from 16, with one new `scale ratio math` case covering `selection_time_span()` (empty / single / all-same-time / two tracks / unresolvable ref), `scale_from_edge_time()` in both directions and every `nullopt` path, `snap_scale_to_frames()` at 24/30/60 fps for both pivots including the **equivalence assertion against a direct `snap_delta_to_frames()` call**, and `incremental_scale_ratio()` composed over 5000 synthetic frames to within `1e-12`
- `./build/marrow_unit_tests` -> passed; `./build/marrow_timeline_graph_model_tests` -> `Timeline graph model: 20 cases passed`; `./build/marrow_selection_tests` -> `SelectionSet: 8 cases passed`; `./build/marrow_preference_tests` -> `PreferenceStore: 11 cases passed` (unchanged; MAR-173 touches no preference code); `./build/marrow_viewport_interaction_tests` -> passed; `./build/marrow_windowing_tests` -> `Windowing: 4 cases passed`; `./build/marrow_pen_input_tests` -> `Pen input: 34 cases passed`
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed, reporting `MAR-173 scale export: JSON 14341 bytes, MBIN 3984 bytes (baseline JSON 14336 bytes, MBIN 3984 bytes).` and, on its own line, `MAR-173 note: the MBIN size is expected to be unchanged -- key times are fixed-width float32. The acceptance signal is the loaded key time asserted above, not the byte count.`, then `MAR-173 atomic key time scaling validated as reject-not-clamp, pivot-exact, and value-preserving.`
- `./build/marrow_project_smoke --create /tmp/player_idle.marrow` -> `Created minimal project defaults, references, and round trip validated.`; `./build/marrow_parameter_project_smoke assets/fixtures/parameter_face_basic.marrow` -> passed
- `./build/marrow_inspect --compare /tmp/marrow_mar173_scale.mbin /tmp/marrow_mar173_scale.mskl` -> `matches`; `./build/marrow_fixture_smoke /tmp/marrow_mar173_scale.mskl /tmp/player_idle.matl` -> `Generic runtime asset smoke test passed.`; `./build/marrow_renderer_sample /tmp/marrow_mar173_scale.mskl /tmp/player_idle.matl --auto-close 2` -> presented through sokol_gfx; `grep -c loop_sync /tmp/marrow_mar173_scale.mskl` -> `0`; `python3 -m json.tool /tmp/marrow_mar173_scale.mskl` -> valid, `"version": 1`
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> passed, including the new `validate_timeline_scale_shell_smoke` scenario. Like every sibling scenario it prints nothing on success; that it executes was proved by inverting six production behaviours in turn (the dead zone, the frame snap, the non-finite-pointer cancel, the rejected-frame hold, `authoring_gesture_active`, and the arm-time refusal) and observing this scenario's own message fail each time. `--project assets/fixtures/parameter_face_basic.marrow --auto-close 2` -> passed unchanged; `--verify-launch-focus` -> `Verified macOS editor launch focus configuration.`
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` with **324** `[ OK ]` cases (up from 291) against the exact **60**-operation registry, including the new `timeline.scale_key_times` expectation row immediately after `timeline.set_loop_sync`, its dry-run/both-pivots/live/read-back/`no_change`/undo sequence, fifteen rejection cases with a proven-unchanged project, and the loop-pin rejection followed by an authored-key survival read-back
- `tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py` -> compiled
- `tools/mcp/venv/bin/python tools/mcp/test_client.py` against `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` -> `mcp test_client: PASSED` with **60/60** exact C++/Python name parity, the explicit `timeline.scale_key_times` registry metadata row, and a dry-run -> both-pivots -> live -> read-back -> undo -> read-back sequence asserting `0.625` and `1.25` to four decimal places, plus rejection of `"scale": -1`, `"scale": "1.5"`, `"pivot": "middle"`, a missing `pivot`, a missing `scale`, a single-key selection, and a collision — proving the advisory schema did not loosen the C++ gate
- `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only` against `parameter_face_basic.marrow` -> `mcp parameter test_client: PASSED`, unchanged. The MCP tool-removal negative was verified by hand once: deleting the new `types.Tool` makes the client fail on `assert len(mcp_names) == 60`, and it was restored
- `ctest --test-dir build -N` -> `Total Tests: 21`; `ctest --test-dir build --output-on-failure` -> `100% tests passed, 0 tests failed out of 21`; `-L runtime` -> 4/4; `-L editor` -> 11/11; `-R marrow.renderer_link_boundary` -> 1/1. MAR-173 registers no new CTest
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j8 && ctest --test-dir build-display --output-on-failure` -> 24/24; `-L windowing` -> 3/3; `-L display` -> 3/3; `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j8 && ctest --test-dir build-platform-release --output-on-failure` -> 24/24
- Preference isolation proof: `$HOME/Library/Application Support/Marrow` did **not** exist before the run and still did not exist after every gate above, including the four `--agent-port` runs of the production `shell_main.cpp` startup load, which resolves the real path with no override. No `/tmp/marrow-shell-config-*` directory was left behind. The new `validate_timeline_scale_shell_smoke` scenario installs its own `ScopedPreferenceIsolation` even though MAR-173 reads no preference, so it cannot resolve that path at all

**Four divergences from the design, recorded rather than glossed.**

First, **the design's §12.6 export-preview assertion is factually wrong about the
code.** It requires `export.preview` to return *different* payloads before and
after a live scale. `export.preview` reports the resolved export **target
paths**, not the exported content, so a time-only edit correctly leaves its
payload identical; the naive assertion passes only because the response envelope
carries revision metadata, which would make it a test of the envelope rather than
of the operation. The agent smoke now asserts what the operation actually
promises — a scaled project still previews the same targets, before, after, and
across the undo — and the content-level proof stays where it belongs, in
`marrow_project_smoke`'s export block, which exports the mutated project and
asserts the loaded key time.

Second, **MAR-172's pin block was already extracted when MAR-173 started.** The
design's §8.5 describes lifting an *inline* block out of
`include_resolved_retime_bounds()`; as built (`596f0c7`) it was already the
file-local template `include_loop_boundary_retime_pins()`. What MAR-173 extracted
is that helper's *predicate*, so retime and scale share one condition. This is
deliberately **not** unified with `879aadb`'s `managed_boundary_index()`, which
answers a different and stricter question — "does the boundary contract *own*
this key, so may synchronization overwrite it?" — and never names key 0. Using
it for the scale pin would have made scaling disagree with retiming about which
keys an opt-in freezes.

Third, **the primitive snapshots the stored `double`, not the selector's time.**
`resolve_timeline_key()` carries the *selector's* time into
`ResolvedTimelineKey::original_time`, and a shell selector is built from the
runtime track rows, whose times are already narrowed to `float32`. A retime adds
one shared delta and does not care; a scale multiplies, and a live gesture
re-derives its selectors every frame, so reading the narrowed value re-rounded
every key through `float32` once per frame. The commit-time drift check caught
this on the first 5000-frame run rather than a reviewer catching it later, which
is exactly the value §16.12 claimed for it.

Fourth, **two public symbols were added beyond §11.2's three, and one parser
parameter beyond §12.2's one.** `timeline_scale_selection_refusal()` exists so
the GUI can refuse to arm through the primitive's *own* predicates instead of
restating the pin and tie rules a third time in shell code; and
`timeline_key_selectors_arg()` takes a `family_noun` alongside
`operation_label`, because six of the retime parser's messages embed the bare
word "retime" rather than the operation name and a single label cannot reproduce
them byte-identically for both callers. Both additions are what kept the
inverted gates meaningful.

**Review follow-up.** Four findings were raised and closed. The dopesheet
readout re-derived its span from the **already-scaled** tracks each frame, so it
printed `span 0.625s -> 0.781s` from the second frame on where §9.5 specifies
`0.500s -> 0.625s` — a wrong number on the feature's headline affordance that no
headless case could see. The text now comes from `timeline_scale_readout()` in
the controller, derived from the gesture's own pre-drag `pivot_time` and
`edge_original_time`, and the shell smoke asserts it verbatim after three
applied frames and in the RangeEnd direction; restoring the old derivation makes
that case print `span 1.250s -> 1.562s` and fail. `scale_keyframe_times()`'s
Doxygen block had a second block between it and its declaration, so the
primitive carrying the whole reject-not-clamp contract had no generated
documentation; it is reattached. The Agent response's `previous_time` echoed the
**request** rather than reading the project, which made the read-back-after-undo
assertion tautological — its only teeth were resolution failure. `previous_times`
is now carried on `TimelineScaleResult` from the primitive's resolved snapshot,
and a new case asks for `0.5000009` and `0.9999993` — inside the resolver's
one-microsecond identity window — and requires the report to come back as the
stored `0.5` and `1.0`; restoring the echo fails it. **The Agent's snap still
derives its moved edge from caller-supplied times** (`agent_handlers_editing.cpp`),
bounded by that same one-microsecond window and with no accumulation, because
resolving before the snap touches the mutation path and wants its own gate.

**One known limitation, recorded in design §7.4 rather than patched.** The
commit-time drift check compares `float32` track times against a **flat**
`kKeyTimeEpsilon`, while a `float32` ulp doubles with every binade: the enforced
margin is ~8 ulps at 1 s, ~1 ulp at 8 s, and under one above 16 s. It is
fail-safe — it cancels with rollback and can never write a drifted time — and
unreachable with fixtures topping out near 2 s, but a legitimate long-clip drag
could cancel spuriously. Scaling the tolerance with key magnitude changes what
"drifted" means for every clip, so it is a deliberate future decision with its
own long-clip fixture, not a constant edit inside MAR-173.

**The Agent response's 256-entry `keys` cap is safe by construction, not merely
untested.** `keys_truncated` is `selectors.size() > std::min(selectors.size(),
256)`, which is true exactly when the size exceeds 256, and the 4096 cap rejects
before any larger call reaches it, so silent truncation is not expressible. No
case builds a selection larger than 256, matching MAR-172's precedent for its
identical `lanes` cap.

No manual-visible-UI, Windows 11, or physical-input qualification credit is
claimed. MAR-192 through MAR-210 stay open.

## MAR-172 Loop Boundary Key Synchronization Validation Results

Validated 2026-08-30. A Transform, Slot Color, or Deform **lane** can now record
the intent "my last key is the loop boundary", as one optional, additive,
`.marrow`-only boolean projected into a top-level `loop_sync` tree that mirrors
`timeline_edits`. The flag is absent from every existing project, so every
existing project serializes byte-identically and behaves byte-identically — the
sync returns before it reaches MAR-171's resolver when no lane is opted in, and
that is asserted rather than argued. An opted-in lane always carries exactly one
managed key at `float32(explicit duration)` whose value and easing record —
`interpolation` plus MAR-171's `curve_mode`/`curve_driver` — are a bit-exact copy
of that lane's key at time zero, so a looping clip wraps with no pop. The
contract is re-established inside the caller's already-open transaction at the
one seam that is provably after every duration change: immediately after
`auto_extend_explicit_animation_durations()` in `EditorSession::refresh_runtime()`
and `EditorSession::commit()`. Managed identity is **derived, never stored** —
the managed key *is* the lane's last key while the lane is opted in — so no
marker can ride a copy/paste into a lane where it would be a lie, and adoption
is the absence of code. `set_animation_duration()`'s inferred floor and
`auto_extend_explicit_animation_durations()`'s overlay scan both exclude managed
boundary keys, without which an opted-in clip could never be shortened and every
shrink would be undone one line later; both exclusions are bit-exact no-ops for
every animation with no opted-in lane. Draw Order, Event, and Slot Attachment
lanes gain **no member**, so their exclusion is compile-enforced: an Event key at
the boundary would fire twice per loop. The Agent/MCP surface grew by exactly one
operation, `timeline.set_loop_sync`, taking the registry to exactly **59**.
`.mskl` v1, `.mbin` v2, C ABI v1, and `editor-settings.json` v1 are unchanged,
and the flag never enters a runtime file — but the managed boundary key does,
as an ordinary keyframe, which is the entire point. MAR-172 adds **no ImGui
code**: the story's criterion 5 says "the UI-free loop-sync operation".

| Slice | Verification | Result |
| --- | --- | --- |
| Additive, default-absent storage | `serialize_project()` of the untouched fixture contains no `loop_sync` and survives save/reload byte-identically; one opted-in lane serializes exactly `{"animations":{"idle":{"bones":{"spine":{"rotate":true}}}}}` with no `false` leaf and no opted-out lane anywhere in the block; the flag survives save and reload on exactly that lane and on no other; `false` loads as opted out and re-serializes as absence; an unknown top-level member round-trips beside the block through `preserved_root` | PASS |
| Load and save validation | Nine hand-built documents rejected with the exact JSON path and message: a non-object `loop_sync`, a missing `animations`, a non-object `animations`, a non-object animation entry, a string leaf, the unknown channel `spinx`, `slots.body.attachment`, an orphan lane with no `timeline_edits` entry, and a `true` leaf on a lane whose first key is at 0.25. `validate_project_for_save()` re-validates the two structural rules a skeleton-free validator can see and refuses both in memory | PASS |
| Derived identity | The contract owns a lane's last key only when that key satisfies one half of the contract: it already sits at the boundary, or it is still the bit-exact mirror of key 0 that a previous synchronization wrote. A last key satisfying neither is authored data no synchronization produced, so the boundary is **created beside it** rather than promoting it — asserted by removing a boundary key behind the guards and checking that the authored key at 1.0 keeps both its time and its value of −2 while a fourth key appears at 1.5 mirroring key 0 | PASS |
| The boundary contract | Create appends a fourth key at exactly `float32(1.5)` whose `angle` and all four cubic control points compare `==` as `AnimationScalar`, not within an epsilon; adopt overwrites an existing key at the boundary and creates none; move relocates only the boundary key after a duration change; rewrite follows a first-key value change and touches nothing else; a single-key lane becomes a two-key constant lane; two consecutive syncs report `synchronized_lane_count == 0`, `created_key_count == 0`, and a byte-identical project, which is the two-phase termination argument asserted | PASS |
| Fail-closed rejections | No explicit duration, no key at time zero, a duration below the one-millisecond spacing, a key past the boundary, a key crowding the boundary, duplicate keys at the boundary, a duplicate lane selector, an unresolvable lane selector, and an empty lane list each reject atomically with the project byte-identical. **Disabling evaluates no prerequisite and always succeeds**, clears the flag, reports `Released`, and leaves every keyframe in place — which is what makes the atomic rejection humane | PASS |
| Duration, both directions | `set_animation_duration("idle", 1.2)` now **succeeds** on an opted-in clip where it previously failed with `Animation duration cannot be shorter than the last authored key (1.500000 seconds).`; the sync then moves the boundary to `float32(1.2)`; `auto_extend_explicit_animation_durations()` reports `changed == false` and does not undo the shrink; a shrink onto the 1.0005 spacing floor is accepted by the duration primitive and rejected by the sync; a shrink to 0.9 keeps MAR-155's own message verbatim; with no opted-in lane the whole accept/reject table and its exact message strings are unchanged | PASS |
| Managed identity, GUI | Retime pins both ends of an opted-in lane, so a selection containing either collapses to `changed == false` and `applied_delta == 0` while a middle-key selection still moves by the full delta and a lane that is not opted in retimes exactly as before; the preset and curve-mode collectors and the graph value gesture skip a managed boundary key and report `is a managed loop boundary`; removing only the boundary key changes nothing and sets a status message; removing the time-zero key fails the transaction with `serialize_project()` byte-identical; pasting past the boundary fails the transaction byte-identically; a clipboard fragment never carries the flag | PASS |
| Managed identity, Agent | `timeline.set_interpolation`, `timeline.set_curve_mode`, `set_transform`, `set_slot_color_keyframe`, `set_deform_keyframe`, `remove_transform_keyframe`, `remove_slot_color_keyframe`, and `remove_deform_keyframe` each reject a managed boundary key atomically with `That key is a managed loop boundary; edit the key at time 0 of the same timeline instead, or disable loop synchronization on that timeline.`, across all three families. Each rejected removal is followed by a read-back proving the lane's **authored** keys at 1.0 and 0.5 still resolve, and every guarded operation still succeeds on a non-boundary key of the same opted-in lane, so the guard is boundary-specific rather than a lock on the lane. The GUI predicate now delegates to the same project-domain derivation, so the two surfaces cannot disagree | PASS |
| The session seam | Adding a key at 2.0 s on the **not** opted-in `root`/`translate` lane grows `idle`'s explicit duration to 2.0 through the session's own auto-extend **and** moves the opted-in lane's boundary key to 2.0 in the **same** history entry; one undo restores the duration, the added key, and the boundary key together. Before the seam existed this reported `The managed boundary key is stale at 1.5 while the duration is 2`, which is the exact staleness a controller-level wiring cannot close | PASS |
| Criterion 3, five ways | A MAR-168 value drag on key 0, a MAR-170 preset on key 0, a MAR-169 handle drag on key 0, a dopesheet retime of a middle key, and an `animation.set_duration`-equivalent duration edit each stay exactly one history entry with the boundary key re-mirrored inside it; cancelling a value drag on key 0 restores `serialize_project()`, `undo_count()`, `redo_count()`, `project_revision()`, `dirty()`, and the rebuilt dopesheet `key_times`; undo and redo of an enable restore the flag and the boundary key together | PASS |
| The MAR-171 seam | MAR-171's `resolved_key_count == 0` duration assertion is **scoped, not replaced** — its failure message now reads "with no lane opted in" and nothing else about it changed — and is joined by an opted-in case where the boundary key gives key 2 a real outgoing segment and a 1.5 → 2.0 duration change reports `resolved_key_count >= 1`, with the boundary's easing equal to key 0's **post**-resolve value, which is §9.2's phase ordering asserted. The demotion guard asserts both key 0 and the boundary key are still `Auto` after a sync: if phase 2 ever called `set_keyframe_interpolation()` both would read `Manual` | PASS |
| Export, on the MUTATED project | `export_runtime_assets()` runs on the project the validator just mutated. The exported `.mskl` carries `idle`'s explicit duration of 1.5, **four** spine rotate keyframes up from three with keyframe 3 at `time == 1.5f` and `angle` plus all four cubic control points bit-equal to keyframe 0's, and four `body` colour keyframes whose last colour is bit-equal to its first; the exported text contains no `loop_sync`; the `.mbin` matches the `.mskl`. Both files are strictly larger than the baseline measured in the same run — **JSON 15077 vs 14336, MBIN 4125 vs 3984** — and unlike MAR-171 the binary growth is a real signal, because two whole keyframe records were added | PASS |
| Agent and MCP parity at 59 | `timeline.set_loop_sync` sits immediately after `timeline.set_curve_mode` as (`edit`, mutating, not review, dry-run supported); its lane entries carry **no `time`**, the surface's one structural difference from every other `timeline.*` operation; the dry run reports each lane's `previous_enabled`, `previous_boundary` (`null` when no key sat at the boundary time), `boundary_action`, and resulting `boundary` without touching the session; a live call reports `changed_lane_count` and `created_key_count` in one history entry; a second identical call returns `no_change`; the runtime-only `aim`/`arm_l` lane reports `adopted`; `set_transform` on the time-zero key moves the boundary in the same entry; twelve rejection cases each leave the project provably unchanged; disabling a lane in a rejected state succeeds | PASS |
| Compatibility | `.mskl` v1, `.mbin` v2, and C ABI v1 unchanged with a zero-byte diff on `src/runtime/**`, `include/marrow/runtime/**`, `include/marrow/c_api/**`, and `src/c_api/**`; `editor-settings.json` v1 unchanged with a zero-byte diff on `src/editor/preferences.cpp` and `include/marrow/editor/preferences.hpp`; a zero-byte diff on every ImGui translation unit plus `shell_state.hpp` and `shell_core.cpp`, so MAR-172 is UI-free as the story requires; `git diff -- src/editor/session.cpp | rg 'rebuild_runtime'` and `git diff -- src/editor/project.cpp | rg 'build_runtime'` are both empty; `retime_keyframes()`'s signature is unchanged for MAR-173; the three discrete lane structs gain no member | PASS |

Command output recorded during validation:

- `cmake -S . -B build && cmake --build build -j10` -> configured and built with zero new warnings; `cmake --build build --target marrow_verify_third_party` -> `Vendored SDL3, zlib, Dear ImGui, Sokol, sokol_imgui, and sokol-shdc hashes verified`
- `./build/marrow_timeline_model_tests` -> `Timeline model: 16 cases passed`, up from 10, with six new MAR-172 cases: default-off-does-not-resolve and idempotence, create/adopt/move/rewrite plus the single-key lane, never-promotes-an-authored-key, every §6.5 rejection and disable-never-validates, retime pinning, and the excluding floor's bit-exact fast path over every fixture animation
- `./build/marrow_timeline_graph_model_tests` -> `Timeline graph model: 20 cases passed`; `./build/marrow_preference_tests` -> `PreferenceStore: 11 cases passed` (unchanged; MAR-172 touches no preference code); `./build/marrow_selection_tests` -> `SelectionSet: 8 cases passed`; `./build/marrow_viewport_interaction_tests` -> passed; `./build/marrow_windowing_tests` -> `Windowing: 4 cases passed`; `./build/marrow_pen_input_tests` -> `Pen input: 34 cases passed`; `./build/marrow_agent_socket_tests` -> `Agent socket tests: 4 cases passed`
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed, reporting `MAR-172 loop boundary export: JSON 15077 bytes, MBIN 4125 bytes.`, `MAR-172 untouched baseline export: JSON 14336 bytes, MBIN 3984 bytes.`, and `MAR-172 loop boundary synchronization validated as additive, default-absent, export-neutral, and strictly re-validated.` The exported artifact is **741 JSON bytes and 141 MBIN bytes larger** than the baseline measured in the same run, so it demonstrably carries the managed boundary keys rather than the untouched baseline
- `./build/marrow_project_smoke --create /tmp/mar172_created.marrow` -> passed; the created project contains no `loop_sync` member (`grep -c loop_sync` -> `0`)
- `./build/marrow_inspect --compare <export>.mbin <export>.mskl` -> `matches`; `./build/marrow_fixture_smoke <export>.mskl assets/fixtures/player_idle.matl` -> `Generic runtime asset smoke test passed.`; `grep -c 'loop_sync' /tmp/marrow_mar172_loop.mskl` -> `0`
- `python3 -m json.tool assets/fixtures/player_idle.marrow` -> valid, and `git diff --stat -- assets/fixtures/` is empty: the checked-in fixture is untouched by MAR-172
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> passed, including the new `validate_timeline_loop_sync_shell_smoke` scenario; `--project assets/fixtures/parameter_face_basic.marrow --auto-close 2` -> passed unchanged; `--verify-launch-focus` -> `Verified macOS editor launch focus configuration.`
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` with 291 `[ OK ]` cases against the exact **59**-operation registry, including the new `timeline.set_loop_sync` expectation row immediately after `timeline.set_curve_mode` and its dry-run/live/read-back/`no_change`/adoption/duration-move/undo sequence, twelve rejection cases with a proven-unchanged project, and the eight managed-boundary guards with their authored-key survival read-backs
- `tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py` -> compiled
- `tools/mcp/venv/bin/python tools/mcp/test_client.py` against `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` -> `mcp test_client: PASSED` with **59/59** exact C++/Python name parity, the explicit `timeline.set_loop_sync` registry metadata row, and a duration -> dry-run -> live -> read-back -> `set_transform` -> read-back -> undo -> read-back sequence asserting `boundary_time == 1.5` and the mirrored angle to four decimal places, plus rejection of `"enabled": "yes"`, of a `draw_order` lane, and of enabling a clip with no explicit duration. A lane entry carrying an undeclared `time` member still succeeds, proving the advisory schema did not loosen the C++ gate
- `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only` against `parameter_face_basic.marrow` -> `mcp parameter test_client: PASSED`, unchanged
- `ctest --test-dir build -N` -> `Total Tests: 21`; `ctest --test-dir build --output-on-failure` -> `100% tests passed, 0 tests failed out of 21`; `-L runtime` -> 4/4; `-L editor` -> 11/11; `-R marrow.renderer_link_boundary` -> 1/1. MAR-172 registers no new CTest
- `./build/marrow_c_smoke` -> `commands=3, indices=6, callbackEvents=2`; `./build/marrow_spine_import_smoke ...` -> passed; `./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl|.mbin assets/fixtures/player_idle.matl` -> both passed; `./build/marrow_parameter_project_smoke` and `./build/marrow_atlas_packer_smoke` -> passed, unchanged
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j10 && ctest --test-dir build-display --output-on-failure` -> automated Debug display-enabled suite 24/24, including 3 windowing-labelled and 3 display-labelled tests, passed in 19.47 s
- Preference isolation proof: `$HOME/Library/Application Support/Marrow` did **not** exist before the run and still did not exist after every gate above, including the three `--agent-port` runs of the production `shell_main.cpp` startup load, which resolves the real path with no override. No `/tmp/marrow-shell-config-*` directory was left behind. The new `validate_timeline_loop_sync_shell_smoke` scenario installs its own `ScopedPreferenceIsolation` even though MAR-172 reads no preference, so it cannot resolve that path at all

**Three divergences from the design, recorded rather than glossed.**

First, **the design's §18.4 assumption that an opted-in 1.5 -> 2.0 duration change
resolves at least one key is false for the fixture's own angles.** With spine
rotate at `angle = 0, 8, -2` and a boundary mirroring key 0, the last two secants
have opposite signs, so `curve_auto`'s monotonicity clamp zeroes the interior
tangent and the normalized control points `[1/3, a/3, 2/3, 1 - b/3]` come out
span-invariant: `a = 0`, `b = 1` at every duration. The assertion is real and is
kept; the test seeds `0, -6, -3` instead, which keeps those two secants the same
sign so the automatic curve genuinely depends on the boundary key's time. The
substituted angles are commented at the call site with that reason.

Second, **the plan's Task 3 instruction to "leave the project-overlay
`include_animation_timeline_maximum()` calls in place" would have defeated the
exclusion it was introducing**: those calls fold the boundary key back into the
floor from the project side one line after the effective side excluded it. The
three continuous families now call a boundary-excluding overload instead, and the
three discrete families keep the original helper — which is a bit-exact no-op
when no lane is opted in, because the overload differs only in skipping a last
key whose `loop_sync` is false.

Third, **the first implementation shipped the GUI half of §10.1 and silently
dropped the Agent half**, which the plan's Task 6 had narrowed to the
controller. A review reproduced the consequence live: `timeline.set_interpolation`
on a managed boundary key returned `no_change` — the exact "the command appears
to do nothing and nothing explains why" outcome §10.1 exists to prevent — and
`remove_transform_keyframe` at the boundary time returned `ok: true` while
**destroying the authored key at 1.0**, because the synchronization then promoted
that key into the vacated boundary, moving it to 1.5 and overwriting its value
from key 0. `remove_slot_color_keyframe` and `remove_deform_keyframe` shared the
hole. The fix is the spec's own remedy — atomic rejection with §10.1's message
across all eight write and removal paths of all three families — plus a
derivation change that makes the promotion itself unreachable: the sync now
treats a lane's last key as the contract's only when it satisfies one half of the
contract, so a key that is neither at the boundary nor still a mirror of key 0 is
authored data the sync creates beside rather than overwrites. The GUI predicate
delegates to the same derivation, so no surface can disagree with the mutation
about which key is derived.

The display suites are automated evidence only. This checkpoint adds no manual
visible-UI, Windows 11, physical-input, or platform qualification credit.
MAR-192 through MAR-210 remain open, and support qualification remains governed
by `docs/root1/platform-validation.md`.

## MAR-171 Project-Local Automatic Curve Handles Validation Results

Validated 2026-08-30. A Transform or Slot Color keyframe can now record the
intent "my outgoing easing is whatever a monotone interpolant through my
neighbours says it should be", as two optional, additive, `.marrow`-only
members — `curve_mode` (`manual` | `auto`) and `curve_driver` (which scalar
series drives the computation). Both are absent from every existing project, so
every existing project keeps today's manual behaviour byte for byte. An
automatic key's easing is resolved eagerly into the pre-existing `curve` field
by a pure monotone **Fritsch–Carlson** interpolant living in the new
`ProjectData`-free translation unit `src/editor/curve_auto.cpp`, and it is
recomputed inside the *same transaction* as the neighbour-time, neighbour-value,
insertion, deletion, paste, retime, gizmo-drag, Inspector-field, or duration
change that invalidated it — one edit, one history entry. Writing any absolute
easing demotes the key to manual, and the rule lives inside
`set_keyframe_interpolation()` itself so MAR-169's handle drag, MAR-170's
presets, the numeric inspector, and the Agent all inherit it and none can forget
it. The registry grows to exactly **58** operations with `timeline.set_curve_mode`
and its matching MCP tool. `.mskl` v1, `.mbin` v2, C ABI v1, and
`editor-settings.json` v1 are unchanged, and neither new field ever reaches a
runtime file.

**The algebra removes the format risk.** Normalizing a cubic Hermite segment to
the unit square gives `cx1 = 1/3` and `cx2 = 2/3` *identically* — not by
clamping — so `X(t) = t` exactly, the runtime's inverse is the identity, and the
`cx ∈ [0, 1]` invariant both loaders enforce holds unconditionally before and
after `float32` narrowing. Fritsch–Carlson's `a² + b² ≤ 9` bounds `a, b ∈ [0, 3]`,
so `cy1 = a/3` and `cy2 = 1 − b/3` are both in `[0, 1]`: an automatic curve can
**never overshoot**. Overshoot in Marrow stays reachable only through MAR-169's
manual handle drag.

| Slice | Verification | Result |
| --- | --- | --- |
| Algorithm and the `cx` invariant | Every returned entry has `cx1 == 1.0/3.0` and `cx2 == 2.0/3.0` bit-exactly and both narrow strictly inside `(0, 1)`; the fixture's `spine` rotate series `(0,0),(0.5,8),(1,-2)` resolves to exactly `[1/3, 1/3, 2/3, 1]` and `[1/3, 0, 2/3, 2/3]` to `1e-12`; two samples give exactly `[1/3, 1/3, 2/3, 2/3]`; zero and one sample give an empty vector rather than an error; a `static_assert` pins both constants | PASS |
| Monotonicity, flatness, and no overshoot | Sampling `runtime::Interpolation::cubic_bezier(...).transform(alpha)` over a 101-point grid on every segment of the spiky series `{0, 10, 0.5, 11, 0}` is finite, non-decreasing within `1e-6`, inside `[0, 1]`, and exactly `0.0`/`1.0` at the endpoints; a flat segment resolves to the neutral `[1/3, 1/3, 2/3, 2/3]` and a plateau still zeroes the following segment's shared tangent; `{5,5,5}` gives two exactly linear segments; the disk clamp fires on `{0, 1, 1.0001}`; `{0, 1e-300, 1e300}` still returns finite in-range points; scaling every time by 1000 and every value by −7 is bitwise identical; two calls on one input are bitwise identical; a non-finite time, a non-finite value, a non-increasing pair, and a `1e-7` s segment each reject the whole track | PASS |
| Additive, default-absent `.marrow` storage | The untouched fixture serializes with neither `curve_mode` nor `curve_driver` and round-trips byte-identically; an automatic key writes the pair exactly once each while a manual key with a non-default driver in memory writes neither; both fields survive save and reload on Transform and Slot Color keys; six malformed documents are each rejected with the exact keyframe-scoped JSON path and message — a numeric `curve_mode`, `"automatic"`, a numeric `curve_driver`, `"z"`, `"x"` on a rotate key, and a driver on a manual key; `validate_project_for_save()` re-rejects an `Angle` driver on a slot-colour key | PASS |
| Same-transaction recomputation | A graph value drag, an `add_timeline_key_at_playhead()` between two automatic keys, a `remove_selected_timeline_keys()` on the middle key, and a dopesheet retime each add exactly **one** history entry with the automatic curves already updated inside it, asserted against literal expected control points for the post-edit neighbourhood; a paste carries the copied mode and driver and resolves against its **new** neighbours; a Deform-only selection opens no transaction; re-applying the same mode and driver adds no history entry and leaves the bytes identical; a resolver rejection cancels the enclosing transaction, rolling back its materialization too. The duration seam is wired on **both** surfaces — the shell's `apply_animation_duration_gesture()` and the Agent's `animation.set_duration` — and is honestly a no-op today: with consistent automatic curves a duration change leaves every stored easing byte-identical, while a deliberately stale mode/curve pair is reconciled by it, which is what proves the call is really there | PASS |
| Demotion, and why it is not MAR-169's rule | `set_keyframe_interpolation()` sets `Manual` on every key it writes and reports `changed == true` **even when the four control points are byte-identical**, because the demotion is itself the authored change; a drag away and exactly back on an automatic key therefore commits one entry with byte-identical points, while the same round trip on a manual key still commits **zero** entries, preserving MAR-169's net-state rule; a MAR-170 preset on an automatic key demotes it in the preset's own single transaction; the two paths that write `interpolation` directly rather than through the primitive — the numeric `Bezier X1..Y2` inspector and the Agent's `set_slot_color_keyframe` — each carry the same one-line demotion. `set_slot_color_keyframe` demotes only when the caller **explicitly supplies** an `interpolation`, because `interpolation_arg()` returns Linear for an absent member: an explicit curve must survive the resolver that runs after it, while a colour-only write must leave an automatic key automatic and re-resolve against the value it just changed | PASS |
| Agent and MCP parity at 58 | `timeline.set_curve_mode` sits immediately after `timeline.set_interpolation` as (`edit`, mutating, not review, dry-run supported); its dry run reports each key's `previous_mode`, `previous_driver` (`null` for a manual key), and `previous_interpolation` without touching the session; the live call reports `changed_key_count` and `resolved_key_count` in one history entry; a second identical call returns `no_change`; ten rejection cases — missing `mode`, `"automatic"`, an unknown driver, a driver with `"manual"`, a driver the family does not own, a `deform` key, a `draw_order` key, a duplicate selector, an empty `keys` array, and an unresolvable selector (`not_found`) — each leave the project provably unchanged; the demotion is proven through the Agent surface by a follow-up dry run reading `previous_mode == "manual"` | PASS |
| Export neutrality, on the mutated project | The smoke exports the project it just authored — automatic on `spine` rotate keys 0 and 1 and on `arm_l` rotate key 0 — and asserts the exported `.mskl` carries `cx1 == float(1/3)`, `cy1 == float(1/3)`, `cx2 == float(2/3)`, `cy2 == 1.0f` on `spine` rotate key 0, that `arm_l` key 0 exports a 4-number array where the fixture stores the string `"linear"`, that reading the file **as text** finds neither `curve_mode` nor `curve_driver`, and that the JSON is strictly larger than a baseline exported in the same run. Pointing the same block at the untouched baseline makes assertion 3 fail before any byte count is consulted, which was verified by inverting it | PASS |
| Compatibility | `.mskl` v1, `.mbin` v2, and C ABI v1 unchanged with a zero-byte diff on `src/runtime/**`, `include/marrow/runtime/**`, `include/marrow/c_api/**`, and `src/c_api/**`; `editor-settings.json` v1 unchanged with a zero-byte diff on `src/editor/preferences.cpp` and `include/marrow/editor/preferences.hpp` and no new preference field; `DeformKeyframeEdit` gains no member, so the Deform exclusion is compile-enforced; no field is added on the timeline-edit (lane) object, leaving that namespace unclaimed for MAR-172 | PASS |

Validated commands and outputs:

- `cmake -S . -B build && cmake --build build -j10` -> configure and all default targets built; `cmake --build build --target marrow_verify_third_party` -> vendored SDL3, zlib, Dear ImGui, Sokol, sokol_imgui, and sokol-shdc hashes verified; `cmake --build build --target marrow_constraint_warning_check` -> built
- `./build/marrow_timeline_model_tests` -> `Timeline model: 10 cases passed`, including the new `automatic curve control points` case. Its teeth were proven three ways by inverting production code: replacing the flat-segment convention with `[1/3, 0, 2/3, 1]` fails 2 assertions, disabling the disk clamp fails 2, and evaluating the clamp as `sqrt(a*a + b*b)` on the raw tangent ratio instead of `std::hypot` on the tangents fails the extreme-ratio case
- `./build/marrow_timeline_graph_model_tests` -> `Timeline graph model: 20 cases passed`; `./build/marrow_preference_tests` -> `PreferenceStore: 11 cases passed` (unchanged; MAR-171 touches no preference code); `./build/marrow_selection_tests` -> `SelectionSet: 8 cases passed`; `./build/marrow_viewport_interaction_tests` -> passed; `./build/marrow_windowing_tests` -> `Windowing: 4 cases passed`; `./build/marrow_pen_input_tests` -> `Pen input: 34 cases passed`; `./build/marrow_unit_tests` -> all runtime and renderer unit tests passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed, reporting `MAR-171 auto export: JSON 14631 bytes, MBIN 4056 bytes (baseline JSON 14336 bytes).` and `MAR-171 automatic curve storage validated as additive, default-absent, and strictly re-validated.` The exported artifact is **295 bytes larger** than the baseline measured in the same run, so it demonstrably carries the authored automatic curves rather than the untouched baseline
- `./build/marrow_project_smoke --create /tmp/player_idle.marrow` -> minimal project defaults, references, and round trip validated; `./build/marrow_parameter_project_smoke assets/fixtures/parameter_face_basic.marrow` -> passed; `python3 -m json.tool` on both fixtures -> parsed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> passed, including the new headless `validate_timeline_curve_mode_shell_smoke` scenario and the actual-frame curve-mode frames, which reported `Timeline Graph actual-frame curve mode: Auto button=(499,698.5) driver=7`. The actual-frame case clicks the reported `Auto` button with real ImGui mouse events and asserts one history entry plus a stored curve whose `cx` pair is exactly `float(1/3)`/`float(2/3)`, then presses the reported auto handle, moves 30 px, releases, and asserts the segment demoted in exactly one more entry; with nothing selected the row is drawn-but-disabled and a click on it changes nothing; MAR-167/168/169/170's `fit_*`, `first_component_*`, `first_preset_*`, and plot rectangles are all still non-degenerate
- `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2` -> parameter-mode shell smoke passed; `./build/marrow_editor_shell --verify-launch-focus` -> verified
- `ctest --test-dir build -N` -> `Total Tests: 21`; `ctest --test-dir build --output-on-failure` -> `100% tests passed, 0 tests failed out of 21`; `-L runtime` -> 4/4; `-L editor` -> 11/11; `-R marrow.renderer_link_boundary` -> 1/1. MAR-171 registers no new CTest
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j10 && ctest --test-dir build-display --output-on-failure` -> automated Debug display-enabled suite `100% tests passed, 0 tests failed out of 24` in 14.01 s, including the 3 display-only tests; `-L windowing` -> 3/3; `-L display` -> 3/3
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j10 && ctest --test-dir build-platform-release --output-on-failure` -> automated Release display-enabled suite `100% tests passed, 0 tests failed out of 24` in 7.30 s
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar171.mskl --export-binary /tmp/marrow_mar171.mbin` -> export passed; binary errors `rotation=0.00274662deg`, `position=0.000811016px`. This CLI leg exports the unedited fixture, so its JSON `14336` / MBIN `3984` are the untouched baseline by construction; the automatic-curve-carrying export is the `/tmp/marrow_mar171_auto.*` pair
- `./build/marrow_inspect --compare /tmp/marrow_mar171_auto.mbin /tmp/marrow_mar171_auto.mskl` -> `Comparison: /tmp/marrow_mar171_auto.mskl matches /tmp/marrow_mar171_auto.mbin`; JSON `14631` bytes, MBIN v2 `4056` bytes with `version=2 optimized=yes animations=3 rotate_channels=4 translate_channels=2 keys=16 sorted=yes`
- `rg -c 'curve_mode|curve_driver' /tmp/marrow_mar171_auto.mskl` -> **no match**, the direct proof of export neutrality; `wc -c /tmp/marrow_mar171_auto.mskl /tmp/marrow_mar171_export_baseline.mskl` -> `14631` vs `14336`; `python3 -m json.tool /tmp/marrow_mar171_auto.mskl` -> parsed; `./build/marrow_fixture_smoke /tmp/marrow_mar171.mskl /tmp/player_idle.matl` -> passed
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` with 227 `[ OK ]` cases against the exact **58**-operation registry, including the new `timeline.set_curve_mode` expectation row immediately after `timeline.set_interpolation` and its dry-run/live/read-back/`no_change`/demotion/undo sequence plus ten rejection cases with a proven-unchanged project
- `./build/marrow_agent_socket_tests` -> `Agent socket tests: 4 cases passed`; `./build/marrow_c_smoke` -> C ABI loaded 3 commands, 6 indices, and 2 callback events
- `tools/mcp/venv/bin/python tools/mcp/test_client.py` against `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` -> `mcp test_client: PASSED` with **58/58** exact C++/Python name parity, the explicit `timeline.set_curve_mode` registry metadata row, and a dry-run/live/read-back/undo sequence asserting the resolved `[0.3333, 0.3333, 0.6667, 1.0]` at four decimal places plus rejection of `"automatic"`, of an `angle` driver on a `slot_color` key, of a `deform` key, and of a driver supplied with `"manual"`. Removing the new MCP tool makes the client fail on `assert len(mcp_names) == 58`, which was verified
- `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only` against `parameter_face_basic.marrow` -> `mcp parameter test_client: PASSED`; MCP schema `py_compile` -> passed
- `git diff --stat -- include/marrow/c_api src/c_api`, `-- src/runtime include/marrow/runtime`, `-- src/editor/preferences.cpp include/marrow/editor/preferences.hpp`, and `-- src/tests/preference_store_tests.cpp` -> **all empty**; `./build/marrow_inspect assets/fixtures/player_idle.mbin` -> `version=2`; `git diff --check` -> clean; `git lfs status` -> no LFS object staged or queued to push (the tracked patterns are `*.png`, `*.psd`, `*.otf`, `*.mbin`; no modified file matches one)
- Preference isolation proof: `$HOME/Library/Application Support/Marrow` did **not** exist before the run and still did not exist after every gate above, including the three `--agent-port` runs of the production `shell_main.cpp` startup load, which resolves the real path with no override. The new `validate_timeline_curve_mode_shell_smoke` scenario installs its own `ScopedPreferenceIsolation` even though MAR-171 reads no preference, so it cannot resolve that path at all
- Post-review fixes, each RED-first. The shell's duration gesture did not call the resolver even though the design lists it as a trigger and MAR-172 builds on that seam; it now does, and removing the call again makes the shell smoke report `The shell duration gesture did not resolve automatic curves`. `set_slot_color_keyframe` wrote `interpolation` on an existing key without demoting, so the resolver that follows silently reverted an agent-supplied curve and the call returned `no_change`; it now demotes on an explicitly supplied easing, and demoting unconditionally instead makes the agent smoke report `a colour-only write must leave an automatic key automatic`

**One divergence from the design, found by the Task 4 export test and recorded
rather than glossed.** The design's §8.6 assumed `build_runtime_document()`
builds transform keyframes through a `build_runtime_*` function of its own, as
it does for slot colour, and the plan therefore forbade any change to the export
path. It does not: the `.marrow` and `.mskl` transform keyframe shapes were
identical, so the export **reused the project serializer**
`build_transform_keyframes_value()`. The first run of the export test caught
this immediately — `MAR-171 leaked a project-local field into the runtime
export` — and the fix is a dedicated `build_runtime_transform_keyframes_value()`
that emits a fixed member list, exactly mirroring
`build_runtime_slot_color_keyframes_value()`. The export path is now
structurally incapable of carrying a project-local keyframe field, which is what
§8.6 claimed all along. `git diff -- src/editor/project.cpp | rg 'build_runtime'`
is therefore **not** empty, contrary to the plan's gate; the four hits are that
new function and its single call site.

The display suites are automated evidence only. This checkpoint adds no manual
visible-UI, Windows 11, physical-input, or platform qualification credit.
MAR-192 through MAR-210 remain open, and support qualification remains governed
by `docs/root1/platform-validation.md`.

## MAR-170 Fixed Curve Presets and Remembered Defaults Validation Results

Validated 2026-08-30. Six fixed presets — Linear, Stepped, Ease
`[0.25, 0.1, 0.25, 1]`, Ease-In `[0.42, 0, 1, 1]`, Ease-Out `[0, 0, 0.58, 1]`,
and Ease-In-Out `[0.42, 0, 0.58, 1]`, the CSS Easing Level 1 timing functions —
are declared once as the `constexpr kCurvePresets` table in
`include/marrow/editor/authoring.hpp` and applied to every compatible selected
key through MAR-169's single `set_keyframe_interpolation()` primitive, from a
shared row drawn in both the Graph and Dopesheet toolbars. One application is
one `EditTransaction`, one previewed `refresh_runtime()`, and one history entry
whatever the key count; easing-free Draw Order, Event, and Slot Attachment
selections are skipped and reported by the GUI and still rejected atomically by
the Agent. MAR-156's never-consumed `editor-settings.json` `default_curve` field
gains its consumer: the remembered default seeds newly authored Transform,
Deform, and Slot Color keys — from "Add Key At Playhead", from a viewport FFD
vertex drag, from a viewport translate/rotate/scale gizmo drag, and from the
Inspector transform fields alike — and is changed only by the explicit
`Default:` combo, never as a side effect of applying a preset. The current-preset readout
is a pure bit-exact `float32` function of the stored curve with no persisted
marker, so it reads `Custom Bezier` the moment a MAR-169 handle drag moves away
from a preset and needs no invalidation under undo, redo, or reload. The
Agent/MCP surface stayed at exactly **57** operations: the growth was four new
string tokens in one operation's `interpolation` argument vocabulary. Automatic
and project-local curve handles remain MAR-171.

| Slice | Verification | Result |
| --- | --- | --- |
| Preset constants | The six presets are Linear, Stepped, Ease `[0.25, 0.1, 0.25, 1]`, Ease-In `[0.42, 0, 1, 1]`, Ease-Out `[0, 0, 0.58, 1]`, Ease-In-Out `[0.42, 0, 0.58, 1]`; a `static_assert` keeps the table in enum order and every `cx` inside `[0, 1]`, re-asserted at runtime before and after `float32` narrowing; each cubic evaluates finite, non-decreasing, overshoot-free, and exactly `0.0`/`1.0` at the endpoints over a 101-point grid, including Ease-In's `X'(1) = 0` right endpoint | PASS |
| Preset application | Applying to compatible selected keys is order-independent — the same twelve selectors reversed serialize byte-identically — skips and counts easing-free keys, collapses a duplicate ref instead of turning it into the primitive's hard error, materializes runtime-only tracks, and produces exactly one previewed undoable transaction; re-applying commits nothing and reports `Selected keys already use <name>`; an easing-free-only selection opens no transaction at all | PASS |
| Remembered default | `default_curve` seeds newly authored Transform, Deform, and Slot Color keys, and a `Stepped` default seeds a Stepped key; all five shell creation paths agree — "Add Key At Playhead", the FFD vertex drag, the three viewport gizmo drags, and the Inspector transform fields — because the shared `upsert_transform_keyframe()`/`upsert_deform_keyframe()` primitives take the seed as an argument that defaults to Linear, so `marrow_editor` never reads a preference and the Agent keeps its reproducible Linear default; the seed initializes an inserted key only and never rewrites the curve of a key that already exists; pasted keys keep the copied curve; it is stored atomically in `editor-settings.json` v1 with `recent_projects` and unknown additive fields preserved; missing, malformed, unsupported-version, and unreadable data fall back to Linear, rewrite no curve, and rewrite no file | PASS |
| Project isolation | Changing and saving the default leaves `serialize_project()`, `dirty()`, `undo_count()`, `redo_count()`, and `project_revision()` byte-identical; `MARROW_CONFIG_HOME` isolates every smoke process, and the real user settings directory did not exist before or after the entire validation run, including two runs of the production `shell_main.cpp` load path against the real resolved path | PASS |
| Curve identity | The readout is a pure bit-exact `float32` function of the stored curve — comparing against the `double` literals instead makes all four cubic presets read `Custom`, which was proven by inverting the implementation; a preset survives save, reload, `.mskl` export, and the v2 `.mbin` as the same preset; a handle drag makes it read `Custom`; a preset followed by a drag is exactly two undo entries and one undo restores the preset | PASS |
| Agent and MCP parity | `timeline.set_interpolation` accepts `ease`, `ease_in`, `ease_out`, and `ease_in_out` with matching C++/Python dry-run, validation, affected-key, mutation, and undo behaviour; `ease-in`, `easeIn`, and `bounce` stay rejected with the project byte-identical; the registry stayed at **57** operations and `interpolation_arg()`, `set_transform`, `set_deform_keyframe`, `set_slot_color_keyframe`, and `agent_handlers_editing.cpp` are unchanged | PASS |
| Compatibility | `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1 (`kEditorSettingsVersion` still `1`), and `SelectionSet` unchanged; only the pre-existing `curve` and `default_curve` fields are written, `src/editor/preferences.cpp` and `include/marrow/editor/preferences.hpp` show a zero-byte diff, and the `authoring.hpp` diff is 61 added lines with 0 removed | PASS |

Validated commands and outputs:

- `cmake -S . -B build && cmake --build build -j8` -> configure and all default targets built
- `./build/marrow_preference_tests` -> `PreferenceStore: 11 cases passed`, including the new `fixed curve preset constants, identity, and well-posedness`, `preset tokens agree with the settings-file vocabulary`, and `shell preference session load, save, fallback, and preservation` cases
- `./build/marrow_timeline_graph_model_tests` -> `Timeline graph model: 20 cases passed`; `./build/marrow_timeline_model_tests` -> `Timeline model: 9 cases passed`; `./build/marrow_viewport_interaction_tests` -> `Viewport interaction kernel tests passed.`; `./build/marrow_selection_tests` -> `SelectionSet: 8 cases passed`
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed; `MAR-170 fixed curve presets validated across transform, deform, and slot-colour families` covering all six presets on a Transform, a Deform, and a Slot Color key with byte-identical scalars, the exactly-one-`interpolation`-field segment-wide assertion, twelve-selector order independence, the double-application no-op, the three easing-free atomic rejections, a save/reload preset-identity round trip, and `upsert_transform_keyframe()`'s seeding contract: no argument inserts Linear (the Agent's contract), an explicit seed inserts that preset (the shell's), and an existing key keeps its own curve while its value is updated
- The same smoke exports the preset-edited project and reports `MAR-170 preset export: JSON 14452 bytes, MBIN 4020 bytes`, asserting the exported runtime JSON carries the narrowed Ease-In-Out quadruple on the `arm_l` rotate key at `t = 0.25` and that `curve_preset_of()` still names it Ease-In-Out there. The fixture's own value at that key is `"curve": "linear"`, and the exported file reads `[0.419999986886978, 0, 0.579999983310699, 1]`, so the exported artifact demonstrably carries the authored edit rather than the untouched baseline `14336`/`3984`
- `./build/marrow_project_smoke --create /tmp/player_idle.marrow` -> minimal project defaults, references, and round trip validated
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> passed, including the new headless `validate_timeline_curve_preset_shell_smoke` scenario, the FFD smoke's new case asserting a vertex-drag-authored Deform key at `t = 0.62` carries the remembered `Ease-Out` while the fixture's own `t = 1.0` key keeps its imported `stepped` curve, the viewport smoke's new case asserting a rotate-gizmo-authored `root` key at `t = 0.37` carries the remembered `Ease-In-Out`, and the actual-frame preset frames, which reported `Timeline Graph actual-frame presets: first button=(405,698.5) default=4`. The actual-frame case clicks the reported first preset button with real ImGui mouse events, asserts one history entry and the settled `Linear` readout, asserts the row is drawn-but-disabled with an empty selection, that hovering its *first* button still submits a guidance tooltip in that frame, and that a click on it changes nothing, and asserts MAR-167/168's `fit_*` and `first_component_*` rectangles are still non-degenerate and still hoverable
- `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2` -> parameter-mode shell smoke passed
- `ctest --test-dir build -N` -> `Total Tests: 21`; `ctest --test-dir build --output-on-failure` -> `100% tests passed, 0 tests failed out of 21` in 2.14 s. MAR-170 registers no new CTest.
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j8 && ctest --test-dir build-display --output-on-failure` -> automated Debug display-enabled suite `100% tests passed, 0 tests failed out of 24`, including the 3 display-only tests `marrow.window_host_smoke`, `marrow.gpu_parity_smoke`, and `marrow.editor_display_smoke`
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j8 && ctest --test-dir build-platform-release --output-on-failure` -> automated Release display-enabled suite `100% tests passed, 0 tests failed out of 24`, same 3 display-only tests
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar170.mskl --export-binary /tmp/marrow_mar170.mbin` -> export passed; binary errors `rotation=0.00274662deg`, `position=0.000811016px`. This CLI leg exports the unedited fixture, so its JSON `14336` / MBIN `3984` are the untouched baseline by construction; the preset-carrying export is the `/tmp/marrow_mar170_preset.*` pair below
- `./build/marrow_inspect --compare /tmp/marrow_mar170.mbin /tmp/marrow_mar170.mskl` -> `Comparison: /tmp/marrow_mar170.mskl matches /tmp/marrow_mar170.mbin`; `rotate_keys=10->10`, `translate_keys=6->6`, JSON `14336` bytes, MBIN v2 `3984` bytes with `version=2 optimized=yes animations=3 rotate_channels=4 translate_channels=2 keys=16 sorted=yes`
- `./build/marrow_inspect --compare /tmp/marrow_mar170_preset.mbin /tmp/marrow_mar170_preset.mskl` -> match on the preset-edited export; `rotation_error=0.00274662deg`, `position_error=0.000811016px`, `rotate_keys=10->10`, `translate_keys=6->6`, JSON `14452` bytes, MBIN v2 `4020` bytes with `version=2 optimized=yes animations=3 rotate_channels=4 translate_channels=2 keys=16 sorted=yes`
- `./build/marrow_fixture_smoke /tmp/marrow_mar170.mskl /tmp/player_idle.matl` and `./build/marrow_fixture_smoke /tmp/marrow_mar170_preset.mskl /tmp/player_idle.matl` -> both passed with 16 bones, 7 slots, 5 skins, 3 animations, 2 events, 3 draw commands, and 1 clip
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` with 203 `[ OK ]` cases against the **still exactly 57**-operation registry, including the four new preset-token write/read-back/undo sequences, the `stepped` token round trip, the three rejected spellings with a proven-unchanged project, the multi-key single-entry preset call whose one undo restores both keys, and `set_transform` still creating a Linear key
- `./build/marrow_agent_socket_tests` -> `Agent socket tests: 4 cases passed`; `./build/marrow_c_smoke` -> C ABI loaded 3 commands, 6 indices, and 2 callback events
- `tools/mcp/venv/bin/python tools/mcp/test_client.py` against `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` -> `mcp test_client: PASSED` with 57/57 exact C++/Python name parity and the `ease_in_out` dry-run/live/read-back/undo sequence asserting `[0.42, 0.0, 0.58, 1.0]` at four decimal places, plus rejection of `ease-in` and of an out-of-range array proving the widened schema did not loosen the C++ gate
- MCP schema `py_compile`, `cmake --build build --target marrow_verify_third_party`, fixture/`.mskl`/PRD JSON parsing, `git diff --check`, and `git lfs status` -> passed; no LFS object was staged or queued to push
- `git diff --stat -- include/marrow/c_api src/c_api`, `git diff --stat -- src/editor/project.cpp src/runtime/skeleton_parse.cpp src/runtime/binary.cpp`, and `git diff --stat -- src/editor/preferences.cpp include/marrow/editor/preferences.hpp` -> all empty; `git diff --numstat -- include/marrow/editor/authoring.hpp` -> `61 0`; the `src/editor/agent_dispatch.cpp` diff touches only `interpolation_request_arg()`'s string branch and its error string, and the `tools/mcp/tools/editing.py` diff touches only `_bezier_interpolation_schema()`
- Preference isolation proof: `$HOME/Library/Application Support/Marrow` did **not** exist before the run and still did not exist after every gate above, including the two `--agent-port` runs of the production `shell_main.cpp` startup load, which resolves the real path with no override. No `/tmp/marrow-shell-config-*` isolation directory was left behind

The display suites are automated evidence only. This checkpoint adds no manual
visible-UI, Windows 11, physical-input, or platform qualification credit.
MAR-192 through MAR-210 remain open, and support qualification remains governed
by `docs/root1/platform-validation.md`.

## MAR-169 Graphical Shared Bezier Handle Editing Validation Results

Validated 2026-08-30. The Timeline Graph tab now edits the active key's outgoing
shared `[cx1, cy1, cx2, cy2]` easing directly. A left press on a drawn handle
arms the same MAR-168 drag candidate, holding no transaction, and the first
motion past the shared inclusive 4.0 logical-pixel dead zone opens one
`EditTransaction`. A handle drag is free 2-D: `decide_drag_axis()` is
deliberately not called, because `cx` and `cy` are two parameters of one curve
written by one primitive. `cx` is clamped into `[0, 1]` by the pure pointer
mapping so a drag past the boundary stops there and stays live, while the new
additive `set_keyframe_interpolation()` primitive independently *rejects* any
out-of-range or non-finite value atomically; finiteness is tested before range,
because `NaN < 0` and `NaN > 1` are both false. Finite `cy` overshoot is
preserved. Grabbing a Linear or Stepped handle converts the segment to Cubic
seeded at `[1/3, 1/3, 2/3, 2/3]` inside the same transaction and the same undo
entry. The identical mutation is exposed as the 57th Agent operation
`timeline.set_interpolation` and as one matching MCP tool, both calling the same
primitive. Curve presets and remembered defaults remain MAR-170.

| Slice | Verification | Result |
| --- | --- | --- |
| Handle mapping | Handles sit at `[cx1, cy1]` and `[cx2, cy2]` along the frozen segment frame; the pixel/control-point mapping round-trips within `1e-9` and its anchors agree with the polyline `build_geometry()` renders; a flat segment substitutes a positive 100 logical-pixel reference span with `flat_value_span` set, and a zero-duration segment exposes no handles at all | PASS |
| X limits and Y overshoot | Drags clamp `cx` to exactly `0.0` and `1.0` and keep the gesture live; the primitive rejects `-1e-6`, `1.0000001`, NaN, `±inf`, and `1e300` atomically with the project byte-identical; finite `cy` overshoot `[0.2, -0.4, 0.8, 1.6]` round-trips through save/reload and reaches the exported runtime JSON verbatim as `[0.2, -0.4, 0.8, 1.6]` on the `arm_l` rotate key at `t = 0.25`, with the v2 MBIN payload compared equal to it | PASS |
| Segment-wide curve identity | `set_keyframe_interpolation()` takes no component parameter; editing while displaying Translate Y makes the Translate X segment report the same `SegmentKind` and control points; exactly one `interpolation` field in the whole project differs; switching the displayed scalar never forks the curve | PASS |
| Conversion and transaction | Grabbing a Linear or Stepped handle converts to Cubic seeded at `[1/3, 1/3, 2/3, 2/3]` inside one transaction; a press without motion opens none; a drag that ends on its original Cubic points commits none and reports no edit, whether it matched on the first frame or travelled and came back, while returning to the seed of a Linear or Stepped segment still commits because the authored kind changed; Escape, `cancel_authoring_gestures()`, `cancel_timeline_graph_point_drag()`, a mid-drag Dopesheet tab switch, a lost active key, a non-finite pointer, and every primitive rejection restore project bytes, history, redo depth, project revision, dirty state, dopesheet key times, graph values, graph segment kinds, and graph control points | PASS |
| Agent and MCP parity | `timeline.set_interpolation` is the 57th operation (`edit`, mutating, no review, dry-run supported) with matching C++/Python dry-run, validation, per-key `previous_interpolation` reporting, mutation, `no_change`, and undo behaviour; the growth is purely additive and no existing operation's name, category, flags, arguments, or response shape changed | PASS |
| Persistence and compatibility | Save/reload preserves the authored curve bitwise and export carries it into both JSON and the v2 binary, moving the exported JSON off its untouched-baseline size; `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1, `SelectionSet`, and GPU ownership are unchanged, and only the pre-existing `curve` field is written | PASS |

Validated commands and outputs:

- `cmake -S . -B build && cmake --build build -j10` -> configure and all default targets built
- `./build/marrow_timeline_graph_model_tests` -> `Timeline graph model: 20 cases passed`, including the new `handle geometry and pointer mapping` case
- `./build/marrow_timeline_model_tests` -> `Timeline model: 9 cases passed`
- `./build/marrow_viewport_interaction_tests` -> `Viewport interaction kernel tests passed.`; `./build/marrow_selection_tests` -> `SelectionSet: 8 cases passed`
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed; `MAR-169 shared bezier interpolation authoring validated across transform, slot-colour, and deform families` covering Rotate/Translate/Scale/Shear, Slot Color, Deform, the exactly-one-`interpolation`-field identity assertion, Linear/Stepped -> Cubic and Cubic -> Linear/Stepped conversion, the `[0, 0, 1, 1]` boundary, eight atomic control-point rejections, seven structural rejections, the split `key_count`/`changed_key_count` no-op, and an overshoot save/reload round trip
- The same smoke exports the curve-edited project and reports `MAR-169 curve export: JSON 14484 bytes, MBIN 4020 bytes`, asserting the exported runtime JSON carries `[0.2, -0.4, 0.8, 1.6]` on the `arm_l` rotate key at `t = 0.25` and that the v2 binary matches it. Both sizes moved off the untouched-baseline `14336`/`3984`, and its binary round-trip error is `rotation=0.00598526deg`, distinct from the baseline `0.00274662deg`
- `./build/marrow_project_smoke --create /tmp/player_idle.marrow` -> minimal project defaults, references, and round trip validated
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> passed, including the new headless `validate_timeline_graph_easing_shell_smoke` scenario and the actual-frame handle frames, which reported `Timeline Graph actual-frame handles: first=(592.909,775.303) second=(747.455,749.235)`. The scenario also covers a multi-frame drag that leaves the original control points and returns to them, asserting the status message is untouched as well as the history, and a Stepped segment whose drag returns to the seed, asserting it still commits the Cubic conversion
- `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2` -> parameter-mode shell smoke passed
- `ctest --test-dir build -N` -> `Total Tests: 21`; `ctest --test-dir build --output-on-failure` -> `100% tests passed, 0 tests failed out of 21` in 0.85 s. MAR-169 registers no new CTest.
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j10 && ctest --test-dir build-display --output-on-failure` -> automated Debug display-enabled suite `100% tests passed, 0 tests failed out of 24` in 9.35 s, including 3 display-labelled tests
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j10 && ctest --test-dir build-platform-release --output-on-failure` -> automated Release display-enabled suite `100% tests passed, 0 tests failed out of 24` in 3.81 s, including 3 display-labelled tests
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar169.mskl --export-binary /tmp/marrow_mar169.mbin` -> export passed; binary errors `rotation=0.00274662deg`, `position=0.000811016px`
- `./build/marrow_inspect --compare /tmp/marrow_mar169.mbin /tmp/marrow_mar169.mskl` -> `Comparison: /tmp/marrow_mar169.mskl matches /tmp/marrow_mar169.mbin`; `rotate_keys=10->10`, `translate_keys=6->6`, JSON `14336` bytes, MBIN v2 `3984` bytes with `version=2 optimized=yes animations=3 rotate_channels=4 translate_channels=2 keys=16 sorted=yes`
- `./build/marrow_inspect --compare /tmp/marrow_mar169_curve.mbin /tmp/marrow_mar169_curve.mskl` -> match on the curve-edited export; `rotation_error=0.00598526deg`, `position_error=0.000811016px`, `rotate_keys=10->10`, `translate_keys=6->6`, JSON `14484` bytes, MBIN v2 `4020` bytes. `./build/marrow_fixture_smoke /tmp/marrow_mar169_curve.mskl /tmp/player_idle.matl` -> passed
- `./build/marrow_fixture_smoke /tmp/marrow_mar169.mskl /tmp/player_idle.matl` -> exported runtime passed with 16 bones, 7 slots, 5 skins, 3 animations, 2 events, 3 draw commands, and 1 clip
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` against the exact 57-operation registry, including the new `timeline.set_interpolation` expectation row and its dry-run/live/read-back/`no_change`/undo sequence plus ten rejection cases. `agent_operation_descriptor_count()` is asserted to equal 57 at the start of each of the three graph shell scenarios and compared for equality at their ends
- `./build/marrow_agent_socket_tests` -> `Agent socket tests: 4 cases passed`; `./build/marrow_c_smoke` -> C ABI loaded 3 commands, 6 indices, and 2 callback events
- `tools/mcp/venv/bin/python tools/mcp/test_client.py` against `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` -> `mcp test_client: PASSED` with 57/57 exact C++/Python name parity, the asserted `timeline.set_interpolation` registry metadata row, and the dry-run/live/read-back/undo sequence proving `[0.2, -0.4, 0.8, 1.6]` survives and is restored
- MCP schema `py_compile`, `cmake --build build --target marrow_verify_third_party`, fixture/`.mskl`/PRD JSON parsing, `git diff --check`, and `git lfs status` -> passed; no LFS object was staged or queued to push
- `git diff --stat -- include/marrow/c_api src/c_api` and `git diff --stat -- src/editor/project.cpp src/runtime/skeleton_parse.cpp src/runtime/binary.cpp` -> empty; the `include/marrow/editor/authoring.hpp`, `src/editor/agent_dispatch.cpp`, and `src/editor/agent_handlers_editing.cpp` diffs contain added lines only

The display suites are automated evidence only. This checkpoint adds no manual
visible-UI, Windows 11, physical-input, or platform qualification credit.
MAR-192 through MAR-210 remain open, and support qualification remains governed
by `docs/root1/platform-validation.md`.

## MAR-168 Graph Key Time and Value Editing Validation Results

Validated 2026-08-30. The Timeline Graph tab is now authoritative for editing.
A left press on a graph point arms a drag candidate that holds no transaction;
the first motion past an inclusive 4.0 logical-pixel dead zone locks one axis by
dominant-axis comparison and keeps it for the rest of the gesture. A Value-locked
drag offsets exactly one scalar component of every selected key on the focused
track through the new additive `offset_keyframe_scalars()` primitive; a
Time-locked drag reuses the dopesheet's `begin/apply/finish_timeline_retime_gesture()`
trio verbatim. The Graph toolbar exposes the same shared
`TimelineEditorState::snap_to_frames` field the Dopesheet tab owns, and Alt
bypasses snapping for the current drag. No code path reads or writes
`Interpolation`; Bezier editing remains MAR-169.

| Slice | Verification | Result |
| --- | --- | --- |
| Axis lock and component preservation | Locked-axis drags: vertical changes only the active component; horizontal changes only key times. `decide_drag_axis` unit tests cover the dead zone, both dominant directions, the exact tie, and every non-finite input; headless drags assert that later off-axis motion never re-decides the axis; actual-frame drags report `drag_axis` and the matching live gesture | PASS |
| Shared authoring reuse | Frame snap, neighbour collision with 1 ms spacing, stable identity, and explicit-duration auto-grow come from `retime_keyframes()`/`refresh_runtime()`; values come from `offset_keyframe_scalars()`. Single retime ownership follows from placement: the dopesheet updater is called only from inside the Dopesheet tab body, and leaving the Graph tab cancels the graph drag first. The Graph-mode guard inside `update_timeline_retime_gesture()` is unreachable defence in depth against that call being hoisted | PASS |
| Atomic rollback | Escape, `cancel_authoring_gestures()`, a mid-drag tab switch, a non-finite delta, and every hard primitive rejection restore project bytes, history, project revision, dirty state, rebuilt dopesheet key times, and every rebuilt graph component value | PASS |
| One drag, one transaction | Press-only opens none; a zero-net drag on an already-authored row commits none; a committed drag adds exactly one undo entry with `selected_keys` and `active_key` bit-identical across commit, undo, and redo of a value edit | PASS |
| Persistence and compatibility | An actually offset project round-trips through save/reload and through JSON plus v2 MBIN export with the offset value present in the exported runtime; `.mskl` v1, `.mbin` v2, C ABI v1, and the 56-operation Agent/MCP surface unchanged; the only public-header change is additive | PASS |

Validated commands and outputs:

- `cmake -S . -B build && cmake --build build -j8` -> configure and all default targets built
- `./build/marrow_timeline_graph_model_tests` -> `Timeline graph model: 19 cases passed`, including the new `drag axis lock and unit mapping` case
- `./build/marrow_timeline_model_tests` -> `Timeline model: 9 cases passed`, including the new `graph value gesture completion reuse` case
- `./build/marrow_viewport_interaction_tests` -> passed; `./build/marrow_selection_tests` -> `SelectionSet: 8 cases passed`
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed; `MAR-168 graph scalar authoring validated across 11 lane-family cases` covering Rotate Angle, Translate X/Y, Scale X/Y, Shear X/Y, Slot Color R/G/B/A, the setup-pose-free Rotate delta, signed and exact-zero scale, the group colour clamp, the four out-of-range colour cases (a lone `1.4` alpha and a mixed `{0.2, 1.4}` group, each dragged both ways), thirteen atomic rejections, and a save/reload round trip
- The same smoke exports the offset project and reports `MAR-168 offset export: JSON 14338 bytes, MBIN 3984 bytes`, asserting the offset angle is present in the exported runtime JSON and that the v2 binary matches it. Its binary round-trip error is `rotation=0.00305176deg`, distinct from the untouched baseline export
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> passed, including the new headless `validate_timeline_graph_edit_shell_smoke` drag scenario and the actual-frame drags, which reported `Timeline Graph actual-frame drags: value axis=2 time axis=1 active point=(438.364,814.064)`
- `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2` -> parameter-mode shell smoke passed
- `ctest --test-dir build -N` -> `Total Tests: 21`; `ctest --test-dir build --output-on-failure` -> `100% tests passed, 0 tests failed out of 21` in 0.83 s. MAR-168 registers no new CTest.
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j8 && ctest --test-dir build-display --output-on-failure` -> automated Debug display-enabled suite 24/24, including 3 display-labelled tests, passed in 8.58 s
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j8 && ctest --test-dir build-platform-release --output-on-failure` -> automated Release display-enabled suite 24/24, including 3 display-labelled tests, passed in 3.40 s
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar168.mskl --export-binary /tmp/marrow_mar168.mbin` -> export passed; binary errors `rotation=0.00274662deg`, `position=0.000811016px`
- `./build/marrow_inspect --compare /tmp/marrow_mar168.mbin /tmp/marrow_mar168.mskl` -> match; `rotate_keys=10->10`, `translate_keys=6->6`, JSON `14336` bytes, MBIN v2 `3984` bytes with `version=2 optimized=yes animations=3 rotate_channels=4 translate_channels=2 keys=16 sorted=yes`
- `./build/marrow_inspect --compare /tmp/marrow_mar168_offset.mbin /tmp/marrow_mar168_offset.mskl` -> match on the graph-edited export; `rotation_error=0.00305176deg`, `position_error=0.000811016px`, `rotate_keys=10->10`, `translate_keys=6->6`, JSON `14338` bytes, MBIN v2 `3984` bytes
- `./build/marrow_fixture_smoke /tmp/marrow_mar168.mskl /tmp/player_idle.matl` -> exported runtime passed with 16 bones, 7 slots, 5 skins, 3 animations, 2 events, 3 draw commands, and 1 clip
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` against the unchanged exact 56-operation registry. `agent_operation_descriptor_count()` is additionally asserted to equal 56 at the start of the graph-edit shell scenario and compared for equality at its end, and the MAR-167 graph scenario keeps its own `!= 56U` guard; `./build/marrow_agent_socket_tests` -> `Agent socket tests: 4 cases passed`; `./build/marrow_c_smoke` -> C ABI loaded 3 commands, 6 indices, and 2 callback events
- MCP schema `py_compile`, `cmake --build build --target marrow_verify_third_party`, fixture/`.mskl`/PRD JSON parsing, `git diff --check`, and `git lfs status` -> passed; no LFS object was staged or queued to push
- `git diff --stat -- include/marrow/c_api src/c_api` and `git diff --stat -- src/editor/agent_dispatch.cpp src/editor/agent_handlers_editing.cpp tools/mcp` -> empty; the `include/marrow/editor/authoring.hpp` diff contains added lines only

The display suites are automated evidence only. This checkpoint adds no manual
visible-UI, Windows 11, physical-input, or platform qualification credit.
MAR-192 through MAR-210 remain open, and support qualification remains governed
by `docs/root1/platform-validation.md`.

## MAR-167 Synchronized Scalar Graph Validation Results

Validated 2026-08-20 and final-review corrections revalidated 2026-08-21. The
Timeline window now has a read-only Graph tab for one
focused continuous parent track. It projects effective runtime Bone
Rotate/Translate/Scale/Shear or Slot light RGBA, shares parent-key selection,
`active_key`, focus, and playhead with the dopesheet, and keeps all graph view
state transient. `player_idle` resolves seven supported parent tracks and
fourteen scalar series because the valid arm_l Rotate project overlay is part of
the effective runtime animation.

| Slice | Verification | Result |
| --- | --- | --- |
| Projection and exclusion | UI-free tests cover the 7 parent/14 scalar fixture projection, absolute unwrapped Rotate, shared parent identity/easing, and fail-closed Inherit, Attachment, FFD, Draw Order, and Event exclusion | PASS |
| Interaction and ownership | One focused parent track exposes per-component toggles and Fit. Wheel time zoom, bounded Shift-wheel value zoom, and middle pan are duration-independent; point activation shares parent selection, `active_key`, focus, and playhead with the dopesheet. Explicit fallback legend/empty-plot interaction focuses its displayed parent while render, Fit, wheel, and pan do not | PASS |
| Easing and presentation | Actual Linear, Stepped, and Cubic outgoing easing use distinct markers. The UI states that easing is parent-key-wide across X/Y or RGBA, and point time/value dragging is read-only until MAR-168 | PASS |
| Persistence and compatibility | Tab/view/cache/visibility/active-component interactions leave serialization, dirty state, history, project/runtime revision, `.mskl` v1, `.mbin` v2, C ABI v1, and the 56-operation Agent/MCP surface unchanged | PASS |

Fresh commands and outputs:

- `cmake -S . -B build && cmake --build build -j4 && ./build/marrow_timeline_graph_model_tests && ./build/marrow_timeline_model_tests && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow && ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2 && ctest --test-dir build --output-on-failure` -> configure/build passed; graph model 18/18; timeline model 8/8; project smoke passed; actual two-frame shell smoke passed; default CTest 21/21.
- The actual-frame shell smoke reported `spine_rotate points=3 linear=0 stepped=1 cubic=1 playhead=1`, `arm_l_rotate points=2 linear=1 stepped=0 cubic=0 playhead=0`, and `spine_translate points=6 linear=2 stepped=2 cubic=0 playhead=1`. Its real wheel arbitration reported hovered plot ownership with graph span `927.273->1066.36` and unchanged Timeline scroll `326->326`; the graph margin left the graph span `1066.36->1066.36` and changed Timeline scroll `326->261`.
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j4 && ctest --test-dir build-display --output-on-failure` -> automated Debug display-enabled suite 24/24, including 3 display tests, passed in 12.58 s.
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j4 && ctest --test-dir build-platform-release --output-on-failure` -> automated Release display-enabled suite 24/24, including 3 display tests, passed in 6.22 s.
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar167.mskl --export-binary /tmp/marrow_mar167.mbin` -> project/runtime export passed; binary errors `rotation=0.00274662deg`, `position=0.000811016px`.
- `./build/marrow_inspect --compare /tmp/marrow_mar167.mbin /tmp/marrow_mar167.mskl` -> match; `rotate_keys=10->10`, `translate_keys=6->6`, JSON `14336` bytes, MBIN v2 `3984` bytes.
- `./build/marrow_fixture_smoke /tmp/marrow_mar167.mskl /tmp/player_idle.matl` -> exported runtime passed with 16 bones, 7 slots, 5 skins, 3 animations, 2 events, 3 draw commands, and 1 clip.
- `./build/marrow_agent_dispatch_smoke` -> passed against the unchanged exact 56-operation registry; `./build/marrow_agent_socket_tests` -> 4/4 passed; `./build/marrow_c_smoke` -> C ABI loaded 3 commands, 6 indices, and 2 callback events.
- MCP schema `py_compile`, `cmake --build build --target marrow_verify_third_party`, fixture/PRD JSON parsing, `git diff --check`, and `git lfs status` passed; no LFS object was staged or queued to push.

The display suites are automated evidence only. This checkpoint adds no manual
visible-UI, Windows 11, physical-input, or platform qualification credit.
MAR-192 through MAR-210 remain open, and support qualification remains governed
by `docs/root1/platform-validation.md`.

## MAR-166 Grid and Magnetic FFD Vertex Snapping Validation Results

Validated 2026-08-20. Attachment-local FFD group drags now share the project
world grid and can opt into magnetic matching against currently visible mesh
vertices. The pressed selected vertex remains the group anchor, and one snapped
common world delta preserves the MAR-164 transaction and linked-target boundary.

| Slice | Verification | Result |
| --- | --- | --- |
| Project and settings | Additive `magnetic_vertex_enabled` defaults false when `.snap` or only the field is absent, rejects non-boolean input, preserves unknown nested data, survives filesystem save/reload, and remains runtime-export neutral. Properties → Viewport Snapping → Magnetic Vertices is one project-only undo/no-op-aware edit; the inclusive 8px radius is fixed and not serialized | PASS |
| Candidate and resolver | The UI-free resolver covers inclusive 8px Euclidean matching, magnetic-before-grid precedence, exact distance and `(slot, optional resolved skin, displayed attachment, vertex)` tie order. Shell coverage includes selected exclusion, duplicate attachment names across skins, cross-slot meshes, positive-alpha gating, per-update on-canvas filtering, same-gesture zoom reprojection, and offscreen-to-onscreen candidate admission | PASS |
| Gesture and lifecycle | Snapping uses the pressed selected vertex as anchor and applies one common world delta through frozen single/weighted inverses. Live Alt bypass and Cmd/Ctrl temporary enable do not enter settings/history; grid fallback, linked child-to-immediate-parent targeting, one commit/undo/redo, click/no-movement, snap-return-to-start, cancel, refresh failure, and atomic rollback remain covered | PASS |
| UI and compatibility | Magnetic and grid previews use transient cyan/gold guides from the raw pressed-anchor target. The display-enabled suites exercise the editor UI path without adding a manual visual-support claim. `.mskl` v1, `.mbin` v2, C ABI v1, `SelectionSet`, GPU ownership, and the 56-operation Agent/MCP surface remain unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build && cmake --build build -j4` -> all default targets built
- `./build/marrow_viewport_interaction_tests` -> inclusive boundary, precedence, stable slot/skin/attachment/vertex ties, live activation/bypass, grid fallback, and invalid-math tests passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> absent/MAR-165-shaped default-off compatibility, strict boolean validation, unknown retention, filesystem save/reload, and runtime-document export neutrality passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> candidate visibility/identity, selected exclusion, same-gesture layout reprojection, group/weighted/linked snapping, modifiers, guide origin, grid fallback, cancel/no-op, save/reload, and undo/redo smoke passed
- `ctest --test-dir build --output-on-failure` -> 20/20 passed
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j4 && ctest --test-dir build-display --output-on-failure` -> Debug display-enabled suite 23/23 passed
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j4 && ctest --test-dir build-platform-release --output-on-failure` -> Release display-enabled suite 23/23 passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar166.mskl --export-binary /tmp/marrow_mar166.mbin` -> project/runtime export passed
- `./build/marrow_inspect --compare /tmp/marrow_mar166.mbin /tmp/marrow_mar166.mskl` -> match; `rotation_error=0.00274662deg`, `position_error=0.000811016px`
- `./build/marrow_fixture_smoke /tmp/marrow_mar166.mskl /tmp/player_idle.matl` -> exported runtime passed
- `./build/marrow_agent_dispatch_smoke`, `./build/marrow_agent_socket_tests`, `./build/marrow_c_smoke`, and MCP schema `py_compile` -> unchanged surfaces and transports passed
- `python3 -m json.tool assets/fixtures/player_idle.marrow > /dev/null`, `python3 -m json.tool .agents/tasks/prd-marrow-runtime.json > /dev/null`, `cmake --build build --target marrow_verify_third_party`, and `git diff --check` -> passed

This checkpoint adds no Windows qualification credit. MAR-192 through MAR-210 remain
open, and macOS/Windows support qualification remains governed by
`docs/root1/platform-validation.md`.

## MAR-165 Shared Project Viewport Transform Snapping Validation Results

Validated 2026-08-20. Animation-mode translate, rotate, and scale gestures now
share one UI-free snap kernel backed by optional project metadata. Existing
projects remain default-off, while transient gesture modifiers never enter the
project, runtime export, dirty state, or history.

| Slice | Verification | Result |
| --- | --- | --- |
| Project model | Optional top-level `.marrow.snap` stores independent world-grid/local-angle/absolute-scale enables and positive finite steps with defaults `10`/`15`/`0.1`; absent projects stay off without materialization, unknown nested fields survive, invalid load/save fails, and a valid filesystem save/reload preserves typed and unknown data | PASS |
| Gesture math | The common scalar primitive covers negative half-steps and live activation; translate snaps the absolute world target before reflected-parent inversion, rotate snaps the raw unnormalized multi-turn local angle, and scale preserves signed/exact-zero components plus the X-if-nonzero-else-Y uniform driver ratio/sign rule | PASS |
| Input, settings, and grid | Alt always bypasses; macOS Cmd/non-macOS Ctrl temporarily enables a disabled domain and is sampled live. A toggle creates one project-only undo, a numeric drag creates one coalesced project-only undo, a blocked drag continuation cannot mutate without its pending item, and the origin-anchored display grid uses only integer multiples of the configured world step | PASS |
| Transaction and compatibility | One successful gesture creates one undo and cancellation is exact. An off-grid scale click with no movement and orthogonal pointer movement on an axis-constrained translate create no key or history. The additive public `ProjectSnapSettings`/`ProjectData::snap_settings` model does not change runtime documents, `.mskl` v1, `.mbin` v2, C ABI v1, GPU resources, or the 56-operation Agent/MCP surface | PASS |

Validated commands and outputs:

- `cmake -S . -B build && cmake --build build -j4` -> all default targets built
- `./build/marrow_viewport_interaction_tests` -> scalar activation/quantization, negative midpoint, signed/exact-zero/uniform scale, invalid math, and visible-grid integer-multiple tests passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> absent/default-off compatibility, strict validation, unknown-field retention, valid filesystem save/reload, and runtime-document export neutrality passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> project-only settings history and blocked-continuation rejection, reflected-parent/free/axis-constrained translation, raw rotation, signed/exact-zero scale, live modifiers, off-grid no-movement regressions, cancellation, and one-undo smoke passed
- `ctest --test-dir build --output-on-failure` -> 20/20 passed
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j4 && ctest --test-dir build-display --output-on-failure` -> Debug display-enabled suite 23/23 passed
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j4 && ctest --test-dir build-platform-release --output-on-failure` -> Release display-enabled suite 23/23 passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar165.mskl --export-binary /tmp/marrow_mar165.mbin` -> project/runtime export passed
- `./build/marrow_inspect --compare /tmp/marrow_mar165.mbin /tmp/marrow_mar165.mskl` -> match; `rotation_error=0.00274662deg`, `position_error=0.000811016px`
- `./build/marrow_fixture_smoke /tmp/marrow_mar165.mskl /tmp/player_idle.matl` -> exported runtime passed
- `./build/marrow_agent_dispatch_smoke`, `./build/marrow_agent_socket_tests`, `./build/marrow_c_smoke`, and MCP schema `py_compile` -> unchanged surfaces and transports passed
- `python3 -m json.tool assets/fixtures/player_idle.marrow > /dev/null`, `cmake --build build --target marrow_verify_third_party`, and `git diff --check` -> passed

This checkpoint adds no Windows qualification credit. MAR-192 through MAR-210 remain
open, and macOS/Windows support qualification remains governed by
`docs/root1/platform-validation.md`.

## MAR-164 Attachment-Local Multi-Vertex FFD Validation Results

Validated 2026-08-16. Animation mode now keeps a shell-private, persistent vertex
sub-selection under the exact active and currently displayed mesh Attachment. Selected
vertices move by one common world-space delta through frozen per-vertex inverses; this
does not add an entity `SelectionSet` group transform or persistent project state.

| Slice | Verification | Result |
| --- | --- | --- |
| Selection and input | Scope is `{slot, skin, displayed attachment, deform target, vertex count}` with sorted unique indices; plain point replace/selected-click collapse, macOS Cmd/non-macOS Ctrl toggle-only, forward/reverse inclusive plain/additive FFD boxes, empty semantics, 4px threshold, and FFD-box priority over the Bone box are covered | PASS |
| Atomic group solve | Single- and weighted-influence vertices use frozen inverses against one common world delta; selected pairs update from one gesture-start animation-only full vector while untouched pairs remain byte-equivalent, and duplicate/out-of-range/malformed/singular/non-finite members reject the complete candidate before mutation | PASS |
| Target and materialization | Normal and linked `deform=false` meshes target themselves; linked `deform=true` keeps the displayed child as selection scope while editing one immediate-parent timeline; parameter-composed positions drive handles without baking parameter output, and topology, UVs, and weights remain read-only | PASS |
| Transaction and lifecycle | Preflight completes before materialization; successful group drag creates one full-vector key and one undo entry, while click/no-movement, return-to-start, Escape, focus/context loss, refresh failure, and downstream override leave no partial candidate; selection survives time/animation/parameter preview/undo/redo and clears only on the documented context or successful-adoption boundaries | PASS |
| Persistence and compatibility | `.marrow` save/reload and exported `.mskl`/MBIN v2 preserve selected and untouched pairs, imported curves, and linked parent identity while vertex selection reloads empty; public editor API, `SelectionSet`, `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1, GPU resources, and the 56-operation Agent/MCP surface remain unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build && cmake --build build -j4` -> all default targets built
- `./build/marrow_viewport_interaction_tests` -> point replace/toggle, sorted unique selection, forward/reverse/inclusive box semantics, atomic group update, mixed inverses, untouched-pair preservation, and invalid-candidate rejection passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> selected-click drag/collapse, toggle-only, FFD box priority, mixed-influence common delta, isolated singular rejection, linked child-to-parent targeting, lifecycle, rollback, parameter non-bake, save/reload, and JSON/MBIN smoke passed
- `ctest --test-dir build --output-on-failure` -> 20/20 passed
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j4 && ctest --test-dir build-display --output-on-failure` -> Debug display-enabled suite 23/23 passed
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j4 && ctest --test-dir build-platform-release --output-on-failure` -> Release display-enabled suite 23/23 passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar164.mskl --export-binary /tmp/marrow_mar164.mbin` -> project/runtime export passed
- `./build/marrow_inspect --compare /tmp/marrow_mar164.mbin /tmp/marrow_mar164.mskl` -> match; `rotation_error=0.00274662deg`, `position_error=0.000811016px`
- `./build/marrow_fixture_smoke /tmp/marrow_mar164.mskl /tmp/player_idle.matl` -> exported runtime passed
- `./build/marrow_agent_dispatch_smoke`, `./build/marrow_agent_socket_tests`, `./build/marrow_c_smoke`, and MCP schema `py_compile` -> unchanged surfaces and transports passed
- `cmake --build build --target marrow_verify_third_party` and `git diff --check` -> passed

This checkpoint adds no Windows qualification credit. MAR-192 through MAR-210 remain
open, and macOS/Windows support qualification remains governed by
`docs/root1/platform-validation.md`.

## MAR-163 Single FFD Vertex Auto-Key Validation Results

Validated 2026-08-16. At this checkpoint, Animation mode exposed every vertex of the
exact active, currently displayed mesh Attachment as a fixed 6px hit target and a drag
edited only that vertex pair in one full animation-FFD vector. Persistent point/toggle/box
and multi-vertex behavior was deferred here and is completed by MAR-164 above.

| Slice | Verification | Result |
| --- | --- | --- |
| Handles and input | Final parameter-composed mesh positions drive the overlay; nearest distance and lower vertex index resolve ties; arbitration is active gesture/brush, translate, rotation, scale, FFD vertex, entity, then box | PASS |
| Frozen mapping | Single-influence and weighted `sum(weight * bone world 2x2)` inverses cover non-uniform and reflected transforms; empty, malformed, relative-singular, and non-finite mappings reject before mutation | PASS |
| Target and materialization | Normal meshes target themselves; linked `deform=true` meshes target their immediate parent while shared topology/weights remain read-only; the selected animation is sampled into a geometry-sized full vector and imported keys/curves materialize intact | PASS |
| Transaction and rollback | Playback pauses; only one X/Y pair changes from the gesture-start vector; click/no-movement creates no history, commit creates one undo entry, and Escape, focus/context loss, invalid payload, refresh failure, or downstream override cancel the whole transaction while preserving selection context | PASS |
| Persistence and compatibility | `.marrow` save/reload and exported `.mskl`/MBIN v2 preserve the full vector and parent target identity; public editor API, `SelectionSet`, `.marrow` schema, `.mskl` v1, C ABI v1, GPU resources, and the 56-operation Agent/MCP surface remain unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build && cmake --build build -j4` -> all default targets built
- `./build/marrow_viewport_interaction_tests` -> nearest/tie, weighted/reflected/non-uniform inverse, full-vector isolation, invalid rejection, and press precedence passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> single/multi-influence, linked `warrior_body -> body_mesh`, sparse interpolation, no-op, undo/redo, rollback, save/reload, and JSON/MBIN smoke passed
- `ctest --test-dir build --output-on-failure` -> 20/20 passed
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j4 && ctest --test-dir build-display --output-on-failure` -> Debug display-enabled suite 23/23 passed
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j4 && ctest --test-dir build-platform-release --output-on-failure` -> Release display-enabled suite 23/23 passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar163.mskl --export-binary /tmp/marrow_mar163.mbin` -> project/runtime export passed
- `./build/marrow_inspect --compare /tmp/marrow_mar163.mbin /tmp/marrow_mar163.mskl` -> match; `rotation_error=0.00274662deg`, `position_error=0.000811016px`
- `./build/marrow_fixture_smoke /tmp/marrow_mar163.mskl /tmp/player_idle.matl` -> exported runtime passed
- `./build/marrow_agent_dispatch_smoke`, `./build/marrow_agent_socket_tests`, `./build/marrow_c_smoke`, and MCP schema `py_compile` -> unchanged surfaces and transports passed
- `cmake --build build --target marrow_verify_third_party` and `git diff --check` -> passed

This checkpoint adds no Windows qualification credit. MAR-192 through MAR-210 remain
open, and macOS/Windows support qualification remains governed by
`docs/root1/platform-validation.md`.

## MAR-162 Signed Local Scale Gizmo Validation Results

Validated 2026-07-25. Animation mode now shows fixed 74px local X, local Y, and uniform
scale handles outside the 58px rotation ring for the runtime-active active Bone. Each gesture
freezes a positive scale-free local-axis basis and auto-keys absolute signed scale through the
existing effective-track materialization and transaction path.

| Slice | Verification | Result |
| --- | --- | --- |
| Handles and input | Local X/Y/uniform endpoints remain 74px from the pivot with a 6px hit radius independent of zoom; arbitration is active gesture/brush, translate Free/X/Y, rotation, scale, entity hit, then empty-space box; active non-Bone and weight-paint contexts hide the handles | PASS |
| Local-axis basis | Root and `OnlyTranslation` use skeleton scale; `Normal` children use evaluated parent-world 2x2 with local rotation/shear and without local scale; the basis is frozen at gesture start and covers non-uniform scale, reflection, and negative determinant | PASS |
| Supported inherit and signed mapping | `NoRotationOrReflection`, `NoScale`, and `NoScaleOrReflection` hide scale together with rotation and show a hint; X/Y ratio mapping crosses signs and preserves exact zero, zero-start axes recover at one scale unit per 74px, uniform preserves the starting signed X:Y ratio, and `(0,0)` hides uniform | PASS |
| Transaction and rollback | Playback pauses; imported effective scale keys and curves materialize once; mixed selection, active identity, hierarchy anchor, and timeline focus stay unchanged; click/no-movement creates no history, commit creates one undo entry, and cancel/non-finite failures restore project/runtime/preview content | PASS |
| Persistence and compatibility | `.marrow`, exported `.mskl`, and canonical MBIN v2 payloads preserve finite negative and exact-zero scale keys; public editor API, `SelectionSet`, `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1, and 56-operation Agent/MCP remain unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build` -> configured successfully
- `cmake --build build -j4` -> all targets built
- `./build/marrow_unit_tests` -> 31 named cases passed, including signed/exact-zero scale sampling
- `./build/marrow_selection_tests` -> 10/10 focused cases passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar162.mskl --export-binary /tmp/marrow_mar162.mbin` -> effective scale materialization, save/reload, and JSON/MBIN export passed
- `./build/marrow_inspect --compare /tmp/marrow_mar162.mbin /tmp/marrow_mar162.mskl` -> match; `rotation_error=0.00274662deg`, `position_error=0.000811016px`
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> fixed handles, basis/mapping, active-only transaction, rollback, and transience smoke passed
- `./build/marrow_agent_dispatch_smoke` -> all 56 registry operations passed without surface changes
- `./build/marrow_c_smoke` -> C ABI v1 smoke passed without API changes
- `ctest --test-dir build --output-on-failure -L editor` -> 7/7 passed
- `ctest --test-dir build --output-on-failure` -> 13/13 passed
- `git diff --check` -> passed

## MAR-161 Parent-Space Rotation Gizmo Validation Results

Validated 2026-07-21. Animation mode now shows one fixed 58px rotation ring for the
runtime-active active Bone. The gesture freezes its parent-space basis, preserves raw multi-turn
absolute rotation, and shares the existing effective-track materialization and transaction path.

| Slice | Verification | Result |
| --- | --- | --- |
| Ring and input | 58px radius, 6px hit band, 42px translate gizmo inside it, translate Free/X/Y precedence, then rotation, entity hit, and empty-space box; active non-Bone and weight-paint contexts hide the ring | PASS |
| Parent-space basis | Root and `OnlyTranslation` use skeleton scale; `Normal` children use the evaluated parent-world 2x2 frozen at gesture start; translation, rotation, shear, non-uniform scale, reflection, and negative determinant are covered | PASS |
| Supported inherit range | `NoRotationOrReflection`, `NoScale`, and `NoScaleOrReflection` hide the ring and show an unsupported-inherit hint; singular and NaN/Inf math cancel safely | PASS |
| Angular and transaction semantics | Per-sample `(-180, 180]` unwrap with +180 tie, positive and negative 450-degree accumulation, 2px pivot suspend/rebase, raw effective restart, playback pause, live preview, one undo, and no-movement no-op | PASS |
| Persistence and compatibility | `.marrow` and exported `.mskl` preserve raw multi-turn keys; unrepresentable continuous rotate channels fall back from optional AKEY to canonical MBIN payload while representable neighbors stay packed; public editor API, `SelectionSet`, `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1, and 56-operation Agent/MCP remain unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build` -> configured successfully
- `cmake --build build -j4` -> all targets built
- `./build/marrow_unit_tests` -> 31 named cases passed, including `Binary Multi-Turn Rotate Fallback`
- `./build/marrow_selection_tests` -> 10/10 focused cases passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar161.mskl --export-binary /tmp/marrow_mar161.mbin` -> raw multi-turn save/reload and JSON/MBIN export passed
- `./build/marrow_inspect --compare /tmp/marrow_mar161.mbin /tmp/marrow_mar161.mskl` -> match; `rotation_error=0.00274662deg`, `position_error=0.000811016px`
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> parent-space ring, unwrap, materialization, rollback, and transience smoke passed
- `./build/marrow_agent_dispatch_smoke` -> all 56 registry operations passed without surface changes
- `./build/marrow_c_smoke` -> C ABI v1 smoke passed without API changes
- `ctest --test-dir build --output-on-failure -L editor` -> 7/7 passed
- `ctest --test-dir build --output-on-failure` -> 13/13 passed
- `git diff --check` -> passed

## MAR-160 Viewport Multi-Selection Validation Results

Validated 2026-07-21. Viewport clicks now resolve typed entities by stable category, distance,
and authored/draw order, while empty-space drags select visible runtime-active Bone joints in
skeleton order. All editing consumers remain scoped to the active item; no group transform was
introduced.

| Slice | Verification | Result |
| --- | --- | --- |
| Point precedence | Visible constraint target, Bone joint, Bone body, Slot centroid, then topmost rendered Attachment triangle; category wins before screen distance and stable order | PASS |
| Point gestures | Plain click replaces and macOS Cmd/non-macOS Ctrl toggles the same exact typed identities used by hierarchy selection | PASS |
| Box gestures | Normalized forward/reverse rectangles, 4px threshold, inclusive runtime-active Bone centers, plain replace/clear, additive mixed-prefix retention and stable append, empty additive no-op | PASS |
| Geometry and source adoption | Region, GPU-skinned mesh, Slot, and visible constraint targets are pickable; hidden/inactive Bones are excluded; source adoption and orphaned viewport frames clear stale rectangles | PASS |
| Transience and consumers | Hierarchy anchor/timeline focus synchronize on selection changes, project/runtime/preview/history/revisions remain unchanged, and inspector/gizmo/timeline/constraint/weight tools edit only the active item | PASS |

Validated commands and outputs:

- `cmake --build build -j4` -> all targets built
- `./build/marrow_selection_tests` -> 10/10 focused cases passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> point, overlap, box, geometry, cleanup, and transience smoke passed
- `./build/marrow_agent_dispatch_smoke` -> all 56 registry operations passed without surface changes
- `ctest --test-dir build --output-on-failure -L editor` -> 7/7 passed
- `ctest --test-dir build --output-on-failure` -> 13/13 passed
- `git diff --check` -> passed

## MAR-159 Hierarchy Multi-Selection Validation Results

Validated 2026-07-21. Hierarchy clicks now apply platform-correct replace, toggle,
visible-range, and additive-range gestures to `SelectionSet` in the actual rendered row order.
The exact-identity anchor remains transient, and inspector, gizmo, timeline, constraint, and
weight-paint editing continue to use only the active item.

| Slice | Verification | Result |
| --- | --- | --- |
| Gesture semantics | Plain replace, macOS Cmd/non-macOS Ctrl toggle, forward/reverse Shift replacement, Cmd/Ctrl+Shift additive append, and deterministic invalid-anchor fallback | PASS |
| Visible order and anchor | Expanded and filtered Bone/Slot/Attachment order, collapsed/filtered row exclusion, toggled-off visible anchor retention, hidden-anchor clearing, and fully scoped attachment row identity | PASS |
| Source adoption | Reordered sources retain the exact anchor identity, deleted identities clear it, and malformed reload preserves selection, anchor, and the prior runtime bundle | PASS |
| Presentation and consumers | Common selected background, active-only primary rail/text, active-path ancestry, timeline-focus reset, selection-count status, and active-only inspector/gizmo/timeline/weight behavior | PASS |
| Transience and compatibility | Gestures leave project bytes, dirty/history, preview/runtime data, and revisions unchanged; `.marrow`, `.mskl` v1, `.mbin` v2, C ABI v1, and 56-operation Agent/MCP remain unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build` → configured successfully
- `cmake --build build -j4` → all targets built
- `./build/marrow_selection_tests` → 10/10 focused cases passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` → transient reload reconciliation passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` → MAR-159 hierarchy gesture, source-adoption, and transience smoke passed
- `./build/marrow_agent_dispatch_smoke` → all 56 registry operations passed without surface changes
- `ctest --test-dir build --output-on-failure -L editor` → 7/7 passed
- `ctest --test-dir build --output-on-failure` → 13/13 passed
- `git diff --check` → passed

## MAR-158 Selection Migration Validation Results

Validated 2026-07-18. `SelectionSet` is now the only entity-selection source used by
hierarchy, inspector, viewport, timeline, constraints, and weight-paint consumers. Successful
project/runtime source adoption re-resolves exact typed identities against the new runtime and
prunes only missing items; failed adoption preserves the selection and prior runtime bundle.

| Slice | Verification | Result |
| --- | --- | --- |
| Consumer resolution | Shell-private `ResolvedSelection`, active Bone-only transform editing, active Slot/Attachment timeline context, and active Constraint-only editing | PASS |
| Weight paint | Active Attachment/Slot then last selected Attachment/Slot target priority, active Bone influence, single target owning-bone fallback, no group edit | PASS |
| Source adoption | Name-based index re-resolution, fully scoped Attachment and kind-scoped Constraint pruning, stable survivor order/active fallback, malformed-reload atomicity | PASS |
| Selection primitives | Constraint remap/delete/collision and constraint-only prune preserve unrelated types, stable order, and active invariants | PASS |
| Transience and compatibility | Reconciliation adds no project bytes, dirty/history, or revision effects; `.marrow`, `.mskl` v1, `.mbin` v2, C ABI v1, and 56-operation Agent/MCP remain unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build` → configured successfully
- `cmake --build build -j4` → all targets built
- `./build/marrow_selection_tests` → 10/10 focused cases passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` → reload reconciliation and transient-state guardrails passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` → mixed consumer and atomic source-replacement shell smoke passed
- `./build/marrow_agent_dispatch_smoke` → all 56 registry operations passed without surface changes
- `ctest --test-dir build --output-on-failure -L editor` → 7/7 passed
- `ctest --test-dir build --output-on-failure` → 13/13 passed
- `git diff --check` → passed

## MAR-157 Typed SelectionSet Validation Results

Validated 2026-07-18. A public UI-free `SelectionSet` now owns exact name-based
Bone, Slot, Attachment, and Constraint identities with stable insertion order and one active
item. `ShellState` owns the only entity-selection set; persistence, preview composition,
history, user preferences, runtime formats, the C ABI, and Agent/MCP remain unchanged.

| Slice | Verification | Result |
| --- | --- | --- |
| Typed identity | Bone/Slot type separation, slot+skin+attachment scope, constraint kind+name scope, case-sensitive equality | PASS |
| Deterministic set | Replace, toggle, ordered range add, duplicate suppression, active fallback, clear, prune, remap/delete, collision tracking, invalid-range atomicity | PASS |
| Shell compatibility | Active Bone index; active Slot/Attachment slot and owning-bone context; Slot preview attachment; active Constraint kind/name | PASS |
| Transience | Project bytes, preview/runtime data, dirty state, undo/redo counts, and project/runtime/preview revisions remain unchanged | PASS |
| Compatibility | `.marrow`, `.mskl` v1, `.mbin` v2, C ABI v1, and the 56-operation Agent/MCP surface are unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build` → configured successfully
- `cmake --build build -j4` → all targets built
- `./build/marrow_selection_tests` → 7/7 focused cases passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` → active compatibility and transient-state shell smoke passed
- `ctest --test-dir build --output-on-failure -L editor` → 7/7 passed
- `ctest --test-dir build --output-on-failure` → 13/13 passed
- `git diff --check` → passed

## MAR-156 Versioned User Preference Store Validation Results

Validated 2026-07-18. A UI-free `PreferenceStore` now owns versioned user-local
`editor-settings.json` without joining project/session state, runtime formats, the C ABI, or
Agent/MCP operations.

| Slice | Verification | Result |
| --- | --- | --- |
| v1 contract | Six curve tokens, raw ordered recent paths, field-local defaults, invalid-entry skipping, malformed recovery, future-version protection | PASS |
| Additive compatibility | Supported-v1 unknown scalar/object/array and round-trip-sensitive numeric values survive known-field overlay | PASS |
| Paths | `MARROW_CONFIG_HOME` priority/restoration plus pure macOS, Linux XDG, Linux HOME fallback, empty/relative/missing environment cases | PASS |
| Atomic save | Unique same-directory temp, checked write/flush/close, POSIX rename, injected rename failure preserving exact prior bytes and cleaning the temp | PASS |
| Project isolation | Open dirty `EditorSession` with both undo and redo retains serialized project, history, dirty state, and project/runtime/preview revisions | PASS |
| Compatibility | `.marrow`, `.mskl` v1, `.mbin` v2, C ABI v1, and the 56-operation Agent/MCP surface are unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build` → configured successfully
- `cmake --build build` → all targets built
- `./build/marrow_preference_tests` → 8/8 focused cases passed
- `ctest --test-dir build --output-on-failure -L editor` → 6/6 passed
- `ctest --test-dir build --output-on-failure` → 12/12 passed
- `git diff --check` → passed

## MAR-155 Editor Duration Authoring Validation Results

Validated 2026-07-17. Ordered `.marrow.animation_edits` `set_duration` operations, editor transactions, key-boundary growth, and Agent/MCP control now complete the duration-authoring slice while keeping `.mskl` v1, `.mbin` v2, and C ABI v1 unchanged.

| Slice | Verification | Result |
| --- | --- | --- |
| Project contract | Ordered `set_duration` load/save/materialization, opaque unknown operation and known-edit additive-field preservation, old-project omission fallback | PASS |
| Session and shell | Live preview/dirty state, one-item undo/redo, queue-boundary rebuild, tail playhead clamp, Escape/invalid rollback | PASS |
| Timeline boundary | Key create/right-retime auto-grow in the same transaction; left-retime/delete never auto-shrink | PASS |
| Atomic rejection | Below-last-key manual shrink preserves project, runtime, preview, selection, history, dirty state, and revisions | PASS |
| Agent/MCP | 56-operation C++/Python parity, duration metadata, dry-run/live/reject/undo/redo, native socket E2E | PASS |
| Export compatibility | Save/reload and JSON/MBIN explicit presence/value plus quantized animation comparison | PASS |

Validated commands and outputs:

- `cmake -S . -B build` → configured successfully
- `cmake --build build -j4` → all targets built
- `./build/marrow_unit_tests` → 30 named cases passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar155_acceptance.mskl --export-binary /tmp/marrow_mar155_acceptance.mbin` → duration authoring/rollback/auto-grow/export E2E passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` → live duration/queue/clamp/reject shell smoke passed
- `./build/marrow_agent_dispatch_smoke` → all 56 registry operations passed
- `tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py` → Python schemas compiled
- `MARROW_AGENT_PORT=9877 tools/mcp/venv/bin/python tools/mcp/test_client.py` against `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9877` → socket E2E passed
- `./build/marrow_inspect --compare /tmp/marrow_mar155_duration.mbin /tmp/marrow_mar155_duration.mskl` → match; `rotation_error=0.00274662deg`, `position_error=0.000811016px`
- `./build/marrow_c_smoke` → C ABI smoke passed without API changes
- `ctest --test-dir build --output-on-failure -L runtime` → 4/4 passed
- `ctest --test-dir build --output-on-failure -L editor` → 5/5 passed
- `ctest --test-dir build --output-on-failure` → 11/11 passed
- `git diff --check` → passed

## MAR-154 Runtime Explicit Duration Validation Results

Validated 2026-07-17. Optional runtime clip duration now preserves authored presence while keeping `.mskl` v1, `.mbin` v2, and C ABI v1 unchanged.

| Slice | Verification | Result |
| --- | --- | --- |
| Runtime contract | Explicit/inferred/effective separation, empty clips, exact validation, copy/assignment, and old-asset fallback | PASS |
| Playback | Tail hold, non-loop/loop completion, queue promotion, reverse sampling, snapshot restore, and synthetic empty-animation behavior | PASS |
| Binary | Generic payload presence, effective-duration AKEY quantization, regenerated fixture, and JSON/MBIN comparison | PASS |
| Fixture compatibility | `aim` explicit `0.5`; `idle`/`attack` inferred `1.0`/`0.4`, validated from JSON and MBIN | PASS |

Validated commands and outputs:

- `cmake -S . -B build` → configured successfully
- `cmake --build build -j4` → all targets built
- `./build/marrow_unit_tests` → 30 named cases passed
- `./build/marrow_inspect --export-binary assets/fixtures/player_idle.mbin assets/fixtures/player_idle.mskl` → regenerated v2 fixture
- `python3 -m json.tool assets/fixtures/player_idle.mskl > /dev/null` → valid JSON
- `./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl` → explicit/fallback JSON fixture passed
- `./build/marrow_fixture_smoke assets/fixtures/player_idle.mbin assets/fixtures/player_idle.matl` → explicit/fallback MBIN fixture passed
- `./build/marrow_inspect --compare assets/fixtures/player_idle.mbin assets/fixtures/player_idle.mskl` → match; `rotation_error=0.00274662deg`, `position_error=0.000811016px`
- `./build/marrow_c_smoke` → C ABI smoke passed without API changes
- `ctest --test-dir build --output-on-failure -L runtime` → 4/4 passed
- `ctest --test-dir build --output-on-failure` → 11/11 passed
- `git diff --check` → passed

## MAR-122–128 Parameter Modeling Validation Results

Validated 2026-07-16. MAR-121 remains a tracking tombstone integrated into MAR-122; the complete MAR-122–128 runtime, renderer, project/editor, and Agent/MCP parameter-modeling checkpoint now passes.

| Slice | Verification | Result |
| --- | --- | --- |
| Runtime parameters and shapes | Finite/raw-direct/default/discrete/clamp/revision rules, post-composition normalization, 1D endpoint/linear shapes, linked/weighted mesh and animation-FFD separation | PASS |
| Deformers and caching | Bilinear warp, rotation pivot/influence, one-level chains, cycle/depth/ambiguity rejection, dependency bitsets, affected-slot cache updates | PASS |
| ArtPath renderer | Deterministic cap/join tessellation and scale-aware bounds, root-overlay ordering, solid-white triangle path, atlas-free preparation and cached missing-atlas guard | PASS |
| Expression and lip sync | Priority/activation order, additive/override, fade/restore/hold, amplitude/phoneme, attack/release/smoothing | PASS |
| Project and editor | Lossless optional parameter model, atomic runtime rebuild/rollback, preview preserve/prune/default, Parameter mode, CRUD, capture/replace and lattice/pivot gestures | PASS |
| Agent/MCP | Exact 55-operation C++/Python parity, dry-run invariants, mutation/undo/rebuild, keyform collision policy and parameter socket E2E | PASS |
| Compatibility and performance | `.mskl` v1, `.mbin` v2 and C ABI v1 retained; old assets use an empty model; 200-skeleton acceptance and separate parameter/deformer metrics pass | PASS |

Validated commands and outputs:

- `cmake -S . -B build` → configured successfully
- `cmake --build build -j4` → all targets built
- `ctest --test-dir build --output-on-failure` → 11/11 passed
- `ctest --test-dir build --output-on-failure -L runtime` → 4/4 passed
- `ctest --test-dir build --output-on-failure -L editor` → 5/5 passed
- `./build/marrow_unit_tests` → 29 named cases passed
- `./build/marrow_parameter_project_smoke` → preview/undo/rollback/save/reload and complete JSON/binary parameter model passed
- `./build/marrow_inspect --compare /tmp/marrow_parameter_face_basic.mbin /tmp/marrow_parameter_face_basic.mskl` → all seven optional parameter roots match
- `./build/marrow_renderer_sample --no-atlas --skip-render assets/fixtures/art_path_stroke.mskl` → two stroke commands and exact tessellation/bounds guardrails passed
- `./build/marrow_agent_dispatch_smoke` → all 55 registry operations and parameter dry/live/undo paths passed
- `tools/mcp/venv/bin/python tools/mcp/test_client.py` and `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only` → both socket E2E paths passed
- `./build-bench/marrow_benchmark --skeletons 200` → `frame_ms=4.45`, `score=100`, `max_skeletons_60fps=749.03`
- `./build-bench/marrow_benchmark --parameter-deformers --skeletons 200 --frames 240` → `parameter_us=0.07`, `deformer_us=0.51`
- `git diff --check` → passed

## MAR-141–153 Editing P0 Validation Results

Validated 2026-07-12. This table records the earlier imported-rig authoring P0 checkpoint; the later MAR-122–128 parameter-modeling validation is recorded above.

| Slice | Verification | Result |
| --- | --- | --- |
| Honest setup + auto-key | Setup transforms/colors are read-only; inspector R/T/S/shear materializes the effective base track, converts non-zero setup rotation correctly, previews live, and rolls back exactly | PASS |
| Stable viewport + move gizmo | Camera inverse/cursor zoom/Fit plus root, transformed child, IK target, singular-parent cancel, one-drag undo/redo | PASS |
| Slot lanes + dopesheet | Color/attachment add/edit/remove, stable same-time identities, exact-playhead remove, box/toggle selection, multi-key retime, typed clipboard and compatible-lane paste | PASS |
| Animation catalog | Ordered `.marrow.animation_edits`, create/duplicate/rename/delete UI and agent operations, unknown-family preservation, atomic queue/preview cascade | PASS |
| Agent/MCP parity | At this historical P0 checkpoint, C++ and Python exposed 49 matching operations; MAR-128 raised that checkpoint total to 55 and MAR-155 raises the current total to 56 | PASS |
| End to end | Base-only auto-key → retime → undo/redo → save/reload → JSON/quantized binary export and comparison | PASS |

Validated commands and outputs:

- `cmake --build build -j4` → all targets built
- `ctest --test-dir build --output-on-failure` → 7/7 passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` → P0 E2E passed; binary errors `rotation=0.00274662deg`, `position=0.000811016px`
- Historical `./build/marrow_agent_dispatch_smoke` result → 49 operations passed; MAR-128 later reached 55/55 and MAR-155 reaches the current 56/56
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` + `tools/mcp/venv/bin/python tools/mcp/test_client.py` → MCP/socket E2E passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 5` → 5 frames rendered with viewport/timeline/catalog P0 smokes
- `./build/marrow_renderer_sample --skip-render assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl` → renderer preparation guardrail passed
- `git diff --check` → passed

## Runtime and Renderer Unit Cases

`./build/marrow_unit_tests` currently reports these 31 named cases (validated 2026-07-21):

- `Interpolation Edge Cases`
- `Constraint Fast Math Approximations`
- `Animation Float Storage And Constant Pruning`
- `Animation Timeline Index And Sampling Cursor`
- `Animation Explicit Duration`
- `Matrix Composition`
- `Topological Bone Reorder`
- `SkeletonData Children Map And Tip Cache`
- `SIMD World Transform Propagation`
- `Constraint Hot Path Allocations`
- `Constraint Dirty Skip Preserves Output`
- `Constraint Dirty Skip Re-evaluates Only Affected Constraints`
- `IK Solving`
- `Physics Stepping`
- `SkeletonBounds Queries`
- `Custom Allocator Lifecycle`
- `AnimationState Snapshot Restore`
- `Animation Layers`
- `Concave Stencil Clipping`
- `Nested Stencil Restoration`
- `Dynamic Mesh Cache Static Payload And Deform Updates`
- `Dynamic Mesh Clipping Uses Stencil Only`
- `PreparedScene Cache Dirty Updates`
- `Parameter Definitions And Composition`
- `Parameter State Transition Semantics`
- `Parameter Shape Final Offsets`
- `Parameter Deformer And ArtPath Evaluation`
- `Parameter Loader Validation`
- `Binary Key Quantization And Reduction`
- `Binary Multi-Turn Rotate Fallback`
- `Runtime Profiler Frame`

## MAR-119 E2E Editor Validation Results

Validated 2026-04-11. All acceptance criteria pass through headless smoke tests and round-trip export verification.

| AC | Description | Verification | Result |
| --- | --- | --- | --- |
| AC1 | Open project → character visible with textures | `validate_viewport_prepared_scene_renderer_smoke()`: region attachments, GPU-skinned mesh, stencil clipping, blend modes | PASS |
| AC2 | Play idle → character animates smoothly | `set_selected_animation("idle")` + `scrub_timeline_time()` verifies arm_l rotation=60.0 at t=0.2; `advance_timeline_playback()` validated in hot-reload smoke | PASS |
| AC3 | Select arm_l → bone highlights, inspector shows properties | `pick_bone_at_position()` joint priority + body hit zones; `select_bone()` sets `selected_bone_index`; hierarchy sync | PASS |
| AC4 | Edit bone rotation → viewport real-time update | Timeline editor smoke: spine rotation 8→9, preview at t=0.625 = 10.5 (linear interp); inserted stepped key at t=0.75 verified | PASS |
| AC5 | Weight paint → heatmap + brush modifies weights | Paint mode (0.25→0.625), erase mode (0.625→0.0), smooth mode (0.0→0.3125); heatmap blue→green→yellow→red ramp; weight normalization to 1.0; undo/redo of all stroke types | PASS |
| AC6 | Onion skinning → semi-transparent ghost characters | Frame mode 2+2 ghosts with blue/red tint + alpha falloff; anchor mode snaps to intervals; keyframe mode samples at authored keys; textured ghost rendering via `render_tinted()` | PASS |
| AC7 | Export → .mskl loads with correct counts | 6 round-trip exports verified: rotate curve, draw-order, events, deform, weight paint, constraints. All load in `marrow_inspect`: bones=16, slots=7, skins=5, animations=3. JSON/binary comparison matches (rotation_error=0.003deg, position_error=0.001px) | PASS |
| AC8 | Undo/redo works through all edit operations | Weight paint undo/redo (3 modes), grouped drag merge into single history entry, 100-action depth cap, full snapshot restore | PASS |
| AC9 | Documentation in AGENTS.md | This section | PASS |

Validated test commands and outputs:
- `./build/marrow_unit_tests` → 31 named cases passed (current executable; revalidated 2026-07-21)
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 5` → 5 frames rendered
- `./build/marrow_renderer_sample --auto-close 2` → all blend/clip/mesh/batch validations passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_e2e_export.mskl --export-binary /tmp/marrow_e2e_export.mbin` → export + undo/redo validated
- `./build/marrow_fixture_smoke /tmp/marrow_e2e_export.mskl /tmp/player_idle.matl` → generic runtime smoke passed
- `./build/marrow_inspect --compare /tmp/marrow_e2e_export.mbin /tmp/marrow_e2e_export.mskl` → comparison matches
