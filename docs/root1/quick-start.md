# Marrow Quick Start

This page shows the shortest path from checked-in assets to a rendered skeleton pose, then maps that flow to the runtime APIs you will reuse in your own game or tool.

## Fastest path

If you just want to verify the stack end to end, build the sample targets and run the renderer sample against the canonical fixture bundle:

```sh
cmake -S . -B build
cmake --build build
./build/marrow_renderer_sample assets/fixtures/player_idle.mskl assets/fixtures/player_idle.matl
```

That command loads `player_idle.mskl`, resolves `player_idle.matl`, applies the fixture animation coverage, prepares renderer commands, and opens the sample window.

CPU scene preparation is owned by the internal `marrow_renderer_commands`
boundary. The public `marrow_renderer` target remains a compatibility umbrella;
its standalone `DemoShell` adds the Sokol-app adapter. The editor uses the same
single-compiled Sokol scene executor through a separate SDL3 host and does not
link `sokol_app` or `sokol_glue`:

```sh
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow
```

On macOS this editor path is SDL Metal. Windows and the retained Linux X11 code
path use an SDL OpenGL 4.1 Core context behind `sokol_gfx`; an unavailable 4.1
context is a startup error rather than a GL 3.2 fallback. Current qualification
and support claims cover macOS arm64 and Windows 11 x64 only; Linux/Ubuntu and
Windows 10 are `NOT REQUIRED` and unqualified.

MAR-192 through MAR-210 remain an open, parallel deferred qualification
backlog. They do not grant support credit and do not block the completed
Task #28/MAR-163/MAR-164/MAR-165/MAR-166/MAR-167/MAR-168/MAR-169/MAR-170/
MAR-172/MAR-173 checkpoints or the next MAR-174 product milestone.

Display/device tests are deliberately absent from the default CTest registry.
Enable them explicitly on a real supported host:

```sh
cmake -S . -B build-display -DMARROW_ENABLE_DISPLAY_TESTS=ON
cmake --build build-display
ctest --test-dir build-display --output-on-failure -L display
```

## Inspect a timeline in Graph mode

Open the canonical project, select an animation, then use the **Graph** tab in
the **Timeline** window. The graph shows one focused continuous parent track at
a time: Bone Rotate, Translate, Scale, or Shear, or Slot Color. Use the colored
per-component checkboxes to show or hide Angle, X/Y, or R/G/B/A. **Fit** (or
`F` while the plot is hovered) frames the visible series. Mouse wheel zooms time
at the cursor, Shift-wheel zooms the value axis, and middle-button drag pans
both axes.

Graph points reuse the dopesheet's parent `TimelineKeyRef`. Selecting a point
therefore updates the shared parent-key selection, active key, focused parent
track, and playhead; component choice is only transient graph context. The
display plots effective runtime values and the actual outgoing Linear, Stepped,
or Cubic easing. One outgoing easing belongs to the complete Transform or RGBA
parent key, not to an individual component.

Graph points are draggable. Press a point to select it, then move the pointer:
the first 4 logical pixels of motion lock one axis for the whole drag. A
dominant vertical move edits only the pressed scalar component on every
selected key of the focused track; a dominant horizontal move retimes the whole
parent key, and every selected key, carrying all of its components with it. A
press and release without motion is still a plain selection click.

The **Snap** checkbox on the Graph toolbar is the same shared frame-snap
setting the Dopesheet tab owns, so toggling it in either tab is visible in the
other. Hold Alt to bypass frame snapping for the current drag. Escape, leaving
the Graph tab, or losing window focus cancels the drag and restores the project
exactly. One drag is always one undo entry, and a drag that ends where it
started creates none. Slot Color R/G/B/A clamps group-wide to `[0, 1]`; Angle,
Translate, Scale, and Shear are unclamped, so a signed or exactly zero scale
stays authorable. Dragging a key past an authored explicit duration grows that
duration inside the same undo entry.

The active key's outgoing segment also draws two light-blue square Bezier
handles, for the active component only, joined to their anchors by thin tangent
lines. One curve therefore has exactly one pair of handles even when several
components are visible, because the easing belongs to the whole parent key.
Pressing a handle wins over pressing a key point and does not scrub the playhead
or change the selection. Dragging is free 2-D, with no axis lock: the horizontal
control point stops at exactly `0` and `1` and the drag keeps going, while
vertical overshoot past the anchors is allowed and is what produces anticipation
and follow-through. Grabbing a handle on a Linear or Stepped segment converts
that segment to Bezier, seeded at the curve that is exactly equal to Linear, in
the same undo entry as the drag. A segment whose two anchors sit less than one
pixel apart vertically still edits: 100 logical pixels of vertical travel is
defined to equal `1.0` there, and the readout says so. A zero-duration segment
gets no handles at all. The component checkboxes and **Fit** are disabled while
any graph drag is live, so the dragged component cannot be hidden underneath the
gesture. Escape, leaving the Graph tab, and losing focus cancel an easing drag
exactly as they cancel a point drag, and one handle drag is one undo entry.

