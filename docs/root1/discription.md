# Marrow 프로젝트 설계 결정사항

> molga-engine 기반 2D 스켈레탈 애니메이션 툴체인

---

## 1. 프로젝트 개요

| 항목 | 결정 |
|---|---|
| 프로젝트명 | **Marrow** |
| 포지셔닝 | Spine Pro 오픈소스 대체제 |
| 목적 | molga-engine 전용 2D 스켈레탈 애니메이션 툴체인 |
| 상업화 | 가능성 열어둠 (라이센스 설계 필요) |

---

## 2. 구성 요소

```
Marrow Editor  →  .mskl / .matl / .png  →  molga-engine Runtime
```

| 컴포넌트 | 역할 |
|---|---|
| **Marrow Editor** | 본 배치, 키프레임, 애니메이션 편집 (standalone 앱) |
| **marrow-runtime** | molga-engine 내장, 포맷 로드 및 재생 |

### 현재 제품 단계와 저작 범위

장기 비전은 본·슬롯·스킨·어태치먼트·메시까지 자체 저작하는 Spine Pro 대체제지만,
현재 제품 단계는 **임포트한 리그를 기반으로 애니메이션과 후처리를 저작하는 에디터**다.

- 베이스 `.mskl`은 본, 슬롯, 스킨, 어태치먼트와 메시 topology의 소유자다.
- `.marrow`는 베이스 런타임 자산 참조와 이름 기반 애니메이션·제약·웨이트 편집 overlay, editor-only metadata를 저장한다. 활성 파라미터 마일스톤의 optional `parameter_model`과 P1의 duration operation, viewport snap, curve/loop metadata, constraint lifecycle, inherit overlay, PSD provenance도 모두 additive 필드다. 알 수 없는 additive 필드는 load/save에서 보존한다.
- 사용자별 UI 기본값은 프로젝트 상태와 분리된 versioned `editor-settings.json`에 저장한다. MAR-156의 UI-free `PreferenceStore`는 curve 기본 preset과 raw Recent Projects 경로를 공유 저장하며 `.marrow` dirty/history/revision, runtime 포맷, C ABI, Agent/MCP 표면을 변경하지 않는다. MAR-170이 그 `default_curve` field의 소비자이며, `kEditorSettingsVersion`은 1 그대로이고 preference는 여전히 project dirty/history/revision, runtime 포맷, C ABI, Agent/MCP 표면 어디도 건드리지 않는다. 파일은 숫자가 아니라 6개 token 중 하나만 공급하므로 손상된 설정 파일이 keyframe에 control point를 주입할 경로는 존재하지 않는다.
- MAR-157의 UI-free `SelectionSet`은 imported Bone/Slot/Attachment/Constraint의 정확한 이름 identity와 단일 active item을 소유하는 transient editor 상태다. `ShellState`만 이를 소유하며 `ProjectData`, `PreviewState`, history snapshot, preference store, runtime 포맷, C ABI, Agent/MCP에는 저장하거나 노출하지 않는다. Attachment identity는 slot·skin·attachment 이름을, Constraint identity는 kind·name을 모두 포함한다.
- MAR-158은 hierarchy, inspector, viewport, timeline, constraint, weight-paint가 이 `SelectionSet`을 유일한 entity-selection 원본으로 직접 해석하게 한다. 성공한 project/runtime source 교체 뒤에만 exact typed identity를 새 runtime에 재해석하고 누락 항목만 stable order와 active fallback을 보존하며 prune한다. Preview skin/attachment refresh와 일반 authoring rebuild는 identity를 자동 교체하거나 prune하지 않으며 실패한 source 교체는 selection과 기존 runtime bundle을 보존한다.
- MAR-159는 `ShellState`의 transient exact-identity hierarchy anchor와 매 frame 실제 렌더된 Bone/Slot/Attachment row 순서를 사용해 plain replace, macOS Cmd/기타 Ctrl toggle, Shift visible-range replace, Cmd/Ctrl+Shift additive-range를 `SelectionSet`에 적용한다. Attachment row ID는 slot·skin·attachment 전체 scope를 사용한다. Anchor는 filter/collapse로 visible order에서 사라지거나 성공한 source 교체에서 identity가 누락될 때만 초기화하며 실패한 교체에서는 보존한다. 모든 selected row는 공통 배경을, active row만 primary rail/text를 표시하고 inspector/gizmo/timeline/constraint/weight consumer는 active item 하나만 편집한다. 이 상태는 project/history/runtime과 분리되며 group transform을 추가하지 않는다.
- MAR-160은 viewport point hit를 constraint target, Bone joint, Bone body, Slot centroid, topmost rendered Attachment triangle의 stable category/distance/order precedence로 `SelectionSet`에 replace/toggle한다. Empty-space drag는 visible runtime-active Bone joint만 skeleton order로 replace/additive box-select한다. Selection 변화는 hierarchy anchor와 timeline focus를 transient하게 동기화하며 inspector와 모든 authoring tool은 active item 하나만 편집하고 group transform은 추가하지 않는다.
- MAR-161은 Animation 모드의 runtime-active active Bone 하나에 58px screen-space rotation ring을 표시한다. Root와 `onlyTranslation`은 skeleton scale basis를, `normal` child는 gesture 시작 시 평가된 parent-world 2x2 basis를 freeze하여 absolute local rotation을 계산한다. `noRotationOrReflection`, `noScale`, `noScaleOrReflection`에서는 ring을 숨기고 unsupported-inherit hint를 표시한다. 연속 sample delta만 `(-180, 180]`로 unwrap하며 absolute degree는 정규화하지 않고 기존 transform-key transaction과 effective-track materialization 경로에 저장한다.
- MAR-162는 같은 active Bone에 rotation ring 바깥 74px screen-space local X/Y/uniform scale handle을 동시에 표시한다. Root와 `onlyTranslation`은 skeleton scale, `normal` child는 evaluated parent-world 2x2에 local rotation/shear를 합성하되 local scale을 제거한 positive axis basis를 gesture 시작 시 freeze한다. Nonzero 축은 pivot projection의 signed ratio로, exact-zero 축은 74px당 scale 1의 delta로 절대 scale을 계산한다. Uniform은 시작 X:Y ratio와 signs를 함께 보존하고 `(0,0)`에서는 숨긴다. 기존 scale effective-track materialization과 transform-key transaction을 사용하며 mixed selection에서도 active Bone 하나만 편집한다.
- MAR-163 checkpoint는 Animation 모드에서 현재 표시 중인 exact active mesh Attachment의 모든 최종-pose vertex를 screen-space handle로 노출하고 한 번의 press-drag 동안 vertex 하나를 편집하는 기반을 닫았다. 한 influence는 해당 bone world 2x2, 여러 influence는 `sum(weight * bone world 2x2)`를 gesture 시작 시 freeze하고 inverse로 world pointer delta를 animation-FFD-local delta로 바꾼다. 기존 deform timeline 전체와 curve를 geometry 크기의 full-vector project overlay로 materialize한 뒤 선택 pair만 바꾸며, linked `deform=true`는 immediate parent attachment를 target으로 한다. Parameter-composed 최종 위치는 handle에만 반영하고 key에는 선택 animation의 animation-FFD-only vector를 저장한다.
- MAR-164는 그 handle 아래에 `{slot, skin, displayed attachment, deform target, vertex count}` scope와 오름차순 unique index를 가진 shell-private persistent vertex sub-selection을 추가한다. Plain point는 replace 또는 selected-group drag/click-collapse를, macOS Cmd/기타 Ctrl은 toggle-only를 수행한다. 유효한 FFD overlay의 진짜 empty space는 Bone box보다 FFD box가 먼저 받아 forward/reverse inclusive plain replace/clear와 additive/no-op을 제공한다. Group drag는 모든 vertex의 frozen weighted inverse를 먼저 검증하고 하나의 common world delta를 gesture-start animation-only full vector의 선택 pair에만 atomic하게 적용한다. 하나라도 malformed, out-of-range, singular, non-finite이거나 refresh/downstream verification이 실패하면 materialization과 transaction 전체를 rollback한다. Linked `deform=true`는 displayed child scope를 유지하면서 immediate parent timeline 하나를 수정하고, `deform=false`는 child 자체를 target으로 한다. 이 sub-selection은 같은 exact displayed Attachment에서 playhead·animation·parameter preview·undo/redo·일반 rebuild 동안 유지되지만 mode·Weight Paint·skin·attachment·deform target 변경과 성공한 source/project adoption에서 clear된다. 실패한 adoption과 gesture rollback은 보존한다. `.marrow`, `SelectionSet`, runtime 포맷, C ABI, GPU, 56-operation Agent/MCP surface는 변경하지 않는다.
- MAR-165는 optional top-level `.marrow.snap`에 world-grid/local-angle/absolute-scale의 독립 enable과 10/15/0.1 step을 저장한다. 섹션이 없는 기존 프로젝트는 모두 OFF이고 불필요한 섹션을 materialize하지 않으며 unknown nested field를 보존한다. Translate는 적용 축의 absolute world target을 원점 0 기준으로 parent inverse 전에, rotate는 정규화하지 않은 raw multi-turn absolute local angle을, scale은 signed absolute component를 공용 scalar primitive로 snap한다. Uniform scale은 X가 nonzero면 X, 아니면 Y를 driver로 한 번 quantize하고 ratio/sign을 재적용하며 exact zero를 허용한다. Alt는 항상 우회하고 macOS Cmd/기타 Ctrl은 disabled domain을 gesture 동안 임시 활성화하며 두 modifier는 project/dirty/history에 저장되지 않는다. 표시 grid는 같은 world origin과 step의 정수배만 사용한다. 설정 toggle은 한 project-only undo, numeric drag는 한 coalesced undo이고 runtime export, `.mskl` v1, `.mbin` v2, C ABI v1, 56-operation Agent/MCP surface는 바뀌지 않는다.
- MAR-166은 같은 `.marrow.snap`에 기본 OFF인 `magnetic_vertex_enabled` boolean만 additive하게 추가하고 FFD group drag의 pressed vertex를 snap anchor로 사용한다. World grid는 MAR-165의 `world_grid_enabled`와 `world_grid_step`을 공유한다. Group drag가 활성화될 때 현재 draw order의 positive-alpha displayed mesh에서 선택된 active-scope vertex를 제외한 finite 후보와 world position을 snapshot하고, 매 update에는 그 snapshot을 현재 camera로 재투영해 canvas membership을 다시 판정한다. Inclusive 8 logical px Euclidean distance 뒤 `(slot name, optional resolved skin name, displayed attachment name, vertex index)` 순서로 tie-break한다. Magnetic vertex가 grid보다 우선하며 Alt는 둘 다 우회하고 macOS Cmd/기타 Ctrl은 disabled source를 active drag 동안만 임시 활성화한다. Snapped pressed-anchor delta 하나를 선택 group 전체에 적용하고 linked child는 immediate-parent deform target을 유지한다. Transient cyan/gold guide는 raw pressed-anchor target에서 snap target을 가리키며 설정 toggle만 project-only history에 남는다. `.mskl` v1, `.mbin` v2, C ABI v1, `SelectionSet`, GPU와 56-operation Agent/MCP surface는 바뀌지 않는다.
- MAR-167은 2026-08-20에 기존 Timeline window의 읽기 전용 Graph mode를 완료했다. Effective runtime animation에서 한 focused Bone Rotate/Translate/Scale/Shear 또는 Slot light RGBA parent track만 scalar series로 투영하고 component별 visibility, Fit, wheel time zoom, Shift-wheel value zoom과 middle-drag pan을 제공한다. Graph와 dopesheet는 같은 `TimelineKeyRef`, parent selection, `active_key`, focused track과 playhead를 공유하며 component 선택과 view/cache는 shell-private transient 상태다. Linear/Stepped/Cubic은 실제 parent outgoing easing을 서로 다른 geometry marker로 표시하고 X/Y 또는 RGBA component가 easing 하나를 공유한다는 notice를 노출한다. FFD와 Inherit/Attachment/Draw Order/Event discrete lane은 제외한다. Graph point의 time/value drag는 MAR-168에서 열렸고 `.marrow`, runtime export, history/dirty/revision, `.mskl` v1, `.mbin` v2, C ABI v1과 56-operation Agent/MCP surface는 바뀌지 않는다.
- MAR-168은 2026-08-30에 Graph tab의 point drag 편집을 완료했다. Point를 누르면 transaction 없이 drag candidate만 무장하고, 4.0 logical pixel dead zone을 벗어나는 순간 dominant-axis 비교로 축을 한 번 고정한 뒤 gesture가 끝날 때까지 유지한다. 세로 drag는 focused track에 속한 모든 선택 key의 눌린 scalar component 하나만 공용 `offset_keyframe_scalars()`로 옮기고, 가로 drag는 dopesheet의 `begin/apply/finish_timeline_retime_gesture()`를 그대로 재사용해 key 전체 시간과 모든 component를 함께 옮긴다. Frame snap, 미선택 이웃과의 1 ms 충돌 clamp, stable identity 재구성, explicit duration auto-grow는 모두 dopesheet와 같은 primitive에서 나오고 Alt는 현재 drag의 frame snap만 우회한다. Slot Color R/G/B/A는 group-wide로 `[0, 1]`에 clamp되고 Angle/Translate/Scale/Shear는 clamp하지 않아 signed scale과 정확한 0이 유지된다. 하나의 drag는 하나의 `EditTransaction`이며 live preview·Escape/focus 상실/tab 전환 취소·undo/redo·안정적 selection을 보장하고, 제자리로 돌아온 drag는 history를 만들지 않는다. Drag 중에는 zoom/pan/Fit/`needs_fit`가 모두 억제돼 view transform이 고정된다. Bezier/easing 편집은 MAR-169에서 열렸고 `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1과 당시 56-operation Agent/MCP surface는 바뀌지 않았다.
- MAR-169는 2026-08-30에 Graph tab의 공용 Bezier handle 편집을 완료했다. Active key의 outgoing segment 하나에만, 그리고 active component 하나에만 두 개의 handle을 그린다. 하나의 curve에는 항상 하나의 handle 쌍만 존재하므로 component마다 별도 curve가 있다는 오해가 생기지 않는다. Handle을 누르면 MAR-168과 같은 drag candidate가 transaction 없이 무장하고, 공용 4.0 logical pixel dead zone을 벗어나면 `EditTransaction` 하나가 열린다. Handle drag는 축을 고정하지 않는 free 2-D다. `cx`와 `cy`는 하나의 primitive가 쓰는 한 curve의 두 parameter이므로 axis lock은 curve를 저작 불가능하게 만들 뿐이다. Press 시점에 두 anchor를 `SegmentFrame`으로 동결해 pixel↔control point 매핑을 gesture 내내 고정한다. Pure mapping은 `cx`를 `[0, 1]`로 clamp해 경계에서 멈추되 drag를 계속하고, 새 additive `set_keyframe_interpolation()` primitive는 범위 밖·비유한·float32 범위 밖 값을 독립적으로 원자 거부한다. `NaN < 0`과 `NaN > 1`이 모두 false이므로 유한성 검사를 범위 검사보다 먼저 수행한다. `cy`의 유한 overshoot는 보존한다. Linear/Stepped handle을 잡으면 같은 transaction·같은 undo entry 안에서 `[1/3, 1/3, 2/3, 2/3]` seed로 Cubic이 되며, 이 seed는 Linear와 정확히 같은 evaluated curve를 갖는 유일한 등간격 cubic이다. 두 anchor 값이 1 logical pixel 미만으로 붙은 flat segment는 100 logical pixel을 `cy = 1`로 정의한 양수 fallback span으로 편집하고, zero-duration segment는 handle을 노출하지 않는다. Primitive는 component 인자를 갖지 않으므로 Translate Y를 보며 편집해도 Translate X segment가 동일하게 바뀌고 project 전체에서 정확히 하나의 `interpolation` field만 달라진다. 같은 mutation을 57번째 agent operation `timeline.set_interpolation`과 대응 MCP tool로 노출하며 Transform·Slot Color·Deform key를 지원하고 Draw Order·Event·Slot Attachment key는 거부한다. `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1은 바뀌지 않고 기존 `curve` field만 기록한다.
- MAR-170은 2026-08-30에 고정 curve preset과 기억되는 기본 curve를 완료했다. Linear, Stepped, Ease `[0.25, 0.1, 0.25, 1]`, Ease-In `[0.42, 0, 1, 1]`, Ease-Out `[0, 0, 0.58, 1]`, Ease-In-Out `[0.42, 0, 0.58, 1]`은 CSS Easing Level 1 상수이며 `include/marrow/editor/authoring.hpp`의 `constexpr kCurvePresets` table 한 곳에만 존재한다. `static_assert`가 enum 순서와 모든 `cx`의 `[0, 1]` 불변식을 compile time에 증명하므로 형식을 위반하는 preset은 컴파일되지 않는다. Graph tab과 Dopesheet tab이 같은 row를 그리고 같은 `TimelineEditorState::selected_keys`를 읽으므로 selection semantics는 하나뿐이다. Preset 적용은 호환 선택 key 전체를 MAR-169의 `set_keyframe_interpolation()` 한 번으로 쓰는 하나의 `EditTransaction`이며, key 개수와 무관하게 preview 한 번과 history entry 하나다. Easing field가 없는 Draw Order·Event·Slot Attachment key는 GUI가 건너뛰고 개수를 보고하지만 agent는 원자적으로 거부한다. Dopesheet box selection은 느슨하게 만들어지고 script selector list는 그렇지 않기 때문이다. 중복 ref는 selector 단계에서 합쳐 primitive의 중복 거부에 걸리지 않게 한다. 모든 preset이 `cx2 >= cx1`과 `0 <= cy1 <= cy2 <= 1`을 만족하므로 preset은 overshoot할 수 없고, overshoot는 MAR-169 수동 handle drag로만 도달한다. 현재 preset 표시는 저장된 `float32` 네 값의 순수 함수이며 epsilon 없이 bit-exact로 비교한다. `double` literal과 비교하면 방금 적용한 preset이 즉시 `Custom`으로 읽히므로 table 값을 narrowing한 뒤 비교한다. 저장된 flag가 없으므로 undo/redo/reload에 무효화 대상이 없고 handle drag 직후 `Custom Bezier`가 된다. 기억되는 기본 curve는 MAR-156의 `editor-settings.json` `default_curve`에 저장한다. Key가 없는 시각에 새 key를 저작하는 shell gesture는 모두 이 값을 seed로 받는다. "Add Key At Playhead", viewport FFD vertex drag, viewport translate/rotate/scale gizmo drag, Inspector transform field 네 경로가 그것이다. 공용 `upsert_transform_keyframe()`·`upsert_deform_keyframe()` primitive는 preference를 직접 읽지 않고 Linear를 기본값으로 갖는 seed 인자를 받는다. 덕분에 agent 출력은 어느 machine에서도 재현 가능하고 `marrow_editor`는 MAR-156이 남긴 만큼 preference로부터 격리된 채로 남는다. Seed는 새로 삽입되는 key만 초기화하므로 이미 있는 key 위에 떨어진 gesture는 그 key의 curve를 건드리지 않는다. Paste는 복사된 curve를 유지하고, agent가 만드는 key는 재현성을 위해 Linear를 유지하며, MAR-169의 `[1/3, 1/3, 2/3, 2/3]` 변환 seed도 그대로다. Preset 적용은 기본값을 바꾸지 않으며 오직 명시적 `Default:` 컨트롤만 바꾼다. 기본값 변경은 transaction을 열지 않아 `serialize_project()`·dirty·history·revision이 모두 그대로다. 없거나 손상되었거나 지원하지 않는 version의 설정 파일은 Linear로 fallback하고 load가 파일을 고쳐 쓰지 않는다. Agent/MCP 표면은 정확히 57 operation 그대로이고 `timeline.set_interpolation`의 문자열 인자만 `ease`, `ease_in`, `ease_out`, `ease_in_out` 4개를 추가로 받는다. Token은 snake_case 하나뿐이며 `ease-in` 같은 별칭은 거부한다. `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1은 모두 그대로이고 기존 `curve`와 `default_curve` field만 기록한다. Project-local automatic curve handle과 driver metadata는 MAR-171 경계다.
- MAR-171은 2026-08-30에 project-local automatic curve handle을 완료했다. Transform과 Slot Color keyframe이 `curve_mode`(`manual` | `auto`)와 `curve_driver`(`angle`|`x`|`y`|`r`|`g`|`b`|`a`) 두 optional field를 `.marrow`에만 추가로 기록한다. 기존 project에는 두 field가 모두 없으므로 모든 기존 project는 오늘의 manual 동작을 byte 단위로 그대로 유지한다. Manual key는 두 field를 아예 직렬화하지 않으므로 disk 상 "manual" 표현은 하나뿐이다. Auto key의 easing은 순수 monotone Fritsch–Carlson 보간으로 기존 공용 `curve` field에 즉시 해석해 써 넣는다. 계산은 `ProjectData`도 session도 ImGui도 모르는 새 translation unit `src/editor/curve_auto.cpp` 한 곳에만 존재한다. Cubic Hermite를 단위 정사각형으로 정규화하면 `cx1 = 1/3`, `cx2 = 2/3`이 clamp가 아니라 항등식으로 나오므로 `X(t) = t`가 정확히 성립하고 runtime의 역함수는 항등함수다. `cx ∈ [0, 1]` 형식 불변식은 float32 narrowing 전후로 무조건 성립한다. Fritsch–Carlson의 `a² + b² ≤ 9`가 `a, b ∈ [0, 3]`을 주므로 `cy1 = a/3`과 `cy2 = 1 − b/3`도 `[0, 1]` 안에 있고, 따라서 automatic curve는 절대 overshoot하지 않는다. Overshoot는 MAR-169 수동 handle drag로만 도달한다. 평탄 구간은 `0/0`이라 방향에 따라 극한이 달라지므로 아무것도 성형하지 않는 중립값 `[1/3, 1/3, 2/3, 2/3]`을 규약으로 택했다. Curve는 driver가 말하지 않는 다른 component와 공유되기 때문이다. 길이 0 구간은 전체 track을 원자적으로 거부한다. 마지막 key는 outgoing segment가 없으므로 easing을 쓰지 않고 mode만 기록한다. 재계산은 이웃 시간·이웃 값·driver·retime·삽입·삭제·paste·duration을 바꾼 바로 그 transaction 안에서 animation 전체를 대상으로 일어나므로 편집 하나가 history entry 하나다. 절대 easing을 쓰는 모든 경로는 key를 manual로 강등한다. 이 규칙은 `set_keyframe_interpolation()` 내부에 있어서 MAR-169 handle drag, MAR-170 preset, 숫자 inspector, agent가 모두 자동으로 상속하며 어느 호출자도 잊을 수 없다. Auto key 위의 drag는 control point가 출발점으로 정확히 되돌아와도 commit한다. Key가 이웃을 추종하기를 멈춘 것 자체가 저작이기 때문이다. Manual key의 왕복 drag는 MAR-169의 net-state 규칙대로 여전히 cancel이다. Deform은 정규 scalar가 없으므로 auto mode에서 제외했고 `DeformKeyframeEdit`에 field를 추가하지 않아 compile time에 강제된다. Load·save·export는 절대 해석하지 않으므로 손으로 고친 stale mode/curve 쌍은 합법 data이며 저장된 숫자가 모든 reader에 대해 권위를 갖는다. `timeline.set_curve_mode`가 명시적 재조정 명령이다. Agent/MCP 표면은 `timeline.set_curve_mode` 하나가 늘어 정확히 58 operation이 됐다. `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1은 모두 그대로이고 두 field는 runtime file에 절대 들어가지 않는다. Lane 단위 metadata와 loop boundary key 동기화는 MAR-172 경계다.
- MAR-172는 2026-08-30에 loop boundary key 동기화를 완료했다. Transform·Slot Color·Deform lane 하나가 `loop_sync` boolean 하나로 opt-in한다. 이 flag는 lane 자신의 JSON 값이 아니라 `timeline_edits`와 같은 모양의 optional top-level `loop_sync` tree에 투영된다. Lane의 값이 bare array라 member를 담을 object가 없고, object로 승격하면 opt-in한 project를 예전 build가 아예 못 읽기 때문이다. Top-level tree는 `preserved_root`로 온전히 보존되므로 예전 build는 timeline을 정상적으로 읽고 loop도 올바르게 재생하며, 다만 유지만 멈춘다. Opt-in한 lane은 항상 `float32(explicit duration)` 위에 관리 key를 정확히 하나 갖고, 그 key의 모든 값 component와 easing 기록(`curve`, 그리고 MAR-171의 `curve_mode`·`curve_driver`)은 같은 lane의 time 0 key를 bit 단위로 복사한 값이다. 동기화는 한 방향이며 첫 key가 언제나 이긴다. 전제 조건은 정확히 explicit duration과 time 0 key 두 가지다. Inferred duration이면 boundary key가 duration을 정의하고 duration이 boundary key를 정의하는 고정점 없는 재귀가 되고, time 0 key가 없으면 boundary가 시간이 임의인 key를 따라가게 되기 때문이다. 관리 key의 정체성은 저장하지 않고 유도한다. Opt-in한 lane의 boundary key는 곧 그 lane의 마지막 key이므로 copy/paste가 거짓 marker를 옮길 수 없고 adopt는 코드가 아예 필요 없다. 동기화는 `EditorSession::refresh_runtime()`과 `commit()`에서 `auto_extend_explicit_animation_durations()` 바로 뒤, 즉 모든 duration 변경 이후임이 증명되는 유일한 지점에서 caller가 이미 연 transaction 안에 실행된다. Session이 controller 코드보다 뒤에 duration을 키우므로 controller 수준 배선에는 실제 staleness 구멍이 있다. 두 phase 사이에 MAR-171 resolver가 들어가고, resolver는 lane의 마지막 key를 절대 쓰지 않으므로 반복 없이 한 번씩으로 정확히 수렴한다. Phase 2는 `set_keyframe_interpolation()`을 절대 호출하지 않는다. 그 함수는 쓰는 key를 manual로 강등하므로 매 transaction마다 mirror한 auto 의도를 조용히 파괴할 것이기 때문이다. `set_animation_duration()`의 inferred floor와 auto-extend의 overlay scan은 둘 다 관리 boundary key를 제외한다. 제외하지 않으면 opt-in한 clip을 영영 줄일 수 없고 줄인 duration이 한 줄 뒤에 되돌아온다. 두 제외 모두 opt-in한 lane이 없으면 bit 단위 no-op이다. Retime은 opt-in한 lane의 양 끝을 고정하고, GUI는 관리 boundary key를 건너뛰며 보고하고 Agent는 값·easing·preset·curve mode 쓰기와 제거 모두를 세 family에서 원자적으로 거부한다. 세 표면이 같은 유도 규칙을 공유하므로 어느 키가 파생인지에 대해 서로 어긋날 수 없다. 계약은 lane의 마지막 key를 그 key가 계약의 절반을 만족할 때만 소유한다. 이미 boundary 위에 있거나, 이전 동기화가 쓴 key 0의 bit 단위 mirror로 남아 있는 경우다. 둘 다 아니면 저작된 data이므로 promote하지 않고 그 옆에 boundary를 새로 만든다. Disable은 어떤 전제도 평가하지 않고 항상 성공하며 boundary key를 일반 key로 그대로 남긴다. Flag 하나 때문에 keyframe data를 잃지 않기 위해서다. Draw Order·Event·Slot Attachment는 piecewise constant이고 boundary event key는 loop마다 두 번 발화하므로 제외했으며 lane struct에 field를 추가하지 않아 compile time에 강제된다. Agent/MCP 표면은 `timeline.set_loop_sync` 하나가 늘어 정확히 59 operation이 됐고, 이 operation의 selector는 key가 아니라 lane이라 `time`을 갖지 않는다. `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1은 모두 그대로이고 flag는 runtime file에 절대 들어가지 않는다. 관리 boundary key는 일반 keyframe으로 들어가며 그것이 이 story의 전부다. MAR-172는 ImGui code를 전혀 추가하지 않는다. Selected-key time scaling은 MAR-173 경계다.
- MAR-173은 2026-08-30에 원자적 key 시간 scaling을 완료했다. Marrow의 다른 모든 timing 편집은 translation이다. `retime_keyframes()`는 하나의 공유 delta를 더한다. Scaling은 key마다 pivot으로부터의 거리에 비례하는 서로 다른 delta를 적용하므로 retime 확장이 아니라 새 primitive `scale_keyframe_times()`다. Pivot은 자유 parameter가 아니다. 언제나 선택 범위 자신의 반대 edge이며, API는 시간이 아니라 어느 edge가 고정되는지를 지시하는 `TimelineScalePivot::RangeStart|RangeEnd` enum을 받는다. Caller가 criterion을 우회할 수 없게 하려는 것이다. 선택된 모든 key는 유한하고 순수하게 양수인 하나의 비율로 `t' = pivot + (t - pivot) * s`에 놓인다. Pivot key는 `p + 0.0 * s == p`라는 IEEE-754 항등식으로 tolerance 없이 bit 단위 불변이며, 이것이 live drag가 매 frame 비율을 곱셈으로 합성해도 어긋나지 않는 근거다. Retime은 clamp하지만 scaling은 **거부**한다. Clamp한 translation은 여전히 translation이지만 clamp한 scale은 사용자가 고르지도 보지도 못한 비율을 만들거나 key마다 다른 비율로 움직여 아예 scale이 아니게 되기 때문이다. 영향받는 timeline의 투영된 인접 쌍이 family 최소 간격보다 가까워지면 — 선택 key가 선택되지 않은 이웃을 침범하는 경우를 포함해 — 호출 전체를 거부하고 두 시간을 이름으로 알린다. 기준은 평평한 1 ms가 아니라 `min(spacing, original_gap)`다. 만족하던 간격은 계속 만족해야 하고 이미 더 좁던 간격은 더 좁아지면 안 된다는 뜻이며, import되었거나 MAR-172가 인접 채택한 sub-millisecond 간격을 가진 timeline도 계속 편집할 수 있다. 같은 논리로 MAR-172의 loop pin은 여기서 거부가 된다. 한 key만 고정하고 나머지를 scale하면 어떤 `s`로도 표현되지 않는 모양이 나오기 때문이다. Pin 술어는 MAR-172의 `include_loop_boundary_retime_pins()`에서 추출해 공유하므로 retime과 scale이 어느 key가 고정인지 어긋날 수 없다. Event tie 보존은 방어 loop가 아니라 정리다. 같은 시간의 두 key는 같은 식에 bit 단위로 같은 입력을 넣으므로 결과도 bit 단위로 같다. 반대로 tie의 일부만 지명한 선택은 조용히 넓히지 않고 시간을 이름으로 거부한다. 원자성은 세 층위에서 각각 주장한다. 한 primitive 호출은 candidate 복사 후 단일 move다. 한 gesture **frame**은 거부되어도 gesture를 죽이지 않고 마지막으로 수락된 상태를 유지한다. Scale handle을 안쪽으로 끌었다 다시 밖으로 빼는 것은 지극히 평범한 조작이고 거기서 drag를 죽이면 편집을 잃기 때문이다. 한 gesture는 한 history entry다. Frame snap은 끌리는 edge 하나에만, 기존 공용 `snap_delta_to_frames()`를 통해 적용되어 비율 자체를 다시 만든다. 모든 key를 양자화하면 key 사이 비율이 바뀌어 결과가 어떤 단일 `s`로도 표현되지 않게 되기 때문이다. Agent의 `snap`은 script가 계산한 비율이 이미 정확하므로 기본값이 `false`이고, retime의 pointer 성격 delta는 기본값 `true`를 유지한다. 전곡 scale은 정규화된 tangent가 Δt의 **비율**에만 의존하므로 MAR-171 자동 curve를 bit 단위로 그대로 두고, 부분 scale은 바꾼다. Duration은 session seam의 `auto_extend_explicit_animation_durations()`로 같은 transaction 안에서 자라기만 하고 절대 줄지 않는다. Agent/MCP 표면은 `timeline.scale_key_times` 하나가 늘어 정확히 60 operation이 됐다. `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1과 `.marrow` schema는 모두 그대로다. 이 operation은 `keyframe.time`만 쓴다. Preview 재생 속도는 MAR-174 경계다.
- MAR-174는 2026-08-30에 transient preview 재생 속도를 완료했다. Timeline transport에 유한하고 순수하게 양수인 배율 하나를 얹는 것이 전부이고, 이야기의 실체는 그 곱셈이 아니라 **아무것도 관측하지 못한다는 증명**이다. 값은 shell 전용 `ShellState::preview_speed` 하나이며 `[0.05, 8.0]` 연속 구간에 기본값 `1.0`, preset `{0.25, 0.5, 1, 2}`를 갖는다. AC가 범위와 preset을 함께 말하므로 preset은 구간 자체가 아니라 구간 위의 단축키다. 곱셈은 `advance_timeline_playback()` **한 곳**에서만 일어난다. 그곳이 preview 시간이 나아가는 유일한 경로이고, `EditorSession::advance()`가 하나의 delta를 표시 시간·sampling된 pose·crossfade·event dispatch로 그대로 넘기므로 네 가지가 구현 네 개의 합의가 아니라 구조적으로 함께 scaling된다. 속도는 방향이 아니라 **크기**다. `PreviewImpl::advance()`가 `delta_seconds <= 0.0`에서 즉시 반환하고 event dispatcher가 `current_time <= previous_time`에서 반환하므로 음수 속도는 조용한 no-op인 동시에 그 frame의 event를 전부 버린다. 방향은 계속 `preview_reverse`의 몫이며, runtime은 이미 reverse를 단조 증가하는 track time 위의 sampling 변환으로 모델링하므로 크기와 방향은 구조적으로 합성된다. `0.0`은 유한하고 하한 아래라 `0.05`로 clamp되며 **정지가 아니다**. 정지는 이미 `timeline_playing`이 있고, 수용 범위 밖 값에 별도 의미를 주면 그 값을 다시 범위 안으로 끌어들이는 셈이 된다. 유한한 범위 밖 값은 clamp하고 비유한 값은 field를 bit 단위로 두고 거부한다. Clamp는 `preview_queue_delay`·`preview_custom_mix_duration`이 이미 쓰는 transient preview scalar의 관례이고, `NaN`에는 clamp 대상이 없어 조용히 대체하면 caller의 bug를 숨기게 되기 때문이다. `preview_playback_speed()` accessor가 사용 시점에서 한 번 더 clamp하므로 직접 오염된 field도 `EditorSession::advance()`에 도달할 수 없다. Loop 경계는 `std::fmod` 단일 정확 modulo라 8배속의 여러 주기 step도 한 번에 올바른 위상에 놓이고 주기별 반올림이 누적되지 않는다. `fmod`가 `[0, duration)`을 주므로 MAR-172의 관리 boundary key는 **어떤 속도에서도** looped 재생 중 sampling되지 않고, 그 key가 time 0 key의 bit 단위 복사라는 계약이 wrap을 값 연속으로 만든다. Loop가 아니면 정확히 `duration`에서 멈춘다. 속도는 끝에 더 빨리 닿을 뿐 넘어설 수 없다. Scrubbing은 비율이 아니라 절대 위치라 영향받지 않고, 일시정지는 그대로 `timeline_playing`이다. Field는 `marrow::editor::PreviewState`에 두지 **않는다**. 그 struct가 이미 `reverse`·`loop`·`playing`을 갖고 있어 자연스러워 보이지만, `assign_history_snapshot()`이 undo마다 `PreviewState`를 되쓰고 `history_snapshots_equal()`은 그것을 비교하지 않으므로 속도가 거기 있으면 Ctrl+Z에서 변경 보고 없이 조용히 튄다. `reload_project()`의 단일 default `kDefaultPreviewSpeed`로만 초기화하고 animation 전환이나 undo에서는 초기화하지 않는다. 직렬화·dirty·history·export·`editor-settings.json` 어디에도 들어가지 않는다. AC 4가 project를 열 때마다 초기화하므로 저장된 값은 관측될 기회 자체가 없다. Agent operation과 MCP tool은 늘지 않아 registry는 60 그대로다. 문서에 아무 효과도 없는 lever를 agent에 주는 것은 UI-free primitive 규율이 존재하는 이유와 정반대이기 때문이다. `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1과 `.marrow` schema는 모두 그대로다. 수동 weight 저작 통합은 MAR-175 경계다.
- MAR-175는 2026-08-30에 수동 weight 저작 통합을 완료했다. 다섯 개 경로가 weight를 쓰고 있었고 그중 넷이 무엇이 유효한 weight 목록인지에 대해 서로 달랐다. 정규화, 중복 제거, 정렬, influence 상한, 0 처리, non-finite 처리, 그리고 bind offset이 어느 pose로 표현되는지가 모두 어긋나 있었다. 그 불일치 중 둘은 Marrow 자신이 저장을 거부하는 프로젝트를 만들 수 있었다. `set_vertex_weights`는 zero weight와 중복 bone을 받아 `ok`를 돌려준 뒤 `save`에서 *"must preserve positive weights"* / *"must not repeat the same bone"*으로 실패했다. 수락해 놓고 저장할 수 없게 만드는 실패다. 이제 규칙은 `marrow_editor` 안의 UI 없는 `mesh_weight_model` 하나가 소유한다. 이 파일이 새 translation unit인 이유는 `shell_weight_paint.cpp`가 실행 파일에 있어 `src/tests/`가 링크할 수 없기 때문이다. canonicalization은 여덟 단계를 이 순서로 적용한다. non-finite 거부, 알 수 없거나 빈 bone 이름 거부, 중복 bone 병합(weight 합, bind는 weight 가중 평균), `1e-6` 이하 제거, weight 내림차순 + skeleton index 오름차순의 **전순서** 정렬, top-4 제한, 빈 결과 거부, 정규화다. Non-finite를 맨 앞에서 거부하는 것이 핵심이다. `NaN <= 1e-6`은 거짓이라 NaN이 기존 두 정규화 함수의 두 guard를 모두 통과한 뒤 자기 자신으로 나뉘어 vertex의 모든 influence를 NaN으로 오염시켰다. 정규화는 합이 exactly `1.0`인지가 아니라 `4 * DBL_EPSILON` 이내인지로 판정하고, 정규화 **후에 한 번 더 정렬**한다. 이 둘이 canonicalization을 자기 자신의 bit 단위 고정점으로 만든다. 정확한 비교는 동작하지 않는다. 프로젝트 자신의 fixture가 반례다. `0.6/0.8 + 0.2/0.8`은 `0.9999999999999999`이라 정확한 판정은 영원히 발화하지 않고 매 호출이 weight를 다시 흔든다. 나눗셈은 1 ULP 차이였던 두 weight를 같은 값으로 반올림할 수 있고 첫 정렬은 나누기 전 값으로 순서를 정했으므로, 두 번째 정렬이 없으면 결과가 자기 comparator를 만족하지 않는다. Paint가 vertex에 없던 bone을 추가할 때 bind offset을 **현재 preview pose**의 역변환으로 계산하던 것도 고쳤다. Playhead가 어디 있느냐에 따라 같은 stroke이 다른 geometry를 `.marrow`에 썼다는 뜻이다. 이제 setup pose로 계산하며, 그 setup transform은 sample마다가 아니라 stroke마다 한 번 해결한다. 기존 smoke는 vertex가 이미 가진 bone만 칠해서 이 분기를 한 번도 밟지 않았고 bind offset을 전혀 검증하지 않았다. 그래서 결함이 보이지 않았다. 그 smoke를 고쳐 attack@0.2에 놓인 채 새 bone을 칠하고 setup pose 값을 요구하게 했다. 고치기 전에는 setup이 요구하는 `(-34, -90)` 대신 `(-94.94, -15.56)`을 썼다. 같은 자리에서 `inverse_transform_point_safe`가 `runtime::AttachmentVertex`를 반환하던 것도 드러났다. 그 구조체의 멤버는 **float**이라 double bind offset이 저장 직전에 조용히 float32로 반올림되고 있었다. `.marrow`는 double을 저장하는데도 그랬다. 이제 double을 반환하며, 그것이 rebind가 자기 문서가 주장하는 일관성 계약을 실제로 만족하게 만든 변경이다. AC3의 네 표면 — Replace brush, active-vertex 수치 influence 표, selected-scope Normalize, setup-pose Rebind — 은 모두 그 primitive 위에 있다. Replace는 rate가 아니라 target이다. Stamp 값이 active bone이 **끝나야 할** weight이고 나머지는 남은 몫으로 scale된다. Raw weight로 대입하고 정규화에 맡기면 strength 1.0에서 `1/(1+others)`가 나와 Replace brush가 존재하는 이유인 한 번에 1.0을 칠하는 동작이 불가능하다. Brush는 sample마다 `ProjectData`를 복사할 수 없으므로 순수 계층만 호출하고 기존 단일 attachment rollback을 유지한다. 수치 편집·Normalize·Rebind는 진짜 transaction을 쓰고 gesture당 history 하나를 남긴다. Bind `x`/`y`는 읽기 전용이다. 그것은 geometry이고, 한 bone의 frame에서만 손으로 고친 offset은 정확히 Rebind가 고치려는 불일치다. Agent/MCP 표면은 `mesh.rebind_weights` 하나가 늘어 정확히 61 operation이 됐고 `normalize_weights`는 optional `vertices` scope를 얻었다. 관측 가능한 호환성 변경은 둘이다. `set_vertex_weights`의 명시적 `"normalize": false`는 이제 거부된다. canonicalization이 무조건이라 그 flag에는 구현 가능한 의미가 없다. 존중하면 저장 불가 결함이 되살아나고, 조용히 무시하면 수행하지 않은 요청에 성공을 돌려주게 된다. `normalize_weights`는 rescale만 하던 것에서 제거·병합·정렬·상한까지 하게 됐다. 좁은 버전은 저장할 수 없는 결과를 남길 수 있어 "정규화됨"이 "저장 가능"을 뜻하지 않았기 때문이다. 이름, 인자, `no_change` disposition과 메시지는 그대로다. Rebind는 결정적이지만 bit 단위로 idempotent하지는 **않다**. `BoneWorldTransform`은 float32 여섯 개인데 bind offset은 double이라 `S(S^-1(V))`가 bit 단위로 `V`를 재현하지 않는다. 테스트는 반복 실행의 bit 동일성을 주장하고 두 번째 적용은 `1e-9` 안정성만 주장하며 그 구분을 출력한다. 거짓인 성질을 주장하지 않기 위해서다. `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1과 `.marrow` schema는 모두 그대로이고 `ProjectData`에 멤버를 더하지 않았다. canonical 규칙은 `.marrow` schema가 이미 받아들이는 것보다 좁기만 하므로 migration도 version bump도 없다. Mesh topology와 자동 weight 생성은 MAR-176 경계다.
- MAR-176은 2026-08-30에 결정적 자동 weight 생성을 완료했다. Marrow에서 weighted mesh의 influence는 사람만 줄 수 있었다. 브러시, 수치 influence 표, 스크립트 `set_vertex_weights` 셋뿐이라 모든 vertex를 손으로 칠해야 했다. MAR-176은 "이 mesh를 이 bone들에 바인딩하라"를 추가하되 제목이 말하는 하나의 제약 아래 둔다. 결과가 **결정적**이어야 한다. 알고리즘은 setup pose에서 각 후보 bone의 **segment**까지의 역제곱 거리다. Marrow에는 bone length가 없다. `runtime::BoneData`는 `{name, parent_index, setup_pose, inherit}`뿐이고 `.mskl` schema에도 `length`가 없다. 그래서 segment는 계층에서 나와야 하고, Marrow에는 이미 그 정의가 정확히 하나 있다. viewport가 bone을 **부모의 world origin에서 자기 world origin까지**의 선으로 그리고 (`shell_viewport.cpp:1659-1675`) 같은 segment로 hit-test한다(`:2127-2140`). 사용자가 체크박스를 켜는 대상이 바로 그 geometry이므로 그것을 채택했다. 부모가 없는 bone은 자기 origin의 **점**으로 퇴화하는데, viewport도 부모 없는 bone의 몸통을 그리지 않는다. vertex의 setup-world 위치는 `geometry.vertices`에서 오지 않는다. weighted mesh에서 그 배열은 장식이다. `evaluate_mesh_attachment_pose`의 weighted 분기는 그것을 **개수 세는 데만** 쓰고(`skeleton_skin.cpp:529`) 두 배열은 서로 검증되지 않은 채 따로 파싱된다. 유일하게 권위 있는 유도는 rebind의 1단계이고, 그래서 그것을 **그대로** 추출해 generate와 rebind가 공유하게 했다. 재유도하지 않았다. 두 구현이 같은 공식을 갖는 것이 MAR-175가 story 하나를 들여 없앤 D7 모양의 결함이기 때문이다. 이 story의 가장 중요한 비자명한 결정은 generator가 top-4 raw weight를 canonicalizer에 넘기기 **전에** 정규화한다는 것이다. canonicalizer의 `1e-6` 이하 제거는 절대값이고 자기 정규화보다 **먼저** 실행된다. 그래서 raw `1/d²`를 넘기면 그 gate가 "1000 world unit보다 멀면 버림"이 된다. 측정값으로 fixture의 10배 scale에서 정당한 influence가 이미 삭제되고, 100배에서는 모든 influence가 삭제되어 vertex 자체가 거부된다. 저장소가 이미 테스트하는 `tank` rig이 fixture의 10배다(max `|world|` 2395 대 230). 가정이 아니라 피한 실제 버그다. 역으로 증명했다. 정규화를 지우면 100배 case가 canonicalizer 자신의 *"must keep at least one positive influence"*로 실패한다. 결정성은 이렇게 만든다. 경로 위의 모든 컨테이너가 skeleton bone index로 색인되거나 정렬된 `std::vector`다. hash 순서 컨테이너도, 병렬성도, `sqrt`/`hypot`/`pow`/`fma`도 없다. 정렬은 `(d² 오름차순, bone index 오름차순)`의 **전순서**이고 bone index가 유일하므로 `std::sort`의 불안정성은 무관하다. 합은 정렬된 top-K를 좌에서 우로 더한다. 수락 값은 fixture vertex 2의 정확한 `0.5 / 0.5`다. 이 vertex는 `spine` segment 끝을 지나 있고(t=2.6→1) `arm_l` segment 시작 뒤에 있어(t=-1.12→0), 두 clamped 최근접점이 bit 단위로 같게 나온다. 그 이유는 정확히 적어둘 필요가 있다. 두 식은 같지 않기 때문이다. `arm_l` 쪽은 `start + ab*0.0`이라 자기 segment 시작점인 `spine`의 world origin이 origin 값과 무관하게 정확히 나오지만, `spine` 쪽은 `root`의 origin에서 `start + ab*1.0`이라 **`root`의 origin이 정확히 `(0,0)`이기 때문에만** `spine`의 origin을 정확히 복원한다. `start + (end - start)`는 일반적으로 round-trip하지 않는다(이 좌표 범위의 랜덤 origin 쌍 약 18%가 실패한다). 두 최근접점이 일치한 뒤부터는 일반적이다. 두 거리가 같은 피연산자로 같은 식을 계산하므로 contraction이 양쪽에 똑같이 적용되고, `r / (r + r)`는 정확히 0.5다. 이것이 중요한 이유는 setup pose가 float32로 합성되어 실제 오차를 갖기 때문이다. 측정하니 `spine`은 `(0,50)`이 아니라 `(-2.1855694285477512e-06, 50)`이었고 `arm_l`은 `(-30,60)`이 아니라 `(-30.000001907348633, 60)`이었다. 설계 문서의 `§5.6`/`§13` 표에 있던 bit 단위 weight 값들은 이상적인 정수 origin으로 계산된 것이라 모두 틀렸다. `0.5/0.5`와 3중 동점만이 정확히 살아남았고, 설계가 그 둘을 고른 이유가 정확히 그것이다. 부동소수점 contraction도 예측이 아니라 측정했다. `point_segment_distance_squared()`의 식을 `-ffp-contract=on`(Marrow가 flag를 주지 않으므로 arm64 clang 기본값)으로 컴파일하면 fixture vertex 1의 `arm_l`이 `d² = 10496.001057976433`, `=off`면 `10496.001057976431`이다. 1 ULP가 쌍 중 작은 weight의 1 ULP로 전파된다. 그래서 bit 동일성은 **하나의 바이너리 안에서만** 주장하고 cross-architecture는 주장하지 않는다. AC3의 동점 처리는 관측 가능한 곳에서 검증해야 한다. cap을 모두 통과하는 후보들 사이의 동점은 출력에 보이지 않는다. weight가 정확히 같게 나오고 canonicalizer가 같은 weight를 다시 bone index 오름차순으로 정렬하기 때문이다. generator의 동점 처리는 **어느 후보가 살아남는지**를 바꾸는 곳에서만 관측된다. 그래서 4-influence cap을 가로지르는 거리 동점 case를 추가했다. generate는 결정적이지만 bit 단위로 idempotent하지 **않다**. rebind와 같은 이유다. `BoneWorldTransform`은 float32 여섯 개인데 bind offset은 double이다. 테스트는 같은 프로젝트에서의 두 실행이 byte 동일함만 주장하고, 이전 출력에 다시 적용한 것은 안정성만 측정해 출력한다. project smoke에서 `5.551e-17`, MCP에서 `0.000e+00`이었다. 거짓인 성질을 주장하지 않기 위해서다. 후보 목록은 **명시적 체크리스트**이고 반경이 아니다. 반경은 scale 의존적이고, rig이 바뀌면 후보를 조용히 넓히며, 어차피 체크리스트를 없애주지 못한다. `bones`는 wire에서 **필수**다. 빠뜨리면 "모든 bone"이 아니라 거부다. 체크리스트는 index가 아니라 **이름**으로 저장하고 `ProjectData`·`.marrow`·`editor-settings.json`·`PreviewState`·history snapshot 어디에도 넣지 않았다. undo가 다시 쓰는데 `history_snapshots_equal()`이 비교하지 않는 필드는 Ctrl+Z에서 튄다는 MAR-174의 교훈이다. Agent/MCP 표면은 `mesh.generate_weights` 하나가 늘어 정확히 62 operation이 됐다. `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1과 `.marrow` schema는 모두 그대로이고 `ProjectData`에 멤버를 더하지 않았으며 새로운 조정 가능한 수치 상수를 하나도 도입하지 않았다. 반경도, 지수도, 스무딩도, 최소 weight 슬라이더도 없다. 존재하지 않는 매개변수는 두 실행이 이견을 가질 수 없는 매개변수다.
- MAR-177은 2026-08-30에 constraint lifecycle project operation을 완료했다. **생성은 이미 있었다.** 네 family 모두 `shell_constraints.cpp`에 Add 버튼과 기본값 builder가 있고 `unique_constraint_name()`이 이름을 확보한다. 없던 것은 rename과 delete다. 이유는 overlay가 가진 동사가 정확히 둘뿐이기 때문이다. `merge_named_object_array_member()`는 이름이 맞는 root array 원소를 제자리에서 교체하거나, 없으면 끝에 append한다. "이 constraint의 이름이 바뀌었다"도 "이 constraint는 사라졌다"도 말할 수 없고, base `.mskl`에 사는 constraint는 upsert로 손댈 수조차 없다. 그래서 `.marrow`에 optional·default-absent member 하나를 더했다. `constraint_edits.operations`, **순서 있는** rename/delete record 배열이다. 순서가 load-bearing인 이유는 세 가지 shape이 증명한다. chain(`A→B`, `B→C`)은 앞 record가 실행된 뒤에만 존재하는 이름을 부르고, swap(`A→tmp`, `B→A`, `tmp→B`)은 통과 중에만 합법인 이름을 지나며, reuse(`delete A`, `rename B→A`)는 이 순서에서만 합법이고 반대 순서에서는 duplicate target이다. source를 키로 하는 map은 셋 중 어느 것도 표현하지 못한다. **이 story의 진짜 정정 위험은 skin이다.** skin은 constraint를 **이름으로** 참조하고, `parse_skin_scope_members()`는 해석되지 않는 이름 하나에 load 전체를 실패시킨다. `skins[*].<family>`를 함께 고치지 않는 delete는 미묘하게 틀린 rig을 만드는 게 아니라, **저장은 되는데 다시는 열 수 없는** project를 만든다. MAR-172가 `ok: true`를 반환하며 인접 key를 파괴했고 MAR-175가 commit 뒤 저장 불가를 남길 수 있었다면, 이것은 열기 불가를 남긴다. 역으로 증명했다. rename branch의 skin rewrite 절반을 지우면 `build_project_runtime()`이 `$.skins.cape.transform[0]: skin references unknown transform constraint 'cape_pull'`로 실패한다. 같은 이유로 family array를 비운 delete는 `[]`를 남기지 않고 **key 자체를 지운다**. runtime이 빈 family array를 `"transform constraints must not be empty when provided"`로 거부하기 때문이고, 이것도 역으로 증명했다. 소유권 규칙은 산문이 아니라 코드 한 곳이다. base에 있으면 record, project-only면 upsert 직접 rewrite, **둘 다면 양쪽 모두**다. 가운데 행이 틀리기 쉽다. base를 가리는 upsert는 shadowing이므로 upsert만 지우면 base constraint가 되살아나고 사용자에게는 "delete가 아무것도 안 했다"로 보인다. Materialization은 Phase A(lifecycle)가 Phase B(기존 네 upsert merge)보다 **엄격히 먼저** 실행된다. Validation은 `validate_project_for_save`가 이미 가진 seam에서 갈린다. 저장 시점에는 base가 없으므로 `consumed`/`introduced` 두 집합 위의 symbolic replay만 하고, materialization 시점에는 base를 갖고 missing source·duplicate target·family mismatch·invalid order 네 원인을 각각 다른 message로 보고한다. `consumed`에 upsert가 남아 있으면 거부하지만 `introduced`와 upsert가 겹치는 것은 **거부하지 않는다**. 그 상태가 shadowing rename이 반드시 만들어내는 바로 그 바이트이기 때문이고, 진짜 collision은 primitive의 preflight가 materialized name set으로 잡는다. Export signal은 유도한 값이고 스스로를 검증한다. `cape_pull`(9 B)을 `cape_pull_renamed`(17 B)로 바꾸면 `.mskl`은 **+16**, `.mbin`은 **+8**이다. JSON은 **출현 횟수**를 세고(root와 skin ref 두 번), MBIN의 `collect_strings`는 **서로 다른 문자열**을 한 번만 intern하므로 table 개수도 모든 index varint도 그대로다. `16 : 8` 비율 자체가 출현 횟수이고, root만 고친 구현은 `+8/+8`로 시끄럽게 실패한다. 측정값이 정확히 그랬다. .mskl 1611→1627, .mbin 605→613, string table 39 그대로(`cape_pull` 나가고 `cape_pull_renamed` 들어옴). 반면 **delete 쪽 설계 예측은 틀렸다.** string table이 1개 줄 것이라 했지만 실제로는 **4개** 줄어 39→35였고, 그 이유는 rename의 +8을 만든 것과 같은 interning이다. intern은 문서 전체에서 object key까지 포함해 distinct 문자열 단위이므로, subtree를 지우면 그 안에서만 나타나던 모든 문자열이 함께 사라진다. `cape_pull`(값), `transform`(root array key이자 skin scope key), `source`, `translateMix` 넷이고 `name`과 `bones`는 bone/slot이 쓰므로 남는다. 상수를 고치는 대신 decode해서 다시 유도했고, 테스트는 이제 사라진 네 문자열을 이름으로 주장한다. MAR-177은 model layer뿐이다. GUI 버튼, 확인 dialog, undoable command, `SelectionSet` cascade, agent/MCP operation은 MAR-178이고 registry는 그대로 정확히 62 ops다. `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1 모두 불변이고 `.marrow`는 optional member 하나만 늘었으며 비어 있으면 아예 쓰지 않으므로 기존 project의 직렬화는 byte 단위로 그대로다. 새로운 조정 가능한 수치 상수는 하나도 도입하지 않았다.
- MAR-178은 2026-08-30에 constraint rename/delete 표면을 완료했다. MAR-177이 `rename_constraint()`/`delete_constraint()`를 만들고 저장 불가·열기 불가 project를 남길 수 없음을 증명했지만 **아무도 부르지 않았다**. MAR-178은 표면 층이다. 네 family 모두에 `Rename... / Delete...` 행, 충돌 미리보기와 skin 참조 미리보기를 가진 두 modal, UI 없는 undoable command 하나, MAR-177이 명시적으로 미룬 `SelectionSet` cascade, 그리고 agent/MCP operation 둘이다. `.marrow` field도, runtime 동작도, 새 수치 상수도 더하지 않으므로 `format-spec.md`는 byte 단위로 그대로다. **selection 설계는 가정이 아니라 측정 두 개 위에 서 있다.** `rebuild_project_runtime()`은 `reconcile_selection_to_runtime()`을 부르지 않는다. 부르는 곳은 `reload_project()`(`shell_core.cpp:629`)와 runtime asset hot reload(`shell_asset_watch.cpp:189`) 둘뿐이다. 그래서 보통의 편집은 아무것도 조정하지 않는다. 명시적 remap 없는 rename은 selection을 조용히 잃고, delete는 유령을 남겨 상태줄의 *"; N selected"* 개수를 부풀린다(`shell_selection.cpp:241`). 그리고 `prune`(`selection.cpp:155-159`)과 `remap`(`:195-199`)은 **같은** 마지막 생존자 fallback을 고른다. 그래서 delete의 remap과 이후의 reconcile은 무엇이 active인지에 대해 이견을 가질 수 없다. rename은 identity를 remap하고 delete는 `nullopt`으로 remap하며, cascade는 **commit이 성공한 뒤에만** 돈다. 이웃을 자동 선택하지는 않는다. animation catalog는 preview가 무언가를 보여줘야 해서 대체를 고르지만, constraint 패널에는 그런 요구가 없고, 사용자가 지금 누르고 있는 Delete 버튼 아래에 다른 constraint를 밀어 넣는 것이 되기 때문이다. **undo/redo는 의도적으로 selection을 복원하지 않는다.** `EditorHistorySnapshot`에는 `SelectionSet`이 없고 `history_snapshots_equal()`은 세 field만 비교한다. 하나를 더하면 MAR-174의 Ctrl+Z 튐이 그대로 재현되고, 비교기에도 더하면 순수 selection 변경이 project 편집으로 세어 undo 항목을 만들기 시작한다. 그래서 세 번째 선택지를 골랐다. 두 경로가 각각 새 `reconcile_constraint_selection()` 호출 하나씩을 얻는다. 오래된 `ConstraintSelection`만 걷어내고 bone·slot·attachment는 건드리지 않는다. 이것을 `reconcile_selection_to_runtime()`으로 넓히면 editor의 **모든** undo 뒤에 bone selection이 잘려 나간다. 역으로 증명했다. 해석되지 않는 `phantom_bone`을 함께 선택해 두면 넓힌 버전이 그것을 잘라내며 실패한다. agent에는 cascade할 selection이 아예 없다. `AgentCommandContext`는 정확히 `{session, control}`이므로 command는 nullable `SelectionSet*`를 받고 GUI는 `&state->selection`을, agent는 `nullptr`을 넘긴다. MAR-172처럼 명세를 GUI로 조용히 좁힌 것이 아니라, agent가 selection을 갖지 않는다는 구조적 사실을 기록한 것이고 selection을 가진 모든 호출자는 요구를 그대로 지킨다. 이 operation들은 **단일 대상**이다. `(family, name)`이 정확히 하나를 지목하므로 GUI는 건너뛰고 agent는 거부하는 이 저장소의 batch 비대칭이 적용되지 않는다. 건너뛸 집합이 없고 "1개 중 1개 건너뜀"은 더 나쁜 message를 가진 거부일 뿐이다. 그래서 **두 표면이 같은 primitive의 같은 message로 똑같이 거부한다**. 차이는 표현뿐이다. GUI는 이미 사용 중인 이름을 미리 보고 Apply를 비활성화하고 거부 시 modal을 열어 둔다. 충돌은 refuse이지 auto-suffix가 아니다. `unique_constraint_name()`은 create 경로의 할당자이고, 사용자가 친 이름 대신 다른 이름을 조용히 주는 것이 거절보다 나쁘기 때문이다. dry run은 project를 복사해 **같은** primitive를 돌리고 `catalog_delta()` builder 하나가 두 payload를 만든다. 손으로 쓴 존재 검사로 바꾸면 dry-run/live message 동일성이 곧바로 깨진다. 역으로 증명했다. atlas gate는 뭉툭한 채로 남겼다. 좁히면 얻는 것이 **도달 가능한 범위에서 0**이기 때문이다. `load_project()`가 빈 `$.runtime.atlases`를 이미 거부하고 shell의 유일한 입구가 `reload_project()` → `EditorSession::open()` → `load_project()`이므로 atlas 없는 `ProjectData`는 command에 도달조차 못 한다. 그 상태에서는 `save_project()`도 이미 거부하므로 rename의 거부는 **사용자가 이미 가진 장애물을 보고할 뿐 새로 만들지 않는다**. 결정이 조용히 상속되지 않도록 scenario E가 이를 못박는다. delete 검증은 반환값이 아니라 **save → reload**로 한다. 최악의 실패는 저장이 아니라 load에서 터지기 때문이다. 역으로 증명했다. root만 고치고 skin 참조를 남긴 rename은 export에서 `cape_drag`를 2번이 아니라 **1번** 내고 reload가 `$.skins.cape.transform[0]: skin references unknown transform constraint 'cape_pull'`로 실패한다. 마지막 constraint를 지우는 것도 막지 않는다. family는 정당하게 빌 수 있고, 그래서 비워진 family array는 `[]`를 남기지 않고 key 자체를 지운다. export가 root `transform` key도 `skins.cape.transform` key도 갖지 않고 reload되며 `cape_target`이 남는 것을 확인했다. family 철자는 `project.cpp`의 file-local `constraint_family_json_key()`를 export하는 대신 새 header에 자기 것을 선언했다. 중복이 만드는 drift는 **구조가 아니라 test로** 막는다. 네 family 각각에 대해 lifecycle record를 하나 넣고 직렬화한 뒤 나온 `"family"` 문자열이 `constraint_family_key(family)`와 같은지 주장한다. 어느 쪽이 움직여도 시끄럽게 실패한다. Agent/MCP 표면은 `constraint.rename`과 `constraint.delete` 둘이 늘어 정확히 64 operation(조회 12, 검증 3, 관리 10, 편집 39)이 됐다. `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1과 `.marrow` schema 모두 그대로이고 `ProjectData`에 멤버를 더하지 않았으며 새로운 조정 가능한 수치 상수를 하나도 도입하지 않았다.
- MAR-182는 2026-08-30에 dirty session intent 통합을 완료했다. 작업을 버릴 수 있는 다섯 경로 — New, Open, Reload, Quit, 네이티브 OS close — 앞에 Save/Discard/Cancel state machine 하나를 세웠다. 제목의 "통합"은 서로 다른 검사가 여럿 있었다는 뜻이지만 실측 결과 **검사는 0개**였고 OS close는 veto할 방법조차 없었으므로, 작업의 실체는 조정이 아니라 구축이다. Gate는 `EditorSession::dirty()` 하나만 읽는다. 그것이 `serialize_project()` 결과와 마지막으로 읽거나 쓴 bytes를 비교하는 content-keyed 신호이기 때문이며, `ShellState::project_dirty`는 읽지도 쓰지도 않는다. 후자는 caller가 넘긴 boolean에서 대입되는 display cache이고 refresh 지점 둘이 `shell_main.cpp`에만 있어 gate로 쓰면 smoke와 출하 shell에서 의미가 갈린다. 틀린 답의 비용도 비대칭이다 — false negative는 사용자의 작업을 조용히 파괴하고 false positive는 dialog 하나를 더 보여줄 뿐이다. Save는 원자적 저장이 성공한 뒤에만 intent를 완료시키며 완료 조건도 `!session.dirty()`다. `save_project_file`의 반환값은 쓰지 않는다. deferred Save As 분기에는 읽을 반환값이 없고 즉시 분기에서도 authoring gesture 중에는 저장 없이 false를 돌려주기 때문이다. 저장 실패는 session·intent·`project_path`·대상 파일을 그대로 두고 prompt를 다시 띄운다. 실패에서 discard나 교체나 종료로 흘러가는 경로는 검사로 막는 것이 아니라 **구조적으로 없다**: `perform_session_intent`의 호출 지점은 정확히 둘뿐이고 둘 다 실패에서 도달 불가다. Cancel은 정의상 아무것도 하지 않아 `SessionSnapshot`이 bit 단위로 동일하고, intent를 숨겨 두지 않고 다시 표현 가능하게만 남긴다 — 숨겨 둔 intent는 다음 클릭을 기습한다. Discard는 저장을 거부할 뿐 되돌리거나 덮어쓰지 않으므로 New/Open/Reload에서는 원자적 교체가 착지하기 전까지 작업이 살아 있고 Quit에서만 즉시 손실이다. Reload는 `bool*` out-parameter를 세 surface와 두 frame body에서 걷어내고 `FileAction::Reload`로 기존 deferred rail에 합류했다 — frame body 로직의 순삭제라 duplicate-frame-body 위험을 늘리지 않고 기존 C11 보호를 그대로 물려받는다. Quit은 메뉴 항목에서 곧장 gate로 들어가고 `ProjectMenuAction` enum은 삭제했다. 예전 처리 자리인 `shell_main.cpp`의 frame body는 반환값을 통째로 버리는 쌍둥이를 갖고 있어 거기 둔 gate는 어떤 테스트에도 보이지 않기 때문이다. Veto가 실제로 동작하도록 `ShellState::should_exit`가 main loop의 유일한 종료 조건이 되었고 `EditorWindowHost::request_close()`는 삭제했다. 구현체가 하나뿐이라 compiler가 강제하며, 막을 수 없는 prompt는 없느니만 못하다. Prompt는 `draw_file_path_modals`에서 root scope로 그린다. 그 함수는 두 frame body가 이미 `draw_menu_bar`를 통해 도달하므로 frame body 편집이 필요 없다. 함께 고친 것은 chooser의 stale-request 구멍이다. 버튼이 아닌 경로로 popup이 닫히면 `file_path_request`가 영원히 남던 문제로, 기존에는 다음 `begin_file_action`이 통째로 다시 seed해서 self-heal했지만 MAR-182에서는 `AwaitingSave` intent가 영영 풀리지 않는 hang이 된다. 외부에서 닫힌 chooser는 cancel이고 외부에서 닫힌 prompt도 cancel이다. Prompt의 Save는 목적지가 없으면 Save As로 재귀하므로 공용 chooser에 도달하고, 그곳의 압도적 다수 경우는 **이미 존재하는 파일 위에 쓰는 것**이다. 따라서 `resolve_choice`의 수용 규칙에 의존한다. `Choose`는 `FilePathChoice::acceptable` 하나로만 gate되고 존재하는 Save 대상은 `"Replaces the existing file."` diagnostic을 **가진 채로 수용**된다. diagnostic이 비어 있는지로 gate하면 바로 그 흔한 경우에 `Choose`가 비활성화되어 `AwaitingSave`에 출구가 없어지고 prompt를 해소할 수 없게 된다. MAR-182는 `resolve_choice`를 건드리지 않으며 C15 half B가 두 조건을 동시에 확인한다. `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1, 64-operation Agent/MCP surface는 모두 그대로다. Recent Projects는 MAR-183 경계이며 그쪽은 `begin_session_intent(SessionIntent::Open)`을 부르기만 하면 된다.
- MAR-183은 2026-08-30에 최근 프로젝트 목록 영속화와 관리를 완료했다. 브리핑은 저장 계층을 새로 만들라고 했지만 실측 결과 **저장 계층은 이미 완성되어 있었다**. `EditorPreferences::recent_projects`는 MAR-156부터 settings **version 1 안에서** parse·serialize·unit test와 함께 출하돼 있었고, 없던 것은 그것을 쓰는 **기능 전체**였다 — reader 0개, writer 0개, 메뉴 0개. 그래서 이 이야기는 저장을 추가하지 않는다. parse도 serialize도 version bump도 없고 `kEditorSettingsVersion`은 **1 그대로**다. 올렸다면 이 에디터가 지금까지 쓴 모든 설정 파일이 이 에디터에서 읽히지 않게 되는데, 그것은 version 1이 이미 올바르게 기술하는 field를 다시 기술하기 위한 대가다. 목록은 `weakly_canonical` 기준 canonical absolute path를 most-recent-first로 최대 `kRecentProjectLimit`(10)개 담고, 연산은 `erase-equal → insert-front → truncate` 순서다. **insert가 truncate보다 먼저**여야 한다 — 뒤집으면 bound에 정확히 걸린 순간 사용자가 방금 연 항목이 잘려 나간다. 동일성은 canonical 결과의 **byte 비교**이고 우리 쪽에서 case를 접지 않는다. `resolve_choice`가 이미 출하한 규칙이며 이유도 같다 — 접으면 에디터의 동작이 host에 따라 달라진다. 다만 그 macOS 귀결은 설계 문서가 적은 것과 **반대**임을 실측했다: `weakly_canonical`은 존재하는 최장 prefix를 파일 시스템으로 해석하므로, **존재하는** 파일의 두 대소문자 표기는 디스크상 표기로 수렴해 **한 항목으로 합쳐지고**, **없는** 파일의 두 표기만 그대로 남아 두 항목이 된다. 기록 시점은 성공한 Open, 성공한 Save As, 그리고 New 세션의 **첫** 저장뿐이다. 마지막 것은 문서의 어떤 속성으로도 구분되지 않으므로 판별자를 `ShellState::pending_recent_on_first_save`라는 명시적 optional로 두었다 — `create`만 세우고 그 경로를 실제로 쓴 저장만 소비한다. bool이 아니라 path인 이유는 세션을 다른 곳으로 옮긴 Save As가 팔을 잘못 소비하지 못하게 하기 위해서다. **자동 정리는 어디에서도 하지 않는다** — load에서도, 그리기에서도, 클릭에서도. 존재 여부는 submenu가 열려 있는 동안 항목마다 매 프레임 읽으므로 cache가 없고 따라서 무효화 규칙도 없으며, 다시 mount된 볼륨은 사용자가 아무것도 하지 않아도 스스로 되살아난다. load에서 정리한다는 것은 곧 **load에서 쓴다**는 뜻이고, 그러면 잠깐 unmount된 볼륨 하나 때문에 사용자의 북마크가 디스크에서 영구히 사라진다. 그래서 **loading은 결코 쓰지 않는다**: 손으로 편집하던 설정 파일은 실행을 거쳐도 그대로고, 디스크의 과대한 목록은 메모리에서만 잘리고 다음 실제 변경 전까지 디스크에서는 과대한 채 남는다. Recent 항목 열기는 MAR-182 gate를 **우회하지 않는다**. `open_recent_project`의 본문은 `begin_session_intent(state, SessionIntent::Open, path)` 한 줄이 전부이고, `DirtyIntentRequest`가 목적지 `path`를 얻었다. "마지막 소원이 이긴다" retarget은 `intent`와 `path`를 **둘 다 무조건** 대입한다 — intent만 옮기면 사용자가 두 번째 항목을 눌렀는데 첫 번째가 열리고, path를 비어 있지 않을 때만 대입하면 뒤이은 Reload가 낡은 Open 목적지를 물려받는다. 표면은 File 메뉴의 `Open Recent` submenu이며, 사라진 항목은 **보이되 비활성**이고 언제나 활성인 `Remove` submenu와 `Clear Missing`이 제거 수단이다. 비활성 항목은 클릭도 우클릭도 받지 않으므로 제거를 그 행에 둘 수 없다. `.marrow` schema, `.mskl` v1, `.mbin` v2, C ABI v1, `editor-settings.json` v1, 64-operation Agent/MCP surface 모두 그대로다.
- Task #28의 behavior-preserving 핵심 경계 리팩터, MAR-163과 MAR-164는 2026-08-16에, MAR-165, MAR-166과 MAR-167은 2026-08-20에, MAR-168, MAR-169, MAR-170, MAR-171, MAR-172, MAR-173과 MAR-174는 2026-08-30에 완료됐다. MAR-175는 완료된 MAR-174에 직접 의존하는 다음 제품 milestone이다. MAR-192~210 플랫폼 프로그램은 별도 재개 결정 전까지 open인 병렬 보류 qualification backlog다. 코드가 한 호스트에서 동작하는 것과 지원 플랫폼 qualification은 계속 구분한다. 2026-08-12 범위 결정에 따라 현재 qualification 대상은 macOS arm64와 Windows 11 x64뿐이다. Ubuntu/Linux와 Windows 10은 `NOT REQUIRED`이며 지원을 주장하지 않는다. 별도 PC portable 실행도 필수 gate가 아니고 같은 Windows 11 호스트의 새 압축해제 폴더 검증을 현재 package gate로 사용한다. 실물 Windows Ink, Windows 11 고배율·수동 UI, 성능·pixel·resource evidence가 기록되기 전에는 MAR-210을 완료 처리하지 않는다.
- Task #28의 viewport 내부 경계에서 `viewport_interaction_kernel`은 ImGui, Sokol, `ShellState` 없이 frozen rotation basis/unwrap, snap activation/scalar quantization, signed·exact-zero·uniform-ratio scale mapping/snap, visible-grid integer-multiple selection, FFD nearest-hit/weighted inverse, point·box selection set algebra, atomic multi-pair full-vector update와 FFD magnetic/grid precedence·stable tie-break를 계산한다. `viewport_interaction_controller`는 Bone transform과 active-gesture snap 적용을, shell-private `viewport_ffd_controller`는 attachment-local selection reconciliation, group FFD begin/update/finish, visible-candidate snapshot과 live canvas reprojection, deform materialization, linked target, session transaction, rollback과 one-undo를 소유한다. `shell_viewport_ui`는 ImGui 입력·modifier 해석, project-only viewport snap settings와 selected/hover/drag-source/marquee/snap-guide 표시만 소유하고 FFD 회귀는 별도 `shell_smoke_ffd.cpp` translation unit에 격리한다. MAR-165/166은 public project model에 optional `ProjectSnapSettings`와 magnetic enable field만 additive하게 추가하며 이 private interaction 경계와 entity `SelectionSet` identity는 바꾸지 않는다.
- Task #28의 timeline 내부 경계에서 `timeline_model`은 ImGui와 `ShellState` 없이 track/key identity, same-time ordinal, selection reconciliation, clipboard collision/order, retime bounds·snap, duration maximum과 completion 결정을 계산한다. `timeline_controller`가 effective imported curve materialization, project/runtime mutation, session transaction, rollback과 one-undo를 소유하고 `shell_timeline`은 ImGui 입력 해석과 표시를 담당한다. Add-key와 live retime 직후에는 현재 frame의 row reference를 안전하게 다시 잡기 위해 의도적으로 uncached track rebuild를 사용하고, 일반 표시 경로의 runtime-revision/identity keyed cache는 유지한다.
- Parameter slider와 agent `parameter.set`은 ID 기반 direct preview 입력이며 `.marrow`나 runtime export에 저장하지 않는다. Persistent parameter-model CRUD만 project dirty 상태를 바꾼다.
- Setup Pose는 현재 읽기 전용이다. Animation 모드의 본 포즈 변경은 현재 playhead의 키프레임으로 저장해야 하며, 저장되지 않는 preview-only 포즈/색상 편집은 허용하지 않는다.
- 본·슬롯·스킨·어태치먼트 생성/삭제, 재부모화, 메시 topology·경로 제어점 편집, entity `SelectionSet`의 group transform, partial/degraded project open은 현재 P1 범위 밖이다. MAR-164의 attachment-local FFD vertex sub-selection은 entity selection이나 hierarchy anchor/timeline focus를 바꾸는 group transform이 아니다.
- P1은 `.mskl` v1, `.mbin` v2와 C ABI v1을 유지한다. 구 자산 fallback과 기존 C 함수·ownership 규칙을 깨는 version bump는 하지 않는다.
- 장기 자체 리그 저작 단계에서는 기존 이름 기반 오버레이를 계속 확장하지 않는다. 버전과 stable ID를 가진 canonical `.marrow` authoring graph를 먼저 설계하고, 임포트 자산을 그 graph로 한 번 변환하는 경계를 정의한다.

