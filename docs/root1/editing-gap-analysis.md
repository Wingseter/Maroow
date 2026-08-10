# Maroow 편집 기능 갭 분석 (vs Spine 4.2/4.3, Live2D Cubism 5.x)

최종 갱신: 2026-08-09. 기준: MAR-141~153 편집 P0, MAR-122~128 Parameter Modeling,
MAR-154 Runtime Explicit Duration, MAR-155 Editor Duration Authoring, MAR-156 Versioned User
Preference Store, MAR-157 Typed SelectionSet, MAR-158 Selection Migration, MAR-159 Hierarchy
Multi-Selection, MAR-160 Viewport Multi-Selection, MAR-161 Parent-Space Rotation Gizmo 및
MAR-162 Signed Local Scale Gizmo 완료 checkpoint, MAR-192~210 platform program 결정과
`.agents/tasks/prd-marrow-runtime.json`.
조사 방법: runtime/renderer/editor 소스 전수 조사 + Spine/Live2D 공식 문서 확인.

---

## TL;DR

Maroow 에디터의 강점은 **"이미 존재하는 리그 위에서의 애니메이션 저작"**에 집중되어 있다.
편집 P0 이후에는 트랜스폼 auto-key, 본/IK 타깃 이동 기즈모, 슬롯/디폼/이벤트/드로우오더 키잉,
다중 키 선택·리타임·복사/잘라내기/붙여넣기, 애니메이션 CRUD, 4종 제약 저작, 웨이트 페인팅,
어니언 스킨, 트랜잭션 기반 언두를 제공한다. MAR-122~128 이후에는 typed parameter/group/shape/deformer,
ArtPath, expression/lip-sync, Parameter Modeling mode와 56개 에이전트 오퍼레이션까지 제공한다.

현재 Spine/Live2D 대비 가장 큰 **미해결** 격차는 세 가지다:

1. **뷰포트 직접 조작의 P1 범위** — 이동, parent-space 회전, signed local scale 기즈모는 구현됐지만 FFD 버텍스 직접 편집과 grid/angle/vertex snap은 아직 없다.
2. **리그/메시 저작 불가** — 본·슬롯·스킨·어태치먼트·메시 지오메트리를 에디터에서 생성/삭제/편집할 수 없다(임포트 전용). 이는 오버레이 아키텍처의 의도된 결과지만, "에디터"로서는 결정적 제약.
3. **고급 타임라인 UX 미구현** — P0 도프시트는 완성됐지만 그래프 에디터, 그래픽 베지어 핸들, 리타임 스케일, 커브 프리셋/자동 핸들/루프 동기화는 P1이다.

이전의 네 번째 핵심 gap이던 **Live2D식 파라미터 모델링 레이어는 해소됐다**. MAR-121은 MAR-122에 통합됐고,
MAR-122~128의 런타임·렌더러·프로젝트·에디터·Agent/MCP checkpoint가 2026-07-16에 검증됐다.
이후 MAR-154가 runtime의 authored/inferred/effective clip duration 경계를, MAR-155가
`.marrow` editor authoring·undo·Agent/MCP·key auto-grow를 2026-07-17에 완료했다.
MAR-156은 project history와 분리된 versioned user preference store를 2026-07-18에 완료했다.
MAR-157은 같은 날 transient typed selection domain과 legacy active accessor 경계를 완료했다.
MAR-158은 모든 panel consumer의 직접 selection 해석과 성공한 runtime-source replacement의 exact
identity reconciliation을 같은 날 완료했다. MAR-159는 실제 visible hierarchy row 순서의
platform-correct replace/toggle/range/additive-range gesture와 transient anchor를 2026-07-21에 완료했다.
같은 날 MAR-160은 viewport point/box multi-selection을, MAR-161은 frozen parent-space rotation과 raw
multi-turn auto-key를 완료했다. MAR-162는 frozen scale-free local axis와 signed/exact-zero scale
auto-key를 2026-07-25에 완료했다. 다음 제품 milestone은 single-vertex FFD를 담당하는
MAR-163이지만, 플랫폼 프로그램이 선행하도록 dependency가 MAR-210으로 바뀌었다. 현재 실행
milestone은 MAR-192이며 MAR-210 qualification 전에는 MAR-163을 시작하지 않는다.

---

## 1. 현재 편집 기능 인벤토리 (요약)

### 아키텍처 전제: 오버레이 모델

`ProjectData`(`include/marrow/editor/project.hpp`)는 본/슬롯/스킨/어태치먼트 컨테이너를 갖지 않는다.
리그 구조는 베이스 `.mskl` 스켈레톤에 대한 **참조** + 이름 키 기반 **편집 오버레이**로 저장하고,
`build_runtime_document`(`src/editor/project.cpp`)가 베이스를 복사한 뒤 오버레이를 병합한다. Parameter modeling은
이와 별도로 optional typed additive `parameter_model`을 소유하며 같은 runtime build에서 root section으로 변환한다.

- 에디터에서 **생성 가능**: 새 애니메이션·타임라인, IK/경로/트랜스폼/물리 제약(이름 기반 upsert), 기존 메시의 웨이트/디폼 재저작, parameter/group/shape/deformer/expression/lip-sync 정의
- **임포트 전용**(에디터 생성 불가): 본, 슬롯, 스킨, 어태치먼트, 메시 버텍스/트라이앵글

### 잘 되어 있는 것