Both timeline tabs also carry a **Curve:** row of six fixed presets — Linear,
Stepped, Ease, In, Out, In-Out — and a **Default:** combo. A preset button
applies its fixed curve to every compatible selected key at once, as a single
previewed undo entry; Draw Order, Event, and Slot Attachment keys carry no
easing, so they are skipped and the status line reports how many. The row is
disabled, not hidden, when the selection contains no compatible key or while a
drag is live. Each button's tooltip gives the full name and the exact
`[cx1, cy1, cx2, cy2]`, and the Graph toolbar's **Outgoing:** readout names the
preset the active key's curve exactly is, or `Custom Bezier` after you drag a
handle away from one. No preset overshoots; only a manual handle drag can. The
**Default:** combo chooses the curve that newly added Transform, Deform, and
Slot Color keys start with — whether you add the key at the playhead, drag a
mesh vertex, drag a viewport gizmo, or type a value into the Inspector at a time
that has no key yet. Editing a key that already exists never changes its curve. It is stored per user in `editor-settings.json`,
never in the project, so it never dirties a file and never changes an existing
key; applying a preset does not change it. Missing, malformed, or
future-versioned settings fall back to Linear without rewriting the file, and
pasted keys always keep the curve they were copied with.

The Graph toolbar carries one more row, **Curve mode:**, with `Manual` and
`Auto` buttons and a `Driver:` combo. `Manual` is what every key has always
been: the stored easing is exactly what you put there. `Auto` records that the
easing should be whatever a smooth curve through the driver's neighbouring keys
says it should be, and the editor keeps that promise — move a neighbour in time
or value, add one, delete one, paste one, or drag a bone, and the affected
curves are recomputed inside that same edit, so it is still one undo step. An
automatic key's handles are drawn hollow and amber instead of filled and blue,
and grabbing one switches that segment back to manual in the same drag, because
you have just said the curve should stop following its neighbours. Automatic
curves never overshoot; only a manual handle drag can. The `Driver:` combo picks
which series drives the shape — `Angle` for a rotate key, `X`/`Y` for
translate, scale, and shear, and `Red`/`Green`/`Blue`/`Alpha` for a slot colour
— and is disabled when the selection spans families that share no component.
Mesh deform keys have no automatic mode: a vertex-offset vector has no single
number to compute a tangent from. MAR-172 adds loop-boundary key
synchronization as a per-lane `.marrow` flag with no widget of its own: an
opted-in Transform, Slot Color, or Deform lane always carries one managed key at
the clip's explicit duration mirroring its key at time zero, the editor
re-establishes that inside the same transaction as any edit, and the graph and
dopesheet skip that derived key for direct value and easing authoring.
FFD and discrete Inherit,
Attachment, Draw Order, and Event lanes show an unsupported empty state instead
of stale graph data. Graph tab, visibility, Fit, pan, zoom, hover,
active-component, and drag state are shell-private and are not saved to
`.marrow`, runtime export, history, dirty state, runtime revision, or
Agent/MCP.

## Auto-key an attachment-local FFD vertex group

Launch the canonical editor fixture, switch to Animation mode, and select the
exact mesh Attachment that is currently displayed in the viewport:

```sh
./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow
```

Every vertex of that active mesh appears automatically. Plain-click an
unselected handle to replace the attachment-local vertex selection. Cmd-click
on macOS (Ctrl-click elsewhere) toggles a handle without starting a drag. Drag
from any selected handle by at least 4px to move the complete selected group by
one common world-space delta; a sub-threshold click on a selected handle
collapses the selection to that vertex. Drag true empty space to box-select
inclusive handle centers, using Cmd/Ctrl for additive boxes.

The editor validates every selected vertex mapping before it materializes the
complete effective deform vector, updates only the selected X/Y pairs, previews
the move live, and commits one undo entry. `mesh_base/body_mesh` exercises a
direct target; `warrior/warrior_body` keeps the displayed linked child selected
while `deform=true` edits the immediate parent `body_mesh` timeline. Parameter
preview affects handle positions but is not baked into animation FFD keys.
Escape, lost context, invalid mapping, failed refresh, or downstream override
rolls back the entire group. Vertex sub-selection is transient: it survives
time, animation, parameter preview, undo/redo, and ordinary rebuilds for the
same exact displayed Attachment, but is not saved and clears on context or
successful source/project replacement.

## Use viewport snapping

Open **Properties → Viewport Snapping** to configure world grid, magnetic FFD
vertices, local angle, and absolute scale snapping. Checked-in defaults are all
off with steps of 10 world units, 15 degrees, and 0.1 scale units; the magnetic
radius is a fixed inclusive 8 logical pixels and is not serialized. Changing a
toggle creates one project undo item; one continuous numeric drag is coalesced
into one undo item.

While dragging a translate, rotate, or scale gizmo, hold Cmd on macOS (Ctrl on
other platforms) to temporarily enable that domain even when its project toggle
is off. Hold Alt to bypass snapping; Alt wins if both modifiers are down. These
modifiers are sampled live and do not dirty the project. Translation aligns to
the configured world-origin snap grid (10 world units by default); the displayed
grid may skip integer multiples while zoomed out. Rotation preserves raw
multi-turn angles, and signed scale can cross through exact zero.

