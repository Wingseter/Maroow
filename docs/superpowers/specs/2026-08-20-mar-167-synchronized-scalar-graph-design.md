# MAR-167 Synchronized Scalar Graph View Design

**Date:** 2026-08-20

**Status:** Implemented and validated

**Authority:** `.agents/tasks/prd-marrow-runtime.json` story `MAR-167` and `docs/root1/discription.md`

## 1. Goal

MAR-167 adds a read-only scalar graph mode to the existing Timeline window. The
graph displays the effective animation values and outgoing easing of the focused
continuous parent track while sharing key identity, key selection, active key,
and playhead with the dopesheet.

This checkpoint establishes a trustworthy graph projection and synchronization
boundary for later graph editing. It does not edit key times, values, easing, or
project data.

## 2. Approved Product Decisions

The following decisions are fixed for MAR-167:

1. The existing `Timeline` window contains `Dopesheet` and `Graph` tabs. Playback
   controls, animation selection, and the playhead remain common to both tabs.
2. The graph displays one focused parent track at a time. It overlays every
   supported scalar component of that parent track and provides a visibility
   toggle for each component.
3. The graph and dopesheet share selection, explicit active key, track focus,
   and playhead. They do not share horizontal pan or zoom state.
4. Clicking a graph point selects the whole parent key, makes that key active,
   records the clicked component as transient graph context, and moves the
   playhead to the key time.
5. Graph point dragging is disabled. Key time/value editing begins in MAR-168.
6. Values use their authored native units. Values from unrelated tracks are
   never normalized onto one mixed axis.
7. One key owns one outgoing easing shared by all of its X/Y or RGBA
   components. The graph never presents per-component easing ownership.

## 3. Scope

### 3.1 Supported parent tracks and scalar components

| Parent track | Components | Displayed value |
| --- | --- | --- |
| Bone Rotate | Angle | Absolute local angle: setup rotation plus effective keyed angle |
| Bone Translate | X, Y | Effective authored X and Y values |
| Bone Scale | X, Y | Effective authored signed X and Y values, including exact zero |
| Bone Shear | X, Y | Effective authored X and Y values |
| Slot Color | R, G, B, A | Effective authored light-color channels in `[0, 1]` |

The checked-in `player_idle` project currently resolves its effective runtime
animation to seven supported parent tracks and fourteen scalar series: root
Translate; spine Rotate, Translate, Scale, and Shear; body Slot Color; and the
valid arm_l Rotate project overlay.

### 3.2 Explicit exclusions

MAR-167 does not graph:

- Bone Inherit;
- Slot Attachment;
- Mesh Deform/FFD;
- Draw Order;
- Events;
- parameter, constraint, or other non-animation-timeline data;
- dark-tint channels that are not present in the current Slot Color timeline;
- graph point insertion, deletion, copy/paste, box selection, time scaling, or
  value/time dragging;
- Bezier handles or interpolation mutation;
- curve presets, automatic handles, loop-boundary management, playback-speed
  editing, or time-warp editing.

The exclusions remain visible as an informative empty state when the focused
dopesheet track is unsupported. The graph must not silently keep displaying the
previous supported track.

### 3.3 Compatibility boundaries

MAR-167 adds no persistent state and changes none of the following:

- `.marrow` project schema;
- `.mskl` v1 or `.mbin` v2;
- C ABI v1;
- public `include/marrow/**` APIs;
- `SelectionSet` entity identity;
- runtime/GPU resource ownership;
- the 56-operation Agent/MCP surface.

## 4. Existing Boundaries to Reuse

- `timeline_model` owns UI-free track/key identity, same-time ordinal handling,
  selection reconciliation, and other timeline calculations.
- `TimelineKeyRef { track_id, time_microseconds, same_time_ordinal,
  same_time_count }` remains the only parent-key identity in both views.
- `timeline_controller` owns track focus and the only valid playhead update path,
  `scrub_timeline_time()`, which also seeks and refreshes preview state.
