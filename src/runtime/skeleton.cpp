#include "skeleton_internal.hpp"

#include "marrow/runtime/animation_state.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace marrow::runtime {
namespace {

std::vector<std::vector<std::size_t>> build_children_map(const std::vector<BoneData>& bones) {
    std::vector<std::vector<std::size_t>> children_map(bones.size());
    for (std::size_t bone_index = 0; bone_index < bones.size(); ++bone_index) {
        const std::optional<std::size_t> parent_index = bones[bone_index].parent_index;
        if (!parent_index.has_value() || *parent_index >= children_map.size()) {
            continue;
        }

        children_map[*parent_index].push_back(bone_index);
    }

    return children_map;
}

std::vector<AttachmentVertex> build_bone_tip_local_vectors(
    const std::vector<BoneData>& bones,
    const std::vector<std::vector<std::size_t>>& children_map) {
    std::vector<AttachmentVertex> bone_tip_local_vectors(bones.size());
    for (std::size_t bone_index = 0; bone_index < children_map.size(); ++bone_index) {
        AttachmentVertex tip{};
        double best_length_squared = 0.0;
        for (const std::size_t child_index : children_map[bone_index]) {
            if (child_index >= bones.size()) {
                continue;
            }

            const BoneTransform& child_pose = bones[child_index].setup_pose;
            const double length_squared =
                (child_pose.x * child_pose.x) + (child_pose.y * child_pose.y);
            if (length_squared <= best_length_squared) {
                continue;
            }

            tip = AttachmentVertex{child_pose.x, child_pose.y};
            best_length_squared = length_squared;
        }

        bone_tip_local_vectors[bone_index] = tip;
    }

    return bone_tip_local_vectors;
}

std::vector<std::uint64_t> build_bone_subtree_word_masks(
    const std::vector<std::vector<std::size_t>>& children_map,
    std::size_t word_count) {
    const std::size_t bone_count = children_map.size();
    std::vector<std::uint64_t> masks(bone_count * word_count, 0U);
    for (std::size_t bone_index = bone_count; bone_index-- > 0U;) {
        std::uint64_t* const mask_words = masks.data() + bone_index * word_count;
        mask_words[bone_index / 64U] |= std::uint64_t{1} << (bone_index % 64U);
        for (const std::size_t child_index : children_map[bone_index]) {
            const std::uint64_t* const child_words = masks.data() + child_index * word_count;
            for (std::size_t word_index = 0; word_index < word_count; ++word_index) {
                mask_words[word_index] |= child_words[word_index];
            }
        }
    }

    return masks;
}

} // namespace

SkeletonData::SkeletonData(
    SkeletonInfo info,
    std::vector<BoneData> bones,
    std::vector<IkConstraintData> ik_constraints,
    std::vector<PathConstraintData> path_constraints,
    std::vector<TransformConstraintData> transform_constraints,
    std::vector<PhysicsConstraintData> physics_constraints,
    std::vector<SlotData> slots,
    std::vector<EventDefinition> events,
    std::vector<AnimationData> animations,
    std::vector<SkinData> skins,
    double default_mix_duration,
    std::vector<AnimationMixDefinition> mix_definitions)
    : info_(std::move(info)),
      bones_(std::move(bones)),
      ik_constraints_(std::move(ik_constraints)),
      path_constraints_(std::move(path_constraints)),
      transform_constraints_(std::move(transform_constraints)),
      physics_constraints_(std::move(physics_constraints)),
      slots_(std::move(slots)),
      events_(std::move(events)),
      animations_(std::move(animations)),
      skins_(std::move(skins)),
      default_skin_index_(detail::find_skin_index(skins_, "default")),
      default_mix_duration_(default_mix_duration),
      mix_definitions_(std::move(mix_definitions)) {
    detail::reorder_topologically(
        &bones_,
        &ik_constraints_,
        &path_constraints_,
        &transform_constraints_,
        &physics_constraints_,
        &slots_,
        &animations_,
        &skins_);
    for (AnimationData& animation : animations_) {
        animation.prune_constant_timelines(bones_, slots_);
        animation.rebuild_bone_timeline_index(bones_.size());
    }
    bone_evaluation_order_ = detail::build_bone_evaluation_order(bones_);
    children_map_ = build_children_map(bones_);
    bone_subtree_word_count_ = (bones_.size() + 63U) / 64U;
    bone_subtree_word_masks_ =
        build_bone_subtree_word_masks(children_map_, bone_subtree_word_count_);
    bone_tip_local_vectors_ = build_bone_tip_local_vectors(bones_, children_map_);
}

