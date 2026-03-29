#include "skeleton_internal.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace marrow::runtime {

namespace {

constexpr double kSamplingRewindEpsilon = 1e-6;
constexpr AnimationScalar kAnimationValueEpsilon = 1e-5f;

template <typename Timeline>
const Timeline* find_bone_timeline_fallback(
    const std::vector<Timeline>& timelines,
    std::size_t bone_index) {
    const auto it = std::find_if(
        timelines.begin(),
        timelines.end(),
        [&](const Timeline& timeline) {
            return timeline.bone_index == bone_index;
        });
    if (it == timelines.end()) {
        return nullptr;
    }

    return &(*it);
}

template <typename Timeline>
std::size_t timeline_local_index(
    const std::vector<Timeline>& timelines,
    const Timeline& timeline) {
    return static_cast<std::size_t>(&timeline - timelines.data());
}

std::size_t* cursor_at(std::vector<std::size_t>* cursors, std::size_t index) {
    if (cursors == nullptr || index >= cursors->size()) {
        return nullptr;
    }

    return &(*cursors)[index];
}

void reset_sampling_cursors(SamplingContext* context) {
    if (context == nullptr) {
        return;
    }

    std::fill(context->rotate_last_keyframe_indices.begin(), context->rotate_last_keyframe_indices.end(), 0);
    std::fill(context->inherit_last_keyframe_indices.begin(), context->inherit_last_keyframe_indices.end(), 0);
    std::fill(
        context->translate_last_keyframe_indices.begin(),
        context->translate_last_keyframe_indices.end(),
        0);
    std::fill(context->scale_last_keyframe_indices.begin(), context->scale_last_keyframe_indices.end(), 0);
    std::fill(context->shear_last_keyframe_indices.begin(), context->shear_last_keyframe_indices.end(), 0);
    std::fill(
        context->attachment_last_keyframe_indices.begin(),
        context->attachment_last_keyframe_indices.end(),
        0);
    std::fill(context->color_last_keyframe_indices.begin(), context->color_last_keyframe_indices.end(), 0);
    std::fill(context->deform_last_keyframe_indices.begin(), context->deform_last_keyframe_indices.end(), 0);
    context->draw_order_last_keyframe_index = 0;
}

void resize_sampling_cursors(const AnimationData& animation, SamplingContext* context) {
    if (context == nullptr) {
        return;
    }

    context->rotate_last_keyframe_indices.assign(animation.bone_rotate_timelines.size(), 0);
    context->inherit_last_keyframe_indices.assign(animation.bone_inherit_timelines.size(), 0);
    context->translate_last_keyframe_indices.assign(animation.bone_translate_timelines.size(), 0);
    context->scale_last_keyframe_indices.assign(animation.bone_scale_timelines.size(), 0);
    context->shear_last_keyframe_indices.assign(animation.bone_shear_timelines.size(), 0);
    context->attachment_last_keyframe_indices.assign(animation.slot_attachment_timelines.size(), 0);
    context->color_last_keyframe_indices.assign(animation.slot_color_timelines.size(), 0);
    context->deform_last_keyframe_indices.assign(animation.mesh_deform_timelines.size(), 0);
    context->draw_order_last_keyframe_index = 0;
}

bool nearly_equal(AnimationScalar actual, double expected) {
    return std::abs(static_cast<double>(actual) - expected) <=
        static_cast<double>(kAnimationValueEpsilon);
}

bool slot_color_matches(const SlotColor& color, const SlotColor& expected) {
    return nearly_equal(color.r, expected.r) &&
        nearly_equal(color.g, expected.g) &&
        nearly_equal(color.b, expected.b) &&
        nearly_equal(color.a, expected.a);
}

bool is_zero_offset_vector(const std::vector<AnimationScalar>& vertex_offsets) {
    return std::all_of(
        vertex_offsets.begin(),
        vertex_offsets.end(),
        [](AnimationScalar offset) {
            return nearly_equal(offset, 0.0);
        });
}

template <typename Timeline, typename Predicate>
void erase_matching_timelines(std::vector<Timeline>* timelines, Predicate&& predicate) {
    timelines->erase(
        std::remove_if(
            timelines->begin(),
            timelines->end(),
            [&](const Timeline& timeline) {
                return predicate(timeline);
            }),
        timelines->end());
}

void append_targeted_bone(std::vector<std::size_t>* targeted_bones, std::size_t bone_index) {
    if (targeted_bones == nullptr) {
        return;
    }
    if (std::find(targeted_bones->begin(), targeted_bones->end(), bone_index) != targeted_bones->end()) {
        return;
    }
    targeted_bones->push_back(bone_index);
}

std::vector<std::size_t> rebuild_targeted_bone_indices(const AnimationData& animation) {
    std::vector<std::size_t> targeted_bones;
    targeted_bones.reserve(
        animation.bone_rotate_timelines.size() +
        animation.bone_inherit_timelines.size() +
        animation.bone_translate_timelines.size() +
        animation.bone_scale_timelines.size() +
        animation.bone_shear_timelines.size());
    for (const BoneRotateTimeline& timeline : animation.bone_rotate_timelines) {
        append_targeted_bone(&targeted_bones, timeline.bone_index);
    }
    for (const BoneInheritTimeline& timeline : animation.bone_inherit_timelines) {
        append_targeted_bone(&targeted_bones, timeline.bone_index);
    }
    for (const BoneTranslateTimeline& timeline : animation.bone_translate_timelines) {
        append_targeted_bone(&targeted_bones, timeline.bone_index);
    }
    for (const BoneScaleTimeline& timeline : animation.bone_scale_timelines) {
        append_targeted_bone(&targeted_bones, timeline.bone_index);
    }
    for (const BoneShearTimeline& timeline : animation.bone_shear_timelines) {
        append_targeted_bone(&targeted_bones, timeline.bone_index);
    }
    std::sort(targeted_bones.begin(), targeted_bones.end());
    return targeted_bones;
}

} // namespace

