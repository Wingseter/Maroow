#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

#include "marrow/runtime/skeleton.hpp"

namespace marrow::runtime::detail {

std::vector<std::size_t> build_bone_evaluation_order(const std::vector<BoneData>& bones);

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

AttachmentVertex add_vertices(const AttachmentVertex& lhs, const AttachmentVertex& rhs);
AttachmentVertex subtract_vertices(const AttachmentVertex& lhs, const AttachmentVertex& rhs);
double vertex_length(const AttachmentVertex& vertex);
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

struct PathDistanceSample {
    double distance{0.0};
    AttachmentVertex point{};
    AttachmentVertex tangent{};
};

std::vector<PathDistanceSample> build_path_distance_samples(
    const std::vector<AttachmentVertex>& control_points);
PathDistanceSample sample_path_distance(
    const std::vector<PathDistanceSample>& samples,
    double distance);

bool polygon_contains_point(
    const std::vector<AttachmentVertex>& polygon,
    double x,
    double y);
bool polygon_intersects_segment(
    const std::vector<AttachmentVertex>& polygon,
    const AttachmentVertex& start,
    const AttachmentVertex& end);

} // namespace marrow::runtime::detail
