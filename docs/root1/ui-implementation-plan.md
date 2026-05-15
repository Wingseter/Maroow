# Maroow Editor UI 구현 계획

Stitch 디자인을 기반으로 한 ImGui 구현 계획.
디자인 100% 재현이 아닌, **기능과 사용자 편의성** 우선.

---

## 원칙

1. **기능 우선**: 시각적 장식보다 실사용 개선에 집중
2. **ImGui 자연스러움**: ImGui에서 어색한 구현(Glassmorphism, Gradient)은 과감히 단순화
3. **점진적 적용**: 한 Phase가 빌드+테스트 통과해야 다음으로 진행
4. **기존 테스트 유지**: 27개 유닛테스트 + headless smoke 5프레임 항상 통과

---

## Phase 1: Charcoal Studio 테마 적용

**목표**: 전체 색상 팔레트 + 폰트 + 기본 스타일 교체
**수정 파일**: `shell_main.cpp` (apply_editor_theme 함수)
**영향 범위**: 시각적 변경만, 레이아웃/기능 변화 없음

### 1.1 apply_editor_theme() 전면 교체

현재 코드 (`shell_main.cpp:356-385`):
```cpp
void apply_editor_theme() {
    ImGui::StyleColorsDark();
    // ... 오렌지 계열 액센트
    style.WindowRounding = 6.0f;
}
```

