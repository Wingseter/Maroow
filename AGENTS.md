# Marrow Agent Notes

## Project State

- The architecture source of truth is `docs/root1/discription.md`; active dependency-ordered milestones are tracked in `.agents/tasks/prd-marrow-runtime.json`.
- MAR-121 is a completed tracking tombstone whose runtime foundation is integrated into MAR-122. MAR-122 through MAR-128, MAR-154 through MAR-162, and the behavior-preserving Task #28 refactor checkpoint are complete. MAR-163 is the next product milestone and depends on MAR-162. MAR-192 through MAR-210 remain an open, parallel deferred qualification backlog and do not block product work.
- Work is organized as small functional milestone checkpoints with focused validation.
- `.agents/ralph/`, `.ralph/`, and `docs/root1/ralph-loop.md` are preserved historical artifacts and are not current execution authority.

## Working Rules

- Read `docs/root1/discription.md` before changing runtime or file-format decisions.
- Keep milestones small, vertical, and independently verifiable at focused checkpoints.
- Preserve the runtime-first plan unless the active story explicitly updates it.
- If a build or test workflow is introduced, document the exact commands here.
- The checked-in PRD already expands the renderer, runtime, and editor roadmap from `docs/root1/discription.md`. Prefer updating that PRD rather than inventing parallel plans.

## Documentation Entry Points

- Architecture source of truth: `docs/root1/discription.md`
- Runtime integration walkthrough: `docs/root1/quick-start.md`
- Runtime ownership and playback model: `docs/root1/concepts.md`
- File format reference: `docs/root1/format-spec.md`
- Fixture mapping and sample asset intent: `docs/root1/fixtures.md`
- Platform qualification evidence: `docs/root1/platform-validation.md`
- Vendored dependency provenance: `THIRD_PARTY.md`
- Archived Ralph loop/operator record: `docs/root1/ralph-loop.md`

## Current Validation

- Configure: `cmake -S . -B build`
- Build: `cmake --build build`
- Vendored dependency/hash/patch verification: `cmake --build build --target marrow_verify_third_party`
- SDL/Sokol window seam unit tests: `./build/marrow_windowing_tests`
- SDL pen/pressure unit tests: `./build/marrow_pen_input_tests`
- Cross-platform preference path and atomic-write tests: `./build/marrow_preference_tests`
- Agent loopback/partial-I/O/repeated-lifecycle transport tests: `./build/marrow_agent_socket_tests`
- Sokol ImGui setup/frame/shutdown lifecycle probe: `./build/marrow_sokol_imgui_runtime_probe`
- Typed transient entity selection model: `./build/marrow_selection_tests`
- Viewport interaction data-kernel tests: `./build/marrow_viewport_interaction_tests`
- Timeline data-model and authoring-boundary tests: `./build/marrow_timeline_model_tests`
- Focused CTest guardrail discovery: `ctest --test-dir build -N`
- Focused CTest guardrail: `ctest --test-dir build --output-on-failure`
- Runtime-labeled CTest guardrail: `ctest --test-dir build --output-on-failure -L runtime`
- Editor-labeled CTest guardrail: `ctest --test-dir build --output-on-failure -L editor`
- Display qualification configure: `cmake -S . -B build-display -DMARROW_ENABLE_DISPLAY_TESTS=ON`
- Display qualification build: `cmake --build build-display`
- Windowing display tests: `ctest --test-dir build-display --output-on-failure -L windowing`
- Renderer/display tests: `ctest --test-dir build-display --output-on-failure -L display`
- Renderer CPU/GPU link-boundary guard: `ctest --test-dir build --output-on-failure -R marrow.renderer_link_boundary`
- Release platform qualification configure: `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON`
- Release platform qualification build: `cmake --build build-platform-release`
- Release platform qualification suite: `ctest --test-dir build-platform-release --output-on-failure`
- Windows VS2022 x64 configure: `cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DMARROW_ENABLE_DISPLAY_TESTS=ON`
- Windows Debug build/test: `cmake --build build-msvc --config Debug` then `ctest --test-dir build-msvc -C Debug --output-on-failure`
- Windows Release build/test: `cmake --build build-msvc --config Release` then `ctest --test-dir build-msvc -C Release --output-on-failure`
- Windows portable folder/ZIP staging: `cmake --build build-msvc --config Release --target marrow_portable_stage`
- Constraint warning check: `cmake --build build --target marrow_constraint_warning_check`
- Documentation build (requires Doxygen on `PATH`): `cmake --build build --target marrow_docs`
- Release benchmark configure: `cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release`
- Release benchmark build: `cmake --build build-bench --target marrow_benchmark`
- Parameter/deformer benchmark: `./build-bench/marrow_benchmark --parameter-deformers --skeletons 200 --frames 240`
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
- Current validated 200-skeleton release metrics on this host: `frame_ms=4.45`, `score=100`, `animation_us=1.98`, `transform_us=0.07`, `skinning_us=1.17`, `constraint_us=15.12`, `render_us=0.00`, `max_skeletons_60fps=749.03`
- 200-skeleton before/after comparison (original profiling baseline from the MAR-099 story brief vs the current validated release run on this host):

