#pragma once

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/runtime/atlas.hpp"
#include "marrow/runtime/json.hpp"
#include "marrow/runtime/skeleton.hpp"

namespace marrow::editor {

enum class TransformTimelineChannel {
    Rotate,
    Translate,
    Scale,
    Shear,
};

enum class AnimationEditKind {
    Create,
    Rename,
    Delete,
    SetDuration,
    Unknown,
};

/**
 * @brief One ordered animation-catalog mutation applied to the referenced runtime skeleton.
 *
 * Create stores a complete animation JSON object so duplicates preserve timeline
 * families that the current editor does not understand. Rename moves the
 * effective animation object, Delete hides it from the authored runtime, and
 * SetDuration authors the optional runtime duration. Unknown operations remain
 * opaque editor data and are ignored by this version during materialization.
 */
struct AnimationEdit {
    AnimationEditKind kind{AnimationEditKind::Create};
    std::string name;
    std::string new_name;
    double duration{0.0};
    runtime::json::Value animation{runtime::json::Value::Object{}, {}};
    // Original edit object. Known fields are overlaid during serialization so
    // additive fields survive; Unknown edits serialize from this value exactly.
    runtime::json::Value preserved_source{runtime::json::Value::Object{}, {}};
};

enum class OnionSkinMode {
    Frame,
    Keyframe,
};

struct OnionSkinSettings {
    bool enabled{false};
    OnionSkinMode mode{OnionSkinMode::Frame};
    bool anchor_to_zero{false};
    int before_count{3};
    int after_count{3};
    int step{1};
};

struct DebugOverlaySettings {
    bool bones{true};
    bool ik_constraints{false};
    bool path_constraints{false};
    bool physics_constraints{false};
    bool mesh_wireframes{false};
    bool bounding_boxes{false};
};

struct TransformKeyframeEdit {
    double time{0.0};
    double angle{0.0};
    double x{0.0};
    double y{0.0};
    runtime::Interpolation interpolation{};
};

struct TransformTimelineEdit {
    std::string animation_name;
    std::string bone_name;
    TransformTimelineChannel channel{TransformTimelineChannel::Rotate};
    std::vector<TransformKeyframeEdit> keyframes;
};

/**
 * @brief Partial value update for one transform keyframe.
 *
 * Rotate timelines consume `angle`; translate, scale, and shear timelines
 * consume `x` and `y`. Omitted values preserve an existing key's value and
 * use the default-initialized value when a key is inserted.
 */
struct TransformKeyframePatch {
    // Absolute local rotation as displayed by the inspector/agent surface.
    // The project-domain upsert converts it to the runtime format's
    // setup-relative rotate-key angle.
    std::optional<double> angle;
    std::optional<double> x;
    std::optional<double> y;
};

/** Finds a sorted key within a symmetric time tolerance. */
template <typename Keyframe>
typename std::vector<Keyframe>::iterator find_keyframe_near_time(
    std::vector<Keyframe>& keyframes,
    double time,
    double epsilon = 1e-6) {
    auto iterator = std::lower_bound(
        keyframes.begin(),
        keyframes.end(),
        time,
        [](const Keyframe& keyframe, double key_time) {
            return keyframe.time < key_time;
        });
    if (iterator != keyframes.end() &&
        std::abs(iterator->time - time) <= epsilon) {
        return iterator;
    }
    if (iterator != keyframes.begin()) {
        auto previous = iterator;
        --previous;
        if (std::abs(previous->time - time) <= epsilon) {
            return previous;
        }
    }
    return keyframes.end();
}

struct DeformKeyframeEdit {
    double time{0.0};
    std::vector<double> vertex_offsets;
    runtime::Interpolation interpolation{};
};

struct MeshDeformTimelineEdit {
    std::string animation_name;
    std::string slot_name;
    std::string attachment_name;
    std::vector<DeformKeyframeEdit> keyframes;
};

struct MeshWeightInfluenceEdit {
    std::string bone_name;
    double x{0.0};
    double y{0.0};
    double weight{0.0};
};

struct MeshWeightVertexEdit {
    std::vector<MeshWeightInfluenceEdit> influences;
};

struct MeshWeightAttachmentEdit {
    std::string skin_name;
    std::string slot_name;
    std::string attachment_name;
    std::vector<MeshWeightVertexEdit> vertices;
};

struct DrawOrderKeyframeEdit {
    double time{0.0};
    std::vector<std::string> slot_names;
};

struct DrawOrderTimelineEdit {
    std::string animation_name;
    std::vector<DrawOrderKeyframeEdit> keyframes;
};

struct EventKeyframeEdit {
    double time{0.0};
    std::string event_name;
    std::optional<int> int_value;
    std::optional<double> float_value;
    std::optional<std::string> string_value;
    std::optional<std::string> audio_path;
    std::optional<double> volume;
    std::optional<double> balance;
};

struct EventTimelineEdit {
    std::string animation_name;
    std::vector<EventKeyframeEdit> keyframes;
};

struct SlotColorKeyframeEdit {
    double time{0.0};
    runtime::SlotColor color{};
    runtime::Interpolation interpolation{};
};

struct SlotColorTimelineEdit {
    std::string animation_name;
    std::string slot_name;
    std::vector<SlotColorKeyframeEdit> keyframes;
};

struct SlotAttachmentKeyframeEdit {
    double time{0.0};
    std::optional<std::string> attachment_name;
};

struct SlotAttachmentTimelineEdit {
    std::string animation_name;
    std::string slot_name;
    std::vector<SlotAttachmentKeyframeEdit> keyframes;
};

struct IkConstraintEdit {
    std::string name;
    std::vector<std::string> bone_names;
    std::string target_bone_name;
    double mix{1.0};
    bool bend_positive{true};
    double softness{0.0};
    bool compress{false};
    bool stretch{false};
};

struct PathConstraintEdit {
    std::string name;
    std::string slot_name;
    std::vector<std::string> bone_names;
    double position{0.0};
    double spacing{0.0};
    runtime::PathConstraintSpacingMode spacing_mode{
        runtime::PathConstraintSpacingMode::Length};
    double rotate_mix{1.0};
    double translate_mix{1.0};
};

struct TransformConstraintEdit {
    std::string name;
    std::string source_bone_name;
    std::vector<std::string> bone_names;
    double rotate_mix{0.0};
    double translate_mix{0.0};
    double scale_mix{0.0};
    double shear_mix{0.0};
    runtime::TransformConstraintOffsets offsets{};
};

struct PhysicsConstraintEdit {
    std::string name;
    std::vector<std::string> bone_names;
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
    runtime::AttachmentVertex gravity{};
    runtime::AttachmentVertex wind{};
    double mix{1.0};
};

struct AtlasPackSprite {
    std::string region_name;
    std::filesystem::path image_path;
    std::optional<double> origin_x;
    std::optional<double> origin_y;
};

struct AtlasPackDefinition {
    std::filesystem::path atlas_path;
    std::string atlas_name;
    std::string filter_min{"linear"};
    std::string filter_mag{"linear"};
    std::string wrap_x{"clamp_to_edge"};
    std::string wrap_y{"clamp_to_edge"};
    bool premultiplied_alpha{false};
    int padding{2};
    bool trim{true};
    int bleed{1};
    std::vector<AtlasPackSprite> sprites;
};

struct RuntimeAssetReferences {
    std::filesystem::path skeleton_path;
    std::vector<std::filesystem::path> atlas_paths;
};

/** @brief Optional editor-only viewport snapping stored at top-level `.marrow.snap`. */
struct ProjectSnapSettings {
    static constexpr double kDefaultWorldGridStep = 10.0;
    static constexpr double kDefaultLocalAngleStepDegrees = 15.0;
    static constexpr double kDefaultAbsoluteScaleStep = 0.1;

