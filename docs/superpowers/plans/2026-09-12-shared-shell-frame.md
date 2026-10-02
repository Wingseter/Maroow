# Shared Shell Frame Execution — Phase 2 Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans to execute the approved Phase 2 scope task-by-task. Checkboxes below track implementation and evidence, not a new product roadmap.

**Goal:** Make the application and headless smoke execute one production frame composition, and remove authoring smoke scenarios from the product executable.

**Architecture:** A concrete `draw_shell_frame(ShellState&, double elapsed_seconds)` coordinator owns session synchronization, asset-watch throttling, playback, shortcuts, window composition, orphan-gesture finalization and deferred file actions. Hosts own ImGui frame begin/end and SDL/GPU or injected test inputs. A shared shell library is linked by the production executable and a BUILD_TESTING-only smoke executable; both compile the same application host, with headless dispatch enabled only in the smoke executable.

**Tech Stack:** C++17, vendored Dear ImGui/SDL3/Sokol, CMake/CTest, Python 3.9+ standard library.

**Spec:** `docs/refectoring_needs_checking.md`, Phase 2 (six ordered items).

## Constraints and decisions

- Preserve `EditorSession`/`EditTransaction`, runtime formats and the existing model/renderer boundaries. No Phase 3 gesture registry or state-mirror redesign.
- Continue in the selected `feat/mar-168` checkout. Preserve all pre-existing Phase 1 and unrelated dirty files; no reset, stash, commit or push.
- Use the application's ordering as the production contract. Keep real elapsed time for the 0.25-second asset-watch accumulator and ImGui DeltaTime for playback/parameter advancement. No test-specific polling bypass in the coordinator.
- The coordinator runs after ImGui frame begin and before render/end. SDL events, command draining, surface acquisition, graphics submission and present stay in the host. Skipped drawable frames must not run the coordinator or count toward auto-close.
- Specialized single-panel smoke scenarios remain specialized; only full-shell composition must delegate to production.
- Product `marrow_editor_shell --auto-close N` becomes a real display run on macOS too. Headless authoring scenarios move to `marrow_editor_shell_smoke --project ... --auto-close N`. CTest names and existing assertions remain. `MARROW_DISPLAY_SMOKE=1` remains supported for display qualification.
- Do not delete the old frame guard before migration. Reassess it after both hosts delegate: retain a narrow host-to-coordinator wiring guard if necessary, not a duplicate window-name oracle.

## Task 1 — Baseline and executable RED

- [x] Read Phase 2, current hosts, CMake and active working rules.
- [x] Confirm checkout/branch and existing dirty changes.
- [x] Build `cmake --build build-sentinel -j 6`.
- [x] Baseline `ctest --test-dir build-sentinel --output-on-failure`: 25/25 passed. An initial 10-second tool timeout interrupted the first attempt; the complete rerun passed in 32.71 seconds.
- [x] Add a real-ImGui regression to the current shared smoke frame: enable the Agent panel and require its window to be active. Run the existing shell smoke and capture the current missing-window failure before changing production.

## Task 2 — Shared coordinator and host migration

**Files:** create `src/editor/shell_frame.hpp` and `.cpp`; modify `shell_main.cpp`, `shell_smoke_frames.cpp`, and the full-frame helper in `shell_smoke_parameters.cpp` if applicable.

**Interface:** `void draw_shell_frame(ShellState& state, double elapsed_seconds);` requires an active ImGui frame. It does not begin/end that frame, drain sockets or touch the window host.

- [x] Extract the existing application state-update/composition/finalization body without changing window order, mode guards or Agent layout invalidation order.
- [x] Route the application and shared headless frame through it exactly once; leave fixture setup and dock-layout assertions in the smoke host.
- [x] Remove obsolete twin-edit comments and duplicated full-frame mutations.
- [x] Add actual coordinator regressions for conditional windows, session synchronization, watch throttle, orphan finalization and deferred actions; verify the original RED becomes GREEN.

## Task 3 — Build ownership and guard replacement

**Files:** modify `CMakeLists.txt`, `shell_main.cpp`, `cmake/CheckFrameBodies.cmake`; add focused boundary tests under `tools/tests/` as needed.

