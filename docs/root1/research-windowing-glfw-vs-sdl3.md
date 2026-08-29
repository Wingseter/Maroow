# Windowing 재평가: GLFW vs SDL3 (2026-08)

> 원래 GLFW는 "Windows+Mac 지원"을 근거로 선택했다. 2026년 8월 시점에 이 선택이 여전히 최선인지 재평가한 기록.

> **상태: 결정 완료, 아키텍처와 활성 PRD에 통합됨.** 실행 authority는
> `discription.md`의 플랫폼·GPU 소유권과 `.agents/tasks/prd-marrow-runtime.json`의
> MAR-192~210이다. 이 문서의 아래 "현재 상태"는 결정 당시 baseline 기록이며,
> 완료된 GLFW story를 소급 재작성하지 않는다.

## 결정 당시 baseline (역사적 사실)

- GLFW 3.4를 `external/glfw`에 vendoring. 사용처는 **에디터 셸에 국한**:
  `shell_main.cpp`, `shell_state.hpp`, `shell_project_panels.*`, `shell_smoke.cpp` + `imgui_impl_glfw`.
- ImGui 1.92.6 WIP, vendored 백엔드는 `glfw` + `opengl3` 두 개뿐.
- 에디터 뷰포트는 raw OpenGL 3.2 core (`viewport_renderer.cpp`, FBO → ImGui 텍스처).
- 런타임 렌더러(`sokol_backend.cpp`)는 sokol_gfx — macOS `SOKOL_METAL`, 그 외 `SOKOL_GLCORE`.
  `sokol_app`은 standalone `renderer_sample` 전용. 에디터는 renderer 모듈의 CPU 씬 준비만 사용.
- **CMake에 WIN32/MSVC 분기 없음.** sokol-shdc 경로도 osx/linux만 지정.
  `discription.md` §3의 렌더링 결정도 "Metal(macOS)과 OpenGL(Linux)" — 즉 Windows 지원은 현재 선언만 있고 배선은 없다.

## GLFW 3.4의 제품 기준 한계 (2026 시점)

| 요구 | GLFW 3.4 | SDL3 (3.4.x, 2026-07) |
|---|---|---|
| 펜 태블릿 압력/틸트 | 없음 (태블릿 PR 2018년부터 미병합) | `SDL_Pen` API 지원 |
| IME preedit (한글) | 없음 (#2130 미병합) | 텍스트 입력/IME API 지원 |
| 네이티브 파일 다이얼로그 | 없음 | `SDL_ShowOpenFileDialog` |
| ImGui 백엔드 | 공식 | 공식 (`imgui_impl_sdl3`) |
| 라이선스 / vendoring | zlib / add_subdirectory | zlib / add_subdirectory (동일) |
| 릴리스 페이스 | 3.4 (2024-02) 이후 없음 | 활발 (2025-01 안정판 이후 지속) |

기각한 대안:
- **sokol_app**: 렌더러와 windowing 통일 매력은 있으나 에디터 셸용으로 미니멀함(단일 윈도우, 펜 압력 없음, IME 약함).
- **Qt**: 네이티브 메뉴·다이얼로그·태블릿 제공하지만 ImGui 중심 구조와 어긋나고 의존성·라이선스 부담 큼.

## 로드맵 입력 (2026-08-09 확인)

가까운 로드맵의 실제 요구: **펜 태블릿 압력 입력**, **Windows 출시**.
(한글 IME 품질은 당장 요구 아님 — SDL3 전환 시 부수 이득으로 따라옴.)

## 당시 결정 입력

1. **GLFW → SDL3 전환을 계획한다.** 펜 압력은 GLFW로는 불가능하고, SDL3가 유일하게 현실적인 대체재.
2. **순서: SDL3 전환 먼저, Windows 포팅은 그 위에.** 셸 5개 파일에 국한된 작은 작업을 먼저 끝내야 플랫폼 작업을 두 번 하지 않는다.
   - SDL3 전환: `external/sdl` vendoring, `imgui_impl_sdl3` re-vendor, `shell_main.cpp` windowing/input 재작성, 모니터/DPI 코드 이관. 펜 이벤트(`SDL_EVENT_PEN_*`)는 뷰포트 입력 경로로 weight paint에 연결.
   - Windows 포팅: CMake WIN32 분기, exact Windows sokol-shdc, GLCORE 4.1, 로컬 Win10/11 실기 매트릭스.
3. **에디터 뷰포트 raw GL → sokol_gfx 통일.** macOS OpenGL은 4.1 동결·deprecated 상태이고, 에디터(GL)와 런타임(Metal)의 이중 GPU 경로는 더 큰 아키텍처 부채다.

최종 프로그램에서는 3번을 독립 병렬 트랙으로 남기지 않고
`SDL3 parity → 전체 editor Sokol 전환 → Windows 이식·실기 검증` 순서로 고정했다.
Hosted CI는 지원 authority가 아니며 `.github/workflows`를 추가하지 않는다.

## 결론 요약

GLFW는 "잘못된 선택"이었던 적은 없지만, 펜 압력 요구가 로드맵에 오른 순간 수명이 끝났다.
SDL3 전환은 격리가 잘 되어 있어 며칠 규모이며, Windows 출시 전에 끝내는 것이 이득이다.

## 통합된 최종 결정과 진행 경계

- MAR-192~198: SDL3 window/input/IME/pen parity 후 GLFW를 완전히 제거한다.
- MAR-199~204: process-root Sokol device, pass-free renderer core, SDL Metal/GL
  surface와 official `sokol_imgui`로 전환한 뒤 production raw GL/CGL을 제거한다.
- MAR-205~209: VS2022 x64, UTF-16 AppData atomic preferences, Winsock,
  Windows 11 editor, physical Windows Ink, portable folder를 검증한다.
- MAR-210: 2026-08-12 범위 결정에 따라 macOS arm64 Metal과 Windows 11 x64의
  같은 source revision evidence를 모아야만 완료한다. Ubuntu/Linux와 Windows 10은
  `NOT REQUIRED`이고 별도 PC portable 실행도 필수 gate가 아니다. 이 qualification
  backlog는 2026-08-16에 완료된 MAR-163/MAR-164와 2026-08-20에 완료된
  MAR-165/MAR-166/MAR-167, 또는 다음 MAR-168 제품 chain을 차단하지 않는다.

현재 source tree에는 SDL3/Sokol 전환 코드, Linux X11 경로와 Windows compile-time 경계가
반영되어 있지만, 실제 지원 status는 `platform-validation.md`를 따른다. Linux X11은
비보증 구현으로 남고 현재 지원 주장을 하지 않는다. Windows 11 고배율·수동 UI,
Windows Ink, 성능·pixel·resource 증거가 없으면 해당 story와 MAR-210은 open이다.
