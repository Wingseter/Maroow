#include "mesh_weight_model.hpp"

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace marrow::editor::mesh_weight_model {

namespace {

struct ResolvedInfluence {
    std::size_t bone_index{0U};
    std::string bone_name;
    double x{0.0};
    double y{0.0};
    double weight{0.0};
};

/// Descending weight, then ascending skeleton bone index. A **total** order:
/// bone indices are unique after the merge step, so the sorted permutation is
/// unique and cannot depend on the caller's input order.
bool canonical_order(const ResolvedInfluence& lhs, const ResolvedInfluence& rhs) {
    if (lhs.weight != rhs.weight) {
        return lhs.weight > rhs.weight;
    }
    return lhs.bone_index < rhs.bone_index;
}

} // namespace

std::optional<MeshWeightBindOffset> inverse_transform_point_safe(
    const marrow::runtime::BoneWorldTransform& transform,
    double world_x,
    double world_y) {
    constexpr double kEpsilon = 1e-8;
    const double determinant =
        (static_cast<double>(transform.a) * static_cast<double>(transform.d)) -
        (static_cast<double>(transform.b) * static_cast<double>(transform.c));
    if (std::abs(determinant) <= kEpsilon) {
        return std::nullopt;
    }

    const double inverse_determinant = 1.0 / determinant;
    const double translated_x = world_x - static_cast<double>(transform.world_x);
    const double translated_y = world_y - static_cast<double>(transform.world_y);
    return MeshWeightBindOffset{
        ((translated_x * static_cast<double>(transform.d)) -
         (translated_y * static_cast<double>(transform.b))) *
            inverse_determinant,
        ((translated_y * static_cast<double>(transform.a)) -
         (translated_x * static_cast<double>(transform.c))) *
            inverse_determinant};
}

std::string canonicalize_mesh_weight_vertex(
    const marrow::runtime::SkeletonData& skeleton,
    marrow::editor::MeshWeightVertexEdit* vertex) {
    if (vertex == nullptr) {
        return "Canonicalization requires a weighted vertex.";
    }

    // Step 1 and 2: reject non-finite values and unresolvable bone names before
    // anything is computed, so a NaN can never reach the arithmetic below.
    // `NaN <= kMeshWeightEpsilon` is false, which is exactly how a NaN survived
    // both guards of the two normalizers this function replaces.
    std::vector<ResolvedInfluence> resolved;
    resolved.reserve(vertex->influences.size());
    for (const marrow::editor::MeshWeightInfluenceEdit& influence : vertex->influences) {
        if (influence.bone_name.empty()) {
            return "A weighted influence requires a non-empty bone name.";
        }
        if (!std::isfinite(influence.weight)) {
            return "Bone '" + influence.bone_name + "' has a non-finite weight.";
        }
        if (!std::isfinite(influence.x) || !std::isfinite(influence.y)) {
            return "Bone '" + influence.bone_name + "' has a non-finite bind offset.";
        }
        const auto bone_index = skeleton.find_bone_index(influence.bone_name);
        if (!bone_index.has_value()) {
            return "Bone not found: " + influence.bone_name;
        }
        resolved.push_back(ResolvedInfluence{
            *bone_index, influence.bone_name, influence.x, influence.y, influence.weight});
    }

    // Step 3: merge duplicate bones. The merged weight is the sum; the merged
    // bind offset is the weight-weighted mean, matching the averaging Smooth
    // already performs. A merged weight that is not positive is removed by
    // step 4, so the degenerate mean is never observed.
    std::vector<ResolvedInfluence> merged;
    merged.reserve(resolved.size());
    for (const ResolvedInfluence& influence : resolved) {
        const auto existing = std::find_if(
            merged.begin(),
            merged.end(),
            [&](const ResolvedInfluence& candidate) {
                return candidate.bone_index == influence.bone_index;
            });
        if (existing == merged.end()) {
            merged.push_back(influence);
            continue;
        }
        const double total = existing->weight + influence.weight;
        if (total > 0.0) {
            existing->x =
                ((existing->x * existing->weight) + (influence.x * influence.weight)) / total;
            existing->y =
                ((existing->y * existing->weight) + (influence.y * influence.weight)) / total;
        }
        existing->weight = total;
    }

    // Step 4: drop non-positive influences. Negative weights are dropped rather
    // than clamped and kept: the two differ only in whether a zero survives to
    // reach `validate_project_for_save()`, which refuses it.
    merged.erase(
        std::remove_if(
            merged.begin(),
            merged.end(),
            [](const ResolvedInfluence& influence) {
                return influence.weight <= kMeshWeightEpsilon;
            }),
        merged.end());

    // Steps 5 and 6: total-order sort, then cap.
    std::sort(merged.begin(), merged.end(), canonical_order);
    if (merged.size() > kMaxMeshWeightInfluences) {
        merged.resize(kMaxMeshWeightInfluences);
    }

    // Step 7: reject an empty result. Both file-format loaders require at least
    // one influence, so an empty list is not a representable vertex.
    if (merged.empty()) {
        return "A weighted vertex must keep at least one positive influence.";
    }

    // Step 8: normalize. Every surviving weight exceeds kMeshWeightEpsilon, so
    // the sum is positive; it can only fail to be finite by overflowing.
    double total_weight = 0.0;
    for (const ResolvedInfluence& influence : merged) {
        total_weight += influence.weight;
    }
    if (!std::isfinite(total_weight) || total_weight <= 0.0) {
        return "Weighted vertex influences must sum to a positive weight.";
    }
    if (std::abs(total_weight - 1.0) > kMeshWeightSumTolerance) {
        for (ResolvedInfluence& influence : merged) {
            influence.weight /= total_weight;
        }
        // Dividing rounds, so two weights that differed by one ULP can land on
        // the same value. The sort above ordered them on their pre-division
        // weights, so re-sort on the normalized ones -- otherwise the result
        // does not satisfy its own comparator and a second canonicalization
        // would reorder it.
        std::sort(merged.begin(), merged.end(), canonical_order);
    }

    vertex->influences.clear();
    vertex->influences.reserve(merged.size());
    for (const ResolvedInfluence& influence : merged) {
        vertex->influences.push_back(marrow::editor::MeshWeightInfluenceEdit{
            influence.bone_name, influence.x, influence.y, influence.weight});
    }
    return {};
}

marrow::editor::MeshWeightAttachmentEdit mesh_weight_edit_from_runtime(
    const marrow::runtime::SkeletonData& skeleton,
    std::string_view skin_name,
    std::string_view slot_name,
    std::string_view attachment_name,
    const marrow::runtime::AttachmentData& attachment) {
    marrow::editor::MeshWeightAttachmentEdit edit;
    edit.skin_name = std::string(skin_name);
    edit.slot_name = std::string(slot_name);
    edit.attachment_name = std::string(attachment_name);
    if (attachment.mesh_geometry == nullptr) {
        return edit;
    }

    edit.vertices.reserve(attachment.mesh_geometry->weights.size());
    for (const auto& runtime_vertex : attachment.mesh_geometry->weights) {
        marrow::editor::MeshWeightVertexEdit vertex;
        vertex.influences.reserve(runtime_vertex.influences.size());
        for (const auto& influence : runtime_vertex.influences) {
            if (influence.bone_index >= skeleton.bones().size()) {
                continue;
            }
            vertex.influences.push_back(marrow::editor::MeshWeightInfluenceEdit{
                skeleton.bones()[influence.bone_index].name,
                influence.x,
                influence.y,
                influence.weight});
        }
        edit.vertices.push_back(std::move(vertex));
    }
    return edit;
}

std::vector<marrow::runtime::BoneWorldTransform> setup_pose_bone_world_transforms(
    const std::shared_ptr<const marrow::runtime::SkeletonData>& skeleton) {
    std::vector<marrow::runtime::BoneWorldTransform> transforms;
    if (!skeleton) {
        return transforms;
    }

    marrow::runtime::Skeleton scratch(skeleton);
    scratch.set_to_setup_pose();
    scratch.update_world_transforms();
    const auto view = scratch.bone_world_transforms();
    transforms.reserve(view.size());
    for (std::size_t index = 0; index < view.size(); ++index) {
        transforms.push_back(view[index]);
    }
    return transforms;
}

std::vector<marrow::runtime::BoneWorldTransform> setup_pose_bone_world_transforms(
    const marrow::runtime::SkeletonData& skeleton) {
    // Aliasing constructor with a null owner: a non-owning view, not a second
    // owner of `skeleton`.
    return setup_pose_bone_world_transforms(
        std::shared_ptr<const marrow::runtime::SkeletonData>(
            std::shared_ptr<const void>{}, &skeleton));
}

std::string rebind_mesh_weight_vertex(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<marrow::runtime::BoneWorldTransform>& setup_transforms,
    marrow::editor::MeshWeightVertexEdit* vertex) {
    if (vertex == nullptr) {
        return "Rebinding requires a weighted vertex.";
    }
    if (vertex->influences.empty()) {
        return "A weighted vertex must keep at least one positive influence.";
    }

    struct Resolved {
        std::size_t bone_index{0U};
        double weight{0.0};
    };
    std::vector<Resolved> resolved;
    resolved.reserve(vertex->influences.size());
    for (const marrow::editor::MeshWeightInfluenceEdit& influence : vertex->influences) {
        if (!std::isfinite(influence.weight) || !std::isfinite(influence.x) ||
            !std::isfinite(influence.y)) {
            return "Bone '" + influence.bone_name + "' has a non-finite bind offset.";
        }
        const auto bone_index = skeleton.find_bone_index(influence.bone_name);
        if (!bone_index.has_value()) {
            return "Bone not found: " + influence.bone_name;
        }
        if (*bone_index >= setup_transforms.size()) {
            return "Bone '" + influence.bone_name + "' is outside the setup pose.";
        }
        resolved.push_back(Resolved{*bone_index, influence.weight});
    }

    // Step 1: the vertex's setup-world position, skinned from the offsets the
    // vertex already carries.
    double world_x = 0.0;
    double world_y = 0.0;
    for (std::size_t index = 0; index < resolved.size(); ++index) {
        const marrow::runtime::BoneWorldTransform& transform =
            setup_transforms[resolved[index].bone_index];
        const marrow::editor::MeshWeightInfluenceEdit& influence = vertex->influences[index];
        const double local_x = (influence.x * static_cast<double>(transform.a)) +
            (influence.y * static_cast<double>(transform.b)) +
            static_cast<double>(transform.world_x);
        const double local_y = (influence.x * static_cast<double>(transform.c)) +
            (influence.y * static_cast<double>(transform.d)) +
            static_cast<double>(transform.world_y);
        world_x += local_x * resolved[index].weight;
        world_y += local_y * resolved[index].weight;
    }
    // Materialized runtime weights need not sum to one, so recover the weighted
    // *average* rather than the raw skinning sum. A canonical vertex is
    // unaffected bit-for-bit, because its sum is inside the tolerance.
    double weight_total = 0.0;
    for (const Resolved& influence : resolved) {
        weight_total += influence.weight;
    }
    if (std::abs(weight_total - 1.0) > kMeshWeightSumTolerance) {
        if (!(weight_total > 0.0)) {
            return "Weighted vertex influences must sum to a positive weight.";
        }
        world_x /= weight_total;
        world_y /= weight_total;
    }
    if (!std::isfinite(world_x) || !std::isfinite(world_y)) {
        return "Rebinding produced a non-finite bind offset.";
    }

    // Step 2: express that one world point in every influencing bone's frame.
    marrow::editor::MeshWeightVertexEdit updated = *vertex;
    for (std::size_t index = 0; index < resolved.size(); ++index) {
        const marrow::runtime::BoneWorldTransform& transform =
            setup_transforms[resolved[index].bone_index];
        const auto bind = inverse_transform_point_safe(transform, world_x, world_y);
        if (!bind.has_value()) {
            return "Bone '" + updated.influences[index].bone_name +
                "' has a singular setup transform and cannot be rebound.";
        }
        if (!std::isfinite(bind->x) || !std::isfinite(bind->y)) {
            return "Rebinding produced a non-finite bind offset.";
        }
        updated.influences[index].x = bind->x;
        updated.influences[index].y = bind->y;
    }

    // Rebind changes no weight, so canonicalization is a no-op on already
    // canonical input; it is run anyway so a rebind can never be the step that
    // introduces a non-canonical vertex.
    if (const std::string error = canonicalize_mesh_weight_vertex(skeleton, &updated);
        !error.empty()) {
        return error;
    }
    *vertex = std::move(updated);
    return {};
}

} // namespace marrow::editor::mesh_weight_model