변경:
```cpp
void apply_editor_theme() {
    ImGui::StyleColorsDark();
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    // ── Surface Hierarchy ──
    c[ImGuiCol_WindowBg]        = ImVec4(0.102f, 0.114f, 0.137f, 1.0f); // #1A1D23
    c[ImGuiCol_ChildBg]         = ImVec4(0.133f, 0.149f, 0.180f, 1.0f); // #22262E
    c[ImGuiCol_PopupBg]         = ImVec4(0.133f, 0.149f, 0.180f, 0.95f);
    c[ImGuiCol_MenuBarBg]       = ImVec4(0.102f, 0.114f, 0.137f, 1.0f); // #1A1D23
    c[ImGuiCol_DockingEmptyBg]  = ImVec4(0.063f, 0.071f, 0.090f, 1.0f); // #101319

    // ── Header (CollapsingHeader, Tab) ──
    c[ImGuiCol_Header]          = ImVec4(0.165f, 0.184f, 0.227f, 1.0f); // #2A2F3A
    c[ImGuiCol_HeaderHovered]   = ImVec4(0.200f, 0.224f, 0.275f, 1.0f); // #333947
    c[ImGuiCol_HeaderActive]    = ImVec4(0.290f, 0.482f, 0.969f, 1.0f); // #4A7BF7

    // ── Widget (Button, Slider, Checkbox frame) ──
    c[ImGuiCol_Button]          = ImVec4(0.200f, 0.227f, 0.278f, 1.0f); // #333A47
    c[ImGuiCol_ButtonHovered]   = ImVec4(0.239f, 0.271f, 0.337f, 1.0f); // #3D4556
    c[ImGuiCol_ButtonActive]    = ImVec4(0.290f, 0.482f, 0.969f, 1.0f); // #4A7BF7
    c[ImGuiCol_FrameBg]         = ImVec4(0.200f, 0.227f, 0.278f, 1.0f); // #333A47
    c[ImGuiCol_FrameBgHovered]  = ImVec4(0.239f, 0.271f, 0.337f, 1.0f); // #3D4556
    c[ImGuiCol_FrameBgActive]   = ImVec4(0.290f, 0.482f, 0.969f, 0.67f);
    c[ImGuiCol_CheckMark]       = ImVec4(0.290f, 0.482f, 0.969f, 1.0f); // #4A7BF7
    c[ImGuiCol_SliderGrab]      = ImVec4(0.290f, 0.482f, 0.969f, 0.80f);
    c[ImGuiCol_SliderGrabActive]= ImVec4(0.290f, 0.482f, 0.969f, 1.0f);

    // ── Selection ──
    c[ImGuiCol_TextSelectedBg]  = ImVec4(0.290f, 0.482f, 0.969f, 0.35f);

    // ── Tab ──
    c[ImGuiCol_Tab]             = ImVec4(0.133f, 0.149f, 0.180f, 1.0f); // #22262E
    c[ImGuiCol_TabHovered]      = ImVec4(0.239f, 0.271f, 0.337f, 1.0f);
    c[ImGuiCol_TabActive]       = ImVec4(0.165f, 0.184f, 0.227f, 1.0f); // #2A2F3A
    c[ImGuiCol_TabUnfocused]    = ImVec4(0.102f, 0.114f, 0.137f, 1.0f);
    c[ImGuiCol_TabUnfocusedActive] = ImVec4(0.133f, 0.149f, 0.180f, 1.0f);

    // ── Title Bar ──
    c[ImGuiCol_TitleBg]         = ImVec4(0.102f, 0.114f, 0.137f, 1.0f);
    c[ImGuiCol_TitleBgActive]   = ImVec4(0.133f, 0.149f, 0.180f, 1.0f);
    c[ImGuiCol_TitleBgCollapsed]= ImVec4(0.063f, 0.071f, 0.090f, 0.75f);

    // ── Scrollbar ──
    c[ImGuiCol_ScrollbarBg]     = ImVec4(0.102f, 0.114f, 0.137f, 0.53f);
    c[ImGuiCol_ScrollbarGrab]   = ImVec4(0.314f, 0.345f, 0.408f, 1.0f); // #505868
    c[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.408f, 0.439f, 0.502f, 1.0f);
    c[ImGuiCol_ScrollbarGrabActive]  = ImVec4(0.494f, 0.722f, 1.000f, 1.0f);

    // ── Separator, Border ──
    c[ImGuiCol_Separator]       = ImVec4(0.180f, 0.204f, 0.251f, 1.0f); // #2E3440
    c[ImGuiCol_SeparatorHovered]= ImVec4(0.290f, 0.482f, 0.969f, 0.78f);
    c[ImGuiCol_SeparatorActive] = ImVec4(0.290f, 0.482f, 0.969f, 1.0f);
    c[ImGuiCol_Border]          = ImVec4(0.227f, 0.251f, 0.314f, 0.50f); // #3A4050

    // ── Resize Grip ──
    c[ImGuiCol_ResizeGrip]      = ImVec4(0.290f, 0.482f, 0.969f, 0.20f);
    c[ImGuiCol_ResizeGripHovered]= ImVec4(0.290f, 0.482f, 0.969f, 0.67f);
    c[ImGuiCol_ResizeGripActive] = ImVec4(0.290f, 0.482f, 0.969f, 0.95f);

    // ── Docking ──
    c[ImGuiCol_DockingPreview]  = ImVec4(0.290f, 0.482f, 0.969f, 0.70f);

    // ── Text ──
    c[ImGuiCol_Text]            = ImVec4(0.878f, 0.894f, 0.925f, 1.0f); // #E0E4EC
    c[ImGuiCol_TextDisabled]    = ImVec4(0.533f, 0.569f, 0.647f, 1.0f); // #8891A5

    // ── Misc ──
    c[ImGuiCol_TableRowBg]      = ImVec4(0.0f, 0.0f, 0.0f, 0.0f);
    c[ImGuiCol_TableRowBgAlt]   = ImVec4(0.133f, 0.149f, 0.180f, 0.30f);
    c[ImGuiCol_TableBorderStrong]= ImVec4(0.227f, 0.251f, 0.314f, 1.0f);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.180f, 0.204f, 0.251f, 1.0f);

    // ── Style Vars ──
    style.WindowRounding    = 2.0f;   // Stitch: sm (2px), 과한 라운딩 제거
    style.ChildRounding     = 2.0f;
    style.FrameRounding     = 2.0f;
    style.PopupRounding     = 2.0f;
    style.TabRounding       = 2.0f;
    style.GrabRounding      = 2.0f;
    style.ScrollbarRounding = 2.0f;
    style.WindowBorderSize  = 0.0f;   // Stitch "No-Line" 규칙
    style.ChildBorderSize   = 0.0f;
    style.FrameBorderSize   = 0.0f;
    style.TabBorderSize     = 0.0f;
    style.WindowPadding     = ImVec2(8.0f, 8.0f);
    style.FramePadding      = ImVec2(8.0f, 4.0f);
    style.ItemSpacing       = ImVec2(8.0f, 4.0f);
    style.ItemInnerSpacing  = ImVec2(4.0f, 4.0f);
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 8.0f;
}
```

