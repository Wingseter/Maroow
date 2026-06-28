# Spine Parity Authoring & Export Enhancement Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` (recommended) or `superpowers:executing-plans` to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Maroow를 Spine Pro의 오픈소스 대체제로 사용할 때 남는 production authoring, 검증, 미디어 export, Spine export-format import 격차를 작고 검증 가능한 단위로 줄인다.

**Architecture:** 런타임 포맷과 재생 기능은 이미 넓게 구현되어 있으므로, 이번 범위는 editor authoring UX, result preview/export, import compatibility policy, standalone validation tooling을 기존 `.mskl`/`.mbin`/`.matl` 계약 위에 얹는다. 외부 Spine format 처리는 공개 문서 기반 독립 parser/exporter 정책을 유지한다.

**Tech Stack:** C++17 editor/importer/exporter, `spine_to_marrow`, Maroow runtime fixtures, renderer sample, project smoke, optional media encoder tooling.

---

## 1. 현재 상태와 근거

Maroow의 기존 방향은 `docs/root1/discription.md:12`에서 Spine Pro 오픈소스 대체제로 정의되어 있고, `docs/root1/discription.md:21`은 `Marrow Editor -> .mskl / .matl / .png -> molga-engine Runtime` 흐름을 기준 아키텍처로 둔다. `docs/root1/discription.md:26`은 standalone Marrow Editor를 본 배치, 키프레임, 애니메이션 편집 도구로 정의한다.

런타임 포맷은 이미 Spine류 기능 대부분을 담을 수 있다. `docs/root1/format-spec.md:14`는 `.mskl`을 setup data, skins, animation timelines, constraint metadata를 담는 런타임 스켈레톤 문서로 설명하고, `docs/root1/format-spec.md:22`는 bones, slots, skins, events, animations, mixing, IK/path/transform/physics constraint를 top-level key로 문서화한다. `docs/root1/format-spec.md:108`은 bone transform, slot attachment/color, mesh deform, draw order, event timeline 지원을 명시한다. `docs/root1/format-spec.md:202`와 `docs/root1/format-spec.md:238`은 production binary `.mbin`과 authoring-only `.marrow` 프로젝트 문서를 구분한다.

Spine import 쪽도 기반이 있다. `docs/root1/research-spine-import-atlas-packing.md:67`은 `.skel` 바이너리 구조를 조사했고, `docs/root1/research-spine-import-atlas-packing.md:90`은 바이너리 포맷이 버전 변화에 민감하므로 JSON 우선, binary 후순위 정책을 권장한다. `docs/root1/research-spine-import-atlas-packing.md:92`는 `.atlas` 구조와 PMA, rotate, trimming 데이터를 정리한다. `CMakeLists.txt:423`은 `spine_to_marrow` importer CLI, `CMakeLists.txt:327`은 `marrow_renderer_sample`, `CMakeLists.txt:441`은 `marrow_project_smoke`, `CMakeLists.txt:484`는 `marrow_editor_shell` 타깃을 정의한다.

현재 검증에 사용할 수 있는 명령은 다음과 같다.

