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

---

## 3. 기술 스택

| 항목 | 결정 | 이유 |
|---|---|---|
| 에디터 언어 | **C++** | molga-engine과 동일 스택, 학습 비용 없음 |
| 에디터 UI | **Dear ImGui** | Mac 지원, MIT 라이센스, C++ 친화적 |
| 렌더링 | **sokol_app + sokol_gfx** | 단일 셰이더/렌더링 코드로 Metal(macOS)과 OpenGL(Linux) 지원 |
| 스키닝 방식 | **GPU 스키닝 (Vertex Shader)** | 성능 우위, 쉐이더 커스텀 가능 |

---

## 4. 파일 포맷

| 확장자 | 용도 | 형식 |
|---|---|---|
| `.marrow` | 에디터 프로젝트 파일 | JSON (초기), 추후 바이너리 컴파일러 추가 |
| `.mskl` | 본 구조 + 애니메이션 데이터 (runtime import용) | JSON → 추후 바이너리 |
| `.mbin` | 프로덕션용 런타임 스켈레톤 바이너리 | Compact binary |
| `.matl` | 텍스처 아틀라스 메타데이터 | JSON |
| `.png` | 실제 텍스처 아틀라스 | 이미지 |

> 개발 초기에는 JSON으로 시작, 디버깅 안정화 후 바이너리 컴파일러 추가
> `.mskl` / `.matl` 루트에는 정수 `version` 필드가 포함되며, 현재 런타임은 `version: 1`만 로드한다.
> `.matl.atlas.premultiplied_alpha` 불리언으로 straight alpha와 PMA 텍스처를 구분하며, 렌더러는 이 값을 기준으로 셰이더/블렌드 경로를 전환한다.
> `.matl.regions[].rotate`는 선택적인 아틀라스 내 회전 각도(도 단위)를 보존하며, Spine `.atlas` 멀티페이지 import는 페이지당 하나의 `.matl` 파일로 변환한다.
> `.mbin`은 검증된 런타임 문서를 그대로 보존하면서 회전/이동 키프레임에 대해 16-bit 시간/채널 인덱스 + 양자화 payload를 추가 저장해, 내보내기 시 키 감소와 런타임 quantized playback을 지원한다.

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
- [ ] **Inherit Timeline** - 애니메이션 중 본의 상속 속성 동적 변경 (회전/스케일/반전 상속 on/off)
- [ ] **Shortest Rotation** - 회전 보간 시 최단 경로 선택 (360도 이상 회전 방지)

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
- [ ] **Binary Export** - JSON 외 바이너리 포맷 내보내기 (용량↓ 로딩속도↑)

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
| **deform** | false면 부모 메시의 FFD만 따름, true면 독립 FFD 가능 |

> 같은 본 구조에 외형만 다른 캐릭터를 만들 때, 메시 구조를 중복 정의할 필요 없음

---

### Inherit Timeline

애니메이션 중 **본의 상속 속성을 동적으로 변경**한다. Spine 4.2에서 `spInheritTimeline`으로 추가.

```
프레임 0: arm_bone → inherit rotation = true  (부모 회전 따라감)
프레임 5: arm_bone → inherit rotation = false (독립 회전)
```

| 제어 가능한 상속 속성 | 설명 |
|---|---|
| **rotation** | 부모 본의 회전 상속 여부 |
| **scale** | 부모 본의 스케일 상속 여부 |
| **reflectX / reflectY** | 부모 본의 반전 상속 여부 |

> 캐릭터가 방향 전환할 때 특정 본만 반전에서 제외하거나, 무기가 항상 월드 기준 회전을 유지하도록 할 때 사용

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
  .mskl / .matl / .png (.mbin 바이너리 추후)
      ↓ import
[molga-engine]
  ├── AtlasLoader        → .matl 파싱, 텍스처 로드
  ├── SkeletonLoader     → .mskl 파싱, 본 트리 구성
  ├── SkeletonData       → 공유 가능한 정적 스켈레톤 데이터 (본/슬롯/스킨/애니메이션)
  ├── Skeleton           → 인스턴스별 상태 (현재 포즈, 활성 스킨, 드로우 오더)
  ├── AnimationState     → 트랙 기반 애니메이션 재생, 믹싱, 큐, 콜백
  ├── ConstraintSolver   → IK / Path / Transform / Physics Constraint 해결
  ├── SkinManager        → 스킨 교체, 합성, 조건부 Constraint 관리
  ├── SkeletonBounds     → Bounding Box 기반 히트 판정
  ├── Skinning           → 본 행렬 계산 (월드 트랜스폼)
  └── Renderer           → GPU 스키닝 쉐이더, 동적 VBO, 블렌드 모드, Two Color
```

---

## 10. 개발 순서 (권장)

1. **molga-engine 렌더러 확장** - 아틀라스 + 동적 메시 지원
2. **런타임 먼저** - `.mskl` 하드코딩으로 파싱/렌더링 테스트
3. **Marrow 에디터** - 런타임 동작 확인 후 시작

> 에디터 먼저 만들면 결과 확인이 안 돼서 동기부여 저하 위험

---

## 11. 미결정 사항

- [ ] `.matl` 텍스처 아틀라스 포맷 상세 설계
- [ ] 본 계층 런타임 자료구조 설계
- [ ] 에디터 타임라인 UI 설계
- [ ] 마일스톤 단계별 계획