const SkeletonInfo& SkeletonData::info() const {
    return info_;
}

const std::vector<BoneData>& SkeletonData::bones() const {
    return bones_;
}

const std::vector<IkConstraintData>& SkeletonData::ik_constraints() const {
    return ik_constraints_;
}

const std::vector<PathConstraintData>& SkeletonData::path_constraints() const {
    return path_constraints_;
}

const std::vector<TransformConstraintData>& SkeletonData::transform_constraints() const {
    return transform_constraints_;
}

const std::vector<PhysicsConstraintData>& SkeletonData::physics_constraints() const {
    return physics_constraints_;
}

const std::vector<SlotData>& SkeletonData::slots() const {
    return slots_;
}

const std::vector<EventDefinition>& SkeletonData::events() const {
    return events_;
}

const std::vector<AnimationData>& SkeletonData::animations() const {
    return animations_;
}

const std::vector<SkinData>& SkeletonData::skins() const {
    return skins_;
}

const std::vector<AnimationMixDefinition>& SkeletonData::mix_definitions() const {
    return mix_definitions_;
}

const std::vector<std::size_t>& SkeletonData::bone_evaluation_order() const {
    return bone_evaluation_order_;
}

const std::vector<std::vector<std::size_t>>& SkeletonData::children_map() const {
    return children_map_;
}

const std::vector<AttachmentVertex>& SkeletonData::bone_tip_local_vectors() const {
    return bone_tip_local_vectors_;
}

std::optional<std::size_t> SkeletonData::default_skin_index() const {
    return default_skin_index_;
}

double SkeletonData::default_mix_duration() const {
    return default_mix_duration_;
}

std::optional<std::size_t> SkeletonData::find_bone_index(std::string_view name) const {
    return detail::find_bone_index(bones_, name);
}

std::optional<std::size_t> SkeletonData::find_slot_index(std::string_view name) const {
    return detail::find_slot_index(slots_, name);
}

std::optional<std::size_t> SkeletonData::find_skin_index(std::string_view name) const {
    return detail::find_skin_index(skins_, name);
}

const AnimationData* SkeletonData::find_animation(std::string_view name) const {
    return detail::find_animation(animations_, name);
}

const SkinData* SkeletonData::find_skin(std::string_view name) const {
    const auto skin_index = find_skin_index(name);
    if (!skin_index.has_value()) {
        return nullptr;
    }

    return &skins_[*skin_index];
}

const AttachmentData* SkeletonData::find_attachment(
    std::size_t skin_index,
    std::size_t slot_index) const {
    if (skin_index >= skins_.size()) {
        return nullptr;
    }

    return skins_[skin_index].find_attachment(slot_index);
}

const AttachmentData* SkeletonData::find_attachment(
    std::size_t skin_index,
    std::size_t slot_index,
    std::string_view attachment_name) const {
    if (skin_index >= skins_.size()) {
        return nullptr;
    }

    return skins_[skin_index].find_attachment(slot_index, attachment_name);
}

const AttachmentData* SkeletonData::find_attachment(
    std::string_view skin_name,
    std::size_t slot_index) const {
    const auto skin_index = find_skin_index(skin_name);
    if (!skin_index.has_value()) {
        return nullptr;
    }

    return find_attachment(*skin_index, slot_index);
}

const AttachmentData* SkeletonData::find_attachment(
    std::string_view skin_name,
    std::size_t slot_index,
    std::string_view attachment_name) const {
    const auto skin_index = find_skin_index(skin_name);
    if (!skin_index.has_value()) {
        return nullptr;
    }

    return find_attachment(*skin_index, slot_index, attachment_name);
}

const AttachmentData* SkeletonData::find_attachment_source(
    std::size_t slot_index,
    std::string_view attachment_name,
    std::optional<std::size_t>* skin_index_out) const {
    return detail::find_attachment_source_in_skins(
        skins_,
        default_skin_index_,
        slot_index,
        attachment_name,
        skin_index_out);
}

