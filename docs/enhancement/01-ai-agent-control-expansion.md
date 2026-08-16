# AI Agent Control Expansion Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Maroow 에디터의 AI Agent Control을 현재의 제한된 키프레임/IK 편집 수준에서, 실제 제작 워크플로를 안전하게 자동화할 수 있는 읽기/쓰기/관리/검증 API와 에이전트 UI로 확장한다.

**Architecture:** 모든 agent mutation은 `AgentCommandDispatcher`와 editor undo stack을 통과해야 하며, `.marrow` 저장과 runtime export는 명시적이고 감사 가능한 관리 op로 유지한다. MCP/Python bridge는 transport layer로 두고, 실제 권한/검증/변경 귀속은 C++ editor core와 UI가 공유한다.

**Tech Stack:** C++17 editor core, Dear ImGui agent panel, localhost JSON command socket, Python MCP server under `tools/mcp/`, existing project/export smoke tests.

---

## 1. 현재 상태와 근거

- `docs/root1/discription.md`는 Maroow를 Spine Pro 대체 오픈소스 2D 스켈레탈 애니메이션 툴체인으로 정의하고, `.marrow`, `.mskl`, `.mbin`, `.matl`, `.png` 파일 흐름을 핵심 런타임 계약으로 둔다.
- `docs/root1/agent-control.md`는 현재 Agent Control이 MCP 기반이며 `127.0.0.1` 로컬 소켓, 선택적 토큰, undoable action, 명시적 save, 계획된 path whitelist를 전제로 한다고 설명한다.
- 현재 구현된 op는 inspection, transform/draw-order/event/deform/weight/slot timeline edit, IK/path/transform/physics constraint edit, reviewed save/export, export preview/runtime validation/temporary JSON-vs-binary comparison, session pause/resume/terminate, import/pack dry-run 및 review queue 요청까지 포함한다.
- `import.spine_json`, `import.spine_atlas`, `import.psd_layers`, `atlas.pack` v1은 local path validation, 기본 `dry_run=true`, reviewed target queue 생성을 담당한다. 실제 importer 실행은 기존 CLI/smoke 경로를 유지하며, agent approval card는 안전한 파일쓰기 승인을 추적하는 제어면이다.
- `src/editor/agent_dispatch.cpp`는 JSON `op`를 받아 `ShellState`와 project edit 모델을 직접 갱신하고, 변경 전후 snapshot을 통해 undo stack에 `"Agent"` 출처로 기록한다.
- `tools/mcp/server.py`, `tools/mcp/tools/inspection.py`, `tools/mcp/tools/editing.py`는 Python MCP 서버가 C++ dispatcher로 tool call을 전달하는 구조를 가진다.
- `docs/design/maroow-editor-visual-renewal-spec.md`는 Agent UI가 현재 없고, 연결/session state, live activity feed, change attribution, pause/resume/terminate, permission visibility, global presence, empty state가 필요하다고 명시한다.

---

## 2. 범위 경계

**이 문서의 범위**

- Agent/MCP 명령 표면 확장: scene inspection, timeline/keyframe, mesh deform/weight, draw-order, constraints, import/export orchestration.
- Agent safety layer: 권한, 경로 제한, 변경 preview/review, explicit save/export, audit trail.
- 에디터 UI: Agent 패널, activity feed, change attribution, global presence, pause/resume/terminate.
- 검증 루프: MCP end-to-end, editor smoke, project export round-trip.

**다른 enhancement 문서에 맡길 범위**

- 렌더러 시각 품질, shader, GPU skinning 최적화는 renderer/runtime enhancement에서 다룬다.
- Spine/PSD/atlas importer의 포맷 해석 정확도 자체는 import pipeline enhancement에서 다룬다. 이 문서는 agent가 importer를 안전하게 호출하는 제어면만 다룬다.
- timeline UX, mesh weight paint UX, constraint authoring UX의 수동 편집 UI 전체 개편은 editor workflow/visual renewal 범위다. 이 문서는 에이전트 실행 상태와 변경 출처 표시만 정의한다.
- MCP 배포 패키징, 외부 호스트 연결, 클라우드 에이전트 연동은 제외한다. 기본 원칙은 local-only다.