- `shell_timeline` owns Timeline-window ImGui input and presentation.
- `cached_timeline_tracks()` supplies effective runtime tracks keyed by runtime
  revision, skeleton identity, and animation name.
- Effective runtime animation data, not project-overlay arrays alone, is the
  graph source. This includes imported runtime-only tracks and authored project
  edits after session materialization.

## 5. Architecture

MAR-167 uses three internal layers:

```text
effective runtime animation + TimelineTrackRow
                    |
                    v
         UI-free scalar graph projection
                    |
                    v
 shared timeline selection / active-key / playhead controller
                    |
                    v
         ImGui graph presenter and hit testing
```

### 5.1 UI-free graph model

Create internal `src/editor/timeline_graph_model.hpp/.cpp` and compile it into
the existing `marrow_timeline_model` library. The model must not include ImGui,
Sokol, or `ShellState`.

The conceptual data contract is:

```cpp
enum class ScalarGraphTrackKind {
    Rotate,
    Translate,
    Scale,
    Shear,
    SlotColor,
};

enum class ScalarGraphComponent {
    Angle,
    X,
    Y,
    Red,
    Green,
    Blue,
    Alpha,
};

struct ScalarGraphComponentDescriptor {
    ScalarGraphComponent component;
    std::string_view label;
};

struct ScalarGraphKey {
    timeline_model::KeyRef identity;
    double time_seconds;
    std::array<double, 4> values;
    std::size_t value_count;
    marrow::runtime::Interpolation outgoing_easing;
};

struct ScalarGraphTrack {
    std::string track_id;
    std::string label;
    ScalarGraphTrackKind kind;
    std::vector<ScalarGraphComponentDescriptor> components;
    std::vector<ScalarGraphKey> keys;
};
```

Exact naming may be adjusted during the implementation plan, but the ownership
and parent-key shape are fixed: components do not own independent keys or
independent easing objects.

Eligibility is a typed allowlist. Existing string suffix checks may be used only
once at the adapter boundary if adding a typed `TrackKind` to `TrackRow` would
create unnecessary churn. FFD or discrete tracks must never become eligible
merely because their runtime keys contain numeric values or interpolation.

### 5.2 Shared timeline interaction controller

Add an explicit `std::optional<TimelineKeyRef> active_key` to
`TimelineEditorState`. Its invariant is:

- it is empty when no selected key is active; or
- it resolves in the current track set and is present in `selected_keys`.

Both views call one UI-free selection transition helper and one shell controller
entry point. The transition rules are:

- Plain click on an unselected key replaces selection with that key.
- Plain click on a selected key preserves the existing multi-selection.
- Every successful plain click makes the clicked key active.
- Cmd on macOS or Ctrl elsewhere toggles the clicked key.
- Additive insertion makes the inserted key active.
- Additive removal preserves the current active key when it remains selected.
- Removing the active key chooses the last remaining key in stable selection
  order; an empty selection clears `active_key`.
- Box selection and selection reconciliation preserve `active_key` when it
  remains valid. Otherwise they choose the last remaining key in stable
  selection order or clear it.

The shell controller entry point also focuses the parent track and calls
`scrub_timeline_time()` with the clicked key time. Neither view writes
`timeline_time_seconds` directly.

### 5.3 Graph presentation

Create `src/editor/shell_timeline_graph.hpp/.cpp`. `shell_timeline.cpp` retains
the common animation/playback toolbar and chooses the dopesheet or graph body
through tabs. The new presenter owns only:

- component legend and visibility controls;
- plot axes and labels;
- polyline and point drawing;
- point hit testing;
- graph-specific pan, zoom, fit, hover, and active-component state;
- calls into the shared timeline controller for selection and scrubbing.

The presenter stores no runtime key pointers or key indices across frames. It
stores stable track IDs, `TimelineKeyRef`, and component enums only.

## 6. Graph Context and Transient State

Add shell-private graph state inside `TimelineEditorState` or a directly owned
presentation struct:

```cpp
enum class TimelineViewMode { Dopesheet, Graph };

struct ScalarGraphViewState {
    double view_start_seconds;
    double pixels_per_second;
    double value_center;
    double pixels_per_value;
    std::array<bool, 4> component_visible;
    std::optional<ScalarGraphComponent> active_component;
    std::string fitted_track_id;
    std::string fitted_animation_name;
};
```

This is a conceptual contract, not persisted schema.

Context resolution follows this order:

1. When `selected_timeline_track_id` resolves, use that row as the graph
   context. An unsupported focused row produces the unsupported empty state.
2. Only when no focused row resolves, find the first supported track matching
   the active bone or slot.
3. Otherwise, show an empty state asking the user to select a supported
   Transform or Slot Color track.

Entering Graph mode does not change selection merely to obtain a graph. An
explicit fallback match may be displayed, but its parent track becomes focused
only after the user clicks the graph or its legend.

The graph auto-fits all visible components when:

- a different parent track or animation becomes the graph context;
- a component visibility change makes the current bounds empty or invalid;
- the user presses the `Fit` control or `F` while the graph is hovered and no
  text input is active.

Ordinary same-project undo/redo preserves the view when the same graph context
and finite view transform remain valid. A runtime revision invalidates and
rebuilds the projected keys and geometry, but does not auto-fit the same parent
track merely because values changed. Project/source adoption resets Graph mode
to the Dopesheet and clears graph-specific view state. Animation and
supported-track changes keep Graph mode active and auto-fit the new context.
They also restore visibility for every available component and clear the prior
track's active component, so X/Y visibility cannot silently become R/G
visibility after a context switch.

Graph pan and zoom are duration-independent and never modify animation duration:

- Mouse wheel performs cursor-anchored horizontal time zoom.
- Shift + mouse wheel performs cursor-anchored vertical value zoom.
- Middle-button drag pans both axes.
- Horizontal zoom is clamped to `0.01..1600` logical pixels per second. The
  lower graph-specific bound allows a long focused track to remain fully
  fittable without changing animation duration.
- Vertical zoom is clamped to finite positive values that keep one logical pixel
  between `1e-9` and `1e9` native value units.
- Horizontal pan may reveal negative time for context, but keys and the playhead
  remain at their authored nonnegative times.

## 7. Axes, Auto-fit, and Visual Semantics

The horizontal axis is seconds with frame-aware tick labels based on the current
project FPS. The vertical axis is the focused track's native authored value.
No implicit conversion or normalization is applied between Angle, X/Y, Scale,
Shear, and RGBA tracks.

Auto-fit rules are deterministic:

- Time bounds include every key in the focused track and 5% horizontal padding,
  with a minimum visible span of one frame.
- Value bounds include all visible key values and every sampled cubic overshoot,
  followed by 10% vertical padding.
- A flat series receives symmetric padding of `max(abs(value) * 0.1, 1e-3)`.
- Slot Color fit includes at least `[0, 1]` so channel meaning remains legible.
- Empty or wholly non-finite input produces an empty state rather than default
  coordinates that look like valid animation data.

Semantic component colors are stable:

| Component | Color |
| --- | --- |
| Angle | gold `#FFC15C` |
| X | red `#FF6B6B` |
| Y | green `#63D471` |
| R | red `#FF5C5C` |
| G | green `#58D68D` |
| B | blue `#5B8CFF` |
| A | light neutral `#E6EAF2` |

Selected parent keys receive a gold outline on every visible component point.
The active component point receives an additional light center mark. Component
color remains visible so active state does not erase series identity.

## 8. Easing Geometry

For parent key `i`, `outgoing_easing` governs the segment from key `i` to key
`i + 1`. The last key has no outgoing segment.

- Linear: draw a straight segment from the current value to the next value.
- Stepped: hold the current value until the next key time, then draw the vertical
  transition to the next value.
- Cubic: evaluate the existing runtime `Interpolation::transform(u)` and draw
  `lerp(value_i, value_{i+1}, transformed_u)`.

