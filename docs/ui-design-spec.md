# Maroow Editor UI Design Specification

## 1. Overview

Maroow Editor는 Spine 4.2 호환 2D 스켈레톤 애니메이션 에디터다.
ImGui + GLFW + OpenGL 기반이며, 도킹 레이아웃으로 7개 패널을 구성한다.

**현재 창 크기**: GLFW 윈도우 (기본값 미지정, OS 기본)
**배경색**: `(0.07, 0.08, 0.10)` — 거의 검정에 가까운 다크 톤

---

## 2. Dock Layout — 전체 배치

```
┌──────────────────────────────────────────────────────────────────────┐
│                          Menu Bar                                    │
├─────────────────┬────────────────────────────────────────────────────┤
│                 │                                                    │
│   LEFT-TOP      │                                                    │
│   (28% width)   │              CENTER                                │
│                 │              Viewport                               │
│  ┌─Tab──────┐   │              (나머지 전체)                           │
│  │ Project  │   │                                                    │
│  │ Hierarchy│   │                                                    │
│  └──────────┘   │                                                    │
│─────────────────│                                                    │
│   LEFT-BOTTOM   │                                                    │
│   (48% of left) ├────────────────────────────────────────────────────┤
│                 │                                                    │
│  ┌─Tab──────┐   │              BOTTOM                                │
│  │ Runtime  │   │              Timeline                               │
│  │ Assets   │   │              (30% height)                          │
│  │Constraints│  │                                                    │
│  │Properties│   │                                                    │
│  └──────────┘   │                                                    │
└─────────────────┴────────────────────────────────────────────────────┘
```

### Split 비율
| Split | 방향 | 비율 | 결과 |
|-------|------|------|------|
| 1차 | Left | 0.28 | 좌측 28%, 나머지 72% |
| 2차 | Down | 0.30 | 하단 30% (Timeline), 상단 70% (Viewport) |
| 3차 | Down (좌측 내) | 0.48 | 좌측 하단 48% (Properties), 상단 52% (Project/Hierarchy) |

### 탭 그룹
| 도킹 위치 | 패널 (탭) |
|-----------|-----------|
| LEFT-TOP | Project, Hierarchy |
| LEFT-BOTTOM | Runtime Assets, Constraints, Properties |
| CENTER | Viewport |
| BOTTOM | Timeline |

---

## 3. Menu Bar

상단 고정 메뉴 바. 3개 메뉴 + 우측 상태 메시지.

### 3.1 File 메뉴
| 항목 | 단축키 | 동작 |
|------|--------|------|
| Reload Project | — | 프로젝트 재로드 |
| Quit | — | 앱 종료 |

### 3.2 Edit 메뉴
| 항목 | 단축키 | 동작 |
|------|--------|------|
| Undo | Ctrl+Z | 마지막 작업 되돌리기 |
| Redo | Ctrl+Shift+Z / Ctrl+Y | 되돌리기 취소 |

### 3.3 View 메뉴
| 항목 | 타입 | 동작 |
|------|------|------|
| Onion Skinning | Checkbox | 오니온 스킨 토글 |
| Performance HUD | Checkbox | 성능 HUD 토글 |
| — | Separator | — |
| Bone Hierarchy | Checkbox | 본 계층 디버그 오버레이 |
| IK Constraints | Checkbox | IK 제약 오버레이 |
| Path Constraints | Checkbox | 패스 제약 오버레이 |
| Physics Constraints | Checkbox | 물리 제약 오버레이 |
| Mesh Wireframes | Checkbox | 메시 와이어프레임 오버레이 |
| Bounding Boxes | Checkbox | 바운딩 박스 오버레이 |

### 3.4 Status Message
- 위치: 메뉴 바 우측
- 내용: 최근 작업 상태 텍스트 (예: "Playing idle", "Selected bone: root")

---

## 4. Project 패널

위치: LEFT-TOP 탭 그룹

### 4.1 Action Bar (상단)
```
[ Reload ] [ Save Project ] [ Export Runtime Assets ] ☑ Export .mbin
```
| 요소 | 타입 | 동작 |
|------|------|------|
| Reload | Button | 프로젝트 JSON 재로드 |
| Save Project | Button | .marrow 프로젝트 파일 저장 |
| Export Runtime Assets | Button | .mskl, .matl 등 런타임 에셋 내보내기 |
| Export .mbin | Checkbox | 바이너리 포맷도 함께 내보내기 |