    bool world_grid_enabled{false};
    bool local_angle_enabled{false};
    bool absolute_scale_enabled{false};
    double world_grid_step{kDefaultWorldGridStep};
    double local_angle_step_degrees{kDefaultLocalAngleStepDegrees};
    double absolute_scale_step{kDefaultAbsoluteScaleStep};
    runtime::json::Value preserved_source{runtime::json::Value::Object{}, {}};
};

struct ViewportState {
    double pan_x{0.0};
    double pan_y{0.0};
    double zoom{1.0};
    OnionSkinSettings onion_skin{};
    DebugOverlaySettings debug_overlay{};
};

struct TimelineSettings {
    double frames_per_second{60.0};
};

enum class ParameterAuthoringType {
    Continuous,
    Discrete,
};

/**
 * @brief Editor-owned parameter definition before runtime index resolution.
 *
 * The source document remains ID based. Runtime loaders resolve parameter and
 * target indices only after project export, so those derived indices never
 * enter the `.marrow` authoring graph.
 */
struct ParameterAuthoringDefinition {
    std::string id;
    std::string name;
    double min_value{0.0};
    double max_value{1.0};
    double default_value{0.0};
    ParameterAuthoringType type{ParameterAuthoringType::Continuous};
    bool clamp{true};
    std::optional<double> ui_step;
    std::optional<std::string> units;
};

struct ParameterGroupAuthoringDefinition {
    std::string id;
    std::string name;
    std::vector<std::string> parameter_ids;
    bool collapsed{false};
    std::optional<std::string> color_tag;
    std::optional<std::string> exclusive_mode;
};

/**
 * @brief Lossless, ID-based project form of one runtime parameter shape.
 *
 * Runtime-only resolved indices inherited from the runtime definition remain
 * unset in project data. `preserved_source` retains additive fields unknown to this
 * editor version, including unknown fields on nested keyforms.
 */
struct ParameterShapeAuthoringDefinition : runtime::ParameterShapeDefinition {
    runtime::json::Value preserved_source{runtime::json::Value::Object{}, {}};
};

/** @brief Lossless project form of one warp or rotation deformer. */
struct ParameterDeformerAuthoringDefinition : runtime::ParameterDeformerDefinition {
    runtime::json::Value preserved_source{runtime::json::Value::Object{}, {}};
};

/** @brief Lossless project form of one skeleton-local ArtPath. */
struct ArtPathAuthoringDefinition : runtime::ArtPathDefinition {
    runtime::json::Value preserved_source{runtime::json::Value::Object{}, {}};
};

/** @brief Lossless project form of one expression preset. */
struct ExpressionAuthoringDefinition : runtime::ExpressionDefinition {
    runtime::json::Value preserved_source{runtime::json::Value::Object{}, {}};
};

/** @brief Lossless project form of one lip-sync target mapping. */
struct LipSyncMappingAuthoringDefinition : runtime::LipSyncMappingDefinition {
    runtime::json::Value preserved_source{runtime::json::Value::Object{}, {}};
};

/** @brief Typed `.marrow.parameter_model.lip_sync` section. */
struct LipSyncAuthoringDefinition {
    std::vector<LipSyncMappingAuthoringDefinition> mappings;
    runtime::json::Value preserved_source{runtime::json::Value::Object{}, {}};