| Metric | Original profiling | Current validated | Target | Status |
| --- | ---: | ---: | ---: | --- |
| Animation us/skeleton | 81.00 | 1.98 | <30.00 | PASS |
| Skinning us/skeleton | 79.00 | 1.17 | <5.00 | PASS |
| Constraint us/skeleton | 56.00 | 15.12 | <25.00 | PASS |
| Render us/skeleton | 12.00 | 0.00 | <12.00 | PASS |
| Transform us/skeleton | 4.00 | 0.07 | <4.00 | PASS |
| Total us/skeleton | ~232.00 | ~18.34 | <76.00 | PASS |

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
- Spine JSON import report CLI: `./build/spine_to_marrow --report /tmp/spine_import_report.json assets/fixtures/spine_import_sample.json /tmp/spine_import_sample.mskl && python3 -m json.tool /tmp/spine_import_report.json > /dev/null`
- Spine atlas import CLI: `./build/spine_to_marrow assets/fixtures/spine_import_sample.atlas /tmp/spine_import_sample.matl`
- Skeleton validator CI report: `./build/marrow_validator --skip-render --report /tmp/player_idle_validator.json assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl && python3 -m json.tool /tmp/player_idle_validator.json > /dev/null`
- Skeleton validator selected animation/skin sample: `./build/marrow_validator --skip-render --animation idle --skin default --time 0.2 --report /tmp/player_idle_idle_validator.json assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl && python3 -m json.tool /tmp/player_idle_idle_validator.json > /dev/null`
- Spine import-report validator integration: `./build/spine_to_marrow --report /tmp/spine_import_report.json assets/fixtures/spine_import_sample.json /tmp/spine_import_sample.mskl && ./build/spine_to_marrow assets/fixtures/spine_import_sample.atlas /tmp/spine_import_sample.matl && ./build/marrow_validator --skip-render --import-report /tmp/spine_import_report.json --report /tmp/spine_import_validator.json /tmp/spine_import_sample.mskl /tmp/spine_import_sample_hero_page.matl && python3 -m json.tool /tmp/spine_import_validator.json > /dev/null`
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
- Parameter project/runtime/preview/export validation: `./build/marrow_parameter_project_smoke assets/fixtures/parameter_face_basic.marrow`
- Parameter JSON vs binary comparison: `./build/marrow_inspect --compare /tmp/marrow_parameter_face_basic.mbin /tmp/marrow_parameter_face_basic.mskl`
- Editor runtime export validation for transform, deform, draw-order, event, and constraint edits: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl`
- Editor runtime asset bundle export validation with optional binary output: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl --export-binary /tmp/player_idle_project_export.mbin`
- Editor atlas packer validation from 24 individual sprite PNGs through runtime and renderer load: `./build/marrow_atlas_packer_smoke`
- Atlas-pack fixture project export validation: `./build/marrow_project_smoke assets/fixtures/atlas_pack_smoke/atlas_pack_project.marrow --export-runtime /tmp/atlas_pack_project_export.mskl`
- Project-export weighted mesh, FFD, and constraint round-trip validation: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl`
- End-to-end sample project export/load validation: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl --export-binary /tmp/player_idle_project_export.mbin`
- End-to-end exported project JSON vs binary comparison: `./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin /tmp/player_idle_project_export.mskl`
- End-to-end exported project render validation: `./build/marrow_renderer_sample /tmp/player_idle_project_export.mskl /tmp/player_idle.matl`
- Exported JSON vs binary project bundle comparison: `./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin /tmp/player_idle_project_export.mskl`
- AI Agent Control (MCP) launch:
  1. Start Maroow with agent port: `./build/marrow_editor_shell --agent-port 9876`
  2. Start MCP server: `source tools/mcp/venv/bin/activate && python3 tools/mcp/server.py`
  3. Test end-to-end: `source tools/mcp/venv/bin/activate && python3 tools/mcp/test_client.py`