### 4.2 Project Info (정보 표시)
| 요소 | 내용 |
|------|------|
| Name | 프로젝트 이름 |
| Animation | 현재 선택된 애니메이션 |
| Skeleton | 본/슬롯/애니메이션/스킨/이벤트 수 요약 |
| Notes | 프로젝트 메모 (TextWrapped) |
| Error | 로드 에러 시 빨간 텍스트 |

### 4.3 Collapsible Sections (접이식)
| 섹션 | 기본상태 | 내용 |
|------|----------|------|
| Animation Clips | Open | 애니메이션 목록 + 각 duration |
| Skins | Open | 스킨 이름 목록 |
| Constraints | Open | IK/Path/Transform/Physics 제약 목록 |
| Atlases | Open | 아틀라스 이름 + 해상도 |

---

## 5. Hierarchy 패널

위치: LEFT-TOP 탭 그룹 (Project와 탭 공유)

### 5.1 Header
```
Bones: 42
──────────────
```

### 5.2 Bone Tree
- 타입: TreeNodeEx 재귀 트리
- 기본 펼침: DefaultOpen, SpanAllColumns
- 클릭: 해당 본 선택 (뷰포트 + 인스펙터 연동)
- 표시: 본 이름, 부모-자식 계층 구조

```
▼ root
  ▼ hip
    ▼ left-upper-leg
      ▼ left-lower-leg
        ▶ left-foot
    ▼ right-upper-leg
      ...
  ▼ torso
    ▼ neck
      ▶ head
    ▼ left-arm
      ...
```

---

## 6. Viewport 패널

위치: CENTER (메인 영역)

### 6.1 Header Bar
```
Setup pose preview   LMB select  RMB drag pan  Wheel zoom
──────────────────────────────────────────────────────────
```
또는 (애니메이션 선택 시):
```
Animation preview / idle   LMB select  RMB drag pan  Wheel zoom
0.340s / 1.200s
──────────────────────────────────────────────────────────
```

### 6.2 Settings Panels (접이식 — 기본 접힘)

캔버스 위에 위치하며, 펼치면 캔버스 높이가 줄어든다.

#### 6.2.1 Onion Skin
| 요소 | 타입 | 범위 | 동작 |
|------|------|------|------|
| Enabled | Checkbox | — | 오니온 스킨 활성화 |
| Mode | Combo | Frame / Keyframe | 고스트 모드 |
| Anchor To Frame 0 | Checkbox | — | 프레임 0 기준 앵커 (Keyframe 모드 시 비활성) |
| Before Ghosts | SliderInt | 0–6 | 이전 고스트 수 |
| After Ghosts | SliderInt | 0–6 | 이후 고스트 수 |
| Frame Step / Keyframe Stride | SliderInt | 1–12 | 고스트 간격 |

#### 6.2.2 Debug Overlay
| 요소 | 타입 | 동작 |
|------|------|------|
| Bones | Checkbox | 본 와이어프레임 표시 |
| IK Constraints | Checkbox | IK 제약 시각화 |
| Path Constraints | Checkbox | 패스 제약 시각화 |
| Physics Constraints | Checkbox | 물리 제약 시각화 |
| Mesh Wireframes | Checkbox | 메시 와이어프레임 |
| Bounding Boxes | Checkbox | 바운딩 박스 |

#### 6.2.3 Performance HUD
| 요소 | 타입 | 동작 |
|------|------|------|
| Enabled | Checkbox | 성능 HUD 오버레이 표시 |

#### 6.2.4 Weight Paint
| 요소 | 타입 | 범위 | 동작 |
|------|------|------|------|
| Enable Tool | Checkbox | — | 웨이트 페인트 도구 활성화 |
| Mode | Combo | Paint / Erase / Smooth | 브러시 모드 |
| Radius | SliderFloat | 8–160 px | 브러시 반경 |
| Strength | SliderFloat | 0.05–1.0 | 브러시 강도 |
| Show Heat Map | Checkbox | — | 히트맵 오버레이 표시 |
| (info) | Text | — | 현재 메시/본 정보 표시 |

### 6.3 Viewport Canvas
- 타입: OpenGL Framebuffer → ImGui::Image
- UV: `(0.0, 1.0)` → `(1.0, 0.0)` (Y-flip)
- 크기: `GetContentRegionAvail()` — 남은 공간 전체