| 영역 | 내용 | 위치 |
|---|---|---|
| 웨이트 페인팅 | Paint/Erase/Smooth 브러시, 반경/강도, 히트맵, 기존 자동 바인딩, 브러시별 top-4 정규화, 스트로크 단위 언두 | `shell_weight_paint.cpp` |
| 키프레임 편집 | 본 R/T/S/shear, 슬롯 light RGBA/attachment, 메시 디폼, 드로우오더, 이벤트 — 추가/삭제/시간·값·보간 수정 | `shell_timeline.cpp` |
| 보간 | 키별 Linear/Stepped/Bezier + 베지어 제어점 4개 숫자 입력 | `shell_timeline.cpp` |
| 재생 | 재생/일시정지(Space), 루프, 역재생, 스크럽, 이전/다음 키 스텝, 애니메이션 큐+믹스 프리뷰 | `shell_timeline.cpp` |
| Clip duration | runtime authored/inferred/effective 경계, ordered `.marrow set_duration`, live UI·undo, key auto-grow, tail/queue/playhead 재구성, JSON↔MBIN 보존 | `authoring.cpp`, `session.cpp`, `shell_project_panels.cpp` |
| User preferences | v1 `editor-settings.json`, six typed curve tokens, raw Recent Projects paths, macOS/Linux/Windows resolver, Windows UTF-16 AppData와 durable atomic replace, unknown-field 보존, malformed/future-version 보호 | `preferences.cpp`, `preferences.hpp`, `preference_store_tests.cpp` |
| Platform/GPU shell | SDL3 window/input/IME/pen, macOS Metal 및 Windows/Linux GLCORE 4.1 Sokol surface, pass-free scene renderer, official sokol_imgui; 물리 플랫폼 qualification은 MAR-210까지 open | `sdl_window_host.cpp`, `sokol_graphics_device.cpp`, `viewport_renderer.cpp` |
| Entity selection | 이름 기반 typed Bone/Slot/Attachment/Constraint 집합, 단일 active invariant, stable-order primitives, hierarchy replace/toggle/range/additive-range와 viewport typed point/bone-only box gesture, transient anchor, selected/active 구분 | `selection.cpp`, `shell_selection.cpp`, `selection_set_tests.cpp` |
| 뷰포트 저작 | 안정적 카메라, screen/world 역변환, cursor zoom, 명시적 Fit, 본/IK 타깃 X/Y/free 이동, frozen parent-space 회전, signed local X/Y/uniform scale auto-key | `shell_viewport.cpp`, `shell_viewport_ui.cpp` |
| 도프시트 | 60 FPS 눈금자, 독립 zoom/pan, 안정적 키 identity, toggle/box 선택, 다중 리타임, typed clipboard | `shell_timeline.cpp` |
| 애니메이션 관리 | create/duplicate/rename/delete, 확인 UI, ordered `.marrow.animation_edits`, queue/preview cascade | `authoring.cpp`, `shell_project_panels.cpp` |
| 제약 저작 | IK/경로/트랜스폼/물리 4종 모두 추가+파라미터 편집, 영구 저장+언두 | `shell_constraints.cpp` |
| 언두/트랜잭션 | 스냅샷 100개 캡, 머지 키 그룹핑, 원자적 런타임 리빌드+실패 롤백 | `session.cpp` |
| 어니언 스킨 | 프레임/키프레임 모드, 전후 개수, 스텝, 앵커 | `shell_viewport_ui.cpp` |
| 임포트/익스포트 | PSD→리그 생성, Spine JSON/atlas 임포트, 아틀라스 패킹, `.mskl`/`.mbin`/`.matl` 익스포트 | `psd_import.cpp` 등 |
| Parameter modeling | raw direct/final preview, 1D shape, 2D warp/rotation, full lattice·pivot gesture, expression/lip-sync, ArtPath runtime/render | `shell_parameters.cpp`, `parameter_project_model.cpp`, `parameter_model.cpp` |
| 에이전트 표면 | 56개 오퍼레이션(조회 12, 검증 3, 관리 10, 편집 31). Animation CRUD·duration·atomic timeline retime·parameter authoring은 MCP 도구에도 노출 | `agent_dispatch.cpp`, `agent_handlers_editing.cpp`, `agent_handlers_parameters.cpp`, `tools/mcp/tools/editing.py` |
| 종합 회귀 방지 | base-only timeline materialization, typed parameter model, duration save/reload·rollback·auto-grow, undo/redo, JSON↔MBIN, 56-op registry와 Parameter shell을 headless smoke로 검증 | `editor_project_smoke.cpp`, `parameter_project_smoke.cpp`, `shell_smoke.cpp`, `agent_dispatch_smoke.cpp` |

### 남아 있는 의도적 제한/부분 구현

- **Setup Pose/슬롯 setup 색상은 의도적으로 read-only** — Animation 모드 본 포즈는 항상 playhead auto-key이고, 슬롯 light/attachment는 timeline editor에서 저작한다. 저장되지 않는 preview-only 포즈/색상 입력은 없다.
- **inherit timeline은 read-only** — project overlay는 MAR-184, 편집 parity는 MAR-185로 미뤘다.
- **뷰포트는 이동·회전·signed scale까지 직접 저작** — single/multi-vertex FFD는 MAR-163~164, 공용/vertex snap은 MAR-165~166이다. 경로 제어점 직접 조작과 선택 항목의 group transform은 P1 범위 밖이다.
- **그래프 에디터 없음** — 숫자 Bezier 입력은 가능하지만 graph view/point/handle, preset/auto handle/loop 동기화는 MAR-167~172다. 선택 키 시간 스케일과 preview 속도는 MAR-173~174다.
- **제약 파라미터 일부 위젯 없음** — IK softness/compress/stretch, Physics step/x/y/rotate/scaleX/shearX/limit/massInverse (라운드트립은 됨).
- **제약 삭제/이름변경 불가** — lifecycle schema는 MAR-177, UI·agent surface는 MAR-178, 누락 위젯은 MAR-179 범위다.
- **Hierarchy와 viewport entity gesture 완료** — MAR-159는 visible row range와 transient anchor를, MAR-160은 typed point hit와 visible active-Bone box selection을 완료했다. 모든 도구는 active item 하나만 편집하며 group transform은 범위 밖이다.

---

## 2. 갭 분석 — Spine/Live2D 참조 기능 대비

우선순위 표기: 🔴 결정적(경쟁 도구의 CORE 워크플로), 🟡 중요(생산성 격차 큼), 🟢 편의(있으면 좋음).

### 2.1 뷰포트 직접 조작 — P0 기반 완료, P1 확장 필요

Spine 편집의 본질은 "뷰포트에서 본을 잡아 끄는 것"이고, Live2D는 "캔버스 위에서 디포머/파라미터를 직접 조작하는 것"이다. Maroow P0는 이 층의 최소 수직 슬라이스를 구현했다.

| 기능 | 현재 상태 | 후속 |
|---|---|---|
| 본/IK 타깃 X/Y/free 이동 | 구현. 부모 2x2 inverse, singular rollback, 한 drag 한 undo, auto-key | P0 유지 |
| 안정적 카메라/`world_from_screen`/cursor zoom/Fit | 구현. 포즈 변화가 카메라를 재프레이밍하지 않음 | P0 유지 |
| rotation 기즈모 | 구현. 58px fixed ring, frozen parent-space inverse, supported-inherit hint, continuous unwrap, raw multi-turn absolute auto-key, one transaction/undo | MAR-161 완료 |
| scale X/Y/uniform 기즈모 | 구현. 74px fixed handles, frozen scale-free local axes, signed ratio와 exact-zero fallback, active-only auto-key, one transaction/undo | MAR-162 완료 |
| single/multi-vertex FFD 직접 편집 | 미구현; 숫자/도프시트 저작은 가능 | MAR-163~164 |
| translate/rotate/scale 공용 snap | 미구현 | MAR-165 |
| FFD grid/magnetic vertex snap | 미구현 | MAR-166 |
| 경로 제어점 직접 조작·group transform | 미구현 | P1 범위 밖 |

