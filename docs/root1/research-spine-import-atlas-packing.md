# Research: Spine File Import & Texture Atlas Packing

**Date**: 2026-03-27
**Confidence**: High (0.85) -- based on official documentation and community sources
**Sources**: 12 primary sources across Esoteric Software docs, GitHub, community forums, academic references

---

## 1. Spine Export File Formats (.json / .skel)

### 1.1 JSON Format Structure

The Spine JSON format is the serialized form of a `SkeletonData` instance. The official specification is documented at `esotericsoftware.com/spine-json-format`.

**Top-level structure:**

```json
{
  "skeleton": { "hash": "...", "spine": "4.2.xx", "x": 0, "y": 0, "width": 0, "height": 0 },
  "bones":  [ { "name": "root", "length": 0, "rotation": 0, "x": 0, "y": 0, "scaleX": 1, "scaleY": 1, "shearX": 0, "shearY": 0, "transform": "normal" } ],
  "slots":  [ { "name": "...", "bone": "...", "color": "FFFFFFFF", "attachment": "..." } ],
  "ik":     [ { "name": "...", "order": 0, "bones": ["..."], "target": "...", "mix": 1, "softness": 0, "bendPositive": true } ],
  "transform": [ { "name": "...", "order": 0, "bones": ["..."], "target": "..." } ],
  "path":   [ { "name": "...", "order": 0, "bones": ["..."], "target": "..." } ],
  "physics": [ { "name": "...", "bone": "..." } ],
  "skins":  [ { "name": "default", "attachments": { "slotName": { "attachmentName": { ... } } } } ],
  "events": { "eventName": { "int": 0, "float": 0, "string": "" } },
  "animations": { "animName": { "bones": {}, "slots": {}, "ik": {}, "transform": {}, "deform": {}, "events": [], "draworder": [] } }
}
```

**Key entities and their relationships:**

| Entity | Purpose | Key Fields |
|--------|---------|------------|
| skeleton | Metadata | hash, spine version, bounds (x,y,w,h), images path |
| bones | Skeletal hierarchy | name, parent, length, rotation, x, y, scaleX/Y, shearX/Y, transform mode |
| slots | Draw order + attachment container | name, bone reference, color (RGBA hex), attachment name, blend mode |
| skins | Attachment lookup by slot+name | Map of slot -> name -> attachment definition |
| animations | Timeline keyframes | bones (rotate/translate/scale/shear), slots (attachment/color), deform, ik, events, draworder |

**Attachment types in skins:**

| Type | Key Fields |
|------|------------|
| region | x, y, scaleX, scaleY, rotation, width, height, color, path |
| mesh | uvs, triangles, vertices (or weighted), hull, edges, width, height, path |
| weightedmesh | Same as mesh but vertices encode bone influences: [boneCount, boneIdx, bindX, bindY, weight, ...] |
| boundingbox | vertexCount, vertices |
| path | closed, constantSpeed, lengths, vertexCount, vertices |
| point | x, y, rotation |
| clipping | end (slot name), vertexCount, vertices |

**Weighted vertex encoding (critical):** A mesh is weighted if `len(vertices) > len(uvs)`. For weighted meshes, the vertices array encodes per-vertex: `[numBones, boneIndex0, bindPosX0, bindPosY0, weight0, boneIndex1, ...]`.

**Animation timeline types:**

- **Bone timelines**: rotate, translate, scale, shear (each with time + value keyframes)
- **Slot timelines**: attachment (visibility switching), color (tinting), twoColor
- **Deform timelines**: per-skin, per-slot mesh vertex offsets
- **Constraint timelines**: IK mix, transform, path position/spacing/mix
- **Draw order timelines**: offset-based reordering of slots
- **Event timelines**: named events at specific times

**Curve interpolation**: Keyframes support `linear` (default), `stepped`, or bezier curves. Bezier is encoded as `"curve": [cx1, cy1, cx2, cy2]` (4 control points for a cubic bezier).

### 1.2 Binary Format (.skel)

The binary format mirrors the JSON structure but uses compact encoding:

- **varint+**: Variable-length integer encoding (7 bits per byte, high bit = continuation)
- **ref string**: Index into a string table built during parsing (first occurrence stores the string, subsequent ones use index)
- **float**: Standard 32-bit IEEE 754
- **boolean**: Single byte (0 or 1)
- **color**: 4 bytes RGBA

