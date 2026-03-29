#include "skeleton_internal.hpp"

#include <chrono>
#include <algorithm>
#include <cstdint>
#include <cmath>
#include <cstring>
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

template <typename T>
void ensure_vector_capacity(
    std::vector<T>* values,
    std::size_t required,
    std::size_t* allocation_counter) {
    if (values == nullptr || values->capacity() >= required) {
        return;
    }

    if (allocation_counter != nullptr) {
        ++(*allocation_counter);
    }
    values->reserve(required);
}

template <typename T>
void resize_vector_without_reallocation(
    std::vector<T>* values,
    std::size_t required,
    std::size_t* allocation_counter) {
    ensure_vector_capacity(values, required, allocation_counter);
    values->resize(required);
}

bool local_pose_matches(const BoneTransform& lhs, const BoneTransform& rhs) {
    return lhs.x == rhs.x &&
        lhs.y == rhs.y &&
        lhs.rotation == rhs.rotation &&
        lhs.scale_x == rhs.scale_x &&
        lhs.scale_y == rhs.scale_y &&
        lhs.shear_x == rhs.shear_x &&
        lhs.shear_y == rhs.shear_y;
}

bool bone_pose_matches(const BonePose& lhs, const BonePose& rhs) {
    return lhs.inherit == rhs.inherit &&
        local_pose_matches(lhs.local_pose, rhs.local_pose);
}

bool world_transform_matches(const BoneWorldTransform& lhs, const BoneWorldTransform& rhs) {
    return lhs.a == rhs.a &&
        lhs.b == rhs.b &&
        lhs.c == rhs.c &&
        lhs.d == rhs.d &&
        lhs.world_x == rhs.world_x &&
        lhs.world_y == rhs.world_y;
}

void bump_revision(std::size_t* revision) {
    if (revision == nullptr) {
        return;
    }

    ++(*revision);
    if (*revision == 0U) {
        *revision = 1U;
    }
}

unsigned least_significant_bit_index(std::uint64_t word) {
#if defined(__clang__) || defined(__GNUC__)
    return static_cast<unsigned>(__builtin_ctzll(word));
#else
    unsigned index = 0U;
    while ((word & std::uint64_t{1}) == 0U) {
        word >>= 1U;
        ++index;
    }
    return index;
#endif
}

} // namespace

namespace detail {
namespace {

constexpr float kPiF = 3.14159265358979323846f;
constexpr float kHalfPiF = kPiF * 0.5f;
constexpr float kTwoPiF = kPiF * 2.0f;

float wrap_radiansf(float angle) {
    if (angle > kPiF) {
        if (angle <= kTwoPiF) {
            angle -= kTwoPiF;
        } else {
            angle -= kTwoPiF * std::floor((angle + kPiF) / kTwoPiF);
        }
    } else if (angle < -kPiF) {
        if (angle >= -kTwoPiF) {
            angle += kTwoPiF;
        } else {
            angle -= kTwoPiF * std::floor((angle + kPiF) / kTwoPiF);
        }
    }
    if (angle > kPiF) {
        angle -= kTwoPiF;
    } else if (angle < -kPiF) {
        angle += kTwoPiF;
    }
    return angle;
}

float fast_atanf(float value) {
    return value * ((kPiF * 0.25f) + (0.273f * (1.0f - std::abs(value))));
}

} // namespace

float fast_sqrtf(float value) {
    if (value <= 0.0f) {
        return 0.0f;
    }
    return std::sqrt(value);
}

float fast_sinf(float angle) {
    const float wrapped = wrap_radiansf(angle);
    constexpr float kB = 4.0f / kPiF;
    constexpr float kC = -4.0f / (kPiF * kPiF);
    constexpr float kP = 0.225f;
    const float y = kB * wrapped + (kC * wrapped * std::abs(wrapped));
    return kP * ((y * std::abs(y)) - y) + y;
}

float fast_cosf(float angle) {
    return fast_sinf(angle + kHalfPiF);
}

float fast_atan2f(float y, float x) {
    if (x == 0.0f) {
        if (y > 0.0f) {
            return kHalfPiF;
        }
        if (y < 0.0f) {
            return -kHalfPiF;
        }
        return 0.0f;
    }

    const float absolute_x = std::abs(x);
    const float absolute_y = std::abs(y);
    if (absolute_x > absolute_y) {
        float angle = fast_atanf(y / x);
        if (x < 0.0f) {
            angle += y >= 0.0f ? kPiF : -kPiF;
        }
        return angle;
    }

    float angle = kHalfPiF - fast_atanf(x / y);
    if (y < 0.0f) {
        angle -= kPiF;
    }
    return angle;
}

float fast_acosf(float value) {
    const float clamped = std::clamp(value, -1.0f, 1.0f);
    const bool negate = clamped < 0.0f;
    const float absolute = std::abs(clamped);

    float result = -0.0187293f;
    result = result * absolute + 0.0742610f;
    result = result * absolute - 0.2121144f;
    result = result * absolute + 1.5707288f;
    result = result * fast_sqrtf(std::max(0.0f, 1.0f - absolute));
    return negate ? kPiF - result : result;
}

} // namespace detail

