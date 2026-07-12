# Editor Architecture Refactor Roadmap

This document describes the editor architecture gate between the runtime-first parameter stories MAR-120–126 and parameter authoring story MAR-127. The architecture and file-format source of truth remains [`discription.md`](discription.md); this document owns the refactor sequence and compatibility boundaries.

## Why this refactor exists

At the start of the 2026-07-12 refactor, `src/editor/shell_main.cpp` was 10,310 lines and `ShellState` owned project data, compiled runtime state, playback, history, agent state, UI resources, selection, and editor gestures. Project history was also exposed through `ProjectCommandStack`, agent dispatch accepted `ShellState`, and operation metadata was maintained separately from string-branched handlers. The `marrow_editor` target compiled shell/UI code, including an `icon_registry.cpp` source that was also compiled into `marrow_editor_shell`.

The refactor replaces those overlapping ownership paths before new parameter-modeling UI is added. It is a behavior-preserving change: it does not add parameter/deformer features or change runtime/file-format decisions.

### Current implementation checkpoint

The refactor is implemented in the current working tree while the new PRD entries remain `todo` for Ralph's one-story-per-iteration execution record:

- `shell_main.cpp` is 748 lines and is limited to CLI/platform startup, theme and docking setup, frame composition, and shutdown; `shell_state.hpp` is 606 lines and is the UI-only state entry point.
- The legacy `shell_types.hpp` and private shell undo stack are gone.
- Preview/playback, asset watching, timeline, constraints, selection, inspector, weight-paint, viewport UI, project/runtime panels, and agent panels have feature-owned source/header pairs.
- `marrow_editor` contains UI-free project/session/agent authoring code and links only `marrow_runtime` and Zlib; icon/UI/OpenGL code is compiled only into `marrow_editor_shell`.
- The C API and socket dispatcher use `EditorSession` plus `AgentControlState`, and C ABI version 1 is unchanged.
- CTest discovers seven source-root compatibility tests, and the agent smoke exercises all 44 registered operations.

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

History remains capped at 100 entries and keeps current merge/grouping behavior. Preview skin and attachment composition remains transient and does not mark the project dirty, but it remains undoable and redoable. `ProjectCommand`, `ProjectCommandStack`, and `make_project_command` are removed after production call sites and their smoke coverage move to `EditorSession`.

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
| MAR-121–126 | Runtime-first parameter definitions, export, shapes, deformers, ArtPath, expressions/lip-sync | Sequential from MAR-120 | Todo |
| MAR-129 | Focused CTest guardrail | MAR-126 | Todo |
| MAR-130 | Characterize external behavior and all 44 agent operations | MAR-129 | Todo |
| MAR-131 | Unified session transactions and history | MAR-130 | Todo |
| MAR-132 | Preview controller | MAR-131 | Todo |
| MAR-133 | EditorSession lifecycle and ownership | MAR-132 | Todo |
| MAR-134 | C API and agent-context migration | MAR-133 | Todo |
| MAR-135 | Registry-driven dispatcher and handler modules | MAR-134 | Todo |
| MAR-136 | Timeline module | MAR-135 | Todo |
| MAR-137 | Constraint module | MAR-136 | Todo |
| MAR-138 | Selection and inspector modules | MAR-137 | Todo |
| MAR-139 | Weight-paint and viewport modules | MAR-138 | Todo |
| MAR-140 | Composition and build-target cleanup | MAR-139 | Todo |
| MAR-127 | Parameter-modeling editor tools | MAR-140 | Todo |
| MAR-128 | Parameter/deformer agent commands | MAR-127 | Todo |

Numeric IDs are intentionally not execution order: MAR-129–140 are inserted before MAR-127 without renumbering existing stories.

## Compatibility boundary

The refactor must preserve:

- C ABI version 1, all C functions, status codes, and ownership rules;
- all 44 existing agent operations, JSON request/response shapes, error messages, permissions, dry runs, reviews, and IDs;
- `.marrow`, `.mskl`, `.mbin`, and `.matl` schemas and versions;
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