---

## 3. 기술 스택

| 항목 | 결정 | 이유 |
|---|---|---|
| 에디터 언어 | **C++** | molga-engine과 동일 스택, 학습 비용 없음 |
| 에디터 UI | **Dear ImGui** | Mac 지원, MIT 라이센스, C++ 친화적 |
| 창·입력 | **SDL3** | high-DPI, IME, pen pressure와 macOS/Windows/Linux 단일 event 경계 |
| 에디터 렌더링 | **sokol_gfx + sokol_imgui** | macOS Metal, Windows/Linux GLCORE 4.1의 단일 GPU 자원·pass 모델 |
| standalone sample | **sokol_app + sokol_glue** | convenience sample host에만 한정하며 editor에는 링크하지 않음 |
| 스키닝 방식 | **GPU 스키닝 (Vertex Shader)** | 성능 우위, 쉐이더 커스텀 가능 |

---

## 3-1. 플랫폼·GPU 소유권

최종 production 경계는 다음과 같다.

| 플랫폼 | 창·입력 | GPU/ImGui | qualification 범위 |
|---|---|---|---|
| macOS arm64 | SDL3 Metal window, SDL3 input/IME/pen | `sokol_gfx` Metal + `sokol_imgui` | 현재 Metal 실기 호스트 |
| Windows x64 | SDL3 OpenGL window, SDL3/Windows Ink pen | `sokol_gfx` GLCORE 4.1 + `sokol_imgui` | Windows 11, VS2022 |
| Linux x64 | SDL3 X11 OpenGL window | `sokol_gfx` GLCORE 4.1 + `sokol_imgui` | 구현 유지, 현재 qualification/support 범위 밖 (`NOT REQUIRED`) |