**Binary parsing order** (must be sequential):
1. Header (hash, version string, bounds)
2. String table
3. Bones array
4. Slots array
5. IK constraints
6. Transform constraints
7. Path constraints
8. Physics constraints
9. Default skin, then named skins
10. Events
11. Animations

**Critical gotcha**: The binary format is extremely version-sensitive. Even minor Spine version bumps can change the binary layout. The JSON format is more forgiving across minor versions.

### 1.3 Atlas Format (.atlas)

Spine uses a custom text-based atlas format (derived from libgdx). Structure:

```
page-image.png
size: 1024,1024
format: RGBA8888
filter: Linear,Linear
repeat: none
pma: true

regionName
  bounds: 0, 0, 128, 64
  offsets: 2, 3, 132, 70
  rotate: true
  index: -1
```

**Page properties**: image filename, size, format, min/mag filter, repeat mode, premultiplied alpha flag.

**Region properties**: bounds (x, y, w, h in atlas), offsets (trim info: offsetX, offsetY, originalW, originalH), rotate (90-degree CW rotation flag), index (for sequences).

**Rendering considerations when rotation is enabled**: If `rotate: true`, the region is stored rotated 90 degrees clockwise in the atlas. UVs must compensate. Width/height in bounds refer to the *rotated* dimensions on the atlas.

**Whitespace stripping**: When trimmed, `offsets` stores the original size and the offset needed to position the trimmed image correctly during rendering.

---

## 2. Building a Spine Importer: Lessons & Gotchas

### 2.1 Prior Art

Several projects have built custom Spine importers:

| Project | Approach | Notes |
|---------|----------|-------|
| Castle Game Engine | Full JSON importer | Supports Spine 3.x and 4.x JSON, all major features |
| Defold | Custom importer | JSON only, specific Spine version coupling |
| coa_tools (Blender) | Export-to-Spine-format | Chose Spine JSON as interchange to avoid new runtime per engine |
| SpineBinaryConverter (GitHub) | Binary-to-JSON converter | Uses spine-csharp internally, version-specific (3.4) |
| Custom C# engine users | spine-csharp integration | Most common path; fork SkeletonJson/SkeletonBinary classes |

### 2.2 Critical Gotchas

1. **Version coupling**: Spine data format changes with each major/minor version. The `skeleton.spine` field in JSON tells you the version. You MUST handle version differences or target a specific version range.

2. **Binary format fragility**: Binary (.skel) parsing is extremely sensitive to version changes. Any added field breaks all older parsers. **Recommendation**: Support JSON first, add binary later if performance demands it.

3. **Weighted mesh vertices**: The most error-prone part. The conditional encoding (weighted vs unweighted) based on `len(vertices) > len(uvs)` is a frequent source of bugs. Parse carefully.

4. **Bezier curve handling**: The binary format stores bezier data inline with timeline keyframes. CurveTimeline.SetBezier has specific expectations about frame/value indices. Off-by-one errors here produce visible animation glitches.

5. **Transform modes**: Bones have transform inheritance modes (Normal, OnlyTranslation, NoRotationOrReflection, NoScale, NoScaleOrReflection). Each requires different matrix math during world transform computation.

6. **Constraint evaluation order**: IK, transform, and path constraints must be applied in the order specified by their `order` field. Getting this wrong produces incorrect poses.

7. **Attachment lookup chain**: When resolving attachments, check the active skin first, then fall back to the default skin. The key is (slotIndex, attachmentName), not just the attachment name.

8. **Physics constraints**: Added in Spine 4.2. Older format files will not have these. Your parser must handle their absence gracefully.

9. **Draw order**: The draw order timeline uses offsets, not absolute positions. You compute the new order by applying offsets to the setup pose draw order.

10. **Atlas rotation mismatch**: Spine's atlas rotation is 90 degrees clockwise, which differs from TexturePacker's convention. Multiple community reports confirm this as a source of rendering bugs.

### 2.3 Recommended Implementation Strategy

**Phase 1 -- JSON Parser (MVP)**:
- Parse `skeleton`, `bones`, `slots`, `skins` (region + mesh attachments only)
- Build bone hierarchy with world transforms
- Load atlas and resolve texture regions
- Render setup pose

**Phase 2 -- Animation Playback**:
- Parse bone timelines (rotate, translate, scale)
- Implement linear and bezier interpolation
- Parse slot timelines (attachment switching, color)
- Implement AnimationState with track mixing

**Phase 3 -- Advanced Features**:
- Weighted mesh / deform animations
- IK constraints (two-bone IK is the most common)
- Transform and path constraints
- Skins and runtime skin composition
- Draw order animation

