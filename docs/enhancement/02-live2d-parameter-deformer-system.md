# Live2D 영감 파라미터/디포머 시스템 강화 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use `superpowers:subagent-driven-development` or `superpowers:executing-plans` when implementing this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Maroow의 기존 Spine-like 런타임/에디터 구조를 깨지 않으면서, Live2D식 작업 흐름에서 유용한 파라미터 기반 모델링과 디포머 레이어를 Maroow-native 기능으로 추가한다.

**Architecture:** `.marrow`는 고해상도 authoring source로 Parameter, Keyform, Deformer, Expression, Lip-sync 매핑을 보관하고, export 단계에서 `.mskl`/`.mbin` 런타임 문서로 컴파일한다. 런타임은 기존 `SkeletonData -> Skeleton -> PreparedScene / RenderCommandList` 흐름을 유지하되, 본/슬롯/메시/FFD 이후 또는 명확히 정의된 단계에서 파라미터 디포머 평가 결과를 적용한다.

**Tech Stack:** C++17, CMake, JSON `.marrow`/`.mskl`, compact `.mbin`, existing Maroow runtime/editor/renderer smoke harnesses.

---

## 1. 목표와 비목표

### 목표

- 기존 본, 슬롯, 스킨, 메시, linked mesh, FFD, IK/path/transform/physics constraint, atlas, export 파이프라인을 유지한다.
- Spine parity와 별개인 **파라미터 기반 모델링 제품 축**을 만든다.
- 캐릭터 얼굴, 시선, 입모양, 표정, VTuber식 실시간 조작에 필요한 authoring 구조를 추가한다.
- `.marrow`에는 사람이 편집하기 좋은 원본 데이터를 보존하고, `.mskl`/`.mbin`에는 런타임 평가에 필요한 최소 구조를 내보낸다.

### 비목표

- Live2D Cubism의 proprietary format, runtime Core, SDK ABI, 파일 구조, 파라미터 이름 규약을 복제하지 않는다.
- Live2D 모델 파일을 직접 로드하는 호환 레이어를 1차 목표로 삼지 않는다.
- 기존 Spine-like runtime-first 설계를 파라미터 전용 엔진으로 대체하지 않는다.

이 계획은 Live2D의 **워크플로 개념**에서 영감을 받는 Maroow-native 설계다.

## 2. 현재 상태와 근거

- [docs/root1/concepts.md](../root1/concepts.md)는 Maroow 데이터 흐름을 `.mskl or .mbin -> SkeletonData -> Skeleton -> PreparedScene / RenderCommandList`로 정의한다. `.marrow`는 editor-only source이며 export로 런타임 자산을 만든다.
- [docs/root1/format-spec.md](../root1/format-spec.md)는 현재 `.mskl` version 1이 bones, slots, skins, mesh/linked_mesh/path/clipping attachments, deform timelines, draw order, events, IK/path/transform/physics constraints를 담는다고 설명한다.
- [docs/root1/format-spec.md](../root1/format-spec.md)의 `.mbin` 섹션은 `.mskl`과 같은 논리 문서를 compact binary로 복원한다고 설명한다. 따라서 새 런타임 데이터는 JSON과 binary equivalence 검증 대상이다.
- [docs/root1/format-spec.md](../root1/format-spec.md)의 `.marrow` 섹션은 `timeline_edits`, `mesh_edits`, `constraint_edits`, `atlas_packs`를 editor-only authoring payload로 둔다. 파라미터/디포머 authoring도 이 원칙을 따라야 한다.
- [docs/root1/fixtures.md](../root1/fixtures.md)는 `player_idle.marrow`가 transform, FFD, draw-order, event, mesh-weight, constraint edits를 export하고 runtime smoke로 검증하는 대표 fixture라고 설명한다.
- [docs/design/maroow-editor-visual-renewal-spec.md](../design/maroow-editor-visual-renewal-spec.md)는 Maroow를 Spine/Live2D류 프로페셔널 2D 에디터로 포지셔닝하지만, 현재 구현 기반은 본/슬롯/타임라인/웨이트/제약 중심의 skeletal editor다.