이 표는 구현 경계와 현재 검증 매트릭스다. 실제 지원 주장은
`docs/root1/platform-validation.md`의 같은 revision 실기 PASS에만 근거한다.
Linux/Ubuntu, Windows 10, Wayland, Windows ARM64, D3D/Vulkan, installer,
ImGui OS multi-viewport는 현재 지원·qualification 범위가 아니다.

렌더러는 네 내부 경계로 나뉜다.

- `marrow_renderer_commands`가 scene preparation, atlas/PNG decode, geometry,
  command packing, CPU cache, software stencil을 소유하며 `marrow_runtime`과
  zlib만 링크한다. C ABI, Agent smoke, runtime unit/fixture test, CPU benchmark는
  이 경계까지만 사용한다.
- `marrow_renderer_core`가 executable별 유일한 `SOKOL_GFX_IMPL`과 한 번만
  컴파일되는 Sokol scene executor를 소유한다. White fallback, sampler,
  shader, streaming buffer와 format/depth/sample 기반 GPU pipeline cache가 이
  계층에 있다.
- `marrow_renderer_sapp_host`만 `SOKOL_APP_IMPL`과 `SOKOL_GLUE_IMPL`을 소유하며
  device/pass/window adapter에서 scene executor에 위임해 standalone renderer
  sample을 구동한다. Editor와 CPU-only binary에는 `sapp_*`/`sglue_*` 심볼이
  없어야 한다.
