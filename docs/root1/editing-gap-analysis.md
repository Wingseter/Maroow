# Maroow 편집 기능 갭 분석 (vs Spine 4.2/4.3, Live2D Cubism 5.x)

작성일: 2026-07-12. 기준: `agent-control-remaining` 브랜치의 에디터 아키텍처 리팩터링 HEAD `4c93ca1`과 MAR-141~153 편집 P0가 구현된 현재 워킹 트리.
조사 방법: 에디터 소스 전수 조사 + Spine/Live2D 공식 문서 확인.

---

## TL;DR

Maroow 에디터의 강점은 **"이미 존재하는 리그 위에서의 애니메이션 저작"**에 집중되어 있다.
편집 P0 이후에는 트랜스폼 auto-key, 본/IK 타깃 이동 기즈모, 슬롯/디폼/이벤트/드로우오더 키잉,
다중 키 선택·리타임·복사/잘라내기/붙여넣기, 애니메이션 CRUD, 4종 제약 저작, 웨이트 페인팅,
어니언 스킨, 트랜잭션 기반 언두와 49개 에이전트 오퍼레이션을 제공한다.

반면 Spine/Live2D 대비 가장 큰 격차는 네 가지다:

1. **뷰포트 직접 조작의 P1 범위** — P0 이동 기즈모는 구현됐지만 회전·스케일, FFD 버텍스 직접 편집, grid/angle/vertex snap은 아직 없다.
2. **리그/메시 저작 불가** — 본·슬롯·스킨·어태치먼트·메시 지오메트리를 에디터에서 생성/삭제/편집할 수 없다(임포트 전용). 이는 오버레이 아키텍처의 의도된 결과지만, "에디터"로서는 결정적 제약.
3. **고급 타임라인 UX 미구현** — P0 도프시트는 완성됐지만 그래프 에디터, 그래픽 베지어 핸들, 리타임 스케일, 커브 프리셋/자동 핸들/루프 동기화는 P1이다.
4. **Live2D식 파라미터 모델링 레이어 미구현** — 포맷 스펙과 로드맵(MAR-121~128)만 존재.

---

## 1. 현재 편집 기능 인벤토리 (요약)

### 아키텍처 전제: 오버레이 모델

`ProjectData`(`include/marrow/editor/project.hpp`)는 본/슬롯/스킨/어태치먼트 컨테이너를 갖지 않는다.
베이스 `.mskl` 스켈레톤에 대한 **참조** + 이름 키 기반 **편집 오버레이**만 저장하고,
`build_runtime_document`(`src/editor/project.cpp`)가 베이스를 복사한 뒤 오버레이를 병합한다.

- 에디터에서 **생성 가능**: 새 애니메이션·타임라인, IK/경로/트랜스폼/물리 제약(이름 기반 upsert), 기존 메시의 웨이트/디폼 재저작
- **임포트 전용**(에디터 생성 불가): 본, 슬롯, 스킨, 어태치먼트, 메시 버텍스/트라이앵글

### 잘 되어 있는 것