#### Mouse Interaction
| 입력 | 동작 |
|------|------|
| LMB Click | 본 선택 (pick_bone_at_position) |
| LMB Drag | Weight Paint 브러시 (도구 활성 시) |
| RMB Drag | 뷰포트 Pan |
| Mouse Wheel | 뷰포트 Zoom |

#### Render Layers (렌더링 순서)
1. Background Grid (와이어프레임)
2. Onion Skin Ghosts (먼 것 → 가까운 것)
   - Before: cool blue `(0.38, 0.67, 1.0)` 반투명
   - After: warm red `(1.0, 0.55, 0.40)` 반투명
3. Main Character (텍스처 렌더링)
4. Debug Overlay (본, 제약, 와이어프레임)
5. Viewport Annotations (선택 하이라이트, 라벨)
6. Weight Paint Heatmap (활성 시)

---

## 7. Timeline 패널

위치: BOTTOM (하단 30%)

### 7.1 Animation Selector
```
Animation: [idle          ▼]
```
| 요소 | 타입 | 동작 |
|------|------|------|
| Animation | Combo | 애니메이션 클립 선택 |

### 7.2 Preview Options
```
☑ Queue Next Clip   ☐ Reverse
```
| 요소 | 타입 | 동작 |
|------|------|------|
| Queue Next Clip | Checkbox | 클립 순차 재생 활성화 |
| Reverse | Checkbox | 역방향 재생 |

#### Queue Settings (Queue 활성 시 나타남)
| 요소 | 타입 | 동작 |
|------|------|------|
| Queued Animation | Combo | 다음 재생할 애니메이션 |
| Queue Delay | DragScalar | 대기 시간 (초) |
| Override Mix Duration | Checkbox | 커스텀 믹스 시간 사용 |
| Mix Duration | DragScalar | 블렌드 시간 (초, 조건부) |

### 7.3 Playback Controls
```
[ Play ] [ Reset ]  ☐ Loop  [ Prev Key ] [ Next Key ]
```
| 요소 | 타입 | 동작 |
|------|------|------|
| Play / Pause | Button | 재생/일시정지 토글 |
| Reset | Button | 타임라인 0으로 리셋 |
| Loop | Checkbox | 루프 재생 |
| Prev Key | Button | 이전 키프레임으로 이동 |
| Next Key | Button | 다음 키프레임으로 이동 |

### 7.4 Time Scrubber
```
Time: ├════════════●══════════════════┤ 0.340s
```
| 요소 | 타입 | 범위 | 동작 |
|------|------|------|------|
| Time | SliderScalar (double) | 0.0 – duration | 타임라인 스크럽 |

### 7.5 Info Bar
```
Preview span: 1.200s   Keyed tracks: 14   Root motion total: (0.00, 0.00)
```

### 7.6 Track Table
2-컬럼 테이블. 스크롤 가능, 높이 180–420px (트랙 수에 따라 동적).

| 컬럼 | 너비 | 내용 |
|------|------|------|
| Track | 260px 고정 | 트랙 이름 + 키 수 (Selectable) |
| Keys | 나머지 전체 | 타임라인 레인 (커스텀 드로우) |

#### Timeline Lane (커스텀 렌더링)
- 배경: 반투명 바
- 키프레임: 다이아몬드 마커
- 플레이헤드: 수직 빨간 라인
- Hover: 툴팁 (트랙 이름, 키 수)
- Click: 해당 시간으로 스크럽

### 7.7 Key Editors (트랙 선택 시 나타남)

트랙 타입에 따라 4종류의 키 에디터 중 하나가 표시:

#### 7.7.1 Transform Key Editor
| 요소 | 타입 | 동작 |
|------|------|------|
| Add Key At Playhead | Button | 현재 시간에 키프레임 추가 |
| (스크롤 영역 250px) | — | — |
| Key N @ Time | CollapsingHeader | 각 키프레임 |
| Remove Key | Button | 키프레임 삭제 |
| Time | DragScalar | 키 시간 편집 |
| Angle | DragScalar | 회전값 |
| X, Y | DragScalar | 이동값 |
| Scale X, Scale Y | DragScalar | 스케일값 |

