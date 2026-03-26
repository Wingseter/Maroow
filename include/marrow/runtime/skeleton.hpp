#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/runtime/animation.hpp"
#include "marrow/runtime/json.hpp"

namespace marrow::runtime {

class AnimationState;

struct SkeletonInfo {
    std::string name;
    double width{0.0};
    double height{0.0};
};

struct BoneTransform {
    double x{0.0};
    double y{0.0};
    double rotation{0.0};
    double scale_x{1.0};
    double scale_y{1.0};
    double shear_x{0.0};
    double shear_y{0.0};
};

struct BoneData {
    std::string name;
    std::optional<std::size_t> parent_index;
    BoneTransform setup_pose;
    BoneInherit inherit{BoneInherit::Normal};
};

struct IkConstraintData {
    std::string name;
    std::vector<std::size_t> bone_indices;
    std::size_t target_bone_index{0};
    double mix{1.0};
    double softness{0.0};
    bool bend_positive{true};
    bool compress{false};
    bool stretch{false};
};

enum class PathConstraintSpacingMode {
    Length,
    Percent,
};

struct PathConstraintData {
    std::string name;
    std::size_t slot_index{0};
    std::vector<std::size_t> bone_indices;
    double position{0.0};
    double spacing{0.0};
    PathConstraintSpacingMode spacing_mode{PathConstraintSpacingMode::Length};
    double rotate_mix{1.0};
    double translate_mix{1.0};
};

struct TransformConstraintOffsets {
    double rotation{0.0};
    double x{0.0};
    double y{0.0};
    double scale_x{0.0};
    double scale_y{0.0};
    double shear_x{0.0};
    double shear_y{0.0};
};

struct TransformConstraintData {
    std::string name;
    std::size_t source_bone_index{0};
    std::vector<std::size_t> target_bone_indices;
    double rotate_mix{0.0};
    double translate_mix{0.0};
    double scale_mix{0.0};
    double shear_mix{0.0};
    TransformConstraintOffsets offsets{};
};

enum class BlendMode {
    Normal,
    Additive,
    Multiply,
    Screen,
};

struct SlotData {
    std::string name;
    std::size_t bone_index{0};
    std::string setup_attachment;
    BlendMode blend_mode{BlendMode::Normal};
    SlotColor color{};
    std::optional<SlotColor> dark_color;
};

enum class AttachmentKind {
    Region,
    Mesh,
    LinkedMesh,
    Point,
    BoundingBox,
    Clipping,
    Path,
};

enum class SequencePlaybackMode {
    Hold,
    Once,
    Loop,
    PingPong,
    OnceReverse,
    LoopReverse,
    PingPongReverse,
};

struct AttachmentVertex {
    double x{0.0};
    double y{0.0};
};

struct PhysicsConstraintData {
    std::string name;
    std::vector<std::size_t> bone_indices;
    double step{1.0 / 60.0};
    double x{1.0};
    double y{1.0};
    double rotate{1.0};
    double scale_x{1.0};
    double shear_x{0.0};
    double limit{500.0};
    double inertia{0.0};
    double damping{0.0};
    double strength{0.0};
    double mass_inverse{1.0};
    AttachmentVertex gravity{};
    AttachmentVertex wind{};
    double mix{1.0};
};

enum class PhysicsMode {
    None,
    Reset,
    Update,
    Pose,
};

struct MeshGeometry {
    std::vector<double> vertices;
    std::vector<std::size_t> triangles;
    std::vector<double> uvs;
    struct VertexWeight {
        std::size_t bone_index{0};
        double x{0.0};
        double y{0.0};
        double weight{0.0};
    };

    struct VertexWeights {
        std::vector<VertexWeight> influences;
    };