double SkeletonData::mix_duration(
    std::string_view from_animation,
    std::string_view to_animation) const {
    const auto exact = std::find_if(
        mix_definitions_.begin(),
        mix_definitions_.end(),
        [&](const AnimationMixDefinition& mix_definition) {
            return !mix_definition.from_any &&
                mix_definition.from_animation == from_animation &&
                mix_definition.to_animation == to_animation;
        });
    if (exact != mix_definitions_.end()) {
        return exact->duration;
    }

    const auto wildcard = std::find_if(
        mix_definitions_.begin(),
        mix_definitions_.end(),
        [&](const AnimationMixDefinition& mix_definition) {
            return mix_definition.from_any &&
                mix_definition.to_animation == to_animation;
        });
    if (wildcard != mix_definitions_.end()) {
        return wildcard->duration;
    }

    return default_mix_duration_;
}

Skeleton::Skeleton(std::shared_ptr<const SkeletonData> data)
    : data_(std::move(data)) {
    if (data_ == nullptr) {
        throw std::invalid_argument("Skeleton requires SkeletonData");
    }

    bone_poses_.resize(data_->bones().size());
    slot_states_.resize(data_->slots().size());
    mesh_deform_states_.resize(data_->slots().size());
    draw_order_.resize(data_->slots().size());
    physics_constraint_states_.resize(data_->physics_constraints().size());

    update_active_skin_scopes({});
    set_to_setup_pose();
}

const std::shared_ptr<const SkeletonData>& Skeleton::data() const {
    return data_;
}

void Skeleton::set_error_callback(SkeletonErrorCallback callback) {
    error_callback_ = std::move(callback);
}

const std::optional<std::string>& Skeleton::last_error() const {
    return last_error_;
}

void Skeleton::clear_last_error() {
    last_error_.reset();
}

void Skeleton::set_scale(double scale_x, double scale_y) {
    scale_x_ = scale_x;
    scale_y_ = scale_y;
    reset_update_throttle_state();
}

double Skeleton::scale_x() const {
    return scale_x_;
}

double Skeleton::scale_y() const {
    return scale_y_;
}

void Skeleton::set_visible(bool visible) {
    if (visible_ == visible) {
        return;
    }

    visible_ = visible;
    reset_update_throttle_state();
}

bool Skeleton::visible() const {
    return visible_;
}

void Skeleton::set_update_interval(std::size_t interval) {
    update_interval_ = std::max<std::size_t>(1, interval);
    reset_update_throttle_state();
}

std::size_t Skeleton::update_interval() const {
    return update_interval_;
}

SamplingContext& Skeleton::standalone_sampling_context(
    const AnimationData& animation,
    double sample_time) {
    animation.prepare_sampling_context(&standalone_sampling_context_, sample_time);
    return standalone_sampling_context_;
}

void Skeleton::begin_track_sampling_frame(std::size_t track_count) {
    ++sampling_generation_;
    if (track_sampling_states_.size() < track_count) {
        track_sampling_states_.resize(track_count);
    }
}

SamplingContext& Skeleton::track_sampling_context(
    std::size_t track_index,
    const void* owner,
    const AnimationData& animation,
    double sample_time) {
    if (track_index >= track_sampling_states_.size()) {
        track_sampling_states_.resize(track_index + 1);
    }

    auto& entry_contexts = track_sampling_states_[track_index].entry_contexts;
    const auto context_it = std::find_if(
        entry_contexts.begin(),
        entry_contexts.end(),
        [&](const TrackSamplingState::EntrySamplingContext& entry_context) {
            return entry_context.owner == owner;
        });
    if (context_it == entry_contexts.end()) {
        entry_contexts.push_back(TrackSamplingState::EntrySamplingContext{});
        entry_contexts.back().owner = owner;
        entry_contexts.back().generation = sampling_generation_;
        animation.prepare_sampling_context(&entry_contexts.back().sampling, sample_time);
        return entry_contexts.back().sampling;
    }

    context_it->generation = sampling_generation_;
    animation.prepare_sampling_context(&context_it->sampling, sample_time);
    return context_it->sampling;
}

void Skeleton::end_track_sampling_frame(std::size_t active_track_count) {
    if (track_sampling_states_.size() > active_track_count) {
        for (std::size_t track_index = active_track_count; track_index < track_sampling_states_.size();
             ++track_index) {
            track_sampling_states_[track_index].entry_contexts.clear();
        }
    }

    for (std::size_t track_index = 0;
         track_index < std::min(track_sampling_states_.size(), active_track_count);
         ++track_index) {
        auto& entry_contexts = track_sampling_states_[track_index].entry_contexts;
        entry_contexts.erase(
            std::remove_if(
                entry_contexts.begin(),
                entry_contexts.end(),
                [&](const TrackSamplingState::EntrySamplingContext& entry_context) {
                    return entry_context.generation != sampling_generation_;
                }),
            entry_contexts.end());
    }
}

