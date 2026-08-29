# Marrow Agent Notes

## Project State

- The architecture source of truth is `docs/root1/discription.md`; active dependency-ordered milestones are tracked in `.agents/tasks/prd-marrow-runtime.json`.
- MAR-121 is a completed tracking tombstone whose runtime foundation is integrated into MAR-122. MAR-122 through MAR-128, MAR-154 through MAR-172, and the behavior-preserving Task #28 refactor checkpoint are complete. MAR-173 is the next product milestone and depends on MAR-172. MAR-192 through MAR-210 remain an open, parallel deferred qualification backlog and do not block product work.
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
- Cross-platform preference path, atomic-write, fixed curve-preset constant, preset-token, and shell preference-session tests: `./build/marrow_preference_tests`
- Agent loopback/partial-I/O/repeated-lifecycle transport tests: `./build/marrow_agent_socket_tests`
- Sokol ImGui setup/frame/shutdown lifecycle probe: `./build/marrow_sokol_imgui_runtime_probe`
- Typed transient entity selection model: `./build/marrow_selection_tests`
- Viewport interaction data-kernel tests: `./build/marrow_viewport_interaction_tests`
- Timeline data-model and authoring-boundary tests: `./build/marrow_timeline_model_tests`
- Timeline scalar-graph projection/geometry/view/drag-math tests: `./build/marrow_timeline_graph_model_tests`
- Editor project authoring smoke including `offset_keyframe_scalars`: `./build/marrow_project_smoke assets/fixtures/player_idle.marrow`
- Headless editor shell smoke including the graph drag scenario and actual-frame drags: `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
- Focused CTest guardrail discovery: `ctest --test-dir build -N`
- Focused CTest guardrail: `ctest --test-dir build --output-on-failure`
- Runtime-labeled CTest guardrail: `ctest --test-dir build --output-on-failure -L runtime`
- Editor-labeled CTest guardrail: `ctest --test-dir build --output-on-failure -L editor`
- Debug display qualification configure: `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON`
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
- Agent registry validation (59 operations, including parameter, animation-duration, timeline-interpolation, timeline-curve-mode, and timeline-loop-boundary authoring): `./build/marrow_agent_dispatch_smoke`
- Parameter Agent/MCP E2E: start `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --agent-port 9876`, then run `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only`
- Editor shell launch: `./build/marrow_editor_shell`
- macOS launch-focus regression check: `./build/marrow_editor_shell --verify-launch-focus`
- Editor shell smoke validation for viewport FBO/docking/bone picking, onion skinning, independent debug overlay toggles (bones, IK, path, physics, mesh wireframe, bounds), the runtime performance HUD overlay, timeline, clip-duration live editing/queue boundary/clamp/reject, draw-order, event, state-preview, attachment-local multi-vertex FFD auto-key, shared world-grid/local-angle/absolute-scale transform snapping, FFD world-grid/magnetic-vertex snapping, live Alt/Cmd/Ctrl modifiers, deform, brush-based mesh weight painting, constraint authoring preview, and runtime asset hot-reload: `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
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

## MAR-172 Loop Boundary Key Synchronization Validation Results

Validated 2026-08-30. A Transform, Slot Color, or Deform **lane** can now record
the intent "my last key is the loop boundary", as one optional, additive,
`.marrow`-only boolean projected into a top-level `loop_sync` tree that mirrors
`timeline_edits`. The flag is absent from every existing project, so every
existing project serializes byte-identically and behaves byte-identically — the
sync returns before it reaches MAR-171's resolver when no lane is opted in, and
that is asserted rather than argued. An opted-in lane always carries exactly one
managed key at `float32(explicit duration)` whose value and easing record —
`interpolation` plus MAR-171's `curve_mode`/`curve_driver` — are a bit-exact copy
of that lane's key at time zero, so a looping clip wraps with no pop. The
contract is re-established inside the caller's already-open transaction at the
one seam that is provably after every duration change: immediately after
`auto_extend_explicit_animation_durations()` in `EditorSession::refresh_runtime()`
and `EditorSession::commit()`. Managed identity is **derived, never stored** —
the managed key *is* the lane's last key while the lane is opted in — so no
marker can ride a copy/paste into a lane where it would be a lie, and adoption
is the absence of code. `set_animation_duration()`'s inferred floor and
`auto_extend_explicit_animation_durations()`'s overlay scan both exclude managed
boundary keys, without which an opted-in clip could never be shortened and every
shrink would be undone one line later; both exclusions are bit-exact no-ops for
every animation with no opted-in lane. Draw Order, Event, and Slot Attachment
lanes gain **no member**, so their exclusion is compile-enforced: an Event key at
the boundary would fire twice per loop. The Agent/MCP surface grew by exactly one
operation, `timeline.set_loop_sync`, taking the registry to exactly **59**.
`.mskl` v1, `.mbin` v2, C ABI v1, and `editor-settings.json` v1 are unchanged,
and the flag never enters a runtime file — but the managed boundary key does,
as an ordinary keyframe, which is the entire point. MAR-172 adds **no ImGui
code**: the story's criterion 5 says "the UI-free loop-sync operation".

| Slice | Verification | Result |
| --- | --- | --- |
| Additive, default-absent storage | `serialize_project()` of the untouched fixture contains no `loop_sync` and survives save/reload byte-identically; one opted-in lane serializes exactly `{"animations":{"idle":{"bones":{"spine":{"rotate":true}}}}}` with no `false` leaf and no opted-out lane anywhere in the block; the flag survives save and reload on exactly that lane and on no other; `false` loads as opted out and re-serializes as absence; an unknown top-level member round-trips beside the block through `preserved_root` | PASS |
| Load and save validation | Nine hand-built documents rejected with the exact JSON path and message: a non-object `loop_sync`, a missing `animations`, a non-object `animations`, a non-object animation entry, a string leaf, the unknown channel `spinx`, `slots.body.attachment`, an orphan lane with no `timeline_edits` entry, and a `true` leaf on a lane whose first key is at 0.25. `validate_project_for_save()` re-validates the two structural rules a skeleton-free validator can see and refuses both in memory | PASS |
| Derived identity | The contract owns a lane's last key only when that key satisfies one half of the contract: it already sits at the boundary, or it is still the bit-exact mirror of key 0 that a previous synchronization wrote. A last key satisfying neither is authored data no synchronization produced, so the boundary is **created beside it** rather than promoting it — asserted by removing a boundary key behind the guards and checking that the authored key at 1.0 keeps both its time and its value of −2 while a fourth key appears at 1.5 mirroring key 0 | PASS |
| The boundary contract | Create appends a fourth key at exactly `float32(1.5)` whose `angle` and all four cubic control points compare `==` as `AnimationScalar`, not within an epsilon; adopt overwrites an existing key at the boundary and creates none; move relocates only the boundary key after a duration change; rewrite follows a first-key value change and touches nothing else; a single-key lane becomes a two-key constant lane; two consecutive syncs report `synchronized_lane_count == 0`, `created_key_count == 0`, and a byte-identical project, which is the two-phase termination argument asserted | PASS |
| Fail-closed rejections | No explicit duration, no key at time zero, a duration below the one-millisecond spacing, a key past the boundary, a key crowding the boundary, duplicate keys at the boundary, a duplicate lane selector, an unresolvable lane selector, and an empty lane list each reject atomically with the project byte-identical. **Disabling evaluates no prerequisite and always succeeds**, clears the flag, reports `Released`, and leaves every keyframe in place — which is what makes the atomic rejection humane | PASS |
| Duration, both directions | `set_animation_duration("idle", 1.2)` now **succeeds** on an opted-in clip where it previously failed with `Animation duration cannot be shorter than the last authored key (1.500000 seconds).`; the sync then moves the boundary to `float32(1.2)`; `auto_extend_explicit_animation_durations()` reports `changed == false` and does not undo the shrink; a shrink onto the 1.0005 spacing floor is accepted by the duration primitive and rejected by the sync; a shrink to 0.9 keeps MAR-155's own message verbatim; with no opted-in lane the whole accept/reject table and its exact message strings are unchanged | PASS |
| Managed identity, GUI | Retime pins both ends of an opted-in lane, so a selection containing either collapses to `changed == false` and `applied_delta == 0` while a middle-key selection still moves by the full delta and a lane that is not opted in retimes exactly as before; the preset and curve-mode collectors and the graph value gesture skip a managed boundary key and report `is a managed loop boundary`; removing only the boundary key changes nothing and sets a status message; removing the time-zero key fails the transaction with `serialize_project()` byte-identical; pasting past the boundary fails the transaction byte-identically; a clipboard fragment never carries the flag | PASS |
| Managed identity, Agent | `timeline.set_interpolation`, `timeline.set_curve_mode`, `set_transform`, `set_slot_color_keyframe`, `set_deform_keyframe`, `remove_transform_keyframe`, `remove_slot_color_keyframe`, and `remove_deform_keyframe` each reject a managed boundary key atomically with `That key is a managed loop boundary; edit the key at time 0 of the same timeline instead, or disable loop synchronization on that timeline.`, across all three families. Each rejected removal is followed by a read-back proving the lane's **authored** keys at 1.0 and 0.5 still resolve, and every guarded operation still succeeds on a non-boundary key of the same opted-in lane, so the guard is boundary-specific rather than a lock on the lane. The GUI predicate now delegates to the same project-domain derivation, so the two surfaces cannot disagree | PASS |
| The session seam | Adding a key at 2.0 s on the **not** opted-in `root`/`translate` lane grows `idle`'s explicit duration to 2.0 through the session's own auto-extend **and** moves the opted-in lane's boundary key to 2.0 in the **same** history entry; one undo restores the duration, the added key, and the boundary key together. Before the seam existed this reported `The managed boundary key is stale at 1.5 while the duration is 2`, which is the exact staleness a controller-level wiring cannot close | PASS |
| Criterion 3, five ways | A MAR-168 value drag on key 0, a MAR-170 preset on key 0, a MAR-169 handle drag on key 0, a dopesheet retime of a middle key, and an `animation.set_duration`-equivalent duration edit each stay exactly one history entry with the boundary key re-mirrored inside it; cancelling a value drag on key 0 restores `serialize_project()`, `undo_count()`, `redo_count()`, `project_revision()`, `dirty()`, and the rebuilt dopesheet `key_times`; undo and redo of an enable restore the flag and the boundary key together | PASS |
| The MAR-171 seam | MAR-171's `resolved_key_count == 0` duration assertion is **scoped, not replaced** — its failure message now reads "with no lane opted in" and nothing else about it changed — and is joined by an opted-in case where the boundary key gives key 2 a real outgoing segment and a 1.5 → 2.0 duration change reports `resolved_key_count >= 1`, with the boundary's easing equal to key 0's **post**-resolve value, which is §9.2's phase ordering asserted. The demotion guard asserts both key 0 and the boundary key are still `Auto` after a sync: if phase 2 ever called `set_keyframe_interpolation()` both would read `Manual` | PASS |
| Export, on the MUTATED project | `export_runtime_assets()` runs on the project the validator just mutated. The exported `.mskl` carries `idle`'s explicit duration of 1.5, **four** spine rotate keyframes up from three with keyframe 3 at `time == 1.5f` and `angle` plus all four cubic control points bit-equal to keyframe 0's, and four `body` colour keyframes whose last colour is bit-equal to its first; the exported text contains no `loop_sync`; the `.mbin` matches the `.mskl`. Both files are strictly larger than the baseline measured in the same run — **JSON 15077 vs 14336, MBIN 4125 vs 3984** — and unlike MAR-171 the binary growth is a real signal, because two whole keyframe records were added | PASS |
| Agent and MCP parity at 59 | `timeline.set_loop_sync` sits immediately after `timeline.set_curve_mode` as (`edit`, mutating, not review, dry-run supported); its lane entries carry **no `time`**, the surface's one structural difference from every other `timeline.*` operation; the dry run reports each lane's `previous_enabled`, `previous_boundary` (`null` when no key sat at the boundary time), `boundary_action`, and resulting `boundary` without touching the session; a live call reports `changed_lane_count` and `created_key_count` in one history entry; a second identical call returns `no_change`; the runtime-only `aim`/`arm_l` lane reports `adopted`; `set_transform` on the time-zero key moves the boundary in the same entry; twelve rejection cases each leave the project provably unchanged; disabling a lane in a rejected state succeeds | PASS |
| Compatibility | `.mskl` v1, `.mbin` v2, and C ABI v1 unchanged with a zero-byte diff on `src/runtime/**`, `include/marrow/runtime/**`, `include/marrow/c_api/**`, and `src/c_api/**`; `editor-settings.json` v1 unchanged with a zero-byte diff on `src/editor/preferences.cpp` and `include/marrow/editor/preferences.hpp`; a zero-byte diff on every ImGui translation unit plus `shell_state.hpp` and `shell_core.cpp`, so MAR-172 is UI-free as the story requires; `git diff -- src/editor/session.cpp | rg 'rebuild_runtime'` and `git diff -- src/editor/project.cpp | rg 'build_runtime'` are both empty; `retime_keyframes()`'s signature is unchanged for MAR-173; the three discrete lane structs gain no member | PASS |