- MCP schema syntax validation: `tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py`
- Agent registry validation (56 operations, including parameter and animation-duration authoring): `./build/marrow_agent_dispatch_smoke`
- Parameter Agent/MCP E2E: start `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --agent-port 9876`, then run `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only`
- Editor shell launch: `./build/marrow_editor_shell`
- macOS launch-focus regression check: `./build/marrow_editor_shell --verify-launch-focus`
- Editor shell smoke validation for viewport FBO/docking/bone picking, onion skinning, independent debug overlay toggles (bones, IK, path, physics, mesh wireframe, bounds), the runtime performance HUD overlay, timeline, clip-duration live editing/queue boundary/clamp/reject, draw-order, event, state-preview, deform, brush-based mesh weight painting, constraint authoring preview, and runtime asset hot-reload: `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- Parameter Modeling shell validation: `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2`
- Native macOS launch-focus note: sandboxed SDL/AppKit startup can stall after `com.apple.hiservices-xpcservice` LaunchServices/XPC errors; use an interactive macOS session to visually confirm that `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow` comes to the front and appears in Cmd+Tab.
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
- Parameter face fixture inspection: `python3 -m json.tool assets/fixtures/parameter_face_basic.mskl > /dev/null`
- Parameter project fixture inspection: `python3 -m json.tool assets/fixtures/parameter_face_basic.marrow > /dev/null`
- Parameter deformer fixture inspection: `python3 -m json.tool assets/fixtures/parameter_deformer_grid.mskl > /dev/null`
- Parameter expression/lip-sync fixture inspection: `python3 -m json.tool assets/fixtures/parameter_expression_lipsync.mskl > /dev/null`
- ArtPath fixture inspection: `python3 -m json.tool assets/fixtures/art_path_stroke.mskl > /dev/null`
- Parameter face renderer preparation: `./build/marrow_renderer_sample --skip-render assets/fixtures/parameter_face_basic.mskl assets/fixtures/parameter_face_basic.matl`
- Parameter deformer renderer preparation: `./build/marrow_renderer_sample --skip-render assets/fixtures/parameter_deformer_grid.mskl assets/fixtures/parameter_face_basic.matl`
- Atlas-free ArtPath renderer preparation: `./build/marrow_renderer_sample --no-atlas --skip-render assets/fixtures/art_path_stroke.mskl`
- Use `./build/marrow_renderer_sample` to verify atlas-backed setup-pose region draw preparation, clipping-mask propagation, sequence frame selection, GPU-skinned weighted-mesh draw preparation, animated slot presentation, slot blend modes, straight-alpha/PMA two-color tint propagation, and the single-color shader fast path from the checked-in fixtures

## MAR-192–210 Platform Program Local Implementation Checkpoint

Validated locally on 2026-08-09 without closing any platform story. The source
implements the SDL3/Sokol architecture and Windows compile-time/service/package
paths. A 2026-08-12 scope decision makes Ubuntu/Linux, Windows 10, and a separate-PC
portable run `NOT REQUIRED`; their code paths do not gain support claims. Windows 11
high-DPI/manual UI, physical Windows Ink, and fixed legacy/Sokol A/B evidence remain
required by MAR-210.

- `cmake --build build -j4` -> all default targets built.
- `cmake --build build --target marrow_verify_third_party` -> pinned SDL3,
  ImGui, Sokol, patched sokol_imgui, three sokol-shdc binaries, generated
  Metal/GL headers, and removed legacy backends verified.
- `ctest --test-dir build --output-on-failure` -> 17/17 noninteractive tests passed.
- `ctest --test-dir build-display --output-on-failure` -> 20/20 passed,
  including three actual SDL/Metal display tests.
- `ctest --test-dir build-platform-release --output-on-failure` -> Release
  display-enabled suite 20/20 passed.
- `marrow_window_host_smoke` -> 20 host lifecycles; logical `640x480`, drawable
  `1280x960`, scale `2x2`, BGRA8 + depth-stencil, 4x MSAA.
- `marrow_gpu_parity_smoke` -> top-left RGBA8 corner/center/bottom-right probes
  within `2/255`, 20 device lifecycles, and 100 image/view lifecycles.
- `marrow_editor_display_smoke` -> actual editor offscreen viewport, main
  sokol_imgui pass, commit, and present completed for 20 frames.
- `nm -gU build/marrow_editor_shell` -> zero `sapp_*`/`sglue_*` symbols;
  standalone `marrow_renderer_sample` retains the adapter.
- CMake link ownership -> `marrow_editor_shell` links `marrow_renderer_core`;
  only the standalone compatibility renderer links `marrow_renderer_sapp_host`.
- `otool -L build/marrow_editor_shell` -> no OpenGL framework or GLFW library.
- `./build/marrow_editor_shell --verify-launch-focus` -> SDL high-DPI window
  and both AppKit/process Regular activation policies verified.
- Current qualification authority and explicit NOT RUN rows:
  `docs/root1/platform-validation.md`.

## MAR-162 Signed Local Scale Gizmo Validation Results

Validated 2026-07-25. Animation mode now shows fixed 74px local X, local Y, and uniform
scale handles outside the 58px rotation ring for the runtime-active active Bone. Each gesture
freezes a positive scale-free local-axis basis and auto-keys absolute signed scale through the
existing effective-track materialization and transaction path.

| Slice | Verification | Result |
| --- | --- | --- |
| Handles and input | Local X/Y/uniform endpoints remain 74px from the pivot with a 6px hit radius independent of zoom; arbitration is active gesture/brush, translate Free/X/Y, rotation, scale, entity hit, then empty-space box; active non-Bone and weight-paint contexts hide the handles | PASS |
| Local-axis basis | Root and `OnlyTranslation` use skeleton scale; `Normal` children use evaluated parent-world 2x2 with local rotation/shear and without local scale; the basis is frozen at gesture start and covers non-uniform scale, reflection, and negative determinant | PASS |
| Supported inherit and signed mapping | `NoRotationOrReflection`, `NoScale`, and `NoScaleOrReflection` hide scale together with rotation and show a hint; X/Y ratio mapping crosses signs and preserves exact zero, zero-start axes recover at one scale unit per 74px, uniform preserves the starting signed X:Y ratio, and `(0,0)` hides uniform | PASS |
| Transaction and rollback | Playback pauses; imported effective scale keys and curves materialize once; mixed selection, active identity, hierarchy anchor, and timeline focus stay unchanged; click/no-movement creates no history, commit creates one undo entry, and cancel/non-finite failures restore project/runtime/preview content | PASS |
| Persistence and compatibility | `.marrow`, exported `.mskl`, and canonical MBIN v2 payloads preserve finite negative and exact-zero scale keys; public editor API, `SelectionSet`, `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1, and 56-operation Agent/MCP remain unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build` -> configured successfully
- `cmake --build build -j4` -> all targets built
- `./build/marrow_unit_tests` -> 31 named cases passed, including signed/exact-zero scale sampling
- `./build/marrow_selection_tests` -> 10/10 focused cases passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar162.mskl --export-binary /tmp/marrow_mar162.mbin` -> effective scale materialization, save/reload, and JSON/MBIN export passed
- `./build/marrow_inspect --compare /tmp/marrow_mar162.mbin /tmp/marrow_mar162.mskl` -> match; `rotation_error=0.00274662deg`, `position_error=0.000811016px`
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> fixed handles, basis/mapping, active-only transaction, rollback, and transience smoke passed
- `./build/marrow_agent_dispatch_smoke` -> all 56 registry operations passed without surface changes
- `./build/marrow_c_smoke` -> C ABI v1 smoke passed without API changes
- `ctest --test-dir build --output-on-failure -L editor` -> 7/7 passed
- `ctest --test-dir build --output-on-failure` -> 13/13 passed
- `git diff --check` -> passed