초기 선결 과제였던 `world_from_screen`과 포즈 bounds에서 분리된 카메라는 MAR-143에서 해결됐다.

### 2.2 포즈 편집의 신뢰성 — P0에서 해결

- Setup 모드는 임포트 setup data를 읽기 전용으로 표시한다.
- Animation 모드의 inspector R/T/S/shear와 viewport 이동·회전·scale은 현재 playhead의 절대 local key를 upsert한다.
- 첫 편집 전에 effective/base timeline 전체를 project overlay로 materialize하므로 임포트 키를 단일 신규 키로 덮어쓰지 않는다.
- Viewport 회전과 scale도 같은 materialization 경로를 사용한다. Non-zero setup transform은 absolute local 값에서 정확히 한 번만 차감되며 imported key와 curve는 보존된다.
- live `EditTransaction`이 반복 preview refresh, 한 gesture 한 history entry, Escape/실패의 정확한 rollback을 담당한다.
- GUI와 agent setter는 같은 UI-free project-domain authoring primitive를 사용한다.

### 2.3 타임라인/애니메이션 워크플로 — P0 도프시트 완료, graph/P1 잔여

| 남은 기능 | 참조 (Spine) | 참조 (Live2D) |
|---|---|---|
| 그래프(커브) 에디터 — 그래픽 베지어 핸들 드래그 | Graph editor, 4.3 자동 조정 핸들·기본 커브 기억·루프 커브 동기화 [CORE] | Graph editor [CORE] |
| 선택 키 시간 스케일 | Dopesheet [CORE] | Timeline [CORE] |
| 재생 속도 조절 | [CORE] | [CORE] |
| 루프 경계 키 자동 복제 | 루프 커브 동기화 (4.2) | 루프 편집 지원 (5.2) |
| 키 인터폴레이션 프리셋 | 커브 프리셋 | 확장 보간 |

현재 도프시트는 초/프레임 ruler, duration과 독립된 zoom/pan, 안정적 same-time event identity,
클릭/Cmd·Ctrl toggle/box selection, Add/Remove, 다중 drag retime, 60 FPS snap/Alt bypass,
typed copy/cut/paste와 compatible single-lane remap을 제공한다. 애니메이션 create/duplicate/rename/delete도
UI와 agent/MCP 양쪽에 있다. MAR-154 이후 runtime은 source asset의 authored duration presence,
key-derived `inferred_duration()`과 effective `duration()`을 구분하며 playback과 MBIN도 effective boundary를
사용한다. MAR-155는 ordered `.marrow.animation_edits set_duration`, UI-free setter, live UI·undo,
Agent/MCP dry-run을 연결했다. 키 생성·오른쪽 이동은 명시 경계를 같은 transaction에서 늘리고,
왼쪽 이동·삭제는 자동 축소하지 않는다. 수동 값이 마지막 키보다 짧으면 project·preview·selection·history를
바꾸지 않고 거부한다. Duration이 없는 기존 project/runtime asset은 마지막 키 inferred fallback을 그대로 유지한다.

현재 wire easing은 outgoing segment 전체가 하나의 `[cx1, cy1, cx2, cy2]`를 공유한다. P1 graph는 이 경계를
유지하며 Transform X/Y나 Slot RGBA component별 독립 curve를 만들지 않고 UI에도 이 제약을 표시한다.

### 2.4 메시/웨이트 저작 — 🟡 (topology는 보류, weight overlay는 P1)

| 부재 기능 | 참조 (Spine) | 참조 (Live2D) |
|---|---|---|
| 메시 지오메트리 편집 (버텍스 추가/이동/삭제, 엣지/헐) | Mesh 편집 [CORE] | 수동 메시 편집 [CORE] |
| 오토메시 (이미지 알파에서 자동 생성) | 4.3 다중 트레이스+균일 슬라이더 [CORE] | 자동 메시 생성기 (5.0 개선) [CORE] |
| 명시적 candidate 목록 기반 결정적 오토 웨이트 | Bind+auto weights [CORE] | 스키닝 자동화 |
| 웨이트 Replace 모드·수치 직접 편집·명시적 정규화 버튼 | 웨이트 툴 | — |
| 웨이트 복사/붙여넣기, Weld(메시 간 이음새) | [+] | Glue [CORE] |

현재 brush와 agent 경로에는 서로 중복된 weight 정규화가 있고 기존 자동 바인딩은 P1의 명시적 candidate·결정성 계약을
제공하지 않는다. MAR-175는 수동 도구와 agent를 하나의 canonical domain primitive로 합치고, MAR-176은 setup pose에서
결정적인 top-4 auto-weight를 추가한다. 이 작업은 기존 `mesh_edits.weights` overlay 안에서 끝난다.

반면 메시 지오메트리는 오버레이 모델상 편집 불가(베이스 문서 소유)다. `mesh_edits`를 지오메트리 override로 확장할지,
베이스 문서 편집을 허용할지는 canonical authoring graph 설계가 선행되어야 하며 P1에는 포함하지 않는다.

### 2.5 리그 구조 편집 — 🟡 (전략 결정 필요)

본/슬롯/스킨/어태치먼트의 생성·삭제·이름변경·재부모화가 GUI에도 에이전트 표면에도 전혀 없다. "Spine/PSD에서 임포트한 리그의 후처리 에디터"가 의도된 포지셔닝이라면 낮은 우선순위로 두되, **자체 저작 도구**를 지향한다면 오버레이 모델 확장(추가 엔티티 오버레이) 또는 베이스 문서 직접 편집이 필요하다. 이 결정이 2.4보다 상위의 갈림길이다.

### 2.6 Live2D식 파라미터 모델링 — 승인된 Maroow 범위 구현 완료

포맷 스펙(`format-spec.md`의 `parameters`/`parameterShapes`/`parameterDeformers`/`artPaths`/`expressions`/`lipSync`)과 MAR-122~128 구현이 검증됐다. MAR-121의 런타임 기반은 MAR-122에 통합되어 별도 구현하지 않는다. 참조한 Live2D 대응물:

- 파라미터 슬라이더 + 키폼 편집 → Cubism 파라미터 시스템 [CORE]
- 워프/회전 디포머 → Cubism 디포머 [CORE] (5.3의 부모 그리드 자동 확장 참고)
- 블렌드셰이프 → Cubism 5.0 "blend shapes everywhere"
- 표정/립싱크 → 표정 프리셋, Motion Sync(5.0)
- (차별화 아이디어) Cubism 5.2 **파라미터 컨트롤러**(캔버스 위 드래그로 파라미터 묶음 조작, 타깃 추종)는 도입 시 강력한 UX

현재 구현 경계와 검증 결과:

| 계층 | 구현·검증 상태 |
| --- | --- |
| Runtime | finite raw direct와 final composed buffer, discrete round/optional clamp, 1D endpoint/linear shape, bilinear warp, rotation pivot/influence, one-level deformer chain과 dependency cache |
| Renderer | attachment-local final mesh offset, skeleton scale/mirror를 반영한 ArtPath root overlay, deterministic cap/join tessellation, atlas-free preparation과 cache 실패 원자성 |
| Project/Editor | 일곱 optional parameter family의 typed·lossless save/reload/export, transient non-dirty preview, CRUD, 3×3 full lattice/pivot gesture, confirmed atomic keyform capture |
| Agent/MCP | `parameters.list`와 mutation 5개(`parameter.set`, `deformer.create`, `keyform.capture`, `expression.create`, `lip_sync.map`), candidate dry-run, MAR-128 C++/Python 55-op exact parity; MAR-155 duration operation 포함 현재 56-op parity |
| Compatibility/성능 | `.mskl` v1, `.mbin` v2, C ABI v1 유지. CTest 11/11, runtime 4/4, editor 5/5, 200 skeleton `frame_ms=4.45`/`score=100`, parameter `0.07us`/deformer `0.51us` |

MAR-122~128 checkpoint 당시 29개 runtime/renderer unit, parameter project save/reload와 JSON↔MBIN compare,
atlas-free ArtPath renderer, 당시 55-op Agent smoke, standard/parameter-only MCP socket E2E와 `git diff --check`가
통과했다. MAR-154의 duration case가 추가된 현재 runtime/renderer unit 목록은 30개다.

여전히 제외되는 parameter 저작 범위는 ArtPath point GUI, raw mesh topology/vertex sculpting, keyform copy/mirror,
N차원 keyform, Live2D 파일/Core/ABI 호환과 audio analysis다. Slider와 `parameter.set`은 preview-only이며 저장·export하지 않는다.

### 2.7 워크플로/편의 기능 — 🟢~🟡

| 부재 기능 | 참조 | 심각도 |
|---|---|---|
| 전역 entity 멀티 셀렉트/박스 셀렉트 (도프시트 키 선택은 P0에서 구현) | 양쪽 다 [CORE] | 🟡 — P1은 selection/gesture와 active item 편집까지만 포함; group transform 제외 |
| File 메뉴: New/Open/Save/Save As/Recent Projects | 양쪽 다 [CORE] | 🟡 — Save가 툴바에만 있음 |
| 대칭 편집 — 반전 붙여넣기/미러 | Live2D 5.2 반전 형상 붙여넣기, Spine flip | 🟢 |
| Problems 뷰 (경고 목록 + 클릭 이동 + allowlist 수정) | Spine 4.3 [+] | 🟢 — 기존 summary 위에 MAR-186 structured collector를 추가한 뒤 MAR-187 UI 연결 |
| 숫자 필드 수식 입력 (`10 + v * 8`) | Spine 4.3 [+] | 🟢 |
| PSD 재임포트 | Spine 4.3 PSD 관리, Live2D 재임포트 [CORE] | 🟡 — 기존 import/smoke를 기반으로 provenance·staging diff(MAR-188), journaled atomic commit(MAR-189), GUI(MAR-190)가 필요 |
| 계층 검색/일괄 치환 | Live2D 검색·치환 [+] | 🟢 — 계층 텍스트 필터는 있음 |
| 키보드 단축키 체계 + 단축키 도움말 | 양쪽 다 | 🟢 — 현재 Space/Ctrl+Z 등 소수 |
| 비디오/GIF 익스포트, HTML 프리뷰 익스포트 | Spine [CORE]/[+] | 🟢 |

---

## 3. 확정 로드맵

### 제품 결정

- 단기 제품은 **임포트 리그 기반 애니메이션/후처리 에디터**다.
- Setup Pose와 슬롯 dark tint는 P0에서 읽기 전용이다. Animation 모드의 R/T/S/shear 변경은 항상 현재 playhead의 키로 영속화한다.
- P0 뷰포트의 안정적 카메라와 본/IK 타깃 **이동** 기즈모 위에 MAR-160 typed point/box selection, MAR-161 parent-space 회전, MAR-162 signed local scale을 추가했다. FFD 직접 조작과 snap은 후속 P1이다.
- MAR-154에서 호환 가능한 runtime explicit/inferred/effective duration 경계를, MAR-155에서 editor
  authoring·undo·Agent/MCP를 완료했다. 키 생성·오른쪽 이동은 같은 transaction에서 duration을 자동 연장하되
  키 삭제·왼쪽 이동은 자동 축소하지 않으며 마지막 키보다 짧은 수동 축소를 원자적으로 거부한다. Duration이 없는 기존
  프로젝트와 runtime asset은 계속 마지막 키를 경계로 사용한다.
- MAR-157의 `SelectionSet`은 exact typed name identity, stable insertion order, 단일 active item을 보장한다. 선택은 `ShellState`의 transient 상태이며 project/preview/history/preferences/runtime/C ABI/Agent·MCP에 포함하지 않는다.
- MAR-158은 shell-private `ResolvedSelection`으로 각 consumer가 active typed item을 직접 해석하게 하고, 성공한 project/runtime source adoption 뒤에만 exact identity reconciliation을 수행한다. 누락 identity만 prune하며 실패한 adoption, preview refresh, 일반 authoring rebuild는 selection을 바꾸지 않는다.
- MAR-161 rotation authoring inverse는 root와 `onlyTranslation`의 skeleton-scale basis, `normal` child의 evaluated parent-world 2x2만 지원한다. `noRotationOrReflection`, `noScale`, `noScaleOrReflection`은 runtime에서는 계속 지원하지만 rotation ring은 숨기고 viewport hint를 표시한다.
- Multi-turn rotation은 project와 exported JSON에 raw degree로 보존한다. MBIN v2 optional AKEY가 continuous winding을 표현하지 못하면 해당 rotate channel만 canonical generic payload로 fallback하며 runtime acceptance는 winding 횟수가 아닌 360도 동치 orientation이다.
- MAR-162 scale은 같은 inherit 지원 범위에서 evaluated local rotation/shear의 positive scale-free X/Y axis를 gesture 시작 시 freeze한다. Nonzero 축은 signed pivot ratio를, exact-zero 축은 74px당 scale 1 delta를 사용한다. Uniform은 시작 X:Y ratio와 signs를 보존하고 `(0,0)`에서는 숨긴다. Finite signed/exact-zero key는 기존 project overlay와 JSON/MBIN canonical payload에 그대로 저장한다.
- P1은 기존 imported-rig/name-overlay 구조와 `.mskl` v1, `.mbin` v2, C ABI v1을 유지한다. `.marrow` 변경은 optional additive metadata/operation으로 제한한다.
- 본·슬롯·스킨·어태치먼트와 메시 topology 저작, 경로 제어점 편집, selection group transform, partial/degraded project open은 P1 범위 밖이다.
- MAR-129~140 리팩터링은 HEAD `4c93ca1`에서 완료됐다. MAR-137은 constraint 모듈 추출만 완료한 것이며 rename/delete는 MAR-178이다.

