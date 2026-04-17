# P2: Tree View & Hierarchy Icon Prompts

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

- **P2 특이사항**: 트리뷰 노드 아이콘은 16~24px에서 사용되므로, 단순하고 즉시 식별 가능한 형태가 핵심. 복잡한 디테일보다 명확한 실루엣 우선.

---

## Icons (#28 ~ #41)

### #28 — Bone Node (`node_bone.png`)

```
A minimal outline icon of a single animation rig bone viewed vertically:
a small circle at the top, a small circle at the bottom, connected by a
tapered diamond-shaped body widening in the middle. The bone points downward.
This represents a bone node in an animation skeleton hierarchy tree.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #29 — Slot (`node_slot.png`)

```
A minimal outline icon of a horizontal rectangle with a small square
tab or notch on its left edge, like a labeled container or drawer slot.
The rectangle is wider than tall, representing a slot container that
holds image attachments in an animation rig hierarchy.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #30 — Region Attachment (`att_region.png`)

```
A minimal outline icon of a simple rectangular picture frame: a rectangle
with a smaller rectangle inside forming a border, and two diagonal lines
crossing inside from corner to corner forming an X pattern. This represents
a flat 2D image or sprite region attachment in an animation editor.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #31 — Mesh Attachment (`att_mesh.png`)

```
A minimal outline icon of a small rectangular shape subdivided into
triangular mesh cells with visible dots at each vertex intersection.
Show approximately 4-6 triangles within the rectangle to indicate
a deformable mesh grid. This represents a mesh attachment that can
be deformed for 2D animation.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #32 — Linked Mesh (`att_linked.png`)

```
A minimal outline icon showing two small rectangles side by side,
each subdivided into a few triangles (representing meshes), connected
by two interlocking chain link ovals between them. The chain link
symbol clearly indicates these meshes share vertex data.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #33 — Point Attachment (`att_point.png`)

```
A minimal outline icon of a precise crosshair target: a small circle
at the center with four short straight lines extending outward in
the cardinal directions (up, down, left, right), plus a small
arrow indicator extending from the circle at a 45-degree angle
to show rotation direction. This represents a point attachment
with position and rotation in an animation rig.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #34 — Bounding Box Attachment (`att_bbox.png`)

```
A minimal outline icon of an irregular convex polygon with 5 or 6
sides, drawn with dashed lines, with small filled dots at each
vertex corner. The polygon is roughly pentagonal. This represents
a custom polygon bounding box used for hit detection in animation.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #35 — Clipping Attachment (`att_clip.png`)

```
A minimal outline icon of a rectangle with a second polygon shape
overlapping it, and the overlapping area indicated by a different
line pattern (hatched or cross-hatched lines inside the overlap).
A small scissors symbol cutting along the edge of the overlap.
This represents a clipping mask attachment in 2D animation.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #36 — Path Attachment (`att_path.png`)

```
A minimal outline icon of a smooth curved S-shaped bezier path line
with two small square control point handles visible at the curve's
inflection points, and short thin lines connecting each handle to
its anchor point on the curve. This represents a bezier path
attachment used for path-following in animation.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #37 — Animation Clip (`node_anim.png`)

```
A minimal outline icon of a short film strip segment: a vertical
rectangle with two small square sprocket holes on each side (left
and right edges), and a horizontal line dividing the rectangle into
two frames. This represents an animation clip or sequence in the
animation editor's hierarchy tree.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #38 — Skin (`node_skin.png`)

```
A minimal outline icon of three overlapping rectangles stacked with
slight offset to the right and downward (like a deck of cards fanned
out), representing multiple appearance layers or skin variations.
The top rectangle is fully visible while the others peek out from
behind, showing only their edges.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #39 — IK Constraint (`constraint_ik.png`)

```
A minimal outline icon showing a chain of two connected bone segments
(three circles connected by two tapered lines) bending at the middle
joint, with a small crosshair target marker placed away from the
chain's end, and a thin dotted line from the chain end to the target.
This represents inverse kinematics where bones reach toward a target.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #40 — Path Constraint (`constraint_path.png`)