| 영역 | 내용 | 위치 |
|---|---|---|
| 웨이트 페인팅 | Paint/Erase/Smooth 브러시, 반경/강도, 히트맵, 자동 바인딩, top-4 정규화, 스트로크 단위 언두 | `shell_weight_paint.cpp` |
| 키프레임 편집 | 본 R/T/S/shear, 슬롯 light RGBA/attachment, 메시 디폼, 드로우오더, 이벤트 — 추가/삭제/시간·값·보간 수정 | `shell_timeline.cpp` |
| 보간 | 키별 Linear/Stepped/Bezier + 베지어 제어점 4개 숫자 입력 | `shell_timeline.cpp` |
| 재생 | 재생/일시정지(Space), 루프, 역재생, 스크럽, 이전/다음 키 스텝, 애니메이션 큐+믹스 프리뷰 | `shell_timeline.cpp` |
| 뷰포트 저작 | 안정적 카메라, screen/world 역변환, cursor zoom, 명시적 Fit, 본/IK 타깃 X/Y/free 이동 auto-key | `shell_viewport.cpp`, `shell_viewport_ui.cpp` |
| 도프시트 | 60 FPS 눈금자, 독립 zoom/pan, 안정적 키 identity, toggle/box 선택, 다중 리타임, typed clipboard | `shell_timeline.cpp` |
| 애니메이션 관리 | create/duplicate/rename/delete, 확인 UI, ordered `.marrow.animation_edits`, queue/preview cascade | `authoring.cpp`, `shell_project_panels.cpp` |
| 제약 저작 | IK/경로/트랜스폼/물리 4종 모두 추가+파라미터 편집, 영구 저장+언두 | `shell_constraints.cpp` |
| 언두/트랜잭션 | 스냅샷 100개 캡, 머지 키 그룹핑, 원자적 런타임 리빌드+실패 롤백 | `session.cpp` |
| 어니언 스킨 | 프레임/키프레임 모드, 전후 개수, 스텝, 앵커 | `shell_viewport_ui.cpp` |
| 임포트/익스포트 | PSD→리그 생성, Spine JSON/atlas 임포트, 아틀라스 패킹, `.mskl`/`.mbin`/`.matl` 익스포트 | `psd_import.cpp` 등 |
| 에이전트 표면 | 49개 오퍼레이션(조회 11, 검증 3, 관리 10, 편집 25). Animation CRUD와 atomic timeline retime은 MCP 도구에도 노출 | `agent_dispatch.cpp`, `tools/mcp/tools/editing.py` |
| P0 회귀 방지 | base-only timeline materialization, save/reload, undo/redo, JSON/binary export, 49-op registry를 headless smoke로 검증 | `editor_project_smoke.cpp`, `shell_smoke.cpp`, `agent_dispatch_smoke.cpp` |

### 남아 있는 의도적 제한/부분 구현

- **Setup Pose/슬롯 setup 색상은 의도적으로 read-only** — Animation 모드 본 포즈는 항상 playhead auto-key이고, 슬롯 light/attachment는 timeline editor에서 저작한다. 저장되지 않는 preview-only 포즈/색상 입력은 없다.
- **inherit timeline은 read-only** — 전용 편집은 MAR-170으로 미뤘다.
- **뷰포트는 이동까지만 직접 저작** — rotate/scale, FFD vertex, snap은 MAR-155~158이다. 경로 제어점 직접 조작도 아직 없다.
- **그래프 에디터 없음** — 숫자 Bezier 입력은 가능하지만 그래픽 핸들/프리셋/자동 핸들/루프 동기화는 MAR-159~161이다.
- **제약 파라미터 일부 위젯 없음** — IK softness/compress/stretch, Physics step/x/y/rotate/scaleX/shearX/limit/massInverse (라운드트립은 됨).
- **제약 삭제/이름변경 불가** — MAR-166~167 범위다.

---

## 2. 갭 분석 — Spine/Live2D 참조 기능 대비

우선순위 표기: 🔴 결정적(경쟁 도구의 CORE 워크플로), 🟡 중요(생산성 격차 큼), 🟢 편의(있으면 좋음).

### 2.1 뷰포트 직접 조작 — P0 기반 완료, P1 확장 필요

Spine 편집의 본질은 "뷰포트에서 본을 잡아 끄는 것"이고, Live2D는 "캔버스 위에서 디포머/파라미터를 직접 조작하는 것"이다. Maroow P0는 이 층의 최소 수직 슬라이스를 구현했다.

