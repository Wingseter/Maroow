#include "marrow/marrow_c.h"

#include <array>
#include <exception>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "marrow/allocator.hpp"
#include "marrow/renderer/module.hpp"
#include "marrow/runtime/animation_state.hpp"
#include "marrow/runtime/atlas.hpp"
#include "marrow/runtime/skeleton.hpp"

struct MarrowSkeletonData {
    explicit MarrowSkeletonData(
        std::shared_ptr<const marrow::runtime::SkeletonData> value_in)
        : value(std::move(value_in)) {}

    std::shared_ptr<const marrow::runtime::SkeletonData> value;
};

struct MarrowAtlasData {
    explicit MarrowAtlasData(
        std::shared_ptr<const marrow::runtime::AtlasData> value_in)
        : value(std::move(value_in)) {}

    std::shared_ptr<const marrow::runtime::AtlasData> value;
};

struct MarrowSkeleton {
    explicit MarrowSkeleton(marrow::UniquePtr<marrow::runtime::Skeleton> value_in)
        : value(std::move(value_in)) {}

    marrow::UniquePtr<marrow::runtime::Skeleton> value;
};

struct MarrowAnimState {
    explicit MarrowAnimState(marrow::UniquePtr<marrow::runtime::AnimationState> value_in)
        : value(std::move(value_in)) {}

    marrow::UniquePtr<marrow::runtime::AnimationState> value;
    MarrowAnimationStateListener listener{nullptr};
    void* listener_user_data{nullptr};
};

struct MarrowSkeletonBounds {
    explicit MarrowSkeletonBounds(marrow::UniquePtr<marrow::runtime::SkeletonBounds> value_in)
        : value(std::move(value_in)) {}

    marrow::UniquePtr<marrow::runtime::SkeletonBounds> value;
};

struct MarrowRenderCommandList {
    explicit MarrowRenderCommandList(marrow::renderer::RenderCommandList value_in)
        : value(std::move(value_in)) {}

    marrow::renderer::RenderCommandList value;
};

namespace {

thread_local std::string g_last_error_message;

class CallbackAllocator final : public marrow::Allocator {
public:
    void set_callbacks(MarrowAllocator callbacks) {
        callbacks_ = callbacks;
    }

    void* allocate(std::size_t size, std::size_t alignment) override {
        return callbacks_.allocate(size, alignment, callbacks_.user_data);
    }