Command output recorded during validation:

- `cmake -S . -B build && cmake --build build -j10` -> configured and built with zero new warnings; `cmake --build build --target marrow_verify_third_party` -> `Vendored SDL3, zlib, Dear ImGui, Sokol, sokol_imgui, and sokol-shdc hashes verified`
- `./build/marrow_timeline_model_tests` -> `Timeline model: 16 cases passed`, up from 10, with six new MAR-172 cases: default-off-does-not-resolve and idempotence, create/adopt/move/rewrite plus the single-key lane, never-promotes-an-authored-key, every §6.5 rejection and disable-never-validates, retime pinning, and the excluding floor's bit-exact fast path over every fixture animation
- `./build/marrow_timeline_graph_model_tests` -> `Timeline graph model: 20 cases passed`; `./build/marrow_preference_tests` -> `PreferenceStore: 11 cases passed` (unchanged; MAR-172 touches no preference code); `./build/marrow_selection_tests` -> `SelectionSet: 8 cases passed`; `./build/marrow_viewport_interaction_tests` -> passed; `./build/marrow_windowing_tests` -> `Windowing: 4 cases passed`; `./build/marrow_pen_input_tests` -> `Pen input: 34 cases passed`; `./build/marrow_agent_socket_tests` -> `Agent socket tests: 4 cases passed`
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed, reporting `MAR-172 loop boundary export: JSON 15077 bytes, MBIN 4125 bytes.`, `MAR-172 untouched baseline export: JSON 14336 bytes, MBIN 3984 bytes.`, and `MAR-172 loop boundary synchronization validated as additive, default-absent, export-neutral, and strictly re-validated.` The exported artifact is **741 JSON bytes and 141 MBIN bytes larger** than the baseline measured in the same run, so it demonstrably carries the managed boundary keys rather than the untouched baseline
- `./build/marrow_project_smoke --create /tmp/mar172_created.marrow` -> passed; the created project contains no `loop_sync` member (`grep -c loop_sync` -> `0`)
- `./build/marrow_inspect --compare <export>.mbin <export>.mskl` -> `matches`; `./build/marrow_fixture_smoke <export>.mskl assets/fixtures/player_idle.matl` -> `Generic runtime asset smoke test passed.`; `grep -c 'loop_sync' /tmp/marrow_mar172_loop.mskl` -> `0`
- `python3 -m json.tool assets/fixtures/player_idle.marrow` -> valid, and `git diff --stat -- assets/fixtures/` is empty: the checked-in fixture is untouched by MAR-172
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> passed, including the new `validate_timeline_loop_sync_shell_smoke` scenario; `--project assets/fixtures/parameter_face_basic.marrow --auto-close 2` -> passed unchanged; `--verify-launch-focus` -> `Verified macOS editor launch focus configuration.`
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` with 291 `[ OK ]` cases against the exact **59**-operation registry, including the new `timeline.set_loop_sync` expectation row immediately after `timeline.set_curve_mode` and its dry-run/live/read-back/`no_change`/adoption/duration-move/undo sequence, twelve rejection cases with a proven-unchanged project, and the eight managed-boundary guards with their authored-key survival read-backs
- `tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py` -> compiled
- `tools/mcp/venv/bin/python tools/mcp/test_client.py` against `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` -> `mcp test_client: PASSED` with **59/59** exact C++/Python name parity, the explicit `timeline.set_loop_sync` registry metadata row, and a duration -> dry-run -> live -> read-back -> `set_transform` -> read-back -> undo -> read-back sequence asserting `boundary_time == 1.5` and the mirrored angle to four decimal places, plus rejection of `"enabled": "yes"`, of a `draw_order` lane, and of enabling a clip with no explicit duration. A lane entry carrying an undeclared `time` member still succeeds, proving the advisory schema did not loosen the C++ gate
- `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only` against `parameter_face_basic.marrow` -> `mcp parameter test_client: PASSED`, unchanged
- `ctest --test-dir build -N` -> `Total Tests: 21`; `ctest --test-dir build --output-on-failure` -> `100% tests passed, 0 tests failed out of 21`; `-L runtime` -> 4/4; `-L editor` -> 11/11; `-R marrow.renderer_link_boundary` -> 1/1. MAR-172 registers no new CTest
- `./build/marrow_c_smoke` -> `commands=3, indices=6, callbackEvents=2`; `./build/marrow_spine_import_smoke ...` -> passed; `./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl|.mbin assets/fixtures/player_idle.matl` -> both passed; `./build/marrow_parameter_project_smoke` and `./build/marrow_atlas_packer_smoke` -> passed, unchanged
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j10 && ctest --test-dir build-display --output-on-failure` -> automated Debug display-enabled suite 24/24, including 3 windowing-labelled and 3 display-labelled tests, passed in 19.47 s
- Preference isolation proof: `$HOME/Library/Application Support/Marrow` did **not** exist before the run and still did not exist after every gate above, including the three `--agent-port` runs of the production `shell_main.cpp` startup load, which resolves the real path with no override. No `/tmp/marrow-shell-config-*` directory was left behind. The new `validate_timeline_loop_sync_shell_smoke` scenario installs its own `ScopedPreferenceIsolation` even though MAR-172 reads no preference, so it cannot resolve that path at all

**Three divergences from the design, recorded rather than glossed.**

First, **the design's §18.4 assumption that an opted-in 1.5 -> 2.0 duration change
resolves at least one key is false for the fixture's own angles.** With spine
rotate at `angle = 0, 8, -2` and a boundary mirroring key 0, the last two secants
have opposite signs, so `curve_auto`'s monotonicity clamp zeroes the interior
tangent and the normalized control points `[1/3, a/3, 2/3, 1 - b/3]` come out
span-invariant: `a = 0`, `b = 1` at every duration. The assertion is real and is
kept; the test seeds `0, -6, -3` instead, which keeps those two secants the same
sign so the automatic curve genuinely depends on the boundary key's time. The
substituted angles are commented at the call site with that reason.

Second, **the plan's Task 3 instruction to "leave the project-overlay
`include_animation_timeline_maximum()` calls in place" would have defeated the
exclusion it was introducing**: those calls fold the boundary key back into the
floor from the project side one line after the effective side excluded it. The
three continuous families now call a boundary-excluding overload instead, and the
three discrete families keep the original helper — which is a bit-exact no-op
when no lane is opted in, because the overload differs only in skipping a last
key whose `loop_sync` is false.

Third, **the first implementation shipped the GUI half of §10.1 and silently
dropped the Agent half**, which the plan's Task 6 had narrowed to the
controller. A review reproduced the consequence live: `timeline.set_interpolation`
on a managed boundary key returned `no_change` — the exact "the command appears
to do nothing and nothing explains why" outcome §10.1 exists to prevent — and
`remove_transform_keyframe` at the boundary time returned `ok: true` while
**destroying the authored key at 1.0**, because the synchronization then promoted
that key into the vacated boundary, moving it to 1.5 and overwriting its value
from key 0. `remove_slot_color_keyframe` and `remove_deform_keyframe` shared the
hole. The fix is the spec's own remedy — atomic rejection with §10.1's message
across all eight write and removal paths of all three families — plus a
derivation change that makes the promotion itself unreachable: the sync now
treats a lane's last key as the contract's only when it satisfies one half of the
contract, so a key that is neither at the boundary nor still a mirror of key 0 is
authored data the sync creates beside rather than overwrites. The GUI predicate
delegates to the same derivation, so no surface can disagree with the mutation
about which key is derived.

The display suites are automated evidence only. This checkpoint adds no manual
visible-UI, Windows 11, physical-input, or platform qualification credit.
MAR-192 through MAR-210 remain open, and support qualification remains governed
by `docs/root1/platform-validation.md`.

## MAR-171 Project-Local Automatic Curve Handles Validation Results

