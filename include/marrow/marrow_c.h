#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MARROW_C_ABI_VERSION 1u

typedef struct MarrowSkeletonData MarrowSkeletonData;
typedef struct MarrowAtlasData MarrowAtlasData;
typedef struct MarrowSkeleton MarrowSkeleton;
typedef struct MarrowAnimState MarrowAnimState;
typedef struct MarrowProject MarrowProject;
typedef struct MarrowSkeletonBounds MarrowSkeletonBounds;
typedef struct MarrowRenderCommandList MarrowRenderCommandList;

typedef enum MarrowStatusCode {
    MARROW_STATUS_OK = 0,
    MARROW_STATUS_INVALID_ARGUMENT = 1,
    MARROW_STATUS_ABI_MISMATCH = 2,
    MARROW_STATUS_IO_ERROR = 3,
    MARROW_STATUS_PARSE_ERROR = 4,
    MARROW_STATUS_NOT_FOUND = 5,
    MARROW_STATUS_OUT_OF_RANGE = 6,
    MARROW_STATUS_STATE_ERROR = 7,
    MARROW_STATUS_BUFFER_TOO_SMALL = 8,
    MARROW_STATUS_INTERNAL_ERROR = 9,
} MarrowStatusCode;

typedef enum MarrowAnimationEventType {
    MARROW_ANIMATION_EVENT_START = 0,
    MARROW_ANIMATION_EVENT_INTERRUPT = 1,
    MARROW_ANIMATION_EVENT_END = 2,
    MARROW_ANIMATION_EVENT_DISPOSE = 3,
    MARROW_ANIMATION_EVENT_COMPLETE = 4,
    MARROW_ANIMATION_EVENT_EVENT = 5,
} MarrowAnimationEventType;

typedef enum MarrowPhysicsMode {
    MARROW_PHYSICS_MODE_NONE = 0,
    MARROW_PHYSICS_MODE_RESET = 1,
    MARROW_PHYSICS_MODE_UPDATE = 2,
    MARROW_PHYSICS_MODE_POSE = 3,
} MarrowPhysicsMode;

typedef enum MarrowBlendMode {
    MARROW_BLEND_MODE_NORMAL = 0,
    MARROW_BLEND_MODE_ADDITIVE = 1,
    MARROW_BLEND_MODE_MULTIPLY = 2,
    MARROW_BLEND_MODE_SCREEN = 3,
} MarrowBlendMode;

typedef enum MarrowColorShaderVariant {
    MARROW_COLOR_SHADER_SINGLE_COLOR = 0,
    MARROW_COLOR_SHADER_TWO_COLOR_TINT = 1,
} MarrowColorShaderVariant;

typedef enum MarrowRenderEventKind {
    MARROW_RENDER_EVENT_CLIP_START = 0,
    MARROW_RENDER_EVENT_DRAW = 1,
    MARROW_RENDER_EVENT_CLIP_END = 2,
} MarrowRenderEventKind;

typedef struct MarrowStringView {
    const char* data;
    size_t size;
} MarrowStringView;

typedef void* (*MarrowAllocateFn)(size_t size, size_t alignment, void* user_data);
typedef void (*MarrowDeallocateFn)(void* ptr, size_t size, void* user_data);

typedef struct MarrowAllocator {
    void* user_data;
    MarrowAllocateFn allocate;
    MarrowDeallocateFn deallocate;
} MarrowAllocator;

typedef struct MarrowSkeletonInfo {
    MarrowStringView name;
    double width;
    double height;
    size_t bone_count;
    size_t slot_count;
    size_t animation_count;
    size_t skin_count;
    double default_mix_duration;
} MarrowSkeletonInfo;

typedef struct MarrowAtlasInfo {
    MarrowStringView name;
    MarrowStringView image;
    double width;
    double height;
    MarrowStringView filter_min;
    MarrowStringView filter_mag;
    MarrowStringView wrap_x;
    MarrowStringView wrap_y;
    bool premultiplied_alpha;
    size_t region_count;
} MarrowAtlasInfo;

typedef struct MarrowAtlasRegionInfo {
    MarrowStringView name;
    double x;
    double y;
    double width;
    double height;
    double origin_x;
    double origin_y;
    double rotate_degrees;
} MarrowAtlasRegionInfo;

