#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "marrow/editor/selection.hpp"
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

/** @brief One editable scalar channel of a persisted timeline key. */
enum class TimelineScalarComponent : std::uint8_t {
    Angle,
    X,
    Y,
    Red,
    Green,
    Blue,
    Alpha,
};

/**
 * @brief Authored intent for one key's outgoing easing.
 *
 * `Manual` is the pre-MAR-171 behaviour and the default for every keyframe and
 * every project that omits the field: the stored `interpolation` is exactly
 * what the animator put there. `Auto` records that the stored easing is a
 * derived value the editor recomputes from the neighbouring keys of the
 * driver's series. The stored easing remains authoritative for every reader,
 * including both file formats and the runtime; the mode records only why the
 * numbers are what they are, and is never exported.
 */
enum class TimelineCurveMode : std::uint8_t { Manual, Auto };

struct TransformKeyframeEdit {
    double time{0.0};
    double angle{0.0};
    double x{0.0};
    double y{0.0};
    runtime::Interpolation interpolation{};
    TimelineCurveMode curve_mode{TimelineCurveMode::Manual};
    TimelineScalarComponent curve_driver{TimelineScalarComponent::Angle};
};

struct TransformTimelineEdit {
    std::string animation_name;
    std::string bone_name;
    TransformTimelineChannel channel{TransformTimelineChannel::Rotate};
    std::vector<TransformKeyframeEdit> keyframes;
    /**
     * @brief Loop-boundary synchronization intent for this timeline.
     *
     * When true, the editor maintains one managed key at the animation's
     * explicit duration whose value and easing mirror this timeline's key at
     * time zero, so a looping clip wraps without a pop. Absent from every
     * project that has not opted in, never exported, and default-off for every
     * newly created timeline.
     */
    bool loop_sync{false};
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
    /**
     * @brief Loop-boundary synchronization intent for this timeline.
     *
     * When true, the editor maintains one managed key at the animation's
     * explicit duration whose value and easing mirror this timeline's key at
     * time zero, so a looping clip wraps without a pop. Absent from every
     * project that has not opted in, never exported, and default-off for every
     * newly created timeline.
     */
    bool loop_sync{false};
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
    TimelineCurveMode curve_mode{TimelineCurveMode::Manual};
    TimelineScalarComponent curve_driver{TimelineScalarComponent::Red};
};