## MAR-161 Parent-Space Rotation Gizmo Validation Results

Validated 2026-07-21. Animation mode now shows one fixed 58px rotation ring for the
runtime-active active Bone. The gesture freezes its parent-space basis, preserves raw multi-turn
absolute rotation, and shares the existing effective-track materialization and transaction path.

| Slice | Verification | Result |
| --- | --- | --- |
| Ring and input | 58px radius, 6px hit band, 42px translate gizmo inside it, translate Free/X/Y precedence, then rotation, entity hit, and empty-space box; active non-Bone and weight-paint contexts hide the ring | PASS |
| Parent-space basis | Root and `OnlyTranslation` use skeleton scale; `Normal` children use the evaluated parent-world 2x2 frozen at gesture start; translation, rotation, shear, non-uniform scale, reflection, and negative determinant are covered | PASS |
| Supported inherit range | `NoRotationOrReflection`, `NoScale`, and `NoScaleOrReflection` hide the ring and show an unsupported-inherit hint; singular and NaN/Inf math cancel safely | PASS |
| Angular and transaction semantics | Per-sample `(-180, 180]` unwrap with +180 tie, positive and negative 450-degree accumulation, 2px pivot suspend/rebase, raw effective restart, playback pause, live preview, one undo, and no-movement no-op | PASS |
| Persistence and compatibility | `.marrow` and exported `.mskl` preserve raw multi-turn keys; unrepresentable continuous rotate channels fall back from optional AKEY to canonical MBIN payload while representable neighbors stay packed; public editor API, `SelectionSet`, `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1, and 56-operation Agent/MCP remain unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build` -> configured successfully
- `cmake --build build -j4` -> all targets built
- `./build/marrow_unit_tests` -> 31 named cases passed, including `Binary Multi-Turn Rotate Fallback`
- `./build/marrow_selection_tests` -> 10/10 focused cases passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar161.mskl --export-binary /tmp/marrow_mar161.mbin` -> raw multi-turn save/reload and JSON/MBIN export passed
- `./build/marrow_inspect --compare /tmp/marrow_mar161.mbin /tmp/marrow_mar161.mskl` -> match; `rotation_error=0.00274662deg`, `position_error=0.000811016px`
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> parent-space ring, unwrap, materialization, rollback, and transience smoke passed
- `./build/marrow_agent_dispatch_smoke` -> all 56 registry operations passed without surface changes
- `./build/marrow_c_smoke` -> C ABI v1 smoke passed without API changes
- `ctest --test-dir build --output-on-failure -L editor` -> 7/7 passed
- `ctest --test-dir build --output-on-failure` -> 13/13 passed
- `git diff --check` -> passed

## MAR-160 Viewport Multi-Selection Validation Results

Validated 2026-07-21. Viewport clicks now resolve typed entities by stable category, distance,
and authored/draw order, while empty-space drags select visible runtime-active Bone joints in
skeleton order. All editing consumers remain scoped to the active item; no group transform was
introduced.

| Slice | Verification | Result |
| --- | --- | --- |
| Point precedence | Visible constraint target, Bone joint, Bone body, Slot centroid, then topmost rendered Attachment triangle; category wins before screen distance and stable order | PASS |
| Point gestures | Plain click replaces and macOS Cmd/non-macOS Ctrl toggles the same exact typed identities used by hierarchy selection | PASS |
| Box gestures | Normalized forward/reverse rectangles, 4px threshold, inclusive runtime-active Bone centers, plain replace/clear, additive mixed-prefix retention and stable append, empty additive no-op | PASS |
| Geometry and source adoption | Region, GPU-skinned mesh, Slot, and visible constraint targets are pickable; hidden/inactive Bones are excluded; source adoption and orphaned viewport frames clear stale rectangles | PASS |
| Transience and consumers | Hierarchy anchor/timeline focus synchronize on selection changes, project/runtime/preview/history/revisions remain unchanged, and inspector/gizmo/timeline/constraint/weight tools edit only the active item | PASS |

Validated commands and outputs:

- `cmake --build build -j4` -> all targets built
- `./build/marrow_selection_tests` -> 10/10 focused cases passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> point, overlap, box, geometry, cleanup, and transience smoke passed
- `./build/marrow_agent_dispatch_smoke` -> all 56 registry operations passed without surface changes
- `ctest --test-dir build --output-on-failure -L editor` -> 7/7 passed
- `ctest --test-dir build --output-on-failure` -> 13/13 passed
- `git diff --check` -> passed

## MAR-159 Hierarchy Multi-Selection Validation Results

Validated 2026-07-21. Hierarchy clicks now apply platform-correct replace, toggle,
visible-range, and additive-range gestures to `SelectionSet` in the actual rendered row order.
The exact-identity anchor remains transient, and inspector, gizmo, timeline, constraint, and
weight-paint editing continue to use only the active item.

| Slice | Verification | Result |
| --- | --- | --- |
| Gesture semantics | Plain replace, macOS Cmd/non-macOS Ctrl toggle, forward/reverse Shift replacement, Cmd/Ctrl+Shift additive append, and deterministic invalid-anchor fallback | PASS |
| Visible order and anchor | Expanded and filtered Bone/Slot/Attachment order, collapsed/filtered row exclusion, toggled-off visible anchor retention, hidden-anchor clearing, and fully scoped attachment row identity | PASS |
| Source adoption | Reordered sources retain the exact anchor identity, deleted identities clear it, and malformed reload preserves selection, anchor, and the prior runtime bundle | PASS |
| Presentation and consumers | Common selected background, active-only primary rail/text, active-path ancestry, timeline-focus reset, selection-count status, and active-only inspector/gizmo/timeline/weight behavior | PASS |
| Transience and compatibility | Gestures leave project bytes, dirty/history, preview/runtime data, and revisions unchanged; `.marrow`, `.mskl` v1, `.mbin` v2, C ABI v1, and 56-operation Agent/MCP remain unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build` → configured successfully
- `cmake --build build -j4` → all targets built
- `./build/marrow_selection_tests` → 10/10 focused cases passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` → transient reload reconciliation passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` → MAR-159 hierarchy gesture, source-adoption, and transience smoke passed
- `./build/marrow_agent_dispatch_smoke` → all 56 registry operations passed without surface changes
- `ctest --test-dir build --output-on-failure -L editor` → 7/7 passed
- `ctest --test-dir build --output-on-failure` → 13/13 passed
- `git diff --check` → passed