    bool empty() const noexcept;
};

// These conversion helpers are shared by project loading, the UI-free
// authoring primitives, and the editor shell. Known fields are rebuilt from
// typed data while unknown additive fields are retained from `preserved_source`.
bool parse_parameter_shape_authoring_value(
    const runtime::json::Value& value,
    ParameterShapeAuthoringDefinition* definition_out,
    std::string* error_out = nullptr);
runtime::json::Value build_parameter_shape_authoring_value(
    const ParameterShapeAuthoringDefinition& definition);

bool parse_parameter_deformer_authoring_value(
    const runtime::json::Value& value,
    ParameterDeformerAuthoringDefinition* definition_out,
    std::string* error_out = nullptr);
runtime::json::Value build_parameter_deformer_authoring_value(
    const ParameterDeformerAuthoringDefinition& definition);

bool parse_art_path_authoring_value(
    const runtime::json::Value& value,
    ArtPathAuthoringDefinition* definition_out,
    std::string* error_out = nullptr);
runtime::json::Value build_art_path_authoring_value(
    const ArtPathAuthoringDefinition& definition);

bool parse_expression_authoring_value(
    const runtime::json::Value& value,
    ExpressionAuthoringDefinition* definition_out,
    std::string* error_out = nullptr);
runtime::json::Value build_expression_authoring_value(
    const ExpressionAuthoringDefinition& definition);

bool parse_lip_sync_authoring_value(
    const runtime::json::Value& value,
    LipSyncAuthoringDefinition* definition_out,
    std::string* error_out = nullptr);
runtime::json::Value build_lip_sync_authoring_value(
    const LipSyncAuthoringDefinition& definition);

/**
 * @brief Optional `.marrow.parameter_model` authoring source.
 *
 * Every milestone-owned family is typed. `source` values preserve unknown
 * additive fields at section, entry, and nested-entry levels when known fields
 * are rewritten.
 */
struct ParameterModel {
    std::vector<ParameterAuthoringDefinition> parameters;
    std::vector<ParameterGroupAuthoringDefinition> groups;
    std::vector<ParameterDeformerAuthoringDefinition> deformers;
    std::vector<ParameterShapeAuthoringDefinition> blend_shapes;
    std::vector<ArtPathAuthoringDefinition> art_paths;
    std::vector<ExpressionAuthoringDefinition> expressions;
    LipSyncAuthoringDefinition lip_sync;
    runtime::json::Value source{runtime::json::Value::Object{}, {}};