- [x] Put production shell implementation in `marrow_editor_shell_core`; keep the platform entry point in `marrow_editor_shell`.
- [x] Build `marrow_editor_shell_smoke` only under BUILD_TESTING, with the same entry point plus smoke sources and a private headless-dispatch definition.
- [x] Preserve platform libraries, Sokol definitions, fonts/icons, portable staging and display-test targets.
- [x] Route both headless CTests to the smoke executable, leaving production/display tests on the product binary.
- [x] Replace list-equality checking only after migration with a non-vacuous host wiring guard, and prove it rejects missing coordinator calls and reintroduced direct composition.
- [x] Verify a BUILD_TESTING=OFF product configure/build and absence of smoke code in the product linkage.

## Task 4 — Review, regression and evidence

- [x] Review scoped diffs, especially shared CMake changes, ordering, exception/early-return paths and retained smoke assertions.
- [x] Run final build, focused frame tests, full CTest, Python MCP transport/stdio regressions and the frame guard.
- [x] Update this execution record, the Phase 2 status in the assessment and the current validation commands in AGENTS.md. Historical AGENTS records are not rewritten.
- [x] Record exact executed results and distinguish macOS headless/build evidence from unexecuted Windows/display/GPU qualification.

## Execution evidence

### Status — 2026-09-12

Phase 2 implementation and local macOS build/headless regression are complete. Work remains uncommitted on `feat/mar-168`; pre-existing Phase 1 and unrelated dirty files were retained. No reset, stash, branch switch, commit or push was performed.

The resumed session found `shell_frame.hpp/.cpp` and both host delegations already present, but not registered in CMake. The previous execution checkpoint had established the Agent-enabled shared-frame RED; the resumed run verified that unchanged assertion becomes GREEN through the shared coordinator.

### Implemented boundaries

- `src/editor/shell_frame.cpp`: one concrete production composition. Session sync runs after the host begins ImGui, watch uses the host's real elapsed time, and playback/parameter progression uses ImGui DeltaTime. Window order and Agent next-frame dock invalidation preserve the application's previous sequence. Deferred session replacement runs after composition/finalizers and before host render.
- `shell_main.cpp`: drawable acquisition, skipped-frame handling, events, command drain, ImGui begin/render and graphics submit/present remain host-owned. The only headless dispatch is compiled under `MARROW_ENABLE_HEADLESS_SMOKE`, a PRIVATE definition of the test executable. The product's `--auto-close` path is a real display run; the test host can still opt into display qualification with `MARROW_DISPLAY_SMOKE=1`.
- `CMakeLists.txt`: production implementations compile once into `marrow_editor_shell_core`, linked by the product host and BUILD_TESTING-only smoke host. Fonts/icons, platform libraries, Sokol definitions, display tests and product portable staging retain their ownership. Both original headless CTest names/assertions remain; only their executable changes.
- `shell_smoke_parameters.cpp` deliberately remains unchanged: its final loop is a specialized parameter-panel no-input/no-history test, not a second full-shell composition. Other focused single-panel smoke helpers also remain specialized.
- `cmake/CheckFrameBodies.cmake` now checks live delegation regions rather than carrying another list of expected windows. It is a dependency of the shared core, so incremental/no-op host builds still run it. It excludes ordinary comments and strings, rejects missing/duplicated delegation and direct composition, and forbids host lifecycle work in the coordinator. It is explicitly a narrow source-wiring check, not a semantic C++ parser or proof of all draw order.

### New executable contracts

`src/editor/shell_smoke_frame_contract.cpp` runs eight groups against the production coordinator with a real ImGui context and a real EditorSession. The smoke host owns context lifetime and isolated preferences. The focused CTest uses `MARROW_FRAME_CONTRACT_ONLY=1`, so its UI state cannot contaminate existing smoke scenarios.

1. Agent hidden/shown/hidden composition and next-frame dock rebuild.
2. Parameter-only windows enabled in Parameter mode and absent in Setup.
3. Session-side preview revision synchronized into a deliberately stale shell mirror.
4. Playback advances once using ImGui DeltaTime, independently of watch elapsed time.
5. Actual asset-watch polling occurs only after the 0.25-second accumulator threshold.
6. A real active coalesced edit defers watch polling; orphan finalization clears it at frame end and the next frame consumes accumulated time.
7. Deferred Open replaces the session and refreshes runtime aliases before host render.
8. An input-free stopped frame preserves serialized project, project revision, dirty state and undo/redo history.

`tools/tests/test_shell_frame_boundary.py` has eight mutation-test methods. Its isolated fixtures run the actual CMake guard and exercise normal delegation, missing/duplicate calls in either host, comment/string false positives, direct composition, later-scenario false positives, an empty coordinator and misplaced host lifecycle calls.