## MAR-158 Selection Migration Validation Results

Validated 2026-07-18. `SelectionSet` is now the only entity-selection source used by
hierarchy, inspector, viewport, timeline, constraints, and weight-paint consumers. Successful
project/runtime source adoption re-resolves exact typed identities against the new runtime and
prunes only missing items; failed adoption preserves the selection and prior runtime bundle.

| Slice | Verification | Result |
| --- | --- | --- |
| Consumer resolution | Shell-private `ResolvedSelection`, active Bone-only transform editing, active Slot/Attachment timeline context, and active Constraint-only editing | PASS |
| Weight paint | Active Attachment/Slot then last selected Attachment/Slot target priority, active Bone influence, single target owning-bone fallback, no group edit | PASS |
| Source adoption | Name-based index re-resolution, fully scoped Attachment and kind-scoped Constraint pruning, stable survivor order/active fallback, malformed-reload atomicity | PASS |
| Selection primitives | Constraint remap/delete/collision and constraint-only prune preserve unrelated types, stable order, and active invariants | PASS |
| Transience and compatibility | Reconciliation adds no project bytes, dirty/history, or revision effects; `.marrow`, `.mskl` v1, `.mbin` v2, C ABI v1, and 56-operation Agent/MCP remain unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build` → configured successfully
- `cmake --build build -j4` → all targets built
- `./build/marrow_selection_tests` → 10/10 focused cases passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` → reload reconciliation and transient-state guardrails passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` → mixed consumer and atomic source-replacement shell smoke passed
- `./build/marrow_agent_dispatch_smoke` → all 56 registry operations passed without surface changes
- `ctest --test-dir build --output-on-failure -L editor` → 7/7 passed
- `ctest --test-dir build --output-on-failure` → 13/13 passed
- `git diff --check` → passed

## MAR-157 Typed SelectionSet Validation Results

Validated 2026-07-18. A public UI-free `SelectionSet` now owns exact name-based
Bone, Slot, Attachment, and Constraint identities with stable insertion order and one active
item. `ShellState` owns the only entity-selection set; persistence, preview composition,
history, user preferences, runtime formats, the C ABI, and Agent/MCP remain unchanged.

| Slice | Verification | Result |
| --- | --- | --- |
| Typed identity | Bone/Slot type separation, slot+skin+attachment scope, constraint kind+name scope, case-sensitive equality | PASS |
| Deterministic set | Replace, toggle, ordered range add, duplicate suppression, active fallback, clear, prune, remap/delete, collision tracking, invalid-range atomicity | PASS |
| Shell compatibility | Active Bone index; active Slot/Attachment slot and owning-bone context; Slot preview attachment; active Constraint kind/name | PASS |
| Transience | Project bytes, preview/runtime data, dirty state, undo/redo counts, and project/runtime/preview revisions remain unchanged | PASS |
| Compatibility | `.marrow`, `.mskl` v1, `.mbin` v2, C ABI v1, and the 56-operation Agent/MCP surface are unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build` → configured successfully
- `cmake --build build -j4` → all targets built
- `./build/marrow_selection_tests` → 7/7 focused cases passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` → active compatibility and transient-state shell smoke passed
- `ctest --test-dir build --output-on-failure -L editor` → 7/7 passed
- `ctest --test-dir build --output-on-failure` → 13/13 passed
- `git diff --check` → passed

## MAR-156 Versioned User Preference Store Validation Results

Validated 2026-07-18. A UI-free `PreferenceStore` now owns versioned user-local
`editor-settings.json` without joining project/session state, runtime formats, the C ABI, or
Agent/MCP operations.

| Slice | Verification | Result |
| --- | --- | --- |
| v1 contract | Six curve tokens, raw ordered recent paths, field-local defaults, invalid-entry skipping, malformed recovery, future-version protection | PASS |
| Additive compatibility | Supported-v1 unknown scalar/object/array and round-trip-sensitive numeric values survive known-field overlay | PASS |
| Paths | `MARROW_CONFIG_HOME` priority/restoration plus pure macOS, Linux XDG, Linux HOME fallback, empty/relative/missing environment cases | PASS |
| Atomic save | Unique same-directory temp, checked write/flush/close, POSIX rename, injected rename failure preserving exact prior bytes and cleaning the temp | PASS |
| Project isolation | Open dirty `EditorSession` with both undo and redo retains serialized project, history, dirty state, and project/runtime/preview revisions | PASS |
| Compatibility | `.marrow`, `.mskl` v1, `.mbin` v2, C ABI v1, and the 56-operation Agent/MCP surface are unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build` → configured successfully
- `cmake --build build` → all targets built
- `./build/marrow_preference_tests` → 8/8 focused cases passed
- `ctest --test-dir build --output-on-failure -L editor` → 6/6 passed
- `ctest --test-dir build --output-on-failure` → 12/12 passed
- `git diff --check` → passed