## 3. Spine Parity와 별도 제품 축인 이유

Spine-like 기능은 시간축 애니메이션, 본 계층, 슬롯 교체, weighted mesh, FFD, constraints, skin composition을 중심으로 한다. 현재 Maroow도 이 축에서 런타임 우선 구조와 export 검증을 갖추고 있다.

Live2D식 작업의 핵심은 다르다. 사용자는 `ParamAngleX`, `ParamMouthOpenY` 같은 연속 파라미터를 조작하고, 여러 파라미터 조합이 ArtMesh와 디포머의 형태를 보간한다. 표정 프리셋, 립싱크, 눈 깜빡임, 시선, VTuber 입력 매핑은 시간축 clip보다 **상태 공간과 실시간 입력**에 가깝다.

따라서 이 기능은 "Spine 기능 몇 개 추가"가 아니라 다음 제품 축으로 다룬다.

- **Skeletal animation pillar:** 본/슬롯/스킨/타임라인/constraint/export.
- **Parameter modeling pillar:** 파라미터, keyform surface, deformer stack, expression/lip-sync/runtime input mapping.
- **Agent-assisted authoring pillar:** AI가 파라미터, 표정, mouth mapping, deformer keyform을 생성/조정할 수 있는 편집 표면.

분리해야 기존 `.mskl` 런타임 문서와 fixture들이 불필요한 파라미터 복잡도를 강제로 떠안지 않는다.

## 4. 제안 개념

### Parameters

- 연속 또는 이산 입력 채널.
- 예: `face.angle_x`, `face.angle_y`, `eye.open_l`, `eye.open_r`, `mouth.open`, `mouth.form`, `expression.smile`.
- 필드: `id`, `name`, `min`, `max`, `default`, `type`, `clamp`, `ui_step`, `units`.
- 런타임 인스턴스는 parameter value buffer를 갖고, AnimationState나 외부 입력이 값을 쓴다.

### ParameterGroups

- 에디터 UI와 런타임 입력 관리 단위.
- 예: `Face`, `Eyes`, `Mouth`, `Expression`, `PhysicsInput`.
- 필드: `id`, `name`, `parameters[]`, `collapsed`, `color_tag`, `exclusive_mode`.
- `.marrow`에는 UI 배치와 그룹 상태를 저장하되, `.mskl`에는 평가에 필요한 그룹 메타데이터만 export한다.

### Keyforms

- 특정 파라미터 좌표에서의 형태 snapshot.
- 1D keyform: `mouth.open = 0.0/0.5/1.0`.
- 2D keyform: `(angle_x, angle_y) = (-30, 15)`.
- N차원 keyform은 초기에는 제한한다. 1D와 2D를 먼저 지원하고, 3개 이상은 명시적 blend graph가 있는 경우에만 허용한다.
- payload는 mesh vertex delta, deformer control points, rotation deformer angle, ArtPath stroke delta를 담는다.

### WarpDeformer

- ArtMesh 또는 하위 디포머들을 감싸는 2D lattice.
- 필드: `id`, `name`, `parent`, `target_slots[]`, `grid_cols`, `grid_rows`, `control_points`, `keyforms`.
- 평가 결과는 대상 mesh vertex에 additive 또는 override delta로 적용한다.
- 기존 FFD와 충돌하지 않도록 적용 순서를 명시한다: setup mesh -> skeletal skinning/linked mesh FFD -> parameter deform layer -> renderer preparation.

### RotationDeformer

- 중심점과 각도 범위를 가진 회전형 디포머.
- 얼굴 각도, 눈썹, 입꼬리, 머리카락 묶음 같은 local pivot 변형에 사용한다.
- 필드: `id`, `name`, `parent`, `pivot`, `influence`, `parameter_bindings`, `keyforms`.
- 본 회전과 유사하지만 bone hierarchy를 강제하지 않는 authoring object로 둔다.

### BlendShape-like Deform Layer