| 기능 | 현재 상태 | 후속 |
|---|---|---|
| 본/IK 타깃 X/Y/free 이동 | 구현. 부모 2x2 inverse, singular rollback, 한 drag 한 undo, auto-key | P0 유지 |
| 안정적 카메라/`world_from_screen`/cursor zoom/Fit | 구현. 포즈 변화가 카메라를 재프레이밍하지 않음 | P0 유지 |
| rotate/scale 기즈모 | 미구현 | MAR-155~156 |
| grid/angle/vertex snap | 미구현 | MAR-157 |
| FFD 버텍스 직접 편집 | 미구현; 숫자/도프시트 저작은 가능 | MAR-158 |
| 경로 제어점 직접 조작 | 미구현 | 후속 범위 결정 필요 |

초기 선결 과제였던 `world_from_screen`과 포즈 bounds에서 분리된 카메라는 MAR-143에서 해결됐다.

### 2.2 포즈 편집의 신뢰성 — P0에서 해결

- Setup 모드는 임포트 setup data를 읽기 전용으로 표시한다.
- Animation 모드의 inspector R/T/S/shear와 viewport 이동은 현재 playhead의 절대 local key를 upsert한다.
- 첫 편집 전에 effective/base timeline 전체를 project overlay로 materialize하므로 임포트 키를 단일 신규 키로 덮어쓰지 않는다.
- live `EditTransaction`이 반복 preview refresh, 한 gesture 한 history entry, Escape/실패의 정확한 rollback을 담당한다.
- GUI와 agent setter는 같은 UI-free project-domain authoring primitive를 사용한다.

### 2.3 타임라인/애니메이션 워크플로 — P0 도프시트 완료, graph/P1 잔여

| 남은 기능 | 참조 (Spine) | 참조 (Live2D) |
|---|---|---|
| 그래프(커브) 에디터 — 그래픽 베지어 핸들 드래그 | Graph editor, 4.3 자동 조정 핸들·기본 커브 기억·루프 커브 동기화 [CORE] | Graph editor [CORE] |
| 선택 키 시간 스케일 | Dopesheet [CORE] | Timeline [CORE] |
| 재생 속도 조절 | [CORE] | [CORE] |
| 명시적 clip duration | Animation duration | 씬/타임라인 길이 |
| 루프 경계 키 자동 복제 | 루프 커브 동기화 (4.2) | 루프 편집 지원 (5.2) |
| 키 인터폴레이션 프리셋 | 커브 프리셋 | 확장 보간 |

현재 도프시트는 초/프레임 ruler, duration과 독립된 zoom/pan, 안정적 same-time event identity,
클릭/Cmd·Ctrl toggle/box selection, Add/Remove, 다중 drag retime, 60 FPS snap/Alt bypass,
typed copy/cut/paste와 compatible single-lane remap을 제공한다. 애니메이션 create/duplicate/rename/delete도
UI와 agent/MCP 양쪽에 있다. P0에서는 clip 길이를 마지막 키에서 계속 추론하며 optional explicit duration은 MAR-154다.

### 2.4 메시/웨이트 저작 — 🟡 (아키텍처 결정 필요)

| 부재 기능 | 참조 (Spine) | 참조 (Live2D) |
|---|---|---|
| 메시 지오메트리 편집 (버텍스 추가/이동/삭제, 엣지/헐) | Mesh 편집 [CORE] | 수동 메시 편집 [CORE] |
| 오토메시 (이미지 알파에서 자동 생성) | 4.3 다중 트레이스+균일 슬라이더 [CORE] | 자동 메시 생성기 (5.0 개선) [CORE] |
| 오토 웨이트 (Bind + 자동 계산) | Bind+auto weights [CORE] | 스키닝 자동화 |
| 웨이트 Replace 모드·수치 직접 편집·명시적 정규화 버튼 | 웨이트 툴 | — |
| 웨이트 복사/붙여넣기, Weld(메시 간 이음새) | [+] | Glue [CORE] |

메시 지오메트리는 오버레이 모델상 편집 불가(베이스 문서 소유). `mesh_edits`에 `weights`만 있는 현재 스키마를 지오메트리 오버라이드로 확장할지, 베이스 문서 편집을 허용할지 **설계 결정이 선행**되어야 한다.

