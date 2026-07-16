# Marrow Concepts

Marrow splits imported animation content into immutable setup data, mutable instance state, and renderer-facing command data. The separation is intentional: it keeps asset loading cheap to share, playback state cheap to clone, and rendering decoupled from file parsing.

## Data flow

```text
.mskl or .mbin  ->  SkeletonData  ->  Skeleton  ->  PreparedScene / RenderCommandList
                                  \
                                   ->  AnimationState
                                   ->  ParameterState (MAR-126 implemented)

.matl           ->  AtlasData     -------------------------------------------^
.marrow         ->  editor-only source that exports .mskl/.mbin + .matl
```

## `SkeletonData`

`SkeletonData` is the immutable runtime asset created by `load_skeleton_data()`.

It owns:

- Skeleton metadata such as size and default mix duration.
- Bone hierarchy and setup-pose transforms.
- Slots, skins, attachments, constraints, events, and animations.
- Mix definitions used by `AnimationState`.
- Optional parameter, parameter group, parameter shape, deformer, art path, expression, and lip-sync definitions implemented by MAR-122~128. Absent roots behave as an empty model.
- Derived parameter id lookup, dependency bitsets, affected-slot lookup, and deformer graph indices; these caches are not serialized.

Why it exists:

- It is safe to share across many characters.
- It avoids reparsing JSON or binary payloads for every spawned instance.
- It gives both `Skeleton` and `AnimationState` a stable, shared lookup table.

Threading model:

- `SkeletonData` is immutable after load.
- Multiple threads may read the same `SkeletonData` concurrently.

## `Skeleton`

`Skeleton` is the mutable per-instance pose container.

It owns:

- Current local bone poses.
- Computed world transforms.
- Current slot attachment state.
- Current mesh deform state.
- Current draw order.
- Skin selection, attachment playback time, visibility, and update-throttling state.
- Separate per-instance direct and final parameter buffers, parameter revision/dirty state, and evaluated parameter/deformer final-offset caches implemented by MAR-122~128.

Use a `Skeleton` when you need one character instance in the world. If you spawn ten enemies that all use the same rig, you normally create ten `Skeleton` objects that all point at one shared `SkeletonData`.

Threading model:

- `Skeleton` is not internally synchronized.
- Drive one `Skeleton` from one thread at a time.

## `AnimationState`

`AnimationState` is the mutable playback controller that sits beside a `Skeleton`.

It owns:

- Track entries.
- Queueing and crossfade state.
- Mix durations.
- Event dispatch.
- Snapshot and restore support for track state.

`AnimationState` does not store bone transforms itself. Instead, it evaluates timelines against `SkeletonData` and applies the results onto a `Skeleton`.

Parameter modeling follows the same ownership rule: `SkeletonData` stores immutable definitions, while `Skeleton` owns mutable direct and final parameter buffers. `set_parameter_value()` preserves finite raw direct preview input, including fractional discrete and out-of-range clamped input; `parameter_values()` exposes the final value after composition, discrete rounding, and optional clamping. `AnimationState` and the C ABI do not own parameter state.

`ParameterState` is separate from timeline playback. It owns expression activation order, amplitude/phoneme input, and attack/release/smoothing filter state. Its active contract is:

```cpp
parameter_state.update(delta_seconds);
parameter_state.apply(skeleton);
```

Composition order is fixed:

```text
direct preview -> lip mapping override -> expressions by priority/activation order -> discrete round -> optional clamp
```

Typical per-frame flow:

```cpp
animation_state.update(delta_seconds);
animation_state.apply(skeleton);
```

Or, for the common one-state-one-skeleton case:

```cpp
marrow::runtime::update_instance(skeleton, animation_state, delta_seconds);
```

Threading model:

- `AnimationState` is also not internally synchronized.
- Keep one `AnimationState` paired with one `Skeleton` on one thread.

## Renderer handoff

The renderer layer does not load animation files directly. It consumes the current `Skeleton` pose plus atlas metadata.

### `AtlasData`

`AtlasData` is the immutable `.matl` payload. It resolves:

- The atlas image path.
- Texture dimensions.
- Sampling and wrapping hints.
- Region rectangles and origins.

### `PreparedScene`

`prepare_setup_pose_scene()` converts the current skeleton pose plus atlas metadata into a renderer-friendly description:

- Region attachments with world-space quad data.
- Dynamic mesh attachments with GPU skinning payloads.
- Clip attachments and ordered clip/draw events.
- Bone palette data and atlas presentation metadata.

The implemented parameter contract preserves animation FFD as a separately observable layer and gives renderer preparation a final attachment-local accessor. The fixed mesh order is:

```text
setup/linked-mesh resolution -> animation FFD -> normalized_override -> additive_clamped -> deformer -> GPU skinning
```

`current_mesh_vertex_offsets(slot)` stays animation-FFD-only. `current_final_mesh_vertex_offsets(slot)` supplies the attachment-local result used for mesh preparation and the existing weighted GPU skinning path.

ArtPaths are skeleton-local root overlays, distinct from constraint paths. Preparation applies the instance's global x/y scale (including mirroring), then appends them in JSON declaration order after slot draw and clipping events. Stroke-only skeletons use the atlas-free `prepare_setup_pose_scene(skeleton)` overload; a document with atlas-backed attachments still requires atlas data, including after a cached attachment or skin change.

### `RenderCommandList`

`build_render_command_list()` converts a `PreparedScene` into a more compact GPU submission package:

- Packed vertices and indices.
- Clip command buffers.
- Projection matrix.
- Bone palette floats.
- Batch-break metadata such as texture, blend, clip, and shader splits.

This split lets you choose how far down the renderer stack you want to integrate. Tools and debug UIs may stop at `PreparedScene`; engine backends usually consume `RenderCommandList`.

## Why `.marrow` is separate

`.marrow` is the editor project format, not the runtime playback format.

It keeps:

- References to runtime assets.
- Authoring-only viewport and note state.
- Unexported timeline, mesh-weight, constraint, and atlas-pack edits.
- Optional `parameter_model` source data for parameters, groups, blend shapes, deformers, art paths, expressions, and lip-sync mappings.
- Lossless unknown additive fields inside `parameter_model`; a completely empty model is omitted on save.

The editor export step merges that authoring data into runtime-ready `.mskl` or `.mbin` plus `.matl` outputs.

Direct parameter preview is not authoring source. Slider/numeric preview and agent `parameter.set` are undoable but non-dirty and are never serialized or exported.

## Rule of thumb

- Share `SkeletonData` and `AtlasData`.
- Instantiate `Skeleton` and `AnimationState`.
- Rebuild renderer input from the current `Skeleton` pose every frame.
- Treat `.marrow` as source and `.mskl`/`.mbin` + `.matl` as shipped runtime assets.
- Keep parameter/deformer data Maroow-native; do not represent it as Live2D Core compatibility or proprietary Live2D file loading.
