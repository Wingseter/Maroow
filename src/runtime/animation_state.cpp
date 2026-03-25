#include "marrow/runtime/animation_state.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace marrow::runtime {

namespace {

constexpr double kEpsilon = 1e-6;

double clamp_unit(double value) {
    return std::clamp(value, 0.0, 1.0);
}

double entry_transition_time(const TrackEntry& entry) {
    if (entry.is_empty) {
        return entry.mix_duration;
    }

    return entry.animation_duration();
}

double sample_time_for_pose(const TrackEntry& entry, double track_time) {
    if (entry.animation == nullptr || entry.is_empty) {
        return 0.0;
    }

    const double duration = entry.animation_duration();
    if (duration <= 0.0) {
        return 0.0;
    }

    if (entry.loop) {
        double sample_time = std::fmod(track_time, duration);
        if (sample_time < 0.0) {
            sample_time += duration;
        }

        if (!entry.reverse) {
            return sample_time;
        }

        if (track_time <= 0.0) {
            return duration;
        }
        if (std::abs(sample_time) <= kEpsilon) {
            return 0.0;
        }

        return duration - sample_time;
    }

    const double clamped_time = std::clamp(track_time, 0.0, duration);
    if (entry.reverse) {
        return duration - clamped_time;
    }

    return clamped_time;
}

double blend_scalar(double current, double base, double sample, double weight) {
    return current + (sample - base) * weight;
}

bool attachment_matches_mesh_deform_source(
    const AttachmentData& attachment,
    std::string_view attachment_name) {
    if (attachment.name == attachment_name) {
        return true;
    }

    return attachment.linked_mesh.has_value() &&
        attachment.linked_mesh->deform &&
        attachment.linked_mesh->parent_attachment == attachment_name;
}

bool has_listener(
    const AnimationStateListener& state_listener,
    const AnimationStateListener& entry_listener) {
    return static_cast<bool>(state_listener) || static_cast<bool>(entry_listener);
}

AnimationEvent resolve_event(
    const SkeletonData& data,
    const EventKeyframe& keyframe) {
    const EventDefinition& definition = data.events()[keyframe.event_index];

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
    return event;
}

VectorSample sample_bone_translation_or_setup(
    const SkeletonData& data,
    const AnimationData& animation,
    std::size_t bone_index,
    double sample_time) {
    if (bone_index < data.bones().size()) {
        if (const std::optional<VectorSample> translation =
                animation.sample_bone_translation(bone_index, sample_time)) {
            return *translation;
        }

        const BoneTransform& setup_pose = data.bones()[bone_index].setup_pose;
        return VectorSample{setup_pose.x, setup_pose.y};
    }

    return {};
}

} // namespace

double TrackEntry::animation_duration() const {
    if (animation == nullptr || is_empty) {
        return 0.0;
    }

    return animation->duration();
}

double TrackEntry::animation_time() const {
    return sample_time_for_pose(*this, track_time);
}

bool TrackEntry::is_complete() const {
    if (loop || animation == nullptr || is_empty) {
        return false;
    }

    return track_time >= animation_duration();
}

void TrackEntry::set_listener(AnimationStateListener value) {
    listener = std::move(value);
    if (listener && owner_ != nullptr && has_started_ && !start_notified_to_entry_) {
        listener(*owner_, AnimationStateEventType::Start, shared_from_this(), nullptr);
        start_notified_to_entry_ = true;
    }
}

AnimationState::AnimationState(std::shared_ptr<const SkeletonData> data)
    : data_(std::move(data)) {
    if (data_ == nullptr) {
        throw std::invalid_argument("AnimationState requires SkeletonData");
    }
}

const std::shared_ptr<const SkeletonData>& AnimationState::data() const {
    return data_;
}

std::shared_ptr<TrackEntry> AnimationState::get_current(std::size_t track_index) const {
    if (track_index >= tracks_.size()) {
        return nullptr;
    }

    return tracks_[track_index];
}

void AnimationState::ensure_track(std::size_t track_index) {
    if (track_index >= tracks_.size()) {
        tracks_.resize(track_index + 1);
    }
}

