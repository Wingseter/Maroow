# Editor Architecture Refactor Roadmap

This document records the completed editor architecture gate that follows MAR-120 and precedes new editor feature work. The architecture and file-format source of truth remains [`discription.md`](discription.md); this document owns the refactor boundaries and completion record.

## Why this refactor exists

At the start of the 2026-07-12 refactor, `src/editor/shell_main.cpp` was 10,310 lines and `ShellState` owned project data, compiled runtime state, playback, history, agent state, UI resources, selection, and editor gestures. Project history was also exposed through `ProjectCommandStack`, agent dispatch accepted `ShellState`, and operation metadata was maintained separately from string-branched handlers. The `marrow_editor` target compiled shell/UI code, including an `icon_registry.cpp` source that was also compiled into `marrow_editor_shell`.

The refactor replaced those overlapping ownership paths before new editing and parameter-modeling UI is added. It is a behavior-preserving change: it did not add parameter/deformer features or change runtime/file-format decisions.

### Current implementation checkpoint

The refactor was completed by HEAD commit `4c93ca15fc0cd0481bf8868577da96b270c04512` at `2026-07-12T13:34:04+09:00`. MAR-129–140 are therefore recorded as completed PRD stories and are not scheduled again:

- The refactor checkpoint reduced `shell_main.cpp` to 748 lines and `shell_state.hpp` to 606 lines. The current P0 tree is 750/718 lines respectively; the added state is limited to P0 camera, inspector/viewport gestures, timeline selection/clipboard, and animation-management presentation.
- The legacy `shell_types.hpp` and private shell undo stack are gone.
- Preview/playback, asset watching, timeline, constraints, selection, inspector, weight-paint, viewport UI, project/runtime panels, and agent panels have feature-owned source/header pairs.
- `marrow_editor` contains UI-free project/session/agent authoring code and links only `marrow_runtime` and Zlib; icon/UI/OpenGL code is compiled only into `marrow_editor_shell`.
- The C API and socket dispatcher use `EditorSession` plus `AgentControlState`, and C ABI version 1 is unchanged.
- CTest discovered seven source-root compatibility tests at this historical P0 checkpoint. The refactor baseline characterized 44 operations; editing P0 added animation CRUD and atomic timeline retime, so the P0-era agent smoke exercised all 49 operations registered at that time. Later parameter/duration milestones raised the current registry to the exact 56-operation total recorded below.

## Target ownership

| Owner | Responsibilities |
| --- | --- |
| `EditorSession` | Project/load state, compiled runtime data, preview controller, dirty baseline, revisions, edit transactions, history, save, and runtime export |
| `AgentControlState` | Pause/terminate state, current operation, activity log, review queue, and monotonic activity/review IDs |
| `ShellState` | ImGui/OpenGL resources, docking, UI selections, gestures, file watcher, HUD, icons, feature panels, and socket configuration |

`EditorSession` is move-only and UI-free. Its public interface lives in `include/marrow/editor/session.hpp` and provides:

- `open`, `reload`, `save`, and `export_runtime` lifecycle operations;
- read-only project and compiled-runtime access;
- preview selection, playback, seeking, composition, and frame advancement;
- `undo`, `redo`, dirty state, and project/runtime/preview revision counters.

A private `PreviewController` owns animation selection, playback time, loop/queue/mix/reverse state, `Skeleton`, and `AnimationState`. Ordinary frame advancement stays incremental through animation-state update/apply and must not rebuild runtime data.

## Transaction and history contract

All project and transient preview edits use one non-nestable session transaction. A transaction descriptor carries an edit kind, label, merge key, merge permission, and project/runtime/preview impact flags.

Commit validates the candidate project and performs any required runtime rebuild atomically. Validation or rebuild failure, and explicit cancellation, restore the prior project and preview state and create no history entry. No-change transactions likewise create no history entry.