    void deallocate(void* ptr, std::size_t size) noexcept override {
        callbacks_.deallocate(ptr, size, callbacks_.user_data);
    }

private:
    MarrowAllocator callbacks_{};
};

CallbackAllocator& callback_allocator() {
    static CallbackAllocator allocator;
    return allocator;
}

void clear_last_error_message() {
    g_last_error_message.clear();
}

MarrowStatusCode fail(MarrowStatusCode code, std::string message) {
    g_last_error_message = std::move(message);
    return code;
}

MarrowStatusCode fail_invalid_argument(std::string message) {
    return fail(MARROW_STATUS_INVALID_ARGUMENT, std::move(message));
}

MarrowStringView to_string_view(const std::string& value) {
    return {value.c_str(), value.size()};
}

MarrowStringView to_string_view(std::string_view value) {
    if (value.empty()) {
        return {nullptr, 0U};
    }

    return {value.data(), value.size()};
}

MarrowStatusCode map_json_error(const marrow::runtime::json::LoadError& error) {
    if (error.message == "failed to open file" ||
        error.message == "failed while reading file") {
        return MARROW_STATUS_IO_ERROR;
    }

    return MARROW_STATUS_PARSE_ERROR;
}

MarrowStatusCode map_exception(const std::exception& error) {
    return fail(MARROW_STATUS_INTERNAL_ERROR, error.what());
}

MarrowBlendMode to_c_blend_mode(marrow::runtime::BlendMode blend_mode) {
    switch (blend_mode) {
    case marrow::runtime::BlendMode::Normal:
        return MARROW_BLEND_MODE_NORMAL;
    case marrow::runtime::BlendMode::Additive:
        return MARROW_BLEND_MODE_ADDITIVE;
    case marrow::runtime::BlendMode::Multiply:
        return MARROW_BLEND_MODE_MULTIPLY;
    case marrow::runtime::BlendMode::Screen:
        return MARROW_BLEND_MODE_SCREEN;
    }

    return MARROW_BLEND_MODE_NORMAL;
}

MarrowColorShaderVariant to_c_shader_variant(
    marrow::renderer::ColorShaderVariant shader_variant) {
    switch (shader_variant) {
    case marrow::renderer::ColorShaderVariant::SingleColor:
        return MARROW_COLOR_SHADER_SINGLE_COLOR;
    case marrow::renderer::ColorShaderVariant::TwoColorTint:
        return MARROW_COLOR_SHADER_TWO_COLOR_TINT;
    }

    return MARROW_COLOR_SHADER_SINGLE_COLOR;
}

MarrowAnimationEventType to_c_animation_event_type(
    marrow::runtime::AnimationStateEventType event_type) {
    switch (event_type) {
    case marrow::runtime::AnimationStateEventType::Start:
        return MARROW_ANIMATION_EVENT_START;
    case marrow::runtime::AnimationStateEventType::Interrupt:
        return MARROW_ANIMATION_EVENT_INTERRUPT;
    case marrow::runtime::AnimationStateEventType::End:
        return MARROW_ANIMATION_EVENT_END;
    case marrow::runtime::AnimationStateEventType::Dispose:
        return MARROW_ANIMATION_EVENT_DISPOSE;
    case marrow::runtime::AnimationStateEventType::Complete:
        return MARROW_ANIMATION_EVENT_COMPLETE;
    case marrow::runtime::AnimationStateEventType::Event:
        return MARROW_ANIMATION_EVENT_EVENT;
    }

    return MARROW_ANIMATION_EVENT_EVENT;
}

MarrowRenderEventKind to_c_render_event_kind(
    marrow::renderer::RenderCommandEventKind kind) {
    switch (kind) {
    case marrow::renderer::RenderCommandEventKind::ClipStart:
        return MARROW_RENDER_EVENT_CLIP_START;
    case marrow::renderer::RenderCommandEventKind::Draw:
        return MARROW_RENDER_EVENT_DRAW;
    case marrow::renderer::RenderCommandEventKind::ClipEnd:
        return MARROW_RENDER_EVENT_CLIP_END;
    }

    return MARROW_RENDER_EVENT_DRAW;
}

bool to_runtime_physics_mode(
    MarrowPhysicsMode physics_mode,
    marrow::runtime::PhysicsMode* out_mode) {
    switch (physics_mode) {
    case MARROW_PHYSICS_MODE_NONE:
        *out_mode = marrow::runtime::PhysicsMode::None;
        return true;
    case MARROW_PHYSICS_MODE_RESET:
        *out_mode = marrow::runtime::PhysicsMode::Reset;
        return true;
    case MARROW_PHYSICS_MODE_UPDATE:
        *out_mode = marrow::runtime::PhysicsMode::Update;
        return true;
    case MARROW_PHYSICS_MODE_POSE:
        *out_mode = marrow::runtime::PhysicsMode::Pose;
        return true;
    }

    return false;
}

void fill_animation_event_data(
    const marrow::runtime::AnimationEvent& source,
    MarrowAnimationEventData* out_event_data) {
    out_event_data->event_index = source.event_index;
    out_event_data->name = to_string_view(source.name);
    out_event_data->time = source.time;
    out_event_data->int_value = source.int_value;
    out_event_data->float_value = source.float_value;
    out_event_data->string_value = to_string_view(source.string_value);
    out_event_data->has_audio_path = source.audio_path.has_value();
    out_event_data->audio_path = source.audio_path.has_value()
        ? to_string_view(*source.audio_path)
        : MarrowStringView{nullptr, 0U};
    out_event_data->volume = source.volume;
    out_event_data->balance = source.balance;
}

void fill_track_state(
    const marrow::runtime::TrackEntry& entry,
    MarrowTrackState* out_track_state) {
    out_track_state->track_index = entry.track_index;
    out_track_state->animation_name = to_string_view(entry.animation_name);
    out_track_state->loop = entry.loop;
    out_track_state->is_empty = entry.is_empty;
    out_track_state->reverse = entry.reverse;
    out_track_state->alpha = entry.alpha;
    out_track_state->mix_duration = entry.mix_duration;
    out_track_state->mix_time = entry.mix_time;
    out_track_state->track_time = entry.track_time;
    out_track_state->track_end = entry.track_end;
}

void fill_render_command_vertex(
    const marrow::renderer::RenderCommandVertex& source,
    MarrowRenderCommandVertex* out_vertex) {
    for (std::size_t influence = 0; influence < 4U; ++influence) {
        out_vertex->local_positions[influence][0] = source.local_positions[influence][0];
        out_vertex->local_positions[influence][1] = source.local_positions[influence][1];
        out_vertex->bone_indices[influence] = source.bone_indices[influence];
        out_vertex->bone_weights[influence] = source.bone_weights[influence];
    }

    out_vertex->uv[0] = source.uv[0];
    out_vertex->uv[1] = source.uv[1];
    for (std::size_t channel = 0; channel < 4U; ++channel) {
        out_vertex->light_color[channel] = source.light_color[channel];
        out_vertex->dark_color[channel] = source.dark_color[channel];
    }
}

template <typename Container>
MarrowStatusCode require_index(
    const Container& container,
    std::size_t index,
    const char* label) {
    if (index >= container.size()) {
        return fail(
            MARROW_STATUS_OUT_OF_RANGE,
            std::string(label) + " index " + std::to_string(index) +
                " was out of range.");
    }

    return MARROW_STATUS_OK;
}

template <typename SourceContainer, typename DestinationValue, typename Copier>
MarrowStatusCode copy_sequence(
    const SourceContainer& source,
    DestinationValue* destination,
    std::size_t capacity,
    std::size_t* out_written,
    const char* label,
    Copier copier) {
    if (out_written == nullptr) {
        return fail_invalid_argument(
            std::string(label) + " copy requires an out_written pointer.");
    }

    *out_written = source.size();
    if (source.empty()) {
        clear_last_error_message();
        return MARROW_STATUS_OK;
    }
    if (destination == nullptr || capacity < source.size()) {
        return fail(
            MARROW_STATUS_BUFFER_TOO_SMALL,
            std::string(label) + " buffer capacity " + std::to_string(capacity) +
                " was smaller than the required element count " +
                std::to_string(source.size()) + ".");
    }

    for (std::size_t index = 0; index < source.size(); ++index) {
        copier(source[index], &destination[index]);
    }
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

const marrow::renderer::RenderCommand* get_render_command(
    const MarrowRenderCommandList* command_list,
    std::size_t command_index) {
    if (command_list == nullptr || command_index >= command_list->value.commands.size()) {
        return nullptr;
    }

    return &command_list->value.commands[command_index];
}

const marrow::renderer::RenderClipCommand* get_clip_command(
    const MarrowRenderCommandList* command_list,
    std::size_t clip_command_index) {
    if (command_list == nullptr ||
        clip_command_index >= command_list->value.clip_commands.size()) {
        return nullptr;
    }

    return &command_list->value.clip_commands[clip_command_index];
}

MarrowStatusCode check_skeleton_last_error(marrow::runtime::Skeleton* skeleton) {
    const std::optional<std::string>& last_error = skeleton->last_error();
    if (!last_error.has_value()) {
        clear_last_error_message();
        return MARROW_STATUS_OK;
    }

    return fail(MARROW_STATUS_STATE_ERROR, *last_error);
}

void install_anim_state_listener(MarrowAnimState* anim_state) {
    if (anim_state->listener == nullptr) {
        anim_state->value->set_listener({});
        return;
    }

    anim_state->value->set_listener(
        [anim_state](
            marrow::runtime::AnimationState&,
            marrow::runtime::AnimationStateEventType type,
            const std::shared_ptr<marrow::runtime::TrackEntry>& entry,
            const marrow::runtime::AnimationEvent* event) {
            const MarrowAnimationStateListener listener = anim_state->listener;
            void* const user_data = anim_state->listener_user_data;
            if (listener == nullptr) {
                return;
            }

            MarrowAnimationStateEvent state_event{};
            state_event.type = to_c_animation_event_type(type);
            if (entry != nullptr) {
                state_event.track_index = entry->track_index;
                state_event.animation_name = to_string_view(entry->animation_name);
                state_event.loop = entry->loop;
                state_event.is_empty = entry->is_empty;
                state_event.track_time = entry->track_time;
                state_event.mix_time = entry->mix_time;
                state_event.mix_duration = entry->mix_duration;
            }
            if (event != nullptr) {
                state_event.has_event_data = true;
                fill_animation_event_data(*event, &state_event.event_data);
            }

            listener(&state_event, user_data);
        });
}

} // namespace

extern "C" {

MarrowStatusCode marrow_check_abi_version(uint32_t expected_abi_version) {
    if (expected_abi_version != MARROW_C_ABI_VERSION) {
        return fail(
            MARROW_STATUS_ABI_MISMATCH,
            "Expected Marrow C ABI version " + std::to_string(expected_abi_version) +
                ", but the library exports version " +
                std::to_string(MARROW_C_ABI_VERSION) + ".");
    }

    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_get_library_abi_version(uint32_t* out_abi_version) {
    if (out_abi_version == nullptr) {
        return fail_invalid_argument("out_abi_version must not be null.");
    }

    *out_abi_version = MARROW_C_ABI_VERSION;
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_get_last_error_message(MarrowStringView* out_message) {
    if (out_message == nullptr) {
        return fail_invalid_argument("out_message must not be null.");
    }

    *out_message = to_string_view(g_last_error_message);
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_status_code_name(
    MarrowStatusCode status_code,
    MarrowStringView* out_name) {
    if (out_name == nullptr) {
        return fail_invalid_argument("out_name must not be null.");
    }

    std::string_view name = "MARROW_STATUS_UNKNOWN";
    switch (status_code) {
    case MARROW_STATUS_OK:
        name = "MARROW_STATUS_OK";
        break;
    case MARROW_STATUS_INVALID_ARGUMENT:
        name = "MARROW_STATUS_INVALID_ARGUMENT";
        break;
    case MARROW_STATUS_ABI_MISMATCH:
        name = "MARROW_STATUS_ABI_MISMATCH";
        break;
    case MARROW_STATUS_IO_ERROR:
        name = "MARROW_STATUS_IO_ERROR";
        break;
    case MARROW_STATUS_PARSE_ERROR:
        name = "MARROW_STATUS_PARSE_ERROR";
        break;
    case MARROW_STATUS_NOT_FOUND:
        name = "MARROW_STATUS_NOT_FOUND";
        break;
    case MARROW_STATUS_OUT_OF_RANGE:
        name = "MARROW_STATUS_OUT_OF_RANGE";
        break;
    case MARROW_STATUS_STATE_ERROR:
        name = "MARROW_STATUS_STATE_ERROR";
        break;
    case MARROW_STATUS_BUFFER_TOO_SMALL:
        name = "MARROW_STATUS_BUFFER_TOO_SMALL";
        break;
    case MARROW_STATUS_INTERNAL_ERROR:
        name = "MARROW_STATUS_INTERNAL_ERROR";
        break;
    }

    *out_name = to_string_view(name);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_set_allocator(const MarrowAllocator* allocator) {
    try {
        if (allocator != nullptr &&
            (allocator->allocate == nullptr || allocator->deallocate == nullptr)) {
            return fail_invalid_argument(
                "marrow_set_allocator requires both allocate and deallocate callbacks.");
        }

        if (allocator != nullptr) {
            callback_allocator().set_callbacks(*allocator);
        }

        if (!marrow::set_allocator(allocator != nullptr ? &callback_allocator() : nullptr)) {
            return fail(
                MARROW_STATUS_STATE_ERROR,
                "marrow_set_allocator must run before any live Marrow allocations exist.");
        }

        clear_last_error_message();
        return MARROW_STATUS_OK;
    } catch (const std::exception& error) {
        return map_exception(error);
    }
}

MarrowStatusCode marrow_skeleton_data_load(
    const char* path,
    MarrowSkeletonData** out_skeleton_data) {
    if (path == nullptr || out_skeleton_data == nullptr) {
        return fail_invalid_argument(
            "marrow_skeleton_data_load requires non-null path and output pointers.");
    }

    try {
        const auto result = marrow::runtime::load_skeleton_data(path);
        if (!result) {
            return fail(map_json_error(*result.error), result.error->format());
        }

        *out_skeleton_data =
            marrow::allocate_object<MarrowSkeletonData>(result.skeleton_data);
        clear_last_error_message();
        return MARROW_STATUS_OK;
    } catch (const std::exception& error) {
        return map_exception(error);
    }
}

MarrowStatusCode marrow_skeleton_data_destroy(MarrowSkeletonData* skeleton_data) {
    marrow::destroy_object(skeleton_data);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_data_get_info(
    const MarrowSkeletonData* skeleton_data,
    MarrowSkeletonInfo* out_info) {
    if (skeleton_data == nullptr || out_info == nullptr) {
        return fail_invalid_argument(
            "marrow_skeleton_data_get_info requires non-null handles.");
    }

    const marrow::runtime::SkeletonInfo& info = skeleton_data->value->info();
    out_info->name = to_string_view(info.name);
    out_info->width = info.width;
    out_info->height = info.height;
    out_info->bone_count = skeleton_data->value->bones().size();
    out_info->slot_count = skeleton_data->value->slots().size();
    out_info->animation_count = skeleton_data->value->animations().size();
    out_info->skin_count = skeleton_data->value->skins().size();
    out_info->default_mix_duration = skeleton_data->value->default_mix_duration();
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_data_get_bone_name(
    const MarrowSkeletonData* skeleton_data,
    size_t bone_index,
    MarrowStringView* out_name) {
    if (skeleton_data == nullptr || out_name == nullptr) {
        return fail_invalid_argument(
            "marrow_skeleton_data_get_bone_name requires non-null handles.");
    }
    if (const MarrowStatusCode status =
            require_index(skeleton_data->value->bones(), bone_index, "bone");
        status != MARROW_STATUS_OK) {
        return status;
    }

    *out_name = to_string_view(skeleton_data->value->bones()[bone_index].name);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_data_get_slot_name(
    const MarrowSkeletonData* skeleton_data,
    size_t slot_index,
    MarrowStringView* out_name) {
    if (skeleton_data == nullptr || out_name == nullptr) {
        return fail_invalid_argument(
            "marrow_skeleton_data_get_slot_name requires non-null handles.");
    }
    if (const MarrowStatusCode status =
            require_index(skeleton_data->value->slots(), slot_index, "slot");
        status != MARROW_STATUS_OK) {
        return status;
    }

    *out_name = to_string_view(skeleton_data->value->slots()[slot_index].name);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_data_get_animation_name(
    const MarrowSkeletonData* skeleton_data,
    size_t animation_index,
    MarrowStringView* out_name) {
    if (skeleton_data == nullptr || out_name == nullptr) {
        return fail_invalid_argument(
            "marrow_skeleton_data_get_animation_name requires non-null handles.");
    }
    if (const MarrowStatusCode status =
            require_index(skeleton_data->value->animations(), animation_index, "animation");
        status != MARROW_STATUS_OK) {
        return status;
    }

    *out_name = to_string_view(skeleton_data->value->animations()[animation_index].name);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_data_get_skin_name(
    const MarrowSkeletonData* skeleton_data,
    size_t skin_index,
    MarrowStringView* out_name) {
    if (skeleton_data == nullptr || out_name == nullptr) {
        return fail_invalid_argument(
            "marrow_skeleton_data_get_skin_name requires non-null handles.");
    }
    if (const MarrowStatusCode status =
            require_index(skeleton_data->value->skins(), skin_index, "skin");
        status != MARROW_STATUS_OK) {
        return status;
    }

    *out_name = to_string_view(skeleton_data->value->skins()[skin_index].name);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_atlas_data_load(
    const char* path,
    MarrowAtlasData** out_atlas_data) {
    if (path == nullptr || out_atlas_data == nullptr) {
        return fail_invalid_argument(
            "marrow_atlas_data_load requires non-null path and output pointers.");
    }

    try {
        const auto result = marrow::runtime::AtlasLoader::load(path);
        if (!result) {
            return fail(map_json_error(*result.error), result.error->format());
        }

        *out_atlas_data = marrow::allocate_object<MarrowAtlasData>(result.atlas_data);
        clear_last_error_message();
        return MARROW_STATUS_OK;
    } catch (const std::exception& error) {
        return map_exception(error);
    }
}

MarrowStatusCode marrow_atlas_data_destroy(MarrowAtlasData* atlas_data) {
    marrow::destroy_object(atlas_data);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_atlas_data_get_info(
    const MarrowAtlasData* atlas_data,
    MarrowAtlasInfo* out_info) {
    if (atlas_data == nullptr || out_info == nullptr) {
        return fail_invalid_argument(
            "marrow_atlas_data_get_info requires non-null handles.");
    }

    const marrow::runtime::AtlasInfo& info = atlas_data->value->info();
    out_info->name = to_string_view(info.name);
    out_info->image = to_string_view(info.image);
    out_info->width = info.width;
    out_info->height = info.height;
    out_info->filter_min = to_string_view(info.filter_min);
    out_info->filter_mag = to_string_view(info.filter_mag);
    out_info->wrap_x = to_string_view(info.wrap_x);
    out_info->wrap_y = to_string_view(info.wrap_y);
    out_info->premultiplied_alpha = info.premultiplied_alpha;
    out_info->region_count = atlas_data->value->regions().size();
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_atlas_data_get_region_info(
    const MarrowAtlasData* atlas_data,
    size_t region_index,
    MarrowAtlasRegionInfo* out_region_info) {
    if (atlas_data == nullptr || out_region_info == nullptr) {
        return fail_invalid_argument(
            "marrow_atlas_data_get_region_info requires non-null handles.");
    }
    if (const MarrowStatusCode status =
            require_index(atlas_data->value->regions(), region_index, "atlas region");
        status != MARROW_STATUS_OK) {
        return status;
    }

    const marrow::runtime::AtlasRegion& region = atlas_data->value->regions()[region_index];
    out_region_info->name = to_string_view(region.name);
    out_region_info->x = region.x;
    out_region_info->y = region.y;
    out_region_info->width = region.width;
    out_region_info->height = region.height;
    out_region_info->origin_x = region.origin_x;
    out_region_info->origin_y = region.origin_y;
    out_region_info->rotate_degrees = region.rotate_degrees;
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_create(
    const MarrowSkeletonData* skeleton_data,
    MarrowSkeleton** out_skeleton) {
    if (skeleton_data == nullptr || out_skeleton == nullptr) {
        return fail_invalid_argument(
            "marrow_skeleton_create requires non-null handles.");
    }

    try {
        *out_skeleton = marrow::allocate_object<MarrowSkeleton>(
            marrow::allocate_unique<marrow::runtime::Skeleton>(skeleton_data->value));
        clear_last_error_message();
        return MARROW_STATUS_OK;
    } catch (const std::exception& error) {
        return map_exception(error);
    }
}

MarrowStatusCode marrow_skeleton_destroy(MarrowSkeleton* skeleton) {
    marrow::destroy_object(skeleton);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_set_scale(
    MarrowSkeleton* skeleton,
    double scale_x,
    double scale_y) {
    if (skeleton == nullptr) {
        return fail_invalid_argument("skeleton must not be null.");
    }

    skeleton->value->set_scale(scale_x, scale_y);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_set_to_setup_pose(MarrowSkeleton* skeleton) {
    if (skeleton == nullptr) {
        return fail_invalid_argument("skeleton must not be null.");
    }

    skeleton->value->clear_last_error();
    skeleton->value->set_to_setup_pose();
    return check_skeleton_last_error(skeleton->value.get());
}

MarrowStatusCode marrow_skeleton_reset_physics(MarrowSkeleton* skeleton) {
    if (skeleton == nullptr) {
        return fail_invalid_argument("skeleton must not be null.");
    }

    skeleton->value->reset_physics();
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_set_skin(
    MarrowSkeleton* skeleton,
    const char* skin_name) {
    if (skeleton == nullptr || skin_name == nullptr) {
        return fail_invalid_argument(
            "marrow_skeleton_set_skin requires non-null handles.");
    }

    skeleton->value->clear_last_error();
    if (!skeleton->value->set_skin(skin_name)) {
        const std::optional<std::string>& last_error = skeleton->value->last_error();
        if (last_error.has_value()) {
            return fail(MARROW_STATUS_STATE_ERROR, *last_error);
        }

        return fail(
            MARROW_STATUS_NOT_FOUND,
            "Skin '" + std::string(skin_name) + "' was not found.");
    }

    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_set_skin_composition(
    MarrowSkeleton* skeleton,
    const char* const* skin_names,
    size_t skin_name_count) {
    if (skeleton == nullptr) {
        return fail_invalid_argument("skeleton must not be null.");
    }
    if (skin_name_count > 0U && skin_names == nullptr) {
        return fail_invalid_argument(
            "skin_names must not be null when skin_name_count is non-zero.");
    }

    std::vector<std::string_view> views;
    views.reserve(skin_name_count);
    for (std::size_t index = 0; index < skin_name_count; ++index) {
        if (skin_names[index] == nullptr) {
            return fail_invalid_argument(
                "skin_names contained a null entry at index " +
                std::to_string(index) + ".");
        }
        views.emplace_back(skin_names[index]);
    }

    skeleton->value->clear_last_error();
    if (!skeleton->value->set_skin_composition(views)) {
        const std::optional<std::string>& last_error = skeleton->value->last_error();
        if (last_error.has_value()) {
            return fail(MARROW_STATUS_STATE_ERROR, *last_error);
        }

        return fail(
            MARROW_STATUS_NOT_FOUND,
            "One or more skin names in the requested composition were not found.");
    }

    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_set_attachment_playback_time(
    MarrowSkeleton* skeleton,
    double time_seconds) {
    if (skeleton == nullptr) {
        return fail_invalid_argument("skeleton must not be null.");
    }

    skeleton->value->set_attachment_playback_time(time_seconds);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_advance_attachment_playback(
    MarrowSkeleton* skeleton,
    double delta_seconds) {
    if (skeleton == nullptr) {
        return fail_invalid_argument("skeleton must not be null.");
    }

    skeleton->value->advance_attachment_playback(delta_seconds);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_update_world_transforms(
    MarrowSkeleton* skeleton,
    MarrowPhysicsMode physics_mode) {
    if (skeleton == nullptr) {
        return fail_invalid_argument("skeleton must not be null.");
    }

    marrow::runtime::PhysicsMode runtime_mode{};
    if (!to_runtime_physics_mode(physics_mode, &runtime_mode)) {
        return fail_invalid_argument("physics_mode did not match a known enum value.");
    }

    skeleton->value->clear_last_error();
    skeleton->value->update_world_transforms(runtime_mode);
    return check_skeleton_last_error(skeleton->value.get());
}

MarrowStatusCode marrow_skeleton_get_current_attachment_name(
    const MarrowSkeleton* skeleton,
    size_t slot_index,
    MarrowStringView* out_attachment_name) {
    if (skeleton == nullptr || out_attachment_name == nullptr) {
        return fail_invalid_argument(
            "marrow_skeleton_get_current_attachment_name requires non-null handles.");
    }
    if (const MarrowStatusCode status =
            require_index(skeleton->value->slot_states(), slot_index, "slot");
        status != MARROW_STATUS_OK) {
        return status;
    }

    *out_attachment_name =
        to_string_view(skeleton->value->slot_states()[slot_index].attachment_name);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_get_current_region_name(
    const MarrowSkeleton* skeleton,
    size_t slot_index,
    MarrowStringView* out_region_name) {
    if (skeleton == nullptr || out_region_name == nullptr) {
        return fail_invalid_argument(
            "marrow_skeleton_get_current_region_name requires non-null handles.");
    }
    if (const MarrowStatusCode status =
            require_index(skeleton->value->slot_states(), slot_index, "slot");
        status != MARROW_STATUS_OK) {
        return status;
    }

    *out_region_name = to_string_view(
        skeleton->value->current_region_name(slot_index));
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_anim_state_create(
    const MarrowSkeletonData* skeleton_data,
    MarrowAnimState** out_anim_state) {
    if (skeleton_data == nullptr || out_anim_state == nullptr) {
        return fail_invalid_argument(
            "marrow_anim_state_create requires non-null handles.");
    }

    try {
        *out_anim_state = marrow::allocate_object<MarrowAnimState>(
            marrow::allocate_unique<marrow::runtime::AnimationState>(skeleton_data->value));
        clear_last_error_message();
        return MARROW_STATUS_OK;
    } catch (const std::exception& error) {
        return map_exception(error);
    }
}

MarrowStatusCode marrow_anim_state_destroy(MarrowAnimState* anim_state) {
    marrow::destroy_object(anim_state);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_anim_state_set_listener(
    MarrowAnimState* anim_state,
    MarrowAnimationStateListener listener,
    void* user_data) {
    if (anim_state == nullptr) {
        return fail_invalid_argument("anim_state must not be null.");
    }

    anim_state->listener = listener;
    anim_state->listener_user_data = user_data;
    install_anim_state_listener(anim_state);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_anim_state_set_animation(
    MarrowAnimState* anim_state,
    size_t track_index,
    const char* animation_name,
    bool loop) {
    if (anim_state == nullptr || animation_name == nullptr) {
        return fail_invalid_argument(
            "marrow_anim_state_set_animation requires non-null handles.");
    }

    try {
        anim_state->value->set_animation(track_index, animation_name, loop);
        clear_last_error_message();
        return MARROW_STATUS_OK;
    } catch (const std::invalid_argument& error) {
        return fail(MARROW_STATUS_NOT_FOUND, error.what());
    } catch (const std::exception& error) {
        return map_exception(error);
    }
}

MarrowStatusCode marrow_anim_state_set_animation_with_mix(
    MarrowAnimState* anim_state,
    size_t track_index,
    const char* animation_name,
    bool loop,
    double mix_duration) {
    if (anim_state == nullptr || animation_name == nullptr) {
        return fail_invalid_argument(
            "marrow_anim_state_set_animation_with_mix requires non-null handles.");
    }

    try {
        anim_state->value->set_animation(track_index, animation_name, loop, mix_duration);
        clear_last_error_message();
        return MARROW_STATUS_OK;
    } catch (const std::invalid_argument& error) {
        return fail(MARROW_STATUS_NOT_FOUND, error.what());
    } catch (const std::exception& error) {
        return map_exception(error);
    }
}

MarrowStatusCode marrow_anim_state_add_animation(
    MarrowAnimState* anim_state,
    size_t track_index,
    const char* animation_name,
    bool loop,
    double delay_seconds) {
    if (anim_state == nullptr || animation_name == nullptr) {
        return fail_invalid_argument(
            "marrow_anim_state_add_animation requires non-null handles.");
    }

    try {
        anim_state->value->add_animation(track_index, animation_name, loop, delay_seconds);
        clear_last_error_message();
        return MARROW_STATUS_OK;
    } catch (const std::invalid_argument& error) {
        return fail(MARROW_STATUS_NOT_FOUND, error.what());
    } catch (const std::exception& error) {
        return map_exception(error);
    }
}

MarrowStatusCode marrow_anim_state_add_animation_with_mix(
    MarrowAnimState* anim_state,
    size_t track_index,
    const char* animation_name,
    bool loop,
    double delay_seconds,
    double mix_duration) {
    if (anim_state == nullptr || animation_name == nullptr) {
        return fail_invalid_argument(
            "marrow_anim_state_add_animation_with_mix requires non-null handles.");
    }

    try {
        anim_state->value->add_animation(
            track_index,
            animation_name,
            loop,
            delay_seconds,
            mix_duration);
        clear_last_error_message();
        return MARROW_STATUS_OK;
    } catch (const std::invalid_argument& error) {
        return fail(MARROW_STATUS_NOT_FOUND, error.what());
    } catch (const std::exception& error) {
        return map_exception(error);
    }
}

MarrowStatusCode marrow_anim_state_set_empty_animation(
    MarrowAnimState* anim_state,
    size_t track_index,
    double mix_duration) {
    if (anim_state == nullptr) {
        return fail_invalid_argument("anim_state must not be null.");
    }

    anim_state->value->set_empty_animation(track_index, mix_duration);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_anim_state_add_empty_animation(
    MarrowAnimState* anim_state,
    size_t track_index,
    double mix_duration,
    double delay_seconds) {
    if (anim_state == nullptr) {
        return fail_invalid_argument("anim_state must not be null.");
    }

    anim_state->value->add_empty_animation(track_index, mix_duration, delay_seconds);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_anim_state_clear_track(
    MarrowAnimState* anim_state,
    size_t track_index) {
    if (anim_state == nullptr) {
        return fail_invalid_argument("anim_state must not be null.");
    }

    anim_state->value->clear_track(track_index);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_anim_state_clear_tracks(MarrowAnimState* anim_state) {
    if (anim_state == nullptr) {
        return fail_invalid_argument("anim_state must not be null.");
    }

    anim_state->value->clear_tracks();
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_anim_state_update(
    MarrowAnimState* anim_state,
    double delta_seconds) {
    if (anim_state == nullptr) {
        return fail_invalid_argument("anim_state must not be null.");
    }

    try {
        anim_state->value->update(delta_seconds);
        clear_last_error_message();
        return MARROW_STATUS_OK;
    } catch (const std::invalid_argument& error) {
        return fail(MARROW_STATUS_INVALID_ARGUMENT, error.what());
    } catch (const std::exception& error) {
        return map_exception(error);
    }
}

MarrowStatusCode marrow_anim_state_apply(
    MarrowAnimState* anim_state,
    MarrowSkeleton* skeleton) {
    if (anim_state == nullptr || skeleton == nullptr) {
        return fail_invalid_argument(
            "marrow_anim_state_apply requires non-null handles.");
    }

    try {
        skeleton->value->clear_last_error();
        anim_state->value->apply(*skeleton->value);
        return check_skeleton_last_error(skeleton->value.get());
    } catch (const std::invalid_argument& error) {
        return fail(MARROW_STATUS_INVALID_ARGUMENT, error.what());
    } catch (const std::exception& error) {
        return map_exception(error);
    }
}

MarrowStatusCode marrow_anim_state_get_current_track(
    const MarrowAnimState* anim_state,
    size_t track_index,
    MarrowTrackState* out_track_state) {
    if (anim_state == nullptr || out_track_state == nullptr) {
        return fail_invalid_argument(
            "marrow_anim_state_get_current_track requires non-null handles.");
    }

    const std::shared_ptr<marrow::runtime::TrackEntry> entry =
        anim_state->value->get_current(track_index);
    if (entry == nullptr) {
        return fail(
            MARROW_STATUS_NOT_FOUND,
            "Track " + std::to_string(track_index) + " did not contain an active entry.");
    }

    fill_track_state(*entry, out_track_state);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_anim_state_extract_root_motion(
    const MarrowAnimState* anim_state,
    size_t track_index,
    size_t root_bone_index,
    MarrowRootMotionDelta* out_delta) {
    if (anim_state == nullptr || out_delta == nullptr) {
        return fail_invalid_argument(
            "marrow_anim_state_extract_root_motion requires non-null handles.");
    }

    const marrow::runtime::RootMotionDelta delta =
        anim_state->value->extract_root_motion(track_index, root_bone_index);
    out_delta->x = delta.x;
    out_delta->y = delta.y;
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_bounds_create(
    MarrowSkeletonBounds** out_bounds) {
    if (out_bounds == nullptr) {
        return fail_invalid_argument("out_bounds must not be null.");
    }

    try {
        *out_bounds = marrow::allocate_object<MarrowSkeletonBounds>(
            marrow::allocate_unique<marrow::runtime::SkeletonBounds>());
        clear_last_error_message();
        return MARROW_STATUS_OK;
    } catch (const std::exception& error) {
        return map_exception(error);
    }
}

MarrowStatusCode marrow_skeleton_bounds_destroy(MarrowSkeletonBounds* bounds) {
    marrow::destroy_object(bounds);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_bounds_update(
    MarrowSkeletonBounds* bounds,
    const MarrowSkeleton* skeleton,
    bool compute_aabb) {
    if (bounds == nullptr || skeleton == nullptr) {
        return fail_invalid_argument(
            "marrow_skeleton_bounds_update requires non-null handles.");
    }

    bounds->value->update(*skeleton->value, compute_aabb);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_bounds_has_aabb(
    const MarrowSkeletonBounds* bounds,
    bool* out_has_aabb) {
    if (bounds == nullptr || out_has_aabb == nullptr) {
        return fail_invalid_argument(
            "marrow_skeleton_bounds_has_aabb requires non-null handles.");
    }

    *out_has_aabb = bounds->value->has_aabb();
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_bounds_get_aabb(
    const MarrowSkeletonBounds* bounds,
    MarrowAabb* out_aabb) {
    if (bounds == nullptr || out_aabb == nullptr) {
        return fail_invalid_argument(
            "marrow_skeleton_bounds_get_aabb requires non-null handles.");
    }
    if (!bounds->value->has_aabb()) {
        return fail(MARROW_STATUS_NOT_FOUND, "Skeleton bounds did not have an AABB.");
    }

    out_aabb->min_x = bounds->value->min_x();
    out_aabb->min_y = bounds->value->min_y();
    out_aabb->max_x = bounds->value->max_x();
    out_aabb->max_y = bounds->value->max_y();
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_bounds_contains_point(
    MarrowSkeletonBounds* bounds,
    double x,
    double y,
    bool* out_contains) {
    if (bounds == nullptr || out_contains == nullptr) {
        return fail_invalid_argument(
            "marrow_skeleton_bounds_contains_point requires non-null handles.");
    }

    *out_contains = bounds->value->contains_point(x, y);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_skeleton_bounds_intersects_segment(
    MarrowSkeletonBounds* bounds,
    double x1,
    double y1,
    double x2,
    double y2,
    bool* out_intersects) {
    if (bounds == nullptr || out_intersects == nullptr) {
        return fail_invalid_argument(
            "marrow_skeleton_bounds_intersects_segment requires non-null handles.");
    }

    *out_intersects = bounds->value->intersects_segment(x1, y1, x2, y2);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_render_build_command_list(
    const MarrowSkeleton* skeleton,
    const MarrowAtlasData* atlas_data,
    const float projection[16],
    MarrowRenderCommandList** out_command_list) {
    if (skeleton == nullptr || atlas_data == nullptr ||
        projection == nullptr || out_command_list == nullptr) {
        return fail_invalid_argument(
            "marrow_render_build_command_list requires non-null handles.");
    }

    try {
        const auto scene_result =
            marrow::renderer::prepare_setup_pose_scene(*skeleton->value, *atlas_data->value);
        if (!scene_result) {
            return fail(MARROW_STATUS_STATE_ERROR, scene_result.error_message);
        }

        std::array<float, 16> projection_array{};
        for (std::size_t index = 0; index < projection_array.size(); ++index) {
            projection_array[index] = projection[index];
        }

        const auto command_list_result =
            marrow::renderer::build_render_command_list(
                *scene_result.scene,
                projection_array);
        if (!command_list_result) {
            return fail(MARROW_STATUS_STATE_ERROR, command_list_result.error_message);
        }

        *out_command_list = marrow::allocate_object<MarrowRenderCommandList>(
            std::move(*command_list_result.command_list));
        clear_last_error_message();
        return MARROW_STATUS_OK;
    } catch (const std::exception& error) {
        return map_exception(error);
    }
}

MarrowStatusCode marrow_render_command_list_destroy(MarrowRenderCommandList* command_list) {
    marrow::destroy_object(command_list);
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_render_command_list_get_command_count(
    const MarrowRenderCommandList* command_list,
    size_t* out_command_count) {
    if (command_list == nullptr || out_command_count == nullptr) {
        return fail_invalid_argument(
            "marrow_render_command_list_get_command_count requires non-null handles.");
    }

    *out_command_count = command_list->value.commands.size();
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_render_command_list_get_clip_command_count(
    const MarrowRenderCommandList* command_list,
    size_t* out_clip_command_count) {
    if (command_list == nullptr || out_clip_command_count == nullptr) {
        return fail_invalid_argument(
            "marrow_render_command_list_get_clip_command_count requires non-null handles.");
    }

    *out_clip_command_count = command_list->value.clip_commands.size();
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_render_command_list_get_event_count(
    const MarrowRenderCommandList* command_list,
    size_t* out_event_count) {
    if (command_list == nullptr || out_event_count == nullptr) {
        return fail_invalid_argument(
            "marrow_render_command_list_get_event_count requires non-null handles.");
    }

    *out_event_count = command_list->value.ordered_events.size();
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_render_command_list_get_premultiplied_alpha(
    const MarrowRenderCommandList* command_list,
    bool* out_premultiplied_alpha) {
    if (command_list == nullptr || out_premultiplied_alpha == nullptr) {
        return fail_invalid_argument(
            "marrow_render_command_list_get_premultiplied_alpha requires non-null handles.");
    }

    *out_premultiplied_alpha = command_list->value.premultiplied_alpha;
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_render_command_list_get_projection(
    const MarrowRenderCommandList* command_list,
    float out_projection[16]) {
    if (command_list == nullptr || out_projection == nullptr) {
        return fail_invalid_argument(
            "marrow_render_command_list_get_projection requires non-null handles.");
    }

    for (std::size_t index = 0; index < command_list->value.projection.size(); ++index) {
        out_projection[index] = command_list->value.projection[index];
    }
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_render_command_list_copy_bone_palette(
    const MarrowRenderCommandList* command_list,
    float* out_bone_palette,
    size_t bone_palette_capacity,
    size_t* out_written) {
    if (command_list == nullptr) {
        return fail_invalid_argument("command_list must not be null.");
    }

    return copy_sequence(
        command_list->value.bone_palette,
        out_bone_palette,
        bone_palette_capacity,
        out_written,
        "bone palette",
        [](float source, float* destination) {
            *destination = source;
        });
}

MarrowStatusCode marrow_render_command_list_get_command_info(
    const MarrowRenderCommandList* command_list,
    size_t command_index,
    MarrowRenderCommandInfo* out_command_info) {
    if (command_list == nullptr || out_command_info == nullptr) {
        return fail_invalid_argument(
            "marrow_render_command_list_get_command_info requires non-null handles.");
    }
    if (const MarrowStatusCode status =
            require_index(command_list->value.commands, command_index, "render command");
        status != MARROW_STATUS_OK) {
        return status;
    }

    const marrow::renderer::RenderCommand& command =
        command_list->value.commands[command_index];
    out_command_info->vertex_count = command.vertices.size();
    out_command_info->index_count = command.indices.size();
    out_command_info->texture_name = to_string_view(command.texture_name);
    out_command_info->texture_handle = command.texture_handle;
    out_command_info->blend_mode = to_c_blend_mode(command.blend_mode);
    out_command_info->shader_variant = to_c_shader_variant(command.shader_variant);
    out_command_info->source_draw_command_offset = command.source_draw_command_offset;
    out_command_info->source_draw_command_count = command.source_draw_command_count;
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_render_command_list_copy_command_vertices(
    const MarrowRenderCommandList* command_list,
    size_t command_index,
    MarrowRenderCommandVertex* out_vertices,
    size_t vertex_capacity,
    size_t* out_written) {
    const marrow::renderer::RenderCommand* command =
        get_render_command(command_list, command_index);
    if (command == nullptr) {
        return fail(
            MARROW_STATUS_OUT_OF_RANGE,
            "render command index " + std::to_string(command_index) +
                " was out of range.");
    }

    return copy_sequence(
        command->vertices,
        out_vertices,
        vertex_capacity,
        out_written,
        "render command vertex",
        [](const marrow::renderer::RenderCommandVertex& source,
           MarrowRenderCommandVertex* destination) {
            fill_render_command_vertex(source, destination);
        });
}

MarrowStatusCode marrow_render_command_list_copy_command_indices(
    const MarrowRenderCommandList* command_list,
    size_t command_index,
    uint32_t* out_indices,
    size_t index_capacity,
    size_t* out_written) {
    const marrow::renderer::RenderCommand* command =
        get_render_command(command_list, command_index);
    if (command == nullptr) {
        return fail(
            MARROW_STATUS_OUT_OF_RANGE,
            "render command index " + std::to_string(command_index) +
                " was out of range.");
    }

    return copy_sequence(
        command->indices,
        out_indices,
        index_capacity,
        out_written,
        "render command index",
        [](std::uint32_t source, std::uint32_t* destination) {
            *destination = source;
        });
}

MarrowStatusCode marrow_render_command_list_get_clip_command_info(
    const MarrowRenderCommandList* command_list,
    size_t clip_command_index,
    MarrowClipCommandInfo* out_clip_command_info) {
    if (command_list == nullptr || out_clip_command_info == nullptr) {
        return fail_invalid_argument(
            "marrow_render_command_list_get_clip_command_info requires non-null handles.");
    }
    if (const MarrowStatusCode status =
            require_index(command_list->value.clip_commands, clip_command_index, "clip command");
        status != MARROW_STATUS_OK) {
        return status;
    }

    const marrow::renderer::RenderClipCommand& command =
        command_list->value.clip_commands[clip_command_index];
    out_clip_command_info->vertex_count = command.vertices.size();
    out_clip_command_info->index_count = command.indices.size();
    out_clip_command_info->attachment_name = to_string_view(command.attachment_name);
    out_clip_command_info->source_clip_attachment_index = command.source_clip_attachment_index;
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

MarrowStatusCode marrow_render_command_list_copy_clip_vertices(
    const MarrowRenderCommandList* command_list,
    size_t clip_command_index,
    MarrowRenderCommandVertex* out_vertices,
    size_t vertex_capacity,
    size_t* out_written) {
    const marrow::renderer::RenderClipCommand* command =
        get_clip_command(command_list, clip_command_index);
    if (command == nullptr) {
        return fail(
            MARROW_STATUS_OUT_OF_RANGE,
            "clip command index " + std::to_string(clip_command_index) +
                " was out of range.");
    }

    return copy_sequence(
        command->vertices,
        out_vertices,
        vertex_capacity,
        out_written,
        "clip vertex",
        [](const marrow::renderer::RenderCommandVertex& source,
           MarrowRenderCommandVertex* destination) {
            fill_render_command_vertex(source, destination);
        });
}

MarrowStatusCode marrow_render_command_list_copy_clip_indices(
    const MarrowRenderCommandList* command_list,
    size_t clip_command_index,
    uint32_t* out_indices,
    size_t index_capacity,
    size_t* out_written) {
    const marrow::renderer::RenderClipCommand* command =
        get_clip_command(command_list, clip_command_index);
    if (command == nullptr) {
        return fail(
            MARROW_STATUS_OUT_OF_RANGE,
            "clip command index " + std::to_string(clip_command_index) +
                " was out of range.");
    }

    return copy_sequence(
        command->indices,
        out_indices,
        index_capacity,
        out_written,
        "clip index",
        [](std::uint32_t source, std::uint32_t* destination) {
            *destination = source;
        });
}

MarrowStatusCode marrow_render_command_list_get_event_ref(
    const MarrowRenderCommandList* command_list,
    size_t event_index,
    MarrowRenderEventRef* out_event_ref) {
    if (command_list == nullptr || out_event_ref == nullptr) {
        return fail_invalid_argument(
            "marrow_render_command_list_get_event_ref requires non-null handles.");
    }
    if (const MarrowStatusCode status =
            require_index(command_list->value.ordered_events, event_index, "render event");
        status != MARROW_STATUS_OK) {
        return status;
    }

    const marrow::renderer::RenderCommandEventRef& event =
        command_list->value.ordered_events[event_index];
    out_event_ref->kind = to_c_render_event_kind(event.kind);
    out_event_ref->index = event.index;
    clear_last_error_message();
    return MARROW_STATUS_OK;
}

} // extern "C"
