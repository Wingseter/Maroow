# Maroow 에디터 — 시각적 전면 리뉴얼 디자인 명세서

> **대상**: `claude design` 에이전트
> **목표**: Maroow 2D 스켈레탈 애니메이션 에디터(7개 패널 통합)의 **시각적 전면 리뉴얼**
> **기반**: 기존 *Charcoal Studio* 디자인 시스템(`docs/stitch/charcoal_studio/DESIGN.md`)을 출발점으로 하되,
> 레이아웃·정보구조·비주얼 언어를 적극적으로 재설계한다. Spine / Live2D 대비 차별화된 룩앤필을 제안할 것.
> **작성일**: 2026-05-16

---

## 0. 산출물 요구사항 (Deliverables)

`docs/stitch/<topic>/` 패턴을 따른다. 다음을 제출:

1. **통합 레이아웃 목업** — 7개 패널 + **에이전트 서피스**가 한 화면에 도킹된 메인 워크스페이스 (setup pose 상태 / animation 재생 상태 / **에이전트 구동 중** 3종)
2. **패널별 상세 목업** — 7개 패널 + 에이전트 서피스(§4.8) 각각의 클로즈업 (§4 참조)
3. **상태/모드 변형** — weight-paint 모드, **에이전트 연결/구동/대기 상태**, 빈 프로젝트(empty state), 저장되지 않음(dirty) 표시, 에러 상태
4. 각 항목은 `code.html`(반응형 정적 HTML/CSS, 인터랙션 불필요) + `screen.png`(렌더 스크린샷)
5. **디자인 토큰 시트** — 색/타이포/스페이싱/라운딩/아이콘 사양을 표로 (구현 코드가 그대로 가져다 쓸 수 있게)

> HTML 목업은 어디까지나 비주얼 합의용이다. **실제 구현 타깃은 ImGui 즉시모드**이므로
> 모든 제안은 §2 기술 제약 안에서 환원 가능해야 한다. 환원 불가한 효과는 "장식"이 아니라 "제거 대상"이다.

---

## 1. 제품 컨텍스트

Maroow는 Spine / Live2D 류의 **프로페셔널 2D 스켈레탈 애니메이션 에디터**다. 기능 구현은 사실상 완료
(PRD 117/117). 본 배치·키프레임·리깅·메쉬/웨이트·제약(IK/Path/Transform/Physics)·아틀라스·런타임 export를
모두 지원하며, 최근 AI 에이전트가 에디터를 직접 제어하는 소켓 파이프라인까지 추가되었다.

타깃 사용자는 **장시간(8–12h) 작업하는 프로 애니메이터**다. 그래서 디자인 북극성은 *"The Digital
Architect"* — 버튼 범벅의 레거시 툴 미학을 거부하고, 라인이 아닌 **톤(charcoal depth)의 층위**와
**외과적 포인트 라이트**로 기능을 드러내는 모놀리식 고정밀 워크스페이스. 눈의 피로를 줄이고 "몰입
상태(flow)"에서 UI가 보이지 않아야 한다.

리뉴얼이 풀어야 할 문제(현재 한계):

- 7개 패널이 4-분할 도킹에 욱여넣어져 **정보 밀도 대비 시각적 위계가 약함**
- 메뉴바·툴바·뷰포트툴바·타임라인툴바에 아이콘 버튼이 분산 → **액션 발견성/일관성 부족**
- 타임라인 트랙 테이블이 데이터는 많은데 **스캔 가능성(scanability)** 이 낮음
- setup-pose ↔ animation 모드 전환, weight-paint 모드 진입이 **시각적으로 명확하지 않음**
- **AI 에이전트 제어가 제품의 핵심 차별점인데 UI가 전무함** — 에이전트는 소켓 스레드로
  헤드리스 동작할 뿐, 연결 여부·에이전트가 무엇을 하는지·어떤 변경이 에이전트發인지·
  사람이 개입/중단할 수단이 **화면에 하나도 없음**(§4.8에서 신설)
- 폰트/그라데이션/글래스 등 DESIGN.md가 규정한 비주얼이 **코드에 절반만 반영**(§5 갭 참조)

---

## 2. 기술 스택 & 구현 제약 (Hard Constraints)