std::size_t Skeleton::constraint_allocation_count() const {
    return constraint_allocation_count_;
}

const std::vector<BonePose>& Skeleton::bone_poses() const {
    return bone_poses_;
}

std::vector<BonePose>& Skeleton::bone_poses() {
    return bone_poses_;
}

BoneWorldTransformsView Skeleton::bone_world_transforms() const {
    return BoneWorldTransformsView(
        &bone_world_a_,
        &bone_world_b_,
        &bone_world_c_,
        &bone_world_d_,
        &bone_world_x_,
        &bone_world_y_,
        bone_world_x_.size());
}

const std::vector<SlotState>& Skeleton::slot_states() const {
    return slot_states_;
}

std::vector<SlotState>& Skeleton::slot_states() {
    return slot_states_;
}

const std::vector<MeshDeformState>& Skeleton::mesh_deform_states() const {
    return mesh_deform_states_;
}

std::vector<MeshDeformState>& Skeleton::mesh_deform_states() {
    return mesh_deform_states_;
}

const std::vector<std::size_t>& Skeleton::draw_order() const {
    return draw_order_;
}

std::vector<std::size_t>& Skeleton::draw_order() {
    return draw_order_;
}

void update_instance(
    Skeleton& skeleton,
    AnimationState& animation_state,
    double delta_seconds) {
    if (delta_seconds < 0.0) {
        throw std::invalid_argument("update_instance requires a non-negative delta");
    }
    if (skeleton.data().get() != animation_state.data().get()) {
        throw std::invalid_argument(
            "update_instance requires Skeleton and AnimationState from the same SkeletonData");
    }
    if (!skeleton.visible_) {
        return;
    }

    Skeleton::UpdateThrottleState& throttle = skeleton.update_throttle_state_;
    if (skeleton.update_interval_ <= 1U) {
        throttle.pending_delta_seconds = 0.0;
        throttle.frames_since_update = 0;
        throttle.has_prediction = false;
        throttle.dirty = false;
        animation_state.update(delta_seconds);
        animation_state.apply(skeleton);
        return;
    }

    throttle.pending_delta_seconds += delta_seconds;
    const bool perform_full_update =
        throttle.dirty ||
        !throttle.has_prediction ||
        (throttle.frames_since_update + 1U >= skeleton.update_interval_);

    if (perform_full_update) {
        const double full_update_delta = throttle.pending_delta_seconds;
        const double predicted_delta = throttle.has_full_update_history
            ? full_update_delta
            : delta_seconds * static_cast<double>(skeleton.update_interval_);

        animation_state.update(full_update_delta);
        animation_state.apply(skeleton);

        throttle.pending_delta_seconds = 0.0;
        throttle.frames_since_update = 0U;
        throttle.last_full_update_delta_seconds = full_update_delta;
        throttle.has_full_update_history = true;
        throttle.dirty = false;

        skeleton.capture_display_state(&throttle.source_snapshot);
        skeleton.rebuild_predicted_display_state(animation_state, predicted_delta);
        return;
    }

    ++throttle.frames_since_update;
    skeleton.apply_interpolated_display_state(
        static_cast<double>(throttle.frames_since_update) /
        static_cast<double>(skeleton.update_interval_));
}

void Skeleton::reset_update_throttle_state() {
    update_throttle_state_.frames_since_update = 0;
    update_throttle_state_.pending_delta_seconds = 0.0;
    update_throttle_state_.last_full_update_delta_seconds = 0.0;
    update_throttle_state_.has_full_update_history = false;
    update_throttle_state_.has_prediction = false;
    update_throttle_state_.dirty = true;
    update_throttle_state_.source_snapshot = {};
    update_throttle_state_.target_snapshot = {};
}