- `marrow_editor_shell`만 SDL3, ImGui SDL3 platform backend, `sokol_imgui`,
  window surface와 main pass/commit을 소유한다. UI-free `marrow_editor`는
  SDL/Sokol/ImGui에 의존하지 않는다.

기존 `marrow_renderer` 타깃 이름과 public renderer/C ABI 표면은 호환성
umbrella로 유지하며 새 내부 타깃을 설치 API로 노출하지 않는다.

Editor frame은 SDL event poll과 raw pen metadata 갱신, event당 한 번의
`ImGui_ImplSDL3_ProcessEvent`, `ImGui_ImplSDL3_NewFrame`, `simgui_new_frame`,
1x offscreen viewport, main swapchain pass, `simgui_render`, `sg_commit`, GL에서만
`SDL_GL_SwapWindow` 순서다. UI와 pointer는 logical coordinate를 사용하고 GPU
target은 drawable pixel size를 사용한다. Texture UV는 hard-coded flip이 아니라
`sg_query_features().origin_top_left`로 결정한다.

종료는 authoring gesture/callback 차단, viewport/icon/atlas/scene resource,
ImGui SDL3 backend, `simgui`, `sg`, native Metal/GL surface, SDL window, `SDL_Quit`
순서다. Nested `sg_setup`은 허용하지 않는다.