```
A minimal outline icon of a single bone shape (small circle connected
to tapered diamond body) positioned along a curved dotted arc path,
with a small arrow on the path indicating direction of travel.
The bone follows the curve of the path. This represents a path
constraint where a bone follows a bezier curve.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

### #41 — Transform Constraint (`constraint_xform.png`)

```
A minimal outline icon showing two overlapping transform gizmos:
a circular rotation arc with an arrowhead at one end, and a pair
of perpendicular arrows (horizontal and vertical) forming a cross
for translation. A small "=" sign or double arrow between them
indicates the constraint copies one transform to another.
clean vector line art, uniform stroke width, outline style,
no fill, no gradients, no shadows, no text labels,
centered on pure white background, isolated single icon,
high contrast black lines on white, 1024x1024,
professional UI icon for dark theme animation editor
```

---

## Checklist

생성 후 각 아이콘 확인:

### Nodes (구조 요소)
- [ ] #28 Bone Node — 세로 뼈 형태 (위/아래 원 + 다이아몬드 바디)
- [ ] #29 Slot — 가로 사각형 + 왼쪽 탭/노치
- [ ] #37 Animation Clip — 필름 스트립 세그먼트
- [ ] #38 Skin — 겹친 사각형 3장 (카드덱 형태)

### Attachments (어태치먼트 유형)
- [ ] #30 Region — 사각형 프레임 + 대각선 X
- [ ] #31 Mesh — 삼각형 메쉬 셀 + 정점 점
- [ ] #32 Linked Mesh — 두 메쉬 + 체인 링크
- [ ] #33 Point — 십자 조준점 + 회전 화살표
- [ ] #34 Bounding Box — 점선 다각형 + 정점 점
- [ ] #35 Clipping — 겹치는 도형 + 가위
- [ ] #36 Path — S자 베지어 곡선 + 컨트롤 핸들

### Constraints (제약 조건)
- [ ] #39 IK — 뼈 체인 + 타겟 마커 + 점선
- [ ] #40 Path Constraint — 뼈 + 점선 곡선 경로 + 방향 화살표
- [ ] #41 Transform — 겹치는 회전/이동 기즈모

## Style Notes

P0/P1 결과물 기반 스타일 가이드:

- **선 굵기**: P0의 prev_key/next_key/undo_v2/redo_v2 수준 (약 40-50px @1024)
- **화살촉**: 열린 삼각형, 채우지 않음 (undo_v2/redo_v2 참고)
- **원호/곡선**: 균일한 두께, 끝이 살짝 잘린 형태 (undo_v2 참고)
- **기하학**: 직선은 날카롭고, 곡선은 매끄럽게
- **배치**: 캔버스 중앙, 좌우/상하 대칭 여백
- **피해야 할 것**: 솔리드 필(play/pause 스타일), 과도한 디테일(save 스타일)

### P2 추가 주의사항

- **트리뷰 크기**: 16~24px에서 사용되므로, 복잡한 내부 디테일 지양
- **유형 구분**: 같은 카테고리 아이콘끼리 시각적 패밀리 느낌 유지
  - `node_*`: 구조적/컨테이너 느낌
  - `att_*`: 개별 에셋/첨부 느낌
  - `constraint_*`: 관계/연결 느낌 (점선, 화살표 활용)
- **#28 vs P1 #15**: Bone Node(#28)는 세로, Bone Toggle(P1 #15)는 가로로 구분
- **#31 vs P1 #18**: Mesh Att(#31)는 작은 삼각형 격자, Mesh Wire(P1 #18)는 큰 와이어프레임
- **#34 vs P1 #19**: BBox Att(#34)는 불규칙 다각형, BBox(P1 #19)는 점선 사각형