#### 7.7.2 Draw Order Key Editor
| 요소 | 타입 | 동작 |
|------|------|------|
| Add Key At Playhead | Button | 키프레임 추가 |
| (스크롤 영역 280px) | — | — |
| Key N @ Time | CollapsingHeader | 각 키프레임 |
| Remove Key | Button | 키프레임 삭제 |
| Time | DragScalar | 키 시간 편집 |
| Use Current Preview Order | Button | 현재 프리뷰 순서 적용 |
| Up / Down | Button (슬롯별) | 드로우 오더 변경 |

#### 7.7.3 Event Key Editor
| 요소 | 타입 | 동작 |
|------|------|------|
| Add Key At Playhead | Button | 키프레임 추가 |
| (스크롤 영역 340px) | — | — |
| Key N @ Time / EventName | CollapsingHeader | 각 키프레임 |
| Remove Key | Button | 키프레임 삭제 |
| Time | DragScalar | 키 시간 편집 |
| Event selection | Selectable 리스트 | 이벤트 정의 선택 |
| Float value | Checkbox + DragScalar | 실수 값 토글 + 편집 |
| Int value | Checkbox + InputInt | 정수 값 토글 + 편집 |
| String value | Checkbox + InputText | 문자열 토글 + 편집 |

#### 7.7.4 Mesh Deform Key Editor
| 요소 | 타입 | 동작 |
|------|------|------|
| Add Key At Playhead | Button | 키프레임 추가 |
| (스크롤 영역 300px) | — | — |
| Key N @ Time | CollapsingHeader | 각 키프레임 |
| Remove Key | Button | 키프레임 삭제 |
| Time | DragScalar | 키 시간 편집 |
| Vertex N Offset X | DragScalar (버텍스별) | X 변형 오프셋 |
| Vertex N Offset Y | DragScalar (버텍스별) | Y 변형 오프셋 |

---

## 8. Properties (Inspector) 패널

위치: LEFT-BOTTOM 탭 그룹

### 8.1 Viewport State Info
```
Viewport pan: 0.0, 0.0
Viewport zoom: 1.00
Timeline: idle @ 0.340s / 1.200s
──────────────────────────────────
```

### 8.2 Bones 섹션 (CollapsingHeader, DefaultOpen)

#### Bone List
- 타입: 스크롤 가능 목록 (140px 높이)
- 각 항목: `bone_name <- parent_name` (Selectable)
- 클릭: 본 선택

#### Selected Bone Details (본 선택 시)
| 요소 | 타입 | 내용 |
|------|------|------|
| Selected | Text | 본 이름 |
| Parent | Text | 부모 본 이름 |
| Children | Text | 자식 본 수 |
| Slots | Text | 연결된 슬롯 이름 |
| Active in preview | Text | 프리뷰 활성 상태 |

#### Setup Pose (읽기 전용)
| 필드 | 내용 |
|------|------|
| Translate | (x, y) |
| Rotation | 각도 |
| Scale | (sx, sy) |
| Shear | (sx, sy) |
| Inherit | 상속 모드 |

#### Local Pose (편집 가능 — 실시간 뷰포트 반영)
| 요소 | 타입 | 동작 |
|------|------|------|
| Translate | DragFloat2 | 본 이동 편집 |
| Rotation | DragFloat | 본 회전 편집 |
| Scale | DragFloat2 | 본 스케일 편집 |
| Shear | DragFloat2 | 본 전단 편집 |

#### World Pose (읽기 전용)
| 필드 | 내용 |
|------|------|
| World transform | a, b, c, d, tx, ty 매트릭스 |

### 8.3 Slots 섹션 (CollapsingHeader, DefaultOpen)

#### Slot List
- 타입: 스크롤 가능 목록 (130px 높이)
- 각 항목: 슬롯 이름 (Selectable)

#### Selected Slot Details (슬롯 선택 시)
| 요소 | 타입 | 내용 |
|------|------|------|
| Selected slot | Text | 슬롯 이름 |
| Bone | Text | 연결 본 이름 |
| Draw order | Text | N / Total |
| Blend mode | Text | Normal/Additive/Multiply/Screen |
| Setup attachment | Text | 기본 어태치먼트 |
| Preview attachment | Text | 현재 프리뷰 어태치먼트 |
| Attachment source skin | Text | 어태치먼트 원본 스킨 |
| Preview override | Text | 오버라이드 상태 |
| Light color | ColorEdit4 | RGBA 색상 편집 (AlphaBar) |
| Dark tint | ColorEdit4 | RGBA 다크 틴트 편집 (조건부) |

