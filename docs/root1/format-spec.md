# Marrow Format Spec

This page documents the implemented Marrow asset formats, the validated MAR-122~128 optional
parameter-model extension, and the independent MAR-156 user-preference document.

## Shared conventions

- Runtime JSON assets carry a root `marrow` string and a numeric `version`.
- The current runtime accepts `.mskl` version `1` and `.matl` version `1`.
- Paths stored inside JSON assets are relative paths unless exported otherwise by the caller.
- The canonical examples live under `assets/fixtures/` and are cross-referenced in [Fixtures](fixtures.md).
- MAR-121 is a done tracking tombstone integrated into MAR-122. Every runtime section added by MAR-122~128 is optional; when absent, loaders behave as if the parameter model were empty.

## `editor-settings.json` v1

Purpose: versioned, user-local editor preferences shared by later curve-default and Recent Projects
features. This file is not a project or runtime asset and is never embedded in `.marrow`, `.mskl`,
`.mbin`, or `.matl`.

The v1 wire document is:

```json
{
  "version": 1,
  "default_curve": "linear",
  "recent_projects": []
}
```

- `version` is required and must be the exact integer `1`.
- `default_curve` accepts `linear`, `stepped`, `ease`, `ease_in`, `ease_out`, or
  `ease_in_out`. A missing, non-string, or unknown token falls back to `linear` without affecting
  other fields.
- `recent_projects` is an ordered array of path strings. A missing or non-array field falls back to
  an empty list; non-string and empty entries are skipped independently. MAR-156 intentionally does
  not normalize, canonicalize, de-duplicate, bound, check, or promote these paths. MAR-183 owns
  those Recent Projects policies.
- Supported v1 roots preserve unknown additive fields when known fields are saved. Unknown field
  preservation covers logical JSON values, not original whitespace, key ordering, or number
  spelling.
- First run returns typed Linear/empty defaults without creating the file. Malformed JSON, a
  non-object root, and a missing or fractional version also return usable defaults and a diagnostic
  without automatic repair. An explicit user-triggered save may recover such a malformed document.
- A future integer version returns defaults plus an unsupported-version diagnostic. Normal save
  refuses to overwrite that file so a newer editor's data remains protected. Existing-file read
  failures are likewise non-destructive.

Production path resolution is deterministic:

1. A non-empty absolute `MARROW_CONFIG_HOME` uses
   `<MARROW_CONFIG_HOME>/editor-settings.json` on every platform.
2. macOS uses `$HOME/Library/Application Support/Marrow/editor-settings.json`.
3. Linux uses `$XDG_CONFIG_HOME/marrow/editor-settings.json` when `XDG_CONFIG_HOME` is non-empty
   and absolute.
4. Linux otherwise uses `$HOME/.config/marrow/editor-settings.json`.

Empty variables are treated as unset. Relative `MARROW_CONFIG_HOME`, relative Linux
`XDG_CONFIG_HOME`, relative `HOME`, and a missing required `HOME` are path errors; resolution never
falls back to the current directory. The explicit `PreferenceStore` path constructor is reserved for
isolated tests and tools.

Saving creates the parent directory, writes pretty JSON to a unique same-directory temporary file,
checks write/flush/close, then replaces the destination with one POSIX atomic rename. A failure
removes only that temporary file and leaves the prior destination bytes unchanged. File locking,
multi-process merging, and directory `fsync` are outside MAR-156.

## `.mskl`

Purpose: runtime skeleton document containing setup data, skins, animation timelines, and optional constraint metadata.

Current loader behavior:

- Parsed from JSON when the file extension is `.mskl`.
- Parsed into the same in-memory `SkeletonData` shape that `.mbin` reconstructs.
- Requires `version: 1`.

Top-level keys:

- `marrow`: format family string such as `"1.0"`.
- `version`: integer loader version. Current runtime accepts `1`.
- `skeleton`: object with skeleton metadata.
- `bones`: array of setup-pose bones.
- `slots`: array of slot-to-bone bindings and presentation defaults.
- `skins`: object mapping skin names to attachments.
- `events`: optional array of event definitions.
- `animations`: object mapping animation names to timelines.
- `mixing`: optional animation-state mix table.
- `ik`: optional IK constraint array.
- `path`: optional path constraint array.
- `transform`: optional transform constraint array.
- `physics`: optional physics constraint array.
- `parameters`: optional parameter definition array.
- `parameterGroups`: optional parameter UI/group metadata array.
- `parameterShapes`: optional parameter-driven mesh shape array.
- `parameterDeformers`: optional parameter-driven warp/rotation deformer array.
- `artPaths`: optional renderable stroke/path object array.
- `expressions`: optional expression preset array.
- `lipSync`: optional lip-sync input mapping object.

### `skeleton`

Required fields:

- `name`
- `width`
- `height`

### `bones[]`

Common fields:

- `name`
- `parent`
- `x`
- `y`
- `rotation`
- `scaleX`
- `scaleY`
- `shearX`
- `shearY`
- `inherit`

### `slots[]`

Common fields:

- `name`
- `bone`
- `attachment`
- `blend`
- `color`
- `dark`

### `skins`

Each skin maps slot names to attachment payloads. The current runtime supports:

- `region`
- `mesh`
- `linked_mesh`
- `point`
- `bounding_box`
- `clipping`
- `path`
- sequence playback data on region-style attachments

Common attachment fields:

- `attachment`
- `type`
- `region`
- `parent`
- `skin`
- `deform`
- `vertices`
- `triangles`
- `uvs`
- `weights`
- `points`
- `x`
- `y`
- `rotation`
- `end`
- `start`
- `fps`
- `mode`

### `animations`

The runtime currently supports timeline families for:

- Bone rotate, translate, scale, shear, and inherit.
- Slot attachment and slot color.
- Mesh deform.
- Draw order.
- Events.

Each `animations.<name>` object may also carry an optional `duration` number in seconds. This is
the authored clip boundary rather than another timeline:

- The value must be finite, non-negative, and no shorter than the animation's inferred duration.
- Inferred duration is the greatest last-key time across bone rotate/inherit/translate/scale/shear,
  slot attachment/color, mesh deform, draw-order, and event timelines.
- A pose-identical single key at a non-zero time still authors that timeline boundary and contributes
  to inference; constant-timeline pruning is limited to identity keys at time zero.
- Validation compares the stored runtime timing values exactly; it does not apply an epsilon,
  clamp the value, or silently replace it with `max(duration, inferred)`.
- JSON duration values are normalized to the same float32 animation-time representation as key
  times. Values outside that runtime range are rejected, and a literal equal to its last key stays
  exactly equal after both values enter runtime storage.
- When `duration` is absent, the effective clip boundary is the inferred duration. An empty clip
  therefore has an effective duration of `0`; an empty clip with an explicit zero or positive value
  uses that authored value.
- A duration longer than the last key holds the final sampled pose until the explicit boundary.
  Looping, completion callbacks, queue promotion, reverse playback, events/root motion, and state
  restoration all use that effective explicit-or-inferred boundary.

This optional field is additive: `.mskl` remains version `1`, older assets retain last-key fallback,
and the C ABI v1 exposes no new field or function.

Keyframe fields vary by timeline type but always include `time` and may include `curve` for interpolation. Supported `curve` encodings are:

- `"linear"`
- `"stepped"`
- `[cx1, cy1, cx2, cy2]`

### `mixing`

The current runtime reads:

- `default_mix`
- `entries[]`

Each entry may use:

- `from`
- `to`
- `duration`

The loader also supports wildcard `from: "*"` for "from any animation" behavior.

### Constraint arrays

Optional root arrays:

- `ik`
- `path`
- `transform`
- `physics`

These arrays mirror the runtime structs in `include/marrow/runtime/skeleton.hpp` and are exported directly from editor project edits.

### Parameter/deformer runtime sections

MAR-122~128 extend `.mskl` without changing `version: 1`. MAR-121 is integrated into MAR-122 rather than implemented separately. Existing assets that omit every field below load with an empty parameter model.