- 메시별 named shape delta 묶음.
- 필드: `id`, `target_slot`, `target_attachment`, `shapes[]`, `parameter_bindings[]`, `blend_mode`.
- `blend_mode`는 초기에는 `additive_clamped`와 `normalized_override`만 허용한다.
- 기존 `.mskl` deform timeline은 시간축 FFD이고, 이 레이어는 파라미터 공간 FFD다. 둘을 같은 데이터 구조로 억지 통합하지 않는다.

### ArtPath-like Stroke Object

- 렌더 가능한 stroke/path authoring object.
- 용도: 눈썹 선, 입 선, 효과선, 머리카락 가닥.
- 기존 path attachment/constraint와 혼동하지 않도록 런타임 타입을 분리한다.
- 필드: `id`, `name`, `parent_deformer`, `points`, `width`, `color`, `cap`, `join`, `parameter_keyforms`.
- 1차 구현은 CPU tessellation 또는 renderer sample의 prepared stroke primitive로 제한한다.

### Expression Presets

- 여러 parameter target을 묶은 named preset.
- 예: `smile`, `angry`, `sad`, `blink`, `surprised`.
- 필드: `id`, `name`, `targets[]`, `duration`, `blend`, `priority`, `reset_policy`.
- 런타임은 expression stack을 AnimationState와 독립적으로 적용할 수 있어야 한다.

### Lip-sync / Mouth Parameter Mapping

- 오디오/외부 입력에서 mouth parameter로 들어오는 mapping.
- 초기 범위: amplitude -> `mouth.open`, vowel class -> `mouth.form`.
- 필드: `source`, `parameter`, `scale`, `bias`, `smoothing`, `attack`, `release`, `phoneme_map`.
- 오디오 분석기는 별도 기능으로 두고, 이 계획의 1차 범위는 이미 계산된 amplitude/phoneme 이벤트를 parameter buffer에 넣는 runtime API와 fixture다.

## 5. Runtime / Editor / File-format 영향

### `.marrow` Authoring Source

`.marrow`에 editor-only 원본을 추가한다.

```json
{
  "parameter_model": {
    "parameters": [],
    "groups": [],
    "deformers": [],
    "blend_shapes": [],
    "art_paths": [],
    "expressions": [],
    "lip_sync": {}
  }
}
```

- 에디터는 lattice control point, keyform grid, expression UI 상태처럼 런타임에 불필요한 authoring metadata를 보존한다.
- `timeline_edits`, `mesh_edits`, `constraint_edits`와 병렬인 새 authoring 영역으로 둔다.
- AI agent authoring surface는 이 영역을 직접 읽고 쓰되, 기존 원칙처럼 undo stack을 경유해야 한다.

### `.mskl` Runtime Export

`.mskl`에는 평가에 필요한 compact logical document만 추가한다.

```json
{
  "parameters": [],
  "parameterGroups": [],
  "parameterDeformers": [],
  "parameterShapes": [],
  "artPaths": [],
  "expressions": [],
  "lipSync": {}
}
```

- 기존 animations/deform timelines와 독립된 root-level section으로 둔다.
- version 1과 호환성을 위해 loader는 없는 section을 빈 값으로 처리한다.
- breaking change가 필요해지는 시점에는 `.mskl` version bump 계획을 별도 story로 잡는다.

### `.mbin` Runtime Export

- `.mbin`은 `.mskl`의 새 logical section을 손실 없이 round-trip해야 한다.
- packed animation section과 parameter section은 별도다. 파라미터 keyform 최적화가 필요하면 후속 binary section을 추가한다.
- `marrow_inspect --compare`가 parameter/deformer payload 동등성을 보고해야 한다.

### Runtime Evaluation

- `SkeletonData`: immutable parameter/deformer definitions를 소유한다.
- `Skeleton`: per-instance parameter values와 evaluated deformer cache를 소유한다.
- `AnimationState`: 선택적으로 parameter timeline이나 expression trigger를 적용할 수 있지만, parameter buffer 자체의 소유자는 아니다.
- Renderer handoff: `PreparedScene` 생성 전에 final mesh/stroke geometry를 계산해 기존 render command path가 사용할 수 있게 한다.

### Editor Impact

