# Marrow Fixture Assets

These fixtures turn the format examples in [docs/discription.md](discription.md) into checked-in runtime assets that future loader tests can reuse without rewriting sample data.

## Canonical runtime assets

- `assets/fixtures/player_idle.mskl`
- `assets/fixtures/player_idle.mbin`
- `assets/fixtures/player_idle.matl`

## Editor project fixture

- `assets/fixtures/player_idle.marrow`
- `assets/fixtures/atlas_pack_smoke/atlas_pack_project.marrow`

## Focused runtime fixtures

- `assets/fixtures/ik_constraints.mskl`
- `assets/fixtures/skin_inherit_constraints.mskl`
- `assets/fixtures/path_transform_constraints.mskl`
- `assets/fixtures/physics_constraints.mskl`

## Spine import fixtures

- `assets/fixtures/spine_import_sample.json`
- `assets/fixtures/spine_import_sample.mskl`
- `assets/fixtures/spine_import_sample.atlas`
- `assets/fixtures/spine_import_sample_hero_page.matl`
- `assets/fixtures/spine_import_sample_fx_page.matl`

## PSD import fixtures

- `assets/fixtures/psd_import_sample.psd`
- `assets/fixtures/psd_import_sample_reimport.psd`

## Mapping to `docs/discription.md`

### `player_idle.mskl`

- `marrow`, `skeleton`, `bones`, `slots`, and the `idle` animation come directly from the `.mskl` draft in section 5.
- `skins` now covers the Skin System and Linked Mesh notes later in the document:
  - `default` keeps the original setup-pose `body` and `arm_l` region attachments so renderer validation stays stable.
  - `default` also adds a `spawn_anchor` point attachment and `hurtbox` bounding-box attachment on dedicated `arm_l` slots so runtime validation can prove non-rendered attachment kinds without changing atlas requirements.
  - `mesh_base` holds the canonical `body_mesh` geometry that linked skins inherit from, including explicit per-vertex bone influences for the weighted-mesh parser.
  - `warrior` and `mage` swap the `body` slot through linked meshes without duplicating the parent mesh arrays, and both opt into parent-mesh deform inheritance through the linked-mesh `deform` flag.
  - `mage_arm` is a partial skin used to validate runtime skin composition on top of another skin.
- Root-level `events` now mirrors the Events section:
  - `footstep` carries shared int/float/string defaults plus optional audio metadata.
  - `dust_vfx` covers a non-audio gameplay hook so the runtime can prove both defaulted and overridden payload fields.
- Root-level `mixing` now mirrors the Animation Mixing extension:
  - `default_mix` is `0.2` so track fades have a deterministic fallback in runtime tests.
  - the single wildcard entry `* -> aim` resolves the upper-body `attack -> aim` crossfade without hardcoding mix pairs in the sample harness.
- The `idle.bones.spine.rotate` keys keep the same three interpolation encodings documented in sections 5 and 6:
  - `"linear"`
  - `[0.25, 0.1, 0.75, 0.9]`
  - `"stepped"`
- The same `idle.bones.spine` track now extends the draft with `translate`, `scale`, and `shear` timelines so runtime stories can validate all bone transform channels against one canonical fixture.
- Translate keys use the same `x`/`y` property names as setup-pose bone transforms, while scale and shear keys mirror the runtime `scaleX`/`scaleY` and `shearX`/`shearY` channels as animated pairs.
- The `body` slot now carries the MAR-017 presentation defaults:
  - `blend: "screen"` exercises a non-default slot blend mode in the renderer validation path.
  - `color: "ffcc99ff"` and `dark: "336699ff"` provide the setup-pose light/dark tint pair used by two-color tint validation.
  - `arm_l` keeps `blend: "normal"` so the same fixture still covers the default slot presentation path.
- `fx_mask` and `spark_fx` now cover MAR-021 end to end:
  - `fx_mask` is a clipping attachment authored as a four-vertex polygon on the `spine` bone and ends on the `spark_fx` slot, so the renderer can prove slot-scoped mask activation and release from the checked-in scene.
  - `spark_fx` keeps a normal region attachment type but adds a `sequence` block with `frames`, `fps`, `mode`, and `setup_frame`, so the runtime can advance atlas-backed effect frames without changing the existing slot timeline format.
  - the runtime resolves those authored frame names directly to atlas regions `spark_fx_0` through `spark_fx_3`, which keeps the checked-in fixture explicit and easy to inspect during smoke tests.
