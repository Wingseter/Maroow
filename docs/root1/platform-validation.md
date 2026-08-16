# Marrow local platform validation

This document is the evidence authority for MAR-192–210. A source change,
successful compile, synthetic test, real display test, physical pen test, and
portable-artifact run are different claims. Empty cells and `NOT RUN` are not
PASS and do not satisfy a dependency.

## Qualification status

| Host | Required path | Current evidence | Status |
|---|---|---|---|
| macOS arm64 | SDL3 Metal + Sokol + sokol_imgui | Task #28 Debug and Release 23/23 suites: 20 noninteractive tests plus three real-display tests, Metal RGBA8 readback, 20 device and 100 resource lifecycles | PARTIAL — manual focus/IME and fixed A/B performance remain open |
| Ubuntu/Linux x64 | Retained SDL3 X11 GLCORE 4.1 implementation | No qualification run; excluded from the current support matrix by the 2026-08-12 scope decision | NOT REQUIRED — unqualified, no support claim |
| Windows 10 x64 | Retained Windows x64 implementation | No qualification run; excluded from the current support matrix by the 2026-08-12 scope decision | NOT REQUIRED — unqualified, no support claim |
| Windows 11 x64, VS2022 | SDL3 GLCORE 4.1 + Windows Ink; 100% and 150% or 200% DPI | Task #28 Debug and Release build plus 23/23 tests at 100% scale, including three display tests; same-host portable extraction/run | PARTIAL — high-DPI/manual desktop and physical Ink evidence remain open |

MAR-192 through MAR-210 remain `open` as a parallel deferred qualification
backlog and do not block the completed MAR-163/MAR-164 checkpoints or the next MAR-165
product milestone. Current qualification targets macOS arm64 and Windows 11 x64 only.
Ubuntu/Linux, Windows 10, Wayland, Windows ARM64, D3D/Vulkan, installer, and
ImGui OS multi-viewport support are not claimed.

The same 2026-08-12 scope decision accepts extraction into a new directory on
the qualified Windows 11 host as the current portable package gate. A separate
clean PC is `NOT REQUIRED`; this waiver does not create clean-machine evidence.

## Task #28 automated regression refresh

Recorded 2026-08-16 in Asia/Seoul against the clean implementation revision
`ea8931a6cec00766285abd7144a8c94b50a60e7d` on both hosts.

- macOS default CTest passed 20/20. Debug and Release display-enabled suites
  each passed 23/23 in the real host session, including loopback socket and the
  three SDL/Metal display tests. Vendored hashes, C ABI v1, all 56 Agent
  operations, project export/runtime load, JSON/MBIN v2 comparison, renderer
  link closure, and live editor/MCP socket validation passed.
- Windows 11 VS2022 x64 Debug and Release default builds completed and each
  full display-enabled CTest passed 23/23. The Windows vendored dependency/hash
  target also passed.
- The Release portable stage produced 119 canonical manifest entries; an
  independent PowerShell SHA-256 pass found zero mismatches. The
  5,118,631-byte `Marrow-Release-x64.zip` independently matched its generated
  SHA-256, `f0c24473e21f7f5b9efda18df48c5e42339a6d0f4fe6a0a4bb57cefd38238d9c`.
  Both the staged folder and a new-directory extraction at
  `E:\Workspace\2026\Maroow-portable-check-ea8931a-20260816\Marrow`
  completed the two-frame editor/project run with exit code 0.

This refresh closes only the Task #28 automated refactor gate. It does not run
or satisfy Windows high-DPI manual UI, physical Windows Ink, or fixed
legacy/Sokol A/B evidence. MAR-192 through MAR-210 therefore remain `open`, and
macOS/Windows qualification remains `PARTIAL`.

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
- zlib: `1.3.2`, archive SHA-256
  `bb329a0a2cd0274d05519d61c667c062e06990d72e125ee2dfa8de64f0119d16`
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

## Windows 11 x64 evidence — 2026-08-12

This is one Windows 11 host run at 100% reported display scale. It advances the
Windows build, automated display, and current same-host package gate, but it is
not high-DPI/manual desktop or physical Windows Ink qualification. Windows 10
and a separate clean-machine portable run are `NOT REQUIRED`, not inferred
passes.

### Host

| Field | Value |
|---|---|
| OS | Microsoft Windows 11 Pro x64, `10.0.22621.4317` |
| Host/CPU | `KWON`; AMD Ryzen 9 3900X, 12 cores / 24 logical processors |
| GPU inventory | NVIDIA GeForce RTX 5090, driver `32.0.15.9186`; SudoMaker Virtual Display Adapter `1.10.9.289` also installed |
| Memory | 34,305,380,352 bytes reported physical memory |
| Visual Studio | VS2022 Community `17.14.36811.4`; MSVC `19.44.35222`, toolset `14.44.35207` |
| CMake | `3.31.6-msvc6` from the VS2022 installation |
| Git | Git for Windows `2.45.1.windows.1`; Git LFS `3.5.1` |
| Source | `agent-control-remaining` at `db104a3` |
| Build root | `E:\Workspace\2026\Maroow\build-msvc` |
| Display session | SSH service session; SDL reported logical/drawable `640x480`, framebuffer/content scale `1x1` / `1` |
| GL surface | SDL OpenGL 4.1 core request; RGBA8 (`sg` format 23), depth-stencil (`sg` format 44), 4x MSAA |