## MAR-155 Editor Duration Authoring Validation Results

Validated 2026-07-17. Ordered `.marrow.animation_edits` `set_duration` operations, editor transactions, key-boundary growth, and Agent/MCP control now complete the duration-authoring slice while keeping `.mskl` v1, `.mbin` v2, and C ABI v1 unchanged.

| Slice | Verification | Result |
| --- | --- | --- |
| Project contract | Ordered `set_duration` load/save/materialization, opaque unknown operation and known-edit additive-field preservation, old-project omission fallback | PASS |
| Session and shell | Live preview/dirty state, one-item undo/redo, queue-boundary rebuild, tail playhead clamp, Escape/invalid rollback | PASS |
| Timeline boundary | Key create/right-retime auto-grow in the same transaction; left-retime/delete never auto-shrink | PASS |
| Atomic rejection | Below-last-key manual shrink preserves project, runtime, preview, selection, history, dirty state, and revisions | PASS |
| Agent/MCP | 56-operation C++/Python parity, duration metadata, dry-run/live/reject/undo/redo, native socket E2E | PASS |
| Export compatibility | Save/reload and JSON/MBIN explicit presence/value plus quantized animation comparison | PASS |

Validated commands and outputs:

- `cmake -S . -B build` → configured successfully
- `cmake --build build -j4` → all targets built
- `./build/marrow_unit_tests` → 30 named cases passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar155_acceptance.mskl --export-binary /tmp/marrow_mar155_acceptance.mbin` → duration authoring/rollback/auto-grow/export E2E passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` → live duration/queue/clamp/reject shell smoke passed
- `./build/marrow_agent_dispatch_smoke` → all 56 registry operations passed
- `tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py` → Python schemas compiled
- `MARROW_AGENT_PORT=9877 tools/mcp/venv/bin/python tools/mcp/test_client.py` against `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9877` → socket E2E passed
- `./build/marrow_inspect --compare /tmp/marrow_mar155_duration.mbin /tmp/marrow_mar155_duration.mskl` → match; `rotation_error=0.00274662deg`, `position_error=0.000811016px`
- `./build/marrow_c_smoke` → C ABI smoke passed without API changes
- `ctest --test-dir build --output-on-failure -L runtime` → 4/4 passed
- `ctest --test-dir build --output-on-failure -L editor` → 5/5 passed
- `ctest --test-dir build --output-on-failure` → 11/11 passed
- `git diff --check` → passed