> **이 절을 위반하는 디자인은 채택 불가.** claude design은 모든 시각 요소를 아래로 환원 가능한지 검증할 것.

| 영역 | 스택 | 디자인 함의 |
|---|---|---|
| 언어/표준 | C++17 | — |
| 빌드 | CMake 3.16+ | — |
| **UI 프레임워크** | **Dear ImGui 1.92.6 WIP** (즉시모드) | CSS 없음. 모든 위젯은 매 프레임 재생성. 레이아웃은 커서/`SameLine`/`Columns`/`Table`/도킹으로만 구성. 임의 절대배치·플렉스박스·그리드 없음 |
| 윈도우/입력 | GLFW 3.4.0 | — |
| 그래픽 API | OpenGL 3.2 Core (에디터 뷰포트) | 뷰포트는 FBO→`ImGui::Image()` 합성 |
| 도킹 | ImGui docking 브랜치 | 패널 = `ImGui` 윈도우. 사용자가 도킹 재배치 가능 → 디자인은 **고정 픽셀 좌표 가정 금지**, 비율·최소크기로 |
| **폰트** | **Pretendard OTF** (Regular / SemiBold) 번들, Git LFS | ⚠️ DESIGN.md는 "Inter"라 적었으나 **실제 번들은 Pretendard**. 본 명세는 **Pretendard 확정**. 한글·CJK 글리프 포함. Condensed/가변축 **없음** → "Inter Condensed" 류 요구 금지. 자간(letter-spacing) 조정은 ImGui 기본 폰트 렌더에서 비용이 큼 → 글자 트래킹에 의존하는 디자인 금지 |
| **아이콘** | PNG 텍스처 (`assets/icons/export/white_48/`, `icon_registry.cpp`로 로드) | 단색 흰색 48px PNG를 런타임 틴트. 아이콘폰트 아님. 신규 아이콘 요구 시 **단색 글리프형**으로, 라인 두께 일관되게 |
| 라운딩 | 현재 전역 2.0px | DESIGN.md "sm(2px)/DEFAULT(4px)만" 규칙 준수. 큰 라운딩 금지 |
| 그림자/블러 | ImGui 네이티브 미지원 | **백드롭 블러·드롭섀도우는 비네이티브.** `ImDrawList` 다중 패스로 근사 가능하나 비용↑. 글래스모피즘은 *반투명 단색 + 미세 톤차*로 근사하는 폴백을 반드시 함께 제시 |
| 그라데이션 | ImGui 버튼/프레임 네이티브 미지원 | `ImDrawList::AddRectFilledMultiColor`로 수동 그릴 수 있으나 위젯 배경에는 직접 안 들어감. **그라데이션 CTA는 커스텀 드로우 비용**을 명시하고, 단색 폴백을 함께 제시 |
| 색 정의 | `shell_main.cpp` 내 RGBA 상수 | 토큰은 **0–255 / 0.0–1.0 RGBA**로 제공. HSL/CSS 변수 무의미 |
| 플랫폼 | macOS(Metal/AppKit), Linux(OpenGL/X11) | OS 네이티브 위젯·블러 효과 사용 불가(자체 렌더) |

**핵심 결론**: "웹스러운" 효과(부드러운 그림자, 백드롭 블러, CSS 그라데이션, 가변 폰트 트래킹)는
ImGui에서 비싸거나 불가능하다. 리뉴얼의 비주얼 임팩트는 **(a) 톤 층위 (b) 포인트 라이트 컬러
(c) 정보 위계·여백 (d) 단색 아이콘 시스템 (e) 타이포 스케일** 로 달성해야 한다. 효과가 아니라 *구조*로 승부.

---

## 3. 기존 디자인 시스템 (Charcoal Studio) — 계승할 원칙

전면 리뉴얼이되 아래 **DNA는 유지**한다(이것이 Spine/Live2D 대비 차별점):

- **No-Line 아키텍처**: 1px 분할선 금지. 경계는 `surface-container-high`를 `surface-container-low`
  배경에 얹어 *톤 차*로만 표현
