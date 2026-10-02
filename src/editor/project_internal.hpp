#pragma once

// Private lifecycle seams; public project.hpp remains the compatibility surface.
#include "marrow/editor/project.hpp"

namespace marrow::editor::project_detail {

bool validate_project_for_save(const ProjectData& project, ProjectSaveError* error_out);

std::optional<runtime::json::LoadError> validate_animation_edit_sequence(
    const ProjectData& project, const runtime::json::Document& base_skeleton_document);

void apply_animation_edits(runtime::json::Value* root, const std::vector<AnimationEdit>& edits);

void apply_constraint_lifecycle_operations(
    runtime::json::Value* root, const std::vector<ConstraintLifecycleOperation>& operations);

// Parse only: no asset I/O. Non-null output is replaced only on success.
std::optional<runtime::json::LoadError> parse_project_document(
    const runtime::json::Document& document, ProjectData* project_out);

std::filesystem::path default_export_binary_path(const std::filesystem::path& skeleton_path);

} // namespace marrow::editor::project_detail
