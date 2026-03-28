# Marrow Format Spec

This page documents the currently implemented Marrow asset formats. It is intentionally aligned with the checked-in parsers and fixtures, not just the original design draft.

## Shared conventions

- Runtime JSON assets carry a root `marrow` string and a numeric `version`.
- The current runtime accepts `.mskl` version `1` and `.matl` version `1`.
- Paths stored inside JSON assets are relative paths unless exported otherwise by the caller.
- The canonical examples live under `assets/fixtures/` and are cross-referenced in [Fixtures](fixtures.md).

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
- Use `inspect_skeleton_binary()` or `marrow_inspect --compare` to validate JSON/binary equivalence and packed-animation metadata.

## `.marrow`

Purpose: editor project document that references runtime assets and stores authoring-only state.

Current loader behavior:

- Parsed from JSON by `load_project()`.
- Uses a root `marrow` string but no separate numeric `version` field today.

Top-level keys:

- `marrow`
- `runtime`
- `editor`
- `timeline_edits`
- `mesh_edits`
- `constraint_edits`
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

`viewport` currently includes:

- `pan_x`
- `pan_y`
- `zoom`
- `onion_skin`
- `debug_overlay`

### `timeline_edits`

Editor-side overrides that have not yet been exported into runtime assets:

- bone transform edits
- mesh deform edits
- draw-order edits
- event edits

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

For fixture-to-feature mapping and validation commands, see [Fixtures](fixtures.md).
