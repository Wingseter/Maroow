# MAR-167 Synchronized Scalar Graph View Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a read-only scalar graph tab that projects the effective Transform and Slot Color timelines, shares parent-key identity/selection/active key/playhead with the dopesheet, and leaves project/history/runtime/Agent surfaces unchanged.

**Architecture:** Extend the UI-free timeline model with typed track identity, parent-key selection transitions, scalar projection, and plot geometry. Keep shell synchronization in `timeline_controller` and graph-only context/cache/presentation in a new `shell_timeline_graph` unit, then place Dopesheet and Graph bodies behind tabs in the existing Timeline window.

**Tech Stack:** C++17, Dear ImGui, existing Marrow runtime/editor session APIs, CMake/CTest, JSON fixtures.

**Spec:** `docs/superpowers/specs/2026-08-20-mar-167-synchronized-scalar-graph-design.md`

## Global Constraints

- Read `docs/root1/discription.md` and the MAR-167 PRD story before each implementation task; current source and tests override stale plan assumptions.
- Preserve all inherited dirty MAR-165/MAR-166 changes. Do not reset, discard, stash, commit, push, or create a PR unless the user separately requests it.
- Follow strict RED-GREEN-REFACTOR: add one focused failing test, observe the expected failure, write the minimum production code, then rerun focused and affected regression tests.
- Graph source data is the effective runtime animation from the active `EditorSession`, including imported runtime-only keys and current project overlays.
- Display one focused parent track at a time: Rotate Angle; Translate X/Y; Scale X/Y; Shear X/Y; or Slot Color R/G/B/A.
- Parent components share one `TimelineKeyRef` and one outgoing easing. Never introduce component-owned key or easing identity.
- Bone Inherit, Slot Attachment, Mesh Deform/FFD, Draw Order, Events, parameter lanes, and constraint lanes are ineligible.
- MAR-167 is read-only. Do not add point dragging, insertion/deletion, graph clipboard, Bezier handles, curve mutation, or authoring transactions.
- Add no persistent project or preference state. Preserve `.marrow`, `.mskl` v1, `.mbin` v2, C ABI v1, public `include/marrow/**` APIs, GPU ownership, `SelectionSet`, and the 56-operation Agent/MCP contract.
- Automated display gates do not establish manual-visible-UI, Windows 11, or physical-input qualification. Keep MAR-192 through MAR-210 open.
- Each task ends with a read-only diff/status checkpoint instead of a commit because the current milestone baseline is intentionally dirty.

## File and Responsibility Map

**Create**

- `src/editor/timeline_graph_model.hpp` — UI-free scalar projection, graph view transform, curve geometry, fit, pan/zoom, and point-hit contracts.
- `src/editor/timeline_graph_model.cpp` — implementation of projection and plot math.
- `src/tests/timeline_graph_model_tests.cpp` — focused projection/geometry/view/hit tests.
- `src/editor/shell_timeline_graph.hpp` — shell-private graph context/cache/presenter interfaces and render statistics.
- `src/editor/shell_timeline_graph.cpp` — effective-runtime cache, graph context resolution, ImGui presentation, and graph input routing.
- `src/editor/shell_smoke_graph.cpp` — headless shell synchronization and display-only mutation guard.

**Modify**

- `src/editor/timeline_model.hpp/.cpp` — typed track kind, shared parent-key activation, and active-key reconciliation.
- `src/tests/timeline_model_tests.cpp` — active-key transition and reconciliation tests.
- `src/editor/shell_state.hpp` — explicit active parent key and transient graph view/cache state.
- `src/editor/timeline_controller.hpp/.cpp` — shared dopesheet/graph activation entry point and active-key maintenance across timeline mutations.
- `src/editor/shell_timeline.hpp/.cpp` — Dopesheet/Graph tabs and reuse of the shared activation entry point.
- `src/editor/shell_smoke_scenarios.hpp` and `src/editor/shell_smoke.cpp` — register the graph smoke.
- `src/editor/shell_smoke_frames.cpp` — run actual Graph-tab frames and assert render statistics.
- `src/editor/shell_project_panels.cpp` — keep animation-catalog selection resets consistent with the new active key; source adoption already resets the complete `TimelineEditorState` in `shell_core.cpp`.
- `CMakeLists.txt` — compile the new model/presenter/smoke sources and register the focused graph test.
- `AGENTS.md`, `docs/root1/*.md` as required, and `.agents/tasks/prd-marrow-runtime.json` — record verified MAR-167 completion and make MAR-168 next.

---

### Task 1: Shared parent-key activation and explicit active key

**Files:**

- Modify: `src/editor/timeline_model.hpp`
- Modify: `src/editor/timeline_model.cpp`
- Modify: `src/tests/timeline_model_tests.cpp`
- Modify: `src/editor/shell_state.hpp`
- Modify: `src/editor/timeline_controller.hpp`
- Modify: `src/editor/timeline_controller.cpp`
- Modify: `src/editor/shell_timeline.cpp`
- Modify: `src/editor/shell_project_panels.cpp`

**Interfaces:**

- Consumes: existing `TimelineKeyRef`, `timeline_key_ref()`, `focus_timeline_track()`, and `scrub_timeline_time()`.
- Produces:

```cpp
void apply_key_activation(
    std::vector<KeyRef>* selection,
    std::optional<KeyRef>* active_key,
    const KeyRef& clicked_key,
    bool additive);

void reconcile_selection(
    std::vector<KeyRef>* selection,
    std::optional<KeyRef>* active_key,
    const std::vector<TrackRow>& tracks);
```

```cpp
bool activate_timeline_key(
    ShellState* state,
    const TimelineTrackRow& track,
    std::size_t key_index,
    bool additive,
    std::string_view source,
    bool update_status_message);
```

- `TimelineEditorState::active_key` is empty or resolves in current tracks and occurs in `selected_keys`.

- [ ] **Step 1: Add failing UI-free activation tests**

In `src/tests/timeline_model_tests.cpp`, add a case that names the production change explicitly:

```cpp
void test_parent_key_activation_and_active_fallback(TestSuite& suite) {
    const model::TrackRow row = track({0.1, 0.2, 0.3});
    const model::KeyRef first = model::key_ref(row, 0U);
    const model::KeyRef second = model::key_ref(row, 1U);
    const model::KeyRef third = model::key_ref(row, 2U);

    std::vector<model::KeyRef> selection{first, second};
    std::optional<model::KeyRef> active = first;

    model::apply_key_activation(&selection, &active, second, false);
    suite.expect(
        selection == std::vector<model::KeyRef>{first, second} &&
            active == std::optional<model::KeyRef>(second),
        "plain activation of a selected key must preserve the group and move active");

    model::apply_key_activation(&selection, &active, third, false);
    suite.expect(
        selection == std::vector<model::KeyRef>{third} &&
            active == std::optional<model::KeyRef>(third),
        "plain activation of an unselected key must replace selection");

    model::apply_key_activation(&selection, &active, first, true);
    suite.expect(
        selection == std::vector<model::KeyRef>{third, first} &&
            active == std::optional<model::KeyRef>(first),
        "additive insertion must append and activate the clicked key");

    model::apply_key_activation(&selection, &active, first, true);
    suite.expect(
        selection == std::vector<model::KeyRef>{third} &&
            active == std::optional<model::KeyRef>(third),
        "removing the active key must choose the last stable remaining key");

    const model::TrackRow reduced = track({0.1});
    model::reconcile_selection(&selection, &active, {reduced});
    suite.expect(
        selection.empty() && !active.has_value(),
        "reconciliation must clear an active key whose identity no longer resolves");
}
```

Register the case in `main()`. Extend the existing same-time reconciliation case so duplicate removal preserves a valid active identity.

- [ ] **Step 2: Run the test and observe RED**

Run:

```bash
cmake --build build --target marrow_timeline_model_tests -j4
```

Expected: compilation fails because `apply_key_activation` and the three-argument `reconcile_selection` overload do not exist.

- [ ] **Step 3: Implement the UI-free transition and reconciliation**

Add the declarations above. Implement the exact stable-order rules:

```cpp
void apply_key_activation(
    std::vector<KeyRef>* selection,
    std::optional<KeyRef>* active_key,
    const KeyRef& clicked_key,
    bool additive) {
    if (selection == nullptr || active_key == nullptr) return;
    const auto found = std::find(selection->begin(), selection->end(), clicked_key);
    if (!additive) {
        if (found == selection->end()) *selection = {clicked_key};
        *active_key = clicked_key;
        return;
    }
    if (found == selection->end()) {
        selection->push_back(clicked_key);
        *active_key = clicked_key;
        return;
    }
    const bool removed_active = *active_key == std::optional<KeyRef>(clicked_key);
    selection->erase(found);
    if (removed_active || !active_key->has_value() ||
        std::find(selection->begin(), selection->end(), **active_key) == selection->end()) {
        *active_key = selection->empty()
            ? std::nullopt
            : std::optional<KeyRef>(selection->back());
    }
}
```

The new reconciliation overload must call the existing two-argument reconciliation, then preserve the active key only if it remains selected; otherwise select `selection->back()` or clear it.

- [ ] **Step 4: Run the model tests and observe GREEN**

Run:

```bash
cmake --build build --target marrow_timeline_model_tests -j4
./build/marrow_timeline_model_tests
```

Expected: the new activation case and every existing timeline model case pass.

- [ ] **Step 5: Add a failing shell-level shared activation test**

In `src/editor/shell_smoke_timeline.cpp`, extend the early timeline selection block to set:

```cpp
state.timeline_editor.selected_keys = {
    timeline_key_ref(source_translate, 0U),
    timeline_key_ref(source_translate, 1U)};
state.timeline_editor.active_key = state.timeline_editor.selected_keys.front();
```

Then call the new controller API for the second key and assert:

```cpp
if (!activate_timeline_key(
        &state, source_translate, 1U, false, "Dopesheet smoke", false) ||
    state.timeline_editor.selected_keys.size() != 2U ||
    state.timeline_editor.active_key !=
        std::optional<TimelineKeyRef>(timeline_key_ref(source_translate, 1U)) ||
    std::abs(state.timeline_time_seconds - source_translate.key_times[1U]) > 1e-6) {
    std::cerr << "Shared timeline activation did not preserve selection, active key, and playhead.\n";
    return false;
}
```

- [ ] **Step 6: Run the shell build and observe RED**

Run:

```bash
cmake --build build --target marrow_editor_shell -j4
```

Expected: compilation fails because `TimelineEditorState::active_key` and `activate_timeline_key()` do not exist.

- [ ] **Step 7: Implement the shared shell contract and refactor the dopesheet**

Add `std::optional<TimelineKeyRef> active_key` immediately after `selected_keys` in `TimelineEditorState`.

Implement `activate_timeline_key()`:

1. reject null state and out-of-range key index;
2. pause playback;
3. call `focus_timeline_track()` with the key time;
4. call `timeline_model::apply_key_activation()`;
5. return true after the playhead and selection are synchronized.

Replace the inline point-selection block in `draw_timeline_lane()` with this API. Preserve the existing retime start condition by calling `begin_timeline_retime_gesture()` only when the clicked parent key remains selected and the track is editable.

When plain empty-lane box selection clears `selected_keys`, clear `active_key`. When a box completes, call the three-argument reconciliation overload so the active fallback is deterministic.

Update every timeline mutation path:

- animation change and animation-catalog selection change: clear `active_key` with `selected_keys`;
- add/replace key: set `active_key` to the rebuilt selected key;
- remove/cut/paste paths that clear selection: clear `active_key`;
- retime preview: map the prior active identity through the same rebuilt index as `selected_keys`;
- project/source adoption through `TimelineEditorState{}` resets both automatically.

- [ ] **Step 8: Run focused and affected regression tests**

Run:

```bash
cmake --build build --target marrow_timeline_model_tests marrow_editor_shell -j4
./build/marrow_timeline_model_tests
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

Expected: model tests pass and the existing editor smoke completes without active-key invariant failures.

- [ ] **Step 9: Refactor only after GREEN**

Remove any duplicated “find/toggle/replace active key” logic left in `shell_timeline.cpp`. Do not refactor entity `SelectionSet` or unrelated timeline authoring functions.

- [ ] **Step 10: Record a no-commit checkpoint**

Run:

```bash
git diff --check
git status --short
```

Confirm only intended Task 1 additions overlap the inherited dirty tree. Do not stage or commit.

---

### Task 2: Typed scalar-track projection from effective runtime data

**Files:**

- Create: `src/editor/timeline_graph_model.hpp`
- Create: `src/editor/timeline_graph_model.cpp`
- Create: `src/tests/timeline_graph_model_tests.cpp`
- Modify: `src/editor/timeline_model.hpp`
- Modify: `src/editor/timeline_model.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: `TimelineTrackRow`, `TimelineKeyRef`, `AnimationData::find_*_timeline()`, and runtime `Interpolation`.
- Produces:

```cpp
enum class TimelineTrackKind : std::uint8_t {
    Unknown,
    Rotate,
    Translate,
    Scale,
    Shear,
    Inherit,
    SlotAttachment,
    SlotColor,
    Deform,
    DrawOrder,
    Event,
};
```

```cpp
namespace marrow::editor::timeline_graph_model {

enum class TrackKind : std::uint8_t {
    Rotate, Translate, Scale, Shear, SlotColor,
};
enum class Component : std::uint8_t {
    Angle, X, Y, Red, Green, Blue, Alpha,
};
enum class ProjectionStatus : std::uint8_t {
    Ready, UnsupportedTrack, MissingSource, InvalidData,
};

struct ComponentDescriptor {
    Component component{Component::Angle};
    std::string_view label;
};
struct Key {
    timeline_model::KeyRef identity;
    double time_seconds{0.0};
    std::array<double, 4> values{};
    std::size_t value_count{0U};
    marrow::runtime::Interpolation outgoing_easing{};
};
struct Track {
    std::string track_id;
    std::string label;
    TrackKind kind{TrackKind::Rotate};
    std::vector<ComponentDescriptor> components;
    std::vector<Key> keys;
};
struct Projection {
    ProjectionStatus status{ProjectionStatus::UnsupportedTrack};
    std::optional<Track> track;
};

bool track_is_supported(const timeline_model::TrackRow& track) noexcept;
Projection project_track(
    const marrow::runtime::AnimationData& animation,
    const timeline_model::TrackRow& track);

}  // namespace marrow::editor::timeline_graph_model
```

- [ ] **Step 1: Add the focused graph-test target and failing projection test**

Add `src/tests/timeline_graph_model_tests.cpp` with the same small `TestSuite` pattern used by `timeline_model_tests.cpp`. The first test must load `assets/fixtures/player_idle.marrow`, build effective tracks, project only supported tracks, and assert six parent tracks and thirteen components:

```cpp
void test_player_idle_supported_projection(TestSuite& suite) {
    auto loaded = marrow::editor::load_project("assets/fixtures/player_idle.marrow");
    suite.expect(static_cast<bool>(loaded), "fixture project must load");
    if (!loaded) return;
    const auto* animation = loaded.skeleton_data->find_animation("idle");
    suite.expect(animation != nullptr, "idle animation must resolve");
    if (animation == nullptr) return;

    const auto rows = model::build_tracks(*loaded.skeleton_data, *animation);
    std::size_t parent_count = 0U;
    std::size_t component_count = 0U;
    for (const auto& row : rows) {
        const graph::Projection projected = graph::project_track(*animation, row);
        if (projected.status != graph::ProjectionStatus::Ready) continue;
        ++parent_count;
        component_count += projected.track->components.size();
        for (const auto& key : projected.track->keys) {
            suite.expect(
                model::key_index(row, key.identity).has_value(),
                "graph key must reuse the dopesheet parent identity");
        }
    }
    suite.expect(parent_count == 6U, "fixture must expose six supported parent tracks");
    suite.expect(component_count == 13U, "fixture must expose thirteen scalar series");
}
```

Also assert every Inherit, Attachment, Deform, Draw Order, and Event row returns `UnsupportedTrack`.

Add:

```cmake
add_executable(marrow_timeline_graph_model_tests
    src/tests/timeline_graph_model_tests.cpp
)
target_include_directories(marrow_timeline_graph_model_tests PRIVATE
    ${PROJECT_SOURCE_DIR}/src/editor
)
target_link_libraries(marrow_timeline_graph_model_tests PRIVATE
    marrow_editor
)
```

Register a CTest named `marrow.timeline_graph_model` with working directory
`${PROJECT_SOURCE_DIR}` and labels
`editor;timeline;graph;unit;noninteractive`.

Declare `namespace model = marrow::editor::timeline_model;` and
`namespace graph = marrow::editor::timeline_graph_model;` at the top of the
test file so every later snippet uses the same aliases.

- [ ] **Step 2: Configure/build and observe RED**

Run:

```bash
cmake -S . -B build
cmake --build build --target marrow_timeline_graph_model_tests -j4
```

Expected: compilation fails because `timeline_graph_model.hpp` and its projection types/functions do not exist.

- [ ] **Step 3: Add typed track kinds without changing existing IDs**

Append `TimelineTrackKind kind{TimelineTrackKind::Unknown}` to `TrackRow` so existing aggregate test fixtures remain source-compatible. Update only `build_tracks()` constructors to assign the exact kind listed above. Keep existing `track.id` values and `track_is_editable()` behavior unchanged.

Pass `TimelineTrackKind` through the bone/slot builder lambdas so the mapping exists in one place. Add model-test assertions that each fixture row has the expected typed kind while IDs remain byte-identical.

- [ ] **Step 4: Implement minimal projection**

Implement `track_is_supported()` as a switch over `TimelineTrackKind`, not an ID substring check.

Implement `project_track()` with these mappings:

```cpp
Rotate    -> {Angle}, value = timeline.setup_rotation + key.angle
Translate -> {X, Y}, values = {key.x, key.y}
Scale     -> {X, Y}, values = {key.x, key.y}
Shear     -> {X, Y}, values = {key.x, key.y}
SlotColor -> {R, G, B, A}, values = {color.r, color.g, color.b, color.a}
```

Do not normalize or wrap Rotate values; raw multi-turn local angles must remain
visible exactly as authored.

For each projected key:

- require the runtime source timeline to exist;
- require source-key count and `TrackRow::key_times` count to match;
- require `time_identity(source.time) == identity.time_microseconds`;
- require finite time and component values;
- for Cubic easing, require all four control values finite;
- store the parent `key_ref(row, index)` and the single source-key interpolation.

