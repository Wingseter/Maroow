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

std::vector<BoneSetupSegment> bone_setup_segments(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<marrow::runtime::BoneWorldTransform>& setup_transforms) {
    const std::vector<marrow::runtime::BoneData>& bones = skeleton.bones();
    // One ascending loop over `min(bones, transforms)`. Indexing by bone index
    // is what makes the result's order the skeleton's order; a bone without a
    // setup transform gets no segment rather than a fabricated one.
    const std::size_t count = std::min(bones.size(), setup_transforms.size());
    std::vector<BoneSetupSegment> segments;
    segments.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        BoneSetupSegment segment;
        segment.end_x = static_cast<double>(setup_transforms[index].world_x);
        segment.end_y = static_cast<double>(setup_transforms[index].world_y);
        const std::optional<std::size_t> parent = bones[index].parent_index;
        if (parent.has_value() && *parent < count) {
            segment.start_x = static_cast<double>(setup_transforms[*parent].world_x);
            segment.start_y = static_cast<double>(setup_transforms[*parent].world_y);
        } else {
            // A root bone -- and a bone whose parent has no setup transform --
            // degenerates to the point at its own origin. The viewport draws no
            // body for a parentless bone either.
            segment.start_x = segment.end_x;
            segment.start_y = segment.end_y;
        }
        segments.push_back(segment);
    }
    return segments;
}

double point_segment_distance_squared(
    double point_x,
    double point_y,
    const BoneSetupSegment& segment) {
    const double ab_x = segment.end_x - segment.start_x;
    const double ab_y = segment.end_y - segment.start_y;
    const double ab_length_squared = (ab_x * ab_x) + (ab_y * ab_y);
    if (!(ab_length_squared > 0.0)) {
        // Zero-length segment, or a NaN from a non-finite setup transform.
        const double point_delta_x = point_x - segment.start_x;
        const double point_delta_y = point_y - segment.start_y;
        return (point_delta_x * point_delta_x) + (point_delta_y * point_delta_y);
    }

    double projection = (((point_x - segment.start_x) * ab_x) +
                         ((point_y - segment.start_y) * ab_y)) /
        ab_length_squared;
    if (projection < 0.0) {
        projection = 0.0;
    } else if (projection > 1.0) {
        projection = 1.0;
    }
    const double closest_x = segment.start_x + (ab_x * projection);
    const double closest_y = segment.start_y + (ab_y * projection);
    const double delta_x = point_x - closest_x;
    const double delta_y = point_y - closest_y;
    return (delta_x * delta_x) + (delta_y * delta_y);
}