### 2.5 리그 구조 편집 — 🟡 (전략 결정 필요)

본/슬롯/스킨/어태치먼트의 생성·삭제·이름변경·재부모화가 GUI에도 에이전트 표면에도 전혀 없다. "Spine/PSD에서 임포트한 리그의 후처리 에디터"가 의도된 포지셔닝이라면 낮은 우선순위로 두되, **자체 저작 도구**를 지향한다면 오버레이 모델 확장(추가 엔티티 오버레이) 또는 베이스 문서 직접 편집이 필요하다. 이 결정이 2.4보다 상위의 갈림길이다.

### 2.6 Live2D식 파라미터 모델링 — 🟡 (이미 로드맵 존재)

포맷 스펙(`format-spec.md`의 `parameters`/`parameterShapes`/`parameterDeformers`/`artPaths`/`expressions`/`lipSync`)과 open 스토리(MAR-121~128)가 정의되어 있지만 아직 구현되지 않았다. 참조할 Live2D 대응물:

- 파라미터 슬라이더 + 키폼 편집 → Cubism 파라미터 시스템 [CORE]
- 워프/회전 디포머 → Cubism 디포머 [CORE] (5.3의 부모 그리드 자동 확장 참고)
- 블렌드셰이프 → Cubism 5.0 "blend shapes everywhere"
- 표정/립싱크 → 표정 프리셋, Motion Sync(5.0)
- (차별화 아이디어) Cubism 5.2 **파라미터 컨트롤러**(캔버스 위 드래그로 파라미터 묶음 조작, 타깃 추종)는 도입 시 강력한 UX

### 2.7 워크플로/편의 기능 — 🟢~🟡

| 부재 기능 | 참조 | 심각도 |
|---|---|---|
| 전역 entity 멀티 셀렉트/박스 셀렉트 (도프시트 키 선택은 P0에서 구현) | 양쪽 다 [CORE] | 🟡 — 이후 hierarchy/viewport 일괄 편집의 전제 |
| File 메뉴: New/Open/Save/Save As/Recent Projects | 양쪽 다 [CORE] | 🟡 — Save가 툴바에만 있음 |
| 대칭 편집 — 반전 붙여넣기/미러 | Live2D 5.2 반전 형상 붙여넣기, Spine flip | 🟢 |
| Problems 뷰 (경고 목록 + 클릭 이동 + 원클릭 수정) | Spine 4.3 [+] | 🟢 — `project.diagnostics`가 이미 있어 UI만 얹으면 됨 |
| 숫자 필드 수식 입력 (`10 + v * 8`) | Spine 4.3 [+] | 🟢 |
| PSD 재임포트 GUI | Spine 4.3 PSD 관리, Live2D 재임포트 [CORE] | 🟡 — 이름 기반 레이어 동기화·기존 애니메이션 보존 엔진과 smoke는 구현됨. 에디터 메뉴/확인/결과 UI만 없음 |
| 계층 검색/일괄 치환 | Live2D 검색·치환 [+] | 🟢 — 계층 텍스트 필터는 있음 |
| 키보드 단축키 체계 + 단축키 도움말 | 양쪽 다 | 🟢 — 현재 Space/Ctrl+Z 등 소수 |
| 비디오/GIF 익스포트, HTML 프리뷰 익스포트 | Spine [CORE]/[+] | 🟢 |

---

## 3. 확정 로드맵

### 제품 결정