Return `MissingSource` for a supported row whose source timeline cannot be resolved and `InvalidData` for mismatch/non-finite data. Never return a partially populated track.

Add `src/editor/timeline_graph_model.cpp` to the existing
`marrow_timeline_model` target in this step, after the failing test has already
proved the interface is missing.

- [ ] **Step 5: Add a failing absolute-rotation/shared-easing test**

Construct a synthetic `AnimationData` with a Rotate timeline whose `setup_rotation` is 30 and key angles are 5 and 15. Assert projected values are 35 and 45 and both keys reuse the row's `TimelineKeyRef`. Construct a Slot Color key and assert its four components share that same one parent identity and one outgoing interpolation object/kind.

Before updating the implementation, run:

```bash
cmake --build build --target marrow_timeline_graph_model_tests -j4
./build/marrow_timeline_graph_model_tests
```

Expected RED: the new absolute-rotation or shared-easing assertion fails.

- [ ] **Step 6: Complete projection and observe GREEN**

Implement the missing setup-rotation and four-channel mapping. Run:

```bash
cmake --build build --target marrow_timeline_graph_model_tests marrow_timeline_model_tests -j4
./build/marrow_timeline_graph_model_tests
./build/marrow_timeline_model_tests
```

Expected: both focused model suites pass.

- [ ] **Step 7: Record a no-commit checkpoint**

Run `git diff --check` and `git status --short`. Confirm no public header, project schema, or Agent registry file changed. Do not stage or commit.

---

### Task 3: UI-free curve geometry, auto-fit, view transforms, and hit testing

**Files:**

- Modify: `src/editor/timeline_graph_model.hpp`
- Modify: `src/editor/timeline_graph_model.cpp`
- Modify: `src/tests/timeline_graph_model_tests.cpp`

**Interfaces:**

- Consumes: Task 2 `Track`, parent `Key` values/easing, and component order.
- Produces:

```cpp
struct PlotRect {
    double min_x{0.0}, min_y{0.0}, max_x{0.0}, max_y{0.0};
};
struct View {
    double view_start_seconds{0.0};
    double pixels_per_second{160.0};
    double value_center{0.0};
    double pixels_per_value{100.0};
};
struct PlotPoint {
    double x{0.0}, y{0.0};
};
enum class SegmentKind : std::uint8_t { Linear, Stepped, Cubic };
struct Point {
    PlotPoint position;
    timeline_model::KeyRef key;
    Component component{Component::Angle};
    std::size_t component_index{0U};
};
struct Segment {
    SegmentKind kind{SegmentKind::Linear};
    Component component{Component::Angle};
    std::vector<PlotPoint> polyline;
    PlotPoint marker;
};
struct Geometry {
    std::vector<Point> points;
    std::vector<Segment> segments;
    std::optional<double> playhead_x;
};
struct PointHit {
    timeline_model::KeyRef key;
    Component component{Component::Angle};
    std::size_t component_index{0U};
};

std::optional<View> fit_view(
    const Track& track,
    const std::array<bool, 4>& visible,
    PlotRect rect,
    double frames_per_second);
bool zoom_time_at(View* view, PlotRect rect, double cursor_x, double wheel_delta);
bool zoom_value_at(View* view, PlotRect rect, double cursor_y, double wheel_delta);
bool pan_view(View* view, double delta_x, double delta_y);
std::optional<Geometry> build_geometry(
    const Track& track,
    const std::array<bool, 4>& visible,
    const View& view,
    PlotRect rect,
    double playhead_time);
std::optional<PointHit> hit_test(
    const Geometry& geometry,
    double pointer_x,
    double pointer_y,
    double inclusive_radius = 8.0);
std::optional<double> nice_tick_interval(
    double pixels_per_unit,
    double minimum_logical_spacing);
```

- [ ] **Step 1: Add failing segment-geometry tests**

Create one two-key parent track for each easing and assert:

```cpp
const auto linear = graph::build_geometry(
    make_track(Interpolation::linear(), 0.0, 10.0),
    {true, false, false, false}, view, rect, 0.5);
suite.expect(
    linear && linear->segments.size() == 1U &&
        linear->segments[0].kind == graph::SegmentKind::Linear &&
        linear->segments[0].polyline.size() == 2U,
    "linear easing must produce one straight two-point segment");

const auto stepped = graph::build_geometry(
    make_track(Interpolation::stepped(), 0.0, 10.0),
    {true, false, false, false}, view, rect, 0.5);
suite.expect(
    stepped && stepped->segments[0].kind == graph::SegmentKind::Stepped &&
        stepped->segments[0].polyline.size() == 3U &&
        near(stepped->segments[0].polyline[1].y,
             stepped->segments[0].polyline[0].y),
    "stepped easing must hold then jump at the next key");

const auto cubic = graph::build_geometry(
    make_track(Interpolation::cubic_bezier(0.25, 0.1, 0.75, 0.9), 0.0, 10.0),
    {true, false, false, false}, view, rect, 0.5);
suite.expect(
    cubic && cubic->segments[0].kind == graph::SegmentKind::Cubic &&
        cubic->segments[0].polyline.size() > 2U,
    "cubic easing must generate adaptive intermediate points");
```

Also assert the last key creates no outgoing segment and a non-finite easing sample rejects the entire geometry.
Assert key points whose centers are outside `PlotRect` are omitted from
`Geometry::points`, and `playhead_x` is empty when the playhead is outside the
plot. This prevents invisible off-canvas points from winning hit tests.

- [ ] **Step 2: Build and observe RED**

Run:

```bash
cmake --build build --target marrow_timeline_graph_model_tests -j4
```

Expected: compilation fails because `View`, `Geometry`, and `build_geometry()` do not exist.

- [ ] **Step 3: Implement coordinate mapping and easing geometry**

Use:

```cpp
screen_x = rect.min_x + (time - view.view_start_seconds) * view.pixels_per_second;
screen_y = midpoint_y - (value - view.value_center) * view.pixels_per_value;
```

Linear emits start/end. Stepped emits start, `{end_time, start_value}`, end. Cubic recursively compares the actual transformed midpoint to the straight midpoint in plot space; split until deviation is at most 0.5 logical pixels or depth reaches 10. Any non-finite transformed alpha or coordinate returns `std::nullopt` for the complete geometry.

