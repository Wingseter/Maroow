# Maroow AI Agent Control (MCP) 도입 계획

## Context

Maroow는 Spine/Live2D 류의 2D 스켈레탈 애니메이션 에디터(C++17, ImGui/GLFW/OpenGL)다. 기능 구현은
대략 완료된 상태(PRD 117/117). 경쟁 우위 확보를 위해, 최근 Claude가 Blender·Photoshop을 agentic하게
제어하는 흐름에서 영감을 받아 **AI 코딩/제작 에이전트가 Maroow를 직접 제어**할 수 있게 한다.
아티스트가 에디터를 열어둔 채 자연어로 본 배치·키프레임·리깅·export를 지시하면 생산성이 크게 오른다.

### 조사 결과 (벤치마크 + 공식 가이드)

- **Blender (커뮤니티/공식)**: Blender 안에 TCP 소켓 리스너(예: 포트 9876)를 두고, 별도 MCP 서버
  프로세스가 Claude(MCP/stdio) ↔ 소켓을 중계. JSON-RPC. 살아있는 GUI 세션을 실시간 제어.
- **Unity (공식)**: Editor를 "tool surface"로 노출. 에이전트 루프 = *발견 → 지원 연산으로 변경 →
  실제 신호 읽어 검증*. 툴은 자동 등록.
- **Anthropic 공식 가이드**: MCP 3원소 = tools / resources / prompts. *code-execution + progressive
  disclosure*로 툴 정의를 on-demand 로드(토큰 ~98% 절감). 중간 결과는 실행 환경에 유지. 명시적
  consent/권한 흐름과 보안 문서화 필수.
- 참고 링크: blender.org/lab/mcp-server, anthropic.com/engineering/code-execution-with-mcp,
  modelcontextprotocol.io, github.com/CoplayDev/unity-mcp

### Maroow가 유리한 점 (탐색 결과)

- `ProjectData`(`include/marrow/editor/project.hpp`)가 **UI 의존성 0의 순수 데이터** + JSON 직렬화 내장.
- 18+ 종 편집 연산에 대한 finder/mutator API 완비.
- 스냅샷 기반 undo/redo (`ProjectCommandStack`, `UndoStack`, `kMaxDepth=100`).
- headless smoke 하니스 존재(`src/samples/editor_project_smoke.cpp`, `src/editor/shell_smoke.cpp`).
- 안정적 C ABI(`include/marrow/marrow_c.h`).
- **약점**: 편집 로직이 `shell_main.cpp` UI 핸들러에 박혀 있음(스냅샷 캡처가 핸들러 안). 프로젝트
  변경 이벤트 버스 없음. 네트워킹/소켓 전무.

## 목표 아키텍처

```
Claude (MCP host)
  │  MCP / stdio
  ▼
Python MCP 서버 (공식 anthropic mcp SDK)
  │  JSON-RPC over localhost TCP
  ▼
Maroow 에디터 (임베디드 Agent 소켓 리스너, 별도 스레드)
  │  스레드세이프 커맨드 큐 → ImGui 메인 루프에서 디스패치
  ▼
AgentCommandDispatcher  ── 공유 코어 ──┐
  │  finder/mutator + 스냅샷 undo       │
  ▼                                     ▼
ProjectData / UndoStack          CLI/C ABI (headless 검증·CI 경로)
```

핵심 설계 원칙:
- **공유 코어 1개**: `AgentCommandDispatcher`가 JSON 커맨드 → `ProjectData` 변경 → 스냅샷 undo
  push → 뷰포트 dirty 처리를 담당. 이 코어를 (a) live 소켓, (b) headless CLI/C ABI 양쪽이 호출.
- **스레드 안전**: 소켓 리스너는 별도 스레드에서 수신만. 실제 `ProjectData` 변경은 lock-free/뮤텍스
  큐에 적재 → ImGui 메인 루프가 프레임마다 drain하여 메인 스레드에서 실행. 결과는 응답 큐로 반환.
- **검증 루프 친화**: 모든 변경 응답에 갱신된 씬 상태 요약을 함께 반환(에이전트가 즉시 검증).
- **가역성·동의**: 모든 편집은 undo 스택 경유(되돌릴 수 있음). `.marrow` 저장은 명시적 `save` 툴로만.
  소켓은 `127.0.0.1` 바인드 + 핸드셰이크 토큰. import/export 경로는 화이트리스트.

## 변경/추가 파일 (핵심)

- `include/marrow/editor/agent_dispatch.hpp` / `src/editor/agent_dispatch.cpp` *(신규)*
  - `AgentCommandDispatcher`: `dispatch(const json& cmd) -> json result`. 기존
    `find_*_edit()` mutator + `make_project_command()` + `command_stack.push()` 재사용.
  - 커맨드 스키마(JSON): `{ "op": "...", "args": {...} }`. 결과 = `{ "ok", "scene_delta"|"error" }`.
