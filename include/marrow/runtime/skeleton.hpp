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
class Skeleton;

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

struct WorldTransformTimingBreakdown {
    double transform_seconds{0.0};
    double constraint_seconds{0.0};
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

    /**
     * @brief Finds the default attachment bound to a slot in this skin.
     * @param slot_index Slot index to search.
     * @return Matching attachment, or `nullptr` when none exists.
     */
    const AttachmentData* find_attachment(std::size_t slot_index) const;
    /**
     * @brief Finds a named attachment bound to a slot in this skin.
     * @param slot_index Slot index to search.
     * @param attachment_name Attachment name to search.
     * @return Matching attachment, or `nullptr` when none exists.
     */
    const AttachmentData* find_attachment(
        std::size_t slot_index,
        std::string_view attachment_name) const;
    /**
     * @brief Finds a named attachment anywhere in this skin.
     * @param attachment_name Attachment name to search.
     * @return Matching attachment, or `nullptr` when none exists.
     */
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

    /**
     * @brief Finds the rotate timeline targeting one bone.
     * @param bone_index Bone index to search.
     * @return Matching rotate timeline, or `nullptr` when none exists.
     */
    const BoneRotateTimeline* find_rotate_timeline(std::size_t bone_index) const;
    /**
     * @brief Finds the inherit timeline targeting one bone.
     * @param bone_index Bone index to search.
     * @return Matching inherit timeline, or `nullptr` when none exists.
     */
    const BoneInheritTimeline* find_inherit_timeline(std::size_t bone_index) const;
    /**
     * @brief Finds the translate timeline targeting one bone.
     * @param bone_index Bone index to search.
     * @return Matching translate timeline, or `nullptr` when none exists.
     */
    const BoneTranslateTimeline* find_translate_timeline(std::size_t bone_index) const;
    /**
     * @brief Finds the scale timeline targeting one bone.
     * @param bone_index Bone index to search.
     * @return Matching scale timeline, or `nullptr` when none exists.
     */
    const BoneScaleTimeline* find_scale_timeline(std::size_t bone_index) const;
    /**
     * @brief Finds the shear timeline targeting one bone.
     * @param bone_index Bone index to search.
     * @return Matching shear timeline, or `nullptr` when none exists.
     */
    const BoneShearTimeline* find_shear_timeline(std::size_t bone_index) const;
    /**
     * @brief Finds the attachment timeline targeting one slot.
     * @param slot_index Slot index to search.
     * @return Matching attachment timeline, or `nullptr` when none exists.
     */
    const SlotAttachmentTimeline* find_attachment_timeline(std::size_t slot_index) const;
    /**
     * @brief Finds the color timeline targeting one slot.
     * @param slot_index Slot index to search.
     * @return Matching color timeline, or `nullptr` when none exists.
     */
    const SlotColorTimeline* find_color_timeline(std::size_t slot_index) const;
    /**
     * @brief Finds the deform timeline targeting one slot attachment.
     * @param slot_index Slot index to search.
     * @param attachment_name Attachment name to search.
     * @return Matching deform timeline, or `nullptr` when none exists.
     */
    const MeshDeformTimeline* find_deform_timeline(
        std::size_t slot_index,
        std::string_view attachment_name) const;
    /// @brief Returns the draw-order timeline when the animation authors one.
    /// @return Draw-order timeline, or `nullptr` when none exists.
    const DrawOrderTimeline* find_draw_order_timeline() const;
    /// @brief Returns the event timeline when the animation authors one.
    /// @return Event timeline, or `nullptr` when none exists.
    const EventTimeline* find_event_timeline() const;
    /**
     * @brief Samples bone rotation from the animation at a given time.
     * @param bone_index Bone index to sample.
     * @param time Sample time in seconds.
     * @return Rotation in degrees, or `std::nullopt` when no timeline exists.
     */
    std::optional<double> sample_bone_rotation(std::size_t bone_index, double time) const;
    /**
     * @brief Samples bone inherit state from the animation at a given time.
     * @param bone_index Bone index to sample.
     * @param time Sample time in seconds.
     * @return Active inherit keyframe, or `nullptr` when no timeline exists.
     */
    const InheritKeyframe* sample_bone_inherit(std::size_t bone_index, double time) const;
    /**
     * @brief Samples bone translation from the animation at a given time.
     * @param bone_index Bone index to sample.
     * @param time Sample time in seconds.
     * @return Translation sample, or `std::nullopt` when no timeline exists.
     */
    std::optional<VectorSample> sample_bone_translation(std::size_t bone_index, double time) const;
    /**
     * @brief Samples bone scale from the animation at a given time.
     * @param bone_index Bone index to sample.
     * @param time Sample time in seconds.
     * @return Scale sample, or `std::nullopt` when no timeline exists.
     */
    std::optional<VectorSample> sample_bone_scale(std::size_t bone_index, double time) const;
    /**
     * @brief Samples bone shear from the animation at a given time.
     * @param bone_index Bone index to sample.
     * @param time Sample time in seconds.
     * @return Shear sample, or `std::nullopt` when no timeline exists.
     */
    std::optional<VectorSample> sample_bone_shear(std::size_t bone_index, double time) const;
    /**
     * @brief Samples slot attachment selection at a given time.
     * @param slot_index Slot index to sample.
     * @param time Sample time in seconds.
     * @return Active attachment keyframe, or `nullptr` when no timeline exists.
     */
    const AttachmentKeyframe* sample_slot_attachment(std::size_t slot_index, double time) const;
    /**
     * @brief Samples slot color at a given time.
     * @param slot_index Slot index to sample.
     * @param time Sample time in seconds.
     * @return Slot color, or `std::nullopt` when no timeline exists.
     */
    std::optional<SlotColor> sample_slot_color(std::size_t slot_index, double time) const;
    /**
     * @brief Samples slot deform offsets at a given time.
     * @param slot_index Slot index to sample.
     * @param attachment_name Attachment name to sample.
     * @param time Sample time in seconds.
     * @return Vertex offsets, or `std::nullopt` when no timeline exists.
     */
    std::optional<std::vector<double>> sample_slot_deform(
        std::size_t slot_index,
        std::string_view attachment_name,
        double time) const;
    /**
     * @brief Samples draw order at a given time.
     * @param time Sample time in seconds.
     * @return Active draw-order keyframe, or `nullptr` when no timeline exists.
     */
    const DrawOrderKeyframe* sample_draw_order(double time) const;
    /// @brief Returns the maximum authored duration of the animation.
    /// @return Animation duration in seconds.
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
    // Immutable setup data shared by Skeleton and AnimationState instances.
    // After load or construction completes, callers may read one SkeletonData
    // object from multiple threads concurrently without extra synchronization.
    /**
     * @brief Constructs immutable runtime skeleton data.
     * @param info Skeleton metadata.
     * @param bones Bone hierarchy and setup pose.
     * @param ik_constraints IK constraints authored on the skeleton.
     * @param path_constraints Path constraints authored on the skeleton.
     * @param transform_constraints Transform constraints authored on the skeleton.
     * @param physics_constraints Physics constraints authored on the skeleton.
     * @param slots Slot bindings and presentation defaults.
     * @param events Event definitions referenced by animations.
     * @param animations Authored animations and timelines.
     * @param skins Authored skins and attachments.
     * @param default_mix_duration Default crossfade duration for `AnimationState`.
     * @param mix_definitions Per-animation mix overrides.
     */
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

