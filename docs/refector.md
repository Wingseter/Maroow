  Maroow 프로젝트 종합 분석

  프로젝트 현황 요약

  ┌────────────────┬───────────────────────────┐
  │      항목      │           수치            │
  ├────────────────┼───────────────────────────┤
  │ 소스 파일      │ 48개 (.cpp/.c/.hpp)       │
  ├────────────────┼───────────────────────────┤
  │ 공개 헤더      │ 15개                      │
  ├────────────────┼───────────────────────────┤
  │ 빌드 타겟      │ 22개 (전부 빌드 성공)     │
  ├────────────────┼───────────────────────────┤
  │ 유닛 테스트    │ 27개 (전부 통과)          │
  ├────────────────┼───────────────────────────┤
  │ PRD 스토리     │ 117개 중 117 완료 (100%)  │
  ├────────────────┼───────────────────────────┤
  │ shell_main.cpp │ 9,334줄 (분리 후)         │
  └────────────────┴───────────────────────────┘

  ---
  1. 뷰포트 렌더링 — 파이프라인 정상 + 애니메이션 재생 연결 완료

  렌더링 파이프라인:
  Skeleton → prepare_setup_pose_scene() → PreparedScene
      → build_render_command_list() → RenderCommandList
      → ViewportRenderer::submit_command_list() → glDrawElements()
      → Framebuffer → ImGui::Image()

  MAR-115 해결: advance_timeline_playback()을 O(n) 전체 재구축에서
  O(1) 증분 업데이트(animation_state->update(delta) + apply(skeleton))로 변경.
  Play 버튼 클릭 시 refresh_preview_pose() 1회 초기화 후 증분 진행.

  검증 완료:
  - Region attachment, GPU-skinned mesh, 블렌드 모드, 스텐실 클리핑 통과
  - 카메라 Pan/Zoom, 디버그 오버레이 통과
  - Headless smoke 5프레임 통과

  MAR-116 해결: 인스펙터 프로퍼티 편집 → 라이브 뷰포트 피드백 연결.
  - Bone local pose: DragFloat2/DragFloat 위젯으로 Translate/Rotation/Scale/Shear
    실시간 편집. 변경 시 update_world_transforms() 즉시 호출.
  - Slot color: ColorEdit4 위젯으로 Light color/Dark tint 실시간 편집.
    뷰포트는 매 프레임 slot_states()에서 색상을 읽으므로 즉시 반영.
  - Skin switching: 기존 Checkbox UI → set_preview_skin_enabled() →
    refresh_preview_pose() 체인으로 이미 동작 확인.
  - Weight paint heatmap: MAR-115 이후 GL 프레임버퍼 경로 활성화되어
    텍스처 메시 위에 히트맵 오버레이 정상 렌더링.

  ---
  2. 남은 스토리

  ┌─────────┬────────┬──────────────────────────────────────────────────┐
  │ 스토리  │  상태  │                       내용                       │
  ├─────────┼────────┼──────────────────────────────────────────────────┤
  │ MAR-115 │ done   │ 애니메이션 재생 → 라이브 뷰포트 연결 완료        │
  ├─────────┼────────┼──────────────────────────────────────────────────┤
  │ MAR-116 │ done   │ Weight painting + 인스펙터 프로퍼티 → 뷰포트     │
  ├─────────┼────────┼──────────────────────────────────────────────────┤
  │ MAR-117 │ done   │ Onion skinning 텍스처 고스트 프레임 완료          │
  ├─────────┼────────┼──────────────────────────────────────────────────┤
  │ MAR-119 │ done   │ E2E 에디터 검증 완료                              │
  └─────────┴────────┴──────────────────────────────────────────────────┘

  MAR-119 해결: 전체 에디터 워크플로우 E2E 검증 완료.
  - AC1-AC9 전부 headless smoke + round-trip export로 검증.
  - 27개 유닛 테스트 + headless smoke 5프레임 + renderer_sample 전부 통과.
  - 6개 exported .mskl 파일 marrow_inspect 로드 성공.
  - project_smoke export → fixture_smoke 런타임 재생 → JSON/binary 비교 일치.
  - 검증 결과 AGENTS.md에 문서화 완료.

  MAR-117 해결: Onion skinning을 wireframe-only에서 textured ghost 렌더링으로 업그레이드.
  - ViewportRenderer::render_tinted() 추가 — PreparedScene의 vertex light_color에
    tint_color를 곱해 반투명 틴팅된 캐릭터 렌더링.
  - render_viewport_framebuffer()에서 build_onion_skin_sample_specs()로 고스트 시간 계산,
    각 시간에 skeleton sampling → prepare_setup_pose_scene() → render_tinted() 파이프라인.
  - Before-ghost: cool blue (0.38, 0.67, 1.0), After-ghost: warm red (1.0, 0.55, 0.40).
  - Alpha = onion_skin_alpha(distance_rank), 거리에 따라 0.48→0.08 감소.
  - 렌더 순서: background wireframe → 먼 고스트→가까운 고스트 → 메인 캐릭터 → overlay.
  - 기존 wireframe ghost bone 렌더링 유지 (smoke test 호환 + fallback path).

  ---
  3. shell_main.cpp 분리 완료

  God file(15,432줄) → 4개 파일로 분리:

  ┌──────────────────────┬───────┬──────────────────────────────────────┐
  │         파일         │  줄   │                 역할                 │
  ├──────────────────────┼───────┼──────────────────────────────────────┤
  │ shell_types.hpp      │   936 │ 타입, 상수, 크로스파일 함수 선언     │
  ├──────────────────────┼───────┼──────────────────────────────────────┤
  │ shell_viewport.cpp   │ 1,907 │ GL 리소스, 뷰포트 geometry/render   │
  ├──────────────────────┼───────┼──────────────────────────────────────┤
  │ shell_smoke.cpp      │ 3,568 │ Headless smoke, hot-reload 검증     │
  ├──────────────────────┼───────┼──────────────────────────────────────┤
  │ shell_main.cpp       │ 9,334 │ UI windows, timeline, undo, main()  │
  └──────────────────────┴───────┴──────────────────────────────────────┘

  namespace marrow::editor::shell 도입. 익명 namespace에서 탈피하여
  파일 간 함수 공유 가능. 기능 변경 없이 순수 코드 이동.

  ---
  4. 잘 되고 있는 부분

  - 런타임 엔진 — 완성도 높음, 모든 Spine 4.2 기능 구현
  - 성능 — 목표 대비 5-6배 빠름 (15.38 us/skeleton vs 목표 76)
  - 임포터 — 5개 공식 Spine 예제 전부 임포트 성공
  - 바이너리 포맷 — 양자화/압축 완료
  - C API — 바인딩 완료 및 검증
  - 스레드 안전성 — ThreadSanitizer 검증 통과
  - 테스트 커버리지 — smoke 테스트가 매우 철저

  ---
  5. 프로젝트 완료

  PRD 117개 스토리 전부 완료 (100%). 런타임 엔진, 렌더러, 에디터, 임포터,
  바이너리 포맷, C API, 스레드 안전성 모두 검증됨.
