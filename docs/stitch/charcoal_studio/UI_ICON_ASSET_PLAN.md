# Marrow Editor UI Icon Asset Plan

## 1. Overview

Charcoal Studio 디자인 시스템에 맞는 커스텀 UI 아이콘 에셋을 Google Gemini (Nano Banana)로 생성하기 위한 계획.

### Design System Alignment (DESIGN.md 참조)

| Token | Hex | 용도 |
|-------|-----|------|
| `primary` | `#b3c5ff` | 아이콘 기본 색상 (Point Blue) |
| `primary-container` | `#608bff` | 아이콘 활성 상태 |
| `secondary` | `#bec7dc` | 비활성/보조 아이콘 |
| `tertiary-container` | `#ff5450` | 재생헤드/긴급 표시 전용 |
| `outline` | `#8d9199` | 비활성 아이콘 기본 |
| `outline-variant` | `#434654` | Ghost 아이콘 |
| `surface` | `#101319` | UI 바탕색 |
| `surface-container-low` | `#191c22` | 패널 배경 |
| `surface-container-high` | `#272a30` | 위젯 배경 |

### Icon Style: "Charcoal Precision"
- **Line weight**: 1.5-2px (16px), 2-2.5px (24px) — 얇고 정밀한 선
- **Corner radius**: 1px (날카롭고 건축적)
- **Fill**: Outline-only 기본, 선택/활성 시 filled
- **Color**: 단색 (monochrome), primary 팔레트 계열
- **NO**: 그림자, 그라데이션, 둥근 blob, 장식적 요소

---

## 2. Post-Processing Workflow

### AI 투명 배경 제한 대응 전략

AI는 투명 배경 생성이 불가능하므로 다음 워크플로우를 적용:

```
[Gemini 생성] → [후처리] → [최종 에셋]

1. 생성: 순수 흰 배경(#FFFFFF)에 단색 아이콘
2. 배경 제거: rembg / ImageMagick / Python PIL
3. 색상 조정: primary (#b3c5ff) 단색으로 tint
4. 크기 조정: 512px → 64px, 48px, 32px, 24px, 16px
5. 포맷: PNG (alpha channel)
```

### 프롬프트 배경 전략

**추천: 순수 흰 배경 + 검은색 아이콘**
- AI가 가장 안정적으로 생성하는 조합
- 후처리 시 흰 배경 제거 → 검은색을 `#b3c5ff`로 색상 교체
- ImageMagick 한 줄: `magick input.png -fuzz 10% -transparent white -fill "#b3c5ff" -opaque black output.png`

**대안: 순수 녹색 배경 (크로마키)**
- 복잡한 아이콘에서 배경 분리가 더 정확
- `magick input.png -fuzz 15% -transparent "#00FF00" output.png`

### 프롬프트 핵심 접미사 (모든 프롬프트에 공통 적용)

```
... clean vector line art, minimal flat icon design,
single weight stroke, no gradients, no shadows, no text,
centered on pure white background, isolated single icon,
high contrast black lines on white, 512x512 pixel,
professional UI icon for dark theme animation software
```

---

## 3. Icon Inventory & Prompts

### Priority Tier System

| Tier | 설명 | 수량 |
|------|------|------|
| **P0** | 핵심 재생/편집 컨트롤 (없으면 UI 부족) | 12 |
| **P1** | 뷰포트/패널 도구 아이콘 | 15 |
| **P2** | 트리뷰/계층구조 아이콘 | 14 |
| **P3** | 속성/상태 표시 아이콘 | 10 |
| **총계** | | **51** |

---

### P0: Core Playback & Edit Controls (12)

생성 시 **일관성 유지**를 위해 한 세션에서 연속 생성 권장.