### P0 — MAR-141~153 (완료 — 2026-07-12 검증)

아래 수직 슬라이스는 모두 구현됐고 project/shell/agent smoke가 base-key 보존, transaction rollback,
save/reload와 JSON/binary export까지 검증한다.

| Story | 수직 슬라이스 |
| --- | --- |
| MAR-141 | Setup Pose/슬롯 색상 읽기 전용화와 preview-only 입력·가짜 key 버튼 제거 |
| MAR-142 | UI-free authoring operation, live transaction 갱신, Animation 인스펙터 auto-key |
| MAR-143 | 포즈 bounds와 분리된 카메라, `world_from_screen`, cursor zoom, 명시적 Fit |
| MAR-144 | 본 X/Y/free 이동 기즈모와 IK 타깃 drag, 부모 역변환, 한 drag 한 undo |
| MAR-145 | 슬롯 light RGBA와 stepped attachment(`<none>` 포함) 키 편집 UI |
| MAR-146 | 초/프레임 눈금자, 60 FPS 표시, duration과 독립된 timeline zoom/pan |
| MAR-147 | `TimelineKeyRef` 기반 클릭/Cmd·Ctrl toggle/box 다중 선택 |
| MAR-148 | 툴바 Add/Remove Key를 모든 editable lane에 연결; inherit는 disabled |
| MAR-149 | 다중 키 drag retime, 전체 delta clamp, 기본 60 FPS snap과 Alt 해제 |
| MAR-150 | 같은 animation 안에서 copy/cut/paste; 최초 키 playhead 정렬, 충돌 replace |
| MAR-151 | `.marrow.animation_edits`, UI-free/agent animation CRUD, atomic cascade |
| MAR-152 | animation 생성/복제/rename/delete UI와 확인·선택·queue remap |
| MAR-153 | 뷰포트→auto-key→도프시트→save/reload→JSON/binary export E2E guardrail |

PRD 배열은 이 스토리를 MAR-120 직후에 둔다. P0 구현 체크포인트가 끝났고 MAR-121은 MAR-122에 통합된 done tombstone이다. 아래 dependency-ordered 파라미터 milestone도 2026-07-16에 완료됐다.

### Parameter Modeling — MAR-122~128 (완료)

| Milestone | 수직 checkpoint |
| --- | --- |
| MAR-122 | MAR-121의 parameter definition/per-instance value 기반을 흡수하고 parameters/groups를 typed 승격한다. 후속 family는 담당 milestone까지 lossless JSON으로 보존하며 runtime export와 구 자산 empty fallback을 함께 구현 |
| MAR-123 | `blend_shapes`를 typed 승격하고 1D attachment-local mesh shape, override/additive 조합, animation-FFD-only와 final offset accessor를 분리 |
| MAR-124 | `deformers`를 typed 승격하고 2D bilinear warp, rotation deformer, one-level chain validation, dependency cache와 별도 benchmark metric을 구현 |
| MAR-125 | `art_paths`를 typed 승격하고 skeleton-local ArtPath root overlay, deterministic CPU stroke tessellation, atlas-free renderer preparation을 구현 |
| MAR-126 | `expressions`/`lip_sync`를 typed 승격하고 direct/lip/expression composition, fade/reset policy, deterministic amplitude/phoneme filter state를 구현 |
| MAR-127 | Parameter mode, transient non-dirty preview, persistent CRUD와 confirmed keyform capture |
| MAR-128 | inspection 1개+mutation 5개의 exact agent/MCP surface, dry-run candidate build, registry parity 49→55 |

이 순서는 기능 선행관계다. 각 checkpoint는 focused 구현·검증 경계다.

### Platform program — MAR-192~210 (MAR-163 선행 gate)

플랫폼 전환은 P1 제품 기능의 병렬 roadmap이 아니라 활성 PRD에 물리적으로 삽입된 선행
dependency chain이다. 순서는 SDL3 parity와 GLFW 제거(MAR-192~198), editor Sokol 전환과
raw GL 제거(MAR-199~204), Windows/portable/physical-pen qualification(MAR-205~209), 같은
revision 최종 로컬 매트릭스(MAR-210)다. 코드·단위 테스트가 구현돼도 macOS, Ubuntu X11,
Win10, Win11, 실물 pen, pixel/performance/resource/package 증거가 모두 없으면 story를 done으로
표시하지 않는다. 상세 acceptance와 명령은 활성 PRD, 증거는 `platform-validation.md`가 authority다.

### P1 — MAR-154~191

P1 시작 gate인 **MAR-128 완료 checkpoint**, MAR-154–155 duration checkpoint, MAR-156 preference checkpoint, MAR-157 typed selection, MAR-158 selection migration, MAR-159 hierarchy multi-selection, MAR-160 viewport multi-selection, MAR-161 parent-space rotation 및 MAR-162 signed local scale checkpoint는 통과했다. 제품 chain은 MAR-163~191 순서를 유지하지만 MAR-163의 dependency는 MAR-210이다. 따라서 현재 next open execution milestone은 MAR-192이고, 플랫폼 qualification 뒤에만 기존 P1 제품 chain이 재개된다.

#### 기반·선택

