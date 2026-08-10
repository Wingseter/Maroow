# Third-party provenance

This file is the single dependency ledger for the source-vendored platform and
renderer stack. Configure and build do not fetch from the network. Run
`cmake --build build --target marrow_verify_third_party` to verify the pinned
files used by the build.

## SDL3

- Upstream: <https://github.com/libsdl-org/SDL>
- Release: `release-3.4.14`
- Commit: `147a8ee32dbf9ac02f3794964490687b6bbda1bc`
- Source archive: <https://github.com/libsdl-org/SDL/archive/refs/tags/release-3.4.14.tar.gz>
- Archive SHA-256: `9d57b178fb297e121ef2605275937b7afaa7cd24d99ce1f95953e69e7a2535d6`
- Final local tree manifest SHA-256: `c557be1ba28ec0a69f4acb06d3e95cdc45808813e7eb99c4f7dbca64ac3695f0`
- Local path: `external/sdl3`
- Build policy: static only, tests/examples/install/docs disabled,
  `EXCLUDE_FROM_ALL`; production consumers link `SDL3::SDL3` only. Upstream
  `.github/workflows` metadata is excluded so this repository does not add
  hosted CI.
- License: zlib, copied to `licenses/SDL3.txt`.

## Dear ImGui

- Upstream: <https://github.com/ocornut/imgui>
- Release line: `v1.92.6-docking`
- Commit: `2a1b69f05748ad909f03acf4533447cac1331611`
- Source archive: <https://github.com/ocornut/imgui/archive/2a1b69f05748ad909f03acf4533447cac1331611.tar.gz>
- Archive SHA-256: `f4bcfa9e65c94b15504f17052c824bf8630fbf840a4b2e5905828b00dc4a053d`
- Upstream full-snapshot manifest SHA-256: `21cb9ebcd78287f62dcbb816dce425ec6174c846b1d71d0aa7a5c18ce3469f3d`
- Final local tree manifest SHA-256: `d8489dd12a5c808d12d7c27bbc6a89916980c7d98250f9d86328477847b3e48c`
- Local path: `external/imgui`
- Snapshot policy: core, internal headers, and all transition backends came
  from the same full snapshot. After SDL/Sokol cutover, the GLFW and OpenGL3
  backend source/header files were removed. Upstream `.github/workflows`
  metadata is also excluded so this repository does not add hosted CI. The
  final production tree retains the SDL3 platform backend and official
  `sokol_imgui` renderer backend only.
- License: MIT, copied to `licenses/Dear-ImGui.txt`.

## Sokol headers and sokol_imgui patch

- Upstream: <https://github.com/floooh/sokol>
- Core/base commit: `31d8a3fce5f85db03b66a8db7c4bd73fce55b8e4`
- Core file SHA-256:
  - `sokol_gfx.h`: `99044f0a719eb98e8e85ce5a8b48b0b41c0cdf08b22c02bffc0545d5915537f6`
  - `sokol_app.h`: `0e8266a9494ed9aed414ac52fc6e36543cac801afa220766a5dcc4d2c4c2c0eb`
  - `sokol_glue.h`: `5fe1ab5a9ab0b7dc8761e1da151cacafea338421d3799745067e7421e28af670`
  - `sokol_log.h`: `b1bf1403c738b8f18b09819d9190ac160298dd590440c9df57cb9d90b77eb29d`
- `util/sokol_imgui.h` base SHA-256:
  `7d92d01906721154ef32117e616a9de60e729c71ebb4363b01f0259e72c5c6aa`
- Local patch source commit:
  `59f0433236d483444d0f313cda6702fd47ea323e`
- Patch file: `external/patches/sokol_imgui-59f0433-shutdown-debug-group.patch`
- Patch file SHA-256:
  `cde046c5d93cb77d7584db51ddf866671be702c67ff0cfadd992dc78d07baaa7`
- Patch scope: move the shutdown `sg_push_debug_group("sokol-imgui")`
  before resource destruction. No later generated shader or API changes are
  carried.
- Patched `external/sokol_imgui.h` SHA-256:
  `dcad7d55e3a14a8adaf0400bfea836697abc5e9135e6b17b2806116797b25082`
- Validation: C++17 syntax target plus a validation-enabled dummy backend
  `setup -> one frame -> shutdown` test.
- License: zlib/libpng, copied to `licenses/Sokol.txt`.

## sokol-shdc

- Binary upstream: <https://github.com/floooh/sokol-tools-bin>
- Binary commit: `03138cef005bc75ef047998a8784b93360486d00`
- Corresponding source: <https://github.com/floooh/sokol-tools>
- Source commit: `4b52ef96db8a1764b160a8b21864d40dd4309905`
- Binary SHA-256:
  - macOS: `a284a4fe969458e79ba464ab0abf8c46924b563838c2d288f9141c5f791dc633`
  - Linux: `74adb2aa9e20708654b502931be8758cee29b0e39a905fd1165b53f061a29d2b`
  - Windows x64: `c26665c3ddb4d6abc199fadd078689a12f9a7857193e1811ab19e5e467496f77`
- Generated shader policy: `marrow_renderer_shader_metal.h` is generated
  only with `metal_macos`; `marrow_renderer_shader_gl.h` is generated only
  with `glsl410`. Windows and Linux therefore cannot overwrite the committed
  Metal payload. Current generated file SHA-256 values are:
  - Metal: `ebb385888764d038831153e72d1aa21061e762021dc2ca2239ed904cca1e058e`
  - GLCORE 4.1: `5da815aaf7b3b73299cae81ef966440429d4362e976350f2b28d37d3b277e31e`
- License: zlib/libpng, copied to `licenses/sokol-tools-bin.txt`.

## Tree-manifest hash procedure

The upstream and final local tree hashes above were calculated at the labeled
snapshot state with the following command, replacing `<tree>` with the local
vendor directory:

```sh
find <tree> -type f -print0 | LC_ALL=C sort -z | xargs -0 shasum -a 256 | shasum -a 256
```
