# Marrow 리팩터링 필요성 점검

- 작성일: 2026-09-12
- 대상 프로젝트: Marrow
- 기준 브랜치: `feat/mar-168`
- 기준 HEAD: `b3a75f3`
- 목적: 최근 기능 확장 이후 구조적 리팩터링 필요성 및 우선순위 평가

## 구현 진행 — 2026-09-12

아래 점검 본문은 수정 전 상태를 설명하는 기록으로 보존한다. 현재 구현 상태는 이 절과 실행 기록을 기준으로 구분한다.

**Phase 1 — MCP transport safety: 구현 및 로컬 회귀 검증 완료.**

- `tools/mcp/marrow_client.py`로 SDK 비의존 transport를 추출했다. 기존 `from server import MarrowClient`는 유지한다.
- connect/auth/write/drain/read/validation 전체를 단일 async lock으로 직렬화했다.
- 요청별 UUID 기반 string ID와 응답 ID/JSON-RPC/result 검증을 추가했다.
- timeout/cancellation/protocol/connection 오류 시 실제 transport를 닫고 두 stream 참조를 폐기한다. 다음 명시적 요청만 재연결하며, 실패한 명령은 자동 재전송하지 않는다.
- 기본 request timeout은 30초이고 lock을 획득한 뒤부터 적용한다. 대기 중인 호출의 취소는 현재 실행 중인 다른 호출을 끊지 않는다. `aclose()`와 MCP 서버 종료 정리도 추가했다.
- 기존 결함 다섯 개를 실제 TCP 회귀 테스트로 먼저 실패시킨 뒤 수정했다. 확장된 Python transport 테스트 20개와 실제 MCP stdio 서버 통합 테스트 1개가 통과했다.
- CTest에 `marrow.mcp_transport`를 등록했다. 최종 `build-sentinel` 빌드와 CTest **25/25**가 통과했다. 기존 24개 테스트도 유지한다.

실행 계획·RED 재현·검증 명령·범위 제한: `docs/superpowers/plans/2026-09-12-mcp-transport-safety.md`.

**Phase 2 — 공유 shell frame 경로: 구현 및 macOS 로컬 회귀 검증 완료.**

- `shell_frame.hpp/.cpp`의 `draw_shell_frame(ShellState&, double)`을 실제 앱과 shared headless smoke가 각각 한 번 호출한다. 세션 동기화, 0.25초 asset-watch 누적, playback/parameter 시간 진행, shortcuts, 창 구성, orphan gesture 정리, deferred file action을 한 production 경로로 통합했다.
- SDL 이벤트·Agent command drain·drawable 획득·ImGui begin/render·GPU submit/present는 host에 남겼다. 창 순서, Parameter 조건, Agent 패널 토글 다음 프레임의 dock invalidation과 skipped-frame 처리도 유지한다.
- CMake를 `marrow_editor_shell_core` + 제품 `marrow_editor_shell` + BUILD_TESTING 전용 `marrow_editor_shell_smoke`로 분리했다. 두 host는 동일 entry point와 공통 core를 사용하며, headless dispatch 매크로와 저작 smoke 소스는 테스트 실행 파일에만 포함된다.
- 기존 두 headless CTest의 이름과 assertions는 유지하고 실행 대상만 smoke 바이너리로 전환했다. 단일 패널에 집중하는 parameter 등의 전문 smoke는 전체 프레임 중복이 아니므로 유지했다. 제품의 `--auto-close N`은 macOS를 포함해 실제 display 경로를 사용한다.
- `CheckFrameBodies.cmake`를 창 목록 복사본 비교 대신 공통 coordinator 배선 검사로 바꿨다. 호출 누락·중복, 직접 창 구성 재도입, 잘못된 lifecycle 소유권을 거부하는 mutation 테스트 8개가 통과했다. 이 검사는 C++ 의미/전체 동작 순서를 증명하는 파서가 아니라 좁은 배선 guard다.
- 실제 ImGui/EditorSession 기반 계약 테스트 8개 그룹이 통과했다. Agent 토글·Parameter 창 조건·세션 revision 동기화·playback 시간·watch throttle·gesture 중 watch 지연 및 orphan 정리·deferred Open·입력 없는 프레임의 persistent state 보존을 확인한다. orphan 종료 호출을 제거한 결함 주입은 정확한 계약 실패를 발생시켰으며, 원본 해시 복원 후 전체 회귀가 다시 통과했다.
- 최종 `build-sentinel` 빌드와 CTest **27/27** 통과. 기존 25개를 유지하면서 `marrow.shell_frame_contract`, `marrow.shell_frame_boundary`를 추가했다. Phase 1 Python transport **20개**, 실제 MCP stdio 통합 **1개**도 재검증했다.
- 별도 `build-phase2-product`에서 `BUILD_TESTING=OFF` 제품 configure/build가 통과했다. compile database 및 `nm -C` 검사로 제품의 공통 coordinator 연결, smoke 컴파일 단위 **0개**, headless/계약 smoke 심벌 부재를 확인했다. 테스트 활성 빌드에서는 core 31개·제품 host 1개·smoke host/시나리오 12개 컴파일 단위가 분리된다.