typedef struct MarrowAabb {
    double min_x;
    double min_y;
    double max_x;
    double max_y;
} MarrowAabb;

typedef struct MarrowRootMotionDelta {
    double x;
    double y;
} MarrowRootMotionDelta;

typedef struct MarrowTrackState {
    size_t track_index;
    MarrowStringView animation_name;
    bool loop;
    bool is_empty;
    bool reverse;
    double alpha;
    double mix_duration;
    double mix_time;
    double track_time;
    double track_end;
} MarrowTrackState;

typedef struct MarrowAnimationEventData {
    size_t event_index;
    MarrowStringView name;
    double time;
    int int_value;
    double float_value;
    MarrowStringView string_value;
    bool has_audio_path;
    MarrowStringView audio_path;
    double volume;
    double balance;
} MarrowAnimationEventData;

typedef struct MarrowAnimationStateEvent {
    MarrowAnimationEventType type;
    size_t track_index;
    MarrowStringView animation_name;
    bool loop;
    bool is_empty;
    double track_time;
    double mix_time;
    double mix_duration;
    bool has_event_data;
    MarrowAnimationEventData event_data;
} MarrowAnimationStateEvent;

typedef void (*MarrowAnimationStateListener)(
    const MarrowAnimationStateEvent* state_event,
    void* user_data);

typedef struct MarrowRenderCommandVertex {
    float local_positions[4][2];
    float uv[2];
    float bone_indices[4];
    float bone_weights[4];
    float light_color[4];
    uint8_t dark_color[4];
} MarrowRenderCommandVertex;

typedef struct MarrowRenderCommandInfo {
    size_t vertex_count;
    size_t index_count;
    MarrowStringView texture_name;
    uint64_t texture_handle;
    MarrowBlendMode blend_mode;
    MarrowColorShaderVariant shader_variant;
    size_t source_draw_command_offset;
    size_t source_draw_command_count;
} MarrowRenderCommandInfo;

typedef struct MarrowClipCommandInfo {
    size_t vertex_count;
    size_t index_count;
    MarrowStringView attachment_name;
    size_t source_clip_attachment_index;
} MarrowClipCommandInfo;

typedef struct MarrowRenderEventRef {
    MarrowRenderEventKind kind;
    size_t index;
} MarrowRenderEventRef;

/**
 * @brief Verifies that the caller's header ABI version matches the library ABI.
 * @param expected_abi_version ABI version compiled into the caller.
 * @return Status code describing success or mismatch.
 */
MarrowStatusCode marrow_check_abi_version(uint32_t expected_abi_version);
/**
 * @brief Returns the ABI version exported by the linked Marrow library.
 * @param out_abi_version Receives the exported ABI version.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_get_library_abi_version(uint32_t* out_abi_version);
/**
 * @brief Returns the last thread-local Marrow error message.
 * @param out_message Receives a borrowed string view for the last error.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_get_last_error_message(MarrowStringView* out_message);
/**
 * @brief Returns a symbolic name for a Marrow status code.
 * @param status_code Status code to describe.
 * @param out_name Receives a borrowed string view for the status-code name.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_status_code_name(
    MarrowStatusCode status_code,
    MarrowStringView* out_name);
/* Process-wide allocator configuration. Call during startup before creating
   runtime objects on worker threads. Changing the allocator while live Marrow
   allocations exist fails. */
/**
 * @brief Sets the process-wide allocator callbacks used by the C API.
 * @param allocator Allocator callbacks to install, or `NULL` to restore defaults.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_set_allocator(const MarrowAllocator* allocator);

/* Loaded skeleton data is immutable and safe to share for concurrent read
   access across threads after this call succeeds. */