| # | Name | File | Prompt |
|---|------|------|--------|
| 1 | Play | `play.png` | A minimal flat icon of a right-pointing triangle (play button), single solid filled triangle pointing right, geometric precision, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon for dark theme animation software |
| 2 | Pause | `pause.png` | A minimal flat icon of two vertical parallel bars (pause button), equal width and height, geometric precision, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 3 | Stop | `stop.png` | A minimal flat icon of a single solid square (stop button), geometric precision, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 4 | Reset/Rewind | `rewind.png` | A minimal flat icon of a vertical bar followed by a left-pointing triangle (skip to beginning), geometric precision, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 5 | Prev Key | `prev_key.png` | A minimal flat icon of a left-pointing chevron arrow with a small diamond shape to the left (previous keyframe), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 6 | Next Key | `next_key.png` | A minimal flat icon of a right-pointing chevron arrow with a small diamond shape to the right (next keyframe), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 7 | Loop | `loop.png` | A minimal flat icon of two curved arrows forming a circular loop (repeat/loop), clean smooth curves, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 8 | Undo | `undo.png` | A minimal flat icon of a single curved arrow pointing counter-clockwise to the left (undo action), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 9 | Redo | `redo.png` | A minimal flat icon of a single curved arrow pointing clockwise to the right (redo action), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 10 | Add Key | `add_key.png` | A minimal flat icon of a diamond shape (keyframe) with a small plus sign at the top-right corner, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 11 | Remove Key | `remove_key.png` | A minimal flat icon of a diamond shape (keyframe) with a small minus sign at the top-right corner, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 12 | Save | `save.png` | A minimal flat icon of a floppy disk (save icon), clean geometric shape, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |

---

### P1: Viewport & Panel Tool Icons (15)

| # | Name | File | Prompt |
|---|------|------|--------|
| 13 | Zoom Fit | `zoom_fit.png` | A minimal flat icon of a magnifying glass with four inward-pointing arrows inside the lens (fit to view), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 14 | Zoom 1:1 | `zoom_one.png` | A minimal flat icon of a magnifying glass with the text "1:1" inside the lens, clean vector line art, no gradients, no shadows, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 15 | Bone Toggle | `bone_toggle.png` | A minimal flat icon of a stylized bone joint, two circles connected by a tapered line (animation rig bone), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 16 | Onion Skin | `onion_skin.png` | A minimal flat icon of three overlapping transparent figure silhouettes showing motion sequence (onion skinning in animation), decreasing opacity from front to back, clean vector line art, no gradients, no shadows, no text, centered on pure white background, high contrast black on white, 512x512, professional UI icon |
| 17 | Performance HUD | `perf_hud.png` | A minimal flat icon of a speedometer gauge or performance meter with a needle, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 18 | Mesh Wireframe | `mesh_wire.png` | A minimal flat icon of a triangulated mesh grid (polygon wireframe), showing connected triangles in a flat plane, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 19 | Bounding Box | `bbox.png` | A minimal flat icon of a dashed rectangle outline (bounding box), corners marked with small squares, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 20 | Weight Paint Brush | `weight_brush.png` | A minimal flat icon of a circular brush tool with a soft gradient edge indicator (weight painting brush), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 21 | Weight Paint Erase | `weight_erase.png` | A minimal flat icon of an eraser tool, rectangular with angled edge, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 22 | Weight Paint Smooth | `weight_smooth.png` | A minimal flat icon of a hand smoothing gesture or a wave/blur symbol (smooth tool), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 23 | Reload | `reload.png` | A minimal flat icon of a single circular arrow forming a refresh/reload symbol, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 24 | Export | `export.png` | A minimal flat icon of an upward arrow emerging from an open box (export/share), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 25 | Move Up | `move_up.png` | A minimal flat icon of an upward-pointing arrow (move item up in order), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 26 | Move Down | `move_down.png` | A minimal flat icon of a downward-pointing arrow (move item down in order), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 27 | Eye (Visibility) | `eye.png` | A minimal flat icon of an open eye (visibility toggle), clean almond-shaped eye with a circle iris, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |

---

### P2: Tree View & Hierarchy Icons (14)