- The `idle.slots.body` track now validates the MAR-011 slot timeline slice:
  - `attachment` swaps the body slot between `body` and `warrior_body`.
  - `color` animates RGBA together, so the same fixture proves both tint and alpha evaluation.
  - `drawOrder` lists every slot explicitly, including the clipping, sequence, point, and bounding-box slots, so runtime and renderer validation can assert animated slot reordering without dropping auxiliary attachment data.
- `idle.deform.body.body_mesh` now validates MAR-018:
  - the deform timeline keys one x/y offset pair per mesh vertex on the shared `body_mesh` source attachment.
  - `warrior_body` inherits those offsets at playback time through its linked-mesh `deform` flag, so the runtime proves FFD on top of weighted mesh skinning instead of on a separate rigid path.
- `idle.events` now validates MAR-012 with three keyed callbacks:
  - two simultaneous events at `0.3` prove non-decreasing event keys are preserved in file order.
  - per-key overrides change only the fields that differ from the shared event definitions.
  - the second footstep key reuses the definition defaults for audio and float data while overriding `int`, `string`, and `balance`.
- `attack` and `aim` now validate MAR-013:
  - `attack` is a short non-loop upper-body rotation used to prove track interruption and end/dispose after mixing out.
  - `aim` is a looping upper-body hold used to prove wildcard mix resolution, loop completion callbacks, queue promotion, and empty-animation fade-out back to setup pose.
- Slot attachment names stay aligned with the draft example so atlas lookups can use `body` and `arm_l` verbatim, while animated swaps still resolve through the same atlas metadata.
- Point attachment validation uses the `spawn_anchor` slot with local `x`, `y`, and `rotation` data so the runtime can expose world-space spawn anchor transforms from the active pose.
- Bounding-box validation uses the `hurtbox` slot with a four-vertex polygon so the runtime can transform authored hit-detection shapes into world space.
- Weighted mesh validation now uses the `warrior_body` linked mesh in setup pose and a manually rotated `arm_l` test pose so the same fixture proves both shared mesh inheritance and multi-bone deformation.
- Sequence playback validation advances the `spark_fx` slot through the atlas regions `spark_fx_0` through `spark_fx_3`, while clipping validation confirms only that slot is masked by `fx_mask`.
- The canonical fixture now also carries appended helper bones and a `guide` slot for editor-only authoring coverage:
  - `ik_upper`, `ik_lower`, `ik_tip`, and `ik_target` give the editor shell a stable two-bone IK chain to author without touching the renderer-facing body rig.
  - `path_a`, `path_b`, and `path_c` pair with the `guide_path` attachment so the editor can preview path constraints against the same explicit Bezier points used by the dedicated runtime fixture.
  - `transform_source` and `transform_target` isolate transform constraint authoring from the animated gameplay bones.
  - `pivot`, `ribbon_01`, `ribbon_02`, and `ribbon_tip` provide a deterministic secondary-motion chain for physics constraint preview and export validation.

### `player_idle.mbin`

- `player_idle.mbin` is the compact binary encoding of the same `player_idle.mskl` document for production-style runtime loading.
- The file uses the documented `MBIN` header and versioned payload with:
  - varint lengths and table indices
  - a shared string table for object keys and string values
  - float32 numeric payloads
  - a packed boolean bitfield
- Regenerate it from the JSON source with:
  - `./build/marrow_inspect --export-binary assets/fixtures/player_idle.mbin assets/fixtures/player_idle.mskl`
- Validate JSON and binary equivalence with:
  - `./build/marrow_inspect --compare assets/fixtures/player_idle.mbin assets/fixtures/player_idle.mskl`

### `player_idle.matl`

- The `.matl` fixture is intentionally narrow because section 11 still marks the full `.matl` design as undecided.
- The checked-in atlas metadata covers the fields upcoming loader work needs immediately:
  - atlas identity and image path
  - atlas dimensions
  - named regions that match both the setup-pose attachments, the alternate skin attachments, and the sequence frames referenced by `spark_fx`
