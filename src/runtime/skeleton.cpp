#include "skeleton_internal.hpp"

#include <algorithm>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace marrow::runtime {

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
      bone_evaluation_order_(detail::build_bone_evaluation_order(bones_)),
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
      mix_definitions_(std::move(mix_definitions)) {}

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
    std::string_view skin_name,
    std::size_t slot_index) const {
    const auto skin_index = find_skin_index(skin_name);
    if (!skin_index.has_value()) {
        return nullptr;
    }

    return find_attachment(*skin_index, slot_index);
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
    bone_world_transforms_.resize(data_->bones().size());
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
}

double Skeleton::scale_x() const {
    return scale_x_;
}

double Skeleton::scale_y() const {
    return scale_y_;
}

const std::vector<BonePose>& Skeleton::bone_poses() const {
    return bone_poses_;
}

std::vector<BonePose>& Skeleton::bone_poses() {
    return bone_poses_;
}

const std::vector<BoneWorldTransform>& Skeleton::bone_world_transforms() const {
    return bone_world_transforms_;
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
            if (!has_aabb_) {
                min_x_ = max_x_ = vertex.x;
                min_y_ = max_y_ = vertex.y;
                has_aabb_ = true;
                continue;
            }

            min_x_ = std::min(min_x_, vertex.x);
            min_y_ = std::min(min_y_, vertex.y);
            max_x_ = std::max(max_x_, vertex.x);
            max_y_ = std::max(max_y_, vertex.y);
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