- `src/editor/shell_main.cpp` *(리팩터)*
  - 기존 UI 핸들러의 "스냅샷→mutate→push" 블록을 `AgentCommandDispatcher` 호출로 치환
    (동작 불변, UI와 에이전트가 같은 코어 공유). 프레임 루프에 커맨드 큐 drain 추가.
- `src/editor/agent_socket.hpp` / `agent_socket.cpp` *(신규)*
  - `--agent-port <n>` 옵션. localhost TCP, JSON-RPC 라인 프로토콜, 토큰 핸드셰이크, 큐 브리지.
- `include/marrow/marrow_c.h` / `src/c_api/marrow_c.cpp` *(확장)*
  - `marrow_agent_dispatch(project_handle, json_cstr) -> json_cstr` 추가(headless/CLI 경로).
- `src/samples/agent_dispatch_smoke.cpp` *(신규)* — `editor_project_smoke.cpp` 패턴 차용,
  JSON 커맨드 파일을 먹여 ProjectData 변경 + export 라운드트립 검증.
- `tools/mcp/` *(신규, Python)*
  - `server.py` — Anthropic 공식 `mcp` SDK 기반. 소켓 클라이언트.
  - `tools/` — op별 모듈(progressive disclosure: 그룹 discovery → on-demand 상세).
  - `resources/scene.py` — read-only 씬 상태 리소스(토큰 절약).
  - `prompts/` — 공통 워크플로(예: "idle 루프 만들기") 프롬프트.
  - `pyproject.toml`, `README.md`, Claude Desktop/Code 설정 예시.
- `docs/root1/agent-control.md` *(신규)* — 아키텍처·커맨드 스키마·보안 모델·설정 가이드.

## 단계별 도입 로드맵

> 각 Phase는 독립 검증 가능한 vertical slice. Maroow의 small-story 원칙(MAR-xxx) 준수.

- **Phase 0 — 공유 코어 추출 (리팩터, 동작 불변)**
  `AgentCommandDispatcher` + JSON 커맨드 스키마. `shell_main.cpp`의 편집 핸들러를 코어 호출로
  이전. headless `agent_dispatch_smoke` + C ABI `marrow_agent_dispatch`. UI 회귀 없음 확인.

- **Phase 1 — Live 파이프라인 + 읽기/검사 전용**
  `--agent-port` 소켓 리스너 + 스레드세이프 큐. Python MCP 서버 골격 + 읽기 툴
  (`scene.describe`, `bones.list/get`, `animation.list/get`) + scene 리소스. 안전한
  전 구간 end-to-end 검증.

- **Phase 2 — 본/트랜스폼 + 키프레임 쓰기**
  write 디스패치 op + `undo`/`redo`/`save` 툴. consent·localhost·가역성 가드 적용.

- **Phase 3 — 메쉬/웨이트/제약(IK·Path·Transform·Physics)**
  고급 리깅 op 추가.

- **Phase 4 — Import/Export/Atlas 자동화 + 워크플로 프롬프트**
  PSD/Spine import, 런타임 export, 아틀라스 패킹 op. 경로 화이트리스트. 공통 프롬프트.

- **Phase 5 — 폴리시**
  progressive disclosure 최적화, 구조화된 에러 리포팅, 보안 문서, Claude 설정 배포물,
  `docs/root1/agent-control.md` 완성.

## 검증 방법 (end-to-end)

1. **Headless (CI)**: `./build/marrow_agent_dispatch_smoke fixtures/player_idle.marrow
   tests/agent/cmds.json` → ProjectData 변경 + JSON/binary export 라운드트립 assert.
2. **소켓 통합 테스트**: `marrow_editor_shell --project ... --agent-port 9876 --auto-close N`
   기동 → 테스트 클라이언트가 커맨드 시퀀스 전송 → 응답의 scene_delta 검증 + undo 후 원상복구 확인.
3. **Python MCP 단위 테스트**: 소켓을 모킹하여 툴 스키마/직렬화 검증. MCP Inspector로 수동 점검.
4. **실사용 E2E**: Claude Desktop/Code에 MCP 서버 등록 → "spine 본을 t=0.5에서 45도 회전"
   지시 → 뷰포트 즉시 반영 + `save` 후 `.marrow` diff 확인 + `marrow_fixture_smoke`로 export 재생.
5. **회귀**: 기존 `marrow_editor_shell --auto-close`, `editor_project_smoke`,
   `marrow_unit_tests`(27개) 전부 green 유지.

## 미해결/실행 시 결정 사항

- 소켓 와이어 포맷: 단순 줄단위 JSON-RPC vs 길이프리픽스 프레이밍 (Phase 1에서 확정).
- Python SDK 버전 핀 및 동봉 방식(uv/venv) — Phase 1.
- 동시 에이전트 다중 접속 허용 여부 (초기엔 단일 세션 권장).