```bash
cmake -S . -B build
cmake --build build
./build/spine_to_marrow assets/fixtures/spine_import_sample.json /tmp/spine_import_sample.mskl
./build/spine_to_marrow assets/fixtures/spine_import_sample.atlas /tmp/spine_import_sample.matl
./build/marrow_spine_import_smoke assets/fixtures/spine_import_sample.json assets/fixtures/spine_import_sample.atlas
./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl --export-binary /tmp/player_idle_project_export.mbin
./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin /tmp/player_idle_project_export.mskl
./build/marrow_renderer_sample --hud --skip-render assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

공식 Spine 4.2 예제 import batch는 기존 AGENTS 검증 목록에 맞춰 유지한다.

```bash
mkdir -p /tmp/marrow-spine-batch
for asset in owl goblins spineboy tank raptor; do ./build/spine_to_marrow assets/spine-examples/$asset/$asset-pro.json /tmp/marrow-spine-batch/$asset.mskl || exit 1; done
for asset in owl goblins spineboy tank raptor; do ./build/spine_to_marrow assets/spine-examples/$asset/$asset.atlas /tmp/marrow-spine-batch/$asset.matl || exit 1; done
for asset in owl goblins spineboy tank raptor; do ./build/marrow_fixture_smoke /tmp/marrow-spine-batch/$asset.mskl /tmp/marrow-spine-batch/$asset.matl || exit 1; done
for asset in owl goblins spineboy tank raptor; do ./build/marrow_renderer_sample --skip-render /tmp/marrow-spine-batch/$asset.mskl /tmp/marrow-spine-batch/$asset.matl || exit 1; done
```

---

## 2. 범위 경계

이번 plan에 포함하는 범위:

- Graph editor, Dopesheet timeline, 키/커브 authoring UX.
- Preview crop, transparent background, bounds 계산, frame stepping.
- PNG/JPEG/GIF/video frame sequence export와 export preset.
- Spine JSON/atlas import coverage 확대와 버전 호환성 정책.
- Spine `.skel` binary import는 독립 구현 가능성이 확인된 뒤 선택 기능으로만 취급.
- Skeleton Viewer와 유사한 standalone validation/preview 도구.

이번 plan에서 제외하는 범위:

- Live2D parameter, deformer, warp deformer, ArtMesh식 authoring 모델.
- AI Agent Control/MCP 기능 자체. 단, export나 validator 명령을 agent가 호출할 수 있게 CLI boundary를 안정화하는 것은 통합 지점으로만 허용한다.
- molga-engine 런타임 embedding API 확장. 본 plan은 authoring/export/import 검증에 집중한다.
- 새로운 Maroow 런타임 포맷 대체. 기존 `.mskl`, `.mbin`, `.matl`, `.marrow` 흐름을 유지한다.

---

## 3. Gap 분석

### Graph Editor

현재 런타임은 linear, stepped, cubic bezier curve를 읽고 평가할 수 있지만, production authoring 관점에서는 선택 키의 curve handle 조작, tangent preset, multi-key edit, channel filter, snapping, normalization된 value scale이 필요하다. 목표는 runtime curve 표현을 바꾸지 않고 `.marrow`의 편집 상태와 `.mskl` export를 왕복시키는 것이다.

### Dopesheet Timeline Scalability

현재 editor smoke는 timeline edit와 undo/redo를 검증하지만, 실제 Spine급 프로젝트에서는 수천 키, 다수 animation, slot, deform, event, draw-order track을 빠르게 탐색해야 한다. 필요한 격차는 track virtualization, key density 표시, channel folding, range selection, ripple/scale/move 편집, 성능 budget 계측이다.

### Export Preview와 Cropping

게임 납품용 export는 렌더 결과를 보며 crop, padding, scale, background, alpha, sampling FPS를 조정해야 한다. 현재 renderer sample은 준비와 HUD 검증에 강하지만, export 전용 preview surface, output bounds, frame range, edge bleed 확인 흐름이 별도 제품 기능으로 정리되어 있지 않다.

### Image, Video, GIF, Sequence Export

현재 `.mskl`, `.mbin`, `.matl` export와 atlas packer 검증은 존재한다. 남은 격차는 애니메이터가 바로 공유할 수 있는 PNG/JPEG still, transparent PNG sequence, GIF, video sequence export다. 외부 video encoder를 직접 번들할지, PNG sequence만 코어로 두고 ffmpeg bridge를 optional로 둘지 정책이 필요하다.

### Spine `.skel` Binary Import Policy

`.skel`은 버전 민감도가 높다. JSON/atlas import가 안정적인 상태에서만 optional milestone로 둔다. 첫 구현은 Spine 4.2의 documented binary export format만 대상으로 하며, 버전 mismatch는 실패해야 한다. Spine runtime 코드를 포팅하지 않고 독립 parser로 구현해야 한다.

### Skeleton Viewer-like Validator

Spine ecosystem의 Skeleton Viewer처럼 import 결과를 빠르게 열고, animation/skin을 바꾸고, bounds와 event를 확인하고, renderer와 runtime smoke를 한 번에 돌리는 standalone validation shell이 필요하다. 기존 `marrow_renderer_sample`, `marrow_inspect`, `marrow_fixture_smoke`를 통합 UX로 묶되, batch CI와 interactive preview가 같은 core validator를 써야 한다.

### Importer Version Compatibility

현재 공식 Spine 4.2 예제 검증은 중요 기준이다. 다음 단계는 `skeleton.spine` 버전별 compatibility matrix, warning/error taxonomy, unsupported feature report, importer golden output 비교를 도입하는 것이다. 지원 범위는 "Spine export format import"로 설명하고, Spine editor 또는 runtime과의 API 호환으로 표현하지 않는다.

---

## 4. 단계별 계획

### Phase 1: Importer Compatibility Baseline

- [ ] `spine_to_marrow`가 `skeleton.spine` 값을 읽어 import report에 기록하게 한다.
- [ ] Spine JSON import report에 `accepted`, `accepted_with_warnings`, `rejected` 상태를 추가한다.
- [ ] unsupported field를 누락시키지 말고 path 기반 warning으로 기록한다. 예: `animations.walk.slots.body.twoColor`.
- [ ] 공식 Spine 4.2 예제 batch 결과를 `marrow_inspect` count와 warning snapshot으로 고정한다.
- [ ] `docs/root1/research-spine-import-atlas-packing.md`의 정책을 반영해 `.skel`은 Phase 7 전까지 unsupported explicit error로 유지한다.

검증:

```bash
cmake --build build --target spine_to_marrow marrow_spine_import_smoke marrow_inspect
./build/marrow_spine_import_smoke assets/fixtures/spine_import_sample.json assets/fixtures/spine_import_sample.atlas
mkdir -p /tmp/marrow-spine-batch
for asset in owl goblins spineboy tank raptor; do ./build/spine_to_marrow assets/spine-examples/$asset/$asset-pro.json /tmp/marrow-spine-batch/$asset.mskl || exit 1; done
for asset in owl goblins spineboy tank raptor; do ./build/marrow_inspect /tmp/marrow-spine-batch/$asset.mskl | sed -n '1,3p' || exit 1; done
```

### Phase 2: Skeleton Viewer-like Validator MVP

- [ ] `marrow_validator` CLI 또는 `marrow_renderer_sample --validate` mode를 추가해 skeleton, atlas, optional animation, optional skin을 한 번에 받는다.
- [ ] JSON load, atlas page load, setup pose prepare, animation sample, bounds/event scan을 하나의 report로 출력한다.
- [ ] `--skip-render`는 CI용 deterministic validation으로 유지하고, interactive host에서는 preview window를 열 수 있게 한다.
- [ ] failure는 load error, unsupported import field, renderer preparation error, runtime sampling error로 분류한다.
- [ ] validator report를 machine-readable JSON으로도 출력한다.

검증:

```bash
cmake --build build --target marrow_renderer_sample marrow_fixture_smoke
./build/marrow_renderer_sample --hud --skip-render assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
./build/marrow_fixture_smoke assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
```

### Phase 3: Dopesheet Scalability Slice

- [ ] `.marrow` project load 후 timeline channel 목록을 animation, bone, slot, deform, draw-order, event 기준으로 flatten하는 read-only model을 만든다.
- [ ] editor timeline UI에 channel folding, text filter, selected animation filter를 추가한다.
- [ ] 1,000개 이상 keyframe fixture를 추가해 visible row만 draw하는 virtualization budget을 측정한다.
- [ ] range selection으로 keyframe move/copy/delete를 처리하고 undo/redo를 하나의 grouped edit로 묶는다.
- [ ] timeline edit 후 `marrow_project_smoke --export-runtime`가 기존 runtime timeline으로 동일하게 export되는지 검증한다.

검증:

```bash
cmake --build build --target marrow_project_smoke marrow_editor_shell
./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl --export-binary /tmp/player_idle_project_export.mbin
./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin /tmp/player_idle_project_export.mskl
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