    bool empty() const noexcept;
    const ParameterAuthoringDefinition* find_parameter(std::string_view id) const;
    ParameterAuthoringDefinition* find_parameter(std::string_view id);
    const ParameterGroupAuthoringDefinition* find_group(std::string_view id) const;
    ParameterGroupAuthoringDefinition* find_group(std::string_view id);
    const ParameterShapeAuthoringDefinition* find_shape(std::string_view id) const;
    ParameterShapeAuthoringDefinition* find_shape(std::string_view id);
    const ParameterDeformerAuthoringDefinition* find_deformer(std::string_view id) const;
    ParameterDeformerAuthoringDefinition* find_deformer(std::string_view id);
    const ArtPathAuthoringDefinition* find_art_path(std::string_view id) const;
    ArtPathAuthoringDefinition* find_art_path(std::string_view id);
    const ExpressionAuthoringDefinition* find_expression(std::string_view id) const;
    ExpressionAuthoringDefinition* find_expression(std::string_view id);
    const LipSyncMappingAuthoringDefinition* find_lip_mapping(
        std::string_view parameter_id) const;
    LipSyncMappingAuthoringDefinition* find_lip_mapping(std::string_view parameter_id);
};

struct ProjectMetadata {
    std::string name;
    std::string active_animation;
    std::vector<std::string> preview_skins;
    std::filesystem::path export_directory{"exports"};
    std::string notes;
    ViewportState viewport{};
    TimelineSettings timeline{};
};

struct ProjectData {
    std::string marrow_version{"1.0"};
    RuntimeAssetReferences runtime_assets;
    ProjectMetadata editor_metadata;
    std::optional<ProjectSnapSettings> snap_settings;
    std::vector<AnimationEdit> animation_edits;
    std::vector<TransformTimelineEdit> transform_timeline_edits;
    std::vector<MeshDeformTimelineEdit> mesh_deform_timeline_edits;
    std::vector<MeshWeightAttachmentEdit> mesh_weight_attachment_edits;
    std::vector<DrawOrderTimelineEdit> draw_order_timeline_edits;
    std::vector<EventTimelineEdit> event_timeline_edits;
    std::vector<SlotColorTimelineEdit> slot_color_timeline_edits;
    std::vector<SlotAttachmentTimelineEdit> slot_attachment_timeline_edits;
    std::vector<IkConstraintEdit> ik_constraint_edits;
    std::vector<PathConstraintEdit> path_constraint_edits;
    std::vector<TransformConstraintEdit> transform_constraint_edits;
    std::vector<PhysicsConstraintEdit> physics_constraint_edits;
    std::optional<ParameterModel> parameter_model;
    std::vector<AtlasPackDefinition> atlas_pack_definitions;
    // Unknown top-level additive fields from the loaded `.marrow` document.
    // Known fields are overlaid during serialization; this value is never
    // exported into the runtime document.
    runtime::json::Value preserved_root{runtime::json::Value::Object{}, {}};
    std::filesystem::path source_path;