    /// @brief Returns skeleton metadata.
    /// @return Immutable skeleton info.
    const SkeletonInfo& info() const;
    /// @brief Returns authored bones.
    /// @return Immutable bone list.
    const std::vector<BoneData>& bones() const;
    /// @brief Returns authored IK constraints.
    /// @return Immutable IK constraint list.
    const std::vector<IkConstraintData>& ik_constraints() const;
    /// @brief Returns authored path constraints.
    /// @return Immutable path constraint list.
    const std::vector<PathConstraintData>& path_constraints() const;
    /// @brief Returns authored transform constraints.
    /// @return Immutable transform constraint list.
    const std::vector<TransformConstraintData>& transform_constraints() const;
    /// @brief Returns authored physics constraints.
    /// @return Immutable physics constraint list.
    const std::vector<PhysicsConstraintData>& physics_constraints() const;
    /// @brief Returns authored slots.
    /// @return Immutable slot list.
    const std::vector<SlotData>& slots() const;
    /// @brief Returns authored event definitions.
    /// @return Immutable event definition list.
    const std::vector<EventDefinition>& events() const;
    /// @brief Returns authored animations.
    /// @return Immutable animation list.
    const std::vector<AnimationData>& animations() const;
    /// @brief Returns authored skins.
    /// @return Immutable skin list.
    const std::vector<SkinData>& skins() const;
    /// @brief Returns per-animation mix overrides.
    /// @return Immutable mix-definition list.
    const std::vector<AnimationMixDefinition>& mix_definitions() const;
    /// @brief Returns the cached bone evaluation order for world-transform solving.
    /// @return Bone evaluation order indices.
    const std::vector<std::size_t>& bone_evaluation_order() const;
    /// @brief Returns the default skin index when one exists.
    /// @return Default skin index, or `std::nullopt` when no default skin exists.
    std::optional<std::size_t> default_skin_index() const;
    /// @brief Returns the default crossfade duration for animation mixing.
    /// @return Default mix duration in seconds.
    double default_mix_duration() const;