## MAR-154 Runtime Explicit Duration Validation Results

Validated 2026-07-17. Optional runtime clip duration now preserves authored presence while keeping `.mskl` v1, `.mbin` v2, and C ABI v1 unchanged.

| Slice | Verification | Result |
| --- | --- | --- |
| Runtime contract | Explicit/inferred/effective separation, empty clips, exact validation, copy/assignment, and old-asset fallback | PASS |
| Playback | Tail hold, non-loop/loop completion, queue promotion, reverse sampling, snapshot restore, and synthetic empty-animation behavior | PASS |
| Binary | Generic payload presence, effective-duration AKEY quantization, regenerated fixture, and JSON/MBIN comparison | PASS |
| Fixture compatibility | `aim` explicit `0.5`; `idle`/`attack` inferred `1.0`/`0.4`, validated from JSON and MBIN | PASS |

Validated commands and outputs:

- `cmake -S . -B build` → configured successfully
- `cmake --build build -j4` → all targets built
- `./build/marrow_unit_tests` → 30 named cases passed
- `./build/marrow_inspect --export-binary assets/fixtures/player_idle.mbin assets/fixtures/player_idle.mskl` → regenerated v2 fixture
- `python3 -m json.tool assets/fixtures/player_idle.mskl > /dev/null` → valid JSON
- `./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl` → explicit/fallback JSON fixture passed
- `./build/marrow_fixture_smoke assets/fixtures/player_idle.mbin assets/fixtures/player_idle.matl` → explicit/fallback MBIN fixture passed
- `./build/marrow_inspect --compare assets/fixtures/player_idle.mbin assets/fixtures/player_idle.mskl` → match; `rotation_error=0.00274662deg`, `position_error=0.000811016px`
- `./build/marrow_c_smoke` → C ABI smoke passed without API changes
- `ctest --test-dir build --output-on-failure -L runtime` → 4/4 passed
- `ctest --test-dir build --output-on-failure` → 11/11 passed
- `git diff --check` → passed

## MAR-122–128 Parameter Modeling Validation Results

Validated 2026-07-16. MAR-121 remains a tracking tombstone integrated into MAR-122; the complete MAR-122–128 runtime, renderer, project/editor, and Agent/MCP parameter-modeling checkpoint now passes.

| Slice | Verification | Result |
| --- | --- | --- |
| Runtime parameters and shapes | Finite/raw-direct/default/discrete/clamp/revision rules, post-composition normalization, 1D endpoint/linear shapes, linked/weighted mesh and animation-FFD separation | PASS |
| Deformers and caching | Bilinear warp, rotation pivot/influence, one-level chains, cycle/depth/ambiguity rejection, dependency bitsets, affected-slot cache updates | PASS |
| ArtPath renderer | Deterministic cap/join tessellation and scale-aware bounds, root-overlay ordering, solid-white triangle path, atlas-free preparation and cached missing-atlas guard | PASS |
| Expression and lip sync | Priority/activation order, additive/override, fade/restore/hold, amplitude/phoneme, attack/release/smoothing | PASS |
| Project and editor | Lossless optional parameter model, atomic runtime rebuild/rollback, preview preserve/prune/default, Parameter mode, CRUD, capture/replace and lattice/pivot gestures | PASS |
| Agent/MCP | Exact 55-operation C++/Python parity, dry-run invariants, mutation/undo/rebuild, keyform collision policy and parameter socket E2E | PASS |
| Compatibility and performance | `.mskl` v1, `.mbin` v2 and C ABI v1 retained; old assets use an empty model; 200-skeleton acceptance and separate parameter/deformer metrics pass | PASS |

Validated commands and outputs:

- `cmake -S . -B build` → configured successfully
- `cmake --build build -j4` → all targets built
- `ctest --test-dir build --output-on-failure` → 11/11 passed
- `ctest --test-dir build --output-on-failure -L runtime` → 4/4 passed
- `ctest --test-dir build --output-on-failure -L editor` → 5/5 passed
- `./build/marrow_unit_tests` → 29 named cases passed
- `./build/marrow_parameter_project_smoke` → preview/undo/rollback/save/reload and complete JSON/binary parameter model passed
- `./build/marrow_inspect --compare /tmp/marrow_parameter_face_basic.mbin /tmp/marrow_parameter_face_basic.mskl` → all seven optional parameter roots match
- `./build/marrow_renderer_sample --no-atlas --skip-render assets/fixtures/art_path_stroke.mskl` → two stroke commands and exact tessellation/bounds guardrails passed
- `./build/marrow_agent_dispatch_smoke` → all 55 registry operations and parameter dry/live/undo paths passed
- `tools/mcp/venv/bin/python tools/mcp/test_client.py` and `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only` → both socket E2E paths passed
- `./build-bench/marrow_benchmark --skeletons 200` → `frame_ms=4.45`, `score=100`, `max_skeletons_60fps=749.03`
- `./build-bench/marrow_benchmark --parameter-deformers --skeletons 200 --frames 240` → `parameter_us=0.07`, `deformer_us=0.51`
- `git diff --check` → passed

## MAR-141–153 Editing P0 Validation Results

Validated 2026-07-12. This table records the earlier imported-rig authoring P0 checkpoint; the later MAR-122–128 parameter-modeling validation is recorded above.

| Slice | Verification | Result |
| --- | --- | --- |
| Honest setup + auto-key | Setup transforms/colors are read-only; inspector R/T/S/shear materializes the effective base track, converts non-zero setup rotation correctly, previews live, and rolls back exactly | PASS |
| Stable viewport + move gizmo | Camera inverse/cursor zoom/Fit plus root, transformed child, IK target, singular-parent cancel, one-drag undo/redo | PASS |
| Slot lanes + dopesheet | Color/attachment add/edit/remove, stable same-time identities, exact-playhead remove, box/toggle selection, multi-key retime, typed clipboard and compatible-lane paste | PASS |
| Animation catalog | Ordered `.marrow.animation_edits`, create/duplicate/rename/delete UI and agent operations, unknown-family preservation, atomic queue/preview cascade | PASS |
| Agent/MCP parity | At this historical P0 checkpoint, C++ and Python exposed 49 matching operations; MAR-128 raised that checkpoint total to 55 and MAR-155 raises the current total to 56 | PASS |
| End to end | Base-only auto-key → retime → undo/redo → save/reload → JSON/quantized binary export and comparison | PASS |

Validated commands and outputs:

- `cmake --build build -j4` → all targets built
- `ctest --test-dir build --output-on-failure` → 7/7 passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` → P0 E2E passed; binary errors `rotation=0.00274662deg`, `position=0.000811016px`
- Historical `./build/marrow_agent_dispatch_smoke` result → 49 operations passed; MAR-128 later reached 55/55 and MAR-155 reaches the current 56/56
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` + `tools/mcp/venv/bin/python tools/mcp/test_client.py` → MCP/socket E2E passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 5` → 5 frames rendered with viewport/timeline/catalog P0 smokes
- `./build/marrow_renderer_sample --skip-render assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl` → renderer preparation guardrail passed
- `git diff --check` → passed

## Runtime and Renderer Unit Cases

`./build/marrow_unit_tests` currently reports these 31 named cases (validated 2026-07-21):

- `Interpolation Edge Cases`
- `Constraint Fast Math Approximations`
- `Animation Float Storage And Constant Pruning`
- `Animation Timeline Index And Sampling Cursor`
- `Animation Explicit Duration`
- `Matrix Composition`
- `Topological Bone Reorder`
- `SkeletonData Children Map And Tip Cache`
- `SIMD World Transform Propagation`
- `Constraint Hot Path Allocations`
- `Constraint Dirty Skip Preserves Output`
- `Constraint Dirty Skip Re-evaluates Only Affected Constraints`
- `IK Solving`
- `Physics Stepping`
- `SkeletonBounds Queries`
- `Custom Allocator Lifecycle`
- `AnimationState Snapshot Restore`
- `Animation Layers`
- `Concave Stencil Clipping`
- `Nested Stencil Restoration`
- `Dynamic Mesh Cache Static Payload And Deform Updates`
- `Dynamic Mesh Clipping Uses Stencil Only`
- `PreparedScene Cache Dirty Updates`
- `Parameter Definitions And Composition`
- `Parameter State Transition Semantics`
- `Parameter Shape Final Offsets`
- `Parameter Deformer And ArtPath Evaluation`
- `Parameter Loader Validation`
- `Binary Key Quantization And Reduction`
- `Binary Multi-Turn Rotate Fallback`
- `Runtime Profiler Frame`

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
- `./build/marrow_unit_tests` → 31 named cases passed (current executable; revalidated 2026-07-21)
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 5` → 5 frames rendered
- `./build/marrow_renderer_sample --auto-close 2` → all blend/clip/mesh/batch validations passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_e2e_export.mskl --export-binary /tmp/marrow_e2e_export.mbin` → export + undo/redo validated
- `./build/marrow_fixture_smoke /tmp/marrow_e2e_export.mskl /tmp/player_idle.matl` → generic runtime smoke passed
- `./build/marrow_inspect --compare /tmp/marrow_e2e_export.mbin /tmp/marrow_e2e_export.mskl` → comparison matches