struct SlotColorTimelineEdit {
    std::string animation_name;
    std::string slot_name;
    std::vector<SlotColorKeyframeEdit> keyframes;
    /**
     * @brief Loop-boundary synchronization intent for this timeline.
     *
     * When true, the editor maintains one managed key at the animation's
     * explicit duration whose value and easing mirror this timeline's key at
     * time zero, so a looping clip wraps without a pop. Absent from every
     * project that has not opted in, never exported, and default-off for every
     * newly created timeline.
     */
    bool loop_sync{false};
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

/**
 * @brief One stepped bone-inherit key in a project overlay.
 *
 * Time is `double` here and `AnimationScalar` (float) in the runtime, the same
 * narrowing every sibling family already carries.
 */
struct InheritKeyframeEdit {
    double time{0.0};
    runtime::BoneInherit inherit{runtime::BoneInherit::Normal};
};

/**
 * @brief A project-owned bone inherit timeline, keyed by (animation, bone).
 *
 * Carries no `loop_sync`: a stepped lane has no boundary easing to mirror, and
 * only transform, deform and slot color opt into MAR-172's loop sync. Carries
 * no `curve_mode`/`curve_driver` either -- the discrete families never have.
 */
struct BoneInheritTimelineEdit {
    std::string animation_name;
    std::string bone_name;
    std::vector<InheritKeyframeEdit> keyframes;
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

/** @brief Which lifecycle transition an ordered constraint operation records. */
enum class ConstraintLifecycleKind {
    Rename,
    Delete,
};

/**
 * @brief One ordered rename or delete applied to a constraint family.
 *
 * A constraint's identity is `(family, name)`, never an index: a delete
 * renumbers every element after it, and the runtime enforces name uniqueness
 * only within a family, so an IK and a physics constraint may share a name.
 *
 * Records are applied front to back over the base skeleton document, and
 * strictly before any `*_constraint_edits` upsert is merged, so a rename can
 * never race an upsert for a name. `new_name` is meaningful only for `Rename`
 * and must be empty for `Delete`.
 */
struct ConstraintLifecycleOperation {
    ConstraintLifecycleKind kind{ConstraintLifecycleKind::Rename};
    ConstraintKind family{ConstraintKind::Ik};
    std::string name;      ///< `from` for a rename, the target for a delete.
    std::string new_name;  ///< `to` for a rename; empty for a delete.
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
    bool magnetic_vertex_enabled{false};
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

/// @brief One PSD layer's stable identity and the runtime targets it produced.
struct PsdLayerProvenance {
    std::vector<std::string> group_path;  ///< Exact ancestor folder names, outermost first.
    std::string layer_name;               ///< Exact PSD layer name, before slot de-duplication.
    std::string slot_name;
    std::string attachment_name;
    std::string bone_name;
    /**
     * @brief The layer image's file NAME under `layers_directory`. Never a path.
     *
     * `write_imported_layers` computes every extracted image as a direct child of
     * the layer directory, so a stored path could only ever restate that directory
     * once per layer. Keeping this a bare name holds the story's rebase surface to
     * TWO fields, which is small enough to enumerate by hand -- and
     * `rebase_project_paths` is a fixed-length call list that the compiler does not
     * police. A load-time rejection enforces the invariant rather than trusting it.
     */
    std::string image_file;
};

/// @brief Where a project's art came from, and what each layer became.
struct PsdImportProvenance {
    std::filesystem::path source_path;       ///< Project-relative `.psd`.
    std::filesystem::path layers_directory;  ///< Project-relative extracted-layer directory.
    std::vector<PsdLayerProvenance> layers;
};

/// @brief Optional per-format import provenance. Absent in every pre-MAR-188 project.
struct ProjectImportSources {
    std::optional<PsdImportProvenance> psd;
};

struct ProjectMetadata {
    std::string name;
    std::string active_animation;
    std::vector<std::string> preview_skins;
    std::filesystem::path export_directory{"exports"};
    std::string notes;
    ViewportState viewport{};
    TimelineSettings timeline{};
    /**
     * @brief MAR-188. `$.editor.import_sources`, absent in every older project.
     *
     * `ProjectMetadata` is the correct home: it is what `$.editor` deserialises
     * into, and `export_directory` -- the other `$.editor` path family -- already
     * lives here. Note the key already round-tripped verbatim through
     * `preserved_root` BEFORE this field existed, so a test asserting on
     * `serialize_project()` text rather than on this struct passes on unmodified
     * code. `build_project_value` must overwrite the preserved copy, and erase it
     * when this is disengaged, or an in-memory edit is silently discarded.
     */
    std::optional<ProjectImportSources> import_sources;
};

struct ProjectData {
    std::string marrow_version{"1.0"};
    RuntimeAssetReferences runtime_assets;
    ProjectMetadata editor_metadata;
    std::optional<ProjectSnapSettings> snap_settings;
    std::vector<AnimationEdit> animation_edits;
    std::vector<TransformTimelineEdit> transform_timeline_edits;
    // Placed beside the transform edits because both are bone tracks, mirroring
    // the runtime's own ordering of an animation's per-bone timelines.
    std::vector<BoneInheritTimelineEdit> bone_inherit_timeline_edits;
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
    // Ordered rename/delete records, applied over the base skeleton before the
    // four upsert vectors above are merged onto it.
    std::vector<ConstraintLifecycleOperation> constraint_lifecycle_operations;
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
     * @brief Finds a bone inherit timeline edit by animation and bone.
     * @param animation_name Animation containing the edit.
     * @param bone_name Bone targeted by the edit.
     * @return Matching inherit edit, or `nullptr` when none exists.
     */
    const BoneInheritTimelineEdit* find_bone_inherit_timeline_edit(
        std::string_view animation_name,
        std::string_view bone_name) const;
    /**
     * @brief Finds a mutable bone inherit timeline edit by animation and bone.
     * @param animation_name Animation containing the edit.
     * @param bone_name Bone targeted by the edit.
     * @return Matching mutable inherit edit, or `nullptr` when none exists.
     */
    BoneInheritTimelineEdit* find_bone_inherit_timeline_edit(
        std::string_view animation_name,
        std::string_view bone_name);
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

/**
 * @brief Maps a `.marrow` inherit token onto the runtime mode.
 * @param key One of `normal`, `onlyTranslation`, `noRotationOrReflection`,
 *        `noScale`, `noScaleOrReflection`.
 * @return The matching mode, or `std::nullopt` for an unknown token.
 *
 * Declared at namespace scope, unlike `transform_channel_json_key`, because the
 * merge primitive in `authoring.cpp` validates a caller-supplied token against
 * the SAME table the parser and both serializers use. A second, one-way copy
 * living in the authoring layer is exactly the drift this avoids.
 */
std::optional<runtime::BoneInherit> inherit_mode_from_key(std::string_view key);

/**
 * @brief Maps a runtime inherit mode onto its `.marrow` / `.mskl` token.
 * @param inherit Mode to encode.
 * @return The token, identical to the runtime parser's vocabulary.
 */
std::string_view inherit_mode_json_key(runtime::BoneInherit inherit);

TransformTimelineEdit* ensure_transform_timeline_edit(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name,
    std::string_view bone_name,
    TransformTimelineChannel channel);

/**
 * @brief Returns the project's inherit edit for one bone, materializing it once.
 *
 * On first touch the imported track's keys are copied into the project, so a
 * first edit extends the base timeline rather than replacing it. A bone with no
 * base track yields an edit with zero keyframes -- the legitimate transient
 * state of a project-only timeline, which both serializers skip.
 *
 * @param project Project receiving the edit.
 * @param effective_skeleton Skeleton the project currently materializes to.
 * @param animation_name Animation to edit.
 * @param bone_name Bone to edit.
 * @return The edit, or `nullptr` when the animation or bone does not exist.
 */
BoneInheritTimelineEdit* ensure_bone_inherit_timeline_edit(
    ProjectData& project,
    const runtime::SkeletonData& effective_skeleton,
    std::string_view animation_name,
    std::string_view bone_name);

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
 *
 * `new_key_interpolation` seeds a newly *inserted* key only; an existing key
 * always keeps the curve it already carries. It defaults to linear, which is
 * the reproducible contract every Agent operation relies on. The shell passes
 * the user's remembered default curve here instead, so the preference is read
 * by the shell and never by this module — the MAR-156 isolation boundary stays
 * one-directional.
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
    const TransformKeyframePatch& patch,
    runtime::Interpolation new_key_interpolation = runtime::Interpolation::linear());

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
 * @brief Relativizes a reference against a project file's directory.
 *
 * The house rule, and the same one `rebase_project_paths` uses: the result is
 * relative when a relative form exists without `../`, and ABSOLUTE otherwise, so
 * a reference never silently starts pointing outside the project folder.
 *
 * Exposed for MAR-188's provenance writer, which has to store project-relative
 * paths and must not reimplement the rule.
 *
 * @param project_path Project file the result is relative to.
 * @param referenced_path Path being stored.
 * @return A project-relative path, or an absolute one when no relative form fits.
 */
std::filesystem::path project_relative_path(
    const std::filesystem::path& project_path,
    const std::filesystem::path& referenced_path);

/**
 * @brief Rewrites project-relative references so they resolve identically from a
 *        new project-file location.
 *
 * Every path a `.marrow` stores is project-relative by design, and `resolve_path`
 * resolves it against the project file's own directory. Moving the project file
 * without rewriting its references therefore changes what they point at, and the
 * written project stops opening. This is the single place that rewrites them.
 *
 * Six families are rebased, and they are exactly the six that are serialized
 * from struct fields: `runtime_assets.skeleton_path`,
 * `runtime_assets.atlas_paths`, `editor_metadata.export_directory`, every
 * `atlas_pack_definitions` entry's `atlas_path` and sprite `image_path`, and
 * MAR-188's `editor_metadata.import_sources->psd` (`source_path` and
 * `layers_directory` -- the per-layer `image_file` is a bare file name, not a
 * path, and is deliberately not a family of its own). Rebasing
 * only some of them is worse than rebasing none: `find_atlas_pack_definition`
 * matches an atlas pack to a runtime atlas by RESOLVED path, so a partial rebase
 * makes the lookup miss and `export_runtime_assets` silently stops packing.
 *
 * The rule is identity-preserving: each reference resolves afterwards to the same
 * absolute file it resolved to before. Empty and absolute references are returned
 * unchanged. `export_directory` rebases by identity like the rest, so exports
 * keep landing where they landed; whether a Save As should instead carry exports
 * along is a UI question owned by MAR-181.
 *
 * Relativization goes through `make_project_relative_path`, the house rule, which
 * returns an ABSOLUTE path whenever the relative form would need `../`. So a Save
 * As into a sibling or subdirectory turns relative references absolute. The
 * project opens either way; the result is simply no longer portable as a folder.
 *
 * MAR-188 added `$.editor.import_sources.psd` as a real project-relative field
 * and it IS registered here, as the sixth family.
 *
 * Paths stored inside `preserved_root` are still round-tripped opaquely and
 * cannot be reached from here, and that limitation is permanent rather than
 * pending: `preserved_root` is by definition whatever this code does not
 * understand, so a rule quantified over "every relative path" is quantified over
 * a set the program cannot enumerate. Any path a future document carries under an
 * unparsed key remains unrebased and silently stale on Save As. Closing that needs
 * a schema-strict loader or a decision that unparsed paths are unsupported --
 * neither of which is a change any single story should make on its own.
 *
 * @param project Project whose references resolve against its current `source_path`.
 * @param new_project_path Location the project file is about to be written to.
 * @return A copy carrying rebased references and `source_path = new_project_path`.
 */
ProjectData rebase_project_paths(
    const ProjectData& project,
    const std::filesystem::path& new_project_path);
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
/** @brief Outcome of a constraint lifecycle primitive. */
struct ConstraintLifecycleResult {
    bool ok{false};
    std::string message;          ///< Empty on success.
    bool used_operation{false};   ///< True when an ordered record was appended.
    bool changed_upsert{false};   ///< True when a `*_constraint_edits` entry was rewritten or erased.
};

/**
 * @brief Renames a constraint, choosing the representation the ownership rule requires.
 *
 * A constraint that lives in the base skeleton cannot be renamed by upsert --
 * the overlay's only verbs are replace-by-name and append -- so it gets an
 * ordered record. A project-only constraint is rewritten in place and gets
 * none. A project upsert that *shadows* a base constraint gets both, because
 * renaming only the upsert would leave the base constraint standing beside the
 * renamed one.
 *
 * Preflight-then-mutate: on any rejection `*project` is untouched and
 * `serialize_project()` is byte-identical.
 *
 * @param project Project to mutate in place on success.
 * @param base_skeleton_document Base runtime skeleton document referenced by the project.
 * @param family Constraint family; identity is `(family, name)`, never an index.
 * @param from Current constraint name.
 * @param to Requested new name; a collision is refused, never auto-suffixed.
 * @return The outcome, with which representation was used.
 */
ConstraintLifecycleResult rename_constraint(
    ProjectData* project,
    const runtime::json::Document& base_skeleton_document,
    ConstraintKind family,
    std::string_view from,
    std::string_view to);

/**
 * @brief Deletes a constraint, choosing the representation the ownership rule requires.
 *
 * Base-backed constraints get an ordered tombstone; project-only constraints
 * have their upsert erased; a shadowing upsert needs both, because erasing only
 * the upsert resurrects the base constraint it was covering.
 *
 * Preflight-then-mutate: on any rejection `*project` is untouched and
 * `serialize_project()` is byte-identical.
 *
 * @param project Project to mutate in place on success.
 * @param base_skeleton_document Base runtime skeleton document referenced by the project.
 * @param family Constraint family; identity is `(family, name)`, never an index.
 * @param name Constraint to remove.
 * @return The outcome, with which representation was used.
 */
ConstraintLifecycleResult delete_constraint(
    ProjectData* project,
    const runtime::json::Document& base_skeleton_document,
    ConstraintKind family,
    std::string_view name);

/**
 * @brief Validates the ordered constraint lifecycle records against a base skeleton.
 *
 * Replays the records front to back over the name set the base document
 * declares, and reports the four causes separately: a source that does not
 * exist, a rename target already taken inside the same family, a name that
 * exists in a different family than the record claims, and a source an earlier
 * record already consumed. Called before either runtime build, so a rejected
 * project produces an error and writes nothing.
 *
 * @param project Project carrying the lifecycle records.
 * @param base_skeleton_document Base runtime skeleton document referenced by the project.
 * @return A located error describing the first fault, or `std::nullopt`.
 */
std::optional<runtime::json::LoadError> validate_constraint_lifecycle_operations(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document);
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
 * @brief Animation names the project authors, after catalog edits and before overlays.
 * @param project Project whose `animation_edits` fold is applied.
 * @param base_skeleton_document Base runtime skeleton document referenced by the project.
 * @return Sorted, unique animation names the project legitimately declares.
 *
 * This is the authority for "does this animation exist", and neither obvious
 * alternative is correct. The **base document** is wrong because an
 * `AnimationEdit{Create}` can declare an animation that exists only in the
 * project. The **materialized `SkeletonData`** is wrong because the overlay
 * merge calls `ensure_object_member(animations, edit.animation_name)`, which
 * *creates* the very phantom animations an orphan detector is looking for — a
 * collector resolving against it reports a clean project, always.
 *
 * The authority is the state `build_runtime_document` is in immediately after
 * `apply_animation_edits` and before the overlay merge. This function lives in
 * `project.cpp` because `apply_animation_edits` is in that file's anonymous
 * namespace and cannot be reached from anywhere else; re-deriving the
 * Create/Rename/Delete/SetDuration/Unknown fold elsewhere would be a second
 * source of truth that drifts the first time an `AnimationEditKind` is added.
 *
 * The whole document root is copied rather than just `animations`, because a
 * `Rename` also rewrites `mixing` references.
 */
std::vector<std::string> authored_animation_names(
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