    /**
     * @brief Finds a bone index by name.
     * @param name Bone name to search.
     * @return Matching bone index, or `std::nullopt` when none exists.
     */
    std::optional<std::size_t> find_bone_index(std::string_view name) const;
    /**
     * @brief Finds a slot index by name.
     * @param name Slot name to search.
     * @return Matching slot index, or `std::nullopt` when none exists.
     */
    std::optional<std::size_t> find_slot_index(std::string_view name) const;
    /**
     * @brief Finds a skin index by name.
     * @param name Skin name to search.
     * @return Matching skin index, or `std::nullopt` when none exists.
     */
    std::optional<std::size_t> find_skin_index(std::string_view name) const;
    /**
     * @brief Finds an animation by name.
     * @param name Animation name to search.
     * @return Matching animation, or `nullptr` when none exists.
     */
    const AnimationData* find_animation(std::string_view name) const;
    /**
     * @brief Finds a skin by name.
     * @param name Skin name to search.
     * @return Matching skin, or `nullptr` when none exists.
     */
    const SkinData* find_skin(std::string_view name) const;
    /**
     * @brief Finds the default attachment for a skin and slot index.
     * @param skin_index Skin index to search.
     * @param slot_index Slot index to search.
     * @return Matching attachment, or `nullptr` when none exists.
     */
    const AttachmentData* find_attachment(std::size_t skin_index, std::size_t slot_index) const;
    /**
     * @brief Finds a named attachment for a skin and slot index.
     * @param skin_index Skin index to search.
     * @param slot_index Slot index to search.
     * @param attachment_name Attachment name to search.
     * @return Matching attachment, or `nullptr` when none exists.
     */
    const AttachmentData* find_attachment(
        std::size_t skin_index,
        std::size_t slot_index,
        std::string_view attachment_name) const;
    /**
     * @brief Finds the default attachment for a skin name and slot index.
     * @param skin_name Skin name to search.
     * @param slot_index Slot index to search.
     * @return Matching attachment, or `nullptr` when none exists.
     */
    const AttachmentData* find_attachment(std::string_view skin_name, std::size_t slot_index) const;
    /**
     * @brief Finds a named attachment for a skin name and slot index.
     * @param skin_name Skin name to search.
     * @param slot_index Slot index to search.
     * @param attachment_name Attachment name to search.
     * @return Matching attachment, or `nullptr` when none exists.
     */
    const AttachmentData* find_attachment(
        std::string_view skin_name,
        std::size_t slot_index,
        std::string_view attachment_name) const;
    /**
     * @brief Finds the first skin that contributes a named attachment for one slot.
     * @param slot_index Slot index to search.
     * @param attachment_name Attachment name to search.
     * @param skin_index_out Optional output receiving the skin index that provided the result.
     * @return Matching attachment, or `nullptr` when none exists.
     */
    const AttachmentData* find_attachment_source(
        std::size_t slot_index,
        std::string_view attachment_name,
        std::optional<std::size_t>* skin_index_out = nullptr) const;
    /**
     * @brief Resolves a mix duration for a transition between two animations.
     * @param from_animation Source animation name.
     * @param to_animation Destination animation name.
     * @return Transition duration in seconds.
     */
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
    /**
     * @brief Rebuilds bounding-box poses from the skeleton's current attachments.
     * @param skeleton Skeleton to evaluate.
     * @param compute_aabb Whether to also recompute the aggregate AABB.
     */
    void update(const Skeleton& skeleton, bool compute_aabb = true);