History remains capped at 100 entries and keeps current merge/grouping behavior. Preview skin and attachment composition remains transient and does not mark the project dirty, but it remains undoable and redoable. `ProjectCommand`, `ProjectCommandStack`, and `make_project_command` were removed after production call sites and smoke coverage moved to `EditorSession`.

## Agent and C API boundary

The C++ dispatcher becomes:

```cpp
dispatch(AgentCommandContext&, const runtime::json::Value&)
```

`AgentCommandContext` contains `EditorSession` and `AgentControlState`. The opaque C `MarrowProject` owns those objects. Socket worker threads continue to queue commands only; dispatch and mutation remain main-thread, single-writer operations.

One operation registry binds every operation name to its category, permissions, review and dry-run metadata, and handler. `operations.list` is generated from the same registry. Tests reject duplicate names and missing handlers. Handler implementations are divided into inspection, editing/timeline, constraints, and management/validation modules without changing JSON requests or responses.

## Shell feature boundaries

After session and dispatch ownership is stable, UI code is extracted in this order:

1. `shell_timeline`: timeline models/editors, playback controls, seeking, and scrubbing.
2. `shell_constraints`: constraint selection, defaults, conversion, mutation, preview, and panels.
3. `shell_selection` and `shell_inspector`: hierarchy and cross-feature selection plus bone/slot/skin/attachment inspection.
4. `shell_weight_paint` and `shell_viewport`: brush math/gestures/overlays and viewport settings/interactions.
5. Project, runtime, and agent panels; final shell composition cleanup.

The final UI state header is `shell_state.hpp`, with narrow private feature headers replacing `shell_types.hpp`. `shell_main.cpp` retains only CLI/platform startup, theme/docking setup, frame composition, and shutdown.

## Build-target boundary

The final `marrow_editor` library is UI-free and depends only on `marrow_runtime` and Zlib. Shell, icon, widget, viewport, ImGui, GLFW, OpenGL, and platform sources compile only into `marrow_editor_shell`; `icon_registry.cpp` is compiled exactly once.

This roadmap does not split the existing combined C API or renderer targets, and it defers broad `project.cpp` and test-source decomposition.

## Story sequence and status

| Story | Purpose | Depends on | Status |
| --- | --- | --- | --- |
| MAR-120 | Fix parameter/deformer format boundary and roadmap slices | MAR-119 | Done (validated 2026-07-12) |
| MAR-129 | Focused CTest guardrail | MAR-120 | Done (`4c93ca1`, 2026-07-12T13:34:04+09:00) |
| MAR-130 | Characterize external behavior and the baseline 44 agent operations | MAR-129 | Done (`4c93ca1`, 2026-07-12T13:34:04+09:00; P0-era registry snapshot 49) |
| MAR-131 | Unified session transactions and history | MAR-130 | Done (`4c93ca1`, 2026-07-12T13:34:04+09:00) |
| MAR-132 | Preview controller | MAR-131 | Done (`4c93ca1`, 2026-07-12T13:34:04+09:00) |
| MAR-133 | EditorSession lifecycle and ownership | MAR-132 | Done (`4c93ca1`, 2026-07-12T13:34:04+09:00) |
| MAR-134 | C API and agent-context migration | MAR-133 | Done (`4c93ca1`, 2026-07-12T13:34:04+09:00) |
| MAR-135 | Registry-driven dispatcher and handler modules | MAR-134 | Done (`4c93ca1`, 2026-07-12T13:34:04+09:00) |
| MAR-136 | Timeline module | MAR-135 | Done (`4c93ca1`, 2026-07-12T13:34:04+09:00) |
| MAR-137 | Constraint module extraction (no new rename/delete behavior) | MAR-136 | Done (`4c93ca1`, 2026-07-12T13:34:04+09:00) |
| MAR-138 | Selection and inspector modules | MAR-137 | Done (`4c93ca1`, 2026-07-12T13:34:04+09:00) |
| MAR-139 | Weight-paint and viewport modules | MAR-138 | Done (`4c93ca1`, 2026-07-12T13:34:04+09:00) |
| MAR-140 | Composition and build-target cleanup | MAR-139 | Done (`4c93ca1`, 2026-07-12T13:34:04+09:00) |
| MAR-141–153 | Editing P0: trustworthy authoring, direct manipulation, dopesheet, slot timelines, animation CRUD, E2E guardrail | MAR-140, then story DAG | Done (current worktree, 2026-07-12) |
| MAR-121 | Tracking tombstone: runtime foundation integrated into MAR-122 | MAR-120 | Done (integrated 2026-07-16; no separate implementation) |
| MAR-122–126 | Runtime-first parameter foundation/export, shapes, deformers, ArtPath, expressions/lip-sync | Sequential from MAR-120 beginning at MAR-122 | Done (validated 2026-07-16) |
| MAR-127 | Parameter-modeling editor tools | MAR-126, MAR-140, MAR-153 | Done (validated 2026-07-16) |
| MAR-128 | Parameter/deformer agent commands | MAR-127 | Done (validated 2026-07-16) |
| MAR-154 | Runtime explicit clip duration | MAR-128 | Done (validated 2026-07-17) |
| MAR-155 | Editor duration authoring | MAR-154 | Done (validated 2026-07-17) |
| MAR-156 | Versioned user preference store | MAR-155 | Done (validated 2026-07-18) |
| MAR-157–167 | Typed selection through synchronized scalar graph checkpoints | Each story depends on the immediately preceding story | Done (validated 2026-07-18 through 2026-08-20) |
| MAR-168–191 | Remaining Editing P1 product chain | Each story depends on the immediately preceding story | Open backlog beginning at MAR-168 |