실행 계획·RED/GREEN 근거·정확한 검증 명령: `docs/superpowers/plans/2026-09-12-shared-shell-frame.md`. AGENTS 상단에는 현재 검증 명령과 새 실행 파일 구분만 추가했고 역사 기록은 보존했다.

**아직 미착수:** Phase 3 shell lifecycle, Phase 4 project 책임 분리, AGENTS 역사 기록 분리.

이번 검증은 macOS 로컬 빌드/headless, 격리 TCP peer, 실제 MCP 서버 프로세스와 CTest를 대상으로 한다. 실제 C++ 에디터에 대한 Python mutation E2E, Windows/Linux 실행, display/GPU/portable qualification 완료를 의미하지 않는다. 구현 작업 당시 기존 dirty 변경을 보존했으며, 이후 사용자의 커밋 정리 요청으로 아래 두 구현 커밋을 생성했다. 푸시는 하지 않았다.

### 커밋 정리 — 2026-09-12

- Phase 1: `7925795` — `fix(mcp): serialize requests and harden connection lifecycle` (8개 파일).
- Phase 2: `9d50276` — `refactor(editor): share frame execution and isolate smoke host` (13개 파일).
- 커밋 직전 최종 조합 소스로 전체 빌드와 CTest **27/27**, MCP transport **20/20**, stdio 통합 **1/1**, 테스트 활성/비활성 제품의 compile database·심벌 분리 검사를 다시 통과했다. 두 Phase가 섞인 CMake/AGENTS 변경은 Phase 1 부분을 먼저 분리해 스테이징했다.
- 실행 계획서의 미커밋 상태·검증 시간은 각 구현 실행 당시의 기록으로 보존한다. 현재 커밋 상태는 이 절을 기준으로 한다.
- `Untitled`, `build-sentinel/`, `build-phase2-product/`, `build-specrev187/`는 커밋에서 제외하고 로컬에 보존했다. 브랜치 병합·푸시·파일 삭제는 하지 않았다.

## 결론

리팩터링은 필요하다. 다만 전면 재작성보다는 **에이전트 통신, 셸 상태 관리, 테스트 실행 경로처럼 새 기능 추가 시 누락되기 쉬운 경계부터 정리하는 방식**이 적절하다.

현재 프로젝트는 이미 `EditorSession`/`EditTransaction`, UI-free 모델 계층, viewport interaction kernel, timeline model, mesh weight model, renderer 내부 경계 등 좋은 분리가 존재한다. 따라서 기존 구조를 폐기할 근거는 없다.

우선순위는 다음과 같다.

1. MCP 통신 요청/응답 및 연결 수명 관리
2. 실제 앱과 테스트의 프레임 처리 중복 제거
3. 셸 상태 동기화 및 authoring gesture 수명 관리
4. `project.cpp` 책임 분리
5. `AGENTS.md` 진입 문서 경량화

---

## 검증 범위

점검 당시 현재 소스로 Debug 빌드를 다시 수행했고, 해당 구성의 CTest 24개가 모두 통과했다.

```text
100% tests passed, 0 tests failed out of 24
```

소스 코드는 점검 과정에서 수정하지 않았다.

단, 이 결과는 macOS 현재 Debug 구성의 C++/shell 테스트 상태를 의미한다. Python MCP client의 concurrency/cancellation 안전성, Windows 실기, display/GPU qualification, 성능 검증까지 포함하는 것은 아니다.

---

## 1. 최우선: MCP 통신의 요청·응답 및 연결 수명 관리

### 현재 구조

`tools/mcp/server.py`의 `MarrowClient`는 하나의 `reader`와 `writer`를 공유한다. 각 `send_command()`가 직접 요청을 쓰고 응답을 한 줄 읽는다.