    std::vector<VertexWeights> weights;
};

struct LinkedMeshData {
    std::string parent_attachment;
    std::optional<std::string> parent_skin_name;
    std::optional<std::size_t> parent_skin_index;
    bool deform{false};
};

struct PointAttachmentData {
    AttachmentVertex local_position{};
    double rotation{0.0};
};

struct BoundingBoxAttachmentData {
    std::vector<AttachmentVertex> polygon;
};

struct AttachmentSequenceData {
    std::vector<std::string> frame_regions;
    double fps{0.0};
    SequencePlaybackMode playback_mode{SequencePlaybackMode::Hold};
    std::size_t setup_frame{0};
};

struct ClippingAttachmentData {
    std::vector<AttachmentVertex> polygon;
    std::optional<std::size_t> end_slot_index;
    std::string end_slot_name;
};

struct PathAttachmentData {
    std::vector<AttachmentVertex> control_points;
};

struct AttachmentData {
    std::string name;
    AttachmentKind kind{AttachmentKind::Region};
    std::string region_name;
    std::shared_ptr<const MeshGeometry> mesh_geometry;
    std::optional<LinkedMeshData> linked_mesh;
    std::optional<PointAttachmentData> point_attachment;
    std::optional<BoundingBoxAttachmentData> bounding_box;
    std::optional<AttachmentSequenceData> sequence;
    std::optional<ClippingAttachmentData> clipping_attachment;
    std::optional<PathAttachmentData> path_attachment;
};

struct SkinSlotData {
    std::size_t slot_index{0};
    AttachmentData attachment;
};

struct SkinData {
    std::string name;
    std::vector<std::size_t> bone_indices;
    std::vector<SkinSlotData> slot_attachments;
    std::vector<std::size_t> ik_constraint_indices;
    std::vector<std::size_t> path_constraint_indices;
    std::vector<std::size_t> transform_constraint_indices;
    std::vector<std::size_t> physics_constraint_indices;

    const AttachmentData* find_attachment(std::size_t slot_index) const;
    const AttachmentData* find_attachment(std::string_view attachment_name) const;
};

struct AnimationData {
    std::string name;
    std::vector<std::size_t> targeted_bone_indices;
    std::vector<BoneRotateTimeline> bone_rotate_timelines;
    std::vector<BoneInheritTimeline> bone_inherit_timelines;
    std::vector<BoneTranslateTimeline> bone_translate_timelines;
    std::vector<BoneScaleTimeline> bone_scale_timelines;
    std::vector<BoneShearTimeline> bone_shear_timelines;
    std::vector<SlotAttachmentTimeline> slot_attachment_timelines;
    std::vector<SlotColorTimeline> slot_color_timelines;
    std::vector<MeshDeformTimeline> mesh_deform_timelines;
    std::optional<DrawOrderTimeline> draw_order_timeline_data;
    std::optional<EventTimeline> event_timeline_data;

    const BoneRotateTimeline* find_rotate_timeline(std::size_t bone_index) const;
    const BoneInheritTimeline* find_inherit_timeline(std::size_t bone_index) const;
    const BoneTranslateTimeline* find_translate_timeline(std::size_t bone_index) const;
    const BoneScaleTimeline* find_scale_timeline(std::size_t bone_index) const;
    const BoneShearTimeline* find_shear_timeline(std::size_t bone_index) const;
    const SlotAttachmentTimeline* find_attachment_timeline(std::size_t slot_index) const;
    const SlotColorTimeline* find_color_timeline(std::size_t slot_index) const;
    const MeshDeformTimeline* find_deform_timeline(
        std::size_t slot_index,
        std::string_view attachment_name) const;
    const DrawOrderTimeline* find_draw_order_timeline() const;
    const EventTimeline* find_event_timeline() const;
    std::optional<double> sample_bone_rotation(std::size_t bone_index, double time) const;
    const InheritKeyframe* sample_bone_inherit(std::size_t bone_index, double time) const;
    std::optional<VectorSample> sample_bone_translation(std::size_t bone_index, double time) const;
    std::optional<VectorSample> sample_bone_scale(std::size_t bone_index, double time) const;
    std::optional<VectorSample> sample_bone_shear(std::size_t bone_index, double time) const;
    const AttachmentKeyframe* sample_slot_attachment(std::size_t slot_index, double time) const;
    std::optional<SlotColor> sample_slot_color(std::size_t slot_index, double time) const;
    std::optional<std::vector<double>> sample_slot_deform(
        std::size_t slot_index,
        std::string_view attachment_name,
        double time) const;
    const DrawOrderKeyframe* sample_draw_order(double time) const;
    double duration() const;
};

struct AnimationMixDefinition {
    std::string from_animation;
    std::string to_animation;
    double duration{0.0};
    bool from_any{false};
};

class SkeletonData {
public:
    SkeletonData(
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
        std::vector<AnimationMixDefinition> mix_definitions);