    /**
     * @brief Tests whether a point lies inside any active bounding box.
     * @param x Point x coordinate.
     * @param y Point y coordinate.
     * @return `true` when the point is inside an active bounding box.
     */
    bool contains_point(double x, double y);
    /**
     * @brief Tests whether a point lies inside any active bounding box and reports the match.
     * @param x Point x coordinate.
     * @param y Point y coordinate.
     * @param bounding_box Receives the matching bounding box when one is found.
     * @return `true` when the point is inside an active bounding box.
     */
    bool contains_point(
        double x,
        double y,
        const BoundingBoxAttachmentPose** bounding_box);
    /**
     * @brief Tests whether a segment intersects any active bounding box.
     * @param x1 First segment endpoint x coordinate.
     * @param y1 First segment endpoint y coordinate.
     * @param x2 Second segment endpoint x coordinate.
     * @param y2 Second segment endpoint y coordinate.
     * @return `true` when the segment intersects an active bounding box.
     */
    bool intersects_segment(double x1, double y1, double x2, double y2);
    /**
     * @brief Tests whether a segment intersects any active bounding box and reports the match.
     * @param x1 First segment endpoint x coordinate.
     * @param y1 First segment endpoint y coordinate.
     * @param x2 Second segment endpoint x coordinate.
     * @param y2 Second segment endpoint y coordinate.
     * @param bounding_box Receives the matching bounding box when one is found.
     * @return `true` when the segment intersects an active bounding box.
     */
    bool intersects_segment(
        double x1,
        double y1,
        double x2,
        double y2,
        const BoundingBoxAttachmentPose** bounding_box);

    /// @brief Returns the most recent matching bounding box, if any.
    /// @return Pointer to the last matched bounding box, or `nullptr`.
    const BoundingBoxAttachmentPose* get_bounding_box() const;
    /// @brief Returns every active bounding box pose.
    /// @return Active bounding box list.
    const std::vector<BoundingBoxAttachmentPose>& bounding_boxes() const;
    /**
     * @brief Returns one bounding-box polygon by index.
     * @param bounding_box_index Bounding-box index to inspect.
     * @return Pointer to the polygon vertices, or `nullptr` when out of range.
     */
    const std::vector<AttachmentVertex>* get_polygon(std::size_t bounding_box_index) const;
    /**
     * @brief Returns one bounding-box polygon by attachment name.
     * @param attachment_name Attachment name to search.
     * @return Pointer to the polygon vertices, or `nullptr` when no match exists.
     */
    const std::vector<AttachmentVertex>* get_polygon(std::string_view attachment_name) const;

    /// @brief Reports whether an aggregate AABB has been computed.
    /// @return `true` when the aggregate AABB is valid.
    bool has_aabb() const;
    /// @brief Returns the aggregate AABB minimum x value.
    /// @return Minimum x coordinate.
    double min_x() const;
    /// @brief Returns the aggregate AABB minimum y value.
    /// @return Minimum y coordinate.
    double min_y() const;
    /// @brief Returns the aggregate AABB maximum x value.
    /// @return Maximum x coordinate.
    double max_x() const;
    /// @brief Returns the aggregate AABB maximum y value.
    /// @return Maximum y coordinate.
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

/**
 * @brief Samples the current sequence frame for a sequence attachment.
 * @param sequence Sequence attachment metadata to sample.
 * @param time_seconds Playback time in seconds.
 * @return Active frame index, or `std::nullopt` when the sequence has no frames.
 */
std::optional<std::size_t> sample_sequence_frame(
    const AttachmentSequenceData& sequence,
    double time_seconds);

/**
 * @brief Advances an animation state and applies it to a matching skeleton instance.
 * @param skeleton Mutable skeleton instance to update.
 * @param animation_state Playback state created from the same `SkeletonData`.
 * @param delta_seconds Frame delta in seconds.
 */
void update_instance(
    Skeleton& skeleton,
    AnimationState& animation_state,
    double delta_seconds);

class Skeleton {
public:
    // Mutable per-instance pose state. Skeleton is not internally
    // synchronized; use each instance from one thread at a time. Distinct
    // Skeleton instances that share the same immutable SkeletonData may update
    // concurrently from different threads.
    /**
     * @brief Creates mutable pose state for one shared `SkeletonData`.
     * @param data Shared immutable skeleton data used by this instance.
     */
    explicit Skeleton(std::shared_ptr<const SkeletonData> data);