    /**
     * @brief Resolves a project-relative path against the project file location.
     * @param referenced_path Path stored in project data.
     * @return Absolute or normalized resolved path.
     */
    std::filesystem::path resolve_path(const std::filesystem::path& referenced_path) const;
    /// @brief Resolves the referenced runtime skeleton path.
    /// @return Resolved runtime skeleton path.
    std::filesystem::path resolved_skeleton_path() const;
    /// @brief Resolves every referenced runtime atlas path.
    /// @return Resolved runtime atlas paths.
    std::vector<std::filesystem::path> resolved_atlas_paths() const;
    /// @brief Resolves the default runtime skeleton export path.
    /// @return Resolved export path for the JSON runtime skeleton.
    std::filesystem::path resolved_export_skeleton_path() const;
    /// @brief Resolves the default runtime binary export path.
    /// @return Resolved export path for the binary runtime skeleton.
    std::filesystem::path resolved_export_binary_path() const;
    /**
     * @brief Finds a transform timeline edit by animation, bone, and channel.
     * @param animation_name Animation containing the edit.
     * @param bone_name Bone targeted by the edit.
     * @param channel Transform channel to match.
     * @return Matching transform edit, or `nullptr` when none exists.
     */
    const TransformTimelineEdit* find_transform_timeline_edit(
        std::string_view animation_name,
        std::string_view bone_name,
        TransformTimelineChannel channel) const;
    /**
     * @brief Finds a mutable transform timeline edit by animation, bone, and channel.
     * @param animation_name Animation containing the edit.
     * @param bone_name Bone targeted by the edit.
     * @param channel Transform channel to match.
     * @return Matching mutable transform edit, or `nullptr` when none exists.
     */
    TransformTimelineEdit* find_transform_timeline_edit(
        std::string_view animation_name,
        std::string_view bone_name,
        TransformTimelineChannel channel);
    /**
     * @brief Finds a mesh deform timeline edit by animation, slot, and attachment.
     * @param animation_name Animation containing the edit.
     * @param slot_name Slot targeted by the edit.
     * @param attachment_name Attachment targeted by the edit.
     * @return Matching deform edit, or `nullptr` when none exists.
     */
    const MeshDeformTimelineEdit* find_mesh_deform_timeline_edit(
        std::string_view animation_name,
        std::string_view slot_name,
        std::string_view attachment_name) const;
    /**
     * @brief Finds a mutable mesh deform timeline edit by animation, slot, and attachment.
     * @param animation_name Animation containing the edit.
     * @param slot_name Slot targeted by the edit.
     * @param attachment_name Attachment targeted by the edit.
     * @return Matching mutable deform edit, or `nullptr` when none exists.
     */
    MeshDeformTimelineEdit* find_mesh_deform_timeline_edit(
        std::string_view animation_name,
        std::string_view slot_name,
        std::string_view attachment_name);
    /**
     * @brief Finds mesh weight edits for one skin, slot, and attachment.
     * @param skin_name Skin containing the weight override.
     * @param slot_name Slot containing the attachment.
     * @param attachment_name Attachment targeted by the override.
     * @return Matching mesh-weight edit, or `nullptr` when none exists.
     */
    const MeshWeightAttachmentEdit* find_mesh_weight_attachment_edit(
        std::string_view skin_name,
        std::string_view slot_name,
        std::string_view attachment_name) const;
    /**
     * @brief Finds mutable mesh weight edits for one skin, slot, and attachment.
     * @param skin_name Skin containing the weight override.
     * @param slot_name Slot containing the attachment.
     * @param attachment_name Attachment targeted by the override.
     * @return Matching mutable mesh-weight edit, or `nullptr` when none exists.
     */
    MeshWeightAttachmentEdit* find_mesh_weight_attachment_edit(
        std::string_view skin_name,
        std::string_view slot_name,
        std::string_view attachment_name);
    /**
     * @brief Finds a draw-order edit for one animation.
     * @param animation_name Animation to search.
     * @return Matching draw-order edit, or `nullptr` when none exists.
     */
    const DrawOrderTimelineEdit* find_draw_order_timeline_edit(
        std::string_view animation_name) const;
    /**
     * @brief Finds a mutable draw-order edit for one animation.
     * @param animation_name Animation to search.
     * @return Matching mutable draw-order edit, or `nullptr` when none exists.
     */
    DrawOrderTimelineEdit* find_draw_order_timeline_edit(
        std::string_view animation_name);
    /**
     * @brief Finds an event timeline edit for one animation.
     * @param animation_name Animation to search.
     * @return Matching event edit, or `nullptr` when none exists.
     */
    const EventTimelineEdit* find_event_timeline_edit(
        std::string_view animation_name) const;
    /**
     * @brief Finds a mutable event timeline edit for one animation.
     * @param animation_name Animation to search.
     * @return Matching mutable event edit, or `nullptr` when none exists.
     */
    EventTimelineEdit* find_event_timeline_edit(
        std::string_view animation_name);
    const SlotColorTimelineEdit* find_slot_color_timeline_edit(
        std::string_view animation_name,
        std::string_view slot_name) const;
    SlotColorTimelineEdit* find_slot_color_timeline_edit(
        std::string_view animation_name,
        std::string_view slot_name);
    const SlotAttachmentTimelineEdit* find_slot_attachment_timeline_edit(
        std::string_view animation_name,
        std::string_view slot_name) const;
    SlotAttachmentTimelineEdit* find_slot_attachment_timeline_edit(
        std::string_view animation_name,
        std::string_view slot_name);
    /**
     * @brief Finds an IK constraint edit by name.
     * @param name Constraint name to search.
     * @return Matching IK edit, or `nullptr` when none exists.
     */
    const IkConstraintEdit* find_ik_constraint_edit(std::string_view name) const;
    /**
     * @brief Finds a mutable IK constraint edit by name.
     * @param name Constraint name to search.
     * @return Matching mutable IK edit, or `nullptr` when none exists.
     */
    IkConstraintEdit* find_ik_constraint_edit(std::string_view name);
    /**
     * @brief Finds a path constraint edit by name.
     * @param name Constraint name to search.
     * @return Matching path edit, or `nullptr` when none exists.
     */
    const PathConstraintEdit* find_path_constraint_edit(std::string_view name) const;
    /**
     * @brief Finds a mutable path constraint edit by name.
     * @param name Constraint name to search.
     * @return Matching mutable path edit, or `nullptr` when none exists.
     */
    PathConstraintEdit* find_path_constraint_edit(std::string_view name);
    /**
     * @brief Finds a transform constraint edit by name.
     * @param name Constraint name to search.
     * @return Matching transform edit, or `nullptr` when none exists.
     */
    const TransformConstraintEdit* find_transform_constraint_edit(std::string_view name) const;
    /**
     * @brief Finds a mutable transform constraint edit by name.
     * @param name Constraint name to search.
     * @return Matching mutable transform edit, or `nullptr` when none exists.
     */
    TransformConstraintEdit* find_transform_constraint_edit(std::string_view name);
    /**
     * @brief Finds a physics constraint edit by name.
     * @param name Constraint name to search.
     * @return Matching physics edit, or `nullptr` when none exists.
     */
    const PhysicsConstraintEdit* find_physics_constraint_edit(std::string_view name) const;
    /**
     * @brief Finds a mutable physics constraint edit by name.
     * @param name Constraint name to search.
     * @return Matching mutable physics edit, or `nullptr` when none exists.
     */
    PhysicsConstraintEdit* find_physics_constraint_edit(std::string_view name);
    /**
     * @brief Finds an atlas pack definition by resolved atlas path.
     * @param atlas_path Atlas path to search.
     * @return Matching atlas pack definition, or `nullptr` when none exists.
     */
    const AtlasPackDefinition* find_atlas_pack_definition(
        const std::filesystem::path& atlas_path) const;
    /**
     * @brief Finds a mutable atlas pack definition by resolved atlas path.
     * @param atlas_path Atlas path to search.
     * @return Matching mutable atlas pack definition, or `nullptr` when none exists.
     */
    AtlasPackDefinition* find_atlas_pack_definition(
        const std::filesystem::path& atlas_path);
};

TransformTimelineEdit* ensure_transform_timeline_edit(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name,
    std::string_view bone_name,
    TransformTimelineChannel channel);

MeshDeformTimelineEdit* ensure_mesh_deform_timeline_edit(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name,
    std::string_view slot_name,
    std::string_view attachment_name);

DrawOrderTimelineEdit* ensure_draw_order_timeline_edit(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name);

EventTimelineEdit* ensure_event_timeline_edit(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name);

SlotColorTimelineEdit* ensure_slot_color_timeline_edit(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name,
    std::string_view slot_name);

SlotAttachmentTimelineEdit* ensure_slot_attachment_timeline_edit(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name,
    std::string_view slot_name);

double setup_relative_rotation_key(
    const runtime::SkeletonData& effective_skeleton,
    std::string_view bone_name,
    double absolute_local_rotation);

/**
 * @brief Inserts or updates one project-owned transform keyframe.
 *
 * The timeline and key are created when absent. When the project has not yet
 * materialized that channel, all effective runtime keys are copied first so a
 * first edit cannot replace the imported track. Keys remain time-sorted and a
 * key within 1e-6 seconds of `time` is updated in place. Inputs are absolute
 * local values; rotate angles are converted to setup-relative runtime keys.
 * Newly inserted keys use linear interpolation.
 *
 * @return The inserted or updated keyframe.
 */
TransformKeyframeEdit& upsert_transform_keyframe(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name,
    std::string_view bone_name,
    TransformTimelineChannel channel,
    double time,
    const TransformKeyframePatch& patch);

struct ProjectLoadResult {
    std::shared_ptr<ProjectData> project;
    std::shared_ptr<const runtime::json::Document> base_skeleton_document;
    std::shared_ptr<const runtime::SkeletonData> skeleton_data;
    std::vector<std::shared_ptr<const runtime::AtlasData>> atlas_data;
    std::optional<runtime::json::LoadError> error;