현재 구조에는 다음 특성이 있다.

- 동시 호출을 직렬화하는 lock 또는 request queue가 없다.
- 모든 요청의 JSON-RPC ID가 `"mcp-req"`로 동일하다.
- 받은 응답의 ID를 요청과 대조하지 않는다.
- 취소/예외 발생 시 `self.writer = None`으로 참조만 버리고 기존 transport를 명시적으로 정리하지 않는다.

관련 위치:

- `tools/mcp/server.py:16-58`

### 실제 재현 결과

격리된 로컬 테스트 서버와 실제 `MarrowClient`를 연결해 확인했다.

#### 순차 호출

`bones.list`와 `slots.list`를 순차 호출하면 정상 동작했다.

#### 병렬 호출

두 조회를 동시에 호출했을 때 3회 모두 한 요청이 다음 오류로 실패했다.

```text
readuntil() called while another coroutine is already waiting for incoming data
```

즉, 동일 `StreamReader`에 여러 coroutine이 동시에 응답 수신을 시도할 수 있다.

#### 요청 취소 후 다음 요청

`bones.list` 응답을 기다리는 coroutine을 취소한 뒤 같은 client로 `slots.list`를 호출하면, 이전 `bones.list`의 응답을 새 `slots.list` 요청의 결과로 받아들이는 상황이 재현됐다.

```text
취소한 요청: bones.list
다음 요청:   slots.list
받은 결과:  {"ok": true, "op": "bones.list"}
```

이는 agent가 실제 수행 결과를 다른 명령의 결과로 잘못 해석할 수 있는 문제다.

### 권장 리팩터링

첫 단계는 구조를 크게 바꾸기보다 **한 연결에서 요청 전체를 직렬화하는 것**이 적절하다.

- connect + write + drain + read + response validation 전체를 하나의 async lock으로 보호한다.
- 요청마다 고유 request ID를 만든다.
- response ID가 현재 request ID와 일치하는지 검증한다.
- cancellation, timeout, protocol error, connection error 발생 시 해당 연결을 명시적으로 닫고 폐기한다.
- 새 요청이 이전 연결의 stale response를 받을 가능성을 제거한다.

중요한 점은 mutating command의 재전송 정책이다. 서버에서 편집은 수행됐으나 응답만 유실됐을 수도 있으므로, 통신 오류를 이유로 edit command를 자동 재시도하면 안 된다.

### 테스트 보강

현재 CTest에는 C++ socket/dispatcher 검증은 있지만 Python `MarrowClient`의 이 경계는 포함되지 않는다.

추가할 회귀 테스트 후보:

- 두 concurrent request의 안전한 직렬화
- cancelled request 이후 다음 request가 stale response를 받지 않는지
- response ID mismatch 거부
- connection reset 이후 clean reconnect
- mutating command timeout 시 자동 재전송이 일어나지 않는지

---

## 2. 높은 우선순위: 실제 에디터와 테스트의 프레임 처리 중복 제거

### 현재 구조

실제 앱의 `render_shell_frame()`과 headless smoke의 `render_headless_smoke_frames()`가 각각 `draw_*_window()` 호출 목록을 가진다.

이 둘의 drift를 막기 위해 `cmake/CheckFrameBodies.cmake`가 존재한다.

관련 위치:

- `cmake/CheckFrameBodies.cmake`
- `src/editor/shell_main.cpp`
- `src/editor/shell_smoke_frames.cpp`

이 검사는 두 실행 경로가 반드시 함께 유지되어야 한다는 사실을 잘 방어하지만, 동시에 **같은 frame composition이 두 군데에 수동 복제되어 있다는 증거**이기도 하다.

### 현재 guard의 한계

`CheckFrameBodies.cmake`는 함수 이름 목록을 추출해 정렬하고 중복을 제거한다.

따라서 다음은 검증한다.

- application frame에 어떤 `draw_*_window()`가 존재하는가
- smoke frame에 같은 window가 존재하는가

그러나 다음까지 동일함을 보장하지는 않는다.

- 호출 순서
- 인자
- 호출 조건
- window 호출 전후의 상태 변경
- frame 내 추가 side effect

### smoke code의 production target 포함

현재 `marrow_editor_shell` target에는 실제 제품 source와 함께 다음과 같은 smoke source가 직접 들어간다.