Pen pressure는 shell transient `ViewportPointerState`에만 존재한다. Synthetic pen
mouse가 ImGui 위치·button을 전달하고 raw pen event는 pressure/tilt/eraser metadata만
갱신한다. Paint/Erase/Smooth stamp 강도는
`configured_strength × pressure × radial_falloff`이며 mouse와 pressure-axis 없는
장치는 `1.0`이다. Radius, spacing, project/preference/runtime format, C ABI와
Agent/MCP 표면은 바뀌지 않는다.

---

## 4. 파일 포맷

| 확장자 | 용도 | 형식 |
|---|---|---|
| `.marrow` | 에디터 프로젝트 파일 | JSON (초기), 추후 바이너리 컴파일러 추가 |
| `.mskl` | 본 구조 + 애니메이션 데이터 (runtime import용) | JSON interchange/runtime source |
| `.mbin` | 프로덕션용 런타임 스켈레톤 바이너리 | Compact binary |
| `.matl` | 텍스처 아틀라스 메타데이터 | JSON |
| `.png` | 실제 텍스처 아틀라스 | 이미지 |

> 개발·검사에는 `.mskl` JSON을 사용하고 프로덕션에는 같은 runtime document의 `.mbin` binary export를 사용할 수 있다.
> `.mskl` / `.matl` 루트에는 정수 `version` 필드가 포함되며, 현재 런타임은 `version: 1`만 로드한다.
> `.matl.atlas.premultiplied_alpha` 불리언으로 straight alpha와 PMA 텍스처를 구분하며, 렌더러는 이 값을 기준으로 셰이더/블렌드 경로를 전환한다.
> `.matl.regions[].rotate`는 선택적인 아틀라스 내 회전 각도(도 단위)를 보존하며, Spine `.atlas` 멀티페이지 import는 페이지당 하나의 `.matl` 파일로 변환한다.
> 현재 `.mbin` wire version은 v2다. 검증된 런타임 문서를 그대로 보존하면서 회전/이동 키프레임에 대해 16-bit 시간/채널 인덱스 + 양자화 payload를 추가 저장해, 내보내기 시 키 감소와 런타임 quantized playback을 지원한다.
> `.marrow`와 exported `.mskl` JSON은 finite rotation degree를 정규화하지 않아 360도를 넘는 값을 보존한다. `.mbin` v2 canonical generic payload도 raw key를 보존하며, optional AKEY가 continuous multi-turn rotate channel을 허용 오차 안에서 표현하지 못하면 그 channel만 packed section에서 생략한다. Runtime JSON-MBIN acceptance는 360도 동치 orientation을 보장하며 segment winding playback은 MAR-161 계약이 아니다.
> Scale key는 finite signed value와 exact zero를 `.marrow`, exported `.mskl`, `.mbin` v2 canonical generic payload에 그대로 보존한다. MAR-162는 기존 transform overlay와 wire schema를 재사용하므로 포맷 version을 변경하지 않는다.

