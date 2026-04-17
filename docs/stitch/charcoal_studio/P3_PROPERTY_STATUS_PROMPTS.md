# P3: Property & Status Icon Prompts

## Generation Guide

- **Tool**: Google Gemini (Nano Banana 2 Pro)
- **Session**: P0 아이콘 중 하나(undo_v2 또는 prev_key)를 참조 이미지로 업로드
- **참조 프롬프트 접두사** (첫 아이콘 이후 매번 사용):

```
Match the exact visual style, line weight, and proportion of this reference icon.
```

- **공통 접미사** (모든 프롬프트 끝에 추가):

```
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

- **P3 특이사항**: 타임라인 트랙 라벨 및 상태 표시 아이콘. 현재 텍스트 라벨(`[R]`, `[T]`, `[S]` 등)을 대체하므로, 각 속성 타입을 즉시 구분할 수 있는 고유한 실루엣이 핵심.

---

## Icons (#42 ~ #51)

### #42 — Rotate (`prop_rotate.png`)

```
A minimal outline icon of a circular arc arrow going clockwise,
approximately 270 degrees of a circle, with an open triangular
arrowhead at one end. A small dot at the center of the arc indicates
the rotation pivot point. This represents a rotation transform property
in an animation timeline.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #43 — Translate (`prop_translate.png`)

```
A minimal outline icon of a four-directional move cross: four arrows
pointing outward in cardinal directions (up, down, left, right) meeting
at a central point, forming a plus-shaped arrangement. Each arrow has
an open triangular arrowhead. This represents a translation/position
property in an animation timeline.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #44 — Scale (`prop_scale.png`)

```
A minimal outline icon of a diagonal double-headed arrow going from
bottom-left to top-right, with open triangular arrowheads at both ends.
A small square at the bottom-left corner and a larger square at the
top-right corner, connected by the arrow, indicating size change.
This represents a scale transform property in an animation timeline.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #45 — Shear (`prop_shear.png`)

```
A minimal outline icon of a rectangle being skewed into a parallelogram
shape, with a small horizontal double-headed arrow at the top edge
indicating the shearing direction. The left side of the rectangle
remains vertical while the top and bottom edges tilt to the right.
This represents a shear/skew transform property in an animation timeline.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #46 — Color Tint (`prop_color.png`)

```
A minimal outline icon of a single water droplet or teardrop shape,
with a perfectly round bottom and a pointed top. The droplet outline
is clean and symmetrical. This represents a color or tint property
in an animation timeline for changing sprite colors.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #47 — Draw Order (`prop_order.png`)

```
A minimal outline icon of three horizontal lines stacked vertically
with equal spacing, and a vertical double-headed arrow on the right
side pointing up and down, indicating reordering capability. The lines
represent layers and the arrow represents changing their draw order
or z-index in the animation.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #48 — Event Marker (`prop_event.png`)

```
A minimal outline icon of a small flag on a vertical flagpole. The flag
is a simple triangular pennant shape pointing to the right, attached to
a straight vertical pole. A small horizontal base line at the bottom of
the pole. This represents an event trigger marker on the animation
timeline.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #49 — Warning (`status_warn.png`)

```
A minimal outline icon of an equilateral triangle with a single vertical
exclamation mark inside it (a short vertical line with a dot below it).
The triangle has sharp corners and the exclamation mark is centered.
This represents a warning or caution status indicator in the editor.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #50 — Error (`status_error.png`)

```
A minimal outline icon of a circle with a diagonal cross (X mark)
inside it, formed by two lines crossing from top-left to bottom-right
and top-right to bottom-left. The circle and X lines have equal stroke
width. This represents an error or failure status indicator in the editor.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #51 — Physics (`constraint_physics.png`)

```
A minimal outline icon of a vertical coil spring with 4-5 visible loops,
and a small filled circle (weight ball) hanging from the bottom of the
spring. A small horizontal line at the top indicates the fixed anchor
point. This represents a physics constraint or spring simulation
in the animation rig.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

---

## Checklist

생성 후 각 아이콘 확인:

### Property Icons (타임라인 트랙 라벨 대체)
- [ ] #42 Rotate — 원형 호 화살표 + 중심 피벗 점
- [ ] #43 Translate — 4방향 이동 십자 화살표
- [ ] #44 Scale — 대각선 양방향 화살표 + 작은/큰 사각형
- [ ] #45 Shear — 기울어진 평행사변형 + 방향 화살표
- [ ] #46 Color Tint — 물방울/티어드롭 형태
- [ ] #47 Draw Order — 수평선 3줄 + 세로 양방향 화살표
- [ ] #48 Event Marker — 깃발 + 깃대

### Status Icons (상태 표시)
- [ ] #49 Warning — 삼각형 + 느낌표
- [ ] #50 Error — 원 + X 표시
- [ ] #51 Physics — 스프링 코일 + 무게추 공

## Style Notes

P0/P1/P2 결과물 기반 스타일 가이드:

- **선 굵기**: P0의 prev_key/next_key/undo_v2/redo_v2 수준 (약 40-50px @1024)
- **화살촉**: 열린 삼각형, 채우지 않음 (undo_v2/redo_v2 참고)
- **원호/곡선**: 균일한 두께, 끝이 살짝 잘린 형태 (undo_v2 참고)
- **기하학**: 직선은 날카롭고, 곡선은 매끄럽게
- **배치**: 캔버스 중앙, 좌우/상하 대칭 여백
- **피해야 할 것**: 솔리드 필(play/pause 스타일), 과도한 디테일(save 스타일)

### P3 추가 주의사항

- **트랙 라벨 용도**: #42~#48은 타임라인에서 `[R]`, `[T]`, `[S]` 등 텍스트를 대체하므로 16px에서도 각각 구분 가능해야 함
- **고유 실루엣**: 7개 속성 아이콘이 나란히 놓였을 때 즉시 구분 가능해야 함
  - Rotate: 원형 / Translate: 십자 / Scale: 대각선 / Shear: 기울어진 사각 / Color: 물방울 / Order: 줄+화살표 / Event: 깃발
- **#42 vs P1 #23**: Rotate(#42)는 중심 피벗 점 포함, Reload(P1 #23)는 피벗 없음으로 구분
- **#49, #50**: 상태 아이콘은 색상 틴트로 최종 구분 (Warning → tertiary/#ff5450, Error → 동일 또는 별도 빨강)
- **#51 vs P2 #39**: Physics(#51)는 스프링+공, IK(P2 #39)는 뼈체인+타겟으로 구분