AnimationData::AnimationData(const AnimationData& other)
    : name(other.name),
      targeted_bone_indices(other.targeted_bone_indices),
      bone_rotate_timelines(other.bone_rotate_timelines),
      bone_inherit_timelines(other.bone_inherit_timelines),
      bone_translate_timelines(other.bone_translate_timelines),
      bone_scale_timelines(other.bone_scale_timelines),
      bone_shear_timelines(other.bone_shear_timelines),
      slot_attachment_timelines(other.slot_attachment_timelines),
      slot_color_timelines(other.slot_color_timelines),
      mesh_deform_timelines(other.mesh_deform_timelines),
      draw_order_timeline_data(other.draw_order_timeline_data),
      event_timeline_data(other.event_timeline_data),
      bone_timeline_index(other.bone_timeline_index.size()) {
    if (!bone_timeline_index.empty()) {
        rebuild_bone_timeline_index(bone_timeline_index.size());
    }
}

AnimationData& AnimationData::operator=(const AnimationData& other) {
    if (this == &other) {
        return *this;
    }

    name = other.name;
    targeted_bone_indices = other.targeted_bone_indices;
    bone_rotate_timelines = other.bone_rotate_timelines;
    bone_inherit_timelines = other.bone_inherit_timelines;
    bone_translate_timelines = other.bone_translate_timelines;
    bone_scale_timelines = other.bone_scale_timelines;
    bone_shear_timelines = other.bone_shear_timelines;
    slot_attachment_timelines = other.slot_attachment_timelines;
    slot_color_timelines = other.slot_color_timelines;
    mesh_deform_timelines = other.mesh_deform_timelines;
    draw_order_timeline_data = other.draw_order_timeline_data;
    event_timeline_data = other.event_timeline_data;
    bone_timeline_index.resize(other.bone_timeline_index.size());
    if (!bone_timeline_index.empty()) {
        rebuild_bone_timeline_index(bone_timeline_index.size());
    } else {
        bone_timeline_index.clear();
    }

    return *this;
}

void AnimationData::rebuild_bone_timeline_index(std::size_t bone_count) {
    bone_timeline_index.assign(bone_count, BoneTimelineIndexEntry{});
    for (const BoneRotateTimeline& timeline : bone_rotate_timelines) {
        if (timeline.bone_index < bone_timeline_index.size()) {
            bone_timeline_index[timeline.bone_index].rotate = &timeline;
        }
    }
    for (const BoneInheritTimeline& timeline : bone_inherit_timelines) {
        if (timeline.bone_index < bone_timeline_index.size()) {
            bone_timeline_index[timeline.bone_index].inherit = &timeline;
        }
    }
    for (const BoneTranslateTimeline& timeline : bone_translate_timelines) {
        if (timeline.bone_index < bone_timeline_index.size()) {
            bone_timeline_index[timeline.bone_index].translate = &timeline;
        }
    }
    for (const BoneScaleTimeline& timeline : bone_scale_timelines) {
        if (timeline.bone_index < bone_timeline_index.size()) {
            bone_timeline_index[timeline.bone_index].scale = &timeline;
        }
    }
    for (const BoneShearTimeline& timeline : bone_shear_timelines) {
        if (timeline.bone_index < bone_timeline_index.size()) {
            bone_timeline_index[timeline.bone_index].shear = &timeline;
        }
    }
}