    const SkeletonInfo& info() const;
    const std::vector<BoneData>& bones() const;
    const std::vector<IkConstraintData>& ik_constraints() const;
    const std::vector<PathConstraintData>& path_constraints() const;
    const std::vector<TransformConstraintData>& transform_constraints() const;
    const std::vector<PhysicsConstraintData>& physics_constraints() const;
    const std::vector<SlotData>& slots() const;
    const std::vector<EventDefinition>& events() const;
    const std::vector<AnimationData>& animations() const;
    const std::vector<SkinData>& skins() const;
    const std::vector<AnimationMixDefinition>& mix_definitions() const;
    const std::vector<std::size_t>& bone_evaluation_order() const;
    std::optional<std::size_t> default_skin_index() const;
    double default_mix_duration() const;

    std::optional<std::size_t> find_bone_index(std::string_view name) const;
    std::optional<std::size_t> find_slot_index(std::string_view name) const;
    std::optional<std::size_t> find_skin_index(std::string_view name) const;
    const AnimationData* find_animation(std::string_view name) const;
    const SkinData* find_skin(std::string_view name) const;
    const AttachmentData* find_attachment(std::size_t skin_index, std::size_t slot_index) const;
    const AttachmentData* find_attachment(std::string_view skin_name, std::size_t slot_index) const;
    const AttachmentData* find_attachment_source(
        std::size_t slot_index,
        std::string_view attachment_name,
        std::optional<std::size_t>* skin_index_out = nullptr) const;
    double mix_duration(std::string_view from_animation, std::string_view to_animation) const;

private:
    SkeletonInfo info_;
    std::vector<BoneData> bones_;
    std::vector<std::size_t> bone_evaluation_order_;
    std::vector<IkConstraintData> ik_constraints_;
    std::vector<PathConstraintData> path_constraints_;
    std::vector<TransformConstraintData> transform_constraints_;
    std::vector<PhysicsConstraintData> physics_constraints_;
    std::vector<SlotData> slots_;
    std::vector<EventDefinition> events_;
    std::vector<AnimationData> animations_;
    std::vector<SkinData> skins_;
    std::optional<std::size_t> default_skin_index_;
    double default_mix_duration_{0.0};
    std::vector<AnimationMixDefinition> mix_definitions_;
};

struct BonePose {
    BoneTransform local_pose;
    BoneInherit inherit{BoneInherit::Normal};
};

struct BoneWorldTransform {
    double a{1.0};
    double b{0.0};
    double c{0.0};
    double d{1.0};
    double world_x{0.0};
    double world_y{0.0};
};

struct SlotState {
    std::string attachment_name;
    std::optional<std::size_t> attachment_skin_index;
    SlotColor color{};
    std::optional<SlotColor> dark_color;
};

struct MeshWorldVertex {
    double x{0.0};
    double y{0.0};
};

struct MeshAttachmentPose {
    std::size_t slot_index{0};
    std::string attachment_name;
    std::string region_name;
    std::vector<MeshWorldVertex> vertices;
    std::vector<std::size_t> triangles;
    std::vector<double> uvs;
};

struct MeshDeformState {
    std::string attachment_name;
    std::vector<double> vertex_offsets;
};

struct PointAttachmentPose {
    std::size_t slot_index{0};
    std::string attachment_name;
    AttachmentVertex position{};
    double rotation{0.0};
};

struct BoundingBoxAttachmentPose {
    std::size_t slot_index{0};
    std::string attachment_name;
    std::vector<AttachmentVertex> polygon;
};

struct ClippingAttachmentPose {
    std::size_t slot_index{0};
    std::string attachment_name;
    std::optional<std::size_t> end_slot_index;
    std::string end_slot_name;
    std::vector<AttachmentVertex> polygon;
};

class Skeleton;

class SkeletonBounds {
public:
    void update(const Skeleton& skeleton, bool compute_aabb = true);

