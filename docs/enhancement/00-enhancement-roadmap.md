# Maroow Enhancement Roadmap

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement one linked enhancement plan at a time. Steps in the linked plans use checkbox syntax for tracking.

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

## Recommended Execution Order

1. **P0: AI Agent Control Expansion**
   - This is Maroow's clearest differentiator against Spine and Live2D.
   - It also creates automation hooks that can accelerate the other tracks.

2. **P1: Production Workflow/UI Validation**
   - The core feature set is already broad, but the editor needs stronger discoverability, diagnostics, and mode clarity before adding more authoring concepts.

3. **P1: Spine Parity Authoring/Export**
   - Existing Spine runtime/import coverage is strong; remaining work is mostly production polish, export formats, and compatibility policy.

4. **P2: Live2D Parameter/Deformer System**
   - This is a new authoring model and should not be bolted onto existing FFD casually. Start after the project has better diagnostics and agent-visible editing primitives.

5. **P2: Runtime SDK/Engine Ecosystem**
   - Begin ABI/package hardening early, but defer broad engine bridges until format changes from the parameter/deformer work are clearer.

## Cross-Track Rules

- Keep `docs/root1/discription.md` as the architecture source of truth unless a story explicitly updates it.
- Treat `.ralph/` as generated runtime state.
- Prefer updating `.agents/tasks/prd-marrow-runtime.json` over creating a competing implementation roadmap.
- Any file-format change must include fixture updates and validation commands.
- Any agent-facing edit operation must be undoable and visible in the editor.
- Any external format import must be independently implemented from public documentation and must not copy proprietary runtime code.

## Baseline Validation Commands

Use the smallest relevant subset per story, then run broader checks before closing a track:

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
- Follow-up implementation stories can be copied into the Ralph PRD without inventing another planning structure.
- No plan requires unrelated rewrites before a first useful slice can ship.
