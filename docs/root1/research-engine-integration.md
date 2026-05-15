# Game Engine Integration Patterns for Skeletal Animation Runtimes

Research report for the Marrow project.
Date: 2026-03-27

---

## Table of Contents

1. [Spine Game Engine Integration Patterns](#1-spine-game-engine-integration-patterns)
2. [C API Wrapper for C++ Runtime (FFI)](#2-c-api-wrapper-for-c-runtime-ffi)
3. [Thread-Safe Animation Update Patterns](#3-thread-safe-animation-update-patterns)
4. [Custom Memory Allocator Integration](#4-custom-memory-allocator-integration)
5. [Hot-Reload and Live Preview](#5-hot-reload-and-live-preview)
6. [Marrow-Specific Recommendations Summary](#6-marrow-specific-recommendations-summary)

---

## 1. Spine Game Engine Integration Patterns

### Architecture Overview

Spine uses a **three-layer architecture** that cleanly separates responsibilities:

```
Layer 3:  Engine-specific runtime   (spine-unity, spine-ue, spine-godot)
Layer 2:  Generic runtime           (spine-cpp or spine-c)
Layer 1:  Data format               (.json / .skel / .atlas files)
```

**Layer 2 (generic runtime)** is the core. It is engine-agnostic and provides:
- Skeleton parsing and data storage (bones, slots, attachments, skins, constraints)
- Animation sampling, mixing, blending via AnimationState
- Constraint solving (IK, path, transform, physics)
- Mesh deformation (FFD / weighted vertices)
- Skin composition and attachment lookup
- Render command generation (vertex/index buffers, draw order)

**Layer 3 (engine-specific runtime)** is a thin wrapper that provides:
- Asset import pipeline (converting exported files into engine-native asset types)
- Texture loading callbacks (the generic runtime never loads textures itself)
- Rendering integration (feeding generated vertices into the engine's renderer)
- Scene graph integration (wrapping skeletons as engine nodes/components/actors)
- Editor tooling (inspector UI, preview panes, animation browsers)
- Engine lifecycle hooks (update ticking, initialization, destruction)

### Interface Boundary: What the Runtime Provides vs. What the Engine Provides

| Responsibility | Generic Runtime (spine-cpp) | Engine Wrapper |
|---|---|---|
| Parse skeleton/animation data | Yes | No (delegates to runtime) |
| Skeleton instance management | Yes (Skeleton, AnimationState) | Wraps in components/nodes |
| Animation playback logic | Yes (AnimationState.update/apply) | Calls update each frame |
| Constraint solving | Yes (IK, path, transform, physics) | No |
| Vertex generation | Yes (computes world vertices) | No |
| Texture loading | No (callback interface) | Yes (provides TextureLoader impl) |
| GPU upload / draw calls | No | Yes (engine renderer) |
| Asset import / serialization | No | Yes (engine asset pipeline) |
| Scene integration (transforms) | No | Yes (maps root bone to scene node) |
| Input / event routing | No | Yes (engine event system) |

### How Each Engine Integrates

**Unity (spine-unity):**
- `SkeletonAnimation` MonoBehaviour wraps a `Skeleton` + `AnimationState`
- `SkeletonDataAsset` is a ScriptableObject referencing .json/.skel + atlas
- Rendering: generates `Mesh` objects each frame, submits via `MeshRenderer`
- Skeleton data is shared across instances (loaded once as setup pose data)
- Inspector provides animation preview, skin browser, slot attachment viewer
- Auto-reload: scene components reload when `SkeletonDataAsset` is modified

**Unreal Engine (spine-ue):**
- `USpineSkeletonAnimationComponent` (UActorComponent) wraps spine-cpp
- Custom `UAsset` types for skeleton and atlas data
- Rendering: generates `ProceduralMeshComponent` geometry
- Full Blueprint exposure of the spine-cpp API
- `SpineWidget` for UMG/UI integration
- `SkeletonFollowerComponent` / `SkeletonDriverComponent` for bone-following

**Godot (spine-godot):**
- Written as a GDExtension (or engine module) wrapping spine-cpp
- `SpineSprite` node extends Node2D
- Exposes spine-cpp API to GDScript and C#
- Integrates with Godot 2D lighting (normal maps per atlas page)
- Material / shader integration through Godot's rendering system

### Key Design Insight: Setup Pose vs Instance Data

Spine's architecture separates data into:
- **Setup pose data** (SkeletonData, AnimationData, Atlas): loaded once, shared read-only
- **Instance data** (Skeleton, AnimationState): per-entity, mutable

This separation is critical for memory efficiency and thread safety. Multiple game objects share the same SkeletonData but each has its own Skeleton instance.

### Recommendations for Marrow

Marrow already follows this pattern with `SkeletonData` / `AnimationData` as shared data and per-instance `Skeleton` / `AnimationState` objects. To formalize the engine integration boundary:

```cpp
// marrow/integration/engine_interface.hpp

namespace marrow::integration {

/// Engine provides this to handle texture loading.
struct TextureLoader {
    virtual ~TextureLoader() = default;
    virtual void* load(const char* path, int* width, int* height) = 0;
    virtual void unload(void* texture) = 0;
};

/// Engine provides this to receive render commands.
struct RenderCommandSink {
    virtual ~RenderCommandSink() = default;
    virtual void submit_mesh(
        const float* positions, const float* uvs, const uint8_t* colors,
        size_t vertex_count, const uint16_t* indices, size_t index_count,
        void* texture, BlendMode blend_mode) = 0;
};

/// Per-entity handle that engines wrap in their component/node type.
struct SkeletonInstance {
    Skeleton skeleton;
    AnimationState animation_state;

    void update(float delta_time);
    void generate_render_commands(RenderCommandSink& sink);
};

} // namespace marrow::integration
```

---

## 2. C API Wrapper for C++ Runtime (FFI)

### Industry Patterns

The established pattern for exposing C++ libraries to FFI consumers (Rust, C#, Python, Swift, Lua) follows these principles:

**Opaque Handle Pattern:**
Every C++ object is represented as an opaque pointer (handle) on the C side. The consumer never sees the struct layout.

```c
// marrow.h -- public C API
typedef struct MarrowSkeleton MarrowSkeleton;      // opaque
typedef struct MarrowAnimState MarrowAnimState;     // opaque
typedef struct MarrowSkeletonData MarrowSkeletonData; // opaque
```

**Lifecycle Functions:**
Constructor/destructor pairs follow `type_create` / `type_destroy` naming.

```c
MarrowSkeletonData* marrow_skeleton_data_load(const char* path, MarrowStatus* out_status);
void marrow_skeleton_data_destroy(MarrowSkeletonData* data);

MarrowSkeleton* marrow_skeleton_create(const MarrowSkeletonData* data);
void marrow_skeleton_destroy(MarrowSkeleton* skeleton);
```

**Method Functions:**
Member functions become free functions with the object as the first argument.

```c
void marrow_anim_state_set_animation(
    MarrowAnimState* state, int track, const char* name, bool loop);
void marrow_anim_state_update(MarrowAnimState* state, float delta);
void marrow_anim_state_apply(MarrowAnimState* state, MarrowSkeleton* skeleton);
```

**Error Handling:**
Return error codes or use an out-parameter status struct. Never let exceptions cross the FFI boundary.

```c
typedef enum {
    MARROW_OK = 0,
    MARROW_ERR_NULL_ARGUMENT,
    MARROW_ERR_PARSE_FAILED,
    MARROW_ERR_OUT_OF_MEMORY,
    MARROW_ERR_INVALID_STATE,
} MarrowStatusCode;

typedef struct {
    MarrowStatusCode code;
    const char* message;  // valid until next API call on same thread
} MarrowStatus;
```

### Best Practices from Spine and Community

1. **spine-c** is auto-generated from spine-cpp, providing a complete C99 API. It uses opaque struct pointers (`spSkeleton*`, `spAnimationState*`) and free functions (`spSkeleton_updateWorldTransform()`). Spine is deprecating spine-c in favor of direct C++ interop, but the pattern remains canonical for FFI.

2. **Exception safety**: All `extern "C"` functions must wrap their body in a try/catch. Exceptions must never propagate across the C boundary.

```cpp
extern "C" MarrowStatusCode marrow_skeleton_update(MarrowSkeleton* s) {
    if (!s) return MARROW_ERR_NULL_ARGUMENT;
    try {
        reinterpret_cast<marrow::runtime::Skeleton*>(s)->update_world_transform();
        return MARROW_OK;
    } catch (const std::exception& e) {
        // store message in thread-local buffer
        return MARROW_ERR_INVALID_STATE;
    }
}
```

3. **Rust FFI guidance** (from users.rust-lang.org): Use pointer+length for strings instead of NUL-terminated whenever possible. This allows zero-cost conversion from Rust `&str`. Make thread-safety and memory ownership static (compile-time), not dynamic.

4. **Type safety via distinct opaque types**: Each C++ class gets its own opaque C struct. Do not use `void*` for everything -- consumers lose type safety.

5. **ABI stability**: Accept `sizeof` of critical structs in create functions to detect ABI mismatches. Provide a `marrow_version()` function returning a version struct for runtime compatibility checks.

6. **Callback registration**: Use function pointer + void* user_data pairs (not closures).

```c
typedef void (*MarrowEventCallback)(
    MarrowAnimState* state, MarrowEventType type,
    const MarrowTrackEntry* entry, void* user_data);

void marrow_anim_state_set_listener(
    MarrowAnimState* state, MarrowEventCallback cb, void* user_data);
```

### Recommendations for Marrow

```c
// include/marrow/marrow_c.h
#ifndef MARROW_C_H
#define MARROW_C_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// --- Version & ABI ---
typedef struct { int major, minor, patch; } MarrowVersion;
MarrowVersion marrow_version(void);

// --- Error handling ---
typedef enum { MARROW_OK = 0, /* ... */ } MarrowStatusCode;

// --- Opaque handles ---
typedef struct MarrowSkeletonData  MarrowSkeletonData;
typedef struct MarrowSkeleton      MarrowSkeleton;
typedef struct MarrowAnimState     MarrowAnimState;
typedef struct MarrowAtlas          MarrowAtlas;

// --- Data loading (shared, ref-counted) ---
MarrowSkeletonData* marrow_skeleton_data_load_json(
    const char* json_path, MarrowStatusCode* out_status);
MarrowSkeletonData* marrow_skeleton_data_load_binary(
    const uint8_t* data, size_t len, MarrowStatusCode* out_status);
void marrow_skeleton_data_destroy(MarrowSkeletonData* d);

// --- Instance lifecycle ---
MarrowSkeleton* marrow_skeleton_create(const MarrowSkeletonData* data);
void marrow_skeleton_destroy(MarrowSkeleton* s);

MarrowAnimState* marrow_anim_state_create(const MarrowSkeletonData* data);
void marrow_anim_state_destroy(MarrowAnimState* s);

// --- Per-frame update ---
void marrow_anim_state_update(MarrowAnimState* s, float delta);
void marrow_anim_state_apply(MarrowAnimState* s, MarrowSkeleton* skel);
void marrow_skeleton_update_world_transform(MarrowSkeleton* s);

// --- Animation control ---
int marrow_anim_state_set_animation(
    MarrowAnimState* s, int track, const char* name, bool loop);
int marrow_anim_state_add_animation(
    MarrowAnimState* s, int track, const char* name, bool loop, float delay);

// --- Render data extraction ---
typedef struct {
    const float* positions;     // x,y pairs
    const float* uvs;           // u,v pairs
    const uint8_t* colors;      // r,g,b,a per vertex
    uint32_t vertex_count;
    const uint16_t* indices;
    uint32_t index_count;
    void* texture;
    int blend_mode;
} MarrowRenderMesh;

uint32_t marrow_skeleton_get_render_meshes(
    MarrowSkeleton* s, MarrowRenderMesh* out_meshes, uint32_t max_meshes);

// --- Query ---
size_t marrow_skeleton_get_bone_count(const MarrowSkeleton* s);
bool marrow_skeleton_get_bone_world_transform(
    const MarrowSkeleton* s, size_t bone_index,
    float* out_x, float* out_y, float* out_rotation);

#ifdef __cplusplus
}
#endif
#endif // MARROW_C_H
```

---

## 3. Thread-Safe Animation Update Patterns

### How Game Engines Parallelize Skeletal Animation

**The fundamental property**: Each skeleton instance is independent. Updating skeleton A never reads or writes skeleton B's data. This makes skeleton updates embarrassingly parallel at the instance level.

**Unity DOTS / ECS approach:**
- The new Unity animation system (Motion, shipping with Unity 6+) is built on ECS and the C# Job System.
- Animation sampling, blending, and state machine evaluation are Burst-compiled jobs scheduled with `IJobParallelFor` across all active skeleton entities.
- Each job operates on a single entity's animation data -- no cross-entity dependencies.
- AnimationCurve.Evaluate is NOT thread-safe in legacy Unity (a known gotcha). The new system avoids this entirely with its own curve evaluation.
- The Latios Kinemation framework (community DOTS animation) demonstrates: skeleton updates are `IJobEntity` iterating over all entities, fully burst-compiled and parallelized.

**Unreal Engine approach:**
- Animation evaluation runs in parallel worker threads via the Task Graph system.
- `USkeletalMeshComponent::TickComponent` dispatches animation evaluation to a worker thread.
- Bone transforms are double-buffered: write to a back buffer during parallel evaluation, swap on the game thread.
- The Animation Budget Allocator dynamically throttles skeleton ticks based on CPU budget -- skeletons farther from camera tick at lower rates.

**ozz-animation approach (most relevant to Marrow):**
- ozz is explicitly designed as thread-safe from the ground up.
- The key principle: **all runtime jobs are stateless processors**. They take input buffers and produce output buffers. No internal mutable state.
- `SamplingJob`, `BlendingJob`, `LocalToModelJob` are all pure functions operating on caller-owned buffers.
- The multi-threading sample uses OpenMP to distribute jobs across threads.
- Thread safety comes from data separation: each skeleton instance has its own local-space transform buffer, sampling cache, and model-space output buffer.

**GPU-based parallel approaches** (research, 2025):
- A recent paper (arXiv:2505.06703) proposes GPU-based skeleton tree updates using parallel prefix-sum reduction on the bone hierarchy.
- This converts the parent-chain matrix multiplication into an O(n) parallel operation.
- Practical for scenes with 1000+ skeletons or skeletons with 100+ joints.

### Thread Safety Guarantees a Runtime Must Provide

1. **Shared data is immutable after loading**: `SkeletonData`, `AnimationData`, `Atlas` must be read-only after construction. Multiple threads reading shared data concurrently must be safe without locks.

2. **Instance data is single-writer**: Each `Skeleton` and `AnimationState` instance is written by exactly one thread at a time. No internal global state.

3. **No hidden global mutable state**: No static variables, no global caches, no thread-unsafe singletons in the hot path.

4. **Stateless processing jobs**: The update pipeline (sample -> blend -> apply -> solve constraints -> compute world transforms) should be expressible as stateless jobs taking input/output buffers.

### Recommendations for Marrow

```cpp
// Marrow's update pipeline should be expressible as a pure function:
namespace marrow::runtime {

/// Stateless update: takes mutable instance, reads shared data, mutates instance only.
/// Safe to call on different instances from different threads concurrently.
void update_instance(
    Skeleton& skeleton,            // per-instance mutable state
    AnimationState& anim_state,    // per-instance mutable state
    float delta_time
);

// The above is equivalent to:
//   anim_state.update(delta_time);
//   anim_state.apply(skeleton);
//   skeleton.update_world_transform();
// All three steps touch only the instance's own data.

} // namespace marrow::runtime

// Engine-side parallel dispatch (pseudocode):
void tick_animation_system(float dt) {
    // All instances can be updated in parallel because they share
    // no mutable state. SkeletonData/AnimationData are read-only.
    parallel_for(instances.begin(), instances.end(), [dt](auto& inst) {
        marrow::runtime::update_instance(inst.skeleton, inst.anim_state, dt);
    });
}
```

**Audit checklist for Marrow's codebase:**
- Verify no `static` mutable variables in runtime source files
- Verify `SkeletonData` and `AnimationData` are not mutated after construction
- Verify `AnimationState::update()` and `Skeleton::update_world_transform()` only access `this` instance's data plus const-ref shared data
- If there is any shared mutable cache (e.g., timeline key lookup cache), make it per-instance or thread-local

---

## 4. Custom Memory Allocator Integration

### Industry Patterns

Game runtimes use three main approaches for custom allocator integration:

**Pattern A: Global Allocator Hook (ozz-animation pattern)**

Override a single global allocator that all library allocations go through.

```cpp
// ozz-animation's approach:
namespace ozz::memory {
class Allocator {
public:
    virtual void* Allocate(size_t size, size_t alignment) = 0;
    virtual void Deallocate(void* ptr) = 0;
};
Allocator* default_allocator();  // returns global default
Allocator* SetDefaulAllocator(Allocator* alloc);  // returns previous
}
```

Pros: Simple, catches all allocations. Engine just sets one hook at startup.
Cons: Global state, not composable, hard to use different allocators for different subsystems.

**Pattern B: Allocator-per-object (constructor injection)**

Pass an allocator to each object at construction time.

```cpp
SkeletonData* load_skeleton(const char* path, Allocator* alloc);
Skeleton* create_skeleton(const SkeletonData* data, Allocator* alloc);
AnimationState* create_anim_state(const SkeletonData* data, Allocator* alloc);
```

Pros: Composable, different allocators for different lifetimes (arena for per-frame, pool for instances).
Cons: More parameters to thread through, allocator must outlive the object.

**Pattern C: C++ PMR (polymorphic memory resource)**

Use `std::pmr::memory_resource` as the allocator interface, enabling standard container compatibility.

```cpp
#include <memory_resource>

namespace marrow {
    Skeleton create_skeleton(
        const SkeletonData* data,
        std::pmr::memory_resource* mr = std::pmr::get_default_resource());
}
```

Pros: Standard C++ interface, works with `std::pmr::vector` etc., composable.
Cons: Requires C++17, virtual dispatch overhead (usually negligible).

### What Game Engines Actually Do

- **Unreal Engine**: Custom global allocator (`FMalloc`) with per-thread caches, arenas for level loading, pool allocators for UObjects.
- **Unity**: Configurable allocator hierarchy: main thread, job system, temp per-frame. Uses dual-threaded allocator with lock-free fast paths.
- **Blender**: `BLI_memarena` (linear/arena) for mesh ops, `BLI_mempool` (pool) for fixed-size elements. Both show significant cache-hit improvements over raw malloc.
- **ozz-animation**: Global allocator hook. Standard containers remapped to use the custom allocator. Runtime libraries (the hot path) avoid standard containers entirely, using raw buffers.

### Allocation Patterns in Skeletal Animation

| Allocation Type | Lifetime | Best Allocator |
|---|---|---|
| SkeletonData, AnimationData, Atlas | Level / session | Arena (load-time, bulk free) |
| Skeleton instance, AnimationState | Entity lifetime | Pool (fixed size per type) |
| Per-frame scratch (blend buffers, vertex output) | Single frame | Frame/bump allocator (reset each frame) |
| Timeline key caches | Per-instance | Embedded in instance (no separate alloc) |

### Recommendations for Marrow

Adopt a hybrid of Pattern A + B. Provide a global default but allow per-object override:

```cpp
// include/marrow/runtime/allocator.hpp
#pragma once
#include <cstddef>

namespace marrow::runtime {

/// Minimal allocator interface. Engine overrides this.
struct Allocator {
    virtual ~Allocator() = default;
    virtual void* allocate(size_t size, size_t alignment) = 0;
    virtual void deallocate(void* ptr, size_t size) = 0;
};

/// Default allocator uses operator new/delete.
Allocator* get_default_allocator();

/// Set the global default. Returns previous allocator. NOT thread-safe;
/// call once at startup before loading any data.
Allocator* set_default_allocator(Allocator* alloc);

} // namespace marrow::runtime
```

For the C API, expose allocator hooks:

```c
typedef struct {
    void* (*allocate)(size_t size, size_t alignment, void* user_data);
    void  (*deallocate)(void* ptr, size_t size, void* user_data);
    void* user_data;
} MarrowAllocator;

/// Set before any other marrow calls. Not thread-safe.
void marrow_set_allocator(MarrowAllocator alloc);
```

Internal usage: all `new`/`delete` in Marrow runtime code should go through the allocator. Use a macro or helper:

```cpp
// Internal helper (not public API)
namespace marrow::runtime::detail {
template<typename T, typename... Args>
T* alloc_new(Allocator* a, Args&&... args) {
    void* mem = a->allocate(sizeof(T), alignof(T));
    return new (mem) T(std::forward<Args>(args)...);
}
template<typename T>
void alloc_delete(Allocator* a, T* ptr) {
    ptr->~T();
    a->deallocate(ptr, sizeof(T));
}
}
```

---

## 5. Hot-Reload and Live Preview

### How Engines Handle Animation Asset Hot-Reload

**Spine-Unity approach:**
- `SkeletonDataAsset` monitors its source files (.json/.skel, .atlas)
- "Auto-reload scene components" setting: when the SkeletonDataAsset is modified, all `SkeletonAnimation` components in the scene re-initialize
- "Reload SkeletonData after Play" setting: shared SkeletonData is re-parsed from disk after exiting play mode to discard runtime mutations
- The process: detect file change -> re-parse skeleton data -> invalidate old shared data -> re-initialize all instances pointing to that data
- Animation reference assets may need re-creation when skeleton changes (known friction point)

**Bevy engine (Rust):**
- `AssetServer` with filesystem watcher
- Assets loaded via handles (`Handle<T>`). When the file changes, the AssetServer reloads and publishes an `AssetEvent::Modified` event
- Game systems react to `AssetEvent` to re-initialize dependent state
- Opt-in via `watch_for_changes: true` in AssetPlugin config

**General hot-reload pattern in game engines:**

```
1. File Watcher        detects file modification (inotify/FSEvents/kqueue)
2. Asset Loader        re-parses the modified asset file
3. Data Invalidation   marks old shared data as stale
4. Instance Update     all instances referencing stale data are re-initialized
5. State Preservation  animation playback state (track times, queued animations)
                       is captured before and restored after re-init
```

### Challenges Specific to Skeletal Animation

1. **Skeleton structure changes**: If bones are added/removed/reordered, all instance state (bone poses, constraint caches) must be fully rebuilt. Partial patching is not feasible.

2. **Animation data changes**: If only animation keyframes change (no skeleton restructuring), instances can keep their AnimationState track configuration. Just swap the underlying AnimationData pointer.

3. **State preservation**: The ideal hot-reload preserves:
   - Which animations are playing on which tracks
   - Current track times and mix progress
   - Listener registrations
   - Skin selections

4. **Thread safety during reload**: The reload must be atomic from the perspective of the update loop. Either defer reload to a sync point, or use a double-buffer / swap strategy.

### Recommendations for Marrow

```cpp
// include/marrow/runtime/hot_reload.hpp
#pragma once

namespace marrow::runtime {

/// Snapshot of playback state that survives a data reload.
struct AnimationStateSnapshot {
    struct TrackSnapshot {
        size_t track_index;
        std::string animation_name;
        bool loop;
        double track_time;
        double mix_duration;
        double alpha;
    };
    std::vector<TrackSnapshot> tracks;
    std::string current_skin_name;
};

/// Capture current playback state before a reload.
AnimationStateSnapshot capture_state(const AnimationState& state, const Skeleton& skeleton);

/// Restore playback state after data has been reloaded.
/// Animations that no longer exist in the new data are silently skipped.
void restore_state(
    AnimationState& state,
    Skeleton& skeleton,
    const SkeletonData& new_data,
    const AnimationStateSnapshot& snapshot);

/// Reload skeleton data from file. Returns new shared data.
/// Old data remains valid until all references are released.
/// Caller is responsible for re-initializing instances.
std::shared_ptr<SkeletonData> reload_skeleton_data(
    const std::filesystem::path& path,
    const Atlas& atlas);

} // namespace marrow::runtime
```

For the editor shell (which already exists in Marrow), the reload flow:

```
File watcher detects .mskl/.matl change
  |
  v
Editor calls reload_skeleton_data()
  |
  v
For each active SkeletonInstance:
  1. snapshot = capture_state(instance.anim_state, instance.skeleton)
  2. instance.skeleton = Skeleton(new_data)
  3. instance.anim_state = AnimationState(new_data)
  4. restore_state(instance.anim_state, instance.skeleton, new_data, snapshot)
  |
  v
Next frame renders with new data, animation continues from same point
```

For the C API:

```c
/// Capture animation state before hot-reload.
MarrowAnimStateSnapshot* marrow_anim_state_capture(const MarrowAnimState* s);

/// Restore animation state after hot-reload with new data.
void marrow_anim_state_restore(
    MarrowAnimState* s, MarrowSkeleton* skel,
    const MarrowAnimStateSnapshot* snapshot);

void marrow_anim_state_snapshot_destroy(MarrowAnimStateSnapshot* snap);
```

---

## 6. Marrow-Specific Recommendations Summary

### Priority 1: Formalize the Integration Boundary

Marrow already has the right architecture (shared SkeletonData + per-instance Skeleton/AnimationState). Formalize it:

- Create `include/marrow/integration/` with `TextureLoader`, `RenderCommandSink`, and `SkeletonInstance` as documented above.
- Keep the runtime (`include/marrow/runtime/`) engine-agnostic. No rendering, no file I/O, no engine types.
- The renderer module (`include/marrow/renderer/`) is one concrete integration; others (Unity, Godot, etc.) would be separate.

### Priority 2: C API for FFI

- Create `include/marrow/marrow_c.h` with the opaque-handle C API.
- Implement in `src/c_api/` using `extern "C"` wrappers with exception guards.
- Every function returns `MarrowStatusCode` or takes an out-status parameter.
- Use distinct opaque types (not `void*`) for type safety.
- This enables Rust bindings (via bindgen), C# bindings (via P/Invoke), Python (via ctypes/cffi), and Lua (via LuaJIT FFI).

### Priority 3: Thread-Safety Audit

- Audit all runtime source files for static mutable state.
- Verify SkeletonData and AnimationData are const after construction.
- Document the thread-safety contract: "distinct instances may be updated concurrently; shared data is immutable after loading."
- Consider making the update pipeline expressible as a stateless job for engine job systems.

### Priority 4: Custom Allocator Support

- Add the `marrow::runtime::Allocator` interface with global default.
- Route internal allocations through it (replace direct `new`/`delete`).
- Expose via C API as `MarrowAllocator` callback struct.
- This is essential for console targets and memory-constrained platforms.

### Priority 5: Hot-Reload Infrastructure

- Implement `capture_state` / `restore_state` for the editor shell.
- The editor shell already loads `.marrow` project files; extend it to watch for file changes and trigger re-parse.
- Expose via C API for engine integrations that want hot-reload in their editor.

---

## Sources

- Spine Runtime Architecture: http://esotericsoftware.com/spine-runtime-architecture
- spine-c Runtime Documentation: http://esotericsoftware.com/spine-c
- spine-cpp Runtime Documentation: http://esotericsoftware.com/spine-cpp
- spine-godot Runtime Documentation: http://esotericsoftware.com/spine-godot
- spine-ue Runtime Documentation: http://esotericsoftware.com/spine-ue
- spine-unity Asset Management: http://esotericsoftware.com/spine-unity-assets
- spine-c deprecation discussion: github.com/esotericsoftware/spine-runtimes/issues/2893
- ozz-animation (C++ skeletal animation library): github.com/guillaumeblanc/ozz-animation
- ozz-animation Advanced (memory, threading, IO): guillaumeblanc.github.io/ozz-animation/documentation/advanced/
- C API wrapper patterns: stackoverflow.com/questions/2045774
- Rust FFI best practices: users.rust-lang.org/t/best-practices-to-design-a-c-api/133151
- Opaque pointer FFI in Rust: web.mit.edu/rust-lang_v1.25/arch/.../ffi.html
- Unity DOTS Animation Status (Q1 2025): discussions.unity.com/t/animation-status-update-q1-2025
- Unity Animation thread safety: discussions.unity.com/t/animation-curves-and-thread-safety/255653
- DMotion (DOTS animation framework): discussions.unity.com/t/0-4-0-dmotion
- GPU skeletal animation (2025): arxiv.org/html/2505.06703v1
- Arena allocators in games: bytesbeneath.com/p/the-arena-custom-memory-allocators
- Pool allocators in games: gamedeveloper.com/programming/designing-and-implementing-a-pool-allocator
- Bevy hot-reloading: bevy-cheatbook.github.io/assets/hot-reload.html
- Hot reload gameplay code (Odin): zylinski.se/posts/hot-reload-gameplay-code/
- Unreal Animation Budget Allocator: dev.epicgames.com/documentation/.../animation-budget-allocator