- Parameter panel: parameter group tree, slider, numeric input, reset/default.
- Deformer mode: WarpDeformer grid editing, RotationDeformer pivot editing, target slot binding.
- Keyform grid: 1D/2D parameter coordinate에서 shape key 저장/복제/미러.
- Expression panel: preset 생성, preview, blend duration, priority.
- Lip-sync panel: mouth parameter mapping, smoothing preview.
- Agent commands: `parameters.list`, `parameter.set`, `deformer.create`, `keyform.capture`, `expression.create`, `lip_sync.map` 같은 명령을 추가하되, 저장/export는 기존 명시적 op 원칙을 유지한다.

## 6. 단계별 계획

### Phase 0: 용어와 포맷 경계 고정

- [ ] `docs/root1/format-spec.md`에 parameter/deformer section의 초안 이름을 확정하는 별도 story를 만든다.
- [ ] `.marrow` authoring section과 `.mskl` runtime section의 필드 차이를 표로 정의한다.
- [ ] Live2D proprietary format/Core를 복제하지 않는다는 법적/제품 원칙을 importer 문서와 PRD에 명시한다.
- [ ] 기존 `player_idle.marrow` fixture를 변경하지 않는 최소 신규 fixture 이름을 정한다.

### Phase 1: Parameter Runtime Skeleton

- [ ] `SkeletonData`에 immutable `ParameterDefinition`과 `ParameterGroupDefinition`을 추가한다.
- [ ] `Skeleton`에 per-instance parameter value buffer를 추가한다.
- [ ] loader가 `.mskl.parameters`와 `.mskl.parameterGroups`를 읽고, 없는 경우 빈 배열로 처리한다.
- [ ] `marrow_inspect`가 parameter count/group count/default range를 출력한다.
- [ ] `./build/marrow_unit_tests`에 parameter clamp/default/reset 단위 테스트를 추가한다.

### Phase 2: `.marrow` Authoring Source와 Export

- [ ] `.marrow.parameter_model.parameters`와 `groups`를 parse/save한다.
- [ ] project export가 `.mskl.parameters`와 `.mskl.parameterGroups`로 내보낸다.
- [ ] `.mbin` export/import가 parameter section을 round-trip한다.
- [ ] `marrow_project_smoke`에 parameter-only fixture export 검증을 추가한다.

### Phase 3: BlendShape-like Mesh Deform Layer

- [ ] 메시 attachment를 대상으로 하는 named shape delta 구조를 추가한다.
- [ ] 1D parameter binding에서 shape weight를 계산한다.
- [ ] 기존 FFD deform timeline과 shape delta의 적용 순서를 단위 테스트로 고정한다.
- [ ] `marrow_fixture_smoke`가 setup pose, animation FFD, parameter shape를 함께 평가한다.
- [ ] renderer sample의 `--skip-render` 경로에서 deformed mesh bounds가 예상 범위인지 검증한다.

### Phase 4: WarpDeformer / RotationDeformer

- [ ] WarpDeformer lattice definition과 target slot binding을 `.marrow`와 `.mskl`에 추가한다.
- [ ] 2D parameter keyform 보간을 bilinear grid로 제한해 구현한다.
- [ ] RotationDeformer pivot/influence 평가를 추가한다.
- [ ] deformer nesting은 1단계 parent-child까지만 허용하고, cycle detection을 loader validation에 추가한다.
- [ ] performance benchmark에 100개 ArtMesh-equivalent target의 parameter update cost를 추가한다.

### Phase 5: ArtPath-like Stroke Object

- [ ] 기존 path constraint attachment와 분리된 renderable stroke type을 정의한다.
- [ ] stroke points/width/color를 parameter keyform으로 변형한다.
- [ ] renderer handoff에 stroke prepared primitive를 추가한다.
- [ ] atlas가 없어도 path-only fixture가 renderer preparation을 통과하도록 한다.

### Phase 6: Expression / Lip-sync Runtime

- [ ] Expression preset stack을 추가하고 priority/blend/reset policy를 구현한다.
- [ ] runtime API에서 expression trigger와 parameter direct set을 제공한다.
- [ ] Lip-sync mapping은 amplitude와 phoneme class 입력을 mouth parameter로 변환한다.
- [ ] smoothing/attack/release를 deterministic test로 고정한다.
- [ ] AnimationState와 외부 parameter input이 같은 parameter를 쓸 때 우선순위 규칙을 문서화한다.