/**
 * @brief Loads immutable runtime skeleton data from a `.mskl` or `.mbin` file.
 * @param path Path to the runtime skeleton asset.
 * @param out_skeleton_data Receives the loaded skeleton-data handle.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_data_load(
    const char* path,
    MarrowSkeletonData** out_skeleton_data);
/**
 * @brief Destroys a skeleton-data handle returned by `marrow_skeleton_data_load()`.
 * @param skeleton_data Skeleton-data handle to destroy. `NULL` is allowed.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_data_destroy(MarrowSkeletonData* skeleton_data);
/**
 * @brief Returns summary information about loaded skeleton data.
 * @param skeleton_data Skeleton-data handle to inspect.
 * @param out_info Receives skeleton metadata and counts.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_data_get_info(
    const MarrowSkeletonData* skeleton_data,
    MarrowSkeletonInfo* out_info);
/**
 * @brief Returns one authored bone name by index.
 * @param skeleton_data Skeleton-data handle to inspect.
 * @param bone_index Bone index to query.
 * @param out_name Receives the borrowed bone name.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_data_get_bone_name(
    const MarrowSkeletonData* skeleton_data,
    size_t bone_index,
    MarrowStringView* out_name);
/**
 * @brief Returns one authored slot name by index.
 * @param skeleton_data Skeleton-data handle to inspect.
 * @param slot_index Slot index to query.
 * @param out_name Receives the borrowed slot name.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_data_get_slot_name(
    const MarrowSkeletonData* skeleton_data,
    size_t slot_index,
    MarrowStringView* out_name);
/**
 * @brief Returns one authored animation name by index.
 * @param skeleton_data Skeleton-data handle to inspect.
 * @param animation_index Animation index to query.
 * @param out_name Receives the borrowed animation name.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_data_get_animation_name(
    const MarrowSkeletonData* skeleton_data,
    size_t animation_index,
    MarrowStringView* out_name);
/**
 * @brief Returns one authored skin name by index.
 * @param skeleton_data Skeleton-data handle to inspect.
 * @param skin_index Skin index to query.
 * @param out_name Receives the borrowed skin name.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_data_get_skin_name(
    const MarrowSkeletonData* skeleton_data,
    size_t skin_index,
    MarrowStringView* out_name);

/**
 * @brief Loads immutable atlas metadata from a `.matl` file.
 * @param path Path to the atlas metadata asset.
 * @param out_atlas_data Receives the loaded atlas-data handle.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_atlas_data_load(
    const char* path,
    MarrowAtlasData** out_atlas_data);
/**
 * @brief Destroys an atlas-data handle returned by `marrow_atlas_data_load()`.
 * @param atlas_data Atlas-data handle to destroy. `NULL` is allowed.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_atlas_data_destroy(MarrowAtlasData* atlas_data);
/**
 * @brief Returns summary information about loaded atlas metadata.
 * @param atlas_data Atlas-data handle to inspect.
 * @param out_info Receives atlas metadata and counts.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_atlas_data_get_info(
    const MarrowAtlasData* atlas_data,
    MarrowAtlasInfo* out_info);
/**
 * @brief Returns one atlas region by index.
 * @param atlas_data Atlas-data handle to inspect.
 * @param region_index Region index to query.
 * @param out_region_info Receives region metadata.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_atlas_data_get_region_info(
    const MarrowAtlasData* atlas_data,
    size_t region_index,
    MarrowAtlasRegionInfo* out_region_info);

/* Each skeleton instance is mutable and not internally synchronized. Use one
   skeleton from one thread at a time. Distinct skeletons created from the same
   immutable MarrowSkeletonData may update concurrently. */