void Skeleton::capture_display_state(DisplayStateSnapshot* snapshot) const {
    if (snapshot == nullptr) {
        return;
    }

    snapshot->bone_poses = bone_poses_;
    snapshot->bone_world_a = bone_world_a_;
    snapshot->bone_world_b = bone_world_b_;
    snapshot->bone_world_c = bone_world_c_;
    snapshot->bone_world_d = bone_world_d_;
    snapshot->bone_world_x = bone_world_x_;
    snapshot->bone_world_y = bone_world_y_;
    snapshot->slot_states = slot_states_;
    snapshot->mesh_deform_states = mesh_deform_states_;
    snapshot->draw_order = draw_order_;
    snapshot->attachment_playback_time = attachment_playback_time_;
}

void Skeleton::apply_display_state(const DisplayStateSnapshot& snapshot) {
    bone_poses_ = snapshot.bone_poses;
    bone_world_a_ = snapshot.bone_world_a;
    bone_world_b_ = snapshot.bone_world_b;
    bone_world_c_ = snapshot.bone_world_c;
    bone_world_d_ = snapshot.bone_world_d;
    bone_world_x_ = snapshot.bone_world_x;
    bone_world_y_ = snapshot.bone_world_y;
    slot_states_ = snapshot.slot_states;
    mesh_deform_states_ = snapshot.mesh_deform_states;
    draw_order_ = snapshot.draw_order;
    attachment_playback_time_ = std::max(0.0, snapshot.attachment_playback_time);
}