std::shared_ptr<TrackEntry> AnimationState::make_animation_entry(
    std::size_t track_index,
    std::string_view animation_name,
    bool loop) const {
    const AnimationData* animation = data_->find_animation(animation_name);
    if (animation == nullptr) {
        throw std::invalid_argument(
            "AnimationState requested unknown animation '" +
            std::string(animation_name) + "'");
    }

    auto entry = std::make_shared<TrackEntry>();
    entry->owner_ = const_cast<AnimationState*>(this);
    entry->track_index = track_index;
    entry->animation = animation;
    entry->animation_name = std::string(animation_name);
    entry->loop = loop;
    return entry;
}

std::shared_ptr<TrackEntry> AnimationState::make_empty_entry(
    std::size_t track_index,
    double mix_duration) const {
    auto entry = std::make_shared<TrackEntry>();
    entry->owner_ = const_cast<AnimationState*>(this);
    entry->track_index = track_index;
    entry->animation_name = "<empty>";
    entry->is_empty = true;
    entry->mix_duration = std::max(0.0, mix_duration);
    return entry;
}

double AnimationState::resolve_mix_duration(
    const std::shared_ptr<TrackEntry>& from,
    std::string_view to_animation,
    std::optional<double> explicit_mix_duration) const {
    if (explicit_mix_duration.has_value()) {
        return std::max(0.0, *explicit_mix_duration);
    }

    if (from == nullptr || from->is_empty || from->animation_name.empty()) {
        return 0.0;
    }

    return std::max(0.0, data_->mix_duration(from->animation_name, to_animation));
}

void AnimationState::replace_current(
    std::size_t track_index,
    const std::shared_ptr<TrackEntry>& entry,
    bool interrupt_current) {
    ensure_track(track_index);

    std::shared_ptr<TrackEntry> current = tracks_[track_index];
    if (current != nullptr) {
        dispose_queued_entries(current->next);
        current->next.reset();

        if (interrupt_current) {
            dispatch(AnimationStateEventType::Interrupt, current);
        }

        if (entry->mix_duration > kEpsilon) {
            entry->mixing_from = current;
            entry->mix_time = 0.0;
        } else {
            dispose_active_entry(current, true);
        }
    }

    tracks_[track_index] = entry;
    dispatch(AnimationStateEventType::Start, entry);
}

std::shared_ptr<TrackEntry> AnimationState::set_animation(
    std::size_t track_index,
    std::string_view animation_name,
    bool loop,
    std::optional<double> mix_duration) {
    auto entry = make_animation_entry(track_index, animation_name, loop);
    entry->mix_duration = resolve_mix_duration(get_current(track_index), animation_name, mix_duration);
    replace_current(track_index, entry, true);
    return entry;
}

std::shared_ptr<TrackEntry> AnimationState::add_animation(
    std::size_t track_index,
    std::string_view animation_name,
    bool loop,
    double delay,
    std::optional<double> mix_duration) {
    auto entry = make_animation_entry(track_index, animation_name, loop);
    entry->delay = std::max(0.0, delay);

    ensure_track(track_index);
    std::shared_ptr<TrackEntry> current = tracks_[track_index];
    if (current == nullptr) {
        entry->mix_duration = 0.0;
        tracks_[track_index] = entry;
        dispatch(AnimationStateEventType::Start, entry);
        return entry;
    }

    while (current->next != nullptr) {
        current = current->next;
    }

    entry->mix_duration = resolve_mix_duration(current, animation_name, mix_duration);
    current->next = entry;
    return entry;
}

std::shared_ptr<TrackEntry> AnimationState::set_empty_animation(
    std::size_t track_index,
    double mix_duration) {
    auto entry = make_empty_entry(track_index, mix_duration);
    replace_current(track_index, entry, true);
    return entry;
}

std::shared_ptr<TrackEntry> AnimationState::add_empty_animation(
    std::size_t track_index,
    double mix_duration,
    double delay) {
    auto entry = make_empty_entry(track_index, mix_duration);
    entry->delay = std::max(0.0, delay);

    ensure_track(track_index);
    std::shared_ptr<TrackEntry> current = tracks_[track_index];
    if (current == nullptr) {
        tracks_[track_index] = entry;
        dispatch(AnimationStateEventType::Start, entry);
        return entry;
    }

    while (current->next != nullptr) {
        current = current->next;
    }

    current->next = entry;
    return entry;
}

