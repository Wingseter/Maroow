# Marrow local platform validation

This document is the evidence authority for MAR-192–210. A source change,
successful compile, synthetic test, real display test, physical pen test, and
portable-artifact run are different claims. Empty cells and `NOT RUN` are not
PASS and do not satisfy a dependency.

## Qualification status

| Host | Required path | Current evidence | Status |
|---|---|---|---|
| macOS arm64 | SDL3 Metal + Sokol + sokol_imgui | Build, 17 noninteractive tests, three real-display tests, Metal RGBA8 readback, 20 device and 100 resource lifecycles | PARTIAL — manual focus/IME, fixed A/B performance, and physical pen not yet recorded |
| Ubuntu 24.04 x64 X11, GCC 13 | SDL3 GLCORE 4.1 | No physical/VM host run in this checkout | NOT RUN |
| Windows 10 22H2 x64, VS2022, 100% DPI | SDL3 GLCORE 4.1 | Source/CMake path implemented; no Windows build or display run | NOT RUN |
| Windows 11 x64, VS2022, 150% or 200% DPI | SDL3 GLCORE 4.1 + Windows Ink | Source/CMake path implemented; no Windows build, display, physical pen, or portable run | NOT RUN |

MAR-192 through MAR-210 therefore remain `open`, and MAR-163 remains blocked by
MAR-210. This checkout does not claim Wayland, Windows ARM64, D3D/Vulkan,
installer, or ImGui OS multi-viewport support.

## Source and dependency identity

Recorded 2026-08-09 in Asia/Seoul.

- Branch: `agent-control-remaining`
- Base commit: `0e0539e633e9a5227fd44e9e003cda68d27fc40f`
- Worktree: dirty. It already contained the MAR-158–162 changes and the
  untracked research note before this platform implementation; no reset or
  checkout was performed.
- Pre-platform tracked binary diff SHA-256:
  `d15ae2de2f4a85c2285a9988deee151b03ff1f775f5883f56d1a84e4fc5314e6`
- Pre-platform untracked research SHA-256:
  `dcd6f1b718016467941b8f5fab62991d2ad128cebec4f60635dcd742109cac41`
- SDL3: `release-3.4.14`, commit
  `147a8ee32dbf9ac02f3794964490687b6bbda1bc`
- Dear ImGui: `v1.92.6-docking`, commit
  `2a1b69f05748ad909f03acf4533447cac1331611`
- Sokol core: `31d8a3fce5f85db03b66a8db7c4bd73fce55b8e4`
- Patched `sokol_imgui.h` SHA-256:
  `dcad7d55e3a14a8adaf0400bfea836697abc5e9135e6b17b2806116797b25082`
- sokol-shdc binary commit:
  `03138cef005bc75ef047998a8784b93360486d00`
- Generated Metal header SHA-256:
  `ebb385888764d038831153e72d1aa21061e762021dc2ca2239ed904cca1e058e`
- Generated GLCORE header SHA-256:
  `5da815aaf7b3b73299cae81ef966440429d4362e976350f2b28d37d3b277e31e`
- Complete archive, tree, license, patch, and tool provenance is in
  `THIRD_PARTY.md` and is executable through `marrow_verify_third_party`.

## macOS arm64 evidence — 2026-08-09

### Host

| Field | Value |
|---|---|
| OS | macOS 26.5.1, build 25F80 |
| Model/CPU | MacBook Pro Mac14,9, Apple M2 Pro, 10 CPU cores |
| GPU | Apple M2 Pro, 16 GPU cores, Metal supported |
| Memory | 16 GB |
| Compiler | Apple clang 21.0.0 (`clang-2100.1.1.101`) |
| CMake | 4.3.2 |
| Window path | SDL3 Metal, high-pixel-density requested |
| Display metrics | logical 640x480, drawable 1280x960, framebuffer/content scale 2x2 (Retina) |
| Main surface | BGRA8 (`sg` format 28), depth-stencil (`sg` format 44), sample count 4 |

Hardware serial numbers and device identifiers are intentionally not retained.

### Commands and results