**Phase 4 -- Binary Support (Optional)**:
- Port SkeletonBinary reader from spine-csharp or spine-c reference
- Implement varint decoding, string table, ref-string resolution

---

## 3. Texture Atlas Packing Algorithms

### 3.1 Algorithm Comparison

| Algorithm | Efficiency | Speed | Complexity | Rotation | Dynamic |
|-----------|-----------|-------|------------|----------|---------|
| **MaxRects** | Best for rectangles | Medium | Medium | Yes (opt) | No |
| **Skyline Bottom-Left** | Good | Fast | Low | No | Partial |
| **Guillotine** | Good | Fast | Low | Optional | No |
| **Shelf (NF/FF/BHF/BAF)** | Fair | Fastest | Lowest | Optional | Partial |
| **Binary Tree Split** | Good | Fast | Low | Optional | No |
| **Touching Perimeter (TPRF)** | Very Good | Slow | High | Yes | No |

### 3.2 MaxRects (Recommended)

MaxRects is the industry standard for offline atlas packing. It maintains a list of free rectangles and uses heuristics to choose placement:

**Heuristics (in order of typical quality):**
1. **Best Short Side Fit (BSSF)**: Minimizes the shorter leftover side. Generally best overall.
2. **Best Area Fit (BAF)**: Minimizes remaining area in the chosen free rect.
3. **Bottom-Left (BL)**: Places at the lowest available Y, then leftmost X.
4. **Contact Point (CP)**: Maximizes touching perimeter with other placed rects.
5. **Best Long Side Fit (BLSF)**: Minimizes the longer leftover side.

**MaxRects pseudocode:**

```
free_rects = [Rect(0, 0, atlas_w, atlas_h)]

for each sprite (sorted by area, descending):
    best_rect = None
    best_score = INFINITY

    for each free_rect in free_rects:
        if sprite fits in free_rect:
            score = heuristic(free_rect, sprite)
            if score < best_score:
                best_score = score
                best_rect = free_rect

    if best_rect:
        place sprite at best_rect position
        split free_rects by placed sprite (generate new free rects)
        merge overlapping free rects
```

**Implementation note**: The "Best" mode in TexturePacker tests ALL heuristics and picks the best result per sprite. This is slower but produces optimal packing.

### 3.3 Skyline Bottom-Left (stb_rect_pack)

Used by stb_rect_pack.h. Maintains a "skyline" representing the top edge of placed rectangles:

- Faster than MaxRects for large numbers of small items
- Slightly less space-efficient than MaxRects
- No rotation support in the default stb implementation
- Zero allocations (stack-friendly for C)

Good choice for: font atlas generation, runtime packing where speed matters.

### 3.4 Guillotine Algorithm

Simple and effective: place a rect, then cut the remaining space into two new rectangles (horizontal or vertical split). Repeat.

- Easiest to implement (~200 lines of code)
- Reasonable packing efficiency with good sorting
- Good starting point for a custom implementation

### 3.5 Runtime vs Offline Packing

| Aspect | Offline | Runtime |
|--------|---------|---------|
| **Speed** | Can take seconds, runs once | Must be sub-frame or amortized |
| **Quality** | Can try all heuristics, optimal | Must use fast heuristic |
| **Memory** | Temporary, freed after | Persistent atlas in GPU memory |
| **Flexibility** | Fixed at build time | Dynamic add/remove |
| **Best algorithm** | MaxRects (Best mode) | Skyline or Shelf |
| **Use case** | Sprite sheets, game assets | Font glyphs, UI elements, modding |

**Recommendation for Marrow**: Use MaxRects offline for asset pipeline atlas packing. For runtime atlas updates (e.g., skin composition), use Skyline.

---

## 4. Atlas Packing Tools & Libraries

### 4.1 Open Source Libraries (C/C++ Focus)

| Library | Language | License | Algorithm | Features |
|---------|----------|---------|-----------|----------|
| **stb_rect_pack** | C (single header) | Public Domain | Skyline BL | Zero allocs, simple API, no rotation |
| **rectpack2D** | C++ | MIT | Binary tree | Multi-sheet, very fast |
| **MaxRectsBinPack** (Jukka Jylanki) | C++ | Public Domain | MaxRects | Reference implementation, all heuristics |
| **smol-atlas** (Aras Pranckevicius) | C++ | MIT/Unlicense | Shelf variant | Dynamic add/remove, fastest in benchmarks |
| **rect_pack** (houmain/spright) | C++17 | MIT | Skyline + MaxRects | Multi-sheet, constraints |
| **maxrects-packer** (npm) | TypeScript | MIT | MaxRects | Multi-bin, rotation support |
| **free-tex-packer** | JS | MIT | MaxRects | Full tool, multiple export formats |
| **etagere** (Mozilla/Firefox) | Rust | MIT/Apache | Shelf | Battle-tested in Firefox, dynamic |
| **atlas-packer** (MIERUNE) | Rust | MIT | Custom | UV-aware, 3D model focused |