void AnimationState::clear_track(std::size_t track_index) {
    if (track_index >= tracks_.size() || tracks_[track_index] == nullptr) {
        return;
    }

    dispose_active_entry(tracks_[track_index], true);
    tracks_[track_index].reset();
}

void AnimationState::clear_tracks() {
    for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        clear_track(track_index);
    }
}

void AnimationState::dispatch_complete_callbacks(
    const std::shared_ptr<TrackEntry>& entry,
    double previous_time,
    double current_time) {
    if (entry == nullptr || entry->animation == nullptr || entry->is_empty) {
        return;
    }

    const double duration = entry->animation_duration();
    if (duration <= 0.0) {
        return;
    }

    if (entry->loop) {
        const auto previous_loops = static_cast<long long>(std::floor(previous_time / duration));
        const auto current_loops = static_cast<long long>(std::floor(current_time / duration));
        for (long long loop_index = previous_loops; loop_index < current_loops; ++loop_index) {
            dispatch(AnimationStateEventType::Complete, entry);
        }
        return;
    }

    if (previous_time < duration && current_time >= duration) {
        dispatch(AnimationStateEventType::Complete, entry);
    }
}

void AnimationState::dispatch_event_range(
    const std::shared_ptr<TrackEntry>& entry,
    double start_time,
    double end_time) {
    if (entry == nullptr || entry->animation == nullptr || entry->is_empty) {
        return;
    }

    const EventTimeline* timeline = entry->animation->find_event_timeline();
    if (timeline == nullptr) {
        return;
    }

    if (end_time >= start_time) {
        for (const EventKeyframe& keyframe : timeline->keyframes) {
            if (keyframe.time <= start_time || keyframe.time > end_time) {
                continue;
            }
            if (keyframe.event_index >= data_->events().size()) {
                continue;
            }

            const AnimationEvent event = resolve_event(*data_, keyframe);
            dispatch(AnimationStateEventType::Event, entry, &event);
        }
        return;
    }

    for (auto it = timeline->keyframes.rbegin(); it != timeline->keyframes.rend(); ++it) {
        if (it->time >= start_time || it->time < end_time) {
            continue;
        }
        if (it->event_index >= data_->events().size()) {
            continue;
        }

        const AnimationEvent event = resolve_event(*data_, *it);
        dispatch(AnimationStateEventType::Event, entry, &event);
    }
}

void AnimationState::dispatch_event_callbacks(
    const std::shared_ptr<TrackEntry>& entry,
    double previous_time,
    double current_time) {
    if (entry == nullptr || entry->animation == nullptr || entry->is_empty ||
        current_time <= previous_time ||
        !has_listener(listener_, entry->listener)) {
        return;
    }

    const double duration = entry->animation_duration();
    if (duration <= 0.0) {
        return;
    }

    if (entry->loop) {
        long long start_loop = static_cast<long long>(std::floor(previous_time / duration));
        const long long end_loop = static_cast<long long>(std::floor(current_time / duration));
        double local_start = previous_time - static_cast<double>(start_loop) * duration;

        if (start_loop == end_loop) {
            const double local_end = current_time - static_cast<double>(end_loop) * duration;
            if (entry->reverse) {
                dispatch_event_range(entry, duration - local_start, duration - local_end);
            } else {
                dispatch_event_range(entry, local_start, local_end);
            }
            return;
        }

        if (entry->reverse) {
            dispatch_event_range(entry, duration - local_start, 0.0);
            for (long long loop_index = start_loop + 1; loop_index < end_loop; ++loop_index) {
                (void)loop_index;
                dispatch_event_range(entry, duration, 0.0);
            }
            dispatch_event_range(
                entry,
                duration,
                duration - (current_time - static_cast<double>(end_loop) * duration));
        } else {
            dispatch_event_range(entry, local_start, duration);
            for (long long loop_index = start_loop + 1; loop_index < end_loop; ++loop_index) {
                (void)loop_index;
                dispatch_event_range(entry, 0.0, duration);
            }
            dispatch_event_range(
                entry,
                0.0,
                current_time - static_cast<double>(end_loop) * duration);
        }
        return;
    }

    const double clamped_previous = std::clamp(previous_time, 0.0, duration);
    const double clamped_current = std::clamp(current_time, 0.0, duration);
    if (entry->reverse) {
        dispatch_event_range(
            entry,
            duration - clamped_previous,
            duration - clamped_current);
    } else {
        dispatch_event_range(entry, clamped_previous, clamped_current);
    }
}