- `shell_smoke.cpp`
- `shell_smoke_project.cpp`
- `shell_smoke_psd.cpp`
- `shell_smoke_viewport.cpp`
- `shell_smoke_ffd.cpp`
- `shell_smoke_graph.cpp`
- `shell_smoke_timeline.cpp`
- `shell_smoke_constraints.cpp`
- `shell_smoke_frames.cpp`
- `shell_smoke_parameters.cpp`

관련 위치:

- `CMakeLists.txt:887-928`

### 권장 리팩터링

제품 frame composition을 하나의 공유 경계로 만든다.

예시 방향:

```text
Shell frame coordinator
 ├─ common frame state update
 ├─ common window composition
 └─ common gesture finalization

Application host
 ├─ SDL event/input
 ├─ real window/GPU
 └─ coordinator 호출

Headless smoke host
 ├─ test input/state injection
 └─ 같은 coordinator 호출
```

핵심은 테스트가 production frame logic을 다시 작성하지 않고 **같은 경로를 호출하도록 만드는 것**이다.

기존 `CheckFrameBodies.cmake`는 먼저 지우면 안 된다. 공통 경로로 migration한 뒤 기존 drift 가능성이 구조적으로 사라졌다는 것을 검증하고 제거하는 편이 안전하다.

---

## 3. 높은 우선순위: 셸 상태 동기화와 gesture 수명 관리

이 영역에는 서로 관련된 두 종류의 유지보수 위험이 있다.

### 3.1 EditorSession과 ShellState의 mirrored state

`ShellState`는 `EditorSession`을 소유하지만 다음 값을 별도 field로 캐시한다.

- selected animation
- timeline time
- loop/playing
- queued animation
- mix duration
- reverse
- skin names
- attachment overrides
- preview events
- root motion
- dirty 상태
- preview skeleton / animation state raw pointer

관련 위치:

- `src/editor/shell_state.hpp:843-908`
- `src/editor/shell_core.cpp:261-298`

`sync_shell_from_editor_session()`이 이 mirror를 수동 동기화한다.

모든 mirror가 잘못된 것은 아니다. UI gesture와 ImGui frame lifecycle 때문에 shell-local mutable state가 필요한 경우가 있다.

문제는 새 mutation/recovery path가 추가될 때 caller가 **어떤 mirror를 언제 다시 sync해야 하는지 기억해야 한다는 것**이다.

실제로 runtime data replacement 이후에는 shell이 session 내부 preview object를 가리키는 raw pointer를 다시 획득해야 하며, 코드에도 dangling 방지를 위한 명시적 주석이 존재한다.

관련 위치:

- `src/editor/shell_core.cpp:499-508`

### 권장 방향

- `EditorSession`을 authoritative source로 유지한다.
- 단순 read mirror는 가능한 한 accessor/view로 대체한다.
- UI interaction 때문에 frame-local mutation이 필요한 상태만 shell-owned로 남긴다.
- runtime replacement 후 반드시 재취득해야 하는 alias는 하나의 좁은 binding/update seam에서 관리한다.
- `ShellState`를 단순히 여러 struct로 쪼개는 것 자체를 목표로 삼지 않는다. 구조체 분할만으로 synchronization 위험은 사라지지 않는다.

---

### 3.2 Authoring gesture 목록의 수동 동기화

현재 `authoring_gesture_active()`에는 현재 활성 authoring gesture를 판별하기 위한 목록이 있다.

관련 위치:

- `src/editor/shell_state.hpp:980-995`

반면 `cancel_authoring_gestures()`에는 취소해야 하는 gesture 목록이 별도로 있다.

관련 위치:

- `src/editor/shell_core.cpp:432-484`

코드 주석 자체가 두 목록이 반드시 함께 유지되어야 한다고 경고한다.

누락 위험은 단순 UI glitch가 아니다. cancel 목록에 transaction-owning gesture가 빠지면 live transaction이 남아 이후 `begin_edit`을 계속 막을 수 있다.

### 권장 리팩터링

활성 여부와 cancellation capability를 같은 소유 경계에서 유도해야 한다.

가능한 방향:

- gesture registry 또는 common gesture lifecycle helper
- transaction-owning gesture를 공통 adapter로 표현
- `active()` / `cancel()`을 같은 정의에서 제공
- 특수 rollback이 필요한 gesture는 custom cancellation policy 사용

주의할 점은 모든 gesture를 하나의 generic reset으로 바꾸면 안 된다는 것이다.

특히 다음은 별도 rollback semantics를 가진다.

- viewport transform gesture
- viewport FFD gesture
- weight paint stroke