### Phase 4: Graph Editor MVP

- [ ] 선택 animation/channel/keyframe을 Graph view model로 노출한다.
- [ ] rotate, translate, scale, shear channel에 대해 key point, in/out handle, stepped marker를 표시한다.
- [ ] tangent preset은 linear, stepped, ease-in, ease-out, ease-in-out, custom bezier로 제한한다.
- [ ] custom bezier handle edit는 `.marrow` edit state와 `.mskl` curve 배열 `[cx1, cy1, cx2, cy2]`로 round-trip한다.
- [ ] multi-key value scale/move는 channel type별 단위를 보존하고 undo/redo 한 action으로 합친다.

검증:

```bash
cmake --build build --target marrow_project_smoke marrow_fixture_smoke
./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/graph_curve_export.mskl
./build/marrow_fixture_smoke /tmp/graph_curve_export.mskl assets/fixtures/player_idle.matl
python3 -m json.tool /tmp/graph_curve_export.mskl > /dev/null
```

### Phase 5: Export Preview, Cropping, Still/Sequence Export

- [ ] export preview model에 animation, skin, start/end time, FPS, scale, background, alpha, padding, crop mode를 추가한다.
- [ ] crop mode는 `skeleton_bounds`, `animation_bounds`, `manual_rect` 세 가지로 제한한다.
- [ ] still PNG/JPEG export를 먼저 구현하고, frame stepping 결과가 renderer sample과 같은 draw preparation path를 쓰게 한다.
- [ ] transparent PNG sequence export를 추가한다. 파일명은 `name_0000.png` 형식으로 deterministic하게 만든다.
- [ ] export preset을 `.marrow` authoring-only state에 저장하고 runtime asset export와 분리한다.