The runtime root sections use these exact names:

- `parameters`: array of parameter definitions.
- `parameterGroups`: array of parameter group definitions.
- `parameterShapes`: array of 1D blend-shape-like mesh deltas.
- `parameterDeformers`: array of warp and rotation deformer definitions.
- `artPaths`: array of renderable stroke objects, separate from constraint path attachments.
- `expressions`: array of named parameter preset definitions.
- `lipSync`: object containing mouth/input-to-parameter mappings.

All seven roots are additive and optional. Empty export sections may be omitted. Their logical content is also carried by `.mbin` v2 through the existing generic JSON-like document payload; there is no parameter-specific binary section or version bump.

#### `parameters` and `parameterGroups`

A parameter definition has this wire shape:

```json
{
  "id": "mouth.open",
  "name": "Mouth Open",
  "min": 0,
  "max": 1,
  "default": 0,
  "type": "continuous",
  "clamp": true,
  "ui_step": 0.01,
  "units": "ratio"
}
```

Fields:

- `id`: stable machine identifier, such as `mouth.open`.
- `name`: display name.
- `min`: minimum numeric value.
- `max`: maximum numeric value.
- `default`: reset value.
- `type`: `continuous` or `discrete`.
- `clamp`: boolean; when true, final composed values are clamped to `min`/`max`.
- `ui_step`: optional editor slider increment.
- `units`: optional display units.

Parameter ids are unique and referenced by id from every other section. `min`, `max`, `default`, `ui_step` when present, and runtime setter input must be finite; `min` cannot exceed `max`.

Runtime setter/composition semantics are exact:

1. `set_parameter_value()` accepts only finite values and preserves that exact value in the direct buffer, including fractional discrete values and out-of-range clamped values.
2. Lip mapping and expression composition operate on those raw direct values.
3. The final buffer then applies C++ `std::round` to `discrete` parameters (halfway values round away from zero).
4. Only `clamp: true` limits the rounded/continuous final result to `[min,max]`; `clamp: false` permits final values outside that interval.
5. A direct-only setter immediately exposes the normalized result in `parameter_values()`; a later `ParameterState::apply()` recomposes from the raw direct buffer.
6. An unchanged final value is a no-op for `parameter_revision` and parameter/deformer dirty state, even when a different raw direct input normalizes to that same value.
7. Reset restores the raw authored default and normalizes the final default through the same round/clamp rules. A missing id or invalid index fails without mutating another value.

A group entry uses:

```json
{
  "id": "face",
  "name": "Face",
  "parameters": ["mouth.open", "mouth.form"],
  "collapsed": false,
  "color_tag": "rose"
}
```

Fields:

- `id`
- `name`
- `parameters`: ordered list of parameter ids.
- `collapsed`: optional default editor collapse state.
- `color_tag`: optional editor color token.
- `exclusive_mode`: optional group interaction mode.

Group ids are unique and every listed parameter must exist. Group order and each group's parameter order are preserved.

#### `parameterShapes`

Each typed 1D shape uses one continuous parameter:

```json
{
  "id": "mouth.open.shape",
  "target_slot": "face",
  "target_attachment": "face_mesh",
  "parameter": "mouth.open",
  "blend_mode": "normalized_override",
  "keyforms": [
    { "value": 0, "vertices": [0, 0, 0, 0] },
    { "value": 1, "vertices": [0, -4, 0, 8] }
  ]
}
```

Fields and validation:

- `id`
- `target_slot`
- `target_attachment`
- `parameter`
- `blend_mode`: `additive_clamped` or `normalized_override`.
- `keyforms`: strictly increasing `value` records. `vertices` is the flat x/y offset array and must exactly match the resolved target mesh offset count.

The parameter is held at the nearest endpoint outside the authored keyform interval and evaluated linearly between adjacent keyforms. Linked meshes use the existing deform-inheritance resolution; incompatible inherited topology fails during loading.

At most one `normalized_override` shape may address a target attachment. It replaces that target's complete animation FFD result. `additive_clamped` evaluates with endpoint hold and adds its result in JSON declaration order. Both outputs are attachment-local.

