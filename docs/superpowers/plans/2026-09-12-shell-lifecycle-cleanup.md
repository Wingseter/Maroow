# Shell Lifecycle Cleanup — Phase 3 Implementation Plan

> **For agentic workers:** Use superpowers:executing-plans (or subagent-driven-development when an executor is available). Track execution with the checkboxes below.

**Goal:** Remove stale runtime aliases and make authoring-gesture activity and cancellation derive from one ownership list without moving transient UI state into session history.

**Architecture:** Keep EditorSession authoritative. Replace the two cached preview pointers with source-private, const-correct ShellState accessors that resolve the current session on every call. Keep playback/composition working values where existing ImGui edits and snapshot assembly require them. Use one typed gesture visitor, with explicit cancellation policies for snapshot-backed and selection-restoring gestures.

**Tech Stack:** Existing C++/CMake editor, EditorSession transactions, existing headless smoke, Python transport tests; no new dependency.

**Spec:** `docs/refectoring_needs_checking.md`, Phase 3 and sections 3.1/3.2.

## Constraints and baseline

- Continue the user's existing `feat/mar-168` checkout. No reset, stash, branch switch, worktree creation, commit or push; preserve `Untitled` and the pre-existing untracked build directories.
- Phase 1 and Phase 2 are already committed (`7925795`, `9d50276`). Baseline `ctest --test-dir build-sentinel --output-on-failure`: **27/27 passed**, before source changes.
- No file-format/runtime algorithm decisions; no Phase 4 project split; no AGENTS historical-document migration.
- Preserve session/history, UI-free models, renderer layering and BUILD_TESTING-only test source boundaries.
- Preserve cancellation order, transaction release, viewport selection/FFD vertex selection/hierarchy anchor/timeline focus, and weight-paint snapshot rollback. Bare drag candidates do not count as authoring edits.

## Ownership classification

| State | Authority / decision |
|---|---|
| Project data, runtime, history, dirty flag, canonical PreviewState | EditorSession. `load_result` is an existing bound reference, not an independent copy; session identity must remain stable while ShellState lives. |
| Cached `preview_skeleton`, `animation_state` | Remove stored raw aliases. Non-const access uses EditorSessionShellBinding; const access uses the public const session view. A returned pointer is only valid until the next session mutation; never retain one across replacement. |
| Animation/time/loop/playing/queue/mix/reverse; skins/attachment overrides | Session-authoritative with shell working copies used by ImGui widgets and pre-commit snapshot assembly. Retain in this phase; revision sync includes session close rather than returning early. |
| `project_dirty` | Read-mirror removal candidate. Existing legacy smoke authors mutate it directly, so keep the compatibility cache in this phase. Session remains the authority for the dirty-intent gate. |
| Root-motion values | Read-mirror candidates, but their explicit reset is coupled to preview recomposition; retain rather than silently change that lifecycle. |
| Preview events | Not a pure read mirror: shell preview/timeline paths clear and append events directly. Retain until event collection has its own explicit ownership contract. |
| Selection, hierarchy anchor, timeline/vertex focus, gestures, camera, preview speed, tool settings, preferences, window resources | Shell-owned transient/presentation state. Do not add to EditorSession/history. Specialized cancellation restores only the pre-existing gesture-owned subset. |
| Derived track/slot/watch caches and observed revisions | Shell-owned caches with revision/source identity keys. Preserve their existing invalidation contracts. |

## Task 1 — Runtime views and close synchronization

Files: `src/tests/shell_lifecycle_tests.cpp`, `CMakeLists.txt`, `src/editor/shell_state.hpp`, `src/editor/shell_core.cpp`, and existing shell consumers of the two pointer fields.

- [x] Add BUILD_TESTING-only `marrow_shell_lifecycle_tests` linked to `marrow_editor_shell_core`, with CTest `marrow.shell_lifecycle` and source-root working directory.
- [x] RED: open a session, replace it directly without shell sync, compare both shell aliases with the actual session pointers; close and require null views. Exercise transaction refresh/cancel, undo/redo and failed open as separate boundaries. Also close after playback and call revision sync; require cleared working state and current observed revisions.
- [x] Replace stored aliases with `preview_skeleton()` / `animation_state()` const and non-const accessors. Update consumers mechanically, removing alias assignments rather than retaining a second binding path.
- [x] Rename `sync_shell_preview_aliases_to_runtime` to `normalize_shell_preview_composition_to_runtime`: it only reconciles shell working composition, never refreshes pointers. Route existing composition-normalization callers through it.
- [x] Let both session-sync functions observe the empty-session state. Keep UI working-value, event and root-motion semantics otherwise unchanged.
- [x] GREEN: focused lifecycle tests and existing shared-frame contract; compile all callers so invalid mutable access from a const shell view is caught.

## Task 2 — Gesture lifecycle policies

Files: new `src/editor/shell_gesture_lifecycle.cpp`, `src/editor/shell_state.hpp`, `src/editor/shell_core.cpp`, `CMakeLists.txt`, lifecycle tests.

- [x] Test each of the 12 existing authoring owners independently: activity gates commands; cancellation releases live transactions/snapshots; the next edit can begin; no history entry is created; cancellation is idempotent.
- [x] RED: common cancellation of timeline retime and scale must restore original selected-key references and the original active key, as their dedicated cancellation paths already do.
- [x] Move `authoring_gesture_active` and `cancel_authoring_gestures` to one implementation unit. Both visit a single ordered list; add a typed default transaction policy plus overloads for pending/coalesced edits, weight paint, viewport transform/FFD and timeline retime/scale.
- [x] Reuse dedicated retime/scale finish functions with `commit=false`; preserve the public common-cancel status message. Preserve viewport custom snapshots and the existing candidate/pointer-mediator reset behavior.
- [x] Verify viewport transform and FFD restore entity selection, hierarchy anchor, timeline focus and (FFD only) vertex selection. Verify weight-paint/coalesced rollback restores serialized project/preview without adding undo/redo entries.
- [x] Verify bare candidates do not report an edit but are cleared, and null/empty/repeated cancellation is harmless.