Keep segment polylines for draw-list clipping, but add point markers and the
playhead to `Geometry` only when their centers lie inside `PlotRect`.

Store a midpoint tick location for Linear, the right-angle location for Stepped, and the `u=0.5` curve point for Cubic in `Segment::marker`.

- [ ] **Step 4: Run geometry tests and observe GREEN**

Run `./build/marrow_timeline_graph_model_tests`. Expected: Linear, Stepped, Cubic, final-key, and invalid-data cases pass.

- [ ] **Step 5: Add failing fit/pan/zoom tests**

Add tests for:

- time range with 5% padding and minimum one-frame span;
- visible value range with 10% padding;
- flat lane padding `max(abs(value) * 0.1, 1e-3)`;
- Slot Color fit including at least `[0, 1]`;
- cubic overshoot included in fitted bounds;
- cursor time/value remaining stable across zoom;
- right/down middle-drag deltas applying `view_start -= dx / pps` and `value_center += dy / ppv`;
- time zoom clamped to `0.01..1600` pixels/second so long tracks remain fully
  fittable;
- finite positive value zoom and no mutation on non-finite input;
- no read or write of animation duration.
- 1/2/5 × power-of-ten tick selection returning at least the requested logical
  spacing and rejecting non-finite or non-positive input.

Run the focused test before implementation. Expected RED: fit/zoom/pan functions are missing.

- [ ] **Step 6: Implement deterministic two-pass fit and view operations**

`fit_view()` must:

1. collect visible key values and seed bounds;
2. form a provisional finite view;
3. build adaptive segment samples with that view;
4. expand value bounds to include every sampled overshoot;
5. apply final padding and clamps.

If the final geometry reveals an additional sampled bound outside the provisional fit, expand once and rebuild. The depth cap makes the pass bounded.

`zoom_time_at()` and `zoom_value_at()` use `pow(1.15, wheel_delta)`, preserve the value under the cursor, and reject non-finite parameters without modifying `View`. `pan_view()` follows the signed equations listed in Step 5.

`nice_tick_interval()` computes the target unit span from
`minimum_logical_spacing / pixels_per_unit`, then chooses the first value in
`{1, 2, 5, 10} * pow(10, floor(log10(target)))` that is not smaller than the
target. The presenter uses 72 logical pixels for time ticks and 48 for value
ticks.

- [ ] **Step 7: Add failing inclusive-hit and tie tests**

Build geometry containing coincident points and assert:

- a point exactly 8 logical pixels away hits;
- a point farther than 8 pixels misses;
- minimum Euclidean distance wins;
- exact ties use lower time, lower same-time ordinal, then component order `Angle, X, Y, R, G, B, A`.

Run the test before implementation. Expected RED: `hit_test()` is missing.

- [ ] **Step 8: Implement hit testing and observe GREEN**

Compare squared distances, include `distance_squared <= radius * radius`, and use a stable tuple comparator after equal distance. Run:

```bash
cmake --build build --target marrow_timeline_graph_model_tests -j4
./build/marrow_timeline_graph_model_tests
```

Expected: all projection, geometry, fit, zoom, pan, invalid-input, and hit cases pass.

- [ ] **Step 9: Record a no-commit checkpoint**

Run `git diff --check` and `git status --short`. Do not stage or commit.

---

### Task 4: Shell graph context, cache, and synchronization smoke

**Files:**

- Create: `src/editor/shell_timeline_graph.hpp`
- Create: `src/editor/shell_timeline_graph.cpp`
- Create: `src/editor/shell_smoke_graph.cpp`
- Modify: `src/editor/shell_state.hpp`
- Modify: `src/editor/shell_smoke_scenarios.hpp`
- Modify: `src/editor/shell_smoke.cpp`
- Modify: `CMakeLists.txt`

**Interfaces:**

- Consumes: Task 1 activation controller, Task 2 projection, Task 3 view math, `cached_timeline_tracks()`, and `resolve_shell_selection()`.
- Produces:

```cpp
enum class TimelineViewMode : std::uint8_t { Dopesheet, Graph };

struct TimelineGraphProjectionCache {
    std::uint64_t runtime_revision{0U};
    const marrow::runtime::SkeletonData* skeleton_identity{nullptr};
    std::string animation_name;
    std::string track_id;
    timeline_graph_model::Projection projection;
    std::uint64_t generation{0U};
    bool valid{false};
};

struct TimelineGraphViewState {
    timeline_graph_model::View view{};
    std::array<bool, 4> component_visible{true, true, true, true};
    std::optional<timeline_graph_model::Component> active_component;
    std::string fitted_track_id;
    std::string fitted_animation_name;
    bool needs_fit{true};
};
```

```cpp
const TimelineTrackRow* resolve_timeline_graph_track(
    const ShellState& state,
    const std::vector<TimelineTrackRow>& tracks);

const timeline_graph_model::Projection& cached_timeline_graph_projection(
    ShellState* state,
    const TimelineTrackRow& track);

bool activate_timeline_graph_point(
    ShellState* state,
    const TimelineTrackRow& track,
    const timeline_graph_model::PointHit& point,
    bool additive,
    std::string_view source);
```

- [ ] **Step 1: Add and register the failing shell smoke**

Declare:

```cpp
bool validate_timeline_graph_shell_smoke(
    const std::filesystem::path& project_path);
```

Call it from `run_headless_smoke()` after the FFD smoke and before the reused `ShellState` scenario chain, so it has an isolated project/session.

Create `shell_smoke_graph.cpp`. Load `player_idle`, select `idle`, build tracks, and assert context resolution:

1. a focused supported track wins;
2. with no focused track, the first supported row matching the active bone/slot is displayed without mutating `selected_timeline_track_id`;
3. a focused Attachment/FFD/discrete row produces an unsupported projection instead of reusing the previous graph.

Capture:

```cpp
const std::string project_before =
    marrow::editor::serialize_project(*state.session.project());
const std::size_t undo_before = state.session.undo_count();
const std::size_t redo_before = state.session.redo_count();
const std::uint64_t project_revision_before = state.session.project_revision();
const std::uint64_t runtime_revision_before = state.session.runtime_revision();
const std::size_t operation_count_before =
    marrow::editor::agent_operation_descriptor_count();
```