void AnimationState::advance_entry(
    const std::shared_ptr<TrackEntry>& entry,
    double delta) {
    if (entry == nullptr) {
        return;
    }

    if (entry->mixing_from != nullptr) {
        advance_entry(entry->mixing_from, delta);
    }

    const double previous_time = entry->track_time;
    entry->last_track_time_ = previous_time;
    entry->track_time += delta;
    if (entry->mixing_from != nullptr || entry->is_empty) {
        entry->mix_time += delta;
    }

    dispatch_event_callbacks(entry, previous_time, entry->track_time);
    dispatch_complete_callbacks(entry, previous_time, entry->track_time);
}

void AnimationState::dispose_queued_entries(const std::shared_ptr<TrackEntry>& entry) {
    if (entry == nullptr) {
        return;
    }

    dispose_queued_entries(entry->next);
    if (entry->mixing_from != nullptr) {
        dispose_active_entry(entry->mixing_from, true);
    }
    dispatch(AnimationStateEventType::Dispose, entry);
}

void AnimationState::dispose_active_entry(
    const std::shared_ptr<TrackEntry>& entry,
    bool dispatch_end) {
    if (entry == nullptr) {
        return;
    }

    dispose_queued_entries(entry->next);
    if (entry->mixing_from != nullptr) {
        dispose_active_entry(entry->mixing_from, dispatch_end);
    }
    if (dispatch_end) {
        dispatch(AnimationStateEventType::End, entry);
    }
    dispatch(AnimationStateEventType::Dispose, entry);
}

void AnimationState::prune_mixing_from(const std::shared_ptr<TrackEntry>& entry) {
    if (entry == nullptr || entry->mixing_from == nullptr) {
        return;
    }

    if (entry->mix_duration <= kEpsilon || entry->mix_time >= entry->mix_duration - kEpsilon) {
        dispose_active_entry(entry->mixing_from, true);
        entry->mixing_from.reset();
        entry->mix_time = std::max(entry->mix_time, entry->mix_duration);
    }
}

void AnimationState::start_next_entry(std::size_t track_index) {
    std::shared_ptr<TrackEntry> current = tracks_[track_index];
    if (current == nullptr || current->next == nullptr) {
        return;
    }

    const double start_time = entry_transition_time(*current) + current->next->delay;
    const double carry = std::max(0.0, current->track_time - start_time);
    std::shared_ptr<TrackEntry> next = current->next;
    current->next.reset();

    dispatch(AnimationStateEventType::Interrupt, current);

    if (next->mix_duration > kEpsilon) {
        next->mixing_from = current;
        next->mix_time = carry;
    } else {
        dispose_active_entry(current, true);
    }

    next->last_track_time_ = 0.0;
    next->track_time = carry;
    tracks_[track_index] = next;
    dispatch(AnimationStateEventType::Start, next);
    prune_mixing_from(next);
}

void AnimationState::update(double delta) {
    if (delta < 0.0) {
        throw std::invalid_argument("AnimationState::update requires a non-negative delta");
    }

    for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        std::shared_ptr<TrackEntry> current = tracks_[track_index];
        if (current == nullptr) {
            continue;
        }

        advance_entry(current, delta);
        prune_mixing_from(current);

        current = tracks_[track_index];
        while (current != nullptr &&
               current->next != nullptr &&
               current->track_time >= entry_transition_time(*current) + current->next->delay) {
            start_next_entry(track_index);
            current = tracks_[track_index];
        }

        current = tracks_[track_index];
        if (current != nullptr &&
            current->is_empty &&
            current->mixing_from == nullptr &&
            current->mix_time >= current->mix_duration - kEpsilon) {
            dispose_active_entry(current, true);
            tracks_[track_index].reset();
        }
    }
}

