#pragma once

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

struct ViewportState {
    double pan_x{0.0};
    double pan_y{0.0};
    double zoom{1.0};
    OnionSkinSettings onion_skin{};
    DebugOverlaySettings debug_overlay{};
};

struct ProjectMetadata {
    std::string name;
    std::string active_animation;
    std::vector<std::string> preview_skins;
    std::filesystem::path export_directory{"exports"};
    std::string notes;
    ViewportState viewport{};
};

struct ProjectData {
    std::string marrow_version{"1.0"};
    RuntimeAssetReferences runtime_assets;
    ProjectMetadata editor_metadata;
    std::vector<TransformTimelineEdit> transform_timeline_edits;
    std::vector<MeshDeformTimelineEdit> mesh_deform_timeline_edits;
    std::vector<MeshWeightAttachmentEdit> mesh_weight_attachment_edits;
    std::vector<DrawOrderTimelineEdit> draw_order_timeline_edits;
    std::vector<EventTimelineEdit> event_timeline_edits;
    std::vector<IkConstraintEdit> ik_constraint_edits;
    std::vector<PathConstraintEdit> path_constraint_edits;
    std::vector<TransformConstraintEdit> transform_constraint_edits;
    std::vector<PhysicsConstraintEdit> physics_constraint_edits;
    std::vector<AtlasPackDefinition> atlas_pack_definitions;
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

struct ProjectCommand {
    std::string label;
    ProjectData before_project;
    ProjectData after_project;
    std::string before_serialized;
    std::string after_serialized;
};

class ProjectCommandStack {
public:
    /// @brief Reports whether an undo command is available.
    /// @return `true` when the undo stack is not empty.
    bool can_undo() const;
    /// @brief Reports whether a redo command is available.
    /// @return `true` when the redo stack is not empty.
    bool can_redo() const;
    /// @brief Returns the number of queued undo commands.
    /// @return Undo stack depth.
    std::size_t undo_count() const;
    /// @brief Returns the number of queued redo commands.
    /// @return Redo stack depth.
    std::size_t redo_count() const;
    /// @brief Clears both undo and redo stacks.
    void clear();
    /// @brief Peeks at the next undo command without consuming it.
    /// @return Pointer to the pending undo command, or `nullptr` when none exists.
    const ProjectCommand* peek_undo() const;
    /// @brief Peeks at the next redo command without consuming it.
    /// @return Pointer to the pending redo command, or `nullptr` when none exists.
    const ProjectCommand* peek_redo() const;
    /**
     * @brief Pushes a new command and clears redo history.
     * @param command Command snapshot to append.
     */
    void push(ProjectCommand command);
    /// @brief Moves the top undo command onto the redo stack.
    void commit_undo();
    /// @brief Moves the top redo command back onto the undo stack.
    void commit_redo();

private:
    std::vector<ProjectCommand> undo_commands_;
    std::vector<ProjectCommand> redo_commands_;
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
 * @brief Serializes a project into `.marrow` JSON text.
 * @param project Project to serialize.
 * @return Pretty-printed `.marrow` JSON text.
 */
std::string serialize_project(const ProjectData& project);
/**
 * @brief Creates an undoable command from two project snapshots.
 * @param label User-facing command label.
 * @param before_project Project state before the edit.
 * @param after_project Project state after the edit.
 * @return Command snapshot, or `std::nullopt` when the edit produces no serialized change.
 */
std::optional<ProjectCommand> make_project_command(
    std::string label,
    const ProjectData& before_project,
    const ProjectData& after_project);
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