### 4.2 Benchmark Data (from smol-atlas)

Testing dynamic allocation/deallocation scenario:

| Library | GC cycles | Repacks | Time (ms) |
|---------|-----------|---------|-----------|
| smol-atlas | 800 | 127 | 9 |
| etagere (Rust) | 876 | 185 | 13 |
| shelf-pack-cpp | 1027 | 426 | 54 |
| stb_rect_pack | 576 | 578 | 97 |

For pure offline packing (no deallocation), stb_rect_pack and MaxRectsBinPack are equivalent.

### 4.3 Critical Atlas Features

For a Spine-compatible atlas packer, these features matter:

| Feature | Priority | Why |
|---------|----------|-----|
| **Padding** | Required | Prevents texture bleeding between regions (1-2px minimum) |
| **Trim/whitespace stripping** | High | Removes transparent borders, dramatically reduces atlas size |
| **Rotation (90-deg)** | High | Can improve packing 10-15% but adds UV complexity |
| **Bleed/extrude** | High | Duplicates edge pixels outward to prevent seam artifacts at mip levels |
| **Power-of-two sizing** | Medium | Some GPUs/APIs require POT textures |
| **Multi-page** | Medium | Overflow to additional atlas pages when one is full |
| **Premultiplied alpha** | Medium | Spine supports PMA; atlas must match renderer expectations |
| **Duplicate detection** | Low | Auto-alias identical sprites to save space |

### 4.4 Recommendation for Marrow

**Build pipeline (offline packer):**
- Use Jukka Jylanki's MaxRectsBinPack (public domain, ~500 lines C++) as the core algorithm
- Wrap with trim, pad, bleed, rotation logic
- Or integrate stb_rect_pack for simplicity if rotation is not needed

**Runtime packing (skin composition):**
- Use stb_rect_pack (single header, zero allocs, public domain)
- Or smol-atlas if dynamic add/remove is needed

---

## 5. Legal Considerations for Spine File Import

### 5.1 License Structure

Spine has a two-part licensing model:

1. **Spine Editor License** -- governs use of the editor tool and grants rights to the runtimes
2. **Spine Runtimes License** -- governs the runtime code libraries

Key sections from the Spine Editor License Agreement:

> **Section 2.1 (Product Integration)**: "You may integrate the Spine Runtimes into software [...] provided that each Product adds significant and primary functionality to the Spine Runtimes; and You have a valid Spine Editor license."

> **Section 2.4 (Requirements)**: "A valid Spine Editor license is required to: (a) integrate the Spine Runtimes into software or otherwise create derivative works of the Spine Runtimes; or (b) modify, adapt, develop, or otherwise create derivative works that contain the Spine Runtimes."

### 5.2 Can We Legally Parse Spine Files?

**The JSON/binary file format itself is NOT copyrightable.** File formats are functional specifications, not creative works. This is well-established in copyright law (Lotus v. Borland, Oracle v. Google for APIs).

**Three distinct legal paths:**

| Approach | Legal Status | Risk |
|----------|-------------|------|
| **A. Write your own parser from format documentation** | LEGAL. No Spine code used. Format is documented publicly. | Lowest risk. Esoteric Software publishes the format spec openly. |
| **B. Use the Spine Runtimes code** | Requires valid Spine Editor license per Section 2 | Medium risk. Must maintain license. |
| **C. Fork/modify the Spine Runtimes** | Requires valid Spine Editor license, derivative work | Highest obligation. License must be active at time of integration. |

### 5.3 Analysis

**Path A is the recommended approach for Marrow:**

- The Spine JSON format specification is publicly documented at `esotericsoftware.com/spine-json-format`
- The atlas format specification is publicly documented at `esotericsoftware.com/spine-atlas-format`
- Writing your own parser that reads these documented formats does NOT require a Spine license
- You are NOT using the Spine Runtimes (their code libraries)
- You are parsing a data format, not integrating their runtime
- Your users who created the Spine files will have their own Spine licenses (required to export from the editor)