void AnimationState::collect_contributions(
    const std::shared_ptr<TrackEntry>& entry,
    double alpha,
    std::vector<Contribution>* contributions) const {
    if (entry == nullptr || contributions == nullptr) {
        return;
    }

    double weighted_alpha = clamp_unit(alpha * entry->alpha);
    if (weighted_alpha <= kEpsilon) {
        return;
    }

    if (entry->mixing_from != nullptr) {
        const double mix_alpha =
            entry->mix_duration <= kEpsilon
                ? 1.0
                : clamp_unit(entry->mix_time / entry->mix_duration);
        collect_contributions(entry->mixing_from, weighted_alpha * (1.0 - mix_alpha), contributions);
        weighted_alpha *= mix_alpha;
    }

    if (entry->animation == nullptr || entry->is_empty || weighted_alpha <= kEpsilon) {
        return;
    }

    contributions->push_back(Contribution{entry, weighted_alpha});
}

void AnimationState::apply_contribution(
    Skeleton& skeleton,
    const TrackEntry& entry,
    double weight,
    const std::vector<BonePose>& base_bones,
    const std::vector<SlotState>& base_slots,
    const std::vector<MeshDeformState>& base_mesh_deforms) {
    auto& bones = skeleton.bone_poses();
    auto& slots = skeleton.slot_states();
    auto& mesh_deforms = skeleton.mesh_deform_states();
    const double sample_time = entry.animation_time();

    for (const std::size_t bone_index : entry.animation->targeted_bone_indices) {
        if (bone_index >= bones.size() || bone_index >= base_bones.size()) {
            continue;
        }

        BoneTransform& current_pose = bones[bone_index].local_pose;
        const BoneTransform& base_pose = base_bones[bone_index].local_pose;

        if (const std::optional<double> rotation =
                entry.animation->sample_bone_rotation(bone_index, sample_time)) {
            current_pose.rotation =
                blend_scalar(current_pose.rotation, base_pose.rotation, *rotation, weight);
        }
        if (const std::optional<VectorSample> translation =
                entry.animation->sample_bone_translation(bone_index, sample_time)) {
            current_pose.x = blend_scalar(current_pose.x, base_pose.x, translation->x, weight);
            current_pose.y = blend_scalar(current_pose.y, base_pose.y, translation->y, weight);
        }
        if (const std::optional<VectorSample> scale =
                entry.animation->sample_bone_scale(bone_index, sample_time)) {
            current_pose.scale_x =
                blend_scalar(current_pose.scale_x, base_pose.scale_x, scale->x, weight);
            current_pose.scale_y =
                blend_scalar(current_pose.scale_y, base_pose.scale_y, scale->y, weight);
        }
        if (const std::optional<VectorSample> shear =
                entry.animation->sample_bone_shear(bone_index, sample_time)) {
            current_pose.shear_x =
                blend_scalar(current_pose.shear_x, base_pose.shear_x, shear->x, weight);
            current_pose.shear_y =
                blend_scalar(current_pose.shear_y, base_pose.shear_y, shear->y, weight);
        }
    }

    for (const SlotColorTimeline& timeline : entry.animation->slot_color_timelines) {
        if (timeline.slot_index >= slots.size() || timeline.slot_index >= base_slots.size()) {
            continue;
        }

        if (const std::optional<SlotColor> color =
                entry.animation->sample_slot_color(timeline.slot_index, sample_time)) {
            SlotColor& current_color = slots[timeline.slot_index].color;
            const SlotColor& base_color = base_slots[timeline.slot_index].color;
            current_color.r = blend_scalar(current_color.r, base_color.r, color->r, weight);
            current_color.g = blend_scalar(current_color.g, base_color.g, color->g, weight);
            current_color.b = blend_scalar(current_color.b, base_color.b, color->b, weight);
            current_color.a = blend_scalar(current_color.a, base_color.a, color->a, weight);
        }
    }

    for (const MeshDeformTimeline& timeline : entry.animation->mesh_deform_timelines) {
        if (timeline.slot_index >= mesh_deforms.size() ||
            timeline.slot_index >= base_mesh_deforms.size()) {
            continue;
        }

        const AttachmentData* attachment = skeleton.current_attachment(timeline.slot_index);
        if (attachment == nullptr || attachment->mesh_geometry == nullptr ||
            !attachment_matches_mesh_deform_source(*attachment, timeline.attachment_name)) {
            continue;
        }

        const std::optional<std::vector<double>> sampled_offsets =
            entry.animation->sample_slot_deform(
                timeline.slot_index,
                timeline.attachment_name,
                sample_time);
        if (!sampled_offsets.has_value()) {
            continue;
        }

        MeshDeformState& current_deform = mesh_deforms[timeline.slot_index];
        const MeshDeformState& base_deform = base_mesh_deforms[timeline.slot_index];
        if (current_deform.attachment_name != timeline.attachment_name ||
            current_deform.vertex_offsets.size() != sampled_offsets->size()) {
            current_deform.attachment_name = timeline.attachment_name;
            current_deform.vertex_offsets.assign(sampled_offsets->size(), 0.0);
        }

        const bool base_matches_timeline =
            base_deform.attachment_name == timeline.attachment_name &&
            base_deform.vertex_offsets.size() == sampled_offsets->size();
        for (std::size_t component_index = 0;
             component_index < sampled_offsets->size();
             ++component_index) {
            const double base_value =
                base_matches_timeline ? base_deform.vertex_offsets[component_index] : 0.0;
            current_deform.vertex_offsets[component_index] = blend_scalar(
                current_deform.vertex_offsets[component_index],
                base_value,
                (*sampled_offsets)[component_index],
                weight);
        }
    }
}