    bool contains_point(double x, double y);
    bool contains_point(
        double x,
        double y,
        const BoundingBoxAttachmentPose** bounding_box);
    bool intersects_segment(double x1, double y1, double x2, double y2);
    bool intersects_segment(
        double x1,
        double y1,
        double x2,
        double y2,
        const BoundingBoxAttachmentPose** bounding_box);

    const BoundingBoxAttachmentPose* get_bounding_box() const;
    const std::vector<BoundingBoxAttachmentPose>& bounding_boxes() const;
    const std::vector<AttachmentVertex>* get_polygon(std::size_t bounding_box_index) const;
    const std::vector<AttachmentVertex>* get_polygon(std::string_view attachment_name) const;

    bool has_aabb() const;
    double min_x() const;
    double min_y() const;
    double max_x() const;
    double max_y() const;

private:
    std::vector<BoundingBoxAttachmentPose> bounding_boxes_;
    std::optional<std::size_t> last_bounding_box_index_;
    bool has_aabb_{false};
    double min_x_{0.0};
    double min_y_{0.0};
    double max_x_{0.0};
    double max_y_{0.0};
};

using AnimationEventCallback = std::function<void(const AnimationEvent& event)>;
using SkeletonErrorCallback = std::function<void(std::string_view message)>;

std::optional<std::size_t> sample_sequence_frame(
    const AttachmentSequenceData& sequence,
    double time_seconds);

class Skeleton {
public:
    explicit Skeleton(std::shared_ptr<const SkeletonData> data);

    const std::shared_ptr<const SkeletonData>& data() const;
    void set_error_callback(SkeletonErrorCallback callback);
    const std::optional<std::string>& last_error() const;
    void clear_last_error();
    void set_scale(double scale_x, double scale_y);
    double scale_x() const;
    double scale_y() const;
    void prepare_animation_pose();
    void set_to_setup_pose();
    void reset_physics();
    void apply_animation(const AnimationData& animation, double time);
    void apply_animation(
        const AnimationData& animation,
        double previous_time,
        double time,
        const AnimationEventCallback& event_callback);
    void update_physics(double delta_seconds);
    void set_attachment_playback_time(double time_seconds);
    void advance_attachment_playback(double delta_seconds);
    double attachment_playback_time() const;
    void update_world_transforms(PhysicsMode physics = PhysicsMode::Pose);
    const std::vector<BonePose>& bone_poses() const;
    std::vector<BonePose>& bone_poses();
    const std::vector<BoneWorldTransform>& bone_world_transforms() const;
    const std::vector<SlotState>& slot_states() const;
    std::vector<SlotState>& slot_states();
    const std::vector<MeshDeformState>& mesh_deform_states() const;
    std::vector<MeshDeformState>& mesh_deform_states();
    const std::vector<std::size_t>& draw_order() const;
    std::vector<std::size_t>& draw_order();
    bool set_skin(std::string_view skin_name);
    bool set_skin_composition(const std::vector<std::string_view>& skin_names);
    bool is_bone_active(std::size_t bone_index) const;
    const AttachmentData* current_attachment(std::size_t slot_index) const;
    std::optional<std::size_t> current_sequence_frame(std::size_t slot_index) const;
    std::string_view current_region_name(std::size_t slot_index) const;
    const std::vector<double>* current_mesh_vertex_offsets(std::size_t slot_index) const;
    std::optional<PointAttachmentPose> evaluate_current_point_attachment(std::size_t slot_index) const;
    std::optional<BoundingBoxAttachmentPose> evaluate_current_bounding_box_attachment(
        std::size_t slot_index) const;
    std::optional<ClippingAttachmentPose> evaluate_current_clipping_attachment(
        std::size_t slot_index) const;
    std::optional<MeshAttachmentPose> evaluate_current_mesh_attachment(std::size_t slot_index) const;

private:
    friend class AnimationState;