### 8.4 Attachments 섹션 (CollapsingHeader, DefaultOpen)

#### Attachment List
- 타입: 스크롤 가능 목록 (130px 높이)
- 각 항목: 어태치먼트 이름 (Selectable)

#### Action Buttons (어태치먼트 선택 시)
| 요소 | 타입 | 동작 |
|------|------|------|
| Apply To Preview Slot | Button | 선택 어태치먼트를 프리뷰 슬롯에 적용 |
| Reset Slot To Skin Preview | Button | 슬롯을 스킨 기본값으로 리셋 |

#### Attachment Details (타입별)
| 타입 | 표시 정보 |
|------|-----------|
| Region | 이름, 스킨, 종류 |
| Mesh | 버텍스 수, 삼각형 수 + Mesh Weights (TreeNode) |
| Linked Mesh | 원본 메시 참조 정보 |
| Point | 회전값 |
| Bounding Box | 버텍스 수 |
| Clipping | 슬롯, 버텍스 수 |
| Path | 제어점 수 |
| Sequence | 이름, 프레임 수 |

### 8.5 Skin Preview 섹션 (CollapsingHeader, DefaultOpen)
```
Active composition: default + custom_skin
  • default (base skin)
  ☑ custom_skin   12 slot attachments, 3 linked meshes
  ☐ another_skin  8 slot attachments
```
| 요소 | 타입 | 동작 |
|------|------|------|
| Active composition | Text | 현재 조합된 스킨 표시 |
| Base skin | BulletText | 기본 스킨 (변경 불가) |
| Additional skins | Checkbox (각 스킨) | 추가 스킨 토글 |
| Skin info | TextDisabled | 슬롯 어태치먼트 / 링크드 메시 수 |

---

## 9. Runtime Assets 패널

위치: LEFT-BOTTOM 탭 그룹 (Properties와 탭 공유)

### 9.1 Skeleton Summary (프로젝트 로드 시)
| 요소 | 내용 |
|------|------|
| Skeleton | 파일 이름 |
| Bounds | 너비 x 높이 |
| Bones | 본 개수 |
| Slots | 슬롯 개수 |
| Skins | 스킨 개수 |
| Animations | 애니메이션 개수 |
| Atlases | 아틀라스 목록 |

### 9.2 Empty State
프로젝트 미로드 시: "Load a valid project..."

---

## 10. Constraints 패널

위치: LEFT-BOTTOM 탭 그룹 (Properties와 탭 공유)

### 10.1 Tab Bar (4개 탭)

#### 10.1.1 IK Tab
| 요소 | 타입 | 동작 |
|------|------|------|
| Constraint List | Selectable (스크롤 120px) | IK 제약 목록 |
| Add IK Constraint | Button | 새 IK 제약 추가 |
| Name | Text | 제약 이름 |
| Display index | Text | 표시 인덱스 |
| Bone mode | RadioButton × 2 | 1 Bone / 2 Bones |
| Mix | SliderScalar | 믹스 강도 (0–1) |

#### 10.1.2 Path Tab
| 요소 | 타입 | 동작 |
|------|------|------|
| Constraint List | Selectable (스크롤 120px) | Path 제약 목록 |
| Add Path Constraint | Button | 새 Path 제약 추가 |
| Add Chain Bone | Button | 체인에 본 추가 |
| Remove Chain Bone | Button | 체인에서 본 제거 |
| Position Mix | SliderScalar | 위치 믹스 |
| Spacing Mix | SliderScalar | 간격 믹스 |
| Rotate Mix | SliderScalar | 회전 믹스 |
| Translate Mix | SliderScalar | 이동 믹스 |

#### 10.1.3 Transform Tab
| 요소 | 타입 | 동작 |
|------|------|------|
| Constraint List | Selectable (스크롤 120px) | Transform 제약 목록 |
| Add Transform Constraint | Button | 새 Transform 제약 추가 |
| Add Target Bone | Button | 타겟 본 추가 |
| Remove Target Bone | Button | 타겟 본 제거 |
| Mix sliders | SliderScalar (복수) | 각 축 믹스 강도 |

