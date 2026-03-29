#include "skeleton_internal.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#if defined(__SSE2__) && (defined(__x86_64__) || defined(_M_X64))
#include <emmintrin.h>
#define MARROW_BONE_PROPAGATION_SSE2 1
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(__aarch64__)
#include <arm_neon.h>
#define MARROW_BONE_PROPAGATION_NEON 1
#endif

namespace marrow::runtime::detail {
BoneWorldTransform load_world_transform(
    std::size_t bone_index,
    const BoneWorldTransformBuffers& world) {
    BoneWorldTransform transform;
    transform.a = (*world.a)[bone_index];
    transform.b = (*world.b)[bone_index];
    transform.c = (*world.c)[bone_index];
    transform.d = (*world.d)[bone_index];
    transform.world_x = (*world.world_x)[bone_index];
    transform.world_y = (*world.world_y)[bone_index];
    return transform;
}

void store_world_transform(
    std::size_t bone_index,
    const BoneWorldTransform& transform,
    const BoneWorldTransformBuffers& world) {
    (*world.a)[bone_index] = static_cast<float>(transform.a);
    (*world.b)[bone_index] = static_cast<float>(transform.b);
    (*world.c)[bone_index] = static_cast<float>(transform.c);
    (*world.d)[bone_index] = static_cast<float>(transform.d);
    (*world.world_x)[bone_index] = static_cast<float>(transform.world_x);
    (*world.world_y)[bone_index] = static_cast<float>(transform.world_y);
}

void store_world_components(
    std::size_t bone_index,
    float a,
    float b,
    float c,
    float d,
    float world_x,
    float world_y,
    const BoneWorldTransformBuffers& world) {
    (*world.a)[bone_index] = a;
    (*world.b)[bone_index] = b;
    (*world.c)[bone_index] = c;
    (*world.d)[bone_index] = d;
    (*world.world_x)[bone_index] = world_x;
    (*world.world_y)[bone_index] = world_y;
}

namespace {

template <typename T>
void remap_index(T* value, const std::vector<std::size_t>& old_to_new) {
    if (value == nullptr) {
        return;
    }

    *value = static_cast<T>(old_to_new[static_cast<std::size_t>(*value)]);
}

void remap_optional_index(
    std::optional<std::size_t>* value,
    const std::vector<std::size_t>& old_to_new) {
    if (value == nullptr || !value->has_value()) {
        return;
    }

    *value = old_to_new[value->value()];
}

void sincosf_compat(float angle, float* sine_out, float* cosine_out) {
#if defined(__APPLE__)
    ::__sincosf(angle, sine_out, cosine_out);
#elif defined(__linux__) || defined(__GLIBC__)
    ::sincosf(angle, sine_out, cosine_out);
#else
    *sine_out = std::sinf(angle);
    *cosine_out = std::cosf(angle);
#endif
}

void propagate_scalar_bone(
    std::size_t bone_index,
    const std::vector<BoneData>& bones,
    const std::vector<BonePose>& poses,
    double skeleton_scale_x,
    double skeleton_scale_y,
    const BoneLocalTransformBuffers& local,
    const BoneWorldTransformBuffers& world) {
    const BonePose& pose = poses[bone_index];
    const std::optional<std::size_t> parent_index = bones[bone_index].parent_index;
    if (!parent_index.has_value()) {
        store_world_components(
            bone_index,
            (*local.a)[bone_index] * static_cast<float>(skeleton_scale_x),
            (*local.b)[bone_index] * static_cast<float>(skeleton_scale_x),
            (*local.c)[bone_index] * static_cast<float>(skeleton_scale_y),
            (*local.d)[bone_index] * static_cast<float>(skeleton_scale_y),
            (*local.x)[bone_index] * static_cast<float>(skeleton_scale_x),
            (*local.y)[bone_index] * static_cast<float>(skeleton_scale_y),
            world);
        return;
    }

    if (pose.inherit == BoneInherit::Normal) {
        const std::size_t parent = *parent_index;
        const float pa = (*world.a)[parent];
        const float pb = (*world.b)[parent];
        const float pc = (*world.c)[parent];
        const float pd = (*world.d)[parent];
        const float lx = (*local.x)[bone_index];
        const float ly = (*local.y)[bone_index];
        const float la = (*local.a)[bone_index];
        const float lb = (*local.b)[bone_index];
        const float lc = (*local.c)[bone_index];
        const float ld = (*local.d)[bone_index];
        const float wx = pa * lx + pb * ly + (*world.world_x)[parent];
        const float wy = pc * lx + pd * ly + (*world.world_y)[parent];
        const float wa = pa * la + pb * lc;
        const float wb = pa * lb + pb * ld;
        const float wc = pc * la + pd * lc;
        const float wd = pc * lb + pd * ld;
        store_world_components(bone_index, wa, wb, wc, wd, wx, wy, world);
        return;
    }

    if (pose.inherit == BoneInherit::OnlyTranslation) {
        const std::size_t parent = *parent_index;
        const float pa = (*world.a)[parent];
        const float pb = (*world.b)[parent];
        const float pc = (*world.c)[parent];
        const float pd = (*world.d)[parent];
        const float lx = (*local.x)[bone_index];
        const float ly = (*local.y)[bone_index];
        const float wx = pa * lx + pb * ly + (*world.world_x)[parent];
        const float wy = pc * lx + pd * ly + (*world.world_y)[parent];
        store_world_components(
            bone_index,
            (*local.a)[bone_index] * static_cast<float>(skeleton_scale_x),
            (*local.b)[bone_index] * static_cast<float>(skeleton_scale_x),
            (*local.c)[bone_index] * static_cast<float>(skeleton_scale_y),
            (*local.d)[bone_index] * static_cast<float>(skeleton_scale_y),
            wx,
            wy,
            world);
        return;
    }

    const BoneWorldTransform parent_world = load_world_transform(*parent_index, world);
    store_world_transform(
        bone_index,
        compose_world_transform(parent_world, pose, skeleton_scale_x, skeleton_scale_y),
        world);
}

bool can_batch(
    const std::vector<BoneData>& bones,
    const std::vector<BonePose>& poses,
    std::size_t start_index,
    BoneInherit inherit) {
    if (start_index + 4U > bones.size()) {
        return false;
    }

    for (std::size_t lane = 0; lane < 4U; ++lane) {
        const std::size_t bone_index = start_index + lane;
        const std::optional<std::size_t> parent_index = bones[bone_index].parent_index;
        if (!parent_index.has_value() ||
            poses[bone_index].inherit != inherit ||
            *parent_index >= start_index) {
            return false;
        }
    }

    return true;
}

bool bones_are_already_topological(const std::vector<BoneData>& bones) {
    for (std::size_t bone_index = 0; bone_index < bones.size(); ++bone_index) {
        const std::optional<std::size_t> parent_index = bones[bone_index].parent_index;
        if (parent_index.has_value() && *parent_index >= bone_index) {
            return false;
        }
    }

    return true;
}

std::vector<std::size_t> build_stable_topological_order(const std::vector<BoneData>& bones) {
    std::vector<std::size_t> order;
    order.reserve(bones.size());
    std::vector<unsigned char> state(bones.size(), 0);

    const auto visit = [&](const auto& self, std::size_t bone_index) -> void {
        if (state[bone_index] == 2) {
            return;
        }
        if (state[bone_index] == 1) {
            throw std::invalid_argument("cyclic bone hierarchy detected");
        }

        state[bone_index] = 1;
        if (bones[bone_index].parent_index.has_value()) {
            self(self, *bones[bone_index].parent_index);
        }
        state[bone_index] = 2;
        order.push_back(bone_index);
    };

    for (std::size_t bone_index = 0; bone_index < bones.size(); ++bone_index) {
        visit(visit, bone_index);
    }

    return order;
}

#if defined(MARROW_BONE_PROPAGATION_SSE2) || defined(MARROW_BONE_PROPAGATION_NEON)
#if defined(MARROW_BONE_PROPAGATION_SSE2)
using Simd4f = __m128;

inline Simd4f simd_load(const float* values) {
    return _mm_loadu_ps(values);
}

inline Simd4f simd_splat(float value) {
    return _mm_set1_ps(value);
}

inline Simd4f simd_add(Simd4f lhs, Simd4f rhs) {
    return _mm_add_ps(lhs, rhs);
}

inline Simd4f simd_mul(Simd4f lhs, Simd4f rhs) {
    return _mm_mul_ps(lhs, rhs);
}

inline void simd_store(float* destination, Simd4f value) {
    _mm_storeu_ps(destination, value);
}
#else
using Simd4f = float32x4_t;

inline Simd4f simd_load(const float* values) {
    return vld1q_f32(values);
}

inline Simd4f simd_splat(float value) {
    return vdupq_n_f32(value);
}

inline Simd4f simd_add(Simd4f lhs, Simd4f rhs) {
    return vaddq_f32(lhs, rhs);
}

inline Simd4f simd_mul(Simd4f lhs, Simd4f rhs) {
    return vmulq_f32(lhs, rhs);
}

inline void simd_store(float* destination, Simd4f value) {
    vst1q_f32(destination, value);
}
#endif

Simd4f gather_parent_lane(
    const std::vector<float>& values,
    const std::array<std::size_t, 4U>& parents) {
    if (parents[0] == parents[1] && parents[1] == parents[2] && parents[2] == parents[3]) {
        return simd_splat(values[parents[0]]);
    }

    alignas(16) float gathered[4U] = {
        values[parents[0]],
        values[parents[1]],
        values[parents[2]],
        values[parents[3]],
    };
    return simd_load(gathered);
}

void propagate_simd_normal_batch(
    std::size_t start_index,
    const std::vector<BoneData>& bones,
    const BoneLocalTransformBuffers& local,
    const BoneWorldTransformBuffers& world) {
    const std::array<std::size_t, 4U> parents = {
        *bones[start_index].parent_index,
        *bones[start_index + 1U].parent_index,
        *bones[start_index + 2U].parent_index,
        *bones[start_index + 3U].parent_index,
    };

    const Simd4f pa = gather_parent_lane(*world.a, parents);
    const Simd4f pb = gather_parent_lane(*world.b, parents);
    const Simd4f pc = gather_parent_lane(*world.c, parents);
    const Simd4f pd = gather_parent_lane(*world.d, parents);
    const Simd4f pwx = gather_parent_lane(*world.world_x, parents);
    const Simd4f pwy = gather_parent_lane(*world.world_y, parents);
    const Simd4f lx = simd_load(local.x->data() + start_index);
    const Simd4f ly = simd_load(local.y->data() + start_index);
    const Simd4f la = simd_load(local.a->data() + start_index);
    const Simd4f lb = simd_load(local.b->data() + start_index);
    const Simd4f lc = simd_load(local.c->data() + start_index);
    const Simd4f ld = simd_load(local.d->data() + start_index);

    const Simd4f wx = simd_add(simd_add(simd_mul(pa, lx), simd_mul(pb, ly)), pwx);
    const Simd4f wy = simd_add(simd_add(simd_mul(pc, lx), simd_mul(pd, ly)), pwy);
    const Simd4f wa = simd_add(simd_mul(pa, la), simd_mul(pb, lc));
    const Simd4f wb = simd_add(simd_mul(pa, lb), simd_mul(pb, ld));
    const Simd4f wc = simd_add(simd_mul(pc, la), simd_mul(pd, lc));
    const Simd4f wd = simd_add(simd_mul(pc, lb), simd_mul(pd, ld));

    simd_store(world.world_x->data() + start_index, wx);
    simd_store(world.world_y->data() + start_index, wy);
    simd_store(world.a->data() + start_index, wa);
    simd_store(world.b->data() + start_index, wb);
    simd_store(world.c->data() + start_index, wc);
    simd_store(world.d->data() + start_index, wd);
}

void propagate_simd_translation_batch(
    std::size_t start_index,
    const std::vector<BoneData>& bones,
    double skeleton_scale_x,
    double skeleton_scale_y,
    const BoneLocalTransformBuffers& local,
    const BoneWorldTransformBuffers& world) {
    const std::array<std::size_t, 4U> parents = {
        *bones[start_index].parent_index,
        *bones[start_index + 1U].parent_index,
        *bones[start_index + 2U].parent_index,
        *bones[start_index + 3U].parent_index,
    };

    const Simd4f pa = gather_parent_lane(*world.a, parents);
    const Simd4f pb = gather_parent_lane(*world.b, parents);
    const Simd4f pc = gather_parent_lane(*world.c, parents);
    const Simd4f pd = gather_parent_lane(*world.d, parents);
    const Simd4f pwx = gather_parent_lane(*world.world_x, parents);
    const Simd4f pwy = gather_parent_lane(*world.world_y, parents);
    const Simd4f lx = simd_load(local.x->data() + start_index);
    const Simd4f ly = simd_load(local.y->data() + start_index);
    const Simd4f local_a = simd_load(local.a->data() + start_index);
    const Simd4f local_b = simd_load(local.b->data() + start_index);
    const Simd4f local_c = simd_load(local.c->data() + start_index);
    const Simd4f local_d = simd_load(local.d->data() + start_index);
    const Simd4f scale_x = simd_splat(static_cast<float>(skeleton_scale_x));
    const Simd4f scale_y = simd_splat(static_cast<float>(skeleton_scale_y));

    const Simd4f wx = simd_add(simd_add(simd_mul(pa, lx), simd_mul(pb, ly)), pwx);
    const Simd4f wy = simd_add(simd_add(simd_mul(pc, lx), simd_mul(pd, ly)), pwy);
    const Simd4f wa = simd_mul(local_a, scale_x);
    const Simd4f wb = simd_mul(local_b, scale_x);
    const Simd4f wc = simd_mul(local_c, scale_y);
    const Simd4f wd = simd_mul(local_d, scale_y);

    simd_store(world.world_x->data() + start_index, wx);
    simd_store(world.world_y->data() + start_index, wy);
    simd_store(world.a->data() + start_index, wa);
    simd_store(world.b->data() + start_index, wb);
    simd_store(world.c->data() + start_index, wc);
    simd_store(world.d->data() + start_index, wd);
}
#endif

} // namespace

void BoneLocalTransformBuffers::resize(std::size_t count) const {
    x->resize(count);
    y->resize(count);
    a->resize(count);
    b->resize(count);
    c->resize(count);
    d->resize(count);
}

void BoneWorldTransformBuffers::resize(std::size_t count) const {
    a->resize(count);
    b->resize(count);
    c->resize(count);
    d->resize(count);
    world_x->resize(count);
    world_y->resize(count);
}

void reorder_topologically(
    std::vector<BoneData>* bones,
    std::vector<IkConstraintData>* ik_constraints,
    std::vector<PathConstraintData>* path_constraints,
    std::vector<TransformConstraintData>* transform_constraints,
    std::vector<PhysicsConstraintData>* physics_constraints,
    std::vector<SlotData>* slots,
    std::vector<AnimationData>* animations,
    std::vector<SkinData>* skins) {
    if (bones == nullptr || bones->empty()) {
        return;
    }

    if (bones_are_already_topological(*bones)) {
        return;
    }

    (void)build_bone_evaluation_order(*bones);
    const std::vector<std::size_t> order = build_stable_topological_order(*bones);
    std::vector<std::size_t> old_to_new(order.size(), 0);
    for (std::size_t new_index = 0; new_index < order.size(); ++new_index) {
        const std::size_t old_index = order[new_index];
        old_to_new[old_index] = new_index;
    }

    std::vector<BoneData> reordered_bones;
    reordered_bones.reserve(bones->size());
    for (const std::size_t old_index : order) {
        reordered_bones.push_back(std::move((*bones)[old_index]));
    }
    for (BoneData& bone : reordered_bones) {
        remap_optional_index(&bone.parent_index, old_to_new);
    }
    *bones = std::move(reordered_bones);

    if (ik_constraints != nullptr) {
        for (IkConstraintData& constraint : *ik_constraints) {
            for (std::size_t& bone_index : constraint.bone_indices) {
                remap_index(&bone_index, old_to_new);
            }
            remap_index(&constraint.target_bone_index, old_to_new);
        }
    }

    if (path_constraints != nullptr) {
        for (PathConstraintData& constraint : *path_constraints) {
            for (std::size_t& bone_index : constraint.bone_indices) {
                remap_index(&bone_index, old_to_new);
            }
        }
    }

    if (transform_constraints != nullptr) {
        for (TransformConstraintData& constraint : *transform_constraints) {
            remap_index(&constraint.source_bone_index, old_to_new);
            for (std::size_t& bone_index : constraint.target_bone_indices) {
                remap_index(&bone_index, old_to_new);
            }
        }
    }

    if (physics_constraints != nullptr) {
        for (PhysicsConstraintData& constraint : *physics_constraints) {
            for (std::size_t& bone_index : constraint.bone_indices) {
                remap_index(&bone_index, old_to_new);
            }
        }
    }

    if (slots != nullptr) {
        for (SlotData& slot : *slots) {
            remap_index(&slot.bone_index, old_to_new);
        }
    }

    if (animations != nullptr) {
        for (AnimationData& animation : *animations) {
            for (std::size_t& bone_index : animation.targeted_bone_indices) {
                remap_index(&bone_index, old_to_new);
            }
            for (BoneRotateTimeline& timeline : animation.bone_rotate_timelines) {
                remap_index(&timeline.bone_index, old_to_new);
            }
            for (BoneInheritTimeline& timeline : animation.bone_inherit_timelines) {
                remap_index(&timeline.bone_index, old_to_new);
            }
            for (BoneTranslateTimeline& timeline : animation.bone_translate_timelines) {
                remap_index(&timeline.bone_index, old_to_new);
            }
            for (BoneScaleTimeline& timeline : animation.bone_scale_timelines) {
                remap_index(&timeline.bone_index, old_to_new);
            }
            for (BoneShearTimeline& timeline : animation.bone_shear_timelines) {
                remap_index(&timeline.bone_index, old_to_new);
            }
        }
    }

    if (skins == nullptr) {
        return;
    }

    std::unordered_map<const MeshGeometry*, std::shared_ptr<const MeshGeometry>> remapped_meshes;
    for (SkinData& skin : *skins) {
        for (std::size_t& bone_index : skin.bone_indices) {
            remap_index(&bone_index, old_to_new);
        }

        for (SkinSlotData& slot_attachment : skin.slot_attachments) {
            AttachmentData& attachment = slot_attachment.attachment;
            if (attachment.mesh_geometry == nullptr) {
                continue;
            }

            const MeshGeometry* source_geometry = attachment.mesh_geometry.get();
            const auto remapped_it = remapped_meshes.find(source_geometry);
            if (remapped_it != remapped_meshes.end()) {
                attachment.mesh_geometry = remapped_it->second;
                continue;
            }

            auto remapped_geometry = std::make_shared<MeshGeometry>(*source_geometry);
            for (MeshGeometry::VertexWeights& weights : remapped_geometry->weights) {
                for (MeshGeometry::VertexWeight& influence : weights.influences) {
                    remap_index(&influence.bone_index, old_to_new);
                }
            }

            remapped_meshes.emplace(source_geometry, remapped_geometry);
            attachment.mesh_geometry = std::move(remapped_geometry);
        }
    }
}

void prepare_local_transform_buffer(
    std::size_t bone_index,
    const BoneTransform& transform,
    const BoneLocalTransformBuffers& buffers) {
    const float x = static_cast<float>(transform.x);
    const float y = static_cast<float>(transform.y);
    const float rotation_x = static_cast<float>(
        degrees_to_radians(transform.rotation + transform.shear_x));
    const float rotation_y = static_cast<float>(
        degrees_to_radians(transform.rotation + 90.0 + transform.shear_y));
    float sin_rotation_x = 0.0f;
    float cos_rotation_x = 1.0f;
    float sin_rotation_y = 0.0f;
    float cos_rotation_y = 1.0f;
    sincosf_compat(rotation_x, &sin_rotation_x, &cos_rotation_x);
    sincosf_compat(rotation_y, &sin_rotation_y, &cos_rotation_y);

    (*buffers.x)[bone_index] = x;
    (*buffers.y)[bone_index] = y;
    (*buffers.a)[bone_index] = cos_rotation_x * static_cast<float>(transform.scale_x);
    (*buffers.b)[bone_index] = cos_rotation_y * static_cast<float>(transform.scale_y);
    (*buffers.c)[bone_index] = sin_rotation_x * static_cast<float>(transform.scale_x);
    (*buffers.d)[bone_index] = sin_rotation_y * static_cast<float>(transform.scale_y);
}

void prepare_local_transform_buffers(
    const std::vector<BonePose>& poses,
    const BoneLocalTransformBuffers& buffers) {
    buffers.resize(poses.size());
    for (std::size_t bone_index = 0; bone_index < poses.size(); ++bone_index) {
        prepare_local_transform_buffer(bone_index, poses[bone_index].local_pose, buffers);
    }
}

BoneWorldTransform compose_cached_world_transform(
    const BoneWorldTransform* parent,
    const BonePose& pose,
    const BoneLocalTransformBuffers& local,
    std::size_t bone_index,
    double skeleton_scale_x,
    double skeleton_scale_y) {
    const double local_x = static_cast<double>((*local.x)[bone_index]);
    const double local_y = static_cast<double>((*local.y)[bone_index]);
    const double local_a = static_cast<double>((*local.a)[bone_index]);
    const double local_b = static_cast<double>((*local.b)[bone_index]);
    const double local_c = static_cast<double>((*local.c)[bone_index]);
    const double local_d = static_cast<double>((*local.d)[bone_index]);

    if (parent == nullptr) {
        BoneWorldTransform world_transform;
        world_transform.a = local_a * skeleton_scale_x;
        world_transform.b = local_b * skeleton_scale_x;
        world_transform.c = local_c * skeleton_scale_y;
        world_transform.d = local_d * skeleton_scale_y;
        world_transform.world_x = local_x * skeleton_scale_x;
        world_transform.world_y = local_y * skeleton_scale_y;
        return world_transform;
    }

    if (pose.inherit == BoneInherit::Normal) {
        BoneWorldTransform world_transform;
        world_transform.world_x =
            parent->a * local_x + parent->b * local_y + parent->world_x;
        world_transform.world_y =
            parent->c * local_x + parent->d * local_y + parent->world_y;
        world_transform.a = parent->a * local_a + parent->b * local_c;
        world_transform.b = parent->a * local_b + parent->b * local_d;
        world_transform.c = parent->c * local_a + parent->d * local_c;
        world_transform.d = parent->c * local_b + parent->d * local_d;
        return world_transform;
    }

    if (pose.inherit == BoneInherit::OnlyTranslation) {
        BoneWorldTransform world_transform;
        world_transform.world_x =
            parent->a * local_x + parent->b * local_y + parent->world_x;
        world_transform.world_y =
            parent->c * local_x + parent->d * local_y + parent->world_y;
        world_transform.a = local_a * skeleton_scale_x;
        world_transform.b = local_b * skeleton_scale_x;
        world_transform.c = local_c * skeleton_scale_y;
        world_transform.d = local_d * skeleton_scale_y;
        return world_transform;
    }

    return compose_world_transform(*parent, pose, skeleton_scale_x, skeleton_scale_y);
}

void propagate_world_transforms_scalar(
    const std::vector<BoneData>& bones,
    const std::vector<BonePose>& poses,
    double skeleton_scale_x,
    double skeleton_scale_y,
    const BoneLocalTransformBuffers& local,
    const BoneWorldTransformBuffers& world) {
    world.resize(bones.size());
    for (std::size_t bone_index = 0; bone_index < bones.size(); ++bone_index) {
        propagate_scalar_bone(
            bone_index,
            bones,
            poses,
            skeleton_scale_x,
            skeleton_scale_y,
            local,
            world);
    }
}

void propagate_world_transforms_optimized(
    const std::vector<BoneData>& bones,
    const std::vector<BonePose>& poses,
    double skeleton_scale_x,
    double skeleton_scale_y,
    const BoneLocalTransformBuffers& local,
    const BoneWorldTransformBuffers& world) {
    world.resize(bones.size());
#if defined(MARROW_BONE_PROPAGATION_SSE2) || defined(MARROW_BONE_PROPAGATION_NEON)
    std::size_t bone_index = 0;
    while (bone_index < bones.size()) {
        if (can_batch(bones, poses, bone_index, BoneInherit::Normal)) {
            propagate_simd_normal_batch(
                bone_index,
                bones,
                local,
                world);
            bone_index += 4U;
            continue;
        }
        if (can_batch(bones, poses, bone_index, BoneInherit::OnlyTranslation)) {
            propagate_simd_translation_batch(
                bone_index,
                bones,
                skeleton_scale_x,
                skeleton_scale_y,
                local,
                world);
            bone_index += 4U;
            continue;
        }

        propagate_scalar_bone(
            bone_index,
            bones,
            poses,
            skeleton_scale_x,
            skeleton_scale_y,
            local,
            world);
        ++bone_index;
    }
#else
    propagate_world_transforms_scalar(
        bones,
        poses,
        skeleton_scale_x,
        skeleton_scale_y,
        local,
        world);
#endif
}

BoneTransformPropagationPath active_bone_transform_propagation_path() {
#if defined(MARROW_BONE_PROPAGATION_SSE2)
    return BoneTransformPropagationPath::SSE2;
#elif defined(MARROW_BONE_PROPAGATION_NEON)
    return BoneTransformPropagationPath::NEON;
#else
    return BoneTransformPropagationPath::Scalar;
#endif
}

std::string_view bone_transform_propagation_path_name(BoneTransformPropagationPath path) {
    switch (path) {
    case BoneTransformPropagationPath::Scalar:
        return "scalar";
    case BoneTransformPropagationPath::SSE2:
        return "sse2";
    case BoneTransformPropagationPath::NEON:
        return "neon";
    }

    return "unknown";
}

} // namespace marrow::runtime::detail