Numeric IDs are intentionally not execution order. The PRD array put MAR-141–153 immediately after MAR-120 so editing P0 could close before the parameter track; both that checkpoint and MAR-122–128 are now implemented. MAR-121 is a done tombstone integrated into MAR-122, MAR-154–156 are complete, and MAR-157–167 remain completed functional checkpoints. The next dependency sequence is the remaining linear MAR-168–191 P1 product chain. MAR-129–140 remain in their historical location but are already complete. Constraint rename/delete is deliberately deferred to MAR-178 rather than being credited to the refactor-only MAR-137.

## Compatibility boundary

The refactor must preserve:

- C ABI version 1, all C functions, status codes, and ownership rules;
- all 44 refactor-baseline agent operations, JSON request/response shapes, error messages, permissions, dry runs, reviews, and IDs, plus five P0 operations, six MAR-128 parameter operations, and MAR-155 `animation.set_duration` for an exact current total of 56;
- existing `.marrow` compatibility, `.mskl` v1, `.mbin` v2, and `.matl` v1; P1 project fields remain optional and additive;
- optional parameter-model roots default to empty for old assets, unknown additive `.marrow` fields survive load/save, and direct preview parameter input is not serialized;
- byte-identical unchanged `.marrow` serialization and equivalent `.mskl`/`.mbin` exports;
- checked-in fixtures, playback behavior, transient preview composition, dirty semantics, and history grouping;
- visible editor selection, timeline, constraint, paint, onion-skin, hot-reload, agent-panel, save, and export workflows.

Maroow parameter modeling remains Maroow-native. It is not Live2D Cubism Core, SDK ABI, proprietary file, parameter-name, or importer compatibility.

## Validation gate

The focused CTest suite runs from the source root so existing relative fixture paths continue to work:

```sh
cmake --build build
ctest --test-dir build --output-on-failure
```

The direct compatibility commands remain required:

```sh
./build/marrow_c_smoke
./build/marrow_agent_dispatch_smoke
./build/marrow_project_smoke assets/fixtures/player_idle.marrow
./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
./build/marrow_renderer_sample --hud --skip-render assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 5
```

Interactive Metal rendering and macOS launch focus remain manual host checks where sandboxed window or Metal-device creation is unavailable.

## Task #27 maintenance checkpoint (2026-08-10)