The adapter inventory is recorded, but the current smoke does not name which
adapter serviced the GL context. No inference assigns this run specifically to
the RTX 5090 or the virtual display adapter.

### Commands and results

| Command | Exit/result | Evidence |
|---|---|---|
| `set GIT_CLONE_PROTECTION_ACTIVE=false&& git clone --branch agent-control-remaining --single-branch https://github.com/Wingseter/Maroow.git E:\Workspace\2026\Maroow-fresh-db104a3` | 0; compatibility flag rationale described below | Fresh clone at `db104a3`, clean status, `git lfs fsck OK` |
| `cmake -S . -B build-msvc -G "Visual Studio 17 2022" -A x64 -DMARROW_ENABLE_DISPLAY_TESTS=ON` | 0 | VS2022 x64 multi-config tree generated |
| `cmake --build build-msvc --config Debug --parallel 4` | 0 | All Debug default targets, including `marrow_editor_shell.exe`, built |
| `ctest --test-dir build-msvc -C Debug --output-on-failure` | 0, 20/20 | `build-msvc\validation\2026-08-12\ctest-debug.log` |
| `cmake --build build-msvc --config Release --parallel 4` | 0 | All Release default targets built |
| `ctest --test-dir build-msvc -C Release --output-on-failure` | 0, 20/20 | `build-msvc\validation\2026-08-12\ctest-release.log` |
| `cmake --build build-msvc --config Debug --target marrow_verify_third_party` | 0 | Pinned SDL3, zlib, Dear ImGui, Sokol, patched sokol_imgui, shader, and tool hashes verified on Windows |
| `cmake --build build-msvc --config Release --target marrow_portable_stage` | 0 | Portable folder, canonical manifest, ZIP, and ZIP SHA-256 generated |
| `marrow_editor_shell.exe --project assets\fixtures\player_idle.marrow --auto-close 2` from the staged folder | 0 | Same-host staged-folder run |
| The same command from `E:\Workspace\2026\Maroow-portable-check-8666edd\Marrow` after ZIP extraction | 0 | Same-host new-directory extraction/run; satisfies the current package gate |

Durable CTest log SHA-256 values:

- Debug: `4d05a1d13df60db3415f991f97312343e4439b1bd999768a545b980449233f1d`
- Release: `0fd3b30b7415c6f2ff5f4d70cc00b97215e75278b6aa97ca688debba11d90afa`

The three display tests passed in both configurations:

- `marrow.window_host_smoke`: 20 SDL/OpenGL host lifecycles; logical and
  drawable `640x480`, scale `1`, RGBA8 + depth-stencil, 4x MSAA.
- `marrow.gpu_parity_smoke`: top-left, center, and bottom-right RGBA8 probes
  within `2/255`, plus 20 device and 100 resource lifecycles.
- `marrow.editor_display_smoke`: the actual editor completed 20 offscreen
  viewport/main-pass/commit/present frames.

The Release portable folder contains 119 hash-manifest entries; an independent
PowerShell pass reported zero mismatches. `Marrow-Release-x64.zip` is 5,112,709
bytes and its generated plus independently checked SHA-256 is
`bca86a87185e6a3c909a43e51e3382aed39b7980c394f375462717bee881c77f`.

Git for Windows 2.45.1 rejected the standard Git LFS `post-checkout` hook while
clone protection was active. The hook was inspected as the normal
`git lfs post-checkout` shim, so protection was disabled only for the fresh
clone command. The repository now stores the 28 affected PNG/PSD fixtures as
real LFS pointers; both the existing checkout and fresh clone pass
`git lfs fsck`, and the smudged `player_fixture.png` SHA-256 remains
`6056c2fa515cdcd7fd39c447532901e5a775d697aa9ace292ecf8e22f7c25455`.

### Debugging readiness

The generated `Marrow.sln` is present. Debug output includes a 20,665,344-byte
`marrow_editor_shell.exe` and a 56,995,840-byte
`marrow_editor_shell.pdb`; the generated project uses
`ProgramDatabase` compile information and `GenerateDebugInformation=true`.
VS2022 `devenv.exe` and the x64 `msvsmon.exe` remote debugger are installed.
This establishes source-level debug-capable artifacts, but no interactive
breakpoint/attach session was performed through the noninteractive SSH run.

## Windows, pen, and performance evidence still required

- Windows 11 at 150% or 200% DPI plus manual Korean IME, focus, clipboard,
  cursor, picking, minimize/restore, and driver evidence. The automated run
  above reported 100% scale only.
- Windows Ink device name/driver/SDL pen IDs, three pressure levels,
  tilt/eraser metadata, focus/proximity cancellation, mouse parity, and one
  changed stroke/one undo.
- Fixed legacy/Sokol A/B performance and the remaining manual resource and
  pixel-oracle evidence.

Those remaining rows still require exact commands, exit status,
hardware/driver, display scale/server, and durable log or artifact paths before
their stories or MAR-210 can move to `done`.