#### 10.1.4 Physics Tab
| 요소 | 타입 | 동작 |
|------|------|------|
| Constraint List | Selectable (스크롤 120px) | Physics 제약 목록 |
| Add Physics Constraint | Button | 새 Physics 제약 추가 |
| Add Chain Bone | Button | 체인에 본 추가 |
| Remove Chain Bone | Button | 체인에서 본 제거 |
| Physics sliders | SliderScalar (복수) | 물리 파라미터 조절 |

---

## 11. Keyboard Shortcuts

| 단축키 | 동작 | 컨텍스트 |
|--------|------|----------|
| Ctrl+Z | Undo | 전역 |
| Ctrl+Shift+Z | Redo | 전역 |
| Ctrl+Y | Redo (대체) | 전역 |

---

## 12. UI Element 전체 통계

| 카테고리 | 수량 |
|----------|------|
| Window (패널) | 7 |
| Menu | 3 (File, Edit, View) |
| MenuItem | 11 |
| Button | 24 |
| Checkbox | 23 |
| Combo (Dropdown) | 5 |
| SliderFloat | 2 |
| SliderInt | 3 |
| SliderScalar | 9+ |
| DragFloat / DragFloat2 | 5 |
| DragScalar | 8+ |
| DragInt | 0 |
| InputText | 1 |
| InputInt | 1 |
| ColorEdit4 | 2 |
| RadioButton | 2 |
| Selectable (리스트 항목) | 20+ |
| CollapsingHeader | 12+ |
| TreeNodeEx | 8+ |
| BeginChild (스크롤 영역) | 12 |
| BeginTable | 1 |
| TabBar (탭 그룹) | 1 (Constraints, 4 탭) |
| InvisibleButton | 2 (viewport_canvas, timeline_lane) |
| Custom Draw (DrawList) | 3 (timeline lane, viewport annotations, debug overlay) |
| ProgressBar | 0 |

---

## 13. 현재 UI 문제점 및 개선 대상

### 13.1 레이아웃
- [ ] 뷰포트 설정 패널 4개가 캔버스와 같은 Window에 있어 펼치면 캔버스 높이 감소
- [ ] 좌측 패널 5개가 2개 탭 그룹에 분산 — 탭 전환 빈번
- [ ] 키보드 단축키 부족 (Undo/Redo 외 없음)

### 13.2 뷰포트
- [ ] Zoom-to-Fit / Frame Skeleton 기능 없음
- [ ] 뷰포트 내 툴바 (Pan/Zoom/Select 모드 전환) 없음
- [ ] 그리드 설정 UI 없음
- [ ] 카메라 리셋 버튼 없음

### 13.3 타임라인
- [ ] 드래그로 키프레임 이동 불가 (DragScalar로만 편집)
- [ ] 다중 키프레임 선택 불가
- [ ] 복사/붙여넣기 불가
- [ ] 재생 속도 조절 UI 없음

### 13.4 인스펙터
- [ ] Setup Pose와 Local Pose 시각적 구분 약함
- [ ] 슬롯 드로우 오더 시각적 편집 불가
- [ ] 어태치먼트 미리보기 이미지 없음

### 13.5 일반
- [ ] 최근 프로젝트 목록 없음
- [ ] 테마/색상 설정 없음
- [ ] 도움말/About 대화상자 없음
- [ ] 프로젝트 생성 마법사 없음

---

## 14. 색상 팔레트

### 14.1 현재 (ImGui 기본 Dark)

| 용도 | 색상값 | 설명 |
|------|--------|------|
| 배경 | `(0.07, 0.08, 0.10)` | 앱 배경 (거의 검정) |
| Onion Before | `(0.38, 0.67, 1.0)` | 이전 고스트 (cool blue) |
| Onion After | `(1.0, 0.55, 0.40)` | 이후 고스트 (warm red) |
| ImGui 테마 | ImGui 기본 Dark | 패널/위젯 색상 |

### 14.2 Target Theme — Charcoal Studio

Spine Editor / After Effects 계열. 파란 기운의 차콜 톤.
캐릭터 아트와 간섭 없는 중립 배경 + 블루 액센트로 전문적 인상.

#### Base Palette