---

## 5. `.mskl` 포맷 초안

```json
{
  "marrow": "1.0",
  "version": 1,
  "skeleton": {
    "name": "player",
    "width": 256,
    "height": 256
  },
  "bones": [
    { "name": "root" },
    { "name": "spine", "parent": "root", "x": 0, "y": 50 },
    { "name": "arm_l", "parent": "spine", "x": -30, "y": 10 }
  ],
  "slots": [
    { "name": "body", "bone": "spine", "attachment": "body" },
    { "name": "arm_l", "bone": "arm_l", "attachment": "arm_l" }
  ],
  "animations": {
    "idle": {
      "bones": {
        "spine": {
          "rotate": [
            { "time": 0.0, "angle": 0, "curve": "linear" },
            { "time": 0.5, "angle": 5, "curve": [0.25, 0.1, 0.75, 0.9] },
            { "time": 1.0, "angle": 0, "curve": "stepped" }
          ]
        }
      }
    }
  }
}
```

> `skins`는 기존처럼 슬롯별 단일 어태치먼트를 바로 둘 수도 있고, 여러 어태치먼트가 필요한 경우 `attachments -> slot -> attachment` 중첩 맵을 사용할 수 있다. Spine JSON importer는 이 중첩 형식을 출력한다.

---

## 6. 키프레임 보간 방식

| 타입 | 값 | 설명 |
|---|---|---|
| Linear | `"linear"` | 직선 보간 |
| Stepped | `"stepped"` | 이전 값 유지 후 순간 전환 |
| 베지에 | `[cx1, cy1, cx2, cy2]` | 큐빅 베지에 제어점 (CSS cubic-bezier 동일 구조) |

---

## 7. GPU 스키닝 쉐이더 구조

```glsl
uniform mat4 u_bones[64];     // 본 행렬 배열

in vec2 a_position;
in vec4 a_bone_indices;        // 영향받는 본 인덱스 (최대 4개)
in vec4 a_bone_weights;        // 각 본의 웨이트

void main() {
    mat4 skinMatrix =
        u_bones[int(a_bone_indices.x)] * a_bone_weights.x +
        u_bones[int(a_bone_indices.y)] * a_bone_weights.y +
        u_bones[int(a_bone_indices.z)] * a_bone_weights.z +
        u_bones[int(a_bone_indices.w)] * a_bone_weights.w;

    gl_Position = skinMatrix * vec4(a_position, 0.0, 1.0);
}
```