| # | Name | File | Prompt |
|---|------|------|--------|
| 28 | Bone Node | `node_bone.png` | A minimal flat icon of a single bone joint for animation rig, elongated diamond shape with circular joints at each end, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 29 | Slot | `node_slot.png` | A minimal flat icon of a horizontal slot or layer indicator, a rectangle with a small tab on the left side, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 30 | Region Attachment | `att_region.png` | A minimal flat icon of a simple rectangular image frame (region/sprite attachment), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 31 | Mesh Attachment | `att_mesh.png` | A minimal flat icon of a deformable mesh grid, rectangle subdivided into triangles with movable vertices, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 32 | Linked Mesh | `att_linked.png` | A minimal flat icon of two overlapping mesh grids connected by a chain link symbol, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 33 | Point Attachment | `att_point.png` | A minimal flat icon of a crosshair or target point with a small directional arrow (point attachment with rotation), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 34 | Bounding Box Att. | `att_bbox.png` | A minimal flat icon of a dashed polygon outline (bounding box attachment), irregular convex shape, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 35 | Clipping Att. | `att_clip.png` | A minimal flat icon of scissors cutting through a shape (clipping mask), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 36 | Path Attachment | `att_path.png` | A minimal flat icon of a curved bezier path with control point handles (path attachment), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 37 | Animation Clip | `node_anim.png` | A minimal flat icon of a film strip frame or clapperboard (animation clip), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 38 | Skin | `node_skin.png` | A minimal flat icon of overlapping layers or a palette swatch (skin/appearance set), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 39 | IK Constraint | `constraint_ik.png` | A minimal flat icon of a chain of connected segments with a target marker at the end (inverse kinematics), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 40 | Path Constraint | `constraint_path.png` | A minimal flat icon of a bone following along a curved dotted path (path constraint), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 41 | Transform Constraint | `constraint_xform.png` | A minimal flat icon of two overlapping transform gizmos, rotation and translation arrows (transform constraint), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |

---

### P3: Property & Status Icons (10)

| # | Name | File | Prompt |
|---|------|------|--------|
| 42 | Rotate | `prop_rotate.png` | A minimal flat icon of a circular arrow indicating rotation around a center point, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 43 | Translate | `prop_translate.png` | A minimal flat icon of a four-directional move crosshair with arrows pointing up down left right (translate/move), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 44 | Scale | `prop_scale.png` | A minimal flat icon of a diagonal double-headed arrow with a small square at each end (scale/resize), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 45 | Shear | `prop_shear.png` | A minimal flat icon of a parallelogram or a rectangle being sheared/skewed with directional indicators, clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 46 | Color Tint | `prop_color.png` | A minimal flat icon of a paint droplet or color circle (color/tint property), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 47 | Draw Order | `prop_order.png` | A minimal flat icon of stacked horizontal lines with vertical reorder arrows (draw order/z-order), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 48 | Event Marker | `prop_event.png` | A minimal flat icon of a lightning bolt or flag marker (event trigger), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 49 | Warning | `status_warn.png` | A minimal flat icon of a triangle with an exclamation mark inside (warning indicator), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 50 | Error | `status_error.png` | A minimal flat icon of a circle with an X mark inside (error indicator), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |
| 51 | Physics | `constraint_physics.png` | A minimal flat icon of a spring coil with a weight ball at the bottom (physics constraint/simulation), clean vector line art, no gradients, no shadows, no text, centered on pure white background, isolated single icon, high contrast black on white, 512x512, professional UI icon |

---

## 4. Batch Generation Strategy

### Session Workflow (Gemini/Nano Banana2 pro)

AI 일관성을 위해 **같은 세션 내**에서 순차 생성:

```
Session 1: P0 Core Controls (#1-#12)
  → 모든 재생/편집 컨트롤을 하나의 세션에서 생성
  → 첫 번째 생성 결과를 참조 이미지로 활용하여 스타일 통일

Session 2: P1 Viewport Tools (#13-#27)
  → Session 1 결과물 중 하나를 참조 이미지로 업로드
  → "Match the style of this reference icon" 접두사 추가

Session 3: P2 Hierarchy Icons (#28-#41)
  → 동일 방식으로 참조 이미지 활용

Session 4: P3 Property Icons (#42-#51)
  → 동일 방식으로 참조 이미지 활용
```

### 참조 이미지 활용 프롬프트 패턴

두 번째 아이콘부터:
```
Match the exact visual style, line weight, and proportion of this reference icon.
Create a [description] icon.
Same minimal flat design, same stroke width, same black on white treatment.
No gradients, no shadows, no text, centered on pure white background, 512x512.
```

---

## 5. Post-Processing Pipeline

### Step 1: Background Removal (일괄 처리)

```bash
# Python rembg (가장 정확)
pip install rembg
rembg p input_folder/ output_folder/

# 또는 ImageMagick (빠르고 간단)
for f in *.png; do
  magick "$f" -fuzz 10% -transparent white "${f%.png}_alpha.png"
done
```

### Step 2: Color Tinting (검은색 → Point Blue)