| Story | Title | 수직 슬라이스 |
| --- | --- | --- |
| MAR-154 | Runtime explicit clip duration (완료, 2026-07-17) | `AnimationData::explicit_duration`, `inferred_duration()`, effective `duration()`을 분리했다. finite/non-negative/last-key 검증, 비제로 identity key boundary, 구 자산 fallback, empty/tail clip, loop·queue·complete·reverse·snapshot, authored presence와 effective-duration AKEY를 JSON↔MBIN fixture로 검증했다. `.mskl` v1, `.mbin` v2, C ABI v1은 유지한다. |
| MAR-155 | Editor duration authoring (완료, 2026-07-17) | ordered `.marrow.animation_edits set_duration`과 unknown/additive 보존, UI-free mutation, live UI·단일 undo, key auto-grow/no-shrink, 원자적 reject, 56번째 Agent/MCP operation, save/reload·JSON/MBIN export를 project/shell/agent smoke로 검증했다. |
| MAR-156 | User preference store (완료, 2026-07-18) | versioned user-local JSON, macOS/Linux pure path resolver와 `MARROW_CONFIG_HOME`, six-token curve default, raw Recent Projects 배열, field별 fallback, unknown additive 보존, same-directory temp+atomic rename을 UI-free service와 전용 CTest로 검증했다. Project/session/runtime/C ABI/Agent/MCP와 분리된다. |
| MAR-157 | Typed SelectionSet (완료, 2026-07-18) | exact typed Bone/Slot/Attachment/Constraint identity, 중복 없는 stable insertion order, 단일 active invariant와 replace/toggle/range/clear/prune/remap을 UI-free public model로 구현했다. `ShellState`는 이 set 하나만 소유하고 legacy accessor가 active 이름을 현재 runtime/preview index로 해석한다. 선택은 project/preview/history/revision/Agent·MCP와 분리된다. |
| MAR-158 | Selection migration (완료, 2026-07-18) | shell-private `ResolvedSelection`으로 hierarchy, inspector, viewport, timeline, constraint, weight-paint를 `SelectionSet`에 직접 연결했다. 성공한 project/runtime source adoption은 exact Bone/Slot/Attachment/Constraint identity를 새 runtime에 재해석하고 누락 항목만 prune하며, 실패는 selection/runtime을 보존한다. Constraint remap/prune primitive는 검증했지만 실제 rename/delete transaction 연결은 MAR-178이 담당한다. |
| MAR-159 | Hierarchy multi-select (완료, 2026-07-21) | 실제 렌더된 Bone/Slot/Attachment row의 exact identity 순서와 transient anchor로 plain replace, macOS Cmd/기타 Ctrl toggle, Shift visible-range replace, Cmd/Ctrl+Shift additive-range를 구현했다. Filter/collapse/source adoption에서 anchor를 검증하고 selected/active 표시와 active-only consumer를 유지한다. |
| MAR-160 | Viewport multi-select (완료, 2026-07-21) | Constraint target, Bone joint/body, Slot centroid, topmost Attachment triangle의 category/distance/stable-order precedence와 platform point replace/toggle을 구현했다. Empty-space drag는 4px threshold 뒤 visible runtime-active Bone center만 skeleton order로 replace/additive selection하며 active-only consumer와 transient project/runtime/history 불변을 유지한다. |

MAR-154 checkpoint는 runtime/renderer unit 30개, runtime-labeled CTest 4/4, 전체 CTest 11/11,
JSON·MBIN fixture smoke와 roundtrip comparison, C ABI smoke 및 `git diff --check`를 통과했다.
MAR-155 checkpoint는 project duration E2E, shell live/queue/clamp/reject, 56-op Agent dispatch,
editor-labeled CTest 5/5, Python MCP schema/parity·native socket E2E, JSON/MBIN duration export와 `git diff --check`를 통과했다.
MAR-156 checkpoint는 first-run/roundtrip/optional fallback/version·malformed 진단, macOS/Linux
경로와 환경 복원, rename failpoint의 기존 bytes·temp cleanup, 열린 `EditorSession`의 serialization,
dirty, undo/redo와 세 revision 불변을 `marrow_preference_tests`와 editor/full CTest로 검증했다.
MAR-157 checkpoint는 7개 `SelectionSet` case로 typed scope, stable order, active fallback,
prune/remap collision, invalid range 원자성을 검증했다. Shell smoke는 Bone/Slot/Attachment/Constraint
legacy 해석과 mixed active-only 동작, project bytes·preview/runtime data·dirty·undo/redo·세 revision 불변을
검증했으며 editor CTest 7/7과 전체 CTest 13/13을 통과했다.
MAR-158 checkpoint는 10개 `SelectionSet` case로 constraint remap/delete/collision, 다른 kind/type 보존,
constraint-only prune와 runtime exact reconciliation을 검증했다. Project/shell smoke는 이름 기반 index
재해석, fully scoped attachment와 constraint prune, deterministic active fallback, mixed consumer 규칙,
malformed reload의 selection/runtime 원자성 및 reconciliation의 transient 불변을 검증했다. Agent registry
56개, editor CTest 7/7, 전체 CTest 13/13과 `git diff --check`도 통과했다.
MAR-159 checkpoint는 forward/reverse visible order의 plain/toggle/range/additive-range, deterministic
active fallback, filter/collapse의 hidden row 제외와 anchor invalidation, toggled-off visible anchor,
source reorder/delete 및 malformed reload 보존을 검증했다. Selected/active 표현과 active-only consumer,
project bytes·preview/runtime data·dirty·undo/redo·revision 불변도 project/shell smoke로 확인했다.
`marrow_selection_tests` 10/10, 56-operation Agent registry, editor CTest 7/7, 전체 CTest 13/13과
`git diff --check`를 통과했다.
MAR-160 checkpoint는 synthetic overlap tie, region/GPU-skinned mesh/Slot/Constraint point hit,
forward/reverse box, additive mixed prefix, empty/no-movement semantics, inactive/hidden Bone 제외,
source-adoption/orphan cleanup과 hierarchy synchronization을 기존 shell smoke로 검증했다.
MAR-161 checkpoint는 root/reflected skeleton scale, translated·rotated·sheared·non-uniform parent,
negative determinant, `normal`/`onlyTranslation`, unsupported inherit, singular/NaN/Inf rejection,
정확한 180도 tie와 positive/negative 450도 unwrap, 2px pivot suspend/rebase를 검증했다. Gesture smoke는
raw effective restart, live preview, imported key/curve materialization, non-zero setup rotation, no-op,
one undo/redo와 cancel/non-finite rollback을 확인했다. Runtime unit 31개, SelectionSet 10개,
56-operation Agent registry, C ABI v1, editor CTest 7/7, 전체 CTest 13/13 및 JSON-MBIN orientation
comparison을 통과했다.
MAR-162 checkpoint는 74px X/Y/uniform handle과 6px hit, zoom 불변, translate·rotation보다 낮고
entity·box보다 높은 input precedence를 확인했다. Root/reflected skeleton scale, non-uniform·negative
determinant `normal` parent, `onlyTranslation`, unsupported inherit, singular/degenerate/NaN basis를
검증했고 signed ratio sign crossing, exact-zero 74px fallback, uniform 양축 sign flip, 한 축 zero 보존,
`(0,0)` uniform 숨김을 다뤘다. Gesture smoke는 mixed-selection active Bone 하나의 effective scale
materialization, imported key/curve 보존, playback pause, live preview, no-op, one undo/redo와 cancel/
non-finite rollback을 확인했다. Runtime unit 31개, SelectionSet 10개, 56-operation Agent registry,
C ABI v1, editor CTest 7/7, 전체 CTest 13/13 및 JSON-MBIN comparison을 통과했다.

#### 뷰포트 직접 조작