> 버텍스당 최대 4개 본 영향 - 업계 표준

---

## 8. 구현 기능 목록

### 필수 (Core)
- [x] 본 계층 구조 (parent-child transform)
- [x] 메시 변형 (Linear Blend Skinning) - **Marrow 핵심 동기**
- [x] 텍스처 아틀라스
- [x] 키프레임 보간 (Linear / Stepped / 베지에)

### 높은 우선순위
- [ ] **IK Constraint** - 발/손 고정, 없으면 애니메이션 제작 고통
- [ ] **Animation Mixing (Track 기반)** - 트랙별 독립 애니메이션 재생 + 크로스페이드
- [ ] **Skin System** - 같은 본에 텍스처 교체 (장비 변경)
- [ ] **Draw Order Timeline** - 애니메이션 중 슬롯 렌더 순서 동적 변경
- [ ] **Slot Color/Alpha Animation** - 슬롯별 색상·투명도 키프레임

### 중간 우선순위
- [ ] **Path Constraint** - 본이 곡선 경로 추적 (꼬리, 머리카락)
- [ ] **Transform Constraint** - 본 A → 본 B 위치/회전/스케일 비율 연동
- [ ] **Events** - 특정 프레임에 이벤트 발생 (발소리, 이펙트 타이밍)
- [ ] **Weighted Mesh** - 버텍스당 다중 본 가중치 (최대 4본)
- [ ] **Free-Form Deformation (FFD)** - 메시 버텍스 직접 변형 키프레임
- [ ] **Attachment Timeline** - 애니메이션 중 슬롯의 어태치먼트 전환
- [ ] **Bone Scale/Shear Timeline** - 본 스케일·전단 키프레임
- [ ] **Linked Mesh** - 다른 메시의 버텍스/가중치를 참조하는 메시 (스킨 간 공유)
- [x] **Inherit Timeline runtime/read-only** - 5개 inherit mode의 stepped runtime timeline 구현 완료; project overlay와 editor parity는 MAR-184~185
- [ ] **Shortest Rotation playback policy** - MAR-161의 raw multi-turn authoring/storage와 별개로 runtime segment의 shortest-path/winding playback 정책은 후속 범위

### 추가 기능
- [ ] **Clipping** - 메시 마스킹 (다각형으로 렌더 영역 제한)
- [ ] **Bounding Box** - 히트 판정용 다각형 (본에 부착, 충돌/클릭 감지)
- [ ] **Point Attachment** - 본 위 특정 좌표+각도 (파티클 생성점, 무기 장착점)
- [ ] **Sequence Attachment** - 프레임 단위 이미지 시퀀스 재생 (이펙트, 연기)
- [ ] **Physics Constraint** - 스프링 기반 물리 시뮬 (머리카락, 장식 흔들림)
- [ ] **Two Color Tinting** - 슬롯별 라이트/다크 듀얼 컬러 (풍부한 색조 표현)
- [ ] **Blend Mode** - 슬롯별 블렌드 모드 (Normal / Additive / Multiply / Screen)
- [ ] **Reverse Playback** - 애니메이션 역재생 (`TrackEntry.reverse`)
- [ ] **Root Motion** - 루트 본 이동을 게임 월드에 반영 + 델타 보정
- [ ] **Skin Constraints** - 특정 스킨 활성 시에만 적용되는 조건부 Constraint
- [ ] **SkeletonBounds** - Bounding Box 기반 히트 판정 유틸리티 클래스
- [ ] **Setup Pose Reset** - 스켈레톤을 초기 설정 포즈로 복원
- [x] **Binary Export** - `.mskl`과 동등한 `.mbin` v2 export·quantized playback 구현 완료

---

## 8-1. 주요 기능 상세 설명

### Animation Mixing (Track 기반)

Spine의 AnimationState는 **다중 트랙** 구조로 애니메이션을 관리한다.

```
Track 0: walk (루프)          ← 하체 기본 동작
Track 1: attack (1회)         ← 상체 오버라이드
Track 2: hit_flash (1회)      ← 전신 색상 효과
```

| 기능 | 설명 |
|---|---|
| **Track Mixing** | 각 트랙은 독립 재생, 높은 트랙이 낮은 트랙 위에 블렌딩 |
| **Crossfade** | `setAnimation(track, anim, mixDuration)` → 이전 애니메이션과 부드럽게 전환 |
| **Queue** | `addAnimation(track, anim, delay)` → 현재 애니메이션 후 대기열 재생 |
| **Mix Alpha** | 트랙별 알파값으로 블렌딩 강도 조절 (0.0 ~ 1.0) |
| **Empty Animation** | 특정 트랙을 서서히 비활성화 (빈 애니메이션으로 페이드아웃) |

**`.mskl` 확장안:**
```json
{
  "mixing": {
    "default_mix": 0.2,
    "entries": [
      { "from": "walk", "to": "run", "duration": 0.15 },
      { "from": "run", "to": "idle", "duration": 0.3 },
      { "from": "*", "to": "attack", "duration": 0.1 }
    ]
  }
}
```

---

### Skin System

같은 스켈레톤 구조에서 **슬롯별 어태치먼트를 교체**하여 외형을 변경한다.

```
Skin: "warrior"                  Skin: "mage"
├── slot:body → warrior_body     ├── slot:body → mage_body
├── slot:helm → iron_helm        ├── slot:helm → wizard_hat
└── slot:weapon → sword          └── slot:weapon → staff
```

| 기능 | 설명 |
|---|---|
| **기본 스킨** | 에디터에서 설정한 기본 외형 |
| **스킨 교체** | 런타임에 전체 스킨 일괄 변경 |
| **스킨 합성** | 여러 스킨의 부분 조합 (머리A + 몸B + 무기C) |
| **커스텀 스킨** | 런타임에 새 스킨 생성 후 슬롯별 어태치먼트 수동 지정 |

**`.mskl` 확장안:**
```json
{
  "skins": {
    "warrior": {
      "body": { "attachment": "warrior_body" },
      "helm": { "attachment": "iron_helm" },
      "weapon": { "attachment": "sword" }
    },
    "mage": {
      "body": { "attachment": "mage_body" },
      "helm": { "attachment": "wizard_hat" },
      "weapon": { "attachment": "staff" }
    }
  }
}
```

---

### IK Constraint

**Inverse Kinematics** - 타겟 본 위치로 체인 본들의 회전을 자동 계산한다.

```
shoulder → elbow → hand → [IK Target]
         자동 회전 계산 ←────────┘
```

| 속성 | 설명 |
|---|---|
| **target** | IK 타겟 본 |
| **bones** | IK 체인에 포함되는 본 목록 (1본 또는 2본) |
| **mix** | IK 적용 비율 (0.0=FK, 1.0=완전IK) |
| **bendPositive** | 2본 IK에서 관절 꺾임 방향 |
| **softness** | 타겟 근처에서 부드러운 감속 거리 |
| **compress / stretch** | 1본 IK에서 본 길이 압축/늘림 허용 |

**`.mskl` 확장안:**
```json
{
  "ik": [
    {
      "name": "left_arm_ik",
      "bones": ["upper_arm_l", "lower_arm_l"],
      "target": "hand_l_target",
      "mix": 1.0,
      "bendPositive": true,
      "softness": 0
    }
  ]
}
```

---

### Path Constraint

본 체인을 **베지에 경로**를 따라 배치한다. 꼬리, 머리카락, 로프 등에 사용.

```
path: ●───○───○───●───○───○───●
        bone1  bone2  bone3  bone4
```

| 속성 | 설명 |
|---|---|
| **path slot** | 경로를 정의하는 슬롯 (PathAttachment) |
| **bones** | 경로를 따르는 본 체인 |
| **position** | 경로 시작 위치 (0.0 ~ 1.0) |
| **spacing** | 본 간격 (고정 길이 / 비율) |
| **rotate mix** | 경로 방향으로 본 회전 적용 비율 |
| **translate mix** | 경로 위 위치 이동 적용 비율 |

---

### Transform Constraint

본 A의 트랜스폼을 **본 B에 비율로 복사**한다. 기어 연동, 그림자 등에 사용.

| 속성 | 설명 |
|---|---|
| **source** | 참조할 원본 본 |
| **target bones** | 영향받는 본 목록 |
| **rotateMix** | 회전 복사 비율 |
| **translateMix** | 위치 복사 비율 |
| **scaleMix** | 스케일 복사 비율 |
| **shearMix** | 전단 복사 비율 |
| **offset** | 각 속성별 오프셋 값 |

---

### Physics Constraint

**스프링 기반 물리 시뮬레이션** - 본 체인에 관성·중력·바람 효과를 적용한다.

```
root (키프레임) → bone1 (물리) → bone2 (물리) → bone3 (물리)
                  ↑ 관성/중력/바람에 반응하여 자연스럽게 흔들림
```

| 속성 | 설명 |
|---|---|
| **bone** | 물리가 적용되는 본 체인 |
| **inertia** | 관성 (0=즉시 추종, 1=최대 지연) |
| **damping** | 감쇠 (높을수록 빨리 안정) |
| **strength** | 원래 포즈로 복귀하는 힘 |
| **gravity** | 중력 방향·크기 |
| **wind** | 바람 방향·크기 |
| **mix** | 물리 적용 비율 |

> Spine 4.2에서 도입. 머리카락, 망토, 꼬리, 장식품에 사용하면 수작업 키프레임 대비 작업량 대폭 감소

---

### Events

애니메이션 타임라인의 **특정 시간에 트리거**되는 명명된 이벤트.

```
Timeline:  0.0 ──── 0.3 ──── 0.6 ──── 1.0
Events:              🔊footstep    🔊footstep
                     💨dust_vfx    💨dust_vfx
```

| 속성 | 설명 |
|---|---|
| **name** | 이벤트 식별자 |
| **int / float / string** | 이벤트에 전달할 데이터 |
| **audio** | 연결된 사운드 파일 경로 (선택) |
| **volume / balance** | 오디오 볼륨·패닝 (선택) |

**`.mskl` 확장안:**
```json
{
  "events": {
    "footstep": { "int": 0, "float": 0, "string": "" },
    "attack_hit": { "int": 1, "float": 25.5, "string": "slash" }
  },
  "animations": {
    "walk": {
      "events": [
        { "time": 0.3, "name": "footstep", "int": 0 },
        { "time": 0.8, "name": "footstep", "int": 1 }
      ]
    }
  }
}
```

**런타임 콜백:**
```cpp
animationState->setEventListener([](int track, const Event& event) {
    if (event.name == "footstep") {
        audioEngine->play("footstep_" + std::to_string(event.intValue));
    }
});
```

---

### Draw Order Timeline

애니메이션 중 **슬롯의 렌더링 순서를 동적으로 변경**한다.

```
기본:     [body] [arm_back] [weapon] [arm_front]
프레임 5: [arm_back] [body] [weapon] [arm_front]  ← body가 뒤로
프레임 10: [body] [arm_back] [weapon] [arm_front]  ← 원래대로
```

> 캐릭터가 뒤돌아보는 동작에서 앞팔↔뒷팔 순서 교체, 무기 위치 변경 등에 필수

---

### Weighted Mesh & FFD

**Weighted Mesh**: 메시 버텍스가 여러 본에 가중치로 연결되어 부드러운 변형 생성.

```
vertex[0]: bone_a(0.7) + bone_b(0.3)  → 두 본 사이에서 부드럽게 변형
vertex[1]: bone_a(1.0)                → 단일 본에 완전 고정
vertex[2]: bone_b(0.5) + bone_c(0.5)  → 관절 부위 자연스러운 꺾임
```

**FFD (Free-Form Deformation)**: 메시 버텍스 위치를 직접 키프레임으로 조작.

```json
{
  "animations": {
    "talk": {
      "deform": {
        "face_mesh": [
          { "time": 0.0, "vertices": [0,0, 0,0, 0,0, ...] },
          { "time": 0.2, "vertices": [2,3, -1,2, 0,5, ...] },
          { "time": 0.4, "vertices": [0,0, 0,0, 0,0, ...] }
        ]
      }
    }
  }
}
```

> 얼굴 표정, 입모양, 눈 깜빡임 등 섬세한 변형에 활용

---

### Bounding Box & Point Attachment

**Bounding Box**: 본에 부착되는 다각형. 히트 판정·클릭 감지용.

```json
{
  "slots": [
    {
      "name": "hitbox_body",
      "bone": "spine",
      "attachment": "hitbox_body",
      "type": "boundingbox",
      "vertices": [-20, -30, 20, -30, 20, 30, -20, 30]
    }
  ]
}
```

**Point Attachment**: 본 위의 특정 좌표+각도. 파티클 생성점, 무기 장착점 등.

```json
{
  "slots": [
    {
      "name": "muzzle_point",
      "bone": "weapon",
      "attachment": "muzzle",
      "type": "point",
      "x": 50, "y": 0, "rotation": 0
    }
  ]
}
```

---

### Two Color Tinting

슬롯별 **라이트 컬러 + 다크 컬러** 이중 색상 시스템.

```
최종 색상 = 텍스처 * light_color + (1 - 텍스처) * dark_color
```

| 일반 Tint | Two Color Tint |
|---|---|
| 밝은 부분만 색조 변경 | 밝은/어두운 부분 독립 제어 |
| 전체 색상이 단조로움 | 풍부한 색감 표현 가능 |

> 예: 갑옷의 하이라이트=금색, 그림자=남색 → 일반 tint로는 불가능

**쉐이더 확장:**
```glsl
uniform vec4 u_light_color;
uniform vec4 u_dark_color;

vec4 final = texColor * u_light_color + (1.0 - texColor) * u_dark_color;
```

---

### Blend Mode

슬롯별 블렌드 모드 지정으로 다양한 합성 효과.

| 모드 | 수식 | 용도 |
|---|---|---|
| **Normal** | `src * alpha + dst * (1 - alpha)` | 기본 렌더링 |
| **Additive** | `src * alpha + dst` | 발광, 불꽃, 마법 이펙트 |
| **Multiply** | `src * dst` | 그림자, 어두운 오버레이 |
| **Screen** | `src + dst - src * dst` | 밝은 하이라이트, 빛 반사 |

```json
{
  "slots": [
    { "name": "body", "bone": "spine", "blend": "normal" },
    { "name": "glow_effect", "bone": "spine", "blend": "additive" },
    { "name": "shadow", "bone": "root", "blend": "multiply" }
  ]
}
```

---

### Sequence Attachment

