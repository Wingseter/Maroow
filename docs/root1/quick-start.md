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