Validated 2026-08-30. A Transform or Slot Color keyframe can now record the
intent "my outgoing easing is whatever a monotone interpolant through my
neighbours says it should be", as two optional, additive, `.marrow`-only
members — `curve_mode` (`manual` | `auto`) and `curve_driver` (which scalar
series drives the computation). Both are absent from every existing project, so
every existing project keeps today's manual behaviour byte for byte. An
automatic key's easing is resolved eagerly into the pre-existing `curve` field
by a pure monotone **Fritsch–Carlson** interpolant living in the new
`ProjectData`-free translation unit `src/editor/curve_auto.cpp`, and it is
recomputed inside the *same transaction* as the neighbour-time, neighbour-value,
insertion, deletion, paste, retime, gizmo-drag, Inspector-field, or duration
change that invalidated it — one edit, one history entry. Writing any absolute
easing demotes the key to manual, and the rule lives inside
`set_keyframe_interpolation()` itself so MAR-169's handle drag, MAR-170's
presets, the numeric inspector, and the Agent all inherit it and none can forget
it. The registry grows to exactly **58** operations with `timeline.set_curve_mode`
and its matching MCP tool. `.mskl` v1, `.mbin` v2, C ABI v1, and
`editor-settings.json` v1 are unchanged, and neither new field ever reaches a
runtime file.

**The algebra removes the format risk.** Normalizing a cubic Hermite segment to
the unit square gives `cx1 = 1/3` and `cx2 = 2/3` *identically* — not by
clamping — so `X(t) = t` exactly, the runtime's inverse is the identity, and the
`cx ∈ [0, 1]` invariant both loaders enforce holds unconditionally before and
after `float32` narrowing. Fritsch–Carlson's `a² + b² ≤ 9` bounds `a, b ∈ [0, 3]`,
so `cy1 = a/3` and `cy2 = 1 − b/3` are both in `[0, 1]`: an automatic curve can
**never overshoot**. Overshoot in Marrow stays reachable only through MAR-169's
manual handle drag.

| Slice | Verification | Result |
| --- | --- | --- |
| Algorithm and the `cx` invariant | Every returned entry has `cx1 == 1.0/3.0` and `cx2 == 2.0/3.0` bit-exactly and both narrow strictly inside `(0, 1)`; the fixture's `spine` rotate series `(0,0),(0.5,8),(1,-2)` resolves to exactly `[1/3, 1/3, 2/3, 1]` and `[1/3, 0, 2/3, 2/3]` to `1e-12`; two samples give exactly `[1/3, 1/3, 2/3, 2/3]`; zero and one sample give an empty vector rather than an error; a `static_assert` pins both constants | PASS |
| Monotonicity, flatness, and no overshoot | Sampling `runtime::Interpolation::cubic_bezier(...).transform(alpha)` over a 101-point grid on every segment of the spiky series `{0, 10, 0.5, 11, 0}` is finite, non-decreasing within `1e-6`, inside `[0, 1]`, and exactly `0.0`/`1.0` at the endpoints; a flat segment resolves to the neutral `[1/3, 1/3, 2/3, 2/3]` and a plateau still zeroes the following segment's shared tangent; `{5,5,5}` gives two exactly linear segments; the disk clamp fires on `{0, 1, 1.0001}`; `{0, 1e-300, 1e300}` still returns finite in-range points; scaling every time by 1000 and every value by −7 is bitwise identical; two calls on one input are bitwise identical; a non-finite time, a non-finite value, a non-increasing pair, and a `1e-7` s segment each reject the whole track | PASS |
| Additive, default-absent `.marrow` storage | The untouched fixture serializes with neither `curve_mode` nor `curve_driver` and round-trips byte-identically; an automatic key writes the pair exactly once each while a manual key with a non-default driver in memory writes neither; both fields survive save and reload on Transform and Slot Color keys; six malformed documents are each rejected with the exact keyframe-scoped JSON path and message — a numeric `curve_mode`, `"automatic"`, a numeric `curve_driver`, `"z"`, `"x"` on a rotate key, and a driver on a manual key; `validate_project_for_save()` re-rejects an `Angle` driver on a slot-colour key | PASS |
| Same-transaction recomputation | A graph value drag, an `add_timeline_key_at_playhead()` between two automatic keys, a `remove_selected_timeline_keys()` on the middle key, and a dopesheet retime each add exactly **one** history entry with the automatic curves already updated inside it, asserted against literal expected control points for the post-edit neighbourhood; a paste carries the copied mode and driver and resolves against its **new** neighbours; a Deform-only selection opens no transaction; re-applying the same mode and driver adds no history entry and leaves the bytes identical; a resolver rejection cancels the enclosing transaction, rolling back its materialization too. The duration seam is wired on **both** surfaces — the shell's `apply_animation_duration_gesture()` and the Agent's `animation.set_duration` — and is honestly a no-op today: with consistent automatic curves a duration change leaves every stored easing byte-identical, while a deliberately stale mode/curve pair is reconciled by it, which is what proves the call is really there | PASS |
| Demotion, and why it is not MAR-169's rule | `set_keyframe_interpolation()` sets `Manual` on every key it writes and reports `changed == true` **even when the four control points are byte-identical**, because the demotion is itself the authored change; a drag away and exactly back on an automatic key therefore commits one entry with byte-identical points, while the same round trip on a manual key still commits **zero** entries, preserving MAR-169's net-state rule; a MAR-170 preset on an automatic key demotes it in the preset's own single transaction; the two paths that write `interpolation` directly rather than through the primitive — the numeric `Bezier X1..Y2` inspector and the Agent's `set_slot_color_keyframe` — each carry the same one-line demotion. `set_slot_color_keyframe` demotes only when the caller **explicitly supplies** an `interpolation`, because `interpolation_arg()` returns Linear for an absent member: an explicit curve must survive the resolver that runs after it, while a colour-only write must leave an automatic key automatic and re-resolve against the value it just changed | PASS |
| Agent and MCP parity at 58 | `timeline.set_curve_mode` sits immediately after `timeline.set_interpolation` as (`edit`, mutating, not review, dry-run supported); its dry run reports each key's `previous_mode`, `previous_driver` (`null` for a manual key), and `previous_interpolation` without touching the session; the live call reports `changed_key_count` and `resolved_key_count` in one history entry; a second identical call returns `no_change`; ten rejection cases — missing `mode`, `"automatic"`, an unknown driver, a driver with `"manual"`, a driver the family does not own, a `deform` key, a `draw_order` key, a duplicate selector, an empty `keys` array, and an unresolvable selector (`not_found`) — each leave the project provably unchanged; the demotion is proven through the Agent surface by a follow-up dry run reading `previous_mode == "manual"` | PASS |
| Export neutrality, on the mutated project | The smoke exports the project it just authored — automatic on `spine` rotate keys 0 and 1 and on `arm_l` rotate key 0 — and asserts the exported `.mskl` carries `cx1 == float(1/3)`, `cy1 == float(1/3)`, `cx2 == float(2/3)`, `cy2 == 1.0f` on `spine` rotate key 0, that `arm_l` key 0 exports a 4-number array where the fixture stores the string `"linear"`, that reading the file **as text** finds neither `curve_mode` nor `curve_driver`, and that the JSON is strictly larger than a baseline exported in the same run. Pointing the same block at the untouched baseline makes assertion 3 fail before any byte count is consulted, which was verified by inverting it | PASS |
| Compatibility | `.mskl` v1, `.mbin` v2, and C ABI v1 unchanged with a zero-byte diff on `src/runtime/**`, `include/marrow/runtime/**`, `include/marrow/c_api/**`, and `src/c_api/**`; `editor-settings.json` v1 unchanged with a zero-byte diff on `src/editor/preferences.cpp` and `include/marrow/editor/preferences.hpp` and no new preference field; `DeformKeyframeEdit` gains no member, so the Deform exclusion is compile-enforced; no field is added on the timeline-edit (lane) object, leaving that namespace unclaimed for MAR-172 | PASS |

Validated commands and outputs:

- `cmake -S . -B build && cmake --build build -j10` -> configure and all default targets built; `cmake --build build --target marrow_verify_third_party` -> vendored SDL3, zlib, Dear ImGui, Sokol, sokol_imgui, and sokol-shdc hashes verified; `cmake --build build --target marrow_constraint_warning_check` -> built
- `./build/marrow_timeline_model_tests` -> `Timeline model: 10 cases passed`, including the new `automatic curve control points` case. Its teeth were proven three ways by inverting production code: replacing the flat-segment convention with `[1/3, 0, 2/3, 1]` fails 2 assertions, disabling the disk clamp fails 2, and evaluating the clamp as `sqrt(a*a + b*b)` on the raw tangent ratio instead of `std::hypot` on the tangents fails the extreme-ratio case
- `./build/marrow_timeline_graph_model_tests` -> `Timeline graph model: 20 cases passed`; `./build/marrow_preference_tests` -> `PreferenceStore: 11 cases passed` (unchanged; MAR-171 touches no preference code); `./build/marrow_selection_tests` -> `SelectionSet: 8 cases passed`; `./build/marrow_viewport_interaction_tests` -> passed; `./build/marrow_windowing_tests` -> `Windowing: 4 cases passed`; `./build/marrow_pen_input_tests` -> `Pen input: 34 cases passed`; `./build/marrow_unit_tests` -> all runtime and renderer unit tests passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed, reporting `MAR-171 auto export: JSON 14631 bytes, MBIN 4056 bytes (baseline JSON 14336 bytes).` and `MAR-171 automatic curve storage validated as additive, default-absent, and strictly re-validated.` The exported artifact is **295 bytes larger** than the baseline measured in the same run, so it demonstrably carries the authored automatic curves rather than the untouched baseline
- `./build/marrow_project_smoke --create /tmp/player_idle.marrow` -> minimal project defaults, references, and round trip validated; `./build/marrow_parameter_project_smoke assets/fixtures/parameter_face_basic.marrow` -> passed; `python3 -m json.tool` on both fixtures -> parsed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> passed, including the new headless `validate_timeline_curve_mode_shell_smoke` scenario and the actual-frame curve-mode frames, which reported `Timeline Graph actual-frame curve mode: Auto button=(499,698.5) driver=7`. The actual-frame case clicks the reported `Auto` button with real ImGui mouse events and asserts one history entry plus a stored curve whose `cx` pair is exactly `float(1/3)`/`float(2/3)`, then presses the reported auto handle, moves 30 px, releases, and asserts the segment demoted in exactly one more entry; with nothing selected the row is drawn-but-disabled and a click on it changes nothing; MAR-167/168/169/170's `fit_*`, `first_component_*`, `first_preset_*`, and plot rectangles are all still non-degenerate
- `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2` -> parameter-mode shell smoke passed; `./build/marrow_editor_shell --verify-launch-focus` -> verified
- `ctest --test-dir build -N` -> `Total Tests: 21`; `ctest --test-dir build --output-on-failure` -> `100% tests passed, 0 tests failed out of 21`; `-L runtime` -> 4/4; `-L editor` -> 11/11; `-R marrow.renderer_link_boundary` -> 1/1. MAR-171 registers no new CTest
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j10 && ctest --test-dir build-display --output-on-failure` -> automated Debug display-enabled suite `100% tests passed, 0 tests failed out of 24` in 14.01 s, including the 3 display-only tests; `-L windowing` -> 3/3; `-L display` -> 3/3
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j10 && ctest --test-dir build-platform-release --output-on-failure` -> automated Release display-enabled suite `100% tests passed, 0 tests failed out of 24` in 7.30 s
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar171.mskl --export-binary /tmp/marrow_mar171.mbin` -> export passed; binary errors `rotation=0.00274662deg`, `position=0.000811016px`. This CLI leg exports the unedited fixture, so its JSON `14336` / MBIN `3984` are the untouched baseline by construction; the automatic-curve-carrying export is the `/tmp/marrow_mar171_auto.*` pair
- `./build/marrow_inspect --compare /tmp/marrow_mar171_auto.mbin /tmp/marrow_mar171_auto.mskl` -> `Comparison: /tmp/marrow_mar171_auto.mskl matches /tmp/marrow_mar171_auto.mbin`; JSON `14631` bytes, MBIN v2 `4056` bytes with `version=2 optimized=yes animations=3 rotate_channels=4 translate_channels=2 keys=16 sorted=yes`
- `rg -c 'curve_mode|curve_driver' /tmp/marrow_mar171_auto.mskl` -> **no match**, the direct proof of export neutrality; `wc -c /tmp/marrow_mar171_auto.mskl /tmp/marrow_mar171_export_baseline.mskl` -> `14631` vs `14336`; `python3 -m json.tool /tmp/marrow_mar171_auto.mskl` -> parsed; `./build/marrow_fixture_smoke /tmp/marrow_mar171.mskl /tmp/player_idle.matl` -> passed
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` with 227 `[ OK ]` cases against the exact **58**-operation registry, including the new `timeline.set_curve_mode` expectation row immediately after `timeline.set_interpolation` and its dry-run/live/read-back/`no_change`/demotion/undo sequence plus ten rejection cases with a proven-unchanged project
- `./build/marrow_agent_socket_tests` -> `Agent socket tests: 4 cases passed`; `./build/marrow_c_smoke` -> C ABI loaded 3 commands, 6 indices, and 2 callback events
- `tools/mcp/venv/bin/python tools/mcp/test_client.py` against `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` -> `mcp test_client: PASSED` with **58/58** exact C++/Python name parity, the explicit `timeline.set_curve_mode` registry metadata row, and a dry-run/live/read-back/undo sequence asserting the resolved `[0.3333, 0.3333, 0.6667, 1.0]` at four decimal places plus rejection of `"automatic"`, of an `angle` driver on a `slot_color` key, of a `deform` key, and of a driver supplied with `"manual"`. Removing the new MCP tool makes the client fail on `assert len(mcp_names) == 58`, which was verified
- `tools/mcp/venv/bin/python tools/mcp/test_client.py --parameter-only` against `parameter_face_basic.marrow` -> `mcp parameter test_client: PASSED`; MCP schema `py_compile` -> passed
- `git diff --stat -- include/marrow/c_api src/c_api`, `-- src/runtime include/marrow/runtime`, `-- src/editor/preferences.cpp include/marrow/editor/preferences.hpp`, and `-- src/tests/preference_store_tests.cpp` -> **all empty**; `./build/marrow_inspect assets/fixtures/player_idle.mbin` -> `version=2`; `git diff --check` -> clean; `git lfs status` -> no LFS object staged or queued to push (the tracked patterns are `*.png`, `*.psd`, `*.otf`, `*.mbin`; no modified file matches one)
- Preference isolation proof: `$HOME/Library/Application Support/Marrow` did **not** exist before the run and still did not exist after every gate above, including the three `--agent-port` runs of the production `shell_main.cpp` startup load, which resolves the real path with no override. The new `validate_timeline_curve_mode_shell_smoke` scenario installs its own `ScopedPreferenceIsolation` even though MAR-171 reads no preference, so it cannot resolve that path at all
- Post-review fixes, each RED-first. The shell's duration gesture did not call the resolver even though the design lists it as a trigger and MAR-172 builds on that seam; it now does, and removing the call again makes the shell smoke report `The shell duration gesture did not resolve automatic curves`. `set_slot_color_keyframe` wrote `interpolation` on an existing key without demoting, so the resolver that follows silently reverted an agent-supplied curve and the call returned `no_change`; it now demotes on an explicitly supplied easing, and demoting unconditionally instead makes the agent smoke report `a colour-only write must leave an automatic key automatic`

**One divergence from the design, found by the Task 4 export test and recorded
rather than glossed.** The design's §8.6 assumed `build_runtime_document()`
builds transform keyframes through a `build_runtime_*` function of its own, as
it does for slot colour, and the plan therefore forbade any change to the export
path. It does not: the `.marrow` and `.mskl` transform keyframe shapes were
identical, so the export **reused the project serializer**
`build_transform_keyframes_value()`. The first run of the export test caught
this immediately — `MAR-171 leaked a project-local field into the runtime
export` — and the fix is a dedicated `build_runtime_transform_keyframes_value()`
that emits a fixed member list, exactly mirroring
`build_runtime_slot_color_keyframes_value()`. The export path is now
structurally incapable of carrying a project-local keyframe field, which is what
§8.6 claimed all along. `git diff -- src/editor/project.cpp | rg 'build_runtime'`
is therefore **not** empty, contrary to the plan's gate; the four hits are that
new function and its single call site.

The display suites are automated evidence only. This checkpoint adds no manual
visible-UI, Windows 11, physical-input, or platform qualification credit.
MAR-192 through MAR-210 remain open, and support qualification remains governed
by `docs/root1/platform-validation.md`.

## MAR-170 Fixed Curve Presets and Remembered Defaults Validation Results

Validated 2026-08-30. Six fixed presets — Linear, Stepped, Ease
`[0.25, 0.1, 0.25, 1]`, Ease-In `[0.42, 0, 1, 1]`, Ease-Out `[0, 0, 0.58, 1]`,
and Ease-In-Out `[0.42, 0, 0.58, 1]`, the CSS Easing Level 1 timing functions —
are declared once as the `constexpr kCurvePresets` table in
`include/marrow/editor/authoring.hpp` and applied to every compatible selected
key through MAR-169's single `set_keyframe_interpolation()` primitive, from a
shared row drawn in both the Graph and Dopesheet toolbars. One application is
one `EditTransaction`, one previewed `refresh_runtime()`, and one history entry
whatever the key count; easing-free Draw Order, Event, and Slot Attachment
selections are skipped and reported by the GUI and still rejected atomically by
the Agent. MAR-156's never-consumed `editor-settings.json` `default_curve` field
gains its consumer: the remembered default seeds newly authored Transform,
Deform, and Slot Color keys — from "Add Key At Playhead", from a viewport FFD
vertex drag, from a viewport translate/rotate/scale gizmo drag, and from the
Inspector transform fields alike — and is changed only by the explicit
`Default:` combo, never as a side effect of applying a preset. The current-preset readout
is a pure bit-exact `float32` function of the stored curve with no persisted
marker, so it reads `Custom Bezier` the moment a MAR-169 handle drag moves away
from a preset and needs no invalidation under undo, redo, or reload. The
Agent/MCP surface stayed at exactly **57** operations: the growth was four new
string tokens in one operation's `interpolation` argument vocabulary. Automatic
and project-local curve handles remain MAR-171.

| Slice | Verification | Result |
| --- | --- | --- |
| Preset constants | The six presets are Linear, Stepped, Ease `[0.25, 0.1, 0.25, 1]`, Ease-In `[0.42, 0, 1, 1]`, Ease-Out `[0, 0, 0.58, 1]`, Ease-In-Out `[0.42, 0, 0.58, 1]`; a `static_assert` keeps the table in enum order and every `cx` inside `[0, 1]`, re-asserted at runtime before and after `float32` narrowing; each cubic evaluates finite, non-decreasing, overshoot-free, and exactly `0.0`/`1.0` at the endpoints over a 101-point grid, including Ease-In's `X'(1) = 0` right endpoint | PASS |
| Preset application | Applying to compatible selected keys is order-independent — the same twelve selectors reversed serialize byte-identically — skips and counts easing-free keys, collapses a duplicate ref instead of turning it into the primitive's hard error, materializes runtime-only tracks, and produces exactly one previewed undoable transaction; re-applying commits nothing and reports `Selected keys already use <name>`; an easing-free-only selection opens no transaction at all | PASS |
| Remembered default | `default_curve` seeds newly authored Transform, Deform, and Slot Color keys, and a `Stepped` default seeds a Stepped key; all five shell creation paths agree — "Add Key At Playhead", the FFD vertex drag, the three viewport gizmo drags, and the Inspector transform fields — because the shared `upsert_transform_keyframe()`/`upsert_deform_keyframe()` primitives take the seed as an argument that defaults to Linear, so `marrow_editor` never reads a preference and the Agent keeps its reproducible Linear default; the seed initializes an inserted key only and never rewrites the curve of a key that already exists; pasted keys keep the copied curve; it is stored atomically in `editor-settings.json` v1 with `recent_projects` and unknown additive fields preserved; missing, malformed, unsupported-version, and unreadable data fall back to Linear, rewrite no curve, and rewrite no file | PASS |
| Project isolation | Changing and saving the default leaves `serialize_project()`, `dirty()`, `undo_count()`, `redo_count()`, and `project_revision()` byte-identical; `MARROW_CONFIG_HOME` isolates every smoke process, and the real user settings directory did not exist before or after the entire validation run, including two runs of the production `shell_main.cpp` load path against the real resolved path | PASS |
| Curve identity | The readout is a pure bit-exact `float32` function of the stored curve — comparing against the `double` literals instead makes all four cubic presets read `Custom`, which was proven by inverting the implementation; a preset survives save, reload, `.mskl` export, and the v2 `.mbin` as the same preset; a handle drag makes it read `Custom`; a preset followed by a drag is exactly two undo entries and one undo restores the preset | PASS |
| Agent and MCP parity | `timeline.set_interpolation` accepts `ease`, `ease_in`, `ease_out`, and `ease_in_out` with matching C++/Python dry-run, validation, affected-key, mutation, and undo behaviour; `ease-in`, `easeIn`, and `bounce` stay rejected with the project byte-identical; the registry stayed at **57** operations and `interpolation_arg()`, `set_transform`, `set_deform_keyframe`, `set_slot_color_keyframe`, and `agent_handlers_editing.cpp` are unchanged | PASS |
| Compatibility | `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1 (`kEditorSettingsVersion` still `1`), and `SelectionSet` unchanged; only the pre-existing `curve` and `default_curve` fields are written, `src/editor/preferences.cpp` and `include/marrow/editor/preferences.hpp` show a zero-byte diff, and the `authoring.hpp` diff is 61 added lines with 0 removed | PASS |

Validated commands and outputs:

- `cmake -S . -B build && cmake --build build -j8` -> configure and all default targets built
- `./build/marrow_preference_tests` -> `PreferenceStore: 11 cases passed`, including the new `fixed curve preset constants, identity, and well-posedness`, `preset tokens agree with the settings-file vocabulary`, and `shell preference session load, save, fallback, and preservation` cases
- `./build/marrow_timeline_graph_model_tests` -> `Timeline graph model: 20 cases passed`; `./build/marrow_timeline_model_tests` -> `Timeline model: 9 cases passed`; `./build/marrow_viewport_interaction_tests` -> `Viewport interaction kernel tests passed.`; `./build/marrow_selection_tests` -> `SelectionSet: 8 cases passed`
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed; `MAR-170 fixed curve presets validated across transform, deform, and slot-colour families` covering all six presets on a Transform, a Deform, and a Slot Color key with byte-identical scalars, the exactly-one-`interpolation`-field segment-wide assertion, twelve-selector order independence, the double-application no-op, the three easing-free atomic rejections, a save/reload preset-identity round trip, and `upsert_transform_keyframe()`'s seeding contract: no argument inserts Linear (the Agent's contract), an explicit seed inserts that preset (the shell's), and an existing key keeps its own curve while its value is updated
- The same smoke exports the preset-edited project and reports `MAR-170 preset export: JSON 14452 bytes, MBIN 4020 bytes`, asserting the exported runtime JSON carries the narrowed Ease-In-Out quadruple on the `arm_l` rotate key at `t = 0.25` and that `curve_preset_of()` still names it Ease-In-Out there. The fixture's own value at that key is `"curve": "linear"`, and the exported file reads `[0.419999986886978, 0, 0.579999983310699, 1]`, so the exported artifact demonstrably carries the authored edit rather than the untouched baseline `14336`/`3984`
- `./build/marrow_project_smoke --create /tmp/player_idle.marrow` -> minimal project defaults, references, and round trip validated
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> passed, including the new headless `validate_timeline_curve_preset_shell_smoke` scenario, the FFD smoke's new case asserting a vertex-drag-authored Deform key at `t = 0.62` carries the remembered `Ease-Out` while the fixture's own `t = 1.0` key keeps its imported `stepped` curve, the viewport smoke's new case asserting a rotate-gizmo-authored `root` key at `t = 0.37` carries the remembered `Ease-In-Out`, and the actual-frame preset frames, which reported `Timeline Graph actual-frame presets: first button=(405,698.5) default=4`. The actual-frame case clicks the reported first preset button with real ImGui mouse events, asserts one history entry and the settled `Linear` readout, asserts the row is drawn-but-disabled with an empty selection, that hovering its *first* button still submits a guidance tooltip in that frame, and that a click on it changes nothing, and asserts MAR-167/168's `fit_*` and `first_component_*` rectangles are still non-degenerate and still hoverable
- `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2` -> parameter-mode shell smoke passed
- `ctest --test-dir build -N` -> `Total Tests: 21`; `ctest --test-dir build --output-on-failure` -> `100% tests passed, 0 tests failed out of 21` in 2.14 s. MAR-170 registers no new CTest.
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j8 && ctest --test-dir build-display --output-on-failure` -> automated Debug display-enabled suite `100% tests passed, 0 tests failed out of 24`, including the 3 display-only tests `marrow.window_host_smoke`, `marrow.gpu_parity_smoke`, and `marrow.editor_display_smoke`
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j8 && ctest --test-dir build-platform-release --output-on-failure` -> automated Release display-enabled suite `100% tests passed, 0 tests failed out of 24`, same 3 display-only tests
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar170.mskl --export-binary /tmp/marrow_mar170.mbin` -> export passed; binary errors `rotation=0.00274662deg`, `position=0.000811016px`. This CLI leg exports the unedited fixture, so its JSON `14336` / MBIN `3984` are the untouched baseline by construction; the preset-carrying export is the `/tmp/marrow_mar170_preset.*` pair below
- `./build/marrow_inspect --compare /tmp/marrow_mar170.mbin /tmp/marrow_mar170.mskl` -> `Comparison: /tmp/marrow_mar170.mskl matches /tmp/marrow_mar170.mbin`; `rotate_keys=10->10`, `translate_keys=6->6`, JSON `14336` bytes, MBIN v2 `3984` bytes with `version=2 optimized=yes animations=3 rotate_channels=4 translate_channels=2 keys=16 sorted=yes`
- `./build/marrow_inspect --compare /tmp/marrow_mar170_preset.mbin /tmp/marrow_mar170_preset.mskl` -> match on the preset-edited export; `rotation_error=0.00274662deg`, `position_error=0.000811016px`, `rotate_keys=10->10`, `translate_keys=6->6`, JSON `14452` bytes, MBIN v2 `4020` bytes with `version=2 optimized=yes animations=3 rotate_channels=4 translate_channels=2 keys=16 sorted=yes`
- `./build/marrow_fixture_smoke /tmp/marrow_mar170.mskl /tmp/player_idle.matl` and `./build/marrow_fixture_smoke /tmp/marrow_mar170_preset.mskl /tmp/player_idle.matl` -> both passed with 16 bones, 7 slots, 5 skins, 3 animations, 2 events, 3 draw commands, and 1 clip
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` with 203 `[ OK ]` cases against the **still exactly 57**-operation registry, including the four new preset-token write/read-back/undo sequences, the `stepped` token round trip, the three rejected spellings with a proven-unchanged project, the multi-key single-entry preset call whose one undo restores both keys, and `set_transform` still creating a Linear key
- `./build/marrow_agent_socket_tests` -> `Agent socket tests: 4 cases passed`; `./build/marrow_c_smoke` -> C ABI loaded 3 commands, 6 indices, and 2 callback events
- `tools/mcp/venv/bin/python tools/mcp/test_client.py` against `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` -> `mcp test_client: PASSED` with 57/57 exact C++/Python name parity and the `ease_in_out` dry-run/live/read-back/undo sequence asserting `[0.42, 0.0, 0.58, 1.0]` at four decimal places, plus rejection of `ease-in` and of an out-of-range array proving the widened schema did not loosen the C++ gate
- MCP schema `py_compile`, `cmake --build build --target marrow_verify_third_party`, fixture/`.mskl`/PRD JSON parsing, `git diff --check`, and `git lfs status` -> passed; no LFS object was staged or queued to push
- `git diff --stat -- include/marrow/c_api src/c_api`, `git diff --stat -- src/editor/project.cpp src/runtime/skeleton_parse.cpp src/runtime/binary.cpp`, and `git diff --stat -- src/editor/preferences.cpp include/marrow/editor/preferences.hpp` -> all empty; `git diff --numstat -- include/marrow/editor/authoring.hpp` -> `61 0`; the `src/editor/agent_dispatch.cpp` diff touches only `interpolation_request_arg()`'s string branch and its error string, and the `tools/mcp/tools/editing.py` diff touches only `_bezier_interpolation_schema()`
- Preference isolation proof: `$HOME/Library/Application Support/Marrow` did **not** exist before the run and still did not exist after every gate above, including the two `--agent-port` runs of the production `shell_main.cpp` startup load, which resolves the real path with no override. No `/tmp/marrow-shell-config-*` isolation directory was left behind

The display suites are automated evidence only. This checkpoint adds no manual
visible-UI, Windows 11, physical-input, or platform qualification credit.
MAR-192 through MAR-210 remain open, and support qualification remains governed
by `docs/root1/platform-validation.md`.

## MAR-169 Graphical Shared Bezier Handle Editing Validation Results

Validated 2026-08-30. The Timeline Graph tab now edits the active key's outgoing
shared `[cx1, cy1, cx2, cy2]` easing directly. A left press on a drawn handle
arms the same MAR-168 drag candidate, holding no transaction, and the first
motion past the shared inclusive 4.0 logical-pixel dead zone opens one
`EditTransaction`. A handle drag is free 2-D: `decide_drag_axis()` is
deliberately not called, because `cx` and `cy` are two parameters of one curve
written by one primitive. `cx` is clamped into `[0, 1]` by the pure pointer
mapping so a drag past the boundary stops there and stays live, while the new
additive `set_keyframe_interpolation()` primitive independently *rejects* any
out-of-range or non-finite value atomically; finiteness is tested before range,
because `NaN < 0` and `NaN > 1` are both false. Finite `cy` overshoot is
preserved. Grabbing a Linear or Stepped handle converts the segment to Cubic
seeded at `[1/3, 1/3, 2/3, 2/3]` inside the same transaction and the same undo
entry. The identical mutation is exposed as the 57th Agent operation
`timeline.set_interpolation` and as one matching MCP tool, both calling the same
primitive. Curve presets and remembered defaults remain MAR-170.

| Slice | Verification | Result |
| --- | --- | --- |
| Handle mapping | Handles sit at `[cx1, cy1]` and `[cx2, cy2]` along the frozen segment frame; the pixel/control-point mapping round-trips within `1e-9` and its anchors agree with the polyline `build_geometry()` renders; a flat segment substitutes a positive 100 logical-pixel reference span with `flat_value_span` set, and a zero-duration segment exposes no handles at all | PASS |
| X limits and Y overshoot | Drags clamp `cx` to exactly `0.0` and `1.0` and keep the gesture live; the primitive rejects `-1e-6`, `1.0000001`, NaN, `±inf`, and `1e300` atomically with the project byte-identical; finite `cy` overshoot `[0.2, -0.4, 0.8, 1.6]` round-trips through save/reload and reaches the exported runtime JSON verbatim as `[0.2, -0.4, 0.8, 1.6]` on the `arm_l` rotate key at `t = 0.25`, with the v2 MBIN payload compared equal to it | PASS |
| Segment-wide curve identity | `set_keyframe_interpolation()` takes no component parameter; editing while displaying Translate Y makes the Translate X segment report the same `SegmentKind` and control points; exactly one `interpolation` field in the whole project differs; switching the displayed scalar never forks the curve | PASS |
| Conversion and transaction | Grabbing a Linear or Stepped handle converts to Cubic seeded at `[1/3, 1/3, 2/3, 2/3]` inside one transaction; a press without motion opens none; a drag that ends on its original Cubic points commits none and reports no edit, whether it matched on the first frame or travelled and came back, while returning to the seed of a Linear or Stepped segment still commits because the authored kind changed; Escape, `cancel_authoring_gestures()`, `cancel_timeline_graph_point_drag()`, a mid-drag Dopesheet tab switch, a lost active key, a non-finite pointer, and every primitive rejection restore project bytes, history, redo depth, project revision, dirty state, dopesheet key times, graph values, graph segment kinds, and graph control points | PASS |
| Agent and MCP parity | `timeline.set_interpolation` is the 57th operation (`edit`, mutating, no review, dry-run supported) with matching C++/Python dry-run, validation, per-key `previous_interpolation` reporting, mutation, `no_change`, and undo behaviour; the growth is purely additive and no existing operation's name, category, flags, arguments, or response shape changed | PASS |
| Persistence and compatibility | Save/reload preserves the authored curve bitwise and export carries it into both JSON and the v2 binary, moving the exported JSON off its untouched-baseline size; `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1, `SelectionSet`, and GPU ownership are unchanged, and only the pre-existing `curve` field is written | PASS |

Validated commands and outputs:

- `cmake -S . -B build && cmake --build build -j10` -> configure and all default targets built
- `./build/marrow_timeline_graph_model_tests` -> `Timeline graph model: 20 cases passed`, including the new `handle geometry and pointer mapping` case
- `./build/marrow_timeline_model_tests` -> `Timeline model: 9 cases passed`
- `./build/marrow_viewport_interaction_tests` -> `Viewport interaction kernel tests passed.`; `./build/marrow_selection_tests` -> `SelectionSet: 8 cases passed`
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed; `MAR-169 shared bezier interpolation authoring validated across transform, slot-colour, and deform families` covering Rotate/Translate/Scale/Shear, Slot Color, Deform, the exactly-one-`interpolation`-field identity assertion, Linear/Stepped -> Cubic and Cubic -> Linear/Stepped conversion, the `[0, 0, 1, 1]` boundary, eight atomic control-point rejections, seven structural rejections, the split `key_count`/`changed_key_count` no-op, and an overshoot save/reload round trip
- The same smoke exports the curve-edited project and reports `MAR-169 curve export: JSON 14484 bytes, MBIN 4020 bytes`, asserting the exported runtime JSON carries `[0.2, -0.4, 0.8, 1.6]` on the `arm_l` rotate key at `t = 0.25` and that the v2 binary matches it. Both sizes moved off the untouched-baseline `14336`/`3984`, and its binary round-trip error is `rotation=0.00598526deg`, distinct from the baseline `0.00274662deg`
- `./build/marrow_project_smoke --create /tmp/player_idle.marrow` -> minimal project defaults, references, and round trip validated
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> passed, including the new headless `validate_timeline_graph_easing_shell_smoke` scenario and the actual-frame handle frames, which reported `Timeline Graph actual-frame handles: first=(592.909,775.303) second=(747.455,749.235)`. The scenario also covers a multi-frame drag that leaves the original control points and returns to them, asserting the status message is untouched as well as the history, and a Stepped segment whose drag returns to the seed, asserting it still commits the Cubic conversion
- `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2` -> parameter-mode shell smoke passed
- `ctest --test-dir build -N` -> `Total Tests: 21`; `ctest --test-dir build --output-on-failure` -> `100% tests passed, 0 tests failed out of 21` in 0.85 s. MAR-169 registers no new CTest.
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j10 && ctest --test-dir build-display --output-on-failure` -> automated Debug display-enabled suite `100% tests passed, 0 tests failed out of 24` in 9.35 s, including 3 display-labelled tests
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j10 && ctest --test-dir build-platform-release --output-on-failure` -> automated Release display-enabled suite `100% tests passed, 0 tests failed out of 24` in 3.81 s, including 3 display-labelled tests
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar169.mskl --export-binary /tmp/marrow_mar169.mbin` -> export passed; binary errors `rotation=0.00274662deg`, `position=0.000811016px`
- `./build/marrow_inspect --compare /tmp/marrow_mar169.mbin /tmp/marrow_mar169.mskl` -> `Comparison: /tmp/marrow_mar169.mskl matches /tmp/marrow_mar169.mbin`; `rotate_keys=10->10`, `translate_keys=6->6`, JSON `14336` bytes, MBIN v2 `3984` bytes with `version=2 optimized=yes animations=3 rotate_channels=4 translate_channels=2 keys=16 sorted=yes`
- `./build/marrow_inspect --compare /tmp/marrow_mar169_curve.mbin /tmp/marrow_mar169_curve.mskl` -> match on the curve-edited export; `rotation_error=0.00598526deg`, `position_error=0.000811016px`, `rotate_keys=10->10`, `translate_keys=6->6`, JSON `14484` bytes, MBIN v2 `4020` bytes. `./build/marrow_fixture_smoke /tmp/marrow_mar169_curve.mskl /tmp/player_idle.matl` -> passed
- `./build/marrow_fixture_smoke /tmp/marrow_mar169.mskl /tmp/player_idle.matl` -> exported runtime passed with 16 bones, 7 slots, 5 skins, 3 animations, 2 events, 3 draw commands, and 1 clip
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` against the exact 57-operation registry, including the new `timeline.set_interpolation` expectation row and its dry-run/live/read-back/`no_change`/undo sequence plus ten rejection cases. `agent_operation_descriptor_count()` is asserted to equal 57 at the start of each of the three graph shell scenarios and compared for equality at their ends
- `./build/marrow_agent_socket_tests` -> `Agent socket tests: 4 cases passed`; `./build/marrow_c_smoke` -> C ABI loaded 3 commands, 6 indices, and 2 callback events
- `tools/mcp/venv/bin/python tools/mcp/test_client.py` against `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --agent-port 9876` -> `mcp test_client: PASSED` with 57/57 exact C++/Python name parity, the asserted `timeline.set_interpolation` registry metadata row, and the dry-run/live/read-back/undo sequence proving `[0.2, -0.4, 0.8, 1.6]` survives and is restored
- MCP schema `py_compile`, `cmake --build build --target marrow_verify_third_party`, fixture/`.mskl`/PRD JSON parsing, `git diff --check`, and `git lfs status` -> passed; no LFS object was staged or queued to push
- `git diff --stat -- include/marrow/c_api src/c_api` and `git diff --stat -- src/editor/project.cpp src/runtime/skeleton_parse.cpp src/runtime/binary.cpp` -> empty; the `include/marrow/editor/authoring.hpp`, `src/editor/agent_dispatch.cpp`, and `src/editor/agent_handlers_editing.cpp` diffs contain added lines only

The display suites are automated evidence only. This checkpoint adds no manual
visible-UI, Windows 11, physical-input, or platform qualification credit.
MAR-192 through MAR-210 remain open, and support qualification remains governed
by `docs/root1/platform-validation.md`.

## MAR-168 Graph Key Time and Value Editing Validation Results

Validated 2026-08-30. The Timeline Graph tab is now authoritative for editing.
A left press on a graph point arms a drag candidate that holds no transaction;
the first motion past an inclusive 4.0 logical-pixel dead zone locks one axis by
dominant-axis comparison and keeps it for the rest of the gesture. A Value-locked
drag offsets exactly one scalar component of every selected key on the focused
track through the new additive `offset_keyframe_scalars()` primitive; a
Time-locked drag reuses the dopesheet's `begin/apply/finish_timeline_retime_gesture()`
trio verbatim. The Graph toolbar exposes the same shared
`TimelineEditorState::snap_to_frames` field the Dopesheet tab owns, and Alt
bypasses snapping for the current drag. No code path reads or writes
`Interpolation`; Bezier editing remains MAR-169.

| Slice | Verification | Result |
| --- | --- | --- |
| Axis lock and component preservation | Locked-axis drags: vertical changes only the active component; horizontal changes only key times. `decide_drag_axis` unit tests cover the dead zone, both dominant directions, the exact tie, and every non-finite input; headless drags assert that later off-axis motion never re-decides the axis; actual-frame drags report `drag_axis` and the matching live gesture | PASS |
| Shared authoring reuse | Frame snap, neighbour collision with 1 ms spacing, stable identity, and explicit-duration auto-grow come from `retime_keyframes()`/`refresh_runtime()`; values come from `offset_keyframe_scalars()`. Single retime ownership follows from placement: the dopesheet updater is called only from inside the Dopesheet tab body, and leaving the Graph tab cancels the graph drag first. The Graph-mode guard inside `update_timeline_retime_gesture()` is unreachable defence in depth against that call being hoisted | PASS |
| Atomic rollback | Escape, `cancel_authoring_gestures()`, a mid-drag tab switch, a non-finite delta, and every hard primitive rejection restore project bytes, history, project revision, dirty state, rebuilt dopesheet key times, and every rebuilt graph component value | PASS |
| One drag, one transaction | Press-only opens none; a zero-net drag on an already-authored row commits none; a committed drag adds exactly one undo entry with `selected_keys` and `active_key` bit-identical across commit, undo, and redo of a value edit | PASS |
| Persistence and compatibility | An actually offset project round-trips through save/reload and through JSON plus v2 MBIN export with the offset value present in the exported runtime; `.mskl` v1, `.mbin` v2, C ABI v1, and the 56-operation Agent/MCP surface unchanged; the only public-header change is additive | PASS |

Validated commands and outputs:

- `cmake -S . -B build && cmake --build build -j8` -> configure and all default targets built
- `./build/marrow_timeline_graph_model_tests` -> `Timeline graph model: 19 cases passed`, including the new `drag axis lock and unit mapping` case
- `./build/marrow_timeline_model_tests` -> `Timeline model: 9 cases passed`, including the new `graph value gesture completion reuse` case
- `./build/marrow_viewport_interaction_tests` -> passed; `./build/marrow_selection_tests` -> `SelectionSet: 8 cases passed`
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> passed; `MAR-168 graph scalar authoring validated across 11 lane-family cases` covering Rotate Angle, Translate X/Y, Scale X/Y, Shear X/Y, Slot Color R/G/B/A, the setup-pose-free Rotate delta, signed and exact-zero scale, the group colour clamp, the four out-of-range colour cases (a lone `1.4` alpha and a mixed `{0.2, 1.4}` group, each dragged both ways), thirteen atomic rejections, and a save/reload round trip
- The same smoke exports the offset project and reports `MAR-168 offset export: JSON 14338 bytes, MBIN 3984 bytes`, asserting the offset angle is present in the exported runtime JSON and that the v2 binary matches it. Its binary round-trip error is `rotation=0.00305176deg`, distinct from the untouched baseline export
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> passed, including the new headless `validate_timeline_graph_edit_shell_smoke` drag scenario and the actual-frame drags, which reported `Timeline Graph actual-frame drags: value axis=2 time axis=1 active point=(438.364,814.064)`
- `./build/marrow_editor_shell --project assets/fixtures/parameter_face_basic.marrow --auto-close 2` -> parameter-mode shell smoke passed
- `ctest --test-dir build -N` -> `Total Tests: 21`; `ctest --test-dir build --output-on-failure` -> `100% tests passed, 0 tests failed out of 21` in 0.83 s. MAR-168 registers no new CTest.
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j8 && ctest --test-dir build-display --output-on-failure` -> automated Debug display-enabled suite 24/24, including 3 display-labelled tests, passed in 8.58 s
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j8 && ctest --test-dir build-platform-release --output-on-failure` -> automated Release display-enabled suite 24/24, including 3 display-labelled tests, passed in 3.40 s
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar168.mskl --export-binary /tmp/marrow_mar168.mbin` -> export passed; binary errors `rotation=0.00274662deg`, `position=0.000811016px`
- `./build/marrow_inspect --compare /tmp/marrow_mar168.mbin /tmp/marrow_mar168.mskl` -> match; `rotate_keys=10->10`, `translate_keys=6->6`, JSON `14336` bytes, MBIN v2 `3984` bytes with `version=2 optimized=yes animations=3 rotate_channels=4 translate_channels=2 keys=16 sorted=yes`
- `./build/marrow_inspect --compare /tmp/marrow_mar168_offset.mbin /tmp/marrow_mar168_offset.mskl` -> match on the graph-edited export; `rotation_error=0.00305176deg`, `position_error=0.000811016px`, `rotate_keys=10->10`, `translate_keys=6->6`, JSON `14338` bytes, MBIN v2 `3984` bytes
- `./build/marrow_fixture_smoke /tmp/marrow_mar168.mskl /tmp/player_idle.matl` -> exported runtime passed with 16 bones, 7 slots, 5 skins, 3 animations, 2 events, 3 draw commands, and 1 clip
- `./build/marrow_agent_dispatch_smoke` -> `agent_dispatch_smoke: PASSED` against the unchanged exact 56-operation registry. `agent_operation_descriptor_count()` is additionally asserted to equal 56 at the start of the graph-edit shell scenario and compared for equality at its end, and the MAR-167 graph scenario keeps its own `!= 56U` guard; `./build/marrow_agent_socket_tests` -> `Agent socket tests: 4 cases passed`; `./build/marrow_c_smoke` -> C ABI loaded 3 commands, 6 indices, and 2 callback events
- MCP schema `py_compile`, `cmake --build build --target marrow_verify_third_party`, fixture/`.mskl`/PRD JSON parsing, `git diff --check`, and `git lfs status` -> passed; no LFS object was staged or queued to push
- `git diff --stat -- include/marrow/c_api src/c_api` and `git diff --stat -- src/editor/agent_dispatch.cpp src/editor/agent_handlers_editing.cpp tools/mcp` -> empty; the `include/marrow/editor/authoring.hpp` diff contains added lines only

The display suites are automated evidence only. This checkpoint adds no manual
visible-UI, Windows 11, physical-input, or platform qualification credit.
MAR-192 through MAR-210 remain open, and support qualification remains governed
by `docs/root1/platform-validation.md`.

## MAR-167 Synchronized Scalar Graph Validation Results

Validated 2026-08-20 and final-review corrections revalidated 2026-08-21. The
Timeline window now has a read-only Graph tab for one
focused continuous parent track. It projects effective runtime Bone
Rotate/Translate/Scale/Shear or Slot light RGBA, shares parent-key selection,
`active_key`, focus, and playhead with the dopesheet, and keeps all graph view
state transient. `player_idle` resolves seven supported parent tracks and
fourteen scalar series because the valid arm_l Rotate project overlay is part of
the effective runtime animation.

| Slice | Verification | Result |
| --- | --- | --- |
| Projection and exclusion | UI-free tests cover the 7 parent/14 scalar fixture projection, absolute unwrapped Rotate, shared parent identity/easing, and fail-closed Inherit, Attachment, FFD, Draw Order, and Event exclusion | PASS |
| Interaction and ownership | One focused parent track exposes per-component toggles and Fit. Wheel time zoom, bounded Shift-wheel value zoom, and middle pan are duration-independent; point activation shares parent selection, `active_key`, focus, and playhead with the dopesheet. Explicit fallback legend/empty-plot interaction focuses its displayed parent while render, Fit, wheel, and pan do not | PASS |
| Easing and presentation | Actual Linear, Stepped, and Cubic outgoing easing use distinct markers. The UI states that easing is parent-key-wide across X/Y or RGBA, and point time/value dragging is read-only until MAR-168 | PASS |
| Persistence and compatibility | Tab/view/cache/visibility/active-component interactions leave serialization, dirty state, history, project/runtime revision, `.mskl` v1, `.mbin` v2, C ABI v1, and the 56-operation Agent/MCP surface unchanged | PASS |

Fresh commands and outputs:

- `cmake -S . -B build && cmake --build build -j4 && ./build/marrow_timeline_graph_model_tests && ./build/marrow_timeline_model_tests && ./build/marrow_project_smoke assets/fixtures/player_idle.marrow && ./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2 && ctest --test-dir build --output-on-failure` -> configure/build passed; graph model 18/18; timeline model 8/8; project smoke passed; actual two-frame shell smoke passed; default CTest 21/21.
- The actual-frame shell smoke reported `spine_rotate points=3 linear=0 stepped=1 cubic=1 playhead=1`, `arm_l_rotate points=2 linear=1 stepped=0 cubic=0 playhead=0`, and `spine_translate points=6 linear=2 stepped=2 cubic=0 playhead=1`. Its real wheel arbitration reported hovered plot ownership with graph span `927.273->1066.36` and unchanged Timeline scroll `326->326`; the graph margin left the graph span `1066.36->1066.36` and changed Timeline scroll `326->261`.
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j4 && ctest --test-dir build-display --output-on-failure` -> automated Debug display-enabled suite 24/24, including 3 display tests, passed in 12.58 s.
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j4 && ctest --test-dir build-platform-release --output-on-failure` -> automated Release display-enabled suite 24/24, including 3 display tests, passed in 6.22 s.
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar167.mskl --export-binary /tmp/marrow_mar167.mbin` -> project/runtime export passed; binary errors `rotation=0.00274662deg`, `position=0.000811016px`.
- `./build/marrow_inspect --compare /tmp/marrow_mar167.mbin /tmp/marrow_mar167.mskl` -> match; `rotate_keys=10->10`, `translate_keys=6->6`, JSON `14336` bytes, MBIN v2 `3984` bytes.
- `./build/marrow_fixture_smoke /tmp/marrow_mar167.mskl /tmp/player_idle.matl` -> exported runtime passed with 16 bones, 7 slots, 5 skins, 3 animations, 2 events, 3 draw commands, and 1 clip.
- `./build/marrow_agent_dispatch_smoke` -> passed against the unchanged exact 56-operation registry; `./build/marrow_agent_socket_tests` -> 4/4 passed; `./build/marrow_c_smoke` -> C ABI loaded 3 commands, 6 indices, and 2 callback events.
- MCP schema `py_compile`, `cmake --build build --target marrow_verify_third_party`, fixture/PRD JSON parsing, `git diff --check`, and `git lfs status` passed; no LFS object was staged or queued to push.

The display suites are automated evidence only. This checkpoint adds no manual
visible-UI, Windows 11, physical-input, or platform qualification credit.
MAR-192 through MAR-210 remain open, and support qualification remains governed
by `docs/root1/platform-validation.md`.

## MAR-166 Grid and Magnetic FFD Vertex Snapping Validation Results

Validated 2026-08-20. Attachment-local FFD group drags now share the project
world grid and can opt into magnetic matching against currently visible mesh
vertices. The pressed selected vertex remains the group anchor, and one snapped
common world delta preserves the MAR-164 transaction and linked-target boundary.

| Slice | Verification | Result |
| --- | --- | --- |
| Project and settings | Additive `magnetic_vertex_enabled` defaults false when `.snap` or only the field is absent, rejects non-boolean input, preserves unknown nested data, survives filesystem save/reload, and remains runtime-export neutral. Properties → Viewport Snapping → Magnetic Vertices is one project-only undo/no-op-aware edit; the inclusive 8px radius is fixed and not serialized | PASS |
| Candidate and resolver | The UI-free resolver covers inclusive 8px Euclidean matching, magnetic-before-grid precedence, exact distance and `(slot, optional resolved skin, displayed attachment, vertex)` tie order. Shell coverage includes selected exclusion, duplicate attachment names across skins, cross-slot meshes, positive-alpha gating, per-update on-canvas filtering, same-gesture zoom reprojection, and offscreen-to-onscreen candidate admission | PASS |
| Gesture and lifecycle | Snapping uses the pressed selected vertex as anchor and applies one common world delta through frozen single/weighted inverses. Live Alt bypass and Cmd/Ctrl temporary enable do not enter settings/history; grid fallback, linked child-to-immediate-parent targeting, one commit/undo/redo, click/no-movement, snap-return-to-start, cancel, refresh failure, and atomic rollback remain covered | PASS |
| UI and compatibility | Magnetic and grid previews use transient cyan/gold guides from the raw pressed-anchor target. The display-enabled suites exercise the editor UI path without adding a manual visual-support claim. `.mskl` v1, `.mbin` v2, C ABI v1, `SelectionSet`, GPU ownership, and the 56-operation Agent/MCP surface remain unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build && cmake --build build -j4` -> all default targets built
- `./build/marrow_viewport_interaction_tests` -> inclusive boundary, precedence, stable slot/skin/attachment/vertex ties, live activation/bypass, grid fallback, and invalid-math tests passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> absent/MAR-165-shaped default-off compatibility, strict boolean validation, unknown retention, filesystem save/reload, and runtime-document export neutrality passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> candidate visibility/identity, selected exclusion, same-gesture layout reprojection, group/weighted/linked snapping, modifiers, guide origin, grid fallback, cancel/no-op, save/reload, and undo/redo smoke passed
- `ctest --test-dir build --output-on-failure` -> 20/20 passed
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j4 && ctest --test-dir build-display --output-on-failure` -> Debug display-enabled suite 23/23 passed
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j4 && ctest --test-dir build-platform-release --output-on-failure` -> Release display-enabled suite 23/23 passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar166.mskl --export-binary /tmp/marrow_mar166.mbin` -> project/runtime export passed
- `./build/marrow_inspect --compare /tmp/marrow_mar166.mbin /tmp/marrow_mar166.mskl` -> match; `rotation_error=0.00274662deg`, `position_error=0.000811016px`
- `./build/marrow_fixture_smoke /tmp/marrow_mar166.mskl /tmp/player_idle.matl` -> exported runtime passed
- `./build/marrow_agent_dispatch_smoke`, `./build/marrow_agent_socket_tests`, `./build/marrow_c_smoke`, and MCP schema `py_compile` -> unchanged surfaces and transports passed
- `python3 -m json.tool assets/fixtures/player_idle.marrow > /dev/null`, `python3 -m json.tool .agents/tasks/prd-marrow-runtime.json > /dev/null`, `cmake --build build --target marrow_verify_third_party`, and `git diff --check` -> passed

This checkpoint adds no Windows qualification credit. MAR-192 through MAR-210 remain
open, and macOS/Windows support qualification remains governed by
`docs/root1/platform-validation.md`.

## MAR-165 Shared Project Viewport Transform Snapping Validation Results

Validated 2026-08-20. Animation-mode translate, rotate, and scale gestures now
share one UI-free snap kernel backed by optional project metadata. Existing
projects remain default-off, while transient gesture modifiers never enter the
project, runtime export, dirty state, or history.

| Slice | Verification | Result |
| --- | --- | --- |
| Project model | Optional top-level `.marrow.snap` stores independent world-grid/local-angle/absolute-scale enables and positive finite steps with defaults `10`/`15`/`0.1`; absent projects stay off without materialization, unknown nested fields survive, invalid load/save fails, and a valid filesystem save/reload preserves typed and unknown data | PASS |
| Gesture math | The common scalar primitive covers negative half-steps and live activation; translate snaps the absolute world target before reflected-parent inversion, rotate snaps the raw unnormalized multi-turn local angle, and scale preserves signed/exact-zero components plus the X-if-nonzero-else-Y uniform driver ratio/sign rule | PASS |
| Input, settings, and grid | Alt always bypasses; macOS Cmd/non-macOS Ctrl temporarily enables a disabled domain and is sampled live. A toggle creates one project-only undo, a numeric drag creates one coalesced project-only undo, a blocked drag continuation cannot mutate without its pending item, and the origin-anchored display grid uses only integer multiples of the configured world step | PASS |
| Transaction and compatibility | One successful gesture creates one undo and cancellation is exact. An off-grid scale click with no movement and orthogonal pointer movement on an axis-constrained translate create no key or history. The additive public `ProjectSnapSettings`/`ProjectData::snap_settings` model does not change runtime documents, `.mskl` v1, `.mbin` v2, C ABI v1, GPU resources, or the 56-operation Agent/MCP surface | PASS |

Validated commands and outputs:

- `cmake -S . -B build && cmake --build build -j4` -> all default targets built
- `./build/marrow_viewport_interaction_tests` -> scalar activation/quantization, negative midpoint, signed/exact-zero/uniform scale, invalid math, and visible-grid integer-multiple tests passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow` -> absent/default-off compatibility, strict validation, unknown-field retention, valid filesystem save/reload, and runtime-document export neutrality passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> project-only settings history and blocked-continuation rejection, reflected-parent/free/axis-constrained translation, raw rotation, signed/exact-zero scale, live modifiers, off-grid no-movement regressions, cancellation, and one-undo smoke passed
- `ctest --test-dir build --output-on-failure` -> 20/20 passed
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j4 && ctest --test-dir build-display --output-on-failure` -> Debug display-enabled suite 23/23 passed
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j4 && ctest --test-dir build-platform-release --output-on-failure` -> Release display-enabled suite 23/23 passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar165.mskl --export-binary /tmp/marrow_mar165.mbin` -> project/runtime export passed
- `./build/marrow_inspect --compare /tmp/marrow_mar165.mbin /tmp/marrow_mar165.mskl` -> match; `rotation_error=0.00274662deg`, `position_error=0.000811016px`
- `./build/marrow_fixture_smoke /tmp/marrow_mar165.mskl /tmp/player_idle.matl` -> exported runtime passed
- `./build/marrow_agent_dispatch_smoke`, `./build/marrow_agent_socket_tests`, `./build/marrow_c_smoke`, and MCP schema `py_compile` -> unchanged surfaces and transports passed
- `python3 -m json.tool assets/fixtures/player_idle.marrow > /dev/null`, `cmake --build build --target marrow_verify_third_party`, and `git diff --check` -> passed

This checkpoint adds no Windows qualification credit. MAR-192 through MAR-210 remain
open, and macOS/Windows support qualification remains governed by
`docs/root1/platform-validation.md`.

## MAR-164 Attachment-Local Multi-Vertex FFD Validation Results

Validated 2026-08-16. Animation mode now keeps a shell-private, persistent vertex
sub-selection under the exact active and currently displayed mesh Attachment. Selected
vertices move by one common world-space delta through frozen per-vertex inverses; this
does not add an entity `SelectionSet` group transform or persistent project state.

| Slice | Verification | Result |
| --- | --- | --- |
| Selection and input | Scope is `{slot, skin, displayed attachment, deform target, vertex count}` with sorted unique indices; plain point replace/selected-click collapse, macOS Cmd/non-macOS Ctrl toggle-only, forward/reverse inclusive plain/additive FFD boxes, empty semantics, 4px threshold, and FFD-box priority over the Bone box are covered | PASS |
| Atomic group solve | Single- and weighted-influence vertices use frozen inverses against one common world delta; selected pairs update from one gesture-start animation-only full vector while untouched pairs remain byte-equivalent, and duplicate/out-of-range/malformed/singular/non-finite members reject the complete candidate before mutation | PASS |
| Target and materialization | Normal and linked `deform=false` meshes target themselves; linked `deform=true` keeps the displayed child as selection scope while editing one immediate-parent timeline; parameter-composed positions drive handles without baking parameter output, and topology, UVs, and weights remain read-only | PASS |
| Transaction and lifecycle | Preflight completes before materialization; successful group drag creates one full-vector key and one undo entry, while click/no-movement, return-to-start, Escape, focus/context loss, refresh failure, and downstream override leave no partial candidate; selection survives time/animation/parameter preview/undo/redo and clears only on the documented context or successful-adoption boundaries | PASS |
| Persistence and compatibility | `.marrow` save/reload and exported `.mskl`/MBIN v2 preserve selected and untouched pairs, imported curves, and linked parent identity while vertex selection reloads empty; public editor API, `SelectionSet`, `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1, GPU resources, and the 56-operation Agent/MCP surface remain unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build && cmake --build build -j4` -> all default targets built
- `./build/marrow_viewport_interaction_tests` -> point replace/toggle, sorted unique selection, forward/reverse/inclusive box semantics, atomic group update, mixed inverses, untouched-pair preservation, and invalid-candidate rejection passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> selected-click drag/collapse, toggle-only, FFD box priority, mixed-influence common delta, isolated singular rejection, linked child-to-parent targeting, lifecycle, rollback, parameter non-bake, save/reload, and JSON/MBIN smoke passed
- `ctest --test-dir build --output-on-failure` -> 20/20 passed
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j4 && ctest --test-dir build-display --output-on-failure` -> Debug display-enabled suite 23/23 passed
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j4 && ctest --test-dir build-platform-release --output-on-failure` -> Release display-enabled suite 23/23 passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar164.mskl --export-binary /tmp/marrow_mar164.mbin` -> project/runtime export passed
- `./build/marrow_inspect --compare /tmp/marrow_mar164.mbin /tmp/marrow_mar164.mskl` -> match; `rotation_error=0.00274662deg`, `position_error=0.000811016px`
- `./build/marrow_fixture_smoke /tmp/marrow_mar164.mskl /tmp/player_idle.matl` -> exported runtime passed
- `./build/marrow_agent_dispatch_smoke`, `./build/marrow_agent_socket_tests`, `./build/marrow_c_smoke`, and MCP schema `py_compile` -> unchanged surfaces and transports passed
- `cmake --build build --target marrow_verify_third_party` and `git diff --check` -> passed

This checkpoint adds no Windows qualification credit. MAR-192 through MAR-210 remain
open, and macOS/Windows support qualification remains governed by
`docs/root1/platform-validation.md`.

## MAR-163 Single FFD Vertex Auto-Key Validation Results

Validated 2026-08-16. At this checkpoint, Animation mode exposed every vertex of the
exact active, currently displayed mesh Attachment as a fixed 6px hit target and a drag
edited only that vertex pair in one full animation-FFD vector. Persistent point/toggle/box
and multi-vertex behavior was deferred here and is completed by MAR-164 above.

| Slice | Verification | Result |
| --- | --- | --- |
| Handles and input | Final parameter-composed mesh positions drive the overlay; nearest distance and lower vertex index resolve ties; arbitration is active gesture/brush, translate, rotation, scale, FFD vertex, entity, then box | PASS |
| Frozen mapping | Single-influence and weighted `sum(weight * bone world 2x2)` inverses cover non-uniform and reflected transforms; empty, malformed, relative-singular, and non-finite mappings reject before mutation | PASS |
| Target and materialization | Normal meshes target themselves; linked `deform=true` meshes target their immediate parent while shared topology/weights remain read-only; the selected animation is sampled into a geometry-sized full vector and imported keys/curves materialize intact | PASS |
| Transaction and rollback | Playback pauses; only one X/Y pair changes from the gesture-start vector; click/no-movement creates no history, commit creates one undo entry, and Escape, focus/context loss, invalid payload, refresh failure, or downstream override cancel the whole transaction while preserving selection context | PASS |
| Persistence and compatibility | `.marrow` save/reload and exported `.mskl`/MBIN v2 preserve the full vector and parent target identity; public editor API, `SelectionSet`, `.marrow` schema, `.mskl` v1, C ABI v1, GPU resources, and the 56-operation Agent/MCP surface remain unchanged | PASS |

Validated commands and outputs:

- `cmake -S . -B build && cmake --build build -j4` -> all default targets built
- `./build/marrow_viewport_interaction_tests` -> nearest/tie, weighted/reflected/non-uniform inverse, full-vector isolation, invalid rejection, and press precedence passed
- `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` -> single/multi-influence, linked `warrior_body -> body_mesh`, sparse interpolation, no-op, undo/redo, rollback, save/reload, and JSON/MBIN smoke passed
- `ctest --test-dir build --output-on-failure` -> 20/20 passed
- `cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-display -j4 && ctest --test-dir build-display --output-on-failure` -> Debug display-enabled suite 23/23 passed
- `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON && cmake --build build-platform-release -j4 && ctest --test-dir build-platform-release --output-on-failure` -> Release display-enabled suite 23/23 passed
- `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar163.mskl --export-binary /tmp/marrow_mar163.mbin` -> project/runtime export passed
- `./build/marrow_inspect --compare /tmp/marrow_mar163.mbin /tmp/marrow_mar163.mskl` -> match; `rotation_error=0.00274662deg`, `position_error=0.000811016px`
- `./build/marrow_fixture_smoke /tmp/marrow_mar163.mskl /tmp/player_idle.matl` -> exported runtime passed
- `./build/marrow_agent_dispatch_smoke`, `./build/marrow_agent_socket_tests`, `./build/marrow_c_smoke`, and MCP schema `py_compile` -> unchanged surfaces and transports passed
- `cmake --build build --target marrow_verify_third_party` and `git diff --check` -> passed

This checkpoint adds no Windows qualification credit. MAR-192 through MAR-210 remain
open, and macOS/Windows support qualification remains governed by
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