    struct PhysicsBoneState {
        double ux{0.0};
        double uy{0.0};
        double cx{0.0};
        double cy{0.0};
        double tx{0.0};
        double ty{0.0};
        double x_offset{0.0};
        double x_velocity{0.0};
        double y_offset{0.0};
        double y_velocity{0.0};
        double rotate_offset{0.0};
        double rotate_velocity{0.0};
        double scale_offset{0.0};
        double scale_velocity{0.0};
    };

    struct PhysicsConstraintState {
        std::vector<PhysicsBoneState> bones;
        double remaining{0.0};
        bool reset{true};
    };

    void apply_setup_attachments();
    void apply_active_skin_attachments();
    bool apply_slot_attachment_keyframe(
        std::size_t slot_index,
        const std::optional<std::string>& attachment_name);
    bool apply_skin_indices(const std::vector<std::size_t>& skin_indices);
    void update_active_skin_scopes(const std::vector<std::size_t>& skin_indices);
    const AttachmentData* resolve_current_attachment(
        std::size_t slot_index,
        bool report_errors) const;
    void report_error(std::string message) const;
    void reset_physics_state();

    std::shared_ptr<const SkeletonData> data_;
    std::vector<BonePose> bone_poses_;
    std::vector<BoneWorldTransform> bone_world_transforms_;
    std::vector<SlotState> slot_states_;
    std::vector<MeshDeformState> mesh_deform_states_;
    std::vector<std::size_t> draw_order_;
    std::vector<std::size_t> active_skin_indices_;
    std::vector<bool> active_bones_;
    std::vector<bool> active_ik_constraints_;
    std::vector<bool> active_path_constraints_;
    std::vector<bool> active_transform_constraints_;
    std::vector<bool> active_physics_constraints_;
    std::vector<PhysicsConstraintState> physics_constraint_states_;
    double scale_x_{1.0};
    double scale_y_{1.0};
    double pending_physics_delta_seconds_{0.0};
    double attachment_playback_time_{0.0};
    mutable SkeletonErrorCallback error_callback_;
    mutable std::optional<std::string> last_error_;

    void reset_to_setup_pose_state(bool reset_slots_and_draw_order);
};

struct SkeletonDataResult {
    std::shared_ptr<const SkeletonData> skeleton_data;
    std::optional<json::LoadError> error;

    explicit operator bool() const {
        return skeleton_data != nullptr;
    }
};

struct BinaryAnimationOptimizationOptions {
    double rotation_error_tolerance_degrees{0.05};
    double translation_error_tolerance{0.25};
};

struct SkeletonBinaryInspection {
    std::uint64_t binary_version{0};
    bool has_optimized_animation_section{false};
    std::size_t animation_count{0};
    std::size_t rotate_channel_count{0};
    std::size_t translate_channel_count{0};
    std::size_t optimized_keyframe_count{0};
    bool keyframes_sorted_by_time_and_bone{false};
};

// `.mbin` stores the exact validated runtime document plus a versioned packed
// animation payload for quantized rotate/translate playback data.
std::string_view skeleton_binary_extension();
json::LoadResult load_skeleton_document(const std::filesystem::path& path);
std::optional<json::LoadError> write_skeleton_binary_document(
    const json::Document& document,
    const std::filesystem::path& path,
    const BinaryAnimationOptimizationOptions& options = {});
std::optional<json::LoadError> inspect_skeleton_binary(
    const std::filesystem::path& path,
    SkeletonBinaryInspection* inspection_out);

SkeletonDataResult load_skeleton_data(const json::Document& document);
SkeletonDataResult load_skeleton_data(const std::filesystem::path& path);

} // namespace marrow::runtime