## Task 3 — Regression, review and evidence

- [x] Build all targets: `cmake --build build-sentinel -j 6`.
- [x] Run all CTest: `ctest --test-dir build-sentinel --output-on-failure`.
- [x] Run MCP transport: `python3 -W error::ResourceWarning tools/mcp/test_transport.py -v` and real stdio integration: `tools/mcp/venv/bin/python -W error::ResourceWarning tools/mcp/test_stdio.py -v`.
- [x] Rebuild BUILD_TESTING=OFF product using the existing `build-phase2-product`; run `tools/tests/check_shell_build_boundary.py` with its supported arguments. Confirm lifecycle tests are absent from the product compile units.
- [x] Perform an explicit omission/rollback mutation against the lifecycle tests, restore the exact production content, then rerun final verification.
- [x] Review implementation/CMake diffs, record exact test evidence and limitations here, and update Phase 3 status in the assessment. Leave changes uncommitted. The optional AGENTS command update is tracked separately below because the native editor rejects the file-size limit.

## Execution evidence

- Initial repository has no tracked modifications. Baseline CTest: 27/27 passed (24.75 s).
- RED before production changes: `ctest --test-dir build-sentinel -R marrow.shell_lifecycle --output-on-failure` exited 8, **13/20 passed**. Seven failures reproduced stale views after direct replacement/close/runtime refresh/undo-redo, stale closed-session working state, and lost retime/scale key selections. The initial test-host link error was fixed by supplying the four host-owned null font globals; it was not counted as a behavioral RED.
- GREEN: the same 20 cases passed after removing pointer storage, handling closed-session synchronization and unifying cancellation. Existing production and smoke callers compile with the const/non-const accessors; no existing behavioral assertions were removed.
- Mutation proof: temporarily removed `visit(state.parameter_geometry_gesture)` and the FFD `vertex_selection_before` restoration. Rebuilt the focused target; CTest exited 8 with **18/20 passed**, failing exactly `parameter geometry owner: transaction owner is not active` and `viewport FFD selection rollback: FFD cancel lost vertex selection`.
- Restored the exact production file SHA-256 `290c2e9cc5581788ed742f559830cab7fc5c9d01133f1b80515a611fcc74b082`. The final all-target rebuild and full CTest then passed again. No mutation is left in the source or the test build.

### Executed final verification

| Command / inspection | Result |
|---|---|
| `cmake --build build-sentinel -j 6` | All targets built, exit 0, after mutation restoration and smoke comment updates. |
| `ctest --test-dir build-sentinel --output-on-failure` | **28/28 passed**, 25.06 s; includes the new 20-case lifecycle executable, shared-frame contract, existing editor/parameter smoke and all previous tests. |
| `python3 -W error::ResourceWarning tools/mcp/test_transport.py -v` | **20/20 passed**, no ResourceWarning failure. |
| `tools/mcp/venv/bin/python -W error::ResourceWarning tools/mcp/test_stdio.py -v` | **1/1 passed**, real stdio server round trip and peer EOF on exit. |
| `cmake -S . -B build-phase2-product -DBUILD_TESTING=OFF -DCMAKE_EXPORT_COMPILE_COMMANDS=ON` | Configure succeeded; reused the existing product-only build directory. |
| `cmake --build build-phase2-product --target marrow_editor_shell -j 6` | Product built, exit 0. Production lifecycle content matches the restored SHA above. |
| `python3 tools/tests/check_shell_build_boundary.py --build-dir build-phase2-product --testing off --nm nm` | Passed: core **32** translation units, product **1**, smoke **0**; symbol check enabled. |
| Native search of `build-phase2-product/compile_commands.json` for `shell_lifecycle_tests` / `marrow_shell_lifecycle_tests` | No matches: the new lifecycle test is not compiled in the product-only configuration. |

### Review and limits

- Reviewed the ShellState/core/CMake changes and the explicit owner policies against the old cancellation order. Runtime/serialization models and public session APIs are unchanged. The many consumer edits only replace stored-field reads with method calls; Fix/reimport comments no longer claim pointer identity depends on a manual sync call.
- The owner list is intentionally explicit: a future gesture still needs one registration and a corresponding test. The shared visitor prevents separate activity and cancellation lists from drifting; it is not reflection-based discovery of every ShellState field.
- A returned runtime pointer remains borrowed. This removes stored ShellState aliases, not every possible local pointer-lifetime mistake in unrelated code. Retained working values and mirror candidates are documented in the ownership table above.
- This was local implementation review, not an independent reviewer-agent run. Verification is macOS/noninteractive plus Python transport/stdio; no new interactive display/GPU, Windows/Linux or Python-to-live-C++ mutation E2E qualification is claimed.
- [ ] Optional AGENTS top-command addition: attempted native exact edit was rejected with `Edited file would be too large (1498492 bytes). Limit: 1000000 bytes.` No alternate write route was used. AGENTS remains unchanged; the focused command and ownership rules are available in this plan and the updated assessment instead. Historical AGENTS splitting remains out of scope.
- Phase 3 implementation and regression work is complete in the working tree; no commit, push, reset, stash or branch switch was performed. Existing untracked files/build directories were preserved.
