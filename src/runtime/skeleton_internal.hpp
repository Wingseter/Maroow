#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "marrow/runtime/skeleton.hpp"

namespace marrow::runtime::detail {

enum class BoneTransformPropagationPath {
    Scalar,
    SSE2,
    NEON,
};

struct BoneLocalTransformBuffers {
    std::vector<float>* x{nullptr};
    std::vector<float>* y{nullptr};
    std::vector<float>* a{nullptr};
    std::vector<float>* b{nullptr};
    std::vector<float>* c{nullptr};
    std::vector<float>* d{nullptr};

    void resize(std::size_t count) const;
};

struct BoneWorldTransformBuffers {
    std::vector<float>* a{nullptr};
    std::vector<float>* b{nullptr};
    std::vector<float>* c{nullptr};
    std::vector<float>* d{nullptr};
    std::vector<float>* world_x{nullptr};
    std::vector<float>* world_y{nullptr};

    void resize(std::size_t count) const;
};

std::vector<std::size_t> build_bone_evaluation_order(const std::vector<BoneData>& bones);
void reorder_topologically(
    std::vector<BoneData>* bones,
    std::vector<IkConstraintData>* ik_constraints,
    std::vector<PathConstraintData>* path_constraints,
    std::vector<TransformConstraintData>* transform_constraints,
    std::vector<PhysicsConstraintData>* physics_constraints,
    std::vector<SlotData>* slots,
    std::vector<AnimationData>* animations,
    std::vector<SkinData>* skins);

std::optional<std::size_t> find_bone_index(
    const std::vector<BoneData>& bones,
    std::string_view name);
std::optional<std::size_t> find_slot_index(
    const std::vector<SlotData>& slots,
    std::string_view name);
std::optional<std::size_t> find_skin_index(
    const std::vector<SkinData>& skins,
    std::string_view name);
const AnimationData* find_animation(
    const std::vector<AnimationData>& animations,
    std::string_view name);
const AttachmentData* find_attachment_source_in_skins(
    const std::vector<SkinData>& skins,
    std::optional<std::size_t> default_skin_index,
    std::size_t slot_index,
    std::string_view attachment_name,
    std::optional<std::size_t>* skin_index_out);

double degrees_to_radians(double degrees);
double radians_to_degrees(double radians);
double clamp_mix(double mix);
double normalize_rotation_degrees(double angle);
double normalize_rotation_radians(double angle);
double mix_rotation_degrees(double current, double target, double mix);
double mix_scalar(double current, double target, double mix);
float fast_sqrtf(float value);
float fast_sinf(float angle);
float fast_cosf(float angle);
float fast_atan2f(float y, float x);
float fast_acosf(float value);

AttachmentVertex add_vertices(const AttachmentVertex& lhs, const AttachmentVertex& rhs);
AttachmentVertex subtract_vertices(const AttachmentVertex& lhs, const AttachmentVertex& rhs);
float vertex_length(const AttachmentVertex& vertex);
double world_axis_length(const BoneWorldTransform& transform);

AttachmentVertex inverse_transform_point(
    const BoneWorldTransform& transform,
    double world_x,
    double world_y);
BoneWorldTransform root_world_transform(
    const BoneTransform& transform,
    double skeleton_scale_x,
    double skeleton_scale_y);
BoneWorldTransform compose_world_transform(
    const BoneWorldTransform& parent,
    const BonePose& pose,
    double skeleton_scale_x,
    double skeleton_scale_y);
AttachmentVertex transform_attachment_vertex(
    const BoneWorldTransform& transform,
    double x,
    double y);
double transform_attachment_rotation(
    const BoneWorldTransform& transform,
    double local_rotation);
MeshWorldVertex transform_mesh_point(
    const BoneWorldTransform& transform,
    double x,
    double y);
void prepare_local_transform_buffer(
    std::size_t bone_index,
    const BoneTransform& transform,
    const BoneLocalTransformBuffers& buffers);
void prepare_local_transform_buffers(
    const std::vector<BonePose>& poses,
    const BoneLocalTransformBuffers& buffers);
BoneWorldTransform load_world_transform(
    std::size_t bone_index,
    const BoneWorldTransformBuffers& world);
void store_world_transform(
    std::size_t bone_index,
    const BoneWorldTransform& transform,
    const BoneWorldTransformBuffers& world);
void store_world_components(
    std::size_t bone_index,
    float a,
    float b,
    float c,
    float d,
    float world_x,
    float world_y,
    const BoneWorldTransformBuffers& world);
BoneWorldTransform compose_cached_world_transform(
    const BoneWorldTransform* parent,
    const BonePose& pose,
    const BoneLocalTransformBuffers& local,
    std::size_t bone_index,
    double skeleton_scale_x,
    double skeleton_scale_y);
void propagate_world_transforms_scalar(
    const std::vector<BoneData>& bones,
    const std::vector<BonePose>& poses,
    double skeleton_scale_x,
    double skeleton_scale_y,
    const BoneLocalTransformBuffers& local,
    const BoneWorldTransformBuffers& world);
void propagate_world_transforms_optimized(
    const std::vector<BoneData>& bones,
    const std::vector<BonePose>& poses,
    double skeleton_scale_x,
    double skeleton_scale_y,
    const BoneLocalTransformBuffers& local,
    const BoneWorldTransformBuffers& world);
BoneTransformPropagationPath active_bone_transform_propagation_path();
std::string_view bone_transform_propagation_path_name(BoneTransformPropagationPath path);

json::LoadResult load_binary_skeleton_document(
    const std::filesystem::path& path,
    bool apply_animation_optimizations,
    SkeletonBinaryInspection* inspection_out = nullptr);

std::size_t clamp_sequence_frame_index(
    const AttachmentSequenceData& sequence,
    std::size_t frame_index);
std::size_t positive_mod(long long value, std::size_t modulus);
std::size_t map_ping_pong_frame(std::size_t cycle_position, std::size_t frame_count);

bool attachment_matches_mesh_deform_source(
    const AttachmentData& attachment,
    std::string_view source_attachment_name);

void build_path_distance_samples(
    const std::vector<AttachmentVertex>& control_points,
    std::vector<PathDistanceSample>* samples_out);
std::vector<PathDistanceSample> build_path_distance_samples(
    const std::vector<AttachmentVertex>& control_points);
PathDistanceSample sample_path_distance(
    const std::vector<PathDistanceSample>& samples,
    float distance);

bool polygon_contains_point(
    const std::vector<AttachmentVertex>& polygon,
    double x,
    double y);
bool polygon_intersects_segment(
    const std::vector<AttachmentVertex>& polygon,
    const AttachmentVertex& start,
    const AttachmentVertex& end);

} // namespace marrow::runtime::detail