void AnimationData::prepare_sampling_context(SamplingContext* context, double time) const {
    if (context == nullptr) {
        return;
    }

    const bool animation_changed = context->animation != this;
    const bool cursor_layout_changed =
        context->rotate_last_keyframe_indices.size() != bone_rotate_timelines.size() ||
        context->inherit_last_keyframe_indices.size() != bone_inherit_timelines.size() ||
        context->translate_last_keyframe_indices.size() != bone_translate_timelines.size() ||
        context->scale_last_keyframe_indices.size() != bone_scale_timelines.size() ||
        context->shear_last_keyframe_indices.size() != bone_shear_timelines.size() ||
        context->attachment_last_keyframe_indices.size() != slot_attachment_timelines.size() ||
        context->color_last_keyframe_indices.size() != slot_color_timelines.size() ||
        context->deform_last_keyframe_indices.size() != mesh_deform_timelines.size();

    if (animation_changed || cursor_layout_changed) {
        context->animation = this;
        resize_sampling_cursors(*this, context);
    } else if (time + kSamplingRewindEpsilon < context->last_sample_time) {
        reset_sampling_cursors(context);
    }

    context->last_sample_time = time;
}

void AnimationData::prune_constant_timelines(
    const std::vector<BoneData>& bones,
    const std::vector<SlotData>& slots) {
    erase_matching_timelines(&bone_rotate_timelines, [&](const BoneRotateTimeline& timeline) {
        return timeline.keyframes.size() == 1U &&
            timeline.bone_index < bones.size() &&
            nearly_equal(timeline.keyframes.front().angle, 0.0);
    });
    erase_matching_timelines(&bone_inherit_timelines, [&](const BoneInheritTimeline& timeline) {
        return timeline.keyframes.size() == 1U &&
            timeline.bone_index < bones.size() &&
            timeline.keyframes.front().inherit == bones[timeline.bone_index].inherit;
    });
    erase_matching_timelines(&bone_translate_timelines, [&](const BoneTranslateTimeline& timeline) {
        return timeline.keyframes.size() == 1U &&
            timeline.bone_index < bones.size() &&
            nearly_equal(timeline.keyframes.front().x, bones[timeline.bone_index].setup_pose.x) &&
            nearly_equal(timeline.keyframes.front().y, bones[timeline.bone_index].setup_pose.y);
    });
    erase_matching_timelines(&bone_scale_timelines, [&](const BoneScaleTimeline& timeline) {
        return timeline.keyframes.size() == 1U &&
            timeline.bone_index < bones.size() &&
            nearly_equal(
                timeline.keyframes.front().x,
                bones[timeline.bone_index].setup_pose.scale_x) &&
            nearly_equal(
                timeline.keyframes.front().y,
                bones[timeline.bone_index].setup_pose.scale_y);
    });
    erase_matching_timelines(&bone_shear_timelines, [&](const BoneShearTimeline& timeline) {
        return timeline.keyframes.size() == 1U &&
            timeline.bone_index < bones.size() &&
            nearly_equal(
                timeline.keyframes.front().x,
                bones[timeline.bone_index].setup_pose.shear_x) &&
            nearly_equal(
                timeline.keyframes.front().y,
                bones[timeline.bone_index].setup_pose.shear_y);
    });
    erase_matching_timelines(&slot_attachment_timelines, [&](const SlotAttachmentTimeline& timeline) {
        return timeline.keyframes.size() == 1U &&
            timeline.slot_index < slots.size() &&
            timeline.keyframes.front().attachment_name == slots[timeline.slot_index].setup_attachment;
    });
    erase_matching_timelines(&slot_color_timelines, [&](const SlotColorTimeline& timeline) {
        return timeline.keyframes.size() == 1U &&
            timeline.slot_index < slots.size() &&
            slot_color_matches(timeline.keyframes.front().color, slots[timeline.slot_index].color);
    });
    erase_matching_timelines(&mesh_deform_timelines, [&](const MeshDeformTimeline& timeline) {
        return timeline.keyframes.size() == 1U &&
            is_zero_offset_vector(timeline.keyframes.front().vertex_offsets);
    });
    if (draw_order_timeline_data.has_value() &&
        draw_order_timeline_data->keyframes.size() == 1U &&
        draw_order_timeline_data->keyframes.front().slot_indices.size() == slots.size()) {
        bool draw_order_identity = true;
        for (std::size_t slot_index = 0;
             slot_index < draw_order_timeline_data->keyframes.front().slot_indices.size();
             ++slot_index) {
            if (draw_order_timeline_data->keyframes.front().slot_indices[slot_index] != slot_index) {
                draw_order_identity = false;
                break;
            }
        }
        if (draw_order_identity) {
            draw_order_timeline_data.reset();
        }
    }

    targeted_bone_indices = rebuild_targeted_bone_indices(*this);
}