- Region names `body`, `arm_l`, `warrior_body`, `mage_body`, `mage_arm_l`, and `spark_fx_0` through `spark_fx_3` are the canonical bridge between the fixture skeleton and fixture atlas metadata.

### `spine_import_sample.atlas` and imported `.matl` pages

- The Spine import fixtures now cover both importer directions:
  - `spine_import_sample.json` -> `spine_import_sample.mskl`
  - `spine_import_sample.atlas` -> `spine_import_sample_hero_page.matl` and `spine_import_sample_fx_page.matl`
- The atlas source fixture intentionally uses two pages so the importer has to emit multiple `.matl` files from one `.atlas` input.
- `hero_page` preserves a rotated `weighted_region` entry with `rotate: 90`, plus explicit `origin` metadata for `hero_body` and `weighted_region`.
- `fx_page` preserves a second-page `plain_region` entry plus a sequential `spark_fx_0` region, which gives the smoke test stable cross-page lookup coverage against imported Spine attachment region names.

### `psd_import_sample.psd` and `psd_import_sample_reimport.psd`

- These fixtures exercise the Photoshop-style art import path without introducing a new checked-in runtime bundle:
  - `psd_import_sample.psd` contains three visible paint layers: `body`, `arm_l`, and `shadow`.
  - The `body` and `arm_l` layers live inside a `torso` folder so the importer can emit a parent `torso` bone hint above the generated slots.
  - `shadow` stays at the root of the PSD stack so the smoke test covers both grouped and ungrouped layers.
- `marrow_psd_import_smoke` imports the first PSD into a temporary `.mskl` + `.matl` bundle, validates slot creation and renderer placement, injects a small authored `idle` animation, then re-imports `psd_import_sample_reimport.psd` over the same skeleton.
- The re-import fixture only moves the `body` layer from `(16, 12)` to `(20, 14)`, which keeps names stable while proving the importer updates attachment placement and preserves the pre-existing animation payload.

### `player_idle.marrow`

- The minimal `.marrow` fixture covers the editor-side project split implied by sections 4 and 9 in [docs/discription.md](discription.md):
  - the root `runtime` object points at the canonical `player_idle.mskl` and `player_idle.matl` files without embedding any exported runtime payload inline.
  - the root `editor` object keeps authoring-only metadata such as the project name, preview animation, preview skin set, export directory, free-form notes, and viewport framing.
  - `editor.viewport.onion_skin` persists the editor-only ghost-frame preview state: the on/off toggle, frame-vs-keyframe mode, frame-0 anchoring, before/after counts, and the sampling step.
  - the root `timeline_edits` object now mirrors the `.mskl` animation layout for editor-authored overrides that have not been exported yet:
    - `bones` stores transform track edits by animation and bone name.
    - `deform` stores FFD track edits by animation, slot, and mesh attachment name using the same per-key `vertices` payload the runtime `.mskl` format consumes.
    - `drawOrder` stores full slot-stack overrides per keyframe so editor-authored presentation reordering round-trips through the runtime `drawOrder` array.
    - `events` stores named event keys plus optional int/float/string/audio/volume/balance overrides using the same payload fields as runtime event timelines.
  - the root `mesh_edits` object stores authoring-time weighted-mesh overrides that have not been exported yet:
    - `weights` stores per-skin/per-slot mesh influence edits, including per-vertex bone names, bind-space offsets, and normalized weights for brush-based painting.
  - the root `constraint_edits` object mirrors the runtime root-level constraint arrays for project-authored overrides that have not been exported yet:
    - `ik` stores editor-authored IK chains by constraint name, constrained bones, target, mix, and bend direction.
    - `path` stores editor-authored slot binding, constrained chain, position/spacing, and rotate/translate mixes.
    - `transform` stores source/target binding plus rotate/translate/scale/shear mixes and the optional offset block.
    - `physics` stores secondary-motion chains plus inertia, damping, strength, gravity, wind, and mix values.
