#include "marrow/marrow_c.h"

#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static void require_status(MarrowStatusCode status, const char* step) {
    if (status == MARROW_STATUS_OK) {
        return;
    }

    MarrowStringView status_name = {0};
    MarrowStringView error_message = {0};
    (void) marrow_status_code_name(status, &status_name);
    (void) marrow_get_last_error_message(&error_message);
    const char* status_data = status_name.data != NULL ? status_name.data : "";
    const char* error_data = error_message.data != NULL ? error_message.data : "";
    fprintf(
        stderr,
        "%s failed: %.*s\n%.*s\n",
        step,
        (int) status_name.size,
        status_data,
        (int) error_message.size,
        error_data);
    exit(1);
}

static void require_condition(int condition, const char* message) {
    if (condition) {
        return;
    }

    fprintf(stderr, "%s\n", message);
    exit(1);
}

static void record_animation_event(
    const MarrowAnimationStateEvent* state_event,
    void* user_data) {
    size_t* event_count = (size_t*) user_data;
    if (state_event != NULL && state_event->type == MARROW_ANIMATION_EVENT_EVENT) {
        *event_count += 1U;
    }
}

typedef struct CountingAllocatorHeader {
    void* base;
} CountingAllocatorHeader;

typedef struct CountingAllocatorState {
    size_t allocation_count;
    size_t deallocation_count;
    size_t live_allocations;
} CountingAllocatorState;

static size_t normalize_alignment(size_t alignment) {
    size_t normalized = alignment;
    if (normalized == 0U) {
        normalized = sizeof(void*);
    }
    if (normalized < sizeof(CountingAllocatorHeader)) {
        normalized = sizeof(CountingAllocatorHeader);
    }
    if ((normalized & (normalized - 1U)) == 0U) {
        return normalized;
    }

    size_t rounded = 1U;
    while (rounded < normalized) {
        rounded <<= 1U;
    }
    return rounded;
}

static void* counting_allocate(size_t size, size_t alignment, void* user_data) {
    CountingAllocatorState* state = (CountingAllocatorState*) user_data;
    if (size == 0U) {
        size = 1U;
    }

    alignment = normalize_alignment(alignment);
    const size_t total_size = size + (alignment - 1U) + sizeof(CountingAllocatorHeader);
    void* base = malloc(total_size);
    if (base == NULL) {
        return NULL;
    }

    const uintptr_t begin =
        (uintptr_t) ((char*) base + sizeof(CountingAllocatorHeader));
    const uintptr_t aligned = (begin + (uintptr_t) alignment - 1U) &
        ~((uintptr_t) alignment - 1U);
    CountingAllocatorHeader* header =
        (CountingAllocatorHeader*) ((char*) (uintptr_t) aligned - sizeof(CountingAllocatorHeader));
    header->base = base;

    state->allocation_count += 1U;
    state->live_allocations += 1U;
    return (void*) (uintptr_t) aligned;
}

static void counting_deallocate(void* ptr, size_t size, void* user_data) {
    (void) size;
    if (ptr == NULL) {
        return;
    }

    CountingAllocatorState* state = (CountingAllocatorState*) user_data;
    CountingAllocatorHeader* header =
        (CountingAllocatorHeader*) ((char*) ptr - sizeof(CountingAllocatorHeader));
    free(header->base);
    state->deallocation_count += 1U;
    if (state->live_allocations > 0U) {
        state->live_allocations -= 1U;
    }
}