Cubic curves use adaptive subdivision in plot-space until midpoint deviation is
at most 0.5 logical pixels, with a recursion-depth cap of 10. The same geometry
builder supplies auto-fit samples and draw points so the fitted bounds cannot
omit a rendered overshoot.

Segment kind must remain visually identifiable even when different easing kinds
produce coincident geometry:

- Linear has a short midpoint tick.
- Stepped has a square at its right-angle transition.
- Cubic has a hollow midpoint circle.
- The legend names the active key's outgoing kind as `Linear`, `Stepped`, or
  `Cubic`.

The graph displays this fixed notice whenever a supported multi-component track
is shown:

> Outgoing easing is shared by every X/Y or RGBA component of this key;
> per-component curves are not supported.

For Rotate, the notice uses “this key” without listing components.

## 9. Point Hit Testing and Input

Graph points use a 5 logical-pixel visual radius and an inclusive 8 logical-pixel
hit radius. Resolution order is:

1. minimum Euclidean screen distance;
2. lower `time_microseconds`;
3. lower `same_time_ordinal`;
4. component order `Angle`, `X`, `Y`, `R`, `G`, `B`, `A`.

Click behavior is:

- Left click on a point applies the shared parent-key selection transition,
  focuses the parent track, records the active component, pauses playback, and
  scrubs to the key time.
- Cmd/Ctrl-left click uses the shared additive toggle rule.
- Left click on empty plot space pauses and scrubs the playhead but preserves
  key selection; MAR-173 owns graph box selection and scaling.
- Dragging a point has no editing effect in MAR-167. The point remains selected
  and the UI tooltip states that value/time dragging arrives in MAR-168.
- Middle-button drag is the only graph pan gesture, so it cannot accidentally
  start a future left-button editing transaction.

## 10. Error Handling and Lifecycle

The graph fails closed without project mutation:

- Missing animation, missing focused track, unsupported track, or fewer than one
  finite key produces a contextual empty state.
- A non-finite key time or component value rejects the complete projected track;
  the graph does not draw a partial curve.
- A non-finite easing result rejects the complete projected track and reports it
  as invalid instead of drawing a partial curve or substituting Linear easing.
- A stale `TimelineKeyRef` is removed by shared selection reconciliation.
- A stale `active_key` follows the deterministic fallback rule in Section 5.2.
- A runtime revision invalidates cached projection data before the next draw.
- Focus/context loss cancels only transient hover/pan state because MAR-167 has
  no authoring transaction.

Display-only interaction must not:

- call `EditorSession::begin_edit()`;
- create undo or redo entries;
- mark the project dirty;
- change serialized project bytes;
- change runtime revision;
- add Agent/MCP operations.

Scrubbing is allowed to change preview revision and sampled pose because that is
the intended playhead effect.

## 11. Caching and Performance

Only one parent track with at most four scalar series is rendered. Projection is
cached by runtime revision, skeleton identity, animation name, and parent track
ID. Plot geometry is rebuilt when projection, plot size, view transform, or
component visibility changes.

No cache stores raw pointers into a runtime animation across revision changes.
Adaptive cubic subdivision is bounded by the depth cap, so one segment produces
at most 1,025 sampled vertices before deduplication.

## 12. Validation Strategy

### 12.1 UI-free focused tests

Add `marrow_timeline_graph_model_tests` linked to `marrow_timeline_model` and
cover:

- exact seven supported parent tracks and fourteen scalar series from
  `player_idle`;
- absolute Rotate plotting with a synthetic nonzero setup rotation;
- X/Y and RGBA components sharing one `TimelineKeyRef` and one outgoing easing;
- exclusion of Inherit, Attachment, FFD, Draw Order, and Event tracks;
- Linear, Stepped, and Cubic geometry, endpoint behavior, cubic overshoot, and
  the absence of a final-key outgoing segment;
- stable point-hit tie ordering;
- cursor-anchored horizontal/vertical zoom, 2D pan, fit, flat lanes, RGBA bounds,
  and finite-input rejection;