    /// @brief Returns the immutable skeleton data used by this instance.
    /// @return Shared skeleton-data handle.
    const std::shared_ptr<const SkeletonData>& data() const;
    /**
     * @brief Installs an error callback for recoverable skeleton errors.
     * @param callback Callback invoked when runtime errors are reported.
     */
    void set_error_callback(SkeletonErrorCallback callback);
    /// @brief Returns the most recent runtime error, if any.
    /// @return Optional last-error string.
    const std::optional<std::string>& last_error() const;
    /// @brief Clears the stored last-error message.
    void clear_last_error();
    /**
     * @brief Sets global x/y scaling applied to the skeleton.
     * @param scale_x Scale applied on the x axis.
     * @param scale_y Scale applied on the y axis.
     */
    void set_scale(double scale_x, double scale_y);
    /// @brief Returns the current x scale applied to the skeleton.
    /// @return Skeleton x scale.
    double scale_x() const;
    /// @brief Returns the current y scale applied to the skeleton.
    /// @return Skeleton y scale.
    double scale_y() const;
    /**
     * @brief Sets whether animation and transform updates should run for this instance.
     * @param visible Visibility flag used by update throttling and rendering.
     */
    void set_visible(bool visible);
    /// @brief Reports whether the skeleton is visible.
    /// @return `true` when the skeleton participates in updates and rendering.
    bool visible() const;
    /**
     * @brief Sets the frame interval used for update throttling.
     * @param interval Number of frames between full updates. `1` disables throttling.
     */
    void set_update_interval(std::size_t interval);
    /// @brief Returns the current update-throttling interval.
    /// @return Number of frames between full updates.
    std::size_t update_interval() const;
    /// @brief Resets mutable pose state in preparation for animation application.
    void prepare_animation_pose();
    /// @brief Restores bones, slots, and draw order to setup pose.
    void set_to_setup_pose();
    /// @brief Resets accumulated physics state to setup pose.
    void reset_physics();
    /**
     * @brief Applies one animation at a specific time without event callbacks.
     * @param animation Animation to apply.
     * @param time Sample time in seconds.
     */
    void apply_animation(const AnimationData& animation, double time);
    /**
     * @brief Applies one animation over a time range and dispatches event callbacks.
     * @param animation Animation to apply.
     * @param previous_time Previous sample time in seconds.
     * @param time Current sample time in seconds.
     * @param event_callback Callback invoked for event keys crossed in the range.
     */
    void apply_animation(
        const AnimationData& animation,
        double previous_time,
        double time,
        const AnimationEventCallback& event_callback);
    /**
     * @brief Advances authored physics constraints for this instance.
     * @param delta_seconds Simulation step in seconds.
     */
    void update_physics(double delta_seconds);
    /**
     * @brief Sets global playback time for sequence attachments.
     * @param time_seconds Sequence playback time in seconds.
     */
    void set_attachment_playback_time(double time_seconds);
    /**
     * @brief Advances sequence-attachment playback time.
     * @param delta_seconds Time step in seconds.
     */
    void advance_attachment_playback(double delta_seconds);
    /// @brief Returns the current sequence-attachment playback time.
    /// @return Sequence playback time in seconds.
    double attachment_playback_time() const;
    /**
     * @brief Recomputes world transforms and optional constraint timing totals.
     * @param physics Physics evaluation mode to use during the solve.
     * @param timing_breakdown Optional output receiving transform and constraint timings.
     */
    void update_world_transforms(
        PhysicsMode physics = PhysicsMode::Pose,
        WorldTransformTimingBreakdown* timing_breakdown = nullptr);
    /// @brief Returns mutable-local bone pose state.
    /// @return Immutable bone-pose list.
    const std::vector<BonePose>& bone_poses() const;
    /// @brief Returns mutable-local bone pose state for in-place editing.
    /// @return Mutable bone-pose list.
    std::vector<BonePose>& bone_poses();
    /// @brief Returns computed world transforms.
    /// @return Immutable world-transform list.
    const std::vector<BoneWorldTransform>& bone_world_transforms() const;
    /// @brief Returns current slot state.
    /// @return Immutable slot-state list.
    const std::vector<SlotState>& slot_states() const;
    /// @brief Returns current slot state for in-place editing.
    /// @return Mutable slot-state list.
    std::vector<SlotState>& slot_states();
    /// @brief Returns current mesh deform state.
    /// @return Immutable mesh-deform list.
    const std::vector<MeshDeformState>& mesh_deform_states() const;
    /// @brief Returns current mesh deform state for in-place editing.
    /// @return Mutable mesh-deform list.
    std::vector<MeshDeformState>& mesh_deform_states();
    /// @brief Returns current draw order.
    /// @return Immutable draw-order list.
    const std::vector<std::size_t>& draw_order() const;
    /// @brief Returns current draw order for in-place editing.
    /// @return Mutable draw-order list.
    std::vector<std::size_t>& draw_order();
    /**
     * @brief Activates one skin by name.
     * @param skin_name Skin name to activate.
     * @return `true` when the skin exists and activation succeeded.
     */
    bool set_skin(std::string_view skin_name);
    /**
     * @brief Activates a composed set of skins by name.
     * @param skin_names Skin names to compose in order.
     * @return `true` when every skin exists and composition succeeded.
     */
    bool set_skin_composition(const std::vector<std::string_view>& skin_names);
    /**
     * @brief Reports whether a bone is active under the current skin scopes.
     * @param bone_index Bone index to inspect.
     * @return `true` when the bone is currently active.
     */
    bool is_bone_active(std::size_t bone_index) const;
    /**
     * @brief Returns the current attachment resolved for one slot.
     * @param slot_index Slot index to inspect.
     * @return Current attachment, or `nullptr` when none is active.
     */
    const AttachmentData* current_attachment(std::size_t slot_index) const;
    /**
     * @brief Returns the current sequence frame for one slot, when applicable.
     * @param slot_index Slot index to inspect.
     * @return Current frame index, or `std::nullopt` when the slot is not a sequence attachment.
     */
    std::optional<std::size_t> current_sequence_frame(std::size_t slot_index) const;
    /**
     * @brief Returns the current region name resolved for one slot.
     * @param slot_index Slot index to inspect.
     * @return Region name, or an empty view when no region is active.
     */
    std::string_view current_region_name(std::size_t slot_index) const;
    /**
     * @brief Returns current mesh vertex offsets for one slot.
     * @param slot_index Slot index to inspect.
     * @return Vertex offset array, or `nullptr` when no deform data is active.
     */
    const std::vector<double>* current_mesh_vertex_offsets(std::size_t slot_index) const;
    /**
     * @brief Evaluates the current point attachment pose for one slot.
     * @param slot_index Slot index to inspect.
     * @return World-space point attachment pose, or `std::nullopt` when none is active.
     */
    std::optional<PointAttachmentPose> evaluate_current_point_attachment(std::size_t slot_index) const;
    /**
     * @brief Evaluates the current bounding-box attachment pose for one slot.
     * @param slot_index Slot index to inspect.
     * @return World-space bounding box pose, or `std::nullopt` when none is active.
     */
    std::optional<BoundingBoxAttachmentPose> evaluate_current_bounding_box_attachment(
        std::size_t slot_index) const;
    /**
     * @brief Evaluates the current clipping attachment pose for one slot.
     * @param slot_index Slot index to inspect.
     * @return World-space clipping pose, or `std::nullopt` when none is active.
     */
    std::optional<ClippingAttachmentPose> evaluate_current_clipping_attachment(
        std::size_t slot_index) const;
    /**
     * @brief Evaluates the current mesh attachment pose for one slot.
     * @param slot_index Slot index to inspect.
     * @return World-space mesh pose, or `std::nullopt` when none is active.
     */
    std::optional<MeshAttachmentPose> evaluate_current_mesh_attachment(std::size_t slot_index) const;

private:
    friend class AnimationState;
    friend void update_instance(
        Skeleton& skeleton,
        AnimationState& animation_state,
        double delta_seconds);

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