/**
 * @brief Creates a mutable skeleton instance from shared immutable skeleton data.
 * @param skeleton_data Loaded skeleton-data handle to instantiate.
 * @param out_skeleton Receives the created skeleton handle.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_create(
    const MarrowSkeletonData* skeleton_data,
    MarrowSkeleton** out_skeleton);
/**
 * @brief Destroys a skeleton instance handle.
 * @param skeleton Skeleton handle to destroy. `NULL` is allowed.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_destroy(MarrowSkeleton* skeleton);
/**
 * @brief Sets the global x/y scale applied to a skeleton instance.
 * @param skeleton Skeleton handle to update.
 * @param scale_x Scale applied on the x axis.
 * @param scale_y Scale applied on the y axis.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_set_scale(
    MarrowSkeleton* skeleton,
    double scale_x,
    double scale_y);
/**
 * @brief Restores bones, slots, and draw order to setup pose.
 * @param skeleton Skeleton handle to reset.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_set_to_setup_pose(MarrowSkeleton* skeleton);
/**
 * @brief Resets accumulated physics state for a skeleton instance.
 * @param skeleton Skeleton handle to reset.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_reset_physics(MarrowSkeleton* skeleton);
/**
 * @brief Activates one skin on a skeleton instance.
 * @param skeleton Skeleton handle to update.
 * @param skin_name Skin name to activate.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_set_skin(
    MarrowSkeleton* skeleton,
    const char* skin_name);
/**
 * @brief Activates a composed set of skins on a skeleton instance.
 * @param skeleton Skeleton handle to update.
 * @param skin_names Array of skin-name strings.
 * @param skin_name_count Number of entries in `skin_names`.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_set_skin_composition(
    MarrowSkeleton* skeleton,
    const char* const* skin_names,
    size_t skin_name_count);
/**
 * @brief Sets global playback time for sequence attachments.
 * @param skeleton Skeleton handle to update.
 * @param time_seconds Sequence playback time in seconds.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_set_attachment_playback_time(
    MarrowSkeleton* skeleton,
    double time_seconds);
/**
 * @brief Advances sequence-attachment playback time.
 * @param skeleton Skeleton handle to update.
 * @param delta_seconds Time step in seconds.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_advance_attachment_playback(
    MarrowSkeleton* skeleton,
    double delta_seconds);
/**
 * @brief Recomputes world transforms for the current skeleton pose.
 * @param skeleton Skeleton handle to update.
 * @param physics_mode Physics evaluation mode to apply during the solve.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_update_world_transforms(
    MarrowSkeleton* skeleton,
    MarrowPhysicsMode physics_mode);
/**
 * @brief Returns the currently active attachment name for a slot.
 * @param skeleton Skeleton handle to inspect.
 * @param slot_index Slot index to query.
 * @param out_attachment_name Receives the borrowed attachment name.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_get_current_attachment_name(
    const MarrowSkeleton* skeleton,
    size_t slot_index,
    MarrowStringView* out_attachment_name);
/**
 * @brief Returns the currently active atlas region name for a slot.
 * @param skeleton Skeleton handle to inspect.
 * @param slot_index Slot index to query.
 * @param out_region_name Receives the borrowed region name.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_get_current_region_name(
    const MarrowSkeleton* skeleton,
    size_t slot_index,
    MarrowStringView* out_region_name);

/* Each animation state instance is mutable and not internally synchronized.
   Use one animation state from one thread at a time. Distinct animation
   states may update concurrently when they drive distinct skeletons and only
   share immutable MarrowSkeletonData. */