---

## 3. 단계별 구현 계획

### Phase 1: Dispatcher/MCP 기반 정리

- [x] `src/editor/agent_dispatch.cpp`의 op 목록과 인자 검증을 문서화하고, 각 op가 읽기/쓰기/관리/검증 중 어디에 속하는지 내부 registry로 정리한다.
- [x] `tools/mcp/tools/inspection.py`와 `tools/mcp/tools/editing.py`의 tool schema를 C++ dispatcher 검증 규칙과 맞춘다.
- [x] 모든 mutation op가 `record_action_from_snapshots(..., "Agent", ...)` 또는 동등한 attribution 경로를 통과하는지 확인한다.
- [x] 실패 응답에 `ok=false`, 사람이 읽을 수 있는 `message`, 가능한 경우 `op`, `category`, `mutating`, `error.code`를 포함하도록 응답 형태를 안정화한다.

### Phase 2: 읽기 API 확장

- [ ] `scene.describe`를 project path, dirty state, selected animation, playhead, counts, export directory, permission mode까지 포함하도록 확장한다.
- [x] `slots.list`, `skins.list`, `attachments.list`, `constraints.list`, `timeline.describe`를 추가한다.
- [x] `mesh.describe`를 추가해 선택 슬롯/attachment의 vertex, weight count, triangle count를 읽게 한다.
- [ ] `project.diagnostics`를 추가해 missing texture, invalid attachment, unsupported constraint, export path 문제를 agent가 먼저 확인할 수 있게 한다.

### Phase 3: 안전한 편집 API 확장

- [x] transform keyframe 외에 slot color, attachment switch, draw-order timeline을 작은 단위 op로 추가한다.
- [x] mesh deform은 `set_deform_keyframe`과 `remove_deform_keyframe`으로 시작하고, 큰 vertex 배열은 bounds/vertex count 제한을 둔다.
- [x] weight 편집은 `paint_weights`보다 먼저 `set_vertex_weights` 또는 `normalize_weights`처럼 결정적이고 검증 가능한 op부터 추가한다.
- [x] path/transform/physics constraint 편집은 기존 `edit_ik_constraint` 패턴을 확장하되, 타입별 op를 분리해 schema를 작게 유지한다.
- [x] 신규 set/edit 계열 write op는 dry-run 모드로 변경 요약을 반환할 수 있으며, 실제 적용은 undo 가능한 단일 action으로 기록한다.

### Phase 4: Import/Export 자동화

- [x] `import.spine_json`, `import.spine_atlas`, `import.psd_layers`, `atlas.pack` op를 추가하되 기본은 dry-run과 reviewed target queue 반환으로 시작한다.
- [x] import/export 대상 경로는 project root, resolved export directory, `/tmp`/`/private/tmp` 하위 whitelist에 포함된 위치로 제한한다.
- [x] `export.preview`와 `export_runtime`은 `.mskl`, `.matl`, `.mbin` 생성 전 출력 경로와 review payload를 남긴다.
- [x] save/export는 explicit review required 상태로 두고, 에디터 Agent panel 승인 없이는 파일을 쓰지 않는다.

### Phase 5: Agent 패널과 전역 presence

- [x] Agent 패널에 agent state를 표시한다: Off, Listening, Connected, Running, Paused, Blocked, Error.
- [x] Agent 패널을 추가해 connection/session, current op, last result, pending review를 보여준다.
- [x] activity feed에 op 성공/실패, review/edit 여부, 메시지를 시간순으로 표시한다.
- [ ] 변경 attribution을 timeline/properties/history UI에서 `"Agent"` 출처로 식별 가능하게 한다.
- [ ] pause/resume/terminate 버튼은 실행 중인 queue에는 적용하되, 이미 적용된 변경은 undo/redo 모델로 되돌리게 한다.