### 1.2 glClearColor 변경

`render_shell_frame()` (`shell_main.cpp:9312`):
```cpp
// 변경 전
glClearColor(0.07f, 0.08f, 0.10f, 1.0f);
// 변경 후  (surface / #101319)
glClearColor(0.063f, 0.075f, 0.098f, 1.0f);
```

### 1.3 검증
```bash
cmake --build build && ./build/marrow_editor_shell --auto-close 5
```

---

## Phase 2: 뷰포트 설정 패널 분리

**목표**: Onion Skin / Debug Overlay / Performance HUD / Weight Paint 설정을 뷰포트 밖으로 분리
**핵심 문제 해결**: CollapsingHeader 4개가 뷰포트 캔버스 높이를 잡아먹는 문제의 근본적 해결

### 2.1 설정을 Properties 패널로 이동

Stitch 디자인에서는 플로팅 패널이지만, ImGui에서는 **Properties 패널 내 CollapsingHeader**가 더 자연스럽다.

현재 구조:
```
Viewport Window
  ├── Header text
  ├── CollapsingHeader "Onion Skin"      ← 제거
  ├── CollapsingHeader "Debug Overlay"   ← 제거
  ├── CollapsingHeader "Performance HUD" ← 제거
  ├── CollapsingHeader "Weight Paint"    ← 제거
  └── Canvas (GetContentRegionAvail)
```

변경 후:
```
Viewport Window
  ├── Toolbar (새로 추가)
  └── Canvas (거의 전체 영역 사용)

Properties Window
  ├── [기존 Bones/Slots/Attachments/Skin Preview]
  ├── CollapsingHeader "Viewport Settings"
  │   ├── Onion Skin 설정
  │   ├── Debug Overlay 토글
  │   └── Performance HUD 토글
  └── CollapsingHeader "Weight Paint"
      └── Weight Paint 설정
```

### 2.2 수정 내용

**`draw_viewport_window()`** (`shell_main.cpp:8416`):
- Onion Skin, Debug Overlay, Performance HUD, Weight Paint CollapsingHeader 블록 전체 제거 (line 8442–8699)
- 대신 한 줄 **뷰포트 툴바** 삽입

**`draw_inspector_window()`** (`shell_main.cpp:8883`):
- 기존 Skin Preview 섹션 뒤에 이동된 설정 섹션 추가

### 2.3 뷰포트 인라인 툴바

Stitch 디자인의 플로팅 툴바를 **단순 버튼 행**으로 구현:

```cpp
// draw_viewport_window() 내부, 캔버스 직전
ImGui::TextUnformatted(preview_label.c_str());
ImGui::SameLine();
ImGui::TextDisabled("RMB pan  Wheel zoom");
ImGui::Separator();

// ── Viewport Toolbar ──
if (ImGui::SmallButton("Fit")) {
    // zoom-to-fit: skeleton bounds 기반으로 pan/zoom 계산
    auto_frame_skeleton(state);
}
ImGui::SameLine();
if (ImGui::SmallButton("1:1")) {
    state->viewport.zoom = 1.0;
    state->viewport.pan_x = 0.0;
    state->viewport.pan_y = 0.0;
}
ImGui::SameLine();
ImGui::TextDisabled("|");
ImGui::SameLine();
bool bones_visible = state->viewport.debug_overlay.show_bones;
if (ImGui::SmallButton(bones_visible ? "[B]" : " B ")) {
    state->viewport.debug_overlay.show_bones = !bones_visible;
}
ImGui::SameLine();
bool grid_visible = state->viewport.show_grid; // 새 필드
if (ImGui::SmallButton(grid_visible ? "[G]" : " G ")) {
    state->viewport.show_grid = !grid_visible;
}
```

