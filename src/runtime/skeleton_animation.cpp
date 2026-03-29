#include "skeleton_internal.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace marrow::runtime {

const BoneRotateTimeline* AnimationData::find_rotate_timeline(std::size_t bone_index) const {
    const auto it = std::find_if(
        bone_rotate_timelines.begin(),
        bone_rotate_timelines.end(),
        [&](const BoneRotateTimeline& timeline) {
            return timeline.bone_index == bone_index;
        });
    if (it == bone_rotate_timelines.end()) {
        return nullptr;
    }

    return &(*it);
}

const BoneInheritTimeline* AnimationData::find_inherit_timeline(std::size_t bone_index) const {
    const auto it = std::find_if(
        bone_inherit_timelines.begin(),
        bone_inherit_timelines.end(),
        [&](const BoneInheritTimeline& timeline) {
            return timeline.bone_index == bone_index;
        });
    if (it == bone_inherit_timelines.end()) {
        return nullptr;
    }

    return &(*it);
}

const BoneTranslateTimeline* AnimationData::find_translate_timeline(std::size_t bone_index) const {
    const auto it = std::find_if(
        bone_translate_timelines.begin(),
        bone_translate_timelines.end(),
        [&](const BoneTranslateTimeline& timeline) {
            return timeline.bone_index == bone_index;
        });
    if (it == bone_translate_timelines.end()) {
        return nullptr;
    }

    return &(*it);
}

const BoneScaleTimeline* AnimationData::find_scale_timeline(std::size_t bone_index) const {
    const auto it = std::find_if(
        bone_scale_timelines.begin(),
        bone_scale_timelines.end(),
        [&](const BoneScaleTimeline& timeline) {
            return timeline.bone_index == bone_index;
        });
    if (it == bone_scale_timelines.end()) {
        return nullptr;
    }

    return &(*it);
}

const BoneShearTimeline* AnimationData::find_shear_timeline(std::size_t bone_index) const {
    const auto it = std::find_if(
        bone_shear_timelines.begin(),
        bone_shear_timelines.end(),
        [&](const BoneShearTimeline& timeline) {
            return timeline.bone_index == bone_index;
        });
    if (it == bone_shear_timelines.end()) {
        return nullptr;
    }

    return &(*it);
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

std::optional<double> AnimationData::sample_bone_rotation(std::size_t bone_index, double time) const {
    const BoneRotateTimeline* timeline = find_rotate_timeline(bone_index);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    return sample_rotate_timeline(*timeline, time);
}

const InheritKeyframe* AnimationData::sample_bone_inherit(
    std::size_t bone_index,
    double time) const {
    const BoneInheritTimeline* timeline = find_inherit_timeline(bone_index);
    if (timeline == nullptr) {
        return nullptr;
    }

    return sample_inherit_timeline(*timeline, time);
}

std::optional<VectorSample> AnimationData::sample_bone_translation(
    std::size_t bone_index,
    double time) const {
    const BoneTranslateTimeline* timeline = find_translate_timeline(bone_index);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    return sample_translate_timeline(*timeline, time);
}

std::optional<VectorSample> AnimationData::sample_bone_scale(
    std::size_t bone_index,
    double time) const {
    const BoneScaleTimeline* timeline = find_scale_timeline(bone_index);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    return sample_scale_timeline(*timeline, time);
}

std::optional<VectorSample> AnimationData::sample_bone_shear(
    std::size_t bone_index,
    double time) const {
    const BoneShearTimeline* timeline = find_shear_timeline(bone_index);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    return sample_shear_timeline(*timeline, time);
}

const AttachmentKeyframe* AnimationData::sample_slot_attachment(
    std::size_t slot_index,
    double time) const {
    const SlotAttachmentTimeline* timeline = find_attachment_timeline(slot_index);
    if (timeline == nullptr) {
        return nullptr;
    }

    return sample_attachment_timeline(*timeline, time);
}

std::optional<SlotColor> AnimationData::sample_slot_color(
    std::size_t slot_index,
    double time) const {
    const SlotColorTimeline* timeline = find_color_timeline(slot_index);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    return sample_color_timeline(*timeline, time);
}

std::optional<std::vector<double>> AnimationData::sample_slot_deform(
    std::size_t slot_index,
    std::string_view attachment_name,
    double time) const {
    const MeshDeformTimeline* timeline = find_deform_timeline(slot_index, attachment_name);
    if (timeline == nullptr) {
        return std::nullopt;
    }

    return sample_deform_timeline(*timeline, time);
}

const DrawOrderKeyframe* AnimationData::sample_draw_order(double time) const {
    const DrawOrderTimeline* timeline = find_draw_order_timeline();
    if (timeline == nullptr) {
        return nullptr;
    }

    return sample_draw_order_timeline(*timeline, time);
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

    for (const std::size_t bone_index : animation.targeted_bone_indices) {
        BoneTransform& pose = bone_poses_[bone_index].local_pose;
        BoneInherit& inherit = bone_poses_[bone_index].inherit;

        if (const std::optional<double> rotation = animation.sample_bone_rotation(bone_index, time)) {
            pose.rotation = *rotation;
        }
        if (const InheritKeyframe* keyframe = animation.sample_bone_inherit(bone_index, time)) {
            inherit = keyframe->inherit;
        }
        if (const std::optional<VectorSample> translation =
                animation.sample_bone_translation(bone_index, time)) {
            pose.x = translation->x;
            pose.y = translation->y;
        }
        if (const std::optional<VectorSample> scale = animation.sample_bone_scale(bone_index, time)) {
            pose.scale_x = scale->x;
            pose.scale_y = scale->y;
        }
        if (const std::optional<VectorSample> shear = animation.sample_bone_shear(bone_index, time)) {
            pose.shear_x = shear->x;
            pose.shear_y = shear->y;
        }
    }

    for (const SlotAttachmentTimeline& timeline : animation.slot_attachment_timelines) {
        if (timeline.slot_index >= slot_states_.size()) {
            continue;
        }

        const AttachmentKeyframe* keyframe = sample_attachment_timeline(timeline, time);
        if (keyframe == nullptr) {
            continue;
        }

        apply_slot_attachment_keyframe(timeline.slot_index, keyframe->attachment_name);
    }

    for (const SlotColorTimeline& timeline : animation.slot_color_timelines) {
        if (timeline.slot_index >= slot_states_.size()) {
            continue;
        }

        if (const std::optional<SlotColor> color = sample_color_timeline(timeline, time)) {
            slot_states_[timeline.slot_index].color = *color;
        }
    }

    for (const MeshDeformTimeline& timeline : animation.mesh_deform_timelines) {
        if (timeline.slot_index >= mesh_deform_states_.size()) {
            continue;
        }

        const AttachmentData* attachment = current_attachment(timeline.slot_index);
        if (attachment == nullptr ||
            !detail::attachment_matches_mesh_deform_source(*attachment, timeline.attachment_name)) {
            continue;
        }

        const std::optional<std::vector<double>> vertex_offsets =
            sample_deform_timeline(timeline, time);
        if (!vertex_offsets.has_value()) {
            continue;
        }

        mesh_deform_states_[timeline.slot_index].attachment_name = timeline.attachment_name;
        mesh_deform_states_[timeline.slot_index].vertex_offsets = *vertex_offsets;
    }

    if (const DrawOrderKeyframe* draw_order_keyframe = animation.sample_draw_order(time)) {
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