| Story | Title | 수직 슬라이스 |
| --- | --- | --- |
| MAR-161 | Rotation gizmo (완료, 2026-07-21) | 58px screen-space ring이 root/`onlyTranslation` skeleton-scale 또는 `normal` evaluated-parent basis를 gesture 시작 시 freeze한다. Per-sample `(-180, 180]` unwrap, 2px pivot rebase, raw multi-turn absolute key, live transaction/one undo를 제공하고 나머지 inherit mode는 숨김+hint로 처리한다. |
| MAR-162 | Scale gizmo (완료, 2026-07-25) | 74px screen-space local X/Y/uniform handle이 frozen positive scale-free rotation/shear axis에서 signed ratio와 exact-zero 74px fallback을 계산한다. Uniform은 시작 X:Y ratio/sign을 보존하고 `(0,0)`에서는 숨긴다. Active Bone 하나만 effective scale track transaction으로 auto-key하며 unsupported inherit·singular·non-finite는 전체 rollback한다. |
| MAR-163 | Single-vertex FFD | effective deform 전체를 materialize한 뒤 한 vertex를 auto-key한다. Weighted vertex는 가중 bone 2×2 행렬 합의 inverse를 사용한다. |
| MAR-164 | Multi-vertex FFD | attachment-local click/toggle/box selection과 한 gesture·한 transaction group move를 추가한다. 하나라도 singular면 전체 gesture를 취소한다. |
| MAR-165 | Shared transform snapping | 프로젝트별 snap metadata와 world grid 10 units, local angle 15°, absolute scale 0.1 snap을 translate/rotate/scale에 공통 적용한다. 기존 프로젝트 기본은 OFF, Alt는 우회, Ctrl/Cmd는 gesture 동안 임시 활성화한다. |
| MAR-166 | FFD vertex snapping | grid와 보이는 모든 비선택 mesh vertex 대상 8px magnetic snap을 추가한다. Vertex snap이 grid보다 우선하고 screen distance 뒤 stable identity로 tie-break한다. |

#### 그래프·타임라인 생산성

| Story | Title | 수직 슬라이스 |
| --- | --- | --- |
| MAR-167 | Graph view | Transform R/T/S/shear와 Slot RGBA scalar series를 표시하고 dopesheet playhead/key selection을 공유한다. FFD와 discrete lane은 제외한다. |
| MAR-168 | Graph point editing | active component 값과 key 전체 시간을 drag한다. 시간 충돌·frame snap·duration auto-grow는 공용 authoring primitive를 사용한다. |
| MAR-169 | Graphical Bezier handles | outgoing segment의 공용 `[cx1,cy1,cx2,cy2]`를 drag한다. X는 `[0,1]`, Y는 finite overshoot를 허용하며 한 drag는 한 undo다. |
| MAR-170 | Curve presets/default | Linear, Stepped, Ease, Ease-In, Ease-Out, Ease-In-Out 고정값을 제공하고 마지막 기본 curve를 user preference에 저장한다. |
| MAR-171 | Automatic handles | `.marrow` 전용 manual/auto curve metadata와 driver component를 저장한다. Auto는 driver scalar에 monotone Fritsch–Carlson tangent를 적용하고 handle drag는 manual로 전환한다. |
| MAR-172 | Persistent loop synchronization | lane별 opt-in metadata를 저장한다. 명시적 duration과 time 0 key가 있을 때만 duration boundary key를 관리하고 첫 값·curve·duration 변화를 같은 transaction에서 동기화한다. |
| MAR-173 | Selected-key time scaling | 선택 범위 반대 edge를 pivot으로 양의 비율 scaling을 제공한다. Event tie는 보존하고 비-event 충돌·이웃 침범은 원자적으로 거부한다. |
| MAR-174 | Preview playback speed | transient 0.05×~8×와 0.25/0.5/1/2× preset을 제공하고 reverse와 합성한다. project dirty/history에는 포함하지 않는다. |

Wire easing은 segment 전체에 공용이므로 graph의 X/Y 또는 RGBA component별 독립 curve는 P1에서도 만들지 않는다.

#### 웨이트·제약

| Story | Title | 수직 슬라이스 |
| --- | --- | --- |
| MAR-175 | Unified manual weight authoring | brush와 agent의 중복 정규화를 하나의 domain primitive로 합친다. Replace brush, active-vertex numeric table, selected-scope Normalize와 setup-pose Rebind를 제공한다. |
| MAR-176 | Deterministic auto weights | 명시적 candidate bone 체크 목록에서 setup-world vertex와 bone segment의 inverse-square distance로 상위 4개를 선택한다. Skeleton order로 tie-break하고 zero-length/isolated vertex는 nearest candidate로 fallback한다. |
| MAR-177 | Constraint lifecycle schema | `.marrow.constraint_edits.operations`에 ordered rename/delete를 추가한다. Base-backed constraint는 operation/tombstone, project-only constraint는 upsert 직접 변경으로 표현한다. |
| MAR-178 | Constraint rename/delete surfaces | IK/Path/Transform/Physics GUI·confirmation·undo·dry-run·agent/MCP를 추가하고 root arrays, `skins[].constraints`, upsert, `SelectionSet`을 원자적으로 cascade한다. |
| MAR-179 | Complete constraint widgets | IK softness/compress/stretch와 Physics step/x/y/rotate/scaleX/shearX/limit/massInverse UI를 추가하고 IK MCP 누락 필드를 보완한다. |

모든 weight 결과는 non-positive 제거, duplicate bone 병합, weight 내림차순+skeleton order 정렬, top-4 제한, 합계 1 정규화를 동일하게 적용한다.

#### 파일·inherit 워크플로

| Story | Title | 수직 슬라이스 |
| --- | --- | --- |
| MAR-180 | Atomic project I/O | 같은 디렉터리 temp+rename 저장, Save As 상대 asset/export/atlas-pack/PSD 경로 rebase, `EditorSession::create/close`와 atomic runtime-source adoption을 구현한다. |
| MAR-181 | File path UI | 외부 의존성 없는 ImGui directory/path modal과 New/Open/Save/Save As를 연결한다. New는 skeleton·최소 한 atlas·대상 `.marrow` 경로를 검증한 뒤 dirty in-memory session으로 시작한다. |
| MAR-182 | Dirty intent state machine | New/Open/Reload/Quit/OS close를 Save/Discard/Cancel로 통합한다. Save 실패나 Cancel은 현재 session과 pending intent를 유지한다. |
| MAR-183 | Recent Projects | 성공한 New-save/Open/Save As만 canonical absolute path로 최대 10개 저장한다. 사라진 경로는 disabled로 남겨 Remove/Clear Missing을 제공한다. |
| MAR-184 | Inherit overlay | runtime과 같은 5개 mode의 stepped-only inherit timeline project schema, materialization, merge/export primitive를 추가한다. |
| MAR-185 | Inherit editing parity | Add/Edit/Remove, selection, retime, scale, clipboard, animation rename/delete cascade와 `set/remove_inherit_keyframe` agent/MCP를 연결한다. |