**Key distinction**: The Spine Runtimes License covers the *runtime code* (SkeletonJson.cs, SkeletonBinary.cs, Animation.cs, etc.), NOT the data format. Parsing JSON is parsing JSON -- the format itself has no copyright protection.

**However:**
- Do NOT copy code from the Spine Runtimes source. Write your parser independently.
- Do NOT embed or link against any Spine Runtime libraries.
- Do NOT claim compatibility with "Spine Runtime" -- say "imports Spine export format" instead.
- Consider consulting legal counsel to confirm for your jurisdiction.

**Precedent**: Castle Game Engine, Defold, and numerous third-party runtimes (Construct3, GameMaker, Raylib, Bevy, etc.) all parse Spine formats. The community runtimes page on Esoteric Software's own site lists these third-party implementations, suggesting they accept this practice.

### 5.4 What Users Need

- Users who export from Spine need their own Spine Editor license (Essential: $69, Professional: $299, Enterprise: $2200/yr)
- Users who only consume animations in your tool do NOT need a Spine license (they are end-users of your product)
- Include a note in documentation: "Spine files require a Spine Editor license from Esoteric Software for creation"

---

## 6. Implementation Recommendations for Marrow

### 6.1 Spine Importer Architecture

```
spine_import/
  spine_json_parser.c       -- Parse .json skeleton data into internal structures
  spine_atlas_parser.c      -- Parse .atlas text format
  spine_to_marrow.c         -- Convert Spine data model to Marrow's internal format
  spine_import.h            -- Public API: marrow_import_spine(path) -> MarrowSkeleton*
```

**Data flow:**
```
.json file --> spine_json_parser --> SpineSkeletonData (intermediate)
.atlas file --> spine_atlas_parser --> SpineAtlasData (intermediate)
.png files --> image loader --> texture pages

SpineSkeletonData + SpineAtlasData --> spine_to_marrow --> MarrowSkeleton + MarrowAnimations
```

### 6.2 Minimum Viable Import (Phase 1)

1. Parse JSON skeleton metadata + bones + slots
2. Parse skins with region attachments only (skip mesh initially)
3. Parse atlas, resolve texture regions
4. Convert bone hierarchy to Marrow's bone representation
5. Render setup pose with region attachments

**Estimated effort**: 1-2 weeks for a competent C developer.

### 6.3 Atlas Packer Integration

For Marrow's own atlas packing:

1. Integrate `stb_rect_pack.h` (drop-in single header, public domain)
2. Add a trim pass (scan for transparent borders per sprite)
3. Add 1-2px padding between regions
4. Add 1px bleed (duplicate edge pixels)
5. Output Marrow's own atlas format (or reuse Spine's text format for compatibility)

**Estimated effort**: 2-3 days for basic packer, 1 week with trim + bleed + rotation.

### 6.4 Priority Order

1. Atlas packer (needed for Marrow's own assets regardless)
2. Spine JSON parser (skeleton + setup pose)
3. Spine atlas parser (texture region resolution)
4. Animation playback (bone timelines first)
5. Mesh attachments + deform
6. Constraints (IK, transform, path)
7. Binary .skel support (optional, performance optimization)

---

## Sources

1. Spine JSON Format Specification -- http://esotericsoftware.com/spine-json-format
2. Spine Binary Format Specification -- http://esotericsoftware.com/spine-binary-format
3. Spine Atlas Format Specification -- http://esotericsoftware.com/spine-atlas-format
4. Spine Editor License Agreement -- http://en.esotericsoftware.com/spine-editor-license
5. Spine Runtimes GitHub (spine-csharp reference) -- https://github.com/EsotericSoftware/spine-runtimes
6. Castle Game Engine Spine Integration -- https://castle-engine.io/spine
7. Jukka Jylanki, "A Thousand Ways to Pack the Bin" -- MaxRects reference paper
8. stb_rect_pack.h -- https://github.com/nothings/stb
9. smol-atlas benchmarks -- https://github.com/aras-p/smol-atlas
10. David Colson, "Exploring Rectangle Packing Algorithms" -- https://www.david-colson.com/2020/03/10/exploring-rect-packing.html
11. andrw.coffee, "Writing a Sprite Packer" (Guillotine) -- https://andrw.coffee/devlog/sprite_packer/
12. TexturePacker Algorithm Documentation -- https://www.codeandweb.com/texturepacker/documentation/texture-settings