#### `parameterDeformers`

Common fields are:

- `id`
- `name`
- `kind`: `warp` or `rotation`.
- `parent`: optional parent deformer id.
- `target_slots`: slot ids affected by the leaf output.
- `parameter_bindings`: exact `{ "parameter": id, "axis": token }` records.
- `keyforms`: complete sampled output states.

A warp record uses:

```json
{
  "id": "face.warp",
  "name": "Face Warp",
  "kind": "warp",
  "target_slots": ["face"],
  "parameter_bindings": [
    { "parameter": "face.angle_x", "axis": "x" },
    { "parameter": "face.angle_y", "axis": "y" }
  ],
  "grid_cols": 2,
  "grid_rows": 2,
  "control_points": [-48, -48, 48, -48, -48, 48, 48, 48],
  "keyforms": [
    { "x": -1, "y": -1, "control_points": [-52, -44, 44, -52, -48, 48, 48, 48] },
    { "x": 1, "y": -1, "control_points": [-44, -52, 52, -44, -48, 48, 48, 48] },
    { "x": -1, "y": 1, "control_points": [-48, -48, 48, -48, -52, 44, 44, 52] },
    { "x": 1, "y": 1, "control_points": [-48, -48, 48, -48, -44, 52, 52, 44] }
  ]
}
```

Warp rules:

- `parameter_bindings` contains exactly two continuous bindings: one `axis: "x"` and one `axis: "y"`.
- `grid_cols` and `grid_rows` are control-point counts and are each at least two.
- Base `control_points` is a flat row-major x/y array with exactly `grid_cols * grid_rows * 2` numbers. It must form an axis-aligned rectangular lattice: x and y are each strictly monotonic in one consistent direction, and corresponding row/column coordinates agree.
- Every keyform contains `x`, `y`, and a full control-point array. All Cartesian combinations of the authored x and y coordinates must exist exactly once.
- Each axis uses endpoint hold outside its range and bilinear interpolation inside it.
- A target vertex outside the base lattice is unchanged.

A rotation record uses:

```json
{
  "id": "face.roll",
  "name": "Face Roll",
  "kind": "rotation",
  "target_slots": ["face"],
  "parameter_bindings": [
    { "parameter": "face.roll", "axis": "angle" }
  ],
  "pivot": [0, 0],
  "influence": 0.75,
  "keyforms": [
    { "value": -30, "angle": -20 },
    { "value": 0, "angle": 0 },
    { "value": 30, "angle": 20 }
  ]
}
```

Rotation rules:

- `parameter_bindings` contains exactly one continuous binding with `axis: "angle"`.
- `pivot` is an attachment-local `[x,y]` pair and `influence` is finite in `[0,1]`.
- `{value,angle}` keyforms are strictly increasing by `value`, use endpoint hold outside the interval, and linearly interpolate inside it. Rotation output is attachment-local.

Deformer graph rules:

- The leaf/child output is evaluated first, then its optional parent is applied to that output. Each deformer has at most one parent and only one parent-child nesting level is allowed.
- Missing parents, cycles, and deeper chains fail loading.
- A slot may be assigned to only one leaf chain. Multiple independent deformers on one slot and ancestor/child double targeting are ambiguous and fail loading.
- Parameter dependency bitsets, affected-slot lookup, and final-offset caches are derived runtime data, not serialized fields. Only output depending on a parameter whose effective final value changed is reevaluated.

#### `artPaths`

ArtPath is a separate runtime type from constraint path attachments. A record uses skeleton-local point coordinates:

```json
{
  "id": "brow.stroke",
  "name": "Brow Stroke",
  "parent_deformer": "face.warp",
  "points": [-40, 8, -20, 18, 0, 20, 20, 18, 40, 8],
  "width": 8,
  "color": { "r": 0.16, "g": 0.08, "b": 0.04, "a": 1 },
  "cap": "round",
  "join": "round",
  "parameter_keyforms": {
    "parameter": "brow.raise",
    "keyforms": [
      {
        "value": 0,
        "points": [-40, 8, -20, 18, 0, 20, 20, 18, 40, 8],
        "width": 8,
        "color": { "r": 0.16, "g": 0.08, "b": 0.04, "a": 1 }
      }
    ]
  }
}
```