const BoneRotateTimeline* AnimationData::find_rotate_timeline(std::size_t bone_index) const {
    if (bone_index < bone_timeline_index.size()) {
        return bone_timeline_index[bone_index].rotate;
    }

    return find_bone_timeline_fallback(bone_rotate_timelines, bone_index);
}

const BoneInheritTimeline* AnimationData::find_inherit_timeline(std::size_t bone_index) const {
    if (bone_index < bone_timeline_index.size()) {
        return bone_timeline_index[bone_index].inherit;
    }

    return find_bone_timeline_fallback(bone_inherit_timelines, bone_index);
}

const BoneTranslateTimeline* AnimationData::find_translate_timeline(std::size_t bone_index) const {
    if (bone_index < bone_timeline_index.size()) {
        return bone_timeline_index[bone_index].translate;
    }

    return find_bone_timeline_fallback(bone_translate_timelines, bone_index);
}

const BoneScaleTimeline* AnimationData::find_scale_timeline(std::size_t bone_index) const {
    if (bone_index < bone_timeline_index.size()) {
        return bone_timeline_index[bone_index].scale;
    }

    return find_bone_timeline_fallback(bone_scale_timelines, bone_index);
}

const BoneShearTimeline* AnimationData::find_shear_timeline(std::size_t bone_index) const {
    if (bone_index < bone_timeline_index.size()) {
        return bone_timeline_index[bone_index].shear;
    }

    return find_bone_timeline_fallback(bone_shear_timelines, bone_index);
}

const SlotAttachmentTimeline* AnimationData::find_attachment_timeline(std::size_t slot_index) const {
    const auto it = std::find_if(
        slot_attachment_timelines.begin(),
        slot_attachment_timelines.end(),
        [&](const SlotAttachmentTimeline& timeline) {
            return timeline.slot_index == slot_index;
        });
    if (it == slot_attachment_timelines.end()) {
        return nullptr;
    }

    return &(*it);
}

const SlotColorTimeline* AnimationData::find_color_timeline(std::size_t slot_index) const {
    const auto it = std::find_if(
        slot_color_timelines.begin(),
        slot_color_timelines.end(),
        [&](const SlotColorTimeline& timeline) {
            return timeline.slot_index == slot_index;
        });
    if (it == slot_color_timelines.end()) {
        return nullptr;
    }

    return &(*it);
}

const MeshDeformTimeline* AnimationData::find_deform_timeline(
    std::size_t slot_index,
    std::string_view attachment_name) const {
    const auto it = std::find_if(
        mesh_deform_timelines.begin(),
        mesh_deform_timelines.end(),
        [&](const MeshDeformTimeline& timeline) {
            return timeline.slot_index == slot_index &&
                timeline.attachment_name == attachment_name;
        });
    if (it == mesh_deform_timelines.end()) {
        return nullptr;
    }

    return &(*it);
}

const DrawOrderTimeline* AnimationData::find_draw_order_timeline() const {
    if (!draw_order_timeline_data.has_value()) {
        return nullptr;
    }

    return &(*draw_order_timeline_data);
}

const EventTimeline* AnimationData::find_event_timeline() const {
    if (!event_timeline_data.has_value()) {
        return nullptr;
    }

    return &(*event_timeline_data);
}