```bash
# 기본 아이콘: #b3c5ff (primary)
for f in *_alpha.png; do
  magick "$f" -fill "#b3c5ff" -opaque black "${f%_alpha.png}_primary.png"
done

# 보조 아이콘: #bec7dc (secondary)
for f in *_alpha.png; do
  magick "$f" -fill "#bec7dc" -opaque black "${f%_alpha.png}_secondary.png"
done

# 활성 아이콘: #608bff (primary-container)
for f in *_alpha.png; do
  magick "$f" -fill "#608bff" -opaque black "${f%_alpha.png}_active.png"
done

# 비활성 아이콘: #8d9199 (outline)
for f in *_alpha.png; do
  magick "$f" -fill "#8d9199" -opaque black "${f%_alpha.png}_inactive.png"
done
```

### Step 3: Multi-Size Export

```bash
# 각 아이콘을 여러 크기로 리사이징
for f in *_primary.png; do
  base="${f%_primary.png}"
  magick "$f" -resize 64x64   "export/64/${base}.png"
  magick "$f" -resize 48x48   "export/48/${base}.png"
  magick "$f" -resize 32x32   "export/32/${base}.png"
  magick "$f" -resize 24x24   "export/24/${base}.png"
  magick "$f" -resize 16x16   "export/16/${base}.png"
done
```

### Step 4: Atlas Packing (Optional)

ImGui에서 개별 텍스처 대신 아이콘 아틀라스 사용 시:
```bash
# 모든 24x24 아이콘을 단일 아틀라스로 패킹
magick montage export/24/*.png -tile 8x -geometry 24x24+0+0 \
  -background none icon_atlas_24.png
```

---

## 6. ImGui Integration Plan

### 아이콘 로딩 (shell_main.cpp)

```cpp
// 개별 텍스처 방식
GLuint load_icon_texture(const char* path) {
    int w, h, channels;
    unsigned char* data = stbi_load(path, &w, &h, &channels, 4);
    if (!data) return 0;
    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, data);
    stbi_image_free(data);
    return tex;
}

// 사용
ImGui::ImageButton("play", (ImTextureID)(intptr_t)icon_play,
                   ImVec2(16, 16));
```

### 아이콘 아틀라스 방식 (권장)

```cpp
// UV 좌표로 아틀라스 내 아이콘 참조
struct IconAtlas {
    GLuint texture;
    int cols, rows;
    float icon_size;  // normalized UV size

    ImVec2 uv0(int index) const {
        int c = index % cols, r = index / cols;
        return ImVec2(c * icon_size, r * icon_size);
    }
    ImVec2 uv1(int index) const {
        int c = index % cols, r = index / cols;
        return ImVec2((c + 1) * icon_size, (r + 1) * icon_size);
    }
};
```

---

## 7. File Structure

```
assets/
  icons/
    raw/              ← Gemini 원본 출력 (512x512, 흰 배경)
    processed/        ← 배경 제거 + 색상 적용
      primary/        ← #b3c5ff 버전
      secondary/      ← #bec7dc 버전
      active/         ← #608bff 버전
      inactive/       ← #8d9199 버전
    export/
      16/             ← 16x16 최종 에셋
      24/             ← 24x24 최종 에셋 (주 사용 크기)
      32/             ← 32x32 최종 에셋
    icon_atlas_24.png ← 통합 아틀라스 (선택)
```

---

## 8. Quality Checklist

생성된 각 아이콘 검증:

- [ ] 아이콘이 의도한 의미를 즉시 전달하는가?
- [ ] 선 굵기가 다른 아이콘들과 일관적인가?
- [ ] 512x512에서 24x24로 축소해도 식별 가능한가?
- [ ] 불필요한 디테일이 없는가? (축소 시 뭉개짐 방지)
- [ ] 배경 제거가 깔끔한가? (halo/fringe 없음)
- [ ] Charcoal Studio 다크 테마 위에서 잘 보이는가?
- [ ] 다른 아이콘들과 시각적 무게감이 균일한가?

---

## 9. Timeline Track Label Icons (Text → Icon 변환)

현재 텍스트 기반 track label을 아이콘으로 교체:

| 현재 | 의미 | 아이콘 |
|------|------|--------|
| `[R]` | Rotate | `prop_rotate.png` |
| `[T]` | Translate | `prop_translate.png` |
| `[S]` | Scale | `prop_scale.png` |
| `[H]` | Shear | `prop_shear.png` |
| `[C]` | Color | `prop_color.png` |
| `[A]` | Attachment | `att_region.png` |
| `[M]` | Mesh/Deform | `att_mesh.png` |
| `[D]` | Draw Order | `prop_order.png` |
| `[E]` | Event | `prop_event.png` |