- the checked-in fixture overrides `idle.bones.spine.rotate`, `idle.deform.body.body_mesh`, `idle.drawOrder`, and `idle.events`, so the editor shell and project smoke validation can prove that project-authored transform, FFD, draw-order, and event edits supersede the referenced runtime skeleton until export time.
- the authored `idle.drawOrder[1]` key intentionally reorders the visible slots to `body -> fx_mask -> spark_fx -> arm_l` so the sample project still proves draw-order authoring while keeping the exported clipping setup render-valid for the end-to-end renderer smoke path.
- The checked-in fixture also persists one authored IK, path, transform, and physics constraint edit so the editor shell and project smoke validation can prove that project-authored constraints preview on the helper rig, export into root-level runtime arrays, and round-trip back through the exported `.mskl`.
- `marrow_project_smoke` uses this fixture to prove that the editor-side loader resolves relative asset references, applies the stored transform, deform, mesh-weight, draw-order, event, and constraint edits on top of the referenced runtime assets, round-trips those edits through an exported `.mskl`, and still replays the exported weighted mesh deform pose and authored constraint data at runtime.

## End-to-end sample project checklist

Use the checked-in `assets/fixtures/player_idle.marrow` project when you need to prove the full editor-to-runtime path for a new change:

1. Validate that the authored project fixture parses:
   - `python3 -m json.tool assets/fixtures/player_idle.marrow > /dev/null`
   - `./build/marrow_editor_shell --project assets/fixtures/player_idle.marrow --auto-close 2`
2. Export the runtime bundle from the project and reload it through the project smoke harness:
   - `./build/marrow_project_smoke assets/fixtures/player_idle.marrow --export-runtime /tmp/player_idle_project_export.mskl --export-binary /tmp/player_idle_project_export.mbin`
3. Compare the exported JSON and binary runtime payloads:
   - `./build/marrow_inspect --compare /tmp/player_idle_project_export.mbin /tmp/player_idle_project_export.mskl`
4. Render the exported bundle with the copied atlas metadata:
   - `./build/marrow_renderer_sample /tmp/player_idle_project_export.mskl /tmp/player_idle.matl`

Why this is the preferred contributor flow:

- `player_idle.marrow` is the only checked-in asset that exercises editor-authored timeline overrides, constraint edits, runtime export, atlas bundle copy, binary export, runtime reload, and renderer preparation as one vertical slice.
- `marrow_project_smoke` validates that the exported `.mskl` and copied `.matl` files are loadable without manual patching, and `marrow_renderer_sample` closes the loop by preparing the exported runtime data for rendering with the atlas bundle that the export step emitted into `/tmp`.

### `atlas_pack_smoke/`

- `atlas_pack_project.marrow` is the checked-in editor-project fixture for the atlas packer path:
  - the root `runtime` section points at `atlas_pack_source.mskl` and the generated `packed_sprites.matl`.
  - the root `atlas_packs` array captures the new editor-side atlas authoring metadata: output atlas path, padding, trim, bleed, filter/wrap state, and the 24 source sprite PNG paths.
- `atlas_pack_source.mskl` is intentionally minimal:
  - one `root` bone drives 24 slots so each source image maps directly to one region attachment without unrelated animation or constraint data.
  - the fixture keeps an empty `idle` clip so the standard runtime loader path stays valid.
- `sprites/sprite_*.png` are the individual authored source images used by the atlas packer smoke:
  - each sprite includes transparent whitespace around an opaque body so trim/offset behavior is explicit and repeatable.
  - the images are small but varied in size, which keeps the checked-in atlas compact while still exercising non-trivial rectangle placement.
- `packed_sprites.matl` and `packed_sprites.png` are the generated reference outputs for that project:
  - the `.matl` records trimmed region sizes plus origin offsets derived from the untrimmed source image centers.
  - the `.png` preserves 1px edge bleed inside the default 2px padding band, which is the seam-prevention behavior the smoke test validates.
- Preferred validation paths for this fixture:
  - `./build/marrow_atlas_packer_smoke`
  - `./build/marrow_project_smoke assets/fixtures/atlas_pack_smoke/atlas_pack_project.marrow --export-runtime /tmp/atlas_pack_project_export.mskl`

### `ik_constraints.mskl`

