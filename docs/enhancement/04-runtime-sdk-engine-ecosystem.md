# 04. Runtime SDK 및 엔진 생태계 확장 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Maroow를 "에디터가 만든 파일을 molga-engine에서 재생하는 런타임"에서 한 단계 넓혀, 안정적인 런타임 SDK와 엔진별 통합 계층을 제공하는 배포 가능한 생태계로 확장한다.

**Architecture:** `SkeletonData`, `Skeleton`, `AnimationState`, `PreparedScene`, `RenderCommandList`, C API를 안정 계약으로 문서화하고, 엔진별 통합은 thin wrapper와 sample app으로 분리한다. 런타임 포맷 versioning과 ABI compatibility를 SDK packaging의 중심 제약으로 둔다.

**Tech Stack:** C++17 runtime, C ABI, CMake install/export packages, sokol/OpenGL renderer paths, optional Web/WASM build, Unity/Web/Godot/Unreal bridge prototypes.

---

## 목표

Maroow를 "에디터가 만든 파일을 molga-engine에서 재생하는 런타임"에서 한 단계 넓혀, 안정적인 런타임 SDK와 엔진별 통합 계층을 제공하는 배포 가능한 생태계로 확장한다. 1차 목표는 molga-engine first-class 통합을 완성하고, 같은 코어 런타임을 C/C++/Unity/Web에서 검증 가능한 형태로 포장하는 것이다.

경쟁 기준은 Spine/Live2D처럼 여러 엔진과 플랫폼에서 같은 자산을 일관되게 로드, 재생, 렌더링할 수 있는 SDK 경험이다. 다만 이 문서는 에디터 UX나 Live2D식 모델링 기능을 다루지 않고, 런타임/SDK/패키징/엔진 통합만 다룬다.

## 현재 상태와 근거

- 런타임 데이터 모델은 이미 엔진 통합에 적합한 분리를 갖고 있다. `docs/root1/concepts.md`는 `.mskl`/`.mbin`에서 `SkeletonData`를 만들고, 인스턴스별 `Skeleton`과 `AnimationState`를 구동한 뒤 `PreparedScene` 또는 `RenderCommandList`로 렌더러에 넘기는 흐름을 설명한다.
- 런타임 파일 포맷은 `.mskl`, `.mbin`, `.matl`, `.marrow`로 구분되어 있다. `docs/root1/format-spec.md`는 `.mskl`/`.matl`의 `version: 1`, `.mbin`의 `MBIN`/`AKEY` 바이너리 구조, `.marrow`가 에디터 전용 소스 포맷이라는 점을 문서화한다.
- C API는 이미 존재한다. `include/marrow/marrow_c.h`는 `MARROW_C_ABI_VERSION`, opaque handle, 상태 코드, allocator callback, animation event listener, render command 구조를 제공하고, 구현은 `src/c_api/marrow_c.cpp`에 있다.
- 검증용 실행 파일과 샘플이 있다. `CMakeLists.txt`는 `marrow_c_smoke`, `marrow_fixture_smoke`, `marrow_renderer_sample`, `marrow_benchmark`, `marrow_thread_stress`, `marrow_inspect` 타깃을 정의한다.
- 공유 `SkeletonData` 동시성 모델은 별도 스트레스 테스트로 다뤄지고 있다. `src/tests/runtime_thread_stress.cpp`는 immutable shared data와 per-instance mutable state 패턴을 검증하는 기준점이다.
- 엔진 통합 방향의 선행 조사가 있다. `docs/root1/research-engine-integration.md`는 Spine식 "generic runtime + engine-specific wrapper" 구조, C API/FFI 패턴, allocator hook, hot-reload, thread-safe update contract를 정리한다.
- 프로젝트 원안은 molga-engine 전용 툴체인이다. `docs/root1/discription.md`는 `Marrow Editor -> .mskl/.matl/.png -> molga-engine Runtime` 흐름과 C++/sokol 기반 런타임 방향을 명시한다.

## 범위 경계

이 계획에 포함한다:

- 런타임 ABI/API 안정화
- C API hardening 및 C++ SDK 패키징
- 엔진별 thin wrapper와 샘플 앱
- Web/WASM viewer/runtime
- conformance test, benchmark suite, CI matrix
- molga-engine first-class 통합

이 계획에서 제외한다:

- 에디터 레이아웃, 타임라인, 인스펙터, 페인팅 등 UX 개선
- Live2D식 파라미터 모델링, 메쉬 모델링, 얼굴/표정 제작 워크플로
- 새 애니메이션 저작 기능
- Spine importer 자체의 표현력 확장
- 렌더러 비주얼 품질 개선만을 목적으로 한 셰이더/이펙트 작업

## 목표 통합 계층

### 1. C API hardening

- `include/marrow/marrow_c.h`를 FFI 안정 경계로 고정한다.
- 예외가 C 경계를 넘지 않도록 모든 entry point의 실패 동작을 명문화한다.
- handle 소유권, borrowed string 수명, thread-local error message, allocator callback의 호출 규약을 문서화한다.
- ABI 버전 체크를 package smoke와 CI에 포함한다.

### 2. C++ SDK 패키징

- `include/marrow/runtime`, `include/marrow/renderer`, `include/marrow/marrow_c.h`를 배포 헤더 세트로 정리한다.
- CMake package config, install target, version header, sample CMake project를 제공한다.
- runtime-only, renderer-command-only, sokol sample backend를 분리해 엔진이 필요한 깊이만 통합할 수 있게 한다.

### 3. Unity bridge

- 1차는 native plugin + C# P/Invoke wrapper로 시작한다.
- Unity 컴포넌트는 `SkeletonDataAsset`, `MarrowSkeletonAnimation` 수준의 얇은 wrapper를 목표로 한다.
- Unity `Mesh` 갱신 경로는 `RenderCommandList`를 소비하는 방식으로 만든다.
- 에디터 importer는 `.mskl`/`.mbin`/`.matl` 파일을 Unity asset으로 묶는 최소 기능만 포함한다.

### 4. Web/WASM viewer/runtime

- Emscripten 빌드로 runtime + renderer-command builder를 WASM 모듈로 노출한다.
- 브라우저 viewer는 `.mskl`/`.mbin` + `.matl` + texture를 로드하고 animation 선택, play/pause, bounds/debug overlay를 제공한다.
- WebGL/WebGPU 렌더러는 첫 단계에서 샘플 구현으로 두고, core runtime과 command schema 안정성을 우선한다.

### 5. Unreal/Godot feasibility

- Unreal은 `UActorComponent`/`UAsset`/ProceduralMesh 경로의 feasibility spike까지만 1차 범위로 둔다.
- Godot은 GDExtension 기반 `Node2D` wrapper feasibility spike까지만 1차 범위로 둔다.
- 두 엔진 모두 정식 지원은 C API와 C++ package가 안정화된 뒤 결정한다.

### 6. molga-engine first-class integration

- molga-engine을 가장 먼저 지원하는 기준 엔진으로 둔다.
- asset loading, texture binding, tick/update, renderer command submission, hot-reload, benchmark gate를 molga-engine 샘플에 연결한다.
- 다른 엔진 wrapper가 따라야 할 reference integration으로 문서화한다.

## 런타임 산출물

- 안정 ABI: `MARROW_C_ABI_VERSION`, ABI compatibility policy, symbol export policy, C struct 확장 규칙
- semantic versioning: runtime API, file format, package version의 호환성 정책
- package layout: `include/`, `lib/`, `bin/`, `share/marrow/cmake/`, `samples/`, `licenses/`
- sample apps: C smoke, C++ playback, renderer command dump, molga-engine sample, Unity sample, Web viewer
- conformance tests: 같은 fixture를 C++, C API, Unity, WASM에서 로드/재생/이벤트/렌더 명령까지 비교
- viewer: 런타임 자산을 빠르게 열어볼 수 있는 standalone 또는 Web viewer
- benchmark suite: load time, update time, render command build time, memory footprint, multi-instance stress

## 단계별 계획

### Phase 0. 계약 문서화 및 기준 고정

- [ ] C API 소유권/수명/threading/error 규약을 `docs/root1` 또는 SDK 문서에 추가한다.
- [ ] `.mskl`/`.mbin`/`.matl` runtime compatibility policy를 `docs/root1/format-spec.md`와 연결한다.
- [ ] `SkeletonData` 공유, `Skeleton`/`AnimationState` 단일 스레드 소유 규약을 SDK 문서의 필수 계약으로 승격한다.
- [ ] 현재 샘플/검증 명령을 SDK release checklist로 정리한다.