`tools/tests/check_shell_build_boundary.py` inspects the generated compile database, not a duplicated CMake source list. Optional `--nm` independently inspects unstripped macOS/Linux executables. It is an explicit artifact-verification command rather than a default CTest dependency on a particular generator or symbol tool.

### Observed RED and restoration evidence

- Before guard replacement, `python3 tools/tests/test_shell_frame_boundary.py -v` exited 1: eight methods produced 15 failing assertions/subtests against the obsolete twin-frame anchors. After replacement the same suite passed all eight methods.
- Before target separation, `cmake --build build-sentinel --target marrow_editor_shell_smoke -j 6` exited 2 because the target did not exist. The registered target subsequently built and ran the original headless assertions successfully.
- A deliberate production mutation removed only `finalize_orphaned_coalesced_edit(&state)` from the coordinator. Compilation and the narrow wiring guard still passed, but `ctest --test-dir build-sentinel --output-on-failure -R "^marrow.shell_frame_contract$"` exited 8 with `coordinator did not finalize an orphaned coalesced edit`. This demonstrates why behavioral coverage is separate from source wiring.
- Restored `shell_frame.cpp` to the exact pre-probe SHA-256 `71b53d4a4567b114fa1b54b118ce34e848788a80b5cc3a24bbada2ca380b8988`, rebuilt both hosts and reran the entire suite: **27/27 passed, 0 failed, 24.57 seconds**. No mutation remains in the source.

### Executed validation commands and results

| Command | Observed result |
| --- | --- |
| `cmake -S . -B build-sentinel -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` | Configure/generate passed with existing test configuration preserved. |
| `cmake --build build-sentinel -j 6` | Full build passed; both hosts linked; shared wiring guard passed, including incremental runs. |
| `ctest --test-dir build-sentinel --output-on-failure -V -R "^marrow.shell_frame_contract$"` | Focused real-ImGui test passed; output: `Shared shell frame contract: 8 groups passed.` |
| `python3 tools/tests/test_shell_frame_boundary.py -v` | Eight methods passed; also covered by CTest. |
| `ctest --test-dir build-sentinel --output-on-failure` | Final restored-source run: **27/27 passed**, including all 25 baseline registrations plus two new frame tests. |
| `python3 -W error::ResourceWarning tools/mcp/test_transport.py -v` | **20/20 passed**; no ResourceWarning failure. |
| `tools/mcp/venv/bin/python -W error::ResourceWarning tools/mcp/test_stdio.py -v` | **1/1 passed**, actual MCP server tools round-trip and peer EOF on exit. |
| `python3 tools/tests/check_shell_build_boundary.py --build-dir build-sentinel --testing on --nm nm` | Passed: core **31** translation units, product host **1**, smoke host/scenarios **12**. Both executables contain the shared coordinator; only the smoke executable contains headless/contract smoke symbols. |
| `cmake -S . -B build-phase2-product -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMARROW_ENABLE_DISPLAY_TESTS=OFF` | Fresh product-only configure/generate passed. |
| `cmake --build build-phase2-product --target marrow_editor_shell -j 6` | Fresh product-only build passed, including common core, platform/renderer dependencies and resources. |
| `python3 tools/tests/check_shell_build_boundary.py --build-dir build-phase2-product --testing off --nm nm` | Passed: core **31**, product host **1**, authoring smoke **0**. No smoke executable or headless/contract smoke symbols; product contains the production coordinator. |

The compile-database verifier requires Makefiles/Ninja and `CMAKE_EXPORT_COMPILE_COMMANDS=ON`; its optional symbol check expects an unstripped executable and `nm`/`llvm-nm`. Its portability description is not a claim that Linux or Windows were executed here.

### Review and qualification limits

Scoped CMake/source diffs were reviewed directly against the approved ordering and target-ownership requirements. No independent reviewer process was run. AGENTS received only four new current-workflow lines; its historical records and Phase 1 entries were retained. The assessment's historical analysis remains intact, with a separate current Phase 2 status above it.

The executed evidence is macOS local build/headless, compile/link inspection, isolated TCP and MCP stdio regression. Windows/Linux runtime behavior, visible-window/GPU/portable qualification, actual-editor Python mutation E2E and new performance limits were **not** qualified. The display/portable targets were preserved, not claimed as executed. Phase 3 state mirrors/gesture lifecycle, Phase 4 project/file-format responsibilities and historical AGENTS cleanup remain outside this change.