슬롯에서 **프레임 단위 이미지 시퀀스**를 재생한다. 전통 프레임 애니메이션과 스켈레탈 혼합.

```
slot: "explosion"
  frame 0: explosion_00.png
  frame 1: explosion_01.png
  frame 2: explosion_02.png
  ...
```

| 속성 | 설명 |
|---|---|
| **region** | 이미지 시퀀스 베이스 이름 |
| **start / end** | 프레임 범위 |
| **mode** | hold (마지막 프레임 유지) / loop / pingpong / once |
| **fps** | 재생 속도 |

> 폭발, 연기, 물 튀김 등 스켈레탈로 표현하기 어려운 이펙트에 활용

---

### Linked Mesh

다른 메시의 **버텍스, 삼각형, 가중치를 참조**하는 메시. 스킨 시스템과 함께 사용.

```
기본 스킨:
  slot:body → mesh "body" (vertices, triangles, weights 정의)

스킨 "warrior":
  slot:body → linked_mesh "warrior_body" (parent: "body")
              → 부모 메시의 구조 공유, 텍스처만 다름
```

| 속성 | 설명 |
|---|---|
| **parent** | 참조할 원본 메시 이름 |
| **skin** | 원본 메시가 속한 스킨 (default skin이면 생략) |
| **deform** | `true`면 immediate parent 메시의 FFD timeline을 상속·공유하고, `false`면 linked child 자체의 deform identity를 사용 |

> 같은 본 구조에 외형만 다른 캐릭터를 만들 때, 메시 구조를 중복 정의할 필요 없음

---

### Inherit Timeline

애니메이션 중 **본의 상속 mode를 동적으로 변경**한다. Runtime과 export는 아래 5개 enum 값을 사용하고,
timeline sampling은 값 사이를 보간하지 않는 stepped-only다.

```
프레임 0: arm_bone → inherit = normal
프레임 5: arm_bone → inherit = noRotationOrReflection
```

| mode | 설명 |
|---|---|
| **normal** | 부모 transform을 정상적으로 상속 |
| **onlyTranslation** | 부모 translation만 상속 |
| **noRotationOrReflection** | 부모 rotation/reflection 영향을 제거 |
| **noScale** | 부모 scale 영향을 제거 |
| **noScaleOrReflection** | 부모 scale/reflection 영향을 제거 |

Runtime timeline과 read-only inspector는 이미 이 mode를 사용한다. MAR-184는 optional project overlay와
materialization/merge/export를 추가하고, MAR-185는 Add/Edit/Remove·selection·retime·scale·clipboard와 agent/MCP parity를 연결한다.

다섯 inherit mode의 runtime 지원과 MAR-161/162 authoring basis 지원 범위는 별개다. Rotation과 scale
gizmo는 `normal`과 `onlyTranslation`만 지원하며 나머지 세 mode는 잘못된 raw-parent basis를 적용하지
않고 gizmo 숨김과 viewport hint로 처리한다.

---

### Reverse Playback & Root Motion

**Reverse Playback**: `TrackEntry.reverse = true`로 애니메이션을 역방향으로 재생.

```cpp
auto* entry = animState->setAnimation(0, "door_open", false);
entry->reverse = true;  // door_open을 거꾸로 → door_close 효과
```

> 별도의 "닫기" 애니메이션 없이 "열기" 애니메이션을 역재생하여 리소스 절약

**Root Motion**: 루트 본의 이동량을 게임 월드 좌표에 반영.

```
애니메이션 "jump":
  root bone → x: 0→100, y: 0→50→0

런타임:
  delta = root.x - prevRoot.x
  character.worldX += delta   ← 애니메이션의 이동이 실제 월드에 반영
```

| 기능 | 설명 |
|---|---|
| **Delta Extraction** | 매 프레임 루트 본 이동 차이를 추출 |
| **Delta Compensation** | 점프 거리 등을 프로그래머가 런타임에서 조정 가능 |
| **세로/가로 분리** | X축(이동)은 게임에 반영, Y축(점프)은 애니메이션에 위임 등 |

> Spine 4.2 Unity 런타임에서 공식 Root Motion 지원. 애니메이터가 설계한 이동과 게임 로직 이동을 자연스럽게 통합

---

### AnimationState 콜백 시스템

AnimationState의 TrackEntry에 **6종 콜백**을 등록하여 애니메이션 상태 변화를 감지.

```
Timeline: ──[walk]──╳──[attack]──[idle]──
                     ↑
          walk: end, interrupt
          attack: start, complete
```

| 콜백 | 발생 시점 |
|---|---|
| **start** | 애니메이션이 처음 적용될 때 |
| **interrupt** | 다른 애니메이션으로 교체되어 밀려날 때 |
| **end** | 애니메이션이 더 이상 적용되지 않을 때 (mixing 종료 후) |
| **dispose** | TrackEntry가 해제될 때 |
| **complete** | 애니메이션이 한 바퀴 완료될 때 (루프마다 발생) |
| **event** | 타임라인에 배치된 이벤트 도달 시 |

```cpp
entry->setListener([](AnimationState* state, EventType type,
                      TrackEntry* entry, Event* event) {
    switch (type) {
        case EventType_Start:    onAnimStart(entry); break;
        case EventType_Complete: onAnimComplete(entry); break;
        case EventType_Event:    onAnimEvent(entry, event); break;
        case EventType_End:      onAnimEnd(entry); break;
    }
});
```

> 게임 로직 연동의 핵심. 공격 완료 → 대기 전환, 루프 카운트 추적, 이벤트 기반 사운드/이펙트 등

---

### Skin Constraints

Constraint를 **특정 스킨에 종속**시켜, 해당 스킨이 활성일 때만 적용.

```
Skin "mage":
  ├── attachments: mage_body, mage_hat, staff
  ├── bones: cape_bone_1, cape_bone_2      ← 스킨 전용 본
  └── constraints: cape_physics             ← 스킨 전용 Constraint

Skin "warrior":
  ├── attachments: warrior_body, helmet, sword
  └── constraints: (없음)
```

| 속성 | 설명 |
|---|---|
| **skin required** | true면 해당 Constraint가 속한 스킨이 활성일 때만 적용 |
| **skin bones** | 스킨에 포함된 추가 본 (스킨 교체 시 자동 활성/비활성) |

> 마법사 스킨에만 망토 물리가 적용되고, 전사 스킨으로 교체하면 자동으로 비활성화. `.mskl` 포맷에서 스킨 정의에 bones, constraints 배열 추가

**`.mskl` 확장안:**
```json
{
  "skins": {
    "mage": {
      "attachments": { ... },
      "bones": ["cape_bone_1", "cape_bone_2"],
      "constraints": ["cape_physics"]
    }
  }
}
```

---

### Setup Pose & SkeletonBounds

**Setup Pose**: 스켈레톤을 에디터에서 정의한 **초기 상태로 복원**.

```cpp
skeleton->setToSetupPose();        // 본 + 슬롯 + 드로우 오더 전부 리셋
skeleton->setBonesToSetupPose();   // 본만 리셋
skeleton->setSlotsToSetupPose();   // 슬롯 + 드로우 오더만 리셋
```

> 스킨 변경, 애니메이션 초기화, 디버깅 시 필수

**SkeletonBounds**: Bounding Box 어태치먼트를 활용한 **히트 판정 유틸리티**.

```cpp
skeletonBounds->update(skeleton, true);

if (skeletonBounds->containsPoint(mouseX, mouseY)) {
    auto* box = skeletonBounds->getBoundingBox();  // 어떤 바운딩 박스에 닿았는지
    // → 히트 판정 로직
}

if (skeletonBounds->intersectsSegment(x1, y1, x2, y2)) {
    // → 레이캐스트/광선 판정
}
```

| 메서드 | 설명 |
|---|---|
| **update()** | 현재 포즈 기준으로 모든 바운딩 박스 다각형 갱신 |
| **containsPoint()** | 점이 바운딩 박스 내부인지 판정 |
| **intersectsSegment()** | 선분이 바운딩 박스와 교차하는지 판정 |
| **getPolygon()** | 특정 바운딩 박스의 변환된 다각형 좌표 반환 |

---

### Binary Export Format

JSON 외에 **바이너리 포맷**으로 내보내기. 프로덕션 배포용.

| 비교 항목 | JSON | Binary |
|---|---|---|
| **파일 크기** | 크다 | 작다 (약 50% 이하) |
| **로딩 속도** | 느림 (파싱 비용) | 빠름 (직접 읽기) |
| **가독성** | 사람이 읽기 가능 | 불가능 |
| **디버깅** | 용이 | 어려움 |
| **확장자** | `.mskl` | `.mbin` |

> Marrow 프로젝트에서는 `.mskl` JSON으로 개발하고, 프로덕션 배포 시 같은 스켈레톤 문서를 `.mbin`으로 컴파일해서 로드한다.

**바이너리 인코딩 전략:**
- varint 가변 길이 정수 (작은 값은 1바이트)
- 문자열 테이블 (중복 문자열 참조 인덱스)
- float는 고정 4바이트
- boolean은 1비트 비트필드로 압축
- 파일 헤더는 `MBIN` 매직 + 버전으로 시작해서 런타임이 JSON 경로와 구분한다.

---

## 9. molga-engine 런타임 아키텍처

```
[Marrow Editor]
      ↓ export
  .mskl / .matl / .png (.mbin v2 binary 선택)
      ↓ import
[molga-engine]
  ├── AtlasLoader        → .matl 파싱, 텍스처 로드
  ├── SkeletonLoader     → .mskl 파싱, 본 트리 구성
  ├── SkeletonData       → 공유 가능한 정적 데이터 (본/슬롯/스킨/애니메이션, immutable parameter 정의)
  ├── Skeleton           → 인스턴스별 상태 (현재 포즈, 활성 스킨, 드로우 오더, direct/final parameter 값)
  ├── AnimationState     → 트랙 기반 애니메이션 재생, 믹싱, 큐, 콜백
  ├── ConstraintSolver   → IK / Path / Transform / Physics Constraint 해결
  ├── SkinManager        → 스킨 교체, 합성, 조건부 Constraint 관리
  ├── SkeletonBounds     → Bounding Box 기반 히트 판정
  ├── Skinning           → 본 행렬 계산 (월드 트랜스폼)
  └── Renderer           → GPU 스키닝 쉐이더, 동적 VBO, 블렌드 모드, Two Color
```

---

## 10. 개발 순서 (권장)

초기 렌더러/런타임 우선 단계, 에디터 아키텍처 리팩터링, 편집 P0와 파라미터 트랙은 완료됐다. 현재 실행 순서는 다음과 같다.

1. **완료: 편집 P0 (MAR-141~153)** - Setup Pose 읽기 전용화, Animation auto-key, 안정적 카메라/이동 기즈모, 도프시트 조작, 슬롯 타임라인, 애니메이션 CRUD와 E2E guardrail
2. **완료: 파라미터 트랙 (MAR-122~128)** - MAR-121의 런타임 기반을 MAR-122에 통합하고 정의·export·shape/deformer·ArtPath·expression/lip-sync·editor·agent surface를 검증
3. **진행 중: 편집 P1 (MAR-154~191)** - MAR-154 runtime explicit duration부터 MAR-159 hierarchy multi-selection, MAR-160 viewport multi-selection, MAR-161 parent-space rotation gizmo, MAR-162 signed local scale gizmo, behavior-preserving Task #28 핵심 경계 리팩터, MAR-163 single-vertex FFD, MAR-164 attachment-local multi-vertex FFD, MAR-165 shared transform snapping, MAR-166 FFD grid/magnetic snapping, MAR-167 synchronized scalar graph, MAR-168 graph key time/value editing, MAR-169 graphical shared Bezier handle editing, MAR-170 fixed curve preset·remembered default와 MAR-171 project-local automatic curve handle까지 완료됐다. 다음 MAR-172부터 loop 동기화·retime·preview, weight·constraint lifecycle, atomic file workflow·inherit, structured Problems, staged/atomic PSD 재임포트와 E2E를 순서대로 닫는다. MAR-192~210 qualification은 open 병렬 보류 backlog다.
4. **보류: 자체 리그 저작 재검토** - canonical `.marrow` authoring graph와 stable ID 전환 설계가 승인된 뒤에만 리그/메시 topology 저작을 시작

P1 시작 gate인 MAR-128 완료 checkpoint는 2026-07-16에, MAR-154 runtime duration과 MAR-155 editor duration checkpoint는 2026-07-17에, MAR-156 user preference, MAR-157 typed selection 및 MAR-158 selection migration checkpoint는 2026-07-18에, MAR-159 hierarchy multi-selection과 MAR-160 viewport multi-selection 및 MAR-161 parent-space rotation checkpoint는 2026-07-21에, MAR-162 signed local scale checkpoint는 2026-07-25에, Task #28 핵심 경계 리팩터, MAR-163 single-vertex FFD와 MAR-164 attachment-local multi-vertex FFD checkpoint는 2026-08-16에, MAR-165 shared transform snapping, MAR-166 FFD grid/magnetic snapping과 MAR-167 synchronized scalar graph checkpoint는 2026-08-20에, MAR-168 graph key time/value editing, MAR-169 graphical shared Bezier handle editing, MAR-170 fixed curve preset/remembered default와 MAR-171 project-local automatic curve handle checkpoint는 2026-08-30에 통과했다. 다음 제품 milestone은 MAR-172다. MAR-172~191은 각각 바로 앞 번호의 스토리에 의존하는 선형 dependency chain이다. 각 milestone checkpoint는 기능·검증 경계이며 자동 커밋 단위가 아니다. 38개 story의 title과 수직 scope는
[`editing-gap-analysis.md`](editing-gap-analysis.md)의 P1 표를 따른다.

> 새 포맷이나 평가 기능은 계속 런타임에서 먼저 검증한다. 편집 P0는 이미 구현된 런타임 위의 데이터 유실·직접 조작·타임라인 UX 갭을 먼저 닫은 예외적인 제품 완성도 단계였다. P1도 `.mskl` v1/`.mbin` v2/C ABI v1 compatibility를 유지한다.

---

## 11. 미결정 사항

- [x] `.matl` 텍스처 아틀라스 포맷 상세 설계
- [x] 본 계층 런타임 자료구조 설계
- [x] 편집 P0와 MAR-128 완료 checkpoint 뒤 MAR-154~191 선형 P1 마일스톤·타임라인 우선순위
- [ ] canonical `.marrow` authoring graph의 version/stable-ID/migration 설계
- [ ] 자체 리그·메시 topology 저작 재개 조건과 범위