| 용도 | Hex | RGB | 비고 |
|------|-----|-----|------|
| App Background | `#1A1D23` | `(26, 29, 35)` | 약간 푸른 기운의 차콜 |
| Panel Background | `#22262E` | `(34, 38, 46)` | 패널 내부 |
| Panel Header | `#2A2F3A` | `(42, 47, 58)` | CollapsingHeader, TabBar |
| Widget Background | `#333A47` | `(51, 58, 71)` | Button, Slider 배경 |
| Widget Hover | `#3D4556` | `(61, 69, 86)` | 마우스 오버 |
| Widget Active | `#4A7BF7` | `(74, 123, 247)` | 클릭/활성 — 포인트 블루 |
| Selection | `#4A7BF7` | `(74, 123, 247)` | 선택 항목 하이라이트 |
| Selection Dim | `#2A4080` | `(42, 64, 128)` | 비활성 선택 |

#### Text

| 용도 | Hex | RGB | 비고 |
|------|-----|-----|------|
| Text Primary | `#E0E4EC` | `(224, 228, 236)` | 일반 텍스트 |
| Text Secondary | `#8891A5` | `(136, 145, 165)` | TextDisabled, 보조 정보 |
| Text Accent | `#7EB8FF` | `(126, 184, 255)` | 링크, 강조 텍스트 |

#### Border & Separator

| 용도 | Hex | RGB | 비고 |
|------|-----|-----|------|
| Border | `#3A4050` | `(58, 64, 80)` | 패널 경계선 |
| Separator | `#2E3440` | `(46, 52, 64)` | 구분선 |
| Scrollbar Track | `#3A4050` | `(58, 64, 80)` | 스크롤바 트랙 |
| Scrollbar Grab | `#505868` | `(80, 88, 104)` | 스크롤바 손잡이 |

#### Viewport

| 용도 | Hex | RGB | 비고 |
|------|-----|-----|------|
| Viewport BG | `#282C34` | `(40, 44, 52)` | 뷰포트 캔버스 배경 |
| Grid Line | `#333840` | `(51, 56, 64)` | 뷰포트 그리드 |

#### Semantic Colors

| 용도 | Hex | RGB | 비고 |
|------|-----|-----|------|
| Success | `#5CB85C` | `(92, 184, 92)` | 성공/활성 상태 |
| Warning | `#F0AD4E` | `(240, 173, 78)` | 경고 |
| Error | `#D9534F` | `(217, 83, 79)` | 에러/삭제 |
| Play | `#5CB85C` | `(92, 184, 92)` | Play 버튼 |

#### Bone & Skeleton

| 용도 | Hex | RGB | 비고 |
|------|-----|-----|------|
| Bone Selected | `#FFB347` | `(255, 179, 71)` | 선택된 본 |
| Bone Hover | `#FFD700` | `(255, 215, 0)` | 호버 본 |
| Onion Before | `#6AABFF` | `(106, 171, 255)` | 이전 고스트 (cool blue) |
| Onion After | `#FF8C69` | `(255, 140, 105)` | 이후 고스트 (warm red) |

#### Weight Paint

| 용도 | Hex | RGB | 비고 |
|------|-----|-----|------|
| Weight Heat Low | `#3366FF` | `(51, 102, 255)` | 웨이트 0 (파랑) |
| Weight Heat High | `#FF3333` | `(255, 51, 51)` | 웨이트 1 (빨강) |

#### Timeline

| 용도 | Hex | RGB | 비고 |
|------|-----|-----|------|
| Playhead | `#FF4444` | `(255, 68, 68)` | 플레이헤드 라인 |
| Keyframe Marker | `#E0E4EC` | `(224, 228, 236)` | 키프레임 다이아몬드 |
| Track BG Even | `#22262E` | `(34, 38, 46)` | 트랙 배경 (짝수) |
| Track BG Odd | `#262B35` | `(38, 43, 53)` | 트랙 배경 (홀수) |

---

## 15. 렌더링 파이프라인 (뷰포트)

```
Skeleton
  → prepare_setup_pose_scene()
  → PreparedScene
  → build_render_command_list()
  → RenderCommandList
  → ViewportRenderer::submit_command_list()
  → glDrawElements()
  → Framebuffer (color_texture)
  → ImGui::Image()
```

**지원 기능**:
- Region attachment (사각형 이미지)
- GPU-skinned mesh (스키닝 메시)
- Blend modes: Normal, Additive, Multiply, Screen
- Stencil clipping (클리핑 어태치먼트)
- Atlas texture loading (PNG → GL Texture)