Activate a Translate graph point, toggle it additively, pan/zoom/fit the transient view, and assert shared selection, `active_key`, parent track, and playhead. At the end assert every captured project/history/runtime/Agent value remains unchanged. Do not assert preview revision because scrubbing intentionally changes it.

- [ ] **Step 2: Add sources and build to observe RED**

Add only `shell_smoke_graph.cpp` to `marrow_editor_shell` for the RED step. Its
include of the still-missing `shell_timeline_graph.hpp` must be the expected
compile failure; do not create an empty production source to satisfy CMake.

Run:

```bash
cmake -S . -B build
cmake --build build --target marrow_editor_shell -j4
```

Expected: compilation fails because the graph state/cache/context interfaces are missing.

- [ ] **Step 3: Add the exact transient state contract**

Add all of the following directly to `TimelineEditorState` so
`TimelineEditorState{}` source adoption resets them atomically:

```cpp
TimelineViewMode view_mode{TimelineViewMode::Dopesheet};
std::optional<TimelineViewMode> requested_view_mode;
std::optional<TimelineKeyRef> active_key;
TimelineGraphViewState graph_view{};
TimelineGraphProjectionCache graph_cache{};
```

Task 1 already adds `active_key`; retain that single field rather than adding a
duplicate.

Do not implement cache lookup in `shell_state.hpp`; it owns data only. The cache
implementation belongs in `shell_timeline_graph.cpp` in the next step.

- [ ] **Step 4: Implement cache, context, and graph-point activation**

Create `shell_timeline_graph.hpp/.cpp`, add `shell_timeline_graph.cpp` to
`marrow_editor_shell`, and implement cache matching with all of:

```cpp
cache.runtime_revision == state.session.runtime_revision()
cache.skeleton_identity == state.session.runtime_data()
cache.animation_name == state.selected_animation_name
cache.track_id == track.id
```

On a miss, project from `*selected_animation(state)`, replace the cached value, and increment `generation`. Store no timeline/key pointers.

Do not auto-fit merely because runtime revision changed on the same track. Rebuild projection and preserve the finite view. Mark `needs_fit` only when the track or animation context changes, the view is invalid, or the user requests Fit.

On track or animation context change, reset `component_visible` to every
available component, clear `active_component`, and auto-fit. This prevents
Translate X/Y visibility bits from being reinterpreted as Slot Color R/G.

`resolve_timeline_graph_track()`:

1. return the focused row whenever its ID resolves; the projection layer will
   return `UnsupportedTrack` for an ineligible focused row;
2. only when no focused row resolves, iterate tracks in established order and
   return the first supported row whose bone/slot matches `ResolvedSelection`
   active context;
3. otherwise return null.

`activate_timeline_graph_point()` resolves the parent key through `timeline_key_index()`, calls `activate_timeline_key()`, and only after success records `graph_view.active_component`. It does not start an edit transaction.

These production files are created only now, after the expected RED result has
been observed.

- [ ] **Step 5: Run the isolated shell smoke and observe GREEN**

Run:

```bash
cmake --build build --target marrow_editor_shell -j4
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

Expected: the graph synchronization smoke passes and the existing headless shell scenarios remain green.

- [ ] **Step 6: Add cache/lifecycle regression assertions**

Extend the smoke so:

- repeated access with the same cache key keeps `generation` unchanged;
- runtime revision change rebuilds projection and increments `generation` but preserves `View` for the same track;
- animation/track context change sets `needs_fit` and then replaces `fitted_track_id/fitted_animation_name` after fit;
- `TimelineEditorState{}` source adoption resets Dopesheet mode, graph visibility, active component, view, cache, selected keys, and active key;
- same-project undo/redo with the same track preserves a finite view while active-key reconciliation removes only stale identities.

Run the editor shell again. Expected: all lifecycle assertions pass.

- [ ] **Step 7: Record a no-commit checkpoint**

Run `git diff --check` and `git status --short`. Confirm no project schema or Agent registry source changed. Do not stage or commit.

---

### Task 5: ImGui Graph tab, rendering, and actual frame smoke

**Files:**

- Modify: `src/editor/shell_timeline_graph.hpp`
- Modify: `src/editor/shell_timeline_graph.cpp`
- Modify: `src/editor/shell_timeline.hpp`
- Modify: `src/editor/shell_timeline.cpp`
- Modify: `src/editor/shell_smoke_frames.cpp`
- Modify: `src/editor/shell_smoke_graph.cpp`

**Interfaces:**

- Consumes: Task 3 geometry/hit functions and Task 4 context/cache/state.
- Produces:

```cpp
struct TimelineGraphRenderStats {
    timeline_graph_model::ProjectionStatus status{
        timeline_graph_model::ProjectionStatus::UnsupportedTrack};
    std::size_t point_count{0U};
    std::size_t linear_segment_count{0U};
    std::size_t stepped_segment_count{0U};
    std::size_t cubic_segment_count{0U};
    bool playhead_drawn{false};
};

TimelineGraphRenderStats draw_timeline_graph_body(
    ShellState* state,
    const std::vector<TimelineTrackRow>& tracks);

void draw_timeline_window(
    ShellState* state,
    TimelineGraphRenderStats* graph_stats_out = nullptr);