검증:

```bash
cmake --build build --target marrow_editor_shell marrow_renderer_sample
./build/marrow_renderer_sample --skip-render assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

추가 fixture 검증은 export 기능 구현 시 다음 파일을 생성하도록 한다.

```text
/tmp/marrow-export/player_idle_0000.png
/tmp/marrow-export/player_idle_0001.png
/tmp/marrow-export/player_idle_preview.jpg
```

### Phase 6: GIF와 Video Export Bridge

- [ ] core exporter는 PNG sequence를 canonical output으로 유지한다.
- [ ] GIF export는 palette generation과 frame delay 검증이 가능한 내부 encoder 또는 optional dependency로 둔다.
- [ ] video export는 bundled encoder보다 `ffmpeg` bridge를 우선 검토한다. `ffmpeg`가 없으면 PNG sequence와 명령 안내만 출력한다.
- [ ] export report에 frame count, FPS, duration, output size, crop rect, alpha 여부를 기록한다.
- [ ] CI에서는 PNG sequence와 GIF header/metadata까지만 검증하고, platform encoder 의존 테스트는 opt-in으로 분리한다.

검증:

```bash
cmake --build build --target marrow_editor_shell
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

### Phase 7: Optional Spine `.skel` Binary Import

- [ ] `.skel` import는 `--experimental-spine-skel` 플래그 없이는 거부한다.
- [ ] 첫 지원 범위는 Spine 4.2 documented binary export format으로 제한한다.
- [ ] parser는 varint, string table, ref string, float, boolean, color primitive를 독립 구현한다.
- [ ] JSON importer와 같은 intermediate model로 변환해 `.mskl` export path를 공유한다.
- [ ] JSON과 `.skel`이 같은 소스에서 export된 fixture를 pair로 두고 `marrow_inspect` count와 sampled pose를 비교한다.
- [ ] minor version mismatch, unknown field layout, truncated stream, invalid string reference는 recover하지 않고 명시적으로 실패한다.

검증:

```bash
cmake --build build --target spine_to_marrow marrow_inspect marrow_fixture_smoke
./build/spine_to_marrow --experimental-spine-skel assets/fixtures/spine_import_sample.skel /tmp/spine_import_sample_from_skel.mskl
./build/marrow_inspect /tmp/spine_import_sample_from_skel.mskl
./build/marrow_fixture_smoke /tmp/spine_import_sample_from_skel.mskl assets/fixtures/spine_import_sample_hero_page.matl
```

---

## 5. 필요한 Fixtures와 Tests

필수 fixture:

- `assets/fixtures/player_idle.marrow`: editor project round-trip, timeline, export preset, preview smoke.
- `assets/fixtures/player_idle.mskl`, `assets/fixtures/player_idle.matl`, `assets/fixtures/player_idle.mbin`: runtime/export equivalence baseline.
- `assets/fixtures/spine_import_sample.json`, `assets/fixtures/spine_import_sample.atlas`: importer smoke baseline.
- `assets/spine-examples/owl`, `assets/spine-examples/goblins`, `assets/spine-examples/spineboy`, `assets/spine-examples/tank`, `assets/spine-examples/raptor`: Spine 4.2 compatibility matrix baseline.

추가해야 할 fixture:

- 대형 timeline fixture: 1,000개 이상 key, 50개 이상 channel, event/draw-order/deform 포함.
- Graph curve fixture: linear, stepped, custom bezier, tangent preset을 모두 포함.
- Export bounds fixture: attachment가 animation 중 canvas 밖으로 나가고, clipping과 transparent edge를 포함.
- Export media expected metadata: frame count, crop rect, FPS, image dimensions를 담은 JSON golden.
- Optional `.skel` pair fixture: 같은 Spine source에서 export된 `.json`과 `.skel` 한 쌍. 라이선스와 재배포 가능 여부 확인 후 추가한다.

테스트 범위:

- Import report snapshot test.
- Large dopesheet model ordering/filtering test.
- Graph curve round-trip test.
- Export crop rect and frame count test.
- PNG sequence deterministic filename test.
- Validator machine-readable report schema test.
- `.skel` parser primitive unit test와 malformed stream negative test.

---

## 6. 법무와 라이선스 메모