- 단기 제품은 **임포트 리그 기반 애니메이션/후처리 에디터**다.
- Setup Pose와 슬롯 dark tint는 P0에서 읽기 전용이다. Animation 모드의 R/T/S/shear 변경은 항상 현재 playhead의 키로 영속화한다.
- P0 뷰포트는 안정적 카메라와 본/IK 타깃 **이동** 기즈모까지다. 회전·스케일·FFD 직접 조작과 snap은 P1이다.
- P0 애니메이션 길이는 계속 마지막 키에서 추론한다. 명시적 clip duration은 MAR-154에서 호환 가능한 optional 필드로 추가한다.
- MAR-129~140 리팩터링은 HEAD `4c93ca1`에서 완료됐다. MAR-137은 constraint 모듈 추출만 완료한 것이며 rename/delete는 MAR-166이다.

### P0 — MAR-141~153 (현재 워킹 트리 구현 완료)

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

PRD 배열은 이 스토리를 MAR-120 직후, MAR-121 전에 둔다. P0 구현 체크포인트가 끝났으므로 다음 활성 제품 트랙은 MAR-121~126 런타임, MAR-127 editor, MAR-128 agent 순서의 파라미터 모델링이다.

### P1 — MAR-154~172

1. MAR-154 optional explicit clip duration.
2. MAR-155~158 rotate/scale 기즈모, grid·angle·vertex snap, FFD vertex 직접 편집.
3. MAR-159~161 graph view, Bezier handle, preset/auto handle/loop 동기화.
4. MAR-162~163 Replace·수치·Normalize weight 도구와 결정적 auto-weight.
5. MAR-164~165 전역 `SelectionSet`과 hierarchy/viewport multi-select.
6. MAR-166~167 constraint rename/delete와 누락 IK/Physics 위젯.
7. MAR-168~169 New/Open/Save As/dirty prompt와 Recent Projects.
8. MAR-170 inherit timeline 편집, MAR-171 Problems view, MAR-172 기존 PSD 재임포트 엔진의 GUI 노출.

### 보류 — canonical authoring graph 선행

본·슬롯·스킨·어태치먼트와 메시 topology 자체 저작은 활성 Ralph 큐에 넣지 않는다. 재개하려면 이름 기반 overlay를 더 확장하는 대신 version/stable ID를 가진 canonical `.marrow` authoring graph, 임포트의 일회성 전환, 기존 프로젝트 migration/compatibility 경계를 먼저 설계한다.

### 참고: 에이전트(MCP) 표면과의 비대칭

에이전트 표면은 현재 49 ops다. P0에서 animation CRUD와 atomic timeline retime을 C++ registry와 Python MCP 도구에 함께 노출했고, transform/slot authoring은 GUI와 agent가 base materialization을 포함한 UI-free mutation을 공유한다. GUI에만 남아 있는 대표 기능은 웨이트 브러시다. 이후 신규 기능도 단일 operation registry와 project-domain authoring 경계를 통해 GUI/agent 양쪽을 함께 여는 것을 기본값으로 유지한다.

---

## 부록: 조사 출처

- 코드: `src/editor/` 전수 (`shell_timeline.cpp`, `shell_constraints.cpp`, `shell_weight_paint.cpp`, `shell_viewport*.cpp`, `shell_inspector.cpp`, `session.cpp`, `agent_dispatch.cpp`, `project.cpp` 등)
- 로드맵: `docs/root1/refector.md`, `docs/root1/format-spec.md`, `.agents/tasks/prd-marrow-runtime.json`
- Spine: [4.2 릴리스](https://esotericsoftware.com/blog/Spine-4.2-The-physics-revolution) · [4.3 릴리스](https://esotericsoftware.com/blog/Spine-4.3-released) · [User Guide](https://en.esotericsoftware.com/spine-user-guide)
- Live2D: [Cubism 5.0](https://docs.live2d.com/en/cubism-editor-manual/new-function5-0/) · [5.1](https://docs.live2d.com/en/cubism-editor-manual/new-function5-1/) · [5.2](https://docs.live2d.com/en/cubism-editor-manual/new-function5-2/) · [5.3](https://docs.live2d.com/en/cubism-editor-manual/new-function5-3/)