### 2.4 auto_frame_skeleton() 구현

현재 없는 기능 — 가장 요청이 많을 UX 개선:

```cpp
void auto_frame_skeleton(ShellState* state) {
    if (!state->preview_skeleton) return;
    const auto& transforms = state->preview_skeleton->bone_world_transforms();
    if (transforms.empty()) return;

    // 모든 본의 world position으로 bounding box 계산
    float min_x = FLT_MAX, min_y = FLT_MAX;
    float max_x = -FLT_MAX, max_y = -FLT_MAX;
    for (const auto& t : transforms) {
        min_x = std::min(min_x, t.tx);
        max_x = std::max(max_x, t.tx);
        min_y = std::min(min_y, t.ty);
        max_y = std::max(max_y, t.ty);
    }

    // 여유 마진 20%
    const float margin = 1.2f;
    const float bounds_w = (max_x - min_x) * margin;
    const float bounds_h = (max_y - min_y) * margin;
    const float center_x = (min_x + max_x) * 0.5f;
    const float center_y = (min_y + max_y) * 0.5f;

    // 캔버스 크기 대비 zoom 계산
    const ImVec2 canvas = ImGui::GetContentRegionAvail();
    if (canvas.x < 1.0f || canvas.y < 1.0f) return;

    const float zoom_x = canvas.x / std::max(bounds_w, 1.0f);
    const float zoom_y = canvas.y / std::max(bounds_h, 1.0f);
    state->viewport.zoom = static_cast<double>(
        clamp_zoom(std::min(zoom_x, zoom_y)));

    state->viewport.pan_x = static_cast<double>(canvas.x * 0.5f - center_x * state->viewport.zoom);
    state->viewport.pan_y = static_cast<double>(canvas.y * 0.5f + center_y * state->viewport.zoom);
}
```

### 2.5 검증
```bash
cmake --build build
./build/marrow_unit_tests
./build/marrow_editor_shell --auto-close 5
```

---

## Phase 3: 좌측 네비게이션 레일

**목표**: Stitch 디자인의 아이콘 사이드바 구현
**구조 변경**: 좌측에 좁은 고정 패널 추가, 클릭으로 좌측 탭 전환

### 3.1 구현 방식

ImGui에서 아이콘 레일을 가장 자연스럽게 구현하는 방법:
**좌측에 좁은 Child Window (40px) + 세로 배치 버튼**

```
┌──┬──────────┬──────────────────────┐
│  │ Project  │                      │
│P │ content  │     Viewport         │
│H │          │                      │
│L │          │                      │
│A │          │                      │
│H │          ├──────────────────────┤
│  │Properties│     Timeline         │
│  │ content  │                      │
└──┴──────────┴──────────────────────┘
 40px
```

### 3.2 네비게이션 항목

| 아이콘 | 텍스트 | 활성화 패널 |
|--------|--------|-------------|
| P | Project | Project 탭 포커스 |
| H | Hierarchy | Hierarchy 탭 포커스 |
| L | Library | (미래 확장용, 현재 비활성) |
| A | Assets | Runtime Assets 탭 포커스 |
| C | Constraints | Constraints 탭 포커스 |
| I | Inspector | Properties 탭 포커스 |

### 3.3 구현

dock layout 변경: 기존 left split 이전에 좁은 영역 추가.

```cpp
// ensure_default_dock_layout() 수정
ImGuiID dock_nav_id = 0;
ImGui::DockBuilderSplitNode(
    dock_center_id, ImGuiDir_Left, 0.025f, &dock_nav_id, &dock_center_id);

// 이후 기존 0.28 split 유지
ImGui::DockBuilderSplitNode(
    dock_center_id, ImGuiDir_Left, 0.28f, &dock_left_id, &dock_center_id);
```

또는 더 간단하게: **DockSpace 외부에 고정 사이드바**를 별도 Window로 배치.