    /// @brief Reports whether project load succeeded and resolved all runtime assets.
    /// @return `true` when project, base runtime document, skeleton, and atlases are present.
    explicit operator bool() const {
        return project != nullptr &&
            base_skeleton_document != nullptr &&
            skeleton_data != nullptr &&
            !atlas_data.empty();
    }
};

struct ProjectSaveError {
    std::filesystem::path path;
    std::string message;

    /// @brief Formats the save error as a human-readable message.
    /// @return A formatted error string containing the path and failure text.
    std::string format() const;
};

struct ProjectSaveResult {
    std::shared_ptr<ProjectData> project;
    std::optional<ProjectSaveError> error;

    /// @brief Reports whether project save succeeded.
    /// @return `true` when no save error is present; otherwise `false`.
    explicit operator bool() const {
        return !error.has_value();
    }
};

struct ProjectRuntimeResult {
    std::shared_ptr<const runtime::SkeletonData> skeleton_data;
    std::optional<runtime::json::LoadError> error;

    /// @brief Reports whether runtime build from project data succeeded.
    /// @return `true` when skeleton data is available; otherwise `false`.
    explicit operator bool() const {
        return skeleton_data != nullptr;
    }
};

struct ProjectExportError {
    std::filesystem::path path;
    std::string message;

