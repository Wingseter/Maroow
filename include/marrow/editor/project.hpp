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
    double inertia{0.0};
    double damping{0.0};
    double strength{0.0};
    runtime::AttachmentVertex gravity{};
    runtime::AttachmentVertex wind{};
    double mix{1.0};
};

struct RuntimeAssetReferences {
    std::filesystem::path skeleton_path;
    std::vector<std::filesystem::path> atlas_paths;
};

struct ViewportState {
    double pan_x{0.0};
    double pan_y{0.0};
    double zoom{1.0};
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
    std::vector<DrawOrderTimelineEdit> draw_order_timeline_edits;
    std::vector<EventTimelineEdit> event_timeline_edits;
    std::vector<IkConstraintEdit> ik_constraint_edits;
    std::vector<PathConstraintEdit> path_constraint_edits;
    std::vector<TransformConstraintEdit> transform_constraint_edits;
    std::vector<PhysicsConstraintEdit> physics_constraint_edits;
    std::filesystem::path source_path;

    std::filesystem::path resolve_path(const std::filesystem::path& referenced_path) const;
    std::filesystem::path resolved_skeleton_path() const;
    std::vector<std::filesystem::path> resolved_atlas_paths() const;
    std::filesystem::path resolved_export_skeleton_path() const;
    std::filesystem::path resolved_export_binary_path() const;
    const TransformTimelineEdit* find_transform_timeline_edit(
        std::string_view animation_name,
        std::string_view bone_name,
        TransformTimelineChannel channel) const;
    TransformTimelineEdit* find_transform_timeline_edit(
        std::string_view animation_name,
        std::string_view bone_name,
        TransformTimelineChannel channel);
    const MeshDeformTimelineEdit* find_mesh_deform_timeline_edit(
        std::string_view animation_name,
        std::string_view slot_name,
        std::string_view attachment_name) const;
    MeshDeformTimelineEdit* find_mesh_deform_timeline_edit(
        std::string_view animation_name,
        std::string_view slot_name,
        std::string_view attachment_name);
    const DrawOrderTimelineEdit* find_draw_order_timeline_edit(
        std::string_view animation_name) const;
    DrawOrderTimelineEdit* find_draw_order_timeline_edit(
        std::string_view animation_name);
    const EventTimelineEdit* find_event_timeline_edit(
        std::string_view animation_name) const;
    EventTimelineEdit* find_event_timeline_edit(
        std::string_view animation_name);
    const IkConstraintEdit* find_ik_constraint_edit(std::string_view name) const;
    IkConstraintEdit* find_ik_constraint_edit(std::string_view name);
    const PathConstraintEdit* find_path_constraint_edit(std::string_view name) const;
    PathConstraintEdit* find_path_constraint_edit(std::string_view name);
    const TransformConstraintEdit* find_transform_constraint_edit(std::string_view name) const;
    TransformConstraintEdit* find_transform_constraint_edit(std::string_view name);
    const PhysicsConstraintEdit* find_physics_constraint_edit(std::string_view name) const;
    PhysicsConstraintEdit* find_physics_constraint_edit(std::string_view name);
};

struct ProjectLoadResult {
    std::shared_ptr<ProjectData> project;
    std::shared_ptr<const runtime::json::Document> base_skeleton_document;
    std::shared_ptr<const runtime::SkeletonData> skeleton_data;
    std::vector<std::shared_ptr<const runtime::AtlasData>> atlas_data;
    std::optional<runtime::json::LoadError> error;

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

    std::string format() const;
};

struct ProjectSaveResult {
    std::shared_ptr<ProjectData> project;
    std::optional<ProjectSaveError> error;

    explicit operator bool() const {
        return !error.has_value();
    }
};

struct ProjectRuntimeResult {
    std::shared_ptr<const runtime::SkeletonData> skeleton_data;
    std::optional<runtime::json::LoadError> error;

    explicit operator bool() const {
        return skeleton_data != nullptr;
    }
};

struct ProjectExportError {
    std::filesystem::path path;
    std::string message;

    std::string format() const;
};

struct ProjectExportResult {
    std::filesystem::path path;
    std::vector<std::filesystem::path> atlas_paths;
    std::vector<std::filesystem::path> texture_paths;
    std::optional<std::filesystem::path> binary_path;
    std::optional<ProjectExportError> error;

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

ProjectData create_minimal_project(const MinimalProjectOptions& options);
ProjectLoadResult load_project(const runtime::json::Document& document);
ProjectLoadResult load_project(const std::filesystem::path& path);
ProjectRuntimeResult build_project_runtime(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document);
ProjectSaveResult save_project(const ProjectData& project, const std::filesystem::path& path);
ProjectExportResult export_runtime_assets(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document,
    const ProjectExportOptions& options = {});
ProjectExportResult export_runtime_skeleton(
    const ProjectData& project,
    const runtime::json::Document& base_skeleton_document,
    const std::filesystem::path& output_path = {});

} // namespace marrow::editor