Fields and rules:

- `id`
- `name`
- `parent_deformer`: optional id applied after keyform interpolation.
- `points`: flat skeleton-local x/y pairs.
- `width`: finite positive stroke width.
- `color`: finite `{r,g,b,a}` components.
- `cap`: `butt`, `square`, or `round`.
- `join`: `miter`, `bevel`, or `round`.
- `parameter_keyforms`: optional object containing exactly one continuous `parameter` and strictly increasing full-state keyforms. Every keyform contains `value`, `points`, `width`, and `color`; evaluation is linear with endpoint hold.

ArtPaths are root overlays drawn after all slot draw and clipping events, in JSON declaration order. They have no slot binding, draw-order field, or clipping behavior. Their points and stroke geometry are skeleton-local, so the renderer applies the instance's global x/y scale (including mirroring) before computing prepared bounds.

The CPU tessellator uses eight segments per semicircle for round cap/join, a miter limit of four times half-width with bevel fallback, and skips consecutive zero-length segments. Fewer than two remaining valid points is invalid. Bounds include half-width and cap extension.

Prepared stroke data becomes ordinary triangles using `kSolidWhiteTextureHandle`, normal blend, and the single-color shader. `prepare_setup_pose_scene(skeleton)` and its cached overload support stroke-only documents without an atlas; atlas-backed attachments without an atlas produce a clear missing-atlas error. The C render-command ABI remains version 1 and unchanged.

#### `expressions` and `lipSync`

Expression records use:

- `id`
- `name`
- `targets`: array of `{ "parameter": "...", "value": number }` records.
- `duration`: finite non-negative fade-in and fade-out time.
- `blend`: `additive` or `override`.
- `priority`: integer priority.
- `reset_policy`: `restore` or `hold`.

For `additive`, target values are deltas. For `override`, they are absolute targets. Active expressions evaluate from lower priority to higher priority, breaking equal-priority ties by activation order. Deactivation with `restore` fades out over `duration` and removes the contribution; `hold` retains the final contribution until explicit clear/reset.

`lipSync` has this shape:

```json
{
  "mappings": [
    {
      "source": "amplitude",
      "parameter": "mouth.open",
      "scale": 1.25,
      "bias": 0,
      "attack": 0.02,
      "release": 0.08,
      "smoothing": 0.04
    },
    {
      "source": "phoneme",
      "parameter": "mouth.form",
      "scale": 1,
      "bias": 0,
      "attack": 0,
      "release": 0,
      "smoothing": 0,
      "phoneme_map": { "A": 0.2, "E": 0.8, "O": -0.7 }
    }
  ]
}
```

Mapping rules:

- `source` is exactly `amplitude` or `phoneme`.
- Each target `parameter` appears in at most one mapping; duplicate targets fail loading.
- `scale`, `bias`, `smoothing`, `attack`, `release`, and all phoneme values are finite; time constants are non-negative.
- An unmapped phoneme supplies source value zero.
- Processing order is scale/bias, attack-or-release envelope, then smoothing. Each filter uses `alpha = 1 - exp(-dt/tau)`; `tau == 0` applies its input immediately.

`ParameterState` owns expression activation order, lip input, and envelope/filter state and exposes `update(dt)` and `apply(Skeleton&)`. Audio analysis is outside this format/runtime scope.

#### Composition and observable values

`Skeleton` owns separate direct and final buffers. `set_parameter_value()` preserves the finite raw direct preview input; `parameter_values()` exposes the rounded/clamped final composed value. The full parameter composition order is:

```text
direct preview -> lip mapping override -> expressions by priority/activation order -> discrete std::round -> optional clamp
```

Final local mesh evaluation order is:

```text
setup/linked-mesh resolution -> animation FFD -> normalized_override -> additive_clamped -> deformer -> GPU skinning
```