void Skeleton::update_world_transforms(
    PhysicsMode physics,
    WorldTransformTimingBreakdown* timing_breakdown) {
    if (timing_breakdown != nullptr) {
        *timing_breakdown = {};
    }
    if (!visible_) {
        return;
    }

    using clock = std::chrono::steady_clock;
    const bool capture_timings = timing_breakdown != nullptr;
    const auto measure_seconds = [&](const auto& fn, double* seconds_out) {
        if (seconds_out == nullptr) {
            fn();
            return;
        }

        const auto start = clock::now();
        fn();
        *seconds_out += std::chrono::duration<double>(clock::now() - start).count();
    };

    detail::BoneLocalTransformBuffers local_buffers{
        &bone_local_x_,
        &bone_local_y_,
        &bone_local_a_,
        &bone_local_b_,
        &bone_local_c_,
        &bone_local_d_,
    };
    detail::BoneWorldTransformBuffers world_buffers{
        &bone_world_a_,
        &bone_world_b_,
        &bone_world_c_,
        &bone_world_d_,
        &bone_world_x_,
        &bone_world_y_,
    };

    const std::size_t bone_count = bone_poses_.size();
    const bool input_pose_cache_size_changed = input_pose_cache_.size() != bone_count;
    const bool solved_pose_size_changed = solved_poses_.size() != bone_count;
    const bool constraint_input_world_cache_size_changed =
        constraint_input_world_cache_.size() != bone_count;
    const bool local_buffer_size_changed =
        bone_local_x_.size() != bone_count ||
        bone_local_y_.size() != bone_count ||
        bone_local_a_.size() != bone_count ||
        bone_local_b_.size() != bone_count ||
        bone_local_c_.size() != bone_count ||
        bone_local_d_.size() != bone_count;

    resize_vector_without_reallocation(
        &input_pose_cache_,
        bone_count,
        &constraint_allocation_count_);
    resize_vector_without_reallocation(
        &solved_poses_,
        bone_count,
        &constraint_allocation_count_);
    resize_vector_without_reallocation(
        &constraint_input_world_cache_,
        bone_count,
        &constraint_allocation_count_);
    resize_vector_without_reallocation(
        &constraint_input_world_revisions_,
        bone_count,
        &constraint_allocation_count_);
    resize_vector_without_reallocation(
        &solved_local_pose_revisions_,
        bone_count,
        &constraint_allocation_count_);
    resize_vector_without_reallocation(
        &solved_applied_local_pose_revisions_,
        bone_count,
        &constraint_allocation_count_);
    resize_vector_without_reallocation(
        &solved_world_revisions_,
        bone_count,
        &constraint_allocation_count_);
    resize_vector_without_reallocation(
        &solved_applied_parent_world_revisions_,
        bone_count,
        &constraint_allocation_count_);
    resize_vector_without_reallocation(
        &constraint_dirty_bone_words_,
        data_->bone_subtree_word_count_,
        &constraint_allocation_count_);
    resize_vector_without_reallocation(
        &constraint_dirty_subtree_words_,
        data_->bone_subtree_word_count_,
        &constraint_allocation_count_);
    resize_vector_without_reallocation(local_buffers.x, bone_count, &constraint_allocation_count_);
    resize_vector_without_reallocation(local_buffers.y, bone_count, &constraint_allocation_count_);
    resize_vector_without_reallocation(local_buffers.a, bone_count, &constraint_allocation_count_);
    resize_vector_without_reallocation(local_buffers.b, bone_count, &constraint_allocation_count_);
    resize_vector_without_reallocation(local_buffers.c, bone_count, &constraint_allocation_count_);
    resize_vector_without_reallocation(local_buffers.d, bone_count, &constraint_allocation_count_);
    resize_vector_without_reallocation(world_buffers.a, bone_count, &constraint_allocation_count_);
    resize_vector_without_reallocation(world_buffers.b, bone_count, &constraint_allocation_count_);
    resize_vector_without_reallocation(world_buffers.c, bone_count, &constraint_allocation_count_);
    resize_vector_without_reallocation(world_buffers.d, bone_count, &constraint_allocation_count_);
    resize_vector_without_reallocation(
        world_buffers.world_x,
        bone_count,
        &constraint_allocation_count_);
    resize_vector_without_reallocation(
        world_buffers.world_y,
        bone_count,
        &constraint_allocation_count_);
    resize_vector_without_reallocation(
        &ik_constraint_revision_states_,
        data_->ik_constraints().size(),
        &constraint_allocation_count_);
    resize_vector_without_reallocation(
        &transform_constraint_revision_states_,
        data_->transform_constraints().size(),
        &constraint_allocation_count_);

    const auto refresh_cached_local_transform = [&](std::size_t bone_index) {
        if (bone_index >= solved_poses_.size()) {
            return;
        }
        detail::prepare_local_transform_buffer(
            bone_index,
            solved_poses_[bone_index].local_pose,
            local_buffers);
    };
    std::fill(constraint_dirty_bone_words_.begin(), constraint_dirty_bone_words_.end(), 0U);
    std::fill(constraint_dirty_subtree_words_.begin(), constraint_dirty_subtree_words_.end(), 0U);
    const bool refresh_all_local_transforms =
        solved_pose_size_changed || local_buffer_size_changed;
    bool raw_input_pose_changed =
        input_pose_cache_size_changed ||
        solved_pose_size_changed ||
        constraint_input_world_cache_size_changed ||
        !has_solved_constraint_state_ ||
        scale_x_ != solved_constraint_scale_x_ ||
        scale_y_ != solved_constraint_scale_y_ ||
        solved_constraint_scope_revision_ != constraint_scope_revision_;
    for (std::size_t bone_index = 0; bone_index < bone_count; ++bone_index) {
        const BonePose& input_pose = bone_poses_[bone_index];
        if (!raw_input_pose_changed &&
            !bone_pose_matches(input_pose_cache_[bone_index], input_pose)) {
            raw_input_pose_changed = true;
        }
        input_pose_cache_[bone_index] = input_pose;
        const bool local_pose_changed =
            refresh_all_local_transforms ||
            !local_pose_matches(solved_poses_[bone_index].local_pose, input_pose.local_pose);
        solved_poses_[bone_index] = input_pose;
        if (local_pose_changed) {
            refresh_cached_local_transform(bone_index);
        }
    }

    const auto bone_is_active = [&](std::size_t bone_index) {
        return bone_index < active_bones_.size() ? active_bones_[bone_index] : true;
    };
    const auto count_unchanged_constraint_skips = [&]() {
        if (!capture_timings) {
            return;
        }

        for (std::size_t constraint_index = 0;
             constraint_index < data_->ik_constraints().size();
             ++constraint_index) {
            if (constraint_index >= ik_constraint_revision_states_.size() ||
                !ik_constraint_revision_states_[constraint_index].valid ||
                constraint_index >= active_ik_constraints_.size() ||
                !active_ik_constraints_[constraint_index]) {
                continue;
            }

            const IkConstraintData& constraint = data_->ik_constraints()[constraint_index];
            if (detail::clamp_mix(constraint.mix) <= 0.0 ||
                constraint.target_bone_index >= bone_count ||
                !bone_is_active(constraint.target_bone_index)) {
                continue;
            }

            bool all_bones_active = true;
            for (const std::size_t bone_index : constraint.bone_indices) {
                if (bone_index >= bone_count || !bone_is_active(bone_index)) {
                    all_bones_active = false;
                    break;
                }
            }
            if (!all_bones_active) {
                continue;
            }

            ++timing_breakdown->skipped_ik_constraints;
        }

        for (std::size_t constraint_index = 0;
             constraint_index < data_->transform_constraints().size();
             ++constraint_index) {
            if (constraint_index >= transform_constraint_revision_states_.size() ||
                !transform_constraint_revision_states_[constraint_index].valid ||
                constraint_index >= active_transform_constraints_.size() ||
                !active_transform_constraints_[constraint_index]) {
                continue;
            }

            const TransformConstraintData& constraint =
                data_->transform_constraints()[constraint_index];
            if (constraint.source_bone_index >= bone_count ||
                !bone_is_active(constraint.source_bone_index) ||
                (detail::clamp_mix(constraint.rotate_mix) <= 0.0 &&
                 detail::clamp_mix(constraint.translate_mix) <= 0.0 &&
                 detail::clamp_mix(constraint.scale_mix) <= 0.0 &&
                 detail::clamp_mix(constraint.shear_mix) <= 0.0)) {
                continue;
            }

            bool has_active_target = false;
            for (const std::size_t bone_index : constraint.target_bone_indices) {
                if (bone_index < bone_count && bone_is_active(bone_index)) {
                    has_active_target = true;
                    break;
                }
            }
            if (!has_active_target) {
                continue;
            }

            ++timing_breakdown->skipped_transform_constraints;
        }
    };
    const auto load_world_transform = [&](std::size_t bone_index) {
        return detail::load_world_transform(bone_index, world_buffers);
    };
    const auto store_world_transform = [&](std::size_t bone_index, const BoneWorldTransform& world) {
        detail::store_world_transform(bone_index, world, world_buffers);
    };

    const auto compute_world_transforms = [&]() {
        if (capture_timings) {
            ++timing_breakdown->full_skeleton_passes;
        }
        detail::propagate_world_transforms_optimized(
            data_->bones(),
            solved_poses_,
            scale_x_,
            scale_y_,
            local_buffers,
            world_buffers);
    };

    if (!raw_input_pose_changed &&
        data_->path_constraints().empty() &&
        data_->physics_constraints().empty() &&
        physics != PhysicsMode::Update &&
        physics != PhysicsMode::Reset) {
        count_unchanged_constraint_skips();
        return;
    }

    std::fill(solved_local_pose_revisions_.begin(), solved_local_pose_revisions_.end(), 0U);
    std::fill(
        solved_applied_local_pose_revisions_.begin(),
        solved_applied_local_pose_revisions_.end(),
        0U);
    std::fill(
        solved_world_revisions_.begin(),
        solved_world_revisions_.end(),
        solved_poses_.empty() ? 0U : 1U);
    std::fill(
        solved_applied_parent_world_revisions_.begin(),
        solved_applied_parent_world_revisions_.end(),
        0U);
    const auto& children_map = data_->children_map();
    const auto& bone_tip_local_vectors = data_->bone_tip_local_vectors();
    for (std::size_t bone_index = 0; bone_index < data_->bones().size(); ++bone_index) {
        const std::optional<std::size_t> parent_index = data_->bones()[bone_index].parent_index;
        if (parent_index.has_value()) {
            solved_applied_parent_world_revisions_[bone_index] =
                solved_world_revisions_[*parent_index];
        }
    }
    std::size_t next_world_revision = solved_poses_.empty() ? 0U : 2U;
    bool constraint_pose_dirty = false;

    const auto recompute_world_children = [&](const auto& self, std::size_t parent_index) -> void {
        if (parent_index >= children_map.size()) {
            return;
        }
        const BoneWorldTransform parent_world = load_world_transform(parent_index);
        for (const std::size_t child_index : children_map[parent_index]) {
            if (child_index >= solved_poses_.size() || child_index >= bone_count) {
                continue;
            }

            const BoneWorldTransform child_world = detail::compose_cached_world_transform(
                &parent_world,
                solved_poses_[child_index],
                local_buffers,
                child_index,
                scale_x_,
                scale_y_);
            store_world_transform(child_index, child_world);
            self(self, child_index);
        }
    };
    const auto ensure_world_transform = [&](const auto& self, std::size_t bone_index) -> void {
        if (bone_index >= solved_poses_.size() || bone_index >= bone_count) {
            return;
        }

        const std::optional<std::size_t> parent_index = data_->bones()[bone_index].parent_index;
        std::size_t parent_world_revision = 0U;
        std::optional<BoneWorldTransform> parent_world;
        if (parent_index.has_value()) {
            self(self, *parent_index);
            parent_world_revision = solved_world_revisions_[*parent_index];
            parent_world = load_world_transform(*parent_index);
        }

        if (solved_applied_local_pose_revisions_[bone_index] ==
                solved_local_pose_revisions_[bone_index] &&
            solved_applied_parent_world_revisions_[bone_index] == parent_world_revision) {
            return;
        }

        const BoneWorldTransform world_transform = detail::compose_cached_world_transform(
            parent_world.has_value() ? &(*parent_world) : nullptr,
            solved_poses_[bone_index],
            local_buffers,
            bone_index,
            scale_x_,
            scale_y_);
        store_world_transform(bone_index, world_transform);
        solved_applied_local_pose_revisions_[bone_index] =
            solved_local_pose_revisions_[bone_index];
        solved_applied_parent_world_revisions_[bone_index] = parent_world_revision;
        solved_world_revisions_[bone_index] = next_world_revision++;
    };
    const auto mark_bone_world_dirty = [&](std::size_t bone_index) {
        if (bone_index >= solved_local_pose_revisions_.size()) {
            return;
        }

        refresh_cached_local_transform(bone_index);
        ++solved_local_pose_revisions_[bone_index];
        constraint_pose_dirty = true;
        const std::size_t word_index = bone_index / 64U;
        if (word_index < constraint_dirty_bone_words_.size()) {
            constraint_dirty_bone_words_[word_index] |= std::uint64_t{1} << (bone_index % 64U);
        }
    };
    const auto ensure_dirty_world_transforms_current = [&]() {
        if (!constraint_pose_dirty) {
            return;
        }

        const std::size_t subtree_word_count = data_->bone_subtree_word_count_;
        const auto& subtree_word_masks = data_->bone_subtree_word_masks_;
        for (std::size_t word_index = 0;
             word_index < constraint_dirty_bone_words_.size();
             ++word_index) {
            std::uint64_t dirty_word = constraint_dirty_bone_words_[word_index];
            while (dirty_word != 0U) {
                const unsigned bit_index = least_significant_bit_index(dirty_word);
                dirty_word &= dirty_word - 1U;
                const std::size_t bone_index = word_index * 64U + bit_index;
                if (bone_index >= bone_count) {
                    continue;
                }

                const std::uint64_t* const subtree_words =
                    subtree_word_masks.data() + bone_index * subtree_word_count;
                for (std::size_t subtree_word_index = 0;
                     subtree_word_index < subtree_word_count;
                     ++subtree_word_index) {
                    constraint_dirty_subtree_words_[subtree_word_index] |=
                        subtree_words[subtree_word_index];
                }
            }
        }

        for (std::size_t word_index = 0;
             word_index < constraint_dirty_subtree_words_.size();
             ++word_index) {
            std::uint64_t dirty_word = constraint_dirty_subtree_words_[word_index];
            while (dirty_word != 0U) {
                const unsigned bit_index = least_significant_bit_index(dirty_word);
                dirty_word &= dirty_word - 1U;
                const std::size_t bone_index = word_index * 64U + bit_index;
                if (bone_index >= solved_poses_.size() || bone_index >= bone_count) {
                    continue;
                }

                const std::optional<std::size_t> parent_index = data_->bones()[bone_index].parent_index;
                std::size_t parent_world_revision = 0U;
                std::optional<BoneWorldTransform> parent_world;
                if (parent_index.has_value()) {
                    parent_world_revision = solved_world_revisions_[*parent_index];
                    parent_world = load_world_transform(*parent_index);
                }

                if (solved_applied_local_pose_revisions_[bone_index] ==
                        solved_local_pose_revisions_[bone_index] &&
                    solved_applied_parent_world_revisions_[bone_index] == parent_world_revision) {
                    continue;
                }

                const BoneWorldTransform world_transform = detail::compose_cached_world_transform(
                    parent_world.has_value() ? &(*parent_world) : nullptr,
                    solved_poses_[bone_index],
                    local_buffers,
                    bone_index,
                    scale_x_,
                    scale_y_);
                store_world_transform(bone_index, world_transform);
                solved_applied_local_pose_revisions_[bone_index] =
                    solved_local_pose_revisions_[bone_index];
                solved_applied_parent_world_revisions_[bone_index] = parent_world_revision;
                solved_world_revisions_[bone_index] = next_world_revision++;
            }
        }

        std::fill(constraint_dirty_bone_words_.begin(), constraint_dirty_bone_words_.end(), 0U);
        std::fill(constraint_dirty_subtree_words_.begin(), constraint_dirty_subtree_words_.end(), 0U);
    };
    constexpr float kEpsilonF = 1e-8f;
    constexpr float kIkEpsilonF = 1e-4f;
    constexpr float kPiF = 3.14159265358979323846f;
    constexpr float kRadiansToDegreesScaleF = 180.0f / kPiF;
    const float skeleton_scale_x = scale_x_;
    const float skeleton_scale_y = scale_y_;

    const auto clamp_mixf = [](double value) -> float {
        return value <= 0.0 ? 0.0f : value >= 1.0 ? 1.0f : value;
    };
    const auto radians_to_degreesf = [](float radians) {
        return radians * kRadiansToDegreesScaleF;
    };
    const auto normalize_rotation_degreesf = [](float angle) {
        return angle - (std::ceil(angle / 360.0f - 0.5f) * 360.0f);
    };
    const auto normalize_rotation_radiansf = [](float angle) {
        return angle - (std::ceil(angle / (kPiF * 2.0f) - 0.5f) * (kPiF * 2.0f));
    };
    const auto mix_rotation_degreesf =
        [&](float current, float target, float mix) {
            return current +
                normalize_rotation_degreesf(target - current) *
                    std::clamp(mix, 0.0f, 1.0f);
        };
    const auto mix_scalarf = [](float current, float target, float mix) {
        return current + ((target - current) * std::clamp(mix, 0.0f, 1.0f));
    };
    const auto add_verticesf =
        [](const AttachmentVertex& lhs, const AttachmentVertex& rhs) {
            AttachmentVertex result;
            result.x = lhs.x + rhs.x;
            result.y = lhs.y + rhs.y;
            return result;
        };
    const auto subtract_verticesf =
        [](const AttachmentVertex& lhs, const AttachmentVertex& rhs) {
            AttachmentVertex result;
            result.x = lhs.x - rhs.x;
            result.y = lhs.y - rhs.y;
            return result;
        };
    const auto vertex_lengthf = [](const AttachmentVertex& vertex) {
        return detail::fast_sqrtf(vertex.x * vertex.x + vertex.y * vertex.y);
    };
    const auto bone_tip_local_vectorf = [&](std::size_t bone_index) -> AttachmentVertex {
        if (bone_index >= bone_tip_local_vectors.size()) {
            return {};
        }
        return bone_tip_local_vectors[bone_index];
    };
    const auto to_parent_localf = [&](std::optional<std::size_t> parent_index,
                                      float world_x,
                                      float world_y) -> AttachmentVertex {
        if (!parent_index.has_value()) {
            AttachmentVertex result;
            result.x = world_x;
            result.y = world_y;
            return result;
        }

        ensure_world_transform(ensure_world_transform, *parent_index);
        const BoneWorldTransform parent_world = load_world_transform(*parent_index);
        const float determinant =
            (parent_world.a * parent_world.d) - (parent_world.b * parent_world.c);
        if (std::abs(determinant) <= kEpsilonF) {
            return {};
        }

        const float inverse_determinant = 1.0f / determinant;
        const float translated_x = world_x - parent_world.world_x;
        const float translated_y = world_y - parent_world.world_y;
        AttachmentVertex result;
        result.x =
            ((translated_x * parent_world.d) - (translated_y * parent_world.b)) *
            inverse_determinant;
        result.y =
            ((translated_y * parent_world.a) - (translated_x * parent_world.c)) *
            inverse_determinant;
        return result;
    };
    const auto world_axis_lengthf = [&](const BoneWorldTransform& world) {
        return detail::fast_sqrtf(world.a * world.a + world.c * world.c);
    };
    const auto invalidate_constraint_state = [&](ConstraintInputRevisionState* state) {
        if (state != nullptr) {
            state->valid = false;
        }
    };
    const auto restore_cached_constraint_outputs =
        [&](const ConstraintInputRevisionState& state,
            const std::vector<std::size_t>& bone_indices) {
            if (!state.valid || state.output_local_poses.size() != bone_indices.size()) {
                return false;
            }

            for (std::size_t pose_index = 0; pose_index < bone_indices.size(); ++pose_index) {
                const std::size_t bone_index = bone_indices[pose_index];
                if (bone_index >= solved_poses_.size()) {
                    return false;
                }

                if (local_pose_matches(
                        solved_poses_[bone_index].local_pose,
                        state.output_local_poses[pose_index])) {
                    continue;
                }

                solved_poses_[bone_index].local_pose = state.output_local_poses[pose_index];
                mark_bone_world_dirty(bone_index);
            }
            return true;
        };
    const auto constraint_inputs_match =
        [&](const ConstraintInputRevisionState& state,
            std::size_t driver_bone_index,
            const std::vector<std::size_t>& bone_indices) {
            if (!state.valid ||
                driver_bone_index >= constraint_input_world_revisions_.size() ||
                state.input_world_revisions.size() != 1U + bone_indices.size()) {
                return false;
            }

            if (state.input_world_revisions[0] !=
                constraint_input_world_revisions_[driver_bone_index]) {
                return false;
            }

            for (std::size_t pose_index = 0; pose_index < bone_indices.size(); ++pose_index) {
                const std::size_t bone_index = bone_indices[pose_index];
                if (bone_index >= constraint_input_world_revisions_.size() ||
                    state.input_world_revisions[pose_index + 1U] !=
                        constraint_input_world_revisions_[bone_index]) {
                    return false;
                }
            }
            return true;
        };
    const auto capture_constraint_state =
        [&](ConstraintInputRevisionState* state,
            std::size_t driver_bone_index,
            const std::vector<std::size_t>& bone_indices) {
            if (state == nullptr ||
                driver_bone_index >= constraint_input_world_revisions_.size()) {
                return;
            }

            resize_vector_without_reallocation(
                &state->input_world_revisions,
                1U + bone_indices.size(),
                &constraint_allocation_count_);
            resize_vector_without_reallocation(
                &state->output_local_poses,
                bone_indices.size(),
                &constraint_allocation_count_);
            state->input_world_revisions[0] =
                constraint_input_world_revisions_[driver_bone_index];
            for (std::size_t pose_index = 0; pose_index < bone_indices.size(); ++pose_index) {
                const std::size_t bone_index = bone_indices[pose_index];
                if (bone_index >= constraint_input_world_revisions_.size() ||
                    bone_index >= solved_poses_.size()) {
                    state->valid = false;
                    return;
                }

                state->input_world_revisions[pose_index + 1U] =
                    constraint_input_world_revisions_[bone_index];
                state->output_local_poses[pose_index] =
                    solved_poses_[bone_index].local_pose;
            }
            state->valid = true;
        };

    measure_seconds(
        compute_world_transforms,
        capture_timings ? &timing_breakdown->transform_seconds : nullptr);
    for (std::size_t bone_index = 0; bone_index < bone_count; ++bone_index) {
        const BoneWorldTransform current_world = load_world_transform(bone_index);
        if (constraint_input_world_revisions_[bone_index] == 0U ||
            !world_transform_matches(
                constraint_input_world_cache_[bone_index],
                current_world)) {
            bump_revision(&constraint_input_world_revisions_[bone_index]);
            constraint_input_world_cache_[bone_index] = current_world;
        }
    }
    const auto constraint_start = capture_timings ? clock::now() : clock::time_point{};

    for (std::size_t constraint_index = 0;
         constraint_index < data_->path_constraints().size();
         ++constraint_index) {
        if (constraint_index < active_path_constraints_.size() &&
            !active_path_constraints_[constraint_index]) {
            continue;
        }

        const PathConstraintData& constraint = data_->path_constraints()[constraint_index];
        const float translate_mix = clamp_mixf(constraint.translate_mix);
        const float rotate_mix = clamp_mixf(constraint.rotate_mix);
        if ((translate_mix <= 0.0f && rotate_mix <= 0.0f) ||
            constraint.slot_index >= slot_states_.size() ||
            constraint.slot_index >= data_->slots().size()) {
            continue;
        }

        const AttachmentData* attachment = current_attachment(constraint.slot_index);
        if (attachment == nullptr || !attachment->path_attachment.has_value()) {
            continue;
        }

        const std::size_t path_bone_index = data_->slots()[constraint.slot_index].bone_index;
        if (path_bone_index >= bone_count) {
            continue;
        }
        ensure_world_transform(ensure_world_transform, path_bone_index);

        const std::size_t control_point_count =
            attachment->path_attachment->control_points.size();
        resize_vector_without_reallocation(
            &path_world_control_points_,
            control_point_count,
            &constraint_allocation_count_);
        const BoneWorldTransform path_bone_transform = load_world_transform(path_bone_index);
        for (std::size_t point_index = 0; point_index < control_point_count; ++point_index) {
            const AttachmentVertex& control_point =
                attachment->path_attachment->control_points[point_index];
            const float point_x = control_point.x;
            const float point_y = control_point.y;
            path_world_control_points_[point_index] = {
                path_bone_transform.a * point_x +
                    path_bone_transform.b * point_y +
                    path_bone_transform.world_x,
                path_bone_transform.c * point_x +
                    path_bone_transform.d * point_y +
                    path_bone_transform.world_y,
            };
        }

        const std::size_t segment_count =
            control_point_count >= 4U ? (control_point_count - 1U) / 3U : 0U;
        ensure_vector_capacity(
            &path_distance_samples_,
            segment_count == 0U ? 0U : 1U + segment_count * 32U,
            &constraint_allocation_count_);
        detail::build_path_distance_samples(
            path_world_control_points_,
            &path_distance_samples_);
        if (path_distance_samples_.empty()) {
            continue;
        }

        const float total_length = path_distance_samples_.back().distance;
        const float spacing = constraint.spacing;
        const float spacing_distance =
            constraint.spacing_mode == PathConstraintSpacingMode::Percent
            ? total_length * spacing
            : spacing;
        const float position = constraint.position;

        for (std::size_t chain_index = 0;
             chain_index < constraint.bone_indices.size();
             ++chain_index) {
            const std::size_t bone_index = constraint.bone_indices[chain_index];
            if (bone_index >= solved_poses_.size() || !bone_is_active(bone_index)) {
                continue;
            }
            const float chain_offset = static_cast<float>(chain_index);
            const float sample_distance = std::clamp(
                position * total_length +
                    spacing_distance * chain_offset,
                0.0f,
                total_length);
            const PathDistanceSample sample =
                detail::sample_path_distance(path_distance_samples_, sample_distance);
            BoneTransform& pose = solved_poses_[bone_index].local_pose;
            const std::optional<std::size_t> parent_index = data_->bones()[bone_index].parent_index;
            float pose_x = pose.x;
            float pose_y = pose.y;
            float pose_rotation = pose.rotation;
            const float pose_shear_x = pose.shear_x;

            if (translate_mix > 0.0f) {
                const AttachmentVertex target_local =
                    to_parent_localf(parent_index, sample.point.x, sample.point.y);
                pose_x += (target_local.x - pose_x) * translate_mix;
                pose_y += (target_local.y - pose_y) * translate_mix;
            }

            if (rotate_mix > 0.0f && vertex_lengthf(sample.tangent) > kEpsilonF) {
                const AttachmentVertex tangent_end_world =
                    add_verticesf(sample.point, sample.tangent);
                const AttachmentVertex tangent_origin_local =
                    to_parent_localf(parent_index, sample.point.x, sample.point.y);
                const AttachmentVertex tangent_end_local = to_parent_localf(
                    parent_index,
                    tangent_end_world.x,
                    tangent_end_world.y);
                const AttachmentVertex tangent_local = subtract_verticesf(
                    tangent_end_local,
                    tangent_origin_local);
                if (vertex_lengthf(tangent_local) > kEpsilonF) {
                    const float desired_rotation =
                        radians_to_degreesf(
                            detail::fast_atan2f(tangent_local.y, tangent_local.x)) -
                        pose_shear_x;
                    pose_rotation = mix_rotation_degreesf(
                        pose_rotation,
                        desired_rotation,
                        rotate_mix);
                }
            }

            pose.x = pose_x;
            pose.y = pose_y;
            pose.rotation = pose_rotation;
            mark_bone_world_dirty(bone_index);
        }
    }

    for (std::size_t constraint_index = 0;
         constraint_index < data_->transform_constraints().size();
         ++constraint_index) {
        ConstraintInputRevisionState* const revision_state =
            constraint_index < transform_constraint_revision_states_.size()
            ? &transform_constraint_revision_states_[constraint_index]
            : nullptr;
        if (constraint_index < active_transform_constraints_.size() &&
            !active_transform_constraints_[constraint_index]) {
            invalidate_constraint_state(revision_state);
            continue;
        }

        const TransformConstraintData& constraint =
            data_->transform_constraints()[constraint_index];
        const auto require_valid_constraint_bone_index =
            [&](std::size_t bone_index, std::string_view role) {
                if (bone_index < data_->bones().size() &&
                    bone_index < solved_poses_.size() &&
                    bone_index < bone_count) {
                    return;
                }

                throw invalid_transform_constraint_bone_error(
                    constraint.name,
                    role,
                    bone_index,
                    data_->bones().size(),
                    solved_poses_.size(),
                    bone_count);
            };
        require_valid_constraint_bone_index(constraint.source_bone_index, "source");
        if (!bone_is_active(constraint.source_bone_index)) {
            invalidate_constraint_state(revision_state);
            continue;
        }

        const float rotate_mix = clamp_mixf(constraint.rotate_mix);
        const float translate_mix = clamp_mixf(constraint.translate_mix);
        const float scale_mix = clamp_mixf(constraint.scale_mix);
        const float shear_mix = clamp_mixf(constraint.shear_mix);
        const float offset_rotation = constraint.offsets.rotation;
        const float offset_x = constraint.offsets.x;
        const float offset_y = constraint.offsets.y;
        const float offset_scale_x = constraint.offsets.scale_x;
        const float offset_scale_y = constraint.offsets.scale_y;
        const float offset_shear_x = constraint.offsets.shear_x;
        const float offset_shear_y = constraint.offsets.shear_y;
        if (rotate_mix <= 0.0f && translate_mix <= 0.0f &&
            scale_mix <= 0.0f && shear_mix <= 0.0f) {
            invalidate_constraint_state(revision_state);
            continue;
        }

        bool has_active_target = false;
        for (const std::size_t bone_index : constraint.target_bone_indices) {
            require_valid_constraint_bone_index(bone_index, "target");
            if (bone_is_active(bone_index)) {
                has_active_target = true;
            }
        }
        if (!has_active_target) {
            invalidate_constraint_state(revision_state);
            continue;
        }

        if (revision_state != nullptr &&
            constraint_inputs_match(
                *revision_state,
                constraint.source_bone_index,
                constraint.target_bone_indices) &&
            restore_cached_constraint_outputs(
                *revision_state,
                constraint.target_bone_indices)) {
            if (capture_timings) {
                ++timing_breakdown->skipped_transform_constraints;
            }
            continue;
        }
        if (capture_timings) {
            ++timing_breakdown->evaluated_transform_constraints;
        }

        ensure_world_transform(ensure_world_transform, constraint.source_bone_index);
        const BoneTransform& source_pose =
            solved_poses_[constraint.source_bone_index].local_pose;
        const BoneWorldTransform source_world = load_world_transform(constraint.source_bone_index);
        const float source_rotation = source_pose.rotation;
        const float source_scale_x = source_pose.scale_x;
        const float source_scale_y = source_pose.scale_y;
        const float source_shear_x = source_pose.shear_x;
        const float source_shear_y = source_pose.shear_y;
        for (const std::size_t bone_index : constraint.target_bone_indices) {
            if (!bone_is_active(bone_index)) {
                continue;
            }

            BoneTransform& pose = solved_poses_[bone_index].local_pose;
            float pose_rotation = pose.rotation;
            float pose_x = pose.x;
            float pose_y = pose.y;
            float pose_scale_x = pose.scale_x;
            float pose_scale_y = pose.scale_y;
            float pose_shear_x = pose.shear_x;
            float pose_shear_y = pose.shear_y;

            if (rotate_mix > 0.0f) {
                const float desired_rotation = source_rotation + offset_rotation;
                pose_rotation = mix_rotation_degreesf(
                    pose_rotation,
                    desired_rotation,
                    rotate_mix);
            }
            if (translate_mix > 0.0f) {
                const AttachmentVertex target_local = to_parent_localf(
                    data_->bones()[bone_index].parent_index,
                    source_world.world_x + offset_x,
                    source_world.world_y + offset_y);
                pose_x = mix_scalarf(pose_x, target_local.x, translate_mix);
                pose_y = mix_scalarf(pose_y, target_local.y, translate_mix);
            }
            if (scale_mix > 0.0f) {
                pose_scale_x = mix_scalarf(
                    pose_scale_x,
                    source_scale_x + offset_scale_x,
                    scale_mix);
                pose_scale_y = mix_scalarf(
                    pose_scale_y,
                    source_scale_y + offset_scale_y,
                    scale_mix);
            }
            if (shear_mix > 0.0f) {
                pose_shear_x = mix_scalarf(
                    pose_shear_x,
                    source_shear_x + offset_shear_x,
                    shear_mix);
                pose_shear_y = mix_scalarf(
                    pose_shear_y,
                    source_shear_y + offset_shear_y,
                    shear_mix);
            }
            pose.rotation = pose_rotation;
            pose.x = pose_x;
            pose.y = pose_y;
            pose.scale_x = pose_scale_x;
            pose.scale_y = pose_scale_y;
            pose.shear_x = pose_shear_x;
            pose.shear_y = pose_shear_y;
            mark_bone_world_dirty(bone_index);
        }
        capture_constraint_state(
            revision_state,
            constraint.source_bone_index,
            constraint.target_bone_indices);
    }

    const auto safe_nonzero = [&](float value) {
        if (std::abs(value) > kIkEpsilonF) {
            return value;
        }
        return value < 0.0f ? -kIkEpsilonF : kIkEpsilonF;
    };
    const auto scale_sign = [](float value) {
        return value < 0.0f ? -1.0f : 1.0f;
    };
    const auto apply_one_bone_ik = [&](std::size_t bone_index,
                                       float target_x,
                                       float target_y,
                                       bool compress,
                                       bool stretch,
                                       float alpha) -> bool {
        if (bone_index >= solved_poses_.size() || !bone_is_active(bone_index)) {
            return false;
        }
        ensure_world_transform(ensure_world_transform, bone_index);

        const std::optional<std::size_t> parent_index = data_->bones()[bone_index].parent_index;
        if (!parent_index.has_value() || *parent_index >= bone_count) {
            return false;
        }

        BoneTransform& pose = solved_poses_[bone_index].local_pose;
        const BoneInherit inherit = solved_poses_[bone_index].inherit;
        const BoneWorldTransform bone_world = load_world_transform(bone_index);
        const BoneWorldTransform parent_world = load_world_transform(*parent_index);
        float pose_x = pose.x;
        float pose_y = pose.y;
        float pose_rotation = pose.rotation;
        const float pose_shear_x = pose.shear_x;
        const float pose_scale_y = pose.scale_y;

        float pa = parent_world.a;
        float pb = parent_world.b;
        float pc = parent_world.c;
        float pd = parent_world.d;
        float rotation_delta = -pose_shear_x - pose_rotation;
        float tx = 0.0f;
        float ty = 0.0f;

        switch (inherit) {
        case BoneInherit::OnlyTranslation:
            tx = (target_x - bone_world.world_x) * scale_sign(skeleton_scale_x);
            ty = (target_y - bone_world.world_y) * scale_sign(skeleton_scale_y);
            break;
        case BoneInherit::NoRotationOrReflection: {
            const float determinant = std::abs(pa * pd - pb * pc);
            const float scale = determinant / std::max(kIkEpsilonF, pa * pa + pc * pc);
            const float safe_scale_x = safe_nonzero(skeleton_scale_x);
            const float safe_scale_y = safe_nonzero(skeleton_scale_y);
            const float sa = pa / safe_scale_x;
            const float sc = pc / safe_scale_y;
            pb = -sc * scale * safe_scale_x;
            pd = sa * scale * safe_scale_y;
            rotation_delta += radians_to_degreesf(detail::fast_atan2f(sc, sa));
            [[fallthrough]];
        }
        default: {
            const float world_offset_x = target_x - parent_world.world_x;
            const float world_offset_y = target_y - parent_world.world_y;
            const float determinant = pa * pd - pb * pc;
            if (std::abs(determinant) > kIkEpsilonF) {
                tx = (world_offset_x * pd - world_offset_y * pb) / determinant - pose_x;
                ty = (world_offset_y * pa - world_offset_x * pc) / determinant - pose_y;
            }
            break;
        }
        }

        rotation_delta += radians_to_degreesf(detail::fast_atan2f(ty, tx));
        if (pose.scale_x < 0.0f) {
            rotation_delta += 180.0f;
        }
        rotation_delta = normalize_rotation_degreesf(rotation_delta);

        float scale_x = pose.scale_x;
        if (compress || stretch) {
            if (inherit == BoneInherit::NoScale ||
                inherit == BoneInherit::NoScaleOrReflection) {
                tx = target_x - bone_world.world_x;
                ty = target_y - bone_world.world_y;
            }

            const AttachmentVertex tip_local = bone_tip_local_vectorf(bone_index);
            const float bone_length = vertex_lengthf(tip_local);
            const float scaled_length = bone_length * scale_x;
            if (scaled_length > kIkEpsilonF) {
                const float target_distance_squared = tx * tx + ty * ty;
                if ((compress && target_distance_squared < scaled_length * scaled_length) ||
                    (stretch && target_distance_squared > scaled_length * scaled_length)) {
                    const float scale =
                        ((detail::fast_sqrtf(target_distance_squared) / scaled_length) - 1.0f) *
                            alpha +
                        1.0f;
                    scale_x *= scale;
                }
            }
        }

        pose.rotation = normalize_rotation_degreesf(pose_rotation + rotation_delta * alpha);
        pose.scale_x = scale_x;
        pose.scale_y = pose_scale_y;
        return true;
    };
    const auto apply_two_bone_ik = [&](std::size_t parent_bone_index,
                                       std::size_t child_bone_index,
                                       float target_x,
                                       float target_y,
                                       int bend_direction,
                                       bool stretch,
                                       float softness,
                                       float alpha) -> bool {
        if (parent_bone_index >= solved_poses_.size() ||
            child_bone_index >= solved_poses_.size() ||
            !bone_is_active(parent_bone_index) ||
            !bone_is_active(child_bone_index)) {
            return false;
        }
        if (solved_poses_[parent_bone_index].inherit != BoneInherit::Normal ||
            solved_poses_[child_bone_index].inherit != BoneInherit::Normal) {
            return false;
        }
        ensure_world_transform(ensure_world_transform, parent_bone_index);

        const std::optional<std::size_t> grandparent_index =
            data_->bones()[parent_bone_index].parent_index;
        if (!grandparent_index.has_value() ||
            *grandparent_index >= bone_count) {
            return false;
        }

        BoneTransform& parent_pose = solved_poses_[parent_bone_index].local_pose;
        BoneTransform& child_pose = solved_poses_[child_bone_index].local_pose;

        float px = parent_pose.x;
        float py = parent_pose.y;
        float parent_scale_x = parent_pose.scale_x;
        float parent_scale_y = parent_pose.scale_y;
        float stretched_parent_scale_x = parent_scale_x;
        float stretched_parent_scale_y = parent_scale_y;
        float child_scale_x = child_pose.scale_x;
        int parent_offset = 0;
        int child_offset = 0;
        int child_sign = 1;
        const float bend_directionf = bend_direction;

        if (parent_scale_x < 0.0f) {
            parent_scale_x = -parent_scale_x;
            parent_offset = 180;
            child_sign = -1;
        }
        if (parent_scale_y < 0.0f) {
            parent_scale_y = -parent_scale_y;
            child_sign = -child_sign;
        }
        if (child_scale_x < 0.0f) {
            child_scale_x = -child_scale_x;
            child_offset = 180;
        }

        const float cx = child_pose.x;
        float cy = child_pose.y;
        const BoneWorldTransform parent_world = load_world_transform(parent_bone_index);
        float a = parent_world.a;
        float b = parent_world.b;
        float c = parent_world.c;
        float d = parent_world.d;
        const bool uniform_scale = std::abs(parent_scale_x - parent_scale_y) <= kIkEpsilonF;
        float child_world_x = 0.0f;
        float child_world_y = 0.0f;
        if (!uniform_scale || stretch) {
            cy = 0.0f;
            child_world_x = a * cx + parent_world.world_x;
            child_world_y = c * cx + parent_world.world_y;
        } else {
            child_world_x = a * cx + b * cy + parent_world.world_x;
            child_world_y = c * cx + d * cy + parent_world.world_y;
        }

        const BoneWorldTransform grandparent_world = load_world_transform(*grandparent_index);
        a = grandparent_world.a;
        b = grandparent_world.b;
        c = grandparent_world.c;
        d = grandparent_world.d;

        float inverse_determinant = a * d - b * c;
        const float child_world_offset_x = child_world_x - grandparent_world.world_x;
        const float child_world_offset_y = child_world_y - grandparent_world.world_y;
        inverse_determinant =
            std::abs(inverse_determinant) <= kIkEpsilonF ? 0.0f : 1.0f / inverse_determinant;
        const float child_parent_x =
            (child_world_offset_x * d - child_world_offset_y * b) * inverse_determinant - px;
        const float child_parent_y =
            (child_world_offset_y * a - child_world_offset_x * c) * inverse_determinant - py;
        const float parent_length =
            detail::fast_sqrtf(child_parent_x * child_parent_x + child_parent_y * child_parent_y);

        const AttachmentVertex child_tip_local = bone_tip_local_vectorf(child_bone_index);
        const float child_length = vertex_lengthf(child_tip_local);
        if (parent_length < kIkEpsilonF || child_length < kIkEpsilonF) {
            if (!apply_one_bone_ik(parent_bone_index, target_x, target_y, false, stretch, alpha)) {
                return false;
            }
            child_pose.y = cy;
            child_pose.rotation = 0.0f;
            return true;
        }

        const float target_world_offset_x = target_x - grandparent_world.world_x;
        const float target_world_offset_y = target_y - grandparent_world.world_y;
        float target_parent_x =
            (target_world_offset_x * d - target_world_offset_y * b) * inverse_determinant - px;
        float target_parent_y =
            (target_world_offset_y * a - target_world_offset_x * c) * inverse_determinant - py;
        float distance_squared =
            target_parent_x * target_parent_x + target_parent_y * target_parent_y;

        float child_scaled_length = child_length * child_scale_x;
        if (softness > 0.0f) {
            const float softness_scale =
                std::max(0.0f, softness) * parent_scale_x * (child_scale_x + 1.0f) * 0.5f;
            if (softness_scale > kIkEpsilonF) {
                const float target_distance = detail::fast_sqrtf(distance_squared);
                const float softness_distance =
                    target_distance - parent_length - child_scaled_length * parent_scale_x +
                    softness_scale;
                if (softness_distance > 0.0f) {
                    const float softened =
                        std::min(1.0f, softness_distance / (softness_scale * 2.0f)) - 1.0f;
                    const float safe_target_distance =
                        target_distance <= kIkEpsilonF ? kIkEpsilonF : target_distance;
                    const float pull =
                        (softness_distance - softness_scale * (1.0f - softened * softened)) /
                        safe_target_distance;
                    target_parent_x -= pull * target_parent_x;
                    target_parent_y -= pull * target_parent_y;
                    distance_squared =
                        target_parent_x * target_parent_x + target_parent_y * target_parent_y;
                }
            }
        }

        float parent_angle = 0.0f;
        float child_angle = 0.0f;
        if (uniform_scale) {
            child_scaled_length *= parent_scale_x;
            const float denominator = 2.0f * parent_length * child_scaled_length;
            float cosine = denominator <= kIkEpsilonF
                ? 1.0f
                : (distance_squared - parent_length * parent_length -
                   child_scaled_length * child_scaled_length) /
                    denominator;
            if (cosine < -1.0f) {
                cosine = -1.0f;
                child_angle = kPiF * bend_directionf;
            } else if (cosine > 1.0f) {
                cosine = 1.0f;
                child_angle = 0.0f;
                if (stretch) {
                    const float total_length =
                        std::max(kIkEpsilonF, parent_length + child_scaled_length);
                    const float scale =
                        ((detail::fast_sqrtf(distance_squared) / total_length) - 1.0f) * alpha +
                        1.0f;
                    stretched_parent_scale_x *= scale;
                }
            } else {
                child_angle = detail::fast_acosf(cosine) * bend_directionf;
            }

            const float adjacent = parent_length + child_scaled_length * cosine;
            const float opposite = child_scaled_length * detail::fast_sinf(child_angle);
            parent_angle = detail::fast_atan2f(
                target_parent_y * adjacent - target_parent_x * opposite,
                target_parent_x * adjacent + target_parent_y * opposite);
        } else {
            const float scaled_child_x = parent_scale_x * child_scaled_length;
            const float scaled_child_y = parent_scale_y * child_scaled_length;
            const float scaled_child_x_squared = scaled_child_x * scaled_child_x;
            const float scaled_child_y_squared = scaled_child_y * scaled_child_y;
            const float target_angle = detail::fast_atan2f(target_parent_y, target_parent_x);
            float curve = scaled_child_y_squared * parent_length * parent_length +
                scaled_child_x_squared * distance_squared -
                scaled_child_x_squared * scaled_child_y_squared;
            const float curve_linear = -2.0f * scaled_child_y_squared * parent_length;
            const float curve_quadratic = scaled_child_y_squared - scaled_child_x_squared;
            const float discriminant =
                curve_linear * curve_linear - 4.0f * curve_quadratic * curve;
            if (discriminant >= 0.0f) {
                float root = detail::fast_sqrtf(discriminant);
                if (curve_linear < 0.0f) {
                    root = -root;
                }
                const float q = -(curve_linear + root) * 0.5f;
                const float r0 =
                    std::abs(curve_quadratic) <= kIkEpsilonF
                    ? std::numeric_limits<float>::infinity()
                    : q / curve_quadratic;
                const float r1 =
                    std::abs(q) <= kIkEpsilonF
                    ? std::numeric_limits<float>::infinity()
                    : curve / q;
                const float chosen_root = std::abs(r0) < std::abs(r1) ? r0 : r1;
                const float height_squared = distance_squared - chosen_root * chosen_root;
                if (std::isfinite(chosen_root) && height_squared >= 0.0f) {
                    const float height = detail::fast_sqrtf(height_squared) * bend_directionf;
                    const float safe_parent_scale_y = safe_nonzero(parent_scale_y);
                    const float safe_parent_scale_x = safe_nonzero(parent_scale_x);
                    parent_angle = target_angle - detail::fast_atan2f(height, chosen_root);
                    child_angle = detail::fast_atan2f(
                        height / safe_parent_scale_y,
                        (chosen_root - parent_length) / safe_parent_scale_x);
                } else {
                    parent_angle = std::numeric_limits<float>::quiet_NaN();
                }
            } else {
                parent_angle = std::numeric_limits<float>::quiet_NaN();
            }

            if (!std::isfinite(parent_angle) || !std::isfinite(child_angle)) {
                float min_angle = kPiF;
                float min_x = parent_length - scaled_child_x;
                float min_distance = min_x * min_x;
                float min_y = 0.0f;
                float max_angle = 0.0f;
                float max_x = parent_length + scaled_child_x;
                float max_distance = max_x * max_x;
                float max_y = 0.0f;
                const float denominator = scaled_child_x_squared - scaled_child_y_squared;
                if (std::abs(denominator) > kIkEpsilonF) {
                    const float value = -scaled_child_x * parent_length / denominator;
                    if (value >= -1.0f && value <= 1.0f) {
                        const float angle = detail::fast_acosf(value);
                        const float x =
                            scaled_child_x * detail::fast_cosf(angle) + parent_length;
                        const float y = scaled_child_y * detail::fast_sinf(angle);
                        const float distance = x * x + y * y;
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

                if (distance_squared <= (min_distance + max_distance) * 0.5f) {
                    parent_angle = target_angle -
                        detail::fast_atan2f(min_y * bend_directionf, min_x);
                    child_angle = min_angle * bend_directionf;
                } else {
                    parent_angle = target_angle -
                        detail::fast_atan2f(max_y * bend_directionf, max_x);
                    child_angle = max_angle * bend_directionf;
                }
            }
        }

        const float offset = detail::fast_atan2f(cy, cx) * child_sign;
        const float parent_rotation = parent_pose.rotation;
        const float parent_delta = normalize_rotation_degreesf(
            radians_to_degreesf(parent_angle - offset) +
            parent_offset - parent_rotation);
        parent_pose.rotation =
            normalize_rotation_degreesf(parent_rotation + parent_delta * alpha);
        parent_pose.scale_x = stretched_parent_scale_x;
        parent_pose.scale_y = stretched_parent_scale_y;
        parent_pose.shear_x = 0.0f;
        parent_pose.shear_y = 0.0f;

        const float child_rotation = child_pose.rotation;
        const float child_delta = normalize_rotation_degreesf(
            (radians_to_degreesf(child_angle + offset) -
             child_pose.shear_x) *
                child_sign +
            child_offset - child_rotation);
        child_pose.rotation = normalize_rotation_degreesf(child_rotation + child_delta * alpha);
        child_pose.y = cy;
        return true;
    };

    for (std::size_t constraint_index = 0;
         constraint_index < data_->ik_constraints().size();
         ++constraint_index) {
        ConstraintInputRevisionState* const revision_state =
            constraint_index < ik_constraint_revision_states_.size()
            ? &ik_constraint_revision_states_[constraint_index]
            : nullptr;
        if (constraint_index < active_ik_constraints_.size() &&
            !active_ik_constraints_[constraint_index]) {
            invalidate_constraint_state(revision_state);
            continue;
        }

        const IkConstraintData& constraint = data_->ik_constraints()[constraint_index];
        const float mix = clamp_mixf(constraint.mix);
        if (mix <= 0.0f || constraint.target_bone_index >= bone_count) {
            invalidate_constraint_state(revision_state);
            continue;
        }
        if (!bone_is_active(constraint.target_bone_index)) {
            invalidate_constraint_state(revision_state);
            continue;
        }

        if (constraint.bone_indices.size() != 1U && constraint.bone_indices.size() != 2U) {
            invalidate_constraint_state(revision_state);
            continue;
        }

        bool all_bones_active = true;
        for (const std::size_t bone_index : constraint.bone_indices) {
            if (bone_index >= bone_count || !bone_is_active(bone_index)) {
                all_bones_active = false;
                break;
            }
        }
        if (!all_bones_active) {
            invalidate_constraint_state(revision_state);
            continue;
        }

        if (revision_state != nullptr &&
            constraint_inputs_match(
                *revision_state,
                constraint.target_bone_index,
                constraint.bone_indices) &&
            restore_cached_constraint_outputs(
                *revision_state,
                constraint.bone_indices)) {
            if (capture_timings) {
                ++timing_breakdown->skipped_ik_constraints;
            }
            continue;
        }
        if (capture_timings) {
            ++timing_breakdown->evaluated_ik_constraints;
        }
        ensure_world_transform(ensure_world_transform, constraint.target_bone_index);
        const BoneWorldTransform target_world = load_world_transform(constraint.target_bone_index);

        if (constraint.bone_indices.size() == 1) {
            const std::size_t bone_index = constraint.bone_indices.front();
            if (apply_one_bone_ik(
                    bone_index,
                    target_world.world_x,
                    target_world.world_y,
                    constraint.compress,
                    constraint.stretch,
                    mix)) {
                mark_bone_world_dirty(bone_index);
            }
            capture_constraint_state(
                revision_state,
                constraint.target_bone_index,
                constraint.bone_indices);
            continue;
        }

        if (apply_two_bone_ik(
                constraint.bone_indices[0],
                constraint.bone_indices[1],
                target_world.world_x,
                target_world.world_y,
                constraint.bend_positive ? 1 : -1,
                constraint.stretch,
                constraint.softness,
                mix)) {
            mark_bone_world_dirty(constraint.bone_indices[0]);
            mark_bone_world_dirty(constraint.bone_indices[1]);
        }
        capture_constraint_state(
            revision_state,
            constraint.target_bone_index,
            constraint.bone_indices);
    }

    const auto finalize_constraint_state = [&]() {
        has_solved_constraint_state_ = true;
        solved_constraint_scope_revision_ = constraint_scope_revision_;
        solved_constraint_scale_x_ = scale_x_;
        solved_constraint_scale_y_ = scale_y_;
    };

    ensure_dirty_world_transforms_current();

    if (physics == PhysicsMode::None) {
        finalize_constraint_state();
        if (capture_timings) {
            timing_breakdown->constraint_seconds +=
                std::chrono::duration<double>(clock::now() - constraint_start).count();
        }
        return;
    }
    if (physics_constraint_states_.size() != data_->physics_constraints().size()) {
        physics_constraint_states_.resize(data_->physics_constraints().size());
    }

    const float physics_delta_seconds =
        physics == PhysicsMode::Update && pending_physics_delta_seconds_ > 0.0
        ? pending_physics_delta_seconds_
        : 0.0f;
    for (std::size_t constraint_index = 0;
         constraint_index < data_->physics_constraints().size();
         ++constraint_index) {
        const PhysicsConstraintData& constraint = data_->physics_constraints()[constraint_index];
        if (constraint_index < active_physics_constraints_.size() &&
            !active_physics_constraints_[constraint_index]) {
            continue;
        }

        const float mix = clamp_mixf(constraint.mix);
        if (mix <= 0.0f || constraint.bone_indices.empty()) {
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

        const float x_mix = constraint.x;
        const float y_mix = constraint.y;
        const float rotate_mix = constraint.rotate;
        const float shear_mix = constraint.shear_x;
        const float scale_mix = constraint.scale_x;
        const bool apply_x = x_mix > 0.0f;
        const bool apply_y = y_mix > 0.0f;
        const bool apply_rotate_or_shear = rotate_mix > 0.0f || shear_mix > 0.0f;
        const bool apply_scale = scale_mix > 0.0f;
        const float step_value = constraint.step;
        const float step = std::max(step_value, kEpsilonF);
        const float inertia = clamp_mixf(constraint.inertia);
        const float strength_value = constraint.strength;
        const float strength = std::max(0.0f, strength_value);
        const float mass_inverse = constraint.mass_inverse;
        const float mass_step = std::max(0.0f, mass_inverse) * step;
        const float damping_value = constraint.damping;
        const float damping_factor = std::exp(-std::max(0.0f, damping_value) * step);
        const float limit = constraint.limit;
        const AttachmentVertex external_force{
            constraint.wind.x * skeleton_scale_x,
            constraint.gravity.y * skeleton_scale_y,
        };
        const float position_limit_x =
            limit * physics_delta_seconds * std::abs(skeleton_scale_x);
        const float position_limit_y =
            limit * physics_delta_seconds * std::abs(skeleton_scale_y);

        float next_remaining = constraint_state.remaining;
        for (std::size_t chain_index = 0;
             chain_index < constraint.bone_indices.size();
             ++chain_index) {
            const std::size_t bone_index = constraint.bone_indices[chain_index];
            if (bone_index >= solved_poses_.size() || !bone_is_active(bone_index)) {
                continue;
            }

            const AttachmentVertex tip_local = bone_tip_local_vectorf(bone_index);
            const float tip_length = vertex_lengthf(tip_local);

            BoneWorldTransform bone_world = load_world_transform(bone_index);
            PhysicsBoneState& bone_state = constraint_state.bones[chain_index];
            if (constraint_state.reset) {
                bone_state.ux = bone_world.world_x;
                bone_state.uy = bone_world.world_y;
            } else if (physics == PhysicsMode::Update) {
                if (apply_x) {
                    const float delta = (bone_state.ux - bone_world.world_x) * inertia;
                    bone_state.x_offset += std::clamp(
                        delta,
                        -position_limit_x,
                        position_limit_x);
                    bone_state.ux = bone_world.world_x;
                }
                if (apply_y) {
                    const float delta = (bone_state.uy - bone_world.world_y) * inertia;
                    bone_state.y_offset += std::clamp(
                        delta,
                        -position_limit_y,
                        position_limit_y);
                    bone_state.uy = bone_world.world_y;
                }
            }

            float remaining = constraint_state.remaining;
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
                bone_world.world_x += bone_state.x_offset * mix * x_mix;
            }
            if (apply_y) {
                bone_world.world_y += bone_state.y_offset * mix * y_mix;
            }

            if (apply_rotate_or_shear || apply_scale) {
                const float base_angle = detail::fast_atan2f(bone_world.c, bone_world.a);
                float cos_angle = detail::fast_cosf(base_angle);
                float sin_angle = detail::fast_sinf(base_angle);
                float mixed_rotate = 0.0f;
                float dx = bone_state.cx - bone_world.world_x;
                float dy = bone_state.cy - bone_world.world_y;
                if (physics == PhysicsMode::Update) {
                    dx = std::clamp(dx, -position_limit_x, position_limit_x);
                    dy = std::clamp(dy, -position_limit_y, position_limit_y);

                    if (apply_rotate_or_shear) {
                        mixed_rotate = rotate_mix * mix;
                        const float angle =
                            detail::fast_atan2f(
                                dy + bone_state.ty,
                                dx + bone_state.tx) -
                            base_angle -
                            bone_state.rotate_offset * mixed_rotate;
                        bone_state.rotate_offset +=
                            normalize_rotation_radiansf(angle) * inertia;
                        const float rotated = bone_state.rotate_offset * mixed_rotate + base_angle;
                        cos_angle = detail::fast_cosf(rotated);
                        sin_angle = detail::fast_sinf(rotated);
                    }

                    if (apply_scale && tip_length > kEpsilonF) {
                        const float reference_length = std::max(
                            kEpsilonF,
                            tip_length * world_axis_lengthf(bone_world));
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

                                const float rotated =
                                    bone_state.rotate_offset * mixed_rotate + base_angle;
                                cos_angle = detail::fast_cosf(rotated);
                                sin_angle = detail::fast_sinf(rotated);
                            } else if (remaining < step) {
                                break;
                            }
                        }
                        next_remaining = remaining;
                    }
                }

                if (apply_rotate_or_shear) {
                    const float offset = bone_state.rotate_offset * mix;
                    float sine = 0.0f;
                    float cosine = 1.0f;
                    if (shear_mix > 0.0f) {
                        float angle = 0.0f;
                        if (rotate_mix > 0.0f) {
                            angle = offset * rotate_mix;
                            sine = detail::fast_sinf(angle);
                            cosine = detail::fast_cosf(angle);
                            const float axis_b = bone_world.b;
                            bone_world.b = cosine * axis_b - sine * bone_world.d;
                            bone_world.d = sine * axis_b + cosine * bone_world.d;
                        }
                        angle += offset * shear_mix;
                        sine = detail::fast_sinf(angle);
                        cosine = detail::fast_cosf(angle);
                        const float axis_a = bone_world.a;
                        bone_world.a = cosine * axis_a - sine * bone_world.c;
                        bone_world.c = sine * axis_a + cosine * bone_world.c;
                    } else {
                        const float angle = offset * rotate_mix;
                        sine = detail::fast_sinf(angle);
                        cosine = detail::fast_cosf(angle);
                        const float axis_a = bone_world.a;
                        bone_world.a = cosine * axis_a - sine * bone_world.c;
                        bone_world.c = sine * axis_a + cosine * bone_world.c;
                        const float axis_b = bone_world.b;
                        bone_world.b = cosine * axis_b - sine * bone_world.d;
                        bone_world.d = sine * axis_b + cosine * bone_world.d;
                    }
                }
                if (apply_scale) {
                    const float scale = 1.0f + bone_state.scale_offset * mix * scale_mix;
                    bone_world.a *= scale;
                    bone_world.c *= scale;
                }
            }

            if (physics != PhysicsMode::Pose) {
                if (tip_length > kEpsilonF) {
                    AttachmentVertex tip_world;
                    tip_world.x =
                        bone_world.a * tip_local.x + bone_world.b * tip_local.y +
                        bone_world.world_x;
                    tip_world.y =
                        bone_world.c * tip_local.x + bone_world.d * tip_local.y +
                        bone_world.world_y;
                    bone_state.tx = tip_world.x - bone_world.world_x;
                    bone_state.ty = tip_world.y - bone_world.world_y;
                } else {
                    bone_state.tx = 0.0f;
                    bone_state.ty = 0.0f;
                }
                bone_state.cx = bone_world.world_x;
                bone_state.cy = bone_world.world_y;
            }

            store_world_transform(bone_index, bone_world);
            recompute_world_children(recompute_world_children, bone_index);
        }

        if (physics == PhysicsMode::Update) {
            constraint_state.remaining = next_remaining;
            constraint_state.reset = false;
        } else if (physics == PhysicsMode::Reset) {
            constraint_state.remaining = 0.0f;
            constraint_state.reset = false;
        }
    }

    finalize_constraint_state();
    if (capture_timings) {
        timing_breakdown->constraint_seconds +=
            std::chrono::duration<double>(clock::now() - constraint_start).count();
    }
}

} // namespace marrow::runtime