std::string generate_mesh_weight_vertex(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<marrow::runtime::BoneWorldTransform>& setup_transforms,
    const std::vector<BoneSetupSegment>& segments,
    const std::vector<std::size_t>& candidate_bone_indices,
    marrow::editor::MeshWeightVertexEdit* vertex) {
    if (vertex == nullptr) {
        return "Generating weights requires a weighted vertex.";
    }
    if (candidate_bone_indices.empty()) {
        return "mesh.generate_weights requires at least one candidate bone.";
    }

    const std::vector<marrow::runtime::BoneData>& bones = skeleton.bones();
    for (std::size_t position = 0; position < candidate_bone_indices.size(); ++position) {
        const std::size_t candidate = candidate_bone_indices[position];
        if (candidate >= bones.size() || candidate >= setup_transforms.size() ||
            candidate >= segments.size()) {
            return "A candidate bone is outside the setup pose.";
        }
        // The caller de-duplicates, but a repeated index would break the strict
        // total order below, and a precondition nothing checks is one no test
        // can hold anyone to. Quadratic over at most a skeleton's worth of
        // bones, and it runs once per vertex.
        for (std::size_t earlier = 0; earlier < position; ++earlier) {
            if (candidate_bone_indices[earlier] == candidate) {
                return "A candidate bone was listed more than once.";
            }
        }
    }

    // Step 1: the vertex's setup-world position, from the influences it already
    // carries. Shared verbatim with rebind -- `geometry.vertices` is decorative
    // for a weighted mesh and is not an alternative source.
    double world_x = 0.0;
    double world_y = 0.0;
    if (const std::string error = setup_world_position_of_weight_vertex(
            skeleton, setup_transforms, *vertex, &world_x, &world_y);
        !error.empty()) {
        return error;
    }

    // Step 2: measure every candidate.
    std::vector<std::pair<double, std::size_t>> ranked;
    ranked.reserve(candidate_bone_indices.size());
    for (const std::size_t candidate : candidate_bone_indices) {
        const double distance_squared =
            point_segment_distance_squared(world_x, world_y, segments[candidate]);
        if (!std::isfinite(distance_squared)) {
            return "Bone '" + bones[candidate].name + "' has a non-finite setup-pose segment.";
        }
        ranked.emplace_back(distance_squared, candidate);
    }

    // Step 3: order by ascending distance, then ascending skeleton index. Bone
    // indices are unique, so this is a strict TOTAL order and the sorted
    // sequence is unique -- which is why `std::sort`'s instability is
    // irrelevant and why `std::stable_sort` would only hide a duplicate.
    std::sort(
        ranked.begin(),
        ranked.end(),
        [](const std::pair<double, std::size_t>& lhs,
           const std::pair<double, std::size_t>& rhs) {
            if (lhs.first != rhs.first) {
                return lhs.first < rhs.first;
            }
            return lhs.second < rhs.second;
        });

    // Step 4: the nearest-candidate fallback. `1/0` is not a weight, and a
    // vertex sitting on a bone is unambiguously that bone's.
    const auto emit_nearest_only = [&]() -> std::string {
        const std::size_t nearest = ranked.front().second;
        const auto bind =
            inverse_transform_point_safe(setup_transforms[nearest], world_x, world_y);
        if (!bind.has_value()) {
            return "Bone '" + bones[nearest].name +
                "' has a singular setup transform and cannot be used as a weight candidate.";
        }
        marrow::editor::MeshWeightVertexEdit updated;
        updated.influences.push_back(marrow::editor::MeshWeightInfluenceEdit{
            bones[nearest].name, bind->x, bind->y, 1.0});
        if (const std::string error = canonicalize_mesh_weight_vertex(skeleton, &updated);
            !error.empty()) {
            return error;
        }
        *vertex = std::move(updated);
        return {};
    };
    if (ranked.front().first == 0.0) {
        return emit_nearest_only();
    }

    // Step 5: cap to the four nearest, BEFORE normalizing. Normalizing over all
    // candidates first would make the drop threshold relative to candidates
    // that were never kept, and would guarantee a second division inside the
    // canonicalizer.
    const std::size_t kept = std::min(kMaxMeshWeightInfluences, ranked.size());

    // Step 6: weigh by inverse square.
    std::vector<double> raw_weights;
    raw_weights.reserve(kept);
    for (std::size_t index = 0; index < kept; ++index) {
        const double raw_weight = 1.0 / ranked[index].first;
        if (!std::isfinite(raw_weight)) {
            // Reachable only when d^2 underflowed to a denormal. A vertex
            // 1e-160 units from a bone is the same authoring intent as one
            // sitting on it, so fall back rather than reject.
            return emit_nearest_only();
        }
        raw_weights.push_back(raw_weight);
    }

    // Step 7: normalize BEFORE the canonicalizer sees anything. This is what
    // turns the canonicalizer's absolute `<= kMeshWeightEpsilon` drop -- which
    // runs before its own normalization -- into a relative one. Without it the
    // gate means "farther than 1000 world units", which silently deletes a
    // legitimate influence at 10x this fixture's scale and rejects the vertex
    // outright at 100x. Summed left to right over the sorted top-K, in double,
    // in one sequential loop: the order is part of the guarantee.
    double total_weight = 0.0;
    for (const double raw_weight : raw_weights) {
        total_weight += raw_weight;
    }
    if (!std::isfinite(total_weight) || !(total_weight > 0.0)) {
        return emit_nearest_only();
    }

    // Step 8: bind each survivor's offset in its own bone's setup frame.
    marrow::editor::MeshWeightVertexEdit updated;
    updated.influences.reserve(kept);
    for (std::size_t index = 0; index < kept; ++index) {
        const std::size_t bone_index = ranked[index].second;
        const auto bind =
            inverse_transform_point_safe(setup_transforms[bone_index], world_x, world_y);
        if (!bind.has_value()) {
            return "Bone '" + bones[bone_index].name +
                "' has a singular setup transform and cannot be used as a weight candidate.";
        }
        if (!std::isfinite(bind->x) || !std::isfinite(bind->y)) {
            return "Bone '" + bones[bone_index].name + "' has a non-finite setup-pose segment.";
        }
        updated.influences.push_back(marrow::editor::MeshWeightInfluenceEdit{
            bones[bone_index].name, bind->x, bind->y, raw_weights[index] / total_weight});
    }

    // Step 9: the canonicalizer owns what a valid weight list is. MAR-176 adds a
    // producer, not a rule.
    if (const std::string error = canonicalize_mesh_weight_vertex(skeleton, &updated);
        !error.empty()) {
        return error;
    }
    *vertex = std::move(updated);
    return {};
}

std::string setup_world_position_of_weight_vertex(
    const marrow::runtime::SkeletonData& skeleton,
    const std::vector<marrow::runtime::BoneWorldTransform>& setup_transforms,
    const marrow::editor::MeshWeightVertexEdit& vertex,
    double* world_x_out,
    double* world_y_out) {
    if (world_x_out == nullptr || world_y_out == nullptr) {
        return "A setup-world position requires output parameters.";
    }
    if (vertex.influences.empty()) {
        return "A weighted vertex must keep at least one positive influence.";
    }

    struct Resolved {
        std::size_t bone_index{0U};
        double weight{0.0};
    };
    std::vector<Resolved> resolved;
    resolved.reserve(vertex.influences.size());
    for (const marrow::editor::MeshWeightInfluenceEdit& influence : vertex.influences) {
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

    // The vertex's setup-world position, skinned from the offsets the vertex
    // already carries.
    double world_x = 0.0;
    double world_y = 0.0;
    for (std::size_t index = 0; index < resolved.size(); ++index) {
        const marrow::runtime::BoneWorldTransform& transform =
            setup_transforms[resolved[index].bone_index];
        const marrow::editor::MeshWeightInfluenceEdit& influence = vertex.influences[index];
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

    *world_x_out = world_x;
    *world_y_out = world_y;
    return {};
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

    // Step 1: the vertex's setup-world position. Shared verbatim with
    // `generate_mesh_weight_vertex()` so the two cannot derive it differently.
    double world_x = 0.0;
    double world_y = 0.0;
    if (const std::string error = setup_world_position_of_weight_vertex(
            skeleton, setup_transforms, *vertex, &world_x, &world_y);
        !error.empty()) {
        return error;
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