Task #27 is a behavior-preserving maintenance checkpoint based on the clean
`53b2285` tree. It does not create or close a MAR story, change the existing
story dependency order, or alter any platform-qualification decision.

The work was integrated as five dependency-ordered, independently validated
commits:

| Checkpoint | Commit | Result |
| --- | --- | --- |
| Remove smoke wrapper layers | `c163d97` | Removed the `*_for_smoke` and viewport `*_impl` indirection; production UI and smoke coverage now use the same shell-private interaction entry points. |
| Deduplicate Agent constraint edits and commit policy | `e7f7123` | Added the shared Path/Transform/Physics typed edit flow and commit policy while preserving operation-specific deltas, error codes, messages, no-op behavior, rollback, and `Agent` undo grouping; IK remains independent. |
| Share coalesced-edit and viewport-transform lifecycle | `686fcfc` | Centralized activation, sampling, finalization, orphan handling, cancellation, rollback, and common transform snapshots without normalizing feature-specific mutation semantics. |
| Use typed parameter-panel fields | `070cce5` | Removed per-frame definition JSON round trips from shape/deformer/expression/lip panels; edits clone only on input and preserve additive unknown source fields. |
| Add revision-keyed derived caches | `ccb53b0` | Added shell-private timeline and slot caches keyed by runtime revision and runtime identity, including pinned slot identity references, sorted-unique timeline names, and generation-based smoke coverage. |

The checkpoint preserves public `include/marrow/**` interfaces, `.marrow`,
`.mskl` v1, `.mbin` v2, `.matl` v1, C ABI v1, all 56 Agent/MCP operations,
wire responses, undo grouping, and selection behavior. The new interaction,
coalesced-edit, and cache helpers remain private to `marrow_editor_shell`; the
UI-free `marrow_editor` target boundary is unchanged. Production source has no
remaining `*_for_smoke` symbols. The two direct production timeline rebuilds
after add-key and retime remain intentionally uncached so the current frame
does not retain invalid row references.

### Task #27 validation record

The final gate completed on this macOS host:

- Default configure/build completed, and `marrow_verify_third_party` verified
  the pinned SDL3, Dear ImGui, Sokol, patched sokol_imgui, sokol-shdc, and
  generated shader artifacts.
- Default CTest passed 17/17.
- The display-enabled build passed 20/20 on the host, including the three
  actual SDL/Metal display tests.
- The Release display-enabled build passed 20/20 on the host.
- `marrow_agent_dispatch_smoke`, `marrow_c_smoke`, and
  `marrow_parameter_project_smoke` passed.
- Project export, exported-runtime loading, and JSON/MBIN v2 comparison passed;
  the comparison matched with `rotation_error=0.00274662deg` and
  `position_error=0.000811016px`.
- MCP Python sources passed syntax compilation, and the live editor/socket MCP
  client completed with `mcp test_client: PASSED` on `127.0.0.1:9877`.
- Source guards found zero production `*_for_smoke` symbols, and
  `git diff --check` passed.

The display/socket suites were run in the actual host environment because the
restricted sandbox does not expose a display or loopback bind authority. These
results are macOS-local evidence only. Ubuntu X11, Windows 10/11, physical
Windows Ink, fixed legacy/Sokol A/B, and clean portable-folder qualification
remain open exactly as recorded elsewhere; Task #27 grants none of that
qualification credit.

## Task #28 core-boundary refactor checkpoint (completed 2026-08-16)

Task #28 was a behavior-preserving implementation checkpoint between completed
MAR-162 and the now-completed MAR-163. It is not a PRD story and does not close
or qualify any platform story. Its execution order was `Task #28 -> MAR-163`;
MAR-163 depends directly on MAR-162. MAR-192 through MAR-210 keep their existing `open` states,
acceptance criteria, evidence requirements, and internal dependency chain as a
parallel deferred qualification backlog.

### Frozen baseline and legacy evidence boundary

- Branch `agent-control-remaining` was clean at
  `8894013e3ec3d38f2f2cc3a789832165abf86e24` before Task #28 edits.