void AnimationState::apply_discrete_timelines(
    Skeleton& skeleton,
    const TrackEntry& entry) const {
    auto& bones = skeleton.bone_poses();
    auto& slots = skeleton.slot_states();
    auto& draw_order = skeleton.draw_order();
    const double sample_time = entry.animation_time();

    for (const BoneInheritTimeline& timeline : entry.animation->bone_inherit_timelines) {
        if (timeline.bone_index >= bones.size()) {
            continue;
        }

        const InheritKeyframe* keyframe =
            entry.animation->sample_bone_inherit(timeline.bone_index, sample_time);
        if (keyframe == nullptr) {
            continue;
        }

        bones[timeline.bone_index].inherit = keyframe->flags;
    }

    for (const SlotAttachmentTimeline& timeline : entry.animation->slot_attachment_timelines) {
        if (timeline.slot_index >= slots.size()) {
            continue;
        }

        const AttachmentKeyframe* keyframe =
            entry.animation->sample_slot_attachment(timeline.slot_index, sample_time);
        if (keyframe == nullptr) {
            continue;
        }

        slots[timeline.slot_index].attachment_name =
            keyframe->attachment_name.value_or(std::string{});
        slots[timeline.slot_index].attachment_skin_index.reset();
        if (keyframe->attachment_name.has_value()) {
            std::optional<std::size_t> attachment_skin_index;
            skeleton.data()->find_attachment_source(
                timeline.slot_index,
                *keyframe->attachment_name,
                &attachment_skin_index);
            slots[timeline.slot_index].attachment_skin_index = attachment_skin_index;
        }
    }

    if (const DrawOrderKeyframe* draw_order_keyframe =
            entry.animation->sample_draw_order(sample_time)) {
        draw_order = draw_order_keyframe->slot_indices;
    }
}

