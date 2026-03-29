#include "marrow/allocator.hpp"
#include "marrow/runtime/animation_state.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <unordered_map>
#include <utility>

namespace marrow::runtime {

namespace {

constexpr double kEpsilon = 1e-6;

double clamp_unit(double value) {
    return std::clamp(value, 0.0, 1.0);
}

std::size_t* cursor_at(std::vector<std::size_t>* cursors, std::size_t index) {
    if (cursors == nullptr || index >= cursors->size()) {
        return nullptr;
    }

    return &(*cursors)[index];
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

double blend_timeline_scalar(
    double current,
    double setup,
    double sample,
    bool use_current_base,
    double weight) {
    const double alpha = clamp_unit(weight);
    if (use_current_base) {
        return current + (sample - current) * alpha;
    }

    return setup + (sample - setup) * alpha;
}

double normalize_rotation_degrees(double angle) {
    return angle - (std::ceil(angle / 360.0 - 0.5) * 360.0);
}

double blend_snapshot_rotation(double current, double target, double alpha) {
    return current + normalize_rotation_degrees(target - current) * clamp_unit(alpha);
}

double track_alpha(const TrackEntry& entry) {
    return clamp_unit(entry.alpha);
}

bool track_is_additive(const TrackEntry& entry) {
    return entry.blend_mode == AnimationLayerBlendMode::Additive;
}

bool track_affects_bone(const TrackEntry& entry, std::size_t bone_index) {
    return entry.bone_filter.empty() ||
        std::find(entry.bone_filter.begin(), entry.bone_filter.end(), bone_index) !=
        entry.bone_filter.end();
}

void apply_rotate_timeline_mixing(
    BoneTransform* current_pose,
    const BoneTransform& setup_pose,
    double target_rotation,
    bool use_current_base,
    double weight,
    TrackEntry* entry,
    std::size_t timeline_index);

void apply_layer_scalar(
    float* current,
    double setup,
    double sample,
    bool additive,
    bool use_current_base,
    double alpha) {
    if (current == nullptr) {
        return;
    }

    const double clamped_alpha = clamp_unit(alpha);
    if (clamped_alpha <= kEpsilon) {
        return;
    }

    if (additive) {
        *current += (sample - setup) * clamped_alpha;
        return;
    }

    *current = blend_timeline_scalar(
        *current,
        setup,
        sample,
        use_current_base,
        clamped_alpha);
}

void apply_layer_rotation(
    BoneTransform* current_pose,
    const BoneTransform& setup_pose,
    double target_rotation,
    bool additive,
    bool use_current_base,
    double weight,
    TrackEntry* entry,
    std::size_t timeline_index) {
    if (current_pose == nullptr) {
        return;
    }

    const double alpha = clamp_unit(weight);
    if (alpha <= kEpsilon) {
        return;
    }

    if (additive) {
        current_pose->rotation +=
            normalize_rotation_degrees(target_rotation - setup_pose.rotation) * alpha;
        return;
    }

    apply_rotate_timeline_mixing(
        current_pose,
        setup_pose,
        target_rotation,
        use_current_base,
        alpha,
        entry,
        timeline_index);
}

void apply_rotate_timeline_mixing(
    BoneTransform* current_pose,
    const BoneTransform& setup_pose,
    double target_rotation,
    bool use_current_base,
    double weight,
    TrackEntry* entry,
    std::size_t timeline_index) {
    if (current_pose == nullptr || entry == nullptr || entry->animation == nullptr) {
        return;
    }

    const double alpha = clamp_unit(weight);
    if (alpha <= kEpsilon) {
        return;
    }

    double start_rotation = use_current_base ? current_pose->rotation : setup_pose.rotation;
    if (alpha >= 1.0 - kEpsilon) {
        current_pose->rotation =
            start_rotation + normalize_rotation_degrees(target_rotation - start_rotation);
        return;
    }

    const std::size_t required_values = entry->animation->bone_rotate_timelines.size() * 2;
    const bool first_frame = entry->timelines_rotation.size() != required_values;
    if (first_frame) {
        entry->timelines_rotation.assign(required_values, 0.0);
    }

    const std::size_t state_index = timeline_index * 2;
    double diff = target_rotation - start_rotation;
    double total = 0.0;
    if (std::abs(diff) <= kEpsilon) {
        total = entry->timelines_rotation[state_index];
    } else {
        diff = normalize_rotation_degrees(diff);

        double last_total = 0.0;
        double last_diff = diff;
        if (!first_frame) {
            last_total = entry->timelines_rotation[state_index];
            last_diff = entry->timelines_rotation[state_index + 1];
        }

        const bool current_direction = diff > 0.0;
        bool direction = last_total >= 0.0;
        if ((last_diff > 0.0) != (diff > 0.0) && std::abs(last_diff) <= 90.0) {
            if (std::abs(last_total) > 180.0) {
                last_total += 360.0 * (last_total > 0.0 ? 1.0 : -1.0);
            }
            direction = current_direction;
        }

        total = diff + last_total - std::fmod(last_total, 360.0);
        if (direction != current_direction) {
            total += 360.0 *
                (last_total > 0.0 ? 1.0 : last_total < 0.0 ? -1.0 : 0.0);
        }
        entry->timelines_rotation[state_index] = total;
    }

    entry->timelines_rotation[state_index + 1] = diff;
    current_pose->rotation = start_rotation + total * alpha;
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

std::string make_index_property_id(std::string_view prefix, std::size_t index) {
    return std::string(prefix) + ":" + std::to_string(index);
}

std::string make_mesh_deform_property_id(
    std::size_t slot_index,
    std::string_view attachment_name) {
    return "meshDeform:" + std::to_string(slot_index) + ":" + std::string(attachment_name);
}

template <typename Fn>
void for_each_timeline_property(const AnimationData& animation, Fn&& fn) {
    std::size_t timeline_index = 0;
    for (const BoneRotateTimeline& timeline : animation.bone_rotate_timelines) {
        fn(timeline_index++, make_index_property_id("boneRotate", timeline.bone_index));
    }
    for (const BoneInheritTimeline& timeline : animation.bone_inherit_timelines) {
        fn(timeline_index++, make_index_property_id("boneInherit", timeline.bone_index));
    }
    for (const BoneTranslateTimeline& timeline : animation.bone_translate_timelines) {
        fn(timeline_index++, make_index_property_id("boneTranslate", timeline.bone_index));
    }
    for (const BoneScaleTimeline& timeline : animation.bone_scale_timelines) {
        fn(timeline_index++, make_index_property_id("boneScale", timeline.bone_index));
    }
    for (const BoneShearTimeline& timeline : animation.bone_shear_timelines) {
        fn(timeline_index++, make_index_property_id("boneShear", timeline.bone_index));
    }
    for (const SlotAttachmentTimeline& timeline : animation.slot_attachment_timelines) {
        fn(timeline_index++, make_index_property_id("slotAttachment", timeline.slot_index));
    }
    for (const SlotColorTimeline& timeline : animation.slot_color_timelines) {
        fn(timeline_index++, make_index_property_id("slotColor", timeline.slot_index));
    }
    for (const MeshDeformTimeline& timeline : animation.mesh_deform_timelines) {
        fn(timeline_index++, make_mesh_deform_property_id(timeline.slot_index, timeline.attachment_name));
    }
    if (animation.draw_order_timeline_data.has_value()) {
        fn(timeline_index++, "drawOrder");
    }
}

std::size_t timeline_count(const AnimationData& animation) {
    std::size_t count = 0;
    for_each_timeline_property(animation, [&](std::size_t, const std::string&) {
        ++count;
    });
    return count;
}

bool animation_has_timeline(const AnimationData& animation, std::string_view property_id) {
    bool found = false;
    for_each_timeline_property(animation, [&](std::size_t, const std::string& id) {
        if (id == property_id) {
            found = true;
        }
    });
    return found;
}

bool property_id_contains(
    const std::vector<std::string>& property_ids,
    std::string_view property_id) {
    return std::find(property_ids.begin(), property_ids.end(), property_id) != property_ids.end();
}

AnimationStateTimelineMode hold_mode_for_previous(bool has_previous) {
    return has_previous ? AnimationStateTimelineMode::HoldSubsequent
                        : AnimationStateTimelineMode::HoldFirst;
}

bool mode_uses_current_base(AnimationStateTimelineMode mode) {
    return mode == AnimationStateTimelineMode::Subsequent ||
        mode == AnimationStateTimelineMode::HoldSubsequent;
}

bool timeline_mode_at(
    const TrackEntry& entry,
    std::size_t timeline_index,
    AnimationStateTimelineMode* mode_out,
    std::shared_ptr<TrackEntry>* hold_mix_out = nullptr) {
    if (timeline_index >= entry.timeline_modes.size()) {
        return false;
    }

    *mode_out = entry.timeline_modes[timeline_index];
    if (hold_mix_out != nullptr) {
        if (timeline_index < entry.timeline_hold_mix.size()) {
            *hold_mix_out = entry.timeline_hold_mix[timeline_index].lock();
        } else {
            hold_mix_out->reset();
        }
    }
    return true;
}

double hold_mix_alpha(
    const std::shared_ptr<TrackEntry>& hold_mix,
    double alpha_hold) {
    if (hold_mix == nullptr || hold_mix->mix_duration <= kEpsilon) {
        return 0.0;
    }

    return alpha_hold * std::max(0.0, 1.0 - hold_mix->mix_time / hold_mix->mix_duration);
}

std::optional<double> snapshot_time_marker(double value) {
    if (value < 0.0) {
        return std::nullopt;
    }

    return value;
}

double restore_time_marker(const std::optional<double>& value) {
    return value.value_or(-1.0);
}

std::optional<double> snapshot_track_end(double value) {
    if (!std::isfinite(value)) {
        return std::nullopt;
    }

    return value;
}

double restore_track_end(const std::optional<double>& value) {
    if (!value.has_value()) {
        return std::numeric_limits<double>::infinity();
    }

    return *value;
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

    auto entry = marrow::allocate_shared<TrackEntry>();
    entry->owner_ = const_cast<AnimationState*>(this);
    entry->track_index = track_index;
    entry->animation = animation;
    entry->animation_name = std::string(animation_name);
    entry->loop = loop;
    entry->track_last = -1.0;
    entry->next_track_last = -1.0;
    entry->track_end = std::numeric_limits<double>::infinity();
    entry->interrupt_alpha = 1.0;
    return entry;
}

std::shared_ptr<TrackEntry> AnimationState::make_empty_entry(
    std::size_t track_index,
    double mix_duration) const {
    auto entry = marrow::allocate_shared<TrackEntry>();
    entry->owner_ = const_cast<AnimationState*>(this);
    entry->track_index = track_index;
    entry->animation_name = "<empty>";
    entry->is_empty = true;
    entry->mix_duration = std::max(0.0, mix_duration);
    entry->track_last = -1.0;
    entry->next_track_last = -1.0;
    entry->track_end = entry->mix_duration;
    entry->interrupt_alpha = 1.0;
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

    std::shared_ptr<TrackEntry> from = tracks_[track_index];
    tracks_[track_index] = entry;

    if (from != nullptr) {
        if (interrupt_current) {
            dispatch(AnimationStateEventType::Interrupt, from);
        }

        if (from->mixing_from != nullptr &&
            from->next_track_last >= 0.0 &&
            !from->snapshot_bone_poses.empty()) {
            from->snapshot_frozen = true;
        }

        entry->mixing_from = from;
        from->mixing_to = entry;
        entry->mix_time = 0.0;
        if (from->mixing_from != nullptr && from->mix_duration > kEpsilon) {
            entry->interrupt_alpha *= clamp_unit(from->mix_time / from->mix_duration);
        }
        from->timelines_rotation.clear();
    }

    animations_changed_ = true;
    dispatch(AnimationStateEventType::Start, entry);
}

std::shared_ptr<TrackEntry> AnimationState::set_animation(
    std::size_t track_index,
    std::string_view animation_name,
    bool loop,
    std::optional<double> mix_duration) {
    ensure_track(track_index);

    bool interrupt = true;
    std::shared_ptr<TrackEntry> current = tracks_[track_index];
    if (current != nullptr) {
        if (current->next_track_last < 0.0) {
            tracks_[track_index] = current->mixing_from;
            if (current->mixing_from != nullptr) {
                current->mixing_from->mixing_to.reset();
            }
            dispatch(AnimationStateEventType::Interrupt, current);
            current->mixing_from.reset();
            dispose_queued_entries(current->next);
            current->next.reset();
            dispose_entry_only(current, true);
            current = tracks_[track_index];
            interrupt = false;
        } else {
            dispose_queued_entries(current->next);
            current->next.reset();
        }
    }

    auto entry = make_animation_entry(track_index, animation_name, loop);
    entry->mix_duration = resolve_mix_duration(current, animation_name, mix_duration);
    replace_current(track_index, entry, interrupt);
    return entry;
}

std::shared_ptr<TrackEntry> AnimationState::add_animation(
    std::size_t track_index,
    std::string_view animation_name,
    bool loop,
    double delay,
    std::optional<double> mix_duration) {
    auto entry = make_animation_entry(track_index, animation_name, loop);
    entry->delay = delay;

    ensure_track(track_index);
    std::shared_ptr<TrackEntry> current = tracks_[track_index];
    if (current == nullptr) {
        entry->mix_duration = 0.0;
        replace_current(track_index, entry, false);
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
    ensure_track(track_index);

    bool interrupt = true;
    std::shared_ptr<TrackEntry> current = tracks_[track_index];
    if (current != nullptr) {
        if (current->next_track_last < 0.0) {
            tracks_[track_index] = current->mixing_from;
            if (current->mixing_from != nullptr) {
                current->mixing_from->mixing_to.reset();
            }
            dispatch(AnimationStateEventType::Interrupt, current);
            current->mixing_from.reset();
            dispose_queued_entries(current->next);
            current->next.reset();
            dispose_entry_only(current, true);
            current = tracks_[track_index];
            interrupt = false;
        } else {
            dispose_queued_entries(current->next);
            current->next.reset();
        }
    }

    auto entry = make_empty_entry(track_index, mix_duration);
    replace_current(track_index, entry, interrupt);
    return entry;
}

std::shared_ptr<TrackEntry> AnimationState::add_empty_animation(
    std::size_t track_index,
    double mix_duration,
    double delay) {
    if (delay <= 0.0) {
        delay -= mix_duration;
    }

    auto entry = make_empty_entry(track_index, mix_duration);
    entry->delay = delay;

    ensure_track(track_index);
    std::shared_ptr<TrackEntry> current = tracks_[track_index];
    if (current == nullptr) {
        replace_current(track_index, entry, false);
        return entry;
    }

    while (current->next != nullptr) {
        current = current->next;
    }

    current->next = entry;
    return entry;
}

AnimationStateSnapshot AnimationState::capture_state() const {
    AnimationStateSnapshot snapshot;
    snapshot.track_roots.resize(tracks_.size());

    std::unordered_map<const TrackEntry*, std::size_t> entry_indices;
    std::function<std::optional<std::size_t>(const std::shared_ptr<TrackEntry>&)> capture_entry =
        [&](const std::shared_ptr<TrackEntry>& entry) -> std::optional<std::size_t> {
            if (entry == nullptr) {
                return std::nullopt;
            }

            if (const auto existing = entry_indices.find(entry.get());
                existing != entry_indices.end()) {
                return existing->second;
            }

            const std::size_t snapshot_index = snapshot.entries.size();
            entry_indices.emplace(entry.get(), snapshot_index);
            snapshot.entries.emplace_back();

            AnimationStateTrackEntrySnapshot out;
            out.track_index = entry->track_index;
            out.animation_name = entry->animation_name;
            out.loop = entry->loop;
            out.is_empty = entry->is_empty;
            out.reverse = entry->reverse;
            out.alpha = entry->alpha;
            out.blend_mode = entry->blend_mode;
            out.bone_filter = entry->bone_filter;
            out.mix_duration = entry->mix_duration;
            out.mix_time = entry->mix_time;
            out.track_time = entry->track_time;
            out.track_last = snapshot_time_marker(entry->track_last);
            out.next_track_last = snapshot_time_marker(entry->next_track_last);
            out.track_end = snapshot_track_end(entry->track_end);
            out.delay = entry->delay;
            out.interrupt_alpha = entry->interrupt_alpha;
            out.total_alpha = entry->total_alpha;
            out.timelines_rotation = entry->timelines_rotation;
            out.snapshot_bone_poses = entry->snapshot_bone_poses;
            out.snapshot_slot_states = entry->snapshot_slot_states;
            out.snapshot_mesh_deforms = entry->snapshot_mesh_deforms;
            out.snapshot_draw_order = entry->snapshot_draw_order;
            out.snapshot_frozen = entry->snapshot_frozen;
            out.has_started = entry->has_started_;
            out.start_notified_to_entry = entry->start_notified_to_entry_;
            out.last_track_time = entry->last_track_time_;
            out.next_entry_index = capture_entry(entry->next);
            out.mixing_from_entry_index = capture_entry(entry->mixing_from);
            snapshot.entries[snapshot_index] = std::move(out);
            return snapshot_index;
        };

    for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        snapshot.track_roots[track_index] = capture_entry(tracks_[track_index]);
    }

    return snapshot;
}

void AnimationState::restore_state(const AnimationStateSnapshot& snapshot) {
    tracks_.assign(snapshot.track_roots.size(), nullptr);
    property_ids_.clear();
    animations_changed_ = true;

    std::vector<std::shared_ptr<TrackEntry>> restored_entries(snapshot.entries.size());
    std::vector<bool> attempted(snapshot.entries.size(), false);
    std::function<std::shared_ptr<TrackEntry>(std::size_t)> restore_entry =
        [&](std::size_t snapshot_index) -> std::shared_ptr<TrackEntry> {
            if (snapshot_index >= snapshot.entries.size()) {
                return nullptr;
            }
            if (attempted[snapshot_index]) {
                return restored_entries[snapshot_index];
            }

            attempted[snapshot_index] = true;
            const AnimationStateTrackEntrySnapshot& in = snapshot.entries[snapshot_index];

            const AnimationData* animation = nullptr;
            if (!in.is_empty) {
                animation = data_->find_animation(in.animation_name);
                if (animation == nullptr) {
                    return nullptr;
                }
            }

            auto entry = marrow::allocate_shared<TrackEntry>();
            entry->owner_ = this;
            entry->track_index = in.track_index;
            entry->animation = animation;
            entry->animation_name = in.animation_name;
            entry->loop = in.loop;
            entry->is_empty = in.is_empty;
            entry->reverse = in.reverse;
            entry->alpha = in.alpha;
            entry->blend_mode = in.blend_mode;
            entry->bone_filter = in.bone_filter;
            entry->mix_duration = in.mix_duration;
            entry->mix_time = in.mix_time;
            entry->track_time = in.track_time;
            entry->track_last = restore_time_marker(in.track_last);
            entry->next_track_last = restore_time_marker(in.next_track_last);
            entry->track_end = restore_track_end(in.track_end);
            entry->delay = in.delay;
            entry->interrupt_alpha = in.interrupt_alpha;
            entry->total_alpha = in.total_alpha;
            entry->timelines_rotation = in.timelines_rotation;
            entry->snapshot_bone_poses = in.snapshot_bone_poses;
            entry->snapshot_slot_states = in.snapshot_slot_states;
            entry->snapshot_mesh_deforms = in.snapshot_mesh_deforms;
            entry->snapshot_draw_order = in.snapshot_draw_order;
            entry->snapshot_frozen = in.snapshot_frozen;
            entry->has_started_ = in.has_started;
            entry->start_notified_to_entry_ = in.start_notified_to_entry;
            entry->last_track_time_ = in.last_track_time;

            restored_entries[snapshot_index] = entry;

            if (in.next_entry_index.has_value()) {
                entry->next = restore_entry(*in.next_entry_index);
            }
            if (in.mixing_from_entry_index.has_value()) {
                entry->mixing_from = restore_entry(*in.mixing_from_entry_index);
                if (entry->mixing_from != nullptr) {
                    entry->mixing_from->mixing_to = entry;
                }
            }

            return entry;
        };

    for (std::size_t track_index = 0; track_index < snapshot.track_roots.size(); ++track_index) {
        if (!snapshot.track_roots[track_index].has_value()) {
            continue;
        }

        std::shared_ptr<TrackEntry> restored = restore_entry(*snapshot.track_roots[track_index]);
        if (restored == nullptr) {
            continue;
        }

        restored->track_index = track_index;
        tracks_[track_index] = restored;
    }
}

void AnimationState::clear_track(std::size_t track_index) {
    if (track_index >= tracks_.size() || tracks_[track_index] == nullptr) {
        return;
    }

    dispose_active_entry(tracks_[track_index], true);
    tracks_[track_index].reset();
    animations_changed_ = true;
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

bool AnimationState::update_mixing_from(
    const std::shared_ptr<TrackEntry>& to,
    double delta) {
    if (to == nullptr || to->mixing_from == nullptr) {
        return true;
    }

    std::shared_ptr<TrackEntry> from = to->mixing_from;
    const bool finished = update_mixing_from(from, delta);

    from->track_last = from->next_track_last;
    if (from->track_last >= 0.0) {
        from->last_track_time_ = from->track_last;
    }

    if (to->mix_time > 0.0 && to->mix_time >= to->mix_duration - kEpsilon) {
        if (from->snapshot_frozen) {
            to->mixing_from.reset();
            from->mixing_to.reset();
            dispose_active_entry(from, true);
            animations_changed_ = true;
            return finished;
        }

        if (to->mixing_to.expired() ||
            from->total_alpha <= kEpsilon ||
            to->mix_duration <= kEpsilon) {
            to->mixing_from = from->mixing_from;
            if (from->mixing_from != nullptr) {
                from->mixing_from->mixing_to = to;
            }
            to->interrupt_alpha = from->interrupt_alpha;
            from->mixing_from.reset();
            from->mixing_to.reset();
            dispose_entry_only(from, true);
            animations_changed_ = true;
        }

        return finished;
    }

    const double previous_time = from->track_time;
    from->last_track_time_ = previous_time;
    from->track_time += delta;
    dispatch_event_callbacks(from, previous_time, from->track_time);
    dispatch_complete_callbacks(from, previous_time, from->track_time);
    to->mix_time += delta;
    return false;
}

void AnimationState::advance_entry(
    const std::shared_ptr<TrackEntry>& entry,
    double delta) {
    if (entry == nullptr) {
        return;
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
        if (auto mixing_to = entry->mixing_from->mixing_to.lock(); mixing_to.get() == entry.get()) {
            entry->mixing_from->mixing_to.reset();
        }
        dispose_active_entry(entry->mixing_from, true);
    }
    dispatch(AnimationStateEventType::Dispose, entry);
}

void AnimationState::dispose_entry_only(
    const std::shared_ptr<TrackEntry>& entry,
    bool dispatch_end) {
    if (entry == nullptr) {
        return;
    }

    if (dispatch_end) {
        dispatch(AnimationStateEventType::End, entry);
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
        if (auto mixing_to = entry->mixing_from->mixing_to.lock(); mixing_to.get() == entry.get()) {
            entry->mixing_from->mixing_to.reset();
        }
        dispose_active_entry(entry->mixing_from, dispatch_end);
    }
    entry->mixing_from.reset();
    entry->mixing_to.reset();
    if (dispatch_end) {
        dispatch(AnimationStateEventType::End, entry);
    }
    dispatch(AnimationStateEventType::Dispose, entry);
}

void AnimationState::prune_mixing_from(const std::shared_ptr<TrackEntry>&) {
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
    next->delay = 0.0;
    next->track_time = carry;

    replace_current(track_index, next, true);
    next->mix_time = carry;
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

        current->track_last = current->next_track_last;
        if (current->track_last >= 0.0) {
            current->last_track_time_ = current->track_last;
        }

        double current_delta = delta;
        if (current->delay > 0.0) {
            current->delay -= current_delta;
            if (current->delay > 0.0) {
                continue;
            }
            current_delta = -current->delay;
            current->delay = 0.0;
        }

        if (current->mixing_from != nullptr) {
            (void)update_mixing_from(current, delta);
        }

        const double previous_time = current->track_time;
        current->last_track_time_ = previous_time;
        current->track_time += current_delta;
        if (current->is_empty && current->mixing_from == nullptr) {
            current->mix_time += current_delta;
        }
        dispatch_event_callbacks(current, previous_time, current->track_time);
        dispatch_complete_callbacks(current, previous_time, current->track_time);

        current = tracks_[track_index];
        while (current != nullptr &&
               current->next != nullptr &&
               current->track_time >= entry_transition_time(*current) + current->next->delay) {
            start_next_entry(track_index);
            current = tracks_[track_index];
        }

        current = tracks_[track_index];
        if (current != nullptr &&
            current->next == nullptr &&
            current->mixing_from == nullptr &&
            current->track_last >= 0.0 &&
            current->track_last >= current->track_end - kEpsilon) {
            dispose_active_entry(current, true);
            tracks_[track_index].reset();
            animations_changed_ = true;
        }
    }
}

void AnimationState::refresh_timeline_modes() {
    animations_changed_ = false;
    property_ids_.clear();

    for (const std::shared_ptr<TrackEntry>& current : tracks_) {
        if (current == nullptr) {
            continue;
        }

        std::shared_ptr<TrackEntry> entry = current;
        while (entry->mixing_from != nullptr) {
            entry = entry->mixing_from;
        }

        while (entry != nullptr) {
            set_timeline_modes(entry);
            entry = entry->mixing_to.lock();
        }
    }
}

void AnimationState::set_timeline_modes(const std::shared_ptr<TrackEntry>& entry) {
    if (entry == nullptr || entry->animation == nullptr || entry->is_empty) {
        entry->timeline_modes.clear();
        entry->timeline_hold_mix.clear();
        return;
    }

    const std::size_t count = timeline_count(*entry->animation);
    entry->timeline_modes.assign(count, AnimationStateTimelineMode::First);
    entry->timeline_hold_mix.assign(count, std::weak_ptr<TrackEntry>{});

    const std::shared_ptr<TrackEntry> to = entry->mixing_to.lock();
    for_each_timeline_property(
        *entry->animation,
        [&](std::size_t timeline_index, const std::string& property_id) {
            const bool has_previous = property_id_contains(property_ids_, property_id);
            if (!has_previous) {
                property_ids_.push_back(property_id);
            }

            if (to != nullptr &&
                entry->mixing_from != nullptr &&
                (to->is_empty ||
                 to->animation == nullptr ||
                 !animation_has_timeline(*to->animation, property_id))) {
                entry->timeline_modes[timeline_index] = hold_mode_for_previous(has_previous);
                return;
            }

            if (to == nullptr ||
                to->animation == nullptr ||
                !animation_has_timeline(*to->animation, property_id)) {
                entry->timeline_modes[timeline_index] = has_previous
                    ? AnimationStateTimelineMode::Subsequent
                    : AnimationStateTimelineMode::First;
                return;
            }

            std::shared_ptr<TrackEntry> hold_mix;
            for (std::shared_ptr<TrackEntry> next = to->mixing_to.lock();
                 next != nullptr;
                 next = next->mixing_to.lock()) {
                if (next->animation != nullptr &&
                    animation_has_timeline(*next->animation, property_id)) {
                    continue;
                }

                if (entry->mix_duration > kEpsilon) {
                    hold_mix = next;
                }
                break;
            }

            if (hold_mix != nullptr) {
                entry->timeline_modes[timeline_index] = AnimationStateTimelineMode::HoldMix;
                entry->timeline_hold_mix[timeline_index] = hold_mix;
            } else {
                entry->timeline_modes[timeline_index] = hold_mode_for_previous(has_previous);
            }
        });
}

void AnimationState::apply_current_timeline_values(
    Skeleton& skeleton,
    TrackEntry& entry,
    double alpha,
    bool) const {
    if (entry.animation == nullptr || entry.is_empty || alpha <= kEpsilon) {
        return;
    }

    auto& bones = skeleton.bone_poses();
    auto& slots = skeleton.slot_states();
    auto& mesh_deforms = skeleton.mesh_deform_states();
    const double sample_time = entry.animation_time();
    SamplingContext& sampling_context =
        skeleton.track_sampling_context(entry.track_index, &entry, *entry.animation, sample_time);
    const bool additive = track_is_additive(entry);
    std::size_t timeline_index = 0;

    for (std::size_t rotate_timeline_index = 0;
         rotate_timeline_index < entry.animation->bone_rotate_timelines.size();
         ++rotate_timeline_index) {
        const BoneRotateTimeline& timeline = entry.animation->bone_rotate_timelines[rotate_timeline_index];
        if (timeline.bone_index < bones.size() &&
            timeline.bone_index < data_->bones().size() &&
            track_affects_bone(entry, timeline.bone_index)) {
            AnimationStateTimelineMode mode = AnimationStateTimelineMode::First;
            (void)timeline_mode_at(entry, timeline_index, &mode);
            const bool use_current_base = mode_uses_current_base(mode);
            const BoneTransform& setup_pose = data_->bones()[timeline.bone_index].setup_pose;

            if (const std::optional<double> rotation =
                    sample_rotate_timeline(
                        timeline,
                        sample_time,
                        cursor_at(
                            &sampling_context.rotate_last_keyframe_indices,
                            rotate_timeline_index))) {
                apply_layer_rotation(
                    &bones[timeline.bone_index].local_pose,
                    setup_pose,
                    *rotation,
                    additive,
                    use_current_base,
                    alpha,
                    &entry,
                    timeline_index);
            }
        }
        ++timeline_index;
    }

    timeline_index += entry.animation->bone_inherit_timelines.size();

    for (std::size_t translate_timeline_index = 0;
         translate_timeline_index < entry.animation->bone_translate_timelines.size();
         ++translate_timeline_index) {
        const BoneTranslateTimeline& timeline =
            entry.animation->bone_translate_timelines[translate_timeline_index];
        if (timeline.bone_index < bones.size() &&
            timeline.bone_index < data_->bones().size() &&
            track_affects_bone(entry, timeline.bone_index)) {
            AnimationStateTimelineMode mode = AnimationStateTimelineMode::First;
            (void)timeline_mode_at(entry, timeline_index, &mode);
            BoneTransform& current_pose = bones[timeline.bone_index].local_pose;
            const BoneTransform& setup_pose = data_->bones()[timeline.bone_index].setup_pose;
            const bool use_current_base = mode_uses_current_base(mode);

            if (const std::optional<VectorSample> translation =
                    sample_translate_timeline(
                        timeline,
                        sample_time,
                        cursor_at(
                            &sampling_context.translate_last_keyframe_indices,
                            translate_timeline_index))) {
                apply_layer_scalar(
                    &current_pose.x,
                    setup_pose.x,
                    translation->x,
                    additive,
                    use_current_base,
                    alpha);
                apply_layer_scalar(
                    &current_pose.y,
                    setup_pose.y,
                    translation->y,
                    additive,
                    use_current_base,
                    alpha);
            }
        }
        ++timeline_index;
    }

    for (std::size_t scale_timeline_index = 0;
         scale_timeline_index < entry.animation->bone_scale_timelines.size();
         ++scale_timeline_index) {
        const BoneScaleTimeline& timeline = entry.animation->bone_scale_timelines[scale_timeline_index];
        if (timeline.bone_index < bones.size() &&
            timeline.bone_index < data_->bones().size() &&
            track_affects_bone(entry, timeline.bone_index)) {
            AnimationStateTimelineMode mode = AnimationStateTimelineMode::First;
            (void)timeline_mode_at(entry, timeline_index, &mode);
            BoneTransform& current_pose = bones[timeline.bone_index].local_pose;
            const BoneTransform& setup_pose = data_->bones()[timeline.bone_index].setup_pose;
            const bool use_current_base = mode_uses_current_base(mode);

            if (const std::optional<VectorSample> scale =
                    sample_scale_timeline(
                        timeline,
                        sample_time,
                        cursor_at(
                            &sampling_context.scale_last_keyframe_indices,
                            scale_timeline_index))) {
                apply_layer_scalar(
                    &current_pose.scale_x,
                    setup_pose.scale_x,
                    scale->x,
                    additive,
                    use_current_base,
                    alpha);
                apply_layer_scalar(
                    &current_pose.scale_y,
                    setup_pose.scale_y,
                    scale->y,
                    additive,
                    use_current_base,
                    alpha);
            }
        }
        ++timeline_index;
    }

    for (std::size_t shear_timeline_index = 0;
         shear_timeline_index < entry.animation->bone_shear_timelines.size();
         ++shear_timeline_index) {
        const BoneShearTimeline& timeline = entry.animation->bone_shear_timelines[shear_timeline_index];
        if (timeline.bone_index < bones.size() &&
            timeline.bone_index < data_->bones().size() &&
            track_affects_bone(entry, timeline.bone_index)) {
            AnimationStateTimelineMode mode = AnimationStateTimelineMode::First;
            (void)timeline_mode_at(entry, timeline_index, &mode);
            BoneTransform& current_pose = bones[timeline.bone_index].local_pose;
            const BoneTransform& setup_pose = data_->bones()[timeline.bone_index].setup_pose;
            const bool use_current_base = mode_uses_current_base(mode);

            if (const std::optional<VectorSample> shear =
                    sample_shear_timeline(
                        timeline,
                        sample_time,
                        cursor_at(
                            &sampling_context.shear_last_keyframe_indices,
                            shear_timeline_index))) {
                apply_layer_scalar(
                    &current_pose.shear_x,
                    setup_pose.shear_x,
                    shear->x,
                    additive,
                    use_current_base,
                    alpha);
                apply_layer_scalar(
                    &current_pose.shear_y,
                    setup_pose.shear_y,
                    shear->y,
                    additive,
                    use_current_base,
                    alpha);
            }
        }
        ++timeline_index;
    }

    timeline_index += entry.animation->slot_attachment_timelines.size();

    for (std::size_t color_timeline_index = 0;
         color_timeline_index < entry.animation->slot_color_timelines.size();
         ++color_timeline_index) {
        const SlotColorTimeline& timeline = entry.animation->slot_color_timelines[color_timeline_index];
        if (timeline.slot_index < slots.size() &&
            timeline.slot_index < data_->slots().size()) {
            AnimationStateTimelineMode mode = AnimationStateTimelineMode::First;
            (void)timeline_mode_at(entry, timeline_index, &mode);
            SlotColor& current_color = slots[timeline.slot_index].color;
            const SlotColor& setup_color = data_->slots()[timeline.slot_index].color;
            const bool use_current_base = mode_uses_current_base(mode);

            if (const std::optional<SlotColor> color =
                    sample_color_timeline(
                        timeline,
                        sample_time,
                        cursor_at(
                            &sampling_context.color_last_keyframe_indices,
                            color_timeline_index))) {
                current_color.r = blend_timeline_scalar(
                    current_color.r,
                    setup_color.r,
                    color->r,
                    use_current_base,
                    alpha);
                current_color.g = blend_timeline_scalar(
                    current_color.g,
                    setup_color.g,
                    color->g,
                    use_current_base,
                    alpha);
                current_color.b = blend_timeline_scalar(
                    current_color.b,
                    setup_color.b,
                    color->b,
                    use_current_base,
                    alpha);
                current_color.a = blend_timeline_scalar(
                    current_color.a,
                    setup_color.a,
                    color->a,
                    use_current_base,
                    alpha);
            }
        }
        ++timeline_index;
    }

    for (std::size_t deform_timeline_index = 0;
         deform_timeline_index < entry.animation->mesh_deform_timelines.size();
         ++deform_timeline_index) {
        const MeshDeformTimeline& timeline = entry.animation->mesh_deform_timelines[deform_timeline_index];
        if (timeline.slot_index < mesh_deforms.size()) {
            AnimationStateTimelineMode mode = AnimationStateTimelineMode::First;
            (void)timeline_mode_at(entry, timeline_index, &mode);

            const AttachmentData* attachment = skeleton.current_attachment(timeline.slot_index);
            if (attachment != nullptr &&
                attachment->mesh_geometry != nullptr &&
                attachment_matches_mesh_deform_source(*attachment, timeline.attachment_name)) {
                const std::optional<std::vector<double>> sampled_offsets =
                    sample_deform_timeline(
                        timeline,
                        sample_time,
                        cursor_at(
                            &sampling_context.deform_last_keyframe_indices,
                            deform_timeline_index));
                if (sampled_offsets.has_value()) {
                    MeshDeformState& current_deform = mesh_deforms[timeline.slot_index];
                    if (current_deform.attachment_name != timeline.attachment_name ||
                        current_deform.vertex_offsets.size() != sampled_offsets->size()) {
                        current_deform.attachment_name = timeline.attachment_name;
                        current_deform.vertex_offsets.assign(sampled_offsets->size(), 0.0);
                    }

                    const bool use_current_base =
                        mode_uses_current_base(mode) &&
                        current_deform.attachment_name == timeline.attachment_name &&
                        current_deform.vertex_offsets.size() == sampled_offsets->size();
                    for (std::size_t component_index = 0;
                         component_index < sampled_offsets->size();
                         ++component_index) {
                        current_deform.vertex_offsets[component_index] = blend_timeline_scalar(
                            current_deform.vertex_offsets[component_index],
                            0.0,
                            (*sampled_offsets)[component_index],
                            use_current_base,
                            alpha);
                    }
                }
            }
        }
        ++timeline_index;
    }
}

void AnimationState::apply_discrete_timelines(
    Skeleton& skeleton,
    const TrackEntry& entry) const {
    if (entry.animation == nullptr || entry.is_empty) {
        return;
    }

    auto& bones = skeleton.bone_poses();
    auto& slots = skeleton.slot_states();
    auto& draw_order = skeleton.draw_order();
    const double sample_time = entry.animation_time();
    SamplingContext& sampling_context =
        skeleton.track_sampling_context(entry.track_index, &entry, *entry.animation, sample_time);

    for (std::size_t inherit_timeline_index = 0;
         inherit_timeline_index < entry.animation->bone_inherit_timelines.size();
         ++inherit_timeline_index) {
        const BoneInheritTimeline& timeline = entry.animation->bone_inherit_timelines[inherit_timeline_index];
        if (track_is_additive(entry) ||
            timeline.bone_index >= bones.size() ||
            !track_affects_bone(entry, timeline.bone_index)) {
            continue;
        }

        const InheritKeyframe* keyframe = sample_inherit_timeline(
            timeline,
            sample_time,
            cursor_at(&sampling_context.inherit_last_keyframe_indices, inherit_timeline_index));
        if (keyframe == nullptr) {
            continue;
        }

        bones[timeline.bone_index].inherit = keyframe->inherit;
    }

    for (std::size_t attachment_timeline_index = 0;
         attachment_timeline_index < entry.animation->slot_attachment_timelines.size();
         ++attachment_timeline_index) {
        const SlotAttachmentTimeline& timeline =
            entry.animation->slot_attachment_timelines[attachment_timeline_index];
        if (timeline.slot_index >= slots.size()) {
            continue;
        }

        const AttachmentKeyframe* keyframe = sample_attachment_timeline(
            timeline,
            sample_time,
            cursor_at(
                &sampling_context.attachment_last_keyframe_indices,
                attachment_timeline_index));
        if (keyframe == nullptr) {
            continue;
        }

        skeleton.apply_slot_attachment_keyframe(timeline.slot_index, keyframe->attachment_name);
    }

    if (const DrawOrderTimeline* draw_order_timeline = entry.animation->find_draw_order_timeline()) {
        if (const DrawOrderKeyframe* draw_order_keyframe = sample_draw_order_timeline(
                *draw_order_timeline,
                sample_time,
                &sampling_context.draw_order_last_keyframe_index)) {
            draw_order = draw_order_keyframe->slot_indices;
        }
    }
}

void AnimationState::capture_entry_snapshot(
    const std::shared_ptr<TrackEntry>& entry,
    const Skeleton& skeleton) const {
    if (entry == nullptr) {
        return;
    }

    entry->snapshot_bone_poses = skeleton.bone_poses();
    entry->snapshot_slot_states = skeleton.slot_states();
    entry->snapshot_mesh_deforms = skeleton.mesh_deform_states();
    entry->snapshot_draw_order = skeleton.draw_order();
}

double AnimationState::apply_mixing_from(
    const std::shared_ptr<TrackEntry>& entry,
    Skeleton& skeleton) const {
    if (entry == nullptr || entry->mixing_from == nullptr) {
        return 1.0;
    }

    const std::shared_ptr<TrackEntry> from = entry->mixing_from;

    double mix = 1.0;
    if (entry->mix_duration > kEpsilon) {
        mix = clamp_unit(entry->mix_time / entry->mix_duration);
    }

    if (from->snapshot_frozen) {
        const double alpha = track_alpha(*from) * (1.0 - mix);
        from->total_alpha = alpha;
        if (alpha <= kEpsilon) {
            from->next_track_last = from->track_time;
            return mix;
        }

        auto& bones = skeleton.bone_poses();
        auto& slots = skeleton.slot_states();
        auto& mesh_deforms = skeleton.mesh_deform_states();
        auto& draw_order = skeleton.draw_order();

        if (from->snapshot_bone_poses.size() == bones.size()) {
            for (std::size_t bone_index = 0; bone_index < bones.size(); ++bone_index) {
                BoneTransform& current_pose = bones[bone_index].local_pose;
                const BoneTransform& snapshot_pose = from->snapshot_bone_poses[bone_index].local_pose;
                current_pose.rotation =
                    blend_snapshot_rotation(current_pose.rotation, snapshot_pose.rotation, alpha);
                current_pose.x += (snapshot_pose.x - current_pose.x) * alpha;
                current_pose.y += (snapshot_pose.y - current_pose.y) * alpha;
                current_pose.scale_x += (snapshot_pose.scale_x - current_pose.scale_x) * alpha;
                current_pose.scale_y += (snapshot_pose.scale_y - current_pose.scale_y) * alpha;
                current_pose.shear_x += (snapshot_pose.shear_x - current_pose.shear_x) * alpha;
                current_pose.shear_y += (snapshot_pose.shear_y - current_pose.shear_y) * alpha;
                bones[bone_index].inherit = from->snapshot_bone_poses[bone_index].inherit;
            }
        }

        if (from->snapshot_slot_states.size() == slots.size()) {
            for (std::size_t slot_index = 0; slot_index < slots.size(); ++slot_index) {
                SlotState& current_slot = slots[slot_index];
                const SlotState& snapshot_slot = from->snapshot_slot_states[slot_index];
                current_slot.attachment_name = snapshot_slot.attachment_name;
                current_slot.attachment_skin_index = snapshot_slot.attachment_skin_index;
                current_slot.color.r += (snapshot_slot.color.r - current_slot.color.r) * alpha;
                current_slot.color.g += (snapshot_slot.color.g - current_slot.color.g) * alpha;
                current_slot.color.b += (snapshot_slot.color.b - current_slot.color.b) * alpha;
                current_slot.color.a += (snapshot_slot.color.a - current_slot.color.a) * alpha;
                current_slot.dark_color = snapshot_slot.dark_color;
            }
        }

        if (from->snapshot_mesh_deforms.size() == mesh_deforms.size()) {
            for (std::size_t slot_index = 0; slot_index < mesh_deforms.size(); ++slot_index) {
                MeshDeformState& current_deform = mesh_deforms[slot_index];
                const MeshDeformState& snapshot_deform = from->snapshot_mesh_deforms[slot_index];
                current_deform.attachment_name = snapshot_deform.attachment_name;
                current_deform.vertex_offsets.resize(snapshot_deform.vertex_offsets.size(), 0.0);
                for (std::size_t component_index = 0;
                     component_index < snapshot_deform.vertex_offsets.size();
                     ++component_index) {
                    current_deform.vertex_offsets[component_index] +=
                        (snapshot_deform.vertex_offsets[component_index] -
                         current_deform.vertex_offsets[component_index]) *
                        alpha;
                }
            }
        }

        if (from->snapshot_draw_order.size() == draw_order.size()) {
            draw_order = from->snapshot_draw_order;
        }

        from->next_track_last = from->track_time;
        return mix;
    }

    const double alpha_hold = track_alpha(*from) * entry->interrupt_alpha;
    const double alpha_mix = alpha_hold * (1.0 - mix);

    if (from->mixing_from != nullptr) {
        (void)apply_mixing_from(from, skeleton);
    }
    const double sample_time = from->animation_time();
    from->total_alpha = 0.0;

    if (from->animation == nullptr || from->is_empty) {
        from->next_track_last = from->track_time;
        return mix;
    }

    SamplingContext& sampling_context =
        skeleton.track_sampling_context(from->track_index, from.get(), *from->animation, sample_time);

    auto& bones = skeleton.bone_poses();
    auto& slots = skeleton.slot_states();
    auto& mesh_deforms = skeleton.mesh_deform_states();
    const bool additive = track_is_additive(*from);
    std::size_t timeline_index = 0;

    for (std::size_t rotate_timeline_index = 0;
         rotate_timeline_index < from->animation->bone_rotate_timelines.size();
         ++rotate_timeline_index) {
        const BoneRotateTimeline& timeline = from->animation->bone_rotate_timelines[rotate_timeline_index];
        AnimationStateTimelineMode mode = AnimationStateTimelineMode::First;
        std::shared_ptr<TrackEntry> hold_mix;
        (void)timeline_mode_at(*from, timeline_index, &mode, &hold_mix);

        double alpha = alpha_mix;
        switch (mode) {
        case AnimationStateTimelineMode::Subsequent:
        case AnimationStateTimelineMode::First:
            alpha = alpha_mix;
            break;
        case AnimationStateTimelineMode::HoldSubsequent:
        case AnimationStateTimelineMode::HoldFirst:
            alpha = alpha_hold;
            break;
        case AnimationStateTimelineMode::HoldMix:
            alpha = hold_mix_alpha(hold_mix, alpha_hold);
            break;
        }

        from->total_alpha += alpha;
        if (alpha > kEpsilon &&
            timeline.bone_index < bones.size() &&
            timeline.bone_index < data_->bones().size() &&
            track_affects_bone(*from, timeline.bone_index)) {
            const bool use_current_base = mode_uses_current_base(mode);
            const BoneTransform& setup_pose = data_->bones()[timeline.bone_index].setup_pose;
            if (const std::optional<double> rotation =
                    sample_rotate_timeline(
                        timeline,
                        sample_time,
                        cursor_at(
                            &sampling_context.rotate_last_keyframe_indices,
                            rotate_timeline_index))) {
                apply_layer_rotation(
                    &bones[timeline.bone_index].local_pose,
                    setup_pose,
                    *rotation,
                    additive,
                    use_current_base,
                    alpha,
                    from.get(),
                    timeline_index);
            }
        }
        ++timeline_index;
    }

    for (const BoneInheritTimeline& timeline : from->animation->bone_inherit_timelines) {
        if (additive || !track_affects_bone(*from, timeline.bone_index)) {
            ++timeline_index;
            continue;
        }
        AnimationStateTimelineMode mode = AnimationStateTimelineMode::First;
        std::shared_ptr<TrackEntry> hold_mix;
        (void)timeline_mode_at(*from, timeline_index, &mode, &hold_mix);
        switch (mode) {
        case AnimationStateTimelineMode::Subsequent:
        case AnimationStateTimelineMode::First:
            from->total_alpha += alpha_mix;
            break;
        case AnimationStateTimelineMode::HoldSubsequent:
        case AnimationStateTimelineMode::HoldFirst:
            from->total_alpha += alpha_hold;
            break;
        case AnimationStateTimelineMode::HoldMix:
            from->total_alpha += hold_mix_alpha(hold_mix, alpha_hold);
            break;
        }
        ++timeline_index;
    }

    for (std::size_t translate_timeline_index = 0;
         translate_timeline_index < from->animation->bone_translate_timelines.size();
         ++translate_timeline_index) {
        const BoneTranslateTimeline& timeline =
            from->animation->bone_translate_timelines[translate_timeline_index];
        AnimationStateTimelineMode mode = AnimationStateTimelineMode::First;
        std::shared_ptr<TrackEntry> hold_mix;
        (void)timeline_mode_at(*from, timeline_index, &mode, &hold_mix);
        double alpha = alpha_mix;
        switch (mode) {
        case AnimationStateTimelineMode::Subsequent:
        case AnimationStateTimelineMode::First:
            alpha = alpha_mix;
            break;
        case AnimationStateTimelineMode::HoldSubsequent:
        case AnimationStateTimelineMode::HoldFirst:
            alpha = alpha_hold;
            break;
        case AnimationStateTimelineMode::HoldMix:
            alpha = hold_mix_alpha(hold_mix, alpha_hold);
            break;
        }

        from->total_alpha += alpha;
        if (alpha > kEpsilon &&
            timeline.bone_index < bones.size() &&
            timeline.bone_index < data_->bones().size() &&
            track_affects_bone(*from, timeline.bone_index)) {
            BoneTransform& current_pose = bones[timeline.bone_index].local_pose;
            const BoneTransform& setup_pose = data_->bones()[timeline.bone_index].setup_pose;
            const bool use_current_base = mode_uses_current_base(mode);
            if (const std::optional<VectorSample> translation =
                    sample_translate_timeline(
                        timeline,
                        sample_time,
                        cursor_at(
                            &sampling_context.translate_last_keyframe_indices,
                            translate_timeline_index))) {
                apply_layer_scalar(
                    &current_pose.x,
                    setup_pose.x,
                    translation->x,
                    additive,
                    use_current_base,
                    alpha);
                apply_layer_scalar(
                    &current_pose.y,
                    setup_pose.y,
                    translation->y,
                    additive,
                    use_current_base,
                    alpha);
            }
        }
        ++timeline_index;
    }

    for (std::size_t scale_timeline_index = 0;
         scale_timeline_index < from->animation->bone_scale_timelines.size();
         ++scale_timeline_index) {
        const BoneScaleTimeline& timeline = from->animation->bone_scale_timelines[scale_timeline_index];
        AnimationStateTimelineMode mode = AnimationStateTimelineMode::First;
        std::shared_ptr<TrackEntry> hold_mix;
        (void)timeline_mode_at(*from, timeline_index, &mode, &hold_mix);
        double alpha = alpha_mix;
        switch (mode) {
        case AnimationStateTimelineMode::Subsequent:
        case AnimationStateTimelineMode::First:
            alpha = alpha_mix;
            break;
        case AnimationStateTimelineMode::HoldSubsequent:
        case AnimationStateTimelineMode::HoldFirst:
            alpha = alpha_hold;
            break;
        case AnimationStateTimelineMode::HoldMix:
            alpha = hold_mix_alpha(hold_mix, alpha_hold);
            break;
        }

        from->total_alpha += alpha;
        if (alpha > kEpsilon &&
            timeline.bone_index < bones.size() &&
            timeline.bone_index < data_->bones().size() &&
            track_affects_bone(*from, timeline.bone_index)) {
            BoneTransform& current_pose = bones[timeline.bone_index].local_pose;
            const BoneTransform& setup_pose = data_->bones()[timeline.bone_index].setup_pose;
            const bool use_current_base = mode_uses_current_base(mode);
            if (const std::optional<VectorSample> scale =
                    sample_scale_timeline(
                        timeline,
                        sample_time,
                        cursor_at(
                            &sampling_context.scale_last_keyframe_indices,
                            scale_timeline_index))) {
                apply_layer_scalar(
                    &current_pose.scale_x,
                    setup_pose.scale_x,
                    scale->x,
                    additive,
                    use_current_base,
                    alpha);
                apply_layer_scalar(
                    &current_pose.scale_y,
                    setup_pose.scale_y,
                    scale->y,
                    additive,
                    use_current_base,
                    alpha);
            }
        }
        ++timeline_index;
    }

    for (std::size_t shear_timeline_index = 0;
         shear_timeline_index < from->animation->bone_shear_timelines.size();
         ++shear_timeline_index) {
        const BoneShearTimeline& timeline = from->animation->bone_shear_timelines[shear_timeline_index];
        AnimationStateTimelineMode mode = AnimationStateTimelineMode::First;
        std::shared_ptr<TrackEntry> hold_mix;
        (void)timeline_mode_at(*from, timeline_index, &mode, &hold_mix);
        double alpha = alpha_mix;
        switch (mode) {
        case AnimationStateTimelineMode::Subsequent:
        case AnimationStateTimelineMode::First:
            alpha = alpha_mix;
            break;
        case AnimationStateTimelineMode::HoldSubsequent:
        case AnimationStateTimelineMode::HoldFirst:
            alpha = alpha_hold;
            break;
        case AnimationStateTimelineMode::HoldMix:
            alpha = hold_mix_alpha(hold_mix, alpha_hold);
            break;
        }

        from->total_alpha += alpha;
        if (alpha > kEpsilon &&
            timeline.bone_index < bones.size() &&
            timeline.bone_index < data_->bones().size() &&
            track_affects_bone(*from, timeline.bone_index)) {
            BoneTransform& current_pose = bones[timeline.bone_index].local_pose;
            const BoneTransform& setup_pose = data_->bones()[timeline.bone_index].setup_pose;
            const bool use_current_base = mode_uses_current_base(mode);
            if (const std::optional<VectorSample> shear =
                    sample_shear_timeline(
                        timeline,
                        sample_time,
                        cursor_at(
                            &sampling_context.shear_last_keyframe_indices,
                            shear_timeline_index))) {
                apply_layer_scalar(
                    &current_pose.shear_x,
                    setup_pose.shear_x,
                    shear->x,
                    additive,
                    use_current_base,
                    alpha);
                apply_layer_scalar(
                    &current_pose.shear_y,
                    setup_pose.shear_y,
                    shear->y,
                    additive,
                    use_current_base,
                    alpha);
            }
        }
        ++timeline_index;
    }

    for (const SlotAttachmentTimeline& timeline : from->animation->slot_attachment_timelines) {
        (void)timeline;
        AnimationStateTimelineMode mode = AnimationStateTimelineMode::First;
        std::shared_ptr<TrackEntry> hold_mix;
        (void)timeline_mode_at(*from, timeline_index, &mode, &hold_mix);
        switch (mode) {
        case AnimationStateTimelineMode::Subsequent:
        case AnimationStateTimelineMode::First:
            from->total_alpha += alpha_mix;
            break;
        case AnimationStateTimelineMode::HoldSubsequent:
        case AnimationStateTimelineMode::HoldFirst:
            from->total_alpha += alpha_hold;
            break;
        case AnimationStateTimelineMode::HoldMix:
            from->total_alpha += hold_mix_alpha(hold_mix, alpha_hold);
            break;
        }
        ++timeline_index;
    }

    for (std::size_t color_timeline_index = 0;
         color_timeline_index < from->animation->slot_color_timelines.size();
         ++color_timeline_index) {
        const SlotColorTimeline& timeline = from->animation->slot_color_timelines[color_timeline_index];
        AnimationStateTimelineMode mode = AnimationStateTimelineMode::First;
        std::shared_ptr<TrackEntry> hold_mix;
        (void)timeline_mode_at(*from, timeline_index, &mode, &hold_mix);
        double alpha = alpha_mix;
        switch (mode) {
        case AnimationStateTimelineMode::Subsequent:
        case AnimationStateTimelineMode::First:
            alpha = alpha_mix;
            break;
        case AnimationStateTimelineMode::HoldSubsequent:
        case AnimationStateTimelineMode::HoldFirst:
            alpha = alpha_hold;
            break;
        case AnimationStateTimelineMode::HoldMix:
            alpha = hold_mix_alpha(hold_mix, alpha_hold);
            break;
        }

        from->total_alpha += alpha;
        if (alpha > kEpsilon &&
            timeline.slot_index < slots.size() &&
            timeline.slot_index < data_->slots().size()) {
            SlotColor& current_color = slots[timeline.slot_index].color;
            const SlotColor& setup_color = data_->slots()[timeline.slot_index].color;
            const bool use_current_base = mode_uses_current_base(mode);
            if (const std::optional<SlotColor> color =
                    sample_color_timeline(
                        timeline,
                        sample_time,
                        cursor_at(
                            &sampling_context.color_last_keyframe_indices,
                            color_timeline_index))) {
                current_color.r = blend_timeline_scalar(
                    current_color.r,
                    setup_color.r,
                    color->r,
                    use_current_base,
                    alpha);
                current_color.g = blend_timeline_scalar(
                    current_color.g,
                    setup_color.g,
                    color->g,
                    use_current_base,
                    alpha);
                current_color.b = blend_timeline_scalar(
                    current_color.b,
                    setup_color.b,
                    color->b,
                    use_current_base,
                    alpha);
                current_color.a = blend_timeline_scalar(
                    current_color.a,
                    setup_color.a,
                    color->a,
                    use_current_base,
                    alpha);
            }
        }
        ++timeline_index;
    }

    for (std::size_t deform_timeline_index = 0;
         deform_timeline_index < from->animation->mesh_deform_timelines.size();
         ++deform_timeline_index) {
        const MeshDeformTimeline& timeline = from->animation->mesh_deform_timelines[deform_timeline_index];
        AnimationStateTimelineMode mode = AnimationStateTimelineMode::First;
        std::shared_ptr<TrackEntry> hold_mix;
        (void)timeline_mode_at(*from, timeline_index, &mode, &hold_mix);
        double alpha = alpha_mix;
        switch (mode) {
        case AnimationStateTimelineMode::Subsequent:
        case AnimationStateTimelineMode::First:
            alpha = alpha_mix;
            break;
        case AnimationStateTimelineMode::HoldSubsequent:
        case AnimationStateTimelineMode::HoldFirst:
            alpha = alpha_hold;
            break;
        case AnimationStateTimelineMode::HoldMix:
            alpha = hold_mix_alpha(hold_mix, alpha_hold);
            break;
        }

        from->total_alpha += alpha;
        if (alpha > kEpsilon && timeline.slot_index < mesh_deforms.size()) {
            const AttachmentData* attachment = skeleton.current_attachment(timeline.slot_index);
            if (attachment != nullptr &&
                attachment->mesh_geometry != nullptr &&
                attachment_matches_mesh_deform_source(*attachment, timeline.attachment_name)) {
                const std::optional<std::vector<double>> sampled_offsets =
                    sample_deform_timeline(
                        timeline,
                        sample_time,
                        cursor_at(
                            &sampling_context.deform_last_keyframe_indices,
                            deform_timeline_index));
                if (sampled_offsets.has_value()) {
                    MeshDeformState& current_deform = mesh_deforms[timeline.slot_index];
                    if (current_deform.attachment_name != timeline.attachment_name ||
                        current_deform.vertex_offsets.size() != sampled_offsets->size()) {
                        current_deform.attachment_name = timeline.attachment_name;
                        current_deform.vertex_offsets.assign(sampled_offsets->size(), 0.0);
                    }

                    const bool use_current_base =
                        mode_uses_current_base(mode) &&
                        current_deform.attachment_name == timeline.attachment_name &&
                        current_deform.vertex_offsets.size() == sampled_offsets->size();
                    for (std::size_t component_index = 0;
                         component_index < sampled_offsets->size();
                         ++component_index) {
                        current_deform.vertex_offsets[component_index] = blend_timeline_scalar(
                            current_deform.vertex_offsets[component_index],
                            0.0,
                            (*sampled_offsets)[component_index],
                            use_current_base,
                            alpha);
                    }
                }
            }
        }
        ++timeline_index;
    }

    if (from->animation->draw_order_timeline_data.has_value()) {
        AnimationStateTimelineMode mode = AnimationStateTimelineMode::First;
        std::shared_ptr<TrackEntry> hold_mix;
        (void)timeline_mode_at(*from, timeline_index, &mode, &hold_mix);
        switch (mode) {
        case AnimationStateTimelineMode::Subsequent:
        case AnimationStateTimelineMode::First:
            from->total_alpha += alpha_mix;
            break;
        case AnimationStateTimelineMode::HoldSubsequent:
        case AnimationStateTimelineMode::HoldFirst:
            from->total_alpha += alpha_hold;
            break;
        case AnimationStateTimelineMode::HoldMix:
            from->total_alpha += hold_mix_alpha(hold_mix, alpha_hold);
            break;
        }
    }

    from->next_track_last = from->track_time;
    return mix;
}

void AnimationState::apply(Skeleton& skeleton) {
    apply_pose(skeleton);
    if (skeleton.visible()) {
        skeleton.update_world_transforms();
    }
}

void AnimationState::apply_pose(Skeleton& skeleton) {
    if (skeleton.data().get() != data_.get()) {
        throw std::invalid_argument(
            "AnimationState::apply requires a Skeleton created from the same SkeletonData");
    }
    if (!skeleton.visible()) {
        for (const std::shared_ptr<TrackEntry>& current : tracks_) {
            if (current == nullptr || current->delay > 0.0) {
                continue;
            }
            current->next_track_last = current->track_time;
        }
        return;
    }

    if (animations_changed_) {
        refresh_timeline_modes();
    }

    skeleton.prepare_animation_pose();
    skeleton.begin_track_sampling_frame(tracks_.size());

    for (std::size_t track_index = 0; track_index < tracks_.size(); ++track_index) {
        const std::shared_ptr<TrackEntry>& current = tracks_[track_index];
        if (current == nullptr || current->delay > 0.0) {
            continue;
        }

        double mix = track_alpha(*current);
        if (current->mixing_from != nullptr) {
            mix *= apply_mixing_from(current, skeleton);
        } else if (current->track_time >= current->track_end && current->next == nullptr) {
            mix = 0.0;
        }

        std::shared_ptr<TrackEntry> discrete_source = current;
        if (current->mixing_from != nullptr &&
            current->mix_duration > kEpsilon &&
            current->mix_time < current->mix_duration - kEpsilon) {
            discrete_source = current->mixing_from;
        }
        if (discrete_source != nullptr) {
            apply_discrete_timelines(skeleton, *discrete_source);
        }

        apply_current_timeline_values(skeleton, *current, mix, false);
        current->next_track_last = current->track_time;
        if (current->mixing_from != nullptr) {
            capture_entry_snapshot(current, skeleton);
        }
    }

    skeleton.end_track_sampling_frame(tracks_.size());
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

    const double previous_track_time =
        entry->track_last >= 0.0 ? entry->track_last : std::max(0.0, entry->last_track_time_);

    if (entry->loop) {
        const double previous_progress = std::max(0.0, previous_track_time);
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

    const double start_progress = std::clamp(previous_track_time, 0.0, duration);
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