    /// @brief Formats the export error as a human-readable message.
    /// @return A formatted error string containing the path and failure text.
    std::string format() const;
};

struct ProjectExportResult {
    std::filesystem::path path;
    std::vector<std::filesystem::path> atlas_paths;
    std::vector<std::filesystem::path> texture_paths;
    std::optional<std::filesystem::path> binary_path;
    std::optional<ProjectExportError> error;

    /// @brief Reports whether runtime export succeeded.
    /// @return `true` when no export error is present; otherwise `false`.
    explicit operator bool() const {
        return !error.has_value();
    }
};

struct ProjectExportOptions {
    std::filesystem::path skeleton_output_path;
    std::optional<std::filesystem::path> binary_output_path;
};

struct MinimalProjectOptions {
    std::filesystem::path project_path;
    std::filesystem::path skeleton_path;
    std::vector<std::filesystem::path> atlas_paths;
    std::string name;
    std::string active_animation{"idle"};
    std::vector<std::string> preview_skins{"default"};
    std::filesystem::path export_directory{"exports"};
    std::string notes;
};

/**
 * @brief Creates a minimal editor project from runtime asset references.
 * @param options Runtime asset paths and project metadata defaults.
 * @return Newly constructed project data.
 */
ProjectData create_minimal_project(const MinimalProjectOptions& options);
/**
 * @brief Loads an editor project from an already parsed document.
 * @param document Parsed `.marrow` document.
 * @return Loaded project plus resolved runtime dependencies or an error.
 */
ProjectLoadResult load_project(const runtime::json::Document& document);
/**
 * @brief Loads an editor project from disk.
 * @param path Path to the `.marrow` file.
 * @return Loaded project plus resolved runtime dependencies or an error.
 */
ProjectLoadResult load_project(const std::filesystem::path& path);
/**
 * @brief Builds runtime skeleton data by applying project edits onto a base runtime document.
 * @param project Project containing editor-side overrides.
 * @param base_skeleton_document Base runtime skeleton document referenced by the project.
 * @return Export-ready runtime skeleton data or an error.
 */
ProjectRuntimeResult build_project_runtime(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document);
/**
 * @brief Builds the effective runtime JSON document before typed runtime parsing.
 * @param project Project containing editor-side overrides.
 * @param base_skeleton_document Referenced runtime skeleton document.
 * @return A deep-copied document with animation, timeline, mesh, and constraint edits applied.
 */
runtime::json::Document build_project_runtime_document(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document);
/**
 * @brief Serializes a project into `.marrow` JSON text.
 * @param project Project to serialize.
 * @return Pretty-printed `.marrow` JSON text.
 */
std::string serialize_project(const ProjectData& project);
/**
 * @brief Saves a project to disk.
 * @param project Project to serialize and save.
 * @param path Destination `.marrow` file path.
 * @return Save result with optional error details.
 */
ProjectSaveResult save_project(const ProjectData& project, const std::filesystem::path& path);
/**
 * @brief Exports runtime assets from a project to `.mskl` and optional `.mbin`.
 * @param project Project containing source references and editor overrides.
 * @param base_skeleton_document Base runtime skeleton document referenced by the project.
 * @param options Output file paths for the exported runtime assets.
 * @return Export result with output paths or an error.
 */
ProjectExportResult export_runtime_assets(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document,
    const ProjectExportOptions& options = {});
/**
 * @brief Exports only the runtime skeleton portion of a project.
 * @param project Project containing source references and editor overrides.
 * @param base_skeleton_document Base runtime skeleton document referenced by the project.
 * @param output_path Destination path for the exported runtime skeleton.
 * @return Export result with output paths or an error.
 */
ProjectExportResult export_runtime_skeleton(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document,
    const std::filesystem::path& output_path = {});

} // namespace marrow::editor