std::optional<double> AnimationData::sample_bone_rotation(
    std::size_t bone_index,
    double time,
    SamplingContext* context) const {
    prepare_sampling_context(context, time);
    const BoneRotateTimeline* timeline = find_rotate_timeline(bone_index);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    std::size_t* last_keyframe_index = nullptr;
    if (context != nullptr && !bone_rotate_timelines.empty()) {
        last_keyframe_index = cursor_at(
            &context->rotate_last_keyframe_indices,
            timeline_local_index(bone_rotate_timelines, *timeline));
    }

    return sample_rotate_timeline(*timeline, time, last_keyframe_index);
}

const InheritKeyframe* AnimationData::sample_bone_inherit(
    std::size_t bone_index,
    double time,
    SamplingContext* context) const {
    prepare_sampling_context(context, time);
    const BoneInheritTimeline* timeline = find_inherit_timeline(bone_index);
    if (timeline == nullptr) {
        return nullptr;
    }

    std::size_t* last_keyframe_index = nullptr;
    if (context != nullptr && !bone_inherit_timelines.empty()) {
        last_keyframe_index = cursor_at(
            &context->inherit_last_keyframe_indices,
            timeline_local_index(bone_inherit_timelines, *timeline));
    }

    return sample_inherit_timeline(*timeline, time, last_keyframe_index);
}

std::optional<VectorSample> AnimationData::sample_bone_translation(
    std::size_t bone_index,
    double time,
    SamplingContext* context) const {
    prepare_sampling_context(context, time);
    const BoneTranslateTimeline* timeline = find_translate_timeline(bone_index);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    std::size_t* last_keyframe_index = nullptr;
    if (context != nullptr && !bone_translate_timelines.empty()) {
        last_keyframe_index = cursor_at(
            &context->translate_last_keyframe_indices,
            timeline_local_index(bone_translate_timelines, *timeline));
    }

    return sample_translate_timeline(*timeline, time, last_keyframe_index);
}

std::optional<VectorSample> AnimationData::sample_bone_scale(
    std::size_t bone_index,
    double time,
    SamplingContext* context) const {
    prepare_sampling_context(context, time);
    const BoneScaleTimeline* timeline = find_scale_timeline(bone_index);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    std::size_t* last_keyframe_index = nullptr;
    if (context != nullptr && !bone_scale_timelines.empty()) {
        last_keyframe_index = cursor_at(
            &context->scale_last_keyframe_indices,
            timeline_local_index(bone_scale_timelines, *timeline));
    }

    return sample_scale_timeline(*timeline, time, last_keyframe_index);
}

std::optional<VectorSample> AnimationData::sample_bone_shear(
    std::size_t bone_index,
    double time,
    SamplingContext* context) const {
    prepare_sampling_context(context, time);
    const BoneShearTimeline* timeline = find_shear_timeline(bone_index);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    std::size_t* last_keyframe_index = nullptr;
    if (context != nullptr && !bone_shear_timelines.empty()) {
        last_keyframe_index = cursor_at(
            &context->shear_last_keyframe_indices,
            timeline_local_index(bone_shear_timelines, *timeline));
    }

    return sample_shear_timeline(*timeline, time, last_keyframe_index);
}

const AttachmentKeyframe* AnimationData::sample_slot_attachment(
    std::size_t slot_index,
    double time,
    SamplingContext* context) const {
    prepare_sampling_context(context, time);
    const SlotAttachmentTimeline* timeline = find_attachment_timeline(slot_index);
    if (timeline == nullptr) {
        return nullptr;
    }

    std::size_t* last_keyframe_index = nullptr;
    if (context != nullptr && !slot_attachment_timelines.empty()) {
        last_keyframe_index = cursor_at(
            &context->attachment_last_keyframe_indices,
            timeline_local_index(slot_attachment_timelines, *timeline));
    }

    return sample_attachment_timeline(*timeline, time, last_keyframe_index);
}

std::optional<SlotColor> AnimationData::sample_slot_color(
    std::size_t slot_index,
    double time,
    SamplingContext* context) const {
    prepare_sampling_context(context, time);
    const SlotColorTimeline* timeline = find_color_timeline(slot_index);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    std::size_t* last_keyframe_index = nullptr;
    if (context != nullptr && !slot_color_timelines.empty()) {
        last_keyframe_index = cursor_at(
            &context->color_last_keyframe_indices,
            timeline_local_index(slot_color_timelines, *timeline));
    }

    return sample_color_timeline(*timeline, time, last_keyframe_index);
}

