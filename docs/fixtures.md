# Marrow Fixture Assets

These fixtures turn the format examples in [docs/discription.md](discription.md) into checked-in JSON assets that future runtime loader tests can reuse without rewriting sample data.

## Canonical pair

- `assets/fixtures/player_idle.mskl`
- `assets/fixtures/player_idle.matl`

## Mapping to `docs/discription.md`

### `player_idle.mskl`

- `marrow`, `skeleton`, `bones`, `slots`, and the `idle` animation come directly from the `.mskl` draft in section 5.
- The `idle.bones.spine.rotate` keys keep the same three interpolation encodings documented in sections 5 and 6:
  - `"linear"`
  - `[0.25, 0.1, 0.75, 0.9]`
  - `"stepped"`
- Slot attachment names stay aligned with the draft example so atlas lookups can use `body` and `arm_l` verbatim.

### `player_idle.matl`

- The `.matl` fixture is intentionally narrow because section 11 still marks the full `.matl` design as undecided.
- The checked-in atlas metadata covers the fields upcoming loader work needs immediately:
  - atlas identity and image path
  - atlas dimensions
  - named regions that match the `.mskl` slot attachment names
- Region names `body` and `arm_l` are the canonical bridge between the fixture skeleton and fixture atlas metadata.

## Intent for future stories

- Treat these files as the default parser fixtures unless a later story explicitly expands the format.
- When the `.matl` format is formalized, update this document in the same change so the fixture-to-spec mapping stays explicit.
