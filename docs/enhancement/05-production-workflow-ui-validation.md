# 05. 프로덕션 워크플로우 UI/검증 고도화 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Maroow 에디터를 기능 구현 완료 상태에서 실제 제작자가 오래 쓰는 프로덕션 툴 상태로 끌어올린다.

**Architecture:** 런타임 포맷과 코어 애니메이션 모델은 유지하고, Dear ImGui shell의 정보구조, command surface, diagnostics, import/export guidance, validation reporting을 작은 UI/workflow slices로 개선한다. 모든 변경은 existing smoke tests와 fixture round-trip을 유지해야 한다.

**Tech Stack:** C++17, Dear ImGui docking, OpenGL/sokol editor viewport, existing project/export/import smoke tests, markdown docs/tutorial fixtures.

---

## 목표

Maroow 에디터를 기능 구현 완료 상태에서 실제 제작자가 오래 쓰는 프로덕션 툴 상태로 끌어올린다. 범위는 Dear ImGui 기반 UI 정리, 작업 흐름 발견성, 타임라인/그래프 편집 UX, 프로젝트/애셋 검증, import/export 편의, 진단/복구, 문서와 튜토리얼 샘플까지다. 런타임 포맷과 코어 애니메이션 모델은 유지하고, 기존 smoke test와 fixture 기반 검증을 깨지 않는 작은 수직 슬라이스로 진행한다.

## 현재 상태와 근거

- 에디터 셸은 이미 뷰포트, 타임라인, 계층, 속성, 런타임 애셋, 제약, 프로젝트 패널을 가진 Dear ImGui 도킹 앱이다. 시각 리뉴얼 명세는 7개 패널의 위계 약화, 메뉴/툴바/뷰포트/타임라인에 흩어진 액션, 낮은 타임라인 스캔성, setup/animation/weight-paint 모드의 불명확성, 에이전트 UI 부재, 디자인 시스템 부분 적용을 주요 문제로 지적한다. 근거: `docs/design/maroow-editor-visual-renewal-spec.md`.
- 프로젝트의 기본 방향은 `Marrow Editor -> .mskl / .matl / .png -> molga-engine Runtime`이며, 에디터 UI는 C++/Dear ImGui, 렌더링은 sokol 기반이다. JSON 런타임 포맷과 `.mbin` 생산 포맷은 이미 문서화되어 있다. 근거: `docs/root1/discription.md`, `docs/root1/format-spec.md`.
- 검증용 fixture는 `.mskl`, `.mbin`, `.matl`, `.marrow`, Spine import, PSD import, atlas packer까지 확장되어 있다. 특히 `assets/fixtures/player_idle.marrow`는 프로젝트 편집, export, binary 비교, renderer preparation을 잇는 선호 E2E 경로다. 근거: `docs/root1/fixtures.md`.
- 현재 AGENTS 검증 목록에는 `marrow_editor_shell`, `marrow_project_smoke`, `marrow_renderer_sample`, `marrow_fixture_smoke`, `marrow_inspect`, `marrow_atlas_packer_smoke`, Spine/PSD import smoke가 포함되어 있다. MAR-119 E2E 결과는 편집, 선택, 웨이트 페인트, onion skin, export, undo/redo가 headless smoke와 round-trip export로 검증됨을 보여준다. 근거: `AGENTS.md`.

## 범위 경계

포함:

- 에디터 UI polish, 패널 정보구조, action/command 발견성, 모드 인디케이터, 타임라인/Graph editor 사용성.
- 프로젝트/애셋 검증 리포트, import/export wizard, diagnostics/warnings, autosave/backup, 문서와 튜토리얼 샘플.
- 기존 runtime/export/import 경로와의 통합점: 검증 명령, fixture, report payload, export 전 경고, wizard 결과 확인.

제외:

- Live2D식 파라미터/디포머 모델, 파라미터 블렌딩, 모션 파라미터 편집 코어.
- AI agent operation 확장, agent protocol 확장, 신규 자동 편집 op. 단, agent 상태 표시와 사람이 이해할 수 있는 실행/변경 표시는 UI 통합점으로만 다룬다.
- 외부 SDK 생태계, 플러그인 마켓, 게임 엔진별 런타임 SDK. 단, SDK가 소비할 export validation report와 문서 링크는 통합점으로 남긴다.

## 기능 계획

### 1. 시각 리뉴얼 구현