### Phase 6: 검증과 회귀 테스트

- [x] MCP test client에 read/write/manage/validate 대표 시나리오를 추가한다.
- [ ] headless editor smoke에서 agent port를 켠 상태의 session lifecycle을 확인한다.
- [ ] project export round-trip으로 agent 편집 결과가 `.mskl`/`.mbin` 비교까지 통과하는지 검증한다.
- [ ] permission/path whitelist 위반, save/export review 누락, unknown op, malformed args를 실패 케이스로 고정한다.

---

## 4. 제안 Agent/MCP Operation

### Read

- `scene.describe`: 프로젝트 상태, dirty, 선택 애니메이션, playhead, export directory, 권한 상태.
- `bones.list`, `slots.list`, `skins.list`, `attachments.list`: rig 구조와 편집 대상 검색.
- `animation.list`, `timeline.describe`: 클립, duration, keyed tracks, draw-order/event/deform 존재 여부.
- `constraints.list`: IK/path/transform/physics constraint 요약.
- `mesh.describe`: attachment vertex/weight/deform summary.
- `project.diagnostics`: export/import 전 문제 목록.

### Write

- `set_transform`, `remove_transform_keyframe`: 기존 op 유지 및 schema 정리.
- `set_slot_color_keyframe`, `set_attachment_keyframe`, `set_draw_order_keyframe`.
- `set_deform_keyframe`, `remove_deform_keyframe`.
- `set_vertex_weights`, `normalize_weights`.
- `edit_ik_constraint`, `edit_path_constraint`, `edit_transform_constraint`, `edit_physics_constraint`.
- `set_event_keyframe`, `remove_event_keyframe`.

### Manage

- `undo`, `redo`.
- `save`: review 승인 후 `.marrow` 저장.
- `export_runtime`: review 승인 후 `.mskl`/`.matl`/선택적 `.mbin` 출력.
- `import.spine_json`, `import.spine_atlas`, `import.psd_layers`, `atlas.pack`.
- `agent.pause`, `agent.resume`, `agent.terminate`, `agent.permissions.describe`.

### Validate

- `project.diagnostics`.
- `export.preview`: 실제 파일 쓰기 전 출력 파일 목록과 변경 요약 반환.
- `runtime.validate`: runtime smoke에 필요한 기본 consistency check.
- `compare_runtime_export`: JSON/binary export 비교 결과 요약.

---

## 5. Safety and Permissions Model

- Agent socket은 기본적으로 `127.0.0.1`에만 bind한다. 외부 bind는 이 계획의 범위 밖이다.
- 토큰이 설정된 경우 첫 요청 전 handshake를 요구하고, UI에는 token enabled/disabled 상태만 표시한다. 토큰 값은 표시하지 않는다.
- 모든 write op는 undo stack에 들어가야 하며, history label에는 사람이 이해할 수 있는 대상과 `"Agent"` 출처를 포함한다.
- path whitelist는 최소한 다음만 허용한다.
  - 현재 `.marrow` project directory.
  - project metadata에 등록된 asset/export directory.
  - 검증용 임시 출력 디렉터리 `/tmp` 하위.
- whitelist 밖의 import/export/save 경로는 dispatcher 레벨에서 거부하고, activity feed에 blocked event로 남긴다.
- `save`와 `export_runtime`은 명시적 review 대상이다. 기본 정책은 agent가 preview를 만들고, 사용자가 UI에서 승인하거나 MCP 세션이 사전에 승인된 permission profile을 가져야 실제 파일을 쓴다.
- destructive action은 delete보다 disable/remove keyframe처럼 undo 가능한 편집 모델을 우선한다. 되돌릴 수 없는 파일 덮어쓰기는 review 없이 허용하지 않는다.
- 긴 실행 op는 취소 가능해야 하며, cancel 이후 partial write가 있으면 diagnostics와 undo 안내를 남긴다.

---

## 6. Agent UI 요구사항