### Phase 7: Editor Authoring Tools

- [ ] Parameter panel을 추가해 group별 slider/default/reset을 제공한다.
- [ ] Deformer edit mode를 추가해 lattice point, pivot, target slot binding을 편집한다.
- [ ] Keyform capture UI를 추가해 현재 mesh/deformer 상태를 parameter coordinate에 저장한다.
- [ ] Expression panel과 lip-sync mapping panel을 추가한다.
- [ ] 모든 editor mutation은 undo/redo와 dirty state를 통과한다.

### Phase 8: Agent Authoring Surface

- [ ] Agent dispatch schema에 parameter/deformer read commands를 먼저 추가한다.
- [ ] mutation commands는 `parameter.set`, `deformer.create`, `keyform.capture`, `expression.create`, `lip_sync.map` 순서로 작은 vertical slice를 만든다.
- [ ] AI가 생성한 변경은 기존 agent attribution UI/undo label과 연결한다.
- [ ] destructive 또는 대량 keyform 변경은 summary와 affected target count를 반환한다.

## 7. 제안 Fixture와 검증 명령

### 신규 Fixture

- `assets/fixtures/parameter_face_basic.mskl`
  - bones/slots는 최소화하고 `parameters`, `parameterGroups`, 1D mouth shape를 포함한다.
- `assets/fixtures/parameter_face_basic.mbin`
  - 같은 문서의 binary round-trip 기준 파일.
- `assets/fixtures/parameter_face_basic.matl`
  - mouth/face mesh region을 위한 최소 atlas metadata.
- `assets/fixtures/parameter_face_basic.marrow`
  - `.marrow.parameter_model` authoring source와 export 검증용.
- `assets/fixtures/parameter_deformer_grid.mskl`
  - 2D angle parameter와 WarpDeformer bilinear keyform 검증.
- `assets/fixtures/parameter_expression_lipsync.mskl`
  - expression preset과 mouth mapping 검증.
- `assets/fixtures/art_path_stroke.mskl`
  - atlas 없는 stroke 렌더 준비 검증.

### 기본 검증 명령

```bash
cmake -S . -B build
cmake --build build
./build/marrow_unit_tests
python3 -m json.tool assets/fixtures/parameter_face_basic.mskl > /dev/null
python3 -m json.tool assets/fixtures/parameter_face_basic.marrow > /dev/null
./build/marrow_inspect assets/fixtures/parameter_face_basic.mskl
./build/marrow_inspect --export-binary assets/fixtures/parameter_face_basic.mbin assets/fixtures/parameter_face_basic.mskl
./build/marrow_inspect --compare assets/fixtures/parameter_face_basic.mbin assets/fixtures/parameter_face_basic.mskl
./build/marrow_fixture_smoke assets/fixtures/parameter_face_basic.mskl assets/fixtures/parameter_face_basic.matl
./build/marrow_project_smoke assets/fixtures/parameter_face_basic.marrow --export-runtime /tmp/parameter_face_basic_export.mskl --export-binary /tmp/parameter_face_basic_export.mbin
./build/marrow_renderer_sample --skip-render /tmp/parameter_face_basic_export.mskl assets/fixtures/parameter_face_basic.matl
```

### 성능 검증 명령

```bash
cmake -S . -B build-bench -DCMAKE_BUILD_TYPE=Release
cmake --build build-bench --target marrow_benchmark
./build-bench/marrow_benchmark --parameter-deformers --skeletons 200 --bones 64
./build-bench/marrow_benchmark --parameter-deformers --skeletons 50 --artmeshes 100 --parameters 32
```

벤치마크 target 이름과 옵션은 구현 story에서 실제 harness에 맞춰 확정한다. 목표는 기존 200-skeleton 60fps 검증 흐름을 보존하면서 parameter evaluation cost를 별도 항목으로 노출하는 것이다.

## 8. 주요 위험과 대응

### 조합 폭발