/**
 * @brief Creates animation playback state from shared immutable skeleton data.
 * @param skeleton_data Loaded skeleton-data handle used for animation lookup.
 * @param out_anim_state Receives the created animation-state handle.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_anim_state_create(
    const MarrowSkeletonData* skeleton_data,
    MarrowAnimState** out_anim_state);
/**
 * @brief Destroys an animation-state handle.
 * @param anim_state Animation-state handle to destroy. `NULL` is allowed.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_anim_state_destroy(MarrowAnimState* anim_state);
/**
 * @brief Installs a listener for animation-state callbacks.
 * @param anim_state Animation-state handle to update.
 * @param listener Listener function to invoke.
 * @param user_data Opaque pointer passed back to the listener.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_anim_state_set_listener(
    MarrowAnimState* anim_state,
    MarrowAnimationStateListener listener,
    void* user_data);
/**
 * @brief Replaces the current entry on a track with a named animation.
 * @param anim_state Animation-state handle to update.
 * @param track_index Track index to update.
 * @param animation_name Animation name to play.
 * @param loop Whether the animation should loop.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_anim_state_set_animation(
    MarrowAnimState* anim_state,
    size_t track_index,
    const char* animation_name,
    bool loop);
/**
 * @brief Replaces the current entry on a track with a named animation and explicit mix duration.
 * @param anim_state Animation-state handle to update.
 * @param track_index Track index to update.
 * @param animation_name Animation name to play.
 * @param loop Whether the animation should loop.
 * @param mix_duration Explicit crossfade duration in seconds.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_anim_state_set_animation_with_mix(
    MarrowAnimState* anim_state,
    size_t track_index,
    const char* animation_name,
    bool loop,
    double mix_duration);
/**
 * @brief Queues a named animation after the current track chain.
 * @param anim_state Animation-state handle to update.
 * @param track_index Track index to update.
 * @param animation_name Animation name to queue.
 * @param loop Whether the animation should loop.
 * @param delay_seconds Delay before the queued entry starts.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_anim_state_add_animation(
    MarrowAnimState* anim_state,
    size_t track_index,
    const char* animation_name,
    bool loop,
    double delay_seconds);
/**
 * @brief Queues a named animation with an explicit mix duration.
 * @param anim_state Animation-state handle to update.
 * @param track_index Track index to update.
 * @param animation_name Animation name to queue.
 * @param loop Whether the animation should loop.
 * @param delay_seconds Delay before the queued entry starts.
 * @param mix_duration Explicit crossfade duration in seconds.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_anim_state_add_animation_with_mix(
    MarrowAnimState* anim_state,
    size_t track_index,
    const char* animation_name,
    bool loop,
    double delay_seconds,
    double mix_duration);
/**
 * @brief Replaces the current entry on a track with an empty fade-out entry.
 * @param anim_state Animation-state handle to update.
 * @param track_index Track index to update.
 * @param mix_duration Empty fade-out duration in seconds.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_anim_state_set_empty_animation(
    MarrowAnimState* anim_state,
    size_t track_index,
    double mix_duration);
/**
 * @brief Queues an empty fade-out entry after the current track chain.
 * @param anim_state Animation-state handle to update.
 * @param track_index Track index to update.
 * @param mix_duration Empty fade-out duration in seconds.
 * @param delay_seconds Delay before the queued entry starts.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_anim_state_add_empty_animation(
    MarrowAnimState* anim_state,
    size_t track_index,
    double mix_duration,
    double delay_seconds);
/**
 * @brief Clears a single animation track.
 * @param anim_state Animation-state handle to update.
 * @param track_index Track index to clear.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_anim_state_clear_track(
    MarrowAnimState* anim_state,
    size_t track_index);
/**
 * @brief Clears every animation track.
 * @param anim_state Animation-state handle to update.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_anim_state_clear_tracks(MarrowAnimState* anim_state);
/**
 * @brief Advances animation playback times without applying to a skeleton.
 * @param anim_state Animation-state handle to update.
 * @param delta_seconds Time step in seconds.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_anim_state_update(
    MarrowAnimState* anim_state,
    double delta_seconds);
/**
 * @brief Applies current animation-state playback to a skeleton and updates world transforms.
 * @param anim_state Animation-state handle to apply.
 * @param skeleton Skeleton handle created from the same skeleton data.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_anim_state_apply(
    MarrowAnimState* anim_state,
    MarrowSkeleton* skeleton);
/**
 * @brief Returns summary state for the current entry on one track.
 * @param anim_state Animation-state handle to inspect.
 * @param track_index Track index to query.
 * @param out_track_state Receives track playback summary data.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_anim_state_get_current_track(
    const MarrowAnimState* anim_state,
    size_t track_index,
    MarrowTrackState* out_track_state);
/**
 * @brief Extracts root-motion delta for one track and root bone.
 * @param anim_state Animation-state handle to inspect.
 * @param track_index Track index to sample.
 * @param root_bone_index Root bone index used for extraction.
 * @param out_delta Receives the extracted root-motion delta.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_anim_state_extract_root_motion(
    const MarrowAnimState* anim_state,
    size_t track_index,
    size_t root_bone_index,
    MarrowRootMotionDelta* out_delta);

/**
 * @brief Creates a reusable bounds helper for bounding-box attachment queries.
 * @param out_bounds Receives the created bounds handle.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_bounds_create(
    MarrowSkeletonBounds** out_bounds);
/**
 * @brief Destroys a bounds helper handle.
 * @param bounds Bounds handle to destroy. `NULL` is allowed.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_bounds_destroy(MarrowSkeletonBounds* bounds);
/**
 * @brief Rebuilds bounds data from the skeleton's current attachment state.
 * @param bounds Bounds handle to update.
 * @param skeleton Skeleton handle to inspect.
 * @param compute_aabb Whether to also recompute the aggregate AABB.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_bounds_update(
    MarrowSkeletonBounds* bounds,
    const MarrowSkeleton* skeleton,
    bool compute_aabb);
/**
 * @brief Reports whether the current bounds data has a valid aggregate AABB.
 * @param bounds Bounds handle to inspect.
 * @param out_has_aabb Receives the AABB validity flag.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_bounds_has_aabb(
    const MarrowSkeletonBounds* bounds,
    bool* out_has_aabb);
/**
 * @brief Returns the current aggregate AABB.
 * @param bounds Bounds handle to inspect.
 * @param out_aabb Receives the aggregate AABB.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_bounds_get_aabb(
    const MarrowSkeletonBounds* bounds,
    MarrowAabb* out_aabb);
/**
 * @brief Tests whether a point lies inside any active bounding box.
 * @param bounds Bounds handle to inspect.
 * @param x Point x coordinate.
 * @param y Point y coordinate.
 * @param out_contains Receives the point-in-bounds result.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_bounds_contains_point(
    MarrowSkeletonBounds* bounds,
    double x,
    double y,
    bool* out_contains);
/**
 * @brief Tests whether a segment intersects any active bounding box.
 * @param bounds Bounds handle to inspect.
 * @param x1 First segment endpoint x coordinate.
 * @param y1 First segment endpoint y coordinate.
 * @param x2 Second segment endpoint x coordinate.
 * @param y2 Second segment endpoint y coordinate.
 * @param out_intersects Receives the segment-intersection result.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_skeleton_bounds_intersects_segment(
    MarrowSkeletonBounds* bounds,
    double x1,
    double y1,
    double x2,
    double y2,
    bool* out_intersects);

/**
 * @brief Builds a packed render command list from a skeleton pose and atlas metadata.
 * @param skeleton Skeleton handle containing the current pose.
 * @param atlas_data Atlas-data handle used for region lookup.
 * @param projection Column-major 4x4 projection matrix.
 * @param out_command_list Receives the created render-command-list handle.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_render_build_command_list(
    const MarrowSkeleton* skeleton,
    const MarrowAtlasData* atlas_data,
    const float projection[16],
    MarrowRenderCommandList** out_command_list);
/**
 * @brief Destroys a render-command-list handle.
 * @param command_list Render-command-list handle to destroy. `NULL` is allowed.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_render_command_list_destroy(MarrowRenderCommandList* command_list);
/**
 * @brief Returns the number of draw commands in a render-command list.
 * @param command_list Render-command-list handle to inspect.
 * @param out_command_count Receives the draw-command count.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_render_command_list_get_command_count(
    const MarrowRenderCommandList* command_list,
    size_t* out_command_count);
/**
 * @brief Returns the number of clip commands in a render-command list.
 * @param command_list Render-command-list handle to inspect.
 * @param out_clip_command_count Receives the clip-command count.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_render_command_list_get_clip_command_count(
    const MarrowRenderCommandList* command_list,
    size_t* out_clip_command_count);
/**
 * @brief Returns the number of ordered render events in a render-command list.
 * @param command_list Render-command-list handle to inspect.
 * @param out_event_count Receives the event count.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_render_command_list_get_event_count(
    const MarrowRenderCommandList* command_list,
    size_t* out_event_count);
/**
 * @brief Reports whether the command list targets a premultiplied-alpha atlas.
 * @param command_list Render-command-list handle to inspect.
 * @param out_premultiplied_alpha Receives the PMA flag.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_render_command_list_get_premultiplied_alpha(
    const MarrowRenderCommandList* command_list,
    bool* out_premultiplied_alpha);
/**
 * @brief Copies the projection matrix stored in a render-command list.
 * @param command_list Render-command-list handle to inspect.
 * @param out_projection Receives the 4x4 projection matrix.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_render_command_list_get_projection(
    const MarrowRenderCommandList* command_list,
    float out_projection[16]);
/**
 * @brief Copies the packed bone palette from a render-command list.
 * @param command_list Render-command-list handle to inspect.
 * @param out_bone_palette Destination float buffer.
 * @param bone_palette_capacity Capacity of `out_bone_palette` in floats.
 * @param out_written Receives the number of floats copied.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_render_command_list_copy_bone_palette(
    const MarrowRenderCommandList* command_list,
    float* out_bone_palette,
    size_t bone_palette_capacity,
    size_t* out_written);
/**
 * @brief Returns summary information for one draw command.
 * @param command_list Render-command-list handle to inspect.
 * @param command_index Draw-command index to query.
 * @param out_command_info Receives draw-command metadata.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_render_command_list_get_command_info(
    const MarrowRenderCommandList* command_list,
    size_t command_index,
    MarrowRenderCommandInfo* out_command_info);
/**
 * @brief Copies packed vertices for one draw command.
 * @param command_list Render-command-list handle to inspect.
 * @param command_index Draw-command index to query.
 * @param out_vertices Destination vertex buffer.
 * @param vertex_capacity Capacity of `out_vertices` in vertices.
 * @param out_written Receives the number of vertices copied.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_render_command_list_copy_command_vertices(
    const MarrowRenderCommandList* command_list,
    size_t command_index,
    MarrowRenderCommandVertex* out_vertices,
    size_t vertex_capacity,
    size_t* out_written);
/**
 * @brief Copies packed indices for one draw command.
 * @param command_list Render-command-list handle to inspect.
 * @param command_index Draw-command index to query.
 * @param out_indices Destination index buffer.
 * @param index_capacity Capacity of `out_indices` in indices.
 * @param out_written Receives the number of indices copied.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_render_command_list_copy_command_indices(
    const MarrowRenderCommandList* command_list,
    size_t command_index,
    uint32_t* out_indices,
    size_t index_capacity,
    size_t* out_written);
/**
 * @brief Returns summary information for one clip command.
 * @param command_list Render-command-list handle to inspect.
 * @param clip_command_index Clip-command index to query.
 * @param out_clip_command_info Receives clip-command metadata.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_render_command_list_get_clip_command_info(
    const MarrowRenderCommandList* command_list,
    size_t clip_command_index,
    MarrowClipCommandInfo* out_clip_command_info);
/**
 * @brief Copies packed vertices for one clip command.
 * @param command_list Render-command-list handle to inspect.
 * @param clip_command_index Clip-command index to query.
 * @param out_vertices Destination vertex buffer.
 * @param vertex_capacity Capacity of `out_vertices` in vertices.
 * @param out_written Receives the number of vertices copied.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_render_command_list_copy_clip_vertices(
    const MarrowRenderCommandList* command_list,
    size_t clip_command_index,
    MarrowRenderCommandVertex* out_vertices,
    size_t vertex_capacity,
    size_t* out_written);
/**
 * @brief Copies packed indices for one clip command.
 * @param command_list Render-command-list handle to inspect.
 * @param clip_command_index Clip-command index to query.
 * @param out_indices Destination index buffer.
 * @param index_capacity Capacity of `out_indices` in indices.
 * @param out_written Receives the number of indices copied.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_render_command_list_copy_clip_indices(
    const MarrowRenderCommandList* command_list,
    size_t clip_command_index,
    uint32_t* out_indices,
    size_t index_capacity,
    size_t* out_written);
/**
 * @brief Returns one ordered render event reference.
 * @param command_list Render-command-list handle to inspect.
 * @param event_index Event index to query.
 * @param out_event_ref Receives the ordered event reference.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_render_command_list_get_event_ref(
    const MarrowRenderCommandList* command_list,
    size_t event_index,
    MarrowRenderEventRef* out_event_ref);

/* Editor project handles are used for headless manipulation and AI agent
   dispatch. They maintain an internal undo stack and project state. */
/**
 * @brief Loads an editor project from a `.marrow` file.
 * @param path Path to the editor project file.
 * @param out_project Receives the loaded project handle.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_editor_project_load(
    const char* path,
    MarrowProject** out_project);
/**
 * @brief Destroys an editor project handle.
 * @param project Project handle to destroy. `NULL` is allowed.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_editor_project_destroy(MarrowProject* project);
/**
 * @brief Dispatches a JSON command to mutate the editor project state.
 * @param project Project handle to update.
 * @param json_command JSON command string: { "op": "...", "args": { ... } }
 * @param out_result_json Receives a borrowed string view for the result JSON.
 * @return Status code describing success or failure.
 */
MarrowStatusCode marrow_editor_agent_dispatch(
    MarrowProject* project,
    const char* json_command,
    MarrowStringView* out_result_json);

#ifdef __cplusplus
}
#endif