이들은 transaction cancel 외에도 selection, vertex selection, hierarchy anchor, timeline focus 또는 snapshot 복구가 필요하다.

목표는 특수 동작을 없애는 것이 아니라 **새 gesture를 추가할 때 active/cancel 목록 중 하나를 빼먹을 수 없게 만드는 것**이다.

---

## 4. 중간 우선순위: `project.cpp` 책임 분리

### 현재 상태

`src/editor/project.cpp`는 약 8,745줄이며, 단순히 긴 것이 아니라 서로 다른 lifecycle 책임이 하나의 translation unit에 모여 있다.

대표 책임:

| 책임 | 예시 위치 |
|---|---|
| timeline/constraint edit parsing | `project.cpp:2100`, `3499` |
| constraint lifecycle operation 적용 | `project.cpp:5477` |
| save validation | `project.cpp:6077` |
| serialization | `project.cpp:7156` |
| project load | `project.cpp:8074` |
| runtime build | `project.cpp:8566` |
| project save | `project.cpp:8591` |
| runtime export | `project.cpp:8645` |

새 project field 또는 overlay behavior를 추가할 때 parse/save/validate/runtime application이 한 파일에서 동시에 수정되는 구조다.

### 권장 분리 기준

파일 줄 수가 아니라 책임 경계를 기준으로 점진적으로 분리한다.

권장 후보:

```text
project_model / project_overlay
project_parse
project_serialize
project_validation
project_runtime_materialization
project_io
```

정확한 파일 이름은 실제 dependency 분석 후 결정한다.

### 반드시 보존해야 하는 계약

이 영역은 behavior-preserving refactor로 진행해야 한다.

특히 다음이 regression gate다.

- unknown additive field 보존
- 기존 `.marrow` compatibility
- `.mskl` v1 유지
- `.mbin` v2 유지
- C ABI v1 유지
- overlay application order
- constraint lifecycle ordering
- save → reopen 가능성
- Save As path rebasing
- runtime export behavior

따라서 단순 compile success만으로 분할 완료를 판단하면 안 된다.

### 우선순위

이 작업은 MCP transport보다 긴급하지 않다.

`project.cpp` 분리는 현재 동작의 안정성을 직접 높이기보다는 이후 기능 추가 비용과 리뷰 비용을 줄이는 성격이 강하다. 먼저 transport와 shell lifecycle처럼 실제 correctness 위험이 있는 경계를 해결한 뒤 진행하는 것이 낫다.

---

## 5. 별도 정리 대상: `AGENTS.md`의 과도한 누적

### 현재 상태

루트 `AGENTS.md`는 약 748 KB까지 증가했다.

점검 중 일반 파일 read 경계인 720 KB를 넘어 한 번에 읽지 못했고, 필요한 규칙을 검색으로 찾아야 했다.

에이전틱 개발 프로젝트에서 이것은 단순 문서 미관 문제가 아니다.

**모든 agent가 작업 시작 시 읽어야 할 진입 문서가 실제 agent tooling의 읽기 한도를 넘었다.**

### 권장 방향

루트 `AGENTS.md`에는 현재 작업에 필요한 durable rule만 유지한다.

예:

- architecture source of truth
- active PRD/source of truth
- branch/worktree discipline
- story commit checklist
- 필수 validation commands
- 몇 개의 핵심 불변식
- 상세 기록 문서로의 링크

과거 story에서 얻은 긴 사건 기록, inversion case, historical failure analysis는 삭제하지 말고 별도 문서로 이동한다.

예:

```text
docs/agent-notes/
  validation-lessons.md
  git-and-worktree-incidents.md
  testing-inversion-patterns.md
  editor-state-invariants.md
```

루트 문서는 index 역할만 한다.

이미 `docs/root1/discription.md`와 PRD가 source of truth이므로 새로운 경쟁 문서를 만들기보다는 기존 source of truth로 연결하는 것이 중요하다.

---

## 유지하는 편이 좋은 구조

이번 점검에서 전면 재작성의 근거는 찾지 못했다.

오히려 다음 구조는 유지하면서 강화하는 편이 좋다.

### EditorSession / EditTransaction

저작 mutation과 history/rollback을 transaction 경계로 관리하는 구조는 프로젝트의 중심으로 유지하는 것이 적절하다.

### UI-free editor models

이미 다음과 같은 좋은 분리가 존재한다.

- `timeline_model`
- `timeline_graph_model`
- `viewport_interaction_kernel`
- `mesh_weight_model`
- selection/preference model