```cpp
void draw_nav_rail(ShellState* state) {
    ImGuiViewport* vp = ImGui::GetMainViewport();
    const float rail_width = 40.0f;
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(ImVec2(rail_width, vp->WorkSize.y));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(4, 8));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.063f, 0.075f, 0.098f, 1.0f));

    ImGui::Begin("##nav_rail", nullptr,
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
        ImGuiWindowFlags_NoDocking);

    auto nav_button = [&](const char* label, int idx) {
        bool active = (state->active_nav_index == idx);
        if (active) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.290f, 0.482f, 0.969f, 1.0f));
        }
        if (ImGui::Button(label, ImVec2(32, 32))) {
            state->active_nav_index = idx;
        }
        if (active) {
            ImGui::PopStyleColor();
        }
    };

    nav_button("P", 0);  // Project
    nav_button("H", 1);  // Hierarchy
    nav_button("A", 2);  // Assets
    nav_button("C", 3);  // Constraints
    nav_button("I", 4);  // Inspector

    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}
```

### 3.4 DockSpace 오프셋

네비게이션 레일 너비만큼 DockSpace를 우측으로 밀어야 함:

```cpp
// render_shell_frame() 내
const float rail_width = 40.0f;
ImGuiViewport* vp = ImGui::GetMainViewport();
ImGui::SetNextWindowPos(ImVec2(vp->WorkPos.x + rail_width, vp->WorkPos.y));
ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x - rail_width, vp->WorkSize.y));
// DockSpace 생성
```

### 3.5 ShellState 확장

```cpp
// shell_types.hpp — ShellState에 추가
int active_nav_index{0};
```

---

## Phase 4: 타임라인 개선

**목표**: 재생 컨트롤 정리 + 트랙 아이콘 + 단축키

### 4.1 재생 컨트롤 정리

현재 (한 줄에 나열):
```
[ Play ] [ Reset ]  ☐ Loop  [ Prev Key ] [ Next Key ]
```

변경 (중앙 정렬 + 간격):
```
        |<  <  [ ▶ ]  >  >|     0.340 / 1.200     ☐ Loop
```

```cpp
// 재생 버튼 중앙 정렬
const float button_group_width = 200.0f;
const float avail = ImGui::GetContentRegionAvail().x;
ImGui::SetCursorPosX((avail - button_group_width) * 0.5f);

if (ImGui::SmallButton("|<")) { /* reset to 0 */ }
ImGui::SameLine();
if (ImGui::SmallButton("<")) { /* prev key */ }
ImGui::SameLine();
if (ImGui::Button(state->timeline_playing ? "||" : ">")) { /* play/pause */ }
ImGui::SameLine();
if (ImGui::SmallButton(">")) { /* next key */ }
ImGui::SameLine();
if (ImGui::SmallButton(">|")) { /* reset to end */ }
ImGui::SameLine();
ImGui::Text("%.3fs / %.3fs", state->timeline_time_seconds, duration_seconds);
ImGui::SameLine(avail - 80.0f);
ImGui::Checkbox("Loop", &loop_enabled);
```

### 4.2 트랙 타입 접두사

현재: `root / Bone / Translate (3)`
변경: `[T] root / Bone / Translate (3)` — 타입 약어로 즉시 식별

```cpp
// build_timeline_tracks() 결과에서 track.kind로 접두사 결정
const char* prefix = "";
switch (track.kind) {
    case TimelineTrackKind::BoneTranslate: prefix = "[T] "; break;
    case TimelineTrackKind::BoneRotate:    prefix = "[R] "; break;
    case TimelineTrackKind::BoneScale:     prefix = "[S] "; break;
    case TimelineTrackKind::SlotColor:     prefix = "[C] "; break;
    case TimelineTrackKind::DrawOrder:     prefix = "[D] "; break;
    case TimelineTrackKind::Event:         prefix = "[E] "; break;
    case TimelineTrackKind::Attachment:    prefix = "[A] "; break;
    case TimelineTrackKind::MeshDeform:    prefix = "[M] "; break;
    default: break;
}
std::string label = std::string(prefix) + track.label + " (" + std::to_string(track.key_times.size()) + ")";
```