| Command | Exit/result | Evidence |
|---|---|---|
| `cmake -S . -B build` | 0 | configure output; SDL static ON, shared OFF |
| `cmake --build build -j4` | 0 | all default targets built |
| `cmake --build build --target marrow_verify_third_party` | 0 | all pinned hashes and removed-backend guards passed |
| `ctest --test-dir build --output-on-failure` | 0, 17/17 | superseded by the display-enabled full log below |
| `cmake -S . -B build-display -DMARROW_ENABLE_DISPLAY_TESTS=ON` | 0 | display registry enabled explicitly |
| `cmake --build build-display` | 0 | display targets built |
| `ctest --test-dir build-display --output-on-failure -L display` | 0, 3/3 | followed by the complete run below |
| `ctest --test-dir build-display --output-on-failure` | 0, 20/20 | `platform-validation-logs/2026-08-09-macos-arm64-debug-display.log` |
| `cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON` | 0 | clean Release configure |
| `cmake --build build-platform-release -j4` | 0 | all Release targets built |
| `ctest --test-dir build-platform-release --output-on-failure` | 0, 20/20 | `platform-validation-logs/2026-08-09-macos-arm64-release-display.log` |
| `./build/marrow_editor_shell --verify-launch-focus` | 0 | SDL high-DPI window plus AppKit and process activation policy both verified `Regular` |

Durable log SHA-256 values:

- Debug/display: `f02d9a80d0d447ef4caf2ba6a09eb0835a810b337e487cea8d8e316b17084d10`
- Release/display: `093e4fc96ae0821f8504b2247dbb9cfe45dfd472ede1014ad64a08e366228d36`

The three display tests mean:

- `marrow.window_host_smoke`: a real SDL Metal surface over 20 complete host
  lifecycles, including logical/drawable metrics and swapchain format/sample
  reporting.
- `marrow.gpu_parity_smoke`: a deterministic 1x RGBA8 offscreen clear read
  through test-only `sg_mtl_query_image_info` and the Sokol command queue.
  Top-left corner, center, and bottom-right match within `2/255`; the test runs
  20 `sg_setup`/`sg_shutdown` lifecycles and 100 image/view resource lifecycles.
- `marrow.editor_display_smoke`: the actual SDL/Metal editor executes 20 frames
  through offscreen viewport, main `sokol_imgui` swapchain pass, `sg_commit`,
  and present.

Additional checked boundaries:

- Default `--auto-close` remains the historical headless editor business
  smoke and is not counted as display evidence.
- `nm` reports zero `sapp_*`/`sglue_*` symbols in
  `marrow_editor_shell`; `marrow_renderer_sample` retains the standalone
  Sokol-app adapter.
- The editor shell links `marrow_renderer_core` directly; only the standalone
  compatibility renderer links `marrow_renderer_sapp_host`.
- `otool -L marrow_editor_shell` contains Metal/SDL platform frameworks and no
  OpenGL framework or GLFW library.
- Production `src/editor` has no GLFW, CGL, direct GL, `GLuint`, or ImGui
  OpenGL3 call. Native backend readback exists only below `src/tests`.

### Evidence still required on macOS

- Manual Cmd-Tab observation and Korean preedit/commit observation. The
  automated `--verify-launch-focus` policy check passes, but is not a manual
  focus-switch recording.
- A physical pressure-capable pen/tablet run if macOS pen behavior is used as
  supplementary evidence (Windows 11 hardware remains mandatory for MAR-208).
- Fixed 1440x900, vsync-off, 60-frame warmup plus 600-frame x5 legacy/Sokol
  median and p95 A/B evidence. The threshold is not waived because the legacy
  baseline is unavailable in this dirty checkout.
- Picking/overlay one-physical-pixel oracle beyond the deterministic clear
  probe and a measured live-resource counter baseline after 100 resizes/reloads.

## Ubuntu, Windows, pen, and package evidence

No claims below have been executed in this checkout:

- Ubuntu 24.04 X11 GCC 13 clean build, OpenGL 4.1 context, display/pixel/
  lifecycle/performance matrix.
- Windows 10 22H2 and Windows 11 VS2022 x64 Debug/Release build and complete
  test matrix.
- Windows DPI 100% plus 150% or 200%, Korean IME, focus, clipboard, cursor,
  picking, minimize/restore, and driver evidence.
- Windows Ink device name/driver/SDL pen IDs, three pressure levels,
  tilt/eraser metadata, focus/proximity cancellation, mouse parity, and one
  changed stroke/one undo.
- `marrow_portable_stage`, canonical folder manifest, transport ZIP SHA-256,
  and clean Win10/Win11 extraction/run.

Those rows must be appended with exact commands, exit status, hardware/driver,
display scale/server, and durable log or artifact paths before their stories or
MAR-210 can move to `done`.