```

- [ ] **Step 1: Add a failing actual-frame graph test**

In `render_headless_smoke_frames()`, after the existing parameter/dock frames:

1. switch to Animation mode;
2. select `idle`;
3. find the spine Rotate row;
4. set `requested_view_mode = TimelineViewMode::Graph`;
5. start an ImGui frame and call `draw_timeline_window(&shell_state, &stats)`;
6. render and assert Ready status, points, playhead, at least one Linear segment, and at least one Cubic segment;
7. repeat with spine Translate and assert a Stepped segment.

Capture project serialization, dirty flag, undo/redo counts, project/runtime revisions, and Agent operation count before these frames and assert they remain unchanged afterward.

Add one assertion that switching back to Dopesheet leaves `selected_keys`, `active_key`, and playhead unchanged.

- [ ] **Step 2: Build and observe RED**

Run:

```bash
cmake --build build --target marrow_editor_shell -j4
```

Expected: compilation fails because `TimelineGraphRenderStats`, the two-argument `draw_timeline_window()`, and `draw_timeline_graph_body()` do not exist.

- [ ] **Step 3: Split common playback from view-specific authoring controls**

Keep these controls above the tabs:

- animation selection and preview options;
- rewind, previous key, play/pause, next key, loop;
- Time slider;
- preview span/root-motion summary.

Move these existing controls inside the Dopesheet tab only, without behavior changes:

- add/remove key;
- copy/cut/paste;
- Snap to Frames;
- keyboard copy/cut/paste/delete;
- dopesheet ruler/table;
- retime update;
- transform/slot/deform editors.

This prevents Graph mode from exposing MAR-168/169 authoring through the old toolbar.

Create:

```cpp
if (ImGui::BeginTabBar("timeline_views")) {
    if (ImGui::BeginTabItem(
            "Dopesheet",
            nullptr,
            state->timeline_editor.requested_view_mode == TimelineViewMode::Dopesheet
                ? ImGuiTabItemFlags_SetSelected
                : ImGuiTabItemFlags_None)) {
        state->timeline_editor.view_mode = TimelineViewMode::Dopesheet;
        draw_dopesheet_body(state, tracks, duration_seconds);
        ImGui::EndTabItem();
    }
    if (ImGui::BeginTabItem(
            "Graph",
            nullptr,
            state->timeline_editor.requested_view_mode == TimelineViewMode::Graph
                ? ImGuiTabItemFlags_SetSelected
                : ImGuiTabItemFlags_None)) {
        state->timeline_editor.view_mode = TimelineViewMode::Graph;
        const auto stats = draw_timeline_graph_body(state, tracks);
        if (graph_stats_out != nullptr) *graph_stats_out = stats;
        ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
    state->timeline_editor.requested_view_mode.reset();
}
```

`ImGuiTabItemFlags_SetSelected` is therefore applied only for a requested
programmatic transition. A user's tab click remains effective on every later
frame.

- [ ] **Step 4: Implement graph legend, notice, axes, and fit**

`draw_timeline_graph_body()` must:

- resolve the focused/fallback track and cached projection;
- render these exact empty-state strings:
  - Unsupported/no context: “Select a Transform or Slot Color track to view its scalar graph.”
  - Missing source: “The focused graph track could not be resolved from the effective animation.”
  - Invalid data: “The focused graph track contains invalid or non-finite data.”
- show fixed component toggles and exact colors: Angle `#FFC15C`, X `#FF6B6B`,
  Y `#63D471`, R `#FF5C5C`, G `#58D68D`, B `#5B8CFF`, A `#E6EAF2`;
- show `Fit` and honor `F` only while the graph is hovered and no text input is active;
- auto-fit on animation/track context change or invalid view;
- draw time/frame ticks and native-value ticks;
- display “Outgoing easing is shared by every X/Y or RGBA component of this key; per-component curves are not supported.” for multi-component tracks and “Outgoing easing is shared by this key.” for Rotate;
- report the active key's outgoing kind as Linear, Stepped, Cubic, or “No outgoing segment”.

Component visibility is transient. Hiding all components shows “Enable at least one component” and preserves the last finite view; re-enabling from zero requests Fit.

Choose time ticks with
`nice_tick_interval(pixels_per_second, 72.0)` and value ticks with
`nice_tick_interval(pixels_per_value, 48.0)`. For either interval `step`, use
`clamp(ceil(-log10(step)) + 1, 0, 6)` decimal places. Time labels also use at
least three decimals when project FPS is 1 or greater. Slot Color always labels
0 and 1 when they are inside the visible plot.

- [ ] **Step 5: Implement curves, markers, playhead, and points**

Use `build_geometry()` and draw:

- component-colored polylines;
- Linear midpoint ticks;
- Stepped right-angle squares;
- Cubic hollow midpoint circles;
- a 1-pixel tertiary red playhead;
- 5-pixel component-colored key points;
- gold outlines for every component of selected parent keys;
- a light center mark only for the active component of `active_key`.

Increment `TimelineGraphRenderStats` from the geometry actually submitted to `ImDrawList`. Do not derive counts from source key arrays.

- [ ] **Step 6: Implement graph input without editing**

- Wheel: call `zoom_time_at()` at the pointer.
- Shift+wheel: call `zoom_value_at()`.
- Middle drag: call `pan_view()` with frame delta.
- Left click on a point: call `activate_timeline_graph_point()` with Cmd/Ctrl additive state.
- Left click on empty plot: pause and call `scrub_timeline_time()`; preserve selection/active key.
- Left drag beginning on a point: keep selection only and show “Value/time dragging is available in MAR-168”; never call `begin_edit()` or retime.

Use the pure inclusive 8-pixel hit test. Do not duplicate tie-breaking in ImGui code.

- [ ] **Step 7: Run focused frame/shell tests and observe GREEN**

Run:

```bash
cmake --build build --target marrow_editor_shell marrow_timeline_graph_model_tests -j4
./build/marrow_timeline_graph_model_tests
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
```

Expected: projection/geometry tests pass, both actual graph frames report the required segment kinds, and display-only state invariants remain unchanged.

- [ ] **Step 8: Add keyboard/tab lifecycle regressions**

In shell smoke:

- assert Delete/Backspace while Graph mode is active does not remove a key;
- assert graph pan/zoom does not change dopesheet `view_start_seconds` or `pixels_per_second`;
- assert switching tabs preserves playhead/selection/active key;
- assert changing to an unsupported FFD/Attachment row clears drawn graph stats instead of leaving stale curves;
- assert project/source reload resets to Dopesheet while animation/track changes inside the same project keep Graph active and auto-fit.

Run the shell smoke again. Expected: all interaction/lifecycle cases pass.

- [ ] **Step 9: Record a no-commit checkpoint**

Run `git diff --check` and `git status --short`. Do not stage or commit.

---

### Task 6: Full validation, documentation, and MAR-167 closure

**Files:**

- Modify: `AGENTS.md`
- Modify: `docs/root1/discription.md`
- Modify: `docs/root1/quick-start.md`
- Modify: `docs/root1/concepts.md`
- Modify: `docs/root1/editing-gap-analysis.md`
- Modify: `docs/root1/fixtures.md` if the graph fixture role is not already explicit
- Modify: `docs/root1/refector.md` and other roadmap pages only where MAR-167/next-milestone text is stale
- Modify: `docs/superpowers/specs/2026-08-20-mar-167-synchronized-scalar-graph-design.md`
- Modify: `.agents/tasks/prd-marrow-runtime.json`

**Interfaces:**

- Consumes: completed Tasks 1–5 and fresh command output.
- Produces: synchronized documentation, exact validation evidence, `MAR-167 status=done`, and `MAR-168 status=open`.

- [ ] **Step 1: Run the fresh default/focused gate**

Run as one fail-fast sequence:

```bash
cmake -S . -B build &&
cmake --build build -j4 &&
./build/marrow_timeline_graph_model_tests &&
./build/marrow_timeline_model_tests &&
./build/marrow_project_smoke assets/fixtures/player_idle.marrow &&
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2 &&
ctest --test-dir build --output-on-failure
```

Expected: every command exits zero. Adding one CTest should produce 21 default tests if no concurrent test target has been added; if discovery differs, inspect `ctest --test-dir build -N` and document the actual justified count.

- [ ] **Step 2: Run Debug and Release display-enabled gates**

Run:

```bash
cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON &&
cmake --build build-display -j4 &&
ctest --test-dir build-display --output-on-failure &&
cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON &&
cmake --build build-platform-release -j4 &&
ctest --test-dir build-platform-release --output-on-failure
```

Expected: all tests pass, including the three existing display tests and the new graph-model CTest. This should produce 24 tests per display build if no concurrent target has changed discovery. Label the evidence automated; do not claim manual UI or Windows qualification.

- [ ] **Step 3: Run export/runtime compatibility gates**

Run:

```bash
./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar167.mskl --export-binary /tmp/marrow_mar167.mbin
./build/marrow_inspect --compare /tmp/marrow_mar167.mbin /tmp/marrow_mar167.mskl
./build/marrow_fixture_smoke /tmp/marrow_mar167.mskl /tmp/player_idle.matl
```

Expected: project export, JSON/MBIN comparison, and runtime fixture smoke pass. Record exact comparison errors and output sizes from the fresh run.

- [ ] **Step 4: Run unchanged-surface and integrity gates**

Run:

```bash
./build/marrow_agent_dispatch_smoke
./build/marrow_agent_socket_tests
./build/marrow_c_smoke
tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
cmake --build build --target marrow_verify_third_party
python3 -m json.tool assets/fixtures/player_idle.marrow >/dev/null
python3 -m json.tool .agents/tasks/prd-marrow-runtime.json >/dev/null
git diff --check
git lfs status
```

Expected: all commands exit zero, the Agent registry remains exactly 56 operations, and no LFS object is accidentally staged or normalized.

- [ ] **Step 5: Synchronize user and architecture documentation**

Write only evidence supported by Steps 1–4:

- `quick-start.md`: Timeline → Graph instructions, focused parent-track behavior, component toggles, Fit/pan/zoom, parent-key selection, and read-only MAR-167 boundary.
- `concepts.md`: effective-runtime scalar projection, shared `TimelineKeyRef`/`active_key`/playhead ownership, one parent outgoing easing, transient graph view/cache.
- `discription.md`: a dated MAR-167 contract paragraph and MAR-168 as next.
- `fixtures.md`: `player_idle` supplies six parent tracks, thirteen series, and Linear/Stepped/Cubic coverage.
- `editing-gap-analysis.md` and `refector.md`: remove “MAR-167 next/open” wording and identify MAR-168 next without changing deferred qualification status.
- `AGENTS.md`: add exact focused/default/display/export/compatibility commands and observed outputs.
- design spec: change status from “Approved design, implementation not started” to “Implemented and validated” only after every required gate is green.

Do not add file-format documentation because MAR-167 persists no graph state and changes no schema.

- [ ] **Step 6: Close only MAR-167 in the PRD**

Set:

```json
{
  "id": "MAR-167",
  "status": "done",
  "completedAt": "2026-08-20"
}
```

Keep MAR-168 open and dependent on MAR-167. Update the overview to say MAR-167 is complete and the remaining strict chain begins at MAR-168. Do not alter MAR-192 through MAR-210 status/dependencies.

- [ ] **Step 7: Run documentation/roadmap integrity checks**

Run:

```bash
python3 -m json.tool .agents/tasks/prd-marrow-runtime.json >/dev/null
jq -e '
  (.stories[] | select(.id == "MAR-167") | .status) == "done" and
  (.stories[] | select(.id == "MAR-168") | .status) == "open" and
  ([.stories[] | select(.id >= "MAR-192" and .id <= "MAR-210") | .status] | all(. == "open"))
' .agents/tasks/prd-marrow-runtime.json >/dev/null
! rg -n "MAR-167 (is )?(the )?next|MAR-167.*status.*open|MAR-166 is the next" AGENTS.md docs/root1 .agents/tasks/prd-marrow-runtime.json
rg -n "MAR-168" AGENTS.md docs/root1/discription.md docs/root1/editing-gap-analysis.md .agents/tasks/prd-marrow-runtime.json
git diff --check
```

Expected: JSON and jq checks pass, stale “MAR-167 next/open” searches return no match, and current MAR-168 references exist.

- [ ] **Step 8: Perform requirement-by-requirement completion audit**

For every MAR-167 acceptance criterion, point to both source and executed evidence:

1. Supported scalar series — projection tests plus actual graph render statistics.
2. Shared identity/selection/active/playhead and transient duration-independent view — model tests plus shell synchronization/no-mutation smoke.
3. Distinct Linear/Stepped/Cubic — geometry tests plus actual Rotate/Translate graph frames.
4. FFD/discrete exclusions and shared outgoing-easing notice — projection tests plus unsupported/notice presentation smoke.
5. Rendering and zero history/Agent changes — frame statistics plus serialization/history/revision/56-operation comparisons.

Treat any missing or indirect evidence as incomplete. Fix via a new RED-GREEN cycle, rerun the affected focused test, then rerun the full gate that supports the claim.

- [ ] **Step 9: Final no-commit checkpoint**

Run:

```bash
git status --short
git diff --stat
git diff --check
git lfs status
```

Report the changed files, fresh test totals, export comparison metrics, exact remaining qualification limitations, and that no commit/push/reset occurred. Leave the long-running commercial-editor goal active because MAR-168–191 and the deferred qualification backlog remain.