void AnimationState::apply(Skeleton& skeleton) {
    if (skeleton.data().get() != data_.get()) {
        throw std::invalid_argument("AnimationState::apply requires a Skeleton created from the same SkeletonData");
    }

    skeleton.prepare_animation_pose();

    for (const std::shared_ptr<TrackEntry>& current : tracks_) {
        if (current == nullptr) {
            continue;
        }

        std::vector<Contribution> contributions;
        collect_contributions(current, 1.0, &contributions);

        if (!contributions.empty()) {
            const bool prefer_current_discrete =
                current->mixing_from == nullptr ||
                current->mix_duration <= kEpsilon ||
                current->mix_time >= current->mix_duration - kEpsilon;
            const std::shared_ptr<TrackEntry>& discrete_source =
                prefer_current_discrete ? contributions.back().entry : contributions.front().entry;
            apply_discrete_timelines(skeleton, *discrete_source);
        }

        const std::vector<BonePose> base_bones = skeleton.bone_poses();
        const std::vector<SlotState> base_slots = skeleton.slot_states();
        const std::vector<MeshDeformState> base_mesh_deforms = skeleton.mesh_deform_states();
        for (const Contribution& contribution : contributions) {
            apply_contribution(
                skeleton,
                *contribution.entry,
                contribution.weight,
                base_bones,
                base_slots,
                base_mesh_deforms);
        }
    }

    skeleton.update_world_transforms();
}

RootMotionDelta AnimationState::extract_root_motion(
    std::size_t track_index,
    std::size_t root_bone_index) const {
    if (track_index >= tracks_.size()) {
        return {};
    }

    const std::shared_ptr<TrackEntry>& entry = tracks_[track_index];
    if (entry == nullptr || entry->animation == nullptr || entry->is_empty ||
        root_bone_index >= data_->bones().size()) {
        return {};
    }

    const double duration = entry->animation_duration();
    if (duration <= 0.0) {
        return {};
    }

    RootMotionDelta delta;
    const auto accumulate_segment = [&](double start_sample_time, double end_sample_time) {
        const VectorSample start = sample_bone_translation_or_setup(
            *data_,
            *entry->animation,
            root_bone_index,
            start_sample_time);
        const VectorSample end = sample_bone_translation_or_setup(
            *data_,
            *entry->animation,
            root_bone_index,
            end_sample_time);
        delta.x += end.x - start.x;
        delta.y += end.y - start.y;
    };

    if (entry->loop) {
        const double previous_progress = std::max(0.0, entry->last_track_time_);
        const double current_progress = std::max(previous_progress, entry->track_time);
        const long long start_loop =
            static_cast<long long>(std::floor(previous_progress / duration));
        const long long end_loop =
            static_cast<long long>(std::floor(current_progress / duration));
        const double local_start =
            previous_progress - static_cast<double>(start_loop) * duration;
        const double local_end =
            current_progress - static_cast<double>(end_loop) * duration;

        if (start_loop == end_loop) {
            if (entry->reverse) {
                accumulate_segment(duration - local_start, duration - local_end);
            } else {
                accumulate_segment(local_start, local_end);
            }
            return delta;
        }

        if (entry->reverse) {
            accumulate_segment(duration - local_start, 0.0);
            for (long long loop_index = start_loop + 1; loop_index < end_loop; ++loop_index) {
                (void)loop_index;
                accumulate_segment(duration, 0.0);
            }
            accumulate_segment(duration, duration - local_end);
        } else {
            accumulate_segment(local_start, duration);
            for (long long loop_index = start_loop + 1; loop_index < end_loop; ++loop_index) {
                (void)loop_index;
                accumulate_segment(0.0, duration);
            }
            accumulate_segment(0.0, local_end);
        }
        return delta;
    }

    const double start_progress = std::clamp(entry->last_track_time_, 0.0, duration);
    const double end_progress = std::clamp(entry->track_time, 0.0, duration);
    if (entry->reverse) {
        accumulate_segment(duration - start_progress, duration - end_progress);
    } else {
        accumulate_segment(start_progress, end_progress);
    }

    return delta;
}

void AnimationState::set_listener(AnimationStateListener value) {
    listener_ = std::move(value);
}

void AnimationState::dispatch(
    AnimationStateEventType type,
    const std::shared_ptr<TrackEntry>& entry,
    const AnimationEvent* event) {
    if (type == AnimationStateEventType::Start && entry != nullptr) {
        entry->has_started_ = true;
    }
    if (entry != nullptr && entry->listener) {
        entry->listener(*this, type, entry, event);
        if (type == AnimationStateEventType::Start) {
            entry->start_notified_to_entry_ = true;
        }
    }
    if (listener_) {
        listener_(*this, type, entry, event);
    }
}

} // namespace marrow::runtime
