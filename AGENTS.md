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
- Rendering validation target: `./build/marrow_renderer_sample`
- Setup-pose region attachment validation: `./build/marrow_renderer_sample assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl`
- Fixture skeleton inspection: `python3 -m json.tool assets/fixtures/player_idle.mskl > /dev/null`
- Fixture atlas inspection: `python3 -m json.tool assets/fixtures/player_idle.matl > /dev/null`
- Use `./build/marrow_renderer_sample` to verify atlas-backed setup-pose region draw preparation from the checked-in fixtures
- To run the full autonomous loop with Codex against the expanded plan: `ralph build 100`