void Skeleton::apply_interpolated_display_state(double alpha) {
    if (!update_throttle_state_.has_prediction) {
        return;
    }

    const DisplayStateSnapshot& source = update_throttle_state_.source_snapshot;
    const DisplayStateSnapshot& target = update_throttle_state_.target_snapshot;
    const double clamped_alpha = std::clamp(alpha, 0.0, 1.0);

    if (bone_poses_.size() != source.bone_poses.size() ||
        source.bone_poses.size() != target.bone_poses.size()) {
        bone_poses_ = source.bone_poses;
    } else {
        for (std::size_t bone_index = 0; bone_index < bone_poses_.size(); ++bone_index) {
            BonePose& pose_state = bone_poses_[bone_index];
            const BonePose& source_pose_state = source.bone_poses[bone_index];
            const BonePose& target_pose_state = target.bone_poses[bone_index];
            pose_state.inherit = source_pose_state.inherit;

            BoneTransform& pose = pose_state.local_pose;
            const BoneTransform& source_pose = source_pose_state.local_pose;
            const BoneTransform& target_pose = target_pose_state.local_pose;
            pose.x = detail::mix_scalar(source_pose.x, target_pose.x, clamped_alpha);
            pose.y = detail::mix_scalar(source_pose.y, target_pose.y, clamped_alpha);
            pose.rotation =
                detail::mix_rotation_degrees(source_pose.rotation, target_pose.rotation, clamped_alpha);
            pose.scale_x = detail::mix_scalar(source_pose.scale_x, target_pose.scale_x, clamped_alpha);
            pose.scale_y = detail::mix_scalar(source_pose.scale_y, target_pose.scale_y, clamped_alpha);
            pose.shear_x = detail::mix_scalar(source_pose.shear_x, target_pose.shear_x, clamped_alpha);
            pose.shear_y = detail::mix_scalar(source_pose.shear_y, target_pose.shear_y, clamped_alpha);
        }
    }

    if (bone_world_a_.size() != source.bone_world_a.size() ||
        source.bone_world_a.size() != target.bone_world_a.size()) {
        bone_world_a_ = source.bone_world_a;
        bone_world_b_ = source.bone_world_b;
        bone_world_c_ = source.bone_world_c;
        bone_world_d_ = source.bone_world_d;
        bone_world_x_ = source.bone_world_x;
        bone_world_y_ = source.bone_world_y;
    } else {
        for (std::size_t bone_index = 0; bone_index < bone_world_a_.size(); ++bone_index) {
            bone_world_a_[bone_index] =
                static_cast<float>(detail::mix_scalar(
                    source.bone_world_a[bone_index],
                    target.bone_world_a[bone_index],
                    clamped_alpha));
            bone_world_b_[bone_index] =
                static_cast<float>(detail::mix_scalar(
                    source.bone_world_b[bone_index],
                    target.bone_world_b[bone_index],
                    clamped_alpha));
            bone_world_c_[bone_index] =
                static_cast<float>(detail::mix_scalar(
                    source.bone_world_c[bone_index],
                    target.bone_world_c[bone_index],
                    clamped_alpha));
            bone_world_d_[bone_index] =
                static_cast<float>(detail::mix_scalar(
                    source.bone_world_d[bone_index],
                    target.bone_world_d[bone_index],
                    clamped_alpha));
            bone_world_x_[bone_index] =
                static_cast<float>(detail::mix_scalar(
                    source.bone_world_x[bone_index],
                    target.bone_world_x[bone_index],
                    clamped_alpha));
            bone_world_y_[bone_index] =
                static_cast<float>(detail::mix_scalar(
                    source.bone_world_y[bone_index],
                    target.bone_world_y[bone_index],
                    clamped_alpha));
        }
    }

    if (slot_states_.size() != source.slot_states.size() ||
        source.slot_states.size() != target.slot_states.size()) {
        slot_states_ = source.slot_states;
    } else {
        for (std::size_t slot_index = 0; slot_index < slot_states_.size(); ++slot_index) {
            SlotState& slot = slot_states_[slot_index];
            const SlotState& source_slot = source.slot_states[slot_index];
            const SlotState& target_slot = target.slot_states[slot_index];
            slot.attachment_name = source_slot.attachment_name;
            slot.attachment_skin_index = source_slot.attachment_skin_index;
            slot.dark_color = source_slot.dark_color;
            slot.color.r = detail::mix_scalar(source_slot.color.r, target_slot.color.r, clamped_alpha);
            slot.color.g = detail::mix_scalar(source_slot.color.g, target_slot.color.g, clamped_alpha);
            slot.color.b = detail::mix_scalar(source_slot.color.b, target_slot.color.b, clamped_alpha);
            slot.color.a = detail::mix_scalar(source_slot.color.a, target_slot.color.a, clamped_alpha);
        }
    }

    if (mesh_deform_states_.size() != source.mesh_deform_states.size() ||
        source.mesh_deform_states.size() != target.mesh_deform_states.size()) {
        mesh_deform_states_ = source.mesh_deform_states;
    } else {
        for (std::size_t slot_index = 0; slot_index < mesh_deform_states_.size(); ++slot_index) {
            MeshDeformState& deform = mesh_deform_states_[slot_index];
            const MeshDeformState& source_deform = source.mesh_deform_states[slot_index];
            const MeshDeformState& target_deform = target.mesh_deform_states[slot_index];
            deform.attachment_name = source_deform.attachment_name;
            if (source_deform.vertex_offsets.size() != target_deform.vertex_offsets.size()) {
                deform.vertex_offsets = source_deform.vertex_offsets;
                continue;
            }

            if (deform.vertex_offsets.size() != source_deform.vertex_offsets.size()) {
                deform.vertex_offsets.resize(source_deform.vertex_offsets.size());
            }
            for (std::size_t component_index = 0;
                 component_index < deform.vertex_offsets.size();
                 ++component_index) {
                deform.vertex_offsets[component_index] = detail::mix_scalar(
                    source_deform.vertex_offsets[component_index],
                    target_deform.vertex_offsets[component_index],
                    clamped_alpha);
            }
        }
    }

    attachment_playback_time_ = detail::mix_scalar(
        source.attachment_playback_time,
        target.attachment_playback_time,
        clamped_alpha);
    if (draw_order_.size() != source.draw_order.size()) {
        draw_order_ = source.draw_order;
    } else {
        std::copy(source.draw_order.begin(), source.draw_order.end(), draw_order_.begin());
    }
}

void Skeleton::rebuild_predicted_display_state(
    const AnimationState& animation_state,
    double predicted_delta_seconds) {
    update_throttle_state_.target_snapshot = update_throttle_state_.source_snapshot;

    if (predicted_delta_seconds <= 0.0) {
        update_throttle_state_.has_prediction = true;
        return;
    }

    Skeleton predicted_skeleton = *this;
    AnimationState predicted_state(data_);
    predicted_state.restore_state(animation_state.capture_state());
    predicted_state.update(predicted_delta_seconds);
    predicted_state.apply(predicted_skeleton);
    predicted_skeleton.capture_display_state(&update_throttle_state_.target_snapshot);
    update_throttle_state_.has_prediction = true;
}