- `docs/design/maroow-editor-visual-renewal-spec.md`의 Charcoal Studio 원칙을 ImGui 제약 안에서 구현한다.
- 톤 층위, restrained accent, 단색 PNG icon tint, 작은 라운딩, 고정밀 간격을 우선한다.
- 마케팅 랜딩 페이지식 hero, 장식 카드, 과한 그라데이션/블러를 피하고, 제작 툴다운 조용한 밀도와 반복 작업 가독성을 유지한다.
- 패널별 최소 크기와 도킹 재배치 상태에서도 깨지지 않는 레이아웃 규칙을 둔다.

### 2. Command/Action 발견성

- 전역 command bar를 도입해 Save, Export, Reload, Undo, Redo, validation, import/export wizard를 한 위치에서 찾게 한다.
- 메뉴바에는 기존 명령을 유지하되 command palette 또는 action search를 추가해 단축키와 상태를 함께 노출한다.
- 뷰포트/타임라인/패널별 컨텍스트 액션은 해당 패널 헤더 또는 compact toolbar로 한정하고, 전역 명령과 시각적으로 분리한다.
- destructive 또는 export-affecting action은 status message와 validation report에 결과를 남긴다.

### 3. 모드 인디케이터

- 셸 상단에 `Setup`, `Animate`, `Weight Paint`, `Constraint`, `Preview` 같은 현재 작업 모드를 segmented control로 표시한다.
- 모드 전환 시 뷰포트 overlay, 브러시 HUD, timeline edit affordance, inspector section이 같은 상태를 공유한다.
- dirty 상태, autosave 상태, agent 연결/실행 상태는 어느 패널을 보고 있어도 읽히는 글로벌 status strip에 둔다.

### 4. Timeline Scanability와 Graph Editor UX

- 트랙을 bone/slot/deform/drawOrder/event/constraint 계열로 그룹화하고, 아이콘과 낮은 채도의 색 코딩을 적용한다.
- 키 레인은 No-Line 원칙을 유지하되 row tone, major/minor tick contrast, playhead accent를 강화한다.
- 고급 preview option(queue, mix, reverse)은 접힘 영역으로 보내고 transport, current time, selected key 정보는 항상 보이게 한다.
- Graph editor는 첫 단계에서 선택 트랙의 curve 편집과 interpolation 표시만 제공한다. 전체 도프시트 대체가 아니라 현재 timeline의 보조 편집 표면으로 시작한다.
- 선택 키, 인접 키, 현재 playhead 값, tangent/Bezier control 상태를 inspector와 동기화한다.

### 5. 애셋/프로젝트 검증 리포트

- `.marrow` 프로젝트 기준으로 runtime refs, missing file, version mismatch, atlas region mismatch, orphan attachment, invalid curve, non-normalized weights, export path 문제를 한 번에 검사한다.
- 리포트는 UI 패널, export wizard, CLI smoke 결과가 같은 severity 체계(`info`, `warning`, `error`)를 쓰게 한다.
- 오류는 export 차단, 경고는 export 허용 + report 기록으로 구분한다.
- fixture 기반 검증과 연결해 `player_idle.marrow`, atlas pack fixture, Spine/PSD import fixture가 리포트 예제를 제공하게 한다.

### 6. Import/Export Wizard

- Export wizard는 대상 형식(`.mskl`, `.mbin`, `.matl`, atlas copy), 출력 경로, binary 비교, renderer preparation check를 단계별로 보여준다.
- Import wizard는 Spine JSON/atlas, PSD layer import, 기존 `.mskl/.matl` 참조 프로젝트 생성을 같은 패턴으로 제공한다.
- wizard 완료 화면에는 생성 파일, validation result, 다음 액션(open project, inspect, renderer sample)을 표시한다.
- CLI와 UI가 다른 결과를 내지 않도록 기존 importer/exporter 함수를 호출하고 UI는 orchestration/report에 집중한다.

### 7. Diagnostics와 Warnings

- 패널 하단 status dump 대신 diagnostics panel을 만든다.
- 항목별 source path, object name, field, severity, suggested fix를 보여주고 클릭 시 관련 패널/선택으로 이동한다.
- renderer preparation warning, atlas page warning, weight normalization warning, constraint target warning을 우선 다룬다.
- 장시간 세션에서 경고가 작업을 방해하지 않도록 non-modal 기본, export 전에는 blocking summary를 제공한다.

### 8. Autosave와 Backup