`Skeleton::current_mesh_vertex_offsets(slot)` remains animation-FFD-only. `Skeleton::current_final_mesh_vertex_offsets(slot)` exposes the final attachment-local offset consumed by renderer preparation. Parameter ownership is not added to `AnimationState` or the C ABI.

These sections are Maroow-native. They are inspired by parameter modeling workflows, but they do not provide Live2D Cubism Core compatibility, proprietary file loading, SDK ABI compatibility, Live2D parameter naming compatibility, or Live2D importer compatibility.

## `.matl`

Purpose: runtime atlas metadata for region lookup and texture presentation.

Current loader behavior:

- Parsed from JSON when the file extension is `.matl`.
- Requires `version: 1`.

Top-level keys:

- `marrow`
- `version`
- `atlas`
- `regions`

### `atlas`

Required fields:

- `name`
- `image`
- `width`
- `height`
- `filter_min`
- `filter_mag`
- `wrap_x`
- `wrap_y`

Optional fields:

- `premultiplied_alpha`

### `regions[]`

Required fields:

- `name`
- `x`
- `y`
- `width`
- `height`
- `origin_x`
- `origin_y`

Optional fields:

- `rotate`

`rotate` accepts either:

- a number of degrees, or
- a boolean, where `true` maps to `90` and `false` maps to `0`

## `.mbin`

Purpose: compact binary runtime skeleton document for production-style loading.

Current loader behavior:

- Selected automatically by `load_skeleton_document()` and `load_skeleton_data()` when the file extension is `.mbin`.
- Decodes back into the same logical runtime document as `.mskl`.

Binary layout:

1. Four-byte magic header: `MBIN`
2. Varint binary version
3. Varint string-table count + string-table payload
4. Varint boolean count + packed boolean bitfield
5. Encoded JSON-like DOM payload
6. Optional packed animation section for optimized playback

Implemented binary versions:

- `1`: generic document payload only
- `2`: generic document payload plus packed animation section

Packed animation section:

- Four-byte section magic: `AKEY`
- Packed rotate/translate channels
- Quantized keyframe timing and payload data
- Interpolation descriptors

Operational notes:

- The file still preserves the validated runtime document structure.
- Version `2` adds quantized rotate/translate data so the runtime can inspect or use optimized animation playback data without changing the logical skeleton document.
- The generic document payload preserves whether each animation authored `duration` and preserves
  its numeric value within the existing float32 JSON/MBIN round-trip tolerance.
- The `AKEY` packed duration and 16-bit key-time quantization use the effective
  explicit-or-inferred duration. The packed value is never used to infer whether the source authored
  the optional field; authored presence comes only from the generic document payload.
- Use `inspect_skeleton_binary()` or `marrow_inspect --compare` to validate JSON/binary equivalence and packed-animation metadata.

## `.marrow`

Purpose: editor project document that references runtime assets and stores authoring-only state.
User preferences belong to the separate `editor-settings.json`; preference load/save never changes
project serialization, dirty state, history, or project/runtime/preview revisions.

Current loader behavior:

- Parsed from JSON by `load_project()`.
- Uses a root `marrow` string but no separate numeric `version` field today.

Top-level keys:

- `marrow`
- `runtime`
- `editor`
- `snap`
- `animation_edits`
- `timeline_edits`
- `mesh_edits`
- `constraint_edits`
- `parameter_model`
- `atlas_packs`

### `runtime`

Required fields:

- `skeleton`
- `atlases`

This section points at exported runtime assets rather than embedding them inline.

### `editor`

Common fields:

- `name`
- `active_animation`
- `preview_skins`
- `export_directory`
- `notes`
- `viewport`
- `timeline`

`viewport` currently includes:

- `pan_x`
- `pan_y`
- `zoom`
- `onion_skin`
- `debug_overlay`

`timeline` is optional and currently stores `fps`, a finite positive editor
display/snap rate. It defaults to `60` for existing projects. Timeline zoom and
pan are presentation state and do not alter animation duration.