std::optional<std::vector<double>> AnimationData::sample_slot_deform(
    std::size_t slot_index,
    std::string_view attachment_name,
    double time,
    SamplingContext* context) const {
    prepare_sampling_context(context, time);
    const MeshDeformTimeline* timeline = find_deform_timeline(slot_index, attachment_name);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    std::size_t* last_keyframe_index = nullptr;
    if (context != nullptr && !mesh_deform_timelines.empty()) {
        last_keyframe_index = cursor_at(
            &context->deform_last_keyframe_indices,
            timeline_local_index(mesh_deform_timelines, *timeline));
    }

    return sample_deform_timeline(*timeline, time, last_keyframe_index);
}

const DrawOrderKeyframe* AnimationData::sample_draw_order(
    double time,
    SamplingContext* context) const {
    prepare_sampling_context(context, time);
    const DrawOrderTimeline* timeline = find_draw_order_timeline();
    if (timeline == nullptr) {
        return nullptr;
    }

    return sample_draw_order_timeline(
        *timeline,
        time,
        context != nullptr ? &context->draw_order_last_keyframe_index : nullptr);
}

namespace {

template <typename Keyframe>
double timeline_duration(const std::vector<Keyframe>& keyframes) {
    if (keyframes.empty()) {
        return 0.0;
    }
    return keyframes.back().time;
}

} // namespace

double AnimationData::duration() const {
    double max_time = 0.0;
    for (const BoneRotateTimeline& timeline : bone_rotate_timelines) {
        max_time = std::max(max_time, timeline_duration(timeline.keyframes));
    }
    for (const BoneInheritTimeline& timeline : bone_inherit_timelines) {
        max_time = std::max(max_time, timeline_duration(timeline.keyframes));
    }
    for (const BoneTranslateTimeline& timeline : bone_translate_timelines) {
        max_time = std::max(max_time, timeline_duration(timeline.keyframes));
    }
    for (const BoneScaleTimeline& timeline : bone_scale_timelines) {
        max_time = std::max(max_time, timeline_duration(timeline.keyframes));
    }
    for (const BoneShearTimeline& timeline : bone_shear_timelines) {
        max_time = std::max(max_time, timeline_duration(timeline.keyframes));
    }
    for (const SlotAttachmentTimeline& timeline : slot_attachment_timelines) {
        max_time = std::max(max_time, timeline_duration(timeline.keyframes));
    }
    for (const SlotColorTimeline& timeline : slot_color_timelines) {
        max_time = std::max(max_time, timeline_duration(timeline.keyframes));
    }
    for (const MeshDeformTimeline& timeline : mesh_deform_timelines) {
        max_time = std::max(max_time, timeline_duration(timeline.keyframes));
    }
    if (draw_order_timeline_data.has_value()) {
        max_time = std::max(max_time, timeline_duration(draw_order_timeline_data->keyframes));
    }
    if (event_timeline_data.has_value()) {
        max_time = std::max(max_time, timeline_duration(event_timeline_data->keyframes));
    }

    return max_time;
}

std::optional<std::size_t> sample_sequence_frame(
    const AttachmentSequenceData& sequence,
    double time_seconds) {
    if (sequence.frame_regions.empty()) {
        return std::nullopt;
    }

    const std::size_t frame_count = sequence.frame_regions.size();
    const std::size_t setup_frame =
        detail::clamp_sequence_frame_index(sequence, sequence.setup_frame);
    if (frame_count == 1 || sequence.fps <= 0.0) {
        return setup_frame;
    }

    const long long advanced_frames = static_cast<long long>(
        std::floor(std::max(0.0, time_seconds) * sequence.fps));
    switch (sequence.playback_mode) {
    case SequencePlaybackMode::Hold:
        return std::min(
            setup_frame + static_cast<std::size_t>(advanced_frames),
            frame_count - 1);
    case SequencePlaybackMode::Once:
        return std::min(
            setup_frame + static_cast<std::size_t>(advanced_frames),
            frame_count - 1);
    case SequencePlaybackMode::Loop:
        return (setup_frame + (static_cast<std::size_t>(advanced_frames) % frame_count)) % frame_count;
    case SequencePlaybackMode::PingPong: {
        const std::size_t cycle_length = (frame_count - 1) * 2;
        const std::size_t cycle_position =
            (setup_frame + (static_cast<std::size_t>(advanced_frames) % cycle_length)) % cycle_length;
        return detail::map_ping_pong_frame(cycle_position, frame_count);
    }
    case SequencePlaybackMode::OnceReverse:
        if (advanced_frames >= static_cast<long long>(setup_frame)) {
            return 0;
        }
        return static_cast<std::size_t>(static_cast<long long>(setup_frame) - advanced_frames);
    case SequencePlaybackMode::LoopReverse:
        return detail::positive_mod(
            static_cast<long long>(setup_frame) -
                static_cast<long long>(static_cast<std::size_t>(advanced_frames) % frame_count),
            frame_count);
    case SequencePlaybackMode::PingPongReverse: {
        const std::size_t cycle_length = (frame_count - 1) * 2;
        const std::size_t start_position = detail::positive_mod(
            static_cast<long long>(cycle_length) - static_cast<long long>(setup_frame),
            cycle_length);
        const std::size_t cycle_position =
            (start_position + (static_cast<std::size_t>(advanced_frames) % cycle_length)) % cycle_length;
        return detail::map_ping_pong_frame(cycle_position, frame_count);
    }
    }

    return setup_frame;
}