- 기능 설명은 "Spine export format import"로 표현한다. "Spine runtime 호환 구현" 또는 "Spine 내부 코드 포팅"으로 표현하지 않는다.
- 공개 문서화된 JSON, atlas, binary export format을 독립적으로 파싱한다.
- Spine runtime 소스, `SkeletonJson`, `SkeletonBinary`, `Animation`, constraint solver 구현을 복사하거나 번역하지 않는다.
- fixture는 재배포 가능한 샘플만 저장한다. 공식 예제 asset의 라이선스가 repo 재배포와 CI 사용을 허용하는지 계속 추적한다.
- `.skel` binary import는 포맷이 버전 민감하므로 지원 버전을 명시하고, unsupported version은 경고가 아니라 실패로 처리한다.
- UI와 문서에서는 Maroow를 독립 도구로 설명하고, Spine은 import 가능한 외부 export format의 출처로만 언급한다.

---

## 7. Acceptance Criteria

- [ ] `spine_to_marrow` import report가 Spine version, accepted status, warnings, unsupported fields를 출력한다.
- [ ] 공식 Spine 4.2 예제 batch가 import, inspect, fixture smoke, renderer setup validation을 통과한다.
- [ ] validator가 `.mskl`/`.matl`을 받아 setup pose, animation sample, event/bounds, renderer preparation을 하나의 report로 검증한다.
- [ ] Dopesheet가 대형 timeline fixture에서 channel filter/fold/range edit를 처리하고 grouped undo/redo를 보존한다.
- [ ] Graph editor가 linear, stepped, custom bezier curve를 편집하고 `.marrow -> .mskl -> runtime smoke` round-trip을 통과한다.
- [ ] Export preview가 crop rect, padding, scale, alpha/background 설정을 보여주고 deterministic PNG/JPEG still을 만든다.
- [ ] PNG sequence export가 frame count, FPS, deterministic filename, crop size를 report와 golden metadata로 검증한다.
- [ ] GIF/video export는 optional dependency 정책과 실패 메시지가 명확하다.
- [ ] `.skel` binary import는 experimental flag와 지원 버전 제한이 문서화되고, unsupported version과 malformed stream을 명확히 거부한다.
- [ ] 법무 메모가 구현 PR과 사용자 문서에 반영된다.

최종 validation command set:

```bash
cmake -S . -B build
cmake --build build
./build/marrow_spine_import_smoke assets/fixtures/spine_import_sample.json assets/fixtures/spine_import_sample.atlas
./build/spine_to_marrow assets/fixtures/spine_import_sample.json /tmp/spine_import_sample.mskl
./build/spine_to_marrow assets/fixtures/spine_import_sample.atlas /tmp/spine_import_sample.matl
./build/marrow_fixture_smoke /tmp/spine_import_sample.mskl /tmp/spine_import_sample.matl
./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl --export-binary /tmp/player_idle_project_export.mbin
./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin /tmp/player_idle_project_export.mskl
./build/marrow_renderer_sample --hud --skip-render assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
mkdir -p /tmp/marrow-spine-batch
for asset in owl goblins spineboy tank raptor; do ./build/spine_to_marrow assets/spine-examples/$asset/$asset-pro.json /tmp/marrow-spine-batch/$asset.mskl || exit 1; done
for asset in owl goblins spineboy tank raptor; do ./build/spine_to_marrow assets/spine-examples/$asset/$asset.atlas /tmp/marrow-spine-batch/$asset.matl || exit 1; done
for asset in owl goblins spineboy tank raptor; do ./build/marrow_fixture_smoke /tmp/marrow-spine-batch/$asset.mskl /tmp/marrow-spine-batch/$asset.matl || exit 1; done
for asset in owl goblins spineboy tank raptor; do ./build/marrow_renderer_sample --skip-render /tmp/marrow-spine-batch/$asset.mskl /tmp/marrow-spine-batch/$asset.matl || exit 1; done
```

---

## 8. 우선순위 권장

권장 순서는 `Phase 1 -> Phase 2 -> Phase 5 -> Phase 3 -> Phase 4 -> Phase 6 -> Phase 7`이다.

이유:

- Importer compatibility와 validator는 이후 모든 authoring/export work의 회귀 기준이 된다.
- Export preview와 PNG/JPEG/sequence는 Spine Pro 대체제로서 사용자 체감 가치가 높고, 런타임 변경 없이 수직 완성이 가능하다.
- Dopesheet와 Graph editor는 생산성 핵심이지만 UI와 undo/redo 영향 범위가 크므로 validator/export baseline 뒤에 진행한다.
- GIF/video는 플랫폼 dependency 정책이 필요하므로 PNG sequence 이후로 둔다.
- `.skel` binary import는 가장 위험하고 버전 민감하다. JSON/atlas import와 validator가 충분히 안정된 뒤 optional로 진행한다.