- 위험: 3개 이상 파라미터가 같은 mesh/deformer를 동시에 제어하면 keyform 조합이 급증한다.
- 대응: 1차 범위는 1D/2D keyform만 지원한다. N차원은 명시적 blend graph와 validation warning이 있는 경우에만 허용한다.

### 성능

- 위험: 매 프레임 모든 parameter shape와 lattice를 재평가하면 기존 runtime benchmark를 훼손한다.
- 대응: dirty parameter bitset, affected deformer index, SoA delta buffer, unchanged mesh cache를 사용한다. benchmark는 `parameter_us` 또는 `deformer_us`를 별도 metric으로 보고한다.

### 기존 Bones/FFD와 호환성

- 위험: FFD timeline, linked mesh deform inheritance, weighted skinning, parameter shape가 같은 vertex에 중복 적용된다.
- 대응: 적용 순서를 문서와 테스트로 고정한다. 초기 순서는 `setup -> skin/linked mesh -> animation FFD -> parameter deformer/shape -> prepared render geometry`로 둔다.

### File-format 호환성

- 위험: 기존 `.mskl` version 1 fixture가 새 loader에서 깨질 수 있다.
- 대응: 새 root section은 optional empty default로 처리한다. version bump가 필요한 필드는 별도 migration story로 분리한다.

### Editor 복잡도

- 위험: 기존 timeline/mesh/constraint UI에 parameter UI를 섞으면 작업 모드가 불명확해진다.
- 대응: Parameter Modeling mode를 별도 mode로 두고, 기존 animation mode와 연결점은 expression trigger와 parameter timeline으로 제한한다.

### AI-agent Authoring Surface

- 위험: 에이전트가 많은 keyform/deformer를 한 번에 만들면 사용자가 변경 범위를 이해하기 어렵다.
- 대응: 모든 agent mutation은 affected parameter/deformer/slot count를 반환하고 undo group을 하나로 묶는다. `.marrow` 저장과 runtime export는 계속 명시적 op로만 수행한다.

### Proprietary Workflow 오해

- 위험: Live2D 호환을 표방하는 것으로 해석될 수 있다.
- 대응: 문서, UI, importer 명칭에서 "Live2D file/Core compatible" 표현을 쓰지 않는다. "Live2D-inspired parameter modeling" 또는 "Maroow parameter modeling"으로 표기한다.

## 9. Acceptance Criteria

- [ ] 기존 `assets/fixtures/player_idle.mskl`, `.mbin`, `.matl`, `.marrow` 검증 명령이 그대로 통과한다.
- [ ] `.mskl`에 parameter/deformer section이 없어도 loader가 빈 모델로 처리한다.
- [ ] `.marrow.parameter_model`에서 작성한 Parameters와 ParameterGroups가 `.mskl`/`.mbin`으로 export되고 `marrow_inspect --compare`가 동등성을 확인한다.
- [ ] 1D mouth-open shape와 2D face-angle WarpDeformer fixture가 runtime smoke에서 deterministic하게 평가된다.
- [ ] animation FFD와 parameter deform layer가 같은 mesh에 적용될 때 순서가 테스트로 고정된다.
- [ ] Expression preset은 여러 parameter target을 blend duration에 따라 적용하고 reset policy를 지킨다.
- [ ] Lip-sync mapping은 amplitude 입력으로 `mouth.open`을, phoneme class 입력으로 `mouth.form`을 갱신한다.
- [ ] Renderer preparation은 parameter-deformed mesh와 ArtPath-like stroke를 atlas-backed path와 atlas-free path 양쪽에서 처리한다.
- [ ] Performance benchmark가 parameter/deformer cost를 별도 metric으로 출력하고, 기존 200-skeleton release target을 회귀시키지 않는다.
- [ ] Editor에서 parameter slider, deformer keyform capture, expression preset, lip-sync mapping 변경이 undo/redo와 dirty state를 통과한다.
- [ ] Agent command surface가 parameter/deformer 변경을 undo group으로 묶고 affected targets summary를 반환한다.
- [ ] 문서와 UI가 Live2D proprietary format/Core 복제가 아니라 Maroow-native workflow inspiration임을 명확히 밝힌다.