void Skeleton::prepare_animation_pose() {
    reset_to_setup_pose_state(true);
}

void Skeleton::reset_to_setup_pose_state(bool reset_slots_and_draw_order) {
    if (bone_poses_.size() != data_->bones().size()) {
        bone_poses_.resize(data_->bones().size());
    }
    if (bone_world_a_.size() != data_->bones().size()) {
        bone_world_a_.resize(data_->bones().size());
        bone_world_b_.resize(data_->bones().size());
        bone_world_c_.resize(data_->bones().size());
        bone_world_d_.resize(data_->bones().size());
        bone_world_x_.resize(data_->bones().size());
        bone_world_y_.resize(data_->bones().size());
    }
    if (mesh_deform_states_.size() != data_->slots().size()) {
        mesh_deform_states_.resize(data_->slots().size());
    }

    for (std::size_t index = 0; index < data_->bones().size(); ++index) {
        bone_poses_[index].local_pose = data_->bones()[index].setup_pose;
        bone_poses_[index].inherit = data_->bones()[index].inherit;
    }
    for (MeshDeformState& deform_state : mesh_deform_states_) {
        deform_state.attachment_name.clear();
        deform_state.vertex_offsets.clear();
    }

    if (reset_slots_and_draw_order) {
        apply_setup_attachments();
        if (draw_order_.size() != data_->slots().size()) {
            draw_order_.resize(data_->slots().size());
        }
        for (std::size_t index = 0; index < data_->slots().size(); ++index) {
            draw_order_[index] = index;
        }
    }
}

void Skeleton::set_to_setup_pose() {
    reset_to_setup_pose_state(true);
    reset_physics_state();
    update_world_transforms();
    reset_update_throttle_state();
}

void Skeleton::apply_animation(const AnimationData& animation, double time) {
    apply_animation(animation, time, time, AnimationEventCallback{});
}