#### Problems·PSD 재임포트·완료 검증

| Story | Title | 수직 슬라이스 |
| --- | --- | --- |
| MAR-186 | Structured diagnostics | stable code/severity/message/typed target/safe-fix ID collector를 만든다. `project.diagnostics`의 기존 summary를 보존하며 `issues`와 count를 추가한다. |
| MAR-187 | Problems view | severity grouping/filter, target selection·panel focus, revision refresh와 allowlist safe fix를 제공한다. Inspection은 무변경이고 fix만 transaction+undo를 쓴다. |
| MAR-188 | PSD reimport planning | `.marrow.editor.import_sources.psd`에 project-relative provenance와 layer mapping을 저장하고 exact name/group 기반 added/updated/missing staging diff를 계산한다. Rename은 추론하지 않는다. |
| MAR-189 | Atomic PSD commit | 현재 overlay까지 staged bundle로 검증하고 layer directory/texture/atlas/skeleton을 journaled rename으로 교체한다. 모든 단계 rollback과 승인 agent import의 실제 경로 사용을 검증한다. |
| MAR-190 | PSD reimport GUI | preview/confirmation과 missing-layer checklist를 연결한다. Missing은 기본 보존하고 명시 선택만 삭제하며 성공 시 unsaved overlay/history를 유지한 채 runtime source를 교체한다. |
| MAR-191 | P1 E2E and docs | P1 상호작용, JSON/MBIN export, GUI/agent registry parity, save/reload, failpoint rollback을 종합 검증하고 roadmap·문서·AGENTS 명령을 최종 동기화한다. |

Problems safe-fix 최초 allowlist는 orphan overlay 제거, weight canonical normalize, stale preview reference reset만 포함한다.
정상적으로 열린 session만 진단하며 최초 open 실패의 partial/degraded loading이나 자동 수정은 제외한다.

#### P1 검증 기준

- **Runtime unit**: explicit/inferred/empty duration, loop·queue·reverse·snapshot, curve flat/overshoot/auto tangent, auto-weight determinism.
- **Project smoke**: 모든 신규 optional schema의 old-project compatibility, constraint rename/delete cascade, inherit/curve metadata, Save As path rebase, JSON↔MBIN equivalence.
- **Selection unit/shell smoke**: typed identity scope, stable order와 active fallback, prune/remap collision, invalid-range atomicity, transient project/preview/runtime/history/revision invariants.
- **Shell smoke**: transformed/reflected/singular gizmo, signed/zero scale, weighted/unweighted/linked FFD, multi-select, snap tie-break, graph point/handle cancel, dirty modal, Problems navigation.
- **Agent/MCP smoke**: 신규 operation metadata·dry-run·mutation·undo·export preview와 C++/Python registry parity.
- **PSD smoke**: missing-preserve/delete, animation·overlay 보존, parse/build 및 각 commit failpoint 뒤 대상 bundle의 byte-for-byte rollback.
- 각 milestone checkpoint는 build와 관련 focused test를 통과한다. MAR-191은 `cmake --build build`, 전체 CTest와 runtime/editor label,
  project/agent/PSD smoke, MCP schema/client, renderer `--skip-render`, `git diff --check`를 실행한다.

### 보류 — canonical authoring graph 선행

본·슬롯·스킨·어태치먼트와 메시 topology 자체 저작, 경로 제어점 편집, group transform, partial project open은 현재 승인된 milestone scope에 넣지 않는다. 재개하려면 이름 기반 overlay를 더 확장하는 대신 version/stable ID를 가진 canonical `.marrow` authoring graph, 임포트의 일회성 전환, 기존 프로젝트 migration/compatibility 경계를 먼저 설계한다.

### 참고: 에이전트(MCP) 표면과의 비대칭

에이전트 표면은 MAR-155 duration operation을 포함해 현재 56 ops다. P0의 animation CRUD와 atomic timeline retime에 더해 MAR-128의
`parameters.list`, `parameter.set`, `deformer.create`, `keyform.capture`, `expression.create`, `lip_sync.map`을
C++ registry와 Python MCP 도구에 함께 노출했다. Transform/slot/parameter authoring은 GUI와 agent가 base materialization
또는 candidate runtime build를 포함한 UI-free mutation을 공유한다. GUI는 의도적으로 더 넓은 parameter/group/shape lifecycle과
warp lattice·rotation pivot gesture를 제공하며, agent는 계획에 확정된 inspection 1개와 mutation 5개만 제공한다.

P1의 **지속 mutation**인 duration, interpolation/curve mode/loop sync, time scaling, weight bind/auto-weight,
constraint lifecycle, inherit key, PSD reimport는 C++ operation registry와 Python MCP에 동시에 노출한다. 순수 selection,
graph 표시, file dialog, transient playback speed는 agent operation 대상이 아니다. GUI와 agent가 함께 제공되는 mutation은
같은 UI-free domain primitive, dry-run, undo/export-preview 경계를 공유한다.

---

## 부록: 조사 출처

- 코드: `src/runtime/skeleton_animation.cpp`, `src/runtime/skeleton_parse.cpp`, `include/marrow/runtime/animation_compare.hpp`, `src/tests/runtime_math_tests.cpp`, `src/samples/runtime_fixture_smoke.cpp`, `src/runtime/parameter_*.cpp`, `src/renderer/module.cpp`, `src/editor/` 전수 (`shell_parameters.cpp`, `parameter_project_model.cpp`, `agent_handlers_parameters.cpp`, `shell_timeline.cpp`, `shell_constraints.cpp`, `shell_weight_paint.cpp`, `shell_viewport*.cpp`, `session.cpp`, `project.cpp` 등)
- 로드맵: `docs/root1/refector.md`, `docs/root1/format-spec.md`, `.agents/tasks/prd-marrow-runtime.json`
- Spine: [4.2 릴리스](https://esotericsoftware.com/blog/Spine-4.2-The-physics-revolution) · [4.3 릴리스](https://esotericsoftware.com/blog/Spine-4.3-released) · [User Guide](https://en.esotericsoftware.com/spine-user-guide)
- Live2D: [Cubism 5.0](https://docs.live2d.com/en/cubism-editor-manual/new-function5-0/) · [5.1](https://docs.live2d.com/en/cubism-editor-manual/new-function5-1/) · [5.2](https://docs.live2d.com/en/cubism-editor-manual/new-function5-2/) · [5.3](https://docs.live2d.com/en/cubism-editor-manual/new-function5-3/)