    struct DisplayStateSnapshot {
        std::vector<BonePose> bone_poses;
        std::vector<BoneWorldTransform> bone_world_transforms;
        std::vector<SlotState> slot_states;
        std::vector<MeshDeformState> mesh_deform_states;
        std::vector<std::size_t> draw_order;
        double attachment_playback_time{0.0};
    };

    struct UpdateThrottleState {
        std::size_t frames_since_update{0};
        double pending_delta_seconds{0.0};
        double last_full_update_delta_seconds{0.0};
        bool has_full_update_history{false};
        bool has_prediction{false};
        bool dirty{true};
        DisplayStateSnapshot source_snapshot;
        DisplayStateSnapshot target_snapshot;
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
    void reset_update_throttle_state();
    void capture_display_state(DisplayStateSnapshot* snapshot) const;
    void apply_display_state(const DisplayStateSnapshot& snapshot);
    void apply_interpolated_display_state(double alpha);
    void rebuild_predicted_display_state(
        const AnimationState& animation_state,
        double predicted_delta_seconds);

    std::shared_ptr<const SkeletonData> data_;
    std::vector<BonePose> bone_poses_;
    std::vector<float> bone_local_x_;
    std::vector<float> bone_local_y_;
    std::vector<float> bone_local_a_;
    std::vector<float> bone_local_b_;
    std::vector<float> bone_local_c_;
    std::vector<float> bone_local_d_;
    std::vector<float> bone_world_a_;
    std::vector<float> bone_world_b_;
    std::vector<float> bone_world_c_;
    std::vector<float> bone_world_d_;
    std::vector<float> bone_world_x_;
    std::vector<float> bone_world_y_;
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
    bool visible_{true};
    std::size_t update_interval_{1};
    double pending_physics_delta_seconds_{0.0};
    double attachment_playback_time_{0.0};
    UpdateThrottleState update_throttle_state_{};
    mutable SkeletonErrorCallback error_callback_;
    mutable std::optional<std::string> last_error_;