- shared selection transitions and the `active_key` invariant across replace,
  preserve, toggle, box-selection reconciliation, and deletion/revision
  reconciliation;
- no duration mutation from every graph-model operation.

Every production behavior begins with a focused failing test and an observed
expected RED result before implementation.

### 12.2 Headless shell smoke

Add a separate `src/editor/shell_smoke_graph.cpp` scenario and verify:

- dopesheet selection, active key, and scrub appear identically in Graph mode;
- graph point selection/toggle appears identically in the dopesheet;
- X/Y or RGBA point clicks select the parent key while preserving only the
  transient active component distinction;
- supported graph data and unsupported empty states are both rendered;
- render statistics count points, playhead geometry, and Linear, Stepped, and
  Cubic segment geometry;
- component visibility and track changes trigger the specified fit lifecycle;
- display-only tab, selection, pan, zoom, fit, and visibility actions preserve
  project serialization, dirty state, undo/redo depth, runtime revision, and the
  56-operation Agent surface;
- scrubbing may change preview revision but not runtime or project revision.

### 12.3 Display and compatibility gates

After focused tests pass, run and record:

```bash
cmake -S . -B build
cmake --build build -j4
./build/marrow_timeline_graph_model_tests
./build/marrow_timeline_model_tests
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2
ctest --test-dir build --output-on-failure

cmake -S . -B build-display -DCMAKE_BUILD_TYPE=Debug -DMARROW_ENABLE_DISPLAY_TESTS=ON
cmake --build build-display -j4
ctest --test-dir build-display --output-on-failure

cmake -S . -B build-platform-release -DCMAKE_BUILD_TYPE=Release -DMARROW_ENABLE_DISPLAY_TESTS=ON
cmake --build build-platform-release -j4
ctest --test-dir build-platform-release --output-on-failure

./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/marrow_mar167.mskl --export-binary /tmp/marrow_mar167.mbin
./build/marrow_inspect --compare /tmp/marrow_mar167.mbin /tmp/marrow_mar167.mskl
./build/marrow_fixture_smoke /tmp/marrow_mar167.mskl /tmp/player_idle.matl

./build/marrow_agent_dispatch_smoke
./build/marrow_agent_socket_tests
./build/marrow_c_smoke
tools/mcp/venv/bin/python -m py_compile tools/mcp/server.py tools/mcp/test_client.py tools/mcp/tools/editing.py tools/mcp/tools/inspection.py
cmake --build build --target marrow_verify_third_party
git diff --check
git lfs status
```

Automated display tests prove the exercised ImGui/display path only. They do not
add manual-visible-UI, Windows 11, or physical-input qualification credit.

## 13. Documentation and Milestone Closure

After code and all required gates pass:

- update `AGENTS.md` with exact MAR-167 commands and outputs;
- update `docs/root1/discription.md`, `quick-start.md`, `concepts.md`, and the
  editing gap/roadmap documents with the graph contract;
- update other documentation only where a current statement becomes stale;
- mark `MAR-167` done with the verified completion date;
- leave `MAR-168` open as the next product milestone;
- leave MAR-192 through MAR-210 open and add no platform qualification credit.

## 14. Commercial-Tool Reference Boundary

The interaction direction is informed by the official Spine Graph/Dopesheet and
Live2D Cubism Graph Editor/Timeline documentation:

- Spine: <https://us.esotericsoftware.com/spine-graph>
- Spine Dopesheet: <https://us.esotericsoftware.com/spine-dopesheet>
- Live2D Graph Editor: <https://docs.live2d.com/en/cubism-editor-manual/grapheditor/>
- Live2D Timeline Palette: <https://docs.live2d.com/en/cubism-editor-manual/timelinepalatte/>

Marrow intentionally does not copy component-owned Bezier handles, Live2D Auto
Smooth/Inverse Step modes, or any behavior unsupported by the current runtime.
Runtime truth and Marrow's shared parent-key easing contract take precedence over
surface similarity to another editor.