### 4.3 키보드 단축키

```cpp
void handle_timeline_shortcuts(ShellState* state) {
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantTextInput) return; // 텍스트 입력 중이면 무시

    // Space: Play/Pause
    if (ImGui::IsKeyPressed(ImGuiKey_Space) && !io.KeyCtrl) {
        state->timeline_playing = !state->timeline_playing;
        // ... (기존 play 로직)
    }
    // Home: 처음으로
    if (ImGui::IsKeyPressed(ImGuiKey_Home)) {
        scrub_timeline_time(state, 0.0, "Shortcut", true);
    }
    // Left/Right: 프레임 단위 이동
    if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow) && !io.KeyCtrl) {
        scrub_timeline_time(state,
            std::max(0.0, state->timeline_time_seconds - 1.0/60.0),
            "Shortcut", true);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_RightArrow) && !io.KeyCtrl) {
        scrub_timeline_time(state,
            std::min(duration, state->timeline_time_seconds + 1.0/60.0),
            "Shortcut", true);
    }
    // F: Zoom to fit (viewport)
    if (ImGui::IsKeyPressed(ImGuiKey_F) && !io.KeyCtrl) {
        auto_frame_skeleton(state);
    }
}
```

---

## Phase 5: 인스펙터 정리

**목표**: 정보 계층 명확화 + 편집 영역 시각 구분

### 5.1 Viewport State를 상단 바에서 제거

현재 인스펙터 상단의 `Viewport pan: 0.0, 0.0 / Viewport zoom: 1.00`는
개발자 디버그 정보에 가까움. **삭제 또는 Performance HUD로 이동**.

### 5.2 Local Pose 편집 영역 강조

Setup Pose (읽기전용)과 Local Pose (편집가능)의 구분:

```cpp
// Local Pose 편집 영역에 배경색 강조
ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.165f, 0.184f, 0.227f, 1.0f)); // #2A2F3A
ImGui::BeginChild("local_pose_editor", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY);
ImGui::TextUnformatted("Local Pose");
ImGui::SameLine();
ImGui::TextDisabled("(editable)");
// ... DragFloat2, DragFloat 위젯들
ImGui::EndChild();
ImGui::PopStyleColor();
```

### 5.3 Slot Color 위젯 개선

ColorEdit4에 프리뷰 스와치 추가:

```cpp
// 색상 프리뷰를 라벨 옆에 표시
ImVec4 light_color = /* ... */;
ImDrawList* dl = ImGui::GetWindowDrawList();
ImVec2 p = ImGui::GetCursorScreenPos();
dl->AddRectFilled(p, ImVec2(p.x + 12, p.y + 12),
    ImGui::ColorConvertFloat4ToU32(light_color));
ImGui::Dummy(ImVec2(14, 14));
ImGui::SameLine();
ImGui::ColorEdit4("Light color##slot_color", &light_color.x, ...);
```

---

## Phase 6: 메뉴 바 확장

**목표**: Stitch 디자인의 6개 메뉴 구조로 확장

### 6.1 메뉴 구조

| 메뉴 | 항목 |
|------|------|
| **File** | Open Project..., Recent Projects >, Reload, Save, Export Runtime Assets, Quit |
| **Edit** | Undo (Ctrl+Z), Redo (Ctrl+Shift+Z), Separator, Reset Viewport |
| **View** | Onion Skin, Debug Overlay >, Performance HUD, Separator, Zoom to Fit (F), Reset Zoom |
| **Assets** | Export .mskl, Export .mbin, Separator, Open Export Folder |
| **Window** | Reset Layout, Separator, (각 패널 표시/숨김 토글) |
| **Help** | About Maroow, Keyboard Shortcuts |

### 6.2 Recent Projects