void SkeletonBounds::update(const Skeleton& skeleton, bool compute_aabb) {
    bounding_boxes_.clear();
    last_bounding_box_index_.reset();
    has_aabb_ = false;
    min_x_ = 0.0;
    min_y_ = 0.0;
    max_x_ = 0.0;
    max_y_ = 0.0;

    const std::size_t slot_count = skeleton.slot_states().size();
    for (std::size_t slot_index = 0; slot_index < slot_count; ++slot_index) {
        const std::optional<BoundingBoxAttachmentPose> pose =
            skeleton.evaluate_current_bounding_box_attachment(slot_index);
        if (!pose.has_value()) {
            continue;
        }

        bounding_boxes_.push_back(*pose);
        if (!compute_aabb || pose->polygon.empty()) {
            continue;
        }

        for (const AttachmentVertex& vertex : pose->polygon) {
            const double vertex_x = vertex.x;
            const double vertex_y = vertex.y;
            if (!has_aabb_) {
                min_x_ = max_x_ = vertex_x;
                min_y_ = max_y_ = vertex_y;
                has_aabb_ = true;
                continue;
            }

            min_x_ = std::min(min_x_, vertex_x);
            min_y_ = std::min(min_y_, vertex_y);
            max_x_ = std::max(max_x_, vertex_x);
            max_y_ = std::max(max_y_, vertex_y);
        }
    }
}

bool SkeletonBounds::contains_point(double x, double y) {
    return contains_point(x, y, nullptr);
}

bool SkeletonBounds::contains_point(
    double x,
    double y,
    const BoundingBoxAttachmentPose** bounding_box) {
    if (bounding_box != nullptr) {
        *bounding_box = nullptr;
    }
    last_bounding_box_index_.reset();

    for (std::size_t index = 0; index < bounding_boxes_.size(); ++index) {
        if (!detail::polygon_contains_point(bounding_boxes_[index].polygon, x, y)) {
            continue;
        }

        last_bounding_box_index_ = index;
        if (bounding_box != nullptr) {
            *bounding_box = &bounding_boxes_[index];
        }
        return true;
    }

    return false;
}

bool SkeletonBounds::intersects_segment(double x1, double y1, double x2, double y2) {
    return intersects_segment(x1, y1, x2, y2, nullptr);
}

bool SkeletonBounds::intersects_segment(
    double x1,
    double y1,
    double x2,
    double y2,
    const BoundingBoxAttachmentPose** bounding_box) {
    if (bounding_box != nullptr) {
        *bounding_box = nullptr;
    }
    last_bounding_box_index_.reset();

    const AttachmentVertex start{x1, y1};
    const AttachmentVertex end{x2, y2};
    for (std::size_t index = 0; index < bounding_boxes_.size(); ++index) {
        if (!detail::polygon_intersects_segment(bounding_boxes_[index].polygon, start, end)) {
            continue;
        }

        last_bounding_box_index_ = index;
        if (bounding_box != nullptr) {
            *bounding_box = &bounding_boxes_[index];
        }
        return true;
    }

    return false;
}

const BoundingBoxAttachmentPose* SkeletonBounds::get_bounding_box() const {
    if (!last_bounding_box_index_.has_value() ||
        *last_bounding_box_index_ >= bounding_boxes_.size()) {
        return nullptr;
    }

    return &bounding_boxes_[*last_bounding_box_index_];
}

const std::vector<BoundingBoxAttachmentPose>& SkeletonBounds::bounding_boxes() const {
    return bounding_boxes_;
}

const std::vector<AttachmentVertex>* SkeletonBounds::get_polygon(
    std::size_t bounding_box_index) const {
    if (bounding_box_index >= bounding_boxes_.size()) {
        return nullptr;
    }

    return &bounding_boxes_[bounding_box_index].polygon;
}

const std::vector<AttachmentVertex>* SkeletonBounds::get_polygon(
    std::string_view attachment_name) const {
    const auto it = std::find_if(
        bounding_boxes_.begin(),
        bounding_boxes_.end(),
        [&](const BoundingBoxAttachmentPose& bounding_box) {
            return bounding_box.attachment_name == attachment_name;
        });
    if (it == bounding_boxes_.end()) {
        return nullptr;
    }

    return &it->polygon;
}

bool SkeletonBounds::has_aabb() const {
    return has_aabb_;
}

double SkeletonBounds::min_x() const {
    return min_x_;
}

double SkeletonBounds::min_y() const {
    return min_y_;
}

double SkeletonBounds::max_x() const {
    return max_x_;
}

double SkeletonBounds::max_y() const {
    return max_y_;
}

} // namespace marrow::runtime
