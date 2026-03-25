# Marrow Agent Notes

## Project State

- This workspace is currently planning-first. The main source of truth is `docs/discription.md`.
- The folder is now a git repository. Ralph is configured to commit one completed story per iteration.
- Ralph state is written to `.ralph/` and should be treated as generated runtime state.

## Ralph Defaults

- Default PRD: `.agents/tasks/prd-marrow-runtime.json`
- Default agent runner: `codex exec --full-auto -`
- Default mode: `NO_COMMIT=false`

## Working Rules

- Read `docs/discription.md` before changing runtime or file-format decisions.
- Keep new stories small and vertical. One story should be finishable in one Ralph iteration.
- Preserve the runtime-first plan unless the active story explicitly updates it.
- If a build or test workflow is introduced, document the exact commands here.
- The checked-in PRD already expands the renderer, runtime, and editor roadmap from `docs/discription.md`. Prefer updating that PRD rather than inventing parallel plans.

## Current Validation

- Configure: `cmake -S . -B build`
- Build: `cmake --build build`
- Bootstrap smoke test: `./build/marrow_bootstrap`
- Runtime fixture smoke test: `./build/marrow_fixture_smoke`
- Runtime inspection CLI: `./build/marrow_inspect assets/fixtures/player_idle.mskl`
- Runtime binary inspection CLI: `./build/marrow_inspect assets/fixtures/player_idle.mbin`
- Binary fixture regeneration: `./build/marrow_inspect --export-binary assets/fixtures/player_idle.mbin assets/fixtures/player_idle.mskl`
- JSON vs binary runtime comparison: `./build/marrow_inspect --compare assets/fixtures/player_idle.mbin assets/fixtures/player_idle.mskl`
- AnimationState, skin, inherit timeline, skin-scoped constraint, linked-mesh, weighted-mesh, FFD deform, IK, path/transform, and physics runtime validation: `./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl`
- Rendering validation target: `./build/marrow_renderer_sample`
- Setup-pose, clipping-mask, sequence-attachment, animated slot-timeline, GPU-skinned weighted-mesh, and FFD deform validation: `./build/marrow_renderer_sample assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl`
- Slot blend-mode and two-color tint validation: `./build/marrow_renderer_sample assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl`
- Editor project load validation: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- Editor project creation validation: `./build/marrow_project_smoke --create /tmp/player_idle.marrow`
- Editor runtime export validation for transform, deform, draw-order, event, and constraint edits: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl`
- Editor runtime asset bundle export validation with optional binary output: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl --export-binary /tmp/player_idle_project_export.mbin`
- Project-export weighted mesh, FFD, and constraint round-trip validation: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl`
- End-to-end sample project export/load validation: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl --export-binary /tmp/player_idle_project_export.mbin`
- End-to-end exported project JSON vs binary comparison: `./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin /tmp/player_idle_project_export.mskl`
- End-to-end exported project render validation: `./build/marrow_renderer_sample /tmp/player_idle_project_export.mskl /tmp/player_idle.matl`
- Exported JSON vs binary project bundle comparison: `./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin /tmp/player_idle_project_export.mskl`
- Editor shell launch: `./build/marrow_editor_shell`
- Editor shell smoke validation for timeline, draw-order, event, state-preview, deform, and constraint authoring preview: `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- Fixture skeleton inspection: `python3 -m json.tool assets/fixtures/player_idle.mskl > /dev/null`
- IK fixture inspection: `python3 -m json.tool assets/fixtures/ik_constraints.mskl > /dev/null`
- Inherit timeline + skin-scoped constraint fixture inspection: `python3 -m json.tool assets/fixtures/skin_inherit_constraints.mskl > /dev/null`
- Path/transform fixture inspection: `python3 -m json.tool assets/fixtures/path_transform_constraints.mskl > /dev/null`
- Physics fixture inspection: `python3 -m json.tool assets/fixtures/physics_constraints.mskl > /dev/null`
- Fixture atlas inspection: `python3 -m json.tool assets/fixtures/player_idle.matl > /dev/null`
- Fixture editor project inspection: `python3 -m json.tool assets/fixtures/player_idle.marrow > /dev/null`
- Use `./build/marrow_renderer_sample` to verify atlas-backed setup-pose region draw preparation, clipping-mask propagation, sequence frame selection, GPU-skinned weighted-mesh draw preparation, animated slot presentation, slot blend modes, and two-color tint propagation from the checked-in fixtures
- To run the full autonomous loop with Codex against the expanded plan: `ralph build 100`