- 명시 저장 모델은 유지하되 `.marrow` 편집 상태에 대해 시간 기반 autosave snapshot을 별도 위치에 쓴다.
- crash/restart 시 복구 후보를 보여주고 원본 파일을 덮어쓰기 전에 diff/metadata를 확인하게 한다.
- export 산출물은 autosave 대상에서 제외한다. runtime export는 사용자의 명시 동작으로만 생성한다.
- backup retention은 프로젝트별 최근 N개와 일자별 1개 보존 같은 단순 정책으로 시작한다.

### 9. 문서와 튜토리얼 샘플

- `player_idle.marrow`를 기준으로 "열기 -> 편집 -> 검증 -> export -> binary compare -> renderer sample" 튜토리얼을 작성한다.
- atlas packer, PSD re-import, Spine import는 별도 짧은 tutorial sample로 분리한다.
- 문서에는 UI 경로와 CLI 검증 명령을 나란히 적어 headless 환경과 interactive 환경 모두에서 같은 결과를 확인하게 한다.
- validation report 예제와 흔한 warning 해결법을 포함한다.

## 단계별 계획

### Phase 1: UI 골격과 명령 발견성

- [ ] 전역 status strip과 mode segmented control을 추가한다.
- [ ] Save/Export/Undo/Redo/Reload/Validate를 command bar로 모은다.
- [ ] 기존 메뉴 명령과 새 command entry가 같은 dispatcher 또는 기존 action path를 호출하는지 확인한다.
- [ ] dirty/autosave/agent 상태 표시의 최소 UI를 추가한다.
- [ ] `marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2` smoke를 유지한다.

### Phase 2: 타임라인/Graph editor 수직 슬라이스

- [ ] 트랙 그룹, 아이콘, row tone, major tick을 timeline table에 적용한다.
- [ ] 선택 트랙 1개에 대한 Graph editor panel 또는 tab을 추가한다.
- [ ] rotate curve의 linear/stepped/Bezier 표시와 선택 key inspector 동기화를 구현한다.
- [ ] preview advanced option을 접힘 영역으로 이동한다.
- [ ] 기존 timeline edit, undo/redo, export round-trip smoke를 통과시킨다.

### Phase 3: 프로젝트/애셋 검증 리포트

- [ ] project validation data model과 severity 체계를 정의한다.
- [ ] missing refs, format version, atlas region, curve encoding, weight normalization 검사부터 구현한다.
- [ ] diagnostics panel과 export 전 blocking summary를 연결한다.
- [ ] `player_idle.marrow`와 atlas pack fixture를 기준으로 report snapshot 또는 smoke assertion을 추가한다.
- [ ] `marrow_project_smoke` export 경로가 기존 결과를 유지하는지 확인한다.

### Phase 4: Import/Export Wizard

- [ ] export wizard 첫 버전: JSON runtime export, optional `.mbin`, validation, compare command 안내.
- [ ] import wizard 첫 버전: Spine JSON+atlas와 PSD import smoke 경로를 UI에서 실행 가능한 작업 단위로 노출한다.
- [ ] wizard 결과 화면에 생성 파일과 diagnostics summary를 표시한다.
- [ ] 실패 시 partial output과 rollback/cleanup 정책을 명확히 한다.
- [ ] 기존 CLI importer/exporter smoke를 그대로 통과시킨다.

### Phase 5: Autosave/Backup과 복구 UX

- [ ] `.marrow` 전용 autosave snapshot writer를 추가한다.
- [ ] 시작 시 복구 후보 탐지와 선택 UI를 제공한다.
- [ ] retention 정책과 파일명 규칙을 문서화한다.
- [ ] autosave가 명시 저장/export 산출물을 덮어쓰지 않는 테스트를 추가한다.
- [ ] 장시간 세션에서 상태 표시가 과도하게 방해되지 않는지 확인한다.

### Phase 6: 문서와 튜토리얼 완성

- [ ] end-to-end sample project tutorial을 작성한다.
- [ ] Spine import, PSD re-import, atlas packer tutorial을 짧은 독립 문서로 작성한다.
- [ ] validation report severity와 흔한 warning 해결법을 문서화한다.
- [ ] AGENTS의 검증 명령이 새 workflow와 모순되지 않게 업데이트한다.
- [ ] headless와 interactive host 차이를 문서에 명시한다.

## 인수 기준