- The root-level `ik` array now mirrors the IK Constraint extension in [docs/discription.md](discription.md):
  - `arm_positive` and `arm_negative` are two-bone chains with identical reach distances but opposite `bendPositive` values, so the runtime can prove bend direction changes without changing the authored target offset.
  - `turret_reach` is a one-bone full-mix chain that lands exactly on its target bone.
  - `turret_half_mix` reuses the same one-bone layout with `mix: 0.5` so the runtime can prove partial IK blending instead of only full overrides.
- The fixture keeps a single dummy slot and empty animation because the loader still requires `slots` and `animations`, but the validation focus is the parsed constraint registry plus the solved world transforms.
- The runtime smoke test also moves `target_full` after load, which proves the solver reacts to runtime target updates rather than only setup-pose data.

### `skin_inherit_constraints.mskl`

- The fixture isolates the MAR-025 format slice so renderer-oriented samples do not need to absorb another runtime-only behavior:
  - `animations.toggle_inherit.bones.child.inherit` keys `inheritRotation`, `inheritScale`, and `inheritReflection` directly, which gives the runtime deterministic full-on, reflection-off, fully-off, and restored samples for inheritance evaluation.
  - the `controller` bone uses a reflected setup scale (`scaleY: -0.5`), so the smoke test can prove `inheritReflection: false` removes mirrored parent axes without disabling rotation or scale inheritance entirely.
  - `skins.cape.bones` activates the otherwise-extra `cape_target` bone only when that skin is active.
  - `skins.cape.transform` activates the `cape_pull` transform constraint only with the same skin, so the smoke test can prove a skin swap changes pose solving instead of only attachment lookup.
  - runtime validation now applies `toggle_inherit` while the `cape` skin is active, which proves the skin-scoped bone and transform constraint survive normal animation playback and setup-pose resets until the runtime explicitly switches back to `default`.
- The fixture keeps a single dummy slot and region attachment because the loader still requires slots and skins, but all validation is runtime-side and atlas-free.

### `path_transform_constraints.mskl`

- The root-level `path` array mirrors the Path Constraint notes in [docs/discription.md](discription.md):
  - `rope_follow` binds the `guide` slot to a cubic path attachment encoded as two straight Bezier segments, which keeps the authored geometry easy to inspect while still exercising path attachment parsing.
  - the constrained `path_a -> path_b -> path_c` chain uses `position: 0.1` plus `spacingMode: "percent"` and `spacing: 0.3`, so the smoke test can assert exact world-space placements 20, 80, and 140 units along the 200-unit guide.
  - full `rotateMix` and `translateMix` verify both tangent-following rotation and path-position translation in one setup-pose solve.
- The root-level `transform` array mirrors the Transform Constraint notes:
  - `mirror_source` copies the `source` bone onto `target` with independent rotate, translate, scale, and shear mixes.
  - the nested `offset` block exercises every supported offset channel so the runtime can prove mixed copy-plus-offset evaluation instead of only zero-offset mirroring.
- The fixture intentionally avoids atlas-backed attachments because the story is runtime-only; the single `guide` slot exists solely to host the authored path attachment for constraint evaluation.

### `physics_constraints.mskl`

- The root-level `physics` array mirrors the Physics Constraint notes in [docs/discription.md](discription.md):
  - `ribbon_secondary` constrains the directly chained `ribbon_01 -> ribbon_02` pair, keeping the authored hierarchy small enough to inspect while still providing a multi-bone secondary-motion chain.
  - `inertia: 0.85`, `damping: 4.0`, and `strength: 18.0` give the smoke test a deterministic spring response that visibly lags behind the driven `pivot` bone before settling.
  - the `gravity` and `wind` vectors stay non-zero so the runtime can preserve and step environmental force metadata alongside the spring parameters during the numeric secondary-motion test.
- The fixture keeps a single dummy slot and empty animation because the loader still requires `slots` and `animations`, but the validation focus is the parsed physics metadata plus the numeric world-space motion over repeated simulation steps.

## Intent for future stories

- Treat these files as the default parser fixtures unless a later story explicitly expands the format.
- When the `.matl` format is formalized, update this document in the same change so the fixture-to-spec mapping stays explicit.