    void reset_to_setup_pose_state(bool reset_slots_and_draw_order);
};

struct SkeletonDataResult {
    std::shared_ptr<const SkeletonData> skeleton_data;
    std::optional<json::LoadError> error;

    /// @brief Reports whether skeleton loading succeeded.
    /// @return `true` when skeleton data is available; otherwise `false`.
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
/// @brief Returns the binary runtime skeleton file extension.
/// @return The `.mbin` file extension.
std::string_view skeleton_binary_extension();
/**
 * @brief Loads a runtime skeleton document from JSON or binary storage.
 * @param path Path to a `.mskl` or `.mbin` file.
 * @return Parsed runtime document or a load error.
 */
json::LoadResult load_skeleton_document(const std::filesystem::path& path);
/**
 * @brief Writes a runtime skeleton document to `.mbin`.
 * @param document Runtime JSON document to encode.
 * @param path Destination `.mbin` path.
 * @param options Quantization tolerances for packed animation channels.
 * @return Load-style error payload when encoding fails.
 */
std::optional<json::LoadError> write_skeleton_binary_document(
    const json::Document& document,
    const std::filesystem::path& path,
    const BinaryAnimationOptimizationOptions& options = {});
/**
 * @brief Inspects a `.mbin` file without constructing `SkeletonData`.
 * @param path Path to the `.mbin` file.
 * @param inspection_out Receives binary inspection details on success.
 * @return Load-style error payload when inspection fails.
 */
std::optional<json::LoadError> inspect_skeleton_binary(
    const std::filesystem::path& path,
    SkeletonBinaryInspection* inspection_out);

/**
 * @brief Loads immutable skeleton data from a parsed runtime document.
 * @param document Parsed runtime skeleton document.
 * @return Immutable skeleton data or a load error.
 */
SkeletonDataResult load_skeleton_data(const json::Document& document);
/**
 * @brief Loads immutable skeleton data from a `.mskl` or `.mbin` file.
 * @param path Path to the runtime skeleton asset.
 * @return Immutable skeleton data or a load error.
 */
SkeletonDataResult load_skeleton_data(const std::filesystem::path& path);

} // namespace marrow::runtime