- 에디터 시작 후 현재 모드, 저장 상태, validation 상태, agent 상태를 한 화면에서 즉시 확인할 수 있다.
- Save/Export/Validate/Undo/Redo/Reload가 메뉴 탐색 없이 command bar 또는 palette에서 발견된다.
- 타임라인에서 트랙 종류와 키 밀도, 현재 playhead, 선택 키가 빠르게 구분된다.
- Graph editor 첫 버전은 최소 1개 선택 트랙의 interpolation 상태를 읽고 수정할 수 있으며 undo/redo와 export에 반영된다.
- export 전 validation report가 blocking error와 warning을 구분하고, report 항목에서 관련 프로젝트 객체로 이동할 수 있다.
- import/export wizard는 기존 CLI와 같은 코어 경로를 사용하며, 생성 파일과 검증 결과를 명확히 보여준다.
- autosave는 `.marrow` 편집 복구만 담당하고 runtime export 파일을 암묵적으로 덮어쓰지 않는다.
- 기존 MAR-119 E2E smoke와 renderer/project/runtime smoke가 회귀 없이 통과한다.
- 튜토리얼 문서는 checked-in fixture만으로 따라 할 수 있다.

## 검증 명령

기본 빌드:

```sh
cmake -S . -B build
cmake --build build
```

핵심 에디터/프로젝트 검증:

```sh
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl --export-binary /tmp/player_idle_project_export.mbin
./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin /tmp/player_idle_project_export.mskl
./build/marrow_fixture_smoke /tmp/player_idle_project_export.mskl /tmp/player_idle.matl
./build/marrow_renderer_sample --hud --skip-render assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
```

애셋 파이프라인 검증:

```sh
./build/marrow_atlas_packer_smoke
./build/marrow_project_smoke assets/fixtures/atlas_pack_smoke/atlas_pack_project.marrow --export-runtime /tmp/atlas_pack_project_export.mskl
./build/marrow_spine_import_smoke assets/fixtures/spine_import_sample.json assets/fixtures/spine_import_sample.atlas
./build/marrow_psd_import_smoke assets/fixtures/psd_import_sample.psd assets/fixtures/psd_import_sample_reimport.psd
```

JSON fixture sanity:

```sh
python3 -m json.tool assets/fixtures/player_idle.marrow > /dev/null
python3 -m json.tool assets/fixtures/player_idle.mskl > /dev/null
python3 -m json.tool assets/fixtures/player_idle.matl > /dev/null
```

인터랙티브 확인은 Metal-capable macOS 세션에서 `./build/marrow_editor_shell assets/fixtures/player_idle.marrow`로 수행한다. headless sandbox에서는 renderer window 생성이 실패할 수 있으므로 `--skip-render` 또는 `--auto-close` smoke를 기본 검증으로 둔다.

## 리스크

- ImGui 제약: CSS layout, blur, shadow, flexible grid가 없으므로 visual renewal은 톤, 간격, typography, icon tint, table/draw-list 조합으로 풀어야 한다.
- 장시간 세션 ergonomics: diagnostics, autosave, agent/status 표시가 계속 깜박이거나 과도하게 강조되면 작업 피로를 높인다.
- 마케팅 스타일 UI 회피: 제작 툴은 첫 화면 hero나 장식 카드보다 dense하지만 정돈된 정보 구조가 우선이다.
- smoke test 회귀: UI orchestration을 바꾸면서 기존 dispatcher, undo/redo, export, fixture smoke를 깨뜨릴 수 있다.
- wizard 범위 팽창: importer/exporter 자체 재작성으로 번지면 작게 끝나지 않는다. UI는 기존 코어 경로 호출과 report 통합에 집중한다.
- validation noise: warning이 너무 많으면 무시된다. 처음에는 export를 실제로 망가뜨리는 항목과 fixture에서 재현 가능한 항목을 우선한다.

## 우선순위 추천

1순위는 Phase 1과 Phase 3이다. 전역 모드/상태/command 발견성과 validation report가 먼저 잡혀야 이후 timeline, wizard, autosave가 같은 언어로 붙는다.

2순위는 Phase 2다. 타임라인은 애니메이터의 체감 품질을 가장 크게 좌우하지만, 기존 편집/export 회귀 위험이 있어 작은 Graph editor 보조 표면부터 시작하는 편이 안전하다.

3순위는 Phase 4와 Phase 5다. import/export wizard와 autosave는 프로덕션 안정성을 높이지만, validation report와 command 구조가 자리 잡은 뒤 붙일 때 중복 UI를 줄일 수 있다.

마지막으로 Phase 6 문서는 각 단계의 실제 UI와 report 결과가 확정될 때마다 갱신한다. 단, 최소 end-to-end tutorial은 Phase 3 완료 시점에 먼저 만들어 새 workflow를 검증 루틴으로 고정하는 것이 좋다.