### `snap`

Optional editor-only viewport-snapping settings. Projects that omit this
section keep all four snap domains disabled while using the numeric defaults
below if Cmd on macOS or Ctrl elsewhere temporarily enables snapping during a
gesture. A MAR-165 project that has `snap` but omits the MAR-166 magnetic field
also defaults magnetic snapping to off.

```json
"snap": {
  "world_grid_enabled": false,
  "magnetic_vertex_enabled": false,
  "local_angle_enabled": false,
  "absolute_scale_enabled": false,
  "world_grid_step": 10,
  "local_angle_step_degrees": 15,
  "absolute_scale_step": 0.1
}
```

- All four enable fields are booleans. The three step fields must be finite and strictly
  greater than zero; invalid values are rejected on load and save.
- Translation quantizes the applicable absolute world target axes about world
  origin zero before parent inversion. Rotation quantizes the unnormalized
  absolute local angle. Scale quantizes signed absolute components and permits
  exact zero; uniform scale uses one deterministic nonzero driver and preserves
  its X:Y ratio and signs.
- Alt bypasses snapping even when the stored domain and Cmd/Ctrl temporary
  enablement are both active. Modifiers are transient input and never enter the
  project document, dirty state, or history.
- The visible viewport grid uses `world_grid_step` at world origin zero and may
  skip only integer multiples while zoomed out.
- FFD uses the same `world_grid_enabled` and `world_grid_step`. Magnetic matching
  is enabled by `magnetic_vertex_enabled` and uses a fixed, non-serialized,
  inclusive 8 logical-pixel Euclidean radius. The pressed selected vertex is the
  group anchor; selected active-scope members are excluded, magnetic wins over
  grid, and exact-distance ties use `(slot name, optional resolved skin name,
  displayed attachment name, vertex index)`. Candidate canvas membership is
  reevaluated under the current layout during the active gesture.
- Unknown additive members inside `snap` survive load/save. The entire section
  is omitted when it was absent and no setting has been authored.
- `snap` never enters `.mskl` or `.mbin` export and does not change runtime
  format versions, C ABI v1, or the Agent/MCP surface.

### `animation_edits`

Optional ordered animation-catalog operations applied to the referenced base skeleton before
`timeline_edits` are merged. Projects that omit this array keep the base animation catalog unchanged.

```json
"animation_edits": [
  { "op": "create", "name": "idle_copy", "animation": {} },
  { "op": "rename", "from": "idle", "to": "idle_main" },
  { "op": "set_duration", "name": "idle_main", "duration": 1.5 },
  { "op": "delete", "name": "walk" }
]
```

- `create` stores a complete animation JSON object. Duplicate therefore deep-copies timeline families unknown to the current editor and remains independent of later source edits.
- `rename` atomically remaps the animation catalog, mixing entries, editor timeline overlays, and compatible preview/queue references.
- `set_duration` authors the target animation's explicit runtime `duration` in seconds. `name` resolves
  against the catalog produced by all preceding operations. The value must be finite, non-negative,
  representable by runtime animation-time storage, and no shorter than the target's last authored key.
  The editor stores the normalized applied value and never substitutes `max(requested, inferred)` for
  an invalid manual request.
- `delete` removes the animation plus its mixing entries and editor timeline overlays.
- Operations are applied in array order. Empty names, missing sources, duplicate destinations, and deleting the last remaining animation are rejected.
- Creating or moving a key past an existing explicit boundary extends that boundary in the same
  transaction. Removing a key or moving it left never shrinks an explicit duration automatically.
- Projects that omit `set_duration` retain the base asset's authored presence or inferred last-key
  fallback. Unknown future animation-edit operation objects are retained losslessly and ignored by
  this version's materializer so load/save does not erase additive data it does not understand.
- The exported `.mskl`/`.mbin` contains the resulting animation catalog; `animation_edits` remains editor-only and does not change runtime format versions.

### `timeline_edits`

Editor-side overrides that have not yet been exported into runtime assets:

- bone transform edits
- mesh deform edits
- draw-order edits
- event edits
- slot light-color edits
- slot attachment edits