### Phase 1. C API 안정화

- [ ] `marrow_c_smoke`를 ABI mismatch, allocator callback, binary load, event listener, render command traversal까지 명시적으로 분리한다.
- [ ] 모든 C API entry point에 null argument, out-of-range, parse failure, internal exception test를 추가한다.
- [ ] C struct 확장을 위한 `size` field 또는 reserved field 정책을 결정한다.
- [ ] public header에서 platform-specific type 노출을 제거하거나 격리한다.
- [ ] C ABI symbol visibility/export map을 추가한다.

### Phase 2. C++ SDK 패키지

- [ ] `cmake --install` 가능한 install target을 추가한다.
- [ ] `MarrowConfig.cmake`, `MarrowTargets.cmake`, version file을 생성한다.
- [ ] external sample project가 `find_package(Marrow CONFIG REQUIRED)`로 빌드되는지 검증한다.
- [ ] runtime-only와 renderer sample backend의 의존성을 분리한다.
- [ ] release archive에 headers, libs, samples, fixtures, license metadata를 포함한다.

### Phase 3. molga-engine reference integration

- [ ] molga-engine asset loader가 `.mskl`/`.mbin`/`.matl`을 로드하는 vertical slice를 만든다.
- [ ] shared `SkeletonData` cache와 per-entity `Skeleton`/`AnimationState` lifecycle을 연결한다.
- [ ] `RenderCommandList`를 molga-engine renderer submission으로 변환한다.
- [ ] runtime hot-reload 시 old shared data invalidation과 instance restore 정책을 검증한다.
- [ ] molga-engine 샘플을 release benchmark와 smoke test에 포함한다.

### Phase 4. Web/WASM viewer

- [ ] Emscripten toolchain file과 runtime-only WASM build target을 추가한다.
- [ ] C API 또는 얇은 JS binding으로 skeleton load/update/event/render-command 조회를 노출한다.
- [ ] `.mskl`/`.mbin`/`.matl` + texture drag-and-drop viewer를 만든다.
- [ ] WebGL 렌더 샘플에서 region, mesh, clipping, blend mode의 최소 렌더 경로를 검증한다.
- [ ] 브라우저 conformance fixture를 native output과 비교한다.

### Phase 5. Unity bridge

- [ ] native library를 Unity plugin layout으로 패키징한다.
- [ ] C# P/Invoke binding과 SafeHandle wrapper를 작성한다.
- [ ] `SkeletonDataAsset` importer와 `MarrowSkeletonAnimation` MonoBehaviour vertical slice를 만든다.
- [ ] Unity Mesh 갱신 샘플에서 region/mesh/blend/clipping을 검증한다.
- [ ] macOS/Windows/Linux editor smoke와 player build smoke를 CI 후보로 정리한다.

### Phase 6. Unreal/Godot feasibility

- [ ] Unreal `UActorComponent` + native runtime load spike를 만든다.
- [ ] Unreal procedural mesh 또는 custom vertex factory 중 최소 렌더 경로를 비교한다.
- [ ] Godot GDExtension `Node2D` wrapper spike를 만든다.
- [ ] 두 엔진의 패키징, license, CI 비용을 비교해 정식 지원 여부를 결정한다.

## 검증 명령

기존 네이티브 기준:

```sh
cmake -S . -B build
cmake --build build
./build/marrow_unit_tests
./build/marrow_c_smoke
./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
./build/marrow_fixture_smoke assets/fixtures/player_idle.mbin assets/fixtures/player_idle.matl
./build/marrow_renderer_sample --skip-render assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
./build/marrow_inspect --compare assets/fixtures/player_idle.mbin assets/fixtures/player_idle.mskl
```

동시성/성능 기준:

```sh
cmake -S . -B build-tsan -DMARROW_ENABLE_THREAD_SANITIZER=ON
cmake --build build-tsan --target marrow_thread_stress
./build-tsan/marrow_thread_stress assets/fixtures/player_idle.mskl

cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release
cmake --build build-bench --target marrow_benchmark
./build-bench/marrow_benchmark --skeletons 200
./build-bench/marrow_benchmark --runtime-stress assets/fixtures/player_idle.mskl
```

신규 SDK/통합 검증 후보:

```sh
cmake --install build --prefix /tmp/marrow-sdk
cmake -S samples/cmake-consumer -B /tmp/marrow-consumer -DMarrow_DIR=/tmp/marrow-sdk/share/marrow/cmake
cmake --build /tmp/marrow-consumer
ctest --test-dir /tmp/marrow-consumer

emcmake cmake -S . -B build-wasm -DMARROW_BUILD_WASM_VIEWER=ON
cmake --build build-wasm --target marrow_wasm_viewer

cmake --build build --target marrow_molga_integration_smoke
```

## CI 매트릭스 제안

| 축 | 후보 |
| --- | --- |
| OS | macOS arm64, macOS x64, Ubuntu x64, Windows x64 |
| Compiler | AppleClang, Clang, GCC, MSVC |
| Build type | Debug, Release, RelWithDebInfo |
| Sanitizer | ASan/UBSan, TSan for `marrow_thread_stress` |
| Package | static library, shared library, installed CMake package |
| Runtime format | `.mskl`, `.mbin` v1/v2, `.matl` v1 |
| Integration | C API smoke, C++ consumer, molga-engine smoke, WASM smoke, Unity editor smoke |
| Performance | release benchmark trend, 200-skeleton 60fps gate, runtime-stress gate |

## 주요 리스크와 대응

- ABI 안정성: C struct layout을 한번 공개하면 수정 비용이 크다. `size`/reserved field/opaque handle 원칙을 먼저 고정하고, ABI break는 major version에서만 허용한다.
- 렌더러 backend 차이: Metal/OpenGL/WebGL/Unity/Unreal/Godot의 blend, clipping, texture coordinate, PMA 처리 차이가 결과 불일치를 만든다. `RenderCommandList` conformance dump와 fixture image comparison을 같이 둔다.
- asset version migration: `.mskl`/`.mbin`/`.matl` 버전이 늘면 오래된 엔진 plugin이 새 파일을 잘못 로드할 수 있다. loader capability query와 명시적 unsupported-version error를 필수화한다.
- license/distribution: Unity/Unreal/Godot/Web 패키지는 엔진별 배포 규칙과 third-party license 표기가 다르다. SDK archive에 license manifest를 포함하고, 엔진 wrapper별 NOTICE 파일을 둔다.
- Web/WASM 메모리 모델: borrowed string과 native pointer 수명이 JS에서 오용되기 쉽다. JS binding은 복사 기반 안전 API를 기본으로 제공한다.
- molga-engine 편향: molga-engine 최적화가 generic runtime 계약을 흐릴 수 있다. molga-engine 통합은 reference wrapper로 두고, core runtime에는 engine type을 넣지 않는다.

## 우선순위

1. P0: C API hardening, ABI/version policy, C++ install package, native conformance tests
2. P0: molga-engine first-class integration vertical slice
3. P1: Web/WASM viewer/runtime
4. P1: Unity bridge MVP
5. P2: Unreal/Godot feasibility spike
6. P2: 정식 multi-engine release automation

## 인수 기준

- P0 완료 시점:
  - `marrow_c_smoke`, `marrow_fixture_smoke`, `marrow_renderer_sample --skip-render`, `marrow_inspect --compare`, `marrow_thread_stress`, release `marrow_benchmark`가 문서화된 명령으로 통과한다.
  - 설치된 SDK를 외부 CMake consumer가 `find_package(Marrow CONFIG REQUIRED)`로 빌드하고 fixture를 로드한다.
  - C ABI 버전, semantic versioning, 파일 포맷 호환성 정책이 문서화되어 있다.
  - molga-engine sample이 `.mskl` 또는 `.mbin` + `.matl`을 로드하고 1개 애니메이션을 재생하며 renderer command를 제출한다.

- P1 완료 시점:
  - Web viewer가 checked-in fixture를 로드하고 animation playback, event display, render command 또는 렌더 결과를 검증한다.
  - Unity sample scene이 native plugin을 통해 같은 fixture를 로드하고 재생한다.
  - native, WASM, Unity conformance 결과가 skeleton count, animation event, draw command count, bounds 기준으로 비교된다.

- P2 완료 시점:
  - Unreal/Godot feasibility 문서가 최소 렌더 경로, 패키징 비용, CI 가능성, 정식 지원 판단을 포함한다.
  - release checklist가 native SDK, molga-engine, Web, Unity 산출물을 한 번에 검증할 수 있다.