이 패턴을 신규 기능에도 계속 적용하는 편이 좋다.

### Renderer layering

아키텍처 문서에 정의된 다음 분리도 유지하는 것이 좋다.

- `marrow_renderer_commands`
- `marrow_renderer_core`
- `marrow_renderer_sapp_host`
- `marrow_editor_shell`

UI-free code에서 SDL/Sokol/ImGui 의존성을 차단한 현재 원칙도 보존한다.

### 테스트 경계

이번 검증에서 다음 모델/경계 테스트들은 함께 통과했다.

- runtime unit
- renderer link boundary
- viewport interaction kernel
- timeline model
- timeline graph model
- mesh weight model
- preference store
- selection set
- project smoke
- agent dispatch smoke
- PSD import smoke
- editor shell smoke
- registry claims

테스트를 줄이는 리팩터링이 아니라, **production 경계와 테스트 경계를 더 일치시키는 리팩터링**이 필요하다.

---

## 권장 실행 순서

### Phase 1 — MCP transport safety

가장 먼저 진행한다.

1. 현재 concurrency/cancellation failure를 regression test로 고정
2. request serialization
3. unique request ID / response matching
4. connection invalidation / close 정책
5. cancellation/timeout behavior
6. 전체 MCP + CTest regression

### Phase 2 — Shared frame execution path

1. application frame과 smoke frame 공통 부분 정의
2. shared coordinator 도입
3. application host 전환
4. smoke host 전환
5. 기존 `CheckFrameBodies.cmake`가 더 이상 필요한지 검증
6. smoke code의 production target 포함 범위 재정리

### Phase 3 — Shell lifecycle cleanup

1. session-authoritative state와 shell-owned state 분류
2. 단순 read mirror 제거 후보 식별
3. runtime alias refresh seam 축소
4. gesture active/cancel 정의 통합
5. 특수 rollback gesture invariant 테스트

### Phase 4 — Project subsystem split

1. 현재 `project.cpp` 내부 dependency map 작성
2. parse/serialize/validation/runtime materialization/io seam 정의
3. 한 책임씩 behavior-preserving extraction
4. 각 extraction마다 save/reopen/export/unknown-field regression

### Parallel maintenance — Agent documentation

`AGENTS.md`를 짧은 durable entrypoint와 historical reference 문서로 나눈다.

---

## 하지 않는 것이 좋은 것

### 전면 재작성

현재 구조에는 이미 좋은 transaction/model/runtime 경계가 존재하므로 비용 대비 근거가 없다.

### ShellState를 무조건 작은 struct 여러 개로 분해

field grouping만 바꾸고 authoritative source가 그대로 여러 곳이면 synchronization 문제는 해결되지 않는다.

### 모든 transient state를 EditorSession/history에 통합

Selection, preference, preview speed 등은 의도적으로 project history와 분리된 상태다. 단순화를 이유로 이 경계를 무너뜨리면 undo/redo semantics가 오히려 복잡해질 수 있다.

### `project.cpp`를 줄 수만 보고 기계적으로 분할

file-local helper의 중복과 circular dependency만 늘어날 가능성이 있다. 책임과 contract를 먼저 정해야 한다.

### MCP edit command 자동 retry

응답 유실과 실행 실패를 구분할 수 없는 상황에서 mutating request를 재전송하면 동일 편집이 두 번 실행될 수 있다.

---

## 최종 판단

현재 Marrow는 **기능 증가로 인해 전체 구조가 붕괴한 상태는 아니다.** 오히려 과거 리팩터링을 통해 중요한 계산/transaction/runtime 경계가 상당 부분 잘 나뉘어 있다.

지금 필요한 것은 대형 재작성보다 다음 두 종류의 부채를 제거하는 것이다.

1. **correctness debt**
   - MCP request/response concurrency와 cancellation
   - runtime alias / shell mirror synchronization
   - 수동 gesture lifecycle 목록

2. **maintenance debt**
   - application/smoke frame body duplication
   - 거대한 `project.cpp`
   - 과도하게 누적된 `AGENTS.md`

따라서 다음 기능을 크게 추가하기 전에 **MCP transport safety와 shell lifecycle 경계를 먼저 정리하는 리팩터링 사이클**을 한 번 갖는 것이 적절하다.

그 이후 `project.cpp` 책임 분리와 agent documentation 정리를 진행하면, 이후의 에이전틱 2D 애니메이션 기능 확장을 훨씬 안전하게 이어갈 수 있다.