The exported runtime path merges these edits back into the `.mskl` animation layout.

### `mesh_edits`

Current editor mesh-authoring payload:

- `weights`

This stores per-skin, per-slot, per-attachment mesh weight overrides.

### `constraint_edits`

Current editor constraint-authoring payload:

- `ik`
- `path`
- `transform`
- `physics`

The export path translates these sections directly into runtime root-level constraint arrays.

### `parameter_model`

Optional editor-only authoring source for parameter modeling. Its logical empty value is:

```json
{
  "parameters": [],
  "groups": [],
  "deformers": [],
  "blend_shapes": [],
  "art_paths": [],
  "expressions": [],
  "lip_sync": {}
}
```

When every known family is empty and there are no unknown additive fields, `save_project()` omits `parameter_model` entirely so existing projects do not change serialization. Loading an absent section produces the logical empty value above.

MAR-122 promotes `parameters` and `groups` to typed project data; MAR-123~126 promote `blend_shapes`, `deformers`, `art_paths`, `expressions`, and `lip_sync`. Every known family is therefore typed in `ProjectData`. Each typed record retains its original JSON source so unknown additive fields at the section, entry, and nested-keyform levels survive load/save even when known fields are rewritten.

This section preserves high-level authoring data that may be richer than runtime export data: group UI state, deformer lattice controls, keyform capture metadata, expression panel state, and lip-sync mapping configuration. Stable ids are immutable after creation and all references remain id based.

Export maps this source into runtime root sections as follows:

| `.marrow.parameter_model` | `.mskl` / `.mbin` root section |
| --- | --- |
| `parameters` | `parameters` |
| `groups` | `parameterGroups` |
| `blend_shapes` | `parameterShapes` |
| `deformers` | `parameterDeformers` |
| `art_paths` | `artPaths` |
| `expressions` | `expressions` |
| `lip_sync` | `lipSync` |

Persistent parameter/group/shape/deformer/expression/lip-sync mutations pass through one `Project|Runtime|Preview` transaction, mark the project dirty, and rebuild exported runtime preview atomically. Dependency-invalid deletes and failed candidate builds roll back all three impacts.

Direct preview values are deliberately not fields of `parameter_model`. Parameter sliders, numeric preview input, and agent `parameter.set` are Preview-only, undoable, mergeable for one continuous drag, and non-dirty; they are never saved to `.marrow` or exported to `.mskl`/`.mbin`.

Keyform capture compares coordinates with `1e-9 * max(1, parameter range)`. A collision is replaced only after explicit GUI confirmation or agent `replace:true`. Blend-shape capture copies animation-FFD-only offsets rather than recursively evaluated parameter output; warp capture copies the current evaluated lattice and rotation capture copies the current angle.

### `atlas_packs`

Optional editor-only atlas packing definitions for source sprites and generated atlas output metadata.

Common fields:

- `atlas`
- `atlas_name`
- `padding`
- `trim`
- `bleed`
- `filter_min`
- `filter_mag`
- `wrap_x`
- `wrap_y`
- `premultiplied_alpha`
- `sprites`

Each sprite entry currently uses:

- `name`
- `image`
- `origin_x`
- `origin_y`

## Reference fixtures

Use these checked-in files when updating or validating the formats:

- `assets/fixtures/player_idle.mskl`
- `assets/fixtures/player_idle.mbin`
- `assets/fixtures/player_idle.matl`
- `assets/fixtures/player_idle.marrow`
- `assets/fixtures/atlas_pack_smoke/atlas_pack_project.marrow`
- Validated MAR-122~128 fixtures: `parameter_face_basic.mskl`, `parameter_face_basic.matl`, `parameter_face_basic.marrow`, `parameter_deformer_grid.mskl`, `parameter_expression_lipsync.mskl`, and `art_path_stroke.mskl`; the comprehensive binary comparison output is generated as `/tmp/marrow_parameter_face_basic.mbin`

For fixture-to-feature mapping and validation commands, see [Fixtures](fixtures.md).
