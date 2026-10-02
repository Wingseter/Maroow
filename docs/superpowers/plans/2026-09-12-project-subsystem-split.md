# Project Subsystem Split Implementation Plan

> **For agentic workers:** Use `superpowers:executing-plans` to execute these checkpoints in order; verify each extraction before proceeding.

**Goal:** Separate `project.cpp` by lifecycle responsibility without changing public APIs, file formats, validation/error ordering, or serialized output.

**Architecture:** Keep the public model/authoring helpers in `project.cpp`. Extract private JSON vocabulary/value codecs, overlay folds, parsing, serialization, validation, runtime materialization, and filesystem orchestration into separate translation units in the existing `marrow_editor` library. Private declarations stay under `src/editor`, not `include/marrow`.

**Tech Stack:** C++17, CMake, CTest, Python 3 standard-library boundary tests.

**Spec:** `docs/refectoring_needs_checking.md`, Phase 4; architecture authority remains `docs/root1/discription.md`.

## Global constraints and starting state

- Preserve `.mskl` v1, `.mbin` v2 and C ABI v1. No public header/schema changes.
- Preserve unknown additive data at the locations supported by the existing implementation; do not invent new normalization/validation behavior.
- Preserve parameter replacement -> animation operations -> weights -> timeline overlays -> constraint lifecycle -> constraint upserts ordering.
- Save As must rebase against the old directory before validation/serialization; atomic project writes remain unchanged.
- Keep `load_project(Document)` an asset-loading public API. Introduce a separate private parse-only seam; successful parsing alone is not a successful public project load.
- Preserve all existing Phase 3 changes on `feat/mar-168`. Work in the selected continuation checkout; do not reset, stash, commit, push, delete unrelated files, or create a different source baseline.
- `AGENTS.md` historical cleanup is not part of this phase. Its 749,921-byte body exceeds the connector read limit, so durable rules were inspected using targeted search.
- Starting `project.cpp`: 8,745 lines; SHA-256 `4466a25c9a0def24a2ab8f47509f25272d6d232ab65afc39debcc9c28f531ff7`.
- Fresh starting build: `cmake --build build-sentinel -j 6`, exit 0. Fresh CTest: `ctest --test-dir build-sentinel --output-on-failure`, 28/28 passed.

## Dependency map (pre-extraction line anchors)

| Responsibility | Original ownership | Dependencies / new seam |
|---|---|---|
| JSON vocabulary and shared value encoding | 33-49, 1608-1809, 1994-2014, 4245-4276, shared builders in 4498-4778 and 5114-5190 | JSON, public model types; one definition for enum spelling and project/runtime-identical value shapes |
| Parse | 50-1550, 1810-1982, 2015-4244, 4278-4426, 8074-8227 | JSON codecs, authoring vocabulary, mesh-weight limits; parse-only `optional<LoadError> parse_project_document(const Document&, ProjectData*)` |
| Serialize | 1983-1993, project-only builders 4427-5460, 6073-6076, 7156-7159 | JSON codecs and typed parameter builders; `serialize_project` remains public |
| Overlay folds | 5461-5564, 5615-5725, 7122-7155 | JSON codecs; constraint request primitives and runtime consume the same folds |
| Validation | 5726-5825, 6077-6879, 8470-8565 | JSON codecs, authoring vocabulary, model queries; no asset loading |
| Runtime materialization | runtime-specific value builders, 5565-5614, 5826-6072, 7116-7121, 8566-8590 | Shared codecs/folds/validation, runtime loader; no filesystem orchestration |
| Filesystem orchestration | 6896-7113, asset-loading tail 8229-8271, 8591-8743 | Parse, validate, serialize, materialize; atomic writer, atlas packer, runtime file loading/export |
| Model / path / authoring primitives | 1551-1607, 6880-6895, 7160-8073, 8273-8469 | Model queries, shared vocabulary, lifecycle fold and validation; public signatures unchanged |

`build_runtime_transform_keyframes_value` and `build_runtime_slot_color_keyframes_value` must remain separate from project-only encoders: auto-curve authoring intent must not leak into runtime files. Shared deform/inherit/attachment/constraint/parameter builders are moved once, not copied into both consumers.

## Checkpoints

- [x] Inspect source, public lifecycle boundaries, architecture rules, dirty state, and fresh 28-test baseline.
- [x] Add regression/boundary tests before extraction. Demonstrate expected RED for missing separated boundaries; freeze existing public save/load/export behavior before moving code.
- [x] Extract shared JSON/value codecs and private declarations. Rebuild and run focused project/parameter/C ABI regressions.
- [x] Extract validation without changing diagnostic order. Rebuild and rerun the same regression gate.
- [x] Extract project serialization; retain preserved-root merging and distinct runtime formats. Rebuild and rerun the regression gate.
- [x] Extract shared overlay folds and runtime materialization in separate verified steps. Keep all application ordering intact.
- [x] Extract the private parse-only function and filesystem orchestration. Test that parse does not load assets, failure does not overwrite output, and the public load still loads/fails on assets.
- [x] Review dependency ownership, private/public symbols, function-body preservation, and CMake compile units. Run mutation tests for preservation/order/rebasing and restore source hashes.
- [x] Run full CTest, MCP Python/stdio, and `BUILD_TESTING=OFF` product build/boundary checks. Record evidence and limitations in this file and the refactoring assessment.

## Regression commands

```sh
cmake -S . -B build-sentinel -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-sentinel -j 6
ctest --test-dir build-sentinel -R 'marrow\.(project_smoke|parameter_project_smoke|c_abi_smoke|project_subsystem)' --output-on-failure
ctest --test-dir build-sentinel --output-on-failure
python3 -W error::ResourceWarning tools/mcp/test_transport.py -v
tools/mcp/venv/bin/python -W error::ResourceWarning tools/mcp/test_stdio.py -v
cmake -S . -B build-phase2-product -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DMARROW_ENABLE_DISPLAY_TESTS=OFF
cmake --build build-phase2-product --target marrow_editor_shell -j 6
python3 tools/tests/check_shell_build_boundary.py --build-dir build-phase2-product --testing off
```

The exact new tests and fresh results are recorded below as they are executed. Existing regression results are not a claim of Windows/Linux/display/GPU qualification.

## Execution evidence

- Baseline build and 28/28 CTest passed before any Phase 4 production edit.
- Extracted 7 responsibility translation units (`project_json`, `project_parse`, `project_validation`, `project_overlay`, `project_runtime`, `project_serialize`, `project_io`) and private headers (`project_internal.hpp`, `project_json.hpp`).
- Verified 192 of 193 original function bodies unchanged byte-for-byte; only `load_project` plumbing updated.
- 4 intentional mutation checks passed and rejected expected errors (`unknown_root_loss`, `constraint_order_reversal`, `partial_parse_commit`, `save_as_without_rebase`), with original sources verified restored.
- Full build and CTest passed with 30/30 tests (including `marrow.project_subsystem` and `marrow.project_subsystem_boundary`).
- `BUILD_TESTING=OFF` product shell build passed boundary verification.
