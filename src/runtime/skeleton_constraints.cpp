#include "skeleton_internal.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace marrow::runtime {
namespace {

std::runtime_error invalid_transform_constraint_bone_error(
    std::string_view constraint_name,
    std::string_view role,
    std::size_t bone_index,
    std::size_t data_bone_count,
    std::size_t pose_bone_count,
    std::size_t world_bone_count) {
    return std::runtime_error(
        "Transform constraint '" + std::string(constraint_name) + "' has invalid " +
        std::string(role) + " bone index " + std::to_string(bone_index) +
        " (bones=" + std::to_string(data_bone_count) +
        ", poses=" + std::to_string(pose_bone_count) +
        ", world=" + std::to_string(world_bone_count) + ")");
}

} // namespace

void Skeleton::update_world_transforms(PhysicsMode physics) {
    if (bone_world_transforms_.size() != bone_poses_.size()) {
        bone_world_transforms_.resize(bone_poses_.size());
    }

    std::vector<BonePose> solved_poses = bone_poses_;
    const auto bone_is_active = [&](std::size_t bone_index) {
        return bone_index < active_bones_.size() ? active_bones_[bone_index] : true;
    };

    const auto compute_world_transforms = [&]() {
        for (const std::size_t bone_index : data_->bone_evaluation_order()) {
            const BonePose& pose = solved_poses[bone_index];
            const BoneData& bone_data = data_->bones()[bone_index];
            if (!bone_data.parent_index.has_value()) {
                bone_world_transforms_[bone_index] =
                    detail::root_world_transform(pose.local_pose, scale_x_, scale_y_);
            } else {
                bone_world_transforms_[bone_index] = detail::compose_world_transform(
                    bone_world_transforms_[*bone_data.parent_index],
                    pose,
                    scale_x_,
                    scale_y_);
            }
        }
    };

    const auto bone_tip_local_vector = [&](std::size_t bone_index) -> AttachmentVertex {
        AttachmentVertex tip;
        double best_length_squared = 0.0;
        for (std::size_t child_index = 0; child_index < data_->bones().size(); ++child_index) {
            if (data_->bones()[child_index].parent_index != std::optional<std::size_t>{bone_index}) {
                continue;
            }

            const BoneTransform& child_pose = solved_poses[child_index].local_pose;
            const double length_squared =
                (child_pose.x * child_pose.x) + (child_pose.y * child_pose.y);
            if (length_squared <= best_length_squared) {
                continue;
            }

            tip = AttachmentVertex{child_pose.x, child_pose.y};
            best_length_squared = length_squared;
        }

        return tip;
    };

    const auto to_parent_local = [&](std::optional<std::size_t> parent_index,
                                     double world_x,
                                     double world_y) -> AttachmentVertex {
        if (!parent_index.has_value()) {
            return {world_x, world_y};
        }

        return detail::inverse_transform_point(bone_world_transforms_[*parent_index], world_x, world_y);
    };

    compute_world_transforms();

    constexpr double kEpsilon = 1e-8;
    for (std::size_t constraint_index = 0;
         constraint_index < data_->path_constraints().size();
         ++constraint_index) {
        if (constraint_index < active_path_constraints_.size() &&
            !active_path_constraints_[constraint_index]) {
            continue;
        }

        const PathConstraintData& constraint = data_->path_constraints()[constraint_index];
        const double translate_mix = detail::clamp_mix(constraint.translate_mix);
        const double rotate_mix = detail::clamp_mix(constraint.rotate_mix);
        if ((translate_mix <= 0.0 && rotate_mix <= 0.0) ||
            constraint.slot_index >= slot_states_.size() ||
            constraint.slot_index >= data_->slots().size()) {
            continue;
        }

        const AttachmentData* attachment = current_attachment(constraint.slot_index);
        if (attachment == nullptr || !attachment->path_attachment.has_value()) {
            continue;
        }

        const std::size_t path_bone_index = data_->slots()[constraint.slot_index].bone_index;
        if (path_bone_index >= bone_world_transforms_.size()) {
            continue;
        }

        std::vector<AttachmentVertex> world_control_points;
        world_control_points.reserve(attachment->path_attachment->control_points.size());
        const BoneWorldTransform& path_bone_transform = bone_world_transforms_[path_bone_index];
        for (const AttachmentVertex& point : attachment->path_attachment->control_points) {
            world_control_points.push_back(detail::transform_attachment_vertex(
                path_bone_transform,
                point.x,
                point.y));
        }

        const std::vector<detail::PathDistanceSample> path_samples =
            detail::build_path_distance_samples(world_control_points);
        if (path_samples.empty()) {
            continue;
        }

        const double total_length = path_samples.back().distance;
        const double spacing_distance =
            constraint.spacing_mode == PathConstraintSpacingMode::Percent
            ? total_length * constraint.spacing
            : constraint.spacing;

        for (std::size_t chain_index = 0;
             chain_index < constraint.bone_indices.size();
             ++chain_index) {
            const std::size_t bone_index = constraint.bone_indices[chain_index];
            if (bone_index >= solved_poses.size() || !bone_is_active(bone_index)) {
                continue;
            }

            const double sample_distance = std::clamp(
                (constraint.position * total_length) +
                    (spacing_distance * static_cast<double>(chain_index)),
                0.0,
                total_length);
            const detail::PathDistanceSample sample =
                detail::sample_path_distance(path_samples, sample_distance);
            BoneTransform& pose = solved_poses[bone_index].local_pose;
            const std::optional<std::size_t> parent_index = data_->bones()[bone_index].parent_index;

            if (translate_mix > 0.0) {
                const AttachmentVertex target_local =
                    to_parent_local(parent_index, sample.point.x, sample.point.y);
                pose.x += (target_local.x - pose.x) * translate_mix;
                pose.y += (target_local.y - pose.y) * translate_mix;
            }

            if (rotate_mix > 0.0 && detail::vertex_length(sample.tangent) > kEpsilon) {
                const AttachmentVertex tangent_end_world =
                    detail::add_vertices(sample.point, sample.tangent);
                const AttachmentVertex tangent_origin_local =
                    to_parent_local(parent_index, sample.point.x, sample.point.y);
                const AttachmentVertex tangent_end_local = to_parent_local(
                    parent_index,
                    tangent_end_world.x,
                    tangent_end_world.y);
                const AttachmentVertex tangent_local = detail::subtract_vertices(
                    tangent_end_local,
                    tangent_origin_local);
                if (detail::vertex_length(tangent_local) > kEpsilon) {
                    const double desired_rotation =
                        detail::radians_to_degrees(std::atan2(tangent_local.y, tangent_local.x)) -
                        pose.shear_x;
                    pose.rotation = detail::mix_rotation_degrees(
                        pose.rotation,
                        desired_rotation,
                        rotate_mix);
                }
            }

            compute_world_transforms();
        }
    }

    for (std::size_t constraint_index = 0;
         constraint_index < data_->transform_constraints().size();
         ++constraint_index) {
        if (constraint_index < active_transform_constraints_.size() &&
            !active_transform_constraints_[constraint_index]) {
            continue;
        }

        const TransformConstraintData& constraint =
            data_->transform_constraints()[constraint_index];
        const auto require_valid_constraint_bone_index =
            [&](std::size_t bone_index, std::string_view role) {
                if (bone_index < data_->bones().size() &&
                    bone_index < solved_poses.size() &&
                    bone_index < bone_world_transforms_.size()) {
                    return;
                }

                throw invalid_transform_constraint_bone_error(
                    constraint.name,
                    role,
                    bone_index,
                    data_->bones().size(),
                    solved_poses.size(),
                    bone_world_transforms_.size());
            };
        require_valid_constraint_bone_index(constraint.source_bone_index, "source");
        if (!bone_is_active(constraint.source_bone_index)) {
            continue;
        }

        const double rotate_mix = detail::clamp_mix(constraint.rotate_mix);
        const double translate_mix = detail::clamp_mix(constraint.translate_mix);
        const double scale_mix = detail::clamp_mix(constraint.scale_mix);
        const double shear_mix = detail::clamp_mix(constraint.shear_mix);
        if (rotate_mix <= 0.0 && translate_mix <= 0.0 &&
            scale_mix <= 0.0 && shear_mix <= 0.0) {
            continue;
        }

        const BoneTransform source_pose = solved_poses[constraint.source_bone_index].local_pose;
        const BoneWorldTransform source_world =
            bone_world_transforms_[constraint.source_bone_index];
        for (const std::size_t bone_index : constraint.target_bone_indices) {
            require_valid_constraint_bone_index(bone_index, "target");
            if (!bone_is_active(bone_index)) {
                continue;
            }

            BoneTransform& pose = solved_poses[bone_index].local_pose;
            if (rotate_mix > 0.0) {
                const double desired_rotation =
                    source_pose.rotation + constraint.offsets.rotation;
                pose.rotation = detail::mix_rotation_degrees(
                    pose.rotation,
                    desired_rotation,
                    rotate_mix);
            }
            if (translate_mix > 0.0) {
                const AttachmentVertex target_local = to_parent_local(
                    data_->bones()[bone_index].parent_index,
                    source_world.world_x + constraint.offsets.x,
                    source_world.world_y + constraint.offsets.y);
                pose.x = detail::mix_scalar(pose.x, target_local.x, translate_mix);
                pose.y = detail::mix_scalar(pose.y, target_local.y, translate_mix);
            }
            if (scale_mix > 0.0) {
                pose.scale_x = detail::mix_scalar(
                    pose.scale_x,
                    source_pose.scale_x + constraint.offsets.scale_x,
                    scale_mix);
                pose.scale_y = detail::mix_scalar(
                    pose.scale_y,
                    source_pose.scale_y + constraint.offsets.scale_y,
                    scale_mix);
            }
            if (shear_mix > 0.0) {
                pose.shear_x = detail::mix_scalar(
                    pose.shear_x,
                    source_pose.shear_x + constraint.offsets.shear_x,
                    shear_mix);
                pose.shear_y = detail::mix_scalar(
                    pose.shear_y,
                    source_pose.shear_y + constraint.offsets.shear_y,
                    shear_mix);
            }
        }

        compute_world_transforms();
    }

    constexpr double kIkEpsilon = 1e-4;
    const auto safe_nonzero = [&](double value) {
        if (std::abs(value) > kIkEpsilon) {
            return value;
        }
        return value < 0.0 ? -kIkEpsilon : kIkEpsilon;
    };
    const auto scale_sign = [](double value) {
        return value < 0.0 ? -1.0 : 1.0;
    };
    const auto apply_one_bone_ik = [&](std::size_t bone_index,
                                       double target_x,
                                       double target_y,
                                       bool compress,
                                       bool stretch,
                                       double alpha) -> bool {
        if (bone_index >= solved_poses.size() || !bone_is_active(bone_index)) {
            return false;
        }

        const std::optional<std::size_t> parent_index = data_->bones()[bone_index].parent_index;
        if (!parent_index.has_value() || *parent_index >= bone_world_transforms_.size()) {
            return false;
        }

        BoneTransform& pose = solved_poses[bone_index].local_pose;
        const BoneInherit inherit = solved_poses[bone_index].inherit;
        const BoneWorldTransform& bone_world = bone_world_transforms_[bone_index];
        const BoneWorldTransform& parent_world = bone_world_transforms_[*parent_index];

        double pa = parent_world.a;
        double pb = parent_world.b;
        double pc = parent_world.c;
        double pd = parent_world.d;
        double rotation_delta = -pose.shear_x - pose.rotation;
        double tx = 0.0;
        double ty = 0.0;

        switch (inherit) {
        case BoneInherit::OnlyTranslation:
            tx = (target_x - bone_world.world_x) * scale_sign(scale_x_);
            ty = (target_y - bone_world.world_y) * scale_sign(scale_y_);
            break;
        case BoneInherit::NoRotationOrReflection: {
            const double determinant = std::abs(pa * pd - pb * pc);
            const double scale = determinant / std::max(kIkEpsilon, pa * pa + pc * pc);
            const double safe_scale_x = safe_nonzero(scale_x_);
            const double safe_scale_y = safe_nonzero(scale_y_);
            const double sa = pa / safe_scale_x;
            const double sc = pc / safe_scale_y;
            pb = -sc * scale * safe_scale_x;
            pd = sa * scale * safe_scale_y;
            rotation_delta += detail::radians_to_degrees(std::atan2(sc, sa));
            [[fallthrough]];
        }
        default: {
            const double world_offset_x = target_x - parent_world.world_x;
            const double world_offset_y = target_y - parent_world.world_y;
            const double determinant = pa * pd - pb * pc;
            if (std::abs(determinant) > kIkEpsilon) {
                tx = (world_offset_x * pd - world_offset_y * pb) / determinant - pose.x;
                ty = (world_offset_y * pa - world_offset_x * pc) / determinant - pose.y;
            }
            break;
        }
        }

        rotation_delta += detail::radians_to_degrees(std::atan2(ty, tx));
        if (pose.scale_x < 0.0) {
            rotation_delta += 180.0;
        }
        rotation_delta = detail::normalize_rotation_degrees(rotation_delta);

        double scale_x = pose.scale_x;
        double scale_y = pose.scale_y;
        if (compress || stretch) {
            if (inherit == BoneInherit::NoScale ||
                inherit == BoneInherit::NoScaleOrReflection) {
                tx = target_x - bone_world.world_x;
                ty = target_y - bone_world.world_y;
            }

            const AttachmentVertex tip_local = bone_tip_local_vector(bone_index);
            const double bone_length = std::hypot(tip_local.x, tip_local.y);
            const double scaled_length = bone_length * scale_x;
            if (scaled_length > kIkEpsilon) {
                const double target_distance_squared = tx * tx + ty * ty;
                if ((compress && target_distance_squared < scaled_length * scaled_length) ||
                    (stretch && target_distance_squared > scaled_length * scaled_length)) {
                    const double scale =
                        ((std::sqrt(target_distance_squared) / scaled_length) - 1.0) * alpha + 1.0;
                    scale_x *= scale;
                }
            }
        }

        pose.rotation =
            detail::normalize_rotation_degrees(pose.rotation + rotation_delta * alpha);
        pose.scale_x = scale_x;
        pose.scale_y = scale_y;
        return true;
    };
    const auto apply_two_bone_ik = [&](std::size_t parent_bone_index,
                                       std::size_t child_bone_index,
                                       double target_x,
                                       double target_y,
                                       int bend_direction,
                                       bool stretch,
                                       double softness,
                                       double alpha) -> bool {
        if (parent_bone_index >= solved_poses.size() ||
            child_bone_index >= solved_poses.size() ||
            !bone_is_active(parent_bone_index) ||
            !bone_is_active(child_bone_index)) {
            return false;
        }
        if (solved_poses[parent_bone_index].inherit != BoneInherit::Normal ||
            solved_poses[child_bone_index].inherit != BoneInherit::Normal) {
            return false;
        }

        const std::optional<std::size_t> grandparent_index =
            data_->bones()[parent_bone_index].parent_index;
        if (!grandparent_index.has_value() ||
            *grandparent_index >= bone_world_transforms_.size()) {
            return false;
        }

        BoneTransform& parent_pose = solved_poses[parent_bone_index].local_pose;
        BoneTransform& child_pose = solved_poses[child_bone_index].local_pose;

        double px = parent_pose.x;
        double py = parent_pose.y;
        double parent_scale_x = parent_pose.scale_x;
        double parent_scale_y = parent_pose.scale_y;
        double stretched_parent_scale_x = parent_scale_x;
        double stretched_parent_scale_y = parent_scale_y;
        double child_scale_x = child_pose.scale_x;
        int parent_offset = 0;
        int child_offset = 0;
        int child_sign = 1;

        if (parent_scale_x < 0.0) {
            parent_scale_x = -parent_scale_x;
            parent_offset = 180;
            child_sign = -1;
        }
        if (parent_scale_y < 0.0) {
            parent_scale_y = -parent_scale_y;
            child_sign = -child_sign;
        }
        if (child_scale_x < 0.0) {
            child_scale_x = -child_scale_x;
            child_offset = 180;
        }

        const double cx = child_pose.x;
        double cy = child_pose.y;
        const BoneWorldTransform& parent_world = bone_world_transforms_[parent_bone_index];
        double a = parent_world.a;
        double b = parent_world.b;
        double c = parent_world.c;
        double d = parent_world.d;
        const bool uniform_scale = std::abs(parent_scale_x - parent_scale_y) <= kIkEpsilon;
        double child_world_x = 0.0;
        double child_world_y = 0.0;
        if (!uniform_scale || stretch) {
            cy = 0.0;
            child_world_x = a * cx + parent_world.world_x;
            child_world_y = c * cx + parent_world.world_y;
        } else {
            child_world_x = a * cx + b * cy + parent_world.world_x;
            child_world_y = c * cx + d * cy + parent_world.world_y;
        }

        const BoneWorldTransform& grandparent_world = bone_world_transforms_[*grandparent_index];
        a = grandparent_world.a;
        b = grandparent_world.b;
        c = grandparent_world.c;
        d = grandparent_world.d;

        double inverse_determinant = a * d - b * c;
        const double child_world_offset_x = child_world_x - grandparent_world.world_x;
        const double child_world_offset_y = child_world_y - grandparent_world.world_y;
        inverse_determinant =
            std::abs(inverse_determinant) <= kIkEpsilon ? 0.0 : 1.0 / inverse_determinant;
        const double child_parent_x =
            (child_world_offset_x * d - child_world_offset_y * b) * inverse_determinant - px;
        const double child_parent_y =
            (child_world_offset_y * a - child_world_offset_x * c) * inverse_determinant - py;
        const double parent_length = std::sqrt(
            child_parent_x * child_parent_x + child_parent_y * child_parent_y);

        const AttachmentVertex child_tip_local = bone_tip_local_vector(child_bone_index);
        const double child_length = std::hypot(child_tip_local.x, child_tip_local.y);
        if (parent_length < kIkEpsilon || child_length < kIkEpsilon) {
            if (!apply_one_bone_ik(parent_bone_index, target_x, target_y, false, stretch, alpha)) {
                return false;
            }
            child_pose.y = cy;
            child_pose.rotation = 0.0;
            return true;
        }

        const double target_world_offset_x = target_x - grandparent_world.world_x;
        const double target_world_offset_y = target_y - grandparent_world.world_y;
        double target_parent_x =
            (target_world_offset_x * d - target_world_offset_y * b) * inverse_determinant - px;
        double target_parent_y =
            (target_world_offset_y * a - target_world_offset_x * c) * inverse_determinant - py;
        double distance_squared =
            target_parent_x * target_parent_x + target_parent_y * target_parent_y;

        double child_scaled_length = child_length * child_scale_x;
        if (softness > 0.0) {
            const double softness_scale =
                std::max(0.0, softness) * parent_scale_x * (child_scale_x + 1.0) * 0.5;
            if (softness_scale > kIkEpsilon) {
                const double target_distance = std::sqrt(distance_squared);
                const double softness_distance =
                    target_distance - parent_length - child_scaled_length * parent_scale_x +
                    softness_scale;
                if (softness_distance > 0.0) {
                    const double softened =
                        std::min(1.0, softness_distance / (softness_scale * 2.0)) - 1.0;
                    const double safe_target_distance =
                        target_distance <= kIkEpsilon ? kIkEpsilon : target_distance;
                    const double pull =
                        (softness_distance - softness_scale * (1.0 - softened * softened)) /
                        safe_target_distance;
                    target_parent_x -= pull * target_parent_x;
                    target_parent_y -= pull * target_parent_y;
                    distance_squared =
                        target_parent_x * target_parent_x + target_parent_y * target_parent_y;
                }
            }
        }

        double parent_angle = 0.0;
        double child_angle = 0.0;
        if (uniform_scale) {
            child_scaled_length *= parent_scale_x;
            const double denominator = 2.0 * parent_length * child_scaled_length;
            double cosine = denominator <= kIkEpsilon
                ? 1.0
                : (distance_squared - parent_length * parent_length -
                   child_scaled_length * child_scaled_length) /
                    denominator;
            if (cosine < -1.0) {
                cosine = -1.0;
                child_angle = 3.14159265358979323846 * static_cast<double>(bend_direction);
            } else if (cosine > 1.0) {
                cosine = 1.0;
                child_angle = 0.0;
                if (stretch) {
                    const double total_length =
                        std::max(kIkEpsilon, parent_length + child_scaled_length);
                    const double scale =
                        ((std::sqrt(distance_squared) / total_length) - 1.0) * alpha + 1.0;
                    stretched_parent_scale_x *= scale;
                }
            } else {
                child_angle = std::acos(cosine) * static_cast<double>(bend_direction);
            }

            const double adjacent = parent_length + child_scaled_length * cosine;
            const double opposite = child_scaled_length * std::sin(child_angle);
            parent_angle = std::atan2(
                target_parent_y * adjacent - target_parent_x * opposite,
                target_parent_x * adjacent + target_parent_y * opposite);
        } else {
            const double scaled_child_x = parent_scale_x * child_scaled_length;
            const double scaled_child_y = parent_scale_y * child_scaled_length;
            const double scaled_child_x_squared = scaled_child_x * scaled_child_x;
            const double scaled_child_y_squared = scaled_child_y * scaled_child_y;
            const double target_angle = std::atan2(target_parent_y, target_parent_x);
            double curve = scaled_child_y_squared * parent_length * parent_length +
                scaled_child_x_squared * distance_squared -
                scaled_child_x_squared * scaled_child_y_squared;
            const double curve_linear = -2.0 * scaled_child_y_squared * parent_length;
            const double curve_quadratic = scaled_child_y_squared - scaled_child_x_squared;
            const double discriminant =
                curve_linear * curve_linear - 4.0 * curve_quadratic * curve;
            if (discriminant >= 0.0) {
                double root = std::sqrt(discriminant);
                if (curve_linear < 0.0) {
                    root = -root;
                }
                const double q = -(curve_linear + root) * 0.5;
                const double r0 =
                    std::abs(curve_quadratic) <= kIkEpsilon
                    ? std::numeric_limits<double>::infinity()
                    : q / curve_quadratic;
                const double r1 =
                    std::abs(q) <= kIkEpsilon
                    ? std::numeric_limits<double>::infinity()
                    : curve / q;
                const double chosen_root = std::abs(r0) < std::abs(r1) ? r0 : r1;
                const double height_squared = distance_squared - chosen_root * chosen_root;
                if (std::isfinite(chosen_root) && height_squared >= 0.0) {
                    const double height =
                        std::sqrt(height_squared) * static_cast<double>(bend_direction);
                    const double safe_parent_scale_y = safe_nonzero(parent_scale_y);
                    const double safe_parent_scale_x = safe_nonzero(parent_scale_x);
                    parent_angle = target_angle - std::atan2(height, chosen_root);
                    child_angle = std::atan2(
                        height / safe_parent_scale_y,
                        (chosen_root - parent_length) / safe_parent_scale_x);
                } else {
                    parent_angle = std::numeric_limits<double>::quiet_NaN();
                }
            } else {
                parent_angle = std::numeric_limits<double>::quiet_NaN();
            }

            if (!std::isfinite(parent_angle) || !std::isfinite(child_angle)) {
                double min_angle = 3.14159265358979323846;
                double min_x = parent_length - scaled_child_x;
                double min_distance = min_x * min_x;
                double min_y = 0.0;
                double max_angle = 0.0;
                double max_x = parent_length + scaled_child_x;
                double max_distance = max_x * max_x;
                double max_y = 0.0;
                const double denominator = scaled_child_x_squared - scaled_child_y_squared;
                if (std::abs(denominator) > kIkEpsilon) {
                    const double value = -scaled_child_x * parent_length / denominator;
                    if (value >= -1.0 && value <= 1.0) {
                        const double angle = std::acos(value);
                        const double x = scaled_child_x * std::cos(angle) + parent_length;
                        const double y = scaled_child_y * std::sin(angle);
                        const double distance = x * x + y * y;
                        if (distance < min_distance) {
                            min_angle = angle;
                            min_distance = distance;
                            min_x = x;
                            min_y = y;
                        }
                        if (distance > max_distance) {
                            max_angle = angle;
                            max_distance = distance;
                            max_x = x;
                            max_y = y;
                        }
                    }
                }

                if (distance_squared <= (min_distance + max_distance) * 0.5) {
                    parent_angle =
                        target_angle - std::atan2(min_y * static_cast<double>(bend_direction), min_x);
                    child_angle = min_angle * static_cast<double>(bend_direction);
                } else {
                    parent_angle =
                        target_angle - std::atan2(max_y * static_cast<double>(bend_direction), max_x);
                    child_angle = max_angle * static_cast<double>(bend_direction);
                }
            }
        }

        const double offset = std::atan2(cy, cx) * static_cast<double>(child_sign);
        const double parent_rotation = parent_pose.rotation;
        const double parent_delta = detail::normalize_rotation_degrees(
            detail::radians_to_degrees(parent_angle - offset) +
            static_cast<double>(parent_offset) - parent_rotation);
        parent_pose.rotation =
            detail::normalize_rotation_degrees(parent_rotation + parent_delta * alpha);
        parent_pose.scale_x = stretched_parent_scale_x;
        parent_pose.scale_y = stretched_parent_scale_y;
        parent_pose.shear_x = 0.0;
        parent_pose.shear_y = 0.0;

        const double child_rotation = child_pose.rotation;
        const double child_delta = detail::normalize_rotation_degrees(
            (detail::radians_to_degrees(child_angle + offset) - child_pose.shear_x) *
                static_cast<double>(child_sign) +
            static_cast<double>(child_offset) - child_rotation);
        child_pose.rotation = detail::normalize_rotation_degrees(child_rotation + child_delta * alpha);
        child_pose.y = cy;
        return true;
    };

    for (std::size_t constraint_index = 0;
         constraint_index < data_->ik_constraints().size();
         ++constraint_index) {
        if (constraint_index < active_ik_constraints_.size() &&
            !active_ik_constraints_[constraint_index]) {
            continue;
        }

        const IkConstraintData& constraint = data_->ik_constraints()[constraint_index];
        const double mix = detail::clamp_mix(constraint.mix);
        if (mix <= 0.0 || constraint.target_bone_index >= bone_world_transforms_.size()) {
            continue;
        }
        if (!bone_is_active(constraint.target_bone_index)) {
            continue;
        }

        const AttachmentVertex target_world{
            bone_world_transforms_[constraint.target_bone_index].world_x,
            bone_world_transforms_[constraint.target_bone_index].world_y};

        if (constraint.bone_indices.size() == 1) {
            const std::size_t bone_index = constraint.bone_indices.front();
            if (apply_one_bone_ik(
                    bone_index,
                    target_world.x,
                    target_world.y,
                    constraint.compress,
                    constraint.stretch,
                    mix)) {
                compute_world_transforms();
            }
            continue;
        }

        if (constraint.bone_indices.size() != 2) {
            continue;
        }

        if (apply_two_bone_ik(
                constraint.bone_indices[0],
                constraint.bone_indices[1],
                target_world.x,
                target_world.y,
                constraint.bend_positive ? 1 : -1,
                constraint.stretch,
                constraint.softness,
                mix)) {
            compute_world_transforms();
        }
    }

    if (physics == PhysicsMode::None) {
        return;
    }
    if (physics_constraint_states_.size() != data_->physics_constraints().size()) {
        physics_constraint_states_.resize(data_->physics_constraints().size());
    }

    const auto recompute_world_subtree = [&](const auto& self, std::size_t parent_index) -> void {
        for (std::size_t child_index = 0; child_index < data_->bones().size(); ++child_index) {
            if (data_->bones()[child_index].parent_index != std::optional<std::size_t>{parent_index}) {
                continue;
            }

            bone_world_transforms_[child_index] = detail::compose_world_transform(
                bone_world_transforms_[parent_index],
                solved_poses[child_index],
                scale_x_,
                scale_y_);
            self(self, child_index);
        }
    };

    const double physics_delta_seconds =
        physics == PhysicsMode::Update ? std::max(0.0, pending_physics_delta_seconds_) : 0.0;
    for (std::size_t constraint_index = 0;
         constraint_index < data_->physics_constraints().size();
         ++constraint_index) {
        const PhysicsConstraintData& constraint = data_->physics_constraints()[constraint_index];
        if (constraint_index < active_physics_constraints_.size() &&
            !active_physics_constraints_[constraint_index]) {
            continue;
        }

        const double mix = detail::clamp_mix(constraint.mix);
        if (mix <= 0.0 || constraint.bone_indices.empty()) {
            continue;
        }

        PhysicsConstraintState& constraint_state = physics_constraint_states_[constraint_index];
        if (constraint_state.bones.size() != constraint.bone_indices.size()) {
            constraint_state = {};
            constraint_state.bones.resize(constraint.bone_indices.size());
        }
        if (physics == PhysicsMode::Reset) {
            constraint_state = {};
            constraint_state.bones.resize(constraint.bone_indices.size());
        } else if (physics == PhysicsMode::Update) {
            constraint_state.remaining += physics_delta_seconds;
        }

        const bool apply_x = constraint.x > 0.0;
        const bool apply_y = constraint.y > 0.0;
        const bool apply_rotate_or_shear = constraint.rotate > 0.0 || constraint.shear_x > 0.0;
        const bool apply_scale = constraint.scale_x > 0.0;
        const double step = std::max(constraint.step, kEpsilon);
        const double inertia = detail::clamp_mix(constraint.inertia);
        const double strength = std::max(0.0, constraint.strength);
        const double mass_step = std::max(0.0, constraint.mass_inverse) * step;
        const double damping_factor =
            std::exp(-std::max(0.0, constraint.damping) * step);
        const AttachmentVertex external_force{
            constraint.wind.x * scale_x_,
            constraint.gravity.y * scale_y_};
        const double position_limit_x =
            constraint.limit * physics_delta_seconds * std::abs(scale_x_);
        const double position_limit_y =
            constraint.limit * physics_delta_seconds * std::abs(scale_y_);

        double next_remaining = constraint_state.remaining;
        for (std::size_t chain_index = 0;
             chain_index < constraint.bone_indices.size();
             ++chain_index) {
            const std::size_t bone_index = constraint.bone_indices[chain_index];
            if (bone_index >= solved_poses.size() || !bone_is_active(bone_index)) {
                continue;
            }

            const AttachmentVertex tip_local = bone_tip_local_vector(bone_index);
            const double tip_length = detail::vertex_length(tip_local);

            BoneWorldTransform& bone_world = bone_world_transforms_[bone_index];
            PhysicsBoneState& bone_state = constraint_state.bones[chain_index];
            if (constraint_state.reset) {
                bone_state.ux = bone_world.world_x;
                bone_state.uy = bone_world.world_y;
            } else if (physics == PhysicsMode::Update) {
                if (apply_x) {
                    const double delta = (bone_state.ux - bone_world.world_x) * inertia;
                    bone_state.x_offset += std::clamp(delta, -position_limit_x, position_limit_x);
                    bone_state.ux = bone_world.world_x;
                }
                if (apply_y) {
                    const double delta = (bone_state.uy - bone_world.world_y) * inertia;
                    bone_state.y_offset += std::clamp(delta, -position_limit_y, position_limit_y);
                    bone_state.uy = bone_world.world_y;
                }
            }

            double remaining = constraint_state.remaining;
            if ((apply_x || apply_y) && physics == PhysicsMode::Update && remaining >= step) {
                do {
                    if (apply_x) {
                        bone_state.x_velocity +=
                            (external_force.x - bone_state.x_offset * strength) * mass_step;
                        bone_state.x_offset += bone_state.x_velocity * step;
                        bone_state.x_velocity *= damping_factor;
                    }
                    if (apply_y) {
                        bone_state.y_velocity +=
                            (external_force.y - bone_state.y_offset * strength) * mass_step;
                        bone_state.y_offset += bone_state.y_velocity * step;
                        bone_state.y_velocity *= damping_factor;
                    }
                    remaining -= step;
                } while (remaining >= step);
                next_remaining = remaining;
            }

            if (apply_x) {
                bone_world.world_x += bone_state.x_offset * mix * constraint.x;
            }
            if (apply_y) {
                bone_world.world_y += bone_state.y_offset * mix * constraint.y;
            }

            if (apply_rotate_or_shear || apply_scale) {
                const double base_angle = std::atan2(bone_world.c, bone_world.a);
                double cos_angle = std::cos(base_angle);
                double sin_angle = std::sin(base_angle);
                double mixed_rotate = 0.0;
                double dx = bone_state.cx - bone_world.world_x;
                double dy = bone_state.cy - bone_world.world_y;
                if (physics == PhysicsMode::Update) {
                    dx = std::clamp(dx, -position_limit_x, position_limit_x);
                    dy = std::clamp(dy, -position_limit_y, position_limit_y);

                    if (apply_rotate_or_shear) {
                        mixed_rotate = constraint.rotate * mix;
                        const double angle = std::atan2(dy + bone_state.ty, dx + bone_state.tx) -
                            base_angle - bone_state.rotate_offset * mixed_rotate;
                        bone_state.rotate_offset += detail::normalize_rotation_radians(angle) * inertia;
                        const double rotated = bone_state.rotate_offset * mixed_rotate + base_angle;
                        cos_angle = std::cos(rotated);
                        sin_angle = std::sin(rotated);
                    }

                    if (apply_scale && tip_length > kEpsilon) {
                        const double reference_length = std::max(
                            kEpsilon,
                            tip_length * detail::world_axis_length(bone_world));
                        bone_state.scale_offset +=
                            ((dx * cos_angle + dy * sin_angle) * inertia) / reference_length;
                    }

                    remaining = constraint_state.remaining;
                    if (remaining >= step) {
                        while (true) {
                            remaining -= step;
                            if (apply_scale) {
                                bone_state.scale_velocity +=
                                    ((external_force.x * cos_angle + external_force.y * sin_angle) -
                                     bone_state.scale_offset * strength) *
                                    mass_step;
                                bone_state.scale_offset += bone_state.scale_velocity * step;
                                bone_state.scale_velocity *= damping_factor;
                            }
                            if (apply_rotate_or_shear) {
                                bone_state.rotate_velocity -=
                                    (((external_force.x * sin_angle - external_force.y * cos_angle) *
                                          tip_length) +
                                     bone_state.rotate_offset * strength) *
                                    mass_step;
                                bone_state.rotate_offset += bone_state.rotate_velocity * step;
                                bone_state.rotate_velocity *= damping_factor;
                                if (remaining < step) {
                                    break;
                                }

                                const double rotated =
                                    bone_state.rotate_offset * mixed_rotate + base_angle;
                                cos_angle = std::cos(rotated);
                                sin_angle = std::sin(rotated);
                            } else if (remaining < step) {
                                break;
                            }
                        }
                        next_remaining = remaining;
                    }
                }

                if (apply_rotate_or_shear) {
                    const double offset = bone_state.rotate_offset * mix;
                    double sine = 0.0;
                    double cosine = 1.0;
                    if (constraint.shear_x > 0.0) {
                        double angle = 0.0;
                        if (constraint.rotate > 0.0) {
                            angle = offset * constraint.rotate;
                            sine = std::sin(angle);
                            cosine = std::cos(angle);
                            const double axis_b = bone_world.b;
                            bone_world.b = cosine * axis_b - sine * bone_world.d;
                            bone_world.d = sine * axis_b + cosine * bone_world.d;
                        }
                        angle += offset * constraint.shear_x;
                        sine = std::sin(angle);
                        cosine = std::cos(angle);
                        const double axis_a = bone_world.a;
                        bone_world.a = cosine * axis_a - sine * bone_world.c;
                        bone_world.c = sine * axis_a + cosine * bone_world.c;
                    } else {
                        const double angle = offset * constraint.rotate;
                        sine = std::sin(angle);
                        cosine = std::cos(angle);
                        const double axis_a = bone_world.a;
                        bone_world.a = cosine * axis_a - sine * bone_world.c;
                        bone_world.c = sine * axis_a + cosine * bone_world.c;
                        const double axis_b = bone_world.b;
                        bone_world.b = cosine * axis_b - sine * bone_world.d;
                        bone_world.d = sine * axis_b + cosine * bone_world.d;
                    }
                }
                if (apply_scale) {
                    const double scale = 1.0 + bone_state.scale_offset * mix * constraint.scale_x;
                    bone_world.a *= scale;
                    bone_world.c *= scale;
                }
            }

            if (physics != PhysicsMode::Pose) {
                if (tip_length > kEpsilon) {
                    const AttachmentVertex tip_world = detail::transform_attachment_vertex(
                        bone_world,
                        tip_local.x,
                        tip_local.y);
                    bone_state.tx = tip_world.x - bone_world.world_x;
                    bone_state.ty = tip_world.y - bone_world.world_y;
                } else {
                    bone_state.tx = 0.0;
                    bone_state.ty = 0.0;
                }
                bone_state.cx = bone_world.world_x;
                bone_state.cy = bone_world.world_y;
            }

            recompute_world_subtree(recompute_world_subtree, bone_index);
        }

        if (physics == PhysicsMode::Update) {
            constraint_state.remaining = next_remaining;
            constraint_state.reset = false;
        } else if (physics == PhysicsMode::Reset) {
            constraint_state.remaining = 0.0;
            constraint_state.reset = false;
        }
    }
}

} // namespace marrow::runtime