- `cmake -S . -B build`, `cmake --build build -j4`, and the default
  `ctest --test-dir build --output-on-failure` passed; CTest was 17/17.
- The initial hot spots were `shell_smoke.cpp` 7,280 lines,
  `shell_viewport.cpp` plus `shell_viewport_ui.cpp` 5,363 lines, and
  `shell_timeline.cpp` 4,422 lines. `shell_parameters.cpp` was 2,825 lines and
  still contained the approximately 812-line parameter smoke scenario.
- `sokol_backend.cpp` was compiled into both `marrow_renderer_core` with
  `MARROW_RENDERER_SCENE_ONLY` and the `marrow_renderer` compatibility target.
  `marrow_c_smoke`, Agent dispatch smoke, runtime unit tests, and the CPU
  benchmark therefore inherited `marrow_renderer_sapp_host` plus
  Cocoa/Foundation/QuartzCore/Metal link dependencies on macOS.
- The retained pre-platform source anchor is
  `0e0539e633e9a5227fd44e9e003cda68d27fc40f`; its tracked binary diff was
  recorded before platform work as
  `d15ae2de2f4a85c2285a9988deee151b03ff1f775f5883f56d1a84e4fc5314e6`.
  The integrated SDL3/Sokol transition is
  `80b494fde00b45a3a8fb89c2324baec9fdac547c`. Those artifacts preserve
  provenance, but there is no buildable legacy comparator at the current
  feature revision, so fixed like-for-like legacy/Sokol A/B evidence is still
  unavailable and receives no qualification credit.

### Checkpoint boundaries

Task #28 is implemented and committed as independently verifiable checkpoints:

1. Synchronize authority, the frozen baseline, and the MAR-163 dependency.
2. Leave `run_headless_smoke` with orchestration and shared fixture lifetime;
   move parameter, viewport/selection, and timeline/project scenarios into
   feature test translation units without changing CTest names, labels, CLI
   exit status, diagnostics, or the single `ShellState` mutation order.
3. Make `marrow_renderer_commands` own CPU scene preparation, cache, command
   packing, PNG/geometry, and software-stencil work; compile the Sokol scene
   executor exactly once in `marrow_renderer_core`; keep app/glue and window
   adaptation in `marrow_renderer_sapp_host`. The public `marrow_renderer`
   compatibility umbrella remains available, while C ABI, Agent, runtime tests,
   fixture tests, and CPU benchmarks link only the CPU boundary.
4. Extract ImGui/Sokol/`ShellState`-free viewport rotation, signed-scale, and hit
   precedence math into a private kernel with focused table-driven tests. A
   shell-private controller retains materialization, transaction, rollback,
   begin/update/finish, and one-undo behavior.
5. Extract ImGui-free timeline identity, reconciliation, clipboard, retime,
   collision, and snap calculations into a private model. A shell-private
   controller retains session mutation, materialization, and transaction
   ownership. The direct uncached rebuild after add-key and retime remains.

| Checkpoint | Commit |
| --- | --- |
| Authority, baseline, and dependency synchronization | `8d4ac09` |
| Feature-owned shell smoke translation units | `c6d9024` |
| Renderer CPU/GPU/host ownership | `1af25e8` |
| Viewport interaction kernel and controller | `d9f25c7` |
| Timeline model and controller | `ea8931a` |

The renderer checkpoint was locally validated on 2026-08-16. The default build
uses one `sokol_scene_renderer.cpp` object in `marrow_renderer_core`, while
`sokol_backend.cpp` is compiled only by `marrow_renderer_sapp_host` and delegates
scene submission to that executor. `marrow_c_smoke`, Agent dispatch smoke,
runtime unit/fixture tests, and the CPU benchmark link
`marrow_renderer_commands` without `sokol_app`, `sokol_glue`, Cocoa, Metal,
QuartzCore, or OpenGL host dependencies. The display-enabled macOS suite passed
21/21, including the new `marrow.renderer_link_boundary` guard, and the
standalone Sokol-app adapter completed its two-frame Metal auto-close smoke.
This is implementation evidence only and does not change the `PARTIAL` platform
qualification state.