- **톤 층위(Tonal Lift)**: Canvas → Panel → Widget → Active 4단계 차징. 그림자 대신 톤으로 깊이
- **포인트 블루**: 인터랙션 하이라이트는 `primary`(#b3c5ff) / `primary-container`(#608bff)
- **플레이헤드 레드**: `tertiary-container`(#ff5450)는 "활성 순간"에만 허용되는 유일한 비-블루
- **여백 = 분리자**: 16px 갭이 1px 라인보다 낫다
- **다이아몬드 키프레임**: 6px 다이아. 상태별 색(unselected=outline / selected=primary / modified=tertiary)
- **고스트 입력**: 배경 없는 텍스트필드, 하단 2px 언더라인이 포커스 시 primary로 발광
- 순수 검정(#000000) 금지, 100% 불투명 보더 금지

### 현행 컬러 토큰 (베이스라인 — 리뉴얼에서 확장/조정 제안 가능)

| 토큰 | HEX | RGBA(0–255) | 용도 |
|---|---|---|---|
| surface-lowest | `#0b0e14` | 11,14,20 | 도킹 빈 영역 / 캔버스 바닥 |
| surface | `#101319` | 16,19,25 | 메인 윈도우 배경 |
| surface-low | `#191c22` | 25,28,34 | 메뉴바 / 패널 배경 |
| surface-default | `#1d2026` | 29,32,38 | 헤더 기본 |
| surface-high | `#272a30` | 39,42,48 | 버튼 / 입력 기본 |
| surface-highest | `#32353b` | 50,53,59 | 스크롤바 grab / 글래스 베이스 |
| surface-bright | `#363940` | 54,57,64 | 버튼 hover ("glow") |
| primary | `#b3c5ff` | 179,197,255 | 슬라이더·아이콘 틴트·키프레임 |
| primary-container | `#608bff` | 96,139,255 | 버튼 active·포커스·CTA 그라데 끝 |
| on-surface | `#e1e2ea` | 225,226,234 | 본문 텍스트 |
| secondary | `#bec7dc` | 190,199,220 | 라벨 |
| inactive | `#8d9199` | 141,145,153 | 비활성 텍스트 |
| outline-variant | `#434654` | 67,70,84 | 고스트 보더(20–30% 알파) |
| tertiary-container | `#ff5450` | 255,84,80 | 플레이헤드·dirty·destructive |

### 현행 스타일 변수

라운딩 전역 2.0 / 보더사이즈 0.0(무테) / WindowPadding (8,8) / FramePadding (8,4) /
ItemSpacing (8,4) / ItemInnerSpacing (4,4) / ScrollbarSize 12 / GrabMinSize 8 /
아이콘 기본 20px, active 틴트 primary@1.0, inactive on-surface@0.9, disabled @0.35.

> 리뉴얼은 이 토큰을 **재정의/확장**할 수 있다(예: surface 단계 추가, 시맨틱 토큰 도입,
> 더 강한 위계 대비). 단 변경 시 §2 제약 내에서 RGBA로 명시하고 마이그레이션 표를 제공할 것.

---

## 4. 패널별 현행 인벤토리 & 리뉴얼 요구사항

> 아래는 현재 구현된 7개 패널 + 메뉴/툴바의 사실 기반 인벤토리다.
> 각 패널마다 **[현행]** 과 **[리뉴얼 요구]** 를 구분. 요구는 "무엇을 풀어야 하는가"이며,
> 구체적 비주얼 해법은 claude design이 §2·§3 안에서 제안한다.

### 4.0 글로벌 셸 (메뉴바 + 툴바 + 상태)

**[현행]**
- 상단 `BeginMainMenuBar`: File(Reload/Quit) · Edit(Undo Ctrl+Z / Redo Ctrl+Shift+Z) ·
  View(Onion Skin, Perf HUD, 디버그 오버레이 5종, Zoom Fit F, Reset Viewport) ·
  Window(Reset Layout) · Help(단축키, 버전)
- 메뉴바 인라인 툴바: 구분자 `|` → Save·Export·Reload 아이콘 → 구분자 `·` →
  Undo·Redo 아이콘 → dirty 인디케이터(경고 아이콘 + "unsaved", tertiary 색) →
  우측 정렬 상태 메시지
- 단축키: Ctrl+Z/Ctrl+Shift+Z, Space(재생/일시정지), Home(플레이헤드 0), F(줌핏)

**[리뉴얼 요구]**
1. **액션 발견성 통합**: 현재 메뉴바·툴바·뷰포트툴바·타임라인툴바 4곳에 흩어진 아이콘 액션의
   위계 재정의. 전역 액션(save/export/undo/redo/reload) vs 컨텍스트 액션(뷰포트/타임라인)을
   시각적으로 구분하는 일관 체계 제안.
2. **모드 인디케이터**: 현재 setup-pose / animation / weight-paint 모드가 어디서도 명확히
   안 보임. 셸 레벨에서 **현재 모드를 한눈에** 드러내는 영역 제안(클릭 전환 가능하면 가산점).
3. **dirty/상태 피드백**: "unsaved" 텍스트 인디케이터를 더 절제되면서 눈에 띄게. 상태 메시지
   영역(성공/에러/진행)의 톤 체계 정의.
4. **에이전트 프레즌스(글로벌)**: 셸 레벨에 AI 에이전트의 상태(미설정 / 리슨 중 /
   연결됨 / **구동 중**)를 항상 보이는 글로벌 인디케이터로. "에이전트가 지금 에디터를
   움직이고 있다"가 어느 패널을 보든 즉시 읽혀야 함. 상세는 §4.8.
5. ImGui `BeginMainMenuBar`는 단일 행이다. 멀티행 헤더가 필요하면 커스텀 차일드 윈도우로
   구성해야 함 — 그 제약을 전제로 디자인.

### 4.1 Viewport (중앙, 메인 캔버스)

**[현행]**
- 헤더: 모드 라벨("Setup pose preview" / "Animation preview / <name>") · 인터랙션 힌트 ·
  재생 시간 표시(`current / duration`)
- 툴바 아이콘: ZoomFit · ZoomOne · `|` · BoneToggle · OnionSkin · PerfHud
- 캔버스: OpenGL FBO 합성. 본 라인망+조인트, 메쉬(텍스처/와이어), 어니언스킨 고스트,
  선택/호버 하이라이트, 제약 디버그 오버레이
- 인터랙션: LMB=본 선택, RMB드래그=팬, 휠=줌(0.2–6.0×). weight-paint 모드 시
  LMB드래그=웨이트 페인트 + 원형 브러시 오버레이 + (옵션)히트맵

**[리뉴얼 요구]**
1. 캔버스가 주인공. 헤더/툴바가 캔버스 면적·몰입을 해치지 않도록 **최소 침습** 오버레이형 제안
   (반투명 단색 근사 — §2: 진짜 블러 불가).
2. weight-paint 모드 진입 시 **캔버스 전체의 비주얼 상태 전환**(브러시 HUD, 모드 톤)을 명확히.
3. 빈 프로젝트(empty state): 에디토리얼한 대형 저대비 타이포 + 다음 액션 안내(§3 타이포 원칙).
4. 디버그 오버레이(본/IK/Path/Physics/메쉬/바운딩) 토글 상태가 캔버스 위에서 읽히는 범례 제안.

### 4.2 Timeline (하단)

**[현행]**
- 애니메이션 셀렉터 콤보 · 프리뷰 옵션(Queue Next Clip, Reverse, Queued Animation,
  Queue Delay, Mix Duration)
- 재생 툴바: Rewind · PrevKey · Play/Pause · NextKey · Loop · `|` · AddKey · RemoveKey
- 시간 스크러버(SliderScalar, "%.3fs") · 통계 라인(span / keyed tracks / root motion)
- 트랙 테이블 2열: Track(고정 260px, 라벨+키수) | Keys(스트레치, 비주얼 레인).
  레인에 1/4 틱 가이드, 보간 힌트선, 플레이헤드(레드), 다이아 키프레임 마커
  (selected=primary / unselected=secondary@82% / near-playhead=red)

**[리뉴얼 요구]**
1. **스캔 가능성**이 최우선 문제. 트랙 수가 많을 때 속성별(rotate/translate/scale/...)
   그룹핑·아이콘·색 코딩으로 빠르게 훑을 수 있는 트랙 리스트 비주얼.
2. 키프레임 레인의 **시간 그리드 가독성** 강화(현 1/4 틱은 약함). zebra(§3 데이터테이블 규칙)
   적용하되 No-Line 유지.
3. 플레이헤드와 재생 컨트롤의 **공간적 연결**(헤드 위치 ↔ 트랜스포트)을 시각적으로.
4. 프리뷰 옵션(queue/mix/reverse)이 자주 안 쓰는 고급 옵션 → 기본 접힘/점진적 노출 제안.
5. ImGui `Table`(`BordersInnerV|RowBg|Resizable|ScrollY|SizingStretchProp`) 기반.
   레인 마커는 `ImDrawList` 수동 드로우 — 6px 다이아 등 픽셀 사양을 명시.

### 4.3 Hierarchy (좌상)

**[현행]**
- 고스트 검색 입력("Search bones, slots…", 포커스 시 언더라인 발광)
- 통계("Bones: N · Slots: M")
- 트리: 필터 없으면 계층 트리(root 펼침), 필터 시 평면 매치 리스트.
  본 노드(Icon::NodeBone, "(inactive)" 접미), 슬롯 노드(Icon::NodeSlot, 리프).
  들여쓰기 기반(브랜치 라인 없음), hover 배경 틴트로 선택 표시

**[리뉴얼 요구]**
1. 깊은 본 계층에서 **들여쓰기만으로는 추적 어려움** — 브랜치 라인 없이(§3) 깊이를
   읽게 하는 대안(미세 톤 들여쓰기 거터, 활성 경로 하이라이트 등) 제안.
2. 본/슬롯 시각 구분 강화, inactive 본의 상태 표현.
3. 검색 결과 평면 리스트와 트리 모드의 전환이 매끄럽게.

### 4.4 Properties (좌하, 인스펙터)

**[현행]** 4개 접이식 헤더(모두 기본 펼침):
- **Bones**: 스크롤 리스트(140px) + 선택 시 setup-pose 값(아이콘: translate/rotate/scale/shear),
  inherit 모드, 편집형 local pose(고스트 입력 DragFloat), 읽기전용 world pose
- **Slots**: 리스트(130px) + draw order/blend/attachment, ColorEdit4 light/dark
- **Attachments**: 슬롯별 어태치먼트 리스트 + Apply/Reset 버튼 + 상세
- **Skin Preview**: 멀티스킨 체크리스트

**[리뉴얼 요구]**
1. 정보 밀도가 가장 높은 패널. **읽기값 vs 편집값**의 시각적 분리(고스트 입력 = 편집,
   world pose = 읽기전용)를 일관 규칙으로.
2. 4개 섹션을 동시에 펼치면 매우 길어짐 — 섹션 헤더/접힘의 위계와 현재 선택 컨텍스트
   (선택된 본/슬롯)를 패널 상단에 고정 표시하는 안.
3. 좌표/수치 데이터의 타이포(§3: 데이터 스케일, 단 Pretendard라 Condensed 불가 — 정렬·라벨
   색 대비로 데이터 느낌 내기).
4. 컬러 위젯(ColorEdit4 light/dark tint)의 일관 배치.

### 4.5 Runtime Assets (좌하 탭)

**[현행]** 스켈레톤 정보(이름/바운드/카운트/제약수) + 접이식: Animation Clips(아이콘+duration),
Skins, Constraints(타입별 그룹), Atlases(리전수/치수). **읽기 전용 인벤토리.**

**[리뉴얼 요구]** 읽기전용임을 비주얼로 분명히(편집 패널과 톤 구분). 카운트/메트릭의
스캔 가능한 요약 카드형 제안.

### 4.6 Constraints (좌하 탭)

**[현행]** 프로젝트 없으면 안내문. 타입 선택(IK/Path/Transform/Physics) → 생성 버튼 →
기존 제약 리스트 → 선택 시 속성 에디터(공통: bone refs/alpha/bone limits, 타입별 필드).

**[리뉴얼 요구]** 4개 제약 타입의 **시각적 구분**(아이콘+악센트). 타입 전환 UI(탭 vs 그룹)
권고안. 생성→리스트→편집 3-스텝 플로우의 명확한 위계.

### 4.7 Project (좌상 탭)

**[현행]** 퀵액션(Reload/Save/Export + Export .mbin 체크) · 경로 · 메타데이터 대량
(name/anim/playhead/preview mode/카운트/root motion/제약수/history/런타임 경로/notes/error).

**[리뉴얼 요구]** 텍스트 덤프 → **구조화된 요약**(섹션 카드, 키-값 정렬). 경로/에러의
가독성. notes 영역 에디토리얼 처리.

### 4.8 Agent (신설 — 제품의 핵심 차별점)

> **현행: UI 없음.** `AgentSocketServer`(`agent_socket.cpp`)가 `127.0.0.1:<port>`에서
> 토큰 핸드셰이크 후 줄단위 JSON-RPC를 수신, 스레드세이프 큐에 적재 →
> 메인 루프가 매 프레임 `drain_commands()`로 `AgentCommandDispatcher::dispatch()` 실행.
> **중요**: UI 버튼(save/export/undo/redo 등)도 *같은 dispatcher*를 호출한다 →
> 사람과 에이전트가 단일 편집 코어를 공유하며, 모든 에이전트 편집은 undo 스택 경유로 가역적.
> `.marrow` 저장은 명시적 `save` op으로만. 소켓은 localhost 바인드 + 토큰.
> 지원 op: undo/redo/save/export_runtime/set_transform/remove_transform_keyframe/
> edit_ik_constraint/scene.describe/bones.list/animation.list (재생/시크 op 없음).

**[리뉴얼 요구]** — Maroow를 "AI가 직접 모는 에디터"로 보이게 하는 전용 서피스를 설계.
구체 형태(독립 패널 / 셸 도크 / 슬라이드오버)는 §2 도킹 제약 안에서 claude design이 제안.

1. **연결·세션 상태**: 미설정(포트 없음) / 리슨 중(포트 N, 대기) / 연결됨(클라이언트 1) /
   구동 중(명령 처리 중)의 4-상태를 명확한 비주얼로. 포트·토큰 설정 여부 표시
   (토큰 값 자체는 마스킹). 다중 접속은 초기 미지원 — 단일 세션 전제.
2. **라이브 액티비티 피드**: 에이전트가 보낸 op 스트림을 시간순 읽기전용 로그로.
   각 항목 = op명 · 인자 요약 · 결과(ok/err) · 영향 받은 대상(본/애니메이션/슬롯) ·
   타임스탬프. 성공/실패 톤 구분(§3 색 규칙, err는 tertiary 레드). 자동 스크롤 +
   일시정지 토글. ImGui 스크롤 차일드 + 매 프레임 폴링(푸시 없음) 전제 —
   피드 버퍼는 thread-safe ring으로 가정, 길이 상한·가상화 고려.
3. **변경 출처 귀속(attribution)**: "이 변경은 사람이 했나 에이전트가 했나"가 핵심.
   에이전트發 최근 변경을 관련 패널(Hierarchy/Timeline/Properties)에서 일시적
   하이라이트(미세 primary 글로우, 수 초 페이드)로 표시하는 일관 규칙. undo 스택
   엔트리에도 출처 라벨(사람/에이전트) 비주얼.
4. **사람↔에이전트 핸드오프 / 통제**: 에이전트 구동 일시정지·재개·세션 종료(연결 끊기)
   컨트롤. destructive(연결 종료)는 §3 레드. "에이전트가 도는 동안 사람이 조작 가능"
   전제이므로 충돌이 아니라 협업으로 읽히는 톤.
5. **동의·권한 가시화**: 파일을 쓰는 op(`save`, `export_runtime`, import/export 경로)는
   민감. 자동 허용/검토 모드 토글과, 방금 어떤 파일 경로에 무엇이 쓰였는지 사후
   확인 표시. (정책 로직은 기존 코드 책임 — 디자인은 *상태를 읽게* 만드는 것.)
6. **에이전트 구동 중 환경 반응**: §4.0-4 글로벌 프레즌스와 연동. 에이전트가 활성
   편집 중일 때 워크스페이스 전체에 과하지 않은 톤/악센트 시그널(예: 셸 가장자리
   미세 primary 라인, 또는 프레즌스 칩 펄스 — 깜빡임 남발 금지).
7. **빈 상태**: 에이전트 미설정 시, 이 서피스가 "포트로 기동하면 AI가 이 에디터를
   제어할 수 있다"를 에디토리얼하게 안내(연결 방법 힌트 포함).

> 차별화 포인트: 경쟁 제품(Spine/Live2D)에 없는 "에이전트와 한 캔버스에서 협업" 경험을
> **신뢰감 있고 통제 가능한** 비주얼로. 블랙박스가 아니라 *유리상자*.

---

## 5. 코드↔DESIGN.md 갭 (리뉴얼이 메워야 할 항목)

현재 구현이 *Charcoal Studio* 의도를 절반만 반영. 리뉴얼 시 결정 필요:

| 항목 | DESIGN.md 의도 | 현행 코드 | 리뉴얼 처리 |
|---|---|---|---|
| 폰트 | "Inter" | Pretendard OTF 번들(또는 ImGui 기본) | **Pretendard 확정**, 타입스케일을 Pretendard 기준 재정의 |
| CTA 그라데이션 | primary→primary-container 135° | 미적용(단색) | §2: 커스텀 드로우 비용 명시 + 단색 폴백 동시 제안 |
| 글래스모피즘 | 16px 백드롭 블러 | 미적용 | §2: 비네이티브 → 반투명 단색 근사 폴백 확정 |
| 슬라이더 핸들 | 2px 수직 바 | ImGui 기본 grab | 아키텍처형 2px 바 사양 제시(GrabMinSize 등) |
| zebra 테이블 | even/odd surface 교차 | RowBg 일부 | 타임라인/리스트 전반 적용 규칙 |
| 타입 스케일 | display-sm / label-sm 등 | 단일 기본 폰트 | Pretendard Regular/SemiBold 2-웨이트로 위계 표 정의 |

---

## 6. 차별화 방향 (Spine/Live2D 대비)

전면 리뉴얼의 정체성. claude design은 아래를 시각으로 구현:

- **모놀리식 슬레이트 인스트루먼트**: 버튼 범벅이 아닌, 톤으로 조각된 단일 정밀 기구. 칩/뱃지/
  보더 남발 금지.
- **외과적 컬러**: 화면의 95%는 무채 charcoal, 5%만 포인트 블루/레드. 컬러가 곧 정보.
- **타이포가 곧 데이터**: 좌표·시간·카운트는 정렬과 라벨 톤 대비로 "계기판"처럼.
- **모드가 곧 환경**: setup ↔ animation ↔ weight-paint ↔ **에이전트 구동** 전환 시
  전체 워크스페이스 톤이 반응(과하지 않게, 미세 톤 시프트).
- **유리상자 에이전트**: AI가 에디터를 모는 과정이 블랙박스가 아니라 *보이고·되돌릴 수
  있고·언제든 사람이 손댈 수 있는* 협업으로. 이것이 Spine/Live2D에 없는 결정적 차별점.
- **여백의 권위**: 빈 공간을 분리자이자 고급감의 원천으로.

---

## 7. 수용 기준 (Acceptance)

- [ ] 7개 패널 + 글로벌 셸 + **에이전트 서피스(§4.8)** 모두 통합 목업에 포함, 도킹 비율/최소크기 가정 명시
- [ ] 모든 시각 요소가 §2 ImGui 제약으로 환원 가능 (비네이티브 효과는 폴백 동반)
- [ ] 디자인 토큰이 RGBA(0–255) 표로 제공, 현행 토큰 대비 마이그레이션 표 포함
- [ ] §3 DNA(No-Line, 톤 층위, 포인트 라이트, 다이아 키프레임, 고스트 입력) 유지
- [ ] §4 각 패널 [리뉴얼 요구] 항목에 대한 구체 해법 제시
- [ ] **§4.8 에이전트 7개 요구(상태/피드/귀속/핸드오프/동의/환경반응/빈상태) 해법 제시**
- [ ] **에이전트 연결/구동/대기 + 변경 출처 귀속 비주얼 포함**
- [ ] setup/animation/weight-paint/**agent-driving**/empty/dirty/error 상태 변형 포함
- [ ] Pretendard 2-웨이트 기준 타입 스케일 표
- [ ] 아이콘은 단색 48px 글리프형 사양, 신규 아이콘 목록 명시
```