```cpp
// ShellState에 추가
std::vector<std::filesystem::path> recent_projects; // 최대 10개

// File 메뉴
if (ImGui::BeginMenu("Recent Projects", !state->recent_projects.empty())) {
    for (const auto& path : state->recent_projects) {
        if (ImGui::MenuItem(path.filename().string().c_str())) {
            state->project_path = path;
            reload_project(state);
        }
    }
    ImGui::EndMenu();
}
```

---

## Phase 7: 하단 탭 바

**목표**: Stitch 디자인의 Timeline/Graph/Audio/Console 탭 중 실현 가능한 것

### 7.1 Timeline + Console 탭

Graph/Audio는 미구현 기능이므로 제외.
**Console** 탭을 추가하여 상태 로그 표시.

```cpp
void draw_bottom_panel(ShellState* state) {
    ImGui::Begin(kTimelineWindowTitle);

    if (ImGui::BeginTabBar("bottom_tabs")) {
        if (ImGui::BeginTabItem("Timeline")) {
            draw_timeline_content(state); // 기존 draw_timeline_window 내용
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Console")) {
            draw_console_content(state); // 상태 메시지 로그
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    ImGui::End();
}
```

### 7.2 Console 내용

```cpp
void draw_console_content(ShellState* state) {
    // 최근 상태 메시지 표시 (최대 100줄 링버퍼)
    ImGui::BeginChild("console_log", ImVec2(0, 0), false);
    for (const auto& msg : state->console_log) {
        ImGui::TextUnformatted(msg.c_str());
    }
    if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY()) {
        ImGui::SetScrollHereY(1.0f); // 자동 스크롤
    }
    ImGui::EndChild();
}
```

---

## 실행 순서 및 우선순위

| 순서 | Phase | 난이도 | UX 개선도 | 예상 라인 변경 |
|------|-------|--------|-----------|----------------|
| 1 | **Phase 1: 테마** | 저 | 중 | ~80줄 교체 |
| 2 | **Phase 2: 뷰포트 설정 분리 + Fit** | 중 | **최고** | ~300줄 이동+추가 |
| 3 | **Phase 4: 타임라인 개선** | 저 | 고 | ~80줄 수정 |
| 4 | **Phase 5: 인스펙터 정리** | 저 | 중 | ~50줄 수정 |
| 5 | **Phase 6: 메뉴 바 확장** | 저 | 중 | ~100줄 추가 |
| 6 | **Phase 3: 네비게이션 레일** | 중 | 중 | ~120줄 추가 |
| 7 | **Phase 7: 하단 탭 바** | 저 | 저 | ~60줄 추가 |

**총 예상**: 약 800줄 수정/추가. 기존 9,334줄 기준 ~8.5% 변경.

---

## Stitch 디자인에서 의도적으로 생략하는 요소

| Stitch 디자인 요소 | 생략 이유 |
|---------------------|-----------|
| Glassmorphism (backdrop blur) | ImGui 미지원. 반투명 배경색으로 충분 |
| Gradient 버튼 | 커스텀 DrawList 필요, 유지보수 비용 대비 효과 낮음 |
| Layers 패널 (Weight Paint) | 런타임 엔진 변경 필요, 현재 스코프 밖 |
| Graph 탭 | 미구현 기능, 향후 커브 에디터로 확장 가능 |
| Audio 탭 | 미구현 기능, Spine 이벤트로 대체 가능 |
| Ghost border (20% opacity outline) | ImGui에서 border opacity 개별 제어 어려움 |
| Inter Condensed 폰트 | TTF 번들링 + 라이선스 확인 필요, 2순위 |

---

## 검증 기준

모든 Phase 완료 후:
```bash
cmake --build build
./build/marrow_unit_tests          # 27개 전부 통과
./build/marrow_editor_shell --auto-close 5  # headless smoke 통과
./build/marrow_renderer_sample --auto-close 2  # renderer 통과
```

시각적 검증:
- 뷰포트에 텍스처 캐릭터 렌더링 정상
- 모든 패널 접근 가능
- 애니메이션 재생 + 타임라인 스크럽 정상
- Undo/Redo 정상
- Onion Skin 고스트 표시 정상