During an FFD drag, the pressed selected vertex is the group anchor. When the
group drag activates, the editor snapshots finite vertices from positive-alpha
displayed meshes while excluding selected members. Each update reprojects that
snapshot and reevaluates which candidates are inside the current canvas before
falling back to the same world grid. Magnetic vertices within 8 logical pixels
win over the grid; exact distance then the stable slot/skin/attachment/vertex
identity resolves ties. Cmd on macOS (Ctrl elsewhere) temporarily enables both
disabled FFD sources, while Alt bypasses both; the live modifiers remain
transient. The resulting common world delta moves the whole selected group,
including linked-child edits that write their immediate-parent deform timeline.
A transient cyan or gold guide shows the correction and disappears on bypass,
cancel, or gesture completion.

## Minimal C++ embedding example

The example below shows the smallest public C++ flow:

1. Load immutable `SkeletonData` from `.mskl`.
2. Load immutable `AtlasData` from `.matl`.
3. Create mutable per-instance `Skeleton` and `AnimationState` objects.
4. Advance animation playback with `update_instance()`.
5. Convert the current pose into renderer input with `prepare_setup_pose_scene()`.
6. Display the prepared scene with `renderer::DemoShell`.

```cpp
#include <filesystem>
#include <iostream>
#include <utility>

#include "marrow/renderer/module.hpp"
#include "marrow/runtime/animation_state.hpp"
#include "marrow/runtime/atlas.hpp"
#include "marrow/runtime/skeleton.hpp"

int main() {
    const std::filesystem::path skeleton_path = "assets/fixtures/player_idle.mskl";
    const std::filesystem::path atlas_path = "assets/fixtures/player_idle.matl";

    const auto skeleton_result = marrow::runtime::load_skeleton_data(skeleton_path);
    if (!skeleton_result) {
        std::cerr << skeleton_result.error->format() << '\n';
        return 1;
    }

    const auto atlas_result = marrow::runtime::AtlasLoader::load(atlas_path);
    if (!atlas_result) {
        std::cerr << atlas_result.error->format() << '\n';
        return 1;
    }

    marrow::runtime::Skeleton skeleton(skeleton_result.skeleton_data);
    marrow::runtime::AnimationState animation_state(skeleton_result.skeleton_data);
    animation_state.set_animation(0, "idle", true);

    constexpr double kFrameDeltaSeconds = 1.0 / 60.0;
    for (int frame = 0; frame < 60; ++frame) {
        marrow::runtime::update_instance(skeleton, animation_state, kFrameDeltaSeconds);
    }

    const auto scene_result =
        marrow::renderer::prepare_setup_pose_scene(skeleton, *atlas_result.atlas_data);
    if (!scene_result) {
        std::cerr << scene_result.error_message << '\n';
        return 1;
    }

    const std::filesystem::path atlas_image_path =
        atlas_path.parent_path() / atlas_result.atlas_data->info().image;

    marrow::renderer::DemoShell shell(
        {.title = "Marrow Quick Start", .width = 1280, .height = 720},
        std::move(*scene_result.scene),
        atlas_image_path);

    if (const auto error = shell.run(); error.has_value()) {
        std::cerr << *error << '\n';
        return 1;
    }

    return 0;
}
```

## What the example is doing

- `load_skeleton_data()` parses either JSON `.mskl` or binary `.mbin` and returns immutable setup data that can be shared across many instances.
- `AtlasLoader::load()` resolves atlas metadata only. The renderer reads the actual texture from `atlas_data->info().image`.
- `Skeleton` holds mutable pose state. `AnimationState` holds track playback, queuing, and mixing state.
- `update_instance()` is the simplest "game loop" helper when you have one `Skeleton` and one `AnimationState` created from the same `SkeletonData`.
- `prepare_setup_pose_scene()` consumes the skeleton's current pose state and produces a `PreparedScene` that the renderer layer can batch or draw.
- `DemoShell` is a convenience viewer used by the checked-in sample apps. Engine integrations typically stop at `PreparedScene` or `RenderCommandList` and submit those results through their own rendering backend.

## Typical engine loop

For a real runtime integration, keep the immutable assets alive for the lifetime of the character set and repeat the mutable steps every frame:

```cpp
marrow::runtime::update_instance(skeleton, animation_state, delta_seconds);
auto scene_result =
    marrow::renderer::prepare_setup_pose_scene(skeleton, *atlas_result.atlas_data);
auto command_list = marrow::renderer::build_render_command_list(
    *scene_result.scene,
    projection_matrix);
```

`build_render_command_list()` is the handoff point if you want packed GPU-ready vertices, indices, clip commands, and bone palette data instead of the higher-level `PreparedScene`.

## Next reads

- [Concepts](concepts.md) explains why `SkeletonData`, `Skeleton`, and `AnimationState` are split.
- [Format Spec](format-spec.md) documents `.mskl`, `.matl`, `.mbin`, and `.marrow`.
- [Fixtures](fixtures.md) maps the checked-in sample assets back to the documented format rules.