- **Global presence:** 메뉴바/status 영역에서 agent 상태를 항상 보여준다. 최소 상태는 `Off`, `Listening`, `Connected`, `Running`, `Paused`, `Blocked`, `Error`.
- **Agent panel:** connection endpoint, session id, permission profile, token 사용 여부, 현재 실행 op, 마지막 응답을 표시한다.
- **Activity feed:** 각 항목은 시간, op, 대상, 결과, 변경 수, undo label, save/export 여부를 포함한다. 실패와 blocked는 별도 색상/아이콘으로 구분한다.
- **Change attribution:** timeline keyframe, properties 변경, history entry에서 사람이 한 변경과 agent 변경을 구분한다. 최소 구현은 history/activity feed attribution이며, 이후 keyframe marker나 inspector badge로 확장한다.
- **Review queue:** save/export/import처럼 파일 시스템에 영향을 주는 op는 pending review 카드로 표시하고 approve/reject를 제공한다.
- **Controls:** pause, resume, terminate를 제공한다. terminate는 현재 세션/queue를 중단하는 동작이며 이미 적용된 변경은 undo로 처리한다.
- **Empty state:** agent port가 꺼진 경우 실행 명령 예시를 보여준다: `./build/marrow_editor_shell --agent-port 9876`.

---

## 7. Acceptance Criteria

- Agent/MCP read API로 project, skeleton, animation, constraints, mesh/deform 요약을 확인할 수 있다.
- Agent write API로 transform, draw-order, slot/attachment, deform/weight, 주요 constraint 편집 중 최소 한 vertical slice가 undo/redo와 export round-trip을 통과한다.
- 모든 mutation은 history와 activity feed에 `"Agent"` 출처로 남는다.
- whitelist 밖 경로를 사용하는 save/import/export 요청은 거부되고, 에디터 상태를 변경하지 않는다.
- `save`와 `export_runtime`은 review 승인 경로 없이 파일을 쓰지 않는 정책을 지원한다.
- UI에서 agent 연결 상태, 실행 중 op, 최근 변경, blocked/error 상태를 확인할 수 있다.
- 기존 headless editor smoke와 project export smoke가 계속 통과한다.

---

## 8. Validation Commands

기본 빌드 후 아래 명령을 사용한다.

```bash
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 5
```

```bash
source tools/mcp/venv/bin/activate && python3 tools/mcp/test_client.py
```

```bash
./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_e2e_export.mskl --export-binary /tmp/marrow_e2e_export.mbin
```

추가 권장 검증:

```bash
./build/marrow_fixture_smoke /tmp/marrow_e2e_export.mskl /tmp/player_idle.matl
```

```bash
./build/marrow_inspect --compare /tmp/marrow_e2e_export.mbin /tmp/marrow_e2e_export.mskl
```

---

## 9. Dependencies and Priority

**Dependencies**

- `src/editor/agent_dispatch.cpp`의 op schema/응답 형태 안정화.
- editor history snapshot과 dirty state가 agent mutation에서 일관되게 동작해야 한다.
- project edit 모델이 deform, weight, draw-order, constraints를 이미 표현할 수 있어야 한다.
- `tools/mcp/` tool schema와 C++ dispatcher 검증 규칙이 함께 업데이트되어야 한다.
- Agent UI는 visual renewal의 토큰/도킹 제약을 따라야 하며, 별도 웹 UI가 아니라 Dear ImGui 패널로 구현한다.

**Suggested Priority**

1. Dispatcher registry, 응답 schema, attribution 정리.
2. Read API 확장과 diagnostics.
3. Path whitelist와 save/export review.
4. Draw-order 또는 slot/attachment timeline write vertical slice.
5. Mesh deform/weight write vertical slice.
6. Constraint 타입 확장.
7. Agent panel/activity feed/global presence.
8. Import/export automation.

이 순서는 에이전트가 먼저 안전하게 “보고 설명”할 수 있게 만든 뒤, 작은 undoable edit을 하나씩 넓히고, 마지막에 파일 시스템 자동화와 고위험 batch 작업을 붙이는 흐름이다.