The viewport checkpoint was locally validated on 2026-08-16. The private
`viewport_interaction_kernel` has no ImGui, Sokol, or `ShellState` dependency
and owns parent/local basis math, rotation unwrap and pivot rebase, signed and
exact-zero scale mapping, press/entity hit precedence, and completion decisions.
The shell-private `viewport_interaction_controller` now owns transform and box
gesture begin/update/finish, materialization, transaction, rollback, and commit;
`shell_viewport_ui.cpp` retains drawing and input routing. The focused
table-driven kernel test covers frozen/reflected/negative-determinant bases,
singular and non-finite rejection, positive and negative 450-degree turns, the
positive 180-degree tie, 2px pivot suspend/rebase, signed and zero scale, hit
ordering, and zero-or-one history disposition. The existing editor smoke still
validates actual cancel/no-op/one-undo session behavior and preserved error
messages.

The timeline checkpoint was locally validated on 2026-08-16. The private
`timeline_model` owns track/key identity including same-time ordinal and
population, selection reconciliation, clipboard collision/order, retime
bounds and 60 FPS snapping, duration maxima, and completion disposition without
ImGui or `ShellState`. The shell-private `timeline_controller` owns imported
curve materialization, project/runtime mutation, live preview refresh,
transactions, rollback, and zero-or-one history entry; `shell_timeline.cpp`
retains input interpretation and drawing. The focused model test passed all six
cases, the editor shell smoke retained imported-curve, atomic rollback, no-op,
and one-undo coverage, and the complete default CTest passed 20/20. The two
Task #27 direct uncached track rebuilds after add-key and live retime remain in
the controller while the normal revision-keyed cache path is unchanged.

Broad `project.cpp`/session decomposition, GPU surface/resource RAII, SDL host
decomposition, and a full CMake reorganization remain follow-up analysis.
Task #28 changes no public `include/marrow/**` interface, file-format version, C ABI,
CLI, or 56-operation Agent/MCP request/response contract.

### Completion gate

Each checkpoint must pass configure/build, complete default CTest, its focused
tests and related smoke targets, plus `git diff --check` before the next starts.
The renderer checkpoint additionally requires the macOS display-enabled suite
and source/link guards proving CPU-only binaries do not inherit app/glue or host
frameworks. Final macOS Debug and Release display, third-party, project/export,
C ABI, Agent, and MCP socket evidence is required.

The implementation revision `ea8931a6cec00766285abd7144a8c94b50a60e7d`
passed this completion gate on both hosts:

- macOS default CTest passed 20/20; Debug and Release display-enabled suites
  each passed 23/23 in the real host session. Third-party hashes, project
  export/runtime load and JSON/MBIN comparison, C ABI v1, all 56 Agent
  operations, renderer link closure, and the live editor/MCP socket passed.
- Windows 11 VS2022 x64 Debug and Release builds completed, and both complete
  display-enabled CTest suites passed 23/23. Windows third-party hash
  verification passed.
- The Windows Release portable stage produced 119 manifest entries with zero
  independent hash mismatches. Its 5,118,631-byte ZIP independently matched
  SHA-256 `f0c24473e21f7f5b9efda18df48c5e42339a6d0f4fe6a0a4bb57cefd38238d9c`;
  staged-folder and new-directory extracted runs both exited 0.

Task #28, MAR-163, and MAR-164 remain complete at that checkpoint. MAR-165,
MAR-166, and MAR-167 were completed separately on 2026-08-20; MAR-168 is the
next product milestone.
Windows 11 high-DPI manual UI, physical Ink, and fixed legacy/Sokol A/B remain
deferred MAR-192 through MAR-210 qualification evidence and received no
Task #28 or platform-qualification credit. Those stories remain `open`, and
macOS and Windows qualification remains `PARTIAL`.