int main(void) {
    CountingAllocatorState allocator_state = {0U, 0U, 0U};
    const MarrowAllocator allocator = {
        &allocator_state,
        counting_allocate,
        counting_deallocate,
    };
    require_status(marrow_set_allocator(&allocator), "marrow_set_allocator");

    const MarrowStatusCode mismatch_status =
        marrow_check_abi_version(MARROW_C_ABI_VERSION + 1U);
    require_condition(
        mismatch_status == MARROW_STATUS_ABI_MISMATCH,
        "ABI mismatch detection did not reject an incorrect header version.");
    require_status(
        marrow_check_abi_version(MARROW_C_ABI_VERSION),
        "marrow_check_abi_version");

    MarrowSkeletonData* skeleton_data = NULL;
    MarrowAtlasData* atlas_data = NULL;
    MarrowSkeleton* skeleton = NULL;
    MarrowAnimState* anim_state = NULL;
    MarrowSkeletonBounds* bounds = NULL;
    MarrowRenderCommandList* command_list = NULL;

    require_status(
        marrow_skeleton_data_load("assets/fixtures/player_idle.mskl", &skeleton_data),
        "marrow_skeleton_data_load");
    require_status(
        marrow_atlas_data_load("assets/fixtures/player_idle.matl", &atlas_data),
        "marrow_atlas_data_load");

    MarrowSkeletonInfo skeleton_info;
    require_status(
        marrow_skeleton_data_get_info(skeleton_data, &skeleton_info),
        "marrow_skeleton_data_get_info");
    require_condition(
        skeleton_info.animation_count > 0U && skeleton_info.slot_count > 0U,
        "Loaded skeleton did not expose animation or slot metadata.");

    MarrowAtlasInfo atlas_info;
    require_status(
        marrow_atlas_data_get_info(atlas_data, &atlas_info),
        "marrow_atlas_data_get_info");
    require_condition(
        atlas_info.region_count > 0U,
        "Loaded atlas did not expose any regions.");

    require_status(
        marrow_skeleton_create(skeleton_data, &skeleton),
        "marrow_skeleton_create");
    require_status(
        marrow_anim_state_create(skeleton_data, &anim_state),
        "marrow_anim_state_create");
    require_status(
        marrow_skeleton_bounds_create(&bounds),
        "marrow_skeleton_bounds_create");

    size_t callback_event_count = 0U;
    require_status(
        marrow_anim_state_set_listener(
            anim_state,
            record_animation_event,
            &callback_event_count),
        "marrow_anim_state_set_listener");
    require_status(
        marrow_anim_state_set_animation(anim_state, 0U, "idle", true),
        "marrow_anim_state_set_animation");
    require_status(
        marrow_anim_state_update(anim_state, 0.35),
        "marrow_anim_state_update");
    require_status(
        marrow_anim_state_apply(anim_state, skeleton),
        "marrow_anim_state_apply");
    require_condition(
        callback_event_count > 0U,
        "Animation callback bridge did not emit the expected idle event.");

    MarrowTrackState track_state;
    require_status(
        marrow_anim_state_get_current_track(anim_state, 0U, &track_state),
        "marrow_anim_state_get_current_track");
    require_condition(
        track_state.animation_name.size == 4U,
        "Track state did not preserve the current animation name.");

    require_status(
        marrow_skeleton_update_world_transforms(skeleton, MARROW_PHYSICS_MODE_POSE),
        "marrow_skeleton_update_world_transforms");
    require_status(
        marrow_skeleton_bounds_update(bounds, skeleton, true),
        "marrow_skeleton_bounds_update");

    MarrowAabb bounds_aabb;
    require_status(
        marrow_skeleton_bounds_get_aabb(bounds, &bounds_aabb),
        "marrow_skeleton_bounds_get_aabb");
    require_condition(
        bounds_aabb.max_x > bounds_aabb.min_x &&
            bounds_aabb.max_y > bounds_aabb.min_y,
        "Bounds query did not produce a valid AABB.");

    {
        const float projection[16] = {
            1.0f, 0.0f, 0.0f, 0.0f,
            0.0f, 1.0f, 0.0f, 0.0f,
            0.0f, 0.0f, 1.0f, 0.0f,
            0.0f, 0.0f, 0.0f, 1.0f,
        };
        require_status(
            marrow_render_build_command_list(
                skeleton,
                atlas_data,
                projection,
                &command_list),
            "marrow_render_build_command_list");
    }

    size_t command_count = 0U;
    size_t event_count = 0U;
    require_status(
        marrow_render_command_list_get_command_count(command_list, &command_count),
        "marrow_render_command_list_get_command_count");
    require_status(
        marrow_render_command_list_get_event_count(command_list, &event_count),
        "marrow_render_command_list_get_event_count");
    require_condition(
        command_count > 0U && event_count > 0U,
        "Render command generation did not produce draw or event output.");

    MarrowRenderCommandInfo command_info;
    require_status(
        marrow_render_command_list_get_command_info(command_list, 0U, &command_info),
        "marrow_render_command_list_get_command_info");
    require_condition(
        command_info.vertex_count > 0U && command_info.index_count > 0U,
        "First render command did not expose any geometry.");

    MarrowRenderCommandVertex* vertices = (MarrowRenderCommandVertex*) calloc(
        command_info.vertex_count,
        sizeof(MarrowRenderCommandVertex));
    uint32_t* indices = (uint32_t*) calloc(command_info.index_count, sizeof(uint32_t));
    require_condition(
        vertices != NULL && indices != NULL,
        "Failed to allocate vertex/index buffers for the C smoke test.");

    {
        size_t vertices_written = 0U;
        size_t indices_written = 0U;
        require_status(
            marrow_render_command_list_copy_command_vertices(
                command_list,
                0U,
                vertices,
                command_info.vertex_count,
                &vertices_written),
            "marrow_render_command_list_copy_command_vertices");
        require_status(
            marrow_render_command_list_copy_command_indices(
                command_list,
                0U,
                indices,
                command_info.index_count,
                &indices_written),
            "marrow_render_command_list_copy_command_indices");
        require_condition(
            vertices_written == command_info.vertex_count &&
                indices_written == command_info.index_count,
            "Render command copy helpers returned inconsistent counts.");
    }

    {
        size_t bone_palette_float_count = 0U;
        require_condition(
            marrow_render_command_list_copy_bone_palette(
                command_list,
                NULL,
                0U,
                &bone_palette_float_count) == MARROW_STATUS_BUFFER_TOO_SMALL,
            "Bone palette query did not report the required buffer size.");
        require_condition(
            bone_palette_float_count > 0U,
            "Bone palette query did not report any packed uniforms.");
    }

    printf(
        "Loaded %.*s via marrow_c.h: commands=%zu, indices=%zu, callbackEvents=%zu\n",
        (int) skeleton_info.name.size,
        skeleton_info.name.data,
        command_count,
        command_info.index_count,
        callback_event_count);

    free(indices);
    free(vertices);
    require_status(
        marrow_render_command_list_destroy(command_list),
        "marrow_render_command_list_destroy");
    require_status(
        marrow_skeleton_bounds_destroy(bounds),
        "marrow_skeleton_bounds_destroy");
    require_status(
        marrow_anim_state_destroy(anim_state),
        "marrow_anim_state_destroy");
    require_status(
        marrow_skeleton_destroy(skeleton),
        "marrow_skeleton_destroy");
    require_status(
        marrow_atlas_data_destroy(atlas_data),
        "marrow_atlas_data_destroy");
    require_status(
        marrow_skeleton_data_destroy(skeleton_data),
        "marrow_skeleton_data_destroy");
    require_condition(
        allocator_state.allocation_count > 0U,
        "Custom C allocator did not observe any Marrow allocations.");
    require_condition(
        allocator_state.live_allocations == 0U,
        "Custom C allocator reported leaked Marrow allocations after teardown.");
    require_condition(
        allocator_state.deallocation_count == allocator_state.allocation_count,
        "Custom C allocator did not see balanced Marrow allocation and deallocation counts.");
    require_status(marrow_set_allocator(NULL), "marrow_set_allocator reset");
    return 0;
}