void Skeleton::apply_animation(
    const AnimationData& animation,
    double previous_time,
    double time,
    const AnimationEventCallback& event_callback) {
    if (!visible_) {
        return;
    }

    reset_to_setup_pose_state(false);
    apply_setup_attachments();
    if (draw_order_.size() != data_->slots().size()) {
        draw_order_.resize(data_->slots().size());
    }
    std::iota(draw_order_.begin(), draw_order_.end(), 0);

    SamplingContext& sampling_context = standalone_sampling_context(animation, time);

    for (const std::size_t bone_index : animation.targeted_bone_indices) {
        BoneTransform& pose = bone_poses_[bone_index].local_pose;
        BoneInherit& inherit = bone_poses_[bone_index].inherit;

        if (const std::optional<double> rotation =
                animation.sample_bone_rotation(bone_index, time, &sampling_context)) {
            pose.rotation = *rotation;
        }
        if (const InheritKeyframe* keyframe =
                animation.sample_bone_inherit(bone_index, time, &sampling_context)) {
            inherit = keyframe->inherit;
        }
        if (const std::optional<VectorSample> translation =
                animation.sample_bone_translation(bone_index, time, &sampling_context)) {
            pose.x = translation->x;
            pose.y = translation->y;
        }
        if (const std::optional<VectorSample> scale =
                animation.sample_bone_scale(bone_index, time, &sampling_context)) {
            pose.scale_x = scale->x;
            pose.scale_y = scale->y;
        }
        if (const std::optional<VectorSample> shear =
                animation.sample_bone_shear(bone_index, time, &sampling_context)) {
            pose.shear_x = shear->x;
            pose.shear_y = shear->y;
        }
    }

    for (std::size_t timeline_index = 0; timeline_index < animation.slot_attachment_timelines.size();
         ++timeline_index) {
        const SlotAttachmentTimeline& timeline = animation.slot_attachment_timelines[timeline_index];
        if (timeline.slot_index >= slot_states_.size()) {
            continue;
        }

        const AttachmentKeyframe* keyframe = sample_attachment_timeline(
            timeline,
            time,
            cursor_at(&sampling_context.attachment_last_keyframe_indices, timeline_index));
        if (keyframe == nullptr) {
            continue;
        }

        apply_slot_attachment_keyframe(timeline.slot_index, keyframe->attachment_name);
    }

    for (std::size_t timeline_index = 0; timeline_index < animation.slot_color_timelines.size();
         ++timeline_index) {
        const SlotColorTimeline& timeline = animation.slot_color_timelines[timeline_index];
        if (timeline.slot_index >= slot_states_.size()) {
            continue;
        }

        if (const std::optional<SlotColor> color = sample_color_timeline(
                timeline,
                time,
                cursor_at(&sampling_context.color_last_keyframe_indices, timeline_index))) {
            slot_states_[timeline.slot_index].color = *color;
        }
    }

    for (std::size_t timeline_index = 0; timeline_index < animation.mesh_deform_timelines.size();
         ++timeline_index) {
        const MeshDeformTimeline& timeline = animation.mesh_deform_timelines[timeline_index];
        if (timeline.slot_index >= mesh_deform_states_.size()) {
            continue;
        }

        const AttachmentData* attachment = current_attachment(timeline.slot_index);
        if (attachment == nullptr ||
            !detail::attachment_matches_mesh_deform_source(*attachment, timeline.attachment_name)) {
            continue;
        }

        const std::optional<std::vector<double>> vertex_offsets =
            sample_deform_timeline(
                timeline,
                time,
                cursor_at(&sampling_context.deform_last_keyframe_indices, timeline_index));
        if (!vertex_offsets.has_value()) {
            continue;
        }

        mesh_deform_states_[timeline.slot_index].attachment_name = timeline.attachment_name;
        mesh_deform_states_[timeline.slot_index].vertex_offsets = *vertex_offsets;
    }

    if (const DrawOrderKeyframe* draw_order_keyframe =
            animation.sample_draw_order(time, &sampling_context)) {
        draw_order_ = draw_order_keyframe->slot_indices;
    }

    if (event_callback && time > previous_time) {
        if (const EventTimeline* event_timeline = animation.find_event_timeline()) {
            for (const EventKeyframe& keyframe : event_timeline->keyframes) {
                if (keyframe.time <= previous_time || keyframe.time > time) {
                    continue;
                }
                if (keyframe.event_index >= data_->events().size()) {
                    continue;
                }

                const EventDefinition& definition = data_->events()[keyframe.event_index];
                AnimationEvent event;
                event.event_index = keyframe.event_index;
                event.name = definition.name;
                event.time = keyframe.time;
                event.int_value = keyframe.int_value.value_or(definition.int_value);
                event.float_value = keyframe.float_value.value_or(definition.float_value);
                event.string_value = keyframe.string_value.value_or(definition.string_value);
                event.audio_path = keyframe.audio_path.has_value()
                    ? keyframe.audio_path
                    : definition.audio_path;
                event.volume = keyframe.volume.value_or(definition.volume);
                event.balance = keyframe.balance.value_or(definition.balance);
                event_callback(event);
            }
        }
    }

    update_world_transforms();
    reset_update_throttle_state();
}

void Skeleton::set_attachment_playback_time(double time_seconds) {
    attachment_playback_time_ = std::max(0.0, time_seconds);
}

void Skeleton::advance_attachment_playback(double delta_seconds) {
    attachment_playback_time_ = std::max(0.0, attachment_playback_time_ + delta_seconds);
}

double Skeleton::attachment_playback_time() const {
    return attachment_playback_time_;
}

} // namespace marrow::runtime
