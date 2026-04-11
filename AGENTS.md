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

## Documentation Entry Points

- Architecture source of truth: `docs/discription.md`
- Runtime integration walkthrough: `docs/quick-start.md`
- Runtime ownership and playback model: `docs/concepts.md`
- File format reference: `docs/format-spec.md`
- Fixture mapping and sample asset intent: `docs/fixtures.md`
- Ralph loop/operator notes: `docs/ralph-loop.md`

## Current Validation

- Configure: `cmake -S . -B build`
- Build: `cmake --build build`
- Constraint warning check: `cmake --build build --target marrow_constraint_warning_check`
- Documentation build (requires Doxygen on `PATH`): `cmake --build build --target marrow_docs`
- Release benchmark configure: `cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release`
- Release benchmark build: `cmake --build build-bench --target marrow_benchmark`
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
- Current validated 200-skeleton release metrics on this host: `frame_ms=3.99`, `score=100`, `animation_us=1.60`, `transform_us=0.01`, `skinning_us=0.13`, `constraint_us=14.11`, `render_us=0.00`, `max_skeletons_60fps=835.31`
- 200-skeleton before/after comparison (original profiling baseline from the MAR-099 story brief vs the current validated release run on this host):

| Metric | Original profiling | Current validated | Target | Status |
| --- | ---: | ---: | ---: | --- |
| Animation us/skeleton | 81.00 | 1.54 | <30.00 | PASS |
| Skinning us/skeleton | 79.00 | 0.05 | <5.00 | PASS |
| Constraint us/skeleton | 56.00 | 14.05 | <25.00 | PASS |
| Render us/skeleton | 12.00 | 0.00 | <12.00 | PASS |
| Transform us/skeleton | 4.00 | 0.00 | <4.00 | PASS |
| Total us/skeleton | ~232.00 | ~15.64 | <76.00 | PASS |

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
- Spine atlas import CLI: `./build/spine_to_marrow assets/fixtures/spine_import_sample.atlas /tmp/spine_import_sample.matl`
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
- Editor runtime export validation for transform, deform, draw-order, event, and constraint edits: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl`
- Editor runtime asset bundle export validation with optional binary output: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl --export-binary /tmp/player_idle_project_export.mbin`
- Editor atlas packer validation from 24 individual sprite PNGs through runtime and renderer load: `./build/marrow_atlas_packer_smoke`
- Atlas-pack fixture project export validation: `./build/marrow_project_smoke assets/fixtures/atlas_pack_smoke/atlas_pack_project.marrow --export-runtime /tmp/atlas_pack_project_export.mskl`
- Project-export weighted mesh, FFD, and constraint round-trip validation: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl`
- End-to-end sample project export/load validation: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl --export-binary /tmp/player_idle_project_export.mbin`
- End-to-end exported project JSON vs binary comparison: `./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin /tmp/player_idle_project_export.mskl`
- End-to-end exported project render validation: `./build/marrow_renderer_sample /tmp/player_idle_project_export.mskl /tmp/player_idle.matl`
- Exported JSON vs binary project bundle comparison: `./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin /tmp/player_idle_project_export.mskl`
- Editor shell launch: `./build/marrow_editor_shell`
- macOS launch-focus regression check: `./build/marrow_editor_shell --verify-launch-focus`
- Editor shell smoke validation for viewport FBO/docking/bone picking, onion skinning, independent debug overlay toggles (bones, IK, path, physics, mesh wireframe, bounds), the runtime performance HUD overlay, timeline, draw-order, event, state-preview, deform, brush-based mesh weight painting, constraint authoring preview, and runtime asset hot-reload: `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- Native macOS launch-focus note: sandboxed GLFW startup can stall after `com.apple.hiservices-xpcservice` LaunchServices/XPC errors; use an interactive macOS session to visually confirm that `./build/marrow_editor_shell assets/fixtures/player_idle.marrow` comes to the front and appears in Cmd+Tab.
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
- Use `./build/marrow_renderer_sample` to verify atlas-backed setup-pose region draw preparation, clipping-mask propagation, sequence frame selection, GPU-skinned weighted-mesh draw preparation, animated slot presentation, slot blend modes, straight-alpha/PMA two-color tint propagation, and the single-color shader fast path from the checked-in fixtures
- To run the full autonomous loop with Codex against the expanded plan: `ralph build 100`

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
- `./build/marrow_unit_tests` → 27 tests passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 5` → 5 frames rendered
- `./build/marrow_renderer_sample --auto-close 2` → all blend/clip/mesh/batch validations passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_e2e_export.mskl --export-binary /tmp/marrow_e2e_export.mbin` → export + undo/redo validated
- `./build/marrow_fixture_smoke /tmp/marrow_e2e_export.mskl /tmp/player_idle.matl` → generic runtime smoke passed
- `./build/marrow_inspect --compare /tmp/marrow_e2e_export.mbin /tmp/marrow_e2e_export.mskl` → comparison matches
