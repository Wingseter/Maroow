# Maroow Enhancement Roadmap

> **Current authority:** `docs/root1/discription.md` owns architecture and `.agents/tasks/prd-marrow-runtime.json` owns dependency-ordered milestones. Linked enhancement plans provide scope context; each milestone closes at a focused validation checkpoint.

**Goal:** Spine parity work, Live2D-inspired authoring, AI-agent control, runtime ecosystem, and production workflow polish are separated into independent planning tracks.

**Architecture:** Keep the current runtime-first Maroow model intact: `.marrow` remains the editor source format, `.mskl`/`.mbin`/`.matl` remain runtime delivery formats, and editor automation routes through shared command/dispatcher paths. New feature tracks must be vertical, testable slices rather than parallel roadmaps that bypass `docs/root1/discription.md` and the checked-in PRD.

**Tech Stack:** C++17, CMake, Dear ImGui, sokol/OpenGL/Metal renderer paths, Maroow runtime formats, Python MCP bridge, existing smoke/benchmark targets.

---

## Scope Split

This directory is organized by product capability, not by code layer. Each document should be implementable independently, with explicit integration points where tracks meet.

| File | Scope | Primary Outcome |
| --- | --- | --- |
| `01-ai-agent-control-expansion.md` | AI/MCP control plane, permission model, agent UI, automation commands | Agents can inspect, edit, validate, and export real projects through visible, reversible operations |
| `02-live2d-parameter-deformer-system.md` | Live2D-inspired parameter, deformer, expression, lip-sync, and keyform authoring | Maroow gains a native facial/VTuber-style authoring pillar without copying Live2D formats |
| `03-spine-parity-authoring-export.md` | Spine-style authoring polish, importer/exporter completeness, media export, validation viewer | Maroow closes remaining Spine production gaps beyond core runtime compatibility |
| `04-runtime-sdk-engine-ecosystem.md` | Runtime SDK packaging, C/C++ ABI, engine bridges, Web/WASM, conformance tests | Maroow assets become easier to ship outside the editor and molga-engine |
| `05-production-workflow-ui-validation.md` | Editor UX, workflow ergonomics, diagnostics, visual renewal, docs/tutorials | The editor becomes practical for long production sessions and easier to validate |

## Current Milestone Order

1. **Completed checkpoint: imported-rig editing P0 (MAR-141~153)**
   - Honest setup/animation semantics, stable viewport interaction, dopesheet/slot authoring, animation catalog CRUD, and the shared agent foundation are validated.

2. **Completed checkpoint: Maroow Parameter Modeling (MAR-122~128)**
   - MAR-121 is a done tracking tombstone; its runtime foundation is implemented as part of MAR-122 rather than as a separate checkpoint.
   - Dependency order is runtime definitions/project export, 1D shapes, warp/rotation deformers, ArtPath, expression/lip-sync, editor mode, then agent/MCP parity.

3. **Active backlog: imported-rig editing P1 (MAR-154~191)**
   - The MAR-128 gate passed on 2026-07-16, MAR-154–155 passed on 2026-07-17, and MAR-156 passed on 2026-07-18. The remaining linear dependency chain recorded in the PRD and `editing-gap-analysis.md` begins with MAR-157.

4. **Deferred enhancement tracks**
   - Broader AI control, production workflow, Spine parity/export, and runtime SDK work remain useful capability plans, but none overrides the active MAR-157 P1 milestone.
   - Native rig/mesh topology authoring remains blocked on a versioned canonical `.marrow` authoring-graph decision.

## Cross-Track Rules

- Keep `docs/root1/discription.md` as the architecture source of truth unless an active milestone explicitly updates it.
- Treat `.agents/ralph/`, `.ralph/`, and `docs/root1/ralph-loop.md` as preserved historical artifacts, not current execution authority.
- Prefer updating `.agents/tasks/prd-marrow-runtime.json` over creating a competing implementation roadmap.
- Any file-format change must include fixture updates and validation commands.
- Any agent-facing edit operation must be undoable and visible in the editor.
- Any external format import must be independently implemented from public documentation and must not copy proprietary runtime code.

## Baseline Validation Commands

Use the smallest relevant subset per milestone checkpoint, then run broader checks before closing a track:

```bash
cmake -S . -B build
cmake --build build
./build/marrow_unit_tests
./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_e2e_export.mskl --export-binary /tmp/marrow_e2e_export.mbin
./build/marrow_fixture_smoke /tmp/marrow_e2e_export.mskl /tmp/player_idle.matl
./build/marrow_inspect --compare /tmp/marrow_e2e_export.mbin /tmp/marrow_e2e_export.mskl
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 5
```

Agent-control work should also validate:

```bash
source tools/mcp/venv/bin/activate && python3 tools/mcp/test_client.py
```

Renderer/export work should also validate:

```bash
./build/marrow_renderer_sample --hud --skip-render assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
./build/marrow_renderer_sample --auto-close 2 assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
```

Performance-sensitive runtime work should also validate:

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release
cmake --build build-bench --target marrow_benchmark
./build-bench/marrow_benchmark --skeletons 200
./build-bench/marrow_benchmark --constraint-drive partial
```

## Done Definition

- Each enhancement track has its own plan document in this directory.
- Each plan states current state, scope boundaries, vertical phases, validation commands, risks, and acceptance criteria.
- Follow-up implementation work is represented in the checked-in PRD as dependency-ordered milestones without inventing another planning structure.
- No plan requires unrelated rewrites before a first useful slice can ship.
